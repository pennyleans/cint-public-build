"""`cint debug replay` and `cint debug locate` (box 14 unit 3b-D; sections 6.4 and 7.1 of the box 14 note).

Run by workbench/tests/test_workbench.py for each configuration it builds (its units()). The
recordings come from cint-interp (box 13 unit 5), built here once from interp/ at -O0:

- replay of a recording of interp/tests/rec/sir and of control/expect_fault_test_entry: the
  listing of the boundaries, --to, and the replay receipt, whose identity, outcome, and receipt
  identity equal those of `cint-interp replay --receipt` for the same recording and stop, also
  under --json, where the receipt is the output;
- a case (CINT-CASE-1) of the same program from `cint-interp run --case`, and one that
  `locate --save-case` writes: replayed, with receipts equal to cint-interp's, and located;
- a recording with one print altered: DIVERGED at that effect, exit 3, with a receipt equal to
  cint-interp's; a recording cut short: refused, exit 3;
- `cint debug RECORDING`, the read-only session of unit 3b-S: a transcript, and a cut
  recording refused with exit status 3;
- locate on recordings (a fault, a recording without one, the altered one), and on the SIR text
  of view/shape_mismatch_runtime with --entry, where the report is checked against the frozen
  .expect field by field; the usage and environment errors of the command line, with their
  CINT-TOOL-1 lines under --json;
- locate --changed on that recording: the last boundary at which a global changed, by the
  backward scan of SPEC-06 10.1, with its bracket and trace window;
- with --cint PATH, locate builds conformance/control/depth_limit_stack.ci with B1 and runs
  `f(0)` with depth limit 4, the example of note section 6.2: the report against the frozen
  .expect, the bracket, and the trace window that ends at the faulting call; and --changed on a
  recording of three boundaries that a session saved.
"""
from __future__ import annotations

import json
import pathlib
import re
import shutil
import tempfile

INTERP = {}   # leg -> cint-interp

LEDGER_REPLAY = """\
recording  ledger.rec   1 boundary   4 events   mode full   density entry
revision   <revision>
source-map <revision>
boundary  event  entry                          outcome                  fuel
       1      3  the program entry              done                     3
replay     ok, 1 of 1 boundary compared with the recording
chain      <revision>
state      <revision>
"""
FAULT_LOCATE = """\
recording  fault.rec   1 boundary
target     boundary 1 of 1, the program entry: the first fault, reproduced by replay
bracket    the initial state; then boundary 1
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> control/expect_fault_test_entry.ci:9:14
   = operation: add.checked.i8
   = operand-count: 2
   = operand: I8 127
   = operand: I8 1
   = exact:   128
   = limit:   I8 127
   = revision: <revision>
   = source-map: none
   = address: none
   = stack-depth: 0
   = fuel-consumed: 1 (not part of the fault record)
   = state: the post-fault state of SPEC-01 IM-121, reached by replay
   = noncanonical: not part of the fault record
   = workbench-position: boundary 1
   = site: test0#2
   = backend: cpu-sir-interp, by replay
the last 1 counted operations of boundary 1, of 1 trace events (:trace shows them all)
  ord  position           operation              operands                 value
    0  control/expect_fault_test_entry.ci:9:14 add.checked.i8         127, 1                   E_OVERFLOW
"""
LEDGER_SESSION = """\
cint workbench   profile cint-core-1   backend cpu-sir-interp   debug ledger.rec (read-only)
recording: entries 1   boundaries 1
at the end of entry 1, reached by replay
ci[2]> :effects
new context
entry 1   the program entry
  print "ledger 100\\n"
  print "deposit 1\\n"
  done   fuel 3   state <h>
entries 1   prints 2   context resets 0   changes of revision set 0
ci[2]> :goto 1
at the end of entry 1, reached by replay
ci[2]> :quit
"""
LEDGER_CHANGED = """\
recording  ledger.rec   1 boundary
target     boundary 1 of 1, the program entry: the last change of ledger.total, from 100 to 101
bracket    the initial state; then boundary 1
the last 4 counted operations of boundary 1, of 6 trace events (:trace shows them all)
  ord  position           operation              operands                 value
    0  ledger.ci:25:9     call                   1
    1  ledger.ci:9:11     add.checked.i64        100, 1                   101
    2  ledger.ci:11:15    call                   1
    3  lib/audit.ci:5:11  add.checked.i64        0, 1                     1
    -  ledger.ci:11:15    return                                          1
    -  ledger.ci:25:9     return                                          101
"""
LEDGER_CHANGE = {"binding": "ledger.total", "boundary": "1", "from": {"t": "I64", "v": "100"},
                 "schema": "CINT-CHANGE-1", "to": {"t": "I64", "v": "101"}}
