"""Run the T0 checks and write results/cint/t0/receipt.json (SPEC-09 RCPT-02, RCPT-03).

This is the first part of the receipt assembly that SPEC-09 RCPT-05 assigns to
`tools/reproduce.py`; it moves there when that tool exists. It runs, from the
repository root:

1. the `cint_ref` unit tests and the generator tests;
2. `cint_ref cif1` over the anchors and every generated CIF-1 table;
3. `oracle3.py`, a third derivation of the generated tables;
4. `ref_tables.py`, the table programs through the `cint_ref` program layer;
5. `gen_expect.py --check` over the program cases;
6. the MANIFEST digests, and a full regeneration into a temporary directory
   compared byte for byte with the MANIFEST;

and writes the receipt as canonical JSON (sorted keys, no spaces, ASCII, LF).
`conformance.cases` counts the CIF-1 records plus the distinct `case` names of
the `.expect` files (ref/OPEN.md I-3: each entry of a multi-entry case is a
case). The receipt holds no host path and no local time.

Usage (from the repository root, with a clean conformance/ and ref/):
    python conformance/tools/t0_receipt.py [--machine NAME] [--os TEXT]

Python standard library only; integers only.
"""
import argparse
import datetime
import glob
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
PY = sys.executable
ENV = dict(os.environ, PYTHONPATH=os.path.join(ROOT, "ref"))
CATEGORIES = ("arith", "control", "switch", "struct", "diag", "determinism")

TREE_METHOD = ("sha256 over the lines '<sha256>  <path>\\n' (the sha256sum list format), one line per "
               "tracked file under %s, path relative to %s, the lines ordered by byte order of path")


def run(cmd, cwd=ROOT):
    t0 = time.monotonic_ns()
    r = subprocess.run(cmd, cwd=cwd, env=ENV, capture_output=True)
    ms = (time.monotonic_ns() - t0) // 1_000_000
    return r.returncode, r.stdout.decode("utf-8", "replace") + r.stderr.decode("utf-8", "replace"), ms


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, check=True).stdout.decode("ascii").strip()


def sha(path):
    with open(path, "rb") as fh:
        return hashlib.sha256(fh.read()).hexdigest()


def tree(paths, base):
    """SHA-256 of the list `<sha256>  <path>\\n`, paths relative to `base`, ordered by the bytes of the path."""
    rel = sorted((os.path.relpath(p, base).replace(os.sep, "/") for p in paths), key=lambda s: s.encode("utf-8"))
    lines = "".join("%s  %s\n" % (sha(os.path.join(base, r)), r) for r in rel)
    return hashlib.sha256(lines.encode("utf-8")).hexdigest(), len(rel)


