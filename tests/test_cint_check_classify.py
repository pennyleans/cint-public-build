"""tools/cint_check.py classify(): a compiler failure without a diagnostic keeps the head of
the compiler's stderr, and exit status 5 reads as an environment failure (#485). Python
standard library only; no toolchain, no compiler build."""
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import cint_check  # noqa: E402

FROZEN = ["outcome computed"]


def classify(rc, stderr, diagnostic=cint_check.b1_diagnostic):
    return cint_check.classify("arith/x", "cint-core-1", FROZEN, rc, diagnostic(stderr), {},
                               compiler=cint_check.COMPILERS["b1"])


class NoDiagnostic(unittest.TestCase):

    def test_stderr_head_kept(self):
        category, detail = classify(3, "cint: out of memory\n  at stage 2\n")
        self.assertEqual(category, "disagreement")
        self.assertNotIn("environment", detail)
        self.assertEqual(detail["reason"], "the compiler failed with exit status 3 and no diagnostic; "
                                           "stderr: cint: out of memory at stage 2")

    def test_seed_stderr_head_kept(self):
        category, detail = classify(1, "seed: abort\n", cint_check.seed_diagnostic)
        self.assertEqual(detail["reason"], "the compiler failed with exit status 1 and no diagnostic; "
                                           "stderr: seed: abort")

    def test_environment(self):
        category, detail = classify(cint_check.EXIT_ENVIRONMENT, "cint: cannot create /w/out/a.c: No space\n")
        self.assertEqual(category, "disagreement")                 # the run still fails
        self.assertTrue(detail["environment"])
        self.assertEqual(detail["reason"], "an environment failure, not a compiler result: exit status 5 "
                                           "(CINT_EXIT_ENVIRONMENT); stderr: cint: cannot create "
                                           "/w/out/a.c: No space")

    def test_environment_silent(self):
        _, detail = classify(cint_check.EXIT_ENVIRONMENT, "")
        self.assertTrue(detail["reason"].endswith("(CINT_EXIT_ENVIRONMENT), and nothing on stderr"))


if __name__ == "__main__":
    unittest.main()
