"""Keeps the staged bootstrap's pins and checks each one before a build relies on it.

    python tools/cint_stage.py check --leg msvc|gcc|clang|apple-clang [--pin N]
    python tools/cint_stage.py cut --pin N --commit REV --receipts DIR

The staged bootstrap builds the current compiler through a pinned one (SB-01, OQ-208;
docs/design/notes/2026-10-05-staged-bootstrap.md). bootstrap/pin-<n>/ holds byte-for-byte
copies of the compiler, runtime and host files that a passing fixpoint matrix names, under
their repository paths, and bootstrap/pin-<n>/PIN lists them with their sizes and digests and
cites the receipts. A pin is never edited (note section 7.4).

`check` builds no compiler. For every pin, or the one named:

1. files: every file under the pin other than PIN is listed in PIN with its size and SHA-256,
   and PIN lists no other file (note section 4.3, check 1);
2. receipts: the cited receipts are an intact, passing matrix (each receipt's identity_sha256
   recomputed, one receipt identity, FIX-04, and the four legs) whose sources are PIN's files
   and whose compiler source identity, seed source identity, runtime contract, S2 tree and
   seed-emitted S1 (SP) are PIN's (check 2);
3. SP: the seed, built with the pin's runtime and bridge, translates the pin's compiler/main.ci
   to C whose SHA-256 is PIN's sp_sha256 (check 3; pin 1, whose compiler the seed builds);

and once:

4. floor: the current rt/cint_bridge.c, rt/cint_build.c, compiler/tests/golden_host.c, the cint
   CLI (cli/) and tools/cint_measure_host.c compile without warnings, under the flags of
   tools/cint_check.py at -O0 with portable helpers, against the newest pin's cint_rt.h and
   cint_rt_internal.h beside the current bridge headers (note section 5.2);
5. rules: cint_ref reads and checks the import closure of compiler/main.ci, and the closure
   holds no call cycle, print statement, module-level variable or statement, call of a program
   function in a constant, static assertion or extent, or byte outside ASCII (note section 7.2).

A pin whose checks 1 or 2 fail is not built from. Every failure is a line `problem: ...` on
stderr, and the last line on stdout is `pass cint stage: ...` or `fail cint stage: ...`.

`cut` writes bootstrap/pin-<n>/ and its PIN from a commit and the fixpoint receipts that name
its inputs, reading each file from the commit with git, and refuses a commit without a passing
matrix, a commit whose files differ from the receipts' records, or a pin that exists.

Builds go under <build>/stage, where <build> is CINT_BUILD or cint-build in the system
temporary directory (cint_check.build_root), never inside the repository, and are removed.
"""
from __future__ import annotations

import argparse
import dataclasses
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import cint_bootstrap as boot  # noqa: E402
import cint_check as cc  # noqa: E402
import cint_fixpoint as fx  # noqa: E402
from cint_ref import check as ref_check  # noqa: E402
from cint_ref import parser as ref_parser  # noqa: E402
from cint_ref import tree as ref_tree  # noqa: E402
from cint_ref.faults import CompileError, Refused  # noqa: E402
from cint_ref.lexer import decode_source  # noqa: E402

PIN_HEADER = "CINT-PIN-1"
HEX64 = "[0-9a-f]{64}"
PIN_FIELDS = (("pin", "[1-9][0-9]*"), ("commit", "[0-9a-f]{40}"), ("receipts", "[A-Za-z0-9_.\\-/]+"),
              ("receipt_count", "[1-9][0-9]*"), ("receipt_identity", HEX64),
              ("compiler_source_identity", HEX64), ("seed_source_identity", HEX64),
              ("runtime_contract", "[a-z0-9][a-z0-9.\\-]*"), ("sp_sha256", HEX64), ("s2_tree_sha256", HEX64))
SOURCE_GROUPS = ("compiler_sources", "runtime_sources", "host_sources")
LEGS = ("apple-clang", "clang", "gcc", "msvc")
FLOOR_PIN = ("cint_rt.h", "cint_rt_internal.h")
FLOOR_CURRENT = ("cint_bridge.h", "cint_bridge_internal.h", "cint_bridge.c", "cint_build.c")


def utf8(text: str) -> bytes:
    return text.encode("utf-8")


