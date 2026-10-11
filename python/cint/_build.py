"""Building a library from source for `cint.load` (SPEC-03 6.1; box 10 ruling BX10-02).

`cint.load("prog.ci")` runs `cint build --lib`, which writes one shared
library per program (BX10-07), into a build directory keyed by the digests
of the program's modules and the identity of the toolchain, so that loading
unchanged source again builds nothing. The modules are the ones the build
read: its receipt lists each with its SHA-256, and each import lookup that
found nothing (SPEC-09 RCPT-07). A cached library is used only when every
listed module still has its digest and every absent lookup is still absent.

The `cint` executable is the one `CINT_EXE` names, else `cint` on PATH, else
the one `tools/cint_bootstrap.py` writes under `CINT_BUILD` (by default
`cint-build` in the temporary directory). Each has its toolchain file,
`cint.toolchain`, beside it, which names the C compiler, the runtime, and the
build cache. The library directories go under that cache, in `py-lib`.

The bridge links its own runtime object into each library (box 13 unit 9):
`cint_rt.c`, `cint_state.c`, and `cint_rec.c` from the toolchain's runtime
sources, compiled as one translation unit with `CINT_RT_LIBRARY`, so the
library also exports the checkpoint, recording, and replay functions of
`rt/cint_state.h`, which the toolchain's own library runtime lacks. One
unit rather than two objects, because MSVC has no partial link and a static
library's member that the program never names is not linked. The object and
a toolchain file that names it are built once per toolchain, under the cache
in `py-rt`, and `cint build --lib --toolchain` reads that file. Each build
also keeps the program's SIR text and its sources in `sir/` beside the
library, copied from the build's staged outputs and checked against the
digests its receipt lists, so that a recording can carry the code it ran and
`cint.replay` can run it on `cpu-sir-interp` (`interpreter_library`).

A `backend="cuda"` build (box 11, BX11-16) compiles the program for
`gpu-cuda`: its C with `CINT_GPU_CUDA`, linked with a runtime object that
joins that unit (with `CINT_GPU_CUDA`), `cint_cuda.c`, and
`cint_cuda_dispatch.c`, as `tools/cint_check.py --backend gpu-cuda` builds
programs, under the cache in `py-cuda`. When `CINT_CUDA_DRIVER` names a
stand-in driver, the CUDA sources are compiled with `CINT_CUDA_TESTING`, so
the runtime opens it (`_cuda`).
"""
from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile

from ._errors import BuildError

TOOLCHAIN_HEADER = b"cint-toolchain-1\n"
CACHE_DOMAIN = b"cint-python/library-build/v1"
RUNTIME_DOMAINS = {"cpu": b"cint-python/runtime/v1", "cuda": b"cint-python/cuda-runtime/v3"}
INTERP_DOMAIN = b"cint-python/interpreter/v1"
# The runtime, the canonical state code, and the recordings as one translation unit. cint_state.c
# defines the same macro for its fopen; an identical definition is no redefinition.
RUNTIME_UNIT = ("cint_rt_state.c", b'#define _CRT_SECURE_NO_WARNINGS 1\n#include "cint_rt.c"\n'
                b'#include "cint_state.c"\n#include "cint_rec.c"\n')
# The sources of each backend's runtime object, with their defines.
RUNTIMES = {"cpu": ((RUNTIME_UNIT[0], ("CINT_RT_LIBRARY",)),),
            "cuda": ((RUNTIME_UNIT[0], ("CINT_RT_LIBRARY", "CINT_GPU_CUDA")), ("cint_cuda.c", ("CINT_RT_LIBRARY",)),
                     ("cint_cuda_dispatch.c", ("CINT_RT_LIBRARY",)))}
