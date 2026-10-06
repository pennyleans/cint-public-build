"""Runs zero-extent fields through a retained compiler and the reference."""
import argparse
import hashlib
import json
import pathlib
import subprocess
import sys

import aggregate_values as av

ROOT = pathlib.Path(__file__).resolve().parents[2]
TESTS = ROOT / "compiler/tests"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=pathlib.Path)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    parser.add_argument("--leg", default="msvc", choices=("msvc", "gcc", "clang"))
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    commands, rows = [], []

    def call(argv, cwd, env=None):
        result = subprocess.run(list(map(str, argv)), cwd=cwd, env=env, capture_output=True, text=True)
        commands.append(dict(command=list(map(str, argv)), cwd=str(cwd), exit=result.returncode,
                             stdout=result.stdout, stderr=result.stderr))
        (args.out / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        return result

    def save(name, passed, **detail):
        rows.append(dict(case=name, passed=passed, **detail))
        (args.out / "results.json").write_text(json.dumps(rows, indent=2) + "\n")
        print(name + ": " + ("pass" if passed else "FAIL"), flush=True)

    hashes = {name: hashlib.sha256((TESTS / name).read_bytes()).hexdigest()
              for name in ("zero_fields.ci", "zero_field_lib.ci", "zero_fields_host.c")}
    (args.out / "sources.json").write_text(json.dumps(hashes, indent=2) + "\n")
    emitted = call([args.exe, "--json", "emit-c", "--root", TESTS, "--out", args.out / "out",
                    "zero_field_lib.ci", "zero_fields.ci"], ROOT)
    if emitted.returncode != 0:
        save("emit", False, command=commands[-1])
        return 1
    sources = []
    for rel, data in av.golden_suite.output_set(args.out / "out").items():
        if rel.endswith(".c"):
            path = args.out / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            sources.append(path)
    tc = av.RecordedToolchain(args.leg)
    try:
        rt = tc.compile([ROOT / "rt/cint_rt.c"], args.out, "zero-field-runtime")
        harness_objects = tc.compile([ROOT / "harness/cint_harness.c"], args.out, "zero-field-harness")
        harness = tc.link_exe(harness_objects + rt, args.out / "harness", "zero-field-harness")
        objects = tc.compile(sources, args.out, "zero-fields")
        library = tc.link_shared(objects + rt, args.out / "arrays", "zero-fields")
        for _, fn, values in (case for case in av.golden_suite.CALLS if case[0] == "zero_fields"):
            refargs = [sys.executable, "-B", "-m", "cint_ref", "run", TESTS / "zero_fields.ci",
                       "--entry", fn, "--path", "zero_fields.ci", "--format", "2"]
            for value in values:
                refargs += ["--arg", value]
            reference = call(refargs, ROOT / "ref")
            refrow = commands[-1]
            cases = args.out / "cases.txt"
            cases.write_bytes((" ".join(["zero_fields", fn, *values]) + "\n").encode("ascii"))
            actual = call([harness, library, "--cases", cases, "--format", "2"], args.out, tc.env)
            save(" ".join([fn, *values]), reference.returncode == 0 and actual.returncode == 0 and
                 not actual.stderr and av.observed(reference.stdout) == av.observed(actual.stdout),
                 reference=refrow, native=commands[-1])
        host = tc.compile([TESTS / "zero_fields_host.c"], args.out, "zero-field-public-host")
        executable = tc.link_exe(objects + host + rt, args.out / "host", "zero-field-public-host")
        actual = call([executable], args.out, tc.env)
        save("public", actual.returncode == 0 and not actual.stderr, native=commands[-1])
        symbols = [tc.symbols(obj) for obj in objects]
        errors = av.cc.audit_program(symbols, tc.exports(library), tc.symbols(rt[0])[0], tc.leg)
        save("symbol audit", not errors, errors=errors)
    except av.cc.BuildError as error:
        save("native build", False, error=str(error))
    finally:
        (args.out / "native-commands.json").write_text(json.dumps(tc.commands, indent=2) + "\n")
    passed = sum(row["passed"] for row in rows)
    print("zero fields: %d of %d pass" % (passed, len(rows)))
    return 0 if passed == len(rows) else 1


if __name__ == "__main__":
    raise SystemExit(main())
