"""The Python device path on the stand-in CUDA driver (SPEC-03 6.1, 6.7, M-28, P-10, P-11,
P-18, section 9 cases 18, 20, 20a and 21; SPEC-02 F-5 check 4, B-16b, B-16d; box 11 units 5
and 5b, BX11-16, OQ-217), and the matrix multiply and the stencil of SPEC-09 section 14 as
kernels (box 11 unit 6b).

`rt/tests/cuda_fake.c` with `ptx_interp.c` is built as a shared library with the
toolchain's C compiler, and `CINT_CUDA_DRIVER` names it while this module runs:
`cint.device("cuda:0")` opens it, and a `backend="cuda"` library is built with
`CINT_CUDA_TESTING`, so its runtime opens it too and interprets each kernel's PTX
on the host, in descending order within a launch (tools/cint_check.py --stand-in).
Its device memory is host memory that it tracks, so the arrays a kernel reads are
allocated through it: `DeviceArray` is a DLPack producer over such memory, as a
CuPy or PyTorch array is over the GPU's. The tests skip where no `cint` of this tree
or no C compiler is found.

With `CINT_TEST_CUDA=device` the same tests run on the system driver and the GPU
instead, and so do the classes `CuPy` and `Torch`, which run SPEC-03 6.7's GPU half
as it is written, case 18, and the read-only export of Open question 17 with the
package where it is installed (OQ-47); neither package is a dependency of `cint`.
"""
import array
import ctypes
import hashlib
import os
import subprocess
import tempfile
import unittest

import cint
from cint import _build, _cuda, _dlpack, _memory
from tests import test_calls
from tests.support import ROOT, Producer, numpy, reference_path, torch

try:
    import cupy
except ImportError:
    cupy = None

DEVICE = os.environ.get("CINT_TEST_CUDA") == "device"       # the system driver and the GPU
PROGRAMS = test_calls.PROGRAMS
STAND_IN = (ROOT / "rt" / "tests" / "cuda_fake.c", ROOT / "rt" / "tests" / "ptx_interp.c")
_saved = None
_modules = {}


def _stand_in(tool: _build.Toolchain) -> str:
    """The stand-in driver library, built once per toolchain and source digest."""
    v = tool.values
    h = hashlib.sha256(tool.identity.encode("ascii"))
    for src in STAND_IN + (ROOT / "rt" / "tests" / "ptx_interp.h",):
        h.update(src.read_bytes())
    out_dir = os.path.join(tool.cache, "py-cuda", "stand-in-" + h.hexdigest()[:32])
    msvc = v.get("leg") == "msvc"
    lib = os.path.join(out_dir, "cuda-stand-in" + (".dll" if msvc else ".dylib" if v.get("host") == "macos"
                                                    else ".so"))
    if os.path.isfile(lib):
        return lib
    os.makedirs(out_dir, exist_ok=True)
    work = tempfile.mkdtemp(prefix="build-", dir=out_dir)
    if msvc:
        cmd = [v["cc"], "/nologo", "/LD", "/DCINT_FAKE_PTX", "/I" + str(ROOT / "rt" / "tests"), *map(str, STAND_IN),
               "/Fo" + work + os.sep, "/Fe" + os.path.join(work, os.path.basename(lib))]
    else:
        cmd = [v["cc"], "-shared", "-fPIC", "-O0", "-DCINT_FAKE_PTX", "-I" + str(ROOT / "rt" / "tests"),
               *map(str, STAND_IN), "-o", os.path.join(work, os.path.basename(lib))]
    env = dict(os.environ)
    env.update(value.split("=", 1) for name, value in tool.rows if name == "env" and "=" in value)
    done = subprocess.run(cmd, cwd=work, env=env, capture_output=True, text=True)
    if done.returncode != 0:
        raise unittest.SkipTest("the stand-in driver does not build here: %s" % (done.stderr or done.stdout)[-2000:])
    os.replace(os.path.join(work, os.path.basename(lib)), lib)
    return lib


def setUpModule():
    global _saved
    test_calls.setUpModule()
    _saved = os.environ.get(_cuda.DRIVER_VARIABLE)
    if DEVICE:
        os.environ.pop(_cuda.DRIVER_VARIABLE, None)
        try:
            cint.device("cuda:0")
        except OSError as failure:
            tearDownModule()
            raise unittest.SkipTest("CINT_TEST_CUDA=device, and no CUDA device opens: %s" % failure)
        return
    os.environ[_cuda.DRIVER_VARIABLE] = _stand_in(_build.Toolchain(_build.find_cint()))


def tearDownModule():
    if _saved is None:
        os.environ.pop(_cuda.DRIVER_VARIABLE, None)
    else:
        os.environ[_cuda.DRIVER_VARIABLE] = _saved


def gpu() -> cint.Device:
    return cint.device("cuda:0")


