"""The C ABI of a library that `cint build --lib` writes (SPEC-03 5.3, A-3, A-12, A-19; BX10-07).

A library holds one program: its generated objects and the runtime, whose
host functions it exports. It describes itself through the reflection table
that the exported `cint_library_desc` reaches (A-19; ruling on OQ-202): one
`cint_module_table` per module, and for each export a row with its name,
kind, effect class, size and parameter names, canonical type signature
(A-20), and `cx` wrapper. The structures below follow `rt/cint_rt.h`
sections 4a, 6, 6b', and 6b'', which govern. `Runtime` binds the host
functions through `ctypes.CDLL`, which releases the GIL during each call
(BX10-08), and `read_library` copies the table into Python values.

Two runtime contracts are read. `cint-rt-2` (ABI 2.0) has a 72-byte
`cint_ctx_config` and registers memory with the five-argument byte form of
`cint_buffer_register`. `cint-rt-3` (ABI 3.0) adds `max_buffers` and
`module` to an 80-byte `cint_ctx_config`, and registers memory through a
`cint_buffer_desc` (SPEC-03 5.3, A-8), keeping the byte form as
`cint_buffer_register_bytes`. `Runtime.config` and `Runtime.register` are
the only places that differ.
"""
from __future__ import annotations

import ctypes
from dataclasses import dataclass

# CINT_ABI_VERSION is (major << 16) | minor. The structures here are those of
# ABI 2.0 with the side table of A-19, which ABI 3 keeps (BX10-28).
ABI_MAJORS = (2, 3)
VIEW_READ = 0
VIEW_WRITE = 1
BUFFER_GENERATION_FIRST = 1
EXPORT_KINDS = {1: "function", 2: "kernel"}
EFFECTS = {1: "pure", 2: "observe", 3: "unrestricted"}
REFUSALS = {
    0: ("none", "the call was refused before entry"),
    1: ("buffer", "a view names a buffer this context has not registered"),
    2: ("generation", "a view names a registration that has ended"),
    3: ("type", "a view's element type is not one this runtime binds"),
    4: ("rank", "a view's rank is not 1 to 4"),
    5: ("stride", "a view's stride is not one this runtime binds"),
    6: ("size", "a view's span overflows"),
    7: ("extent", "a view reaches outside its registration"),
    8: ("align", "a view's first element is not aligned for its type (SPEC-03 A-13a)"),
    9: ("permission", "a view asks for more access than its registration grants"),
    10: ("call", "the wrapper was called with arguments it cannot read"),
    11: ("bool", "a Bool argument, element, or field holds a byte other than 0 or 1 (SPEC-03 A-13)"),
    12: ("result", "the result pointer is NULL"),
    13: ("leased", "a buffer with a live lease is bound for writing (SPEC-03 A-9)"),
    14: ("unsupported", "device memory, a device, or a synchronization object this runtime does not take"),
    15: ("lease", "a lease of memory the runtime does not own, or an unknown lease"),
}
MEM_HOST = 1
PUBLISH_NONE = 0
PUBLISH_COPY = 1

# Bounds on what a library's table may claim, so that a damaged table fails
# with a message instead of reading far through memory.
_MAX_MODULES = 1 << 16
_MAX_EXPORTS = 1 << 20
_MAX_NAME = 1 << 16
_MAX_SIGNATURE = 1 << 24


class CintType(ctypes.Structure):
    _fields_ = [("code", ctypes.c_uint16), ("storage", ctypes.c_uint8), ("reserved0", ctypes.c_uint8),
                ("frac_bits", ctypes.c_uint16), ("reserved1", ctypes.c_uint16), ("record_id", ctypes.c_uint32),
                ("reserved2", ctypes.c_uint32)]


class CintView(ctypes.Structure):
    _fields_ = [("buffer", ctypes.c_uint64), ("generation", ctypes.c_uint64), ("type", CintType),
                ("rank", ctypes.c_uint8), ("perm", ctypes.c_uint8), ("reserved", ctypes.c_uint8 * 6),
                ("origin", ctypes.c_int64), ("shape", ctypes.c_int64 * 4), ("stride", ctypes.c_int64 * 4),
                ("lower", ctypes.c_int64 * 4)]


