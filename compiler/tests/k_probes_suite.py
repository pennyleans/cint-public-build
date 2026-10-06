"""The 33 probe modules of the K reduction note, and seven more, as capacity tests (KR-11).

docs/design/notes/2026-10-05-k-reduction.md appendix A lists the 33 probes: each pushes one
module table toward its capacity per source byte. The seven more test the bound proofs
where the 33 do not reach: four refer to module constants whose initializers are not
literals (lemma S), one writes a text run before each of 300 holes (lemma N), and two are
box 12's shapes that grew with the product of two source sizes before each deferred
statement and each conversion between two sets was lowered once (lemma S): 30 sets
widened between two orders at 30 calls, and a deferred assignment of 100 terms with 60
returns past it.
compiler/tests/k_probes_host.c runs each through the real scan, parse, check, declaration
and lowering passes of compiler/tests/table_capacity.ci, with the tables main.plan plans
for a build of that one module (SPEC-09 CINTC-02), and this part requires, of every probe:

  - every pass succeeds, with no diagnostic;
  - lemma N (SPEC-09 CINTC-16 row 4): the parser writes at most one node per source byte
    besides the module node, so at most T = N + 1 nodes for N source bytes;
  - lemma W (row 14): the checker's work stack never holds more frames than the module has
    nodes, as for every module without box 09's constructs, so the rows it writes, the
    interval rows included, stay within 2T + 1, 31 below its limit;
  - lowering holds at most one frame per node (row 14);
  - lemma S (row 6): lowering writes at most 4 SIR rows per source byte and 16 more, a
    reference to a module constant being one row.

The probes are generated here; they are not conformance cases.
"""

import hashlib
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
TESTS = ROOT / "compiler" / "tests"
REQUIRES = ["compiler/scan.ci", "compiler/parse.ci", "compiler/resolve.ci", "compiler/check.ci",
            "compiler/decl.ci", "compiler/lowering.ci", "compiler/tests/table_capacity.ci",
            "compiler/tests/k_probes_host.c"]
RECORDS = ("scan_5_Token", "parse_4_Node", "parse_5_Frame", "limits_12_CompilerDiag",
           "resolve_3_Sem", "resolve_3_Sym", "resolve_4_Name", "limits_4_Decl",
           "lowering_3_Sir", "lowering_7_SirFunc", "lowering_7_SirSlot", "lowering_6_LState")


