"""Recordings and replay from Python (SPEC-03 6.1, H-14 to H-18; box 13 note 6.1, 6.2, 7.2).

`cint.Recorder(path)` writes a `CINT-REC-1` of one context through the
runtime's recorder (H-14b): `Module.context(record=rec)` begins it with the
header, the code, and a checkpoint of the new context, and from then on
every call on that context goes through the recorder, `cint_rec_begin` to
`cint_rec_end` in `rt/cint_state.h`. The code is the SIR text and the
sources the bridge kept beside the library when it built it (`_build`), so
the recording replays on `cpu-sir-interp` without the program's files.
Until borrowed arrays reach recordings (P-16), an entry that takes an array
is refused while recording, as is an export whose types are not integers
or `Bool`, which the recorder cannot write (H-14b).

`cint.replay(module, recording)` re-executes a recording and compares every
boundary (H-16, H-17, H-17a). `backend="ref"`, the default (BX13-19),
runs the recording's SIR text on the interpreter's library (SPEC-09
BACK-01a); `backend="cpu"` runs the module's own library. Either way the
replay runs on a fresh context of the module's revision, which it destroys
when it returns; the result carries a checkpoint of the state it reached,
which restores into a context of either backend (H-19).
"""
from __future__ import annotations

import ctypes
from dataclasses import dataclass, field
import os
import threading

from . import _abi, _build, _state
from ._errors import (
    CINT_OK, CINT_REFUSED, CINT_RESOURCE, CINT_DIVERGED, CINT_UNREPLAYABLE, LoadError, Refused, ReplayDivergence,
    Unreplayable)

_p, _u8, _u32, _i32, _size = ctypes.c_void_p, ctypes.c_uint8, ctypes.c_uint32, ctypes.c_int32, ctypes.c_size_t
DEFAULT_DEPTH = 256                    # SPEC-06 1.4, for a recording with no entry
DEFAULT_ARENA_BYTES = 16777216


# The interpreter's library (cint_interp.h).

class SirText(ctypes.Structure):
    _fields_ = [("path", _p), ("path_len", _size), ("text", _p), ("len", _size)]


class SirError(ctypes.Structure):
    _fields_ = [("path", ctypes.c_char * 256), ("line", _u32), ("col", _u32), ("text", ctypes.c_char * 256)]


class Interpreter:
    """The library of `cpu-sir-interp`: its runtime, its state code, and the
    reader and preparation of SIR text. Its entries run through one
    process-wide image, the last one prepared (cint_interp.h), so `lock` is
    held from preparing an image until it is released."""

    def __init__(self, path: str):
        try:
            lib = ctypes.CDLL(path)
            self.runtime = _abi.Runtime(lib)
        except (OSError, _abi.LibraryProblem) as failure:
            raise LoadError("the interpreter's library %s cannot be used: %s" % (path, failure)) from None
        self.state = _state.StateApi(lib)
        if self.state.stale is not None:
            raise LoadError("the interpreter's library %s is %s, older than ABI 3.3 (SPEC-03 A-5)"
                            % (path, self.state.stale))
        if self.state.missing is not None:
            raise LoadError("the interpreter's library %s lacks %s" % (path, self.state.missing))
        P = ctypes.POINTER
        bind = _state._bind
        self.load_text = bind(lib, "cint_sir_load_text", ctypes.c_int, P(SirText), _u32, P(_p), P(SirError))
        self.free = bind(lib, "cint_sir_free", None, _p)
        self.prepare = bind(lib, "cint_sir_prepare", ctypes.c_int, _p, P(_p), P(SirError))
        self.release = bind(lib, "cint_sir_release", None, _p)
        self.revision = bind(lib, "cint_sir_revision", None, _p, P(_u8 * 32))
        self.program = bind(lib, "cint_sir_cint_program", _p, _p)
        self.observer = bind(lib, "cint_sir_observer", _p, _p)
        self.table = bind(lib, "cint_sir_state_table", _p, _p)
        self.main = bind(lib, "cint_sir_main", _p, _p)
        self.path = path
        self.lock = threading.Lock()


