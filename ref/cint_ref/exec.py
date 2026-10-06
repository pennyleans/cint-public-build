"""Execution of checked programs (SPEC-01 sections 8 to 10; SPEC-04 sections 8 to 12).

`run_program(source, path, entry, fuel, depth)` decodes, parses, checks and
runs one module and returns an `Outcome`: a value, a fault with its canonical
record, a compile error with its diagnostic, or a refusal for a construct or
case cint_ref does not decide.

Evaluation is left to right, each operand once (SPEC-01 8.1). Fuel follows
`fuel-v1` (SPEC-01 10.2): one unit at entry to every user-function call and
at the start of every loop iteration; the entry function, a test block and a
script's implicit entry each charge one unit (interim decision OQ-17). The
call-depth limit counts the entry as depth 1 (SPEC-01 9.4).

Arrays (slice 2 task 2.8; box 09): an array value is an `ArrayVal`, a view of
rank 1 to 4 over a buffer, with its origin, shape and strides in elements
(SPEC-02 V-1). Owned storage (a variable, a field, an element) is a row-major
view of its own buffer. An index, a partial index, a slice and the view
functions `transpose`, `reverse` and `reshape` form views of the same buffer;
a view parameter or view variable binds the caller's view, so an `inout`
write reaches the caller (SPEC-04 LS-119). A call evaluates its arguments
left to right, then runs the entry checks of SPEC-04 LS-123 (shape binding
and agreement, `E_SHAPE` `bind.shape`; then aliasing by the tests of SPEC-02
A-5, `E_ALIAS` `bind.alias`, with the operand forms of `rt/cint_rt.h`), then
the call depth and the fuel charge (LS-141, LS-144). An index out of range
faults `E_BOUNDS` at the `[` (SPEC-01 IM-186); with rank 2 and above the
record carries the dimension first (box 09 ruling R1), and a slice bound
faults with `slice.checked.<E>` (R2). A copy checks its shape, then its
aliasing (`copy.alias`, R8), before any element is written, and charges no
fuel (R7). A reduction charges `ceil(n / 64)` units before it reads an
element (SPEC-01 IM-139; R5, R6).

Kernels (box 09; SPEC-02): a dispatch evaluates its arguments left to right,
runs the entry checks of F-5 in order (two-pass shape binding, K-6; the
`where` constraints; the aliasing tests of A-5 for each written parameter;
the dispatch charge of N fuel units, X-8), then the work-items in ascending
linear order (K-9) over staging buffers (P-4), then the reduction epilogue
in target declaration order (R-7), and publishes its outputs only when
nothing faulted (P-1). A fault inside a dispatch carries the dispatch
address and phase (F-8; CONF-11 format 4): at entry, the dispatch site and
the caller's stack; in a work-item or the epilogue, the dispatch site as the
innermost stack position. A work-item's step ordinal counts the operations
of SPEC-01 IM-113, and its `reduce` statement once after its contribution
(SPEC-02 F-6a).

Errors and cleanup (box 12; SPEC-04 LS-92 to LS-97, LS-129 to LS-133, LS-185 to
LS-189, LS-314 to LS-322): an error value is the checker's `ErrVal`, and a call
of a function whose result is `E!T` returns the success value or a `Failure`,
the error value with the set it is returned through. `try` returns a failure
from the current function, with its tag in the function's set (in a test, in
its own set); `catch` and `_ =` consume it. A statement list that declares a
`defer` or `errdefer` runs its deferred statements in reverse order when it
exits by its end, `break`, `continue`, `return` or `try`, an `errdefer` only
when a failure is returned, and none after a fault (LS-188). An entry that
returns a failure has the outcome `error` (SPEC-09 CONF-11 rule 12).

Arenas and pools (box 12's memory lifetimes group; SPEC-03 M-12 to M-23, with the
readings of ref/OPEN.md REF-OQ-45): the module-level arenas and pools are created
with the context, in module order then declaration order, with identifiers from 1
per kind (M-13). An arena is bump storage of one element type with a generation; a
child is carved from its parent's used extent, and a reset retires every
descendant (M-14, M-20). A pool has a generation per slot and a last-in, first-out
free list (M-15, M-18). A handle is checked where it is dereferenced, and a view
derived from an arena or a pool slot is checked at each element access (M-17,
M-23): `E_STALE_HANDLE` with the handle's five fields, the current generation and
the reason as operands. Local arrays charge the frame arena, in elements, from the
declaration to the exit of its block (SPEC-04 LS-110; BX12-18, BX12-23).
"""
from __future__ import annotations

import os
import sys
import threading
from dataclasses import dataclass, field

from . import arith, fmt, views
from . import parser as P
from . import reduce as red
from .check import (ALLOC_ERROR, ARITH_ERROR, ARITH_VALUES, BIN_OPS, SHIFTS, Z, ArrayType, ErrorSet, ErrorUnion,
                    HandleType, MemType, StructType, check_module, elem_ident, is_aggregate, shape_key)
from .faults import (UNASSIGNED, Address, CompileError, Descriptor, Diagnostic, Fault, FaultRecord,
                     FaultSignal, Position, Refused)
from .lexer import decode_source
from .types import BOOL, Value, int_type

M64 = (1 << 63) - 1
DEFAULT_DEPTH = 256          # SPEC-01 9.4 (Proposed default)
FRAME_ELEMENTS = 2_097_152   # SPEC-04 LS-110: the default frame-arena capacity in elements (BX12-23)
GEN_RETIRED = (1 << 64) - 1  # SPEC-03 M-16: generations never reach 2^64 - 1
# SPEC-03 M-17 reasons, numbered by its precedence, and `container` (ref/OPEN.md REF-OQ-45),
# which is checked after `null`.
REASONS = {"null": 1, "retired": 2, "generation": 3, "extent": 4, "vacant": 5, "container": 6}
FUEL_BLOCK = 64              # SPEC-01 IM-139: a reduction over n elements charges ceil(n / 64)
# The arithmetic operations that count as steps (SPEC-01 IM-113); `& | ^ ~` do not.
COUNTED = frozenset({"add", "sub", "mul", "div", "rem", "shl", "shr"})


@dataclass
class Outcome:
    kind: str                         # value, fault, error, compile-error, refused
    stdout: bytes = b""
    fuel: int = 0
    record: FaultRecord | None = None
    diagnostic: Diagnostic | None = None
    value: Value | None = None        # the entry's return value, if any
    message: str = ""
    globals: dict = field(default_factory=dict)    # the root module's variables, by name
    state: tuple = ()                 # (module, name, Value) per scalar module-level variable, every module
    error: tuple | None = None        # an `error` outcome's (set, value, tag) lines (SPEC-09 CONF-11 rule 12)


class StructVal:
    __slots__ = ("type", "fields")

    def __init__(self, type_, fields):
        self.type = type_
        self.fields = fields

    def copy(self):
        return StructVal(self.type, {k: _copy(v) for k, v in self.fields.items()})


class ArrayVal:
    """A view of rank 1 to 4 over a buffer (SPEC-02 V-1): the element type, the buffer (a
    list of element values), the origin, shape and strides in elements, and whether it
    may be written. Owned storage is a row-major view of its own buffer."""
    __slots__ = ("elem", "buf", "origin", "shape", "strides", "writable", "lease")

    def __init__(self, elem, buf, origin, shape, strides, writable=True, lease=None):
        self.elem = elem
        self.buf = buf
        self.origin = origin
        self.shape = tuple(shape)
        self.strides = tuple(strides)
        self.writable = writable
        self.lease = lease          # (container state, handle) of a view derived from an arena or pool (M-23)

    @classmethod
    def owned(cls, elem, shape, items, writable=True):
        return cls(elem, items, 0, shape, views.row_major(shape), writable)

    def offsets(self):
        """The element offsets in row-major logical order (SPEC-01 IM-74)."""
        return list(views.offsets(self.origin, self.shape, self.strides))

    def values(self):
        return [self.buf[o] for o in self.offsets()]

    def key(self):
        """The view as SPEC-02 A-5 compares it: (buffer, origin, shape, strides)."""
        return (id(self.buf), self.origin, self.shape, self.strides)

    def derive(self, origin, shape, strides):
        """A view of the same buffer with the same permission (and the same lease)."""
        return ArrayVal(self.elem, self.buf, origin, shape, strides, self.writable, self.lease)

    def with_permission(self, writable):
        return ArrayVal(self.elem, self.buf, self.origin, self.shape, self.strides, writable, self.lease)

    def descriptor(self) -> Descriptor:
        """The canonical descriptor of SPEC-02 V-13 in element units (CONF-11 rule 13)."""
        return Descriptor(self.elem, self.origin, self.shape, self.strides, self.writable)

    def copy(self):
        """Owned storage holding a copy of the view's elements, in logical order."""
        return ArrayVal.owned(self.elem, self.shape, [_copy(x) for x in self.values()])


