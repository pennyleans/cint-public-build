import os, sys; sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # put ref/ on the path
"""Executor tests: whole programs, faults, fuel, depth and printing."""
import hashlib
import unittest

from cint_ref.exec import run_program
from cint_ref.types import Value

M = 9223372036854775807
m = -9223372036854775808


def run(src, entry=None, fuel=None, depth=256, args=(), path="t.ci", test=None):
    data = src.encode("utf-8") if isinstance(src, str) else src
    return run_program(data, path, entry, fuel, depth, args=args, test=test)


def value_of(src, name="r"):
    """Run a script and return the value of its local `r` through a print hole."""
    o = run(src + '\n"{%s}";\n' % name)
    assert o.kind == "value", (o.kind, o.diagnostic, o.record, o.message)
    return o.stdout.decode()


class Conf01(unittest.TestCase):
    SRC = ("// conformance/arith/add_i64_overflow.ci\n"
           "test \"add overflow at I64 max\" {\n"
           "    I64 a = 9223372036854775807;\n"
           "    I64 b = 1;\n"
           "    I64 c = a + b;\n"
           "    \"c={c}\\n\";\n"
           "}\n")

    def test_record(self):
        o = run(self.SRC, path="arith/add_i64_overflow.ci")
        self.assertEqual(o.kind, "fault")
        self.assertEqual(o.stdout, b"")
        self.assertEqual(o.fuel, 1)          # interim decision OQ-17: a test block charges one unit
        self.assertEqual(o.record.to_expect_lines(), [
            "fault.code E_OVERFLOW",
            "fault.operation add.checked.i64",
            "fault.operand I64 9223372036854775807",
            "fault.operand I64 1",
            "fault.exact 9223372036854775808",
            "fault.limit I64 9223372036854775807",
            "fault.position arith/add_i64_overflow.ci:5:15",
            "fault.revision self",
            "fault.address none",
            "fault.stack-depth 0",
        ])

    def test_crlf_gives_the_same_record(self):
        lf = run(self.SRC, path="a.ci")
        crlf = run(self.SRC.replace("\n", "\r\n"), path="a.ci")
        self.assertEqual(lf.record.to_expect_lines(), crlf.record.to_expect_lines())
        nofinal = run(self.SRC.rstrip("\n"), path="a.ci")
        self.assertEqual(lf.record.to_expect_lines(), nofinal.record.to_expect_lines())

    def test_bom_is_c1002(self):
        o = run(b"\xef\xbb\xbf" + self.SRC.encode())
        self.assertEqual((o.kind, o.diagnostic.code, str(o.diagnostic.position)), ("compile-error", "C1002", "t.ci:1:1"))

    def test_legacy_extension_is_c1001(self):
        o = run(self.SRC, path="x.cint")
        self.assertEqual((o.kind, o.diagnostic.code), ("compile-error", "C1001"))


class Seed08(unittest.TestCase):
    """SPEC-09 SEED-08 boundary cases."""

    def diag(self, src, **kw):
        o = run(src, **kw)
        self.assertEqual(o.kind, "compile-error", (o.kind, o.record, o.message))
        return o.diagnostic

    def test_literal_out_of_range_is_c2003(self):
        d = self.diag("void f() {\n    I16 h = 40000 - 30000;\n}\n")
        self.assertEqual((d.code, str(d.position)), ("C2003", "t.ci:2:13"))

    def test_constant_typed_first(self):
        d = self.diag("const I8 X = 100 + 100 - 150;\n")
        self.assertEqual((d.code, str(d.position)), ("C6001", "t.ci:1:18"))
        self.assertEqual((d.fault.code, d.fault.operation, d.fault.exact), ("E_OVERFLOW", "add.checked.i8", 200))
        self.assertEqual([o.render() for o in d.fault.operands], ["I8 100", "I8 100"])

    def test_i64_constant_overflow(self):
        d = self.diag("void f() {\n    I64 c = 9223372036854775807 + 1;\n}\n")
        self.assertEqual((d.code, d.fault.code, d.fault.operation, d.fault.exact),
                         ("C6001", "E_OVERFLOW", "add.checked.i64", 9223372036854775808))

    def test_same_operands_at_run_time(self):
        o = run("I64 f() {\n    I64 a = 9223372036854775807;\n    I64 c = a + 1;\n    return c;\n}\n", entry="f")
        self.assertEqual(o.kind, "fault")
        self.assertEqual((o.record.code, o.record.operation, o.record.exact), ("E_OVERFLOW", "add.checked.i64", 9223372036854775808))
        self.assertEqual([x.render() for x in o.record.operands], ["I64 9223372036854775807", "I64 1"])

    def test_wrapping_constant(self):
        o = run("I64 f() {\n    I8 p = 100 +% 100;\n    return p as I64;\n}\n", entry="f")
        self.assertEqual((o.kind, o.value), ("value", Value("I64", -56)))

    def test_divide_by_zero_constant(self):
        d = self.diag("void f() {\n    I64 d = 1 / 0;\n}\n")
        self.assertEqual((d.code, d.fault.code, str(d.position)), ("C6001", "E_DIV_ZERO", "t.ci:2:15"))

    def test_constant_by_form_even_in_dead_code(self):
        d = self.diag("void f() {\n    if (false) { I64 d = 1 / 0; }\n}\n")
        self.assertEqual((d.code, d.fault.code), ("C6001", "E_DIV_ZERO"))


