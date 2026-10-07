"""Bridge-owned storage: `Buffer`, `cint.copy`, `cint.empty`, and `cint.from_float`
(SPEC-03 6.1, P-3a, P-17, P-18; BX10-16).

A `Buffer` holds its storage, aligned as SPEC-03 A-13a requires, until
`Buffer.release()` or garbage collection. Storage never passes from the
bridge to a context or back. `cint.copy` always copies, exactly: integers
keep their values, byte order is converted (P-3a), and a refused element
leaves no `Buffer` behind (X-8). Its exports are read-only and counted (P-18).
"""
from __future__ import annotations

import ctypes
from dataclasses import dataclass
import functools
import numbers
import weakref

from . import _dlpack, _floats, _memory
from ._elem import Elem, parse_elem
from ._errors import Busy, Refused, StaleHandle, Unpublished
from ._fixed import Fixed
from ._floats import FloatPolicy, is_float_like
from ._memory import BORROW_FLAGS, SIGNED_CODES, UNSIGNED_CODES, HeldBuffer, element_count
from ._scalar import _numpy_kind, _range, from_raw, to_raw
from ._view import MAX_RANK, Region, View, _values, classify_dtype, classify_format, region

M64 = (1 << 63) - 1


class Buffer:
    """Bridge-owned storage (SPEC-03 6.1, P-17). A `Buffer` from `cint.copy` or
    `cint.from_float` holds values; one from `cint.empty` is unpublished until
    an entry publishes it, and reading it before then raises
    `cint.Unpublished`. Its exports (`memoryview()`, NumPy) are read-only and
    counted; using it as an output while one is live raises `cint.Busy`
    (P-18). Use it as a context manager to bound its storage."""

    def __init__(self, *args, **kwargs):
        raise TypeError("a Buffer is made by cint.copy, cint.empty, or cint.from_float")

    @classmethod
    def _make(cls, elem: Elem, shape: tuple, raws, *, published: bool = True, conversion=None) -> "Buffer":
        n = element_count(shape)
        nbytes = n * elem.size
        if nbytes > M64:
            raise Refused("%d elements of %s need more than M64 bytes (SPEC-03 A-8)" % (n, elem))
        storage, address = _memory.alloc(nbytes, elem.align)
        if raws:
            data = _memory.pack(raws, elem.signed, elem.size)
            ctypes.memmove(address, data, len(data))
        b = object.__new__(cls)
        b._elem = elem
        b.shape = tuple(shape)
        b._storage = storage
        b._address = address
        b._nbytes = nbytes
        b._published = published
        b._exports = 0
        b._released = False
        b._registrations = {}
        b.conversion = conversion
        return b

    @property
    def elem(self) -> str:
        return self._elem.name

    @property
    def ndim(self) -> int:
        return len(self.shape)

    @property
    def size(self) -> int:
        """The number of elements."""
        return element_count(self.shape)

    @property
    def strides(self) -> tuple:
        """Row-major element strides."""
        return _memory.c_strides(self.shape, 1)

    @property
    def published(self) -> bool:
        return self._published

    @property
    def exports(self) -> int:
        """The number of live exports of this Buffer (SPEC-03 P-18)."""
        return self._exports

    @property
    def released(self) -> bool:
        return self._released

    @property
    def region(self) -> Region:
        """The registration this storage gets on first use with a context (SPEC-03 P-20)."""
        self._check_live()
        return region(self._address, self._elem, self.shape, self.strides, True)

    def _check_live(self):
        if self._released:
            raise StaleHandle("this Buffer was released (SPEC-03 6.1)")

    def _check_readable(self):
        self._check_live()
        if not self._published:
            raise Unpublished("this Buffer has never been published: an entry writes it as an output first "
                              "(SPEC-03 P-17)")

    def _raw_values(self) -> list:
        self._check_readable()
        return _memory.unpack(ctypes.string_at(self._address, self._nbytes), self._elem.signed, self._elem.size)

    def _claim_output(self):
        """The check before this Buffer is bound as an output (SPEC-03 P-18)."""
        self._check_live()
        if self._exports:
            raise Busy("this Buffer has %d live export%s, and an output with live exports is refused (SPEC-03 "
                       "P-18): release them first, for example with del" % (self._exports,
                                                                           "s" if self._exports > 1 else ""),
                       code="E_ALIAS")

    def _mark_published(self):
        self._published = True

    def tolist(self) -> list:
        """The values as nested lists: `int`, `cint.Fixed`, or `bool`."""
        return _memory.nest([from_raw(r, self._elem) for r in self._raw_values()], self.shape)

    def memoryview(self) -> memoryview:
        """A read-only zero-copy export, counted until every memoryview and
        array made from it is gone (SPEC-03 P-18). Elements wider than 8 bytes
        have no buffer format: `tolist()` gives their exact values."""
        self._check_readable()
        e = self._elem
        if e.wide:
            raise TypeError("%s elements have no buffer format; tolist() gives their exact values" % e)
        code = "?" if e.kind == "bool" else (SIGNED_CODES if e.signed else UNSIGNED_CODES)[e.size]
        token = _token_type(self._nbytes).from_buffer(self._storage)
        self._exports += 1
        weakref.finalize(token, _export_ended, weakref.ref(self))
        mv = memoryview(token).cast("B")
        mv = mv.cast(code, self.shape) if self.size else mv.cast(code)
        return mv.toreadonly()

    def __array__(self, dtype=None, copy=None):
        """The NumPy view of the export, for Python 3.11, where a class has no
        buffer protocol; from 3.12 NumPy reads `__buffer__` first. A `dtype`
        other than the element's is NumPy's conversion of the raw storage, as
        for any integer array: for fixed point it converts raw values, and
        `cint.to_float` gives the values (SPEC-03 X-10)."""
        import numpy
        arr = numpy.asarray(self.memoryview()).reshape(self.shape)
        if dtype is not None and numpy.dtype(dtype) != arr.dtype:
            if copy is False:
                raise ValueError("a %s Buffer exports dtype %s; dtype %s needs a copy"
                                 % (self._elem, arr.dtype, numpy.dtype(dtype)))
            return arr.astype(dtype)
        return arr.copy() if copy else arr

    def __buffer__(self, flags: int) -> memoryview:
        if flags & _memory.PyBUF_WRITABLE:
            raise BufferError("a Buffer has no writable export (SPEC-03 P-18)")
        return self.memoryview()

    def __release_buffer__(self, view: memoryview):
        try:
            view.release()
        except BufferError:
            pass

    def release(self):
        """End this Buffer: each registration of it ends first, then the bridge
        drops its storage, which live exports keep valid until they end
        (BX10-16). Idempotent."""
        if self._released:
            return
        for r in list(self._registrations.values()):
            r.end()
        self._registrations.clear()
        self._released = True
        self._storage = None

    def __enter__(self) -> "Buffer":
        self._check_live()
        return self

    def __exit__(self, *exc):
        self.release()
        return False

    def __reduce__(self):
        raise TypeError("a Buffer is bridge-owned storage and is not pickled; tolist() gives its values")

    def __repr__(self) -> str:
        state = "released" if self._released else "published" if self._published else "unpublished"
        return "<cint.Buffer %s%s %s>" % (self.elem, list(self.shape), state)


