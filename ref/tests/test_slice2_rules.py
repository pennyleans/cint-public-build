import os, sys; sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # put ref/ on the path
"""Tests for the rules of decision 23 (2026-10-03) that slice 2 task 2.1 implements:
the numbered codes of SPEC-04 17.2 (D-4), their positions (D-11), the block nesting limit
(D-5), constant expressions (D-9, D-17), negative and parenthesized literals (D-21),
script-mode notes and module-variable forward references (D-22), `main` (D-19), and
`cint_ref run --stdout` (D-19)."""
import subprocess
import tempfile
import unittest

from cint_ref.exec import run_program
from cint_ref.types import Value

REF_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def run(src, entry="run", path="t.ci"):
    data = src.encode("utf-8") if isinstance(src, str) else src
    return run_program(data, path, entry)


def diag(src, entry="run"):
    o = run(src, entry)
    assert o.kind == "compile-error", (o.kind, o.value, o.record, o.message)
    p = o.diagnostic.position
    return o.diagnostic.code, p.line, p.column


def fn(*body):
    """An exported `run` returning I64, with one statement per line from line 2."""
    return "export I64 run() {\n" + "".join("    %s\n" % b for b in body) + "    return 0;\n}\n"


class Codes(unittest.TestCase):
    """D-4: every compile error has a number; the same number as the seed (seed/tests)."""

    CASES = [
        # (source, code, line, column)
        (fn("return y;"), "C3005", 2, 12),
        (fn("Foo x = 1;"), "C3006", 2, 5),
        ("struct S {\n    I64 a;\n    I64 a;\n}\n" + fn(), "C3001", 3, 9),
        ("struct S {\n    T t;\n}\nstruct T {\n    S s;\n}\n" + fn(), "C2055", 5, 7),
        ("const I64 A = A + 1;\n" + fn(), "C6005", 1, 15),
        (fn("I64 v = 1;", "const I64 K = v;"), "C6004", 3, 19),
        ("void f() {\n}\n" + fn("I64 x = f();"), "C2001", 4, 13),
        (fn("I64 x = I64;"), "C3007", 2, 13),
        (fn("I64 = 1;"), "C3007", 2, 5),
        (fn("I64 a = 1;", "Bool b = !a;"), "C2002", 3, 15),
        ("struct S {\n    I64 a;\n}\n" + fn("S p = S(1);", "Bool b = p == p;"), "C2103", 6, 14),
        (fn("Bool b = true < false;"), "C2103", 2, 14),
        (fn("Bool b = true;", "I64 x = b + 1;"), "C2103", 3, 13),
        (fn("Bool b = true;", "Bool c = -b;"), "C2103", 3, 15),
        ("struct S {\n    I64 a;\n}\n" + fn("S p = S(1);", "Bool b = p as Bool;"), "C2008", 6, 14),
        ("struct S {\n    I64 a;\n}\n" + fn("S p = S(1);", "I64 b = p as I64;"), "C2008", 6, 13),
        (fn("I64 x = true as% I64;"), "C2008", 2, 13),
        (fn("I64 a = 1;", "I64 b = a.x;"), "C2103", 3, 15),
        ("struct S {\n    I64 a;\n}\n" + fn("S p = S(1);", "I64 b = p.z;"), "C3005", 6, 15),
        (fn("I64 a = 1;", "I64 b = a(2);"), "C3007", 3, 13),
        ("struct S {\n    I64 a;\n    I64 b;\n}\n" + fn("S p = S(a = 1, 2);"), "C2069", 6, 20),
        ("I64 f(I64 a) {\n    return a;\n}\n" + fn("I64 v = f(1, 2);"), "C2022", 5, 13),
        ("I64 f(I64 a) {\n    return a;\n}\n" + fn("I64 v = f();"), "C2022", 5, 13),
        (fn("I64 a = 1;", "a + 1 = 2;"), "C2058", 3, 5),
        (fn("const I64 K = 1;", "K = 2;"), "C2058", 3, 5),
        (fn("I64 a = 1;", "I64 v = 2;", "switch (a) {", "    case v:", "        return 1;", "    default:",
            "        return 0;", "}"), "C6004", 5, 14),
        (fn("I64 a = 1;", "switch (a) {", "    case 5..=1:", "        return 1;", "    default:",
            "        return 0;", "}"), "C4024", 4, 14),
        (fn("break;"), "C4013", 2, 5),
        (fn("continue;"), "C4013", 2, 5),
        ("void f() {\n    return 1;\n}\n" + fn(), "C2066", 2, 12),
        ("I64 f() {\n    return;\n}\n" + fn(), "C2066", 2, 5),
        (fn("I64 x = ;"), "C1050", 2, 13),
        (fn("I64 x = 0;", "while x < 1 {", "}"), "C1043", 3, 11),
        (fn("I64 x = 0;", "while (x < 1) x = 1;"), "C1043", 3, 19),
        (fn("I64 x = 0;", "for i in 0..3 x = 1;"), "C1043", 3, 19),
        (fn("I64 x = 0;", "for (I64 i = 0; i < 3; i++) x = 1;"), "C1043", 3, 33),
        (fn("I64 a = 1;", "switch (a) {", "    default:", "        return 0;", "    case 1:",
            "        return 1;", "}"), "C4025", 6, 9),
        (fn("I64 a = 1;", "switch (a) {", "    case 1:", "        return 1;", "    default:",
            "        return 0;", "    default:", "        return 2;", "}"), "C4025", 8, 9),
        ("export I64 run() {\n    I64 a = 1;\n", "C1050", 3, 1),
        (fn("I64 a = 0x;"), "C1024", 2, 13),
        (fn("I64 a = 0b12;"), "C1024", 2, 13),
        (fn("I64 a = 1 $ 2;"), "C1050", 2, 15),
        (fn("U8 c = 'a;"), "C1038", 2, 12),
        (fn("I64 %s = 1;" % ("a" * 256)), "C1012", 2, 9),
        (fn("I64 café = 1;"), "C1013", 2, 12),
        ('"a } b";\n', "C1039", 1, 4),
        ('"unterminated\n', "C1034", 1, 1),
        ('"x\\u{110000}";\n', "C1031", 1, 3),
        ('I64 x = 1;\n"{x!nope}";\n', "C1035", 2, 3),
        ('I64 x = 1;\n"{x:5}";\n', "C1037", 2, 3),
        ('I64 x = 1;\n"{x:>\u0663}";\n', "C1037", 2, 3),
        ('I64 x = 1;\n"{x:>\u00b2}";\n', "C1037", 2, 3),
        (fn("I64 x;"), "C2050", 2, 9),
        (fn("I64 x = 1;", "I64 x = 2;"), "C3001", 3, 9),
        ("export I64 run() {\n    I64 x = 1;\n    if (x == 1) {\n        return 1;\n    }\n}\n", "C4001", 1, 12),
        (fn("I64 x = clamp(1, 10, 1);"), "C6006", 2, 13),
        (fn("I64 x = div_round(7, 2, nearest);"), "C3007", 2, 29),
        (fn("I64 x = abs(v = 1);"), "C2063", 2, 17),
        (fn("I64 x = 1;", "fallthrough;"), "C4014", 3, 5),
        ("struct S {\n    I64 a;\n}\n" + fn("S p = S(1);", "switch (p) {", "    default:", "        return 0;", "}"),
         "C4026", 6, 13),
        ("test \"t\" {\n    static_assert(1 == 1 + 0 * 0 && true, \"ok\");\n    I64 v = 1;\n"
         "    static_assert(v == 1);\n}\n", "C6004", 4, 19),
    ]

    def test_codes_and_positions(self):
        failures = []
        for src, code, line, col in self.CASES:
            o = run(src, entry=None if "export" not in src else "run")
            got = (o.kind, o.diagnostic.code if o.diagnostic else None,
                   o.diagnostic.position.line if o.diagnostic else None,
                   o.diagnostic.position.column if o.diagnostic else None)
            if got != ("compile-error", code, line, col):
                failures.append("%r: got %s, want %s %d:%d" % (src[:60], got, code, line, col))
        self.assertEqual(failures, [], "\n".join(failures))

    def test_no_unassigned_code_in_the_sources(self):
        import cint_ref
        root = os.path.dirname(cint_ref.__file__)
        hits = []
        for name in sorted(os.listdir(root)):
            if name.endswith(".py"):
                with open(os.path.join(root, name), encoding="utf-8") as f:
                    for n, line in enumerate(f, 1):
                        if "error(None" in line or "SpecError(None" in line:
                            hits.append("%s:%d" % (name, n))
        self.assertEqual(hits, [])


