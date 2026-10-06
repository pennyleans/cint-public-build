"""Compares nested record values, faults, fuel, and state through the native harness."""
import argparse
import hashlib
import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / "tools"), str(ROOT / "compiler/tests")]
import cint_check as cc
import golden_suite

CASES = {
    'array_field_view': 'struct O { I item; }\nstruct I { I64[2] bits; }\nvoid set(inout O[1] a) { a[0].item.bits[1] = 9; a[0].item.bits[0]++; }\nexport I64 run() { O[1] a; set(a); return a[0].item.bits[0] + a[0].item.bits[1]; }\n',
    'array_field_order': 'struct O { I64[1] bits; }\nI64 seen = 0;\nI64 tick(I64 v) { seen = seen * 10 + v; return v; }\nexport I64 run() { O[1] a; a[tick(0)].bits[tick(0)] = tick(7); return seen * 10 + a[0].bits[0]; }\n',
    'array_field_outer_fault': 'struct O { I64[1] bits; }\nI64 seen = 0;\nI64 tick(I64 v) { seen = seen * 10 + v; return v; }\nexport I64 run() { O[1] a; a[tick(1)].bits[tick(0)] = tick(7); return 0; }\n',
    'array_field_inner_fault': 'struct O { I64[1] bits; }\nI64 seen = 0;\nI64 tick(I64 v) { seen = seen * 10 + v; return v; }\nexport I64 run() { O[1] a; a[tick(0)].bits[tick(1)] = tick(7); return 0; }\n',
    'disjoint_fields': 'struct O { I a; I b; }\nstruct I { I64 v; }\nvoid set(inout I a, inout I b) { a.v = 2; b.v = 3; }\nexport I64 run() { O o; set(o.a, o.b); return o.a.v * 10 + o.b.v; }\n',
    'field_argument_alias': 'struct O { I item; }\nstruct I { I64 v; }\nI64 take(I a, I64 later) { return a.v + later; }\nI64 mutate(inout O[1] a) { a[0].item.v = 9; return 0; }\nexport I64 run() { O[1] a; a[0].item.v = 1; return take(a[0].item, mutate(a)); }\n',
    'constructor_argument': 'struct O { I item; }\nstruct I { I64 v; }\nI64 take(O a) { return a.item.v; }\nexport I64 run() { return take(O(I(7))); }\n',
    "constructor_field_alias": 'struct O { I item; }\nstruct I { I64 v; }\nstruct Pair { I first; I64 second; }\nI64 mutate(inout O[1] a) { a[0].item.v = 9; return 0; }\nexport I64 run() { O[1] a; a[0].item.v = 1; Pair p = Pair(a[0].item, mutate(a)); return p.first.v; }\n',
    "record_parameter_index": 'struct Row { I64 code; I64[2] detail; }\nvoid clear(inout Row[2] rows, I64 i) { rows[i].code = 3; rows[i].code++; rows[i].detail[1] = 4; }\nexport I64 run() { Row[2] rows; clear(rows, 1); return rows[1].code + rows[1].detail[1]; }\n',
    "forward": "struct Outer { Inner item; }\nstruct Inner { I64 v; }\nexport I64 run() { Outer b; b.item.v = 7; return b.item.v; }\n",
    "backward": "struct Inner { I64 v; }\nstruct Outer { Inner item; }\nexport I64 run() { Outer b; b.item.v = 7; return b.item.v; }\n",
    "constructor": "struct Outer { Middle item; }\nstruct Middle { Inner item; }\nstruct Inner { I64 v; Bool b; }\nexport I64 run() { Outer a = Outer(Middle(Inner(b = true, v = 8))); return a.item.item.v; }\n",
    "copy": "struct A { B b; }\nstruct B { C c; }\nstruct C { I64 v; }\nexport I64 run() { A a = A(B(C(7))); A b = a; b.b.c.v = 9; I64 old = a.b.c.v; a.b = b.b; return old * 100 + a.b.c.v; }\n",
    "view": "struct Outer { Inner item; }\nstruct Inner { I64 v; }\nI64 touch(inout Outer[1] a) { a[0].item.v += 4; return a[0].item.v; }\nexport I64 run() { Outer[1] a; a[0].item.v = 7; return touch(a); }\n",
    "diamond": "struct Root { Left l; Right r; }\nstruct Left { Leaf item; }\nstruct Right { Leaf item; }\nstruct Leaf { Bool b; I64 v; }\nexport I64 run() { Root a = Root(Left(Leaf(true, 3)), Right(Leaf(false, 5))); return a.l.item.v + a.r.item.v; }\n",
    "argument_order": "struct Pair { Leaf a; Leaf b; }\nstruct Leaf { I64 v; }\nI64 seen = 0;\nI64 tick(I64 v) { seen = seen * 10 + v; return v; }\nexport I64 run() { Pair a = Pair(b = Leaf(tick(2)), a = Leaf(tick(1))); return seen * 100 + a.a.v * 10 + a.b.v; }\n",
    "argument_fault": "struct Pair { Leaf a; Leaf b; }\nstruct Leaf { I64 v; }\nI64 seen = 0;\nI64 tick(I64 v) { seen = seen * 10 + v; return v; }\nI64 divide(I64 v) { return 10 / v; }\nexport I64 run() { Pair a = Pair(Leaf(tick(1)), Leaf(divide(0))); return a.a.v; }\n",
    "inout_field": "struct Outer { Inner item; }\nstruct Inner { I64 v; }\nvoid set(inout Inner a) { a.v = 12; }\nexport I64 run() { Outer a; set(a.item); return a.item.v; }\n",
    "imported_nested": "import records;\nexport I64 run() { records.Outer a; a.item.v = 13; return a.item.v; }\n",
}
LIB = "export struct Outer { Inner item; }\nstruct Inner { I64 v; }\n"
PREFIXES = ("outcome ", "return ", "fuel-consumed ", "fault.", "state.global ")


