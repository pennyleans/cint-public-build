"""DLPack on the CPU, as a consumer (SPEC-03 P-10 to P-14).

The structures follow `dlpack.h` version 1 (`DLManagedTensorVersioned`) and
the earlier unversioned `DLManagedTensor`; the capsule protocol follows the
DLPack Python specification. The bridge consumes every capsule it receives,
accepted or refused: it renames the capsule to `used_dltensor_versioned` (or
`used_dltensor`) and calls the deleter itself, at once for a refused tensor
and otherwise when it no longer needs the tensor (P-14).
"""
from __future__ import annotations

import ctypes

from ._memory import _api

kDLCPU = 1
DEVICE_NAMES = {1: "kDLCPU", 2: "kDLCUDA", 3: "kDLCUDAHost", 4: "kDLOpenCL", 7: "kDLVulkan", 8: "kDLMetal",
                9: "kDLVPI", 10: "kDLROCM", 11: "kDLROCMHost", 12: "kDLExtDev", 13: "kDLCUDAManaged",
                14: "kDLOneAPI", 15: "kDLWebGPU", 16: "kDLHexagon", 17: "kDLMAIA"}
kDLInt, kDLUInt, kDLFloat, kDLOpaqueHandle, kDLBfloat, kDLComplex, kDLBool = range(7)
CODE_NAMES = {kDLInt: "kDLInt", kDLUInt: "kDLUInt", kDLFloat: "kDLFloat", kDLOpaqueHandle: "kDLOpaqueHandle",
              kDLBfloat: "kDLBfloat", kDLComplex: "kDLComplex", kDLBool: "kDLBool"}
FLAG_READ_ONLY = 1          # DLPACK_FLAG_BITMASK_READ_ONLY
FLAG_IS_COPIED = 2          # DLPACK_FLAG_BITMASK_IS_COPIED
MAJOR_VERSION = 1


class DLDevice(ctypes.Structure):
    _fields_ = [("device_type", ctypes.c_int32), ("device_id", ctypes.c_int32)]


class DLDataType(ctypes.Structure):
    _fields_ = [("code", ctypes.c_uint8), ("bits", ctypes.c_uint8), ("lanes", ctypes.c_uint16)]


class DLTensor(ctypes.Structure):
    _fields_ = [("data", ctypes.c_void_p), ("device", DLDevice), ("ndim", ctypes.c_int32),
                ("dtype", DLDataType), ("shape", ctypes.POINTER(ctypes.c_int64)),
                ("strides", ctypes.POINTER(ctypes.c_int64)), ("byte_offset", ctypes.c_uint64)]


class DLManagedTensor(ctypes.Structure):
    _fields_ = [("dl_tensor", DLTensor), ("manager_ctx", ctypes.c_void_p), ("deleter", ctypes.c_void_p)]


class DLPackVersion(ctypes.Structure):
    _fields_ = [("major", ctypes.c_uint32), ("minor", ctypes.c_uint32)]


class DLManagedTensorVersioned(ctypes.Structure):
    _fields_ = [("version", DLPackVersion), ("manager_ctx", ctypes.c_void_p), ("deleter", ctypes.c_void_p),
                ("flags", ctypes.c_uint64), ("dl_tensor", DLTensor)]


# Capsule names live as long as the process: PyCapsule_New and PyCapsule_SetName keep the pointer.
NAME_VERSIONED = ctypes.c_char_p(b"dltensor_versioned")
NAME_LEGACY = ctypes.c_char_p(b"dltensor")
NAME_USED_VERSIONED = ctypes.c_char_p(b"used_dltensor_versioned")
NAME_USED_LEGACY = ctypes.c_char_p(b"used_dltensor")

_capsule_get_name = _api("PyCapsule_GetName", ctypes.c_char_p, ctypes.py_object)
_capsule_get_pointer = _api("PyCapsule_GetPointer", ctypes.c_void_p, ctypes.py_object, ctypes.c_char_p)
_capsule_set_name = _api("PyCapsule_SetName", ctypes.c_int, ctypes.py_object, ctypes.c_char_p)
_DELETER = ctypes.PYFUNCTYPE(None, ctypes.c_void_p)


class Tensor:
    """A DLPack tensor as the consumer reads it: one capsule, consumed when
    read, its managed tensor, and the fields of its `DLTensor`."""

    def __init__(self, capsule):
        name = _capsule_get_name(capsule)
        if name == NAME_VERSIONED.value:
            self.versioned = True
            self.address = _capsule_get_pointer(capsule, NAME_VERSIONED)
            managed = DLManagedTensorVersioned.from_address(self.address)
        elif name == NAME_LEGACY.value:
            self.versioned = False
            self.address = _capsule_get_pointer(capsule, NAME_LEGACY)
            managed = DLManagedTensor.from_address(self.address)
        else:
            raise ValueError("a DLPack capsule is named dltensor_versioned or dltensor, not %r (it may have "
                             "been consumed already)" % (name,))
        _capsule_set_name(capsule, NAME_USED_VERSIONED if self.versioned else NAME_USED_LEGACY)
        self.managed = managed
        self.version = (managed.version.major, managed.version.minor) if self.versioned else None
        self.flags = managed.flags if self.versioned else 0
        t = managed.dl_tensor
        self.data = t.data or 0
        self.device = (t.device.device_type, t.device.device_id)
        self.ndim = t.ndim
        self.dtype = (t.dtype.code, t.dtype.bits, t.dtype.lanes)
        self.shape = self.strides = None
        if 0 <= t.ndim <= 64 and (t.shape or not t.ndim):
            self.shape = tuple(t.shape[i] for i in range(t.ndim))
            if t.strides and t.ndim:
                self.strides = tuple(t.strides[i] for i in range(t.ndim))
        self.byte_offset = t.byte_offset

    @property
    def deleted(self) -> bool:
        return self.managed is None

    def delete(self):
        """Call the deleter, once (SPEC-03 P-14). The tensor is not read after."""
        managed, self.managed = self.managed, None
        if managed is not None and managed.deleter:
            _DELETER(managed.deleter)(self.address)
