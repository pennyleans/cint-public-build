"""Benchmark runs: cint programs timed against their baselines, with `perf` receipts.

    python tools/cint_bench.py run WORKLOAD --leg gcc|clang|apple-clang|msvc [--cint EXE]
        [--opt 0|2] [--size full|agreement] [--rounds N] [--warmup N] [--samples N]
        [--only VARIANT ...] [--rustc PATH] [--cpu N] [--out DIR]
    python tools/cint_bench.py list

A workload is bench/workloads/WORKLOAD.json: the cint program, its baselines under bench/baselines/
(or, for the Rust row, bench/cpu_trials/), the parameters, the method, and the bars it is judged
against. Three forms:

- program: each sample runs a built program as a process, timed from outside; the sample's
  digest is the SHA-256 of its stdout (orbit_1e8, B-1);
- call: bench/driver/bench_call.c calls one exported function with I64 arguments in process
  and times each call with a monotonic clock (ledger and transactions, B-6; gravity_pairs, B-3);
- compiler: B1 translating the compiler's own source to C, stage s2 of SPEC-09 11.1 (B-10).

A cint source may import modules of the tree that the manifest lists (`cint.imports`); each is
placed beside the source, and the receipts name its digest.

Every variant is built by one toolchain configuration of tools/cint_check.py (the leg, --opt,
and each cint variant's helper mode); a baseline takes the same flags without the helper define,
plus the flags its manifest names. Before timing, each cint variant is checked against cint_ref
(posture 6.6): through cint-harness for a call, through the program's stdout, fuel and fault
record for a program, at the manifest's agreement parameters, and FIX-01 (S3 equal to S2) for the
compiler. Each check writes a `diff` receipt, whose identity the `perf` receipt names. Every
timed sample must give the same result digest, the integer baselines the cint result, and a
program its expected stdout digest where the manifest records one; otherwise nothing is written.

Variants run in rounds, in an order that rotates each round (SPEC-06 11, alternating), after the
warm-up of each variant. Statistics are integer nanoseconds with nearest-rank percentiles
(SPEC-06 11): a percentile p only when N >= 100 / (100 - p), the mean as the pair (sum, count)
beside the raw samples. Fuel off is a call with no allowance and fuel on a call with a finite
one (2^63 - 1); the runtime counts fuel either way (SPEC-01 IM-137).

The receipt is `cint-receipt-1/perf` (SPEC-06 12.2; posture 6.1): identity and outcome hold no
toolchain, flags, host or time, so one workload at one tree has one receipt identity on every
leg that runs the same variants (SPEC-09 RCPT-03); samples, statistics, the toolchain, the machine
and the bar verdicts are observations. Receipts and work go under <build>/bench/<workload>-<leg>-O<opt>, where <build> is
CINT_BUILD or cint-build in the system temporary directory, unless --out names a directory.
Python standard library only; no floating point. Never run with python -O.
"""
from __future__ import annotations

import argparse
import datetime
import functools
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
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "ref"))
import cint_bootstrap as boot  # noqa: E402
import cint_check as cc  # noqa: E402

WORKLOADS = ROOT / "bench" / "workloads"
DRIVER = ROOT / "bench" / "driver"
FORMS = ("program", "call", "compiler")
LEGS = ("gcc", "clang", "apple-clang", "msvc")
FUEL_ON = 2 ** 63 - 1   # the finite allowance of a "fuel on" variant
STATISTICS = ("median", "mean", "p99")
RELATIONS = ("at_most", "below")
PERF, DIFF = "cint-receipt-1/perf", "cint-receipt-1/diff"
LIMITS = ("Timings are observations of one machine and toolchain; the identity says which computation "
          "was timed, never how fast. A program sample includes process start and exit; a call sample "
          "includes the cx wrapper's entry and exit. Agreement with cint_ref is checked at the "
          "manifest's agreement parameters, and the full-size result against the other variants and the "
          "recorded digest. Fuel on and fuel off both count fuel (SPEC-01 IM-137).")


class BenchError(Exception):
    pass


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


# --------------------------------------------------------------------------- statistics (SPEC-06 11)

