"""Tests for the C17 seed compiler `cint-seed` (plan Task 1.5) on one leg.

Usage (from the repository root):
    python seed/tests/test_seed.py [--leg msvc|gcc|clang] [--out DIR] [--full]

The test builds the seed (seed/*.c with rt/cint_rt.c, which the seed uses for
constant evaluation) with the strict warning flags of the leg, then compiles
every conformance program inside cint-boot-1 that has an .expect file. A
compile-error case must give exit 2 and the expected code and position (and,
for C6001, the fault fields). Every other case is built against rt/cint_rt.c
with a small driver written here (the harness of Task 1.7 is built in
parallel), run, and compared line by line with its .expect file from the
`outcome` line on. Build outputs go under <build>/seed/<leg>/, never inside
the repository, where <build> is the CINT_BUILD environment variable or
cint-build in the system temporary directory.

Legs: `msvc` on Windows (MSVC 2022 through vcvars64.bat); `gcc` and `clang`
in WSL Ubuntu 24.04, where the seed and every program are also built with
AddressSanitizer and UndefinedBehaviorSanitizer. `--full` also runs the
exhaustive 8-bit table programs (1,441,792 calls); by default only the I64
boundary tables run (11 x 256 calls).

Standard library only (unittest).
"""
from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SEED = ROOT / "seed"
RT = ROOT / "rt"
CONF = ROOT / "conformance"

VCVARS = pathlib.Path(r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
                      r"\VC\Auxiliary\Build\vcvars64.bat")
VS_INSTALLER = r"C:\Program Files (x86)\Microsoft Visual Studio\Installer"
GNU_WARNINGS = ["-std=c17", "-Wall", "-Wextra", "-Wconversion", "-Wsign-conversion",
                "-Wshadow", "-Wpedantic", "-Werror"]
CLANG_EXTRA = ["-Wimplicit-int-conversion"]
SANITIZE = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
MIN_AVAILABLE_BYTES = 8 * 1024 ** 3
SIZE_CEILING = 8000
BOOT_REFUSAL = "C9100"   # SPEC-04 LS-313: the code of a construct outside cint-boot-1
UNSUPPORTED_CODE = "C9102"   # SPEC-04 LS-313: inside cint-boot-1, not supported by the seed (SEED-16)

ARGS = argparse.Namespace(leg=None, out=None, full=False)
STATE: dict = {}

DIAG_RE = re.compile(r"^(?P<path>[^:\s]+):(?P<line>\d+):(?P<col>\d+): error (?P<code>C\d{4}): (?P<text>.*)$")


# -- environment ---------------------------------------------------------------------------
def default_out(leg: str) -> pathlib.Path:
    build = os.environ.get("CINT_BUILD") or os.path.join(tempfile.gettempdir(), "cint-build")
    return pathlib.Path(build) / "seed" / leg


def available_memory() -> int:
    if os.name == "nt":
        import ctypes

        class MemoryStatus(ctypes.Structure):
            _fields_ = [("length", ctypes.c_uint32), ("load", ctypes.c_uint32)] + \
                       [(n, ctypes.c_uint64) for n in ("tp", "ap", "tf", "af", "tv", "av", "ae")]
        s = MemoryStatus()
        s.length = ctypes.sizeof(MemoryStatus)
        if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(s)):
            raise OSError("GlobalMemoryStatusEx failed")
        return int(s.ap)
    with open("/proc/meminfo", encoding="ascii") as f:
        for line in f:
            if line.startswith("MemAvailable:"):
                return int(line.split()[1]) * 1024
    raise OSError("MemAvailable not found")


def msvc_environment() -> dict:
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
    result["SEED_CL"] = cl
    return result


def c_compile(sources, output: pathlib.Path, objects=(), obj_only=False, defines=()):
    """Compile with the leg's strict flags; returns (ok, diagnostics text)."""
    leg, env = STATE["leg"], STATE["env"]
    output.parent.mkdir(parents=True, exist_ok=True)
    if leg == "msvc":
        cmd = [env["SEED_CL"], "/nologo", "/std:c17", "/W4", "/WX", "/O2", f"/I{RT}",
               *[f"/D{d}" for d in defines], *map(str, sources), *map(str, objects)]
        cmd += ["/c", f"/Fo{output}"] if obj_only else [f"/Fe{output}", f"/Fo{output.parent}{os.sep}"]
    else:
        cmd = [leg, *GNU_WARNINGS, *(CLANG_EXTRA if leg == "clang" else []), "-O2", *SANITIZE,
               f"-I{RT}", *[f"-D{d}" for d in defines], *map(str, sources), *map(str, objects)]
        cmd += ["-c", "-o", str(output)] if obj_only else ["-o", str(output)]
    r = subprocess.run(cmd, cwd=output.parent, env=env, capture_output=True, text=True)
    text = (r.stdout + r.stderr).strip()
    if leg == "msvc":   # cl echoes the source file names
        names = {pathlib.Path(s).name for s in sources}
        text = "\n".join(l for l in text.splitlines()
                         if l.strip() not in names and l.strip() not in ("Generating Code...", "Compiling...")
                         and not l.strip().startswith("Creating library "))   # dllexport in an executable
    return r.returncode == 0 and text == "", text


def run_seed(args, cwd, stdin=None):
    env = dict(STATE["env"])
    env["ASAN_OPTIONS"] = "detect_leaks=1"
    r = subprocess.run([str(STATE["seed"]), *map(str, args)], cwd=cwd, env=env,
                       capture_output=True, input=stdin)
    return r.returncode, r.stdout.decode("ascii", "replace"), r.stderr.decode("ascii", "replace")


# -- cases -----------------------------------------------------------------------------------
def header(path: pathlib.Path) -> dict:
    """The `// key: value` header fields of a conformance case."""
    h = {}
    for line in path.read_bytes().decode("utf-8", "replace").splitlines():
        if not line.lstrip("\ufeff").startswith("//"):
            break
        m = re.match(r"^\ufeff?// ([a-z ]+): ?(.*)$", line)
        if m:
            h.setdefault(m.group(1), m.group(2))
    return h


def call_spec(h: dict):
    """(entries, typed args, fuel) from the `entry` header line."""
    parts = [p.strip() for p in h.get("entry", "run; args: none").split(";")]
    entries = [e.strip() for e in parts[0].split(",")]
    args, fuel = [], -1
    for p in parts[1:]:
        if p.startswith("args:"):
            toks = p[5:].split()
            if toks != ["none"]:
                args = list(zip(toks[0::2], toks[1::2]))
        elif p.startswith("fuel "):
            fuel = int(p[5:])
    return entries, args, fuel


def boot_cases():
    """Every conformance program inside cint-boot-1 with at least one .expect file."""
    out = []
    for ci in sorted(CONF.glob("*/*.ci")):
        if ci.parent.name == "tables":
            continue
        h = header(ci)
        if not h.get("subset", "").startswith("cint-boot-1"):
            continue
        expects = sorted(ci.parent.glob(ci.stem + ".expect")) + sorted(ci.parent.glob(ci.stem + ".*.expect"))
        if expects:
            out.append((ci, expects))
    return out


def unsupported(compiler="cint-seed") -> dict:
    """conformance/unsupported.txt (SPEC-09 CONF-14): {case: (code, limit id)} for one compiler."""
    out = {}
    for line in (CONF / "unsupported.txt").read_text(encoding="ascii").splitlines():
        if line and not line.startswith("#"):
            case, comp, code, limit = line.split(" ", 4)[:4]
            if comp == compiler:
                out[case] = (code, limit)
    return out


def expect_lines(path: pathlib.Path):
    lines = path.read_text(encoding="ascii").splitlines()
    start = next(i for i, line in enumerate(lines) if line.startswith("outcome "))
    return lines[start:]


def diag_lines(stderr: str):
    first = stderr.splitlines()[0] if stderr else ""
    m = DIAG_RE.match(first)
    if not m:
        return None, first
    lines = ["outcome compile-error", "diagnostic.code " + m.group("code"),
             "diagnostic.position %s:%s:%s" % (m.group("path"), m.group("line"), m.group("col"))]
    for item in m.group("text").split("; "):
        if item.startswith("fault."):
            lines.append("diagnostic." + item)
    return lines, first


# -- driver ------------------------------------------------------------------------------------
# The driver calls the root module's entries through the observer interface of SPEC-09
# CONF-13 (`cint_observer_desc`), with a fresh context per call and the depth limit 256.
DRIVER = r"""
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cint_rt.h"
extern const cint_observer cint_observer_desc;
static const char *const code_names[] = {"none", "E_OVERFLOW", "E_DIV_ZERO", "E_BOUNDS", "E_SHAPE",
    "E_SHIFT", "E_NARROW", "E_ALIAS", "E_STALE_HANDLE", "E_FUEL", "E_UNSUPPORTED", "E_DOMAIN",
    "E_DEPTH", "E_ASSERT"};
static cint_fault_record rec;
static void tv(const char *key, const cint_tvalue *v, int typed)
{
    char buf[1024];
    size_t n = typed ? cint_tvalue_render(v, buf, sizeof buf) : cint_tvalue_render_decimal(v, buf, sizeof buf);
    printf("%s %s\n", key, n > 0u ? buf : "?");
}
static void fault_lines(cint_ctx *ctx, int full)
{
    cint_position pos;
    uint32_t i;
    if (cint_ctx_fault(ctx, &rec) != CINT_OK || rec.code == 0u || rec.code > 13u) {
        printf("fault.code ?\n");
        return;
    }
    printf("fault.code %s\n", code_names[rec.code]);
    printf("fault.operation %.*s\n", (int)rec.operation_len, rec.operation);
    if (!full) {
        if (rec.has_exact) { tv("fault.exact", &rec.exact, 0); } else { printf("fault.exact none\n"); }
        if (rec.has_limit) { tv("fault.limit", &rec.limit, 0); } else { printf("fault.limit none\n"); }
        return;
    }
    for (i = 0; i < rec.operand_count; i++) {
        tv("fault.operand", &rec.operands[i], 1);
    }
    if (rec.has_exact) { tv("fault.exact", &rec.exact, 0); } else { printf("fault.exact none\n"); }
    if (rec.has_limit) { tv("fault.limit", &rec.limit, 1); } else { printf("fault.limit none\n"); }
    if (cint_site_resolve(rec.program, rec.position, &pos)) {
        printf("fault.position %.*s:%u:%u\n", (int)pos.path_len, pos.path, pos.line, pos.column);
    } else {
        printf("fault.position ?\n");
    }
    printf("fault.revision self\nfault.address none\nfault.stack-depth %u\n", rec.stack_count);
}
static cint_ctx *new_ctx(void)
{
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL;
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = cint_observer_desc.program;
    return cint_ctx_create(&cfg, &ctx) == CINT_OK ? ctx : NULL;
}
static const cint_observer_entry *find(const char *name)
{
    uint32_t i;
    for (i = 0; i < cint_observer_desc.entry_count; i++) {
        const cint_observer_entry *e = &cint_observer_desc.entries[i];
        if (e->module == 0u && e->name_len == strlen(name) && memcmp(e->name, name, e->name_len) == 0) {
            return e;
        }
    }
    return NULL;
}
static const char *type_name(uint32_t tag)
{
    switch (tag) {
    case CINT_TAG_I8: return "I8";
    case CINT_TAG_I16: return "I16";
    case CINT_TAG_I32: return "I32";
    case CINT_TAG_I64: return "I64";
    case CINT_TAG_U8: return "U8";
    case CINT_TAG_U16: return "U16";
    case CINT_TAG_U32: return "U32";
    case CINT_TAG_U64: return "U64";
    default: return "Bool";
    }
}
static int call(const char *name, int64_t fuel, char **argv, int full)
{
    const cint_observer_entry *e = find(name);
    cint_ctx *ctx = new_ctx();
    cint_status st = CINT_REFUSED;
    uint64_t args[16] = {0}, r = 0;
    int64_t used = 0;
    uint32_t i, tag = 0u;
    char ret[128];
    ret[0] = 0;
    if (ctx == NULL) { return 3; }
    if (e != NULL) {
        for (i = 0; i < e->types[1]; i++) {
            uint32_t t = e->types[2 + i];
            const char *v = argv[2 * i + 1];
            args[i] = t == CINT_TAG_BOOL ? (uint64_t)(strcmp(v, "true") == 0)
                      : (t & 0xf0u) == 0x10u ? (uint64_t)(int64_t)strtoll(v, NULL, 10) : (uint64_t)strtoull(v, NULL, 10);
        }
        tag = e->types[0];
        st = e->call(ctx, fuel, 256, args, &r);
    }
    if (st == CINT_OK && tag == CINT_TAG_BOOL) {
        snprintf(ret, sizeof ret, "Bool %s", r != 0u ? "true" : "false");
    } else if (st == CINT_OK && (tag & 0xf0u) == 0x10u) {
        snprintf(ret, sizeof ret, "%s %" PRId64, type_name(tag), r <= (uint64_t)INT64_MAX ? (int64_t)r : -(int64_t)~r - 1);
    } else if (st == CINT_OK && tag != 0u) {
        snprintf(ret, sizeof ret, "%s %" PRIu64, type_name(tag), r);
    }
    if (full) {
        if (st == CINT_OK || st == CINT_FAULT) {
            (void)cint_fuel_consumed(ctx, &used);
            printf("outcome %s\nstdout-bytes 0\n", st == CINT_OK ? "value" : "fault");
            if (st == CINT_OK && ret[0] != 0) { printf("return %s\n", ret); }
            printf("fuel-consumed %" PRId64 "\n", used);
            if (st == CINT_FAULT) { fault_lines(ctx, 1); }
        } else {
            printf("outcome refused\nstatus %d\n", (int)st);
        }
    } else if (st == CINT_OK) {
        printf("value %s\n", ret);
    } else if (st == CINT_FAULT) {
        printf("fault\n");
        fault_lines(ctx, 0);
    } else {
        printf("refused %d\n", (int)st);
    }
    cint_ctx_destroy(ctx);
    return 0;
}
int main(int argc, char **argv)
{
    static char line[4096];
    char *words[40];
    if (argc >= 3 && strcmp(argv[1], "--cases") != 0) {
        return call(argv[1], (int64_t)strtoll(argv[2], NULL, 10), argv + 2, 1);
    }
    /* --cases: CONF-12 case-list lines on stdin, unbounded fuel, one fresh context each. */
    while (fgets(line, sizeof line, stdin) != NULL) {
        int n = 0;
        char *p = line;
        while (*p != 0 && n < 40) {
            while (*p == ' ' || *p == '\n' || *p == '\r') { *p++ = 0; }
            if (*p == 0) { break; }
            words[n++] = p;
            while (*p != 0 && *p != ' ' && *p != '\n' && *p != '\r') { p++; }
        }
        if (n >= 2) {
            if (call(words[1], -1, words + 2, 0) != 0) { return 3; }
        }
    }
    return 0;
}
"""


# -- building and running one program ----------------------------------------------------------
def build_program(name: str, c_path: pathlib.Path):
    """Build emitted C with the runtime and a generated driver; returns (exe, error)."""
    driver = c_path.with_name(c_path.stem + "_driver.c")
    driver.write_text(DRIVER, encoding="ascii", newline="\n")
    exe = c_path.with_name(c_path.stem + (".exe" if STATE["leg"] == "msvc" else ""))
    ok, diags = c_compile([c_path, driver], exe, objects=[STATE["rt_obj"]])
    return (exe if ok else None), diags