class CintAllocator(ctypes.Structure):
    _fields_ = [("alloc", ctypes.c_void_p), ("release", ctypes.c_void_p), ("user", ctypes.c_void_p)]


class CintCtxConfig(ctypes.Structure):
    """`cint_ctx_config` of `cint-rt-2`."""
    _fields_ = [("size", ctypes.c_uint32), ("reserved", ctypes.c_uint32), ("program", ctypes.c_void_p),
                ("allocator", CintAllocator), ("depth_limit", ctypes.c_int64),
                ("frame_arena_bytes", ctypes.c_int64), ("output", ctypes.c_void_p),
                ("output_user", ctypes.c_void_p)]


class CintCtxConfig3(ctypes.Structure):
    """`cint_ctx_config` of `cint-rt-3`: `max_buffers` (0 for the default)
    and the root module's descriptor, which sizes record elements."""
    _fields_ = [("size", ctypes.c_uint32), ("max_buffers", ctypes.c_uint32), ("program", ctypes.c_void_p),
                ("allocator", CintAllocator), ("depth_limit", ctypes.c_int64),
                ("frame_arena_bytes", ctypes.c_int64), ("output", ctypes.c_void_p),
                ("output_user", ctypes.c_void_p), ("module", ctypes.c_void_p)]


class CintMem(ctypes.Structure):
    """`cint_mem`: the kind, then a union whose host member is a pointer."""
    _fields_ = [("kind", ctypes.c_uint32), ("reserved", ctypes.c_uint32), ("u", ctypes.c_uint64 * 4)]


class CintSync(ctypes.Structure):
    _fields_ = [("kind", ctypes.c_uint32), ("reserved", ctypes.c_uint32), ("u", ctypes.c_uint64 * 3)]


class CintBufferDesc(ctypes.Structure):
    _fields_ = [("size", ctypes.c_uint32), ("writable", ctypes.c_uint32), ("type", CintType), ("mem", CintMem),
                ("extent", ctypes.c_int64), ("device", ctypes.c_uint32), ("publish", ctypes.c_uint32),
                ("sync", CintSync)]


class CintName(ctypes.Structure):
    _fields_ = [("bytes", ctypes.c_void_p), ("len", ctypes.c_uint32), ("reserved", ctypes.c_uint32)]


class CintExport(ctypes.Structure):
    _fields_ = [("name", CintName), ("kind", ctypes.c_uint32), ("effect", ctypes.c_uint32),
                ("size_count", ctypes.c_uint32), ("param_count", ctypes.c_uint32),
                ("size_names", ctypes.c_void_p), ("param_names", ctypes.c_void_p),
                ("type_signature", ctypes.c_void_p), ("type_signature_len", ctypes.c_uint64),
                ("call", ctypes.c_void_p)]


class CintModuleTable(ctypes.Structure):
    _fields_ = [("info", ctypes.c_void_p), ("module", ctypes.c_uint32), ("export_count", ctypes.c_uint32),
                ("exports", ctypes.c_void_p), ("record_names", ctypes.c_void_p)]


class CintLibrary(ctypes.Structure):
    _fields_ = [("abi", ctypes.c_uint32), ("module_count", ctypes.c_uint32), ("modules", ctypes.c_void_p)]


class CintModuleInfo(ctypes.Structure):
    _fields_ = [("abi", ctypes.c_uint32), ("record_count", ctypes.c_uint32), ("program", ctypes.c_void_p),
                ("records", ctypes.c_void_p)]


class CintProgram(ctypes.Structure):
    _fields_ = [("module_count", ctypes.c_uint32), ("reserved", ctypes.c_uint32), ("modules", ctypes.c_void_p),
                ("revision", ctypes.c_void_p)]