def load(rel: str, backend: str = "cuda") -> cint.Module:
    """A program of programs/, built once per run for `backend`."""
    key = (rel, backend)
    if key not in _modules:
        try:
            _modules[key] = cint.load(os.path.join(PROGRAMS, rel), backend=backend)
        except cint.BuildError as failure:
            if failure.status == 5:
                raise unittest.SkipTest("cint build --lib cannot run here: %s" % failure)
            raise
    return _modules[key]


def i64s(*values) -> array.array:
    return array.array("q", values)


class DeviceArray(Producer):
    """A DLPack producer over device memory the stand-in allocated, as a CUDA
    package's array is: `values` uploaded, every capsule versioned with flags 0
    (or as the keywords of `Producer` say), and each call recorded."""

    def __init__(self, values, *, device_id=0, **kwargs):
        kwargs.setdefault("device", (_dlpack.kDLCUDA, device_id))
        super().__init__(values, **kwargs)
        self.memory = _cuda.Memory(gpu(), ctypes.sizeof(self.storage))
        self.memory.upload(bytes(self.storage))

    @property
    def address(self) -> int:
        return self.memory.address

    def values(self) -> list:
        data = self.memory.download()
        return list(memoryview(data).cast(self.storage._type_._type_))

    def write(self, index: int, value: int):
        one = (self.storage._type_ * 1)(value)
        gpu()._gpu.upload(self.address + index * ctypes.sizeof(one), bytes(one))


def consume(capsule) -> _dlpack.Tensor:
    """Take a capsule as a consumer does."""
    return _dlpack.Tensor(capsule)


class Devices(unittest.TestCase):
    def test_device(self):
        d = gpu()
        self.assertIs(cint.device("cuda:0"), d)
        self.assertIs(cint.device(d), d)
        self.assertEqual((d.kind, d.index, str(d)), ("cuda", 0, "cuda:0"))
        if not DEVICE:
            self.assertEqual(d.name, "cint fake device")
        self.assertTrue(cint.device("cpu").is_host)
        with self.assertRaisesRegex(OSError, "cuDeviceGet\\(1\\).*CUDA_ERROR_INVALID_DEVICE"):
            cint.device("cuda:1")
        for spec, error in (("cuda", ValueError), ("cuda:x", ValueError), ("gpu:0", ValueError), (0, TypeError),
                            ("rocm:0", NotImplementedError)):
            with self.subTest(spec), self.assertRaises(error):
                cint.device(spec)

    def test_missing_driver(self):
        driver = os.environ.get(_cuda.DRIVER_VARIABLE)
        os.environ[_cuda.DRIVER_VARIABLE] = os.path.join(tempfile.gettempdir(), "no-such-cuda-driver.so")
        try:
            with self.assertRaisesRegex(OSError, "no-such-cuda-driver.*NVIDIA driver"):
                cint.device("cuda:0")
        finally:
            if driver is None:
                os.environ.pop(_cuda.DRIVER_VARIABLE)
            else:
                os.environ[_cuda.DRIVER_VARIABLE] = driver


class Buffers(unittest.TestCase):
    def test_copy_and_empty(self):
        b = cint.copy([1, -2, 3], elem="I64", device=gpu())
        self.assertIs(b.device, gpu())
        self.assertEqual(repr(b), "<cint.Buffer I64[3] published on cuda:0>")
        self.assertEqual(b.region.device, "cuda:0")
        self.assertEqual(cint.copy(b).tolist(), [1, -2, 3])            # the explicit copy to the host
        self.assertIs(cint.copy(b).device, cint.device("cpu"))
        again = cint.copy(b, device="cuda:0", elem="I16")
        self.assertEqual((again.elem, cint.copy(again).tolist()), ("I16", [1, -2, 3]))
        e = cint.empty((2, 3), "U8", device=gpu())
        self.assertFalse(e.published)
        with self.assertRaises(cint.Unpublished):
            cint.copy(e)

    def test_no_implicit_reads(self):
        b = cint.copy([1, 2], elem="I32", device=gpu())
        for read in (b.tolist, b.memoryview, lambda: cint.to_float(b)):
            with self.subTest(read), self.assertRaisesRegex(cint.Refused, "cuda:0 memory.*cint.copy"):
                read()
        with self.assertRaisesRegex(BufferError, "cuda:0"):
            b.__buffer__(0)                    # memoryview(b) from Python 3.12

    def test_device_elements(self):
        for elem in ("Bool", "Q16.16", "T27", "I128"):
            with self.subTest(elem), self.assertRaisesRegex(cint.Refused, "I8 to U64"):
                cint.empty((2,), elem, device=gpu())

    def test_dlpack_export(self):
        """P-18: a read-only versioned capsule, counted until the consumer's
        deleter runs; an output with a live export is refused."""
        b = cint.copy([5, 6, 7], elem="I64", device=gpu())
        self.assertEqual(b.__dlpack_device__(), (_dlpack.kDLCUDA, 0))
        t = consume(b.__dlpack__(stream=1, max_version=(1, 0)))
        self.assertTrue(t.versioned)
        self.assertEqual((t.version, t.flags & _dlpack.FLAG_READ_ONLY), ((1, 0), _dlpack.FLAG_READ_ONLY))
        self.assertEqual((t.data, t.device, t.shape, t.strides, t.dtype),
                         (b.region.address, (2, 0), (3,), (1,), (_dlpack.kDLInt, 64, 1)))
        self.assertEqual(b.exports, 1)
        with self.assertRaisesRegex(cint.Busy, "E_ALIAS"):
            load("kernels.ci").context(device=gpu()).scale(cint.copy([1, 2, 3], elem="I64", device=gpu()), 2, y=b)
        t.delete()
        self.assertEqual(b.exports, 0)
        b.__dlpack__(max_version=(1, 2))           # never consumed: the capsule's destructor ends it
        self.assertEqual(b.exports, 0)
        for kwargs, error in (({}, BufferError), ({"max_version": (1, 0), "copy": True}, BufferError),
                              ({"max_version": (1, 0), "dl_device": (1, 0)}, BufferError),
                              ({"max_version": (1, 0), "stream": 0}, ValueError)):
            with self.subTest(kwargs), self.assertRaises(error):
                b.__dlpack__(**kwargs)
        self.assertEqual(b.exports, 0)

    def test_host_dlpack_export(self):
        b = cint.copy([1, 2, 3, 4], elem="I8")
        t = consume(b.__dlpack__(max_version=(1, 0)))
        self.assertEqual((t.device, t.flags, t.shape), ((_dlpack.kDLCPU, 0), _dlpack.FLAG_READ_ONLY, (4,)))
        self.assertEqual(ctypes.string_at(t.data, 4), bytes([1, 2, 3, 4]))
        self.assertEqual(b.exports, 1)
        b.release()
        self.assertEqual(ctypes.string_at(t.data, 4), bytes([1, 2, 3, 4]))   # the export keeps the storage
        t.delete()


