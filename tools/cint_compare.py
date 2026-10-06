"""Two conformance configurations compared with each other, record by record.

Box 14 default BX14-16 (docs/design/notes/2026-10-05-box14-workbench-debugging.md section
7.2); SPEC-06 12.2 (receipts of kind `diff`); SPEC-09 CONF-14.

Usage (from the repository root):

    python tools/cint_compare.py --ref SIDE --cand SIDE [--quick] [--only TEXT] [--jobs N]
                                 [--out DIR] [--cint PATH] [--receipt PATH | --no-receipt]
    python tools/cint_compare.py --receipts REF.json CAND.json [the options above]
    python tools/cint_compare.py --observe CONFIG --records FILE [--quick] [--only TEXT]
                                 [--jobs N] [--out DIR] [--cint PATH]

A configuration is `[seed:|b1:]<leg>-<opt>-<helpers>[-san]` in tools/cint_check.py's terms,
for example `b1:gcc-0-portable` or `b1:clang-2-builtin-san`; the compiler is B1 unless `seed:`
names the seed. A side (--ref, --cand) is a configuration, or a records file (`*.jsonl`) that
--observe wrote for one configuration, on this host or another: a configuration this host
cannot build (MSVC off Windows, Apple Clang off macOS, gcc and clang outside WSL on Windows) is
observed where it builds and compared here. --receipts takes two receipts of
tools/cint_check.py and reads each one's configuration and scope; where one names a first
disagreement, the run selects that program as --only would.

One run:

1. Inventory, with tools/cint_check.py's own functions, imported and never edited (its digest
   is a receipt identity input): the program cases with their frozen `.expect` files, the
   anchor programs and, unless --quick, the table programs, each case in one CONF-14 category.
2. Build each configuration as tools/cint_check.py builds it: `cint-seed`, the runtime object
   and `cint-harness`, and B1, bootstrapped once per leg or given by --cint PATH. B1's
   compilations are kept in <out>/emit-cache, so two configurations of one compiler compile
   each program once.
3. Per program, on each configuration: compile, classify (CONF-14), and run every case through
   `cint-harness --cases`, or run the program's entry, as tools/cint_check.py does; its
   determinism and `cint_ref check` passes are left to it, run on each configuration. A program
   the compiler accepts runs whatever its category, so two configurations that both disagree
   with the frozen file are still compared with each other.
4. Comparison: the two bodies of every record, line by line, and the CONF-14 category of every
   case. For each program with a difference, the run prints the records compared and the first
   differing record with both bodies (a fault's fields are body lines) and, for each
   configuration, whether its body is the expected one (the frozen `.expect` body, or the
   record cint_ref computes for an anchor or table); then a summary by CONF-14 category. Every
   difference goes to <out>/differences.jsonl, one canonical JSON object per line.
5. A receipt of schema `cint-receipt-1/diff` (SPEC-06 12.2) in the RCPT-02 form, by default
   <out>/receipt-diff.json: `identity` names both configurations, the suite digest, the scope,
   and the digests of this tool, tools/cint_check.py, the harness and the runtime; `outcome`
   holds the counts, the programs that differ, the first differing record, the categories, and
   the verdict; the host, the toolchains and the timings are `observations`.

Build outputs go under <build>/compare/<ref>--<cand>[-quick], never inside the repository,
where <build> is the CINT_BUILD environment variable or cint-build in the system temporary
directory.

Exit status: 0 when no record and no category differs and no sanitizer reports; 1 when one
does; 3 when the run is blocked (a configuration this host cannot build, a build failure,
records of another suite or scope). Python standard library only; no floating point.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import datetime
import json
import os
import pathlib
import platform
import re
import shutil
import sys
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import cint_check as cc  # noqa: E402

CONFIG_RE = re.compile(r"(?:(seed|b1):)?(msvc|gcc|clang|apple-clang)-([02])-(portable|builtin)(-san)?\Z")
RECORDS_SCHEMA = "cint-compare-records-1"
SIDES = ("reference", "candidate")
LISTED = 50   # programs and cases a receipt lists; differences.jsonl holds every one


class Blocked(Exception):
    pass


def parse_config(text: str) -> dict:
    m = CONFIG_RE.match(text)
    if not m:
        raise ValueError("%s is not a configuration: [seed:|b1:]<leg>-<0|2>-<portable|builtin>[-san]" % text)
    compiler, leg, opt, helpers, san = m.groups()
    if san and leg in ("msvc", "apple-clang"):
        raise ValueError("%s: sanitized builds are for the gcc and clang legs" % text)
    return {"compiler": compiler or "b1", "leg": leg, "opt": opt, "helpers": helpers, "sanitize": bool(san)}


def config_tag(c: dict) -> str:
    return "%s:%s-%s-%s%s" % (c["compiler"], c["leg"], c["opt"], c["helpers"], "-san" if c["sanitize"] else "")


def config_identity(c: dict) -> dict:
    """A configuration as the receipt names it (SPEC-06 12.2 `identity.backend`)."""
    return {"backend": "cpu-c17", "compiler": cc.COMPILERS[c["compiler"]], "helpers": c["helpers"],
            "leg": c["leg"], "opt": int(c["opt"]), "sanitize": c["sanitize"]}


def builds_here(c: dict):
    """None when this host builds the configuration, else why it does not."""
    if c["leg"] == "msvc" and os.name != "nt":
        return "the msvc leg builds on Windows"
    if c["leg"] == "apple-clang" and sys.platform != "darwin":
        return "the apple-clang leg builds on macOS"
    if c["leg"] in ("gcc", "clang") and os.name == "nt":
        return "the %s leg builds in WSL (%s)" % (c["leg"], cc.WSL_DISTRO)
    return None


def program_of(first: dict) -> str:
    """The --only text that selects the program of a receipt's first disagreement."""
    if first.get("call"):
        return first["call"].split(" ")[0].replace(".", "/")   # the module of the call
    case = first["case"]
    if case.startswith("anchors/"):
        return case   # an anchor's record id; an anchor without a program is selected by it
    case = case.split(":")[0]   # a table record is <program>:<n>
    head, _, last = case.rpartition("/")
    return head + "/" + last.split(".")[0]   # a program case's entry is <case>.<entry>


