"""`cint.Fixed`: an exact fixed-point scalar (SPEC-03 P-21a, P-24; SPEC-01 IM-54; SPEC-04 LS-207).

A `Fixed` holds a fixed-point type `Q<i>.<f>` and its raw storage integer. Its
value is `raw / 2^f`, exactly. It is a value carrier, not a number type: it has
no arithmetic, `int(x)` is refused (P-21a), and there is no implicit float
conversion (`cint.to_float` is the explicit one, X-10).
"""
from __future__ import annotations

from fractions import Fraction
import numbers

from ._elem import Elem, parse_elem
from ._errors import Refused


def _exact_int(x):
    """An integer argument as a Python int: an int or another Integral type,
    never a bool (Python's or NumPy's)."""
    if isinstance(x, bool) or getattr(getattr(x, "dtype", None), "kind", None) == "b":
        return None
    if isinstance(x, numbers.Integral):
        return int(x)
    return None


class Fixed:
    """An exact fixed-point scalar. Give exactly one of `raw=` (the storage
    integer) and `value=` (an `int` or `fractions.Fraction` that the type
    represents exactly): `Fixed("Q32.32", raw=429496730)` or
    `Fixed("Q32.32", value=1)`. A value out of range, or one that needs
    rounding, is refused with `E_NARROW` (SPEC-03 P-21a)."""

    __slots__ = ("_elem", "_raw")

    def __init__(self, elem, *, raw=None, value=None):
        e = parse_elem(elem)
        if e.kind != "fixed":
            raise ValueError("Fixed takes a fixed-point type Q<i>.<f>, such as \"Q32.32\", not %s" % e)
        if (raw is None) == (value is None):
            raise TypeError("Fixed takes exactly one of raw= (the storage integer) and value= "
                            "(an int or fractions.Fraction)")
        if raw is not None:
            r = _exact_int(raw)
            if r is None:
                raise TypeError("raw= is the storage integer, an int, not %s" % type(raw).__name__)
        else:
            r = _scaled(e, value)
        if not e.lo <= r <= e.hi:
            bound = e.hi if r > e.hi else e.lo
            raise Refused("%s is outside %s: its raw values run from %d to %d"
                          % ("raw %d" % r if raw is not None else "value %s" % _rational_text(value), e,
                             e.lo, e.hi),
                          code="E_NARROW", exact=r, limit=Fixed._make(e, bound), limit_type=e.name)
        object.__setattr__(self, "_elem", e)
        object.__setattr__(self, "_raw", r)

    @classmethod
    def _make(cls, elem: Elem, raw: int) -> "Fixed":
        """A Fixed of a raw value already known to be in range."""
        x = object.__new__(cls)
        object.__setattr__(x, "_elem", elem)
        object.__setattr__(x, "_raw", raw)
        return x

    def __setattr__(self, name, value):
        raise AttributeError("a Fixed is immutable")

    @property
    def elem(self) -> str:
        """The type, for example `"Q32.32"`."""
        return self._elem.name

    @property
    def raw(self) -> int:
        """The raw storage integer."""
        return self._raw

    @property
    def frac_bits(self) -> int:
        return self._elem.frac_bits

    def as_fraction(self) -> Fraction:
        """The exact value, `raw / 2^f`."""
        return Fraction(self._raw, 1 << self._elem.frac_bits)

    def __str__(self) -> str:
        """The exact decimal expansion with at least one fractional digit
        (SPEC-04 LS-207). It always terminates: the scale is a power of two."""
        f = self._elem.frac_bits
        sign = "-" if self._raw < 0 else ""
        whole, part = divmod(abs(self._raw), 1 << f)
        digits = str(part * 5 ** f).rjust(f, "0").rstrip("0") if f else ""
        return "%s%d.%s" % (sign, whole, digits or "0")

    def __repr__(self) -> str:
        return "cint.Fixed(%r, raw=%d)" % (self._elem.name, self._raw)

    def __format__(self, spec: str) -> str:
        if spec:
            raise TypeError("a Fixed formats only as its exact decimal; format x.as_fraction() or x.raw instead")
        return str(self)

    def __eq__(self, other):
        if not isinstance(other, Fixed):
            return NotImplemented
        return self._elem == other._elem and self._raw == other._raw

    def __hash__(self):
        return hash((self._elem.name, self._raw))

    def _same(self, other):
        if not isinstance(other, Fixed):
            return NotImplemented
        if self._elem != other._elem:
            raise TypeError("cannot order %s and %s: compare as_fraction() values" % (self.elem, other.elem))
        return other._raw

    def __lt__(self, other):
        r = self._same(other)
        return r if r is NotImplemented else self._raw < r

    def __le__(self, other):
        r = self._same(other)
        return r if r is NotImplemented else self._raw <= r

    def __gt__(self, other):
        r = self._same(other)
        return r if r is NotImplemented else self._raw > r

    def __ge__(self, other):
        r = self._same(other)
        return r if r is NotImplemented else self._raw >= r

    def __bool__(self) -> bool:
        return self._raw != 0

    def __int__(self):
        raise TypeError("int() of a Fixed is refused (SPEC-03 P-21a): use x.raw for the storage integer, "
                        "or x.as_fraction() for the exact value")

    def __float__(self):
        raise TypeError("a Fixed has no implicit float conversion (SPEC-03 X-10, X-11): use cint.to_float "
                        "on an array, or convert x.as_fraction() yourself")

    def __reduce__(self):
        return (_from_raw, (self._elem.name, self._raw))


def _from_raw(elem: str, raw: int) -> Fixed:
    return Fixed(elem, raw=raw)


def _rational_text(value) -> str:
    return str(value) if isinstance(value, (int, Fraction)) else repr(value)


def _scaled(e: Elem, value) -> int:
    """The raw integer of an exact `value=`: refused with `E_NARROW` when the
    value is not a multiple of 2^-f, and with `E_UNSUPPORTED` for a float
    (SPEC-03 P-21a, P-23)."""
    from ._floats import is_float_like
    if isinstance(value, bool) or getattr(getattr(value, "dtype", None), "kind", None) == "b":
        raise TypeError("value= is an int or a fractions.Fraction, not a bool")
    if is_float_like(value):
        raise Refused("a %s is never converted implicitly (SPEC-03 P-23, X-1): give value= an int or a "
                      "fractions.Fraction, or convert an array with cint.from_float(a, %r)"
                      % (type(value).__name__, e.name))
    n = _exact_int(value)
    if n is not None:
        return n << e.frac_bits
    if not isinstance(value, numbers.Rational):
        raise TypeError("value= is an int or a fractions.Fraction, not %s" % type(value).__name__)
    q = Fraction(value.numerator, value.denominator) * (1 << e.frac_bits)
    if q.denominator != 1:
        raise Refused("value %s is not a multiple of 2^-%d, so %s does not represent it exactly"
                      % (value, e.frac_bits, e), code="E_NARROW")
    return q.numerator
