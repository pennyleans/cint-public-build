"""The exceptions of the Python bridge (SPEC-03 6.1) and the mapping from ABI status to exception.

`Fault` and its subclasses carry a canonical fault record (SPEC-01 IM-149) and
its decoded fields. `ErrorResult` is an entry's error result (BX10-06): it is
not a `Fault`, so `except cint.Fault` never catches an error, and one subclass
per error set is made with `error_set_class`. A refusal (`Refused`) happens
before entry: no fuel, no fault, and no state change (SPEC-03 H-12).
"""
from __future__ import annotations

# ABI status results (SPEC-03 A-6).
CINT_OK = 0
CINT_FAULT = 1
CINT_REFUSED = 2
CINT_BUSY = 3
CINT_FAULTED = 4
CINT_RESOURCE = 5
CINT_DIVERGED = 6
CINT_UNREPLAYABLE = 7
CINT_HOST_ERROR = 8
CINT_HAZARD = 9

# Fault codes and numbers (SPEC-01 IM-104).
FAULT_CODES = {
    "E_OVERFLOW": 1, "E_DIV_ZERO": 2, "E_BOUNDS": 3, "E_SHAPE": 4, "E_SHIFT": 5, "E_NARROW": 6,
    "E_ALIAS": 7, "E_STALE_HANDLE": 8, "E_FUEL": 9, "E_UNSUPPORTED": 10, "E_DOMAIN": 11,
    "E_DEPTH": 12, "E_ASSERT": 13,
}
PHASES = ("entry", "work-item", "epilogue")      # SPEC-02 F-3; the phase byte of IM-149


class Fault(Exception):
    """`CINT_FAULT`: CINT code faulted, at entry phase or during execution.

    Attributes follow SPEC-03 6.1: `code`, `operation`, `operands` (exact
    Python values), `exact`, `limit`, `file`, `line`, `column`, `stack`,
    `revision`, `phase`, `dispatch`, `work_item`, `work_index`, `step`,
    `reason`, and `record` (the canonical bytes). `dispatch`, `phase`,
    `work_item`, and `step` are None outside a kernel. `operand_types` and
    `limit_type` name the type of each value, as the record's tags do."""
    code = None
    number = None

    def __init__(self, message: str | None = None, *, code: str | None = None, operation: str | None = None,
                 operands: tuple = (), operand_types: tuple = (), exact=None, limit=None, limit_type=None,
                 file: str | None = None, line: int | None = None, column: int | None = None,
                 stack: tuple = (), revision: bytes | None = None, source_map: bytes | None = None,
                 kernel: str | None = None, dispatch: int | None = None, phase: str | None = None,
                 work_item: int | None = None, work_index: tuple | None = None, step: int | None = None,
                 reason: str | None = None, record: bytes | None = None, version: int | None = None):
        self.code = code if code is not None else type(self).code
        self.operation = operation
        self.operands = tuple(operands)
        self.operand_types = tuple(operand_types)
        self.exact = exact
        self.limit = limit
        self.limit_type = limit_type
        self.file = file
        self.line = line
        self.column = column
        self.stack = tuple(stack)
        self.revision = revision
        self.source_map = source_map
        self.kernel = kernel
        self.dispatch = dispatch
        self.phase = phase
        self.work_item = work_item
        self.work_index = work_index
        self.step = step
        self.reason = reason
        self.record = record
        self.version = version
        super().__init__(message if message is not None else self._presentation())

    @property
    def position(self) -> str | None:
        if self.file is None:
            return None
        return "%s:%d:%d" % (self.file, self.line, self.column)

    def _presentation(self) -> str:
        """The diagnostic presentation of SPEC-01 IM-110: the code and position,
        a blank line, then one field per line."""
        head = self.code or "fault"
        if self.position is not None:
            head += " at " + self.position
        return head + "\n\n" + "\n".join(_field_lines(self))

    @classmethod
    def from_record(cls, record: bytes, *, index_space: tuple | None = None) -> "Fault":
        """The exception for canonical fault record bytes (SPEC-01 IM-149), as
        `cint_fault_get` returns them. On `Fault` itself the subclass is the
        one named for the record's code; on `HostError`, `HazardError`, and
        `ResourceError` it is that class. `index_space` is the kernel's index
        space, from which `work_index` is computed; without it `work_index`
        is None. Raises ValueError for bytes IM-148 rejects."""
        from ._record import decode_fault_record
        fields = decode_fault_record(record)
        target = cls
        if cls is Fault:
            target = FAULT_CLASSES.get(fields["code"], Fault)
        if index_space is not None and fields["work_item"] is not None:
            fields["work_index"] = work_index(fields["work_item"], index_space)
        return target(**fields)

    def entry_phase(self) -> bool:
        """True for a fault of the entry checks: a `bind.*` operation (SPEC-02
        F-8) or a kernel address in the entry phase."""
        return self.phase == "entry" or (self.operation or "").startswith("bind.")

    def expect_lines(self, fmt: int = 2, revision: bytes | None = None) -> list:
        """The `fault.*` lines of a `.expect` file (SPEC-09 CONF-11) for this
        fault, to compare with a frozen case field by field. `fault.revision`
        and `fault.source-map` read `self` when the record's digest is absent
        or equals `revision`. A fault that carries a kernel address takes the
        kernel form of format 4 (CONF-11 rule 13, box 09 ruling R9), so it
        raises ValueError in formats 1 and 2."""
        from ._record import render_value
        if fmt not in (1, 2, 4):
            raise ValueError("an .expect file is format 1, 2 or 4, not %r" % (fmt,))
        if self.kernel is not None and fmt != 4:
            raise ValueError("a fault raised by a kernel dispatch is written in format 4 (CONF-11 rule 13)")
        lines = ["fault.code " + self.code, "fault.operation " + self.operation]
        lines += ["fault.operand " + render_value(t, v) for t, v in zip(self.operand_types, self.operands)]
        lines.append("fault.exact " + ("none" if self.exact is None else render_value("Z", self.exact, bare=True)))
        lines.append("fault.limit " + ("none" if self.limit is None else render_value(self.limit_type, self.limit)))
        lines.append("fault.position " + self.position)
        lines.append("fault.revision " + _self_or_hex(self.revision, revision))
        if fmt >= 2:
            lines.append("fault.source-map " + _self_or_hex(self.source_map, None))
        if self.kernel is None:
            lines.append("fault.address none")
        else:
            lines += ["fault.phase " + self.phase, "fault.kernel " + self.kernel]
            item = "none none" if self.work_item is None else "%d %d" % (self.work_item, self.step)
            lines.append("fault.address %d %s" % (self.dispatch, item))
        lines.append("fault.stack-depth %d" % len(self.stack))
        if fmt >= 2:
            lines += ["fault.stack %s:%d:%d" % p for p in self.stack]
        return lines


