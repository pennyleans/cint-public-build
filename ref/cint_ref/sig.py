"""Canonical type signatures of exports (SPEC-03 5.6, A-20 to A-22; box 10 default BX10-23).

The canonical type signature of an exported function or kernel is the bytes of A-20: the
domain string `cint-core-1/type-signature/v1`, the definitions of the structs, enums and
error sets it names (A-21), the count of size parameters or shape symbols, each parameter's
mode and type, then the result. It holds no parameter or size names, no kind, no effect
class and no `where` clause (A-22).

The type model below covers every form A-20 lists: `Bool` and the integers of SPEC-01
IM-146, `Q<i>.<f>`, `T1` and `T27`, arrays with the four shape forms, structs (with their
layout and bit widths), enums, error-set values, declared and combined error sets, error
unions and tuples. `encode` writes a `Signature` of the model and refuses one that A-20
does not admit, such as an array result or a tuple of one element. `program_exports` maps
cint_ref's checked program to the model for each export of the root module, the exports
that get a `cx` wrapper under the interim rule of A-12, and gives no type signature to an
export whose type holds a form of A-25. Declared, combined and built-in
error sets and error unions map from the checker (roadmap box 12). cint_ref's surface has
no enums, tuples, fixed point, ternary types, bit fields or `@packed` (parser.py refuses
them), so those forms are reached through the model alone; ref/tests/test_sig.py builds
the examples of SPEC-03 5.6 that use them. SPEC-09 CONF-16 compares these bytes with the
reflection table that the compiler under test writes (tools/cint_check.py).
"""
from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import NamedTuple

from . import parser as P
from .check import VOID, ArrayType, StructType, check_module, module_name, shape_key
from .check import ErrorSet as CheckedSet, ErrorUnion as CheckedUnion
from .exec import _with_large_stack, loader
from .lexer import decode_source
from .types import INT_TYPES

__all__ = ["DOMAIN", "Fixed", "Bound", "Shape", "ANY", "Array", "Field", "Struct", "Enum", "ErrorSet",
           "ErrorValue", "ErrorUnion", "Tuple", "Signature", "Export", "NoCarrier", "encode",
           "program_exports", "literal", "symbol"]

DOMAIN = b"cint-core-1/type-signature/v1"

# The scalar tags of SPEC-01 IM-146 that A-20 admits, by type name.
SCALAR_TAGS = {"Bool": 0x01, "I8": 0x11, "I16": 0x12, "I32": 0x13, "I64": 0x14, "I128": 0x15, "I256": 0x16,
               "I512": 0x17, "I1024": 0x18, "U8": 0x21, "U16": 0x22, "U32": 0x23, "U64": 0x24,
               "T1": 0x41, "T27": 0x42}
TAG_FIXED, TAG_ARRAY, TAG_STRUCT, TAG_ENUM, TAG_ERROR, TAG_UNION, TAG_TUPLE = (
    0x31, 0x52, 0x71, 0x72, 0x73, 0x74, 0x76)
MODES = {"in": 0x00, "inout": 0x01, "out": 0x02}
SHAPE_FORMS = {"any": 0x00, "extent": 0x01, "inclusive": 0x02, "exclusive": 0x03}
NO_SYMBOL = 0xFFFFFFFF
MAX_RANK = 4                                    # SPEC-02 V-2
FIXED_STORAGE = ("I8", "I16", "I32", "I64")     # the storage of `Q<i>.<f>` (IM-146)
ENUM_UNDERLYING = ("I8", "I16", "I32", "I64", "U8", "U16", "U32", "U64")
ERROR_UNDERLYING = ("U8", "U16", "U32", "U64")  # SPEC-04 LS-93
IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")
I64_MIN, I64_MAX = -(1 << 63), (1 << 63) - 1


# --------------------------------------------------------------------------- the type model
# A scalar is its type name: "Bool", "I8" to "I1024", "U8" to "U64", "T1" or "T27".

@dataclass
class Fixed:
    """`Q<i>.<f>` (tag 31): the storage type, `I8` to `I64`, and `f`, the fractional bits."""
    storage: str
    frac: int


@dataclass
class Bound:
    """An extent or bound `B` of A-20: the value of size symbol `symbol` (its index among the
    size parameters or shape symbols; None for none) plus `offset`."""
    symbol: int | None
    offset: int


@dataclass
class Shape:
    """The shape of one dimension (A-20): `any` (`_`), `extent` B with lower bound 0,
    `inclusive` (`L ..= B`) or `exclusive` (`L .. B`), where L is `lower`."""
    form: str
    bound: Bound | None = None
    lower: int = 0