SESSION_ENTRIES = """\
I64 x = 1;
export I64 bump(I64 n) {
    x = x + n;
    return x;
}
:run bump(2)
:run bump(0)
:save saved
:quit
"""
SESSION_CHANGED = """\
recording  session.cirec   3 boundaries
target     boundary 2 of 3, bump(I64 2): the last change of session.x, from 1 to 3
bracket    boundary 1, wb_entry(): done; then boundary 2
the last 1 counted operations of boundary 2, of 1 trace events (:trace shows them all)
  ord  position           operation              operands                 value
    0  session.ci:3:11    add.checked.i64        1, 2                     3
"""
DEPTH_TRACE = """\
the last 8 counted operations of boundary 1, of 8 trace events (:trace shows them all)
  ord  position           operation              operands                 value
    0  control/depth_limit_stack.ci:8:16 add.checked.i64        0, 1                     1
    1  control/depth_limit_stack.ci:8:12 call                   1
    2  control/depth_limit_stack.ci:12:16 add.checked.i64        1, 1                     2
    3  control/depth_limit_stack.ci:12:12 call                   2
    4  control/depth_limit_stack.ci:16:16 add.checked.i64        2, 1                     3
    5  control/depth_limit_stack.ci:16:12 call                   3
    6  control/depth_limit_stack.ci:20:16 add.checked.i64        3, 1                     4
    7  control/depth_limit_stack.ci:20:12 call                   4                        E_DEPTH
"""


def cint_interp(case, wb) -> pathlib.Path:
    """cint-interp, which makes the recordings and the reference receipts, built once per leg."""
    if case.leg not in INTERP:
        out = case.out / "debug-interp"
        out.mkdir(parents=True, exist_ok=True)
        tc = wb.cc.Toolchain(case.leg, "0", "portable", False)
        sources = [wb.ROOT / "interp" / name for name in ("sir_read.c", "sir_exec.c", "cint_interp_main.c")]
        objs = tc.compile(sources + [wb.ROOT / "rt" / n for n in ("cint_rt.c", "cint_state.c", "cint_rec.c")], out,
                          "cint-interp")
        INTERP[case.leg] = tc.link_exe(objs, out / "cint-interp", "cint-interp")
    return INTERP[case.leg]


def expect_fields(expect: pathlib.Path) -> list:
    """The lines of a frozen .expect that a CINT-FAULT-1 object carries (CONF-11)."""
    keep = ("fuel-consumed", "fault.code", "fault.operation", "fault.operand ", "fault.exact", "fault.limit",
            "fault.position", "fault.address", "fault.stack-depth", "fault.stack ")
    return [line for line in expect.read_text(encoding="utf-8").splitlines() if line.startswith(keep)]


def fault_fields(fault: dict, expect: pathlib.Path) -> list:
    """The same lines from a CINT-FAULT-1 object; the frames only where the .expect, of format 2, lists them."""
    typed = lambda v: "none" if v is None else ("%s %s" % (v["t"], v["v"]) if v["t"] != "Z" else v["v"])
    lines = ["fuel-consumed " + fault["noncanonical"]["fuel_consumed"], "fault.code " + fault["code"],
             "fault.operation " + fault["operation"]]
    lines += ["fault.operand " + typed(v) for v in fault["operands"]]
    lines += ["fault.exact " + typed(fault["exact"]), "fault.limit " + typed(fault["limit"]),
              "fault.position %s:%d:%d" % (fault["file"], fault["line"], fault["column"]),
              "fault.address " + ("none" if fault["address"] is None else str(fault["address"])),
              "fault.stack-depth %d" % len(fault["stack"])]
    if "format 2" not in expect.read_text(encoding="utf-8").splitlines():
        return lines
    return lines + ["fault.stack %s:%d:%d" % (s["file"], s["line"], s["column"]) for s in fault["stack"]]


def receipt(path: pathlib.Path) -> dict:
    data = path.read_bytes()
    body = json.loads(data)
    assert data == json.dumps(body, sort_keys=True, separators=(",", ":")).encode("ascii") + b"\n", path
    return body