def _self_or_hex(digest, own):
    if digest is None or digest == own:
        return "self"
    return digest.hex()


def _field_lines(f) -> list:
    """The labeled lines of SPEC-01 IM-110 for a fault or a refusal: two operands
    are `left` and `right`; `exact` is `rounded` for an operation that rounds,
    and `result` otherwise; `limit` is `maximum` or `minimum` by the side
    `exact` is on."""
    from ._record import display_value
    lines = []
    if f.operation is not None:
        lines.append("operation: %s" % f.operation)
    types = tuple(f.operand_types) + (None,) * (len(f.operands) - len(f.operand_types))
    labels = ("left", "right") if len(f.operands) == 2 else ("operand",) * len(f.operands)
    for label, t, v in zip(labels, types, f.operands):
        lines.append("%-10s %s" % (label + ":", display_value(t, v)))
    if f.exact is not None:
        lines.append("%-10s %s" % ("rounded:" if _rounds(f.operation) else "result:", display_value("Z", f.exact)))
    if f.limit is not None:
        label = "limit:"
        bound = getattr(f.limit, "raw", f.limit)
        if isinstance(f.exact, int) and isinstance(bound, int) and f.exact != bound:
            label = "maximum:" if f.exact > bound else "minimum:"
        lines.append("%-10s %s" % (label, display_value(f.limit_type, f.limit)))
    if getattr(f, "kernel", None) is not None:
        where = "%s, dispatch %d, phase %s" % (f.kernel, f.dispatch, f.phase)
        if f.work_item is not None:
            where += ", work item %d, step %d" % (f.work_item, f.step)
        lines.append("%-10s %s" % ("kernel:", where))
    if f.reason is not None:
        lines.append("%-10s %s" % ("reason:", f.reason))
    return lines


_ROUNDING_OPS = ("muldiv.", "div_round.", "isqrt_round.", "from_f64.", "from_f32.")


def _rounds(operation) -> bool:
    """An operation whose `exact` is already rounded (SPEC-01 IM-110): one that
    names a rounding mode as its last component, or a host-float conversion."""
    if operation is None:
        return False
    from ._floats import ROUND_MODES
    return operation.startswith(_ROUNDING_OPS) or operation.rsplit(".", 1)[-1] in ROUND_MODES


