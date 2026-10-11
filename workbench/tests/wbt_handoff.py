"""`cint <name>` reaches the workbench's executables (SPEC-06 3.1, BX13-15).

With --cint PATH, a directory holds a copy of that `cint`, its toolchain file, and the
executables just built. There `cint debug --help` prints cint-debug's usage, and `cint workbench`
runs a session that B1 compiles from the entries on its standard input, finding the `cint`
beside it with no --cint. None without --cint.
"""
from __future__ import annotations

import pathlib
import shutil
import tempfile

ENTRIES = "I64 x = 40;\nx + 2\n"
SAID = ("cint workbench   profile cint-core-1   backend cpu-sir-interp   new session\n"
        "ci[1]> I64 x = 40;\nx = 40 : I64\nci[2]> x + 2\n= 42 : I64\nci[3]> \n")


def check(case, wb, exes) -> int:
    if wb.ARGS.cint is None:
        return 0
    cint = pathlib.Path(wb.ARGS.cint).resolve()
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp)
        here = work / "bin"
        here.mkdir()
        shutil.copy2(cint, here / cint.name)
        shutil.copy2(cint.with_name("cint.toolchain"), here / "cint.toolchain")
        for name in ("cint-workbench", "cint-debug"):
            exe = pathlib.Path(exes[name])
            shutil.copy2(exe, here / (name + exe.suffix))
        result = wb.run([here / cint.name, "debug", "--help"])
        case.assertClean(result, "cint debug --help")
        case.assertEqual((result.returncode, result.stderr), (0, b""))
        case.assertEqual(result.stdout, wb.run([exes["cint-debug"], "--help"]).stdout)
        (work / "root").mkdir()
        result = wb.run([here / cint.name, "workbench", "--root", work / "root", "--out", work / "session", "--echo"],
                        cwd=work, entries=ENTRIES)
        case.assertClean(result, "cint workbench")
        case.assertEqual((result.returncode, result.stderr, result.stdout.decode("utf-8")), (0, b"", SAID))
    return 2