class Positions(unittest.TestCase):
    """D-11: the position table of SPEC-04 LS-313 and the run-time positions of LS-278."""

    def test_assert_operation_and_position(self):
        o = run("export I64 run() {\n    I64 a = 3;\n    assert(a == 4);\n    return a;\n}\n")
        r = o.record
        self.assertEqual((r.code, r.operation, str(r.position)), ("E_ASSERT", "assert.checked.bool", "t.ci:3:12"))

    def test_format_char_operation(self):
        o = run('I64 x = 55296;\n"{x:c}";\n', entry=None)
        r = o.record
        self.assertEqual((r.code, r.operation), ("E_NARROW", "format.char.i64"))

    def test_increment_fault_at_the_operator(self):
        o = run("export I64 run() {\n    I8 i = 127;\n    i++;\n    return 0;\n}\n")
        self.assertEqual(str(o.record.position), "t.ci:3:6")

    def test_fuel_at_the_loop_keyword(self):
        o = run_program(b"export I64 run() {\n    while (true) {\n    }\n    return 0;\n}\n", "t.ci", "run", 3)
        self.assertEqual((o.record.code, str(o.record.position)), ("E_FUEL", "t.ci:2:5"))


class Nesting(unittest.TestCase):
    """D-5: blocks nest 256 deep inside a function body; `else if` and switch arms are flat."""

    def blocks(self, n):
        return "export I64 run() {\n    I64 a = 1;\n%s a = 2; %s\n    return a;\n}\n" % ("{" * n, "}" * n)

    def test_256_blocks(self):
        self.assertEqual(run(self.blocks(256)).value, Value("I64", 2))

    def test_257_blocks(self):
        self.assertEqual(diag(self.blocks(257)), ("C9004", 3, 257))

    def test_256_whiles_and_fors(self):
        src = "export I64 run() {\n    I64 a = 0;\n%s a = 1; %s\n    return a;\n}\n" % (
            "while (a < 1) { " * 256, "} " * 256)
        self.assertEqual(run(src).value, Value("I64", 1))
        src = "export I64 run() {\n    I64 a = 0;\n%s a = a + 1; %s\n    return a;\n}\n" % (
            "".join("for i%d in 0..1 { " % k for k in range(256)), "} " * 256)
        self.assertEqual(run(src).value, Value("I64", 1))
        src = src.replace("for i0 in 0..1 { ", "for i0 in 0..1 { for j in 0..1 { ").replace("}\n    return", "} }\n    return")
        self.assertEqual(diag(src)[0], "C9004")

    def test_else_if_does_not_nest(self):
        arms = "".join(" else if (x == %d) {\n        r = %d;\n    }" % (k, k) for k in range(1, 1000))
        src = "export I64 run() {\n    I64 x = 999;\n    I64 r = 0;\n    if (x == 0) {\n        r = 0;\n    }%s\n" \
              "    return r;\n}\n" % arms
        self.assertEqual(run(src).value, Value("I64", 999))


