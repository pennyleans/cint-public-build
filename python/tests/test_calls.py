"""Calls into libraries that `cint build --lib` writes (SPEC-03 6.1, section 9; BX10-02, BX10-14, BX10-21).

Each program under `programs/` is built once per toolchain and cached (the
second run of this file builds nothing). The tests skip, saying why, when no
`cint` with its toolchain file is found, when the C compiler is missing, or
when the bootstrap found under `CINT_BUILD` was built from other compiler or
runtime sources than this tree's. `CINT_EXE` names a `cint` to use as it is,
for example one bootstrapped from another branch.

The SPEC-03 section 9 cases that a function export can reach are here (9,
11, 12, 13a, 14, 15, 15a, 19, 23, 23a, 24, 35); a fault is compared with the
reference interpreter's record, and three frozen conformance cases are
compared field by field with their `.expect` files (BX10-21).
"""
import array
import ctypes
import hashlib
import os
import shutil
import sys
import tempfile
import threading
import time
import unittest

import cint
from cint import _build
from tests.support import ROOT, numpy, reference_path

PROGRAMS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "programs")
CONFORMANCE = str(ROOT / "conformance")
_modules = {}
_problem = None


def _tree_problem(exe: str) -> str | None:
    """Why a bootstrap found on its own does not belong to this tree, or None.
    One named by CINT_EXE is used as it is."""
    if os.environ.get("CINT_EXE"):
        return None
    rows = {}
    with open(os.path.join(os.path.dirname(exe), "cint.toolchain"), encoding="utf-8") as f:
        for line in f.read().splitlines()[1:]:
            key, _, value = line.partition(" ")
            rows.setdefault(key, []).append(value)
    tools = str(ROOT / "tools")
    if tools not in sys.path:
        sys.path.append(tools)
    reference_path()
    import cint_bootstrap
    sources = {"compiler/" + p.name: p.read_bytes() for p in sorted((ROOT / "compiler").glob("*.ci"))}
    want = hashlib.sha256(cint_bootstrap.source_manifest(cint_bootstrap.compiler_closure(sources))).hexdigest()
    with open(rows["compiler_sources"][0], "rb") as f:
        have = hashlib.sha256(f.read()).hexdigest()
    if have != want:
        return ("the cint at %s was bootstrapped from other compiler sources than this tree's; run "
                "tools/cint_bootstrap.py, or set CINT_EXE to use it as it is" % exe)
    for path in rows.get("runtime_source", ()):
        mine = ROOT / "rt" / os.path.basename(path)
        with open(path, "rb") as f:
            if not mine.is_file() or mine.read_bytes() != f.read():
                return ("the cint at %s carries another runtime than this tree's rt/%s; run "
                        "tools/cint_bootstrap.py, or set CINT_EXE" % (exe, os.path.basename(path)))
    return None


def setUpModule():
    global _problem
    try:
        exe = _build.find_cint()
    except cint.BuildError as missing:
        _problem = str(missing)
    else:
        _problem = _tree_problem(exe)
    if _problem is not None:
        raise unittest.SkipTest(_problem)


def load(rel: str, root: str | None = None) -> cint.Module:
    """The module of a program, built once per run (and cached across runs)."""
    key = (rel, root)
    if key not in _modules:
        path = os.path.join(root or PROGRAMS, rel)
        try:
            if root is None:
                _modules[key] = cint.load(path)
            else:
                _modules[key] = cint.load(_build.build_library(path, root=root))
        except cint.BuildError as failure:
            if failure.status == 5:          # the CLI's environment status: no C compiler, for one
                raise unittest.SkipTest("cint build --lib cannot run here: %s" % failure)
            raise
    return _modules[key]


def calls() -> cint.Module:
    return load("calls.ci")


def errors() -> cint.Module:
    return load("errors.ci")


def i64s(*values) -> array.array:
    return array.array("q", values)


