"""Loading a library and calling its exports (SPEC-03 6.1, A-3, A-7, A-12, A-13, H-11, H-12; BX10-02, BX10-07).

`cint.load` opens a library that `cint build --lib` wrote, building it first
when given a `.ci` source (`_build`), and reads its reflection table
(`_abi.read_library`): each export's name, kind, effect class, size and
parameter names, and canonical type signature (A-19, A-20), which `_sig`
decodes under every obligation of A-24. `Module.context()` creates a context
on the root module's descriptor (`modules[module_count-1]->info`), and a
call `ctx.<export>(...)` forms its arguments (`_args`), registers the memory
of each array argument once per context (P-20), and calls the export's `cx`
wrapper (A-12) with the carriers of A-13 and BX10-26:

- an `I8` to `U64` scalar by value, `Bool` as `uint8_t`, `T1` as `int8_t`,
  `T27` as `int64_t`, `Q<i>.<f>` as its storage integer, and `I128` to
  `I1024` as a pointer to their 64-bit limbs, least significant first;
- an array as a `cint_view` by value, and a kernel's scalar `out` as a
  pointer to its carrier;
- the result through a pointer, written only when the entry returns
  `CINT_OK`: a scalar as its carrier, a tuple as a C struct of its items, and
  `E!T` as a C struct of the tag (the set's underlying type, 0 for success)
  and the success value.

Registration and its end go through `Runtime.register` and `Runtime.release`
only. A registration lasts until its `View` or `Buffer` is released (or
collected) or the context is closed; a release that arrives while an entry
runs is queued and done when the entry returns, since the runtime refuses
registry changes during an entry (A-11).
"""
from __future__ import annotations

import collections
import contextlib
import ctypes
import itertools
import keyword
import os
import threading
import weakref

from . import _abi, _build, _sig
from ._args import bind_arguments
from ._buffer import Buffer
from ._elem import parse_elem
from ._errors import (
    CINT_BUSY, CINT_FAULT, CINT_FAULTED, CINT_HAZARD, CINT_HOST_ERROR, CINT_OK, CINT_REFUSED, CINT_RESOURCE,
    Busy, BuildError, Fault, Faulted, LoadError, Refused, error_set_family, exception_for_status)
from ._report import CopyRecord, EntryReport
from ._scalar import from_raw
from ._view import View

BACKENDS = ("ref", "cpu", "cuda", "vulkan", "metal", "webgpu")      # SPEC-09 BACK-15 names
I64_MIN, I64_MAX = -(1 << 63), (1 << 63) - 1
FUEL_UNBOUNDED = -1                                                   # H-11: a budget of -1 is unbounded
ARENA_ELEMENT_BYTES = 8                                               # LS-110 counts the frame arena in 8-byte elements
_context_keys = itertools.count(1)


# Carriers (A-13, BX10-26).

_INT_CARRIERS = {(1, True): ctypes.c_int8, (2, True): ctypes.c_int16, (4, True): ctypes.c_int32,
                 (8, True): ctypes.c_int64, (1, False): ctypes.c_uint8, (2, False): ctypes.c_uint16,
                 (4, False): ctypes.c_uint32, (8, False): ctypes.c_uint64}


