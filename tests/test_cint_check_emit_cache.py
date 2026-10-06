"""tools/cint_check.py --emit-cache (EmitCache, run_b1): a kept B1 compilation reads back exactly
as a fresh one, the CONF-05 second compilation never reuses the first, and a change to the B1
executable or to any module under the source root compiles again. B1 is simulated by replacing
subprocess.run; no toolchain and no B1 build are needed."""
import json
import os
import pathlib
import shutil
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))

import cint_check  # noqa: E402


class FakeB1:
    """`cint --json emit-c --root R --out O MODULE...` as B1 writes it: O/MANIFEST.ref naming
    O.stage-0/MANIFEST and one .c file per module; exit 2 with a CINT-DIAG-1 line for a module
    that holds `bad`."""

    def __init__(self):
        self.calls = []

    def __call__(self, cmd, cwd=None, capture_output=None, env=None):
        root, out, rels = pathlib.Path(cmd[cmd.index("--root") + 1]), pathlib.Path(cmd[cmd.index("--out") + 1]), \
            cmd[cmd.index("--out") + 2:]
        self.calls.append((rels, None if env is None else env.get("TZ")))
        sources = [(root / r).read_bytes() for r in rels]
        if any(b"bad" in s for s in sources):
            diag = json.dumps({"schema": "CINT-DIAG-1", "code": "C3001", "file": rels[-1], "line": 1,
                               "column": 1})
            return mock.Mock(returncode=2, stderr=(diag + "\n").encode("ascii"), stdout=b"")
        stage = out.parent / (out.name + ".stage-0")
        lines = []
        for rel, src in zip(rels, sources):
            data = b"/* " + cint_check.sha256(src).encode("ascii") + b" */\n"
            (stage / rel).parent.mkdir(parents=True, exist_ok=True)
            (stage / rel).with_suffix(".c").write_bytes(data)
            lines.append("%s %d %s" % (cint_check.sha256(data), len(data), rel[:-3] + ".c"))
        manifest = ("cint-manifest-1\n" + "".join(x + "\n" for x in sorted(lines))).encode("ascii")
        (stage / "MANIFEST").write_bytes(manifest)
        out.mkdir(parents=True, exist_ok=True)
        (out / "MANIFEST.ref").write_bytes(("%s %s\n" % (stage.name, cint_check.sha256(manifest))).encode("ascii"))
        return mock.Mock(returncode=0, stderr=b"", stdout=b"")


class EmitCacheTest(unittest.TestCase):
    def setUp(self):
        self.tmp = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        self.root = self.tmp / "src"
        (self.root / "arith").mkdir(parents=True)
        (self.root / "arith" / "add.ci").write_bytes(b"export I64 f() { return 1; }\n")
        (self.root / "arith" / "other.ci").write_bytes(b"export I64 g() { return 2; }\n")
        self.b1 = {"sha256": "a" * 64, "identity": "b" * 64}
        self.fake = FakeB1()
        patcher = mock.patch.object(cint_check.subprocess, "run", self.fake)
        patcher.start()
        self.addCleanup(patcher.stop)

    def cache(self, b1=None):
        return cint_check.EmitCache(self.tmp / "cache", b1 or self.b1)

    def run_b1(self, cache, out, env=None, rels=("arith/add.ci",)):
        return cint_check.run_b1("cint", self.root, list(rels), self.tmp / out / "b1" / "out", env, cache)

    def test_a_kept_compilation_reads_back_as_the_fresh_one(self):
        first = self.run_b1(self.cache(), "a")
        cache = self.cache()   # another configuration's run
        again = self.run_b1(cache, "b")
        self.assertEqual(len(self.fake.calls), 1)
        self.assertEqual((again[0], again[1], again[2][1]), (first[0], first[1], first[2][1]))
        self.assertEqual(again[2][0], self.tmp / "b" / "b1" / "out.stage-0")
        for rel, data in again[2][1].items():
            self.assertEqual((again[2][0] / rel).read_bytes(), data)
        self.assertEqual(cache.counts, {"emitted": 0, "reused": 1})

    def test_the_second_compilation_never_reuses_the_first(self):
        env = dict(os.environ, **cint_check.ALT_ENV)
        self.run_b1(self.cache(), "a")
        cache = self.cache()
        self.run_b1(cache, "alt", env)
        self.assertEqual(self.fake.calls, [(["arith/add.ci"], None), (["arith/add.ci"], "Pacific/Kiritimati")])
        self.run_b1(cache, "alt2", env)
        self.assertEqual(len(self.fake.calls), 2)
        self.assertEqual(cache.counts, {"emitted": 1, "reused": 1})

    def test_any_module_under_the_root_or_another_b1_compiles_again(self):
        self.run_b1(self.cache(), "a")
        self.run_b1(self.cache(dict(self.b1, sha256="c" * 64)), "b")
        (self.root / "arith" / "other.ci").write_bytes(b"export I64 g() { return 3; }\n")
        self.run_b1(self.cache(), "c")
        self.assertEqual(len(self.fake.calls), 3)

    def test_a_compile_error_is_kept_with_its_diagnostic(self):
        (self.root / "arith" / "add.ci").write_bytes(b"bad\n")
        first = self.run_b1(self.cache(), "a")
        again = self.run_b1(self.cache(), "b")
        self.assertEqual(len(self.fake.calls), 1)
        self.assertEqual(first[0], 2)
        self.assertEqual(again, first)
        self.assertEqual(cint_check.b1_diagnostic(again[1])[1], "diagnostic.code C3001")

    def test_only_an_output_set_or_a_compile_error_is_kept(self):
        cache = self.cache()
        key = cache.key(self.root, ["arith/add.ci"], None)
        for code in (1, 5, 7, -9):
            cache.store(key, code, "", None, None)
        self.assertEqual(list((self.tmp / "cache").iterdir()), [])

    def test_a_damaged_entry_compiles_again(self):
        self.run_b1(self.cache(), "a")
        entry = next(p for p in (self.tmp / "cache").iterdir() if not p.name.startswith("tmp-"))
        (entry / "set" / "MANIFEST").unlink()
        got = self.run_b1(self.cache(), "b")
        self.assertEqual(len(self.fake.calls), 2)
        self.assertEqual(got[0], 0)

    def test_a_second_store_of_one_key_keeps_the_first(self):
        cache = self.cache()
        key = cache.key(self.root, ["arith/add.ci"], None)
        cache.store(key, 2, "first", None, None)
        cache.store(key, 2, "second", None, None)
        self.assertEqual(json.loads((self.tmp / "cache" / key / "entry.json").read_bytes())["stderr"], "first")
        self.assertEqual([p.name for p in (self.tmp / "cache").iterdir()], [key])


if __name__ == "__main__":
    unittest.main()
