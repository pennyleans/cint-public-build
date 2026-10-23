"""Checkpoints, recordings, and replay through the runtime (SPEC-03 5.3, H-14 to H-19a; box 13 note 7.2).

The canonical state code is `rt/cint_state.c`, with recordings and replay
in `rt/cint_rec.c`, which every library the bridge builds carries beside
the runtime (`_build`), as does the interpreter's library. This module
mirrors the structures of `rt/cint_state.h`, which governs, binds its
functions for one loaded library (`StateApi`), and gives the read-only
typed view of a checkpoint (`read_checkpoint`, BX13-11). A checkpoint and a
recording are untrusted bytes: only the runtime's decoders read them, never
Python (H-19a). The mirrors take the layouts of ABI 3.3, whose tagged value
holds a `Z` of fault record version 3 (OQ-263) and whose recording event
carries a host call's response (RT-OQ-70), so `StateApi` reads them from
ABI 3.3 libraries only (`stale`).
"""
from __future__ import annotations

import ctypes
from dataclasses import dataclass
import hashlib
import threading

from . import _abi, _record
from ._elem import SIGNED_TAGS, TAG_BOOL, TAG_FIXED, TAG_T1, TAG_T27, UNSIGNED_TAGS
from ._errors import CINT_OK, CINT_RESOURCE

ABI_STATE = 0x00030003    # ABI 3.3: cint_recording_event carries a host call's response (RT-OQ-70)

RECORD_MODES = {"full": 0, "digest": 1}              # CINT_RECORD_FULL, CINT_RECORD_DIGEST (H-18)
DEPTH_FROM_CONFIG = 0                                # CINT_DEPTH_FROM_CONFIG (rt/cint_rt.h)
EVENT_ENTRY, EVENT_RESTORE = 1, 6                   # CINT_EVENT_ENTRY, CINT_EVENT_RESTORE
OWNERS = {1: "borrowed", 2: "bridge"}                # CINT_OWNER_*
_TAG_NAMES = {TAG_BOOL: "Bool", TAG_T1: "T1", TAG_T27: "T27", TAG_FIXED: "fixed point",
              **{tag: "I%d" % bits for bits, tag in SIGNED_TAGS.items()},
              **{tag: "U%d" % bits for bits, tag in UNSIGNED_TAGS.items()}}

_p, _u8, _u16, _u32, _u64, _i32, _i64, _size = (ctypes.c_void_p, ctypes.c_uint8, ctypes.c_uint16, ctypes.c_uint32,
                                                 ctypes.c_uint64, ctypes.c_int32, ctypes.c_int64, ctypes.c_size_t)


# The structures of rt/cint_state.h and rt/cint_rt.h that this module reads.

class TValue(ctypes.Structure):
    _fields_ = [("len", _u16), ("bytes", _u8 * 269)]   # CINT_TVALUE_MAX, ABI 3.2 (SPEC-03 A-5)


class Site(ctypes.Structure):
    _fields_ = [("module", _u32), ("index", _u32)]


SinkWrite = ctypes.CFUNCTYPE(_i32, _p, ctypes.POINTER(_u8), _size)


class Sink(ctypes.Structure):
    _fields_ = [("write", SinkWrite), ("user", _p)]


class CodeModule(ctypes.Structure):
    _fields_ = [("path", _abi.CintName), ("sir", _p), ("sir_len", _size), ("source", _p), ("source_len", _size)]


class CheckpointInfo(ctypes.Structure):
    _fields_ = [("contract", _abi.CintName), ("payload_digest", _p), ("build_record_digest", _p),
                ("build_identity", _p), ("revision", _u8 * 32), ("global_count", _u64), ("buffer_count", _u64),
                ("next_buffer", _u64), ("next_arena", _u64), ("next_pool", _u64), ("entries", _u64),
                ("effects", _i64), ("fault", _p), ("fault_len", _size), ("state", _p), ("state_len", _size),
                ("globals_at", _size), ("globals_end", _size), ("buffers_at", _size), ("buffers_end", _size)]


class CheckpointGlobal(ctypes.Structure):
    _fields_ = [("name", _abi.CintName), ("value", TValue), ("elements", _p), ("element_count", _u64)]


class CheckpointBuffer(ctypes.Structure):
    _fields_ = [("id", _u64), ("generation", _u64), ("type", _abi.CintType), ("extent", _i64), ("owner", _u32),
                ("ceiling", _u8), ("publish", _u8), ("reserved", _u16)]


class RecordingInfo(ctypes.Structure):
    _fields_ = [("contract", _abi.CintName), ("revision", _u8 * 32), ("mode", _u32), ("is_case", _u32),
                ("has_map", _u32), ("map", _u8 * 32), ("density", _u32),
                ("interval", _u64), ("module_count", _u32), ("end_reason", _u32), ("initial", _p),
                ("initial_len", _size), ("event_count", _u64), ("boundaries", _u64), ("chain", _u8 * 32),
                ("bytes", _p), ("len", _size), ("code_at", _size), ("events_at", _size)]


