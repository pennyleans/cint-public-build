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
        # each names a case of the suite and a capability that back_ptx names (cap_name); 16 since
        # fixed/kernel_scale_reduce_q80_48 (OQ-294), 18 since the two lane Q kernels of S-1 U-1
        # (kernel/lane_q_half_sweep, kernel/lane_q_i16_halving; OQ-338)
        entries = cint_check.read_unsupported(ROOT / "conformance" / "unsupported.txt")
        names = set(re.findall(r'w_str\(w, dst, "([a-z.]+)"\)',
                               (ROOT / "compiler" / "back_ptx.ci").read_text(encoding="utf-8")))
        gpu = {case: v for (case, compiler), v in entries.items() if compiler == GPU}
        self.assertEqual(len(gpu), 18)
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


QBE = cint_check.QBE_TOKEN
QBE_REFUSAL = {"backend": "cpu-qbe", "capability": "storage", "limit": "BACK-18", "node": 2,
               "position": "t/x.ci:4:15"}
QBE_SSA = """data $cg_1_t_xb0 = { b 114, b 117, b 110 }
data $cg_1_t_xs0 = { b 29 0 0 0 20 }
data $cg_1_t_exports = align 8 { l $cg_1_t_xb0, w 3, w 0, w 1, w 1, w 0, w 0, l 0, l 0, l $cg_1_t_xs0, l 5, l $cx_1_t_3_run }
export function w $cx_1_t_3_run(l %ctx, l %fuel, l %result) {
"""


class QbeBackend(unittest.TestCase):
    """The cpu-qbe axis (SPEC-09 BACK-18): its unsupported.txt lines, refusals and SSA rows."""

    def write(self, text):
        return UnsupportedLines.write(self, text)

    def test_line_names_back18_and_a_capability(self):
        entries = cint_check.read_unsupported(self.write(
            "a/b %s E_UNSUPPORTED BACK-18 capability storage module state is Q2\n" % QBE))
        self.assertEqual(entries[("a/b", QBE)][1], "BACK-18")
        for text in ("a/b %s E_UNSUPPORTED BACK-05 capability storage x\n" % QBE,
                     "a/b %s C9102 BACK-18 capability storage x\n" % QBE,
                     "a/b %s E_UNSUPPORTED BACK-18 no capability first\n" % QBE):
            with self.subTest(text=text), self.assertRaises(ValueError):
                cint_check.read_unsupported(self.write(text))

    def test_repository_lines(self):
        # each names a case of the suite and a capability of cli/cint_cache.c qbe_capability
        names = {"entry", "storage", "node", "type", "module", "symbol"}
        entries = cint_check.read_unsupported(ROOT / "conformance" / "unsupported.txt")
        qbe = {case: v for (case, compiler), v in entries.items() if compiler == QBE}
        self.assertTrue(qbe)
        for case, (code, limit, reason) in qbe.items():
            self.assertEqual((code, limit), ("E_UNSUPPORTED", "BACK-18"), case)
            self.assertIn(reason.split(" ")[1], names, case)
            self.assertTrue((ROOT / "conformance" / (case + ".ci")).is_file() or
                            case.startswith(("anchors/", "tables/")), case)

    def test_refusal_category(self):
        listed = {("t/x", QBE): ("E_UNSUPPORTED", "BACK-18", "capability storage Q2")}
        category, detail = cint_check.backend_refusal("t/x", QBE_REFUSAL, listed, QBE)
        self.assertEqual((category, detail["compiler"]), ("unsupported", QBE))
        for other in ({}, {("t/x", QBE): ("E_UNSUPPORTED", "BACK-18", "capability node Q2")}):
            category, detail = cint_check.backend_refusal("t/x", QBE_REFUSAL, other, QBE)
            self.assertEqual(category, "disagreement")
            self.assertIn("back_qbe refuses the program at t/x.ci:4:15", detail["reason"])

    def test_refusal_message(self):
        m = cint_check.QBE_REFUSAL_RE.search(
            '{"message":"E_UNSUPPORTED cpu-qbe capability symbol node 0 at 3:12"}')
        self.assertEqual(m.groups(), ("symbol", "0", "3", "12"))

    def test_ssa_rows(self):
        self.assertEqual(cint_check.qbe_table_rows(QBE_SSA), ([("run", bytes([29, 0, 0, 0, 20]))], []))
        for old, new, words in (("l 5,", "l 6,", "inconsistently"), ("w 3,", "w 4,", "inconsistently"),
                                ("$cx_1_t_3_run(", "$cx_1_t_3_go(", "does not define")):
            with self.subTest(new=new):
                rows, problems = cint_check.qbe_table_rows(QBE_SSA.replace(old, new, 1))
                self.assertTrue(any(words in p for p in problems), problems)

    def test_options(self):
        base = ["--leg", "gcc", "--opt", "2", "--helpers", "builtin", "--backend", "cpu-qbe"]
        for extra in ([], ["--compiler", "b1"], ["--compiler", "b1", "--no-receipt", "--stage", "T2"],
                      ["--compiler", "b1", "--no-receipt", "--interp"]):
            with self.subTest(extra=extra):
                err = io.StringIO()
                with self.assertRaises(SystemExit), contextlib.redirect_stderr(err):
                    cint_check.main(base + extra)
                self.assertIn("--backend cpu-qbe", err.getvalue())


if __name__ == "__main__":
    unittest.main()