class Borrows(unittest.TestCase):
    def test_borrow(self):
        a = DeviceArray([4, 5, 6])
        with cint.borrow(a) as v:
            self.assertEqual(a.calls[-1]["stream"], -1)          # no synchronization at the borrow
            self.assertEqual((v.elem, v.shape, v.device, v.region.device), ("I64", (3,), gpu(), "cuda:0"))
            self.assertIn("on cuda:0", repr(v))
            with self.assertRaisesRegex(cint.Refused, "cuda:0 memory"):
                v.tolist()
            self.assertEqual(cint.copy(v).tolist(), [4, 5, 6])
            self.assertEqual(a.calls[-1]["stream"], gpu()._gpu.stream)   # the copy's exchange is on its stream
        self.assertEqual((a.exported, a.deleted), (2, 2))
        self.assertEqual(cint.copy(a).tolist(), [4, 5, 6])

    def test_refusals(self):
        cases = [
            (DeviceArray([1], dtype=(_dlpack.kDLBool, 8, 1), ctype=ctypes.c_uint8), "I8 to U64"),
            (DeviceArray([1], legacy_signature=True), "takes no max_version"),
            (DeviceArray([1], versioned=False), "unversioned DLPack capsule of CUDA memory"),
            (DeviceArray([1], flags=_dlpack.FLAG_IS_COPIED), "copied the data"),
        ]
        for producer, fragment in cases:
            with self.subTest(fragment), self.assertRaisesRegex(cint.BorrowRefused, fragment):
                cint.borrow(producer)
        with self.assertRaisesRegex(cint.BorrowRefused, "I8 to U64"):
            cint.borrow(DeviceArray([1]), elem="Q32.32")

    def test_case_18_at_borrow(self):
        """Case 18: a CUDA array for a context on another device, or on none, is
        refused with E_UNSUPPORTED at the borrow when context= is given (P-11)."""
        for ctx in (load("kernels.ci").context(), load("kernels.ci", "cpu").context()):
            a = DeviceArray([1, 2])
            with self.subTest(ctx), self.assertRaisesRegex(cint.Refused, "on cuda:0.*P-11") as caught:
                cint.borrow(a, context=ctx)
            self.assertEqual((caught.exception.code, a.exported), ("E_UNSUPPORTED", 0))
        gctx = load("kernels.ci").context(device=gpu())
        a = DeviceArray([1, 2])
        with cint.borrow(a, context=gctx):
            self.assertEqual(a.calls[-1]["stream"], gctx._stream())  # the first exchange, on its stream (P-10)

    def test_case_18_at_entry(self):
        """Case 18: at the entry that binds it, before anything runs: no exchange,
        no fuel."""
        for ctx in (load("kernels.ci").context(), load("kernels.ci", "cpu").context()):
            a = DeviceArray([1, 2])
            with self.subTest(ctx), cint.borrow(a) as v:
                with self.assertRaisesRegex(cint.Refused, "parameter x is on cuda:0") as caught:
                    ctx.scale(v, 3, y=cint.empty((2,), "I64"))
                self.assertEqual((caught.exception.code, a.exported), ("E_UNSUPPORTED", 1))
                self.assertIsNone(ctx.last_entry())
        gctx = load("kernels.ci").context(device=gpu())
        a = DeviceArray([1, 2], device_id=1)
        with cint.borrow(a) as v:
            with self.assertRaisesRegex(cint.Refused, "on cuda:1, and this context runs on cuda:0"):
                gctx.scale(v, 3, y=cint.empty((2,), "I64", device=gpu()))


