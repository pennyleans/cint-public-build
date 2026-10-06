"""The self-hosting fixpoint and corpus agreement of cintc (SPEC-09 FIX-01 to FIX-05).

    python tools/cint_fixpoint.py --leg msvc|gcc|clang|apple-clang [--opt 0|2]
        [--helpers portable|builtin] [--sanitize] [--no-corpus] [--out DIR]
        [--receipt FILE | --no-receipt] [--jobs N] [--timeout SECONDS]
    python tools/cint_fixpoint.py --compare RECEIPT [RECEIPT ...]

Builds the stages of SPEC-09 11.1 from retained copies of this checkout's inputs, all with one
toolchain configuration: B0 (the seed), S1 (what B0 emits for compiler/main.ci), B1 (S1 with
the runtime, the bridge, rt/cint_build.c and compiler/tests/golden_host.c), S2 (what B1 emits
for the import closure of compiler/main.ci in D-13 order, cint_check.module_order), B2 (S2's C
files with the same runtime, bridge and host objects), and S3 (what B2 emits for the same
sources). Then:

- FIX-01: S2 and S3 hold the same files, MANIFEST included, each with the same bytes;
- FIX-02: every file is compared by bytes and by SHA-256, and a difference names the first
  differing file in byte order of path, with its byte offset and line;
- FIX-03 (unless --no-corpus): B1 and B2 build every program of the corpus with the same
  report and the same committed output set, file for file. The corpus is every tracked .ci
  file under conformance/ (source root conformance/), examples/ and compiler/tests/ (source
  root the file's directory), each built as the program of its import closure. The anchor
  programs that tools/cint_check.py generates from CIF-1 are not in it.

Every host run must leave stderr empty, so a sanitizer report (--sanitize) is a failure. Before
T6 the receipt is a recorded observation, not a stage acceptance gate (STAGE-02). It keeps
identity and outcome apart from the host's observations (RCPT-03): the identity holds the input
digests and S2's files with s2_tree_sha256, the SHA-256 of their CISRC001 source manifest with
paths relative to the S2 output root, MANIFEST included (RCPT-04); neither part holds paths,
times or executable digests, so two configurations whose S2 and corpus results agree share one
identity_sha256, and --compare checks that over a set of receipts (FIX-04, cross-toolchain
bootstrap agreement). Executable digests are observations only (FIX-05).

Work goes under <build>/fixpoint/<tag>, where <build> is CINT_BUILD or cint-build in the
system temporary directory (cint_check.build_root), never inside the repository. The receipt
defaults to results/cint/fixpoint/fixpoint-<tag>.json, with <tag> <leg>-<opt>-<helpers>[-san].
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
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import cint_bootstrap as boot  # noqa: E402
import cint_check as cc  # noqa: E402

SCHEMA = "cint-fixpoint-receipt-1"
CORPUS_ROOTS = ("conformance", "examples", "compiler/tests")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def records(files: dict) -> list:
    return [{"path": p, "bytes": len(d), "sha256": sha256(d)}
            for p, d in sorted(files.items(), key=lambda kv: kv[0].encode("utf-8"))]


def tree_digest(files: dict) -> str:
    """RCPT-04: SHA-256 of the CISRC001 source manifest of an output set's files, with paths
    relative to its output root."""
    return sha256(boot.source_manifest(files))


class Toolchain(boot.LoggedToolchain):
    """The bootstrap's logged toolchain at any configuration of the run matrix (SPEC-09 9.4)."""
    def __init__(self, leg: str, opt: str, helpers: str, sanitize: bool, logs: pathlib.Path):
        cc.Toolchain.__init__(self, leg, opt, helpers, sanitize)
        self.logs, self.commands = logs, []
        logs.mkdir()


def first_difference(a: dict, b: dict):
    """None when a and b map the same paths to the same bytes; else the first differing path in
    byte order, with the byte offset and the 1-based line of its first difference in a (FIX-02).
    A path present on one side only has no offset."""
    for path in sorted(set(a) | set(b), key=lambda p: p.encode("utf-8")):
        x, y = a.get(path), b.get(path)
        if x == y:
            continue
        if x is None or y is None:
            return {"file": path, "only_in": "first" if y is None else "second", "offset": None, "line": None}
        n = min(len(x), len(y))
        offset = next((i for i in range(n) if x[i] != y[i]), n)
        return {"file": path, "only_in": None, "offset": offset, "line": x[:offset].count(b"\n") + 1}
    return None