# What Python calls in the interpreter's library: a Windows DLL exports only what is named.
INTERP_EXPORTS = (
    "cint_abi_version", "cint_ctx_create", "cint_ctx_destroy", "cint_fault_get", "cint_ctx_clear_fault",
    "cint_ctx_refusal", "cint_fuel_consumed", "cint_buffer_release", "cint_buffer_register",
    "cint_buffer_register_bytes", "cint_ctx_bind_state", "cint_ctx_checkpoint", "cint_ctx_restore",
    "cint_ctx_state_hash", "cint_checkpoint_decode", "cint_checkpoint_global_next", "cint_checkpoint_buffer_next",
    "cint_checkpoint_render", "cint_recording_decode", "cint_recording_next", "cint_recording_module",
    "cint_recording_render", "cint_replay", "cint_replay_what", "cint_sir_load_text", "cint_sir_free",
    "cint_sir_prepare", "cint_sir_release", "cint_sir_revision", "cint_sir_cint_program", "cint_sir_observer",
    "cint_sir_state_table", "cint_sir_main")
INTERP_SOURCES = ("sir_read.c", "sir_exec.c")
SIR_DIR = "sir"
_HEX = frozenset("0123456789abcdef")

builds = 0               # libraries built so far; tests read it to show that a cached library was used


def _exe_name() -> str:
    return "cint.exe" if os.name == "nt" else "cint"


def _legs() -> tuple:
    """The bootstrap legs of this host, in the order they are tried."""
    if os.name == "nt":
        return ("msvc",)
    if sys.platform == "darwin":
        return ("apple-clang", "clang")
    return ("gcc", "clang")


def library_name(stem: str) -> str:
    """The file `cint build --lib` writes for a root module (cli/cint_proc.h)."""
    if os.name == "nt":
        return stem + ".dll"
    return "lib" + stem + (".dylib" if sys.platform == "darwin" else ".so")


def find_cint() -> str:
    """The `cint` executable to build with, which has `cint.toolchain` beside it."""
    given = os.environ.get("CINT_EXE")
    if given:
        if not os.path.isfile(given) or not os.path.isfile(os.path.join(os.path.dirname(given), "cint.toolchain")):
            raise BuildError("CINT_EXE names %s, which is not a cint executable with cint.toolchain beside it, so "
                             "cint build --lib cannot run" % given)
        return os.path.abspath(given)
    candidates = []
    found = shutil.which(_exe_name())
    if found:
        candidates.append(found)
    base = os.environ.get("CINT_BUILD") or os.path.join(tempfile.gettempdir(), "cint-build")
    candidates += [os.path.join(base, "boot", leg, _exe_name()) for leg in _legs()]
    for path in candidates:
        if os.path.isfile(path) and os.path.isfile(os.path.join(os.path.dirname(path), "cint.toolchain")):
            return os.path.abspath(path)
    raise BuildError("no cint executable to run cint build --lib: set CINT_EXE, put cint on PATH, or run "
                     "tools/cint_bootstrap.py, which needs a C compiler (looked for %s)" % ", ".join(candidates))


def _sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


class Toolchain:
    """The toolchain file beside a `cint` executable, and its identity: the
    file itself with the digests of the executable, the runtime sources, the
    runtime library object, and the compiler sources it names."""

    def __init__(self, exe: str):
        self.exe = exe
        path = os.path.join(os.path.dirname(exe), "cint.toolchain")
        with open(path, "rb") as f:
            data = f.read()
        if not data.startswith(TOOLCHAIN_HEADER):
            raise BuildError("%s is not a toolchain file (cint-toolchain-1), so cint build --lib cannot run" % path)
        self.data = data
        self.rows = [line.split(" ", 1) for line in data.decode("utf-8").splitlines()[1:] if " " in line]
        values = dict(self.rows)
        self.values = values
        self.cache = values.get("cache")
        if not self.cache:
            raise BuildError("%s names no cache directory" % path)
        named = [values.get("runtime_library_object"), values.get("compiler_sources")]
        named += [v for k, v in self.rows if k == "runtime_source"]
        h = hashlib.sha256(CACHE_DOMAIN)
        h.update(len(data).to_bytes(8, "little") + data)
        h.update(_sha256_file(exe).encode("ascii"))
        for item in named:
            h.update(b"\n" + (_sha256_file(item) if item and os.path.isfile(item) else "absent").encode("ascii"))
        self.identity = h.hexdigest()


