import os, sys; sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # put ref/ on the path
"""Parser tests: SPEC-04 sections 5 to 12 and the grammar of section 18."""
import unittest

from cint_ref import parser as P
from cint_ref.faults import CompileError, Refused


def parse(text):
    return P.parse_module(text, "t.ci")


def expr(text):
    """Parse `text` as the initializer of a declaration and return the expression."""
    m = parse("I64 __v = %s;" % text)
    return m.statements[0].init


def code_of(text):
    try:
        parse(text)
    except CompileError as e:
        d = e.diagnostic
        return d.code, (d.position.line, d.position.column)
    raise AssertionError("no compile error for %r" % text)


def shape(e):
    """A compact s-expression of an expression tree."""
    if isinstance(e, P.Lit):
        return e.value
    if isinstance(e, P.BoolLit):
        return e.value
    if isinstance(e, P.Name):
        return e.name
    if isinstance(e, P.Unary):
        return (e.op, shape(e.operand))
    if isinstance(e, (P.Binary, P.Compare, P.Logical)):
        return (e.op, shape(e.left), shape(e.right))
    if isinstance(e, P.Convert):
        return (e.op, shape(e.operand), e.target.name)
    if isinstance(e, P.Cond):
        return ("?", shape(e.cond), shape(e.a), shape(e.b))
    if isinstance(e, P.Call):
        return ("call", e.callee) + tuple(shape(a.value) for a in e.args)
    if isinstance(e, P.TypeArg):
        return ("type", e.name)
    if isinstance(e, P.Field):
        return (".", shape(e.obj), e.field)
    if isinstance(e, P.TypeProp):
        return (e.type_name, e.prop)
    raise AssertionError(type(e))


class Expressions(unittest.TestCase):
    def test_precedence_7_1(self):
        self.assertEqual(shape(expr("a + b * c")), ("+", "a", ("*", "b", "c")))
        self.assertEqual(shape(expr("a as I64 + b")), ("+", ("as", "a", "I64"), "b"))
        self.assertEqual(shape(expr("flags & MASK == 0")), ("==", ("&", "flags", "MASK"), 0))
        self.assertEqual(shape(expr("a +% b + c")), ("+", ("+%", "a", "b"), "c"))
        self.assertEqual(shape(expr("x << 2 >> 1")), (">>", ("<<", "x", 2), 1))
        self.assertEqual(shape(expr("(a < b && c != d) || e")), ("||", ("&&", ("<", "a", "b"), ("!=", "c", "d")), "e"))

    def test_conditional_is_right_associative(self):
        self.assertEqual(shape(expr("a ? 1 : b ? 2 : 3")), ("?", "a", 1, ("?", "b", 2, 3)))

    def test_negative_literal_3_5_rule_4(self):
        e = expr("-128")
        self.assertIsInstance(e, P.Lit)
        self.assertEqual(e.value, -128)
        self.assertEqual(shape(expr("-(128)")), ("-", 128))
        self.assertEqual(shape(expr("-x")), ("-", "x"))
        self.assertEqual(shape(expr("a -1")), ("-", "a", 1))
        self.assertEqual(shape(expr("-1 as% U64")), ("as%", -1, "U64"))

    def test_calls_type_args_and_properties(self):
        self.assertEqual(shape(expr("div_round(7, 2, half_even)")), ("call", "div_round", 7, 2, "half_even"))
        self.assertEqual(shape(expr("muldiv(I64, a, b, c, floor)")), ("call", "muldiv", ("type", "I64"), "a", "b", "c", "floor"))
        self.assertEqual(shape(expr("I64.max")), ("I64", "max"))
        self.assertEqual(shape(expr("p.x + 1")), ("+", (".", "p", "x"), 1))

    def test_named_arguments(self):
        e = expr("Body(id = 7, flags = 0)")
        self.assertEqual([a.name for a in e.args], ["id", "flags"])

    def test_positions_are_operator_tokens(self):
        e = expr("a + b")
        self.assertEqual((e.pos.line, e.pos.column), (1, 13))
        self.assertEqual((e.start.line, e.start.column), (1, 11))


