"""The parse part of the `front` suite: compiler/scan.ci and compiler/parse.ci (slice 2 task 2.10a).

The scanner and parser take their tables as views, which cint-harness cannot pass, so this
part builds compiler/tests/front_host.c with the C that cint-seed emits for
compiler/tests/front.ci and runs it over a list of sources in one process per
configuration. Each source is one case. A case passes when cintc's outcome is `cint_ref`'s
on the surface of boxes 09 and 12 (front_ref.py):

  - `cint_ref` parses it: cintc parses it and its tree text is the same, byte for byte;
  - `cint_ref` reports a diagnostic: cintc's first diagnostic (SPEC-04 LS-282 order) has
    the same code, line and column; for a file with an error on several lines
    (front_cases.multi), cintc reports each, as `cint_ref` reports each line alone;
  - `cint_ref` refuses it (outside its surface): cintc parses it, and for the frozen cases
    its tree text is the reviewed one in front_refused.txt; a written case may instead
    name the outcome SPEC-04 18 gives (front_cases.EXPECT), as may the few written cases
    where `cint_ref` reports an error for text SPEC-04 18 admits (compiler/OPEN.md
    CINTC-OQ-06). A held case that cintc does not parse yet gives the diagnostic that
    front_pending.txt lists (CINTC-OQ-06 item 5). Mutants that `cint_ref` refuses
    are not cases.

The sources are every `.ci` file under conformance/ and compiler/, the written cases of
front_cases.py, the `else if` and switch chains of 1 to 5,000 arms, and seeded mutants of
the frozen files. Four more cases check that the parse stack's high-water mark of each
chain family is the same for every arm count (constant depth, SPEC-09 CINTC-03). A fault
inside the compiler (an internal error, CINTC-10) or a sanitizer report fails the case or
the configuration.
"""
import hashlib
import pathlib
import re
import subprocess

import front_cases
import front_ref

ROOT = pathlib.Path(__file__).resolve().parents[2]
TESTS = ROOT / "compiler" / "tests"
REQUIRES = ["compiler/scan.ci", "compiler/parse.ci", "compiler/tests/front.ci", "compiler/tests/front_host.c"]
MUTANTS_PER_FILE = 8
MUTANT_SEED = 2010
RECORDS = ("scan_5_Token", "parse_4_Node", "parse_5_Frame", "limits_12_CompilerDiag")


def refused_trees() -> dict:
    out = {}
    for line in (TESTS / "front_refused.txt").read_text(encoding="ascii").splitlines():
        if line and not line.startswith("#"):
            name, tree = line.split("\t", 1)
            out[name] = tree
    return out


def pending_diagnostics() -> dict:
    out = {}
    for line in (TESTS / "front_pending.txt").read_text(encoding="ascii").splitlines():
        if line and not line.startswith("#"):
            name, diag = line.split("\t", 1)
            code, row, col = diag.split()
            out[name] = ("error", int(code.lstrip("C")), int(row), int(col))
    return out


def prepare(gen: pathlib.Path) -> list:
    """The sources, written under gen, with their expected outcomes, computed once."""
    gen.mkdir(parents=True, exist_ok=True)
    golden = refused_trees()
    pending = pending_diagnostics()
    sources = []

    def add(name, data, expect):
        path = gen / (hashlib.sha256(name.encode("utf-8")).hexdigest()[:20] + ".ci")
        if not path.exists() or path.read_bytes() != data:
            path.write_bytes(data)
        sources.append((name, path, expect))

    for name, data in front_cases.frozen():
        want = front_ref.outcome(data, name.split("/", 1)[1] if name.startswith("conformance/") else name)
        if want[0] == "refused":
            want = pending.pop(name, None) or (("tree", golden[name]) if name in golden else ("accept",))
        elif name in pending:
            raise ValueError("front_pending.txt: cint_ref decides %s" % name)
        add(name, data, want)
    if pending:
        raise ValueError("front_pending.txt: no such file: %s" % ", ".join(sorted(pending)))
    for name, data, override in front_cases.written():
        want = override or front_ref.outcome(data, "case.ci")
        if want[0] == "refused":
            want = ("accept",)
        add(name, data, want)
    for name, data in front_cases.chains():
        add(name, data, front_ref.outcome(data, "case.ci"))
    for name, data in front_cases.mutants(MUTANTS_PER_FILE, MUTANT_SEED):
        want = front_ref.outcome(data, "case.ci")
        if want[0] != "refused":
            add(name, data, want)
    return sources


def records(c_text: str) -> list:
    """The record ids the seed gave Token, Node, Frame and CompilerDiag (its cg_records order)."""
    m = re.search(r"cg_records\[\d+\] = \{\n(.*?)\n\};", c_text, re.S)
    names = re.findall(r"sizeof\((ci_\w+)\)\)", m.group(1)) if m else []
    ids = []
    for key in RECORDS:
        found = [i for i, nm in enumerate(names) if nm.endswith("_" + key) or nm == "ci_" + key]
        if len(found) != 1:
            raise ValueError("record %s not found in the emitted C" % key)
        ids.append(str(found[0]))
    return ids