def valid_path(path: str) -> bool:
    """A pinned file's repository path: relative, `/`-separated, no empty, `.` or `..` part."""
    return (re.fullmatch(r"[A-Za-z0-9_.\-/]+", path) is not None and path != "PIN"
            and not path.startswith("/") and all(p not in ("", ".", "..") for p in path.split("/")))


# --------------------------------------------------------------------------- PIN

def write_pin(fields: dict, files: dict) -> bytes:
    """PIN: the header, one `key value` line per field in PIN_FIELDS order, then one line
    `file <sha256> <bytes> <path>` per file in byte order of path; ASCII, each line ended by LF."""
    lines = [PIN_HEADER] + ["%s %s" % (key, fields[key]) for key, _ in PIN_FIELDS]
    for path in sorted(files, key=utf8):
        lines.append("file %s %d %s" % (cc.sha256(files[path]), len(files[path]), path))
    data = ("\n".join(lines) + "\n").encode("ascii")
    read_pin(data)
    return data


def read_pin(data: bytes) -> tuple:
    """(fields, files) of a PIN, files mapping each path to (sha256, bytes); ValueError if malformed."""
    try:
        text = data.decode("ascii")
    except UnicodeDecodeError:
        raise ValueError("PIN is not ASCII") from None
    if not text.endswith("\n") or "\r" in text:
        raise ValueError("PIN lines must each end with a line feed alone")
    lines = text[:-1].split("\n")
    if lines[0] != PIN_HEADER:
        raise ValueError("PIN does not begin with %s" % PIN_HEADER)
    fields = {}
    for (key, pattern), line in zip(PIN_FIELDS, lines[1:]):
        name, _, value = line.partition(" ")
        if name != key or re.fullmatch(pattern, value) is None or (key == "receipts" and not valid_path(value)):
            raise ValueError("PIN line %d: expected `%s <%s>`, found %r" % (len(fields) + 2, key, pattern, line))
        fields[key] = value
    if len(fields) != len(PIN_FIELDS):
        raise ValueError("PIN ends before its field `%s`" % PIN_FIELDS[len(fields)][0])
    files, previous = {}, None
    for number, line in enumerate(lines[1 + len(PIN_FIELDS):], 2 + len(PIN_FIELDS)):
        m = re.fullmatch(r"file (%s) (0|[1-9][0-9]*) (\S+)" % HEX64, line)
        if m is None or not valid_path(m.group(3)):
            raise ValueError("PIN line %d: expected `file <sha256> <bytes> <path>`, found %r" % (number, line))
        path = m.group(3)
        if previous is not None and utf8(path) <= utf8(previous):
            raise ValueError("PIN line %d: %s is out of byte order of path or repeated" % (number, path))
        files[path], previous = (m.group(1), int(m.group(2))), path
    if not files:
        raise ValueError("PIN lists no file")
    return fields, files


def pin_directories(root: pathlib.Path = ROOT) -> dict:
    """{n: directory} of every bootstrap/pin-<n>/."""
    found = {}
    for d in sorted((root / "bootstrap").glob("pin-*")):
        m = re.fullmatch(r"pin-([1-9][0-9]*)", d.name)
        if m is None or not d.is_dir() or d.is_symlink():
            raise ValueError("not a pin directory: %s" % d.relative_to(root).as_posix())
        found[int(m.group(1))] = d
    return dict(sorted(found.items()))


def pin_tree(directory: pathlib.Path) -> dict:
    """{path: bytes} of every file under a pin directory but PIN, paths relative to it."""
    files = {}
    for path in sorted(directory.rglob("*"), key=lambda p: utf8(p.as_posix())):
        if path.is_symlink():
            raise ValueError("a link in the pin: %s" % path)
        if path.is_dir():
            continue
        rel = path.relative_to(directory).as_posix()
        if rel != "PIN":
            files[rel] = path.read_bytes()
    return files


# --------------------------------------------------------------------------- checks 1 and 2

def check_files(listed: dict, files: dict) -> list:
    """Check 1: the pin's files are exactly PIN's, with PIN's sizes and digests."""
    problems = []
    for path in sorted(set(listed) | set(files), key=utf8):
        if path not in files:
            problems.append("PIN lists %s, which the pin does not hold" % path)
        elif path not in listed:
            problems.append("the pin holds %s, which PIN does not list" % path)
        elif (cc.sha256(files[path]), len(files[path])) != listed[path]:
            problems.append("%s: %d bytes with SHA-256 %s; PIN gives %d bytes with %s" % (
                path, len(files[path]), cc.sha256(files[path]), listed[path][1], listed[path][0]))
    return problems


