"""Signed binary fixed point `Q<i>.<f>` (SPEC-01 section 5) on Python integers.

A fixed-point value is held as its raw storage integer, `Value("Q16.16", raw)`.
Multiplication, division and square root form the exact rational result in an
unbounded intermediate (Python ints are at least as wide as the `2w` that
SPEC-01 5.1 requires), round it once by the named mode (4.5), then apply the
operator form to the raw value. No floating point is used.
"""
from __future__ import annotations

import re
from dataclasses import dataclass
from fractions import Fraction

from .arith import ROUND_MODES, _isqrt_round, op_id, round_div
from .faults import Fault
from .types import IntType, Value, int_type, is_int_type

__all__ = ["QType", "q_type", "q_from_ident", "is_q_type", "op", "convert", "literal_raw", "exact_text",
           "rounded_text"]

_Q = re.compile(r"Q([1-9][0-9]*)\.(0|[1-9][0-9]*)\Z")
_Q_IDENT = re.compile(r"q([1-9][0-9]*)_(0|[1-9][0-9]*)\Z")


@dataclass(frozen=True)
class QType:
    i: int
    f: int

    @property
    def name(self) -> str:
        return "Q%d.%d" % (self.i, self.f)

    @property
    def ident(self) -> str:
        """Spelling in operation identifiers: Q16.16 is q16_16 (SPEC-01 9.8)."""
        return "q%d_%d" % (self.i, self.f)

    @property
    def width(self) -> int:
        return self.i + self.f

    @property
    def storage(self) -> IntType:
        """The signed integer of width `w` (SPEC-01 5.1)."""
        return int_type("I%d" % self.width)


def _make(i: int, f: int) -> QType:
    # SPEC-01 5.1: i >= 1, f >= 0, i + f in {8, 16, 32, 64, 128} (IM-54; 128 by decision
    # 2026-10-15 on OQ-292); storage above 128 bits is Proposed (IM-59).
    if i < 1 or f < 0 or i + f not in (8, 16, 32, 64, 128):
        raise ValueError("Q%d.%d is not a legal fixed-point type (SPEC-01 5.1)" % (i, f))
    return QType(i, f)


def q_type(name: str) -> QType:
    m = _Q.match(name) if isinstance(name, str) else None
    if not m:
        raise ValueError("not a fixed-point type: %r" % (name,))
    return _make(int(m.group(1)), int(m.group(2)))


def q_from_ident(ident: str) -> QType:
    m = _Q_IDENT.match(ident) if isinstance(ident, str) else None
    if not m:
        raise ValueError("not a fixed-point type component: %r" % (ident,))
    return _make(int(m.group(1)), int(m.group(2)))


def is_q_type(name) -> bool:
    try:
        q_type(name)
        return True
    except ValueError:
        return False


def _raw(x, q: QType) -> Value:
    v = x if isinstance(x, Value) else Value(q.name, x)
    if v.type != q.name:
        raise ValueError("operand type %s differs from %s" % (v.type, q.name))
    if isinstance(v.value, bool) or not isinstance(v.value, int) or not q.storage.contains(v.value):
        raise ValueError("raw value %r is outside the storage of %s" % (v.value, q.name))
    return v


def _form(q: QType, form: str, r: int, operation: str, operands, code="E_OVERFLOW"):
    """The operator form applied to a raw result (SPEC-01 5.1 "Overflow")."""
    s = q.storage
    if form == "checked":
        if s.contains(r):
            return Value(q.name, r)
        return Fault(code, operation, tuple(operands), r, Value(q.name, s.max if r > s.max else s.min))
    if form == "wrap":
        return Value(q.name, s.wrap(r))
    if form == "sat":
        return Value(q.name, s.sat(r))
    raise ValueError("unknown form %r" % (form,))


