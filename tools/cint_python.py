"""Python receipts: the Python cases of SPEC-03 section 9 on one leg (SPEC-09 RCPT-11).

    python tools/cint_python.py --leg msvc|gcc|clang|apple-clang [--cint EXE]
        [--receipt FILE | --no-receipt] [--timeout SECONDS]
    python tools/cint_python.py --compare RECEIPT [RECEIPT ...]
    python tools/cint_python.py --list

Run it with the Python that has NumPy, for example a virtual environment's. It takes the leg's
`cint` from <build>/boot/<leg> when that bootstrap was built from this tree's inputs, and
otherwise bootstraps it there (tools/cint_bootstrap.py); --cint names one to use, which must
also be built from this tree. Then it runs python/tests with that `cint` (CINT_EXE), runs
examples/matmul_i8.py and compares its stdout with examples/matmul_i8.stdout, and measures
CR-09: NumPy's buffer format for `int64` and `longlong` and the element type `cint.borrow`
gives each.

The receipt keeps identity and outcome apart from the host's observations (RCPT-03). Its
identity hashes every tracked file of python/ (box 10 default BX10-21: python/ enters the
receipt identity here), the compiler source identity, the runtime, bridge and CLI sources,
ref/cint_ref/, conformance/, and the demonstration's files. Its outcome lists the required
tests that passed, the SPEC-03 section 9 cases of box 10 they cover (box 10 note, section
3.1), the demonstration's result and the element types of CR-09. A test belongs to a case
when the first line of its docstring, or else of its class's, begins "Case <id>" or "Cases
<id>, <id> and <id>". Every test is required except those of OPTIONAL, which a host may lack
the means to run; their results are observations, and a failure among them still fails the
receipt. A required test that is skipped, for example because NumPy is not installed, fails
the receipt. So two legs that pass share one identity_sha256, and --compare checks that over
a set of receipts. Interpreter, NumPy and C compiler versions, NumPy's buffer formats, paths
and times are observations.

The receipt defaults to results/cint/box10/python-<leg>.json. --list prints each test with the
cases it covers, without running anything.
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import io
import json
import os
import pathlib
import platform
import re
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
SCHEMA = "cint-python-receipt-1"
# Box 10 note, section 3.1, item 2: the Python cases of SPEC-03 section 9 that box 10 owns.
CASES = ("12a", "13", "16", "16a", "16b", "17", "17a", "19", "20", "20a", "21", "23", "23a", "23b",
         "24", "33")
# Tests a host may lack the means to run; each still has to pass where it runs.
OPTIONAL = {
    "tests.test_borrow.Case16b.test_torch_cpu_int64": "PyTorch, where it is installed (case 16b)",
    "tests.test_borrow.Arguments.test_flags_seen_by_the_exporter": "CPython 3.12 or later",
    "tests.test_examples.NewcomerTask3P.test_answers": "the newcomer check's script, where it is in the tree",
}
DEMO = ("examples/matmul_i8.ci", "examples/matmul_i8.py", "examples/matmul_i8.stdout",
        "docs/tutorials/newcomer/p1_copies.py")
_CASE_LINE = re.compile(r"Cases? (\d+[a-z]?(?:(?:,? and |, )\d+[a-z]?)*)\b")


def cases_of(doc: str | None) -> list:
    """The case ids a docstring's first line opens with: "Case 16: ..." gives ["16"], "Cases 23,
    23a and 24: ..." gives ["23", "23a", "24"], and a line that does not open so gives []."""
    lines = (doc or "").strip().splitlines()
    m = _CASE_LINE.match(lines[0]) if lines else None
    return re.split(r",? and |, ", m.group(1)) if m else []


# --------------------------------------------------------------------------- the worker

def _walk(suite):
    import unittest
    for test in suite:
        if isinstance(test, unittest.TestSuite):
            yield from _walk(test)
        else:
            yield test


