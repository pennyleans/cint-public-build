"""The session on cpu-c17 (box 14; sections 5.2 and 5.3 of the box 14 note, default BX14-08).

Run by workbench/tests/test_workbench.py for each configuration it builds (its units()); with
--cint PATH, a transcript against B1: entries and :run on cpu-c17 through the libraries of unit 5,
each revision built once; :run --check in both directions, whose entries cpu-sir-interp records; a
fault found on cpu-c17 alone, whose entry has no boundary; the refusals of :run's words; and the
backend kept by :save and brought back by :open in a new process.
"""
from __future__ import annotations

import pathlib
import tempfile

ENTRIES = """\
I64 x = 1;
export I64 bump(I64 n) {
    x = x + n;
    "bumped {x}\\n";
    return x * 2;
}
:watch x
:backend cpu-c17
:run bump(2)
x
:run bump(3) 2 --check cpu-sir-interp
:effects
:run bump(9223372036854775807)
:goto 7
:backend cpu-sir-interp
:run bump(1) 2 --check cpu-c17
:run bump(5) --check cpu-sir-interp
:run bump(5) --check gpu-cuda
:run bump(5) --device gpu-cuda
:run bump(5) 1 2
:backend cpu-c17
:save saved
:quit
"""
BUILT = "cpu-c17: revision <h> built as a library in <n> ms\n"
SAID = """\
cint workbench   profile cint-core-1   backend cpu-sir-interp   new session
ci[1]> I64 x = 1;
x = 1 : I64
ci[2]> export I64 bump(I64 n) {
..>     x = x + n;
..>     "bumped {x}\\n";
..>     return x * 2;
..> }
bump : (I64) -> I64   revision session@r2
ci[3]> :watch x
watch 1   x = 1 : I64
ci[3]> :backend cpu-c17
backend: cpu-c17; each revision is built as a library, entries on it are not recorded, and :inspect and the \
watches stay on cpu-sir-interp
ci[3]> :run bump(2)
""" + BUILT + """\
bumped 3
= 6 : I64
watch 1   entry 3   x = 3
ci[4]> x
""" + BUILT + """\
= 3 : I64
ci[5]> :run bump(3) 2 --check cpu-sir-interp
bumped 6
= 12 : I64
watch 1   entry 5   x = 6
bumped 9
= 18 : I64
watch 1   entry 6   x = 9
check: cpu-c17 and cpu-sir-interp agree at 2 boundaries
ci[7]> :effects
new context
revisions: session@r2
entry 5   :run bump(3) 2 --check cpu-sir-interp
  print "bumped 6\\n"
  value I64 12   fuel 1   state <h>
entry 6   :run bump(3) 2 --check cpu-sir-interp
  print "bumped 9\\n"
  value I64 18   fuel 1   state <h>
entries 2   prints 2   context resets 0   changes of revision set 0
ci[7]> :run bump(9223372036854775807)
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> entry 2:2:11
   |
 2 |     x = x + n;
   |           ^ add.checked.i64
   |
   = operation: add.checked.i64
   = operand-count: 2
   = operand: I64 9
   = operand: I64 9223372036854775807
   = exact:   9223372036854775816
   = limit:   I64 9223372036854775807
   = revision: <revision>
   = source-map: none
   = address: none
   = stack-depth: 0
   = fuel-consumed: 1 (not part of the fault record)
   = state: no store completed in this entry; context reset
   = noncanonical: not part of the fault record
   = workbench-position: entry 7
   = site: bump#2
   = backend: cpu-c17
ci[8]> :goto 7
entry 7 has no boundary in the recording: it ran no code, ran on cpu-c17 alone, or ran while the state was not \
recorded
ci[8]> :backend cpu-sir-interp
backend: cpu-sir-interp
ci[8]> :run bump(1) 2 --check cpu-c17
bumped 10
= 20 : I64
watch 1   entry 8   x = 10
bumped 11
= 22 : I64
watch 1   entry 9   x = 11
check: cpu-sir-interp and cpu-c17 agree at 2 boundaries
ci[10]> :run bump(5) --check cpu-sir-interp
:run --check names a second backend; cpu-sir-interp is the one in force
ci[11]> :run bump(5) --check gpu-cuda
:run --check: no backend gpu-cuda (cpu-sir-interp, cpu-c17)
ci[12]> :run bump(5) --device gpu-cuda
:run --device waits for a gpu-cuda library: `cint build --lib` builds cpu-c17 only
ci[13]> :run bump(5) 1 2
:run takes F(ARGS) [N] [--check BACKEND]; N is a whole number of calls, at least 1
ci[14]> :backend cpu-c17
backend: cpu-c17; each revision is built as a library, entries on it are not recorded, and :inspect and the \
watches stay on cpu-sir-interp
ci[14]> :save saved
saved session to saved   entries 13   checkpoints <n>   objects <n>
ci[14]> :quit
"""
REOPEN_ENTRIES = """\
:open saved
:backend
x
:quit --yes
"""
REOPEN_SAID = """\
cint workbench   profile cint-core-1   backend cpu-sir-interp   new session
ci[1]> :open saved
opened saved   entries 13   checkpoints <n>   branch main
ci[14]> :backend
backend: cpu-c17
ci[14]> x
""" + BUILT + """\
= 11 : I64
ci[15]> :quit --yes
"""


def check(case, wb, exes) -> int:
    """The session on cpu-c17 against B1, and the backend kept across :save and :open."""
    if wb.ARGS.cint is None:
        return 0
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp) / "backend"
        (work / "root").mkdir(parents=True)
        argv = [exes["cint-workbench"], "--cint", pathlib.Path(wb.ARGS.cint).resolve(), "--root", work / "root",
                "--echo"]
        result = wb.run(argv + ["--out", work / "session-1"], cwd=work, entries=ENTRIES)
        case.assertClean(result, "backend")
        case.assertEqual((result.returncode, result.stderr), (0, b""))
        case.assertRegex(result.stdout.decode("utf-8"), wb.transcript(SAID))
        result = wb.run(argv + ["--out", work / "session-2"], cwd=work, entries=REOPEN_ENTRIES)
        case.assertClean(result, "backend reopened")
        case.assertEqual((result.returncode, result.stderr), (0, b""))
        case.assertRegex(result.stdout.decode("utf-8"), wb.transcript(REOPEN_SAID))
    return 2