class ConstantExpressions(unittest.TestCase):
    """D-9 and D-17."""

    def test_skipped_operand_literals_are_range_checked(self):
        self.assertEqual(diag(fn("Bool b = false && (1 / 0 == 300 as I8);")), ("C6001", 2, 37))
        self.assertEqual(diag(fn("I8 x = 1;", "Bool b = x == 2 && (x < 300);")), ("C2003", 3, 29))

    def test_literal_conversion_in_a_runtime_skipped_operand(self):
        # D-17: decided at compile time wherever it appears; C6001 E_NARROW at the `as`.
        o = run(fn("I64 x = 1;", "Bool b = x == 2 && ((300 as I8) == 0);"))
        d = o.diagnostic
        self.assertEqual((d.code, d.position.line, d.position.column, d.fault.code, d.fault.operation),
                         ("C6001", 3, 30, "E_NARROW", "unassigned"))

    def test_evaluation_order_before_skipped_checks(self):
        # A fault on the evaluated path comes before a range check in a skipped operand.
        o = run(fn("Bool b = (false && (5 < 300)) || (1 / 0 == 0);"))
        self.assertEqual((o.diagnostic.code, o.diagnostic.fault.code), ("C6001", "E_DIV_ZERO"))

    def test_statement_always_evaluated_is_c6001(self):
        src = fn("I64 x = 1;", "if (x == 6) {", "    x = 9223372036854775807 + 1;", "}")
        self.assertEqual(diag(src)[0], "C6001")


