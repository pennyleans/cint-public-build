"""Host memory for the Python bridge: the buffer protocol through `ctypes`, and
bridge-owned storage.

The bridge requests buffer flags itself, through `PyObject_GetBuffer`, and
never borrows through `memoryview(obj)`, which requests `PyBUF_FULL_RO`, a
combination that includes `PyBUF_INDIRECT` (SPEC-03 P-1; BX10-08). From
CPython 3.11, `PyObject_GetBuffer`, `PyBuffer_Release`, and the layout of
`Py_buffer` are in the stable ABI, so the structure declared here stays valid.
"""
from __future__ import annotations

import array
import ctypes
import weakref

PyBUF_WRITABLE = 0x0001
PyBUF_FORMAT = 0x0004
PyBUF_ND = 0x0008
PyBUF_STRIDES = 0x0010 | PyBUF_ND
BORROW_FLAGS = PyBUF_STRIDES | PyBUF_FORMAT       # 0x1c (SPEC-03 P-1); PyBUF_INDIRECT never

# Array type codes by item size; checked here, because a few C sizes vary.
SIGNED_CODES = {1: "b", 2: "h", 4: "i", 8: "q"}
UNSIGNED_CODES = {1: "B", 2: "H", 4: "I", 8: "Q"}
for _size, _code in [*SIGNED_CODES.items(), *UNSIGNED_CODES.items()]:
    if array.array(_code).itemsize != _size:
        raise ImportError("array type code %r is not %d bytes on this host" % (_code, _size))

allocations = 0          # bridge-owned allocations made so far; tests read it to show that nothing was copied


class Py_buffer(ctypes.Structure):
    _fields_ = [("buf", ctypes.c_void_p), ("obj", ctypes.c_void_p), ("len", ctypes.c_ssize_t),
                ("itemsize", ctypes.c_ssize_t), ("readonly", ctypes.c_int), ("ndim", ctypes.c_int),
                ("format", ctypes.c_char_p), ("shape", ctypes.POINTER(ctypes.c_ssize_t)),
                ("strides", ctypes.POINTER(ctypes.c_ssize_t)), ("suboffsets", ctypes.POINTER(ctypes.c_ssize_t)),
                ("internal", ctypes.c_void_p)]


def _api(name, restype, *argtypes):
    """A function of the Python C API called with the GIL held, its error
    indicator raised as the Python exception (`ctypes.PYFUNCTYPE`)."""
    return ctypes.PYFUNCTYPE(restype, *argtypes)((name, ctypes.pythonapi))


_get_buffer = _api("PyObject_GetBuffer", ctypes.c_int, ctypes.py_object, ctypes.POINTER(Py_buffer), ctypes.c_int)
_release_buffer = _api("PyBuffer_Release", None, ctypes.POINTER(Py_buffer))


def _release(view: Py_buffer):
    _release_buffer(ctypes.byref(view))


