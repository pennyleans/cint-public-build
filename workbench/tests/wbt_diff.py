"""`cint debug diff`, its receipt, and the debugger's qualification test (box 14 unit 3b-Q; SPEC-06
9.3, 9.4, and 12.2; sections 7.1 and 7.3 of the box 14 note).

Run by workbench/tests/test_workbench.py for each configuration it builds (its units()). The
cases are of module/state_two_modules, whose `run` adds 2 to `seen`, counts a call in
`module.lib.tally.calls`, and returns `seen`, from its SIR text in interp/tests/sir/, three calls
each, recorded by wb-qualify (workbench/tests/wb_qualify.c):

- the clean case compared on cpu-sir-interp with itself: `agree`, exit 0, with the receipt in
  canonical JSON, its identity recomputed, the same identity on a second run, and --json printing
  the receipt;
- the qualification test of note section 7.3: wb-qualify's candidate wraps cpu-sir-interp and
  adds 1 at boundary 2 to the first global of the state in canonical order, `calls`. diff
  reports `value-difference` at boundary 2 with the binding and both values, and the rerun
  reproduces it; when the change does not repeat on the rerun, `unreproduced`. When the change is
  a fault record instead, `fault-difference` at boundary 2. A case recorded with the same change
  replays to `DIVERGED` at boundary 2;
- a case of memory/arena_alloc_view_sum, whose module-level arena holds 1, 2, 3, 4, 0, 0, 100
  after its first call: with element 0 of the arena changed at boundary 1, diff localizes the
  difference to that element, with both values as bytes;
- a case cut short is `invalid-case`; a case of the program entry, made by cint-interp, agrees on
  cpu-sir-interp and is refused on cpu-c17; the usage and environment errors, with their
  CINT-TOOL-1 lines under --json;
- with --cint PATH, the same with cpu-c17 built by that cint as the reference: `agree` for the
  clean case, and `value-difference`, `unreproduced`, and `fault-difference` against the changed
  candidate; `refused` for a case of errors/combined_try_converts_tag, whose `run` returns an
  error union that a cpu-c17 candidate does not call (box 14 note section 6.5); each receipt's
  observations with both sides' build configurations; and the session's `:run --check` (box 14
  note section 5.2) with the same change at entry 4, through the hook wb_check_alter, in both
  directions: the :run stops there, and the session goes on from the state of the backend in
  force.

Element by element localization within a pool, which the arena's shares, is not reached.
"""
from __future__ import annotations

import hashlib
import importlib.util
import json
import os
import pathlib
import tempfile

DEBUG = []   # wbt_debug.py, whose cint-interp makes the case of the program entry

AGREE = """\
case       clean.case   3 boundaries   density entry
reference  {ref}
candidate  {cand}
boundaries 3 of 3 compared
outcome    agree
"""
CHANGED = """\
case       clean.case   3 boundaries   density entry
reference  {ref}
candidate  cpu-sir-interp
boundaries 2 of 3 compared
outcome    value-difference
  first differing boundary   2, run()
  binding                    module.lib.tally.calls
  values
    {a:<16} I64 2
    {z:<16} I64 3
  candidate rerun            difference reproduced
"""
CHECKED = """\
I64 x = 1;
export I64 bump(I64 n) {{
    x = x + n;
    return x * 2;
}}
{backend}:run bump(1) 3 --check {other}
x
:quit --yes
"""
SESSION = """\
cint workbench   profile cint-core-1   backend cpu-sir-interp   new session
ci[1]> I64 x = 1;
x = 1 : I64
ci[2]> export I64 bump(I64 n) {{
..>     x = x + n;
..>     return x * 2;
..> }}
bump : (I64) -> I64   revision session@r2
{backend}ci[3]> :run bump(1) 3 --check {other}
cpu-c17: revision <h> built as a library in <n> ms
= 4 : I64
check: entry 4 differs: its state on {force} is not its state on {other}
2 of 3 calls made
ci[5]> x
{built}= {x} : I64
ci[6]> :quit --yes
"""
UNREPRODUCED = """\
case       clean.case   3 boundaries   density entry
reference  {ref}
candidate  cpu-sir-interp
boundaries 2 of 3 compared
outcome    unreproduced
  first differing boundary   2, run()
  candidate rerun            the difference did not come back
"""


def receipt(case, path: pathlib.Path, data: bytes = None) -> dict:
    """The receipt at path, or in data, checked: canonical JSON and its recomputed identity."""
    data = path.read_bytes() if data is None else data
    body = json.loads(data)
    case.assertEqual(data, json.dumps(body, sort_keys=True, separators=(",", ":")).encode("ascii") + b"\n")
    core = json.dumps({"identity": body["identity"], "outcome": body["outcome"]}, sort_keys=True,
                      separators=(",", ":")).encode("ascii")
    case.assertEqual(body["observations"]["receipt_identity_sha256"], hashlib.sha256(core).hexdigest())
    case.assertEqual(body["identity"]["schema"], "cint-receipt-1/diff")
    return body