def committed_set(out: pathlib.Path):
    """The output set the build committed under out (CINTC-06): {path: bytes} with its MANIFEST,
    checked against MANIFEST.ref and the MANIFEST lines; None when nothing was committed."""
    ref_path = out / "MANIFEST.ref"
    if not ref_path.is_file():
        return None
    ref = ref_path.read_bytes().decode("ascii")
    stage = out.parent / ref.split(" ")[0]
    manifest = (stage / "MANIFEST").read_bytes()
    if sha256(manifest) != ref.rstrip("\n").split(" ")[1]:
        raise ValueError("MANIFEST.ref does not name the MANIFEST it points to: %s" % out)
    files = {"MANIFEST": manifest}
    for line in manifest.decode("ascii").split("\n")[1:]:
        if line:
            digest, _size, rel = line.split(" ", 2)
            files[rel] = (stage / rel).read_bytes()
            if sha256(files[rel]) != digest:
                raise ValueError("%s differs from its MANIFEST line: %s" % (rel, out))
    return files


def host_report(stdout: bytes, windows: bool = os.name == "nt") -> bytes:
    """The golden host's report as the host wrote it. On Windows its stdout is in text mode, so
    the C runtime writes each line feed as CR LF; undoing that gives every leg the same bytes."""
    return stdout.replace(b"\r\n", b"\n") if windows else stdout


def run_host(host: pathlib.Path, env: dict, root: pathlib.Path, rels: list, out: pathlib.Path,
             timeout: int) -> dict:
    """One build by a golden host (compiler/tests/golden_host.c): its exit status, report
    (stdout, with line feeds as host_report gives them), stderr and committed output set."""
    shutil.rmtree(out.parent, ignore_errors=True)
    out.parent.mkdir(parents=True)
    try:
        r = subprocess.run([str(host), str(root), str(out), *rels], cwd=out.parent, env=env,
                           capture_output=True, timeout=timeout)
        code, stdout, stderr = r.returncode, r.stdout, r.stderr
    except subprocess.TimeoutExpired as e:
        code, stdout, stderr = "timeout", e.stdout or b"", e.stderr or b""
    (out.parent / "stdout.log").write_bytes(stdout)
    (out.parent / "stderr.log").write_bytes(stderr)
    files, error = None, None
    if code == 0:
        try:
            files = committed_set(out)
        except (OSError, ValueError, IndexError) as e:
            error = "unreadable output set: %s" % str(e).replace(str(out.parent), "<work>")
    return {"code": code, "stdout": host_report(stdout), "stderr": stderr, "files": files, "error": error}


def corpus_inventory(root: pathlib.Path) -> list:
    """(source root relative to the checkout, module path, name) of every corpus program, in
    byte order of name: conformance programs under the conformance/ root, the others under their
    own directory, as tools/cint_check.py and the 2026-10-05 hand check build them."""
    rows = []
    for top in CORPUS_ROOTS:
        listed = subprocess.run(["git", "-c", "safe.directory=*", "ls-files", "-z", "--", top + "/"],
                                cwd=root, capture_output=True, check=True).stdout.decode("utf-8")
        for name in (p for p in listed.split("\0") if p.endswith(".ci")):
            if top == "conformance":
                rows.append(("conformance", name[len("conformance/"):], name))
            else:
                parent, _, base = name.rpartition("/")
                rows.append((parent, base, name))
    return sorted(rows, key=lambda r: r[2].encode("utf-8"))


