"""Modules, contexts, and calls without a compiler (SPEC-03 6.1, A-12, A-13, A-24; BX10-14, BX10-26, BX10-27).

The calls of `E!T`, tuple, kernel, and wide-integer exports are tested here
through decoded type signatures, including the forms cintc does not compile yet
(tuples, wide integers): a table of `ExportRow` values whose `call` is a ctypes
callback with the wrapper's C signature, and a runtime stand-in with the methods
of `cint._abi.Runtime`. The same Module, Context, carriers, and registrations
run as with a library; test_calls.py and test_kernels.py run them against
`cint build --lib`.
"""
import array
import ctypes
import gc
import os
import struct
import tempfile
import threading
import unittest

import cint
from cint import _abi, _module, _sig
from cint._errors import ErrorResult, error_set_class, error_set_family
from cint._sig import (Array, Dim, EnumDef, EnumRef, ErrorSetDef, ErrorUnion, Parameter, Scalar, Signature,
                       StructDef, StructRef, Field, Tuple)

I64, BOOL = Scalar("I64"), Scalar("Bool")
IO = ErrorSetDef("parse.IoError", "U16", ("closed", "full"))
PARSE = ErrorSetDef("parse.ParseError", "U16", ("empty", "bad_digit", "too_large"))
IO_OR_PARSE = ErrorSetDef("parse.IoOrParse", "U16", members=(0, 1))
P, V, I = ctypes.c_void_p, ctypes.c_int64, ctypes.c_int32


class FakeRuntime:
    """The methods of `cint._abi.Runtime`, recording what the bridge asks."""
    abi = 0x00030000
    major = 3

    def __init__(self):
        self.created, self.destroyed, self.released = [], [], []
        self.registered = {}
        self.next_id = 1
        self.reason = 0
        self.record = None

    def create(self, program, module, depth, frame_arena_bytes):
        self.created.append((program, module, depth, frame_arena_bytes))
        return 0, 0x1000 + len(self.created)

    def destroy(self, ctx):
        self.destroyed.append(ctx)

    def fault(self, ctx):
        return self.record

    def refusal_reason(self, ctx):
        return self.reason

    def consumed(self, ctx):
        return 0, 3

    def clear(self, ctx):
        self.record = None
        return 0

    def register(self, ctx, address, elem, extent, writable, publish):
        ident = self.next_id
        self.next_id += 1
        self.registered[ident] = (ctx, address, elem.name, extent, writable, publish)
        return 0, ident, 1

    def release(self, ctx, ident):
        self.released.append(ident)
        return 0


class Library:
    """A table of exports over ctypes callbacks."""

    def __init__(self):
        self.rows, self.keep = [], []
        self.runtime = FakeRuntime()

    def add(self, name, sig, fn=None, argtypes=(), *, kind="function", effect="pure", params=None, sizes=None,
            raw=None):
        call = 0
        if fn is not None:
            cb = ctypes.CFUNCTYPE(I, *argtypes)(fn)
            self.keep.append(cb)
            call = ctypes.cast(cb, P).value
        data = raw if raw is not None else _sig.encode(sig)
        n = len(sig.params) if sig is not None else 0
        params = params if params is not None else tuple("p%d" % i for i in range(n))
        sizes = sizes if sizes is not None else tuple("s%d" % i for i in range(sig.size_count if sig else 0))
        self.rows.append(_abi.ExportRow(name, kind, effect, sizes, params, data, call, 0))

    def module(self):
        table = _abi.LibraryTable(self.runtime.abi, 0x10, 0x20, bytes(range(32)), (), tuple(self.rows))
        return _module.Module._from_table("fake.so", table, self.runtime)


def store(address, fmt, *values):
    ctypes.memmove(address, struct.pack(fmt, *values), struct.calcsize(fmt))