class ErrorUnions(unittest.TestCase):
    """A-13, A-18 and BX10-06: an `E!T` export writes a struct of the tag and then the value,
    and a tag other than 0 raises the `ErrorResult` of its set; the class of a combined set
    is a base of each member set's (OQ-201)."""

    def setUp(self):
        self.mod = errors()
        self.ctx = self.mod.context()

    def raises(self, call, cls, name, tag):
        with self.assertRaises(cint.ErrorResult) as caught:
            call()
        e = caught.exception
        self.assertEqual((type(e), e.name, e.tag), (cls, name, tag))
        return e

    def test_values(self):
        c = self.ctx
        self.assertEqual(c.digit(ord("7")), 7)
        self.assertEqual(c.read_digit(True, ord("0")), 0)
        self.assertIsNone(c.stop(True))
        self.assertIs(c.above(60), True)
        self.assertIs(c.above(10), False)
        self.assertEqual(c.narrow(65535), 65535)
        self.assertEqual(c.twice(-4), -8)

    def test_errors(self):
        m, c = self.mod, self.ctx
        self.raises(lambda: c.digit(32), m.ParseError, "empty", 1)
        self.raises(lambda: c.digit(ord("a")), m.ParseError, "bad_digit", 2)
        e = self.raises(lambda: c.read_digit(False, ord("0")), m.IoError, "closed", 1)
        self.assertIsInstance(e, m.IoOrParse)
        e = self.raises(lambda: c.read_digit(True, ord("a")), m.ParseError, "bad_digit", 2)
        self.assertIsInstance(e, m.IoOrParse)
        self.raises(lambda: c.stop(False), m.IoError, "full", 2)
        self.raises(lambda: c.above(-1), m.Level, "low", 1)
        self.raises(lambda: c.above(101), m.Level, "high", 2)
        self.raises(lambda: c.narrow(65536), m.Wide, "overflowed", 1)
        self.raises(lambda: c.twice(1 << 62), m.ArithError, "overflow", 1)

    def test_context_stays_ready(self):
        c = self.ctx
        with self.assertRaises(cint.ErrorResult):
            c.digit(32)
        self.assertEqual(c.digit(ord("9")), 9)       # an error result is not a fault (A-18)

    def test_table(self):
        m = self.mod
        self.assertEqual(sorted(m.error_sets), ["ArithError", "errors.IoError", "errors.IoOrParse",
                                                "errors.Level", "errors.ParseError", "errors.Wide"])
        self.assertTrue(all(m.exports[n].callable for n in ("digit", "read_digit", "stop", "above",
                                                             "narrow", "twice")))