def check(case, wb, exes) -> int:
    debug, checked = exes["cint-debug"], 0
    interp = cint_interp(case, wb)
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp)

        def ran(argv, status, cwd=work):
            result = wb.run([debug, *argv], cwd=cwd)
            case.assertClean(result, argv)
            case.assertEqual(result.returncode, status, (argv, result.stdout, result.stderr))
            return result.stdout.decode("utf-8"), result.stderr.decode("utf-8")

        made = wb.run([interp, "run", "--record", work / "ledger.rec", wb.ROOT / "interp" / "tests" / "rec" / "sir"])
        case.assertEqual((made.returncode, made.stdout), (0, b"ledger 100\ndeposit 1\n"), made.stderr)
        made = wb.run([interp, "run", "--record", work / "fault.rec", wb.SIR / "control__expect_fault_test_entry"])
        case.assertEqual(made.returncode, 1, made.stderr)
        ledger = (work / "ledger.rec").read_bytes()
        case.assertEqual(ledger.count(b"ledger 100\n"), 1)
        (work / "altered.rec").write_bytes(ledger.replace(b"ledger 100\n", b"ledger 101\n"))
        (work / "cut.rec").write_bytes(ledger[:-1])

        # replay: the listing, and the receipt equal to cint-interp's, for whole and altered recordings
        out, err = ran(["replay", "ledger.rec"], 0)
        case.assertRegex(out, wb.transcript(LEDGER_REPLAY))
        case.assertEqual(err, "")
        for name, stop, status in (("ledger.rec", [], 0), ("ledger.rec", ["--to", "1"], 0), ("fault.rec", [], 0),
                                   ("altered.rec", [], 3)):
            mine, theirs = work / ("debug-%s-%d.json" % (name, len(stop))), work / ("interp-%s.json" % name)
            tool = ["--source-identity", "cd" * 32] if name == "fault.rec" else []   # null when not given
            ran(["replay", name, *stop, *tool, "--receipt", mine], status)
            want = wb.run([interp, "replay", *(["--stop", "1"] if stop else []), *tool, "--receipt", theirs, name],
                          cwd=work)
            case.assertEqual(want.returncode, status, want.stderr)
            a, b = receipt(mine), receipt(theirs)
            case.assertEqual(a["identity"]["tool"], {"interpreter_source_identity": tool[1] if tool else None})
            for key in ("identity", "outcome"):
                case.assertEqual(a[key], b[key], (name, stop, key))
            case.assertEqual(a["observations"]["receipt_identity_sha256"], b["observations"]["receipt_identity_sha256"])
            checked += 1
        out, err = ran(["replay", "--json", "ledger.rec"], 0)
        case.assertEqual((json.loads(out)["identity"], err), (receipt(work / "debug-ledger.rec-0.json")["identity"], ""))
        case.assertEqual(receipt(work / "interp-altered.rec.json")["outcome"]["what"], "effect")
        out, err = ran(["replay", "altered.rec"], 3)
        case.assertIn("\nreplay DIVERGED at event 1: its effect differs from the recording (SPEC-03 H-17)\n"
                      "replay     stopped, 0 of 1 boundary compared with the recording\n", out)
        case.assertEqual(err, "")
        out, err = ran(["replay", "fault.rec"], 0)
        case.assertIn("\n       1      1  the program entry              faulted E_OVERFLOW       1\n", out)
        for argv in (["replay", "cut.rec"], ["locate", "cut.rec"], ["locate", "--json", "cut.rec"]):
            case.assertEqual(ran(argv, 3), ("", "cut.rec is not a complete recording\n"))

        # the read-only session of unit 3b-S, in --out DIR or build/debug beside the recording
        result = wb.run([debug, "--echo", "ledger.rec"], cwd=work, entries=":effects\n:goto 1\n:quit\n")
        case.assertClean(result, "session")
        case.assertEqual((result.returncode, result.stderr), (0, b""))
        case.assertRegex(result.stdout.decode("utf-8"), wb.transcript(LEDGER_SESSION))
        case.assertTrue((work / "build" / "debug").is_dir())
        result = wb.run([debug, "cut.rec", "--out", work / "elsewhere"], cwd=work, entries="")
        case.assertEqual((result.returncode, result.stdout.decode("utf-8").splitlines()[-1]),
                         (3, "workbench: cut.rec is not a complete recording (CINT-REC-1)"))
        checked += 2

        # locate on recordings: a fault, none, a divergence
        out, err = ran(["locate", "fault.rec"], 0)
        case.assertRegex(out, wb.transcript(FAULT_LOCATE))
        out, err = ran(["locate", "--json", "fault.rec"], 0)
        expect = wb.CONFORMANCE / "control" / "expect_fault_test_entry.expect"
        case.assertEqual(fault_fields(json.loads(out), expect), expect_fields(expect))
        out, err = ran(["locate", "ledger.rec"], 3)
        case.assertEqual(out, "recording  ledger.rec   1 boundary\ntarget     none: no boundary ends in a fault or an "
                              "error result; 1 replayed and compared with the recording\n")
        out, err = ran(["locate", "altered.rec"], 3)
        case.assertTrue(out.endswith("\nreplay DIVERGED at event 1: its effect differs from the recording (SPEC-03 "
                                     "H-17)\n"), out)

        # cases: one from cint-interp, one that locate --save-case writes; the receipts equal cint-interp's
        rec = wb.ROOT / "interp" / "tests" / "rec"
        made = wb.run([interp, "run", "--case", work / "ledger.case", "--source", rec, rec / "sir"])
        case.assertEqual((made.returncode, made.stdout), (0, b"ledger 100\ndeposit 1\n"), made.stderr)
        out, err = ran(["locate", rec / "ledger.ci", "--sir", rec / "sir", "--save-case", "saved.case"], 3)
        case.assertTrue(out.startswith("program    %s   evaluator cpu-sir-interp, recorded as the case saved.case\n"
                                       "target     none: " % (rec / "ledger.ci")), out)
        for name in ("ledger.case", "saved.case"):
            out, err = ran(["replay", name, "--receipt", "debug-" + name + ".json"], 0)
            case.assertTrue(out.startswith("case       %s   1 boundary   4 events   mode full   density entry\n"
                                           % name), out)
            want = wb.run([interp, "replay", "--receipt", "interp-" + name + ".json", name], cwd=work)
            case.assertEqual(want.returncode, 0, want.stderr)
            a, b = receipt(work / ("debug-" + name + ".json")), receipt(work / ("interp-" + name + ".json"))
            case.assertEqual((a["identity"], a["outcome"]), (b["identity"], b["outcome"]), name)
            case.assertEqual(a["identity"]["case"]["sha256"], b["identity"]["case"]["sha256"])
            checked += 1
        out, err = ran(["locate", "ledger.rec", "--save-case", "x.case"], 4)
        case.assertEqual(err, "cint debug: error: --root, --cint, --out, --sir, --entry, --args, and --save-case take a "
                              "PROGRAM.ci\n")

        # locate --changed: the backward scan of SPEC-06 10.1 over the recording
        out, err = ran(["locate", "ledger.rec", "--changed", "total"], 0)
        case.assertEqual((out, err), (LEDGER_CHANGED, ""))
        case.assertEqual(ran(["locate", "ledger.rec", "--changed", "nope"], 4),
                         ("", "cint debug: error: no global nope in the recording's state\n"))
        out, err = ran(["locate", "--json", "ledger.rec", "--changed", "total"], 0)   # CINT-CHANGE-1
        case.assertEqual((json.loads(out), err), (LEDGER_CHANGE, ""))
        checked += 2

        # locate on SIR text with --entry: the report against the frozen .expect
        program = ["locate", "view/shape_mismatch_runtime.ci", "--root", wb.CONFORMANCE, "--sir",
                   wb.SIR / "view__shape_mismatch_runtime", "--entry", "run"]
        out, err = ran(program + ["--json", "--receipt", work / "shape.json", "--source-identity", "ef" * 32], 0)
        expect = wb.CONFORMANCE / "view" / "shape_mismatch_runtime.expect"
        case.assertEqual(fault_fields(json.loads(out), expect), expect_fields(expect))
        shape = receipt(work / "shape.json")
        case.assertEqual((shape["outcome"]["status"], shape["identity"]["tool"]),
                         ("ok", {"interpreter_source_identity": "ef" * 32}))
        out, err = ran(program + ["--save-case", "shape.case"], 0)
        case.assertIn("\n12 |     return first_sum(a, b);\n   |            ^ bind.shape\n", out)
        case.assertIn("   = stack-source: view/shape_mismatch_runtime.ci:18:12\n", out)
        out, err = ran(["locate", "--json", "shape.case"], 0)
        case.assertEqual(fault_fields(json.loads(out), expect), expect_fields(expect))

        # the command line
        usage = wb.run([debug, "--help"]).stdout
        plain = [  # (arguments, exit status, standard error after `cint debug: error: `)
            ([], 4, "debug takes a recording, a case, or one of diff, replay, and locate"),
            (["replay"], 4, "diff, replay, and locate take a case or a recording, or for locate a PROGRAM.ci"),
            (["replay", "--to", "0", "ledger.rec"], 4, "--to takes a boundary, 1 or more"),
            (["replay", "ledger.rec", "--to", "2"], 4, "ledger.rec has 1 boundary; --to takes one of them"),
            (["replay", "no.rec"], 5, "cannot read no.rec"),
            (["locate", "ledger.rec", "--entry", "f"], 4,
             "--root, --cint, --out, --sir, --entry, --args, and --save-case take a PROGRAM.ci"),
            (["locate", "a.ci", "--args", "I64 1"], 4, "--args needs --entry NAME"),
            (["locate", "a.ci", "--depth", "0"], 4, "--depth takes a limit of 1 or more"),
            (program[:-2] + ["--entry", "nope"], 4, "view/shape_mismatch_runtime.ci exports no function nope that "
                                                    "an entry can call"),
            (program + ["--args", "I64", "1"], 4, "run takes 0 arguments; --args gives 1"),
            (["locate", "a.ci", "--when", "x > 0"], 5, "--when is not provided yet"),
            (["locate", "ledger.rec", "--changed", "total", "--when", "x"], 4, "--when and --changed exclude each other"),
            (["replay", "ledger.rec", "--echo"], 4, "debug takes the options of --help"),
        ]
        for argv, status, message in plain:
            out, err = ran(argv, status)
            case.assertEqual((out, err), ("", (usage.decode() if argv == [] else "") + "cint debug: error: "
                                          + message + "\n"), argv)
            result = wb.run([debug, "--json", *argv], cwd=work)
            line = json.dumps({"category": "usage" if status == 4 else "environment", "exit_status": status,
                               "message": message, "schema": "CINT-TOOL-1"}, sort_keys=True, separators=(",", ":"))
            case.assertEqual((result.returncode, result.stdout, result.stderr), (status, b"", line.encode() + b"\n"))
        checked += len(plain)

        if wb.ARGS.cint is not None:   # note 6.2: `cint debug locate depth_limit_stack.ci --entry f ...`
            argv = ["locate", "control/depth_limit_stack.ci", "--root", ".", "--cint",
                    pathlib.Path(wb.ARGS.cint).resolve(), "--out", work / "b1", "--entry", "f", "--args", "I64 0"]
            out, err = ran(argv + ["--depth", "4", "--json"], 0, cwd=wb.CONFORMANCE)
            expect = wb.CONFORMANCE / "control" / "depth_limit_stack.expect"
            case.assertEqual(fault_fields(json.loads(out), expect), expect_fields(expect))
            out, err = ran(argv + ["--depth", "4"], 0, cwd=wb.CONFORMANCE)
            case.assertTrue(out.startswith("program    control/depth_limit_stack.ci   evaluator cpu-sir-interp, under a "
                                           "recording\ntarget     boundary 1 of 1, f(I64 0): the first fault, reproduced "
                                           "by replay\nbracket    the initial state; then boundary 1\n"), out)
            case.assertTrue(out.endswith(DEPTH_TRACE), out)
            out, err = ran(argv + ["--depth", "5"], 3, cwd=wb.CONFORMANCE)
            case.assertIn("\ntarget     none: no boundary ends in a fault or an error result", out)
            # a session's recording of three boundaries, of which the second changes x
            (work / "root").mkdir()
            result = wb.run([exes["cint-workbench"], "--cint", pathlib.Path(wb.ARGS.cint).resolve(), "--root",
                             work / "root", "--out", work / "session"], cwd=work, entries=SESSION_ENTRIES)
            case.assertEqual((result.returncode, result.stderr), (0, b""))
            manifest = (work / "saved" / "session.manifest").read_text(encoding="utf-8")
            segment = re.search(r"^segment - 0 0 3 ([0-9a-f]{64})$", manifest, re.M).group(1)
            shutil.copyfile(work / "saved" / "objects" / segment, work / "session.cirec")
            out, err = ran(["locate", "session.cirec", "--changed", "x"], 0)
            case.assertEqual((out, err), (SESSION_CHANGED, ""))
            checked += 4
    return checked + 15
