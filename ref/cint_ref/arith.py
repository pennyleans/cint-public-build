"""Exact integer arithmetic of SPEC-01 section 4 on Python integers.

`op(name, form, type_, *operands, mode=None, target=None)` returns either a
`Value` or a `Fault`. Every form first computes the exact mathematical result
`v` in Z (SPEC-01 4.1), then applies the form: checked faults with `exact` and
`limit`, wrapping reduces modulo 2^w, saturating clamps. Operation identifiers
are spelled as SPEC-01 9.8 defines them.

Operands may be plain ints (typed `type_`; a shift count is then typed `I64`,
as a literal count is, SPEC-01 3.2) or `Value`s carrying their own type.
"""
from __future__ import annotations

import math

from .faults import Fault
from .types import BOOL, IntType, Value, int_type, unsigned_of

__all__ = ["op", "Value", "Fault", "OpenCase", "ROUND_MODES", "round_div", "op_id"]

# SPEC-01 4.5, in the order of the built-in enum `Round` (SPEC-04 6.8).
ROUND_MODES = ("floor", "ceil", "trunc", "away", "half_even", "half_away",
               "half_trunc", "half_up", "half_down")

FORMS = ("checked", "wrap", "sat")


class OpenCase(Exception):
    """An input cint_ref decides nothing for: the specification leaves it Open or Proposed, or
    cint_ref does not implement it yet (ref/OPEN.md)."""


def op_id(name: str, form: str, types, mode=None) -> str:
    parts = [name, form] + [t.lower() for t in types]
    if mode is not None:
        parts.append(mode)
    return ".".join(parts)


def round_div(n: int, d: int, mode: str) -> int:
    """The rational n/d (d != 0) rounded once by `mode` (SPEC-01 4.5)."""
    if d == 0:
        raise ZeroDivisionError
    if mode not in ROUND_MODES:
        raise ValueError("unknown rounding mode: %r" % (mode,))
    if d < 0:
        n, d = -n, -d
    q, r = divmod(n, d)           # q = floor(n/d), 0 <= r < d
    if r == 0:
        return q
    positive = n > 0              # the rational is positive (it is nonzero here)
    if mode == "floor":
        return q
    if mode == "ceil":
        return q + 1
    if mode == "trunc":
        return q if positive else q + 1
    if mode == "away":
        return q + 1 if positive else q
    twice = 2 * r
    if twice < d:
        return q
    if twice > d:
        return q + 1
    # exact tie between q and q + 1
    if mode == "half_even":
        return q if q % 2 == 0 else q + 1
    if mode == "half_away":
        return q + 1 if positive else q
    if mode == "half_trunc":
        return q if positive else q + 1
    if mode == "half_up":
        return q + 1
    return q                      # half_down


def _isqrt_round(x: int, mode: str) -> int:
    r = math.isqrt(x)
    if r * r == x:
        return r
    if mode in ("floor", "trunc"):
        return r
    if mode in ("ceil", "away"):
        return r + 1
    # half modes: sqrt(x) is irrational here, so there are no ties;
    # round up exactly when x > (r + 1/2)^2, that is x > r^2 + r.
    return r + 1 if x > r * r + r else r


def _typed(x, default_type: str) -> Value:
    v = x if isinstance(x, Value) else Value(default_type, x)
    if v.type == BOOL:
        if not isinstance(v.value, bool):
            raise ValueError("Bool operand must be a bool: %r" % (v,))
        return v
    if isinstance(v.value, bool) or not isinstance(v.value, int):
        raise ValueError("integer operand required: %r" % (v,))
    t = int_type(v.type)
    if not t.contains(v.value):
        raise ValueError("operand %d is outside %s" % (v.value, v.type))
    return v


def _checked(t: IntType, v: int, operation: str, operands) -> Value | Fault:
    if t.contains(v):
        return Value(t.name, v)
    limit = Value(t.name, t.max if v > t.max else t.min)
    return Fault("E_OVERFLOW", operation, tuple(operands), v, limit)


def _apply_form(t: IntType, form: str, v: int, operation: str, operands):
    if form == "checked":
        return _checked(t, v, operation, operands)
    if form == "wrap":
        return Value(t.name, t.wrap(v))
    if form == "sat":
        return Value(t.name, t.sat(v))
    raise ValueError("unknown form: %r" % (form,))


def _need(cond: bool, message: str):
    if not cond:
        raise ValueError(message)