def _discover():
    """python/tests, as `python -m unittest discover -s tests -t .` from python/ finds it."""
    import unittest
    sys.path[:] = [str(ROOT / "python")] + [p for p in sys.path if p and pathlib.Path(p).resolve() != TOOLS]
    os.chdir(ROOT / "python")
    suite = unittest.defaultTestLoader.discover("tests", top_level_dir=".")
    return suite, {t.id(): cases_of(getattr(t, "_testMethodDoc", None)) or cases_of(type(t).__doc__)
                   for t in _walk(suite)}


def ascii_text(text):
    """Receipt strings are ASCII (RCPT-02); anything else is escaped."""
    return None if text is None else str(text).encode("ascii", "backslashreplace").decode("ascii")


def _last_line(text) -> str | None:
    """A skip reason, or the last line of a traceback: the exception and its message."""
    return None if text is None else ascii_text((str(text).strip().splitlines() or [""])[-1][:300])


def worker(out: pathlib.Path, listing: bool) -> int:
    """Runs in a separate interpreter: discovers python/tests, runs it unless listing, measures
    CR-09, and writes what it saw to out as JSON."""
    import unittest
    suite, cases = _discover()
    seen = {}

    class Result(unittest.TestResult):
        def _put(self, test, result, detail=None):
            prior = seen.get(test.id())
            if prior is None or prior["result"] == "pass":
                seen[test.id()] = {"result": result, "detail": _last_line(detail)}

        def addSuccess(self, test):
            self._put(test, "pass")

        def addFailure(self, test, err):
            self._put(test, "fail", self._exc_info_to_string(err, test))

        def addError(self, test, err):
            self._put(test, "error", self._exc_info_to_string(err, test))

        def addSkip(self, test, reason):
            self._put(test, "skip", reason)

        def addExpectedFailure(self, test, err):
            self._put(test, "fail", "an expected failure")

        def addUnexpectedSuccess(self, test):
            self._put(test, "fail", "an unexpected success")

        def addSubTest(self, test, subtest, err):
            if err is not None:
                failed = issubclass(err[0], test.failureException)
                seen[test.id()] = {"result": "fail" if failed else "error",
                                   "detail": _last_line(self._exc_info_to_string(err, test))}

    report = {"cases": cases, "tests": seen, "versions": {}, "cr09": None}
    if not listing:
        stream = io.StringIO()
        saved = sys.stdout, sys.stderr
        sys.stdout = sys.stderr = stream
        try:
            suite.run(Result())
        finally:
            sys.stdout, sys.stderr = saved
        versions = {"implementation": platform.python_implementation(), "python": platform.python_version()}
        for name in ("numpy", "torch"):
            try:
                versions[name] = __import__(name).__version__
            except ImportError:
                versions[name] = None
        report["versions"] = versions
        if versions["numpy"] is not None:
            import numpy
            import cint
            arrays = {"int64": numpy.zeros(2, dtype=numpy.int64), "longlong": numpy.zeros(2, dtype=numpy.longlong)}
            report["cr09"] = {name: {"format": memoryview(a).format, "elem": cint.borrow(a).elem}
                              for name, a in arrays.items()}
    out.write_text(json.dumps(report, sort_keys=True), encoding="utf-8")
    return 0


def run_worker(env: dict, listing: bool, timeout: int) -> dict:
    with tempfile.TemporaryDirectory(prefix="cint-python-") as tmp:
        out = pathlib.Path(tmp) / "suite.json"
        proc = subprocess.run([sys.executable, str(pathlib.Path(__file__).resolve()),
                               "--worker-list" if listing else "--worker", str(out)],
                              cwd=ROOT, env=env, capture_output=True, text=True, timeout=timeout)
        if proc.returncode != 0 or not out.is_file():
            raise RuntimeError("the test worker exited %d: %s" % (proc.returncode, (proc.stderr or proc.stdout)[-2000:]))
        return json.loads(out.read_text(encoding="utf-8"))


# --------------------------------------------------------------------------- the receipt