class Calls(unittest.TestCase):
    def test_kernel(self):
        """A kernel over device data: the DLPack exchange at entry on the context's
        stream (P-10); outputs staged in device memory and published by copy (B-16d)."""
        ctx = load("kernels.ci").context(device=gpu())
        self.assertIs(ctx.device, gpu())
        x = DeviceArray([1, 2, 3])
        y = cint.empty((3,), "I64", device=gpu())
        out = DeviceArray([0, 0, 0])
        with cint.borrow(x) as v, cint.borrow(out, writable=True, publish="copy") as o:
            ctx.scale(v, 5, y=y)
            self.assertEqual(x.calls[-1]["stream"], ctx._stream())
            self.assertEqual(ctx.last_entry().copies, (cint.CopyRecord("y", "staging", "I64", 3),))
            self.assertEqual(ctx.scale(y, 2, y=o), None)
            self.assertEqual(ctx.last_entry().copies, (cint.CopyRecord("y", "publish-copy", "I64", 3),))
        self.assertEqual((cint.copy(y).tolist(), out.values()), ([5, 10, 15], [10, 20, 30]))
        self.assertEqual(ctx.fuel_consumed(), 3)
        self.assertEqual(ctx.square(y, y=cint.empty((3,), "I64", device=gpu())), 350)

    def test_host_memory(self):
        """A context without a device runs the kernels over mapped host memory;
        one with a device refuses host memory at a kernel's entry (M-28a), and
        takes it at a function's entry (M-28)."""
        mod = load("kernels.ci")
        out = cint.empty((3,), "I64")
        mod.context().scale(i64s(1, 2, 3), 7, y=out)
        self.assertEqual(out.tolist(), [7, 14, 21])
        ctx = mod.context(device=gpu())
        with self.assertRaises(cint.UnsupportedFault) as caught:
            ctx.scale(i64s(1, 2, 3), 7, y=cint.empty((3,), "I64", device=gpu()))
        f = caught.exception
        self.assertEqual((f.operation, f.operands, f.phase, f.kernel), ("bind.placement", (0,), "entry", "kernels.scale"))
        ctx.clear_fault()
        self.assertEqual(ctx.trace(memoryview(i64s(1, 2, 3, 4)).cast("B").cast("q", (2, 2))), 5)

    def test_case_20a(self):
        """Case 20a with device arrays: twice passes a and b only, whole, to its
        dispatches, so its entry takes them on the device (OQ-217). The second
        dispatch faults; b keeps the first's result and a is unchanged (M-30), for
        device Buffers and borrowed device Views alike."""
        a, b = cint.copy([1, 2, 3], elem="I64", device=gpu()), cint.copy([0, 0, 0], elem="I64", device=gpu())
        da, db = DeviceArray([1, 2, 3]), DeviceArray([0, 0, 0])
        with cint.borrow(da, writable=True) as va, cint.borrow(db, writable=True) as vb:
            for pair in ((a, b), (va, vb)):
                ctx = load("kernels.ci").context(device=gpu())
                with self.assertRaises(cint.OverflowFault) as caught:
                    ctx.twice(a=pair[0], b=pair[1])
                f = caught.exception
                self.assertEqual((f.phase, f.dispatch, f.work_item, f.kernel), ("work-item", 1, 0, "kernels.scale"))
                self.assertEqual(f.stack, (("kernels.ci", 30, 5),))
        self.assertEqual((cint.copy(b).tolist(), cint.copy(a).tolist()), ([2, 4, 6], [1, 2, 3]))
        self.assertEqual((db.values(), da.values()), ([2, 4, 6], [1, 2, 3]))

    def test_marks(self):
        """A function's entry takes a device array only for a parameter its body
        passes only, whole, to dispatches, and a dispatch issued from `.ci` takes
        device arrays only or storage the context owns only (F-5 check 4, OQ-217)."""
        mod = load("passing.ci")
        ctx = mod.context(device=gpu())
        x, y = cint.copy([1, 2, 3], elem="I64", device=gpu()), cint.copy([0, 0, 0], elem="I64", device=gpu())
        ctx.triple(x, y=y)
        self.assertEqual(cint.copy(y).tolist(), [3, 6, 9])

        def fault(call, operand, kernel=None):
            with self.assertRaises(cint.UnsupportedFault) as caught:
                call()
            f = caught.exception
            self.assertEqual((f.operation, f.operands, f.phase, f.kernel),
                             ("bind.placement", (operand,), kernel and "entry", kernel))
            ctx.clear_fault()
            return f

        fault(lambda: ctx.head(x, y=y), 0)                              # x is indexed
        fault(lambda: ctx.forward(x, y=y), 0)                           # x goes to another function
        f = fault(lambda: ctx.head(i64s(1, 2, 3), y=y), 0, "passing.scale")   # host memory at the dispatch
        self.assertEqual((f.dispatch, f.position, f.stack), (0, "passing.ci:18:5", ()))   # the dispatch site
        fault(lambda: ctx.triple(x, y=cint.copy([0, 0, 0], elem="I64")), 2, "passing.scale")   # y differs from x
        fault(lambda: ctx.own(x), 2, "passing.scale")                  # the frame's array differs from x
        fault(lambda: ctx.own(i64s(1, 2, 3)), 0, "passing.scale")
        self.assertEqual(cint.copy(y).tolist(), [3, 6, 9])
        plain, out = mod.context(), cint.copy([0, 0, 0], elem="I64")     # no device: host memory, mapped (M-28a)
        self.assertEqual((plain.head(i64s(4, 5, 6), y=out), out.tolist()), (4, [8, 10, 12]))
        plain.forward(i64s(1, 2, 3), y=out)
        self.assertEqual((out.tolist(), plain.own(i64s(1, 2, 3))), ([3, 6, 9], 30))

    def test_case_20(self):
        """Case 20: a dispatch that faults leaves its device Buffer output as last
        published (M-30), and the copy report lists the device arrays read back
        for the host's re-execution of the faulting item (B-16b)."""
        b = cint.empty((3,), "I64", device=gpu())
        ctx = load("kernels.ci").context(device=gpu())
        ctx.scale(cint.copy([1, 2, 3], elem="I64", device=gpu()), 2, y=b)
        ctx = load("kernels.ci").context(device=gpu())
        with self.assertRaises(cint.OverflowFault) as caught:
            ctx.scale(cint.copy([1, 1 << 62, 3], elem="I64", device=gpu()), 2, y=b)
        f = caught.exception
        self.assertEqual((f.phase, f.dispatch, f.work_item), ("work-item", 0, 1))
        self.assertEqual(f.stack, (("kernels.ci", 5, 15),))
        self.assertEqual(cint.copy(b).tolist(), [2, 4, 6])
        self.assertEqual(ctx.last_entry().copies, (cint.CopyRecord("x", "fault-readback", "I64", 3),
                                                   cint.CopyRecord("y", "fault-readback", "I64", 3)))
        out = DeviceArray([7, 7, 7])
        with cint.borrow(out, writable=True, publish="copy") as o, self.assertRaises(cint.OverflowFault):
            load("kernels.ci").context(device=gpu()).scale(DeviceArray([1, 1 << 62, 3]), 2, y=o)
        self.assertEqual(out.values(), [7, 7, 7])

    def test_case_21(self):
        """Case 21: work-items 900 and 7 fault; the stand-in runs 900 first, and the
        reported fault is 7's, with the record of the same dispatch on cpu-c17."""
        values = [0] * 1000
        values[7] = values[900] = 1 << 32
        records = []
        for ctx, x, y in ((load("kernels.ci", "cpu").context(), cint.copy(values, elem="I64"),
                           cint.empty((1000,), "I64")),
                          (load("kernels.ci").context(device=gpu()), cint.copy(values, elem="I64", device=gpu()),
                           cint.empty((1000,), "I64", device=gpu()))):
            with self.assertRaises(cint.OverflowFault) as caught:
                ctx.square(x, y=y)
            got = caught.exception
            self.assertEqual((got.work_item, got.step), (7, 2))
            records.append((got.expect_lines(4), ctx.fuel_consumed()))
        self.assertEqual(records[0], records[1])


