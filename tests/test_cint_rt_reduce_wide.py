"""The reductions of rt/cint_wide.h with a wide side against cint_ref (rt/OPEN.md RT-OQ-54; SPEC-01
IM-77 to IM-87; SPEC-02 R-5; box 15 rulings BX15-06 and BX15-07 C).

rt/tests/test_reduce_wide.c reads cases written here, one per line: `<id> <op> <elem> <result>
<init> X <origin> <rank> <extent>... <stride>... <count> <element>... [Y <view of the same
form>] = <outcome> fuel <units>`. op is a CINT_REDUCE_* number of
rt/cint_rt.h, elem and result are type tags, init is `-` or a value, and X and Y give the buffer
of xs and of dot's ys with the view over it (origin, extents and strides in elements). A value is
the hexadecimal of its little-endian bytes (IM-14). The outcome is `V <tag> <hex>` for a result or
`F <hex>`, the canonical bytes of the run-time fault record (SPEC-01 IM-149) at site 1 of
`reduce.ci` (line 1, column 1) with an empty stack; units is the fuel the call charged.

`cint_ref` (ref/cint_ref/reduce.py) computes sum, sum_wrap, sum_sat, fold_checked, min and max
over the view's elements in logical order, and dot is computed as Machine.reduction computes it
(ref/cint_ref/exec.py): the shape check first, then the exact sum of products and one range check.
A record of `dot.checked.i1024.<R>` is of fault record version 3 (BX15-07 C), which cint_ref does
not encode yet: it is written as version 2 with the domain string `cint-core-1/fault/v3` and an
`exact` of up to 264 bytes, the layout of version 3.

The cases cover every op on I128 to I1024 and U128 to U512: sum into each wide type and into I8,
I64 and U64, sum and dot of I8 to U64 elements into wide results, at the boundaries of each type
and on seeded random values; overflow at the final check, at the first and a later step of
fold_checked, and saturation at either bound; empty views with and without init; views of rank 1
to 4 that are transposed, strided, reversed or of stride 0, so that 65,536 copies of MIN(I1024)
give dot an exact of 2^2062; dot's E_SHAPE before any fuel; and the arguments no emitted code
passes, each refused with `dispatch.admit`. The driver also checks that a faulting call leaves its
result unwritten, the steps of kernels (cint_wzacc_add, cint_wzacc_add_product, cint_rt_wzacc_fit,
cint_rt_wide_fold_step) on their own, E_FUEL inside an entry, and a context that already holds a
fault.

The driver, with rt/cint_rt.c, rt/cint_state.c and rt/cint_rec.c, is built for each leg of this
host (gcc, and clang when installed, on Linux; apple-clang on macOS; msvc on Windows) at -O0 and
-O2 (MSVC /Od and /O2) in both helper modes, with the flags of tools/cint_check.py and, under
`workflow.py check --sanitize`, its sanitizers on gcc and clang. Build outputs go under
<build>/rt-reduce-wide, where <build> is CINT_BUILD or cint-build in the system temporary
directory.
"""
import dataclasses
import itertools
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
from cint_ref import reduce as red  # noqa: E402
from cint_ref.faults import (Fault, FaultRecord, Position, RawValue, encode_fault_record,  # noqa: E402
                             z_bytes)
from cint_ref.types import Value, int_type  # noqa: E402

WIDE = (("I128", 0x15), ("I256", 0x16), ("I512", 0x17), ("I1024", 0x18),
        ("U128", 0x25), ("U256", 0x26), ("U512", 0x27))
NARROW = (("I8", 0x11), ("I16", 0x12), ("I32", 0x13), ("I64", 0x14),
          ("U8", 0x21), ("U16", 0x22), ("U32", 0x23), ("U64", 0x24))
