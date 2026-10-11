"""Devices for the Python bridge: `cint.device` and device memory (SPEC-03 6.1, A-16a, P-11; box 11, BX11-16).

`cint.device("cuda:0")` opens CUDA device 0 through the driver library, as
the runtime does (`nvcuda.dll` on Windows, `libcuda.so.1` elsewhere), so the
bridge needs no CUDA toolkit. The bridge uses the driver for the memory of
device `Buffer`s: it allocates, fills, and reads that memory on a stream of
its own in the device's primary context, which is the context the runtime's
CUDA contexts use (A-16a). Each operation returns after its stream has
finished it, so what the bridge wrote is ready before any entry, and what an
entry wrote is ready when the entry returns, since the runtime synchronizes
its stream before an entry returns (A-16a). A device stays open, with its
primary context retained, for the life of the process.

`CINT_CUDA_DRIVER` names a stand-in driver library for tests
(`rt/tests/cuda_fake.c`): the bridge opens it in place of the system driver,
and a `backend="cuda"` build compiles the runtime with `CINT_CUDA_TESTING`,
under which the runtime opens the same library.
"""
from __future__ import annotations

import contextlib
import ctypes
import os
import threading
import weakref

from . import _dlpack

DRIVER_VARIABLE = "CINT_CUDA_DRIVER"
STREAM_NON_BLOCKING = 1                 # CU_STREAM_NON_BLOCKING
CUDA_ERROR_OUT_OF_MEMORY = 2
LATER_KINDS = ("rocm", "vulkan", "metal", "webgpu")
_lock = threading.Lock()
_devices = {}
_drivers = {}


def stand_in() -> str | None:
    """The stand-in driver library that `CINT_CUDA_DRIVER` names, or None."""
    return os.environ.get(DRIVER_VARIABLE) or None


def driver_path() -> str:
    """The driver library a CUDA device is opened through."""
    return stand_in() or ("nvcuda.dll" if os.name == "nt" else "libcuda.so.1")


class _Driver:
    """The entry points of the driver library that the bridge calls. `ctypes`
    releases the GIL during each call."""

    def __init__(self, path: str):
        try:
            lib = ctypes.CDLL(path)
        except OSError as failure:
            raise OSError("the CUDA driver library %s could not be loaded (%s): a CUDA device needs the NVIDIA "
                          "driver (SPEC-03 A-16a)" % (path, failure)) from None
        i, u, p, n, u64 = ctypes.c_int, ctypes.c_uint, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint64
        pp = ctypes.POINTER(p)
        table = {"cuInit": (u,), "cuDeviceGet": (ctypes.POINTER(i), i), "cuDeviceGetName": (ctypes.c_char_p, i, i),
                 "cuDevicePrimaryCtxRetain": (pp, i), "cuCtxPushCurrent_v2": (p,), "cuCtxPopCurrent_v2": (pp,),
                 "cuStreamCreate": (pp, u), "cuStreamSynchronize": (p,), "cuMemAlloc_v2": (ctypes.POINTER(u64), n),
                 "cuMemFree_v2": (u64,), "cuMemcpyHtoDAsync_v2": (u64, p, n, p),
                 "cuMemcpyDtoHAsync_v2": (p, u64, n, p), "cuMemsetD32Async": (u64, u, n, p),
                 "cuGetErrorName": (i, ctypes.POINTER(ctypes.c_char_p))}
        self.path = path
        for name, argtypes in table.items():
            try:
                fn = getattr(lib, name)
            except AttributeError:
                raise OSError("the CUDA driver library %s has no entry point %s" % (path, name)) from None
            fn.restype = ctypes.c_int
            fn.argtypes = list(argtypes)
            setattr(self, name, fn)

    def error_name(self, code: int) -> str:
        name = ctypes.c_char_p()
        if self.cuGetErrorName(code, ctypes.byref(name)) == 0 and name.value:
            return name.value.decode("ascii", "replace")
        return "CUresult %d" % code

    def check(self, code: int, what: str):
        if code == 0:
            return
        message = "%s failed with %s (%s)" % (what, self.error_name(code), self.path)
        if code == CUDA_ERROR_OUT_OF_MEMORY:
            raise MemoryError(message)
        raise OSError(message)


