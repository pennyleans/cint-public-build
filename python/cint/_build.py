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
        self.rows = [line.split(" ", 1) for line in data.decode("utf-8").splitlines()[1:] if " " in line]
        values = dict(self.rows)
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


def build_library(source: str, root: str | None = None) -> str:
    """The library of the program whose root module is `source`, built by
    `cint build --lib` unless an unchanged build is cached. `root` is the
    source root that module paths are relative to (`--root`), by default
    the directory of `source`."""
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
    probe = hashlib.sha256(CACHE_DOMAIN + b"\n" + tool.identity.encode("ascii") + b"\n" +
                           module.encode("utf-8")).hexdigest()
    probe_dir = os.path.join(tool.cache, "py-lib", probe)
    found = _cached(probe_dir, root, stem)
    if found is not None:
        return found
    os.makedirs(probe_dir, exist_ok=True)
    work = tempfile.mkdtemp(prefix="build-", dir=probe_dir)
    receipt = os.path.join(work, "build.json")
    try:
        command = [tool.exe, "--json", "build", "--lib", "--receipt", receipt, "--out", work, "--root", root, module]
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
            files, absent = _receipt_modules(json.loads(f.read()))
        final = os.path.join(probe_dir, _key(probe, files, absent))
        try:
            os.rename(work, final)
        except OSError:
            if not os.path.isfile(os.path.join(final, library_name(stem))):
                raise
            shutil.rmtree(work, ignore_errors=True)      # another process built it first
        work = None
        return os.path.join(final, library_name(stem))
    finally:
        if work is not None:
            shutil.rmtree(work, ignore_errors=True)
