import os, sys; sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # put ref/ on the path
"""`.expect` reader and writer (SPEC-09 9.2 CONF-01; layout per ref/OPEN.md REF-OQ-01) and the CLI."""
import hashlib
import os
import subprocess
import tempfile
import unittest

from cint_ref import expect
from cint_ref.exec import run_program
from cint_ref.types import Value

REF_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

CONF01_EXPECT = """case arith/add_i64_overflow
clause ARITH-ADD-CHECKED
source reference
outcome fault
stdout-bytes 0
fuel-consumed 0
fault.code E_OVERFLOW
fault.operation add.checked.i64
fault.operand I64 9223372036854775807
fault.operand I64 1
fault.exact 9223372036854775808
fault.limit I64 9223372036854775807
fault.position arith/add_i64_overflow.ci:5:15
fault.revision self
fault.address none
fault.stack-depth 0
"""

CONF01_SOURCE = """// conformance/arith/add_i64_overflow.ci
test "add overflow at I64 max" {
    I64 a = 9223372036854775807;
    I64 b = 1;
    I64 c = a + b;
    "c={c}\\n";
}
"""


def outcome(src, **kw):
    kw.setdefault("path", "t.ci")
    path = kw.pop("path")
    return run_program(src.encode(), path, kw.pop("entry", None), kw.pop("fuel", None), kw.pop("depth", 256), **kw)


class RoundTrip(unittest.TestCase):
    def test_conf01_example_is_byte_identical(self):
        e = expect.parse(CONF01_EXPECT)
        self.assertEqual(expect.render(e), CONF01_EXPECT)
        self.assertEqual(e.get("outcome"), "fault")
        self.assertEqual(e.getall("fault.operand"), ["I64 9223372036854775807", "I64 1"])

    def test_conf01_program_gives_the_example_except_fuel(self):
        o = outcome(CONF01_SOURCE, path="arith/add_i64_overflow.ci")
        e = expect.from_outcome(o, case="arith/add_i64_overflow", clause="ARITH-ADD-CHECKED")
        expected = CONF01_EXPECT.replace("fuel-consumed 0", "fuel-consumed 1")   # interim decision OQ-17
        self.assertEqual(expect.render(e), expected)


class Outcomes(unittest.TestCase):
    def test_value_with_stdout_and_return(self):
        o = outcome('I64 f() {\n    "hello\\n";\n    return 42;\n}\n', entry="f")
        text = expect.render(expect.from_outcome(o, case="t"))
        self.assertEqual(text, "case t\nsource reference\noutcome value\nstdout-bytes 6\n"
                               "stdout-sha256 %s\nreturn I64 42\nfuel-consumed 1\n"
                               % hashlib.sha256(b"hello\n").hexdigest())

    def test_value_of_a_script(self):
        o = outcome("I64 x = 1;\n")
        self.assertEqual(o.kind, "refused")         # declarations only: an importable module, no entry
        o = outcome('I64 x = 1;\n"";\n')
        self.assertEqual(expect.render(expect.from_outcome(o, case="t")),
                         "case t\nsource reference\noutcome value\nstdout-bytes 0\nfuel-consumed 1\n")

    def test_compile_error_c2003(self):
        o = outcome("void f() {\n    I16 h = 40000 - 30000;\n}\n")
        self.assertEqual(expect.render(expect.from_outcome(o, case="const/i16_literal")),
                         "case const/i16_literal\nsource compile-error\noutcome compile-error\n"
                         "diagnostic.code C2003\ndiagnostic.position t.ci:2:13\n")

    def test_compile_error_c6001(self):
        o = outcome("const I8 X = 100 + 100 - 150;\n")
        self.assertEqual(expect.render(expect.from_outcome(o, case="c")),
                         "case c\nsource compile-error\noutcome compile-error\n"
                         "diagnostic.code C6001\ndiagnostic.position t.ci:1:18\n"
                         "diagnostic.fault.code E_OVERFLOW\ndiagnostic.fault.operation add.checked.i8\n"
                         "diagnostic.fault.operand I8 100\ndiagnostic.fault.operand I8 100\n"
                         "diagnostic.fault.exact 200\ndiagnostic.fault.limit I8 127\n")

    def test_shift_fault_lines(self):
        o = outcome("I64 f(I64 k) { return 1 << k; }\n", entry="f", args=[Value("I64", 64)])
        lines = expect.render(expect.from_outcome(o, case="s")).splitlines()
        self.assertIn("fault.code E_SHIFT", lines)
        self.assertIn("fault.exact none", lines)
        self.assertIn("fault.limit I64 63", lines)

    def test_refused(self):
        o = outcome("I64[4] xs;\n")
        self.assertEqual(expect.render(expect.from_outcome(o, case="v")).splitlines()[:3],
                         ["case v", "source reference", "outcome refused"])


