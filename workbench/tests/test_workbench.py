"""Build the workbench library and its executables on one leg; check them (box 14, units 1, 1b, 2a, 2b, and 3a).

Usage (from the repository root):
    python workbench/tests/test_workbench.py [--leg msvc|gcc|clang|apple-clang] [--out DIR] [--cint PATH]

Legs as in rt/tests/test_rt.py: `msvc` on Windows, `gcc` and `clang` on Linux, `apple-clang`
on macOS. Each leg builds workbench/*.c at -O0 and -O2 (/Od and /O2) with the strict warning
flags and -Werror (/WX); the Linux legs also build both with -fsanitize=address,undefined.
Each configuration builds and checks:

- test_wb (workbench/tests/test_wb.c): the multiword decimals, the fault record decoder, the
  report, and the inspector's formats, against texts written out in the test;
- wb-report (workbench/tests/wb_report.c), which renders a record file as the workbench
  reports it, against `cint run` (below);
- the executables cint-workbench, cint-repl, cint-explain, and cint-debug (box 14 ruling
  BX14-03 A): usage, exit statuses, and the CINT-TOOL-1 line under --json;
- `cint explain` (unit 1b; SPEC-06 section 7) on every module of the SIR text under
  interp/tests/sir/ and interp/tests/kernels/, below;
- the session (units 2a, 2b, and 3a; sections 5.2 and 5.4 of the box 14 note): its loop,
  prompts, continuation lines, and the commands that compile nothing, with a saved and reopened
  session that has no entry, as an exact transcript with any file as `cint`; with --cint PATH, a
  transcript of entries that B1 compiles, :reload of a module changed on disk, a transcript of
  checkpoints, :restore, :undo, branches, :diff, watches, and :save, the saved session opened in
  a new process, manifests that :open refuses, and on POSIX an interrupt of a running entry;
- the checks of each later unit's own module, workbench/tests/wbt_*.py (units()), so that units
  built in parallel add files rather than edit this one: wbt_replay.py holds unit 3a's
  transcript of the recording (:trace, :replay with a watch added after the fact, :effects, and
  :goto, with the recording saved, opened in a new process, and refused when a count is altered).

Explain. For each module of each program under interp/tests/sir/ and interp/tests/kernels/,
cint-explain --json --sir reads the SIR text through box 13's reader. Its CINT-EXPLAIN-1 object
must be canonical, carry the revision identity of the directory's REVISIONS, and agree with a
reading of the same text written here: for each function, kernel, script, and test, its SIR text
byte for byte, its signature, the identifier, position, and operation of every fault site, its
print statements, and its fuel charge points, a dispatch's included; each module's compile budget
is k x its source bytes + c (SPEC-09 CINTC-09). The fault codes of each operation are checked
against a table written here from SPEC-01 IM-104, those of a dispatch and of a kernel's entry
against the kernel's entry checks, and those of a kernel's reductions without a fuel charge; the
text form must carry the same sites and the same SIR text. What a kernel checks, reads, writes,
and publishes is wbt_arrays.py's. With --cint PATH, explain also
compiles a program with that `cint` and reads what it wrote.

Report parity (decision 26; box 14 ruling BX14-05 A). For each case NAME of
workbench/tests/fixtures/report/, NAME.rec.hex holds the fault record that `cint run` left for
NAME.ci, NAME.stderr what it printed, and NAME.json what it printed under --json, captured from
a bootstrapped `cint`. For the same record, fuel, and state, the workbench's human form must
begin with NAME.stderr byte for byte and add only the lines that are not canonical (default
BX14-13), and its JSON must equal NAME.json. With --cint PATH, the test also runs that `cint`
on each case and checks that it still prints the captured texts and leaves the captured record.

Build outputs go under <build>/workbench/<leg>/, never inside the repository, where <build> is
the CINT_BUILD environment variable or cint-build in the system temporary directory. The
script is a unittest module; it uses only the Python standard library and tools/cint_check.py.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import os
import pathlib
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
WB = ROOT / "workbench"
TESTS = WB / "tests"
REPORT = TESTS / "fixtures" / "report"
SIR = ROOT / "interp" / "tests" / "sir"
KERNELS = ROOT / "interp" / "tests" / "kernels"   # box 13 unit 7b's kernel programs
CONFORMANCE = ROOT / "conformance"
sys.path.insert(0, str(ROOT / "tools"))
import cint_check as cc  # noqa: E402

LIBRARY = sorted(path.name for path in WB.glob("wb_*.c"))   # every source of the library, a unit's new ones included
# The library links box 13's SIR text reader and interpreter and the runtime (box 14 ruling BX14-02 B).
LINKED = [ROOT / "interp" / "sir_read.c", ROOT / "interp" / "sir_exec.c", ROOT / "rt" / "cint_rt.c",
          ROOT / "rt" / "cint_state.c", ROOT / "rt" / "cint_rec.c"]
EXECUTABLES = {"cint-workbench": "cint_workbench.c", "cint-repl": "cint_repl.c",
               "cint-explain": "cint_explain.c", "cint-debug": "cint_debug.c"}
STATE = "the program stopped; stdout holds what it wrote before the fault"

ARGS = argparse.Namespace(leg=None, out=None, cint=None)


def source_identity() -> str:
    """The workbench's source identity, which `cint debug diff --workbench-identity` writes in its
    receipt (SPEC-06 12.2): tools/cint_check.py's digest of the files of its size row,
    workbench/*.c and workbench/*.h, as interp/tests/test_interp.py's is of the interpreter's."""
    return cc.files_digest(sorted(WB.glob("*.c")) + sorted(WB.glob("*.h")))


def default_out(leg: str) -> pathlib.Path:
    build = os.environ.get("CINT_BUILD") or os.path.join(tempfile.gettempdir(), "cint-build")
    return pathlib.Path(build) / "workbench" / leg


def configurations(leg: str):
    configs = [("O0", "0", False), ("O2", "2", False)]
    if leg in ("gcc", "clang"):
        configs += [("O0-san", "0", True), ("O2-san", "2", True)]
    return configs


def build(leg: str, opt: str, sanitized: bool, outdir: pathlib.Path) -> dict:
    """Every executable of one configuration, by name."""
    tc = cc.Toolchain(leg, opt, "portable", sanitized)
    inc = "/I" if leg == "msvc" else "-I"
    tc.flags = tc.flags + [inc + str(WB), inc + str(ROOT / "cli"), inc + str(ROOT / "interp")]
    proc = ROOT / "cli" / ("cint_proc_win.c" if leg == "msvc" else "cint_proc_posix.c")
    lib = tc.compile([WB / name for name in LIBRARY] + [proc] + LINKED, outdir, "workbench library")
    exes = {"test_wb": tc.link_exe(tc.compile([TESTS / "test_wb.c"], outdir, "test_wb") + lib,
                                   outdir / "test_wb", "test_wb")}
    for main in sorted(TESTS.glob("wb_*.c")):   # the test programs, a unit's new ones included: wb-report, ...
        name = main.stem.replace("_", "-")
        exes[name] = tc.link_exe(tc.compile([main], outdir, name) + lib, outdir / name, name)
    for name, main in EXECUTABLES.items():
        exes[name] = tc.link_exe(tc.compile([WB / main], outdir, name) + lib, outdir / name, name)
    return exes


def units() -> list:
    """The test modules of later units, workbench/tests/wbt_*.py, in name order. Each defines
    check(case, wb, exes) -> int, which test_builds_and_runs calls for each configuration: case is
    the running test, wb this module (run, converse, transcript, ARGS, the paths), and exes the
    executables just built. It returns the number of checks made, 0 when it needs --cint and has
    none."""
    found = []
    for path in sorted(TESTS.glob("wbt_*.py")):
        spec = importlib.util.spec_from_file_location(path.stem, path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        found.append(module)
    return found


def run(argv, cwd=None, entries=None) -> subprocess.CompletedProcess:
    """argv's result; `entries`, when given, is its standard input."""
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:abort_on_error=0",
               UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1")
    if entries is not None:
        return subprocess.run([str(a) for a in argv], cwd=cwd, env=env, capture_output=True,
                              input=entries.encode("utf-8"), timeout=300)
    return subprocess.run([str(a) for a in argv], cwd=cwd, env=env, capture_output=True,
                          stdin=subprocess.DEVNULL, timeout=120)


def converse(argv, cwd, steps):
    """Runs argv, writing each step's line (or calling it with the process, when it is a function) and then waiting
    for the step's text on standard output. Returns standard output, standard error, and the exit
    status; a step whose text does not come in 120 s fails the test."""
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:abort_on_error=0",
               UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1")
    proc = subprocess.Popen([str(a) for a in argv], cwd=cwd, env=env, stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    out, err, ready = bytearray(), bytearray(), threading.Condition()

    def pump(stream, into):
        for chunk in iter(lambda: stream.read1(4096), b""):
            with ready:
                into.extend(chunk)
                ready.notify_all()

    pumps = [threading.Thread(target=pump, args=(proc.stdout, out), daemon=True),
             threading.Thread(target=pump, args=(proc.stderr, err), daemon=True)]
    for t in pumps:
        t.start()
    seen = 0
    try:
        for send, want in steps:
            if callable(send):
                send(proc)
            else:
                proc.stdin.write(send.encode("utf-8") + b"\n")
                proc.stdin.flush()
            with ready:
                if not ready.wait_for(lambda: out.find(want.encode("utf-8"), seen) >= 0, timeout=120):
                    raise AssertionError("no %r after %r; the session wrote %r" % (want, send, bytes(out[seen:])))
                seen = out.find(want.encode("utf-8"), seen) + len(want)
        proc.stdin.close()
        status = proc.wait(timeout=120)
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()
    for t in pumps:
        t.join(timeout=10)
    proc.stdout.close()
    proc.stderr.close()
    return bytes(out), bytes(err), status


def excerpt(source: bytes, line: int, column: int) -> bytes:
    """The SPEC-04 LS-276 excerpt with no label, written here independently of wb_fault.c."""
    text = source.decode("utf-8").split("\n")[line - 1].rstrip("\r")
    w = min(6, max(2, len(str(line))))
    caret = "".join("\t" if ch == "\t" else " " for ch in text[:column - 1])
    pad = " " * w
    return f"{pad} |\n{line:>{w}} | {text}\n{pad} | {caret}^\n{pad} |\n".encode("utf-8")


def stack_lines(case: str, stack: list) -> bytes:
    """What the workbench adds after `cint run`'s lines for a stack, with the source loaded."""
    if not stack:
        return b""
    source = (REPORT / (case + ".ci")).read_bytes()
    out = b"   = noncanonical: not part of the fault record\n"
    for entry in stack:
        out += f"   = stack-source: {entry['file']}:{entry['line']}:{entry['column']}\n".encode("utf-8")
        out += excerpt(source, entry["line"], entry["column"])
    return out


def cases():
    return sorted(p.stem for p in REPORT.glob("*.ci"))


SITE_AT = re.compile(r" +#(\d+) @(\S+):(\d+):(\d+)$")
LOWERING = [{"backend": "cpu-c17", "profile": "ref-c", "status": "supported"},
            {"backend": "cpu-sir-interp", "status": "supported"}]
# The fault codes of a site's operation (SPEC-01 IM-104, IM-39, IM-45, IM-49, IM-52), written
# here independently of wb_explain.c; an empty list is a site no operand of its type can fault.
CODES = {
    "add.checked.i64": ["E_OVERFLOW"], "sub.checked.u8": ["E_OVERFLOW"], "abs.checked.i64": ["E_OVERFLOW"],
    "neg.checked.i64": ["E_OVERFLOW"], "mul.checked.i64": ["E_OVERFLOW"],
    "div.checked.i64": ["E_OVERFLOW", "E_DIV_ZERO"], "div_trunc.checked.i64": ["E_OVERFLOW", "E_DIV_ZERO"],
    "div_round.checked.i64.half_even": ["E_OVERFLOW", "E_DIV_ZERO"], "rem.checked.i64": ["E_DIV_ZERO"],
    "rem_euclid.checked.i64": ["E_DIV_ZERO"], "muldiv.checked.u64.u64.floor": ["E_OVERFLOW", "E_DIV_ZERO"],
    "shl.checked.i64": ["E_OVERFLOW", "E_SHIFT"], "shl.wrap.u8": ["E_SHIFT"], "shr.checked.i64": ["E_SHIFT"],
    "rotl.checked.u8": ["E_SHIFT"], "as.checked.u8.i64": ["E_NARROW"], "as.checked.i64.i32": ["E_NARROW"],
    "index.checked.i64": ["E_BOUNDS"], "index.checked.struct": ["E_BOUNDS"], "copy.shape": ["E_SHAPE"],
    "bind.shape": ["E_SHAPE"], "clamp.checked.i64": ["E_DOMAIN"], "isqrt.checked.i64": ["E_DOMAIN"],
    "isqrt.checked.u64": [], "isqrt_round.checked.i64.ceil": ["E_OVERFLOW", "E_DOMAIN"],
    "isqrt_round.checked.u64.away": ["E_OVERFLOW"], "assert.checked.bool": ["E_ASSERT"],
    "fuel.charge": ["E_FUEL"], "call.enter": ["E_FUEL", "E_DEPTH"], "index.checked.i32": ["E_BOUNDS"],
    "as.checked.i32.i64": ["E_NARROW"], "sub.checked.i64": ["E_OVERFLOW"],
}
# A kernel's reduction charges no fuel, since the dispatch charges each work-item (SPEC-01
# IM-139). Its step (reduce.add) faults as its operation does on one term, and its epilogue
# (reduce.final) also faults E_SHAPE for a minimum or a maximum that nothing contributed to.
KERNEL_REDUCE = {"sum": ["E_OVERFLOW"], "fold_checked": ["E_OVERFLOW"], "reduce_min": ["E_SHAPE"],
                 "reduce_max": ["E_SHAPE"]}
KERNEL_KEYS = ("aliasing", "domain", "publish", "reads", "reductions", "shapes", "writes")   # wbt_arrays.py's


WB_CODES = ["E_OVERFLOW", "E_DIV_ZERO", "E_BOUNDS", "E_SHAPE", "E_SHIFT", "E_NARROW", "E_ALIAS", "E_STALE_HANDLE",
            "E_FUEL", "E_UNSUPPORTED", "E_DOMAIN", "E_DEPTH", "E_ASSERT"]   # codes 1 to 13 (SPEC-01 9.1)
# The text form of one function (default BX14-12): the SIR text follows it.
ADD3 = """\
function add3   module/lib/plain_math.sites:4   pure
  signature  (in x: I64, in y: I64, in z: I64) -> I64
  fault sites (3): 3 possible, 0 statically excluded
    add3#1  module/lib/plain_math.ci:12:12  fuel.charge      E_FUEL possible
    add3#2  module/lib/plain_math.ci:13:14  add.checked.i64  E_OVERFLOW possible
    add3#3  module/lib/plain_math.ci:13:18  add.checked.i64  E_OVERFLOW possible
  hint       not analyzed: excluding a possible site needs range analysis, which no component performs yet
  fuel       fuel-v1 charge points (SPEC-01 IM-139, SPEC-09 SIR-07)
    add3#1  fuel.charge 1   at the start of the body
  lowering
    cpu-sir-interp    supported   the workbench evaluator runs this text (SPEC-09 BACK-01a)
    cpu-c17 (ref-c)   supported   cintc emits C17 from the same SIR (SPEC-09 BACK-03)
  sir
"""


def bridge_constant(name: str) -> int:
    text = (ROOT / "rt" / "cint_bridge.h").read_text(encoding="utf-8")
    return int(re.search(r"#define %s \(\(int64_t\)(\d+)\)" % name, text).group(1))


def sir_cases():
    return sorted(p for d in (SIR, KERNELS) for p in d.iterdir() if p.is_dir())


def revisions() -> dict:
    return dict(line.split() for d in (SIR, KERNELS) for line in (d / "REVISIONS").read_text(encoding="ascii").splitlines())


def sir_modules(case: pathlib.Path) -> list:
    """The module paths of a program, in the order of its cint-program.sites."""
    lines = (case / "cint-program.sites").read_text(encoding="utf-8").splitlines()
    return [line.split(" ", 2)[2] for line in lines if line.startswith("module ")]


def entry_checks(case: pathlib.Path) -> dict:
    """The codes of each kernel's entry checks in a program, by its dotted name: E_SHAPE for a
    shape or where clause, E_ALIAS for an alias check (SPEC-02 F-5)."""
    out = {}
    for module in sir_modules(case):
        lines = (case / (module[:-len(".ci")] + ".sites")).read_text(encoding="utf-8").split("\n")
        for at, line in enumerate(lines):
            m = re.match(r"(?:export )?kernel @([\w.]+)\(", line)
            if m:
                body = [step.strip() for step in lines[at + 1:lines.index("}", at)]]
                out[m.group(1)] = (["E_SHAPE"] if any(s.startswith(("bind.shape ", "bind.where ")) for s in body) else []) + \
                                  (["E_ALIAS"] if any(s.startswith("bind.alias ") for s in body) else [])
    return out


def sir_functions(case: pathlib.Path, module: str) -> list:
    """Each function, kernel, script, and test of a module's SIR text, read here independently of
    interp/sir_read.c and workbench/wb_explain.c: what explain reports of it, but its effect
    class (explained() adds it), the codes, status, and reason of its fault sites (`_codes` has
    those of a dispatch, a kernel's entry, and a kernel's epilogue), and what a kernel checks,
    reads, writes, and publishes (KERNEL_KEYS)."""
    file = module[:-len(".ci")] + ".sites"
    lines = (case / file).read_text(encoding="utf-8").split("\n")
    dotted = lines[0].split(" ", 1)[1]
    entries = entry_checks(case)
    sites = {}
    for item in lines[1].split(" ")[2:]:
        ordinal, line, column, op = item.split(":", 3)
        sites[int(ordinal)] = (int(line), int(column), op)
    out = []
    for at, line in enumerate(lines):
        head = line[len("export "):] if line.startswith("export ") else line
        word = head.split(" ", 1)[0]
        if word not in ("func", "kernel", "script", "test"):
            continue
        body = lines[at:lines.index("}", at) + 1]
        f = {"kind": "function" if word == "func" else word, "lowering": LOWERING,
             "sir": "\n".join(body) + "\n", "sir_position": {"file": file, "line": at + 1}}
        if word in ("func", "kernel"):
            name = head[len(word + " @" + dotted + "."):head.index("(")]
            params = head[head.index("(") + 1:head.rindex(")")]
            # a kernel's view is `out` or `inout` by how it is staged (SPEC-02 K-10), 1 or 2
            staged = {m.group(1): m.group(2) for m in (re.match(r"\s*dispatch\.stage \$sc\.\d+, %(?:p\.)?(\w+), (\d)", s)
                                                        for s in body) if m}
            f["name"], f["parameters"] = name, []
            for item in re.split(r", (?=%)", params) if params else []:
                pname, ptype = item[1:].split(": ", 1)
                pname = pname[len("p."):] if pname.startswith("p.") else pname
                if word == "kernel":
                    direction = "out" if ptype.startswith("out ") else {"1": "out", "2": "inout"}.get(staged.get(pname), "in")
                else:
                    direction = "view" if "[" in ptype else "inout" if ptype.startswith("inout ") else "in"
                f["parameters"].append({"direction": direction, "name": pname,
                                        "type": ptype.split(" ", 1)[1] if ptype.startswith(("inout ", "out ")) else ptype})
            if ") -> " in head:
                f["result"] = head[head.index(") -> ") + len(") -> "):-len(" {")]
        elif word == "test":
            name = "test" + head.split(" ")[2]
            f["test"] = int(head.split(" ")[2])
        else:
            name = "script"
        f["fault_sites"], f["prints"], f["fuel"], f["_codes"] = [], [], [], {}
        f["_id"] = "%s.%s" % (dotted, name) if word in ("func", "kernel") else "%s#%s" % (dotted, name)
        f["_calls"] = [m.group(1) for m in (re.search(r"(?:call|dispatch) @([\w.]+)\(", t) for t in body) if m]
        loops = []
        for text in body[1:-1]:
            step = text.strip()
            if step.startswith("}"):
                loops.pop()
            if step.endswith("{"):
                loops.append(step.split(" ")[1] if step.startswith("loop ") else None)
            m = SITE_AT.search(text)
            if m is None:
                if step.startswith("fuel.charge"):
                    raise ValueError("a charge point with no site: " + text)
                continue
            ordinal = int(m.group(1))
            line, column, op = sites[ordinal]
            if (m.group(2), int(m.group(3)), int(m.group(4))) != (module, line, column):
                raise ValueError("the sites line and the node disagree: " + text)
            ident = "%s#%d" % (name, ordinal)
            if op == "print":
                f["prints"].append(ident)
                continue
            f["fault_sites"].append({"id": ident, "operation": op,
                                     "position": {"column": column, "file": module, "line": line}})
            inner = [label for label in loops if label is not None]
            if op == "fuel.charge":
                charge = {"amount": int(step.split(" ")[1]), "charge": "iteration" if inner else "start", "id": ident}
                if inner:
                    charge["loop"] = inner[-1]
                f["fuel"].append(charge)
            elif op == "call.enter":
                callee = re.search(r"call @([\w.]+)\(", step).group(1)
                f["fuel"].append({"amount": 1, "callee": callee, "charge": "call", "id": ident})
            elif op in ("dispatch", "dispatch.entry"):   # one unit for each work-item (SPEC-02 K-9)
                callee = re.search(r"dispatch @([\w.]+)\(", step).group(1) if op == "dispatch" else "%s.%s" % (dotted, name)
                f["fuel"].append(dict({"callee": callee} if op == "dispatch" else {}, charge=op, id=ident, per="work-item"))
                f["_codes"][ident] = [c for c in WB_CODES if c in entries[callee] + ["E_FUEL"]]
            elif word == "kernel" and step.startswith(("reduce.add ", "reduce.final ")) and op.split(".")[0] in KERNEL_REDUCE:
                final = step.startswith("reduce.final ")
                f["_codes"][ident] = [c for c in KERNEL_REDUCE[op.split(".")[0]] if final or c != "E_SHAPE"]
        out.append(f)
    return out


def explained(case: pathlib.Path, module: str) -> list:
    """sir_functions() with the effect class of each (SPEC-03 H-2, H-7): `observe` when it prints
    or calls a function of the program that is `observe`, `pure` otherwise."""
    every = [f for m in sir_modules(case) for f in sir_functions(case, m)]
    observe, changed = set(), True
    while changed:
        changed = False
        for f in every:
            if f["_id"] not in observe and (f["prints"] or any(c in observe for c in f["_calls"])):
                observe.add(f["_id"])
                changed = True
    out = []
    for f in sir_functions(case, module):
        f["effects"] = "observe" if f.pop("_id") in observe else "pure"
        del f["_calls"], f["_codes"]
        out.append(f)
    return out


def canonical(value) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode("ascii") + b"\n"


SESSION_BANNER = "cint workbench   profile cint-core-1   backend cpu-sir-interp   new session"
UNSAVED = "the session has changes since it was last saved; :save PATH keeps them, and :quit again ends it\n"
SESSION_PLAIN = [  # (entry, what the session writes after it) for entries that compile nothing
    (":fuel 5", "fuel: 5 units per entry\n"),
    (":fuel none", "fuel: no allowance per entry\n"),
    (":fuel x", ":fuel takes N >= 0, or none\n"),
    (":depth 0", ":depth takes N >= 1\n"),
    (":depth 9", "depth: 9 per entry\n"),
    (":backend", "backend: cpu-sir-interp\n"),
    (":backend cpu-c17", "backend: cpu-c17; each revision is built as a library, entries on it are not recorded, "
                         "and :inspect and the watches stay on cpu-sir-interp\n"),
    (":backend cpu-sir-interp", "backend: cpu-sir-interp\n"),
    (":backend gpu-cuda", ":backend gpu-cuda waits for a gpu-cuda library: `cint build --lib` builds cpu-c17 "
                          "only\n"),
    (":backend other", ":backend: no backend other (cpu-sir-interp, cpu-c17, gpu-cuda)\n"),
    (":checkpoint", ":checkpoint takes a NAME of at most 64 letters, digits, _, - and .\n"),
    (":checkpoint auto-1", ":checkpoint: names auto-N are the session's own; choose another\n"),
    (":undo", "nothing to undo\n"),
    (":watches", "no watches\n"),
    (":watch", ":watch takes a pure EXPR, then when COND, then stop, as you need them\n"),
    (":watch log 1", "no watch 1\n"),
    (":unwatch 1", "no watch 1\n"),
    (":diff", ":diff takes a checkpoint NAME and an optional BINDING\n"),
    (":restore x", "no checkpoint x (:branches lists them)\n"),
    (":branches", "* main   checkpoints 0   head the start\n"),
    (":checkpoint start", "checkpoint start   #1   revisions: none\n"),
    (":diff start", "no binding differs from start (0 compared)\n"),
    (":restore start", "current state equals checkpoint start; no automatic checkpoint\nrestored start   #1\n"),
    (":branch b", "current state equals checkpoint start; no automatic checkpoint\nbranch b from start\n"),
    (":branches", "  main   checkpoints 1   head start\n* b   from start   checkpoints 0   head start\n"
                  "#1  start   main   before any entry\n"),
    (":quit", UNSAVED),
    (":save", ":save takes a PATH, the directory of the saved session\n"),
    (":save saved", "saved session to saved   entries 0   checkpoints 1   objects 4\n"),
    (":open", ":open takes a PATH, the directory of a saved session\n"),
    (":open nowhere", "no session manifest at nowhere/session.manifest: not a saved session, or a save that did "
                      "not finish; nothing changed\n"),
    (":open saved", "opened saved   entries 0   checkpoints 1   branch b\n"),
    (":replay", ":replay takes a checkpoint NAME\n"),
    (":replay start", "checkpoint start, or the current state, was not recorded, so no replay reaches it\n"),
    (":goto 1", ":goto takes N, an entry of this session, or fault\n"),
    (":goto fault", "no entry of this session faulted\n"),
    (":trace", "no entry of this session ran code\n"),
    (":effects", "the current state is not recorded: no entry has run on it\n"),
    (":save-case", ":save-case takes a PATH, the file of the case\n"),
    (":save-case c", "the current state was not recorded, so no case reaches it\n"),
    (":place", ":place waits for a gpu-cuda library: `cint build --lib` builds cpu-c17 only\n"),
    (":history x", "x has no revision in this session\n"),
    (":history", ":history takes a NAME\n"),
    (":log", ""),
    (":fault", "no fault in this session\n"),
    (":fault /a", ":fault takes /path, or nothing\n"),
    (":fault /path", "no recorded entry of this session left an error result\n"),
    (":nope", "no command :nope; :help lists them\n"),
    ("   ", ""),
    ("// a comment alone", ""),
    ("/* a block comment /* nested */\n   over two lines */", ""),
    (":q", ""),
]