class Example67(unittest.TestCase):
    """SPEC-03 6.7's GPU half on the stand-in, with DeviceArray in place of CuPy:
    the same outputs as the CPU half, the same fault, and the read-only export."""

    n = 1000

    def test_gpu_half(self):
        n = self.n
        cpu = load("advance.ci", "cpu").context()
        nxt = cint.empty((n,), "I64")
        cpu.advance(i64s(*range(n)), i64s(*[3] * n), 16, next=nxt)
        g = gpu()
        gctx = load("advance.ci").context(device=g)
        pos_d, vel_d = DeviceArray(list(range(n))), DeviceArray([3] * n)
        nxt_d = cint.empty((n,), "I64", device=g)
        with cint.borrow(pos_d) as p, cint.borrow(vel_d) as v:
            gctx.advance(p, v, 16, next=nxt_d)
        t = consume(nxt_d.__dlpack__(stream=1, max_version=(1, 0)))     # the consumer's read-only view
        self.assertEqual(t.flags & _dlpack.FLAG_READ_ONLY, _dlpack.FLAG_READ_ONLY)
        got = _memory.unpack(g._gpu.download(t.data, 8 * n), True, 8)
        self.assertEqual(got[10], 10 + 3 * 16)
        self.assertEqual(got, nxt.tolist())                      # same bytes as the CPU run
        self.assertEqual(cpu.total(nxt), 500 * (n - 1) + 48 * n)
        t.delete()                                              # release the export before reusing nxt_d
        vel_d.write(7, 1 << 62)
        with self.assertRaises(cint.OverflowFault) as caught:
            with cint.borrow(pos_d) as p, cint.borrow(vel_d) as v:
                gctx.advance(p, v, 16, next=nxt_d)
        f = caught.exception
        self.assertEqual((f.code, f.operation, f.operands, f.work_item),
                         ("E_OVERFLOW", "mul.checked.i64", (4611686018427387904, 16), 7))
        self.assertEqual(cint.copy(nxt_d).tolist(), nxt.tolist())   # nothing partial is visible (M-30)
        with self.assertRaises(cint.Faulted):
            gctx.advance(DeviceArray([0]), DeviceArray([0]), 1, next=cint.empty((1,), "I64", device=g))
        gctx.clear_fault()
        with self.assertRaises(cint.UnsupportedFault):           # host memory reaching a device kernel (M-28a)
            gctx.advance(cint.borrow(i64s(1)), DeviceArray([0]), 1, next=cint.empty((1,), "I64", device=g))


