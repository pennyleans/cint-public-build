"""The workbench's golden files (box 14 unit 6; SPEC-09 section 14, T4; box 14 default BX14-21).

Run by workbench/tests/test_workbench.py for each configuration it builds (its units()).
workbench/tests/golden/ holds what the workbench prints for fixed inputs. Each file is frozen
once and changed only in a dedicated change, like an `.expect` file:

- sir/NAME/: the SIR text (the *.sites files) that B1 wrote for program NAME of PROGRAMS, its
  source under programs/ or conformance/, and in sir/REVISIONS the revision identity that B1
  compiled into the program. These are inputs. They are not written again when B1 changes, so
  the files below test the workbench, not the compiler; test_workbench.py reads what B1 writes
  now. When B1's SIR text changes form and the reader no longer takes a program's sir/NAME, the
  change that does so deletes sir/NAME, its REVISIONS line, and the explain and locate files of
  NAME, and writes them again with --write.
- explain/NAME.txt and NAME.json: `cint explain` of the program's root module, and under
  --json, from that SIR text (SPEC-06 section 7; box 14 default BX14-12). The JSON carries the
  revision of sir/REVISIONS.
- locate/NAME.txt: `cint debug locate` of an entry of the program that faults (LOCATE): the
  fault report (SPEC-06 8.4; box 14 ruling BX14-05 A and default BX14-13), the boundary
  bracket, and the last counted operations (default BX14-14).
- sessions/NAME.in and NAME.txt: a session's entries and its transcript under --echo (SPEC-06
  section 16 as adapted to column 02, box 14 ruling BX14-06 A). B1 compiles each entry, so
  these run only with --cint, and sessions B and C (240 calls of a 64 x 64 kernel each) only
  in the -O2 builds. A 64-digit identity in a transcript is written <revision>, as SPEC-06 16
  writes a placeholder, since it changes whenever B1's SIR text does.
- sessions/NAME.locate.txt: `cint debug locate` of the case that session NAME saves (CASES),
  which stands in for SPEC-06 16.3's `:trace` of the faulting work-item.

`python workbench/tests/wbt_golden.py --write --cint PATH [--leg LEG]` writes every file that
is missing, with the workbench built at -O2 under --out and with that B1, and never rewrites
one: to change a golden file, delete it in a dedicated change and write it again.
"""
from __future__ import annotations

import json
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
GOLDEN = HERE / "golden"
PROGRAMS = {   # NAME: (root under the repository, its modules, the root module last)
    "money": ("workbench/tests/golden/programs", ["money.ci"]),
    "field": ("workbench/tests/golden/programs", ["field.ci"]),
    "kernel__heat_e2_narrowing": ("conformance", ["kernel/heat_e2_narrowing.ci"]),
    "view__index_checked_before_value": ("conformance", ["view/index_checked_before_value.ci"]),
    "struct__array_of_structs": ("conformance", ["struct/array_of_structs.ci"]),
    "memory__arena_alloc_view_sum": ("conformance", ["memory/arena_alloc_view_sum.ci"]),
    "errors__combined_try_converts_tag": ("conformance", ["errors/combined_try_converts_tag.ci"]),
    "module__import_plain": ("conformance", ["module/lib/plain_math.ci", "module/import_plain.ci"]),
}
LOCATE = {   # NAME: the entry that faults
    "money": "overflow",
    "kernel__heat_e2_narrowing": "run",
    "view__index_checked_before_value": "run",
}
SESSIONS = ("a", "b", "c")
SLOW = ("b", "c")   # run only in the -O2 builds
CASES = {"c": "overflow.cicase"}   # NAME: the case that session NAME saves, for sessions/NAME.locate.txt
# What changes with B1's SIR text in a transcript, and how a golden transcript writes it.
VARYING = [(re.compile(r"\b[0-9a-f]{64}\b"), "<revision>"),
           (re.compile(r"revision [0-9a-f]{8} built as a library in [0-9]+ ms"), "revision <h> built as a library in <n> ms")]


