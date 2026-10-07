"""Canonical fault record bytes, decoded (SPEC-01 IM-144 to IM-149).

`cint_fault_get` returns the canonical record of the held fault; any Python
structure is a non-canonical view of those bytes (IM-149). The bytes are
untrusted input (IM-148): every length is compared with the bytes remaining
before anything is read, and anything IM-148 rejects raises ValueError.

Values decode to exact Python values: integers and `Z` to `int`, `Bool` to
`bool`, fixed point to `cint.Fixed`, `T1` and `T27` to `int` (SPEC-05
TR-SER-1), and arrays, view identities, and redacted values to the classes
below. Each value's type is reported beside it by name.
"""
from __future__ import annotations

from dataclasses import dataclass

from ._elem import SIGNED_TAGS, T27_MAX, TAG_BOOL, TAG_FIXED, TAG_T1, TAG_T27, TAG_Z, UNSIGNED_TAGS, Elem, parse_elem
from ._fixed import Fixed

DOMAIN_V1 = b"cint-core-1/fault/v1"
DOMAIN_V2 = b"cint-core-1/fault/v2"
RUN_TIME = "run-time"             # a record from cint_fault_get, or embedded in state (IM-156)
COMPILE_TIME = "compile-time"     # a record from a compiler's diagnostic carrier (SPEC-09 CINTC-14)
Z_MAX = 257                       # IM-108 item 1, and every Z of a version 1 record
Z_MAX_OPERAND = 513               # IM-108 item 2: an operand of a compile-time version 2 record
Z_MAX_EXACT = 520                 # IM-108 item 3: `exact` of a compile-time version 2 record
MAX_OPERANDS = 8                  # IM-106
MAX_OPERATION = 64                # IM-106
MAX_PATH = 1024                   # IM-106
MAX_RANK = 8                      # IM-146: an array or view rank is 1 to 8 in the encoding
M64 = (1 << 63) - 1
TAG_ARRAY = 0x51
TAG_VIEW = 0x61
TAG_REDACTED = 0x7F
PHASE_NAMES = ("entry", "work-item", "epilogue")

_SIGNED = {tag: bits for bits, tag in SIGNED_TAGS.items()}
_UNSIGNED = {tag: bits for bits, tag in UNSIGNED_TAGS.items()}
_INT_NAMES = {"I%d" % b for b in SIGNED_TAGS} | {"U%d" % b for b in UNSIGNED_TAGS}
_FAULT_NAMES = ("E_OVERFLOW", "E_DIV_ZERO", "E_BOUNDS", "E_SHAPE", "E_SHIFT", "E_NARROW", "E_ALIAS",
                "E_STALE_HANDLE", "E_FUEL", "E_UNSUPPORTED", "E_DOMAIN", "E_DEPTH", "E_ASSERT")


@dataclass(frozen=True)
class ArrayValue:
    """An array value of a fault record (tag `51`): elements in row-major order."""
    elem: str
    shape: tuple
    values: tuple

    @property
    def type(self) -> str:
        return "%s[%s]" % (self.elem, ", ".join(str(n) for n in self.shape))


@dataclass(frozen=True)
class ViewIdentity:
    """The identity of a view in a fault record (tag `61`): no contents and no
    placement (SPEC-01 IM-146). `dims` holds (extent, lower bound, stride) per
    dimension, in elements."""
    elem: str
    buffer: int
    generation: int
    origin: int
    dims: tuple
    writable: bool

    @property
    def type(self) -> str:
        return "view of %s" % self.elem


@dataclass(frozen=True)
class Redacted:
    """A redacted value (tag `7f`): its type, and no payload."""
    type: str


class _Reader:
    def __init__(self, data: bytes):
        self.data = data
        self.at = 0

    def left(self) -> int:
        return len(self.data) - self.at

    def take(self, n: int) -> bytes:
        if n > self.left():
            raise ValueError("truncated: %d bytes needed at offset %d, %d remain" % (n, self.at, self.left()))
        b = self.data[self.at:self.at + n]
        self.at += n
        return b

    def u(self, n: int) -> int:
        return int.from_bytes(self.take(n), "little")

    def i64(self) -> int:
        return int.from_bytes(self.take(8), "little", signed=True)

    def flag(self, what: str) -> bool:
        b = self.u(1)
        if b > 1:
            raise ValueError("%s: presence byte %02x is not 00 or 01" % (what, b))
        return b == 1

    def counted(self, what: str, limit: int) -> bytes:
        n = self.u(4)
        if n > limit:
            raise ValueError("%s: length %d exceeds %d" % (what, n, limit))
        return self.take(n)