@functools.lru_cache(maxsize=256)
def _token_type(nbytes: int):
    """A `ctypes` array type that can be weakly referenced: one instance per
    export, alive as long as the export's memoryviews are."""
    return type("_ExportToken", (ctypes.c_uint8 * nbytes,), {})


def _export_ended(ref):
    b = ref()
    if b is not None:
        b._exports -= 1


def _check_device(device):
    if device is None or device == "cpu":
        return
    raise NotImplementedError("device=%r: this bridge works in host memory only; there is no implicit transfer "
                              "(SPEC-03 P-11)" % (device,))


def _shape(shape) -> tuple:
    if isinstance(shape, numbers.Integral) and not isinstance(shape, bool):
        shape = (shape,)
    if not isinstance(shape, (tuple, list)) or any(isinstance(n, bool) or not isinstance(n, numbers.Integral)
                                                   for n in shape):
        raise TypeError("a shape is a tuple of extents, such as (4,) or (2, 3)")
    shape = tuple(int(n) for n in shape)
    if any(n < 0 for n in shape):
        raise ValueError("negative extent in shape %s" % (shape,))
    if not 1 <= len(shape) <= MAX_RANK:
        raise Refused("rank %d: a Buffer has rank 1 to %d (SPEC-03 P-6)" % (len(shape), MAX_RANK))
    return shape