def load_receipts(directory: pathlib.Path) -> dict:
    """{file name: receipt} of every .json file in a receipt directory."""
    if not directory.is_dir():
        raise ValueError("no receipt directory %s" % directory)
    return {p.name: json.loads(p.read_bytes())
            for p in sorted(directory.glob("*.json"), key=lambda p: utf8(p.name))}


def matrix_problems(receipts: dict) -> list:
    """A passing fixpoint matrix: schema 1 receipts, each intact and passing, one receipt
    identity (FIX-04), at least one receipt from each leg, each configuration once, one SP."""
    problems, identities, configurations, sp = [], set(), set(), set()
    if not receipts:
        return ["no fixpoint receipt"]
    for name, r in receipts.items():
        if not isinstance(r, dict) or r.get("schema") != fx.SCHEMA:
            problems.append("%s: not a %s" % (name, fx.SCHEMA))
            continue
        try:
            digest = cc.sha256(cc.canonical({"identity": r["identity"], "outcome": r["outcome"]}))
            result, c = r["outcome"]["result"], r["observations"]["configuration"]
            configuration = (c["leg"], c["opt"], c["helpers"], c["sanitize"])
            sp.add(r["observations"]["s1_sha256"])
        except (KeyError, TypeError, ValueError) as error:
            problems.append("%s: unreadable receipt (%s)" % (name, error))
            continue
        if digest != r.get("identity_sha256"):
            problems.append("%s: identity_sha256 does not match its identity and outcome" % name)
        if result != "pass":
            problems.append("%s: result %s" % (name, result))
        if configuration in configurations:
            problems.append("%s: a second receipt of configuration %s" % (name, "-".join(map(str, configuration))))
        configurations.add(configuration)
        identities.add(digest)
    if len(identities) > 1:
        problems.append("%d receipt identities; FIX-04 needs one" % len(identities))
    for leg in sorted(set(LEGS) - {c[0] for c in configurations}):
        problems.append("no receipt from the %s leg" % leg)
    if len(sp) > 1:
        problems.append("%d digests of the seed's C for the compiler (s1_sha256)" % len(sp))
    return problems


def matrix_fields(receipts: dict) -> dict:
    """What PIN records of a passing matrix, and the files its receipts name."""
    r = next(iter(receipts.values()))
    identity = r["identity"]
    fields = {"receipt_count": str(len(receipts)), "receipt_identity": r["identity_sha256"],
              "compiler_source_identity": identity["compiler_source_identity"],
              "seed_source_identity": identity["seed_source_identity"],
              "runtime_contract": identity["runtime_contract_version"],
              "sp_sha256": r["observations"]["s1_sha256"], "s2_tree_sha256": identity["s2_tree_sha256"]}
    named = {}
    for group in SOURCE_GROUPS:
        for record in identity[group]:
            if record["path"] in named or not valid_path(record["path"]):
                raise ValueError("the receipts name %r twice or as an invalid path" % record["path"])
            named[record["path"]] = (record["sha256"], record["bytes"])
    return {"fields": fields, "files": dict(sorted(named.items(), key=lambda kv: utf8(kv[0])))}


def check_receipts(fields: dict, listed: dict, receipts: dict) -> list:
    """Check 2: the cited receipts are a passing matrix that names PIN's files and values."""
    problems = matrix_problems(receipts)
    if problems:
        return problems
    matrix = matrix_fields(receipts)
    for key, value in matrix["fields"].items():
        if fields[key] != value:
            problems.append("PIN gives %s %s; the receipts give %s" % (key, fields[key], value))
    for path in sorted(set(listed) | set(matrix["files"]), key=utf8):
        if path not in matrix["files"]:
            problems.append("PIN lists %s, which the receipts do not name" % path)
        elif path not in listed:
            problems.append("the receipts name %s, which PIN does not list" % path)
        elif listed[path] != matrix["files"][path]:
            problems.append("%s: PIN gives %d bytes with %s; the receipts give %d bytes with %s" % (
                path, listed[path][1], listed[path][0], matrix["files"][path][1], matrix["files"][path][0]))
    return problems


# --------------------------------------------------------------------------- checks 3 and 4

