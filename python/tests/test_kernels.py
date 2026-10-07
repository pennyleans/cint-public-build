"""Kernels and arrays of rank 2 to 4 called from Python (SPEC-02 K-15; SPEC-03
section 9; box 10 note, section 3.1 items 2 and 3).

The SPEC-03 section 9 cases that bind a kernel's `out` (LS-119) or need an array of
rank 2 are here: 12a, 13, 17a, 19, 20, 20a and 21. A dispatch issued from Python
has the outcome of the same dispatch from `.ci` (K-15), with two differences of
position: an entry fault is at the kernel's name with an empty stack (F-8), and a
work-item fault's stack holds the kernel's name where a `.ci` dispatch holds its
call (rt/OPEN.md RT-OQ-40). A dispatch charges no call unit (F-8), so it consumes
one unit of fuel less than a `.ci` entry that makes the same dispatch. Faults are
compared field by field with the frozen `.expect` of the heat cases of box 09 and
with the reference interpreter's record (BX10-21).
"""
import array
import ctypes
import os
import unittest

import cint
from cint import _dlpack
from tests.support import Producer, numpy, reference_path
from tests.test_calls import CONFORMANCE, PROGRAMS, load, setUpModule  # noqa: F401 (setUpModule runs here too)


def kernels() -> cint.Module:
    return load("kernels.ci")


def i64s(*values) -> array.array:
    return array.array("q", values)


def shaped(data: array.array, shape: tuple) -> memoryview:
    """`data` as a C-order memoryview of `shape`."""
    return memoryview(data).cast("B").cast(data.typecode, shape)


def nested(values: list, shape: tuple) -> list:
    """Flat `values` as nested lists of `shape`, as Buffer.tolist() gives them."""
    if len(shape) == 1:
        return list(values)
    step = len(values) // shape[0]
    return [nested(values[i * step:(i + 1) * step], shape[1:]) for i in range(shape[0])]


class Table(unittest.TestCase):
    def test_kernels_are_listed(self):
        m = kernels()
        kinds = {name: e.kind for name, e in m.exports.items()}
        self.assertEqual(kinds, {"scale": "kernel", "square": "kernel", "ramp": "kernel", "shift": "kernel",
                                 "twice": "function", "trace": "function", "paint": "function",
                                 "corner": "function", "last4": "function"})
        self.assertTrue(all(e.callable for e in m.exports.values()))
        shift = m.exports["shift"]
        self.assertEqual((shift.effect, shift.size_names, shift.param_names), ("pure", ("h", "w"), ("x", "y")))
        self.assertEqual(shift.declaration, "kernel shift[h, w](in I64[h, w + 1] x, inout I64[h, w] y)")


