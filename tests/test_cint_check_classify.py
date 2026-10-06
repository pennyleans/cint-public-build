"""The result categories of SPEC-09 CONF-14 (slice 2 decision patch D-8), as tools/cint_check.py
decides them with its pure classify(); and the readers of conformance/unsupported.txt and of a
required case list (CONF-15). Python standard library only; no toolchain is needed."""
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import cint_check  # noqa: E402
from cint_check import COMPUTED, classify  # noqa: E402

VALUE = ["outcome value", "result I64 3"]
FAULT = ["outcome fault", "fault.code E_OVERFLOW"]


def error(code, position):
    return ["outcome compile-error", "diagnostic.code " + code, "diagnostic.position " + position]


UNSUPPORTED = {("control/nest_blocks_200", "cint-seed"):
               ("C9004", "SEED-15", "the seed compiles at most 199 nested blocks")}
HELD = {"arith/stores_before_fault": ("REF-OQ-23", "the .expect layout cannot state it")}

# The eight rows of D-8: case, subset, frozen body, seed exit status, seed record, category.
ROWS = [
    # 1. Compared: the compiled program runs and its record is compared with the frozen one.
    ("arith/add_i64", "cint-boot-1", VALUE, 0, None, "compared"),
    # 1. Compared, an expected compile error: the same code at the same position.
    ("diag/c2003_x", "cint-boot-1", error("C2003", "diag/c2003_x.ci:4:9"), 2,
     error("C2003", "diag/c2003_x.ci:4:9"), "compared"),
    # 2. Unsupported by this implementation: a listed C9xxx code under a documented limit.
    ("control/nest_blocks_200", "cint-boot-1", VALUE, 2,
     error("C9004", "control/nest_blocks_200.ci:9:204"), "unsupported"),
    # 3. Held: held.txt lists the case, and it has no frozen expectation.
    ("arith/stores_before_fault", "cint-core-1", None, None, None, "held"),
    # 4. Outside the subset: a case of another subset that the seed refuses with C9100.
    ("switch/fallthrough", "cint-core-1", VALUE, 2,
     error("C9100", "switch/fallthrough.ci:13:13"), "outside_subset"),
    # 5. Not applicable: an anchor whose operation has no operator form in the subset.
    ("anchors/sat-add-i8-1", None, COMPUTED, None, None, "not_applicable"),
    # Any other refusal is a disagreement: here an unlisted C9xxx code.
    ("control/nest_blocks_201", "cint-boot-1", VALUE, 2,
     error("C9004", "control/nest_blocks_201.ci:9:204"), "disagreement"),
    # A listed unsupported case that the compiler accepts also fails.
    ("control/nest_blocks_200", "cint-boot-1", VALUE, 0, None, "disagreement"),
]


class ClassifyTable(unittest.TestCase):
    def test_eight_rows(self):
        self.assertEqual(len(ROWS), 8)
        for case, subset, frozen, rc, rec, want in ROWS:
            with self.subTest(case=case, rc=rc):
                got, _ = classify(case, subset, frozen, rc, rec, UNSUPPORTED, HELD)
                self.assertEqual(got, want)

    def test_unsupported_entry_carries_the_receipt_fields(self):
        _, detail = classify(*ROWS[2][:5], UNSUPPORTED, HELD)
        self.assertEqual(detail, {"code": "C9004", "compiler": "cint-seed", "frozen_outcome": "value",
                                  "limit": "SEED-15",
                                  "position": "control/nest_blocks_200.ci:9:204"})

    def test_held_carries_its_open_item(self):
        self.assertEqual(classify(*ROWS[3][:5], UNSUPPORTED, HELD), ("held", {"open_item": "REF-OQ-23"}))


class Disagreements(unittest.TestCase):
    def category(self, *args, **kwargs):
        return classify(*args, unsupported=UNSUPPORTED, held=HELD, **kwargs)[0]

    def test_subset_refusal_of_a_boot_case(self):
        self.assertEqual(self.category("arith/x", "cint-boot-1", VALUE, 2,
                                       error("C9100", "arith/x.ci:2:1")), "disagreement")

    def test_compile_error_at_another_position(self):
        self.assertEqual(self.category("diag/x", "cint-boot-1", error("C2003", "diag/x.ci:4:9"), 2,
                                       error("C2003", "diag/x.ci:4:10")), "disagreement")

    def test_compile_error_where_a_value_is_frozen(self):
        self.assertEqual(self.category("arith/x", "cint-boot-1", FAULT, 2,
                                       error("C2003", "arith/x.ci:4:9")), "disagreement")

    def test_accepted_program_whose_frozen_outcome_is_a_compile_error(self):
        self.assertEqual(self.category("diag/x", "cint-boot-1", error("C2003", "diag/x.ci:4:9"), 0,
                                       None), "disagreement")

    def test_listed_case_with_another_code(self):
        self.assertEqual(self.category("control/nest_blocks_200", "cint-boot-1", VALUE, 2,
                                       error("C9102", "control/nest_blocks_200.ci:9:204")),
                         "disagreement")

    def test_no_frozen_expectation_and_not_held(self):
        self.assertEqual(self.category("arith/new_case", "cint-boot-1", None, None, None),
                         "disagreement")

    def test_held_case_with_a_frozen_expectation(self):
        self.assertEqual(self.category("arith/stores_before_fault", "cint-core-1", VALUE, None,
                                       None), "disagreement")

    def test_seed_failure_without_a_diagnostic(self):
        self.assertEqual(self.category("arith/x", "cint-boot-1", VALUE, 3,
                                       ["outcome seed-failed", "seed.stderr crash"]),
                         "disagreement")