def op(name: str, form: str, type_: str, *operands, mode: str | None = None, target: str | None = None):
    """An operation on `Q<i>.<f>`. `add`, `sub`, `neg`, `abs`, `min` and `max` act on the
    raw values with the integer semantics of SPEC-01 section 4 (IM-60); `mul_int` multiplies
    by an integer exactly (IM-64); `qmul` multiplies two formats into `target` (IM-63);
    `mul`, `div` and `sqrt` (5.3 to 5.5) round once and always name the mode (9.8)."""
    q = q_type(type_)
    if name in ("add", "sub", "neg", "abs", "min", "max"):
        return _raw_op(name, form, q, operands)
    if name == "mul_int":
        if form != "checked":
            raise ValueError("mul_int has only the checked form")
        a_raw, n = _two(operands)
        a = _raw(a_raw, q)
        if not isinstance(n, Value) or not is_int_type(n.type):
            raise ValueError("mul_int multiplies by an integer: %r" % (n,))
        return _form(q, form, a.value * n.value, op_id("mul_int", "checked", [q.ident, n.type]), (a, n))
    if name == "qmul":
        if form != "checked":
            raise ValueError("qmul has only the checked form")
        a, b = _two(operands)
        qa, qb, r = q_type(a.type), q_type(b.type), q_type(target)
        a, b = _raw(a, qa), _raw(b, qb)
        shift = qa.f + qb.f - r.f
        if shift > 0 and mode not in ROUND_MODES:
            raise ValueError("this qmul rounds and names its mode (SPEC-01 IM-63)")
        if shift <= 0 and mode is not None:
            raise ValueError("this qmul is exact and names no mode (SPEC-01 IM-63, IM-133)")
        p = a.value * b.value
        raw = round_div(p, 1 << shift, mode) if shift > 0 else p << -shift
        return _form(r, form, raw, op_id("qmul", "checked", [qa.ident, qb.ident, r.ident], mode), (a, b))
    if mode not in ROUND_MODES:
        raise ValueError("a fixed-point %s rounds and names its mode (SPEC-01 9.8): %r" % (name, mode))
    operation = op_id(name, form, [q.ident], mode)
    if name == "mul":
        if form not in ("checked", "wrap", "sat"):
            raise ValueError("unknown form %r" % (form,))
        a, b = (_raw(x, q) for x in _two(operands))
        return _form(q, form, round_div(a.value * b.value, 1 << q.f, mode), operation, (a, b))
    if name == "div":
        # SPEC-01 5.4 and 9.8 name only `/` and `div(a, b, mode)`: the checked form.
        if form != "checked":
            raise ValueError("fixed-point division has only the checked form")
        a, b = (_raw(x, q) for x in _two(operands))
        if b.value == 0:
            return Fault("E_DIV_ZERO", operation, (a, b))      # before any other check
        return _form(q, form, round_div(a.value << q.f, b.value, mode), operation, (a, b))
    if name == "sqrt":
        if form not in ("checked", "wrap", "sat"):
            raise ValueError("unknown form %r" % (form,))
        if len(operands) != 1:
            raise ValueError("sqrt takes one operand")
        a = _raw(operands[0], q)
        if a.value < 0:
            return Fault("E_DOMAIN", operation, (a,))         # SPEC-01 4.10; interim decision OQ-01
        return _form(q, form, _isqrt_round(a.value << q.f, mode), operation, (a,))
    raise ValueError("unknown fixed-point operation %r" % (name,))


def _raw_op(name: str, form: str, q: QType, operands):
    """IM-60: the operation on the raw storage values, as on the storage integer type, with
    the fixed-point type in the operation identifier and the limit."""
    vals = [_raw(x, q) for x in operands]
    if len(vals) != (1 if name in ("neg", "abs") else 2):
        raise ValueError("%s takes %d operands" % (name, 1 if name in ("neg", "abs") else 2))
    if name in ("neg", "abs", "min", "max") and form != "checked":
        raise ValueError("%s has only the checked form" % name)
    raw = [v.value for v in vals]
    if name in ("min", "max"):
        return Value(q.name, min(raw) if name == "min" else max(raw))
    r = raw[0] + raw[1] if name == "add" else raw[0] - raw[1] if name == "sub" else -raw[0] if name == "neg" \
        else abs(raw[0])
    return _form(q, form, r, op_id(name, form, [q.ident]), vals)


