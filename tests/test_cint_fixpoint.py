"""tools/cint_fixpoint.py: the FIX-02 difference report, the committed output set reader, the corpus
inventory, and the FIX-04 comparison of receipts. No toolchain and no build are needed."""
import contextlib
import hashlib
import io
import json
import pathlib
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))

import cint_check  # noqa: E402
import cint_fixpoint  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[1]


def receipt(identity, outcome, observations):
    return {"schema": cint_fixpoint.SCHEMA, "identity": identity, "outcome": outcome,
            "observations": observations,
            "identity_sha256": cint_fixpoint.sha256(cint_check.canonical({"identity": identity,
                                                                          "outcome": outcome}))}


class FirstDifference(unittest.TestCase):
    def test_equal_sets_have_none(self):
        files = {"MANIFEST": b"m\n", "main.c": b"int x;\n"}
        self.assertIsNone(cint_fixpoint.first_difference(files, dict(files)))

    def test_first_differing_file_in_byte_order_with_offset_and_line(self):
        a = {"b.c": b"one\ntwo\nthree\n", "a.c": b"same\n", "c.c": b"x"}
        b = {"b.c": b"one\ntwo\nthre3\n", "a.c": b"same\n", "c.c": b"y"}
        self.assertEqual(cint_fixpoint.first_difference(a, b),
                         {"file": "b.c", "only_in": None, "offset": 12, "line": 3})

    def test_a_prefix_differs_at_the_shorter_length(self):
        d = cint_fixpoint.first_difference({"f": b"ab\n"}, {"f": b"ab\ncd"})
        self.assertEqual((d["offset"], d["line"]), (3, 2))

    def test_a_file_on_one_side_only(self):
        self.assertEqual(cint_fixpoint.first_difference({"a": b"", "z": b""}, {"z": b""})["only_in"], "first")
        self.assertEqual(cint_fixpoint.first_difference({}, {"q": b"1"}),
                         {"file": "q", "only_in": "second", "offset": None, "line": None})


class HostReport(unittest.TestCase):
    def test_windows_text_mode_line_ends_are_undone(self):
        self.assertEqual(cint_fixpoint.host_report(b"build 1\r\ndiag 1001 0\r\n", windows=True),
                         b"build 1\ndiag 1001 0\n")

    def test_other_legs_keep_the_bytes(self):
        self.assertEqual(cint_fixpoint.host_report(b"build 0\r\n", windows=False), b"build 0\r\n")


class TreeDigest(unittest.TestCase):
    def test_cisrc001_manifest_of_the_output_set(self):
        files = {"main.c": b"int x;\n", "MANIFEST": b"MANIFEST 1\n", "back_c.sites": b""}
        framed = b"CISRC001" + struct.pack("<I", 3)
        for path in ("MANIFEST", "back_c.sites", "main.c"):   # byte order of path
            framed += struct.pack("<H", len(path)) + path.encode("ascii")
            framed += struct.pack("<Q", len(files[path])) + hashlib.sha256(files[path]).digest()
        self.assertEqual(cint_fixpoint.tree_digest(files), hashlib.sha256(framed).hexdigest())


class CommittedSet(unittest.TestCase):
    def write_set(self, base, files, ref_digest=None):
        stage = base / "out.stage-0"
        stage.mkdir(parents=True)
        lines = ["MANIFEST 1"]
        for rel, data in files.items():
            (stage / rel).write_bytes(data)
            lines.append("%s %d %s" % (hashlib.sha256(data).hexdigest(), len(data), rel))
        manifest = ("\n".join(lines) + "\n").encode("ascii")
        (stage / "MANIFEST").write_bytes(manifest)
        (base / "out").mkdir()
        (base / "out" / "MANIFEST.ref").write_bytes(
            ("out.stage-0 %s\n" % (ref_digest or hashlib.sha256(manifest).hexdigest())).encode("ascii"))
        return manifest

    def test_reads_the_set_the_manifest_names(self):
        with tempfile.TemporaryDirectory() as tmp:
            manifest = self.write_set(pathlib.Path(tmp), {"main.c": b"int x;\n", "main.sites": b"s\n"})
            got = cint_fixpoint.committed_set(pathlib.Path(tmp) / "out")
            self.assertEqual(got, {"MANIFEST": manifest, "main.c": b"int x;\n", "main.sites": b"s\n"})

    def test_no_reference_means_nothing_committed(self):
        with tempfile.TemporaryDirectory() as tmp:
            (pathlib.Path(tmp) / "out").mkdir()
            self.assertIsNone(cint_fixpoint.committed_set(pathlib.Path(tmp) / "out"))

    def test_a_reference_to_another_manifest_is_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.write_set(pathlib.Path(tmp), {"main.c": b"1"}, ref_digest="0" * 64)
            with self.assertRaises(ValueError):
                cint_fixpoint.committed_set(pathlib.Path(tmp) / "out")

    def test_a_file_that_differs_from_its_manifest_line_is_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.write_set(pathlib.Path(tmp), {"main.c": b"1"})
            (pathlib.Path(tmp) / "out.stage-0" / "main.c").write_bytes(b"2")
            with self.assertRaises(ValueError):
                cint_fixpoint.committed_set(pathlib.Path(tmp) / "out")