def from_receipt(path: pathlib.Path):
    """(configuration, scope, --only texts, suite digest) of a receipt of tools/cint_check.py."""
    r = json.loads(path.read_bytes().decode("ascii"))
    ident, obs = r["identity"], r["observations"]
    compiler = {v: k for k, v in cc.COMPILERS.items()}[ident["compiler"]]
    config = {"compiler": compiler, "leg": obs["leg"], "opt": str(obs["opt"]), "helpers": obs["helpers"],
              "sanitize": bool(obs["sanitize"])}
    first = r["outcome"]["agreement"].get("first_disagreement")
    return config, ident["scope"], [program_of(json.loads(first))] if first else [], ident["suite_sha256"]


def byte_order(names):
    return sorted(names, key=lambda n: n.encode("utf-8"))


def ascii_json(value) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True)


# --------------------------------------------------------------------------- one configuration

def inventory(compiler: str, quick: bool, only: list, gen_root: pathlib.Path) -> dict:
    """tools/cint_check.py's inventory for one compiler: its programs by name, the categories
    of the cases classified without compiling, its lists, and cint_ref against the frozen files."""
    lists = {"held": cc.gen_expect.read_held(), "compiler": cc.COMPILERS[compiler],
             "unsupported": cc.read_unsupported(cc.CONF / "unsupported.txt")}
    cats, problems = {}, []
    programs = cc.inventory_program_cases(only, cats, problems, lists)
    programs += cc.inventory_anchors(gen_root, only, cats, lists)
    if not quick:
        programs += cc.inventory_tables(only, cats, lists)
    return {"programs": {p.name: p for p in programs}, "cats": {k: list(v) for k, v in cats.items()},
            "lists": lists, "problems": problems, "expected": {}}


