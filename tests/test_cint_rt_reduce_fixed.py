"""The reductions over fixed-point elements and dot with a rounding mode against cint_ref (rt/OPEN.md
RT-OQ-57; rt/cint_fixed.h; SPEC-02 R-4; SPEC-01 IM-77 to IM-87; box 15 ruling BX15-04 A).

rt/tests/test_reduce_fixed.c reads cases written here, one per line: `<id> <op> <elem> <result> <mode>
<init> X <origin> <rank> <extent>... <stride>... <count> <element>... [Y <view of the same form>] =
<outcome> fuel <units>`. op is a CINT_REDUCE_* number of rt/cint_rt.h, elem and result are type
words (CINT_QT of rt/cint_fixed.h for Q<i>.<f>, the tag for I8 to U64), mode is dot's rounding mode
(1 to 9, as for cint_rt_builtin) or 0, init is `-` or a value, and X and Y give the buffer of xs and
of dot's ys with the view over it (origin, extents and strides in elements). A value is the
hexadecimal of its little-endian bytes, a fixed-point one that of its raw storage. The outcome is
`V <word> <hex>` for a result or `F <hex>`, the canonical bytes of the run-time fault record
(SPEC-01 IM-149) at site 1 of `reduce.ci` (line 1, column 1) with an empty stack; units is the fuel
the call charged.

`cint_ref` (ref/cint_ref/reduce.py) computes sum, sum_wrap, sum_sat, fold_checked, min and max over
the view's raw values in logical order, and dot is computed as Machine.reduction computes it
(ref/cint_ref/exec.py): the shape check first, then the exact sum of the products at scale 2^(2f),
rounded once to the result's 2^g when 2f > g (half_even unless a mode is given), shifted exactly
otherwise, and one range check. The driver calls cint_rt_reduce, or cint_rt_dot_fixed for dot, and
then the wrapper of the result type where one applies (cint_reduce_q_<S> and cint_dot_q_<S> of
rt/cint_fixed.h, cint_reduce_<t> of rt/cint_rt.h), and both calls must give the outcome.

The cases cover every op on Q types of 8 to 64 bits at several scales, at the bounds of the storage
and on seeded random values: sums that overflow at the final check, fold_checked overflowing at its
first and a later step, saturation at either bound, empty views with and without init, views of
rank 1 to 4 that are transposed, strided, reversed or of stride 0; dot between formats in both
directions in every mode, with ties, an exact sum of up to 2^142 shifted 63 places, and the shape
fault before any fuel; the same ops on I8 to U64, whose code they share; the inputs of the five
conformance cases of box 15 unit 4e2 (conformance/fixed/), whose frozen outcomes the oracle is
checked against; and the arguments no emitted code passes, each refused with `dispatch.admit`. The
driver also checks a kernel's steps (cint_zacc_add with cint_ksum_q_<S>, cint_kfold_q_<S>,
cint_rt_fault_reduce_empty) on their own, E_FUEL inside an entry, and a context that already holds a
fault.

The driver, with rt/cint_rt.c, rt/cint_state.c and rt/cint_rec.c, is built for each leg of this host
(gcc, and clang when installed, on Linux; apple-clang on macOS; msvc on Windows) at -O0 and -O2
(MSVC /Od and /O2) in both helper modes, with the flags of tools/cint_check.py and, under
`workflow.py check --sanitize`, its sanitizers on gcc and clang. Build outputs go under
<build>/rt-reduce-fixed, where <build> is CINT_BUILD or cint-build in the system temporary
directory.
"""
import itertools
from fractions import Fraction
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
from cint_ref import arith, fixed  # noqa: E402
from cint_ref import reduce as red  # noqa: E402
from cint_ref.faults import Fault, FaultRecord, Position, encode_fault_record  # noqa: E402
from cint_ref.types import Value, int_type  # noqa: E402

INTS = (("I8", 0x11), ("I16", 0x12), ("I32", 0x13), ("I64", 0x14),
        ("U8", 0x21), ("U16", 0x22), ("U32", 0x23), ("U64", 0x24))
INT_TAG = dict(INTS)
STORAGE = {8: 0x11, 16: 0x12, 32: 0x13, 64: 0x14}
QTYPES = ("Q4.4", "Q1.7", "Q8.0", "Q8.8", "Q1.15", "Q16.0", "Q16.16", "Q24.8", "Q1.31", "Q32.32", "Q48.16",
          "Q1.63", "Q64.0")