def build_sp(tc: cc.Toolchain, pin_dir: pathlib.Path, seed: pathlib.Path, work: pathlib.Path) -> str:
    """Check 3: B0 from seed/*.c with the pin's runtime and bridge; SP = B0(the pin's main.ci)."""
    saved = cc.RT
    cc.RT = pin_dir / "rt"   # Toolchain.compile's include root: the pin's runtime headers
    try:
        dirs = {name: work / name for name in ("rt", "b0", "compiler")}
        for d in dirs.values():
            d.mkdir(parents=True)
        for src in sorted((pin_dir / "compiler").glob("*.ci"), key=lambda p: utf8(p.name)):
            shutil.copyfile(src, dirs["compiler"] / src.name)
        rt_objs = tc.compile([pin_dir / "rt" / n for n in ("cint_rt.c", "cint_bridge.c")], dirs["rt"],
                             "pin runtime")
        seed_objs = tc.compile(sorted(seed.glob("*.c"), key=lambda p: utf8(p.name)), dirs["b0"], "B0")
        b0 = tc.link_exe(seed_objs + rt_objs, dirs["b0"] / "cint-seed", "B0")
        sp = work / "sp.c"
        result = tc.run([b0, "main.ci", "-o", sp], dirs["compiler"], "SP: B0 translates the pin's main.ci")
        if result.stdout or not sp.is_file() or not sp.stat().st_size:
            raise cc.BuildError("SP: the seed did not produce its generated source")
        return cc.sha256(sp.read_bytes())
    finally:
        cc.RT = saved


def stage_floor(pin_dir: pathlib.Path, root: pathlib.Path, dest: pathlib.Path) -> pathlib.Path:
    """One include directory: the pin's runtime headers beside the current bridge headers and
    sources, since a quoted include looks first in the including file's own directory."""
    rt = dest / "rt"
    rt.mkdir(parents=True)
    for name in FLOOR_PIN:
        shutil.copyfile(pin_dir / "rt" / name, rt / name)
    for name in FLOOR_CURRENT:
        shutil.copyfile(root / "rt" / name, rt / name)
    return rt


def check_floor(tc: cc.Toolchain, pin_dir: pathlib.Path, work: pathlib.Path, compiler_identity: str) -> int:
    """Check 4: the host floor; returns the number of C files compiled."""
    rt = stage_floor(pin_dir, ROOT, work)
    saved, flags = cc.RT, tc.flags
    cc.RT = rt
    define = "/D" if tc.leg == "msvc" else "-D"
    proc = "cint_proc_win.c" if tc.leg == "msvc" else "cint_proc_posix.c"
    groups = (("floor: bridge and hosts", flags,
               [rt / "cint_bridge.c", rt / "cint_build.c", ROOT / "compiler/tests/golden_host.c"]),
              ("floor: cint CLI", boot.cli_compile_flags(flags, tc.leg, compiler_identity),
               [ROOT / "cli/cint_main.c", ROOT / "cli/cint_receipt.c", ROOT / "cli" / proc]),
              ("floor: measurement host", flags + [define + "CINT_BRIDGE_MEASURE_HOOKS"],
               [ROOT / "tools/cint_measure_host.c", rt / "cint_build.c"]))
    compiled = set()
    try:
        for index, (what, group_flags, sources) in enumerate(groups):
            obj_dir = work / ("obj-%d" % index)
            obj_dir.mkdir()
            tc.flags = group_flags
            tc.compile(sources, obj_dir, what)
            compiled.update(p.name for p in sources)
    finally:
        cc.RT, tc.flags = saved, flags
    return len(compiled)


# --------------------------------------------------------------------------- check 5

def read_compiler(root: pathlib.Path) -> tuple:
    """({rel: Module}, {rel: bytes}) of the import closure of compiler/main.ci, read and checked
    by cint_ref; CompileError or Refused if cint_ref does not accept it."""
    modules, sources = {}, {}

    def load(rel):
        path = root / "compiler" / rel
        if not path.is_file():
            return None
        sources[rel] = path.read_bytes()
        modules[rel] = ref_parser.parse_module(decode_source(sources[rel], rel), rel)
        return modules[rel]

    main = load("main.ci")
    if main is None:
        raise ValueError("no compiler/main.ci")
    ref_check.check_program(main, load)
    order = sorted(modules, key=utf8)
    return {rel: modules[rel] for rel in order}, {rel: sources[rel] for rel in order}