def _receipt_modules(receipt: dict):
    """The modules a build read, with their digests, and its absent lookups."""
    ident = receipt["identity"]
    files = ident["source"]["files"]
    absent = sorted(path for path, found in ident.get("lookups", {}).items() if not found)
    return files, absent


def _key(probe: str, files: dict, absent: list) -> str:
    h = hashlib.sha256(probe.encode("ascii"))
    for path in sorted(files, key=lambda p: p.encode("utf-8")):
        h.update(b"\nmodule " + path.encode("utf-8") + b" " + files[path].encode("ascii"))
    for path in absent:
        h.update(b"\nabsent " + path.encode("utf-8"))
    return h.hexdigest()


def _unchanged(root: str, files: dict, absent: list, digests: dict) -> bool:
    for path, want in files.items():
        full = os.path.join(root, *path.split("/"))
        if full not in digests:
            digests[full] = _sha256_file(full) if os.path.isfile(full) else None
        if digests[full] != want:
            return False
    return not any(os.path.exists(os.path.join(root, *path.split("/"))) for path in absent)


def _cached(probe_dir: str, root: str, stem: str):
    """The library of an earlier build of this program whose modules are
    unchanged, or None."""
    digests = {}
    try:
        names = os.listdir(probe_dir)
    except FileNotFoundError:
        return None
    for name in sorted(names):
        if len(name) != 64 or not set(name) <= _HEX:
            continue
        lib = os.path.join(probe_dir, name, library_name(stem))
        try:
            with open(os.path.join(probe_dir, name, "build.json"), "rb") as f:
                files, absent = _receipt_modules(json.loads(f.read()))
        except (OSError, ValueError, KeyError, TypeError, AttributeError):
            continue
        if os.path.isfile(lib) and _unchanged(root, files, absent, digests):
            return lib
    return None


def _diagnostics(stdout: str) -> tuple:
    out = []
    for line in stdout.splitlines():
        try:
            row = json.loads(line)
        except ValueError:
            continue
        if isinstance(row, dict) and "code" in row:
            out.append(row)
    return tuple(out)


def _run(command: list, cwd: str, env: dict, what: str):
    try:
        done = subprocess.run(command, cwd=cwd, env=env, capture_output=True, text=True, encoding="utf-8",
                              errors="replace")
    except OSError as failure:
        raise BuildError("%s could not run %s: %s" % (what, command[0], failure)) from None
    if done.returncode != 0:
        raise BuildError("%s failed with exit status %d: %s" % (what, done.returncode,
                                                                (done.stderr + done.stdout).strip()[-4000:]),
                         status=done.returncode, output=done.stderr + done.stdout)


def _compile_env(tool: Toolchain) -> tuple:
    """The environment and flags of the toolchain's C compiler, and whether it is MSVC."""
    v = tool.values
    if not v.get("cc") or not v.get("include"):
        raise BuildError("the toolchain beside %s names no C compiler or runtime sources" % tool.exe)
    env = dict(os.environ)
    env.update(row[1].split("=", 1) for row in tool.rows if row[0] == "env" and "=" in row[1])
    flags = [value for name, value in tool.rows if name == "flag"]
    return env, flags, v.get("leg") == "msvc"


def _publish(work: str, final: str, ready: str):
    """Move a finished build directory into place; another process may have built it first."""
    try:
        os.rename(work, final)
    except OSError:
        if not os.path.isfile(ready):
            raise
        shutil.rmtree(work, ignore_errors=True)