class Literals(unittest.TestCase):
    """D-21 option 1."""

    def value(self, src):
        o = run(src)
        self.assertEqual(o.kind, "value", (o.kind, o.diagnostic, o.message))
        return o.value

    def test_separated_minus_forms_a_negative_literal(self):
        self.assertEqual(self.value("export I8 run() { I8 b = - 128; return b; }"), Value("I8", -128))
        self.assertEqual(self.value("export I8 run() { I8 c = -/*comment*/128; return c; }"), Value("I8", -128))
        self.assertEqual(self.value("export I8 run() { return - 128 as I8; }"), Value("I8", -128))
        self.assertEqual(self.value("export U64 run() { return - 1 as% U64; }"), Value("U64", 18446744073709551615))

    def test_parenthesized_literal_converts_from_z(self):
        self.assertEqual(self.value("export U64 run() { return (18446744073709551615) as U64; }"),
                         Value("U64", 18446744073709551615))
        self.assertEqual(self.value("export U64 run() { return (0xFFFF_FFFF_FFFF_FFFF) as% U64; }"),
                         Value("U64", 18446744073709551615))
        self.assertEqual(self.value("export I8 run() { return ((((-128)))) as I8; }"), Value("I8", -128))
        o = run("export I8 run() { return (300) as I8; }")
        d = o.diagnostic
        self.assertEqual((d.code, d.fault.operation, [(x.type, x.value) for x in d.fault.operands]),
                         ("C6001", "unassigned", [("Z", 300)]))

    def test_negation_stays_negation(self):
        self.assertEqual(diag("export I8 run() { I8 d = -(128); return d; }"), ("C2003", 1, 28))
        self.assertEqual(diag("export I8 run() { return -(128) as I8; }"), ("C2101", 1, 26))
        self.assertEqual(diag("export U8 run() { U8 c = - 1; return c; }"), ("C2003", 1, 26))
        o = run("export U8 run() { U8 u = -(1); return u; }")
        self.assertEqual((o.diagnostic.code, o.diagnostic.fault.operation), ("C6001", "neg.checked.u8"))


class ScriptMode(unittest.TestCase):
    """D-22: the LS-312 notes; the module-variable forward reference (REF-OQ-27)."""

    def test_c3004_note_names_the_statement(self):
        o = run('I64 balance = 5;\nexport I64 read_balance() { return balance; }\n"ready\\n";\n', "read_balance")
        d = o.diagnostic
        self.assertEqual((d.code, d.position.line, d.position.column), ("C3004", 2, 36))
        self.assertEqual(d.notes[0], 'note: this module is a script because of the top-level statement at '
                                     't.ci:3:1: `"ready\\n";`')
        self.assertTrue(d.notes[1].startswith("help: move the top-level statements into `void main() { ... }`"))

    def test_c3011_note(self):
        o = run('void main() {\n}\n"x";\n', None)
        self.assertEqual(o.diagnostic.code, "C3011")
        self.assertIn("t.ci:3:1", o.diagnostic.notes[0])
        self.assertEqual(o.diagnostic.notes[1], "help: move the top-level statements into `main`")

    def test_c6004_note_for_a_module(self):
        o = run("I64 a = 9223372036854775807;\nI64 q = div_round(a, 1, half_even) + 1;\n", None)
        d = o.diagnostic
        self.assertEqual((d.code, d.position.line, d.position.column), ("C6004", 2, 9))
        self.assertEqual(d.notes[0], "note: this file is a module, not a script, because every top-level item "
                                     "is a declaration (LS-218)")

    def test_module_variable_read_before_its_declaration(self):
        o = run("export I64 show() { return balance; }\nI64 balance = 5;\n", "show")
        self.assertEqual((o.kind, o.value), ("value", Value("I64", 5)))

    def test_script_local_read_before_its_declaration(self):
        o = run('export I64 show() { return balance; }\nI64 balance = 5;\n"x";\n', "show")
        self.assertEqual(o.diagnostic.code, "C3004")


class Main(unittest.TestCase):
    """D-19: SPEC-04 LS-221 admits `void main()` only (and `E!void main()`, outside the scalar surface)."""

    def test_value_returning_main(self):
        self.assertEqual(diag("I64 main() {\n    return 7;\n}\n", None), ("C2066", 1, 1))

    def test_void_main_runs(self):
        o = run('void main() {\n    "m";\n}\n', None)
        self.assertEqual((o.kind, o.stdout), ("value", b"m"))


class StdoutOption(unittest.TestCase):
    """D-19: `cint_ref run --stdout FILE` writes the program's output bytes."""

    def test_stdout_file(self):
        with tempfile.TemporaryDirectory() as d:
            src = os.path.join(d, "hello.ci")
            with open(src, "wb") as f:
                f.write(b'"hello, world\\n";\nI64 answer = 6 * 7;\n"answer={answer}\\n";\n')
            out = os.path.join(d, "hello.stdout")
            env = dict(os.environ, PYTHONPATH=REF_DIR)
            r = subprocess.run([sys.executable, "-m", "cint_ref", "run", src, "--path", "examples/hello.ci",
                                "--stdout", out], capture_output=True, env=env)
            self.assertEqual(r.returncode, 0, r.stderr)
            with open(out, "rb") as f:
                self.assertEqual(f.read(), b"hello, world\nanswer=42\n")
            self.assertIn(b"stdout-sha256 0d3f756a0f6c68f4cee819726e6ddf3f969bb5b63c827eb66a1166eba9104b57\n", r.stdout)


if __name__ == "__main__":
    unittest.main()