def matmul_inputs():
    """The inputs of examples/matmul_i8.py as rows: four rows of 16 activations from -8 to 8,
    the last scaled by 15, and 16 by 3 weights of -1, 0 or 1."""
    x = [[(i * 5 + p * 3) % 17 - 8 for p in range(16)] for i in range(4)]
    x[3] = [v * 15 for v in x[3]]
    return x, [[(p + 2 * j) % 3 - 1 for j in range(3)] for p in range(16)]


def product(x, w) -> list:
    return [[sum(a * b for a, b in zip(row, col)) for col in zip(*w)] for row in x]


class Matmul(unittest.TestCase):
    """SPEC-09 section 14's matrix multiply as kernels (programs/matmul.ci): on gpu-cuda,
    from host memory on a context without a device and from device memory, the exact
    product, also when dispatched from `.ci`, and kept in I8, the fault that cpu-c17 and
    cint_ref give, with nothing published."""

    def test_product_and_fault(self):
        x, w = matmul_inputs()
        want, faults = product(x, w), []
        for name, ctx, device in (("cpu-c17", load("matmul.ci", "cpu").context(), None),
                                  ("gpu-cuda, host memory", load("matmul.ci").context(), None),
                                  ("gpu-cuda, device memory", load("matmul.ci").context(device=gpu()),
                                   gpu())):
            with self.subTest(backend=name):
                xd, wd = (cint.copy(rows, elem="I8", device=device) for rows in (x, w))
                y = cint.empty((4, 3), "I32", device=device)
                ctx.matmul(xd, wd, y=y)
                self.assertEqual(cint.copy(y).tolist(), want)
                y2 = cint.copy([[0] * 3] * 4, elem="I32", device=device)
                ctx.product(xd, wd, y=y2)
                self.assertEqual(cint.copy(y2).tolist(), want)
                with self.assertRaises(cint.OverflowFault) as caught:
                    ctx.matmul_narrow(xd, wd, y=y)
                self.assertEqual(cint.copy(y).tolist(), want)       # nothing published (M-30)
                faults.append((caught.exception.expect_lines(4), ctx.fuel_consumed()))
        self.assertEqual(faults[1:], faults[:1] * 2)

    def test_fault_matches_the_reference(self):
        """The fault on the device has the fields cint_ref gives the same dispatch from `.ci`;
        only the stack's one frame differs: a dispatch from Python stands at the kernel, and
        cint_ref's at the call in run()."""
        x, w = matmul_inputs()
        g = gpu()
        with self.assertRaises(cint.OverflowFault) as caught:
            load("matmul.ci").context(device=g).matmul_narrow(
                cint.copy(x, elem="I8", device=g), cint.copy(w, elem="I8", device=g),
                y=cint.empty((4, 3), "I32", device=g))
        got = caught.exception
        lines = ["export I64 run() {", "    I8[4, 16] x;", "    I8[16, 3] w;", "    I32[4, 3] y;"]
        lines += ["    x[%d, %d] = %d;" % (i, p, v) for i, row in enumerate(x) for p, v in enumerate(row)]
        lines += ["    w[%d, %d] = %d;" % (p, j, v) for p, row in enumerate(w) for j, v in enumerate(row)]
        lines += ["    matmul_narrow(x, w, y);", "    return 0;", "}", ""]
        with open(os.path.join(PROGRAMS, "matmul.ci"), "rb") as f:
            source = f.read() + "\n".join(lines).encode()
        reference_path()
        from cint_ref.exec import DEFAULT_DEPTH, run_program
        from cint_ref.faults import RUN_TIME, encode_fault_record
        o = run_program(source, "matmul.ci", "run", None, DEFAULT_DEPTH)
        self.assertEqual(o.kind, "fault")
        want = cint.Fault.from_record(encode_fault_record(o.record, RUN_TIME, 2))
        same = ("code", "operation", "operands", "operand_types", "exact", "limit", "limit_type", "position",
                "kernel", "work_item", "step")
        self.assertEqual({k: getattr(got, k) for k in same}, {k: getattr(want, k) for k in same})
        self.assertEqual((got.stack, want.stack[0][0]), ((("matmul.ci", 16, 15),), "matmul.ci"))

    @unittest.skipIf(numpy is None, "NumPy is not installed")
    def test_from_numpy(self):
        """NumPy's int8 arrays: borrowed as mapped host memory by a context without a device,
        and copied to the device for a context on it."""
        x, w = (numpy.array(rows, dtype=numpy.int8) for rows in matmul_inputs())
        want = (x.astype(numpy.int64) @ w.astype(numpy.int64)).tolist()
        y = cint.empty((4, 3), "I32")
        load("matmul.ci").context().matmul(x, w, y=y)
        g = gpu()
        y_d = cint.empty((4, 3), "I32", device=g)
        load("matmul.ci").context(device=g).matmul(cint.copy(x, device=g), cint.copy(w, device=g), y=y_d)
        self.assertEqual((y.tolist(), cint.copy(y_d).tolist()), (want, want))


