"""Measures how CuPy and PyTorch exchange CUDA arrays through DLPack (box 11 unit 5; OQ-47).

Two directions are measured on one device. As consumers, `cupy.from_dlpack`
and `torch.from_dlpack` are given a producer that exports device memory the
way a `cint.Buffer` does (SPEC-03 P-18): a versioned capsule with
`DLPACK_FLAG_BITMASK_READ_ONLY`, then the same without the flag, then an
unversioned capsule. Each row records the keywords the consumer passed to
`__dlpack__`, whether it accepted the capsule, whether the result shares the
producer's memory, and whether a write through the result was refused or
reached the producer's memory (OQ-47: accept, and honor). As producers, a
CuPy array and a PyTorch tensor are asked for the exchange that the bridge
makes at each entry (SPEC-03 P-10): `__dlpack__(stream=s, max_version=(1,
0), dl_device=None, copy=False)` on a stream of their own, and each row
records the capsule the bridge's consumer (`python/cint/_dlpack.py`) reads:
its name, version, flags, device, type, shape, strides and data address.

The probe stands alone: its DLPack structures are those of
`python/cint/_dlpack.py`, so that it runs from any directory. It needs a CUDA
device and either package; a package that is absent is recorded as absent.
It writes a JSON receipt (`--out`) and prints one line per row.
"""
from __future__ import annotations

import argparse
import ctypes
import gc
import json
import os
import platform
import sys
import traceback
import warnings

kDLCUDA = 2
kDLInt, kDLUInt, kDLFloat, kDLOpaqueHandle, kDLBfloat, kDLComplex, kDLBool = range(7)
CODE_NAMES = {kDLInt: "kDLInt", kDLUInt: "kDLUInt", kDLFloat: "kDLFloat", kDLOpaqueHandle: "kDLOpaqueHandle",
              kDLBfloat: "kDLBfloat", kDLComplex: "kDLComplex", kDLBool: "kDLBool"}
FLAG_READ_ONLY = 1          # DLPACK_FLAG_BITMASK_READ_ONLY
SCHEMA = "cint-dlpack-probe-1"


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


NAME_VERSIONED = ctypes.c_char_p(b"dltensor_versioned")
NAME_LEGACY = ctypes.c_char_p(b"dltensor")
NAME_USED_VERSIONED = ctypes.c_char_p(b"used_dltensor_versioned")
NAME_USED_LEGACY = ctypes.c_char_p(b"used_dltensor")


def _api(name, restype, *argtypes):
    fn = ctypes.PYFUNCTYPE(restype, *argtypes)((name, ctypes.pythonapi))
    return fn


_capsule_new = _api("PyCapsule_New", ctypes.py_object, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p)
_capsule_get_name = _api("PyCapsule_GetName", ctypes.c_char_p, ctypes.py_object)
_capsule_get_pointer = _api("PyCapsule_GetPointer", ctypes.c_void_p, ctypes.py_object, ctypes.c_char_p)
_capsule_set_name = _api("PyCapsule_SetName", ctypes.c_int, ctypes.py_object, ctypes.c_char_p)
_DELETER = ctypes.CFUNCTYPE(None, ctypes.c_void_p)
_CALL_DELETER = ctypes.PYFUNCTYPE(None, ctypes.c_void_p)


def _failure(exc: BaseException) -> dict:
    return {"error": type(exc).__module__ + "." + type(exc).__qualname__, "message": str(exc).splitlines()[0][:300]
            if str(exc) else ""}


def _kwargs(stream, kwargs) -> dict:
    out = {"stream": stream if stream is None or isinstance(stream, int) else repr(stream)}
    for key in sorted(kwargs):
        value = kwargs[key]
        out[key] = list(value) if isinstance(value, tuple) else value if value is None or isinstance(
            value, (bool, int, str)) else repr(value)
    return out