class Dispatch(unittest.TestCase):
    def setUp(self):
        self.ctx = kernels().context()

    def test_outputs(self):
        """A borrowed output with publish="copy" and a Buffer: staged and copied on
        success (M-30a, M-31), and listed in the copy report (BX10-17)."""
        c = self.ctx
        out = i64s(0, 0, 0)
        self.assertIsNone(c.scale(i64s(1, 2, 3), 5, y=cint.borrow(out, writable=True, publish="copy")))
        self.assertEqual(list(out), [5, 10, 15])
        self.assertEqual(c.fuel_consumed(), 3)                 # N = 3, and no call unit (F-8)
        self.assertEqual(c.last_entry().copies, (cint.CopyRecord("y", "publish-copy", "I64", 3),))
        b = cint.empty((3,), "I64")
        self.assertEqual(c.square(i64s(1, 2, 3), y=b), 14)     # a scalar out returns (BX10-14)
        self.assertEqual(b.tolist(), [1, 4, 9])
        self.assertTrue(b.published)
        self.assertEqual(c.last_entry().copies, (cint.CopyRecord("y", "staging", "I64", 3),))
        self.assertEqual(str(c.last_entry()), "square: 1 copy\n  staging y: I64 x 3")

    def test_rank_two(self):
        c = self.ctx
        y = cint.empty((2, 3), "I64")
        c.ramp(10, y=y)
        self.assertEqual(y.tolist(), [[10, 11, 12], [13, 14, 15]])
        self.assertEqual(c.fuel_consumed(), 6)
        x = shaped(i64s(*range(8)), (2, 4))
        y = cint.copy(shaped(i64s(*[100] * 6), (2, 3)))
        c.shift(x, y=y)                                         # w from y, then x is checked against w + 1
        self.assertEqual(y.tolist(), [[101, 102, 103], [105, 106, 107]])
        with self.assertRaises(cint.ShapeFault) as caught:
            kernels().context().shift(shaped(i64s(*range(6)), (2, 3)), y=y)
        f = caught.exception
        self.assertEqual((f.operation, f.operands, f.limit), ("bind.shape", (0, 1, 3), 4))
        self.assertEqual((f.phase, f.dispatch, f.kernel, f.stack), ("entry", 0, "kernels.shift", ()))
        self.assertEqual(f.position, "kernels.ci:22:15")       # the kernel's name (F-8)
        self.assertEqual(y.tolist(), [[101, 102, 103], [105, 106, 107]])

    def test_case_12a_dlpack(self):
        """Case 12a without NumPy: a DLPack output of shape (1, 4) with stride 0 on its
        extent-1 axis is injective, so it is admitted."""
        p = Producer([0, 0, 0, 0], shape=(1, 4), strides=(0, 1))
        self.ctx.ramp(3, y=cint.borrow(p, writable=True, publish="copy"))
        self.assertEqual(list(p.storage), [3, 4, 5, 6])

    @unittest.skipIf(numpy is None, "NumPy is not installed")
    def test_case_12a(self):
        """Case 12a: `np.zeros(4)[None, :]`, as `int64`, is a write view with stride 0 on
        an axis of extent 1, which is admitted."""
        a = numpy.zeros(4, dtype=numpy.int64)[None, :]
        self.assertEqual(a.strides[0], 0)
        self.ctx.ramp(7, y=cint.borrow(a, writable=True, publish="copy"))
        self.assertEqual(a.tolist(), [[7, 8, 9, 10]])

    def entry_fault(self, call, cls, operation) -> cint.Fault:
        """An entry fault of a host-issued dispatch: at the kernel's name with an
        empty stack, dispatch number 0 consumed (F-1, F-8), no fuel, and a context
        that clear_fault alone makes usable again."""
        ctx = kernels().context()
        with self.assertRaises(cls) as caught:
            call(ctx)
        f = caught.exception
        self.assertEqual((f.operation, f.phase, f.dispatch, f.work_item, f.stack), (operation, "entry", 0, None, ()))
        self.assertEqual(ctx.fuel_consumed(), 0)
        with self.assertRaises(cint.Faulted) as held:
            ctx.ramp(0, y=cint.empty((1, 1), "I64"))
        self.assertTrue(held.exception.nothing_ran)
        ctx.clear_fault()
        self.assertIsNone(ctx.ramp(0, y=cint.empty((1, 1), "I64")))
        return f

    def test_case_13(self):
        """Case 13: one memory as `in` and as an `out` with publish="copy" is two
        registrations that overlap: E_ALIAS by test T1, dispatch number 0 consumed."""
        a = i64s(1, 2, 3)
        f = self.entry_fault(lambda c: c.scale(cint.borrow(a), 2, y=cint.borrow(a, writable=True, publish="copy")),
                             cint.AliasFault, "bind.alias")
        self.assertEqual((f.code, f.kernel, f.position), ("E_ALIAS", "kernels.scale", "kernels.ci:5:15"))
        self.assertEqual(list(a), [1, 2, 3])

    def test_case_17a(self):
        """Case 17a: a borrowed View without publish="copy" as an `out` faults
        E_UNSUPPORTED at entry (M-30a, check 8), naming the parameter."""
        out = i64s(0, 0)
        f = self.entry_fault(lambda c: c.scale(i64s(1, 2), 2, y=cint.borrow(out, writable=True)),
                             cint.UnsupportedFault, "bind.limit")
        self.assertEqual((f.code, f.operands), ("E_UNSUPPORTED", (2,)))
        self.assertIn('publish="copy"', str(f))               # P-15: the message names both remedies
        self.assertIn("cint.empty", str(f))
        self.assertEqual(list(out), [0, 0])

    def test_check_order(self):
        """F-5: a shape fault (check 5) comes before the overlap (check 7) and the
        limit (check 8) of the same dispatch."""
        a = i64s(1, 2, 3)
        f = self.entry_fault(lambda c: c.scale(cint.borrow(a), 2, y=cint.borrow(memoryview(a)[:2], writable=True)),
                             cint.ShapeFault, "bind.shape")
        self.assertEqual((f.operands, f.limit), ((2, 0, 2), 3))

    def test_case_19(self):
        """Case 19: a Buffer with a live export bound as an `out` is Busy, E_ALIAS
        (P-18), before the dispatch, which consumes nothing."""
        b = cint.empty((2,), "I64")
        c = self.ctx
        c.scale(i64s(1, 2), 3, y=b)
        mv = b.memoryview()
        with self.assertRaises(cint.Busy) as caught:
            c.scale(i64s(1, 2), 5, y=b)
        self.assertEqual(caught.exception.code, "E_ALIAS")
        self.assertEqual(c.fuel_consumed(), 2)
        del mv
        c.scale(i64s(1, 2), 5, y=b)
        self.assertEqual(b.tolist(), [5, 10])

    def test_case_20(self):
        """Case 20: a dispatch that faults leaves its Buffer output as last published
        (M-30)."""
        b = cint.empty((3,), "I64")
        self.ctx.scale(i64s(1, 2, 3), 2, y=b)
        ctx = kernels().context()
        with self.assertRaises(cint.OverflowFault) as caught:
            ctx.scale(i64s(1, 1 << 62, 3), 2, y=b)
        f = caught.exception
        self.assertEqual((f.phase, f.dispatch, f.work_item), ("work-item", 0, 1))
        self.assertEqual(f.stack, (("kernels.ci", 5, 15),))
        self.assertEqual(b.tolist(), [2, 4, 6])
        self.assertEqual(ctx.last_entry().copies, ())
        out = i64s(7, 7, 7)
        with self.assertRaises(cint.OverflowFault):
            kernels().context().scale(i64s(1, 1 << 62, 3), 2, y=cint.borrow(out, writable=True, publish="copy"))
        self.assertEqual(out.tolist(), [7, 7, 7])

    def test_case_20a(self):
        """Case 20a: in one entry, step(a, b) then step(b, a), whose second faults: b
        keeps the first dispatch's result and a is unchanged (M-30), for Buffers and
        borrowed Views alike (M-30b)."""
        for make in (lambda v: cint.copy(i64s(*v)), lambda v: cint.borrow(i64s(*v), writable=True)):
            a, b = make([1, 2, 3]), make([0, 0, 0])
            ctx = kernels().context()
            with self.assertRaises(cint.OverflowFault) as caught:
                ctx.twice(a=a, b=b)
            f = caught.exception
            self.assertEqual((f.phase, f.dispatch, f.work_item, f.kernel), ("work-item", 1, 0, "kernels.scale"))
            self.assertEqual(f.stack, (("kernels.ci", 30, 5),))
            self.assertEqual(b.tolist(), [2, 4, 6])
            self.assertEqual(a.tolist(), [1, 2, 3])
            self.assertEqual(ctx.last_entry().copies, ())

    def test_case_21(self):
        """Case 21: work-items 900 and 7 fault; the least, 7, is reported (F-3), with
        the record the reference gives the same dispatch from `.ci`, but for the stack
        and the call unit."""
        x = i64s(*[0] * 1000)
        x[7] = x[900] = 1 << 32
        ctx = kernels().context()
        with self.assertRaises(cint.OverflowFault) as caught:
            ctx.square(x, y=cint.empty((1000,), "I64"))
        got = caught.exception
        self.assertEqual((got.work_item, got.step), (7, 2))
        reference_path()
        from cint_ref.exec import DEFAULT_DEPTH, run_program
        from cint_ref.faults import RUN_TIME, encode_fault_record
        with open(os.path.join(PROGRAMS, "kernels.ci"), "rb") as f:
            source = f.read()
        call = len(source.split(b"\n")) + 7
        source += (b"\nexport I64 run21() {\n    I64[1000] x;\n    I64[1000] y;\n    x[7] = 4294967296;\n"
                   b"    x[900] = 4294967296;\n    I64 t = 0;\n    square(x, y, t);\n    return t;\n}\n")
        o = run_program(source, "kernels.ci", "run21", None, DEFAULT_DEPTH)
        self.assertEqual(o.kind, "fault")
        want = cint.Fault.from_record(encode_fault_record(o.record, RUN_TIME, 2)).expect_lines(4)
        self.assertIn("fault.stack kernels.ci:%d:5" % call, want)
        want[want.index("fault.stack kernels.ci:%d:5" % call)] = "fault.stack kernels.ci:10:15"
        self.assertEqual(got.expect_lines(4, revision=kernels().revision), want)
        self.assertEqual(ctx.fuel_consumed(), o.fuel - 1)