def hot_spot(heat: int = 65536) -> list:
    """SPEC-00 section 4's starting field: `heat` at the center of a 64 by 64 field."""
    rows = [[0] * 64 for _ in range(64)]
    rows[32][32] = heat
    return rows


class Stencil(unittest.TestCase):
    """SPEC-09 section 14's stencil as kernels (programs/stencil.ci): SPEC-00 section 4's
    tour, 240 steps from a hot spot, gives the total and center SPEC-00 states, with the
    fuel of cpu-c17, on gpu-cuda over the context's own storage, and over device arrays
    that an exported function only passes to its dispatches."""

    def test_tour(self):
        cpu = load("stencil.ci", "cpu").context()
        want = cpu.tour()
        self.assertEqual(divmod(want, 1000000), (42319, 175))
        for name, device in (("gpu-cuda", None), ("gpu-cuda, device context", gpu())):
            with self.subTest(backend=name):
                ctx = load("stencil.ci").context(device=device)
                self.assertEqual((ctx.tour(), ctx.fuel_consumed()), (want, cpu.fuel_consumed()))

    def test_field_in_device_memory(self):
        """240 dispatches in one call over device arrays: each dispatch's device staging is
        freed when the next one stages, so a call is not limited to the staging table's 61."""
        cpu = load("stencil.ci", "cpu").context()
        a, b = (cint.copy(rows, elem="I32") for rows in (hot_spot(), hot_spot(0)))
        self.assertEqual(cpu.field(120, a=a, b=b), 42319)
        g = gpu()
        ctx = load("stencil.ci").context(device=g)
        a_d, b_d = (cint.copy(rows, elem="I32", device=g) for rows in (hot_spot(), hot_spot(0)))
        self.assertEqual(ctx.field(120, a=a_d, b=b_d), 42319)
        self.assertEqual(ctx.fuel_consumed(), cpu.fuel_consumed())
        self.assertEqual((cint.copy(a_d).tolist(), cint.copy(b_d).tolist()), (a.tolist(), b.tolist()))
        self.assertEqual(a.tolist()[32][32], 175)


class Loading(unittest.TestCase):
    def test_backend_must_match(self):
        cuda, cpu = load("kernels.ci"), load("kernels.ci", "cpu")
        self.assertEqual((cuda.backend, cpu.backend), ("cuda", "cpu"))
        with self.assertRaisesRegex(cint.LoadError, "built for backend='cuda'"):
            cint.load(cuda.path)
        with self.assertRaisesRegex(cint.LoadError, "built for backend='cpu'"):
            cint.load(cpu.path, backend="cuda")
        with self.assertRaisesRegex(ValueError, "backend=\"cuda\""):
            cpu.context(device=gpu())