SIZES = {CintType: 16, CintView: 144, CintAllocator: 24, CintCtxConfig: 72, CintCtxConfig3: 80, CintMem: 40,
         CintSync: 32, CintBufferDesc: 112, CintName: 16, CintExport: 72, CintModuleTable: 32, CintLibrary: 16,
         CintModuleInfo: 24, CintProgram: 24}


def layout_problem() -> str | None:
    """Why this host cannot use the ABI's structures, or None: the layouts
    of `rt/cint_rt.h` assume 8-byte pointers (its _Static_assert lines)."""
    if ctypes.sizeof(ctypes.c_void_p) != 8:
        return "cint libraries need a 64-bit host: the ABI's structures assume 8-byte pointers"
    for cls, size in SIZES.items():
        if ctypes.sizeof(cls) != size:
            return "%s is %d bytes here, not %d (rt/cint_rt.h)" % (cls.__name__, ctypes.sizeof(cls), size)
    return None


class LibraryProblem(Exception):
    """A library whose table cannot be read; `cint.load` reports it as a LoadError."""


@dataclass(frozen=True)
class ExportRow:
    """One row of a module's table (A-19), copied out of the library."""
    name: str
    kind: str
    effect: str
    size_names: tuple
    param_names: tuple
    signature: bytes
    call: int
    module: int


@dataclass(frozen=True)
class LibraryTable:
    """What `cint_library_desc` describes: the ABI version, the root
    module's `cm` descriptor and its program, the program's revision
    identity (32 bytes, or None when the compiler reports none), the qualified
    names of the root module's records, and every export row."""
    abi: int
    root_info: int
    program: int
    revision: bytes | None
    records: tuple
    exports: tuple


def _text(name: CintName, what: str) -> str:
    if not name.bytes or not 1 <= name.len <= _MAX_NAME:
        raise LibraryProblem("%s has no name or a length of %d" % (what, name.len))
    try:
        return ctypes.string_at(name.bytes, name.len).decode("utf-8")
    except UnicodeDecodeError:
        raise LibraryProblem("%s is not UTF-8" % what) from None


def _names(address: int, count: int, what: str) -> tuple:
    if count == 0:
        return ()
    if not address:
        raise LibraryProblem("%s: %d names and a NULL table" % (what, count))
    rows = (CintName * count).from_address(address)
    return tuple(_text(rows[i], "%s %d" % (what, i)) for i in range(count))


