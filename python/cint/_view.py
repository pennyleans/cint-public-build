"""`cint.borrow` and `View`: host memory used without a copy (SPEC-03 P-1 to P-14, A-8a).

A borrow never copies, under any argument combination (P-9). Every condition
it cannot meet raises `BorrowRefused`, naming the condition and the
alternative, which is usually `cint.copy`. The `View` holds the `Py_buffer`
or the DLPack tensor, and with it a reference to the exporter, until it is
released (P-2, P-14).
"""
from __future__ import annotations

from contextlib import contextmanager
from dataclasses import dataclass
import weakref

from . import _dlpack, _memory
from ._elem import Elem, integer_elem, parse_elem
from ._errors import BorrowRefused, Refused, StaleHandle
from ._memory import BORROW_FLAGS, PyBUF_WRITABLE, HeldBuffer, c_strides, element_count
from ._scalar import from_raw

MAX_RANK = 4                       # CINT_MAX_RANK (SPEC-02 V-2; SPEC-03 open question 4)
LAYOUTS = ("any", "C", "F")
_SIGNED_CHARS = "bhilqn"
_UNSIGNED_CHARS = "BHILQN"
_FLOAT_NAMES = {"e": "float16", "f": "float32", "d": "float64", "g": "long double"}
_DL_FLOAT_CODES = {_dlpack.kDLFloat, _dlpack.kDLBfloat, _dlpack.kDLComplex}


@dataclass(frozen=True)
class Format:
    """A classified buffer or DLPack element format: `kind` is "int", "bool",
    or "float"; `elem` the mapped element (None for floats); `width` the
    float width in bits; `swap` True for big-endian storage."""
    kind: str
    elem: Elem | None
    width: int = 0
    swap: bool = False
    name: str = ""


def classify_format(fmt: str, itemsize: int, refusal=BorrowRefused, copying: bool = False) -> Format:
    """The element a buffer format maps to (SPEC-03 P-3): by the signedness of
    the format character and the reported item size, never the character's
    nominal C type. `copying` admits big-endian storage (P-3a) and floats,
    which only an explicit float conversion reads."""
    body, prefix = fmt, ""
    if body[:1] in ("@", "=", "<", ">", "!"):
        prefix, body = body[0], body[1:]
    big = prefix in (">", "!")
    if len(body) == 1 and body in _SIGNED_CHARS + _UNSIGNED_CHARS:
        if itemsize not in (1, 2, 4, 8):
            raise refusal("element format %r with item size %d maps to no integer element (SPEC-03 P-3)"
                          % (fmt, itemsize))
        f = Format("int", integer_elem(body in _SIGNED_CHARS, itemsize), swap=big)
    elif body == "?":
        if itemsize != 1:
            raise refusal("element format '?' with item size %d: Bool elements are one byte (BX10-15)" % itemsize)
        f = Format("bool", parse_elem("Bool"), swap=big)
    elif body == "c":
        raise refusal("element format 'c' (char) is not borrowed or copied: use format 'B', or pass elem=\"U8\" "
                      "on a uint8 export (SPEC-03 P-3)")
    elif body in _FLOAT_NAMES or body[:1] == "Z":
        what = _FLOAT_NAMES.get(body, "complex")
        if copying:
            ok = body in ("f", "d") and itemsize in (4, 8)
            return Format("float", None, 8 * itemsize if ok else 0, big, "format %r (%s)" % (fmt, what))
        raise refusal("element format %r (%s) cannot be borrowed.\nCINT has no floating-point element type. "
                      "Convert explicitly:\n  cint.from_float(a, \"Q32.32\", rounding=\"half_even\")" % (fmt, what))
    else:
        raise refusal("element format %r is not an integer element (SPEC-03 P-3): struct, string, pointer, and "
                      "object formats are refused" % (fmt,))
    if big and not copying:
        raise refusal("element format %r is big-endian, and cint.borrow never converts byte order (SPEC-03 P-3): "
                      "cint.copy(a) converts it exactly while copying (P-3a)" % (fmt,))
    return f