def _runtime_toolchain(tool: Toolchain, backend: str, testing: bool = False) -> str:
    """The toolchain file of a library build through the bridge: the
    toolchain's own, with the runtime library object replaced by the bridge's
    (RUNTIMES), and for gpu-cuda the flag `CINT_GPU_CUDA` added. Both are
    built once per toolchain identity."""
    v = tool.values
    env, flags, msvc = _compile_env(tool)
    cc, include = v["cc"], v["include"]
    if not v.get("runtime_library_object"):
        raise BuildError("the toolchain beside %s names no library runtime, so cint build --lib cannot run" % tool.exe)
    key = hashlib.sha256(RUNTIME_DOMAINS[backend] + b"\n" + tool.identity.encode("ascii") +
                         (b"\ntesting" if testing else b"")).hexdigest()
    base = os.path.join(tool.cache, "py-cuda" if backend == "cuda" else "py-rt")
    final = os.path.join(base, key)
    toolchain = os.path.join(final, "cint.toolchain")
    if os.path.isfile(toolchain):
        return toolchain
    os.makedirs(base, exist_ok=True)
    work = tempfile.mkdtemp(prefix="build-", dir=base)
    define = "/D" if msvc else "-D"
    obj = ".obj" if msvc else ".o"
    try:
        with open(os.path.join(work, RUNTIME_UNIT[0]), "wb") as f:
            f.write(RUNTIME_UNIT[1])
        objs = []
        for name, defines in RUNTIMES[backend]:
            defines += ("CINT_CUDA_TESTING",) if testing and name != RUNTIME_UNIT[0] else ()
            source = os.path.join(work if name == RUNTIME_UNIT[0] else include, name)
            out = os.path.join(work, name[:-2] + obj)
            command = [cc, *flags, *(define + d for d in defines), ("/I" if msvc else "-I") + include, "/c" if msvc
                       else "-c", source, ("/Fo" + out) if msvc else "-o"]
            _run(command + ([] if msvc else [out]), work, env, "compiling %s for %s" % (name, backend))
            objs.append(out)
        if len(objs) == 1:
            joined = os.path.join(final, os.path.basename(objs[0]))
        elif msvc:
            joined = os.path.join(final, "cint_rt_cuda.lib")
            librarian = os.path.join(os.path.dirname(cc), "lib.exe")
            _run([librarian, "/nologo", "/OUT:" + os.path.join(work, "cint_rt_cuda.lib"), *objs], work, env,
                 "joining the gpu-cuda runtime")
        else:
            # Apple's ld64 -r makes hidden symbols local, which leaves the runtime's internal calls
            # between the three objects unresolved; GNU ld keeps them hidden and global.
            darwin = v.get("leg") == "apple-clang" or sys.platform == "darwin"
            joined = os.path.join(final, "cint_rt_cuda.o")
            _run([cc, "-r", "-nostdlib", *(["-Wl,-keep_private_externs"] if darwin else []), *objs, "-o",
                  os.path.join(work, "cint_rt_cuda.o")], work, env, "joining the gpu-cuda runtime")
        lines = [TOOLCHAIN_HEADER.decode("utf-8").rstrip("\n")]
        for name, value in tool.rows:
            lines.append("%s %s" % (name, joined if name == "runtime_library_object" else value))
        if backend == "cuda":
            lines.append("flag %sCINT_GPU_CUDA" % define)
        with open(os.path.join(work, "cint.toolchain"), "wb") as f:
            f.write(("\n".join(lines) + "\n").encode("utf-8"))
        _publish(work, final, toolchain)
        work = None
    finally:
        if work is not None:
            shutil.rmtree(work, ignore_errors=True)
    return toolchain


def _keep_code(receipt: dict, root: str, out: str):
    """Copy the program's SIR text (output 1 of each module and the program
    file `cint-program.sites`, SPEC-09 SIR-17) from the build's staged
    outputs, and its sources from the source root, into `out`, each checked
    against the digest the receipt lists. Nothing is kept when any is
    missing or differs, so a library's `sir/` is whole or absent."""
    ident = receipt.get("identity", {})
    generated = ident.get("generated", {}).get("files", {})
    sources = ident.get("source", {}).get("files", {})
    cache = receipt.get("observations", {}).get("cache")
    if not cache or not generated or not sources:
        return
    try:
        with open(os.path.join(cache, "out", "MANIFEST.ref"), "rb") as f:
            stage = f.read().decode("utf-8").split(" ", 1)[0]
    except (OSError, UnicodeDecodeError):
        return
    if not stage or os.sep in stage or "/" in stage:
        return
    files = {}
    for name, digest in generated.items():
        if name.endswith(".sites"):
            files[name] = (os.path.join(cache, stage, *name.split("/")), digest)
    for name, digest in sources.items():
        files[name] = (os.path.join(root, *name.split("/")), digest)
    if "cint-program.sites" not in files:
        return
    kept = {}
    for name, (path, digest) in files.items():
        try:
            with open(path, "rb") as f:
                data = f.read()
        except OSError:
            return
        if hashlib.sha256(data).hexdigest() != digest:
            return
        kept[name] = data
    for name, data in kept.items():
        target = os.path.join(out, *name.split("/"))
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, "wb") as f:
            f.write(data)


