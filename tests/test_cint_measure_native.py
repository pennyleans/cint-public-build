"""The observed B1 of tools/cint_measure.py against the production bridge (slice 2 task 2.17).

Bootstraps B1 for the host's default leg (gcc on Linux, apple-clang on macOS; msvc on Windows
only when CINT_MEASURE_NATIVE is set), links the measurement host, and builds a few inputs.
"""
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import cint_measure as measure  # noqa: E402

LEG = "msvc" if os.name == "nt" else "apple-clang" if sys.platform == "darwin" else "gcc"
ENABLED = (os.environ.get("CINT_MEASURE_NATIVE") if os.name == "nt"
           else shutil.which("clang" if LEG == "apple-clang" else LEG))


@unittest.skipUnless(ENABLED, "no %s toolchain (set CINT_MEASURE_NATIVE on Windows)" % LEG)
class Observed(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="cint-measure-test-")
        work = pathlib.Path(cls.temp.name).resolve()
        cls.bootstrap = measure.boot.bootstrap(LEG, work / "boot")
        (work / "host").mkdir()
        cls.host, cls.tc = measure.build_host(LEG, cls.bootstrap, work / "host")
        cls.work, cls.budget = work, measure.bridge_budget()

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_case(self, root, rel, name):
        return measure.run_sample(self.host, self.tc.env, root, rel, name, self.work / "runs" / name, 120,
                                  self.budget)

    def test_empty_module_calls_in_bridge_order_with_fuel(self):
        root = self.work / "empty"
        root.mkdir()
        (root / "empty.ci").write_bytes(b"")
        first, again = self.run_case(root, "empty.ci", "empty-1"), self.run_case(root, "empty.ci", "empty-2")
        self.assertEqual((first["build"], first["errors"]), (0, []))
        # plan, the discovery compile, then compile, measure, and emit of the module (D-13).
        self.assertEqual([r[0] for r in first["phases"]][:4], [1, 2, 2, 3])
        self.assertTrue(all(r[3] == 0 and r[4] == 0 and r[5] > 0 for r in first["phases"]))
        self.assertEqual([r[2] for r in first["phases"]], [0] * len(first["phases"]))
        self.assertEqual(first["phases"], again["phases"])
        self.assertEqual([t[0] for t in first["tables"]], list(range(17)))
        self.assertTrue(all(0 <= t[4] <= max(t[2], 0) for t in first["tables"]))

    def test_hooks_leave_the_committed_set_unchanged(self):
        rel = "arith/shift_left_operand_typing.ci"
        observed = self.run_case(ROOT / "conformance", rel, "observed")
        self.assertEqual(observed["build"], 0)
        out = self.work / "cli" / "out"
        out.parent.mkdir()
        cint = self.bootstrap.parent.parent / ("cint" + self.tc.exe)   # the published CLI and its toolchain file
        code, err, committed = measure.cc.run_b1(cint, ROOT / "conformance", [rel], out)
        self.assertEqual(code, 0, err)
        self.assertEqual(observed["manifest_sha256"], measure.sha256(committed[1]["MANIFEST"]))
        n = (ROOT / "conformance" / rel).stat().st_size
        self.assertEqual({r[2] for r in observed["phases"] if r[0] != 1 and r[1] >= 0}, {n})

    def test_plan_mode_gives_the_manifest_a_build_allocates(self):
        rel = "arith/shift_left_operand_typing.ci"
        observed = self.run_case(ROOT / "conformance", rel, "plan-compare")
        self.assertEqual(len(observed["modules"]), 1)
        n = observed["modules"][0][1]
        plans, errors = measure.plan_sweep(self.host, self.tc.env, [1, n, 4 * measure.MIB], 120)
        self.assertEqual(errors, [])
        self.assertEqual([[t, *row] for t, row in enumerate(plans[n])], [t[:4] for t in observed["tables"]])
        derived = measure.derive_K(plans)
        self.assertEqual(derived["name_constant_rows"], [4096])
        self.assertLessEqual(derived["K"], measure.K_CEILING)

    def test_diagnostic_build_ends_on_the_returning_call(self):
        result = self.run_case(ROOT / "conformance", "boot/refuse_where.ci", "diagnostic")
        self.assertEqual(result["build"], 1)
        self.assertEqual(result["phases"][-1][3:5], [0, 1])
        self.assertIsNotNone(result["diagnostic"])
        log = (self.work / "runs/diagnostic/stdout.log").read_bytes().splitlines()
        self.assertEqual(json.loads(log[-1])["e"], "build")


if __name__ == "__main__":
    unittest.main()
