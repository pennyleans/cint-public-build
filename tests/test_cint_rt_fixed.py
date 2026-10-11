"""The fixed-point helpers of rt/cint_fixed.h against cint_ref (roadmap box 15; SPEC-01 IM-54 to
IM-69; box 15 note 5.3 and 6.4).

rt/tests/test_fixed.c reads two inputs written here. The cases, one per line, are `<id> <kind> <ta>
<tb> <tr> <mode> <a> <b> = <result>`: the type words of the operands and the result (CINT_QT of
rt/cint_fixed.h for Q<i>.<f>, the tag for I8 to U64, 0 when absent), the rounding mode (1 to 9, as
for cint_rt_builtin, or 0), and the operands as 64-bit patterns in hexadecimal. The result,
computed by `cint_ref` (ref/cint_ref/fixed.py), is `V <hex>` for a value, `R <ArithError tag>
[<hex>]` for `as?` and the result forms, or `F <hex>`, the canonical bytes of the run-time fault record (SPEC-01
IM-149) at site 1 of `fixed.ci` (line 1, column 1) with an empty stack. The cases cover every
helper: `+`, `-`, unary `-`, `abs`, `*` in its three forms, `/`, `sqrt`, `mul_int` by each integer
type, `qmul` across formats in both directions, the result forms `add_result`, `sub_result`,
`mul_result`, `div_result` and `sqrt_result` (rt/OPEN.md RT-OQ-59), and conversions between fixed-point types and to
and from I8 to U64 in their checked, wrapping, saturating (`as|`, rt/OPEN.md RT-OQ-58) and `as?` forms; for storage of 16, 32 and 64 bits
at several scales, on the boundary values of each type (zero, one ulp, one, one half, MIN, MAX and
their neighbors) and seeded random values, in every rounding mode.

The table is the exhaustive run of note 6.4: for every 8-bit type, Q1.7 to Q8.0, every rounding
mode and every pair of raw values, `*` in each form and `/`; `sqrt` of every value; and every
conversion among the 8-bit types and between them and I8 and U8 in both directions, checked,
wrapping and saturating. Its blocks hold one outcome per input (the result's raw byte, or the fault code), from
`cint_ref`'s rounding (`arith.round_div`, `arith._isqrt_round`) on the exact intermediate; a seeded
sample of each block is checked against `fixed.op` and `fixed.convert` themselves here before the
table is written. It is generated into the build directory at each run, about 19 million `*`
and `/` pairs, and its SHA-256 printed.

The driver, with rt/cint_rt.c, rt/cint_state.c and rt/cint_rec.c, is built for each leg of this
host (gcc, and clang when installed, on Linux; apple-clang on macOS; msvc on Windows) at -O0 and
-O2 (MSVC /Od and /O2) in both helper modes, with the flags of tools/cint_check.py and, under
`workflow.py check --sanitize`, its sanitizers on gcc and clang. Build outputs go under
<build>/rt-fixed, where <build> is CINT_BUILD or cint-build in the system temporary directory.
"""
import hashlib
import os
import pathlib
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "ref"))
import cint_check as cc  # noqa: E402
from cint_ref import arith, fixed  # noqa: E402
from cint_ref.faults import Fault, FaultRecord, Position, encode_fault_record  # noqa: E402
from cint_ref.types import Value, int_type  # noqa: E402

STORAGE = {8: 0x11, 16: 0x12, 32: 0x13, 64: 0x14}
INTS = (("I8", 0x11), ("I16", 0x12), ("I32", 0x13), ("I64", 0x14),
        ("U8", 0x21), ("U16", 0x22), ("U32", 0x23), ("U64", 0x24))