def op(name: str, form: str, type_: str, *operands, mode: str | None = None,
       target: str | None = None):
    _need(form in FORMS, "unknown form %r" % (form,))
    if name == "as":
        return _convert(form, type_, operands, target, mode)
    t = int_type(type_)
    _need(mode is None or mode in ROUND_MODES, "unknown rounding mode %r" % (mode,))

    if name in ("add", "sub", "mul"):
        a, b = (_typed(x, type_) for x in _arity(operands, 2))
        _same(t, a, b)
        v = a.value + b.value if name == "add" else a.value - b.value if name == "sub" else a.value * b.value
        return _apply_form(t, form, v, op_id(name, form, [type_]), (a, b))

    if name == "neg":
        _need(form == "checked", "negation has only the checked form; write 0 -% a or 0 -| a")
        (a,) = (_typed(x, type_) for x in _arity(operands, 1))
        _same(t, a)
        return _checked(t, -a.value, op_id("neg", "checked", [type_]), (a,))

    if name in ("div", "rem", "div_trunc", "rem_trunc", "div_euclid", "rem_euclid"):
        _need(form == "checked", "there is no wrapping or saturating division (SPEC-01 4.4)")
        a, b = (_typed(x, type_) for x in _arity(operands, 2))
        _same(t, a, b)
        operation = op_id(name, "checked", [type_])
        if b.value == 0:
            return Fault("E_DIV_ZERO", operation, (a, b))
        q, r = _divide(name, a.value, b.value)
        if name.startswith("div"):
            return _checked(t, q, operation, (a, b))
        return _checked(t, r, operation, (a, b))

    if name == "div_round":
        _need(form == "checked", "div_round has only the checked form")
        _need(mode is not None, "div_round needs a rounding mode")
        a, b = (_typed(x, type_) for x in _arity(operands, 2))
        _same(t, a, b)
        operation = op_id(name, "checked", [type_], mode)
        if b.value == 0:
            return Fault("E_DIV_ZERO", operation, (a, b))
        return _checked(t, round_div(a.value, b.value, mode), operation, (a, b))

    if name in ("shl", "shr"):
        _need(form in (("checked", "wrap") if name == "shl" else ("checked",)),
              "no %s form of %s" % (form, name))
        a_raw, k_raw = _arity(operands, 2)
        a = _typed(a_raw, type_)
        k = _typed(k_raw, "I64")
        _same(t, a)
        _need(k.type != BOOL, "shift count must be an integer")
        operation = op_id(name, form, [type_])
        if not 0 <= k.value <= t.width - 1:
            return Fault("E_SHIFT", operation, (a, k), None, Value("I64", t.width - 1))
        if name == "shr":
            return Value(t.name, a.value >> k.value)
        return _apply_form(t, form, a.value << k.value, operation, (a, k))

    if name in ("rotl", "rotr"):
        _need(form == "checked", "rotations have only the checked form")
        _need(not t.signed, "rotations are defined on unsigned types only (SPEC-01 2.4, 4.6)")
        if t.width > 64:
            raise OpenCase("rotations above U64 are Proposed (SPEC-01 2.4)")
        a_raw, k_raw = _arity(operands, 2)
        a = _typed(a_raw, type_)
        k = _typed(k_raw, "I64")
        operation = op_id(name, "checked", [type_])
        if not 0 <= k.value <= t.width - 1:
            return Fault("E_SHIFT", operation, (a, k), None, Value("I64", t.width - 1))
        s = k.value if name == "rotl" else (t.width - k.value) % t.width
        return Value(t.name, t.wrap((a.value << s) | (a.value >> (t.width - s))) if s else a.value)

    if name in ("and", "or", "xor"):
        a, b = (_typed(x, type_) for x in _arity(operands, 2))
        _same(t, a, b)
        v = a.value & b.value if name == "and" else a.value | b.value if name == "or" else a.value ^ b.value
        return Value(t.name, t.wrap(v))

    if name == "not":
        (a,) = (_typed(x, type_) for x in _arity(operands, 1))
        _same(t, a)
        return Value(t.name, t.wrap(~a.value))

    if name == "abs":
        _need(form == "checked", "abs has only the checked form")
        (a,) = (_typed(x, type_) for x in _arity(operands, 1))
        _same(t, a)
        return _checked(t, abs(a.value), op_id("abs", "checked", [type_]), (a,))

    if name == "uabs":
        _need(t.signed, "uabs is defined on signed types only (SPEC-01 2.4, 4.3)")
        if t.width > 64:
            raise OpenCase("uabs above I64 is Proposed: it needs U128 and wider (SPEC-01 2.4)")
        (a,) = (_typed(x, type_) for x in _arity(operands, 1))
        return Value(unsigned_of(t).name, abs(a.value))

    if name in ("min", "max"):
        a, b = (_typed(x, type_) for x in _arity(operands, 2))
        _same(t, a, b)
        return Value(t.name, min(a.value, b.value) if name == "min" else max(a.value, b.value))

    if name == "clamp":
        x, lo, hi = (_typed(v, type_) for v in _arity(operands, 3))
        _same(t, x, lo, hi)
        if lo.value > hi.value:
            raise OpenCase("clamp with lo > hi at run time faults E_DOMAIN (SPEC-01 IM-49), which cint_ref does not "
                           "implement yet (ref/OPEN.md REF-OQ-15)")
        return Value(t.name, min(max(x.value, lo.value), hi.value))

    if name == "mul_full":
        # SPEC-01 2.4 and 4.10: Specified for I8..I512 and, with the same signedness,
        # U8..U32; U64 to U128 is Proposed.
        if not t.signed and t.width == 64:
            raise OpenCase("mul_full on U64 (result U128) is Proposed (SPEC-01 2.4)")
        _need(t.width <= (512 if t.signed else 32), "mul_full is Specified for I8..I512 and U8..U32 (SPEC-01 2.4)")
        a, b = (_typed(x, type_) for x in _arity(operands, 2))
        _same(t, a, b)
        return Value("%s%d" % ("I" if t.signed else "U", 2 * t.width), a.value * b.value)

    if name == "muldiv":
        _need(mode is not None and target is not None, "muldiv needs a result type and a mode")
        r_t = int_type(target)
        a, b, c = (_typed(x, type_) for x in _arity(operands, 3))
        _same(t, a, b, c)
        operation = op_id("muldiv", "checked", [type_, target], mode)
        if c.value == 0:
            return Fault("E_DIV_ZERO", operation, (a, b, c))
        return _checked(r_t, round_div(a.value * b.value, c.value, mode), operation, (a, b, c))

    if name in ("isqrt", "isqrt_round"):
        _need(name == "isqrt" or mode is not None, "isqrt_round needs a rounding mode")
        (a,) = (_typed(x, type_) for x in _arity(operands, 1))
        _same(t, a)
        operation = op_id(name, "checked", [type_], mode if name == "isqrt_round" else None)
        if a.value < 0:
            return Fault("E_DOMAIN", operation, (a,))     # interim decision OQ-01
        v = math.isqrt(a.value) if name == "isqrt" else _isqrt_round(a.value, mode)
        return _checked(t, v, operation, (a,))

    raise ValueError("unknown operation: %r" % (name,))


