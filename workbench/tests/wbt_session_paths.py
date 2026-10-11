"""Session and locate paths that no other unit's transcript reaches (the box 14 review, R-7).

Run by workbench/tests/test_workbench.py for each configuration it builds (its units()); with
--cint PATH, against B1:

- a session transcript: `:explain` of the session module and of one function, a depth fault after
  `:depth`, an entry that assigns an exported variable of a loaded module and a call that reads it
  (box 14 default BX14-08), C2054 for a variable of an error set declared without an initializer,
  and such a variable declared with one, which stays in the session module (BX14-08) and shows
  as its tag, since the session has no display of an error-set value yet, an `Arena`
  declared at the prompt, which the session does not hold yet (OQ-243), and a fault inside
  `:inspect`, after which the session is as it was;
- `cint debug locate --fuel N` on a program: the E_FUEL fault at that allowance, and none at a
  larger one.
"""
from __future__ import annotations

import pathlib
import re
import tempfile

METER = """\
export I64 total = 0;
export I64 read() {
    return total;
}
"""
ENTRIES = """\
:load meter.ci
meter.total = 5;
meter.read()
I64 x = 1;
export I64 deep(I64 n) {
    if (n == 0) {
        return 0;
    }
    return deep(n - 1) + 1;
}
:explain
:explain deep
:depth 4
deep(10)
deep(2)
error E { a, b }
E e;
E f = E.b;
Arena(I64, 8) a;
I64[4] arr;
:inspect arr[9]
x
:quit --yes
"""
# The transcript, up to the output of :explain, which follows B1's SIR text and is checked by its
# lines below; then from the depth fault on.
HEAD = """\
cint workbench   profile cint-core-1   backend cpu-sir-interp   new session
ci[1]> :load meter.ci
loaded meter (meter.ci)
ci[2]> meter.total = 5;
ci[3]> meter.read()
= 5 : I64
ci[4]> I64 x = 1;
x = 1 : I64
ci[5]> export I64 deep(I64 n) {
..>     if (n == 0) {
..>         return 0;
..>     }
..>     return deep(n - 1) + 1;
..> }
deep : (I64) -> I64   revision session@r2
ci[6]> :explain
"""
EXPLAIN = [   # lines of the two :explain outputs
    "program   2 modules, 2 functions, ",
    "  meter.ci     62 bytes   ",
    "function deep   session.sites:",
    "  signature  (in n: I64) -> I64",
    "    deep#3  session.ci:7:12  call.enter       E_FUEL E_DEPTH possible",
    "    cpu-sir-interp    supported   the workbench evaluator runs this text (SPEC-09 BACK-01a)",
]
TAIL = """\
ci[6]> :depth 4
depth: 4 per entry
ci[6]> deep(10)
fault[E_DEPTH]: call depth limit exceeded
  --> entry 5:5:12
   |
 5 |     return deep(n - 1) + 1;
   |            ^ call.enter
   |
   = operation: call.enter
   = operand-count: 0
   = exact:   none
   = limit:   I64 4
"""
AFTER = """\
ci[7]> deep(2)
= 2 : I64
ci[8]> error E { a, b }
E   revision session@r3
ci[9]> E e;
error[C2054]: type rule
  --> entry 9:1:1
   |
 1 | E e;
   | ^
   |
ci[10]> E f = E.b;
f = 2 : U16
ci[11]> Arena(I64, 8) a;
a : not shown by the session (it shows integers, Bool, and arrays of them)
ci[12]> I64[4] arr;
arr : I64[4]   4 elements   host
  [0] = 0
  [1] = 0
  [2] = 0
  [3] = 0
ci[13]> :inspect arr[9]
fault[E_BOUNDS]: index out of bounds
"""
END = """\
   = state: nothing is kept: the session is as it was; context reset
   = noncanonical: not part of the fault record
   = workbench-position: :inspect, not an entry
"""
LAST = """\
ci[13]> x
= 1 : I64
ci[14]> :quit --yes
"""
COUNT = """\
export I64 total(I64 n) {
    I64 t = 0;
    for i in 0..n {
        t = t + i;
    }
    return t;
}
"""


def check(case, wb, exes) -> int:
    """The transcript and locate --fuel against B1; none without --cint."""
    if wb.ARGS.cint is None:
        return 0
    cint = pathlib.Path(wb.ARGS.cint).resolve()
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp)
        (work / "root").mkdir()
        (work / "root" / "meter.ci").write_text(METER, encoding="utf-8", newline="\n")
        result = wb.run([exes["cint-workbench"], "--cint", cint, "--root", work / "root", "--echo", "--out",
                         work / "session"], cwd=work, entries=ENTRIES)
        case.assertClean(result, "session paths")
        case.assertEqual((result.returncode, result.stderr), (0, b""))
        said = result.stdout.decode("utf-8")
        case.assertTrue(said.startswith(HEAD), said)
        explained = said[len(HEAD):said.index("ci[6]> :depth 4\n")]
        case.assertEqual(len(re.findall(r"^module session\.ci   revision [0-9a-f]{64}$", explained, re.M)), 2, explained)
        for line in EXPLAIN:
            case.assertIn("\n" + line, explained)
        rest = said[said.index("ci[6]> :depth 4\n"):]
        case.assertTrue(rest.startswith(TAIL), rest)
        case.assertIn("   = workbench-position: entry 6\n", rest)
        after = rest[rest.index("ci[7]> deep(2)\n"):]
        case.assertTrue(after.startswith(AFTER), after)
        case.assertIn(END, after)
        case.assertTrue(after.endswith(LAST), after)

        (work / "count.ci").write_text(COUNT, encoding="utf-8", newline="\n")
        locate = [exes["cint-debug"], "locate", "count.ci", "--cint", cint, "--out", work / "built", "--entry", "total",
                  "--args", "I64 10", "--fuel"]
        result = wb.run(locate + ["5"], cwd=work)
        case.assertClean(result, "locate --fuel")
        out = result.stdout.decode("utf-8")
        case.assertEqual((result.returncode, result.stderr), (0, b""), out)
        case.assertIn("\nfault[E_FUEL]: fuel allowance exhausted\n", out)
        case.assertIn("\n   = limit:   I64 5\n", out)
        result = wb.run(locate + ["50"], cwd=work)
        case.assertEqual((result.returncode, result.stderr), (3, b""))
        case.assertIn("\ntarget     none: no boundary ends in a fault or an error result", result.stdout.decode("utf-8"))
    return 2
