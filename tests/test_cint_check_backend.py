"""The backend axis of tools/cint_check.py (SPEC-09 CONF-17; box 11 unit 6): the
`cintc/gpu-cuda` lines of conformance/unsupported.txt, the category of a kernel that back_ptx
refuses (BACK-05), and --stage. Python standard library only; no toolchain and no B1 build
are needed."""
import contextlib
import io
import pathlib
import re
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import cint_check  # noqa: E402

GPU = cint_check.GPU_TOKEN
REFUSAL = {"backend": "gpu-cuda", "kernel": "apply", "capability": "div", "limit": "BACK-05",
           "refusal": "E_UNSUPPORTED ..."}


class UnsupportedLines(unittest.TestCase):
    def write(self, text):
        d = tempfile.TemporaryDirectory()
        self.addCleanup(d.cleanup)
        path = pathlib.Path(d.name) / "unsupported.txt"
        path.write_bytes(text.encode("ascii"))
        return path

    def test_backend_line(self):
        entries = cint_check.read_unsupported(self.write(
            "a/b %s E_UNSUPPORTED BACK-05 capability div the cuda profile refuses it\n" % GPU))
        self.assertEqual(entries[("a/b", GPU)],
                         ("E_UNSUPPORTED", "BACK-05", "capability div the cuda profile refuses it"))

    def test_malformed_backend_lines(self):
        for text in ("a/b %s C9102 BACK-05 capability div x\n" % GPU,
                     "a/b %s E_UNSUPPORTED CINTC-OQ-15 capability div x\n" % GPU,
                     "a/b %s E_UNSUPPORTED BACK-05 the capability is not named first\n" % GPU,
                     "a/b cintc E_UNSUPPORTED BACK-05 capability div x\n"):
            with self.subTest(text=text):
                with self.assertRaises(ValueError):
                    cint_check.read_unsupported(self.write(text))

    def test_repository_backend_lines(self):
        # each names a case of the suite and a capability that back_ptx names (cap_name)
        entries = cint_check.read_unsupported(ROOT / "conformance" / "unsupported.txt")
        names = set(re.findall(r'w_str\(w, dst, "([a-z.]+)"\)',
                               (ROOT / "compiler" / "back_ptx.ci").read_text(encoding="utf-8")))
        gpu = {case: v for (case, compiler), v in entries.items() if compiler == GPU}
        self.assertEqual(len(gpu), 13)
        for case, (_, _, reason) in gpu.items():
            self.assertIn(reason.split(" ")[1], names, case)
            self.assertTrue((ROOT / "conformance" / (case + ".ci")).is_file(), case)


class BackendRefusal(unittest.TestCase):
    def test_listed_with_its_capability_is_unsupported(self):
        listed = {("t/x", GPU): ("E_UNSUPPORTED", "BACK-05", "capability div no division yet")}
        category, detail = cint_check.backend_refusal("t/x", REFUSAL, listed)
        self.assertEqual(category, "unsupported")
        self.assertEqual((detail["compiler"], detail["limit"], detail["kernel"]),
                         (GPU, "BACK-05", "apply"))
        self.assertEqual(cint_check.required_token(category, detail), "BACK-05")

    def test_unlisted_or_another_capability_is_a_disagreement(self):
        for listed, words in (({}, "does not list the case"),
                              ({("t/x", "cintc"): ("C9102", "CINTC-OQ-15", "capability div x")},
                               "does not list the case"),
                              ({("t/x", GPU): ("E_UNSUPPORTED", "BACK-05", "capability divx y")},
                               "names another capability")):
            with self.subTest(listed=listed):
                category, detail = cint_check.backend_refusal("t/x", REFUSAL, listed)
                self.assertEqual(category, "disagreement")
                self.assertIn(words, detail["reason"])


class Stage(unittest.TestCase):
    def test_stage_must_fit_the_compiler_and_backend(self):
        base = ["--leg", "gcc", "--opt", "0", "--helpers", "portable"]
        for extra in (["--stage", "T3"], ["--stage", "T1", "--compiler", "b1"],
                      ["--stage", "T2", "--compiler", "b1", "--backend", "gpu-cuda"],
                      ["--stage", "T3", "--compiler", "b1", "--set", "t27-1",
                       "--required", "conformance/required/t27-1.txt"],
                      ["--stage", "T4"], ["--stage", "T2", "--compiler", "b1", "--interp"],
                      ["--stage", "T4", "--compiler", "b1", "--backend", "gpu-cuda"]):
            with self.subTest(extra=extra):
                err = io.StringIO()
                with self.assertRaises(SystemExit), contextlib.redirect_stderr(err):
                    cint_check.main(base + extra)
                self.assertIn("--stage:", err.getvalue())


if __name__ == "__main__":
    unittest.main()
