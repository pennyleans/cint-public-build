"""Faults, canonical fault records and diagnostics.

Fault codes and numbers are SPEC-01 9.1, plus the interim decisions of the
foundations plan: `E_DOMAIN` (11, OQ-01) and `E_ASSERT` (13, OQ-13). The
fault record carries every field of SPEC-01 9.2; `revision` and the
source-map digest are absent because `cint_ref` builds no SIR (SPEC-09
REF-03); `address` is present for a fault raised by a kernel dispatch only
(SPEC-02 F-8), and so are the view descriptors of an entry `E_ALIAS` record.

`encode_fault_record` and `decode_fault_record` give the canonical bytes of
SPEC-01 IM-149 version 2 (`cint-core-1/fault/v2`, slice 2 patch D-10); a
version 1 record stays readable. The caller passes the record kind
(compile-time or run-time), because the bytes do not show it and the bound on
a `Z` depends on it (IM-108, IM-148; decision 23, D-17 with OQ-147).
"""
from __future__ import annotations

from dataclasses import dataclass

from .types import Value

FAULT_CODES = {
    "E_OVERFLOW": 1,
    "E_DIV_ZERO": 2,
    "E_BOUNDS": 3,
    "E_SHAPE": 4,
    "E_SHIFT": 5,
    "E_NARROW": 6,
    "E_ALIAS": 7,
    "E_STALE_HANDLE": 8,
    "E_FUEL": 9,
    "E_UNSUPPORTED": 10,
    "E_DOMAIN": 11,
    "E_DEPTH": 12,
    "E_ASSERT": 13,
}

# The text written wherever the specification has not decided a value (see ref/OPEN.md).
UNASSIGNED = "unassigned"


@dataclass(frozen=True)
class Position:
    path: str
    line: int
    column: int

    def __str__(self) -> str:
        return "%s:%d:%d" % (self.path, self.line, self.column)


@dataclass(frozen=True)
class Fault:
    """The result of an operation that faults (SPEC-01 9.2 fields an operation determines)."""
    code: str
    operation: str
    operands: tuple
    exact: int | None = None
    limit: Value | None = None
    index: int | None = None      # fold_checked only: the least faulting element index (SPEC-01 6.4)


@dataclass(frozen=True)
class Descriptor:
    """A canonical view descriptor (SPEC-02 V-13; the fields of SPEC-01 IM-146 tag `61`) in
    element units: the element type, the origin, and the extent and stride of each
    dimension (every declared lower bound is 0), and the permission. The buffer has no
    canonical identifier in `cint_ref` (SPEC-09 CONF-11 rule 13)."""
    elem: str
    origin: int
    shape: tuple
    strides: tuple
    writable: bool

    def render(self) -> str:
        dims = " ".join("%d 0 %d" % (n, s) for n, s in zip(self.shape, self.strides))
        return "%s none %d %d %s %s" % (self.elem, len(self.shape), self.origin, dims,
                                         "write" if self.writable else "read")


PHASES = ("entry", "work-item", "epilogue")      # SPEC-02 F-3, in the order of the address phase byte