def build_chain(tc: Toolchain, inputs: pathlib.Path, work: pathlib.Path, timeout: int) -> dict:
    """B0, S1, B1, S2, B2 and S3 (SPEC-09 11.1), each stage in its own directory under work."""
    cc.RT = inputs / "rt"   # Toolchain.compile's include root: the retained runtime headers
    dirs = {name: work / name for name in ("rt", "b0", "s1", "b1", "s2", "b2", "s3")}
    for d in dirs.values():
        d.mkdir()
    times, chain = {}, {}
    t0 = time.monotonic_ns()
    rt_objs = tc.compile([inputs / "rt" / n for n in ("cint_rt.c", "cint_bridge.c", "cint_build.c")],
                         dirs["rt"], "runtime")
    seed_objs = tc.compile(sorted((inputs / "seed").glob("*.c"), key=lambda p: p.name.encode("utf-8")),
                           dirs["b0"], "B0")
    b0 = tc.link_exe(seed_objs + rt_objs[:2], dirs["b0"] / "cint-seed", "B0")
    s1 = dirs["s1"] / "main.c"
    result = tc.run([b0, "main.ci", "-o", s1], inputs / "compiler", "S1: B0 translates main.ci")
    if result.stdout or not s1.is_file() or not s1.stat().st_size:
        raise cc.BuildError("S1: the seed did not produce its generated source")
    host_obj = tc.compile([inputs / "compiler/tests/golden_host.c"], dirs["b1"], "golden host")[0]
    s1_obj = tc.compile([s1], dirs["s1"], "B1 compiler")[0]
    chain["b1"] = tc.link_exe([host_obj, s1_obj] + rt_objs, dirs["b1"] / "cintc", "B1")
    times["b1"] = (time.monotonic_ns() - t0) // 1000000
    print("b1: built", flush=True)
    rels = cc.module_order(inputs / "compiler", "main.ci")
    chain["rels"] = rels
    for stage, host, build in (("s2", "b1", "b2"), ("s3", "b2", None)):
        t0 = time.monotonic_ns()
        chain[stage] = run_host(chain[host], tc.env, inputs / "compiler", rels, dirs[stage] / "out", timeout)
        times[stage] = (time.monotonic_ns() - t0) // 1000000
        print("%s: %s by %s" % (stage, chain[stage]["stdout"].decode("ascii", "replace").split("\n")[0],
                                host.upper()), flush=True)
        if build is None:
            break
        if chain[stage]["files"] is None:
            raise cc.BuildError("%s: %s committed no output set: %s" % (stage.upper(), host.upper(),
                                (chain[stage]["stdout"] + chain[stage]["stderr"])[-2000:].decode("utf-8", "replace")))
        t0 = time.monotonic_ns()
        stage_dir = dirs[stage] / committed_ref(dirs[stage] / "out")
        c_files = sorted(stage_dir.glob("*.c"), key=lambda p: p.name.encode("utf-8"))
        objs = tc.compile(c_files, dirs[build], build.upper() + " compiler")
        chain[build] = tc.link_exe([host_obj] + objs + rt_objs, dirs[build] / "cintc", build.upper())
        times[build] = (time.monotonic_ns() - t0) // 1000000
        print("%s: built from %d C files" % (build, len(c_files)), flush=True)
    chain["b0"], chain["s1_sha256"], chain["times"] = b0, sha256(s1.read_bytes()), times
    return chain


def committed_ref(out: pathlib.Path) -> str:
    return (out / "MANIFEST.ref").read_bytes().decode("ascii").split(" ")[0]


def host_problems(what: str, run: dict) -> list:
    problems = []
    if run["code"] != 0:
        problems.append("%s: host exit status %s" % (what, run["code"]))
    if run["error"]:
        problems.append("%s: %s" % (what, run["error"]))
    if run["stderr"].strip():
        problems.append("%s: %d stderr lines (%d sanitizer reports)" % (
            what, len(run["stderr"].splitlines()), cc.sanitizer_reports(run["stderr"].decode("utf-8", "replace"))))
    return problems


