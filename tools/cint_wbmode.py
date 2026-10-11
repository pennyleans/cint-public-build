"""Conformance workbench mode: each conformance script, entry by entry, against `cint run`.

SPEC-06 5.1 (box 14 ruling BX14-04); SPEC-09 section 14, row T4; box 14 note section 5.1.

Usage (from the repository root):

    python tools/cint_wbmode.py --leg {msvc,gcc,clang,apple-clang} [--cint PATH]
                                [--workbench PATH] [--only TEXT]... [--out DIR]
                                [--receipt PATH | --no-receipt]

The scripts are those that cpu-sir-interp runs, the executed set of `tools/cint_check.py
--compiler b1 --interp`: a conformance case with no named entry and a frozen `.expect`, not
held, whose root module has a top-level statement other than a variable declaration (a script,
SPEC-04 LS-218; a module of declarations only is not one, as cint_ref decides), that B1 compiles
(`cint emit-c`) and that `cint-interp run` does not refuse (exit 3 and its `unsupported:`
reason). A script outside the set is listed with the reason.

The feeder submits to a workbench session on cpu-sir-interp: each imported module by `:load`,
which imports it into the session under its last name (the session refuses `import` as an
entry); then the script's declarations other than variables as one entry; then each top-level
statement, variable declarations included, as an entry of its own, in source order. It stops
after the first entry that faults or that the session refuses. Three defaults of the feeder:

- a `test` is left out of the declarations entry: `cint run` does not run tests, and the
  session refuses a test as an entry (C1050);
- an arena or a pool declaration goes last in the declarations entry, since the session names
  an entry of several declarations by its first;
- a top-level `defer` and the statements after it are one entry: the session runs a deferred
  block when its entry ends, and `cint run` when the script ends (SPEC-04 LS-185).

The session ends an entry at a line that leaves no bracket or block comment open
(wb_session.c, incomplete()). So the feeder blanks comments and joins an item's lines where
the session would end it early, and keeps for each line of an entry the script line and
column of each part of it. After the last entry the session saves its case (`:save-case`,
CINT-CASE-1, SPEC-06 9.2), which gives the bytes printed, each entry's outcome and the fault
record, and `:inspect` reads each variable that the fed entries declared.

The reference is `cint run` of the script rewritten: `void main() {` before the first
statement, the type of each top-level variable declaration blanked (columns kept) so that the
declaration assigns the module-level variable a `TYPE NAME;` line at the end declares, any
declaration among the statements moved after `main`, and `}` after the last statement. `cint
run` gives the bytes printed and the outcome; the program `cint build` makes gives the fault
record and the module state (the `state.global` lines of SPEC-09 CONF-11 rule 10), and its
bytes and outcome must equal `cint run`'s.

Compared for each script: the stdout bytes; the outcome (value, fault, or error); the
canonical fault, the CONF-11 `fault.*` lines of both records (cint_ref's IM-148 reader) with
the positions mapped back to the script's lines: the session's from its report's `-->` and
`called from:` lines (`entry K:L:C`), the program's from its record; and the final value of
each variable whose declaring entry was fed, the post-fault state included, that has a
`state.global` line. Not compared, and listed so: fuel, since the session meters each entry
and `cint run` the whole script; a variable of a type with no `state.global` form yet (an
array, struct, or fixed-point value; OQ-156); a variable declared after a top-level `defer`,
a local of that entry; and a variable whose entry was not reached.

Build outputs go under <out> (default <build>/wbmode/<leg>, <build> from CINT_BUILD), never in
the repository: cint-workbench (unless --workbench PATH) and cint-interp at -O2 with portable
helpers, and B1 from this tree (unless --cint PATH, whose compiler sources must be this
tree's). The receipt, schema cint-wbmode-receipt-1, goes to <out>/receipt.json or --receipt
PATH. Exit status 0 when every executed script agrees, 1 when one does not, 2 for a usage or
build error. workbench/tests/wbt_wbmode.py runs a quick subset in the cint-workbench group.
"""
from __future__ import annotations

import argparse
import dataclasses
import datetime
import json
import os
import pathlib
import platform
import re
import shutil
import subprocess
import sys
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import cint_check as cc  # noqa: E402  (also puts ref/ and conformance/tools/ on the path)
from cint_ref import parser as P  # noqa: E402
from cint_ref.faults import RUN_TIME, CompileError, Refused, decode_fault_record  # noqa: E402
from cint_ref.lexer import BOX12_KEYWORDS, tokenize  # noqa: E402
import gen_expect  # noqa: E402

CONF = ROOT / "conformance"
SCHEMA = "cint-wbmode-receipt-1"
TIMEOUT = 120                     # seconds for one session step or one command
PROMPT_RE = re.compile(rb"ci\[(\d+)\]> \Z")
POS_RE = re.compile(r"\A(.*):(\d+):(\d+)\Z")
ENTRY_RE = re.compile(r"\Aentry (\d+)\Z")
SAN_ENV = {"ASAN_OPTIONS": "detect_leaks=1:abort_on_error=0",
           "UBSAN_OPTIONS": "print_stacktrace=1:halt_on_error=1"}
