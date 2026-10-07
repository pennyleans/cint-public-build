"""Checks that tools/cint_lint.py refuses what it says it refuses, on a scratch tree."""
import contextlib
import io
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import cint_lint  # noqa: E402

FLT = "flo" + "at"
DBL = "dou" + "ble"


class CintLint(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        for rel, text in {
            "rt/cint_rt.h": "#include <stdint.h>\nint64_t a(void);\n",
            "rt/cint_rt.c": "/* a " + DBL + " in a comment is fine */\nint x = 0x1e5;\n",
            "rt/cint_rt_internal.h": "\n",
            "rt/cint_bridge.c": 'const char *s = "' + FLT + ' 1.5";\n',
            "rt/cint_build.c": "\n",
            "rt/cint_cuda.c": "\n",
            "rt/cint_cuda_dispatch.c": "\n",
            "rt/cint_state.c": "\n",
            "rt/cint_mem.c": "\n",
            "rt/cint_mem.h": "\n",
            "seed/scan.c": "int v = 10ULL;\n",
            "harness/cint_harness.c": "char c = '.';\n",
            "cli/cint_main.c": "int main(void) { return 0; }\n",
            "cli/cint_receipt.c": "int receipt;\n",
            "cli/cint_proc.h": "int cli_process(void);\n",
            "interp/sir_read.c": "int sir;\n",
            "workbench/wb.h": "int wb(void);\n",
            "ref/cint_ref/arith.py": "import math\nr = math.isqrt(9) // 2\n# 1.5 " + FLT + "\n",
            "conformance/arith/a.expect": "outcome value\n",
        }.items():
            self.write(rel, text)

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, rel, text):
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(text.encode("utf-8"))

    def run_lint(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = cint_lint.run(self.root)
        return code, out.getvalue()

    def assertRefused(self, rel, text, needle):
        self.write(rel, text)
        code, out = self.run_lint()
        self.assertEqual(code, 1, out)
        self.assertIn(needle, out)

    def test_clean_tree_passes(self):
        code, out = self.run_lint()
        self.assertEqual(code, 0, out)

    def test_c_float_type(self):
        self.assertRefused("seed/scan.c", "int a;\nstatic " + DBL + " d;\n", "seed/scan.c:2: floating-point type")

    def test_c_long_double_and_extension(self):
        self.assertRefused("harness/cint_harness.c", "__float128 q;\n", "floating-point type `__float128`")

    def test_c_float_literals(self):
        for literal in ("1.0", ".5", "2.", "1e3", "0x1p3", "3.0f"):
            with self.subTest(literal=literal):
                self.assertRefused("rt/cint_rt.c", "long x = " + literal + ";\n", "floating-point literal")

    def test_c_float_header(self):
        self.assertRefused("rt/cint_rt.c", "#include <math.h>\n", "floating-point header")

    def test_c_test_sources_are_scanned(self):
        self.assertRefused("rt/tests/t.c", FLT + " f;\n", "rt/tests/t.c:1")

    def test_cli_c_headers_and_test_sources_are_scanned(self):
        for rel in ("cli/cint_main.c", "cli/cint_proc.h", "cli/tests/helper.c", "cli/tests/helper.h"):
            with self.subTest(rel=rel):
                self.assertRefused(rel, DBL + " d;\n", rel + ":1: floating-point type")
                self.write(rel, "int d;\n")

    def test_cli_sources_and_test_files_refuse_cr(self):
        for rel in ("cli/cint_main.c", "cli/cint_proc.h", "cli/tests/run.py", "cli/tests/bytes.stdout"):
            with self.subTest(rel=rel):
                self.assertRefused(rel, "line\r\n", rel + ":1: CR byte")
                self.write(rel, "line\n")

    def test_interp_sources_and_tests_are_scanned(self):
        for rel in ("interp/sir_read.c", "interp/sir.h", "interp/tests/sir_check.c"):
            with self.subTest(rel=rel):
                self.assertRefused(rel, DBL + " d;\n", rel + ":1: floating-point type")
                self.write(rel, "int d;\n")
        for rel in ("interp/sir.h", "interp/tests/test_interp.py", "interp/tests/sir/a/a.sites"):
            with self.subTest(rel=rel):
                self.assertRefused(rel, "line\r\n", rel + ":1: CR byte")
                self.write(rel, "line\n")

    def test_cli_size_counts_top_level_c_and_reports_headers_separately(self):
        self.write("cli/cint_main.c", "\n" * 900)
        self.write("cli/cint_proc_win.c", "\n" * 300)
        self.write("cli/cint_proc_posix.c", "\n" * 300)
        self.write("cli/cint_proc.h", "\n" * 9000)
        self.write("cli/tests/helper.c", "\n" * 3000)
        code, out = self.run_lint()
        self.assertEqual(code, 0, out)
        self.assertIn("PASS size cint CLI (cli/*.c except cli/cint_receipt.c): 1,500 lines "
                      "(target 1,500, ceiling 2,500)", out)
        self.assertIn("INFO size cli headers (cli/*.h): 9,000 lines", out)
        self.write("cli/cint_proc_posix.c", "\n" * 301)
        code, out = self.run_lint()
        self.assertEqual(code, 0, out)
        self.assertIn("WARN size cint CLI", out)
        self.assertRefused("cli/cint_proc_posix.c", "\n" * 1301, "FAIL size cint CLI")

    def test_cli_receipt_file_has_its_own_row(self):
        self.write("cli/cint_main.c", "\n" * 1500)
        self.write("cli/cint_receipt.c", "\n" * 400)
        code, out = self.run_lint()
        self.assertEqual(code, 0, out)
        self.assertIn("PASS size cint CLI (cli/*.c except cli/cint_receipt.c): 1,500 lines", out)
        self.assertIn("PASS size cint CLI receipts (cli/cint_receipt.c): 400 lines (target 400, ceiling 600)", out)
        self.write("cli/cint_receipt.c", "\n" * 401)
        code, out = self.run_lint()
        self.assertEqual(code, 0, out)
        self.assertIn("WARN size cint CLI receipts", out)
        self.assertRefused("cli/cint_receipt.c", "\n" * 601, "FAIL size cint CLI receipts")

    def test_memory_runtime_has_its_own_row(self):
        # Decision 2026-10-06 on OQ-212: rt/cint_mem.c and rt/cint_mem.h, outside the cint_rt rows.
        self.write("rt/cint_mem.c", "\n" * 500)
        self.write("rt/cint_mem.h", "\n" * 100)
        code, out = self.run_lint()
        self.assertEqual(code, 0, out)
        self.assertIn("PASS size cint_mem (rt/cint_mem.c, rt/cint_mem.h): 600 lines (target 600, ceiling 900)", out)
        self.assertIn("PASS size cint_rt library (cint_rt.c, cint_rt_internal.h): 3 lines", out)
        self.assertRefused("rt/cint_mem.c", "\n" * 801, "FAIL size cint_mem")

    def test_missing_cli_component_fails(self):
        (self.root / "cli/cint_main.c").unlink()
        code, out = self.run_lint()
        self.assertEqual(code, 1, out)
        self.assertIn("FAIL size cint CLI", out)

    def test_workbench_sources_and_tests_are_scanned(self):
        for rel in ("workbench/wb_num.c", "workbench/wb.h", "workbench/tests/test_wb.c"):
            with self.subTest(rel=rel):
                self.assertRefused(rel, DBL + " d;\n", rel + ":1: floating-point type")
                self.write(rel, "int d;\n")

    def test_workbench_files_refuse_cr(self):
        for rel in ("workbench/wb_num.c", "workbench/tests/fixtures/report/a.stderr"):
            with self.subTest(rel=rel):
                self.assertRefused(rel, "line\r\n", rel + ":1: CR byte")
                self.write(rel, "line\n")

    def test_workbench_row_counts_top_level_c_and_h(self):
        # Box 14 ruling BX14-03 A: target 8,000, ceiling 12,000; workbench/tests/ is not counted.
        self.write("workbench/wb.h", "\n" * 1000)
        self.write("workbench/wb_fault.c", "\n" * 7000)
        self.write("workbench/tests/test_wb.c", "\n" * 9000)
        code, out = self.run_lint()
        self.assertEqual(code, 0, out)
        self.assertIn("PASS size cint workbench (workbench/*.c, workbench/*.h): 8,000 lines "
                      "(target 8,000, ceiling 12,000)", out)
        self.write("workbench/wb_cmd.c", "\n")
        code, out = self.run_lint()
        self.assertEqual(code, 0, out)
        self.assertIn("WARN size cint workbench", out)
        self.assertRefused("workbench/wb_cmd.c", "\n" * 4001, "FAIL size cint workbench")

    def test_python_float_forms(self):
        cases = {
            FLT + "(3)\n": "floating-point name",
            "x = 1.5\n": "floating-point literal",
            "x = 3 / 2\n": "true division",
            "x = 2j\n": "floating-point literal",
            "import decimal\n": "floating-point module",
            "import math\nx = math.sqrt(2)\n": "`math` used",
            "from math import isqrt\n": "write `import math`",
        }
        for text, needle in cases.items():
            with self.subTest(text=text):
                self.assertRefused("ref/cint_ref/arith.py", text, needle)

    def test_python_bridge_is_scanned(self):
        self.assertRefused("python/cint/_view.py", "x = " + FLT + "(3)\n", "python/cint/_view.py:1: floating-point name")

    def test_float_boundary_may_name_float_types(self):
        """BX10-18: python/cint/_floats.py may name the types; literals and true division stay refused."""
        self.write("python/cint/_floats.py", "def f(x):\n    return isinstance(x, (" + FLT + ", complex))\n")
        code, out = self.run_lint()
        self.assertEqual(code, 0, out)
        for text, needle in (("x = 1.5\n", "floating-point literal"), ("x = 3 / 2\n", "true division"),
                             ("import decimal\n", "floating-point module")):
            with self.subTest(text=text):
                self.assertRefused("python/cint/_floats.py", text, needle)

    def test_python_tests_are_not_scanned(self):
        self.write("python/tests/test_floats.py", "x = " + FLT + "(1.5) / 2\n")
        code, out = self.run_lint()
        self.assertEqual(code, 0, out)

    def test_cr_refused(self):
        self.assertRefused("conformance/arith/a.expect", "outcome\r\nvalue\n", "conformance/arith/a.expect:1: CR byte")
        self.write("conformance/arith/a.expect", "ok\n")
        self.assertRefused("rt/cint_rt.h", "a\nb\r\n", "rt/cint_rt.h:2: CR byte")

    def test_size_ceiling_and_target(self):
        self.write("harness/cint_harness.c", "\n" * 700)
        code, out = self.run_lint()
        self.assertEqual(code, 0, out)
        self.assertIn("WARN size cint-harness", out)
        self.assertRefused("harness/cint_harness.c", "\n" * 1001, "FAIL size cint-harness")

    def test_harness_row_counts_every_c_and_h_file(self):
        # Slice 2 patch D-6: a split into files cannot hide lines; harness/tests/ is not counted.
        self.write("harness/cint_harness.c", "\n" * 600)
        self.write("harness/tests/mod_basic.c", "\n" * 900)
        code, out = self.run_lint()
        self.assertEqual(code, 0, out)
        self.assertIn("PASS size cint-harness (harness/*.c, harness/*.h): 600 lines", out)
        self.assertRefused("harness/split.h", "\n" * 401,
                           "FAIL size cint-harness (harness/*.c, harness/*.h): 1,001 lines")

    def test_optional_component_may_be_absent(self):
        import shutil
        for d in ("interp", "workbench"):
            shutil.rmtree(self.root / d, ignore_errors=True)
        code, out = self.run_lint()
        self.assertEqual(code, 0, out)
        self.assertIn("not in this tree", out)

    def test_missing_component_fails(self):
        (self.root / "rt/cint_bridge.c").unlink()
        code, out = self.run_lint()
        self.assertEqual(code, 1, out)
        self.assertIn("no files", out)


if __name__ == "__main__":
    unittest.main()