@dataclass(frozen=True)
class _Desc:
    kind: str          # "scalar", "z", "array", "view", or "redacted"
    name: str          # the type's name
    elem: Elem | None  # the scalar or element type
    size: int          # payload bytes; 0 for a view or a redacted value
    shape: tuple = ()
    view: tuple = ()   # buffer, generation, origin, dims, writable


def _scalar_desc(rd: _Reader, tag: int, where: str) -> Elem | None:
    if tag == TAG_BOOL:
        return parse_elem("Bool")
    if tag in _SIGNED:
        return parse_elem("I%d" % _SIGNED[tag])
    if tag in _UNSIGNED:
        return parse_elem("U%d" % _UNSIGNED[tag])
    if tag == TAG_FIXED:
        storage, f = rd.u(1), rd.u(2)
        if storage not in (0x11, 0x12, 0x13, 0x14) or f > _SIGNED[storage] - 1:
            raise ValueError("%s: a Q descriptor with storage %02x and f %d (SPEC-01 IM-146, IM-148)"
                             % (where, storage, f))
        w = _SIGNED[storage]
        return parse_elem("Q%d.%d" % (w - f, f))
    if tag == TAG_T1:
        return parse_elem("T1")
    if tag == TAG_T27:
        return parse_elem("T27")
    return None


def _descriptor(rd: _Reader, where: str, nested: bool = False) -> _Desc:
    """One type descriptor (IM-146). An element descriptor (`nested`) is a
    scalar type; `Z`, arrays, views, and redaction occur only at the top of a
    value, views only as operands, and redaction only for an operand or
    `exact` (IM-148: tags `61` and `7f` only in a fault record)."""
    tag = rd.u(1)
    elem = _scalar_desc(rd, tag, where)
    if elem is not None:
        return _Desc("scalar", elem.name, elem, elem.size)
    if tag == TAG_Z and not nested:
        return _Desc("z", "Z", None, 0)
    if tag in (TAG_ARRAY, TAG_VIEW) and not nested and (tag == TAG_ARRAY or where == "operand"):
        inner = _descriptor(rd, "element", True)
        view = tag == TAG_VIEW
        if view:
            buffer, generation = rd.u(8), rd.u(8)
        rank = rd.u(1)
        if not 1 <= rank <= MAX_RANK:
            raise ValueError("%s: rank %d is outside 1 to 8 (SPEC-01 IM-148)" % (where, rank))
        if view:
            origin = rd.i64()
        size, shape, dims = inner.size, [], []
        for _ in range(rank):
            extent = rd.i64()
            if extent < 0:
                raise ValueError("%s: negative extent %d (SPEC-01 IM-148)" % (where, extent))
            shape.append(extent)
            if view:
                dims.append((extent, rd.i64(), rd.i64()))
            size *= extent
        if view:
            permission = rd.u(1)
            if permission > 1:
                raise ValueError("%s: a view permission byte is 00 or 01 (SPEC-01 IM-146)" % where)
            return _Desc("view", "view of " + inner.name, inner.elem, 0,
                         view=(buffer, generation, origin, tuple(dims), permission == 1))
        if size > M64:
            raise ValueError("%s: an array payload of %d bytes exceeds M64 (SPEC-01 IM-148)" % (where, size))
        return _Desc("array", "%s[%s]" % (inner.name, ", ".join(map(str, shape))), inner.elem, size, tuple(shape))
    if tag == TAG_REDACTED and not nested and where in ("operand", "exact"):
        inner = rd.data[rd.at:rd.at + 1]
        if inner == bytes([TAG_REDACTED]) or (inner == bytes([TAG_VIEW]) and where != "operand"):
            raise ValueError("%s: tag %s inside a redacted value" % (where, inner.hex()))
        hidden = _descriptor(rd, where)
        return _Desc("redacted", hidden.name, hidden.elem, 0)
    raise ValueError("%s: tag %02x is unknown, Proposed, or not allowed here (SPEC-01 IM-146, IM-148)"
                     % (where, tag))