def run_entry(exe: pathlib.Path, entry: str, fuel: int, args):
    argv = [str(exe), entry, str(fuel)]
    for t, v in args:
        argv += [v, t]
    env = dict(STATE["env"])
    env["ASAN_OPTIONS"] = "detect_leaks=1"
    r = subprocess.run(argv, capture_output=True, text=True, env=env, timeout=120)
    return r.returncode, r.stdout.splitlines(), r.stderr


def compile_case(ci: pathlib.Path, outdir: pathlib.Path, root=None, cwd=None):
    rel = ci.relative_to(CONF).as_posix()
    out_c = outdir / (rel[:-3].replace("/", "__") + ".c")
    out_c.parent.mkdir(parents=True, exist_ok=True)
    args = [rel, "-o", out_c, "--sitemap", out_c.with_suffix(".sites")]
    if root is not None:
        args += ["--root", root]
    code, _, err = run_seed(args, cwd or CONF)
    return code, err, out_c


def check_case(ci: pathlib.Path, expects):
    """Returns a list of failure strings for one conformance program."""
    failures = []
    outdir = STATE["out"] / "cases"
    code, err, out_c = compile_case(ci, outdir)
    want_error = expect_lines(expects[0])[0] == "outcome compile-error"
    rel = ci.relative_to(CONF).as_posix()
    listed = unsupported().get(rel[:-3])
    if listed is not None:
        # Unsupported by the seed under a documented limit (CONF-14): the listed code, and
        # no output. A listed case that the seed accepts fails until the list is corrected.
        got, first = diag_lines(err)
        if code != 2 or got is None or got[1] != "diagnostic.code " + listed[0] or out_c.exists():
            failures.append("%s: listed as unsupported (%s %s), but exit %d: %s" % (rel, listed[0], listed[1], code, first))
        return failures
    if want_error:
        got, first = diag_lines(err)
        want = expect_lines(expects[0])
        if code != 2 or got != want:
            failures.append("%s: exit %d, want 2\n  got  %s\n  want %s\n  first line: %s" % (rel, code, got, want, first))
        return failures
    if code != 0:
        return ["%s: seed exit %d: %s" % (rel, code, err.strip())]
    exe, diags = build_program(ci.stem, out_c)
    if exe is None:
        return ["%s: emitted C does not build warning-free:\n%s" % (rel, diags)]
    entries, args, fuel = call_spec(header(ci))
    for exp in expects:
        if exp.read_bytes().split(b"\n")[2] == b"format 2":
            continue   # stack and state lines (CONF-11 format 2): compared through the harness by cint_check
        parts = exp.name.split(".")
        entry = parts[1] if len(parts) == 3 else entries[0]
        rc, lines, stderr = run_entry(exe, entry, fuel, args)
        want = expect_lines(exp)
        if rc != 0 or lines != want or "runtime error" in stderr or "Sanitizer" in stderr:
            failures.append("%s [%s]: exit %d\n  got  %s\n  want %s\n%s" % (rel, entry, rc, lines, want, stderr[-2000:]))
    return failures


def check_format(data: bytes) -> list:
    problems = []
    if not data.endswith(b"\n"):
        problems.append("no final LF")
    if b"\r" in data or b"\t" in data:
        problems.append("CR or tab")
    if any(b > 0x7E or (b < 0x20 and b != 0x0A) for b in data):
        problems.append("non-ASCII or control byte")
    if re.search(rb" \n", data):
        problems.append("trailing space")
    for word in (b"float", b"double"):
        if re.search(rb"\b" + word + rb"\b", data):
            problems.append("binary floating type token")
    return problems


def ref_outcome(ci: pathlib.Path, entry: str, fuel: int, args, path=None):
    """cint_ref's outcome lines (from `outcome` on) for one entry, or None when it refuses."""
    cmd = [sys.executable, "-m", "cint_ref", "run", str(ci), "--entry", entry]
    if fuel >= 0:
        cmd += ["--fuel", str(fuel)]
    for t, v in args:
        cmd += ["--arg", "%s %s" % (t, v)]
    if path is not None:
        cmd += ["--path", path]
    r = subprocess.run(cmd, cwd=ROOT / "ref", capture_output=True, text=True)
    lines = r.stdout.splitlines()
    k = next((i for i, l in enumerate(lines) if l.startswith("outcome ")), None)
    if k is None or lines[k] == "outcome refused":
        return None
    return lines[k:]


# A host program for test_22: it calls the `cx` wrappers of boot/wrap.ci through registered
# buffers and views (SPEC-03 A-12, H-12, H-13; slice 2 patch D-2, D-3) and prints one line per check.
CX_HOST = r"""
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "cint_rt.h"
extern const cint_module_info cm_12_boot_x2Fwrap;
extern const cint_observer cint_observer_desc;
cint_status cx_12_boot_x2Fwrap_5_touch(cint_ctx *ctx, int64_t fuel, cint_view t, cint_view w);
cint_status cx_12_boot_x2Fwrap_5_total(cint_ctx *ctx, int64_t fuel, cint_view xs, cint_view ys, uint8_t flip,
                                       int64_t *result);
cint_status cx_12_boot_x2Fwrap_4_four(cint_ctx *ctx, int64_t fuel, cint_view buf);
cint_status cx_12_boot_x2Fwrap_5_twice(cint_ctx *ctx, int64_t fuel, int64_t v, uint8_t neg, int64_t *result);
cint_status cx_12_boot_x2Fwrap_5_tally(cint_ctx *ctx, int64_t fuel, cint_view fs, int64_t *result);
typedef struct Tok { int32_t kind; int32_t len; uint8_t tag[3]; } Tok;
typedef struct Flag { uint8_t on; uint8_t pad; uint8_t more[2]; } Flag;
static int passed, total;
static void check(const char *name, int ok)
{
    total++;
    passed += ok != 0;
    printf("%s: %s\n", name, ok ? "ok" : "FAILED");
}
static cint_view view(cint_buffer_id id, uint16_t code, uint32_t record, uint8_t perm, int64_t origin, int64_t n)
{
    cint_view v;
    memset(&v, 0, sizeof v);
    v.buffer = id;
    v.generation = CINT_BUFFER_GENERATION_FIRST;
    v.type.code = code;
    v.type.record_id = record;
    v.rank = 1u;
    v.perm = perm;
    v.origin = origin;
    v.shape[0] = n;
    v.stride[0] = 1;
    return v;
}
/* The context's fault: code, operation and operands as "I64 a,I64 b", limit; then cleared. */
static int fault_is(cint_ctx *ctx, uint16_t code, const char *op, const char *operands, const char *limit,
                    int64_t fuel)
{
    static cint_fault_record r;
    char buf[256], text[256] = "";
    uint32_t i;
    int64_t used = -1;
    int ok = cint_ctx_fault(ctx, &r) == CINT_OK && r.code == code && r.operation_len == strlen(op) &&
             memcmp(r.operation, op, r.operation_len) == 0 && cint_fuel_consumed(ctx, &used) == CINT_OK && used == fuel;
    for (i = 0; i < r.operand_count; i++) {
        size_t n = cint_tvalue_render(&r.operands[i], buf, sizeof buf), len = strlen(text);
        snprintf(text + len, sizeof text - len, "%s%.*s", i > 0 ? "," : "", (int)n, buf);
    }
    ok = ok && strcmp(text, operands) == 0;
    if (limit != NULL) {
        ok = ok && r.has_limit && cint_tvalue_render(&r.limit, buf, sizeof buf) > 0u && strcmp(buf, limit) == 0;
    }
    return ok && cint_ctx_clear_fault(ctx) == CINT_OK;
}
static int refused_for(cint_ctx *ctx, cint_status st, uint32_t want)
{
    uint32_t why = 0u;
    return st == CINT_REFUSED && cint_ctx_refusal(ctx, &why) == CINT_OK && why == want;
}
int main(void)
{
    const cint_module_info *cm = &cm_12_boot_x2Fwrap;
    const cint_record_layout *rec = cm->records;
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL;
    Tok toks[3];
    int64_t w[3] = {10, 20, 30}, xs[4] = {1, 2, 3, 4}, ys[4] = {0, 0, 0, 0}, r = 0;
    int32_t four[4] = {5, 0, 0, 0};
    cint_buffer_id bt = 0, bw = 0, bx = 0, by = 0, bf = 0, bstale = 0;
    uint32_t i;
    int ok;
    memset(toks, 0xA5, sizeof toks);
    for (i = 0; i < 3u; i++) {
        toks[i].len = (int32_t)i;
    }
    check("cm descriptor", cm->abi == CINT_ABI_VERSION && cm->program == cint_observer_desc.program &&
          cm->record_count == 2u && rec[0].bytes == sizeof(Tok) && rec[0].align == 4u && rec[0].field_count == 3u &&
          rec[0].fields[0].offset == 0u && rec[0].fields[1].offset == 4u && rec[0].fields[2].offset == 8u &&
          rec[0].fields[2].count == 3u && rec[0].fields[2].type.code == CINT_TAG_U8 &&
          rec[0].fields[0].type.code == CINT_TAG_I32);
    check("observer table", cint_observer_desc.entry_count == 3u &&
          cint_observer_desc.entries[0].module == 0u && memcmp(cint_observer_desc.entries[0].name, "twice", 5) == 0 &&
          cint_observer_desc.entries[1].module == 1u && memcmp(cint_observer_desc.entries[1].name, "half", 4) == 0 &&
          cint_observer_desc.entries[2].module == 1u && memcmp(cint_observer_desc.entries[2].name, "neg8", 4) == 0);
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = cm->program;
    if (cint_ctx_create(&cfg, &ctx) != CINT_OK) {
        printf("context: FAILED\n");
        return 1;
    }
    ok = cint_buffer_register_bytes(ctx, toks, (int64_t)sizeof toks, CINT_VIEW_WRITE, &bt) == CINT_OK &&
         cint_buffer_register_bytes(ctx, w, (int64_t)sizeof w, CINT_VIEW_READ, &bw) == CINT_OK &&
         cint_buffer_register_bytes(ctx, xs, (int64_t)sizeof xs, CINT_VIEW_WRITE, &bx) == CINT_OK &&
         cint_buffer_register_bytes(ctx, ys, (int64_t)sizeof ys, CINT_VIEW_WRITE, &by) == CINT_OK &&
         cint_buffer_register_bytes(ctx, four, (int64_t)sizeof four, CINT_VIEW_WRITE, &bf) == CINT_OK &&
         cint_buffer_register_bytes(ctx, w, (int64_t)sizeof w, CINT_VIEW_READ, &bstale) == CINT_OK &&
         cint_buffer_release(ctx, bstale) == CINT_OK;
    check("registry", ok);
    {   /* a struct-element inout view and an I64 view: every byte outside the written fields kept */
        cint_status st = cx_12_boot_x2Fwrap_5_touch(ctx, -1, view(bt, CINT_TAG_RECORD, 0u, 1u, 0, 3),
                                                    view(bw, CINT_TAG_I64, 0u, 0u, 0, 3));
        int64_t used = 0;
        ok = st == CINT_OK && cint_fuel_consumed(ctx, &used) == CINT_OK && used == 4;
        for (i = 0; i < 3u; i++) {
            ok = ok && toks[i].kind == (int32_t)(w[i] + 1) && toks[i].len == (int32_t)i + 1 && toks[i].tag[0] == 0xA5 &&
                 toks[i].tag[1] == 0xA5 && toks[i].tag[2] == 7;
        }
        check("struct view round trip", ok);
    }
    check("E_SHAPE between views", cx_12_boot_x2Fwrap_5_touch(ctx, -1, view(bt, CINT_TAG_RECORD, 0u, 1u, 0, 3),
                                                             view(bw, CINT_TAG_I64, 0u, 0u, 1, 2)) == CINT_FAULT &&
          fault_is(ctx, CINT_E_SHAPE, "bind.shape", "I64 1,I64 0,I64 2", "I64 3", 0) && toks[0].len == 1);
    check("E_SHAPE constant extent", cx_12_boot_x2Fwrap_4_four(ctx, -1, view(bf, CINT_TAG_I32, 0u, 1u, 0, 3)) ==
          CINT_FAULT && fault_is(ctx, CINT_E_SHAPE, "bind.shape", "I64 0,I64 0,I64 3", "I64 4", 0) && four[3] == 0);
    check("constant extent", cx_12_boot_x2Fwrap_4_four(ctx, -1, view(bf, CINT_TAG_I32, 0u, 1u, 0, 4)) == CINT_OK &&
          four[3] == 6);
    check("E_ALIAS", cx_12_boot_x2Fwrap_5_total(ctx, -1, view(bx, CINT_TAG_I64, 0u, 0u, 0, 3),
                                               view(bx, CINT_TAG_I64, 0u, 1u, 1, 3), 1u, &r) == CINT_FAULT &&
          fault_is(ctx, CINT_E_ALIAS, "bind.alias", "I64 1,I64 0", NULL, 0) && xs[1] == 2);
    check("adjacent views do not overlap", cx_12_boot_x2Fwrap_5_total(ctx, -1, view(bx, CINT_TAG_I64, 0u, 0u, 0, 2),
                                                                     view(bx, CINT_TAG_I64, 0u, 1u, 2, 2), 0u, &r) ==
          CINT_OK && r == 3);
    check("views by value", cx_12_boot_x2Fwrap_5_total(ctx, -1, view(bx, CINT_TAG_I64, 0u, 0u, 0, 4),
                                                       view(by, CINT_TAG_I64, 0u, 1u, 0, 4), 1u, &r) == CINT_OK &&
          r == 10 && ys[3] == 4);
    r = 99;
    check("Bool above 1 refused", cx_12_boot_x2Fwrap_5_total(ctx, -1, view(bx, CINT_TAG_I64, 0u, 0u, 0, 4),
                                                             view(by, CINT_TAG_I64, 0u, 1u, 0, 4), 2u, &r) ==
          CINT_REFUSED && r == 99);
    check("element type refused", refused_for(ctx, cx_12_boot_x2Fwrap_5_touch(ctx, -1, view(bt, CINT_TAG_I64, 0u, 1u,
                                                                                         0, 3),
                                                                          view(bw, CINT_TAG_I64, 0u, 0u, 0, 3)),
                                              CINT_REFUSAL_TYPE));
    check("record id refused", refused_for(ctx, cx_12_boot_x2Fwrap_5_touch(ctx, -1, view(bt, CINT_TAG_RECORD, 1u, 1u,
                                                                                     0, 3),
                                                                      view(bw, CINT_TAG_I64, 0u, 0u, 0, 3)),
                                          CINT_REFUSAL_TYPE));
    check("stale generation refused", refused_for(ctx, cx_12_boot_x2Fwrap_5_touch(ctx, -1, view(bt, CINT_TAG_RECORD,
                                                                                            0u, 1u, 0, 3),
                                                                             view(bstale, CINT_TAG_I64, 0u, 0u, 0, 3)),
                                                 CINT_REFUSAL_GENERATION));
    check("read view for inout refused", refused_for(ctx, cx_12_boot_x2Fwrap_4_four(ctx, -1, view(bf, CINT_TAG_I32, 0u,
                                                                                               0u, 0, 4)),
                                                    CINT_REFUSAL_PERMISSION));
    check("refusal before shape", refused_for(ctx, cx_12_boot_x2Fwrap_5_touch(ctx, -1, view(bt, CINT_TAG_RECORD, 0u, 1u,
                                                                                        0, 3),
                                                                         view(bw, CINT_TAG_I64, 0u, 0u, 2, 2)),
                                             CINT_REFUSAL_EXTENT));
    check("extent past the buffer refused", refused_for(ctx, cx_12_boot_x2Fwrap_5_touch(ctx, -1,
        view(bt, CINT_TAG_RECORD, 0u, 1u, 1, 3), view(bw, CINT_TAG_I64, 0u, 0u, 0, 3)), CINT_REFUSAL_EXTENT));
    check("scalar wrapper", cx_12_boot_x2Fwrap_5_twice(ctx, -1, 21, 0u, &r) == CINT_OK && r == 42 &&
          cx_12_boot_x2Fwrap_5_twice(ctx, -1, 5, 1u, &r) == CINT_OK && r == 2);
    {   /* an observer entry of a module other than the root */
        uint64_t args[1] = {(uint64_t)(int64_t)-128}, res = 0;
        check("observer entry of module 1", cint_observer_desc.entries[2].call(ctx, -1, 8, args, &res) == CINT_FAULT &&
              fault_is(ctx, CINT_E_OVERFLOW, "neg.checked.i8", "I8 -128", "I8 127", 1));
    }
    check("depth from the context", cx_12_boot_x2Fwrap_5_twice(ctx, -1, 9, 1u, &r) == CINT_OK && r == 4);
    {   /* A-13: a Bool inside a view's elements is checked as a byte before the body reads it */
        Flag fs[2] = {{1u, 0u, {0u, 1u}}, {0u, 9u, {0u, 0u}}};
        cint_buffer_id bfl = 0;
        ok = cint_buffer_register_bytes(ctx, fs, (int64_t)sizeof fs, CINT_VIEW_READ, &bfl) == CINT_OK;
        check("Bool fields 0 and 1 accepted", ok && cx_12_boot_x2Fwrap_5_tally(ctx, -1, view(bfl, CINT_TAG_RECORD, 1u,
              0u, 0, 2), &r) == CINT_OK && r == 10101);
        fs[0].on = 2u;
        r = 99;
        check("Bool field above 1 refused", refused_for(ctx, cx_12_boot_x2Fwrap_5_tally(ctx, -1, view(bfl,
              CINT_TAG_RECORD, 1u, 0u, 0, 2), &r), CINT_REFUSAL_BOOL) && r == 99);
        fs[0].on = 1u;
        fs[1].more[1] = 255u;
        check("Bool array field above 1 refused", refused_for(ctx, cx_12_boot_x2Fwrap_5_tally(ctx, -1, view(bfl,
              CINT_TAG_RECORD, 1u, 0u, 0, 2), &r), CINT_REFUSAL_BOOL) && r == 99);
    }
    cint_ctx_destroy(ctx);
    printf("cx: %d of %d checks passed\n", passed, total);
    return passed == total ? 0 : 1;
}
"""