def _tools():
    if str(TOOLS) not in sys.path:
        sys.path.insert(0, str(TOOLS))
    import cint_bootstrap as boot
    import cint_check as cc
    return boot, cc


def toolchain_rows(cint: pathlib.Path) -> dict:
    rows = {}
    lines = (cint.parent / "cint.toolchain").read_text(encoding="utf-8").splitlines()
    for line in lines[1:]:
        key, _, value = line.partition(" ")
        rows.setdefault(key, []).append(value)
    return rows


def tree_problem(cint: pathlib.Path) -> str | None:
    """Why the cint at that path was not built from this tree's inputs, or None: its bootstrap's
    retained snapshot (inputs/) must hold exactly the files the bootstrap takes from this tree."""
    boot, cc = _tools()
    try:
        rows = toolchain_rows(cint)
        if rows["executable_sha256"][0] != cc.sha256(cint.read_bytes()):
            return "%s is not the executable its cint.toolchain describes" % cint
        inputs = pathlib.Path(rows["include"][0]).parent
        with tempfile.TemporaryDirectory(prefix="cint-python-") as tmp:
            groups = boot.snapshot_inputs(ROOT, pathlib.Path(tmp))
        want = {rel: data for files in groups.values() for rel, data in files.items()}
        have = {p.relative_to(inputs).as_posix(): p for p in inputs.rglob("*") if p.is_file()}
    except (OSError, KeyError, IndexError, ValueError) as error:
        return "the bootstrap of %s cannot be read: %s" % (cint, error)
    changed = sorted(r for r in set(want) | set(have) if r not in want or r not in have
                     or have[r].read_bytes() != want[r])
    if changed:
        return "%s was bootstrapped from other inputs than this tree's (%s%s)" % (
            cint, ", ".join(changed[:3]), ", ..." if len(changed) > 3 else "")
    return None


def leg_cint(leg: str, given: pathlib.Path | None) -> tuple:
    """The leg's cint and whether it was bootstrapped by this run."""
    boot, cc = _tools()
    if given is not None:
        problem = tree_problem(given)
        if problem:
            raise ValueError(problem)
        return given, False
    out = cc.build_root() / "boot" / leg
    exe = out / ("cint.exe" if leg == "msvc" else "cint")
    if exe.is_file() and tree_problem(exe) is None:
        return exe, False
    boot.bootstrap(leg, out)
    problem = tree_problem(exe)
    if problem:
        raise ValueError(problem)
    return exe, True


def records(paths) -> list:
    return [{"path": p, "bytes": (ROOT / p).stat().st_size, "sha256": hashlib.sha256((ROOT / p).read_bytes()).hexdigest()}
            for p in sorted(paths, key=lambda s: s.encode("utf-8"))]


def identity_of() -> dict:
    boot, cc = _tools()
    sources = {"compiler/" + f.name: f.read_bytes() for f in sorted((ROOT / "compiler").glob("*.ci"))}
    rt = ROOT / "rt"
    python_tree, python_files = cc.tree_digest("python/", "python/")
    ref_tree, ref_files = cc.tree_digest("ref/cint_ref/", "ref/")
    suite, suite_files = cc.tree_digest("conformance/", "conformance/")
    return {"profile": "cint-core-1", "runtime_contract_version": cc.runtime_contract(),
            "compiler_source_identity": cc.sha256(boot.source_manifest(boot.compiler_closure(sources))),
            "runtime_header_sha256": cc.sha256((rt / "cint_rt.h").read_bytes()),
            "runtime_library_sha256": cc.files_digest([rt / "cint_rt.c", rt / "cint_rt_internal.h",
                                                         rt / "cint_mem.c", rt / "cint_mem.h"]),
            "bridge_sha256": cc.files_digest([rt / "cint_bridge.c", rt / "cint_bridge.h",
                                              rt / "cint_bridge_internal.h", rt / "cint_build.c"]),
            "cli_sha256": cc.files_digest(sorted((ROOT / "cli").glob("*.c")) + sorted((ROOT / "cli").glob("*.h"))),
            "python_tree_sha256": python_tree, "python_tree_files": python_files,
            "cint_ref_tree_sha256": ref_tree, "cint_ref_tree_files": ref_files,
            "conformance_tree_sha256": suite, "conformance_tree_files": suite_files,
            "tree_method": "sha256 over the lines '<sha256>  <path>\\n' of the tracked files, paths "
                           "relative to the tree, sorted by bytes",
            "demonstration": records(p for p in DEMO if (ROOT / p).exists()), "required_cases": list(CASES)}