def normal(text: str) -> str:
    for pattern, written in VARYING:
        text = pattern.sub(written, text)
    return text


def explain(wb, exes, name, json_mode):
    root, modules = PROGRAMS[name]
    return wb.run([exes["cint-explain"], *(["--json"] if json_mode else []), "--sir", GOLDEN / "sir" / name,
                   "--root", wb.ROOT / root, modules[-1]])


def locate(wb, exes, name, out):
    root, modules = PROGRAMS[name]
    return wb.run([exes["cint-debug"], "locate", "--sir", GOLDEN / "sir" / name, "--root", wb.ROOT / root,
                   "--out", out, modules[-1], "--entry", LOCATE[name]])


def session(wb, exes, name, cint, work):
    (work / "root").mkdir(parents=True)
    return wb.run([exes["cint-workbench"], "--cint", cint, "--root", work / "root", "--echo", "--out", work / "session"],
                  cwd=work, entries=(GOLDEN / "sessions" / (name + ".in")).read_text(encoding="utf-8"))


def sessions(exes) -> list:
    """The sessions this configuration runs: B and C only in the -O2 builds."""
    return [name for name in SESSIONS if name not in SLOW or pathlib.Path(exes["test_wb"]).parent.name.startswith("O2")]


def locate_case(wb, exes, name, work):
    return wb.run([exes["cint-debug"], "locate", CASES[name]], cwd=work)   # relative: no temporary path


def revisions() -> dict:
    lines = (GOLDEN / "sir" / "REVISIONS").read_text(encoding="ascii").splitlines()
    return dict(line.split() for line in lines)


def check(case, wb, exes) -> int:
    """Each golden file against what the workbench prints now."""
    checked = 0
    case.assertEqual(sorted(revisions()), sorted(PROGRAMS))
    case.assertEqual(sorted(p.name for p in (GOLDEN / "sir").iterdir() if p.is_dir()), sorted(PROGRAMS))
    case.assertEqual(sorted(p.name for p in (GOLDEN / "explain").iterdir()),
                     sorted(name + suffix for name in PROGRAMS for suffix in (".json", ".txt")))
    case.assertEqual(sorted(p.name for p in (GOLDEN / "locate").iterdir()), sorted(name + ".txt" for name in LOCATE))
    case.assertEqual(sorted(p.name for p in (GOLDEN / "sessions").iterdir()),
                     sorted([name + suffix for name in SESSIONS for suffix in (".in", ".txt")]
                            + [name + ".locate.txt" for name in CASES]))
    for name in PROGRAMS:
        for json_mode, suffix in ((False, ".txt"), (True, ".json")):
            result = explain(wb, exes, name, json_mode)
            case.assertClean(result, name)
            case.assertEqual((result.returncode, result.stderr), (0, b""), name)
            case.assertEqual(result.stdout, (GOLDEN / "explain" / (name + suffix)).read_bytes(), name + suffix)
            checked += 1
        case.assertEqual(json.loads(result.stdout)["revision"], revisions()[name], name)
    with tempfile.TemporaryDirectory() as tmp:
        for name in LOCATE:
            result = locate(wb, exes, name, pathlib.Path(tmp) / name)
            case.assertClean(result, name)
            case.assertEqual((result.returncode, result.stderr), (0, b""), name)
            case.assertEqual(result.stdout, (GOLDEN / "locate" / (name + ".txt")).read_bytes(), name)
            checked += 1
        if wb.ARGS.cint is not None:
            cint = pathlib.Path(wb.ARGS.cint).resolve()
            for name in sessions(exes):
                work = pathlib.Path(tmp) / ("session-" + name)
                result = session(wb, exes, name, cint, work)
                case.assertClean(result, name)
                case.assertEqual((result.returncode, result.stderr), (0, b""), name)
                case.assertEqual(normal(result.stdout.decode("utf-8")),
                                 (GOLDEN / "sessions" / (name + ".txt")).read_text(encoding="utf-8"), name)
                checked += 1
                if name in CASES:
                    result = locate_case(wb, exes, name, work)
                    case.assertClean(result, name)
                    case.assertEqual((result.returncode, result.stderr), (0, b""), name)
                    case.assertEqual(normal(result.stdout.decode("utf-8")),
                                     (GOLDEN / "sessions" / (name + ".locate.txt")).read_text(encoding="utf-8"), name)
                    checked += 1
    return checked