def _driver() -> _Driver:
    path = driver_path()
    d = _drivers.get(path)
    if d is None:
        d = _drivers[path] = _Driver(path)
    return d


class _Gpu:
    """One CUDA device opened: its primary context, retained, and a stream of
    the bridge's own. Every call holds the lock with the context current."""

    def __init__(self, driver: _Driver, ordinal: int):
        self.driver = driver
        self.lock = threading.Lock()
        d = driver
        d.check(d.cuInit(0), "cuInit")
        handle = ctypes.c_int()
        d.check(d.cuDeviceGet(ctypes.byref(handle), ordinal), "cuDeviceGet(%d)" % ordinal)
        name = ctypes.create_string_buffer(256)
        d.check(d.cuDeviceGetName(name, len(name), handle.value), "cuDeviceGetName")
        self.name = name.value.decode("utf-8", "replace")
        context = ctypes.c_void_p()
        d.check(d.cuDevicePrimaryCtxRetain(ctypes.byref(context), handle.value), "cuDevicePrimaryCtxRetain")
        self.context = context.value
        stream = ctypes.c_void_p()
        with self._current():
            d.check(d.cuStreamCreate(ctypes.byref(stream), STREAM_NON_BLOCKING), "cuStreamCreate")
        self.stream = stream.value

    @contextlib.contextmanager
    def _current(self):
        with self.lock:
            self.driver.check(self.driver.cuCtxPushCurrent_v2(self.context), "cuCtxPushCurrent")
            try:
                yield
            finally:
                popped = ctypes.c_void_p()
                self.driver.cuCtxPopCurrent_v2(ctypes.byref(popped))

    def _run(self, name: str, *args):
        """One asynchronous call on the bridge's stream, then the stream's end."""
        d = self.driver
        with self._current():
            d.check(getattr(d, name)(*args, self.stream), name)
            d.check(d.cuStreamSynchronize(self.stream), "cuStreamSynchronize")

    def alloc(self, nbytes: int) -> int:
        address = ctypes.c_uint64()
        with self._current():
            self.driver.check(self.driver.cuMemAlloc_v2(ctypes.byref(address), nbytes), "cuMemAlloc of %d bytes"
                              % nbytes)
        return address.value

    def free(self, address: int):
        with self._current():
            self.driver.cuMemFree_v2(address)

    def upload(self, address: int, data: bytes):
        if data:
            self._run("cuMemcpyHtoDAsync_v2", address, data, len(data))

    def download(self, address: int, nbytes: int) -> bytes:
        out = ctypes.create_string_buffer(max(1, nbytes))
        if nbytes:
            self._run("cuMemcpyDtoHAsync_v2", out, address, nbytes)
        return out.raw[:nbytes]

    def zero(self, address: int, nbytes: int):
        """Zero `nbytes` bytes, a multiple of 4, at `address`."""
        if nbytes:
            self._run("cuMemsetD32Async", address, 0, nbytes // 4)


class Device:
    """A device that a context runs on and that device `Buffer`s live in
    (SPEC-03 6.1): `kind` is "cpu" or "cuda", `index` its ordinal, and
    `str(d)` its spec, such as "cuda:0". Made by `cint.device(spec)`, which
    gives one object per device (and per driver library)."""

    def __init__(self, *args, **kwargs):
        raise TypeError("a Device is made by cint.device(spec), for example cint.device(\"cuda:0\")")

    @classmethod
    def _make(cls, kind: str, index: int, gpu: _Gpu | None) -> "Device":
        d = object.__new__(cls)
        d.kind = kind
        d.index = index
        d._gpu = gpu
        d._record = None
        if gpu is not None:
            from ._abi import CintCudaDevice
            d._record = CintCudaDevice(size=ctypes.sizeof(CintCudaDevice), ordinal=index)
        return d

    @property
    def name(self) -> str:
        """The driver's name for the device, or "cpu"."""
        return self._gpu.name if self._gpu is not None else "cpu"

    @property
    def is_host(self) -> bool:
        return self.kind == "cpu"

    def _pair(self) -> tuple:
        """The DLPack device of this device: (device type, ordinal)."""
        return (_dlpack.kDLCUDA, self.index) if self.kind == "cuda" else (_dlpack.kDLCPU, 0)

    def __str__(self) -> str:
        return self.kind if self.kind == "cpu" else "%s:%d" % (self.kind, self.index)

    def __repr__(self) -> str:
        return "<cint.Device %s%s>" % (self, "" if self._gpu is None else " (%s)" % self._gpu.name)

    def __reduce__(self):
        raise TypeError("a Device is not pickled; cint.device(%r) opens it again" % str(self))


def device(spec) -> Device:
    """The device `spec` names: "cpu", or "cuda:<ordinal>", opened at once
    through the driver library; an `OSError` names the library when it cannot
    be loaded, and the device when the driver refuses it (SPEC-03 6.1, A-16a)."""
    if isinstance(spec, Device):
        return spec
    if not isinstance(spec, str):
        raise TypeError("a device spec is a str such as \"cuda:0\" or \"cpu\", not %s" % type(spec).__name__)
    kind, sep, index = spec.partition(":")
    if kind == "cpu" and (not sep or index == "0"):
        key = ("cpu", 0)
    elif kind in LATER_KINDS:
        raise NotImplementedError("device %r: this bridge runs on the CPU and on CUDA; the %s backend is not built"
                                  % (spec, kind))
    elif kind == "cuda" and sep and index.isascii() and index.isdigit() and len(index) <= 4:
        key = ("cuda", int(index), driver_path())
    else:
        raise ValueError("a device spec is \"cpu\" or \"cuda:<ordinal>\", such as \"cuda:0\", not %r" % spec)
    with _lock:
        d = _devices.get(key)
        if d is None:
            gpu = _Gpu(_driver(), key[1]) if key[0] == "cuda" else None
            d = _devices[key] = Device._make(key[0], key[1], gpu)
    return d


def is_host(spec) -> bool:
    """Whether a `device=` argument names host memory, without opening anything."""
    if isinstance(spec, Device):
        return spec.is_host
    return spec is None or spec in ("cpu", "cpu:0")


def resolve(spec) -> Device | None:
    """A `device=` argument as a CUDA `Device`, or None for host memory
    (None, "cpu", or the CPU device)."""
    if spec is None:
        return None
    d = device(spec)
    return None if d.is_host else d


class Memory:
    """Device memory the bridge allocated: at least `nbytes` bytes at `address`,
    whole 8-byte words, freed when the last holder lets it go (a `Buffer`, or a
    DLPack export of one)."""

    def __init__(self, dev: Device, nbytes: int):
        words = max(1, (nbytes + 7) // 8)
        self.device = dev
        self.nbytes = nbytes
        self.address = dev._gpu.alloc(8 * words)
        self._words = words
        weakref.finalize(self, dev._gpu.free, self.address)

    def zero(self):
        self.device._gpu.zero(self.address, 8 * self._words)

    def upload(self, data: bytes):
        self.device._gpu.upload(self.address, data)

    def download(self) -> bytes:
        return self.device._gpu.download(self.address, self.nbytes)


def gather(dev: Device, address: int, itemsize: int, shape: tuple, strides: tuple) -> bytes:
    """The bytes of each element, in row-major order, of the strided array at
    device `address` (`strides` in bytes): its reachable span read back to the
    host, then gathered there."""
    from ._memory import _offsets, element_count
    n = element_count(shape)
    if n == 0:
        return b""
    lo = sum((e - 1) * s for e, s in zip(shape, strides) if s < 0)
    hi = sum((e - 1) * s for e, s in zip(shape, strides) if s > 0)
    span = dev._gpu.download(address + lo, hi - lo + itemsize)
    return b"".join(span[o - lo:o - lo + itemsize] for o in _offsets(shape, strides))
