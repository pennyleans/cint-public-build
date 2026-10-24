"""Builds B0, seed-generated S1 and B1 at O0 with portable helpers, and cint from the same S1 at O2
with the builtin helpers.

    python tools/cint_bootstrap.py --leg msvc|gcc|clang [--out DIR]

Builds stay under <build>/boot/<leg>, where <build> is the CINT_BUILD environment
variable or cint-build in the system temporary directory (tools/cint_check.py
build_root); a leg relaunched in WSL Ubuntu-24.04 receives CINT_BUILD as a WSL
path. Each invocation retains its input snapshot and build logs.
The compiler's CISRC001 manifest covers main.ci's actual import closure.
B1's digest export and hashlib independently verify retained source bytes.
S1 and native executable digests are observations (SPEC-09 RCPT-04, FIX-05).
The receipt covers this bootstrap only, through B1.

Two toolchain files go beside cint. cint.toolchain, which cint run, cint build, cint build
--lib and the Python loader read by default, is the release toolchain (decision 2026-10-08 on
OQ-236): user programs and their runtime objects at -O2 (/O2) with the builtin helpers.
cint-bootstrap.toolchain is the configuration B1 was built with, -O0 (/Od) with the portable
helpers, which `--toolchain` selects.

The repository's lib/ modules (the standard service modules, SPEC-07 SEC-CAP-13) go to lib/
beside cint, its default standard library root (SPEC-09 RCPT-07; `--stdlib DIR` names another).

cint itself is built with the release toolchain's flags (decision 2026-10-19 on OQ-298): S1,
the runtime, the bridge, rt/cint_build.c and the CLI at -O2 (/O2) with the builtin helpers, so
the compiler a program is built with runs at the speed of the programs it builds. The C it
emits is the same: the fixpoint receipts give one identity for B1 at -O0 and at -O2 with either
helper mode on every leg (FIX-04). The release compile of S1 runs beside B1's.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import copy
import hashlib
import os
import pathlib
import platform
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import cint_check as cc  # noqa: E402
from cint_ref.lexer import decode_source, tokenize  # noqa: E402


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def source_manifest(sources: dict[str, bytes]) -> bytes:
    """SEC-REC-3 framing, retaining exact source bytes through their digests."""
    result = bytearray(b"CISRC001" + struct.pack("<I", len(sources)))
    for path in sorted(sources, key=lambda p: p.encode("utf-8")):
        encoded = path.encode("utf-8")
        if not encoded or len(encoded) > 65535 or not re.fullmatch(r"[A-Za-z0-9_.\-/]+", path):
            raise ValueError("invalid source-manifest path: %r" % path)
        if path.startswith("/") or any(p in ("", ".", "..") for p in path.split("/")):
            raise ValueError("invalid source-manifest path: %r" % path)
        data = sources[path]
        result += struct.pack("<H", len(encoded)) + encoded
        result += struct.pack("<Q", len(data)) + hashlib.sha256(data).digest()
    return bytes(result)


def snapshot_inputs(root: pathlib.Path, dest: pathlib.Path) -> dict:
    """Reads each build input once and builds against its retained copy."""
    groups = {}
    for group, directory, suffixes in (("compiler", "compiler", (".ci",)),
                                       ("seed", "seed", (".c", ".h")),
                                       ("runtime", "rt", (".c", ".h")),
                                       ("cli", "cli", (".c", ".h")),
                                       ("lib", "lib", (".ci",))):
        files = {}
        source_dir = root / directory
        if source_dir.is_symlink():
            raise ValueError("linked input directory: %s" % source_dir)
        for src in sorted(source_dir.iterdir(), key=lambda p: p.name.encode("utf-8")):
            if src.suffix not in suffixes:
                continue
            if src.is_symlink() or not src.is_file():
                raise ValueError("not a regular build input: %s" % src)
            files[src.relative_to(root).as_posix()] = src.read_bytes()
        groups[group] = files
    host = root / "compiler" / "tests" / "golden_host.c"
    if host.is_symlink() or host.parent.is_symlink():
        raise ValueError("linked B1 host input: %s" % host)
    groups["host"] = {"compiler/tests/golden_host.c": host.read_bytes()}
    for files in groups.values():
        for rel, data in files.items():
            dst = dest / rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_bytes(data)
    return groups


_CLOSURE_PATHS: dict[bytes, tuple[str, ...]] = {}


def compiler_closure(sources: dict[str, bytes]) -> dict[str, bytes]:
    """Follows imports using the source lexer so comments cannot name inputs.

    The result depends only on the given paths and bytes, so the closure's paths are kept per
    process, keyed by a digest of every path and its bytes; any changed byte rescans.
    """
    key = hashlib.sha256()
    for path in sorted(sources, key=lambda p: p.encode("utf-8")):
        encoded = path.encode("utf-8")
        key.update(struct.pack("<Q", len(encoded)) + encoded + hashlib.sha256(sources[path]).digest())
    key = key.digest()
    if key not in _CLOSURE_PATHS:
        _CLOSURE_PATHS[key] = tuple(_scan_closure(sources))
    return {path: sources[path] for path in _CLOSURE_PATHS[key]}


def _scan_closure(sources: dict[str, bytes]) -> dict[str, bytes]:
    closure, pending = {}, ["compiler/main.ci"]
    while pending:
        path = pending.pop()
        if path in closure:
            continue
        if path not in sources:
            raise ValueError("compiler import absent from snapshot: %s" % path)
        closure[path] = sources[path]
        tokens = tokenize(decode_source(sources[path], path), path)
        for i, token in enumerate(tokens):
            if token.text != "import" or token.kind != "keyword":
                continue
            names, j = [], i + 1
            while tokens[j].kind == "ident":
                names.append(tokens[j].text)
                j += 1
                if tokens[j].text != ".":
                    break
                j += 1
            if not names or tokens[j].text != ";":
                raise ValueError("invalid compiler import in %s" % path)
            pending.append("compiler/" + "/".join(names) + ".ci")
    return {p: closure[p] for p in sorted(closure, key=lambda p: p.encode("utf-8"))}


def verify_digest_output(output: bytes, paths: list[pathlib.Path]) -> dict[str, str]:
    lines = output.decode("ascii").splitlines()
    if len(lines) != len(paths):
        raise ValueError("B1 digest result count differs from its input count")
    checked = {}
    for path, line in zip(paths, lines):
        expected = sha256(path.read_bytes())
        if line != "digest " + expected:
            raise ValueError("B1 digest disagrees with hashlib for %s: %s" % (path, line))
        checked[str(path)] = expected
    return checked


def toolchain_bytes(values: dict, flags: list, runtime_sources: list, environment: dict) -> bytes:
    lines = ["cint-toolchain-1"]
    pairs = sorted(values.items()) + [("flag", f) for f in flags]
    pairs += [("runtime_source", p) for p in runtime_sources]
    pairs += [("env", k + "=" + v) for k, v in sorted(environment.items())]
    for key, value in pairs:
        value = str(value)
        if any(c in key + value for c in "\r\n\0") or not key or " " in key:
            raise ValueError("invalid toolchain field: %r" % key)
        lines.append(key + " " + value)
    return ("\n".join(lines) + "\n").encode("utf-8")


def executable_toolchain_bytes(executable: pathlib.Path, values: dict, flags: list,
                               runtime_sources: list, environment: dict) -> bytes:
    """Binds each configuration to the exact executable published with it."""
    return toolchain_bytes(dict(values, executable_sha256=sha256(executable.read_bytes())),
                           flags, runtime_sources, environment)


def release_flags(flags: list[str]) -> list[str]:
    """The release toolchain's flags: the bootstrap's at -O2 (/O2) with the builtin helpers, as
    the -O2 builtin agreement configuration builds programs (OQ-236)."""
    raise_to = {"-O0": "-O2", "/Od": "/O2", "-DCINT_RT_HELPERS_PORTABLE": "-DCINT_RT_HELPERS_BUILTIN",
                "/DCINT_RT_HELPERS_PORTABLE": "/DCINT_RT_HELPERS_BUILTIN"}
    raised = [raise_to.get(f, f) for f in flags]
    if sum(a != b for a, b in zip(flags, raised)) != 2:
        raise ValueError("the bootstrap flags need one optimization flag and one helper flag to raise")
    return raised


def validate_output_root(root: pathlib.Path, out: pathlib.Path) -> None:
    root, out = root.resolve(), out.resolve()
    if out == root or out in root.parents or root in out.parents:
        raise ValueError("bootstrap output must not overlap the checkout: %s" % out)


def cli_compile_flags(flags: list[str], leg: str, compiler_identity: str) -> list[str]:
    """Binds the CLI to its compiler without adding a program compiler option."""
    if not re.fullmatch("[0-9a-f]{64}", compiler_identity):
        raise ValueError("invalid compiler source identity")
    prefix = "/D" if leg == "msvc" else "-D"
    return flags + [prefix + 'CINT_COMPILER_SOURCE_IDENTITY="' + compiler_identity + '"']


class LoggedToolchain(cc.Toolchain):
    """Keeps compiler invocations and their streams beside each build."""
    def __init__(self, leg: str, logs: pathlib.Path):
        super().__init__(leg, "0", "portable", False)
        self.logs, self.commands, self.lock = logs, [], threading.Lock()
        logs.mkdir()

    def with_flags(self, flags: list[str]) -> "LoggedToolchain":
        """A toolchain of these flags that logs into this one's commands, for a compile that runs
        beside this one's."""
        other = copy.copy(self)
        other.flags = flags
        return other

    def run(self, cmd, cwd, what):
        cmd = [str(c) for c in cmd]
        with self.lock:
            index = len(self.commands)
            self.commands.append(None)
        r = subprocess.run(cmd, cwd=cwd, env=self.env, capture_output=True)
        log = self.logs / ("%03d.log" % index)
        log.write_bytes(r.stdout + r.stderr)
        self.commands[index] = {"argv": cmd, "exit_status": r.returncode,
                                "log": log.name, "purpose": what}
        noise = r.stderr.strip() if self.leg != "msvc" else re.search(rb"\bwarning\s*(?:[A-Z]+\d+\s*)?:", r.stdout + r.stderr)
        if r.returncode != 0 or noise:
            raise cc.BuildError("%s: exit %d; log %s\n%s" %
                                (what, r.returncode, log, (r.stdout + r.stderr)[-4000:].decode("utf-8", "replace")))
        return r


