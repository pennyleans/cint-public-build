"""Real checked-module C9001 boundaries for the physical declaration view."""

import hashlib
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
TESTS = ROOT / "compiler" / "tests"
REQUIRES = ["compiler/scan.ci", "compiler/parse.ci", "compiler/resolve.ci",
            "compiler/check.ci", "compiler/decl.ci", "compiler/tests/decl_capacity.ci",
            "compiler/tests/decl_capacity_host.c"]
RECORDS = ("scan_5_Token", "parse_4_Node", "parse_5_Frame", "limits_12_CompilerDiag",
           "resolve_3_Sem", "resolve_3_Sym", "resolve_4_Name", "limits_4_Decl")
CASES = 18


def prepare(_):
    return None


def records(c_text: str) -> list:
    m = re.search(r"cg_records\[\d+\] = \{\n(.*?)\n\};", c_text, re.S)
    names = re.findall(r"sizeof\((ci_\w+)\)\)", m.group(1)) if m else []
    ids = []
    for key in RECORDS:
        found = [i for i, name in enumerate(names) if name.endswith("_" + key)]
        if len(found) != 1:
            raise ValueError("record %s not found in emitted C" % key)
        ids.append(str(found[0]))
    return ids


def run(tc, tools, work: pathlib.Path, emitted: dict, _prepared, cc) -> tuple:
    c_file = work / "decl_capacity.c"
    code, err = cc.run_seed(tools["seed"], ROOT / "compiler", "tests/decl_capacity.ci", c_file)
    if code != 0 or cc.sanitizer_reports(err):
        return 0, CASES, ["seed: exit %d: %s" % (code, " ".join(err.split())[:400])]
    text = c_file.read_bytes()
    digest = hashlib.sha256(text).hexdigest()
    if b"\r" in text:
        return 0, CASES, ["emitted C holds a CR byte"]
    if emitted.setdefault("tests/decl_capacity.ci", digest) != digest:
        return 0, CASES, ["emitted C differs from first configuration"]
    try:
        ids = records(text.decode("ascii"))
        objs = tc.compile([c_file, TESTS / "decl_capacity_host.c"], work, "decl_capacity")
        exe = tc.link_exe(objs + [tools["rt_obj"]], work / "decl-capacity-host", "decl_capacity")
    except (cc.BuildError, ValueError) as e:
        return 0, CASES, ["build: %s" % " ".join(str(e).split())[:600]]
    env = dict(tc.env)
    env["ASAN_OPTIONS"] = "detect_leaks=1:abort_on_error=0"
    env["UBSAN_OPTIONS"] = "print_stacktrace=1:halt_on_error=1"
    r = subprocess.run([str(exe), *ids], cwd=work, env=env, capture_output=True)
    stderr = r.stderr.decode("utf-8", "replace")
    stdout = r.stdout.decode("utf-8", "replace").strip()
    if r.returncode != 0 or cc.sanitizer_reports(stderr) or stdout != "decl capacity: 18 cases pass":
        return 0, CASES, ["host exit %d: %s; %s" % (r.returncode, stderr.strip()[:400], stdout[:200])]
    return CASES, CASES, []
