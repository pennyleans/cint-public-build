"""Build rt/cint_bridge.c on one leg and test it (SPEC-09 CINTC-05, CINTC-06, CINTC-09,
CINTC-14, CINTC-15; slice 2 decision patch D-3, D-13; plan task 2.6).

Usage (from the repository root):
    python rt/tests/test_bridge.py [--leg msvc|gcc|clang|apple-clang] [--out DIR]

Legs: `msvc` on Windows (MSVC 2022 through vcvars64.bat), `gcc` and `clang` on
Linux (WSL Ubuntu 24.04), and `apple-clang` on macOS (the system `clang`, which
must report Apple clang; plan task 2.2a, decision 25). Each leg first builds the seed compiler and with it
the stub compiler rt/tests/stub/main.ci and the round-trip module
rt/tests/roundtrip/touch.ci. It then builds rt/cint_bridge.c with the runtime,
the stub, and the driver rt/tests/test_bridge_main.c, and the round-trip host
rt/tests/roundtrip/host.c with touch.ci, under the strict warning flags, at -O0
and -O2 (/Od and /O2), with and without CINT_BRIDGE_TEST_HOOKS (the injected
failures); the Linux legs also build the hook variants with
-fsanitize=address,undefined. Every case then runs against every build. Build
outputs and the case directories go under <build>/bridge/<leg>/, where
<build> is the CINT_BUILD environment variable or cint-build in the system
temporary directory. On Linux the cases run a second time
in a directory on the native file system (tempfile, removed afterward), because
a WSL mount of a Windows drive ignores chmod, so a read-only directory cannot be
made there when <build> is on one. On macOS the leg builds no sanitizer
variants, as its stage configurations are unsanitized
(CONF-15), and the cases run a second time in the system temporary directory
on the boot volume, as on Linux. The Windows-only cases (junctions and sharing
modes) are skipped there, as on Linux.

The tests of plan task 2.6: (a) to (c) are RoundTrip; (d) test_build_two_modules,
test_build_diagnostic_in_module_2, test_build_emit_failure_in_module_2 and
test_build_commit_failure; (e) test_input_replaced_after_read and
test_hard_link_of_input_as_manifest_ref; (f) test_kill_after_every_file; (g)
test_phase_budget; (h) test_file_identity_hard_link, which on Windows exercises
FILE_ID_INFO. Decision 24 (each phase's status is sequenced before its result
is read): test_diagnostic_stops_later_phases.

The script is a unittest module; it uses only the Python standard library.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import pathlib
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import test_rt  # noqa: E402  (msvc_environment, available_memory, flag lists)

ROOT = HERE.parents[1]
RT = ROOT / "rt"
SEED = ROOT / "seed"
SOURCES = [RT / "cint_bridge.c", RT / "cint_build.c", RT / "cint_rt.c", HERE / "test_bridge_main.c"]
STEPS = {1: "create staging file", 2: "write", 3: "flush", 4: "close", 5: "replace"}
K, C = 21524, 19876   # CINT_PHASE_FUEL_K and _C (rt/cint_bridge.h), frozen on 2026-10-05 (OQ-175)

ARGS = argparse.Namespace(leg=None, out=None)
BUILDS: list[tuple[str, pathlib.Path, bool]] = []  # (name, executable, has hooks)
ROUNDTRIPS: list[tuple[str, pathlib.Path]] = []
STATE = {"leg": "msvc", "out": None, "env": None}


def default_out(leg: str) -> pathlib.Path:
    build = os.environ.get("CINT_BUILD") or os.path.join(tempfile.gettempdir(), "cint-build")
    return pathlib.Path(build) / "bridge" / leg


def configurations(leg: str):
    configs = [("O0-hooks", "0", True, False), ("O2-hooks", "2", True, False),
               ("O2-plain", "2", False, False)]
    if leg in ("gcc", "clang"):
        configs += [("O0-hooks-san", "0", True, True), ("O2-hooks-san", "2", True, True)]
    return configs


def compile_command(leg, opt, hooks, sanitized, outdir: pathlib.Path, sources, exe_name):
    defines = ["CINT_RT_HELPERS_PORTABLE"] + (["CINT_BRIDGE_TEST_HOOKS"] if hooks else [])
    if leg == "msvc":
        exe = outdir / (exe_name + ".exe")
        cmd = [test_rt.ENV_CL[0], "/nologo", "/std:c17", "/W4", "/WX",
               "/Od" if opt == "0" else "/O2", *[f"/D{d}" for d in defines], f"/I{RT}",
               *map(str, sources), f"/Fo{outdir}{os.sep}", f"/Fe{exe}"]
        return cmd, exe
    exe = outdir / exe_name
    cmd = [test_rt.compiler(leg), *test_rt.GNU_WARNINGS, *(test_rt.CLANG_EXTRA if leg != "gcc" else []),
           f"-O{opt}", *[f"-D{d}" for d in defines], f"-I{RT}",
           *(test_rt.SANITIZE if sanitized else []), *map(str, sources), "-o", str(exe)]
    return cmd, exe


def build(cmd, cwd, env, name):
    r = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True)
    if r.returncode != 0 or (STATE["leg"] != "msvc" and r.stderr.strip()):
        raise RuntimeError(f"{name} build failed:\n{r.stdout}\n{r.stderr}")
    warnings = [l for l in r.stdout.splitlines() if re.search(r"warning [CD]\d+", l)]
    if warnings:
        raise RuntimeError(f"{name} build printed warnings:\n" + "\n".join(warnings))


def setUpModule():
    leg = ARGS.leg or ("msvc" if os.name == "nt" else "apple-clang" if sys.platform == "darwin" else "gcc")
    out = pathlib.Path(ARGS.out) if ARGS.out else default_out(leg)
    env = test_rt.msvc_environment() if leg == "msvc" else dict(os.environ)
    if leg == "msvc":
        test_rt.ENV_CL[0] = env["CINT_RT_CL"]
    avail = test_rt.available_memory()
    if avail < test_rt.MIN_AVAILABLE_BYTES:
        raise RuntimeError(f"only {avail / 1024 ** 3:.1f} GiB available; the plan needs 8 GiB")
    STATE.update(leg=leg, out=out, env=env)
    gen = out / "gen"
    gen.mkdir(parents=True, exist_ok=True)
    cmd, seed = compile_command(leg, "2", False, False, gen, sorted(SEED.glob("*.c")) + [RT / "cint_rt.c", RT / "cint_bridge.c"],
                                "cint-seed")
    build(cmd, gen, env, "seed")
    for rel, root in (("main.ci", HERE / "stub"), ("touch.ci", HERE / "roundtrip")):
        r = subprocess.run([str(seed), rel, "-o", str(gen / (rel[:-3] + ".c")), "--root", str(root)],
                           cwd=root, env=env, capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError(f"the seed refused {rel}:\n{r.stderr}")
    for name, opt, hooks, sanitized in configurations(leg):
        outdir = out / name
        outdir.mkdir(parents=True, exist_ok=True)
        cmd, exe = compile_command(leg, opt, hooks, sanitized, outdir, SOURCES + [gen / "main.c"], "test_bridge")
        build(cmd, outdir, env, name)
        BUILDS.append((name, exe, hooks))
        rt_dir = outdir / "roundtrip"
        rt_dir.mkdir(exist_ok=True)
        cmd, exe = compile_command(leg, opt, False, sanitized, rt_dir,
                                   [HERE / "roundtrip" / "host.c", gen / "touch.c", RT / "cint_rt.c"], "host")
        build(cmd, rt_dir, env, name + " round trip")
        ROUNDTRIPS.append((name, exe))
        print(f"[{leg}] built {name}", flush=True)


def hexpath(p) -> str:
    return os.fsencode(str(p)).hex() if os.name != "nt" else str(p).encode("utf-8").hex()


def pattern(n: int) -> bytes:
    return bytes((i * 31 + 7) % 256 for i in range(n))


class Driver:
    """Runs one build's driver and checks the sanitizer channels."""

    def __init__(self, case: unittest.TestCase, exe: pathlib.Path):
        self.case, self.exe = case, exe

    def env(self):
        env = dict(STATE["env"])
        env["ASAN_OPTIONS"] = "detect_leaks=1:abort_on_error=0"
        env["UBSAN_OPTIONS"] = "print_stacktrace=1:halt_on_error=1"
        return env

    def checked(self, r):
        self.case.assertNotIn("runtime error", r.stderr)
        self.case.assertNotIn("Sanitizer", r.stderr)
        self.stdout = r.stdout
        return r.returncode

    def run(self, *argv) -> int:
        return self.checked(subprocess.run([str(self.exe), *argv], env=self.env(), capture_output=True, text=True))

    def commit(self, path, data: bytes | str, limit: int | None = None, inputs=(), fail=None) -> int:
        spec = data if isinstance(data, str) else "hex:" + data.hex()
        size = int(spec[4:]) if spec.startswith("pat:") else len(bytes.fromhex(spec[4:]))
        argv = ["commit", hexpath(path), spec, str(size if limit is None else limit)]
        for i in inputs:
            argv += ["-i", hexpath(i)]
        if fail is not None:
            argv += ["-f", str(fail)]
        return self.run(*argv)

    def read(self, root, rel, limit=1 << 20) -> int:
        return self.run("read", hexpath(root), rel.encode("utf-8").hex(), str(limit))

    def build(self, root, out, *rels, flags=()) -> int:
        return self.run("build", hexpath(root), hexpath(out), *flags, *rels)

    def build_paused(self, root, out, *rels, between=None) -> int:
        """A build that stops after reading its inputs, runs `between`, and goes on."""
        p = subprocess.Popen([str(self.exe), "build", hexpath(root), hexpath(out), "-p", *rels],
                             env=self.env(), stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, text=True)
        self.case.assertEqual(p.stdout.readline(), "paused\n")
        between()
        rest, err = p.communicate("go\n", timeout=120)
        return self.checked(subprocess.CompletedProcess(p.args, p.returncode, "paused\n" + rest, err))