FIELDS = {}


def walk(node):
    """Every syntax node under node (a node or a list of them), in source order."""
    stack = [node]
    while stack:
        item = stack.pop()
        if isinstance(item, (list, tuple)):
            stack.extend(reversed(item))
        elif isinstance(item, ref_tree.Node):
            yield item
            names = FIELDS.get(type(item))
            if names is None:
                names = FIELDS[type(item)] = [f.name for f in dataclasses.fields(item)][::-1]
            stack.extend(getattr(item, name) for name in names)


def user_call(node) -> bool:
    """A call that cint_ref's checker resolved to a program function."""
    return isinstance(node, ref_tree.Call) and getattr(node, "kind", None) == "user"


def call_cycle(edges: dict):
    """The first call cycle found depth first, as [(caller, callee, position), ...], or None.
    edges maps each function to its calls [(callee, position)] in source order."""
    state = {}
    for start in edges:
        if start in state:
            continue
        state[start] = "open"
        stack, calls = [(start, iter(edges[start]))], []
        while stack:
            name, pending = stack[-1]
            step = next(pending, None)
            if step is None:
                state[name] = "done"
                stack.pop()
                if calls:
                    calls.pop()
                continue
            callee, pos = step
            if state.get(callee) == "open":
                at = [n for n, _ in stack].index(callee)
                return calls[at:] + [(name, callee, pos)]
            if callee not in state:
                state[callee] = "open"
                calls.append((name, callee, pos))
                stack.append((callee, iter(edges.get(callee, ()))))
    return None


def design_problems(modules: dict, sources: dict) -> list:
    """Check 5 over checked modules: the design rules of SPEC-09 5.3 that the compiler keeps."""
    problems, edges, owner = [], {}, {}
    for rel, m in modules.items():
        for f in m.functions:
            owner[id(f)] = (rel, f.name)
    where = lambda pos: "compiler/%s" % (pos,)
    for rel, m in modules.items():
        data = sources[rel]
        for i, byte in enumerate(data):
            if byte > 0x7F:
                line, column = data.count(b"\n", 0, i) + 1, i - data.rfind(b"\n", 0, i)
                problems.append("compiler/%s:%d:%d: a byte outside ASCII (0x%02X)" % (rel, line, column, byte))
                break
        for s in m.statements:
            if isinstance(s, ref_tree.VarDecl) and not s.is_const:
                problems.append("%s: a module-level variable `%s`" % (where(s.pos), s.name))
            else:
                problems.append("%s: a module-level statement" % where(s.pos))
        for node in walk([m.functions, m.tests, m.statements, m.kernels]):
            if isinstance(node, ref_tree.Print):
                problems.append("%s: a print statement" % where(node.pos))
        constant, seen = [m.consts, m.static_asserts], set()
        for node in walk([m.functions, m.tests, m.structs, m.kernels]):
            if isinstance(node, (ref_tree.StaticAssert, ref_tree.TypeRef)) or \
                    (isinstance(node, ref_tree.VarDecl) and node.is_const):
                constant.append(node)
        for node in walk(constant):
            if user_call(node) and id(node) not in seen:
                seen.add(id(node))
                problems.append("%s: a call of the program function `%s` in a constant, static assertion or "
                                "extent" % (where(node.pos), node.callee))
        for f in m.functions:
            edges[(rel, f.name)] = [(owner[id(n.func)], n.pos) for n in walk(f.body)
                                    if user_call(n) and id(n.func) in owner]
    cycle = call_cycle(edges)
    if cycle is not None:
        names = [cycle[0][0][1]] + [callee[1] for _, callee, _ in cycle]
        problems.append("%s: a call cycle, %s" % (where(cycle[-1][2]), " -> ".join("`%s`" % n for n in names)))
    return problems


# --------------------------------------------------------------------------- cut

def git_blob(commit: str, path: str) -> bytes:
    return subprocess.run(["git", "-c", "safe.directory=*", "cat-file", "blob", "%s:%s" % (commit, path)],
                          cwd=ROOT, capture_output=True, check=True).stdout