class Export:
    """A DLPack producer over device memory another package allocated, which
    exports as a `cint.Buffer` does: versioned (1, 0) when the consumer asks
    for a version, with `flags`; `versioned=False` exports the unversioned
    form whatever the consumer asks. It records each call and each deleter
    call, and keeps every exported tensor alive until its deleter runs."""

    def __init__(self, address: int, count: int, device_id: int, *, versioned: bool, flags: int):
        self.address, self.count, self.device_id = address, count, device_id
        self.versioned, self.flags = versioned, flags
        self.calls, self.live = [], {}
        self.deleted = 0
        self._deleter = _DELETER(self._delete)

    def __dlpack_device__(self):
        return (kDLCUDA, self.device_id)

    def __dlpack__(self, stream=None, **kwargs):
        self.calls.append(_kwargs(stream, kwargs))
        max_version = kwargs.get("max_version")
        versioned = self.versioned and max_version is not None and tuple(max_version)[0] >= 1
        managed = (DLManagedTensorVersioned if versioned else DLManagedTensor)()
        shape = (ctypes.c_int64 * 1)(self.count)
        t = managed.dl_tensor
        t.data = self.address
        t.device = DLDevice(kDLCUDA, self.device_id)
        t.ndim = 1
        t.dtype = DLDataType(kDLInt, 64, 1)
        t.shape = ctypes.cast(shape, ctypes.POINTER(ctypes.c_int64))
        if versioned:
            managed.version = DLPackVersion(1, 0)
            managed.flags = self.flags
        managed.deleter = ctypes.cast(self._deleter, ctypes.c_void_p).value
        address = ctypes.addressof(managed)
        self.live[address] = (managed, shape)
        name = NAME_VERSIONED if versioned else NAME_LEGACY
        return _capsule_new(address, name, None)

    def _delete(self, address):
        if self.live.pop(address, None) is not None:
            self.deleted += 1


class Package:
    """What the probe needs from CuPy or PyTorch: device arrays, their
    addresses, a stream of their own, and the package's `from_dlpack`."""

    def __init__(self, name: str):
        self.name = name
        if name == "cupy":
            import cupy
            self.mod = cupy
            self.stream_obj = cupy.cuda.Stream(non_blocking=True)
            self.stream = int(self.stream_obj.ptr)
        else:
            import torch
            self.mod = torch
            self.stream_obj = torch.cuda.Stream(device=0)
            self.stream = int(self.stream_obj.cuda_stream)

    def versions(self) -> dict:
        m = self.mod
        if self.name == "cupy":
            props = m.cuda.runtime.getDeviceProperties(0)
            device = props["name"].decode() if isinstance(props["name"], bytes) else str(props["name"])
            return {"cupy": m.__version__, "cuda_runtime": m.cuda.runtime.runtimeGetVersion(),
                    "cuda_driver": m.cuda.runtime.driverGetVersion(), "device": device}
        return {"torch": m.__version__, "torch_cuda": m.version.cuda, "available": bool(m.cuda.is_available()),
                "device": m.cuda.get_device_name(0)}

    def arange(self, n: int):
        if self.name == "cupy":
            return self.mod.arange(n, dtype=self.mod.int64)
        return self.mod.arange(n, dtype=self.mod.int64, device="cuda:0")

    def zeros(self, n: int, dtype: str):
        if self.name == "cupy":
            return self.mod.zeros(n, dtype=getattr(self.mod, dtype))
        return self.mod.zeros(n, dtype=getattr(self.mod, dtype), device="cuda:0")

    def address(self, x) -> int:
        return int(x.data.ptr) if self.name == "cupy" else int(x.data_ptr())

    def item(self, x, i: int) -> int:
        self.sync()
        return int(x[i].item())

    def sync(self):
        if self.name == "cupy":
            self.mod.cuda.Device(0).synchronize()
        else:
            self.mod.cuda.synchronize()

    def from_dlpack(self, obj):
        return self.mod.from_dlpack(obj)


