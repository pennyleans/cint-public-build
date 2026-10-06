"""Build and run rt/tests/test_rt.c on one leg of the slice matrix.

Usage (from the repository root):
    python rt/tests/test_rt.py [--leg msvc|gcc|clang|apple-clang] [--out DIR] [--quick]

Legs: `msvc` on Windows (MSVC 2022 through vcvars64.bat), `gcc` and `clang` on
Linux (WSL Ubuntu 24.04), and `apple-clang` on macOS (the system `clang`, which
must report Apple clang; SPEC-09 CONF-09, plan task 2.2a). Each leg builds
test_rt.c with the runtime at two optimization levels (-O0 and -O2, MSVC /Od
and /O2) in both helper modes (CINT_RT_HELPERS_PORTABLE and
CINT_RT_HELPERS_BUILTIN), with the warning flags of SPEC-09 EMIT-26 and
-Werror (/WX), and runs each build. The Linux legs also
build and run with -fsanitize=address,undefined; the apple-clang leg, like its
stage configurations (CONF-15), is unsanitized. Build outputs go under
<build>/rt/<leg>/, never inside the repository, where <build> is the
CINT_BUILD environment variable or cint-build in the system temporary directory.
`--quick` skips the exhaustive 8-bit tables.

Each build also runs the program-process scenarios of test_rt.c (`--run`),
which call cint_program_run (SPEC-06 3.4a; slice 2 decision patch D-19), and
checks stdout byte for byte (no CR on Windows), stderr empty, the exit status,
and the canonical fault record bytes at the destination (a file path, including
a non-ASCII one, and an inherited handle) against the record the same scenario
leaves in process (`--record`). macOS has no /dev/full, so the scenario
"stdout not writable" runs on Linux only; on macOS it is omitted, not skipped.
The error scenarios check exit status 6 and the error record bytes, built here
from rt/cint_rt.h section 6d (rt/OPEN.md RT-OQ-33); with three destinations
(`--run <scenario> <fault> <fuel> <state>`) the scenarios call
cint_program_run_state, and the fuel record and the `state.global` lines are
checked too.

The script is a unittest module; it uses only the Python standard library.
"""
from __future__ import annotations

import argparse
import os
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
RT = ROOT / "rt"
SOURCES = [RT / "cint_rt.c", RT / "cint_state.c", RT / "tests" / "test_rt.c", RT / "tests" / "test_state.c"]

VCVARS = pathlib.Path(r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
                      r"\VC\Auxiliary\Build\vcvars64.bat")
VS_INSTALLER = r"C:\Program Files (x86)\Microsoft Visual Studio\Installer"

GNU_WARNINGS = ["-std=c17", "-Wall", "-Wextra", "-Wconversion", "-Wsign-conversion",
                "-Wshadow", "-Wpedantic", "-Werror"]
CLANG_EXTRA = ["-Wimplicit-int-conversion"]
SANITIZE = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all",
            "-fno-omit-frame-pointer", "-g"]
MODES = ["portable", "builtin"]
MIN_AVAILABLE_BYTES = 8 * 1024 ** 3

ARGS = argparse.Namespace(leg=None, out=None, quick=False)


def lp(data: bytes) -> bytes:
    """A U32 little-endian length, then the bytes."""
    return struct.pack("<I", len(data)) + data


def error_record(set_name: str, value: str, tag_code: int, tag: int) -> bytes:
    """The error record of rt/cint_rt.h section 6d (rt/OPEN.md RT-OQ-33)."""
    return (lp(b"cint-core-1/error-result/v1") + lp(set_name.encode("ascii")) + lp(value.encode("ascii")) +
            struct.pack("<IQ", tag_code, tag))


def fuel_record(fuel: int) -> bytes:
    return lp(b"cint-core-1/fuel-consumed/v1") + struct.pack("<q", fuel)


def state_text(count: int) -> bytes:
    return b"state.global rt.program count I64 %d\nstate.global rt.program flag Bool true\n" % count
