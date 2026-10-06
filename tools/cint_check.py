"""T1 agreement run: the seed, the runtime and the harness against cint_ref.

Plan Task 1.8 (docs/plans/2026-10-02-cint-foundations-plan.md); SPEC-09
sections 9.2 to 9.8 (CONF-01 to CONF-12, and CONF-16 of box 10 default BX10-29),
7.11 (EMIT-20, EMIT-31) and 12 (RCPT-02, RCPT-03).

Usage (from the repository root):

    python tools/cint_check.py --leg {msvc,gcc,clang,apple-clang} --opt {0,2}
                               --helpers {portable,builtin} [--quick]
                               [--sanitize] [--only TEXT] [--verify-tables]
                               [--jobs N] [--out DIR] [--compiler {seed,b1}]
                               [--cint PATH] [--emit-cache DIR] [--required FILE]
                               [--write-required FILE] [--interp] [--set NAME]
                               [--backend gpu-cuda [--block {32,256}] [--stand-in]]
                               [--receipt PATH | --no-receipt]

A gcc or clang leg started on Windows runs itself again in WSL
(Ubuntu-24.04) with python3. The apple-clang leg (SPEC-09 CONF-09, plan task
2.2a) runs on macOS with the system `clang`, which must report itself as
Apple clang, and takes no --sanitize (the stage's Apple Clang configurations
are unsanitized, CONF-15). Build outputs go under
<build>/check/<leg>-<opt>-<helpers>[-san][-quick], never inside the repository.
<build> is the CINT_BUILD environment variable, or cint-build in the system
temporary directory when it is unset; CINT_CHECK_OUT, when set, replaces
<build>/check. A run relaunched in WSL receives CINT_BUILD as a WSL path.

One run:

1. Inventory. Program cases (`conformance/<category>/*.ci` with their frozen
   `.expect` files); anchor programs generated from
   `conformance/integer-machine/anchors.cif1.jsonl` (one program per binary
   operator, unary minus, or `as`/`as%` conversion of `cint-boot-1` and its
   operand types); and, unless --quick, the table programs
   `conformance/tables/*.ci` of the 11 non-saturating 8-bit forms and the
   I64 boundary matrices. Every case falls in exactly one category of SPEC-09
   CONF-14 (slice 2 decision patch D-8), decided by the pure function
   `classify()`: compared (an expected compile error included), unsupported by
   this implementation (`conformance/unsupported.txt`), held (`held.txt`),
   outside the subset, or not applicable; anything else is a disagreement.
   Held cases, anchors without a `cint-boot-1` operator form and table
   programs without the `cint-boot-1` marker are classified without
   compiling; the others after the seed compiles them. With --required FILE
   the run also fails when a category differs from the stage's required case
   list (CONF-15); --write-required FILE writes this run's list for review.
   Under --only, the list is checked against the selection: a case outside it
   is neither classified nor reported absent. A case with a program is
   selected by its program's name, and an anchor without a `cint-boot-1`
   form, which has none, by its own. --set NAME runs a conformance set
   (`SETS`, for example t27-1, the cases of conformance/ternary/), selected
   as --only selects them, against its own list; the receipt records the set
   in place of the stage.
2. Build on this leg: `cint-seed` (seed/*.c), the runtime object
   (rt/cint_rt.c) and `cint-harness` (harness/cint_harness.c), warning-free
   with the EMIT-26 flags and -Werror (/WX), at the chosen optimization level
   and helper mode, and with ASan and UBSan under --sanitize.
3. Per program: the seed compiles it twice, the second time from a copy at
   another absolute path with another working directory and other TZ, LANG
   and SOURCE_DATE_EPOCH (CONF-05); the two C files must be identical. The C
   is compiled warning-free and linked with the runtime into a shared library
   that follows the observer interface of SPEC-09 CONF-13, and the harness
   runs its case list (`--cases`, CONF-12), one fresh context per call.
   B1's output does not depend on the configuration under test (every
   configuration bootstraps the same B1, at -O0 with portable helpers), so
   with --emit-cache DIR (outside the repository) both B1 compilations of a
   program keep their exit status, stderr and output set in DIR, and the
   other configurations of the leg reuse them instead of compiling again. An
   entry is keyed by the B1 executable's digest, this tool's digest, the
   module list, every .ci file under the source root and the environment of
   the compilation, never by a path; observations.b1.emit_cache counts the
   compilations run and reused.
4. Comparison with cint_ref on the full canonical record. A program case's
   observed record (the harness outcome, or the seed's diagnostic) must equal
   its frozen `.expect` and pass `python -m cint_ref check`; anchor and table
   records must equal what cint_ref computes for the same call. B1 also writes
   the reflection table of SPEC-03 A-19, and the seed writes none: under
   CONF-16 the canonical type signature (A-20) in every export row of a B1
   output set must equal the bytes cint_ref writes for that export
   (ref/cint_ref/sig.py). Under the interim rule of A-12 only the root module
   has rows. A row whose bytes differ, a row and cx wrapper without a cint_ref
   signature, and a cint_ref signature without a row are disagreements, and
   fail the run as a record disagreement does (CONF-04).
5. Symbol audit (SPEC-09 EMIT-31 and SPEC-03 A-3 as amended by slice 2 patch
   D-2; `dumpbin /symbols` or `nm`): the runtime object defines only `cint_`
   symbols; a program object defines only `cx_*`, `cm_*`, `ci_*` and `cg_*`
   symbols and `cint_program_abi` and `cint_observer_desc`, and refers only to
   the runtime object's `cint_` functions, the EMIT-31 support list with the
   rows of its own target (on `apple-clang` only, `__chkstk_darwin`, decision
   25, and `bzero`, ruling 2026-10-05), and `ci_*` and `cg_*` symbols another
   object of the same program defines; the program library exports only
   `cx_*`, `cm_*`, the two observer symbols, and what the runtime object
   linked into it defines. Mach-O names
   are compared without the one leading underscore Mach-O adds to every C name.
   Sanitizer builds are exempt (EMIT-20).
6. --verify-tables regenerates the exhaustive 8-bit tables, the boundary
   matrices and the table programs with this host's Python, into the run's
   own directory <out>/tables-regen, and compares every file with
   conformance/integer-machine/MANIFEST.txt (plan Review Focus item 5).
6a. --interp (with --compiler b1) is the interpreter leg (box 13 unit 2; SPEC-09
   BACK-01a, CONF-10a): `cint-interp` (interp/ with the runtime object) runs each
   program from the SIR text of B1's output set in place of its compiled C, through
   `cint-interp observe` (the harness with a SIR directory) or `cint-interp run`
   (a script or main() program), and its records are compared as compiled C's are.
   A program that uses a form the interpreter does not run yet is not compared:
   `outcome.not_compared.not_executed` names it with the interpreter's reason.
   No program library is built, so no program is symbol-audited.
6b. --backend gpu-cuda (with --compiler b1; box 11 unit 4; SPEC-09 CONF-17, RCPT-09) compiles
   every program with CINT_GPU_CUDA and links it with rt/cint_cuda.c and
   rt/cint_cuda_dispatch.c, so each kernel an entry reaches runs on CUDA device 0 from the
   PTX its C carries, with the host protocol of SPEC-02 F-11; the harness is unchanged
   (BX11-22). --block 32 or 256 is the schedule (SPEC-02 Q-3), compiled into the dispatch as
   CINT_CUDA_BLOCK. A kernel back_ptx refuses fails its program's build with BACK-05's
   E_UNSUPPORTED, and the case is unsupported, with the kernel and the capability. --stand-in
   runs the kernels on the stand-in driver of rt/tests (cuda_fake.c with CINT_FAKE_PTX and
   ptx_interp.c), which interprets the PTX on the host, one work-item at a time, in descending
   order within a launch unless CINT_FAKE_ORDER is `asc`: the dispatch path on any leg, with no
   device. A device run records the device that tools/cint_cuda_probe.c reports in
   `observations.gpu`, with the block size and the PTX `.version` and `.target`; the identity
   gains `backend`, profile `cuda`, the CUDA sources' digest and, for the stand-in, the
   stand-in's digest; the receipt goes under results/cint/box11/.
7. A receipt of schema `cint-conformance-receipt-2`,
   results/cint/slice2/t1/receipt-<leg>-<opt>-<helpers>[-san][-quick].json (the
   schema 1 receipts of slice 1 under results/cint/t1/ are never rewritten), in
   the RCPT-02 form: RFC 8785 serialization (ASCII keys, integers and
   strings only), then one LF. `outcome.not_compared` holds the four
   categories that are not compared, each sorted by case bytes, and
   `identity.encodings` each encoding version the run relies on (D-20), for B1
   the type signature encoding too; a B1 receipt's `outcome.type_signatures`
   holds the CONF-16 counts and its first disagreement. The
   key `schema` names the format (SPEC-06 section 15). `identity` and `outcome` hold nothing that
   depends on the toolchain or host; the leg, compiler version and flags,
   counts of executions and timings are `observations`. No host path and no
   local time.

Exit status: 0 when every compared record agrees and every check passes; 1
on a disagreement or a failed check; 3 when the run is blocked (memory gate,
missing toolchain). Python standard library only; no floating point.
"""
from __future__ import annotations

import argparse
import collections
import concurrent.futures
import datetime
import hashlib
import json
import os
import pathlib
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
CONF = ROOT / "conformance"
RT = ROOT / "rt"
SEED = ROOT / "seed"
HARNESS = ROOT / "harness"
INTERP = ROOT / "interp"
INTERP_SOURCES = ("sir_read.c", "sir_exec.c", "cint_interp_main.c")
CUDA_SOURCES = (ROOT / "rt" / "cint_cuda.c", ROOT / "rt" / "cint_cuda_dispatch.c")
STAND_IN_SOURCES = (ROOT / "rt" / "tests" / "cuda_fake.c", ROOT / "rt" / "tests" / "ptx_interp.c")
REF = ROOT / "ref"
sys.path.insert(0, str(REF))
sys.path.insert(0, str(CONF / "tools"))

from cint_ref import cif1, expect, sig  # noqa: E402
from cint_ref import parser as P  # noqa: E402
from cint_ref.check import check_module  # noqa: E402
from cint_ref.exec import DEFAULT_DEPTH, _check_args, _run, run_program  # noqa: E402
from cint_ref.faults import RUN_TIME, CompileError, Refused, decode_fault_record  # noqa: E402
from cint_ref.lexer import decode_source  # noqa: E402
from cint_ref.types import Value  # noqa: E402
import gen_expect  # noqa: E402  (case-header and held.txt readers)

WSL_DISTRO = "Ubuntu-24.04"
VS_INSTALLER = r"C:\Program Files (x86)\Microsoft Visual Studio\Installer"
MIN_AVAILABLE_BYTES = 8 * 1024 ** 3
GNU_WARNINGS = ["-std=c17", "-Wall", "-Wextra", "-Wconversion", "-Wsign-conversion",
                "-Wshadow", "-Wpedantic", "-Werror"]
CLANG_EXTRA = ["-Wimplicit-int-conversion"]
SANITIZE = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all",
            "-fno-omit-frame-pointer", "-g"]
MSVC_WARNINGS = ["/nologo", "/std:c17", "/W4", "/WX"]
# SPEC-09 EMIT-31: what a compiled program object may refer to besides the runtime.
EMIT31_SUPPORT = {"memcpy", "memset", "memmove", "memcmp", "__chkstk", "__security_cookie",
                  "__security_check_cookie", "__GSHandlerCheck", "__stack_chk_fail",
                  "__stack_chk_guard"}
# EMIT-31 rows of one target only (decision 25). Apple Clang on macOS AArch64 calls Apple's stack
# probe for frames over one page; `nm -P` prints the Mach-O name `___chkstk_darwin`, and norm()
# removes exactly the one underscore Mach-O prefixes to every C name, giving `__chkstk_darwin`.
# At -O0 it also zeroes a large local array with `bzero` rather than `memset` (`int64_t l0_a[512]
# = {0};` of boot/const_extent_limit; ruling 2026-10-05, G-C1, rt/OPEN.md RT-OQ-32). MSVC at /Od
# guards the zero store of B1's loop `l[z] = 0;` into a local array of 1-byte elements with its
# /GS range check, `__report_rangecheckfailure` (OQ-211). MSVC at /O2 refers to its C runtime's
# CPU feature level `__isa_available` from the object of box 09's reduce/dot_value (OQ-215).
EMIT31_TARGET = {"apple-clang": frozenset({"__chkstk_darwin", "bzero"}),
                 "msvc": frozenset({"__report_rangecheckfailure", "__isa_available"})}
# The operator spelling per (op, form) of SPEC-01 4.1 for the cint-boot-1 binary forms.
BOOT_BINARY = {("add", "checked"): "+", ("sub", "checked"): "-", ("mul", "checked"): "*",
               ("div", "checked"): "/", ("rem", "checked"): "%", ("shl", "checked"): "<<",
               ("shr", "checked"): ">>", ("add", "wrap"): "+%", ("sub", "wrap"): "-%",
               ("mul", "wrap"): "*%", ("shl", "wrap"): "<<%"}
# The saturating forms (SPEC-04 LS-48, LS-157), outside cint-boot-1 (SPEC-09 5.3): B1 compiles
# them (D-15 row 2), the seed does not, so an anchor gains a program from them under B1 only.
SAT_BINARY = {("add", "sat"): "+|", ("sub", "sat"): "-|", ("mul", "sat"): "*|"}
# Names the toolchain makes, not the program: MSVC names pooled string literals ??_C@...
# (/GF, on under /O2) and pooled 16-, 32- and 64-byte vector constants __xmm@, __ymm@, __zmm@
# (at /O2, for example the constant of a two-field struct initializer) as COMDAT externals;
# the ELF linker defines the section bounds. A pooled floating-point constant __real@ is not
# excluded: it stays a finding. MSVC at /O2 also defines `__isa_available_default` beside its
# reference to `__isa_available` (OQ-215); that one name is excluded, not a prefix.
MSVC_POOLED = ("??_C@", "__xmm@", "__ymm@", "__zmm@")
MSVC_NAMES = frozenset({"__isa_available_default"})
# SPEC-09 EMIT-22, EMIT-31 and CONF-13 X-2: the program-level observer symbols and the
# library descriptor of the reflection table (SPEC-03 A-19), the public families
# (cint-abi-1, SPEC-03 A-12, A-14), and the internal families of per-module output.
OBSERVER_SYMBOLS = {"cint_program_abi", "cint_observer_desc", "cint_observer_state", "cint_library_desc"}
PUBLIC_PREFIXES = ("cx_", "cm_")
INTERNAL_PREFIXES = ("ci_", "cg_")
ELF_LINKER_SYMBOLS = {"_init", "_fini", "__bss_start", "_edata", "_end"}
BOOT_TYPES = {"I8", "I16", "I32", "I64", "U8", "U16", "U32", "U64"}
BACKEND_REFUSAL_RE = re.compile(r"gpu-cuda: E_UNSUPPORTED: kernel (\S+) needs capability "
                                r"([^,\s]+)[^\"]*")
DIAG_RE = re.compile(r"^(?P<path>[^:\s]+):(?P<line>\d+):(?P<col>\d+): error (?P<code>C\d{4}): "
                     r"(?P<text>.*)$")
TREE_METHOD = ("sha256 over the lines '<sha256>  <path>\\n' (the sha256sum list format), one line "
               "per tracked file under %s, path relative to %s, the lines ordered by byte order "
               "of path")
REF_LOCK = threading.Lock()  # cint_ref runs one call at a time
# CONF-05: the environment of the second compilation, from another path and working directory.
ALT_ENV = {"TZ": "Pacific/Kiritimati", "LANG": "tr_TR.UTF-8", "LC_ALL": "tr_TR.UTF-8",
           "SOURCE_DATE_EPOCH": "1"}


# --------------------------------------------------------------------------- helpers

