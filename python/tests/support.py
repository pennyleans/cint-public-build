"""Shared helpers: optional NumPy and PyTorch, bit-pattern floats, and two
DLPack producers, one over a NumPy array and one over ctypes storage."""
import ctypes
from pathlib import Path
import struct
import sys

try:
    import numpy
except ImportError:
    numpy = None
try:
    import torch
except ImportError:
    torch = None

from cint import _dlpack, _memory

ROOT = Path(__file__).resolve().parents[2]


def reference_path():
    """Put `ref/` and `conformance/tools/` on the path, after everything else."""
    for sub in ("ref", "conformance/tools"):
        path = str(ROOT / sub)
        if path not in sys.path:
            sys.path.append(path)


def allocations() -> int:
    """Bridge-owned allocations so far: unchanged means nothing was copied."""
    return _memory.allocations


def f64(bits: int) -> float:
    """The binary64 value of a bit pattern."""
    return struct.unpack("<d", bits.to_bytes(8, "little"))[0]


def bits64(x: float) -> int:
    return int.from_bytes(struct.pack("<d", x), "little")


def address_of(obj) -> int:
    """The data address of a writable buffer, through ctypes."""
    return ctypes.addressof((ctypes.c_char * len(memoryview(obj).cast("B"))).from_buffer(obj))


class DLPackOnly:
    """An object with `__dlpack__` and `__dlpack_device__` and no buffer
    protocol, over a NumPy array, so that `cint.borrow` takes the DLPack path."""

    def __init__(self, array):
        self.array = array

    def __dlpack__(self, **kwargs):
        return self.array.__dlpack__(**kwargs)

    def __dlpack_device__(self):
        return self.array.__dlpack_device__()


_capsule_new = ctypes.PYFUNCTYPE(ctypes.py_object, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p)(
    ("PyCapsule_New", ctypes.pythonapi))
_NAMES = {True: ctypes.c_char_p(b"dltensor_versioned"), False: ctypes.c_char_p(b"dltensor")}
_DELETER = ctypes.CFUNCTYPE(None, ctypes.c_void_p)


class Producer:
    """A DLPack producer over ctypes storage. Its capsules have no destructor,
    so each tensor is freed only by the deleter the consumer calls; the
    producer counts its exports and the deleter calls (SPEC-03 P-14)."""

    def __init__(self, values, *, ctype=ctypes.c_int64, dtype=(_dlpack.kDLInt, 64, 1), shape=None, strides=None,
                 versioned=True, legacy_signature=False, flags=0, version=(1, 0), device=(_dlpack.kDLCPU, 0),
                 byte_offset=0, refuse_copy_false=False):
        self.storage = (ctype * max(1, len(values)))(*values)
        self.shape = tuple(shape) if shape is not None else (len(values),)
        self.strides = strides
        self.dtype = dtype
        self.versioned = versioned
        self.legacy_signature = legacy_signature
        self.flags = flags
        self.version = version
        self.device = device
        self.byte_offset = byte_offset
        self.refuse_copy_false = refuse_copy_false
        self.shift = 0                 # bytes added to the data pointer of later exports
        self.calls = []
        self.exported = 0
        self.deleted = 0
        self.unknown_deletes = 0
        self.live = {}
        self._deleter = _DELETER(self._delete)

    @property
    def address(self) -> int:
        return ctypes.addressof(self.storage)

    def __dlpack_device__(self):
        return self.device

    def __dlpack__(self, stream=None, **kwargs):
        if self.legacy_signature and kwargs:
            raise TypeError("__dlpack__() got an unexpected keyword argument %r" % next(iter(kwargs)))
        self.calls.append(dict(kwargs, stream=stream))
        if self.refuse_copy_false and kwargs.get("copy") is False:
            raise BufferError("this producer cannot export without a copy")
        versioned = self.versioned and kwargs.get("max_version") is not None
        managed = (_dlpack.DLManagedTensorVersioned if versioned else _dlpack.DLManagedTensor)()
        shape = (ctypes.c_int64 * max(1, len(self.shape)))(*self.shape)
        strides = None if self.strides is None else (ctypes.c_int64 * max(1, len(self.strides)))(*self.strides)
        t = managed.dl_tensor
        t.data = self.address + self.shift
        t.device = _dlpack.DLDevice(*self.device)
        t.ndim = len(self.shape)
        t.dtype = _dlpack.DLDataType(*self.dtype)
        t.shape = ctypes.cast(shape, ctypes.POINTER(ctypes.c_int64))
        if strides is not None:
            t.strides = ctypes.cast(strides, ctypes.POINTER(ctypes.c_int64))
        t.byte_offset = self.byte_offset
        if versioned:
            managed.version = _dlpack.DLPackVersion(*self.version)
            managed.flags = self.flags
        managed.deleter = ctypes.cast(self._deleter, ctypes.c_void_p).value
        address = ctypes.addressof(managed)
        self.live[address] = (managed, shape, strides)
        self.exported += 1
        return _capsule_new(address, _NAMES[versioned], None)

    def _delete(self, address):
        if self.live.pop(address, None) is None:
            self.unknown_deletes += 1
        else:
            self.deleted += 1