_interpreter = None
_interpreter_lock = threading.Lock()


def interpreter() -> Interpreter:
    """The interpreter's library, built once per toolchain (`_build`)."""
    global _interpreter
    with _interpreter_lock:
        if _interpreter is None:
            _interpreter = Interpreter(_build.interpreter_library())
        return _interpreter


def _sir_error(err: SirError) -> str:
    where = err.path.decode("utf-8", errors="replace")
    if err.line:
        where += ":%d:%d" % (err.line, err.col)
    return "%s: %s" % (where, err.text.decode("utf-8", errors="replace"))


# Recordings.

def _code_modules(code) -> tuple:
    """A `cint_code_module` array over the code a library keeps, and the
    buffers it points into, which the caller keeps alive."""
    if not code:
        return None, 0, ()
    arr = (_state.CodeModule * len(code))()
    keep = []
    for i, (path, sir, source) in enumerate(code):
        p = ctypes.create_string_buffer(path.encode("utf-8"), len(path.encode("utf-8")))
        s = ctypes.create_string_buffer(sir, len(sir))
        keep += [p, s]
        arr[i].path.bytes = ctypes.addressof(p)
        arr[i].path.len = len(p)
        arr[i].sir = ctypes.addressof(s)
        arr[i].sir_len = len(s)
        if source is not None:
            b = ctypes.create_string_buffer(source, max(1, len(source)))
            keep.append(b)
            arr[i].source = ctypes.addressof(b)
            arr[i].source_len = len(source)
    return arr, len(code), tuple(keep)


class Recorder:
    """A recording of one context, written to `path` (SPEC-03 H-14, H-14b;
    BX13-12). Pass it as `Module.context(record=rec)`; the recording begins
    there, and every call on that context, its entries, checkpoints,
    restores, and clears, is then an event. `close()` (or leaving a `with`
    block) writes the `end` event; a recording without it is incomplete and
    `cint.replay` refuses it. `mode` is "full" (the default) or "digest"
    (H-18). `audit_pure=True` asks for the conversion audit records of X-9,
    which this bridge does not write yet."""

    def __init__(self, path, *, mode: str = "full", audit_pure: bool = False):
        path = os.fspath(path)
        if not isinstance(path, str):
            raise TypeError("a recording's path is a str or os.PathLike of str")
        if mode not in _state.RECORD_MODES:
            raise ValueError("mode is \"full\" or \"digest\" (SPEC-03 H-18), not %r" % (mode,))
        if audit_pure not in (True, False):
            raise TypeError("audit_pure is True or False")
        if audit_pure:
            raise NotImplementedError("audit_pure=True: conversion audit records are not provided yet")
        self.path = path
        self.mode = mode
        self.context = None
        self.closed = False
        self.ended = False          # CINT_RESOURCE ended the recording (H-14b)
        self._handle = None
        self._file = None
        self._sink = None
        self._keep = ()
        self._live = []             # what the context's finalizer ends the recording with

    def __enter__(self) -> "Recorder":
        if self.closed:
            raise ValueError("this Recorder is closed")
        return self

    def __exit__(self, *exc):
        self.close()
        return False

    def __repr__(self) -> str:
        state = "closed" if self.closed else "recording" if self.context is not None else "not begun"
        return "<cint.Recorder %s, %s, %s>" % (self.path, self.mode, state)

    def _begin(self, ctx, api: _state.StateApi, observer: int, code):
        """`cint_rec_begin` on a new context, which no other thread holds yet."""
        if self.closed:
            raise ValueError("this Recorder is closed; make a new one for another recording")
        if self.context is not None:
            raise ValueError("this Recorder already records a context: a recording holds one context, so make "
                             "another Recorder for this one")
        arr, count, keep = _code_modules(code)
        f = open(self.path, "wb")
        sink = _state.ByteSink(f)
        handle = ctypes.c_void_p()
        try:
            status = api.rec_begin(ctx._handle, observer, arr, count, _state.RECORD_MODES[self.mode],
                                   ctypes.byref(sink.c), ctypes.byref(handle))
        except BaseException:
            f.close()
            raise
        if status != CINT_OK or not handle.value:
            f.close()
            if sink.failure is not None:
                raise OSError("%s could not be written: %s" % (self.path, sink.failure)) from sink.failure
            if status == CINT_RESOURCE:
                raise MemoryError("cint_rec_begin could not allocate the recorder (CINT_RESOURCE)")
            raise Refused("cint_rec_begin refused this context (status %d): %s" % (status, _CHECKPOINT_REFUSAL))
        self._handle, self._file, self._sink, self._keep = handle.value, f, sink, keep
        self._live[:] = [api, handle.value, f, sink]
        self.context = ctx

    def _sink_failure(self):
        if self._sink is not None and self._sink.failure is not None:
            raise OSError("%s could not be written: %s" % (self.path, self._sink.failure)) from self._sink.failure

    def close(self):
        """Write the `end` event and close the file (`cint_rec_end`); the
        context is no longer recorded. Idempotent. Raises `cint.Busy` while
        an entry runs on the context, and OSError when a write failed, after
        which the recording is incomplete."""
        if self.closed:
            return
        ctx = self.context
        status = CINT_OK
        if ctx is not None and self._live and not ctx.closed:
            status = ctx._end_recording(self._handle)
        self._handle = None
        self._live.clear()
        self.closed = True
        if self._file is not None:
            self._file.close()
        self._sink_failure()
        if status != CINT_OK:
            raise OSError("cint_rec_end returned status %d: %s is incomplete" % (status, self.path))