ANY = Shape("any")


def literal(n: int) -> Shape:
    """The extent `n`, a constant: (none, n)."""
    return Shape("extent", Bound(None, n))


def symbol(index: int, offset: int = 0) -> Shape:
    """The extent of size symbol `index` plus `offset`: `n`, `n + 1`, `n - 1` (SPEC-02 K-1)."""
    return Shape("extent", Bound(index, offset))


@dataclass
class Array:
    """An array (tag 52): its element type, never an array, and a shape per dimension."""
    elem: object
    dims: list


@dataclass(eq=False)
class Field:
    name: str
    type: object
    bits: int = 0       # the bit width, 0 when the field is not a bit field (SPEC-04 LS-81)


@dataclass(eq=False)
class Struct:
    """A struct definition (tag 71): its qualified name, its fields in declaration order, and
    its layout, SPEC-04 LS-73 or `@packed` (LS-75)."""
    name: str
    fields: list = field(default_factory=list)
    packed: bool = False


@dataclass(eq=False)
class Enum:
    """An enum definition (tag 72): its qualified name, underlying integer type, and
    enumerators [(name, value)] in declaration order."""
    name: str
    underlying: str
    values: list


@dataclass(eq=False)
class ErrorSet:
    """An error set definition (tag 73): a declared set lists its value names in tag order
    from tag 1 (SPEC-04 LS-93); a combined set (LS-97) lists its operand sets as written."""
    name: str
    values: list | None = None
    operands: list | None = None
    underlying: str = "U16"

    def members(self) -> list:
        """The declared sets whose values a combined set holds, each once, in the order that
        numbers it (A-21): an operand that is itself combined contributes its own members in
        place, and a set already listed is not listed again."""
        if self.operands is None:
            return [self]
        out = []
        for op in self.operands:
            for m in op.members():
                if not any(m is x for x in out):
                    out.append(m)
        return out


@dataclass
class ErrorValue:
    """A value of an error set (tag 73)."""
    set: ErrorSet


@dataclass
class ErrorUnion:
    """`E!T` (tag 74): the set, and the success type, None for `void`, a type, or a Tuple."""
    set: ErrorSet
    success: object = None


@dataclass
class Tuple:
    """A tuple (tag 76) of at least two element types."""
    elems: list


@dataclass
class Signature:
    """What a type signature holds (A-22): the count of size parameters or shape symbols, each
    parameter as (mode, type) with mode `in`, `inout` or `out`, and the result, None for none
    (`void`, and every kernel)."""
    size_count: int
    params: list
    result: object = None


# --------------------------------------------------------------------------- the encoding

def _u32(n: int) -> bytes:
    if not 0 <= n <= 0xFFFFFFFF:
        raise ValueError("%d is not a U32" % n)
    return n.to_bytes(4, "little")


def _i64(n: int) -> bytes:
    if not I64_MIN <= n <= I64_MAX:
        raise ValueError("%d is not an I64" % n)
    return n.to_bytes(8, "little", signed=True)


def _ident(name: str) -> bytes:
    """A name (A-20): U32 length and UTF-8; an identifier of SPEC-04 LS-15 is ASCII."""
    if not isinstance(name, str) or not IDENT.match(name) or len(name) > 255:
        raise ValueError("%r is not an identifier (SPEC-04 LS-15)" % (name,))
    return _u32(len(name)) + name.encode("ascii")


def _qualified(name: str) -> bytes:
    """A qualified name: identifiers joined by full stops (A-20)."""
    if not isinstance(name, str):
        raise ValueError("%r is not a qualified name" % (name,))
    for part in name.split("."):
        _ident(part)
    return _u32(len(name)) + name.encode("ascii")


def _width(name: str) -> int:
    return INT_TYPES[name].width


