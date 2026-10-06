"""The interpreter leg of tools/cint_check.py (--interp; box 13 unit 2, SPEC-09 BACK-01a,
CONF-10a): how a run of cint-interp is read. A program the interpreter declines for a form it
does not run yet is not compared (its reason is kept), a malformed text or a crash is not a
refusal, and a program entry's record is composed from its stdout and the records it wrote.
Python standard library only; no toolchain and no B1 build are needed."""
import contextlib
import io
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import cint_check  # noqa: E402

FUEL_DOMAIN = b"cint-core-1/fuel-consumed/v1"


def done(code, stdout=b"", stderr=b""):
    return subprocess.CompletedProcess([], code, stdout, stderr)


def fuel_record(consumed):
    return len(FUEL_DOMAIN).to_bytes(4, "little") + FUEL_DOMAIN + consumed.to_bytes(8, "little", signed=True)


class Refusal(unittest.TestCase):
    def test_unsupported_form_is_not_executed_with_its_reason(self):
        for stderr in (b"cint-interp: unsupported: struct/a.sites:5:0: local: struct values run once ...\n",
                       b"cint-harness: cannot load the program library: unsupported: struct/a.sites:5:0: local\n"):
            with self.subTest(stderr=stderr):
                res = {}
                self.assertTrue(cint_check.interp_refused(res, done(3, stderr=stderr)))
                self.assertTrue(res["interp_reason"].startswith("struct/a.sites:5:0: local"))

    def test_malformed_text_and_other_failures_are_not_refusals(self):
        for code, stderr in ((3, b"cint-interp: malformed SIR: a.sites:7:4: indented 3 spaces\n"),
                             (4, b"cint-harness: unsupported: \n"), (0, b""), (-11, b"")):
            with self.subTest(code=code):
                res = {}
                self.assertFalse(cint_check.interp_refused(res, done(code, stderr=stderr)))
                self.assertNotIn("interp_reason", res)

    def test_leg_requires_b1(self):
        err = io.StringIO()
        with self.assertRaises(SystemExit), contextlib.redirect_stderr(err):
            cint_check.main(["--leg", "gcc", "--opt", "0", "--helpers", "portable", "--interp"])
        self.assertIn("--interp is for --compiler b1", err.getvalue())


class EntryRecord(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = pathlib.Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_value_with_stdout(self):
        (self.dir / "fuel.bin").write_bytes(fuel_record(3))
        lines = cint_check.entry_record(done(0, b"hi\n"), self.dir / "fault.bin", self.dir / "fuel.bin")
        self.assertEqual(lines, ["outcome value", "stdout-bytes 3", "stdout-sha256 " + cint_check.sha256(b"hi\n"),
                                 "fuel-consumed 3"])

    def test_missing_or_malformed_fuel_record_is_reported(self):
        lines = cint_check.entry_record(done(0, stderr=b"boom\n"), self.dir / "fault.bin", self.dir / "fuel.bin")
        self.assertEqual(lines[0], "program exit 0")
        self.assertIn("boom", lines)
        (self.dir / "fuel.bin").write_bytes(fuel_record(3)[:-1])
        lines = cint_check.entry_record(done(0), self.dir / "fault.bin", self.dir / "fuel.bin")
        self.assertEqual(lines[:2], ["program exit 0", "malformed fuel record"])

    def test_other_exit_status_is_reported(self):
        (self.dir / "fuel.bin").write_bytes(fuel_record(0))
        lines = cint_check.entry_record(done(5), self.dir / "fault.bin", self.dir / "fuel.bin")
        self.assertEqual(lines[0], "program exit 5")


if __name__ == "__main__":
    unittest.main()
