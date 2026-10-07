"""cint: the Python bridge of CINT, the pure-Python layer (SPEC-03 section 6; box 10).

This layer forms arguments and refuses what cannot be formed, before any
compiled code runs: borrowing host memory without a copy (`borrow`, `View`),
bridge-owned storage (`copy`, `empty`, `Buffer`), exact scalars (`Fixed`),
the float boundary (`from_float`, `to_float`, `FloatPolicy`), the exceptions
of SPEC-03 6.1 with `ErrorResult` outside `Fault` (BX10-06), and the copy
report (`CopyRecord`, `EntryReport`). `load` opens a library that `cint build
--lib` wrote, building it from a `.ci` source when given one (BX10-02,
BX10-07), and `Module.context()` makes a context whose exports are called as
`ctx.<export>(...)` through their `cx` wrappers (A-12).

CPython 3.11 or later, standard library only, little-endian hosts (SPEC-03 A-1).
"""
import sys

if sys.version_info < (3, 11):
    raise ImportError("cint needs CPython 3.11 or later (BX10-20)")
if sys.byteorder != "little":
    raise ImportError("cint supports little-endian hosts only (SPEC-03 A-1)")

from ._buffer import Buffer, copy, empty, from_float  # noqa: E402
from ._errors import (  # noqa: E402
    AliasFault, AssertFault, BorrowRefused, BoundsFault, BuildError, Busy, DepthFault, DivZeroFault, DomainFault,
    ErrorResult, Fault, Faulted, FuelFault, HazardError, HostError, LoadError, NarrowFault, OverflowFault, Refused,
    ReplayDivergence, ResourceError, ShapeFault, ShiftFault, StaleHandle, StaleHandleFault, Unpublished,
    Unreplayable, UnsupportedFault)
from ._fixed import Fixed  # noqa: E402
from ._floats import FloatConversion, FloatPolicy, to_float  # noqa: E402
from ._module import Context, Module, load  # noqa: E402
from ._report import CopyRecord, EntryReport  # noqa: E402
from ._view import Region, View, borrow  # noqa: E402

__all__ = [
    "load", "Module", "Context", "borrow", "copy", "empty", "from_float", "to_float", "View", "Buffer", "Region",
    "Fixed", "FloatPolicy", "FloatConversion", "CopyRecord", "EntryReport",
    "Fault", "OverflowFault", "DivZeroFault", "BoundsFault", "ShapeFault", "ShiftFault", "NarrowFault",
    "AliasFault", "StaleHandleFault", "FuelFault", "UnsupportedFault", "DomainFault", "DepthFault", "AssertFault",
    "HostError", "HazardError", "ResourceError", "Refused", "BorrowRefused", "Busy", "Faulted", "Unpublished",
    "StaleHandle", "ReplayDivergence", "Unreplayable", "ErrorResult", "LoadError", "BuildError",
]
