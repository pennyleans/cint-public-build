"""Checks the directory form of `tools/cint_receipts.py compare` (plan task 2.3)."""
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import cint_receipts  # noqa: E402


def receipt(harness, compared):
    return {"identity": {"schema": "cint-conformance-receipt-2", "harness_sha256": harness},
            "outcome": {"pass": True, "compared": compared}}


class CompareDirectories(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.a = Path(self.tmp.name) / "a"
        self.b = Path(self.tmp.name) / "b"
        for d in (self.a, self.b):
            d.mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def put(self, d, name, obj):
        (d / name).write_bytes(json.dumps(obj).encode("ascii"))

    def run_compare(self, *flags):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = cint_receipts.main(["compare", *flags, str(self.a), str(self.b)])
        return code, out.getvalue().splitlines()

    def test_outcome_identical_while_identity_differs(self):
        for name in ("receipt-gcc-0-portable.json", "receipt-msvc-0-portable.json"):
            self.put(self.a, name, receipt("aa", 5))
            self.put(self.b, name, receipt("bb", 5))
        code, lines = self.run_compare("--outcome")
        self.assertEqual((code, lines[-1]), (0, "outcome identical in 2 of 2 receipt pairs"))
        code, lines = self.run_compare()
        self.assertEqual(code, 1)
        self.assertEqual(lines[-1], "identity and outcome identical in 0 of 2 receipt pairs")
        self.assertIn("receipt-gcc-0-portable.json: /identity/harness_sha256", lines[0])

    def test_outcome_difference_and_missing_partner(self):
        self.put(self.a, "receipt-gcc-0-portable.json", receipt("aa", 5))
        self.put(self.b, "receipt-gcc-0-portable.json", receipt("aa", 6))
        self.put(self.a, "receipt-gcc-2-portable.json", receipt("aa", 5))
        code, lines = self.run_compare("--outcome")
        self.assertEqual(code, 1)
        self.assertIn("receipt-gcc-2-portable.json: only in A", lines)
        self.assertEqual(lines[-1], "outcome identical in 0 of 2 receipt pairs")

    def test_empty_directories_fail(self):
        self.assertEqual(self.run_compare("--outcome")[0], 1)


def run_receipt(host):
    """A `cint run` receipt (D-16): no pass flag; `host` is an observation only."""
    value = {"identity": {"schema": "cint-receipt-1/run", "kind": "run", "source": {"files": {"hello.ci": "aa"}}},
             "outcome": {"kind": "value", "exit_status": 0, "fuel_consumed": "1"},
             "observations": {"host": host}}
    value["observations"]["receipt_identity_sha256"] = cint_receipts.receipt_identity(value)
    return cint_receipts.canonical(value) + b"\n"


class IdentityDirectory(unittest.TestCase):
    """`identity DIR` over run receipts (plan milestone M1: `1 receipt identity in 3 receipts`)."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.d = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def run_identity(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = cint_receipts.main(["identity", str(self.d)])
        return code, out.getvalue().splitlines()

    def test_three_run_receipts_one_identity(self):
        for leg in ("clang", "gcc", "msvc"):
            (self.d / ("run-hello-%s.json" % leg)).write_bytes(run_receipt(leg))
        code, lines = self.run_identity()
        self.assertEqual((code, lines[-1]), (0, "1 receipt identity in 3 receipts"))
        self.assertTrue(lines[0].startswith("run-hello-clang.json ") and lines[0].endswith(" value 0"), lines[0])

    def test_identity_difference_and_bad_serialization(self):
        (self.d / "run-hello-gcc.json").write_bytes(run_receipt("gcc"))
        other = json.loads(run_receipt("msvc"))
        other["outcome"]["fuel_consumed"] = "2"
        other["observations"]["receipt_identity_sha256"] = cint_receipts.receipt_identity(other)
        (self.d / "run-hello-msvc.json").write_bytes(cint_receipts.canonical(other) + b"\n")
        code, lines = self.run_identity()
        self.assertEqual((code, lines[-1]), (0, "2 receipt identities in 2 receipts"))
        (self.d / "run-hello-msvc.json").write_bytes(json.dumps(other, indent=1).encode("ascii"))
        code, lines = self.run_identity()
        self.assertEqual(code, 1)
        self.assertIn("not its RFC 8785 serialization", lines[1])

    def test_conformance_receipt_keeps_pass_flag(self):
        value = receipt("aa", 5)
        value["outcome"]["pass"] = False
        value["observations"] = {"receipt_identity_sha256": cint_receipts.receipt_identity(value)}
        (self.d / "receipt-gcc-0-portable.json").write_bytes(cint_receipts.canonical(value) + b"\n")
        code, lines = self.run_identity()
        self.assertEqual((code, lines[-1]), (1, "1 receipt identity in 1 receipt"))
        self.assertTrue(lines[0].endswith(" fail"), lines[0])

    def test_empty_directory_fails(self):
        self.assertEqual(self.run_identity(), (1, ["0 receipt identities in 0 receipts"]))


if __name__ == "__main__":
    unittest.main()