def read_set(out: pathlib.Path) -> dict:
    """What a reader sees (CINTC-06 step 4): the files the MANIFEST that MANIFEST.ref names
    lists, each checked against its digest and size; {} when there is no MANIFEST.ref."""
    ref = out / "MANIFEST.ref"
    if not ref.exists():
        return {}
    name, digest = ref.read_text(encoding="ascii").rstrip("\n").split(" ")
    stage = out.parent / name
    manifest = (stage / "MANIFEST").read_bytes()
    assert hashlib.sha256(manifest).hexdigest() == digest, "MANIFEST digest"
    lines = manifest.decode("ascii").splitlines()
    assert lines[0] == "cint-manifest-1"
    files = {}
    for line in lines[1:]:
        h, n, rel = line.split(" ", 2)
        data = (stage / rel).read_bytes()
        assert hashlib.sha256(data).hexdigest() == h and len(data) == int(n), rel
        files[rel] = data
    assert list(files) == sorted(files, key=lambda s: s.encode())
    return files


class CommitCases:
    """File-system cases, run against every build in the directory `root()`."""

    scratch: pathlib.Path

    def root(self) -> pathlib.Path:
        raise NotImplementedError

    def setUp(self):
        self.scratch = self.root() / self.id().rsplit(".", 1)[-1]
        if self.scratch.exists():
            self.rmtree(self.scratch)
        self.scratch.mkdir(parents=True)

    @staticmethod
    def rmtree(p: pathlib.Path):
        def onexc(func, path, _exc):
            os.chmod(path, stat.S_IRWXU)
            func(path)
        shutil.rmtree(p, onexc=onexc) if sys.version_info >= (3, 12) else shutil.rmtree(p)

    def builds(self, hooks_only=False):
        for name, exe, hooks in BUILDS:
            if hooks_only and not hooks:
                continue
            d = self.scratch / name
            d.mkdir()
            yield d, Driver(self, exe)

    def assertOnly(self, d: pathlib.Path, names):
        """No staging file or other residue is left in d."""
        self.assertEqual(sorted(os.listdir(d)), sorted(names))

    # Commit to a new path, over an existing file, empty, at and over the limit.
    def test_commit_new_path(self):
        for d, drv in self.builds():
            with self.subTest(build=d.name):
                self.assertEqual(drv.commit(d / "out.c", b"int x;\n"), 0)
                self.assertEqual((d / "out.c").read_bytes(), b"int x;\n")
                self.assertOnly(d, ["out.c"])

    def test_commit_over_existing_file(self):
        for d, drv in self.builds():
            with self.subTest(build=d.name):
                out = d / "out.c"
                out.write_bytes(b"old contents, longer than the new ones\n")
                self.assertEqual(drv.commit(out, b"new\n"), 0)
                self.assertEqual(out.read_bytes(), b"new\n")
                self.assertOnly(d, ["out.c"])

    def test_commit_empty_and_large(self):
        for d, drv in self.builds():
            with self.subTest(build=d.name):
                self.assertEqual(drv.commit(d / "empty", b""), 0)
                self.assertEqual((d / "empty").read_bytes(), b"")
                self.assertEqual(drv.commit(d / "large", "pat:3145739"), 0)  # 3 MiB + 11 bytes
                self.assertEqual((d / "large").read_bytes(), pattern(3145739))
                self.assertOnly(d, ["empty", "large"])

    def test_len_at_and_above_limit(self):
        for d, drv in self.builds():
            with self.subTest(build=d.name):
                out = d / "out"
                out.write_bytes(b"old\n")
                self.assertEqual(drv.commit(out, b"12345", limit=4), 1)
                self.assertEqual(out.read_bytes(), b"old\n")
                self.assertEqual(drv.commit(d / "new", b"12345", limit=4), 1)
                self.assertOnly(d, ["out"])
                self.assertEqual(drv.commit(out, b"1234", limit=4), 0)
                self.assertEqual(out.read_bytes(), b"1234")

    # Aliases between an input the bridge read and the output.
    def test_output_is_the_input(self):
        for d, drv in self.builds():
            with self.subTest(build=d.name):
                src = d / "main.ci"
                src.write_bytes(b"source\n")
                self.assertEqual(drv.commit(src, b"clobber\n", inputs=[src]), 1)
                self.assertEqual(src.read_bytes(), b"source\n")
                self.assertOnly(d, ["main.ci"])

    def test_hardlink_alias_of_input(self):
        for d, drv in self.builds():
            with self.subTest(build=d.name):
                src, out = d / "main.ci", d / "out.c"
                src.write_bytes(b"source\n")
                os.link(src, out)
                self.assertEqual(drv.commit(out, b"clobber\n", inputs=[src]), 1)
                self.assertEqual(src.read_bytes(), b"source\n")
                self.assertEqual(out.read_bytes(), b"source\n")
                self.assertEqual(os.stat(src).st_nlink, 2)
                self.assertOnly(d, ["main.ci", "out.c"])
                # The same hard link is replaced when the source is not an input.
                self.assertEqual(drv.commit(out, b"new\n", inputs=[]), 0)
                self.assertEqual(src.read_bytes(), b"source\n")
                self.assertEqual(out.read_bytes(), b"new\n")

    def make_symlink(self, target, link):
        try:
            os.symlink(target, link)
        except OSError as e:
            if os.name == "nt" and getattr(e, "winerror", None) == 1314:
                self.skipTest("SeCreateSymbolicLinkPrivilege not held (Windows developer mode off); "
                              "reparse refusal is covered by test_junction_output")
            raise

    def test_symlink_alias_of_input(self):
        for d, drv in self.builds():
            with self.subTest(build=d.name):
                src, out = d / "main.ci", d / "out.c"
                src.write_bytes(b"source\n")
                self.make_symlink(src, out)
                self.assertEqual(drv.commit(out, b"clobber\n", inputs=[src]), 1)
                self.assertEqual(src.read_bytes(), b"source\n")
                self.assertTrue(os.path.islink(out))
                self.assertOnly(d, ["main.ci", "out.c"])

    def test_symlink_output_refused_even_when_not_an_input(self):
        for d, drv in self.builds():
            with self.subTest(build=d.name):
                other, out = d / "other", d / "out.c"
                other.write_bytes(b"other\n")
                self.make_symlink(other, out)
                self.assertEqual(drv.commit(out, b"x\n"), 1)
                self.make_symlink(d / "missing", d / "dangling")
                self.assertEqual(drv.commit(d / "dangling", b"x\n"), 1)
                self.assertEqual(other.read_bytes(), b"other\n")
                self.assertOnly(d, ["other", "out.c", "dangling"])

    def test_input_through_symlink_is_refused(self):
        for d, drv in self.builds():
            with self.subTest(build=d.name):
                out, link = d / "out.c", d / "link.ci"
                out.write_bytes(b"old\n")
                self.make_symlink(out, link)
                self.assertEqual(drv.commit(out, b"new\n", inputs=[link]), 2)   # the reader refuses links
                self.assertEqual(out.read_bytes(), b"old\n")

    def test_directory_output_refused(self):
        for d, drv in self.builds():
            with self.subTest(build=d.name):
                (d / "sub").mkdir()
                self.assertEqual(drv.commit(d / "sub", b"x"), 1)
                self.assertTrue((d / "sub").is_dir())
                self.assertOnly(d, ["sub"])

    def test_path_forms_refused(self):
        for d, drv in self.builds():
            with self.subTest(build=d.name):
                self.assertEqual(drv.run("commit", "", "hex:", "0"), 1)
                self.assertEqual(drv.commit(str(d) + "/", b"x"), 1)
                self.assertEqual(drv.commit(d / "missing-dir" / "out", b"x"), 1)
                bad = os.fsencode(str(d / "bad")) if os.name != "nt" else str(d / "bad").encode()
                for tail in (b"\xff", b"\xc0\xaf", b"\xed\xa0\x80", b"\xf4\x90\x80\x80", b"\xe2\x82"):
                    self.assertEqual(drv.run("commit", (bad + tail).hex(), "hex:00", "1"), 1)
                self.assertOnly(d, [])

    def test_read_of_missing_input_fails(self):
        for d, drv in self.builds():
            with self.subTest(build=d.name):
                self.assertEqual(drv.commit(d / "out", b"x", inputs=[d / "nope"]), 2)

    # Failures after the checks: the old output survives byte for byte.
    def test_injected_failure_at_every_step(self):
        old = b"old output \x00\xff\n"
        for d, drv in self.builds(hooks_only=True):
            with self.subTest(build=d.name):
                out = d / "out.c"
                out.write_bytes(old)
                for step, what in STEPS.items():
                    with self.subTest(step=what):
                        self.assertEqual(drv.commit(out, "pat:70000", fail=step), 1)
                        self.assertEqual(out.read_bytes(), old)
                        self.assertOnly(d, ["out.c"])
                for step in STEPS:
                    self.assertEqual(drv.commit(d / f"new{step}", b"x", fail=step), 1)
                self.assertOnly(d, ["out.c"])

    def test_read_only_directory(self):
        old = b"old output\n"
        for d, drv in self.builds():
            with self.subTest(build=d.name):
                out = d / "out.c"
                out.write_bytes(old)
                with self.read_only(d):
                    self.assertEqual(drv.commit(out, b"new output\n"), 1)
                self.assertEqual(out.read_bytes(), old)
                self.assertOnly(d, ["out.c"])

    def read_only(self, d: pathlib.Path):
        case = self

        class Guard:
            def __enter__(self):
                if os.name == "nt":
                    # Deny "create files" and "create folders" on the directory to Everyone.
                    subprocess.run(["icacls", str(d), "/deny", "*S-1-1-0:(WD,AD)"], check=True,
                                   capture_output=True)
                else:
                    os.chmod(d, stat.S_IRUSR | stat.S_IXUSR)
                probe = d / "probe"
                try:
                    probe.write_bytes(b"")
                except PermissionError:
                    return self
                probe.unlink()
                self.__exit__()
                case.skipTest(f"{d} stays writable after chmod (root, or a mount that ignores modes)")

            def __exit__(self, *exc):
                if os.name == "nt":
                    subprocess.run(["icacls", str(d), "/remove:d", "*S-1-1-0"], check=True,
                                   capture_output=True)
                else:
                    os.chmod(d, stat.S_IRWXU)
                return False

        return Guard()

    def test_existing_staging_name_is_skipped(self):
        for d, drv in self.builds():
            with self.subTest(build=d.name):
                out = d / "out.c"
                (d / "out.c.stage-0").write_bytes(b"someone else's staging file\n")
                self.assertEqual(drv.commit(out, b"new\n"), 0)
                self.assertEqual(out.read_bytes(), b"new\n")
                self.assertEqual((d / "out.c.stage-0").read_bytes(), b"someone else's staging file\n")
                self.assertOnly(d, ["out.c", "out.c.stage-0"])

    def test_non_bmp_path(self):
        name = "out-\U0001F600-\U00010348-\u00e9.c"
        for d, drv in self.builds():
            with self.subTest(build=d.name):
                sub = d / "dir-\U0001D11E"
                sub.mkdir()
                (sub / name).write_bytes(b"old\n")
                self.assertEqual(drv.commit(sub / name, b"new \xf0\x9f\x98\x80\n"), 0)
                self.assertEqual(os.listdir(sub), [name])
                self.assertEqual((sub / name).read_bytes(), b"new \xf0\x9f\x98\x80\n")

    # Inputs by handle (CINTC-15 "Inputs", D-3).
    def test_read_input(self):
        for d, drv in self.builds():
            with self.subTest(build=d.name):
                (d / "sub").mkdir()
                (d / "sub" / "a.ci").write_bytes(b"source\n")
                self.assertEqual(drv.read(d, "sub/a.ci"), 0)
                self.assertEqual(drv.stdout, "read 7 %s\n" % hashlib.sha256(b"source\n").hexdigest())
                self.assertEqual(drv.read(d, "sub/a.ci", limit=7), 0)
                self.assertEqual(drv.read(d, "sub/a.ci", limit=6), 1)
                (d / "empty.ci").write_bytes(b"")
                self.assertEqual(drv.read(d, "empty.ci"), 0)

    def test_read_input_refusals(self):
        for d, drv in self.builds():
            with self.subTest(build=d.name):
                (d / "sub").mkdir()
                (d / "sub" / "a.ci").write_bytes(b"source\n")
                (d / "a.ci").write_bytes(b"x\n")
                for rel in ("sub", "missing.ci", "../a.ci", "sub/../a.ci", "./a.ci", "sub//a.ci", "/a.ci",
                            "a.ci/", "con.ci", "sub/NUL", "Lpt9.ci", "com1", "a.", "sub\\a.ci", "a:b.ci",
                            "a b.ci", "é.ci", ""):
                    with self.subTest(rel=rel):
                        self.assertEqual(drv.read(d, rel), 1)
                # A name that differs from the entry only in case is refused on every host: by
                # the final path on Windows, and by the directory entry on POSIX, also where the
                # file system folds case, such as the WSL mount of F: (CINTC-12; RT-OQ-29).
                for rel in ("A.ci", "SUB/a.ci", "sub/A.ci"):
                    with self.subTest(rel=rel):
                        self.assertEqual(drv.read(d, rel), 1)
                self.assertEqual(drv.read(d, "sub/a.ci"), 0)

    def test_read_through_symlink(self):
        for d, drv in self.builds():
            with self.subTest(build=d.name):
                (d / "sub").mkdir()
                (d / "sub" / "a.ci").write_bytes(b"source\n")
                self.make_symlink(d / "sub" / "a.ci", d / "link.ci")
                self.assertEqual(drv.read(d, "link.ci"), 1)
                if os.name != "nt":
                    os.symlink(d / "sub", d / "dirlink")
                    self.assertEqual(drv.read(d, "dirlink/a.ci"), 1)
                    self.assertEqual(drv.read(d, "sub/a.ci"), 0)

    @unittest.skipUnless(os.name == "nt", "Windows junctions")
    def test_read_through_junction(self):
        import _winapi
        for d, drv in self.builds():
            with self.subTest(build=d.name):
                (d / "sub").mkdir()
                (d / "sub" / "a.ci").write_bytes(b"source\n")
                _winapi.CreateJunction(str(d / "sub"), str(d / "jdir"))
                self.assertEqual(drv.read(d, "jdir/a.ci"), 1)
                self.assertEqual(drv.read(d, "sub/a.ci"), 0)

    def test_file_identity_hard_link(self):
        """(h) The identity recorded from the read handle (FILE_ID_INFO on Windows, device
        and inode on POSIX) names the file under every name."""
        for d, drv in self.builds():
            with self.subTest(build=d.name):
                src, link, copy = d / "a.ci", d / "link.c", d / "copy.c"
                src.write_bytes(b"source\n")
                os.link(src, link)
                copy.write_bytes(b"source\n")
                self.assertEqual(drv.commit(link, b"clobber\n", inputs=[src]), 1)
                self.assertEqual(drv.commit(copy, b"new\n", inputs=[src]), 0)
                self.assertEqual(src.read_bytes(), b"source\n")
                self.assertEqual(copy.read_bytes(), b"new\n")

    # A build through the stub compiler (CINTC-05, CINTC-06, D-13).
    MODS = {"lib/util.ci": b"line one\nline two\n", "main.ci": b"export I64 f() {\n    return 1;\n}\n"}

    def project(self, d: pathlib.Path, mods: dict) -> pathlib.Path:
        proj = d / "proj"
        for rel, data in mods.items():
            (proj / rel).parent.mkdir(parents=True, exist_ok=True)
            (proj / rel).write_bytes(data)
        return proj

    @staticmethod
    def expected_set(mods: dict) -> dict:
        files = {"cint-program.c": bytes(range(97, 97 + len(mods)))}
        for i, (rel, data) in enumerate(mods.items()):
            files[rel[:-3] + ".c"] = data
            files[rel[:-3] + ".sites"] = b"%d\n" % (i % 10)
        return dict(sorted(files.items(), key=lambda kv: kv[0].encode()))

    def old_set(self, d, drv):
        """A committed build of MODS: the set a failed build must leave unchanged."""
        proj, out = self.project(d, self.MODS), d / "out"
        self.assertEqual(drv.build(proj, out, *self.MODS), 0, drv.stdout)
        self.assertEqual(read_set(out), self.expected_set(self.MODS))
        return proj, out, (out / "MANIFEST.ref").read_bytes(), sorted(os.listdir(d))

    def assertUnchanged(self, d, out, ref, names):
        self.assertEqual((out / "MANIFEST.ref").read_bytes(), ref)
        self.assertEqual(read_set(out), self.expected_set(self.MODS))
        self.assertEqual(sorted(os.listdir(d)), names)
        self.assertEqual(os.listdir(out), ["MANIFEST.ref"])

    def test_build_two_modules(self):
        for d, drv in self.builds():
            with self.subTest(build=d.name):
                proj, out, _, names = self.old_set(d, drv)
                self.assertEqual(names, ["out", "out.stage-0", "proj"])
                mods = dict(self.MODS, **{"main.ci": b"export I64 g() {\n    return 2;\n}\n"})
                (proj / "main.ci").write_bytes(mods["main.ci"])
                self.assertEqual(drv.build(proj, out, *mods), 0)
                self.assertEqual(read_set(out), self.expected_set(mods))
                self.assertEqual(sorted(os.listdir(d)), ["out", "out.stage-1", "proj"])   # CINTC-06 step 4

    def test_empty_module_output_is_revision_input_in_both_orders(self):
        empty_digest = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
        for d, drv in self.builds(hooks_only=True):
            with self.subTest(build=d.name):
                proj, out, _, _ = self.old_set(d, drv)
                (proj / "a.ci").write_bytes(b"^")
                (proj / "z.ci").write_bytes(b"normal\n")
                for rels, z_site in ((('a.ci', 'z.ci'), b'1\n'), (('z.ci', 'a.ci'), b'0\n')):
                    with self.subTest(order=rels):
                        self.assertEqual(drv.build(proj, out, *rels, flags=("-l",)), 0, drv.stdout)
                        self.assertEqual(drv.stdout, "build 0\nphases 1 4 3 7\n")
                        self.assertEqual(read_set(out), {
                            "a.c": b"^", "a.sites": b"", "cint-program.c": b"ab",
                            "z.c": b"normal\n", "z.sites": z_site,
                        })
                        stage_name = (out / "MANIFEST.ref").read_text(encoding="ascii").split(" ", 1)[0]
                        manifest = (d / stage_name / "MANIFEST").read_text(encoding="ascii")
                        self.assertIn(f"{empty_digest} 0 a.sites\n", manifest)
                        self.assertEqual(len([n for n in os.listdir(d) if n.startswith("out.stage-")]), 1)

    def test_empty_module_output_failure_preserves_old_set(self):
        for d, drv in self.builds(hooks_only=True):
            with self.subTest(build=d.name):
                proj, out, ref, _ = self.old_set(d, drv)
                (proj / "a.ci").write_bytes(b"^")
                (proj / "z.ci").write_bytes(b"#late\n")
                names, before = sorted(os.listdir(d)), self.snapshot(d)
                self.assertEqual(drv.build(proj, out, "a.ci", "z.ci", flags=("-w", "2", "-l")), 4)
                self.assertEqual(drv.stdout, "build 4\nphases 1 3 1 2\n")
                self.assertUnchanged(d, out, ref, names)
                self.assertEqual(self.snapshot(d), before)
                self.assertEqual(drv.build(proj, out, "a.ci", "z.ci", flags=("-l",)), 1)
                self.assertEqual(drv.stdout, "build 1\ndiag 2003 1 1 1 0 0\nphases 1 4 2 2\n")
                self.assertUnchanged(d, out, ref, names)
                self.assertEqual(self.snapshot(d), before)

    def test_consecutive_builds_leave_one_stage(self):
        """CINTC-06 step 4: a commit removes the stage the replaced MANIFEST.ref named."""
        for d, drv in self.builds():
            with self.subTest(build=d.name):
                proj, out, _, _ = self.old_set(d, drv)
                for _ in range(3):
                    self.assertEqual(drv.build(proj, out, *self.MODS), 0)
                    stages = [n for n in os.listdir(d) if n.startswith("out.stage-")]
                    self.assertEqual(len(stages), 1, stages)
                    self.assertEqual(read_set(out), self.expected_set(self.MODS))

    def test_more_builds_than_stage_tries(self):
        """With 2 stage names, 6 builds into one output all commit: the old stage is
        removed, so its name is free again (the 10,000-build failure of the review)."""
        for d, drv in self.builds(hooks_only=True):
            with self.subTest(build=d.name):
                proj, out = self.project(d, self.MODS), d / "out"
                for _ in range(6):
                    self.assertEqual(drv.build(proj, out, *self.MODS, flags=("-t", "2")), 0, drv.stdout)
                    self.assertEqual(read_set(out), self.expected_set(self.MODS))

    def test_stale_reference_does_not_remove_other_directories(self):
        """Only a sibling `<out name>.stage-<N>` whose MANIFEST matches the reference is removed."""
        for d, drv in self.builds():
            with self.subTest(build=d.name):
                proj, out, _, _ = self.old_set(d, drv)
                (d / "keep").mkdir()
                (d / "keep" / "MANIFEST").write_bytes(b"cint-manifest-1\n")
                (out / "MANIFEST.ref").write_bytes(b"keep " + b"0" * 64 + b"\n")
                self.assertEqual(drv.build(proj, out, *self.MODS), 0)
                self.assertEqual(os.listdir(d / "keep"), ["MANIFEST"])
                self.assertIn("out.stage-0", os.listdir(d))

    def check_backslash_in_output_name(self):
        """POSIX: a backslash is a name character, so the stage is named after all of out."""
        for d, drv in self.builds():
            with self.subTest(build=d.name):
                proj, out = self.project(d, self.MODS), d / "x\\out"
                self.assertEqual(drv.build(proj, out, *self.MODS), 0)
                self.assertEqual((out / "MANIFEST.ref").read_bytes()[:14], b"x\\out.stage-0 ")
                self.assertEqual(read_set(out), self.expected_set(self.MODS))

    def check_extended_length_output(self):
        """Windows: an output root with the extended-length prefix works for the output set too."""
        for d, drv in self.builds():
            with self.subTest(build=d.name):
                proj, out = self.project(d, self.MODS), d / "out"
                self.assertEqual(drv.build(proj, "\\\\?\\" + str(out), *self.MODS), 0, drv.stdout)
                self.assertEqual(read_set(out), self.expected_set(self.MODS))

    def test_build_diagnostic_in_module_2(self):
        for d, drv in self.builds():
            with self.subTest(build=d.name):
                proj, out, ref, names = self.old_set(d, drv)
                (proj / "bad.ci").write_bytes(b"!x\n")
                names = sorted(os.listdir(d))
                self.assertEqual(drv.build(proj, out, "main.ci", "bad.ci"), 1)
                self.assertEqual(drv.stdout, "build 1\ndiag 2001 1 1 1 0 0\n")
                self.assertUnchanged(d, out, ref, names)

    @staticmethod
    def snapshot(d: pathlib.Path) -> dict:
        """Every file under d outside proj/, by relative path, with its bytes."""
        return {str(f.relative_to(d)): f.read_bytes() for f in sorted(d.rglob("*"))
                if f.is_file() and f.relative_to(d).parts[0] != "proj"}

    def test_diagnostic_stops_later_phases(self):
        """Decision 24: a phase that returns a diagnostic (r != 0 with CINT_OK) ends the
        build. Plan (a 2-byte module), then compile in module 1 and in module 2 (`!`), both
        in the discovery pass that compiles every module before any is measured (CINTC-05,
        decision 27 item 3): the result is CINT_BUILD_DIAG with the row, the call log
        (-l, hook builds) shows no later phase ran, and the old set (MANIFEST.ref, MANIFEST, every file) survives
        byte for byte."""
        cases = (("plan", ("main.ci", "two.ci"), "diag 2002 1 1 1 0 0\n", "phases 1 0 0 0\n"),
                 ("compile in module 1", ("bad.ci", "main.ci"), "diag 2001 0 1 1 0 0\n", "phases 1 1 0 0\n"),
                 ("compile in module 2", ("main.ci", "bad.ci"), "diag 2001 1 1 1 0 0\n", "phases 1 2 0 0\n"))
        hooks = {name: h for name, _, h in BUILDS}
        for d, drv in self.builds():
            proj, out, ref, _ = self.old_set(d, drv)
            (proj / "two.ci").write_bytes(b"?\n")
            (proj / "bad.ci").write_bytes(b"!x\n")
            names, before = sorted(os.listdir(d)), self.snapshot(d)
            self.assertIn("MANIFEST", {pathlib.PurePath(k).name for k in before})
            for phase, rels, row, calls in cases:
                with self.subTest(build=d.name, phase=phase):
                    flags = ("-l",) if hooks[d.name] else ()
                    self.assertEqual(drv.build(proj, out, *rels, flags=flags), 1)
                    self.assertEqual(drv.stdout, "build 1\n" + row + (calls if flags else ""))
                    self.assertUnchanged(d, out, ref, names)
                    self.assertEqual(self.snapshot(d), before)

    def test_build_diagnostic_candidate_path_lifetime(self):
        """CINTC-12: copy a candidate path before table release; the old output set survives."""
        for d, drv in self.builds():
            with self.subTest(build=d.name):
                proj, out, ref, names = self.old_set(d, drv)
                path = b"/".join([b"a" * 50] * 6 + [b"aux.ci"])
                (proj / "main.ci").write_bytes(b"?0" + path)
                self.assertEqual(drv.build(proj, out, *self.MODS, flags=("-d", "1")), 1)
                self.assertEqual(drv.stdout, "build 1\ndiag 3030 1 1 1 2 0\npaths 1\npath 1 " + path.hex() + "\n")
                self.assertUnchanged(d, out, ref, names)
                self.assertEqual(drv.build(proj, out, *self.MODS), 1)
                self.assertEqual(drv.stdout, "build 1\ndiag 3030 1 1 1 2 0\n")
                self.assertUnchanged(d, out, ref, names)

    def test_build_diagnostic_candidate_path_failure(self):
        """A reporting allocation failure preserves the previously committed output set."""
        for d, drv in self.builds():
            with self.subTest(build=d.name):
                proj, out, ref, names = self.old_set(d, drv)
                (proj / "main.ci").write_bytes(b"?0lib/aux.ci")
                self.assertEqual(drv.build(proj, out, *self.MODS, flags=("-d", "2")), 4)
                self.assertEqual(drv.stdout, "build 4\npaths 1\n")
                self.assertUnchanged(d, out, ref, names)

    def test_build_diagnostic_candidate_path_bounds(self):
        """Malformed diagnostic ranges and row counts never reach the path sink."""
        for d, drv in self.builds():
            proj, out, ref, names = self.old_set(d, drv)
            for kind in range(1, 7):
                for flags in ((), ("-d", "1")):
                    with self.subTest(build=d.name, kind=kind, flags=flags):
                        (proj / "main.ci").write_bytes(("?%dlib/aux.ci" % kind).encode("ascii"))
                        self.assertEqual(drv.build(proj, out, *self.MODS, flags=flags), 3)
                        self.assertEqual(drv.stdout, "build 3\n" + ("paths 0\n" if flags else ""))
                        self.assertUnchanged(d, out, ref, names)

    def test_build_emit_failure_in_module_2(self):
        for d, drv in self.builds(hooks_only=True):
            with self.subTest(build=d.name):
                proj, out, ref, names = self.old_set(d, drv)
                for k in (3, 4, 5):   # module 2's two files, then the program-level file
                    self.assertEqual(drv.build(proj, out, *self.MODS, flags=("-w", str(k))), 4)
                    self.assertUnchanged(d, out, ref, names)

    def test_build_commit_failure(self):
        for d, drv in self.builds(hooks_only=True):
            with self.subTest(build=d.name):
                proj, out, ref, names = self.old_set(d, drv)
                self.assertEqual(drv.build(proj, out, *self.MODS, flags=("-f", "5")), 4)
                self.assertUnchanged(d, out, ref, names)

    def test_hard_link_of_input_as_manifest_ref(self):
        for d, drv in self.builds():
            with self.subTest(build=d.name):
                proj, out = self.project(d, self.MODS), d / "out"
                out.mkdir()
                os.link(proj / "main.ci", out / "MANIFEST.ref")
                self.assertEqual(drv.build(proj, out, *self.MODS), 4)
                self.assertEqual((proj / "main.ci").read_bytes(), self.MODS["main.ci"])
                self.assertEqual(sorted(os.listdir(d)), ["out", "proj"])

    def test_input_replaced_after_read(self):
        """(e) The identity comes from the handle that read the input: replacing the input
        after its read does not free the bytes that were read to be overwritten."""
        for d, drv in self.builds(hooks_only=True):
            with self.subTest(build=d.name):
                proj, out = self.project(d, self.MODS), d / "out"
                out.mkdir()
                os.link(proj / "main.ci", out / "MANIFEST.ref")

                def replace():
                    (proj / "main.new").write_bytes(b"export I64 h() {\n    return 3;\n}\n")
                    os.replace(proj / "main.new", proj / "main.ci")

                self.assertEqual(drv.build_paused(proj, out, *self.MODS, between=replace), 4)
                self.assertEqual((out / "MANIFEST.ref").read_bytes(), self.MODS["main.ci"])
                self.assertEqual(sorted(os.listdir(d)), ["out", "proj"])

    def test_kill_after_every_file(self):
        """(f) CINTC-06: killed after file k of n, for every k, a reader still sees the old set."""
        for d, drv in self.builds(hooks_only=True):
            with self.subTest(build=d.name):
                proj, out, ref, _ = self.old_set(d, drv)
                mods = dict(self.MODS, **{"main.ci": b"export I64 g() {\n    return 2;\n}\n"})
                (proj / "main.ci").write_bytes(mods["main.ci"])
                n = len(self.expected_set(mods))
                for k in range(1, n + 1):
                    with self.subTest(k=k):
                        self.assertEqual(drv.build(proj, out, *mods, flags=("-k", str(k))), 70)
                        self.assertEqual((out / "MANIFEST.ref").read_bytes(), ref)
                        self.assertEqual(read_set(out), self.expected_set(self.MODS))
                self.assertEqual(drv.build(proj, out, *mods), 0)
                self.assertEqual(read_set(out), self.expected_set(mods))

    def test_phase_budget(self):
        """(g) CINTC-09: a phase that exhausts K * bytes + C is C9001 naming the phase (2,
        compile) and the module, not an internal error."""
        for d, drv in self.builds():
            with self.subTest(build=d.name):
                proj, out, ref, names = self.old_set(d, drv)
                (proj / "spin.ci").write_bytes(b"~spin\n")
                names = sorted(os.listdir(d))
                self.assertEqual(drv.build(proj, out, "main.ci", "spin.ci"), 1)
                self.assertEqual(drv.stdout, "build 1\ndiag 9001 1 0 0 2 %d\n" % (K * 6 + C))
                self.assertUnchanged(d, out, ref, names)

    # Windows only: a reparse point at the path, and a reader holding the target.
    @unittest.skipUnless(os.name == "nt", "Windows junctions")
    def test_junction_output(self):
        import _winapi
        for d, drv in self.builds():
            with self.subTest(build=d.name):
                (d / "target").mkdir()
                _winapi.CreateJunction(str(d / "target"), str(d / "out.c"))
                self.assertEqual(drv.commit(d / "out.c", b"x"), 1)
                self.assertEqual(os.listdir(d / "target"), [])
                self.assertOnly(d, ["out.c", "target"])

    @unittest.skipUnless(os.name == "nt", "Windows sharing modes")
    def test_target_held_open_by_a_reader(self):
        for d, drv in self.builds():
            with self.subTest(build=d.name):
                out = d / "out.c"
                out.write_bytes(b"old\n")
                with open(out, "rb") as reader:  # CRT share mode: read and write, not delete
                    self.assertEqual(drv.commit(out, b"new\n"), 1)
                    self.assertEqual(reader.read(), b"old\n")
                self.assertEqual(out.read_bytes(), b"old\n")
                self.assertOnly(d, ["out.c"])


