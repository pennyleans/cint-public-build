"""Error results in the session (box 14; sections 5.3, 6.4 and 6.5 of the box 14 note).

Run by workbench/tests/test_workbench.py for each configuration it builds (its units()); with
--cint PATH, a transcript against B1, which box 13 unit 8b-1 lets cpu-sir-interp run. Error sets,
a combined set, and functions that return an error union, declared at the prompt; `:run` of an
export that returns an error, which completes with the error as its result (SPEC-03 A-18); `:fault`
and `:fault /path`, the path rebuilt by replay from the call and return events (BX12-19, BX14-18)
through two `try`, one of them widened into the combined set, and for an `as?` and a `catch`
followed by a new error; `:trace` of the entry; cpu-c17, which cannot call an export of an error
union yet; `:log`; the saved session opened in a new process; `cint debug locate` on the case,
which finds the first error result and prints its path; and a debug session on the case.
"""
from __future__ import annotations

import pathlib
import tempfile

ENTRIES = """\
error IoError { closed, full }
error ParseError { empty, bad_digit, too_large }
error IoOrParse : U32 = IoError | ParseError;
ParseError!I64 digit(U8 c) {
    if (c < '0' || c > '9') {
        return .bad_digit;
    }
    return (c - '0') as I64;
}
ParseError!I64 twice(U8 c) {
    I64 d = try digit(c);
    return d * 2;
}
export IoOrParse!I64 go(U8 c) {
    I64 t = try twice(c);
    return t + 1;
}
export ArithError!I64 narrow(I64 v) {
    I8 s = try (v as? I8);
    return s as I64;
}
export IoError!I64 caught(U8 c) {
    I64 d = digit(c) catch 0;
    if (d == 0) {
        return .closed;
    }
    return d;
}
:fault /path
:run go(55)
:run go(120)
:fault
:fault /path
:trace
:run narrow(300)
:fault /path
:run caught(120)
:fault /path
:run go(120) --check cpu-c17
:fault /x
:log
:save-case errors.case
:save saved
:quit
"""
SAID = """\
cint workbench   profile cint-core-1   backend cpu-sir-interp   new session
ci[1]> error IoError { closed, full }
IoError   revision session@r1
ci[2]> error ParseError { empty, bad_digit, too_large }
ParseError   revision session@r2
ci[3]> error IoOrParse : U32 = IoError | ParseError;
IoOrParse   revision session@r3
ci[4]> ParseError!I64 digit(U8 c) {
..>     if (c < '0' || c > '9') {
..>         return .bad_digit;
..>     }
..>     return (c - '0') as I64;
..> }
digit : (U8) -> U16!I64   revision session@r4
ci[5]> ParseError!I64 twice(U8 c) {
..>     I64 d = try digit(c);
..>     return d * 2;
..> }
twice : (U8) -> U16!I64   revision session@r5
ci[6]> export IoOrParse!I64 go(U8 c) {
..>     I64 t = try twice(c);
..>     return t + 1;
..> }
go : (U8) -> U32!I64 error E2   revision session@r6
ci[7]> export ArithError!I64 narrow(I64 v) {
..>     I8 s = try (v as? I8);
..>     return s as I64;
..> }
narrow : (I64) -> U16!I64 error E9   revision session@r7
ci[8]> export IoError!I64 caught(U8 c) {
..>     I64 d = digit(c) catch 0;
..>     if (d == 0) {
..>         return .closed;
..>     }
..>     return d;
..> }
caught : (U8) -> U16!I64 error E15   revision session@r8
ci[9]> :fault /path
no recorded entry of this session left an error result
ci[9]> :run go(55)
= 15 : I64
ci[10]> :run go(120)
error: session.IoOrParse.bad_digit (tag 4)
ci[11]> :fault
error: session.IoOrParse.bad_digit (tag 4)
ci[11]> :fault /path
entry 10   error: session.IoOrParse.bad_digit (tag 4)   evaluator cpu-sir-interp, by replay
the path, innermost first, from the calls and returns of the replay:
  digit returned the error (tag 2)
  twice passed it on with try (tag 2), from its call of digit at entry 5:2:17
  go passed it on with try (tag 4), from its call of twice at entry 6:2:17
ci[11]> :trace
entry 10   :run go(120)   evaluator cpu-sir-interp, by replay
  ord  position           operation              operands                 value
    0  entry 6:2:17       call                   120
    1  entry 5:2:17       call                   120
    -  entry 5:2:17       return                                          error 2
    -  entry 6:2:17       return                                          error 2
    2                     as.wrap.u16.u32        2                        2
    3                     add.wrap.u32           2, 2                     4
ci[11]> :run narrow(300)
error: ArithError.narrow (tag 4)
ci[12]> :fault /path
entry 11   error: ArithError.narrow (tag 4)   evaluator cpu-sir-interp, by replay
the path, innermost first, from the calls and returns of the replay:
  narrow returned the error (tag 4)
ci[12]> :run caught(120)
error: session.IoError.closed (tag 1)
ci[13]> :fault /path
entry 12   error: session.IoError.closed (tag 1)   evaluator cpu-sir-interp, by replay
the path, innermost first, from the calls and returns of the replay:
  caught returned the error (tag 1)
ci[13]> :run go(120) --check cpu-c17
cpu-c17: revision <h> built as a library in <n> ms
workbench: cpu-c17 cannot call go: it has a struct, enum, or error set
ci[14]> :fault /x
:fault takes /path, or nothing
ci[14]> :log
entry 1     declared       error IoError { closed, full }
entry 2     declared       error ParseError { empty, bad_digit, too_large }
entry 3     declared       error IoOrParse : U32 = IoError | ParseError;
entry 4     declared       ParseError!I64 digit(U8 c) { ...
entry 5     declared       ParseError!I64 twice(U8 c) { ...
entry 6     declared       export IoOrParse!I64 go(U8 c) { ...
entry 7     declared       export ArithError!I64 narrow(I64 v) { ...
entry 8     declared       export IoError!I64 caught(U8 c) { ...
entry 9     value          :run go(55)
entry 10    error          :run go(120)
entry 11    error          :run narrow(300)
entry 12    error          :run caught(120)
entry 13    not run        :run go(120) --check cpu-c17
ci[14]> :save-case errors.case
note: the case includes every recorded observe result verbatim; this case has none
case errors.case   from the recording's start   entries 9..12   effects 0   density entry
ci[14]> :save saved
saved session to saved   entries 13   checkpoints 0   objects <n>
ci[14]> :quit
"""
REOPEN_ENTRIES = """\
:open saved
:fault
:fault /path
:quit
"""
REOPEN_SAID = """\
cint workbench   profile cint-core-1   backend cpu-sir-interp   new session
ci[1]> :open saved
opened saved   entries 13   checkpoints 0   branch main
ci[14]> :fault
no fault in this session; entry 12 left an error result, which :fault /path shows
ci[14]> :fault /path
entry 12   error: session.IoError.closed (tag 1)   evaluator cpu-sir-interp, by replay
the path, innermost first, from the calls and returns of the replay:
  caught returned the error (tag 1)
ci[14]> :quit
"""
LOCATED = """\
case       errors.case   4 boundaries
target     boundary 2 of 4, go(U8 120): the first error result, reproduced by replay
bracket    boundary 1, go(U8 55): value I64 15; then boundary 2
error: session.IoOrParse.bad_digit (tag 4)
the path, innermost first, from the calls and returns of the replay:
  digit returned the error (tag 2)
  twice passed it on with try (tag 2), from its call of digit at session.ci:11:17
  go passed it on with try (tag 4), from its call of twice at session.ci:15:17
"""
DEBUG_ENTRIES = """\
:log
:fault
:fault /path
:quit
"""
DEBUG_SAID = """\
cint workbench   profile cint-core-1   backend cpu-sir-interp   debug errors.case (read-only)
case: entries 4   boundaries 4
at the end of entry 4, reached by replay
ci[5]> :log
entry 1     value          session.ci: go(U8 55)
entry 2     error          session.ci: go(U8 120)
entry 3     error          session.ci: narrow(I64 300)
entry 4     error          session.ci: caught(U8 120)
ci[5]> :fault
no fault in this session; entry 4 left an error result, which :fault /path shows
ci[5]> :fault /path
entry 4   error: session.IoError.closed (tag 1)   evaluator cpu-sir-interp, by replay
the path, innermost first, from the calls and returns of the replay:
  caught returned the error (tag 1)
ci[5]> :quit
"""


