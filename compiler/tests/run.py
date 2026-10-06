"""Builds compiler modules with the seed and calls their test entries.

    python compiler/tests/run.py --leg msvc|gcc|clang|apple-clang --suite foundations|front|check|fold|golden|capacity|ptx
        [--opt 0|2] [--helpers portable|builtin] [--sanitize | --no-sanitize] [--out DIR]

Slice 2 plan tasks 2.9 (suite foundations), 2.10a and 2.10b (suite front), 2.10b alone
(suite fold), and 2.11 (suite golden: compiler/main.ci through the bridge,
golden_suite.py). The harness reaches only scalar-only exports (SPEC-09 CONF-13), and
cint-boot-1 has no module variables and local arrays of at most 4,096 bytes (EMIT-30), so
each module under compiler/ exports test entries that build their inputs in a loop and
return one scalar; this driver computes every expected value in Python (hashlib for SHA-256, cint_ref and the frozen files for
fold.ci in compiler/tests/fold_model.py, and a model of each rule for the others) and
compares.

For each configuration it builds the runtime, the seed, and the harness with
tools/cint_check.py's toolchain flags (warning-free under MSVC /W4 /WX and GCC and Clang
-Wall -Wextra -Wconversion -Wshadow -Werror -Wpedantic), compiles each module of the
suite as the root module with `cint-seed --root compiler`, links it with the runtime as a
shared library, and runs its case list with `cint-harness --cases`. A case passes when the
harness prints the expected outcome; a configuration also fails a case for a sanitizer
report, a harness exit status other than 0, or emitted C that differs between
configurations or holds a CR byte.

The default configurations are those of the T1 matrix for the leg: -O0 and -O2, both
helper modes, and on gcc and clang each with and without ASan and UBSan (apple-clang, on
macOS, as msvc: unsanitized; box 11 unit 3 runs the ptx suite there). --opt,
--helpers, --sanitize and --no-sanitize narrow them. On Windows the gcc and clang legs
run in WSL (Ubuntu-24.04). Build outputs go under <build>/compiler/<suite> (never in the
repository), where <build> is the CINT_BUILD environment variable or cint-build in the system
temporary directory. The last line is `<suite>: <passed> of <cases> pass`, counted over every
configuration (`golden: <n> of <cases> identical` for the golden suite, whose items are
byte-for-byte comparisons).

A suite is a list of parts. A harness part is a module with scalar test entries, as
above. A host part runs a C host built with a module's emitted C, for test entries that
take views (slice 2 task 2.10a: the `parse` part of the `front` suite, front_suite.py).
A part names the sources it needs; when one is absent (a task that has not landed yet)
the part is skipped and says so, and the suite runs the parts that are present.
"""
import argparse
import hashlib
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
COMPILER = ROOT / "compiler"
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(COMPILER / "tests"))

import cint_check as cc  # noqa: E402  (toolchains, the seed and harness builds)
import front_suite  # noqa: E402  (the parse part of the front suite, task 2.10a)
import import_names
import check_suite  # noqa: E402  (the check part of the front suite, task 2.10c)
import decl_capacity_suite  # noqa: E402  (checked declaration-view capacity cases)
import parse_capacity_suite  # noqa: E402  (physical parser-node capacity cases)
import table_capacity_suite  # noqa: E402  (module tables at their capacity boundary, OQ-179)
import k_probes_suite  # noqa: E402  (the K reduction probes and lemmas N, S and W, KR-11)
import fold_model  # noqa: E402  (the fold suite: compiler/fold.ci against cint_ref)
import golden_suite  # noqa: E402  (the golden suite: task 2.11)
import ptx_suite  # noqa: E402  (the ptx suite: compiler/back_ptx.ci, box 11 unit 3)

I64_MIN = -(1 << 63)


def wrap_i64(v: int) -> int:
    v &= (1 << 64) - 1
    return v - (1 << 64) if v >> 63 else v


def value(type_name: str, v: int) -> list:
    return ["outcome value", "return %s %d" % (type_name, v)]


def fault(code: str) -> list:
    return ["outcome fault", "fault.code " + code]


# ------------------------------------------------------------------------------ sha256.ci

