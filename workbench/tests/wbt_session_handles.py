"""Pool and handle declarations at the prompt (box 14; box 13 module-level Handle globals, OQ-242 A),
and the pool carried from one entry to the next (box 13, decision 2026-10-08 on OQ-243).

Run by workbench/tests/test_workbench.py for each configuration it builds (its units()); with
--cint PATH, two transcripts against B1. A declaration whose type has a parenthesized group, such
as `Pool(I64, 2)` or `Handle(I64)`, is a declaration of the session, not a statement of its entry
(wb_session.c classify()). The session shows neither as a value, since a handle is no integer.
Each later entry is a new revision of the session's program, and the change of revision carries
the pool, its slot and the handle (SPEC-03 H-19b; rt/OPEN.md RT-OQ-46), so `counts[h]` reads 42.
The session is saved, opened in a new process, and changed again; :undo goes back to the state
before the last entry.
"""
from __future__ import annotations

import pathlib
import tempfile

ENTRIES = """\
Pool(I64, 2) counts;
Handle(I64) h = counts.insert(41);
counts[h] += 1;
counts[h]
I64 n = counts[h];
:log
:save saved
:quit
"""
SAID = """\
cint workbench   profile cint-core-1   backend cpu-sir-interp   new session
ci[1]> Pool(I64, 2) counts;
counts : not shown by the session (it shows integers, Bool, and arrays of them)
ci[2]> Handle(I64) h = counts.insert(41);
h : not shown by the session (it shows integers, Bool, and arrays of them)
ci[3]> counts[h] += 1;
ci[4]> counts[h]
= 42 : I64
ci[5]> I64 n = counts[h];
n = 42 : I64
ci[6]> :log
entry 1     declared       Pool(I64, 2) counts;
entry 2     declared       Handle(I64) h = counts.insert(41);
entry 3     done           counts[h] += 1;
entry 4     value          counts[h]
entry 5     declared       I64 n = counts[h];
ci[6]> :save saved
saved session to saved   entries 5   checkpoints 0   objects <n>
ci[6]> :quit
"""
REOPEN_ENTRIES = """\
:open saved
counts[h] += 5;
n + counts[h]
:undo
counts[h]
:quit
:quit
"""
REOPEN_SAID = """\
cint workbench   profile cint-core-1   backend cpu-sir-interp   new session
ci[1]> :open saved
opened saved   entries 5   checkpoints 0   branch main
ci[6]> counts[h] += 5;
ci[7]> n + counts[h]
= 89 : I64
ci[8]> :undo
saved current state as auto-1
back to the state before entry 7
ci[8]> counts[h]
= 47 : I64
ci[9]> :quit
the session has changes since it was last saved; :save PATH keeps them, and :quit again ends it
ci[9]> :quit
"""


def check(case, wb, exes) -> int:
    """The transcripts against B1; none without --cint."""
    if wb.ARGS.cint is None:
        return 0
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp)
        (work / "root").mkdir()
        argv = [exes["cint-workbench"], "--cint", pathlib.Path(wb.ARGS.cint).resolve(), "--root", work / "root",
                "--echo"]
        for entries, said, out in ((ENTRIES, SAID, "session-1"), (REOPEN_ENTRIES, REOPEN_SAID, "session-2")):
            result = wb.run(argv + ["--out", work / out], cwd=work, entries=entries)
            case.assertClean(result, "handles " + out)
            case.assertEqual((result.returncode, result.stderr), (0, b""))
            case.assertRegex(result.stdout.decode("utf-8"), wb.transcript(said))
    return 2