OP = {"sum": 1, "sum_wrap": 2, "sum_sat": 3, "fold_add": 4, "fold_mul": 5, "min": 6, "max": 7, "count": 8, "dot": 9}
MODES = arith.ROUND_MODES                          # numbered 1 to 9, as for cint_rt_builtin
MODE_NUM = {m: k + 1 for k, m in enumerate(MODES)}
POSITION = Position("reduce.ci", 1, 1)
REFUSED = Fault("E_UNSUPPORTED", "dispatch.admit", ())


def word(t: str) -> int:
    """The type word of rt/cint_fixed.h: CINT_QT(storage, f), or the integer tag."""
    if t in INT_TAG:
        return INT_TAG[t]
    q = fixed.q_type(t)
    return (q.f << 16) | (0x31 << 8) | STORAGE[q.width]


def storage(t: str):
    return fixed.q_type(t).storage if fixed.is_q_type(t) else int_type(t)


def frac(t: str) -> int:
    return fixed.q_type(t).f if fixed.is_q_type(t) else 0


def hexle(t: str, v: int) -> str:
    w = storage(t).width
    return (v & ((1 << w) - 1)).to_bytes(w // 8, "little").hex()


def boundaries(t: str) -> list:
    s = storage(t)
    f = frac(t)
    vals = {0, 1, -1, 2, -2, 3, s.max, s.min, s.max - 1, s.min + 1, 1 << 63, -(1 << 63)}
    for k in (f - 1, f, f + 1):
        if k >= 0:
            vals.update((1 << k, -(1 << k), 3 << k, -(3 << k), (1 << k) + 1, -(1 << k) - 1))
    return sorted(v for v in vals if s.contains(v))


def rand(rng: random.Random, t: str) -> int:
    s = storage(t)
    bits = rng.randint(1, s.width)
    v = rng.randrange(-(1 << (bits - 1)), 1 << (bits - 1)) if s.signed else rng.randrange(0, 1 << bits)
    return max(s.min, min(s.max, v))


def logical(buf: list, origin: int, shape: tuple, stride: tuple) -> list:
    """The elements of the view in logical order, the last index fastest (SPEC-01 6.1)."""
    if any(e == 0 for e in shape):
        return []
    return [buf[origin + sum(i * s for i, s in zip(idx, stride))]
            for idx in itertools.product(*(range(e) for e in shape))]


def outcome(r) -> str:
    if isinstance(r, Fault):
        return "F " + encode_fault_record(FaultRecord.from_fault(r, POSITION), "run-time").hex()
    return "V %d %s" % (word(r.type), hexle(r.type, r.value))


def dot_ref(elem: str, result: str, a: list, b: list, mode=None):
    """exec.py Machine.reduction's dot: the shape check, then the exact sum, rounded once for fixed point."""
    q = fixed.is_q_type(elem)
    if q:
        qa, qr = fixed.q_type(elem), fixed.q_type(result)
        shift = 2 * qa.f - qr.f
        mode = (mode or "half_even") if shift > 0 else None
        operation = arith.op_id("dot", "checked", [qa.ident, qr.ident], mode)
    else:
        operation = "dot.checked.%s.%s" % (elem.lower(), result.lower())
    if len(a) != len(b):
        return Fault("E_SHAPE", operation, (Value("I64", 0), Value("I64", len(b))), None, Value("I64", len(a)))
    total = sum(x * y for x, y in zip(a, b))
    if q:
        total = arith.round_div(total, 1 << shift, mode) if shift > 0 else total << -shift
    t = storage(result)
    if not t.contains(total):
        return Fault("E_OVERFLOW", operation, (Value("I64", len(a)),), total,
                     Value(result, t.max if total > t.max else t.min))
    return Value(result, total)


def expect(op: str, elem: str, result: str, items: list, init):
    if op == "sum":
        return red.op("sum", "checked", elem, items, target=result)
    if op in ("fold_add", "fold_mul"):
        return red.op("fold_checked", op[5:], elem, items, init=init)
    if op == "sum_wrap":
        return red.op("sum_wrap", "wrap", elem, items)
    if op == "sum_sat":
        return red.op("sum_sat", "sat", elem, items)
    return red.op("reduce_" + op, "checked", elem, items, init=init)


class Cases:
    def __init__(self):
        self.lines = []

    def view(self, elem: str, buf: list, origin: int, shape: tuple, stride: tuple) -> str:
        return " ".join([str(origin), str(len(shape))] + [str(e) for e in shape + stride] + [str(len(buf))] +
                        [hexle(elem, v) for v in buf])

    def add(self, op: str, elem, result, mode: int, init, xview: str, yview, result_text: str, units: int):
        line = "%d %d %d %d %d %s X %s" % (len(self.lines), OP[op], elem if isinstance(elem, int) else word(elem),
                                           result if isinstance(result, int) else word(result), mode,
                                           "-" if init is None else hexle(elem, init), xview)
        if yview is not None:
            line += " Y " + yview
        self.lines.append("%s = %s fuel %d" % (line, result_text, units))

    def reduce(self, op: str, elem: str, buf: list, init=None, origin=0, shape=None, stride=None, result=None):
        result = elem if result is None else result
        shape = (len(buf),) if shape is None else shape
        stride = (1,) * len(shape) if stride is None else stride
        items = logical(buf, origin, shape, stride)
        self.add(op, elem, result, 0, init, self.view(elem, buf, origin, shape, stride), None,
                 outcome(expect(op, elem, result, items, init)), -(-len(items) // 64))

    def dot(self, elem: str, result: str, a: list, b: list, mode=None, n=None, m=None, sa=1, sb=1):
        """dot over views of n and m elements of strides sa and sb over a and b (stride 0 repeats)."""
        n = len(a) if n is None else n
        m = len(b) if m is None else m
        xa, xb = logical(a, 0, (n,), (sa,)), logical(b, 0, (m,), (sb,))
        r = dot_ref(elem, result, xa, xb, mode)
        rounds = fixed.is_q_type(elem) and 2 * frac(elem) > frac(result)
        self.add("dot", elem, result, MODE_NUM[mode or "half_even"] if rounds else 0, None,
                 self.view(elem, a, 0, (n,), (sa,)), self.view(elem, b, 0, (m,), (sb,)), outcome(r),
                 0 if n != m else -(-n // 64))

    def refused(self, op: str, elem, result, buf: list, mode=0, init=None, ys=False, store="I32"):
        v = self.view(store, buf, 0, (len(buf),), (1,))
        self.add(op, elem, result, mode, None if init is None else init, v, v if ys else None, outcome(REFUSED), 0)


def q_sequences(rng, t, b):
    def seq(k):
        return [rng.choice(b) if rng.random() < 0.3 else rand(rng, t) for _ in range(k)]
    return seq


def generate(seed: int = 57) -> list:
    rng = random.Random(seed)
    c = Cases()
    lengths = (0, 1, 2, 3, 7, 64, 65, 130)
    for t in QTYPES + tuple(name for name, _ in INTS):
        s = storage(t)
        b = boundaries(t)
        seq = q_sequences(rng, t, b)
        neg = -1 if s.signed else 0
        # sum: the exact total, checked once against the element's format (a wider one for integers).
        for k in lengths:
            c.reduce("sum", t, seq(k))
        for k in (2, 3):
            c.reduce("sum", t, [s.max] * k)
            c.reduce("sum", t, [s.min] * k if s.signed else [s.max, 1] * k)
            c.reduce("sum", t, [s.max, 1] + seq(k - 2))
        c.reduce("sum", t, [s.max, s.min, s.max])
        c.reduce("sum", t, [s.max], shape=(1000,), stride=(0,))
        c.reduce("sum", t, [s.min if s.signed else s.max], shape=(1000,), stride=(0,))
        if t in INT_TAG:
            c.reduce("sum", t, seq(9), result="I64" if s.signed else "U64")
            c.reduce("sum", t, [s.max] * 3, result="I8" if s.signed else "U8")
        # sum_wrap and sum_sat: at the storage width.
        for op in ("sum_wrap", "sum_sat"):
            for k in lengths:
                c.reduce(op, t, seq(k))
            for k in (2, 3, 6):
                c.reduce(op, t, [s.max] * k)
                c.reduce(op, t, [s.min, neg] * k)
            c.reduce(op, t, [s.max, 1, s.min, neg] * 3)
        # fold_checked(add), and (mul) on integers: overflowing at the first step and later.
        for k in lengths[:6]:
            c.reduce("fold_add", t, seq(k), init=rng.choice(b))
        for init in (s.max, s.min, 0, 1):
            c.reduce("fold_add", t, [1, neg, s.max, s.min], init=init)
        c.reduce("fold_add", t, [s.max, 0, 1, 0], init=0, shape=(2, 2), stride=(1, 2))
        c.reduce("fold_add", t, [], init=s.min)
        if t in INT_TAG:
            for init in (1, s.max):
                c.reduce("fold_mul", t, [2, 3, 1, s.max], init=init)
        # min and max, with and without init, over empty views too.
        for op in ("min", "max"):
            for k in lengths[:6]:
                c.reduce(op, t, seq(k))
                c.reduce(op, t, seq(k), init=rng.choice(b))
            c.reduce(op, t, b)
            c.reduce(op, t, list(reversed(b)), init=0)
        # Views: rank 2 transposed, rank 3 strided, rank 4, reversed, stride 0 and empty.
        buf = seq(24)
        for op in ("sum", "fold_add", "min", "max", "sum_sat", "sum_wrap"):
            init = 0 if op == "fold_add" else None
            c.reduce(op, t, buf, init=init, shape=(4, 6), stride=(6, 1))
            c.reduce(op, t, buf, init=init, shape=(6, 4), stride=(1, 6))
            c.reduce(op, t, buf, init=init, shape=(2, 3, 2), stride=(12, 2, 7))
            c.reduce(op, t, buf, init=init, shape=(2, 2, 2, 3), stride=(12, 6, 3, 1))
            c.reduce(op, t, buf, init=init, origin=23, shape=(24,), stride=(-1,))
            c.reduce(op, t, buf, init=init, origin=5, shape=(3, 4), stride=(0, 2))
            c.reduce(op, t, buf, init=init, shape=(3, 0), stride=(1, 1))
    # dot between every pair of formats: the default mode on random and boundary values, every mode
    # on values whose products leave a remainder, ties, overflow at both bounds, and the shape fault.
    for elem in QTYPES:
        s = storage(elem)
        b = boundaries(elem)
        seq = q_sequences(rng, elem, b)
        for result in QTYPES:
            rounds = 2 * frac(elem) > frac(result)
            for k in (0, 1, 3, 65):
                c.dot(elem, result, seq(k), seq(k))
            c.dot(elem, result, [s.max] * 2, [s.max] * 2)
            c.dot(elem, result, [s.max] * 2, [s.min] * 2)
            c.dot(elem, result, [s.min] * 2, [s.min] * 2)
            if rounds:
                shift = 2 * frac(elem) - frac(result)
                half = 1 << (shift - 1)
                for mode in MODES:
                    c.dot(elem, result, seq(5), seq(5), mode=mode)
                    c.dot(elem, result, [rand(rng, elem)], [rand(rng, elem)], mode=mode)
                    if half <= s.max:                       # a tie at +-0.5 and +-1.5 of the result's ulp
                        c.dot(elem, result, [1], [half], mode=mode)
                        c.dot(elem, result, [-1], [half], mode=mode)
                        c.dot(elem, result, [3], [half], mode=mode)
                        c.dot(elem, result, [-3], [half], mode=mode)
                    c.dot(elem, result, [s.min] * 2, [s.min] * 2, mode=mode)
                    c.dot(elem, result, [s.min, s.max], [s.max, s.min], mode=mode)
                c.dot(elem, result, seq(3), seq(4), mode="floor")
            c.dot(elem, result, seq(3), seq(4))
            c.dot(elem, result, seq(0), seq(2))
        c.dot(elem, elem, seq(4), seq(4), n=2, m=2, sa=2, sb=1)
        c.dot(elem, elem, [s.min], [s.min], n=513, m=513, sa=0, sb=0)
    # The widest exact sums: 100,000 products of 2^126, shifted 63 places, and rounded 126 places.
    c.dot("Q64.0", "Q1.63", [-(1 << 63)], [-(1 << 63)], n=100000, m=100000, sa=0, sb=0)
    c.dot("Q1.63", "Q64.0", [-(1 << 63)], [-(1 << 63)], n=100000, m=100000, sa=0, sb=0, mode="ceil")
    c.dot("Q1.63", "Q64.0", [-(1 << 63)], [(1 << 63) - 1], n=3, m=3, sa=0, sb=0, mode="half_away")
    # Integer dot.
    for elem, _ in INTS:
        s = storage(elem)
        seq = q_sequences(rng, elem, boundaries(elem))
        for result in (elem, "I64", "U64", "I8"):
            for k in (0, 1, 3, 65):
                c.dot(elem, result, seq(k), seq(k))
            c.dot(elem, result, [s.max] * 3, [s.max] * 3)
        c.dot(elem, elem, seq(2), seq(3))
    # The inputs of the conformance cases of box 15 unit 4e2.
    for case in FROZEN.values():
        case["add"](c)
    # Arguments no emitted code passes.
    c.refused("sum", "Q16.16", "Q32.32", [1])                                     # a Q sum into another format
    c.refused("sum", "Q16.16", "I64", [1])                                        # Q into an integer
    c.refused("sum", "I32", "Q16.16", [1])
    c.refused("sum_wrap", "Q16.16", "Q8.8", [1])
    c.refused("min", "Q16.16", "Q24.8", [1])
    c.refused("fold_mul", "Q16.16", "Q16.16", [1], init=1)                        # no rule (SPEC-02 R-4)
    c.refused("fold_add", "Q16.16", "Q16.16", [1])                                # fold_checked without init
    c.refused("count", "Q16.16", "I64", [1])
    c.refused("dot", "Q16.16", "Q16.16", [1], ys=True)                            # rounds, and no mode
    c.refused("dot", "Q16.16", "Q32.32", [1], mode=1, ys=True)                    # a mode, and it does not round
    c.refused("dot", "Q16.16", "Q16.16", [1], mode=10, ys=True)
    c.refused("dot", "I32", "I64", [1], mode=5, ys=True)
    c.refused("dot", "Q16.16", "I64", [1], ys=True)
    c.refused("dot", "Q16.16", "Q16.16", [1], mode=5)                             # dot without ys
    c.refused("sum", (16 << 16) | (0x31 << 8) | 0x12, (16 << 16) | (0x31 << 8) | 0x12, [1], store="I16")  # Q0.16
    c.refused("sum", (4 << 16) | (0x31 << 8) | 0x22, (4 << 16) | (0x31 << 8) | 0x22, [1], store="U16")   # U16 storage
    c.refused("min", (4 << 8) | 0x12, (4 << 8) | 0x12, [1], store="I16")                                 # no Q tag
    c.lines.append("%d 0 %d %d 0 - X 0 1 1 1 1 %s = %s fuel 0" % (len(c.lines), word("Q16.16"), word("Q16.16"),
                                                                   hexle("Q16.16", 1), outcome(REFUSED)))
    c.lines.append("%d 10 %d %d 0 - X 0 1 1 1 1 %s = %s fuel 0" % (len(c.lines), word("Q16.16"), word("Q16.16"),
                                                                    hexle("Q16.16", 1), outcome(REFUSED)))
    return c.lines


def _raw(t: str, x: str) -> int:
    """The raw value of the decimal x in type t, which holds it exactly."""
    r = Fraction(x) * (1 << frac(t))
    assert r.denominator == 1, (t, x)
    return r.numerator


# conformance/fixed/<name>.ci: how each case's reductions are added, and what the oracle gives for them
# next to the frozen fields of its .expect (`return`, or the fault's operation, operand, exact and limit).
FROZEN = {
    "reductions_q16_16": {
        "add": lambda c: [c.reduce(op, "Q16.16", [_raw("Q16.16", x) for x in ("1.5", "2.25", "-0.5", "0.125")],
                                   init=init)
                          for op, init in (("sum", None), ("min", None), ("max", None), ("sum_wrap", None),
                                           ("sum_sat", None), ("fold_add", _raw("Q16.16", "1.0")),
                                           ("min", _raw("Q16.16", "-1.0")))],
        "got": lambda: expect("sum", "Q16.16", "Q16.16",
                              [_raw("Q16.16", x) for x in ("1.5", "2.25", "-0.5", "0.125")], None),
    },
    "sum_overflow_q8_8": {
        "add": lambda c: c.reduce("sum", "Q8.8", [_raw("Q8.8", "100.0")] * 2),
        "got": lambda: expect("sum", "Q8.8", "Q8.8", [_raw("Q8.8", "100.0")] * 2, None),
    },
    "dot_one_rounding": {
        "add": lambda c: (c.dot("Q16.16", "Q16.16", [1, 1], [_raw("Q16.16", "0.5")] * 2),
                          c.dot("Q16.16", "Q32.32", [_raw("Q16.16", x) for x in ("1.5", "0.25")],
                                [_raw("Q16.16", x) for x in ("0.5", "3.0")]),
                          c.dot("Q16.16", "Q16.16", [1, 1], [_raw("Q16.16", "0.5")] * 2, mode="floor")),
        "got": lambda: dot_ref("Q16.16", "Q16.16", [1, 1], [_raw("Q16.16", "0.5")] * 2),
    },
    "dot_overflow_q8_8": {
        "add": lambda c: c.dot("Q8.8", "Q8.8", [_raw("Q8.8", "100.0")] * 2, [_raw("Q8.8", "100.0")] * 2, mode="floor"),
        "got": lambda: dot_ref("Q8.8", "Q8.8", [_raw("Q8.8", "100.0")] * 2, [_raw("Q8.8", "100.0")] * 2, "floor"),
    },
    "kernel_scale_reduce": {
        "add": lambda c: c.reduce("sum", "Q16.16", [_raw("Q16.16", x) for x in ("1.5", "-0.25", "2.0")]),
        "got": lambda: expect("sum", "Q16.16", "Q16.16", [_raw("Q16.16", x) for x in ("1.5", "-0.25", "2.0")], None),
    },
}


def legs():
    if os.name == "nt":
        return ["msvc"]
    if sys.platform == "darwin":
        return ["apple-clang"]
    return [leg for leg in ("gcc", "clang") if shutil.which(leg)]


class ReduceFixed(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lines = generate()

    def test_cases_reach_every_outcome(self):
        fields = [line.split(" ", 5) for line in self.lines]
        self.assertEqual({int(f[1]) for f in fields}, set(range(0, 11)))
        # Every mode on a dot that rounds, and a fixed-point result of each storage width.
        self.assertEqual({int(f[4]) for f in fields if f[1] == "9" and " = V " in f[5]}, set(range(0, 10)))
        self.assertEqual({int(f[3]) & 0xff for f in fields if (int(f[3]) >> 8) & 0xff == 0x31 and " = V " in f[5]},
                         set(STORAGE.values()))
        text = "\n".join(self.lines)
        for marker in ("= V ", "= F ", "fuel 0", "fuel 1563"):
            self.assertIn(marker, text)

    def test_oracle_gives_the_frozen_outcomes(self):
        """The oracle's outcome for each conformance case's reduction matches the case's frozen .expect."""
        for name, case in FROZEN.items():
            with self.subTest(case=name):
                expected = dict(line.split(" ", 1) for line in
                                (ROOT / "conformance" / "fixed" / (name + ".expect")).read_text().splitlines())
                r = case["got"]()
                if expected["outcome"] == "fault":
                    self.assertIsInstance(r, Fault)
                    q = fixed.q_type(r.limit.type)
                    self.assertEqual((r.code, r.operation, ["%s %d" % (v.type, v.value) for v in r.operands],
                                      str(r.exact), "%s raw %d" % (q.name, r.limit.value)),
                                     (expected["fault.code"], expected["fault.operation"],
                                      [expected["fault.operand"]], expected["fault.exact"], expected["fault.limit"]))
                else:
                    # Each value case returns the raw value of that reduction.
                    self.assertEqual("I64 %d" % r.value, expected["return"])

    def test_every_build_agrees(self):
        found = legs()
        if not found:
            self.skipTest("no gcc or clang on this host")
        root = cc.build_root() / "rt-reduce-fixed"
        root.mkdir(parents=True, exist_ok=True)
        sanitize = os.environ.get("INTOS_SANITIZE") == "1"
        with tempfile.TemporaryDirectory(prefix="reduce-", dir=root) as tmp:
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
                                                                          "tests/test_reduce_fixed.c")], out, name)
                            exe = tc.link_exe(objs, out / "test_reduce_fixed", name)
                            run = subprocess.run([str(exe), str(cases)], capture_output=True, timeout=600)
                            self.assertEqual(run.returncode, 0, run.stderr[-3000:])
                            self.assertEqual(run.stderr, b"")
                            self.assertIn(b"helper mode: " + helpers.encode("ascii"), run.stdout)
                            self.assertIn(b"reduce cases: %d, passed %d, failed 0" % (len(self.lines),
                                                                                      len(self.lines)), run.stdout)
                            self.assertIn(b"direct checks: failed 0", run.stdout)


if __name__ == "__main__":
    unittest.main()