class RecordingEvent(ctypes.Structure):
    _fields_ = [("kind", _u32), ("form", _u32), ("index", _u64), ("next", _size), ("sequence", _u64),
                ("module", _abi.CintName), ("name", _abi.CintName), ("arg_count", _u32), ("module_count", _u32),
                ("fuel", _i64), ("depth", _i64), ("arena", _i64), ("ordinal", _i64), ("site", Site),
                ("status", _i32), ("data", _p), ("data_len", _size), ("consumed", _i64), ("allowance", _i64),
                ("has_result", _u32), ("has_error", _u32), ("has_hash", _u32), ("reason", _u32),
                ("result", TValue), ("error_set", _abi.CintName), ("error_tag", _u64), ("hash", _u8 * 32),
                ("next_buffer", _u64), ("next_arena", _u64), ("next_pool", _u64), ("revision", _u8 * 32),
                ("args_at", _size), ("response", _p), ("response_len", _size)]


class ReplayConfig(ctypes.Structure):
    _fields_ = [("size", _u32), ("echo", _u32), ("stop", _u64), ("entries", _p), ("main_fn", _p),
                ("revision", _p), ("user", _p)]


class ReplayReport(ctypes.Structure):
    _fields_ = [("status", _i32), ("what", _u32), ("event", _u64), ("boundaries", _u64), ("chain", _u8 * 32)]


class ObserverEntry(ctypes.Structure):
    _fields_ = [("module", _u32), ("name_len", _u32), ("name", _p), ("types", _p), ("call", _p)]


class Observer(ctypes.Structure):
    _fields_ = [("entry_count", _u32), ("reserved", _u32), ("program", _p), ("entries", _p)]


SIZES = {TValue: 272, Site: 8, Sink: 16, CodeModule: 48, CheckpointInfo: 192, CheckpointGlobal: 304,
         CheckpointBuffer: 48, RecordingInfo: 208, RecordingEvent: 576, ReplayConfig: 48, ReplayReport: 56,
         ObserverEntry: 32, Observer: 24}


def layout_problem() -> str | None:
    """Why these mirrors do not match this host's ABI, or None (rt/cint_state.h)."""
    for cls, size in SIZES.items():
        if ctypes.sizeof(cls) != size:
            return "%s is %d bytes here, not %d (rt/cint_state.h)" % (cls.__name__, ctypes.sizeof(cls), size)
    return None


def tag_name(tag: int) -> str:
    return _TAG_NAMES.get(tag, "tag %#04x" % tag)


def _text(name: _abi.CintName) -> str:
    return ctypes.string_at(name.bytes, name.len).decode("utf-8") if name.bytes and name.len else ""


# A sink: the bytes the runtime writes, to memory or to a file.

class ByteSink:
    """A `cint_sink` that keeps what the runtime writes, in memory or in an
    open binary file. A write that raises is reported to the runtime as
    `CINT_RESOURCE`, and its exception is kept in `failure`."""

    def __init__(self, file=None):
        self.file = file
        self.data = bytearray()
        self.failure = None
        self.written = 0
        self._write = SinkWrite(self._call)          # kept alive as long as the sink
        self.c = Sink(self._write, None)

    def _call(self, user, data, length) -> int:
        try:
            chunk = ctypes.string_at(data, length) if length else b""
            if self.file is None:
                self.data += chunk
            else:
                self.file.write(chunk)
            self.written += length
            return CINT_OK
        except BaseException as failure:      # noqa: B902 - nothing may unwind through C
            if self.failure is None:
                self.failure = failure
            return CINT_RESOURCE


# The functions of rt/cint_state.h in one library.

def _bind(lib, name: str, restype, *argtypes):
    fn = getattr(lib, name)
    fn.restype = restype
    fn.argtypes = list(argtypes)
    return fn


