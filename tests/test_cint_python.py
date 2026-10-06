"""SPEC-09 RCPT-11 in tools/cint_python.py (box 10 default BX10-21): which tests cover which case,
when a run passes, the reuse check of a bootstrap, and --compare. Synthetic results only, plus
one listing of python/tests that runs no test and needs no compiler."""
import contextlib
import io
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import cint_bootstrap
import cint_check
import cint_python

CR09 = {"int64": {"format": "l", "elem": "I64"}, "longlong": {"format": "q", "elem": "I64"}}
DEMO = {"exit_status": 0, "stdout_equal": True, "stdout_sha256": "0" * 64, "stderr": ""}


def suite(**results):
    """A worker report: one passing test per case, the optional tests, and the given results."""
    cases = {"tests.test_x.Case%s.test_it" % c: [c] for c in cint_python.CASES}
    cases.update({t: [] for t in cint_python.OPTIONAL})
    cases["tests.test_x.Other.test_plain"] = []
    tests = {t: {"result": "pass", "detail": None} for t in cases}
    for test, result in results.items():
        test = test.replace("__", ".")
        cases.setdefault(test, [])
        tests[test] = {"result": result[0], "detail": result[1]}
    return {"cases": cases, "tests": tests, "versions": {}, "cr09": CR09}


class CasesOf(unittest.TestCase):
    def test_lines(self):
        self.assertEqual(cint_python.cases_of("Case 16: `cint.borrow` of float64"), ["16"])
        self.assertEqual(cint_python.cases_of("Cases 23, 23a, 24: refused before entry."), ["23", "23a", "24"])
        self.assertEqual(cint_python.cases_of("Cases 23 and 23a: numpy.int32 is refused"), ["23", "23a"])
        self.assertEqual(cint_python.cases_of("Cases 15 and 15a, by forging what"), ["15", "15a"])
        self.assertEqual(cint_python.cases_of("Case 12a without NumPy: a DLPack output"), ["12a"])
        self.assertEqual(cint_python.cases_of("\n    Case 19 for a function's inout.\n    More."), ["19"])

    def test_not_a_case(self):
        for doc in (None, "", "P-18 (case 19 at the argument layer): Busy.", "Cases of the table.",
                    "A transposed, a reversed and a sliced NumPy array."):
            self.assertEqual(cint_python.cases_of(doc), [], doc)


class Listing(unittest.TestCase):
    """python/tests as the worker discovers it: every case of box 10 has a test, and every
    optional test exists, so a renamed test cannot drop out of a receipt unnoticed."""

    def test_python_tests(self):
        proc = subprocess.run([sys.executable, str(ROOT / "tools" / "cint_python.py"), "--list"], cwd=ROOT,
                              capture_output=True, text=True, timeout=600)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        rows = {}
        for line in proc.stdout.splitlines():
            test, _, rest = line.partition(" ")
            rows[test] = rest.split(" optional: ")[0].split(",") if rest and not rest.startswith("optional") else []
        self.assertGreater(len(rows), 300)
        for case in cint_python.CASES:
            self.assertTrue(any(case in cases for cases in rows.values()), case)
        for test in cint_python.OPTIONAL:
            self.assertIn(test, rows)
        self.assertEqual(rows["tests.test_kernels.Dispatch.test_case_20a"], ["20a"])
        self.assertEqual(rows["tests.test_calls.Scalars.test_refusals"], ["23", "23a", "24"])


