"""Scalar values across the Python boundary (SPEC-03 P-21 to P-24, A-13; BX10-15).

`to_raw` forms the raw value of a scalar argument for its parameter type, or
refuses the argument: no implicit conversion between numeric types, no
`bool` for an integer, no bare `int` for fixed point, and no float anywhere.
`from_raw` is the other direction: integers return as exact Python `int`,
fixed point as `cint.Fixed`, and `Bool` as `bool`.
"""
from __future__ import annotations

from ._elem import Elem, integer_elem, parse_elem
from ._errors import Refused
from ._fixed import Fixed
from ._floats import float_refusal, is_float_like


def _numpy_kind(x):
    """(kind, itemsize) of a NumPy scalar, or None."""
    dt = getattr(x, "dtype", None)
    if dt is None or getattr(x, "ndim", None) != 0 or type(x).__module__ != "numpy":
        return None
    return dt.kind, dt.itemsize


def _describe(x) -> str:
    np = _numpy_kind(x)
    if np is not None:
        return "numpy.%s(%s)" % (type(x).__name__, x)
    return "%s %r" % (type(x).__name__, x)


def _a(e: Elem) -> str:
    """The type with its article: "an I64", "a U8"."""
    return ("an " if e.name[0] == "I" else "a ") + e.name


def _range(e: Elem, value: int, what: str) -> int:
    if e.lo <= value <= e.hi:
        return value
    bound = e.hi if value > e.hi else e.lo
    return _narrow(e, value, bound, what)


def _narrow(e: Elem, value: int, bound: int, what: str):
    operation = {"t1": "decode.checked.t1", "t27": "decode.checked.t27"}.get(e.kind)
    raise Refused("%s: %d is outside %s (%d to %d)" % (what, value, e, e.lo, e.hi), code="E_NARROW",
                  operation=operation, operands=(value,), operand_types=("Z",), exact=value, limit=bound,
                  limit_type=e.name)


def to_raw(value, elem, what: str = "argument") -> int:
    """The raw value of a scalar argument for a parameter of type `elem`: the
    integer itself, the storage integer of a `Fixed`, 0 or 1 for `Bool`, and
    the carrier for `T1` and `T27`. `what` names the argument in a refusal."""
    e = parse_elem(elem)
    if is_float_like(value):
        raise float_refusal(value, what)
    np = _numpy_kind(value)
    if e.kind == "bool":
        if isinstance(value, bool):
            return int(value)
        if np is not None and np[0] == "b":
            return int(bool(value))
        raise Refused("%s: a Bool parameter takes True or False, not %s (BX10-15)" % (what, _describe(value)))
    if isinstance(value, bool) or (np is not None and np[0] == "b"):
        raise Refused("%s: a bool is not %s argument, although bool subclasses int (SPEC-03 P-21)"
                      % (what, _a(e)))
    if e.kind == "fixed":
        if isinstance(value, Fixed):
            if value.elem != e.name:
                raise Refused("%s: cint.Fixed(%r) for a %s parameter; fixed-point formats are never converted "
                              "implicitly: make cint.Fixed(%r, value=x.as_fraction()) if it is exact"
                              % (what, value.elem, e, e.name))
            return value.raw
        if isinstance(value, int) or np is not None:
            n = int(value)
            raise Refused("%s: a bare %s for a %s parameter has two readings, so it is refused (SPEC-03 P-21a): "
                          "cint.Fixed(%r, value=%d) is raw %d, and cint.Fixed(%r, raw=%d) is %d * 2^-%d"
                          % (what, _describe(value), e, e.name, n, n << e.frac_bits, e.name, n, n, e.frac_bits))
        raise Refused("%s: %s parameter takes a cint.Fixed, not %s" % (what, _a(e), _describe(value)))
    if isinstance(value, Fixed):
        raise Refused("%s: cint.Fixed(%r) for %s parameter" % (what, value.elem, _a(e)))
    if np is not None:
        kind, size = np
        if kind not in ("i", "u") or e.kind != "int" or integer_elem(kind == "i", size) != e:
            raise Refused("%s: %s for %s parameter; NumPy scalars are accepted only for the type their "
                          "dtype maps to, with no implicit conversion (SPEC-03 P-22)"
                          % (what, _describe(value), _a(e)))
        return _range(e, int(value), what)
    if isinstance(value, int):
        return _range(e, value, what)
    if isinstance(value, str):
        raise Refused("%s: a str is not a CINT value; text crosses as bytes to an in U8[n] parameter, "
                      "for example s.encode(\"utf-8\") (BX10-15)" % what)
    raise Refused("%s: %s is not a value of %s" % (what, _describe(value), e))


def from_raw(raw: int, elem):
    """The Python value of a raw scalar result (SPEC-03 P-24)."""
    e = parse_elem(elem)
    if e.kind == "fixed":
        return Fixed._make(e, raw)
    if e.kind == "bool":
        return bool(raw)
    return raw


def in_range(e: Elem, raw: int) -> bool:
    return e.lo <= raw <= e.hi