class Reader(unittest.TestCase):
    def bad(self, text):
        with self.assertRaises(ValueError):
            expect.parse(text)

    def test_rejects(self):
        self.bad(CONF01_EXPECT.replace("\n", "\r\n"))                        # LF only
        self.bad(CONF01_EXPECT.rstrip("\n"))                                  # final LF required
        self.bad(CONF01_EXPECT.replace("case arith", "kase arith"))           # unknown key
        self.bad("outcome fault\ncase x\n")                                   # out of order
        self.bad(CONF01_EXPECT.replace("arith/add", "arith/é"))          # ASCII only
        self.bad("case x\ncase y\n")                                          # repeated key
        self.bad("case\n")                                                    # no value

    def test_first_difference(self):
        a = expect.parse(CONF01_EXPECT)
        b = expect.parse(CONF01_EXPECT.replace("fault.exact 9223372036854775808", "fault.exact 1"))
        self.assertEqual(expect.first_difference(a, a), None)
        self.assertEqual(expect.first_difference(a, b),
                         (11, "fault.exact 9223372036854775808", "fault.exact 1"))
        c = expect.parse(CONF01_EXPECT.replace("fault.stack-depth 0\n", ""))
        self.assertEqual(expect.first_difference(a, c), (16, "fault.stack-depth 0", None))


class Cli(unittest.TestCase):
    def cli(self, *args):
        env = dict(os.environ, PYTHONPATH=REF_DIR)
        return subprocess.run([sys.executable, "-m", "cint_ref"] + list(args), capture_output=True, env=env)

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        d = os.path.join(self.tmp.name, "conformance", "arith")
        os.makedirs(d)
        self.ci = os.path.join(d, "add_i64_overflow.ci")
        with open(self.ci, "wb") as f:
            f.write(CONF01_SOURCE.encode())
        self.expect_path = os.path.join(d, "add_i64_overflow.expect")

    def tearDown(self):
        self.tmp.cleanup()

    def test_run_prints_expect_text_with_lf(self):
        r = self.cli("run", self.ci, "--clause", "ARITH-ADD-CHECKED")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(r.stdout.decode("ascii"), CONF01_EXPECT.replace("fuel-consumed 0", "fuel-consumed 1"))
        self.assertNotIn(b"\r", r.stdout)

    def test_check_agreement_and_difference(self):
        with open(self.expect_path, "wb") as f:
            f.write(CONF01_EXPECT.replace("fuel-consumed 0", "fuel-consumed 1").encode())
        r = self.cli("check", self.ci, self.expect_path)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        with open(self.expect_path, "wb") as f:
            f.write(CONF01_EXPECT.encode())
        r = self.cli("check", self.ci, self.expect_path)
        self.assertEqual(r.returncode, 1)
        self.assertIn(b"line 6", r.stdout)
        self.assertIn(b"fuel-consumed 0", r.stdout)

    def test_run_with_entry_arguments_and_fuel(self):
        src = os.path.join(self.tmp.name, "total.ci")
        with open(src, "wb") as f:
            f.write(b"I64 total(I64 n) {\n    I64 s = 0;\n    for (I64 i = 0; i < n; i++) { s = s + i; }\n    return s;\n}\n")
        r = self.cli("run", src, "--entry", "total", "--arg", "I64 3", "--fuel", "3")
        text = r.stdout.decode("ascii")
        self.assertIn("case total\n", text)
        self.assertIn("fault.code E_FUEL\n", text)
        self.assertIn("fuel-consumed 3\n", text)
        self.assertIn("fault.position total.ci:3:5\n", text)


if __name__ == "__main__":
    unittest.main()
