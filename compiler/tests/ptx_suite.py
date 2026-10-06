"""The `ptx` suite: compiler/back_ptx.ci, the device code of the gpu-cuda backend (box 11 unit 3).

compiler/tests/ptx.ci builds one module through main.compile and writes the PTX of its
kernels with back_ptx.ci (entries named cint_k_<name>); compiler/tests/ptx_host.c runs it
over every kernel case of conformance/kernel/ and every source of compiler/tests/ptx/ (the
forms and refusals those cases miss), in one process per configuration. A case passes when:

  - its outcome is the one ptx_cases.txt lists: `ptx`, written PTX; `unsupported <cap>`,
    refused with that capability of back_ptx.ci's table (BACK-05); or `stopped`, a
    compile error the case is about;
  - its PTX equals compiler/tests/golden/<name>.ptx byte for byte where ptx_cases.txt
    marks the case `golden`, and is the same on every configuration (BACK-02);
  - when a ptxas is found (CINT_PTXAS, else `ptxas` on PATH), each written module
    assembles with no diagnostic for every target: sm_52, the modules' `.target`, when that
    ptxas generates code for it, else the lowest sm_<n> its --gpu-name option lists (ptxas
    13.x lists none below sm_75), and the targets that CINT_PTXAS_TARGETS adds
    (space-separated, for example `sm_86` on the development machine);
  - when CINT_PTX_DEVICE is 1, in the unsanitized -O2 configurations, the driver loads each
    written module and finds each of its entries through rt/cint_cuda.c
    (compiler/tests/ptx_jit.c): the JIT step, which needs an NVIDIA GPU.

A fault inside the compiler (CINTC-10) or a sanitizer report fails the configuration.
"""
import hashlib
import os
import pathlib
import re
import shutil
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
TESTS = ROOT / "compiler" / "tests"
GOLDEN = TESTS / "golden"
REQUIRES = ["compiler/back_ptx.ci", "compiler/tests/ptx.ci", "compiler/tests/ptx_host.c",
            "compiler/tests/ptx_cases.txt", "compiler/tests/ptx_jit.c"]
RECORDS = ("scan_5_Token", "parse_4_Node", "parse_5_Frame", "limits_12_CompilerDiag",
           "resolve_3_Sem", "resolve_3_Sym", "resolve_4_Name", "limits_4_Decl",
           "lowering_3_Sir", "lowering_7_SirFunc", "lowering_7_SirSlot", "lowering_6_LState",
           "back_x5Fc_4_PMod")


def cases() -> list:
    """(relative path, expected outcome, golden) for each line of ptx_cases.txt."""
    out = []
    for line in (TESTS / "ptx_cases.txt").read_text(encoding="ascii").splitlines():
        if not line or line.startswith("#"):
            continue
        fields = line.split()
        golden = fields[-1] == "golden"
        if golden:
            fields = fields[:-1]
        out.append((fields[0], " ".join(fields[1:]), golden))
    return out


def prepare(_):
    listed = {rel for rel, _, _ in cases()}
    sources = list((ROOT / "conformance" / "kernel").glob("*.ci")) + list((TESTS / "ptx").glob("*.ci"))
    missing = sorted({p.relative_to(ROOT).as_posix() for p in sources} - listed)
    if missing:
        raise ValueError("ptx_cases.txt does not list: %s" % ", ".join(missing))
    return cases()


def ptxas_tool():
    given = os.environ.get("CINT_PTXAS")
    if given:
        return given
    return shutil.which("ptxas")


def baseline(tool) -> str:
    """sm_52, the modules' `.target` (BX11-02), when ptxas generates code for it; else the lowest
    sm_<n> that its --gpu-name option lists."""
    try:
        text = subprocess.run([tool, "--help"], capture_output=True, timeout=120).stdout.decode("utf-8", "replace")
    except (OSError, subprocess.SubprocessError):
        return "sm_52"
    m = re.search(r"--gpu-name.*?Allowed values for this option:(.*?)Default value", text, re.S)
    listed = sorted({int(n) for n in re.findall(r"\bsm_(\d+)(?=['\",.\s])", m.group(1))}) if m else []
    above = [n for n in listed if n >= 52]
    return "sm_52" if not above or above[0] == 52 else "sm_%d" % above[0]


def targets(tool) -> list:
    out = [baseline(tool)]
    for target in os.environ.get("CINT_PTXAS_TARGETS", "").split():
        if target not in out:
            out.append(target)
    return out


def jit(tc, work: pathlib.Path, paths: list, cc) -> tuple:
    """The device step: (the device line, {path: the jit line and its log}), or a failure."""
    try:
        objs = tc.compile([TESTS / "ptx_jit.c", ROOT / "rt" / "cint_cuda.c"], work, "ptx jit")
        exe = tc.link_exe(objs, work / "ptx-jit", "ptx jit")
    except cc.BuildError as e:
        return "build: %s" % " ".join(str(e).split())[:600], None
    env = dict(tc.env)
    env["CUDA_CACHE_DISABLE"] = "1"
    r = subprocess.run([str(exe), *map(str, paths)], cwd=work, env=env, capture_output=True, timeout=600)
    lines = r.stdout.decode("utf-8", "replace").splitlines()
    if r.returncode == 2 or not lines or not lines[0].startswith("device "):
        return "no device: %s" % " ".join(lines)[:400], None
    got, current = {}, None
    for line in lines[1:]:
        named = [str(path) for path in paths if line.startswith("jit %s " % path)]
        if named:
            current = named[0]
            got[current] = line[len("jit %s " % current):]
        elif current is not None:
            got[current] += " | " + line
    return lines[0], got