def observe(p, tc, tools: dict, lists: dict, work: pathlib.Path) -> dict:
    """One program on one configuration (module docstring, item 3): its CONF-14 category and the
    body observed for each record."""
    res = {"sanitizer": 0, "executions": 0, "exports": 0, "audited": False, "entry": False, "symbols": []}
    work.mkdir(parents=True, exist_ok=True)
    b1 = lists["compiler"] == cc.COMPILERS["b1"]
    if b1:   # every module in D-13 order through `cint emit-c`; the output set's C files
        rels = cc.module_order(p.root, p.rel)
        code, err, outset = cc.run_b1(tools["b1"]["cint"], p.root, rels, work / "b1" / "out",
                                      cache=tools["b1"]["cache"])
        got = cc.b1_diagnostic(err) if code != 0 else None
        c_file = sorted((outset[0] / rel for rel in outset[1] if rel.endswith(".c")),
                        key=lambda f: str(f).encode("utf-8")) if outset else None
    else:
        c_file = work / (pathlib.Path(p.rel).stem + ".c")
        code, err = cc.run_seed(tools["seed"], p.root, p.rel, c_file)
        got = cc.seed_diagnostic(err) if code != 0 else None
    res["sanitizer"] += cc.sanitizer_reports(err)
    frozen = p.frozen_body if p.kind == "program" else cc.COMPUTED
    category = cc.classify(p.name, p.subset, frozen, code, got, lists["unsupported"], lists["held"],
                           lists["compiler"], seed_only=p.seed_only)
    entries = [line.split(" ")[1] for line, _ in p.cases]
    if code != 0:
        bodies = [got] * len(p.cases)
    elif b1 and len(p.cases) == 1 and entries[0] == "-" and p.format == 1:
        bodies = cc.run_program_entry(p, tc, tools, work, c_file, res)
    elif "-" in entries:
        bodies = [["outcome no-exported-entry"]] * len(p.cases)
    else:
        bodies = cc.run_compiled(p, tc, tools, work, c_file, res)
    return {"category": list(category), "kind": p.kind, "program": p.name,
            "records": [[label, body] for (_, label), body in zip(p.cases, bodies)],
            "sanitizer": res["sanitizer"], "executions": res["executions"]}


class Local:
    """A configuration this run builds and observes."""

    def __init__(self, config: dict, inv: dict, out: pathlib.Path, cint, b1s: dict):
        self.config, self.inv, self.out = config, inv, out
        self.tc = cc.Toolchain(config["leg"], config["opt"], config["helpers"], config["sanitize"])
        self.tools = cc.build_tools(self.tc, out)
        # run_compiled audits each program's symbols against the runtime's, as cint_check does
        self.tools["rt_defined"] = set() if config["sanitize"] else self.tc.symbols(self.tools["rt_obj"])[0]
        if config["compiler"] == "b1":
            if config["leg"] not in b1s:   # one B1 per leg, its compilations kept for both sides
                b1 = cc.b1_tool(config["leg"], cint, out.parent / ("b1-boot-" + config["leg"]))
                b1["cache"] = cc.EmitCache(out.parent / "emit-cache", b1)
                b1s[config["leg"]] = b1
            self.tools["b1"] = b1s[config["leg"]]
        self.names = byte_order(inv["programs"])
        self.cats = inv["cats"]
        self.end = {"c0": self.tc.describe(), "executions": 0, "sanitizer_reports": 0}
        self.lock = threading.Lock()
        if config["compiler"] == "b1":
            self.end["b1"] = {"cint_sha256": self.tools["b1"]["sha256"], "how": self.tools["b1"]["how"]}

    def observe(self, name: str):
        p = self.inv["programs"].get(name)
        if p is None:
            return None
        o = observe(p, self.tc, self.tools, self.inv["lists"], self.out / "programs" / p.name)
        with self.lock:
            self.end["executions"] += o["executions"]
            self.end["sanitizer_reports"] += o["sanitizer"]
        return o


