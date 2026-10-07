"""Canonical type signatures (SPEC-03 A-20 to A-24; box 10 defaults BX10-23 and BX10-27).

A library's reflection table holds, for each export, the canonical type
signature of A-20: the parameters' modes and types and the result's type,
with each struct, enum, and error set defined once and referred to by index
(A-21). The bytes are untrusted input (A-24). `decode` checks every
obligation of A-24 before it allocates for what follows: each count and
length is compared with the bytes remaining before anything is read for it,
and nothing is ever read past the end. Every rejection raises
`SignatureError`; a binding reports such an export as not callable, which is
also what an unknown tag means (A-26).

`encode` writes the bytes of a decoded signature, so that decoding and then
encoding returns the input exactly. `python_forms` turns a signature into the
forms of unit 1 (BX10-27): a `Param` for each parameter, and the result's
form. `render` spells an export as a declaration, for messages and help.
"""
from __future__ import annotations

from dataclasses import dataclass
import re
import struct

from ._elem import SIGNED_TAGS, UNSIGNED_TAGS, TAG_BOOL, TAG_FIXED, TAG_T1, TAG_T27

DOMAIN = b"cint-core-1/type-signature/v1"
PREFIX = struct.pack("<I", len(DOMAIN)) + DOMAIN      # 33 bytes: 1d 00 00 00 and the domain string
NO_SYMBOL = 0xFFFFFFFF
MODES = ("in", "inout", "out")                        # the mode bytes 00, 01, 02

TAG_ARRAY = 0x52
TAG_STRUCT = 0x71
TAG_ENUM = 0x72
TAG_ERROR = 0x73
TAG_ERROR_UNION = 0x74
TAG_TUPLE = 0x76

_INTEGER_NAMES = {tag: "I%d" % bits for bits, tag in SIGNED_TAGS.items()}
_INTEGER_NAMES.update({tag: "U%d" % bits for bits, tag in UNSIGNED_TAGS.items()})
_SCALAR_NAMES = dict(_INTEGER_NAMES)
_SCALAR_NAMES.update({TAG_BOOL: "Bool", TAG_T1: "T1", TAG_T27: "T27"})
_SCALAR_TAGS = {name: tag for tag, name in _SCALAR_NAMES.items()}
_FIXED_STORAGE = {0x11: 8, 0x12: 16, 0x13: 32, 0x14: 64}
_ENUM_TAGS = (0x11, 0x12, 0x13, 0x14, 0x21, 0x22, 0x23, 0x24)
_SET_TAGS = (0x21, 0x22, 0x23, 0x24)
_IDENTIFIER = re.compile(rb"[A-Za-z_][A-Za-z0-9_]*\Z")
_FIXED_NAME = re.compile(r"Q([0-9]+)\.([0-9]+)\Z")

# Where a type stands (the "Allowed in" column of A-20). "Every position" is a
# parameter, a field, an array element, a tuple element, a result, and the
# success type of an error union.
PARAM, FIELD, ELEMENT, ITEM, RESULT, SUCCESS = "parameter", "field", "array element", "tuple element", \
    "result", "success type"
_NAMED = (TAG_STRUCT, TAG_ENUM, TAG_ERROR)
_ALLOWED = {
    PARAM: (TAG_ARRAY,) + _NAMED,
    FIELD: (TAG_ARRAY,) + _NAMED,
    ELEMENT: _NAMED,
    ITEM: _NAMED,
    RESULT: _NAMED + (TAG_ERROR_UNION, TAG_TUPLE),
    SUCCESS: _NAMED + (TAG_TUPLE,),
}
_KNOWN = set(_SCALAR_NAMES) | {TAG_FIXED, TAG_ARRAY, TAG_STRUCT, TAG_ENUM, TAG_ERROR, TAG_ERROR_UNION, TAG_TUPLE}