def program_code(library: str) -> list | None:
    """The code kept beside a library that the bridge built: for each module,
    in the order of `cint-program.sites`, its path, its SIR text, and its
    source bytes or None. None when the library has no `sir/` beside it."""
    base = os.path.join(os.path.dirname(os.path.abspath(library)), SIR_DIR)
    try:
        with open(os.path.join(base, "cint-program.sites"), "rb") as f:
            listing = f.read().decode("ascii").splitlines()
        code = []
        for line in listing:
            parts = line.split(" ", 2)
            if len(parts) != 3 or parts[0] != "module" or not parts[2].endswith(".ci"):
                return None
            path = parts[2]
            with open(os.path.join(base, *(path[:-3] + ".sites").split("/")), "rb") as f:
                sir = f.read()
            source = os.path.join(base, *path.split("/"))
            if os.path.isfile(source):
                with open(source, "rb") as f:
                    source = f.read()
            else:
                source = None
            code.append((path, sir, source))
    except (OSError, UnicodeDecodeError):
        return None
    return code or None


def _source_root() -> str:
    """The source tree this package belongs to: python/cint is two levels below it."""
    return os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def interpreter_library() -> str:
    """The interpreter's library (cpu-sir-interp, SPEC-09 BACK-01a): the
    reader and executor of `interp/` with the runtime and the canonical state
    code of `rt/`, from the source tree this package belongs to, built as a
    shared library with the toolchain's C compiler once per toolchain and
    source digest, under the cache in `py-interp`. Raises `BuildError` when
    the tree or the toolchain is missing."""
    tree = _source_root()
    interp, rt = os.path.join(tree, "interp"), os.path.join(tree, "rt")
    if not (os.path.isfile(os.path.join(interp, "cint_interp.h")) and os.path.isfile(os.path.join(rt, "cint_state.c"))):
        raise BuildError("the interpreter's sources are not beside this package (%s has no interp/ and rt/), so "
                         "backend=\"ref\" cannot build cpu-sir-interp" % tree)
    tool = Toolchain(find_cint())
    env, flags, msvc = _compile_env(tool)
    h = hashlib.sha256(INTERP_DOMAIN + b"\n" + tool.identity.encode("ascii"))
    for d in (interp, rt):
        for name in sorted(os.listdir(d)):
            if name.endswith((".c", ".h")):
                h.update(b"\n" + name.encode("utf-8") + b" " + _sha256_file(os.path.join(d, name)).encode("ascii"))
    name = "cint_interp.dll" if os.name == "nt" else (
        "libcint_interp" + (".dylib" if sys.platform == "darwin" else ".so"))
    base = os.path.join(tool.cache, "py-interp")
    final = os.path.join(base, h.hexdigest())
    library = os.path.join(final, name)
    if os.path.isfile(library):
        return library
    os.makedirs(base, exist_ok=True)
    work = tempfile.mkdtemp(prefix="build-", dir=base)
    try:
        unit = os.path.join(work, RUNTIME_UNIT[0])
        with open(unit, "wb") as f:
            f.write(RUNTIME_UNIT[1])
        sources = [unit] + [os.path.join(interp, n) for n in INTERP_SOURCES]
        include = ["/I" + rt, "/I" + interp] if msvc else ["-I" + rt, "-I" + interp]
        if msvc:
            definitions = os.path.join(work, "cint_interp.def")
            with open(definitions, "wb") as f:
                f.write(("EXPORTS\n" + "".join("    %s\n" % n for n in INTERP_EXPORTS)).encode("ascii"))
            command = [tool.values["cc"], "/LD", *flags, *include, *sources, "/Fe" + os.path.join(work, name),
                       "/Fo" + work + os.sep, "/link", "/DEF:" + definitions]
        else:
            command = [tool.values["cc"], "-shared", *flags, *include, *sources, "-o", os.path.join(work, name)]
        _run(command, work, env, "building the interpreter's library")
        _publish(work, final, library)
        work = None
    finally:
        if work is not None:
            shutil.rmtree(work, ignore_errors=True)
    return library