def end_then_destroy(destroy, handle: int, live: list):
    """The finalizer of a recorded context: a context dropped without
    `close()` ends its recording first (`cint_rec_end`), so that the file
    is complete, and is then destroyed."""
    if live:
        api, rec, f, _sink = live          # the sink stays alive until rec_end returns
        live.clear()
        try:
            api.rec_end(rec)
        finally:
            f.close()
    destroy(handle)


_CHECKPOINT_REFUSAL = ("the runtime does not checkpoint a context with arenas or pools yet (rt/OPEN.md RT-OQ-42), "
                       "and a program the seed built has no revision identity")


# Replay.

@dataclass(frozen=True)
class ReplayResult:
    """The outcome of `cint.replay` (SPEC-03 H-17, H-17a). `status` is
    "ok", "diverged", or "unreplayable" (the last two are raised, with this
    result as the exception's `result`); `what` names what differed
    ("status", "fault", "fuel", "state", ...) or is None; `event` is the
    index of the event that diverged or that replay stopped before, else of
    the event after the last one replayed; `boundaries` counts the entries
    replayed; `chain` is the head of the digest chain after them (SPEC-06
    10.5); `stopped` is True when replay stopped at `stop`. `final_state_hash`
    and `checkpoint` are the state hash and a `CINT-CKPT-1` of the context
    where replay stopped."""
    status: str
    backend: str
    what: str | None
    event: int
    boundaries: int
    chain: bytes
    stopped: bool
    final_state_hash: bytes
    checkpoint: bytes = field(repr=False)


def _recording_bytes(recording) -> bytes:
    if isinstance(recording, (bytes, bytearray, memoryview)):
        return bytes(recording)
    path = os.fspath(recording)
    if not isinstance(path, str):
        raise TypeError("a recording is a path (str or os.PathLike of str) or its bytes")
    with open(path, "rb") as f:
        return f.read()


def _first_entry(api: _state.StateApi, info: _state.RecordingInfo) -> tuple:
    """The frame-arena capacity and the depth limit of the recorded context:
    the capacity of its first entry, and the depth limit of its first program
    entry, which runs with the context's limit (H-14), else of its first entry."""
    e = _state.RecordingEvent()
    arena = depth = main_depth = None
    while api.recording_next(ctypes.byref(info), ctypes.byref(e)) == CINT_OK:
        if e.kind == _state.EVENT_ENTRY:
            arena = e.arena if arena is None else arena
            depth = e.depth if depth is None else depth
            if e.form == 1 and main_depth is None:
                main_depth = e.depth
    depth = main_depth if main_depth is not None else depth
    return (DEFAULT_ARENA_BYTES if arena is None else arena), (DEFAULT_DEPTH if depth is None else depth)


