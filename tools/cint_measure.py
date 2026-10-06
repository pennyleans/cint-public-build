"""Phase fuel and table storage of B1 (slice 2 task 2.17; SPEC-09 CINTC-02, CINTC-09).

    python tools/cint_measure.py --leg msvc|gcc|clang|apple-clang [--bootstrap-receipt FILE]
        [--out DIR] [--receipt FILE] [--jobs N] [--timeout SECONDS]

Work goes under <build>/measure/<leg>, where <build> is CINT_BUILD or cint-build in the system
temporary directory (cint_check.build_root), never inside the repository.

Builds B1 with tools/cint_bootstrap.py (or takes a retained bootstrap of the current sources),
links its compiler with tools/cint_measure_host.c and rt/cint_build.c compiled with
CINT_BRIDGE_MEASURE_HOOKS, and builds each input once through the production bridge: an empty
module, each conformance case (root conformance/), and each compiler module (root compiler/),
every one as the program of its import closure in D-13 order (cint_check.module_order). So
compiler/main.ci is measured as the whole compiler program.

For every plan, compile, measure, and emit call it records the fuel consumed (A-17) and, as the
build ends, the capacity and highest written row of every table. From those it computes:

- the phase fuel constants of CINTC-09 by the plan's method: c is the most fuel any call of the
  empty module consumed; k is the largest ceil(max(0, fuel - c) / n) over every call that
  returned (diagnostic results included), where n is the input bytes the bridge budgets the call
  by; the candidate constants are 4k and 4c, checked against every such call;
- the memory constant K of CINTC-02, derived from the capacity formulas (decision 2026-10-05 on
  OQ-175): plan alone runs for one module of n bytes, for every n from 1 to 1,024 and every
  multiple of 1,024 up to the 4 MiB module limit, and K is the largest
  ceil(max(0, R - D - 1 MiB) / n), where R is the bytes of module-scope tables requested
  (max(capacity, 1) times the element size, every table live at once) and D the name table's
  term for the program's declarations (two name rows per declaration row), which CINTC-02 states
  apart as 80 B per declaration until the name table's import term moves to program scope;
- for every input, whether its module-scope tables fit K n + 1 MiB + D, with each table's share
  of the rows the compiler wrote.

The receipt (default results/cint/t2/measure-<leg>.json) keeps identity and outcome apart from
the host's observations (RCPT-03): the outcome holds no paths, times, or executable digests, so
two legs that agree have the same identity_sha256. The outcome also checks the frozen budget of
the bridge (CINT_PHASE_FUEL_K and _C) against every call, and K against the T2 ceiling.
"""
from __future__ import annotations

import argparse
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

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import cint_bootstrap as boot  # noqa: E402
import cint_check as cc  # noqa: E402

SCHEMA = "cint-measure-receipt-4"   # schemas 1 to 3 belong to an unreleased branch
MIB = 1048576
MARGIN = 4
K_CEILING = 800        # T2 (CINTC-02; decision 2026-10-05 on OQ-207, KR-06)
K_GOAL = 800           # the column 02 reduction goal (CINTC-02; OQ-198, OQ-204)
T_SYMBOLS, T_NAMES, T_DECLS = 12, 13, 16
MEASUREMENT_INPUTS = ("tools/cint_measure.py", "tools/cint_measure_host.c")
TABLE_NAMES = ("modules", "paths", "text", "tokens", "nodes", "parse frames", "SIR", "functions",
               "slots", "lowering frames", "lowering state", "semantic", "symbols", "names",
               "frame region", "literals", "declarations")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def bridge_budget() -> tuple:
    """CINT_PHASE_FUEL_K and _C as rt/cint_bridge.h defines them."""
    text = (ROOT / "rt/cint_bridge.h").read_text(encoding="ascii")
    k, c = (int(re.search(r"#define CINT_PHASE_FUEL_%s \(\(int64_t\)(\d+)\)" % n, text).group(1)) for n in "KC")
    return k, c