TAG = dict(WIDE + NARROW)
OP = {"sum": 1, "sum_wrap": 2, "sum_sat": 3, "fold_add": 4, "fold_mul": 5, "min": 6, "max": 7, "count": 8, "dot": 9}
POSITION = Position("reduce.ci", 1, 1)
EDGE_LIMBS = (0, 1, (1 << 63) - 1, 1 << 63, (1 << 64) - 1)
V3_OP = "dot.checked.i1024."
Z_V3 = 264                                         # fault record version 3 (BX15-07 C)


def hexle(name: str, v: int) -> str:
    w = int_type(name).width
    return (v & ((1 << w) - 1)).to_bytes(w // 8, "little").hex()


def boundaries(name: str) -> list:
    t = int_type(name)
    w = t.width
    vals = {0, 1, -1, 2, -2, t.max, t.min, t.max - 1, t.min + 1, 1 << 63, -(1 << 63), (1 << 64) - 1,
            1 << 64, -(1 << 64), 1 << (w - 2), -(1 << (w - 2)), (1 << (w - 1)) - 1}
    return sorted(v for v in vals if t.contains(v))


def patterned(rng: random.Random, name: str) -> int:
    t = int_type(name)
    n = max(1, t.width // 64)
    used = rng.randint(1, n)
    limbs = [rng.choice(EDGE_LIMBS) if rng.random() < 0.4 else rng.getrandbits(64) for _ in range(used)]
    if rng.random() < 0.3:
        limbs[-1] >>= rng.randint(0, 63)
    v = sum(limb << (64 * i) for i, limb in enumerate(limbs))
    if rng.random() < 0.5:
        v = -v
    return v if t.contains(v) else t.wrap(v)


def small(rng: random.Random, name: str) -> int:
    t = int_type(name)
    v = rng.randint(-9, 9)
    return v if t.contains(v) else -v


def logical(buf: list, origin: int, shape: tuple, stride: tuple) -> list:
    """The elements of the view in logical order, the last index fastest (SPEC-01 6.1)."""
    if any(e == 0 for e in shape):
        return []
    return [buf[origin + sum(i * s for i, s in zip(idx, stride))]
            for idx in itertools.product(*(range(e) for e in shape))]


def encode(fault: Fault) -> str:
    record = FaultRecord.from_fault(fault, POSITION)
    if not fault.operation.startswith(V3_OP):
        return "F " + encode_fault_record(record, "run-time").hex()
    if record.exact is not None:
        z = z_bytes(record.exact)
        assert len(z) <= Z_V3, len(z)
        record = dataclasses.replace(record, exact=RawValue(b"\x0f" + len(z).to_bytes(4, "little") + z))
    data = encode_fault_record(record, "run-time")
    return "F " + data.replace(b"cint-core-1/fault/v2", b"cint-core-1/fault/v3", 1).hex()


def outcome(r) -> str:
    if isinstance(r, Fault):
        return encode(r)
    return "V %d %s" % (TAG[r.type], hexle(r.type, r.value))


REFUSED = Fault("E_UNSUPPORTED", "dispatch.admit", ())


def dot_ref(elem: str, result: str, a: list, b: list):
    """exec.py Machine.reduction on integer elements: shape check, then the exact sum, once."""
    operation = "dot.checked.%s.%s" % (elem.lower(), result.lower())
    if len(a) != len(b):
        return Fault("E_SHAPE", operation, (Value("I64", 0), Value("I64", len(b))), None, Value("I64", len(a)))
    total = sum(x * y for x, y in zip(a, b))
    t = int_type(result)
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

    def add(self, op: str, elem: str, result: str, init, xview: str, yview, result_text: str, units: int):
        line = "%d %d %d %d %s X %s" % (len(self.lines), OP[op], TAG[elem], TAG[result],
                                        "-" if init is None else hexle(elem, init), xview)
        if yview is not None:
            line += " Y " + yview
        self.lines.append("%s = %s fuel %d" % (line, result_text, units))

    def reduce(self, op: str, elem: str, result: str, buf: list, init=None, origin=0, shape=None, stride=None):
        shape = (len(buf),) if shape is None else shape
        stride = (1,) * len(shape) if stride is None else stride
        items = logical(buf, origin, shape, stride)
        units = -(-len(items) // 64)
        self.add(op, elem, result, init, self.view(elem, buf, origin, shape, stride), None,
                 outcome(expect(op, elem, result, items, init)), units)

    def dot(self, elem: str, result: str, a: list, b: list, n=None, m=None, sa=1, sb=1):
        """dot over views of n and m elements of strides sa and sb over a and b (stride 0 repeats)."""
        n = len(a) if n is None else n
        m = len(b) if m is None else m
        xa, xb = logical(a, 0, (n,), (sa,)), logical(b, 0, (m,), (sb,))
        r = dot_ref(elem, result, xa, xb)
        units = 0 if n != m else -(-n // 64)
        self.add("dot", elem, result, None, self.view(elem, a, 0, (n,), (sa,)), self.view(elem, b, 0, (m,), (sb,)),
                 outcome(r), units)

    def refused(self, op: str, elem: str, result: str, buf: list, init=None, rank=1, ys=False):
        shape = (len(buf),) if rank == 1 else (1, len(buf))
        v = self.view(elem, buf, 0, shape, (1,) * len(shape))
        self.add(op, elem, result, init, v, v if ys else None, outcome(REFUSED), 0)


def generate(seed: int = 54, scale: int = 1):
    rng = random.Random(seed)
    c = Cases()
    for elem, _ in WIDE:
        t = int_type(elem)
        b = boundaries(elem)

        def seq(k, src=None):
            return [rng.choice(b) if rng.random() < 0.3 else (src or patterned)(rng, elem) for _ in range(k)]

        lengths = (0, 1, 2, 3, 7, 64, 65, 130)
        # sum, into the element type and wider, narrower and narrow results.
        for result in [elem] + [r for r, _ in WIDE if r != elem] + ["I8", "I64", "U64"]:
            for k in lengths[: 8 if result == elem else 5]:
                c.reduce("sum", elem, result, seq(k))
        for k in (2, 3, 5):
            c.reduce("sum", elem, elem, [t.max] * k)
            c.reduce("sum", elem, elem, [t.min] * k)
            c.reduce("sum", elem, elem, [t.max, 1] + seq(k - 2))
        c.reduce("sum", elem, elem, [t.max, t.min, t.max])                      # in range, by way of the ends
        c.reduce("sum", elem, elem, [t.max], shape=(1000,), stride=(0,))       # 1,000 copies of MAX
        c.reduce("sum", elem, elem, [t.min], shape=(1000,), stride=(0,))
        c.reduce("sum", elem, "I1024" if t.signed else "U512", [t.max], shape=(1000,), stride=(0,))
        c.reduce("sum", elem, elem, [t.max, t.min] * 40)
        # sum_wrap and sum_sat: left folds at the element width.
        for op in ("sum_wrap", "sum_sat"):
            for k in lengths:
                c.reduce(op, elem, elem, seq(k))
            for k in (2, 3, 6):
                c.reduce(op, elem, elem, [t.max] * k)
                c.reduce(op, elem, elem, [t.min, -1 if t.signed else 0] * k)
            c.reduce(op, elem, elem, [t.max, 1, t.min, -1 if t.signed else 0] * 3)
            c.reduce(op, elem, elem, [t.max, t.max, t.min] if t.signed else [t.max, 5, 7])
        # fold_checked: add and mul, overflowing at the first step and later.
        for k in lengths[:6]:
            c.reduce("fold_add", elem, elem, seq(k), init=rng.choice(b))
            c.reduce("fold_mul", elem, elem, seq(k, small), init=rng.choice((1, 2, 3, t.max, t.min)))
        for init in (t.max, t.min, 0, 1):
            c.reduce("fold_add", elem, elem, [1, -1 if t.signed else 0, t.max, t.min], init=init)
            c.reduce("fold_mul", elem, elem, [2, 3, -1 if t.signed else 1, t.max], init=init)
        big = 1 << (t.width // 2)
        c.reduce("fold_mul", elem, elem, [big, big - 1, 2], init=1)               # at the bound, then past it
        c.reduce("fold_mul", elem, elem, [t.max, t.max], init=1)                  # exact up to 2^(2w - 2)
        if t.signed:
            c.reduce("fold_mul", elem, elem, [t.min, t.min], init=1)              # 2^(2w - 2): the largest exact
            c.reduce("fold_mul", elem, elem, [t.min, -1], init=1)
            c.reduce("fold_add", elem, elem, [t.min, -1], init=0)
        c.reduce("fold_add", elem, elem, [], init=t.max)
        c.reduce("fold_mul", elem, elem, [], init=t.min)
        # min and max, with and without init, over empty views too.
        for op in ("min", "max"):
            for k in lengths:
                c.reduce(op, elem, elem, seq(k))
                c.reduce(op, elem, elem, seq(k), init=rng.choice(b))
            c.reduce(op, elem, elem, b)
            c.reduce(op, elem, elem, list(reversed(b)), init=0)
            c.reduce(op, elem, elem, [t.max, t.min, t.max, t.min])
        # Views: rank 2 transposed, rank 3 strided, rank 4, reversed and stride 0.
        buf = seq(24)
        for op in ("sum", "fold_add", "min", "max", "sum_sat"):
            init = 0 if op == "fold_add" else None
            c.reduce(op, elem, elem, buf, init=init, shape=(4, 6), stride=(6, 1))
            c.reduce(op, elem, elem, buf, init=init, shape=(6, 4), stride=(1, 6))
            c.reduce(op, elem, elem, buf, init=init, shape=(2, 3, 2), stride=(12, 2, 7))
            c.reduce(op, elem, elem, buf, init=init, shape=(2, 2, 2, 3), stride=(12, 6, 3, 1))
            c.reduce(op, elem, elem, buf, init=init, origin=23, shape=(24,), stride=(-1,))
            c.reduce(op, elem, elem, buf, init=init, origin=5, shape=(3, 4), stride=(0, 2))
            c.reduce(op, elem, elem, buf, init=init, shape=(3, 0), stride=(1, 1))
        # Fold overflow at a later index of a transposed view: j counts in logical order.
        c.reduce("fold_add", elem, elem, [t.max, 0, 1, 0], init=0, shape=(2, 2), stride=(1, 2))
        # dot into each result, with a shape fault before any fuel.
        for result in [elem] + [r for r, _ in WIDE if r != elem] + ["I64", "U64"]:
            for k in lengths[: 6 if result == elem else 4]:
                c.dot(elem, result, seq(k), seq(k))
        for k in (1, 2, 3):
            c.dot(elem, elem, [t.max] * k, [t.max] * k)
            c.dot(elem, elem, [t.max] * k, [t.min if t.signed else 1] * k)
            c.dot(elem, elem, [1 << (t.width // 2)] * k, [1 << (t.width // 2 - 2)] * k)
        c.dot(elem, elem, seq(3), seq(4))
        c.dot(elem, "I64", seq(0), seq(2))
        c.dot(elem, elem, seq(4), seq(4), n=2, m=2, sa=2, sb=1)
        c.dot(elem, elem, [t.min if t.signed else t.max], [t.min if t.signed else t.max], n=512, m=512, sa=0, sb=0)
        c.dot(elem, elem, [t.max], [t.min if t.signed else t.max], n=513, m=513, sa=0, sb=0)
        if elem == "I1024":
            # Exact sums of version 3: 2^2055 (a Z of 258 bytes), 2^2062, and a negative one.
            c.dot(elem, elem, [t.min], [t.min], n=1 << 16, m=1 << 16, sa=0, sb=0)
            c.dot(elem, "I64", [t.min], [t.max], n=4099, m=4099, sa=0, sb=0)
            c.dot(elem, "U512", [t.min, t.max], [t.min, t.max])
            c.dot(elem, elem, [t.min] * 3, [t.min] * 2)
    # Narrow elements into wide results: sum and dot.
    for elem, _ in NARROW:
        t = int_type(elem)
        b = boundaries(elem)
        for result, _ in WIDE:
            vals = [rng.choice(b) if rng.random() < 0.4 else rng.randint(t.min, t.max) for _ in range(70)]
            c.reduce("sum", elem, result, vals)
            c.reduce("sum", elem, result, [t.max] * 3)
            c.reduce("sum", elem, result, [t.min] * 3)
            c.reduce("sum", elem, result, [t.max, t.min] * 5, origin=9, shape=(10,), stride=(-1,))
            c.dot(elem, result, vals[:33], vals[33:66])
            c.dot(elem, result, [t.min if t.signed else t.max] * 3, [t.min if t.signed else t.max] * 3)
        c.reduce("sum", elem, "U128", [t.min], shape=(5000,), stride=(0,))
        c.dot(elem, "I128", [t.min], [t.min], n=100000, m=100000, sa=0, sb=0)
        c.dot(elem, "I128", [1], [2], n=2, m=3, sa=0, sb=0)
    # Arguments no emitted code passes.
    c.refused("count", "I128", "I64", [1])
    c.refused("sum", "I64", "I64", [1])                                           # no wide side: cint_rt_reduce's
    c.refused("min", "I128", "I256", [1])
    c.refused("sum_wrap", "I64", "I128", [1])
    c.refused("fold_add", "I256", "I256", [1])                                    # fold_checked without init
    c.refused("dot", "I128", "I128", [1], rank=2, ys=True)
    c.refused("dot", "I128", "I128", [1])                                        # dot without ys
    c.lines.append("%d 0 %d %d - X 0 1 1 1 1 %s = %s fuel 0" % (len(c.lines), TAG["I128"], TAG["I128"],
                                                                 hexle("I128", 1), outcome(REFUSED)))
    c.lines.append("%d 10 %d %d - X 0 1 1 1 1 %s = %s fuel 0" % (len(c.lines), TAG["I128"], TAG["I128"],
                                                                  hexle("I128", 1), outcome(REFUSED)))
    return c.lines


def legs():
    if os.name == "nt":
        return ["msvc"]
    if sys.platform == "darwin":
        return ["apple-clang"]
    return [leg for leg in ("gcc", "clang") if shutil.which(leg)]


class ReduceWide(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lines = generate()

    def test_cases_reach_every_outcome(self):
        ops = {int(line.split(" ", 2)[1]) for line in self.lines}
        self.assertEqual(ops, set(range(0, 11)))
        text = "\n".join(self.lines)
        for marker in ("= V ", "= F ", "fuel 0", "fuel 1024"):
            self.assertIn(marker, text)
        v3 = b"cint-core-1/fault/v3".hex()
        for line in self.lines:
            fields = line.split()
            # Version 3 exactly for the records of dot over I1024 (BX15-07 C).
            self.assertEqual(v3 in line, fields[1] == "9" and int(fields[2]) == TAG["I1024"] and " = F " in line,
                             line[:80])
        self.assertGreater(sum(1 for line in self.lines if v3 in line), 5)

    def test_every_build_agrees(self):
        found = legs()
        if not found:
            self.skipTest("no gcc or clang on this host")
        root = cc.build_root() / "rt-reduce-wide"
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
                                                                          "tests/test_reduce_wide.c")], out, name)
                            exe = tc.link_exe(objs, out / "test_reduce_wide", name)
                            run = subprocess.run([str(exe), str(cases)], capture_output=True, timeout=600)
                            self.assertEqual(run.returncode, 0, run.stderr[-3000:])
                            self.assertEqual(run.stderr, b"")
                            self.assertIn(b"helper mode: " + helpers.encode("ascii"), run.stdout)
                            self.assertIn(b"reduce cases: %d, passed %d, failed 0" % (len(self.lines),
                                                                                      len(self.lines)), run.stdout)
                            self.assertIn(b"direct checks: failed 0", run.stdout)


if __name__ == "__main__":
    unittest.main()