def probes() -> dict:
    """The 33 probes of appendix A, then the seven for the bound proofs, by name."""
    p = {}
    n = 400
    p["or_param"] = "export Bool run(Bool a) {\n    return " + "||".join(["a"] * n) + ";\n}\n"
    p["or_local"] = "export Bool run(Bool p) {\n    Bool a = p;\n    return " + "||".join(["a"] * n) + ";\n}\n"
    p["and_param"] = "export Bool run(Bool a) {\n    return " + "&&".join(["a"] * n) + ";\n}\n"
    p["or_not"] = "export Bool run(Bool a) {\n    return " + "||".join(["!a"] * n) + ";\n}\n"
    p["bang_names"] = "export Bool run(Bool a) {\n    return " + "!a&&" * 300 + "a;\n}\n"
    p["cond_chain"] = "export I64 run(Bool c, I64 x) {\n    return " + "c?x:" * 200 + "x;\n}\n"
    p["mixed_logic"] = "export Bool run(Bool a) {\n    return " + "&&".join(["(a||a)"] * 200) + ";\n}\n"
    p["cmp_or"] = "export Bool run(I64 a) {\n    return " + "||".join(["a<a"] * 300) + ";\n}\n"
    p["const_cond"] = "export I64 run() {\n    return " + "true?1:" * 60 + "1;\n}\n"
    p["const_sum"] = "export I64 run() {\n    I64 s = " + "+".join(["1"] * 800) + ";\n    return s;\n}\n"
    p["for_loops"] = "export I64 run() {\n    I64 s = 0;\n" + "for i in 0..3{}\n" * 300 + "    return s;\n}\n"
    p["const_not"] = "export Bool run() {\n    return " + "!" * 250 + "true;\n}\n"
    p["not_chain"] = "export Bool run(Bool a) {\n    return " + "!" * 200 + "a;\n}\n"
    p["incr_stmts"] = "export I64 run(I64 p) {\n    I64 x = p;\n" + "x++;\n" * 800 + "    return x;\n}\n"
    p["print_holes"] = "I64 a = 1;\n\"" + "{a}" * 600 + "\\n\";\n"
    p["if_stmts"] = "export I64 run(Bool a) {\n" + "if(a){}\n" * 500 + "    return 0;\n}\n"
    p["elem_compound"] = "export I64 run(I64 i) {\n    I64[4] a;\n" + "a[i]+=1;\n" * 500 + "    return a[0];\n}\n"
    p["switch_items"] = ("export I64 run(I64 x) {\n    switch (x) {\n        case " +
                         ",".join(str(i) for i in range(800)) +
                         ": return 1;\n        default: return 0;\n    }\n}\n")
    p["not_stmt"] = "export Bool run(Bool a) {\n    Bool b = a;\n" + "b=!b;\n" * 600 + "    return b;\n}\n"
    p["add_chain"] = "export I64 run(I64 x) {\n    return " + "+".join(["x"] * 800) + ";\n}\n"
    p["while_loops"] = "export I64 run(Bool a) {\n" + "while(a){}\n" * 400 + "    return 0;\n}\n"
    p["switch_arms"] = ("export I64 run(I64 x) {\n    switch (x) {\n" +
                        "".join("case %d:{}\n" % i for i in range(500)) +
                        "        default: return 0;\n    }\n    return 1;\n}\n")
    p["nested_switch"] = ("export I64 run(I64 x) {\n" +
                          "".join("switch (x) { case %s: {\n" % ",".join(str(10 * k + j) for j in range(10))
                                  for k in range(40)) +
                          "return 1;\n" + "} default: {} }\n" * 40 + "    return 0;\n}\n")
    p["call_args"] = ("I64 f(I64 a) { return a; }\nexport I64 run(I64 x) {\n    I64 s = 0;\n" + "s=f(x);\n" * 500 +
                      "    return s;\n}\n")
    p["field_chain"] = ("struct A { I64 v; }\nstruct B { A a; }\nstruct C { B b; }\nexport I64 run() {\n"
                        "    C c = C(B(A(1)));\n    I64 s = 0;\n" + "s=c.b.a.v;\n" * 400 + "    return s;\n}\n")
    p["struct_ctor"] = ("struct P {\n" + "".join("    I64 f%d;\n" % i for i in range(40)) +
                        "}\nexport I64 run(I64 x) {\n" +
                        "".join("    P p%d = P(%s);\n" % (k, ",".join(["x"] * 40)) for k in range(20)) +
                        "    return p0.f0;\n}\n")
    p["neg_lits"] = "export I64 run() {\n    I64 s = 0;\n" + "s=-1;\n" * 600 + "    return s;\n}\n"
    p["const_neg"] = "export I64 run() {\n    return " + "-(" * 200 + "1" + ")" * 200 + ";\n}\n"
    p["neg_chain"] = "export I64 run(I64 x) {\n    return " + "-(" * 200 + "x" + ")" * 200 + ";\n}\n"
    p["else_if"] = "export I64 run(Bool a) {\n    if(a){}" + "else if(a){}" * 400 + "\n    return 0;\n}\n"
    p["items_1"] = ("export I64 run(I64 x) {\n    switch (x) {\n        case 0,1: return 1;\n"
                    "        default: return 0;\n    }\n}\n")
    p["as_chain"] = "export I64 run(I64 x) {\n    return x" + " as I32 as I64" * 150 + ";\n}\n"
    p["nest_paren"] = "export I64 run(I64 x) {\n    return " + "(" * 200 + "x" + ")" * 200 + ";\n}\n"
    p["const_refs"] = "const I64 C = 1+1+1+1+1;\nexport I64 run() {\n    return " + "+".join(["C"] * n) + ";\n}\n"
    p["const_bool"] = ("const Bool B = true||true;\nexport Bool run() {\n    return " + "||".join(["B"] * n) +
                       ";\n}\n")
    p["const_chain"] = ("const I64 C0 = 1+1;\n" +
                        "".join("const I64 C%d = C%d+C%d;\n" % (i, i - 1, i - 1) for i in range(1, 13)) +
                        "export I64 run() {\n    return C12;\n}\n")
    p["const_script"] = ("const I64 C = 1+1+1+1+1;\nI64 f() {\n    return " + "+".join(["C"] * n) +
                         ";\n}\n_ = f();\n")
    p["text_holes"] = "I64 x = 1;\n\"" + "a{x}" * 300 + "\";\n"
    p["widen_pairs"] = ("".join("error E%d { v%d }\n" % (i, i) for i in range(30)) +
                        "error S = " + " | ".join("E%d" % i for i in range(30)) + ";\n" +
                        "error W = " + " | ".join("E%d" % i for i in reversed(range(30))) + ";\n" +
                        "I64 f(W e) {\n    return 1;\n}\nexport I64 run() {\n    S s = E0.v0;\n"
                        "    I64 n = 0;\n" + "    n += f(s);\n" * 30 + "    return n;\n}\n")
    p["defer_returns"] = ("export I64 run(I64 p) {\n    I64 s = 0;\n    defer s = s" + "+1" * 99 + ";\n" +
                          "".join("    if (p == %d) { return %d; }\n" % (i, i) for i in range(60)) +
                          "    return s;\n}\n")
    return p