def write(path: pathlib.Path, data: bytes) -> None:
    if path.exists():
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    print("wrote", path.relative_to(HERE))


def write_missing(wb, exes, cint: pathlib.Path) -> None:
    """Every golden file that is missing; none is rewritten."""
    known = revisions() if (GOLDEN / "sir" / "REVISIONS").exists() else {}
    with tempfile.TemporaryDirectory() as tmp:
        for name, (root, modules) in PROGRAMS.items():
            if name in known:
                continue
            out = pathlib.Path(tmp) / name
            subprocess.run([str(cint), "emit-c", "--root", str(wb.ROOT / root), "--out", str(out), *modules], check=True)
            stage = pathlib.Path(tmp) / (name + ".stage-0")
            if (GOLDEN / "sir" / name).exists():
                shutil.rmtree(GOLDEN / "sir" / name)   # a program not in REVISIONS was not written in full
            for path in sorted(stage.rglob("*.sites")):
                write(GOLDEN / "sir" / name / path.relative_to(stage), path.read_bytes())
            m = re.search(rb"static const uint8_t cg_revision\[32\] = \{\n(.*?)\n\};",
                          (stage / "cint-program.c").read_bytes(), re.S)
            known[name] = "".join("%02x" % int(x, 0) for x in m.group(1).decode("ascii").replace("\n", " ").split(",")
                                  if x.strip())
            (GOLDEN / "sir" / "REVISIONS").write_bytes("".join("%s %s\n" % item for item in sorted(known.items()))
                                                       .encode("ascii"))
        for name in PROGRAMS:
            for json_mode, suffix in ((False, ".txt"), (True, ".json")):
                result = explain(wb, exes, name, json_mode)
                if result.returncode == 0:
                    write(GOLDEN / "explain" / (name + suffix), result.stdout)
        for name in LOCATE:
            result = locate(wb, exes, name, pathlib.Path(tmp) / ("locate-" + name))
            if result.returncode == 0:
                write(GOLDEN / "locate" / (name + ".txt"), result.stdout)
        for name in SESSIONS:
            work = pathlib.Path(tmp) / ("session-" + name)
            result = session(wb, exes, name, cint, work)
            if result.returncode == 0 and result.stderr == b"":
                write(GOLDEN / "sessions" / (name + ".txt"), normal(result.stdout.decode("utf-8")).encode("utf-8"))
            result = locate_case(wb, exes, name, work) if name in CASES else None
            if result is not None and result.returncode == 0 and result.stderr == b"":
                write(GOLDEN / "sessions" / (name + ".locate.txt"),
                      normal(result.stdout.decode("utf-8")).encode("utf-8"))


if __name__ == "__main__":
    import argparse
    sys.path.insert(0, str(HERE))
    import test_workbench
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--leg", choices=["msvc", "gcc", "clang", "apple-clang"], default="gcc")
    parser.add_argument("--out")
    parser.add_argument("--cint", required=True)
    parser.add_argument("--write", action="store_true", required=True, help="write the golden files that are missing")
    known = parser.parse_args()
    test_workbench.ARGS.leg, test_workbench.ARGS.cint = known.leg, known.cint
    outdir = pathlib.Path(known.out) if known.out else test_workbench.default_out(known.leg) / "golden"
    outdir.mkdir(parents=True, exist_ok=True)
    write_missing(test_workbench, test_workbench.build(known.leg, "2", False, outdir), pathlib.Path(known.cint).resolve())