class SignatureError(ValueError):
    """Type signature bytes that SPEC-03 A-24 rejects. `offset` is the byte at
    which the decoder stopped."""

    def __init__(self, offset: int, reason: str):
        self.offset = offset
        self.reason = reason
        super().__init__("type signature rejected at byte %d: %s (SPEC-03 A-24)" % (offset, reason))


# The decoded forms. A struct, enum, or error set appears as the index of its
# definition (A-21); `Signature.definitions` holds the definitions.

@dataclass(frozen=True)
class Scalar:
    """`Bool`, `I8` to `I1024`, `U8` to `U64`, `Q<i>.<f>`, `T1`, or `T27`, by
    the spelling of `cint._elem.parse_elem`."""
    elem: str


@dataclass(frozen=True)
class Bound:
    """An extent or bound: the value of size symbol `symbol` (None for none)
    plus `offset`."""
    symbol: int | None
    offset: int


@dataclass(frozen=True)
class Dim:
    """The shape of one dimension: form 0 is any extent (`_`), 1 is extent
    `bound`, 2 is `lower ..= bound`, and 3 is `lower .. bound`."""
    form: int
    lower: int = 0
    bound: Bound | None = None


@dataclass(frozen=True)
class Array:
    elem: object
    dims: tuple


@dataclass(frozen=True)
class StructRef:
    index: int


@dataclass(frozen=True)
class EnumRef:
    index: int


@dataclass(frozen=True)
class ErrorValue:
    """A value of an error set, as a parameter, field, element, or result."""
    index: int


@dataclass(frozen=True)
class ErrorUnion:
    """`E!T`: the set's definition index and the success type, None for `void`."""
    index: int
    value: object


@dataclass(frozen=True)
class Tuple:
    items: tuple


@dataclass(frozen=True)
class Field:
    name: str
    bits: int            # 0 when the field is not a bit field
    type: object


@dataclass(frozen=True)
class StructDef:
    name: str
    packed: bool
    fields: tuple


@dataclass(frozen=True)
class EnumDef:
    name: str
    underlying: str
    values: tuple        # (name, value) in declaration order


@dataclass(frozen=True)
class ErrorSetDef:
    """A declared set (`members` None, `values` its value names in tag order
    from 1) or a combined set (`members` the definition indices of its
    declared sets, in the order that numbers it; SPEC-04 LS-97)."""
    name: str
    underlying: str
    values: tuple = ()
    members: tuple | None = None

    @property
    def combined(self) -> bool:
        return self.members is not None


@dataclass(frozen=True)
class Parameter:
    mode: str
    type: object


@dataclass(frozen=True)
class Signature:
    definitions: tuple
    size_count: int
    params: tuple
    result: object       # None when the export returns nothing (void, and every kernel)


_KIND = {TAG_STRUCT: (StructDef, "a struct"), TAG_ENUM: (EnumDef, "an enum"), TAG_ERROR: (ErrorSetDef, "an error set"),
         TAG_ERROR_UNION: (ErrorSetDef, "an error set")}


def _width(name: str) -> int:
    """The bits of an integer type's name."""
    return int(name[1:])


def _type_width(t, defs) -> int:
    """The width in bits of a field type other than `Bool`, an array, or a
    struct: an integer's, a fixed-point type's storage, the carrier of `T1`
    (8) and `T27` (64), and an enum's or error set's underlying type."""
    if isinstance(t, (EnumRef, ErrorValue)):
        return _width(defs[t.index].underlying)
    m = _FIXED_NAME.match(t.elem)
    if m:
        return int(m.group(1)) + int(m.group(2))
    return {"T1": 8, "T27": 64}.get(t.elem) or _width(t.elem)


def _set_capacity(underlying: str) -> int:
    """The values a set of this unsigned underlying type can number: tags 1
    to 2^w - 1, since 0 is success (SPEC-04 LS-93)."""
    return (1 << _width(underlying)) - 1