# Defined per host, so neither appears as a skip on the other.
if os.name == "nt":
    CommitCases.test_extended_length_output = CommitCases.check_extended_length_output
else:
    CommitCases.test_backslash_in_output_name = CommitCases.check_backslash_in_output_name


class CommitInBuildTree(CommitCases, unittest.TestCase):
    def root(self):
        return STATE["out"] / "fs"


@unittest.skipIf(os.name == "nt", "the native-file-system pass is for the Linux and macOS legs")
class CommitOnNativeFileSystem(CommitCases, unittest.TestCase):
    base: str | None = None

    @classmethod
    def setUpClass(cls):
        cls.base = tempfile.mkdtemp(prefix="cint-bridge-")

    @classmethod
    def tearDownClass(cls):
        CommitCases.rmtree(pathlib.Path(cls.base))

    def root(self):
        return pathlib.Path(self.base)


class RoundTrip(unittest.TestCase):
    """(a) to (c): a struct table through a seed-built cx wrapper, every descriptor refusal,
    E_SHAPE and E_ALIAS (rt/tests/roundtrip/host.c)."""

    def test_round_trip_refusals_shape_alias(self):
        for name, exe in ROUNDTRIPS:
            with self.subTest(build=name):
                env = dict(STATE["env"])
                env["ASAN_OPTIONS"] = "detect_leaks=1"
                r = subprocess.run([str(exe)], env=env, capture_output=True, text=True)
                self.assertNotIn("runtime error", r.stderr)
                self.assertNotIn("Sanitizer", r.stderr)
                failed = [l for l in r.stdout.splitlines()[:-1] if not l.endswith(": ok")]
                self.assertEqual((r.returncode, failed), (0, []), r.stdout)
                self.assertEqual(r.stdout.splitlines()[-1], "roundtrip: 13 of 13 checks passed")