class ContextTyping(unittest.TestCase):
    """SPEC-01 3.2 examples and SPEC-04 4.2."""

    def code(self, src):
        o = run(src + '\n"x";\n')          # a script, so that declarations are locals
        self.assertEqual(o.kind, "compile-error", (o.kind, o.record))
        return o.diagnostic.code

    def test_values(self):
        self.assertEqual(value_of("I8 r = -128;"), "-128")
        self.assertEqual(value_of("U64 r = -1 as% U64;"), "18446744073709551615")
        self.assertEqual(value_of("I32 r = 0xFFFFFFFF as% I32;"), "-1")
        self.assertEqual(value_of("I64 r = 3 * 1_000_000_000_000 / 7;"), "428571428571")
        self.assertEqual(value_of("I8 r = 100 +% 100;"), "-56")
        self.assertEqual(value_of("const I8 K = 100;\nI16 r = (K as I16) + (K as I16);"), "200")
        self.assertEqual(value_of("U8 k = 40;\nI64 r = 1 << k;"), "1099511627776")      # fixture 109
        self.assertEqual(value_of("U8 b8 = 1;\nI64 k = 3;\nU8 r = b8 << k;"), "8")
        self.assertEqual(value_of("I64 r = 7 / 2;"), "3")
        self.assertEqual(value_of("I64 r = -7 / 2;"), "-4")
        self.assertEqual(value_of("U8 r = 200 +% 100;"), "44")
        self.assertEqual(value_of("I64 one = 1;\nI64 r = one <<% 63;"), str(m))
        self.assertEqual(value_of("I16 r = -32768;"), "-32768")
        self.assertEqual(value_of("U64 r = 0xFFFF_FFFF_FFFF_FFFF;"), "18446744073709551615")
        self.assertEqual(value_of("I64 r = I64.max;"), str(M))
        self.assertEqual(value_of("U8 r = 'a';"), "97")

    def test_compile_errors(self):
        self.assertEqual(self.code("I8 b = 128;"), "C2003")
        self.assertEqual(self.code("U8 c = -1;"), "C2003")
        self.assertEqual(self.code("I32 e = 0xFFFFFFFF;"), "C2003")
        self.assertEqual(self.code("I64 d = 0xFFFF_FFFF_FFFF_FFFF;"), "C2003")
        self.assertEqual(self.code("U8 c = '\\u{100}';"), "C2003")
        self.assertEqual(self.code("const I8 K = 100;\nI16 z = K + K;"), "C2001")
        self.assertEqual(self.code("I8 x = 0;\nI8 y = x + 300;"), "C2003")
        self.assertEqual(self.code("I8 x = 0;\nBool y = x < 1000;"), "C2003")
        self.assertEqual(self.code("const I64 LIMIT = 1 << 40;\nI32 small = LIMIT;"), "C2001")
        self.assertEqual(self.code("I64 x = 1.5;"), "C2004")
        self.assertEqual(self.code("I32 a = 1;\nI64 b = 2;\nI64 c = b + a;"), "C2001")

    def test_c6001_cases(self):
        o = run("I32 v = 2_000_000_000 * 2 / 4;")
        self.assertEqual((o.diagnostic.code, o.diagnostic.fault.code, o.diagnostic.fault.exact),
                         ("C6001", "E_OVERFLOW", 4000000000))
        o = run("I64 s = 1 << 100 >> 90;")
        self.assertEqual((o.diagnostic.code, o.diagnostic.fault.code, o.diagnostic.fault.operands[1].value),
                         ("C6001", "E_SHIFT", 100))
        o = run("I16 m2 = -(1 << 15);")
        self.assertEqual((o.diagnostic.fault.code, o.diagnostic.fault.operation, o.diagnostic.fault.exact),
                         ("E_OVERFLOW", "shl.checked.i16", 32768))
        o = run("I64 bad = 1 << -1;")
        self.assertEqual((o.diagnostic.fault.code, o.diagnostic.fault.operands[1].value), ("E_SHIFT", -1))

    def test_run_time_faults(self):
        o = run("I32 x = 7;\nI32 y = x + 2_147_483_647;\n\"x\";")
        self.assertEqual((o.kind, o.record.code, str(o.record.position)), ("fault", "E_OVERFLOW", "t.ci:2:11"))
        o = run("U8 b8 = 1;\nU8 far = b8 << 300;\n\"x\";")
        self.assertEqual((o.kind, o.record.code, o.record.limit.value), ("fault", "E_SHIFT", 7))

    def test_conversion_of_a_literal_value(self):
        self.assertEqual(value_of("I8 r = 300 as% I8;"), "44")
        o = run("I8 x = 300 as I8;\n\"x\";")
        d = o.diagnostic
        self.assertEqual((d.code, d.fault.code, d.fault.operation, [(x.type, x.value) for x in d.fault.operands]),
                         ("C6001", "E_NARROW", "unassigned", [("Z", 300)]))       # ref/OPEN.md REF-OQ-03

    def test_negative_literal_ignores_spacing(self):
        # D-21 option 1 (SPEC-04 LS-28): a `-` before a literal forms one negative literal.
        self.assertEqual(value_of("I8 a = - 128;\nI64 r = a as I64;"), "-128")
        self.assertEqual(value_of("I8 a = -/* c */128;\nI64 r = a as I64;"), "-128")
        self.assertEqual(self.code("I8 a = -(128);"), "C2003")                      # parentheses: negation

    def test_compound_and_decrement_faults(self):
        o = run("I64 x = 1;\nx <<= 70;\n\"x\";")
        self.assertEqual((o.record.code, o.record.operation, str(o.record.position)), ("E_SHIFT", "shl.checked.i64", "t.ci:2:3"))
        o = run("U8 u = 0;\nu--;\n\"x\";")
        self.assertEqual((o.record.code, o.record.operation, o.record.exact, str(o.record.position)),
                         ("E_OVERFLOW", "sub.checked.u8", -1, "t.ci:2:2"))
        self.assertEqual(value_of("I64 r = 5;\nr += 2;\nr *%= 3;\nr -= 1;"), "20")

    def test_range_bounds_must_be_i64_fixture_110(self):
        self.assertEqual(self.code("I32 n32 = 10;\nfor i in 0..n32 { }"), "C2001")
        self.assertEqual(value_of("I32 n32 = 10;\nI64 r = 0;\nfor i in 0..(n32 as I64) { r += i; }"), "45")