@dataclass
class FaultRecord:
    """The canonical fault record of SPEC-01 9.2."""
    code: str
    operation: str
    operands: tuple
    exact: int | None
    limit: Value | None
    position: Position
    stack: tuple = ()
    revision: bytes | None = None      # always absent in cint_ref
    address: object | None = None      # absent for a fault outside a kernel dispatch; else an Address
    source_map: bytes | None = None    # always absent in cint_ref (SPEC-09 SIR-17)
    descriptors: tuple = ()            # the view descriptors of an entry E_ALIAS record (SPEC-02 A-8)

    @classmethod
    def from_fault(cls, f: Fault, position: Position, stack=()) -> "FaultRecord":
        return cls(f.code, f.operation, tuple(f.operands), f.exact, f.limit, position, tuple(stack))

    def to_expect_lines(self, fmt: int = 1) -> list[str]:
        """The `fault.*` lines of CONF-11; format 2 and later add `fault.source-map` and one
        `fault.stack` line per stack position (rule 5). A fault raised by a kernel dispatch
        has the kernel form of format 4 (rule 13), with its descriptors, phase, kernel name
        and address."""
        kernel = self.address is not None
        if kernel and fmt < 4:
            raise ValueError("a fault raised by a kernel dispatch is written in format 4 (CONF-11 rule 13)")
        lines = ["fault.code " + self.code, "fault.operation " + self.operation]
        lines += ["fault.operand " + render_operand(o) for o in self.operands]
        lines += ["fault.descriptor " + d.render() for d in self.descriptors]
        lines.append("fault.exact " + ("none" if self.exact is None else str(self.exact)))
        lines.append("fault.limit " + ("none" if self.limit is None else self.limit.render()))
        lines.append("fault.position %s" % (self.position,))
        lines.append("fault.revision self")
        if fmt >= 2:
            lines.append("fault.source-map self")
        if kernel:
            a = self.address
            lines.append("fault.phase " + PHASES[a.phase])
            lines.append("fault.kernel " + a.name)
            lines.append("fault.address %d %s" % (a.dispatch, "%d %d" % (a.work_item, a.step) if a.phase == 1
                                                  else "none none"))
        else:
            lines.append("fault.address none")
        lines.append("fault.stack-depth %d" % len(self.stack))
        if fmt >= 2:
            lines += ["fault.stack %s" % (p,) for p in self.stack]
        return lines


def render_operand(o) -> str:
    if isinstance(o, Value):
        if o.type == "Z":
            return "Z %d" % o.value
        return o.render()
    return str(o)


@dataclass
class Diagnostic:
    """A compile error: a `C` code of SPEC-04 17.2 (LS-313) and a position.

    `notes` holds the `= note:` and `= help:` lines of SPEC-04 LS-276 (for
    example the script-mode note of LS-312). Message and notes are not part of
    an expectation (SPEC-09 9.2)."""
    code: str | None
    position: Position
    message: str
    fault: Fault | None = None      # for C6001: the fault met during constant evaluation
    notes: tuple = ()

    def to_expect_lines(self) -> list[str]:
        lines = ["diagnostic.code " + (self.code or UNASSIGNED),
                 "diagnostic.position %s" % (self.position,)]
        if self.fault is not None:
            f = self.fault
            lines.append("diagnostic.fault.code " + f.code)
            lines.append("diagnostic.fault.operation " + f.operation)
            lines += ["diagnostic.fault.operand " + render_operand(o) for o in f.operands]
            lines.append("diagnostic.fault.exact " + ("none" if f.exact is None else str(f.exact)))
            lines.append("diagnostic.fault.limit " + ("none" if f.limit is None else f.limit.render()))
        return lines

    def record(self) -> FaultRecord:
        """The compile-time fault record of a C6001 or C6002 diagnostic (SPEC-09 CINTC-14)."""
        if self.fault is None:
            raise ValueError("the diagnostic carries no fault record")
        return FaultRecord.from_fault(self.fault, self.position)

    def brief(self) -> str:
        """The compact one-line form of SPEC-09 CINTC-11."""
        return "%s: %s: %s" % (self.position, self.code or UNASSIGNED, self.message)


class CompileError(Exception):
    def __init__(self, diagnostic: Diagnostic):
        super().__init__(diagnostic.brief())
        self.diagnostic = diagnostic


class FaultSignal(Exception):
    """Raised inside the executor; ends the entry with a canonical record."""
    def __init__(self, record: FaultRecord):
        super().__init__(record.code)
        self.record = record


class Refused(Exception):
    """A construct or case that cint_ref does not decide (outside the scalar surface, or Open)."""
    def __init__(self, message: str, position: Position | None = None):
        super().__init__(message)
        self.message = message
        self.position = position


def error(code, position, message, fault=None, notes=()):
    """Raise a CompileError."""
    raise CompileError(Diagnostic(code, position, message, fault, tuple(notes)))