class BridgeSource(unittest.TestCase):
    def test_calls_refuse_bad_arguments(self):
        for name, exe, _ in BUILDS:
            with self.subTest(build=name):
                self.assertEqual(Driver(self, exe).run("calls"), 0)

    def test_no_binary_floating_types(self):
        words = ["flo" + "at", "dou" + "ble", "long dou" + "ble"]
        pattern_ = re.compile(r"\b(" + "|".join(words) + r")\b")
        for p in (RT / "cint_bridge.c", RT / "cint_build.c", RT / "cint_bridge.h",
                  RT / "cint_bridge_internal.h", HERE / "test_bridge_main.c",
                  HERE / "roundtrip" / "host.c"):
            for n, line in enumerate(p.read_text(encoding="utf-8").splitlines(), 1):
                self.assertIsNone(pattern_.search(line), f"{p.name}:{n}")

    def test_size_ceiling_and_line_endings(self):
        """SPEC-09 4.2: cint_bridge.c with cint_bridge_internal.h, and cint_build.c, at most
        600 lines each (the ceilings; the targets are 400)."""
        for p in (RT / "cint_bridge.c", RT / "cint_build.c", RT / "cint_bridge.h", RT / "cint_bridge_internal.h"):
            self.assertNotIn(b"\r", p.read_bytes(), p.name)
        rows = {"cint_bridge.c + cint_bridge_internal.h": (RT / "cint_bridge.c", RT / "cint_bridge_internal.h"),
                "cint_build.c": (RT / "cint_build.c",)}   # decision 24
        for name, files in rows.items():
            self.assertLessEqual(sum(p.read_bytes().count(b"\n") for p in files), 600, name)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--leg", choices=["msvc", "gcc", "clang", "apple-clang"])
    parser.add_argument("--out")
    known, rest = parser.parse_known_args()
    ARGS.leg, ARGS.out = known.leg, known.out
    unittest.main(argv=[sys.argv[0], "-v", *rest])


if __name__ == "__main__":
    main()