class Order(unittest.TestCase):
    def test_left_operand_first_fixture_92(self):
        o = run("I64 f(I64 x, I64 y) { return (x + 1) / y; }", entry="f", args=[Value("I64", M), Value("I64", 0)])
        self.assertEqual((o.record.code, str(o.record.position)), ("E_OVERFLOW", "t.ci:1:33"))

    def test_arguments_left_to_right(self):
        src = ("I64 g() { I64 k = 64; return 1 << k; }\n"
               "I64 h(I64 a, I64 b) { return a + b; }\n"
               "I64 f(I64 z) { return h(g(), 1 / z); }\n")
        o = run(src, entry="f", args=[Value("I64", 0)])
        self.assertEqual((o.record.code, o.record.operation, len(o.record.stack)), ("E_SHIFT", "shl.checked.i64", 1))
        self.assertEqual([str(p) for p in o.record.stack], ["t.ci:3:25"])

    def test_stores_of_completed_statements_remain_fixture_108(self):
        src = ("I64 counter = 0;\n"
               "I64 g() { counter = counter + 1; return 5; }\n"
               "void step(I64 z) {\n"
               "    I64 y = 7;\n"
               "    y = g() + 1 / z;\n"
               "}\n")
        o = run(src, entry="step", args=[Value("I64", 0)])
        self.assertEqual((o.kind, o.record.code), ("fault", "E_DIV_ZERO"))
        self.assertEqual(o.globals["counter"], 1)


