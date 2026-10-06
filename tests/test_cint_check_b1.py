"""The B1 path of tools/cint_check.py (plan task 2.12a): the module order of slice 2 decision
D-13, the reading of `cint --json emit-c` diagnostics as `.expect` bodies, and the SPEC-09
CONF-14 category of a B1 (`cintc`) result: compared, an expected compile error, unsupported
with its limit and reason, and the disagreements that fail a run. Python standard library
only; no toolchain and no B1 build are needed."""
import json
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import cint_check  # noqa: E402
from cint_check import classify  # noqa: E402

B1 = cint_check.COMPILERS["b1"]
VALUE = ["outcome value", "stdout-bytes 0", "return I64 3", "fuel-consumed 2"]
UNSUPPORTED = {("struct/zero_fill", B1): ("C9102", "CINTC-OQ-15", "B1 refuses struct declarations")}


def error(code, position):
    return ["outcome compile-error", "diagnostic.code " + code, "diagnostic.position " + position]


def diag_line(code, path, line, column, fault=None):
    d = {"code": code, "column": column, "file": path, "line": line, "message": "m",
         "schema": "CINT-DIAG-1", "severity": "error"}
    if fault is not None:
        d["fault"] = fault
    return json.dumps(d, sort_keys=True)


class B1Diagnostic(unittest.TestCase):
    def test_first_diagnostic_with_its_compile_time_fault(self):
        fault = {"address": None, "code": "E_OVERFLOW", "column": 27, "exact": {"t": "Z", "v": "4000000000"},
                 "file": "arith/const_mul_overflow.ci", "limit": {"t": "I32", "v": "2147483647"}, "line": 8,
                 "operands": [{"t": "I32", "v": "2000000000"}, {"t": "I32", "v": "2"}],
                 "operation": "mul.checked.i32", "revision": None, "schema": "CINT-FAULT-1",
                 "severity": "fault", "source_map": None, "stack": []}
        stderr = diag_line("C6001", "arith/const_mul_overflow.ci", 8, 27, fault) + "\n" + \
            diag_line("C3001", "arith/const_mul_overflow.ci", 9, 1) + "\n"
        frozen = (ROOT / "conformance/arith/const_mul_overflow.expect").read_text(encoding="ascii")
        self.assertEqual(cint_check.b1_diagnostic(stderr), cint_check.body(frozen.rstrip("\n").split("\n")))

    def test_diagnostic_of_an_imported_module_names_that_module(self):
        stderr = diag_line("C9102", "module/lib/plain_math.ci", 7, 15)
        self.assertEqual(cint_check.b1_diagnostic(stderr),
                         error("C9102", "module/lib/plain_math.ci:7:15"))

    def test_no_diagnostic_is_a_compiler_failure(self):
        got = cint_check.b1_diagnostic('{"schema":"CINT-TOOL-1","message":"x"}\nnot json\n')
        self.assertEqual(got[0], "outcome compiler-failed")
        category, detail = classify("arith/x", "cint-boot-1", VALUE, 7, got, {}, {}, B1)
        self.assertEqual(category, "disagreement")
        self.assertIn("exit status 7", detail["reason"])