OUTCOMES = {0: "value", 1: "fault", 6: "error"}   # `cint run` exit status (SPEC-06 3.4a)


class Unfit(Exception):
    """A script the feeder or the rewrite cannot shape, with the reason."""


def ascii_text(text: str) -> str:
    """text with every character outside ASCII escaped, for the receipt's canonical form."""
    return text.encode("ascii", "backslashreplace").decode("ascii")


# --------------------------------------------------------------------------- the items

@dataclasses.dataclass
class Item:
    kind: str       # profile, import, decl, test, memory, var, stmt, defer
    node: object
    start: int      # index of its first token (its `export`, when it has one)
    end: int        # index after its last token


class _Recorder(P.Parser):
    """cint_ref's parser, recording the kind and the tokens of each top-level item."""

    def __init__(self, toks, path):
        super().__init__(P._ListStream(toks), path)
        self.toks, self.items, self.depth = toks, [], 0


def _recording(name, kind):
    base = getattr(P.Parser, name)

    def method(self, *args, **kw):
        start, top = self.s.i, self.depth == 0
        self.depth += 1
        try:
            node = base(self, *args, **kw)
        finally:
            self.depth -= 1
        if top:
            before = self.toks[start - 1] if start > 0 else None
            if before is not None and before.kind == "keyword" and before.text == "export":
                start -= 1
            k = kind
            if kind == "stmt":
                if isinstance(node, P.VarDecl):
                    k = "memory" if getattr(node.type, "capacity", None) is not None else "var"
                elif isinstance(node, P.Defer):
                    k = "defer"
            self.items.append(Item(k, node, start, self.s.i))
        return node
    return method


for _name, _kind in (("parse_profile", "profile"), ("parse_import", "import"),
                     ("parse_error_decl", "decl"), ("parse_kernel", "decl"), ("parse_struct", "decl"),
                     ("parse_test", "test"), ("parse_static_assert", "decl"), ("parse_const", "decl"),
                     ("parse_function", "decl"), ("parse_statement", "stmt")):
    setattr(_Recorder, _name, _recording(_name, _kind))


def script_items(text: str, rel: str):
    """The tokens and the top-level items of a module, in source order, and its Module. The
    items are checked against cint_ref's own parse: the same nodes, each once."""
    toks = tokenize(text, rel, keywords=BOX12_KEYWORDS)
    rec = _Recorder(toks, rel)
    m = rec.parse_module(text)
    nodes = [*m.imports, *m.functions, *m.structs, *m.consts, *m.tests, *m.statements,
             *m.static_asserts, *m.kernels, *m.errors, *getattr(m, "memories", [])]
    if sorted(map(id, nodes)) != sorted(id(it.node) for it in rec.items if it.kind != "profile"):
        raise Unfit("the recorded items differ from cint_ref's parse of the module")
    return toks, rec.items, m


def blanked(text: str, toks: list, item: Item) -> str:
    """The text of the item's lines, with every character outside its tokens on them that is
    not a line feed or a tab made a space: comments go, columns stay (cint_ref counts columns
    in characters)."""
    first, last = toks[item.start], toks[item.end - 1]
    a = text.rfind("\n", 0, first.offset) + 1
    b = text.find("\n", last.end)
    b = len(text) if b < 0 else b
    out = [c if c in "\n\t" else " " for c in text[a:b]]
    for t in toks[item.start:item.end]:
        out[t.offset - a:t.end - a] = text[t.offset:t.end]
    return "".join(out)


def incomplete(text: str) -> bool:
    """wb_session.c's incomplete(): whether a bracket or a block comment is still open."""
    depth = comment = 0
    i, n = 0, len(text)
    while i < n:
        a, b = text[i], text[i + 1:i + 2]
        if comment > 0 or (a == "/" and b == "*"):   # block comments nest (LS-14)
            opens, closes = a == "/" and b == "*", a == "*" and b == "/"
            comment += opens
            comment -= comment > 0 and closes
            i += 2 if opens or closes else 1
        elif a == "/" and b == "/":
            j = text.find("\n", i)
            i = n if j < 0 else j + 1
        elif a in "\"'":
            i += 1
            while i < n and text[i] != a and text[i] != "\n":
                i += 2 if text[i] == "\\" and i + 1 < n and text[i + 1] != "\n" else 1
            if i >= n:
                break
            i += 1
        else:
            depth += a in "([{"
            depth -= a in ")]}"
            i += 1
    return depth > 0 or comment > 0


@dataclasses.dataclass
class Entry:
    kind: str           # import, declarations, var, stmt, defer
    items: list
    lines: list         # the entry's lines as the session reads them
    segments: list      # per line: [(entry column, script line, script column)], ascending
    names: list         # the variables it declares as session variables

    def map(self, line: int, column: int):
        """The script's (line, column) of the entry's (line, column), or None."""
        if not 1 <= line <= len(self.segments) or not self.segments[line - 1]:
            return None
        at, src_line, src_col = [s for s in self.segments[line - 1] if s[0] <= column][-1]
        return src_line, src_col + column - at