def check(case, wb, exes) -> int:
    """The transcripts against B1; none without --cint."""
    if wb.ARGS.cint is None:
        return 0
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp) / "errors"
        (work / "root").mkdir(parents=True)
        argv = [exes["cint-workbench"], "--cint", pathlib.Path(wb.ARGS.cint).resolve(), "--root", work / "root",
                "--echo"]
        for entries, said, out in ((ENTRIES, SAID, "session-1"), (REOPEN_ENTRIES, REOPEN_SAID, "session-2")):
            result = wb.run(argv + ["--out", work / out], cwd=work, entries=entries)
            case.assertClean(result, out)
            case.assertEqual((result.returncode, result.stderr), (0, b""))
            case.assertRegex(result.stdout.decode("utf-8"), wb.transcript(said))
        result = wb.run([exes["cint-debug"], "locate", "errors.case"], cwd=work)
        case.assertClean(result, "locate")
        case.assertEqual((result.returncode, result.stderr, result.stdout.decode("utf-8")), (0, b"", LOCATED))
        result = wb.run([exes["cint-debug"], "--echo", "errors.case", "--out", work / "debug"], cwd=work,
                        entries=DEBUG_ENTRIES)
        case.assertClean(result, "debug session")
        case.assertEqual((result.returncode, result.stderr, result.stdout.decode("utf-8")), (0, b"", DEBUG_SAID))
    return 4