def observed(text):
    return [line for line in text.splitlines() if line.startswith(PREFIXES) and not line.startswith("fault.revision ")]


class RecordedToolchain(cc.Toolchain):
    def __init__(self, leg):
        self.commands = []
        super().__init__(leg, "0", "portable", False)

    def run(self, command, cwd, what):
        self.commands.append({"command": list(map(str, command)), "cwd": str(cwd), "what": what})
        result = super().run(command, cwd, what)
        self.commands[-1].update(exit=result.returncode, stdout=result.stdout, stderr=result.stderr)
        return result


def public_host(exe, out, tc, rt):
    directory = out / "nested_public"
    directory.mkdir()
    command = [str(exe), "--json", "emit-c", "--root", str(ROOT / "compiler/tests"),
               "--out", str(directory / "out"), "nested_public.ci"]
    emitted = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    row = {"case": "nested_public", "emit": {"command": command, "exit": emitted.returncode,
           "stdout": emitted.stdout, "stderr": emitted.stderr}, "passed": False}
    if emitted.returncode == 0:
        sources = []
        for rel, data in golden_suite.output_set(directory / "out").items():
            if rel.endswith(".c"):
                target = directory / rel
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
                sources.append(target)
        try:
            objects = tc.compile(sources, directory, "nested-public")
            host = tc.compile([ROOT / "compiler/tests/nested_public_host.c"], directory, "nested-public-host")
            executable = tc.link_exe(objects + host + rt, directory / "nested-public-host", "nested-public-host")
            run = subprocess.run([str(executable)], env=tc.env, capture_output=True, text=True)
            row["run"] = {"command": [str(executable)], "exit": run.returncode,
                          "stdout": run.stdout, "stderr": run.stderr}
            row["passed"] = run.returncode == 0 and not run.stderr
        except cc.BuildError as error:
            row["build_error"] = str(error)
    print("nested_public: %s" % ("pass" if row["passed"] else "FAIL"), flush=True)
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=pathlib.Path)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    parser.add_argument("--leg", default="msvc", choices=("msvc", "gcc", "clang"))
    parser.add_argument("--only", action="append", choices=(*CASES, "nested_public"))
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    source_root = args.out / "sources"
    source_root.mkdir()
    (source_root / "records.ci").write_bytes(LIB.encode())
    tc = RecordedToolchain(args.leg)
    native = args.out / "native"
    native.mkdir()
    rt = tc.compile([ROOT / "rt/cint_rt.c"], native, "aggregate-runtime")
    harness_objs = tc.compile([ROOT / "harness/cint_harness.c"], native, "aggregate-harness")
    harness = tc.link_exe(harness_objs + rt, native / "harness", "aggregate-harness")
    results = []
    for name, source in CASES.items():
        if args.only and name not in args.only:
            continue
        path = source_root / (name + ".ci")
        path.write_bytes(source.encode())
        directory = args.out / name
        directory.mkdir()
        ref_command = [sys.executable, "-B", "-m", "cint_ref", "run", str(path), "--entry", "run",
                       "--path", path.name, "--format", "2"]
        ref = subprocess.run(ref_command, cwd=ROOT / "ref", capture_output=True, text=True)
        command = [str(args.exe), "--json", "emit-c", "--root", str(source_root), "--out",
                   str(directory / "out"), *cc.module_order(source_root, path.name)]
        emitted = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        row = {"case": name, "source_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
               "reference": {"command": ref_command, "exit": ref.returncode, "stdout": ref.stdout, "stderr": ref.stderr},
               "emit": {"command": command, "exit": emitted.returncode, "stdout": emitted.stdout, "stderr": emitted.stderr},
               "passed": False}
        if emitted.returncode == 0:
            sources = []
            for rel, data in golden_suite.output_set(directory / "out").items():
                if rel.endswith(".c"):
                    target = directory / rel
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(data)
                    sources.append(target)
            try:
                objects = tc.compile(sources, directory, "aggregate-case")
                library = tc.link_shared(objects + rt, directory / name, "aggregate-case")
                cases = directory / "cases.txt"
                cases.write_bytes((name + " run\n").encode())
                run_command = [str(harness), str(library), "--cases", str(cases), "--format", "2"]
                run = subprocess.run(run_command, env=tc.env, capture_output=True, text=True)
                row["run"] = {"command": run_command, "exit": run.returncode, "stdout": run.stdout, "stderr": run.stderr}
                row["passed"] = (ref.returncode == 0 and run.returncode == 0 and not run.stderr
                                 and observed(ref.stdout) == observed(run.stdout))
            except cc.BuildError as error:
                row["build_error"] = str(error)
        results.append(row)
        (args.out / "results.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
        print("%s: %s" % (name, "pass" if row["passed"] else "FAIL"), flush=True)
    if not args.only or "nested_public" in args.only:
        results.append(public_host(args.exe, args.out, tc, rt))
    (args.out / "results.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    (args.out / "native-commands.json").write_text(json.dumps(tc.commands, indent=2) + "\n", encoding="utf-8")
    passed = sum(row["passed"] for row in results)
    print("aggregate values: %d of %d pass" % (passed, len(results)))
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
