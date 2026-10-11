"""Conformance workbench mode's quick subset (box 14; SPEC-06 5.1, box 14 ruling BX14-04): the
pieces of tools/cint_wbmode.py, and with --cint a few scripts against `cint run`.

Run by workbench/tests/test_workbench.py for each configuration it builds (its units()):

- without --cint: wb_session.c's incomplete() as the feeder reads it (brackets, strings and
  character literals, nested block comments, line comments); the items and entries of a script
  written here: an import taken by :load, one declarations entry with the test left out and a
  later const joined after the function's last line, a variable whose initializer is joined to
  its line with its comment blanked, and a `defer` with the statement after it; each entry's
  map back to the script's lines and columns; the rewrite and its line map; and the case reader
  on the case wb-qualify records of three calls, and on that case cut short;
- with --cint PATH: three conformance scripts that cpu-sir-interp runs (a fault in a print
  hole, a fault after a print with no line feed, and a top-level defer) and three scripts
  written here (declarations, joined lines, and a fault in a function; a fault in an imported
  module; Bool, U8 and array variables and a variable after a defer), each fed to this
  configuration's cint-workbench and compared with `cint run`: each agrees, with the fault's
  position and stack mapped to the script's lines and the values compared or listed as not
  compared; and the first of them again with its rewrite altered, which differs in stdout and
  in the value.

The full run, over every conformance script that cpu-sir-interp runs and with a receipt, is the
gate tool tools/cint_wbmode.py.
"""
from __future__ import annotations

import pathlib
import tempfile

TEXT = """\
// header
import lib.acc;

I64 twice(I64 v) {
    return v * 2;
}
I64 total =
    twice(3);   // six
test "left out" {
    assert(1 == 1);
}
"t={total}\\n";
defer { "end\\n"; }
I64 after = 1;
const I64 K = 4;
"""
ENTRIES = [  # (kind, lines, segments, names)
    ("import", [":load lib/acc.ci"], [[]], []),
    ("declarations", ["I64 twice(I64 v) {", "    return v * 2;", "} const I64 K = 4;"],
     [[(1, 4, 1)], [(1, 5, 1)], [(1, 6, 1), (3, 15, 1)]], []),
    ("var", ["I64 total = twice(3);"], [[(1, 7, 1), (13, 8, 5)]], ["total"]),
    ("stmt", ['"t={total}\\n";'], [[(1, 12, 1)]], []),
    ("defer", ['defer { "end\\n"; } I64 after = 1;'], [[(1, 13, 1), (20, 14, 1)]], []),
]
REWRITTEN = """\
// header
import lib.acc;

I64 twice(I64 v) {
    return v * 2;
}
void main() {
    total =
    twice(3);   // six
"t={total}\\n";
defer { "end\\n"; }
    after = 1;
}
test "left out" {
    assert(1 == 1);
}
const I64 K = 4;
I64 total;
I64 after;
"""
LINE_MAP = [1, 2, 3, 4, 5, 6, None, 7, 8, 12, 13, 14, None, 9, 10, 11, 15, None, None]
INCOMPLETE = [("f(", True), ("f()", False), ('"(" ', False), ("'{'", False), ('"a\\"(" ', False),
              ("/* (", True), ("/* /* */", True), ("/* /* */ */", False), ("x // (", False),
              ("{\n}", False), ("[\n", True), ('"unterminated (', False)]

SCRIPTS = {  # written here, under a root of their own
    "lib/acc.ci": "export I64 add(I64 a, I64 b) {\n    return a + b;\n}\n",
    "main/a.ci": """\
// declarations, joined lines, and a fault in a function
I64 twice(I64 v) {
    return v * K;   // faults on overflow
}

I64 total = 0;
total = twice(
    3);
I64 big =
    4611686018427387904;
"total={total}\\n"; /* a comment */
I64 r = twice(big);
"never\\n";
test "left out" {
    assert(1 == 2);
}
const I64 K = 2;
""",
    "main/b.ci": """\
import lib.acc;

I64 x = acc.add(1, 2);
"x={x}\\n";
I64 y = acc.add(x, 9223372036854775807);
""",
    "main/c.ci": """\
I64[2] a;
Bool f = true;
U8 u = 200;
"start\\n";
defer { "end\\n"; }
I64 after = 5;
"mid {after}\\n";
""",
}
CONFORMANCE = {  # case: the fault position, or None
    "control/print_hole_fault_partial.ci": "control/print_hole_fault_partial.ci:8:11",
    "control/print_no_newline_then_fault.ci": "control/print_no_newline_then_fault.ci:8:13",
    "cleanup/script_defer_runs_at_end.ci": None,
}
WRITTEN = {  # script: (outcome, the fault's position and stack lines, values compared, not compared)
    "main/a.ci": ("fault", ["fault.position main/a.ci:3:14", "fault.stack main/a.ci:12:9"],
                  [["total", "I64 6"], ["big", "I64 4611686018427387904"], ["r", "I64 0"]], []),
    "main/b.ci": ("fault", ["fault.position lib/acc.ci:2:14", "fault.stack main/b.ci:5:13"],
                  [["x", "I64 3"], ["y", "I64 0"]], []),
    "main/c.ci": ("value", [], [["f", "Bool true"], ["u", "U8 200"]],
                  [["a", "no state.global form for its type (CONF-11 rule 10)"],
                   ["after", "declared after a top-level defer, a local of that entry"]]),
}


