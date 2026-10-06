"""Fixture coverage for SPEC-09 CINTC-03 and the SEED-15 boundaries.

Derives return values and fuel from SPEC-04 8.2/8.4 and SPEC-01 10.1.
Preserves the existing long-branch and block/while fixtures unchanged.
"""

from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "ref"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from cint_ref.lexer import tokenize
import gen_expect


class ControlBoundaries(unittest.TestCase):
    def source(self, case):
        path = ROOT / "conformance" / (case + ".ci")
        raw = path.read_bytes()
        self.assertNotIn(b"\r", raw, case)
        self.assertTrue(raw.endswith(b"\n"), case)
        return path, raw.decode("ascii")

    def tokens_and_depth(self, case):
        _, text = self.source(case)
        tokens = tokenize(text, case + ".ci")
        spellings = [token.text for token in tokens]
        depth = maximum = 0
        for spelling in spellings:
            if spelling == "{":
                depth += 1
                maximum = max(maximum, depth)
            elif spelling == "}":
                depth -= 1
                self.assertGreaterEqual(depth, 0, case)
        self.assertEqual(depth, 0, case)
        return spellings, maximum

    def test_long_branch_coverage(self):
        for case, keyword, count in (
            ("control/else_if_126", "if", 126),
            ("control/else_if_1000", "if", 1000),
            ("control/else_if_1000_minimal", "if", 1000),
            ("switch/arms_1000", "case", 1000),
            ("switch/arms_1000_minimal", "case", 1000),
        ):
            with self.subTest(case=case):
                spellings, depth = self.tokens_and_depth(case)
                self.assertEqual(spellings.count(keyword), count)
                self.assertEqual(depth, 2)

    def test_seed_boundary_coverage(self):
        for case, keyword, count, depth, weight in (
            ("control/nest_blocks_199", "{", 200, 200, 1),
            ("control/nest_blocks_200", "{", 201, 201, 1),
            ("control/nest_whiles_66", "while", 66, 67, 3),
            ("control/nest_whiles_67", "while", 67, 68, 3),
            ("control/nest_for_49", "for", 49, 50, 4),
            ("control/nest_for_c_49", "for", 49, 50, 4),
        ):
            with self.subTest(case=case):
                spellings, actual_depth = self.tokens_and_depth(case)
                self.assertEqual(spellings.count(keyword), count)
                self.assertEqual(actual_depth, depth)
                levels = 1 + (depth - 1) * weight
                within_limit = case.endswith(("_199", "_66", "_49"))
                self.assertEqual(levels <= 200, within_limit)

    def test_new_headers_and_reference_outcomes(self):
        for case, outcomes, fuel in (
            ("control/else_if_126", {"first": 0, "middle": 126, "last": 250, "fallback": -2}, 2),
            ("control/nest_for_49", {"run": 1}, 50),
            ("control/nest_for_c_49", {"run": 1}, 50),
        ):
            path, text = self.source(case)
            header = gen_expect.header(text)
            self.assertEqual(header["case"], case)
            self.assertTrue(header["clause"])
            self.assertEqual(header["entries"], list(outcomes))
            self.assertEqual(header["args"], [])
            self.assertIn("// subset: cint-boot-1\n", text)
            for entry, value in outcomes.items():
                with self.subTest(case=case, entry=entry):
                    suffix = "." + entry if len(outcomes) > 1 else ""
                    frozen = path.with_suffix(suffix + ".expect").read_bytes()
                    code, actual, error = gen_expect.run_case(str(path), header, entry)
                    self.assertEqual(code, 0, error.decode("ascii", "replace"))
                    self.assertEqual(actual, frozen)
                    self.assertIn(b"\noutcome value\n", frozen)
                    self.assertIn(("\nreturn I64 %d\n" % value).encode("ascii"), frozen)
                    self.assertIn(("\nfuel-consumed %d\n" % fuel).encode("ascii"), frozen)


if __name__ == "__main__":
    unittest.main()