# ---------------------------------------------------------------------------
# Canonical fault record bytes (SPEC-01 IM-144 to IM-149).

DOMAIN_V1 = "cint-core-1/fault/v1"
DOMAIN_V2 = "cint-core-1/fault/v2"
COMPILE_TIME = "compile-time"    # a record from a compiler's diagnostic carrier (CINTC-14)
RUN_TIME = "run-time"            # a record from cint_fault_get, or embedded in state (IM-156)
RECORD_KINDS = (COMPILE_TIME, RUN_TIME)
Z_MAX = 257                      # IM-108 item 1, and every Z of a version 1 record
Z_MAX_OPERAND = 513              # IM-108 item 2: an operand of a compile-time v2 record
Z_MAX_EXACT = 520                # IM-108 item 3: `exact` of a compile-time v2 record
MAX_OPERANDS = 8                 # IM-106
MAX_OPERATION = 64               # IM-106
MAX_PATH = 1024                  # IM-106
M64 = (1 << 63) - 1
U64_MASK = (1 << 64) - 1
_CODE_NAMES = {n: c for c, n in FAULT_CODES.items()}
_SIGNED_TAGS = {0x11: 8, 0x12: 16, 0x13: 32, 0x14: 64, 0x15: 128, 0x16: 256, 0x17: 512, 0x18: 1024}
_UNSIGNED_TAGS = {0x21: 8, 0x22: 16, 0x23: 32, 0x24: 64}
_TAG_OF = {"I%d" % w: t for t, w in _SIGNED_TAGS.items()}
_TAG_OF.update({"U%d" % w: t for t, w in _UNSIGNED_TAGS.items()})


@dataclass(frozen=True)
class RawValue:
    """A tagged value that cint_ref does not model (fixed point, an array, a view
    identity, a redacted value), kept as its canonical bytes (IM-147)."""
    raw: bytes

    def render(self) -> str:
        return "<tagged %s>" % self.raw.hex()


@dataclass(frozen=True)
class Address:
    """The kernel-dispatch address of IM-149; `work_item` and `step` only in phase 1."""
    name: str
    dispatch: int
    phase: int
    work_item: int | None = None
    step: int | None = None


def z_bytes(n: int) -> bytes:
    """Minimal little-endian two's complement; zero is empty (IM-145)."""
    if n == 0:
        return b""
    length = ((n if n >= 0 else ~n).bit_length() + 8) // 8    # value bits and a sign bit
    return (n & ((1 << (8 * length)) - 1)).to_bytes(length, "little")