def render(text: str, toks: list, items: list):
    """The entry's lines and segments for items, in order: each line of each item after its
    comments are blanked, the first padded to its column; a line is joined to the one before,
    after a space, where the session would otherwise end the entry before the items end."""
    pieces = []
    for it in items:
        first = toks[it.start]
        for k, piece in enumerate(blanked(text, toks, it).split("\n")):
            col = first.col if k == 0 else 1
            pieces.append((first.line + k, col, piece[col - 1:].rstrip() if k == 0 else piece.rstrip()))
    lines, segments = [], []
    for line, col, piece in pieces:
        if lines and not incomplete("\n".join(lines)):
            rest = piece.lstrip()
            if rest:
                segments[-1].append((len(lines[-1]) + 2, line, col + len(piece) - len(rest)))
                lines[-1] += " " + rest
        elif lines or piece.strip():
            lines.append(" " * (col - 1) + piece)
            segments.append([(1, line, 1)])
    if not lines or incomplete("\n".join(lines)):
        raise Unfit("an entry that leaves a bracket or a comment open")
    return lines, segments


def plan(text: str, toks: list, items: list) -> list:
    """The entries the feeder submits, in order."""
    groups = [("import", [it]) for it in items if it.kind == "import"]
    decls = [it for it in items if it.kind == "decl"] + [it for it in items if it.kind == "memory"]
    if decls:
        groups.append(("declarations", decls))
    stmts = [it for it in items if it.kind in ("var", "stmt", "defer")]
    for k, it in enumerate(stmts):
        if it.kind == "defer":
            groups.append(("defer", stmts[k:]))
            break
        groups.append((it.kind, [it]))
    entries = []
    for kind, group in groups:
        if kind == "import":   # the session takes a module by :load, under its last name
            d = group[0].node
            if d.selected is not None or d.alias != d.segments[-1]:
                raise Unfit("an import with `as` or selected names, which :load cannot make")
            entries.append(Entry(kind, group, [":load %s.ci" % "/".join(d.segments)], [[]], []))
            continue
        lines, segments = render(text, toks, group)
        names = [group[0].node.name] if kind == "var" else []
        entries.append(Entry(kind, group, lines, segments, names))
    return entries


# --------------------------------------------------------------------------- the rewrite

def name_token(toks: list, item: Item) -> int:
    """The index of the token of a variable declaration's name."""
    pos = item.node.name_pos
    for k in range(item.start, item.end):
        if toks[k].line == pos.line and toks[k].col == pos.column:
            return k
    raise Unfit("no token at the name of %s" % item.node.name)


def rewrite(text: str, toks: list, items: list):
    """The script rewritten for `cint run`, and for each of its lines the script line it comes
    from (None for a line it adds)."""
    stmts = [it for it in items if it.kind in ("var", "stmt", "defer")]
    if not stmts:
        raise Unfit("no top-level statement")
    for it in items:   # each item has its lines to itself
        if it.start > 0 and toks[it.start - 1].line == toks[it.start].line or \
                toks[it.end].kind != "eof" and toks[it.end].line == toks[it.end - 1].line:
            raise Unfit("an item shares a line with another (line %d)" % toks[it.start].line)
    src = text.split("\n")
    if src[-1] == "":
        src.pop()
    first, last = toks[stmts[0].start].line, toks[stmts[-1].end - 1].line
    moved = []
    for it in items:
        a, b = toks[it.start].line, toks[it.end - 1].line
        if it.kind not in ("var", "stmt", "defer") and first < a <= last:
            moved += range(a, b + 1)
    body, globals_ = list(src), []
    for it in stmts:
        if it.kind != "var":
            continue
        d = it.node
        if d.is_const or d.mode is not None:
            raise Unfit("the variable %s is a constant or a view" % d.name)
        a, n = toks[it.start], toks[name_token(toks, it)]
        if a.line != n.line:
            raise Unfit("the type and the name of %s are on different lines" % d.name)
        line = body[a.line - 1]
        if d.init is None:   # `T x;`: the module-level declaration gives the value
            for k in range(a.line, toks[it.end - 1].line + 1):
                body[k - 1] = ""
        else:
            body[a.line - 1] = line[:a.col - 1] + " " * (n.col - a.col) + line[n.col - 1:]
        type_text = " ".join(blanked(text, toks, it)[a.col - 1:n.col - 1].split())
        globals_.append("%s %s;" % (type_text, d.name))
    out = [(src[k - 1], k) for k in range(1, first)] + [("void main() {", None)]
    out += [(body[k - 1], k) for k in range(first, last + 1) if k not in moved]
    out += [("}", None)] + [(src[k - 1], k) for k in moved]
    out += [(src[k - 1], k) for k in range(last + 1, len(src) + 1)]
    out += [(g, None) for g in globals_]
    return "".join(t + "\n" for t, _ in out), [k for _, k in out]


# --------------------------------------------------------------------------- the case

TAG_SIZES = {0x01: 1, 0x11: 1, 0x12: 2, 0x13: 4, 0x14: 8, 0x15: 16, 0x16: 32, 0x17: 64, 0x18: 128,
             0x21: 1, 0x22: 2, 0x23: 4, 0x24: 8}


