"""Tests real parser allocation with physical short Node views."""

import hashlib
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
TESTS = ROOT / "compiler" / "tests"
REQUIRES = ["compiler/limits.ci", "compiler/scan.ci", "compiler/parse.ci",
            "compiler/tests/parse_capacity.ci", "compiler/tests/parse_capacity_host.c"]
RECORDS = ("scan_5_Token", "parse_4_Node", "parse_5_Frame", "limits_12_CompilerDiag")
CASES = 56


def prepare(_):
    return None


def records(c_text: str) -> list:
    match = re.search(r"cg_records\[\d+\] = \{\n(.*?)\n\};", c_text, re.S)
    names = re.findall(r"sizeof\((ci_\w+)\)\)", match.group(1)) if match else []
    ids = []
    for key in RECORDS:
        found = [i for i, name in enumerate(names) if name.endswith("_" + key)]
        if len(found) != 1:
            raise ValueError("record %s not found in emitted C" % key)
        ids.append(str(found[0]))
    return ids


def run(tc, tools, work: pathlib.Path, emitted: dict, _prepared, cc) -> tuple:
    c_file = work / "parse_capacity.c"
    code, err = cc.run_seed(tools["seed"], ROOT / "compiler", "tests/parse_capacity.ci", c_file)
    if code != 0 or cc.sanitizer_reports(err):
        return 0, CASES, ["seed: exit %d: %s" % (code, " ".join(err.split())[:400])]
    text = c_file.read_bytes()
    digest = hashlib.sha256(text).hexdigest()
    if b"\r" in text:
        return 0, CASES, ["emitted C holds a CR byte"]
    if emitted.setdefault("tests/parse_capacity.ci", digest) != digest:
        return 0, CASES, ["emitted C differs from first configuration"]
    try:
        ids = records(text.decode("ascii"))
        objs = tc.compile([c_file, TESTS / "parse_capacity_host.c"], work, "parse_capacity")
        exe = tc.link_exe(objs + [tools["rt_obj"]], work / "parse-capacity-host", "parse_capacity")
    except (cc.BuildError, ValueError) as error:
        return 0, CASES, ["build: %s" % " ".join(str(error).split())[:600]]
    env = dict(tc.env)
    env["ASAN_OPTIONS"] = "detect_leaks=1:abort_on_error=0"
    env["UBSAN_OPTIONS"] = "print_stacktrace=1:halt_on_error=1"
    result = subprocess.run([str(exe), *ids], cwd=work, env=env, capture_output=True)
    stderr = result.stderr.decode("utf-8", "replace")
    stdout = result.stdout.decode("utf-8", "replace").strip()
    if result.returncode != 0 or cc.sanitizer_reports(stderr) or stdout.splitlines()[-1:] != [
            "parse node capacity: 56 of 56 pass"]:
        return 0, CASES, ["host exit %d: %s; %s" % (result.returncode, stderr.strip()[:400], stdout[-600:])]
    return CASES, CASES, []
