"""Stage acceptance: run a roadmap stage's conformance matrix and require one receipt identity.

SPEC-09 CONF-15 and STAGE-01 (slice 2 decision patch D-14; decisions 23, 24, and 25).
Usage (from the repository root):

    python tools/cint_accept.py --stage T1 --required conformance/required/T1.txt
                                --out results/cint/slice2/t1 [--legs LEGS] [--jobs N]
    python tools/cint_accept.py --stage T1|T2 --dry-run --receipts DIR [--required FILE]
                                [--legs LEGS] [--out DIR]
    python tools/cint_accept.py --set t27-1 --out results/cint/t27/set1-<tree7> [--legs LEGS]
    python tools/cint_accept.py --set t27-1 --dry-run --receipts DIR [--out DIR]
    python tools/cint_accept.py --amendment e8fbc37 --receipts results/cint/slice2/t1-mac-e8fbc37
                                --original results/cint/t1

The T1 matrix has 24 configurations, one `tools/cint_check.py` run each at full scope with
`--required` and `--verify-tables`: MSVC `/Od` and `/O2` in both helper modes on Windows; GCC
and Clang at `-O0` and `-O2` in both helper modes, with and without ASan and UBSan, on Linux
(WSL); Apple Clang (leg `apple-clang`) at `-O0` and `-O2` in both helper modes, unsanitized, on
macOS AArch64 (CONF-09, plan task 2.2a). T2 has the same 24 per compiler: 48 for the seed and
B1. A host runs only its own legs, so a run defaults to the 20 Windows and Linux
configurations and the Mac runs `--legs apple-clang`. Each run writes
`<out>/receipt-<leg>-<opt>-<helpers>[-san].json` (`receipt-<compiler>-...` off the T1 seed
path; schema `cint-conformance-receipt-2`) and refuses to overwrite a receipt or
`acceptance.json`. The runs share one heavy-run slot, so they run one after another. The B1
runs of one acceptance run share one `--emit-cache` directory, removed at its end, so B1
compiles each program once per leg.

Every result is validated from the saved receipts, not from a run's exit status: the receipt
form and identity digest, the current source hashes, the configuration with its recorded C0 name
and flags (on Apple Clang also the macOS AArch64 host, the Apple clang version, and the SDK),
required-case coverage, agreement, determinism, the symbol audit, the table check, and for B1
the comparison of type signatures with cint_ref (SPEC-09 CONF-16). A run or dry run over a
subset of the legs, over one of the two T2 compilers, or without measurements, is provisional:
its last line says "(provisional)" and its summary has "complete": false, whatever its result.
T1 is the matrix of one compiler, the seed or (with --compiler b1) B1. A stage passes only in
the import step, `--dry-run --receipts DIR` with the default `--legs` (all four)
over the complete set: 24 receipts for T1 and 48 for T2, fresh from one source revision, with
one receipt identity per compiler. Receipts of different revisions are never one identity, so
old Windows and Linux receipts with newer Mac receipts fail the import.

The T2 path checks saved conformance, cint-fuzz-1, and K and phase-fuel evidence, and accepts
--compiler seed|b1. A B1 receipt is bound to the current tree by its compiler source identity
and CLI digest (check_b1_binding). The K and phase-fuel evidence is one
`measure-<leg>.json` per selected leg (tools/cint_measure.py, task 2.17; check_measurement):
bound to the current compiler, runtime, corpus, and measurement tool, showing every measured
compiler phase call within the bridge's frozen budget (CINTC-09) and K within the T2 ceiling
(CINTC-02), and one identity_sha256 across the legs; acceptance.json records k, c, and K.
--skip-measurements omits K and phase fuel only; fuzz evidence is still
required. Fuzz evidence is read from fuzz*.json, measurements from measure-*.json. Dry runs
leave the input directory unchanged; --out explicitly requests a fresh acceptance.json elsewhere.

`--set NAME` accepts a conformance set (`cint_check.SETS`, t27 scoping note 7.2) instead of a
stage: the same 24 configurations on B1, each `tools/cint_check.py --set NAME` against the set's
list (default `conformance/required/<NAME>.txt`), without `--verify-tables`, fuzz, or K and
phase-fuel evidence, which belong to the stages. Each receipt must record the set, and a stage
acceptance refuses a set receipt. The summary and the last line name the set.

`--amendment REV` is the historical qualification comparison of decisions 24 and 25, not stage
acceptance. It checks that the receipts in `--receipts` cover the amendment's configurations,
pass, and share one receipt identity, and that they differ from the original receipts in
`--original` (one receipt identity) only in the reviewed identity fields, with equal outcomes.
Its last line names it: `qualification amendment <REV> (...; not stage acceptance): ...`.

The tool writes `<out>/acceptance.json` (RFC 8785, then one LF) and prints, last,
`acceptance <stage>[ (provisional)]: pass|fail, <n> receipts, <m> receipt identit(y|ies)`.
Exit status 0 on a pass, complete or provisional (the label and "complete" tell them apart), 1 on
a fail, 3 when blocked. Python standard library only.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import cint_receipts  # noqa: E402

LEGS = ("msvc", "gcc", "clang", "apple-clang")
RUN_LEGS = ("msvc", "gcc", "clang")   # a run's default: apple-clang runs on macOS, by --legs
STAGE_COMPILERS = {"T1": ("seed",), "T2": ("seed", "b1")}
SET_COMPILERS = ("b1",)   # a set's cases print, which the seed's cint-boot-1 refuses (SEED-14)
# Historical qualification amendments (decisions 24 and 25): the configurations re-run and the
# identity fields that the reviewed differences may change. Everything else must be equal.
AMENDMENTS = {
    "e8fbc37": {
        "decisions": "24 and 25",
        "configurations": [("clang", "0", "portable", False), ("clang", "0", "builtin", False),
                           ("clang", "2", "portable", False), ("clang", "2", "builtin", False)],
        "host_os": "Darwin",
        "fields": ("bridge_sha256", "tool_sha256"),   # the bridge define; the memory probe
    },
}


def read_evidence(path):
    """Read ASCII JSON without duplicate keys or non-I-JSON integers."""
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError("duplicate key: " + key)
            result[key] = value
        return result

    def integer(text):
        value = int(text)
        if abs(value) > 2 ** 53 - 1:
            raise ValueError("integer outside the I-JSON range")
        return value

    def invalid_number(text):
        raise ValueError("non-integer number: " + text)

    data = path.read_bytes()
    value = json.loads(data.decode("ascii"), object_pairs_hook=pairs, parse_int=integer,
                       parse_float=invalid_number, parse_constant=invalid_number)
    if not isinstance(value, dict):
        raise ValueError("receipt must be an object")
    # Rejects escaped non-ASCII strings as well as non-ASCII file bytes.
    if any(ord(char) > 127 for char in json.dumps(value, ensure_ascii=False)):
        raise ValueError("receipt strings must be ASCII")
    return value, data


def current_identity(compiler="seed"):
    """Current source fields using the existing conformance producer's digest rules, for the
    receipts of one compiler: a B1 receipt also names the type signature encoding (CONF-16)."""
    import cint_check
    suite, count = cint_check.tree_digest("conformance/", "conformance/")
    reference, ref_count = cint_check.tree_digest("ref/cint_ref/", "ref/")
    digest = cint_check.files_digest
    return {
        "suite_sha256": suite, "suite_files": count,
        "cint_ref_tree_sha256": reference, "cint_ref_tree_files": ref_count,
        "seed_sha256": digest(sorted((ROOT / "seed").glob("*.c")) +
                              sorted((ROOT / "seed").glob("*.h"))),
        "bridge_sha256": digest([ROOT / "rt" / name for name in
                                 ("cint_bridge.c", "cint_bridge.h", "cint_bridge_internal.h",
                                  "cint_build.c")]),
        "harness_sha256": digest([ROOT / "harness/cint_harness.c"]),
        "runtime_header_sha256": hashlib.sha256((ROOT / "rt/cint_rt.h").read_bytes()).hexdigest(),
        "runtime_library_sha256": digest([ROOT / "rt/cint_rt.c", ROOT / "rt/cint_rt_internal.h",
                                           ROOT / "rt/cint_mem.c", ROOT / "rt/cint_mem.h"]),
        "runtime_contract_version": cint_check.runtime_contract(),
        "encodings": cint_check.encodings(cint_check.COMPILERS[compiler]),
        "tool_sha256": digest([ROOT / "tools/cint_check.py"]),
    }


def b1_binding_inputs(root=ROOT):
    """Read the current bootstrap inputs without building or writing a snapshot.

    Retains tools/cint_bootstrap.py's field names and CISRC001 framing. The
    compiler manifest covers main.ci's import closure, not unrelated .ci files.
    These are expected source inputs, never proof of a qualified B1 executable.
    """
    import cint_bootstrap
    groups = {}
    for group, directory, suffixes in (("compiler", "compiler", (".ci",)),
                                       ("seed", "seed", (".c", ".h")),
                                       ("runtime", "rt", (".c", ".h")),
                                       ("cli", "cli", (".c", ".h"))):
        source_dir = root / directory
        if source_dir.is_symlink():
            raise ValueError("linked input directory: " + str(source_dir))
        files = {}
        for path in sorted(source_dir.iterdir(), key=lambda p: p.name.encode("utf-8")):
            if path.suffix not in suffixes:
                continue
            if path.is_symlink() or not path.is_file():
                raise ValueError("not a regular build input: " + str(path))
            files[path.relative_to(root).as_posix()] = path.read_bytes()
        groups[group] = files
    groups["compiler"] = cint_bootstrap.compiler_closure(groups["compiler"])
    host = root / "compiler/tests/golden_host.c"
    if host.parent.is_symlink() or host.is_symlink() or not host.is_file():
        raise ValueError("not a regular B1 host input: " + str(host))
    groups["host"] = {"compiler/tests/golden_host.c": host.read_bytes()}

    def records(group):
        return [{"path": path, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
                for path, data in groups[group].items()]

    return {
        "compiler_source_identity": hashlib.sha256(
            cint_bootstrap.source_manifest(groups["compiler"])).hexdigest(),
        "compiler_sources": records("compiler"),
        "seed_source_identity": hashlib.sha256(
            cint_bootstrap.source_manifest(groups["seed"])).hexdigest(),
        "runtime_sources": records("runtime"),
        "build_inputs": {group: records(group) for group in ("cli", "host", "seed")},
    }


B1_SOURCE_METHOD = ("sha256 of the CISRC001 source manifest of the import closure of "
                    "compiler/main.ci (tools/cint_bootstrap.py)")


def records_digest(records) -> str:
    """tools/cint_check.py files_digest over source records: sha256 of the lines
    '<sha256>  <path>' and a line feed, sorted by path bytes."""
    lines = sorted(((r["path"], r["sha256"]) for r in records),
                   key=lambda item: item[0].encode("utf-8"))
    return hashlib.sha256("".join("%s  %s\n" % (digest, path) for path, digest in lines)
                          .encode("utf-8")).hexdigest()


def check_b1_binding(receipt, expected_sources, expected_provenance=None):
    """The problems of a B1 receipt's compiler binding (empty when it holds).

    A B1 receipt (schema cint-conformance-receipt-2, compiler cintc) names the compiler it ran by
    identity.compiler_source_identity, the sha256 of the CISRC001 manifest of compiler/main.ci's
    import closure that tools/cint_check.py checks against the executable's toolchain file before
    it runs (b1_tool). Requires that identity to equal the one b1_binding_inputs() computes
    from the current tree, the documented method and emitter, the CLI digest over the current
    cli/*.c and cli/*.h, and a recorded executable digest. expected_provenance, when given, is a
    retained cint-bootstrap-receipt-1 object; its compiler source identity must agree too. The
    other source fields (seed, runtime, bridge, harness, suite, cint_ref, tool) are compared with
    the current tree by check_conformance. A receipt is evidence of what the producer ran, not
    an authenticated signature.
    """
    problems = []
    if expected_sources is None or "compiler_source_identity" not in expected_sources:
        return ["B1 compiler-source binding needs the current tree's source inputs"]
    try:
        identity, observations = receipt["identity"], receipt["observations"]
    except (KeyError, TypeError):
        return ["B1 compiler-source binding: the receipt has no identity or observations"]
    if identity.get("schema") != "cint-conformance-receipt-2" or identity.get("compiler") != "cintc":
        problems.append("B1 compiler-source binding: expected cint-conformance-receipt-2 of cintc")
    if identity.get("compiler_source_identity") != expected_sources["compiler_source_identity"]:
        problems.append("B1 compiler-source identity differs from the current compiler/main.ci "
                        "closure")
    if identity.get("compiler_source_method") != B1_SOURCE_METHOD:
        problems.append("B1 compiler-source method is missing or differs")
    if identity.get("emitter") != "cint emit-c":
        problems.append("B1 emitter is missing or not `cint emit-c`")
    cli = expected_sources.get("build_inputs", {}).get("cli")
    if cli is None or identity.get("cli_sha256") != records_digest(cli):
        problems.append("B1 CLI digest is missing or stale")
    b1 = observations.get("b1") if isinstance(observations, dict) else None
    if not isinstance(b1, dict) or not re.fullmatch(r"[0-9a-f]{64}", str(b1.get("cint_sha256")))             or b1.get("how") not in ("bootstrapped", "given"):
        problems.append("B1 executable digest or provenance is not recorded")
    if expected_provenance is not None:
        source = (expected_provenance.get("identity") or {}).get("compiler_source_identity")             if isinstance(expected_provenance, dict) else None
        if source != identity.get("compiler_source_identity"):
            problems.append("B1 bootstrap receipt names other compiler sources")
    return problems


def expected_c0_flags(cfg):
    """Derives the recorded flags without constructing a toolchain or starting a compiler."""
    import cint_check
    leg, opt, helpers, sanitize = cfg
    define = "CINT_RT_HELPERS_" + helpers.upper()
    if leg == "msvc":
        flags = cint_check.MSVC_WARNINGS + ["/Od" if opt == "0" else "/O2", "/D" + define]
    else:
        flags = cint_check.GNU_WARNINGS + (cint_check.CLANG_EXTRA if leg != "gcc" else []) + \
            ["-O" + opt, "-D" + define, "-fPIC"] + (cint_check.SANITIZE if sanitize else [])
        # Inspects receipts by leg, independently of the host reading them: the Linux legs
        # link -ldl, and macOS has dlopen in its C library.
        flags += [] if leg == "apple-clang" else ["-ldl"]
    return " ".join(flags)


def check_conformance(path, cfg, compiler, required, entries, current, b1_sources=None,
                      set_name=None):
    """Validate saved evidence independently of its pass flag (CONF-14 and CONF-15); with
    set_name, the evidence of that conformance set rather than of a stage."""
    import cint_check
    row = {"file": path.name, "identity": None, "pass": False, "problems": []}
    problems = row["problems"]
    try:
        receipt, data = read_evidence(path)
        identity, outcome, observations = (receipt[name] for name in
                                           ("identity", "outcome", "observations"))
        if not all(isinstance(part, dict) for part in (identity, outcome, observations)):
            raise ValueError("identity, outcome, and observations must be objects")
        if identity.get("schema") != "cint-conformance-receipt-2":
            raise ValueError("expected cint-conformance-receipt-2; run receipts are not conformance")
        if data != cint_receipts.canonical(receipt) + b"\n":
            problems.append("not canonical JSON followed by one LF")
        row["identity"] = cint_receipts.receipt_identity(receipt)
        if observations.get("receipt_identity_sha256") != row["identity"]:
            problems.append("receipt identity digest differs")
        if identity.get("set") != set_name:
            problems.append("expected %s; the receipt is %s" % (
                "set " + set_name if set_name else "a stage receipt",
                "for set %s" % identity["set"] if "set" in identity else "a stage receipt"))
        expected_compiler = {"seed": "cint-seed", "b1": "cintc"}[compiler]
        if identity.get("compiler") != expected_compiler:
            problems.append("wrong compiler: expected " + expected_compiler)
        if compiler == "b1":
            # The compiler binding of a B1 receipt (task 2.13): its compiler source identity
            # and CLI digest against the current tree.
            problems += check_b1_binding(receipt, b1_sources if b1_sources is not None
                                          else b1_binding_inputs())
        if identity.get("scope") != "full" or identity.get("profile") != "cint-core-1":
            problems.append("expected full scope and cint-core-1")
        for name, value in current.items():
            if identity.get(name) != value:
                problems.append("stale or missing identity." + name)
        emitted = identity.get("emitted_c_count")
        if type(emitted) is not int or emitted <= 0 or \
                not isinstance(identity.get("emitted_c_tree_sha256"), str) or \
                not re.fullmatch(r"[0-9a-f]{64}", identity["emitted_c_tree_sha256"]) or \
                identity.get("emitted_c_method") != \
                "sha256 over the lines '<sha256>  <program>.c\\n', sorted by bytes":
            problems.append("emitted-C identity is missing or malformed")
        leg, opt, helpers, sanitize = cfg
        for name, value in (("leg", leg), ("opt", int(opt)), ("helpers", helpers),
                            ("sanitize", sanitize)):
            if observations.get(name) != value or type(observations.get(name)) is not type(value):
                problems.append("wrong configuration: " + name)
        c0 = observations["c0"]
        if c0.get("name") != leg or c0.get("flags") != expected_c0_flags(cfg):
            problems.append("recorded C0 name or flags contradict the requested configuration")
        if leg == "apple-clang":
            host = observations["host"]
            if host.get("arch") != "arm64" or not str(host.get("os", "")).startswith("macOS ") or \
                    not str(c0.get("full_version", "")).startswith("Apple clang") or \
                    not isinstance(c0.get("sdk"), str) or not c0["sdk"]:
                problems.append("Apple Clang receipt lacks the macOS AArch64 host, the Apple clang "
                                "version, or the SDK")
        if type(observations.get("sanitizer_reports")) is not int or \
                observations["sanitizer_reports"] != 0:
            problems.append("sanitizer reports missing or nonzero")
        req = outcome["required"]
        if not isinstance(req, dict) or req.get("pass") is not True or \
                type(req.get("problem_count")) is not int or req["problem_count"] != 0 or \
                req.get("problems") != []:
            problems.append("required-case evidence missing or failed")
        if req.get("sha256") != hashlib.sha256(required.read_bytes()).hexdigest():
            problems.append("required-case list digest differs")
        if req.get("file") != (required.relative_to(ROOT).as_posix()
                               if required.is_relative_to(ROOT) else required.name):
            problems.append("required-case list path differs")
        wanted = {case: value for (case, comp), value in entries.items()
                  if comp == expected_compiler}
        # CONF-15 exclusion rule (Proposed, task 2.14): a case the list excludes from its boot
        # scope, with no row for this compiler, is a row of category `excluded`.
        exclusions = cint_check.read_exclusions(required)
        if not wanted:
            problems.append("required list has no cases for " + expected_compiler)
        actual = {}
        categories = outcome["not_compared"]
        if set(categories) != {"held", "not_applicable", "outside_subset", "unsupported"}:
            raise ValueError("not_compared must contain all four CONF-14 categories")
        for category, cases in categories.items():
            if not isinstance(cases, list):
                raise ValueError("category cases must be lists")
            previous = None
            for case in cases:
                name = case["case"]
                if not isinstance(name, str) or (previous is not None and name <= previous):
                    raise ValueError("category cases must be unique and sorted by case bytes")
                previous = name
                if name in actual:
                    raise ValueError("case occurs in more than one category: " + name)
                detail = cint_check.required_token(category, case)
                actual[name] = (category, detail)
                if wanted.get(name) != actual[name] and                         not cint_check.excluded(entries, exclusions, name, expected_compiler):
                    problems.append("unlisted or wrong category: " + name)
                if category == "outside_subset" and compiler != "seed":
                    problems.append("B1 cannot have outside_subset cases")
                if category == "unsupported" and (case.get("compiler") != expected_compiler or
                        not re.fullmatch(r"C9[0-9]{3}", case.get("code", "")) or
                        not case.get("position") or not case.get("frozen_outcome")):
                    problems.append("incomplete unsupported case: " + name)
        for name, category in wanted.items():
            if category[0] != "compared" and actual.get(name) != category:
                problems.append("required category evidence absent: " + name)
        agreement = outcome["agreement"]
        if "first_disagreement" not in agreement or agreement["first_disagreement"] is not None:
            problems.append("first disagreement is missing or contradicts a passing run")
        if type(agreement.get("programs_compiled")) is not int or \
                agreement["programs_compiled"] != emitted:
            problems.append("compiled-program count differs from the emitted-C identity")
        if type(agreement.get("disagree")) is not int or agreement["disagree"] != 0 or \
                type(agreement.get("agree")) is not int or \
                agreement.get("agree") != agreement.get("compared") \
                or type(agreement.get("compared")) is not int or agreement["compared"] <= 0:
            problems.append("agreement counts are missing or disagree")
        elif agreement["compared"] < sum(category[0] == "compared" for category in wanted.values()):
            problems.append("fewer compared records than required cases")
        if type(outcome["conformance"].get("failed")) is not int or \
                outcome["conformance"]["failed"] != 0 or \
                outcome["conformance"].get("suite_sha256") != identity.get("suite_sha256"):
            problems.append("conformance failed or names a different suite")
        if type(outcome["cint_ref_against_frozen"].get("differ")) is not int or \
                outcome["cint_ref_against_frozen"]["differ"] != 0 or \
                outcome["determinism"].get("differ") != [] or \
                outcome["symbol_audit"].get("pass") is not True:
            problems.append("reference, determinism, or symbol audit evidence failed")
        determinism_count = outcome["determinism"].get("programs")
        if type(determinism_count) is not int or determinism_count <= 0 or determinism_count != emitted:
            problems.append("determinism evidence does not cover every emitted program")
        if compiler == "b1":
            # SPEC-09 CONF-16 (box 10 default BX10-29): B1 writes the reflection table, and its
            # run compared the type signature of every export row of every emitted program
            # with cint_ref's.
            signatures = outcome.get("type_signatures")
            if not isinstance(signatures, dict) or \
                    type(signatures.get("compared")) is not int or signatures["compared"] <= 0 or \
                    type(signatures.get("disagree")) is not int or signatures["disagree"] != 0 or \
                    "first_disagreement" not in signatures or \
                    signatures["first_disagreement"] is not None or \
                    type(signatures.get("programs")) is not int or signatures["programs"] != emitted:
                problems.append("type signature evidence (CONF-16) is missing, does not cover "
                                "every emitted program, or disagrees")
        audit = observations["symbol_audit"]
        if sanitize:
            if audit != {"exempt": "EMIT-20"}:
                problems.append("sanitized symbol audit requires the EMIT-20 exemption")
        else:
            # Every emitted program was audited, or is a B1 entry program linked as an
            # executable with a C `main`, which the audit does not cover (CINTC-OQ-36).
            entries = audit.get("entry_programs", [])
            if audit.get("outside_namespace") != [] or "exempt" in audit or \
                    type(audit.get("program_objects")) is not int or type(entries) is not list or \
                    not all(isinstance(name, str) for name in entries) or \
                    entries != sorted(set(entries), key=lambda name: name.encode("utf-8")) or \
                    (entries and compiler == "seed") or \
                    audit["program_objects"] + len(entries) != emitted:
                problems.append("symbol audit records findings or omits emitted-program coverage")
        # The producer lists the differing files (tools/cint_check.py verify_tables); a set
        # runs no table check, which belongs to the stages.
        tables = outcome["tables_regenerated"] if set_name is None else {
            "differ": [], "files": 1, "match": 1}
        if tables.get("differ") != [] or \
                type(tables.get("files")) is not int or tables["files"] <= 0 or \
                type(tables.get("match")) is not int or tables["match"] != tables["files"]:
            problems.append("table verification missing or failed")
        if outcome.get("pass") is not True:
            problems.append("the conformance run failed")
        row["pass"] = not problems
    except (OSError, ValueError, KeyError, TypeError, AttributeError, RecursionError) as error:
        problems.append("missing or malformed evidence: " + str(error))
    return row


def check_fuzz(path):
    """Validate the existing cint-fuzz-1 producer, including its source hashes."""
    problems = []
    try:
        receipt, _ = read_evidence(path)
        if receipt.get("schema") != "cint-fuzz-1":
            raise ValueError("expected cint-fuzz-1")
        for name in ("ref/cint_ref/faults.py", "ref/cint_ref/expect.py",
                     "conformance/tools/fuzz_decoders.py"):
            if receipt["sources_sha256"].get(name) != hashlib.sha256(
                    (ROOT / name).read_bytes()).hexdigest():
                problems.append("stale or missing fuzz source: " + name)
        targets = receipt["targets"]
        if sorted(row["target"] for row in targets) != ["expect", "record"]:
            problems.append("fuzz receipt requires exactly the record and expect targets")
        for row in targets:
            for field in ("seed", "count", "accepted", "rejected", "failures"):
                if type(row[field]) is not int or row[field] < 0:
                    raise ValueError("invalid fuzz " + field)
            if row["count"] <= 0 or row["accepted"] + row["rejected"] < row["count"] or \
                    row["failures"] != 0 or not re.fullmatch(r"[0-9a-f]{64}", row["inputs_sha256"]):
                problems.append("fuzz target has no run evidence or has findings")
    except (OSError, ValueError, KeyError, TypeError, AttributeError, RecursionError) as error:
        problems.append("missing or malformed fuzz evidence: " + str(error))
    return problems


def current_measurement_identity():
    """The identity a measurement receipt of the current tree carries (tools/cint_measure.py):
    the B1 compiler and runtime sources as tools/cint_bootstrap.py records them, the measurement
    tool, the measured corpus, and the bridge's frozen phase budget."""
    import cint_bootstrap
    import cint_check
    import cint_measure
    sources = b1_binding_inputs()
    corpus = {top + "/" + rel: (ROOT / top / rel).read_bytes()
              for top, rel in cint_measure.inventory(ROOT)}
    k, c = cint_measure.bridge_budget()
    return {"profile": "cint-core-1", "runtime_contract": cint_check.runtime_contract(),
            "fuel_encoding": "fuel-v1",
            "compiler_source_identity": sources["compiler_source_identity"],
            "runtime_sources": sources["runtime_sources"],
            "measurement_sources": cint_measure.source_rows(
                {name: (ROOT / name).read_bytes() for name in cint_measure.MEASUREMENT_INPUTS}),
            "inventory_sha256": hashlib.sha256(cint_bootstrap.source_manifest(corpus)).hexdigest(),
            "production_budget": {"k": k, "c": c}}


def check_measurement(path, leg, expected):
    """The problems of one K and phase-fuel receipt (empty when it holds) and the receipt.

    The receipt must be tools/cint_measure.py's schema for leg, with an identity digest that
    covers its identity and outcome, and an identity equal to expected (the current tree's).
    Its outcome must show every compiler module built, no measurement error, every measured
    phase call returned and within the frozen budget k x bytes + c (CINTC-09), K derived and
    within the T2 ceiling, and no input over the storage bound (CINTC-02)."""
    import cint_check
    import cint_measure
    problems = []
    try:
        receipt, _ = read_evidence(path)
        if receipt.get("schema") != cint_measure.SCHEMA:
            return ["expected %s" % cint_measure.SCHEMA], None
        identity, outcome = receipt["identity"], receipt["outcome"]
        if receipt["identity_sha256"] != hashlib.sha256(cint_check.canonical(
                {"identity": identity, "outcome": outcome})).hexdigest():
            problems.append("identity_sha256 does not cover the identity and outcome")
        if receipt["observations"]["leg"] != leg:
            problems.append("measured on leg %s, named for leg %s"
                            % (receipt["observations"]["leg"], leg))
        for field in sorted(set(expected) | set(identity)):
            if identity.get(field) != expected.get(field):
                problems.append("identity.%s differs from the current tree" % field)
        frozen, budget = outcome["frozen"], expected["production_budget"]
        if (frozen["k"], frozen["c"]) != (budget["k"], budget["c"]):
            problems.append("frozen k and c are not the bridge's budget")
        for field in ("calls", "calls_not_returned", "K", "K_ceiling"):
            if type(frozen[field]) is not int:
                raise ValueError("invalid frozen " + field)
        if frozen["calls"] <= 0 or frozen["calls_not_returned"] != 0 or \
                frozen["every_call_within"] is not True:
            problems.append("not every measured phase call returned within the frozen budget")
        if frozen["K_ceiling"] != cint_measure.K_CEILING or frozen["K_within_ceiling"] is not True or \
                not 0 <= frozen["K"] <= frozen["K_ceiling"]:
            problems.append("K is missing or over the T2 ceiling of %d" % cint_measure.K_CEILING)
        if outcome["storage"]["inputs_over_bound"] != []:
            problems.append("inputs over the storage bound: %d"
                            % len(outcome["storage"]["inputs_over_bound"]))
        if outcome["measurement_errors"] != []:
            problems.append("measurement errors: %d" % len(outcome["measurement_errors"]))
        if outcome["compiler_modules_not_built"] != [] or not outcome["compiler_modules_built"]:
            problems.append("not every compiler module was built and measured")
        if outcome["fuel"]["verified"] is not True:
            problems.append("the fuel fit is not verified")
    except (OSError, ValueError, KeyError, TypeError, AttributeError, RecursionError) as error:
        return ["missing or malformed measurement evidence: " + str(error)], None
    return problems, receipt


def check_measurements(paths, legs, expected):
    """The problems of the K and phase-fuel receipts in paths and their summary for
    acceptance.json: one `measure-<leg>.json` for each selected leg, each valid
    (check_measurement), and one identity_sha256 across all of them, since the measured outcome
    does not depend on the host."""
    problems, valid, by_leg = [], {}, {}
    names = {"measure-%s.json" % leg: leg for leg in LEGS}
    for path in paths:
        if path.name in names:
            by_leg[names[path.name]] = path
        else:
            problems.append("unrequested measurement receipt: " + path.name)
    problems += ["missing measurement receipt of leg %s" % leg for leg in legs if leg not in by_leg]
    for leg, path in sorted(by_leg.items()):
        found, receipt = check_measurement(path, leg, expected)
        problems += [path.name + ": " + problem for problem in found]
        if receipt is not None and not found:
            valid[leg] = receipt
    identities = sorted({receipt["identity_sha256"] for receipt in valid.values()})
    if len(identities) > 1:
        problems.append("measurement receipts have %d identities; the legs disagree" % len(identities))
    summary = {"legs": sorted(valid), "identity_sha256": identities}
    if len(identities) == 1:
        frozen = next(iter(valid.values()))["outcome"]["frozen"]
        summary.update({name: frozen[name] for name in ("k", "c", "calls", "K", "K_ceiling")})
    return problems, summary


def supplemental_evidence(directory, skip_measurements, legs=LEGS):
    """Fuzz evidence, and unless skipped, the K and phase-fuel evidence of the selected legs."""
    result = {"missing": [], "problems": [], "files": [],
              "measurements_skipped": skip_measurements}
    if not skip_measurements:
        measurements = sorted(directory.glob("measure-*.json"))
        if not measurements:
            result["missing"] += ["K", "phase-fuel"]
        else:
            result["files"] += [p.name for p in measurements]
            try:
                expected = current_measurement_identity()
            except (OSError, ValueError, KeyError, AttributeError) as error:
                result["problems"].append("current measurement identity unavailable: " + str(error))
            else:
                problems, result["measurements"] = check_measurements(measurements, legs, expected)
                result["problems"] += problems
    fuzz = sorted(directory.glob("fuzz*.json"))
    if not fuzz:
        result["missing"].append("fuzz")
    for path in fuzz:
        result["files"].append(path.name)
        result["problems"] += [path.name + ": " + problem for problem in check_fuzz(path)]
    return result


def receipt_name(stage, compiler, cfg) -> str:
    """The T1 seed path keeps the slice 1 names; other compilers carry their name."""
    if stage == "T1" and compiler == "seed":
        return "receipt-%s.json" % tag(cfg)
    return "receipt-%s-%s.json" % (compiler, tag(cfg))


def saved_receipt(directory, stage, compiler, cfg):
    """The saved receipt of a configuration; a seed receipt may carry either name."""
    path = directory / receipt_name(stage, compiler, cfg)
    if compiler == "seed" and not path.exists():
        for name in ("receipt-%s.json" % tag(cfg), "receipt-seed-%s.json" % tag(cfg)):
            if (directory / name).exists():
                return directory / name
    return path


def t2_import_problems(directory):
    """Rejects ambiguous aliases and receipts outside the defined T2 matrix.

    Known receipts outside a selected provisional scope may remain in the input
    directory. Two names for one configuration are still duplicates.
    """
    problems, known = [], set()
    for compiler in STAGE_COMPILERS["T2"]:
        for cfg in configurations("T1", LEGS):
            names = [receipt_name("T2", compiler, cfg)]
            if compiler == "seed":
                names.append("receipt-%s.json" % tag(cfg))
            known.update(names)
            present = [name for name in names if (directory / name).exists()]
            if len(present) > 1:
                problems.append("duplicate %s configuration %s: %s" %
                                (compiler, tag(cfg), ", ".join(present)))
    for path in sorted(directory.glob("receipt-*.json")):
        if path.name not in known:
            problems.append("unrequested T2 receipt: " + path.name)
    return problems


def acceptance(ns, legs, required, out):
    """Run (unless --dry-run) and validate a stage's matrix from its saved receipts."""
    import cint_check
    directory = pathlib.Path(ns.receipts).resolve() if ns.receipts else out
    if ns.dry_run and ns.out and out.resolve().is_relative_to(directory):
        print("BLOCKED: --out must be outside the saved receipt directory")
        return 3
    stage_compilers = STAGE_COMPILERS[ns.stage] if ns.stage else SET_COMPILERS
    compilers = [ns.compiler] if ns.compiler else list(stage_compilers)
    name = ns.stage or ns.set
    cfgs = configurations("T1", legs)
    problems, rows = [], []
    if ns.stage == "T2" and ns.dry_run:
        problems += t2_import_problems(directory)
    supplemental = supplemental_evidence(directory, ns.skip_measurements, legs) \
        if ns.stage == "T2" else {"missing": [], "problems": [], "files": []}
    try:
        entries = cint_check.read_required(required)
        if not entries:
            raise ValueError("required case list is empty")
        if b"# Status: Draft" in required.read_bytes():
            problems.append("required case list is Draft; %s inventory is not approved"
                            % ("set" if ns.set else "stage"))
        if any(comp not in ("cint-seed", "cintc") for _, comp in entries):
            problems.append("required case list names an unknown compiler")
        current = {compiler: current_identity(compiler) for compiler in compilers}
        # The B1 binding inputs, read once per acceptance (check_b1_binding).
        b1_sources = b1_binding_inputs() if "b1" in compilers else None
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        entries, current, b1_sources = {}, {}, None
        problems.append("required inputs unavailable: " + str(error))
    if not ns.dry_run:
        if problems or any(compiler not in cint_check.COMPILERS for compiler in compilers):
            for problem in problems:
                print("BLOCKED: " + problem)
            if "b1" in compilers and "b1" not in cint_check.COMPILERS:
                print("BLOCKED: B1 conformance producer is not implemented (task 2.12a)")
            return 3
        if (out / "acceptance.json").exists():
            print("BLOCKED: acceptance summary already exists: " + str(out / "acceptance.json"))
            return 3
        out.mkdir(parents=True, exist_ok=True)
    # B1 compiles a program alike in every configuration of a leg: the B1 runs share their
    # compilations (cint_check.py --emit-cache) for this acceptance run only.
    emit_cache = None
    if not ns.dry_run and "b1" in compilers:
        cint_check.build_root().mkdir(parents=True, exist_ok=True)
        emit_cache = pathlib.Path(tempfile.mkdtemp(prefix="emit-cache-", dir=cint_check.build_root()))
    for compiler in compilers:
        for cfg in cfgs:
            if ns.dry_run:
                path = saved_receipt(directory, name, compiler, cfg)
            else:
                path = directory / receipt_name(name, compiler, cfg)
                if path.exists():
                    print("BLOCKED: receipt already exists: " + str(path))
                    return 3
                leg, opt, helpers, sanitize = cfg
                cmd = [sys.executable, "tools/cint_check.py", "--compiler", compiler,
                       "--leg", leg, "--opt", opt, "--helpers", helpers,
                       *(["--set", ns.set] if ns.set else ["--verify-tables"]),
                       "--required", str(required), "--receipt", str(path), "--jobs", str(ns.jobs)]
                if sanitize:
                    cmd.append("--sanitize")
                if compiler == "b1":
                    cmd += ["--emit-cache", str(emit_cache)]
                run = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, errors="replace")
                lines = [x for x in run.stdout.splitlines() if x.strip()]
                result = next((x for x in lines if x.startswith(tag(cfg) + ":")), None)
                print("%s: exit %d; %s" % (tag(cfg), run.returncode,
                                           result.split(": ", 1)[1] if result else
                                           (lines[-1] if lines else run.stderr.strip()[-300:])),
                      flush=True)
                if run.returncode != 0:
                    problems.append("%s: conformance command exited %d" % (path.name, run.returncode))
                    if run.returncode == 3:
                        print("BLOCKED: " + (run.stdout or run.stderr).strip()[-500:])
                        if emit_cache:
                            shutil.rmtree(emit_cache, ignore_errors=True)
                        return 3
            row = check_conformance(path, cfg, compiler, required, entries,
                                    current.get(compiler, {}), b1_sources, ns.set)
            row["compiler"] = compiler
            rows.append(row)
    if emit_cache:
        shutil.rmtree(emit_cache, ignore_errors=True)
    identities = {compiler: sorted({row["identity"] for row in rows
                                    if row["compiler"] == compiler and row["identity"]})
                  for compiler in compilers}
    for compiler, values in identities.items():
        if len(values) != 1:
            problems.append("%s: expected one receipt identity, found %d" % (compiler, len(values)))
    # T1 is one compiler's matrix (the seed, or B1 by --compiler); T2 is both compilers'; a
    # set is B1's.
    if ns.stage != "T2":
        stage_compilers = compilers
    complete_count = len(configurations("T1", LEGS)) * len(stage_compilers)
    scoped = ns.skip_measurements or set(compilers) != set(stage_compilers) or set(legs) != set(LEGS)
    full_matrix = not scoped and len(rows) == complete_count
    if not scoped and not full_matrix:
        problems.append("complete %s requires %d receipts including Apple Clang (CONF-09)"
                        % (name, complete_count))
    problems += supplemental["problems"]
    passed = not problems and not supplemental["missing"] and all(row["pass"] for row in rows)
    for row in rows:
        if row["problems"]:
            print(row["file"] + ": " + "; ".join(row["problems"]))
    for problem in problems:
        print(problem)
    summary = {"schema": "cint-stage-acceptance-1", "pass": passed,
               **({"set": ns.set} if ns.set else {"stage": ns.stage}),
               "complete": passed and full_matrix,
               "scope": "provisional" if scoped else "complete",
               "legs": list(legs), "complete_receipts": complete_count,
               "configurations": [tag(cfg) for cfg in cfgs], "compilers": compilers,
               "receipt_identities": identities, "receipts": rows, "problems": problems,
               "supplemental": supplemental,
               "required": {"file": required.relative_to(ROOT).as_posix()
                            if required.is_relative_to(ROOT) else required.name,
                            "sha256": hashlib.sha256(required.read_bytes()).hexdigest()
                            if required.is_file() else None}}
    # Writes a dry-run summary only to an explicitly requested output directory.
    if not ns.dry_run or ns.out:
        out.mkdir(parents=True, exist_ok=True)
        summary_path = out / "acceptance.json"
        if summary_path.exists():
            print("BLOCKED: acceptance summary already exists: " + str(summary_path))
            return 3
        summary_path.write_bytes(cint_receipts.canonical(summary) + b"\n")
    label = name + (" on " + ns.compiler if ns.compiler else "")
    if scoped:
        label += " (without measurements; provisional)" if ns.skip_measurements else " (provisional)"
    if supplemental["missing"]:
        missing = supplemental["missing"]
        text = missing[0] if len(missing) == 1 else " and ".join(missing) if len(missing) == 2 \
            else ", ".join(missing[:-1]) + ", and " + missing[-1]
        print("acceptance %s: fail, missing %s receipts" % (label, text))
    else:
        count = sum(len(values) for values in identities.values())
        print("acceptance %s: %s, %d receipts, %d receipt identit%s" %
              (label, "pass" if passed else "fail", sum(row["identity"] is not None for row in rows),
               count, "y" if count == 1 else "ies"))
    return 0 if passed else 1


def amendment(ns) -> int:
    """The historical qualification comparison (decisions 24 and 25); never stage acceptance."""
    spec = AMENDMENTS[ns.amendment]
    directory, original = pathlib.Path(ns.receipts), pathlib.Path(ns.original)
    problems, ids, originals = [], set(), set()
    ours = theirs = None
    for cfg in spec["configurations"]:
        path = directory / ("receipt-%s.json" % tag(cfg))
        try:
            receipt, data = read_evidence(path)
            if data != cint_receipts.canonical(receipt) + b"\n":
                problems.append(path.name + ": not canonical JSON followed by one LF")
            ident = cint_receipts.receipt_identity(receipt)
            obs = receipt["observations"]
            if obs.get("receipt_identity_sha256") != ident:
                problems.append(path.name + ": receipt identity digest differs")
            leg, opt, helpers, sanitize = cfg
            if (obs.get("leg"), obs.get("opt"), obs.get("helpers"), obs.get("sanitize")) != \
                    (leg, int(opt), helpers, sanitize) or obs["host"].get("os") != spec["host_os"]:
                problems.append(path.name + ": wrong configuration or host")
            if receipt["identity"].get("scope") != "full" or receipt["outcome"].get("pass") is not True:
                problems.append(path.name + ": not a passing full-scope run")
            ids.add(ident)
            ours = ours or receipt
        except (OSError, ValueError, KeyError, TypeError, AttributeError) as error:
            problems.append(path.name + ": missing or malformed evidence: " + str(error))
    for path in sorted(original.glob("receipt-*.json")):
        try:
            receipt, _ = read_evidence(path)
            originals.add(cint_receipts.receipt_identity(receipt))
            theirs = theirs or receipt
        except (OSError, ValueError, KeyError, TypeError) as error:
            problems.append(path.name + ": malformed original receipt: " + str(error))
    if len(ids) != 1 or len(originals) != 1:
        problems.append("expected one receipt identity on each side, found %d and %d"
                        % (len(ids), len(originals)))
    changed = []
    if ours and theirs:
        a, b = theirs["identity"], ours["identity"]
        changed = sorted(k for k in set(a) | set(b) if a.get(k) != b.get(k))
        unexpected = [k for k in changed if k not in spec["fields"]]
        if unexpected:
            problems.append("identity differs outside the reviewed differences: " + ", ".join(unexpected))
        if theirs["outcome"] != ours["outcome"]:
            problems.append("outcome differs from the original receipts")
    for problem in problems:
        print(problem)
    print("qualification amendment %s (decisions %s; not stage acceptance): %s, %d receipts, "
          "receipt identity %s against original %s; identity differs in %s"
          % (ns.amendment, spec["decisions"], "fail" if problems else "pass",
             sum(1 for cfg in spec["configurations"]
                 if (directory / ("receipt-%s.json" % tag(cfg))).is_file()),
             "/".join(sorted(ids)) or "none", "/".join(sorted(originals)) or "none",
             ", ".join(changed) or "nothing"))
    return 1 if problems else 0


def configurations(stage: str, legs) -> list:
    """(leg, opt, helpers, sanitize) of a stage, in run order."""
    if stage != "T1":
        raise ValueError("stage %s is not defined yet (T2 joins with plan tasks 2.12b and 2.18)"
                         % stage)
    out = []
    for leg in legs:
        for opt in ("0", "2"):
            for helpers in ("portable", "builtin"):
                for sanitize in ((False,) if leg in ("msvc", "apple-clang") else (False, True)):
                    out.append((leg, opt, helpers, sanitize))
    return out


def tag(cfg) -> str:
    leg, opt, helpers, sanitize = cfg
    return "%s-%s-%s%s" % (leg, opt, helpers, "-san" if sanitize else "")


def main(argv=None) -> int:
    import cint_check
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--stage", choices=["T1", "T2"])
    ap.add_argument("--set", choices=sorted(cint_check.SETS),
                    help="a conformance set instead of a stage (default list "
                         "conformance/required/<SET>.txt)")
    ap.add_argument("--required", help="the stage's required case list")
    ap.add_argument("--out", help="receipt directory, e.g. results/cint/slice2/t1")
    ap.add_argument("--compiler", choices=["seed", "b1"], help="one compiler (T2 defaults to both)")
    ap.add_argument("--skip-measurements", action="store_true", help="provisional check without K or phase fuel")
    ap.add_argument("--dry-run", action="store_true",
                    help="import: validate saved receipts without running conformance")
    ap.add_argument("--receipts", help="saved receipt directory for --dry-run or --amendment")
    ap.add_argument("--legs", help="comma-separated subset of %s (default: a run %s; an import all)"
                    % (", ".join(LEGS), ",".join(RUN_LEGS)))
    ap.add_argument("--amendment", choices=sorted(AMENDMENTS),
                    help="historical qualification comparison of --receipts with --original")
    ap.add_argument("--original", help="the original receipts of --amendment")
    ap.add_argument("--jobs", type=int, default=4, help="workers inside each cint_check run")
    ns = ap.parse_args(argv)
    if ns.amendment:
        if not ns.receipts or not ns.original or ns.stage or ns.set or ns.dry_run:
            ap.error("--amendment takes --receipts and --original, without --stage, --set or "
                     "--dry-run")
        return amendment(ns)
    if not ns.stage and not ns.set:
        ap.error("--stage is required")
    if ns.stage and ns.set:
        ap.error("--stage and --set are exclusive")
    if ns.set and ns.compiler not in (None,) + SET_COMPILERS:
        ap.error("--set runs on %s" % ", ".join(SET_COMPILERS))
    if ns.original:
        ap.error("--original requires --amendment")
    if ns.dry_run != bool(ns.receipts):
        ap.error("--dry-run and --receipts are required together")
    if ns.skip_measurements and ns.stage != "T2":
        ap.error("--skip-measurements requires --stage T2")
    if ns.stage == "T1" and (not ns.required or (not ns.out and not ns.dry_run)):
        ap.error("--stage T1 requires --required and --out (or --dry-run --receipts)")
    if ns.set and not ns.out and not ns.dry_run:
        ap.error("--set requires --out (or --dry-run --receipts)")
    ns.required = ns.required or ("conformance/required/%s.txt" % ns.set if ns.set
                                  else "conformance/required/T2.txt")
    legs = [x for x in (ns.legs or ",".join(LEGS if ns.dry_run else RUN_LEGS)).split(",") if x]
    if not legs or len(legs) != len(set(legs)) or any(x not in LEGS for x in legs):
        ap.error("--legs takes a comma-separated subset of %s" % ", ".join(LEGS))
    required = (ROOT / ns.required).resolve() if not pathlib.Path(ns.required).is_absolute() \
        else pathlib.Path(ns.required)
    out = (ROOT / (ns.out or "results/cint/t2")).resolve()
    return acceptance(ns, legs, required, out)


if __name__ == "__main__":
    sys.exit(main())