ACC = """\
export I64 total = 0;

export I64 add(I64 n) {
    total = total + n;
    return total;
}

export void show() {
    "total {total}\\n";
}

export I64 rate() {
    return %d;
}
"""
# A session against B1 (--cint): what each kind of entry writes, state across revisions (I6),
# positions in entries and in :type, and a fault report. <revision> stands for the revision identity.
SESSION_ENTRIES = """\
I64 x = 40;
x + 2
x = x * 3;
x
export I64 twice(I64 n) {
    return n * 2;
}
x
twice(x)
:run twice(21) 2
:type x > 3
:inspect x /hex
:type x + true
x + true
:load lib/acc.ci
acc.add(5)
acc.show()
"no newline";
I64 y = x / 0;
y
U8 x = 200;
x
:history x
:log
"""
SESSION_SAID = """\
cint workbench   profile cint-core-1   backend cpu-sir-interp   new session
ci[1]> I64 x = 40;
x = 40 : I64
ci[2]> x + 2
= 42 : I64
ci[3]> x = x * 3;
ci[4]> x
= 120 : I64
ci[5]> export I64 twice(I64 n) {
..>     return n * 2;
..> }
twice : (I64) -> I64   revision session@r2
ci[6]> x
= 120 : I64
ci[7]> twice(x)
= 240 : I64
ci[8]> :run twice(21) 2
= 42 : I64
= 42 : I64
ci[10]> :type x > 3
x > 3 : Bool
ci[10]> :inspect x /hex
x : I64   (signed, 64 bits)
  exact   120
  hex     0x0000000000000078
ci[10]> :type x + true
error[C2001]: type mismatch
  --> inspect:1:5
   |
 1 | x + true
   |     ^
   |
ci[10]> x + true
error[C2001]: type mismatch
  --> entry 10:1:5
   |
 1 | x + true
   |     ^
   |
ci[11]> :load lib/acc.ci
loaded lib.acc (lib/acc.ci)
ci[12]> acc.add(5)
= 5 : I64
ci[13]> acc.show()
total 5
ci[14]> "no newline";
no newline
ci[15]> I64 y = x / 0;
fault[E_DIV_ZERO]: division by zero
  --> entry 15:1:11
   |
 1 | I64 y = x / 0;
   |           ^ div.checked.i64
   |
   = operation: div.checked.i64
   = operand-count: 2
   = operand: I64 120
   = operand: I64 0
   = exact:   none
   = limit:   none
   = revision: <revision>
   = source-map: none
   = address: none
   = stack-depth: 0
   = fuel-consumed: 1 (not part of the fault record)
   = state: no store completed in this entry; context reset
   = noncanonical: not part of the fault record
   = workbench-position: entry 15
   = site: wb_entry#4
   = backend: cpu-sir-interp
ci[16]> y
= 0 : I64
ci[17]> U8 x = 200;
x = 200 : U8
ci[18]> x
= 200 : U8
ci[19]> :history x
r1  entry 1
r4  entry 17
ci[19]> :log
entry 1     declared       I64 x = 40;
entry 2     value          x + 2
entry 3     done           x = x * 3;
entry 4     value          x
entry 5     declared       export I64 twice(I64 n) { ...
entry 6     value          x
entry 7     value          twice(x)
entry 8     value          :run twice(21) 2
entry 9     value          :run twice(21) 2
entry 10    compile error  x + true
entry 11    loaded         :load lib/acc.ci
entry 12    value          acc.add(5)
entry 13    done           acc.show()
entry 14    done           "no newline";
entry 15    fault          I64 y = x / 0;
entry 16    value          y
entry 17    declared       U8 x = 200;
entry 18    value          x
ci[19]> 
"""