def read_library(lib: ctypes.CDLL) -> LibraryTable:
    """Copy the reflection table of a loaded library (A-19). Raises
    `LibraryProblem` for a library without one or with a damaged one."""
    try:
        desc = CintLibrary.in_dll(lib, "cint_library_desc")
    except ValueError:
        raise LibraryProblem("it exports no cint_library_desc, so it has no reflection table (SPEC-03 A-19): "
                             "build it with cint build --lib from cintc, not the seed") from None
    if desc.abi >> 16 not in ABI_MAJORS:
        raise LibraryProblem("its ABI version is %d.%d, and this bridge reads major versions %s"
                             % (desc.abi >> 16, desc.abi & 0xFFFF, " and ".join(map(str, ABI_MAJORS))))
    if not 1 <= desc.module_count <= _MAX_MODULES or not desc.modules:
        raise LibraryProblem("its library descriptor lists %d modules" % desc.module_count)
    pointers = (ctypes.c_void_p * desc.module_count).from_address(desc.modules)
    tables = []
    for i in range(desc.module_count):
        if not pointers[i]:
            raise LibraryProblem("module table %d is NULL" % i)
        t = CintModuleTable.from_address(pointers[i])
        if t.module != i or not t.info or t.export_count > _MAX_EXPORTS:
            raise LibraryProblem("module table %d is damaged (index %d, %d exports)" % (i, t.module, t.export_count))
        tables.append(t)
    # cintc writes the root module last (SPEC-03 A-19; compiler/OPEN.md CINTC-OQ-39).
    root = tables[-1]
    info = CintModuleInfo.from_address(root.info)
    if info.abi != desc.abi or not info.program:
        raise LibraryProblem("the root module's descriptor has ABI %#x and program %#x"
                             % (info.abi, info.program or 0))
    program = CintProgram.from_address(info.program)
    if program.module_count != desc.module_count:
        raise LibraryProblem("the program has %d modules and the library descriptor %d"
                             % (program.module_count, desc.module_count))
    revision = ctypes.string_at(program.revision, 32) if program.revision else None
    records = _names(root.record_names, info.record_count if root.record_names else 0, "record")
    exports = []
    for order in [len(tables) - 1] + list(range(len(tables) - 1)):
        t = tables[order]
        if t.export_count and not t.exports:
            raise LibraryProblem("module table %d lists %d exports and a NULL row table" % (order, t.export_count))
        rows = (CintExport * t.export_count).from_address(t.exports) if t.export_count else ()
        for row in rows:
            name = _text(row.name, "an export of module %d" % order)
            if row.type_signature_len > _MAX_SIGNATURE or (row.type_signature_len and not row.type_signature):
                raise LibraryProblem("export %s has a type signature of %d bytes at %#x"
                                     % (name, row.type_signature_len, row.type_signature or 0))
            exports.append(ExportRow(
                name=name, kind=EXPORT_KINDS.get(row.kind, "kind %d" % row.kind),
                effect=EFFECTS.get(row.effect, "effect %d" % row.effect),
                size_names=_names(row.size_names, row.size_count, "size name of " + name),
                param_names=_names(row.param_names, row.param_count, "parameter name of " + name),
                signature=ctypes.string_at(row.type_signature, row.type_signature_len),
                call=row.call or 0, module=order))
    return LibraryTable(desc.abi, root.info, info.program, revision, records, tuple(exports))


def _bind(lib, name: str, restype, *argtypes):
    try:
        fn = getattr(lib, name)
    except AttributeError:
        raise LibraryProblem("it does not export the runtime's host function %s (SPEC-03 A-3, BX10-07)"
                             % name) from None
    fn.restype = restype
    fn.argtypes = list(argtypes)
    return fn