def sha_message(vector: int) -> bytes:
    if vector == 0:
        return b""
    if vector == 1 or vector == 5:
        return b"abc"
    if vector == 2:
        return b"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
    if vector == 3:
        return b"a" * 1000000
    if vector == 4:
        return (b"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopq"
                b"klmnopqrlmnopqrsmnopqrstnopqrstu")
    n = vector - 100 if vector < 1000 else vector - 1000
    return bytes((31 * i + n) % 256 for i in range(n))


# FIPS 180-4 example digests, checked against hashlib before any case runs.
SHA_KNOWN = {
    0: "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
    1: "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
    2: "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
    3: "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
    4: "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1",
}


def sha_cases() -> list:
    for vector, digest in SHA_KNOWN.items():
        if hashlib.sha256(sha_message(vector)).hexdigest() != digest:
            raise SystemExit("hashlib disagrees with FIPS 180-4 on vector %d" % vector)
    vectors = [0, 1, 2, 3, 4, 5] + [100 + n for n in range(201)] + \
        [1000 + n for n in (0, 1, 55, 56, 63, 64, 65, 119, 120, 128, 200)]
    cases = []
    for vector in vectors:
        digest = hashlib.sha256(sha_message(vector)).digest()
        for word in range(4):
            v = int.from_bytes(digest[8 * word:8 * word + 8], "big")
            cases.append(("sha256 sha_test I64 %d I64 %d" % (vector, word), value("U64", v)))
    return cases


# ------------------------------------------------------------------------------ limits.ci

LIM = {"source": 16, "program": 17, "modules": 18, "tokens": 19, "nodes": 20, "symbols": 21,
       "functions": 22, "types": 23, "sir": 24, "stack": 25, "arms": 26, "literals": 27,
       "output": 28}
CONSTS = [4194304, 67108864, 4096, 1048576, 65536, 65536, 131072, 256, 65536, 100, 101, 513, 65,
          268435456, 2372, 237200, 17, 2, 237200, 1048576]