def work_index(item: int, space: tuple) -> tuple:
    """The index tuple of a linear work-item index, row-major over `space`
    (SPEC-01 9.3: the last index varies fastest)."""
    out = []
    for extent in reversed(tuple(space)):
        if extent <= 0:
            raise ValueError("an index space with an empty dimension has no work items")
        item, i = divmod(item, extent)
        out.append(i)
    if item:
        raise ValueError("work item outside the index space %r" % (tuple(space),))
    return tuple(reversed(out))


def _fault_class(name: str, code: str, doc: str):
    return type(name, (Fault,), {"code": code, "number": FAULT_CODES[code], "__doc__": doc,
                                 "__module__": "cint"})


OverflowFault = _fault_class("OverflowFault", "E_OVERFLOW", "E_OVERFLOW: a checked result out of range.")
DivZeroFault = _fault_class("DivZeroFault", "E_DIV_ZERO", "E_DIV_ZERO: a zero divisor.")
BoundsFault = _fault_class("BoundsFault", "E_BOUNDS", "E_BOUNDS: an index outside a view's extent.")
ShapeFault = _fault_class("ShapeFault", "E_SHAPE", "E_SHAPE: a shape disagreement.")
ShiftFault = _fault_class("ShiftFault", "E_SHIFT", "E_SHIFT: a shift count outside its range.")
NarrowFault = _fault_class("NarrowFault", "E_NARROW", "E_NARROW: a checked conversion out of range.")
AliasFault = _fault_class("AliasFault", "E_ALIAS", "E_ALIAS: a writable output overlapping another operand.")
StaleHandleFault = _fault_class("StaleHandleFault", "E_STALE_HANDLE", "E_STALE_HANDLE: a handle or view "
                                "whose generation has ended.")
FuelFault = _fault_class("FuelFault", "E_FUEL", "E_FUEL: the entry's fuel budget is exhausted.")
UnsupportedFault = _fault_class("UnsupportedFault", "E_UNSUPPORTED", "E_UNSUPPORTED: the backend cannot "
                                "implement a type, operation, layout, or depth exactly.")
DomainFault = _fault_class("DomainFault", "E_DOMAIN", "E_DOMAIN: an argument outside an operation's domain.")
DepthFault = _fault_class("DepthFault", "E_DEPTH", "E_DEPTH: a call beyond the entry's depth limit.")
AssertFault = _fault_class("AssertFault", "E_ASSERT", "E_ASSERT: a failed assert outside a test block.")
FAULT_CLASSES = {cls.code: cls for cls in (
    OverflowFault, DivZeroFault, BoundsFault, ShapeFault, ShiftFault, NarrowFault, AliasFault,
    StaleHandleFault, FuelFault, UnsupportedFault, DomainFault, DepthFault, AssertFault)}


class HostError(Fault):
    """`CINT_HOST_ERROR`: a host function failed outside its declared error union.
    The Python exception it raised, if any, is chained as `__cause__`."""


class HazardError(Fault):
    """`CINT_HAZARD`: a borrowed input changed during the entry (SPEC-03 P-16)."""


class ResourceError(Fault):
    """`CINT_RESOURCE` mid-entry: the host allocator or device failed."""


class Refused(Exception):
    """`CINT_REFUSED`: an argument could not be formed into a CINT value. Nothing
    ran: no fuel, no fault, and no registration (SPEC-03 H-12). Carries the code
    and operand fields of a fault record but no source position: `operation`
    is None where no SPEC-01 IM-130 operation applies. For an element of an
    array, `index` is the first refused element as an index tuple in row-major
    order and `count` the number refused (SPEC-03 X-8, SPEC-05 TR-VAL-2)."""

    def __init__(self, message: str, *, code: str = "E_UNSUPPORTED", operation: str | None = None,
                 operands: tuple = (), operand_types: tuple = (), exact=None, limit=None,
                 limit_type: str | None = None, reason: str | None = None, index: tuple | None = None,
                 count: int | None = None):
        if code not in FAULT_CODES:
            raise ValueError("a refusal carries a fault code of SPEC-01 IM-104, not %r" % (code,))
        self.code = code
        self.operation = operation
        self.operands = tuple(operands)
        self.operand_types = tuple(operand_types)
        self.exact = exact
        self.limit = limit
        self.limit_type = limit_type
        self.reason = reason
        self.index = index
        self.count = count
        self.detail = message
        text = "%s: %s" % (code, message)
        fields = _field_lines(self)
        if fields:
            text += "\n" + "\n".join("  " + line for line in fields)
        super().__init__(text)


class BorrowRefused(Refused):
    """`cint.borrow` cannot meet its contract (SPEC-03 P-1 to P-14). It never copies;
    the message names the condition and the alternative, usually `cint.copy`."""