class _Reader:
    def __init__(self, data: bytes):
        self.d, self.at = data, 0

    def take(self, n: int) -> bytes:
        if n < 0 or n > len(self.d) - self.at:
            raise ValueError("the case is cut short at byte %d" % self.at)
        self.at += n
        return self.d[self.at - n:self.at]

    def u(self, n: int) -> int:
        return int.from_bytes(self.take(n), "little")

    def i(self, n: int) -> int:
        return int.from_bytes(self.take(n), "little", signed=True)

    def s32(self) -> bytes:
        return self.take(self.u(4))

    def s64(self) -> bytes:
        return self.take(self.u(8))

    def flag(self) -> bool:
        b = self.u(1)
        if b > 1:
            raise ValueError("presence byte %d at byte %d" % (b, self.at - 1))
        return b == 1

    def size(self) -> int:
        """The bytes of a value of the descriptor read here (SPEC-01 11.1)."""
        tag = self.u(1)
        if tag in TAG_SIZES:
            return TAG_SIZES[tag]
        if tag == 0x31:      # fixed point: its storage tag and fraction bits
            storage, _frac = self.u(1), self.u(2)
            if storage not in TAG_SIZES:
                raise ValueError("fixed-point storage tag %#x" % storage)
            return TAG_SIZES[storage]
        if tag == 0x51:      # array: the element's descriptor, the rank, and the extents
            size = self.size()
            for _ in range(self.u(1)):
                size *= self.i(8)
            return size
        raise ValueError("value tag %#x" % tag)

    def tagged(self) -> bytes:
        start = self.at
        self.take(self.size())
        return self.d[start:self.at]

    def code(self):
        for _ in range(self.u(4)):
            self.s32()
            self.s64()
            if self.flag():
                self.s64()


def read_case(data: bytes) -> list:
    """The events of a case (CINT-CASE-1; the recording layout of rt/cint_state.c):
    ("entry", seq), ("effect", seq, name, bytes), ("outcome", seq, status, fault record or None,
    error or None), and ("end", reason), in order; the others by kind name alone."""
    r = _Reader(data)
    if r.s32() != b"cint-core-1/case/v1":
        raise ValueError("not a case")
    r.s32(), r.s32(), r.s32(), r.take(32)
    if r.flag():
        r.take(32)
    r.u(1), r.u(8), r.u(1)
    r.code()
    r.s64()
    events, seq = [], None
    while True:
        kind = r.u(1)
        if kind == 1:
            seq = r.u(8)
            r.u(1), r.s32(), r.s32()
            for _ in range(r.u(4)):
                r.tagged()
            r.i(8), r.i(8), r.i(8)
            if r.u(4) != 0:
                raise ValueError("an entry with input records")
            events.append(("entry", seq))
        elif kind == 2:
            r.i(8), r.u(4), r.u(4)
            name = r.s32().decode("ascii")
            r.i(4)
            events.append(("effect", seq, name, r.s64()))
        elif kind == 3:
            status = r.i(4)
            fault = r.s32() if r.flag() else None
            r.i(8)
            if r.flag():
                r.tagged()
            error = (r.s32().decode("ascii"), r.u(8)) if r.flag() else None
            if r.u(4) != 0:
                raise ValueError("an outcome with output records")
            if r.flag():
                r.take(32)
            r.i(8)
            events.append(("outcome", seq, status, fault, error))
        elif kind == 4:
            r.take(32)
            events.append(("clear",))
        elif kind == 5:
            r.take(32)
            events.append(("checkpoint",))
        elif kind == 6:
            r.take(32), r.u(8), r.u(8), r.u(8), r.s64()
            events.append(("restore",))
        elif kind == 7:
            r.take(32)
            if r.flag():
                r.take(32)
            r.code()
            events.append(("revision",))
        elif kind == 8:
            reason = r.u(1)
            r.take(32), r.u(8)
            events.append(("end", reason))
            break
        elif kind == 9:
            r.i(8), r.take(32)
            events.append(("dispatch",))
        else:
            raise ValueError("event kind %d at byte %d" % (kind, r.at - 1))
    if r.at != len(data):
        raise ValueError("%d bytes after the end event" % (len(data) - r.at))
    return events


# --------------------------------------------------------------------------- the session

