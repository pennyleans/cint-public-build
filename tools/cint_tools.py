"""Build cint-interp and the workbench (cint-workbench, cint-repl, cint-explain, cint-debug) for one leg.

    python3 tools/cint_tools.py --leg gcc|clang|apple-clang|msvc [--out DIR]

The executables go to DIR, by default <build>/boot/<leg>, where tools/cint_bootstrap.py writes
`cint`: the workbench runs the `cint` beside it unless given --cint PATH. <build> is CINT_BUILD,
or cint-build in the system temporary directory. Each is built at -O2 (/O2) with the portable
helpers by the build functions of interp/tests/test_interp.py and
workbench/tests/test_workbench.py, so it is the executable those tests check; their objects go
under DIR/tools-build. Never run with `python -O`.
"""

import argparse
import os
import pathlib
import shutil
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "interp" / "tests"))
sys.path.insert(0, str(ROOT / "workbench" / "tests"))

import test_interp  # noqa: E402
import test_workbench  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--leg", required=True, choices=["gcc", "clang", "apple-clang", "msvc"])
    parser.add_argument("--out")
    args = parser.parse_args()
    build = os.environ.get("CINT_BUILD") or os.path.join(tempfile.gettempdir(), "cint-build")
    out = pathlib.Path(args.out) if args.out else pathlib.Path(build) / "boot" / args.leg
    work = out / "tools-build"
    shutil.rmtree(work, ignore_errors=True)
    (work / "workbench").mkdir(parents=True)
    test_interp.build(args.leg, work / "interp", False)
    built = [path for label, path in test_interp.INTERPS if label == "O2"]
    exes = test_workbench.build(args.leg, "2", False, work / "workbench")
    built += [pathlib.Path(exes[name]) for name in test_workbench.EXECUTABLES]
    for path in built:
        shutil.copy2(path, out / pathlib.Path(path).name)
        print(out / pathlib.Path(path).name)
    return 0


if __name__ == "__main__":
    assert __debug__, "run without -O"
    sys.exit(main())
