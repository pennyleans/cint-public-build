"""The session's recording and replay (box 14 units 3a and 3b; sections 5.4 and 10 of the box 14 note).

Run by workbench/tests/test_workbench.py for each configuration it builds (its units()); with
--cint PATH, a transcript of the recording against B1: :trace of a fault and of an entry that
calls a function, :replay with a watch added after the fact, :effects, and :goto, with the
recording saved, opened in a new process, and refused when a segment line miscounts its
boundaries; then the first segment of that recording opened read-only in a debug session
(SPEC-06 10.4), a cut recording refused, and the backward scan and bisection of SPEC-06 10.1;
then :save-case, and the case in a debug session (SPEC-06 9.2).
"""
from __future__ import annotations

import pathlib
import re
import shutil
import tempfile

# Unit 3a against B1: :trace of a fault and of an entry that calls a function, :replay with a
# watch added after the fact, :effects, :goto, and the recording saved; then the saved session in
# a new process.
REPLAY_ENTRIES = """\
I64 x = 1;
export I64 bump(I64 n) {
    x = x + n;
    return x * 2;
}
:checkpoint start
:run bump(2)
x = bump(1) - 3;
"x is set\\n";
:run bump(9223372036854775807)
:trace
:trace 4
:watch x
:replay start
:effects
:goto fault
:goto 4
x
:run bump(1) 2
:branches
:save saved
:quit
"""
TRACE_6 = """\
entry 6   :run bump(9223372036854775807)   evaluator cpu-sir-interp, by replay
  ord  position           operation              operands                 value
    0  entry 2:2:11       add.checked.i64        5, 9223372036854775807   E_OVERFLOW
"""
EFFECTS_TO_4 = """\
new context
revisions: session@r1
entry 1   I64 x = 1;
  done   fuel 1   state <h>
revisions: session@r2
entry 3   :run bump(2)
  value I64 6   fuel 1   state <h>
entry 4   x = bump(1) - 3;
  done   fuel 2   state <h>
"""
REPLAY_SAID = """\
cint workbench   profile cint-core-1   backend cpu-sir-interp   new session
ci[1]> I64 x = 1;
x = 1 : I64
ci[2]> export I64 bump(I64 n) {
..>     x = x + n;
..>     return x * 2;
..> }
bump : (I64) -> I64   revision session@r2
ci[3]> :checkpoint start
checkpoint start   #1   bindings 1   revisions: session@r2
ci[3]> :run bump(2)
= 6 : I64
ci[4]> x = bump(1) - 3;
ci[5]> "x is set\\n";
x is set
ci[6]> :run bump(9223372036854775807)
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> entry 2:2:11
   |
 2 |     x = x + n;
   |           ^ add.checked.i64
   |
   = operation: add.checked.i64
   = operand-count: 2
   = operand: I64 5
   = operand: I64 9223372036854775807
   = exact:   9223372036854775812
   = limit:   I64 9223372036854775807
   = revision: <revision>
   = source-map: none
   = address: none
   = stack-depth: 0
   = fuel-consumed: 1 (not part of the fault record)
   = state: no store completed in this entry; context reset
   = noncanonical: not part of the fault record
   = workbench-position: entry 6
   = site: bump#2
   = backend: cpu-sir-interp
ci[7]> :trace
""" + TRACE_6 + """\
ci[7]> :trace 4
entry 4   x = bump(1) - 3;   evaluator cpu-sir-interp, by replay
  ord  position           operation              operands                 value
    0  entry 4:1:5        call                   1
    1  entry 2:2:11       add.checked.i64        3, 1                     4
    2  entry 2:3:14       mul.checked.i64        4, 2                     8
    -  entry 4:1:5        return                                          8
    3  entry 4:1:13       sub.checked.i64        8, 3                     5
ci[7]> :watch x
watch 1   x = 5 : I64
ci[7]> :replay start
replaying 4 entries from start to the current state (entry 3 to entry 6), comparing prints and outcomes with the \
recording
watch 1   entry 3   x = 3
watch 1   entry 4   x = 5
entry 6: E_OVERFLOW reproduced, its fault record equal to the recorded one
replay matches the recording at 4 boundaries
ci[7]> :effects
""" + EFFECTS_TO_4 + """\
entry 5   "x is set\\n";
  print "x is set\\n"
  done   fuel 1   state <h>
entry 6   :run bump(9223372036854775807)
  fault E_OVERFLOW   fuel 1   state <h>
entries 5   prints 1   context resets 0   changes of revision set 1
ci[7]> :goto fault
the current state is the state at the end of entry 6
ci[7]> :goto 4
saved current state as auto-1
at the end of entry 4, reached by replay
later entries go on branch entry-4, from auto-2
ci[7]> x
= 5 : I64
ci[8]> :run bump(1) 2
= 12 : I64
watch 1   entry 8   x = 6
= 14 : I64
watch 1   entry 9   x = 7
ci[10]> :branches
  main   checkpoints 3   head auto-2
* entry-4   from auto-2   checkpoints 0   head auto-2
#1  start   main   after entry 2
#2  auto-1   main   after entry 6   follows start
#3  auto-2   main   after entry 6   follows start
ci[10]> :save saved
saved session to saved   entries 9   checkpoints 3   objects <n>
ci[10]> :quit
"""
REPLAY_REOPEN_ENTRIES = """\
:open miscounted
:open saved
:effects
:goto 3
:trace 6
:replay start
:quit --yes
"""
REPLAY_REOPEN_SAID = """\
cint workbench   profile cint-core-1   backend cpu-sir-interp   new session
ci[1]> :open miscounted
:open: miscounted/session.manifest is not a CINT-SESSION-1 session this workbench reads (line <n>); nothing \
changed
ci[1]> :open saved
opened saved   entries 9   checkpoints 3   branch entry-4
ci[10]> :effects
""" + EFFECTS_TO_4 + """\
:goto: the state at the end of entry 4
entry 7   x
  value I64 5   fuel 1   state <h>
entry 8   :run bump(1) 2
  value I64 12   fuel 1   state <h>
entry 9   :run bump(1) 2
  value I64 14   fuel 1   state <h>
entries 6   prints 0   context resets 0   changes of revision set 1
ci[10]> :goto 3
saved current state as auto-3
at the end of entry 3, reached by replay
later entries go on branch entry-3, from auto-4
ci[10]> :trace 6
""" + TRACE_6 + """\
ci[10]> :replay start
replaying 1 entry from start to the current state (entry 3), comparing prints and outcomes with the recording
watch 1   entry 3   x = 3
replay matches the recording at 1 boundary
ci[10]> :quit --yes
"""