class Runtime:
    """The host functions of the runtime a library carries (SPEC-03 5.3).
    Each library carries its own copy, so a context is used through the
    library that created it only (BX10-07). `abi` is the library's
    `cint_abi_version()`, which selects the contract's layouts."""

    def __init__(self, lib: ctypes.CDLL):
        p, u8, u32, i32, i64, u64 = (ctypes.c_void_p, ctypes.c_uint8, ctypes.c_uint32, ctypes.c_int32,
                                     ctypes.c_int64, ctypes.c_uint64)
        self.lib = lib
        self.abi = _bind(lib, "cint_abi_version", u32)()
        self.major = self.abi >> 16
        config = CintCtxConfig3 if self.major >= 3 else CintCtxConfig
        self.ctx_create = _bind(lib, "cint_ctx_create", i32, ctypes.POINTER(config), ctypes.POINTER(p))
        self.ctx_destroy = _bind(lib, "cint_ctx_destroy", None, p)
        self.fault_get = _bind(lib, "cint_fault_get", i32, p, p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t))
        self.clear_fault = _bind(lib, "cint_ctx_clear_fault", i32, p)
        self.refusal = _bind(lib, "cint_ctx_refusal", i32, p, ctypes.POINTER(u32))
        self.fuel_consumed = _bind(lib, "cint_fuel_consumed", i32, p, ctypes.POINTER(i64))
        self.buffer_release = _bind(lib, "cint_buffer_release", i32, p, u64)
        if self.major >= 3:
            self._register_desc = _bind(lib, "cint_buffer_register", i32, p, ctypes.POINTER(CintBufferDesc),
                                        ctypes.POINTER(u64), ctypes.POINTER(u64))
            self._register_bytes = _bind(lib, "cint_buffer_register_bytes", i32, p, p, i64, u8, ctypes.POINTER(u64))
        else:
            self._register_desc = None
            self._register_bytes = _bind(lib, "cint_buffer_register", i32, p, p, i64, u8, ctypes.POINTER(u64))

    def create(self, program: int, module: int, depth: int, frame_arena_bytes: int) -> tuple:
        """cint_ctx_create with `config`: (status, context handle)."""
        c = self.config(program, module, depth, frame_arena_bytes)
        handle = ctypes.c_void_p()
        status = self.ctx_create(ctypes.byref(c), ctypes.byref(handle))
        return status, handle.value or 0

    def destroy(self, ctx: int):
        self.ctx_destroy(ctx)

    def fault(self, ctx: int) -> bytes | None:
        """The canonical bytes of the context's fault record (A-17), or None
        when it holds none or they cannot be read."""
        n = ctypes.c_size_t(0)
        if self.fault_get(ctx, None, 0, ctypes.byref(n)) != 0 or n.value == 0:
            return None
        buf = (ctypes.c_uint8 * n.value)()
        if self.fault_get(ctx, buf, n.value, ctypes.byref(n)) != 0 or n.value > len(buf):
            return None
        return bytes(buf[:n.value])

    def refusal_reason(self, ctx: int) -> int | None:
        """The CINT_REFUSAL_* reason of the last refused entry, or None."""
        reason = ctypes.c_uint32(0)
        if self.refusal(ctx, ctypes.byref(reason)) != 0:
            return None
        return reason.value

    def consumed(self, ctx: int) -> tuple:
        """cint_fuel_consumed: (status, fuel the last entry consumed)."""
        n = ctypes.c_int64(0)
        status = self.fuel_consumed(ctx, ctypes.byref(n))
        return status, n.value

    def clear(self, ctx: int) -> int:
        return self.clear_fault(ctx)

    def config(self, program: int, module: int, depth: int, frame_arena_bytes: int):
        """A `cint_ctx_config` for this contract: the program, the C library's
        allocator, no output (prints are discarded), and the root module's
        descriptor where the contract has the field."""
        if self.major >= 3:
            c = CintCtxConfig3(size=ctypes.sizeof(CintCtxConfig3), program=program, module=module)
        else:
            c = CintCtxConfig(size=ctypes.sizeof(CintCtxConfig), program=program)
        c.depth_limit = depth
        c.frame_arena_bytes = frame_arena_bytes
        return c

    def register(self, ctx: int, address: int, elem, extent: int, writable: bool, publish: bool) -> tuple:
        """Register `extent` elements of `elem` (a `cint._elem.Elem`) at
        `address`, the lowest reachable byte (SPEC-03 A-8, A-8a): (status, id,
        generation). Under `cint-rt-3` a `Bool` or `I8` to `U64` element goes
        through a `cint_buffer_desc`, a typed registration with its ceiling and
        publish flag; other elements, and every element under `cint-rt-2`, go
        through the byte form, whose generation is the first."""
        ident, generation = ctypes.c_uint64(0), ctypes.c_uint64(BUFFER_GENERATION_FIRST)
        if self._register_desc is not None and (elem.kind == "bool" or (elem.kind == "int" and not elem.wide)):
            d = CintBufferDesc(size=ctypes.sizeof(CintBufferDesc), writable=1 if writable else 0, extent=extent,
                               publish=PUBLISH_COPY if publish and writable else PUBLISH_NONE)
            d.type.code = elem.tag
            d.mem.kind = MEM_HOST
            d.mem.u[0] = address
            status = self._register_desc(ctx, ctypes.byref(d), ctypes.byref(ident), ctypes.byref(generation))
        else:
            status = self._register_bytes(ctx, address or None, extent * elem.size,
                                          VIEW_WRITE if writable else VIEW_READ, ctypes.byref(ident))
        return status, ident.value, generation.value

    def release(self, ctx: int, ident: int) -> int:
        """End a registration (cint_buffer_release): its status."""
        return self.buffer_release(ctx, ident)