class _Reader:
    """The decoder of A-24 over one byte string."""

    def __init__(self, data: bytes):
        self.data = data
        self.at = 0
        self.defs = []           # definitions decoded so far
        self.used = []           # references to each, so far
        self.size_count = 0

    def fail(self, reason: str, at: int | None = None):
        raise SignatureError(self.at if at is None else at, reason)

    def remaining(self) -> int:
        return len(self.data) - self.at

    def take(self, n: int) -> bytes:
        if n > self.remaining():
            self.fail("truncated: %d byte%s needed, %d remain" % (n, "" if n == 1 else "s", self.remaining()))
        chunk = self.data[self.at:self.at + n]
        self.at += n
        return chunk

    def u8(self) -> int:
        return self.take(1)[0]

    def u16(self) -> int:
        return int.from_bytes(self.take(2), "little")

    def u32(self) -> int:
        return int.from_bytes(self.take(4), "little")

    def i64(self) -> int:
        return int.from_bytes(self.take(8), "little", signed=True)

    def count(self, what: str) -> int:
        """A count or length, refused when it is larger than the bytes remaining."""
        at = self.at
        n = self.u32()
        if n > self.remaining():
            self.fail("%s %d is larger than the %d bytes remaining" % (what, n, self.remaining()), at)
        return n

    def identifier(self, what: str) -> str:
        at = self.at
        raw = self.take(self.count("the length of a " + what))
        if not _IDENTIFIER.match(raw) or len(raw) > 255:
            self.fail("%s %r is not an identifier (SPEC-04 LS-15)" % (what, raw), at)
        return raw.decode("ascii")

    def qualified(self) -> str:
        at = self.at
        raw = self.take(self.count("the length of a qualified name"))
        parts = raw.split(b".")
        if not all(_IDENTIFIER.match(p) and len(p) <= 255 for p in parts):
            self.fail("qualified name %r is not identifiers joined by full stops" % (raw,), at)
        return raw.decode("ascii")

    def reference(self, tag: int, limit: int) -> int:
        """A definition index, below `limit`, naming a definition of the kind `tag` needs."""
        at = self.at
        index = self.u32()
        cls, kind = _KIND[tag]
        if index >= limit:
            self.fail("definition index %d is not below the %d definitions before it" % (index, limit), at)
        if not isinstance(self.defs[index], cls):
            self.fail("definition %d is not a %s" % (index, kind), at)
        self.used[index] += 1
        return index

    def scalar(self, tag: int, at: int) -> Scalar:
        if tag != TAG_FIXED:
            return Scalar(_SCALAR_NAMES[tag])
        storage = self.u8()
        if storage not in _FIXED_STORAGE:
            self.fail("fixed-point storage tag %02x is not 11 to 14" % storage, self.at - 1)
        w = _FIXED_STORAGE[storage]
        f = self.u16()
        if f > w - 1:
            self.fail("fixed-point f %d is above the storage width less 1, %d" % (f, w - 1), self.at - 2)
        return Scalar("Q%d.%d" % (w - f, f))

    def type(self, where: str, limit: int):
        at = self.at
        tag = self.u8()
        if tag not in _KNOWN:
            self.fail("unknown tag %02x" % tag, at)
        if tag in _SCALAR_NAMES or tag == TAG_FIXED:
            return self.scalar(tag, at)
        if tag not in _ALLOWED[where]:
            self.fail("tag %02x is not allowed in a %s" % (tag, where), at)
        if tag == TAG_ARRAY:
            return self.array(where, limit)
        if tag == TAG_TUPLE:
            n = self.count("the element count of a tuple")
            if n < 2:
                self.fail("a tuple has at least two elements, not %d" % n, at + 1)
            return Tuple(tuple(self.type(ITEM, limit) for _ in range(n)))
        index = self.reference(tag, limit)
        if tag == TAG_STRUCT:
            return StructRef(index)
        if tag == TAG_ENUM:
            return EnumRef(index)
        if tag == TAG_ERROR:
            return ErrorValue(index)
        if self.remaining() and self.data[self.at] == 0:
            self.at += 1
            return ErrorUnion(index, None)
        return ErrorUnion(index, self.type(SUCCESS, limit))

    def array(self, where: str, limit: int) -> Array:
        elem = self.type(ELEMENT, limit)
        at = self.at
        rank = self.u8()
        if not 1 <= rank <= 4:
            self.fail("rank %d is not 1 to 4" % rank, at)
        return Array(elem, tuple(self.dim(where) for _ in range(rank)))

    def dim(self, where: str) -> Dim:
        at = self.at
        form = self.u8()
        if form > 3:
            self.fail("unknown shape form %02x" % form, at)
        if form == 0:
            if where == FIELD:
                self.fail("a field array has an open extent", at)
            return Dim(0)
        lower = self.i64() if form >= 2 else 0
        return Dim(form, lower, self.bound(where))

    def bound(self, where: str) -> Bound:
        at = self.at
        symbol = self.u32()
        offset = self.i64()
        if symbol == NO_SYMBOL:
            return Bound(None, offset)
        if where == FIELD:
            self.fail("a field array's extent names a size symbol", at)
        if symbol >= self.size_count:
            self.fail("size symbol %d is not below size_count %d" % (symbol, self.size_count), at)
        return Bound(symbol, offset)

    def definition(self):
        at = self.at
        tag = self.u8()
        if tag not in (TAG_STRUCT, TAG_ENUM, TAG_ERROR):
            self.fail("unknown definition tag %02x" % tag, at)
        name = self.qualified()
        if any(type(d) is _KIND[tag][0] and d.name == name for d in self.defs):
            self.fail("two definitions of %s named %s" % (_KIND[tag][1], name), at)
        if tag == TAG_STRUCT:
            return self.struct_def(name)
        if tag == TAG_ENUM:
            return self.enum_def(name)
        return self.set_def(name)

    def struct_def(self, name: str) -> StructDef:
        at = self.at
        layout = self.u8()
        if layout > 1:
            self.fail("unknown struct layout %02x" % layout, at)
        fields, seen = [], set()
        for _ in range(self.count("the field count of " + name)):
            at = self.at
            field = self.identifier("field name")
            if field in seen:
                self.fail("struct %s names field %s twice" % (name, field), at)
            seen.add(field)
            at = self.at
            bits = self.u8()
            t = self.type(FIELD, len(self.defs))
            if bits:
                self.bit_width(name, field, bits, t, at)
            fields.append(Field(field, bits, t))
        return StructDef(name, layout == 1, tuple(fields))

    def bit_width(self, name: str, field: str, bits: int, t, at: int):
        """SPEC-04 LS-82: at least 1 and at most the width of the field's type;
        1 for `Bool`; never on an array or struct field."""
        if isinstance(t, (Array, StructRef)):
            self.fail("%s.%s is an array or struct field with a bit width" % (name, field), at)
        if isinstance(t, Scalar) and t.elem == "Bool":
            if bits != 1:
                self.fail("Bool field %s.%s has bit width %d, not 1" % (name, field, bits), at)
            return
        width = _type_width(t, self.defs)
        if bits > width:
            self.fail("bit width %d of %s.%s is above its type's width, %d" % (bits, name, field, width), at)

    def enum_def(self, name: str) -> EnumDef:
        at = self.at
        tag = self.u8()
        if tag not in _ENUM_TAGS:
            self.fail("enum %s has underlying tag %02x, not 11 to 14 or 21 to 24" % (name, tag), at)
        underlying = _INTEGER_NAMES[tag]
        size = _width(underlying) >> 3
        values, names, numbers = [], set(), set()
        for _ in range(self.count("the enumerator count of " + name)):
            at = self.at
            member = self.identifier("enumerator name")
            value = int.from_bytes(self.take(size), "little", signed=tag < 0x20)
            if member in names:
                self.fail("enum %s names enumerator %s twice" % (name, member), at)
            if value in numbers:
                self.fail("enum %s gives the value %d twice" % (name, value), at)
            names.add(member)
            numbers.add(value)
            values.append((member, value))
        return EnumDef(name, underlying, tuple(values))

    def set_def(self, name: str) -> ErrorSetDef:
        at = self.at
        tag = self.u8()
        if tag not in _SET_TAGS:
            self.fail("error set %s has underlying tag %02x, not 21 to 24" % (name, tag), at)
        underlying = _INTEGER_NAMES[tag]
        at = self.at
        form = self.u8()
        if form > 1:
            self.fail("unknown error set form %02x" % form, at)
        capacity = _set_capacity(underlying)
        if form == 0:
            at = self.at
            n = self.count("the value count of " + name)
            if n > capacity:
                self.fail("error set %s has %d values, more than %s numbers" % (name, n, underlying), at)
            values, seen = [], set()
            for _ in range(n):
                at = self.at
                value = self.identifier("error value name")
                if value in seen:
                    self.fail("error set %s names value %s twice" % (name, value), at)
                seen.add(value)
                values.append(value)
            return ErrorSetDef(name, underlying, tuple(values))
        members, total = [], 0
        for _ in range(self.count("the member count of " + name)):
            at = self.at
            index = self.reference(TAG_ERROR, len(self.defs))
            if self.defs[index].combined:
                self.fail("member %d of %s is a combined set, not a declared set" % (index, name), at)
            if index in members:
                self.fail("%s lists member %d twice" % (name, index), at)
            members.append(index)
            total += len(self.defs[index].values)
            if total > capacity:
                self.fail("error set %s holds more than the %d values %s numbers" % (name, capacity, underlying), at)
        return ErrorSetDef(name, underlying, (), tuple(members))

    def signature(self) -> Signature:
        if self.take(len(PREFIX)) != PREFIX:
            self.fail("the domain is not %s" % DOMAIN.decode("ascii"), 0)
        for _ in range(self.count("def_count")):
            d = self.definition()
            self.defs.append(d)
            self.used.append(0)
        self.size_count = self.count("size_count")
        params = []
        for _ in range(self.count("param_count")):
            at = self.at
            mode = self.u8()
            if mode > 2:
                self.fail("unknown mode %02x" % mode, at)
            params.append(Parameter(MODES[mode], self.type(PARAM, len(self.defs))))
        if self.remaining() and self.data[self.at] == 0:
            self.at += 1
            result = None
        else:
            result = self.type(RESULT, len(self.defs))
        if self.remaining():
            self.fail("%d trailing byte%s" % (self.remaining(), "" if self.remaining() == 1 else "s"))
        for index, n in enumerate(self.used):
            if not n:
                self.fail("definition %d (%s) is never referred to" % (index, self.defs[index].name))
        return Signature(tuple(self.defs), self.size_count, tuple(params), result)