class Heat(unittest.TestCase):
    """The heat cases of box 09 (rulings R7 and R9) from Python: their `run` entry
    gives every line of the frozen `.expect`, and `diffuse` dispatched from Python
    gives the same fault with the kernel's name as its stack and one unit of fuel
    less (K-15)."""

    def expect(self, stem: str) -> tuple:
        with open(os.path.join(CONFORMANCE, stem + ".expect"), encoding="ascii") as f:
            lines = f.read().split("\n")
        faults = [line for line in lines if line.startswith("fault.")]
        fuel = int(next(line for line in lines if line.startswith("fuel-consumed "))[len("fuel-consumed "):])
        return faults, fuel

    def case(self, stem: str, center: int, kn: int, kd: int):
        mod = load(stem + ".ci", root=CONFORMANCE)
        want, fuel = self.expect(stem)
        ctx = mod.context()
        with self.assertRaises(cint.Fault) as caught:
            ctx.run()
        self.assertEqual(caught.exception.expect_lines(4, revision=mod.revision), want)
        self.assertEqual(ctx.fuel_consumed(), fuel)
        u = array.array("i", [0] * 64)
        u[3 * 8 + 4] = center
        v = cint.empty((8, 8), "I32")
        ctx = mod.context()
        with self.assertRaises(cint.Fault) as caught:
            ctx.diffuse(shaped(u, (8, 8)), kn, kd, v=v)
        stack = [i for i, line in enumerate(want) if line.startswith("fault.stack ")]
        self.assertEqual(len(stack), 1)
        want[stack[0]] = "fault.stack %s.ci:7:15" % stem
        self.assertEqual(caught.exception.expect_lines(4, revision=mod.revision), want)
        self.assertEqual(ctx.fuel_consumed(), fuel - 1)
        self.assertFalse(v.published)

    def test_e1_overflow(self):
        self.case("kernel/heat_e1_overflow", 1000, 2305843009213693952, 9223372036854775807)

    def test_e2_narrowing(self):
        self.case("kernel/heat_e2_narrowing", 2147483647, 1, 1)

    def test_e9_div_zero(self):
        self.case("kernel/heat_e9_div_zero", 1, 1, 0)

    def test_hot_spot(self):
        """heat_hot_spot: total 65536, center 32768, fuel 64 for N = 64."""
        mod = load("kernel/heat_hot_spot.ci", root=CONFORMANCE)
        ctx = mod.context()
        self.assertEqual(ctx.run(), 65536032768)
        u = array.array("i", [0] * 64)
        u[3 * 8 + 4] = 65536
        v = cint.empty((8, 8), "I32")
        ctx = mod.context()
        self.assertEqual(ctx.diffuse(shaped(u, (8, 8)), 1, 8, v=v), 65536)
        self.assertEqual(v.tolist()[3][4], 32768)
        self.assertEqual(ctx.fuel_consumed(), 64)

    def test_zero_work_items(self):
        mod = load("kernel/zero_work_items.ci", root=CONFORMANCE)
        ctx = mod.context()
        u = Producer([], ctype=ctypes.c_int32, dtype=(_dlpack.kDLInt, 32, 1), shape=(0, 8), strides=(8, 1))
        self.assertEqual(ctx.diffuse(u, 1, 8, v=cint.empty((0, 8), "I32")), 0)
        self.assertEqual(ctx.fuel_consumed(), 0)


