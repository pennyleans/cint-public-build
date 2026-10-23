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

`backend="cuda"` builds the library for `gpu-cuda` (`_build`), whose
kernels run on CUDA device 0 (SPEC-03 A-16a, BX11-16). A context on such a
module with `device=` a CUDA device registers CUDA memory as placement
device(1): device `Buffer`s and CUDA arrays borrowed through DLPack, whose
producer is asked again at each entry on the context's stream (P-10); an
array on another device is refused before entry (P-11). A context without
`device=` runs the kernels over host memory, which the runtime maps.

Registration and its end go through `Runtime.register` and `Runtime.release`
only. A registration lasts until its `View` or `Buffer` is released (or
collected), the context is restored, or the context is closed; a release
that arrives while an entry runs is queued and done when the entry returns,
since the runtime refuses registry changes during an entry (A-11).

Checkpoints, restore, and the state hash (box 13 unit 9; SPEC-03 5.3, H-19,
H-19a, M-5, M-11, M-11a) go through `rt/cint_state.h` in the library
(`_state`): a context binds its program's module state table
(`cint_observer_state`) before its first checkpoint or restore. A restore
removes every registration (M-11a), and each `View` or `Buffer` registers
again on its next use, with a new identifier (P-20; BX13-04 A). A context
made with `record=` makes every call through its `cint.Recorder` (`_replay`;
H-14b): an entry through `cint_rec_entry` with the 64-bit patterns of its
arguments (SPEC-09 CONF-13 X-3), and checkpoints, restores, and clears as
recorded events.
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

from . import _abi, _build, _cuda, _replay, _sig, _state
from ._args import bind_arguments
from ._buffer import Buffer
from ._elem import parse_elem
from ._errors import (
    CINT_BUSY, CINT_FAULT, CINT_FAULTED, CINT_HAZARD, CINT_HOST_ERROR, CINT_OK, CINT_REFUSED, CINT_RESOURCE,
    Busy, BuildError, Fault, Faulted, LoadError, Refused, error_set_family, exception_for_status)
from ._replay import Recorder
from ._report import CopyRecord, EntryReport
from ._scalar import from_raw
from ._view import View