class Records:
    """A configuration observed by --observe, read from its records file."""

    def __init__(self, path: pathlib.Path):
        self.path = path
        self.file = path.open("rb")
        head = json.loads(self.file.readline())
        if head.get("schema") != RECORDS_SCHEMA:
            raise Blocked("%s is not a records file of %s" % (path, RECORDS_SCHEMA))
        self.head, self.config, self.names, self.cats = head, head["config"], head["programs"], head["categories"]
        self.end, self.pending = None, None

    def observe(self, name: str):
        """The observation of program `name`; the file holds them in byte order of name."""
        if name not in self.names:
            return None
        while True:
            if self.pending is None:
                self.pending = json.loads(self.file.readline())
            line = self.pending
            if "end" in line:
                raise Blocked("%s ends before program %s" % (self.path, name))
            if line["program"].encode("utf-8") > name.encode("utf-8"):
                raise Blocked("%s holds no observation of program %s" % (self.path, name))
            self.pending = None
            if line["program"] == name:
                return line

    def close(self):
        if self.end is None:
            rest = [json.loads(x) for x in self.file.read().splitlines() if x]
            ends = [x["end"] for x in rest + ([self.pending] if self.pending else []) if "end" in x]
            if not ends:
                raise Blocked("%s has no end line: the run that wrote it did not finish" % self.path)
            self.end = ends[0]
        self.file.close()


# --------------------------------------------------------------------------- the comparison

def compare_program(name: str, ref, cand) -> dict:
    """The records of one program on the two configurations (observe()), label by label."""
    a = {label: body for label, body in ref["records"]} if ref else {}
    b = {label: body for label, body in cand["records"]} if cand else {}
    labels = [label for label, _ in ref["records"]] if ref else []
    labels += [label for label, _ in cand["records"] if label not in a] if cand else []
    both = [label for label in labels if label in a and label in b]
    return {"program": name, "compared": len(both), "differ": [label for label in both if a[label] != b[label]],
            "only_reference": [label for label in labels if label not in b],
            "only_candidate": [label for label in labels if label not in a], "bodies": (a, b)}


def case_categories(cats: dict, observations) -> dict:
    """{case: [category, detail]}: tools/cint_check.py's case keys, an anchor by each record."""
    out = dict(cats)
    for o in observations:
        for label in ([label for label, _ in o["records"]] if o["kind"] == "anchor" else [o["program"]]):
            out[label] = o["category"]
    return out


def expected_verdict(inv, name: str, label: str, body):
    """Whether a body is the record expected of it, by the inventory of its compiler."""
    p = inv["programs"].get(name) if inv else None
    if p is None:
        return "not inventoried"
    if name not in inv["expected"]:
        inv["expected"][name] = p.expected() if callable(p.expected) else p.expected
    labels = [lab for _, lab in p.cases]
    if label not in labels:
        return "not inventoried"
    return "agrees" if inv["expected"][name][labels.index(label)] == body else "differs"


def indent(body, prefix: str) -> str:
    if body is None:
        return prefix + "(no record)\n"
    pad = " " * len(prefix)
    return "".join((prefix if k == 0 else pad) + line + "\n" for k, line in enumerate(body or ["(empty)"]))