FORMS = (("versioned read-only", True, FLAG_READ_ONLY), ("versioned writable", True, 0),
         ("unversioned", False, 0))


def consume(p: Package) -> list:
    """`from_dlpack` over the three forms of export (OQ-47)."""
    rows = []
    for form, versioned, flags in FORMS:
        base = p.arange(8)
        p.sync()
        source = Export(p.address(base), 8, 0, versioned=versioned, flags=flags)
        row = {"direction": "consumer", "package": p.name, "form": form}
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            try:
                y = p.from_dlpack(source)
            except Exception as exc:     # a refusal is a result
                row.update(accepted=False, **_failure(exc))
                y = None
            if y is not None:
                row.update(accepted=True, result_type=type(y).__module__ + "." + type(y).__qualname__)
                try:
                    row.update(shares_memory=p.address(y) == p.address(base), value=p.item(y, 3))
                    try:
                        y[3] = 99
                        p.sync()
                        row["write"] = "allowed"
                    except Exception as exc:
                        row["write"] = "refused"
                        row["write_failure"] = _failure(exc)
                    row["source_after_write"] = p.item(base, 3)
                except Exception as exc:
                    row["inspect_failure"] = _failure(exc)
            row["warnings"] = [str(w.message).splitlines()[0][:300] for w in caught]
        del y
        gc.collect()
        p.sync()
        row.update(calls=source.calls, exported=len(source.calls), deleted=source.deleted,
                   live_after_release=len(source.live))
        if form == "versioned read-only" and row["accepted"]:
            honored = row.get("write") == "refused" or row.get("shares_memory") is False
            row["oq47"] = ("accepts and honors read-only" if honored and row.get("source_after_write") == 3
                           else "accepts and does not honor read-only")
        elif form == "versioned read-only":
            row["oq47"] = "refuses read-only"
        rows.append(row)
    return rows


def _exchange(x, stream, **kwargs) -> dict:
    """One exchange, read as the bridge's consumer reads it (consumed by
    renaming the capsule, then the deleter called once)."""
    capsule = x.__dlpack__(stream=stream, **kwargs)
    name = _capsule_get_name(capsule)
    if name not in (NAME_VERSIONED.value, NAME_LEGACY.value):
        raise ValueError("the capsule is named %r" % (name,))
    versioned = name == NAME_VERSIONED.value
    address = _capsule_get_pointer(capsule, NAME_VERSIONED if versioned else NAME_LEGACY)
    _capsule_set_name(capsule, NAME_USED_VERSIONED if versioned else NAME_USED_LEGACY)
    managed = (DLManagedTensorVersioned if versioned else DLManagedTensor).from_address(address)
    try:
        t = managed.dl_tensor
        shape = [t.shape[i] for i in range(t.ndim)] if t.shape and 0 <= t.ndim <= 8 else None
        strides = [t.strides[i] for i in range(t.ndim)] if t.strides and 0 <= t.ndim <= 8 else None
        return {"capsule": name.decode(), "version": [managed.version.major, managed.version.minor]
                if versioned else None, "flags": managed.flags if versioned else None,
                "device": [t.device.device_type, t.device.device_id],
                "dtype": [CODE_NAMES.get(t.dtype.code, t.dtype.code), t.dtype.bits, t.dtype.lanes],
                "shape": shape, "strides": strides, "byte_offset": t.byte_offset, "data": t.data or 0}
    finally:
        if managed.deleter:
            _CALL_DELETER(managed.deleter)(address)


ENTRY = {"max_version": (1, 0), "dl_device": None, "copy": False}
DTYPES = ("bool", "int8", "uint8", "int16", "uint16", "int32", "uint32", "int64", "uint64", "float32")