def error_library():
    lib = Library()

    def read_code(ctx, fuel, code, result):
        if code == 0:
            store(result, "<H6xq", 0, 42)
        else:
            store(result, "<H", code)
        return 0
    lib.add("read_code", Signature((IO, PARSE, IO_OR_PARSE), 0, (Parameter("in", I64),), ErrorUnion(2, I64)),
            read_code, (P, V, V, P))

    def check(ctx, fuel, x, result):
        store(result, "<H", 0 if x >= 0 else 1)
        return 0
    lib.add("check", Signature((PARSE,), 0, (Parameter("in", I64),), ErrorUnion(0, None)), check, (P, V, V, P))

    def split(ctx, fuel, x, result):
        if x < 0:
            store(result, "<B", 1)
        else:
            store(result, "<B3xBxxxi", 0, x & 0xFF, -x)
        return 0
    small = ErrorSetDef("m.Small", "U8", ("negative",))
    lib.add("split", Signature((small,), 0, (Parameter("in", I64),), ErrorUnion(0, Tuple((Scalar("U8"), Scalar("I32"))))),
            split, (P, V, V, P))
    return lib


class ErrorSets(unittest.TestCase):
    """OQ-201 with the 13:57 correction: a combined set's class is a base of
    each member's class, and its tag converts to (member set, member tag)."""

    def test_family(self):
        classes = error_set_family({"parse.IoError": ("declared", ("closed", "full")),
                                    "parse.ParseError": ("declared", ("empty", "bad_digit", "too_large")),
                                    "parse.IoOrParse": ("combined", ("parse.IoError", "parse.ParseError"))})
        io, parse, both = classes["parse.IoError"], classes["parse.ParseError"], classes["parse.IoOrParse"]
        self.assertEqual((io.__name__, parse.__name__, both.__name__), ("IoError", "ParseError", "IoOrParse"))
        self.assertTrue(issubclass(io, both) and issubclass(parse, both))
        self.assertFalse(issubclass(both, cint.Fault))
        self.assertEqual(both.split(4), (parse, 2))
        self.assertEqual(both.split(1), (io, 1))
        e = both.from_tag(4)
        self.assertIs(type(e), parse)
        self.assertEqual((e.set, e.name, e.tag), ("ParseError", "bad_digit", 2))
        self.assertEqual(parse.qualified, "parse.ParseError")
        self.assertIsInstance(e, both)
        for bad in (0, 6, True, "1"):
            with self.assertRaises(ValueError):
                both.from_tag(bad)
        self.assertEqual(io.from_tag(2).name, "full")

    def test_one_member_in_two_combined_sets(self):
        classes = error_set_family({"m.A": ("declared", ("a",)), "m.B": ("declared", ("b",)),
                                    "m.AB": ("combined", ("m.A", "m.B")), "m.BA": ("combined", ("m.B", "m.A"))})
        a = classes["m.A"]
        self.assertTrue(issubclass(a, classes["m.AB"]) and issubclass(a, classes["m.BA"]))
        self.assertEqual(classes["m.AB"].from_tag(2).name, "b")
        self.assertEqual(classes["m.BA"].from_tag(2).name, "a")

    def test_bad_families(self):
        with self.assertRaises(ValueError):
            error_set_family({"m.C": ("combined", ("m.A",))})
        with self.assertRaises(ValueError):
            error_set_family({"m.A": ("declared", ("a",)), "m.C": ("combined", ("m.A", "m.A"))})
        with self.assertRaises(ValueError):
            error_set_class("E", ())
        with self.assertRaises(TypeError):
            ErrorResult.from_tag(1)


class ErrorResults(unittest.TestCase):
    """E!T results through decoded signatures (BX10-06, BX10-27)."""

    def setUp(self):
        self.lib = error_library()
        self.mod = self.lib.module()
        self.ctx = self.mod.context()

    def test_success_and_error(self):
        self.assertEqual(self.ctx.read_code(0), 42)
        with self.assertRaises(self.mod.IoOrParse) as caught:
            self.ctx.read_code(4)
        e = caught.exception
        self.assertIsInstance(e, self.mod.ParseError)
        self.assertNotIsInstance(e, cint.Fault)
        self.assertEqual((e.set, e.name, e.tag), ("ParseError", "bad_digit", 2))
        with self.assertRaises(self.mod.IoError) as caught:
            self.ctx.read_code(2)
        self.assertEqual(caught.exception.name, "full")
        self.assertIs(self.mod.error_sets["parse.IoOrParse"], self.mod.IoOrParse)

    def test_void_success(self):
        self.assertIsNone(self.ctx.check(1))
        with self.assertRaises(self.mod.ParseError):
            self.ctx.check(-1)

    def test_tuple_success(self):
        self.assertEqual(self.ctx.split(300), (44, -300))
        with self.assertRaises(self.mod.Small) as caught:
            self.ctx.split(-1)
        self.assertEqual(caught.exception.name, "negative")

    def test_declarations(self):
        self.assertEqual(self.mod.exports["read_code"].declaration, "parse.IoOrParse!I64 read_code(I64 p0)")
        self.assertEqual(self.mod.exports["split"].declaration, "m.Small!(U8, I32) split(I64 p0)")