BACKENDS = ("ref", "cpu", "cuda", "vulkan", "metal", "webgpu")      # SPEC-09 BACK-15 names
I64_MIN, I64_MAX = -(1 << 63), (1 << 63) - 1
M64 = (1 << 64) - 1
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
            return "its table row has export %s, which the package does not call" % self.kind
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
    source's directory. `backend` is "cpu" (`cpu-c17`) or "cuda" (`gpu-cuda`,
    BX11-16); a library file must have been built for it. Raises
    `BuildError` when the build fails or no `cint` is available, and
    `LoadError` for a library this bridge cannot use."""
    if backend not in BACKENDS:
        raise ValueError("backend is \"ref\", \"cpu\" or \"cuda\", not %r (SPEC-09 BACK-15)" % (backend,))
    if backend == "ref":
        raise NotImplementedError("backend=\"ref\" runs cpu-sir-interp, which cint.load does not build yet; "
                                  "cint.load builds with cint build --lib for backend=\"cpu\"")
    if backend not in ("cpu", "cuda"):
        raise NotImplementedError("backend=%r is not built: this bridge loads cpu-c17 and gpu-cuda libraries"
                                  % backend)
    if require_verified not in (True, False):
        raise TypeError("require_verified is True or False")
    path = os.fspath(source)
    if not isinstance(path, str):
        raise TypeError("a source or library path is a str or os.PathLike of str")
    if require_verified:
        raise LoadError("require_verified=True: %s has no build record that this bridge reads, so its verification "
                        "status is missing" % path)
    if path.endswith(".ci"):
        library = _build.build_library(path, backend=backend)
    elif os.path.isfile(path):
        library = os.path.abspath(path)
    else:
        raise LoadError("%s is neither a .ci source nor a library file" % path)
    return Module._open(library, backend)


class Module:
    """A loaded library: one program (BX10-07). `exports` maps each export
    name to its `Export`; `revision` is the program's revision identity (32
    bytes); `abi` the runtime's ABI version; `backend` "cpu" or "cuda", the
    backend it was built for; `error_sets` maps the qualified
    name of each error set an export's result reaches to its `ErrorResult`
    subclass, which is also `mod.<last part of the name>` when no other set
    shares that last part (BX10-27)."""

    def __init__(self, *args, **kwargs):
        raise TypeError("a Module is made by cint.load(source)")

    @classmethod
    def _open(cls, library: str, backend: str = "cpu") -> "Module":
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
        built = "cuda" if runtime.cuda else "cpu"
        if built != backend:
            raise LoadError("%s was built for backend=%r (%s), not backend=%r" % (
                library, built, "gpu-cuda" if runtime.cuda else "cpu-c17", backend))
        return cls._from_table(library, table, runtime, backend)

    @classmethod
    def _from_table(cls, path: str, table: _abi.LibraryTable, runtime, backend: str = "cpu") -> "Module":
        """A module over a table and a runtime: `_abi.Runtime`, or an object
        with its methods (the tests use one to call through signatures that
        cintc does not yet emit)."""
        m = object.__new__(cls)
        m.path = path
        m.backend = backend
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

    # The canonical state code (rt/cint_state.h) and the observer table (CONF-13).

    def _state_api(self) -> _state.StateApi:
        """The library's checkpoint, recording, and replay functions; raises
        NotImplementedError for a library built without them."""
        api = self.__dict__.get("_state")
        if api is not None:
            return api
        problem = _state.layout_problem()
        lib = getattr(self._runtime, "lib", None)
        if problem is None and lib is None:
            problem = "this module's runtime is not a loaded library, so it has no canonical state code"
        if problem is None:
            api = _state.StateApi(lib)
            if api.stale is not None:
                problem = ("%s was built at %s, whose checkpoint and recording structures are smaller than "
                           "ABI 3.3's (SPEC-03 A-5): load its .ci source with cint.load to rebuild it"
                           % (self.path, api.stale))
            elif api.missing is not None:
                problem = ("%s was built without the canonical state code of rt/cint_state.c and rt/cint_rec.c "
                           "(it lacks %s): load its .ci source with cint.load, whose build links it"
                           % (self.path, api.missing))
        if problem is not None:
            raise NotImplementedError(problem)
        _state.remember(api)
        self.__dict__["_state"] = api
        return api

    def _symbol(self, name: str) -> int | None:
        """The address of a data symbol the library exports, or None."""
        try:
            return ctypes.addressof(ctypes.c_uint8.in_dll(self._runtime.lib, name))
        except (ValueError, AttributeError):
            return None

    def _state_table_address(self) -> int | None:
        """`cint_observer_state`, absent for a program without module state."""
        return self._symbol("cint_observer_state")

    def _observer_address(self) -> int:
        address = self._symbol("cint_observer_desc")
        if address is None:
            raise NotImplementedError("%s exports no cint_observer_desc, so its entries cannot be recorded or "
                                      "replayed (SPEC-09 CONF-13)" % self.path)
        return address

    def _observer_entry(self, export: Export):
        """(index, type tags) of the export's observer entry, the entry
        `cint_rec_entry` and replay call; None when it has none, as for an
        export whose types are not integers and Bool (CONF-13 X-3)."""
        table = self.__dict__.get("_observers")
        if table is None:
            table = {}
            obs = _state.Observer.from_address(self._observer_address())
            rows = (_state.ObserverEntry * obs.entry_count).from_address(obs.entries) if obs.entry_count else ()
            for i, e in enumerate(rows):
                n = ctypes.c_uint32.from_address(e.types + 4).value if e.types else 0
                if not e.types or n > 16:
                    continue
                name = ctypes.string_at(e.name, e.name_len).decode("utf-8")
                table[(e.module, name)] = (i, tuple((ctypes.c_uint32 * (n + 2)).from_address(e.types)))
            self.__dict__["_observers"] = table
        found = table.get((export.module_index, export.name))
        if found is None or found[1][1] != len(export.params):
            return None
        return found

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
        `device` is a `cint.Device` or its spec: a CUDA device, for a module
        loaded with backend="cuda", whose kernels then take that device's
        memory (SPEC-03 A-16a, BX11-16), or the CPU. `fuel` is the per-entry
        budget (H-11), None for unbounded; `depth` and `frame_arena_elements`
        set the depth limit and the frame-arena capacity in 8-byte elements
        (BX10-12). Prints are discarded: the context has no output stream.
        `record` is a `cint.Recorder`, which then records every call on the
        context, from a checkpoint of it as it is created (SPEC-03 H-14b)."""
        if not _cuda.is_host(device) and self.backend != "cuda":
            raise ValueError("device=%s: this module was loaded with backend=%r; a context on a CUDA device needs a "
                             "module loaded with backend=\"cuda\"" % (device, self.backend))
        dev = _cuda.resolve(device)
        if dev is not None:
            if not self._runtime.devices:
                raise LoadError("%s has runtime ABI %d.%d, whose context configuration has no device (SPEC-03 "
                                "A-16a; ABI 3.1): rebuild it" % (self.path, self.abi >> 16,
                                                                         self.abi & 0xFFFF))
        if host:
            raise NotImplementedError("host=: host functions for imported externs (A-10) arrive with cint.host; "
                                      "this bridge loads programs that import none")
        if grants is not None:
            raise NotImplementedError("grants=: services take grants (H-8a), and this bridge passes none yet")
        if record is not None:
            if not isinstance(record, Recorder):
                raise TypeError("record is a cint.Recorder or None, not %s" % type(record).__name__)
            if record.closed or record.context is not None:
                raise ValueError("this Recorder is closed or already records a context: a recording holds one "
                                 "context, so make another Recorder")
            api = self._state_api()
            if getattr(api, "rec_end", None) is None:
                raise NotImplementedError("%s exports no recorder (cint_rec_begin to cint_rec_end, rt/cint_state.h)"
                                          % self.path)
            observer = self._observer_address()
        if check_inputs is not False:
            if check_inputs is not True:
                raise TypeError("check_inputs is True or False")
            raise NotImplementedError("check_inputs=True: the borrowed-input hazard check (SPEC-03 P-16) is not "
                                      "provided yet")
        budget = _fuel(fuel)
        depth = _i64(depth, "depth")
        if depth < 0:
            raise ValueError("depth is 0 or more, not %d" % depth)
        elements = _i64(frame_arena_elements, "frame_arena_elements")
        if not 0 <= elements <= I64_MAX // ARENA_ELEMENT_BYTES:
            raise ValueError("frame_arena_elements is 0 to %d, not %d" % (I64_MAX // ARENA_ELEMENT_BYTES, elements))
        rt = self._runtime
        extra = {} if dev is None else {"device": dev._record}
        status, handle = rt.create(self._table.program, self._table.root_info, depth, elements * ARENA_ELEMENT_BYTES,
                                   **extra)
        if status != CINT_OK or not handle:
            if status == CINT_RESOURCE:
                raise MemoryError("cint_ctx_create could not allocate the context (CINT_RESOURCE)")
            raise ValueError("cint_ctx_create refused the configuration (status %d): depth %d, frame arena %d "
                             "elements%s" % (status, depth, elements, "" if dev is None else ", device %s" % dev))
        c = Context._make(self, handle, budget, dev)
        if record is not None:
            try:
                c._bind(api)
                record._begin(c, api, observer, _build.program_code(self.path))
            except BaseException:
                c.close()
                raise
            c.__dict__["_recorder"] = record
            c._destroy.detach()
            c.__dict__["_destroy"] = weakref.finalize(c, _replay.end_then_destroy, rt.destroy, handle, record._live)
        return c

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
    `ctx.entry(name)`. `device` is the `cint.Device` its kernels take memory
    of: a CUDA device, or the CPU."""

    def __init__(self, *args, **kwargs):
        raise TypeError("a Context is made by Module.context()")

    @classmethod
    def _make(cls, module: Module, handle: int, fuel: int, device=None) -> "Context":
        c = object.__new__(cls)
        d = c.__dict__
        d["module"] = module
        d["device"] = device or _cuda.device("cpu")
        d["_stream_value"] = None
        d["fuel"] = fuel
        d["_rt"] = module._runtime
        d["_handle"] = handle
        d["_key"] = next(_context_keys)
        d["_lock"] = threading.Lock()
        d["_pending"] = collections.deque()
        d["_holders"] = weakref.WeakSet()
        d["_last"] = None
        d["_recorder"] = None
        d["_bound"] = False
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
        """Destroy the context: its registrations end with it, and a recording
        of it ends first. Idempotent; raises `cint.Busy` while an entry runs."""
        if self.closed:
            return
        if self._recorder is not None:
            try:
                self._recorder.close()
            except OSError:
                self._destroy_now()
                raise
        self._destroy_now()

    def _destroy_now(self):
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
        """Clear the fault record (A-7a); no other state changes. While
        recording, a clear of a held fault is a `clear_fault` event (H-14)."""
        with self._exclusive():
            rec = self._recording()
            status = self.module._state_api().rec_clear_fault(rec) if rec else self._rt.clear(self._handle)
        if status == CINT_BUSY:
            raise Busy("an entry is running on this context")
        if status == CINT_RESOURCE:
            raise MemoryError("cint_rec_clear_fault could not allocate (CINT_RESOURCE)")
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
        that fails published nothing and lists no copy, except that a kernel's
        fault at a work-item on a device lists each device array as
        `fault-readback`, read back for the host's re-execution of that item
        (SPEC-02 B-16b). None before the first entry."""
        return self._last

    def fault(self) -> Fault | None:
        """The fault the context holds, or None."""
        with self._exclusive():
            record = self._rt.fault(self._handle)
        return Fault.from_record(record) if record is not None else None

    # Checkpoints (SPEC-03 5.3, H-19, H-19a; box 13 note 5).

    @property
    def recorder(self) -> Recorder | None:
        """The `cint.Recorder` that records this context, or None."""
        return self._recorder

    def _bind(self, api: _state.StateApi):
        """Bind the program's module state once (cint_ctx_bind_state)."""
        if self._bound:
            return
        status = api.bind_state(self._handle, self.module._state_table_address())
        if status != CINT_OK:
            raise LoadError("%s: cint_ctx_bind_state refused the library's own state table (status %d)"
                            % (self.module.path, status))
        self.__dict__["_bound"] = True

    def _recording(self) -> int | None:
        """The recorder handle while recording; refused once the recording ended."""
        rec = self._recorder
        if rec is None:
            return None
        if rec.ended:
            raise Refused("the recording of this context ended at an entry that returned CINT_RESOURCE (SPEC-03 "
                          "A-6, H-14b); close its Recorder to use the context again", reason="recording")
        return rec._handle

    def _end_recording(self, handle: int) -> int:
        """`cint_rec_end`: the context gets its output back and is no longer recorded."""
        with self._exclusive():
            status = self.module._state_api().rec_end(handle)
            self.__dict__["_recorder"] = None
        return status

    def _state_failure(self, status: int, what: str) -> BaseException:
        if status == CINT_BUSY:
            return Busy("%s: a buffer of this context has a live export lease (SPEC-03 A-9)" % what)
        if status == CINT_RESOURCE:
            return MemoryError("%s could not allocate (CINT_RESOURCE)" % what)
        return Refused("%s refused this context: %s" % (what, _CHECKPOINT_REFUSAL), reason="state")

    def checkpoint(self) -> bytes:
        """A `CINT-CKPT-1` of this context between entries, Ready or Faulted
        (SPEC-03 H-19; BX13-06): the envelope and the canonical state, which
        holds the fault record of a Faulted context. While recording, it is a
        `checkpoint` event (H-14)."""
        api = self.module._state_api()
        with self._exclusive():
            self._bind(api)
            status, data = api.take_checkpoint(self._handle, self._recording())
        if status != CINT_OK:
            raise self._state_failure(status, "cint_ctx_checkpoint")
        return data

    def restore(self, data) -> None:
        """Restore a checkpoint (SPEC-03 H-19a, M-11, M-11a). Every check runs
        before any state changes: bytes that are not a well-formed
        `CINT-CKPT-1` (case 29a), or a checkpoint of another revision (case
        29), raise `cint.Refused` with the context unchanged. Module state,
        the fault record, and the effect-log position come back; identifiers
        are never reused, since each counter becomes the greater of the
        checkpoint's and the context's (case 29b); and every registration
        ends, so each `View` or `Buffer` registers again on its next use (P-20;
        BX13-04 A). While recording, it is a `restore` event (H-14)."""
        if not isinstance(data, (bytes, bytearray, memoryview)):
            raise TypeError("a checkpoint is bytes, as Context.checkpoint() returns, not %s" % type(data).__name__)
        data = bytes(data)
        api = self.module._state_api()
        with self._exclusive():
            self._bind(api)
            rec = self._recording()
            status = api.rec_restore(rec, data, len(data)) if rec else api.restore(self._handle, data, len(data))
            if status == CINT_OK:
                self._forget_registrations()
                return
        if status == CINT_REFUSED:
            raise self._restore_refusal(api, data)
        raise self._state_failure(status, "cint_ctx_restore")

    def _restore_refusal(self, api: _state.StateApi, data: bytes) -> Refused:
        info = api.decode_checkpoint(data)
        if info is None:
            return Refused("these %d bytes are not a well-formed CINT-CKPT-1 (SPEC-03 H-19a, case 29a); the context "
                           "is unchanged" % len(data), reason="malformed")
        revision = bytes(info.revision)
        if self.module.revision is not None and revision != self.module.revision:
            return Refused("the checkpoint is of revision %s and this context runs revision %s: a checkpoint "
                           "restores only into its own revision (SPEC-03 H-19, case 29); the context is unchanged"
                           % (revision.hex(), self.module.revision.hex()), reason="revision")
        return Refused("the checkpoint names globals, a record type, or a fault position this program does not "
                       "have, or %s (SPEC-03 H-19a); the context is unchanged" % _CHECKPOINT_REFUSAL,
                       reason="state")

    def _forget_registrations(self):
        """M-11a: the restore removed every registration, so each `View` and
        `Buffer` registers again on its next use (P-20; BX13-04 A). The
        caller holds the lock."""
        for holder in list(self._holders):
            r = holder._registrations.pop(self._key, None)
            if r is not None:
                r.finalizer.detach()
        self._holders.clear()
        self._pending.clear()

    def state_hash(self) -> bytes:
        """The SHA-256 of the canonical state (SPEC-01 IM-154, SPEC-03 M-5),
        32 bytes: equal on both CPU backends for the same history."""
        api = self.module._state_api()
        with self._exclusive():
            self._bind(api)
            status, h = api.hash_of(self._handle)
        if status != CINT_OK:
            raise self._state_failure(status, "cint_ctx_state_hash")
        return h

    @contextlib.contextmanager
    def entry_scope(self):
        """`with ctx.entry_scope():` takes a checkpoint when the block starts
        and restores it when a `cint.Fault` leaves the block, which then
        continues (BX10-05; box 13 default BX13-14). It needs a Ready context
        (`cint.Faulted` otherwise). An error result, any other exception, and
        a normal exit restore nothing. Scopes nest, each with its own
        checkpoint; while recording, the checkpoint and the restore are
        recorded events (H-14)."""
        held = self.fault()
        if held is not None:
            raise Faulted(held)
        saved = self.checkpoint()
        try:
            yield self
        except Fault:
            self.restore(saved)
            raise

    def global_view(self, name: str) -> View:
        raise NotImplementedError("global_view needs context-owned views, which this bridge does not make yet")

    def __repr__(self) -> str:
        return "<cint.Context of %s%s%s>" % (os.path.basename(self.module.path),
                                             "" if self.device.is_host else " on %s" % self.device,
                                             ", closed" if self.closed else "")

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
        extra = {} if region.device is None else {"device": True}
        status, ident, generation = self._rt.register(self._handle, region.address, elem, region.extent,
                                                      region.writable, publish, **extra)
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

    # Devices (SPEC-03 P-10, P-11).

    def _placement_problem(self, placement: str | None, what: str) -> Refused | None:
        """The refusal of memory on `placement` (None for host memory) bound
        on this context, or None: device memory must be the context's device
        (P-11). Host memory is the runtime's to check (F-5 check 4)."""
        if placement is None or placement == str(self.device):
            return None
        where = ("on %s" % self.device) if not self.device.is_host else (
            "without a device" if self.module.backend == "cuda" else "on the CPU")
        return Refused("%s is on %s, and this context runs %s; the bridge makes no implicit transfer (SPEC-03 P-11): "
                       "cint.copy(x) copies device data to host memory, and cint.copy(x, device=...) to a device"
                       % (what, placement, where))

    def _stream(self) -> int:
        """The context's CUDA stream (cint_ctx_device_stream); the caller holds the lock."""
        if self._stream_value is None:
            status, stream = self._rt.device_stream(self._handle)
            if status != CINT_OK or not stream:
                raise OSError("the context's CUDA stream could not be opened (status %d): the runtime could not "
                              "open %s through %s (SPEC-03 A-16a)" % (status, self.device, _cuda.driver_path()))
            self.__dict__["_stream_value"] = stream
        return self._stream_value

    def _exchange_stream(self, placement: str) -> int:
        """`cint.borrow(..., context=self)` of memory on `placement`: refused
        unless it is this context's device, and then the stream of the first
        exchange (P-10, P-11)."""
        problem = self._placement_problem(placement, "the array")
        if problem is not None:
            raise problem
        with self._exclusive():
            return self._stream()

    def _refusal(self, what: str) -> Refused:
        reason = self._rt.refusal_reason(self._handle)
        name, text = _abi.REFUSALS.get(reason, ("reason %s" % reason, "the runtime gave no known reason"))
        code = "E_NARROW" if name == "bool" else "E_UNSUPPORTED"
        return Refused("%s: %s (refusal reason %s)" % (what, text, name), code=code, reason=name)

    # A call.

    def _call(self, export: Export, args: tuple, kwargs: dict):
        if not export.callable:
            raise NotImplementedError("%s: %s" % (export.declaration, export.reason))
        if self._recorder is not None:
            return self._call_recorded(export, args, kwargs)
        entry = self.module._entries[export.name]
        fuel = _fuel(kwargs.pop("fuel", self.fuel)) if "fuel" not in export.param_names else self.fuel
        with self._exclusive():
            with bind_arguments(export.params, args, kwargs) as call, contextlib.ExitStack() as stack:
                placed = []
                for b in call.bounds:
                    if b.param.rank and isinstance(b.value, (View, Buffer)):
                        where = b.value._placement
                        problem = self._placement_problem(where, "parameter %s" % b.param.name)
                        if problem is not None:
                            raise problem
                        if where is not None:
                            placed.append(b)
                stream = self._stream() if placed else None
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
                        stack.enter_context(holder._entry_exchange(stream if holder._placement else None))
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
                if (export.kind == "kernel" and isinstance(failure, Fault) and failure.dispatch is not None
                        and failure.work_item is not None and placed):
                    # B-16b: the host re-executed the faulting item after reading back each device array.
                    self.__dict__["_last"] = EntryReport(export.name, tuple(
                        CopyRecord(b.param.name, "fault-readback", b.param.elem, b.value.size) for b in placed),
                        tuple(notes))
                raise failure

    def _call_recorded(self, export: Export, args: tuple, kwargs: dict):
        """A call through the recorder (`cint_rec_entry`, H-14b): an `entry`
        event, the effects, and an `outcome`. Refused before anything runs
        for an export the recorder cannot call."""
        arrays = [p.name for p in export.params if p.rank]
        if arrays:
            raise Refused("%s takes the array %s, and a recording does not carry borrowed inputs yet (SPEC-03 P-16, "
                          "H-14b): call it on a context made without record=" % (export.declaration, arrays[0]),
                          reason="recording")
        found = self.module._observer_entry(export)
        if found is None:
            raise Refused("%s has types other than integers and Bool, which a recording does not carry yet "
                          "(SPEC-03 H-14b; SPEC-09 CONF-13 X-3): call it on a context made without record="
                          % export.declaration, reason="recording")
        index, _ = found
        entry = self.module._entries[export.name]
        fuel = _fuel(kwargs.pop("fuel", self.fuel)) if "fuel" not in export.param_names else self.fuel
        api = self.module._state_api()
        with self._exclusive():
            handle = self._recording()
            with bind_arguments(export.params, args, kwargs) as call:
                raw = [int(b.value) & M64 for b in call.bounds]
            patterns = (ctypes.c_uint64 * max(1, len(raw)))(*raw)
            result = ctypes.c_uint64(0)
            self.__dict__["_last"] = EntryReport(export.name, (), ())
            status = api.rec_entry(handle, index, patterns, fuel, _state.DEPTH_FROM_CONFIG, ctypes.byref(result))
            if status == CINT_RESOURCE:
                self._recorder.ended = True
            if status != CINT_OK:
                raise self._failure(status, fuel)
            form = entry.result.form
            if entry.result.error is not None:
                es, tag = ctypes.c_void_p(), ctypes.c_uint64(0)
                if api.ctx_error(self._handle, ctypes.byref(es), ctypes.byref(tag)) == CINT_OK and es.value:
                    raise entry.result.error.from_tag(tag.value)
                form = entry.result.value_form
        if form is None:
            return None
        bits = result.value
        if parse_elem(form).signed and bits >> 63:
            bits -= 1 << 64
        return from_raw(bits, form)

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


_CHECKPOINT_REFUSAL = ("the runtime does not checkpoint a context with arenas or pools yet (rt/OPEN.md RT-OQ-42), "
                       "and a program the seed built has no revision identity")


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
