"""Element and scalar types at the Python boundary (SPEC-03 A-13, A-13a; SPEC-01 IM-54, IM-146).

An element type is named by its CINT spelling: `I8` to `I1024`, `U8` to `U64`,
`Q<i>.<f>`, `Bool`, `T1`, and `T27`. Each carries its SPEC-01 IM-146 tag, the
fields of a `cint_type`, its element size and alignment, and its range in raw
units. `T1` and `T27` are carried as `I8` and `I64` (SPEC-05 TR-T1-6, TR-T27-4).
"""
from __future__ import annotations

from dataclasses import dataclass
import re

TAG_BOOL = 0x01
TAG_Z = 0x0F
TAG_FIXED = 0x31
TAG_T1 = 0x41
TAG_T27 = 0x42
SIGNED_TAGS = {8: 0x11, 16: 0x12, 32: 0x13, 64: 0x14, 128: 0x15, 256: 0x16, 512: 0x17, 1024: 0x18}
UNSIGNED_TAGS = {8: 0x21, 16: 0x22, 32: 0x23, 64: 0x24}
T27_MAX = (3 ** 27 - 1) // 2          # 3,812,798,742,493 (SPEC-05 TR-T27-1)

_INT = re.compile(r"([IU])([1-9][0-9]*)\Z")
_FIXED = re.compile(r"Q([1-9][0-9]*)\.(0|[1-9][0-9]*)\Z")     # the spelling of SPEC-01 IM-54


@dataclass(frozen=True)
class Elem:
    """One element or scalar type. `lo` and `hi` bound the raw value: the
    storage integer for fixed point, the carrier for `T1` and `T27`."""
    name: str
    tag: int
    storage: int          # fixed point: the storage tag; otherwise 0
    frac_bits: int        # fixed point: f; otherwise 0
    size: int             # bytes per element
    signed: bool
    lo: int
    hi: int
    kind: str             # "int", "fixed", "bool", "t1", or "t27"

    @property
    def align(self) -> int:
        """A-13a: the size for elements of up to 8 bytes, 8 for wider ones."""
        return min(self.size, 8)

    @property
    def wide(self) -> bool:
        return self.size > 8

    @property
    def token(self) -> str:
        """The type component of an operation identifier (SPEC-01 IM-130)."""
        if self.kind == "fixed":
            return self.name.lower().replace(".", "_")
        return self.name.lower()

    def cint_type(self) -> tuple:
        """The fields `code`, `storage`, `frac_bits`, and `record_id` of a `cint_type`."""
        return (self.tag, self.storage, self.frac_bits, 0)

    def __str__(self) -> str:
        return self.name


def _integer(signed: bool, bits: int) -> Elem:
    tags = SIGNED_TAGS if signed else UNSIGNED_TAGS
    if bits not in tags:
        raise ValueError("%s%d is not an integer type of cint-core-1" % ("I" if signed else "U", bits))
    lo, hi = (-(1 << (bits - 1)), (1 << (bits - 1)) - 1) if signed else (0, (1 << bits) - 1)
    return Elem("%s%d" % ("I" if signed else "U", bits), tags[bits], 0, 0, bits // 8, signed, lo, hi, "int")


def parse_elem(name) -> Elem:
    """The element type a string names. ValueError for a spelling that names no
    type the boundary can carry, with the reason."""
    if isinstance(name, Elem):
        return name
    if not isinstance(name, str):
        raise TypeError("an element type is a string such as \"I64\" or \"Q32.32\", not %r" % (name,))
    m = _INT.match(name)
    if m:
        return _integer(m.group(1) == "I", int(m.group(2)))
    m = _FIXED.match(name)
    if m:
        i, f = int(m.group(1)), int(m.group(2))
        w = i + f
        if w not in (8, 16, 32, 64):
            raise ValueError("%s is not a fixed-point type: i + f must be 8, 16, 32, or 64 (SPEC-01 IM-54)" % name)
        store = _integer(True, w)
        return Elem(name, TAG_FIXED, store.tag, f, store.size, True, store.lo, store.hi, "fixed")
    if name == "Bool":
        return Elem(name, TAG_BOOL, 0, 0, 1, False, 0, 1, "bool")
    if name == "T1":
        return Elem(name, TAG_T1, 0, 0, 1, True, -1, 1, "t1")
    if name == "T27":
        return Elem(name, TAG_T27, 0, 0, 8, True, -T27_MAX, T27_MAX, "t27")
    if name in ("PT5", "PT4"):
        raise ValueError("%s: packed trit arrays have no element code in this ABI yet (SPEC-03 A-13, "
                         "SPEC-05 X-6)" % name)
    if name == "Str":
        raise ValueError("Str crosses as an in U8[n] parameter: pass bytes, for example s.encode(\"utf-8\")")
    raise ValueError("unknown element type %r; examples: \"I64\", \"U8\", \"Q32.32\", \"Bool\", \"T27\"" % (name,))


def integer_elem(signed: bool, size: int) -> Elem:
    """The element P-3 maps a signedness and an item size in bytes to."""
    return _integer(signed, 8 * size)


def contains(elem: Elem, raw: int) -> bool:
    return elem.lo <= raw <= elem.hi