INT_TAG = dict(INTS)
MODES = arith.ROUND_MODES                          # numbered 1 to 9, as for cint_rt_builtin
MODE_NUM = {m: k + 1 for k, m in enumerate(MODES)}
CODES = {"E_OVERFLOW": 1, "E_DIV_ZERO": 2, "E_DOMAIN": 3, "E_NARROW": 4}
ARITH_ERROR = {"E_OVERFLOW": 1, "E_DIV_ZERO": 2, "E_DOMAIN": 5}   # the ArithError tags of IM-188
POSITION = Position("fixed.ci", 1, 1)
M64 = (1 << 64) - 1
# Types of the case lines: each storage width at f = 0, 1, w/2, w - 2 and w - 1, and two 8-bit ones.
CASE_TYPES = ["Q%d.%d" % (w - f, f) for w in (16, 32, 64) for f in (0, 1, w // 2, w - 2, w - 1)] + \
             ["Q4.4", "Q1.7"]
TYPES8 = ["Q%d.%d" % (8 - f, f) for f in range(8)]
# Block kinds of the table.
K_MUL, K_DIV, K_SQRT, K_AS = 1, 2, 3, 4
FORM_NUM = {"checked": 0, "wrap": 1, "sat": 2}


def word(t: str) -> int:
    """The type word of rt/cint_fixed.h: CINT_QT(storage, f), or the integer tag."""
    if t in INT_TAG:
        return INT_TAG[t]
    q = fixed.q_type(t)
    return (q.f << 16) | (0x31 << 8) | STORAGE[q.width]


def rng_of(t: str):
    s = fixed.q_type(t).storage if fixed.is_q_type(t) else int_type(t)
    return s.min, s.max


def outcome(result) -> str:
    if isinstance(result, Fault):
        return "F " + encode_fault_record(FaultRecord.from_fault(result, POSITION), "run-time").hex()
    return "V %016x" % (result.value & M64)


def result_form(r) -> str:
    """The result form of an operation whose checked form gives r (SPEC-01 IM-188; decision 2026-10-10
    on OQ-273 and OQ-278): `R 0 <hex>` for its value, or `R <tag>`, the ArithError tag that mirrors its
    fault. cint_ref's checker refuses the fixed-point result forms yet, so this reads the checked form."""
    if isinstance(r, Fault):
        return "R %d" % ARITH_ERROR[r.code]
    return "R 0 %016x" % (r.value & M64)


def convert(form: str, source: str, target: str, x: int, mode=None):
    """fixed.convert, and `as|` (SPEC-01 IM-50, IM-68; decision 2026-10-10 on OQ-272), which cint_ref
    does not take yet: the checked conversion's value, or the limit of its fault, which is the bound
    of the target on the exact value's side (IM-106)."""
    if form != "sat":
        return fixed.convert(form, source, target, Value(source, x), mode=mode)
    r = fixed.convert("checked", source, target, Value(source, x), mode=mode)
    return r.limit if isinstance(r, Fault) else r


def boundaries(t: str) -> list:
    lo, hi = rng_of(t)
    f = fixed.q_type(t).f if fixed.is_q_type(t) else 0
    out = {0, 1, 2, 3, lo, lo + 1, lo + 2, hi, hi - 1, hi - 2, -1, -2, -3}
    for k in (f - 1, f, f + 1, (f + 1) // 2, (lo.bit_length() + f) // 2):
        if k >= 0:
            for v in (1 << k, (1 << k) + 1, (1 << k) - 1, 3 << k, 3 << max(k - 1, 0)):
                out.update((v, -v))
    return sorted(v for v in out if lo <= v <= hi)


def randoms(rng: random.Random, t: str, n: int) -> list:
    lo, hi = rng_of(t)
    w = (hi - lo).bit_length()
    out = []
    for _ in range(n):
        bits = rng.randrange(1, w + 1)
        v = rng.randrange(-(1 << (bits - 1)), 1 << (bits - 1)) if lo < 0 else rng.randrange(0, 1 << bits)
        out.append(max(lo, min(hi, v)))
    return out


def values(rng: random.Random, t: str, n: int = 12) -> list:
    return boundaries(t) + randoms(rng, t, n)


def pairs(rng: random.Random, t: str, n: int) -> list:
    """Each boundary with a few others, then seeded random pairs."""
    b = boundaries(t)
    out = [(x, y) for x in b for y in (b[0], b[len(b) // 2], b[-1], 0, 1, -1)]
    out += [(x, rng.choice(b)) for x in b]
    out += list(zip(randoms(rng, t, n), randoms(rng, t, n)))
    return out


def generate(seed: int = 15) -> list:
    """The case lines."""
    rng = random.Random(seed)
    out = []

    def add(kind, ta, tb, tr, mode, a, b, result):
        out.append("%d %s %d %d %d %d %016x %016x = %s" % (len(out), kind, word(ta) if ta else 0,
                                                            word(tb) if tb else 0, word(tr), MODE_NUM.get(mode, 0),
                                                            a & M64, b & M64, result))

    for t in CASE_TYPES:
        q = lambda v: Value(t, v)  # noqa: E731
        for a, b in pairs(rng, t, 40):
            for name in ("add", "sub"):
                r = fixed.op(name, "checked", t, q(a), q(b))
                add(name, t, t, t, None, a, b, outcome(r))
                add(name + "_result", t, t, t, None, a, b, result_form(r))
            for m in MODES:
                for form in ("checked", "wrap", "sat"):
                    kind = "mul" if form == "checked" else "mul_" + form
                    add(kind, t, t, t, m, a, b, outcome(fixed.op("mul", form, t, q(a), q(b), mode=m)))
                add("mul_result", t, t, t, m, a, b, result_form(fixed.op("mul", "checked", t, q(a), q(b), mode=m)))
                r = fixed.op("div", "checked", t, q(a), q(b), mode=m)
                add("div", t, t, t, m, a, b, outcome(r))
                add("div_result", t, t, t, m, a, b, result_form(r))
        for a in values(rng, t, 40):
            for name in ("neg", "abs"):
                add(name, t, None, t, None, a, 0, outcome(fixed.op(name, "checked", t, q(a))))
            for m in MODES:
                r = fixed.op("sqrt", "checked", t, q(a), mode=m)
                add("sqrt", t, None, t, m, a, 0, outcome(r))
                add("sqrt_result", t, None, t, m, a, 0, result_form(r))
        for n_t, _ in INTS:
            for a in boundaries(t)[::3] + randoms(rng, t, 4):
                for n in boundaries(n_t)[::2] + randoms(rng, n_t, 4):
                    add("mul_int", t, n_t, t, None, a, n,
                        outcome(fixed.op("mul_int", "checked", t, q(a), Value(n_t, n))))
        # Conversions to each other type of the set and to and from every integer type.
        for dst in CASE_TYPES + [n for n, _ in INTS]:
            if dst == t:
                continue
            rounds = not fixed.is_q_type(dst) or fixed.q_type(dst).f < fixed.q_type(t).f
            for x in boundaries(t)[::2] + randoms(rng, t, 6):
                for m in MODES if rounds else (None,):
                    for form, kind in (("checked", "as"), ("wrap", "as_wrap"), ("sat", "as_sat")):
                        add(kind, t, None, dst, m, x, 0, outcome(convert(form, t, dst, x, m)))
                    r = fixed.convert("checked", t, dst, Value(t, x), mode=m)
                    add("as_result", t, None, dst, m, x, 0,
                        "R 4" if isinstance(r, Fault) else "R 0 %016x" % (r.value & M64))
        for src, _ in INTS:
            for x in boundaries(src)[::2] + randoms(rng, src, 6):
                for form, kind in (("checked", "as"), ("wrap", "as_wrap"), ("sat", "as_sat")):
                    add(kind, src, None, t, None, x, 0, outcome(convert(form, src, t, x)))
                r = fixed.convert("checked", src, t, Value(src, x))
                add("as_result", src, None, t, None, x, 0,
                    "R 4" if isinstance(r, Fault) else "R 0 %016x" % (r.value & M64))
    # qmul across formats: narrower and wider results, scaling down (rounded, by 1 to 126 bits, 64
    # among them) and up (exact).
    formats = [("Q16.16", "Q1.15", "Q16.16"), ("Q16.16", "Q16.16", "Q32.32"), ("Q32.32", "Q32.32", "Q1.63"),
               ("Q64.0", "Q64.0", "Q1.63"), ("Q1.63", "Q1.63", "Q64.0"), ("Q4.4", "Q8.8", "Q16.16"),
               ("Q8.24", "Q1.31", "Q2.6"), ("Q1.63", "Q8.0", "Q32.32"), ("Q32.32", "Q32.32", "Q64.0"),
               ("Q32.32", "Q32.32", "Q8.0"), ("Q1.63", "Q8.8", "Q64.0")]
    for ta, tb, tr in formats:
        shift = fixed.q_type(ta).f + fixed.q_type(tb).f - fixed.q_type(tr).f
        for a in boundaries(ta)[::2] + randoms(rng, ta, 12):
            for b in boundaries(tb)[::3] + randoms(rng, tb, 6):
                for m in MODES if shift > 0 else (None,):
                    add("qmul", ta, tb, tr, m, a, b,
                        outcome(fixed.op("qmul", "checked", tr, Value(ta, a), Value(tb, b), mode=m, target=tr)))
    return out


# --------------------------------------------------------------------------- the exhaustive table

def _status(r: int, lo: int, hi: int, form: str):
    """(status, raw byte) of a raw result r under form (the code E_OVERFLOW, or E_NARROW for `as`)."""
    if lo <= r <= hi:
        return 0, r & 0xFF
    if form == "wrap":
        return 0, r & 0xFF
    if form == "sat":
        return 0, (hi if r > hi else lo) & 0xFF
    return None, 0


def table_blocks():
    """(header, entries) for each block: entries are 2 bytes each, the status (0 a value, else the
    fault code of CODES) and the raw byte of the value."""
    span = range(-128, 128)
    for t in TYPES8:
        q = fixed.q_type(t)
        lo, hi = q.storage.min, q.storage.max
        for m in MODES:
            rounded = {p: arith.round_div(p, 1 << q.f, m) for p in range(-128 * 127, 128 * 128 + 1)}
            for form in ("checked", "wrap", "sat"):
                enc = {}
                for p, r in rounded.items():
                    st, v = _status(r, lo, hi, form)
                    enc[p] = bytes((CODES["E_OVERFLOW"], 0) if st is None else (0, v))
                yield (K_MUL, FORM_NUM[form], MODE_NUM[m], word(t), word(t)), \
                    b"".join(enc[a * b] for a in span for b in span)
            entries = bytearray()
            for a in span:
                n = a << q.f
                for b in span:
                    if b == 0:
                        entries += bytes((CODES["E_DIV_ZERO"], 0))
                        continue
                    st, v = _status(arith.round_div(n, b, m), lo, hi, "checked")
                    entries += bytes((CODES["E_OVERFLOW"], 0) if st is None else (0, v))
            yield (K_DIV, 0, MODE_NUM[m], word(t), word(t)), bytes(entries)
            entries = bytearray()
            for a in span:
                if a < 0:
                    entries += bytes((CODES["E_DOMAIN"], 0))
                    continue
                st, v = _status(arith._isqrt_round(a << q.f, m), lo, hi, "checked")
                entries += bytes((CODES["E_OVERFLOW"], 0) if st is None else (0, v))
            yield (K_SQRT, 0, MODE_NUM[m], word(t), word(t)), bytes(entries)
    # Conversions among the 8-bit types and with I8 and U8, both ways.
    for src in TYPES8 + ["I8", "U8"]:
        for dst in TYPES8 + ["I8", "U8"]:
            if src == dst or not (fixed.is_q_type(src) or fixed.is_q_type(dst)):
                continue
            fs = fixed.q_type(src).f if fixed.is_q_type(src) else 0
            fd = fixed.q_type(dst).f if fixed.is_q_type(dst) else 0
            rounds = fixed.is_q_type(src) and (not fixed.is_q_type(dst) or fd < fs)
            lo, hi = rng_of(dst)
            slo, shi = rng_of(src)
            for m in MODES if rounds else (None,):
                for form in ("checked", "wrap", "sat"):
                    entries = bytearray()
                    for x in range(slo, shi + 1):
                        r = x << (fd - fs) if fd >= fs else arith.round_div(x, 1 << (fs - fd), m)
                        st, v = _status(r, lo, hi, form)
                        entries += bytes((CODES["E_NARROW"], 0) if st is None else (0, v))
                    yield (K_AS, FORM_NUM[form], MODE_NUM.get(m, 0), word(src), word(dst)), bytes(entries)


def _ref_entry(kind, form, mode, ta, tr, a, b):
    """The same entry from fixed.op or fixed.convert, for the sample check."""
    names = {v: k for k, v in FORM_NUM.items()}
    m = MODES[mode - 1] if mode else None
    if kind == K_AS:
        r = convert(names[form], ta, tr, a, m)
    elif kind == K_SQRT:
        r = fixed.op("sqrt", "checked", ta, Value(ta, a), mode=m)
    else:
        r = fixed.op("mul" if kind == K_MUL else "div", names[form], ta, Value(ta, a), Value(ta, b), mode=m)
    return bytes((CODES[r.code], 0)) if isinstance(r, Fault) else bytes((0, r.value & 0xFF))


def write_table(path: pathlib.Path, seed: int = 15):
    """Writes the table; returns (blocks, entries, sha256). Each block is a 24-byte header of six
    little-endian U32 (kind, form, mode, ta, tr, count), then count entries. A seeded sample of
    each block's entries is first checked against fixed.op and fixed.convert."""
    rng = random.Random(seed)
    names = {word(t): t for t in TYPES8 + ["I8", "U8"]}
    h = hashlib.sha256()
    blocks = entries = 0
    with open(path, "wb") as f:
        for (kind, form, mode, ta, tr), body in table_blocks():
            count = len(body) // 2
            lo = rng_of(names[ta])[0]
            for _ in range(24):
                i = rng.randrange(count)
                a, b = (i // 256 - 128, i % 256 - 128) if count == 65536 else (lo + i, 0)
                want = _ref_entry(kind, form, mode, names[ta], names[tr], a, b)
                if body[2 * i:2 * i + 2] != want:
                    raise AssertionError("table entry %d of block %s differs from cint_ref: %s, %s" %
                                         (i, (kind, form, mode, names[ta], names[tr]), body[2 * i:2 * i + 2], want))
            head = struct.pack("<6I", kind, form, mode, ta, tr, count)
            f.write(head)
            f.write(body)
            h.update(head)
            h.update(body)
            blocks += 1
            entries += count
    return blocks, entries, h.hexdigest()


def legs():
    if os.name == "nt":
        return ["msvc"]
    if sys.platform == "darwin":
        return ["apple-clang"]
    return [leg for leg in ("gcc", "clang") if shutil.which(leg)]


class FixedPoint(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lines = generate()

    def test_cases_reach_every_kind(self):
        kinds = {line.split(" ", 2)[1] for line in self.lines}
        self.assertEqual(kinds, {"add", "sub", "neg", "abs", "mul", "mul_wrap", "mul_sat", "div", "sqrt",
                                 "mul_int", "qmul", "as", "as_wrap", "as_sat", "as_result", "add_result",
                                 "sub_result", "mul_result", "div_result", "sqrt_result"})
        results = {line.split(" = ", 1)[1][0] for line in self.lines}
        self.assertEqual(results, {"V", "R", "F"})
        tags = {line.split(" = R ", 1)[1].split(" ", 1)[0] for line in self.lines if " = R " in line}
        self.assertEqual(tags, {"0", "1", "2", "4", "5"})  # a value, .overflow, .div_zero, .narrow, .domain
        sqrt_tags = {line.split(" = R ", 1)[1][0] for line in self.lines if " sqrt_result " in line}
        self.assertEqual(sqrt_tags, {"0", "1", "5"})       # Q1.<f> roots rounded up to 2^(w - 1) overflow
        codes = {line.split(" = F ", 1)[1][48:52] for line in self.lines if " = F " in line}
        self.assertEqual(len(codes), 4)  # E_OVERFLOW, E_DIV_ZERO, E_DOMAIN and E_NARROW records

    def test_every_build_agrees(self):
        found = legs()
        if not found:
            self.skipTest("no gcc or clang on this host")
        root = cc.build_root() / "rt-fixed"
        root.mkdir(parents=True, exist_ok=True)
        sanitize = os.environ.get("INTOS_SANITIZE") == "1"
        with tempfile.TemporaryDirectory(prefix="fixed-", dir=root) as tmp:
            work = pathlib.Path(tmp)
            cases = work / "cases.txt"
            cases.write_bytes("".join(line + "\n" for line in self.lines).encode("ascii"))
            table = work / "exh8.bin"
            blocks, entries, digest = write_table(table)
            print("exhaustive 8-bit table: %d blocks, %d entries, sha256 %s" % (blocks, entries, digest))
            for leg in found:
                for opt in ("0", "2"):
                    for helpers in ("portable", "builtin"):
                        name = "%s-O%s-%s" % (leg, opt, helpers)
                        with self.subTest(build=name):
                            out = work / name
                            out.mkdir()
                            tc = cc.Toolchain(leg, opt, helpers, sanitize and leg in ("gcc", "clang"))
                            objs = tc.compile([ROOT / "rt" / f for f in ("cint_rt.c", "cint_state.c", "cint_rec.c",
                                                                          "tests/test_fixed.c")], out, name)
                            exe = tc.link_exe(objs, out / "test_fixed", name)
                            run = subprocess.run([str(exe), str(cases), str(table)], capture_output=True,
                                                 timeout=900)
                            self.assertEqual(run.returncode, 0, run.stderr[-3000:])
                            self.assertEqual(run.stderr, b"")
                            self.assertIn(b"helper mode: " + helpers.encode("ascii"), run.stdout)
                            self.assertIn(b"fixed cases: %d, passed %d, failed 0" % (len(self.lines), len(self.lines)),
                                          run.stdout)
                            self.assertIn(b"exhaustive 8-bit: %d blocks, %d entries, 0 differ" % (blocks, entries),
                                          run.stdout)


if __name__ == "__main__":
    unittest.main()