def _scalar(elem: Elem, payload: bytes, where: str):
    if elem.kind == "bool":
        if payload[0] > 1:
            raise ValueError("%s: Bool byte %02x (SPEC-01 IM-145)" % (where, payload[0]))
        return payload[0] == 1
    raw = int.from_bytes(payload, "little", signed=elem.signed)
    if elem.kind == "t1" and raw not in (-1, 0, 1):
        raise ValueError("%s: T1 byte %02x is not ff, 00, or 01 (SPEC-05 TR-T1-6)" % (where, payload[0]))
    if elem.kind == "t27" and not -T27_MAX <= raw <= T27_MAX:
        raise ValueError("%s: T27 carrier %d is outside its range (SPEC-05 TR-T27-4)" % (where, raw))
    if elem.kind == "fixed":
        return Fixed._make(elem, raw)
    return raw


def _value(rd: _Reader, where: str, z_max: int):
    """One tagged value: (type name, Python value)."""
    d = _descriptor(rd, where)
    if d.kind == "z":
        n = rd.u(4)
        if n > z_max:
            raise ValueError("%s: a Z of %d bytes exceeds the bound %d (SPEC-01 IM-108)" % (where, n, z_max))
        b = rd.take(n)
        if (n == 1 and b[0] == 0) or (n >= 2 and ((b[-1] == 0 and b[-2] < 0x80) or (b[-1] == 0xFF and b[-2] >= 0x80))):
            raise ValueError("%s: a non-minimal Z (SPEC-01 IM-145)" % where)
        return "Z", int.from_bytes(b, "little", signed=True)
    if d.kind == "scalar":
        return d.name, _scalar(d.elem, rd.take(d.size), where)
    if d.kind == "array":
        payload = rd.take(d.size)
        step = d.elem.size
        values = tuple(_scalar(d.elem, payload[i:i + step], where) for i in range(0, len(payload), step))
        return d.name, ArrayValue(d.elem.name, d.shape, values)
    if d.kind == "view":
        buffer, generation, origin, dims, writable = d.view
        return d.name, ViewIdentity(d.elem.name, buffer, generation, origin, dims, writable)
    return "redacted " + d.name, Redacted(d.name)


def _position(rd: _Reader, what: str) -> tuple:
    raw = rd.counted(what + " path", MAX_PATH)
    try:
        path = raw.decode("utf-8")
    except UnicodeDecodeError:
        raise ValueError("%s: the path is not UTF-8" % what) from None
    line, column = rd.u(4), rd.u(4)
    if not path or line == 0 or column == 0:
        raise ValueError("%s: an empty path, or line or column 0" % what)
    return path, line, column


