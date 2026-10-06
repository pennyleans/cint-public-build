"""Numeric types of the integer machine (SPEC-01 section 2) and typed values.

Every value is a Python int together with the name of its type. Ranges come
from SPEC-01 2.1 (`MIN(T)`, `MAX(T)`); `wrap` and `sat` are the functions of
SPEC-01 1.2. No floating point is used anywhere in this package.
"""
from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class IntType:
    name: str
    signed: bool
    width: int

    @property
    def min(self) -> int:
        return -(1 << (self.width - 1)) if self.signed else 0

    @property
    def max(self) -> int:
        return (1 << (self.width - 1)) - 1 if self.signed else (1 << self.width) - 1

    @property
    def ident(self) -> str:
        """The lowercase spelling used in operation identifiers (SPEC-01 9.8)."""
        return self.name.lower()

    def contains(self, v: int) -> bool:
        """`in(T, v)` of SPEC-01 1.2."""
        return self.min <= v <= self.max

    def wrap(self, v: int) -> int:
        """`wrap(T, v)`: the unique r in range with r = v (mod 2^w)."""
        r = v & ((1 << self.width) - 1)
        if self.signed and r > self.max:
            r -= 1 << self.width
        return r

    def sat(self, v: int) -> int:
        """`sat(T, v)`: clamp to [MIN(T), MAX(T)]."""
        return self.min if v < self.min else self.max if v > self.max else v

    def bits(self, v: int) -> int:
        """The two's complement storage bits of `v` as an unsigned value."""
        return v & ((1 << self.width) - 1)


_ALL = [IntType("I%d" % w, True, w) for w in (8, 16, 32, 64, 128, 256, 512, 1024)]
_ALL += [IntType("U%d" % w, False, w) for w in (8, 16, 32, 64)]
INT_TYPES = {t.name: t for t in _ALL}

# Types of the scalar surface that conformance programs use (I8..I64, U8..U64).
SCALAR_INT_NAMES = ("I8", "I16", "I32", "I64", "U8", "U16", "U32", "U64")
WIDE_INT_NAMES = ("I128", "I256", "I512", "I1024")

BOOL = "Bool"
Z = "Z"   # mathematical integer; appears only as a fault-record value


def int_type(name: str) -> IntType:
    try:
        return INT_TYPES[name]
    except KeyError:
        raise ValueError("not a binary integer type: %r" % (name,)) from None


def is_int_type(name) -> bool:
    return isinstance(name, str) and name in INT_TYPES


def type_from_ident(ident: str) -> str:
    """Map an operation-identifier type component (`i64`) back to its name (`I64`)."""
    if ident == "bool":
        return BOOL
    name = ident.upper()
    if name in INT_TYPES:
        return name
    raise ValueError("unknown or unsupported type component: %r" % (ident,))


def unsigned_of(t: IntType) -> IntType:
    """The unsigned type of the same width (SPEC-01 4.3, `uabs`)."""
    return int_type("U%d" % t.width)


@dataclass(frozen=True)
class Value:
    """A typed value: `type` is a type name (`"I64"`, `"Bool"`), `value` an int or bool."""
    type: str
    value: object

    def render(self) -> str:
        """`<Type> <decimal>`, the typed-value form of `.expect` files (SPEC-09 CONF-01)."""
        if self.type == BOOL:
            return "Bool " + ("true" if self.value else "false")
        return "%s %d" % (self.type, self.value)