def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def canonical(obj) -> bytes:
    """RFC 8785 bytes for objects of ASCII keys, strings, booleans, null and I-JSON integers."""
    def check(x):
        if isinstance(x, bool) or x is None or isinstance(x, str):
            if isinstance(x, str):
                x.encode("ascii")
            return
        if isinstance(x, int):
            if not -(2 ** 53 - 1) <= x <= 2 ** 53 - 1:
                raise ValueError("integer outside the I-JSON range: %d" % x)
            return
        if isinstance(x, dict):
            for k, v in x.items():
                k.encode("ascii")
                check(v)
            return
        if isinstance(x, list):
            for v in x:
                check(v)
            return
        raise TypeError("not a receipt value: %r" % (x,))
    check(obj)
    return json.dumps(obj, sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode("ascii")


def git_files(prefix: str) -> list:
    out = subprocess.run(["git", "-c", "safe.directory=*", "ls-files", "-z", "--", prefix],
                         cwd=ROOT, capture_output=True, check=True).stdout
    return sorted((p for p in out.decode("utf-8").split("\0") if p), key=lambda s: s.encode("utf-8"))


def tree_digest(prefix: str, base: str):
    """SHA-256 of `<sha256>  <path>\\n` lines of the tracked files under prefix (the T0 method)."""
    lines = []
    for rel in git_files(prefix):
        path = ROOT / rel
        if path.is_file():
            lines.append("%s  %s\n" % (sha256(path.read_bytes()), rel[len(base):]))
    lines.sort(key=lambda s: s.split("  ", 1)[1].encode("utf-8"))
    return sha256("".join(lines).encode("utf-8")), len(lines)


def files_digest(paths) -> str:
    rel = sorted((p.relative_to(ROOT).as_posix() for p in paths), key=lambda s: s.encode("utf-8"))
    return sha256("".join("%s  %s\n" % (sha256((ROOT / r).read_bytes()), r) for r in rel).encode())


def available_memory() -> int:
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
    if sys.platform == "darwin":
        # macOS has no /proc/meminfo. Physical memory is sysctl hw.memsize (sysctl(3)); vm_stat(1)
        # prints page counts and the page size. Free and inactive pages are the ones the kernel
        # can hand out without paging out active memory (Apple, Memory Usage Performance
        # Guidelines, "About the Virtual Memory System"); speculative and purgeable pages are
        # left out, so the figure is conservative. A failed or implausible measurement raises.
        total = int(subprocess.run(["sysctl", "-n", "hw.memsize"], capture_output=True,
                                   text=True, check=True).stdout.strip())
        text = subprocess.run(["vm_stat"], capture_output=True, text=True, check=True).stdout
        size = re.search(r"page size of (\d+) bytes", text)
        pages = dict(re.findall(r"^(Pages free|Pages inactive):\s+(\d+)\.$", text, re.M))
        if not size or len(pages) != 2:
            raise OSError("vm_stat output not understood")
        avail = int(size.group(1)) * (int(pages["Pages free"]) + int(pages["Pages inactive"]))
        if not 0 < avail <= total:
            raise OSError("vm_stat reports %d bytes available of %d" % (avail, total))
        return avail
    with open("/proc/meminfo", encoding="ascii") as f:
        for line in f:
            if line.startswith("MemAvailable:"):
                return int(line.split()[1]) * 1024
    raise OSError("MemAvailable not found")


def wsl_path(path: pathlib.Path) -> str:
    drive, rest = os.path.splitdrive(str(path))
    return "/mnt/%s%s" % (drive[0].lower(), rest.replace("\\", "/"))


def build_root() -> pathlib.Path:
    """The root of build outputs: CINT_BUILD, else cint-build in the system temporary directory."""
    if os.environ.get("CINT_BUILD"):
        return pathlib.Path(os.environ["CINT_BUILD"])
    return pathlib.Path(tempfile.gettempdir()) / "cint-build"


def wsl_env() -> dict:
    """The environment of a relaunch in WSL: CINT_BUILD crosses as a translated path."""
    env = dict(os.environ)
    if env.get("CINT_BUILD"):
        env["WSLENV"] = ":".join(p for p in (env.get("WSLENV"), "CINT_BUILD/p") if p)
    return env


def default_base() -> pathlib.Path:
    if os.environ.get("CINT_CHECK_OUT"):
        return pathlib.Path(os.environ["CINT_CHECK_OUT"])
    return build_root() / "check"


def host_os() -> str:
    if os.name == "nt":
        return "Windows %s" % platform.release()
    if sys.platform == "darwin":
        return "macOS %s" % platform.mac_ver()[0]
    try:
        with open("/etc/os-release", encoding="utf-8") as f:
            for line in f:
                if line.startswith("PRETTY_NAME="):
                    return line.split("=", 1)[1].strip().strip('"')
    except OSError:
        pass
    return platform.system()


# --------------------------------------------------------------------------- toolchains

class BuildError(Exception):
    pass


CC_OVERRIDES = ("CL", "_CL_", "LINK", "_LINK_", "CCC_OVERRIDE_OPTIONS")


def without_cc_overrides(env) -> dict:
    return {k: v for k, v in env.items() if k.upper() not in CC_OVERRIDES}


class Toolchain:
    """Compile commands of one leg: compiler, optimization, helper mode, sanitizers."""

    def __init__(self, leg: str, opt: str, helpers: str, sanitize: bool):
        self.leg, self.opt, self.helpers, self.sanitize = leg, opt, helpers, sanitize
        self.define = "CINT_RT_HELPERS_" + helpers.upper()
        self.retries = []
        # Variables that add or remove compiler and linker options unseen by the recorded flags
        # (MSVC CL, _CL_, LINK, _LINK_; clang CCC_OVERRIDE_OPTIONS) are removed (M1 review).
        self.env = without_cc_overrides(os.environ)
        if leg == "msvc":
            self.env = without_cc_overrides(msvc_environment())
            self.cc = self.env["CINT_CHECK_CL"]
            self.dumpbin = str(pathlib.Path(self.cc).with_name("dumpbin.exe"))
            banner = subprocess.run([self.cc], env=self.env, capture_output=True, text=True).stderr
            m = re.search(r"Version (\S+)", banner)
            self.version = m.group(1) if m else "unknown"
            self.flags = MSVC_WARNINGS + ["/Od" if opt == "0" else "/O2", "/D" + self.define]
        else:
            self.cc = "clang" if leg == "apple-clang" else leg
            if leg == "apple-clang":
                banner = subprocess.run([self.cc, "--version"], capture_output=True, text=True,
                                        check=True).stdout
                if not banner.startswith("Apple clang"):
                    raise BuildError("the apple-clang leg needs Apple clang: %r"
                                     % banner.splitlines()[:1])
                sdk = [subprocess.run(["xcrun", "--show-sdk-" + what], capture_output=True,
                                      text=True, check=True).stdout.strip()
                       for what in ("version", "build-version")]
                # Recorded with the C0 (decision 25): the Apple clang version line and the SDK.
                self.apple = {"full_version": banner.splitlines()[0].strip(),
                              "sdk": "MacOSX %s (%s)" % tuple(sdk)}
            self.version = subprocess.run(
                [self.cc, "-dumpfullversion" if leg == "gcc" else "-dumpversion"],
                capture_output=True, text=True, check=True).stdout.strip()
            self.flags = GNU_WARNINGS + (CLANG_EXTRA if leg != "gcc" else []) + \
                ["-O" + opt, "-D" + self.define, "-fPIC"] + (SANITIZE if sanitize else [])
        self.obj = ".obj" if leg == "msvc" else ".o"
        self.exe = ".exe" if leg == "msvc" else ""
        self.lib = ".dll" if leg == "msvc" else ".so"

    def run(self, cmd, cwd, what):
        r = subprocess.run([str(c) for c in cmd], cwd=cwd, env=self.env, capture_output=True,
                           text=True, errors="replace")
        if r.returncode != 0 and "internal compiler error" in r.stderr:
            self.retries.append(what)   # a toolchain flake, retried once (SEED-OQ-17)
            r = subprocess.run([str(c) for c in cmd], cwd=cwd, env=self.env, capture_output=True,
                               text=True, errors="replace")
        noise = r.stderr.strip() if self.leg != "msvc" else ""
        if r.returncode != 0 or noise:
            raise BuildError("%s: exit %d\n%s\n%s" % (what, r.returncode, r.stdout[-4000:],
                                                     r.stderr[-4000:]))
        return r

    def compile(self, sources, obj_dir: pathlib.Path, what: str, defines=()) -> list:
        objs = []
        for src in sources:
            obj = obj_dir / (pathlib.Path(src).stem + self.obj)
            if self.leg == "msvc":
                cmd = [self.cc, *self.flags, *("/D" + d for d in defines), "/I" + str(RT), "/c",
                       str(src), "/Fo" + str(obj)]
            else:
                cmd = [self.cc, *self.flags, *("-D" + d for d in defines), "-I" + str(RT), "-c",
                       str(src), "-o", str(obj)]
            self.run(cmd, obj_dir, "%s: compile %s" % (what, pathlib.Path(src).name))
            objs.append(obj)
        return objs

    def link_exe(self, objs, out: pathlib.Path, what: str) -> pathlib.Path:
        exe = pathlib.Path(str(out) + self.exe)
        if self.leg == "msvc":
            cmd = [self.cc, "/nologo", *map(str, objs), "/Fe" + str(exe)]
        else:
            cmd = [self.cc, *(SANITIZE if self.sanitize else []), *map(str, objs), "-o", str(exe)]
            if sys.platform != "darwin":
                cmd += ["-ldl"]
        self.run(cmd, out.parent, "%s: link" % what)
        return exe

    def link_shared(self, objs, out: pathlib.Path, what: str) -> pathlib.Path:
        lib = pathlib.Path(str(out) + self.lib)
        if self.leg == "msvc":
            cmd = [self.cc, "/nologo", "/LD", *map(str, objs), "/Fe" + str(lib)]
        else:
            cmd = [self.cc, "-shared", *(SANITIZE if self.sanitize else []), *map(str, objs),
                   "-o", str(lib)]
        self.run(cmd, out.parent, "%s: link" % what)
        return lib

    def symbols(self, obj: pathlib.Path):
        """(defined external symbols, undefined symbols) of one object file."""
        defined, undefined = set(), set()
        if self.leg == "msvc":
            out = self.run([self.dumpbin, "/nologo", "/symbols", str(obj)], obj.parent,
                           "dumpbin").stdout
            for line in out.splitlines():
                m = re.match(r"^[0-9A-F]{3,} [0-9A-F]{8} (\S+)\s+.*?\|\s+(\S+)", line)
                if (not m or " External " not in line or m.group(2).startswith(MSVC_POOLED)
                        or m.group(2) in MSVC_NAMES):
                    continue
                (undefined if m.group(1) == "UNDEF" else defined).add(m.group(2))
        else:
            out = self.run(["nm", "-P", str(obj)], obj.parent, "nm").stdout
            for line in out.splitlines():
                parts = line.split()
                if len(parts) < 2:
                    continue
                name, kind = norm(parts[0]), parts[1]
                if kind == "U":
                    undefined.add(name)
                elif kind.isupper() and kind != "N":
                    defined.add(name)
        return defined, undefined

    def exports(self, lib: pathlib.Path) -> set:
        """The names a shared library exports (`dumpbin /exports`, `nm -D --defined-only`)."""
        names = set()
        if self.leg == "msvc":
            out = self.run([self.dumpbin, "/nologo", "/exports", str(lib)], lib.parent,
                           "dumpbin").stdout
            for line in out.splitlines():
                m = re.match(r"^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]{8}\s+(\S+)", line)
                if m:
                    names.add(m.group(1))
        else:
            dynamic = ["-gU"] if sys.platform == "darwin" else ["-D", "--defined-only"]
            out = self.run(["nm", *dynamic, "-P", str(lib)], lib.parent, "nm").stdout
            names = {norm(line.split()[0]) for line in out.splitlines() if line.split()}
            names -= ELF_LINKER_SYMBOLS
        return names

    def describe(self) -> dict:
        link = [] if self.leg == "msvc" else (["-ldl"] if sys.platform != "darwin" else [])
        return {"flags": " ".join(self.flags + link), "name": self.leg, "version": self.version,
                **getattr(self, "apple", {})}


def norm(symbol: str) -> str:
    """The C name of a symbol (Mach-O prefixes an underscore; ELF and x64 COFF do not)."""
    return symbol[1:] if sys.platform == "darwin" and symbol.startswith("_") else symbol


def msvc_environment() -> dict:
    env = dict(os.environ)
    env["PATH"] = VS_INSTALLER + os.pathsep + env.get("PATH", "")
    vswhere = pathlib.Path(VS_INSTALLER) / "vswhere.exe"
    install = subprocess.run([str(vswhere), "-latest", "-products", "*", "-requires",
                              "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property",
                              "installationPath"], capture_output=True, text=True).stdout.strip()
    vcvars = pathlib.Path(install.splitlines()[0] if install else "") / "VC" / "Auxiliary" / \
        "Build" / "vcvars64.bat"
    if not vcvars.is_file():
        raise BuildError("vcvars64.bat not found (vswhere: %r)" % install)
    out = subprocess.run('cmd /d /c ""%s" >nul && set"' % vcvars, env=env, shell=True,
                         capture_output=True, text=True, check=True).stdout
    result = {}
    for line in out.splitlines():
        key, sep, value = line.partition("=")
        if sep and key:
            result[key] = value
    path = next(v for k, v in result.items() if k.upper() == "PATH")
    cl = shutil.which("cl", path=path)
    if cl is None:
        raise BuildError("cl.exe not found on the vcvars64.bat PATH")
    result["CINT_CHECK_CL"] = cl
    return result


# --------------------------------------------------------------------------- result categories

# SPEC-09 CONF-14 (slice 2 decision patch D-8): every case is in exactly one category.
CATEGORIES = ("compared", "unsupported", "held", "outside_subset", "not_applicable")
SUBSET_CODES = ("C9100", "C4040")   # the seed's refusals of a program outside cint-boot-1
COMPILERS = {"seed": "cint-seed", "b1": "cintc"}   # B1: compiler/main.ci through `cint emit-c`
# Conformance sets beside the stages, each with its own required list (t27 scoping note 7.2,
# TT-04): the set's name and the --only texts that select its cases.
SETS = {"t27-1": ("ternary/",)}
COMPUTED = "computed"               # anchor and table records: cint_ref computes each expectation
LIMIT_RE = re.compile(r"[A-Z][A-Z0-9]*(?:-[A-Z0-9]+)+\Z")


def read_unsupported(path: pathlib.Path) -> dict:
    """conformance/unsupported.txt as {(case, compiler): (code, limit, reason)} (CONF-14).

    One line per entry, `<case> <compiler> <code> <limit-id> <reason>`; `#` lines and empty
    lines are comments. Raises ValueError on a malformed, duplicated or unsorted line."""
    text = path.read_bytes().decode("ascii")
    if "\r" in text:
        raise ValueError("%s: CR found; LF only" % path.name)
    entries, previous = {}, None
    for n, line in enumerate(text.split("\n"), start=1):
        if not line or line.startswith("#"):
            continue
        parts = line.split(" ", 4)
        if len(parts) != 5 or not parts[4].strip():
            raise ValueError("%s line %d: expected `<case> <compiler> <code> <limit-id> <reason>`"
                             % (path.name, n))
        case, compiler, code, limit, reason = parts
        if compiler not in COMPILERS.values():
            raise ValueError("%s line %d: unknown compiler %s" % (path.name, n, compiler))
        if not re.fullmatch(r"C9\d{3}", code):
            raise ValueError("%s line %d: %s is not a C9xxx code" % (path.name, n, code))
        if not LIMIT_RE.match(limit):
            raise ValueError("%s line %d: %s is not a limit identifier" % (path.name, n, limit))
        if (case, compiler) in entries:
            raise ValueError("%s line %d: %s %s listed twice" % (path.name, n, case, compiler))
        key = (case.encode("utf-8"), compiler.encode("ascii"))
        if previous is not None and key < previous:
            raise ValueError("%s line %d: not sorted by case, then compiler" % (path.name, n))
        previous = key
        entries[(case, compiler)] = (code, limit, reason)
    return entries


def read_required(path: pathlib.Path) -> dict:
    """A required case list (CONF-15) as {(case, compiler): (category, detail or None)}.

    One line per case and compiler, `<case> <compiler> <category> [<detail>]`: the limit
    identifier for `unsupported`, the Open item for `held`, nothing for the other categories."""
    text = path.read_bytes().decode("ascii")
    if "\r" in text:
        raise ValueError("%s: CR found; LF only" % path.name)
    entries = {}
    for n, line in enumerate(text.split("\n"), start=1):
        if not line or line.startswith("#"):
            continue
        parts = line.split(" ")
        if len(parts) not in (3, 4) or parts[2] not in CATEGORIES:
            raise ValueError("%s line %d: expected `<case> <compiler> <category> [<detail>]`"
                             % (path.name, n))
        needs = parts[2] in ("unsupported", "held")
        if needs != (len(parts) == 4):
            raise ValueError("%s line %d: %s %s a detail"
                             % (path.name, n, parts[2], "needs" if needs else "takes no"))
        if (parts[0], parts[1]) in entries:
            raise ValueError("%s line %d: %s %s listed twice" % (path.name, n, parts[0], parts[1]))
        entries[(parts[0], parts[1])] = (parts[2], parts[3] if needs else None)
    return entries


def classify(case, subset, frozen, seed_rc, seed_rec, unsupported, held=None,
             compiler="cint-seed", *, seed_only=False):
    """The CONF-14 category of one case for one compiler; a pure function (D-8).

    case: the case id (`arith/x`, `anchors/<record id>`, `tables/<name>`). subset: the subset
    the case names (a table program names `cint-boot-1` by its marker). frozen: the frozen
    `.expect` body (its lines from `outcome` on), COMPUTED for anchor and table records, or None
    when the case has no `.expect` file. seed_only: the case's `seed outcome` header provides
    that expectation. seed_rc: the compiler's exit status, or None when no
    program exists or the compiler is not run. seed_rec: the compiler's diagnostic as `.expect`
    body lines (seed_diagnostic). unsupported: read_unsupported(); held: gen_expect.read_held().

    Returns (category, detail): category is one of CATEGORIES or "disagreement"; detail holds
    the code, position, limit, Open item or frozen outcome, or the reason of a disagreement."""
    held = held or {}
    listed = unsupported.get((case, compiler))
    if case in held:
        if frozen is None:
            return "held", {"open_item": held[case][0]}
        return "disagreement", {"reason": "held.txt lists a case that has a frozen .expect file"}
    if frozen is None:
        return "disagreement", {"reason": "no frozen .expect file, and held.txt does not list "
                                          "the case"}
    if seed_rc is None:
        if case.startswith("anchors/") and frozen == COMPUTED:
            return "not_applicable", {}
        if case.startswith("tables/") and frozen == COMPUTED and subset != "cint-boot-1" \
                and compiler == COMPILERS["seed"]:
            # CONF-14 category 4 belongs to the seed: cintc compiles every table (CINTC-02).
            return "outside_subset", {"marker": "absent"}
        return "disagreement", {"reason": "the compiler was not run on a program of the subset"}
    want = None if frozen == COMPUTED else list(frozen)
    want_error = bool(want) and want[0] == "outcome compile-error"
    if seed_rc == 0:
        if listed is not None:
            return "disagreement", {"reason": "unsupported.txt lists the case under %s (%s), but "
                                              "the compiler accepts it" % (listed[1], listed[0])}
        if want_error:
            return "disagreement", {"reason": "the frozen outcome is compile error %s, but the "
                                              "compiler accepts the program" % want[1][16:]}
        return "compared", {}
    rec = list(seed_rec or [])
    if len(rec) < 3 or rec[0] != "outcome compile-error":
        return "disagreement", {"reason": "the compiler failed with exit status %d and no "
                                          "diagnostic" % seed_rc}
    code, position = rec[1].split(" ", 1)[1], rec[2].split(" ", 1)[1]
    found = {"code": code, "position": position}
    if compiler == "cint-seed" and listed is None and subset != "cint-boot-1" and code in SUBSET_CODES:
        if seed_only and want != rec:
            return "disagreement", dict(found, reason="the code or position differs from the "
                                                      "boot/ case's frozen expectation")
        return "outside_subset", found
    if want_error and want[1:3] == rec[1:3]:
        if listed is not None:
            return "disagreement", dict(found, reason="unsupported.txt lists a case whose frozen "
                                                      "compile error the compiler reproduces")
        return "compared", found
    if listed is not None:
        if listed[0] != code:
            return "disagreement", dict(found, reason="unsupported.txt lists %s for the case"
                                                      % listed[0])
        return "unsupported", dict(found, compiler=compiler, limit=listed[1],
                                   frozen_outcome=want[0].split(" ", 1)[1] if want else COMPUTED)
    if code in SUBSET_CODES:
        reason = "a subset refusal of a cint-boot-1 case (SEED-06)"
    elif code.startswith("C9"):
        reason = "a C9xxx code that unsupported.txt does not list"
    elif want_error:
        reason = "the code or position differs from the frozen expectation (%s at %s)" % (
            want[1][16:], want[2][20:])
    else:
        reason = "a compile error where the frozen outcome is %s" % (
            want[0].split(" ", 1)[1] if want else "computed by cint_ref")
    return "disagreement", dict(found, reason=reason)


EXCLUSION_PREFIX = "# Boot scope exclusion: "


def read_exclusions(path: pathlib.Path) -> dict:
    """The boot-scope exclusions a required case list records, as {case: reason} (SPEC-09
    CONF-15, exclusion rule, Proposed by plan task 2.14 after G-C1 finding EV-1).

    An exclusion is a comment line `# Boot scope exclusion: <case> <reason>`. It reads the list,
    it does not approve it: the list's `# Status:` line still decides whether it may authorize
    acceptance (OQ-174). Raises ValueError on a malformed or repeated exclusion."""
    text = path.read_bytes().decode("ascii")
    out = {}
    for n, line in enumerate(text.split("\n"), start=1):
        if not line.startswith(EXCLUSION_PREFIX):
            continue
        case, _, reason = line[len(EXCLUSION_PREFIX):].partition(" ")
        if not re.fullmatch(r"[a-z0-9_]+(?:/[a-z0-9_.\-]+)+", case) or not reason.strip():
            raise ValueError("%s line %d: expected `%s<case> <reason>`" % (path.name, n, EXCLUSION_PREFIX))
        if case in out:
            raise ValueError("%s line %d: %s excluded twice" % (path.name, n, case))
        out[case] = reason
    return out


def excluded(required: dict, exclusions: dict, case: str, compiler: str) -> bool:
    """Whether the list excludes `case` for `compiler`: an exclusion names it and the list has
    no row for that case and compiler (a row is a requirement, and it wins)."""
    return case in exclusions and (case, compiler) not in required


def required_token(category: str, detail: dict):
    return detail.get("limit") if category == "unsupported" else \
        detail.get("open_item") if category == "held" else None


def check_required(required: dict, actual: dict, compiler: str, in_scope, exclusions=None) -> list:
    """CONF-15: the problems of one run against its required case list (empty when it holds).
    actual is {case: (category, detail)} for the cases this run classified. exclusions is
    read_exclusions() of the same list: an excluded case (excluded()) is a row of category
    `excluded`, which no run category contradicts; a disagreement still fails the run, and
    B1's outside_subset stays a CONF-14 problem of the producer (cint_accept)."""
    problems = []
    exclusions = exclusions or {}
    for case, (category, detail) in sorted(actual.items(), key=lambda kv: kv[0].encode("utf-8")):
        if category == "disagreement":
            continue   # counted as a disagreement already
        token = required_token(category, detail)
        want = required.get((case, compiler))
        if want is None and excluded(required, exclusions, case, compiler):
            continue
        if want is None:
            problems.append("%s: not in the required list (category %s)" % (case, category))
        elif want != (category, token):
            problems.append("%s: category %s%s; the required list says %s%s" % (
                case, category, " " + token if token else "", want[0],
                " " + want[1] if want[1] else ""))
    for case, comp in sorted(required, key=lambda k: k[0].encode("utf-8")):
        if comp == compiler and case not in actual and in_scope(case):
            problems.append("%s: in the required list but absent from the run" % case)
    return problems


def required_scope(only, quick):
    """in_scope of check_required: the listed cases a run reports when absent from it. Under
    --only the list follows the selection; an anchor is selected by its program's name, which a
    row does not carry, so under --only an absent anchor is left to a full run."""
    return lambda case: selected(case, only) and not (only and case.startswith("anchors/")) \
        and not (quick and case.startswith("tables/"))


def write_required(path: pathlib.Path, actual: dict, compiler: str, stage: str):
    """A required case list from this run's categories, for review before it is committed;
    `stage` names the stage (T1, T2) or the set (`SETS`) the list is for."""
    lines = ["# Required case list of %s %s (SPEC-09 CONF-15; slice 2 decision patch D-14)."
             % ("set" if stage in SETS else "stage", stage),
             "# One line per case and compiler: <case> <compiler> <category> [<detail>], where",
             "# the detail is the limit identifier of an unsupported case or the Open item of a",
             "# held case; the categories are those of CONF-14. Written by tools/cint_check.py",
             "# --write-required from a full-scope run, then reviewed; sorted by case bytes."]
    for case, (category, detail) in sorted(actual.items(), key=lambda kv: kv[0].encode("utf-8")):
        if category == "disagreement":
            raise ValueError("%s is a disagreement, which no required list names" % case)
        token = required_token(category, detail)
        lines.append(" ".join([case, compiler, category] + ([token] if token else [])))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(("\n".join(lines) + "\n").encode("ascii"))


def encodings(compiler: str = COMPILERS["seed"]) -> dict:
    """identity.encodings (RCPT-02, D-20): each encoding version this run relies on. A run of
    B1, which writes the reflection table, also relies on the canonical type signature of
    SPEC-03 A-20 that CONF-16 compares; the seed writes no table."""
    rt = (RT / "cint_rt.c").read_bytes().decode("utf-8")
    fault = sorted(set(re.findall(r'"(cint-core-1/fault/v\d+)"', rt)))
    formats = set()
    for path in CONF.glob("*/*.expect"):
        m = re.search(rb"^format (\d+)$", path.read_bytes(), re.M)
        formats.add(int(m.group(1)) if m else 1)
    if len(fault) != 1:
        raise ValueError("rt/cint_rt.c names %d fault encodings: %s" % (len(fault), fault))
    found = {"expect_formats": sorted(formats), "fault": fault[0]}
    if compiler == COMPILERS["b1"]:
        found["type_signature"] = sig.DOMAIN.decode("ascii")
    return found


def runtime_contract() -> str:
    """The contract name rt/cint_rt.h declares (CINT_RT_CONTRACT; RCPT-08)."""
    m = re.search(r'#define CINT_RT_CONTRACT "([^"]+)"',
                  (RT / "cint_rt.h").read_bytes().decode("utf-8"))
    if not m:
        raise ValueError("rt/cint_rt.h declares no CINT_RT_CONTRACT")
    return m.group(1)


# --------------------------------------------------------------------------- inventory

class Program:
    """One compiled unit: a .ci source and its calls, each with the record expected for it."""

    def __init__(self, name, root, rel, kind):
        self.name = name          # e.g. arith/add_i64_overflow, tables/add_checked_i8
        self.root = root          # the directory the module-relative path is relative to
        self.rel = rel            # module-relative path, e.g. arith/add_i64_overflow.ci
        self.kind = kind          # program, anchor, table
        self.cases = []           # (case line, label)
        self.frozen = []          # program cases: (frozen .expect path, entry, args)
        self.expected = None      # list of expected bodies, or a callable that computes them
        self.fuel = None
        self.depth = DEFAULT_DEPTH
        self.subset = "cint-boot-1"
        self.frozen_body = None   # program cases: the first entry's frozen `.expect` body
        self.format = 1           # program cases: the CONF-11 format of the frozen files
        self.seed_only = False    # a boot/ refusal case: its `.expect` holds the seed's outcome
        self.ref_stdout = []      # program cases: cint_ref's stdout bytes per entry, or None


def body(lines):
    """The `.expect` lines from `outcome` on (what an observer of a compiled program prints)."""
    for i, line in enumerate(lines):
        if line.startswith("outcome "):
            return lines[i:]
    raise ValueError("no outcome line")


def expect_lines(e) -> list:
    return ["%s %s" % kv for kv in e.lines]


def module_name(rel: str) -> str:
    return rel[:-3].replace("/", ".")


def typed_arg(text: str) -> Value:
    t, _, v = text.strip().partition(" ")
    if t == "Bool":
        return Value("Bool", v.strip() == "true")
    return Value(t, int(v, 10))


def render_arg(v: Value) -> str:
    if v.type == "Bool":
        return "Bool %s" % ("true" if v.value else "false")
    return "%s %d" % (v.type, v.value)


def selected(name: str, only) -> bool:
    """Whether --only selects a program or case name (no --only selects everything)."""
    return not only or any(o in name for o in only)


def inventory_program_cases(only, cats, ref_problems, lists):
    """Program cases with their frozen `.expect` files; a held case, or one without a frozen
    file, is classified here and not compiled."""
    held, unsupported = lists["held"], lists["unsupported"]
    programs = []
    for cat in gen_expect.CATEGORIES:
        for ci in sorted((CONF / cat).glob("*.ci")):
            rel = ci.relative_to(CONF).as_posix()
            case_id = rel[:-3]
            if not selected(case_id, only):
                continue
            data = ci.read_bytes()
            text = data.decode("utf-8", "replace")
            h = gen_expect.header(text)
            m = re.search(r"subset:? (cint-[a-z0-9-]+)", text)
            p = Program(case_id, CONF, rel, "program")
            p.subset = m.group(1) if m else "unknown"
            p.seed_only = gen_expect.seed_expect(text, h, case_id) is not None
            suffixes = ["" if len(h["entries"]) == 1 or e is None else "." + e for e in h["entries"]]
            paths = [ci.with_name(ci.stem + s + ".expect") for s in suffixes]
            cintc_file = False
            if p.seed_only and lists["compiler"] == COMPILERS["b1"]:
                # Decision 26 item 2 (compiler/OPEN.md CINTC-OQ-48; SPEC-09 CONF-11): the
                # seed's file is not cintc's expectation. cintc's is <case>.cintc.expect, frozen
                # from cint_ref, or held.txt holds <case>.cintc where cint_ref refuses the program.
                c_path = ci.with_name(ci.stem + gen_expect.CINTC + ".expect")
                c_held = held.get(case_id + gen_expect.CINTC)
                if c_held is not None or not c_path.is_file():
                    cats[case_id] = ("held", {"open_item": c_held[0]}) if c_held and not c_path.is_file() \
                        else ("disagreement", {"reason": "held.txt holds the cintc expectation, which has "
                                                         "a frozen file" if c_held else
                                                         "no frozen cintc expectation, and held.txt does not "
                                                         "list %s%s" % (case_id, gen_expect.CINTC)})
                    continue
                paths, suffixes, p.seed_only, cintc_file = [c_path], [""], False, True
            if case_id in held or not all(f.is_file() for f in paths):
                frozen = None if not all(f.is_file() for f in paths) else ["outcome frozen"]
                cats[case_id] = classify(case_id, p.subset, frozen, None, None, unsupported, held)
                continue
            if any("[" in a for a in h["args"]):
                cats[case_id] = ("disagreement", {"reason": "array arguments: the observer entry "
                                                            "carries scalars only (SPEC-09 CONF-13 "
                                                            "X-3)"})
                continue
            p.fuel = int(h["fuel"]) if h["fuel"] is not None else None
            p.depth = int(h["depth"]) if h["depth"] is not None else DEFAULT_DEPTH
            args = [typed_arg(a) for a in h["args"]]
            bodies = []
            formats = {gen_expect.expect_format(str(f)) for f in paths}
            if len(formats) != 1:
                cats[case_id] = ("disagreement", {"reason": "the entries' .expect files differ in format"})
                continue
            p.format = formats.pop()
            for entry, suffix, frozen in zip(h["entries"], suffixes, paths):
                want = frozen.read_bytes().decode("ascii").rstrip("\n").split("\n")
                # A boot/ refusal case's .expect holds the seed's outcome (CONF-14 category 4),
                # not cint_ref's, so it is not compared with cint_ref here.
                ref_out = None
                if h["case"] is not None and (cintc_file or gen_expect.seed_expect(text, h, case_id) is None):
                    o = run_program(data, rel, entry, p.fuel, p.depth, args=args, root=str(CONF))
                    ref_out = o.stdout
                    got = expect_lines(expect.from_outcome(o, h["case"] + suffix, h["clause"], None,
                                                           p.format))
                    if body(got) != body(want):
                        ref_problems.append("%s: cint_ref differs from the frozen %s"
                                            % (case_id + suffix, frozen.name))
                p.ref_stdout.append(ref_out)
                line = " ".join([module_name(rel), entry or "-"] + [render_arg(v) for v in args])
                p.cases.append((line, case_id + suffix))
                p.frozen.append((frozen, entry, args))
                bodies.append(body(want))
            p.expected = bodies
            p.frozen_body = bodies[0]
            programs.append(p)
    return programs


def ref_bodies(src: bytes, rel: str, fname: str, arg_lists):
    """cint_ref's `.expect` body for one call per argument list, each with a fresh entry."""
    with REF_LOCK:
        prog = check_module(P.parse_module(decode_source(src, rel), rel))
        f = prog.functions[fname]
        out = []
        for args in arg_lists:
            o = _run(prog, "function", f, _check_args(f, args), None, DEFAULT_DEPTH)
            out.append(body(expect_lines(expect.from_outcome(o, rel))))
        return out


def inventory_tables(only, cats, lists):
    programs = []
    for ci in sorted((CONF / "tables").glob("*.ci")):
        rel = ci.relative_to(CONF).as_posix()
        name = rel[:-3]
        if not selected(name, only):
            continue
        src = ci.read_bytes()
        if "// subset: cint-boot-1\n" not in src.decode("ascii") and lists["compiler"] == COMPILERS["seed"]:
            # a saturating form: outside cint-boot-1 (SPEC-09 5.3), so outside the seed's
            # subset (CONF-14 category 4); B1 has no such category and compiles it like any
            # table (CONF-15, CINTC-02, LS-311)
            cats[name] = classify(name, None, COMPUTED, None, None, lists["unsupported"],
                                  lists["held"])
            continue
        lines = ci.with_suffix(".cases").read_bytes().decode("ascii").split("\n")[:-1]
        p = Program(name, CONF, rel, "table")
        fname = lines[0].split(" ")[1]
        arg_lists = []
        for n, line in enumerate(lines, start=1):
            _, _, ta, a, tb, b = line.split(" ")
            arg_lists.append([Value(ta, int(a)), Value(tb, int(b))])
            p.cases.append((line, "%s:%d" % (name, n)))
        p.expected = (lambda s=src, r=rel, f=fname, al=arg_lists: ref_bodies(s, r, f, al))
        programs.append(p)
    return programs


# The integer built-ins of SPEC-01 2.4 (IM-134 operation names) by value-argument count,
# and the rounding modes of SPEC-01 4.5.
BUILTIN_ARITY = {"abs": 1, "uabs": 1, "min": 2, "max": 2, "clamp": 3, "div_trunc": 2,
                 "rem_trunc": 2, "div_euclid": 2, "rem_euclid": 2, "div_round": 2, "muldiv": 3,
                 "mul_full": 2, "isqrt": 1, "isqrt_round": 1, "rotl": 2, "rotr": 2}
ROUND_MODES = ("floor", "ceil", "trunc", "away", "half_even", "half_away", "half_trunc",
               "half_up", "half_down")


def builtin_form(parts: list, types: list):
    """anchor_form for a built-in operation `<name>.checked.<T>[.<R>][.<mode>]` (cintc only,
    box 07): a call of the built-in on the parameters, None when the operation names a type
    outside the scalar surface (I128, the result of mul_full on I64)."""
    name, t = parts[0], parts[2].upper()
    rest = parts[3:]
    mode = rest.pop() if rest and rest[-1] in ROUND_MODES else None
    result = rest.pop().upper() if rest else t
    if rest or (mode is not None) != (name in ("div_round", "muldiv", "isqrt_round")) \
            or len(types) != BUILTIN_ARITY[name] or result not in BOOT_TYPES \
            or types[0] != t or any(x != t for x in types[1:] if name not in ("rotl", "rotr")):
        return None
    args = ", ".join("abc"[:len(types)])
    if name == "muldiv":
        args = "%s, %s" % (result, args)
    if mode is not None:
        args += ", " + mode
    fname = "_".join(parts)
    return fname, result, types, "%s(%s)" % (name, args)


def anchor_form(op: str, types: list, compiler: str = "cint-seed"):
    """(program name, result type, parameter types, body expression) for an anchor record
    whose operation is a cint-boot-1 form (SPEC-09 5.5: binary operators, unary `-`, `as`
    and `as%`), or, for cintc, a saturating operator form (D-15 row 2) or a built-in call
    (box 07); None otherwise."""
    parts = op.split(".")
    if not types or not set(types) <= BOOT_TYPES:
        return None
    if compiler == COMPILERS["b1"] and len(parts) == 3 and (parts[0], parts[1]) in SAT_BINARY \
            and types == [parts[2].upper()] * 2:
        name = "%s_sat_%s_%s" % (parts[0], types[0].lower(), types[1].lower())
        return name, types[0], types, "a %s b" % SAT_BINARY[(parts[0], parts[1])]
    if len(parts) == 3 and (parts[0], parts[1]) in BOOT_BINARY and len(types) == 2 \
            and parts[2].upper() == types[0] \
            and (types[0] == types[1] or parts[0] in ("shl", "shr")):
        name = "%s_%s_%s_%s" % (parts[0], parts[1], types[0].lower(), types[1].lower())
        return name, types[0], types, "a %s b" % BOOT_BINARY[(parts[0], parts[1])]
    if compiler == COMPILERS["b1"] and len(parts) >= 3 and parts[0] in BUILTIN_ARITY \
            and parts[1] == "checked":
        return builtin_form(parts, types)
    if parts[:2] == ["neg", "checked"] and len(parts) == 3 and types == [parts[2].upper()]:
        return "neg_checked_%s" % parts[2], types[0], types, "-a"
    if len(parts) == 4 and parts[0] == "as" and parts[1] in ("checked", "wrap") \
            and types == [parts[2].upper()] and parts[3].upper() in BOOT_TYPES:
        return ("as_%s_%s_%s" % (parts[1], parts[2], parts[3]), parts[3].upper(), types,
                "a %s %s" % ("as" if parts[1] == "checked" else "as%", parts[3].upper()))
    return None


def inventory_anchors(gen_root: pathlib.Path, only, cats, lists):
    """Programs for the anchor records whose operations are cint-boot-1 forms."""
    groups = collections.OrderedDict()
    for _, r in cif1.read_file(str(CONF / "integer-machine" / "anchors.cif1.jsonl")):
        try:
            types = [a.type for a in (cif1._value(x) for x in r.args)]
        except ValueError:
            types = [str(x.get("t", "?")) if isinstance(x, dict) else "?" for x in r.args]
        form = anchor_form(r.op, types, lists["compiler"])
        if form is None:   # no operator form in cint-boot-1, so no program exists
            if not selected("anchors/" + r.id, only):
                continue   # selected by its own name, as it has no program's name
            category, detail = classify("anchors/" + r.id, None, COMPUTED, None, None,
                                        lists["unsupported"], lists["held"])
            cats["anchors/" + r.id] = (category, dict(detail, operation="%s on %s" % (
                r.op, ", ".join(types) or "no operands")))
            continue
        groups.setdefault((form[0], form[1], tuple(form[2]), form[3]), []).append(r)
    programs = []
    (gen_root / "anchors").mkdir(parents=True, exist_ok=True)
    for (fname, result, params, expr), recs in groups.items():
        rel = "anchors/%s.ci" % fname
        if not selected(rel[:-3], only):
            continue
        decl = ", ".join("%s %s" % (t, n) for t, n in zip(params, "abc"))
        src = ("// generated by tools/cint_check.py from conformance/integer-machine/"
               "anchors.cif1.jsonl; do not edit\n// subset: %s\n"
               "export %s %s(%s) {\n    return %s;\n}\n"
               % ("cint-core-1" if "|" in expr or "(" in expr else "cint-boot-1", result, fname,
                  decl, expr)).encode("ascii")
        (gen_root / rel).write_bytes(src)
        p = Program(rel[:-3], gen_root, rel, "anchor")
        arg_lists = [[cif1._value(x) for x in r.args] for r in recs]
        for r, args in zip(recs, arg_lists):
            p.cases.append((" ".join([module_name(rel), fname] + [render_arg(v) for v in args]),
                            "anchors/" + r.id))
        p.expected = ref_bodies(src, rel, fname, arg_lists)
        programs.append(p)
    return programs


# --------------------------------------------------------------------------- one program

def run_seed(seed_exe, root: pathlib.Path, rel: str, out_c: pathlib.Path, env=None):
    r = subprocess.run([str(seed_exe), rel, "-o", str(out_c)], cwd=root, capture_output=True,
                       env=env)
    return r.returncode, r.stderr.decode("utf-8", "replace")


IMPORT_RE = re.compile(rb"(?m)^[ \t]*import[ \t]+([A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*)"
                       rb"(?:[ \t]+as[ \t]+[A-Za-z_][A-Za-z0-9_]*|[ \t]*\.\{[^}]*\})?[ \t]*;")


def exact_file(root: pathlib.Path, rel: str) -> bool:
    """Whether rel names a regular file under root with exactly these bytes in every segment (a
    case-folding file system would otherwise find module/lib/Plain_math.ci for plain_math)."""
    here = root
    for part in rel.split("/"):
        try:
            if part not in os.listdir(here):
                return False
        except OSError:
            return False
        here = here / part
    return here.is_file()


def module_order(root: pathlib.Path, rel: str) -> list:
    """The modules of the program whose root module is rel, in the order of slice 2 decision
    D-13: each module after every module it imports, ties in byte order of path, the root module
    last. An import is `import a.b;` at the start of a line (SPEC-04 LS-225: path a/b.ci under
    root); an import that names no file is left to the compiler to diagnose."""
    deps, pending = {}, [rel]
    while pending:
        m = pending.pop()
        if m in deps:
            continue
        found = []
        for name in IMPORT_RE.findall((root / m).read_bytes()):
            dep = name.decode("ascii").replace(".", "/") + ".ci"
            if dep != m and exact_file(root, dep):
                found.append(dep)
        deps[m] = sorted(set(found), key=lambda d: d.encode("utf-8"))
        pending += deps[m]
    order, done = [], set()
    while len(order) < len(deps):
        ready = sorted((m for m in deps if m not in done and m != rel and
                        all(d in done for d in deps[m])), key=lambda m: m.encode("utf-8"))
        if not ready:   # a cycle, or only the root module left: the root module goes last
            ready = [m for m in sorted(deps, key=lambda m: m.encode("utf-8")) if m not in done]
            ready = [m for m in ready if m != rel] or [rel]
        order.append(ready[0])
        done.add(ready[0])
    return order


class EmitCache:
    """B1 compilations kept across runs (--emit-cache DIR; module docstring, item 3).

    One entry per key, a directory `<key>` holding `entry.json` (exit status, stderr, the
    MANIFEST.ref line and the output set's file list) and `set/` (the output set). An entry is
    written to a temporary directory and renamed into place, so runs sharing DIR see whole
    entries only; when two runs store one key, the first rename wins and the bytes are equal."""

    def __init__(self, path: pathlib.Path, b1: dict):
        self.path = path
        self.base = {"cint_sha256": b1["sha256"], "compiler_source_identity": b1["identity"],
                     "tool_sha256": files_digest([pathlib.Path(__file__).resolve()])}
        self.lock = threading.Lock()
        self.roots = {}
        self.counts = {"emitted": 0, "reused": 0}
        path.mkdir(parents=True, exist_ok=True)

    def modules(self, root: pathlib.Path) -> dict:
        """{path: sha256} of every .ci file under root, which B1 may read (once per root)."""
        with self.lock:
            found = self.roots.get(str(root))
        if found is None:
            found = {}
            for d, _, names in os.walk(root):
                for name in names:
                    if name.endswith(".ci"):
                        f = pathlib.Path(d) / name
                        found[f.relative_to(root).as_posix()] = sha256(f.read_bytes())
            with self.lock:
                self.roots[str(root)] = found
        return found

    def key(self, root: pathlib.Path, rels: list, env) -> str:
        # The first compilation (env None) and the second (ALT_ENV) never share a key, so the
        # CONF-05 comparison is always between two compilations.
        return sha256(json.dumps(dict(self.base, rels=rels, modules=self.modules(root),
                                      env=None if env is None else {k: env.get(k) for k in ALT_ENV}),
                                 sort_keys=True, separators=(",", ":")).encode("ascii"))

    def load(self, key: str, out: pathlib.Path):
        """(exit status, stderr) of a kept compilation, its output set written under out as B1
        writes it; None when DIR has no entry for key."""
        entry = self.path / key
        try:
            meta = json.loads((entry / "entry.json").read_bytes().decode("ascii"))
            if meta["ref"] is not None:
                stage = out.parent / meta["ref"].split(" ")[0]
                for rel in meta["files"]:
                    (stage / rel).parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(entry / "set" / rel, stage / rel)
                out.mkdir(parents=True, exist_ok=True)
                (out / "MANIFEST.ref").write_bytes(meta["ref"].encode("ascii"))
        except (OSError, ValueError, KeyError):
            shutil.rmtree(out.parent, ignore_errors=True)
            out.parent.mkdir(parents=True)
            return None
        self.count("reused")
        return meta["code"], meta["stderr"]

    def count(self, what: str):
        with self.lock:
            self.counts[what] += 1

    def store(self, key: str, code: int, err: str, ref, files):
        # An output set or a compile error (exit status 2, CINT_EXIT_COMPILE) is kept; any other
        # exit status, and a stage directory this cache cannot place again, are not.
        if code not in (0, 2) or ref is not None and not re.fullmatch(r"[A-Za-z0-9_.\-]+",
                                                                      ref.split(" ")[0]):
            return
        tmp = pathlib.Path(tempfile.mkdtemp(prefix="tmp-", dir=self.path))
        try:
            for rel, data in (files or {}).items():
                (tmp / "set" / rel).parent.mkdir(parents=True, exist_ok=True)
                (tmp / "set" / rel).write_bytes(data)
            (tmp / "entry.json").write_bytes(json.dumps(
                {"code": code, "files": sorted(files or {}), "ref": ref, "stderr": err},
                sort_keys=True).encode("ascii"))
            os.rename(tmp, self.path / key)
        except OSError:
            shutil.rmtree(tmp, ignore_errors=True)


def run_b1(cint, root: pathlib.Path, rels: list, out: pathlib.Path, env=None, cache=None):
    """`cint --json emit-c` (task 2.12a): exit status, stderr, and the committed output set as
    (stage directory, {relative path: bytes} with its MANIFEST), or None when nothing was
    committed. With cache (EmitCache), a kept compilation of the same key is written back under
    out and read exactly as one B1 has just run."""
    shutil.rmtree(out.parent, ignore_errors=True)
    out.parent.mkdir(parents=True)
    key = cache.key(root, rels, env) if cache else None
    kept = cache.load(key, out) if cache else None
    if kept is None:
        r = subprocess.run([str(cint), "--json", "emit-c", "--root", str(root), "--out", str(out), *rels],
                           cwd=out.parent, capture_output=True, env=env)
        code, err = r.returncode, r.stderr.decode("utf-8", "replace")
        if cache:
            cache.count("emitted")
    else:
        code, err = kept
    if code != 0:
        if cache and kept is None:
            cache.store(key, code, err, None, None)
        return code, err, None
    ref = (out / "MANIFEST.ref").read_bytes().decode("ascii")
    stage = out.parent / ref.split(" ")[0]
    manifest = (stage / "MANIFEST").read_bytes()
    if sha256(manifest) != ref.rstrip("\n").split(" ")[1]:
        return 7, "MANIFEST.ref does not name the MANIFEST it points to", None
    files = {"MANIFEST": manifest}
    for line in manifest.decode("ascii").split("\n")[1:]:
        if line:
            digest, _size, rel = line.split(" ", 2)
            files[rel] = (stage / rel).read_bytes()
            if sha256(files[rel]) != digest:
                return 7, "%s differs from its MANIFEST line" % rel, None
    if cache and kept is None:
        cache.store(key, code, err, ref, files)
    return 0, err, (stage, files)


def b1_diagnostic(stderr: str) -> list:
    """The `.expect` body of B1's first CINT-DIAG-1 line (SPEC-06 15; LS-282 order), with its
    compile-time fault as the `diagnostic.fault.*` lines of CONF-11."""
    for line in stderr.splitlines():
        try:
            d = json.loads(line)
        except ValueError:
            continue
        if not isinstance(d, dict) or d.get("schema") != "CINT-DIAG-1":
            continue
        lines = ["outcome compile-error", "diagnostic.code " + d["code"],
                 "diagnostic.position %s:%d:%d" % (d["file"], d["line"], d["column"])]
        f = d.get("fault")
        if f:
            lines += ["diagnostic.fault.code " + f["code"], "diagnostic.fault.operation " + f["operation"]]
            lines += ["diagnostic.fault.operand %s %s" % (o["t"], o["v"]) for o in f["operands"]]
            lines.append("diagnostic.fault.exact " + (f["exact"]["v"] if f["exact"] else "none"))
            lines.append("diagnostic.fault.limit " + ("%s %s" % (f["limit"]["t"], f["limit"]["v"])
                                                      if f["limit"] else "none"))
        return lines
    return ["outcome compiler-failed", "cint.stderr " + " ".join(stderr.split())[:300]]


def b1_source_identity() -> str:
    """The compiler source identity of this tree: sha256 of the CISRC001 manifest of
    compiler/main.ci's import closure (tools/cint_bootstrap.py; the CLI's
    CINT_COMPILER_SOURCE_IDENTITY)."""
    import cint_bootstrap
    sources = {"compiler/" + f.name: f.read_bytes() for f in sorted((ROOT / "compiler").glob("*.ci"))}
    return sha256(cint_bootstrap.source_manifest(cint_bootstrap.compiler_closure(sources)))


def b1_tool(leg: str, given, out: pathlib.Path) -> dict:
    """The B1 `cint` executable: --cint PATH, or one bootstrapped from this tree into out
    (tools/cint_bootstrap.py). Its toolchain file must name this tree's compiler sources."""
    if given:
        exe = pathlib.Path(given).resolve()
        how = "given"
    else:
        if leg not in ("msvc", "gcc", "clang", "apple-clang"):
            raise BuildError("the %s leg has no bootstrap; give --cint PATH" % leg)
        r = subprocess.run([sys.executable, str(ROOT / "tools" / "cint_bootstrap.py"), "--leg", leg,
                            "--out", str(out)], cwd=ROOT, capture_output=True, text=True)
        if r.returncode != 0:
            raise BuildError("cint_bootstrap.py: exit %d\n%s%s" % (r.returncode, r.stdout[-2000:],
                                                                   r.stderr[-2000:]))
        exe = out / ("cint.exe" if leg == "msvc" else "cint")
        how = "bootstrapped"
    lines = (exe.parent / "cint.toolchain").read_bytes().decode("utf-8").split("\n")
    sources = next(line[len("compiler_sources "):] for line in lines
                   if line.startswith("compiler_sources "))
    identity, want = sha256(pathlib.Path(sources).read_bytes()), b1_source_identity()
    if identity != want:
        raise BuildError("B1 at %s was built from compiler sources %s, not this tree's %s"
                         % (exe, identity, want))
    return {"cint": exe, "identity": identity, "how": how, "sha256": sha256(exe.read_bytes())}


def sanitizer_reports(stderr: str) -> int:
    return len(re.findall(r"runtime error:|ERROR: (?:Address|Leak)Sanitizer", stderr))


def seed_diagnostic(stderr: str) -> list:
    """The `.expect` body of the seed's first diagnostic (SPEC-09 DIAG-01; seed/diag.c)."""
    first = stderr.replace("\r\n", "\n").split("\n", 1)[0]
    m = DIAG_RE.match(first)
    if not m:
        return ["outcome seed-failed", "seed.stderr " + " ".join(stderr.split())[:300]]
    lines = ["outcome compile-error", "diagnostic.code " + m.group("code"),
             "diagnostic.position %s:%s:%s" % (m.group("path"), m.group("line"), m.group("col"))]
    lines += ["diagnostic." + item for item in m.group("text").split("; ") if item.startswith("fault.")]
    return lines


def parse_harness(stdout: bytes, count: int):
    blocks = [None] * count
    current = None
    for line in stdout.decode("ascii", "replace").split("\n"):
        if line.startswith("case "):
            n = int(line[5:])
            if not 1 <= n <= count:
                raise ValueError("harness printed case %d of %d" % (n, count))
            current = blocks[n - 1] = []
        elif line and current is not None:
            current.append(line)
    return blocks


def ref_check(p: Program, observed, work: pathlib.Path) -> list:
    """`python -m cint_ref check` on each program-case record the seed and harness produced:
    per case, None when cint_ref agrees, else the problem."""
    problems = []
    for (frozen, entry, args), got in zip(p.frozen, observed):
        if got is None or not got[0].startswith("outcome ") or got[0] in (
                "outcome seed-failed", "outcome build-failed"):
            problems.append({"case": frozen.stem, "cint_ref_check": "no record observed"})
            continue
        lines = frozen.read_bytes().decode("ascii").split("\n")
        head = lines[:next(i for i, line in enumerate(lines) if line.startswith("outcome "))]
        obs = work / (frozen.stem + ".observed.expect")
        obs.write_bytes(("\n".join(head + got) + "\n").encode("ascii"))
        cmd = [sys.executable, "-m", "cint_ref", "check", str(p.root / p.rel), str(obs),
               "--path", p.rel, "--depth", str(p.depth)]
        if entry is not None:
            cmd += ["--entry", entry]
        if p.fuel is not None:
            cmd += ["--fuel", str(p.fuel)]
        for v in args:
            cmd += ["--arg", render_arg(v)]
        r = subprocess.run(cmd, cwd=REF, capture_output=True, text=True,
                           env=dict(os.environ, PYTHONPATH=str(REF)))
        problems.append(None if r.returncode == 0 else {"case": frozen.stem, "cint_ref_check": " ".join(
            (r.stdout + r.stderr).split())[:600]})
    return problems


def not_run(res: dict, p: Program, reason: str) -> dict:
    """A program classified as a disagreement before it runs: every call counts as compared and
    disagreeing, so the run fails (CONF-14)."""
    res["compared"] = len(p.cases)
    res["disagreements"].append({"case": p.name, "reason": reason})
    return res


# --------------------------------------------------------------------------- type signatures

# SPEC-03 A-19 and A-20, as compiler/back_c.ci writes them into a module's C file (rt/cint_rt.h
# section 6b''): one byte array `cg<P>_xs<j>` per canonical type signature, sixteen bytes to a
# line, and the module's rows in `cg<P>_exports`, one line each, in declaration order. Each row
# names its export, its kind and effect class (not part of the signature, so not compared), its
# size and parameter names, its signature array and the array's size, and its cx wrapper.
SIG_ARRAY_RE = re.compile(r"^static const uint8_t (cg\w+_xs\d+)\[\] = \{\n((?:    [^\n]*\n)*?)\};$",
                          re.M)
SIG_BYTES_RE = re.compile(r"(?:   (?: 0x[0-9a-f]{2},)+\n)*")
EXPORTS_RE = re.compile(r"^static const cint_export (cg\w+)_exports\[(\d+)\] = \{\n"
                        r"((?:    [^\n]*\n)*?)\};$", re.M)
EXPORT_ROW_RE = re.compile(r'    \{\{"([A-Za-z_][A-Za-z0-9_]*)", (\d+)u, 0u\}, '
                           r'CINT_EXPORT_(?:FUNCTION|KERNEL), CINT_[A-Z]+, \d+u, \d+u, '
                           r'(?:NULL|&cg\w+_xn\d+\[\d+\]), (?:NULL|&cg\w+_xn\d+\[\d+\]), '
                           r'(cg\w+_xs\d+), \(uint64_t\)sizeof\((cg\w+_xs\d+)\), '
                           r'\(cint_entry_fn\)(cx\w+)\},')
WRAPPER_RE = re.compile(r"^CINT_RT_EXPORT cint_status (cx\w+)\(", re.M)


def table_rows(c_text: str):
    """The export rows of the reflection table in one module's C file, in row order, as (name,
    type signature bytes), and the problems that keep the table from being read: a row not in
    the form above, a signature array that is not a list of bytes, a row whose length is not its
    name's or whose array is not its own, and a cx wrapper that no row names or a row naming no
    wrapper."""
    arrays, problems, rows = {}, [], []
    for m in SIG_ARRAY_RE.finditer(c_text):
        if not SIG_BYTES_RE.fullmatch(m.group(2)):
            problems.append("%s is not a list of bytes" % m.group(1))
        arrays[m.group(1)] = bytes(int(h, 16) for h in re.findall(r"0x([0-9a-f]{2})", m.group(2)))
    tables = list(EXPORTS_RE.finditer(c_text))
    if len(tables) > 1:
        problems.append("%d export tables in one module" % len(tables))
    calls = []
    for t in tables[:1]:
        lines = t.group(3).split("\n")[:-1]
        if len(lines) != int(t.group(2)):
            problems.append("%s_exports declares %s rows and holds %d"
                            % (t.group(1), t.group(2), len(lines)))
        for line in lines:
            r = EXPORT_ROW_RE.fullmatch(line)
            if r is None:
                problems.append("a row not in the form compiler/back_c.ci writes: "
                                + line.strip()[:160])
                continue
            name, length, data, sized, call = r.groups()
            if int(length) != len(name) or sized != data or data not in arrays:
                problems.append("the row of %s names its length or its signature array "
                                "inconsistently" % name)
                continue
            rows.append((name, arrays[data]))
            calls.append(call)
    wrappers = WRAPPER_RE.findall(c_text)
    for call in sorted(set(wrappers) - set(calls)):
        problems.append("cx wrapper %s has no row" % call)
    for call in sorted(set(calls) - set(wrappers)):
        problems.append("a row names %s, which the file does not define" % call)
    return rows, problems


def signature_problems(files: dict, rels: list, root_rel: str, expected) -> tuple:
    """SPEC-09 CONF-16 (box 10 default BX10-29) over one output set: (exports compared,
    [disagreement]). files maps each module's C file (`<rel without .ci>.c`) to its bytes;
    expected is cint_ref's sig.Export rows of the root module root_rel, or the reason cint_ref
    has none (a compile error or a refusal). Under the interim rule of SPEC-03 A-12 the root
    module's exports with a type signature have rows, in declaration order, and no other
    module has one (A-19). A row whose bytes differ from cint_ref's, a row with no cint_ref
    signature, a cint_ref signature with no row, and an unreadable table are disagreements.
    An exported kernel has a cx wrapper and a row since box 10 unit 3 (compiler/OPEN.md
    CINTC-OQ-58 item 8), so it is compared as a function is."""
    compared, bad = 0, []
    for rel in rels:
        data = files.get(rel[:-3] + ".c")
        if data is None:
            bad.append({"module": rel, "reason": "no C file in the output set"})
            continue
        rows, problems = table_rows(data.decode("utf-8", "replace"))
        bad += [{"module": rel, "reason": problem} for problem in problems]
        if rel != root_rel:
            want, why = [], "not the root module (SPEC-03 A-12, interim rule)"
        elif isinstance(expected, str):
            want, why = [], expected
        else:
            want, why = [e for e in expected if e.signature is not None], ""
        ref = {e.name: e.signature for e in want}
        reasons = {e.name: e.reason for e in ([] if isinstance(expected, str) else expected)}
        names = [name for name, _ in rows]
        for name, got in rows:
            compared += 1
            if names.count(name) > 1:
                bad.append({"module": rel, "export": name, "reason": "two rows"})
            elif name not in ref:
                bad.append({"module": rel, "export": name, "reason": "a row and a cx wrapper, and "
                            "no cint_ref type signature: " + (why or reasons.get(name)
                                                              or "not an export of the module")})
            elif got != ref[name]:
                at = next((i for i, (a, b) in enumerate(zip(got, ref[name])) if a != b),
                          min(len(got), len(ref[name])))
                bad.append({"module": rel, "export": name, "reason": "the type signature differs "
                            "from cint_ref's at byte %d" % at, "actual": got.hex()[:1024],
                            "expected": ref[name].hex()[:1024]})
        for e in want:
            if e.name not in names:
                compared += 1
                bad.append({"module": rel, "export": e.name, "reason": "cint_ref writes a type "
                            "signature, and the table has no row"})
        order = [e.name for e in want if e.name in names]
        if [name for name in names if name in ref] != order:
            bad.append({"module": rel, "reason": "rows not in declaration order: "
                        + ", ".join(names)})
    return compared, bad


def ref_exports(p: Program):
    """cint_ref's exports of the root module of p (sig.program_exports), or why it has none."""
    with REF_LOCK:
        try:
            return sig.program_exports((p.root / p.rel).read_bytes(), p.rel, str(p.root))
        except CompileError as e:
            return "cint_ref reports %s at %s" % (e.diagnostic.code, e.diagnostic.position)
        except Refused as e:
            return "cint_ref refuses the program: " + e.message


def process(p: Program, tc: Toolchain, tools: dict, out: pathlib.Path, alt: pathlib.Path,
            lists: dict):
    """Compile, classify (CONF-14), determinism-check, build and run one program; return its
    result record."""
    res = {"name": p.name, "kind": p.kind, "compared": 0, "agree": 0, "disagreements": [],
           "sanitizer": 0, "category": None, "emitted_sha256": None, "determinism": None,
           "symbols": [], "executions": 0, "ref_checked": 0, "exports": 0, "audited": False,
           "entry": False, "compile_errors": 0, "signatures": 0, "signature_disagreements": [],
           "labels": [label for _, label in p.cases]}
    work = out / "programs" / p.name
    work.mkdir(parents=True, exist_ok=True)
    b1 = lists["compiler"] == COMPILERS["b1"]
    if b1:   # every module in D-13 order through `cint emit-c`; the output set's C files
        rels = module_order(p.root, p.rel)
        code, err, outset = run_b1(tools["b1"]["cint"], p.root, rels, work / "b1" / "out",
                                   cache=tools["b1"].get("cache"))
        got = b1_diagnostic(err) if code != 0 else None
        c_file = sorted((outset[0] / rel for rel in outset[1] if rel.endswith(".c")),
                        key=lambda f: str(f).encode("utf-8")) if outset else None
        res["modules"] = len(rels)
    else:
        c_file = work / (pathlib.Path(p.rel).stem + ".c")
        code, err = run_seed(tools["seed"], p.root, p.rel, c_file)
        got = seed_diagnostic(err) if code != 0 else None
    res["sanitizer"] += sanitizer_reports(err)
    # A boot/ refusal case reaches B1 with its cintc expectation (inventory_program_cases).
    frozen = p.frozen_body if p.kind == "program" else COMPUTED
    res["category"] = classify(p.name, p.subset, frozen, code, got, lists["unsupported"],
                               lists["held"], lists["compiler"], seed_only=p.seed_only)
    category, detail = res["category"]
    if category in ("unsupported", "outside_subset"):
        return res
    if category == "disagreement":
        return not_run(res, p, "%s%s" % (detail["reason"], " (%s at %s)" % (
            detail["code"], detail["position"]) if "code" in detail else ""))
    if code != 0:   # an expected compile error with the frozen code and position
        res["compile_errors"] = 1
        observed = [got] * len(p.cases)
    else:
        res["emitted_sha256"] = sha256(outset[1]["MANIFEST"] if b1 else c_file.read_bytes())
        if tools["program_defines"]:   # --backend gpu-cuda: the PTX headers of its device code
            res["ptx"] = sorted(ptx_headers(c_file if b1 else [c_file]))
        if b1:   # CONF-16: B1 writes the reflection table (SPEC-03 A-19); the seed writes none
            res["signatures"], res["signature_disagreements"] = signature_problems(
                outset[1], rels, p.rel, ref_exports(p))
        alt_root = alt / p.name
        (alt_root / pathlib.Path(p.rel).parent).mkdir(parents=True, exist_ok=True)
        shutil.copyfile(p.root / p.rel, alt_root / p.rel)
        if re.search(rb"(?m)^import ", (p.root / p.rel).read_bytes()):
            # Imported modules are read from the same source root (SPEC-04 LS-225): copy the
            # case's category directory with its subdirectories.
            top = pathlib.PurePosixPath(p.rel).parts[0]
            shutil.copytree(p.root / top, alt_root / top, dirs_exist_ok=True)
        env = dict(os.environ, **ALT_ENV)
        if b1:   # the second output set must equal the first, file for file (CONF-05)
            code2, err2, set2 = run_b1(tools["b1"]["cint"], alt_root, rels,
                                       alt / "b1-second" / p.name / "out", env,
                                       tools["b1"].get("cache"))
            res["determinism"] = code2 == 0 and set2[1] == outset[1]
        else:
            c2 = alt_root / "second.c"
            code2, err2 = run_seed(tools["seed"], alt_root, p.rel, c2, env)
            res["determinism"] = code2 == 0 and c2.read_bytes() == c_file.read_bytes()
        res["sanitizer"] += sanitizer_reports(err2)
        sir_dir = outset[0] if b1 else None
        if b1 and len(p.cases) == 1 and p.cases[0][0].split(" ")[1] == "-" and p.format in (1, 2, 3):
            observed = run_program_entry(p, tc, tools, work, c_file, res, sir_dir)
        elif any(c[1] == "-" for c in (line.split(" ") for line, _ in p.cases)):
            res["category"] = ("disagreement", {"reason": "no exported entry"})
            return not_run(res, p, "no exported entry named in the case header: the harness "
                                   "runs exported entries only")
        else:
            observed = run_compiled(p, tc, tools, work, c_file, res, sir_dir)
        if observed is None and "backend_refusal" in res:   # BACK-05, --backend gpu-cuda
            res["category"] = ("unsupported", dict(res.pop("backend_refusal"),
                                                   compiler=lists["compiler"]))
            return res
        if observed is None:   # the interpreter leg: a form cpu-sir-interp does not run yet
            res["category"] = ("not_executed", {"reason": res.pop("interp_reason")})
            return res
    expected = p.expected() if callable(p.expected) else p.expected
    checked = ref_check(p, observed, work) if p.kind == "program" else [None] * len(p.cases)
    res["ref_checked"] = len(p.frozen)
    for (line, label), want, got, problem in zip(p.cases, expected, observed, checked):
        res["compared"] += 1
        if got == want and problem is None:
            res["agree"] += 1
        elif len(res["disagreements"]) < 20:
            res["disagreements"].append({"case": label, "call": line, "expected": want,
                                         "actual": got, "cint_ref_check": problem})
    return res


def ci_path(symbol: str):
    """The first EMIT-22 component C(P) of a `ci_` symbol, `_<L>_<E(P)>`: the module path P
    of the function it names (D-2), or None for a symbol of another form."""
    m = re.match(r"ci_([0-9]+)_", symbol)
    if not m or len(symbol) < m.end() + int(m.group(1)):
        return None
    return symbol[:m.end() + int(m.group(1))]


def audit_program(objects, exports: set, rt_defined: set, leg: str) -> list:
    """The EMIT-31 audit of one program: [(defined, undefined) per object] and its library's
    exports, against what the runtime object defines (module docstring, item 5), and the
    support list of the leg's target (EMIT31_SUPPORT and EMIT31_TARGET[leg])."""
    support = EMIT31_SUPPORT | EMIT31_TARGET.get(leg, frozenset())
    ours = set().union(*(d for d, _ in objects))
    bad = []
    for i, (defined, undefined) in enumerate(objects):
        others = set().union(*(d for k, (d, _) in enumerate(objects) if k != i))
        bad += sorted("defines " + s for s in defined
                      if not s.startswith(PUBLIC_PREFIXES + INTERNAL_PREFIXES) and s not in OBSERVER_SYMBOLS)
        bad += sorted("refers to " + s for s in undefined
                      if not (s.startswith("cint_") and s in rt_defined) and s not in support
                      and not (s.startswith(INTERNAL_PREFIXES) and s in others))
    bad += sorted("exports " + s for s in exports
                  if not s.startswith(PUBLIC_PREFIXES) and s not in OBSERVER_SYMBOLS
                  and not (s in rt_defined and s not in ours))
    # Cross-module `ci_` rule (D-2, EMIT-31 "Same build"; task 2.13 part (iii)): a module's
    # object defines `ci_` functions of its own path only, and an object refers to another
    # module's `ci_` function only when exactly one other object, the one of that path,
    # defines it.
    owner = {}
    for i, (defined, _) in enumerate(objects):
        paths = {ci_path(s) for s in defined if s.startswith("ci_")}
        if len(paths) > 1 or None in paths:
            bad.append("object %d defines ci_ symbols of %d module paths" % (i, len(paths)))
        for p in paths:
            owner.setdefault(p, set()).add(i)
    for i, (defined, undefined) in enumerate(objects):
        for s in sorted(u for u in undefined if u.startswith("ci_")):
            definers = [k for k, (d, _) in enumerate(objects) if k != i and s in d]
            if len(definers) != 1 or owner.get(ci_path(s), set()) != set(definers):
                bad.append("refers to %s, which no single object of its module path defines" % s)
    return bad



def audit_coverage(ran) -> dict:
    """The coverage counts of the symbol audit over the programs run: the exports of the audited
    libraries, the number of program libraries audited (whatever their export count), and the
    emitted programs with a script or `void main()` entry, which are linked as executables with
    a C `main` and are not audited (CINTC-OQ-36). Every emitted program is in exactly one of the
    last two when the audit covers the run."""
    return {"program_exports": sum(r["exports"] for r in ran if r["audited"]),
            "program_objects": sum(1 for r in ran if r["audited"]),
            "entry_programs": sorted((r["name"] for r in ran if r["entry"] and r["emitted_sha256"]),
                                     key=lambda n: n.encode("utf-8"))}


ERROR_DOMAIN = b"cint-core-1/error-result/v1"   # rt/cint_rt.h CINT_ERROR_RECORD_DOMAIN
ERROR_TAGS = {0x21: ("U8", 8), 0x22: ("U16", 16), 0x23: ("U32", 32), 0x24: ("U64", 64)}
ERROR_SET_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*\Z")
ERROR_VALUE_RE = re.compile(r"(?:[A-Za-z_][A-Za-z0-9_]*\.)?[A-Za-z_][A-Za-z0-9_]*\Z")


def decode_error_record(data: bytes) -> list:
    """The `error.*` lines of CONF-11 rule 12 for the error record a program writes when it exits
    with status 6 (rt/cint_rt.h 6d; rt/OPEN.md RT-OQ-33): the domain, the set's name and the
    value's name, each a U32 length and its bytes, then the U32 tag code of the set's underlying
    type and the U64 tag, all little-endian. ValueError for any other bytes."""
    pos, parts = 0, []
    for _ in range(3):
        n = int.from_bytes(data[pos:pos + 4], "little")
        if len(data) < pos + 4 + n or n == 0:
            raise ValueError("truncated error record")
        parts.append(data[pos + 4:pos + 4 + n])
        pos += 4 + n
    if parts[0] != ERROR_DOMAIN or len(data) != pos + 12:
        raise ValueError("not an error record of domain %s, or not of its length" % ERROR_DOMAIN.decode("ascii"))
    code = int.from_bytes(data[pos:pos + 4], "little")
    tag = int.from_bytes(data[pos + 4:], "little")
    if code not in ERROR_TAGS or tag == 0 or tag >> ERROR_TAGS[code][1]:
        raise ValueError("error record: tag %d of type code %#x" % (tag, code))
    name, value = parts[1].decode("ascii"), parts[2].decode("ascii")
    if not ERROR_SET_RE.match(name) or not ERROR_VALUE_RE.match(value):
        raise ValueError("error record: malformed set or value name")
    return ["error.set " + name, "error.value " + value, "error.tag %s %d" % (ERROR_TAGS[code][0], tag)]


def state_lines(path: pathlib.Path) -> list:
    """The `state.global` lines a program with module state writes to its third destination
    (cint_program_run_state, rt/cint_rt.h 6d); none when it wrote no file."""
    if not path.is_file():
        return []
    text = path.read_bytes().decode("ascii")
    lines = text.split("\n")
    if lines.pop() != "" or any(not line.startswith("state.global ") for line in lines):
        raise ValueError("malformed state destination")
    return lines


def interp_refused(res: dict, r) -> bool:
    """Whether cint-interp declined a program for a form it does not run yet (exit 3 and its
    `unsupported:` reason); the reason is kept for outcome.not_compared.not_executed."""
    m = re.search(r"unsupported: ([^\n]*)", r.stderr.decode("utf-8", "replace"))
    if r.returncode == 3 and m:
        res["interp_reason"] = m.group(1).strip()
        return True
    return False


def run_program_entry(p: Program, tc: Toolchain, tools: dict, work: pathlib.Path, c_files, res,
                      sir_dir=None):
    """A script or `main` program of B1 (no exported entry for the harness): its output set is
    linked with the runtime into an executable whose C `main` (CINTC-OQ-36) runs the entry with
    unbounded fuel and writes the fault or error record and the fuel record to the paths it is
    given (SPEC-06 3.4a, D-19), and in format 2 and later the module state to a third path
    (cint_program_run_state; a main without module state refuses a third path with status 4 and
    is run again without it). The observed body holds the stdout bytes, the fuel consumed, the
    decoded fault record (cint_ref's IM-148 reader) or error record (exit status 6, format 3),
    and the state lines, in the case's format. The program-level C `main` is outside the EMIT-31
    families, so these programs are not symbol-audited (CINTC-OQ-36). On the interpreter leg
    `cint-interp run` reads the output set's SIR text and does what that `main` does; None when
    it declines the program."""
    fault_path, fuel_path, state_path = work / "fault.bin", work / "fuel.bin", work / "state.txt"
    res["entry"] = True
    if "interp" in tools:
        cmd = [str(tools["interp"]), "run", str(sir_dir)]
    else:
        try:
            objs = []
            for k, f in enumerate(c_files):
                (work / "obj" / str(k)).mkdir(parents=True, exist_ok=True)
                objs += tc.compile([f], work / "obj" / str(k), p.name, tools["program_defines"])
            cmd = [str(tc.link_exe(objs + tools["link"], work / "program", p.name))]
        except BuildError as e:
            if backend_refused(res, c_files, str(e)):
                return None
            return [["outcome build-failed"] + str(e).splitlines()[:20]]
    env = dict(tc.env, ASAN_OPTIONS="detect_leaks=1:abort_on_error=0",
               UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1")
    dests = [fault_path, fuel_path] + ([state_path] if p.format >= 2 else [])
    while True:
        for f in (fault_path, fuel_path, state_path):
            f.unlink(missing_ok=True)
        r = subprocess.run(cmd + [str(d) for d in dests], cwd=work, env=env,
                           capture_output=True, stdin=subprocess.DEVNULL)
        res["sanitizer"] += sanitizer_reports(r.stderr.decode("utf-8", "replace"))
        if "interp" in tools and interp_refused(res, r):
            return None
        if r.returncode != 4 or len(dests) == 2:
            break
        dests = dests[:2]
    res["executions"] = 1
    lines = entry_record(r, fault_path, fuel_path, state_path, p.format)
    return [lines + (stdout_bytes_check(p, 0, r.stdout) if lines[0].startswith("outcome ") else [])]


def entry_record(r, fault_path: pathlib.Path, fuel_path: pathlib.Path, state_path=None, fmt: int = 1) -> list:
    """The body, in format fmt, of a program entry's run (r, a completed subprocess.run with
    stdout bytes) from its stdout and the fuel, fault or error, and state records it wrote; or
    the exit status and what went wrong."""
    try:
        fuel = fuel_path.read_bytes()
        n = int.from_bytes(fuel[:4], "little")
        if fuel[4:4 + n] != b"cint-core-1/fuel-consumed/v1" or len(fuel) != 12 + n:
            raise ValueError("malformed fuel record")
        lines = ["outcome " + {0: "value", 1: "fault", 6: "error"}[r.returncode],
                 "stdout-bytes %d" % len(r.stdout)]
        lines += ["stdout-sha256 " + sha256(r.stdout)] if r.stdout else []
        lines.append("fuel-consumed %d" % int.from_bytes(fuel[4 + n:], "little", signed=True))
        if r.returncode == 1:
            lines += decode_fault_record(fault_path.read_bytes(), RUN_TIME)[1].to_expect_lines(fmt)
        elif r.returncode == 6:
            lines += decode_error_record(fault_path.read_bytes())
        if fmt >= 2 and state_path is not None:
            lines += state_lines(state_path)
    except (OSError, KeyError, ValueError) as e:
        return ["program exit %d" % r.returncode, str(e)] + r.stderr.decode(
            "utf-8", "replace").splitlines()[:5]
    return lines


def run_compiled(p: Program, tc: Toolchain, tools: dict, work: pathlib.Path, c_file, res,
                 sir_dir=None):
    """Build the program's C (the seed's one file, or B1's list of files, each in an object
    directory of its own so that equal stems cannot collide), link it with the runtime into a
    shared library, and run the harness on its cases. On the interpreter leg `cint-interp
    observe` runs them from the output set's SIR text; None when it declines the program."""
    if "interp" in tools:
        program = [str(tools["interp"]), "observe", str(sir_dir)]
    else:
        try:
            if isinstance(c_file, list):
                objs = []
                for k, f in enumerate(c_file):
                    (work / "obj" / str(k)).mkdir(parents=True, exist_ok=True)
                    objs += tc.compile([f], work / "obj" / str(k), p.name, tools["program_defines"])
            else:
                objs = tc.compile([c_file], work, p.name, tools["program_defines"])
            lib = tc.link_shared(objs + tools["link"], work / pathlib.Path(p.rel).stem, p.name)
        except BuildError as e:
            if backend_refused(res, c_file if isinstance(c_file, list) else [c_file], str(e)):
                return None
            return [["outcome build-failed"] + str(e).splitlines()[:20]] * len(p.cases)
        if not tc.sanitize:
            exports = tc.exports(lib)
            res["symbols"] = audit_program([tc.symbols(o) for o in objs], exports, tools["rt_defined"],
                                           tc.leg)
            res["exports"] = len(exports)
            res["audited"] = True
        program = [str(tools["harness"]), str(lib)]
    cases_file = work / "cases.txt"
    cases_file.write_bytes("".join(c[0] + "\n" for c in p.cases).encode("ascii"))
    stdout_file = work / "stdout.bin"
    stdout_file.unlink(missing_ok=True)
    cmd = program + ["--cases", str(cases_file), "--depth", str(p.depth),
                     "--format", str(p.format), "--stdout-file", str(stdout_file)]
    if p.fuel is not None:
        cmd += ["--fuel", str(p.fuel)]
    env = dict(tc.env)
    env["ASAN_OPTIONS"] = "detect_leaks=1:abort_on_error=0"
    env["UBSAN_OPTIONS"] = "print_stacktrace=1:halt_on_error=1"
    r = subprocess.run(cmd, cwd=work, env=env, capture_output=True)
    stderr = r.stderr.decode("utf-8", "replace")
    res["sanitizer"] += sanitizer_reports(stderr)
    if "interp" in tools and interp_refused(res, r):
        return None
    res["executions"] = len(p.cases)
    try:
        blocks = parse_harness(r.stdout, len(p.cases))
    except ValueError as e:
        blocks = [None] * len(p.cases)
        stderr = "%s\n%s" % (e, stderr)
    if r.returncode != 0:
        blocks = [b if b else ["harness exit %d" % r.returncode] + stderr.splitlines()[:5]
                  for b in blocks]
    elif len(p.cases) == 1 and blocks[0]:
        # The harness wrote the call's stdout bytes raw (--stdout-file); its stdout-bytes and
        # stdout-sha256 lines are computed from them, and the bytes are compared with cint_ref's.
        raw = stdout_file.read_bytes() if stdout_file.is_file() else None
        have = [x for x in blocks[0] if x.startswith(("stdout-bytes ", "stdout-sha256 "))]
        if have or raw is not None:
            want = ["stdout-bytes %d" % len(raw)] + (["stdout-sha256 " + sha256(raw)] if raw else [])                 if raw is not None else ["no --stdout-file output"]
            if have != want:
                blocks[0] = blocks[0] + ["--stdout-file differs from the stdout lines"]
            blocks[0] = blocks[0] + stdout_bytes_check(p, 0, raw if raw is not None else b"")
    return blocks


def stdout_bytes_check(p: Program, k: int, raw: bytes) -> list:
    """Plan task 2.14 (D-19): call k's stdout bytes against cint_ref's, byte for byte; a line
    that makes the call disagree when they differ, nothing when they agree or cint_ref was not
    run for the call."""
    ref = p.ref_stdout[k] if k < len(p.ref_stdout) else None
    return [] if ref is None or raw == ref else ["stdout bytes differ from cint_ref's (D-19)"]


def backend_refused(res: dict, c_files, error: str) -> bool:
    """Whether a gpu-cuda build failed on the BACK-05 refusal its emitted C carries (an `#error`
    under CINT_GPU_CUDA naming the kernel and the capability back_ptx refused); the refusal is
    kept for outcome.not_compared.unsupported."""
    for f in c_files:
        m = BACKEND_REFUSAL_RE.search(pathlib.Path(f).read_text("ascii", "replace"))
        if m and "E_UNSUPPORTED" in error:
            res["backend_refusal"] = {"backend": "gpu-cuda", "kernel": m.group(1),
                                      "capability": m.group(2), "limit": "BACK-05",
                                      "refusal": m.group(0)}
            return True
    return False


def ptx_headers(c_files) -> set:
    """The (.version, .target) pairs of the PTX arrays in a gpu-cuda program's emitted C."""
    found = set()
    for f in c_files:
        text = pathlib.Path(f).read_text("ascii", "replace")
        found |= set(zip(re.findall(r'"\.version ([0-9.]+)\\n"', text),
                         re.findall(r'"\.target (\w+)\\n"', text)))
    return found


def build_tools(tc: Toolchain, out: pathlib.Path, backend=None) -> dict:
    """The runtime object, the seed and the harness; with `backend` (--backend gpu-cuda: its
    block size and whether the stand-in driver runs the kernels) also the CUDA objects linked
    into every program, and the stand-in driver library, which CINT_CUDA_DRIVER names."""
    tools = {}
    rt_dir = out / "rt"
    rt_dir.mkdir(parents=True, exist_ok=True)
    tools["rt_obj"] = tc.compile([RT / "cint_rt.c"], rt_dir, "runtime")[0]
    tools["link"], tools["program_defines"] = [tools["rt_obj"]], ()
    if backend is not None:
        cuda_defines = ["CINT_CUDA_BLOCK=%du" % backend["block"]] + \
            (["CINT_CUDA_TESTING"] if backend["stand_in"] else [])
        tools["link"] += tc.compile(CUDA_SOURCES, rt_dir, "cuda runtime", cuda_defines)
        tools["program_defines"] = ("CINT_GPU_CUDA",)
        if backend["stand_in"]:
            f_dir = out / "stand-in"
            f_dir.mkdir(parents=True, exist_ok=True)
            objs = tc.compile(STAND_IN_SOURCES, f_dir, "stand-in driver", ["CINT_FAKE_PTX"])
            tools["stand_in"] = tc.link_shared(objs, f_dir / "cuda-stand-in", "stand-in driver")
            tc.env["CINT_CUDA_DRIVER"] = str(tools["stand_in"])
    # EMIT-26 flags; tested by test_bridge.py. The seed reads its modules through cint_bridge.c.
    bridge_obj = tc.compile([RT / "cint_bridge.c", RT / "cint_build.c"], rt_dir, "bridge")[0]
    seed_dir = out / "seed"
    seed_dir.mkdir(parents=True, exist_ok=True)
    seed_objs = tc.compile(sorted(SEED.glob("*.c")), seed_dir, "seed")
    tools["seed"] = tc.link_exe(seed_objs + [tools["rt_obj"], bridge_obj], seed_dir / "cint-seed", "seed")
    h_dir = out / "harness"
    h_dir.mkdir(parents=True, exist_ok=True)
    h_objs = tc.compile([HARNESS / "cint_harness.c"], h_dir, "harness")
    tools["harness"] = tc.link_exe(h_objs + [tools["rt_obj"]], h_dir / "cint-harness", "harness")
    return tools


def cuda_device(tc: Toolchain, out: pathlib.Path) -> dict:
    """The device of a --backend gpu-cuda run, for observations (RCPT-09): the facts
    tools/cint_cuda_probe.c records for ordinal 0 (its other lines stay in <out>/probe.txt),
    and the display driver's version that nvidia-smi reports, when it is installed; or
    {"error": ...} when the driver cannot be loaded."""
    p_dir = out / "probe"
    p_dir.mkdir(parents=True, exist_ok=True)
    objs = tc.compile([ROOT / "tools" / "cint_cuda_probe.c"], p_dir, "cuda probe")
    exe = tc.link_exe(objs, p_dir / "cint-cuda-probe", "cuda probe")
    r = subprocess.run([str(exe), str(p_dir / "probe.txt"), "cint-check"], cwd=p_dir,
                       env=tc.env, capture_output=True, text=True, errors="replace")
    facts = dict(line.split(" ", 1) for line in r.stdout.splitlines() if " " in line)
    if r.returncode == 3 or "device.0.name" not in facts:
        return {"error": "cint-cuda-probe exit %d: %s" % (r.returncode, (r.stdout + r.stderr)[-400:])}
    smi = shutil.which("nvidia-smi")
    version = None
    if smi:
        q = subprocess.run([smi, "--query-gpu=driver_version", "--format=csv,noheader"],
                           capture_output=True, text=True, errors="replace")
        version = q.stdout.strip().splitlines()[0] if q.returncode == 0 and q.stdout.strip() else None
    return {"name": facts["device.0.name"],
            "compute_capability": "%s.%s" % (facts.get("device.0.compute_capability_major"),
                                             facts.get("device.0.compute_capability_minor")),
            "driver_api_version": facts.get("driver.api_version"),
            "driver_library": facts.get("driver.library"),
            "driver_version": version,
            "kernel_exec_timeout": facts.get("device.0.kernel_exec_timeout"),
            "probe_exit": r.returncode}


def build_interp(tc: Toolchain, tools: dict, out: pathlib.Path) -> pathlib.Path:
    """cint-interp (interp/, with the runtime object) for the interpreter leg."""
    i_dir = out / "interp"
    i_dir.mkdir(parents=True, exist_ok=True)
    objs = tc.compile([INTERP / f for f in INTERP_SOURCES] + [RT / "cint_state.c"], i_dir, "interp")
    return tc.link_exe(objs + [tools["rt_obj"]], i_dir / "cint-interp", "interp")


def verify_tables(regen: pathlib.Path) -> dict:
    """Regenerate the generated conformance files with this host's Python; compare with MANIFEST."""
    shutil.rmtree(regen, ignore_errors=True)
    tools_dir = CONF / "tools"
    exh8 = regen / "integer-machine" / "exh8"
    bnd64 = regen / "integer-machine" / "bnd64"
    py = sys.executable
    subprocess.run([py, str(tools_dir / "gen_exhaustive8.py"), "--batch", "all", "--out", str(exh8)],
                   check=True, capture_output=True)
    subprocess.run([py, str(tools_dir / "gen_boundary64.py"), "--batch", "all", "--out", str(bnd64)],
                   check=True, capture_output=True)
    cifs = sorted(str(p) for p in exh8.glob("*.cif1.jsonl")) + \
        sorted(str(p) for p in bnd64.glob("*.cif1.jsonl"))
    subprocess.run([py, str(tools_dir / "gen_tables.py"), *cifs, "--out", str(regen / "tables"),
                    "--root", str(regen)], check=True, capture_output=True)
    match, differ = 0, []
    for line in (CONF / "integer-machine" / "MANIFEST.txt").read_text(encoding="ascii").splitlines():
        if not line or line.startswith("#"):
            continue
        digest, _size, rel = line.split("  ")
        path = regen / rel
        if path.is_file() and sha256(path.read_bytes()) == digest:
            match += 1
        else:
            differ.append(rel)
    produced = sum(1 for p in regen.rglob("*") if p.is_file())
    return {"differ": differ, "files": match + len(differ), "match": match, "produced": produced,
            "python": platform.python_version()}


# --------------------------------------------------------------------------- main

def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--leg", choices=["msvc", "gcc", "clang", "apple-clang"], required=True)
    ap.add_argument("--opt", choices=["0", "2"], required=True)
    ap.add_argument("--helpers", choices=["portable", "builtin"], required=True)
    ap.add_argument("--quick", action="store_true", help="anchors and program cases only")
    ap.add_argument("--sanitize", action="store_true", help="ASan and UBSan (gcc and clang)")
    ap.add_argument("--only", action="append", default=[], help="programs whose name contains TEXT")
    ap.add_argument("--verify-tables", action="store_true",
                    help="regenerate the tables with this host's Python and compare with MANIFEST")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--out", help="build directory (default <build>/check/<tag>, <build> from CINT_BUILD)")
    ap.add_argument("--receipt",
                    help="receipt path (default results/cint/slice2/t1/receipt-<tag>.json)")
    ap.add_argument("--no-receipt", action="store_true")
    ap.add_argument("--compiler", choices=sorted(COMPILERS), default="seed",
                    help="the compiler under test: the seed, or B1 through `cint emit-c`")
    ap.add_argument("--cint", metavar="PATH",
                    help="with --compiler b1: the B1 cint executable, beside the cint.toolchain "
                         "tools/cint_bootstrap.py wrote (default: bootstrap one into the build "
                         "directory)")
    ap.add_argument("--emit-cache", metavar="DIR",
                    help="with --compiler b1: keep B1's compilations in DIR, outside the "
                         "repository, for the other configurations of the leg to reuse")
    ap.add_argument("--required", metavar="FILE",
                    help="the stage's required case list (CONF-15); the run fails when a case's "
                         "category differs from it")
    ap.add_argument("--write-required", metavar="FILE",
                    help="write this run's categories as a required case list, for review")
    ap.add_argument("--interp", action="store_true",
                    help="with --compiler b1: run each program on cpu-sir-interp from its SIR "
                         "text in place of its compiled C (the interpreter leg)")
    ap.add_argument("--set", choices=sorted(SETS),
                    help="run a conformance set against its own --required list: its cases as "
                         "--only selects them, and the receipt records the set, not the stage")
    ap.add_argument("--backend", choices=["cpu-c17", "gpu-cuda"], default="cpu-c17",
                    help="with --compiler b1: gpu-cuda runs each kernel on the CUDA device "
                         "(SPEC-09 CONF-17, RCPT-09)")
    ap.add_argument("--block", type=int, choices=[32, 256],
                    help="with --backend gpu-cuda: the work-items per block, the schedule "
                         "(SPEC-02 Q-3; default 256)")
    ap.add_argument("--stand-in", action="store_true",
                    help="with --backend gpu-cuda: run the kernels' PTX on the stand-in driver "
                         "of rt/tests, on the host, in place of the CUDA driver")
    raw = list(argv if argv is not None else sys.argv[1:])
    ns = ap.parse_args(raw)
    for flag, value in (("--out", ns.out), ("--emit-cache", ns.emit_cache)):
        if value and pathlib.Path(value).resolve().is_relative_to(ROOT.resolve()):
            ap.error("%s must be outside the repository: %s" % (flag, value))
    if ns.emit_cache and ns.compiler != "b1":
        ap.error("--emit-cache is for --compiler b1: the seed is built per configuration")
    if ns.interp and ns.compiler != "b1":
        ap.error("--interp is for --compiler b1: the interpreter reads B1's SIR text")
    gpu = ns.backend == "gpu-cuda"
    if gpu and (ns.compiler != "b1" or ns.interp):
        ap.error("--backend gpu-cuda is for --compiler b1 without --interp: B1 writes the PTX")
    if not gpu and (ns.block or ns.stand_in):
        ap.error("--block and --stand-in are for --backend gpu-cuda")
    block = ns.block or 256
    if ns.set:
        if ns.only or not ns.required:
            ap.error("--set selects its own cases and takes --required, without --only")
        if not ns.receipt and not ns.no_receipt:
            ap.error("--set takes --receipt or --no-receipt: the default path is a stage's")
        ns.only = list(SETS[ns.set])
    if ns.leg == "apple-clang" and sys.platform != "darwin":
        ap.error("the apple-clang leg runs on macOS")
    if ns.leg in ("gcc", "clang") and os.name == "nt":
        fwd, i = [], 0
        paths = ("--out", "--receipt", "--required", "--write-required", "--cint", "--emit-cache")
        while i < len(raw):   # Windows paths as WSL paths
            a = raw[i]
            if a in paths and i + 1 < len(raw):
                fwd += [a, wsl_path(pathlib.Path(raw[i + 1]).resolve())]
                i += 2
                continue
            for flag in (p + "=" for p in paths):
                if a.startswith(flag):
                    a = flag + wsl_path(pathlib.Path(a[len(flag):]).resolve())
            fwd.append(a)
            i += 1
        cmd = ["wsl", "-d", WSL_DISTRO, "--cd", wsl_path(ROOT), "--", "python3",
               "tools/cint_check.py", *fwd]
        return subprocess.run(cmd, env=wsl_env()).returncode
    if ns.leg == "msvc" and os.name != "nt":
        ap.error("the msvc leg runs on Windows")
    if ns.sanitize and ns.leg in ("msvc", "apple-clang"):
        ap.error("--sanitize is for the gcc and clang legs")
    tag = "%s-%s-%s%s%s%s%s" % (ns.leg, ns.opt, ns.helpers, "-san" if ns.sanitize else "",
                                "-quick" if ns.quick else "", "-interp" if ns.interp else "",
                                ("-gpu-cuda-b%d%s" % (block, "-stand-in" if ns.stand_in else ""))
                                if gpu else "")
    base = default_base()
    out = pathlib.Path(ns.out) if ns.out else base / tag
    started = time.monotonic_ns()
    if not ns.quick:
        try:
            avail = available_memory()
        except (OSError, ValueError, subprocess.CalledProcessError) as e:
            print("BLOCKED: available memory could not be measured: %s" % e)
            return 3
        if avail < MIN_AVAILABLE_BYTES:
            print("BLOCKED: %d MiB available; the heavy-run gate needs 8 GiB" % (avail >> 20))
            return 3
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    gen_root = out / "gen"
    alt = out / "alt-tree"

    compiler = COMPILERS[ns.compiler]
    try:
        lists = {"held": gen_expect.read_held(), "compiler": compiler,
                 "unsupported": read_unsupported(CONF / "unsupported.txt")}
        required = read_required(pathlib.Path(ns.required)) if ns.required else None
        exclusions = read_exclusions(pathlib.Path(ns.required)) if ns.required else {}
    except (OSError, ValueError) as e:
        print("BLOCKED: %s" % e)
        return 3
    cats, ref_problems = {}, []   # cats: {case: (category, detail)} of the cases not compiled
    t0 = time.monotonic_ns()
    programs = inventory_program_cases(ns.only, cats, ref_problems, lists)
    programs += inventory_anchors(gen_root, ns.only, cats, lists)
    if not ns.quick:
        programs += inventory_tables(ns.only, cats, lists)
    ref_ms = (time.monotonic_ns() - t0) // 1_000_000
    records = sum(len(p.cases) for p in programs)
    print("inventory: %d programs, %d records, %d cases classified without compiling; cint_ref "
          "against the frozen .expect files: %d differ (%d ms)"
          % (len(programs), records, len(cats), len(ref_problems), ref_ms), flush=True)
    for line in ref_problems:
        print("  " + line)

    tables = None
    if ns.verify_tables:
        # Under the run's own directory: runs in parallel on one host do not share it.
        tables = verify_tables(out / "tables-regen")
        print("tables regenerated with Python %(python)s: %(match)d of %(files)d MANIFEST files "
              "identical" % tables, flush=True)

    try:
        tc = Toolchain(ns.leg, ns.opt, ns.helpers, ns.sanitize)
        tools = build_tools(tc, out, {"block": block, "stand_in": ns.stand_in} if gpu else None)
        device = cuda_device(tc, out) if gpu and not ns.stand_in else None
        if ns.compiler == "b1":
            tools["b1"] = b1_tool(ns.leg, ns.cint, out / "b1-boot")
            if ns.emit_cache:
                tools["b1"]["cache"] = EmitCache(pathlib.Path(ns.emit_cache), tools["b1"])
            print("b1: %s %s, compiler source identity %s" % (
                tools["b1"]["how"], tools["b1"]["cint"].name, tools["b1"]["identity"]), flush=True)
        if ns.interp:
            tools["interp"] = build_interp(tc, tools, out)
    except (BuildError, OSError, subprocess.CalledProcessError) as e:
        print("BLOCKED: build failed: %s" % e)
        return 3
    if gpu and not ns.stand_in and "error" in device:
        print("BLOCKED: no CUDA device: %s" % device["error"])
        return 3
    rt_bad, rt_defined, tools["rt_defined"] = [], 0, set()
    if not ns.sanitize:   # the runtime object, and with --backend gpu-cuda the CUDA objects
        defined = set().union(*(tc.symbols(o)[0] for o in tools["link"]))
        rt_defined, tools["rt_defined"] = len(defined), defined
        rt_bad = sorted("rt defines " + s for s in defined if not s.startswith("cint_"))
        print("runtime symbol audit: %d defined external symbols, %d outside cint_%s"
              % (len(defined), len(rt_bad), (": " + ", ".join(rt_bad)) if rt_bad else ""),
              flush=True)

    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, ns.jobs)) as pool:
        for res in pool.map(lambda p: process(p, tc, tools, out, alt, lists), programs):
            results.append(res)
            if res["disagreements"] or res["sanitizer"] or res["symbols"]:
                print("%s: %d/%d agree; sanitizer reports %d; symbols %s%s" % (
                    res["name"], res["agree"], res["compared"], res["sanitizer"],
                    res["symbols"], "; first: " + json.dumps(res["disagreements"][0])[:400]
                    if res["disagreements"] else ""), flush=True)
            if res["signature_disagreements"]:
                print("%s: %d of %d type signatures disagree; first: %s" % (
                    res["name"], len(res["signature_disagreements"]), res["signatures"],
                    json.dumps(res["signature_disagreements"][0])[:400]), flush=True)
    early = sorted((c for c, (cat, _) in cats.items() if cat == "disagreement"),
                   key=lambda c: c.encode("utf-8"))   # classified as disagreements, not compiled
    for res in results:
        for label in (res["labels"] if res["kind"] == "anchor" else [res["name"]]):
            cats[label] = res["category"]
    ran = [r for r in results if r["category"][0] in ("compared", "disagreement")]
    compared = sum(r["compared"] for r in ran) + len(early)
    agree = sum(r["agree"] for r in ran)
    first = next((d for r in ran for d in r["disagreements"]), None) or \
        ({"case": early[0], "reason": cats[early[0]][1]["reason"]} if early else None)
    not_compared = {k: [] for k in ("held", "not_applicable", "outside_subset", "unsupported")
                    + (("not_executed",) if ns.interp else ())}
    for case, (cat, detail) in sorted(cats.items(), key=lambda kv: kv[0].encode("utf-8")):
        if cat in not_compared:
            not_compared[cat].append(dict(detail, case=case))
    problems = None
    if required is not None:
        problems = check_required(required, cats, compiler, required_scope(ns.only, ns.quick),
                                  exclusions)
        for line in problems[:40]:
            print("required: " + line)
        n_ex = sorted(c for c in cats if excluded(required, exclusions, c, compiler))
        print("required: %d problems; %d cases excluded by the list's boot scope (%s)" % (
            len(problems), len(n_ex), ", ".join("%s %d" % (k, sum(1 for c in n_ex if cats[c][0] == k))
                                               for k in CATEGORIES + ("disagreement",)
                                               if any(cats[c][0] == k for c in n_ex)) or "none"))
    if ns.write_required:
        try:
            write_required(pathlib.Path(ns.write_required), cats, compiler,
                           ns.set or ("T2" if ns.compiler == "b1" else "T1"))
        except ValueError as e:
            print("required list not written: %s" % e)
    sanitizer = sum(r["sanitizer"] for r in ran)
    nondeterministic = sorted(r["name"] for r in ran if r["determinism"] is False)
    sym_bad = sorted({"%s %s" % (r["name"], s) for r in ran for s in r["symbols"]} | set(rt_bad))
    emitted = sorted("%s  %s.c\n" % (r["emitted_sha256"], r["name"]) for r in ran
                     if r["emitted_sha256"])
    by_kind = collections.Counter(r["kind"] for r in ran)
    # CONF-16: B1's reflection tables against cint_ref, export by export; a disagreement fails
    # the run as an outcome disagreement does (CONF-04).
    signatures = sum(r["signatures"] for r in ran)
    sig_bad = [dict(d, program=r["name"]) for r in ran for d in r["signature_disagreements"]]
    passed = (compared == agree and not ref_problems and sanitizer == 0 and not nondeterministic
              and not sym_bad and (tables is None or not tables["differ"]) and not problems
              and not sig_bad)
    elapsed_ms = (time.monotonic_ns() - started) // 1_000_000
    suite, suite_files = tree_digest("conformance/", "conformance/")
    ref_tree, ref_files = tree_digest("ref/cint_ref/", "ref/")
    identity = {
        "cint_ref_tree_files": ref_files,
        "cint_ref_tree_method": TREE_METHOD % ("ref/cint_ref/", "ref/"),
        "cint_ref_tree_sha256": ref_tree,
        "emitted_c_count": len(emitted),
        "emitted_c_method": "sha256 over the lines '<sha256>  <program>.c\\n', sorted by bytes",
        "emitted_c_tree_sha256": sha256("".join(emitted).encode("ascii")),
        "bridge_sha256": files_digest([RT / "cint_bridge.c", RT / "cint_bridge.h",
                                       RT / "cint_bridge_internal.h", RT / "cint_build.c"]),
        "harness_sha256": files_digest([HARNESS / "cint_harness.c"]),
        "profile": "cuda" if gpu else "cint-core-1",
        "compiler": compiler,
        **({"cli_sha256": files_digest(sorted((ROOT / "cli").glob("*.c")) +
                                       sorted((ROOT / "cli").glob("*.h"))),
            "compiler_source_identity": tools["b1"]["identity"],
            "compiler_source_method": "sha256 of the CISRC001 source manifest of the import "
                                      "closure of compiler/main.ci (tools/cint_bootstrap.py)",
            "emitter": "cint emit-c"} if ns.compiler == "b1" else {}),
        **({"executor": "cpu-sir-interp",
            "interp_sha256": files_digest([INTERP / f for f in INTERP_SOURCES] + sorted(INTERP.glob("*.h")) +
                                          [RT / "cint_state.c", RT / "cint_state.h"])} if ns.interp else {}),
        **({"backend": "gpu-cuda",
            "cuda_sha256": files_digest([RT / "cint_cuda.c", RT / "cint_cuda.h",
                                         RT / "cint_cuda_dispatch.c"]),
            **({"cuda_driver": "stand-in",
                "stand_in_sha256": files_digest(list(STAND_IN_SOURCES) +
                                                [RT / "tests" / "ptx_interp.h"])}
               if ns.stand_in else {})} if gpu else {}),
        "encodings": encodings(compiler),
        "runtime_contract_version": runtime_contract(),
        "runtime_header_sha256": sha256((RT / "cint_rt.h").read_bytes()),
        "runtime_library_sha256": files_digest([RT / "cint_rt.c", RT / "cint_rt_internal.h",
                                                 RT / "cint_mem.c", RT / "cint_mem.h"]),
        "schema": "cint-conformance-receipt-2",
        "scope": "quick" if ns.quick else "full",
        "seed_sha256": files_digest(sorted(SEED.glob("*.c")) + sorted(SEED.glob("*.h"))),
        **({"set": ns.set} if ns.set else {"stage": "T2" if ns.compiler == "b1" else "T1"}),
        "subset": "cint-boot-1",
        "suite_digest_method": TREE_METHOD % ("conformance/", "conformance/"),
        "suite_files": suite_files,
        "suite_sha256": suite,
        "tool_sha256": files_digest([pathlib.Path(__file__).resolve()]),
    }
    outcome = {
        "agreement": {"agree": agree, "compared": compared, "disagree": compared - agree,
                      "first_disagreement": json.dumps(first, sort_keys=True) if first else None,
                      "compile_errors": sum(r["compile_errors"] for r in ran),
                      "programs_compiled": len(emitted), "programs_run": len(ran),
                      "programs_by_kind": dict(sorted(by_kind.items())),
                      "cint_ref_check_records": sum(r["ref_checked"] for r in ran)},
        "cint_ref_against_frozen": {"differ": len(ref_problems)},
        "conformance": {"cases": records, "failed": compared - agree + len(ref_problems),
                        "suite_sha256": suite},
        "determinism": {"differ": nondeterministic,
                        "programs": sum(1 for r in ran if r["determinism"] is not None)},
        "not_compared": not_compared,
        **({"device_programs": sum(1 for r in ran if r.get("ptx"))} if gpu else {}),
        "pass": passed,
        # RCPT-03: one value on every leg; what was audited is in observations.symbol_audit.
        "symbol_audit": {"pass": not sym_bad},
        "builds": {"bridge": "compiled", "harness": "linked", "runtime": "compiled",
                   "seed": "linked", **({"interp": "linked"} if ns.interp else {}),
                   **({"cuda": "compiled"} if gpu else {}),
                   **({"stand_in": "linked"} if ns.stand_in else {})},
    }
    if tables is not None:
        outcome["tables_regenerated"] = {k: tables[k] for k in ("differ", "files", "match")}
    if ns.compiler == "b1":
        outcome["type_signatures"] = {
            "compared": signatures, "disagree": len(sig_bad),
            "first_disagreement": json.dumps(sig_bad[0], sort_keys=True) if sig_bad else None,
            "programs": sum(1 for r in ran if r["emitted_sha256"])}
    if required is not None:
        req = pathlib.Path(ns.required).resolve()
        outcome["required"] = {
            "file": req.relative_to(ROOT).as_posix() if req.is_relative_to(ROOT) else req.name,
            "pass": not problems, "problems": problems[:50], "problem_count": len(problems),
            "sha256": sha256(req.read_bytes())}
    observations = {
        "c0": tc.describe(),
        "date_utc": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d"),
        "elapsed_s": elapsed_ms // 1000,
        "executions": sum(r["executions"] for r in ran),
        "helpers": ns.helpers,
        "host": {"arch": platform.machine().lower().replace("amd64", "x86-64").replace(
            "x86_64", "x86-64"), "os": host_os()},
        "leg": ns.leg,
        "opt": int(ns.opt),
        "python": platform.python_version(),
        "rt_defined_symbols": None if ns.sanitize else rt_defined,
        "sanitize": ns.sanitize,
        "sanitizer_reports": sanitizer,
        "compile_retries": tc.retries,
        **({"b1": {"cint_sha256": tools["b1"]["sha256"], "how": tools["b1"]["how"],
                   **({"emit_cache": dict(tools["b1"]["cache"].counts)} if ns.emit_cache else {})}}
           if ns.compiler == "b1" else {}),
        "wall_ms": {"reference_inventory": ref_ms, "total": elapsed_ms},
        **({"gpu": {"block": block,
                    "device": device if device is not None else "stand-in (rt/tests/cuda_fake.c, "
                              "CINT_FAKE_PTX; item order %s)" % (
                                  "ascending" if os.environ.get("CINT_FAKE_ORDER") == "asc"
                                  else "descending"),
                    "ptx": sorted({" ".join(v) for r in ran for v in r.get("ptx", [])})}}
           if gpu else {}),
    }
    if ns.sanitize:   # instrumented objects define sanitizer symbols: exempt (EMIT-20)
        observations["symbol_audit"] = {"exempt": "EMIT-20"}
    else:             # toolchain-dependent counts (RCPT-03): observations, not outcome
        observations["symbol_audit"] = {
            "method": "defined externals and undefined references of the runtime object and of "
                      "each program object, and the export table of each program library (EMIT-31 "
                      "and SPEC-03 A-3 as amended by slice 2 patch D-2: a program defines cx_, cm_, "
                      "ci_, cg_, cint_program_abi and cint_observer_desc; refers to the runtime's "
                      "cint_ functions, the support list (on apple-clang also __chkstk_darwin and "
                      "bzero, on msvc also __report_rangecheckfailure and __isa_available), and "
                      "ci_ and cg_ of the same program; "
                      "exports cx_, cm_, the two observer symbols and the linked runtime's own "
                      "definitions); toolchain names excluded: MSVC pooled string literals ??_C@ "
                      "and pooled vector constants __xmm@, __ymm@, __zmm@ (a pooled "
                      "floating-point constant __real@ stays a finding), MSVC's "
                      "__isa_available_default, "
                      "and the ELF linker's _init, _fini, __bss_start, _edata, _end; Mach-O names "
                      "lose the one leading underscore of the Mach-O C name (nm ___chkstk_darwin "
                      "is __chkstk_darwin); program_objects counts the program libraries "
                      "audited, and entry_programs names the emitted programs with a script or "
                      "void main() entry, linked as executables with a C main and not audited "
                      "(CINTC-OQ-36)",
            "outside_namespace": sym_bad,
            **audit_coverage(ran)}
    if tables is not None:
        observations["tables_regenerated"] = {"produced": tables["produced"],
                                              "python": tables["python"]}
    observations["receipt_identity_sha256"] = sha256(canonical({"identity": identity,
                                                                "outcome": outcome}))
    receipt = {"identity": identity, "observations": observations, "outcome": outcome}
    print("%s: %d programs compiled, %d records compared, %d disagree, %d sanitizer reports, "
          "%d nondeterministic, %d symbol audit findings, %s%d s; suite %s"
          % (tag, len(emitted), compared, compared - agree, sanitizer, len(nondeterministic),
             len(sym_bad), "%d type signatures compared, %d type signature disagreements, " % (
                 signatures, len(sig_bad)) if ns.compiler == "b1" else "",
             elapsed_ms // 1000, suite))
    print("not compared: %s; disagreements before compiling: %d" % (", ".join(
        "%s %d" % (k, len(v)) for k, v in sorted(not_compared.items())), len(early)))
    if required is not None:
        print("required list %s: %s" % (ns.required, "%d problems" % len(problems) if problems
                                        else "every category as listed"))
    if first:
        print("first disagreement: " + json.dumps(first))
    if sig_bad:
        print("first type signature disagreement: " + json.dumps(sig_bad[0]))
    # CONF-14 per compiler: records compared by running, unsupported cases (each with its limit
    # and reason in conformance/unsupported.txt), records compared as an expected compile error.
    refused = sum(r["compared"] for r in ran if r["compile_errors"])
    print("%s: %d compared, %d unsupported, %d refused, %d disagree" % (
        ns.compiler, compared - refused, len(not_compared["unsupported"]), refused,
        compared - agree))
    if gpu:
        print("gpu-cuda: block %d, %s; %d programs ran device code; %d kernels refused (BACK-05)" % (
            block, "stand-in driver" if ns.stand_in else device["name"],
            outcome["device_programs"], sum(1 for d in not_compared["unsupported"]
                                            if d.get("backend") == "gpu-cuda")))
    if not ns.no_receipt:
        path = pathlib.Path(ns.receipt) if ns.receipt else \
            ROOT / "results" / "cint" / "box11" / ("receipt-%s.json" % tag) if gpu else \
            ROOT / "results" / "cint" / "slice2" / ("b1" if ns.compiler == "b1" else "t1") / \
            ("receipt-%s.json" % tag)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(canonical(receipt) + b"\n")
        try:
            print("receipt: " + path.resolve().relative_to(ROOT).as_posix())
        except ValueError:
            print("receipt written")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
