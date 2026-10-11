"""Arrays and kernels in the session (box 14 unit 4b; sections 5.2, 5.3, 5.6, 6.5 and 8.3 of the box 14
note).

Run by workbench/tests/test_workbench.py for each configuration it builds (its units()); with
--cint PATH, a transcript against B1. Module-level arrays (box 13 MLA) declared at the prompt, zero
filled or from a literal, which stays in the declaration; an array named as an entry; `:inspect`
with /stats, /grid and a window, and /view, and `:type`, all on live state; `:diff` element by
element and of a retyped array; a kernel declared and dispatched from an export, on both backends
under --check; a fault in a work-item; arrays brought back by `:restore`, by `:open` in a new
process, and in a debug session opened on a case; the device commands, which wait (BX14-22); and
`:fault /path` in a session with no error result.
"""
from __future__ import annotations

import pathlib
import tempfile

ENTRIES = """\
I64[4] acc;
I32[3] base = [3, 1, 4];
Bool[2] flags = [true, false];
acc[1] = 7;
acc
:inspect acc /stats
:inspect base /grid [1..3]
:inspect flags /view
:type base
:checkpoint one
acc[2] = 5;
acc[3] = -1;
:diff one
:diff one base
I32[3] base = [5, 5, 5];
I64[5] acc;
:diff one acc
kernel twice[n](in I32[n] a, out I32[n] b) over [i: n] {
    b[i] = a[i] * 2;
}
I32[3] doubled;
export I64 go() {
    twice(base, doubled);
    return sum(doubled) as I64;
}
:run go() --check cpu-c17
doubled
base[1] = 2000000000;
:run go()
:restore one
acc[0] = 3;
:save-case arrays.case
:place
:faults
:run go() --device
:fault /path
:save saved
:quit
"""
SAID = """\
cint workbench   profile cint-core-1   backend cpu-sir-interp   new session
ci[1]> I64[4] acc;
acc : I64[4]   4 elements   host
  [0] = 0
  [1] = 0
  [2] = 0
  [3] = 0
ci[2]> I32[3] base = [3, 1, 4];
base : I32[3]   3 elements   host
  [0] = 3
  [1] = 1
  [2] = 4
ci[3]> Bool[2] flags = [true, false];
flags : Bool[2]   2 elements   host
  [0] = true
  [1] = false
ci[4]> acc[1] = 7;
ci[5]> acc
acc : I64[4]   4 elements   host
  [0] = 0
  [1] = 7
  [2] = 0
  [3] = 0
ci[6]> :inspect acc /stats
acc : I64[4]   4 elements   host
  count    4
  sum      7   accumulator I66 (SPEC-01 IM-80)
  min      0   first at [0]
  max      7   first at [1]
  nonzero  1
ci[6]> :inspect base /grid [1..3]
base : I32[3]   3 elements   host
  grid   columns 1 to 2 of 0 to 2
    1  2
    1  4
ci[6]> :inspect flags /view
flags : Bool[2]   2 elements   host
  view
    buffer      none: this buffer has no canonical identity (SPEC-01 IM-81)
    element     Bool
    rank        1
    shape       [2]
    lower       [0]
    origin      0
    strides     [1] elements
    permission  write
    placement   host
ci[6]> :type base
base : I32[3]
ci[6]> :checkpoint one
checkpoint one   #1   bindings 3   revisions: session@r3
ci[6]> acc[2] = 5;
ci[7]> acc[3] = -1;
ci[8]> :diff one
binding   status      differing   first canonical difference
acc       differs     2           [2]: 5 here, 0 in one
ci[8]> :diff one base
base is equal here and in one: I32[3]
ci[8]> I32[3] base = [5, 5, 5];
base : I32[3]   3 elements   host
  [0] = 3
  [1] = 1
  [2] = 4
  (a redeclaration of the same type keeps the elements (I6); an assignment changes them)
ci[9]> I64[5] acc;
acc : I64[5]   5 elements   host
  [0] = 0
  [1] = 0
  [2] = 0
  [3] = 0
  [4] = 0
ci[10]> :diff one acc
binding   status      differing   first canonical difference
acc       retyped     1           I64[5] here, I64[4] in one
ci[10]> kernel twice[n](in I32[n] a, out I32[n] b) over [i: n] {
..>     b[i] = a[i] * 2;
..> }
twice   revision session@r6
ci[11]> I32[3] doubled;
doubled : I32[3]   3 elements   host
  [0] = 0
  [1] = 0
  [2] = 0
ci[12]> export I64 go() {
..>     twice(base, doubled);
..>     return sum(doubled) as I64;
..> }
go : () -> I64   revision session@r8
ci[13]> :run go() --check cpu-c17
cpu-c17: revision <h> built as a library in <n> ms
= 16 : I64
check: cpu-sir-interp and cpu-c17 agree at 1 boundary
ci[14]> doubled
doubled : I32[3]   3 elements   host
  [0] = 6
  [1] = 2
  [2] = 8
ci[15]> base[1] = 2000000000;
ci[16]> :run go()
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> entry 10:2:17
   |
 2 |     b[i] = a[i] * 2;
   |                 ^ mul.checked.i32
   |
   = operation: mul.checked.i32
   = operand-count: 2
   = operand: I32 2000000000
   = operand: I32 2
   = exact:   4000000000
   = limit:   I32 2147483647
   = revision: <revision>
   = source-map: none
   = address: session.twice, dispatch 0, phase 1, work-item 1, step 2
   = stack-depth: 1
   = called from: entry 12:2:5
   = fuel-consumed: 4 (not part of the fault record)
   = state: no store completed in this entry; context reset
   = noncanonical: not part of the fault record
   = stack-source: entry 12:2:5
   |
 2 |     twice(base, doubled);
   |     ^
   |
   = work-item-tuple: [1]
   = workbench-position: entry 16
   = site: twice#3
   = backend: cpu-sir-interp; the dispatch ran in F-3 order and stopped at its first fault (SPEC-02 B-5)
ci[17]> :restore one
saved current state as auto-1
restored one   #1   acc is revision session@r1   base is revision session@r2   twice is not declared   \
doubled is not declared   go is not declared
ci[17]> acc[0] = 3;
ci[18]> :save-case arrays.case
note: the case includes every recorded observe result verbatim; this case has none
case arrays.case   checkpoint one   entry 17   effects 0   density entry
ci[18]> :place
:place waits for a gpu-cuda library: `cint build --lib` builds cpu-c17 only
ci[18]> :faults
:faults waits for a gpu-cuda library: `cint build --lib` builds cpu-c17 only
ci[18]> :run go() --device
:run --device waits for a gpu-cuda library: `cint build --lib` builds cpu-c17 only
ci[19]> :fault /path
no recorded entry of this session left an error result
ci[19]> :save saved
saved session to saved   entries 18   checkpoints 2   objects <n>
ci[19]> :quit
"""
REOPEN_ENTRIES = """\
:open saved
acc
:quit --yes
"""
REOPEN_SAID = """\
cint workbench   profile cint-core-1   backend cpu-sir-interp   new session
ci[1]> :open saved
opened saved   entries 18   checkpoints 2   branch main
ci[19]> acc
acc : I64[4]   4 elements   host
  [0] = 3
  [1] = 7
  [2] = 0
  [3] = 0
ci[20]> :quit --yes
"""
DEBUG_ENTRIES = """\
:inspect acc /stats
:type base
:quit
"""
DEBUG_SAID = """\
cint workbench   profile cint-core-1   backend cpu-sir-interp   debug arrays.case (read-only)
case: entries 1   boundaries 1
at the end of entry 1, reached by replay
ci[2]> :inspect acc /stats
session.acc : I64[4]   4 elements   host
  count    4
  sum      10   accumulator I66 (SPEC-01 IM-80)
  min      0    first at [2]
  max      7    first at [1]
  nonzero  2
ci[2]> :type base
session.base : I32[3]
ci[2]> :quit
"""


def check(case, wb, exes) -> int:
    """The transcripts against B1; none without --cint."""
    if wb.ARGS.cint is None:
        return 0
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp) / "arrays"
        (work / "root").mkdir(parents=True)
        argv = [exes["cint-workbench"], "--cint", pathlib.Path(wb.ARGS.cint).resolve(), "--root", work / "root",
                "--echo"]
        for entries, said, out in ((ENTRIES, SAID, "session-1"), (REOPEN_ENTRIES, REOPEN_SAID, "session-2")):
            result = wb.run(argv + ["--out", work / out], cwd=work, entries=entries)
            case.assertClean(result, out)
            case.assertEqual((result.returncode, result.stderr), (0, b""))
            case.assertRegex(result.stdout.decode("utf-8"), wb.transcript(said))
        result = wb.run([exes["cint-debug"], "--echo", "arrays.case", "--out", work / "debug"], cwd=work,
                        entries=DEBUG_ENTRIES)
        case.assertClean(result, "debug session")
        case.assertEqual((result.returncode, result.stderr, result.stdout.decode("utf-8")), (0, b"", DEBUG_SAID))
    return 3