class Fuel(unittest.TestCase):
    """SPEC-01 10.2 (fuel-v1) and fixture 95."""

    SRC = ("I64 total(I64 n) {\n"
           "    I64 s = 0;\n"
           "    for (I64 i = 0; i < n; i++) {\n"
           "        s = s + i;\n"
           "    }\n"
           "    return s;\n"
           "}\n")

    def test_one_unit_per_call_and_iteration(self):
        o = run(self.SRC, entry="total", args=[Value("I64", 3)])
        self.assertEqual((o.kind, o.value, o.fuel), ("value", Value("I64", 3), 4))

    def test_exhaustion_fixture_95(self):
        o = run(self.SRC, entry="total", fuel=3, args=[Value("I64", 3)])
        self.assertEqual((o.kind, o.fuel), ("fault", 3))
        r = o.record
        self.assertEqual((r.code, r.operation, r.limit, r.operands, r.exact), ("E_FUEL", "fuel.charge", Value("I64", 3), (), None))

    def test_zero_allowance_faults_at_entry(self):
        o = run(self.SRC, entry="total", fuel=0, args=[Value("I64", 3)])
        self.assertEqual((o.record.code, o.fuel), ("E_FUEL", 0))

    def test_while_and_range_loops(self):
        o = run("I64 n = 0;\nwhile (n < 5) { n += 1; }\nfor i in 0..3 { }\nfor j in 1..=2 { }\n")
        self.assertEqual(o.fuel, 1 + 5 + 3 + 2)

    def test_calls_charge(self):
        o = run("I64 one() { return 1; }\nI64 a = one() + one();\n\"x\";\n")
        self.assertEqual(o.fuel, 3)


class Depth(unittest.TestCase):
    def test_fixture_116(self):
        o = run("I64 f(I64 n) { return f(n + 1); }\n", entry="f", depth=4, args=[Value("I64", 0)])
        r = o.record
        self.assertEqual((r.code, r.operation, r.limit, o.fuel, len(r.stack)), ("E_DEPTH", "call.enter", Value("I64", 4), 4, 3))
        self.assertEqual(str(r.position), "t.ci:1:23")

    def test_deep_recursion_within_limit(self):
        src = "I64 f(I64 n) { if (n == 0) { return 0; } return f(n - 1) + 1; }\n"
        o = run(src, entry="f", depth=4096, args=[Value("I64", 4095)])
        self.assertEqual((o.kind, o.value), ("value", Value("I64", 4095)))
        o = run(src, entry="f", depth=4096, args=[Value("I64", 1000000)])
        self.assertEqual((o.record.code, o.fuel), ("E_DEPTH", 4096))


class Assertions(unittest.TestCase):
    def test_e_assert_with_operands(self):
        o = run('test "t" {\n    I64 x = 1;\n    assert(x + 0 == 2);\n}\n')
        r = o.record
        self.assertEqual((r.code, [x.render() for x in r.operands], str(r.position)), ("E_ASSERT", ["I64 1", "I64 2"], "t.ci:3:12"))
        self.assertEqual((r.operation, r.exact, r.limit), ("assert.checked.bool", None, None))   # SPEC-01 IM-134

    def test_passing_assert(self):
        o = run('test "t" {\n    assert(-7 / 2 == -4);\n    assert(-7 % 2 == 1);\n}\n')
        self.assertEqual((o.kind, o.fuel), ("value", 1))

    def test_non_comparison_has_no_operands(self):
        o = run('test "t" {\n    Bool b = false;\n    assert(b);\n}\n')
        self.assertEqual((o.record.code, o.record.operands), ("E_ASSERT", ()))


