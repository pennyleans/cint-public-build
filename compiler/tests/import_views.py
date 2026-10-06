"""Compares imported record and literal view arguments with the exact reference."""
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

LIBRARY = """export struct R { Bool active; I64 value; }
struct Hidden { I64 value; }
export const I64 TOKEN = 7;
const I64 SECRET = 9;
export I64 read(in R[_] rows) { return rows[0].value; }
export void bump(inout R[_] rows) { rows[0].value += 5; }
export Hidden produce() { return Hidden(19); }
export I64 consume(Hidden value) { return value.value; }
export I64 first(in U8[_] text) { return text[0] as I64; }
export I64 weigh[n](in U8[n] text) {
 I64 acc = 0;
 for i in 0..n { acc += (i + 1) * (text[i] as I64); }
 return acc;
}
export I64 extent_of[n](in U8[n] text) { return n; }
export I64 three(in U8[3] text) { return text[2] as I64; }
export I64 paired[n](in U8[n] first_text, in U8[n] second_text) { return n; }
export void copied[n, m](inout U8[n] dst, in U8[m] text) { dst[0] = text[0]; dst[1] = text[m - 1]; }
export I64 mixed(I64 value, in U8[_] text) { return value + (text[0] as I64); }
I64 private_text(in U8[_] text) { return 0; }
export void writable(inout U8[_] text) { text[0] = 1; }
export I64 wide(in U16[_] text) { return 0; }
"""
HELPERS = """import records;
export I64 total[n](in records.R[n] rows) {
 I64 acc = 0;
 for i in 0..n { acc += rows[i].value; }
 return acc;
}
"""
VALUES = {
    "parameter": "I64 take(records.R value) { return value.value; }\nexport I64 run() { records.R value = records.R(true, 7); return take(value); }\n",
    "result": "records.R make() { records.R value = records.R(true, 7); return value; }\nexport I64 run() { records.R value = make(); return value.value; }\n",
    "wildcard": "I64 take(in records.R[_] rows) { return rows[0].value; }\nexport I64 run() { records.R[2] rows; rows[0].value = 7; return take(rows); }\n",
    "fixed": "void change(inout records.R[2] rows) { rows[1].value = 8; }\nexport I64 run() { records.R[2] rows; change(rows); return rows[1].value; }\n",
    "symbolic": "I64 take[n](in records.R[n] rows) { I64 acc = 0; for i in 0..n { acc += rows[i].value; } return acc; }\nexport I64 run() { records.R[2] rows; rows[0].value = 7; rows[1].value = 8; return take(rows); }\n",
    "aliases": "import records as other;\nI64 take(in other.R[_] rows) { return rows[0].value; }\nexport I64 run() { records.R[1] rows; rows[0].value = 7; return take(rows); }\n",
    "imported_callee": "export I64 run() { records.R[1] rows; rows[0].value = 7; records.bump(rows); return records.read(rows); }\n",
    "qualified_callee": "import helpers;\nexport I64 run() { records.R[2] rows; rows[0].value = 7; rows[1].value = 8; return helpers.total(rows); }\n",
    "private_signature_closure": "export I64 run() { return records.consume(records.produce()); }\n",
    "empty_view": "I64 take[n](in records.R[n] rows) { return n; }\nexport I64 run() { records.R[0] rows; return take(rows); }\n",
    "explicit_in_record": "I64 take(in records.R value) { return value.value; }\nexport I64 run() { return take(records.R(true, 7)); }\n",
    "string_wildcard": 'export I64 run() { return records.first("Z"); }\n',
    "string_symbolic": 'export I64 run() { return records.weigh("a\\\\\\\"\\\'\\n\\r\\t\\0\\x41\\x7F{{}}" "bc"); }\n',
    "string_empty": 'export I64 run() { return records.extent_of(""); }\n',
    "string_empty_fault": 'export I64 run() { return records.first(""); }\n',
    "string_fixed": 'export I64 run() { return records.three("A\\0Z"); }\n',
    "string_pair": 'export I64 run() { return records.paired("ab", "cd"); }\n',
    "string_copy": 'export I64 run() { U8[2] dst; records.copied(dst, "A\\0Z"); return (dst[0] as I64) + (dst[1] as I64); }\n',
    "string_alias": 'import records as other;\nexport I64 run() { return other.first("Q"); }\n',
}
DIAGNOSTICS = {
    "unknown_alias": "I64 take(in missing.R[_] rows) { return 0; }\n",
    "missing_type": "I64 take(in records.Missing[_] rows) { return 0; }\n",
    "private_type": "I64 take(in records.Hidden[_] rows) { return 0; }\n",
    "wrong_kind": "I64 take(in records.TOKEN[_] rows) { return 0; }\n",
    "private_before_kind": "I64 take(in records.SECRET[_] rows) { return 0; }\n",
    "qualified_builtin": "I64 take(in records.I64[_] rows) { return 0; }\n",
    "unknown_builtin_alias": "I64 take(in missing.I64[_] rows) { return 0; }\n",
    "field_before_signature": "struct Bad { records.Hidden value; }\nI64 take(in records.Missing[_] rows) { return 0; }\n",
    "signature_before_body": "I64 early() { return absent; }\nI64 take(in records.Missing[_] rows) { return 0; }\n",
    "readonly": "void change(inout records.R[_] rows) { rows[0].value = 8; }\nI64 take(in records.R[_] rows) { change(rows); return 0; }\n",
    "same_size": "I64 take[n](in records.R[n] first, in records.R[n] second) { return 0; }\nexport I64 run() { records.R[1] first; records.R[2] second; return take(first, second); }\n",
    "overlap": "I64 take(inout records.R[_] first, in records.R[_] second) { return 0; }\nexport I64 run() { records.R[1] rows; return take(rows, rows); }\n",
    "size_name_before_type": "I64 take[records](in records.Missing[_] rows) { return 0; }\n",
    "size_duplicate_before_type": "I64 take[n, n](in records.Missing[n] rows) { return 0; }\n",
    "unbound_size_before_result": "records.Missing take[n](in records.R[_] rows) { return records.Missing(); }\n",
    "string_fixed_shape": 'export I64 run() { return records.three("ab"); }\n',
    "string_same_size": 'export I64 run() { return records.paired("a", "bc"); }\n',
    "string_private": 'export I64 run() { return records.private_text("x"); }\n',
    "string_missing": 'export I64 run() { return records.absent("x"); }\n',
    "string_wrong_kind": 'export I64 run() { return records.TOKEN("x"); }\n',
    "string_argument_count": 'export I64 run() { return records.first("x", "y"); }\n',
    "string_prior_type": 'export I64 run() { return records.mixed(true, "x"); }\n',
    "explicit_in_readonly": "I64 take(in records.R value) { value.value = 8; return 0; }\n",
}
REFUSALS = {
    "string_writable": 'export I64 run() { records.writable("x"); return 0; }\n',
    "string_wrong_element": 'export I64 run() { return records.wide("x"); }\n',
    "string_scalar": 'export I64 run() { return records.mixed("x", "y"); }\n',
}