# Unit 3b: the first segment of the recording above (entries 1 and 3 to 6) in a debug session.
DEBUG_ENTRIES = """\
:log
:inspect x /hex
:type x
:inspect y
:fault
:trace fault
:goto 2
:inspect x
:effects
:replay
:replay start
:replay later
:replay --changed x
:replay --when x > 3
x = 2;
:watch x
:save again
:save-case first.cicase
:quit
"""
DEBUG_SAID = """\
cint workbench   profile cint-core-1   backend cpu-sir-interp   debug first.cirec (read-only)
recording: entries 5   boundaries 5
at the end of entry 5, reached by replay
ci[6]> :log
entry 1     done           session.ci: wb_entry()
entry 2     value          session.ci: bump(I64 2)
entry 3     done           session.ci: wb_entry()
entry 4     done           session.ci: wb_entry()
entry 5     fault          session.ci: bump(I64 9223372036854775807)
ci[6]> :inspect x /hex
session.x : I64   (signed, 64 bits)
  exact   5
  hex     0x0000000000000005
ci[6]> :type x
session.x : I64
ci[6]> :inspect y
no global y in the current state
  (a debug session reads globals: it has no source to compile an expression in)
ci[6]> :fault
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> session.ci:3:11
   = operation: add.checked.i64
   = operand-count: 2
   = operand: I64 5
   = operand: I64 9223372036854775807
   = exact:   9223372036854775812
   = limit:   I64 9223372036854775807
   = revision: <revision>
   = source-map: none
   = address: none
   = stack-depth: 0
   = state: the state the fault left, reached by replay (SPEC-01 IM-121)
   = noncanonical: not part of the fault record
   = workbench-position: entry 5
   = site: bump#2
   = backend: cpu-sir-interp
ci[6]> :trace fault
entry 5   session.ci: bump(I64 9223372036854775807)   evaluator cpu-sir-interp, by replay
  ord  position           operation              operands                 value
    0  session.ci:3:11    add.checked.i64        5, 9223372036854775807   E_OVERFLOW
ci[6]> :goto 2
at the end of entry 2, reached by replay
ci[6]> :inspect x
session.x : I64
  exact   3
ci[6]> :effects
new context
entry 1   session.ci: wb_entry()
  done   fuel 1   state <h>
entry 2   session.ci: bump(I64 2)
  value I64 6   fuel 1   state <h>
:goto: the state at the end of entry 2
entries 2   prints 0   context resets 0   changes of revision set 0
ci[6]> :replay
replaying 2 entries from the recording's start to the current state (entry 1 to entry 2), comparing prints and \
outcomes with the recording
replay matches the recording at 2 boundaries
ci[6]> :replay start
replaying 2 entries from the recording's start to the current state (entry 1 to entry 2), comparing prints and \
outcomes with the recording
replay matches the recording at 2 boundaries
ci[6]> :replay later
:replay in a debug session takes start, or nothing: the recording's start
ci[6]> :replay --changed x
session.x last changed at entry 2, from 1 to 3
backward scan: 2 boundaries replayed
ci[6]> :replay --when x > 3
--when compiles PRED in the session's sources, which a debug session does not have
ci[6]> x = 2;
a debug session is read-only: it takes commands, not entries
ci[6]> :watch x
:watch is not available in a debug session, which is read-only
ci[6]> :save again
:save is not available in a debug session, which is read-only
ci[6]> :save-case first.cicase
:save-case needs each module's source, which a case holds and a recording does not
ci[6]> :quit
"""
DEBUG_CUT_SAID = """\
cint workbench   profile cint-core-1   backend cpu-sir-interp   debug cut.cirec (read-only)
workbench: cut.cirec is not a complete recording (CINT-REC-1)
"""

