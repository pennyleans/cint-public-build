"""The wide integers I128 to I1024 and U128 to U512 of rt/cint_wide.h against cint_ref (roadmap box 15,
unit 3).

rt/tests/test_wide.c reads cases written here, one per line: `<id> <kind> <tag> <mode> <aux>
<arg>... = <result>`, with the type tag of the operands, the rounding mode (1 to 9, as for
cint_rt_builtin), then the built-in's operation number, or the result or target tag of `muldiv`,
`mul_full` and `as`; each argument is `<tag>:<hex>`, the little-endian bytes of its value (IM-14).
The result, computed by `cint_ref` (ref/cint_ref/arith.py), is `V <tag> <hex>` for a value,
`R <ArithError tag> [<tag> <hex>]` for a result form, `B <0|1>` for a comparison, or `F <hex>`,
the canonical bytes of the run-time fault record (SPEC-01 IM-149) at the site test_wide.c raises
it from, `wide.ci` line 1 column 1, with an empty stack. test_wide.c also checks that a helper
whose result slot aliases a wide input (SPEC-09 EMIT-29) gives the same value, that a faulting
helper leaves its slot unwritten, how a wide operand, exact and limit render, and that module
state of wide types goes through a checkpoint (rt/cint_state.c): the payloads of an I128, an
I1024 and an I256[2] global, the decoder's text, the `state.global` lines, a restore, and a fault
record with I1024 operands that comes back from a restore byte for byte.

The cases cover every helper of each width, signed and unsigned: checked, wrapping, saturating and
result forms of `+`, `-` and `*`; negation, `not`, `abs`; floor division and remainder and their
result forms;
`div_trunc`, `rem_trunc`, `div_euclid`, `rem_euclid`, and `div_round` in every mode; the bitwise
operations, `min`, `max` and the comparisons; `clamp`, with `lo > hi` faulting `E_DOMAIN` as
decided on 2026-10-05; shifts by counts of several types, in range, at `w - 1` and `w`, and
negative; `isqrt` and `isqrt_round` in every mode on squares and their neighbors; `rotl` and `rotr` of
U128 to U512 by the same counts; shifts and rotations by counts of I128 to I1024 and U128 to U512
(rt/OPEN.md RT-OQ-52), of wide left operands and of I8 to U64, whose counts include 2^64 + 3 and
-2^64 + 5, out of range with a low limb in range; `uabs` of I128 to I512; `muldiv` to every result type in every
mode; `mul_full` of I64 to I512 and U64 to U256; and conversions among all wide types and I8 to
U64 both ways, and among I8 to U64, checked, wrapping and saturating (`as|`, rt/OPEN.md RT-OQ-58). The values are the boundaries of each width (zero, one, minus one, MIN, MAX,
2^(w - 1), the limb edges), then seeded random values of every limb length whose limbs are often
0, 1, 2^63 - 1, 2^63 or 2^64 - 1, and divisions whose dividend is a multiple of the divisor plus
a remainder, so that the corrections of algorithm D are reached.

The driver, with rt/cint_rt.c, rt/cint_state.c and rt/cint_rec.c, is built for each leg of this host (gcc, and clang when installed, on Linux;
apple-clang on macOS; msvc on Windows) at -O0 and -O2 (MSVC /Od and /O2) in both helper modes,
with the flags of tools/cint_check.py and, under `workflow.py check --sanitize`, its sanitizers
on gcc and clang. Build outputs go under <build>/rt-wide-types, where <build> is CINT_BUILD or
cint-build in the system temporary directory. Each run also checks the builtin limb steps
against the portable ones on CINT_WIDE_PAIRS seeded pairs (default 1,000,000; SPEC-09 EMIT-05
admits a builtin body after 10^8).
"""
import os
import pathlib
import random
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "ref"))
import cint_check as cc  # noqa: E402
from cint_ref import arith  # noqa: E402
from cint_ref.arith import OpenCase  # noqa: E402
from cint_ref.faults import Fault, FaultRecord, Position, encode_fault_record  # noqa: E402
from cint_ref.types import Value, int_type  # noqa: E402

WIDE = (("I128", 0x15), ("I256", 0x16), ("I512", 0x17), ("I1024", 0x18),
        ("U128", 0x25), ("U256", 0x26), ("U512", 0x27))
NARROW = (("I8", 0x11), ("I16", 0x12), ("I32", 0x13), ("I64", 0x14),
          ("U8", 0x21), ("U16", 0x22), ("U32", 0x23), ("U64", 0x24))
