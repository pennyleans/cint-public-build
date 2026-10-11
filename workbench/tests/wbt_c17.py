"""cpu-c17 through libraries (box 14 unit 5; section 5.2 of the box 14 note, default BX14-08).

Run by workbench/tests/test_workbench.py for each configuration it builds (its units()), or on
its own before that runner finds it:

    python workbench/tests/wbt_c17.py [--leg msvc|gcc|clang|apple-clang] [--out DIR] [--cint PATH]

Each configuration builds workbench/tests/c17_host.c, which drives workbench/wb_c17.c and links
no runtime of its own, and three libraries of workbench/tests/c17_lib.c, a hand-written stand-in
for what cintc writes for bank.ci: one with the toolchain's own library runtime (rt/cint_rt.c),
one that also carries rt/cint_state.c and rt/cint_rec.c, as wb_c17_build links them, and one
without the reflection table. On every leg, the Windows CI leg's LoadLibraryW among them, the
host loads each, lists the exports with their signatures, makes a context through the library's
runtime, and calls the exports through their cx wrappers: values, a print, module state kept
between entries, faults with their records, a refused Bool, an export it cannot call, and the
fuel allowance. The library with the state code takes a checkpoint, restores it into a new
context with the same state hash, and continues from it; the others refuse a checkpoint, saying
why. Files that do not load, a library without the table, and a cint without a toolchain file are
refused.

With --cint PATH, the host also builds bank.ci (the source in c17_lib.c's comment) through
wb_c17_build with that cint, and the library it writes must give the stand-in's transcript, its
checkpoints included.

Build outputs go under the configuration's directory, <build>/workbench/<leg>/<config>/c17/.
"""
from __future__ import annotations

import argparse
import importlib.util
import pathlib
import re
import shutil
import sys
import unittest

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]

# The calls each library answers alike: the stand-in and what B1 builds from its source.
CALLS = """\
exports
context
call add 2 3
call add 9223372036854775807 1
call deposit 7
call balance
call neg8 -128
call neg8 -5
call flip true
call flip 2
call twice16 40000
call twice16 300
call sum3 1
fuel 0
call add 1 1
fuel none
call balance
"""
CALLS_SAID = """\
> exports
  add (I64, I64) -> I64   pure
  deposit (I64) -> void   observe
  balance () -> I64   pure
  neg8 (I8) -> I8   pure
  flip (Bool) -> Bool   pure
  twice16 (U16) -> U16   pure
  sum3 (...)   not callable: it takes an array
> context
context
> call add 2 3
= 5 : I64   fuel 1
> call add 9223372036854775807 1
fault[E_OVERFLOW] add.checked.i64 at bank.ci:5:14   fuel 1
> call deposit 7
out: deposit
done   fuel 1
> call balance
= 7 : I64   fuel 1
> call neg8 -128
fault[E_OVERFLOW] neg.checked.i8 at bank.ci:18:12   fuel 1
> call neg8 -5
= 5 : I8   fuel 1
> call flip true
= false : Bool   fuel 1
> call flip 2
refused   reason 11
> call twice16 40000
fault[E_OVERFLOW] mul.checked.u16 at bank.ci:26:14   fuel 1
> call twice16 300
= 600 : U16   fuel 1
> call sum3 1
not callable: it takes an array
> fuel 0
> call add 1 1
fault[E_FUEL] fuel.charge at bank.ci:4:12   fuel 0
> fuel none
> call balance
= 7 : I64   fuel 1
"""
# A checkpoint after the calls above: refused without rt/cint_state.c, else kept, and restored
# into a new context that continues from it.
STATE = "checkpoint\ncall deposit 1\ncall balance\nrestore\ncall balance\ncall deposit 2\ncall balance\n"
STATE_NONE = """\
> checkpoint
refused: this library's runtime has no checkpoints: it was built without rt/cint_state.c, which wb_c17_build links
> call deposit 1
out: deposit
done   fuel 1
> call balance
= 8 : I64   fuel 1
> restore
refused: this library's runtime has no checkpoints: it was built without rt/cint_state.c, which wb_c17_build links
> call balance
= 8 : I64   fuel 1
> call deposit 2
out: deposit
done   fuel 1
> call balance
= 10 : I64   fuel 1
"""
STATE_KEPT = """\
> checkpoint
checkpoint   state <h>
> call deposit 1
out: deposit
done   fuel 1
> call balance
= 8 : I64   fuel 1
> restore
restored   state <h>
> call balance
= 7 : I64   fuel 1
> call deposit 2
out: deposit
done   fuel 1
> call balance
= 9 : I64   fuel 1
"""