def classify_dtype(dtype: tuple, refusal=BorrowRefused, copying: bool = False) -> Format:
    """The element a DLPack data type maps to (SPEC-03 P-12): `kDLInt` and
    `kDLUInt` of 8 to 64 bits, and `kDLBool` of 8 bits, which is borrowed as
    `Bool` as the `?` format is (BX10-15); `lanes` 1."""
    code, bits, lanes = dtype
    name = _dlpack.CODE_NAMES.get(code, "a low-precision floating-point code (%d)" % code if 7 <= code <= 17
                                  else "an unknown code (%d)" % code)
    if lanes != 1:
        raise refusal("DLPack data type %s with %d lanes: only lanes 1 maps to an element (SPEC-03 P-12)"
                      % (name, lanes))
    if code in (_dlpack.kDLInt, _dlpack.kDLUInt) and bits in (8, 16, 32, 64):
        return Format("int", integer_elem(code == _dlpack.kDLInt, bits // 8))
    if code == _dlpack.kDLBool and bits == 8:
        return Format("bool", parse_elem("Bool"))
    if copying and (code in _DL_FLOAT_CODES or 7 <= code <= 17):
        ok = code == _dlpack.kDLFloat and bits in (32, 64)
        return Format("float", None, bits if ok else 0, name="DLPack %s, %d bits" % (name, bits))
    if code in _DL_FLOAT_CODES or 7 <= code <= 17:
        raise refusal("DLPack data type %s, %d bits, cannot be borrowed.\nCINT has no floating-point element "
                      "type. Convert explicitly:\n  cint.from_float(a, \"Q32.32\", rounding=\"half_even\")"
                      % (name, bits))
    raise refusal("DLPack data type %s, %d bits, maps to no element (SPEC-03 P-12)" % (name, bits))


def check_elem(mapped: Elem, want: Elem | None, refusal=BorrowRefused) -> Elem:
    """The element of a borrow (SPEC-03 P-4): the mapped element, or an `elem`
    equal to it, or a fixed-point `elem` over it as storage; also `T1` over
    `I8` and `T27` over `I64` (SPEC-05 TR-HOST-1). The bridge never converts."""
    if want is None or want == mapped:
        return mapped
    if want.kind == "fixed" and mapped.kind == "int" and want.storage == mapped.tag:
        return want
    if (want.kind, mapped.name) in (("t1", "I8"), ("t27", "I64")):
        return want
    if mapped.kind == "bool":
        raise refusal("elem=%r over Bool data: booleans are borrowed only as Bool, never as integers "
                      "(SPEC-03 P-3, BX10-15)" % want.name)
    if want.kind == "bool":
        raise refusal("elem=\"Bool\" over %s data: only format '?' or kDLBool is borrowed as Bool (BX10-15)"
                      % mapped)
    hint = ""
    if want.kind == "fixed":
        hint = "; a fixed-point elem needs storage of its width, %s" % ("I%d" % (8 * want.size))
    raise refusal("elem=%r disagrees with the %s elements of this export, and the bridge never converts (SPEC-03 "
                  "P-4)%s; cint.copy(a, elem=%r) converts exactly while copying" % (want.name, mapped, hint, want.name))


@dataclass(frozen=True)
class Region:
    """What a registration of this memory records (SPEC-03 A-8, A-8a): the
    lowest reachable byte, the reachable span in elements (`hi - lo + 1`, 0
    when there are no elements), and the element offset of the first logical
    element from that base, with the view's shape and element strides."""
    address: int
    extent: int
    origin: int
    shape: tuple
    strides: tuple
    elem: str
    writable: bool


def region(address: int, e: Elem, shape: tuple, strides: tuple, writable: bool) -> Region:
    reach = _memory.reachable(shape, strides)
    if reach is None:
        return Region(address, 0, 0, shape, strides, e.name, writable)
    lo, hi = reach
    return Region(address + lo * e.size, hi - lo + 1, -lo, shape, strides, e.name, writable)


def _contiguous(shape: tuple, strides: tuple, order: str) -> bool:
    """Compact in row-major (`C`) or column-major (`F`) order; axes of extent
    1 may have any stride, and an array with no elements is contiguous."""
    if element_count(shape) == 0:
        return True
    dims = list(zip(shape, strides))
    if order == "F":
        dims.reverse()
    step = 1
    for n, s in reversed(dims):
        if n != 1 and s != step:
            return False
        step *= n
    return True


def _values(e: Elem, raws: list, refusal=BorrowRefused, shape: tuple = ()):
    """Check the stored values of `T1`, `T27`, and `Bool` elements, which bytes
    from outside the type system may violate (SPEC-05 TR-VAL-2, TR-VAL-3;
    SPEC-03 A-13). Refuses naming the first invalid element in row-major order
    and the number of invalid elements."""
    if e.kind not in ("t1", "t27", "bool"):
        return
    bad = [i for i, r in enumerate(raws) if not e.lo <= r <= e.hi]
    if not bad:
        return
    from ._floats import unravel
    index = unravel(bad[0], shape) if shape else (bad[0],)
    raw = raws[bad[0]]
    if e.kind == "bool":
        raise refusal("element %s holds the byte %d; a Bool element is 0 or 1 (SPEC-03 A-13); %d invalid "
                      "element%s" % (index, raw, len(bad), "s" if len(bad) > 1 else ""), code="E_NARROW",
                      operands=(raw,), operand_types=("U8",), index=index, count=len(bad))
    carrier = "I8" if e.kind == "t1" else "I64"
    byte = (" (byte %02x)" % (raw & 0xFF)) if e.kind == "t1" else ""
    raise refusal("element %s holds %d%s, not a %s value (SPEC-05 TR-VAL-2); %d invalid element%s"
                  % (index, raw, byte, e, len(bad), "s" if len(bad) > 1 else ""), code="E_NARROW",
                  operation="decode.checked.%s" % e.token, operands=(raw if e.kind == "t27" else raw & 0xFF,),
                  operand_types=(carrier if e.kind == "t27" else "U8",), reason="unused value", index=index,
                  count=len(bad))


class View:
    """A lease over host memory, borrowed without a copy (SPEC-03 6.1). It is
    read-only unless borrowed with `writable=True`. Use it as a context
    manager to bound the lease; after `release()` every use raises
    `cint.StaleHandle`. Made by `cint.borrow`."""

    def __init__(self, *args, **kwargs):
        raise TypeError("a View is made by cint.borrow(obj)")

    @classmethod
    def _make(cls, *, elem: Elem, shape: tuple, strides: tuple, address: int, writable: bool, publish,
              protocol: str, held: HeldBuffer | None = None, tensor=None, exporter=None, notes: tuple = (),
              exchange: dict | None = None) -> "View":
        v = object.__new__(cls)
        v._elem = elem
        v.shape = shape
        v.strides = strides
        v._address = address
        v.writable = writable
        v.publish = publish
        v.protocol = protocol
        v.notes = notes
        v._held = held
        v._tensor = tensor
        v._exporter = exporter
        v._exchange_args = exchange
        v._registrations = {}
        v._finalizer = weakref.finalize(v, _end_lease, held, tensor)
        return v

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
    def released(self) -> bool:
        return not self._finalizer.alive

    @property
    def region(self) -> Region:
        """The registration this view's memory gets on first use with a
        context (SPEC-03 P-20, A-8a)."""
        self._check_live()
        return region(self._address, self._elem, self.shape, self.strides, self.writable)

    def release(self):
        """End the lease: release the `Py_buffer`, or call the DLPack deleter
        (SPEC-03 P-2, P-14). Each registration of the view's memory ends first
        (P-20). Idempotent."""
        for r in list(self._registrations.values()):
            r.end()
        self._registrations.clear()
        self._finalizer()
        self._exporter = None

    def __enter__(self) -> "View":
        self._check_live()
        return self

    def __exit__(self, *exc):
        self.release()
        return False

    def _check_live(self):
        if self.released:
            raise StaleHandle("this View was released; borrow the array again (SPEC-03 6.1)")

    def _raw_values(self) -> list:
        """The raw stored values, in row-major order."""
        self._check_live()
        e = self._elem
        data = _memory.gather(self._address, e.size, self.shape, tuple(s * e.size for s in self.strides))
        return _memory.unpack(data, e.signed, e.size)

    def _check_values(self):
        """The value check that runs when an entry binds this view: `T1` and
        `T27` carriers (SPEC-05 TR-VAL-3) and `Bool` bytes (SPEC-03 A-13). A
        refusal: no fuel, no fault, and no state change."""
        _values(self._elem, self._raw_values(), BorrowRefused, self.shape)

    def tolist(self) -> list:
        """The current values as nested lists: `int`, `cint.Fixed`, or `bool`."""
        raws = self._raw_values()
        _values(self._elem, raws, BorrowRefused, self.shape)
        return _memory.nest([from_raw(r, self._elem) for r in raws], self.shape)

    @contextmanager
    def _entry_exchange(self):
        """The DLPack exchange of one entry (SPEC-03 P-10): the producer is
        asked again, and the data pointer, shape, strides, and data type must
        equal those of the first exchange. The capsule of the entry is released
        when the entry ends (P-14). A buffer-protocol view needs no exchange."""
        self._check_live()
        if self._tensor is None:
            yield None
            return
        t = _dlpack.Tensor(_call_dlpack(self._exporter, self._exchange_args))
        try:
            first = self._tensor
            same = (t.data + t.byte_offset == first.data + first.byte_offset and t.shape == first.shape
                    and _element_strides(t) == _element_strides(first) and t.dtype == first.dtype
                    and t.device == first.device)
            if not same:
                raise Refused("the DLPack producer exported a different pointer, shape, strides, or data type "
                              "than at the borrow (SPEC-03 P-10); borrow the array again")
            yield t
        finally:
            t.delete()

    def __repr__(self) -> str:
        state = "released" if self.released else ("writable" if self.writable else "read-only")
        return "<cint.View %s%s %s, %s via %s>" % (self.elem, list(self.shape), state,
                                                   "publish=copy" if self.publish else "no publish",
                                                   self.protocol)


def _end_lease(held, tensor):
    if held is not None:
        held.release()
    if tensor is not None:
        tensor.delete()


def _element_strides(t) -> tuple:
    return t.strides if t.strides is not None else c_strides(t.shape, 1)


def _call_dlpack(obj, args: dict):
    return obj.__dlpack__(**args)


def _refuse_object(obj, refusal=BorrowRefused):
    """The refusal for an object with neither the buffer protocol nor DLPack."""
    if isinstance(obj, str):
        return refusal("a str has no elements to borrow; text crosses as bytes, for example s.encode(\"utf-8\") "
                       "(BX10-15)", reason="protocol")
    if isinstance(obj, (list, tuple, range)):
        return refusal("a %s has neither the buffer protocol nor __dlpack__, and cint.borrow never copies "
                       "(SPEC-03 P-1, P-9): cint.copy(seq, elem=\"I64\") copies it exactly" % type(obj).__name__,
                       reason="protocol")
    return refusal("%s has neither the buffer protocol nor __dlpack__ (SPEC-03 P-1): cint.borrow takes host "
                   "memory such as a NumPy array, bytes, or an array.array" % type(obj).__name__, reason="protocol")


def borrow(obj, *, elem: str | None = None, writable: bool = False, publish: str | None = None,
           layout: str = "any", context=None) -> View:
    """Borrow host memory without copying it (SPEC-03 P-1 to P-14).

    An object with the buffer protocol is borrowed through it, with
    `PyBUF_STRIDES | PyBUF_FORMAT` (plus `PyBUF_WRITABLE` when `writable`);
    an object without it but with `__dlpack__` on the CPU is borrowed through
    DLPack. `elem` may name the mapped element, or a fixed-point type over it
    (P-4). `publish="copy"` (with `writable=True`) lets the view be a kernel
    output, staged and copied on success (P-15). `layout` is "any", "C", or
    "F" (P-8). `context=` takes a `Context` and makes the DLPack and device
    checks at the borrow rather than at the first entry (P-10, P-11): this
    bridge borrows host memory only, so they are the checks made here."""
    from ._buffer import Buffer
    if context is not None:
        from ._module import Context
        if not isinstance(context, Context):
            raise TypeError("context= takes a Context from Module.context(), not %s" % type(context).__name__)
        context._check_open()
    if publish not in (None, "copy"):
        raise ValueError("publish is None or \"copy\", not %r" % (publish,))
    if publish == "copy" and not writable:
        raise BorrowRefused("publish=\"copy\" binds the view as an output, which needs writable=True (SPEC-03 P-15)")
    if layout not in LAYOUTS:
        raise ValueError("layout is \"any\", \"C\", or \"F\", not %r" % (layout,))
    if writable not in (True, False):
        raise TypeError("writable is True or False")
    want = parse_elem(elem) if elem is not None else None
    if isinstance(obj, (View, Buffer)):
        raise BorrowRefused("this is already a cint.%s: pass it to the entry itself" % type(obj).__name__)
    device = getattr(obj, "__dlpack_device__", None)
    if device is not None:
        kind, index = (int(x) for x in device())
        if kind != _dlpack.kDLCPU:
            raise BorrowRefused("the array is on %s:%d, and cint.borrow takes CPU memory only (SPEC-03 P-1, P-11); "
                                "there is no implicit transfer, so copy it to host memory first"
                                % (_dlpack.DEVICE_NAMES.get(kind, "device type %d" % kind), index))
    held = _get_buffer(obj, writable)
    if held is not None:
        return _from_buffer(held, want, writable, publish, layout)
    if getattr(obj, "__dlpack__", None) is None:
        raise _refuse_object(obj)
    return _from_dlpack(obj, want, writable, publish, layout)


def _get_buffer(obj, writable: bool):
    """The `Py_buffer` of `obj`, or None when it has no buffer protocol.
    Refuses `writable=True` over a read-only export (SPEC-03 P-7)."""
    flags = BORROW_FLAGS | (PyBUF_WRITABLE if writable else 0)
    try:
        return HeldBuffer(obj, flags)
    except TypeError:
        return None
    except (BufferError, ValueError) as first:
        if not writable:
            raise BorrowRefused("the exporter refused PyBUF_STRIDES | PyBUF_FORMAT (%s); an export that needs "
                                "PyBUF_INDIRECT is never borrowed (SPEC-03 P-1): cint.copy(a) copies it" % first)
    try:
        probe = HeldBuffer(obj, BORROW_FLAGS)
    except (TypeError, BufferError, ValueError) as second:
        raise BorrowRefused("the exporter refused a writable strided buffer (%s); cint.copy(a) copies it" % second)
    readonly = probe.readonly
    probe.release()
    if readonly:
        raise BorrowRefused("writable=True over a read-only export (SPEC-03 P-7): borrow it read-only, or make a "
                            "writable copy with cint.copy(a)")
    raise BorrowRefused("the exporter refused PyBUF_WRITABLE although its export is writable (SPEC-03 P-1)")


def _from_buffer(held: HeldBuffer, want, writable: bool, publish, layout: str) -> View:
    try:
        if held.indirect:
            raise BorrowRefused("the export has suboffsets (PyBUF_INDIRECT), which are never borrowed (SPEC-03 P-1)")
        if held.ndim == 0:
            raise BorrowRefused("rank 0 is not borrowed: pass a scalar as a Python int (SPEC-03 P-6)")
        if held.ndim > MAX_RANK:
            raise BorrowRefused("rank %d is above CINT_MAX_RANK, %d (SPEC-03 P-6)" % (held.ndim, MAX_RANK))
        if writable and held.readonly:
            raise BorrowRefused("writable=True over a read-only export (SPEC-03 P-7)")
        f = classify_format(held.format, held.itemsize)
        e = check_elem(f.elem, want)
        size = held.itemsize
        if any(s % size for s in held.strides):
            raise BorrowRefused("byte strides %s are not multiples of the item size %d (SPEC-03 P-5): cint.copy(a) "
                                "copies any layout" % (list(held.strides), size))
        strides = tuple(s // size for s in held.strides)
        view = _finish(e, held.shape, strides, held.address, writable, publish, layout, "buffer protocol",
                       held=held)
    except BaseException:
        held.release()
        raise
    return view


def _from_dlpack(obj, want, writable: bool, publish, layout: str) -> View:
    notes = []
    args = {"stream": None, "max_version": (1, 0), "dl_device": None, "copy": False}
    try:
        capsule = obj.__dlpack__(**args)
    except TypeError:
        args = {"stream": None}
        try:
            capsule = obj.__dlpack__(**args)
        except BufferError as failure:
            raise BorrowRefused("the DLPack producer could not export (%s)" % failure) from None
        notes.append("legacy DLPack producer: copy=False could not be requested (SPEC-03 P-10a)")
    except BufferError as failure:
        raise BorrowRefused("the DLPack producer cannot export without a copy (copy=False, SPEC-03 P-10: %s); "
                            "cint.copy(a) makes the copy explicitly" % failure) from None
    try:
        t = _dlpack.Tensor(capsule)
    except ValueError as failure:
        raise BorrowRefused(str(failure)) from None
    try:
        if t.versioned and t.version[0] != _dlpack.MAJOR_VERSION:
            raise BorrowRefused("DLPack version %d.%d: this bridge reads major version 1" % t.version)
        if not t.versioned:
            if len(args) > 1:
                notes.append("unversioned DLPack capsule: read-only status cannot be expressed (SPEC-03 P-10a)")
            if writable:
                raise BorrowRefused("an unversioned DLPack capsule cannot express read-only status, so it is "
                                    "borrowed read-only only (SPEC-03 P-10a)")
        if t.flags & _dlpack.FLAG_IS_COPIED:
            raise BorrowRefused("the DLPack producer copied the data (DLPACK_FLAG_BITMASK_IS_COPIED), and a borrow "
                                "never copies (SPEC-03 P-10)")
        if t.device[0] != _dlpack.kDLCPU:
            raise BorrowRefused("the DLPack tensor is on %s:%d, and cint.borrow takes CPU memory only (SPEC-03 P-1, "
                                "P-11)" % (_dlpack.DEVICE_NAMES.get(t.device[0], "device type %d" % t.device[0]),
                                           t.device[1]))
        if t.shape is None or t.ndim == 0 or t.ndim > MAX_RANK:
            raise BorrowRefused("rank %d is not borrowed: ranks 1 to %d are (SPEC-03 P-6)%s"
                                % (t.ndim, MAX_RANK, "; pass a scalar as a Python int" if t.ndim == 0 else ""))
        if any(n < 0 for n in t.shape):
            raise BorrowRefused("negative extent in DLPack shape %s" % (list(t.shape),))
        f = classify_dtype(t.dtype)
        e = check_elem(f.elem, want)
        if t.byte_offset % e.size:
            raise BorrowRefused("DLPack byte_offset %d is not a multiple of the element size %d (SPEC-03 P-13)"
                                % (t.byte_offset, e.size))
        if writable and t.flags & _dlpack.FLAG_READ_ONLY:
            raise BorrowRefused("writable=True over a read-only DLPack tensor (DLPACK_FLAG_BITMASK_READ_ONLY, "
                                "SPEC-03 P-13)")
        strides = t.strides if t.strides is not None else c_strides(t.shape, 1)
        view = _finish(e, t.shape, strides, t.data + t.byte_offset, writable, publish, layout, "DLPack",
                       tensor=t, exporter=obj, notes=tuple(notes), exchange=args)
    except BaseException:
        t.delete()
        raise
    return view


def _finish(e: Elem, shape: tuple, strides: tuple, address: int, writable: bool, publish, layout: str,
            protocol: str, **kept) -> View:
    """The checks every borrow shares (SPEC-03 P-5, P-8, TR-VAL-3), then the View."""
    if address % e.align:
        raise BorrowRefused("the data pointer %#x is not aligned to %d bytes, the alignment of %s (SPEC-03 P-5, "
                            "A-13a): cint.copy(a) copies it into aligned storage" % (address, e.align, e))
    if layout != "any" and not _contiguous(shape, strides, layout):
        raise BorrowRefused("layout=%r refuses this export, which is not contiguous in %s order (SPEC-03 P-8)"
                            % (layout, "row-major" if layout == "C" else "column-major"))
    view = View._make(elem=e, shape=tuple(shape), strides=tuple(strides), address=address, writable=writable,
                      publish=publish, protocol=protocol, **kept)
    if e.kind in ("t1", "t27"):
        try:
            view._check_values()
        except BorrowRefused:
            view._finalizer.detach()
            raise
    return view