def run(tc, tools, work: pathlib.Path, emitted: dict, prepared, cc) -> tuple:
    import table_capacity_suite  # the record ids of the tables, as table_capacity.ci reads them
    total = len(prepared)
    c_file = work / "ptx.c"
    code, err = cc.run_seed(tools["seed"], ROOT / "compiler", "tests/ptx.ci", c_file)
    if code != 0 or cc.sanitizer_reports(err):
        return 0, total, ["seed: exit %d: %s" % (code, " ".join(err.split())[:400])]
    text = c_file.read_bytes()
    digest = hashlib.sha256(text).hexdigest()
    if b"\r" in text:
        return 0, total, ["emitted C holds a CR byte"]
    if emitted.setdefault("tests/ptx.ci", digest) != digest:
        return 0, total, ["emitted C differs from the first configuration's"]
    try:
        assert table_capacity_suite.RECORDS == RECORDS
        ids = table_capacity_suite.records(text.decode("ascii"))
        objs = tc.compile([c_file, TESTS / "ptx_host.c"], work, "ptx")
        exe = tc.link_exe(objs + [tools["rt_obj"]], work / "ptx-host", "ptx")
    except (cc.BuildError, ValueError) as e:
        return 0, total, ["build: %s" % " ".join(str(e).split())[:600]]
    out_dir = work / "ptx-out"
    out_dir.mkdir(exist_ok=True)
    listing = work / "ptx.list"
    outputs = {rel: out_dir / (pathlib.Path(rel).stem + ".ptx") for rel, _, _ in prepared}
    for path in outputs.values():
        if path.exists():
            path.unlink()
    listing.write_bytes("".join("%s\t%s\n" % (ROOT / rel, outputs[rel]) for rel, _, _ in prepared)
                        .encode("utf-8"))
    env = dict(tc.env)
    env["ASAN_OPTIONS"] = "detect_leaks=1:abort_on_error=0"
    env["UBSAN_OPTIONS"] = "print_stacktrace=1:halt_on_error=1"
    r = subprocess.run([str(exe), *ids, str(listing)], cwd=work, env=env, capture_output=True)
    stderr = r.stderr.decode("utf-8", "replace")
    if r.returncode != 0 or cc.sanitizer_reports(stderr):
        return 0, total, ["host exit %d: %s %s" % (r.returncode, " ".join(stderr.split())[:400],
                                                    r.stdout.decode("utf-8", "replace")[-400:])]
    got, current = {}, None
    for line in r.stdout.decode("utf-8", "replace").splitlines():
        if line.startswith("file "):
            current = pathlib.Path(line[5:]).resolve().relative_to(ROOT).as_posix()
        elif current is not None:
            got[current] = line
    tool = ptxas_tool()
    assemble = targets(tool) if tool else []
    passed, failures = 0, []
    device, loaded = None, None
    if os.environ.get("CINT_PTX_DEVICE") == "1" and tc.opt == "2" and not tc.sanitize:
        device, loaded = jit(tc, work, [outputs[rel] for rel, want, _ in prepared
                                        if want == "ptx" and outputs[rel].exists()], cc)
        if loaded is None:
            return 0, total, ["ptx jit: %s" % device]
    for rel, want, golden in prepared:
        line = got.get(rel, "no output")
        fields = line.split()
        problem = None
        if want == "ptx":
            if not fields or fields[0] != "ptx":
                problem = "want ptx, got %s" % line
        elif want == "stopped":
            if not fields or fields[0] != "stopped":
                problem = "want a compile error, got %s" % line
        elif not line.startswith(want + " ") and line != want:
            problem = "want %s, got %s" % (want, line)
        if problem is None and want == "ptx":
            data = outputs[rel].read_bytes()
            key = "ptx:" + rel
            if b"\r" in data:
                problem = "PTX holds a CR byte"
            elif emitted.setdefault(key, hashlib.sha256(data).hexdigest()) != hashlib.sha256(data).hexdigest():
                problem = "PTX differs from the first configuration's"
            elif golden and not (GOLDEN / (pathlib.Path(rel).stem + ".ptx")).is_file():
                problem = "no golden/%s.ptx" % pathlib.Path(rel).stem
            elif golden and data != (GOLDEN / (pathlib.Path(rel).stem + ".ptx")).read_bytes():
                problem = "PTX differs from golden/%s.ptx" % pathlib.Path(rel).stem
            else:
                refused = []
                for target in assemble:
                    a = subprocess.run([tool, "--gpu-name", target, "-o", os.devnull, str(outputs[rel])],
                                       capture_output=True)
                    if a.returncode != 0 or a.stdout or a.stderr:
                        refused.append("ptxas %s: exit %d: %s" % (target, a.returncode, " ".join(
                            (a.stdout + a.stderr).decode("utf-8", "replace").split())[:300]))
                if refused:
                    problem = "; ".join(refused)
            if problem is None and loaded is not None:
                result = loaded.get(str(outputs[rel]), "no jit line")
                if not result.isdigit():
                    problem = "jit: %s" % result[:600]
        if problem is None:
            passed += 1
        else:
            failures.append("%s: %s" % (rel, problem))
    print("ptx: ptxas %s" % ("%s for %s" % (tool, " ".join(assemble)) if tool else "not found; PTX not assembled"))
    if device is not None:
        print("ptx: %s; %d modules loaded" % (device, sum(1 for v in loaded.values() if v.isdigit())))
    return passed, total, failures
