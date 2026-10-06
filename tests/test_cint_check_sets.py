"""Conformance sets beside the stages (t27 scoping note 7.2, TT-04), without runs."""
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import cint_check


class SetSelection(unittest.TestCase):
    def lists(self, compiler="cintc"):
        return {"held": cint_check.gen_expect.read_held(), "compiler": compiler,
                "unsupported": cint_check.read_unsupported(ROOT / "conformance/unsupported.txt")}

    def test_only_leaves_unselected_anchors_unclassified(self):
        # Before t27 unit 3, an anchor without a cint-boot-1 form was classified under any
        # --only, so a run against a set's list reported it as unlisted.
        with tempfile.TemporaryDirectory(prefix="synthetic-sets-") as tmp:
            for compiler in ("cint-seed", "cintc"):
                with self.subTest(compiler=compiler):
                    cats = {}
                    programs = cint_check.inventory_anchors(pathlib.Path(tmp), ["ternary/"], cats,
                                                            self.lists(compiler))
                    self.assertEqual((programs, cats), ([], {}))
            # An anchor without a form is selected by its own name.
            cats = {}
            programs = cint_check.inventory_anchors(
                pathlib.Path(tmp), ["anchors/add.checked.i128.001"], cats, self.lists("cint-seed"))
            self.assertEqual((programs, sorted(cats)), ([], ["anchors/add.checked.i128.001"]))

    def test_the_required_list_follows_the_selection(self):
        required = {("ternary/a", "cintc"): ("compared", None),
                    ("ternary/b", "cintc"): ("compared", None),
                    ("arith/x", "cintc"): ("compared", None),
                    ("anchors/add.checked.i8.001", "cintc"): ("compared", None),
                    ("tables/add_wrap_i8", "cintc"): ("compared", None)}
        run = {"ternary/a": ("compared", {}), "ternary/b": ("compared", {})}
        scope = cint_check.required_scope(["ternary/"], False)
        self.assertEqual(cint_check.check_required(required, run, "cintc", scope), [])
        del run["ternary/b"]
        self.assertEqual(cint_check.check_required(required, run, "cintc", scope),
                         ["ternary/b: in the required list but absent from the run"])
        # Without --only every listed case is in scope; --quick leaves the tables out.
        self.assertEqual(len(cint_check.check_required(
            required, run, "cintc", cint_check.required_scope([], False))), 4)
        self.assertEqual(len(cint_check.check_required(
            required, run, "cintc", cint_check.required_scope([], True))), 3)
        # Under --only, a selected anchor is left to a full run.
        self.assertEqual(cint_check.check_required(
            required, {}, "cintc", cint_check.required_scope(["anchors/add"], False)), [])

    def test_a_written_set_list_names_the_set(self):
        with tempfile.TemporaryDirectory(prefix="synthetic-sets-") as tmp:
            path = pathlib.Path(tmp) / "t27-1.txt"
            cint_check.write_required(path, {"ternary/a": ("compared", {})}, "cintc", "t27-1")
            lines = path.read_text().splitlines()
        self.assertTrue(lines[0].startswith("# Required case list of set t27-1 "))
        self.assertEqual(lines[-1], "ternary/a cintc compared")


class T27Set1(unittest.TestCase):
    PATH = ROOT / "conformance/required/t27-1.txt"

    def test_the_list_names_every_case_of_the_set_for_both_compilers(self):
        cases = {p.relative_to(ROOT / "conformance").as_posix()[:-3]
                 for p in (ROOT / "conformance/ternary").glob("*.ci")}
        self.assertEqual(len(cases), 8)
        entries = cint_check.read_required(self.PATH)
        self.assertEqual(set(entries), {(c, k) for c in cases for k in ("cint-seed", "cintc")})
        self.assertTrue(all(entries[(c, "cintc")] == ("compared", None) for c in cases))
        # The cases print, and print statements are outside cint-boot-1 (SEED-14).
        self.assertTrue(all(entries[(c, "cint-seed")] == ("outside_subset", None) for c in cases))
        self.assertTrue(all(cint_check.selected(c, cint_check.SETS["t27-1"]) for c in cases))
        self.assertRegex(self.PATH.read_text(), r"^# Status: Approved 2026-10-05 ")

    def test_each_case_is_frozen_in_format_2_and_returns_zero(self):
        for path in sorted((ROOT / "conformance/ternary").glob("*.ci")):
            with self.subTest(case=path.stem):
                expect = path.with_suffix(".expect").read_text().splitlines()
                self.assertEqual(expect[2:5], ["format 2", "source reference", "outcome value"])
                self.assertIn("return I64 0", expect)
                text = path.read_text()
                self.assertRegex(text, r"(?m)^// subset: cint-core-1 ")
                self.assertRegex(text, r"(?m)^// entry: run; args: none$")


if __name__ == "__main__":
    unittest.main()
