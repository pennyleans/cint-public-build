"""Module-level structs declared at the prompt (box 15, decision 2026-10-09 on OQ-260; rt/OPEN.md
RT-OQ-51).

Run by workbench/tests/test_workbench.py for each configuration it builds (its units()); with
--cint PATH, a transcript against B1. The session shows no struct as a value, only its fields;
`:diff` compares a struct's type, then its payload byte by byte, and names the first payload byte
that differs; a redeclaration of the same type keeps the value, and a change of the struct's type
gives it its new initial value (SPEC-03 H-19b), which `:diff` reports as retyped; and `:restore`
brings the struct back.
"""
from __future__ import annotations

import pathlib
import tempfile

ENTRIES = """\
struct P { I64 x; Bool on; I64[2] ys; }
P p;
p.x = 1;
p.ys[1] = 7;
p.ys[1]
:checkpoint one
p.x = 9;
p.on = true;
:diff one
:diff one p
P p;
p.x
struct P { I64 x; Bool on; I64[3] ys; }
p.x
:diff one
:restore one
p.x * 100 + p.ys[1]
:quit --yes
"""
SAID = """\
cint workbench   profile cint-core-1   backend cpu-sir-interp   new session
ci[1]> struct P { I64 x; Bool on; I64[2] ys; }
P   revision session@r1
ci[2]> P p;
p : not shown by the session (it shows integers, Bool, and arrays of them)
ci[3]> p.x = 1;
ci[4]> p.ys[1] = 7;
ci[5]> p.ys[1]
= 7 : I64
ci[6]> :checkpoint one
checkpoint one   #1   bindings 1   revisions: session@r2
ci[6]> p.x = 9;
ci[7]> p.on = true;
ci[8]> :diff one
binding   status      differing   first canonical difference
p         differs     2           payload byte 0: 09 here, 01 in one
ci[8]> :diff one p
binding   status      differing   first canonical difference
p         differs     2           payload byte 0: 09 here, 01 in one
ci[8]> P p;
p : not shown by the session (it shows integers, Bool, and arrays of them)
ci[9]> p.x
= 9 : I64
ci[10]> struct P { I64 x; Bool on; I64[3] ys; }
P   revision session@r4
ci[11]> p.x
= 0 : I64
ci[12]> :diff one
binding   status      differing   first canonical difference
p         retyped     1           session.P here, session.P in one
ci[12]> :restore one
saved current state as auto-1
restored one   #1   P is revision session@r1   p is revision session@r2
ci[12]> p.x * 100 + p.ys[1]
= 107 : I64
ci[13]> :quit --yes
"""


def check(case, wb, exes) -> int:
    """The transcript against B1; none without --cint."""
    if wb.ARGS.cint is None:
        return 0
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp)
        (work / "root").mkdir()
        argv = [exes["cint-workbench"], "--cint", pathlib.Path(wb.ARGS.cint).resolve(), "--root", work / "root",
                "--echo", "--out", work / "session"]
        result = wb.run(argv, cwd=work, entries=ENTRIES)
        case.assertClean(result, "structs")
        case.assertEqual((result.returncode, result.stderr), (0, b""))
        case.assertRegex(result.stdout.decode("utf-8"), wb.transcript(SAID))
    return 1