def said(text: str) -> str:
    """The host's output with Windows line ends made LF."""
    return text.replace("\r\n", "\n")


def bank_source() -> str:
    """bank.ci as c17_lib.c's comment holds it, numbered lines ` *   N  text`."""
    lines = []
    for line in (HERE / "c17_lib.c").read_text(encoding="utf-8").splitlines():
        m = re.match(r"^ \*\s+(\d+)(?:  (.*))?$", line)
        if m and int(m.group(1)) == len(lines) + 1:
            lines.append(m.group(2) or "")
    return "\n".join(lines) + "\n"


def build(wb, leg: str, opt: str, sanitized: bool, out: pathlib.Path) -> dict:
    """The host and the three libraries of one configuration, by name."""
    tc = wb.cc.Toolchain(leg, opt, "portable", sanitized)
    inc = "/I" if leg == "msvc" else "-I"
    tc.flags = tc.flags + [inc + str(ROOT / "workbench"), inc + str(ROOT / "cli")]
    dirs = {name: out / name for name in ("rt", "lib", "notab", "host")}
    for d in dirs.values():
        d.mkdir(parents=True)
    rt, *state = tc.compile([ROOT / "rt" / n for n in ("cint_rt.c", "cint_state.c", "cint_rec.c")], dirs["rt"],
                            "c17 library runtime", defines=("CINT_RT_LIBRARY",))
    lib = tc.compile([HERE / "c17_lib.c"], dirs["lib"], "c17 library")
    notab = tc.compile([HERE / "c17_lib.c"], dirs["notab"], "c17 library without a table", defines=("C17_NO_TABLE",))
    proc = ROOT / "cli" / ("cint_proc_win.c" if leg == "msvc" else "cint_proc_posix.c")
    host = tc.compile([ROOT / "workbench" / n for n in ("wb_c17.c", "wb_text.c", "wb_fault.c", "wb_num.c")]
                      + [proc, HERE / "c17_host.c"], dirs["host"], "c17 host")
    return {"host": tc.link_exe(host, out / "c17-host", "c17 host"),
            "plain": tc.link_shared(lib + [rt], out / "c17", "c17 library"),
            "state": tc.link_shared(lib + [rt, *state], out / "c17-state", "c17 library with state"),
            "notab": tc.link_shared(notab + [rt], out / "c17-notab", "c17 library without a table")}


def host_run(case, wb, host, cwd, commands: str) -> str:
    result = wb.run([host], cwd=cwd, entries=commands)
    case.assertClean(result, "c17-host")
    case.assertEqual(result.returncode, 0, result.stderr.decode("utf-8", "replace"))
    return said(result.stdout.decode("utf-8"))


def state_part(case, text: str, kept: bool) -> int:
    """Checks the checkpoint part of a transcript; with `kept`, the two state hashes agree."""
    case.assertRegex(text, wb_pattern(STATE_KEPT if kept else STATE_NONE))
    if kept:
        hashes = re.findall(r"state ([0-9a-f]{8})", text)
        case.assertEqual(len(hashes), 2)
        case.assertEqual(hashes[0], hashes[1], "the restored context has the checkpoint's state hash")
    return 1


def wb_pattern(text: str) -> str:
    return "^" + re.escape(text).replace(re.escape("<h>"), "[0-9a-f]{8}") + "$"


