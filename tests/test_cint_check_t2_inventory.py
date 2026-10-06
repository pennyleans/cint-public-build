"""Checks requirement coverage against available source fixtures, without runs."""
import json
import pathlib
import re
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import cint_check


class T2Inventory(unittest.TestCase):
    def available(self):
        cases = set()
        for category in cint_check.gen_expect.CATEGORIES:
            for source in (ROOT / "conformance" / category).glob("*.ci"):
                case = source.relative_to(ROOT / "conformance").as_posix()[:-3]
                cases.add(case)
        cases.update("tables/" + path.stem for path in (ROOT / "conformance/tables").glob("*.ci"))
        cases.update("anchors/" + json.loads(line)["id"] for line in
                     (ROOT / "conformance/integer-machine/anchors.cif1.jsonl").read_text().splitlines())
        return cases

    def test_every_available_pair_is_required_for_both_compilers(self):
        path = ROOT / "conformance/required/T2.txt"
        entries = cint_check.read_required(path)
        cases = self.available()
        self.assertEqual({case for case, compiler in entries if compiler == "cint-seed"}, cases)
        self.assertFalse(re.findall(r"^# B1 expectation gap: ", path.read_text(), re.M))
        b1 = {case for case, compiler in entries if compiler == "cintc"}
        self.assertEqual(b1, cases)
        self.assertTrue(all(category != "outside_subset" for (case, compiler), (category, detail)
                            in entries.items() if compiler == "cintc"))

    def test_t1_seed_inventory_covers_every_available_case(self):
        entries = cint_check.read_required(ROOT / "conformance/required/T1.txt")
        self.assertEqual({case for case, compiler in entries if compiler == "cint-seed"}, self.available())

    def test_seed_classifications_agree_between_full_scope_lists(self):
        t1 = cint_check.read_required(ROOT / "conformance/required/T1.txt")
        t2 = cint_check.read_required(ROOT / "conformance/required/T2.txt")
        for key in t1.keys() & t2.keys():
            with self.subTest(case=key[0]):
                self.assertEqual(t1[key], t2[key])

    def test_b1_scalar_gains_and_future_anchors_have_their_required_categories(self):
        entries = cint_check.read_required(ROOT / "conformance/required/T2.txt")
        for case in ("anchors/add.sat.i8.001", "anchors/abs.checked.i64.001",
                     "anchors/div_round.checked.i64.half_even.001",
                     "anchors/muldiv.checked.i64.i64.floor.001", "anchors/min.checked.i64.001"):
            with self.subTest(case=case):
                self.assertEqual(entries.get((case, "cintc")), ("compared", None))
        for case in ("anchors/add.checked.i128.001", "anchors/mul_full.checked.i64.i128.001",
                     "anchors/sum_sat.sat.i8.001", "anchors/mul.checked.q16_16.half_even.001"):
            with self.subTest(case=case):
                self.assertEqual(entries.get((case, "cintc")), ("not_applicable", None))
        self.assertEqual(entries.get(("control/fuel_total_n3", "cintc")), ("held", "I-2"))
        self.assertEqual(entries[("control/nest_blocks_257", "cintc")], ("compared", None))
        self.assertEqual(entries[("control/nest_blocks_257", "cint-seed")], ("unsupported", "SEED-15"))

    def test_boot_inventory_accounts_for_full_suite_with_explicit_scope_exclusions(self):
        path = ROOT / "conformance/required/T2-boot.txt"
        entries = cint_check.read_required(path)
        exclusions = set(re.findall(r"^# Boot scope exclusion: ([^ ]+) ", path.read_text(), re.M))
        listed = {case for case, compiler in entries if compiler == "cintc"}
        self.assertFalse(exclusions & listed)
        self.assertEqual(exclusions | listed, self.available())
        # B1 compares the saturating anchors since task 2.14b; they have no cint-boot-1 form.
        self.assertIn("anchors/add.sat.i8.001", exclusions)
        self.assertEqual(entries.get(("anchors/sum_sat.sat.i8.001", "cintc")), ("not_applicable", None))
        self.assertEqual(entries.get(("control/fuel_total_n3", "cintc")), ("held", "I-2"))
        self.assertEqual(entries[("arith/const_narrow_literal_2p3000", "cintc")], ("compared", None))

    def test_exhaustive_saturating_tables_are_required_of_b1(self):
        # Task 2.14 closed the six fixture gaps: the 8-bit saturating tables exist, B1 compares
        # them, and the seed has them outside its subset (SPEC-09 9.3, CONF-14).
        path = ROOT / "conformance/required/T2.txt"
        entries = cint_check.read_required(path)
        self.assertFalse(re.findall(r"^# Future fixture gap: ", path.read_text(), re.M))
        for case in ("tables/" + op + "_sat_" + kind for op in ("add", "sub", "mul")
                     for kind in ("i8", "u8")):
            with self.subTest(case=case):
                self.assertTrue((ROOT / "conformance" / (case + ".ci")).is_file())
                self.assertEqual(entries[(case, "cintc")], ("compared", None))
                self.assertEqual(entries[(case, "cint-seed")], ("outside_subset", None))


if __name__ == "__main__":
    unittest.main()