def replay(module, recording, *, backend: str = "ref", stop: int | None = None) -> ReplayResult:
    """Replay a recording (SPEC-03 H-16, H-17, H-17a) on `backend`: "ref",
    `cpu-sir-interp` (the default, BX13-19), which runs the SIR text the
    recording carries, or that kept beside the module's library when it
    carries none; or "cpu", the module's own library. `recording` is a path
    or the recording's bytes. `stop` names a boundary (1 for the first entry
    that ran): replay stops after its comparisons (SPEC-06 10.1). Raises
    `cint.ReplayDivergence` at the first difference and `cint.Unreplayable`
    before an event replay cannot pass, each with the `ReplayResult` as its
    `result`, and `cint.Refused` for a recording that is incomplete or
    malformed or of another revision than the module's (H-15, H-19)."""
    from ._module import Module
    if not isinstance(module, Module):
        raise TypeError("cint.replay takes the Module the recording was made with, from cint.load")
    if backend not in ("ref", "cpu"):
        raise ValueError("backend is \"ref\" (cpu-sir-interp) or \"cpu\" (cpu-c17): a recording replays on either "
                         "CPU backend (SPEC-03 H-16), not %r" % (backend,))
    if stop is not None and (isinstance(stop, bool) or not isinstance(stop, int) or stop < 1):
        raise ValueError("stop is None or a boundary from 1, not %r" % (stop,))
    data = _recording_bytes(recording)
    if backend == "cpu":
        api = module._state_api()
        return _replay_on(api, module._runtime, data, module, stop, "cpu",
                          lambda info: (module._table.program, module._table.root_info, module._observer_address(),
                                        module._state_table_address(), None))
    x = interpreter()
    with x.lock:
        return _replay_ref(x, data, module, stop)


def _decode(api: _state.StateApi, data: bytes, module) -> _state.RecordingInfo:
    info = _state.RecordingInfo()
    if api.recording_decode(data, len(data), ctypes.byref(info)) != CINT_OK:
        raise Refused("these %d bytes are not a complete CINT-REC-1: a recording is checked whole before replay "
                      "reads any of it, and one without its end event is incomplete (SPEC-03 H-14, H-17a)"
                      % len(data))
    revision = bytes(info.revision)
    if module.revision is not None and revision != module.revision:
        raise Refused("the recording is of revision %s and the module of revision %s: a recording replays on a "
                      "program with its revision identity (SPEC-03 H-15, H-16)" % (revision.hex(),
                                                                                    module.revision.hex()))
    return info