class CorpusInventory(unittest.TestCase):
    def test_roots_and_order(self):
        rows = cint_fixpoint.corpus_inventory(ROOT)
        names = [name for _top, _rel, name in rows]
        self.assertEqual(names, sorted(names, key=lambda n: n.encode("utf-8")))
        self.assertIn("examples/hello.ci", names)
        self.assertIn("compiler/tests/golden/add.ci", names)
        for top, rel, name in rows:
            self.assertTrue(name.endswith(".ci"))
            self.assertEqual(top + "/" + rel, name)
            if name.startswith("conformance/"):
                self.assertEqual(top, "conformance")
            else:
                self.assertNotIn("/", rel)
            self.assertTrue((ROOT / top / rel).is_file())
        self.assertIn(("conformance", "module/lib/plain_math.ci", "conformance/module/lib/plain_math.ci"),
                      rows)


class Compare(unittest.TestCase):
    def run_compare(self, receipts):
        with tempfile.TemporaryDirectory() as tmp:
            paths = []
            for i, r in enumerate(receipts):
                path = pathlib.Path(tmp) / ("r%d.json" % i)
                path.write_bytes(json.dumps(r).encode("ascii"))
                paths.append(str(path))
            out = io.StringIO()
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
                passed = cint_fixpoint.compare(paths)
            return passed, out.getvalue().splitlines()[-1]

    def config(self, leg, opt):
        return {"configuration": {"leg": leg, "opt": opt, "helpers": "portable", "sanitize": False},
                "work": "/tmp/" + leg}

    def test_one_identity_across_configurations(self):
        identity, outcome = {"compiler_source_identity": "a" * 64}, {"result": "pass", "fixpoint": {"equal": True}}
        passed, last = self.run_compare([receipt(identity, outcome, self.config("gcc", "0")),
                                         receipt(identity, outcome, self.config("clang", "2"))])
        self.assertTrue(passed)
        self.assertEqual(last, "fix-04: pass, 2 receipts, 1 receipt identity")

    def test_different_s2_is_two_identities(self):
        outcome = {"result": "pass"}
        passed, last = self.run_compare([
            receipt({"s2_tree_sha256": "1" * 64}, outcome, self.config("gcc", "0")),
            receipt({"s2_tree_sha256": "2" * 64}, outcome, self.config("gcc", "2"))])
        self.assertFalse(passed)
        self.assertEqual(last, "fix-04: fail, 2 receipts, 2 receipt identities")

    def test_a_failed_or_altered_receipt_fails(self):
        identity = {"compiler_source_identity": "a" * 64}
        failed = receipt(identity, {"result": "fail"}, self.config("gcc", "0"))
        self.assertFalse(self.run_compare([failed])[0])
        altered = receipt(identity, {"result": "pass"}, self.config("gcc", "0"))
        altered["outcome"]["corpus"] = None
        self.assertFalse(self.run_compare([altered])[0])


class Arguments(unittest.TestCase):
    def refused(self, argv):
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as raised:
            cint_fixpoint.main(argv)
        return raised.exception.code

    def test_refusals(self):
        self.assertEqual(self.refused([]), 2)
        self.assertEqual(self.refused(["--leg", "msvc", "--sanitize"]), 2)
        self.assertEqual(self.refused(["--leg", "gcc", "--receipt", "x.json", "--no-receipt"]), 2)
        self.assertEqual(self.refused(["--leg", "gcc", "--jobs", "0"]), 2)


if __name__ == "__main__":
    unittest.main()