ENV_CL = ["cl"]  # full path of cl.exe, set from the vcvars64.bat environment


def default_out(leg: str) -> pathlib.Path:
    build = os.environ.get("CINT_BUILD") or os.path.join(tempfile.gettempdir(), "cint-build")
    return pathlib.Path(build) / "rt" / leg


def available_memory() -> int:
    """Available physical memory in bytes (the heavy-run gate of the plan)."""
    if sys.platform == "darwin":
        sys.path.insert(0, str(ROOT / "tools"))
        import cint_check   # sysctl hw.memsize and vm_stat; raises when the measurement fails
        return cint_check.available_memory()
    if os.name == "nt":
        import ctypes

        class MemoryStatus(ctypes.Structure):
            _fields_ = [("length", ctypes.c_uint32), ("load", ctypes.c_uint32),
                        ("total_phys", ctypes.c_uint64), ("avail_phys", ctypes.c_uint64),
                        ("total_page", ctypes.c_uint64), ("avail_page", ctypes.c_uint64),
                        ("total_virtual", ctypes.c_uint64), ("avail_virtual", ctypes.c_uint64),
                        ("avail_extended", ctypes.c_uint64)]

        status = MemoryStatus()
        status.length = ctypes.sizeof(MemoryStatus)
        if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
            raise OSError("GlobalMemoryStatusEx failed")
        return int(status.avail_phys)
    with open("/proc/meminfo", encoding="ascii") as f:
        for line in f:
            if line.startswith("MemAvailable:"):
                return int(line.split()[1]) * 1024
    raise OSError("MemAvailable not found in /proc/meminfo")


def msvc_environment() -> dict:
    """The environment vcvars64.bat sets up, read once."""
    env = dict(os.environ)
    env["PATH"] = VS_INSTALLER + os.pathsep + env.get("PATH", "")
    out = subprocess.run(f'cmd /d /c ""{VCVARS}" >nul && set"', env=env, shell=True,
                         capture_output=True, text=True, check=True).stdout
    result = {}
    for line in out.splitlines():
        key, sep, value = line.partition("=")
        if sep and key:
            result[key] = value
    if "INCLUDE" not in result:
        raise RuntimeError("vcvars64.bat did not set INCLUDE")
    path = next(v for k, v in result.items() if k.upper() == "PATH")
    cl = shutil.which("cl", path=path)
    if cl is None:
        raise RuntimeError("cl.exe not found on the vcvars64.bat PATH")
    result["CINT_RT_CL"] = cl
    return result


def configurations(leg: str):
    """(name, compile command builder, sanitized) for every build of the leg."""
    configs = []
    for opt in ("0", "2"):
        for mode in MODES:
            configs.append((f"O{opt}-{mode}", opt, mode, False))
    if leg in ("gcc", "clang"):
        for opt in ("0", "2"):
            for mode in MODES:
                configs.append((f"O{opt}-{mode}-san", opt, mode, True))
    return configs


def compiler(leg: str) -> str:
    """The C compiler command of a GNU-style leg; apple-clang must be Apple's clang."""
    if leg != "apple-clang":
        return leg
    if sys.platform != "darwin":
        raise RuntimeError("the apple-clang leg runs on macOS")
    banner = subprocess.run(["clang", "--version"], capture_output=True, text=True, check=True).stdout
    if not banner.startswith("Apple clang"):
        raise RuntimeError("the apple-clang leg needs Apple clang: %r" % banner.splitlines()[:1])
    return "clang"


def compile_command(leg: str, opt: str, mode: str, sanitized: bool, outdir: pathlib.Path):
    define = "CINT_RT_HELPERS_" + mode.upper()
    if leg == "msvc":
        exe = outdir / "test_rt.exe"
        cmd = [ENV_CL[0], "/nologo", "/std:c17", "/W4", "/WX", "/Od" if opt == "0" else "/O2",
               f"/D{define}", f"/I{RT}", *map(str, SOURCES), f"/Fo{outdir}{os.sep}",
               f"/Fe{exe}"]
        return cmd, exe
    exe = outdir / "test_rt"
    cmd = [compiler(leg), *GNU_WARNINGS, *(CLANG_EXTRA if leg != "gcc" else []), f"-O{opt}",
           f"-D{define}", f"-I{RT}", *(SANITIZE if sanitized else []), *map(str, SOURCES),
           "-o", str(exe)]
    return cmd, exe