def _copy(v):
    return v.copy() if isinstance(v, (StructVal, ArrayVal)) else v


class HandleVal:
    """A handle (SPEC-03 M-12): slot, length, generation, container and kind (1 arena, 2
    pool); the all-zero handle is the null handle. Immutable."""
    __slots__ = ("slot", "length", "generation", "container", "kind")

    def __init__(self, slot, length, generation, container, kind):
        self.slot = slot
        self.length = length
        self.generation = generation
        self.container = container
        self.kind = kind

    def operands(self):
        return (Value("U64", self.slot), Value("I64", self.length), Value("U64", self.generation),
                Value("U32", self.container), Value("U8", self.kind))

    def is_null(self) -> bool:
        return not (self.slot or self.length or self.generation or self.container or self.kind)


NULL_HANDLE = HandleVal(0, 0, 0, 0, 0)


class ArenaState:
    """An arena (SPEC-03 M-14, M-19, M-20): its identifier, element type, capacity, used
    extent and generation, all in elements; its storage is `buf[base:base + capacity]`,
    shared with its ancestors. A child records its parent; a reset retires its children."""
    __slots__ = ("id", "elem", "capacity", "used", "generation", "buf", "base", "parent", "children", "retired")

    def __init__(self, id_, elem, capacity, buf, base, parent=None):
        self.id = id_
        self.elem = elem
        self.capacity = capacity
        self.used = 0
        self.generation = 0
        self.buf = buf
        self.base = base
        self.parent = parent
        self.children = []
        self.retired = False

    def own(self) -> HandleVal:
        """The arena's own fields, as the handle operands of a fault on the arena itself."""
        return HandleVal(0, self.capacity, self.generation, self.id, 1)


class PoolState:
    """A pool (SPEC-03 M-15, M-18, M-19): a generation, an occupied mark and a retired mark
    per slot, a last-in, first-out free list, and the next fresh slot."""
    __slots__ = ("id", "elem", "capacity", "slots", "gens", "occupied", "retired", "free", "fresh")

    def __init__(self, id_, elem, capacity):
        self.id = id_
        self.elem = elem
        self.capacity = capacity
        self.slots = [None] * capacity
        self.gens = [0] * capacity
        self.occupied = [False] * capacity
        self.retired = [False] * capacity
        self.free = []
        self.fresh = 0


def assign_into(dst, src):
    """Copy `src` into the storage `dst`, keeping the identity of `dst` and of every array
    and struct inside it (views of them stay valid). Shapes are equal (the checker, or the
    caller's run-time check), and the source is read in full before anything is written."""
    if isinstance(dst, ArrayVal):
        new = [_copy(x) for x in src.values()]
        for off, x in zip(dst.offsets(), new):
            if isinstance(x, (StructVal, ArrayVal)):
                assign_into(dst.buf[off], x)
            else:
                dst.buf[off] = x
        return
    new = {k: _copy(v) for k, v in src.fields.items()}
    for k, v in new.items():
        if isinstance(v, (StructVal, ArrayVal)):
            assign_into(dst.fields[k], v)
        else:
            dst.fields[k] = v


def _overlaps(p, q) -> bool:
    """Whether two argument values may share storage at run time: two views of one buffer
    unless the tests of SPEC-02 A-5 find them disjoint (SPEC-04 LS-121; box 09 ruling R8),
    or one inside an element or a field of the other."""
    if p is q:
        return True
    if isinstance(p, ArrayVal) and isinstance(q, ArrayVal) and p.buf is q.buf:
        return views.classify(p.key(), q.key()) != "disjoint"
    return _inside(p, q) or _inside(q, p)


def _inside(outer, inner) -> bool:
    if isinstance(outer, ArrayVal):
        parts = [x for x in outer.values() if isinstance(x, StructVal)]
    elif isinstance(outer, StructVal):
        parts = [x for x in outer.fields.values() if isinstance(x, (StructVal, ArrayVal))]
    else:
        return False
    return any(_overlaps(x, inner) for x in parts)


def zero_value(t, shape=None):
    if t == BOOL:
        return False
    if isinstance(t, HandleType):
        return NULL_HANDLE
    if isinstance(t, StructType):
        return StructVal(t, {k: zero_value(ft) for k, ft in t.fields.items()})
    if isinstance(t, ArrayType):
        shape = tuple(t.extents if shape is None else shape)
        return ArrayVal.owned(t.elem, shape, [zero_value(t.elem) for _ in range(views.count(shape))])
    return 0


def _unravel(k, extents):
    """The indices of linear work-item index `k` in row-major order over `extents` (K-9)."""
    idx = []
    for n in reversed(extents):
        idx.append(k % n)
        k //= n
    return idx[::-1]


class _Break(Exception):
    def __init__(self, label):
        self.label = label


class _Continue(Exception):
    def __init__(self, label):
        self.label = label


class _Return(Exception):
    def __init__(self, value):
        self.value = value


class Failure:
    """An error result (SPEC-04 LS-94): the error value and the set it is returned through,
    which numbers its tag (LS-97)."""
    __slots__ = ("value", "set")

    def __init__(self, value, set_):
        self.value = value
        self.set = set_

    def lines(self):
        """`error.set`, `error.value` and `error.tag` (SPEC-09 CONF-11 rule 12)."""
        s = self.set
        return s.qualname, s.value_text(self.value), "%s %d" % (s.underlying, s.tags[self.value])


NARROW = Failure(ARITH_VALUES["E_NARROW"], ARITH_ERROR)      # the failure of `as?` (SPEC-01 IM-188)


class _Dispatch:
    """The dispatch that is running (SPEC-02 F-8): the kernel's qualified name, the dispatch
    number, the dispatch site, the phase (0 entry, 1 work-item, 2 epilogue), the work-item
    index and its step ordinal (SPEC-01 IM-113), and one accumulator per `reduce` target."""
    __slots__ = ("name", "number", "site", "phase", "wi", "step", "acc")

    def __init__(self, name, number, site):
        self.name, self.number, self.site = name, number, site
        self.phase, self.wi, self.step = 0, None, None
        self.acc = {}


class _Acc:
    """The contributions of one `reduce` statement so far (SPEC-02 R-2): their count and
    the running value: the exact total (`sum`, `sum_wrap`), the left fold (`fold_checked`,
    `sum_sat`), the number of `true` (`count`), or the least or greatest (`min`, `max`)."""
    __slots__ = ("n", "value")

    def __init__(self, value):
        self.n = 0
        self.value = value


_COMPARE = {
    "==": lambda a, b: a == b, "!=": lambda a, b: a != b, "<": lambda a, b: a < b,
    "<=": lambda a, b: a <= b, ">": lambda a, b: a > b, ">=": lambda a, b: a >= b,
}