class Printing(unittest.TestCase):
    """SPEC-04 9.5 rows for integers and Bool."""

    def out(self, decls, holes):
        o = run(decls + '\n"' + holes + '";\n')
        self.assertEqual(o.kind, "value", (o.diagnostic, o.record, o.message))
        return o.stdout.decode("utf-8")

    def test_rows(self):
        rows = [
            ("I64 n = -42;", "{n}", "-42"),
            ("I64 n = 0;", "{n:+}", "+0"),
            ("I64 n = -42;", "{n:08}", "-0000042"),
            ("I64 n = 42;", "{n:>6}", "    42"),
            ("I64 n = 42;", "{n:*^7}", "**42***"),
            ("I64 n = 1234567;", "{n:_}", "1_234_567"),
            ("I64 n = 1234567;", "{n:,}", "1,234,567"),
            ("I64 n = 255;", "{n:x}", "ff"),
            ("I64 n = 255;", "{n:#X}", "0xFF"),
            ("I64 n = -255;", "{n:x}", "-ff"),
            ("U32 n = 0xDEADBEEF;", "{n:#_x}", "0xdead_beef"),
            ("I8 n = -1;", "{n!bits:x}", "ff"),
            ("I8 n = -128;", "{n!bits:#b}", "0b10000000"),
            ("I64 n = -1;", "{n!bits:x}", "ffffffffffffffff"),
            ("I64 n = 8;", "{n:t}", "0tP0N"),
            ("I64 n = -8;", "{n:t}", "0tN0P"),
            ("I64 n = 0;", "{n:t}", "0t0"),
            ("I64 n = 233;", "{n:c}", "\u00e9"),
            ("I64 c = 123456;", "{c:/100}", "1234.56"),
            ("I64 c = -5;", "{c:/100}", "-0.05"),
            ("I64 c = I64.min;", "{c:/100}", "-92233720368547758.08"),
            ("U32 w = 0x12345678;", "{w!lanes(U8):02x}", "[78, 56, 34, 12]"),
            ("Bool ok = true;", "{ok}", "true"),
            ("I64 x = 5;", "{x=}", "x=5"),
            ("I64 x = 6;", "{x + 1 =}", "x + 1 =7"),
            ("I64 x = 1;", "{{ and }}", "{ and }"),
            ("I64 x = 1;", "a\\tb\\n", "a\tb\n"),
            ("I64 n = 255;", "{n:o} {n:#o} {n:b} {n:d}", "377 0o377 11111111 255"),
            ("I64 n = 5;", "{n: }", " 5"),
        ]
        for decls, holes, expected in rows:
            with self.subTest(holes=holes):
                self.assertEqual(self.out(decls, holes), expected)

    def test_literal_hole_is_i64(self):
        self.assertEqual(self.out("", "{9223372036854775807}"), "9223372036854775807")

    def test_spec_not_applicable_c1037(self):
        for decls, hole in [("I64 n = 8;", "{n:#t}"), ("I64 n = 1;", "{n:.3}"), ("I64 n = 1;", "{n:,x}"),
                            ("Bool b = true;", "{b:x}"), ("I64 n = 1;", "{n:+t}")]:
            with self.subTest(hole=hole):
                o = run(decls + '\n"' + hole + '";\n')
                self.assertEqual((o.kind, o.diagnostic.code), ("compile-error", "C1037"))

    def test_fault_in_hole_writes_nothing_from_the_statement(self):
        o = run('I64 x = 9223372036854775807;\n"before\\n";\n"a{x}b{x + 1}c\\n";\n')
        self.assertEqual((o.kind, o.stdout), ("fault", b"before\n"))

    def test_stdout_digest(self):
        o = run('"hello\\n";\n')
        self.assertEqual(hashlib.sha256(o.stdout).hexdigest(), hashlib.sha256(b"hello\n").hexdigest())


