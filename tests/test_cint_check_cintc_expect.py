"""The cintc expectations of the boot/ refusal cases (decision 26, item 2; SPEC-09
CONF-11 as proposed; compiler/OPEN.md CINTC-OQ-48). Python standard library only; no
toolchain, no compiler build."""
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import cint_check  # noqa: E402

gen_expect = cint_check.gen_expect
CONF = ROOT / "conformance"
# `defer` and error sets: frozen from cint_ref, which runs them since box 12 (ref/OPEN.md I-8, the
# box 12 freeze), and compared since B1's box 12 work (compiler/OPEN.md CINTC-OQ-63). Kernels:
# frozen from cint_ref, which runs them since box 09 (ref/OPEN.md I-7, the box 09 freeze), and
# compared since B1's box 09 work (decision 2026-10-06 on OQ-193).
COMPARED = {"print", "profile_line", "static_assert", "ternary_literal", "test_block", "defer", "error_union",
            "kernel"}
# A frozen cintc expectation that B1 refuses with C9102 under CINTC-OQ-15; none since box 09.
UNSUPPORTED = set()


def seed_cases():
    out = {}
    for ci in sorted((CONF / "boot").glob("*.ci")):
        text = ci.read_bytes().decode("utf-8")
        case = "boot/" + ci.stem
        if gen_expect.seed_expect(text, gen_expect.header(text), case) is not None:
            out[case] = (ci, text)
    return out


class CintcExpectations(unittest.TestCase):
    def test_each_refusal_case_has_one_cintc_expectation_or_is_held(self):
        held = gen_expect.read_held()
        cases = seed_cases()
        self.assertEqual(len(cases), 16)
        for case, (ci, text) in cases.items():
            with self.subTest(case=case):
                frozen = ci.with_name(ci.stem + ".cintc.expect")
                listed = case + ".cintc" in held
                self.assertNotEqual(frozen.is_file(), listed)
                self.assertEqual(frozen.is_file(), ci.stem[len("refuse_"):] in COMPARED | UNSUPPORTED)
                if listed:
                    self.assertEqual(held[case + ".cintc"][0], "I-2")
                # The seed's frozen file is unchanged: still the bytes of its header line.
                seed = gen_expect.seed_expect(text, gen_expect.header(text), case)
                self.assertEqual((CONF / (case + ".expect")).read_bytes(), seed)

    def test_cintc_files_are_reference_values_of_their_spec_outcome(self):
        for case, (ci, text) in seed_cases().items():
            frozen = ci.with_name(ci.stem + ".cintc.expect")
            if not frozen.is_file():
                continue
            with self.subTest(case=case):
                lines = frozen.read_text(encoding="ascii").split("\n")
                self.assertEqual(lines[:3], ["case " + case, "clause " + gen_expect.header(text)["clause"],
                                             "source reference"])
                self.assertEqual(lines[3], "outcome value")
                spec = next(line for line in text.split("\n") if line.startswith("// spec outcome: "))
                value = spec.split("value I64 ", 1)[1].split(" ", 1)[0]
                self.assertIn("return I64 " + value, lines)

    def test_held_cintc_line_needs_a_boot_case(self):
        with tempfile.TemporaryDirectory(prefix="held-") as directory:
            path = pathlib.Path(directory) / "held.txt"
            path.write_bytes(b"arith/add_i64_overflow.cintc I-2 not a boot case\n")
            with self.assertRaises(ValueError):
                gen_expect.read_held(str(path))

    def inventory(self, compiler):
        lists = {"held": gen_expect.read_held(), "unsupported": cint_check.read_unsupported(CONF / "unsupported.txt"),
                 "compiler": compiler}
        cats, problems = {}, []
        programs = cint_check.inventory_program_cases(["boot/refuse_"], cats, problems, lists)
        self.assertEqual(problems, [])
        return {p.name: p for p in programs}, cats

    def test_b1_compares_the_cintc_file_and_holds_the_rest(self):
        programs, cats = self.inventory(cint_check.COMPILERS["b1"])
        self.assertEqual(set(programs), {"boot/refuse_" + n for n in COMPARED | UNSUPPORTED})
        for name, p in programs.items():
            self.assertEqual([f.name for f, _, _ in p.frozen], [name[5:] + ".cintc.expect"])
            self.assertFalse(p.seed_only)
        self.assertEqual(len(cats), 8)
        self.assertTrue(all(c == ("held", {"open_item": "I-2"}) for c in cats.values()))

    def test_seed_keeps_its_own_frozen_files(self):
        programs, cats = self.inventory(cint_check.COMPILERS["seed"])
        self.assertEqual(len(programs), 16)
        self.assertEqual(cats, {})
        for name, p in programs.items():
            self.assertTrue(p.seed_only)
            self.assertEqual([f.name for f, _, _ in p.frozen], [name[5:] + ".expect"])

    def test_required_lists_name_the_same_categories(self):
        entries = cint_check.read_required(CONF / "required/T2.txt")
        for case in seed_cases():
            with self.subTest(case=case):
                name = case[len("boot/refuse_"):]
                want = ("compared", None) if name in COMPARED else \
                    ("unsupported", "CINTC-OQ-15") if name in UNSUPPORTED else ("held", "I-2")
                self.assertEqual(entries[(case, "cintc")], want)


if __name__ == "__main__":
    unittest.main()
