# python/OPEN.md: open items

Known limitations of the Python package (`python/cint/`) at this release, by entry ID. Source comments cite these IDs. SPEC-03 section 6 governs the package.

## PY-OQ-01. Loading and calling compiled code

Status: Proposed.

- `cint.load` builds the library with `cint build --lib`, using the `cint` that `CINT_EXE` names, else `cint` on `PATH`, else a bootstrap under `CINT_BUILD`. It keeps the library in the toolchain cache, keyed by every module digest the build receipt lists, the absent lookups and the toolchain identity.
- `require_verified=True` raises `LoadError`: nothing reads build records yet.
- A context keeps the runtime's default of 256 live registrations, and `Module.context()` has no `max_buffers` parameter, so the 257th live `View` or `Buffer` on one context raises `MemoryError`.
- An export whose type signature the decoder rejects is not callable; the rest of the library still loads. The decoder checks that references point backward, not the full order of definitions.
- A struct result raises `NotImplementedError` naming the parameter and form. An `E!T` or tuple result is read as a natural C struct: the tag in the set's underlying type, then the value.
- `fuel=` is reserved in every call unless the export has a parameter of that name; `None` is -1. Print output is discarded.
- A borrowed `View` bound to a kernel output must have `publish="copy"`; without it the entry faults `E_UNSUPPORTED` at entry, and the message says what to pass instead.
- Not provided, raising `NotImplementedError`: `.inplace`, and the `host`, `grants` and `check_inputs` parameters.

## PY-OQ-02. The device path

Status: Proposed.

- `cint.device(spec)` takes `"cpu"` (or `"cpu:0"`) and `"cuda:<ordinal>"`. A CUDA device is opened through the driver library (`nvcuda.dll` on Windows, `libcuda.so.1` elsewhere) with `ctypes`, so the package needs no toolkit. `"rocm"`, `"vulkan"`, `"metal"` and `"webgpu"` raise `NotImplementedError`.
- A device `Buffer` holds `I8` to `U64` elements. `Bool`, wide and fixed-point elements are refused.
- Nothing reads device memory implicitly. Indexing, `values()`, `memoryview` and the buffer protocol of a device `Buffer` or `View` raise, naming `cint.copy(x)`.
- DLPack: a published `Buffer` exports a read-only versioned capsule, version 1.0. A borrowed CUDA array must come as a versioned capsule, since an unversioned one cannot report a copy.
- When a kernel entry faults, the copy report lists a `fault-readback` copy for each device array argument. A function entry's copy report lists no read-backs, since the package cannot tell which of its device arrays the faulting dispatch read back.

## PY-OQ-03. Checkpoints, recordings and replay

Status: Proposed.

- Every library the package builds links the runtime's state functions (`rt/cint_state.h`), so a context can take a checkpoint, restore one, and give its state hash. A library built elsewhere loads and calls as before, and its checkpoints and `record=` raise `NotImplementedError` naming the function it lacks.
- `restore` raises `cint.Refused`, with `reason` "malformed", "revision" or "state", and leaves the context unchanged. After a restore, every `View` and `Buffer` registers again on its next use.
- `cint.read_checkpoint(data)` gives a typed view. Its `arenas` and `pools` are empty until the runtime checkpoints them.
- `cint.Recorder` records calls through each export's observer entry. An export that borrows an array is refused while recording, since a registration is not an event of `CINT-REC-1`.
- `cint.replay(module, recording, backend="ref")` replays on the SIR interpreter, built from this tree's `interp/` and `rt/`; `backend="cpu"` replays on the module's own library. Replays on the interpreter run one at a time.
- Not provided yet: `global_view`, `check_inputs=`, `audit_pure=True`, interrupting a call, a change of revision, `backend="ref"` in `cint.load`, and a context on the interpreter other than replay's.