# Unit 2b against B1: checkpoints, :diff, watches that report and stop :run, :restore with its
# automatic checkpoint, :undo, branches, :history with checkpoints, and :save.
HISTORY_ENTRIES = """\
I64 x = 1;
:checkpoint one
x = x + 10;
export I64 twice(I64 n) {
    return n * 2;
}
:watch x
:watch twice(x) when x > 30 stop
x = x * 2;
:watches
:checkpoint two
:diff one
:diff two
:diff one twice
export void bump() {
    x = x + 1;
}
:run bump() 30
:watch log 1
:restore one
x
:branches
:undo
:undo
:history x
:history twice
:load lib/acc.ci
acc.add(4)
:checkpoint three
:branch side
I64 y = x / 0;
:restore three
:fault
:diff two
:branches
:save saved
:quit
"""
FAULT_Y = """\
fault[E_DIV_ZERO]: division by zero
  --> entry 18:1:11
   |
 1 | I64 y = x / 0;
   |           ^ div.checked.i64
   |
   = operation: div.checked.i64
   = operand-count: 2
   = operand: I64 1
   = operand: I64 0
   = exact:   none
   = limit:   none
   = revision: <revision>
   = source-map: none
   = address: none
   = stack-depth: 0
"""
BRANCHES = """\
  main   checkpoints 5   head three
* side   from three   checkpoints 1   head three
#1  one   main   after entry 1
#2  two   main   after entry 4   follows one
#3  auto-1   main   after entry 14   follows two
#4  auto-2   main   after entry 15   follows one
#5  three   main   after entry 17   follows one
#6  auto-3   side   after entry 18   follows three
"""
HISTORY_SAID = """\
cint workbench   profile cint-core-1   backend cpu-sir-interp   new session
ci[1]> I64 x = 1;
x = 1 : I64
ci[2]> :checkpoint one
checkpoint one   #1   bindings 1   revisions: session@r1
ci[2]> x = x + 10;
ci[3]> export I64 twice(I64 n) {
..>     return n * 2;
..> }
twice : (I64) -> I64   revision session@r2
ci[4]> :watch x
watch 1   x = 11 : I64
ci[4]> :watch twice(x) when x > 30 stop
watch 2   twice(x) = 22 : I64   when x > 30   stop
ci[4]> x = x * 2;
watch 1   entry 4   x = 22
ci[5]> :watches
watch 1   x : I64
watch 2   twice(x) : I64   when x > 30   stop
ci[5]> :checkpoint two
checkpoint two   #2   bindings 1   revisions: session@r2
ci[5]> :diff one
binding   status      differing   first canonical difference
x         differs     1           22 here, 1 in one
ci[5]> :diff two
no binding differs from two (1 compared)
ci[5]> :diff one twice
no binding twice here or in one
ci[5]> export void bump() {
..>     x = x + 1;
..> }
bump : () -> void   revision session@r3
ci[6]> :run bump() 30
""" + "".join("watch 1   entry %d   x = %d\n" % (e, e + 17) for e in range(6, 15)) + """\
watch 2   entry 14   twice(x) = 62
a watch stops :run: 9 of 30 calls made
ci[15]> :watch log 1
entry 4   22
""" + "".join("entry %d   %d\n" % (e, e + 17) for e in range(6, 15)) + """\
ci[15]> :restore one
saved current state as auto-1
restored one   #1   twice is not declared   bump is not declared
error[C3005]: undefined or unusable name
  --> watch 2:1:1
   |
 1 | twice(x)
   | ^
   |
watch 2 is removed: it no longer compiles
ci[15]> x
= 1 : I64
ci[16]> :branches
* main   checkpoints 3   head one
#1  one   main   after entry 1
#2  two   main   after entry 4   follows one
#3  auto-1   main   after entry 14   follows two
ci[16]> :undo
saved current state as auto-2
back to the state before entry 15
ci[16]> :undo
nothing to undo
ci[16]> :history x
r1  entry 1   in checkpoints: one, two, auto-1, auto-2
ci[16]> :history twice
r2  entry 3   in checkpoints: two, auto-1
ci[16]> :load lib/acc.ci
loaded lib.acc (lib/acc.ci)
ci[17]> acc.add(4)
= 4 : I64
ci[18]> :checkpoint three
checkpoint three   #5   bindings 1   revisions: session@r1, acc@r1
ci[18]> :branch side
current state equals checkpoint three; no automatic checkpoint
branch side from three
ci[18]> I64 y = x / 0;
""" + FAULT_Y + """\
   = fuel-consumed: 1 (not part of the fault record)
   = state: no store completed in this entry; context reset
   = noncanonical: not part of the fault record
   = workbench-position: branch side, entry 18
   = site: wb_entry#2
   = backend: cpu-sir-interp
ci[19]> :restore three
saved current state as auto-3
restored three   #5   y is not declared
ci[19]> :fault
no fault in this session
ci[19]> :diff two
binding     status      differing   first canonical difference
acc.total   only here   1           4 here, not in two
x           differs     1           1 here, 22 in two
ci[19]> :branches
""" + BRANCHES + """\
ci[19]> :save saved
saved session to saved   entries 18   checkpoints 6   objects <n>
ci[19]> :quit
"""
# The saved session in a new process, after lib/acc.ci changed on disk, and before it two copies
# that :open refuses: one whose manifest has no last line, and one with an object that does not
# match its name.
REOPEN_ENTRIES = """\
:open cut
:open bad
:open saved
:branches
:watches
x
:undo
:restore auto-3
:fault
:quit --yes
"""
DIFFERS = "lib/acc.ci differs on disk from the restored revision; :reload reads it\n"
REOPEN_SAID = """\
cint workbench   profile cint-core-1   backend cpu-sir-interp   new session
ci[1]> :open cut
cut/session.manifest is incomplete: a save that did not finish; nothing changed
ci[1]> :open bad
:open: bad/session.manifest is not a CINT-SESSION-1 session this workbench reads (line <n>); nothing changed
ci[1]> :open saved
opened saved   entries 18   checkpoints 6   branch side
""" + DIFFERS + """\
ci[19]> :branches
""" + BRANCHES + """\
ci[19]> :watches
watch 1   x : I64
ci[19]> x
= 1 : I64
ci[20]> :undo
saved current state as auto-4
back to the state before entry 19
""" + DIFFERS + """\
ci[20]> :restore auto-3
saved current state as auto-5
restored auto-3   #6   y is revision session@r4
""" + DIFFERS + """\
ci[20]> :fault
""" + FAULT_Y + """\
   = state: restored with the checkpoint (SPEC-01 IM-121)
   = noncanonical: not part of the fault record
   = workbench-position: a restored checkpoint
   = site: wb_entry#2
   = backend: cpu-sir-interp
ci[20]> :quit --yes
"""

