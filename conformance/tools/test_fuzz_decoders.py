"""Tests for conformance/tools/fuzz_decoders.py (slice 2 patch D-10): the fuzz
targets run, find no failure, and are deterministic for a seed.

Run from the repository root:
    python -m unittest discover -s conformance/tools -p "test_*.py" -v
"""

import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import fuzz_decoders  # noqa: E402


class FuzzTargets(unittest.TestCase):
    def test_record_target(self):
        accepted, rejected, failures, digest = fuzz_decoders.fuzz_record(7, 3000)
        self.assertEqual(failures, [])
        self.assertEqual(accepted + rejected, len(fuzz_decoders.record_corpus()) + 3000)
        self.assertGreater(accepted, len(fuzz_decoders.record_corpus()))
        self.assertGreater(rejected, 1000)
        self.assertEqual(fuzz_decoders.fuzz_record(7, 3000)[3], digest)
        self.assertNotEqual(fuzz_decoders.fuzz_record(8, 3000)[3], digest)

    def test_expect_target(self):
        accepted, rejected, failures, digest = fuzz_decoders.fuzz_expect(7, 3000)
        self.assertEqual(failures, [])
        self.assertGreater(accepted, len(fuzz_decoders.expect_corpus()))
        self.assertGreater(rejected, 1000)
        self.assertEqual(fuzz_decoders.fuzz_expect(7, 3000)[3], digest)

    def test_corpus_holds_both_versions_and_kinds(self):
        corpus = fuzz_decoders.record_corpus()
        self.assertEqual({k for _, k in corpus}, {"compile-time", "run-time"})
        self.assertEqual({b[4:24] for b, _ in corpus}, {b"cint-core-1/fault/v1", b"cint-core-1/fault/v2"})
        self.assertTrue(any(b"format 2\n" in t for t in fuzz_decoders.expect_corpus()))

    def test_draws_are_bounded(self):
        d = fuzz_decoders.Draw(1)
        self.assertTrue(all(0 <= d.below(5) < 5 for _ in range(200)))
        self.assertEqual(d.below(1), 0)


if __name__ == "__main__":
    unittest.main()