# -- the tests -------------------------------------------------------------------------------------
class SeedLeg(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        leg = ARGS.leg or ("msvc" if os.name == "nt" else "gcc")
        STATE["leg"] = leg
        STATE["out"] = pathlib.Path(ARGS.out) if ARGS.out else default_out(leg)
        STATE["env"] = msvc_environment() if leg == "msvc" else dict(os.environ)
        avail = available_memory()
        if avail < MIN_AVAILABLE_BYTES:
            raise unittest.SkipTest("only %.1f GiB available; the plan needs 8 GiB" % (avail / 1024 ** 3))
        out = STATE["out"]
        if out.exists():
            shutil.rmtree(out)
        out.mkdir(parents=True)
        seed_exe = out / ("cint-seed.exe" if leg == "msvc" else "cint-seed")
        sources = sorted(SEED.glob("*.c"))
        ok, diags = c_compile(sources + [RT / "cint_rt.c", RT / "cint_bridge.c"], seed_exe)
        STATE["seed_build"] = (ok, diags, [s.name for s in sources])
        STATE["seed"] = seed_exe
        rt_obj = out / "rt" / ("cint_rt.obj" if leg == "msvc" else "cint_rt.o")
        ok2, diags2 = c_compile([RT / "cint_rt.c"], rt_obj, obj_only=True)
        if not (ok and ok2):
            raise AssertionError("seed or runtime build failed:\n%s\n%s" % (diags, diags2))
        STATE["rt_obj"] = rt_obj

    def test_00_seed_builds_warning_free(self):
        ok, diags, names = STATE["seed_build"]
        self.assertTrue(ok, diags)
        self.assertIn("seed_main.c", names)

    def test_01_size_target(self):
        files = sorted(SEED.glob("*.c")) + sorted(SEED.glob("*.h"))
        total = sum(p.read_bytes().count(b"\n") for p in files)
        print("[seed] %d physical lines in %d files (ceiling %d)" % (total, len(files), SIZE_CEILING))
        self.assertLessEqual(total, SIZE_CEILING)
        for p in files:
            self.assertNotIn(b"\r", p.read_bytes(), p.name)

    def test_02_no_binary_floating_types(self):
        words = ["flo" + "at", "dou" + "ble"]
        pattern = re.compile(r"\b(" + "|".join(words) + r")\b")
        hits = ["%s:%d" % (p.name, n) for p in sorted(SEED.rglob("*.[ch]"))
                for n, line in enumerate(p.read_text(encoding="ascii").splitlines(), 1) if pattern.search(line)]
        self.assertEqual(hits, [])

    def test_03_expect_format_reader(self):
        cases = (
            ("diag/c1020_uppercase_hex_prefix", [
                "outcome compile-error", "diagnostic.code C1020",
                "diagnostic.position diag/c1020_uppercase_hex_prefix.ci:8:13"]),
            ("module/private_struct_c3008", [
                "outcome compile-error", "diagnostic.code C3008",
                "diagnostic.position module/private_struct_c3008.ci:10:12"]),
        )
        for name, want in cases:
            ci = CONF / (name + ".ci")
            exp = CONF / (name + ".expect")
            self.assertEqual(expect_lines(exp), want)
            self.assertEqual(check_case(ci, [exp]), [])
        runtime = CONF / "boot" / "compound_assignment"
        self.assertEqual(expect_lines(runtime.with_suffix(".expect"))[0], "outcome value")
        self.assertEqual(check_case(runtime.with_suffix(".ci"), [runtime.with_suffix(".expect")]), [])

    def test_10_conformance_boot_cases(self):
        cases = boot_cases()
        self.assertGreater(len(cases), 40)
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            results = list(pool.map(lambda c: check_case(*c), cases))
        failures = [f for r in results for f in r]
        print("[seed %s] %d boot-1 programs, %d expectations, %d failures"
              % (STATE["leg"], len(cases), sum(len(e) for _, e in cases), len(failures)))
        self.assertEqual(failures, [], "\n".join(failures))

    def test_11_seed08_rows_and_review_focus_4(self):
        rows = ["seed08_literal_out_of_range", "seed08_const_overflow", "seed08_const_i64_overflow",
                "seed08_runtime_overflow", "seed08_wrap_i8", "seed08_const_div_zero",
                "const_nested_literal_out_of_range"]
        names = {ci.stem for ci, _ in boot_cases()}
        for r in rows:
            self.assertIn(r, names)
            ci = CONF / "arith" / (r + ".ci")
            self.assertEqual(check_case(ci, [CONF / "arith" / (r + ".expect")]), [])
        # Review Focus 4 written inline: a nested literal-only expression in I16 context.
        failures = self.inline_diag("export I16 run() {\n    I16 h = 2 * (40000 - 30000);\n    return h;\n}\n",
                                    "C2003", 2, 18)
        self.assertEqual(failures, [])

    def test_12_conf01(self):
        # The CONF-01 listing uses a test block and a print statement, which SEED-14 excludes:
        # the seed must refuse it with a positioned diagnostic (the `test` keyword).
        code, err, _ = compile_case(CONF / "arith" / "add_i64_overflow.ci", STATE["out"] / "conf01")
        self.assertEqual(code, 2, err)
        self.assertTrue(err.startswith("arith/add_i64_overflow.ci:2:1: error %s: " % BOOT_REFUSAL), err)
        # Its cint-boot-1 transliteration keeps the faulting `+` at 5:15 and must reproduce
        # the frozen CONF-01 record field for field (seed/OPEN.md SEED-OQ-02).
        src = ("// conformance/arith/add_i64_overflow.ci as cint-boot-1 (seed/tests/test_seed.py)\n"
               "export void run() {\n    I64 a = 9223372036854775807;\n    I64 b = 1;\n"
               "    I64 c = a + b;\n}\n")
        tmp = pathlib.Path(tempfile.mkdtemp(dir=STATE["out"]))
        (tmp / "arith").mkdir()
        (tmp / "arith" / "add_i64_overflow.ci").write_bytes(src.encode("ascii"))
        out_c = tmp / "conf01.c"
        code, _, err = run_seed(["arith/add_i64_overflow.ci", "-o", out_c, "--root", tmp], tmp)
        self.assertEqual(code, 0, err)
        exe, diags = build_program("conf01", out_c)
        self.assertIsNotNone(exe, diags)
        rc, lines, stderr = run_entry(exe, "run", -1, [])
        self.assertEqual((rc, lines), (0, expect_lines(CONF / "arith" / "add_i64_overflow.expect")), stderr)

    def inline_diag(self, source: str, code: str, line: int, col: int, path="boot/inline.ci"):
        tmp = pathlib.Path(tempfile.mkdtemp(dir=STATE["out"]))
        (tmp / path).parent.mkdir(parents=True, exist_ok=True)
        (tmp / path).write_bytes(source.encode("utf-8"))
        rc, _, err = run_seed([path, "-o", tmp / "out.c", "--root", tmp], tmp)
        lines = err.splitlines()
        want = "%s:%d:%d: error %s: " % (path, line, col, code)
        if rc != 2 or not lines or not lines[0].startswith(want) or len(lines) != 3 or not lines[2].rstrip().endswith("^"):
            return ["%r: exit %d, want 2 and %r with a source and caret line; got %r" % (source[:40], rc, want, err)]
        if (tmp / "out.c").exists():
            return ["output written although compilation failed"]
        return []

    def test_13_outside_boot_subset(self):
        b = BOOT_REFUSAL
        cases = [
            ('test "t" {\n    I64 a = 1;\n}\n', b, 1, 1),                              # test block
            ('export void run() {\n    "x\\n";\n}\n', b, 2, 5),                         # print statement
            ("export I8 run() {\n    I8 a = 100;\n    return a +| a;\n}\n", b, 3, 14),  # saturating operator
            ("export I64 run() {\n    I64 a = 1;\n    a +|= 1;\n    return a;\n}\n", b, 3, 7),
            ("export I32 run() {\n    Q16.16 x = 0.5;\n    return 0;\n}\n", b, 2, 5),   # fixed point
            ("enum Mode : U8 { idle }\n", b, 1, 1),
            ("I64 f(I64 n) {\n    return f(n);\n}\n", "C4040", 2, 12),                # recursion
            ("export I64 run() {\n    defer run();\n    return 0;\n}\n", b, 2, 5),
            ("export I128 run() {\n    return 0;\n}\n", b, 1, 8),
            ("export I64 run() {\n    I64 x = 1;\n    x = x catch 0;\n    return x;\n}\n", b, 3, 11),
            ("profile \"cint-core-1\";\n", b, 1, 1),
            ("I64 g = 0;\n", b, 1, 1),                                                 # module variable
            ("export I64 run() {\n    assert(true);\n    return 0;\n}\n", b, 2, 5),
            ("export I64 run() {\n    I64 x = 1;\n    // caf\xe9\n    return x;\n}\n", b, 3, 11),
        ]
        failures = []
        for src, code, line, col in cases:
            failures += self.inline_diag(src, code, line, col)
        self.assertEqual(failures, [], "\n".join(failures))

    def test_14_lexical_rules(self):
        cases = [
            ("export I64 run() {\n    return 0X1F;\n}\n", "C1020", 2, 12),
            ("export I64 run() {\n    return 1_;\n}\n", "C1021", 2, 12),
            ("export I64 run() {\n    return 0x_1;\n}\n", "C1021", 2, 12),
            ("export I64 run() {\n    return 00;\n}\n", "C1022", 2, 12),
            ("export I64 run() {\n    return 1;\n}\r\n\r", "C1004", 4, 1),
            ("/* open\nexport I64 run() {\n    return 1;\n}\n", "C1005", 1, 1),
            ("export U8 run() {\n    return '';\n}\n", "C1030", 2, 12),
            ("export U8 run() {\n    return '\\q';\n}\n", "C1032", 2, 13),
            ("export U8 run() {\n    return '\\x80';\n}\n", "C1032", 2, 13),
            ("export I64 run() {\n    I64 I7 = 1;\n    return 0;\n}\n", "C1010", 2, 9),
        ]
        failures = []
        for src, code, line, col in cases:
            failures += self.inline_diag(src, code, line, col)
        self.assertEqual(failures, [], "\n".join(failures))

    def test_19_codes_agree_with_cint_ref(self):
        """D-4 and D-11: the seed reports the code and position cint_ref reports, error by error."""
        def fn(*body):
            return "export I64 run() {\n" + "".join("    %s\n" % b for b in body) + "    return 0;\n}\n"
        st = "struct S {\n    I64 a;\n    I64 b;\n}\n"
        f1 = "I64 f(I64 a) {\n    return a;\n}\n"
        sources = [
            fn("return y;"), fn("Foo x = 1;"), "struct S {\n    I64 a;\n    I64 a;\n}\n" + fn(),
            "struct S {\n    T t;\n}\nstruct T {\n    S s;\n}\n" + fn(), "const I64 A = A + 1;\n" + fn(),
            fn("I64 v = 1;", "const I64 K = v;"), "void f() {\n}\n" + fn("I64 x = f();"), fn("I64 x = I64;"),
            fn("I64 = 1;"), fn("I64 a = 1;", "Bool b = !a;"), st + fn("S p = S(1, 2);", "Bool b = p == p;"),
            fn("Bool b = true < false;"), fn("Bool b = true;", "I64 x = b + 1;"), fn("Bool b = true;", "Bool c = -b;"),
            st + fn("S p = S(1, 2);", "Bool b = p as Bool;"), st + fn("S p = S(1, 2);", "I64 b = p as I64;"),
            fn("I64 x = true as% I64;"), fn("I64 a = 1;", "I64 b = a.x;"), st + fn("S p = S(1, 2);", "I64 b = p.z;"),
            fn("I64 a = 1;", "I64 b = a(2);"), st + fn("S p = S(a = 1, 2);"), st + fn("S p = S(1, 2, 3);"),
            f1 + fn("I64 v = f(1, 2);"), f1 + fn("I64 v = f();"), fn("I64 a = 1;", "a + 1 = 2;"),
            fn("const I64 K = 1;", "K = 2;"),
            fn("I64 a = 1;", "I64 v = 2;", "switch (a) {", "    case v:", "        return 1;", "    default:",
               "        return 0;", "}"),
            fn("I64 a = 1;", "switch (a) {", "    case 5..=1:", "        return 1;", "    default:", "        return 0;", "}"),
            fn("break;"), fn("continue;"), "void f() {\n    return 1;\n}\n" + fn(), "I64 f() {\n    return;\n}\n" + fn(),
            fn("I64 x = ;"), fn("I64 x = 0;", "while x < 1 {", "}"), fn("I64 x = 0;", "while (x < 1) x = 1;"),
            fn("I64 x = 0;", "for i in 0..3 x = 1;"), fn("I64 x = 0;", "for (I64 i = 0; i < 3; i++) x = 1;"),
            fn("I64 a = 1;", "switch (a) {", "    default:", "        return 0;", "    case 1:", "        return 1;", "}"),
            fn("I64 a = 1;", "switch (a) {", "    case 1:", "        return 1;", "    default:", "        return 0;",
               "    default:", "        return 2;", "}"),
            "export I64 run() {\n    I64 a = 1;\n", fn("I64 a = 0x;"), fn("I64 a = 0b12;"), fn("I64 a = 1 $ 2;"),
            fn("U8 c = 'a;"), fn("I64 %s = 1;" % ("a" * 256)), fn("I64 x;"), fn("I64 x = 1;", "I64 x = 2;"),
            "export I64 run() {\n    I64 x = 1;\n    if (x == 1) {\n        return 1;\n    }\n}\n",
            fn("I8 x = 1;", "I8 y = x + (1 << 6);"), fn("I8 x = 0;", "Bool b = x < (1 << 10);"),
            fn("I8 a = - 128;", "U8 c = - 1;"), fn("I8 d = -(128);"), fn("I8 x = -(128) as I8;"),
            fn("I8 a = (300) as I8;"), fn("I64 x = 1;", "Bool b = x == 2 && ((300 as I8) == 0);"),
            fn("I8 x = 1;", "Bool b = x == 2 && (x < 300);"), fn("Bool b = false && (1 / 0 == 300 as I8);"),
            fn("Bool b = (false && (5 < 300)) || (1 / 0 == 0);"),
            fn("I64 a = 0x1%s;" % ("0" * 1024)),
            fn("I64 a = 1;", "{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{ a = 2; }}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}"),
        ]
        failures = []
        tmp = pathlib.Path(tempfile.mkdtemp(dir=STATE["out"]))
        for k, src in enumerate(sources):
            rel = "boot/d%d.ci" % k
            (tmp / rel).parent.mkdir(parents=True, exist_ok=True)
            (tmp / rel).write_bytes(src.encode("ascii"))
            rc, _, err = run_seed([rel, "-o", tmp / ("d%d.c" % k), "--root", tmp], tmp)
            got = diag_lines(err)[0] if rc == 2 else ["exit %d" % rc]
            want = ref_outcome(tmp / rel, "run", -1, [], path=rel)
            if want is not None:
                want = want[:3] if want[0] == "outcome compile-error" else ["exit 0"]
            if got is None or want is None or got[:3] != want[:3]:
                failures.append("%r\n  seed     %s\n  cint_ref %s" % (src[-70:], got, want))
        print("[seed %s] %d diagnostics compared with cint_ref, %d failures" % (STATE["leg"], len(sources), len(failures)))
        self.assertEqual(failures, [], "\n".join(failures))

    def test_19c_qualified_type_diagnostics(self):
        tmp = pathlib.Path(tempfile.mkdtemp(dir=STATE["out"]))
        (tmp / "lib").mkdir()
        (tmp / "app").mkdir()
        (tmp / "lib" / "dep.ci").write_bytes(b"export const I64 VALUE = 1;\n")
        (tmp / "lib" / "m.ci").write_bytes(
            b"import lib.dep;\n"
            b"export I64 f() { return 1; }\n"
            b"export const I64 K = 1;\n"
            b"const I64 hidden = 1;\n"
            b"export struct S { I64 v; }\n")
        cases = (
            ("function", "import lib.m;\nexport I64 run() { m.f v; return 0; }\n", "f", "C3007", "`f` is not a type"),
            ("constant", "import lib.m;\nexport I64 run() { m.K v; return 0; }\n", "K", "C3007", "`K` is not a type"),
            ("missing", "import lib.m;\nexport I64 run() { m.absent v; return 0; }\n", "absent", "C3005", "unknown type `absent`"),
            ("private", "import lib.m;\nexport I64 run() { m.hidden v; return 0; }\n", "hidden", "C3008", "the struct is not exported by its module"),
            ("unknown alias", "export I64 run() { x.f v; return 0; }\n", "x", "C3006", "`x` is not an imported module"),
            ("nonmodule base", "const I64 m = 1;\nexport I64 run() { m.f v; return 0; }\n", "m", "C3006", "`m` is not an imported module"),
            ("unknown builtin base", "export I64 run() { x.I64 v = 0; return 0; }\n", "x", "C3006", "`x` is not an imported module"),
            ("unknown bool base", "export I64 run() { x.Bool v = true; return 0; }\n", "x", "C3006", "`x` is not an imported module"),
            ("nonmodule builtin base", "const I64 m = 1;\nexport I64 run() { m.I64 v = 0; return 0; }\n", "m", "C3006", "`m` is not an imported module"),
            ("builtin member", "import lib.m;\nexport I64 run() { m.I64 v = 0; return 0; }\n", "I64", "C3005", "unknown type `I64`"),
            ("bool member", "import lib.m;\nexport I64 run() { m.Bool v = true; return 0; }\n", "Bool", "C3005", "unknown type `Bool`"),
            ("transitive import alias", "import lib.m;\nexport I64 run() { m.dep v; return 0; }\n", "dep", "C3005", "unknown type `dep`"),
        )
        failures = []
        for name, source, token, code, message in cases:
            rel = "app/main.ci"
            (tmp / rel).write_bytes(source.encode("ascii"))
            line = next(i for i, s in enumerate(source.splitlines(), 1) if "run()" in s)
            col = source.splitlines()[line - 1].index(token + ("." if token in ("x", "m") else " v")) + 1
            out_c = tmp / "out.c"
            out_c.unlink(missing_ok=True)
            rc, stdout, stderr = run_seed([rel, "-o", out_c, "--root", tmp], tmp)
            nl = os.linesep
            want = "%s:%d:%d: error %s: %s%s%s%s%s^%s" % (
                rel, line, col, code, message, nl, source.splitlines()[line - 1], nl, " " * (col - 1), nl)
            ref = ref_outcome(tmp / rel, "run", -1, [], path=rel)
            if rc != 2 or stdout or stderr != want or out_c.exists() or ref is None or ref[:3] != [
                    "outcome compile-error", "diagnostic.code " + code,
                    "diagnostic.position %s:%d:%d" % (rel, line, col)]:
                failures.append("%s: seed exit %d %r; expected %r; ref %r" % (name, rc, stderr, want, ref))
        self.assertEqual(failures, [], "\n".join(failures))

    def test_19d_signature_phase_order(self):
        """Compares size names, parameter types, bound sizes, then result types."""
        tmp = pathlib.Path(tempfile.mkdtemp(dir=STATE["out"]))
        (tmp / "records.ci").write_bytes(b"export struct R { I64 value; }\n")
        bodies = {
            "alias_size": "I64 take[records](in records.Missing[_] rows) { return 0; }",
            "duplicate_size": "I64 take[n, n](in records.Missing[n] rows) { return 0; }",
            "global_size": "const I64 n = 2;\nI64 take[n](in records.Missing[n] rows) { return 0; }",
            "prelude_size": "I64 take[len](in records.Missing[len] rows) { return 0; }",
            "unbound_before_result": "records.Missing take[n](in records.R[_] rows) { return records.Missing(); }",
            "parameter_before_result": "absent.R take(in missing.R[_] rows) { return absent.R(); }",
            "parameter_before_bound": "I64 take[n](in missing.R[_] rows) { return 0; }",
            "result_before_public_limit": "export records.Missing take(records.R value) { return records.Missing(); }",
            "bound_before_public_limit": "export records.Missing take[n](records.R value) { return records.Missing(); }",
        }
        for name, body in bodies.items():
            with self.subTest(case=name):
                rel = "main.ci"
                (tmp / rel).write_bytes(("import records;\n" + body + "\nexport I64 run() { return 0; }\n").encode("ascii"))
                want = ref_outcome(tmp / rel, "run", -1, [], path=rel)
                self.assertIsNotNone(want)
                self.assertEqual(want[0], "outcome compile-error")
                output = tmp / (name + ".c")
                rc, stdout, stderr = run_seed([rel, "-o", output, "--root", tmp], tmp)
                found = DIAG_RE.match(stderr.splitlines()[0]) if stderr else None
                self.assertEqual(rc, 2, stderr)
                self.assertEqual(stdout, "")
                self.assertFalse(output.exists())
                self.assertIsNotNone(found, stderr)
                got = ["outcome compile-error", "diagnostic.code " + found["code"],
                       "diagnostic.position %(path)s:%(line)s:%(col)s" % found.groupdict()]
                self.assertEqual(got, want[:3])
        for body, code, token in (
                ("export records.R take(records.R value) { return value; }", "C9100", "R take"),
                ("export I64 take(records.R value) { return value.value; }", "C9102", "value")):
            with self.subTest(limit=code):
                rel = "main.ci"
                (tmp / rel).write_bytes(("import records;\n" + body + "\nexport I64 run() { return 0; }\n").encode("ascii"))
                want = ref_outcome(tmp / rel, "run", -1, [], path=rel)
                self.assertIsNotNone(want)
                self.assertEqual(want[0], "outcome value")
                output = tmp / (code + ".c")
                rc, stdout, stderr = run_seed([rel, "-o", output, "--root", tmp], tmp)
                found = DIAG_RE.match(stderr.splitlines()[0]) if stderr else None
                self.assertEqual(rc, 2, stderr)
                self.assertEqual(stdout, "")
                self.assertFalse(output.exists())
                self.assertIsNotNone(found, stderr)
                self.assertEqual((found["code"], found["path"], found["line"], found["col"]),
                                 (code, rel, "2", str(body.index(token) + 1)))

    def test_19e_field_name_before_type(self):
        """Checks each field name before its type, in declaration order."""
        tmp = pathlib.Path(tempfile.mkdtemp(dir=STATE["out"]))
        bodies = {
            "duplicate_before_fault": "struct R { I64 a; I64[1 / 0] a; }",
            "duplicate_before_unknown": "struct R { I64 a; Missing a; }",
            "duplicate_before_width": "struct R { I64 a; I64[true] a; }",
            "first_type_before_duplicate": "struct R { Missing a; I64 a; }",
            "first_fault_before_duplicate": "struct R { I64[1 / 0] a; I64 a; }",
            "later_field_after_fault": "struct R { I64 a; I64[1 / 0] b; I64 a; }",
        }
        for name, body in bodies.items():
            with self.subTest(case=name):
                rel = "main.ci"
                (tmp / rel).write_bytes((body + "\nexport I64 run() { return 0; }\n").encode("ascii"))
                want = ref_outcome(tmp / rel, "run", -1, [], path=rel)
                self.assertIsNotNone(want)
                self.assertEqual(want[0], "outcome compile-error")
                output = tmp / (name + ".c")
                rc, stdout, stderr = run_seed([rel, "-o", output, "--root", tmp], tmp)
                found = DIAG_RE.match(stderr.splitlines()[0]) if stderr else None
                self.assertEqual(rc, 2, stderr)
                self.assertEqual(stdout, "")
                self.assertFalse(output.exists())
                self.assertIsNotNone(found, stderr)
                got = ["outcome compile-error", "diagnostic.code " + found["code"],
                       "diagnostic.position %(path)s:%(line)s:%(col)s" % found.groupdict()]
                self.assertEqual(got, want[:3])

    def test_19f_early_signature_dependencies(self):
        """Resolves called type signatures before classifying an extent as nonconstant."""
        tmp = pathlib.Path(tempfile.mkdtemp(dir=STATE["out"]))
        bodies = {
            'narrow_result': 'struct R { I64[f()] a; }\nU8 f() { return 2; }',
            'bool_result': 'struct R { I64[f()] a; }\nBool f() { return true; }',
            'void_result': 'struct R { I64[f()] a; }\nvoid f() {}',
            'integer_result': 'struct R { I64[f()] a; }\nI64 f() { return 2; }',
            'arity_before_parameter': 'struct R { I64[f()] a; }\nI64 f(Missing n) { return 0; }',
            'parameter_before_result': 'struct R { I64[f(1)] a; }\nAbsent f(Missing n) { return n; }',
            'invalid_parameter': 'struct R { I64[f(1)] a; }\nI64 f(Missing n) { return 0; }',
            'argument_width': 'struct R { I64[f(true)] a; }\nI64 f(I64 n) { return n; }',
            'typed_parameter': 'struct R { I64[f(1)] a; }\nU8 f(U8 n) { return n; }',
            'invalid_result': 'struct R { I64[f()] a; }\nMissing f() { return 0; }',
            'size_name_before_result': 'struct R { I64[f()] a; }\nMissing f[len]() { return 0; }',
            'unbound_size_before_result': 'struct R { I64[f()] a; }\nMissing f[n]() { return 0; }',
            'nested_signature': 'struct R { I64[f("")] a; }\nI64 f(in U8[g()] a) { return 0; }\nU8 g() { return 2; }',
            'arity_before_nested_signature': 'struct R { I64[f()] a; }\nI64 f(in U8[g()] a) { return 0; }\nU8 g() { return 2; }',
            'pending_constructor': 'struct A { I64 value; }\nstruct R { I64[f(A(1))] a; }\nI64 f(A x) { return x.value; }',
            'nested_argument_call': 'struct R { I64[f(g())] a; }\nI64 f(I64 x) { return x; }\nU8 g() { return 2; }',
            'constant_initializer': 'const I64 N = f();\nU8 f() { return 2; }',
            'pending_constant': 'const I64 N = f("x");\nconst I64 K = 1;\nU8 f(in U8[K] x) { return 2; }',
            'pending_constant_chain': 'const I64 N = f("x");\nconst I64 K = L + 1;\nconst I64 L = 0;\nU8 f(in U8[K] x) { return 2; }',
            'pending_constant_width': 'const I64 N = f("x");\nconst U8 K = 1;\nU8 f(in U8[K] x) { return 2; }',
            'pending_constant_fault': 'const I64 N = f("x");\nconst I64 K = 1 / 0;\nU8 f(in U8[K] x) { return 2; }',
            'cached_parameter': 'const I64 N = f("aa", "x");\nconst I64 K = 1;\nU8 f(in U8[2] a, in U8[K] b) { return 2; }',
            'field_before_constant': 'const I64 N = absent;\nstruct R { Missing a; }',
            'signature_before_constant': 'const I64 N = absent;\nI64 f(Missing a) { return 0; }',
            'demanded_constant_cycle': 'struct R { I64[K] a; }\nconst I64 K = L;\nconst I64 L = K;',
        }
        for length in (8, 64):
            body = ['struct R { I64[f0("")] a; }']
            for i in range(length):
                body.append('I64 f%d(in U8[f%d("")] a) { return 0; }' % (i, i + 1))
            body.append('U8 f%d(in U8[_] a) { return 2; }' % length)
            bodies['chain_%d' % length] = '\n'.join(body)
        for name, body in bodies.items():
            with self.subTest(case=name):
                rel = "main.ci"
                (tmp / rel).write_bytes((body + "\nexport I64 run() { return 0; }\n").encode("ascii"))
                want = ref_outcome(tmp / rel, "run", -1, [], path=rel)
                self.assertIsNotNone(want)
                self.assertEqual(want[0], "outcome compile-error")
                output = tmp / (name + ".c")
                rc, stdout, stderr = run_seed([rel, "-o", output, "--root", tmp], tmp)
                found = DIAG_RE.match(stderr.splitlines()[0]) if stderr else None
                self.assertEqual(rc, 2, stderr)
                self.assertEqual(stdout, "")
                self.assertFalse(output.exists())
                self.assertIsNotNone(found, stderr)
                got = ["outcome compile-error", "diagnostic.code " + found["code"],
                       "diagnostic.position %(path)s:%(line)s:%(col)s" % found.groupdict()]
                self.assertEqual(got, want[:3])
                self.assertEqual(diag_lines(stderr)[0], want)

    def test_19g_constant_signature_continuations(self):
        """Retains computed extents across declarations and refuses active signature cycles."""
        cases = [
            {'main.ci': 'struct R { I64[K] a; }\nconst I64 K = L + 1;\nconst I64 L = 1;\n'
             'export I64 run() { R r; r.a[1] = 7; return r.a[1]; }\n'},
            {'main.ci': 'const I64 K = L + 1;\nconst I64 L = 1;\n'
             'I64 take(in U8[K] a, in U8[K] b) { return (a[0] + b[0]) as I64; }\n'
             'export I64 run() { return take("ab", "cd"); }\n'},
            {'main.ci': 'import lib;\nconst I64 K = 1;\n'
             'export I64 run() { U8[K] a; a[0] = 7; return lib.take(a); }\n',
             'lib.ci': 'const I64 K = L + 1;\nconst I64 L = 0;\n'
             'export I64 take(in U8[K] a) { return a[0] as I64; }\n'},
        ]
        chain = ['struct R { I64[K0] a; }']
        chain += ['const I64 K%d = K%d + 1;' % (i, i + 1) for i in range(64)]
        chain += ['const I64 K64 = 1;', 'export I64 run() { R r; r.a[64] = 7; return r.a[64]; }']
        cases.append({'main.ci': '\n'.join(chain) + '\n'})
        for files in cases:
            with self.subTest(source=files['main.ci']):
                tmp = pathlib.Path(tempfile.mkdtemp(dir=STATE['out']))
                for name, source in files.items():
                    (tmp / name).write_bytes(source.encode('ascii'))
                want = ref_outcome(tmp / 'main.ci', 'run', -1, [], path='main.ci')
                self.assertIsNotNone(want)
                self.assertEqual(want[0], 'outcome value')
                self.assertEqual(self.run_inline(files, 'main.ci'), want)
        cycles = [
            'const I64 N = f("");\nI64 f(in U8[N] x) { return 0; }\n',
            'I64 f(in U8[g("")] x) { return 0; }\nI64 g(in U8[f("")] x) { return 0; }\n',
        ]
        for source in cycles:
            with self.subTest(cycle=source):
                tmp = pathlib.Path(tempfile.mkdtemp(dir=STATE['out']))
                (tmp / 'main.ci').write_bytes((source + 'export I64 run() { return 0; }\n').encode('ascii'))
                reference = subprocess.run([sys.executable, '-m', 'cint_ref', 'run', str(tmp / 'main.ci'),
                                            '--entry', 'run'], cwd=ROOT / 'ref', capture_output=True, text=True)
                self.assertEqual(reference.returncode, 0, reference.stderr)
                self.assertIn('outcome refused\n', reference.stdout)
                self.assertIn('a cyclic function-signature dependency', reference.stdout)
                rc, stdout, stderr = run_seed(['main.ci', '-o', tmp / 'out.c', '--root', tmp], tmp)
                self.assertEqual(rc, 2, stderr)
                self.assertEqual(stdout, '')
                self.assertEqual(diag_lines(stderr)[0][1], 'diagnostic.code ' + UNSUPPORTED_CODE)
                self.assertFalse((tmp / 'out.c').exists())

    def test_19h_call_binding_order(self):
        """Binds a call and demands its signature before checking arguments in source order."""
        cases = {'signature_before_argument_name': {'main.ci': 'struct R { I64[f(absent)] a; }\n'
                                                       'I64 f(Missing x) { return 0; }\n'
                                                       'export I64 run() { return 0; }\n'},
         'arity_before_argument_name': {'main.ci': 'struct R { I64[f(absent)] a; }\n'
                                                   'I64 f() { return 0; }\n'
                                                   'export I64 run() { return 0; }\n'},
         'signature_before_argument_call': {'main.ci': 'struct R { I64[f(g())] a; }\n'
                                                       'I64 f(Missing x) { return 0; }\n'
                                                       'I64 g(Absent x) { return 0; }\n'
                                                       'export I64 run() { return 0; }\n'},
         'left_operand_before_call': {'main.ci': 'struct R { I64[absent + f(missing)] a; }\n'
                                                 'I64 f() { return 0; }\n'
                                                 'export I64 run() { return 0; }\n'},
         'left_call_before_right_name': {'main.ci': 'struct R { I64[f(missing) + absent] a; }\n'
                                                    'I64 f() { return 0; }\n'
                                                    'export I64 run() { return 0; }\n'},
         'first_argument_type_before_second': {'main.ci': 'struct R { I64[f(true, absent)] a; }\n'
                                                          'I64 f(I64 x,I64 y) { return x; }\n'
                                                          'export I64 run() { return 0; }\n'},
         'first_argument_width_before_second': {'main.ci': 'struct R { I64[f(256, absent)] a; }\n'
                                                           'I64 f(U8 x,I64 y) { return y; }\n'
                                                           'export I64 run() { return 0; }\n'},
         'first_argument_name_before_second_call': {'main.ci': 'struct R { I64[f(absent, g(missing))] a; }\n'
                                                               'I64 f(I64 x,I64 y) { return x; }\n'
                                                               'I64 g() { return 0; }\n'
                                                               'export I64 run() { return 0; }\n'},
         'first_argument_call_before_second_name': {'main.ci': 'struct R { I64[f(g(missing), absent)] a; }\n'
                                                               'I64 f(I64 x,I64 y) { return x; }\n'
                                                               'I64 g() { return 0; }\n'
                                                               'export I64 run() { return 0; }\n'},
         'callee_before_argument': {'main.ci': 'struct R { I64[missing(absent)] a; }\n'
                                               'export I64 run() { return 0; }\n'},
         'noncallable_before_argument': {'main.ci': 'const I64 f=1;\n'
                                                    'struct R { I64[f(absent)] a; }\n'
                                                    'export I64 run() { return 0; }\n'},
         'arity_before_signature_cycle': {'main.ci': 'I64 f(in U8[f(absent,missing)] a) { return 0; }\n'
                                                     'export I64 run() { return 0; }\n'},
         'signature_constant_before_argument': {'main.ci': 'const I64 K=absent;\n'
                                                           'struct R { I64[f(missing)] a; }\n'
                                                           'I64 f(in U8[K] a) { return 0; }\n'
                                                           'export I64 run() { return 0; }\n'},
         'signature_constant_fault_before_argument': {'main.ci': 'const I64 K=1/0;\n'
                                                                 'struct R { I64[f(missing)] a; }\n'
                                                                 'I64 f(in U8[K] a) { return 0; }\n'
                                                                 'export I64 run() { return 0; }\n'},
         'completed_signature_then_argument': {'main.ci': 'const I64 K=1;\n'
                                                          'struct R { I64[f(missing)] a; }\n'
                                                          'I64 f(in U8[K] a) { return 0; }\n'
                                                          'export I64 run() { return 0; }\n'},
         'pending_named_constructor': {'main.ci': 'struct A { I64 x; I64 y; }\n'
                                                  'struct R { I64[f(A(y=2,x=1),g())] a; }\n'
                                                  'I64 f(A a,I64 b) { return b; }\n'
                                                  'U8 g() { return 0; }\n'
                                                  'export I64 run() { return 0; }\n'},
         'pending_constant_after_constructor': {'main.ci': 'struct A { I64 x; I64 y; }\n'
                                                           'struct R { I64[f(A(y=2,x=1),K)] a; }\n'
                                                           'const I64 K=1;\n'
                                                           'I64 f(A a,I64 b) { return b; }\n'
                                                           'export I64 run() { return 0; }\n'},
         'body_arity_before_argument': {'main.ci': 'I64 f() { return 0; }\nexport I64 run() { return f(absent); }\n'},
         'body_first_argument_type': {'main.ci': 'I64 f(I64 a,I64 b) { return a; }\n'
                                                 'export I64 run() { return f(true,absent); }\n'},
         'body_readonly_before_later_name': {'main.ci': 'I64 f(inout U8[_] a,I64 b) { return b; }\n'
                                                        'I64 read(in U8[_] a) { return f(a,absent); }\n'
                                                        'export I64 run() { U8[1] a; return read(a); }\n'},
         'body_view_type_before_later_name': {'main.ci': 'I64 f(in I64[_] a,I64 b) { return b; }\n'
                                                         'export I64 run() { U8[1] a; return f(a,absent); }\n'},
         'body_nested_arity': {'main.ci': 'I64 f(I64 a,I64 b) { return a; }\n'
                                          'I64 g() { return 0; }\n'
                                          'export I64 run() { return f(g(absent),missing); }\n'},
         'signature_chain_8': {'main.ci': 'struct R { I64[f0(absent)] a; }\n'
                                          'I64 f0(in U8[f1(absent)] a) { return 0; }\n'
                                          'I64 f1(in U8[f2(absent)] a) { return 0; }\n'
                                          'I64 f2(in U8[f3(absent)] a) { return 0; }\n'
                                          'I64 f3(in U8[f4(absent)] a) { return 0; }\n'
                                          'I64 f4(in U8[f5(absent)] a) { return 0; }\n'
                                          'I64 f5(in U8[f6(absent)] a) { return 0; }\n'
                                          'I64 f6(in U8[f7(absent)] a) { return 0; }\n'
                                          'I64 f7(in U8[f8(absent)] a) { return 0; }\n'
                                          'I64 f8(Missing a) { return 0; }\n'
                                          'export I64 run() { return 0; }\n'},
         'signature_chain_64': {'main.ci': 'struct R { I64[f0(absent)] a; }\n'
                                           'I64 f0(in U8[f1(absent)] a) { return 0; }\n'
                                           'I64 f1(in U8[f2(absent)] a) { return 0; }\n'
                                           'I64 f2(in U8[f3(absent)] a) { return 0; }\n'
                                           'I64 f3(in U8[f4(absent)] a) { return 0; }\n'
                                           'I64 f4(in U8[f5(absent)] a) { return 0; }\n'
                                           'I64 f5(in U8[f6(absent)] a) { return 0; }\n'
                                           'I64 f6(in U8[f7(absent)] a) { return 0; }\n'
                                           'I64 f7(in U8[f8(absent)] a) { return 0; }\n'
                                           'I64 f8(in U8[f9(absent)] a) { return 0; }\n'
                                           'I64 f9(in U8[f10(absent)] a) { return 0; }\n'
                                           'I64 f10(in U8[f11(absent)] a) { return 0; }\n'
                                           'I64 f11(in U8[f12(absent)] a) { return 0; }\n'
                                           'I64 f12(in U8[f13(absent)] a) { return 0; }\n'
                                           'I64 f13(in U8[f14(absent)] a) { return 0; }\n'
                                           'I64 f14(in U8[f15(absent)] a) { return 0; }\n'
                                           'I64 f15(in U8[f16(absent)] a) { return 0; }\n'
                                           'I64 f16(in U8[f17(absent)] a) { return 0; }\n'
                                           'I64 f17(in U8[f18(absent)] a) { return 0; }\n'
                                           'I64 f18(in U8[f19(absent)] a) { return 0; }\n'
                                           'I64 f19(in U8[f20(absent)] a) { return 0; }\n'
                                           'I64 f20(in U8[f21(absent)] a) { return 0; }\n'
                                           'I64 f21(in U8[f22(absent)] a) { return 0; }\n'
                                           'I64 f22(in U8[f23(absent)] a) { return 0; }\n'
                                           'I64 f23(in U8[f24(absent)] a) { return 0; }\n'
                                           'I64 f24(in U8[f25(absent)] a) { return 0; }\n'
                                           'I64 f25(in U8[f26(absent)] a) { return 0; }\n'
                                           'I64 f26(in U8[f27(absent)] a) { return 0; }\n'
                                           'I64 f27(in U8[f28(absent)] a) { return 0; }\n'
                                           'I64 f28(in U8[f29(absent)] a) { return 0; }\n'
                                           'I64 f29(in U8[f30(absent)] a) { return 0; }\n'
                                           'I64 f30(in U8[f31(absent)] a) { return 0; }\n'
                                           'I64 f31(in U8[f32(absent)] a) { return 0; }\n'
                                           'I64 f32(in U8[f33(absent)] a) { return 0; }\n'
                                           'I64 f33(in U8[f34(absent)] a) { return 0; }\n'
                                           'I64 f34(in U8[f35(absent)] a) { return 0; }\n'
                                           'I64 f35(in U8[f36(absent)] a) { return 0; }\n'
                                           'I64 f36(in U8[f37(absent)] a) { return 0; }\n'
                                           'I64 f37(in U8[f38(absent)] a) { return 0; }\n'
                                           'I64 f38(in U8[f39(absent)] a) { return 0; }\n'
                                           'I64 f39(in U8[f40(absent)] a) { return 0; }\n'
                                           'I64 f40(in U8[f41(absent)] a) { return 0; }\n'
                                           'I64 f41(in U8[f42(absent)] a) { return 0; }\n'
                                           'I64 f42(in U8[f43(absent)] a) { return 0; }\n'
                                           'I64 f43(in U8[f44(absent)] a) { return 0; }\n'
                                           'I64 f44(in U8[f45(absent)] a) { return 0; }\n'
                                           'I64 f45(in U8[f46(absent)] a) { return 0; }\n'
                                           'I64 f46(in U8[f47(absent)] a) { return 0; }\n'
                                           'I64 f47(in U8[f48(absent)] a) { return 0; }\n'
                                           'I64 f48(in U8[f49(absent)] a) { return 0; }\n'
                                           'I64 f49(in U8[f50(absent)] a) { return 0; }\n'
                                           'I64 f50(in U8[f51(absent)] a) { return 0; }\n'
                                           'I64 f51(in U8[f52(absent)] a) { return 0; }\n'
                                           'I64 f52(in U8[f53(absent)] a) { return 0; }\n'
                                           'I64 f53(in U8[f54(absent)] a) { return 0; }\n'
                                           'I64 f54(in U8[f55(absent)] a) { return 0; }\n'
                                           'I64 f55(in U8[f56(absent)] a) { return 0; }\n'
                                           'I64 f56(in U8[f57(absent)] a) { return 0; }\n'
                                           'I64 f57(in U8[f58(absent)] a) { return 0; }\n'
                                           'I64 f58(in U8[f59(absent)] a) { return 0; }\n'
                                           'I64 f59(in U8[f60(absent)] a) { return 0; }\n'
                                           'I64 f60(in U8[f61(absent)] a) { return 0; }\n'
                                           'I64 f61(in U8[f62(absent)] a) { return 0; }\n'
                                           'I64 f62(in U8[f63(absent)] a) { return 0; }\n'
                                           'I64 f63(in U8[f64(absent)] a) { return 0; }\n'
                                           'I64 f64(Missing a) { return 0; }\n'
                                           'export I64 run() { return 0; }\n'},
         'import_private_before_argument': {'main.ci': 'import lib;\nexport I64 run() { return lib.f(absent); }\n',
                                            'lib.ci': 'I64 f() { return 0; }\n'},
         'import_missing_before_argument': {'main.ci': 'import lib;\n'
                                                       'export I64 run() { return lib.missing(absent); }\n',
                                            'lib.ci': 'export I64 f() { return 0; }\n'},
         'import_arity_before_argument': {'main.ci': 'import lib;\nexport I64 run() { return lib.f(absent); }\n',
                                          'lib.ci': 'export I64 f() { return 0; }\n'}}
        cases.update({'module_ordinary_first': {'lib.ci': 'export I64 x() { return 0; }\n',
                                   'main.ci': 'import lib;\n'
                                              'I64 f(I64 a,I64 b) { return a; }\n'
                                              'export I64 run() { return f(lib,absent); }\n'},
         'module_ordinary_later': {'lib.ci': 'export I64 x() { return 0; }\n',
                                   'main.ci': 'import lib;\n'
                                              'I64 f(I64 a,I64 b) { return a; }\n'
                                              'export I64 run() { return f(absent,lib); }\n'},
         'module_ordinary_earlier_type': {'lib.ci': 'export I64 x() { return 0; }\n',
                                          'main.ci': 'import lib;\n'
                                                     'I64 f(I64 a,I64 b) { return a; }\n'
                                                     'export I64 run() { return f(true,lib); }\n'},
         'module_ordinary_view': {'lib.ci': 'export I64 x() { return 0; }\n',
                                  'main.ci': 'import lib;\n'
                                             'I64 f(in I64[_] a) { return 0; }\n'
                                             'export I64 run() { return f(lib); }\n'}})
        for name, files in cases.items():
            with self.subTest(case=name):
                tmp = pathlib.Path(tempfile.mkdtemp(dir=STATE['out']))
                for rel, source in files.items():
                    (tmp / rel).write_bytes(source.encode('ascii'))
                want = ref_outcome(tmp / 'main.ci', 'run', -1, [], path='main.ci')
                self.assertIsNotNone(want)
                self.assertEqual(want[0], 'outcome compile-error')
                rc, stdout, stderr = run_seed(['main.ci', '-o', tmp / 'out.c', '--root', tmp], tmp)
                self.assertEqual(rc, 2, stderr)
                self.assertEqual(stdout, '')
                self.assertFalse((tmp / 'out.c').exists())
                self.assertEqual(diag_lines(stderr)[0], want)

    def test_19i_call_continuation_values(self):
        """Retains nested call types and completed constructor metadata while resuming inference."""
        cases = [
            {'main.ci': 'struct A { I64 x; I64 y; }\n'
             'I64 add_values(A a,I64 n) { return a.x+a.y+n; }\n'
             'I64 id(I64 x) { return x; }\n'
             'export I64 run() { return add_values(A(y=2,x=1),id(id(4))); }\n'},
            {'main.ci': 'const I64 K=L+1;\nconst I64 L=1;\n'
             'I64 f(in U8[K] a) { return a[1] as I64; }\n'
             'export I64 run() { return f("ab"); }\n'},
            {'main.ci': 'import lib;\nI64 id(I64 x) { return x; }\n'
             'export I64 run() { return id(lib.id(id(7))); }\n',
             'lib.ci': 'export I64 id(I64 x) { return x; }\n'},
        ]
        nested = '7'
        for _ in range(64):
            nested = 'id(' + nested + ')'
        cases.append({'main.ci': 'I64 id(I64 x) { return x; }\n'
                      'export I64 run() { return ' + nested + '; }\n'})
        for files in cases:
            with self.subTest(source=files['main.ci']):
                tmp = pathlib.Path(tempfile.mkdtemp(dir=STATE['out']))
                for rel, source in files.items():
                    (tmp / rel).write_bytes(source.encode('ascii'))
                want = ref_outcome(tmp / 'main.ci', 'run', -1, [], path='main.ci')
                self.assertIsNotNone(want)
                self.assertEqual(want[0], 'outcome value')
                self.assertEqual(self.run_inline(files, 'main.ci'), want)

    def test_19j_array_initializer_profile(self):
        """Refuses the excluded array initializer before resolving its owned extent."""
        fixture = 'view/copy_shape_dynamic_initializer.ci'
        cases = [
            (fixture, {fixture: (CONF / fixture).read_text()}, 8, 'src'),
            ('main.ci', {'main.ci': 'void move[n](in I64[n] src) {\n'
             '    I64[n] dst = src;\n}\n'
             'export I64 run() { I64[2] a; move(a); return 0; }\n'}, 2, 'src'),
            ('main.ci', {'main.ci': 'void move(in I64[2] src) {\n'
             '    I64[2] dst = src;\n}\n'
             'export I64 run() { I64[2] a; move(a); return 0; }\n'}, 2, 'src'),
            ('main.ci', {'main.ci': 'export I64 run() {\n'
             '    I64[0] a; I64[0] b = a; return 0;\n}\n'}, 2, 'a; return'),
            ('main.ci', {'main.ci': 'struct E { I64 value; }\n'
             'void move[n](in E[n] src) {\n    E[n+1] dst = src;\n}\n'
             'export I64 run() { E[2] a; move(a); return 0; }\n'}, 3, 'src'),
            ('main.ci', {'main.ci': 'import lib;\n'
             'void move[n](in lib.E[n] src) {\n    lib.E[n+1] dst = src;\n}\n'
             'export I64 run() { lib.E[2] a; move(a); return 0; }\n',
             'lib.ci': 'export struct E { I64 value; }\n'}, 3, 'src'),
        ]
        for rel, files, line, token in cases:
            with self.subTest(source=files[rel]):
                tmp = pathlib.Path(tempfile.mkdtemp(dir=STATE['out']))
                for name, source in files.items():
                    (tmp / name).parent.mkdir(parents=True, exist_ok=True)
                    (tmp / name).write_bytes(source.encode('ascii'))
                want = ref_outcome(tmp / rel, 'run', -1, [], path=rel)
                self.assertIsNotNone(want)
                self.assertIn(want[0], ('outcome value', 'outcome fault'))
                rc, stdout, stderr = run_seed([rel, '-o', tmp / 'out.c', '--root', tmp], tmp)
                col = files[rel].splitlines()[line - 1].rindex(token) + 1
                self.assertEqual(rc, 2, stderr)
                self.assertEqual(stdout, '')
                self.assertFalse((tmp / 'out.c').exists())
                self.assertEqual(diag_lines(stderr)[0], ['outcome compile-error',
                    'diagnostic.code ' + BOOT_REFUSAL, 'diagnostic.position %s:%d:%d' % (rel, line, col)])
        controls = [
            'export I64 run() { Missing[2] a; return 0; }\n',
            'I64 f(I64 n) { I64[n] a; return 0; }\nexport I64 run() { return f(2); }\n',
            'export I64 run() { U8 n=2; I64[n] a; return 0; }\n',
            'export I64 run() { I64[1/0] a; return 0; }\n',
            'export I64 run() { I64 a=true; return 0; }\n',
            'struct A { I64 v; }\nstruct B { I64 v; }\n'
            'export I64 run() { A a=B(1); return 0; }\n',
        ]
        for source in controls:
            with self.subTest(control=source):
                tmp = pathlib.Path(tempfile.mkdtemp(dir=STATE['out']))
                (tmp / 'main.ci').write_bytes(source.encode('ascii'))
                want = ref_outcome(tmp / 'main.ci', 'run', -1, [], path='main.ci')
                self.assertIsNotNone(want)
                self.assertEqual(want[0], 'outcome compile-error')
                rc, stdout, stderr = run_seed(['main.ci', '-o', tmp / 'out.c', '--root', tmp], tmp)
                self.assertEqual(rc, 2, stderr)
                self.assertEqual(stdout, '')
                self.assertFalse((tmp / 'out.c').exists())
                self.assertEqual(diag_lines(stderr)[0], want)

    def test_19b_seed16_wide_literals(self):
        """SEED-16: C1023 for a radix literal over 4,096 bits; C9102 where the seed would need the value."""
        failures = self.inline_diag("export I64 run() {\n    I64 a = -0x1%s as I64;\n    return 0;\n}\n" % ("0" * 1024),
                                    "C1023", 2, 14)
        failures += self.inline_diag("export I64 run() {\n    I64 a = 0x%s;\n    return 0;\n}\n" % ("F" * 1024),
                                     "C2003", 2, 13)
        failures += self.inline_diag("export I64 run() {\n    I64 a = 1%s;\n    return 0;\n}\n" % ("0" * 1233),
                                     UNSUPPORTED_CODE, 2, 13)
        failures += self.inline_diag("export I64 run() {\n    I8 a = 0x1_0000_0000_0000_0000 as I8;\n    return 0;\n}\n",
                                     UNSUPPORTED_CODE, 2, 36)
        self.assertEqual(failures, [], "\n".join(failures))

    def test_15_crlf_bom_and_final_line(self):

        """Review Focus 1: CR LF and a missing final line feed change no position and no byte."""
        failures = []
        tmp = pathlib.Path(tempfile.mkdtemp(dir=STATE["out"]))
        for rel in ["arith/seed08_runtime_overflow.ci", "determinism/tab_columns.ci",
                    "arith/seed08_const_overflow.ci", "switch/classify_ranges.ci"]:
            data = (CONF / rel).read_bytes()
            outs = []
            for variant, body in (("lf", data), ("crlf", data.replace(b"\n", b"\r\n")),
                                  ("nofinal", data.rstrip(b"\n"))):
                root = tmp / variant
                (root / rel).parent.mkdir(parents=True, exist_ok=True)
                (root / rel).write_bytes(body)
                code, _, err = run_seed([rel, "-o", root / "out.c", "--root", root], tmp)
                c = (root / "out.c").read_bytes() if code == 0 else b""
                outs.append((code, err.splitlines()[0] if err else "", c))
            if not (outs[0][:2] == outs[1][:2] == outs[2][:2] and outs[0][2] == outs[1][2] == outs[2][2]):
                failures.append("%s: LF, CR LF and no-final-LF differ: %r" % (rel, [o[:2] for o in outs]))
        self.assertEqual(failures, [], "\n".join(failures))

    def run_inline(self, files: dict, rel: str, entry="run", fuel=-1, args=()):
        """Compiles files[rel] (with the other files as imports), builds and runs one entry."""
        tmp = pathlib.Path(tempfile.mkdtemp(dir=STATE["out"]))
        for name, text in files.items():
            (tmp / name).parent.mkdir(parents=True, exist_ok=True)
            (tmp / name).write_bytes(text.encode("ascii"))
        code, _, err = run_seed([rel, "-o", tmp / "out.c", "--root", tmp], tmp)
        if code != 0:
            return diag_lines(err)[0] or [err]
        exe, diags = build_program("out", tmp / "out.c")
        if exe is None:
            return ["build: " + diags]
        rc, lines, stderr = run_entry(exe, entry, fuel, list(args))
        if rc != 0 or "runtime error" in stderr or "Sanitizer" in stderr:
            return ["exit %d %s" % (rc, stderr[-1500:])]
        return lines

    def test_16_held_cases_against_cint_ref(self):
        """Boot-1 cases held out of the frozen set: the seed agrees with cint_ref wherever cint_ref decides."""
        failures, compared, skipped = [], 0, []
        for ci in sorted(CONF.glob("*/*.ci")):
            h = header(ci)
            if ci.parent.name == "tables" or not h.get("subset", "").startswith("cint-boot-1"):
                continue
            if list(ci.parent.glob(ci.stem + ".expect")) or list(ci.parent.glob(ci.stem + ".*.expect")):
                continue
            entries, args, fuel = call_spec(h)
            for entry in entries:
                want = ref_outcome(ci, entry, fuel, args)
                if want is None or args:
                    skipped.append("%s (%s)" % (ci.relative_to(CONF).as_posix(), "array arguments" if args else "cint_ref refuses"))
                    continue
                rel = ci.relative_to(CONF).as_posix()
                got = self.run_inline({rel: ci.read_text(encoding="ascii")}, rel, entry, fuel)
                compared += 1
                if got != want:
                    failures.append("%s [%s]\n  seed     %s\n  cint_ref %s" % (rel, entry, got, want))
        print("[seed %s] held cases: %d compared with cint_ref, %d failures; not compared: %s"
              % (STATE["leg"], compared, len(failures), ", ".join(skipped)))
        self.assertEqual(failures, [], "\n".join(failures))

    def test_17_feature_programs(self):
        """cint-boot-1 features no frozen case reaches: scalar programs against cint_ref, the rest by hand."""
        p = "boot/feature.ci"
        scalar = {
            "compound": "export I64 run() {\n    I64 x = 100;\n    x += 5;\n    x -= 3;\n    x *= 2;\n    x /= 7;\n"
                        "    x %= 5;\n    x <<= 3;\n    x >>= 1;\n    x &= 0xFF;\n    x |= 0x100;\n    x ^= 0x3;\n"
                        "    x +%= 1;\n    x -%= 2;\n    x *%= 3;\n    x <<%= 1;\n    x++;\n    x--;\n    x++;\n"
                        "    return x;\n}\n",
            "widths": "export U16 run() {\n    U8 a = 200;\n    U8 b = a +% 100;\n    I16 c = -300;\n"
                      "    U16 d = c as% U16;\n    U32 e = (b as U32) << 3;\n    I8 f = ~(5 as I8);\n"
                      "    U64 g = ~(0 as U64) >> 60;\n    return d -% (e as U16) +% (f as% U16) +% (g as U16);\n}\n",
            "short_circuit_calls": "I64 hits(I64 n) {\n    return n + 1;\n}\nexport Bool run() {\n    I64 z = 0;\n"
                                   "    Bool a = z == 1 && hits(z) > 0;\n    Bool b = z == 0 || hits(z) > 5;\n"
                                   "    Bool c = !(z != 0) && (hits(z) == 1 || z == 9);\n    Bool d = false && hits(z) > 0;\n"
                                   "    Bool e = true || hits(z) > 0;\n    return !a && b && c && !d && e;\n}\n",
            "for_c_continue_break": "export I64 run() {\n    I64 s = 0;\n    for (I64 i = 0; i < 100; i++) {\n"
                                    "        if (i % 3 == 0) {\n            continue;\n        }\n        if (i > 20) {\n"
                                    "            break;\n        }\n        s = s + i;\n    }\n    return s;\n}\n",
            "nested_switch_break": "I64 k(U8 c) {\n    switch (c) {\n        case 'a'..='f':\n            return 1;\n"
                                   "        case '0'..='9', '_':\n            return 2;\n        default:\n            return 3;\n"
                                   "    }\n}\nexport I64 run() {\n    I64 s = 0;\n    I64 i = 0;\n    while (i < 10) {\n"
                                   "        switch (i) {\n            case 5:\n                break;\n            default:\n"
                                   "                s = s + k('b') * 100 + k('7') * 10 + k('z');\n        }\n"
                                   "        i++;\n    }\n    return s;\n}\n",
            "stack_depth_two": "I64 g(I64 x) {\n    I64 z = 0;\n    return x / z;\n}\nI64 f(I64 x) {\n    return g(x);\n}\n"
                               "export I64 run() {\n    return f(7);\n}\n",
            "narrow_u8": "export U8 run() {\n    I32 v = 256;\n    return v as U8;\n}\n",
            "neg_min": "export I32 run() {\n    I32 m = -2147483648;\n    return -m;\n}\n",
            "bool_as_int": "export I64 run() {\n    Bool t = true;\n    return (t as I64) + (false as I64) * 7;\n}\n",
            "shift_u8_count": "export I32 run() {\n    U8 k = 31;\n    I32 one = 1;\n    return one << k;\n}\n",
            "discard_and_void": "I64 inc(I64 v) {\n    return v + 1;\n}\nvoid nothing(I64 v) {\n    _ = inc(v);\n}\n"
                                "export I64 run() {\n    nothing(4);\n    _ = inc(1);\n    return inc(inc(1));\n}\n",
            "const_local_and_module": "const I32 BASE = 1 << 10;\nexport I32 run() {\n    const I32 K = BASE / 4 - 1;\n"
                                      "    I32 v = K;\n    return v *% 3 + BASE;\n}\n",
            "fuel_loop": "export I64 run() {\n    I64 n = 0;\n    while (n < 1000) {\n        n = n + 1;\n    }\n    return n;\n}\n",
        }
        failures = []
        for name, src in scalar.items():
            ci = STATE["out"] / "ref-inputs" / p
            ci.parent.mkdir(parents=True, exist_ok=True)
            ci.write_bytes(src.encode("ascii"))
            fuel = 50 if name == "fuel_loop" else -1
            want = ref_outcome(ci, "run", fuel, [], path=p)
            got = self.run_inline({p: src}, p, "run", fuel)
            if want is None or got != want:
                failures.append("%s\n  seed     %s\n  cint_ref %s" % (name, got, want))
        # Arrays, views, size parameters, strings and imports: cint_ref implements the scalar
        # surface only (ref/OPEN.md I-2), so these values are derived by hand.
        by_hand = [
            ("arrays_views_strings", {p: "I64 total[n](in I64[n] v) {\n    I64 t = 0;\n    for i in 0..n {\n        t = t + v[i];\n"
                                         "    }\n    return t;\n}\nI64 count_x[n](in U8[n] s) {\n    I64 c = 0;\n"
                                         "    for i in 0..n {\n        if (s[i] == 'x') {\n            c++;\n        }\n    }\n"
                                         "    return c;\n}\nexport I64 run() {\n    I64[5] a;\n    for i in 0..5 {\n"
                                         "        a[i] = i * i;\n    }\n    I64 s = 0;\n    for (I64 k = 0; k < 5; k++) {\n"
                                         "        if (k == 1) {\n            continue;\n        }\n        s += a[k];\n    }\n"
                                         "    return s + total(a) + count_x(\"axbxx\");\n}\n"},
             ["outcome value", "stdout-bytes 0", "return I64 62", "fuel-consumed 23"]),
            ("inout_struct_view", {p: "struct Tok {\n    I32 kind;\n    I32 len;\n    U8[3] tag;\n}\n"
                                      "void fill_toks[n](inout Tok[n] ts) {\n    for i in 0..n {\n        ts[i].kind = 1;\n"
                                      "        ts[i].len = (i + 1) as I32;\n        ts[i].tag[2] = 7;\n    }\n}\n"
                                      "I32 first_len(Tok t) {\n    return t.len + (t.tag[2] as I32);\n}\n"
                                      "export I32 run() {\n    Tok[4] ts;\n    fill_toks(ts);\n    I32 s = 0;\n    for i in 0..4 {\n"
                                      "        s += ts[i].len * ts[i].kind;\n    }\n    return s + first_len(ts[0]);\n}\n"},
             ["outcome value", "stdout-bytes 0", "return I32 18", "fuel-consumed 11"]),
            ("bounds_fault", {p: "export I64 run() {\n    I64[3] a;\n    I64 i = 3;\n    return a[i];\n}\n"},
             ["outcome fault", "stdout-bytes 0", "fuel-consumed 1", "fault.code E_BOUNDS",
              "fault.operation index.checked.i64", "fault.operand I64 3", "fault.exact none", "fault.limit I64 3",
              "fault.position boot/feature.ci:4:13", "fault.revision self", "fault.address none", "fault.stack-depth 0"]),
            ("imports", {"app/main.ci": "import lib.limits;\nexport I64 run() {\n    limits.P p = limits.P(y = 2, x = 1);\n"
                                        "    return limits.twice(p.x + p.y) + limits.MAX;\n}\n",
                         "lib/limits.ci": "export const I64 MAX = 10;\nexport struct P {\n    I64 x;\n    I64 y;\n}\n"
                                          "export I64 twice(I64 v) {\n    return v * 2;\n}\nI64 unused() {\n    return 0;\n}\n"},
             ["outcome value", "stdout-bytes 0", "return I64 16", "fuel-consumed 2"]),
            # SEED-OQ-23: a string literal's length was read as a child node, so "" (length 0)
            # named node 0, the module name `k`, and the seed reported C3007 at `k.K`. Fuel: the two
            # calls of h and the call and entry charge of the exported g (CONF-13 X-2).
            ("string_after_module_member", {"app/main.ci": "import lib.k;\nexport I64 g() {\n    return k.K;\n}\n"
                                                           "I64 h[n](in U8[n] s) {\n    return n;\n}\n"
                                                           "export I64 run() {\n    return h(\"\") + h(\"ab\") + g();\n}\n",
                                            "lib/k.ci": "export const I64 K = 7;\n"},
             ["outcome value", "stdout-bytes 0", "return I64 9", "fuel-consumed 4"]),
        ]
        for name, files, want in by_hand:
            rel = "app/main.ci" if "app/main.ci" in files else p
            got = self.run_inline(files, rel)
            if got != want:
                failures.append("%s\n  seed %s\n  want %s" % (name, got, want))
        # Wrappers with view, Bool and struct-element parameters (SPEC-03 A-12): the source builds warning-free.
        entries = ("export struct Pair {\n    I32 a;\n    I32 b;\n}\n"
                   "export I64 total[n](in I64[n] xs, inout I64[n] ys, Bool flip) {\n    I64 s = 0;\n"
                   "    for i in 0..n {\n        s += xs[i];\n        if (flip) {\n            ys[i] = xs[i];\n        }\n"
                   "    }\n    return s;\n}\nexport Bool any_pair[m](in Pair[m] ps, in U8[_] name) {\n"
                   "    return m > 0 && ps[0].a == 1 && name[0] == 'x';\n}\n"
                   "export void touch(inout I32[4] buf) {\n    buf[0] = 1;\n}\n")
        tmp = pathlib.Path(tempfile.mkdtemp(dir=STATE["out"]))
        (tmp / "boot").mkdir()
        (tmp / p).write_bytes(entries.encode("ascii"))
        code, _, err = run_seed([p, "-o", tmp / "entries.c", "--root", tmp], tmp)
        ok, diags = c_compile([tmp / "entries.c"], tmp / ("entries.obj" if STATE["leg"] == "msvc" else "entries.o"),
                              obj_only=True) if code == 0 else (False, err)
        sig = re.search(r"CINT_RT_EXPORT cint_status cx_15_boot_x2Ffeature_5_total\(([^)]*)\)",
                        (tmp / "entries.c").read_text(encoding="ascii") if code == 0 else "")
        want = "cint_ctx *ctx, int64_t fuel, cint_view p_xs, cint_view p_ys, uint8_t p_flip, int64_t *result"
        if not ok or sig is None or sig.group(1) != want:
            failures.append("entries: %s %s" % (diags, sig and sig.group(1)))
        # Long `else if` chains, wide switches and long expressions: no nesting limit (legacy defect).
        n = 400
        arms = "".join("    } else if (x == %d) {\n        return %d;\n" % (i, i * 2) for i in range(1, n))
        chain = "I64 pick(I64 x) {\n    if (x == 0) {\n        return 0;\n" + arms + "    }\n    return -1;\n}\n" \
                "export I64 run() {\n    return pick(%d) + wide(%d) + " % (n - 1, n - 2) + " + ".join(["1"] * 1500) + ";\n}\n" \
                "I64 wide(I64 x) {\n    switch (x) {\n" + "".join("        case %d:\n            return %d;\n" % (i, i)
                                                             for i in range(n)) + "        default:\n            return 0;\n    }\n}\n"
        got = self.run_inline({p: chain}, p)
        want = ["outcome value", "stdout-bytes 0", "return I64 %d" % ((n - 1) * 2 + (n - 2) + 1500), "fuel-consumed 3"]
        if got != want:
            failures.append("long chains\n  seed %s\n  want %s" % (got, want))
        self.assertEqual(failures, [], "\n".join(failures))

    def test_17b_constructor_record_argument_capture(self):
        """Binds record fields after all constructor arguments have been evaluated."""
        rel = "boot/constructor_capture.ci"
        prefix = "struct I { I64 v; }\nstruct O { I item; }\n" \
                 "I64 mutate(inout O[2] a) { a[0].item.v = 9; return 0; }\n"
        cases = {
            "nested_field": "struct Pair { I first; I64 second; }\n" \
                "export I64 run() { O[2] a; a[0].item.v = 1; " \
                "Pair p = Pair(a[0].item, mutate(a)); return p.first.v; }\n",
            "named_field": "struct Pair { I first; I64 second; }\n" \
                "export I64 run() { O[2] a; a[0].item.v = 1; " \
                "Pair p = Pair(first = a[0].item, second = mutate(a)); return p.first.v; }\n",
            "scalar_snapshot": "struct Pair { I64 first; I64 second; }\n" \
                "export I64 run() { O[2] a; a[0].item.v = 1; " \
                "Pair p = Pair(a[0].item.v, mutate(a)); return p.first; }\n",
            "constructed_snapshot": "struct Pair { I first; I64 second; }\n" \
                "export I64 run() { O[2] a; a[0].item.v = 1; " \
                "Pair p = Pair(I(a[0].item.v), mutate(a)); return p.first.v; }\n",
            "early_bounds": "struct Pair { I first; I64 second; }\n" \
                "export I64 run() { O[2] a; I64 idx = 2; " \
                "Pair p = Pair(a[idx].item, mutate(a)); return p.first.v; }\n",
        }
        for name, body in cases.items():
            with self.subTest(case=name):
                src = prefix + body
                ci = STATE["out"] / "ref-inputs" / name / rel
                ci.parent.mkdir(parents=True, exist_ok=True)
                ci.write_bytes(src.encode("ascii"))
                want = ref_outcome(ci, "run", -1, [], path=rel)
                self.assertIsNotNone(want)
                self.assertEqual(self.run_inline({rel: src}, rel), want)

    def test_17c_function_record_argument_capture(self):
        """Binds record arguments after evaluating every argument in source order."""
        rel = "boot/call_capture.ci"
        prefix = "struct I { I64 v; }\nstruct O { I item; }\n" \
                 "I64 mutate(inout O[2] a) { a[0].item.v = 9; return 0; }\n"
        cases = {
            "nested_field": prefix +
                "I64 take(I first, I64 later) { return first.v; }\n"
                "export I64 run() { O[2] a; a[0].item.v = 1; return take(a[0].item, mutate(a)); }\n",
            "scalar_snapshot": prefix +
                "I64 take(I64 first, I64 later) { return first; }\n"
                "export I64 run() { O[2] a; a[0].item.v = 1; return take(a[0].item.v, mutate(a)); }\n",
            "constructed_snapshot": prefix +
                "I64 take(O first, I64 later) { return first.item.v; }\n"
                "export I64 run() { O[2] a; a[0].item.v = 1; return take(O(I(a[0].item.v)), mutate(a)); }\n",
            "early_bounds": prefix +
                "I64 take(I first, I64 later) { return first.v; }\n"
                "export I64 run() { O[2] a; I64 idx = 2; return take(a[idx].item, mutate(a)); }\n",
            "array_element": "struct I { I64 v; }\n"
                "I64 mutate(inout I[2] a) { a[0].v = 9; return 0; }\n"
                "I64 take(I first, I64 later) { return first.v; }\n"
                "export I64 run() { I[2] a; a[0].v = 1; return take(a[0], mutate(a)); }\n",
            "index_snapshot": "struct I { I64 v; }\n"
                "I64 mutate(inout I[2] a, inout I64[1] idx) { a[0].v = 9; idx[0] = 1; return 0; }\n"
                "I64 take(I first, I64 later) { return first.v; }\n"
                "export I64 run() { I[2] a; I64[1] idx; a[0].v = 1; a[1].v = 2; "
                "return take(a[idx[0]], mutate(a, idx)); }\n",
        }
        for name, src in cases.items():
            with self.subTest(case=name):
                ci = STATE["out"] / "ref-inputs" / name / rel
                ci.parent.mkdir(parents=True, exist_ok=True)
                ci.write_bytes(src.encode("ascii"))
                want = ref_outcome(ci, "run", -1, [], path=rel)
                self.assertIsNotNone(want)
                self.assertEqual(self.run_inline({rel: src}, rel), want)

    def test_18_truncated_and_mutated_sources(self):
        """SEED-11: malformed input ends with exit 2 and one positioned diagnostic, never a crash.
        On the gcc and clang legs the seed itself runs under AddressSanitizer and UBSan."""
        tmp = pathlib.Path(tempfile.mkdtemp(dir=STATE["out"]))
        failures, runs = [], 0
        cases = [ci for ci, _ in boot_cases()] + [CONF / "struct" / "array_of_structs.ci"]
        for ci in cases:
            data = ci.read_bytes()
            variants = [data[:len(data) * k // 9] for k in range(1, 9)]
            variants += [data.replace(b"{", b"(", 1), data.replace(b";", b"", 2), data.replace(b"(", b"[", 3),
                         data.replace(b"I64", b"I6", 1), data + b"}\n" * 3, b"(" * 5000, b"-" * 3000 + b"1"]
            for k, body in enumerate(variants):
                rel = "boot/v%d.ci" % k
                (tmp / rel).parent.mkdir(parents=True, exist_ok=True)
                (tmp / rel).write_bytes(body)
                code, _, err = run_seed([rel, "-o", tmp / "out.c", "--root", tmp], tmp)
                runs += 1
                first = err.splitlines()[0] if err else ""
                if code not in (0, 2) or (code == 2 and not DIAG_RE.match(first)) or "Sanitizer" in err \
                        or "runtime error" in err:
                    failures.append("%s variant %d: exit %d: %s" % (ci.name, k, code, err[-600:]))
        print("[seed %s] malformed inputs: %d runs, %d failures" % (STATE["leg"], runs, len(failures)))
        self.assertEqual(failures, [], "\n".join(failures[:10]))

    def test_20_determinism(self):
        """Same bytes from two runs, two working directories and an absolute root."""
        failures, hashes = [], {}
        tmp = pathlib.Path(tempfile.mkdtemp(dir=STATE["out"]))
        cases = [ci for ci, _ in boot_cases()] + sorted(p for p in CONF.glob("tables/*.ci")
                                                        if header(p).get("subset", "").startswith("cint-boot-1"))
        for ci in cases:
            rel = ci.relative_to(CONF).as_posix()
            c1, _, out1 = compile_case(ci, tmp / "a")
            if c1 != 0:
                continue
            c2, _, out2 = compile_case(ci, tmp / "b", root=str(CONF), cwd=tmp)
            b1, b2 = out1.read_bytes(), out2.read_bytes()
            s1, s2 = out1.with_suffix(".sites").read_bytes(), out2.with_suffix(".sites").read_bytes()
            if c2 != 0 or b1 != b2 or s1 != s2:
                failures.append("%s: output depends on the working directory or root" % rel)
            c3, _, out3 = compile_case(ci, tmp / "c")
            if out3.read_bytes() != b1:
                failures.append("%s: two runs differ" % rel)
            problems = check_format(b1)
            for needle in (str(CONF).encode(), str(ROOT).encode(), b"/mnt/", b":\\"):
                if needle in b1 or needle in s1:
                    problems.append("absolute path %r" % needle)
            if problems:
                failures.append("%s: %s" % (rel, ", ".join(problems)))
            hashes[rel] = hashlib.sha256(b1).hexdigest() + " " + hashlib.sha256(s1).hexdigest()
        digest = hashlib.sha256(b"".join(p.read_bytes() for p in sorted(SEED.glob("*.[ch]")))).hexdigest()
        manifest = STATE["out"].parent / ("emitted-%s.json" % STATE["leg"])
        manifest.write_text(json.dumps({"seed_sources_sha256": digest, "programs": hashes}, indent=1, sort_keys=True)
                            + "\n", encoding="ascii", newline="\n")
        print("[seed %s] determinism: %d programs hashed into %s (seed sources %s)"
              % (STATE["leg"], len(hashes), manifest, digest[:16]))
        self.assertEqual(failures, [], "\n".join(failures))
        self.assertGreater(len(hashes), 50)
        # Cross-host identity: compare with the manifests the other legs wrote from the same seed sources.
        compared = 0
        for other in sorted(STATE["out"].parent.glob("emitted-*.json")):
            theirs = json.loads(other.read_text(encoding="ascii"))
            if other == manifest or theirs.get("seed_sources_sha256") != digest:
                continue
            theirs = theirs["programs"]
            diff = sorted(k for k in hashes.keys() | theirs.keys() if hashes.get(k) != theirs.get(k))
            self.assertEqual(diff, [], "emitted C differs from %s" % other.name)
            compared += 1
            print("[seed %s] identical emitted C and site maps to %s (%d programs)" % (STATE["leg"], other.name, len(hashes)))
        if compared == 0:
            print("[seed %s] no other leg has a manifest from these seed sources yet" % STATE["leg"])

    def test_21_symbols_c9002_c5051(self):
        """EMIT-22 (slice 2 patch D-2): 247 bytes is the limit of a generated symbol (C9002 above it),
        and a `cx` or `cm` symbol with `__` is C5051; internal symbols keep the plain rule."""
        fn = "export I64 %s() {\n    return 1;\n}\n"
        # boot/inline.ci: `ci_14_boot_x2Finline` is 20 bytes, and a name of 222 bytes adds 227.
        failures = self.inline_diag(fn % ("a" * 223), "C9002", 1, 12)
        failures += self.inline_diag(fn % "_f", "C5051", 1, 12)
        failures += self.inline_diag(fn % "f", "C5051", 1, 1, path="_p.ci")
        tmp = pathlib.Path(tempfile.mkdtemp(dir=STATE["out"]))
        (tmp / "boot").mkdir()
        src = "I64 _g() {\n    return 2;\n}\nexport I64 %s() {\n    return _g();\n}\n" % ("a" * 222)
        (tmp / "boot" / "inline.ci").write_bytes(src.encode("ascii"))
        rc, _, err = run_seed(["boot/inline.ci", "-o", tmp / "out.c", "--root", tmp], tmp)
        text = (tmp / "out.c").read_text(encoding="ascii") if rc == 0 else ""
        longest = max((len(s) for s in re.findall(r"\b(?:c[gimx]_\w+|cint_\w+)", text)), default=0)
        if rc != 0 or longest != 247:
            failures.append("a 247-byte symbol: exit %d, longest symbol %d: %s" % (rc, longest, err))
        self.assertEqual(failures, [], "\n".join(failures))

    def test_21b_module_paths_c3030(self):
        """SPEC-09 CINTC-12 and SPEC-04 LS-225 (slice 2 patch D-2): every segment of a module path,
        the last without `.ci`, is an identifier and no device name, and no two module paths of
        one program are equal under ASCII case folding; any other path is C3030 at <path>:1:1."""
        tmp = pathlib.Path(tempfile.mkdtemp(dir=STATE["out"]))
        failures = []
        bad = ["a.b.ci", "a-b.ci", "1a.ci", "9.ci", "x/1y.ci", "x/y.z.ci", "x.y/z.ci", "a..b.ci", "a.b.c.ci",
               "a./b.ci", "a/b..ci", ".a.ci", "AUX.ci", "a/NUL.ci", "com1.ci", "x/Lpt9.ci", "a/\u00e9.ci",
               "\u00e9a.ci", "a\u00e9/b.ci"]
        for path in bad:
            rc, _, err = run_seed([path, "-o", tmp / "out.c", "--root", tmp], tmp)
            if rc != 2 or ":1:1: error C3030: " not in err or not err.isascii() or (tmp / "out.c").exists():
                failures.append("%r: exit %d: %r" % (path, rc, err))
        (tmp / "Lib.ci").write_bytes(b"export I64 f() {\n    return 1;\n}\n")
        for name, src, want in (("m.ci", "import Lib;\nimport lib;\n", "lib.ci:1:1: error C3030: "),
                                ("n.ci", "import con;\n", "con.ci:1:1: error C3030: ")):
            (tmp / name).write_bytes(("%sexport I64 g() {\n    return 2;\n}\n" % src).encode("ascii"))
            rc, _, err = run_seed([name, "-o", tmp / "out.c", "--root", tmp], tmp)
            if rc != 2 or not err.startswith(want):
                failures.append("%r: exit %d: %r" % (name, rc, err))
        for path in ("_p.ci", "com0.ci", "con_x.ci", "a/b_1/C9.ci"):
            (tmp / path).parent.mkdir(parents=True, exist_ok=True)
            (tmp / path).write_bytes(b"export I64 g() {\n    return 2;\n}\n")
            rc, _, err = run_seed([path, "-o", tmp / "ok.c", "--root", tmp], tmp)
            if "C3030" in err:
                failures.append("%r refused: %r" % (path, err))
        self.assertEqual(failures, [], "\n".join(failures))

    def test_21c_long_names_in_wrappers(self):
        """A 255-byte `inout` parameter beside a struct of 100 bytes builds (seed review of 2.5):
        the alias check of the `cx` wrapper is not cut at a fixed buffer."""
        tmp = pathlib.Path(tempfile.mkdtemp(dir=STATE["out"]))
        st, pn = "T" * 100, "p" * 255
        src = ("export struct %s {\n    I64 k;\n}\nexport void touch[n](inout %s[n] %s, in I64[n] w) {\n"
               "    for i in 0..n {\n        %s[i].k = w[i];\n    }\n}\n" % (st, st, pn, pn))
        (tmp / "w.ci").write_bytes(src.encode("ascii"))
        rc, _, err = run_seed(["w.ci", "-o", tmp / "w.c", "--root", tmp], tmp)
        self.assertEqual(rc, 0, err)
        self.assertIn("cint_rt_bytes_overlap(q_%s, n_%s * (int64_t)sizeof(ci_1_w_100_%s), q_w, n_w * "
                      "(int64_t)sizeof(int64_t))) {" % (pn, pn, st), (tmp / "w.c").read_text(encoding="ascii"))
        ok, diags = c_compile([tmp / "w.c"], tmp / ("w.obj" if STATE["leg"] == "msvc" else "w.o"), obj_only=True)
        self.assertTrue(ok, diags)

    def test_22_public_wrappers_and_observer(self):
        """D-2 and D-3: `cx` wrappers bind `cint_view` arguments with cint_view_bind (refusals
        first), fault E_SHAPE and E_ALIAS at entry, and accept struct-element views described by
        the `cm` record table; the observer table lists the scalar entries of every module."""
        files = {
            "boot/wrap.ci": "import lib.util;\nexport struct Tok {\n    I32 kind;\n    I32 len;\n    U8[3] tag;\n}\n"
                            "export void touch[n](inout Tok[n] t, in I64[n] w) {\n    for i in 0..n {\n"
                            "        t[i].kind = (w[i] as I32) + 1;\n        t[i].len = t[i].len + 1;\n"
                            "        t[i].tag[2] = 7;\n    }\n}\n"
                            "export I64 total[n](in I64[n] xs, inout I64[n] ys, Bool flip) {\n    I64 s = 0;\n"
                            "    for i in 0..n {\n        s += xs[i];\n        if (flip) {\n            ys[i] = xs[i];\n"
                            "        }\n    }\n    return s;\n}\n"
                            "export void four(inout I32[4] buf) {\n    buf[3] = buf[0] + 1;\n}\n"
                            "export I64 twice(I64 v, Bool neg) {\n    if (neg) {\n        return util.half(v);\n    }\n"
                            "    return v * 2;\n}\n"
                            "export struct Flag {\n    Bool on;\n    U8 pad;\n    Bool[2] more;\n}\n"
                            "export I64 tally[n](in Flag[n] fs) {\n    I64 s = 0;\n    for i in 0..n {\n"
                            "        if (fs[i].on) {\n            s += 1;\n        } else {\n            s += 100;\n"
                            "        }\n        if (fs[i].more[1]) {\n            s += 10000;\n        }\n    }\n"
                            "    return s;\n}\n",
            "lib/util.ci": "export I64 half(I64 v) {\n    return v / 2;\n}\nexport I8 neg8(I8 a) {\n    return -a;\n}\n"
                           "export I64 addv[n](in I64[n] v) {\n    return 0;\n}\nI64 hidden() {\n    return 0;\n}\n",
        }
        tmp = pathlib.Path(tempfile.mkdtemp(dir=STATE["out"]))
        for name, text in files.items():
            (tmp / name).parent.mkdir(parents=True, exist_ok=True)
            (tmp / name).write_bytes(text.encode("ascii"))
        rc, _, err = run_seed(["boot/wrap.ci", "-o", tmp / "wrap.c", "--root", tmp], tmp)
        self.assertEqual(rc, 0, err)
        text = (tmp / "wrap.c").read_text(encoding="ascii")
        exported = sorted(re.findall(r"^CINT_RT_EXPORT (?:const )?\w+ (\w+)", text, re.M))
        self.assertEqual(exported, ["cint_observer_desc", "cint_program_abi", "cm_12_boot_x2Fwrap",
                                    "cx_12_boot_x2Fwrap_4_four", "cx_12_boot_x2Fwrap_5_tally", "cx_12_boot_x2Fwrap_5_total",
                                    "cx_12_boot_x2Fwrap_5_touch", "cx_12_boot_x2Fwrap_5_twice"])
        self.assertNotIn("ci_11_lib_x2Futil_4_addv", text)      # a view entry outside the root: no wrapper, unreached
        self.assertNotIn("ci_11_lib_x2Futil_6_hidden", text)
        self.assertIn("cint_status cx_12_boot_x2Fwrap_5_total(cint_ctx *ctx, int64_t fuel, cint_view p_xs, "
                      "cint_view p_ys, uint8_t p_flip, int64_t *result)", text)
        host = tmp / "host.c"
        host.write_text(CX_HOST, encoding="ascii", newline="\n")
        exe = tmp / ("host.exe" if STATE["leg"] == "msvc" else "host")
        ok, diags = c_compile([tmp / "wrap.c", host], exe, objects=[STATE["rt_obj"]])
        self.assertTrue(ok, diags)
        env = dict(STATE["env"])
        env["ASAN_OPTIONS"] = "detect_leaks=1"
        r = subprocess.run([str(exe)], capture_output=True, text=True, env=env, timeout=120)
        failed = [l for l in r.stdout.splitlines() if not l.endswith(": ok")]
        print("[seed %s] cx wrappers: %s" % (STATE["leg"], r.stdout.splitlines()[-1] if r.stdout else r.stderr[-300:]))
        self.assertEqual((r.returncode, failed, r.stderr), (0, ["cx: 23 of 23 checks passed"], ""))

    def test_30_table_programs(self):
        """Agreement with the CIF-1 fixtures through table programs (I64 boundary matrices; --full: exh8)."""
        tables = sorted(p for p in CONF.glob("tables/*.ci") if header(p).get("subset", "").startswith("cint-boot-1"))
        if not ARGS.full:
            tables = [p for p in tables if p.stem.endswith("_i64")]
        failures, calls = [], 0
        for ci in tables:
            source = re.search(r"^// source: (\S+)$", ci.read_text(encoding="ascii"), re.M).group(1)
            records = [json.loads(l) for l in (CONF / source).read_text(encoding="ascii").splitlines()]
            code, err, out_c = compile_case(ci, STATE["out"] / "tables")
            if code != 0:
                failures.append("%s: seed exit %d %s" % (ci.name, code, err))
                continue
            exe, diags = build_program(ci.stem, out_c)
            if exe is None:
                failures.append("%s: build failed %s" % (ci.name, diags))
                continue
            stdin = (ci.with_suffix(".cases")).read_text(encoding="ascii")
            r = subprocess.run([str(exe), "--cases"], input=stdin, capture_output=True, text=True, env=STATE["env"])
            got = parse_table_output(r.stdout)
            if r.returncode != 0 or len(got) != len(records):
                failures.append("%s: driver exit %d, %d results for %d records %s" % (ci.name, r.returncode, len(got), len(records), r.stderr[-500:]))
                continue
            for rec, g in zip(records, got):
                calls += 1
                want = table_expect(rec)
                if not table_agrees(g, want):
                    failures.append("%s %s: got %s want %s" % (ci.name, rec["id"], g, want))
                    break
        print("[seed %s] table programs: %d programs, %d calls, %d failures" % (STATE["leg"], len(tables), calls, len(failures)))
        self.assertEqual(failures, [], "\n".join(failures))


def parse_table_output(text: str):
    out, cur = [], None
    for line in text.splitlines():
        if line.startswith("value "):
            out.append(("value", line[6:]))
        elif line == "fault":
            cur = ["fault", None, None, None]
            out.append(cur)
        elif line.startswith("fault.code "):
            cur[1] = line[11:]
        elif line.startswith("fault.exact "):
            cur[2] = line[12:]
        elif line.startswith("fault.limit "):
            cur[3] = line[12:]
        elif line.startswith("refused"):
            out.append(("refused", line))
    return [tuple(x) for x in out]


def table_expect(rec):
    """The expected (kind, ...) of a CIF-1 record; None where the record asserts nothing."""
    e = rec["expect"]
    if "value" in e:
        return ("value", "%s %s" % (e["value"]["t"], e["value"]["v"]))
    f = e["fault"]
    shift = f["code"] == "E_SHIFT"   # a negative count asserts only the code (ref/OPEN.md O-2)
    return ("fault", f["code"], f.get("exact", None if shift else "none"),
            f.get("limit", None if shift else "none"))


def table_agrees(got, want) -> bool:
    return len(got) == len(want) and all(w is None or g == w for g, w in zip(got, want))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--leg", choices=["msvc", "gcc", "clang"])
    parser.add_argument("--out")
    parser.add_argument("--full", action="store_true")
    known, rest = parser.parse_known_args()
    ARGS.leg, ARGS.out, ARGS.full = known.leg, known.out, known.full
    unittest.main(argv=[sys.argv[0], "-v", *rest])


if __name__ == "__main__":
    main()