# Unit 3b: the backward scan (--changed) and bisection (--when) of SPEC-06 10.1, over one
# segment and then over two, after :goto starts a branch.
SCAN_ENTRIES = """\
I64 x = 1;
I64 y = 0;
export I64 bump(I64 n) {
    x = x + n;
    return x;
}
:checkpoint start
:run bump(2)
y = 7;
:run bump(0)
:run bump(5)
y = y + 0;
:replay start --changed x
:replay start --changed y
:replay start --when x > 5
:replay start --when x > 100
:replay start --when x > 0
:replay start --when z > 1
:replay start --changed z
:replay start --oops
:goto 5
:run bump(10)
:run bump(0) 3
:replay start --changed x
:replay start --changed y
:replay start --when x >= 13
:replay start --when y == 7
:quit --yes
"""
SCAN_SAID = """\
cint workbench   profile cint-core-1   backend cpu-sir-interp   new session
ci[1]> I64 x = 1;
x = 1 : I64
ci[2]> I64 y = 0;
y = 0 : I64
ci[3]> export I64 bump(I64 n) {
..>     x = x + n;
..>     return x;
..> }
bump : (I64) -> I64   revision session@r3
ci[4]> :checkpoint start
checkpoint start   #1   bindings 2   revisions: session@r3
ci[4]> :run bump(2)
= 3 : I64
ci[5]> y = 7;
ci[6]> :run bump(0)
= 3 : I64
ci[7]> :run bump(5)
= 8 : I64
ci[8]> y = y + 0;
ci[9]> :replay start --changed x
session.x last changed at entry 7, from 3 to 8
backward scan: 7 boundaries replayed
ci[9]> :replay start --changed y
session.y last changed at entry 5, from 0 to 7
backward scan: 7 boundaries replayed
ci[9]> :replay start --when x > 5
x > 5 first holds at entry 7
bisection: 5 probes over 5 boundaries; PRED is taken as monotone
ci[9]> :replay start --when x > 100
x > 100 does not hold at the current state
ci[9]> :replay start --when x > 0
x > 0 holds already at checkpoint start
ci[9]> :replay start --when z > 1
error[C3005]: undefined or unusable name
  --> when:1:1
   |
 1 | z > 1
   | ^
   |
ci[9]> :replay start --changed z
no global z in the current state
ci[9]> :replay start --oops
:replay takes NAME, then --changed BINDING or --when PRED, as you need them
ci[9]> :goto 5
saved current state as auto-1
at the end of entry 5, reached by replay
later entries go on branch entry-5, from auto-2
ci[9]> :run bump(10)
= 13 : I64
ci[10]> :run bump(0) 3
= 13 : I64
= 13 : I64
= 13 : I64
ci[13]> :replay start --changed x
session.x last changed at entry 9, from 3 to 13
backward scan: 4 boundaries replayed
ci[13]> :replay start --changed y
session.y last changed at entry 5, from 0 to 7
backward scan: 8 boundaries replayed
ci[13]> :replay start --when x >= 13
x >= 13 first holds at entry 9
bisection: 5 probes over 6 boundaries; PRED is taken as monotone
ci[13]> :replay start --when y == 7
y == 7 first holds at entry 5
bisection: 5 probes over 6 boundaries; PRED is taken as monotone
ci[13]> :quit --yes
"""