class Machine:
    def __init__(self, fuel=None, depth=DEFAULT_DEPTH, const_mode=False):
        self.fuel_limit = fuel
        self.fuel_used = 0
        self.depth_limit = depth
        self.depth = 0
        self.stack: list[Position] = []
        self.out = bytearray()
        self.globals: dict = {}
        self.frame: dict = {}
        self.const_mode = const_mode
        self.dispatches = 0                 # dispatch numbers given out so far (SPEC-02 F-1)
        self.k: _Dispatch | None = None     # the dispatch that is running
        self.arenas = 0                     # arena identifiers given out so far (SPEC-03 M-13)
        self.pools = 0                      # pool identifiers given out so far
        self.frame_used = 0                 # elements of the frame arena in use (SPEC-04 LS-110)
        self.frame_capacity = FRAME_ELEMENTS

    # -- faults, fuel, calls -----------------------------------------------------------
    def fault(self, f: Fault, pos: Position, descriptors=()):
        record = FaultRecord.from_fault(f, pos, tuple(self.stack))
        k = self.k
        if k is not None:
            work = k.phase == 1
            record.address = Address(k.name, k.number, k.phase, k.wi if work else None, k.step if work else None)
            record.descriptors = tuple(descriptors)
        raise FaultSignal(record)

    def check(self, r, pos):
        if isinstance(r, Fault):
            self.fault(r, pos)
        return r.value

    def step(self):
        """One counted operation of the running work-item has completed (SPEC-01 IM-113)."""
        k = self.k
        if k is not None and k.phase == 1:
            k.step += 1

    def charge(self, pos: Position, units: int = 1):
        if self.fuel_limit is not None and self.fuel_used + units > self.fuel_limit:
            self.fault(Fault("E_FUEL", "fuel.charge", (), None, Value("I64", self.fuel_limit)), pos)
        self.fuel_used += units

    def charge_elements(self, n: int, pos: Position):
        """The charge of a reduction over `n` elements: ceil(n / 64), none for n = 0 (IM-139)."""
        if n > 0:
            self.charge(pos, -(-n // FUEL_BLOCK))

    def entry_checks(self, f: P.FuncDecl, args, pos: Position) -> dict:
        """The entry checks of a call (SPEC-04 LS-123): shape binding and agreement, then
        aliasing. Returns the size parameters' values."""
        sizes = {}
        for i, (t, a) in enumerate(zip(f.param_types, args)):
            if not isinstance(t, ArrayType):
                continue
            for dim, (d, n) in enumerate(zip(t.dims, a.shape)):
                want = sizes.get(d.size) if d.size is not None else d.extent
                if want is not None and n != want:
                    self.fault(Fault("E_SHAPE", "bind.shape", (Value("I64", i), Value("I64", dim), Value("I64", n)),
                                     None, Value("I64", want)), pos)
                if d.size is not None:
                    sizes.setdefault(d.size, n)
        k = len(args)
        for i in range(k):
            for x in range(i + 1, k):
                if not (is_aggregate(f.param_types[i]) and is_aggregate(f.param_types[x])):
                    continue
                mi, mx = f.params[i].mode, f.params[x].mode
                if mi != "inout" and mx != "inout":
                    continue
                if _overlaps(args[i], args[x]):
                    p, o = (i, x) if mi == "inout" else (x, i)
                    self.fault(Fault("E_ALIAS", "bind.alias", (Value("I64", p), Value("I64", o))), pos)
        return sizes

    def call(self, f: P.FuncDecl, args, pos: Position):
        sizes = self.entry_checks(f, args, pos)
        if self.depth + 1 > self.depth_limit:
            self.fault(Fault("E_DEPTH", "call.enter", (), None, Value("I64", self.depth_limit)), pos)
        self.charge(pos)
        self.stack.append(pos)
        self.depth += 1
        saved = self.frame
        self.frame = dict(sizes)
        for prm, t, a in zip(f.params, f.param_types, args):
            # An `in` struct is a copy; a view, and an `inout` struct, bind the caller's storage.
            if isinstance(t, StructType):
                a = a.copy() if prm.mode == "in" else a
            elif isinstance(t, ArrayType) and prm.mode == "in":
                a = a.with_permission(False)
            self.frame[prm.name] = a
        try:
            self.exec_list(f.body.stmts)
            result = None
        except _Return as r:
            result = r.value
        finally:
            self.frame = saved
            self.depth -= 1
            self.stack.pop()
        return result

    # -- expressions --------------------------------------------------------------------
    def eval(self, e):
        # Inside a dispatch every operation is evaluated, so that each one has its step
        # ordinal (SPEC-01 IM-114); elsewhere a folded constant stands for its expression.
        c = e.const
        if c is not None and self.k is None:
            return c.value
        return _EVAL[type(e)](self, e)

    def _lit(self, e):
        if self.const_mode and e.ty != Z:
            # The range check of a literal happens when the literal is evaluated, in
            # evaluation order, so `const I8 X = 100 + 100 - 150;` meets the overflow at
            # the first `+` before the literal 150 (SPEC-09 SEED-08; ref/OPEN.md).
            t = int_type(e.ty)
            if not t.contains(e.value):
                raise CompileError(Diagnostic("C2003", getattr(e, "lit_start", e.start), "constant %d does not fit %s (%d ..= %d)"
                                              % (e.value, e.ty, t.min, t.max)))
        return e.value

    def _bool(self, e):
        return e.value

    def _typeprop(self, e):
        t = int_type(e.type_name)
        return t.min if e.prop == "min" else t.max

    def _name(self, e):
        sym = e.sym
        if sym.kind == "const":
            return sym.value.value
        if self.const_mode:
            raise AssertionError("non-constant name in constant evaluation")
        if sym.kind in ("modvar", "memory"):
            return self.globals[sym.key]
        # A write to a kernel's `out` or `inout` array goes to its staging buffer (SPEC-02 P-4).
        return self.frame[getattr(e, "wkey", None) or sym.name]

    def _unary(self, e):
        v = self.eval(e.operand)
        if e.op == "!":
            return not v
        t = e.ty
        if e.op == "-":
            r = self.check(arith.op("neg", "checked", t, Value(t, v)), e.pos)
        elif e.op == "-%":
            r = self.check(arith.op("sub", "wrap", t, Value(t, 0), Value(t, v)), e.pos)
        else:
            return self.check(arith.op("not", "checked", t, Value(t, v)), e.pos)
        self.step()
        return r

    def _binary(self, e):
        a = self.eval(e.left)
        b = self.eval(e.right)
        name, form = BIN_OPS[e.op]
        t = e.ty
        rt = e.right.ty if e.op in SHIFTS else t
        r = self.check(arith.op(name, form, t, Value(t, a), Value(rt, b)), e.pos)
        if name in COUNTED:
            self.step()
        return r

    def _compare(self, e):
        a = self.eval(e.left)
        b = self.eval(e.right)
        return _COMPARE[e.op](a, b)

    def _logical(self, e):
        a = self.eval(e.left)
        if e.op == "&&":
            return self.eval(e.right) if a else False
        return True if a else self.eval(e.right)

    def _cond(self, e):
        return self.eval(e.a) if self.eval(e.cond) else self.eval(e.b)

    def _convert(self, e):
        if e.op == "as?":
            return self._convert_result(e)
        form = "checked" if e.op == "as" else "wrap"
        target = e.ty
        if e.src_type == Z:
            v = e.operand.value
            t = int_type(target)
            if form == "wrap":
                r = t.wrap(v)
            elif t.contains(v):
                r = v
            else:
                # A conversion from a literal's value in Z keeps the operation `unassigned` in
                # cint-core-1 (SPEC-01 IM-184; decision 2026-10-03, D-11 on OQ-124).
                self.fault(Fault("E_NARROW", UNASSIGNED, (Value("Z", v),), v,
                                 Value(target, t.max if v > t.max else t.min)), e.pos)
            self.step()
            return r
        v = self.eval(e.operand)
        r = self.check(arith.op("as", form, e.src_type, Value(e.src_type, v), target=target), e.pos)
        self.step()
        return r

    def _convert_result(self, e):
        """`x as? T`: the value, or `ArithError.narrow` where `x as T` faults `E_NARROW`
        (SPEC-01 IM-188; SPEC-04 LS-96)."""
        target = e.target_type
        if e.src_type == Z:
            v = e.operand.value
            r = v if int_type(target).contains(v) else None
        else:
            v = self.eval(e.operand)
            r = arith.op("as", "checked", e.src_type, Value(e.src_type, v), target=target)
            r = None if isinstance(r, Fault) else r.value
        self.step()
        return NARROW if r is None else r

    def _errorlit(self, e):
        return e.errval

    def _try(self, e):
        """`try e` (SPEC-04 LS-129): a failure leaves the current function with its tag in the
        function's set (`target_set`); in a test block, in its own set (LS-315)."""
        v = self.eval(e.operand)
        if isinstance(v, Failure):
            raise _Return(Failure(v.value, e.target_set or v.set))
        return v

    def _catch(self, e):
        """`e catch value` and `e catch (name) { ... }` (SPEC-04 LS-130, LS-131); the block
        leaves by `return`, `break` or `continue` (C4002)."""
        v = self.eval(e.left)
        if not isinstance(v, Failure):
            return v
        if e.block is None:
            return self.eval(e.value)
        self.frame[e.bind] = v.value
        self.exec_list(e.block.stmts)
        raise AssertionError("a catch block completed")

    def _field(self, e):
        errval = getattr(e, "errval", None)
        if errval is not None:                     # `Set.name`, an error value
            return errval
        sym = getattr(e, "sym", None)
        if sym is not None:                        # `alias.NAME` of an imported module
            if sym.kind == "const":
                return sym.value.value
            return self.globals[sym.key]
        return self.eval(e.obj).fields[e.field]

    def subscript(self, e):
        """`obj[items]`: the object, then every index expression and slice bound left to
        right, then the checks dimension by dimension, the first failure reported (SPEC-04
        LS-64, LS-161; box 09 rulings R1 to R3). One counted operation (IM-113). Returns
        (buffer, offset) for an element, else the view."""
        a = self.eval(e.obj)
        vals = []
        if a.lease is not None:
            # The index items are evaluated first; the container is checked before the
            # dimensions (SPEC-03 M-23; ref/OPEN.md REF-OQ-45).
            leased = True
        else:
            leased = False
        for item in e.items:
            if isinstance(item, P.Slice):
                vals.append((None if item.lo is None else self.eval(item.lo),
                             None if item.hi is None else self.eval(item.hi)))
            else:
                vals.append(self.eval(item))
        if leased:
            self.check_lease(a, e.pos)
        rank = len(a.shape)
        origin, shape, strides = a.origin, [], []
        for dim, (item, v) in enumerate(zip(e.items, vals)):
            n, s = a.shape[dim], a.strides[dim]
            lead = (Value("I64", dim),) if rank > 1 else ()
            if isinstance(item, P.Slice):
                valid, first, length, lo, hi = views.slice_dim(n, v[0], v[1], item.inclusive, item.step_value)
                if not valid:
                    self.fault(Fault("E_BOUNDS", "slice.checked." + elem_ident(a.elem),
                                     lead + (Value("I64", lo), Value("I64", hi)), None, Value("I64", n)), e.pos)
                origin += first * s
                shape.append(length)
                strides.append(s * item.step_value)
            else:
                if not 0 <= v < n:
                    self.fault(Fault("E_BOUNDS", "index.checked." + elem_ident(a.elem), lead + (Value("I64", v),),
                                     None, Value("I64", n)), e.pos)
                origin += v * s
        self.step()
        if not e.view:
            return a.buf, origin
        k = len(e.items)
        return a.derive(origin, tuple(shape) + a.shape[k:], tuple(strides) + a.strides[k:])

    def _index(self, e):
        if getattr(e, "deref", None) is not None:
            cell, key = self.deref(e)
            return cell if e.deref == "Arena" else cell[key]
        r = self.subscript(e)
        if e.view:
            return r
        buf, off = r
        return buf[off]

    def _strlit(self, e):
        data = e.parts[0]
        return ArrayVal.owned("U8", (len(data),), list(data), False)

    def _arraylit(self, e):
        items = []

        def visit(lit):
            for x in lit.items:
                if isinstance(x, P.ArrayLit):
                    visit(x)
                else:
                    items.append(_copy(self.eval(x)))
        visit(e)
        return ArrayVal.owned(e.elem, e.extents, items)

    def _call(self, e):
        kind = e.kind
        if kind == "len":
            return self.eval(e.arg).shape[0]
        if kind == "builtin":
            t = e.operand_type
            vals = [Value(t, self.eval(v)) for v in e.values]
            if e.count is not None:
                vals.append(Value(e.count.ty, self.eval(e.count)))
            try:
                r = arith.op(e.callee, "checked", t, *vals, mode=e.mode, target=e.target)
            except arith.OpenCase as x:
                raise Refused(str(x), e.pos)
            r = self.check(r, e.pos)
            self.step()
            return r
        if kind == "result":
            return self.result_builtin(e)
        if self.const_mode:
            raise AssertionError("call in constant evaluation")
        if kind == "member":
            return self.member(e)
        if kind == "reduce":
            return self.reduction(e)
        if kind == "shape":
            a = self.eval(e.values[0])
            if e.callee == "size":
                return views.count(a.shape)
            return a.shape[self.eval(e.values[1])]
        if kind == "view":
            return self.view_function(e)
        if kind in ("copy", "fill"):
            return self.copy_fill(e)
        if kind == "kernel":
            return self.dispatch(e)
        values = {}
        for idx, a in e.order:
            values[idx] = self.eval(a)
        args = [values[i] for i in range(len(values))]
        if kind == "struct":
            names = list(e.struct.fields)
            for i, t in enumerate(e.struct.fields.values()):
                if isinstance(t, ArrayType):
                    self.copy_shape(t.extents, args[i], e.pos)
            return StructVal(e.struct, {n: _copy(args[i]) for i, n in enumerate(names)})
        return self.call(e.func, args, e.pos)

    def result_builtin(self, e):
        """`add_result(a, b)` and the other result-returning forms (SPEC-01 IM-30): the value of
        the checked form, or the `ArithError` value that mirrors its fault code (IM-188)."""
        t = e.operand_type
        a = self.eval(e.values[0])
        b = self.eval(e.values[1])
        bt = e.values[1].ty if e.op == "shl" else t
        r = arith.op(e.op, "checked", t, Value(t, a), Value(bt, b))
        self.step()
        if isinstance(r, Fault):
            return Failure(ARITH_VALUES[r.code], ARITH_ERROR)
        return r.value

    # -- arenas and pools (SPEC-03 3.2; ref/OPEN.md REF-OQ-45) ---------------------------
    def stale(self, operation, h: HandleVal, current, reason, pos):
        """`E_STALE_HANDLE` (SPEC-03 M-17): the handle's five fields, the current generation
        and the reason as operands, no `exact` and no `limit` (ref/OPEN.md REF-OQ-45)."""
        self.fault(Fault("E_STALE_HANDLE", operation, h.operands() + (Value("U64", current),
                                                                        Value("U8", REASONS[reason]))), pos)

    def arena_reason(self, a: ArenaState, h: HandleVal):
        """The first reason of SPEC-03 M-17 that applies to `h` in arena `a`, or None."""
        if h.is_null():
            return "null"
        if h.kind != 1 or h.container != a.id:
            return "container"
        x = a
        while x is not None:
            if x.retired:
                return "retired"
            x = x.parent
        if h.generation != a.generation:
            return "generation"
        if h.length < 0 or h.slot + h.length > a.used:
            return "extent"
        return None

    def pool_reason(self, p: PoolState, h: HandleVal):
        if h.is_null():
            return "null"
        if h.kind != 2 or h.container != p.id:
            return "container"
        inside = h.slot < p.capacity
        if inside and p.retired[h.slot]:
            return "retired"
        if inside and h.generation != p.gens[h.slot]:
            return "generation"
        if not inside:
            return "extent"
        if not p.occupied[h.slot]:
            return "vacant"
        return None

    def pool_current(self, p: PoolState, h: HandleVal) -> int:
        return p.gens[h.slot] if h.slot < p.capacity else 0

    def deref(self, e):
        """`p[h]` or `a[h]` (SPEC-03 M-14a, M-15, M-17): the container, then the handle, then
        the check at the `[`. A pool gives (slots, slot); an arena the rank-1 view of the
        allocation, which records its container and handle (M-23)."""
        c = self.eval(e.obj)
        h = self.eval(e.index)
        if isinstance(c, PoolState):
            reason = self.pool_reason(c, h)
            if reason is not None:
                self.stale("pool.deref", h, self.pool_current(c, h), reason, e.pos)
            return c.slots, h.slot
        reason = self.arena_reason(c, h)
        if reason is not None:
            self.stale("handle.check", h, c.generation, reason, e.pos)
        return ArrayVal(c.elem, c.buf, c.base + h.slot, (h.length,), (1,), e.ty.writable, (c, h)), None

    def check_lease(self, v: ArrayVal, pos):
        """An element access through a view derived from an arena allocation: the arena and
        its ancestors are live and its generation is unchanged (SPEC-03 M-23)."""
        c, h = v.lease
        reason = self.arena_reason(c, h)
        if reason is not None:
            self.stale("handle.check", h, c.generation, reason, pos)

    def member(self, e):
        """The members of an arena and of a pool (SPEC-03 M-14 to M-20; SPEC-04 LS-145): the
        receiver, then the argument; faults are at the member name (ref/OPEN.md REF-OQ-45)."""
        c = self.eval(e.recv)
        name = e.callee
        arg = self.eval(e.values[0]) if e.values else None
        if isinstance(c, ArenaState):
            if c.retired or any(x.retired for x in self.ancestors(c)):
                self.stale("arena.reset" if name == "reset" else "arena.alloc", c.own(), c.generation, "retired",
                           e.pos)
            if name == "reset":
                return self.reset(c, e.pos)
            n = arg
            if n < 0 or c.used + n > c.capacity:
                if name == "alloc_result" and n >= 0:
                    return Failure(ALLOC_ERROR.values[0], ALLOC_ERROR)
                self.fault(Fault("E_BOUNDS", "arena.alloc", (Value("I64", n), Value("I64", c.used),
                                                            Value("I64", c.capacity))), e.pos)
            slot = c.used
            c.used += n
            if name == "child":
                if self.arenas == (1 << 32) - 1:
                    raise Refused("a child arena past 2^32 - 1 arena identifiers: no clause gives the fault "
                                  "(ref/OPEN.md REF-OQ-45)", e.pos)
                self.arenas += 1
                child = ArenaState(self.arenas, c.elem, n, c.buf, c.base + slot, c)
                c.children.append(child)
                return child
            for k in range(c.base + slot, c.base + slot + n):
                c.buf[k] = zero_value(c.elem)
            return HandleVal(slot, n, c.generation, c.id, 1)
        p = c
        if name == "remove":
            h = arg
            reason = self.pool_reason(p, h)
            if reason is not None:
                self.stale("pool.deref", h, self.pool_current(p, h), reason, e.pos)
            p.occupied[h.slot] = False
            p.slots[h.slot] = None
            if p.gens[h.slot] + 1 == GEN_RETIRED:
                p.retired[h.slot] = True       # never reused (SPEC-03 M-16)
            else:
                p.gens[h.slot] += 1
                p.free.append(h.slot)
            return None
        if p.free:
            slot = p.free.pop()                # last in, first out (SPEC-03 M-18)
        elif p.fresh < p.capacity:
            slot = p.fresh
            p.fresh += 1
        else:
            if name == "insert_result":
                return Failure(ALLOC_ERROR.values[0], ALLOC_ERROR)
            occupied = sum(p.occupied)
            retired = sum(p.retired)
            self.fault(Fault("E_BOUNDS", "pool.insert", (Value("I64", occupied), Value("I64", retired),
                                                        Value("I64", p.capacity))), e.pos)
        p.slots[slot] = _copy(arg)
        p.occupied[slot] = True
        return HandleVal(slot, 1, p.gens[slot], p.id, 2)

    @staticmethod
    def ancestors(a: ArenaState):
        x = a.parent
        while x is not None:
            yield x
            x = x.parent

    def reset(self, a: ArenaState, pos):
        """`a.reset()` (SPEC-03 M-14, M-16, M-20): the generation advances, the used extent is
        0, and every descendant is retired."""
        if a.generation + 1 == GEN_RETIRED:
            raise Refused("an arena reset at generation 2^64 - 2: SPEC-03 M-16 gives E_OVERFLOW without the "
                          "operands of its record (ref/OPEN.md REF-OQ-45)", pos)
        a.generation += 1
        a.used = 0
        stack = list(a.children)
        while stack:
            x = stack.pop()
            x.retired = True
            stack.extend(x.children)
        a.children = []
        return None

    def create_memories(self, memories):
        """The module-level arenas and pools, created with the context in module order and
        declaration order, identifiers from 1 per kind (SPEC-03 M-13, M-20; BX12-05, BX12-29)."""
        for key, t, n in memories:
            if t.kind == "Arena":
                self.arenas += 1
                self.globals[key] = ArenaState(self.arenas, t.elem, n, [zero_value(t.elem) for _ in range(n)], 0)
            else:
                self.pools += 1
                self.globals[key] = PoolState(self.pools, t.elem, n)

    # -- arrays: views, copies, reductions ----------------------------------------------
    def view_function(self, e):
        """`transpose(a)`, `reverse(a, d)` and `reshape(a, e1, ...)` (SPEC-02 V-10, V-11; SPEC-01
        IM-76; box 09 ruling R4): views of the same buffer."""
        a = self.eval(e.values[0])
        if e.callee == "transpose":
            v = a.derive(a.origin, a.shape[::-1], a.strides[::-1])
        elif e.callee == "reverse":
            d = self.eval(e.values[1])
            n = a.shape[d]
            strides = list(a.strides)
            strides[d] = -strides[d]
            v = a.derive(a.origin + (a.strides[d] * (n - 1) if n > 0 else 0), a.shape, strides)
        else:
            extents = tuple(self.eval(x) for x in e.values[1:])
            if any(n < 0 for n in extents):
                raise Refused("`reshape` to a negative extent: no clause gives the record", e.pos)
            if views.count(extents) != views.count(a.shape):
                raise Refused("`reshape` to another element count: no clause gives the record", e.pos)
            if views.count(extents) > 0 and a.strides != views.row_major(a.shape):
                raise Refused("a `reshape` that needs a copy faults E_UNSUPPORTED (SPEC-02 V-11), and no clause gives "
                              "the record", e.pos)
            v = a.derive(a.origin, extents, views.row_major(extents))
        self.step()
        return v

    def copy_shape(self, shape, source, pos):
        """The extents of a copy agree, dimension by dimension (SPEC-01 IM-187): else
        `copy.shape` with the lowest differing dimension and the source extent."""
        for dim, (want, n) in enumerate(zip(shape, source.shape)):
            if n != want:
                self.fault(Fault("E_SHAPE", "copy.shape", (Value("I64", dim), Value("I64", n)), None,
                                 Value("I64", want)), pos)

    def copy_into(self, dst, src, pos):
        """A copy into the view `dst` (SPEC-04 LS-70; SPEC-01 IM-187; box 09 ruling R8): the
        shape check, then the aliasing tests of SPEC-02 A-5 (the same view is no copy;
        storage that is not disjoint faults `copy.alias`), then the elements. No fuel (R7)."""
        for v in (dst, src):
            if v.lease is not None:
                self.check_lease(v, pos)
        self.copy_shape(dst.shape, src, pos)
        if dst.buf is src.buf:
            c = views.classify(dst.key(), src.key())
            if c == "identical":
                return
            if c != "disjoint":
                self.fault(Fault("E_ALIAS", "copy.alias", ()), pos)
        assign_into(dst, src)

    def copy_fill(self, e):
        dst = self.eval(e.values[0])
        src = self.eval(e.values[1])
        if e.kind == "copy":
            self.copy_into(dst, src, e.pos)
            return None
        if dst.lease is not None:
            self.check_lease(dst, e.pos)
        for off in dst.offsets():
            if isinstance(src, StructVal):
                assign_into(dst.buf[off], src)
            else:
                dst.buf[off] = src
        return None

    def reduction(self, e):
        """A reduction over an array or view (SPEC-01 IM-77 to IM-87; SPEC-04 LS-145; box 09
        rulings R5, R6): its arguments left to right, then (for `dot`) the shape check, then
        the fuel charge, then the elements in logical order. Faults are at the callee."""
        vals = [self.eval(x) for x in e.values]
        for v in vals:
            if isinstance(v, ArrayVal) and v.lease is not None:
                self.check_lease(v, e.pos)
        if e.callee == "dot":
            a, b = vals
            operation = "dot.checked.%s.%s" % (e.elem.lower(), e.target.lower())
            n, nb = a.shape[0], b.shape[0]
            if n != nb:
                self.fault(Fault("E_SHAPE", operation, (Value("I64", 0), Value("I64", nb)), None, Value("I64", n)),
                           e.pos)
            self.charge_elements(n, e.pos)
            total = sum(x * y for x, y in zip(a.values(), b.values()))
            t = int_type(e.target)
            if not t.contains(total):
                self.fault(Fault("E_OVERFLOW", operation, (Value("I64", n),), total,
                                 Value(e.target, t.max if total > t.max else t.min)), e.pos)
            return total
        xs = vals[-1]
        init = vals[0] if e.init is not None else None
        self.charge_elements(views.count(xs.shape), e.pos)
        items = xs.values()
        if e.callee == "count":
            return sum(1 for x in items if x)
        form = {"fold_checked": e.op, "sum_wrap": "wrap", "sum_sat": "sat"}.get(e.callee, "checked")
        r = red.op(e.rname, form, e.elem, items, init=init, target=e.target if e.callee == "sum" else None)
        return self.check(r, e.pos)

    # -- kernels (SPEC-02) ----------------------------------------------------------------
    def shape_expr(self, key, sizes, pos):
        """A shape expression of SPEC-02 K-1 under the shape symbols' values, in checked
        `I64` arithmetic; an overflow faults at the dispatch site, at entry."""
        if key[0] == "n":
            return sizes[key[1]]
        if key[0] == "l":
            return key[1]
        _, op, name, c = key
        return self.check(arith.op("add" if op == "+" else "sub", "checked", "I64", Value("I64", sizes[name]),
                                   Value("I64", c)), pos)

    def dispatch(self, e):
        k = e.kernel
        site = e.pos
        # The arguments, left to right: views for arrays, values for scalar `in`
        # parameters, and the places that a scalar `out` publishes to (SPEC-02 K-4, P-1).
        args = []
        for p, t, a in zip(k.params, k.param_types, e.values):
            if isinstance(t, ArrayType) or p.mode == "in":
                args.append(self.eval(a))
            else:
                args.append(self.place(a))
        d = _Dispatch(k.qualname, self.dispatches, site)
        self.dispatches += 1
        saved_frame, saved_k = self.frame, self.k
        self.k = d
        try:
            return self._dispatch(k, d, args)
        finally:
            self.frame, self.k = saved_frame, saved_k

    def _dispatch(self, k, d, args):
        site = d.site
        arrays = [(i, t, v) for i, (t, v) in enumerate(zip(k.param_types, args)) if isinstance(t, ArrayType)]
        # F-5 check 5: the two-pass shape binding of K-6, then the `where` constraints.
        sizes = {}
        for _, t, v in arrays:
            for dm, n in zip(t.dims, v.shape):
                if dm.size is not None:
                    sizes.setdefault(dm.size, n)
        for i, t, v in arrays:
            for dim, (dm, n) in enumerate(zip(t.dims, v.shape)):
                want = dm.extent if dm.extent is not None else self.shape_expr(shape_key(dm.expr) if dm.size is None
                                                                               else ("n", dm.size), sizes, site)
                if want != n:
                    self.fault(Fault("E_SHAPE", "bind.shape", (Value("I64", i), Value("I64", dim), Value("I64", n)),
                                     None, Value("I64", want)), site)
        # A false constraint faults `bind.where` with its index in written order and the
        # values of its two sides (SPEC-02 K-1, F-5 check 5; decision 2026-10-05 on OQ-203).
        for j, c in enumerate(k.where):
            a = self.shape_expr(shape_key(c.left), sizes, site)
            b = self.shape_expr(shape_key(c.right), sizes, site)
            if not _COMPARE[c.op](a, b):
                self.fault(Fault("E_SHAPE", "bind.where", (Value("I64", j), Value("I64", a), Value("I64", b))), site)
        extents = [self.shape_expr(shape_key(x), sizes, site) for _, _, x in k.over]
        # F-5 checks 6 and 7: each written view is injective (A-7), and disjoint from every
        # other array argument by the tests of A-5 (A-6: uncertain is E_ALIAS).
        for i, t, v in arrays:
            if k.params[i].mode != "in" and not views.injective(v.shape, v.strides):
                raise Refused("an `out` or `inout` view that is not injective (SPEC-02 A-7): no clause gives the record",
                              site)
        for i, t, v in arrays:
            if k.params[i].mode == "in":
                continue
            for x, u, w in arrays:
                if x != i and _overlaps(v, w):
                    self.fault(Fault("E_ALIAS", "bind.alias", (Value("I64", i), Value("I64", x))), site,
                               (v.descriptor(), w.descriptor()))
        # F-5 check 9: the dispatch charge of N units (X-8; box 09 ruling R7).
        total = views.count(extents)
        self.charge(site, total)
        # The work-items, over staging buffers (P-4): an `out` starts at zero, an `inout`
        # at its values before the dispatch, which its reads see throughout.
        base = dict(sizes)
        staged = {}
        for (p, t), v in zip(zip(k.params, k.param_types), args):
            if isinstance(t, ArrayType):
                base[p.name] = v
                if p.mode != "in":
                    items = list(v.values()) if p.mode == "inout" else [0] * views.count(v.shape)
                    staged[p.name] = base["\0w" + p.name] = ArrayVal.owned(t.elem, v.shape, items)
            elif p.mode == "in":
                base[p.name] = v
        self.frame = base
        for s in k.reductions:
            d.acc[s.target] = _Acc(self.eval(s.init) if s.op == "fold_checked" else
                                   None if s.op in ("min", "max") else 0)
        self.stack.append(site)
        d.phase = 1
        for wi in range(total):
            d.wi, d.step = wi, 0
            self.frame = dict(base)
            for (name, _, _), x in zip(k.over, _unravel(wi, extents)):
                self.frame[name] = x
            self.exec_list(k.body.stmts)
        # The epilogue: each reduction's final check, in target declaration order (R-7).
        d.phase = 2
        results = {}
        for s in k.reductions:
            acc = d.acc[s.target]
            t = int_type(s.rtype)
            if s.op == "sum":
                if not t.contains(acc.value):
                    self.fault(Fault("E_OVERFLOW", arith.op_id("sum", "checked", [s.elem, s.rtype]),
                                     (Value("I64", acc.n),), acc.value,
                                     Value(s.rtype, t.max if acc.value > t.max else t.min)), s.call.pos)
            elif s.op == "sum_wrap":
                acc.value = t.wrap(acc.value)
            elif s.op in ("min", "max") and acc.n == 0:
                self.fault(Fault("E_SHAPE", arith.op_id(s.rname, "checked", [s.rtype]), (Value("I64", 0),)),
                           s.call.pos)
            results[s.target] = acc.value
        self.stack.pop()
        # Publication, in parameter order (P-1).
        for (p, t), a in zip(zip(k.params, k.param_types), args):
            if p.mode == "in":
                continue
            if isinstance(t, ArrayType):
                assign_into(a, staged[p.name])
            else:
                cell, key = a
                cell[key] = results[p.name]
        return None

    def _reduce(self, s):
        """`reduce target = op(e);` (SPEC-02 R-1, R-2, R-6): one contribution, in ascending
        work-item order; `fold_checked` folds at once and faults at this statement's step."""
        x = self.eval(s.value)
        acc = self.k.acc[s.target]
        op = s.op
        if op == "fold_checked":
            t = int_type(s.rtype)
            v = acc.value + x
            if not t.contains(v):
                self.fault(Fault("E_OVERFLOW", arith.op_id("fold_checked", s.fold_op, [s.rtype]),
                                 (Value("I64", acc.n), Value(s.rtype, acc.value), Value(s.rtype, x)), v,
                                 Value(s.rtype, t.max if v > t.max else t.min)), s.call.pos)
            acc.value = v
        elif op in ("sum", "sum_wrap"):
            acc.value += x
        elif op == "sum_sat":
            acc.value = int_type(s.rtype).sat(acc.value + x)
        elif op == "count":
            acc.value += 1 if x else 0
        elif acc.value is None:
            acc.value = x
        else:
            acc.value = min(acc.value, x) if op == "min" else max(acc.value, x)
        acc.n += 1
        self.step()

    # -- statements -------------------------------------------------------------------
    def exec_list(self, stmts):
        # The local arrays a statement list declares are released from the frame arena
        # when it exits, however it exits (SPEC-04 LS-110; ref/OPEN.md REF-OQ-45).
        mark = self.frame_used
        try:
            for i, s in enumerate(stmts):
                if type(s) is _DEFER:
                    return self._exec_deferring(stmts, i)
                _EXEC[type(s)](self, s)
        finally:
            self.frame_used = mark

    def _exec_deferring(self, stmts, i):
        """Run `stmts` from `i`, a `defer` or `errdefer` (SPEC-04 LS-185 to LS-189, LS-316 to
        LS-321). The deferred statements run in reverse order of declaration when the list
        exits by its end, `break`, `continue`, `return` or `try` (after a returned value is
        evaluated, LS-316); an `errdefer` only when the exit returns a failure (LS-189;
        ref/OPEN.md REF-OQ-43). A fault or a refusal runs none (LS-188)."""
        pending = []
        try:
            for k in range(i, len(stmts)):
                s = stmts[k]
                if type(s) is _DEFER:
                    pending.append(s)
                else:
                    _EXEC[type(s)](self, s)
        except _Return as r:
            if pending and isinstance(r.value, StructVal):
                # The returned value is taken before the deferred statements run, which
                # cannot change it (LS-316), so a struct is copied out of its variable.
                r.value = r.value.copy()
            self.run_deferred(pending, isinstance(r.value, Failure))
            raise
        except (_Break, _Continue):
            self.run_deferred(pending, False)
            raise
        self.run_deferred(pending, False)

    def run_deferred(self, pending, failed):
        """Run the deferred statements, last declared first; they charge no fuel of their own
        (LS-317), and a fault in one is at its own position with this function's stack."""
        for d in reversed(pending):
            if d.kind == "errdefer" and not failed:
                continue
            body = d.body
            if type(body) is P.Block:
                self.exec_list(body.stmts)
            else:
                _EXEC[type(body)](self, body)

    def _block(self, s):
        self.exec_list(s.stmts)

    def decl_shape(self, s, t):
        """The extents of a declared array, evaluated left to right; a negative one faults
        `E_SHAPE` `decl.shape` at the declared name (SPEC-04 LS-62; box 09 ruling R10)."""
        shape = [d.extent if d.extent is not None else self.eval(d.expr) for d in t.dims]
        for dim, n in enumerate(shape):
            if n < 0:
                self.fault(Fault("E_SHAPE", "decl.shape", (Value("I64", dim), Value("I64", n)), None,
                                 Value("I64", 0)), s.name_pos)
        return tuple(shape)

    def _vardecl(self, s):
        if s.is_const:
            return
        if s.mode is not None:
            # A view variable binds the storage of its initializer (SPEC-04 LS-69, LS-106).
            v = self.eval(s.init)
            self.frame[s.name] = v.with_permission(s.mode == "inout")
            return
        t = s.ty
        if isinstance(t, ArrayType):
            shape = self.decl_shape(s, t)
            if self.k is None:
                self.frame_charge(views.count(shape), s.name_pos)
            if s.init is not None:
                value = self.eval(s.init)
                if value.lease is not None:
                    self.check_lease(value, s.init_pos)
                self.copy_shape(shape, value, s.init_pos)
                self.frame[s.name] = value.copy()
            else:
                self.frame[s.name] = zero_value(t, shape)
            return
        self.frame[s.name] = _copy(self.eval(s.init)) if s.init is not None else zero_value(t)

    def frame_charge(self, n, pos):
        """A local array of `n` elements takes `n` elements of the frame arena, whatever its
        element type; past the capacity it faults `E_BOUNDS` `arena.alloc` at the declared
        name, after `decl.shape` and before the initializer (SPEC-04 LS-110; BX12-18,
        BX12-23; ref/OPEN.md REF-OQ-45)."""
        if self.frame_used + n > self.frame_capacity:
            self.fault(Fault("E_BOUNDS", "arena.alloc", (Value("I64", n), Value("I64", self.frame_used),
                                                        Value("I64", self.frame_capacity))), pos)
        self.frame_used += n

    def place(self, target):
        """The storage cell of a place, as (container, key); its indices are evaluated and
        checked first (SPEC-04 LS-139)."""
        if isinstance(target, P.Index) and getattr(target, "deref", None) == "Pool":
            return self.deref(target)
        if isinstance(target, P.Name):
            if target.sym.kind == "modvar":
                return self.globals, target.sym.key
            return self.frame, getattr(target, "wkey", None) or target.name
        if isinstance(target, P.Field):
            sym = getattr(target, "sym", None)
            if sym is not None:
                return self.globals, sym.key
            return self.eval(target.obj).fields, target.field
        return self.subscript(target)

    def _assign(self, s):
        if s.op == "=" and isinstance(s.ty, ArrayType):
            dst = self.eval(s.target)          # the place first, its indices checked (LS-139)
            self.copy_into(dst, self.eval(s.value), s.pos)
            return
        cell, key = self.place(s.target)
        if s.op == "=":
            v = self.eval(s.value)
            if isinstance(v, StructVal):
                assign_into(cell[key], v)
            else:
                cell[key] = v
            return
        cur = cell[key]
        v = self.eval(s.value)
        name, form = BIN_OPS[s.binop]
        t = s.ty
        rt = s.value.ty if s.binop in SHIFTS else t
        cell[key] = self.check(arith.op(name, form, t, Value(t, cur), Value(rt, v)), s.pos)
        if name in COUNTED:
            self.step()

    def _incdec(self, s):
        cell, key = self.place(s.target)
        name = "add" if s.op == "++" else "sub"
        cell[key] = self.check(arith.op(name, "checked", s.ty, Value(s.ty, cell[key]), Value(s.ty, 1)), s.pos)
        self.step()

    def _exprstmt(self, s):
        self.eval(s.expr)

    def _discard(self, s):
        self.eval(s.expr)

    def render_one(self, v, t, p, used) -> str:
        try:
            return fmt.render(v, t, p.conv, p.lanes, p.parsed, used)
        except fmt.FormatFault:
            # SPEC-04 9.3 rule 7; operation `format.char.<T>` (SPEC-01 IM-134, OQ-124).
            self.fault(Fault("E_NARROW", "format.char." + t.lower(), (Value(t, v),), v, None), p.expr.pos)

    def render_parts(self, parts) -> bytes:
        chunks = []
        size = 0              # the statement's bytes so far (fmt.staged)
        for p in parts:
            if isinstance(p, bytes):
                chunks.append(p)
                size = fmt.staged(size + len(p))
                continue
            v = self.eval(p.expr)
            used = size if p.name is None else size + len(p.name.encode("utf-8")) + 1
            if isinstance(v, ArrayVal):
                # SPEC-04 LS-214: `[a, b, c]`, each element with the hole's spec applied.
                items, used = [], used + 2
                for x in v.values():
                    used += 2 if items else 0
                    items.append(self.render_one(x, v.elem, p, used))
                    used += len(items[-1].encode("utf-8"))
                text = "[" + ", ".join(items) + "]"
            else:
                text = self.render_one(v, p.ty, p, used)
            if p.name is not None:
                text = p.name + "=" + text
            chunks.append(text.encode("utf-8"))
            size = fmt.staged(size + len(chunks[-1]))
        return b"".join(chunks)

    def _print(self, s):
        self.out += self.render_parts(s.parts)      # nothing is written if a hole faults

    def _if(self, s):
        if self.eval(s.cond):
            self.exec_list(s.then.stmts)
        elif s.else_ is not None:
            _EXEC[type(s.else_)](self, s.else_)

    def _run_body(self, s, body) -> bool:
        """Run a loop body; returns False when the loop must stop (a matching break)."""
        try:
            self.exec_list(body.stmts)
        except _Break as b:
            if b.label is None or b.label == s.label:
                return False
            raise
        except _Continue as c:
            if c.label is not None and c.label != s.label:
                raise
        return True

    def _while(self, s):
        while self.eval(s.cond):
            self.charge(s.pos)
            if not self._run_body(s, s.body):
                break

    def _forc(self, s):
        _EXEC[type(s.init)](self, s.init)
        while self.eval(s.cond):
            self.charge(s.pos)
            if not self._run_body(s, s.body):
                break
            _EXEC[type(s.update)](self, s.update)

    def _forrange(self, s):
        lo = self.eval(s.lo)
        hi = self.eval(s.hi)
        step = s.step_value
        i = lo
        while (i < hi or (s.inclusive and i == hi)) if step > 0 else (i > hi or (s.inclusive and i == hi)):
            self.charge(s.pos)
            self.frame[s.var] = i
            if not self._run_body(s, s.body):
                break
            i += step          # Python integers: no value past the last element is stored

    def _switch(self, s):
        v = self.eval(s.scrutinee)
        es = s.error_set
        key = es.tags[v] if es is not None else int(v)
        clauses = s.cases + ([s.default] if s.default else [])
        start = None
        for k, c in enumerate(s.cases):
            if any(a <= key <= b for a, b in c.ranges):
                start = k
                break
        if start is None:
            if s.default is None:
                raise AssertionError("exhaustive switch matched nothing")
            start = len(s.cases)
        try:
            k = start
            while True:
                body = clauses[k].body
                self.exec_list(body)
                if body and isinstance(body[-1], P.Fallthrough):
                    k += 1
                    continue
                break
        except _Break as b:
            if b.label is not None:
                raise

    def _break(self, s):
        raise _Break(s.label)

    def _continue(self, s):
        raise _Continue(s.label)

    def _return(self, s):
        if s.value is None:
            raise _Return(None)
        v = self.eval(s.value)
        raise _Return(Failure(v, s.set) if getattr(s, "is_error", False) else v)

    def _nothing(self, s):
        pass

    def _assert(self, s):
        e = s.cond
        if isinstance(e, P.Compare):
            a = self.eval(e.left)
            b = self.eval(e.right)
            if not _COMPARE[e.op](a, b):
                t = e.operand_type
                if isinstance(t, ErrorSet):
                    raise Refused("a failed assert that compares error values: the `fault.operand` form of "
                                  "SPEC-09 CONF-11 has no error value (ref/OPEN.md REF-OQ-44)", e.start)
                self.fault(Fault("E_ASSERT", "assert.checked.bool", (Value(t, a), Value(t, b))), e.start)
            return
        if not self.eval(e):
            self.fault(Fault("E_ASSERT", "assert.checked.bool", ()), e.start)


_EVAL = {
    P.Lit: Machine._lit, P.BoolLit: Machine._bool, P.TypeProp: Machine._typeprop, P.Name: Machine._name,
    P.Unary: Machine._unary, P.Binary: Machine._binary, P.Compare: Machine._compare,
    P.Logical: Machine._logical, P.Cond: Machine._cond, P.Convert: Machine._convert,
    P.Field: Machine._field, P.Call: Machine._call, P.Index: Machine._index, P.StrLit: Machine._strlit,
    P.ArrayLit: Machine._arraylit, P.ErrorLit: Machine._errorlit, P.Try: Machine._try, P.Catch: Machine._catch,
}
_EXEC = {
    P.Block: Machine._block, P.VarDecl: Machine._vardecl, P.Assign: Machine._assign,
    P.IncDec: Machine._incdec, P.ExprStmt: Machine._exprstmt, P.Discard: Machine._discard,
    P.Print: Machine._print, P.If: Machine._if, P.While: Machine._while, P.ForC: Machine._forc,
    P.ForRange: Machine._forrange, P.Switch: Machine._switch, P.Break: Machine._break,
    P.Continue: Machine._continue, P.Return: Machine._return, P.Fallthrough: Machine._nothing,
    P.Assert: Machine._assert, P.StaticAssert: Machine._nothing, P.Reduce: Machine._reduce,
}


_DEFER = P.Defer


def test_verdict(test: P.TestDecl, outcome: Outcome) -> bool:
    """Whether a test passes, from the outcome of its body (SPEC-04 LS-235, LS-236): without
    `expect_fault` it passes when its body completes; with `expect_fault E_NAME` only when the
    body faults with code `E_NAME`, and with `at N` (LS-240, Proposed) only when the canonical
    fault position is also on line N."""
    if test.expect_fault is None:
        return outcome.kind == "value"
    if outcome.kind != "fault" or outcome.record.code != test.expect_fault:
        return False
    return test.expect_line is None or outcome.record.position.line == test.expect_line


def const_eval(e) -> Value:
    """Evaluate a constant expression with the run-time rules (SPEC-01 3.2, 8.4)."""
    m = Machine(const_mode=True)
    return Value(e.ty, m.eval(e))


# ---------------------------------------------------------------------------
# Entries
# ---------------------------------------------------------------------------

def _select_entry(prog, entry, test):
    if entry is not None:
        f = prog.functions.get(entry)
        if f is None:
            raise Refused("no function named `%s`" % entry)
        return ("function", f)
    if test is not None:
        for t in prog.tests:
            if t.name == test:
                return ("test", t)
        raise Refused("no test named %r" % test)
    if prog.is_script:
        return ("script", None)
    if "main" in prog.functions:
        return ("function", prog.functions["main"])
    if len(prog.tests) == 1:
        return ("test", prog.tests[0])
    raise Refused("no entry: name a function with --entry or a test with --test")


def _check_args(f, args):
    if len(args) != len(f.params):
        raise Refused("`%s` takes %d arguments, %d given" % (f.name, len(f.params), len(args)))
    out = []
    for p, t, a in zip(f.params, f.param_types, args):
        if not isinstance(a, Value) or a.type != (t.name if isinstance(t, (StructType, ArrayType)) else t):
            raise Refused("argument `%s` must be a %s value" % (p.name, t))
        if is_aggregate(t):
            raise Refused("struct and array arguments from the host are not supported by cint_ref")
        if t == BOOL:
            if not isinstance(a.value, bool):
                raise Refused("argument `%s` must be true or false" % p.name)
        elif not int_type(t).contains(a.value):
            raise Refused("argument `%s` is outside %s" % (p.name, t))
        out.append(a.value)
    return out


def _state(m, prog):
    """The root module's variables by name, and the module state after the entry for
    `state.global` lines (SPEC-09 CONF-11 rule 10): each module-level variable of an
    integer type or `Bool`, of every module in module order (the root first, then each module
    at its first import, breadth first; CONF-11 rule 10), as (module, name, Value);
    a struct or array has no line until OQ-156. The module name is the dotted path (LS-225)."""
    root = {name: m.globals[key] for key, _, _ in prog.state for name in [key[1]] if key[0] == prog.name}
    return root, tuple((key[0], key[1], Value(t, m.globals[key])) for key, t, _ in prog.state
                       if not is_aggregate(t) and not isinstance(t, HandleType))


def _run(prog, kind, target, args, fuel, depth) -> Outcome:
    m = Machine(fuel, depth)
    state = prog.state or [((prog.name, name), t, v) for name, t, v in prog.module_vars]
    m.globals = {key: (zero_value(t) if v is None else v) for key, t, v in state}
    m.create_memories(prog.memories)
    prog.state = state
    value = None
    failure = None
    try:
        m.depth = 1                     # the entry is depth 1 (SPEC-01 9.4)
        if kind == "function":
            f = target
            m.charge(f.name_pos)
            m.frame = {p.name: a for p, a in zip(f.params, args)}
            try:
                m.exec_list(f.body.stmts)
            except _Return as r:
                rt = f.result_type.value if isinstance(f.result_type, ErrorUnion) else f.result_type
                if isinstance(r.value, Failure):
                    failure = r.value
                elif r.value is not None:
                    if is_aggregate(rt):
                        raise Refused("a struct or array return value has no `.expect` form")
                    if isinstance(rt, HandleType):
                        raise Refused("a handle return value has no `.expect` form (ref/OPEN.md REF-OQ-45)")
                    if isinstance(rt, ErrorSet):
                        raise Refused("an error-set return value: the typed values of SPEC-09 CONF-01 have no "
                                      "error value (ref/OPEN.md REF-OQ-44)")
                    value = Value(rt, r.value)
        else:
            pos = target.pos if kind == "test" else (prog.script_pos or Position(prog.path, 1, 1))
            m.charge(pos)
            try:
                m.exec_list(target.body.stmts if kind == "test" else prog.script)
            except _Return as r:
                # An error that leaves a test by `try` fails it (SPEC-04 LS-315).
                if isinstance(r.value, Failure):
                    failure = r.value
    except FaultSignal as f:
        root, state = _state(m, prog)
        return Outcome("fault", bytes(m.out), m.fuel_used, record=f.record, globals=root, state=state)
    except Refused as r:
        return Outcome("refused", bytes(m.out), m.fuel_used, message=r.message)
    root, state = _state(m, prog)
    if failure is not None:
        return Outcome("error", bytes(m.out), m.fuel_used, globals=root, state=state, error=failure.lines())
    return Outcome("value", bytes(m.out), m.fuel_used, value=value, globals=root, state=state)


def _with_large_stack(fn):
    """Run `fn` on a thread with a large stack, so that the call-depth limit is
    always reached before Python's own limits (SPEC-09 REF-01)."""
    box = {}

    def target():
        try:
            box["value"] = fn()
        except BaseException as e:       # re-raised in the caller
            box["error"] = e

    old_limit = sys.getrecursionlimit()
    sys.setrecursionlimit(max(old_limit, 1_000_000))
    old_size = threading.stack_size()
    for size in (256, 128, 64):
        try:
            threading.stack_size(size * 1024 * 1024)
            break
        except ValueError:
            continue
    try:
        t = threading.Thread(target=target)
        t.start()
        t.join()
    finally:
        threading.stack_size(old_size)
        sys.setrecursionlimit(old_limit)
    if "error" in box:
        raise box["error"]
    return box["value"]


def loader(root: str | None):
    """`load(rel)` for check_module: parses the module at `root/rel`, or returns None when the
    file cannot be read (C3009). Every module of a program is read from one source root. Each
    segment must match its directory entry byte for byte, so a name that differs only in case
    is C3009 on every host, whether or not the file system folds case (SPEC-09 CINTC-12)."""
    def load(rel):
        if root is None:
            return None
        d = root
        try:
            for seg in rel.split("/"):
                if seg not in os.listdir(d):
                    return None
                d = os.path.join(d, seg)
            with open(d, "rb") as f:
                data = f.read()
        except OSError:
            return None
        return P.parse_module(decode_source(data, rel), rel)
    return load


def run_program(source: bytes, path: str, entry: str | None = None, fuel: int | None = None,
                depth: int = DEFAULT_DEPTH, args=(), test: str | None = None, root: str | None = None) -> Outcome:
    """Compile and run one module. Parsing, checking and execution all run on a
    thread with a large stack, so that nesting up to the SPEC-04 20 minimum (256)
    and well beyond never meets Python's recursion limit; nesting beyond the
    parser's limits is C9004 (parser.MAX_BLOCK_NESTING, parser.MAX_NESTING)."""

    return _with_large_stack(lambda: _compile_and_run(source, path, entry, fuel, depth, args, test, root))


def _compile_and_run(source, path, entry, fuel, depth, args, test, root=None) -> Outcome:
    try:
        if path.endswith(".cint"):
            raise CompileError(Diagnostic("C1001", Position(path, 1, 1),
                                          "`.cint` files belong to the legacy profile cint-bt27-legacy"))
        text = decode_source(source, path)
        module = P.parse_module(text, path)
        prog = check_module(module, loader(root))
        if fuel is not None and not (0 <= fuel <= M64):
            raise Refused("the fuel allowance must be an I64 with B >= 0 (SPEC-01 10.1)")
        if not (1 <= depth <= M64):
            raise Refused("the depth limit must be an I64 with D >= 1 (SPEC-01 9.4)")
        kind, target = _select_entry(prog, entry, test)
        values = _check_args(target, args) if kind == "function" else []
        if kind == "function" and target.name == "main" and entry is None and target.params:
            raise Refused("`main` takes no parameters")
    except CompileError as e:
        return Outcome("compile-error", diagnostic=e.diagnostic)
    except Refused as r:
        return Outcome("refused", message=r.message)
    return _run(prog, kind, target, values, fuel, depth)