def prepare(out: pathlib.Path) -> list:
    """Writes each probe to out; (name, path, bytes) in appendix A's order."""
    out.mkdir(parents=True, exist_ok=True)
    rows = []
    for name, text in probes().items():
        data = text.encode("ascii")
        path = out / (name + ".ci")
        path.write_bytes(data)
        rows.append((name, path, len(data)))
    return rows


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


def problems(name: str, n: int, fields: list) -> list:
    """What a probe's counts break, as text; empty when it meets every bound."""
    result, nodes, tokens, parse_frames, check_frames, lower_frames, sir, ndiag, code = fields
    t = n + 1
    found = []
    if result != 0 or ndiag != 0:
        found.append("pass %d stopped, %d diagnostics, first C%d" % (result, ndiag, code))
    if nodes > t:
        found.append("lemma N: %d nodes for %d tokens" % (nodes, t))
    if check_frames > nodes:
        found.append("lemma W: %d checker frames for %d nodes" % (check_frames, nodes))
    if lower_frames > nodes:
        found.append("%d lowering frames for %d nodes" % (lower_frames, nodes))
    if sir > 4 * n + 16:
        found.append("lemma S: %d SIR rows for %d bytes" % (sir, n))
    if tokens > t or parse_frames > min(512 + t // 32, 131072):
        found.append("%d tokens, %d parse frames" % (tokens, parse_frames))
    return ["%s: %s" % (name, f) for f in found]


def run(tc, tools, work: pathlib.Path, emitted: dict, prepared, cc) -> tuple:
    total = len(prepared)
    c_file = work / "k_probes.c"
    code, err = cc.run_seed(tools["seed"], ROOT / "compiler", "tests/table_capacity.ci", c_file)
    if code != 0 or cc.sanitizer_reports(err):
        return 0, total, ["seed: exit %d: %s" % (code, " ".join(err.split())[:400])]
    text = c_file.read_bytes()
    digest = hashlib.sha256(text).hexdigest()
    if b"\r" in text:
        return 0, total, ["emitted C holds a CR byte"]
    if emitted.setdefault("tests/table_capacity.ci", digest) != digest:
        return 0, total, ["emitted C differs from the first configuration's"]
    try:
        ids = records(text.decode("ascii"))
        objs = tc.compile([c_file, TESTS / "k_probes_host.c"], work, "k_probes")
        exe = tc.link_exe(objs + [tools["rt_obj"]], work / "k-probes-host", "k_probes")
    except (cc.BuildError, ValueError) as e:
        return 0, total, ["build: %s" % " ".join(str(e).split())[:600]]
    listing = work / "k_probes.list"
    listing.write_bytes("".join("%s\n" % path for _, path, _ in prepared).encode("utf-8"))
    env = dict(tc.env)
    env["ASAN_OPTIONS"] = "detect_leaks=1:abort_on_error=0"
    env["UBSAN_OPTIONS"] = "print_stacktrace=1:halt_on_error=1"
    r = subprocess.run([str(exe), *ids, str(listing)], cwd=work, env=env, capture_output=True)
    stderr = r.stderr.decode("utf-8", "replace")
    if r.returncode != 0 or cc.sanitizer_reports(stderr):
        return 0, total, ["host exit %d: %s" % (r.returncode, " ".join(stderr.split())[:400])]
    (work / "k_probes.stdout").write_bytes(r.stdout)
    lines = {}
    for line in r.stdout.decode("ascii").splitlines():
        parts = line.split(" ")
        if len(parts) == 12 and parts[0] == "probe":
            lines[parts[1]] = [int(x) for x in parts[2:]]
    passed, failures = 0, []
    for name, path, n in prepared:
        got = lines.get(str(path))
        if got is None or got[0] != n:
            failures.append("%s: no result" % name)
            continue
        found = problems(name, n, got[1:])
        failures += found
        passed += not found
    return passed, total, failures