def build_library(source: str, root: str | None = None, backend: str = "cpu") -> str:
    """The library of the program whose root module is `source`, built by
    `cint build --lib` unless an unchanged build is cached. `root` is the
    source root that module paths are relative to (`--root`), by default
    the directory of `source`. `backend` is "cpu" or "cuda"."""
    global builds
    source = os.path.abspath(source)
    root = os.path.abspath(root) if root is not None else os.path.dirname(source)
    module = os.path.relpath(source, root).replace(os.sep, "/")
    if module.startswith("../") or not module.endswith(".ci"):
        raise BuildError("%s is not a .ci module under the source root %s" % (source, root))
    if not os.path.isfile(source):
        raise FileNotFoundError(source)
    stem = module.rsplit("/", 1)[-1][:-3]
    tool = Toolchain(find_cint())
    if backend == "cuda":
        from ._cuda import stand_in
        testing = stand_in() is not None
        toolchain = ["--toolchain", _runtime_toolchain(tool, "cuda", testing)]
        variant = b"\nbackend gpu-cuda" + (b" testing" if testing else b"")
    elif backend == "cpu":
        toolchain = ["--toolchain", _runtime_toolchain(tool, "cpu")]
        variant = b"\nruntime with state"     # not a library of the toolchain's own runtime object
    else:
        raise BuildError("backend %r is not built by cint build --lib" % (backend,))
    probe = hashlib.sha256(CACHE_DOMAIN + b"\n" + tool.identity.encode("ascii") + b"\n" +
                           module.encode("utf-8") + variant).hexdigest()
    probe_dir = os.path.join(tool.cache, "py-lib", probe)
    found = _cached(probe_dir, root, stem)
    if found is not None:
        return found
    os.makedirs(probe_dir, exist_ok=True)
    work = tempfile.mkdtemp(prefix="build-", dir=probe_dir)
    receipt = os.path.join(work, "build.json")
    try:
        command = [tool.exe, "--json", "build", "--lib", *toolchain, "--receipt", receipt, "--out", work, "--root",
                   root, module]
        try:
            done = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace")
        except OSError as failure:
            raise BuildError("cint build --lib could not run %s: %s" % (tool.exe, failure)) from None
        builds += 1
        if done.returncode != 0:
            diagnostics = _diagnostics(done.stderr + "\n" + done.stdout)     # the CLI writes them to stderr
            first = diagnostics[0] if diagnostics else None
            what = "%s at %s:%s:%s: %s" % (first.get("code"), first.get("file"), first.get("line"),
                                           first.get("column"), first.get("message")) if first else \
                (done.stderr.strip() or done.stdout.strip() or "no message")
            raise BuildError("cint build --lib %s failed with exit status %d: %s" % (module, done.returncode, what),
                             status=done.returncode, diagnostics=diagnostics, output=done.stderr + done.stdout)
        with open(receipt, "rb") as f:
            built = json.loads(f.read())
        files, absent = _receipt_modules(built)
        _keep_code(built, root, os.path.join(work, SIR_DIR))
        final = os.path.join(probe_dir, _key(probe, files, absent))
        _publish(work, final, os.path.join(final, library_name(stem)))
        work = None
        return os.path.join(final, library_name(stem))
    finally:
        if work is not None:
            shutil.rmtree(work, ignore_errors=True)