def model_capacity(lim: int, n: int) -> int:
    """SPEC-09 CINTC-02, capped by the hard limits of the same table (CINTC-01)."""
    t = n + 1
    table = {16: min(n, 4194304), 17: min(n, 67108864), 18: min(n, 4096), 28: min(n, 268435456),
             19: t, 20: 2 * t + 16, 21: min(t // 2 + 64, 1048576), 22: min(t // 4 + 1, 65536),
             23: min(256 + t // 8, 65536), 24: 4 * t + 16, 25: min(512 + t // 32, 131072),
             27: -(-n // 2) + 64, 26: 65536, 29: min(n // 4 + n // 32 + 1, 1048576)}
    return table.get(lim, -1)


def model_hard(lim: int) -> int:
    # 30, the imported-name index: two rows for each of at most 1,048,576 declarations.
    # 31, the bytes of a record's cpu-c17 layout (SPEC-09).
    fixed = {16: 4194304, 17: 67108864, 18: 4096, 28: 268435456, 26: 65536, 29: 1048576,
             30: 2 * 1048576, 31: 4294967295}
    if lim in fixed:
        return fixed[lim]
    return model_capacity(lim, 4194304) if lim in (19, 20, 21, 22, 23, 24, 25, 27) else -1


class DiagModel:
    """CINTC-14 retained diagnostics with LS-282 order, written from the rule, not from
    limits.ci: keep the `keep` least rows by (module, line, column, code), equal keys in
    the order added; count the rest; a C6001 or C6002 row takes the least fault slot that
    no retained row holds."""

    def __init__(self, rows: int):
        self.keep = min(100, rows - 1)
        self.rows, self.dropped, self.seq = [], 0, 0

    def add(self, code, module, line, column, d0, d1):
        self.seq += 1
        row = {"key": (module, line, column, code), "seq": self.seq,
               "detail": [d0, d1, 0, 0], "code": code}
        candidates = sorted(self.rows + [row], key=lambda r: (r["key"], r["seq"]))
        kept = candidates[:self.keep]
        self.dropped += len(candidates) - len(kept)
        if row in kept and code in (6001, 6002):
            used = {r["detail"][2] // 2372 for r in kept if r is not row and r["code"] in (6001, 6002)}
            row["detail"][2] = 2372 * min(set(range(101)) - used)
        self.rows = kept


def diag_scenario(scenario: int) -> DiagModel:
    m = DiagModel(41)
    if scenario == 0:
        m.add(2001, 0, 5, 3, 11, 12)
        m.add(3004, 0, 2, 7, 21, 22)
        m.add(1050, 0, 5, 3, 31, 32)
        m.add(2001, 0, 5, 3, 41, 42)
    elif scenario in (1, 2):
        for k in range(60):
            code = 6001 if scenario == 2 and k % 3 == 0 else 2001
            m.add(code, k % 2, (k * 37) % 61 + 1, k % 5 + 1, k, 0)
    elif scenario == 3:
        m.add(9001, 4096, 1, 1, LIM["modules"], 4096)      # check_modules(4097)
        sizes = [4000000] * 20
        sizes[3] = 4194305
        m.add(9001, 3, 1, 1, LIM["source"], 4194304)       # check_program: module 3 too large
        first = next(i for i in range(20) if sum(sizes[:i + 1]) > 67108864)
        m.add(9001, first, 1, 1, LIM["program"], 67108864)
    elif scenario == 4:
        m.add(9001, 2, 9, 5, LIM["tokens"], 11)
    return m


def diag_value(m: DiagModel, i: int, f: int) -> int:
    if i == 0:
        return [0, 0, 0, 0, len(m.rows), m.dropped, 0, 0][f]
    if i > len(m.rows):
        return 0
    r = m.rows[i - 1]
    if f == 0:
        return r["code"]
    if f < 4:
        return r["key"][f - 1]
    return r["detail"][f - 4]


def limits_cases() -> list:
    cases = []
    for k, v in enumerate(CONSTS + [-1]):
        cases.append(("limits limits_const I64 %d" % k, value("I64", v)))
    sizes = [0, 1, 2, 3, 7, 8, 63, 64, 100, 1000, 65535, 131071, 2097150, 2097151, 4194303, 4194304]
    # Identifiers 16 to 31 have a limit; 14, 15 and 32, on either side, have none.
    for lim in list(range(14, 33)):
        for n in sizes:
            cases.append(("limits capacity I64 %d I64 %d" % (lim, n), value("I64", model_capacity(lim, n))))
        cases.append(("limits hard_limit I64 %d" % lim, value("I64", model_hard(lim))))
    for b in (1, 2, 3, 4, 6, 8, 12, 16, 24, 64, 100):
        align = 8 if b % 8 == 0 else 4 if b % 4 == 0 else 2 if b % 2 == 0 else 1
        cases.append(("limits elem_align I64 %d" % b, value("I64", align)))
    for scenario in range(5):
        m = diag_scenario(scenario)
        for i in range(41 if scenario in (1, 2) else 5):
            for f in range(8):
                cases.append(("limits diag_test I64 %d I64 %d I64 %d" % (scenario, i, f),
                              value("I64", diag_value(m, i, f))))
    row = [1, 16, 8, model_capacity(LIM["tokens"], 100), 1, 0, 0x71, 3]
    for i, want in ((1, row), (0, [0] * 8)):
        for f in range(8):
            cases.append(("limits diag_test I64 5 I64 %d I64 %d" % (i, f), value("I64", want[f])))
    return cases


# ------------------------------------------------------------------------------ decl.ci

def decl_rows() -> list:
    """The rows decl.decl_test writes, from CINTC-16: (kind, name_len, module, tag, rank, a, b,
    name)."""
    text = bytes(0x61 + j % 26 for j in range(48))
    text = b"SCALE" + text[5:]
    rows = [(1, 0, 0, 0, 0, 11, 2, b""), (2, 0, 3, 0, 0, 8, 3, b""), (3, 5, 3, 0x14, 0, -5, 0, b"SCALE"),
            (4, 4, 3, 0x14, 0, 3, 0, b"add3")]
    rows += [(7, 1, 3, 0x14, 0, p, 0, text[5 + p:6 + p]) for p in range(3)]
    rows += [(3, 40, 3, 0x24, 0, 1, 0, text[:32]), (8, 8, 3, 0, 0, 0, 0, text[32:40]),
             (2, 0, 7, 0, 0, 2, 1, b""), (4, 1, 7, 2, 0, 0, 0, b"f")]
    return rows


def decl_cases() -> list:
    cases = []
    for i, want in enumerate([1, 9, -1, 3, -1, 7, 4, 0]):
        cases.append(("decl decl_test I64 0 I64 64 I64 %d I64 0" % i, value("I64", want)))
    for r, row in enumerate(decl_rows()):
        name = row[7] + bytes(32 - len(row[7]))
        for f in range(7 + 9):
            want = row[f] if f < 7 else name[f - 7]
            cases.append(("decl decl_test I64 0 I64 64 I64 %d I64 %d" % (8 + r, f), value("I64", want)))
    for k in (0, 1, 2, 8, 9, 10, 11, 12, 64):
        a, b, failed = (11, 2, -1) if k >= 11 else (9, 1, 9) if k >= 9 else (1, 0, 1)
        for i, want in enumerate([a, b, failed]):
            cases.append(("decl decl_test I64 1 I64 %d I64 %d I64 0" % (k, i), value("I64", want)))
    return cases


# ------------------------------------------------------------------------------ emit.ci

def emit_sample(k: int) -> bytes:
    if k == 1:
        return b"hello, world\n"
    if k == 2:
        return b"%d %d 0 -1 42\n" % (I64_MIN, (1 << 63) - 1)
    if k == 3:
        return b"%d 10 deadbeef 00000000deadbeef fedcba9876543210 c\n" % ((1 << 64) - 1)
    if k == 4:
        return b"int64_t f(void) {\n    return 0;\n            }\n"
    if k == 5:
        return b"cdeh\n"
    if k == 6:
        return b"".join(b"line %d {x}\t%08x\n" % (i, (i * 2654435761) & 0xffffffff) for i in range(100))
    if k == 7:
        return b"0123456789" * 1000
    return b""


def fnv1a64(data: bytes) -> int:
    h = 0xcbf29ce484222325
    for byte in data:
        h = ((h ^ byte) * 0x100000001b3) & ((1 << 64) - 1)
    return h


def emit_cases() -> list:
    cases = []
    for k in range(8):
        text = emit_sample(k)
        cases.append(("emit emit_test I64 %d I64 0 I64 0" % k, value("I64", len(text))))
        cases.append(("emit emit_test I64 %d I64 4 I64 0" % k, value("I64", 0)))
        if len(text) > 4096:   # the emit pass writes past its buffer: an internal error
            cases.append(("emit emit_test I64 %d I64 3 I64 0" % k, fault("E_BOUNDS")))
            continue
        cases.append(("emit emit_test I64 %d I64 1 I64 0" % k, value("I64", len(text))))
        cases.append(("emit emit_test I64 %d I64 2 I64 0" % k, value("I64", 0)))
        cases.append(("emit emit_test I64 %d I64 3 I64 0" % k, value("I64", wrap_i64(fnv1a64(text)))))
        for extra in (1, -1):
            if len(text) + extra >= 0:
                cases.append(("emit emit_test I64 %d I64 2 I64 %d" % (k, extra), value("I64", 2)))
    for n, want in ((0, 0), (268435456, 0), (268435457, 1)):
        cases.append(("emit emit_limit I64 %d" % n, value("I64", want)))
    return cases


def fold_cases() -> list:
    return list(fold_model.fold_cases())


# ------------------------------------------------------------------------------ parse.ci plan rows

def front_plan_cases() -> list:
    """parse.plan_front: the manifest rows of the token table and the node table (CINTC-02,
    CINTC-14); the parse stack is in the frame region of check.plan_check (KR-03)."""
    cases = []
    for n in (0, 1, 100, 65536, 4194304):
        t = n + 1
        caps = [t, 2 * t + 16]
        for row in range(2):
            want = [row, (32, 48)[row], 8, caps[row], 1, 0, 0x71, (7, 4)[row]]
            for f in range(8):
                cases.append(("tests.front front_plan_test I64 %d I64 %d I64 %d" % (n, row, f),
                              value("I64", want[f])))
    return cases


def check_plan_cases() -> list:
    """check.plan_check: the manifest rows of the Sem, Sym, Name, frame-region and
    literal-value tables (CINTC-02, CINTC-14; compiler/OPEN.md CINTC-OQ-16). The region has
    the larger of the parser's limit and the checker's, nodes + 16 (KR-02, KR-03)."""
    cases = []
    for n in (0, 1, 100, 65536, 4194304):
        nodes = model_capacity(LIM["nodes"], n)
        symbols = model_capacity(LIM["symbols"], n)
        region = max(nodes + 16, model_capacity(LIM["stack"], n))
        for declarations in (0, 1, 1048576):
            names = 2 * symbols + (4096 + 2 * declarations if declarations else 0)
            caps = [nodes, symbols, names, region, model_capacity(LIM["literals"], n)]
            elem = [56, 80, 40, 48, 1]
            for row in range(5):
                code, record = (0x21, 0) if row == 4 else (0x71, 7 + row)
                want = [row, elem[row], 8 if row < 4 else 1, caps[row], 1, 0, code, record]
                for f in range(8):
                    cases.append(("tests.front_check check_plan_test I64 %d I64 %d I64 %d I64 %d" %
                                  (n, row, f, declarations), value("I64", want[f])))
    return cases


def path_capacity_cases() -> list:
    # The path region has 171 or 172 bytes including 144 revision bytes. The
    # module path uses 4 bytes and the parsed import needs 24 more.
    wants = {
        0: [1, 1, 0, 9001, 0, 1, 1, 16, 171, 0, 0, 4, 0, 0],
        1: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 24, 0],
    }
    cases = []
    for scenario, values in wants.items():
        for field, want in enumerate(values):
            cases.append(("main path_capacity_test I64 %d I64 %d" % (scenario, field),
                          value("I64", want)))
    return cases


def scan_capacity_cases() -> list:
    wants = {
        0: [-1, 1, 0, 9001, 0, 1, 2, 19, 1, 0, 0],
        1: [2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    }
    cases = []
    for scenario, values in wants.items():
        for field, want in enumerate(values):
            cases.append(("scan scan_capacity_test I64 %d I64 %d" % (scenario, field),
                          value("I64", want)))
    return cases


def lowering_capacity_cases() -> list:
    cases = []
    for mode in (0, 1):
        for calls in (1, 2, 3):
            full = calls > 1
            values = [0 if not full else -1, int(mode == 0), int(mode == 1),
                      int(mode == 0), int(mode == 0), 9001 if full else 0,
                      int(full), 0]
            values += [9001, 7, 8, 13, 24 if mode == 0 else 21, 1, 0, 0] if full else [0] * 8
            values += [1, 20, 0, -1, -1, 0, 1, 0, 0, 0, 0] if mode == 0 else [17, 20, 3, 1] + [0] * 7
            for field, want in enumerate(values):
                cases.append(("lowering writer_capacity_test I64 %d I64 %d I64 %d" % (mode, calls, field),
                              value("I64", want)))
    for calls in (1, 2, 3):
        full = calls > 1
        values = [1, 0, 9001 if full else 0, int(full), 0]
        values += [9001, 7, 2, 6, 22, 1, 0, 0] if full else [0] * 8
        values += [1] + [0] * 10
        for field, want in enumerate(values):
            cases.append(("lowering function_capacity_test I64 %d I64 %d" % (calls, field), value("I64", want)))
    return cases


def resolver_capacity_cases() -> list:
    cases = []
    for mode in (0, 1):
        for calls in (1, 2, 3):
            full = calls > 1
            values = [-1 if full else 0, int(mode == 0), int(mode == 1),
                      9001 if full else 0, int(full), 0]
            values += [9001, 7, 8, 13, 21, 1, 0, 0] if full else [0] * 8
            values += [2, 20, 11, 0, 17, -1, -1, 2, 0, 0] if mode == 0 else \
                      [1, 1, 1, int(full), 0, 0, 0, 0, 0, 0]
            values += [1]
            for field, want in enumerate(values):
                cases.append(("tests.resolve_capacity run I64 %d I64 %d I64 %d" % (mode, calls, field),
                              value("I64", want)))
    return cases


def harness_part(rel: str, make) -> dict:
    return {"kind": "harness", "name": rel, "rel": rel, "make": make, "requires": ["compiler/" + rel]}


def host_part(name: str, module) -> dict:
    return {"kind": "host", "name": name, "module": module, "requires": module.REQUIRES}


SUITES = {
    "imports": [host_part("imports", import_names)],
    "foundations": [harness_part("limits.ci", limits_cases), harness_part("sha256.ci", sha_cases),
                    harness_part("emit.ci", emit_cases), harness_part("decl.ci", decl_cases)],

    # Task 2.10: 2.10a the parser, 2.10b fold.ci, 2.10c the resolver and checker.
    "front": [host_part("parse", front_suite), host_part("parse_capacity", parse_capacity_suite),
              harness_part("tests/front.ci", front_plan_cases),
              harness_part("fold.ci", fold_cases), host_part("check", check_suite),
              host_part("decl_capacity", decl_capacity_suite),
              harness_part("tests/front_check.ci", check_plan_cases)],
    # Task 2.10c alone: resolve.ci and check.ci.
    "check": [host_part("check", check_suite), host_part("decl_capacity", decl_capacity_suite),
              harness_part("tests/front_check.ci", check_plan_cases)],
    "capacity": [host_part("decl_capacity", decl_capacity_suite),
                 host_part("parse_capacity", parse_capacity_suite),
                 harness_part("main.ci", path_capacity_cases),
                 harness_part("scan.ci", scan_capacity_cases),
                 harness_part("lowering.ci", lowering_capacity_cases),
                 harness_part("tests/resolve_capacity.ci", resolver_capacity_cases),
                 host_part("table_capacity", table_capacity_suite),
                 host_part("k_probes", k_probes_suite)],
    # Task 2.10b's own acceptance: fold.ci alone.
    "fold": [harness_part("fold.ci", fold_cases)],
    # Task 2.11: lowering, verify, back_c and main through the bridge.
    "golden": [host_part("golden", golden_suite)],
    # Box 11 unit 3: back_ptx.ci through main.ci, its PTX golden and assembled by ptxas.
    "ptx": [host_part("ptx", ptx_suite)],
}

# What the last line calls a passed item.
VERB = {"golden": "identical"}


def present(part) -> bool:
    return all((ROOT / rel).is_file() for rel in part["requires"])


# ------------------------------------------------------------------------------ driver

def configurations(ns) -> list:
    opts = [ns.opt] if ns.opt else ["0", "2"]
    helpers = [ns.helpers] if ns.helpers else ["portable", "builtin"]
    if ns.leg in ("msvc", "apple-clang"):
        sans = [False]
    elif ns.sanitize is None:
        sans = [False, True]
    else:
        sans = [ns.sanitize]
    return [(o, h, s) for s in sans for o in opts for h in helpers]


def run_config(leg, opt, helpers, sanitize, parts, prepared, out: pathlib.Path, emitted: dict) -> tuple:
    tag = "%s-O%s-%s%s" % (leg, opt, helpers, "-san" if sanitize else "")
    tc = cc.Toolchain(leg, opt, helpers, sanitize)
    work = out / tag
    work.mkdir(parents=True, exist_ok=True)
    tools = cc.build_tools(tc, work)
    passed, total, failures = 0, 0, []
    env = dict(tc.env)
    env["ASAN_OPTIONS"] = "detect_leaks=1:abort_on_error=0"
    env["UBSAN_OPTIONS"] = "print_stacktrace=1:halt_on_error=1"
    for part in parts:
        if part["kind"] == "host":
            p, t, f = part["module"].run(tc, tools, work, emitted, prepared[part["name"]], cc)
            passed, total = passed + p, total + t
            failures += ["%s %s: %s" % (tag, part["name"], x) for x in f[:40]]
            continue
        rel, make = part["rel"], part["make"]
        cases = make()
        total += len(cases)
        stem = pathlib.Path(rel).stem
        c_file = work / (stem + ".c")
        code, err = cc.run_seed(tools["seed"], COMPILER, rel, c_file)
        problem = None
        if code != 0 or cc.sanitizer_reports(err):
            problem = "seed: exit %d: %s" % (code, " ".join(err.split())[:400])
        else:
            text = c_file.read_bytes()
            digest = hashlib.sha256(text).hexdigest()
            if b"\r" in text:
                problem = "emitted C holds a CR byte"
            elif emitted.setdefault(rel, digest) != digest:
                problem = "emitted C differs from the first configuration's (%s)" % emitted[rel][:16]
        if problem is None:
            try:
                objs = tc.compile([c_file], work, stem)
                lib = tc.link_shared(objs + [tools["rt_obj"]], work / stem, stem)
            except cc.BuildError as e:
                problem = "build: %s" % " ".join(str(e).split())[:600]
        if problem is not None:
            failures.append("%s %s: %s (%d cases)" % (tag, rel, problem, len(cases)))
            continue
        cases_file = work / (stem + ".cases")
        cases_file.write_bytes("".join(line + "\n" for line, _ in cases).encode("ascii"))
        r = subprocess.run([str(tools["harness"]), str(lib), "--cases", str(cases_file)], cwd=work,
                           env=env, capture_output=True)
        stderr = r.stderr.decode("utf-8", "replace")
        if r.returncode != 0 or cc.sanitizer_reports(stderr):
            failures.append("%s %s: harness exit %d: %s" % (tag, rel, r.returncode,
                                                            " ".join(stderr.split())[:400]))
            continue
        blocks = cc.parse_harness(r.stdout, len(cases))
        for (line, want), got in zip(cases, blocks):
            if got is not None and all(w in got for w in want):
                passed += 1
            elif len(failures) < 40:
                failures.append("%s %s: %s: want %s, got %s" % (tag, rel, line, want, got))
    return tag, passed, total, failures


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--leg", choices=["msvc", "gcc", "clang", "apple-clang"], required=True)
    ap.add_argument("--suite", choices=sorted(SUITES), required=True)
    ap.add_argument("--opt", choices=["0", "2"])
    ap.add_argument("--helpers", choices=["portable", "builtin"])
    ap.add_argument("--sanitize", dest="sanitize", action="store_true", default=None,
                    help="only sanitized configurations (gcc and clang)")
    ap.add_argument("--no-sanitize", dest="sanitize", action="store_false",
                    help="only unsanitized configurations")
    ap.add_argument("--out", help="build directory (default <build>/compiler/<suite>, <build> from CINT_BUILD)")
    raw = list(argv if argv is not None else sys.argv[1:])
    ns = ap.parse_args(raw)
    if ns.out and pathlib.Path(ns.out).resolve().is_relative_to(ROOT.resolve()):
        ap.error("--out must be outside the repository: %s" % ns.out)
    if ns.leg != "msvc" and os.name == "nt":
        fwd = []
        for i, a in enumerate(raw):
            fwd.append(cc.wsl_path(pathlib.Path(a).resolve()) if i and raw[i - 1] == "--out" else a)
        cmd = ["wsl", "-d", cc.WSL_DISTRO, "--cd", cc.wsl_path(ROOT), "--", "python3",
               "compiler/tests/run.py", *fwd]
        return subprocess.run(cmd, env=cc.wsl_env()).returncode
    if ns.leg == "msvc" and os.name != "nt":
        ap.error("the msvc leg runs on Windows")
    if ns.leg == "apple-clang" and sys.platform != "darwin":
        ap.error("the apple-clang leg runs on macOS")
    if ns.sanitize and ns.leg in ("msvc", "apple-clang"):
        ap.error("--sanitize is for the gcc and clang legs")
    avail = cc.available_memory()
    if avail < cc.MIN_AVAILABLE_BYTES:
        print("BLOCKED: %d MiB available; a compile-heavy run needs 8 GiB" % (avail >> 20))
        return 3
    out = pathlib.Path(ns.out) if ns.out else cc.build_root() / "compiler" / ns.suite
    parts = []
    for part in SUITES[ns.suite]:
        if present(part):
            parts.append(part)
        else:
            missing = [r for r in part["requires"] if not (ROOT / r).is_file()]
            print("%s: part %s skipped: %s not present" % (ns.suite, part["name"], ", ".join(missing)))
    prepared = {p["name"]: p["module"].prepare(out / ("%s-sources" % p["name"])) for p in parts
                if p["kind"] == "host"}
    passed = total = 0
    emitted = {}
    failed = False
    for opt, helpers, sanitize in configurations(ns):
        try:
            tag, p, t, failures = run_config(ns.leg, opt, helpers, sanitize, parts, prepared, out, emitted)
        except cc.BuildError as e:
            print("BLOCKED: %s" % " ".join(str(e).split())[:800])
            return 3
        for f in failures:
            print("FAIL " + f)
        failed = failed or bool(failures)
        print("%s: %d of %d %s" % (tag, p, t, VERB.get(ns.suite, "pass")))
        passed, total = passed + p, total + t
    for rel, digest in sorted(emitted.items()):
        print("emitted C %s sha256 %s" % (rel, digest))
    print("%s: %d of %d %s" % (ns.suite, passed, total, VERB.get(ns.suite, "pass")))
    return 0 if passed == total and not failed else 1


if __name__ == "__main__":
    sys.exit(main())
