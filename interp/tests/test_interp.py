"""Build cpu-sir-interp (interp/), its SIR text reader and its interpreter, and test them on one leg.

Usage (from the repository root):
    python interp/tests/test_interp.py [--leg msvc|gcc|clang|apple-clang] [--out DIR]
                                       [--fuzz N] [--seed S] [--b1 PATH | --bootstrap]
                                       [--no-sanitize]

Box 13 units 1 and 2 (docs/design/notes/2026-10-05-box13-interpreter-checkpoints.md,
section 9; SPEC-09 BACK-01a, SIR-09a, SIR-10, SIR-17, CONF-10a). Each leg builds
interp/tests/sir_check.c with interp/sir_read.c and rt/cint_rt.c, and cint-interp
(interp/cint_interp_main.c, which compiles harness/cint_harness.c in, with interp/sir_read.c,
interp/sir_exec.c and rt/cint_rt.c), through the toolchain of tools/cint_check.py (the
warning flags of SPEC-09 EMIT-26 and -Werror, /WX): at -O2 (/O2), and on the Linux legs
also at -O0 with -fsanitize=address,undefined (--no-sanitize leaves that build out, on a
host without the sanitizer runtime). Build outputs go under
<build>/interp/<leg>/, never inside the repository (<build> as tools/cint_check.py
chooses it). Each build is checked on:

  - the SIR-17 golden test: compiler/tests/golden/add.sir has the revision identity that
    compiler/tests/golden_suite.py fixes (ADD_REVISION), and hello.sir the one in
    hello.program.c;
  - the programs under interp/tests/sir/, for kernels (unit 7b) interp/tests/kernels/, for
    error sets and error results (unit 8b) interp/tests/errors/, and for extern rows, Grants
    parameters and service calls (I/O services lane U5c) interp/tests/services/, SIR text
    that B1 wrote for conformance cases (each directory is a case, `<category>__<name>`): the revision
    identity equals the one cintc wrote in the program file, kept in the REVISIONS of its
    directory, and the one computed here in Python from the same bytes; the counts of
    modules, functions and nodes equal the ones counted here;
  - malformed texts, each an edit of one of those programs: the reader reports the
    position and the rule (MALFORMED below);
  - a seeded fuzz pass: N random edits of those programs (--fuzz, default 200), each
    either read or reported as an input error or a form not run yet, never a crash, a
    resource error, or a sanitizer report (SPEC-09 9.7); an edit that reads is also run by cint-interp under a
    fuel allowance (fuzz_fuel), and gives an outcome or a refusal, never a crash, and the same
    output with `--trace`, under which every node runs itself rather than its fast form
    (optimization unit O9b); the seed and count are printed;
  - execution: each of those programs runs on cint-interp as the case header
    of its conformance case says, and the output equals the frozen .expect: `observe`
    with the entry, arguments, fuel and depth prints the whole .expect text, and `run`, for
    a script or test entry, gives the stdout, fuel record and fault record of its body,
    structs included (the struct lines of box 13 unit 3), with and without `--trace`;
    `--trace` writes one event per counted operation, call and return and changes nothing it
    observes; `run` refuses a malformed command line;
  - wide integers (box 15): the programs under interp/tests/wide/, one program over I128 and I256
    and the same over I512 and I1024, with the SIR text B1 wrote for them, read with the
    revision identity and counts computed here, and each of their calls (wide_cases) gives
    cint_ref's output, with and without `--trace`;
  - checkpoints (box 13 unit 4, interp/tests/state_host.c) and recordings and replay (unit 5,
    interp/tests/rec_host.c on the program interp/tests/rec/ledger.ci, whose SIR text B1
    wrote into interp/tests/rec/sir/): `show`, `replay` with its stop boundary, checkpoint
    and receipt, a recording altered at one boundary, a change of revision, and
    `run --record`.

With --b1 PATH (a B1 `cint`) or --bootstrap (one built by tools/cint_bootstrap.py), every
conformance program B1 emits and cintc's own sources are also read, and each revision
identity is compared with the program file's `cg_revision`, the
programs under interp/tests/probes/ (box 09 forms the conformance suite leaves out: a
reversed view, a rank-2 parameter, slice bounds and initial values that are parameters,
rank 3, a charged array in a recursion) run on cint-interp from B1's SIR text with the
outcome cint_ref gives (box 13 unit 7), and so do the calls of compiler/tests/wide_reductions.ci
and the scripts wide_print.ci and wide_assert.ci there (box 15 forms no case holds), and the checkpoint
and recording hosts are also linked with B1's C of their programs, so each backend restores
the other's checkpoints and replays the other's recordings, and the checkpoint host is
linked with B1's C of interp/tests/mem/arena_state.ci, whose arenas and pools a checkpoint
holds and a restore brings back (unit 8a), and with B1's C of
interp/tests/mem/wide_struct_state.ci, whose module-level structs hold wide fields (box 15), and
the recording host records the program entry of
interp/tests/density/ at `dispatch` density, the same bytes on both backends (unit 7c). The
script is a unittest module; it uses only the Python standard library.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
import pathlib
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import cint_check  # noqa: E402  (the toolchain of the slice matrix)

TESTS = ROOT / "interp" / "tests"
FIXTURES = TESTS / "sir"
# Kernel programs (unit 7b), apart from sir/, whose programs workbench/tests/test_workbench.py
# also explains; each directory holds its own REVISIONS.
KERNELS = TESTS / "kernels"
# Error programs (box 12 forms, unit 8b): error sets, result forms, error.tag, return.error,
# error.leave and widen; the workbench does not explain them yet.
ERRORS = TESTS / "errors"
# Service programs (I/O services lane U5c, rt/OPEN.md RT-OQ-75): extern rows, Grants parameters
# and host.call, each call denied, since no grant is given.
SERVICES = TESTS / "services"
DIRECTORIES = (FIXTURES, KERNELS, ERRORS, SERVICES)
PROBES = TESTS / "probes"
# Wide integers (box 15): one program over two signed wide types, feat128.ci over I128 and I256
# and feat512.ci over I512 and I1024, with the SIR text B1 wrote for each under <name>/; each
# name gives the bits of its smaller type and of the operands of its mul_full, which B1 takes
# up to I256.
WIDE = TESTS / "wide"
WIDE_PROGRAMS = {"feat128": (128, 128), "feat512": (512, 256)}
COMPILER_TESTS = ROOT / "compiler" / "tests"
# The calls of compiler/tests/wide_reductions.ci that compiler/tests/golden_suite.py makes (CALLS):
# each function with its argument lists.
WIDE_REDUCTIONS = (("mixed", ((5,), (-3,))), ("narrow_result", ((2,), (-2 ** 63,))), ("fold_at", ((0,), (3,))),
                   ("dot_shape", ((3,), (2,))), ("kernels", ((4,), (-2 ** 63 + 1,), (-2 ** 63,))),
                   ("kernel_empty", ((0,), (2,))), ("kernel_faults", ((0,), (1,), (2,))),
                   ("bounds", ((0, 3), (1, 1), (2, 1))), ("wrap_sat", ((),)))
GOLDEN = ROOT / "compiler" / "tests" / "golden"
# The canonical state code, with recordings and replay in rt/cint_rec.c (decision 2026-10-08 on OQ-240).
STATE_SOURCES = [ROOT / "rt" / "cint_state.c", ROOT / "rt" / "cint_rec.c"]
SOURCES = [ROOT / "interp" / "sir_read.c", TESTS / "sir_check.c", ROOT / "interp" / "sir_exec.c",
           ROOT / "interp" / "cint_interp_main.c", ROOT / "rt" / "cint_rt.c", *STATE_SOURCES]
sys.path.insert(0, str(ROOT / "conformance" / "tools"))
import gen_expect  # noqa: E402  (the case header of a conformance program)

# compiler/tests/golden_suite.py, ADD_REVISION: the reviewed SIR-17 golden value of add.ci.
ADD_REVISION = "fb64a16754f09ae94c9f6744b88de6791cf5a9dc4328e2c8a882ff36cef5c3e8"
# The T4 case (box 13 unit 6): interp/tests/rec's program entry saved by `run --case`, the same
# bytes on every machine and backend, and the head of its digest chain. The receipt of its replay
# then has one identity wherever it runs (box 13 default BX13-13). It changes with the recording
# layout, interp/tests/rec, or the canonical state encoding.
CASE_SHA256 = "172451e73c96241751a97a6440856b243d1d5fbd9c4c7edcd65ca24ca7d4d5f1"
CASE_CHAIN = "053bb993493a1a834bda2eeb5e5190dfa3d3482add38fdf8c600dcd0e583a62b"
# The runtime contract that enters every revision identity (SPEC-01 IM-157), as rt/cint_rt.h
# defines it and interp/sir_read.c uses it.
CONTRACT = re.search(rb'#define CINT_RT_CONTRACT "([^"]+)"', (ROOT / "rt" / "cint_rt.h").read_bytes()).group(1)

ARGS = argparse.Namespace(leg=None, out=None, fuzz=200, seed=None, b1=None, bootstrap=False, no_sanitize=False)
# The least fuel a fuzz input runs with (fuzz_fuel).
FUZZ_FUEL = 1000
BUILDS = []   # (label, path of sir_check) per build of the leg
INTERPS = []  # (label, path of cint-interp) per build of the leg
HOSTS = []    # (label, path of interp/tests/state_host.c on the interpreter) per build of the leg
RECS = []     # (label, path of interp/tests/rec_host.c on the interpreter) per build of the leg
REC = TESTS / "rec"
# A program whose entry dispatches three kernels (box 13 unit 7c), recorded at `dispatch` density.
DENSITY = TESTS / "density"
# What interp/tests/rec_host.c prints for its steps up to the program entry (its header).
REC_LINES = ("deposit 5\ndeposit 0 105\ncheckpoint 0\ndeposit 7\ndeposit 0 112\nburn 1 0\nclear 0\n"
             "deposit 1 0\nclear 0\nrestore 0\nledger 105\ndeposit 1\nmain 0\n")


def sir17_encoding(sir: bytes) -> bytes:
    """SIR-17's encoding of one module's SIR text (compiler/tests/golden_suite.py)."""
    lines = sir.split(b"\n")
    del lines[1]
    return b"\n".join(re.sub(rb"(#[0-9]+) @[^ ]+:[0-9]+:[0-9]+$", rb"\1", line) for line in lines)


def sir17_revision(modules) -> str:
    """SPEC-01 IM-157 over [(module path, SIR text)] (compiler/tests/golden_suite.py)."""
    text = lambda b: len(b).to_bytes(4, "little") + b
    data = text(b"cint-core-1/revision/v1") + text(b"cint-core-1") + text(CONTRACT)
    data += len(modules).to_bytes(4, "little")
    for path, sir in sorted(modules, key=lambda m: m[0].encode("utf-8")):
        enc = sir17_encoding(sir)
        data += text(path.encode("utf-8")) + len(enc).to_bytes(8, "little") + enc
    return hashlib.sha256(data).hexdigest()


def source_map(modules) -> str:
    """SPEC-09 SIR-17's source-map digest over [(module path, SIR text)], in the encoding of
    rt/cint_rec.c's header comment (rt/OPEN.md RT-OQ-45): the domain and the module count,
    then each module in byte order of path, its path, the count of its sites, and each site of
    its `sites` line as ordinal, line and column (U32 each) and operation."""
    text = lambda b: len(b).to_bytes(4, "little") + b
    data = text(b"cint-core-1/source-map/v1") + len(modules).to_bytes(4, "little")
    for path, sir in sorted(modules, key=lambda m: m[0].encode("utf-8")):
        line = sir.split(b"\n")[1].split(b" ")
        assert line[:2] == [b"sites", path.encode("utf-8")], line[:2]
        data += text(path.encode("utf-8")) + (len(line) - 2).to_bytes(4, "little")
        for site in line[2:]:
            ordinal, row, column, op = site.split(b":")
            data += b"".join(int(x).to_bytes(4, "little") for x in (ordinal, row, column)) + text(op)
    return hashlib.sha256(data).hexdigest()


def interp_source_identity() -> str:
    """The interpreter's source identity that a `replay` receipt names: tools/cint_check.py's
    digest of the files cint-interp is built from."""
    return cint_check.files_digest(SOURCES + sorted((ROOT / "interp").glob("*.h")) +
                                   [ROOT / "rt" / n for n in ("cint_rt.h", "cint_rt_host.h", "cint_rt_internal.h",
                                                              "cint_state.h")] +
                                   [ROOT / "harness" / "cint_harness.c"])


def program(directory: pathlib.Path):
    """[(module path, SIR text)] of a program directory, from its cint-program.sites."""
    out = []
    for line in (directory / "cint-program.sites").read_bytes().decode("ascii").splitlines():
        path = line.split(" ", 2)[2]
        out.append((path, (directory / (path[:-3] + ".sites")).read_bytes()))
    return out


def counts(modules) -> str:
    """`counts <modules> <functions> <nodes>`: a node is each line of a function body,
    its closing brace included."""
    functions = nodes = 0
    for _, sir in modules:
        for line in sir.decode("ascii").split("\n")[2:]:
            if re.match(r"(export )?(func|script|test|kernel) @", line):
                functions += 1
            elif line and not line.startswith(("global ", "struct ", "widen W", "error E", "arena @", "pool @",
                                               "extern @")):
                nodes += 1
    return "counts %d %d %d" % (len(modules), functions, nodes)


def program_revision(program_c: bytes) -> str:
    m = re.search(rb"static const uint8_t cg_revision\[32\] = \{\n(.*?)\n\};", program_c, re.S)
    return "".join("%02x" % int(x, 0) for x in m.group(1).decode("ascii").replace("\n", " ").split(",")
                   if x.strip()) if m else ""


def recorded_revisions() -> dict:
    out = {}
    for d in DIRECTORIES:
        for line in (d / "REVISIONS").read_text(encoding="ascii").splitlines():
            case, revision = line.split()
            out[case] = revision
    return out


def fixture_names() -> list:
    """The programs under interp/tests/sir/, kernels/, errors/ and services/, each named for its case."""
    return sorted(p.name for d in DIRECTORIES for p in d.iterdir() if p.is_dir())


def fixture(name: str) -> pathlib.Path:
    """The directory of fixture `name`."""
    return next((d for d in (KERNELS, ERRORS, SERVICES) if (d / name).is_dir()), FIXTURES) / name


def run(exe, directory) -> tuple:
    """Exit status, stdout and stderr of sir_check (text mode: CR LF on Windows read as LF)."""
    r = subprocess.run([str(exe), str(directory)], capture_output=True, timeout=120)
    text = lambda b: b.decode("ascii", "replace").replace("\r\n", "\n")
    return r.returncode, text(r.stdout), text(r.stderr)


def fixture_case(name: str):
    """The header of fixture `<category>__<case>`'s conformance program and its frozen .expect."""
    category, case = name.split("__")
    ci = ROOT / "conformance" / category / (case + ".ci")
    return gen_expect.header(ci.read_text(encoding="utf-8")), ci.with_suffix(".expect")


def fuzz_fuel(base: str) -> int:
    """The fuel of a fuzz input edited from program `base`: twice the most that any call of its
    frozen .expect consumes, and at least FUZZ_FUEL. An edit that keeps a loop from ending then
    stops at E_FUEL soon after the program would have finished, so an input's time stays near
    its base's: memory__frame_arena_released_at_block_exit zeroes 1,500,000 elements in each
    iteration, and without its `store.local $i.1` it ran 100,000 iterations, about a minute with
    gcc -O2 and past the 120 s timeout with the sanitizers (seeds 3000294508 and 1533549988),
    where it now runs 1,000."""
    if base == "add":
        return FUZZ_FUEL
    used = re.findall(r"^fuel-consumed (\d+)$", fixture_case(base)[1].read_text(encoding="utf-8"), re.M)
    return max([FUZZ_FUEL] + [2 * int(u) for u in used])


