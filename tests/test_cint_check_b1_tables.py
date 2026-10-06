"""B1 and the saturating table programs (SPEC-09 CONF-14, CONF-15, CINTC-02; SPEC-04 LS-311):
CONF-14's category 4, outside the subset, belongs to the seed only. Under --compiler b1,
tools/cint_check.py compiles a table program without the cint-boot-1 marker like any other
table, and an anchor whose operation is a saturating operator gains a program (D-15 row 2).
Python standard library only; no toolchain is needed."""
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import cint_check  # noqa: E402
from cint_check import COMPILERS, COMPUTED, classify  # noqa: E402


def lists(compiler):
    return {"unsupported": {}, "held": {}, "compiler": COMPILERS[compiler]}


class TablesWithoutTheMarker(unittest.TestCase):
    def test_seed_classifies_the_table_outside_the_subset(self):
        cats = {}
        programs = cint_check.inventory_tables(["add_sat_i64"], cats, lists("seed"))
        self.assertEqual(programs, [])
        self.assertEqual(cats["tables/add_sat_i64"][0], "outside_subset")

    def test_b1_compiles_the_table(self):
        cats = {}
        programs = cint_check.inventory_tables(["add_sat_i64"], cats, lists("b1"))
        self.assertEqual([p.name for p in programs], ["tables/add_sat_i64"])
        self.assertEqual(cats, {})
        self.assertEqual(len(programs[0].cases), 256)

    def test_cintc_has_no_outside_subset_category(self):
        got = classify("tables/add_sat_i64", None, COMPUTED, None, None, {}, {},
                       compiler=COMPILERS["b1"])
        self.assertEqual(got[0], "disagreement")
        self.assertEqual(classify("tables/add_sat_i64", None, COMPUTED, None, None, {}, {})[0],
                         "outside_subset")


class SaturatingAnchors(unittest.TestCase):
    def test_operator_form_for_cintc_only(self):
        self.assertIsNone(cint_check.anchor_form("add.sat.i8", ["I8", "I8"]))
        self.assertEqual(cint_check.anchor_form("add.sat.i8", ["I8", "I8"], COMPILERS["b1"]),
                         ("add_sat_i8_i8", "I8", ["I8", "I8"], "a +| b"))
        self.assertEqual(cint_check.anchor_form("mul.sat.u64", ["U64", "U64"], COMPILERS["b1"])[3],
                         "a *| b")
        self.assertIsNone(cint_check.anchor_form("sub.sat.i8", ["I8", "I16"], COMPILERS["b1"]))
        self.assertIsNone(cint_check.anchor_form("add.sat.i128", ["I128", "I128"],
                                                 COMPILERS["b1"]))


if __name__ == "__main__":
    unittest.main()