def outcome_of(suite: dict, demo: dict) -> tuple:
    """The outcome (identical on every leg that passes) and the optional tests' results."""
    tests, cases = suite["tests"], suite["cases"]
    passed, not_passed, optional, problems = [], [], [], []
    for test in sorted(set(cases) | set(tests), key=lambda s: s.encode("utf-8")):
        got = tests.get(test, {"result": "not run", "detail": None})
        if test not in cases:
            problems.append("%s: %s, %s" % (test, got["result"], got["detail"]))
        elif test in OPTIONAL:
            optional.append({"test": test, "result": got["result"], "detail": got["detail"]})
            if got["result"] not in ("pass", "skip"):
                not_passed.append({"test": test, "result": got["result"], "detail": got["detail"]})
        elif got["result"] == "pass":
            passed.append(test)
        else:
            not_passed.append({"test": test, "result": got["result"], "detail": got["detail"]})
    by_case = {c: sum(1 for t in passed if c in cases[t]) for c in CASES}
    failing = {c for t in not_passed for c in cases.get(t["test"], ())}
    problems += ["case %s has no passing required test" % c for c in CASES if not by_case[c]]
    cr09 = suite["cr09"]
    if cr09 is None:
        problems.append("NumPy is not installed, so CR-09 is not measured")
    elif any(v["elem"] != "I64" for v in cr09.values()):
        problems.append("CR-09: %s" % json.dumps(cr09, sort_keys=True))
    if not demo["stdout_equal"]:
        problems.append("examples/matmul_i8.py: exit status %d, or stdout not examples/matmul_i8.stdout"
                        % demo["exit_status"])
    passes = not not_passed and not problems and not failing
    outcome = {"result": "pass" if passes else "fail", "passed": passed, "not_passed": not_passed,
               "cases": {c: {"tests": by_case[c], "pass": bool(by_case[c]) and c not in failing} for c in CASES},
               "matmul_i8": {"exit_status": demo["exit_status"], "stdout_equal": demo["stdout_equal"]},
               "cr09": None if cr09 is None else {name: v["elem"] for name, v in cr09.items()},
               "problems": problems}
    return outcome, optional


def demonstration(env: dict, timeout: int) -> dict:
    proc = subprocess.run([sys.executable, str(ROOT / "examples" / "matmul_i8.py")], cwd=ROOT, env=env,
                          capture_output=True, timeout=timeout)
    want = (ROOT / "examples" / "matmul_i8.stdout").read_bytes()
    stdout = proc.stdout.replace(b"\r\n", b"\n") if os.name == "nt" else proc.stdout
    return {"exit_status": proc.returncode, "stdout_equal": proc.returncode == 0 and stdout == want,
            "stdout_sha256": hashlib.sha256(stdout).hexdigest(),
            "stderr": ascii_text(proc.stderr.decode("utf-8", "replace")[-2000:])}