# Unit 3b: :save-case writes the case of the entries since the checkpoint the state follows
# (SPEC-06 9.2); a debug session opens it and replays it, and its own :save-case of the whole
# case writes the same bytes.
CASE_ENTRIES = """\
I64 x = 1;
export I64 bump(I64 n) {
    x = x + n;
    return x * 2;
}
:save-case
:save-case start.cicase
:checkpoint baseline
:save-case early.cicase
:run bump(2)
"hello\\n";
:run bump(9223372036854775807)
:save-case overflow.cicase
:quit --yes
"""
CASE_NOTE = "note: the case includes every recorded observe result verbatim; this case has none\n"
CASE_SAID = """\
cint workbench   profile cint-core-1   backend cpu-sir-interp   new session
ci[1]> I64 x = 1;
x = 1 : I64
ci[2]> export I64 bump(I64 n) {
..>     x = x + n;
..>     return x * 2;
..> }
bump : (I64) -> I64   revision session@r2
ci[3]> :save-case
:save-case takes a PATH, the file of the case
ci[3]> :save-case start.cicase
""" + CASE_NOTE + """\
case start.cicase   from the recording's start   entry 1   effects 0   density entry
ci[3]> :checkpoint baseline
checkpoint baseline   #1   bindings 1   revisions: session@r2
ci[3]> :save-case early.cicase
no entry ran after checkpoint baseline, so a case would hold none
ci[3]> :run bump(2)
= 6 : I64
ci[4]> "hello\\n";
hello
ci[5]> :run bump(9223372036854775807)
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> entry 2:2:11
   |
 2 |     x = x + n;
   |           ^ add.checked.i64
   |
   = operation: add.checked.i64
   = operand-count: 2
   = operand: I64 3
   = operand: I64 9223372036854775807
   = exact:   9223372036854775810
   = limit:   I64 9223372036854775807
   = revision: <revision>
   = source-map: none
   = address: none
   = stack-depth: 0
   = fuel-consumed: 1 (not part of the fault record)
   = state: no store completed in this entry; context reset
   = noncanonical: not part of the fault record
   = workbench-position: entry 5
   = site: bump#2
   = backend: cpu-sir-interp
ci[6]> :save-case overflow.cicase
""" + CASE_NOTE + """\
case overflow.cicase   checkpoint baseline   entries 3..5   effects 1 (print)   density entry
ci[6]> :quit --yes
"""
CASE_DEBUG_ENTRIES = """\
:log
:inspect x
:save-case again.cicase
:goto 2
:save-case two.cicase
:quit
"""
CASE_DEBUG_SAID = """\
cint workbench   profile cint-core-1   backend cpu-sir-interp   debug overflow.cicase (read-only)
case: entries 3   boundaries 3
at the end of entry 3, reached by replay
ci[4]> :log
entry 1     value          session.ci: bump(I64 2)
entry 2     done           session.ci: wb_entry()
entry 3     fault          session.ci: bump(I64 9223372036854775807)
ci[4]> :inspect x
session.x : I64
  exact   3
ci[4]> :save-case again.cicase
""" + CASE_NOTE + """\
case again.cicase   from the recording's start   entries 1..3   effects 1 (print)   density entry
ci[4]> :goto 2
at the end of entry 2, reached by replay
ci[4]> :save-case two.cicase
""" + CASE_NOTE + """\
case two.cicase   from the recording's start   entries 1..2   effects 1 (print)   density entry
ci[4]> :quit
"""