class Scalars(unittest.TestCase):
    """A-13: every integer width and Bool by value, results through the pointer."""

    def setUp(self):
        self.ctx = calls().context()

    def test_values(self):
        c = self.ctx
        self.assertEqual(c.add(2, 3), 5)
        self.assertEqual(c.add(-(1 << 62), -(1 << 62)), -(1 << 63))
        self.assertEqual(c.quotient(-7, 2), -4)                     # floor division (SPEC-01)
        self.assertIs(c.flip(True), False)
        self.assertIs(c.flip(False), True)
        self.assertEqual(c.low(255), 255)
        self.assertEqual(c.low(0), 0)
        self.assertEqual(c.neg8(127), -127)
        self.assertEqual(c.twice16(32767), 65534)
        self.assertEqual(c.next32(2 ** 32 - 2), 2 ** 32 - 1)
        self.assertEqual(c.prev16(-32767), -32768)
        self.assertEqual(c.diff32(-(2 ** 31) + 5, 5), -(2 ** 31))
        self.assertEqual(c.entry("add")(1, b=2), 3)

    def test_body_faults(self):
        cases = [
            (lambda c: c.add(1 << 62, 1 << 62), cint.OverflowFault, "add.checked.i64"),
            (lambda c: c.neg8(-128), cint.OverflowFault, None),
            (lambda c: c.twice16(40000), cint.OverflowFault, None),
            (lambda c: c.next32(2 ** 32 - 1), cint.OverflowFault, None),
            (lambda c: c.prev16(-32768), cint.OverflowFault, None),
            (lambda c: c.low(256), cint.NarrowFault, "as.checked.u64.u8"),
            (lambda c: c.quotient(1, 0), cint.DivZeroFault, "div.checked.i64"),
        ]
        for call, cls, operation in cases:
            ctx = calls().context()
            with self.subTest(cls=cls.__name__, operation=operation):
                with self.assertRaises(cls) as caught:
                    call(ctx)
                f = caught.exception
                self.assertFalse(f.entry_phase())
                self.assertEqual(f.file, "calls.ci")
                if operation is not None:
                    self.assertEqual(f.operation, operation)
                self.assertEqual(f.revision, calls().revision)
                self.assertEqual(Faulted_held(ctx).record, f.record)

    def test_refusals(self):
        """Cases 23, 23a, 24: refused before entry, with no fuel and no fault."""
        c = self.ctx
        c.add(1, 1)
        before = c.fuel_consumed()
        for args in ((True, 1), (1.0, 1), ("1", 1), (2 ** 63, 0), (None, 1)):
            with self.subTest(args=args):
                with self.assertRaises(cint.Refused):
                    c.add(*args)
        with self.assertRaises(cint.Refused) as caught:
            c.add(2 ** 63, 0)
        self.assertEqual(caught.exception.code, "E_NARROW")
        with self.assertRaises(cint.Refused) as caught:
            c.add(1.5, 0)
        self.assertEqual(caught.exception.code, "E_UNSUPPORTED")
        with self.assertRaises(cint.Refused):
            c.flip(1)
        with self.assertRaises(cint.Refused):
            c.low(-1)
        with self.assertRaises(TypeError):
            c.add(1)
        with self.assertRaises(TypeError):
            c.add(1, 2, 3)
        with self.assertRaises(TypeError):
            c.add(1, 2, c=3)
        self.assertEqual(c.fuel_consumed(), before)
        self.assertEqual(c.add(1, 1), 2)

    @unittest.skipIf(numpy is None, "NumPy is not installed")
    def test_numpy_scalars(self):
        """Cases 23 and 23a: numpy.int32 is refused for I64, numpy.longlong accepted."""
        with self.assertRaises(cint.Refused):
            self.ctx.add(numpy.int32(5), 1)
        with self.assertRaises(cint.Refused):
            self.ctx.add(numpy.bool_(True), 1)
        self.assertEqual(self.ctx.add(numpy.longlong(5), 1), 6)
        self.assertEqual(self.ctx.add(numpy.int64(5), 1), 6)

    def test_module_state_per_context(self):
        a, b = calls().context(), calls().context()
        a.deposit(5)
        a.deposit(6)
        b.deposit(1)
        self.assertEqual((a.balance(), b.balance()), (11, 1))
        self.assertIsNone(a.say(3))          # prints, which a context without output discards

    def test_struct_exports_are_listed(self):
        """BX10-27: a struct parameter or result is listed but not callable."""
        mod = calls()
        self.assertEqual(mod.exports["make"].declaration, "calls.Pair make(I64 a, Bool b)")
        self.assertEqual(mod.exports["first"].declaration, "I64 first(calls.Pair p)")
        with self.assertRaisesRegex(NotImplementedError, r"result is struct calls\.Pair.*BX10-27"):
            self.ctx.make(1, True)
        with self.assertRaisesRegex(NotImplementedError, r"parameter p is struct calls\.Pair.*BX10-27"):
            self.ctx.first(None)

    def test_table(self):
        mod = calls()
        self.assertIn(mod.abi >> 16, (2, 3))
        self.assertEqual(len(mod.revision), 32)
        self.assertEqual(mod.exports["scale"].declaration, "void scale[n](inout I64[n] a, I64 k)")
        self.assertEqual(mod.exports["four"].declaration, "I64 four(in I32[4] a)")
        self.assertEqual(mod.exports["head"].declaration, "I64 head(in I64[_] a)")
        self.assertTrue(all(e.kind == "function" for e in mod.exports.values()))


def Faulted_held(ctx) -> cint.Fault:
    """The held fault, through the Faulted of the next entry (A-7)."""
    try:
        ctx.add(1, 1)
    except cint.Faulted as f:
        return f.held
    raise AssertionError("the context was not Faulted")


