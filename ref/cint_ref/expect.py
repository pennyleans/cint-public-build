"""The `.expect` program-case format (SPEC-09 9.2 CONF-01, CONF-11).

Canonical text: ASCII, LF line ends including after the last line, one
`<key> <value>` field per line, keys in a fixed order. Format 1 is the T1
layout. Format 2 (decision 23, slice 2 patch D-10) has the third line
`format 2`, writes `fault.source-map self` and one `fault.stack` line per
stack position in every fault record, and ends a run with one `state.global`
line per scalar module-level variable (CONF-11 rules 5 and 10). Format 3
(decision 2026-10-05, BX12-21) is format 2 with the outcome `error` of an entry
that returns an error, with its `error.set`, `error.value` and `error.tag` lines
after `fuel-consumed` (rule 12). Format 4 (decision 2026-10-05, box 09 ruling
R9) adds the kernel form of a fault record: `fault.descriptor`, `fault.phase`
and `fault.kernel` lines and the kernel address (rule 13). A file without the
`format` line is format 1 and is compared in format 1.
"""
from __future__ import annotations

import hashlib
from dataclasses import dataclass, field

# Keys in canonical order. A key marked repeatable may occur several times in a row.
KEY_ORDER = [
    "case", "clause", "format", "source", "outcome", "stdout-bytes", "stdout-sha256", "return",
    "fuel-consumed", "error.set", "error.value", "error.tag",
    "fault.code", "fault.operation", "fault.operand", "fault.descriptor", "fault.exact", "fault.limit",
    "fault.position", "fault.revision", "fault.source-map", "fault.phase", "fault.kernel", "fault.address",
    "fault.stack-depth", "fault.stack",
    "diagnostic.code", "diagnostic.position", "diagnostic.fault.code", "diagnostic.fault.operation",
    "diagnostic.fault.operand", "diagnostic.fault.exact", "diagnostic.fault.limit",
    "refused.reason", "state.global",
]
REPEATABLE = {"fault.operand", "fault.descriptor", "diagnostic.fault.operand", "fault.stack", "state.global"}
FORMAT2_KEYS = ("format", "fault.source-map", "fault.stack", "state.global")
FORMAT3_KEYS = ("error.set", "error.value", "error.tag")
FORMAT4_KEYS = ("fault.descriptor", "fault.phase", "fault.kernel")
FORMATS = (1, 2, 3, 4)
PHASES = ("entry", "work-item", "epilogue")
_RANK = {k: i for i, k in enumerate(KEY_ORDER)}
OUTCOMES = ("value", "fault", "error", "compile-error", "refused")
SOURCES = ("anchor", "reference", "compile-error")


@dataclass
class Expect:
    lines: list = field(default_factory=list)      # [(key, value)]

    def get(self, key, default=None):
        for k, v in self.lines:
            if k == key:
                return v
        return default

    def getall(self, key):
        return [v for k, v in self.lines if k == key]

    @property
    def format(self) -> int:
        """2, 3 or 4 for a file with the line `format 2`, `format 3` or `format 4`, else 1 (SPEC-09
        CONF-11)."""
        f = self.get("format")
        return int(f) if f in ("2", "3", "4") else 1


def parse(text: str) -> Expect:
    if not text.endswith("\n"):
        raise ValueError("an .expect file ends with LF")
    if "\r" in text:
        raise ValueError("an .expect file uses LF line ends only")
    if not text.isascii():
        raise ValueError("an .expect file is ASCII")
    e = Expect()
    last = -1
    for n, line in enumerate(text[:-1].split("\n"), start=1):
        key, sep, value = line.partition(" ")
        if not sep or not value or value != value.strip():
            raise ValueError("line %d: expected `<key> <value>`" % n)
        if key not in _RANK:
            raise ValueError("line %d: unknown key %r" % (n, key))
        rank = _RANK[key]
        if rank < last or (rank == last and key not in REPEATABLE):
            raise ValueError("line %d: key %r is out of order or repeated" % (n, key))
        last = rank
        e.lines.append((key, value))
    if e.get("case") is None or e.get("outcome") is None:
        raise ValueError("an .expect file has `case` and `outcome` lines")
    if e.get("outcome") not in OUTCOMES:
        raise ValueError("unknown outcome %r" % e.get("outcome"))
    _check_format(e)
    return e