class StateApi:
    """The checkpoint, recording, and replay functions a library exports
    (SPEC-03 5.3, H-14b, H-17a). `missing` names the first function the
    library lacks, when it was built without `rt/cint_state.c` and
    `rt/cint_rec.c`, and `stale` the ABI version of a library older than
    the ABI 3.3 layouts these mirrors hold (SPEC-03 A-5); the other
    attributes are then absent."""

    def __init__(self, lib):
        self.lib = lib
        self.missing = self.stale = None
        P = ctypes.POINTER
        try:
            abi = _bind(lib, "cint_abi_version", _u32)()
        except AttributeError:
            self.missing = "cint_abi_version"
            return
        if abi < ABI_STATE:
            self.stale = "ABI %d.%d" % (abi >> 16, abi & 0xFFFF)
            return
        try:
            self.bind_state = _bind(lib, "cint_ctx_bind_state", _i32, _p, _p)
            self.checkpoint = _bind(lib, "cint_ctx_checkpoint", _i32, _p, P(Sink))
            self.restore = _bind(lib, "cint_ctx_restore", _i32, _p, _p, _size)
            self.state_hash = _bind(lib, "cint_ctx_state_hash", _i32, _p, P(_u8 * 32))
            self.decode = _bind(lib, "cint_checkpoint_decode", _i32, _p, _size, P(CheckpointInfo))
            self.global_next = _bind(lib, "cint_checkpoint_global_next", _i32, P(CheckpointInfo), P(_size),
                                     P(CheckpointGlobal))
            self.buffer_next = _bind(lib, "cint_checkpoint_buffer_next", _i32, P(CheckpointInfo), P(_size),
                                     P(CheckpointBuffer))
            self.render = _bind(lib, "cint_checkpoint_render", _size, P(CheckpointInfo), _p, _size)
            self.recording_decode = _bind(lib, "cint_recording_decode", _i32, _p, _size, P(RecordingInfo))
            self.recording_next = _bind(lib, "cint_recording_next", _i32, P(RecordingInfo), P(RecordingEvent))
            self.recording_module = _bind(lib, "cint_recording_module", _i32, P(RecordingInfo), P(RecordingEvent),
                                          _u32, P(CodeModule))
            self.replay = _bind(lib, "cint_replay", _i32, _p, P(RecordingInfo), P(ReplayConfig), P(ReplayReport))
            self.replay_what = _bind(lib, "cint_replay_what", ctypes.c_char_p, _u32)
        except AttributeError as gone:
            self.missing = str(gone).rsplit(" ", 1)[-1].strip("'")
            return
        try:
            self.rec_begin = _bind(lib, "cint_rec_begin", _i32, _p, _p, P(CodeModule), _u32, _u32, P(Sink), P(_p))
            self.rec_entry = _bind(lib, "cint_rec_entry", _i32, _p, _u32, P(_u64), _i64, _i64, P(_u64))
            self.rec_checkpoint = _bind(lib, "cint_rec_checkpoint", _i32, _p, P(Sink))
            self.rec_restore = _bind(lib, "cint_rec_restore", _i32, _p, _p, _size)
            self.rec_clear_fault = _bind(lib, "cint_rec_clear_fault", _i32, _p)
            self.rec_end = _bind(lib, "cint_rec_end", _i32, _p)
            self.ctx_error = _bind(lib, "cint_ctx_error", _i32, _p, P(_p), P(_u64))
        except AttributeError:
            pass       # the interpreter's library: replay and decoding only

    def take_checkpoint(self, ctx: int, recorder: int | None = None) -> tuple:
        """(status, bytes) of `cint_ctx_checkpoint`, or of `cint_rec_checkpoint`
        when a recorder records the context."""
        sink = ByteSink()
        if recorder:
            status = self.rec_checkpoint(recorder, ctypes.byref(sink.c))
        else:
            status = self.checkpoint(ctx, ctypes.byref(sink.c))
        if sink.failure is not None:
            raise sink.failure
        return status, bytes(sink.data)

    def hash_of(self, ctx: int) -> tuple:
        out = (_u8 * 32)()
        status = self.state_hash(ctx, ctypes.byref(out))
        return status, bytes(out)

    def decode_checkpoint(self, data: bytes) -> CheckpointInfo | None:
        """The decoded view of checkpoint bytes, or None when the decoder
        refuses them. The view points into `data`, which the caller keeps."""
        info = CheckpointInfo()
        return info if self.decode(data, len(data), ctypes.byref(info)) == CINT_OK else None


# Libraries whose decoder `read_checkpoint` may use: the last one loaded with the state code.
_decoders = []
_decoders_lock = threading.Lock()


def remember(api: StateApi):
    if api.missing is None and api.stale is None:
        with _decoders_lock:
            _decoders.append(api)
            del _decoders[:-4]


def decoder() -> StateApi:
    """A loaded library's decoder, or else the interpreter's library's."""
    with _decoders_lock:
        if _decoders:
            return _decoders[-1]
    from . import _replay
    return _replay.interpreter().state


# The typed view of a checkpoint (BX13-11; SPEC-03 5.3, 6.1).

@dataclass(frozen=True)
class Global:
    """One module-level variable of a checkpoint: its fully qualified name
    (SPEC-04 LS-225), its type, and its exact value."""
    name: str
    type: str
    value: object


@dataclass(frozen=True)
class Registration:
    """One entry of a checkpoint's registry block (SPEC-03 M-3): its
    identifier and generation, its element type (`bytes` for a registration
    without one), its extent in elements (bytes for `bytes`), whose memory it
    is, and its ceiling and publish mode."""
    id: int
    generation: int
    elem: str
    extent: int
    owner: str
    writable: bool
    publish: str