def cif1(path):
    code, out, ms = run([PY, "-m", "cint_ref", "cif1", os.path.join(ROOT, path)], cwd=os.path.join(ROOT, "ref"))
    m = re.search(r"cif1: (\d+) records, (\d+) agree, (\d+) disagree, (\d+) unsupported, (\d+) invalid", out)
    assert m, out[-2000:]
    return [int(x) for x in m.groups()], ms


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--machine", default=platform.node())
    ap.add_argument("--os", default="%s %s" % (platform.system(), platform.release()))
    ns = ap.parse_args(argv)
    os.chdir(ROOT)
    assert git("status", "--porcelain", "--", "conformance", "ref") == "", "conformance/ or ref/ has uncommitted changes"
    head = git("rev-parse", "HEAD")
    wall = {}
    commands = []

    # 1. Unit tests.
    code, out, wall["ref_unittest"] = run([PY, "-m", "unittest", "discover", "-s", "tests", "-t", "."],
                                          cwd=os.path.join(ROOT, "ref"))
    ref_tests = int(re.search(r"Ran (\d+) tests", out).group(1))
    assert code == 0 and out.rstrip().endswith("OK"), out[-2000:]
    commands.append("cd ref && python -m unittest discover -s tests -t .")
    code, out, wall["generator_unittest"] = run([PY, "-m", "unittest", "discover", "-s", "conformance/tools",
                                                 "-p", "test_*.py"])
    gen_tests = int(re.search(r"Ran (\d+) tests", out).group(1))
    assert code == 0 and out.rstrip().endswith("OK"), out[-2000:]
    commands.append('python -m unittest discover -s conformance/tools -p "test_*.py"')

    # 2. CIF-1: anchors and generated tables.
    keys = ("records", "agree", "disagree", "unsupported", "invalid")
    vals, wall["anchors_cif1"] = cif1("conformance/integer-machine/anchors.cif1.jsonl")
    anchors = dict(zip(keys, vals))
    commands.append("cd ref && python -m cint_ref cif1 ../conformance/integer-machine/anchors.cif1.jsonl")
    with open("conformance/integer-machine/anchors.cif1.jsonl", "rb") as fh:
        recs = [json.loads(l) for l in fh.read().decode("utf-8").split("\n") if l]
    red_ops = ("sum.", "fold_checked.", "sum_sat.", "sum_wrap.", "reduce_min.", "reduce_max.")
    reductions = sum(1 for r in recs if r["op"].startswith(red_ops))
    groups = {}
    for key, sub in (("exhaustive_8", "exh8"), ("boundary_64", "bnd64")):
        tot = dict.fromkeys(("files",) + keys, 0)
        ms_total = 0
        for p in sorted(glob.glob("conformance/integer-machine/%s/*.cif1.jsonl" % sub)):
            vals, ms = cif1(p.replace(os.sep, "/"))
            ms_total += ms
            tot["files"] += 1
            for k, v in zip(keys, vals):
                tot[k] += v
        groups[key] = tot
        wall[key + "_cif1"] = ms_total
        commands.append("cd ref && for f in ../conformance/integer-machine/%s/*.cif1.jsonl; "
                        "do python -m cint_ref cif1 $f; done" % sub)

    # 3. Third derivation of the generated tables.
    code, out, wall["oracle3"] = run([PY, "conformance/tools/oracle3.py"])
    m = re.search(r"oracle3: (\d+) records, (\d+) mismatches", out)
    assert m, out[-2000:]
    oracle3 = {"records": int(m.group(1)), "mismatches": int(m.group(2))}
    commands.append("python conformance/tools/oracle3.py")

    # 4. Table programs through the cint_ref program layer.
    code, out, wall["table_programs"] = run([PY, "conformance/tools/ref_tables.py"])
    m = re.search(r"tables: (\d+) programs, (\d+) cases, (\d+) agree, (\d+) disagree", out)
    assert m, out[-2000:]
    tables = dict(zip(("programs", "cases", "agree", "disagree"), (int(x) for x in m.groups())))
    commands.append("python conformance/tools/ref_tables.py")

    # 5. Program cases.
    code, out, wall["program_cases"] = run([PY, "conformance/tools/gen_expect.py", "--check"])
    m = re.search(r"check: (\d+) differ, (\d+) held, (\d+) unlisted", out)
    assert m, out[-2000:]
    with open("conformance/held.txt", encoding="ascii") as fh:
        held_lines = [l for l in fh.read().split("\n") if l and not l.startswith("#")]
    sources = sorted(p for c in CATEGORIES for p in glob.glob("conformance/%s/*.ci" % c))
    expects = sorted(p for c in CATEGORIES for p in glob.glob("conformance/%s/*.expect" % c))
    texts = []
    for p in expects:
        with open(p, encoding="ascii") as fh:
            texts.append(fh.read())
    names = set()
    for t in texts:
        names.update(re.findall(r"^case (\S+)$", t, re.M))
    program = {"source_files": len(sources), "held": len(held_lines),
               "frozen_source_files": len(sources) - len(held_lines),
               "frozen_cases": len(names), "expect_files": len(expects),
               "differ": int(m.group(1)), "unlisted": int(m.group(3)),
               "source_reference": sum(1 for t in texts if "\nsource reference\n" in t),
               "source_compile_error": sum(1 for t in texts if "\nsource compile-error\n" in t),
               "outcome_fault": sum(1 for t in texts if "\noutcome fault\n" in t)}
    assert program["held"] == int(m.group(2))
    commands.append("python conformance/tools/gen_expect.py --check")

    # 6. MANIFEST digests and a full regeneration.
    t0 = time.monotonic_ns()
    with open("conformance/integer-machine/MANIFEST.txt", encoding="ascii") as fh:
        man = [l.split() for l in fh.read().split("\n") if l and not l.startswith("#")]
    mismatch = sum(1 for h, size, p in man if sha(os.path.join("conformance", p)) != h
                   or os.path.getsize(os.path.join("conformance", p)) != int(size))
    wall["manifest_verify"] = (time.monotonic_ns() - t0) // 1_000_000
    regen = tempfile.mkdtemp(prefix="t0-regen-")
    try:
        for d in ("exh8", "bnd64", "tables"):
            os.makedirs(os.path.join(regen, d))
        t0 = time.monotonic_ns()
        for cmd in ([PY, "conformance/tools/gen_exhaustive8.py", "--batch", "all", "--out", os.path.join(regen, "exh8")],
                    [PY, "conformance/tools/gen_boundary64.py", "--batch", "all", "--out", os.path.join(regen, "bnd64")],
                    [PY, "conformance/tools/gen_tables.py"]
                    + sorted(glob.glob("conformance/integer-machine/exh8/*.cif1.jsonl"))
                    + sorted(glob.glob("conformance/integer-machine/bnd64/*.cif1.jsonl"))
                    + ["--out", os.path.join(regen, "tables")]):
            code, out, _ = run(cmd)
            assert code == 0, out[-2000:]
        wall["regenerate"] = (time.monotonic_ns() - t0) // 1_000_000
        same = 0
        for h, size, p in man:
            q = os.path.join(regen, p.split("/", 1)[1] if p.startswith("integer-machine/") else p)
            same += sha(q) == h
    finally:
        shutil.rmtree(regen, ignore_errors=True)
    manifest = {"files": len(man), "mismatch": mismatch, "regenerated_identical": same}
    commands += ["python conformance/tools/gen_exhaustive8.py --batch all --out <scratch>/exh8",
                 "python conformance/tools/gen_boundary64.py --batch all --out <scratch>/bnd64",
                 "python conformance/tools/gen_tables.py conformance/integer-machine/exh8/*.cif1.jsonl "
                 "conformance/integer-machine/bnd64/*.cif1.jsonl --out <scratch>/tables",
                 "sha256 of each regenerated file compared with conformance/integer-machine/MANIFEST.txt"]

    # 7. Suite digest and cint_ref source identity.
    suite_sha, suite_n = tree([os.path.join(ROOT, p) for p in git("ls-files", "conformance").split("\n")],
                              os.path.join(ROOT, "conformance"))
    ref_sha, ref_n = tree([os.path.join(ROOT, p) for p in git("ls-files", "ref/cint_ref").split("\n")],
                          os.path.join(ROOT, "ref"))

    today = datetime.datetime.now(datetime.timezone.utc).date().isoformat()
    cif1_groups = (anchors, groups["exhaustive_8"], groups["boundary_64"])
    failed = (sum(g["disagree"] + g["invalid"] for g in cif1_groups) + anchors["unsupported"]
              + tables["disagree"] + oracle3["mismatches"] + program["differ"] + program["unlisted"]
              + manifest["mismatch"] + (manifest["files"] - manifest["regenerated_identical"]))
    machine = platform.machine().lower()
    receipt = {
        "identity": {
            "cint_ref_head": head,
            "cint_ref_tree_files": ref_n,
            "cint_ref_tree_method": TREE_METHOD % ("ref/cint_ref/", "ref/"),
            "cint_ref_tree_sha256": ref_sha,
            "profile": "cint-core-1",
            "schema": "cint-conformance-receipt-1",
            "stage": "T0",
            "suite_digest_method": TREE_METHOD % ("conformance/", "conformance/"),
            "suite_files": suite_n,
            "suite_sha256": suite_sha,
        },
        "outcome": {
            "anchors": anchors,
            "anchors_reductions": reductions,
            "boundary_64": groups["boundary_64"],
            "conformance": {"cases": anchors["records"] + groups["exhaustive_8"]["records"]
                            + groups["boundary_64"]["records"] + program["frozen_cases"],
                            "cases_method": "CIF-1 records of the anchors and generated tables, plus the distinct "
                                            "`case` names of the program .expect files (ref/OPEN.md I-3)",
                            "failed": failed, "suite_sha256": suite_sha},
            "exhaustive_8": groups["exhaustive_8"],
            "generator_tests": {"failed": 0, "run": gen_tests},
            "manifest": manifest,
            "oracle3": oracle3,
            "program_cases": program,
            "reference_tests": {"failed": 0, "run": ref_tests},
            "table_programs": tables,
        },
        "observations": {
            "commands": commands,
            "date_utc": today,
            "host": {"arch": "x86-64" if machine in ("amd64", "x86_64") else machine,
                     "machine": ns.machine, "os": ns.os},
            "python": "%d.%d.%d" % sys.version_info[:3],
            "python_implementation": platform.python_implementation(),
            "wall_ms": wall,
            "wall_ms_total": sum(wall.values()),
        },
    }
    text = json.dumps(receipt, sort_keys=True, separators=(",", ":"), ensure_ascii=True) + "\n"
    assert ROOT not in text and ROOT.replace("\\", "/") not in text and regen not in text
    os.makedirs("results/cint/t0", exist_ok=True)
    with open("results/cint/t0/receipt.json", "wb") as fh:
        fh.write(text.encode("ascii"))
    ident = json.dumps({"identity": receipt["identity"], "outcome": receipt["outcome"]},
                       sort_keys=True, separators=(",", ":"), ensure_ascii=True)
    print(json.dumps(receipt, indent=1, sort_keys=True))
    print("identity digest", hashlib.sha256(ident.encode("ascii")).hexdigest())
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