def run_compare(sides, invs, out: pathlib.Path, jobs: int, write):
    """Observe and compare every program of both sides in byte order of name; returns the totals."""
    names = byte_order(set(sides[0].names) | set(sides[1].names))
    local = [isinstance(s, Local) for s in sides]
    totals = {"compared": 0, "differ": 0, "only_reference": 0, "only_candidate": 0, "programs": 0,
              "programs_differ": [], "first": None}
    observed = ([], [])
    diff_path = out / "differences.jsonl"

    def job(name):
        return tuple(s.observe(name) if is_local else None for s, is_local in zip(sides, local))

    with diff_path.open("w", encoding="ascii", newline="\n") as diffs, \
            concurrent.futures.ThreadPoolExecutor(max_workers=max(1, jobs)) as pool:
        for name, pair in zip(names, pool.map(job, names) if any(local) else ((None, None) for _ in names)):
            pair = tuple(o if is_local else s.observe(name) for o, s, is_local in zip(pair, sides, local))
            for k, o in enumerate(pair):
                if o is not None:   # only what classifies a case, not its bodies
                    observed[k].append({"category": o["category"], "kind": o["kind"], "program": o["program"],
                                        "records": [[label, None] for label, _ in o["records"]]})
            c = compare_program(name, *pair)
            totals["programs"] += 1
            totals["compared"] += c["compared"]
            totals["differ"] += len(c["differ"])
            totals["only_reference"] += len(c["only_reference"])
            totals["only_candidate"] += len(c["only_candidate"])
            if not (c["differ"] or c["only_reference"] or c["only_candidate"]):
                continue
            first = c["differ"][0] if c["differ"] else None
            totals["programs_differ"].append({"compared": c["compared"], "differ": len(c["differ"]),
                                              "first": first, "program": name,
                                              "only_candidate": len(c["only_candidate"]),
                                              "only_reference": len(c["only_reference"])})
            write("%s: %d records compared, %d differ%s\n" % (
                name, c["compared"], len(c["differ"]), "".join(
                    "; %d only on the %s" % (len(c["only_" + s]), s) for s in SIDES if c["only_" + s])))
            for label in c["differ"]:
                entry = {"case": label, "program": name}
                for side, inv, bodies in zip(SIDES, invs, c["bodies"]):
                    entry[side] = {"body": bodies[label], "expected": expected_verdict(inv, name, label, bodies[label])}
                diffs.write(ascii_json(entry) + "\n")
                if label == first:
                    write("  first differing record %s\n" % label)
                    for side in SIDES:
                        write(indent(entry[side]["body"], "    %-10s " % side))
                    write("    expected: the reference %s, the candidate %s (%s)\n" % (
                        entry["reference"]["expected"], entry["candidate"]["expected"],
                        "the frozen .expect body" if pair[0] and pair[0]["kind"] == "program" or
                        pair[1] and pair[1]["kind"] == "program" else "the record cint_ref computes"))
                    if totals["first"] is None:
                        totals["first"] = entry
            for side in SIDES:
                for label in c["only_" + side]:
                    diffs.write(ascii_json({"case": label, "only": side, "program": name}) + "\n")
    return totals, observed


def category_summary(cats: tuple) -> tuple:
    """Counts by CONF-14 category on each side, and the cases whose category differs."""
    counts = {k: {side: 0 for side in SIDES} for k in cc.CATEGORIES + ("disagreement",)}
    for side, side_cats in zip(SIDES, cats):
        for cat, _ in side_cats.values():
            counts[cat][side] += 1
    differ = []
    for case in byte_order(set(cats[0]) | set(cats[1])):
        a, b = cats[0].get(case), cats[1].get(case)
        if (a or [None])[0] != (b or [None])[0]:
            differ.append({"case": case, "candidate": b, "reference": a})
    return counts, differ


# --------------------------------------------------------------------------- the commands

def observe_command(ns, config: dict, out: pathlib.Path) -> int:
    """--observe: one configuration's observations, written to --records."""
    if builds_here(config):
        raise Blocked(builds_here(config))
    inv = inventory(config["compiler"], ns.quick, ns.only, out / "gen")
    side = Local(config, inv, out / "build", ns.cint, {})
    records = pathlib.Path(ns.records)
    records.parent.mkdir(parents=True, exist_ok=True)
    suite, _ = cc.tree_digest("conformance/", "conformance/")
    head = {"categories": inv["cats"], "config": config, "only": sorted(ns.only), "programs": side.names,
            "schema": RECORDS_SCHEMA, "scope": "quick" if ns.quick else "full", "suite_sha256": suite,
            "tool": tool_identity([config])}
    with records.open("w", encoding="ascii", newline="\n") as f, \
            concurrent.futures.ThreadPoolExecutor(max_workers=max(1, ns.jobs)) as pool:
        f.write(ascii_json(head) + "\n")
        for o in pool.map(side.observe, side.names):
            f.write(ascii_json(o) + "\n")
        f.write(ascii_json({"end": side.end}) + "\n")
    print("%s: %d programs observed, %d executions, %d sanitizer reports; records: %s" % (
        config_tag(config), len(side.names), side.end["executions"], side.end["sanitizer_reports"], records))
    return 0