def parse_output(stdout: bytes) -> dict:
    blocks, current = {}, None
    for line in stdout.decode("utf-8", "replace").split("\n"):
        if line.startswith("file "):
            current = blocks.setdefault(line[5:], [])
        elif line and current is not None:
            current.append(line)
    return blocks


def judge(expect, got) -> str:
    """'' when the host's lines `got` meet `expect`; otherwise what differs."""
    if got is None:
        return "no output"
    status = got[0] if got else ""
    faults = [x for x in got if x.startswith("fault")]
    if faults:
        return "internal error: " + faults[0]
    tree = next((x[5:] for x in got if x.startswith("tree ")), None)
    diags = [x.split()[1:] for x in got if x.startswith("diag ")]
    if expect[0] in ("tree", "accept"):
        if status != "status 0 0":
            return "want a tree, got %s %s" % (status, diags[:1])
        if expect[0] == "tree" and tree != expect[1]:
            a, b = tree or "", expect[1]
            i = next((k for k in range(min(len(a), len(b))) if a[k] != b[k]), min(len(a), len(b)))
            return "tree differs at %d: got ...%s... want ...%s..." % (i, a[max(0, i - 60):i + 40],
                                                                      b[max(0, i - 60):i + 40])
        return ""
    if expect[0] == "errors":
        want_rows = [[str(x) for x in row] for row in expect[1]]
        if status != "status 0 1" or diags != want_rows:
            return "want diagnostics %s, got %s %s" % (want_rows, status, diags)
        return ""
    want = [str(expect[1]), str(expect[2]), str(expect[3])]
    if status != "status 0 1" or not diags:
        return "want C%s at %s:%s, got %s" % (want[0], want[1], want[2], status)
    if diags[0] != want:
        return "want C%s at %s:%s, got C%s at %s:%s" % tuple(want + diags[0])
    return ""


def run(tc, tools, work: pathlib.Path, emitted: dict, sources: list, cc) -> tuple:
    """Builds the host for configuration tc and runs every source. (passed, total, failures)."""
    total = len(sources) + 4
    c_file = work / "front.c"
    code, err = cc.run_seed(tools["seed"], ROOT / "compiler", "tests/front.ci", c_file)
    if code != 0 or cc.sanitizer_reports(err):
        return 0, total, ["seed: exit %d: %s" % (code, " ".join(err.split())[:400])]
    text = c_file.read_bytes()
    digest = hashlib.sha256(text).hexdigest()
    if b"\r" in text:
        return 0, total, ["emitted C holds a CR byte"]
    if emitted.setdefault("tests/front.ci", digest) != digest:
        return 0, total, ["emitted C differs from the first configuration's"]
    try:
        ids = records(text.decode("ascii"))
        objs = tc.compile([c_file, TESTS / "front_host.c"], work, "front")
        exe = tc.link_exe(objs + [tools["rt_obj"]], work / "front-host", "front")
    except (cc.BuildError, ValueError) as e:
        return 0, total, ["build: %s" % " ".join(str(e).split())[:600]]
    listing = work / "front.list"
    listing.write_bytes("".join(str(p) + "\n" for _, p, _ in sources).encode("utf-8"))
    env = dict(tc.env)
    env["ASAN_OPTIONS"] = "detect_leaks=1:abort_on_error=0"
    env["UBSAN_OPTIONS"] = "print_stacktrace=1:halt_on_error=1"
    r = subprocess.run([str(exe), *ids, str(listing)], cwd=work, env=env, capture_output=True)
    stderr = r.stderr.decode("utf-8", "replace")
    if r.returncode != 0 or cc.sanitizer_reports(stderr):
        return 0, total, ["host exit %d: %s" % (r.returncode, " ".join(stderr.split())[:400])]
    blocks = parse_output(r.stdout)
    passed, failures, depth = 0, [], {}
    for name, path, expect in sources:
        got = blocks.get(str(path))
        problem = judge(expect, got)
        if problem:
            failures.append("%s: %s" % (name, problem))
        else:
            passed += 1
        if name.startswith("chain/") and got:
            stats = next((x.split()[1:] for x in got if x.startswith("stats ")), None)
            family = name.rsplit("_", 1)[0]
            depth.setdefault(family, set()).add(stats[4] if stats else "?")
    for family in ("chain/if_realistic", "chain/if_minimal", "chain/switch_realistic", "chain/switch_minimal"):
        seen = depth.get(family, set())
        if len(seen) == 1 and "?" not in seen:
            passed += 1
        else:
            failures.append("%s: parse stack high-water marks %s differ across %s arms" %
                            (family, sorted(seen), front_cases.CHAIN_ARMS))
    return passed, total, failures
