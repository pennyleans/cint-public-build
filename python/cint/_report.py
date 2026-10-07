"""The copy report of an entry (BX10-17; SPEC-03 P-15, P-16, M-31).

`Context.last_entry()` lists each copy an entry made: what was copied, why,
its element type, and its element count. The report counts elements, not
bytes, so it reads the same on every backend and representation. Under
BX10-03 A a function entry lists no copy.
"""
from __future__ import annotations

from dataclasses import dataclass
import numbers

from ._elem import parse_elem

REASONS = {
    "publish-copy": "a borrowed output staged and copied on success (SPEC-03 P-15)",
    "snapshot": "a borrowed input copied while recording (SPEC-03 P-16)",
    "staging": "an output staged because it could not publish by renaming (SPEC-03 M-31)",
}


@dataclass(frozen=True)
class CopyRecord:
    """One copy an entry made. `buffer` names what was copied: the parameter
    it was bound to."""
    buffer: str
    reason: str
    elem: str
    elements: int

    def __post_init__(self):
        if self.reason not in REASONS:
            raise ValueError("a copy's reason is one of %s, not %r" % (", ".join(REASONS), self.reason))
        parse_elem(self.elem)
        if isinstance(self.elements, bool) or not isinstance(self.elements, numbers.Integral) or self.elements < 0:
            raise ValueError("a copy's element count is a non-negative int, not %r" % (self.elements,))

    def __str__(self) -> str:
        return "%s %s: %s x %s" % (self.reason, self.buffer, self.elem, format(self.elements, ","))


@dataclass(frozen=True)
class EntryReport:
    """What one entry did that the caller may need to see: its copies, and
    notes such as a legacy DLPack producer that could not be asked for
    `copy=False` (SPEC-03 P-10a)."""
    entry: str
    copies: tuple = ()
    notes: tuple = ()

    def __post_init__(self):
        object.__setattr__(self, "copies", tuple(self.copies))
        object.__setattr__(self, "notes", tuple(self.notes))
        if not all(isinstance(c, CopyRecord) for c in self.copies):
            raise TypeError("copies are CopyRecord values")

    @property
    def copied_elements(self) -> int:
        """The elements copied, over every copy of the entry."""
        return sum(c.elements for c in self.copies)

    def __str__(self) -> str:
        lines = ["%s: %s" % (self.entry, "no copies" if not self.copies else
                             "%d cop%s" % (len(self.copies), "y" if len(self.copies) == 1 else "ies"))]
        lines += ["  " + str(c) for c in self.copies]
        lines += ["  note: " + n for n in self.notes]
        return "\n".join(lines)