class Views(unittest.TestCase):
    """Functions with arrays of rank 2 to 4: each extent is checked against its
    declaration, and the body indexes the view by its strides (RT-OQ-38)."""

    def setUp(self):
        self.ctx = kernels().context()

    def test_values(self):
        c = self.ctx
        self.assertEqual(c.trace(shaped(i64s(*range(9)), (3, 3))), 12)
        m = array.array("i", [0] * 6)
        self.assertIsNone(c.paint(array.array("i", [7, 8, 9]), m=cint.borrow(shaped(m, (2, 3)), writable=True)))
        self.assertEqual(list(m), [7, 8, 9, 7, 8, 9])
        self.assertEqual(c.last_entry().copies, ())
        self.assertEqual(c.corner(shaped(i64s(*range(24)), (2, 3, 4))), 20)
        self.assertEqual(c.last4(shaped(array.array("b", range(16)), (2, 2, 2, 2))), 15)
        self.assertEqual(c.corner(cint.copy(shaped(i64s(*range(6)), (2, 3, 1)))), 5)

    def test_shape_faults(self):
        for call, operands, limit in ((lambda c: c.trace(shaped(i64s(*range(6)), (2, 3))), (0, 1, 3), 2),
                                      (lambda c: c.corner(shaped(i64s(*range(8)), (2, 4, 1))), (0, 1, 4), 3),
                                      (lambda c: c.last4(shaped(array.array("b", range(8)), (1, 1, 1, 8))),
                                       (0, 3, 8), 2),
                                      (lambda c: c.paint(array.array("i", [1, 2]), m=cint.copy(shaped(array.array("i", [0] * 6), (2, 3)))),
                                       (1, 0, 2), 3)):
            ctx = kernels().context()
            with self.subTest(operands=operands):
                with self.assertRaises(cint.ShapeFault) as caught:
                    call(ctx)
                f = caught.exception
                self.assertEqual((f.operation, f.operands, f.limit, f.kernel), ("bind.shape", operands, limit, None))
                self.assertTrue(f.entry_phase())

    @unittest.skipIf(numpy is None, "NumPy is not installed")
    def test_numpy_layouts(self):
        """A transposed, a reversed and a sliced NumPy array, as they are."""
        c = self.ctx
        a = numpy.arange(9, dtype=numpy.int64).reshape(3, 3)
        for view in (a, a.T, a[::-1, ::-1], numpy.arange(36, dtype=numpy.int64).reshape(6, 6)[::2, 1::2]):
            self.assertEqual(c.trace(view), int(numpy.trace(view)))
        m = numpy.zeros((3, 4), dtype=numpy.int32)
        c.paint(numpy.array([1, 2, 3], dtype=numpy.int32), m=cint.borrow(m.T[::-1], writable=True))
        self.assertEqual(m.T[::-1].tolist(), [[1, 2, 3]] * 4)
        t = numpy.arange(48, dtype=numpy.int64).reshape(4, 3, 4)[::2, :, ::-1]
        self.assertEqual(c.corner(t), int(t[1, 2, 0]))


if __name__ == "__main__":
    unittest.main()