class Package:
    """SPEC-03 6.7's GPU half as written, with a CUDA package's arrays; the
    subclasses name the package. It needs the GPU (CINT_TEST_CUDA=device)."""
    pkg = None

    def setUp(self):
        if not DEVICE:
            self.skipTest("%s arrays are on the GPU: set CINT_TEST_CUDA=device" % self.name)

    def test_example_67(self):
        n = 1_000_000
        mod = load("advance.ci", "cpu")
        ctx = mod.context()
        pos, vel = i64s(*range(n)), i64s(*[3] * n)
        nxt = cint.empty((n,), "I64")
        with cint.borrow(pos) as p, cint.borrow(vel) as v:
            ctx.advance(p, v, 16, next=nxt)
        g = cint.device("cuda:0")
        gmod = load("advance.ci")
        self.assertEqual(gmod.revision, mod.revision)              # same revision identity
        gctx = gmod.context(device=g)
        pos_d, vel_d = self.arange(n), self.full(n, 3)
        nxt_d = cint.empty((n,), "I64", device=g)
        with cint.borrow(pos_d) as p, cint.borrow(vel_d) as v:    # DLPack exchange at entry, on gctx's stream
            gctx.advance(p, v, 16, next=nxt_d)
        out_d = self.from_dlpack(nxt_d)                           # read-only DLPack export (P-18)
        self.assertEqual(nxt_d.exports, 1)
        self.assertEqual(int(out_d[10]), 10 + 3 * 16)
        self.assertEqual(self.host_bytes(out_d), bytes(nxt.memoryview()))   # same bytes as the CPU run
        del out_d                                                 # release the export before reusing nxt_d
        self.assertEqual(nxt_d.exports, 0)
        vel_d[7] = 2 ** 62
        with self.assertRaises(cint.OverflowFault) as caught:
            with cint.borrow(pos_d) as p, cint.borrow(vel_d) as v:
                gctx.advance(p, v, 16, next=nxt_d)
        f = caught.exception
        self.assertEqual((f.code, f.operation, f.operands, f.work_item),
                         ("E_OVERFLOW", "mul.checked.i64", (4611686018427387904, 16), 7))
        self.assertEqual(bytes(cint.copy(nxt_d).memoryview()), bytes(nxt.memoryview()))
        gctx.clear_fault()
        with self.assertRaisesRegex(cint.BorrowRefused, "kDLFloat"):
            cint.borrow(self.floats(4))
        with self.assertRaises(cint.UnsupportedFault):           # host memory reaching a device kernel (M-28a)
            gctx.advance(cint.borrow(pos), cint.borrow(vel), 16, next=nxt_d)

    def test_case_18(self):
        """Case 18: the package's CUDA array with a context on the CPU is refused,
        E_UNSUPPORTED, at the borrow with context= and at the entry."""
        ctx = load("advance.ci", "cpu").context()
        a = self.arange(4)
        with self.assertRaises(cint.Refused) as caught:
            cint.borrow(a, context=ctx)
        self.assertEqual(caught.exception.code, "E_UNSUPPORTED")
        with self.assertRaises(cint.Refused) as caught:
            ctx.advance(a, a, 1, next=cint.empty((4,), "I64"))
        self.assertEqual(caught.exception.code, "E_UNSUPPORTED")

    def test_matmul(self):
        """SPEC-09 section 14's matrix multiply with the package's int8 arrays on the GPU: the
        exact product, and the fault that cpu-c17 gives."""
        x, w = matmul_inputs()
        g = cint.device("cuda:0")
        gctx = load("matmul.ci").context(device=g)
        xd, wd = self.int8(x), self.int8(w)
        y = cint.empty((4, 3), "I32", device=g)
        with cint.borrow(xd) as xv, cint.borrow(wd) as wv:
            gctx.matmul(xv, wv, y=y)
        self.assertEqual(cint.copy(y).tolist(), product(x, w))
        with self.assertRaises(cint.OverflowFault) as caught:
            with cint.borrow(xd) as xv, cint.borrow(wd) as wv:
                gctx.matmul_narrow(xv, wv, y=y)
        with self.assertRaises(cint.OverflowFault) as want:
            load("matmul.ci", "cpu").context().matmul_narrow(
                cint.copy(x, elem="I8"), cint.copy(w, elem="I8"), y=cint.empty((4, 3), "I32"))
        self.assertEqual(caught.exception.expect_lines(4), want.exception.expect_lines(4))
        self.assertEqual(cint.copy(y).tolist(), product(x, w))

    def test_open_question_17(self):
        """OQ-47: the package accepts a Buffer's read-only versioned capsule and
        shares its memory; the export ends with the package's array."""
        b = cint.copy([1, 2, 3], elem="I64", device=cint.device("cuda:0"))
        out = self.from_dlpack(b)
        self.assertEqual((b.exports, int(out[2])), (1, 3))
        del out
        self.assertEqual(b.exports, 0)


@unittest.skipIf(cupy is None, "CuPy is not installed")
class CuPy(Package, unittest.TestCase):
    name = "CuPy"

    def arange(self, n):
        return cupy.arange(n, dtype=cupy.int64)

    def full(self, n, value):
        return cupy.full(n, value, dtype=cupy.int64)

    def int8(self, rows):
        return cupy.asarray(rows, dtype=cupy.int8)

    def floats(self, n):
        return cupy.zeros(n, dtype=cupy.float32)

    def from_dlpack(self, x):
        return cupy.from_dlpack(x)

    def host_bytes(self, x) -> bytes:
        return cupy.asnumpy(x).tobytes()


@unittest.skipIf(torch is None, "PyTorch is not installed")
class Torch(Package, unittest.TestCase):
    name = "PyTorch"

    def arange(self, n):
        return torch.arange(n, dtype=torch.int64, device="cuda:0")

    def full(self, n, value):
        return torch.full((n,), value, dtype=torch.int64, device="cuda:0")

    def int8(self, rows):
        return torch.tensor(rows, dtype=torch.int8, device="cuda:0")

    def floats(self, n):
        return torch.zeros(n, dtype=torch.float32, device="cuda:0")

    def from_dlpack(self, x):
        return torch.from_dlpack(x)

    def host_bytes(self, x) -> bytes:
        return x.cpu().numpy().tobytes()


if __name__ == "__main__":
    unittest.main()