def tool(wb):
    """tools/cint_wbmode.py; tools/ is on the path, where test_workbench.py found cint_check."""
    import cint_wbmode
    return cint_wbmode


def check_pieces(case, wb, W, exes) -> int:
    for text, want in INCOMPLETE:
        case.assertEqual(W.incomplete(text), want, text)
    toks, items, _ = W.script_items(TEXT, "main/u.ci")
    case.assertEqual([(it.kind, toks[it.start].line) for it in items],
                     [("import", 2), ("decl", 4), ("var", 7), ("test", 9), ("stmt", 12), ("defer", 13),
                      ("var", 14), ("decl", 15)])
    entries = W.plan(TEXT, toks, items)
    case.assertEqual([(e.kind, e.lines, e.segments, e.names) for e in entries], ENTRIES)
    case.assertEqual(entries[1].map(3, 3), (15, 1))      # `const` after the joined `}`
    case.assertEqual(entries[2].map(1, 19), (8, 11))     # `(` of twice(3), from line 8
    case.assertEqual(entries[2].map(1, 5), (7, 5))
    case.assertIsNone(entries[0].map(1, 1))
    case.assertEqual(W.rewrite(TEXT, toks, items), (REWRITTEN, LINE_MAP))
    with case.assertRaises(W.Unfit):
        W.rewrite('I64 x = 1; "x";\n', *W.script_items('I64 x = 1; "x";\n', "u.ci")[:2])
    with case.assertRaises(W.Unfit):
        W.plan("import lib.acc as a;\n\"a\";\n", *W.script_items("import lib.acc as a;\n\"a\";\n", "u.ci")[:2])
    checked = len(INCOMPLETE) + 8
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp)
        sir = wb.SIR / "module__state_two_modules"
        result = wb.run([exes["wb-qualify"], "record", sir, wb.CONFORMANCE, "run", "3", "c.case"], cwd=work)
        case.assertEqual(result.returncode, 0, result.stderr)
        data = (work / "c.case").read_bytes()
        events = W.read_case(data)
        case.assertEqual([e[:2] if e[0] in ("entry", "end") else e[:3] for e in events
                          if e[0] in ("entry", "outcome", "end")],
                         [("entry", 1), ("outcome", 1, 0), ("entry", 2), ("outcome", 2, 0), ("entry", 3),
                          ("outcome", 3, 0), ("end", 0)])
        case.assertTrue(all(e[3] is None and e[4] is None for e in events if e[0] == "outcome"))
        with case.assertRaises(ValueError):
            W.read_case(data[:-1])
    return checked + 3


def check_scripts(case, wb, W, exes) -> int:
    cint = pathlib.Path(wb.ARGS.cint).resolve()
    tc = wb.cc.Toolchain(case.leg, "2", "portable", False)
    env = dict(tc.env, **W.SAN_ENV)
    workbench, checked = exes["cint-workbench"], 0
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp)
        for rel, position in CONFORMANCE.items():
            res = W.compare_script(rel, wb.CONFORMANCE, workbench, cint, work / rel[:-3], env, tc.exe)
            case.assertEqual((res["result"], res["differences"]), ("agree", []), rel)
            if position is not None:
                case.assertIn("fault.position " + position, res["fault"], rel)
            checked += 1
        root = work / "root"
        for rel, text in SCRIPTS.items():
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_bytes(text.encode("utf-8"))
        for rel, (outcome, lines, compared, not_compared) in WRITTEN.items():
            res = W.compare_script(rel, root, workbench, cint, work / rel[:-3], env, tc.exe)
            case.assertEqual((res["result"], res["differences"], res["outcome"]), ("agree", [], outcome), rel)
            case.assertEqual([x for x in res.get("fault", []) if x.startswith(("fault.position", "fault.stack "))],
                             lines, rel)
            case.assertEqual(res["values"], {"compared": compared, "not_compared": not_compared}, rel)
            checked += 1
        original = W.rewrite

        def altered(text, toks, items):   # the reference prints and keeps 2, not 6
            out, lines = original(text, toks, items)
            return out.replace("total = twice(\n    3);", "total = twice(\n    1);"), lines
        W.rewrite = altered
        try:
            res = W.compare_script("main/a.ci", root, workbench, cint, work / "altered", env, tc.exe)
        finally:
            W.rewrite = original
        case.assertEqual(res["result"], "differ")
        case.assertEqual([d.split(":")[0] for d in res["differences"]], ["stdout", "value of total"])
        case.assertIn("value of total: session I64 6, cint run I64 2", res["differences"])
    return checked + 1


def check(case, wb, exes) -> int:
    W = tool(wb)
    checked = check_pieces(case, wb, W, exes)
    if wb.ARGS.cint is not None:
        checked += check_scripts(case, wb, W, exes)
    return checked
