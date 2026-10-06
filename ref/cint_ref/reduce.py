"""Named reductions over a one-dimensional sequence (SPEC-01 section 6.2).

Elements are taken in logical order (6.1). `sum` forms the exact total in Z and
range-checks it once; `fold_checked` and `sum_sat` are left folds; `sum_wrap`
reduces the exact total modulo 2^w. A `sum` fault record carries `n` (an `I64`);
a `fold_checked` fault carries the index, the accumulator and the element (IM-83;
box 09 ruling R5). The view identity operand (tag `61`, SPEC-01 11.1) is not
formed until arrays have canonical buffer identifiers (IM-81).
"""
from __future__ import annotations

from .arith import op_id
from .faults import Fault
from .types import Value, int_type

__all__ = ["op", "NAMES"]

NAMES = ("sum", "fold_checked", "sum_wrap", "sum_sat", "reduce_min", "reduce_max")


def op(name: str, form: str, elem: str, xs, init=None, target: str | None = None):
    t = int_type(elem)
    xs = list(xs)
    for x in xs:
        if isinstance(x, bool) or not isinstance(x, int) or not t.contains(x):
            raise ValueError("element %r is not a %s" % (x, elem))
    if init is not None and (isinstance(init, bool) or not isinstance(init, int) or not t.contains(init)):
        raise ValueError("init %r is not a %s" % (init, elem))
    n = Value("I64", len(xs))

    if name == "sum":
        _need(form == "checked" and init is None, "sum is sum.checked.<E>.<R> over one sequence")
        r = int_type(target or elem)
        total = sum(xs)
        operation = op_id("sum", "checked", [elem, r.name])        # second type always present (9.8)
        if r.contains(total):
            return Value(r.name, total)
        return Fault("E_OVERFLOW", operation, (n,), total, Value(r.name, r.max if total > r.max else r.min))

    _need(target is None, "only sum names a result type")
    if name == "fold_checked":
        _need(form in ("add", "mul") and init is not None, "fold_checked.<add|mul>.<E> takes init and a sequence")
        acc = init
        for j, x in enumerate(xs):
            v = acc + x if form == "add" else acc * x
            if not t.contains(v):
                return Fault("E_OVERFLOW", op_id("fold_checked", form, [elem]),
                             (Value("I64", j), Value(elem, acc), Value(elem, x)),
                             v, Value(elem, t.max if v > t.max else t.min), index=j)
            acc = v
        return Value(elem, acc)
    if name == "sum_wrap":
        _need(form == "wrap" and init is None, "sum_wrap is sum_wrap.wrap.<E>")
        return Value(elem, t.wrap(sum(xs)))
    if name == "sum_sat":
        _need(form == "sat" and init is None, "sum_sat is sum_sat.sat.<E>")
        acc = 0
        for x in xs:
            acc = t.sat(acc + x)
        return Value(elem, acc)
    if name in ("reduce_min", "reduce_max"):
        _need(form == "checked", "%s is %s.checked.<E>" % (name, name))
        pool = xs + ([init] if init is not None else [])
        if not pool:
            return Fault("E_SHAPE", op_id(name, "checked", [elem]), (n,))
        return Value(elem, min(pool) if name == "reduce_min" else max(pool))
    raise ValueError("unknown reduction %r" % (name,))


def _need(cond, message):
    if not cond:
        raise ValueError(message)