def _divide(name: str, a: int, b: int):
    if name in ("div", "rem"):
        q = a // b                      # Python floors (SPEC-01 4.4)
    elif name in ("div_trunc", "rem_trunc"):
        q = abs(a) // abs(b)
        if (a < 0) != (b < 0):
            q = -q
    else:                               # Euclidean: 0 <= r < |b|
        q = a // b if b > 0 else -(a // -b)
    return q, a - b * q


def _convert(form, source, operands, target, mode):
    _need(target is not None, "conversion needs a target type")
    _need(mode is None, "integer conversions do not round")
    (x,) = _arity(operands, 1)
    v = _typed(x, source)
    t = int_type(target)
    operation = op_id("as", form, [source, target])
    if v.type == BOOL:
        return Value(t.name, 1 if v.value else 0)
    if form == "checked":
        if t.contains(v.value):
            return Value(t.name, v.value)
        return Fault("E_NARROW", operation, (v,), v.value, Value(t.name, t.max if v.value > t.max else t.min))
    if form == "wrap":
        return Value(t.name, t.wrap(v.value))
    raise OpenCase("saturating conversion `as|` is Proposed and not implemented (SPEC-01 2.4)")


def _arity(operands, n):
    if len(operands) != n:
        raise ValueError("expected %d operands, got %d" % (n, len(operands)))
    return operands


def _same(t: IntType, *vals):
    for v in vals:
        if v.type != t.name:
            raise ValueError("operand type %s differs from %s" % (v.type, t.name))
