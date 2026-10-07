"""The float boundary of the Python bridge (SPEC-03 section 8, X-1 to X-11; SPEC-01 IM-70 to IM-72).

This is the one module of `python/cint` that the float-token lint exempts
(`tools/cint_lint.py`, BX10-18). It handles binary64 and binary32 values only
as bit patterns: each input is decoded into an integer significand `m` and
exponent `e`, so its value is exactly `m * 2^e` (X-3), and every result is
computed in Python integers, so none depends on the floating-point
environment (IM-72). It also recognizes floating-point arguments, so that the
rest of the bridge refuses them (P-23) without naming the type.
"""
from __future__ import annotations

import array
from dataclasses import dataclass
import numbers
import struct

from ._elem import Elem, parse_elem
from ._errors import Refused

# SPEC-01 4.5, in the order of the built-in enum `Round` (SPEC-04 6.8).
ROUND_MODES = ("floor", "ceil", "trunc", "away", "half_even", "half_away", "half_trunc", "half_up", "half_down")
OVERFLOW_POLICIES = ("refuse", "saturate")
# Names a caller may reach for; the refusal names the SPEC-01 mode (X-4).
_MODE_HINTS = {"toward_zero": "trunc", "towards_zero": "trunc", "round_toward_zero": "trunc", "truncate": "trunc",
               "down": "floor", "toward_negative": "floor", "up": "ceil", "toward_positive": "ceil",
               "nearest": "half_even", "nearest_even": "half_even", "round_half_even": "half_even",
               "ties_to_even": "half_even", "ties_away": "half_away", "away_from_zero": "away"}
# IEEE 754 interchange formats accepted by X-2: width -> (precision, exponent bits, bias).
FORMATS = {64: (53, 11, 1023), 32: (24, 8, 127)}
DTYPES = {"float64": 64, "float32": 32}


@dataclass(frozen=True)
class FloatPolicy:
    """How floats become CINT values (SPEC-03 X-4, X-6): a rounding mode named
    as in SPEC-01 4.5 (default `half_even`) and an overflow policy, `refuse`
    (default) or `saturate`. An unknown name is refused with `E_UNSUPPORTED`
    when the policy is made, before any conversion."""
    rounding: str = "half_even"
    overflow: str = "refuse"

    def __post_init__(self):
        if not isinstance(self.rounding, str) or self.rounding not in ROUND_MODES:
            hint = _MODE_HINTS.get(self.rounding) if isinstance(self.rounding, str) else None
            raise Refused("unknown rounding mode %r%s; the modes of SPEC-01 4.5 are %s (SPEC-03 X-4)"
                          % (self.rounding, "; use %r" % hint if hint else "", ", ".join(ROUND_MODES)))
        if not isinstance(self.overflow, str) or self.overflow not in OVERFLOW_POLICIES:
            raise Refused("unknown overflow policy %r: it is 'refuse' or 'saturate', and there is no wrapping "
                          "policy (SPEC-03 X-6)" % (self.overflow,))

    @property
    def form(self) -> str:
        return "checked" if self.overflow == "refuse" else "sat"


@dataclass(frozen=True)
class FloatConversion:
    """What one bulk conversion did, held by the `Buffer` it made: the source
    format, the policy, the element count, and how many elements were clamped
    under `saturate`. The conversion audit record of X-9 needs a recorder,
    which arrives with recordings."""
    source: str           # "binary64", "binary32", or "mixed" (a sequence of both)
    rounding: str
    overflow: str
    elements: int
    saturated: int


def round_div(n: int, d: int, mode: str) -> int:
    """The rational n/d (d > 0) rounded once by `mode` (SPEC-01 4.5)."""
    q, r = divmod(n, d)                   # q = floor(n/d), 0 <= r < d
    if r == 0:
        return q
    positive = n > 0
    if mode == "floor":
        return q
    if mode == "ceil":
        return q + 1
    if mode == "trunc":
        return q if positive else q + 1
    if mode == "away":
        return q + 1 if positive else q
    if 2 * r != d:
        return q + 1 if 2 * r > d else q
    if mode == "half_even":
        return q if q % 2 == 0 else q + 1
    if mode == "half_away":
        return q + 1 if positive else q
    if mode == "half_trunc":
        return q if positive else q + 1
    if mode == "half_up":
        return q + 1
    return q                              # half_down