def bootstrap(leg: str, out: pathlib.Path) -> pathlib.Path:
    validate_output_root(ROOT, out)
    out.mkdir(parents=True, exist_ok=True)
    work = pathlib.Path(tempfile.mkdtemp(prefix="build-", dir=out)).resolve()
    inputs = work / "inputs"
    started = time.monotonic_ns()
    groups = snapshot_inputs(ROOT, inputs)
    groups["compiler"] = compiler_closure(groups["compiler"])
    manifests = {}
    for group, files in groups.items():
        path = work / (group + ".cisrc")
        path.write_bytes(source_manifest(files))
        manifests[group] = path
    tc = LoggedToolchain(leg, work / "logs")
    # Toolchain.compile uses this include root; every header is a retained input.
    cc.RT = inputs / "rt"
    rt_dir, rt_lib_dir, rel_dir, rel_lib_dir, b0_dir, s1_dir, s1_rel_dir, b1_dir, cli_dir = [
        work / p for p in ("rt", "rt-lib", "rt-release", "rt-release-lib", "b0", "s1", "s1-release", "b1", "cli")]
    for directory in (rt_dir, rt_lib_dir, rel_dir, rel_lib_dir, b0_dir, s1_dir, s1_rel_dir, b1_dir, cli_dir):
        directory.mkdir()
    times = {}
    t0 = time.monotonic_ns()
    rt_sources = [inputs / p for p in groups["runtime"]]
    rt_obj, bridge_obj, build_obj = tc.compile(
        [inputs / "rt" / n for n in ("cint_rt.c", "cint_bridge.c", "cint_build.c")], rt_dir, "runtime")
    # The runtime object of `cint build --lib`, which exports the host functions and no other
    # runtime symbol (SPEC-09 EMIT-31; box 10 default BX10-07).
    program_flags = tc.flags
    library = [("/D" if leg == "msvc" else "-D") + "CINT_RT_LIBRARY"]
    rel_flags = release_flags(program_flags)
    try:
        tc.flags = program_flags + library
        rt_lib_obj = tc.compile([inputs / "rt" / "cint_rt.c"], rt_lib_dir, "library runtime")[0]
        # The release toolchain's runtime objects, for programs and for libraries (OQ-236).
        tc.flags = rel_flags
        rel_obj = tc.compile([inputs / "rt" / "cint_rt.c"], rel_dir, "release runtime")[0]
        tc.flags = rel_flags + library
        rel_lib_obj = tc.compile([inputs / "rt" / "cint_rt.c"], rel_lib_dir, "release library runtime")[0]
    finally:
        tc.flags = program_flags
    seed_sources = [inputs / p for p in groups["seed"] if p.endswith(".c")]
    seed_objs = tc.compile(seed_sources, b0_dir, "B0")
    b0 = tc.link_exe(seed_objs + [rt_obj, bridge_obj], b0_dir / "cint-seed", "B0")
    times["b0"] = (time.monotonic_ns() - t0) // 1000000
    print("b0: built", flush=True)
    t0 = time.monotonic_ns()
    s1 = s1_dir / "main.c"
    result = tc.run([b0, "main.ci", "-o", s1], inputs / "compiler", "S1: seed translates main.ci")
    if result.stdout or not s1.is_file() or not s1.stat().st_size:
        raise cc.BuildError("S1: seed did not produce its generated source")
    s1_manifest = work / "s1.cisrc"
    s1_manifest.write_bytes(source_manifest({"main.c": s1.read_bytes()}))
    times["s1"] = (time.monotonic_ns() - t0) // 1000000
    print("s1: emitted", flush=True)
    t0 = time.monotonic_ns()
    rel_tc = tc.with_flags(rel_flags)
    with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
        # cint's compiler at the release flags (OQ-298), compiled while B1's is.
        s1_rel = pool.submit(rel_tc.compile, [s1], s1_rel_dir, "cint compiler")
        s1_obj = tc.compile([s1], s1_dir, "B1 compiler")[0]
        host_obj = tc.compile([inputs / "compiler/tests/golden_host.c"], b1_dir, "B1 host")[0]
        compiler_objs = [s1_obj, rt_obj, bridge_obj, build_obj]
        b1 = tc.link_exe([host_obj] + compiler_objs, b1_dir / "cintc", "B1")
        times["b1"] = (time.monotonic_ns() - t0) // 1000000
        print("b1: built", flush=True)
        t0 = time.monotonic_ns()
        rel_bridge_obj, rel_build_obj = rel_tc.compile(
            [inputs / "rt" / n for n in ("cint_bridge.c", "cint_build.c")], rel_dir, "cint runtime")
        proc = "cint_proc_win.c" if leg == "msvc" else "cint_proc_posix.c"
        cli_tc = tc.with_flags(cli_compile_flags(rel_flags, leg, sha256(manifests["compiler"].read_bytes())))
        cli_objs = cli_tc.compile([inputs / "cli/cint_main.c", inputs / "cli/cint_receipt.c", inputs / "cli/cint_discover.c", inputs / "cli/cint_cache.c",
                                   inputs / "cli" / proc],
                                  cli_dir, "cint CLI")
        s1_rel_obj = s1_rel.result()[0]
    cint = tc.link_exe(cli_objs + [s1_rel_obj, rel_obj, rel_bridge_obj, rel_build_obj], cli_dir / "cint", "cint CLI")
    times["cli"] = (time.monotonic_ns() - t0) // 1000000
    paths = list(manifests.values()) + [s1_manifest, s1, cint] + rt_sources
    digest_result = tc.run([b1, "--digest", *paths], work, "B1 digest verification")
    checked = verify_digest_output(digest_result.stdout, paths)
    cache = out / "cache"
    cache.mkdir(exist_ok=True)
    values = {"leg": leg, "cc": shutil.which(tc.cc, path=tc.env.get("PATH")) or tc.cc,
              "cc_version": tc.version, "include": inputs / "rt", "runtime_object": rt_obj,
              "runtime_library_object": rt_lib_obj,
              "cache": cache.resolve(), "host": cc.host_os(), "compiler_sources": manifests["compiler"]}
    environment = {k: v for k, v in tc.env.items() if k.upper() in ("PATH", "INCLUDE", "LIB", "LIBPATH")}
    bootstrap_config = executable_toolchain_bytes(cint, values, tc.flags, rt_sources, environment)
    config = executable_toolchain_bytes(cint, dict(values, runtime_object=rel_obj, runtime_library_object=rel_lib_obj),
                                        rel_flags, rt_sources, environment)
    (work / "cint-bootstrap.toolchain").write_bytes(bootstrap_config)
    (work / "cint.toolchain").write_bytes(config)
    source_files = lambda group: [{"path": p, "bytes": len(data), "sha256": sha256(data)}
                                  for p, data in groups[group].items()]
    identity = {"compiler_source_identity": checked[str(manifests["compiler"])],
                "compiler_sources": source_files("compiler"), "profile": "cint-core-1",
                "runtime_contract_version": "cint-rt-3",
                "runtime_sources": source_files("runtime"), "seed_source_identity": sha256(manifests["seed"].read_bytes())}
    outcome = {"source_digest_verification": "pass"}
    observed = {"c0": tc.describe(), "commands": tc.commands,
                "host": {"arch": platform.machine(), "os": cc.host_os()},
                "build_inputs": {g: source_files(g) for g in ("cli", "host", "lib", "seed")},
                "b0_sha256": sha256(b0.read_bytes()), "b1_sha256": sha256(b1.read_bytes()),
                "cint_sha256": sha256(cint.read_bytes()), "s1_tree_sha256": sha256(s1_manifest.read_bytes()),
                "s1_files": [{"path": "main.c", "bytes": s1.stat().st_size, "sha256": sha256(s1.read_bytes())}],
                "objects": [{"path": p.relative_to(work).as_posix(), "sha256": sha256(p.read_bytes())}
                            for p in sorted(work.rglob("*" + tc.obj), key=lambda p: p.as_posix().encode("utf-8"))],
                "verified_digests": [{"path": pathlib.Path(p).relative_to(work).as_posix(), "sha256": digest}
                                     for p, digest in sorted(checked.items())],
                "wall_ms": times, "total_ms": (time.monotonic_ns() - started) // 1000000}
    receipt = {"schema": "cint-bootstrap-receipt-1", "identity": identity, "outcome": outcome,
               "observations": observed, "identity_sha256": sha256(cc.canonical({"identity": identity, "outcome": outcome}))}
    receipt_path = work / "bootstrap.json"
    receipt_path.write_bytes(cc.canonical(receipt) + b"\n")
    # Publish only after every native build and independent digest check passed.
    executable = out / ("cint" + tc.exe)
    pending = out / ("cint" + tc.exe + ".new-" + work.name)
    shutil.copy2(cint, pending)
    pending.replace(executable)
    for name, data in (("cint-bootstrap.toolchain", bootstrap_config), ("cint.toolchain", config)):
        config_pending = out / (name + ".new-" + work.name)
        config_pending.write_bytes(data)
        config_pending.replace(out / name)
    receipt_pending = out / ("bootstrap.json.new-" + work.name)
    receipt_pending.write_bytes(receipt_path.read_bytes())
    receipt_pending.replace(out / "bootstrap.json")
    # The standard library root beside cint (SPEC-07 SEC-CAP-13, ruling OQ-303): the snapshot of
    # the repository's lib/ modules, which cint reads for an import of one segment that the source
    # root does not answer (SPEC-09 RCPT-07). A new directory replaces the old one whole.
    lib_pending, lib_old = out / ("lib.new-" + work.name), out / ("lib.old-" + work.name)
    lib_pending.mkdir()
    for rel, data in groups["lib"].items():
        (lib_pending / pathlib.PurePosixPath(rel).name).write_bytes(data)
    if (out / "lib").exists():
        (out / "lib").replace(lib_old)
    lib_pending.replace(out / "lib")
    shutil.rmtree(lib_old, ignore_errors=True)
    print("cint: %s" % executable, flush=True)
    print("receipt: %s" % receipt_path, flush=True)
    return receipt_path


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--leg", required=True, choices=("msvc", "gcc", "clang", "apple-clang"))
    parser.add_argument("--out", type=pathlib.Path)
    args = parser.parse_args(argv)
    if os.name == "nt" and args.leg != "msvc":
        command = ["wsl", "-d", cc.WSL_DISTRO, "--", "python3", cc.wsl_path(pathlib.Path(__file__).resolve()),
                   "--leg", args.leg]
        if args.out is not None:
            command += ["--out", cc.wsl_path(args.out.resolve())]
        return subprocess.run(command, cwd=ROOT, env=cc.wsl_env()).returncode
    if args.leg == "msvc" and os.name != "nt":
        parser.error("the msvc leg requires Windows")
    out = args.out or cc.build_root() / "boot" / args.leg
    try:
        bootstrap(args.leg, out)
    except (OSError, ValueError, cc.BuildError, subprocess.SubprocessError) as error:
        print("bootstrap: %s" % error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