def python_receipt(leg: str, given: pathlib.Path | None, receipt_path: pathlib.Path | None, timeout: int) -> bool:
    boot, cc = _tools()
    started = datetime.datetime.now(datetime.timezone.utc)
    t0 = time.monotonic_ns()
    before = identity_of()
    cint, bootstrapped = leg_cint(leg, given)
    times = {"cint": (time.monotonic_ns() - t0) // 1000000}
    env = dict(os.environ, CINT_EXE=str(cint), CINT_BUILD=str(cc.build_root()))
    env["PYTHONPATH"] = os.pathsep.join(p for p in (str(ROOT / "python"), os.environ.get("PYTHONPATH")) if p)
    t1 = time.monotonic_ns()
    suite = run_worker(env, False, timeout)
    times["tests"] = (time.monotonic_ns() - t1) // 1000000
    t1 = time.monotonic_ns()
    demo = demonstration(env, timeout)
    times["matmul_i8"] = (time.monotonic_ns() - t1) // 1000000
    identity = identity_of()
    outcome, optional = outcome_of(suite, demo)
    if identity != before:
        outcome["problems"].append("checked sources changed during the run")
        outcome["result"] = "fail"
    rows = toolchain_rows(cint)
    bootstrap_receipt = cint.parent / "bootstrap.json"
    counts = {}
    for t in suite["tests"].values():
        counts[t["result"]] = counts.get(t["result"], 0) + 1
    observations = {
        "configuration": {"leg": leg},
        "c0": {"cc": rows.get("cc", [None])[0], "cc_version": rows.get("cc_version", [None])[0],
               "flags": rows.get("flag", [])},
        "cint": {"path": ascii_text(cint), "sha256": cc.sha256(cint.read_bytes()), "bootstrapped": bootstrapped,
                 "bootstrap_identity_sha256": json.loads(bootstrap_receipt.read_bytes())["identity_sha256"]
                 if bootstrap_receipt.is_file() else None},
        "python": dict(suite["versions"], executable=ascii_text(sys.executable)),
        "cr09_formats": None if suite["cr09"] is None else {k: v["format"] for k, v in suite["cr09"].items()},
        "optional": optional, "counts": dict(sorted(counts.items()), discovered=len(suite["cases"])),
        "matmul_i8": {"stdout_sha256": demo["stdout_sha256"], "stderr": demo["stderr"]},
        "machine": {"system": platform.system(), "release": platform.release(),
                    "architecture": platform.machine(), "host": cc.host_os()},
        "wall_ms": times, "total_ms": (time.monotonic_ns() - t0) // 1000000,
        "started_utc": started.isoformat(timespec="seconds"),
        "finished_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds")}
    receipt = {"schema": SCHEMA, "identity": identity, "outcome": outcome, "observations": observations,
               "identity_sha256": cc.sha256(cc.canonical({"identity": identity, "outcome": outcome}))}
    data = cc.canonical(receipt) + b"\n"
    skipped = [o for o in optional if o["result"] == "skip"]
    print("python %s: %s, %d required tests passed, %d not, %d of %d cases, %d optional (%d skipped), "
          "matmul_i8 stdout %s, CR-09 %s" % (
              leg, outcome["result"], len(outcome["passed"]), len(outcome["not_passed"]),
              sum(1 for c in outcome["cases"].values() if c["pass"]), len(CASES), len(optional), len(skipped),
              "equal" if demo["stdout_equal"] else "differs",
              "not measured" if suite["cr09"] is None else ", ".join(
                  "%s %s -> %s" % (k, v["format"], v["elem"]) for k, v in sorted(suite["cr09"].items()))))
    for o in skipped:
        print("optional skipped: %s: %s" % (o["test"], o["detail"]))
    for item in outcome["not_passed"][:20]:
        print("not passed: %s: %s: %s" % (item["test"], item["result"], item["detail"]), file=sys.stderr)
    for p in outcome["problems"][:20]:
        print("problem: %s" % p, file=sys.stderr)
    print("identity: %s" % receipt["identity_sha256"])
    if receipt_path is not None:
        receipt_path.parent.mkdir(parents=True, exist_ok=True)
        receipt_path.write_bytes(data)
        shown = receipt_path.resolve()
        print("receipt: %s" % (shown.relative_to(ROOT).as_posix() if ROOT in shown.parents else shown), flush=True)
    return outcome["result"] == "pass"


