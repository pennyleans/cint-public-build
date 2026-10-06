"""Build cint-harness, four hand-written programs, and the recovery fixture on one leg; check them.

Usage (from the repository root):
    python harness/tests/test_harness.py [--leg msvc|gcc|clang] [--out DIR]

Legs: `msvc` on Windows (MSVC 2022 through vcvars64.bat), `gcc` and `clang` on
Linux (WSL Ubuntu 24.04). Each leg builds harness/cint_harness.c with the
runtime, and the programs harness/tests/mod_basic.c (two modules), mod_conf01.c,
mod_errors.c (error results and module state) and mod_kernel.c (kernel
dispatches, `.expect` format 4), which follow the observer interface of SPEC-09
CONF-13, as shared libraries, at -O0 and -O2 (/Od and /O2) in both helper modes,
with the strict warning flags and -Werror (/WX). The Linux legs also build every
configuration with -fsanitize=address,undefined. Each build runs every case below. Build
outputs go under <build>/harness/<leg>/, never inside the repository, where
<build> is the CINT_BUILD environment variable or cint-build in the system
temporary directory.

Each configuration also builds harness/tests/recovery_fixture.c with the
runtime as one executable, the runtime recovery fixture of slice 2 decision
patch D-25 for harness/recovery.ci, and runs it: all nine steps must pass
(steps 7 and 8 read module state, task 2.14). The values the fixture
expects are compared with what cint_ref reports for harness/recovery.ci.

The expected texts come from SPEC-09 9.2 (CONF-01, CONF-11) and from the
cint-rt-1 rules the modules exercise (rt/cint_rt.h, docs/cint/RT-CONTRACT.md).
The CONF-01 case is compared byte for byte with the frozen file
conformance/arith/add_i64_overflow.expect, and the format 4 texts of
mod_kernel.c are what cint_ref writes for the source it stands for. Every
printed outcome is also checked with conformance/tools/check_expect_abnf.py.

The script is a unittest module; it uses only the Python standard library.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
RT = ROOT / "rt"
HARNESS = ROOT / "harness"
TESTS = HARNESS / "tests"
CONF01 = ROOT / "conformance" / "arith" / "add_i64_overflow.expect"
ABNF = ROOT / "conformance" / "tools" / "check_expect_abnf.py"

VCVARS = pathlib.Path(r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
                      r"\VC\Auxiliary\Build\vcvars64.bat")
VS_INSTALLER = r"C:\Program Files (x86)\Microsoft Visual Studio\Installer"

GNU_WARNINGS = ["-std=c17", "-Wall", "-Wextra", "-Wconversion", "-Wsign-conversion",
                "-Wshadow", "-Wpedantic", "-Werror"]
CLANG_EXTRA = ["-Wimplicit-int-conversion"]
SANITIZE = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all",
            "-fno-omit-frame-pointer", "-g"]
MIN_AVAILABLE_BYTES = 8 * 1024 ** 3
SIZE_CEILING = 1000  # SPEC-09 4.2: every harness/*.c and harness/*.h (slice 2 patch D-6)

ARGS = argparse.Namespace(leg=None, out=None)

CLAUSE = "SPEC-09 CONF-10, CONF-11"  # added to every call that gives no --clause
BASIC = "case harness/basic\nclause " + CLAUSE + "\nsource reference\n"
BASIC2 = "case harness/basic\nclause " + CLAUSE + "\nformat 2\nsource reference\n"  # CONF-11 format 2
BASIC4 = "case harness/basic\nclause " + CLAUSE + "\nformat 4\nsource reference\n"  # CONF-11 format 4
KERNEL4 = "case harness/kernel\nclause " + CLAUSE + "\nformat 4\nsource reference\n"
CLAUSE01 = "SPEC-01 IM-30, IM-32, IM-106, IM-107"


def fault_text(fuel, code, operation, operands, exact, limit, position, depth=0, head=BASIC, stack=None):
    """A fault outcome; `stack` (a list of positions) makes it format 2 (CONF-11 rule 5)."""
    lines = [head + "outcome fault", "stdout-bytes 0", f"fuel-consumed {fuel}",
             f"fault.code {code}", f"fault.operation {operation}"]
    lines += [f"fault.operand {o}" for o in operands]
    lines += [f"fault.exact {exact}", f"fault.limit {limit}", f"fault.position {position}",
              "fault.revision self"] + (["fault.source-map self"] if stack is not None else []) + \
        ["fault.address none", f"fault.stack-depth {depth}"]
    lines += [f"fault.stack {p}" for p in stack or []]
    return "\n".join(lines) + "\n"


def value_text(fuel, ret=None, head=BASIC):
    return head + "outcome value\nstdout-bytes 0\n" + (f"return {ret}\n" if ret else "") + \
        f"fuel-consumed {fuel}\n"


def kernel_text(fuel, code, operation, operands, descriptors, exact, limit, position, phase, kernel, address,
                stack, runs):
    """A fault raised by a kernel dispatch of mod_kernel.c: the kernel form of format 4 (CONF-11 rule 13),
    then the module state line (rule 10)."""
    lines = [KERNEL4 + "outcome fault", "stdout-bytes 0", f"fuel-consumed {fuel}", f"fault.code {code}",
             f"fault.operation {operation}"]
    lines += [f"fault.operand {o}" for o in operands] + [f"fault.descriptor {d}" for d in descriptors]
    lines += [f"fault.exact {exact}", f"fault.limit {limit}", f"fault.position harness/kernel.ci:{position}",
              "fault.revision self", "fault.source-map self", f"fault.phase {phase}",
              f"fault.kernel harness.kernel.{kernel}", f"fault.address {address}", f"fault.stack-depth {len(stack)}"]
    lines += [f"fault.stack harness/kernel.ci:{p}" for p in stack]
    return "\n".join(lines + [f"state.global harness.kernel runs I64 {runs}"]) + "\n"


OVERLAP = kernel_text(1, "E_ALIAS", "bind.alias", ["I64 1", "I64 0"],
                      ["I32 none 2 8 8 0 16 8 0 1 write", "I32 none 2 0 8 0 16 8 0 1 write"],
                      "none", "none", "22:5", "entry", "copy2", "0 none none", [], 0)


def errors_head(fmt=None):
    return "case harness/errors\nclause " + CLAUSE + "\n" + (f"format {fmt}\n" if fmt else "") + "source reference\n"


def error_text(fuel, error_set, value, tag, head, stdout=b"", calls=None):
    """An error outcome (CONF-11 rule 12); `calls` is the module state line of format 2 and later."""
    out = f"stdout-bytes {len(stdout)}\n" + (f"stdout-sha256 {hashlib.sha256(stdout).hexdigest()}\n" if stdout else "")
    return head + "outcome error\n" + out + f"fuel-consumed {fuel}\nerror.set {error_set}\nerror.value {value}\n" + \
        f"error.tag {tag}\n" + (f"state.global harness.errors calls I64 {calls}\n" if calls is not None else "")


# (name, module, harness arguments, expected stdout). Exit status 0 for all.
CASES = [
    ("value_f", "basic", ["--entry", "f"], value_text(1, "I64 42")),
    ("conf01", "conf01", ["--entry", "main", "--clause", CLAUSE01], None),  # compared with CONF01
    ("fuel_spin", "basic", ["--entry", "spin", "--fuel", "10"],
     fault_text(10, "E_FUEL", "fuel.charge", [], "none", "I64 10", "harness/basic.ci:6:26")),
    ("fuel_at_entry", "basic", ["--entry", "f", "--fuel", "0"],
     fault_text(0, "E_FUEL", "fuel.charge", [], "none", "I64 0", "harness/basic.ci:1:1")),
    ("fuel_enough", "basic", ["--entry", "f", "--fuel", "1"], value_text(1, "I64 42")),
    ("add", "basic", ["--entry", "add", "--args", "I64", "2", "I64", "-3"], value_text(1, "I64 -1")),
    ("add_overflow", "basic", ["--entry", "add", "--args", "I64", "9223372036854775807", "I64", "1"],
     fault_text(1, "E_OVERFLOW", "add.checked.i64", ["I64 9223372036854775807", "I64 1"],
                "9223372036854775808", "I64 9223372036854775807", "harness/basic.ci:2:41")),
    ("add_underflow", "basic", ["--entry", "add", "--args", "I64", "-9223372036854775808", "I64", "-1"],
     fault_text(1, "E_OVERFLOW", "add.checked.i64", ["I64 -9223372036854775808", "I64 -1"],
                "-9223372036854775809", "I64 -9223372036854775808", "harness/basic.ci:2:41")),
    ("neg8", "basic", ["--entry", "neg8", "--args", "I8", "5"], value_text(1, "I8 -5")),
    ("neg8_min", "basic", ["--entry", "neg8", "--args", "I8", "-128"],
     fault_text(1, "E_OVERFLOW", "neg.checked.i8", ["I8 -128"], "128", "I8 127",
                "harness/basic.ci:3:31")),
    ("bool_true", "basic", ["--entry", "is_big", "--args", "U64", "18446744073709551615"],
     value_text(1, "Bool true")),
    ("bool_false", "basic", ["--entry", "is_big", "--args", "U64", "0"], value_text(1, "Bool false")),
    ("void", "basic", ["--entry", "nothing"], value_text(1)),
    ("deep", "basic", ["--entry", "deep", "--args", "I64", "5"], value_text(6, "I64 5")),
    ("depth_limit", "basic", ["--entry", "deep", "--args", "I64", "10", "--depth", "3"],
     fault_text(3, "E_DEPTH", "call.enter", [], "none", "I64 3", "harness/basic.ci:7:62", depth=2)),
    ("depth_limit_v2", "basic", ["--entry", "deep", "--args", "I64", "10", "--depth", "3", "--format", "2"],
     fault_text(3, "E_DEPTH", "call.enter", [], "none", "I64 3", "harness/basic.ci:7:62", depth=2,
                head=BASIC2, stack=["harness/basic.ci:7:62"] * 2)),
    ("overflow_v2", "basic", ["--entry", "add", "--args", "I64", "9223372036854775807", "I64", "1",
                              "--format", "2"],
     fault_text(1, "E_OVERFLOW", "add.checked.i64", ["I64 9223372036854775807", "I64 1"],
                "9223372036854775808", "I64 9223372036854775807", "harness/basic.ci:2:41", head=BASIC2, stack=[])),
    ("value_v2", "basic", ["--entry", "f", "--format", "2"], value_text(1, "I64 42", head=BASIC2)),
    ("format_1", "basic", ["--entry", "f", "--format", "1"], value_text(1, "I64 42")),
    ("depth_default", "basic", ["--entry", "deep", "--args", "I64", "300"],
     fault_text(256, "E_DEPTH", "call.enter", [], "none", "I64 256", "harness/basic.ci:7:62", depth=255)),
    ("mix", "basic", ["--entry", "mix", "--args", "I8", "-128", "U16", "65535", "I32", "-2147483648",
                      "U64", "18446744073709551615"], value_text(1, "I64 -2147418241")),
    ("u64_max", "basic", ["--entry", "id_u64", "--args", "U64", "18446744073709551615"],
     value_text(1, "U64 18446744073709551615")),
    ("names", "basic", ["--entry", "f", "--case", "control/named-case", "--clause", "SPEC-09 CONF-11",
                        "--source", "anchor"],
     value_text(1, "I64 42", head="case control/named-case\nclause SPEC-09 CONF-11\nsource anchor\n")),
    # Format 4 (CONF-11 rule 13; box 09 ruling R9). A scalar fault keeps the form of format 2.
    ("value_v4", "basic", ["--entry", "f", "--format", "4"], value_text(1, "I64 42", head=BASIC4)),
    ("depth_limit_v4", "basic", ["--entry", "deep", "--args", "I64", "10", "--depth", "3", "--format", "4"],
     fault_text(3, "E_DEPTH", "call.enter", [], "none", "I64 3", "harness/basic.ci:7:62", depth=2,
                head=BASIC4, stack=["harness/basic.ci:7:62"] * 2)),
    ("kernel_value", "kernel", ["--entry", "hot", "--args", "I64", "1", "--format", "4"],
     value_text(9, "I64 1", head=KERNEL4) + "state.global harness.kernel runs I64 1\n"),
    ("kernel_work_item", "kernel", ["--entry", "hot", "--args", "I64", "2", "--format", "4"],
     kernel_text(9, "E_OVERFLOW", "mul.checked.i64", ["I64 4611686018427387904", "I64 2"], [],
                 "9223372036854775808", "I64 9223372036854775807", "2:23", "work-item", "scale", "1 2 3",
                 ["17:5"], 1)),
    ("kernel_entry_fuel", "kernel", ["--entry", "hot", "--args", "I64", "2", "--fuel", "5", "--format", "4"],
     kernel_text(5, "E_FUEL", "fuel.charge", [], [], "none", "I64 5", "17:5", "entry", "scale", "1 none none",
                 [], 1)),
    ("kernel_entry_alias", "kernel", ["--entry", "overlap", "--format", "4"], OVERLAP),
    ("kernel_read_descriptor", "kernel", ["--entry", "read_alias", "--format", "4"],
     kernel_text(1, "E_ALIAS", "bind.alias", ["I64 1", "I64 0"], ["U8 none 1 2 4 0 1 write", "U8 none 1 0 4 0 1 read"],
                 "none", "none", "22:5", "entry", "copy2", "0 none none", [], 0)),
    ("kernel_epilogue", "kernel", ["--entry", "sum_max", "--format", "4"],
     kernel_text(3, "E_OVERFLOW", "sum.checked.i32.i32", ["I64 2"], [], "2147483648", "I32 2147483647", "8:16",
                 "epilogue", "total", "0 none none", ["30:5"], 0)),
    ("kernel_name_valid", "kernel", ["--entry", "bad_kernel", "--args", "I64", "3", "--format", "4"], OVERLAP),
    # Error results (CONF-11 rule 12; rt/cint_rt.h 6c', RT-OQ-33) and module state (rule 10).
    ("error_v3", "errors", ["--entry", "open", "--args", "I64", "-1", "--format", "3"],
     error_text(1, "harness.errors.IoError", "full", "U16 2", errors_head(3), calls=1)),
    ("error_value_v3", "errors", ["--entry", "open", "--args", "I64", "7", "--format", "3"],
     value_text(1, "I64 7", head=errors_head(3)) + "state.global harness.errors calls I64 1\n"),
    ("error_stdout_v3", "errors", ["--entry", "parse", "--format", "3"],
     error_text(1, "harness.errors.ParseError", "bad_digit", "U8 2", errors_head(3), b"bad\n", calls=0)),
    ("error_combined_v3", "errors", ["--entry", "either", "--args", "Bool", "true", "--format", "3"],
     error_text(1, "harness.errors.IoOrParse", "IoError.full", "U32 2", errors_head(3), calls=0)),
    ("error_combined_last_v3", "errors", ["--entry", "either", "--args", "Bool", "false", "--format", "3"],
     error_text(1, "harness.errors.IoOrParse", "ParseError.full", "U32 5", errors_head(3), calls=0)),
    ("error_arith_v3", "errors", ["--entry", "add8", "--args", "I8", "127", "I8", "1", "--format", "3"],
     error_text(1, "ArithError", "overflow", "U16 1", errors_head(3), calls=0)),
    ("error_arith_value_v3", "errors", ["--entry", "add8", "--args", "I8", "-100", "I8", "-28", "--format", "3"],
     value_text(1, "I8 -128", head=errors_head(3)) + "state.global harness.errors calls I64 0\n"),
    ("error_v1", "errors", ["--entry", "open", "--args", "I64", "-1"],
     error_text(1, "harness.errors.IoError", "full", "U16 2", errors_head())),
    ("error_v2", "errors", ["--entry", "open", "--args", "I64", "-1", "--format", "2"],
     error_text(1, "harness.errors.IoError", "full", "U16 2", errors_head(2), calls=1)),
]
# The harness prints an error outcome in every format, but only a file of format 3
# or later holds one (CONF-11 rule 12), so these are not checked against the grammar.
BELOW_FORMAT_3 = {"error_v1", "error_v2"}

# (name, module, arguments): each prints an `outcome refused` case and exits 0.
REFUSALS = [
    ("negative_fuel", "basic", ["--entry", "f", "--fuel", "-2"]),
    ("fuel_minus_one", "basic", ["--entry", "f", "--fuel", "-1"]),
    ("fuel_above_i64", "basic", ["--entry", "f", "--fuel", "9223372036854775808"]),
    ("depth_zero", "basic", ["--entry", "f", "--depth", "0"]),
    ("no_such_entry", "basic", ["--entry", "nosuch"]),
    ("entry_name_255", "basic", ["--entry", "e" * 255]),  # CONF-13 X-3: 255 bytes is a name
    ("too_few_args", "basic", ["--entry", "add", "--args", "I64", "1"]),
    ("too_many_args", "basic", ["--entry", "f", "--args", "I64", "1"]),
    ("wrong_type", "basic", ["--entry", "add", "--args", "I64", "1", "U64", "2"]),
    ("i8_out_of_range", "basic", ["--entry", "neg8", "--args", "I8", "128"]),
    ("u64_negative", "basic", ["--entry", "id_u64", "--args", "U64", "-1"]),
    ("u64_above", "basic", ["--entry", "id_u64", "--args", "U64", "18446744073709551616"]),
]

# (name, module or None for a missing file, arguments, exit status): nothing on stdout.
ERRORS = [
    ("no_arguments", None, [], 2),
    ("no_entry_option", "basic", [], 2),
    ("no_clause", "basic", ["--entry", "f", "--no-default-clause"], 2),
    ("unknown_option", "basic", ["--entry", "f", "--frobnicate"], 2),
    ("format_04", "basic", ["--entry", "f", "--format", "04"], 2),
    ("format_5", "basic", ["--entry", "f", "--format", "5"], 2),
    ("bad_integer", "basic", ["--entry", "add", "--args", "I64", "1x", "I64", "2"], 2),
    ("bad_type", "basic", ["--entry", "add", "--args", "Q8", "1", "I64", "2"], 2),
    ("arg_option_removed", "basic", ["--entry", "add", "--arg", "I64 2", "--arg", "I64 3"], 2),  # D-6
    ("odd_args", "basic", ["--entry", "add", "--args", "I64"], 2),
    ("entry_name_256", "basic", ["--entry", "e" * 256], 2),
    ("bad_case", "basic", ["--entry", "f", "--case", "has space"], 2),
    ("bad_clause", "basic", ["--entry", "f", "--clause", " edge"], 2),
    ("bad_source", "basic", ["--entry", "f", "--source", "oracle"], 2),
    ("bad_entry_name", "basic", ["--entry", "a-b"], 2),
    ("missing_library", None, ["--entry", "f"], 3),
    ("missing_signature", "basic", ["--entry", "seven"], 3),
    ("result_outside_type", "basic", ["--entry", "bad_ret"], 4),
    # A kernel fault is written in format 4 only, as cint_ref writes it (CONF-11 rule 13).
    ("kernel_fault_v1", "kernel", ["--entry", "hot", "--args", "I64", "2"], 4),
    ("kernel_fault_v2", "kernel", ["--entry", "hot", "--args", "I64", "2", "--format", "2"], 4),
    ("kernel_alias_v2", "kernel", ["--entry", "overlap", "--format", "2"], 4),
    ("kernel_name_one_part", "kernel", ["--entry", "bad_kernel", "--args", "I64", "0", "--format", "4"], 4),
    ("kernel_name_empty_part", "kernel", ["--entry", "bad_kernel", "--args", "I64", "1", "--format", "4"], 4),
    ("kernel_name_digit_first", "kernel", ["--entry", "bad_kernel", "--args", "I64", "2", "--format", "4"], 4),
    ("descriptor_of_bool", "kernel", ["--entry", "bool_alias", "--format", "4"], 4),
    ("error_set_signed", "errors", ["--entry", "bad_set", "--format", "3"], 4),
]


def default_out(leg: str) -> pathlib.Path:
    build = os.environ.get("CINT_BUILD") or os.path.join(tempfile.gettempdir(), "cint-build")
    return pathlib.Path(build) / "harness" / leg


def available_memory() -> int:
    """Available physical memory in bytes (the heavy-run gate of the plan)."""
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
    path = next(v for k, v in result.items() if k.upper() == "PATH")
    cl = shutil.which("cl", path=path)
    if cl is None:
        raise RuntimeError("cl.exe not found on the vcvars64.bat PATH")
    result["CINT_HARNESS_CL"] = cl
    return result


def configurations(leg: str):
    configs = [(f"O{opt}-{mode}", opt, mode, False) for opt in ("0", "2") for mode in ("portable", "builtin")]
    if leg in ("gcc", "clang"):
        configs += [(name + "-san", opt, mode, True) for name, opt, mode, _ in configs]
    return configs


def build_commands(leg, cl, opt, mode, sanitized, outdir: pathlib.Path):
    """[(command, output)] building the harness, the three programs and the recovery fixture."""
    define = "CINT_RT_HELPERS_" + mode.upper()
    rt_c = str(RT / "cint_rt.c")
    items = [("cint-harness", HARNESS / "cint_harness.c", False),
             ("mod_basic", TESTS / "mod_basic.c", True),
             ("mod_conf01", TESTS / "mod_conf01.c", True),
             ("mod_kernel", TESTS / "mod_kernel.c", True),
             ("mod_errors", TESTS / "mod_errors.c", True),
             ("recovery_fixture", TESTS / "recovery_fixture.c", False)]
    cmds = []
    for name, src, shared in items:
        if leg == "msvc":
            objdir = outdir / ("obj-" + name)
            objdir.mkdir(parents=True, exist_ok=True)
            target = outdir / (name + (".dll" if shared else ".exe"))
            cmd = [cl, "/nologo", "/std:c17", "/W4", "/WX", "/Od" if opt == "0" else "/O2",
                   f"/D{define}", f"/I{RT}", *(["/LD"] if shared else []), str(src), rt_c,
                   f"/Fo{objdir}{os.sep}", f"/Fe{target}"]
        else:
            target = outdir / (name + (".so" if shared else ""))
            cmd = [leg, *GNU_WARNINGS, *(CLANG_EXTRA if leg == "clang" else []), f"-O{opt}",
                   f"-D{define}", f"-I{RT}", *(SANITIZE if sanitized else []),
                   *(["-shared", "-fPIC"] if shared else []), str(src), rt_c, "-o", str(target),
                   *([] if shared else ["-ldl"])]
        cmds.append((cmd, target))
    return cmds


def abnf_rejections(texts) -> str:
    """Run check_expect_abnf.py on the texts; '' when it accepts all of them."""
    with tempfile.TemporaryDirectory() as tmp:
        paths = []
        for i, text in enumerate(texts):
            p = pathlib.Path(tmp) / f"out{i:03d}.expect"
            p.write_bytes(text)
            paths.append(str(p))
        run = subprocess.run([sys.executable, str(ABNF), *paths], capture_output=True, text=True)
        if run.returncode != 0 or f"checked {len(paths)}, rejected 0" not in run.stdout:
            return run.stdout + run.stderr
    return ""


class HarnessSource(unittest.TestCase):
    def test_size_ceiling_and_line_ends(self):
        sources = sorted(HARNESS.glob("*.c")) + sorted(HARNESS.glob("*.h"))
        self.assertLessEqual(sum(p.read_bytes().count(b"\n") for p in sources), SIZE_CEILING)
        for p in sorted(HARNESS.rglob("*")):
            if p.is_file() and p.suffix in (".c", ".h", ".py"):
                self.assertNotIn(b"\r", p.read_bytes(), p.name)

    def test_no_binary_floating_types(self):
        # The pattern is assembled so that this file does not match itself.
        words = ["flo" + "at", "dou" + "ble", "long dou" + "ble"]
        pattern = re.compile(r"\b(" + "|".join(words) + r")\b")
        hits = []
        for p in sorted(HARNESS.rglob("*")):
            if p.is_file() and p.suffix in (".c", ".h", ".py", ".md"):
                for n, line in enumerate(p.read_text(encoding="utf-8").splitlines(), 1):
                    if pattern.search(line):
                        hits.append(f"{p.relative_to(ROOT)}:{n}")
        self.assertEqual(hits, [])

    def test_recovery_source_matches_reference(self):
        """The values recovery_fixture.c expects are what cint_ref reports (D-25 evidence)."""
        src = HARNESS / "recovery.ci"
        self.assertEqual(src.read_bytes().count(b"\n"), 9)
        fixture = (TESTS / "recovery_fixture.c").read_text(encoding="utf-8")
        listed = [m.group(1) for m in re.finditer(r"^ \*\s+\d+  (.*)$", fixture, re.M)]
        self.assertEqual(listed, src.read_text(encoding="utf-8").splitlines(), "the fixture quotes recovery.ci")

        def ref(*args):
            run = subprocess.run([sys.executable, "-m", "cint_ref", "run", str(src), "--path", "harness/recovery.ci",
                                  *args], cwd=ROOT / "ref", capture_output=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            return run.stdout.decode("ascii")

        self.assertEqual(ref("--entry", "deposit_then_fault", "--arg", "I64 9223372036854775807"),
                         "case harness/recovery\nsource reference\noutcome fault\nstdout-bytes 8\n"
                         "stdout-sha256 95aebb28195b8d737effe0df18d71d39c8d8ba6569286fd3930fbc9f9767181e\n"
                         "fuel-consumed 1\nfault.code E_OVERFLOW\nfault.operation add.checked.i64\n"
                         "fault.operand I64 9223372036854775807\nfault.operand I64 1\n"
                         "fault.exact 9223372036854775808\nfault.limit I64 9223372036854775807\n"
                         "fault.position harness/recovery.ci:6:20\nfault.revision self\nfault.address none\n"
                         "fault.stack-depth 0\n")
        self.assertEqual(hashlib.sha256(b"partial\n").hexdigest(),
                         "95aebb28195b8d737effe0df18d71d39c8d8ba6569286fd3930fbc9f9767181e")
        # On a fresh machine read_balance gives the initializer: cint_ref has no
        # host lifecycle, so steps 7 and 8 take their values from A-7 and IM-121.
        self.assertIn("return I64 5\n", ref("--entry", "read_balance"))

    def test_loader_confined_to_one_function(self):
        text = (HARNESS / "cint_harness.c").read_text(encoding="utf-8")
        body = re.findall(r"^static int load_program\(.*?^}", text, re.S | re.M)
        self.assertEqual(len(body), 1)
        rest = text.replace(body[0], "")
        for word in ("LoadLibraryExW", "dlopen", "GetProcAddress", "dlsym"):
            self.assertIn(word, body[0])
            self.assertNotRegex(rest, r"\b%s\s*\(" % word)


class HarnessLeg(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.leg = ARGS.leg or ("msvc" if os.name == "nt" else "gcc")
        cls.out = pathlib.Path(ARGS.out) if ARGS.out else default_out(cls.leg)
        cls.env = msvc_environment() if cls.leg == "msvc" else dict(os.environ)
        cls.cl = cls.env.get("CINT_HARNESS_CL", "cl")

    def build(self, name, opt, mode, sanitized):
        outdir = self.out / name
        outdir.mkdir(parents=True, exist_ok=True)
        targets = {}
        for cmd, target in build_commands(self.leg, self.cl, opt, mode, sanitized, outdir):
            run = subprocess.run(cmd, cwd=outdir, env=self.env, capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, f"{name}: {target.name} build failed:\n{run.stdout}\n{run.stderr}")
            if self.leg == "msvc":
                noise = [l for l in run.stdout.splitlines() if re.search(r"\b(warning|error) [A-Z]+\d+", l)]
                self.assertEqual(noise, [], f"{name}: {target.name} build printed diagnostics")
            else:
                self.assertEqual(run.stderr.strip(), "", f"{name}: {target.name} build printed diagnostics")
            targets[target.stem] = target
        return outdir, targets

    def harness(self, targets, module, args):
        program = str(targets["mod_" + module]) if module else str(targets["cint-harness"].parent / "absent.lib")
        env = dict(self.env)
        env["ASAN_OPTIONS"] = "detect_leaks=1:abort_on_error=0"
        env["UBSAN_OPTIONS"] = "print_stacktrace=1:halt_on_error=1"
        if "--no-default-clause" in args:
            args = [a for a in args if a != "--no-default-clause"]
        elif args and "--clause" not in args:
            args = args + ["--clause", CLAUSE]
        argv = [str(targets["cint-harness"])] + ([program] if module or args else []) + args
        run = subprocess.run(argv, env=env, capture_output=True)
        stderr = run.stderr.decode("utf-8", "replace")
        self.assertNotIn("runtime error", stderr)
        self.assertNotIn("Sanitizer", stderr)
        return run.returncode, run.stdout, stderr

    def test_leg(self):
        avail = available_memory()
        self.assertGreaterEqual(avail, MIN_AVAILABLE_BYTES,
                                f"only {avail / 1024 ** 3:.1f} GiB available; the plan needs 8 GiB")
        conf01 = CONF01.read_bytes()
        for name, opt, mode, sanitized in configurations(self.leg):
            with self.subTest(config=name):
                outdir, targets = self.build(name, opt, mode, sanitized)
                printed, passed = [], 0
                for case, module, args, want in CASES:
                    with self.subTest(config=name, case=case):
                        code, out, err = self.harness(targets, module, args)
                        self.assertEqual(code, 0, f"{case}: {err}")
                        self.assertEqual(out, conf01 if want is None else want.encode("ascii"), case)
                        if case not in BELOW_FORMAT_3:
                            printed.append(out)
                        passed += 1
                for case, module, args in REFUSALS:
                    with self.subTest(config=name, case=case):
                        code, out, err = self.harness(targets, module, args)
                        self.assertEqual(code, 0, f"{case}: {err}")
                        self.assertRegex(out.decode("ascii"), r"\Acase harness/basic\nclause SPEC-09 CONF-10, CONF-11\nsource reference\n"
                                         r"outcome refused\nrefused\.reason [!-~]( ?[!-~])*\n\Z", case)
                        printed.append(out)
                        passed += 1
                for case, module, args, status in ERRORS:
                    with self.subTest(config=name, case=case):
                        code, out, err = self.harness(targets, module, args)
                        self.assertEqual(code, status, f"{case}: {err}")
                        self.assertEqual(out, b"", case)
                        self.assertNotEqual(err.strip(), "", case)
                        passed += 1
                with self.subTest(config=name, case="determinism"):
                    a = self.harness(targets, "basic", ["--entry", "add", "--args", "I64", "1", "I64", "2"])
                    b = self.harness(targets, "basic", ["--entry", "add", "--args", "I64", "1", "I64", "2"])
                    self.assertEqual(a, b)
                    for out in printed:
                        self.assertNotIn(b"\r", out)
                        self.assertNotIn(str(outdir).encode("utf-8"), out)
                        self.assertNotIn(str(ROOT).encode("utf-8"), out)
                    passed += 1
                with self.subTest(config=name, case="case-list"):
                    one = self.harness(targets, "basic", ["--entry", "add", "--args", "I64", "1", "I64", "2"])[1]
                    body = b"".join(one.splitlines(keepends=True)[3:])
                    lst = outdir / "cases.txt"
                    lst.write_bytes(b"harness.basic add I64 1 I64 2\nharness.basic add I64 1 I64 2\n"
                                    b"other.mod add I64 1 I64 2\n")
                    code, out, err = self.harness(targets, "basic", ["--cases", str(lst), "--no-default-clause"])
                    self.assertEqual(code, 0, err)
                    self.assertEqual(out, b"case 1\n" + body + b"case 2\n" + body + b"case 3\noutcome refused\n"
                                     b"refused.reason the case names a module that the program does not contain\n")
                    # Entries of a module other than the root (CONF-13 X-2; plan task 2.5).
                    lst.write_bytes(b"harness.lib twice I64 21\nharness.lib add I64 1 I64 2\n"
                                    b"harness.lib twice I64 9223372036854775807\n")
                    code, out, err = self.harness(targets, "basic", ["--cases", str(lst), "--no-default-clause"])
                    self.assertEqual(code, 0, err)
                    fault = fault_text(1, "E_OVERFLOW", "mul.checked.i64", ["I64 9223372036854775807", "I64 2"],
                                       "18446744073709551614", "I64 9223372036854775807", "harness/lib.ci:1:36", head="")
                    self.assertEqual(out.decode("ascii"), "case 1\n" + value_text(1, "I64 42", head="") +
                                     "case 2\noutcome refused\nrefused.reason the program exports no function `add`\n"
                                     "case 3\n" + fault)
                    lst.write_bytes(b"harness.basic add I64 1 I64 2")
                    code, out, err = self.harness(targets, "basic", ["--cases", str(lst), "--no-default-clause"])
                    self.assertEqual(code, 2, err)
                    self.assertIn("no LF", err)
                    # The list is read a line at a time into 4 KiB (slice 2 patch D-6).
                    lst.write_bytes(b"harness.basic add I64 " + b"1" * 4096 + b" I64 2\n")
                    code, out, err = self.harness(targets, "basic", ["--cases", str(lst), "--no-default-clause"])
                    self.assertEqual(code, 2, err)
                    self.assertIn("longer than 4 KiB", err)
                    passed += 1
                with self.subTest(config=name, case="load-path"):
                    # A path without a separator names the file in the working directory on every
                    # platform; a same-named decoy beside the harness is never loaded (H-OQ-01).
                    lib = targets["mod_basic"]
                    cwd = outdir / "cwd"
                    cwd.mkdir(exist_ok=True)
                    shutil.copyfile(lib, cwd / ("prog" + lib.suffix))
                    shutil.copyfile(targets["mod_conf01"], targets["cint-harness"].parent / ("prog" + lib.suffix))
                    want = self.harness(targets, "basic", ["--entry", "add", "--args", "I64", "1", "I64", "2"])
                    run = subprocess.run([str(targets["cint-harness"]), "prog" + lib.suffix, "--entry", "add",
                                          "--args", "I64", "1", "I64", "2", "--clause", CLAUSE],
                                         cwd=cwd, env=self.env, capture_output=True)
                    self.assertEqual((run.returncode, run.stdout), want[:2], run.stderr)
                    passed += 1
                with self.subTest(config=name, case="case-list-utf8"):
                    lst = outdir / "cas\u00e9.txt"   # UTF-8 on every platform; _wfopen on Windows
                    lst.write_bytes(b"harness.basic add I64 1 I64 2\n")
                    code, out, err = self.harness(targets, "basic", ["--cases", str(lst), "--no-default-clause"])
                    self.assertEqual(code, 0, err)
                    self.assertTrue(out.startswith(b"case 1\noutcome value\n"), out)
                    passed += 1
                with self.subTest(config=name, case="abnf"):
                    self.assertEqual(abnf_rejections(printed), "")
                    passed += 1
                with self.subTest(config=name, case="recovery"):
                    env = dict(self.env)
                    env["ASAN_OPTIONS"] = "detect_leaks=1:abort_on_error=0"
                    env["UBSAN_OPTIONS"] = "print_stacktrace=1:halt_on_error=1"
                    run = subprocess.run([str(targets["recovery_fixture"])], env=env, capture_output=True)
                    out = run.stdout.decode("ascii", "replace")
                    err = run.stderr.decode("utf-8", "replace")
                    self.assertEqual(run.returncode, 0, out + err)
                    self.assertEqual(err, "", "the fixture writes nothing to stderr (no sanitizer report)")
                    for n in range(1, 10):
                        self.assertIn(f"step {n}: ok:", out)
                    self.assertEqual(out.splitlines()[-1], "recovery: 9 of 9 steps passed, 0 failed")
                    passed += 1
                print(f"[{self.leg} {name}] {passed} harness checks passed", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--leg", choices=["msvc", "gcc", "clang"])
    parser.add_argument("--out")
    known, rest = parser.parse_known_args()
    ARGS.leg, ARGS.out = known.leg, known.out
    unittest.main(argv=[sys.argv[0], "-v", *rest])


if __name__ == "__main__":
    main()