class ControlFlow(unittest.TestCase):
    CLASSIFY = ("const I32 TK_NONE = 0;\nconst I32 TK_IDENT = 1;\nconst I32 TK_INT = 2;\nconst I32 TK_PUNCT = 3;\n"
                "I32 classify(U8 c) {\n"
                "    switch (c) {\n"
                "        case 'a'..='z', 'A'..='Z', '_':\n"
                "            return TK_IDENT;\n"
                "        case '0'..='9':\n"
                "            return TK_INT;\n"
                "        case '!'..='/', ':'..='@', '['..='^', '{'..='~':\n"
                "            return TK_PUNCT;\n"
                "        default:\n"
                "            return TK_NONE;\n"
                "    }\n"
                "}\n")

    def test_switch_ranges_spec09_5_4(self):
        for arg, expected in ((ord("9"), 2), (ord("/"), 3), (ord("q"), 1), (ord(" "), 0), (ord("_"), 1)):
            o = run(self.CLASSIFY, entry="classify", args=[Value("U8", arg)])
            self.assertEqual(o.value, Value("I32", expected), chr(arg))

    def test_switch_checks(self):
        self.assertEqual(run("void f(U8 c) { switch (c) { case 0..=127: return; } }").diagnostic.code, "C4020")
        o = run("I64 f(U8 c) { switch (c) { case 0..=127: return 1; case 128..=255: return 2; } }", entry="f",
                args=[Value("U8", 200)])
        self.assertEqual(o.value, Value("I64", 2))
        self.assertEqual(run("void f(I64 c) { switch (c) { case 1..=5: return; case 5: return; default: return; } }")
                         .diagnostic.code, "C4021")
        o = run("I64 f(Bool b) { switch (b) { case true: return 1; case false: return 0; } }", entry="f",
                args=[Value("Bool", True)])
        self.assertEqual(o.value, Value("I64", 1))

    def test_fallthrough_and_break(self):
        src = ("I64 f(I64 c) {\n I64 r = 0;\n switch (c) {\n  case 1:\n   r += 1;\n   fallthrough;\n"
               "  case 2:\n   r += 10;\n   break;\n  default:\n   r = 100;\n }\n return r;\n}\n")
        self.assertEqual(run(src, entry="f", args=[Value("I64", 1)]).value, Value("I64", 11))
        self.assertEqual(run(src, entry="f", args=[Value("I64", 2)]).value, Value("I64", 10))
        self.assertEqual(run(src, entry="f", args=[Value("I64", 3)]).value, Value("I64", 100))

    def test_ranges(self):
        self.assertEqual(value_of("I64 r = 0;\nfor i in 1..=4 { r = r * 10 + i; }"), "1234")
        self.assertEqual(value_of("I64 r = 0;\nfor i in 0..10 by 4 { r = r * 10 + i; }"), "48")
        self.assertEqual(value_of("I64 r = 0;\nfor i in 3..0 by -1 { r = r * 10 + i; }"), "321")
        self.assertEqual(value_of("I64 r = 0;\nfor i in 3..=0 by -1 { r = r * 10 + i; }"), "3210")
        self.assertEqual(value_of("I64 r = 0;\nfor i in (I64.max - 2)..=I64.max { r += 1; }"), "3")
        self.assertEqual(value_of("I64 r = 0;\nfor i in 5..5 { r += 1; }"), "0")

    def test_c_for_overflow_8_4(self):
        o = run("for (I8 i = 0; i <= 127; i++) { }\n")
        self.assertEqual((o.kind, o.record.code, o.record.operation, o.fuel), ("fault", "E_OVERFLOW", "add.checked.i8", 129))
        self.assertEqual(str(o.record.position), "t.ci:1:27")

    def test_labels_and_continue(self):
        src = ("I64 r = 0;\nouter: for i in 0..3 {\n  for j in 0..3 {\n    if (j == 1) { continue outer; }\n"
               "    if (i == 2) { break outer; }\n    r += 1;\n  }\n}\n")
        self.assertEqual(value_of(src), "2")

    def test_while_else_if(self):
        src = ("I64 r = 0;\nI64 n = 0;\nwhile (n < 6) {\n  if (n % 3 == 0) { r += 1; } else if (n % 3 == 1) { r += 10; }"
               " else { r += 100; }\n  n++;\n}")
        self.assertEqual(value_of(src), "222")

    def test_short_circuit(self):
        self.assertEqual(value_of("I64 z = 0;\nBool r = z != 0 && 10 / z > 1;"), "false")
        self.assertEqual(value_of("I64 z = 0;\nI64 r = z == 0 ? 7 : 10 / z;"), "7")