def nearest_rank(ordered: list, p: int) -> int:
    """The nearest-rank p-th percentile of ascending samples: rank ceil(p * N / 100)."""
    rank = max(1, (p * len(ordered) + 99) // 100)
    return ordered[rank - 1]


def reportable(p: int, n: int) -> bool:
    """SPEC-06 11: a percentile p is reported only when N >= 100 / (100 - p), rounded up."""
    return n * (100 - p) >= 100


def statistics(samples: list) -> dict:
    """Integer statistics of nanosecond samples; ns values as decimal strings (SPEC-06 12.1)."""
    ordered = sorted(samples)
    n = len(ordered)
    out = {"count": n, "min_ns": str(ordered[0]), "max_ns": str(ordered[-1]), "sum_ns": str(sum(ordered)),
           "samples_ns": [str(s) for s in samples]}
    for p, key in ((50, "median_ns"), (99, "p99_ns")):
        if reportable(p, n):
            out[key] = str(nearest_rank(ordered, p))
    return out


def statistic(stats: dict, which: str):
    """A statistic as an exact fraction (numerator, denominator), or None when not reportable."""
    if which == "mean":
        return int(stats["sum_ns"]), stats["count"]
    key = {"median": "median_ns", "p99": "p99_ns"}[which]
    return (int(stats[key]), 1) if key in stats else None


def evaluate_bar(bar: dict, per_variant: dict, configs: dict, elements=None) -> dict:
    """One bar of the manifest against the measured statistics: the best of its cint variants
    against the fastest of its baselines, a fixed time for the cint variant's configuration
    (`<leg>-<opt>-<helpers>`), or a fixed time per element on every configuration (a sample
    covers `elements` of them), times num / den. The verdict is `met`, `not met` or
    `not evaluated`, with the exact fractions compared."""
    which = bar["statistic"]
    num, den = bar.get("times", [1, 1])
    out = {"id": bar["id"], "status": bar["status"], "statistic": which, "relation": bar["relation"],
           "times": [num, den]}
    cint = [(statistic(per_variant[v], which), v) for v in bar["cint"] if v in per_variant]
    cint = [(s, v) for s, v in cint if s is not None]
    against = bar["against"]
    if "variants" in against:
        base = [(statistic(per_variant[v], which), v) for v in against["variants"] if v in per_variant]
        base = [(s, v) for s, v in base if s is not None]
    elif "ns_per_element" in against:
        ns = against["ns_per_element"]
        out.update({"ns_per_element": ns, "elements": elements})
        base = [((ns * elements, 1), "reference %d ns per element" % ns)]
    else:
        config = configs.get(cint[0][1]) if cint else None
        ns = against["ns_by_config"].get(config)
        base = [((ns, 1), "reference " + config)] if ns is not None else []
    if not cint or not base:
        out["verdict"] = "not evaluated"
        out["reason"] = "no cint variant measured" if not cint else "no baseline measured or reference recorded"
        return out
    best = min(cint, key=lambda sv: fraction_key(sv[0]))
    fastest = min(base, key=lambda sv: fraction_key(sv[0]))
    (a, b), (c, d) = best[0], fastest[0]
    lhs, rhs = a * d * den, c * b * num   # a/b against (c/d) * (num/den)
    ok = lhs <= rhs if bar["relation"] == "at_most" else lhs < rhs
    out.update({"cint_variant": best[1], "cint": [str(a), str(b)], "against_variant": fastest[1],
                "against": [str(c), str(d)], "verdict": "met" if ok else "not met"})
    if "ns_per_element" in against:
        out["cint_per_element"] = [str(a), str(b * elements)]
    return out


fraction_key = functools.cmp_to_key(lambda x, y: (x[0] * y[1] > y[0] * x[1]) - (x[0] * y[1] < y[0] * x[1]))


# --------------------------------------------------------------------------- manifests

def workload_path(name: str) -> pathlib.Path:
    """bench/workloads/<name>.json, or a manifest named by its path inside the checkout (a test's)."""
    if name.endswith(".json"):
        path = pathlib.Path(name).resolve()
        if not path.is_relative_to(ROOT):
            raise BenchError("a workload manifest is inside the checkout: %s" % name)
        return path
    if not re.fullmatch(r"[a-z0-9_]+", name):
        raise BenchError("a workload name is lower case letters, digits and underscores: %r" % name)
    return WORKLOADS / (name + ".json")


def load_workload(name: str) -> dict:
    path = workload_path(name)
    if not path.is_file():
        raise BenchError("no workload %s (%s)" % (name, path.relative_to(ROOT).as_posix()))
    w = json.loads(path.read_text("utf-8"))
    problems = manifest_problems(w, path.stem)
    if problems:
        raise BenchError("%s: %s" % (path.relative_to(ROOT).as_posix(), "; ".join(problems)))
    w["manifest"] = path.relative_to(ROOT).as_posix()
    return w


def template_parts(template: str):
    if template.count("{}") != 1:
        raise BenchError("a line template holds one {}: %r" % template)
    return template.split("{}")


def find_line(text: str, template: str):
    """(line index, value) of the one line of text that is template with an integer in place of
    {}, after leading white space and before any trailing comment; None when not exactly one."""
    head, tail = template_parts(template)
    pattern = re.compile(r"(\s*)" + re.escape(head) + r"(-?[0-9][0-9_]*)" + re.escape(tail) + r"(.*)")
    hits = [(i, m) for i, line in enumerate(text.split("\n")) for m in [pattern.fullmatch(line)] if m]
    if len(hits) != 1:
        return None
    i, m = hits[0]
    return i, int(m.group(2).replace("_", ""))


def substitute(data: bytes, lines: dict, values: dict) -> bytes:
    """The source with each templated parameter line set to values[parameter]."""
    text = data.decode("utf-8")
    rows = text.split("\n")
    for param, template in sorted(lines.items()):
        found = find_line(text, template)
        if found is None:
            raise BenchError("no single line of the form %r" % template)
        i, _ = found
        head, tail = template_parts(template)
        indent = rows[i][:len(rows[i]) - len(rows[i].lstrip())]
        rest = rows[i].lstrip()[len(head):]
        rest = rest[len(re.match(r"-?[0-9][0-9_]*", rest).group(0)) + len(tail):]
        rows[i] = indent + head + str(values[param]) + tail + rest
    return "\n".join(rows).encode("utf-8")


def manifest_problems(w: dict, stem: str) -> list:
    """What is wrong with a workload manifest; an empty list when it is well formed."""
    p = []
    for key in ("benchmark", "name", "form", "summary", "method", "variants", "bars"):
        if key not in w:
            p.append("no %s" % key)
    if p:
        return p
    if w["name"] != stem:
        p.append("name %r is not the file's name" % w["name"])
    if w["form"] not in FORMS:
        p.append("form %r is not one of %s" % (w["form"], ", ".join(FORMS)))
    params = w.get("parameters", {})
    for name, spec in params.items():
        for k in ("value", "agreement"):
            if k in spec and (not isinstance(spec[k], int) or isinstance(spec[k], bool)):
                p.append("parameter %s: %s is not an integer" % (name, k))
    for k in ("rounds", "warmup", "samples"):
        v = w["method"].get(k)
        if not isinstance(v, int) or v < (0 if k == "warmup" else 1):
            p.append("method.%s is not a count" % k)
    names = [v.get("name") for v in w["variants"]]
    if len(set(names)) != len(names):
        p.append("two variants share a name")
    if not any(v.get("role") == "cint" for v in w["variants"]):
        p.append("no cint variant")
    for v in w["variants"]:
        role = v.get("role")
        if role not in ("cint", "baseline"):
            p.append("variant %s: role %r" % (v.get("name"), role))
            continue
        if role == "cint" and v.get("helpers") not in ("portable", "builtin"):
            p.append("variant %s: helpers %r" % (v["name"], v.get("helpers")))
        if role == "cint" and v.get("fuel", "off") not in ("off", "on"):
            p.append("variant %s: fuel %r" % (v["name"], v.get("fuel")))
        if role == "baseline":
            src = ROOT / v.get("source", "")
            if not v.get("source") or not src.is_file():
                p.append("variant %s: no source %r" % (v["name"], v.get("source")))
            elif "lines" in v:
                p += line_problems(src, v["lines"], params, v["name"])
            if w["form"] == "call" and not v.get("entry"):
                p.append("variant %s: no entry" % v["name"])
            for leg in v.get("legs", LEGS):
                if leg not in LEGS:
                    p.append("variant %s: leg %r" % (v["name"], leg))
    if w["form"] != "compiler":
        cint = w.get("cint", {})
        src = ROOT / cint.get("source", "")
        if not cint.get("source") or not src.is_file():
            p.append("no cint source")
        elif "lines" in cint:
            p += line_problems(src, cint["lines"], params, "cint")
        files = [pathlib.PurePosixPath(m).name for m in cint.get("imports", [])]
        for m in cint.get("imports", []):
            if not m.endswith(".ci") or not (ROOT / m).is_file():
                p.append("import %r is not a .ci file of the tree" % m)
        if len(set(files)) != len(files) or pathlib.PurePosixPath(cint.get("source", "")).name in files:
            p.append("two cint modules share a file name")
        if w["form"] == "call":
            if not cint.get("entry"):
                p.append("no cint entry")
            for a in w.get("args", []):
                if a not in params:
                    p.append("argument %s is not a parameter" % a)
            if len(w.get("args", [])) > 4:
                p.append("more than four arguments")
            if "index" in w and w["index"] not in w.get("args", []):
                p.append("index %r is not an argument" % w["index"])
        for size, digest in cint.get("expected_stdout_sha256", {}).items():
            if not re.fullmatch(r"[0-9]+", size) or not re.fullmatch(r"[0-9a-f]{64}", digest):
                p.append("expected_stdout_sha256 %r: %r" % (size, digest))
    if w.get("elements") is not None and w["elements"] not in params:
        p.append("elements %r is not a parameter" % w["elements"])
    for bar in w["bars"]:
        p += bar_problems(bar, set(names))
        if "ns_per_element" in bar.get("against", {}) and w.get("elements") is None:
            p.append("bar %s: a time per element needs the workload's elements" % bar.get("id"))
    for r in w.get("reports", []):
        for k in ("of", "against"):
            if r.get(k) not in names:
                p.append("report %s: %s %r is not a variant" % (r.get("id"), k, r.get(k)))
    return p


def line_problems(src: pathlib.Path, lines: dict, params: dict, who: str) -> list:
    p = []
    text = src.read_bytes().decode("utf-8")
    for param, template in lines.items():
        if param not in params:
            p.append("%s: line for unknown parameter %s" % (who, param))
            continue
        found = find_line(text, template)
        if found is None:
            p.append("%s: no single line of the form %r in %s" % (who, template, src.name))
        elif found[1] != params[param]["value"]:
            p.append("%s: %s holds %s = %d, not the parameter's value %d" % (
                who, src.name, param, found[1], params[param]["value"]))
    return p


def bar_problems(bar: dict, names: set) -> list:
    p = []
    for k in ("id", "status", "statistic", "relation", "cint", "against"):
        if k not in bar:
            p.append("bar %s: no %s" % (bar.get("id"), k))
    if p:
        return p
    if bar["statistic"] not in STATISTICS:
        p.append("bar %s: statistic %r" % (bar["id"], bar["statistic"]))
    if bar["relation"] not in RELATIONS:
        p.append("bar %s: relation %r" % (bar["id"], bar["relation"]))
    times = bar.get("times", [1, 1])
    if len(times) != 2 or not all(isinstance(t, int) and t >= 1 for t in times):
        p.append("bar %s: times is not a pair of positive integers" % bar["id"])
    if not isinstance(bar["cint"], list) or not bar["cint"] or not set(bar["cint"]) <= names:
        p.append("bar %s: cint names no variants" % bar["id"])
    a = bar["against"]
    if "variants" in a:
        if not a["variants"] or not set(a["variants"]) <= names:
            p.append("bar %s: against names an unknown variant" % bar["id"])
    elif "ns_by_config" in a:
        if not all(isinstance(v, int) and v > 0 for v in a["ns_by_config"].values()):
            p.append("bar %s: ns_by_config holds a value that is not a positive integer" % bar["id"])
    elif "ns_per_element" in a:
        v = a["ns_per_element"]
        if not isinstance(v, int) or isinstance(v, bool) or v <= 0:
            p.append("bar %s: ns_per_element is not a positive integer" % bar["id"])
    else:
        p.append("bar %s: against names neither variants, ns_by_config nor ns_per_element" % bar["id"])
    return p


def values_for(w: dict, size: str) -> dict:
    """The parameter values of a run at `full` size or at the `agreement` size."""
    params = w.get("parameters", {})
    if size == "full":
        return {k: v["value"] for k, v in params.items()}
    return {k: v.get("agreement", v["value"]) for k, v in params.items()}


# --------------------------------------------------------------------------- toolchains and builds

def toolchain(leg: str, opt: str, helpers: str) -> cc.Toolchain:
    try:
        return cc.Toolchain(leg, opt, helpers, False)
    except (OSError, subprocess.CalledProcessError, cc.BuildError) as e:
        raise BenchError("the %s toolchain is not available: %s" % (leg, e)) from e


def baseline_flags(tc: cc.Toolchain, variant: dict) -> list:
    """The cint variant's flags without the helper define, plus the baseline's own (posture 6.2)."""
    flags = [f for f in tc.flags if f not in ("-D" + tc.define, "/D" + tc.define)]
    if tc.leg != "msvc":
        flags += variant.get("gnu_flags", [])
    return flags


def compile_c(tc: cc.Toolchain, src: pathlib.Path, obj_dir: pathlib.Path, what: str, flags=None,
              defines=(), includes=()) -> pathlib.Path:
    """One C file to one object with the leg's compiler; include directories come before the source
    (MSVC D9026)."""
    flags = tc.flags if flags is None else flags
    obj_dir.mkdir(parents=True, exist_ok=True)
    obj = obj_dir / (src.stem + tc.obj)
    inc = [cc.RT, *includes]
    if tc.leg == "msvc":
        cmd = [tc.cc, *flags, *("/D" + d for d in defines), *("/I" + str(i) for i in inc), "/c", str(src),
               "/Fo" + str(obj)]
    else:
        cmd = [tc.cc, *flags, *("-D" + d for d in defines), *("-I" + str(i) for i in inc), "-c", str(src),
               "-o", str(obj)]
    tc.run(cmd, obj_dir, "%s: compile %s" % (what, src.name))
    return obj


def link_exe(tc: cc.Toolchain, objs: list, out: pathlib.Path, what: str, libs=()) -> pathlib.Path:
    exe = pathlib.Path(str(out) + tc.exe)
    if tc.leg == "msvc":
        libs_in = [x for x in libs if not x.startswith("/")]
        options = [x for x in libs if x.startswith("/")]
        cmd = [tc.cc, "/nologo", *map(str, objs), *libs_in, "/Fe" + str(exe)] + (["/link", *options] if options else [])
    else:
        cmd = [tc.cc, *map(str, objs), "-o", str(exe), *libs] + (["-ldl"] if sys.platform != "darwin" else [])
    tc.run(cmd, out.parent, "%s: link" % what)
    return exe


def place_imports(imports: list, directory: pathlib.Path):
    """The modules a cint source imports (the manifest's `cint.imports`), copied into its directory."""
    directory.mkdir(parents=True, exist_ok=True)
    for m in imports:
        (directory / pathlib.PurePosixPath(m).name).write_bytes((ROOT / m).read_bytes())


def emit(cint: pathlib.Path, source: bytes, name: str, work: pathlib.Path, imports=()):
    """`cint emit-c` of one module and the modules it imports, each after its imports (slice 2
    D-13): (its C files, the SHA-256 of its MANIFEST, the build identity)."""
    src_dir = work / "src"
    place_imports(imports, src_dir)
    (src_dir / name).write_bytes(source)
    out = work / "emit"
    r = subprocess.run([str(cint), "emit-c", "--root", str(src_dir), "--out", str(out),
                        *cc.module_order(src_dir, name)], cwd=work, capture_output=True)
    if r.returncode != 0:
        raise BenchError("cint emit-c %s: exit %d\n%s" % (name, r.returncode, (r.stdout + r.stderr).decode(
            "utf-8", "replace")[-2000:]))
    ref = (out / "MANIFEST.ref").read_bytes().decode("ascii").split()
    stage = work / ref[0]
    manifest = (stage / "MANIFEST").read_bytes()
    if sha256(manifest) != ref[1]:
        raise BenchError("cint emit-c: MANIFEST.ref does not name the MANIFEST")
    return sorted(stage.glob("*.c"), key=lambda p: p.name.encode("utf-8")), sha256(manifest)


def compile_program(tc: cc.Toolchain, c_files: list, rt_obj: pathlib.Path, obj_dir: pathlib.Path, what: str) -> list:
    """The emitted C files, each in an object directory of its own (equal stems cannot collide)."""
    return [compile_c(tc, f, obj_dir / str(k), what) for k, f in enumerate(c_files)] + [rt_obj]


def rust_build(rustc: str, src: pathlib.Path, out_dir: pathlib.Path, msvc: bool):
    """The Rust baseline as a static library: (its path, the native libraries it needs, rustc's
    version line)."""
    out_dir.mkdir(parents=True, exist_ok=True)
    lib = out_dir / ("bench_rust.lib" if msvc else "libbench_rust.a")
    cmd = [rustc, "--edition", "2021", "--crate-type", "staticlib", "--crate-name", "bench_rust",
           "-C", "opt-level=2", "-C", "overflow-checks=on", "--print", "native-static-libs", "-o", str(lib), str(src)]
    r = subprocess.run(cmd, cwd=out_dir, capture_output=True, text=True, errors="replace")
    if r.returncode != 0:
        raise BenchError("rustc: exit %d\n%s" % (r.returncode, r.stderr[-2000:]))
    native = native_libs(r.stderr + r.stdout, sys.platform == "darwin")
    version = subprocess.run([rustc, "--version"], capture_output=True, text=True).stdout.strip()
    flags = ["--edition 2021", "--crate-type staticlib", "-C opt-level=2", "-C overflow-checks=on"]
    return lib, native, {"compiler": "rustc", "version": version, "flags": " ".join(flags)}


def native_libs(output: str, darwin: bool) -> list:
    """The libraries rustc's `--print native-static-libs` names, less those the C driver links by
    itself on macOS: libSystem, which also holds libc and libm. Apple ld warns about a second
    -lSystem, and any linker output fails a build."""
    m = re.search(r"native-static-libs: (.*)", output)
    libs = m.group(1).split() if m else []
    return [x for x in libs if not (darwin and x in ("-lSystem", "-lc", "-lm"))]


# --------------------------------------------------------------------------- runs

def ref_record(source: pathlib.Path, entry=None, args=()) -> tuple:
    """cint_ref's record of one run (the .expect lines from `outcome` on) and its stdout bytes."""
    stdout = source.with_suffix(".ref-stdout")
    cmd = [sys.executable, "-m", "cint_ref", "run", str(source), "--stdout", str(stdout)]
    if entry is not None:
        cmd += ["--entry", entry] + [x for a in args for x in ("--arg", "I64 %d" % a)]
    r = subprocess.run(cmd, cwd=ROOT / "ref", capture_output=True)
    if r.returncode != 0:
        raise BenchError("cint_ref run %s: exit %d\n%s" % (source.name, r.returncode, r.stderr.decode(
            "utf-8", "replace")[-2000:]))
    return from_outcome(r.stdout.decode("ascii").split("\n")), stdout.read_bytes() if stdout.is_file() else b""


def from_outcome(lines: list) -> list:
    for i, line in enumerate(lines):
        if line.startswith("outcome "):
            return [x for x in lines[i:] if x]
    return [x for x in lines if x]


def harness_record(harness: pathlib.Path, lib: pathlib.Path, module: str, entry: str, args: list, env: dict) -> list:
    """cint-harness's record of one call of the root module's export, named through a one-line case
    list (CONF-12), which finds the module by name when the program has imports."""
    cases = lib.parent / "cases.txt"
    cases.write_bytes((" ".join([module, entry] + ["I64 %d" % a for a in args]) + "\n").encode("ascii"))
    cmd = [str(harness), str(lib), "--cases", str(cases), "--clause", "bench"]
    r = subprocess.run(cmd, cwd=lib.parent, capture_output=True, env=env)
    if r.returncode != 0:
        return ["harness exit %d" % r.returncode] + r.stderr.decode("utf-8", "replace").splitlines()[:5]
    return from_outcome(r.stdout.decode("ascii").replace("\r\n", "\n").split("\n"))


def run_program(exe: pathlib.Path, work: pathlib.Path, env: dict, pin) -> dict:
    """One run of a cint program built with its C `main`: its record (cint_check.entry_record),
    stdout, and wall time in ns, timed around the process with a monotonic clock."""
    fault, fuel = work / "fault.bin", work / "fuel.bin"
    for f in (fault, fuel):
        f.unlink(missing_ok=True)
    t0 = time.perf_counter_ns()
    r = subprocess.run([str(exe), str(fault), str(fuel)], cwd=work, env=env, capture_output=True,
                       stdin=subprocess.DEVNULL, preexec_fn=pin)
    ns = time.perf_counter_ns() - t0
    return {"ns": ns, "record": cc.entry_record(r, fault, fuel), "stdout": r.stdout, "exit": r.returncode}


def run_baseline_program(exe: pathlib.Path, work: pathlib.Path, env: dict, pin) -> dict:
    t0 = time.perf_counter_ns()
    r = subprocess.run([str(exe)], cwd=work, env=env, capture_output=True, stdin=subprocess.DEVNULL, preexec_fn=pin)
    ns = time.perf_counter_ns() - t0
    return {"ns": ns, "stdout": r.stdout, "exit": r.returncode}


def parse_driver(text: str) -> dict:
    """bench-call's output: the timer and each sample (ns, status, result, fuel)."""
    lines = [x for x in text.replace("\r\n", "\n").split("\n") if x]
    if not lines or lines[0] != "bench-call 1" or not lines[1].startswith("timer "):
        raise BenchError("bench-call printed no header")
    kind, num, den = lines[1].split()[1:]
    samples = []
    for line in lines[2:]:
        parts = line.split()
        if parts[0] == "sample":
            samples.append(tuple(int(x) for x in parts[1:5]))
        elif parts[0] == "end":
            if int(parts[1]) != len(samples):
                raise BenchError("bench-call's end line does not count its samples")
            return {"timer": {"kind": kind, "num": int(num), "den": int(den)}, "samples": samples}
    raise BenchError("bench-call printed no end line")


def run_driver(exe: pathlib.Path, entry: str, args: list, fuel, warmup: int, samples: int, env: dict, pin,
               index=None) -> dict:
    cmd = [str(exe), "--entry", entry, "--warmup", str(warmup), "--samples", str(samples)]
    cmd += [x for a in args for x in ("--arg", str(a))]
    if index is not None:
        cmd += ["--index", str(index)]
    if fuel is not None:
        cmd += ["--fuel", str(fuel)]
    r = subprocess.run(cmd, cwd=exe.parent, env=env, capture_output=True, stdin=subprocess.DEVNULL, preexec_fn=pin)
    out = r.stdout.decode("ascii", "replace")
    if r.returncode != 0:
        raise BenchError("bench-call %s: exit %d\n%s%s" % (entry, r.returncode, out[-500:],
                                                         r.stderr.decode("utf-8", "replace")[-1000:]))
    return parse_driver(out)


def stream_digest(samples: list, first: int) -> dict:
    """The digest of a per-iteration run: the calls it covers, the SHA-256 of its results in call
    order (decimal, space separated), and of its fuel counts the same way when it counts fuel."""
    out = {"calls": "%d to %d" % (first, first + len(samples) - 1),
           "result_stream_sha256": sha256(" ".join(str(s[2]) for s in samples).encode("ascii"))}
    if all(s[3] >= 0 for s in samples):
        out["fuel_stream_sha256"] = sha256(" ".join(str(s[3]) for s in samples).encode("ascii"))
    return out


def same_result(cint: dict, other: dict) -> bool:
    """Whether two result digests agree on everything but fuel, which only a cint variant counts."""
    def strip(d):
        return {k: v for k, v in d.items() if not k.startswith("fuel")}
    return strip(cint) == strip(other)


def rounds_order(names: list, rounds: int) -> list:
    """The variants of each round, the list rotated by the round's index (alternating order)."""
    n = len(names)
    return [[names[(r + i) % n] for i in range(n)] for r in range(rounds)]


# --------------------------------------------------------------------------- receipts

def write_receipt(path: pathlib.Path, identity: dict, outcome: dict, observations: dict) -> str:
    """Canonical JSON (SPEC-09 RCPT-02) with its identity digest among the observations."""
    ident = sha256(cc.canonical({"identity": identity, "outcome": outcome}))
    body = {"identity": identity, "outcome": outcome,
            "observations": dict(observations, receipt_identity_sha256=ident)}
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(cc.canonical(body) + b"\n")
    return ident


def machine() -> dict:
    """The machine as SPEC-06 12.2 lists it, with no host name or user path."""
    cpu, memory = platform.processor() or "", None
    try:
        if sys.platform.startswith("linux"):
            info = pathlib.Path("/proc/cpuinfo").read_text("utf-8", "replace")
            m = re.search(r"^model name\s*:\s*(.+)$", info, re.M)
            cpu = m.group(1).strip() if m else cpu
            m = re.search(r"^MemTotal:\s*(\d+) kB", pathlib.Path("/proc/meminfo").read_text("ascii"), re.M)
            memory = int(m.group(1)) * 1024 if m else None
        elif sys.platform == "darwin":
            cpu = subprocess.run(["sysctl", "-n", "machdep.cpu.brand_string"], capture_output=True,
                                 text=True).stdout.strip() or cpu
            memory = int(subprocess.run(["sysctl", "-n", "hw.memsize"], capture_output=True, text=True).stdout)
        elif os.name == "nt":
            r = subprocess.run(["powershell", "-NoProfile", "-Command",
                                "(Get-CimInstance Win32_Processor).Name; "
                                "(Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory"],
                               capture_output=True, text=True, timeout=60)
            rows = [x.strip() for x in r.stdout.splitlines() if x.strip()]
            cpu, memory = (rows[0], int(rows[-1])) if len(rows) >= 2 else (cpu, None)
    except (OSError, ValueError, subprocess.SubprocessError):
        pass
    return {"arch": platform.machine(), "cores": os.cpu_count() or 0, "cpu": cpu,
            "memory_bytes": str(memory) if memory is not None else None, "os": cc.host_os()}


def source_identity(paths: list) -> dict:
    """SPEC-06 12.2 `source`: the revision, whether tracked files differ from it, and each input."""
    def git(*args):
        return subprocess.run(["git", "-c", "safe.directory=*", *args], cwd=ROOT, capture_output=True,
                              text=True).stdout.strip()
    files = {}
    for p in paths:
        rel = p.resolve().relative_to(ROOT).as_posix()
        files[rel] = sha256(p.read_bytes())
    return {"revision": git("rev-parse", "HEAD") or None,
            "dirty": bool(git("status", "--porcelain", "--untracked-files=no")), "files": files}


def compiler_identity(cint) -> str:
    """The compiler source identity of this checkout (tools/cint_bootstrap.py); with a `cint`, also
    that of the bootstrap receipt beside it, which must be the same."""
    sources = {}
    for p in sorted((ROOT / "compiler").glob("*.ci"), key=lambda p: p.name.encode("utf-8")):
        sources["compiler/" + p.name] = p.read_bytes()
    here = sha256(boot.source_manifest(boot.compiler_closure(sources)))
    if cint is not None:
        receipt = pathlib.Path(cint).resolve().parent / "bootstrap.json"
        if not receipt.is_file():
            raise BenchError("no bootstrap.json beside %s: give a cint that tools/cint_bootstrap.py built" % cint)
        theirs = json.loads(receipt.read_text("ascii"))["identity"]["compiler_source_identity"]
        if theirs != here:
            raise BenchError("%s was bootstrapped from other compiler sources (%s, this checkout %s); "
                             "bootstrap it from this checkout" % (cint, theirs[:12], here[:12]))
    return here


def utc_now() -> str:
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


# --------------------------------------------------------------------------- the run

class Run:
    """One workload at one size on one leg and optimization level."""

    def __init__(self, w: dict, ns: argparse.Namespace):
        self.w, self.ns = w, ns
        self.leg, self.opt, self.size = ns.leg, ns.opt, ns.size
        self.values = values_for(w, self.size)
        self.agree_values = values_for(w, "agreement")
        self.method = {k: getattr(ns, k) if getattr(ns, k) is not None else w["method"][k]
                       for k in ("rounds", "warmup", "samples")}
        self.tag = "%s-%s-O%s" % (w["name"], self.leg, self.opt)
        base = pathlib.Path(ns.out) if ns.out else cc.build_root() / "bench" / self.tag
        self.receipts = base / "receipts"
        self.work = base / "work"
        self.tcs, self.rt_objs, self.observed_builds, self.harnesses = {}, {}, {}, {}
        self.skipped, self.inputs = {}, [ROOT / w["manifest"], pathlib.Path(__file__)]
        self.env = None
        self.pin = None
        if ns.cpu is not None:
            if not hasattr(os, "sched_setaffinity"):
                raise BenchError("--cpu needs Linux")
            cpu = ns.cpu
            self.pin = lambda: os.sched_setaffinity(0, {cpu})

    # ---- selection and setup
    def variants(self) -> list:
        chosen = []
        for v in self.w["variants"]:
            if self.ns.only and v["name"] not in self.ns.only:
                continue
            if self.leg not in v.get("legs", LEGS):
                self.skipped[v["name"]] = "not built on the %s leg" % self.leg
                continue
            if v.get("language") == "Rust":
                rustc = self.ns.rustc or shutil.which("rustc") or str(pathlib.Path.home() / ".cargo" / "bin" / "rustc")
                if not pathlib.Path(rustc).is_file() and shutil.which(rustc) is None:
                    self.skipped[v["name"]] = "no rustc on this machine"
                    continue
                self.rustc = rustc
            chosen.append(v)
        if not any(v["role"] == "cint" for v in chosen):
            raise BenchError("no cint variant selected")
        return chosen

    def tc(self, helpers: str) -> cc.Toolchain:
        if helpers not in self.tcs:
            tc = toolchain(self.leg, self.opt, helpers)
            self.tcs[helpers] = tc
            self.env = tc.env
            self.rt_objs[helpers] = compile_c(tc, cc.RT / "cint_rt.c", self.work / ("rt-" + helpers), "runtime")
        return self.tcs[helpers]

    def prepare(self):
        if self.work.exists():
            shutil.rmtree(self.work)
        self.work.mkdir(parents=True)
        if self.receipts.exists() and any(self.receipts.iterdir()):
            raise BenchError("%s already holds receipts; give another --out or remove them" % self.receipts)
        self.inputs += [cc.RT / "cint_rt.c", cc.RT / "cint_rt.h", cc.RT / "cint_rt_host.h"]

    # ---- identities
    def variant_identity(self, v: dict, build=None) -> dict:
        if v["role"] == "cint":
            out = {"backend": "cpu-c17", "build": build, "helpers": v["helpers"], "role": "cint"}
            if self.w["form"] != "compiler":
                fuel = v.get("fuel", "off")
                out["fuel"] = {"model": "fuel-v1", "allowance": "none" if fuel == "off" else str(FUEL_ON)}
            return dict(out, name=v["name"])
        return {"language": v["language"], "name": v["name"], "number_format": v.get("number_format"),
                "role": "baseline", "source": v["source"], "sha256": sha256((ROOT / v["source"]).read_bytes())}

    def toolchain_observation(self) -> dict:
        tc = next(iter(self.tcs.values()))
        out = {"cc": tc.leg, "version": tc.version, "opt": self.opt, **getattr(tc, "apple", {})}
        out["flags"] = {h: " ".join(t.flags) for h, t in sorted(self.tcs.items())}
        return out

    def diff_receipt(self, v: dict, build: str, reference: list, candidate: list, ref_stdout: bytes,
                     cand_stdout, values: dict, extra=None) -> tuple:
        """The agreement receipt of one cint variant (posture 6.6): kind `diff`, cint_ref against the
        variant's emitted C on cpu-c17, record for record."""
        agree = reference == candidate and (cand_stdout is None or cand_stdout == ref_stdout)
        identity = {"schema": DIFF, "kind": "diff", "profile": "cint-core-1",
                    "workload": {"name": self.w["name"], "form": self.w["form"], "parameters": values,
                                 **({"entry": self.w["cint"]["entry"]} if self.w["form"] == "call" else {})},
                    "reference": {"backend": "cint_ref", "source": cc.tree_digest("ref/cint_ref", "ref/")[0]},
                    "candidate": {"backend": "cpu-c17", "build": build, "helpers": v["helpers"],
                                  "harness": sha256((ROOT / "harness" / "cint_harness.c").read_bytes())
                                  if self.w["form"] == "call" else None},
                    "source": source_identity([ROOT / self.w["cint"]["source"]] + self.imports())}
        outcome = {"outcome": "agree" if agree else "differ", "record_sha256": sha256("\n".join(reference).encode()),
                   **(extra or {})}
        observations = {"reference_record": reference, "candidate_record": candidate,
                        "toolchain": self.toolchain_observation(), "machine": machine(),
                        "finished_utc": utc_now(), "limits": "Agreement at the parameters named in the identity."}
        path = self.receipts / ("agree-%s-%s.json" % (self.tag, v["name"]))
        return write_receipt(path, identity, outcome, observations), agree, path

    # ---- forms
    def build_program(self, v: dict, values: dict, work: pathlib.Path):
        """(executable, build identity) of one variant of a program workload at these values."""
        if v["role"] == "cint":
            tc = self.tc(v["helpers"])
            src = substitute((ROOT / self.w["cint"]["source"]).read_bytes(), self.w["cint"].get("lines", {}), values)
            c_files, build = emit(self.cint, src, pathlib.Path(self.w["cint"]["source"]).name, work,
                                  self.w["cint"].get("imports", []))
            objs = compile_program(tc, c_files, self.rt_objs[v["helpers"]], work / "obj", v["name"])
            return link_exe(tc, objs, work / "program", v["name"]), build
        tc = self.tc("builtin")
        src = substitute((ROOT / v["source"]).read_bytes(), v.get("lines", {}), values)
        path = work / pathlib.Path(v["source"]).name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(src)
        obj = compile_c(tc, path, work / "obj", v["name"], flags=baseline_flags(tc, v))
        self.observed_builds[v["name"]] = {"compiler": tc.leg, "version": tc.version,
                                           "flags": " ".join(baseline_flags(tc, v) + v.get("gnu_libs", []))}
        libs = v.get("gnu_libs", []) if tc.leg != "msvc" else []
        return link_exe(tc, [obj], work / "baseline", v["name"], libs), None

    def build_call(self, v: dict, work: pathlib.Path):
        """(bench-call executable, build identity, shared library for the harness or None)."""
        nargs = len(self.w.get("args", []))
        if v["role"] == "cint":
            tc = self.tc(v["helpers"])
            src = (ROOT / self.w["cint"]["source"]).read_bytes()
            c_files, build = emit(self.cint, src, pathlib.Path(self.w["cint"]["source"]).name, work,
                                  self.w["cint"].get("imports", []))
            objs = compile_program(tc, c_files, self.rt_objs[v["helpers"]], work / "obj", v["name"])
            lib = tc.link_shared(objs, work / "program", v["name"])
            drv = [compile_c(tc, DRIVER / n, work / "drv", v["name"], includes=[DRIVER])
                   for n in ("bench_call.c", "target_cint.c")]
            return link_exe(tc, drv + objs, work / "bench-call", v["name"]), build, lib
        tc = self.tc("builtin")
        flags = baseline_flags(tc, v)
        defines = ["BENCH_FN=" + v["entry"], "BENCH_ARITY=%d" % nargs]
        libs = []
        if v.get("language") == "Rust":
            lib, native, observed = rust_build(self.rustc, ROOT / v["source"], work / "rust", tc.leg == "msvc")
            self.observed_builds[v["name"]] = observed
            objs, libs = [lib], native
            if tc.leg == "msvc":
                flags = flags + ["/MD"]   # the C runtime the Rust standard library links
        else:
            objs = [compile_c(tc, ROOT / v["source"], work / "obj", v["name"], flags=flags)]
            self.observed_builds[v["name"]] = {"compiler": tc.leg, "version": tc.version, "flags": " ".join(flags)}
        drv = [compile_c(tc, DRIVER / "bench_call.c", work / "drv", v["name"], flags=flags, includes=[DRIVER]),
               compile_c(tc, DRIVER / "target_c.c", work / "drv", v["name"], flags=flags, defines=defines,
                         includes=[DRIVER])]
        return link_exe(tc, drv + objs, work / "bench-call", v["name"], libs), None, None

    def harness(self, helpers: str) -> pathlib.Path:
        if helpers not in self.harnesses:
            tc = self.tc(helpers)
            obj = compile_c(tc, ROOT / "harness" / "cint_harness.c", self.work / ("harness-" + helpers), "harness")
            self.harnesses[helpers] = link_exe(tc, [obj, self.rt_objs[helpers]],
                                               self.work / ("harness-" + helpers) / "cint-harness", "harness")
        return self.harnesses[helpers]

    def fuel_of(self, v: dict):
        if v["role"] != "cint":
            return None
        return FUEL_ON if v.get("fuel", "off") == "on" else -1

    # ---- the run
    def execute(self) -> int:
        w, form = self.w, self.w["form"]
        self.cint = None
        if form != "compiler":
            if not self.ns.cint:
                raise BenchError("--cint is required for a %s workload" % form)
            self.cint = pathlib.Path(self.ns.cint).resolve()
        compiler = compiler_identity(self.cint)
        variants = self.variants()
        self.prepare()
        started = utc_now()
        if form == "compiler":
            return self.execute_compiler(variants, compiler, started)
        self.inputs += [ROOT / w["cint"]["source"]] + self.imports()
        self.inputs += [ROOT / v["source"] for v in variants if v["role"] == "baseline"]
        if form == "call":
            self.inputs += [DRIVER / n for n in ("bench_call.c", "bench_target.h", "target_cint.c", "target_c.c")]
            self.inputs.append(ROOT / "harness" / "cint_harness.c")
        built, builds, agreements, ref_lines = {}, {}, {}, None
        for v in variants:
            vw = self.work / v["name"]
            if form == "program":
                built[v["name"]], builds[v["name"]] = self.build_program(v, self.values, vw / "full")
            else:
                built[v["name"]], builds[v["name"]], lib = self.build_call(v, vw)
            print("built %s" % v["name"], flush=True)
            if v["role"] != "cint":
                continue
            # Agreement before timing (posture 6.6).
            if form == "program":
                aw = vw / "agreement"
                exe, abuild = self.build_program(v, self.agree_values, aw)
                src = aw / "src" / pathlib.Path(w["cint"]["source"]).name
                reference, ref_out = ref_record(src)
                got = run_program(exe, aw, self.env, None)
                ident, ok, path = self.diff_receipt(v, abuild, reference, got["record"], ref_out, got["stdout"],
                                                    self.agree_values)
            else:
                ident, ok, path, reference = self.agree_call(v, built[v["name"]], builds[v["name"]], lib)
                ref_lines = reference
            agreements[v["name"]] = ident
            print("agreement %s: %s (%s)" % (v["name"], "agree" if ok else "differ", path.name), flush=True)
            if not ok:
                print("\n".join(["reference:"] + reference), flush=True)
                raise BenchError("%s does not agree with cint_ref; nothing is timed" % v["name"])
        if form == "call" and ref_lines is not None:
            for v in variants:
                if v["role"] == "baseline":
                    got = [s[2] for s in self.agreement_calls(built[v["name"]], v["entry"], None)]
                    if got != self.ref_returns:
                        raise BenchError("baseline %s gives %s at the agreement arguments; cint_ref %s" % (
                            v["name"], got, self.ref_returns))
        samples, digests, timers, order = self.time(variants, built)
        return self.finish(variants, builds, agreements, samples, digests, timers, order, compiler, started)

    def imports(self) -> list:
        return [ROOT / m for m in self.w.get("cint", {}).get("imports", [])]

    def call_args(self, values: dict) -> list:
        return [values[a] for a in self.w.get("args", [])]

    def agreement_calls(self, exe: pathlib.Path, entry: str, fuel) -> list:
        """bench-call's samples at the agreement arguments: one call, or with an index argument the
        calls 0 to A - 1 (method.agreement_calls, default 3)."""
        index = self.index()
        count = 1 if index is None else self.w["method"].get("agreement_calls", 3)
        return run_driver(exe, entry, self.call_args(self.agree_values), fuel, 0, count, self.env, None,
                          index)["samples"]

    def index(self):
        name = self.w.get("index")
        return None if name is None else self.w["args"].index(name)

    def agree_call(self, v: dict, exe: pathlib.Path, build: str, lib: pathlib.Path):
        """Agreement of one cint variant of a call workload: cint-harness's record of the call against
        cint_ref's, and bench-call's result and fuel against cint_ref's for each agreement call."""
        w, args, index = self.w, self.call_args(self.agree_values), self.index()
        src = self.work / "ref" / pathlib.Path(w["cint"]["source"]).name
        place_imports(w["cint"].get("imports", []), src.parent)
        src.write_bytes((ROOT / w["cint"]["source"]).read_bytes())
        driver = self.agreement_calls(exe, w["cint"]["entry"], self.fuel_of(v))
        references, returns = [], []
        for k in range(len(driver)):
            call = list(args)
            if index is not None:
                call[index] = k
            lines, _ = ref_record(src, w["cint"]["entry"], call)
            references.append(lines)
            ret = next((x for x in lines if x.startswith("return I64 ")), None)
            returns.append(int(ret.split()[2]) if ret else None)
        self.ref_returns = returns
        call = list(args)
        if index is not None:
            call[index] = 0
        candidate = harness_record(self.harness(v["helpers"]), lib, pathlib.PurePosixPath(w["cint"]["source"]).stem,
                                   w["cint"]["entry"], call, self.env)
        driver_ok = all(st == 0 and ["return I64 %d" % r, "fuel-consumed %d" % f] == [
            x for x in ref if x.startswith(("return ", "fuel-consumed "))] for (_, st, r, f), ref in zip(driver, references))
        values = dict(self.agree_values, **({w["index"]: "0 to %d" % (len(driver) - 1)} if index is not None else {}))
        ident, ok, path = self.diff_receipt(v, build, references[0], candidate, b"", None, values,
                                            {"driver": "agree" if driver_ok else "differ", "calls": len(driver),
                                             "returns_sha256": sha256(" ".join(map(str, returns)).encode())})
        return ident, ok and driver_ok, path, references[0]

    def time(self, variants: list, built: dict):
        form, m = self.w["form"], self.method
        names = [v["name"] for v in variants]
        by_name = {v["name"]: v for v in variants}
        samples = {n: [] for n in names}
        digests = {n: set() for n in names}
        timers, order = {}, []
        args = self.call_args(self.values)
        index = self.index() if form == "call" else None
        if form == "program":
            for n in names:   # the warm-up runs, discarded (SPEC-06 11)
                for _ in range(m["warmup"]):
                    self.sample_program(by_name[n], built[n], None)
        for k, round_names in enumerate(rounds_order(names, m["rounds"])):
            for n in round_names:
                v = by_name[n]
                order.append(n)
                if form == "program":
                    for _ in range(m["samples"]):
                        ns, digest = self.sample_program(v, built[n], self.pin)
                        samples[n].append(ns)
                        digests[n].add(digest)
                else:
                    entry = self.w["cint"]["entry"] if v["role"] == "cint" else v["entry"]
                    out = run_driver(built[n], entry, args, self.fuel_of(v), m["warmup"], m["samples"], self.env,
                                     self.pin, index)
                    timers[n] = out["timer"]
                    samples[n] += [x[0] for x in out["samples"]]
                    if index is None:
                        for _, _, result, fuel in out["samples"]:
                            digests[n].add(json.dumps({"result": "I64 %d" % result} if fuel < 0 else
                                                      {"fuel_consumed": str(fuel), "result": "I64 %d" % result},
                                                      sort_keys=True))
                    else:   # a digest stream (SPEC-06 11, per iteration): the same in every round
                        digests[n].add(json.dumps(stream_digest(out["samples"], m["warmup"]), sort_keys=True))
            print("round %d of %d" % (k + 1, m["rounds"]), flush=True)
        return samples, digests, timers, order

    def sample_program(self, v: dict, exe: pathlib.Path, pin):
        work = exe.parent
        if v["role"] == "cint":
            r = run_program(exe, work, self.env, pin)
            if r["exit"] != 0 or not r["record"][0].startswith("outcome value"):
                raise BenchError("%s: %s" % (v["name"], " / ".join(r["record"][:4])))
            fuel = next(x for x in r["record"] if x.startswith("fuel-consumed "))
            return r["ns"], json.dumps({"exit": r["exit"], "fuel_consumed": fuel.split()[1],
                                        "stdout_sha256": sha256(r["stdout"])}, sort_keys=True)
        r = run_baseline_program(exe, work, self.env, pin)
        if r["exit"] != 0:
            raise BenchError("%s exited %d" % (v["name"], r["exit"]))
        return r["ns"], json.dumps({"exit": 0, "stdout_sha256": sha256(r["stdout"])}, sort_keys=True)

    def finish(self, variants, builds, agreements, samples, digests, timers, order, compiler, started) -> int:
        w = self.w
        results, baseline_results = {}, {}
        for v in variants:
            n = v["name"]
            if len(digests[n]) != 1:
                raise BenchError("%s: the samples gave %d different results; the run is invalid (SPEC-06 11)"
                                 % (n, len(digests[n])))
            digest = json.loads(next(iter(digests[n])))
            if v["role"] == "cint":
                results[n] = digest
            else:
                baseline_results[n] = digest
        cint_result = next(iter(results.values()))
        if not all(same_result(cint_result, d) for d in results.values()):
            raise BenchError("the cint variants gave different results: %s" % results)
        for v in variants:   # an integer baseline computes the cint result; another (binary64) need not
            if v["role"] == "baseline" and v.get("number_format") == "I64" and \
                    not same_result(cint_result, baseline_results[v["name"]]):
                raise BenchError("baseline %s gave %s, the cint variants %s" % (
                    v["name"], baseline_results[v["name"]], cint_result))
        expected = w.get("cint", {}).get("expected_stdout_sha256", {})
        key = str(self.values.get(w.get("elements"), "")) if w.get("elements") else None
        if w["form"] == "program" and key in expected and cint_result["stdout_sha256"] != expected[key]:
            raise BenchError("the cint stdout digest %s is not the recorded %s" % (cint_result["stdout_sha256"],
                                                                                  expected[key]))
        per_variant = {n: statistics(samples[n]) for n in samples}
        configs = {v["name"]: "%s-%s-%s" % (self.leg, self.opt, v["helpers"]) for v in variants if v["role"] == "cint"}
        elements = self.values.get(w["elements"]) if w.get("elements") else None
        bars = [evaluate_bar(b, per_variant, configs, elements) for b in w["bars"]]
        reports = []
        for r in w.get("reports", []):
            if r["of"] in per_variant and r["against"] in per_variant:
                a, b = (statistic(per_variant[x], r.get("statistic", "median")) for x in (r["of"], r["against"]))
                reports.append({"id": r["id"], "of": r["of"], "against": r["against"],
                                "statistic": r.get("statistic", "median"),
                                "of_value": [str(a[0]), str(a[1])] if a else None,
                                "against_value": [str(b[0]), str(b[1])] if b else None})
        identity = {
            "schema": PERF, "kind": "perf", "profile": "cint-core-1", "benchmark": w["benchmark"],
            "hypothesis": w.get("hypothesis"),
            "tool": {"compiler_source_identity": compiler},
            "source": source_identity(self.inputs),
            "workload": {"name": w["name"], "form": w["form"], "parameters": self.values,
                         "elements": elements, **({"entry": w["cint"]["entry"], "args": w.get("args", [])}
                                                  if w["form"] == "call" else {})},
            "method": {"rounds": self.method["rounds"], "warmup": self.method["warmup"],
                       "samples": self.method["samples"], "order": "alternating",
                       "isolation": "call" if w["form"] == "call" else "process", "timer": "monotonic",
                       "digest": "outside"},
            "variants": [self.variant_identity(v, builds.get(v["name"])) for v in variants],
            "agreement": agreements,
        }
        outcome = {"results": results, "verdict": "measured"}
        observations = {
            "per_variant": per_variant, "baseline_results": baseline_results, "baseline_build": self.observed_builds,
            "bars": bars, "reports": reports, "not_run": self.skipped, "execution_order": order,
            "timers": timers, "toolchain": self.toolchain_observation(), "machine": machine(),
            "started_utc": started, "finished_utc": utc_now(), "limits": LIMITS,
            "pinned_cpu": self.ns.cpu,
        }
        path = self.receipts / ("perf-%s.json" % self.tag)
        ident = write_receipt(path, identity, outcome, observations)
        for n in samples:
            s = per_variant[n]
            print("%-24s median %s ns  min %s  max %s  (n %d)" % (n, s.get("median_ns", "-"), s["min_ns"],
                                                                  s["max_ns"], s["count"]))
        for b in bars:
            if "cint_per_element" in b:
                a, n = map(int, b["cint_per_element"])
                detail = ": %s %d/%d ns per element (%d.%03d) against %d ns, times %d/%d" % (
                    b["cint_variant"], a, n, a // n, a * 1000 // n % 1000, b["ns_per_element"], b["times"][0],
                    b["times"][1])
            elif "cint" in b:
                detail = ": %s %s/%s ns against %s %s/%s ns, times %d/%d" % (
                    b["cint_variant"], b["cint"][0], b["cint"][1], b["against_variant"], b["against"][0],
                    b["against"][1], b["times"][0], b["times"][1])
            else:
                detail = ""
            print("bar %s (%s): %s%s" % (b["id"], b["status"], b["verdict"], detail))
        print("receipt %s identity %s" % (path, ident[:16]))
        return 0

    # ---- the compiler form (B-10)
    def execute_compiler(self, variants, compiler, started) -> int:
        import cint_fixpoint as fx  # noqa: PLC0415 (only this form builds the stage chain)
        v = variants[0]
        inputs = self.work / "inputs"
        groups = boot.snapshot_inputs(ROOT, inputs)
        groups["compiler"] = boot.compiler_closure(groups["compiler"])
        tc = fx.Toolchain(self.leg, self.opt, v["helpers"], False, self.work / "logs")
        self.tcs[v["helpers"]], self.env = tc, tc.env
        (self.work / "chain").mkdir()
        chain = fx.build_chain(tc, inputs, self.work / "chain", self.ns.timeout)
        s2, s3 = chain["s2"]["files"], chain["s3"]["files"]
        if s2 is None or s3 is None:
            raise BenchError("B1 or B2 committed no output set")
        build = fx.tree_digest(s2)
        reference = ["s2-tree-sha256 " + build]
        candidate = ["s2-tree-sha256 " + fx.tree_digest(s3)]
        identity = {"schema": DIFF, "kind": "diff", "profile": "cint-core-1",
                    "workload": {"name": self.w["name"], "form": "compiler", "parameters": {}},
                    "reference": {"backend": "cpu-c17", "stage": "S2 by B1"},
                    "candidate": {"backend": "cpu-c17", "stage": "S3 by B2", "helpers": v["helpers"]},
                    "source": {"compiler_source_identity": compiler}}
        agree = fx.first_difference(s2, s3) is None
        outcome = {"outcome": "agree" if agree else "differ", "record_sha256": sha256(reference[0].encode()),
                   "method": "FIX-01: S2 and S3 file for file"}
        path = self.receipts / ("agree-%s-%s.json" % (self.tag, v["name"]))
        ident = write_receipt(path, identity, outcome, {"reference_record": reference, "candidate_record": candidate,
                                                        "toolchain": self.toolchain_observation(),
                                                        "machine": machine(), "finished_utc": utc_now(),
                                                        "limits": "FIX-01 on this toolchain configuration."})
        print("agreement %s: %s (%s)" % (v["name"], "agree" if agree else "differ", path.name), flush=True)
        if not agree:
            raise BenchError("S3 differs from S2; nothing is timed")
        rels, root, b1 = chain["rels"], inputs / "compiler", chain["b1"]

        def one(pin):
            out = self.work / "sample" / "out"
            shutil.rmtree(out.parent, ignore_errors=True)
            out.parent.mkdir(parents=True)
            t0 = time.perf_counter_ns()
            r = subprocess.run([str(b1), str(root), str(out), *rels], cwd=out.parent, env=self.env,
                               capture_output=True, preexec_fn=pin, timeout=self.ns.timeout)
            ns = time.perf_counter_ns() - t0
            files = fx.committed_set(out) if r.returncode == 0 else None
            if files is None:
                raise BenchError("B1 exited %d with no output set" % r.returncode)
            return ns, fx.tree_digest(files)
        for _ in range(self.method["warmup"]):
            one(None)
        samples, digests, order = {v["name"]: []}, {v["name"]: set()}, []
        for _ in range(self.method["rounds"]):
            for _ in range(self.method["samples"]):
                ns, digest = one(self.pin)
                samples[v["name"]].append(ns)
                digests[v["name"]].add(json.dumps({"s2_tree_sha256": digest}, sort_keys=True))
                order.append(v["name"])
        if digests[v["name"]] != {json.dumps({"s2_tree_sha256": build}, sort_keys=True)}:
            raise BenchError("a timed run of B1 gave another output set; the run is invalid (SPEC-06 11)")
        self.inputs += [ROOT / rel for files in groups.values() for rel in sorted(files)]
        return self.finish(variants, {v["name"]: build}, {v["name"]: ident}, samples, digests, {}, order,
                           compiler, started)


# --------------------------------------------------------------------------- command line

def main(argv=None) -> int:
    if not __debug__:
        raise SystemExit("cint_bench.py checks its results with assertions; run it without python -O")
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="command", required=True)
    sub.add_parser("list", help="the workloads and the bars they serve")
    run = sub.add_parser("run", help="time one workload and write its receipts")
    run.add_argument("workload")
    run.add_argument("--leg", required=True, choices=LEGS)
    run.add_argument("--cint", help="a cint that tools/cint_bootstrap.py built from this checkout")
    run.add_argument("--opt", default="2", choices=("0", "2"))
    run.add_argument("--size", default="full", choices=("full", "agreement"))
    run.add_argument("--rounds", type=int)
    run.add_argument("--warmup", type=int)
    run.add_argument("--samples", type=int)
    run.add_argument("--only", nargs="+", help="these variants only")
    run.add_argument("--rustc", help="the rustc of the Rust baseline (default: rustc on PATH, then ~/.cargo/bin)")
    run.add_argument("--cpu", type=int, help="pin each timed process to this logical CPU (Linux)")
    run.add_argument("--timeout", type=int, default=3600, help="seconds for one compiler run (compiler form)")
    run.add_argument("--out", help="the directory of receipts/ and work/")
    ns = ap.parse_args(argv)
    try:
        if ns.command == "list":
            for path in sorted(WORKLOADS.glob("*.json")):
                w = load_workload(path.stem)
                print("%-14s %-6s %-9s %s" % (w["name"], w["benchmark"], w["form"],
                                              ", ".join("%s (%s)" % (b["id"], b["status"]) for b in w["bars"])))
            return 0
        for k in ("rounds", "samples"):
            if getattr(ns, k) is not None and getattr(ns, k) < 1:
                ap.error("--%s is at least 1" % k)
        if ns.warmup is not None and ns.warmup < 0:
            ap.error("--warmup is at least 0")
        return Run(load_workload(ns.workload), ns).execute()
    except (BenchError, cc.BuildError) as e:
        print("cint_bench: %s" % e, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