def check(case, wb, exes) -> int:
    """Unit 3a: the recording, through :trace, :replay, :effects, and :goto, kept by :save and read
    by :open in a new process; a manifest whose segment line miscounts its boundaries is refused."""
    if wb.ARGS.cint is None:
        return 0
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp) / "replay"
        (work / "root").mkdir(parents=True)
        argv = [exes["cint-workbench"], "--cint", pathlib.Path(wb.ARGS.cint).resolve(), "--root", work / "root",
                "--echo"]
        result = wb.run(argv + ["--out", work / "session-1"], cwd=work, entries=REPLAY_ENTRIES)
        case.assertClean(result, "replay")
        case.assertEqual((result.returncode, result.stderr), (0, b""))
        case.assertRegex(result.stdout.decode("utf-8"), wb.transcript(REPLAY_SAID))
        manifest = (work / "saved" / "session.manifest").read_text(encoding="utf-8")
        for kind in ("segment", "position", "bound", "tip"):
            case.assertRegex(manifest, "\n%s [^\n]+\n" % kind, kind)
        shutil.copytree(work / "saved", work / "miscounted")
        first = re.search(r"^segment - 0 0 5 ", manifest, re.M)
        case.assertIsNotNone(first, manifest)
        (work / "miscounted" / "session.manifest").write_text(
            manifest[:first.start()] + "segment - 0 0 4 " + manifest[first.end():], encoding="utf-8", newline="\n")
        result = wb.run(argv + ["--out", work / "session-2"], cwd=work, entries=REPLAY_REOPEN_ENTRIES)
        case.assertClean(result, "replay reopened")
        case.assertEqual((result.returncode, result.stderr), (0, b""))
        case.assertRegex(result.stdout.decode("utf-8"), wb.transcript(REPLAY_REOPEN_SAID))
        recording = re.search(r"^segment - 0 0 5 ([0-9a-f]{64})$", manifest, re.M).group(1)
        whole = (work / "saved" / "objects" / recording).read_bytes()
        (work / "first.cirec").write_bytes(whole)
        (work / "cut.cirec").write_bytes(whole[:len(whole) // 2])
        result = wb.run([exes["wb-debug-session"], "first.cirec", work / "debug-1"], cwd=work, entries=DEBUG_ENTRIES)
        case.assertClean(result, "debug")
        case.assertEqual((result.returncode, result.stderr), (0, b""))
        case.assertRegex(result.stdout.decode("utf-8"), wb.transcript(DEBUG_SAID))
        result = wb.run([exes["wb-debug-session"], "cut.cirec", work / "debug-2"], cwd=work, entries=":quit\n")
        case.assertClean(result, "debug cut")
        case.assertEqual((result.returncode, result.stdout.decode("utf-8"), result.stderr), (3, DEBUG_CUT_SAID, b""))
        result = wb.run(argv + ["--out", work / "session-3"], cwd=work, entries=SCAN_ENTRIES)
        case.assertClean(result, "scan")
        case.assertEqual((result.returncode, result.stderr), (0, b""))
        case.assertRegex(result.stdout.decode("utf-8"), wb.transcript(SCAN_SAID))
        result = wb.run(argv + ["--out", work / "session-4"], cwd=work, entries=CASE_ENTRIES)
        case.assertClean(result, "save-case")
        case.assertEqual((result.returncode, result.stderr), (0, b""))
        case.assertRegex(result.stdout.decode("utf-8"), wb.transcript(CASE_SAID))
        case.assertFalse((work / "early.cicase").exists())
        saved = (work / "overflow.cicase").read_bytes()
        case.assertEqual(saved[:23], b"\x13\x00\x00\x00cint-core-1/case/v1")
        result = wb.run([exes["wb-debug-session"], "overflow.cicase", work / "debug-3"], cwd=work,
                        entries=CASE_DEBUG_ENTRIES)
        case.assertClean(result, "debug a case")
        case.assertEqual((result.returncode, result.stderr), (0, b""))
        case.assertRegex(result.stdout.decode("utf-8"), wb.transcript(CASE_DEBUG_SAID))
        case.assertEqual((work / "again.cicase").read_bytes(), saved)   # made again: the same bytes
    return 6