class Views(unittest.TestCase):
    """Arrays as cint_view by value; entry faults of H-12 from the runtime."""

    def setUp(self):
        self.ctx = calls().context()

    def test_in_views(self):
        c = self.ctx
        self.assertEqual(c.total_of(i64s(1, 2, 3)), 6)
        self.assertEqual(c.inner(i64s(1, 2), i64s(3, 4)), 11)
        self.assertEqual(c.bytes_sum(b"abc"), 294)                  # bytes cross to in U8[n] (BX10-15)
        self.assertEqual(c.bytes_sum(bytearray(b"\x01\x02")), 3)
        self.assertEqual(c.four(array.array("i", [1, 2, 3, 4])), 5)
        self.assertEqual(c.head(i64s(9, 8)), 9)
        self.assertEqual(c.trues(cint.copy([True, False, True], elem="Bool")), 2)
        bools = (ctypes.c_bool * 3)(True, True, False)
        self.assertEqual(c.trues(bools), 2)
        self.assertEqual(c.total_of(cint.copy([5, 6], elem="I64")), 11)
        self.assertEqual(c.total_of(memoryview(i64s(5))[:0]), 0)

    def test_overlapping_inputs(self):
        """Case 14: two overlapping read-only inputs are admitted."""
        data = i64s(1, 2, 3)
        v = cint.borrow(data)
        self.assertEqual(self.ctx.inner(v, v), 14)
        self.assertEqual(self.ctx.inner(data, cint.borrow(data)), 14)

    def test_inout(self):
        c = self.ctx
        data = i64s(1, 2, 3)
        v = cint.borrow(data, writable=True)
        self.assertIsNone(c.scale(3, a=v))
        self.assertEqual(list(data), [3, 6, 9])
        y = i64s(1, 1, 1)
        self.assertEqual(c.axpy(cint.borrow(i64s(1, 2, 3)), 2, y=cint.borrow(y, writable=True)), 3)
        self.assertEqual(list(y), [3, 5, 7])
        b = cint.copy([1, 2], elem="I64")
        c.scale(5, a=b)
        self.assertEqual(b.tolist(), [5, 10])
        with self.assertRaisesRegex(cint.Refused, "by name|View or a Buffer"):
            c.scale(2, a=i64s(1))
        with self.assertRaisesRegex(cint.Refused, "cint.empty"):
            c.scale(2)
        with self.assertRaises(TypeError):
            c.scale(v, 2)

    def test_buffer_with_live_export(self):
        """Case 19 for a function's inout: Busy, E_ALIAS (P-18)."""
        b = cint.copy([1, 2], elem="I64")
        mv = b.memoryview()
        with self.assertRaises(cint.Busy) as caught:
            self.ctx.scale(2, a=b)
        self.assertEqual(caught.exception.code, "E_ALIAS")
        del mv
        self.ctx.scale(2, a=b)
        self.assertEqual(b.tolist(), [2, 4])

    def entry_fault(self, call, cls, operation):
        with self.assertRaises(cls) as caught:
            call(self.ctx)
        f = caught.exception
        self.assertEqual(f.operation, operation)
        self.assertTrue(f.entry_phase())
        with self.assertRaises(cint.Faulted) as held:
            self.ctx.add(1, 1)
        self.assertTrue(held.exception.nothing_ran)
        self.assertEqual(held.exception.held.record, f.record)
        self.ctx.clear_fault()
        self.assertEqual(self.ctx.add(1, 1), 2)
        return f

    def test_entry_faults(self):
        """H-12 and BX10-13: element type, shape, alias, and permission are
        faults of the entry checks, after which clear_fault alone suffices."""
        self.entry_fault(lambda c: c.total_of(array.array("i", [1, 2])), cint.UnsupportedFault, "bind.type")
        self.entry_fault(lambda c: c.inner(i64s(1, 2), i64s(1)), cint.ShapeFault, "bind.shape")
        self.entry_fault(lambda c: c.four(array.array("i", [1, 2, 3])), cint.ShapeFault, "bind.shape")
        y = cint.borrow(i64s(1, 2), writable=True)
        self.entry_fault(lambda c: c.axpy(y, 1, y=y), cint.AliasFault, "bind.alias")
        self.entry_fault(lambda c: c.scale(2, a=cint.borrow(i64s(1))), cint.AliasFault, "bind.permission")

    def test_mixed_registrations_alias(self):
        """Case 13a: one memory borrowed as I64 and as U8, bound as inout and in."""
        data = bytearray(16)
        a = cint.borrow(memoryview(data).cast("q"), writable=True)
        b = cint.borrow(data)
        self.entry_fault(lambda c: c.mix(b, a=a), cint.AliasFault, "bind.alias")
        other = bytearray(b"\x07")
        self.ctx.mix(other, a=a)
        self.assertEqual(memoryview(data).cast("q")[0], 7)

    def test_strided_views(self):
        """Case 9: a view with a negative or non-unit stride is passed as it is,
        and the compiled body indexes it by its stride (rt/OPEN.md RT-OQ-38)."""
        data = i64s(1, 2, 3, 4)
        for view, total in ((memoryview(data)[::-1], 10), (memoryview(data)[::2], 4),
                            (memoryview(data)[3::-2], 6)):
            self.assertEqual(self.ctx.total_of(view), total)
        self.assertIsNone(self.ctx.scale(10, a=cint.borrow(memoryview(data)[::-2], writable=True)))
        self.assertEqual(list(data), [1, 20, 3, 40])

    @unittest.skipIf(numpy is None, "NumPy is not installed")
    def test_zero_stride_write(self):
        """Case 12: a write view with stride 0 on an axis of extent 2 faults
        E_ALIAS at entry."""
        base = numpy.zeros(1, dtype=numpy.int64)
        twice = numpy.lib.stride_tricks.as_strided(base, shape=(2,), strides=(0,), writeable=True)
        f = self.entry_fault(lambda c: c.scale(2, a=cint.borrow(twice, writable=True)), cint.AliasFault,
                             "bind.injective")
        self.assertEqual(f.code, "E_ALIAS")

    @unittest.skipIf(numpy is None, "NumPy is not installed")
    def test_numpy_arrays(self):
        a = numpy.arange(5, dtype=numpy.int64)
        self.assertEqual(self.ctx.total_of(a), 10)
        self.ctx.scale(2, a=cint.borrow(a, writable=True))
        self.assertEqual(a.tolist(), [0, 2, 4, 6, 8])

    def test_empty_view(self):
        """Case 11: an empty view is admitted, and any element access is E_BOUNDS."""
        with self.assertRaises(cint.BoundsFault) as caught:
            self.ctx.head(memoryview(i64s(5))[:0])
        self.assertFalse(caught.exception.entry_phase())

    def test_stale_and_unknown_registrations(self):
        """Cases 15 and 15a, by forging what the bridge passes: a view whose
        generation is not its live registration's is an entry fault
        (E_STALE_HANDLE, SPEC-02 F-5 check 1); an unknown identifier, or a
        registration that has ended, is a refusal with no fuel and no fault."""
        ctx = self.ctx
        v = cint.borrow(i64s(1, 2))
        self.assertEqual(ctx.total_of(v), 3)
        consumed = ctx.fuel_consumed()
        r = v._registrations[ctx._key]
        r.generation = 2
        f = self.entry_fault(lambda c: c.total_of(v), cint.StaleHandleFault, "bind.stale")
        self.assertEqual(f.code, "E_STALE_HANDLE")
        consumed = ctx.fuel_consumed()
        r.generation, r.ident, real = 1, 1 << 40, r.ident
        with self.assertRaises(cint.Refused) as caught:
            ctx.total_of(v)
        self.assertEqual(caught.exception.reason, "buffer")
        self.assertEqual(ctx.fuel_consumed(), consumed)
        r.ident = real
        ctx._rt.release(ctx._handle, real)
        with self.assertRaises(cint.Refused) as caught:
            ctx.total_of(v)
        self.assertEqual(caught.exception.reason, "generation")
        self.assertEqual(ctx.fuel_consumed(), consumed)
        self.assertEqual(ctx.add(1, 1), 2)
        r.finalizer.detach()
        v._registrations.clear()
        v.release()