class RuntimeLeg(unittest.TestCase):
    leg = "msvc"

    @classmethod
    def setUpClass(cls):
        cls.leg = ARGS.leg or ("msvc" if os.name == "nt" else
                               "apple-clang" if sys.platform == "darwin" else "gcc")
        cls.out = pathlib.Path(ARGS.out) if ARGS.out else default_out(cls.leg)
        cls.env = msvc_environment() if cls.leg == "msvc" else dict(os.environ)
        if cls.leg == "msvc":
            ENV_CL[0] = cls.env["CINT_RT_CL"]

    def test_no_binary_floating_types_in_rt(self):
        # The pattern is assembled so that this file does not match itself.
        words = ["flo" + "at", "dou" + "ble", "long dou" + "ble"]
        pattern = re.compile(r"\b(" + "|".join(words) + r")\b")
        hits = []
        for path in sorted(RT.rglob("*")):
            if path.is_file() and path.suffix in (".c", ".h", ".py", ".md"):
                for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
                    if pattern.search(line):
                        hits.append(f"{path.relative_to(ROOT)}:{n}")
        self.assertEqual(hits, [])

    def test_size_targets(self):
        def lines(p):
            return p.read_bytes().count(b"\n")
        self.assertLessEqual(lines(RT / "cint_rt.h"), 2500)
        self.assertLessEqual(lines(RT / "cint_rt.c") + lines(RT / "cint_rt_internal.h"), 4000)
        for p in (RT / "cint_rt.h", RT / "cint_rt.c", RT / "cint_rt_internal.h"):
            self.assertNotIn(b"\r", p.read_bytes(), p.name)

    def test_builds_and_runs(self):
        avail = available_memory()
        self.assertGreaterEqual(avail, MIN_AVAILABLE_BYTES,
                                f"only {avail / 1024 ** 3:.1f} GiB available; the plan needs 8 GiB")
        for name, opt, mode, sanitized in configurations(self.leg):
            with self.subTest(config=name):
                outdir = self.out / name
                outdir.mkdir(parents=True, exist_ok=True)
                cmd, exe = compile_command(self.leg, opt, mode, sanitized, outdir)
                build = subprocess.run(cmd, cwd=outdir, env=self.env, capture_output=True, text=True)
                self.assertEqual(build.returncode, 0, f"{name} build failed:\n{build.stdout}\n{build.stderr}")
                if self.leg != "msvc":
                    self.assertEqual(build.stderr.strip(), "", f"{name} build printed diagnostics")
                run_env = dict(self.env)
                run_env["ASAN_OPTIONS"] = "detect_leaks=1:abort_on_error=0"
                run_env["UBSAN_OPTIONS"] = "print_stacktrace=1:halt_on_error=1"
                argv = [str(exe), str(ROOT)] + (["--no-exh8"] if ARGS.quick else [])
                run = subprocess.run(argv, cwd=outdir, env=run_env, capture_output=True, text=True)
                print(f"[{self.leg} {name}] exit {run.returncode}: "
                      + " | ".join(l for l in run.stdout.splitlines() if l), flush=True)
                self.assertEqual(run.returncode, 0, f"{name} run failed:\n{run.stdout}\n{run.stderr}")
                self.assertNotIn("runtime error", run.stderr)
                self.assertNotIn("AddressSanitizer", run.stderr)
                self.assertIn(f"helper mode: {mode}", run.stdout)
                checked = self.check_program_run(exe, outdir, run_env)
                print(f"[{self.leg} {name}] program scenarios: {checked} checked", flush=True)

    def run_scenario(self, exe, env, scenario, dest, *more, **kwargs):
        run = subprocess.run([str(exe), "--run", scenario, dest, *more], env=env, capture_output=True, **kwargs)
        err = run.stderr.decode("utf-8", "replace")
        self.assertNotIn("runtime error", err)
        self.assertNotIn("Sanitizer", err)
        return run

    def expected_record(self, exe, env, scenario) -> bytes:
        run = subprocess.run([str(exe), "--record", scenario], env=env, capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        record = bytes.fromhex(run.stdout.strip())
        # The canonical record begins with its length-prefixed domain (SPEC-01 IM-149).
        self.assertTrue(record[4:24].startswith(b"cint-core-1/fault/v"), record[:32])
        return record

    def check_program_run(self, exe, outdir: pathlib.Path, env) -> int:
        """The D-19 program side of cint_program_run, one scenario per process."""
        hello = b"hello, world\nanswer=42\n"
        partial = b"partial: big=9223372036854775807"
        dest = outdir / "fault-é.bin"  # a UTF-8 path on every platform
        checked = 0
        io_full = error_record("rt.program.IoError", "full", 0x22, 2)
        cases = [  # (scenario, exit status, stdout, record written: True for the --record bytes)
            ("hello", 0, hello, False),
            ("print_then_fault", 1, partial, True),
            ("hole_fault", 1, b"start ", True),
            ("fuel_zero", 1, b"", True),
            ("refused", 7, b"", False),
            ("bad_site", 7, b"", False),
            ("error", 6, b"partial\n", io_full),
            ("error_bad_set", 7, b"", False),
        ]
        for scenario, status, out, writes in cases:
            with self.subTest(scenario=scenario):
                if dest.exists():
                    dest.unlink()
                run = self.run_scenario(exe, env, scenario, str(dest))
                self.assertEqual((run.returncode, run.stdout, run.stderr), (status, out, b""), scenario)
                self.assertEqual(dest.exists(), writes is not False, scenario)
                if writes is True:
                    self.assertEqual(dest.read_bytes(), self.expected_record(exe, env, scenario), scenario)
                elif writes:
                    self.assertEqual(dest.read_bytes(), writes, scenario)
                checked += 1
        with self.subTest(scenario="error without a destination"):
            run = self.run_scenario(exe, env, "error", "-")
            self.assertEqual((run.returncode, run.stdout, run.stderr), (6, b"partial\n", b""))
            checked += 1
        checked += self.check_program_state(exe, outdir, env, hello, io_full)
        with self.subTest(scenario="no destination"):
            run = self.run_scenario(exe, env, "print_then_fault", "-")
            self.assertEqual((run.returncode, run.stdout, run.stderr), (1, partial, b""))
            checked += 1
        with self.subTest(scenario="inherited handle"):
            r, w = os.pipe()
            try:
                if os.name == "nt":
                    import msvcrt
                    handle = msvcrt.get_osfhandle(w)
                    os.set_handle_inheritable(handle, True)
                    info = subprocess.STARTUPINFO()
                    info.lpAttributeList = {"handle_list": [handle]}
                    run = self.run_scenario(exe, env, "print_then_fault", f"handle:{handle}", startupinfo=info)
                else:
                    run = self.run_scenario(exe, env, "print_then_fault", f"handle:{w}", pass_fds=(w,))
                os.close(w)
                w = -1
                with os.fdopen(r, "rb") as f:
                    r = -1
                    got = f.read()
            finally:
                for fd in (r, w):
                    if fd >= 0:
                        os.close(fd)
            self.assertEqual((run.returncode, run.stdout, run.stderr), (1, partial, b""))
            self.assertEqual(got, self.expected_record(exe, env, "print_then_fault"))
            checked += 1
        with self.subTest(scenario="unwritable destination"):
            run = self.run_scenario(exe, env, "print_then_fault", str(outdir))  # a directory
            self.assertEqual((run.returncode, run.stdout), (5, partial))
            checked += 1
        for bad in ("handle:", "handle:x1", "handle:-1", "handle:99999999999999999999999", ""):
            with self.subTest(scenario="malformed destination", dest=bad):
                run = self.run_scenario(exe, env, "hello", bad)
                self.assertEqual((run.returncode, run.stdout, run.stderr), (4, b"", b""), bad)
                checked += 1
        if os.path.exists("/dev/full"):   # Linux; macOS has no /dev/full
            with self.subTest(scenario="stdout not writable"):
                with open("/dev/full", "wb") as full:
                    run = subprocess.run([str(exe), "--run", "hello", "-"], env=env, stdout=full,
                                         stderr=subprocess.PIPE)
                self.assertEqual(run.returncode, 5)
                self.assertNotIn(b"Sanitizer", run.stderr)
                checked += 1
        return checked

    def check_program_state(self, exe, outdir: pathlib.Path, env, hello: bytes, io_full: bytes) -> int:
        """cint_program_run_state: the fault or error record, the fuel record, and the
        `state.global` lines, each at its own destination (rt/OPEN.md RT-OQ-33)."""
        dests = [outdir / "state-fault.bin", outdir / "state-fuel.bin", outdir / "state-é.txt"]
        closed = error_record("rt.program.IoError", "closed", 0x22, 1)
        checked = 0
        cases = [  # (scenario, exit status, stdout, fault or error record, fuel record, state text)
            ("hello", 0, hello, None, fuel_record(1), state_text(5)),
            ("fuel_zero", 1, b"", True, fuel_record(0), state_text(5)),
            ("error", 6, b"partial\n", io_full, fuel_record(1), None),
            ("state_error", 6, b"", closed, fuel_record(1), state_text(6)),
            ("state_fault", 1, b"", True, fuel_record(1), state_text(6)),
            ("state_bad_table", 7, b"", None, fuel_record(1), None),
            ("refused", 7, b"", None, None, None),
        ]
        for scenario, status, out, record, fuel, state in cases:
            with self.subTest(scenario="state " + scenario):
                for d in dests:
                    if d.exists():
                        d.unlink()
                run = self.run_scenario(exe, env, scenario, *map(str, dests))
                self.assertEqual((run.returncode, run.stdout, run.stderr), (status, out, b""), scenario)
                if record is True:
                    record = self.expected_record(exe, env, scenario)
                for d, want in zip(dests, (record, fuel, state)):
                    self.assertEqual(d.read_bytes() if d.exists() else None, want, (scenario, d.name))
                checked += 1
        with self.subTest(scenario="state destination malformed or not writable"):
            run = self.run_scenario(exe, env, "hello", str(dests[0]), str(dests[1]), "handle:x")
            self.assertEqual((run.returncode, run.stdout, run.stderr), (4, b"", b""))
            run = self.run_scenario(exe, env, "state_error", str(dests[0]), str(dests[1]), str(outdir))
            self.assertEqual((run.returncode, run.stdout, run.stderr), (5, b"", b""))
            checked += 1
        with self.subTest(scenario="state without fuel or state destination"):
            for d in dests:
                if d.exists():
                    d.unlink()
            run = self.run_scenario(exe, env, "state_error", str(dests[0]), "-", "-")
            self.assertEqual((run.returncode, run.stdout, run.stderr), (6, b"", b""))
            self.assertEqual((dests[0].read_bytes(), dests[1].exists(), dests[2].exists()), (closed, False, False))
            run = self.run_scenario(exe, env, "print_then_fault", str(dests[0]), "-", str(dests[2]))
            self.assertEqual((run.returncode, dests[2].exists()), (1, False), "a program without state tables")
            checked += 1
        return checked


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--leg", choices=["msvc", "gcc", "clang", "apple-clang"])
    parser.add_argument("--out")
    parser.add_argument("--quick", action="store_true")
    known, rest = parser.parse_known_args()
    ARGS.leg, ARGS.out, ARGS.quick = known.leg, known.out, known.quick
    unittest.main(argv=[sys.argv[0], "-v", *rest])


if __name__ == "__main__":
    main()