def empty(shape, elem: str, *, device=None) -> Buffer:
    """An unpublished bridge-owned buffer of `shape` (extents, in elements)
    for use as a kernel output (SPEC-03 6.1)."""
    _check_device(device)
    return Buffer._make(parse_elem(elem), _shape(shape), None, published=False)


@dataclass
class _Source:
    kind: str                 # "raw" (raws of elem), "values" (Python leaves), or "float" (bit patterns)
    shape: tuple
    elem: Elem | None = None
    raws: list | None = None
    leaves: list | None = None
    bits: list | None = None
    desc: str = ""


def _walk(obj, depth: int = 1):
    """(shape, leaves) of nested lists, tuples, and ranges, all or nothing."""
    if depth > MAX_RANK:
        raise Refused("nesting deeper than CINT_MAX_RANK, %d (SPEC-03 P-6)" % MAX_RANK)
    items = list(obj)
    nested = [isinstance(x, (list, tuple, range)) for x in items]
    if not items or not any(nested):
        return (len(items),), items
    if not all(nested):
        raise Refused("a sequence mixes nested sequences and values, so it has no shape", code="E_SHAPE")
    parts = [_walk(x, depth + 1) for x in items]
    inner = parts[0][0]
    if any(p[0] != inner for p in parts):
        raise Refused("a ragged sequence has no shape: its rows have shapes %s"
                      % sorted({p[0] for p in parts}), code="E_SHAPE")
    return (len(items),) + inner, [v for p in parts for v in p[1]]


def _refuse_rank0(obj):
    if is_float_like(obj):
        return Refused("a single %s is rank 0; cint.from_float converts arrays (SPEC-03 P-6, X-1)" % type(obj).__name__)
    return Refused("%s is rank 0: a Buffer has rank 1 to %d, and a scalar argument is passed as itself "
                   "(SPEC-03 P-6)" % (type(obj).__name__, MAX_RANK))


def _read(obj, floats: bool) -> _Source:
    """The elements of a copy source, in row-major order."""
    if isinstance(obj, View):
        return _Source("raw", obj.shape, obj._elem, raws=obj._raw_values(), desc="a %s View" % obj.elem)
    if isinstance(obj, Buffer):
        return _Source("raw", obj.shape, obj._elem, raws=obj._raw_values(), desc="a %s Buffer" % obj.elem)
    if isinstance(obj, str):
        raise Refused("a str is not copied; text crosses as bytes, for example s.encode(\"utf-8\") (BX10-15)")
    if isinstance(obj, (list, tuple, range)):
        shape, leaves = _walk(obj)
        if not floats:
            return _Source("values", shape, leaves=leaves, desc="a %s" % type(obj).__name__)
        bits = [_floats.leaf_bits(x) for x in leaves]
        bad = [i for i, b in enumerate(bits) if b is None]
        if bad:
            first = _floats.unravel(bad[0], shape)
            raise Refused("element %s is %s, not a float; cint.from_float converts floats, and cint.copy(seq, "
                          "elem=...) copies integers; %d element%s refused (SPEC-03 X-8)"
                          % (first, type(leaves[bad[0]]).__name__, len(bad), "s" if len(bad) > 1 else ""),
                          index=first, count=len(bad))
        return _Source("float", shape, bits=bits, desc="a %s of floats" % type(obj).__name__)
    if isinstance(obj, (numbers.Number, Fixed)) or _numpy_kind(obj) is not None:
        raise _refuse_rank0(obj)
    device = getattr(obj, "__dlpack_device__", None)
    if device is not None:
        kind, index = (int(x) for x in device())
        if kind != _dlpack.kDLCPU:
            raise Refused("the array is on %s:%d; this bridge copies from host memory only, with no implicit "
                          "transfer (SPEC-03 P-11)" % (_dlpack.DEVICE_NAMES.get(kind, "device type %d" % kind), index))
    try:
        held = HeldBuffer(obj, BORROW_FLAGS)
    except TypeError:
        held = None
    except (BufferError, ValueError) as failure:
        raise Refused("the exporter refused a strided buffer (%s)" % failure) from None
    if held is not None:
        return _read_buffer(held)
    if getattr(obj, "__dlpack__", None) is not None:
        return _read_dlpack(obj)
    raise Refused("%s has neither the buffer protocol nor __dlpack__, and is not a sequence of values"
                  % type(obj).__name__)