class OtherCategories(unittest.TestCase):
    def test_table_program_without_the_marker_is_outside_the_subset(self):
        self.assertEqual(classify("tables/add_sat_i64", None, COMPUTED, None, None, {}, {})[0],
                         "outside_subset")

    def test_c4040_outside_the_subset(self):
        self.assertEqual(classify("control/depth_limit", "cint-core-1", VALUE, 2,
                                  error("C4040", "control/depth_limit.ci:8:12"), {}, {})[0],
                         "outside_subset")

    def test_lexical_error_of_a_core_case_is_compared(self):
        e = error("C1003", "diag/c1003_bidi_control_in_comment.ci:8:18")
        self.assertEqual(classify("diag/c1003_bidi_control_in_comment", "cint-core-1", e, 2, e,
                                  {}, {})[0], "compared")

    def test_boot_case_compares_the_seed_outcome(self):
        e = error("C9100", "boot/x.ci:3:1")
        self.assertEqual(classify("boot/x", "cint-core-1", e, 2, e, {}, {}, seed_only=True)[0],
                         "outside_subset")
        self.assertEqual(classify("boot/x", "cint-core-1", e, 2, error("C9100", "boot/x.ci:3:2"),
                                  {}, {}, seed_only=True)[0], "disagreement")
        self.assertEqual(classify("boot/x", "cint-core-1", e, 2, error("C4040", "boot/x.ci:3:1"),
                                  {}, {}, seed_only=True)[0], "disagreement")

    def test_boot_directory_can_hold_a_reference_expectation(self):
        for frozen in (VALUE, error("C6001", "boot/x.ci:3:9")):
            with self.subTest(frozen=frozen):
                self.assertEqual(classify("boot/x", "cint-core-1", frozen, 2,
                                          error("C9100", "boot/x.ci:3:1"), {}, {})[0],
                                 "outside_subset")

    def test_pure(self):
        args = ("control/nest_blocks_200", "cint-boot-1", list(VALUE), 2,
                error("C9004", "control/nest_blocks_200.ci:9:204"), dict(UNSUPPORTED), dict(HELD))
        self.assertEqual(classify(*args), classify(*args))
        self.assertEqual(args[2], VALUE)


class Readers(unittest.TestCase):
    def write(self, text):
        d = tempfile.TemporaryDirectory()
        self.addCleanup(d.cleanup)
        path = pathlib.Path(d.name) / "list.txt"
        path.write_bytes(text.encode("ascii"))
        return path

    def test_repository_lists_parse(self):
        unsupported = cint_check.read_unsupported(ROOT / "conformance" / "unsupported.txt")
        self.assertIn(("control/nest_blocks_200", "cint-seed"), unsupported)
        required = cint_check.read_required(ROOT / "conformance" / "required" / "T1.txt")
        self.assertEqual(required[("control/nest_blocks_200", "cint-seed")],
                         ("unsupported", "SEED-15"))
        for (case, compiler), (code, limit, _) in unsupported.items():
            if compiler == "cint-seed":   # T1 is the seed's list; B1's entries are task 2.12a's
                self.assertEqual(required.get((case, compiler)), ("unsupported", limit), case)

    def test_unsupported_rejects_malformed_lines(self):
        for text in ("a/b cint-seed C2003 SEED-15 not a C9xxx code\n",
                     "a/b cint-seed C9004 SEED-15\n",
                     "a/b cint-other C9004 SEED-15 an unknown compiler\n",
                     "b/b cint-seed C9004 SEED-15 x\na/b cint-seed C9004 SEED-15 unsorted\n",
                     "a/b cint-seed C9004 SEED-15 x\na/b cint-seed C9004 SEED-15 twice\n"):
            with self.subTest(text=text):
                with self.assertRaises(ValueError):
                    cint_check.read_unsupported(self.write(text))

    def test_required_details(self):
        ok = self.write("# comment\na/b cint-seed held O-13,I-4\na/c cint-seed compared\n")
        self.assertEqual(cint_check.read_required(ok), {
            ("a/b", "cint-seed"): ("held", "O-13,I-4"), ("a/c", "cint-seed"): ("compared", None)})
        for text in ("a/b cint-seed held\n", "a/b cint-seed compared SEED-15\n",
                     "a/b cint-seed skipped\n"):
            with self.subTest(text=text):
                with self.assertRaises(ValueError):
                    cint_check.read_required(self.write(text))

    def test_check_required(self):
        required = {("a/x", "cint-seed"): ("compared", None),
                    ("a/y", "cint-seed"): ("unsupported", "SEED-15"),
                    ("tables/t", "cint-seed"): ("compared", None)}
        actual = {"a/x": ("compared", {}), "a/y": ("unsupported", {"limit": "SEED-15"})}
        self.assertEqual(cint_check.check_required(required, actual, "cint-seed",
                                                   lambda c: not c.startswith("tables/")), [])
        self.assertEqual(len(cint_check.check_required(required, actual, "cint-seed",
                                                       lambda c: True)), 1)
        actual["a/z"] = ("compared", {})
        actual["a/y"] = ("outside_subset", {})
        self.assertEqual(len(cint_check.check_required(required, actual, "cint-seed",
                                                       lambda c: False)), 2)


if __name__ == "__main__":
    unittest.main()