def decode_fault_record(data, kind: str = RUN_TIME) -> dict:
    """The fields of canonical fault record bytes, version 1 or 2, read under
    the record kind the caller passes (IM-148, IM-149): the keyword arguments
    of `cint.Fault`. A version 1 record's 32-byte revision of zero bytes,
    which `cint-rt-1` writes for an absent revision, reads as None."""
    if kind not in (RUN_TIME, COMPILE_TIME):
        raise ValueError("the record kind is %r or %r, not %r (SPEC-01 IM-148)" % (RUN_TIME, COMPILE_TIME, kind))
    data = bytes(data)
    rd = _Reader(data)
    domain = rd.counted("domain", 64)
    if domain not in (DOMAIN_V1, DOMAIN_V2):
        raise ValueError("unknown domain string %r" % (domain,))
    version = 2 if domain == DOMAIN_V2 else 1
    op_max, exact_max = (Z_MAX_OPERAND, Z_MAX_EXACT) if version == 2 and kind == COMPILE_TIME else (Z_MAX, Z_MAX)
    number = rd.u(2)
    if not 1 <= number <= len(_FAULT_NAMES):
        raise ValueError("unknown fault code %d (SPEC-01 IM-104)" % number)
    op = rd.counted("operation", MAX_OPERATION)
    if not op or any(not (0x61 <= c <= 0x7A or 0x30 <= c <= 0x39 or c in (0x2E, 0x5F)) for c in op):
        raise ValueError("the operation is not a lowercase identifier of SPEC-01 IM-130")
    count = rd.u(4)
    if count > MAX_OPERANDS or count > rd.left():
        raise ValueError("operand count %d: at most 8 (SPEC-01 IM-106) and the bytes remaining" % count)
    typed = [_value(rd, "operand", op_max) for _ in range(count)]
    exact = None
    if rd.flag("exact"):
        t, exact = _value(rd, "exact", exact_max)
        if t not in ("Z", "redacted Z"):
            raise ValueError("exact is a Z or a redacted Z (SPEC-01 IM-109, IM-149), not %s" % t)
    limit_type = limit = None
    if rd.flag("limit"):
        limit_type, limit = _value(rd, "limit", Z_MAX)
    path, line, column = _position(rd, "position")
    if version == 1:
        revision, source_map = rd.take(32), None
        if revision == bytes(32):
            revision = None
    else:
        revision = rd.take(32) if rd.flag("revision") else None
        source_map = rd.take(32) if rd.flag("source-map digest") else None
    kernel = dispatch = phase = work_item = step = None
    if rd.flag("address"):
        raw = rd.counted("kernel name", rd.left())
        try:
            kernel = raw.decode("utf-8")
        except UnicodeDecodeError:
            raise ValueError("the kernel name is not UTF-8") from None
        dispatch, p = rd.i64(), rd.u(1)
        if p > 2:
            raise ValueError("phase %d is not 0, 1, or 2" % p)
        phase = PHASE_NAMES[p]
        if rd.flag("work item"):
            if p != 1:
                raise ValueError("a work item outside the work-item phase (SPEC-01 IM-106)")
            work_item, step = rd.i64(), rd.i64()
    depth = rd.u(4)
    if depth > rd.left():
        raise ValueError("stack count %d exceeds the bytes remaining" % depth)
    stack = tuple(_position(rd, "stack position") for _ in range(depth))
    if rd.left():
        raise ValueError("%d trailing bytes" % rd.left())
    return {"code": _FAULT_NAMES[number - 1], "operation": op.decode("ascii"),
            "operands": tuple(v for _, v in typed), "operand_types": tuple(t for t, _ in typed),
            "exact": exact, "limit": limit, "limit_type": limit_type, "file": path, "line": line,
            "column": column, "stack": stack, "revision": revision, "source_map": source_map,
            "kernel": kernel, "dispatch": dispatch, "phase": phase, "work_item": work_item,
            "work_index": None, "step": step, "reason": None, "record": data, "version": version}


def render_value(type_name: str, value, bare: bool = False) -> str:
    """The typed-value form of an `.expect` file (SPEC-09 CONF-11):
    `<Type> <decimal>`, `Bool true`, or `Z <decimal>`; with `bare`, the value
    alone. ValueError for a value that form cannot show (fixed point, ternary,
    arrays, views, and redacted values have no `.expect` form yet)."""
    if type_name == "Bool" and isinstance(value, bool):
        text = "true" if value else "false"
    elif (type_name == "Z" or type_name in _INT_NAMES) and isinstance(value, int) and not isinstance(value, bool):
        text = "%d" % value
    else:
        raise ValueError("a %s value has no .expect typed-value form (SPEC-09 CONF-11)" % (type_name,))
    return text if bare else "%s %s" % (type_name, text)


def display_value(type_name, value) -> str:
    """A value as the diagnostic presentation shows it (SPEC-01 IM-110):
    integers in exact decimal, never rounded; fixed point as its exact decimal
    and raw value; other values with their type."""
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        return "%d" % value
    if isinstance(value, Fixed):
        return "%s (%s raw %d)" % (value, value.elem, value.raw)
    if isinstance(value, ArrayValue):
        return "%s {%s}" % (value.type, ", ".join(display_value(value.elem, v) for v in value.values))
    if isinstance(value, ViewIdentity):
        dims = ", ".join("extent %d lower %d stride %d" % d for d in value.dims)
        return "view of %s: buffer %d generation %d origin %d, %s, %s" % (
            value.elem, value.buffer, value.generation, value.origin, dims, "write" if value.writable else "read")
    if isinstance(value, Redacted):
        return "<redacted %s>" % value.type
    return "%r" % (value,)