def _read_buffer(held: HeldBuffer) -> _Source:
    try:
        if held.indirect:
            raise Refused("the export has suboffsets (PyBUF_INDIRECT), which the bridge does not read")
        if held.ndim == 0:
            raise Refused("rank 0: a Buffer has rank 1 to %d (SPEC-03 P-6)" % MAX_RANK)
        if held.ndim > MAX_RANK:
            raise Refused("rank %d is above CINT_MAX_RANK, %d (SPEC-03 P-6)" % (held.ndim, MAX_RANK))
        f = classify_format(held.format, held.itemsize, Refused, copying=True)
        data = _memory.gather(held.address, held.itemsize, held.shape, held.strides)
    finally:
        held.release()
    return _decoded(f, held.shape, held.itemsize, data, "format %r" % held.format)


def _read_dlpack(obj) -> _Source:
    try:
        capsule = obj.__dlpack__(stream=None, max_version=(1, 0))
    except TypeError:
        capsule = obj.__dlpack__(stream=None)
    try:
        t = _dlpack.Tensor(capsule)
    except ValueError as failure:
        raise Refused(str(failure)) from None
    try:
        if t.versioned and t.version[0] != _dlpack.MAJOR_VERSION:
            raise Refused("DLPack version %d.%d: this bridge reads major version 1" % t.version)
        if t.device[0] != _dlpack.kDLCPU:
            raise Refused("the DLPack tensor is on %s:%d; this bridge copies from host memory only (SPEC-03 P-11)"
                          % (_dlpack.DEVICE_NAMES.get(t.device[0], "device type %d" % t.device[0]), t.device[1]))
        if t.shape is None or not 1 <= t.ndim <= MAX_RANK or any(n < 0 for n in t.shape):
            raise Refused("DLPack rank %d: a Buffer has rank 1 to %d (SPEC-03 P-6)" % (t.ndim, MAX_RANK))
        f = classify_dtype(t.dtype, Refused, copying=True)
        size = f.elem.size if f.elem is not None else max(1, t.dtype[1] // 8)
        strides = t.strides if t.strides is not None else _memory.c_strides(t.shape, 1)
        data = b"" if f.kind == "float" and not f.width else _memory.gather(
            t.data + t.byte_offset, size, t.shape, tuple(s * size for s in strides))
        shape = t.shape
    finally:
        t.delete()
    return _decoded(f, shape, size, data, "DLPack data type %s" % (t.dtype,))


def _decoded(f, shape: tuple, size: int, data: bytes, desc: str) -> _Source:
    if f.kind == "float":
        if not f.width:
            return _Source("float", shape, bits=None, desc=f.name)
        values = _memory.unpack(data, False, size, swap=f.swap)
        return _Source("float", shape, bits=[(b, f.width) for b in values], desc=f.name)
    e = f.elem
    return _Source("raw", shape, e, raws=_memory.unpack(data, e.signed, e.size, swap=f.swap),
                   desc="%s data (%s)" % (e, desc))


def _all_or_nothing(results, shape: tuple, total: int):
    """Raise the first refusal of a bulk conversion, naming its element and the
    number refused (SPEC-03 X-8)."""
    first, count = None, 0
    for i, r in results:
        count += 1
        if first is None:
            first = (i, r)
    if first is None:
        return
    i, r = first
    index = _floats.unravel(i, shape)
    raise Refused("element %s: %s; %d of %d elements refused, so no Buffer was made (SPEC-03 X-8)"
                  % (index, r.detail, count, total), code=r.code, operation=r.operation, operands=r.operands,
                  operand_types=r.operand_types, exact=r.exact, limit=r.limit, limit_type=r.limit_type,
                  reason=r.reason, index=index, count=count)


def _leaf_raws(leaves: list, t: Elem, shape: tuple) -> list:
    raws, refused = [], []
    for i, x in enumerate(leaves):
        try:
            np = _numpy_kind(x)
            if np is not None and np[0] in ("i", "u") and t.kind in ("int", "t1", "t27"):
                raws.append(_range(t, int(x), "the value"))
            else:
                raws.append(to_raw(x, t, "the value"))
        except Refused as r:
            refused.append((i, r))
    _all_or_nothing(refused, shape, len(leaves))
    return raws


def _convert_raws(raws: list, s: Elem, t: Elem, shape: tuple) -> list:
    """Source elements of type `s` as raws of `t`, exactly, or a refusal."""
    if t == s or (t.kind == "fixed" and s.kind == "int" and t.storage == s.tag):
        _values(t, raws, Refused, shape)
        return raws
    if s.kind == "int" and t.kind in ("int", "t1", "t27"):
        refused = []
        for i, r in enumerate(raws):
            if not t.lo <= r <= t.hi:
                try:
                    _range(t, r, "the value")
                except Refused as failure:
                    refused.append((i, failure))
        _all_or_nothing(refused, shape, len(raws))
        return raws
    if t.kind == "fixed" and s.kind == "int":
        raise Refused("elem=%r over %s data has two readings, so it is refused (SPEC-03 P-4, P-21a): the raw "
                      "storage values need %s data, and the values need cint.Fixed(%r, value=v) elements"
                      % (t.name, s, "I%d" % (8 * t.size), t.name))
    if "bool" in (s.kind, t.kind):
        raise Refused("%s data does not copy as %s: booleans and integers never convert (SPEC-03 P-3, BX10-15)"
                      % (s, t))
    raise Refused("%s data copies as %s only; the bridge converts nothing else implicitly" % (s, s))


def copy(obj, *, elem: str | None = None, device=None, from_float: FloatPolicy | None = None) -> Buffer:
    """Copy `obj` into a new bridge-owned `Buffer` (SPEC-03 6.1).

    `obj` is any object with the buffer protocol or CPU DLPack, in any layout
    and byte order (P-3a); a `View` or `Buffer`; or nested lists, tuples, or
    ranges of values, which need `elem`. Values convert exactly: an integer
    `elem` takes integers in its range; a fixed-point `elem` takes the raw
    values of its storage type, or `cint.Fixed` elements. Floating-point data
    needs `from_float`, a `cint.FloatPolicy` (SPEC-03 section 8). All or
    nothing: a refused element leaves no `Buffer` (X-8)."""
    _check_device(device)
    target = parse_elem(elem) if elem is not None else None
    if from_float is not None:
        if not isinstance(from_float, FloatPolicy):
            raise TypeError("from_float takes a cint.FloatPolicy, not %s" % type(from_float).__name__)
        if target is None:
            raise TypeError("a float conversion needs elem=, its target type (SPEC-03 X-1)")
        t = _floats.target(target)
        src = _read(obj, floats=True)
        if src.kind != "float":
            raise Refused("from_float converts floating-point data, and this is %s: cint.copy(a, elem=%r) "
                          "copies integers exactly" % (src.desc, t.name))
        if src.bits is None:
            raise Refused("%s input is not accepted: only binary32 and binary64 are (SPEC-03 X-2)" % src.desc)
        raws, conversion = _floats.convert_many(src.bits, src.shape, t, from_float)
        return Buffer._make(t, src.shape, raws, conversion=conversion)
    src = _read(obj, floats=False)
    if src.kind == "float":
        raise Refused("%s is floating-point data, which enters only through an explicit conversion (SPEC-03 X-1): "
                      "cint.from_float(a, \"Q32.32\", rounding=\"half_even\")" % src.desc)
    if src.kind == "values":
        if target is None:
            raise TypeError("%s has no element type: pass elem=, for example cint.copy(seq, elem=\"I64\")" % src.desc)
        return Buffer._make(target, src.shape, _leaf_raws(src.leaves, target, src.shape))
    t = target or src.elem
    return Buffer._make(t, src.shape, _convert_raws(src.raws, src.elem, t, src.shape))


def from_float(obj, elem: str, *, rounding: str = "half_even", overflow: str = "refuse", recorder=None) -> Buffer:
    """`cint.copy(obj, elem=elem, from_float=cint.FloatPolicy(rounding, overflow))`
    (SPEC-03 6.1, X-1 to X-8). The policy is checked before any element is read."""
    if recorder is not None:
        raise NotImplementedError("recorder=: the conversion audit record (SPEC-03 X-9) needs recordings, which "
                                  "this bridge does not have yet")
    return copy(obj, elem=elem, from_float=FloatPolicy(rounding, overflow))