def corpus_agreement(b1: pathlib.Path, b2: pathlib.Path, env: dict, src: pathlib.Path, rows: list,
                     work: pathlib.Path, jobs: int, timeout: int) -> dict:
    """FIX-03: B1 and B2 build each corpus program with the same report and output set."""
    def one(i, row):
        top, rel, name = row
        root = src / top
        rels = cc.module_order(root, rel)
        runs = {h: run_host(exe, env, root, rels, work / ("%04d" % i) / h / "out", timeout)
                for h, exe in (("b1", b1), ("b2", b2))}
        problems = host_problems(name + " B1", runs["b1"]) + host_problems(name + " B2", runs["b2"])
        same = all(runs["b1"][k] == runs["b2"][k] for k in ("code", "stdout", "files"))
        if same and not problems:
            shutil.rmtree(work / ("%04d" % i), ignore_errors=True)
        r = runs["b1"]
        report = r["stdout"].decode("ascii", "replace").split("\n")[0]
        line = "%s %s %s %s" % (name, r["code"], sha256(r["stdout"]),
                                sha256(r["files"]["MANIFEST"]) if r["files"] else "-")
        return {"name": name, "same": same, "problems": problems, "report": report, "line": line,
                "difference": None if same else first_difference(r["files"] or {}, runs["b2"]["files"] or {})}
    results = [None] * len(rows)
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = {pool.submit(one, i, row): i for i, row in enumerate(rows)}
        for done, f in enumerate(concurrent.futures.as_completed(futures), 1):
            i = futures[f]
            results[i] = f.result()
            if not results[i]["same"] or results[i]["problems"] or done % 100 == 0:
                print("corpus %d/%d %s: %s%s" % (done, len(rows), results[i]["name"], results[i]["report"],
                                                 "" if results[i]["same"] else " DIFFER"), flush=True)
    differ = [r["name"] for r in results if not r["same"]]
    problems = [p for r in results for p in r["problems"]]
    return {"programs": len(results), "equal": len(results) - len(differ),
            "built": sum(r["report"] == "build 0" for r in results),
            "not_built": sum(r["report"] != "build 0" for r in results), "differ": differ,
            "first_differences": [{"program": r["name"], **r["difference"]} for r in results
                                  if not r["same"] and r["difference"]][:20],
            "problems": problems,
            "results_sha256": sha256("".join(r["line"] + "\n" for r in results).encode("utf-8")),
            "results_method": "sha256 over one line per program in byte order of name: '<name> <host exit "
                              "status> <sha256 of the host report> <sha256 of the committed MANIFEST, or ->', "
                              "each line ended by a line feed, from B1's run"}