def observed(text):
    prefixes = ("outcome ", "return ", "fuel-consumed ", "fault.", "state.global ")
    return [line for line in text.splitlines() if line.startswith(prefixes) and not line.startswith("fault.revision ")]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=pathlib.Path)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    parser.add_argument("--leg", default="msvc", choices=("msvc", "gcc", "clang"))
    parser.add_argument("--emit-only", action="store_true")
    parser.add_argument("--only", action="append", choices=(*VALUES, *DIAGNOSTICS, *REFUSALS, "public"))
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    rows = []
    commands = []

    def call(argv, cwd, env=None):
        result = subprocess.run(list(map(str, argv)), cwd=cwd, env=env, capture_output=True, text=True, timeout=120)
        record = {"command": list(map(str, argv)), "cwd": str(cwd), "exit": result.returncode,
                  "stdout": result.stdout, "stderr": result.stderr}
        commands.append(record)
        (args.out / "commands.json").write_text(json.dumps(commands, indent=2) + "\n", encoding="utf-8")
        return result

    class RecordedToolchain(cc.Toolchain):
        def run(self, command, cwd, what):
            result = call(command, cwd, self.env)
            if result.returncode != 0:
                raise cc.BuildError("%s: %s\n%s" % (what, result.stdout, result.stderr))
            return result

    tc = None
    runtime = None
    harness = None

    def native_tools():
        nonlocal tc, runtime, harness
        if tc is None:
            tc = RecordedToolchain(args.leg, "0", "portable", False)
            native = args.out / "native"; native.mkdir()
            runtime = tc.compile([ROOT / "rt/cint_rt.c"], native, "import-view runtime")
            objects = tc.compile([ROOT / "harness/cint_harness.c"], native, "import-view harness")
            harness = tc.link_exe(objects + runtime, native / "harness", "import-view harness")

    def emitted_sources(directory):
        sources = []
        for relative, data in golden_suite.output_set(directory / "out").items():
            if relative.endswith(".c"):
                path = directory / relative; path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data); sources.append(path)
        return sources

    def save(row):
        rows.append(row)
        (args.out / "results.json").write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
        print(row["case"] + ": " + ("pass" if row["passed"] else "FAIL"), flush=True)

    for name, body in {**VALUES, **DIAGNOSTICS, **REFUSALS}.items():
        if args.only and name not in args.only:
            continue
        directory = args.out / name
        directory.mkdir()
        source = directory / "main.ci"
        source.write_bytes(("import records;\n" + body).encode("ascii"))
        (directory / "records.ci").write_bytes(LIBRARY.encode("ascii"))
        (directory / "helpers.ci").write_bytes(HELPERS.encode("ascii"))
        ref = call([sys.executable, "-B", "-m", "cint_ref", "run", source, "--entry", "run", "--path", "main.ci", "--format", "2"], ROOT / "ref")
        modules = ["records.ci", "helpers.ci", "main.ci"] if "import helpers;" in body else ["records.ci", "main.ci"]
        emitted = call([args.exe, "--json", "emit-c", "--root", directory, "--out", directory / "out", *modules], ROOT)
        row = {"case": name, "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(), "reference": commands[-2], "emit": commands[-1], "passed": False}
        if name in REFUSALS:
            diagnostics = [json.loads(line) for line in emitted.stderr.splitlines()]
            row["passed"] = ref.returncode == 0 and "outcome refused\n" in ref.stdout and emitted.returncode == 2 and \
                len(diagnostics) == 1 and diagnostics[0].get("code") == "C9102"
        elif name in DIAGNOSTICS:
            expected = dict(line.split(" ", 1) for line in ref.stdout.splitlines() if " " in line)
            diagnostics = [json.loads(line) for line in emitted.stderr.splitlines()]
            got = [(d.get("code"), "%s:%s:%s" % (d.get("file"), d.get("line"), d.get("column"))) for d in diagnostics]
            row["passed"] = ref.returncode == 0 and expected.get("outcome") == "compile-error" and emitted.returncode == 2 and got == [(expected.get("diagnostic.code"), expected.get("diagnostic.position"))]
        elif ref.returncode == 0 and any("outcome " + kind + "\n" in ref.stdout for kind in ("value", "fault")) and emitted.returncode == 0:
            if args.emit_only:
                row["passed"] = True
            else:
                native_tools()
                sources = emitted_sources(directory)
                try:
                    objects = tc.compile(sources, directory, name)
                    library = tc.link_shared(objects + runtime, directory / name, name)
                    cases = directory / "cases.txt"; cases.write_bytes(b"main run\n")
                    result = call([harness, library, "--cases", cases, "--format", "2"], directory, tc.env)
                    row["run"] = commands[-1]
                    row["passed"] = result.returncode == 0 and not result.stderr and observed(ref.stdout) == observed(result.stdout)
                except cc.BuildError as error:
                    row["build_error"] = str(error)
        save(row)
    if not args.only or "public" in args.only:
        directory = args.out / "public"; directory.mkdir()
        source = directory / "main.ci"
        source.write_bytes((ROOT / "compiler/tests/import_views_public.ci").read_bytes())
        (directory / "records.ci").write_bytes(LIBRARY.encode("ascii"))
        (directory / "twin.ci").write_bytes(b"export struct R { Bool active; I64 value; }\n")
        ref = call([sys.executable, "-B", "-m", "cint_ref", "run", source, "--entry", "run", "--path", "main.ci", "--format", "2"], ROOT / "ref")
        emitted = call([args.exe, "--json", "emit-c", "--root", directory, "--out", directory / "out", "records.ci", "twin.ci", "main.ci"], ROOT)
        row = {"case": "public", "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
               "reference": commands[-2], "emit": commands[-1], "passed": False}
        if ref.returncode == 0 and "outcome value\n" in ref.stdout and emitted.returncode == 0:
            if args.emit_only:
                row["passed"] = True
            else:
                native_tools()
                try:
                    objects = tc.compile(emitted_sources(directory), directory, "public import views")
                    host = tc.compile([ROOT / "compiler/tests/import_views_host.c"], directory, "public import views host")
                    executable = tc.link_exe(objects + host + runtime, directory / "host", "public import views host")
                    result = call([executable], directory, tc.env)
                    row["run"] = commands[-1]
                    library = tc.link_shared(objects + runtime, directory / "audit", "public import views audit")
                    symbols = [tc.symbols(obj) for obj in objects]
                    exports = tc.exports(library)
                    row["audit"] = cc.audit_program(symbols, exports, tc.symbols(runtime[0])[0], tc.leg)
                    public = {symbol for symbol in exports if symbol.startswith(("cx", "cm"))}
                    expected = {"cm_4_main", "cm_7_records", "cm_4_twin"}
                    expected.update("cx_4_main_" + suffix for suffix in
                                    ("4_read", "5_fixed", "4_same", "6_copied", "4_fail", "3_run"))
                    row["public_symbols"] = sorted(public)
                    row["passed"] = result.returncode == 0 and not result.stderr and not row["audit"] and public == expected
                except cc.BuildError as error:
                    row["build_error"] = str(error)
        save(row)
    passed = sum(row["passed"] for row in rows)
    print("import views: %d of %d pass" % (passed, len(rows)))
    return 0 if passed == len(rows) else 1


if __name__ == "__main__":
    raise SystemExit(main())