class B1Categories(unittest.TestCase):
    def test_compared(self):
        self.assertEqual(classify("arith/add", "cint-boot-1", VALUE, 0, None, UNSUPPORTED, {}, B1),
                         ("compared", {}))

    def test_expected_compile_error_is_compared(self):
        frozen = error("C3030", "module/c3030_not-an-identifier.ci:1:1")
        self.assertEqual(classify("module/c3030_not-an-identifier", "cint-boot-1", frozen, 2, list(frozen),
                                  UNSUPPORTED, {}, B1),
                         ("compared", {"code": "C3030", "position": "module/c3030_not-an-identifier.ci:1:1"}))

    def test_listed_c9102_refusal_is_unsupported_with_its_limit(self):
        got = error("C9102", "struct/zero_fill.ci:7:15")
        category, detail = classify("struct/zero_fill", "cint-core-1", VALUE, 2, got, UNSUPPORTED, {}, B1)
        self.assertEqual(category, "unsupported")
        self.assertEqual(detail, {"code": "C9102", "position": "struct/zero_fill.ci:7:15", "compiler": B1,
                                  "limit": "CINTC-OQ-15", "frozen_outcome": "value"})
        self.assertEqual(cint_check.required_token(category, detail), "CINTC-OQ-15")

    def test_unlisted_c9102_refusal_is_a_disagreement(self):
        got = error("C9102", "struct/zero_fill.ci:7:15")
        category, detail = classify("struct/zero_fill", "cint-core-1", VALUE, 2, got, {}, {}, B1)
        self.assertEqual(category, "disagreement")
        self.assertIn("unsupported.txt does not list", detail["reason"])

    def test_a_seed_entry_does_not_cover_b1(self):
        seed = {("struct/zero_fill", "cint-seed"): UNSUPPORTED[("struct/zero_fill", B1)]}
        got = error("C9102", "struct/zero_fill.ci:7:15")
        self.assertEqual(classify("struct/zero_fill", "cint-core-1", VALUE, 2, got, seed, {}, B1)[0],
                         "disagreement")

    def test_listed_case_that_b1_accepts_is_a_disagreement(self):
        category, detail = classify("struct/zero_fill", "cint-core-1", VALUE, 0, None, UNSUPPORTED, {}, B1)
        self.assertEqual(category, "disagreement")
        self.assertIn("accepts it", detail["reason"])

    def test_b1_has_no_outside_the_subset_category(self):
        # A C9100 subset refusal is the seed's (CONF-14 category 4); from B1 it is unlisted.
        got = error("C9100", "control/print.ci:3:1")
        self.assertEqual(classify("control/print", "cint-core-1", VALUE, 2, got, {}, {}, B1)[0], "disagreement")

    def test_compile_error_at_another_position_fails_the_run(self):
        frozen = error("C3008", "module/import_private_c3008.ci:10:23")
        got = error("C3008", "module/import_private_c3008.ci:10:5")
        category, detail = classify("module/import_private_c3008", "cint-boot-1", frozen, 2, got, {}, {}, B1)
        self.assertEqual(category, "disagreement")
        p = cint_check.Program("module/import_private_c3008", ROOT / "conformance",
                               "module/import_private_c3008.ci", "program")
        p.cases = [("module.import_private_c3008 run", "module/import_private_c3008")]
        res = cint_check.not_run({"compared": 0, "disagreements": []}, p, detail["reason"])
        self.assertEqual((res["compared"], len(res["disagreements"])), (1, 1))   # agree stays 0

    def test_repository_lists_every_b1_entry_with_c9102_a_limit_and_a_reason(self):
        entries = cint_check.read_unsupported(ROOT / "conformance/unsupported.txt")
        b1 = {case: v for (case, compiler), v in entries.items() if compiler == B1}
        self.assertTrue(entries)   # B1 lists none since its box 09 and box 12 work; the seed does
        for case, (code, limit, reason) in b1.items():
            self.assertEqual(code, "C9102", case)
            self.assertTrue(limit.startswith("CINTC-OQ-"), case)
            self.assertGreater(len(reason.split()), 4, case)
            self.assertTrue((ROOT / "conformance" / (case + ".ci")).is_file(), case)


class ModuleOrder(unittest.TestCase):
    def test_import_before_importer(self):
        self.assertEqual(cint_check.module_order(ROOT / "conformance", "module/import_plain.ci"),
                         ["module/lib/plain_math.ci", "module/import_plain.ci"])

    def test_a_path_that_differs_in_case_names_no_file(self):
        self.assertEqual(cint_check.module_order(ROOT / "conformance", "module/c3030_case_fold.ci"),
                         ["module/lib/plain_math.ci", "module/c3030_case_fold.ci"])

    def test_dependency_order_with_ties_in_path_order(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            (root / "p").mkdir()
            files = {"main.ci": "// import z; is a comment\nimport p.b;\nimport p.a;\n",
                     "p/a.ci": "import p.c;\n", "p/b.ci": "  import p.c;\nimport p.missing;\n",
                     "p/c.ci": "export I64 f() { return 1; }\n"}
            for rel, text in files.items():
                (root / rel).write_bytes(text.encode("ascii"))
            self.assertEqual(cint_check.module_order(root, "main.ci"),
                             ["p/c.ci", "p/a.ci", "p/b.ci", "main.ci"])

    def test_single_module(self):
        self.assertEqual(cint_check.module_order(ROOT / "conformance", "arith/bitwise_not.ci"),
                         ["arith/bitwise_not.ci"])


if __name__ == "__main__":
    unittest.main()
