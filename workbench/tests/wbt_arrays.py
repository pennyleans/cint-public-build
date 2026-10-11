"""Arrays, views, and kernels in explain, the inspector, and reports (box 14 unit 4a; sections
5.6, 5.7, and 6.1 of the box 14 note).

Run by workbench/tests/test_workbench.py for each configuration it builds (its units()), or on its
own with `python workbench/tests/wbt_arrays.py --leg LEG [--cint PATH]`. It builds its driver,
workbench/tests/wbt_arrays.c, against the workbench library objects of the configuration, and
checks:

- explain of the SIR text that B1 wrote for the kernel and reduction cases kept under
  workbench/tests/fixtures/sir/: the revision identity recorded in fixtures/sir/REVISIONS
  (SPEC-09 SIR-17), the functions and kernels as test_workbench.py reads them, and for each
  kernel its parameters, domain, shapes, aliasing, reads, writes, and
  publication, and each reduction with its result and accumulator (SPEC-01 IM-80, IM-87), against
  a reading of the same text written here; one kernel in the text form, byte for byte; with
  --cint, that B1 still writes those fixtures (--write-fixtures with --cint writes them anew
  when the SIR text B1 writes changes);
- the inspector's /grid, /stats, and /view of views that the driver builds: the text of a few,
  /stats against sums computed here, and the views and windows it refuses;
- the report of every frozen conformance/**/*.expect file and of the same outcome as a block of
  `cint-harness --cases` output: each canonical line against the file's lines (SPEC-09 CONF-11;
  box 14 ruling BX14-05 A), for faults, error results, values, and compile errors.
"""
from __future__ import annotations

import json
import pathlib
import random
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
FIXTURES = HERE / "fixtures" / "sir"
SITE = re.compile(r" +#\d+ @\S+:\d+:\d+$")
CMP = {"1": "==", "2": "!=", "3": "<", "4": "<=", "5": ">", "6": ">="}
SEQUENTIAL = ("sum", "dot", "fold_checked", "sum_wrap", "sum_sat", "reduce_min", "reduce_max", "count")
CODE_NAMES = ["E_OVERFLOW", "E_DIV_ZERO", "E_BOUNDS", "E_SHAPE", "E_SHIFT", "E_NARROW", "E_ALIAS",
              "E_STALE_HANDLE", "E_FUEL", "E_UNSUPPORTED", "E_DOMAIN", "E_DEPTH", "E_ASSERT"]

# One kernel in the text form (default BX14-12; box 09's kernels, SPEC-02): the SIR text follows it.
DIFFUSE1 = """\
kernel diffuse1   kernel/where_violation.sites:3   pure
  signature  (in u: I32[_], out next: I32[_])
  domain     over [i: n]   row-major work-items, one fuel unit each, charged by the dispatch before any runs \
(SPEC-02 K-8, K-9, X-8)
  shapes     n = extent 0 of u; next extent 0 = n; where n >= 2   else E_SHAPE at entry (SPEC-02 F-5)
  aliasing   next must not overlap u   checked at entry, E_ALIAS (SPEC-02 A-8, F-5)
  reads      u: 1 element load
  writes     next: 1 element store, in each work-item's own block (SPEC-02 K-10), every element of the block \
on every path that completes (K-10a, checked by cintc)
  publish    next, only when every work-item completes; a fault publishes nothing
  fault sites (2): 2 possible, 0 statically excluded
    diffuse1#1  kernel/where_violation.ci:8:9   index.checked.i32  E_BOUNDS possible
    diffuse1#2  kernel/where_violation.ci:8:16  index.checked.i32  E_BOUNDS possible
  hint       not analyzed: excluding a possible site needs range analysis, which no component performs yet
  fuel       fuel-v1 charge points (SPEC-01 IM-139, SPEC-09 SIR-07): none in this body
  lowering
    cpu-sir-interp    supported   the workbench evaluator runs this text (SPEC-09 BACK-01a)
    cpu-c17 (ref-c)   supported   cintc emits C17 from the same SIR (SPEC-09 BACK-03)
  sir
"""