def cut(root: pathlib.Path, n: int, commit: str, receipts_dir: pathlib.Path, read=git_blob) -> pathlib.Path:
    """Write bootstrap/pin-<n>/ and its PIN from commit's files that the receipts name."""
    dest = root / "bootstrap" / ("pin-%d" % n)
    if dest.exists():
        raise ValueError("%s exists, and a pin is never edited" % dest.relative_to(root).as_posix())
    receipts = load_receipts(receipts_dir)
    problems = matrix_problems(receipts)
    if problems:
        raise ValueError("no passing matrix in %s: %s" % (receipts_dir, "; ".join(problems)))
    matrix = matrix_fields(receipts)
    files = {}
    for path, (digest, size) in matrix["files"].items():
        files[path] = read(commit, path)
        if (cc.sha256(files[path]), len(files[path])) != (digest, size):
            problems.append("%s at %s: %d bytes with %s; the receipts give %d bytes with %s" % (
                path, commit[:7], len(files[path]), cc.sha256(files[path]), size, digest))
    if problems:
        raise ValueError("; ".join(problems))
    fields = {"pin": str(n), "commit": commit,
              "receipts": receipts_dir.resolve().relative_to(root.resolve()).as_posix(), **matrix["fields"]}
    pin = write_pin(fields, files)
    dest.parent.mkdir(parents=True, exist_ok=True)
    pending = pathlib.Path(tempfile.mkdtemp(prefix=".pin-%d-" % n, dir=dest.parent))
    try:
        for path, data in files.items():
            (pending / path).parent.mkdir(parents=True, exist_ok=True)
            (pending / path).write_bytes(data)
        (pending / "PIN").write_bytes(pin)
        pending.replace(dest)
    except BaseException:
        shutil.rmtree(pending, ignore_errors=True)
        raise
    return dest


# --------------------------------------------------------------------------- check

