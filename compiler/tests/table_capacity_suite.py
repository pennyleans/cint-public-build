"""Module tables at their capacity boundary through the real passes (OQ-179)."""

import hashlib
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
TESTS = ROOT / "compiler" / "tests"
REQUIRES = ["compiler/scan.ci", "compiler/parse.ci", "compiler/resolve.ci", "compiler/check.ci",
            "compiler/decl.ci", "compiler/lowering.ci", "compiler/tests/table_capacity.ci",
            "compiler/tests/table_capacity_host.c"]
RECORDS = ("scan_5_Token", "parse_4_Node", "parse_5_Frame", "limits_12_CompilerDiag",
           "resolve_3_Sem", "resolve_3_Sym", "resolve_4_Name", "limits_4_Decl",
           "lowering_3_Sir", "lowering_7_SirFunc", "lowering_7_SirSlot", "lowering_6_LState",
           "back_x5Fc_4_PMod")
CASES = 79


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


def build(tc, tools, work: pathlib.Path, emitted: dict, cc):
    c_file = work / "table_capacity.c"
    code, err = cc.run_seed(tools["seed"], ROOT / "compiler", "tests/table_capacity.ci", c_file)
    if code != 0 or cc.sanitizer_reports(err):
        raise ValueError("seed: exit %d: %s" % (code, " ".join(err.split())[:400]))
    text = c_file.read_bytes()
    digest = hashlib.sha256(text).hexdigest()
    if b"\r" in text:
        raise ValueError("emitted C holds a CR byte")
    if emitted.setdefault("tests/table_capacity.ci", digest) != digest:
        raise ValueError("emitted C differs from first configuration")
    ids = records(text.decode("ascii"))
    objs = tc.compile([c_file, TESTS / "table_capacity_host.c"], work, "table_capacity")
    exe = tc.link_exe(objs + [tools["rt_obj"]], work / "table-capacity-host", "table_capacity")
    return exe, ids


def run(tc, tools, work: pathlib.Path, emitted: dict, _prepared, cc) -> tuple:
    try:
        exe, ids = build(tc, tools, work, emitted, cc)
    except (cc.BuildError, ValueError) as e:
        return 0, CASES, ["build: %s" % " ".join(str(e).split())[:600]]
    env = dict(tc.env)
    env["ASAN_OPTIONS"] = "detect_leaks=1:abort_on_error=0"
    env["UBSAN_OPTIONS"] = "print_stacktrace=1:halt_on_error=1"
    r = subprocess.run([str(exe), *ids], cwd=work, env=env, capture_output=True)
    stderr = r.stderr.decode("utf-8", "replace")
    stdout = r.stdout.decode("utf-8", "replace").strip()
    if r.returncode != 0 or cc.sanitizer_reports(stderr) or stdout.splitlines()[-1:] != [
            "table capacity: %d of %d pass" % (CASES, CASES)]:
        return 0, CASES, ["host exit %d: %s; %s" % (r.returncode, stderr.strip()[:400], stdout[-600:])]
    return CASES, CASES, []