# The inspector: (arguments of `wbt_arrays inspect`, the text it prints).
GRID = """\
a : I32[3, 4]   12 elements   host   lower bounds [1, 0]
  grid   rows 1 to 3 of 1 to 3, columns 0 to 3 of 0 to 3
     0   1   2   3
  1  1  -2   3   0
  2  5   6  -7   8
  3  9  10   0  12
  count    12
  sum      45   accumulator I36 (SPEC-01 IM-80)
  min      -7   first at [2, 2]
  max      12   first at [3, 3]
  nonzero  10
  view
    buffer      none: this buffer has no canonical identity (SPEC-01 IM-81)
    element     I32
    rank        2
    shape       [3, 4]
    lower       [1, 0]
    origin      0
    strides     [4, 1] elements
    permission  write
    placement   host
"""
INSPECTED = [
    (["I32", "3,4", "1,0", "4,1", "0", "1,-2,3,0,5,6,-7,8,9,10,0,12", "/all"], GRID),
    (["I32", "3,4", "0,0", "4,1", "0", "1,-2,3,0,5,6,-7,8,9,10,0,12"],
     "a : I32[3, 4]   12 elements   host\n" + "".join("  [%d, %d] = %d\n" % (i // 4, i % 4, v) for i, v in
                                                       enumerate([1, -2, 3, 0, 5, 6, -7, 8]))
     + "  ... 4 more; /grid and /stats show them\n"),
    (["U8", "5", "0", "-1", "4", "1,2,3,4,250", "/grid", "[1..4]", "/stats"],
     "a : U8[5]   5 elements   host\n  grid   columns 1 to 3 of 0 to 4\n    1  2  3\n    4  3  2\n"
     "  count    5\n  sum      260   accumulator U11 (SPEC-01 IM-80)\n  min      1     first at [4]\n"
     "  max      250   first at [0]\n  nonzero  5\n"),
    (["I64", "2,3", "0,0", "1,2", "0", ",".join(["9223372036854775807"] * 6), "/stats"],
     "a : I64[2, 3]   6 elements   host\n  count    6\n"
     "  sum      55340232221128654842   accumulator I67 (SPEC-01 IM-80)\n"
     "  min      9223372036854775807    first at [0, 0]\n  max      9223372036854775807    first at [0, 0]\n"
     "  nonzero  6\n"),
    (["I32", "0,4", "0,0", "4,1", "0", "-", "/stats"],
     "a : I32[0, 4]   0 elements   host\n  count    0\n  sum      0   accumulator I32 (SPEC-01 IM-80)\n"
     "  min      none, no elements\n  max      none, no elements\n  nonzero  0\n"),
    (["I16", "2", "0", "1", "0", "1050,-7", "/scale", "100", "/stats"],
     "a : I16[2]   2 elements   host\n  count    2\n  sum      10.43   accumulator I17 (SPEC-01 IM-80)   raw 1043\n"
     "  min      -0.07   first at [1]\n  max      10.5    first at [0]\n  nonzero  2\n"),
]
REFUSED = [
    (["I32", "3,4", "0,0", "4,1", "1", "1,2,3", "/stats"], "the view reaches outside its buffer of 3 elements"),
    (["I8", "2,2,2", "0,0,0", "4,2,1", "0", "1,2,3,4,5,6,7,8", "/grid"], "/grid shows rank 1 or 2; this value has rank 3"),
    (["I8", "2,2", "0,0", "2,1", "0", "1,2,3,4", "/grid", "[0..3,", "0..1]"],
     "the /grid window lies outside the value's indices"),
    (["I8", "2", "0", "1", "0", "1,2", "/grid", "[1..0]"],
     "/grid takes a window [r0..r1, c0..c1] or [c0..c1], half-open, not [1..0]"),
    (["I8", "2", "0", "1", "0", "1,2", "/ratio"], "/ratio waits for fixed point"),
]


def driver(case, wb, exes) -> pathlib.Path:
    """wbt_arrays.c linked with the library objects that wb.build left beside the executables."""
    outdir = pathlib.Path(exes["test_wb"]).parent
    tc = wb.cc.Toolchain(case.leg, "2" if outdir.name.startswith("O2") else "0", "portable",
                         outdir.name.endswith("-san"))
    inc = "/I" if case.leg == "msvc" else "-I"
    tc.flags = tc.flags + [inc + str(wb.WB), inc + str(wb.ROOT / "cli"), inc + str(wb.ROOT / "interp")]
    proc = "cint_proc_win.c" if case.leg == "msvc" else "cint_proc_posix.c"
    lib = [outdir / (pathlib.Path(name).stem + tc.obj) for name in [*wb.LIBRARY, proc, *wb.LINKED]]
    return tc.link_exe(tc.compile([HERE / "wbt_arrays.c"], outdir, "wbt_arrays") + lib, outdir / "wbt_arrays",
                       "wbt_arrays")


# ----------------------------------------------------------------------------
# explain

def bare(operand: str) -> str:
    return operand[3:] if operand.startswith("%p.") else operand.lstrip("%")


def read_kernels(text: str) -> dict:
    """Each kernel of a module's SIR text, read here independently of wb_explain.c: its parameters
    with their directions, and what its body checks, reads, writes, and publishes."""
    lines, out = text.split("\n"), {}
    for at, line in enumerate(lines):
        head = line[len("export "):] if line.startswith("export ") else line
        if not head.startswith("kernel @"):
            continue
        body = [SITE.sub("", step).strip() for step in lines[at + 1:lines.index("}", at)]]
        ops = [step.split(" = ", 1)[1] if step.startswith("%") and " = " in step else step for step in body]
        args = lambda op: op.split(" ", 1)[1].split(", ") if " " in op else []
        temps = {step.split(" = ")[0]: step.split(" = ", 1)[1].split(" ")[0] for step in body if " = " in step}
        stages = {args(op)[0]: (bare(args(op)[1]), args(op)[2]) for op in ops if op.startswith("dispatch.stage ")}
        params = []
        for item in re.split(r", (?=%)", head[head.index("(") + 1:head.rindex(")")]):
            name, ptype = item.split(": ", 1)
            direction = ptype.split(" ")[0] if ptype.startswith(("out ", "inout ")) else "in"
            ptype = ptype.split(" ", 1)[1] if direction != "in" else ptype
            mode = [m for slot, (p, m) in stages.items() if p == bare(name)]
            direction = {"1": "out", "2": "inout"}[mode[0]] if mode else direction
            params.append({"direction": direction, "name": bare(name), "type": ptype})
        loads = {p["name"]: 0 for p in params if "[" in p["type"]}
        for op in ops:
            if op.startswith("index.load "):
                view = args(op)[0]
                name = stages[view][0] if view in stages else bare(view)
                loads[name] = loads.get(name, 0) + 1
        stores = {stages[s][0]: sum(op.startswith("index.store %s," % s) for op in ops) for s in stages}
        k = {"aliasing": ["%s must not overlap %s" % (bare(args(op)[0]), bare(args(op)[1]))
                          for op in ops if op.startswith("bind.alias ")],
             "publish": [bare(args(op)[0]) for op in ops if op.startswith("dispatch.publish ")],
             "reads": ["%s: %d element load%s" % (n, c, "" if c == 1 else "s") for n, c in loads.items() if c],
             "writes": ["%s: %d element store%s" % (stages[s][0], stores[stages[s][0]], "" if stores[stages[s][0]] == 1
                                                     else "s") for s in stages],
             "full": [stages[s][1] == "1" for s in stages],
             "rank": int(next(op for op in ops if op.startswith("dispatch.charge ")).split(", ")[1]),
             "where": [CMP[args(op)[1]] for op in ops if op.startswith("bind.where ")],
             "shapes": sum(op.startswith(("bind.shape ", "bind.where ")) for op in ops)
             + sum(1 for op in ops if op.startswith("store.local $") and not op.startswith("store.local $sc.")
                   and temps.get(args(op)[1]) == "view.extent"),
             "parameters": params}
        out[head[head.index("@"):head.index("(")].rsplit(".", 1)[1]] = k
    return out


def result_of(op: str) -> str:
    """The result type of a reduction's operation, and its accumulator text, read here."""
    word, *rest = op.split(".")
    elem = rest[-1] if word in ("sum_wrap", "sum_sat", "count") else rest[1]
    result = (rest[2] if word in ("sum", "dot") else "i64" if word == "count" else elem)
    name = "Bool" if result == "bool" else result[0].upper() + result[1:]
    if word not in ("sum", "dot"):
        return name, None
    bits = int(elem[1:]) * (2 if word == "dot" else 1)
    return name, "%d + ceil(log2 n) bits for n terms (SPEC-01 %s)" % (bits, "IM-87" if word == "dot" else "IM-80")


def reductions_of(text: str, name: str, kind: str) -> list:
    """The reductions of one function or kernel of a module's SIR text: (id, operation, target)."""
    lines = text.split("\n")
    sites = {int(o): op for o, _, _, op in (item.split(":", 3) for item in lines[1].split(" ")[2:])}
    at = next(i for i, line in enumerate(lines) if re.match(r"(export )?(func|kernel) @[\w.]+\.%s\(" % name, line))
    body, out = lines[at + 1:lines.index("}", at)], []
    ordinal = lambda step: int(re.search(r"#(\d+) @", step).group(1)) if "#" in step else None
    if kind != "kernel":
        return [("%s#%d" % (name, ordinal(s)), sites[ordinal(s)], None) for s in body
                if ordinal(s) and sites[ordinal(s)].split(".")[0] in SEQUENTIAL]
    for step in body:
        m = re.match(r"\s*reduce\.add (\S+) (%[\w.]+),", SITE.sub("", step))
        if m and all(t != bare(m.group(2)) for _, _, t in out):
            final = [ordinal(s) for s in body if re.match(r"\s*reduce\.final \S+ %s[, ]" % re.escape(m.group(2)), s)
                     and ordinal(s)]
            site = final[0] if final else ordinal(step)
            out.append(("%s#%d" % (name, site) if site else None, m.group(1), bare(m.group(2))))
    return out


def check_explain(case, wb, explain) -> int:
    recorded = dict(line.split() for line in (FIXTURES / "REVISIONS").read_text(encoding="ascii").splitlines())
    checked, kernels = 0, 0
    for program, revision in recorded.items():
        for module in wb.sir_modules(FIXTURES / program):
            with case.subTest(program=program, module=module):
                result = wb.run([explain, "--json", "--sir", FIXTURES / program, "--root", wb.CONFORMANCE, module])
                case.assertClean(result, module)
                case.assertEqual((result.returncode, result.stderr), (0, b""), module)
                got = json.loads(result.stdout)
                case.assertEqual(result.stdout, wb.canonical(got), module)
                case.assertEqual((got["schema"], got["revision"]), ("CINT-EXPLAIN-1", revision), module)
                text = (FIXTURES / program / (module[:-len(".ci")] + ".sites")).read_text(encoding="utf-8")
                read = read_kernels(text)
                # Functions and kernels as test_workbench.py reads them; what a kernel checks, reads,
                # writes, and publishes, and the reductions, are checked below.
                funcs = [dict({k: v for k, v in f.items() if k not in wb.KERNEL_KEYS},
                              fault_sites=[{key: s[key] for key in ("id", "operation", "position")}
                                           for s in f["fault_sites"]]) for f in got["functions"]]
                case.assertEqual(funcs, wb.explained(FIXTURES / program, module), module)
                dispatches = re.findall(r"^ *(?:%t\d+ = )?dispatch @([\w.]+)\(.* #(\d+) @", text, re.M)
                case.assertEqual(sorted((c["callee"], c["id"].split("#")[1]) for f in got["functions"] for c in f["fuel"]
                                        if c["charge"] == "dispatch"), sorted(dispatches), module)
                case.assertEqual(bool(dispatches), program.startswith("kernel__"), module)
                for f in got["functions"]:
                    want = [{"id": i, "operation": op, "result": result_of(op)[0],
                             **({"target": t} if t is not None else {})}
                            for i, op, t in reductions_of(text, f.get("name"), f["kind"])]
                    have = f.get("reductions", [])
                    case.assertEqual([{k: v for k, v in r.items() if k in ("id", "operation", "result", "target")}
                                      for r in have], want, f.get("name"))
                    case.assertEqual([r["accumulator"] for r in have], [result_of(op)[1] for _, op, _ in
                                     reductions_of(text, f.get("name"), f["kind"])], f.get("name"))
                    if f["kind"] != "kernel":
                        continue
                    k = read[f["name"]]
                    kernels += 1
                    case.assertEqual((f["parameters"], f["aliasing"], f["publish"], f["reads"]),
                                     (k["parameters"], k["aliasing"], k["publish"], k["reads"]), f["name"])
                    case.assertEqual(len(f["writes"]), len(k["writes"]), f["name"])
                    for item, start, full in zip(f["writes"], k["writes"], k["full"]):
                        case.assertTrue(item.startswith(start + ", in each work-item's own block (SPEC-02 K-10)"), item)
                        case.assertEqual(item.endswith("(K-10a, checked by cintc)"), full, item)
                    case.assertTrue(f["domain"].startswith("over [") and f["domain"].endswith("]"), f["domain"])
                    case.assertEqual(len(f["domain"][len("over ["):-1].split(", ")), k["rank"], f["domain"])
                    case.assertEqual(len(f["shapes"]), k["shapes"], f["name"])
                    case.assertEqual([re.search(r" (\S+) ", s[len("where "):]).group(1) for s in f["shapes"]
                                      if s.startswith("where ")], k["where"], f["name"])
                checked += 1
    case.assertGreaterEqual(kernels, 9)
    golden = wb.run([explain, "--sir", FIXTURES / "kernel__where_violation", "--root", wb.CONFORMANCE,
                     "kernel/where_violation.ci:diffuse1"])
    case.assertClean(golden, "diffuse1")
    out = golden.stdout.decode("utf-8")
    sir = (FIXTURES / "kernel__where_violation" / "kernel" / "where_violation.sites").read_text(encoding="utf-8")
    body = sir[sir.index("kernel @"):sir.index("\n}\n", sir.index("kernel @")) + 3]
    case.assertEqual((golden.returncode, out[out.index("\n\n") + 2:]), (0, DIFFUSE1 + body))
    return checked + 1 + check_fixtures(case, wb, recorded)


def check_fixtures(case, wb, recorded) -> int:
    """With --cint, B1 still writes each fixture byte for byte."""
    if wb.ARGS.cint is None:
        return 0
    cint = pathlib.Path(wb.ARGS.cint).resolve()
    with tempfile.TemporaryDirectory() as tmp:
        for program in recorded:
            module = wb.sir_modules(FIXTURES / program)[0]
            out = pathlib.Path(tmp) / program
            result = wb.run([cint, "emit-c", "--root", wb.CONFORMANCE, "--out", out, module])
            case.assertEqual(result.returncode, 0, result.stderr)
            stage = pathlib.Path(tmp) / (program + ".stage-0")
            files = sorted(p.relative_to(stage) for p in stage.rglob("*.sites"))
            case.assertEqual(files, sorted(p.relative_to(FIXTURES / program)
                                           for p in (FIXTURES / program).rglob("*.sites")), program)
            for rel in files:
                case.assertEqual((stage / rel).read_bytes(), (FIXTURES / program / rel).read_bytes(), str(rel))
    return len(recorded)


def write_fixtures(cint, wb) -> None:
    """The fixtures and REVISIONS as `cint` (a B1) writes them now, for the same programs."""
    programs = [line.split()[0] for line in (FIXTURES / "REVISIONS").read_text(encoding="ascii").splitlines()]
    revisions = []
    with tempfile.TemporaryDirectory() as tmp:
        for program in programs:
            module = wb.sir_modules(FIXTURES / program)[0]
            out = pathlib.Path(tmp) / program
            subprocess.run([str(cint), "emit-c", "--root", str(wb.CONFORMANCE), "--out", str(out), module], check=True)
            stage = pathlib.Path(tmp) / (program + ".stage-0")
            shutil.rmtree(FIXTURES / program)
            for path in stage.rglob("*.sites"):
                dest = FIXTURES / program / path.relative_to(stage)
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_bytes(path.read_bytes())
            m = re.search(rb"static const uint8_t cg_revision\[32\] = \{\n(.*?)\n\};",
                          (stage / "cint-program.c").read_bytes(), re.S)
            revisions.append("%s %s\n" % (program, "".join("%02x" % int(x, 0) for x in
                                                         m.group(1).decode("ascii").replace("\n", " ").split(",") if x.strip())))
    (FIXTURES / "REVISIONS").write_bytes("".join(revisions).encode("ascii"))


# ----------------------------------------------------------------------------
# The inspector

def check_inspector(case, wb, exe) -> int:
    for argv, want in INSPECTED:
        result = wb.run([exe, "inspect", *argv])
        case.assertClean(result, argv)
        case.assertEqual((result.returncode, result.stdout.decode("utf-8")), (0, want), argv)
    for argv, reason in REFUSED:
        result = wb.run([exe, "inspect", *argv])
        case.assertEqual((result.returncode, result.stdout.decode("utf-8")), (1, reason + "\n"), argv)
    rng = random.Random(4)
    for tag, bits, signed in (("I8", 8, True), ("U16", 16, False), ("I32", 32, True), ("I64", 64, True),
                              ("U64", 64, False), ("Bool", 1, False)):
        lo, hi = (-(1 << (bits - 1)), (1 << (bits - 1)) - 1) if signed else (0, (1 << bits) - 1)
        rows, cols = rng.randint(1, 9), rng.randint(1, 9)
        values = [rng.choice([lo, hi, 0, rng.randint(lo, hi)]) for _ in range(rows * cols)]
        # a transposed view: element [i, j] is values[j * rows + i]
        grid = [[values[j * rows + i] for j in range(cols)] for i in range(rows)]
        flat = [v for row in grid for v in row]
        out = wb.run([exe, "inspect", tag, "%d,%d" % (rows, cols), "0,0", "1,%d" % rows, "0",
                      ",".join(str(v if tag != "U64" or v < (1 << 63) else v - (1 << 64)) for v in values), "/stats"])
        case.assertClean(out, tag)
        n, width = len(flat), bits + (len(flat) - 1).bit_length()
        first = lambda v: "[%d, %d]" % divmod(flat.index(v), cols)
        text = lambda v: ("true" if v else "false") if tag == "Bool" else str(v)
        want = ("  count    %d\n" % n, "accumulator %s%d (SPEC-01 IM-80)" % ("I" if signed else "U", width),
                "  min      %s" % text(min(flat)), "first at %s" % first(min(flat)),
                "  max      %s" % text(max(flat)), "first at %s" % first(max(flat)),
                "  nonzero  %d\n" % sum(1 for v in flat if v))
        got = out.stdout.decode("utf-8")
        case.assertTrue(got.startswith("a : %s[%d, %d]   %d elements   host\n" % (tag, rows, cols, n)), got)
        case.assertIn("  sum      %d " % sum(flat), got)
        for piece in want:
            case.assertIn(piece, got, tag)
    return len(INSPECTED) + len(REFUSED) + 6


# ----------------------------------------------------------------------------
# Reports from .expect text

def want_report(lines: list) -> list:
    """The canonical lines of the report of a fault, an error, a value, or a compile error, from
    the outcome lines of a frozen file, written here independently of wb_fault.c."""
    kv = [(line.split(" ", 1) + [""])[:2] for line in lines]
    get = lambda key: [v for k, v in kv if k == key]
    one = lambda key: (get(key) or ["none"])[0]
    outcome = one("outcome")
    fuel = one("fuel-consumed")
    if outcome == "value":
        ret = get("return")
        head = ["value = %s : %s" % tuple(reversed(ret[0].split(" ", 1))) if ret else "value, none returned"]
        return head + ["   = fuel-consumed: " + fuel, "   = stdout-bytes: " + one("stdout-bytes")]
    if outcome == "error":
        return ["error: %s.%s (tag %s)" % (one("error.set"), one("error.value"), one("error.tag").split(" ")[1]),
                "   = error.set: " + one("error.set"), "   = error.value: " + one("error.value"),
                "   = error.tag: " + one("error.tag"), "   = fuel-consumed: " + fuel,
                "   = stdout-bytes: " + one("stdout-bytes")]
    prefix = "diagnostic.fault." if outcome == "compile-error" else "fault."
    fields = ["   = operation: " + one(prefix + "operation"),
              "   = operand-count: %d" % len(get(prefix + "operand"))]
    fields += ["   = operand: " + v for v in get(prefix + "operand")]
    fields += ["   = descriptor: " + v for v in get(prefix + "descriptor")]
    fields += ["   = exact:   " + one(prefix + "exact"), "   = limit:   " + one(prefix + "limit")]
    code = one(prefix + "code")
    if outcome == "compile-error":
        head = ["error[%s]: the .expect text keeps no message (SPEC-09 CONF-11 rule 6)" % one("diagnostic.code"),
                "  --> " + one("diagnostic.position")]
        return head + (["fault[%s]: " % code] + fields if get(prefix + "code") else [])
    address = one("fault.address")
    if address != "none":
        d, i, s = address.split(" ")
        address = "%s, dispatch %s, phase %d, work-item %s, step %s" % (
            one("fault.kernel") if get("fault.kernel") else "", d,
            ["entry", "work-item", "epilogue"].index(one("fault.phase")) if get("fault.phase") else 0, i, s)
    return (["fault[%s]: " % code, "  --> " + one("fault.position")] + fields
            + ["   = revision: " + one("fault.revision"), "   = source-map: " + one("fault.source-map"),
               "   = address: " + address, "   = stack-depth: " + one("fault.stack-depth")]
            + ["   = called from: " + v for v in get("fault.stack")]
            + ["   = fuel-consumed: %s (not part of the fault record)" % fuel])


def canonical_lines(report: str) -> list:
    """The report's lines without the source excerpts, the headline's text, and what follows the
    lines that are not canonical."""
    out = []
    for line in report.split("\n"):
        if line.startswith("   = noncanonical") or line.startswith("-- format"):
            break
        if line.startswith("fault["):
            line = line[:line.index("]: ") + 3]
        if line and not re.match(r"^ *\d* \|", line):
            out.append(line)
    return out


def check_reports(case, wb, exe) -> int:
    checked, kinds = 0, set()
    files = sorted(wb.CONFORMANCE.rglob("*.expect"))
    case.assertGreater(len(files), 500)
    with tempfile.TemporaryDirectory() as tmp:
        block = pathlib.Path(tmp) / "block"
        for path in files:
            text = path.read_text(encoding="ascii")
            lines = text.split("\n")[:-1]
            outcome = next(line.split(" ", 1)[1] for line in lines if line.startswith("outcome "))
            if outcome == "refused":
                continue
            kinds.add(outcome)
            result = wb.run([exe, "expect", path, wb.CONFORMANCE])
            case.assertClean(result, path.name)
            case.assertEqual(result.returncode, 0, (path, result.stdout))
            got = result.stdout.decode("utf-8")
            case.assertEqual(canonical_lines(got), want_report(lines), path)
            fmt = next((line.split(" ")[1] for line in lines if line.startswith("format ")), "1")
            case.assertTrue(got.endswith("-- format %s, %d of %d bytes\n" % (fmt, len(text), len(text))), path)
            if outcome == "fault" and "\nfault.phase " in text:
                phase = re.search(r"^fault\.phase (\S+)$", text, re.M).group(1)
                case.assertIn("   = phase: %s (SPEC-02 F-3)\n" % phase, got, path)
            # The same outcome as cint-harness --cases writes it, followed by the next case.
            start = lines.index("outcome " + outcome)
            block.write_text("case 7\n" + "\n".join(lines[start:]) + "\ncase 8\noutcome value\n", encoding="ascii",
                             newline="\n")
            again = wb.run([exe, "expect", block, wb.CONFORMANCE])
            case.assertEqual(again.returncode, 0, path)
            case.assertEqual(again.stdout.decode("utf-8").rsplit("-- format", 1)[0], got.rsplit("-- format", 1)[0],
                             path)
            checked += 2
        for text, reason in BAD:
            block.write_text(text, encoding="ascii", newline="\n")
            result = wb.run([exe, "expect", block, wb.CONFORMANCE])
            case.assertEqual((result.returncode, result.stdout.decode("utf-8")), (1, reason + "\n"), text)
    case.assertEqual(kinds, {"value", "fault", "error", "compile-error"})
    return checked + len(BAD)


BAD = [  # (outcome text, the reason the driver prints)
    ("case x\noutcome fault\nfault.code E_NOPE\n", "line 3 is not a line of SPEC-09 CONF-11"),
    ("case x\noutcome value\nreturn I8 128\n", "line 3 is not a line of SPEC-09 CONF-11"),
    ("case x\nsource reference\n", "the text has no outcome line"),
    ("case x\noutcome fault\nfault.code E_SHAPE\nfault.operation bind.where\n",
     "the fault has no position"),
    ("outcome fault\nfault.code E_DEPTH\nfault.operation call.enter\nfault.position a.ci:1:1\n"
     "fault.source-map self\nfault.stack-depth 1\n",
     "fault.stack lines differ from fault.stack-depth")]


def check(case, wb, exes) -> int:
    """Unit 4a: explain of kernels, views, and reductions, the inspector's array formats, and
    reports from .expect text."""
    exe = driver(case, wb, exes)
    return check_explain(case, wb, exes["cint-explain"]) + check_inspector(case, wb, exe) + check_reports(case, wb, exe)


# ----------------------------------------------------------------------------
# On its own

class ArraysLeg(unittest.TestCase):
    leg = None

    def assertClean(self, result, what):
        err = result.stderr.decode("utf-8", "replace")
        self.assertNotIn("runtime error", err, what)
        self.assertNotIn("Sanitizer", err, what)

    def test_arrays(self):
        import test_workbench as wb
        if self.leg is None:
            self.skipTest("--leg not given")
        for config, opt, sanitized in wb.configurations(self.leg):
            outdir = (pathlib.Path(wb.ARGS.out) if wb.ARGS.out else wb.default_out(self.leg)) / "arrays" / config
            shutil.rmtree(outdir, ignore_errors=True)
            outdir.mkdir(parents=True)
            print("[%s %s] %d checks" % (self.leg, config, check(self, wb, wb.build(self.leg, opt, sanitized, outdir))))


if __name__ == "__main__":
    sys.path.insert(0, str(HERE))
    import argparse
    import test_workbench
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--leg", choices=["msvc", "gcc", "clang", "apple-clang"])
    parser.add_argument("--out")
    parser.add_argument("--cint")
    parser.add_argument("--write-fixtures", action="store_true", help="with --cint, write fixtures/sir/ anew")
    known, rest = parser.parse_known_args()
    test_workbench.ARGS.leg, test_workbench.ARGS.out, test_workbench.ARGS.cint = known.leg, known.out, known.cint
    if known.write_fixtures:
        if known.cint is None:
            parser.error("--write-fixtures needs --cint")
        write_fixtures(pathlib.Path(known.cint).resolve(), test_workbench)
        sys.exit(0)
    ArraysLeg.leg = known.leg
    unittest.main(argv=[sys.argv[0], "-v", *rest])
