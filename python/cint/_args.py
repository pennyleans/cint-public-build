"""Arguments of a call from Python (BX10-04, BX10-14; SPEC-03 P-21 to P-24).

`bind_arguments` places the arguments of `ctx.<export>(...)` on the export's
parameters and forms each one, before anything runs. A refusal here is
`cint.Refused` before entry: no fuel, no fault, and no state change (SPEC-03
H-12). The checks that SPEC-03 H-12 makes entry faults (element type, rank,
permission, alias) belong to the entry, not to this step.

BX10-04 (ruled B): an array argument that is neither a `View` nor a `Buffer`
is borrowed read-only for the call, with no copy, when its parameter is
`in` and `cint.borrow(obj)` accepts it with its defaults; it is released when
the entry returns. A parameter the entry may write takes only a `View` or a
`Buffer`. A list, tuple, `range`, or other object with neither the buffer
protocol nor `__dlpack__` is refused, naming `cint.copy(seq, elem=...)`.
"""
from __future__ import annotations

from dataclasses import dataclass

from ._buffer import Buffer
from ._elem import parse_elem
from ._errors import BorrowRefused, Refused, StaleHandle
from ._fixed import Fixed
from ._floats import float_refusal, is_float_like
from ._scalar import to_raw
from ._view import MAX_RANK, View, borrow

MODES = ("in", "inout", "out")


@dataclass(frozen=True)
class Param:
    """One parameter of an export: its name, its mode, its type (the element
    type of an array), and its rank, 0 for a scalar. A scalar `out` is a
    result, not an argument (BX10-14)."""
    name: str
    mode: str
    elem: str
    rank: int = 0

    def __post_init__(self):
        if self.mode not in MODES:
            raise ValueError("a parameter mode is in, inout, or out, not %r" % (self.mode,))
        if not 0 <= self.rank <= MAX_RANK:
            raise ValueError("a parameter rank is 0 to %d" % MAX_RANK)
        if self.rank == 0 and self.mode == "inout":
            raise ValueError("a scalar parameter is in, or a kernel's out, which returns as a result")
        parse_elem(self.elem)

    @property
    def written(self) -> bool:
        return self.mode != "in"

    @property
    def argument(self) -> bool:
        """Whether a caller passes it: every parameter but a scalar `out`."""
        return not (self.rank == 0 and self.mode == "out")

    def describe(self) -> str:
        if self.rank == 0:
            return "%s %s %s" % (self.mode, self.elem, self.name)
        return "%s %s[%s] %s" % (self.mode, self.elem, ", ".join("n%d" % i for i in range(self.rank)), self.name)


@dataclass
class Bound:
    """One formed argument: the raw value of a scalar, or the `View` or
    `Buffer` of an array. `borrowed` marks a View this call borrowed under
    BX10-04, which is released when the entry returns."""
    param: Param
    value: object
    borrowed: bool = False


def bind_argument(arg, param: Param) -> Bound:
    """Form one argument for its parameter, or refuse it."""
    what = "parameter %s" % param.name
    if param.rank == 0:
        if isinstance(arg, (View, Buffer)):
            raise Refused("%s is a scalar %s; pass a Python value, not a cint.%s" % (what, param.elem,
                                                                                    type(arg).__name__))
        return Bound(param, to_raw(arg, param.elem, what))
    if isinstance(arg, Buffer):
        if param.mode != "out":
            arg._check_readable()
        if param.written:
            arg._claim_output()
        else:
            arg._check_live()
        return Bound(param, arg)
    if isinstance(arg, View):
        if arg.released:
            raise StaleHandle("%s: this View was released; borrow the array again (SPEC-03 6.1)" % what)
        return Bound(param, arg)
    if param.written:
        if param.mode == "inout":
            alternatives = "cint.borrow(a, writable=True), or a Buffer from cint.copy(a)"
        else:
            alternatives = "a Buffer from cint.empty(shape, %r), or cint.borrow(a, writable=True, publish=\"copy\")" % param.elem
        raise Refused("%s is written by the entry, so it takes a View or a Buffer, never a bare argument (BX10-04): "
                      "pass %s" % (what, alternatives))
    if is_float_like(arg):
        raise float_refusal(arg, what)
    if isinstance(arg, (int, Fixed)):
        raise Refused("%s is an array of rank %d; pass a View, a Buffer, or an array such as a NumPy array"
                      % (what, param.rank))
    try:
        view = borrow(arg)
    except BorrowRefused as failure:
        if failure.reason != "protocol":
            raise
        if isinstance(arg, str):
            raise Refused("%s takes text as bytes: pass s.encode(\"utf-8\") (BX10-15)" % what) from None
        raise Refused("%s: a %s is not borrowed, and the bridge copies nothing it was not asked to (SPEC-03 P-9, "
                      "BX10-04): cint.copy(seq, elem=%r) copies it exactly"
                      % (what, type(arg).__name__, param.elem)) from None
    return Bound(param, view, borrowed=True)


class CallArguments:
    """The formed arguments of one call, in declaration order. Used as a
    context manager, it releases the views the call borrowed (BX10-04)."""

    def __init__(self, bounds: list):
        self.bounds = bounds

    def release(self):
        for b in self.bounds:
            if b.borrowed:
                b.value.release()

    def __enter__(self) -> "CallArguments":
        return self

    def __exit__(self, *exc):
        self.release()
        return False


def bind_arguments(params, args: tuple, kwargs: dict) -> CallArguments:
    """Place and form the arguments of a call (BX10-14). Positional arguments
    fill the `in` parameters in declaration order; a written argument (an
    `inout`, or a kernel's array `out`) is passed by name; a scalar `out` is a
    result. A missing written array is refused, naming `cint.empty`."""
    params = [p for p in params if p.argument]
    by_name = {p.name: p for p in params}
    positional = [p for p in params if not p.written]
    if len(args) > len(positional):
        raise TypeError("takes %d positional argument%s (%s); written arguments are passed by name"
                        % (len(positional), "" if len(positional) == 1 else "s",
                           ", ".join(p.name for p in positional)))
    given = {p.name: a for p, a in zip(positional, args)}
    for name, value in kwargs.items():
        if name not in by_name:
            raise TypeError("no parameter named %r" % name)
        if name in given:
            raise TypeError("parameter %r given twice" % name)
        given[name] = value
    for p in params:
        if p.name not in given:
            if p.written and p.rank:
                raise Refused("parameter %s is written by the entry and was not supplied: pass it by name, for "
                              "example %s=cint.empty(shape, %r) (BX10-14)" % (p.name, p.name, p.elem))
            raise TypeError("missing argument for parameter %s" % p.describe())
    bounds = []
    try:
        for p in params:
            bounds.append(bind_argument(given[p.name], p))
    except BaseException:
        CallArguments(bounds).release()
        raise
    return CallArguments(bounds)