def tool_identity(configs) -> dict:
    """The digests of what decides the records (the receipt's `identity.tool`)."""
    tool = {"check_sha256": cc.files_digest([ROOT / "tools" / "cint_check.py"]),
            "compare_sha256": cc.files_digest([pathlib.Path(__file__).resolve()]),
            "harness_sha256": cc.files_digest([cc.HARNESS / "cint_harness.c"]),
            "runtime_library_sha256": cc.files_digest([cc.RT / "cint_rt.c", cc.RT / "cint_rt_internal.h",
                                                     cc.RT / "cint_mem.c", cc.RT / "cint_mem.h"])}
    if any(c["compiler"] == "b1" for c in configs):
        tool["compiler_source_identity"] = cc.b1_source_identity()
    if any(c["compiler"] == "seed" for c in configs):
        tool["seed_sha256"] = cc.files_digest(sorted(cc.SEED.glob("*.c")) + sorted(cc.SEED.glob("*.h")))
    return tool


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--ref", help="the reference: a configuration, or a records file of --observe")
    ap.add_argument("--cand", help="the candidate: a configuration, or a records file of --observe")
    ap.add_argument("--receipts", nargs=2, metavar=("REF", "CAND"),
                    help="two receipts of tools/cint_check.py: their configurations and scope")
    ap.add_argument("--observe", metavar="CONFIG", help="observe one configuration into --records")
    ap.add_argument("--records", metavar="FILE", help="with --observe: the records file to write")
    ap.add_argument("--quick", action="store_true", help="anchors and program cases only")
    ap.add_argument("--only", action="append", default=[], help="programs whose name contains TEXT")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--out", help="build directory (default <build>/compare/<ref>--<cand>)")
    ap.add_argument("--cint", metavar="PATH", help="the B1 cint executable (default: bootstrap one per leg)")
    ap.add_argument("--receipt", help="receipt path (default <out>/receipt-diff.json)")
    ap.add_argument("--no-receipt", action="store_true")
    ns = ap.parse_args(argv)
    started = time.monotonic_ns()
    try:
        if ns.observe:
            if ns.ref or ns.cand or ns.receipts or not ns.records:
                ap.error("--observe takes --records FILE, without --ref, --cand or --receipts")
            config = parse_config(ns.observe)
        elif ns.receipts:
            if ns.ref or ns.cand:
                ap.error("--receipts replaces --ref and --cand")
        elif not (ns.ref and ns.cand):
            ap.error("give --ref and --cand, --receipts, or --observe")
    except ValueError as e:
        ap.error(str(e))
    scope_given = ns.quick
    specs, notes = [], []
    if ns.receipts:
        suite, _ = cc.tree_digest("conformance/", "conformance/")
        scopes = set()
        for path in ns.receipts:
            config, scope, only, receipt_suite = from_receipt(pathlib.Path(path))
            specs.append(config)
            scopes.add(scope)
            ns.only += [o for o in only if o not in ns.only]
            if receipt_suite != suite:
                notes.append("%s was made on suite %s; this tree's suite is %s" % (path, receipt_suite, suite))
        ns.quick = ns.quick or "quick" in scopes
    elif not ns.observe:
        for text in (ns.ref, ns.cand):
            try:
                specs.append(pathlib.Path(text) if text.endswith(".jsonl") else parse_config(text))
            except ValueError as e:
                ap.error(str(e))
    tags = [config_tag(config)] if ns.observe else [
        s.stem if isinstance(s, pathlib.Path) else config_tag(s) for s in specs]
    out = pathlib.Path(ns.out) if ns.out else cc.build_root() / "compare" / (
        "--".join(t.replace(":", "-") for t in tags) + ("-quick" if ns.quick else ""))
    if out.resolve().is_relative_to(ROOT.resolve()):
        ap.error("--out must be outside the repository: %s" % out)
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    try:
        if ns.observe:
            return observe_command(ns, config, out)
        return compare_command(ns, specs, out, notes, scope_given, started)
    except (Blocked, cc.BuildError, OSError, ValueError) as e:
        print("BLOCKED: %s" % e)
        return 3