def transcript(said: str) -> str:
    """A pattern for a transcript in which <revision> is a revision identity, <h> the first four
    bytes of a state hash, and <n> a count."""
    return "^" + re.escape(said).replace(re.escape("<revision>"), "[0-9a-f]{64}").replace(
        re.escape("<h>"), "[0-9a-f]{8}").replace(re.escape("<n>"), "[0-9]+") + "$"


class WorkbenchLeg(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.leg = ARGS.leg or ("msvc" if os.name == "nt" else
                               "apple-clang" if sys.platform == "darwin" else "gcc")
        cls.out = pathlib.Path(ARGS.out) if ARGS.out else default_out(cls.leg)

    def assertClean(self, result, what):
        err = result.stderr.decode("utf-8", "replace")
        self.assertNotIn("runtime error", err, what)
        self.assertNotIn("Sanitizer", err, what)

    def test_fixtures_are_complete(self):
        names = cases()
        self.assertGreaterEqual(len(names), 7)
        for name in names:
            for suffix in (".stderr", ".json", ".rec.hex"):
                self.assertTrue((REPORT / (name + suffix)).is_file(), name + suffix)

    def test_builds_and_runs(self):
        for config, opt, sanitized in configurations(self.leg):
            with self.subTest(config=config):
                outdir = self.out / config
                if outdir.exists():
                    shutil.rmtree(outdir)
                outdir.mkdir(parents=True)
                exes = build(self.leg, opt, sanitized, outdir)
                result = run([exes["test_wb"]])
                self.assertClean(result, "test_wb")
                self.assertEqual(result.returncode, 0, result.stdout.decode("utf-8", "replace"))
                summary = result.stdout.decode("utf-8").strip().splitlines()[-1]
                self.assertTrue(summary.startswith("pass test_wb:"), summary)
                checked = (self.check_reports(exes["wb-report"]) + self.check_commands(exes)
                           + self.check_explain(exes["cint-explain"])
                           + sum(unit.check(self, sys.modules[__name__], exes) for unit in units()))
                print(f"[{self.leg} {config}] {summary}; {checked} report, command, and explain checks", flush=True)

    def check_reports(self, report) -> int:
        checked = 0
        for name in cases():
            with self.subTest(case=name):
                record = REPORT / (name + ".rec.hex")
                want_text = (REPORT / (name + ".stderr")).read_bytes()
                want_json = (REPORT / (name + ".json")).read_bytes()
                fields = json.loads(want_json)
                fuel = fields["noncanonical"]["fuel_consumed"]
                text = run([report, "--fuel", fuel, "--state", STATE, "--source", name + ".ci",
                            REPORT / (name + ".ci"), record])
                self.assertClean(text, name)
                self.assertEqual((text.returncode, text.stderr), (0, b""), name)
                self.assertEqual(text.stdout[:len(want_text)], want_text, name)
                self.assertEqual(text.stdout[len(want_text):], stack_lines(name, fields["stack"]), name)
                js = run([report, "--json", "--fuel", fuel, record])
                self.assertClean(js, name)
                self.assertEqual((js.returncode, js.stdout, js.stderr), (0, want_json, b""), name)
                # The workbench's additions go under `noncanonical` and leave the rest as it was.
                extra = run([report, "--json", "--fuel", fuel, "--site", "f#2", "--position", "branch 0, entry 3",
                             "--backend", "cpu-c17", record])
                self.assertEqual(extra.returncode, 0, name)
                got = json.loads(extra.stdout)
                self.assertEqual(extra.stdout, json.dumps(got, sort_keys=True, separators=(",", ":"),
                                                          ensure_ascii=True).encode("ascii") + b"\n")
                self.assertEqual(got.pop("noncanonical"),
                                 {"backend": "cpu-c17", "fuel_consumed": fuel, "site": "f#2",
                                  "workbench_position": "branch 0, entry 3"})
                del fields["noncanonical"]
                self.assertEqual(got, fields, name)
                # A record cut short is refused, and a directory is not a readable record file.
                cut = self.out / (name + ".cut.hex")
                cut.write_bytes(record.read_bytes().strip()[:-2])
                self.assertEqual(run([report, cut]).returncode, 1, name)
                self.assertEqual(run([report, WB]).returncode, 5, name)
                checked += 5
        return checked

    def check_commands(self, exes) -> int:
        usage = {}
        for name in EXECUTABLES:
            result = run([exes[name], "--help"])
            self.assertClean(result, name)
            self.assertEqual((result.returncode, result.stderr), (0, b""), name)
            self.assertTrue(result.stdout.startswith(b"usage: cint "), name)
            self.assertTrue(result.stdout.endswith(b"\n"), name)
            usage[name] = result.stdout
        self.assertEqual(usage["cint-repl"], usage["cint-workbench"])
        nocint = b"cannot find no-cint; give --cint PATH"
        plain = [  # (executable, arguments, exit status, stderr)
            ("cint-workbench", ["--cint", "no-cint"], 5, b"cint workbench: error: " + nocint + b"\n"),
            ("cint-repl", ["--cint", "no-cint", "main.ci"], 5, b"cint repl: error: " + nocint + b"\n"),
            ("cint-workbench", ["a.ci", "b.ci"], 4,
             b"cint workbench: error: the session takes the options of --help and one FILE.ci or DIR\n"),
            ("cint-workbench", ["--root", "a", "b"], 4, b"cint workbench: error: --root and a root DIR exclude each other\n"),
            ("cint-repl", ["--out"], 4, b"cint repl: error: an option needs one value and is given at most once\n"),
            ("cint-explain", [], 4, usage["cint-explain"]
             + b"cint explain: error: explain takes FILE.ci or FILE.ci:NAME\n"),
            ("cint-explain", ["main.ci:f"], 4, b"cint explain: error: cannot read main.ci under .\n"),
            ("cint-explain", ["--sir", SIR, "--out", "out", "main.ci"], 4,
             b"cint explain: error: --out and --sir exclude each other\n"),
            ("cint-debug", [], 4, usage["cint-debug"]
             + b"cint debug: error: debug takes a recording, a case, or one of diff, replay, and locate\n"),
            ("cint-debug", ["locate", "main.ci"], 4, b"cint debug: error: cannot read main.ci under .\n"),
        ]
        for name, argv, status, err in plain:
            result = run([exes[name], *argv], cwd=self.out)
            self.assertClean(result, name)
            self.assertEqual((result.returncode, result.stdout, result.stderr), (status, b"", err), (name, argv))
        tool = [  # under --json: one CINT-TOOL-1 line, canonical, and nothing else
            ("cint-explain", [], 4, "usage", "explain takes FILE.ci or FILE.ci:NAME"),
            ("cint-workbench", ["--cint", "no-cint", "main.ci"], 5, "environment", nocint.decode()),
            ("cint-debug", ["replay", "run.rec"], 5, "environment", "cannot read run.rec"),
        ]
        for name, argv, status, category, message in tool:
            result = run([exes[name], "--json", *argv])
            self.assertClean(result, name)
            line = json.dumps({"category": category, "exit_status": status, "message": message,
                               "schema": "CINT-TOOL-1"}, sort_keys=True, separators=(",", ":"))
            self.assertEqual((result.returncode, result.stdout, result.stderr),
                             (status, b"", line.encode("ascii") + b"\n"), (name, argv))
        return len(EXECUTABLES) + len(plain) + len(tool) + self.check_session(exes["cint-workbench"])

    def check_session(self, workbench) -> int:
        """The session's loop and the commands that compile nothing, with any file as `cint`."""
        with tempfile.TemporaryDirectory() as tmp:
            argv = [workbench, "--cint", workbench, "--out", pathlib.Path(tmp) / "session", "--echo"]
            result = run(argv, cwd=tmp, entries=":help\n:quit\n")
            self.assertClean(result, "session :help")
            self.assertEqual((result.returncode, result.stderr), (0, b""))
            lines = result.stdout.decode("utf-8").split("\n")
            self.assertEqual(lines[:3], [SESSION_BANNER, "ci[1]> :help", "Entries: a declaration (I64 x = 1;), a "
                                         "statement (x = x + 1;), or an expression (x * 2)."])
            for name in ("load", "reload", "run", "inspect", "type", "explain", "fault", "fuel", "depth", "backend",
                         "history", "log", "checkpoint", "restore", "undo", "branch", "branches", "diff", "watch",
                         "watches", "goto", "trace", "replay", "effects", "save", "open"):
                self.assertTrue(any(line.startswith("  :" + name + " ") for line in lines), name)
            self.assertEqual(lines[-2:], ["ci[1]> :quit", ""])
            result = run(argv, cwd=tmp, entries="".join(entry + "\n" for entry, _ in SESSION_PLAIN))
            self.assertClean(result, "session")
            want = SESSION_BANNER + "\n" + "".join("ci[1]> %s\n%s" % (entry.replace("\n", "\n..> "), said)
                                                   for entry, said in SESSION_PLAIN)
            self.assertEqual((result.returncode, result.stdout.decode("utf-8"), result.stderr), (0, want, b""))
            # The end of the input ends the session as :quit does, after an empty prompt line.
            result = run(argv, cwd=tmp, entries=":fuel 3\n")
            self.assertEqual((result.returncode, result.stdout.decode("utf-8")),
                             (0, SESSION_BANNER + "\nci[1]> :fuel 3\nfuel: 3 units per entry\nci[1]> \n"))
            checked = 3
            if ARGS.cint is not None:
                checked += self.check_session_end_to_end(workbench, pathlib.Path(ARGS.cint).resolve(), pathlib.Path(tmp))
        return checked

    def check_session_end_to_end(self, workbench, cint, tmp) -> int:
        """Sessions that B1 compiles: a transcript, and :reload of a module changed on disk."""
        root = tmp / "root"
        (root / "lib").mkdir(parents=True)
        (root / "lib" / "acc.ci").write_text(ACC % 1, encoding="utf-8", newline="\n")
        argv = [workbench, "--cint", cint, "--root", root, "--out", tmp / "e2e", "--echo"]
        result = run(argv, entries=SESSION_ENTRIES)
        self.assertClean(result, "session end to end")
        self.assertEqual((result.returncode, result.stderr), (0, b""))
        want = re.escape(SESSION_SAID).replace(re.escape("<revision>"), "[0-9a-f]{64}")
        self.assertRegex(result.stdout.decode("utf-8"), "^" + want + "$")
        # :reload reads the module again; its variable keeps its value across the new revision.
        changed = lambda proc: (root / "lib" / "acc.ci").write_text(ACC % 2, encoding="utf-8", newline="\n")
        out, err, status = converse(argv[:-1] + ["lib/acc.ci"], tmp, [
            ("acc.rate()", "= 1 : I64\n"), ("acc.add(7)", "= 7 : I64\n"), (changed, ""),
            (":reload", "reloaded lib.acc (lib/acc.ci)\n"), ("acc.rate()", "= 2 : I64\n"),
            ("acc.total", "= 7 : I64\n"), (":quit", UNSAVED)])
        self.assertEqual((status, err), (0, b""), out)
        self.assertTrue(out.startswith((SESSION_BANNER + "\nloaded lib.acc (lib/acc.ci)\nci[2]> ").encode()), out)
        # No cint where it is looked for.
        result = run([workbench, "--cint", tmp / "no-cint", "--root", root])
        self.assertEqual((result.returncode, result.stdout), (5, b""))
        return 3 + self.check_history(workbench, cint, tmp) + self.check_interrupt(workbench, cint, tmp)

    def check_history(self, workbench, cint, tmp) -> int:
        """Unit 2b: checkpoints, watches, :undo, branches, and :save, then :open in a new process."""
        work = tmp / "history"
        (work / "root" / "lib").mkdir(parents=True)
        (work / "root" / "lib" / "acc.ci").write_text(ACC % 1, encoding="utf-8", newline="\n")
        argv = [workbench, "--cint", cint, "--root", work / "root", "--echo"]
        result = run(argv + ["--out", work / "session-1"], cwd=work, entries=HISTORY_ENTRIES)
        self.assertClean(result, "history")
        self.assertEqual((result.returncode, result.stderr), (0, b""))
        self.assertRegex(result.stdout.decode("utf-8"), transcript(HISTORY_SAID))
        manifest = (work / "saved" / "session.manifest").read_bytes()
        self.assertTrue(manifest.startswith(b"CINT-SESSION-1\n") and manifest.endswith(b"\nend\n"))
        for name in ("cut", "bad"):
            shutil.copytree(work / "saved", work / name)
        (work / "cut" / "session.manifest").write_bytes(manifest[:-len(b"end\n")])
        listed = re.search(rb"^object ([0-9a-f]{64})$", manifest, re.M).group(1).decode()
        damaged = work / "bad" / "objects" / listed
        damaged.write_bytes(damaged.read_bytes() + b" ")
        (work / "root" / "lib" / "acc.ci").write_text(ACC % 2, encoding="utf-8", newline="\n")
        result = run(argv + ["--out", work / "session-2"], cwd=work, entries=REOPEN_ENTRIES)
        self.assertClean(result, "reopen")
        self.assertEqual((result.returncode, result.stderr), (0, b""))
        self.assertRegex(result.stdout.decode("utf-8"), transcript(REOPEN_SAID))
        return 2

    def check_interrupt(self, workbench, cint, tmp) -> int:
        """An interrupt caps the running entry's allowance at its consumption, so the entry faults
        E_FUEL, and it ends a :run between calls (BX14-10). POSIX only: the test sends SIGINT."""
        if os.name == "nt":
            return 0
        (tmp / "spin").mkdir()
        argv = [workbench, "--cint", cint, "--root", tmp / "spin", "--out", tmp / "spin-session", "--echo"]
        stop, wait = (lambda proc: proc.send_signal(signal.SIGINT)), (lambda proc: None)
        capped = "interrupted: the allowance of this entry was capped at the "
        out, err, status = converse(argv, tmp, [
            ("I64 n = 0;", "n = 0 : I64\n"),
            ('export void spin() {\n    n = n + 1;\n    "spinning\\n";\n    while (true) {\n        n = n + 1;\n    }\n}',
             "spin : () -> void   revision session@r2\n"),
            ("spin();", "spinning\n"), (stop, capped), (wait, "fault[E_FUEL]: fuel allowance exhausted\n"),
            ("n > 0", "= true : Bool\n"),
            (":run spin() 3", "spinning\n"), (stop, capped), (wait, "1 of 3 calls made\n"),
            (":undo", "back to the state before entry 5\n"), (":quit --yes", "")])
        self.assertEqual((status, err), (0, b""), out)
        return 1

    def check_explain(self, explain) -> int:
        k, c = bridge_constant("CINT_PHASE_FUEL_K"), bridge_constant("CINT_PHASE_FUEL_C")
        recorded, seen, checked, effects, derived = revisions(), set(), 0, set(), set()
        for case in sir_cases():
            modules = sir_modules(case)
            sizes = {m: (CONFORMANCE / m).stat().st_size for m in modules if (CONFORMANCE / m).is_file()}
            for module in modules:
                with self.subTest(case=case.name, module=module):
                    result = run([explain, "--json", "--sir", case, "--root", CONFORMANCE, module])
                    self.assertClean(result, module)
                    self.assertEqual((result.returncode, result.stderr), (0, b""), module)
                    got = json.loads(result.stdout)
                    self.assertEqual(result.stdout, canonical(got), module)
                    self.assertEqual((got["schema"], got["module"], got["revision"]),
                                     ("CINT-EXPLAIN-1", module, recorded[case.name]))
                    want = explained(case, module)
                    codes_of = {i: c for f in sir_functions(case, module) for i, c in f["_codes"].items()}
                    effects.update(f["effects"] for f in want)
                    bare = [dict({k: v for k, v in f.items() if k not in KERNEL_KEYS},   # wbt_arrays.py checks them
                                 fault_sites=[{key: s[key] for key in ("id", "operation", "position")}
                                              for s in f["fault_sites"]]) for f in got["functions"]]
                    self.assertEqual(bare, want, module)
                    for site in (s for f in got["functions"] for s in f["fault_sites"]):
                        op, codes = site["operation"], site["codes"]
                        self.assertEqual(codes, [name for name in WB_CODES if name in codes], site["id"])
                        if site["status"] == "possible":
                            self.assertTrue(codes, (module, site["id"], op))   # every operation is known
                            self.assertEqual(site["reason"], "not analyzed", site["id"])
                        else:
                            self.assertEqual((site["status"], codes, site["reason"]),
                                             ("excluded", [], "an unsigned operand is never negative (SPEC-01 IM-52)"))
                        if site["id"] in codes_of:
                            self.assertEqual(codes, codes_of[site["id"]], (module, site["id"]))
                            derived.add(op.split(".")[0] if op.startswith(("sum.", "fold_checked.", "reduce_")) else op)
                        elif op in CODES:
                            self.assertEqual(codes, CODES[op], (module, site["id"]))
                            seen.add(op)
                    build = got["build"]
                    entries = [dict({"module": m}, **({"bytes": sizes[m], "units": k * sizes[m] + c} if m in sizes else {}))
                               for m in modules]
                    fuel = {"c": c, "k": k, "modules": entries}
                    if len(sizes) == len(modules):
                        fuel["plan"] = {"bytes": sum(sizes.values()), "units": k * sum(sizes.values()) + c}
                    self.assertEqual(build["compile_fuel"], fuel, module)
                    self.assertEqual(build["program"]["modules"], len(modules))
                    self.assertEqual(build["program"]["functions"], sum(len(sir_functions(case, m)) for m in modules))
                    self.assertIn("(SPEC-09 CINTC-02)", build["memory"])
                    # The text form: the same sites, the same codes, and the same SIR text.
                    plain = run([explain, "--sir", case, "--root", CONFORMANCE, module])
                    self.assertClean(plain, module)
                    self.assertEqual((plain.returncode, plain.stderr), (0, b""), module)
                    text = plain.stdout.decode("utf-8")
                    self.assertTrue(text.startswith("module %s   revision %s\n" % (module, got["revision"])), module)
                    self.assertTrue(text.split("\n")[1].startswith("program   "), module)
                    for f in got["functions"]:
                        self.assertIn("\n  sir\n" + f["sir"], text, module)
                        for site in f["fault_sites"]:
                            position = "%s:%d:%d" % (module, site["position"]["line"], site["position"]["column"])
                            line = re.search(r"^    %s +%s +%s {2,}(\S.*)$" % (
                                re.escape(site["id"]), re.escape(position), re.escape(site["operation"])), text, re.M)
                            self.assertIsNotNone(line, (module, site["id"]))
                            self.assertEqual(line.group(1), " ".join(site["codes"]) + " possible" if site["codes"]
                                             else "excluded: " + site["reason"], site["id"])
                    checked += 2
        self.assertEqual(sorted(set(CODES) - seen), [], "operations of the code table no fixture has")
        self.assertEqual(sorted(derived), ["dispatch", "dispatch.entry", "fold_checked", "reduce_min", "sum"])
        self.assertEqual(effects, {"observe", "pure"})
        return checked + self.check_explain_forms(explain)

    def check_explain_forms(self, explain) -> int:
        case, module = SIR / "module__import_plain", "module/lib/plain_math.ci"
        revision = revisions()[case.name]
        sir = explained(case, module)[0]["sir"]
        want = (f"module {module}   revision {revision}\n\n" + ADD3 + sir).encode("utf-8")
        forms = [  # (arguments, working directory): --root, a longer path, and the module's own path
            (["--sir", case, "--root", CONFORMANCE, module + ":add3"], None),
            (["--sir", case, "conformance/" + module + ":add3"], ROOT),
            (["--sir", case, module + ":add3"], CONFORMANCE),
        ]
        for argv, cwd in forms:
            result = run([explain, *argv], cwd=cwd)
            self.assertClean(result, argv)
            self.assertEqual((result.returncode, result.stdout, result.stderr), (0, want, b""), argv)
        errors = [  # (arguments, exit status, category, message)
            ([module + ":nosuch"], 4, "usage", module + " has no function nosuch"),
            (["other/none.ci"], 4, "usage", "the program has no module other/none.ci"),
        ]
        for argv, status, category, message in errors:
            for json_mode in (False, True):
                result = run([explain, *(["--json"] if json_mode else []), "--sir", case, "--root", CONFORMANCE, *argv])
                self.assertClean(result, argv)
                err = (canonical({"category": category, "exit_status": status, "message": message,
                                  "schema": "CINT-TOOL-1"}) if json_mode
                       else b"cint explain: error: " + message.encode("utf-8") + b"\n")
                self.assertEqual((result.returncode, result.stdout, result.stderr), (status, b"", err), argv)
        missing = run([explain, "--sir", self.out / "no-such-directory", "--root", CONFORMANCE, module])
        self.assertEqual((missing.returncode, missing.stdout), (5, b""))
        self.assertTrue(missing.stderr.startswith(b"cint explain: error: the SIR text under "), missing.stderr)
        checked = len(forms) + 2 * len(errors) + 1
        if ARGS.cint is not None:
            checked += self.check_explain_end_to_end(explain, pathlib.Path(ARGS.cint).resolve())
        return checked

    def check_explain_end_to_end(self, explain, cint) -> int:
        """Explain compiles a program with `cint emit-c` and reads the SIR text it wrote."""
        with tempfile.TemporaryDirectory() as tmp:
            out = pathlib.Path(tmp) / "explain"
            argv = ["--root", CONFORMANCE, "module/import_plain.ci"]
            result = run([explain, "--json", "--cint", cint, "--out", out, *argv])
            self.assertClean(result, "end to end")
            self.assertEqual((result.returncode, result.stderr), (0, b""), "end to end")
            self.assertEqual(run([explain, "--json", "--sir", out, *argv]).stdout, result.stdout)
            got = json.loads(result.stdout)
            self.assertEqual([f["name"] for f in got["functions"]], ["run"])
            self.assertEqual([m["module"] for m in got["build"]["compile_fuel"]["modules"]],
                             ["module/lib/plain_math.ci", "module/import_plain.ci"])
            # A compile error: cint's own diagnostics on stderr and its exit status.
            bad = pathlib.Path(tmp) / "bad.ci"
            bad.write_bytes(b"fn broken( {\n")
            result = run([explain, "--cint", cint, bad])
            self.assertEqual((result.returncode, result.stdout), (2, b""))
            self.assertNotEqual(result.stderr, b"")
            self.assertFalse(result.stderr.startswith(b"cint explain:"), result.stderr)
            # No cint where it is looked for.
            result = run([explain, "--cint", pathlib.Path(tmp) / "no-cint", "--out", out, *argv])
            self.assertEqual((result.returncode, result.stdout), (5, b""))
            self.assertTrue(result.stderr.startswith(b"cint explain: error: cannot run "), result.stderr)
        return 4

    def test_cint_still_prints_the_fixtures(self):
        if ARGS.cint is None:
            self.skipTest("--cint not given")
        cint = pathlib.Path(ARGS.cint).resolve()
        for name in cases():
            with self.subTest(case=name), tempfile.TemporaryDirectory() as tmp:
                work = pathlib.Path(tmp)
                shutil.copyfile(REPORT / (name + ".ci"), work / (name + ".ci"))
                for mode, want in (([], ".stderr"), (["--json"], ".json")):
                    cache = work / ("cache" + "".join(mode))
                    result = run([cint, *mode, "run", "--cache", cache, name + ".ci"], cwd=work)
                    self.assertEqual(result.returncode, 1, name)
                    self.assertEqual(result.stderr, (REPORT / (name + want)).read_bytes(), name)
                    records = sorted(cache.rglob("fault.bin"))
                    self.assertEqual(len(records), 1, name)
                    self.assertEqual(records[0].read_bytes().hex(),
                                     (REPORT / (name + ".rec.hex")).read_text(encoding="ascii").strip(), name)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--leg", choices=["msvc", "gcc", "clang", "apple-clang"])
    parser.add_argument("--out")
    parser.add_argument("--cint")
    known, rest = parser.parse_known_args()
    ARGS.leg, ARGS.out, ARGS.cint = known.leg, known.out, known.cint
    unittest.main(argv=[sys.argv[0], "-v", *rest])


if __name__ == "__main__":
    main()