def _check_format(e: Expect):
    """CONF-11: `format 2`, `format 3` or `format 4` is the third line; a format 1 file has
    no line of a later format, a file below format 3 no `error` outcome (rule 12), and a
    file below format 4 no format 4 line; a fault record of format 2 or later has
    `fault.source-map` and `fault.stack-depth` stack lines (rule 5); in format 4 a kernel
    fault has its phase, kernel and address, with a work-item and step exactly in phase
    `work-item`, and any other fault has none of these (rule 13). An `error` outcome has
    the three `error.*` lines and no `return` line, and no other outcome has them."""
    keys = [k for k, _ in e.lines]
    if e.get("outcome") == "error":
        if [k for k in keys if k in FORMAT3_KEYS] != list(FORMAT3_KEYS) or "return" in keys:
            raise ValueError("an error outcome has `error.set`, `error.value` and `error.tag` lines and no `return` "
                             "line (CONF-11 rule 12)")
    elif any(k in FORMAT3_KEYS for k in keys):
        raise ValueError("error lines outside an error outcome")
    if "format" not in keys:
        late = [k for k in keys if k in FORMAT2_KEYS or k in FORMAT4_KEYS]
        if late:
            raise ValueError("a format 1 file has no %s line" % late[0])
        if e.get("outcome") == "error":
            raise ValueError("an error outcome appears only in a file of format 3 or later (CONF-11 rule 12)")
        return
    if keys.index("format") != 2 or e.get("format") not in ("2", "3", "4"):
        raise ValueError("the third line of a format 2, 3 or 4 file is `format 2`, `format 3` or `format 4`")
    if e.format < 3 and e.get("outcome") == "error":
        raise ValueError("an error outcome appears only in a file of format 3 or later (CONF-11 rule 12)")
    if e.format < 4 and any(k in FORMAT4_KEYS for k in keys):
        raise ValueError("a format %d file has no %s line" % (e.format, next(k for k in keys if k in FORMAT4_KEYS)))
    if e.get("outcome") == "fault":
        depth = e.get("fault.stack-depth")
        if e.get("fault.source-map") != "self" or depth is None or not depth.isdigit() \
                or int(depth) != len(e.getall("fault.stack")):
            raise ValueError("a format 2 fault record has `fault.source-map self` and one `fault.stack` "
                             "line per stack position (CONF-11 rule 5)")
        phase, address = e.get("fault.phase"), e.get("fault.address", "").split(" ")
        if phase is None:
            if e.get("fault.kernel") is not None or e.getall("fault.descriptor"):
                raise ValueError("kernel fault lines without `fault.phase` (CONF-11 rule 13)")
        elif phase not in PHASES or e.get("fault.kernel") is None or len(address) != 3 or \
                (address[1:] == ["none", "none"]) != (phase != "work-item"):
            raise ValueError("a kernel fault has its phase, kernel and address, with a work-item and step in "
                             "phase work-item only (CONF-11 rule 13)")
    elif any(k.startswith("fault.") for k in keys):
        raise ValueError("fault lines outside a fault outcome")
    if e.getall("state.global") and e.get("outcome") not in ("value", "fault", "error"):
        raise ValueError("state.global lines follow a run outcome only")


def render(e: Expect) -> str:
    return "".join("%s %s\n" % (k, v) for k, v in e.lines)


def _ascii(text: str) -> str:
    return " ".join(text.encode("ascii", "backslashreplace").decode("ascii").split())


def _pairs(lines):
    out = []
    for line in lines:
        k, _, v = line.partition(" ")
        out.append((k, v))
    return out


def from_outcome(o, case: str, clause: str | None = None, source: str | None = None,
                 fmt: int = 1) -> Expect:
    """The `.expect` text of an `exec.Outcome` in format `fmt` (SPEC-09 CONF-11). An `error`
    outcome has no text below format 3 (rule 12)."""
    if fmt not in FORMATS:
        raise ValueError("cint_ref writes .expect formats 1 to 4, not %r" % (fmt,))
    if o.kind == "error" and fmt < 3:
        raise ValueError("an error outcome is written in format 3 or later (SPEC-09 CONF-11 rule 12), not %d" % fmt)
    e = Expect()
    e.lines.append(("case", case))
    if clause:
        e.lines.append(("clause", clause))
    if fmt >= 2:
        e.lines.append(("format", str(fmt)))
    if source is None:
        source = "compile-error" if o.kind == "compile-error" else "reference"
    e.lines.append(("source", source))
    e.lines.append(("outcome", o.kind))
    if o.kind in ("value", "fault", "error"):
        e.lines.append(("stdout-bytes", str(len(o.stdout))))
        if o.stdout:
            e.lines.append(("stdout-sha256", hashlib.sha256(o.stdout).hexdigest()))
        if o.kind == "value" and o.value is not None:
            e.lines.append(("return", o.value.render()))
        e.lines.append(("fuel-consumed", str(o.fuel)))
        if o.kind == "error":
            e.lines += list(zip(FORMAT3_KEYS, o.error))
        if o.kind == "fault":
            e.lines += _pairs(o.record.to_expect_lines(fmt))
        if fmt >= 2:
            e.lines += [("state.global", "%s %s %s" % (m, n, v.render())) for m, n, v in o.state]
    elif o.kind == "compile-error":
        e.lines += _pairs(o.diagnostic.to_expect_lines())
    else:
        e.lines.append(("refused.reason", _ascii(o.message) or "unspecified"))
    return e


def first_difference(expected: Expect, actual: Expect):
    """None when equal, else (line number, expected line or None, actual line or None)."""
    a = ["%s %s" % kv for kv in expected.lines]
    b = ["%s %s" % kv for kv in actual.lines]
    for i in range(max(len(a), len(b))):
        x = a[i] if i < len(a) else None
        y = b[i] if i < len(b) else None
        if x != y:
            return (i + 1, x, y)
    return None