class _Encoder:
    """One type signature: a depth-first visit numbers the definitions (A-21), then the bytes
    are written with those numbers."""

    def __init__(self, sig: Signature):
        self.sig = sig
        self.defs = []          # definitions, in the order the visit completes them
        self.index = {}         # (tag, qualified name) -> index in defs
        self.open = {}          # (tag, qualified name) -> a struct whose fields are being visited

    # -- the visit (A-21), with the positions of A-20 --------------------------------------
    def visit(self, t, where: str):
        """Visit type t, at position `where` (param, field, element, tuple, result, success)."""
        if isinstance(t, str):
            if t not in SCALAR_TAGS:
                raise ValueError("%r is not a type of A-20" % (t,))
        elif isinstance(t, Fixed):
            if t.storage not in FIXED_STORAGE:
                raise ValueError("the storage of a fixed-point type is I8 to I64, not %r" % (t.storage,))
            if not 0 <= t.frac <= _width(t.storage) - 1:
                raise ValueError("Q with %d fractional bits in %s: at most the storage width less 1"
                                 % (t.frac, t.storage))
        elif isinstance(t, Array):
            if where not in ("param", "field"):
                raise ValueError("an array is a parameter or a struct field (A-20), not a %s" % where)
            if not 1 <= len(t.dims) <= MAX_RANK:
                raise ValueError("an array of rank %d: the rank is 1 to %d (SPEC-02 V-2)" % (len(t.dims), MAX_RANK))
            for s in t.dims:
                if s.form not in SHAPE_FORMS:
                    raise ValueError("unknown shape form %r" % (s.form,))
                if where == "field" and (s.form == "any" or s.bound.symbol is not None):
                    raise ValueError("a struct field's array has literal extents only (A-20)")
            self.visit(t.elem, "element")
        elif isinstance(t, (Struct, Enum)):
            self.define(t)
        elif isinstance(t, ErrorValue):
            self.define(t.set)
        elif isinstance(t, ErrorUnion):
            if where != "result":
                raise ValueError("an error union is a result only (A-20), not a %s" % where)
            self.define(t.set)
            if t.success is not None:
                self.visit(t.success, "success")
        elif isinstance(t, Tuple):
            if where not in ("result", "success"):
                raise ValueError("a tuple is a result or the success type of an error union (A-20), not a %s"
                                 % where)
            if len(t.elems) < 2:
                raise ValueError("a tuple has at least two elements (A-20)")
            for e in t.elems:
                self.visit(e, "tuple")
        else:
            raise ValueError("%r is not a type of the model" % (t,))

    def define(self, d):
        """Visit definition d once: what it refers to first, then d itself (A-21)."""
        tag = TAG_STRUCT if isinstance(d, Struct) else TAG_ENUM if isinstance(d, Enum) else TAG_ERROR
        key = (tag, d.name)
        if key in self.index or key in self.open:
            if (self.defs[self.index[key]] if key in self.index else self.open[key]) is not d:
                raise ValueError("two definitions of one kind are named %s (A-24)" % d.name)
            if key in self.open:
                raise ValueError("struct %s contains itself" % d.name)
            return
        if isinstance(d, Struct):
            self.open[key] = d
            names = set()
            for f in d.fields:
                if f.name in names:
                    raise ValueError("struct %s has two fields named %s" % (d.name, f.name))
                names.add(f.name)
                self.visit(f.type, "field")
                self.bit_width(d, f)
            del self.open[key]
        elif isinstance(d, Enum):
            if d.underlying not in ENUM_UNDERLYING:
                raise ValueError("enum %s: the underlying type is I8 to I64 or U8 to U64" % d.name)
            if len({n for n, _ in d.values}) != len(d.values) or len({v for _, v in d.values}) != len(d.values):
                raise ValueError("enum %s has two enumerators with one name or one value" % d.name)
        else:
            if d.underlying not in ERROR_UNDERLYING:
                raise ValueError("error set %s: the underlying type is U8 to U64 (SPEC-04 LS-93)" % d.name)
            if (d.values is None) == (d.operands is None):
                raise ValueError("error set %s is declared (values) or combined (operands)" % d.name)
            if d.values is not None and len(set(d.values)) != len(d.values):
                raise ValueError("error set %s has two values with one name" % d.name)
            if d.operands is not None:
                if not d.operands:
                    raise ValueError("combined error set %s has no operand" % d.name)
                for m in d.members():
                    self.define(m)
            count = sum(len(m.values) for m in d.members())
            if count > (1 << _width(d.underlying)) - 1:
                raise ValueError("error set %s has %d values, more than %s numbers" % (d.name, count, d.underlying))
        self.index[key] = len(self.defs)
        self.defs.append(d)

    def bit_width(self, s: Struct, f: Field):
        """A-24's rules on bit widths, after SPEC-04 LS-81 and LS-82."""
        if f.bits == 0:
            return
        if f.type == "Bool":
            ok = f.bits == 1
        elif isinstance(f.type, str) and f.type in INT_TYPES:
            ok = 1 <= f.bits <= _width(f.type)
        else:
            ok = False      # an array or struct field (LS-82), or a form LS-81 gives no bit field
        if not ok or f.bits > 255:
            raise ValueError("field %s.%s: bit width %d is not admitted for its type" % (s.name, f.name, f.bits))

    # -- the bytes (A-20, A-21) ---------------------------------------------------------------
    def ref(self, tag: int, d) -> bytes:
        return bytes([tag]) + _u32(self.index[(tag, d.name)])

    def type_bytes(self, t) -> bytes:
        if isinstance(t, str):
            return bytes([SCALAR_TAGS[t]])
        if isinstance(t, Fixed):
            return bytes([TAG_FIXED, SCALAR_TAGS[t.storage]]) + t.frac.to_bytes(2, "little")
        if isinstance(t, Array):
            return (bytes([TAG_ARRAY]) + self.type_bytes(t.elem) + bytes([len(t.dims)])
                    + b"".join(self.shape_bytes(s) for s in t.dims))
        if isinstance(t, Struct):
            return self.ref(TAG_STRUCT, t)
        if isinstance(t, Enum):
            return self.ref(TAG_ENUM, t)
        if isinstance(t, ErrorValue):
            return self.ref(TAG_ERROR, t.set)
        if isinstance(t, ErrorUnion):
            return (bytes([TAG_UNION]) + _u32(self.index[(TAG_ERROR, t.set.name)])
                    + (b"\x00" if t.success is None else self.type_bytes(t.success)))
        return bytes([TAG_TUPLE]) + _u32(len(t.elems)) + b"".join(self.type_bytes(e) for e in t.elems)

    def shape_bytes(self, s: Shape) -> bytes:
        out = bytes([SHAPE_FORMS[s.form]])
        if s.form == "any":
            return out
        if s.form != "extent":
            out += _i64(s.lower)
        b = s.bound
        if b.symbol is not None and not 0 <= b.symbol < self.sig.size_count:
            raise ValueError("size symbol %d of %d (A-24)" % (b.symbol, self.sig.size_count))
        return out + _u32(NO_SYMBOL if b.symbol is None else b.symbol) + _i64(b.offset)

    def definition_bytes(self, d) -> bytes:
        if isinstance(d, Struct):
            out = bytes([TAG_STRUCT]) + _qualified(d.name) + bytes([1 if d.packed else 0]) + _u32(len(d.fields))
            for f in d.fields:
                out += _ident(f.name) + bytes([f.bits]) + self.type_bytes(f.type)
            return out
        if isinstance(d, Enum):
            width = _width(d.underlying)
            out = bytes([TAG_ENUM]) + _qualified(d.name) + bytes([SCALAR_TAGS[d.underlying]]) + _u32(len(d.values))
            for name, v in d.values:
                if not INT_TYPES[d.underlying].contains(v):
                    raise ValueError("enumerator %s.%s = %d is not a %s" % (d.name, name, v, d.underlying))
                out += _ident(name) + v.to_bytes(width // 8, "little", signed=v < 0)
            return out
        out = bytes([TAG_ERROR]) + _qualified(d.name) + bytes([SCALAR_TAGS[d.underlying]])
        if d.values is not None:
            return out + b"\x00" + _u32(len(d.values)) + b"".join(_ident(n) for n in d.values)
        members = d.members()
        return out + b"\x01" + _u32(len(members)) + b"".join(
            _u32(self.index[(TAG_ERROR, m.name)]) for m in members)

    def encode(self) -> bytes:
        sig = self.sig
        for mode, t in sig.params:
            if mode not in MODES:
                raise ValueError("unknown mode %r" % (mode,))
            self.visit(t, "param")
        if sig.result is not None:
            self.visit(sig.result, "result")
        out = _u32(len(DOMAIN)) + DOMAIN + _u32(len(self.defs))
        out += b"".join(self.definition_bytes(d) for d in self.defs)
        out += _u32(sig.size_count) + _u32(len(sig.params))
        for mode, t in sig.params:
            out += bytes([MODES[mode]]) + self.type_bytes(t)
        return out + (b"\x00" if sig.result is None else self.type_bytes(sig.result))


def encode(sig: Signature) -> bytes:
    """The canonical type signature of `sig` (A-20 to A-22); ValueError for a signature that
    A-20 does not admit."""
    return _Encoder(sig).encode()


# --------------------------------------------------------------------------- cint_ref's exports

class NoCarrier(Exception):
    """A type that A-20 cannot describe (A-25): the export gets no wrapper and no row."""


class Export(NamedTuple):
    name: str
    kind: str                   # function or kernel (A-19)
    signature: bytes | None     # None for an export with a form of A-25
    reason: str = ""            # why there is no signature


class _Mapper:
    """cint_ref's checked types (check.py) as types of the model."""

    def __init__(self, module_of: dict):
        self.module_of = module_of  # id(StructDecl) -> the dotted name of its module (SPEC-04 LS-225)
        self.structs = {}       # id(StructType) -> Struct
        self.sets = {}          # id(check.ErrorSet) -> ErrorSet

    def type(self, t, sizes: list):
        if isinstance(t, ArrayType):
            return Array(self.type(t.elem, []), [self.dim(d, sizes) for d in t.dims])
        if isinstance(t, StructType):
            return self.struct(t)
        if isinstance(t, CheckedSet):
            return ErrorValue(self.error_set(t))
        if isinstance(t, CheckedUnion):
            return ErrorUnion(self.error_set(t.set), None if t.value == VOID else self.type(t.value, sizes))
        if isinstance(t, str) and t in SCALAR_TAGS:
            return t
        raise NoCarrier("the type %s has no carrier (SPEC-03 A-25)" % (t,))

    def struct(self, st: StructType) -> Struct:
        s = self.structs.get(id(st))
        if s is None:
            s = self.structs[id(st)] = Struct("%s.%s" % (self.module_of[id(st.decl)], st.name))
            s.fields = [Field(name, self.type(ft, [])) for name, ft in st.fields.items()]
        return s

    def error_set(self, s: CheckedSet) -> ErrorSet:
        """A set as the checker numbered it (SPEC-04 LS-93, LS-97): a declared or built-in set
        lists its values, a combined set its operand sets as written (A-21). Its name is the
        qualified one; a built-in set has its name alone (A-20)."""
        e = self.sets.get(id(s))
        if e is None:
            e = self.sets[id(s)] = ErrorSet(s.qualname, underlying=s.underlying)
            if s.operands is None:
                e.values = [v.name for v in s.values]
            else:
                e.operands = [self.error_set(o) for o in s.operands]
        return e

    def dim(self, d, sizes: list) -> Shape:
        """A dimension of check.py (Dim) as a shape of A-20. A constant extent, a literal or a
        constant expression, is (none, its value), as cintc writes it; a size parameter or
        shape symbol `n` is (n, 0); a kernel's `n + c` and `n - c` are (n, c) and (n, -c)."""
        if d.extent is not None:
            return literal(d.extent)
        if d.size is not None:
            return symbol(sizes.index(d.size))
        if d.expr is None:
            return ANY
        key = shape_key(d.expr)
        if key is not None and key[0] == "b" and key[2] in sizes:
            return symbol(sizes.index(key[2]), key[3] if key[1] == "+" else -key[3])
        if key is not None and key[0] == "n" and key[1] in sizes:
            return symbol(sizes.index(key[1]))
        raise NoCarrier("an extent outside the shape_expr of SPEC-02 K-1 (SPEC-03 A-25)")


def _export(mapper: _Mapper, d, kind: str) -> Export:
    sizes = [name for name, _ in d.size_params]
    try:
        params = [(p.mode, mapper.type(t, sizes)) for p, t in zip(d.params, d.param_types)]
        result = None
        if kind == "function" and d.result_type != "void":
            if isinstance(d.result_type, ArrayType):
                raise NoCarrier("an array result (SPEC-03 A-25)")
            result = mapper.type(d.result_type, sizes)
        return Export(d.name, kind, encode(Signature(len(sizes), params, result)))
    except NoCarrier as e:
        return Export(d.name, kind, None, str(e))
    except ValueError as e:     # a form that A-20 does not admit, such as an offset beyond I64
        return Export(d.name, kind, None, "not encodable: %s" % e)


def _program_exports(source: bytes, path: str, root):
    modules = []
    read = loader(root)

    def load(rel):
        m = read(rel)
        if m is not None:
            modules.append(m)
        return m

    module = P.parse_module(decode_source(source, path), path)
    modules.append(module)
    check_module(module, load)
    mapper = _Mapper({id(s): module_name(m.path) for m in modules for s in m.structs})
    decls = [(f.name_pos, f, "function") for f in module.functions if f.exported]
    decls += [(k.name_pos, k, "kernel") for k in module.kernels if k.exported]
    decls.sort(key=lambda x: (x[0].line, x[0].column))
    return [_export(mapper, d, kind) for _, d, kind in decls]


def program_exports(source: bytes, path: str, root: str | None = None) -> list:
    """The exports of the root module at module-relative `path` (its imports read from `root`,
    SPEC-04 LS-225), in declaration order, as Export rows: under the interim rule of SPEC-03
    A-12 each gets a `cx` wrapper and a row of the reflection table (A-19) unless its
    signature is None. A compile error or a refusal of the program raises CompileError or
    Refused. Runs on a large stack, as run_program does."""
    return _with_large_stack(lambda: _program_exports(source, path, root))