class Busy(Exception):
    """`CINT_BUSY`: an entry is already active on the context, or an output has
    live exports (code `E_ALIAS`, SPEC-03 P-18)."""

    def __init__(self, message: str, *, code: str | None = None):
        super().__init__(message if code is None else "%s: %s" % (code, message))
        self.code = code


class Faulted(Exception):
    """An entry attempted on a faulted context (SPEC-03 A-7). Recovery is
    explicit (BX10-05): `ctx.clear_fault()` and then an entry that
    reinitializes the module state, or a new context from `Module.context()`.
    `held` is the fault the context holds. `nothing_ran` is True for a fault
    of the entry checks, after which `clear_fault()` alone is enough; by
    default it is read from `held`."""

    def __init__(self, held: Fault | None = None, *, nothing_ran: bool | None = None):
        if nothing_ran is None:
            nothing_ran = held is not None and held.entry_phase()
        self.held = held
        self.nothing_ran = nothing_ran
        what = "a fault"
        if held is not None:
            what = held.code or what
            if held.operation is not None:
                what += ", " + held.operation
            if held.position is not None:
                what += " at " + held.position
        text = "the context holds %s and runs no entry until the fault is cleared (SPEC-03 A-7). " % what
        if nothing_ran:
            text += ("It is a fault of the entry checks, so nothing ran: ctx.clear_fault() alone makes the "
                     "context usable again, or make a new context with Module.context().")
        else:
            text += ("It was raised while the entry ran, so the module state is as the fault left it: call "
                     "ctx.clear_fault() and then an entry that reinitializes that state (A-7b), or make a new "
                     "context with Module.context().")
        super().__init__(text)


class Unpublished(Exception):
    """Reading a `Buffer` that has never been published (SPEC-03 P-17)."""


class StaleHandle(Exception):
    """Using a `View` or `Buffer` after its release, or a `View` whose
    registration a `restore` removed (SPEC-03 M-11a)."""


class ReplayDivergence(Exception):
    """`CINT_DIVERGED`: a replay mismatch (SPEC-03 H-17)."""


class Unreplayable(Exception):
    """`CINT_UNREPLAYABLE`: replay reached an `unrestricted` occurrence (SPEC-03 H-17)."""


class ErrorResult(Exception):
    """An entry's error result (BX10-06): the entry completed with `CINT_OK`
    and an error alternative, and the context is not faulted (BX12-12). It is
    not a `Fault`. `set` names the error set, `name` the value, and `tag` its
    number, counted from 1 in declaration order (SPEC-04 LS-93). The class of
    a combined set (SPEC-04 LS-97) is a base of the class of each of its
    member sets, so `except` on the combined class catches each member's
    values (OQ-201)."""
    set: str | None = None
    qualified: str | None = None
    members: tuple = ()
    parts: tuple = ()

    def __init__(self, name: str, tag: int, set_name: str | None = None):
        self.set = set_name if set_name is not None else type(self).set
        self.name = name
        self.tag = tag
        super().__init__("%s.%s (tag %d)" % (self.set, name, tag))

    @classmethod
    def split(cls, tag: int) -> tuple:
        """The member set and its tag for a tag of this set: (cls, tag) for a
        declared set; for a combined set, whose tags count through its member
        sets in order, (member set class, tag within it) (OQ-201)."""
        if not cls.members and not cls.parts:
            raise TypeError("split is called on the class of one error set")
        total = len(cls.members) if not cls.parts else sum(len(p.members) for p in cls.parts)
        if isinstance(tag, bool) or not isinstance(tag, int) or not 1 <= tag <= total:
            raise ValueError("%s has no value with tag %r (tags 1 to %d; 0 is success)" % (cls.set, tag, total))
        if not cls.parts:
            return cls, tag
        for part in cls.parts:
            if tag <= len(part.members):
                return part, tag
            tag -= len(part.members)
        raise AssertionError("unreachable")

    @classmethod
    def from_tag(cls, tag: int) -> "ErrorResult":
        """The error result of a set class for the tag an entry returned. For
        a combined set it is an instance of the member set's class, with the
        tag within that set."""
        part, inner = cls.split(tag)
        return part(part.members[inner - 1], inner)


def _short(qualified: str) -> str:
    return qualified.rsplit(".", 1)[-1]