def decode(data) -> Signature:
    """Decode canonical type signature bytes (SPEC-03 A-20, A-21), or raise
    `SignatureError` for bytes that A-24 rejects, including an unknown tag."""
    if not isinstance(data, (bytes, bytearray, memoryview)):
        raise TypeError("a type signature is bytes, not %s" % type(data).__name__)
    return _Reader(bytes(data)).signature()


# Encoding: the inverse of `decode`.

def _name(text: str) -> bytes:
    raw = text.encode("utf-8")
    return struct.pack("<I", len(raw)) + raw


def _encode_type(t) -> bytes:
    if isinstance(t, Scalar):
        m = _FIXED_NAME.match(t.elem)
        if m:
            i, f = int(m.group(1)), int(m.group(2))
            return bytes([TAG_FIXED, SIGNED_TAGS[i + f]]) + struct.pack("<H", f)
        return bytes([_SCALAR_TAGS[t.elem]])
    if isinstance(t, Array):
        out = bytes([TAG_ARRAY]) + _encode_type(t.elem) + bytes([len(t.dims)])
        for d in t.dims:
            out += bytes([d.form])
            if d.form >= 2:
                out += struct.pack("<q", d.lower)
            if d.form >= 1:
                out += struct.pack("<Iq", NO_SYMBOL if d.bound.symbol is None else d.bound.symbol, d.bound.offset)
        return out
    if isinstance(t, Tuple):
        return bytes([TAG_TUPLE]) + struct.pack("<I", len(t.items)) + b"".join(_encode_type(x) for x in t.items)
    if isinstance(t, ErrorUnion):
        return bytes([TAG_ERROR_UNION]) + struct.pack("<I", t.index) + (b"\x00" if t.value is None else
                                                                         _encode_type(t.value))
    tag = {StructRef: TAG_STRUCT, EnumRef: TAG_ENUM, ErrorValue: TAG_ERROR}[type(t)]
    return bytes([tag]) + struct.pack("<I", t.index)