def elapsed(t0: int) -> str:
    return "%d ms" % ((time.monotonic_ns() - t0) // 1000000)


def check(leg: str, only=None) -> bool:
    problems, summary = [], {"pins": [], "floor": [], "rules": []}
    pins = pin_directories()
    if only is not None:
        pins = {n: d for n, d in pins.items() if n == only}
    if not pins:
        problems.append("no pin under bootstrap/" if only is None else "no bootstrap/pin-%d/" % only)
    tc = cc.Toolchain(leg, "0", "portable", False)
    build = cc.build_root() / "stage"
    build.mkdir(parents=True, exist_ok=True)
    verified = {}
    for n, pin_dir in pins.items():
        t0 = time.monotonic_ns()
        fields, listed = None, {}
        try:
            fields, listed = read_pin((pin_dir / "PIN").read_bytes())
            if fields["pin"] != str(n):
                raise ValueError("PIN names pin %s" % fields["pin"])
            found = check_files(listed, pin_tree(pin_dir))
        except (OSError, ValueError) as error:
            found = [str(error)]
        problems += ["pin %d: %s" % (n, p) for p in found]
        print("%s files: pin %d, %d files (%s)" % ("fail" if found else "pass", n, len(listed), elapsed(t0)),
              flush=True)
        if found:
            continue
        t0 = time.monotonic_ns()
        receipts = {}
        try:
            receipts = load_receipts(ROOT / fields["receipts"])
            found = check_receipts(fields, listed, receipts)
        except (OSError, ValueError, KeyError, TypeError) as error:
            found = ["receipts: %s" % error]
        problems += ["pin %d: %s" % (n, p) for p in found]
        print("%s receipts: pin %d, %d receipts in %s, identity %s (%s)" % (
            "fail" if found else "pass", n, len(receipts), fields["receipts"], fields["receipt_identity"][:8],
            elapsed(t0)), flush=True)
        if found:
            continue
        verified[n] = pin_dir
        if n != 1:
            problems.append("pin %d: SP is defined only for pin 1, whose compiler the seed builds" % n)
            continue
        t0 = time.monotonic_ns()
        sp = None
        try:
            with tempfile.TemporaryDirectory(prefix="sp-", dir=build) as work:
                sp = build_sp(tc, pin_dir, ROOT / "seed", pathlib.Path(work))
            found = [] if sp == fields["sp_sha256"] else ["SP %s; PIN gives %s" % (sp, fields["sp_sha256"])]
        except (OSError, cc.BuildError, subprocess.SubprocessError) as error:
            found = ["SP: %s" % error]
        problems += ["pin %d: %s" % (n, p) for p in found]
        print("%s SP: pin %d, %s from the seed on %s (%s)" % ("fail" if found else "pass", n, (sp or "none")[:8],
                                                            leg, elapsed(t0)), flush=True)
        if not found:
            summary["pins"].append("pin %d, %d files, %d receipts (%s), SP %s on %s" % (
                n, len(listed), int(fields["receipt_count"]), fields["receipt_identity"][:8], sp[:8], leg))
    t0 = time.monotonic_ns()
    modules, sources = {}, {}
    try:
        modules, sources = read_compiler(ROOT)
        found = design_problems(modules, sources)
    except (CompileError, Refused, ValueError, OSError) as error:
        found = ["cint_ref does not accept the compiler: %s" % error]
    problems += ["rules: %s" % p for p in found]
    print("%s rules: %d modules, %d problems (%s)" % ("fail" if found else "pass", len(modules), len(found),
                                                     elapsed(t0)), flush=True)
    if not found:
        summary["rules"].append("design rules over %d modules" % len(modules))
    if pins and max(pins) in verified:
        n, count = max(pins), 0
        t0 = time.monotonic_ns()
        identity = cc.sha256(boot.source_manifest({"compiler/" + rel: data for rel, data in sources.items()}))
        try:
            with tempfile.TemporaryDirectory(prefix="floor-", dir=build) as work:
                count = check_floor(tc, verified[n], pathlib.Path(work), identity)
            found = []
        except (OSError, ValueError, cc.BuildError, subprocess.SubprocessError) as error:
            found = [str(error)]
        problems += ["floor: %s" % p for p in found]
        print("%s floor: %d files against pin %d's runtime header on %s (%s)" % (
            "fail" if found else "pass", count, n, leg, elapsed(t0)), flush=True)
        if not found:
            summary["floor"].append("host floor")
    for p in problems:
        print("problem: %s" % p, file=sys.stderr)
    if problems:
        print("fail cint stage: %d problems" % len(problems), flush=True)
    else:
        print("pass cint stage: %s" % ", ".join(summary["pins"] + summary["floor"] + summary["rules"]), flush=True)
    return not problems


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="command", required=True)
    c = sub.add_parser("check", help="check every pin, the host floor and the design rules")
    c.add_argument("--leg", required=True, choices=LEGS)
    c.add_argument("--pin", type=int)
    k = sub.add_parser("cut", help="write bootstrap/pin-<n>/ from a commit and its fixpoint receipts")
    k.add_argument("--pin", type=int, required=True)
    k.add_argument("--commit", required=True)
    k.add_argument("--receipts", type=pathlib.Path, required=True)
    args = ap.parse_args(argv)
    if args.command == "check":
        if os.name == "nt" and args.leg != "msvc":
            command = ["wsl", "-d", cc.WSL_DISTRO, "--", "python3", cc.wsl_path(pathlib.Path(__file__).resolve()),
                       "check", "--leg", args.leg] + (["--pin", str(args.pin)] if args.pin is not None else [])
            return subprocess.run(command, cwd=ROOT, env=cc.wsl_env()).returncode
        if args.leg == "msvc" and os.name != "nt":
            ap.error("the msvc leg requires Windows")
        try:
            return 0 if check(args.leg, args.pin) else 1
        except (OSError, ValueError, cc.BuildError, subprocess.SubprocessError) as error:
            print("problem: %s" % error, file=sys.stderr)
            print("fail cint stage: no pin directory or no toolchain for the %s leg" % args.leg)
            return 1
    if args.pin < 1:
        ap.error("--pin takes a number from 1")
    try:
        commit = subprocess.run(["git", "-c", "safe.directory=*", "rev-parse", "--verify", args.commit + "^{commit}"],
                                cwd=ROOT, capture_output=True, text=True, check=True).stdout.strip()
        dest = cut(ROOT, args.pin, commit, args.receipts)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print("cut: %s" % (error.stderr.strip() if isinstance(error, subprocess.CalledProcessError) else error),
              file=sys.stderr)
        return 1
    fields, files = read_pin((dest / "PIN").read_bytes())
    print("pin %d: %d files from %s, %d receipts (%s), SP %s: %s" % (
        args.pin, len(files), commit[:7], int(fields["receipt_count"]), fields["receipt_identity"][:8],
        fields["sp_sha256"][:8], dest.relative_to(ROOT).as_posix()))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