class Faults(unittest.TestCase):
    """A-7, BX10-05: Faulted, clear_fault, and the reference's record."""

    def test_faulted_and_clear(self):
        ctx = calls().context()
        with self.assertRaises(cint.DivZeroFault) as caught:
            ctx.quotient(1, 0)
        with self.assertRaises(cint.Faulted) as held:
            ctx.add(1, 2)
        self.assertFalse(held.exception.nothing_ran)
        self.assertEqual(held.exception.held.record, caught.exception.record)
        self.assertEqual(ctx.fault().record, caught.exception.record)
        ctx.clear_fault()
        self.assertIsNone(ctx.fault())
        self.assertEqual(ctx.add(1, 2), 3)

    def test_same_record_as_the_reference(self):
        """A fault raised in Python has the fields the reference interpreter
        gives the same call (SPEC-01 IM-149, BX10-21)."""
        reference_path()
        from cint_ref.exec import DEFAULT_DEPTH, run_program
        from cint_ref.faults import RUN_TIME, encode_fault_record
        from cint_ref.types import Value
        with open(os.path.join(PROGRAMS, "calls.ci"), "rb") as f:
            source = f.read()
        cases = [("add", (("I64", 1 << 62), ("I64", 1 << 62))), ("quotient", (("I64", 5), ("I64", 0))),
                 ("neg8", (("I8", -128),)), ("low", (("U64", 300),)), ("twice16", (("U16", 40000),)),
                 ("nest", (("I64", 300),))]
        for name, args in cases:
            with self.subTest(name):
                o = run_program(source, "calls.ci", name, None, DEFAULT_DEPTH, args=[Value(t, v) for t, v in args])
                self.assertEqual(o.kind, "fault")
                want = cint.Fault.from_record(encode_fault_record(o.record, RUN_TIME, 2))
                ctx = calls().context()
                with self.assertRaises(cint.Fault) as caught:
                    ctx.entry(name)(*[v for _, v in args])
                got = caught.exception
                self.assertIs(type(got), type(want))
                self.assertEqual(got.expect_lines(1, revision=calls().revision), want.expect_lines(1))
                self.assertEqual(ctx.fuel_consumed(), o.fuel)

    def test_depth(self):
        ctx = calls().context(depth=8)
        self.assertEqual(ctx.nest(6), 6)
        with self.assertRaises(cint.DepthFault) as caught:
            ctx.nest(20)
        self.assertEqual(caught.exception.limit, 8)
        self.assertEqual(calls().context(depth=1000).nest(500), 500)