def fixpoint(leg: str, opt: str, helpers: str, sanitize: bool, corpus: bool, out: pathlib.Path,
             receipt_path, jobs: int, timeout: int) -> bool:
    boot.validate_output_root(ROOT, out)
    out.mkdir(parents=True, exist_ok=True)
    work = pathlib.Path(tempfile.mkdtemp(prefix="build-", dir=out)).resolve()
    started = datetime.datetime.now(datetime.timezone.utc)
    t_start = time.monotonic_ns()
    inputs = work / "inputs"
    groups = boot.snapshot_inputs(ROOT, inputs)
    groups["compiler"] = boot.compiler_closure(groups["compiler"])
    rows, corpus_files = [], {}
    if corpus:
        rows = corpus_inventory(ROOT)
        for _top, _rel, name in rows:
            corpus_files[name] = (ROOT / name).read_bytes()
            (work / "corpus-src" / name).parent.mkdir(parents=True, exist_ok=True)
            (work / "corpus-src" / name).write_bytes(corpus_files[name])
    tc = Toolchain(leg, opt, helpers, sanitize, work / "logs")
    chain = build_chain(tc, inputs, work, timeout)
    s2, s3 = chain["s2"], chain["s3"]
    problems = host_problems("S2", s2) + host_problems("S3", s3)
    difference = first_difference(s2["files"] or {}, s3["files"] or {})
    if s3["files"] is None:
        problems.append("S3: B2 committed no output set")
    fix = {"s2_report": s2["stdout"].decode("ascii", "replace").split("\n")[0],
           "s3_report": s3["stdout"].decode("ascii", "replace").split("\n")[0],
           "s3_tree_sha256": tree_digest(s3["files"]) if s3["files"] else None,
           "equal": difference is None and s3["files"] is not None, "first_difference": difference,
           "method": "S2 and S3 compared file by file, MANIFEST included, by bytes and by sha256 (FIX-01, FIX-02)"}
    agreement = None
    if corpus:
        t0 = time.monotonic_ns()
        agreement = corpus_agreement(chain["b1"], chain["b2"], tc.env, work / "corpus-src", rows,
                                     work / "corpus", jobs, timeout)
        chain["times"]["corpus"] = (time.monotonic_ns() - t0) // 1000000
        problems += agreement["problems"]
    current = {p: (ROOT / p).read_bytes() for p in corpus_files}
    if current != corpus_files or boot.compiler_closure(
            {"compiler/" + f.name: f.read_bytes() for f in (ROOT / "compiler").glob("*.ci")}) != groups["compiler"]:
        problems.append("checked sources changed during the run")
    passed = fix["equal"] and not problems and (agreement is None or not agreement["differ"])
    identity = {"profile": "cint-core-1", "runtime_contract_version": cc.runtime_contract(),
                "compiler_source_identity": sha256(boot.source_manifest(groups["compiler"])),
                "compiler_sources": records(groups["compiler"]),
                "seed_source_identity": sha256(boot.source_manifest(groups["seed"])),
                "runtime_sources": records(groups["runtime"]), "host_sources": records(groups["host"]),
                "module_order": chain["rels"],
                "corpus": {"programs": len(rows), "inventory_sha256": sha256(boot.source_manifest(corpus_files)),
                           "roots": list(CORPUS_ROOTS)} if corpus else None,
                "s2_files": records(s2["files"]), "s2_tree_sha256": tree_digest(s2["files"]),
                "s2_manifest_sha256": sha256(s2["files"]["MANIFEST"])}
    outcome = {"result": "pass" if passed else "fail", "s2_equals_s3": fix["equal"],
               "corpus_b1_equals_b2": None if agreement is None else not agreement["differ"],
               "fixpoint": fix, "corpus": agreement, "problems": problems,
               "checks": ["FIX-01", "FIX-02"] + (["FIX-03"] if corpus else [])}
    observations = {"configuration": {"leg": leg, "opt": opt, "helpers": helpers, "sanitize": sanitize},
                    "c0": tc.describe(), "commands": tc.commands, "retried": tc.retries,
                    "executables": {"b0_sha256": sha256(chain["b0"].read_bytes()),
                                    "b1_sha256": sha256(chain["b1"].read_bytes()),
                                    "b2_sha256": sha256(chain["b2"].read_bytes())},
                    "s1_sha256": chain["s1_sha256"], "wall_ms": chain["times"],
                    "total_ms": (time.monotonic_ns() - t_start) // 1000000,
                    "machine": {"system": platform.system(), "release": platform.release(),
                                "architecture": platform.machine(), "cpus": os.cpu_count()},
                    "work": str(work), "jobs": jobs, "timeout_seconds": timeout,
                    "started_utc": started.isoformat(timespec="seconds"),
                    "finished_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds")}
    receipt = {"schema": SCHEMA, "identity": identity, "outcome": outcome, "observations": observations,
               "identity_sha256": sha256(cc.canonical({"identity": identity, "outcome": outcome}))}
    data = cc.canonical(receipt) + b"\n"
    (work / "fixpoint.json").write_bytes(data)
    if difference is not None:
        print("first difference: %s" % json.dumps(difference, sort_keys=True))
    print("fixpoint: %s, S2 %s S3, %d files, tree %s, MANIFEST %s" % (
        "pass" if fix["equal"] else "fail", "=" if fix["equal"] else "!=", len(identity["s2_files"]),
        identity["s2_tree_sha256"][:8], identity["s2_manifest_sha256"][:8]))
    if agreement is not None:
        print("corpus: %s, %d programs, %d equal (%d built, %d not built), %d differ" % (
            "pass" if not agreement["differ"] and not agreement["problems"] else "fail", agreement["programs"],
            agreement["equal"], agreement["built"], agreement["not_built"], len(agreement["differ"])))
    for p in problems[:10]:
        print("problem: %s" % p, file=sys.stderr)
    print("identity: %s" % receipt["identity_sha256"])
    if receipt_path is not None:
        receipt_path.parent.mkdir(parents=True, exist_ok=True)
        receipt_path.write_bytes(data)
        shown = receipt_path.resolve()
        print("receipt: %s" % (shown.relative_to(ROOT).as_posix() if ROOT in shown.parents else shown), flush=True)
    return passed


