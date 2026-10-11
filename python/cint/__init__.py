"""cint: the Python bridge of CINT, the pure-Python layer (SPEC-03 section 6; box 10).

This layer forms arguments and refuses what cannot be formed, before any
compiled code runs: borrowing host memory without a copy (`borrow`, `View`),
bridge-owned storage (`copy`, `empty`, `Buffer`), exact scalars (`Fixed`),
the float boundary (`from_float`, `to_float`, `FloatPolicy`), the exceptions
of SPEC-03 6.1 with `ErrorResult` outside `Fault` (BX10-06), and the copy
report (`CopyRecord`, `EntryReport`). `load` opens a library that `cint build
--lib` wrote, building it from a `.ci` source when given one (BX10-02,
BX10-07), and `Module.context()` makes a context whose exports are called as
`ctx.<export>(...)` through their `cx` wrappers (A-12). `device("cuda:0")`
opens a CUDA device, for `backend="cuda"` modules, device `Buffer`s, and
CUDA arrays borrowed through DLPack (box 11, BX11-16). `Context.checkpoint`,
`restore`, `state_hash` and `entry_scope` take and restore a context's CINT
state (SPEC-03 H-19, H-19a, H-14b), `read_checkpoint` reads a checkpoint as a
`CheckpointView`, `Recorder` records a context's calls (`Module.context(
record=...)`), and `replay` runs a recording again on the interpreter or the
compiled library and returns a `ReplayResult` (H-17a; box 13).

CPython 3.11 or later, standard library only, little-endian hosts (SPEC-03 A-1).
"""
import sys

if sys.version_info < (3, 11):
    raise ImportError("cint needs CPython 3.11 or later")
if sys.byteorder != "little":
    raise ImportError("cint supports little-endian hosts only (SPEC-03 A-1)")

from ._buffer import Buffer, copy, empty, from_float  # noqa: E402
from ._cuda import Device, device  # noqa: E402
from ._errors import (  # noqa: E402
    AliasFault, AssertFault, BorrowRefused, BoundsFault, BuildError, Busy, DepthFault, DivZeroFault, DomainFault,
    ErrorResult, Fault, Faulted, FuelFault, HazardError, HostError, LoadError, NarrowFault, OverflowFault, Refused,
    ReplayDivergence, ResourceError, ShapeFault, ShiftFault, StaleHandle, StaleHandleFault, Unpublished,
    Unreplayable, UnsupportedFault)
from ._fixed import Fixed  # noqa: E402
from ._floats import FloatConversion, FloatPolicy, to_float  # noqa: E402
from ._module import Context, Module, load  # noqa: E402
from ._replay import Recorder, ReplayResult, replay  # noqa: E402
from ._report import CopyRecord, EntryReport  # noqa: E402
from ._state import CheckpointView, read_checkpoint  # noqa: E402
from ._view import Region, View, borrow  # noqa: E402

__all__ = [
    "load", "Module", "Context", "device", "Device", "borrow", "copy", "empty", "from_float", "to_float", "View",
    "Buffer", "Region",
    "Fixed", "FloatPolicy", "FloatConversion", "CopyRecord", "EntryReport", "Recorder", "replay", "ReplayResult",
    "read_checkpoint", "CheckpointView",
    "Fault", "OverflowFault", "DivZeroFault", "BoundsFault", "ShapeFault", "ShiftFault", "NarrowFault",
    "AliasFault", "StaleHandleFault", "FuelFault", "UnsupportedFault", "DomainFault", "DepthFault", "AssertFault",
    "HostError", "HazardError", "ResourceError", "Refused", "BorrowRefused", "Busy", "Faulted", "Unpublished",
    "StaleHandle", "ReplayDivergence", "Unreplayable", "ErrorResult", "LoadError", "BuildError",
]