def _encode_def(d) -> bytes:
    if isinstance(d, StructDef):
        out = bytes([TAG_STRUCT]) + _name(d.name) + bytes([1 if d.packed else 0]) + struct.pack("<I", len(d.fields))
        for f in d.fields:
            out += _name(f.name) + bytes([f.bits]) + _encode_type(f.type)
        return out
    if isinstance(d, EnumDef):
        tag = _SCALAR_TAGS[d.underlying]
        out = bytes([TAG_ENUM]) + _name(d.name) + bytes([tag]) + struct.pack("<I", len(d.values))
        for member, value in d.values:
            out += _name(member) + value.to_bytes(_width(d.underlying) >> 3, "little", signed=tag < 0x20)
        return out
    out = bytes([TAG_ERROR]) + _name(d.name) + bytes([_SCALAR_TAGS[d.underlying]])
    if d.combined:
        return out + b"\x01" + struct.pack("<I", len(d.members)) + b"".join(struct.pack("<I", i) for i in d.members)
    return out + b"\x00" + struct.pack("<I", len(d.values)) + b"".join(_name(v) for v in d.values)


def encode(sig: Signature) -> bytes:
    """The canonical bytes of a signature (SPEC-03 A-20). It does not check
    the obligations of A-24; `decode` does."""
    out = bytearray(PREFIX)
    out += struct.pack("<I", len(sig.definitions))
    for d in sig.definitions:
        out += _encode_def(d)
    out += struct.pack("<II", sig.size_count, len(sig.params))
    for p in sig.params:
        out += bytes([MODES.index(p.mode)]) + _encode_type(p.type)
    out += b"\x00" if sig.result is None else _encode_type(sig.result)
    return bytes(out)