class Fuel(unittest.TestCase):
    """H-11 and case 35."""

    def test_budgets(self):
        ctx = calls().context(fuel=50)
        self.assertEqual(ctx.spin(5), 5)
        used = ctx.fuel_consumed()
        self.assertGreater(used, 5)
        with self.assertRaises(cint.FuelFault) as caught:
            ctx.spin(1000)
        self.assertEqual(caught.exception.limit, 50)
        self.assertEqual(ctx.fuel_consumed(), 50)
        ctx.clear_fault()
        self.assertEqual(ctx.spin(1000, fuel=None), 1000)          # -1, unbounded
        self.assertEqual(ctx.spin(1000, fuel=-1), 1000)
        with self.assertRaises(cint.FuelFault):
            ctx.spin(5, fuel=0)
        ctx.clear_fault()
        before = ctx.fuel_consumed()
        with self.assertRaises(cint.Refused) as caught:            # -2 is CINT_REFUSED
            ctx.spin(5, fuel=-2)
        self.assertEqual(caught.exception.operands, (-2,))
        self.assertEqual(ctx.fuel_consumed(), before)
        self.assertEqual(ctx.spin(3), 3)
        ctx.fuel = None
        self.assertEqual(ctx.spin(10_000), 10_000)


class Threads(unittest.TestCase):
    """One entry per context (A-11): Busy, and the GIL released during a call (BX10-08)."""

    def test_busy(self):
        mod = calls()
        ctx = mod.context(fuel=None)
        done = []
        t = threading.Thread(target=lambda: done.append(ctx.spin(60_000_000)))
        t.start()
        try:
            deadline = time.monotonic() + 30
            while not ctx._lock.locked() and not done and time.monotonic() < deadline:
                time.sleep(0.001)
            if done:
                self.skipTest("the entry ended before the second call could be made")
            with self.assertRaisesRegex(cint.Busy, "already running"):
                ctx.add(1, 1)
            with self.assertRaises(cint.Busy):
                ctx.clear_fault()
            other = mod.context()
            self.assertEqual(other.add(2, 2), 4)                   # this thread runs while the other is in C
            self.assertTrue(ctx._lock.locked() or done)
        finally:
            t.join(120)
        self.assertEqual(done, [60_000_000])
        self.assertEqual(ctx.add(1, 1), 2)


class Effects(unittest.TestCase):
    """A-19: the effect class each row of cint_library_desc carries."""

    def test_effect_classes(self):
        mod = load(os.path.join("effects", "effects.ci"))
        if mod.abi >> 16 < 3:
            self.skipTest("the ABI %d.%d cintc writes the effect class pure in every row; cint-rt-3 computes them"
                          % (mod.abi >> 16, mod.abi & 0xFFFF))
        self.assertEqual(mod.exports["loud"].effect, "observe")      # its body prints
        self.assertEqual(mod.exports["relay"].effect, "observe")     # it calls an imported function that prints
        self.assertEqual(mod.exports["quiet"].effect, "pure")
        ctx = mod.context()
        self.assertEqual((ctx.relay(3), ctx.quiet(1)), (3, 2))
        self.assertIsNone(ctx.loud(2))