def error_set_class(set_name: str, members, *, bases: tuple = (), qualified: str | None = None) -> type:
    """The `ErrorResult` subclass of one error set, reachable from a module as
    `mod.<set_name>`. `members` are the value names in declaration order; the
    tag of the first is 1 (SPEC-04 LS-93). `bases` are the classes of the
    combined sets it belongs to, and `qualified` its qualified name."""
    members = tuple(members)
    if not set_name.isidentifier() or not members or not all(isinstance(m, str) and m.isidentifier()
                                                              for m in members):
        raise ValueError("an error set has an identifier for a name and at least one value name")
    if len(set(members)) != len(members):
        raise ValueError("error set %s names a value twice" % set_name)
    if not all(isinstance(b, type) and issubclass(b, ErrorResult) and b is not ErrorResult and not b.members
               for b in bases):
        raise ValueError("the bases of a declared set's class are classes of combined sets")
    return type(set_name, tuple(bases) or (ErrorResult,),
                {"set": set_name, "qualified": qualified or set_name, "members": members, "parts": (),
                 "__module__": "cint", "__doc__": "Error results of the set %s." % (qualified or set_name)})


def error_set_family(sets: dict) -> dict:
    """The classes of a group of error sets, by qualified name. `sets` maps a
    qualified name to ("declared", value names) or ("combined", member
    qualified names). Each class is named by the last part of its qualified
    name (BX10-27); a combined set's class is a base of each member set's
    class, and its tags count through the member sets in order (OQ-201)."""
    combined = {q: tuple(spec[1]) for q, spec in sets.items() if spec[0] == "combined"}
    classes = {}
    for q, parts in combined.items():
        if len(set(parts)) != len(parts) or not parts:
            raise ValueError("combined set %s lists a member set twice, or none" % q)
        for m in parts:
            if m not in sets or sets[m][0] != "declared":
                raise ValueError("combined set %s names %s, which is not a declared set" % (q, m))
        name = _short(q)
        classes[q] = type(name, (ErrorResult,), {"set": name, "qualified": q, "members": (), "parts": (),
                                                 "__module__": "cint",
                                                 "__doc__": "Error results of the combined set %s." % q})
    for q, spec in sets.items():
        if spec[0] == "declared":
            bases = tuple(classes[c] for c in combined if q in combined[c])
            classes[q] = error_set_class(_short(q), spec[1], bases=bases, qualified=q)
        elif spec[0] != "combined":
            raise ValueError("an error set is declared or combined, not %r" % (spec[0],))
    for q, parts in combined.items():
        classes[q].parts = tuple(classes[m] for m in parts)
    return classes


class LoadError(Exception):
    """`cint.load` could not load a module: the path names no library or
    source, the library has no reflection table or an ABI this bridge does not
    read, or no build is possible (BX10-02, BX10-07)."""


class BuildError(LoadError):
    """`cint build --lib` failed for a source passed to `cint.load`. `status`
    is the CLI's exit status, `diagnostics` its JSON diagnostics (dicts with
    `code`, `file`, `line`, `column`, and `message`), and `output` the rest of
    what it wrote."""

    def __init__(self, message: str, *, status: int | None = None, diagnostics: tuple = (), output: str = ""):
        self.status = status
        self.diagnostics = tuple(diagnostics)
        self.output = output
        super().__init__(message)


def exception_for_status(status: int, *, record: bytes | None = None, held: Fault | None = None,
                         nothing_ran: bool | None = None, refusal: Refused | None = None, mid_entry: bool = True,
                         message: str | None = None) -> BaseException:
    """The exception for a status other than `CINT_OK` (SPEC-03 6.1 mapping).
    `record` is the canonical fault record for the statuses that leave the
    context Faulted; `held` and `nothing_ran` describe the held fault for
    `CINT_FAULTED`; `refusal` is the prepared refusal for `CINT_REFUSED`.
    `CINT_RESOURCE` before an entry is a MemoryError: no CINT state changed."""
    if status == CINT_FAULT:
        return Fault.from_record(record)
    if status == CINT_HOST_ERROR:
        return HostError.from_record(record)
    if status == CINT_HAZARD:
        return HazardError.from_record(record)
    if status == CINT_RESOURCE:
        if mid_entry:
            return ResourceError.from_record(record)
        return MemoryError(message or "the allocator could not supply the storage (CINT_RESOURCE)")
    if status == CINT_REFUSED:
        return refusal if refusal is not None else Refused(message or "the arguments were refused")
    if status == CINT_BUSY:
        return Busy(message or "an entry or effect is already active on this context")
    if status == CINT_FAULTED:
        return Faulted(held, nothing_ran=nothing_ran)
    if status == CINT_DIVERGED:
        return ReplayDivergence(message or "replay diverged")
    if status == CINT_UNREPLAYABLE:
        return Unreplayable(message or "replay reached an unrestricted occurrence")
    raise ValueError("status %r is not a failure status of SPEC-03 A-6" % (status,))
