"""Compares containment diagnostics with the reference through the B1 CLI."""
import argparse
import hashlib
import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
CASES = {
    'same_field': 'struct O { I item; }\nstruct I { I64 v; }\nvoid set(inout I a, I b) { a.v = b.v; }\nexport I64 run() { O o; set(o.item, o.item); return 0; }\n',
    'whole_field': 'struct O { I item; }\nstruct I { I64 v; }\nvoid set(inout O a, I b) { a.item.v = b.v; }\nexport I64 run() { O o; set(o, o.item); return 0; }\n',
    'readonly_field': 'struct O { I item; }\nstruct I { I64 v; }\nvoid set(inout I a) { a.v = 1; }\nvoid read(O o) { set(o.item); }\nexport I64 run() { return 0; }\n',
    "direct": "struct A { A item; }\nexport I64 run() { return 0; }\n",
    "indirect": "struct A { B item; }\nstruct B { A back; }\nexport I64 run() { return 0; }\n",
    "cycle_before_signature": "struct A { B item; }\nstruct B { A back; }\nMissing f() { return 0; }\n",
    "later_duplicate_before_cycle": "struct A { A item; }\nstruct B { I64 x; I64 x; }\n",
    "later_unknown_before_cycle": "struct A { A item; }\nstruct B { Missing x; }\n",
    "field_order": "struct A { B first; C second; }\nstruct B { D inner; }\nstruct C { A back; }\nstruct D { B back; }\n",
    "declaration_order": "struct B { A back; }\nstruct A { B item; }\n",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=pathlib.Path)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    results = []
    for name, source in CASES.items():
        path = args.out / (name + ".ci")
        path.write_bytes(source.encode())
        ref_cmd = [sys.executable, "-B", "-m", "cint_ref", "run", str(path), "--entry", "run",
                   "--path", path.name]
        ref = subprocess.run(ref_cmd, cwd=ROOT / "ref", capture_output=True, text=True)
        cli_cmd = [str(args.exe), "--json", "emit-c", "--root", str(args.out), "--out",
                   str(args.out / (name + "-out")), path.name]
        cli = subprocess.run(cli_cmd, cwd=ROOT, capture_output=True, text=True)
        lines = dict(line.split(" ", 1) for line in ref.stdout.splitlines() if " " in line)
        try:
            diag = json.loads(cli.stderr)
        except json.JSONDecodeError:
            diag = {}
        pos = "%s:%s:%s" % (diag.get("file"), diag.get("line"), diag.get("column"))
        passed = (ref.returncode == 0 and lines.get("outcome") == "compile-error" and cli.returncode == 2
                  and diag.get("code") == lines.get("diagnostic.code") and pos == lines.get("diagnostic.position"))
        results.append({"case": name, "source_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                        "reference": {"command": ref_cmd, "exit": ref.returncode, "stdout": ref.stdout,
                                      "stderr": ref.stderr},
                        "b1": {"command": cli_cmd, "exit": cli.returncode, "stdout": cli.stdout,
                               "stderr": cli.stderr}, "passed": passed})
        print("%s: %s" % (name, "pass" if passed else "FAIL"), flush=True)
    (args.out / "results.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    passed = sum(row["passed"] for row in results)
    print("aggregate diagnostics: %d of %d pass" % (passed, len(results)))
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