class Outcome(unittest.TestCase):
    def test_pass(self):
        outcome, optional = cint_python.outcome_of(suite(), DEMO)
        self.assertEqual(outcome["result"], "pass", outcome["problems"])
        self.assertEqual(outcome["cr09"], {"int64": "I64", "longlong": "I64"})
        self.assertEqual(outcome["cases"]["21"], {"tests": 1, "pass": True})
        self.assertEqual(len(outcome["passed"]), len(cint_python.CASES) + 1)
        self.assertEqual([o["test"] for o in optional], sorted(cint_python.OPTIONAL))

    def test_optional_results_are_observations(self):
        torch = next(iter(cint_python.OPTIONAL)).replace(".", "__")
        ran, _ = cint_python.outcome_of(suite(), DEMO)
        skipped, optional = cint_python.outcome_of(suite(**{torch: ("skip", "PyTorch is not installed")}), DEMO)
        self.assertEqual(ran, skipped)
        self.assertIn({"test": torch.replace("__", "."), "result": "skip", "detail": "PyTorch is not installed"},
                      optional)
        failed, _ = cint_python.outcome_of(suite(**{torch: ("fail", "AssertionError")}), DEMO)
        self.assertEqual(failed["result"], "fail")

    def test_required_skip_fails(self):
        outcome, _ = cint_python.outcome_of(suite(tests__test_x__Case13__test_it=("skip", "NumPy is not installed")),
                                            DEMO)
        self.assertEqual(outcome["result"], "fail")
        self.assertEqual(outcome["cases"]["13"], {"tests": 0, "pass": False})
        self.assertIn("case 13 has no passing required test", outcome["problems"])
        self.assertEqual(outcome["not_passed"], [{"test": "tests.test_x.Case13.test_it", "result": "skip",
                                                  "detail": "NumPy is not installed"}])

    def test_a_failing_test_fails_its_case(self):
        report = suite(tests__test_x__Case17a__test_more=("error", "TypeError: boom"))
        report["cases"]["tests.test_x.Case17a.test_more"] = ["17a"]
        outcome, _ = cint_python.outcome_of(report, DEMO)
        self.assertEqual(outcome["cases"]["17a"], {"tests": 1, "pass": False})
        self.assertEqual(outcome["result"], "fail")

    def test_not_run_and_module_errors(self):
        report = suite()
        del report["tests"]["tests.test_x.Other.test_plain"]
        report["tests"]["setUpModule (tests.test_calls)"] = {"result": "skip", "detail": "no cint"}
        outcome, _ = cint_python.outcome_of(report, DEMO)
        self.assertEqual(outcome["result"], "fail")
        self.assertIn({"test": "tests.test_x.Other.test_plain", "result": "not run", "detail": None},
                      outcome["not_passed"])
        self.assertIn("setUpModule (tests.test_calls): skip, no cint", outcome["problems"])

    def test_numpy_and_the_demonstration(self):
        report = suite()
        report["cr09"] = None
        outcome, _ = cint_python.outcome_of(report, dict(DEMO, exit_status=1, stdout_equal=False))
        self.assertEqual(outcome["result"], "fail")
        self.assertIsNone(outcome["cr09"])
        self.assertEqual(len(outcome["problems"]), 2)
        report = suite()
        report["cr09"] = {"int64": {"format": "l", "elem": "I32"}}
        self.assertEqual(cint_python.outcome_of(report, DEMO)[0]["result"], "fail")


class Bootstrap(unittest.TestCase):
    """A bootstrap is reused only when its retained inputs are this tree's, file for file."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        base = pathlib.Path(self.tmp.name)
        self.inputs = base / "build-1" / "inputs"
        cint_bootstrap.snapshot_inputs(ROOT, self.inputs)
        self.exe = base / "cint"
        self.exe.write_bytes(b"a cint")
        (base / "cint.toolchain").write_text("cint-toolchain-1\ninclude %s\nexecutable_sha256 %s\n" % (
            self.inputs / "rt", cint_check.sha256(b"a cint")), encoding="utf-8")

    def tearDown(self):
        self.tmp.cleanup()

    def test_this_tree(self):
        self.assertIsNone(cint_python.tree_problem(self.exe))

    def test_other_inputs(self):
        (self.inputs / "rt" / "cint_rt.c").write_bytes(b"/* other */\n")
        self.assertIn("rt/cint_rt.c", cint_python.tree_problem(self.exe))
        (self.inputs / "rt" / "cint_rt.c").unlink()
        self.assertIn("rt/cint_rt.c", cint_python.tree_problem(self.exe))

    def test_other_executable(self):
        self.exe.write_bytes(b"another cint")
        self.assertIn("not the executable", cint_python.tree_problem(self.exe))


class Compare(unittest.TestCase):
    def write(self, directory, name, identity, outcome, leg):
        receipt = {"schema": cint_python.SCHEMA, "identity": identity, "outcome": outcome,
                   "observations": {"configuration": {"leg": leg}, "cr09_formats": {"int64": "q"},
                                    "python": {"implementation": "CPython", "python": "3.11.0", "numpy": "2.0.0"}},
                   "identity_sha256": cint_check.sha256(cint_check.canonical({"identity": identity,
                                                                              "outcome": outcome}))}
        path = os.path.join(directory, name)
        with open(path, "wb") as f:
            f.write(cint_check.canonical(receipt) + b"\n")
        return path

    def test_compare(self):
        identity, outcome = {"python_tree_sha256": "1" * 64}, {"result": "pass", "passed": ["t"]}
        with tempfile.TemporaryDirectory() as tmp, contextlib.redirect_stdout(io.StringIO()), \
                contextlib.redirect_stderr(io.StringIO()):
            a = self.write(tmp, "python-msvc.json", identity, outcome, "msvc")
            b = self.write(tmp, "python-gcc.json", identity, outcome, "gcc")
            self.assertTrue(cint_python.compare([a, b]))
            c = self.write(tmp, "python-clang.json", identity, dict(outcome, passed=["t", "u"]), "clang")
            self.assertFalse(cint_python.compare([a, c]))
            with open(b, "rb") as f:
                receipt = json.loads(f.read())
            receipt["outcome"]["result"] = "fail"
            with open(b, "wb") as f:
                f.write(cint_check.canonical(receipt) + b"\n")
            self.assertFalse(cint_python.compare([a, b]))
            self.assertFalse(cint_python.compare([]))


if __name__ == "__main__":
    unittest.main()