# The forms of unit 1 (BX10-27).

class NoPythonForm(Exception):
    """A form that box 10 lists but does not call: a struct, an enum, or an
    error-set value outside an error union (BX10-27)."""


def _form_name(sig: Signature, t) -> str:
    """What a form is, for a message: "struct geo.Body", "an array of enum E"."""
    if isinstance(t, Array):
        return "an array of " + _form_name(sig, t.elem)
    if isinstance(t, StructRef):
        return "struct " + sig.definitions[t.index].name
    if isinstance(t, EnumRef):
        return "enum " + sig.definitions[t.index].name
    if isinstance(t, ErrorValue):
        return "a value of error set " + sig.definitions[t.index].name
    if isinstance(t, Tuple):
        return "a tuple holding " + ", ".join(_form_name(sig, x) for x in t.items)
    return t.elem


def _no_form(sig: Signature, what: str, t) -> NoPythonForm:
    return NoPythonForm("%s is %s, which has no Python form in box 10 (BX10-27): the export is listed and "
                        "reachable through ctx.entry, but not callable" % (what, _form_name(sig, t)))


def result_form(sig: Signature):
    """The result's form: None for none, an element name for a scalar, a tuple
    of element names for a tuple, or `("error", set_index, value_form)` for
    `E!T`, where `value_form` is one of the first three."""
    def plain(t, what):
        if t is None:
            return None
        if isinstance(t, Tuple) and all(isinstance(x, Scalar) for x in t.items):
            return tuple(x.elem for x in t.items)
        if not isinstance(t, Scalar):
            raise _no_form(sig, what, t)
        return t.elem
    r = sig.result
    if isinstance(r, ErrorUnion):
        return ("error", r.index, plain(r.value, "the success type of the result"))
    return plain(r, "the result")