def _carrier(elem):
    """The C type that carries a scalar of `elem` (a `cint._elem.Elem`)."""
    if elem.kind == "bool":
        return ctypes.c_uint8
    if elem.wide:
        return ctypes.c_uint64 * (elem.size // 8)
    return _INT_CARRIERS[(elem.size, elem.signed)]


def _to_c(elem, raw: int):
    """The C value of a raw scalar: an instance of its carrier."""
    carrier = _carrier(elem)
    if not elem.wide:
        return carrier(raw)
    bits = elem.size * 8
    raw &= (1 << bits) - 1
    return carrier(*((raw >> (64 * i)) & 0xFFFFFFFFFFFFFFFF for i in range(elem.size // 8)))


def _from_c(elem, value) -> int:
    """The raw scalar a carrier holds."""
    if not elem.wide:
        return int(value.value if hasattr(value, "value") else value)
    raw = 0
    for i, limb in enumerate(value):
        raw |= int(limb) << (64 * i)
    bits = elem.size * 8
    if elem.signed and raw >> (bits - 1):
        raw -= 1 << bits
    return raw


def _struct(name: str, fields: list):
    return type(name, (ctypes.Structure,), {"_fields_": fields})


class _Result:
    """The result carrier of an export: the C type its pointer points to, and
    the conversion of what the entry wrote to a Python value or an error."""

    def __init__(self, form, sig: _sig.Signature, set_classes: dict):
        self.form = form
        self.ctype = None
        self.error = None
        self.value_form = form
        if form is None:
            return
        if isinstance(form, tuple) and form and form[0] == "error":
            _, index, value_form = form
            d = sig.definitions[index]
            self.error = set_classes[d.name]
            self.value_form = value_form
            fields = [("tag", _carrier(parse_elem(d.underlying)))]
            value = self._ctype_of(value_form)
            if value is not None:
                fields.append(("value", value))
            self.ctype = _struct("cint_error_union", fields)
        else:
            self.ctype = self._ctype_of(form)

    @staticmethod
    def _ctype_of(form):
        if form is None:
            return None
        if isinstance(form, tuple):
            return _struct("cint_tuple", [("i%d" % i, _carrier(parse_elem(e))) for i, e in enumerate(form)])
        return _carrier(parse_elem(form))

    @staticmethod
    def _value(form, c):
        if form is None:
            return None
        if isinstance(form, tuple):
            return tuple(from_raw(_from_c(parse_elem(e), getattr(c, "i%d" % i)), e) for i, e in enumerate(form))
        e = parse_elem(form)
        return from_raw(_from_c(e, c), form)

    def convert(self, c):
        """The Python value of the written result, or the `ErrorResult` to
        raise for an error alternative (BX10-06)."""
        if self.error is None:
            return self._value(self.form, c)
        tag = int(c.tag)
        if tag != 0:
            return self.error.from_tag(tag)
        return self._value(self.value_form, c.value) if self.value_form is not None else None


# The export table.

class Export:
    """One export of a module as its table describes it (A-19): `name`,
    `kind` ("function" or "kernel"), `effect` ("pure", "observe", or
    "unrestricted"), `declaration` (rendered from the type signature), and
    `signature` (the decoded `cint._sig.Signature`). `callable` is False for
    an export whose types box 10 does not call (BX10-10, BX10-27), and
    `reason` then says why."""

    def __init__(self, row: _abi.ExportRow):
        self.name = row.name
        self.kind = row.kind
        self.effect = row.effect
        self.module_index = row.module
        self.size_names = row.size_names
        self.param_names = row.param_names
        self.call = row.call
        self.params = ()
        self.result_form = None
        self.reason = None
        try:
            self.signature = _sig.decode(row.signature)
        except _sig.SignatureError as why:
            # A-24: a rejected type signature makes its export not callable,
            # and the rest of the library stays usable.
            self.signature = None
            self.declaration = "%s(...)" % row.name
            self.reason = str(why)
            return
        self.declaration = _sig.render(self.signature, row.name, row.size_names, row.param_names,
                                       kernel=row.kind == "kernel")
        try:
            self.params, self.result_form = _sig.python_forms(self.signature, row.param_names)
        except (_sig.NoPythonForm, ValueError) as why:
            self.reason = str(why)
        if self.reason is None:
            self.reason = self._shape_problem()

    def _shape_problem(self) -> str | None:
        if not self.call:
            return "its table row has no cx wrapper (A-12)"
        if self.kind not in ("function", "kernel"):
            return "its table row has export %s, which box 10 does not call" % self.kind
        if self.kind == "kernel" and self.result_form is not None:
            return "it is a kernel with a result; a kernel returns through its out parameters (SPEC-02 F-1)"
        if self.kind == "function" and any(p.mode == "out" for p in self.params):
            return "it is a function with an out parameter; a function writes caller data through inout (LS-119)"
        return None

    @property
    def callable(self) -> bool:
        return self.reason is None

    def __repr__(self) -> str:
        return "<cint export %s %s (%s)%s>" % (self.kind, self.declaration, self.effect,
                                              "" if self.callable else ", not callable")


class _Entry:
    """The C side of one callable export: the `cx` wrapper as a ctypes
    function (CFUNCTYPE releases the GIL during the call, BX10-08) and its
    result carrier."""

    def __init__(self, export: Export, set_classes: dict):
        self.export = export
        self.result = _Result(export.result_form, export.signature, set_classes)
        argtypes = [ctypes.c_void_p, ctypes.c_int64]
        for p in export.params:
            if p.rank:
                argtypes.append(_abi.CintView)
            elif p.mode == "out":
                argtypes.append(ctypes.POINTER(_carrier(parse_elem(p.elem))))
            else:
                e = parse_elem(p.elem)
                argtypes.append(ctypes.POINTER(_carrier(e)) if e.wide else _carrier(e))
        if self.result.ctype is not None:
            argtypes.append(ctypes.POINTER(self.result.ctype))
        self.fn = ctypes.CFUNCTYPE(ctypes.c_int32, *argtypes)(export.call)


# Loading.

def load(source, *, backend: str = "cpu", require_verified: bool = False) -> "Module":
    """Load a library that `cint build --lib` wrote, or build one from a `.ci`
    source and load it (SPEC-03 6.1, BX10-02). A source is built into a
    directory keyed by the digests of its modules and the toolchain identity,
    so loading unchanged source again builds nothing; its source root is the
    source's directory. Raises `BuildError` when the build fails or no `cint`
    is available, and `LoadError` for a library this bridge cannot use."""
    if backend not in BACKENDS:
        raise ValueError("backend is one of %s, not %r (SPEC-09 BACK-15)" % (", ".join(BACKENDS), backend))
    if backend == "ref":
        raise NotImplementedError("backend=\"ref\" runs cpu-sir-interp, which is admitted at T4; until then "
                                  "cint.load builds with cint build --lib for backend=\"cpu\" (BX10-02)")
    if backend != "cpu":
        raise NotImplementedError("backend=%r: this bridge loads cpu-c17 libraries only; device backends arrive "
                                  "with their boxes" % backend)
    if require_verified not in (True, False):
        raise TypeError("require_verified is True or False")
    path = os.fspath(source)
    if not isinstance(path, str):
        raise TypeError("a source or library path is a str or os.PathLike of str")
    if require_verified:
        raise LoadError("require_verified=True: %s has no build record that this bridge reads, so its verification "
                        "status is missing (SPEC-07 SEC-HOST-2, SEC-HOST-4)" % path)
    if path.endswith(".ci"):
        library = _build.build_library(path)
    elif os.path.isfile(path):
        library = os.path.abspath(path)
    else:
        raise LoadError("%s is neither a .ci source nor a library file" % path)
    return Module._open(library)


class Module:
    """A loaded library: one program (BX10-07). `exports` maps each export
    name to its `Export`; `revision` is the program's revision identity (32
    bytes); `abi` the runtime's ABI version; `error_sets` maps the qualified
    name of each error set an export's result reaches to its `ErrorResult`
    subclass, which is also `mod.<last part of the name>` when no other set
    shares that last part (BX10-27)."""

    def __init__(self, *args, **kwargs):
        raise TypeError("a Module is made by cint.load(source)")

    @classmethod
    def _open(cls, library: str) -> "Module":
        problem = _abi.layout_problem()
        if problem is not None:
            raise LoadError(problem)
        try:
            lib = ctypes.CDLL(library)
        except OSError as failure:
            raise LoadError("%s could not be loaded: %s" % (library, failure)) from None
        try:
            table = _abi.read_library(lib)
            runtime = _abi.Runtime(lib)
        except _abi.LibraryProblem as problem:
            raise LoadError("%s cannot be used: %s" % (library, problem)) from None
        if runtime.abi != table.abi:
            raise LoadError("%s cannot be used: its runtime reports ABI %#x and its table %#x" % (library, runtime.abi,
                                                                                                   table.abi))
        return cls._from_table(library, table, runtime)

    @classmethod
    def _from_table(cls, path: str, table: _abi.LibraryTable, runtime) -> "Module":
        """A module over a table and a runtime: `_abi.Runtime`, or an object
        with its methods (the tests use one to call through signatures that
        cintc does not yet emit)."""
        m = object.__new__(cls)
        m.path = path
        m.abi = table.abi
        m.revision = table.revision
        m.records = table.records
        m._table = table
        m._runtime = runtime
        m.exports = {}
        for row in table.exports:
            if row.name not in m.exports:   # the root module's export of a name comes first (A-19)
                m.exports[row.name] = Export(row)
        sets, clash = {}, set()
        for export in m.exports.values():
            if export.signature is None:
                continue
            for q, spec in _sig.error_sets(export.signature).items():
                if sets.setdefault(q, spec) != spec:
                    clash.add(q)
        # A set with no values has no class (error_set_class), so neither has a
        # combined set over one; the exports whose result reaches them are
        # listed but not callable.
        empty = {q for q, spec in sets.items() if spec[0] == "declared" and not spec[1]} | clash
        usable = {q: spec for q, spec in sets.items()
                  if q not in empty and not (spec[0] == "combined" and set(spec[1]) & empty)}
        try:
            m.error_sets = error_set_family(usable)
        except ValueError as why:
            raise LoadError("%s cannot be used: %s" % (path, why)) from None
        m._entries = {}
        for export in m.exports.values():
            if not export.callable:
                continue
            missing = [q for q in _sig.error_sets(export.signature) if q not in m.error_sets]
            if missing:
                export.reason = ("its result's error set %s has no values, combines a set with none, or has two "
                                 "definitions in this library, so it has no ErrorResult class" % missing[0])
                continue
            m._entries[export.name] = _Entry(export, m.error_sets)
        by_short = collections.defaultdict(list)
        for q, c in m.error_sets.items():
            by_short[c.__name__].append(c)
        m._set_names = {k: v[0] for k, v in by_short.items() if len(v) == 1}
        return m

    def __getattr__(self, name: str):
        if name.startswith("_"):
            raise AttributeError(name)
        found = self.__dict__.get("_set_names", {}).get(name)
        if found is None:
            raise AttributeError("module %s has no error set named %s" % (self.__dict__.get("path"), name))
        return found

    def context(self, *, device=None, host=None, grants=None, record=None, check_inputs: bool = False,
                fuel: int | None = 10 ** 9, depth: int = 256, frame_arena_elements: int = 2_097_152) -> "Context":
        """Create a context (cint_ctx_create) on the root module's descriptor.
        `fuel` is the per-entry budget (H-11), None for unbounded; `depth` and
        `frame_arena_elements` set the depth limit and the frame-arena
        capacity in 8-byte elements (BX10-12). Prints are discarded: the
        context has no output stream."""
        if device is not None:
            raise NotImplementedError("device=: this bridge runs on the host CPU only")
        if host:
            raise NotImplementedError("host=: host functions for imported externs (A-10) arrive with cint.host; "
                                      "this bridge loads programs that import none")
        if grants is not None:
            raise NotImplementedError("grants=: services take grants (H-8a), and this bridge passes none yet")
        if record is not None:
            raise NotImplementedError("record=: recordings arrive with box 13 (BX10-01)")
        if check_inputs is not False:
            if check_inputs is not True:
                raise TypeError("check_inputs is True or False")
            raise NotImplementedError("check_inputs=True: the borrowed-input hazard check (SPEC-03 P-16) arrives "
                                      "with recordings in box 13")
        budget = _fuel(fuel)
        depth = _i64(depth, "depth")
        if depth < 0:
            raise ValueError("depth is 0 or more, not %d" % depth)
        elements = _i64(frame_arena_elements, "frame_arena_elements")
        if not 0 <= elements <= I64_MAX // ARENA_ELEMENT_BYTES:
            raise ValueError("frame_arena_elements is 0 to %d, not %d" % (I64_MAX // ARENA_ELEMENT_BYTES, elements))
        rt = self._runtime
        status, handle = rt.create(self._table.program, self._table.root_info, depth, elements * ARENA_ELEMENT_BYTES)
        if status != CINT_OK or not handle:
            if status == CINT_RESOURCE:
                raise MemoryError("cint_ctx_create could not allocate the context (CINT_RESOURCE)")
            raise ValueError("cint_ctx_create refused the configuration (status %d): depth %d, frame arena %d "
                             "elements" % (status, depth, elements))
        return Context._make(self, handle, budget)

    def __repr__(self) -> str:
        return "<cint.Module %s, %d exports>" % (os.path.basename(self.path), len(self.exports))


def _i64(value, what: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise TypeError("%s is an int, not %s" % (what, type(value).__name__))
    if not I64_MIN <= value <= I64_MAX:
        raise ValueError("%s %d does not fit the I64 field of cint_ctx_config" % (what, value))
    return value


def _fuel(value) -> int:
    """The `int64_t` fuel argument of a budget (H-11): None is unbounded (-1);
    an int crosses as it is, so the runtime refuses one below -1."""
    if value is None:
        return FUEL_UNBOUNDED
    if isinstance(value, bool) or not isinstance(value, int):
        raise TypeError("fuel is an int or None (unbounded), not %s" % type(value).__name__)
    if not I64_MIN <= value <= I64_MAX:
        raise Refused("fuel %d does not fit the I64 budget of a cx wrapper (H-11)" % value, code="E_NARROW",
                      operands=(value,), operand_types=("Z",))
    return value


# Registrations.

class _Registration:
    """One registration of a `View` or `Buffer` with one context (P-20)."""
    __slots__ = ("context", "ident", "generation", "finalizer")

    def __init__(self, context: "Context", ident: int, generation: int, finalizer):
        self.context = weakref.ref(context)
        self.ident = ident
        self.generation = generation
        self.finalizer = finalizer

    def end(self):
        """End the registration: now, or when the running entry returns."""
        if self.finalizer.detach() is None:
            return
        ctx = self.context()
        if ctx is not None:
            ctx._end_registration(self.ident)


# Contexts and calls.

class Context:
    """One context (SPEC-03 5.3): module state, a fault slot, and the
    registrations of the memory passed to it. One entry runs at a time; a
    call while another runs raises `cint.Busy`. Use it as a context manager,
    or call `close()`, to destroy it. Exports are reached as `ctx.<name>` or
    `ctx.entry(name)`."""

    def __init__(self, *args, **kwargs):
        raise TypeError("a Context is made by Module.context()")

    @classmethod
    def _make(cls, module: Module, handle: int, fuel: int) -> "Context":
        c = object.__new__(cls)
        d = c.__dict__
        d["module"] = module
        d["fuel"] = fuel
        d["_rt"] = module._runtime
        d["_handle"] = handle
        d["_key"] = next(_context_keys)
        d["_lock"] = threading.Lock()
        d["_pending"] = collections.deque()
        d["_holders"] = weakref.WeakSet()
        d["_last"] = None
        d["_destroy"] = weakref.finalize(c, module._runtime.destroy, handle)
        return c

    # Attribute access: exports by name.

    def __getattr__(self, name: str):
        if name.startswith("_"):
            raise AttributeError(name)
        if keyword.iskeyword(name):
            raise AttributeError("%s is a Python keyword: reach it with ctx.entry(%r)" % (name, name))
        return self.entry(name)

    def __setattr__(self, name: str, value):
        if name == "fuel":
            self.__dict__["fuel"] = _fuel(value)
            return
        raise AttributeError("a Context has no settable attribute %s" % name)

    def __dir__(self):
        return sorted(set(super().__dir__()) | {n for n in self.module.exports if n.isidentifier()
                                                and not keyword.iskeyword(n)})

    def entry(self, name: str) -> "BoundEntry":
        """The export `name` as a callable bound to this context."""
        export = self.module.exports.get(name)
        if export is None:
            raise AttributeError("module %s has no export named %s" % (os.path.basename(self.module.path), name))
        return BoundEntry(self, export)

    # State.

    @property
    def closed(self) -> bool:
        return not self._destroy.alive

    def _check_open(self):
        if self.closed:
            raise ValueError("this context is closed; make a new one with Module.context()")

    def close(self):
        """Destroy the context: its registrations end with it. Idempotent;
        raises `cint.Busy` while an entry runs."""
        if self.closed:
            return
        if not self._lock.acquire(blocking=False):
            raise Busy("an entry is running on this context, so it cannot be closed now")
        try:
            for holder in list(self._holders):
                r = holder._registrations.pop(self._key, None)
                if r is not None:
                    r.finalizer.detach()
            self._holders.clear()
            self._pending.clear()
            self._destroy()
        finally:
            self._lock.release()

    def __enter__(self) -> "Context":
        self._check_open()
        return self

    def __exit__(self, *exc):
        self.close()
        return False

    def clear_fault(self) -> None:
        """Clear the fault record (A-7a); no other state changes."""
        with self._exclusive():
            status = self._rt.clear(self._handle)
        if status == CINT_BUSY:
            raise Busy("an entry is running on this context")
        if status != CINT_OK:
            raise ValueError("cint_ctx_clear_fault returned status %d" % status)

    def fuel_consumed(self) -> int:
        """The fuel the last entry consumed (H-11)."""
        with self._exclusive():
            status, n = self._rt.consumed(self._handle)
        if status != CINT_OK:
            raise ValueError("cint_fuel_consumed returned status %d" % status)
        return n

    def last_entry(self) -> EntryReport | None:
        """What the last entry copied (BX10-17): a function entry copies nothing
        (BX10-03 A); a kernel entry that succeeds lists each array it wrote, a
        borrowed View as `publish-copy` (P-15) and a Buffer as `staging`, since
        this runtime stages every output rather than renaming it (M-31). An entry
        that fails published nothing and lists no copy. None before the first
        entry."""
        return self._last

    def fault(self) -> Fault | None:
        """The fault the context holds, or None."""
        with self._exclusive():
            record = self._rt.fault(self._handle)
        return Fault.from_record(record) if record is not None else None

    def checkpoint(self) -> bytes:
        raise NotImplementedError("checkpoints (H-19) arrive with box 13 (BX10-01)")

    def restore(self, data: bytes) -> None:
        raise NotImplementedError("restore (H-19a) arrives with box 13 (BX10-01)")

    def entry_scope(self):
        raise NotImplementedError("entry_scope (BX10-05) needs checkpoints, which arrive with box 13")

    def state_hash(self) -> bytes:
        raise NotImplementedError("state_hash (M-5) arrives with checkpoints in box 13")

    def global_view(self, name: str) -> View:
        raise NotImplementedError("global_view needs context-owned views, which this bridge does not make yet")

    def __repr__(self) -> str:
        return "<cint.Context of %s%s>" % (os.path.basename(self.module.path), ", closed" if self.closed else "")

    @contextlib.contextmanager
    def _exclusive(self):
        self._check_open()
        if not self._lock.acquire(blocking=False):
            raise Busy("an entry is already running on this context (SPEC-03 A-11)")
        try:
            self._drain()
            yield
        finally:
            self._drain()
            self._lock.release()

    # Registrations: Runtime.register and Runtime.release are the only calls.

    def _register(self, holder) -> _Registration:
        r = holder._registrations.get(self._key)
        if r is not None:
            return r
        region = holder.region
        elem = parse_elem(region.elem)
        # A borrowed View publishes a kernel's output by copy only when it asked to (P-15);
        # a Buffer's storage is the bridge's, so this runtime, which cannot rename it, stages
        # the output and copies it on success (M-31).
        publish = (isinstance(holder, View) and holder.publish == "copy") or (
            isinstance(holder, Buffer) and region.writable)
        status, ident, generation = self._rt.register(self._handle, region.address, elem, region.extent,
                                                      region.writable, publish)
        if status != CINT_OK:
            if status == CINT_REFUSED:
                raise self._refusal("the memory of this argument could not be registered")
            raise exception_for_status(status, mid_entry=False,
                                       message="the context has no room for another registration")
        r = _Registration(self, ident, generation, weakref.finalize(holder, self._pending.append, ident))
        holder._registrations[self._key] = r
        self._holders.add(holder)
        return r

    def _end_registration(self, ident: int):
        if self.closed:
            return
        if self._lock.acquire(blocking=False):
            try:
                self._rt.release(self._handle, ident)
            finally:
                self._lock.release()
        else:
            self._pending.append(ident)

    def _drain(self):
        """Release the queued registrations; the caller holds the lock."""
        while self._pending and not self.closed:
            self._rt.release(self._handle, self._pending.popleft())

    def _refusal(self, what: str) -> Refused:
        reason = self._rt.refusal_reason(self._handle)
        name, text = _abi.REFUSALS.get(reason, ("reason %s" % reason, "the runtime gave no known reason"))
        code = "E_NARROW" if name == "bool" else "E_UNSUPPORTED"
        return Refused("%s: %s (refusal reason %s)" % (what, text, name), code=code, reason=name)

    # A call.

    def _call(self, export: Export, args: tuple, kwargs: dict):
        if not export.callable:
            raise NotImplementedError("%s: %s" % (export.declaration, export.reason))
        entry = self.module._entries[export.name]
        fuel = _fuel(kwargs.pop("fuel", self.fuel)) if "fuel" not in export.param_names else self.fuel
        with self._exclusive():
            with bind_arguments(export.params, args, kwargs) as call, contextlib.ExitStack() as stack:
                bounds = iter(call.bounds)
                c_args, outs, written, notes, copies, holders = [], [], [], [], [], {}
                for p in export.params:
                    e = parse_elem(p.elem)
                    if not p.argument:
                        out = _carrier(e)()
                        outs.append((p, out))
                        c_args.append(ctypes.byref(out))
                        continue
                    b = next(bounds)
                    if not p.rank:
                        value = _to_c(e, b.value)
                        c_args.append(ctypes.byref(value) if e.wide else value)
                        continue
                    holder = b.value
                    holders[p.name] = holder
                    if isinstance(holder, View):
                        stack.enter_context(holder._entry_exchange())
                        notes.extend(holder.notes)
                    c_args.append(self._view(holder, p))
                    if p.written and isinstance(holder, Buffer):
                        written.append(holder)
                    if p.written and export.kind == "kernel":
                        copies.append(CopyRecord(p.name, "staging" if isinstance(holder, Buffer) else "publish-copy",
                                                 p.elem, holder.size))
                result = entry.result.ctype() if entry.result.ctype is not None else None
                if result is not None:
                    c_args.append(ctypes.byref(result))
                self.__dict__["_last"] = EntryReport(export.name, (), tuple(notes))
                status = entry.fn(self._handle, fuel, *c_args)
                if status == CINT_OK:
                    for holder in written:
                        holder._mark_published()
                    # A dispatch that succeeds copied each output it staged (M-30a, M-31).
                    self.__dict__["_last"] = EntryReport(export.name, tuple(copies), tuple(notes))
                    return self._outcome(entry, result, outs)
                failure = self._failure(status, fuel)
                if isinstance(failure, Fault) and failure.operation == "bind.limit" and failure.operands:
                    _publish_hint(failure, export, holders)
                raise failure

    def _view(self, holder, p) -> _abi.CintView:
        """The `cint_view` of an array argument over its registration."""
        r = self._register(holder)
        region = holder.region
        e = parse_elem(region.elem)
        v = _abi.CintView(buffer=r.ident, generation=r.generation, rank=len(region.shape),
                          perm=_abi.VIEW_WRITE if (p.written and region.writable) else _abi.VIEW_READ,
                          origin=region.origin)
        v.type.code, v.type.storage, v.type.frac_bits, v.type.record_id = e.cint_type()
        for i, (n, s) in enumerate(zip(region.shape, region.strides)):
            v.shape[i] = n
            v.stride[i] = s
        return v

    def _outcome(self, entry: _Entry, result, outs: list):
        if outs:
            values = tuple(from_raw(_from_c(parse_elem(p.elem), o), p.elem) for p, o in outs)
            return values[0] if len(values) == 1 else values
        if result is None:
            return None
        value = entry.result.convert(result)
        if isinstance(value, BaseException):
            raise value
        return value

    def _failure(self, status: int, fuel: int) -> BaseException:
        if status in (CINT_FAULT, CINT_HOST_ERROR, CINT_HAZARD):
            record = self._rt.fault(self._handle)
            if record is None:
                return Fault("the entry returned status %d and the context holds no readable fault record" % status)
            return exception_for_status(status, record=record)
        if status == CINT_FAULTED:
            record = self._rt.fault(self._handle)
            return Faulted(Fault.from_record(record) if record is not None else None)
        if status == CINT_REFUSED:
            if fuel < FUEL_UNBOUNDED and self._rt.refusal_reason(self._handle) == 0:
                return Refused("fuel %d is not a budget: a budget is 0 or more, or -1 (None) for unbounded (H-11)"
                               % fuel, code="E_NARROW", operands=(fuel,), operand_types=("I64",))
            return self._refusal("the entry was refused before it ran (H-12)")
        if status == CINT_BUSY:
            reason = self._rt.refusal_reason(self._handle)
            text = _abi.REFUSALS[reason][1] if reason in _abi.REFUSALS and reason else \
                "an entry or effect is already active on this context"
            return Busy(text, code="E_ALIAS" if reason == 13 else None)
        if status == CINT_RESOURCE:
            record = self._rt.fault(self._handle)
            return exception_for_status(status, record=record, mid_entry=record is not None)
        try:
            return exception_for_status(status)
        except ValueError:
            return ValueError("the entry returned status %d, which is not one of SPEC-03 A-6" % status)


def _publish_hint(failure: Fault, export: Export, holders: dict):
    """P-15: a borrowed View without publish="copy" bound to a kernel's output
    faults E_UNSUPPORTED (M-30a); the message says what to pass instead."""
    index = failure.operands[0]
    if not isinstance(index, int) or not 0 <= index < len(export.params):
        return
    p = export.params[index]
    holder = holders.get(p.name)
    if isinstance(holder, View) and holder.publish != "copy":
        failure.args = (str(failure) + "\n\n%s is a borrowed View bound to %s: borrow it with writable=True, "
                        "publish=\"copy\" to have the output copied in on success, or pass a Buffer from "
                        "cint.empty (SPEC-03 P-15, M-30a)" % (p.name, p.describe()),)


class BoundEntry:
    """An export bound to a context: calling it runs the entry. `inplace`
    is the named in-place operation of SPEC-02 P-6."""

    def __init__(self, context: Context, export: Export):
        self.context = context
        self.export = export

    @property
    def name(self) -> str:
        return self.export.name

    def __call__(self, *args, **kwargs):
        return self.context._call(self.export, args, kwargs)

    def inplace(self, *args, **kwargs):
        raise NotImplementedError("%s has no named in-place operation: cintc declares none yet (SPEC-02 P-6)"
                                  % self.export.name)

    def __repr__(self) -> str:
        return "<cint entry %s>" % self.export.declaration