def convert(form: str, source: str, target: str, x, mode: str | None = None):
    """`as`, `as%` and `as|` where the source or the target is fixed point (SPEC-01 5.6):
    `as|` gives `sat(T, v)` of the value `as` would range-check (IM-68; OQ-272)."""
    if form not in ("checked", "wrap", "sat"):
        raise ValueError("unknown form %r" % (form,))
    src_q = q_type(source) if is_q_type(source) else None
    dst_q = q_type(target) if is_q_type(target) else None
    if src_q is None and dst_q is None:
        raise ValueError("neither %s nor %s is fixed point" % (source, target))
    if src_q is None and not is_int_type(source) or dst_q is None and not is_int_type(target):
        raise ValueError("unsupported conversion %s to %s" % (source, target))

    # Does the conversion round? Q to Q with g < f, and Q to integer, round (5.6).
    rounds = src_q is not None and (dst_q is None or dst_q.f < src_q.f)
    if rounds and mode not in ROUND_MODES:
        raise ValueError("this conversion rounds and names its mode (SPEC-01 9.8)")
    if not rounds and mode is not None:
        raise ValueError("this conversion is exact and names no mode (SPEC-01 9.8)")

    ids = [src_q.ident if src_q else source.lower(), dst_q.ident if dst_q else target.lower()]
    operation = op_id("as", form, ids, mode)
    if src_q is not None:
        v = _raw(x, src_q)
    else:
        v = x if isinstance(x, Value) else Value(source, x)
        if v.type != source or not int_type(source).contains(v.value):
            raise ValueError("operand %r is not a %s" % (v, source))

    if src_q is not None and dst_q is not None:
        r = v.value << (dst_q.f - src_q.f) if dst_q.f >= src_q.f else round_div(v.value, 1 << (src_q.f - dst_q.f), mode)
        return _form(dst_q, form, r, operation, (v,), code="E_NARROW")
    if src_q is not None:                                     # fixed point to integer
        t = int_type(target)
        r = round_div(v.value, 1 << src_q.f, mode)
        if form == "wrap":
            return Value(t.name, t.wrap(r))
        if form == "sat":
            return Value(t.name, t.sat(r))
        if t.contains(r):
            return Value(t.name, r)
        return Fault("E_NARROW", operation, (v,), r, Value(t.name, t.max if r > t.max else t.min))
    # Integer to fixed point is exact: `exact` is the scaled raw value n * 2^f, and `limit`
    # the target's bound as a fixed-point value (IM-106; decision 2026-10-03 on OQ-147).
    r = v.value << dst_q.f
    return _form(dst_q, form, r, operation, (v,), code="E_NARROW")


def literal_raw(value, type_: str, mode: str | None = None):
    """The raw value of a literal of exact rational `value` in the context `type_` (SPEC-01
    IM-27, SPEC-04 LS-37): `value * 2^f` when it is an integer, else rounded once by the
    `round` clause's `mode`; None when it is off the grid and no mode is given (C2006). The
    caller checks the storage range (C2003)."""
    q = q_type(type_)
    v = Fraction(value)
    n = v.numerator << q.f
    if n % v.denominator == 0:
        return n // v.denominator
    if mode is None:
        return None
    return round_div(n, v.denominator, mode)


def exact_text(raw: int, f: int) -> str:
    """`raw / 2^f` as the shortest exact decimal with at least one fractional digit (SPEC-04
    LS-207): the expansion terminates within `f` digits."""
    neg = raw < 0
    a = -raw if neg else raw
    whole, rest = divmod(a, 1 << f)
    digits = ""
    while rest:
        rest *= 10
        digits += str(rest >> f)
        rest &= (1 << f) - 1
    return ("-" if neg else "") + "%d.%s" % (whole, digits or "0")


def rounded_text(raw: int, f: int, precision: int) -> str:
    """`raw / 2^f` rounded half to even to `precision` fractional digits, printed with exactly
    that many; no minus sign when the rounded value is zero (SPEC-04 LS-208)."""
    scaled = round_div(raw * 10 ** precision, 1 << f, "half_even")
    neg = scaled < 0
    a = -scaled if neg else scaled
    whole, frac = divmod(a, 10 ** precision)
    text = "%d.%0*d" % (whole, precision, frac) if precision else "%d" % whole
    return ("-" if neg and a else "") + text


def _two(operands):
    if len(operands) != 2:
        raise ValueError("expected 2 operands, got %d" % len(operands))
    return operands