class CheckpointView:
    """A read-only typed view of a `CINT-CKPT-1` (BX13-11), read through the
    runtime library's decoder, which validates as H-19a requires before
    anything is shown. `globals` lists each global with its type and value;
    `arenas` and `pools` are empty until box 12's blocks have content;
    `registry` lists the registrations; `next_buffer`, `next_arena`,
    `next_pool`, and `entries` are the counters; `effects` the effect-log
    position; `fault` the held `cint.Fault` or None; `revision` and
    `state_hash` 32 bytes each. `str(view)` is the text `cint-interp show`
    prints."""

    def __init__(self, data: bytes, api: StateApi):
        info = api.decode_checkpoint(data)
        if info is None:
            raise ValueError("these %d bytes are not a well-formed CINT-CKPT-1 (SPEC-03 H-19, H-19a)" % len(data))
        self.data = data
        self.contract = _text(info.contract)
        self.revision = bytes(info.revision)
        self.payload_digest = ctypes.string_at(info.payload_digest, 32) if info.payload_digest else None
        self.build_record_digest = ctypes.string_at(info.build_record_digest, 32) if info.build_record_digest else None
        self.build_identity = ctypes.string_at(info.build_identity, 32) if info.build_identity else None
        self.next_buffer, self.next_arena, self.next_pool = info.next_buffer, info.next_arena, info.next_pool
        self.entries = info.entries
        self.effects = info.effects
        state = ctypes.string_at(info.state, info.state_len)
        self.state_hash = hashlib.sha256(state).digest()
        fault = ctypes.string_at(info.fault, info.fault_len) if info.fault else None
        if fault is not None:
            from ._errors import Fault
            self.fault = Fault.from_record(fault)
        else:
            self.fault = None
        rows, cursor, g = [], _size(0), CheckpointGlobal()
        while api.global_next(ctypes.byref(info), ctypes.byref(cursor), ctypes.byref(g)) == CINT_OK:
            raw = bytes(g.value.bytes[:g.value.len])
            if raw[:1] == bytes([_record.TAG_RECORD]):   # a struct (RT-OQ-51): its type, then its payload
                raw += ctypes.string_at(g.elements, g.element_count)
            elif g.elements is not None:   # an array (rt/OPEN.md RT-OQ-49): its descriptor, then its elements
                width = 1 if raw[1] == TAG_BOOL else 1 << ((raw[1] & 15) - 1)
                raw += ctypes.string_at(g.elements, g.element_count * width)
            type_name, value = _record._value(_record._Reader(raw), "global", _record.Z_MAX)
            rows.append(Global(_text(g.name), type_name, value))
        self.globals = tuple(rows)
        rows, cursor, b = [], _size(0), CheckpointBuffer()
        while api.buffer_next(ctypes.byref(info), ctypes.byref(cursor), ctypes.byref(b)) == CINT_OK:
            rows.append(Registration(b.id, b.generation, tag_name(b.type.code) if b.type.code else "bytes", b.extent,
                                     OWNERS.get(b.owner, "owner %d" % b.owner), b.ceiling == _abi.VIEW_WRITE,
                                     "copy" if b.publish == _abi.PUBLISH_COPY else "none"))
        self.registry = tuple(rows)
        self.arenas = ()
        self.pools = ()
        n = api.render(ctypes.byref(info), None, 0)
        out = ctypes.create_string_buffer(n)
        api.render(ctypes.byref(info), out, n)
        self._text = out.raw[:n].decode("utf-8", errors="replace")

    def global_value(self, name: str):
        """The value of the global `name`: its fully qualified name
        (`module.name`), or its bare name when one module alone has it."""
        found = [g for g in self.globals if g.name == name]
        if not found:
            found = [g for g in self.globals if g.name.rpartition(".")[2] == name]
        if len(found) != 1:
            raise KeyError("%s names %s" % (name, "no global" if not found else "globals of several modules: %s"
                                            % ", ".join(g.name for g in found)))
        return found[0].value

    def __str__(self) -> str:
        return self._text

    def __repr__(self) -> str:
        return "<cint.CheckpointView %s, %d globals, %d registrations%s>" % (
            self.revision.hex()[:12], len(self.globals), len(self.registry), ", faulted" if self.fault else "")


def read_checkpoint(data) -> CheckpointView:
    """A read-only typed view of the `CINT-CKPT-1` in `data` (SPEC-03 5.3,
    6.1; BX13-11). Raises ValueError for bytes the decoder refuses."""
    if not isinstance(data, (bytes, bytearray, memoryview)):
        raise TypeError("a checkpoint is bytes, as Context.checkpoint() returns, not %s" % type(data).__name__)
    return CheckpointView(bytes(data), decoder())