class Carriers(unittest.TestCase):
    """A-13 and BX10-26: tuples, wide integers, Bool, fixed point, T27, and a
    kernel's scalar outs."""

    def setUp(self):
        lib = Library()
        self.fuels = []

        def pair(ctx, fuel, x, result):
            self.fuels.append(fuel)
            store(result, "<q?7x", x * 2, x > 0)
            return 0
        lib.add("pair", Signature((), 0, (Parameter("in", I64),), Tuple((I64, BOOL))), pair, (P, V, V, P))

        def wide(ctx, fuel, x, result):
            lo, hi = struct.unpack("<QQ", ctypes.string_at(x, 16))
            v = (hi << 64 | lo) + 1
            store(result, "<QQ", v & (2 ** 64 - 1), (v >> 64) & (2 ** 64 - 1))
            return 0
        lib.add("wide", Signature((), 0, (Parameter("in", Scalar("I128")),), Scalar("I128")), wide, (P, V, P, P))

        def scalars(ctx, fuel, b, q, t, result):
            self.seen = (b, q, t)
            store(result, "<q", q)
            return 0
        lib.add("scalars", Signature((), 0, (Parameter("in", BOOL), Parameter("in", Scalar("Q32.32")),
                                             Parameter("in", Scalar("T27"))), Scalar("Q32.32")),
                scalars, (P, V, ctypes.c_uint8, V, V, P))

        def twin(ctx, fuel, x, a, b):
            store(a, "<q", x + 1)
            store(b, "<i", -x)
            return 0
        lib.add("twin", Signature((), 0, (Parameter("in", I64), Parameter("out", I64), Parameter("out", Scalar("I32"))),
                                  None), twin, (P, V, V, P, P), kind="kernel", params=("x", "a", "b"))

        def once(ctx, fuel, v, out):
            self.view = (v.buffer, v.generation, v.rank, v.perm, v.origin, list(v.shape)[:v.rank],
                         list(v.stride)[:v.rank], v.type.code)
            store(out, "<q", 9)
            return 0
        lib.add("once", Signature((), 1, (Parameter("in", Array(I64, (Dim(1, bound=_sig.Bound(0, 0)),))),
                                         Parameter("out", I64)), None), once, (P, V, _abi.CintView, P),
                kind="kernel", params=("a", "r"), sizes=("n",))
        self.lib = lib
        self.mod = lib.module()
        self.ctx = self.mod.context()

    def test_tuple(self):
        self.assertEqual(self.ctx.pair(5), (10, True))
        self.assertEqual(self.ctx.pair(-5), (-10, False))

    def test_wide(self):
        self.assertEqual(self.ctx.wide(2 ** 100), 2 ** 100 + 1)
        self.assertEqual(self.ctx.wide(-2), -1)
        self.assertEqual(self.ctx.wide(2 ** 64 - 1), 2 ** 64)
        with self.assertRaises(cint.Refused):
            self.ctx.wide(2 ** 127)

    def test_scalar_carriers(self):
        q = cint.Fixed("Q32.32", raw=3 << 31)
        r = self.ctx.scalars(True, q, -(3 ** 27 - 1) // 2)
        self.assertEqual(self.seen, (1, 3 << 31, -(3 ** 27 - 1) // 2))
        self.assertEqual(r, q)
        self.assertIsInstance(r, cint.Fixed)
        with self.assertRaises(cint.Refused):
            self.ctx.scalars(1, q, 0)              # an int for Bool
        with self.assertRaises(cint.Refused):
            self.ctx.scalars(True, q, 3 ** 27)     # outside T27
        with self.assertRaises(cint.Refused) as caught:
            self.ctx.scalars(True, 1, 0)           # case 23b: an int for Q32.32
        self.assertEqual(caught.exception.code, "E_UNSUPPORTED")

    def test_kernel_outs(self):
        self.assertEqual(self.ctx.twin(4), (5, -4))
        self.assertEqual(self.mod.exports["twin"].declaration, "kernel twin(in I64 x, out I64 a, out I32 b)")
        a = array.array("q", [1, 2, 3])
        self.assertEqual(self.ctx.once(a), 9)
        ident = self.view[0]
        self.assertEqual(self.view, (ident, 1, 1, _abi.VIEW_READ, 0, [3], [1], 0x14))
        self.assertIn(ident, self.lib.runtime.released)       # a bare argument's registration ends with the call

    def test_fuel(self):
        self.ctx.pair(1)
        self.ctx.pair(1, fuel=None)
        self.ctx.pair(1, fuel=7)
        self.assertEqual(self.fuels, [10 ** 9, -1, 7])
        for bad in (True, 1.0, "7"):
            with self.assertRaises(TypeError):
                self.ctx.pair(1, fuel=bad)
        with self.assertRaises(cint.Refused) as caught:
            self.ctx.pair(1, fuel=2 ** 63)
        self.assertEqual(caught.exception.code, "E_NARROW")
        ctx = self.mod.context(fuel=None)
        ctx.pair(1)
        self.assertEqual(self.fuels[-1], -1)
        self.assertEqual(ctx.fuel_consumed(), 3)


class Callable(unittest.TestCase):
    """BX10-10, BX10-27, A-24: what is listed and what is called."""

    def setUp(self):
        lib = Library()
        noop = (lambda ctx, fuel, result: 0, (P, V, P))
        s = StructDef("m.S", False, (Field("a", 0, I64),))
        lib.add("make", Signature((s,), 0, (), StructRef(0)), *noop)
        lib.add("take", Signature((s,), 0, (Parameter("in", StructRef(0)),), None), lambda c, f, v: 0, (P, V, P))
        lib.add("color", Signature((EnumDef("m.K", "U8", (("red", 0),)),), 0, (), EnumRef(0)), *noop)
        lib.add("broken", None, raw=_sig.PREFIX + b"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x99")
        lib.add("empty", Signature((ErrorSetDef("m.None_", "U8", ()),), 0, (), ErrorUnion(0, I64)), *noop)
        lib.add("kres", Signature((), 0, (), I64), *noop, kind="kernel")
        lib.add("fout", Signature((), 0, (Parameter("out", I64),), None), lambda c, f, r: 0, (P, V, P))
        lib.add("nocall", Signature((), 0, (), I64))
        lib.add("class", Signature((), 0, (), I64), lambda c, f, r: store(r, "<q", 1) or 0, (P, V, P))
        lib.add("close", Signature((), 0, (), I64), lambda c, f, r: store(r, "<q", 2) or 0, (P, V, P))
        self.lib = lib
        self.mod = lib.module()
        self.ctx = self.mod.context()

    def test_listed_not_callable(self):
        reasons = {
            "make": r"result is struct m\.S.*BX10-27",
            "take": r"parameter p0 is struct m\.S.*BX10-27",
            "color": r"result is enum m\.K",
            "broken": r"A-24",
            "empty": r"no values",
            "kres": r"kernel with a result",
            "fout": r"out parameter",
            "nocall": r"no cx wrapper",
        }
        for name, pattern in reasons.items():
            with self.subTest(name):
                export = self.mod.exports[name]
                self.assertFalse(export.callable)
                self.assertRegex(export.reason, pattern)
                with self.assertRaisesRegex(NotImplementedError, pattern):
                    self.ctx.entry(name)()

    def test_names(self):
        self.assertEqual(self.ctx.entry("class")(), 1)
        with self.assertRaisesRegex(AttributeError, "keyword"):
            getattr(self.ctx, "class")
        self.assertEqual(self.ctx.entry("close")(), 2)        # ctx.close is the method
        with self.assertRaisesRegex(AttributeError, "no export named nothing"):
            self.ctx.entry("nothing")
        with self.assertRaisesRegex(AttributeError, "no error set"):
            self.mod.Nothing
        self.assertIn("make", dir(self.ctx))
        with self.assertRaises(NotImplementedError):
            self.ctx.entry("make").inplace()


class Registrations(unittest.TestCase):
    """P-20: one registration per array and context, ended by release, by
    collection, or with the context; a release during an entry waits for it."""

    def setUp(self):
        lib = Library()
        self.during = None

        def total(ctx, fuel, v, result):
            if self.during is not None:
                self.during()
            store(result, "<q", v.shape[0])
            return 0
        lib.add("total", Signature((), 1, (Parameter("in", Array(I64, (Dim(1, bound=_sig.Bound(0, 0)),))),), I64),
                total, (P, V, _abi.CintView, P), sizes=("n",))

        def scale(ctx, fuel, v, k):
            self.perm = v.perm
            return 0
        lib.add("scale", Signature((), 1, (Parameter("inout", Array(I64, (Dim(1, bound=_sig.Bound(0, 0)),))),
                                           Parameter("in", I64)), None), scale, (P, V, _abi.CintView, V),
                sizes=("n",), params=("a", "k"))
        self.lib = lib
        self.rt = lib.runtime
        self.mod = lib.module()
        self.ctx = self.mod.context()

    def test_once_per_context(self):
        v = cint.borrow(array.array("q", [1, 2]))
        self.assertEqual(self.ctx.total(v), 2)
        self.assertEqual(self.ctx.total(v), 2)
        self.assertEqual(len(self.rt.registered), 1)
        other = self.mod.context()
        other.total(v)
        self.assertEqual(len(self.rt.registered), 2)
        v.release()
        self.assertEqual(sorted(self.rt.released), [1, 2])

    def test_buffer_and_ceiling(self):
        b = cint.copy([1, 2, 3], elem="I64")
        self.ctx.scale(k=2, a=b)
        self.assertEqual(self.perm, _abi.VIEW_WRITE)
        ctx, address, elem, extent, writable, publish = self.rt.registered[1]
        # A Buffer's output is staged and copied, since the runtime cannot rename it (M-31).
        self.assertEqual((elem, extent, writable, publish), ("I64", 3, True, True))
        ro = cint.borrow(array.array("q", [1]))
        self.ctx.scale(k=2, a=ro)                 # the runtime faults bind.permission (H-12); the view says read
        self.assertEqual(self.perm, _abi.VIEW_READ)
        w = cint.borrow(array.array("q", [1]), writable=True, publish="copy")
        self.ctx.scale(k=2, a=w)
        self.assertEqual(self.rt.registered[3][4:], (True, True))
        b.release()
        self.assertIn(1, self.rt.released)

    def test_release_during_an_entry_waits(self):
        v = cint.borrow(array.array("q", [1, 2, 3]))
        self.ctx.total(v)
        seen = []
        self.during = lambda: (v.release(), seen.append(list(self.rt.released)))
        self.ctx.total(array.array("q", [4]))      # a bare argument, borrowed for the call
        self.assertEqual(seen, [[]])               # nothing released while the entry ran
        self.assertEqual(sorted(self.rt.released), [1, 2])

    def test_collected_holder(self):
        v = cint.borrow(array.array("q", [1]))
        self.ctx.total(v)
        del v
        gc.collect()
        self.assertEqual(self.rt.released, [])
        self.ctx.total(array.array("q", [1]))      # queued releases go at the next call
        self.assertEqual(sorted(self.rt.released), [1, 2])

    def test_close(self):
        v = cint.borrow(array.array("q", [1]))
        self.ctx.total(v)
        self.ctx.close()
        self.assertEqual(self.rt.destroyed, [0x1001])
        self.assertEqual(v._registrations, {})
        v.release()
        self.assertEqual(self.rt.released, [])     # destroyed with the context
        self.ctx.close()
        with self.assertRaisesRegex(ValueError, "closed"):
            self.ctx.total(array.array("q", [1]))
        with self.mod.context() as ctx:
            ctx.total(array.array("q", [1]))
        self.assertTrue(ctx.closed)

    def test_borrow_with_context(self):
        v = cint.borrow(array.array("q", [1]), context=self.ctx)
        self.assertEqual(self.ctx.total(v), 1)
        self.ctx.close()
        with self.assertRaisesRegex(ValueError, "closed"):
            cint.borrow(array.array("q", [1]), context=self.ctx)

    def test_busy(self):
        entered, leave = threading.Event(), threading.Event()

        def block():
            entered.set()
            leave.wait(10)
        self.during = block
        out = []
        t = threading.Thread(target=lambda: out.append(self.ctx.total(array.array("q", [1, 2]))))
        t.start()
        try:
            self.assertTrue(entered.wait(10))
            with self.assertRaisesRegex(cint.Busy, "already running"):
                self.ctx.total(array.array("q", [1]))
            with self.assertRaises(cint.Busy):
                self.ctx.close()
            other = self.mod.context()            # another context is free
            self.during = None
            self.assertEqual(other.total(array.array("q", [7])), 1)
        finally:
            leave.set()
            t.join(10)
        self.assertEqual(out, [2])


class Contexts(unittest.TestCase):

    def test_configuration(self):
        lib = Library()
        mod = lib.module()
        mod.context(depth=4, frame_arena_elements=10)
        self.assertEqual(lib.runtime.created[-1], (0x20, 0x10, 4, 80))
        mod.context()
        self.assertEqual(lib.runtime.created[-1][2:], (256, 2_097_152 * 8))
        for kwargs in ({"device": "cpu"}, {"host": {"f": print}}, {"grants": object()}, {"record": object()},
                       {"check_inputs": True}):
            with self.subTest(kwargs):
                with self.assertRaises(NotImplementedError):
                    mod.context(**kwargs)
        for kwargs, error in (({"depth": -1}, ValueError), ({"depth": 1.5}, TypeError),
                              ({"frame_arena_elements": 2 ** 62}, ValueError), ({"fuel": True}, TypeError)):
            with self.subTest(kwargs):
                with self.assertRaises(error):
                    mod.context(**kwargs)
        ctx = mod.context()
        for name in ("checkpoint", "state_hash", "entry_scope"):
            with self.assertRaisesRegex(NotImplementedError, "box 13"):
                getattr(ctx, name)()
        with self.assertRaisesRegex(NotImplementedError, "box 13"):
            ctx.restore(b"")
        self.assertIsNone(ctx.last_entry())
        self.assertIsNone(ctx.fault())


class Load(unittest.TestCase):

    def test_backends_and_paths(self):
        with self.assertRaisesRegex(NotImplementedError, "T4"):
            cint.load("x.ci", backend="ref")
        with self.assertRaises(NotImplementedError):
            cint.load("x.ci", backend="cuda")
        with self.assertRaises(ValueError):
            cint.load("x.ci", backend="gpu")
        with self.assertRaisesRegex(cint.LoadError, "missing"):
            cint.load("x.so", require_verified=True)
        with self.assertRaisesRegex(cint.LoadError, "neither"):
            cint.load(os.path.join(tempfile.gettempdir(), "no-such-library-here.so"))
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "libnot.so")
            with open(path, "wb") as f:
                f.write(b"not a library")
            with self.assertRaisesRegex(cint.LoadError, "could not be loaded"):
                cint.load(path)

    def test_no_cint(self):
        with tempfile.TemporaryDirectory() as d:
            src = os.path.join(d, "p.ci")
            with open(src, "w") as f:
                f.write("export I64 one() { return 1; }\n")
            old = os.environ.get("CINT_EXE")
            os.environ["CINT_EXE"] = os.path.join(d, "cint")
            try:
                with self.assertRaisesRegex(cint.BuildError, "cint build --lib") as caught:
                    cint.load(src)
                self.assertIsInstance(caught.exception, cint.LoadError)
            finally:
                if old is None:
                    del os.environ["CINT_EXE"]
                else:
                    os.environ["CINT_EXE"] = old

    def test_library_without_a_table(self):
        with self.assertRaisesRegex(_abi.LibraryProblem, "cint_library_desc"):
            _abi.read_library(ctypes.pythonapi)

    def test_layouts(self):
        self.assertIsNone(_abi.layout_problem())


if __name__ == "__main__":
    unittest.main()