def compare(paths: list) -> bool:
    """FIX-04 over fixpoint receipts: each intact and passing, and one identity_sha256 for all."""
    identities, problems = set(), []
    for path in paths:
        receipt = json.loads(pathlib.Path(path).read_bytes())
        if receipt.get("schema") != SCHEMA:
            problems.append("%s: not a %s" % (path, SCHEMA))
            continue
        digest = sha256(cc.canonical({"identity": receipt["identity"], "outcome": receipt["outcome"]}))
        if digest != receipt["identity_sha256"]:
            problems.append("%s: identity_sha256 does not match its identity and outcome" % path)
        if receipt["outcome"]["result"] != "pass":
            problems.append("%s: result %s" % (path, receipt["outcome"]["result"]))
        identities.add(receipt["identity_sha256"])
        c = receipt["observations"]["configuration"]
        print("%s: %s %s-%s-%s%s, identity %s" % (path, receipt["outcome"]["result"], c["leg"], c["opt"],
                                                  c["helpers"], "-san" if c["sanitize"] else "", digest[:16]))
    for p in problems:
        print("problem: %s" % p, file=sys.stderr)
    passed = not problems and len(identities) == 1
    print("fix-04: %s, %d receipts, %d receipt %s" % ("pass" if passed else "fail", len(paths), len(identities),
                                                      "identity" if len(identities) == 1 else "identities"))
    return passed


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--leg", choices=("msvc", "gcc", "clang", "apple-clang"))
    ap.add_argument("--opt", choices=("0", "2"), default="0")
    ap.add_argument("--helpers", choices=("portable", "builtin"), default="portable")
    ap.add_argument("--sanitize", action="store_true", help="ASan and UBSan (gcc and clang)")
    ap.add_argument("--no-corpus", action="store_true", help="check S2 = S3 only, without FIX-03")
    ap.add_argument("--out", type=pathlib.Path, help="work directory (default <build>/fixpoint/<tag>)")
    ap.add_argument("--receipt", type=pathlib.Path, help="default results/cint/fixpoint/fixpoint-<tag>.json")
    ap.add_argument("--no-receipt", action="store_true", help="write the receipt only into the work directory")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    ap.add_argument("--timeout", type=int, default=600, help="seconds per host run")
    ap.add_argument("--compare", nargs="+", metavar="RECEIPT", help="check one identity across receipts (FIX-04)")
    args = ap.parse_args(argv)
    if args.compare:
        return 0 if compare(args.compare) else 1
    if args.leg is None:
        ap.error("--leg is required unless --compare is given")
    if args.sanitize and args.leg in ("msvc", "apple-clang"):
        ap.error("--sanitize is for the gcc and clang legs")
    if args.receipt is not None and args.no_receipt:
        ap.error("--receipt and --no-receipt exclude each other")
    if args.jobs < 1 or args.timeout < 1:
        ap.error("--jobs and --timeout must be positive")
    tag = "%s-%s-%s%s" % (args.leg, args.opt, args.helpers, "-san" if args.sanitize else "")
    if os.name == "nt" and args.leg != "msvc":
        command = ["wsl", "-d", cc.WSL_DISTRO, "--", "python3", cc.wsl_path(pathlib.Path(__file__).resolve()),
                   "--leg", args.leg, "--opt", args.opt, "--helpers", args.helpers, "--jobs", str(args.jobs),
                   "--timeout", str(args.timeout)] + [f for f, on in (("--sanitize", args.sanitize),
                                                                    ("--no-corpus", args.no_corpus),
                                                                    ("--no-receipt", args.no_receipt)) if on]
        for option in ("out", "receipt"):
            if getattr(args, option) is not None:
                command += ["--" + option, cc.wsl_path(getattr(args, option).resolve())]
        return subprocess.run(command, cwd=ROOT, env=cc.wsl_env()).returncode
    if args.leg == "msvc" and os.name != "nt":
        ap.error("the msvc leg requires Windows")
    receipt = None if args.no_receipt else (args.receipt or ROOT / "results/cint/fixpoint" / ("fixpoint-%s.json" % tag))
    try:
        passed = fixpoint(args.leg, args.opt, args.helpers, args.sanitize, not args.no_corpus,
                          (args.out or cc.build_root() / "fixpoint" / tag).resolve(),
                          receipt.resolve() if receipt else None, args.jobs, args.timeout)
    except (OSError, ValueError, KeyError, cc.BuildError, subprocess.SubprocessError) as error:
        print("fixpoint: %s" % error, file=sys.stderr)
        return 1
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
