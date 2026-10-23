"""Build and run rt/tests/test_rt.c on one leg of the slice matrix.

Usage (from the repository root):
    python rt/tests/test_rt.py [--leg msvc|gcc|clang|apple-clang] [--out DIR] [--quick]
                               [--fuzz-receipt FILE]

Legs: `msvc` on Windows (MSVC 2022 through vcvars64.bat), `gcc` and `clang` on
Linux (WSL Ubuntu 24.04), and `apple-clang` on macOS (the system `clang`, which
must report Apple clang; SPEC-09 CONF-09, plan task 2.2a). Each leg builds
test_rt.c with the runtime at two optimization levels (-O0 and -O2, MSVC /Od
and /O2) in both helper modes (CINT_RT_HELPERS_PORTABLE and
CINT_RT_HELPERS_BUILTIN), with the warning flags of SPEC-09 EMIT-26 and
-Werror (/WX), and runs each build, and compiles rt/cint_rt.c, rt/cint_state.c, and
rt/cint_rec.c as the one translation unit that the Python package and the workbench
build (decision 2026-10-08 on OQ-240). The Linux legs also
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
leaves in process (`--record`). Without a destination, stderr holds the one line
of OQ-289 A in place of the record. macOS has no /dev/full, so the scenario
"stdout not writable" runs on Linux only; on macOS it is omitted, not skipped.
The error scenarios check exit status 6 and the error record bytes, built here
from rt/cint_rt.h section 6d (rt/OPEN.md RT-OQ-33); with three destinations
(`--run <scenario> <fault> <fuel> <state>`) the scenarios call
cint_program_run_state, and the fuel record and the `state.global` lines are
checked too.

The seeded fuzz targets of test_state.c (checkpoint), test_record.c (recording) and
test_density.c (density) each print one line, `fuzz <target>: seed S, count N, accepted A,
rejected R, failures F, inputs-sha256 H`, the form of conformance/tools/fuzz_decoders.py.
test_wide_fast.c, the differential test of the 2- and 4-limb paths of rt/cint_wide.h and
rt/cint_wide.c (rt/OPEN.md RT-OQ-62) and of the I512, U512 and I1024 products (RT-OQ-66) and
quotients (RT-OQ-69), is built in every configuration twice, as is and with CINT_W_GENERIC, which
keeps the limb loops for every width; the two builds must print the same digest lines, which every configuration with the
same pair count shares. `--quick` lowers its pairs per operation from 2^20 to 2^12 in the -O2
builds without sanitizers, where the others run 2^12 always.

`--fuzz-receipt FILE` checks that every build of the leg printed the same three lines and
writes them as a cint-fuzz-1 receipt with the leg, its build configurations and the SHA-256 of
the sources and headers built (OQ-14).

The script is a unittest module; it uses only the Python standard library.
"""
from __future__ import annotations

import argparse
import hashlib
import json
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
SOURCES = [RT / "cint_rt.c", RT / "cint_state.c", RT / "cint_rec.c", RT / "tests" / "test_rt.c",
           RT / "tests" / "test_state.c", RT / "tests" / "test_record.c", RT / "tests" / "test_density.c"]
# The headers SOURCES include, which a fuzz receipt pins with them.
HEADERS = [RT / name for name in ("cint_fixed.h", "cint_mem.h", "cint_rt.h", "cint_rt_host.h", "cint_rt_internal.h",
                                  "cint_state.h", "cint_state_internal.h", "cint_wide.h")]
# test_wide_fast.c, the differential test of the 2- and 4-limb paths (rt/OPEN.md RT-OQ-62) and of the
# wider products (RT-OQ-66) and quotients (RT-OQ-69), with the runtime as is and with CINT_W_GENERIC,
# which keeps the limb loops for every width; its seeded pairs per operation in the -O2 builds without sanitizers (2^20, or 2^12 with
# --quick) and in the others, which run the same paths for the sanitizers and at -O0.
FAST_SOURCES = [RT / "cint_rt.c", RT / "cint_state.c", RT / "cint_rec.c", RT / "tests" / "test_wide_fast.c"]
FAST_PAIRS, FAST_PAIRS_OTHER = 1 << 20, 1 << 12
FUZZ_TARGETS = ["checkpoint", "recording", "density"]
FUZZ_LINE = re.compile(r"fuzz (\S+): seed (\d+), count (\d+), accepted (\d+), rejected (\d+), failures (\d+), "
                       r"inputs-sha256 ([0-9a-f]{64})")
# The one translation unit of the runtime, the canonical state code, and the recordings that the
# Python package (python/cint/_build.py RUNTIME_UNIT) and the workbench (workbench/wb_c17.c) compile.
UNIT = b'#define _CRT_SECURE_NO_WARNINGS 1\n#include "cint_rt.c"\n#include "cint_state.c"\n#include "cint_rec.c"\n'

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

