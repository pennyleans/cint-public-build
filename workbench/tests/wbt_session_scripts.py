"""`:load` of a script and the workbench position (box 14 defaults BX14-04 and BX14-13).

Run by workbench/tests/test_workbench.py for each configuration it builds (its units()); with
--cint PATH, against B1:

- `:load` of a script (SPEC-04 LS-218): its statements run as one entry, so its variables stay
  locals of that entry (LS-219) and its declarations stay in the session module; a fault in it is
  at the script's own line and column, with its excerpt; a script whose import the session loaded;
  the module-level pool of a conformance script;
- the workbench position: the call ordinal of a fault within `:run F() N`, and the branch after
  `:goto`;
- a saved session with scripts, opened again: `:goto` and `:trace` of the script's entry.
"""
from __future__ import annotations

import pathlib
import tempfile

FILES = {
    "tally.ci": """\
export I64 total(I64 n) {
    I64 t = 0;
    for i in 0..n {
        t = t + i;
    }
    return t;
}
I64 x = total(5);
"{x}\\n";
""",
    "scripts/bad.ci": """\
I64 a = 3;
I64 b = half(a);
"{b}\\n";
I64 c = b / (a - 3);
"{c}\\n";

export I64 half(I64 v) {
    return v / 2;
}
""",
    "util.ci": "export I64 twice(I64 v) {\n    return v * 2;\n}\n",
    "use.ci": "import util;\nI64 y = util.twice(21);\n\"{y}\\n\";\n",
}
ENTRIES = """\
:load tally.ci
x
total(4)
:load scripts/bad.ci
half(9)
:load util.ci
:load use.ci
I64 k = 2;
export I64 tick() {
    k = k - 1;
    return 10 / k;
}
:run tick() 3
:goto 5
9 / half(1)
:save s.cis
:load pool.ci
"""
SAID = [  # in this order in the transcript
    "ci[1]> :load tally.ci\n10\ntally.ci   revision session@r1\n",
    "ci[2]> x\nerror[C3005]: undefined or unusable name\n  --> entry 2:1:1\n",
    "ci[3]> total(4)\n= 6 : I64\n",
    """\
ci[4]> :load scripts/bad.ci
1
fault[E_DIV_ZERO]: division by zero
  --> scripts/bad.ci:4:11
   |
 4 | I64 c = b / (a - 3);
   |           ^ div.checked.i64
   |
""",
    "   = workbench-position: entry 4\n",
    "scripts/bad.ci   revision session@r2\nci[5]> half(9)\n= 4 : I64\n",
    "ci[7]> :load use.ci\n42\nuse.ci   revision session@r3\n",
    "ci[10]> :run tick() 3\n= 10 : I64\nfault[E_DIV_ZERO]: division by zero\n  --> entry 9:3:15\n",
    "   = workbench-position: entry 11, call 2 of 3\n",
    "2 of 3 calls made\n",
    "later entries go on branch entry-5, from auto-2\n",
    "ci[12]> 9 / half(1)\nfault[E_DIV_ZERO]: division by zero\n",
    "   = workbench-position: branch entry-5, entry 12\n",
    "ci[13]> :load pool.ci\n42\npool.ci   revision session@r6\n",
]
REOPENED = """\
ci[1]> :open s.cis
opened s.cis   entries 12   checkpoints 2   branch entry-5
ci[13]> half(4)
= 2 : I64
ci[14]> :goto 4
saved current state as auto-3
at the end of entry 4, reached by replay
later entries go on branch entry-4, from auto-4
ci[14]> :trace 4
entry 4   :load scripts/bad.ci   evaluator cpu-sir-interp, by replay
  ord  position           operation              operands                 value
    0  scripts/bad.ci:2:9 call                   3
    1  scripts/bad.ci:8:14 div.checked.i64        3, 2                     1
    -  scripts/bad.ci:2:9 return                                          1
    2  scripts/bad.ci:4:16 sub.checked.i64        3, 3                     0
    3  scripts/bad.ci:4:11 div.checked.i64        1, 0                     E_DIV_ZERO
ci[14]> :history scripts/bad.ci
r2  entry 4   in checkpoints: auto-1, auto-2, auto-3, auto-4
ci[14]> """


def check(case, wb, exes) -> int:
    """The transcript and the reopened session against B1; none without --cint."""
    if wb.ARGS.cint is None:
        return 0
    cint = pathlib.Path(wb.ARGS.cint).resolve()
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp)
        for name, text in FILES.items():
            (work / "root" / name).parent.mkdir(parents=True, exist_ok=True)
            (work / "root" / name).write_text(text, encoding="utf-8", newline="\n")
        pool = (wb.CONFORMANCE / "memory" / "script_pool_module_level.ci").read_bytes()
        (work / "root" / "pool.ci").write_bytes(pool)
        workbench = [exes["cint-workbench"], "--cint", cint, "--root", work / "root", "--echo"]
        result = wb.run(workbench + ["--out", work / "session"], cwd=work, entries=ENTRIES)
        case.assertClean(result, "scripts")
        case.assertEqual((result.returncode, result.stderr), (0, b""))
        said, at = result.stdout.decode("utf-8"), 0
        for text in SAID:
            found = said.find(text, at)
            case.assertGreaterEqual(found, 0, "%r not in order in\n%s" % (text, said))
            at = found + len(text)
        result = wb.run(workbench + ["--out", work / "again"], cwd=work,
                        entries=":open s.cis\nhalf(4)\n:goto 4\n:trace 4\n:history scripts/bad.ci\n")
        case.assertClean(result, "scripts reopened")
        case.assertEqual((result.returncode, result.stderr), (0, b""))
        said = result.stdout.decode("utf-8")
        case.assertTrue(said.endswith(REOPENED + "\n"), said)
    return len(SAID) + 2