def ceil_div(a: int, b: int) -> int:
    return -(-a // b)


def inventory(root: pathlib.Path) -> list:
    """Every measured input as (source root name, module-relative path), in byte order of path."""
    rows = [("conformance", p.relative_to(root / "conformance").as_posix())
            for p in (root / "conformance").rglob("*.ci")]
    rows += [("compiler", p.name) for p in (root / "compiler").glob("*.ci")]
    return sorted(rows, key=lambda r: (r[0] + "/" + r[1]).encode("utf-8"))


def snapshot(root: pathlib.Path, dest: pathlib.Path) -> dict:
    """Copies conformance/**/*.ci and compiler/*.ci under dest; their CISRC001 manifest rows."""
    files = {}
    for top, rel in inventory(root):
        data = (root / top / rel).read_bytes()
        target = dest / top / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        files[top + "/" + rel] = data
    return files


def source_rows(files: dict) -> list:
    return [{"path": p, "bytes": len(d), "sha256": sha256(d)}
            for p, d in sorted(files.items(), key=lambda kv: kv[0].encode("utf-8"))]


def check_bootstrap(path: pathlib.Path) -> dict:
    """The bootstrap receipt at path, refused unless its compiler and runtime are this tree's."""
    receipt = json.loads(path.read_bytes())
    if receipt.get("schema") != "cint-bootstrap-receipt-1":
        raise ValueError("unsupported bootstrap receipt: %s" % path)
    identity = receipt["identity"]
    if receipt["identity_sha256"] != sha256(cc.canonical({"identity": identity, "outcome": receipt["outcome"]})):
        raise ValueError("bootstrap receipt identity changed: %s" % path)
    if identity["compiler_source_identity"] != cc.b1_source_identity():
        raise ValueError("the bootstrap's compiler is not this tree's: %s" % path)
    for row in identity["runtime_sources"]:
        data = (ROOT / row["path"]).read_bytes()
        if sha256(data) != row["sha256"]:
            raise ValueError("the bootstrap's runtime is not this tree's: %s" % row["path"])
    return receipt


def build_host(leg: str, bootstrap: pathlib.Path, work: pathlib.Path):
    """The observed B1: the bootstrap's compiler and runtime objects with the observer host and
    the bootstrap's own rt/cint_build.c compiled with CINT_BRIDGE_MEASURE_HOOKS."""
    base = bootstrap.parent
    tc = boot.LoggedToolchain(leg, work / "logs")
    cc.RT = base / "inputs/rt"
    tc.flags = tc.flags + [("/D" if leg == "msvc" else "-D") + "CINT_BRIDGE_MEASURE_HOOKS"]
    objs = tc.compile([ROOT / "tools/cint_measure_host.c", base / "inputs/rt/cint_build.c"], work,
                      "measurement host")
    exe = tc.link_exe(objs + [base / ("s1/main" + tc.obj), base / ("rt/cint_rt" + tc.obj),
                              base / ("rt/cint_bridge" + tc.obj)], work / "cint-measure-host", "measurement host")
    return exe, tc


def parse_events(stdout: bytes) -> tuple:
    phases, tables, build, errors = [], [], None, []
    for line in stdout.decode("ascii", "replace").splitlines():
        try:
            event = json.loads(line)
            kind = event.pop("e")
        except (ValueError, KeyError, AttributeError):
            errors.append("unreadable host line: %r" % line[:200])
            continue
        if kind == "phase":
            phases.append(event)
        elif kind == "table":
            tables.append(event)
        elif kind == "build" and build is None:
            build = event
        else:
            errors.append("unexpected host event: %r" % line[:200])
    return phases, tables, build, errors


def run_sample(host, env, root: pathlib.Path, rel: str, name: str, work: pathlib.Path, timeout: int,
               budget: tuple) -> dict:
    """Builds the program of root module rel under root with the observed B1."""
    rels = cc.module_order(root, rel)
    sizes = [(root / m).stat().st_size for m in rels]
    out = work / "out"
    work.mkdir(parents=True)
    status = "completed"
    try:
        r = subprocess.run([str(host), str(root), str(out), *rels], env=env, capture_output=True, timeout=timeout)
        stdout, stderr = r.stdout, r.stderr
        if r.returncode != 0:
            status = "exit %d" % r.returncode
    except subprocess.TimeoutExpired as e:
        stdout, stderr, status = e.stdout or b"", e.stderr or b"", "timeout"
    (work / "stdout.log").write_bytes(stdout)
    (work / "stderr.log").write_bytes(stderr)
    phases, tables, build, errors = parse_events(stdout)
    if status != "completed":
        errors.append("host " + status)
    elif build is None:
        errors.append("no build event")
    k, c = budget
    rows = []
    for p in phases:
        n, rem = divmod(p["budget"] - c, k)
        if rem or n < 0:
            errors.append("budget %d is not K n + C" % p["budget"])
        rows.append([p["phase"], p["module"], n, p["status"], p["result"], p["fuel"], p["fault"]])
    manifest = None
    if build is not None and build["result"] == 0:   # the committed set (CINTC-06; cint_check.run_b1)
        ref = (out / "MANIFEST.ref").read_bytes().decode("ascii")
        manifest = sha256((out.parent / ref.split(" ")[0] / "MANIFEST").read_bytes())
    return {"path": name, "modules": [[m, n] for m, n in zip(rels, sizes)],
            "build": build["result"] if build else None,
            "diagnostic": build["diag"][0][:4] if build and build["diag"] else None,
            "phases": rows,
            "tables": sorted([[t["table"], t["elem_bytes"], t["capacity"], t["scope"], t["used"]] for t in tables]),
            "manifest_sha256": manifest, "errors": errors}


def fuel_fit(samples: list) -> dict:
    """The CINTC-09 constants by the plan's method, over every call that returned (status 0)."""
    returned = [(s["path"], row) for s in samples for row in s["phases"] if row[3] == 0 and row[5] is not None]
    empty = [row[5] for path, row in returned if path == "@empty"]
    if not empty:
        return {"raw_c": None, "raw_k": None, "verified": False, "calls": len(returned)}
    c = max(empty)
    k, at = 0, None
    for path, row in returned:
        if row[2] > 0 and ceil_div(max(0, row[5] - c), row[2]) > k:
            k, at = ceil_div(max(0, row[5] - c), row[2]), {"path": path, "phase": row[0], "module": row[1],
                                                            "bytes": row[2], "fuel": row[5]}
    by_phase = {}
    for path, row in returned:
        best = by_phase.get(str(row[0]))
        if best is None or row[5] > best["fuel"]:
            by_phase[str(row[0])] = {"path": path, "module": row[1], "bytes": row[2], "fuel": row[5]}
    verified = all(row[5] <= MARGIN * k * row[2] + MARGIN * c for _, row in returned)
    return {"method": "c: most fuel of a call of the empty module; k: largest ceil(max(0, fuel - c) / bytes); "
                      "every plan, compile, measure, and emit call that returned, diagnostic results included",
            "calls": len(returned), "raw_c": c, "raw_k": k, "k_at": at, "margin": MARGIN,
            "candidate_c": MARGIN * c, "candidate_k": MARGIN * k, "verified": verified,
            "most_fuel_by_phase": by_phase}


def frozen(samples: list, budget: tuple, derived: dict) -> dict:
    """The bridge's frozen budget (CINTC-09) against every call, and K against the T2 ceiling."""
    k, c = budget
    calls = [row for s in samples for row in s["phases"]]
    returned = [row for row in calls if row[3] == 0 and row[5] is not None]
    return {"k": k, "c": c, "calls": len(calls), "calls_not_returned": len(calls) - len(returned),
            "every_call_within": len(returned) == len(calls) and all(row[5] <= k * row[2] + c for row in returned),
            "K": derived.get("K"), "K_ceiling": K_CEILING, "K_within_ceiling": derived.get("within_ceiling", False)}


def sweep_sizes() -> list:
    """The module sizes plan runs for: 1 to 1,024, then every multiple of 1,024 to the 4 MiB limit."""
    return list(range(1, 1025)) + list(range(2048, 4 * MIB + 1, 1024))


def plan_sweep(host, env, sizes: list, timeout: int) -> tuple:
    """Each size's manifest rows [elem_bytes, capacity, scope] from the host's --plan mode."""
    r = subprocess.run([str(host), "--plan"], env=env, input="".join("%d\n" % n for n in sizes).encode("ascii"),
                       capture_output=True, timeout=timeout)
    plans, errors = {}, []
    if r.returncode != 0:
        errors.append("plan host exit %d" % r.returncode)
    for line in r.stdout.decode("ascii", "replace").splitlines():
        try:
            event = json.loads(line)
            if event["e"] != "plan":
                raise ValueError
        except (ValueError, KeyError, TypeError):
            errors.append("unreadable plan line: %r" % line[:200])
            continue
        if event["status"] != 0 or event["result"] != 0 or len(event["rows"]) != len(TABLE_NAMES):
            errors.append("plan of %d bytes: status %s, result %s" % (event["n"], event["status"], event["result"]))
            continue
        plans[event["n"]] = event["rows"]
    if sorted(plans) != sorted(sizes) and not errors:
        errors.append("plan sweep returned %d of %d sizes" % (len(plans), len(sizes)))
    return plans, errors


def name_terms(caps: dict, elems: dict) -> tuple:
    """(the name table's constant term, D): CINTC-02 plans names as 2 x symbols + a constant +
    2 x declaration rows (limits.name_capacity), so D is 2 x name bytes x declaration rows."""
    return (caps[T_NAMES] - 2 * caps[T_SYMBOLS] - 2 * caps[T_DECLS],
            2 * elems[T_NAMES] * caps[T_DECLS])


def module_bytes(rows) -> int:
    return sum(max(cap, 1) * e for e, cap, scope in rows if scope == 1)


def derive_K(plans: dict) -> dict:
    """CINTC-02's K from the formulas: the largest ceil(max(0, R - D - 1 MiB) / n) over the sweep."""
    best, at, constants = None, None, set()
    for n in sorted(plans):
        rows = plans[n]
        constant, d = name_terms({t: r[1] for t, r in enumerate(rows)}, {t: r[0] for t, r in enumerate(rows)})
        constants.add(constant)
        k = ceil_div(max(0, module_bytes(rows) - d - MIB), n)
        if best is None or k > best:
            best, at = k, n
    if best is None:
        return {"K": None, "ceiling": K_CEILING, "within_ceiling": False}
    rows = plans[at]
    tables = []
    for t, (e, cap, scope) in enumerate(rows):
        if scope != 1:
            continue
        b = max(cap, 1) * e - (2 * e * rows[T_DECLS][1] if t == T_NAMES else 0)
        tables.append({"table": t, "name": TABLE_NAMES[t], "elem_bytes": e, "capacity": cap, "bytes": b,
                       "per_source_byte": [b, at]})
    return {"method": "plan alone for one module of n bytes, n from 1 to 1,024 and every multiple of 1,024 to "
                      "4,194,304; K is the largest ceil(max(0, R - D - 1 MiB) / n), R the requested module-scope "
                      "table bytes (max(capacity, 1) times element bytes, every table live at once), D two name "
                      "rows per declaration row",
            "K": best, "at_bytes": at, "ceiling": K_CEILING, "within_ceiling": best <= K_CEILING,
            "reduction_goal": K_GOAL, "sizes": len(plans),
            "declaration_row_bytes": 2 * rows[T_NAMES][0], "name_constant_rows": sorted(constants),
            "tables": tables}


def storage(samples: list, derived: dict) -> dict:
    """K from the formulas, and the inputs checked against K n + 1 MiB + D."""
    K = derived.get("K")
    over, constants, most = [], set(), None
    use = {}
    for s in samples:
        if not s["tables"]:
            continue
        n = max(b for _, b in s["modules"])
        caps = {t: cap for t, _, cap, _, _ in s["tables"]}
        elems = {t: e for t, e, _, _, _ in s["tables"]}
        constant, d = name_terms(caps, elems)
        constants.add(constant)
        requested = sum(max(cap, 1) * e for _, e, cap, scope, _ in s["tables"] if scope == 1)
        if K is None or requested > K * n + MIB + d:
            over.append(s["path"])
        with_d = ceil_div(max(0, requested - MIB), n) if n > 0 else 0
        if most is None or with_d > most[0]:
            most = (with_d, s["path"], n, requested, d)
        for t, e, cap, scope, used in s["tables"]:
            u = use.setdefault(t, {"table": t, "name": TABLE_NAMES[t] if t < len(TABLE_NAMES) else str(t),
                                   "scope": scope, "elem_bytes": e, "most_used_fraction": [0, 1],
                                   "most_used_bytes_per_source_byte": [0, 1], "at": None})
            if cap > 0 and used * u["most_used_fraction"][1] > u["most_used_fraction"][0] * cap:
                u["most_used_fraction"] = [used, cap]
            if n >= 4096 and used * e * u["most_used_bytes_per_source_byte"][1] > \
                    u["most_used_bytes_per_source_byte"][0] * n:
                u["most_used_bytes_per_source_byte"] = [used * e, n]
                u["at"] = s["path"]
    result = {"derived": derived,
              "bound": "module-scope table bytes <= K x largest module bytes + 1 MiB + D",
              "inputs_over_bound": over, "name_constant_rows": sorted(constants),
              "use": [use[t] for t in sorted(use)],
              "used_rows_method": "one more than the last row holding a nonzero byte as the build frees the table, a lower "
                                  "bound on the rows written; most_used_bytes_per_source_byte over inputs of at least "
                                  "4,096 bytes; for the hashed names table (13) the rows the checker cleared, not its fill",
              "not_counted": ["source buffers", "program-scope tables", "output buffers", "diagnostic and fault "
                              "buffers", "bridge metadata", "allocator overhead", "native stack", "process memory"]}
    if most is not None:
        result["most_with_declarations"] = {"K": most[0], "path": most[1], "largest_module_bytes": most[2],
                                            "module_bytes_requested": most[3], "declaration_bytes": most[4]}
    return result


def measure(leg: str, out: pathlib.Path, receipt_path: pathlib.Path, bootstrap: pathlib.Path | None,
            jobs: int, timeout: int) -> pathlib.Path:
    boot.validate_output_root(ROOT, out)
    out.mkdir(parents=True, exist_ok=True)
    work = pathlib.Path(tempfile.mkdtemp(prefix="measure-", dir=out)).resolve()
    if bootstrap is None:
        bootstrap = boot.bootstrap(leg, work / "boot")
    started = datetime.datetime.now(datetime.timezone.utc)
    breceipt = check_bootstrap(bootstrap)
    if breceipt["observations"]["c0"]["name"] != leg:
        raise ValueError("the bootstrap is of leg %s" % breceipt["observations"]["c0"]["name"])
    (work / "host").mkdir()
    host, tc = build_host(leg, bootstrap, work / "host")
    host_sha = sha256(host.read_bytes())
    measured = {p: (ROOT / p).read_bytes() for p in MEASUREMENT_INPUTS}
    corpus = work / "corpus"
    files = snapshot(ROOT, corpus)
    (corpus / "empty").mkdir()
    (corpus / "empty/empty.ci").write_bytes(b"")
    budget = bridge_budget()
    todo = [(corpus / "empty", "empty.ci", "@empty")] + \
        [(corpus / top, rel, top + "/" + rel) for top, rel in inventory(ROOT)]
    samples = [None] * len(todo)
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = {pool.submit(run_sample, host, tc.env, root, rel, name, work / "samples" / ("%04d" % i),
                               timeout, budget): i for i, (root, rel, name) in enumerate(todo)}
        for done, f in enumerate(concurrent.futures.as_completed(futures), 1):
            i = futures[f]
            samples[i] = f.result()
            if samples[i]["errors"] or todo[i][2].startswith("compiler/") or done % 50 == 0:
                print("%d/%d %s: build %s%s" % (done, len(todo), todo[i][2], samples[i]["build"],
                                                 "; " + "; ".join(samples[i]["errors"]) if samples[i]["errors"] else ""),
                      flush=True)
    plans, plan_errors = plan_sweep(host, tc.env, sweep_sizes(), timeout)
    errors = ["%s: %s" % (s["path"], e) for s in samples for e in s["errors"]] + plan_errors
    if {p: (ROOT / p).read_bytes() for p in MEASUREMENT_INPUTS} != measured or \
            source_rows({t + "/" + r: (ROOT / t / r).read_bytes() for t, r in inventory(ROOT)}) != source_rows(files):
        errors.append("measured sources changed during the run")
    for s in samples:
        del s["errors"]
    compiler = [s for s in samples if s["path"].startswith("compiler/")]
    identity = {"profile": "cint-core-1", "runtime_contract": "cint-rt-3", "fuel_encoding": "fuel-v1",
                "compiler_source_identity": breceipt["identity"]["compiler_source_identity"],
                "runtime_sources": breceipt["identity"]["runtime_sources"],
                "measurement_sources": source_rows(measured),
                "inventory_sha256": sha256(boot.source_manifest(files)),
                "production_budget": {"k": budget[0], "c": budget[1]}}
    outcome = {"inputs": len(samples), "built": sum(s["build"] == 0 for s in samples),
               "diagnosed": sum(s["build"] == 1 for s in samples),
               "compiler_modules_built": [s["path"] for s in compiler if s["build"] == 0],
               "compiler_modules_not_built": [s["path"] for s in compiler if s["build"] != 0],
               "measurement_errors": errors, "fuel": fuel_fit(samples), "storage": storage(samples, derive_K(plans)),
               "samples": samples}
    outcome["frozen"] = frozen(samples, budget, outcome["storage"]["derived"])
    observations = {"leg": leg, "toolchain": tc.describe(), "commands": tc.commands,
                    "bootstrap_receipt": {"path": str(bootstrap), "sha256": sha256(bootstrap.read_bytes())},
                    "b1_sha256": breceipt["observations"]["b1_sha256"], "host_sha256": host_sha,
                    "machine": {"system": platform.system(), "release": platform.release(),
                                "architecture": platform.machine(), "cpus": os.cpu_count()},
                    "work": str(work), "jobs": jobs, "timeout_seconds": timeout,
                    "started_utc": started.isoformat(timespec="seconds"),
                    "finished_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds")}
    receipt = {"schema": SCHEMA, "identity": identity, "outcome": outcome, "observations": observations,
               "identity_sha256": sha256(cc.canonical({"identity": identity, "outcome": outcome}))}
    data = cc.canonical(receipt) + b"\n"
    (work / "receipt.json").write_bytes(data)
    receipt_path.parent.mkdir(parents=True, exist_ok=True)
    receipt_path.write_bytes(data)
    fit, mem, fz = outcome["fuel"], outcome["storage"], outcome["frozen"]
    print("fuel: c %s, k %s; candidate c %s, k %s; verified %s" % (fit["raw_c"], fit["raw_k"], fit.get("candidate_c"),
                                                                    fit.get("candidate_k"), fit["verified"]))
    print("frozen budget: k %d, c %d; every call within %s" % (fz["k"], fz["c"], fz["every_call_within"]))
    print("storage: K %s (ceiling %d) at %s bytes; inputs over the bound %d" % (
        mem["derived"]["K"], K_CEILING, mem["derived"].get("at_bytes"), len(mem["inputs_over_bound"])))
    print("identity: %s" % receipt["identity_sha256"])
    if errors:
        print("measure: %d measurement errors (first: %s)" % (len(errors), errors[0]), file=sys.stderr)
    shown = receipt_path.resolve()
    print("receipt: %s" % (shown.relative_to(ROOT).as_posix() if ROOT in shown.parents else shown), flush=True)
    return receipt_path


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--leg", required=True, choices=("msvc", "gcc", "clang", "apple-clang"))
    ap.add_argument("--bootstrap-receipt", type=pathlib.Path, help="a retained build-*/bootstrap.json")
    ap.add_argument("--out", type=pathlib.Path, help="work directory (default <build>/measure/<leg>, <build> from CINT_BUILD)")
    ap.add_argument("--receipt", type=pathlib.Path, help="default results/cint/t2/measure-<leg>.json")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    ap.add_argument("--timeout", type=int, default=600, help="seconds per input")
    args = ap.parse_args(argv)
    if os.name == "nt" and args.leg not in ("msvc",):
        command = ["wsl", "-d", cc.WSL_DISTRO, "--", "python3", cc.wsl_path(pathlib.Path(__file__).resolve()),
                   "--leg", args.leg, "--jobs", str(args.jobs), "--timeout", str(args.timeout)]
        for option in ("bootstrap_receipt", "out", "receipt"):
            if getattr(args, option) is not None:
                command += ["--" + option.replace("_", "-"), cc.wsl_path(getattr(args, option).resolve())]
        return subprocess.run(command, cwd=ROOT, env=cc.wsl_env()).returncode
    if args.leg == "msvc" and os.name != "nt":
        ap.error("the msvc leg requires Windows")
    if args.jobs < 1 or args.timeout < 1:
        ap.error("--jobs and --timeout must be positive")
    receipt = args.receipt or ROOT / "results/cint/t2" / ("measure-%s.json" % args.leg)
    try:
        measure(args.leg, (args.out or cc.build_root() / "measure" / args.leg).resolve(), receipt.resolve(),
                args.bootstrap_receipt.resolve() if args.bootstrap_receipt else None, args.jobs, args.timeout)
    except (OSError, ValueError, KeyError, cc.BuildError, subprocess.SubprocessError) as error:
        print("measure: %s" % error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