def decode(bits: int, width: int):
    """(m, e) with value exactly m * 2^e for finite bits (X-3, X-7: negative
    zero gives m = 0, and subnormals decode exactly); None for NaN and the
    infinities."""
    p, ebits, bias = FORMATS[width]
    biased = (bits >> (p - 1)) & ((1 << ebits) - 1)
    frac = bits & ((1 << (p - 1)) - 1)
    if biased == (1 << ebits) - 1:
        return None
    if biased == 0:
        m, e = frac, 1 - bias - (p - 1)
    else:
        m, e = frac | (1 << (p - 1)), biased - bias - (p - 1)
    return (-m if bits >> (width - 1) else m), e


def _non_finite_name(bits: int, width: int) -> str:
    p = FORMATS[width][0]
    if bits & ((1 << (p - 1)) - 1):
        return "NaN"
    return "-inf" if bits >> (width - 1) else "+inf"


def _bits_text(bits: int, width: int) -> str:
    return "0x%0*x" % (width // 4, bits)


def target(elem) -> Elem:
    """The target of a float conversion: an integer or fixed-point type (X-1)."""
    e = parse_elem(elem)
    if e.kind not in ("int", "fixed"):
        raise Refused("a float conversion targets an integer or fixed-point type, not %s (SPEC-03 X-1)" % e)
    return e


@dataclass(frozen=True)
class _Rejection:
    message: str
    reason: str | None
    operands: tuple
    operand_types: tuple
    exact: int | None
    limit: int | None


def convert(bits: int, width: int, e: Elem, policy: FloatPolicy):
    """One element: (raw, saturated) or a _Rejection. The value m * 2^e is
    scaled to m * 2^(e + f), rounded once, then range-checked (X-3, X-6)."""
    if width not in FORMATS:
        raise ValueError("binary%d is not an accepted input format (SPEC-03 X-2)" % width)
    bit_type = "U%d" % width
    d = decode(bits, width)
    if d is None:
        return _Rejection("%s (bits %s) is never converted, under either overflow policy (SPEC-03 X-5)"
                          % (_non_finite_name(bits, width), _bits_text(bits, width)), "non_finite",
                          (bits,), (bit_type,), None, None)
    m, exp = d
    k = exp + e.frac_bits
    raw = m << k if k >= 0 else round_div(m, 1 << -k, policy.rounding)
    if e.lo <= raw <= e.hi:
        return raw, False
    bound = e.hi if raw > e.hi else e.lo
    if policy.overflow == "saturate":
        return bound, True
    return _Rejection("%s * 2^%d (bits %s) rounds to raw %d, outside %s (raw %d to %d) (SPEC-03 X-6)"
                      % (m, exp, _bits_text(bits, width), raw, e, e.lo, e.hi), None,
                      (bits, m, exp), (bit_type, "Z", "Z"), raw, bound)


def operation(width: int, e: Elem, policy: FloatPolicy) -> str:
    """The SPEC-01 IM-130 identifier of a host float conversion."""
    return "from_f%d.%s.%s.%s" % (width, policy.form, e.token, policy.rounding)


def unravel(index: int, shape: tuple) -> tuple:
    """The row-major index tuple of a linear element index."""
    out = []
    for n in reversed(shape):
        index, i = divmod(index, n)
        out.append(i)
    return tuple(reversed(out))


def convert_many(items, shape: tuple, e: Elem, policy: FloatPolicy):
    """A bulk conversion, all or nothing (X-8): `items` are (bits, width) in
    row-major order. Returns (raws, FloatConversion); refuses naming the first
    refused element and the number refused."""
    from ._fixed import Fixed
    raws, first, refused, saturated, widths = [], None, 0, 0, set()
    for i, (bits, width) in enumerate(items):
        widths.add(width)
        r = convert(bits, width, e, policy)
        if isinstance(r, _Rejection):
            refused += 1
            if first is None:
                first = (i, width, r)
            continue
        raws.append(r[0])
        saturated += r[1]
    if first is not None:
        i, width, r = first
        index = unravel(i, shape)
        limit = None if r.limit is None else (Fixed._make(e, r.limit) if e.kind == "fixed" else r.limit)
        raise Refused("element %s: %s; %d of %d elements refused, so no Buffer was made (SPEC-03 X-8)"
                      % (index, r.message, refused, len(raws) + refused), code="E_NARROW",
                      operation=operation(width, e, policy), operands=r.operands, operand_types=r.operand_types,
                      exact=r.exact, limit=limit, limit_type=None if limit is None else e.name, reason=r.reason,
                      index=index, count=refused)
    source = "mixed" if len(widths) > 1 else "binary%d" % (widths.pop() if widths else 64)
    return raws, FloatConversion(source, policy.rounding, policy.overflow, len(raws), saturated)


def format_width(code: str, itemsize: int):
    """The width of a floating-point buffer format character, or a refusal
    for the formats X-2 does not accept."""
    if code in ("d", "f") and itemsize in (4, 8):
        return 8 * itemsize
    names = {"e": "binary16", "g": "long double (x87 extended or binary128)"}
    raise Refused("%s input is not accepted: only binary32 and binary64 are (SPEC-03 X-2)"
                  % names.get(code, "format %r" % code))


def leaf_bits(x):
    """(bits, width) of one float in a sequence: a Python float (binary64) or a
    NumPy float32 or float64 scalar. None for anything else."""
    if isinstance(x, float):
        return int.from_bytes(struct.pack("<d", x), "little"), 64
    dt = getattr(x, "dtype", None)
    if getattr(dt, "kind", None) == "f" and getattr(x, "ndim", None) == 0:
        if dt.itemsize not in (4, 8):
            format_width("e" if dt.itemsize == 2 else "g", dt.itemsize)
        order = "big" if dt.byteorder == ">" else "little"
        return int.from_bytes(x.tobytes(), order), 8 * dt.itemsize
    return None


def is_float_like(x) -> bool:
    """A Python float or complex, a NumPy floating or complex scalar, or a
    `decimal.Decimal`: the arguments P-23 refuses for every parameter."""
    if isinstance(x, (float, complex)):
        return True
    if isinstance(x, numbers.Number) and not isinstance(x, numbers.Rational):
        return True
    dt = getattr(x, "dtype", None)
    return getattr(dt, "kind", None) in ("f", "c") and getattr(x, "ndim", None) == 0


def float_refusal(x, what: str) -> Refused:
    """The refusal of a float argument (SPEC-03 P-23, case 24)."""
    return Refused("%s: a %s is refused for every parameter (SPEC-03 P-23). A float enters CINT only "
                   "through an explicit conversion: cint.from_float(a, elem, rounding=...) for an array, "
                   "or cint.Fixed(elem, value=fractions.Fraction(...)) for an exact fixed-point value"
                   % (what, type(x).__name__))


def exact_to_bits(n: int, f: int, width: int = 64) -> int:
    """The bits of the binary64 (or binary32) value nearest to n / 2^f, ties to
    even (X-10), computed from the integers. A value beyond the format's range
    rounds to an infinity, as roundTiesToEven does; no CINT type reaches it in
    binary64."""
    if n == 0:
        return 0
    p, ebits, bias = FORMATS[width]
    sign = 1 if n < 0 else 0
    a = -n if sign else n
    top = a.bit_length() - 1 - f                     # value lies in [2^top, 2^(top + 1))
    unit = max(top - (p - 1), 1 - bias - (p - 1))    # exponent of one unit in the last place
    shift = unit + f
    m = round_div(a, 1 << shift, "half_even") if shift > 0 else a << -shift
    if m == 1 << p:                                  # rounding carried into the next binade
        m, unit = m >> 1, unit + 1
    if m >> (p - 1):
        biased, frac = unit + (p - 1) + bias, m - (1 << (p - 1))
    else:
        biased, frac = 0, m                          # subnormal: unit is the minimum
    if biased >= (1 << ebits) - 1:
        biased, frac = (1 << ebits) - 1, 0
    return (sign << (width - 1)) | (biased << (p - 1)) | frac


def to_float(obj, *, dtype: str = "float64"):
    """`cint.to_float`: each element of a `View` or `Buffer` as the nearest
    binary64 (or binary32), ties to even, computed from the exact integer or
    fixed-point value (X-10). The result is a display or export value (X-11):
    a read-only NumPy array when NumPy is installed, else a read-only
    memoryview of that format and shape (of one dimension when there are no
    elements, which a memoryview cannot shape)."""
    from ._buffer import Buffer
    from ._view import View
    if not isinstance(obj, (View, Buffer)):
        raise TypeError("cint.to_float takes a View or a Buffer, not %s" % type(obj).__name__)
    if dtype not in DTYPES:
        raise ValueError("dtype is \"float64\" or \"float32\", not %r" % (dtype,))
    width = DTYPES[dtype]
    e = obj._elem
    bits = [exact_to_bits(r, e.frac_bits, width) for r in obj._raw_values()]
    data = array.array("Q" if width == 64 else "I", bits).tobytes()
    try:
        import numpy
    except ImportError:
        code = "d" if width == 64 else "f"
        return memoryview(data).cast(code, obj.shape) if bits else memoryview(data).cast(code)
    return numpy.frombuffer(data, dtype="<f8" if width == 64 else "<f4").reshape(obj.shape)