def encode_value(v, z_max: int = Z_MAX) -> bytes:
    """The tagged bytes of a value (IM-145 to IM-147)."""
    if isinstance(v, RawValue):
        return v.raw
    if not isinstance(v, Value):
        raise ValueError("not a typed value: %r" % (v,))
    if v.type == "Bool":
        return bytes([0x01, 1 if v.value else 0])
    if v.type == "Z":
        b = z_bytes(v.value)
        if len(b) > z_max:
            raise ValueError("a Z of %d bytes exceeds the bound %d (SPEC-01 IM-108)" % (len(b), z_max))
        return bytes([0x0F]) + len(b).to_bytes(4, "little") + b
    tag = _TAG_OF.get(v.type)
    if tag is None:
        raise ValueError("no canonical encoding for type %r in cint_ref" % (v.type,))
    w = _SIGNED_TAGS.get(tag) or _UNSIGNED_TAGS[tag]
    lo, hi = (-(1 << (w - 1)), (1 << (w - 1)) - 1) if tag in _SIGNED_TAGS else (0, (1 << w) - 1)
    if not lo <= v.value <= hi:
        raise ValueError("%s is outside its type" % v.render())
    return bytes([tag]) + (v.value & ((1 << w) - 1)).to_bytes(w // 8, "little")


def _position_bytes(p: Position) -> bytes:
    path = p.path.encode("utf-8")
    if not path or len(path) > MAX_PATH or not (1 <= p.line <= 0xFFFFFFFF and 1 <= p.column <= 0xFFFFFFFF):
        raise ValueError("a position has a path of 1 to 1024 bytes and a 1-based line and column")
    return len(path).to_bytes(4, "little") + path + p.line.to_bytes(4, "little") + p.column.to_bytes(4, "little")


def _bounds(version: int, kind: str):
    """(operand bound, exact bound) for a Z in this record (IM-108, IM-148)."""
    if kind not in RECORD_KINDS:
        raise ValueError("the record kind is %s or %s, not %r (SPEC-01 IM-148)" % (COMPILE_TIME, RUN_TIME, kind))
    if version == 2 and kind == COMPILE_TIME:
        return Z_MAX_OPERAND, Z_MAX_EXACT
    return Z_MAX, Z_MAX


def encode_fault_record(r: FaultRecord, kind: str, version: int = 2) -> bytes:
    """The canonical bytes of a fault record (IM-149) of the given kind and
    version; ValueError when a field is outside its bound for that kind."""
    if version not in (1, 2):
        raise ValueError("fault record version 1 or 2")
    op_max, exact_max = _bounds(version, kind)
    if r.code not in FAULT_CODES:
        raise ValueError("unknown fault code %r" % r.code)
    if r.descriptors:
        raise ValueError("a view descriptor operand has no canonical bytes until arrays in .ci programs have "
                         "canonical buffer identifiers (SPEC-01 IM-81, IM-146)")
    op = r.operation.encode("ascii")
    if not op or len(op) > MAX_OPERATION or len(r.operands) > MAX_OPERANDS:
        raise ValueError("an operation of 1 to 64 bytes and at most 8 operands (SPEC-01 IM-106)")
    domain = (DOMAIN_V2 if version == 2 else DOMAIN_V1).encode("ascii")
    out = [len(domain).to_bytes(4, "little"), domain, FAULT_CODES[r.code].to_bytes(2, "little"),
           len(op).to_bytes(4, "little"), op, len(r.operands).to_bytes(4, "little")]
    out += [encode_value(o, op_max) for o in r.operands]
    if r.exact is None:
        out.append(b"\x00")
    else:
        exact = r.exact if isinstance(r.exact, RawValue) else Value("Z", r.exact)
        out += [b"\x01", encode_value(exact, exact_max)]
    out += [b"\x00"] if r.limit is None else [b"\x01", encode_value(r.limit)]
    out.append(_position_bytes(r.position))
    for digest in (r.revision, r.source_map):
        if digest is not None and len(digest) != 32:
            raise ValueError("a revision or source-map digest is 32 bytes")
    if version == 1:
        if r.source_map is not None:
            raise ValueError("a version 1 record has no source-map digest")
        out.append(r.revision or bytes(32))   # v1: an absent revision is 32 zero bytes
    else:
        for digest in (r.revision, r.source_map):
            out += [b"\x00"] if digest is None else [b"\x01", digest]
    a = r.address
    if a is None:
        out.append(b"\x00")
    else:
        name = a.name.encode("utf-8")
        if a.phase not in (0, 1, 2) or (a.work_item is not None and a.phase != 1):
            raise ValueError("phase 0, 1 or 2, with a work item only in phase 1")
        out += [b"\x01", len(name).to_bytes(4, "little"), name, (a.dispatch & U64_MASK).to_bytes(8, "little"),
                bytes([a.phase])]
        if a.work_item is None:
            out.append(b"\x00")
        else:
            out += [b"\x01", (a.work_item & U64_MASK).to_bytes(8, "little"), (a.step & U64_MASK).to_bytes(8, "little")]
    out.append(len(r.stack).to_bytes(4, "little"))
    out += [_position_bytes(p) for p in r.stack]
    return b"".join(out)


class _Reader:
    """Bounds-checked reads over untrusted bytes: every length is compared with
    the bytes remaining before anything is sliced (IM-148)."""

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
        v = self.u(8)
        return v - (1 << 64) if v >> 63 else v

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


def _descriptor(rd: _Reader, where: str, nested: bool = False):
    """One type descriptor (IM-146). Returns (tag, payload size in bytes, or None
    for a Z, whose length follows). Tags 41 to 44 and 71 are refused: their
    payloads are owned by SPEC-05 or need a struct layout (ref/OPEN.md REF-OQ-36)."""
    tag = rd.u(1)
    if tag == 0x01:
        return tag, 1
    if tag == 0x0F and not nested:
        return tag, None
    if tag in _SIGNED_TAGS:
        return tag, _SIGNED_TAGS[tag] // 8
    if tag in _UNSIGNED_TAGS:
        return tag, _UNSIGNED_TAGS[tag] // 8
    if tag == 0x31:
        storage, f = rd.u(1), rd.u(2)
        if storage not in (0x11, 0x12, 0x13, 0x14) or f > _SIGNED_TAGS[storage] - 1:
            raise ValueError("a Q descriptor with storage %02x and f %d (SPEC-01 IM-146, IM-148)" % (storage, f))
        return tag, _SIGNED_TAGS[storage] // 8
    if tag in (0x51, 0x61) and not nested and (tag == 0x51 or where == "operand"):
        _, esize = _descriptor(rd, "element", True)
        if tag == 0x61:
            rd.take(16)                      # buffer identifier, generation (U64 each)
        rank = rd.u(1)
        if not 1 <= rank <= 8:
            raise ValueError("rank %d is outside 1 to 8 (SPEC-01 IM-148)" % rank)
        if tag == 0x61:
            rd.take(8)                       # origin offset
        size = esize
        for _ in range(rank):
            extent = rd.i64()
            if extent < 0:
                raise ValueError("negative extent %d (SPEC-01 IM-148)" % extent)
            if tag == 0x61:
                rd.take(16)                  # declared lower bound, stride
            size *= extent
        if tag == 0x61:
            if rd.u(1) > 1:
                raise ValueError("a view permission byte is 00 or 01 (SPEC-01 IM-146)")
            return tag, 0
        if size > M64:
            raise ValueError("an array payload of more than M64 bytes (SPEC-01 IM-148)")
        return tag, size
    if tag == 0x7F and not nested and where in ("operand", "exact"):
        inner = rd.data[rd.at:rd.at + 1]
        if inner == b"\x7f" or (inner == b"\x61" and where != "operand"):
            raise ValueError("tag %s inside a redacted value" % inner.hex())
        _descriptor(rd, where, False)      # the descriptor only; a redacted value has no payload
        return tag, 0
    raise ValueError("%s: tag %02x is unknown, Proposed, or not allowed here (SPEC-01 IM-146, IM-148)" % (where, tag))


def _value(rd: _Reader, where: str, z_max: int):
    """One tagged value: a Value for Bool, Z and the integer types, else a RawValue."""
    start = rd.at
    tag, size = _descriptor(rd, where)
    if tag == 0x0F:
        n = rd.u(4)
        if n > z_max:
            raise ValueError("%s: a Z of %d bytes exceeds the bound %d (SPEC-01 IM-108)" % (where, n, z_max))
        b = rd.take(n)
        if (n == 1 and b[0] == 0) or (n >= 2 and ((b[-1] == 0 and b[-2] < 0x80) or (b[-1] == 0xFF and b[-2] >= 0x80))):
            raise ValueError("%s: a non-minimal Z (SPEC-01 IM-145)" % where)
        return Value("Z", int.from_bytes(b, "little", signed=True))
    payload = rd.take(size)
    if tag == 0x01:
        if payload[0] > 1:
            raise ValueError("%s: Bool byte %02x (SPEC-01 IM-145)" % (where, payload[0]))
        return Value("Bool", payload[0] == 1)
    if tag in _SIGNED_TAGS:
        return Value("I%d" % _SIGNED_TAGS[tag], int.from_bytes(payload, "little", signed=True))
    if tag in _UNSIGNED_TAGS:
        return Value("U%d" % _UNSIGNED_TAGS[tag], int.from_bytes(payload, "little"))
    if tag == 0x51 and rd.data[start + 1] == 0x01 and any(c > 1 for c in payload):
        raise ValueError("%s: an invalid Bool element (SPEC-01 IM-148)" % where)
    return RawValue(rd.data[start:rd.at])


def _position(rd: _Reader, what: str) -> Position:
    raw = rd.counted(what + " path", MAX_PATH)
    try:
        path = raw.decode("utf-8")
    except UnicodeDecodeError:
        raise ValueError("%s: the path is not UTF-8" % what) from None
    line, column = rd.u(4), rd.u(4)
    if not path or line == 0 or column == 0:
        raise ValueError("%s: an empty path, or line or column 0" % what)
    return Position(path, line, column)


def decode_fault_record(data: bytes, kind: str) -> tuple:
    """Read canonical fault record bytes, version 1 or 2, under the record kind
    the caller passes (IM-148, IM-149). Returns (version, FaultRecord). Raises
    ValueError for everything IM-148 rejects, and for content outside IM-106:
    an unknown code, an operation that is not lowercase IM-130 text of at most
    64 bytes, more than 8 operands, an `exact` that is not a Z, or a work item
    outside the work-item phase."""
    rd = _Reader(bytes(data))
    domain = rd.counted("domain", 64)
    if domain == DOMAIN_V2.encode("ascii"):
        version = 2
    elif domain == DOMAIN_V1.encode("ascii"):
        version = 1
    else:
        raise ValueError("unknown domain string %r" % domain)
    op_max, exact_max = _bounds(version, kind)
    code = _CODE_NAMES.get(rd.u(2))
    if code is None:
        raise ValueError("unknown fault code")
    op = rd.counted("operation", MAX_OPERATION)
    if not op or any(not (0x61 <= c <= 0x7A or 0x30 <= c <= 0x39 or c in (0x2E, 0x5F)) for c in op):
        raise ValueError("the operation is not a lowercase identifier of SPEC-01 IM-130")
    count = rd.u(4)
    if count > MAX_OPERANDS or count > rd.left():
        raise ValueError("operand count %d: at most 8 (SPEC-01 IM-106) and the bytes remaining" % count)
    operands = tuple(_value(rd, "operand", op_max) for _ in range(count))
    exact = None
    if rd.flag("exact"):
        exact = _value(rd, "exact", exact_max)
        if isinstance(exact, Value) and exact.type == "Z":
            exact = exact.value
        elif not (isinstance(exact, RawValue) and exact.raw == b"\x7f\x0f"):
            raise ValueError("exact is a Z or a redacted Z (SPEC-01 IM-109, IM-149)")
    limit = _value(rd, "limit", Z_MAX) if rd.flag("limit") else None
    position = _position(rd, "position")
    if version == 1:
        revision, source_map = rd.take(32), None
    else:
        revision = rd.take(32) if rd.flag("revision") else None
        source_map = rd.take(32) if rd.flag("source-map digest") else None
    address = None
    if rd.flag("address"):
        raw = rd.counted("kernel name", rd.left())
        try:
            name = raw.decode("utf-8")
        except UnicodeDecodeError:
            raise ValueError("the kernel name is not UTF-8") from None
        dispatch, phase = rd.i64(), rd.u(1)
        if phase > 2:
            raise ValueError("phase %d is not 0, 1 or 2" % phase)
        work_item = step = None
        if rd.flag("work item"):
            if phase != 1:
                raise ValueError("a work item outside the work-item phase (SPEC-01 IM-106)")
            work_item, step = rd.i64(), rd.i64()
        address = Address(name, dispatch, phase, work_item, step)
    depth = rd.u(4)
    if depth > rd.left():
        raise ValueError("stack count %d exceeds the bytes remaining" % depth)
    stack = tuple(_position(rd, "stack position") for _ in range(depth))
    if rd.left():
        raise ValueError("%d trailing bytes" % rd.left())
    return version, FaultRecord(code, op.decode("ascii"), operands, exact, limit, position, stack, revision,
                                address, source_map)