class Structs(unittest.TestCase):
    def test_construct_read_write(self):
        src = ("struct P { I64 x; I32 y; }\n"
               "I64 total(P p) { return p.x + (p.y as I64); }\n"
               "P a = P(1, 2);\nP b = P(y = 5, x = 4);\nP z;\n"
               "a.x = 10;\nb = a;\nb.y += 1;\n"
               "I64 r = total(a) * 100 + total(b) * 10 + total(z);\n")
        self.assertEqual(value_of(src), "%d" % (12 * 100 + 13 * 10 + 0))

    def test_errors(self):
        self.assertEqual(run("struct P { I64 x; I32 y; }\nP a = P(1);\n\"x\";").diagnostic.code, "C2020")
        self.assertEqual(run("struct P { I64 x; }\nP a = P(x = 1, x = 2);\n\"x\";").diagnostic.code, "C2062")
        self.assertEqual(run("struct P { I64 x; }\nP a = P(q = 1);\n\"x\";").diagnostic.code, "C2063")


class TourScalarParts(unittest.TestCase):
    """The scalar arithmetic of the SPEC-00 section 4 tour test block."""

    SRC = ("I32 step(I32 center, I32 n, I32 s, I32 w, I32 e, I64 kn, I64 kd) {\n"
           "    I64 c = center as I64;\n"
           "    I64 lap = (n as I64) + (s as I64) + (w as I64) + (e as I64) - 4 * c;\n"
           "    return (c + div_round(kn * lap, kd, half_even)) as I32;\n"
           "}\n")

    def call(self, *a):
        args = [Value("I32", x) for x in a[:5]] + [Value("I64", a[5]), Value("I64", a[6])]
        return run(self.SRC, entry="step", args=args)

    def test_hot_spot(self):
        self.assertEqual(self.call(65536, 0, 0, 0, 0, 1, 8).value, Value("I32", 32768))
        self.assertEqual(self.call(0, 65536, 0, 0, 0, 1, 8).value, Value("I32", 8192))

    def test_narrowing_fault(self):
        o = self.call(2147483647, 0, 0, 0, 0, 1, 1)
        self.assertEqual((o.record.code, o.record.operation, o.record.exact), ("E_NARROW", "as.checked.i64.i32", -6442450941))
        self.assertEqual(str(o.record.position), "t.ci:4:53")


class Diagnostics(unittest.TestCase):
    def code(self, src):
        o = run(src + '\n"x";\n')          # a script, so that declarations are locals
        self.assertEqual(o.kind, "compile-error", (o.kind, o.record, o.message))
        return o.diagnostic.code

    def test_codes(self):
        self.assertEqual(self.code("I64 f(I64 x) { if (x > 0) { return 1; } }"), "C4001")
        self.assertEqual(self.code("I64 g() { return 1; }\ng();"), "C4011")
        self.assertEqual(self.code("void show() { \"{balance}\\n\"; }\nshow();\nI64 balance = 5;"), "C3004")
        self.assertEqual(self.code("I64 a = 1;\nI64 a = 2;"), "C3001")
        self.assertEqual(self.code("I64 abs = 1;"), "C3001")
        self.assertEqual(self.code("I64 a;"), "C2050")
        self.assertEqual(self.code("I64 n = 1;\nif (n) { }"), "C2002")
        self.assertEqual(self.code("I64 n = 1;\nBool b = n as Bool;"), "C2056")
        self.assertEqual(self.code("for i in 0..3 { i = 2; }"), "C2052")
        self.assertEqual(self.code("void f(I64 a) { a = 2; }"), "C2060")
        self.assertEqual(self.code('test "a" { }\ntest "a" { }'), "C3020")
        self.assertEqual(self.code("void main() { }\n\"x\";"), "C3011")
        self.assertEqual(self.code("I64 g() { return 1; }\nI64 x = 1;\nx = g;"), "C4010")
        self.assertEqual(self.code("static_assert(1 + 1 == 3);"), "C6010")
        self.assertEqual(self.code("I64 x = 1;\nI64 y = x as I64 round floor;"), "C2009")

    def test_script_main_and_entries(self):
        o = run("void main() { \"m\"; }\n")
        self.assertEqual((o.kind, o.stdout, o.fuel), ("value", b"m", 1))
        o = run('I64 f() { return 2; }\ntest "only" { "t"; }\n')
        self.assertEqual((o.kind, o.stdout), ("value", b"t"))
        o = run("I64 f() { return 2; }\n", entry="f")
        self.assertEqual(o.value, Value("I64", 2))


if __name__ == "__main__":
    unittest.main()