TAG = dict(WIDE + NARROW)
ARITH_ERROR = {"E_OVERFLOW": 1, "E_DIV_ZERO": 2, "E_SHIFT": 3, "E_NARROW": 4}
MODES = arith.ROUND_MODES                          # numbered 1 to 9, as for cint_rt_builtin
BUILTIN = {"abs": 1, "clamp": 5, "div_trunc": 6, "rem_trunc": 7, "div_euclid": 8, "rem_euclid": 9,
           "div_round": 10, "isqrt": 13, "isqrt_round": 14}
POSITION = Position("wide.ci", 1, 1)
EDGE_LIMBS = (0, 1, (1 << 63) - 1, 1 << 63, (1 << 64) - 1)


def width(name: str) -> int:
    return int_type(name).width


def hexle(name: str, v: int) -> str:
    w = width(name)
    return (v & ((1 << w) - 1)).to_bytes(w // 8, "little").hex()


def arg(name: str, v: int) -> str:
    return "%d:%s" % (TAG[name], hexle(name, v))


def boundaries(name: str) -> list:
    t = int_type(name)
    w = t.width
    vals = {0, 1, -1, 2, -2, 3, -3, t.max, t.min, t.max - 1, t.min + 1, 1 << 63, (1 << 63) - 1, -(1 << 63),
            1 << 64, (1 << 64) - 1, -(1 << 64), 1 << (w // 2), (1 << (w // 2)) - 1, -(1 << (w // 2)),
            1 << (w - 2), -(1 << (w - 2)), 1 << (w - 64), -(1 << (w - 64)) - 1, 1 << (w - 1), (1 << (w - 1)) + 1}
    return sorted(v for v in vals if t.contains(v))


def patterned(rng: random.Random, name: str) -> int:
    t = int_type(name)
    n = t.width // 64
    used = rng.randint(1, n)
    limbs = [rng.choice(EDGE_LIMBS) if rng.random() < 0.4 else rng.getrandbits(64) for _ in range(used)]
    if rng.random() < 0.3:
        limbs[-1] >>= rng.randint(0, 63)
    v = sum(limb << (64 * i) for i, limb in enumerate(limbs))
    if used == n:
        v = t.wrap(v)                                # every bit pattern of the width, read as signed
    elif rng.random() < 0.5:
        v = -v
    return v if t.contains(v) else t.wrap(v)


def values(rng: random.Random, name: str, count: int) -> list:
    return boundaries(name) + [patterned(rng, name) for _ in range(count)]


def pairs(rng: random.Random, name: str, count: int) -> list:
    """Every pair of boundaries, then `count` random pairs."""
    t = int_type(name)
    b = boundaries(name)
    out = [(x, y) for x in b for y in b]
    for i in range(count):
        if i % 3 == 2:
            d = patterned(rng, name) or 1
            q = patterned(rng, name) >> rng.randint(0, t.width - 1)
            r = rng.randrange(abs(d))
            x = d * q + (r if d * q >= 0 else -r)
            out.append((x if t.contains(x) else t.wrap(x), d))
        else:
            out.append((patterned(rng, name), patterned(rng, name)))
    return out


def wide_counts(ct: str, w: int) -> list:
    """Counts of type ct for a left operand of width w: in range, at w - 1 and w, and the values
    whose low limb is in range but whose higher limbs are not (RT-OQ-52)."""
    t = int_type(ct)
    vals = (0, 1, w - 1, w, 1 << 64, (1 << 64) + 3, -1, -(1 << 64) + 5, t.max, t.min)
    return [c for c in dict.fromkeys(vals) if t.contains(c)]


def outcome(result, form_result: bool = False) -> str:
    if isinstance(result, Fault):
        if form_result:
            return "R %d" % ARITH_ERROR[result.code]
        record = FaultRecord.from_fault(result, POSITION)
        return "F " + encode_fault_record(record, "run-time").hex()
    if isinstance(result, bool):
        return "B %d" % int(result)
    prefix = "R 0 " if form_result else "V "
    return prefix + "%d %s" % (TAG[result.type], hexle(result.type, result.value))


def ref(name: str, form: str, t: str, *operands, mode=None, target=None):
    try:
        return arith.op(name, form, t, *operands, mode=mode, target=target)
    except OpenCase:
        if name == "clamp":                          # lo > hi: E_DOMAIN at the callee (decided 2026-10-05)
            return Fault("E_DOMAIN", arith.op_id("clamp", "checked", [t]),
                         tuple(Value(t, v) for v in operands))
        raise


def sat(target: str, x: int) -> Value:
    """`x as| target`: sat(target, x), which never faults (SPEC-01 IM-50; decision 2026-10-10 on OQ-272).
    cint_ref's arith.py does not take `as|` yet, so this is the saturation of its types.py."""
    return Value(target, int_type(target).sat(x))


def generate(seed: int = 15, scale: int = 1):
    """The case lines of every wide width."""
    rng = random.Random(seed)
    out = []

    def add(kind, tag, mode, aux, args, result):
        n = len(out)
        out.append("%d %s %d %d %d %s = %s" % (n, kind, tag, mode, aux, " ".join(args), result))

    for t, tag in WIDE:
        w = width(t)
        big = 60 * scale if w <= 256 else 30 * scale
        vals = values(rng, t, big)
        prs = pairs(rng, t, 3 * big)
        few = prs[:: max(1, len(prs) // (60 * scale))]
        for op in ("add", "sub", "mul"):
            for x, y in prs:
                a = [arg(t, x), arg(t, y)]
                add(op, tag, 0, 0, a, outcome(ref(op, "checked", t, x, y)))
                add(op + "_wrap", tag, 0, 0, a, outcome(ref(op, "wrap", t, x, y)))
                add(op + "_sat", tag, 0, 0, a, outcome(ref(op, "sat", t, x, y)))
                add(op + "_result", tag, 0, 0, a, outcome(ref(op, "checked", t, x, y), True))
        for x in vals:
            add("neg", tag, 0, 0, [arg(t, x)], outcome(ref("neg", "checked", t, x)))
            add("neg_wrap", tag, 0, 0, [arg(t, x)], outcome(Value(t, int_type(t).wrap(-x))))
            add("not", tag, 0, 0, [arg(t, x)], outcome(ref("not", "checked", t, x)))
            add("abs", tag, 0, 1, [arg(t, x)], outcome(ref("abs", "checked", t, x)))
            add("isqrt", tag, 0, 13, [arg(t, x)], outcome(ref("isqrt", "checked", t, x)))
            for m, mode in enumerate(MODES, 1):
                add("isqrt_round", tag, m, 14, [arg(t, x)], outcome(ref("isqrt_round", "checked", t, x, mode=mode)))
        for _ in range(4 * big):                     # squares and their neighbors
            r = rng.getrandbits(rng.randint(1, w // 2 - 1))
            for x in (r * r - 1, r * r, r * r + 1, r * r + r, r * r + r + 1):
                if 0 <= x <= int_type(t).max:
                    add("isqrt", tag, 0, 13, [arg(t, x)], outcome(ref("isqrt", "checked", t, x)))
                    m = rng.randint(1, 9)
                    add("isqrt_round", tag, m, 14, [arg(t, x)],
                        outcome(ref("isqrt_round", "checked", t, x, mode=MODES[m - 1])))
        for op in ("div", "rem"):
            for x, y in prs:
                a = [arg(t, x), arg(t, y)]
                add(op, tag, 0, 0, a, outcome(ref(op, "checked", t, x, y)))
                add(op + "_result", tag, 0, 0, a, outcome(ref(op, "checked", t, x, y), True))
        for op in ("div_trunc", "rem_trunc", "div_euclid", "rem_euclid"):
            for x, y in prs:
                add(op, tag, 0, BUILTIN[op], [arg(t, x), arg(t, y)], outcome(ref(op, "checked", t, x, y)))
        for m, mode in enumerate(MODES, 1):
            for x, y in prs[:: 2]:
                add("div_round", tag, m, 10, [arg(t, x), arg(t, y)],
                    outcome(ref("div_round", "checked", t, x, y, mode=mode)))
        for op in ("and", "or", "xor", "min", "max"):
            for x, y in few:
                add(op, tag, 0, 0, [arg(t, x), arg(t, y)], outcome(ref(op, "checked", t, x, y)))
        for op, f in (("eq", int.__eq__), ("ne", int.__ne__), ("lt", int.__lt__), ("le", int.__le__),
                      ("gt", int.__gt__), ("ge", int.__ge__)):
            for x, y in few + [(x, x) for x, _ in few[:20]]:
                add(op, tag, 0, 0, [arg(t, x), arg(t, y)], "B %d" % int(f(x, y)))
        for x, y in few:
            lo, hi = sorted((x, y)) if rng.random() < 0.8 else (max(x, y), min(x, y))
            for v in (vals[rng.randrange(len(vals))], lo, hi):
                add("clamp", tag, 0, 5, [arg(t, v), arg(t, lo), arg(t, hi)], outcome(ref("clamp", "checked", t, v, lo, hi)))
        counts = [(c, "I64") for c in (0, 1, 63, 64, 65, w // 2, w - 2, w - 1, w, w + 1, -1, -(1 << 63), (1 << 63) - 1)]
        counts += [(5, "U8"), (255, "U8"), (w - 1, "U16"), (w, "U32"), ((1 << 64) - 1, "U64"), (-7, "I8")]
        for x in vals:
            for c, ct in counts[:: 1 if x in boundaries(t) else 3] + [(rng.randrange(w), "I64")]:
                if not int_type(ct).contains(c):
                    continue
                k = Value(ct, c)
                a = [arg(t, x), arg(ct, c)]
                add("shl", tag, 0, 0, a, outcome(ref("shl", "checked", t, x, k)))
                add("shl_wrap", tag, 0, 0, a, outcome(ref("shl", "wrap", t, x, k)))
                add("shr", tag, 0, 0, a, outcome(ref("shr", "checked", t, x, k)))
                add("shl_result", tag, 0, 0, a, outcome(ref("shl", "checked", t, x, k), True))
                if not int_type(t).signed:
                    add("rotl", tag, 0, 0, a, outcome(ref("rotl", "checked", t, x, k)))
                    add("rotr", tag, 0, 0, a, outcome(ref("rotr", "checked", t, x, k)))
        for ct, _ in WIDE:
            for c in wide_counts(ct, w):
                k = Value(ct, c)
                for x in (vals[rng.randrange(len(vals))], 1, int_type(t).max):
                    a = [arg(t, x), arg(ct, c)]
                    add("shl", tag, 0, 0, a, outcome(ref("shl", "checked", t, x, k)))
                    add("shl_wrap", tag, 0, 0, a, outcome(ref("shl", "wrap", t, x, k)))
                    add("shr", tag, 0, 0, a, outcome(ref("shr", "checked", t, x, k)))
                    add("shl_result", tag, 0, 0, a, outcome(ref("shl", "checked", t, x, k), True))
                    if not int_type(t).signed:
                        add("rotl", tag, 0, 0, a, outcome(ref("rotl", "checked", t, x, k)))
                        add("rotr", tag, 0, 0, a, outcome(ref("rotr", "checked", t, x, k)))
        for target, rtag in WIDE + NARROW:
            for m in (1, 2, 3, 4, 5, 6, 7, 8, 9):
                for x, y in prs[m:: 9 * (2 if w >= 512 else 1)]:
                    z = patterned(rng, t) if rng.random() < 0.7 else int_type(t).wrap(rng.choice((1, -1, 0, 3, -7)))
                    add("muldiv", tag, m, rtag, [arg(t, x), arg(t, y), arg(t, z)],
                        outcome(ref("muldiv", "checked", t, x, y, z, mode=MODES[m - 1], target=target)))
        signed = int_type(t).signed
        if w <= (512 if signed else 256):
            wide2 = "%s%d" % (t[0], 2 * w)
            for x, y in prs[:: 2]:
                add("mul_full", tag, 0, TAG[wide2], [arg(t, x), arg(t, y)], outcome(ref("mul_full", "checked", t, x, y)))
        if signed and w <= 512:
            for x in vals:
                add("uabs", tag, 0, TAG["U%d" % w], [arg(t, x)], outcome(ref("uabs", "checked", t, x)))
        for target, rtag in WIDE + NARROW:
            for x in vals:
                add("as", tag, 0, rtag, [arg(t, x)], outcome(ref("as", "checked", t, x, target=target)))
                add("as_wrap", tag, 0, rtag, [arg(t, x)], outcome(ref("as", "wrap", t, x, target=target)))
                add("as_sat", tag, 0, rtag, [arg(t, x)], outcome(sat(target, x)))
        for source, stag in NARROW:
            st = int_type(source)
            for x in (st.min, st.max, 0, 1, -1, rng.randint(st.min, st.max)):
                if st.contains(x):
                    add("as", stag, 0, tag, [arg(source, x)], outcome(ref("as", "checked", source, x, target=t)))
                    add("as_wrap", stag, 0, tag, [arg(source, x)], outcome(ref("as", "wrap", source, x, target=t)))
                    add("as_sat", stag, 0, tag, [arg(source, x)], outcome(sat(t, x)))
    for t, tag in NARROW:
        nt = int_type(t)
        for ct, _ in WIDE + NARROW:
            for c in wide_counts(ct, nt.width):
                k = Value(ct, c)
                for x in (1, -1, nt.max, nt.min, rng.randint(nt.min, nt.max)):
                    if not nt.contains(x):
                        continue
                    a = [arg(t, x), arg(ct, c)]
                    add("shl", tag, 0, 0, a, outcome(ref("shl", "checked", t, x, k)))
                    add("shl_wrap", tag, 0, 0, a, outcome(ref("shl", "wrap", t, x, k)))
                    add("shr", tag, 0, 0, a, outcome(ref("shr", "checked", t, x, k)))
                    add("shl_result", tag, 0, 0, a, outcome(ref("shl", "checked", t, x, k), True))
    for source, stag in NARROW:
        st = int_type(source)
        for target, rtag in NARROW:
            dt = int_type(target)
            xs = (st.min, st.max, 0, 1, -1, dt.min, dt.max, dt.min - 1, dt.max + 1, rng.randint(st.min, st.max))
            for x in dict.fromkeys(xs):
                if st.contains(x):
                    add("as", stag, 0, rtag, [arg(source, x)], outcome(ref("as", "checked", source, x, target=target)))
                    add("as_wrap", stag, 0, rtag, [arg(source, x)], outcome(ref("as", "wrap", source, x, target=target)))
                    add("as_sat", stag, 0, rtag, [arg(source, x)], outcome(sat(target, x)))
    for t, t2 in (("I64", "I128"), ("U64", "U128")):
        for x, y in pairs(rng, t, 200):
            add("mul_full", TAG[t], 0, TAG[t2], [arg(t, x), arg(t, y)], outcome(Value(t2, x * y)))
    return out


def legs():
    if os.name == "nt":
        return ["msvc"]
    if sys.platform == "darwin":
        return ["apple-clang"]
    return [leg for leg in ("gcc", "clang") if shutil.which(leg)]


class WideTypes(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lines = generate()

    def test_cases_reach_every_kind(self):
        kinds = {line.split(" ", 2)[1] for line in self.lines}
        self.assertEqual(len(kinds), 51)
        outcomes = {line.split(" = ", 1)[1][0] for line in self.lines}
        self.assertEqual(outcomes, {"V", "R", "B", "F"})

    def test_every_build_agrees(self):
        found = legs()
        if not found:
            self.skipTest("no gcc or clang on this host")
        root = cc.build_root() / "rt-wide-types"
        root.mkdir(parents=True, exist_ok=True)
        sanitize = os.environ.get("INTOS_SANITIZE") == "1"
        pairs = int(os.environ.get("CINT_WIDE_PAIRS", "1000000"))
        with tempfile.TemporaryDirectory(prefix="wide-", dir=root) as tmp:
            work = pathlib.Path(tmp)
            cases = work / "cases.txt"
            cases.write_bytes("".join(line + "\n" for line in self.lines).encode("ascii"))
            for leg in found:
                for opt in ("0", "2"):
                    for helpers in ("portable", "builtin"):
                        name = "%s-O%s-%s" % (leg, opt, helpers)
                        with self.subTest(build=name):
                            out = work / name
                            out.mkdir()
                            tc = cc.Toolchain(leg, opt, helpers, sanitize and leg in ("gcc", "clang"))
                            objs = tc.compile([ROOT / "rt" / f for f in ("cint_rt.c", "cint_state.c", "cint_rec.c",
                                                                          "tests/test_wide.c")], out, name)
                            exe = tc.link_exe(objs, out / "test_wide", name)
                            run = subprocess.run([str(exe), str(cases), str(pairs)], capture_output=True,
                                                 timeout=300 + pairs // 100000)
                            self.assertEqual(run.returncode, 0, run.stderr[-3000:])
                            self.assertEqual(run.stderr, b"")
                            self.assertIn(b"helper mode: " + helpers.encode("ascii"), run.stdout)
                            self.assertIn(b"limb steps: %d pairs, 0 disagree" % pairs, run.stdout)
                            self.assertIn(b"wide cases: %d, passed %d, failed 0" % (len(self.lines), len(self.lines)),
                                          run.stdout)


if __name__ == "__main__":
    unittest.main()