def compare_command(ns, specs, out: pathlib.Path, notes: list, scope_given: bool, started: int) -> int:
    suite, suite_files = cc.tree_digest("conformance/", "conformance/")
    sides, invs, b1s = [None, None], [None, None], {}
    files = [Records(s) if isinstance(s, pathlib.Path) else None for s in specs]
    for f in files:   # a records file decides the scope and selection of the whole run
        if f is None:
            continue
        if f.head["suite_sha256"] != suite:
            raise Blocked("%s was observed on suite %s; this tree's suite is %s" % (f.path, f.head["suite_sha256"], suite))
        quick = f.head["scope"] == "quick"
        if (scope_given and not quick) or (ns.only and sorted(ns.only) != f.head["only"]):
            raise Blocked("%s was observed with scope %s and --only %s" % (f.path, f.head["scope"], f.head["only"]))
        ns.quick, ns.only = quick, list(f.head["only"])
    if all(f is not None for f in files) and (files[0].head["scope"], files[0].head["only"]) != (
            files[1].head["scope"], files[1].head["only"]):
        raise Blocked("the two records files differ in scope or selection")
    configs = [f.config if f else s for f, s in zip(files, specs)]
    for f in files:
        if f is not None and f.head["tool"] != tool_identity([f.config]):
            raise Blocked("%s was observed with other tools, runtime or compiler sources than this tree's"
                          % f.path)
    for f, c in zip(files, configs):
        if f is None and builds_here(c):
            raise Blocked("%s: %s; observe it there with --observe and give its records file" % (
                config_tag(c), builds_here(c)))
    if not ns.quick:
        avail = cc.available_memory()
        if avail < cc.MIN_AVAILABLE_BYTES:
            raise Blocked("%d MiB available; the full scope needs 8 GiB" % (avail >> 20))
    by_compiler = {}
    for c in configs:   # one inventory per compiler, for the runs and the expected bodies
        if c["compiler"] not in by_compiler:
            by_compiler[c["compiler"]] = inventory(c["compiler"], ns.quick, ns.only, out / ("gen-" + c["compiler"]))
    for k, (f, c) in enumerate(zip(files, configs)):
        invs[k] = by_compiler[c["compiler"]]
        if f is not None:
            sides[k] = f
            continue
        sides[k] = Local(c, invs[k], out / SIDES[k], ns.cint, b1s)
    tags = [config_tag(c) for c in configs]
    print("compare: reference %s, candidate %s; %s scope%s; suite %s" % (
        tags[0], tags[1], "quick" if ns.quick else "full",
        "; --only " + " ".join(ns.only) if ns.only else "", suite), flush=True)
    for line in notes:
        print("note: " + line)
    for compiler, inv in sorted(by_compiler.items()):
        print("inventory %s: %d programs, %d records, %d cases classified without compiling; cint_ref "
              "against the frozen .expect files: %d differ" % (
                  compiler, len(inv["programs"]), sum(len(p.cases) for p in inv["programs"].values()),
                  len(inv["cats"]), len(inv["problems"])), flush=True)

    def write(text):
        sys.stdout.write(text)
        sys.stdout.flush()

    totals, observed = run_compare(sides, invs, out, ns.jobs, write)
    for f in files:
        if f is not None:
            f.close()
    cats = tuple(case_categories(s.cats, obs) for s, obs in zip(sides, observed))
    counts, cat_differ = category_summary(cats)
    with (out / "differences.jsonl").open("a", encoding="ascii", newline="\n") as diffs:
        for d in cat_differ:
            diffs.write(ascii_json({"case": d["case"], "category": {"candidate": d["candidate"],
                                                                      "reference": d["reference"]}}) + "\n")
    ends = [s.end for s in sides]
    sanitizer = {side: e["sanitizer_reports"] for side, e in zip(SIDES, ends)}
    passed = not (totals["differ"] or totals["only_reference"] or totals["only_candidate"] or cat_differ
                  or any(sanitizer.values()))
    print("categories (CONF-14), reference and candidate: %s; %d cases differ in category" % (
        "; ".join("%s %d and %d" % (k, v["reference"], v["candidate"]) for k, v in counts.items()
                  if v["reference"] or v["candidate"]), len(cat_differ)))
    for d in cat_differ[:20]:
        print("  %s: reference %s, candidate %s" % (d["case"], (d["reference"] or ["absent"])[0],
                                                     (d["candidate"] or ["absent"])[0]))
    print("records: %d compared on both, %d differ, in %d of %d programs; %d only on the reference, %d only "
          "on the candidate; sanitizer reports %d and %d" % (
              totals["compared"], totals["differ"], len(totals["programs_differ"]), totals["programs"],
              totals["only_reference"], totals["only_candidate"], sanitizer["reference"], sanitizer["candidate"]))
    identity = {
        "backend": {side: config_identity(c) for side, c in zip(SIDES, configs)},
        "kind": "diff",
        "only": sorted(ns.only),
        "profile": "cint-core-1",
        "schema": "cint-receipt-1/diff",
        "scope": "quick" if ns.quick else "full",
        "suite": {"files": suite_files, "method": cc.TREE_METHOD % ("conformance/", "conformance/"),
                  "sha256": suite},
        "tool": tool_identity(configs),
    }
    outcome = {
        "categories": counts,
        "category_differences": {"count": len(cat_differ), "first": [ascii_json(d) for d in cat_differ[:LISTED]]},
        "first_difference": ascii_json(totals["first"]) if totals["first"] else None,
        "programs": {"compared": totals["programs"], "differ": len(totals["programs_differ"]),
                     "first": totals["programs_differ"][:LISTED]},
        "records": {k: totals[k] for k in ("compared", "differ", "only_candidate", "only_reference")},
        "sanitizer_reports": sanitizer,
        "verdict": "pass" if passed else "fail",
    }
    for p in outcome["programs"]["first"]:
        if p["first"] is None:
            del p["first"]
    elapsed_ms = (time.monotonic_ns() - started) // 1_000_000
    observations = {
        "date_utc": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d"),
        "elapsed_s": elapsed_ms // 1000,
        "host": {"arch": platform.machine().lower().replace("amd64", "x86-64").replace("x86_64", "x86-64"),
                 "os": cc.host_os()},
        "python": platform.python_version(),
        "sides": {side: e for side, e in zip(SIDES, ends)},
        **({"emit_cache": {leg: dict(b["cache"].counts) for leg, b in sorted(b1s.items())}} if b1s else {}),
        "limits": "Records are compared with each other, not with cint_ref; tools/cint_check.py runs that "
                  "comparison, the determinism check and the symbol audit on each configuration.",
    }
    observations["receipt_identity_sha256"] = cc.sha256(cc.canonical({"identity": identity, "outcome": outcome}))
    if not ns.no_receipt:
        path = pathlib.Path(ns.receipt) if ns.receipt else out / "receipt-diff.json"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(cc.canonical({"identity": identity, "observations": observations, "outcome": outcome}) + b"\n")
        print("receipt: %s" % path)
    print("differences: %s" % (out / "differences.jsonl"))
    print("%s: %s" % ("pass" if passed else "fail", "the two configurations agree record by record"
                      if passed else "the configurations differ"))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