def wide_cases(w: int, m: int) -> list:
    """The calls of a wide program whose smaller type has w bits and whose mul_full operands
    m, (entry, k, s): big(k, s) is k << s, so s near w reaches the top bits, and the f_ entries
    each end at one fault."""
    return [("values", 5, w - 10), ("values", 5, w // 2), ("values", -9, w // 3), ("values", 1, 0),
            ("values", 5, 40), ("values", -7, 60), ("aggregates", -3, 5), ("aggregates", 7, w - 12),
            ("products", 5, m // 2), ("products", -3, m - 3), ("products", -1, m - 1), ("products", 1, m - 1),
            ("f_add", 1, w - 2), ("f_mul", 1, w // 2), ("f_div", 3, 4), ("f_neg", 1, w - 2), ("f_shl", 1, w - 2),
            ("f_shr", 1, 5), ("f_as", 1, 70), ("f_as_wide", 1, w - 2), ("f_abs", 1, w - 2),
            ("f_div_trunc", 2, 3), ("f_div_round", 2, 3), ("f_isqrt", 2, 3), ("f_muldiv", 1, 40), ("f_index", 3, 0),
            ("f_call", 1, w - 2), ("f_struct", 1, w // 2), ("f_slice", 7, 0), ("f_slice", 5, 0), ("f_slice", 4, 0),
            ("f_view", 1, w - 3), ("f_view", 1, 2 * w - 3)]


def observe_args(h: dict, frozen: pathlib.Path) -> list:
    """cint-harness options for one call as the case header gives it."""
    args = ["--entry", h["entries"][0], "--clause", h["clause"]]
    args += ["--args", *(x for a in h["args"] for x in a.split(" ", 1))] if h["args"] else []
    args += ["--fuel", str(h["fuel"])] if h["fuel"] is not None else []
    args += ["--depth", str(h["depth"])] if h["depth"] is not None else []
    fmt = gen_expect.expect_format(str(frozen))
    return args + (["--format", str(fmt)] if fmt != 1 else [])


def observe_case(exe, directory: pathlib.Path, h: dict, frozen: pathlib.Path, work: pathlib.Path, pre=()):
    """`cint-interp observe` on one call of a program of more than one module, through a case
    list (CONF-12) as tools/cint_check.py runs it: the harness takes the root from the first
    module of the program table, which is the first in build order (compiler/OPEN.md
    CINTC-OQ-39), and a case list names the module; pre goes before the command, as --trace.
    (exit status, the call's lines, stderr)."""
    rel = h["case"] + ".ci"
    line = " ".join([cint_check.module_name(rel), h["entries"][0]] +
                    [cint_check.render_arg(cint_check.typed_arg(a)) for a in h["args"]])
    cases = work / "cases.txt"
    cases.write_bytes((line + "\n").encode("ascii"))
    args = ["--cases", cases, "--format", str(gen_expect.expect_format(str(frozen)))]
    args += ["--fuel", str(h["fuel"])] if h["fuel"] is not None else []
    args += ["--depth", str(h["depth"])] if h["depth"] is not None else []
    code, out, err = interp(exe, *pre, "observe", directory, *args)
    return code, cint_check.parse_harness(out, 1)[0], err


def interp(exe, *args, timeout=120):
    """cint-interp's exit status, stdout bytes and stderr text."""
    r = subprocess.run([str(exe), *map(str, args)], capture_output=True, timeout=timeout,
                       stdin=subprocess.DEVNULL)
    return r.returncode, r.stdout, r.stderr.decode("utf-8", "replace").replace("\r\n", "\n")


def run_entry(exe, directory: pathlib.Path, work: pathlib.Path, fmt: int = 1, pre=(), argv=()):
    """`cint-interp run` on a program with a script or test entry, pre before the command and
    argv the program arguments of `main` as `--arg` pairs (OQ-306): (exit status, its record
    lines in format fmt as tools/cint_check.py composes them, the error record and module state
    included, stderr)."""
    fault, fuel, state = work / "fault.bin", work / "fuel.bin", work / "state.txt"
    for f in (fault, fuel, state):
        f.unlink(missing_ok=True)
    stateful = any(re.search(rb"^global @", sir, re.M) for _, sir in program(directory))
    r = subprocess.run([str(exe), *map(str, pre), "run", str(directory), *(x for a in argv for x in ("--arg", a)),
                        str(fault), str(fuel), *([str(state)] if stateful else [])],
                       capture_output=True, timeout=120, stdin=subprocess.DEVNULL)
    lines = cint_check.entry_record(r, fault, fuel, state if stateful else None, fmt)
    return r.returncode, lines, r.stderr.decode("utf-8", "replace")


def write_program(directory: pathlib.Path, modules, listing=None):
    shutil.rmtree(directory, ignore_errors=True)
    directory.mkdir(parents=True)
    names = []
    for k, (path, sir) in enumerate(modules):
        target = directory / (path[:-3] + ".sites")
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(sir)
        names.append(b"module %d %s\n" % (k, path.encode("ascii")))
    (directory / "cint-program.sites").write_bytes(b"".join(names) if listing is None else listing)


# Malformed texts: (name, fixture, module path or None for cint-program.sites, the edit as
# (old, new) bytes or a function of the text, expected exit status, expected output). The
# output is `<file>:<line>:<column>: <text>`; a column of 0 concerns the whole line. Status 3
# is text cintc writes in a form not run yet (a form of box 12).
def drop_last_line(text: bytes) -> bytes:
    return text[:text.rstrip(b"\n").rfind(b"\n") + 1]


def with_set(text: bytes) -> bytes:
    """compiler/tests/golden/add.sir with an error union result naming error set E0 and its
    last addition a result form, which is no site (box 13 unit 8b)."""
    text = re.sub(rb" 3:3:18:add\.checked\.i64", b"", text)
    text = re.sub(rb"add\.checked\.i64 %t0, %c +#3 [^\n]*", b"add.result.i64 %t0, %c", text)
    return (text.replace(b"export func", b"error E0: ArithError U16 overflow, div_zero, shift, narrow\nexport func")
            .replace(b"-> I64 {", b"-> U16!I64 error E0 {"))


ADD = "add"  # compiler/tests/golden/add.sir as module add.ci
JUMPS = "control__labelled_jumps"
PLAIN = "module__import_plain"
NESTED = "struct__nested_construct_copy"
SCRIPT = "control__script_with_test"
STRING = "boot__string_literal_view"
SWITCH = "switch__full_range_fallthrough_default"
DEFER = "cleanup__defer_in_loop_continue_break"
PT5 = "ternary__pt5_table"
STATE = "module__state_two_modules"
COMBINED = "errors__combined_set_once_at_first_position"
NARROW = "errors__as_question_try_narrow"
FOLD2 = "reduce__fold_rank2_transposed"
REVERSED = "reduce__fold_checked_reversed_view"
ARENA = "memory__frame_arena_released_at_block_exit"
TOTAL = "kernel__total_exact_sum"
UNPUBLISHED = "kernel__unpublished_after_fault"
MEM = TESTS / "mem"  # a program with arenas and pools for the checkpoint host (unit 8a)


def lp(data: bytes) -> bytes:
    return struct.pack("<I", len(data)) + data


def state_bytes(revision: str, seen: int, calls: int, entries: int) -> bytes:
    """The canonical state of module__state_two_modules (SPEC-01 IM-156 as rt/cint_state.c
    writes it): no registrations, no fault, no print statement."""
    out = lp(b"cint-core-1/state/v1") + bytes.fromhex(revision) + lp(b"cint-core-1") + struct.pack("<Q", 2)
    for name, value in ((b"module.lib.tally.calls", calls), (b"module.state_two_modules.seen", seen)):
        out += lp(name) + bytes([0x14]) + struct.pack("<q", value)
    return out + struct.pack("<7Q", 0, 0, 0, 1, 1, 1, entries) + lp(b"fuel-v1") + b"\x00" + struct.pack("<q", 0)


def checkpoint_bytes(state: bytes) -> bytes:
    """CINT-CKPT-1 (SPEC-06 15): the envelope with its three digests absent, then the state."""
    return (lp(b"cint-core-1/checkpoint/v1") + lp(cint_check.runtime_contract().encode("ascii")) + bytes(3) +
            struct.pack("<Q", len(state)) + state)


def show_text(revision: str, seen: int, calls: int, entries: int) -> str:
    """`cint-interp show` of that checkpoint (SPEC-06 6.1)."""
    state = state_bytes(revision, seen, calls, entries)
    return ("checkpoint cint-core-1/checkpoint/v1\ncontract %s\npayload-digest absent\n"
            "build-record-digest absent\nbuild-identity absent\nstate cint-core-1/state/v1\nrevision %s\n"
            "profile cint-core-1\nfuel-model fuel-v1\nglobal module.lib.tally.calls I64 %d\n"
            "global module.state_two_modules.seen I64 %d\narena-block 0\npool-block 0\n"
            "counters next-buffer 1 next-arena 1 next-pool 1 entries %d\nfault absent\neffects 0\n"
            "state-hash %s\n" % (cint_check.runtime_contract(), revision, calls, seen, entries,
                                  hashlib.sha256(state).hexdigest()))


def state_host(host, *args) -> tuple:
    r = subprocess.run([str(host), *map(str, args)], capture_output=True, timeout=120, stdin=subprocess.DEVNULL)
    return (r.returncode, r.stdout.decode("ascii", "replace").replace("\r\n", "\n"),
            r.stderr.decode("utf-8", "replace").replace("\r\n", "\n"))


def twos(value: int, bits: int) -> bytes:
    """value as a two's complement integer of `bits` bits, little-endian (SPEC-01 IM-146)."""
    return (value % (1 << bits)).to_bytes(bits // 8, "little")

MALFORMED = [
    ("program file missing", ADD, None, lambda t: None, 2,
     "cint-program.sites:0:0: cannot be opened"),
    ("program file numbering", ADD, None, (b"module 0 ", b"module 1 "), 1,
     "cint-program.sites:1:1: expected 'module 0 <path>' ending with LF"),
    ("program file path", ADD, None, (b"add.ci", b"../add.ci"), 1,
     "cint-program.sites:1:1: expected 'module 0 <path>' ending with LF"),
    ("program file without LF", ADD, None, (b"add.ci\n", b"add.ci"), 1,
     "cint-program.sites:1:1: expected 'module 0 <path>' ending with LF"),
    ("module listed twice", ADD, None, lambda t: t + b"module 1 add.ci\n", 1,
     "cint-program.sites:0:0: module add.ci is listed twice"),
    ("module text missing", ADD, "add.ci", lambda t: None, 2, "add.sites:0:0: cannot be opened"),
    ("CR byte", ADD, "add.ci", (b"-> I64 {\n", b"-> I64 {\r\n"), 1,
     "add.sites:3:58: byte 0x0d is not printable ASCII or LF"),
    ("empty line", ADD, "add.ci", (b"-> I64 {\n", b"-> I64 {\n\n"), 1, "add.sites:4:0: an empty line"),
    ("no final LF", ADD, "add.ci", lambda t: t[:-1], 1, "add.sites:0:0: the text ends with LF"),
    ("module name", ADD, "add.ci", (b"module add\n", b"module adder\n"), 1, "add.sites:1:8: module of path add.ci"),
    ("sites ordinal", ADD, "add.ci", (b" 2:3:14:", b" 3:3:14:"), 1, "add.sites:2:33: site 3 where 2 comes next"),
    ("site missing from the sites line", ADD, "add.ci", (b" 3:3:18:add.checked.i64", b""), 1,
     "add.sites:6:50: site #3 is not in the sites line"),
    ("site on a closing brace", ADD, "add.ci",
     lambda t: t.replace(b"3:3:18:add.checked.i64", b"3:3:18:add.checked.i64 4:4:1:return")
                .replace(b"\n}\n", b"\n}" + b" " * 48 + b"#4 @add.ci:4:1\n"), 1,
     "add.sites:8:1: this node has no site"),
    ("annotation column", ADD, "add.ci", (b"%a, %b                   #2", b"%a, %b                  #2"), 1,
     "add.sites:5:49: a site annotation starts at column 50 of this line"),
    ("annotation path", ADD, "add.ci", (b"#2 @add.ci:", b"#2 @sub.ci:"), 1,
     "add.sites:5:53: a site annotation names the module's path, add.ci"),
    ("annotation order", ADD, "add.ci", (b"#2 @add.ci:3:14", b"#3 @add.ci:3:14"), 1,
     "add.sites:5:50: site #3 where #2 comes next (sites are numbered in node order)"),
    ("annotation position", ADD, "add.ci", (b"#2 @add.ci:3:14", b"#2 @add.ci:3:15"), 1,
     "add.sites:5:53: site #2 is at 3:14 in the sites line"),
    ("annotation operation", ADD, "add.ci", (b"2:3:14:add.checked.i64", b"2:3:14:sub.checked.i64"), 1,
     "add.sites:5:9: site #2 names sub.checked.i64 in the sites line"),
    ("faulting node without a site", ADD, "add.ci",
     lambda t: t.replace(b" 3:3:18:add.checked.i64", b"").replace(b"%t0, %c                  #3 @add.ci:3:18",
                                                                b"%t0, %c"), 1,
     "add.sites:6:9: a node that can fault has a site"),
    ("sites line lists more", ADD, "add.ci", (b"3:3:18:add.checked.i64", b"3:3:18:add.checked.i64 4:4:1:return"),
     1, "add.sites:0:0: the sites line lists 4 sites; the nodes carry 3"),
    ("temporary out of order", ADD, "add.ci", (b"%t1 = add", b"%t2 = add"), 1,
     "add.sites:6:3: %t2 is defined where %t1 comes next"),
    ("temporary used by its own node", ADD, "add.ci", (b"%t0 = add.checked.i64 %a, %b ", b"%t0 = add.checked.i64 %t0, %b"),
     1, "add.sites:5:25: %t0 is used before it is defined"),
    ("temporary used before definition", ADD, "add.ci", (b"%t0 = add.checked.i64 %a, %b ", b"%t0 = add.checked.i64 %t1, %b"),
     1, "add.sites:5:25: %t1 is used before it is defined"),
    ("operand type", ADD, "add.ci", (b"%c: I64)", b"%c: I32)"), 1,
     "add.sites:6:30: operand of type I32 where I64 is required"),
    ("unknown operation", ADD, "add.ci", (b"%t1 = add.checked.i64", b"%t1 = add.checked.i65"), 1,
     "add.sites:6:9: unknown operation 'add.checked.i65'"),
    ("indentation", ADD, "add.ci", (b"  return %t1", b"   return %t1"), 1,
     "add.sites:7:4: indented 3 spaces where 2 are expected"),
    ("function without its brace", ADD, "add.ci", drop_last_line, 1,
     "add.sites:0:0: the function of line 3 has no closing brace"),
    ("defines no temporary", ADD, "add.ci", (b"  fuel.charge 1       ", b"  %t0 = fuel.charge 1 "), 1,
     "add.sites:4:3: this node defines no temporary"),
    ("needs a temporary", ADD, "add.ci", (b"%t1 = add.checked.i64 %t0, %c ", b"add.checked.i64 %t0, %c       "), 1,
     "add.sites:6:3: this node defines a temporary, written %t1 = first"),
    ("value out of range", ADD, "add.ci", (b"  return %t1\n", b"  %t2 = const I8 128\n  return %t1\n"), 1,
     "add.sites:7:18: number out of range"),
    ("return type", ADD, "add.ci", (b"-> I64 {", b"-> I32 {"), 1,
     "add.sites:7:10: operand of type I64 where I32 is required"),
    ("parameter spelled as a temporary", ADD, "add.ci", (b"%c: I64)", b"%t3: I64)"), 1,
     "add.sites:3:41: a parameter named t3 is written %p.t3 (CINTC-OQ-35, item 2)"),
    ("parameter form without need", ADD, "add.ci", (b"%c: I64)", b"%p.c: I64)"), 1,
     "add.sites:3:41: only a parameter named t or s and digits is written %p.<name>"),
    ("parameter form used bare", PT5, "ternary/pt5_table.ci", (b"mul.checked.i64 %t0, %p.t1", b"mul.checked.i64 %t0, %t1  "),
     1, "ternary/pt5_table.sites:100:30: %t1 is used before it is defined"),
    ("export mark on a script", SCRIPT, "control/script_with_test.ci", (b"script @", b"export script @"), 1,
     "control/script_with_test.sites:3:8: expected func or kernel"),
    ("kernel with a result", ADD, "add.ci", (b"export func @", b"export kernel @"), 1,
     "add.sites:3:52: a kernel has no result; it publishes through its out parameters"),
    ("kernel form in a function", TOTAL, "kernel/total_exact_sum.ci",
     (b"  %t4 = load.local $s.1\n", b"  dispatch.step\n  %t4 = load.local $s.1\n"), 1,
     "kernel/total_exact_sum.sites:52:3: dispatch.step is a form of a kernel"),
    ("dispatch in a kernel", TOTAL, "kernel/total_exact_sum.ci",
     (b"  dispatch.pop\n", b"  dispatch @kernel.total_exact_sum.total_exact(%x, out %s), 1\n  dispatch.pop\n"), 1,
     "kernel/total_exact_sum.sites:36:3: a kernel dispatches no kernel"),
    ("out argument not a scalar", TOTAL, "kernel/total_exact_sum.ci", (b"out $s.1)", b"out $x.0)"), 1,
     "kernel/total_exact_sum.sites:51:54: argument 2 is not a slot or global of its out parameter's type"),
    ("out argument not a global", UNPUBLISHED, "kernel/unpublished_after_fault.ci", (b"out @total)", b"out @totals)"), 1,
     "kernel/unpublished_after_fault.sites:51:64: argument 2 is not a slot or global of its out parameter's type"),
    ("phase other than 2", TOTAL, "kernel/total_exact_sum.ci", (b"dispatch.phase 2", b"dispatch.phase 1"), 1,
     "kernel/total_exact_sum.sites:34:18: the phase after the work-items is 2"),
    ("reduction a kernel does not take", TOTAL, "kernel/total_exact_sum.ci",
     (b"reduce.add sum.checked.i64.i64", b"reduce.add dot.checked.i64.i64"), 1,
     "kernel/total_exact_sum.sites:26:16: no reduction of a kernel 'dot.checked.i64.i64'"),
    ("reduction of another type", TOTAL, "kernel/total_exact_sum.ci",
     (b"reduce.final sum.checked.i64.i64", b"reduce.final sum.checked.i32.i32"), 1,
     "kernel/total_exact_sum.sites:35:36: the reduction's result is not of its out parameter's type"),
    ("kernel view of a fixed extent", TOTAL, "kernel/total_exact_sum.ci", (b"(%x: I64[_],", b"(%x: I64[4],"), 1,
     "kernel/total_exact_sum.sites:3:48: a kernel takes views T[_, ...] of scalars, scalars and scalar outs"),
    ("view extents not the array's", FOLD2, "reduce/fold_rank2_transposed.ci", (b"$sc.0, 0, 8194", b"$sc.0, 0, 8195"),
     1, "reduce/fold_rank2_transposed.sites:6:14: the extents of the view are not its array's"),
    ("transpose of a rank-1 view", FOLD2, "reduce/fold_rank2_transposed.ci",
     (b"view.transpose $sc.2: I8[_, _]", b"view.transpose $sc.2: I8[_]"), 1,
     "reduce/fold_rank2_transposed.sites:16:18: the view does not have the rank of its place"),
    ("slice bounds not its flags", REVERSED, "reduce/fold_checked_reversed_view.ci",
     (b"_, _, 0, -1           #2", b"_, _, 64, -1          #2"), 1,
     "reduce/fold_checked_reversed_view.sites:14:34: the slice's dimension and bounds disagree with what it writes"),
    ("slice step 0", REVERSED, "reduce/fold_checked_reversed_view.ci", (b"0, -1           #2", b"0, 0            #2"),
     1, "reduce/fold_checked_reversed_view.sites:14:37: a slice's step is never 0"),
    ("view end keeps another rank", REVERSED, "reduce/fold_checked_reversed_view.ci",
     (b"view.end $sc.1, $a.0, 1, 0", b"view.end $sc.1, $a.0, 1, 1"), 1,
     "reduce/fold_checked_reversed_view.sites:15:12: the view keeps 0 dimensions"),
    ("fold without its initial value", REVERSED, "reduce/fold_checked_reversed_view.ci",
     (b"fold_checked.add.i32 $sc.1, %t3", b"fold_checked.add.i32 $sc.1     "), 1,
     "reduce/fold_checked_reversed_view.sites:16:35: fold_checked takes an initial value"),
    ("reduction of another element type", REVERSED, "reduce/fold_checked_reversed_view.ci",
     (b"fold_checked.add.i32 $sc.1", b"fold_checked.add.i64 $sc.1"), 1,
     "reduce/fold_checked_reversed_view.sites:16:30: the reduction names another element type or rank"),
    ("array.init past its array", REVERSED, "reduce/fold_checked_reversed_view.ci",
     (b"array.init $a.0, %t2, 0, 2", b"array.init $a.0, %t2, 0, 3"), 1,
     "reduce/fold_checked_reversed_view.sites:11:28: 3 is out of range here"),
    ("frame.mark twice", ARENA, "memory/frame_arena_released_at_block_exit.ci",
     (b"frame.mark 14, 2", b"frame.mark 1, 2"), 1,
     "memory/frame_arena_released_at_block_exit.sites:18:16: a second frame.mark 1 in this function"),
    ("frame.release of parts not marked", ARENA, "memory/frame_arena_released_at_block_exit.ci",
     (b"frame.release 14, 2", b"frame.release 14, 3"), 1,
     "memory/frame_arena_released_at_block_exit.sites:34:19: frame.release gives back parts of an earlier frame.mark"),
    ("view.alloc of another rank", ARENA, "memory/frame_arena_released_at_block_exit.ci",
     (b"view.alloc $big.2: I64[_], 1", b"view.alloc $big.2: I64[_], 0"), 1,
     "memory/frame_arena_released_at_block_exit.sites:21:16: view.alloc names its view's rank"),
    ("frame.charge removed", PLAIN, "module/import_plain.ci", lambda t: re.sub(rb"  frame.charge 4 [^\n]*\n", b"", t),
     1, "module/import_plain.sites:0:0: the function of line 3 gives back charges and has no frame.charge"),
    ("index.load before the index.checked of its place", PLAIN, "module/import_plain.ci",
     lambda t: re.sub(rb"(  index.checked.i64 \$cells.2, [^\n]*\n)(  %t12 = index.load [^\n]*\n)", rb"\2\1", t), 1,
     "module/import_plain.sites:26:10: index.load of a place that no index.checked checks before it"),
    ("entry without its error set", ADD, "add.ci", (b"-> I64 {", b"-> U16!I64 {"), 1,
     "add.sites:3:60: an entry's error union result names its error set, error E<k>"),
    ("error set of no line", ADD, "add.ci", (b"-> I64 {", b"-> U16!I64 error E0 {"), 1,
     "add.sites:3:67: no error set E0 before the functions"),
    ("error set on a function that is no entry", ADD, "add.ci", lambda t: with_set(t).replace(b"export func", b"func"),
     1, "add.sites:4:62: only an entry's error union result names its error set"),
    ("error union of a signed tag", ADD, "add.ci", lambda t: with_set(t).replace(b"-> U16!", b"-> I16!"), 1,
     "add.sites:4:56: an error union's tag has an unsigned type"),
    ("error set of another tag type", ADD, "add.ci", lambda t: with_set(t).replace(b"ArithError U16", b"ArithError U8"),
     1, "add.sites:4:69: the error set's tag type is not the result's"),
    ("error set of a signed tag", ADD, "add.ci", lambda t: with_set(t).replace(b"ArithError U16", b"ArithError I16"),
     1, "add.sites:3:25: an error set's tag has an unsigned type"),
    ("second error set of one number", ADD, "add.ci",
     lambda t: with_set(t).replace(b"error E0:", b"error E0: ArithError U16 overflow\nerror E0:", 1), 1,
     "add.sites:4:1: a second E0"),
    ("error line after a function", ADD, "add.ci", lambda t: t + b"error E0: ArithError U16 overflow\n", 1,
     "add.sites:9:0: widen and error lines come before the functions"),
    ("widen line after a function", ADD, "add.ci", lambda t: t + b"widen W0: 0 1\n", 1,
     "add.sites:9:0: widen and error lines come before the functions"),
    ("error.tag without its producer", ADD, "add.ci", (b"  return %t1\n", b"  %t2 = error.tag\n  return %t1\n"), 1,
     "add.sites:7:9: error.tag follows a call of a function or an extern with an error union result, or a result "
     "form"),
    ("return.error without an error union", ADD, "add.ci", (b"  return %t1\n", b"  return.error %t1\n"), 1,
     "add.sites:7:3: return.error is in a function with an error union result"),
    ("error.leave outside a test", ADD, "add.ci",
     lambda t: with_set(t).replace(b"  return %t1\n", b"  error.leave %t1, E0\n  return %t1\n"), 1,
     "add.sites:8:3: error.leave is in a test"),
    ("widen of signed tags", ADD, "add.ci",
     lambda t: with_set(t).replace(b"  return %t1\n", b"  %t2 = widen.i16.u16 %t1, W0\n  return %t1\n"), 1,
     "add.sites:8:9: widen converts between unsigned tag types"),
    ("second function of one name", ADD, "add.ci", lambda t: t + t[t.index(b"func"):], 1,
     "add.sites:9:0: a second function of this name"),
    ("node outside a function", ADD, "add.ci", lambda t: t + b"  return %t1\n", 1,
     "add.sites:9:1: a node outside a function"),
    ("global after a function", ADD, "add.ci", lambda t: t + b"global @g: I64 = 0\n", 1,
     "add.sites:9:0: struct and global lines come before the functions"),
    ("struct after a function", ADD, "add.ci", lambda t: t + b"struct Q()\n", 1,
     "add.sites:9:0: struct and global lines come before the functions"),
    ("module-level array of too many elements", ADD, "add.ci", (b"export func", b"global @g: I64[300, 300]\nexport func"),
     1, "add.sites:3:16: a module-level array of more than 65536 elements"),
    ("module-level array of rank 5", ADD, "add.ci", (b"export func", b"global @g: I64[1, 1, 1, 1, 1]\nexport func"), 1,
     "add.sites:3:26: expected ']'"),
    ("module-level array of too few items", ADD, "add.ci",
     (b"export func", b"global @g: I64[2, 2] = [1, 2, 3]\nexport func"), 1, "add.sites:3:32: expected ', '"),
    ("unsigned wide value with a sign", ADD, "add.ci", (b"export func", b"global @g: U128 = -1\nexport func"), 1,
     "add.sites:3:19: an unsigned value has no sign"),
    ("unsigned wide value of 2^128", ADD, "add.ci",
     (b"export func", b"global @g: U128 = 340282366920938463463374607431768211456\nexport func"), 1,
     "add.sites:3:19: number out of range"),
    ("fixed-point value without raw", ADD, "add.ci", (b"export func", b"global @g: Q16.16 = 5\nexport func"), 1,
     "add.sites:3:21: expected raw and the storage integer"),
    ("fixed-point raw value out of range", ADD, "add.ci", (b"export func", b"global @g: Q8.8 = raw 32768\nexport func"),
     1, "add.sites:3:23: number out of range"),
    ("fixed-point type of 12 bits", ADD, "add.ci", (b"export func", b"global @g: Q4.8 = raw 0\nexport func"), 1,
     "add.sites:3:12: no struct Q4.8"),
    ("struct of no line", PLAIN, "module/import_plain.ci", (b"local $p.1: module.lib.plain_math.Pair",
                                                           b"local $p.1: module.lib.plain_math.Pairs"), 1,
     "module/import_plain.sites:11:15: no struct module.lib.plain_math.Pairs"),
    ("struct of this module qualified", PLAIN, "module/lib/plain_math.ci", (b"(%p: Pair)", b"(%p: module.lib.plain_math.Pair)"),
     1, "module/lib/plain_math.sites:10:49: no struct module.lib.plain_math.Pair"),
    ("second struct of one name", PLAIN, "module/lib/plain_math.ci", (b"struct Pair(a: I64, b: I64)\n",
                                                                     b"struct Pair(a: I64, b: I64)\nstruct Pair(a: I64)\n"),
     1, "module/lib/plain_math.sites:4:8: a second struct of this name"),
    ("second field of one name", PLAIN, "module/lib/plain_math.ci", (b"(a: I64, b: I64)", b"(a: I64, a: I64)"), 1,
     "module/lib/plain_math.sites:3:21: a second field of this name"),
    ("struct inside itself", NESTED, "struct/nested_construct_copy.ci", (b"struct C(v: I64)", b"struct C(v: A[2])"), 1,
     "struct/nested_construct_copy.sites:5:10: struct A holds itself"),
    ("field of no line", PLAIN, "module/lib/plain_math.ci", (b"field.load %p, b", b"field.load %p, c"), 1,
     "module/lib/plain_math.sites:13:24: no field c"),
    ("field type", PLAIN, "module/lib/plain_math.ci", (b"(a: I64, b: I64)", b"(a: I64, b: I32)"), 1,
     "module/lib/plain_math.sites:14:30: operand of type I32 where I64 is required"),
    ("field of a view", PLAIN, "module/lib/plain_math.ci", (b"index.store %cells, %t3, %v", b"field.store %cells, a, %v"),
     1, "module/lib/plain_math.sites:29:25: a field of a value that is not a struct"),
    ("arm closed by a brace", JUMPS, "control/labelled_jumps.ci", (b"        } then %t11 {\n", b"        }\n        } then %t11 {\n"),
     1, "control/labelled_jumps.sites:31:9: an arm's condition closes with '} then'"),
    ("then outside an arm", JUMPS, "control/labelled_jumps.ci", (b"          continue L8\n", b"        } then %t11 {\n"), 1,
     "control/labelled_jumps.sites:32:9: '} then' closes an arm's condition"),
    ("else before an arm", JUMPS, "control/labelled_jumps.ci", (b"      if {\n        arm {\n          %t9",
                                                                b"      if {\n        else {\n        }\n        arm {\n          %t9"),
     1, "control/labelled_jumps.sites:27:9: else follows an arm"),
    ("loop without while", JUMPS, "control/labelled_jumps.ci", (b"    while %t4\n", b""), 1,
     "control/labelled_jumps.sites:68:5: next is inside a loop, after its while"),
    ("break to no structure", JUMPS, "control/labelled_jumps.ci", (b"break L8", b"break L9"), 1,
     "control/labelled_jumps.sites:41:11: L9 is no enclosing loop, switch or block"),
    ("continue to a switch", JUMPS, "control/labelled_jumps.ci", (b"continue L17", b"continue L41"), 1,
     "control/labelled_jumps.sites:47:11: continue names a loop, and L41 is a switch"),
    ("continue to a block", DEFER, "cleanup/defer_in_loop_continue_break.ci",
     (b"          break L10\n", b"          continue L10\n"), 1,
     "cleanup/defer_in_loop_continue_break.sites:24:11: continue names a loop, and L10 is a block"),
    ("while outside a loop", JUMPS, "control/labelled_jumps.ci", (b"          continue L8\n", b"          while %t11\n"), 1,
     "control/labelled_jumps.sites:32:11: while is inside a loop, once"),
    ("case after default", JUMPS, "control/labelled_jumps.ci", (b"        default {", b"        case 9 {\n        }\n        default {\n        }\n        case 8 {"),
     1, "control/labelled_jumps.sites:56:9: case is inside a switch, before its default"),
    ("call of no function", PLAIN, "module/import_plain.ci", (b"plain_math.add3(", b"plain_math.add4("), 1,
     "module/import_plain.sites:20:14: no function module.lib.plain_math.add4 in this module or a module read before it"),
    ("call argument type", PLAIN, "module/import_plain.ci", (b"add3(%t3, %t4, %t5)", b"add3(%t3, %t4, $p.1)"), 1,
     "module/import_plain.sites:20:52: expected a temporary or a parameter"),
    ("slot before its local line", NESTED, "struct/nested_construct_copy.ci", (b"  local $b.4: A\n", b""), 1,
     "struct/nested_construct_copy.sites:17:15: $b.4 is used before its local line"),
    ("data defined twice", STRING, "boot/string_literal_view.ci", (b"%s42 = data", b"%s36 = data"), 1,
     "boot/string_literal_view.sites:48:3: %s36 is defined twice"),
    ("print text escape", SCRIPT, "control/script_with_test.ci", (b'"script\\n"', b'"script\\q"'), 1,
     "control/script_with_test.sites:9:22: unknown escape"),
    ("print piece outside a print statement", SCRIPT, "control/script_with_test.ci",
     (b"  print.end\n", b'  print.end\n  print.bytes "x"\n'), 1,
     "control/script_with_test.sites:11:3: print.bytes is inside a print statement"),
    ("node inside a print statement", SCRIPT, "control/script_with_test.ci",
     (b'  print.bytes "script\\n"\n', b'  print.bytes "script\\n"\n  %t1 = const I64 1\n'), 1,
     "control/script_with_test.sites:10:9: a print statement holds print.bytes and print.value only"),
    ("print statement left open", SCRIPT, "control/script_with_test.ci", (b"  print.end\n", b""), 1,
     "control/script_with_test.sites:10:1: a print statement ends with print.end"),
    ("fallthrough not last", SWITCH, "switch/full_range_fallthrough_default.ci",
     (b"      fallthrough\n", b"      fallthrough\n      store.local $r.1, %t5\n"), 1,
     "switch/full_range_fallthrough_default.sites:19:7: fallthrough ends its case"),
    ("leading zero", ADD, "add.ci", (b"fuel.charge 1 ", b"fuel.charge 01"), 1, "add.sites:4:15: a number has no leading zero"),
]


class Interp(unittest.TestCase):
    def setUp(self):
        self.work = pathlib.Path(tempfile.mkdtemp(prefix="sir-", dir=ARGS.out))

    def tearDown(self):
        shutil.rmtree(self.work, ignore_errors=True)

    def check_read(self, exe, directory, want_revision, want_counts):
        code, out, err = run(exe, directory)
        self.assertEqual((code, err), (0, ""), out)
        self.assertEqual(out, "revision %s\n%s\n" % (want_revision, want_counts))

    def test_golden(self):
        """The SIR-17 golden test (compiler/tests/golden_suite.py)."""
        for name, want in (("add", ADD_REVISION), ("hello", program_revision((GOLDEN / "hello.program.c").read_bytes()))):
            modules = [(name + ".ci", (GOLDEN / (name + ".sir")).read_bytes())]
            self.assertEqual(sir17_revision(modules), want)
            write_program(self.work / name, modules)
            for label, exe in BUILDS:
                with self.subTest(build=label, program=name):
                    self.check_read(exe, self.work / name, want, counts(modules))
            for label, exe in INTERPS:   # `cint-interp identity`, with the source-map digest (box 13 unit 6)
                with self.subTest(build=label, program=name, command="identity"):
                    self.assertEqual(interp(exe, "identity", self.work / name),
                                     (0, b"revision %s\nsource-map %s\n" % (want.encode(), source_map(modules).encode()),
                                      ""))
        self.assertEqual(interp(INTERPS[0][1], "identity")[0], 4)
        self.assertEqual(interp(INTERPS[0][1], "identity", self.work / "missing")[0], 5)

    def test_fixtures(self):
        recorded = recorded_revisions()
        cases = fixture_names()
        self.assertEqual(sorted(recorded), cases)
        for case in cases:
            modules = program(fixture(case))
            self.assertEqual(sir17_revision(modules), recorded[case], case)
            for label, exe in BUILDS:
                with self.subTest(build=label, case=case):
                    self.check_read(exe, fixture(case), recorded[case], counts(modules))

    def test_malformed(self):
        for name, base, path, edit, status, want in MALFORMED:
            modules = base_program(base)
            listing = b"".join(b"module %d %s\n" % (k, p.encode("ascii")) for k, (p, _) in enumerate(modules))
            if path is None:
                listing = apply(listing, edit)
            else:
                modules = [(p, apply(sir, edit) if p == path else sir) for p, sir in modules]
            directory = self.work / "malformed"
            write_program(directory, [(p, s if s is not None else b"") for p, s in modules],
                          listing if listing is not None else b"")
            if listing is None:
                (directory / "cint-program.sites").unlink()
            for p, sir in modules:
                if sir is None:
                    (directory / (p[:-3] + ".sites")).unlink()
            for label, exe in BUILDS:
                with self.subTest(build=label, case=name):
                    code, out, err = run(exe, directory)
                    self.assertEqual((code, out, err), (status, want + "\n", ""))

    def test_unchecked_access(self):
        """An index the text did not check first faults E_BOUNDS at the index.checked of its place
        before it, so its position resolves (fuzz seed 1687865382, input 102)."""
        modules = [(p, apply(s, (b"index.store $three.46, %t425,", b"index.store $three.46, %t42,")))
                   for p, s in base_program(PT5)]
        directory = self.work / "unchecked"
        write_program(directory, modules)
        for label, exe in INTERPS:
            with self.subTest(build=label):
                code, out, err = interp(exe, "observe", directory, "--entry", "run", "--clause", "unchecked",
                                        "--fuel", "100000")
                self.assertEqual(code, 0, err)
                self.assertRegex(out.decode("ascii"), r"\noutcome fault\n(.*\n)*fault.code E_BOUNDS\n(.*\n)*"
                                 r"fault.operation index.checked.u8\n(.*\n)*fault.position ternary/pt5_table.ci:190:10\n")

    def test_item_of_empty_domain(self):
        """A work-item over an extent of 0, which only an edited text reaches (its loop's while
        moved after the item), faults E_BOUNDS at the dispatch rather than dividing by the
        extent (fuzz seed 3164951688, input 93)."""
        name = "kernel/min_no_contributions.ci"
        modules = [(p, s if p != name else apply(apply(s, (b"    while %t5\n", b"    dispatch.step\n")),
                                                 (b"    dispatch.step\n    %t9", b"    while %t5\n    %t9")))
                   for p, s in base_program("kernel__min_no_contributions")]
        directory = self.work / "empty_domain"
        write_program(directory, modules)
        for label, exe in INTERPS:
            with self.subTest(build=label):
                code, out, err = interp(exe, "observe", directory, "--entry", "run", "--clause", "empty_domain",
                                        "--fuel", "1000", "--format", "4")
                self.assertEqual(code, 0, err)
                self.assertNotRegex(err, "Sanitizer|runtime error")
                self.assertRegex(out.decode("ascii"), r"\noutcome fault\n(.*\n)*fault.code E_BOUNDS\n(.*\n)*"
                                 r"fault.operation index.checked.i64\n(.*\n)*fault.position "
                                 r"kernel/min_no_contributions.ci:14:5\n")

    def test_items_beyond_charge(self):
        """A dispatch whose work-item loop never steps its counter, which only an edited text runs
        (the store of the next index removed), faults E_BOUNDS at the dispatch once it has run the
        N work-items it charged, rather than running work-items without end and without fuel
        (fuzz seed 3030150361, input 12)."""
        name = "kernel/inout_copy_in_copy_out.ci"
        modules = [(p, s if p != name else apply(s, (b"    store.local $sc.4, %t21\n", b"")))
                   for p, s in base_program("kernel__inout_copy_in_copy_out")]
        directory = self.work / "items_beyond_charge"
        write_program(directory, modules)
        for label, exe in INTERPS:
            with self.subTest(build=label):
                code, out, err = interp(exe, "observe", directory, "--entry", "run", "--clause", "beyond_charge",
                                        "--fuel", "1000", "--format", "4")
                self.assertEqual(code, 0, err)
                self.assertNotRegex(err, "Sanitizer|runtime error")
                self.assertRegex(out.decode("ascii"), r"\noutcome fault\n(.*\n)*fault.code E_BOUNDS\n(.*\n)*"
                                 r"fault.operation index.checked.i64\n(.*\n)*fault.position "
                                 r"kernel/inout_copy_in_copy_out.ci:15:5\n")

    def test_item_out_of_range(self):
        """A work-item whose index lies outside 0 to N - 1, which only an edited text runs (its loop's
        counter starting at -1), faults E_BOUNDS at the dispatch, and the fault's address names the
        item's ordinal, which CONF-11 rule 13 can write (fuzz seed 839230565, input 31)."""
        name = "kernel/total_exact_sum.ci"
        modules = [(p, s if p != name else apply(s, (b"  dispatch.push\n  %t2 = const I64 0\n",
                                                     b"  dispatch.push\n  %t2 = const I64 -1\n")))
                   for p, s in base_program("kernel__total_exact_sum")]
        directory = self.work / "item_out_of_range"
        write_program(directory, modules)
        for label, exe in INTERPS:
            with self.subTest(build=label):
                code, out, err = interp(exe, "observe", directory, "--entry", "run", "--clause", "out_of_range",
                                        "--fuel", "1000", "--format", "4")
                self.assertEqual(code, 0, err)
                self.assertNotRegex(err, "Sanitizer|runtime error")
                self.assertRegex(out.decode("ascii"), r"\noutcome fault\n(.*\n)*fault.code E_BOUNDS\n(.*\n)*"
                                 r"fault.operation index.checked.i64\nfault.operand I64 -1\n(.*\n)*fault.position "
                                 r"kernel/total_exact_sum.ci:14:5\n(.*\n)*fault.phase work-item\n(.*\n)*"
                                 r"fault.address \d+ 0 0\n")

    def test_store_after_release(self):
        """A store through an array after the frame.release that gave back its storage, which only
        an edited text runs (the store moved after the release), faults E_BOUNDS at the check the
        reader paired with it rather than writing storage the arena has freed (fuzz seed 2809249378,
        input 46)."""
        name = "memory/frame_arena_released_at_block_exit.ci"
        store = b"    index.store $big.2, %t7, %t8\n"
        modules = [(p, s if p != name else apply(apply(s, (store + b"    %t9", b"    next\n    %t9")),
                                                 (b"    frame.release 14, 2\n    next\n",
                                                  b"    frame.release 14, 2\n" + store)))
                   for p, s in base_program("memory__frame_arena_released_at_block_exit")]
        directory = self.work / "store_after_release"
        write_program(directory, modules)
        for label, exe in INTERPS:
            with self.subTest(build=label):
                code, out, err = interp(exe, "observe", directory, "--entry", "run", "--clause", "after_release",
                                        "--fuel", "1000", "--format", "4")
                self.assertEqual(code, 0, err)
                self.assertNotRegex(err, "Sanitizer|runtime error")
                self.assertRegex(out.decode("ascii"), r"\noutcome fault\n(.*\n)*fault.code E_BOUNDS\n(.*\n)*"
                                 r"fault.operation index.checked.i64\n(.*\n)*fault.position "
                                 r"memory/frame_arena_released_at_block_exit.ci:12:17\n")

    def test_fuzz(self):
        """Seeded random edits: each is read or reported, never a crash (SPEC-09 9.7)."""
        seed = ARGS.seed if ARGS.seed is not None else random.SystemRandom().randrange(1 << 32)
        rng = random.Random(seed)
        names = ("add", *fixture_names())
        bases = [(base_program(b), fuzz_fuel(b)) for b in names]
        tally = {0: 0, 1: 0, 3: 0, "ran": 0}
        for i in range(ARGS.fuzz):
            base, fuel = rng.choice(bases)
            modules = list(base)
            k = rng.randrange(len(modules))
            path, sir = modules[k]
            modules[k] = (path, mutate(rng, sir))
            directory = self.work / "fuzz"
            write_program(directory, modules)
            for (label, exe), (_, runner) in zip(BUILDS, INTERPS):
                code, out, err = run(exe, directory)
                with self.subTest(build=label, seed=seed, index=i):
                    self.assertIn(code, (0, 1, 3), out + err)
                    self.assertEqual(err, "")
                    if code != 0:
                        self.assertRegex(out, r"^[^\n:]+:\d+:\d+: [^\n]+\n$")
                    tally[code] += 1
                    if code == 0:   # it reads, so it runs: an outcome or a refusal, never a crash
                        call = ("observe", directory, "--entry", "run", "--clause", "fuzz", "--fuel", str(fuel),
                                "--format", "4")
                        status, stdout, err = interp(runner, *call)
                        self.assertIn(status, (0, 3), err)
                        # every node run itself (--trace) gives the same as the fast forms (O9b)
                        self.assertEqual(interp(runner, "--trace", os.devnull, *call)[:2], (status, stdout))
                        self.assertNotRegex(err, "Sanitizer|runtime error")
                        if status == 0:
                            self.assertRegex(stdout.decode("ascii"), r"\noutcome (value|fault|error|refused)\n")
                        else:
                            self.assertRegex(err, r"cannot load the program library: (malformed SIR|unsupported): ")
                        tally["ran"] += status == 0
        print("fuzz: seed %d, %d inputs on each of %d builds: %d read, %d reported, %d run" % (
            seed, ARGS.fuzz, len(BUILDS), tally[0], tally[1] + tally[3], tally["ran"]), file=sys.stderr)

    def test_execution(self):
        """Each fixture on cint-interp against its frozen .expect (CONF-10a), as it runs and with
        --trace, under which every node runs itself rather than its fast form (O9b)."""
        for case in fixture_names():
            h, frozen = fixture_case(case)
            for (label, exe), pre in ((i, p) for i in INTERPS for p in ((), ("--trace", os.devnull))):
                with self.subTest(build=label, case=case, trace=bool(pre)):
                    if h["entries"][0] is None:
                        fmt = gen_expect.expect_format(str(frozen))
                        code, lines, err = run_entry(exe, fixture(case), self.work, fmt, pre, h["argv"])
                        want = cint_check.body(frozen.read_bytes().decode("ascii").rstrip("\n").split("\n"))
                        self.assertEqual(lines, want, err)
                        self.assertEqual(code, {"outcome fault": 1, "outcome error": 6}.get(want[0], 0), err)
                    elif len(program(fixture(case))) > 1:
                        code, lines, err = observe_case(exe, fixture(case), h, frozen, self.work, pre)
                        want = cint_check.body(frozen.read_bytes().decode("ascii").rstrip("\n").split("\n"))
                        self.assertEqual((code, lines), (0, want), err)
                    else:
                        code, out, err = interp(exe, *pre, "observe", fixture(case), *observe_args(h, frozen))
                        self.assertEqual((code, out.decode("ascii")), (0, frozen.read_text(encoding="ascii")), err)
                    self.assertNotRegex(err, "Sanitizer|runtime error")

    def test_wide(self):
        """The wide programs (box 15): the reader gives the revision identity and counts computed
        here, and each call of wide_cases on cint-interp gives cint_ref's body, with and without
        --trace."""
        names = sorted(p.stem for p in WIDE.glob("*.ci"))
        self.assertEqual(names, sorted(WIDE_PROGRAMS))
        for name in names:
            directory, rel = WIDE / name, "wide/%s.ci" % name
            modules = program(directory)
            for label, exe in BUILDS:
                with self.subTest(build=label, program=name):
                    self.check_read(exe, directory, sir17_revision(modules), counts(modules))
            calls = wide_cases(*WIDE_PROGRAMS[name])
            cases = self.work / "cases.txt"
            cases.write_bytes("".join("%s %s I64 %d I64 %d\n" % ((cint_check.module_name(rel),) + c)
                                      for c in calls).encode("ascii"))
            src = (WIDE / (name + ".ci")).read_bytes()
            want = [cint_check.ref_bodies(src, rel, entry, [[cint_check.Value("I64", k), cint_check.Value("I64", s)]],
                                          2)[0] for entry, k, s in calls]
            self.assertTrue(any(w[0] == "outcome fault" for w in want) and any(w[0] == "outcome value" for w in want))
            for (label, exe), pre in ((i, p) for i in INTERPS for p in ((), ("--trace", os.devnull))):
                with self.subTest(build=label, program=name, trace=bool(pre)):
                    code, out, err = interp(exe, *pre, "observe", directory, "--cases", cases, "--format", "2",
                                            "--depth", cint_check.DEFAULT_DEPTH)
                    self.assertEqual(code, 0, err)
                    for call, got, w in zip(calls, cint_check.parse_harness(out, len(calls)), want):
                        self.assertEqual(got, w, call)
                    self.assertNotRegex(err, "Sanitizer|runtime error")

    def test_trace(self):
        """--trace: one line per counted operation, call and return, in step order; the
        outcome is the same as without it. A call that returns an error, and a result form,
        give its tag (box 12)."""
        trace = self.work / "trace.txt"
        line = re.compile(r"(op|call|return) (\d+) \d+\.\d+ [a-z0-9.]+( 0x[0-9a-f]{16})*( -> 0x[0-9a-f]{16})?"
                          r"( error [1-9][0-9]*)?")
        narrow = "op %d 0.0 as.result.i64.i8 0x000000000000012c -> 0x000000000000002c error 4"
        for case, errors in ((STRING, []), (COMBINED, ["return 0 0.2 call error 2"]),
                             (NARROW, [narrow % 1, narrow % 2])):
            h, frozen = fixture_case(case)
            for label, exe in INTERPS:
                with self.subTest(build=label, case=case):
                    code, out, err = interp(exe, "--trace", trace, "observe", fixture(case), *observe_args(h, frozen))
                    self.assertEqual((code, out.decode("ascii")), (0, frozen.read_text(encoding="ascii")), err)
                    events = trace.read_text(encoding="ascii").splitlines()
                    for e in events:
                        self.assertRegex(e, "^" + line.pattern + "$")
                    self.assertEqual([e for e in events if " error " in e], errors)
                    steps = [int(line.match(e).group(2)) for e in events if not e.startswith("return ")]
                    self.assertEqual(steps, list(range(len(steps))))
                    calls = [e.split(" ")[1] for e in events if e.startswith("call ")]
                    returns = [e.split(" ")[1] for e in events if e.startswith("return ")]
                    self.assertTrue(calls)
                    self.assertEqual(sorted(calls), sorted(returns))

    def test_run_refusals(self):
        """`run` refuses a malformed command line, a program without a program entry, and a
        text that breaks a rule, with the exit statuses of a compiled program's main."""
        broken = self.work / "broken"
        write_program(broken, [(p, apply(sir, (b"  return %t1", b"   return %t1")) if p == "add.ci" else sir)
                               for p, sir in base_program(ADD)])
        for label, exe in INTERPS:
            with self.subTest(build=label):
                self.assertEqual(interp(exe, "run")[0], 4)
                self.assertEqual(interp(exe, "run", FIXTURES / STRING, "a", "b", "c")[0], 4)
                self.assertEqual(interp(exe, "launch", FIXTURES / STRING)[0], 4)
                code, out, err = interp(exe, "run", FIXTURES / STRING)
                self.assertEqual((code, out), (4, b""))
                self.assertIn("no script, main(), or single test", err)
                code, out, err = interp(exe, "run", broken)
                self.assertEqual((code, out), (3, b""))
                self.assertIn("malformed SIR: add.sites:7:4: indented 3 spaces", err)
                code, out, err = interp(exe, "observe", broken, "--entry", "add", "--clause", "x")
                self.assertEqual((code, out), (3, b""))
                self.assertIn("malformed SIR: add.sites:7:4: indented 3 spaces", err)

    def test_program_args_tail(self):
        """The program arguments of `main` (OQ-306) after `--`, alone, after `--arg` pairs or as an
        empty tail, on `run`, `run --record`, `run --case` and `replay`: the separator is not an
        argument, and an empty tail gives the empty view. `replay` of a full recording or a case
        runs the entry over the recorded arguments, whatever tail it is given. Before the fix the
        three commands that take the arguments out of argv copied the separator as well, past the
        end of the arguments' buffer."""
        directory = fixture("services__main_args_print")
        source = self.work / "src"
        (source / "services").mkdir(parents=True)
        shutil.copyfile(ROOT / "lib" / "console.ci", source / "console.ci")
        shutil.copyfile(ROOT / "conformance" / "services" / "main_args_print.ci",
                        source / "services" / "main_args_print.ci")
        three, none = b"3 10 9710811210497|98|99|\n", b"0 0 \n"
        rec, case = self.work / "m.rec", self.work / "m.case"
        for label, exe in INTERPS:
            for argv, want in ((("--", "alpha", "b", "c"), three), (("--arg", "alpha", "--", "b", "c"), three),
                               (("--arg", "alpha", "--arg", "b", "--arg", "c", "--"), three), (("--",), none)):
                with self.subTest(build=label, argv=argv):
                    code, out, err = interp(exe, "run", directory, *argv)
                    self.assertEqual((code, out, err), (0, want, ""))
                    code, out, err = interp(exe, "run", "--record", rec, directory, *argv)
                    self.assertEqual((code, out, err), (0, want, ""))
                    code, out, err = interp(exe, "run", "--case", case, "--source", source, directory, *argv)
                    self.assertEqual((code, out, err), (0, want, ""))
                    for path in (rec, case):
                        for tail in ((), ("--",), ("--", "x", "--")):
                            code, out, err = interp(exe, "replay", "--echo", path, *tail)
                            self.assertEqual((code, out), (0, want), (path.name, tail, err))
                            self.assertRegex(err, r"^replay: ok, boundaries [0-9]+, chain [0-9a-f]{64}\n$")

    def test_operand_free_module(self):
        """A module none of whose nodes has an operand reads, prepares and runs, also when it is
        imported, with no sanitizer report (its operand array is empty)."""
        noop = (b"module module.lib.noop\nsites module/lib/noop.ci 1:1:13:fuel.charge\n"
                b"export func @module.lib.noop.f() {\n"
                b"  fuel.charge 1                                  #1 @module/lib/noop.ci:1:13\n  return\n}\n")
        root = (b"module module.noop_main\nsites module/noop_main.ci 1:3:12:fuel.charge 2:4:5:call.enter\n"
                b"export func @module.noop_main.run() -> I64 {\n"
                b"  fuel.charge 1                                  #1 @module/noop_main.ci:3:12\n"
                b"  call @module.lib.noop.f()                      #2 @module/noop_main.ci:4:5\n"
                b"  %t0 = const I64 7\n  return %t0\n}\n")
        alone, both = self.work / "noop", self.work / "noop2"
        write_program(alone, [("module/lib/noop.ci", noop)])
        write_program(both, [("module/lib/noop.ci", noop), ("module/noop_main.ci", root)])
        cases = self.work / "noop-cases.txt"
        cases.write_bytes(b"module.noop_main run\n")
        for label, exe in INTERPS:
            with self.subTest(build=label):
                code, out, err = interp(exe, "observe", alone, "--entry", "f", "--clause", "x")
                self.assertEqual((code, err), (0, ""))
                self.assertIn(b"outcome value\n", out)
                code, out, err = interp(exe, "observe", both, "--cases", cases)
                self.assertEqual((code, err), (0, ""))
                self.assertIn(b"return I64 7\n", out)

    def test_checkpoints(self):
        """Box 13 unit 4 on module__state_two_modules (interp/tests/state_host.c): the
        checkpoint's bytes, its state hash and `cint-interp show`; a restore and a run from it
        (SPEC-03 H-19a); an interrupt from inside an entry, which faults E_FUEL at the next
        call with the allowance at its consumption; and a change of revision, which keeps
        the values, after which a checkpoint of the first revision is refused (H-19b, case 29)."""
        revision = recorded_revisions()[STATE]
        other = self.work / "revision2"
        write_program(other, [(p, apply(sir, (b"%t1 = const I64 2", b"%t1 = const I64 3"))
                               if p == "module/state_two_modules.ci" else sir) for p, sir in program(FIXTURES / STATE)])
        state1 = state_bytes(revision, 7, 1, 1)
        final = state_bytes(sir17_revision(program(other)), 16, 4, 5)
        lines = ("bind 0\nrun 0 7\ncheckpoint 0\nhash 0 %s\nrun 0 9\nrestore 0\nhash 0 %s\nrun 0 9\n"
                 "run 1 0\ninterrupt code 9 site 1.3 limit I64 1\nclear 0\nrun 0 13\n"
                 "change 0\nrun 0 16\nrestore-old 2\nhash 0 %s\n" % (hashlib.sha256(state1).hexdigest(),
                                                                       hashlib.sha256(state1).hexdigest(),
                                                                       hashlib.sha256(final).hexdigest()))
        for (label, host), (_, exe) in zip(HOSTS, INTERPS):
            with self.subTest(build=label):
                ckpt = self.work / "state.ckpt"
                code, out, err = state_host(host, FIXTURES / STATE, ckpt, ckpt, other)
                self.assertEqual((code, out), (0, lines), err)
                self.assertNotRegex(err, "Sanitizer|runtime error")
                self.assertEqual(ckpt.read_bytes(), checkpoint_bytes(state1))
                code, text, err = interp(exe, "show", ckpt)
                self.assertEqual((code, text.decode("ascii"), err), (0, show_text(revision, 7, 1, 1), ""))
                short = self.work / "short.ckpt"
                short.write_bytes(ckpt.read_bytes()[:-1])
                code, text, err = interp(exe, "show", short)
                self.assertEqual((code, text), (3, b""))
                self.assertIn("not a checkpoint that a restore accepts", err)
                self.assertEqual(interp(exe, "show")[0], 4)
                self.assertEqual(interp(exe, "show", self.work / "missing.ckpt")[0], 5)

    def test_checkpoints_compiled(self):
        """The same program built by B1 for cpu-c17: the steps a host takes before the
        interrupt print the same lines on both backends, the checkpoints are byte-identical,
        and each backend restores the other's (SPEC-03 H-19; box 13 default BX13-07)."""
        if not (ARGS.b1 or ARGS.bootstrap):
            self.skipTest("no B1 (--b1 PATH or --bootstrap)")
        tool = cint_check.b1_tool(ARGS.leg, ARGS.b1, pathlib.Path(ARGS.out) / "b1")["cint"]
        root, rel = ROOT / "conformance", "module/state_two_modules.ci"
        got = cint_check.run_b1(tool, root, cint_check.module_order(root, rel), self.work / "c17" / "out")[2]
        self.assertIsNotNone(got)
        stage = got[0]
        self.assertEqual(program(stage), program(FIXTURES / STATE), "the fixture is B1's text")
        sanitize = ARGS.leg in ("gcc", "clang") and not ARGS.no_sanitize
        tc = cint_check.Toolchain(ARGS.leg, "0" if sanitize else "2", "portable", sanitize)
        objs = tc.compile([TESTS / "state_host.c", *sorted(stage.rglob("*.c")), ROOT / "rt" / "cint_rt.c",
                           *STATE_SOURCES], self.work / "c17", "state host")
        compiled = tc.link_exe(objs, self.work / "c17" / "state_host", "state host")
        a, b, c = (self.work / name for name in ("sir.ckpt", "c17.ckpt", "cross.ckpt"))
        code, want, err = state_host(compiled, "-", b)
        self.assertEqual(code, 0, err)
        for label, host in HOSTS:
            with self.subTest(build=label):
                code, out, err = state_host(host, FIXTURES / STATE, a)
                self.assertEqual((code, "\n".join(out.split("\n")[:8]) + "\n"), (0, want), err)
                self.assertEqual(a.read_bytes(), b.read_bytes())
                self.assertEqual(state_host(compiled, "-", c, a)[:2], (0, want))
                code, out, err = state_host(host, FIXTURES / STATE, c, b)
                self.assertEqual((code, "\n".join(out.split("\n")[:8]) + "\n"), (0, want), err)

    def test_checkpoints_compiled_arenas(self):
        """Box 13 units 8a and 8b-3 on cpu-c17 and cpu-sir-interp: a program built by B1 whose
        arena, child and pool change at each call (interp/tests/mem/arena_state.ci). A restore
        brings back the used extents, the slots, the generations and the elements, so the run
        after it returns what the run after the checkpoint returned and the state hash is the
        checkpoint's; `show` lists the arenas and pools with each element type and the elements
        as their IM-146 values, with no padding (SPEC-03 M-3, M-11; SPEC-06 6.1; rt/OPEN.md
        RT-OQ-46; decision 2026-10-08 on OQ-244). Both backends write the same state, and each
        restores the other's checkpoint."""
        if not (ARGS.b1 or ARGS.bootstrap):
            self.skipTest("no B1 (--b1 PATH or --bootstrap)")
        tool = cint_check.b1_tool(ARGS.leg, ARGS.b1, pathlib.Path(ARGS.out) / "b1")["cint"]
        got = cint_check.run_b1(tool, MEM, cint_check.module_order(MEM, "arena_state.ci"),
                                self.work / "mem" / "out")[2]
        self.assertIsNotNone(got)
        sanitize = ARGS.leg in ("gcc", "clang") and not ARGS.no_sanitize
        tc = cint_check.Toolchain(ARGS.leg, "0" if sanitize else "2", "portable", sanitize)
        objs = tc.compile([TESTS / "state_host.c", *sorted(got[0].rglob("*.c")), ROOT / "rt" / "cint_rt.c",
                           *STATE_SOURCES], self.work / "mem", "arena host")
        host = tc.link_exe(objs, self.work / "mem" / "state_host", "arena host")
        ckpt = self.work / "mem" / "arena.ckpt"
        code, out, err = state_host(host, "-", ckpt)
        self.assertEqual(code, 0, err)
        self.assertNotRegex(err, "Sanitizer|runtime error")
        lines = out.split("\n")
        self.assertEqual([lines[k] for k in (0, 1, 2, 4, 5, 7, 8)],
                         ["bind 0", "run 0 11", "checkpoint 0", "run 0 21", "restore 0", "run 0 21", ""], out)
        self.assertRegex(lines[3], "^hash 0 [0-9a-f]{64}$")
        self.assertEqual(lines[6], lines[3], "the state after the restore is the checkpoint's")
        # Three Body elements, a U8 tag and an I64 mass each (IM-146): the child's, then the
        # two of the first call; an I32 of the pool in 4 bytes.
        elements = cint_check.sha256(b"".join(bytes([tag]) + struct.pack("<q", mass)
                                              for tag, mass in ((0, 99), (7, 10), (7, 11))))
        listed = ("global arena_state.calls I64 1\narena-block 2\n"
                  "arena 1 parent 0 element-type arena_state.Body live offset 0 capacity 6 used 3 generation 0 "
                  "elements-sha256 %s\n"
                  "arena 2 parent 1 live offset 0 capacity 1 used 1 generation 0\n"
                  "pool-block 1\n"
                  "pool 1 element-type I32 capacity 3 used 1 occupied 1 retired 0 elements-sha256 %s\n"
                  "pool-slot 1 0 generation 0 occupied\n"
                  "counters next-buffer 1 next-arena 3 next-pool 2 entries 1\n"
                  % (elements, cint_check.sha256(struct.pack("<i", 1))))
        code, shown, err = interp(INTERPS[0][1], "show", ckpt)
        self.assertEqual((code, err), (0, ""))
        self.assertIn(listed, shown.decode("ascii"))
        self.assertIn("state-hash %s\n" % lines[3][len("hash 0 "):], shown.decode("ascii"))
        # On cpu-sir-interp (unit 8b-2) the same runs, restore and state before the interrupt,
        # an element in the interpreter's cells (unit 8b-3), and each backend restores the
        # other's checkpoint.
        want = "\n".join(lines[:8]) + "\n"
        for (label, host_i), (_, exe) in zip(HOSTS, INTERPS):
            with self.subTest(build=label):
                mine = self.work / "mem" / ("%s.ckpt" % label)
                code, out, err = state_host(host_i, got[0], mine)
                self.assertEqual(code, 0, err)
                self.assertNotRegex(err, "Sanitizer|runtime error")
                self.assertEqual("\n".join(out.split("\n")[:8]) + "\n", want, "the runs and state hashes of compiled C")
                self.assertEqual(mine.read_bytes(), ckpt.read_bytes(), "the checkpoint is compiled C's")
                code, shown, err = interp(exe, "show", mine)
                self.assertEqual((code, err), (0, ""))
                self.assertIn(listed, shown.decode("ascii"))
                code, out, err = state_host(host_i, got[0], self.work / "mem" / "x.ckpt", ckpt)
                self.assertEqual((code, "\n".join(out.split("\n")[:8]) + "\n"), (0, want), err)
                code, out, err = state_host(host, "-", self.work / "mem" / "y.ckpt", mine)
                self.assertEqual((code, out), (0, "\n".join(lines)), err)

    def test_recordings(self):
        """Box 13 unit 5 on interp/tests/rec/ (interp/tests/rec_host.c): recordings of entries
        with print statements, a checkpoint, two faults and their clears, a restore, the
        program entry, an interrupt and a change of revision; `show`; `replay`, which compares
        every boundary, with its stop boundary, checkpoint, echo and receipt; a recording
        altered at one boundary, which diverges there; and `run --record` (SPEC-03 H-14 to
        H-17; SPEC-06 10.5, 12.2)."""
        sir = REC / "sir"
        revision = sir17_revision(program(sir))
        other = self.work / "revision2"
        write_program(other, [(p, apply(t, (b"%t5 = const I64 1", b"%t5 = const I64 2")) if p == "ledger.ci" else t)
                              for p, t in program(sir)])
        case_ids = []
        for (label, host), (_, exe) in zip(RECS, INTERPS):
            with self.subTest(build=label):
                a, i, v, m = (self.work / name for name in ("a.rec", "i.rec", "v.rec", "m.rec"))
                for rec, args, more in ((a, (), ""), (i, ("-i",), "burn 1 0\n"),
                                        (v, ("-c", other), "change 0\ndeposit 2\ndeposit 0 108\n")):
                    code, out, err = state_host(host, sir, rec, *args)
                    self.assertEqual((code, out), (0, REC_LINES + more + "end 0\n"), err)
                    self.assertNotRegex(err, "Sanitizer|runtime error")
                code, shown, err = interp(exe, "show", a)
                self.assertEqual((code, err), (0, ""))
                text = shown.decode("ascii")
                self.assertTrue(text.startswith("recording cint-core-1/recording/v1\ncontract %s\nrevision %s\n"
                                                "source-map %s\nmode full\nstate-hash every entry\ndensity entry\n"
                                                % (cint_check.runtime_contract(), revision, source_map(program(sir)))),
                                text)
                for p, t in program(sir):
                    self.assertIn("module %s sir %d sha256 %s source absent\n" % (p, len(t), cint_check.sha256(t)), text)
                for line in ("event 0 entry 1 ledger.ci deposit (I64 5) fuel -1 depth 256 frame-arena 16777216\n",
                             'event 1 effect 0 site 1.3 print status 0 "deposit 5\\x0a"\n',
                             "event 2 outcome ok fuel 2 allowance -1 result I64 105 state ",
                             "event 8 outcome fault fuel 50 allowance 50 state ",
                             "fault E_FUEL fuel.charge\nfault-limit I64 50\nfault-position ledger.ci:17:5\n",
                             "fault E_OVERFLOW add.checked.i64\nfault-operand I64 112\n",
                             "event 13 restore state ", "event 14 entry 2 program fuel 100 depth 256",
                             "event 18 end host chain "):
                    self.assertIn(line, text)
                chain = re.search(r"end host chain ([0-9a-f]{64}) events 18\n$", text).group(1)
                code, out, err = interp(exe, "replay", a)
                self.assertEqual((code, out, err), (0, b"", "replay: ok, boundaries 5, chain %s\n" % chain))
                for rec in (i, v):
                    code, out, err = interp(exe, "replay", rec)
                    self.assertEqual((code, out), (0, b""), err)
                    self.assertRegex(err, r"^replay: ok, boundaries 6, chain [0-9a-f]{64}\n$")

                # The receipt of kind `replay`: canonical, its identity digest, nothing of the machine.
                receipts = []
                for k in range(2):
                    path = self.work / ("r%d.json" % k)
                    code, out, err = interp(exe, "replay", "--receipt", path, "--source-identity", "ab" * 32, i)
                    self.assertEqual(code, 0, err)
                    raw = path.read_bytes()
                    receipt = json.loads(raw)
                    self.assertEqual(raw, cint_check.canonical(receipt) + b"\n")
                    receipts.append(receipt)
                identity, outcome = receipts[0]["identity"], receipts[0]["outcome"]
                self.assertEqual(receipts[0]["observations"]["receipt_identity_sha256"],
                                 cint_check.sha256(cint_check.canonical({"identity": identity, "outcome": outcome})))
                self.assertEqual(receipts[1]["observations"]["receipt_identity_sha256"],
                                 receipts[0]["observations"]["receipt_identity_sha256"])
                self.assertEqual(identity, {
                    "backend": {"id": "cpu-sir-interp"},
                    "identities": {"revision": revision, "source_map": source_map(program(sir))},
                    "kind": "replay", "method": {"density": "entry", "fuel_model": "fuel-v1", "mode": "full",
                                                 "state_hash_interval": "1", "stop": "0"},
                    "profile": "cint-core-1",
                    "recording": {"bytes": str(len(i.read_bytes())), "sha256": cint_check.sha256(i.read_bytes())},
                    "runtime_contract": cint_check.runtime_contract(), "schema": "cint-receipt-1/replay",
                    "tool": {"interpreter_source_identity": "ab" * 32}})
                last = interp(exe, "show", i)[1].decode("ascii")
                self.assertEqual(outcome["chain_head"], re.search(r"end host chain ([0-9a-f]{64})", last).group(1))
                self.assertEqual(outcome["state_sha256"], re.findall(r"outcome [a-z]+ .* state ([0-9a-f]{64})\n", last)[-1])
                self.assertEqual(outcome["fuel_consumed"][:5], ["2", "2", "50", "1", "3"])
                self.assertEqual((outcome["status"], outcome["what"], outcome["boundaries"], len(outcome["fuel_consumed"])),
                                 ("ok", None, "6", 6))
                self.assertRegex(outcome["fault_sha256"], "^[0-9a-f]{64}$")

                # The stop boundary, with the recorded print bytes echoed and the context's checkpoint.
                ckpt = self.work / "stop.ckpt"
                code, out, err = interp(exe, "replay", "--stop", "2", "--echo", "--checkpoint", ckpt, a)
                self.assertEqual((code, out), (0, b"deposit 5\ndeposit 7\n"), err)
                code, state, err = interp(exe, "show", ckpt)
                self.assertEqual(code, 0, err)
                self.assertIn(b"global ledger.total I64 112\nglobal lib.audit.notes I64 2\n", state)

                # A recording altered at one boundary diverges there; a cut or rehashed one is refused.
                data = a.read_bytes()
                at = data.index(struct.pack("<q", 105), data.index(b"deposit 5\n"))
                bad = self.work / "bad.rec"
                bad.write_bytes(data[:at] + struct.pack("<q", 106) + data[at + 8:])
                self.assertEqual(interp(exe, "replay", bad), (3, b"", "replay: diverged at event 2 (result), boundaries 0\n"))
                bad.write_bytes(data.replace(b"deposit 7\n", b"deposit 8\n"))
                self.assertEqual(interp(exe, "replay", bad), (3, b"", "replay: diverged at event 5 (effect), boundaries 1\n"))
                at = data.index(bytes.fromhex(re.search(r"event 6 outcome .* state ([0-9a-f]{64})", text).group(1)))
                bad.write_bytes(data[:at] + bytes([data[at] ^ 1]) + data[at + 1:])
                for args in (("replay", bad), ("show", bad), ("replay", other / "ledger.sites")):
                    code, out, err = interp(exe, *args)
                    self.assertEqual((code, out), (3, b""))
                    self.assertRegex(err, "a complete recording")
                for cut in (1, 41, len(data) // 2):
                    bad.write_bytes(data[:-cut])
                    self.assertEqual(interp(exe, "replay", bad)[0], 3)

                # `run --record`, and its replay.
                code, out, err = interp(exe, "run", "--record", m, sir)
                self.assertEqual((code, out, err), (0, b"ledger 100\ndeposit 1\n", ""))
                code, out, err = interp(exe, "replay", "--echo", m)
                self.assertEqual((code, out), (0, b"ledger 100\ndeposit 1\n"), err)
                self.assertIn("event 0 entry 1 program fuel -1 depth 256 frame-arena 16777216\n",
                              interp(exe, "show", m)[1].decode("ascii"))
                for args in (("replay",), ("replay", "--stop", "0", a), ("replay", "--stop", a),
                             ("replay", "--source-identity", "AB" * 32, a), ("run", "--record", m),
                             ("run", "--case", m, sir), ("run", "--case", m, "--src", REC, sir)):
                    self.assertEqual(interp(exe, *args)[0], 4, args)
                self.assertEqual(interp(exe, "replay", self.work / "missing.rec")[0], 5)

                # A case (CINT-CASE-1, box 13 unit 6): the recording with each module's source.
                case = self.work / "ledger.case"
                code, out, err = interp(exe, "run", "--case", case, "--source", REC, sir)
                self.assertEqual((code, out, err), (0, b"ledger 100\ndeposit 1\n", ""))
                code, shown, err = interp(exe, "show", case)
                text = shown.decode("ascii")
                self.assertEqual(code, 0, err)
                self.assertTrue(text.startswith("case cint-core-1/case/v1\ncontract %s\nrevision %s\nsource-map %s\n"
                                                "mode full\n" % (cint_check.runtime_contract(), revision,
                                                                 source_map(program(sir)))), text)
                for p, t in program(sir):
                    self.assertIn("module %s sir %d sha256 %s source %d\n"
                                  % (p, len(t), cint_check.sha256(t), len((REC / p).read_bytes())), text)
                data = case.read_bytes()
                self.assertEqual(cint_check.sha256(data), CASE_SHA256)
                for p, _ in program(sir):
                    self.assertIn((REC / p).read_bytes(), data)
                code, out, err = interp(exe, "replay", "--echo", case)
                self.assertEqual((code, out), (0, b"ledger 100\ndeposit 1\n"), err)
                path = self.work / "case.json"
                code, out, err = interp(exe, "replay", "--receipt", path, "--source-identity", "cd" * 32, case)
                self.assertEqual(code, 0, err)
                receipt = json.loads(path.read_bytes())
                self.assertEqual(path.read_bytes(), cint_check.canonical(receipt) + b"\n")
                self.assertEqual(receipt["identity"]["case"], {"bytes": str(len(data)), "sha256": cint_check.sha256(data)})
                self.assertNotIn("recording", receipt["identity"])
                self.assertEqual(receipt["identity"]["identities"]["source_map"], source_map(program(sir)))
                self.assertEqual((receipt["outcome"]["status"], receipt["outcome"]["chain_head"]), ("ok", CASE_CHAIN))
                case_ids.append(receipt["observations"]["receipt_identity_sha256"])
                code, out, err = interp(exe, "run", "--case", self.work / "none.case", "--source", self.work / "nowhere", sir)
                self.assertEqual((code, out), (5, b""))
                self.assertIn("cannot read the source", err)
                self.assertFalse((self.work / "none.case").exists())
        self.assertEqual(len(set(case_ids)), 1, "every build gives a case one receipt identity")

    def test_recordings_compiled(self):
        """The same program built by B1 for cpu-c17, with interp/tests/rec_host.c linked to its
        C: the recording of the same steps is the same, byte for byte, on both backends; each
        backend replays the other's, the interrupted one included, since replay runs an
        interrupted entry with the allowance its outcome records; and a change of revision,
        which that host does not follow, diverges at its event (SPEC-03 H-16, H-17)."""
        if not (ARGS.b1 or ARGS.bootstrap):
            self.skipTest("no B1 (--b1 PATH or --bootstrap)")
        tool = cint_check.b1_tool(ARGS.leg, ARGS.b1, pathlib.Path(ARGS.out) / "b1")["cint"]
        sir = REC / "sir"
        got = cint_check.run_b1(tool, REC, cint_check.module_order(REC, "ledger.ci"), self.work / "c17" / "out")[2]
        self.assertIsNotNone(got)
        stage = got[0]
        self.assertEqual(program(stage), program(sir), "the fixture is B1's text")
        sanitize = ARGS.leg in ("gcc", "clang") and not ARGS.no_sanitize
        tc = cint_check.Toolchain(ARGS.leg, "0" if sanitize else "2", "portable", sanitize)
        define = "/D" if ARGS.leg == "msvc" else "-D"
        flags = tc.flags
        tc.flags = flags + [define + "main=ledger_program_main"]   # the generated C main
        (self.work / "c17" / "table").mkdir()
        objs = tc.compile([stage / "cint-program.c"], self.work / "c17" / "table", "rec host")
        tc.flags = flags + [define + "REC_HOST_MAIN=cg_6_ledger_run"]
        objs += tc.compile([TESTS / "rec_host.c", stage / "ledger.c", stage / "lib" / "audit.c", ROOT / "rt" / "cint_rt.c",
                            *STATE_SOURCES], self.work / "c17", "rec host")
        tc.flags = flags
        compiled = tc.link_exe(objs, self.work / "c17" / "rec_host", "rec host")
        other = self.work / "revision2"
        write_program(other, [(p, apply(t, (b"%t5 = const I64 1", b"%t5 = const I64 2")) if p == "ledger.ci" else t)
                              for p, t in program(sir)])
        c = self.work / "c17.rec"
        code, out, err = state_host(compiled, "-", c, *("%s=%s" % (p, sir / (p[:-3] + ".sites")) for p, _ in program(sir)))
        self.assertEqual((code, out), (0, REC_LINES + "end 0\n"), err)
        for label, host in RECS:
            with self.subTest(build=label):
                a, i, v = (self.work / name for name in ("a.rec", "i.rec", "v.rec"))
                self.assertEqual(state_host(host, sir, a)[0], 0)
                self.assertEqual(state_host(host, sir, i, "-i")[0], 0)
                self.assertEqual(state_host(host, sir, v, "-c", other)[0], 0)
                self.assertEqual(a.read_bytes(), c.read_bytes())
                for rec, want in ((i, "replay 0 - 20 6 chain "), (c, "replay 0 - 18 5 chain ")):
                    code, out, err = state_host(compiled, "-", "-r", rec)
                    self.assertEqual(code, 0, err)
                    self.assertTrue(out.startswith(want), out)
                    self.assertEqual(state_host(host, sir, "-r", rec)[:2], (0, out))
                code, out, err = state_host(compiled, "-", "-r", v)
                self.assertTrue(out.startswith("replay 6 revision 18 5 chain "), out)
        case = self.work / "ledger.case"   # a case the interpreter saved replays on cpu-c17 (box 13 unit 6)
        code, out, err = interp(INTERPS[0][1], "run", "--case", case, "--source", REC, sir)
        self.assertEqual(code, 0, err)
        code, out, err = state_host(compiled, "-", "-r", case)
        self.assertTrue(out.startswith("replay 0 - "), out + err)
        self.assertEqual(state_host(RECS[0][1], sir, "-r", case)[:2], (0, out))

    def test_dispatch_density_compiled(self):
        """Box 13 unit 7c on interp/tests/density/ (SPEC-06 1.5; OQ-234): B1's C calls
        cint_rt_dispatch_end where each kernel has published, as the interpreter does, so the
        program entry recorded at `dispatch` density is the same, byte for byte, on both
        backends: dispatches 0 and 1 have boundaries of their own, and dispatch 2, which the
        entry's outcome follows at the same state, is that outcome's boundary. Each backend
        replays the other's recording and stops at each of its boundaries with the same state.
        The interpreter's last dispatch domain (BX14-13) is then sum_of's: rank 1, extent 4."""
        if not (ARGS.b1 or ARGS.bootstrap):
            self.skipTest("no B1 (--b1 PATH or --bootstrap)")
        tool = cint_check.b1_tool(ARGS.leg, ARGS.b1, pathlib.Path(ARGS.out) / "b1")["cint"]
        got = cint_check.run_b1(tool, DENSITY, ["density.ci"], self.work / "density" / "out")[2]
        self.assertIsNotNone(got)
        stage = got[0]
        self.assertEqual((stage / "density.c").read_bytes().count(b"cint_rt_dispatch_end(ctx, kd);"), 2)
        sanitize = ARGS.leg in ("gcc", "clang") and not ARGS.no_sanitize
        tc = cint_check.Toolchain(ARGS.leg, "0" if sanitize else "2", "portable", sanitize)
        define = "/D" if ARGS.leg == "msvc" else "-D"
        flags = tc.flags
        tc.flags = flags + [define + "main=density_program_main"]   # the generated C main
        (self.work / "density" / "table").mkdir()
        objs = tc.compile([stage / "cint-program.c"], self.work / "density" / "table", "density host")
        tc.flags = flags + [define + "REC_HOST_MAIN=cg_7_density_run"]
        objs += tc.compile([TESTS / "rec_host.c", stage / "density.c", ROOT / "rt" / "cint_rt.c",
                            *STATE_SOURCES], self.work / "density", "density host")
        tc.flags = flags
        compiled = tc.link_exe(objs, self.work / "density" / "rec_host", "density host")
        c = self.work / "density.c17.rec"
        code, out, err = state_host(compiled, "-", c, "-d", "density.ci=%s" % (stage / "density.sites"))
        self.assertEqual((code, out), (0, "total 10\nmain 0\nend 0\n"), err)
        code, text, err = interp(INTERPS[0][1], "show", c)
        self.assertEqual(code, 0, err)
        text = text.decode("ascii")
        self.assertIn("\ndensity dispatch\n", text)
        self.assertEqual(re.findall(r"^event \d+ (\w+)", text, re.M),
                         ["entry", "dispatch", "effect", "dispatch", "dispatch", "outcome", "end"], text)
        for label, host in RECS:
            with self.subTest(build=label):
                a = self.work / "density.interp.rec"
                code, out, err = state_host(host, stage, a, "-d")
                self.assertEqual((code, out), (0, "total 10\nmain 0\nend 0\n"), err)
                self.assertIn("domain 1 4 0 0 0\n", err, "the last dispatch, sum_of over [i: 4]")
                self.assertEqual(a.read_bytes(), c.read_bytes())
                code, out, err = state_host(compiled, "-", "-r", a)
                self.assertTrue(out.startswith("replay 0 - "), out + err)
                self.assertEqual(state_host(host, stage, "-r", c)[:2], (0, out))
                self.assertEqual(out.split()[4], "3", "dispatches 0 and 1, then the entry's end with dispatch 2")
                states = set()
                for stop in range(1, 4):
                    code, out, err = state_host(compiled, "-", "-r", a, stop)
                    self.assertEqual(out.split()[:2] + out.split()[4:5], ["replay", "0", str(stop)], out + err)
                    self.assertEqual(state_host(host, stage, "-r", c, stop)[:2], (0, out))
                    states.add(out.split("\n")[1])
                self.assertEqual(len(states), 3, out)

    def test_whitespace_compiled(self):
        """SPEC-09 SIR-17's second golden test and the whitespace test of SPEC-09 T4 (box 13 unit
        6), with B1: a blank line before `export` in add.ci leaves its revision identity
        ADD_REVISION and changes its source-map digest; and a checkpoint of
        module__state_two_modules taken before a blank line is added to its root module
        restores after it, since the revision identity does not change (SPEC-03 H-19)."""
        if not (ARGS.b1 or ARGS.bootstrap):
            self.skipTest("no B1 (--b1 PATH or --bootstrap)")
        tool = cint_check.b1_tool(ARGS.leg, ARGS.b1, pathlib.Path(ARGS.out) / "b1")["cint"]
        maps = []
        for name, edit in (("add", False), ("add_blank", True)):
            src = self.work / name / "src"
            src.mkdir(parents=True)
            text = (GOLDEN / "add.ci").read_bytes()
            (src / "add.ci").write_bytes(text.replace(b"\nexport ", b"\n\nexport ", 1) if edit else text)
            got = cint_check.run_b1(tool, src, ["add.ci"], self.work / name / "b1" / "out")[2]
            self.assertIsNotNone(got)
            modules = program(got[0])
            if not edit:
                self.assertEqual(modules, [("add.ci", (GOLDEN / "add.sir").read_bytes())], "B1 writes the golden text")
            maps.append(source_map(modules))
            self.assertEqual(interp(INTERPS[0][1], "identity", got[0]),
                             (0, b"revision %s\nsource-map %s\n" % (ADD_REVISION.encode(), maps[-1].encode()), ""))
        self.assertNotEqual(maps[0], maps[1], "the blank line moves the sites")
        root, rel = ROOT / "conformance", "module/state_two_modules.ci"
        order = cint_check.module_order(root, rel)
        src = self.work / "ws" / "src"
        for r in order:
            (src / r).parent.mkdir(parents=True, exist_ok=True)
            (src / r).write_bytes((b"\n" if r == rel else b"") + (root / r).read_bytes())
        got = cint_check.run_b1(tool, src, order, self.work / "ws" / "b1" / "out")[2]
        self.assertIsNotNone(got)
        edited = program(got[0])
        self.assertEqual(sir17_revision(edited), recorded_revisions()[STATE])
        self.assertNotEqual(source_map(edited), source_map(program(FIXTURES / STATE)))
        before, after = self.work / "before.ckpt", self.work / "after.ckpt"
        for label, host in HOSTS:
            with self.subTest(build=label):
                code, want, err = state_host(host, FIXTURES / STATE, before)
                self.assertEqual(code, 0, err)
                code, out, err = state_host(host, got[0], after, before)
                self.assertEqual((code, out), (0, want), err)
                self.assertIn("\nrestore 0\n", out)
                self.assertEqual(after.read_bytes(), before.read_bytes())

    def test_checkpoints_compiled_arrays(self):
        """Box 13 unit MLA on both backends: module-level arrays of I64, U8 and Bool, initialized and
        zero-filled, one of rank 2, that each call changes (interp/tests/mem/array_state.ci). The
        lines before the interrupt are the same on both, the checkpoints are byte-identical and each
        backend restores the other's, so the run after a restore returns what the run after the
        checkpoint returned; `show` lists each array with the SHA-256 of its elements; and a change
        of revision keeps the arrays whose element type and extent are unchanged and gives `last`,
        whose extent changed, its new initial value (rt/OPEN.md RT-OQ-49; SPEC-03 H-19, H-19b)."""
        if not (ARGS.b1 or ARGS.bootstrap):
            self.skipTest("no B1 (--b1 PATH or --bootstrap)")
        tool = cint_check.b1_tool(ARGS.leg, ARGS.b1, pathlib.Path(ARGS.out) / "b1")["cint"]
        got = cint_check.run_b1(tool, MEM, cint_check.module_order(MEM, "array_state.ci"),
                                self.work / "arrays-b1" / "out")[2]
        self.assertIsNotNone(got)
        stage = got[0]
        later = self.work / "arrays-later"   # the next revision: `last` has four elements
        later.mkdir(parents=True)
        (later / "array_state.ci").write_bytes((MEM / "array_state.ci").read_bytes().replace(
            b"I64[3] last = [7, 8, 9];", b"I64[4] last = [1, 2, 3, 4];"))
        got2 = cint_check.run_b1(tool, later, ["array_state.ci"], self.work / "arrays-b1-later" / "out")[2]
        self.assertIsNotNone(got2)
        (self.work / "arrays").mkdir()
        sanitize = ARGS.leg in ("gcc", "clang") and not ARGS.no_sanitize
        tc = cint_check.Toolchain(ARGS.leg, "0" if sanitize else "2", "portable", sanitize)
        objs = tc.compile([TESTS / "state_host.c", *sorted(stage.rglob("*.c")), ROOT / "rt" / "cint_rt.c",
                           *STATE_SOURCES], self.work / "arrays", "array host")
        compiled = tc.link_exe(objs, self.work / "arrays" / "state_host", "array host")
        a, b, c = (self.work / "arrays" / name for name in ("sir.ckpt", "c17.ckpt", "cross.ckpt"))
        code, want, err = state_host(compiled, "-", b)
        self.assertEqual(code, 0, err)
        self.assertNotRegex(err, "Sanitizer|runtime error")
        lines = want.split("\n")
        # call 1: last 8 9 1, hits 0 1 0 0, odd 0 0; call 2: last 9 1 2, hits 0 1 1 0, odd 1 0
        self.assertEqual([lines[k] for k in (0, 1, 2, 4, 5, 7, 8)],
                         ["bind 0", "run 0 891010000", "checkpoint 0", "run 0 912011010", "restore 0",
                          "run 0 912011010", ""], want)
        self.assertEqual(lines[6], lines[3], "the state after the restore is the checkpoint's")
        for label, host in HOSTS:
            with self.subTest(build=label):
                code, out, err = state_host(host, stage, a, a, got2[0])
                self.assertEqual((code, "\n".join(out.split("\n")[:8]) + "\n"), (0, want), err)
                self.assertNotRegex(err, "Sanitizer|runtime error")
                self.assertEqual(a.read_bytes(), b.read_bytes())
                self.assertEqual(state_host(compiled, "-", c, a)[:2], (0, want))
                code, cross, err = state_host(host, stage, c, b)
                self.assertEqual((code, "\n".join(cross.split("\n")[:8]) + "\n"), (0, want), err)
                # The interrupted call 3 counts itself and faults at the call of `shift`; call 4
                # leaves last 1 2 4, hits 1 1 1 0 and odd 0 0. The change keeps calls, hits and
                # odd and gives last its new initial value 1 2 3 4, so call 5 returns last 2 3 5,
                # hits 1 2 1 0 and odd 0 1; the old revision's checkpoint is refused.
                rest = out.split("\n")[8:]
                self.assertEqual(rest[:5], ["run 1 0", "interrupt code 9 site 0.8 limit I64 1", "clear 0",
                                            "run 0 124111000", "change 0"], out)
                self.assertEqual(rest[5:7], ["run 0 235121001", "restore-old 2"], out)
        code, shown, err = interp(INTERPS[0][1], "show", b)
        self.assertEqual((code, err), (0, ""))
        self.assertIn("global array_state.calls I64 1\n"
                      "global array_state.grid I64[2, 2] elements-sha256 %s\n"
                      "global array_state.hits U8[4] elements-sha256 %s\n"
                      "global array_state.last I64[3] elements-sha256 %s\n"
                      "global array_state.odd Bool[2] elements-sha256 %s\n"
                      % (cint_check.sha256(struct.pack("<4q", 0, 0, 0, 1)), cint_check.sha256(bytes([0, 1, 0, 0])),
                         cint_check.sha256(struct.pack("<3q", 8, 9, 1)), cint_check.sha256(bytes([0, 0]))),
                      shown.decode("ascii"))

    def test_checkpoints_compiled_structs(self):
        """Box 15 on both backends (decision on OQ-260; rt/OPEN.md RT-OQ-51): module-level structs
        with fields of rank 1 and 2, of U8 and of a struct array, initialized and zero-filled, that
        each call changes (interp/tests/mem/struct_state.ci). The lines before the interrupt are
        the same on both, the checkpoints are byte-identical and each backend restores the
        other's; `show` lists each struct with the SHA-256 of its payload; and a change of
        revision keeps the struct whose type is unchanged and gives `tail`, whose type changed,
        its new initial value (SPEC-03 H-19, H-19b)."""
        if not (ARGS.b1 or ARGS.bootstrap):
            self.skipTest("no B1 (--b1 PATH or --bootstrap)")
        tool = cint_check.b1_tool(ARGS.leg, ARGS.b1, pathlib.Path(ARGS.out) / "b1")["cint"]
        got = cint_check.run_b1(tool, MEM, cint_check.module_order(MEM, "struct_state.ci"),
                                self.work / "structs-b1" / "out")[2]
        self.assertIsNotNone(got)
        stage = got[0]
        later = self.work / "structs-later"   # the next revision: `tail` has a third field
        later.mkdir(parents=True)
        (later / "struct_state.ci").write_bytes((MEM / "struct_state.ci").read_bytes().replace(
            b"struct Tail { I64 n; I64 sq; }", b"struct Tail { I64 n; I64 sq; I64 cube; }").replace(
            b"Tail tail;", b"Tail tail = Tail(1, 2, 3);"))
        got2 = cint_check.run_b1(tool, later, ["struct_state.ci"], self.work / "structs-b1-later" / "out")[2]
        self.assertIsNotNone(got2)
        (self.work / "structs").mkdir()
        sanitize = ARGS.leg in ("gcc", "clang") and not ARGS.no_sanitize
        tc = cint_check.Toolchain(ARGS.leg, "0" if sanitize else "2", "portable", sanitize)
        objs = tc.compile([TESTS / "state_host.c", *sorted(stage.rglob("*.c")), ROOT / "rt" / "cint_rt.c",
                           *STATE_SOURCES], self.work / "structs", "struct host")
        compiled = tc.link_exe(objs, self.work / "structs" / "state_host", "struct host")
        a, b, c = (self.work / "structs" / name for name in ("sir.ckpt", "c17.ckpt", "cross.ckpt"))
        code, want, err = state_host(compiled, "-", b)
        self.assertEqual(code, 0, err)
        self.assertNotRegex(err, "Sanitizer|runtime error")
        lines = want.split("\n")
        # call 1: last 8 9 1, hits 0 1 0 0, odd 0 0, tail 1; call 2: last 9 1 2, hits 0 1 1 0,
        # odd 1 0, tail 3
        self.assertEqual([lines[k] for k in (0, 1, 2, 4, 5, 7, 8)],
                         ["bind 0", "run 0 8910100001", "checkpoint 0", "run 0 9120110103", "restore 0",
                          "run 0 9120110103", ""], want)
        self.assertEqual(lines[6], lines[3], "the state after the restore is the checkpoint's")
        for label, host in HOSTS:
            with self.subTest(build=label):
                code, out, err = state_host(host, stage, a, a, got2[0])
                self.assertEqual((code, "\n".join(out.split("\n")[:8]) + "\n"), (0, want), err)
                self.assertNotRegex(err, "Sanitizer|runtime error")
                self.assertEqual(a.read_bytes(), b.read_bytes())
                self.assertEqual(state_host(compiled, "-", c, a)[:2], (0, want))
                code, cross, err = state_host(host, stage, c, b)
                self.assertEqual((code, "\n".join(cross.split("\n")[:8]) + "\n"), (0, want), err)
                # The interrupted call 3 counts itself and faults at the call of `shift`; call 4
                # leaves last 1 2 4, hits 1 1 1 0, odd 0 0 and tail 7. The change keeps calls and
                # track and gives tail its new initial value (1, 2, 3), so call 5 returns last
                # 2 4 5, hits 1 2 1 0, odd 0 1 and tail 6; the old revision's checkpoint is refused.
                rest = out.split("\n")[8:]
                self.assertEqual(rest[:5], ["run 1 0", "interrupt code 9 site 0.8 limit I64 1",
                                            "clear 0", "run 0 1241110007", "change 0"], out)
                self.assertEqual(rest[5:7], ["run 0 2451210016", "restore-old 2"], out)
        code, shown, err = interp(INTERPS[0][1], "show", b)
        self.assertEqual((code, err), (0, ""))
        track = (struct.pack("<3q", 8, 9, 1) + bytes([0, 1, 0, 0]) + struct.pack("<q", 0) + bytes([0]) +
                 struct.pack("<q", 1) + bytes([0]) + struct.pack("<4q", 0, 0, 0, 1))
        self.assertIn("global struct_state.calls I64 1\n"
                      "global struct_state.tail struct_state.Tail payload-sha256 %s\n"
                      "global struct_state.track struct_state.Track payload-sha256 %s\n"
                      % (cint_check.sha256(struct.pack("<2q", 1, 1)), cint_check.sha256(track)),
                      shown.decode("ascii"))

    def test_checkpoints_compiled_wide_structs(self):
        """Box 15 on both backends (rt/OPEN.md RT-OQ-51): module-level structs with fields of
        I128, I256 (an array), I512 (in a nested struct) and I1024, one initialized and one
        zero-filled, that each call changes (interp/tests/mem/wide_struct_state.ci). The
        interpreter keeps a wide field in a cell per limb, and the lines before the interrupt
        are the same on both backends, the checkpoints are byte-identical, each backend restores
        the other's, and `show` lists each struct with the SHA-256 of its payload, a wide field
        at its width."""
        if not (ARGS.b1 or ARGS.bootstrap):
            self.skipTest("no B1 (--b1 PATH or --bootstrap)")
        tool = cint_check.b1_tool(ARGS.leg, ARGS.b1, pathlib.Path(ARGS.out) / "b1")["cint"]
        got = cint_check.run_b1(tool, MEM, cint_check.module_order(MEM, "wide_struct_state.ci"),
                                self.work / "wide-structs-b1" / "out")[2]
        self.assertIsNotNone(got)
        stage = got[0]
        (self.work / "wide-structs").mkdir()
        sanitize = ARGS.leg in ("gcc", "clang") and not ARGS.no_sanitize
        tc = cint_check.Toolchain(ARGS.leg, "0" if sanitize else "2", "portable", sanitize)
        objs = tc.compile([TESTS / "state_host.c", *sorted(stage.rglob("*.c")), ROOT / "rt" / "cint_rt.c",
                           *STATE_SOURCES], self.work / "wide-structs", "wide struct host")
        compiled = tc.link_exe(objs, self.work / "wide-structs" / "state_host", "wide struct host")
        a, b, c = (self.work / "wide-structs" / name for name in ("sir.ckpt", "c17.ckpt", "cross.ckpt"))
        code, want, err = state_host(compiled, "-", b)
        self.assertEqual(code, 0, err)
        self.assertNotRegex(err, "Sanitizer|runtime error")
        # The program's two calls, computed here; the state after call 1 is the checkpoint's.
        acc = {"lo": -5 * 2 ** 64, "pair": [2 ** 200, -9], "big": 2 ** 300, "odd": True, "n": 3}
        zero = {"sum": 0, "k": 0}
        runs = []
        for calls in (1, 2):
            acc["lo"] = acc["lo"] * 3 + calls
            acc["pair"][calls % 2] = -acc["pair"][calls % 2]
            acc["big"] -= calls
            acc["odd"] = not acc["odd"]
            acc["n"] += calls
            zero["sum"] += acc["lo"] << 900
            zero["k"] += 1
            d = acc["lo"] % 1000   # floor division, as Python's (SPEC-01 IM-35)
            for v in acc["pair"]:
                d = d * 10 + (1 if v < 0 else 2)
            d = (d * 1000 + acc["big"] % 1000) * 10 + int(acc["odd"])
            runs.append(d * 1000 + (zero["sum"] >> 950) % 1000)
            if calls == 1:
                payload = (twos(acc["lo"], 128) + twos(acc["pair"][0], 256) + twos(acc["pair"][1], 256) +
                           twos(acc["big"], 512) + bytes([acc["odd"]]) + struct.pack("<q", acc["n"]),
                           twos(zero["sum"], 1024) + struct.pack("<i", zero["k"]))
        lines = want.split("\n")
        self.assertEqual([lines[k] for k in (0, 1, 2, 4, 5, 7, 8)],
                         ["bind 0", "run 0 %d" % runs[0], "checkpoint 0", "run 0 %d" % runs[1], "restore 0",
                          "run 0 %d" % runs[1], ""], want)
        self.assertRegex(lines[3], "^hash 0 [0-9a-f]{64}$")
        self.assertEqual(lines[6], lines[3], "the state after the restore is the checkpoint's")
        for label, host in HOSTS:
            with self.subTest(build=label):
                code, out, err = state_host(host, stage, a)
                self.assertEqual((code, "\n".join(out.split("\n")[:8]) + "\n"), (0, want), err)
                self.assertNotRegex(err, "Sanitizer|runtime error")
                self.assertEqual(a.read_bytes(), b.read_bytes())
                self.assertEqual(state_host(compiled, "-", c, a)[:2], (0, want))
                code, cross, err = state_host(host, stage, c, b)
                self.assertEqual((code, "\n".join(cross.split("\n")[:8]) + "\n"), (0, want), err)
        code, shown, err = interp(INTERPS[0][1], "show", b)
        self.assertEqual((code, err), (0, ""))
        self.assertIn("global wide_struct_state.acc wide_struct_state.Acc payload-sha256 %s\n"
                      "global wide_struct_state.calls I64 1\n"
                      "global wide_struct_state.zero wide_struct_state.Zero payload-sha256 %s\n"
                      % (cint_check.sha256(payload[0]), cint_check.sha256(payload[1])), shown.decode("ascii"))

    def test_checkpoints_compiled_handles(self):
        """Module-level handles (decision on OQ-242; rt/OPEN.md RT-OQ-50) on both backends: a pool
        slot's, an arena allocation's and one that starts null, which each call changes
        (interp/tests/mem/handle_state.ci). The lines are the same on both, the checkpoints are
        byte-identical and each backend restores the other's, so the run after a restore returns
        what the run after the checkpoint returned; `show` lists each handle with its container
        and fields, and the null handle as null. On cpu-c17 a change into the same revision carries
        the pool, the arena and the handles (decision 2026-10-08 on OQ-243)."""
        if not (ARGS.b1 or ARGS.bootstrap):
            self.skipTest("no B1 (--b1 PATH or --bootstrap)")
        tool = cint_check.b1_tool(ARGS.leg, ARGS.b1, pathlib.Path(ARGS.out) / "b1")["cint"]
        got = cint_check.run_b1(tool, MEM, cint_check.module_order(MEM, "handle_state.ci"),
                                self.work / "handles-b1" / "out")[2]
        self.assertIsNotNone(got)
        stage = got[0]
        (self.work / "handles").mkdir()
        sanitize = ARGS.leg in ("gcc", "clang") and not ARGS.no_sanitize
        tc = cint_check.Toolchain(ARGS.leg, "0" if sanitize else "2", "portable", sanitize)
        objs = tc.compile([TESTS / "state_host.c", *sorted(stage.rglob("*.c")), ROOT / "rt" / "cint_rt.c",
                           *STATE_SOURCES], self.work / "handles", "handle host")
        compiled = tc.link_exe(objs, self.work / "handles" / "state_host", "handle host")
        a, b, c = (self.work / "handles" / name for name in ("sir.ckpt", "c17.ckpt", "cross.ckpt"))
        code, want, err = state_host(compiled, "-", b)
        self.assertEqual(code, 0, err)
        self.assertNotRegex(err, "Sanitizer|runtime error")
        lines = want.split("\n")
        # call 1: newest 10, span [1]; call 2: oldest 10, newest 20, span [2, 2]
        self.assertEqual([lines[k] for k in (0, 1, 2, 4, 5, 7, 8)],
                         ["bind 0", "run 0 1001", "checkpoint 0", "run 0 102004", "restore 0", "run 0 102004", ""],
                         want)
        self.assertEqual(lines[6], lines[3], "the state after the restore is the checkpoint's")
        # A change into the same revision carries the pool, the arena and the handles (OQ-243):
        # call 3 removes the slot of 10 and returns 20, 30 and 3 + 3 + 3.
        code, out, err = state_host(compiled, "-", c, b, "change")
        self.assertEqual((code, out.split("\n")[8:]), (0, ["change 0", "run 0 203009", ""]), err)
        self.assertNotRegex(err, "Sanitizer|runtime error")
        for label, host in HOSTS:
            with self.subTest(build=label):
                code, out, err = state_host(host, stage, a)
                self.assertEqual((code, "\n".join(out.split("\n")[:8]) + "\n"), (0, want), err)
                self.assertNotRegex(err, "Sanitizer|runtime error")
                self.assertEqual(a.read_bytes(), b.read_bytes())
                self.assertEqual(state_host(compiled, "-", c, a)[:2], (0, want))
                code, cross, err = state_host(host, stage, c, b)
                self.assertEqual((code, "\n".join(cross.split("\n")[:8]) + "\n"), (0, want), err)
        code, shown, err = interp(INTERPS[0][1], "show", b)
        self.assertEqual((code, err), (0, ""))
        self.assertIn("global handle_state.calls I64 1\n"
                      "global handle_state.newest Handle pool 1 slot 0 length 1 generation 0\n"
                      "global handle_state.oldest Handle null\n"
                      "global handle_state.span Handle arena 1 slot 0 length 1 generation 0\n",
                      shown.decode("ascii"))

    def test_revision_handles(self):
        """The change of revision carries arenas and pools (decision 2026-10-08 on OQ-243; SPEC-03
        H-19b; rt/OPEN.md RT-OQ-46) on cpu-sir-interp (interp/tests/state_host.c with DIR2), from
        interp/tests/mem/handle_state.ci to a revision that declares a new pool `spare` before
        `jobs`, so that `jobs` becomes pool 2: its slots, the arena `trail` and the handles carry
        over, and the run after the change returns what the run after a change into the same
        revision returns. A revision whose `jobs` holds three elements drops it, and the handle
        `oldest` into it then faults as stale."""
        if not (ARGS.b1 or ARGS.bootstrap):
            self.skipTest("no B1 (--b1 PATH or --bootstrap)")
        tool = cint_check.b1_tool(ARGS.leg, ARGS.b1, pathlib.Path(ARGS.out) / "b1")["cint"]
        text = (MEM / "handle_state.ci").read_bytes()
        stages = [cint_check.run_b1(tool, MEM, ["handle_state.ci"], self.work / "rev1" / "out")[2]]
        for name, new in (("rev2", text.replace(b"Pool(I64, 2) jobs;", b"Pool(I64, 4) spare;\nPool(I64, 2) jobs;")),
                          ("rev3", text.replace(b"Pool(I64, 2) jobs;", b"Pool(I64, 3) jobs;"))):
            self.assertNotEqual(new, text)
            (self.work / (name + "-src")).mkdir()  # run_b1 empties the parent of its output
            (self.work / (name + "-src") / "handle_state.ci").write_bytes(new)
            stages.append(cint_check.run_b1(tool, self.work / (name + "-src"), ["handle_state.ci"],
                                            self.work / name / "out")[2])
        self.assertTrue(all(got is not None for got in stages))
        for label, host in HOSTS:
            with self.subTest(build=label):
                runs = []
                for got in stages:
                    code, out, err = state_host(host, stages[0][0], self.work / "rev.ckpt", self.work / "rev.ckpt", got[0])
                    self.assertEqual(code, 0, err)
                    self.assertNotRegex(err, "Sanitizer|runtime error")
                    lines = out.split("\n")
                    at = lines.index("change 0") if "change 0" in lines else len(lines) - 1
                    runs.append(lines[at:at + 2])
                self.assertEqual(runs[0][0], "change 0", "a change into the same revision keeps everything")
                self.assertRegex(runs[0][1], "^run 0 [0-9]+$")
                self.assertEqual(runs[1], runs[0], "spare is new and empty; jobs, trail and the handles carry over")
                self.assertEqual(runs[2], ["change 0", "run 1 0"], "jobs of another capacity is dropped: CINT_FAULT")

    def test_dispatch_density_arrays(self):
        """Box 13 unit MLA at `dispatch` density (interp/tests/mem/tally.ci): the entry writes a
        module-level array before each dispatch, and the interpreter, which keeps the array in
        its cells, writes them back to the module's block at each boundary as compiled C has
        stored them there, so both backends record the same bytes and stop at each boundary
        with the same state (SPEC-06 1.5; rt/OPEN.md RT-OQ-49)."""
        if not (ARGS.b1 or ARGS.bootstrap):
            self.skipTest("no B1 (--b1 PATH or --bootstrap)")
        tool = cint_check.b1_tool(ARGS.leg, ARGS.b1, pathlib.Path(ARGS.out) / "b1")["cint"]
        got = cint_check.run_b1(tool, MEM, ["tally.ci"], self.work / "tally" / "out")[2]
        self.assertIsNotNone(got)
        stage = got[0]
        sanitize = ARGS.leg in ("gcc", "clang") and not ARGS.no_sanitize
        tc = cint_check.Toolchain(ARGS.leg, "0" if sanitize else "2", "portable", sanitize)
        define = "/D" if ARGS.leg == "msvc" else "-D"
        flags = tc.flags
        tc.flags = flags + [define + "main=tally_program_main"]   # the generated C main
        (self.work / "tally" / "table").mkdir()
        objs = tc.compile([stage / "cint-program.c"], self.work / "tally" / "table", "tally host")
        tc.flags = flags + [define + "REC_HOST_MAIN=cg_5_tally_run"]
        objs += tc.compile([TESTS / "rec_host.c", stage / "tally.c", ROOT / "rt" / "cint_rt.c",
                            *STATE_SOURCES], self.work / "tally", "tally host")
        tc.flags = flags
        compiled = tc.link_exe(objs, self.work / "tally" / "rec_host", "tally host")
        c = self.work / "tally.c17.rec"
        code, out, err = state_host(compiled, "-", c, "-d", "tally.ci=%s" % (stage / "tally.sites"))
        self.assertEqual((code, out), (0, "total 9\nmain 0\nend 0\n"), err)
        for label, host in RECS:
            with self.subTest(build=label):
                a = self.work / "tally.interp.rec"
                self.assertEqual(state_host(host, stage, a, "-d")[:2], (0, "total 9\nmain 0\nend 0\n"))
                self.assertEqual(a.read_bytes(), c.read_bytes())
                states = set()
                for stop in range(1, 4):
                    code, out, err = state_host(compiled, "-", "-r", a, stop)
                    self.assertEqual(out.split()[:2] + out.split()[4:5], ["replay", "0", str(stop)], out + err)
                    self.assertEqual(state_host(host, stage, "-r", c, stop)[:2], (0, out))
                    states.add(out.split("\n")[1])
                self.assertEqual(len(states), 3, out)

    def test_b1_agreement(self):
        """Every conformance program B1 emits, and cintc's own sources: the reader's
        revision identity equals the program file's cg_revision."""
        if not (ARGS.b1 or ARGS.bootstrap):
            self.skipTest("no B1 (--b1 PATH or --bootstrap)")
        tool = cint_check.b1_tool(ARGS.leg, ARGS.b1, pathlib.Path(ARGS.out) / "b1")["cint"]
        conformance = ROOT / "conformance"
        jobs = [(conformance, p.relative_to(conformance).as_posix()) for p in sorted(conformance.glob("*/*.ci"))]
        jobs.append((ROOT / "compiler", "main.ci"))

        def emit(job):
            root, rel = job
            work = self.work / "b1" / rel.replace("/", "__")
            got = cint_check.run_b1(tool, root, cint_check.module_order(root, rel), work / "out")[2]
            return rel, work, got[0] if got else None

        read = 0
        with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 1) as pool:
            for rel, work, stage in pool.map(emit, jobs):
                if stage is None:
                    shutil.rmtree(work, ignore_errors=True)
                    continue
                want = program_revision((stage / "cint-program.c").read_bytes())
                for label, exe in BUILDS:
                    with self.subTest(build=label, program=rel):
                        code, out, err = run(exe, stage)
                        self.assertEqual((code, err), (0, ""), out)
                        self.assertTrue(out.startswith("revision %s\n" % want), out)
                read += 1
                shutil.rmtree(work, ignore_errors=True)
        print("b1 agreement: %d programs read, of %d" % (read, len(jobs)), file=sys.stderr)

    def test_b1_probes(self):
        """Each program under interp/tests/probes/, emitted by B1, on cint-interp: the
        output equals cint_ref's for its case header (box 13 unit 7)."""
        if not (ARGS.b1 or ARGS.bootstrap):
            self.skipTest("no B1 (--b1 PATH or --bootstrap)")
        tool = cint_check.b1_tool(ARGS.leg, ARGS.b1, pathlib.Path(ARGS.out) / "b1")["cint"]
        probes = sorted(PROBES.glob("*.ci"))
        self.assertTrue(probes)
        for ci in probes:
            h = gen_expect.header(ci.read_text(encoding="utf-8"))
            rel = "probes/" + ci.name
            r = subprocess.run([sys.executable, "-B", "-m", "cint_ref", "run", str(ci), "--case", h["case"], "--clause",
                                h["clause"], "--entry", h["entries"][0], "--path", rel], cwd=ROOT / "ref",
                               capture_output=True)
            ref = r.stdout
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertNotIn(b"\noutcome compile-error\n", ref, rel)
            got = cint_check.run_b1(tool, TESTS, cint_check.module_order(TESTS, rel), self.work / "probes" / ci.stem / "out")
            self.assertIsNotNone(got[2], "%s: %s" % (rel, got[1]))
            for label, exe in INTERPS:
                with self.subTest(build=label, program=rel):
                    code, out, err = interp(exe, "observe", got[2][0], "--case", h["case"], "--entry",
                                            h["entries"][0], "--clause", h["clause"])
                    self.assertEqual((code, out.decode("ascii")), (0, ref.decode("ascii")), err)
                    self.assertNotRegex(err, "Sanitizer|runtime error")

    def test_b1_wide_forms(self):
        """Box 15 forms no conformance case holds, emitted by B1 from compiler/tests/, on
        cint-interp: the calls of wide_reductions.ci (WIDE_REDUCTIONS: narrow elements into a wide
        result, wide elements into a narrow one, min and max with an initial value over empty and
        rank-2 views, sum_wrap and sum_sat over rank 3, dot's shape fault, and kernel reductions
        with their faults, in format 4), and the scripts wide_print.ci (format specifications on
        wide values) and wide_assert.ci (a failed assert of I512 operands; rt/OPEN.md RT-OQ-55)
        give cint_ref's outcome, with and without --trace."""
        if not (ARGS.b1 or ARGS.bootstrap):
            self.skipTest("no B1 (--b1 PATH or --bootstrap)")
        tool = cint_check.b1_tool(ARGS.leg, ARGS.b1, pathlib.Path(ARGS.out) / "b1")["cint"]
        stages = {}
        for name in ("wide_reductions", "wide_print", "wide_assert"):
            got = cint_check.run_b1(tool, COMPILER_TESTS, cint_check.module_order(COMPILER_TESTS, name + ".ci"),
                                    self.work / "wide-forms" / name / "out")
            self.assertIsNotNone(got[2], "%s: %s" % (name, got[1]))
            stages[name] = got[2][0]
        calls = [(fn, args) for fn, values in WIDE_REDUCTIONS for args in values]
        cases = self.work / "wide-forms" / "cases.txt"
        cases.write_bytes("".join(" ".join(["wide_reductions", fn] + ["I64 %d" % a for a in args]) + "\n"
                                  for fn, args in calls).encode("ascii"))
        want = cint_check.ref_bodies((COMPILER_TESTS / "wide_reductions.ci").read_bytes(), "wide_reductions.ci",
                                     [fn for fn, _ in calls],
                                     [[cint_check.Value("I64", a) for a in args] for _, args in calls], 4)
        self.assertTrue(any(w[0] == "outcome fault" for w in want) and any(w[0] == "outcome value" for w in want))
        scripts = {}
        for name in ("wide_print", "wide_assert"):
            r = subprocess.run([sys.executable, "-B", "-m", "cint_ref", "run", str(COMPILER_TESTS / (name + ".ci")),
                                "--path", name + ".ci"], cwd=ROOT / "ref", capture_output=True)
            lines = r.stdout.decode("ascii").rstrip("\n").split("\n")
            self.assertTrue(any(line.startswith("outcome ") for line in lines), r.stderr)
            scripts[name] = lines[next(i for i, line in enumerate(lines) if line.startswith("outcome ")):]
        for (label, exe), pre in ((i, p) for i in INTERPS for p in ((), ("--trace", os.devnull))):
            with self.subTest(build=label, program="wide_reductions", trace=bool(pre)):
                code, out, err = interp(exe, *pre, "observe", stages["wide_reductions"], "--cases", cases,
                                        "--format", "4", "--depth", cint_check.DEFAULT_DEPTH)
                self.assertEqual(code, 0, err)
                for call, got, w in zip(calls, cint_check.parse_harness(out, len(calls)), want):
                    self.assertEqual(got, w, call)
                self.assertNotRegex(err, "Sanitizer|runtime error")
            for name, w in scripts.items():
                with self.subTest(build=label, program=name, trace=bool(pre)):
                    code, got, err = run_entry(exe, stages[name], self.work / "wide-forms" / name, pre=pre)
                    self.assertEqual(got, w, err)
                    self.assertNotRegex(err, "Sanitizer|runtime error")

    def test_b1_fixed_point(self):
        """Box 15 units 4e1 and 4e2 (compiler/OPEN.md): B1's golden script fixed_point.ci, which
        writes every fixed-point form of B1 and ends at a fault of fixed-point operands, gives
        cint_ref's outcome on cpu-sir-interp in format 5, with and without --trace."""
        if not (ARGS.b1 or ARGS.bootstrap):
            self.skipTest("no B1 (--b1 PATH or --bootstrap)")
        if not (COMPILER_TESTS / "fixed_point.ci").is_file():
            self.skipTest("no compiler/tests/fixed_point.ci before box 15 unit 4e1")
        tool = cint_check.b1_tool(ARGS.leg, ARGS.b1, pathlib.Path(ARGS.out) / "b1")["cint"]
        got = cint_check.run_b1(tool, COMPILER_TESTS, cint_check.module_order(COMPILER_TESTS, "fixed_point.ci"),
                                self.work / "fixed-point" / "out")
        self.assertIsNotNone(got[2], got[1])
        r = subprocess.run([sys.executable, "-B", "-m", "cint_ref", "run", str(COMPILER_TESTS / "fixed_point.ci"),
                            "--path", "fixed_point.ci", "--format", "5"], cwd=ROOT / "ref", capture_output=True)
        lines = r.stdout.decode("ascii").rstrip("\n").split("\n")
        self.assertIn("outcome fault", lines, r.stderr)
        want = lines[lines.index("outcome fault"):]
        for (label, exe), pre in ((i, p) for i in INTERPS for p in ((), ("--trace", os.devnull))):
            with self.subTest(build=label, trace=bool(pre)):
                code, have, err = run_entry(exe, got[2][0], self.work / "fixed-point", 5, pre)
                self.assertEqual(have, want, err)
                self.assertNotRegex(err, "Sanitizer|runtime error")


def base_program(name):
    if name == "add":
        return [("add.ci", (GOLDEN / "add.sir").read_bytes())]
    return program(fixture(name))


def apply(text, edit):
    if edit is None or text is None:
        return text
    if callable(edit):
        return edit(text)
    old, new = edit
    if old not in text:
        raise AssertionError("edit does not apply: %r" % old)
    return text.replace(old, new, 1)


def mutate(rng, sir: bytes) -> bytes:
    lines = sir.split(b"\n")
    choice = rng.randrange(6)
    if choice == 0 and sir:
        at = rng.randrange(len(sir))
        return sir[:at] + bytes([rng.randrange(256)]) + sir[at + 1:]
    if choice == 1 and sir:
        at = rng.randrange(len(sir))
        return sir[:at] + sir[at + 1:]
    if choice == 2:
        at = rng.randrange(len(lines))
        return b"\n".join(lines[:at] + lines[at + 1:])
    if choice == 3:
        a, b = rng.randrange(len(lines)), rng.randrange(len(lines))
        lines[a], lines[b] = lines[b], lines[a]
        return b"\n".join(lines)
    if choice == 4:
        at = rng.randrange(len(lines))
        return b"\n".join(lines[:at] + [lines[at]] + lines[at:])
    tokens = re.split(rb"( )", sir)
    at = rng.randrange(len(tokens))
    tokens[at] = rng.choice([b"%t0", b"%t99", b"I8", b"U64", b"-1", b"9223372036854775808", b"{", b"}",
                             b"L0", b"$sc.0", b"@x", b"#1", b"\"", b"\\x", b""])
    return b"".join(tokens)


def build(leg: str, out: pathlib.Path, sanitize: bool):
    plans = [("O2", "2", False)] + ([("O0-sanitize", "0", True)] if sanitize and leg in ("gcc", "clang") else [])
    for label, opt, sanitize in plans:
        tc = cint_check.Toolchain(leg, opt, "portable", sanitize)
        directory = out / label
        shutil.rmtree(directory, ignore_errors=True)
        directory.mkdir(parents=True)
        read, check, run_, main_, rt, *state = tc.compile(SOURCES, directory, "interp " + label)
        BUILDS.append((label, tc.link_exe([read, check, rt], directory / "sir_check", "interp " + label)))
        INTERPS.append((label, tc.link_exe([read, run_, main_, rt, *state], directory / "cint-interp",
                                           "interp " + label)))
        define = "/D" if leg == "msvc" else "-D"
        flags, tc.flags = tc.flags, tc.flags + [define + "STATE_HOST_SIR"]
        (directory / "host").mkdir()
        host = tc.compile([TESTS / "state_host.c"], directory / "host", "interp " + label)
        tc.flags = flags + [define + "REC_HOST_SIR"]
        (directory / "rec").mkdir()
        rec = tc.compile([TESTS / "rec_host.c"], directory / "rec", "interp " + label)
        tc.flags = flags
        HOSTS.append((label, tc.link_exe([read, run_, rt, *state] + host, directory / "state_host", "interp " + label)))
        RECS.append((label, tc.link_exe([read, run_, rt, *state] + rec, directory / "rec_host", "interp " + label)))
        print("built %s: %s" % (label, " ".join(tc.flags)), file=sys.stderr)


def case_receipt(case: pathlib.Path, receipt: pathlib.Path) -> int:
    """--case-receipt: the case of SPEC-09 T4 on another machine (box 13 default BX13-13). The
    case is interp/tests/rec's program entry, saved by `run --case` unless CASE exists; its
    replay writes RECEIPT, and the receipt identity printed is the same on every machine."""
    exe = INTERPS[0][1]
    if not case.exists():
        code, out, err = interp(exe, "run", "--case", case, "--source", REC, REC / "sir")
        if code != 0:
            sys.stderr.write(err)
            return 1
    code, out, err = interp(exe, "replay", "--receipt", receipt, "--source-identity", interp_source_identity(), case)
    sys.stderr.write(err)
    if code != 0:
        return 1
    r = json.loads(receipt.read_bytes())
    print("case %s\nreceipt-identity %s\nstatus %s chain %s" % (
        cint_check.sha256(case.read_bytes()), r["observations"]["receipt_identity_sha256"], r["outcome"]["status"],
        r["outcome"]["chain_head"]))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    host = "msvc" if os.name == "nt" else "apple-clang" if sys.platform == "darwin" else "gcc"
    parser.add_argument("--leg", choices=("msvc", "gcc", "clang", "apple-clang"), default=host)
    parser.add_argument("--out", help="build directory (default <build>/interp/<leg>)")
    parser.add_argument("--fuzz", type=int, default=200, help="fuzz inputs (default 200)")
    parser.add_argument("--seed", type=int, help="fuzz seed (default: a fresh one, printed)")
    parser.add_argument("--b1", help="a B1 cint executable for the agreement test")
    parser.add_argument("--bootstrap", action="store_true", help="bootstrap B1 for the agreement test")
    parser.add_argument("--no-sanitize", action="store_true", help="leave out the sanitized build")
    parser.add_argument("--case-receipt", nargs=2, metavar=("CASE", "RECEIPT"),
                        help="replay CASE (saved first when it does not exist) with a receipt and print the "
                             "receipt identity, instead of the tests (SPEC-09 T4 on another machine)")
    args, rest = parser.parse_known_args()
    ARGS.__dict__.update(vars(args))
    ARGS.out = str(pathlib.Path(args.out) if args.out else cint_check.build_root() / "interp" / args.leg)
    pathlib.Path(ARGS.out).mkdir(parents=True, exist_ok=True)
    build(args.leg, pathlib.Path(ARGS.out), not args.no_sanitize)
    if args.case_receipt:
        return case_receipt(*(pathlib.Path(p).resolve() for p in args.case_receipt))
    result = unittest.main(argv=[sys.argv[0], *rest], exit=False, verbosity=2).result
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
