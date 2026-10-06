"""The CONF-15 boot-scope exclusion rule of tools/cint_check.py and tools/cint_accept.py.

Slice 2 plan task 2.14, after gate G-C1 finding EV-1: a case that a required list records as
`# Boot scope exclusion: <case> <reason>` and lists in no row for the compiler is a row of
category `excluded`, not "unlisted". The rule reads the list; it does not approve it (OQ-174).
Synthetic lists only; no compiler runs.
"""
import copy
import hashlib
import json
import pathlib
import re
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import cint_accept  # noqa: E402
import cint_check  # noqa: E402
import cint_receipts  # noqa: E402

LIST = (b"# Status: Draft.\n"
        b"# Boot scope exclusion: arith/core_only SPEC-09 5.5; outside cint-boot-1\n"
        b"# Boot scope exclusion: arith/both SPEC-09 5.5; outside cint-boot-1\n"
        b"arith/both cint-seed outside_subset\n"
        b"arith/both cintc compared\n"
        b"arith/x cint-seed compared\n"
        b"arith/x cintc compared\n")


class ExclusionRule(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory(prefix="synthetic-exclusions-")
        self.addCleanup(tmp.cleanup)
        self.path = pathlib.Path(tmp.name) / "T2-boot.txt"
        self.path.write_bytes(LIST)
        self.required = cint_check.read_required(self.path)
        self.exclusions = cint_check.read_exclusions(self.path)

    def check(self, actual, compiler="cintc"):
        return cint_check.check_required(self.required, actual, compiler, lambda c: True,
                                         self.exclusions)

    def test_reads_the_exclusion_comments(self):
        self.assertEqual(sorted(self.exclusions), ["arith/both", "arith/core_only"])
        self.assertEqual(self.exclusions["arith/core_only"], "SPEC-09 5.5; outside cint-boot-1")

    def test_an_excluded_case_is_no_problem_in_any_category(self):
        for category, detail in (("compared", {}), ("unsupported", {"limit": "CINTC-OQ-15"}),
                                 ("held", {"open_item": "REF-OQ-1"}), ("outside_subset", {})):
            with self.subTest(category=category):
                actual = {"arith/x": ("compared", {}), "arith/both": ("compared", {}),
                          "arith/core_only": (category, detail)}
                self.assertEqual(self.check(actual), [])

    def test_a_row_wins_over_an_exclusion(self):
        actual = {"arith/x": ("compared", {}), "arith/both": ("unsupported", {"limit": "CINTC-OQ-15"})}
        self.assertEqual(self.check(actual),
                         ["arith/both: category unsupported CINTC-OQ-15; the required list says compared"])
        self.assertFalse(cint_check.excluded(self.required, self.exclusions, "arith/both", "cintc"))
        self.assertTrue(cint_check.excluded(self.required, self.exclusions, "arith/core_only", "cintc"))

    def test_an_unexcluded_unlisted_case_is_still_a_problem(self):
        actual = {"arith/x": ("compared", {}), "arith/both": ("compared", {}), "arith/new": ("compared", {})}
        self.assertEqual(self.check(actual), ["arith/new: not in the required list (category compared)"])

    def test_without_exclusions_the_old_reading_holds(self):
        actual = {"arith/x": ("compared", {}), "arith/both": ("compared", {}),
                  "arith/core_only": ("compared", {})}
        self.assertEqual(cint_check.check_required(self.required, actual, "cintc", lambda c: True),
                         ["arith/core_only: not in the required list (category compared)"])

    def test_an_excluded_case_absent_from_the_run_is_no_problem(self):
        self.assertEqual(self.check({"arith/x": ("compared", {}), "arith/both": ("compared", {})}), [])

    def test_malformed_and_repeated_exclusions_fail(self):
        for text in (b"# Boot scope exclusion: arith/x\n", b"# Boot scope exclusion: Bad Case reason\n",
                     b"# Boot scope exclusion: arith/x a\n# Boot scope exclusion: arith/x b\n"):
            with self.subTest(text=text):
                self.path.write_bytes(text)
                with self.assertRaises(ValueError):
                    cint_check.read_exclusions(self.path)

    def test_the_draft_list_reads_the_exclusions_its_header_counts(self):
        path = ROOT / "conformance" / "required" / "T2-boot.txt"
        exclusions = cint_check.read_exclusions(path)
        stated = re.search(rb"# The following (\d+) core-only", path.read_bytes())
        self.assertIsNotNone(stated)
        self.assertEqual(len(exclusions), int(stated.group(1)))
        required = cint_check.read_required(path)
        self.assertEqual([c for c in exclusions if (c, "cintc") in required], [])


class AcceptanceReadsExclusions(unittest.TestCase):
    """cint_accept.check_conformance reads the same rule from the list it is given."""

    def setUp(self):
        tmp = tempfile.TemporaryDirectory(prefix="synthetic-accept-exclusions-")
        self.addCleanup(tmp.cleanup)
        self.directory = pathlib.Path(tmp.name)
        self.required = self.directory / "required.txt"
        self.required.write_bytes(b"# Boot scope exclusion: arith/core_only SPEC-09 5.5\n"
                                  b"arith/x cint-seed compared\narith/x cintc compared\n")
        self.receipt = copy.deepcopy(json.loads((ROOT / "results/cint/slice2/t1-gb/"
                                                "receipt-msvc-0-portable.json").read_bytes()))
        self.receipt["outcome"]["required"].update(
            file="required.txt", sha256=hashlib.sha256(self.required.read_bytes()).hexdigest())
        self.receipt["outcome"]["not_compared"] = {
            name: [] for name in ("held", "not_applicable", "outside_subset", "unsupported")}
        self.receipt["outcome"]["tables_regenerated"] = {"differ": [], "files": 1, "match": 1}
        self.path = self.directory / "receipt-msvc-0-portable.json"
        self.current = dict(self.receipt["identity"])
        for field in ("emitted_c_count", "emitted_c_method", "emitted_c_tree_sha256"):
            self.current.pop(field)
        self.entries = cint_check.read_required(self.required)

    def problems(self):
        self.receipt["observations"]["receipt_identity_sha256"] = cint_receipts.receipt_identity(self.receipt)
        self.path.write_bytes(cint_receipts.canonical(self.receipt) + b"\n")
        return cint_accept.check_conformance(self.path, ("msvc", "0", "portable", False), "seed",
                                             self.required, self.entries, self.current)["problems"]

    def test_an_excluded_held_case_is_not_unlisted(self):
        self.receipt["outcome"]["not_compared"]["held"] = [{"case": "arith/core_only", "open_item": "REF-OQ-1"}]
        self.assertNotIn("unlisted or wrong category: arith/core_only", self.problems())
        self.receipt["outcome"]["not_compared"]["held"] = [{"case": "arith/other", "open_item": "REF-OQ-1"}]
        self.assertIn("unlisted or wrong category: arith/other", self.problems())


if __name__ == "__main__":
    unittest.main()