def python_forms(sig: Signature, param_names) -> tuple:
    """The parameters as unit 1's `Param` values, in declaration order, and
    the result's form (`result_form`). Raises `NoPythonForm` for a form box 10
    does not call (BX10-27), and ValueError for one `Param` refuses, such as
    a scalar `inout`."""
    from ._args import Param
    names = tuple(param_names)
    if len(names) != len(sig.params):
        raise ValueError("%d parameter names for %d parameters" % (len(names), len(sig.params)))
    params = []
    for name, p in zip(names, sig.params):
        t = p.type
        elem = t.elem if isinstance(t, Array) else t
        if not isinstance(elem, Scalar):
            raise _no_form(sig, "parameter " + name, t)
        params.append(Param(name, p.mode, elem.elem, len(t.dims) if isinstance(t, Array) else 0))
    return tuple(params), result_form(sig)


def error_sets(sig: Signature) -> dict:
    """The error sets an error-union result reaches, by qualified name, each
    as ("declared", value names) or ("combined", member qualified names); a
    combined set brings its members (BX10-27, OQ-201)."""
    out = {}
    if isinstance(sig.result, ErrorUnion):
        d = sig.definitions[sig.result.index]
        if d.combined:
            for i in d.members:
                out[sig.definitions[i].name] = ("declared", sig.definitions[i].values)
            out[d.name] = ("combined", tuple(sig.definitions[i].name for i in d.members))
        else:
            out[d.name] = ("declared", d.values)
    return out


# Rendering.

def _render_bound(b: Bound, sizes) -> str:
    if b.symbol is None:
        return str(b.offset)
    name = sizes[b.symbol] if b.symbol < len(sizes) else "s%d" % b.symbol
    if b.offset == 0:
        return name
    return "%s %s %d" % (name, "+" if b.offset > 0 else "-", abs(b.offset))


def _render_type(sig: Signature, t, sizes) -> str:
    if t is None:
        return "void"
    if isinstance(t, Scalar):
        return t.elem
    if isinstance(t, Array):
        dims = []
        for d in t.dims:
            if d.form == 0:
                dims.append("_")
            elif d.form == 1:
                dims.append(_render_bound(d.bound, sizes))
            else:
                dims.append("%d%s%s" % (d.lower, "..=" if d.form == 2 else "..", _render_bound(d.bound, sizes)))
        return "%s[%s]" % (_render_type(sig, t.elem, sizes), ", ".join(dims))
    if isinstance(t, Tuple):
        return "(%s)" % ", ".join(_render_type(sig, x, sizes) for x in t.items)
    if isinstance(t, ErrorUnion):
        return "%s!%s" % (sig.definitions[t.index].name, _render_type(sig, t.value, sizes))
    return sig.definitions[t.index].name


def render(sig: Signature, name: str, size_names=(), param_names=(), kernel: bool = False) -> str:
    """The export as a declaration, such as
    `I64 paired[n, m](inout I64[n] a, I64 bias, in I64[n] b, in U8[m] bytes, in U8[m] more)`.
    A function's array parameter shows its mode, and its scalar or struct
    parameter shows a mode other than `in`; a kernel's parameters all show
    their modes, as SPEC-03 5.4 writes `in I64 dt`."""
    sizes = tuple(size_names)
    names = tuple(param_names) + tuple("p%d" % i for i in range(len(param_names), len(sig.params)))
    params = []
    for pname, p in zip(names, sig.params):
        text = _render_type(sig, p.type, sizes) + " " + pname
        if kernel or isinstance(p.type, Array) or p.mode != "in":
            text = p.mode + " " + text
        params.append(text)
    head = "kernel " + name if kernel else _render_type(sig, sig.result, sizes) + " " + name
    if sizes:
        head += "[%s]" % ", ".join(sizes)
    return "%s(%s)" % (head, ", ".join(params))