def compare(paths: list) -> bool:
    """Each receipt intact and passing, and one identity_sha256 for all."""
    boot, cc = _tools()
    identities, problems = set(), []
    for path in paths:
        receipt = json.loads(pathlib.Path(path).read_bytes())
        if receipt.get("schema") != SCHEMA:
            problems.append("%s: not a %s" % (path, SCHEMA))
            continue
        digest = cc.sha256(cc.canonical({"identity": receipt["identity"], "outcome": receipt["outcome"]}))
        if digest != receipt["identity_sha256"]:
            problems.append("%s: identity_sha256 does not match its identity and outcome" % path)
        if pathlib.Path(path).read_bytes() != cc.canonical(receipt) + b"\n":
            problems.append("%s: not its RFC 8785 serialization followed by one LF" % path)
        if receipt["outcome"]["result"] != "pass":
            problems.append("%s: result %s" % (path, receipt["outcome"]["result"]))
        identities.add(digest)
        o = receipt["observations"]
        print("%s: %s %s, %s %s, NumPy %s, int64 format %s, identity %s" % (
            path, receipt["outcome"]["result"], o["configuration"]["leg"], o["python"]["implementation"],
            o["python"]["python"], o["python"]["numpy"], (o["cr09_formats"] or {}).get("int64"), digest[:16]))
    for p in problems:
        print("problem: %s" % p, file=sys.stderr)
    passed = bool(paths) and not problems and len(identities) == 1
    print("python receipts: %s, %d receipts, %d receipt %s" % (
        "pass" if passed else "fail", len(paths), len(identities), "identity" if len(identities) == 1 else "identities"))
    return passed


def main(argv=None) -> int:
    argv = sys.argv[1:] if argv is None else argv
    if argv[:1] in (["--worker"], ["--worker-list"]) and len(argv) == 2:
        return worker(pathlib.Path(argv[1]), argv[0] == "--worker-list")
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--leg", choices=("msvc", "gcc", "clang", "apple-clang"))
    ap.add_argument("--cint", type=pathlib.Path, help="a cint bootstrapped from this tree, used as it is")
    ap.add_argument("--receipt", type=pathlib.Path, help="default results/cint/box10/python-<leg>.json")
    ap.add_argument("--no-receipt", action="store_true", help="print the summary and write no receipt")
    ap.add_argument("--timeout", type=int, default=1800, help="seconds for the tests and for the demonstration")
    ap.add_argument("--compare", nargs="+", metavar="RECEIPT", help="check one identity across receipts")
    ap.add_argument("--list", action="store_true", help="print each test with its cases, and run nothing")
    args = ap.parse_args(argv)
    if args.compare:
        return 0 if compare(args.compare) else 1
    if args.list:
        suite = run_worker(dict(os.environ), True, args.timeout)
        for test, cases in sorted(suite["cases"].items(), key=lambda kv: kv[0].encode("utf-8")):
            print("%s%s%s" % (test, " " + ",".join(cases) if cases else "",
                              " optional: " + OPTIONAL[test] if test in OPTIONAL else ""))
        return 0
    if args.leg is None:
        ap.error("--leg is required unless --compare or --list is given")
    if (args.leg == "msvc") != (os.name == "nt"):
        ap.error("the msvc leg runs on Windows, and the other legs elsewhere (on Windows, in WSL with its own Python)")
    if args.receipt is not None and args.no_receipt:
        ap.error("--receipt and --no-receipt exclude each other")
    receipt = None if args.no_receipt else (args.receipt or ROOT / "results/cint/box10" / ("python-%s.json" % args.leg))
    boot, cc = _tools()
    try:
        passed = python_receipt(args.leg, args.cint.resolve() if args.cint else None,
                                receipt.resolve() if receipt else None, args.timeout)
    except (OSError, ValueError, KeyError, RuntimeError, cc.BuildError, subprocess.SubprocessError) as error:
        print("python: %s" % error, file=sys.stderr)
        return 1
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