def check_config(case, wb, leg: str, opt: str, sanitized: bool, out: pathlib.Path) -> int:
    if out.exists():
        shutil.rmtree(out)
    built = build(wb, leg, opt, sanitized, out)
    names = {k: v.name for k, v in built.items()}
    checked = 0
    for which, kept in (("plain", False), ("state", True)):
        text = host_run(case, wb, built["host"], out, "open %s\n%s%s" % (names[which], CALLS, STATE))
        head = "> open %s\nopen: 7 exports, checkpoints %s\n" % (names[which], "yes" if kept else "no")
        case.assertTrue(text.startswith(head + CALLS_SAID), text)
        checked += 1 + state_part(case, text[len(head + CALLS_SAID):], kept)
    (out / "not-a-library.so").write_bytes(b"text\n")
    commands = "open nowhere%s\nopen not-a-library.so\nopen %s\ncontext\nbuild nowhere/cint src bank.ci b1\n" % (
        built["plain"].suffix, names["notab"])
    text = host_run(case, wb, built["host"], out, commands)
    case.assertRegex(text, r"^> open nowhere.*\nerror: cannot load nowhere.*\n> open not-a-library.so\n"
                           r"error: cannot load not-a-library.so.*\n")
    case.assertTrue(text.endswith("> build nowhere/cint src bank.ci b1\nerror: cannot read the toolchain file beside "
                                  "nowhere/cint; give --cint PATH\n"), text)
    case.assertIn("> open %s\nerror: %s exports no cint_library_desc, so it has no reflection table (SPEC-03 A-19); "
                  "build it with cint build --lib from cintc, not the seed\n> context\nno library is open\n"
                  % (names["notab"], names["notab"]), text)
    checked += 4
    if wb.ARGS.cint:
        src = out / "src"
        src.mkdir()
        (src / "bank.ci").write_bytes(bank_source().encode("utf-8"))
        cint = pathlib.Path(wb.ARGS.cint).resolve()
        text = host_run(case, wb, built["host"], out, "build %s src bank.ci b1\nopen lib\n%s%s" % (cint, CALLS, STATE))
        m = re.match(r"^> build .*\nbuilt\n> open lib\nopen: 7 exports, checkpoints yes\n", text)
        case.assertIsNotNone(m, text)
        case.assertTrue(text[m.end():].startswith(CALLS_SAID), text)
        checked += 1 + state_part(case, text[m.end() + len(CALLS_SAID):], True)
    return checked


def check(case, wb, exes) -> int:
    """For test_workbench.py: the configuration is the directory its executables were built in."""
    outdir = pathlib.Path(exes["test_wb"]).parent
    for config, opt, sanitized in wb.configurations(case.leg):
        if config == outdir.name:
            return check_config(case, wb, case.leg, opt, sanitized, outdir / "c17")
    raise AssertionError("no configuration %s on the %s leg" % (outdir.name, case.leg))


def load_workbench_tests():
    spec = importlib.util.spec_from_file_location("test_workbench", HERE / "test_workbench.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class C17Leg(unittest.TestCase):
    """The module on its own, each configuration of the leg."""

    def assertClean(self, result, what):
        err = result.stderr.decode("utf-8", "replace")
        self.assertNotIn("runtime error", err, what)
        self.assertNotIn("Sanitizer", err, what)

    def test_c17(self):
        wb = WB
        self.leg = wb.ARGS.leg or ("msvc" if sys.platform == "win32" else
                                   "apple-clang" if sys.platform == "darwin" else "gcc")
        base = pathlib.Path(wb.ARGS.out) if wb.ARGS.out else wb.default_out(self.leg)
        for config, opt, sanitized in wb.configurations(self.leg):
            with self.subTest(config=config):
                n = check_config(self, wb, self.leg, opt, sanitized, base / config / "c17")
                print("[%s %s] %d cpu-c17 checks" % (self.leg, config, n), flush=True)


WB = None

if __name__ == "__main__":
    WB = load_workbench_tests()
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--leg", choices=["msvc", "gcc", "clang", "apple-clang"])
    parser.add_argument("--out")
    parser.add_argument("--cint")
    known, rest = parser.parse_known_args()
    WB.ARGS.leg, WB.ARGS.out, WB.ARGS.cint = known.leg, known.out, known.cint
    unittest.main(argv=[sys.argv[0], "-v", *rest])