def check(case, wb, exes) -> int:
    debug, qualify, checked = exes["cint-debug"], exes["wb-qualify"], 0
    sir = wb.SIR / "module__state_two_modules"
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp)

        def ran(argv, status, exe=debug):
            result = wb.run([exe, *argv], cwd=work)
            case.assertClean(result, argv)
            case.assertEqual(result.returncode, status, (argv, result.stdout, result.stderr))
            return result.stdout.decode("utf-8"), result.stderr.decode("utf-8")

        for name, change in (("clean.case", []), ("altered.case", ["2"])):
            case.assertEqual(ran(["record", sir, wb.CONFORMANCE, "run", "3", name, *change], 0, qualify), ("", ""))
        clean = (work / "clean.case").read_bytes()

        # agree, and the receipt
        interp = ["--ref", "cpu-sir-interp", "--cand", "cpu-sir-interp"]
        out, err = ran(["diff", "clean.case", *interp, "--receipt", "a.json"], 0)
        case.assertEqual((out, err), (AGREE.format(ref="cpu-sir-interp", cand="cpu-sir-interp"), ""))
        body = receipt(case, work / "a.json")
        case.assertEqual(body["identity"]["case"], {"bytes": str(len(clean)), "sha256": hashlib.sha256(clean).hexdigest()})
        case.assertEqual(body["identity"]["backend"], {"candidate": {"id": "cpu-sir-interp"},
                                                       "reference": {"id": "cpu-sir-interp"}})
        case.assertEqual(body["outcome"], {"binding": None, "boundaries": "3", "boundary": None, "candidate": None,
                                           "compared": "3", "differs": [], "element": None, "outcome": "agree",
                                           "reference": None, "rerun": None, "why": None})
        # the tools' source identities (SPEC-06 12.2): null unless given
        case.assertEqual(body["identity"]["tool"], {"compiler_source_identity": None,
                                                    "interpreter_source_identity": None,
                                                    "workbench_source_identity": None})
        tools = {"compiler": "ab" * 32, "interpreter": "cd" * 32, "workbench": wb.source_identity()}
        ran(["diff", "clean.case", *interp, "--receipt", "t.json",
             *[w for k, v in tools.items() for w in ("--%s-identity" % k, v)]], 0)
        case.assertEqual(receipt(case, work / "t.json")["identity"]["tool"],
                         {k + "_source_identity": v for k, v in tools.items()})
        out, err = ran(["diff", "--json", "clean.case", *interp, "--receipt", "b.json"], 0)
        again = receipt(case, work / "b.json")
        printed = receipt(case, work / "b.json", out.encode("ascii"))
        for other in (again, printed):
            case.assertEqual(other["observations"]["receipt_identity_sha256"],
                             body["observations"]["receipt_identity_sha256"])
        checked += 4

        # note 7.3: a changed element at boundary 2, reproduced and not; replay of a changed recording
        for ref, cint in (("cpu-sir-interp", "-"),) + ((("cpu-c17", wb.ARGS.cint),) if wb.ARGS.cint else ()):
            cint = str(pathlib.Path(cint).resolve()) if cint != "-" else cint
            labels = ("reference", "candidate") if ref == "cpu-sir-interp" else (ref, "cpu-sir-interp")
            argv = ["diff", "clean.case", ref, "cpu-sir-interp", work / "b1", cint]
            out, err = ran(argv + ["v.json", "2"], 3, qualify)
            case.assertEqual((out, err), (CHANGED.format(ref=ref, a=labels[0], z=labels[1]), ""))
            case.assertEqual(receipt(case, work / "v.json")["outcome"], {
                "binding": "module.lib.tally.calls", "boundaries": "3", "boundary": "2",
                "candidate": "I64 3", "compared": "2", "differs": ["state"], "element": None,
                "outcome": "value-difference", "reference": "I64 2", "rerun": "reproduced", "why": None})
            out, err = ran(argv + ["u.json", "2", "once"], 3, qualify)
            case.assertEqual((out, err), (UNREPRODUCED.format(ref=ref), ""))
            case.assertEqual(receipt(case, work / "u.json")["outcome"], {
                "binding": None, "boundaries": "3", "boundary": "2", "candidate": None, "compared": "2",
                "differs": [], "element": None, "outcome": "unreproduced", "reference": None,
                "rerun": "not reproduced", "why": None})
            out, err = ran(argv + ["f.json", "2", "fault"], 3, qualify)
            case.assertIn("\noutcome    fault-difference\n  first differing boundary   2, run()\n", out)
            case.assertIn("\nfault[E_ASSERT]: assertion failed\n", out)
            case.assertTrue(out.endswith("  candidate rerun            difference reproduced\n"), out)
            case.assertEqual(receipt(case, work / "f.json")["outcome"], {
                "binding": None, "boundaries": "3", "boundary": "2", "candidate": None, "compared": "2",
                "differs": ["fault", "state"], "element": None, "outcome": "fault-difference", "reference": None,
                "rerun": "reproduced", "why": None})
            checked += 3

        # an arena's element, localized
        arena = wb.WB / "tests" / "golden" / "sir" / "memory__arena_alloc_view_sum"
        case.assertEqual(ran(["record", arena, wb.CONFORMANCE, "run", "2", "arena.case"], 0, qualify), ("", ""))
        out, err = ran(["diff", "arena.case", "cpu-sir-interp", "cpu-sir-interp", work / "b1", "-", "e.json", "1",
                        "arena"], 3, qualify)
        case.assertIn("\n  binding                    arena 1\n  first element              0\n  values\n"
                      "    reference        bytes 0100000000000000\n    candidate        bytes 0200000000000000\n", out)
        case.assertEqual(receipt(case, work / "e.json")["outcome"], {
            "binding": "arena 1", "boundaries": "2", "boundary": "1", "candidate": "bytes 0200000000000000",
            "compared": "1", "differs": ["state"], "element": "0", "outcome": "value-difference",
            "reference": "bytes 0100000000000000", "rerun": "reproduced", "why": None})
        checked += 1
        out, err = ran(["replay", "altered.case", "--receipt", "r.json"], 3)
        case.assertIn("\nreplay DIVERGED at event 3: its state differs from the recording (SPEC-03 H-17)\n"
                      "replay     stopped, 1 of 3 boundaries compared with the recording\n", out)
        replayed = json.loads((work / "r.json").read_bytes())["outcome"]
        case.assertEqual((replayed["status"], replayed["event"], replayed["boundaries"]), ("diverged", "3", "1"))
        checked += 1

        # a case cut short; a case of the program entry; the command line
        (work / "cut.case").write_bytes(clean[:-1])
        out, err = ran(["diff", "cut.case", *interp, "--receipt", "c.json"], 3)
        case.assertTrue(out.endswith("boundaries 0 of 0 compared\noutcome    invalid-case: not a complete case or "
                                     "recording\n"), out)
        cut = receipt(case, work / "c.json")
        case.assertEqual((cut["outcome"]["outcome"], cut["identity"]["identities"]),
                         ("invalid-case", {"revision": None, "source_map": None}))
        if not DEBUG:
            spec = importlib.util.spec_from_file_location("wbt_debug", pathlib.Path(__file__).with_name("wbt_debug.py"))
            DEBUG.append(importlib.util.module_from_spec(spec))
            spec.loader.exec_module(DEBUG[0])
        rec = wb.ROOT / "interp" / "tests" / "rec"
        made = wb.run([DEBUG[0].cint_interp(case, wb), "run", "--case", work / "ledger.case", "--source", rec, rec / "sir"])
        case.assertEqual((made.returncode, made.stdout), (0, b"ledger 100\ndeposit 1\n"), made.stderr)
        out, err = ran(["diff", "ledger.case", *interp], 0)
        case.assertEqual((out, err), (AGREE.format(ref="cpu-sir-interp", cand="cpu-sir-interp")
                                      .replace("clean.case   3 boundaries", "ledger.case   1 boundary")
                                      .replace("3 of 3", "1 of 1"), ""))
        checked += 2
        usage = [  # (arguments, exit status, standard error after `cint debug: error: `)
            (["diff"], 4, "diff, replay, and locate take a case or a recording, or for locate a PROGRAM.ci"),
            (["diff", "clean.case", "--ref", "cpu-sir-interp", "--cand", "gpu-cuda"], 5,
             "gpu-cuda is compared once there is a gpu-cuda library: `cint build --lib` builds cpu-c17 only"),
            (["diff", "clean.case", "--ref", "nope"], 4, "--ref and --cand take cpu-c17 or cpu-sir-interp, not nope"),
            (["diff", "clean.case", "--to", "2"], 4, "debug takes the options of --help"),
            (["diff", "clean.case", "--workbench-identity", "AB" * 32], 4,
             "a source identity is 64 lowercase hexadecimal digits"),
            (["replay", "clean.case", "--source-identity", "a" * 63], 4,
             "a source identity is 64 lowercase hexadecimal digits"),
            (["diff", "clean.case", "--source-identity", "a" * 64], 4, "debug takes the options of --help"),
            (["diff", "no.case"], 5, "cannot read no.case"),
            (["diff", "ledger.case"], 5, "the case calls the program entry, which a cpu-c17 library does not have; "
                                         "compare it with --ref cpu-sir-interp"),
        ]
        if wb.ARGS.cint is None:   # cpu-c17 by default, with no cint beside cint-debug
            usage.append((["diff", "clean.case"], 5, "cannot read the toolchain file beside %s; give --cint PATH"
                          % debug.with_name("cint" + debug.suffix)))   # cint.exe on Windows
        for argv, status, message in usage:
            out, err = ran(argv, status)
            case.assertEqual((out, err), ("", "cint debug: error: " + message + "\n"), argv)
            result = wb.run([debug, "--json", *argv], cwd=work)
            line = json.dumps({"category": "usage" if status == 4 else "environment", "exit_status": status,
                               "message": message, "schema": "CINT-TOOL-1"}, sort_keys=True, separators=(",", ":"))
            case.assertEqual((result.returncode, result.stdout, result.stderr), (status, b"", line.encode() + b"\n"))
        checked += len(usage)

        if wb.ARGS.cint is not None:   # cpu-c17, built by B1, as the default reference
            cint = str(pathlib.Path(wb.ARGS.cint).resolve())
            out, err = ran(["diff", "clean.case", "--cint", cint, "--out", work / "c17", "--receipt", "d.json"], 0)
            case.assertEqual((out, err), (AGREE.format(ref="cpu-c17", cand="cpu-sir-interp"), ""))
            body = receipt(case, work / "d.json")
            case.assertEqual(body["identity"]["backend"], {"candidate": {"id": "cpu-sir-interp"},
                                                           "reference": {"id": "cpu-c17"}})
            built = body["observations"]["backend"]   # outside the identity (SPEC-06 12.2)
            case.assertEqual(sorted(built["reference"]), ["cc_version", "flags", "leg"])
            case.assertEqual(built["reference"]["leg"], wb.ARGS.leg or case.leg)
            case.assertEqual(sorted(built["candidate"]), ["compiler"])
            out, err = ran(["diff", "clean.case", "--cand", "cpu-c17", "--cint", cint, "--out", work / "c17"], 0)
            case.assertEqual((out, err), (AGREE.format(ref="cpu-c17", cand="cpu-c17"), ""))
            # refused: an export whose signature holds an error set, which cpu-c17 does not call
            errors = wb.WB / "tests" / "golden" / "sir" / "errors__combined_try_converts_tag"
            case.assertEqual(ran(["record", errors, wb.CONFORMANCE, "run", "1", "error.case"], 0, qualify), ("", ""))
            out, err = ran(["diff", "error.case", "--ref", "cpu-sir-interp", "--cand", "cpu-c17", "--cint", cint,
                            "--out", work / "c17e", "--receipt", "x.json"], 3)
            case.assertEqual((out, err), ("case       error.case   1 boundary   density entry\nreference  cpu-sir-interp\n"
                                          "candidate  cpu-c17\nboundaries 0 of 1 compared\noutcome    refused: cpu-c17 "
                                          "does not call run: it has a struct, enum, or error set\n", ""))
            case.assertEqual(receipt(case, work / "x.json")["outcome"]["outcome"], "refused")
            checked += 3

            # the session's :run --check with the same change at entry 4, in both directions: the
            # :run stops there, and the session goes on from the state of the backend in force
            (work / "root").mkdir()
            os.environ["WB_QUALIFY_SESSION"] = "4"
            try:
                for force, other, x in (("cpu-sir-interp", "cpu-c17", 4), ("cpu-c17", "cpu-sir-interp", 3)):
                    backend = ":backend cpu-c17\n" if force == "cpu-c17" else ""
                    result = wb.run([qualify, "--cint", cint, "--root", work / "root", "--echo", "--out",
                                     work / ("session-" + force)], cwd=work,
                                    entries=CHECKED.format(backend=backend, other=other))
                    case.assertClean(result, "check " + force)
                    case.assertEqual((result.returncode, result.stderr), (0, b""))
                    said = SESSION.format(
                        backend="ci[3]> :backend cpu-c17\nbackend: cpu-c17; each revision is built as a library, "
                                "entries on it are not recorded, and :inspect and the watches stay on "
                                "cpu-sir-interp\n" if backend else "",
                        other=other, force=force, x=x,
                        built="cpu-c17: revision <h> built as a library in <n> ms\n" if backend else "")
                    case.assertRegex(result.stdout.decode("utf-8"), wb.transcript(said))
                    checked += 1
            finally:
                del os.environ["WB_QUALIFY_SESSION"]
    return checked