def produce(p: Package) -> list:
    """The entry exchange of SPEC-03 P-10 on CuPy and PyTorch arrays."""
    rows = []
    x = p.arange(8)
    p.sync()
    address = p.address(x)
    cases = [("entry exchange on a stream of the package's own", p.stream, ENTRY),
             ("second entry exchange", p.stream, ENTRY),
             ("entry exchange on the legacy default stream", 1, ENTRY),
             ("entry exchange with dl_device (kDLCUDA, 0)", p.stream, dict(ENTRY, dl_device=(kDLCUDA, 0))),
             ("unversioned call (stream only)", p.stream, {})]
    row = {"direction": "producer", "package": p.name, "case": "__dlpack_device__"}
    try:
        row["device"] = list(x.__dlpack_device__())
    except Exception as exc:
        row.update(_failure(exc))
    rows.append(row)
    for case, stream, kwargs in cases:
        row = {"direction": "producer", "package": p.name, "case": case, "kwargs": _kwargs(stream, kwargs)}
        try:
            got = _exchange(x, stream, **kwargs)
            got["same_data"] = got.pop("data") == address
            row.update(accepted=True, **got)
        except Exception as exc:
            row.update(accepted=False, **_failure(exc))
        rows.append(row)
    for name in DTYPES:
        row = {"direction": "producer", "package": p.name, "case": "dtype " + name}
        try:
            z = p.zeros(4, name)
            got = _exchange(z, p.stream, **ENTRY)
            row.update(accepted=True, dtype=got["dtype"], flags=got["flags"])
        except Exception as exc:
            row.update(accepted=False, **_failure(exc))
        rows.append(row)
    row = {"direction": "producer", "package": p.name, "case": "every second element"}
    try:
        got = _exchange(x[::2], p.stream, **ENTRY)
        row.update(accepted=True, shape=got["shape"], strides=got["strides"], byte_offset=got["byte_offset"])
    except Exception as exc:
        row.update(accepted=False, **_failure(exc))
    rows.append(row)
    return rows


def _line(row: dict) -> str:
    keep = {k: v for k, v in row.items() if k not in ("direction", "package", "form", "case")}
    what = row.get("form") or row.get("case")
    return "%s %s %s: %s" % (row["direction"], row["package"], what, json.dumps(keep, sort_keys=True))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", help="the JSON receipt to write")
    args = ap.parse_args(argv)
    receipt = {"schema": SCHEMA, "python": platform.python_version(), "platform": platform.platform(),
               "packages": {}, "rows": []}
    try:
        import numpy
        receipt["packages"]["numpy"] = {"numpy": numpy.__version__}
    except ImportError:
        receipt["packages"]["numpy"] = None
    for name in ("cupy", "torch"):
        try:
            p = Package(name)
            receipt["packages"][name] = p.versions()
        except ImportError as exc:
            receipt["packages"][name] = {"absent": str(exc)}
            print("%s: absent (%s)" % (name, exc))
            continue
        except Exception as exc:
            receipt["packages"][name] = {"unusable": _failure(exc), "trace": traceback.format_exc().splitlines()}
            print("%s: unusable: %s" % (name, _failure(exc)))
            continue
        print("%s: %s" % (name, json.dumps(receipt["packages"][name], sort_keys=True)))
        for step in (consume, produce):
            try:
                rows = step(p)
            except Exception as exc:
                rows = [{"direction": step.__name__, "package": name, "case": "aborted", **_failure(exc),
                         "trace": traceback.format_exc().splitlines()}]
            receipt["rows"] += rows
            for row in rows:
                print(_line(row))
    verdicts = {r["package"]: r["oq47"] for r in receipt["rows"] if "oq47" in r}
    receipt["oq47"] = verdicts
    for name, verdict in sorted(verdicts.items()):
        print("OQ-47 %s: %s" % (name, verdict))
    if args.out:
        os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
        with open(args.out, "w", encoding="utf-8", newline="\n") as f:
            json.dump(receipt, f, indent=1, sort_keys=True)
            f.write("\n")
    return 0 if verdicts else 1


if __name__ == "__main__":
    sys.exit(main())