ARGS = argparse.Namespace(leg=None, out=None, quick=False, fuzz_receipt=None)


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


# What a program writes to stderr for `big + 1` at site 2 of rt/program.ci when it has no fault
# destination (rt/cint_rt_host.h section 6d; SPEC-06 3.4a, OQ-289 A).
NO_DEST_FAULT = (b"fault[E_OVERFLOW]: add.checked.i64 at rt/program.ci:3:13 "
                 b"(pass a file path as argv[1] for the full fault record)\n")


def lf(data: bytes) -> bytes:
    """stderr with CR LF read as LF: stderr is in text mode on Windows."""
    return data.replace(b"\r\n", b"\n")


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


def compile_command(leg: str, opt: str, mode: str, sanitized: bool, outdir: pathlib.Path, sources=SOURCES,
                    name="test_rt", defines=()):
    defines = ["CINT_RT_HELPERS_" + mode.upper(), *defines]
    if leg == "msvc":
        exe = outdir / (name + ".exe")
        cmd = [ENV_CL[0], "/nologo", "/std:c17", "/W4", "/WX", "/Od" if opt == "0" else "/O2",
               *(f"/D{d}" for d in defines), f"/I{RT}", *map(str, sources), f"/Fo{outdir}{os.sep}",
               f"/Fe{exe}"]
        return cmd, exe
    exe = outdir / name
    cmd = [compiler(leg), *GNU_WARNINGS, *(CLANG_EXTRA if leg != "gcc" else []), f"-O{opt}",
           *(f"-D{d}" for d in defines), f"-I{RT}", *(SANITIZE if sanitized else []), *map(str, sources),
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
        self.assertLessEqual(lines(RT / "cint_rt_host.h"), 600)
        self.assertLessEqual(lines(RT / "cint_rt.c") + lines(RT / "cint_rt_internal.h"), 4000)
        for p in (RT / "cint_rt.h", RT / "cint_rt_host.h", RT / "cint_rt.c", RT / "cint_rt_internal.h"):
            self.assertNotIn(b"\r", p.read_bytes(), p.name)

    def test_one_unit(self):
        # No static name or macro of rt/cint_rec.c repeats one of cint_rt.c or cint_state.c with
        # another meaning (decision 2026-10-08 on OQ-240), in either helper mode.
        for mode in MODES:
            with self.subTest(mode=mode):
                outdir = self.out / f"unit-{mode}"
                outdir.mkdir(parents=True, exist_ok=True)
                (outdir / "cint_rt_state.c").write_bytes(UNIT)
                define = "CINT_RT_HELPERS_" + mode.upper()
                if self.leg == "msvc":
                    cmd = [ENV_CL[0], "/nologo", "/std:c17", "/W4", "/WX", "/O2", f"/D{define}", "/DCINT_RT_LIBRARY",
                           f"/I{RT}", "/c", "cint_rt_state.c", f"/Fo{outdir}{os.sep}"]
                else:
                    cmd = [compiler(self.leg), *GNU_WARNINGS, *(CLANG_EXTRA if self.leg != "gcc" else []), "-O2",
                           f"-D{define}", "-DCINT_RT_LIBRARY", f"-I{RT}", "-c", "cint_rt_state.c", "-o",
                           "cint_rt_state.o"]
                build = subprocess.run(cmd, cwd=outdir, env=self.env, capture_output=True, text=True)
                self.assertEqual(build.returncode, 0, f"the unit does not compile:\n{build.stdout}\n{build.stderr}")
                if self.leg != "msvc":
                    self.assertEqual(build.stderr.strip(), "", "the unit printed diagnostics")

    def test_builds_and_runs(self):
        avail = available_memory()
        self.assertGreaterEqual(avail, MIN_AVAILABLE_BYTES,
                                f"only {avail / 1024 ** 3:.1f} GiB available; the plan needs 8 GiB")
        fuzz = {}
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
                fuzz[name] = [m.groups() for m in map(FUZZ_LINE.fullmatch, run.stdout.splitlines()) if m]
                checked = self.check_program_run(exe, outdir, run_env)
                print(f"[{self.leg} {name}] program scenarios: {checked} checked", flush=True)
        if ARGS.fuzz_receipt:
            self.write_fuzz_receipt(fuzz)

    def test_wide_fast_paths(self):
        # Each configuration's two builds print the same lines, one process for each width, and every
        # configuration with the same pair count prints the lines of the first.
        seen = {}
        for name, opt, mode, sanitized in configurations(self.leg):
            with self.subTest(config=name):
                pairs = FAST_PAIRS if opt == "2" and not sanitized and not ARGS.quick else FAST_PAIRS_OTHER
                exes = {}
                for paths in ("fast", "generic"):
                    outdir = self.out / "wide-fast" / f"{name}-{paths}"
                    outdir.mkdir(parents=True, exist_ok=True)
                    cmd, exe = compile_command(self.leg, opt, mode, sanitized, outdir, FAST_SOURCES, "test_wide_fast",
                                               ["CINT_W_GENERIC"] if paths == "generic" else [])
                    build = subprocess.run(cmd, cwd=outdir, env=self.env, capture_output=True, text=True)
                    self.assertEqual(build.returncode, 0,
                                     f"{name} {paths} build failed:\n{build.stdout}\n{build.stderr}")
                    if self.leg != "msvc":
                        self.assertEqual(build.stderr.strip(), "", f"{name} {paths} build printed diagnostics")
                    exes[paths] = exe
                run_env = dict(self.env)
                run_env["ASAN_OPTIONS"] = "detect_leaks=1:abort_on_error=0"
                run_env["UBSAN_OPTIONS"] = "print_stacktrace=1:halt_on_error=1"
                procs = {(paths, width): subprocess.Popen([str(exe), str(pairs), width], cwd=exe.parent, env=run_env,
                                                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                         for paths, exe in exes.items() for width in ("128", "256", "1024")}
                out = {}
                for (paths, width), proc in procs.items():
                    stdout, stderr = proc.communicate()
                    self.assertEqual(proc.returncode, 0, f"{name} {paths} {width}-bit run failed:\n{stdout}\n{stderr}")
                    self.assertNotIn("runtime error", stderr)
                    self.assertNotIn("AddressSanitizer", stderr)
                    lines = stdout.splitlines()
                    self.assertEqual(lines[:2], [f"helper mode: {mode}", f"paths: {paths}"])
                    out[paths, width] = lines[2:]
                for width in ("128", "256", "1024"):
                    self.assertEqual(out["fast", width], out["generic", width],
                                     f"{name}: the {width}-bit paths differ from the limb loops")
                lines = out["fast", "128"] + out["fast", "256"] + out["fast", "1024"]
                print(f"[{self.leg} {name}] wide fast paths: "
                      + " | ".join(l for l in lines if l.startswith(("operands", "digest", "checks"))), flush=True)
                first, first_lines = seen.setdefault(pairs, (name, lines))
                self.assertEqual(lines, first_lines, f"{name} printed other lines than {first}")

    def write_fuzz_receipt(self, fuzz):
        """The cint-fuzz-1 receipt of the seeded targets: the rows every build printed alike."""
        names = [config[0] for config in configurations(self.leg)]
        self.assertEqual(sorted(fuzz), sorted(names), "a build did not run clean; no fuzz receipt")
        lines = fuzz[names[0]]
        self.assertEqual([line[0] for line in lines], FUZZ_TARGETS)
        for name in names:
            self.assertEqual(fuzz[name], lines, f"{name} fuzzed differently from {names[0]}")
        fields = ("seed", "count", "accepted", "rejected", "failures")
        rows = [{"target": line[0], **dict(zip(fields, map(int, line[1:6]))), "inputs_sha256": line[6]}
                for line in lines]
        sources = {path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
                   for path in [*SOURCES, *HEADERS, pathlib.Path(__file__).resolve()]}
        receipt = {"schema": "cint-fuzz-1", "leg": self.leg, "configurations": names, "targets": rows,
                   "sources_sha256": sources}
        pathlib.Path(ARGS.fuzz_receipt).write_bytes(
            (json.dumps(receipt, indent=2, sort_keys=True) + "\n").encode("ascii"))

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
            # One line on stderr in place of the record, the line `cint run` prints (OQ-289 A).
            run = self.run_scenario(exe, env, "error", "-")
            self.assertEqual((run.returncode, run.stdout, lf(run.stderr)),
                             (6, b"partial\n", b"error: rt.program.IoError.full (tag 2)\n"))
            checked += 1
        checked += self.check_program_state(exe, outdir, env, hello, io_full)
        checked += self.check_program_args(exe, outdir, env, hello)
        with self.subTest(scenario="no destination"):
            # One line on stderr naming the code, the operation and the position (OQ-289 A).
            run = self.run_scenario(exe, env, "print_then_fault", "-")
            self.assertEqual((run.returncode, run.stdout, lf(run.stderr)), (1, partial, NO_DEST_FAULT))
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

    def check_program_args(self, exe, outdir: pathlib.Path, env, hello: bytes) -> int:
        """cint_program_run_args (rt/OPEN.md RT-OQ-74): the destinations and `--grant` lines of a
        generated main's argv, through `test_rt --args <scenario> ARG...`."""
        def run_args(scenario, *args):
            run = subprocess.run([str(exe), "--args", scenario, *args], env=env, capture_output=True)
            self.assertNotIn("Sanitizer", run.stderr.decode("utf-8", "replace"))
            return run
        dests = [outdir / "args-fault.bin", outdir / "args-fuel.bin", outdir / "args-state.txt"]
        checked = 0
        with self.subTest(scenario="args: destinations and grants"):
            for d in dests:
                d.unlink(missing_ok=True)
            run = run_args("hello", str(dests[0]), "--grant", "console.input", str(dests[1]), "--grant",
                           "console.raw:x", "-")
            self.assertEqual((run.returncode, run.stdout, run.stderr), (0, hello, b""))
            self.assertEqual((dests[0].exists(), dests[1].read_bytes(), dests[2].exists()),
                             (False, fuel_record(1), False))
            run = run_args("state_error", str(dests[0]), "-", str(dests[2]), "--grant", "fs.read:/d=" + str(outdir))
            self.assertEqual((run.returncode, run.stdout, run.stderr), (6, b"", b""))
            self.assertEqual((dests[0].read_bytes(), dests[2].read_bytes()),
                             (error_record("rt.program.IoError", "closed", 0x22, 1), state_text(6)))
            run = run_args("hello")  # no destination and no grant
            self.assertEqual((run.returncode, run.stdout, run.stderr), (0, hello, b""))
            checked += 1
        with self.subTest(scenario="args: usage errors"):
            for args in (("--grant",), (str(dests[0]), "--grant"), ("a", "b", "c", "d"), ("--grant", "", "-")):
                run = run_args("hello", *args)
                self.assertEqual((run.returncode, run.stdout), (4, b""), args)
            # A program without module state takes two destinations.
            run = run_args("print_then_fault", "-", "-", "-")
            self.assertEqual((run.returncode, run.stdout), (4, b""))
            checked += 1
        with self.subTest(scenario="args: a grant line the runtime refuses"):
            for line in ("fs.read:/d", "fs.read:/d=" + str(outdir / "absent"), "fs.read", "=x"):
                run = run_args("hello", "--grant", line)
                self.assertEqual((run.returncode, run.stdout), (4, b""), line)
                self.assertIn(b"--grant", run.stderr, line)
            checked += 1
        with self.subTest(scenario="args: the program arguments of main"):
            # cint_program_run_argv with CINT_PROGRAM_ARGS (rt/OPEN.md RT-OQ-76; OQ-306): `--arg ARG`
            # pairs anywhere and every word after `--`, each its bytes and one NUL, in order.
            for d in dests:
                d.unlink(missing_ok=True)
            run = run_args("args", "--arg", "a", str(dests[0]), "--grant", "console.raw:x", str(dests[1]), "--", "bc", "")
            self.assertEqual((run.returncode, run.stdout, run.stderr), (0, b"a\0bc\0\0", b""))
            self.assertEqual((dests[0].exists(), dests[1].read_bytes()), (False, fuel_record(1)))
            run = run_args("args", "-", "-", "--", "--grant", "-")
            self.assertEqual((run.returncode, run.stdout, run.stderr), (0, b"--grant\0-\0", b""))
            run = run_args("args")
            self.assertEqual((run.returncode, run.stdout, run.stderr), (0, b"", b""))
            run = run_args("args", "--")
            self.assertEqual((run.returncode, run.stdout, run.stderr), (0, b"", b""))
            # A main that takes no arguments refuses any, with the one usage line (status 4).
            for args in (("--arg", "x"), ("--", "x"), ("-", "--arg", "x", "-")):
                run = run_args("hello", *args)
                self.assertEqual((run.returncode, run.stdout), (4, b""), args)
                self.assertIn(b"LS-221", run.stderr, args)
            run = run_args("hello", "--")
            self.assertEqual((run.returncode, run.stdout), (0, hello), "an empty tail is no argument")
            for args in (("--arg",), ("-", "-", "--arg")):
                run = run_args("args", *args)
                self.assertEqual((run.returncode, run.stdout), (4, b""), args)
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
        with self.subTest(scenario="state without a fault destination"):
            for d in dests:
                if d.exists():
                    d.unlink()
            run = self.run_scenario(exe, env, "state_fault", "-", str(dests[1]), str(dests[2]))
            self.assertEqual((run.returncode, run.stdout, lf(run.stderr)), (1, b"", NO_DEST_FAULT))
            self.assertEqual((dests[0].exists(), dests[1].read_bytes(), dests[2].read_bytes()),
                             (False, fuel_record(1), state_text(6)))
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
    parser.add_argument("--fuzz-receipt", help="write the seeded fuzz targets' cint-fuzz-1 receipt to this file")
    known, rest = parser.parse_known_args()
    ARGS.leg, ARGS.out, ARGS.quick, ARGS.fuzz_receipt = known.leg, known.out, known.quick, known.fuzz_receipt
    unittest.main(argv=[sys.argv[0], "-v", *rest])


if __name__ == "__main__":
    main()