class HeldBuffer:
    """A `Py_buffer` obtained with `PyObject_GetBuffer` and held, with its
    reference to the exporter, until `release()` or garbage collection
    (SPEC-03 P-2)."""

    def __init__(self, obj, flags: int):
        self.view = Py_buffer()
        _get_buffer(obj, ctypes.byref(self.view), flags)
        self._finalizer = weakref.finalize(self, _release, self.view)
        v = self.view
        self.address = v.buf or 0
        self.nbytes = v.len
        self.itemsize = v.itemsize
        self.readonly = bool(v.readonly)
        self.ndim = v.ndim
        self.format = v.format.decode("ascii", "replace") if v.format is not None else "B"
        if v.shape:
            self.shape = tuple(v.shape[i] for i in range(v.ndim))
        else:
            self.shape = (v.len // v.itemsize,) if v.itemsize else (0,)
        if v.strides:
            self.strides = tuple(v.strides[i] for i in range(v.ndim))
        else:
            self.strides = c_strides(self.shape, v.itemsize)
        self.indirect = bool(v.suboffsets)

    def release(self):
        self._finalizer()

    @property
    def released(self) -> bool:
        return not self._finalizer.alive


def c_strides(shape: tuple, size: int) -> tuple:
    """Row-major compact strides, in units of `size`."""
    out, step = [], size
    for n in reversed(shape):
        out.append(step)
        step *= n
    return tuple(reversed(out))


def element_count(shape: tuple) -> int:
    n = 1
    for extent in shape:
        n *= extent
    return n


def alloc(nbytes: int, align: int):
    """Zeroed bridge-owned storage of `nbytes` bytes, aligned to `align`
    (at most 8, SPEC-03 A-13a): (storage, address). The storage is a `ctypes`
    array of 8-byte words, so its address is 8-byte aligned."""
    global allocations
    words = max(1, (nbytes + 7) // 8)
    storage = (ctypes.c_uint64 * words)()
    address = ctypes.addressof(storage)
    if address % max(align, 8):
        raise MemoryError("bridge storage at %#x is not %d-byte aligned (SPEC-03 A-13a)" % (address, align))
    allocations += 1
    return storage, address


def gather(address: int, itemsize: int, shape: tuple, strides: tuple) -> bytes:
    """The bytes of each element, in row-major order, of the strided array at
    `address` (`strides` in bytes, any sign)."""
    n = element_count(shape)
    if n == 0:
        return b""
    if strides == c_strides(shape, itemsize):
        return ctypes.string_at(address, n * itemsize)
    lo = sum((e - 1) * s for e, s in zip(shape, strides) if s < 0)
    hi = sum((e - 1) * s for e, s in zip(shape, strides) if s > 0)
    span = memoryview(ctypes.string_at(address + lo, hi - lo + itemsize))
    out = bytearray()
    for offset in _offsets(shape, strides):
        at = offset - lo
        out += span[at:at + itemsize]
    return bytes(out)


def _offsets(shape: tuple, strides: tuple):
    """Element offsets in row-major order."""
    if not shape:
        yield 0
        return
    for i in range(shape[0]):
        for rest in _offsets(shape[1:], strides[1:]):
            yield i * strides[0] + rest


def reachable(shape: tuple, strides: tuple):
    """(lo, hi) element offsets reachable from the first element, for strides
    in elements (SPEC-03 A-8a); None for an array with no elements."""
    if element_count(shape) == 0:
        return None
    lo = sum((e - 1) * s for e, s in zip(shape, strides) if s < 0)
    hi = sum((e - 1) * s for e, s in zip(shape, strides) if s > 0)
    return lo, hi


def pack(raws, signed: bool, size: int) -> bytes:
    """Little-endian two's complement bytes of each raw value, `size` bytes
    each; wide values (more than 8 bytes) in limb order (SPEC-01 IM-145)."""
    codes = SIGNED_CODES if signed else UNSIGNED_CODES
    if size in codes:
        return array.array(codes[size], raws).tobytes()
    return b"".join(r.to_bytes(size, "little", signed=signed) for r in raws)


def unpack(data, signed: bool, size: int, swap: bool = False) -> list:
    """The raw values of little-endian (or, with `swap`, big-endian) elements."""
    codes = SIGNED_CODES if signed else UNSIGNED_CODES
    if size in codes:
        a = array.array(codes[size])
        a.frombytes(bytes(data))
        if swap:
            a.byteswap()
        return a.tolist()
    order = "big" if swap else "little"
    return [int.from_bytes(data[i:i + size], order, signed=signed) for i in range(0, len(data), size)]


def nest(values: list, shape: tuple) -> list:
    """Row-major values as nested lists of `shape`."""
    if len(shape) <= 1:
        return list(values)
    step = len(values) // shape[0] if shape[0] else 0
    if step == 0:
        inner = element_count(shape[1:])
        return [nest([], shape[1:]) for _ in range(shape[0])] if inner == 0 else []
    return [nest(values[i:i + step], shape[1:]) for i in range(0, len(values), step)]