class ParenthesesRequired(unittest.TestCase):
    """SPEC-04 7.2 (C2101) and 7.3 (C2102)."""

    def test_rejected(self):
        for src in ["a && b || c", "a || b && c", "1 << n + 1", "a + b << 2", "a & b + 1",
                    "a & b | c", "a ^ b & c", "-x as% U8", "~x as I8", "!b as I64"]:
            with self.subTest(src=src):
                self.assertEqual(code_of("I64 __v = %s;" % src)[0], "C2101")

    def test_accepted(self):
        for src in ["(a && b) || c", "1 << (n + 1)", "(a & b) | c", "a | b | c",
                    "(-x) as% U8", "-(x as% U8)", "-1 as% U64", "a & (b + 1)", "a +% b + c"]:
            with self.subTest(src=src):
                expr(src)

    def test_chained_comparison(self):
        self.assertEqual(code_of("Bool b = a < b < c;"), ("C2102", (1, 10)))
        self.assertEqual(code_of("Bool b = 0 <= i < n;")[0], "C2102")
        parse("Bool b = (a < b) == c;")


class Statements(unittest.TestCase):
    def test_function_and_if_chain(self):
        m = parse("I64 f(I64 a, U8 b) {\n if (a > 0) { return 1; } else if (a < 0) { return -1; } else { return 0; }\n}\n")
        (f,) = m.functions
        self.assertEqual((f.name, f.result.name, [(p.type.name, p.name) for p in f.params]),
                         ("f", "I64", [("I64", "a"), ("U8", "b")]))
        s = f.body.stmts[0]
        self.assertIsInstance(s, P.If)
        self.assertIsInstance(s.else_, P.If)
        self.assertIsInstance(s.else_.else_, P.Block)

    def test_loops(self):
        m = parse("void f() {\n"
                  " for (I64 i = 0; i < 10; i++) { }\n"
                  " for i in 0..10 { }\n"
                  " for j in 1..=10 by 2 { }\n"
                  " outer: while (true) { break outer; }\n"
                  "}\n")
        a, b, c, d = m.functions[0].body.stmts
        self.assertIsInstance(a, P.ForC)
        self.assertIsInstance(b, P.ForRange)
        self.assertFalse(b.inclusive)
        self.assertTrue(c.inclusive)
        self.assertEqual(shape(c.step), 2)
        self.assertIsInstance(d, P.While)
        self.assertEqual(d.label, "outer")

    def test_switch_8_5(self):
        m = parse("void f(I64 code) {\n switch (code) {\n  case 0:\n   return;\n"
                  "  case 1..=4, 9:\n   return;\n  default:\n   return;\n }\n}\n")
        sw = m.functions[0].body.stmts[0]
        self.assertEqual(len(sw.cases), 2)
        self.assertEqual([(shape(lo), hi and shape(hi)) for lo, hi in sw.cases[1].items], [(1, 4), (9, None)])
        self.assertIsNotNone(sw.default)

    def test_switch_errors(self):
        self.assertEqual(code_of("void f(I64 c) {\n switch (c) {\n  case 1..5:\n   return;\n  default:\n   return;\n }\n}\n"),
                         ("C4022", (3, 8)))
        self.assertEqual(code_of("void f(I64 c) {\n switch (c) {\n  case 1:\n  case 2:\n   return;\n  default:\n   return;\n }\n}\n"),
                         ("C4023", (3, 3)))

    def test_test_blocks_and_assert(self):
        m = parse('test "floor" {\n assert(-7 / 2 == -4);\n assert(x == 1, "x is {x}");\n}\n'
                  'test "narrow" expect_fault E_NARROW { I64 big = 300; }\n')
        t1, t2 = m.tests
        self.assertEqual((t1.name, t2.name, t2.expect_fault), ("floor", "narrow", "E_NARROW"))
        self.assertIsInstance(t1.body.stmts[0], P.Assert)
        self.assertEqual(code_of("void f() { expect_fault E_X; }")[0], "C3021")

    def test_print_statement_9_1(self):
        m = parse('I64 x = 5;\n"x={x} " "y={x + 1 =}:{x:>6}\\n";\n')
        pr = m.statements[1]
        self.assertIsInstance(pr, P.Print)
        holes = [p for p in pr.parts if isinstance(p, P.HoleExpr)]
        self.assertEqual([h.name for h in holes], [None, "x + 1 ", None])
        self.assertEqual(holes[2].spec, ">6")
        self.assertEqual((holes[1].expr.pos.line, holes[1].expr.pos.column), (2, 16))

    def test_hole_conversions_and_ternary(self):
        m = parse('"{n!bits:x}{(ok ? a : b)}";')
        holes = [p for p in m.statements[0].parts if isinstance(p, P.HoleExpr)]
        self.assertEqual((holes[0].conv, holes[0].spec), ("bits", "x"))
        self.assertEqual(code_of('"{ok ? a : b}";')[0], "C1035")

    def test_struct_8_and_constructor(self):
        m = parse("struct P { I64 x; I32 y; }\nP p = P(1, 2);\np.x = 3;\n")
        (s,) = m.structs
        self.assertEqual([(f.type.name, f.name) for f in s.fields], [("I64", "x"), ("I32", "y")])
        self.assertIsInstance(m.statements[1], P.Assign)

    def test_statement_errors(self):
        self.assertEqual(code_of("void f(Bool c) { if (c); }")[0], "C1040")
        self.assertEqual(code_of("void f(Bool c) { if c { } }")[0], "C1043")
        self.assertEqual(code_of("void f(I64 x) { if (x == 1) x = 2; }")[0], "C1043")
        self.assertEqual(code_of("void f(I64 x) { if (x = 5) { } }")[0], "C1041")
        self.assertEqual(code_of("void f(I64 i) { g(i++); }")[0], "C1042")
        self.assertEqual(code_of("void f(I64 x) { x + 1; }")[0], "C4012")
        self.assertEqual(code_of("void f() { ; }")[0], "C1040")

    def test_reserved_identifier_c1010(self):
        self.assertEqual(code_of("I64 Q32 = 1;"), ("C1010", (1, 5)))
        self.assertEqual(code_of("I64 I7 = 1;")[0], "C1010")

    def test_unknown_fault_name_c1011(self):
        self.assertEqual(code_of('test "t" expect_fault E_NOPE { }')[0], "C1011")

    def test_crlf_source_gives_lf_positions(self):
        from cint_ref.lexer import decode_source
        lf = b"I64 f(I64 a) {\n\tI64 b = a +% 1;\n    return b * 2;\n}\n"
        def positions(data):
            m = P.parse_module(decode_source(data, "t.ci"), "t.ci")
            body = m.functions[0].body.stmts
            return [(str(body[0].init.pos), str(body[0].init.start)), str(body[1].value.pos)]
        self.assertEqual(positions(lf), [("t.ci:2:12", "t.ci:2:10"), "t.ci:3:14"])
        self.assertEqual(positions(lf.replace(b"\n", b"\r\n")), positions(lf))
        self.assertEqual(positions(lf.rstrip(b"\n")), positions(lf))

    def test_outside_scalar_surface_is_refused(self):
        # Arrays and `import` are implemented since slice 2 task 2.8; ranks 2 to 4, slices and
        # kernels since box 09; error sets, error unions and `defer` since box 12.
        for src in ["Q16.16 q = 0.5;", "enum E : U8 { a }", 'profile "cint-boot-1";']:
            with self.subTest(src=src):
                with self.assertRaises(Refused):
                    parse(src)

    def test_surface_before_box09_is_kept(self):
        # The front and check suites of compiler/tests parse with box09=False: the box 09
        # constructs are refused there, at the same tokens as before box 09.
        for src in ["I64[2, 2] m;", "void f() { I64[4] a; I64 x = a[0..2]; }", "kernel k[n](in I64[n] a) { }",
                    "I64[2] a = [1, 2];", "void f(in I64[_] a) { in I64[_] v = a; }"]:
            with self.subTest(src=src):
                parse(src)
                with self.assertRaises(Refused):
                    P.parse_module(src, "t.ci", box09=False)

    def test_surface_before_box12_is_kept(self):
        # The front and check suites of compiler/tests parse with box12=False as well: the box 12
        # constructs are refused there, at the same tokens as before box 12.
        for src, column in [("error E { a, b }", 1), ("error E = A | B;", 1), ("void f() { defer g(); }", 12),
                            ("I64 f() { return try g(); }", 18), ("I64 f() { return g() catch 0; }", 22),
                            ("I64 f(I64 x) { return x as? I8 catch 0; }", 25), ("void f() { E e = .a; }", 18)]:
            with self.subTest(src=src):
                parse(src)
                with self.assertRaises(Refused) as r:
                    P.parse_module(src, "t.ci", box12=False)
                self.assertEqual(r.exception.position.column, column)
        # `errdefer` is reserved like `defer` (SPEC-04 LS-18, LS-189), and an identifier there, as
        # before its reservation.
        self.assertEqual(code_of("I64 errdefer = 1;"), code_of("I64 defer = 1;"))
        P.parse_module("I64 errdefer = 1;", "t.ci", box12=False)


if __name__ == "__main__":
    unittest.main()