class BuildCache(unittest.TestCase):
    """BX10-02: keyed by the digests of every module the build read and the
    toolchain identity; editing an imported module builds again."""

    def test_cache(self):
        calls()
        before = _build.builds
        again = cint.load(os.path.join(PROGRAMS, "calls.ci"))
        self.assertEqual(_build.builds, before)
        self.assertEqual(again.revision, calls().revision)
        with tempfile.TemporaryDirectory() as d:
            shutil.copytree(os.path.join(PROGRAMS, "imports"), os.path.join(d, "imports"))
            main = os.path.join(d, "imports", "main.ci")
            helper = os.path.join(d, "imports", "lib", "helper.ci")
            self.assertEqual(cint.load(main).context().value(), 11)      # built once, then cached across runs
            first = _build.builds
            self.assertEqual(cint.load(main).context().value(), 11)
            self.assertEqual(_build.builds, first)
            # newline="" keeps the bytes as they are, so the restored file has the digest it had (on
            # Windows, text mode would write CRLF and the last load would build again).
            with open(helper, encoding="utf-8", newline="") as f:
                text = f.read()
            # A value no earlier run used, so that this edit must build.
            fresh = int.from_bytes(os.urandom(6), "little")
            with open(helper, "w", encoding="utf-8", newline="") as f:
                f.write(text.replace("return 10;", "return %d;" % fresh))
            changed = cint.load(main)
            try:
                self.assertEqual(_build.builds, first + 1)
                self.assertEqual(changed.context().value(), fresh + 1)
                self.assertNotEqual(changed.revision, again.revision)
            finally:
                shutil.rmtree(os.path.dirname(changed.path), ignore_errors=True)   # keep the cache from growing
            with open(helper, "w", encoding="utf-8", newline="") as f:
                f.write(text)
            self.assertEqual(cint.load(main).context().value(), 11)
            self.assertEqual(_build.builds, first + 1)

    def test_build_error(self):
        with tempfile.TemporaryDirectory() as d:
            bad = os.path.join(d, "bad.ci")
            with open(bad, "w", encoding="utf-8") as f:
                f.write("export I64 f() {\n    return nope;\n}\n")
            with self.assertRaises(cint.BuildError) as caught:
                cint.load(bad)
            e = caught.exception
            self.assertEqual(e.status, 2)
            self.assertTrue(e.diagnostics)
            self.assertEqual((e.diagnostics[0]["file"], e.diagnostics[0]["line"]), ("bad.ci", 2))
            self.assertIn(e.diagnostics[0]["code"], str(e))


class Conformance(unittest.TestCase):
    """BX10-21: frozen cases built with cint build --lib and called from
    Python, compared field by field with their `.expect` files."""

    def case(self, stem: str):
        reference_path()
        import gen_expect
        from cint_ref.__main__ import parse_arg
        with open(os.path.join(CONFORMANCE, stem + ".ci"), encoding="utf-8") as f:
            h = gen_expect.header(f.read())
        with open(os.path.join(CONFORMANCE, stem + ".expect"), encoding="ascii") as f:
            lines = f.read().split("\n")
        fmt = int(lines[2][len("format "):]) if lines[2] in ("format 2", "format 4") else 1
        mod = load(stem + ".ci", root=CONFORMANCE)
        ctx = mod.context(fuel=None if h["fuel"] is None else int(h["fuel"]),
                          depth=256 if h["depth"] is None else int(h["depth"]))
        with self.assertRaises(cint.Fault) as caught:
            ctx.entry(h["entries"][0])(*[parse_arg(a).value for a in h["args"]])
        want = [line for line in lines if line.startswith("fault.")]
        self.assertEqual(caught.exception.expect_lines(fmt, revision=mod.revision), want)
        consumed = [line for line in lines if line.startswith("fuel-consumed ")]
        self.assertEqual(["fuel-consumed %d" % ctx.fuel_consumed()], consumed)

    def test_depth_limit_stack(self):
        self.case("control/depth_limit_stack")

    def test_stack_three_modules(self):
        self.case("module/stack_three_modules")

    def test_fuel_while_true(self):
        self.case("control/fuel_while_true")


if __name__ == "__main__":
    unittest.main()