class Session:
    """A workbench session driven entry by entry: each step waits for the next prompt."""

    def __init__(self, argv, cwd, env):
        self.proc = subprocess.Popen([str(a) for a in argv], cwd=cwd, env=env, stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.out, self.err, self.ready = bytearray(), bytearray(), threading.Condition()
        self.pumps = [threading.Thread(target=self._pump, args=(s, b), daemon=True)
                      for s, b in ((self.proc.stdout, self.out), (self.proc.stderr, self.err))]
        for t in self.pumps:
            t.start()
        self.seen = 0
        self.number, self.banner = self._prompt()

    def _pump(self, stream, into):
        for chunk in iter(lambda: stream.read1(4096), b""):
            with self.ready:
                into.extend(chunk)
                self.ready.notify_all()
        with self.ready:
            self.ready.notify_all()

    def _prompt(self):
        """The number of the next prompt and what the session wrote before it."""
        with self.ready:
            found = self.ready.wait_for(lambda: PROMPT_RE.search(self.out, self.seen) is not None
                                        or self.proc.poll() is not None, timeout=TIMEOUT)
            m = PROMPT_RE.search(self.out, self.seen)
            if not found or m is None:
                raise Unfit("the session wrote no prompt; it wrote %r" % bytes(self.out[self.seen:])[-600:])
            text = bytes(self.out[self.seen:m.start()])
            self.seen = m.end()
        return int(m.group(1)), text.decode("utf-8", "replace")

    def step(self, lines):
        """Submits lines; returns the number the entry had and what the session wrote."""
        number = self.number
        self.proc.stdin.write("".join(line + "\n" for line in lines).encode("utf-8"))
        self.proc.stdin.flush()
        self.number, text = self._prompt()
        return number, text

    def close(self):
        """`:quit --yes`; the exit status and what the session wrote to stderr."""
        try:
            self.proc.stdin.write(b":quit --yes\n")
            self.proc.stdin.close()
            status = self.proc.wait(timeout=TIMEOUT)
        finally:
            if self.proc.poll() is None:
                self.proc.kill()
                self.proc.wait()
        for t in self.pumps:
            t.join(timeout=10)
        self.proc.stdout.close()
        self.proc.stderr.close()
        return status, bytes(self.err).decode("utf-8", "replace")


def report_positions(text: str):
    """The `-->` position and the `called from:` positions of the fault report in text."""
    m = re.search(r"(?m)^fault\[\w+\]: [^\n]*\n  --> ([^\n]*)$", text)
    if m is None:
        return None, []
    rest = text[m.end():]
    return m.group(1), re.findall(r"(?m)^   = called from: ([^\n]*)$", rest.split("   = noncanonical:")[0])


def inspected(text: str):
    """`TYPE VALUE` of an `:inspect` of a scalar, None for another value, or the report."""
    m = re.match(r"\A\S+ : (\S+)\n  exact   (\S+)\n\Z", text)
    if m:
        return m.group(1) + " " + m.group(2)
    if re.match(r"\A\S+ : ", text):
        return None
    return "not inspected: " + " ".join(text.split())[:200]


def run_session(workbench, cint, root, entries, work: pathlib.Path, env):
    """Feeds entries to a session until one faults or is refused; then :inspect, :save-case.
    Returns a dict of what the session did."""
    out = {"fed": [], "stop": None, "report": None, "values": {}, "stderr": "", "status": None}
    sess = Session([workbench, "--cint", cint, "--root", root, "--out", work / "session"], work, env)
    try:
        for k, e in enumerate(entries):
            number, text = sess.step(e.lines)
            out["fed"].append((number, k))
            if "   = workbench-position: entry " in text:
                out["stop"], out["report"] = "fault", text
                break
            if re.search(r"(?m)^(error\[C\d{4}\]|workbench: )", text):
                out["stop"], out["report"] = "refused", text
                break
        for k in [k for _, k in out["fed"]]:
            for name in entries[k].names:
                out["values"][name] = inspected(sess.step([":inspect " + name])[1])
        _, said = sess.step([":save-case session.case"])   # under work, the session's directory
        out["case"] = (work / "session.case").read_bytes() if (work / "session.case").is_file() else None
        if out["case"] is None:
            out["case_error"] = " ".join(said.split())[:300]
    finally:
        out["status"], out["stderr"] = sess.close()
    return out


# --------------------------------------------------------------------------- the reference

def run_reference(cint, src: pathlib.Path, rel: str, rewritten: str, work: pathlib.Path, env, exe_suffix: str):
    """`cint run` of the rewritten script, with the modules it imports from src, and the
    program `cint build` makes of it run with its fault, fuel, and state destinations
    (cint_check.run_program_entry)."""
    root = work / "root"
    for m in cc.module_order(src, rel):
        (root / m).parent.mkdir(parents=True, exist_ok=True)
        if m == rel:
            (root / m).write_bytes(rewritten.encode("utf-8"))
        else:
            shutil.copyfile(src / m, root / m)
    ran = subprocess.run([str(cint), "run", "--root", str(root), "--cache", str(work / "run-cache"), rel],
                         cwd=work, env=env, capture_output=True, stdin=subprocess.DEVNULL, timeout=600)
    out = {"run_status": ran.returncode, "stdout": ran.stdout, "run_stderr": ran.stderr.decode("utf-8", "replace")}
    built = subprocess.run([str(cint), "build", "--root", str(root), "--out", str(work / "build"), rel],
                           cwd=work, env=env, capture_output=True, stdin=subprocess.DEVNULL, timeout=600)
    exe = work / "build" / (pathlib.PurePosixPath(rel).stem + exe_suffix)
    if built.returncode != 0 or not exe.is_file():
        said = " ".join(built.stderr.decode("utf-8", "replace").split())[:400]
        out["build_error"] = "cint build: exit %d: %s" % (built.returncode, said)
        return out
    dests = [work / "fault.bin", work / "fuel.bin", work / "state.txt"]
    while True:
        for f in dests:
            f.unlink(missing_ok=True)
        prog = subprocess.run([str(exe)] + [str(d) for d in dests], cwd=work, env=env, capture_output=True,
                              stdin=subprocess.DEVNULL, timeout=600)
        if prog.returncode != 4 or len(dests) == 2:   # no module state: 2 destinations
            break
        dests = dests[:2]
    out["program_status"], out["program_stdout"] = prog.returncode, prog.stdout
    fault = work / "fault.bin"
    out["fault"] = fault.read_bytes() if prog.returncode == 1 and fault.is_file() else None
    out["state"] = cc.state_lines(work / "state.txt")
    return out


# --------------------------------------------------------------------------- the comparison

def mapped(pos: str, rel: str, mapper) -> str:
    """A position `path:line:column` with the script's line and column for the path mapper
    knows, or the position as given."""
    m = POS_RE.match(pos)
    if m is None:
        return pos
    found = mapper(m.group(1), int(m.group(2)), int(m.group(3)))
    if found is not None:
        return "%s:%d:%d" % (rel, found[0], found[1])
    return "unmapped " + pos if m.group(1) == rel or ENTRY_RE.match(m.group(1)) else pos


def fault_lines(record: bytes, position: str, stack: list) -> list:
    """The CONF-11 `fault.*` lines of a record (format 2) with the given position and stack."""
    lines = decode_fault_record(record, RUN_TIME)[1].to_expect_lines(2)
    depth = sum(1 for line in lines if line.startswith("fault.stack "))
    if depth != len(stack):
        return lines + ["report stack positions %d, record %d" % (len(stack), depth)]
    out, k = [], 0
    for line in lines:
        if line.startswith("fault.position "):
            line = "fault.position " + position
        elif line.startswith("fault.stack "):
            line, k = "fault.stack " + stack[k], k + 1
        out.append(line)
    return out


def compare_script(rel: str, root: pathlib.Path, workbench, cint, work: pathlib.Path, env, exe_suffix: str) -> dict:
    """The comparison of one script: result `agree` or `differ`, with the differences, or
    `unfit` when the feeder or the rewrite cannot shape it."""
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    text = (root / rel).read_bytes().decode("utf-8")
    res = {"case": rel[:-3], "result": None, "differences": []}
    try:
        toks, items, _m = script_items(text, rel)
        entries = plan(text, toks, items)
        rewritten, line_map = rewrite(text, toks, items)
    except (Unfit, CompileError, Refused) as e:
        res.update(result="unfit", reason=ascii_text(str(e)))
        return res
    (work / "rewritten.ci").write_bytes(rewritten.encode("utf-8"))
    res["entries"] = len(entries)
    try:
        s = run_session(workbench, cint, root, entries, work, env)
    except Unfit as e:
        res.update(result="differ", differences=[ascii_text("session: " + str(e))])
        return res
    r = run_reference(cint, root, rel, rewritten, work / "reference", env, exe_suffix)
    diffs = []
    numbers = {number: k for number, k in s["fed"]}
    res["fed"] = len(s["fed"])
    res["stopped"] = s["stop"]

    def wb_map(path, line, col):
        m = ENTRY_RE.match(path)
        return entries[numbers[int(m.group(1))]].map(line, col) if m and int(m.group(1)) in numbers else None

    def ref_map(path, line, col):
        if path != rel or not 1 <= line <= len(line_map) or line_map[line - 1] is None:
            return None
        return line_map[line - 1], col

    def finish(extra=None):
        res["differences"] = [ascii_text(d) for d in diffs]
        res["result"] = "differ" if diffs else "agree"
        if extra:
            res.update(extra)
        return res

    if s["status"] != 0 or s["stderr"]:
        diffs.append("session: exit %d, stderr %r" % (s["status"], s["stderr"][-300:]))
    if s["stop"] == "refused":
        diffs.append("session refused entry %d: %s" % (s["fed"][-1][0], " ".join(s["report"].split())[:300]))
    if s.get("case") is None:
        diffs.append("session: no case: %s" % s.get("case_error"))
    if "build_error" in r or r["run_status"] not in OUTCOMES:
        diffs.append("reference: " + (r.get("build_error") or "cint run exit %d: %s" % (
            r["run_status"], " ".join(r["run_stderr"].split())[:300])))
        return finish({"values": "not compared: the reference did not run"})
    if (r["program_status"], r["program_stdout"]) != (r["run_status"], r["stdout"]):
        diffs.append("reference: cint run exit %d, %d bytes; the built program exit %d, %d bytes"
                     % (r["run_status"], len(r["stdout"]), r["program_status"], len(r["program_stdout"])))
    res["stdout"] = {"bytes": len(r["stdout"]), "sha256": cc.sha256(r["stdout"])}
    res["outcome"] = OUTCOMES[r["run_status"]]
    if s.get("case") is None:
        return finish()
    try:
        events = read_case(s["case"])
    except ValueError as e:
        diffs.append("session: the case cannot be read: %s" % e)
        return finish()
    printed = b"".join(e[3] for e in events if e[0] == "effect" and e[2] == "print")
    others = sorted({e[2] for e in events if e[0] == "effect" and e[2] != "print"})
    if others:
        diffs.append("session effects other than print: %s" % ", ".join(others))
    outcomes = [e for e in events if e[0] == "outcome"]
    faults = [e for e in outcomes if e[2] == 1]
    errors = [e for e in outcomes if e[4] is not None]
    wb_outcome = "fault" if faults else "error" if errors else "value"
    ref_outcome = res["outcome"]
    if printed != r["stdout"]:
        diffs.append("stdout: session %d bytes %s, cint run %d bytes %s" % (
            len(printed), cc.sha256(printed)[:16], len(r["stdout"]), cc.sha256(r["stdout"])[:16]))
    if wb_outcome != ref_outcome:
        diffs.append("outcome: session %s, cint run %s" % (wb_outcome, ref_outcome))
    if (s["stop"] == "fault") != bool(faults) or len(faults) > 1 or faults and outcomes[-1] is not faults[0]:
        diffs.append("session: the report and the case disagree on the fault")
    if faults and r.get("fault") is None and ref_outcome == "fault":
        diffs.append("reference: the built program wrote no fault record")
    elif faults and r.get("fault") is not None:
        position, stack = report_positions(s["report"])
        try:
            wb_lines = fault_lines(faults[0][3], mapped(position or "", rel, wb_map),
                                   [mapped(p, rel, wb_map) for p in stack])
            rec = decode_fault_record(r["fault"], RUN_TIME)[1]
            ref_lines = fault_lines(r["fault"], mapped(str(rec.position), rel, ref_map),
                                    [mapped(str(p), rel, ref_map) for p in rec.stack])
        except ValueError as e:
            diffs.append("fault: a record cannot be read: %s" % e)
        else:
            res["fault"] = ref_lines
            if wb_lines != ref_lines:
                diffs.append("fault: session %s; cint run %s" % (
                    [x for x in wb_lines if x not in ref_lines], [x for x in ref_lines if x not in wb_lines]))
    # values: each variable of an entry fed, against its state.global line
    module = cc.module_name(rel)
    state = {}
    for line in r.get("state") or []:
        parts = line.split(" ")
        if parts[1] == module:
            state[parts[2]] = " ".join(parts[3:])
    fed = {k for _, k in s["fed"]}
    compared, not_compared = [], []
    for k, e in enumerate(entries):
        names = e.names if e.kind != "defer" else [it.node.name for it in e.items if it.kind == "var"]
        for name in names:
            if e.kind == "defer":
                not_compared.append([name, "declared after a top-level defer, a local of that entry"])
            elif k not in fed:
                not_compared.append([name, "its entry was not reached"])
            elif name not in state:
                not_compared.append([name, "no state.global form for its type (CONF-11 rule 10)"])
            else:
                compared.append([name, state[name]])
                if s["values"].get(name) != state[name]:
                    diffs.append("value of %s: session %s, cint run %s" % (name, s["values"].get(name), state[name]))
    return finish({"values": {"compared": compared, "not_compared": not_compared}})


# --------------------------------------------------------------------------- the executed set

def candidates(only) -> tuple:
    """The scripts among the conformance cases (in category order, then name), and the cases
    with no named entry that cint_ref cannot parse, with the reason."""
    held = gen_expect.read_held()
    scripts, unparsed = [], []
    for cat in gen_expect.CATEGORIES:
        for ci in sorted((CONF / cat).glob("*.ci")):
            rel = ci.relative_to(CONF).as_posix()
            if not cc.selected(rel[:-3], only) or rel[:-3] in held or not ci.with_suffix(".expect").is_file():
                continue
            text = ci.read_bytes().decode("utf-8", "replace")
            if gen_expect.header(text)["entries"] != [None]:
                continue
            try:
                m = P.parse_module(text, rel)
            except Exception as e:   # noqa: BLE001  (a compile-error case)
                unparsed.append((rel, "%s: %s" % (type(e).__name__, str(e)[:200])))
                continue
            if any(not isinstance(d, P.VarDecl) for d in m.statements):   # cint_ref's test (check.py)
                scripts.append(rel)
    return scripts, unparsed


def executed(rel: str, cint, interp, work: pathlib.Path, env):
    """None when cint-interp runs the script, else why not: B1's diagnostic, or the
    interpreter's `unsupported:` reason."""
    code, err, outset = cc.run_b1(cint, CONF, cc.module_order(CONF, rel), work / "emit" / "out", env=env)
    if outset is None:
        return "B1: " + " ".join(cc.b1_diagnostic(err))[:200]
    dests = [work / "fault.bin", work / "fuel.bin", work / "state.txt"]
    while True:
        r = subprocess.run([str(interp), "run", str(outset[0])] + [str(d) for d in dests], cwd=work, env=env,
                           capture_output=True, stdin=subprocess.DEVNULL, timeout=600)
        if r.returncode != 4 or len(dests) == 2:
            break
        dests = dests[:2]
    m = re.search(r"unsupported: ([^\n]*)", r.stderr.decode("utf-8", "replace"))
    return m.group(1).strip() if r.returncode == 3 and m else None


def build(tc, out: pathlib.Path, workbench):
    """cint-interp, and cint-workbench unless given (test_workbench.py's recipe)."""
    rt = out / "rt"
    rt.mkdir(parents=True, exist_ok=True)
    rt_obj = tc.compile([cc.RT / "cint_rt.c"], rt, "runtime")[0]
    interp = cc.build_interp(tc, {"rt_obj": rt_obj}, out)
    if workbench:
        return interp, pathlib.Path(workbench).resolve()
    import importlib.util
    spec = importlib.util.spec_from_file_location("test_workbench", ROOT / "workbench" / "tests" / "test_workbench.py")
    twb = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(twb)
    wtc = cc.Toolchain(tc.leg, tc.opt, "portable", False)
    inc = "/I" if tc.leg == "msvc" else "-I"
    wtc.flags = wtc.flags + [inc + str(twb.WB), inc + str(ROOT / "cli"), inc + str(ROOT / "interp")]
    proc = ROOT / "cli" / ("cint_proc_win.c" if tc.leg == "msvc" else "cint_proc_posix.c")
    wdir = out / "workbench"
    wdir.mkdir(parents=True, exist_ok=True)
    lib = wtc.compile([twb.WB / n for n in twb.LIBRARY] + [proc] + twb.LINKED, wdir, "workbench library")
    exe = wtc.link_exe(wtc.compile([twb.WB / "cint_workbench.c"], wdir, "cint-workbench") + lib,
                       wdir / "cint-workbench", "cint-workbench")
    return interp, exe


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--leg", choices=["msvc", "gcc", "clang", "apple-clang"], required=True)
    ap.add_argument("--cint", metavar="PATH", help="B1 (default: bootstrapped from this tree)")
    ap.add_argument("--workbench", metavar="PATH", help="cint-workbench (default: built from this tree)")
    ap.add_argument("--only", action="append", default=[], help="scripts whose name contains TEXT")
    ap.add_argument("--out", help="build directory (default <build>/wbmode/<leg>)")
    ap.add_argument("--receipt", metavar="PATH")
    ap.add_argument("--no-receipt", action="store_true")
    ns = ap.parse_args(argv)
    started = time.monotonic()
    out = pathlib.Path(ns.out).resolve() if ns.out else cc.build_root() / "wbmode" / ns.leg
    out.mkdir(parents=True, exist_ok=True)
    try:
        tc = cc.Toolchain(ns.leg, "2", "portable", False)
        b1 = cc.b1_tool(ns.leg, ns.cint, out / "b1")
        interp, workbench = build(tc, out / "tools", ns.workbench)
    except (cc.BuildError, OSError, subprocess.CalledProcessError) as e:
        print("cint_wbmode: %s" % e, file=sys.stderr)
        return 2
    env = dict(tc.env, **SAN_ENV)
    scripts, unparsed = candidates(ns.only)
    results, not_executed = [], [{"case": rel[:-3], "reason": why} for rel, why in unparsed]
    for rel in scripts:
        why = executed(rel, b1["cint"], interp, out / "set" / rel[:-3], env)
        if why is not None:
            not_executed.append({"case": rel[:-3], "reason": why})
            continue
        res = compare_script(rel, CONF, workbench, b1["cint"], out / "scripts" / rel[:-3], env, tc.exe)
        print("%-7s %s%s" % (res["result"], res["case"], "".join("\n        " + d for d in res["differences"])))
        results.append(res)
    agree = sum(1 for r in results if r["result"] == "agree")
    suite, suite_files = cc.tree_digest("conformance/", "conformance/")
    identity = {
        "b1_compiler_source_identity": b1["identity"],
        "backend": {"reference": "cpu-c17 (cint run)", "session": "cpu-sir-interp"},
        "runtime_contract_version": cc.runtime_contract(),
        "runtime_header_sha256": cc.runtime_header_digest(),
        "schema": SCHEMA,
        "scope": "only" if ns.only else "full",
        "suite_files": suite_files,
        "suite_sha256": suite,
        "tool_sha256": cc.files_digest([pathlib.Path(__file__).resolve()]),
        "workbench_sha256": cc.files_digest(sorted((ROOT / "workbench").glob("*.c")) +
                                            sorted((ROOT / "workbench").glob("*.h"))),
    }
    outcome = {
        "agree": agree,
        "executed": len(results),
        "first_difference": next((r for r in results if r["result"] != "agree"), None),
        "not_executed": not_executed,
        "pass": bool(results) and agree == len(results),
        "scripts": results,
    }
    observations = {
        "b1": {"cint_sha256": b1["sha256"], "how": b1["how"]},
        "c0": tc.describe(),
        "date_utc": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d"),
        "elapsed_s": int(time.monotonic() - started),
        "host": {"arch": platform.machine().lower(), "os": cc.host_os()},
        "leg": ns.leg,
        "python": platform.python_version(),
        "workbench_executable": "given" if ns.workbench else "built",
    }
    observations["receipt_identity_sha256"] = cc.sha256(cc.canonical({"identity": identity, "outcome": outcome}))
    if not ns.no_receipt:
        path = pathlib.Path(ns.receipt) if ns.receipt else out / "receipt.json"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(json.dumps({"identity": identity, "observations": observations, "outcome": outcome},
                                    indent=2, sort_keys=True).encode("ascii") + b"\n")
    print("wbmode %s: %d scripts executed, %d agree, %d not executed; receipt identity %s"
          % (ns.leg, len(results), agree, len(not_executed), observations["receipt_identity_sha256"][:16]))
    return 0 if outcome["pass"] else 1


if __name__ == "__main__":
    sys.exit(main())