def _replay_ref(x: Interpreter, data: bytes, module, stop) -> ReplayResult:
    info = _decode(x.state, data, module)
    code = []
    for k in range(info.module_count):
        m = _state.CodeModule()
        if x.state.recording_module(ctypes.byref(info), None, k, ctypes.byref(m)) != CINT_OK:
            raise Refused("module %d of the recording's code cannot be read" % k)
        code.append((ctypes.string_at(m.path.bytes, m.path.len), ctypes.string_at(m.sir, m.sir_len)))
    if not code:
        kept = _build.program_code(module.path)
        if kept is None:
            raise Refused("the recording carries no code and %s has no SIR text kept beside it, so cpu-sir-interp "
                          "has nothing to run: replay it with backend=\"cpu\", or record a module that cint.load "
                          "built from source" % module.path)
        code = [(path.encode("utf-8"), sir) for path, sir, _ in kept]
    texts = (SirText * len(code))()
    keep = []
    for i, (path, sir) in enumerate(code):
        p, s = ctypes.create_string_buffer(path, len(path)), ctypes.create_string_buffer(sir, len(sir))
        keep += [p, s]
        texts[i].path, texts[i].path_len = ctypes.addressof(p), len(p)
        texts[i].text, texts[i].len = ctypes.addressof(s), len(s)
    program, image, err = ctypes.c_void_p(), ctypes.c_void_p(), SirError()
    if x.load_text(texts, len(code), ctypes.byref(program), ctypes.byref(err)) != 0:
        raise Refused("cpu-sir-interp does not read the recording's SIR text: %s (SPEC-09 SIR-10); replay it with "
                      "backend=\"cpu\"" % _sir_error(err))
    try:
        rev = (_u8 * 32)()
        x.revision(program, ctypes.byref(rev))
        if bytes(rev) != bytes(info.revision):
            raise Refused("the recording's SIR text has revision %s, and its header %s (SPEC-09 SIR-17)"
                          % (bytes(rev).hex(), bytes(info.revision).hex()))
        if x.prepare(program, ctypes.byref(image), ctypes.byref(err)) != 0:
            raise Refused("cpu-sir-interp does not run this program yet: %s; replay it with backend=\"cpu\""
                          % _sir_error(err))
        try:
            return _replay_on(x.state, x.runtime, data, module, stop, "ref",
                              lambda _: (x.program(image), 0, x.observer(image), x.table(image), x.main(image)),
                              info=info)
        finally:
            x.release(image)
    finally:
        x.free(program)
        del keep


def _replay_on(api: _state.StateApi, runtime, data: bytes, module, stop, backend: str, program_of,
               info: _state.RecordingInfo | None = None) -> ReplayResult:
    if info is None:
        info = _decode(api, data, module)
    program, module_info, observer, table, main_fn = program_of(info)
    arena, depth = _first_entry(api, info)
    status, ctx = runtime.create(program, module_info, depth, arena)
    if status != CINT_OK or not ctx:
        raise MemoryError("cint_ctx_create could not make the replay's context (status %d)" % status)
    try:
        if api.bind_state(ctx, table) != CINT_OK:
            raise Refused("the program's module state does not bind to the replay's context (rt/cint_state.h)")
        cfg = _state.ReplayConfig(size=ctypes.sizeof(_state.ReplayConfig), echo=0, stop=stop or 0, entries=observer,
                                  main_fn=main_fn, revision=None, user=None)
        rep = _state.ReplayReport()
        status = api.replay(ctx, ctypes.byref(info), ctypes.byref(cfg), ctypes.byref(rep))
        if status == CINT_RESOURCE:
            raise MemoryError("cint_replay ran out of memory (CINT_RESOURCE)")
        if status == CINT_REFUSED:
            raise Refused("cint_replay refused: the recording's initial state does not restore into a fresh context "
                          "of this program as it was recorded (SPEC-03 H-17a, H-19a)")
        if status != CINT_OK:
            raise Refused("cint_replay returned status %d" % status)
        st, final = api.hash_of(ctx)
        cst, checkpoint = api.take_checkpoint(ctx)
        if st != CINT_OK or cst != CINT_OK:
            raise Refused("the replay's final state cannot be read (status %d, %d)" % (st, cst))
    finally:
        runtime.destroy(ctx)
    what = api.replay_what(rep.what).decode("ascii") if rep.what else None
    name = {CINT_OK: "ok", CINT_DIVERGED: "diverged", CINT_UNREPLAYABLE: "unreplayable"}.get(rep.status, "ok")
    result = ReplayResult(name, backend, what, rep.event, rep.boundaries, bytes(rep.chain),
                          stop is not None and rep.status == CINT_OK and rep.boundaries == stop, final, checkpoint)
    if rep.status == CINT_DIVERGED:
        raise ReplayDivergence("replay on %s diverged at event %d (%s) after %d boundaries (SPEC-03 H-17)"
                               % (backend, rep.event, what, rep.boundaries), result)
    if rep.status == CINT_UNREPLAYABLE:
        raise Unreplayable("replay on %s stopped before event %d: its entry ended the recording with CINT_RESOURCE, "
                           "which depends on the machine (SPEC-03 A-6, H-17a)" % (backend, rep.event), result)
    return result
