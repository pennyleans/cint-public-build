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

from .arith import ROUND_MODES, OpenCase, _isqrt_round, op_id, round_div
from .faults import Fault
from .types import IntType, Value, int_type, is_int_type

__all__ = ["QType", "q_type", "q_from_ident", "is_q_type", "op", "convert"]

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
    # SPEC-01 5.1: i >= 1, f >= 0, i + f in {8, 16, 32, 64} (wider storage is Proposed).
    if i < 1 or f < 0 or i + f not in (8, 16, 32, 64):
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


def op(name: str, form: str, type_: str, *operands, mode: str | None = None):
    """`mul`, `div` and `sqrt` on `Q<i>.<f>` (SPEC-01 5.3 to 5.5). The mode is always named (9.8)."""
    q = q_type(type_)
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


def convert(form: str, source: str, target: str, x, mode: str | None = None):
    """`as` and `as%` where the source or the target is fixed point (SPEC-01 5.6)."""
    if form not in ("checked", "wrap"):
        raise ValueError("only `as` and `as%` are Specified (SPEC-01 4.9)")
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
        if t.contains(r):
            return Value(t.name, r)
        return Fault("E_NARROW", operation, (v,), r, Value(t.name, t.max if r > t.max else t.min))
    r = v.value << dst_q.f                                    # integer to fixed point, exact
    if form == "checked" and not dst_q.storage.contains(r):
        # E_NARROW, but `exact` may be the integer or the raw value (ref/OPEN.md O-8).
        raise OpenCase("exact of an out-of-range integer to fixed-point conversion is not stated (ref/OPEN.md O-8)")
    return _form(dst_q, form, r, operation, (v,), code="E_NARROW")


def _two(operands):
    if len(operands) != 2:
        raise ValueError("expected 2 operands, got %d" % len(operands))
    return operands
