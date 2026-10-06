"""Synthetic acceptance evidence only. No compiler, build, or measurement runs."""
import contextlib
import copy
import hashlib
import io
import json
import pathlib
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import cint_accept
import cint_check
import cint_receipts


class AcceptanceEvidence(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="synthetic-accept-")
        self.addCleanup(self.tmp.cleanup)
        self.directory = pathlib.Path(self.tmp.name)
        self.required = self.directory / "required.txt"
        self.required.write_bytes(b"arith/x cint-seed compared\narith/x cintc compared\n")
        self.receipt = copy.deepcopy(json.loads((ROOT / "results/cint/slice2/t1-gb/"
                                                "receipt-msvc-0-portable.json").read_bytes()))
        self.receipt["outcome"]["required"].update(
            file="required.txt", sha256=hashlib.sha256(self.required.read_bytes()).hexdigest())
        self.receipt["outcome"]["not_compared"] = {
            name: [] for name in ("held", "not_applicable", "outside_subset", "unsupported")}
        self.receipt["outcome"]["tables_regenerated"] = {"differ": [], "files": 1, "match": 1}
        self.path = self.directory / "receipt-msvc-0-portable.json"
        self.current = dict(self.receipt["identity"])
        for field in ("emitted_c_count", "emitted_c_method", "emitted_c_tree_sha256"):
            self.current.pop(field)
        self.entries = cint_check.read_required(self.required)

    def write_receipt(self):
        self.receipt["observations"]["receipt_identity_sha256"] = cint_receipts.receipt_identity(
            self.receipt)
        self.path.write_bytes(cint_receipts.canonical(self.receipt) + b"\n")

    def problems(self):
        self.write_receipt()
        return cint_accept.check_conformance(self.path, ("msvc", "0", "portable", False),
                                             "seed", self.required, self.entries,
                                             self.current)["problems"]

    def test_existing_receipt_contract_can_validate_without_running_compilers(self):
        self.assertEqual(self.problems(), [])

    def test_missing_malformed_and_run_receipts_fail_closed(self):
        for content in (None, b"{", b"[]\n", b'{"identity":{"kind":"run"}}\n'):
            with self.subTest(content=content):
                self.path.unlink(missing_ok=True)
                if content is not None:
                    self.path.write_bytes(content)
                row = cint_accept.check_conformance(
                    self.path, ("msvc", "0", "portable", False), "seed", self.required,
                    self.entries, self.current)
                self.assertFalse(row["pass"])
                self.assertTrue(row["problems"])

    def test_rejects_wrong_compiler_configuration_and_stale_sources(self):
        for part, field, value in (("identity", "compiler", "cintc"),
                                   ("identity", "scope", "quick"),
                                   ("identity", "seed_sha256", "0" * 64),
                                   ("identity", "suite_sha256", "0" * 64),
                                   ("observations", "opt", 2),
                                   ("observations", "sanitize", True)):
            with self.subTest(field=field):
                saved = self.receipt[part][field]
                self.receipt[part][field] = value
                self.assertTrue(self.problems())
                self.receipt[part][field] = saved

    def test_required_evidence_is_mandatory_and_bound_to_exact_list(self):
        saved = copy.deepcopy(self.receipt["outcome"]["required"])
        for value in (None, {}, dict(saved, sha256="0" * 64), dict(saved, pass_=False),
                      dict(saved, problem_count=1), dict(saved, problems=["absent"])):
            with self.subTest(value=value):
                if value is None:
                    self.receipt["outcome"].pop("required", None)
                else:
                    if "pass_" in value:
                        value["pass"] = value.pop("pass_")
                    self.receipt["outcome"]["required"] = value
                self.assertTrue(self.problems())
        self.receipt["outcome"]["required"] = saved

    def test_rejects_unlisted_or_wrong_category_and_missing_tables(self):
        self.receipt["outcome"]["not_compared"]["held"] = [
            {"case": "arith/x", "open_item": "I-2"}]
        self.assertTrue(self.problems())
        self.receipt["outcome"]["not_compared"]["held"] = []
        for differ in (0, ["integer-machine/exh8.txt"], None):
            with self.subTest(differ=differ):   # the producer lists differing files
                self.receipt["outcome"]["tables_regenerated"]["differ"] = differ
                self.assertTrue(self.problems())
        self.receipt["outcome"]["tables_regenerated"]["differ"] = []
        del self.receipt["outcome"]["tables_regenerated"]
        self.assertTrue(self.problems())

    def test_false_pass_flags_do_not_hide_failures(self):
        for part, field, value in (("agreement", "disagree", 1),
                                   ("agreement", "agree", 1),
                                   ("conformance", "failed", 1),
                                   ("cint_ref_against_frozen", "differ", 1),
                                   ("determinism", "differ", ["arith/x"]),
                                   ("symbol_audit", "pass", False)):
            with self.subTest(field=field):
                saved = self.receipt["outcome"][part][field]
                self.receipt["outcome"][part][field] = value
                self.assertTrue(self.problems())
                self.receipt["outcome"][part][field] = saved

    def test_emitted_source_evidence_is_present_and_well_formed(self):
        mutations = [("emitted_c_tree_sha256", value) for value in
                     (None, "not-a-sha256", "A" * 64, 1)] + [
                         ("emitted_c_count", value) for value in (None, 0, True, 175)]
        for field, value in mutations:
            with self.subTest(field=field, value=value):
                saved = self.receipt["identity"][field]
                if value is None:
                    self.receipt["identity"].pop(field)
                else:
                    self.receipt["identity"][field] = value
                try:
                    self.assertTrue(self.problems())
                finally:
                    self.receipt["identity"][field] = saved

    def test_determinism_records_cover_the_emitted_programs(self):
        for value in (None, 0, True, 175):
            with self.subTest(programs=value):
                saved = self.receipt["outcome"]["determinism"]["programs"]
                if value is None:
                    self.receipt["outcome"]["determinism"].pop("programs")
                else:
                    self.receipt["outcome"]["determinism"]["programs"] = value
                try:
                    self.assertTrue(self.problems())
                finally:
                    self.receipt["outcome"]["determinism"]["programs"] = saved

    def test_symbol_findings_and_missing_audit_cannot_hide_behind_pass(self):
        for value in (None, ["runtime forbidden_symbol"]):
            with self.subTest(outside_namespace=value):
                if value is None:
                    self.receipt["observations"]["symbol_audit"].pop("outside_namespace")
                else:
                    self.receipt["observations"]["symbol_audit"]["outside_namespace"] = value
                self.assertTrue(self.problems())

    def test_first_disagreement_is_present_and_null_on_pass(self):
        self.receipt["outcome"]["agreement"].pop("first_disagreement")
        self.assertTrue(self.problems())
        self.receipt["outcome"]["agreement"]["first_disagreement"] = "compiled result differs"
        self.assertTrue(self.problems())

    def test_c0_name_and_flags_match_the_requested_configuration(self):
        flags = self.receipt["observations"]["c0"]["flags"]
        for field, value in (("name", "gcc"), ("name", None), ("flags", None),
                              ("flags", flags.replace("/Od", "/O2")),
                              ("flags", flags.replace("PORTABLE", "BUILTIN")),
                              ("flags", flags.replace("/WX ", "")),
                              ("flags", flags + " /w")):
            with self.subTest(field=field, value=value):
                saved = self.receipt["observations"]["c0"][field]
                if value is None:
                    self.receipt["observations"]["c0"].pop(field)
                else:
                    self.receipt["observations"]["c0"][field] = value
                try:
                    self.assertTrue(self.problems())
                finally:
                    self.receipt["observations"]["c0"][field] = saved

    def test_sanitized_receipts_require_sanitizer_flags_and_the_audit_exemption(self):
        flags = ("-std=c17 -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow -Wpedantic "
                 "-Werror -Wimplicit-int-conversion -O2 -DCINT_RT_HELPERS_BUILTIN -fPIC "
                 "-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer "
                 "-g -ldl")
        self.receipt["observations"].update(
            leg="clang", opt=2, helpers="builtin", sanitize=True,
            c0={"name": "clang", "version": "18.1.3", "flags": flags},
            symbol_audit={"exempt": "EMIT-20"}, rt_defined_symbols=None)

        def check():
            self.write_receipt()
            return cint_accept.check_conformance(
                self.path, ("clang", "2", "builtin", True), "seed", self.required,
                self.entries, self.current)

        self.assertTrue(check()["pass"])
        for audit in ({}, {"exempt": "unlisted"},
                      {"exempt": "EMIT-20", "outside_namespace": ["forbidden_symbol"]}):
            with self.subTest(audit=audit):
                self.receipt["observations"]["symbol_audit"] = audit
                self.assertFalse(check()["pass"])
        self.receipt["observations"]["symbol_audit"] = {"exempt": "EMIT-20"}
        self.receipt["observations"]["c0"]["flags"] = flags.replace("-fsanitize=address,undefined ", "")
        self.assertFalse(check()["pass"])

    def test_duplicate_keys_non_integer_numbers_and_bad_digest_fail(self):
        self.write_receipt()
        data = self.path.read_bytes()
        for bad in (data.replace(b'"compiler":"cint-seed"',
                                  b'"compiler":"cintc","compiler":"cint-seed"'),
                    data.replace(b'"opt":0', b'"opt":0.0'),
                    data.replace(b'"opt":0', b'"opt":9007199254740992')):
            with self.subTest(data=bad[:30]):
                self.path.write_bytes(bad)
                self.assertTrue(cint_accept.check_conformance(
                    self.path, ("msvc", "0", "portable", False), "seed", self.required,
                    self.entries, self.current)["problems"])

    def test_dry_run_does_not_spawn_or_change_input_receipts(self):
        self.write_receipt()
        before = {p.name: p.read_bytes() for p in self.directory.iterdir()}
        output = io.StringIO()
        with patch.object(cint_accept, "current_identity", return_value=self.current), \
                patch.object(cint_accept.subprocess, "run", side_effect=AssertionError("spawn")), \
                contextlib.redirect_stdout(output):
            code = cint_accept.main(["--stage", "T2", "--dry-run", "--receipts",
                                     str(self.directory), "--required", str(self.required)])
        self.assertEqual(code, 1)
        self.assertEqual(output.getvalue().splitlines()[-1],
                         "acceptance T2: fail, missing K, phase-fuel, and fuzz receipts")
        self.assertEqual(before, {p.name: p.read_bytes() for p in self.directory.iterdir()})

    def test_dry_run_rejects_equal_equivalent_and_descendant_output_directories(self):
        self.write_receipt()

        def snapshot():
            return {p.relative_to(self.directory).as_posix(): p.read_bytes() if p.is_file() else None
                    for p in self.directory.rglob("*")}

        for out in (self.directory, self.directory / ".", self.directory / "unused" / "..",
                    self.directory / "nested-output",
                    self.directory / "unused" / ".." / "nested-output"):
            with self.subTest(out=out):
                before = snapshot()
                output = io.StringIO()
                with patch.object(cint_accept, "current_identity", return_value=self.current), \
                        patch.object(cint_accept.subprocess, "run", side_effect=AssertionError("spawn")), \
                        contextlib.redirect_stdout(output):
                    code = cint_accept.main(["--stage", "T2", "--dry-run", "--receipts",
                                             str(self.directory), "--required", str(self.required),
                                             "--out", str(out)])
                self.assertEqual(snapshot(), before)
                self.assertEqual(code, 3)
                self.assertIn("outside the saved receipt directory", output.getvalue())

    def test_dry_run_can_write_a_summary_outside_the_input_tree(self):
        self.write_receipt()
        before = {p.name: p.read_bytes() for p in self.directory.iterdir()}
        with tempfile.TemporaryDirectory(prefix="synthetic-accept-output-") as directory, \
                patch.object(cint_accept, "current_identity", return_value=self.current), \
                patch.object(cint_accept.subprocess, "run", side_effect=AssertionError("spawn")), \
                contextlib.redirect_stdout(io.StringIO()):
            code = cint_accept.main(["--stage", "T2", "--dry-run", "--receipts",
                                     str(self.directory), "--required", str(self.required),
                                     "--out", directory])
            self.assertEqual(code, 1)
            self.assertTrue((pathlib.Path(directory) / "acceptance.json").is_file())
        self.assertEqual(before, {p.name: p.read_bytes() for p in self.directory.iterdir()})

    def test_fuzz_receipts_require_both_targets_counts_and_current_decoder_sources(self):
        hashes = {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
                  for name in ("ref/cint_ref/faults.py", "ref/cint_ref/expect.py",
                               "conformance/tools/fuzz_decoders.py")}
        receipt = {"schema": "cint-fuzz-1", "sources_sha256": hashes, "targets": [
            {"target": target, "seed": 7, "count": 10, "accepted": 5, "rejected": 20,
             "failures": 0, "inputs_sha256": "1" * 64} for target in ("record", "expect")]}
        path = self.directory / "fuzz.json"
        for mutate, valid in ((lambda r: None, True),
                              (lambda r: r["targets"].pop(), False),
                              (lambda r: r["targets"][0].update(failures=1), False),
                              (lambda r: r["targets"][0].update(count=0), False),
                              (lambda r: r["sources_sha256"].clear(), False)):
            candidate = copy.deepcopy(receipt)
            mutate(candidate)
            path.write_text(json.dumps(candidate), encoding="ascii")
            self.assertEqual(not cint_accept.check_fuzz(path), valid)

    def measurement(self, leg):
        """A K and phase-fuel receipt of the committed gcc form, measured on leg; its own identity
        stands for the current tree's (self.measured)."""
        receipt = copy.deepcopy(json.loads((ROOT / "results/cint/t2/measure-gcc.json").read_bytes()))
        receipt["observations"]["leg"] = leg
        self.measured = copy.deepcopy(receipt["identity"])
        return receipt

    def write_measurement(self, receipt, name=None, digest=True):
        if digest:
            receipt["identity_sha256"] = hashlib.sha256(cint_check.canonical(
                {"identity": receipt["identity"], "outcome": receipt["outcome"]})).hexdigest()
        path = self.directory / (name or "measure-%s.json" % receipt["observations"]["leg"])
        path.write_bytes(cint_check.canonical(receipt) + b"\n")
        return path

    def test_measurement_receipts_of_every_leg_with_one_identity_pass(self):
        paths = [self.write_measurement(self.measurement(leg)) for leg in cint_accept.LEGS]
        problems, summary = cint_accept.check_measurements(paths, cint_accept.LEGS, self.measured)
        self.assertEqual(problems, [])
        self.assertEqual(summary["legs"], sorted(cint_accept.LEGS))
        self.assertEqual(len(summary["identity_sha256"]), 1)
        self.assertEqual((summary["k"], summary["c"], summary["K"], summary["K_ceiling"]),
                         (21524, 19876, 628, 800))

    def test_measurement_receipt_contract_fails_closed(self):
        def frozen(**fields):
            return lambda r: r["outcome"]["frozen"].update(fields)

        for name, mutate in (
                ("schema", lambda r: r.update(schema="cint-measure-receipt-3")),
                ("leg", lambda r: r["observations"].update(leg="clang")),
                ("stale compiler", lambda r: r["identity"].update(compiler_source_identity="0" * 64)),
                ("stale corpus", lambda r: r["identity"].update(inventory_sha256="0" * 64)),
                ("another budget", lambda r: r["identity"].update(production_budget={"k": 1, "c": 1})),
                ("frozen k", frozen(k=21523)),
                ("a call over the budget", frozen(every_call_within=False)),
                ("a call not returned", frozen(calls_not_returned=1)),
                ("no calls", frozen(calls=0)),
                ("K over the ceiling", frozen(K=801)),
                ("K not within", frozen(K_within_ceiling=False)),
                ("another ceiling", frozen(K_ceiling=2048)),
                ("K absent", frozen(K=None)),
                ("Boolean count", frozen(calls_not_returned=False)),
                ("input over the bound", lambda r: r["outcome"]["storage"].update(inputs_over_bound=["x.ci"])),
                ("measurement error", lambda r: r["outcome"].update(measurement_errors=["x.ci: timeout"])),
                ("module not built", lambda r: r["outcome"].update(
                    compiler_modules_not_built=["compiler/check.ci"])),
                ("fuel not verified", lambda r: r["outcome"]["fuel"].update(verified=False)),
                ("no outcome", lambda r: r.pop("outcome"))):
            with self.subTest(name=name):
                receipt = self.measurement("gcc")
                mutate(receipt)
                path = self.write_measurement(receipt, "measure-gcc.json", digest=name != "no outcome")
                problems, receipt = cint_accept.check_measurement(path, "gcc", self.measured)
                self.assertTrue(problems)
        receipt = self.measurement("gcc")
        path = self.write_measurement(receipt)
        receipt["outcome"]["frozen"]["K"] = 1100   # a changed outcome under the old digest
        self.write_measurement(receipt, digest=False)
        self.assertIn("identity_sha256 does not cover the identity and outcome",
                      cint_accept.check_measurement(path, "gcc", self.measured)[0])

    def test_missing_unrequested_and_disagreeing_measurement_receipts(self):
        paths = [self.write_measurement(self.measurement(leg)) for leg in ("gcc", "clang")]
        other = self.measurement("msvc")
        other["outcome"]["frozen"]["calls"] += 1
        paths.append(self.write_measurement(other))
        paths.append(self.write_measurement(self.measurement("gcc"), "measure-gcc-old.json"))
        problems, summary = cint_accept.check_measurements(paths, cint_accept.LEGS, self.measured)
        self.assertIn("missing measurement receipt of leg apple-clang", problems)
        self.assertIn("unrequested measurement receipt: measure-gcc-old.json", problems)
        self.assertIn("measurement receipts have 2 identities; the legs disagree", problems)
        self.assertNotIn("K", summary)
        problems, summary = cint_accept.check_measurements(paths[:2], ("gcc", "clang"), self.measured)
        self.assertEqual((problems, summary["legs"]), ([], ["clang", "gcc"]))

    def test_malformed_measurement_files_fail_closed(self):
        self.measurement("msvc")
        (self.directory / "measure-msvc.json").write_text('{"pass":true}', encoding="ascii")
        with patch.object(cint_accept, "current_measurement_identity", return_value=self.measured):
            result = cint_accept.supplemental_evidence(self.directory, skip_measurements=False)
        self.assertEqual(result["missing"], ["fuzz"])
        self.assertIn("measure-msvc.json: expected cint-measure-receipt-4", result["problems"])
        self.assertIn("missing measurement receipt of leg gcc", result["problems"])

    def test_committed_measurement_receipts_meet_the_contract_of_their_tree(self):
        paths = [ROOT / "results/cint/t2" / ("measure-%s.json" % leg) for leg in ("gcc", "clang")]
        own = json.loads(paths[0].read_bytes())["identity"]
        problems, summary = cint_accept.check_measurements(paths, ("gcc", "clang"), own)
        self.assertEqual(problems, [])
        self.assertEqual((summary["k"], summary["c"], summary["K"]), (21524, 19876, 628))

    def test_current_measurement_identity_is_the_current_tree(self):
        import cint_measure
        identity = cint_accept.current_measurement_identity()
        k, c = cint_measure.bridge_budget()
        self.assertEqual(identity["production_budget"], {"k": k, "c": c})
        self.assertEqual(identity["compiler_source_identity"],
                         cint_accept.b1_binding_inputs()["compiler_source_identity"])
        self.assertEqual([row["path"] for row in identity["measurement_sources"]],
                         sorted(cint_measure.MEASUREMENT_INPUTS))
        self.assertRegex(identity["inventory_sha256"], "^[0-9a-f]{64}$")

    def test_a_dry_run_records_k_and_the_phase_fuel_budget(self):
        self.write_receipt()
        for leg in cint_accept.LEGS:
            self.write_measurement(self.measurement(leg))
        hashes = {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
                  for name in ("ref/cint_ref/faults.py", "ref/cint_ref/expect.py",
                               "conformance/tools/fuzz_decoders.py")}
        (self.directory / "fuzz.json").write_text(json.dumps({
            "schema": "cint-fuzz-1", "sources_sha256": hashes, "targets": [
                {"target": target, "seed": 7, "count": 10, "accepted": 5, "rejected": 20,
                 "failures": 0, "inputs_sha256": "1" * 64} for target in ("record", "expect")]}),
            encoding="ascii")
        output = io.StringIO()
        with tempfile.TemporaryDirectory(prefix="synthetic-accept-output-") as directory, \
                patch.object(cint_accept, "current_identity", return_value=self.current), \
                patch.object(cint_accept, "current_measurement_identity", return_value=self.measured), \
                patch.object(cint_accept.subprocess, "run", side_effect=AssertionError("spawn")), \
                contextlib.redirect_stdout(output):
            code = cint_accept.main(["--stage", "T2", "--dry-run", "--receipts",
                                     str(self.directory), "--required", str(self.required),
                                     "--out", directory])
            summary = json.loads((pathlib.Path(directory) / "acceptance.json").read_bytes())
        self.assertEqual(code, 1)   # one conformance receipt of 48
        self.assertNotIn("missing", output.getvalue().splitlines()[-1])
        self.assertFalse(any(p.startswith("measure-") for p in summary["problems"]))
        measured = summary["supplemental"]["measurements"]
        self.assertEqual((measured["legs"], measured["K"], measured["k"], measured["c"]),
                         (sorted(cint_accept.LEGS), 628, 21524, 19876))

    def test_skip_measurements_still_requires_fuzz(self):
        result = cint_accept.supplemental_evidence(self.directory, skip_measurements=True)
        self.assertEqual(result["missing"], ["fuzz"])
        self.assertEqual(result["problems"], [])

    def test_boolean_counts_are_malformed_evidence(self):
        self.receipt["observations"]["sanitizer_reports"] = False
        self.assertTrue(self.problems())
        self.receipt["observations"]["sanitizer_reports"] = 0
        self.receipt["outcome"]["tables_regenerated"]["match"] = True
        self.assertTrue(self.problems())

    def test_each_missing_configuration_fails_a_saved_matrix(self):
        names = []
        for opt in (0, 2):
            for helpers in ("portable", "builtin"):
                self.receipt["observations"].update(opt=opt, helpers=helpers)
                self.receipt["observations"]["c0"]["flags"] = (
                    "/nologo /std:c17 /W4 /WX %s /DCINT_RT_HELPERS_%s" %
                    ("/Od" if opt == 0 else "/O2", helpers.upper()))
                self.path = self.directory / ("receipt-msvc-%d-%s.json" % (opt, helpers))
                self.write_receipt()
                names.append(self.path)

        def check():
            output = io.StringIO()
            with patch.object(cint_accept, "current_identity", return_value=self.current), \
                    patch.object(cint_accept.subprocess, "run", side_effect=AssertionError("spawn")), \
                    contextlib.redirect_stdout(output):
                code = cint_accept.main(["--stage", "T1", "--dry-run", "--receipts",
                                         str(self.directory), "--required", str(self.required),
                                         "--legs", "msvc"])
            return code, output.getvalue()

        code, output = check()
        self.assertEqual((code, output.splitlines()[-1]),
                         (0, "acceptance T1 (provisional): pass, 4 receipts, 1 receipt identity"))
        for path in names:
            with self.subTest(path=path.name):
                data = path.read_bytes()
                path.unlink()
                code, output = check()
                self.assertEqual(code, 1)
                self.assertIn(path.name + ": missing or malformed evidence", output)
                path.write_bytes(data)

    def test_forged_b1_label_without_source_binding_cannot_pass(self):
        self.receipt["identity"]["compiler"] = "cintc"
        self.current["compiler"] = "cintc"
        self.write_receipt()
        row = cint_accept.check_conformance(self.path, ("msvc", "0", "portable", False),
                                            "b1", self.required, self.entries, self.current)
        self.assertFalse(row["pass"])
        self.assertTrue(any("B1 compiler-source identity" in problem for problem in row["problems"]))

    def bound_b1(self):
        """The synthetic receipt as a B1 receipt bound to the current tree (task 2.13)."""
        sources = cint_accept.b1_binding_inputs()
        self.receipt["identity"].update(
            compiler="cintc", compiler_source_identity=sources["compiler_source_identity"],
            compiler_source_method=cint_accept.B1_SOURCE_METHOD, emitter="cint emit-c",
            cli_sha256=cint_accept.records_digest(sources["build_inputs"]["cli"]))
        self.receipt["observations"]["b1"] = {"cint_sha256": "1" * 64, "how": "bootstrapped"}
        self.current["compiler"] = "cintc"
        # B1 writes the reflection table: its run compares type signatures (CONF-16) and names
        # their encoding.
        encodings = dict(self.current["encodings"], type_signature="cint-core-1/type-signature/v1")
        self.receipt["identity"]["encodings"], self.current["encodings"] = encodings, dict(encodings)
        self.receipt["outcome"]["type_signatures"] = {
            "compared": 7, "disagree": 0, "first_disagreement": None,
            "programs": self.receipt["identity"]["emitted_c_count"]}

    def b1_problems(self):
        self.write_receipt()
        return cint_accept.check_conformance(self.path, ("msvc", "0", "portable", False), "b1",
                                             self.required, self.entries, self.current)["problems"]

    def test_b1_receipt_with_consistent_identity_is_accepted(self):
        self.bound_b1()
        self.assertEqual(self.b1_problems(), [])

    def test_b1_entry_programs_complete_the_symbol_audit_coverage(self):
        # Two B1 script programs are executables with a C `main` and are not audited
        # (CINTC-OQ-36): the audited libraries and the named entry programs cover the emitted set.
        self.bound_b1()
        audit = self.receipt["observations"]["symbol_audit"]
        emitted = self.receipt["identity"]["emitted_c_count"]
        audit["program_objects"] = emitted - 2
        audit["entry_programs"] = ["control/print_hole_fault_partial",
                                   "control/print_no_newline_then_fault"]
        self.assertEqual(self.b1_problems(), [])
        coverage = "symbol audit records findings or omits emitted-program coverage"
        for entries in ([], ["control/a"], ["control/b", "control/a"], ["control/a", "control/a"],
                        "control/a,control/b", [1, 2]):
            with self.subTest(entry_programs=entries):
                audit["entry_programs"] = entries
                self.assertIn(coverage, self.b1_problems())
        audit.pop("entry_programs")
        self.assertIn(coverage, self.b1_problems())
        # The seed has no entry programs (it refuses scripts as outside its subset).
        audit["program_objects"] = emitted - 1
        audit["entry_programs"] = ["control/a"]
        self.assertIn(coverage, self.problems())

    def test_b1_receipt_needs_agreeing_type_signatures(self):
        # SPEC-09 CONF-16: every export row of every emitted program compared, none disagreeing.
        self.bound_b1()
        signatures = self.receipt["outcome"]["type_signatures"]
        evidence = ("type signature evidence (CONF-16) is missing, does not cover every emitted "
                    "program, or disagrees")
        emitted = self.receipt["identity"]["emitted_c_count"]
        for field, value in (("compared", 0), ("compared", True), ("compared", "7"),
                             ("disagree", 1), ("disagree", None),
                             ("first_disagreement", '{"export": "both"}'),
                             ("programs", emitted - 1), ("programs", None)):
            with self.subTest(field=field, value=value):
                saved = signatures[field]
                if value is None:
                    del signatures[field]
                else:
                    signatures[field] = value
                self.assertIn(evidence, self.b1_problems())
                signatures[field] = saved
        self.assertEqual(self.b1_problems(), [])
        self.receipt["outcome"].pop("type_signatures")
        self.assertIn(evidence, self.b1_problems())
        # A seed receipt has no table to compare.
        self.assertNotIn(evidence, self.problems())

    def test_b1_receipt_names_the_type_signature_encoding(self):
        self.bound_b1()
        self.receipt["identity"]["encodings"].pop("type_signature")
        self.assertIn("stale or missing identity.encodings", self.b1_problems())
        self.assertEqual(cint_check.encodings("cintc"),
                         dict(cint_check.encodings("cint-seed"),
                              type_signature="cint-core-1/type-signature/v1"))
        self.assertNotIn("type_signature", cint_check.encodings())

    def test_b1_receipt_with_inconsistent_identity_fails(self):
        self.bound_b1()
        for part, field, value in (("identity", "compiler_source_identity", "0" * 64),
                                   ("identity", "compiler_source_method", "other"),
                                   ("identity", "emitter", "cint build"),
                                   ("identity", "cli_sha256", "0" * 64),
                                   ("identity", "schema", "cint-conformance-receipt-1"),
                                   ("observations", "b1", {"cint_sha256": "x", "how": "bootstrapped"}),
                                   ("observations", "b1", None)):
            with self.subTest(field=field, value=value):
                saved = self.receipt[part].get(field)
                if value is None:
                    del self.receipt[part][field]
                else:
                    self.receipt[part][field] = value
                self.assertTrue(self.b1_problems())
                self.receipt[part][field] = saved
        self.assertEqual(self.b1_problems(), [])

    def configured(self, cfg):
        """A copy of the synthetic receipt for one configuration of the matrix."""
        leg, opt, helpers, sanitize = cfg
        receipt = copy.deepcopy(self.receipt)
        obs = receipt["observations"]
        obs.update(leg=leg, opt=int(opt), helpers=helpers, sanitize=sanitize)
        obs["c0"] = {"name": leg, "version": "1", "flags": cint_accept.expected_c0_flags(cfg)}
        if leg == "apple-clang":
            obs["host"] = {"arch": "arm64", "os": "macOS 27.0.1"}
            obs["c0"].update(full_version="Apple clang version 21.0.0 (clang-2100.3.34.2)",
                             sdk="MacOSX 27.0 (26A425)")
        if sanitize:
            obs["symbol_audit"], obs["rt_defined_symbols"] = {"exempt": "EMIT-20"}, None
        obs["receipt_identity_sha256"] = cint_receipts.receipt_identity(receipt)
        return receipt

    def write_matrix(self, legs, directory=None):
        directory = directory or self.directory
        for cfg in cint_accept.configurations("T1", legs):
            path = directory / ("receipt-%s.json" % cint_accept.tag(cfg))
            path.write_bytes(cint_receipts.canonical(self.configured(cfg)) + b"\n")

    def import_set(self, *extra):
        output = io.StringIO()
        with patch.object(cint_accept, "current_identity", return_value=self.current), \
                patch.object(cint_accept.subprocess, "run", side_effect=AssertionError("spawn")), \
                contextlib.redirect_stdout(output):
            code = cint_accept.main(["--stage", "T1", "--dry-run", "--receipts", str(self.directory),
                                     "--required", str(self.required), *extra])
        return code, output.getvalue().splitlines()

    def test_the_complete_set_of_24_passes_and_20_alone_is_provisional(self):
        self.write_matrix(cint_accept.LEGS)
        with tempfile.TemporaryDirectory(prefix="synthetic-accept-output-") as out:
            code, lines = self.import_set("--out", out)
            summary = json.loads((pathlib.Path(out) / "acceptance.json").read_bytes())
        self.assertEqual((code, lines[-1]), (0, "acceptance T1: pass, 24 receipts, 1 receipt identity"))
        self.assertEqual((summary["complete"], summary["scope"], summary["complete_receipts"]),
                         (True, "complete", 24))
        # The 20 Windows and Linux receipts alone pass only as a provisional, per-host result.
        for cfg in cint_accept.configurations("T1", ["apple-clang"]):
            (self.directory / ("receipt-%s.json" % cint_accept.tag(cfg))).unlink()
        code, lines = self.import_set("--legs", "msvc,gcc,clang")
        self.assertEqual((code, lines[-1]),
                         (0, "acceptance T1 (provisional): pass, 20 receipts, 1 receipt identity"))
        # Imported as the stage, a missing Apple configuration fails it.
        code, lines = self.import_set()
        self.assertEqual((code, lines[-1]), (1, "acceptance T1: fail, 20 receipts, 1 receipt identity"))
        self.assertIn("receipt-apple-clang-0-portable.json: missing or malformed evidence",
                      "\n".join(lines))

    def test_the_apple_host_alone_is_provisional(self):
        self.write_matrix(["apple-clang"])
        code, lines = self.import_set("--legs", "apple-clang")
        self.assertEqual((code, lines[-1]),
                         (0, "acceptance T1 (provisional): pass, 4 receipts, 1 receipt identity"))

    def test_receipts_of_two_revisions_are_not_one_identity(self):
        self.write_matrix(cint_accept.LEGS)
        for cfg in cint_accept.configurations("T1", ["apple-clang"]):
            receipt = self.configured(cfg)
            receipt["identity"]["emitted_c_tree_sha256"] = "a" * 64   # emitted by another revision
            receipt["observations"]["receipt_identity_sha256"] = cint_receipts.receipt_identity(receipt)
            (self.directory / ("receipt-%s.json" % cint_accept.tag(cfg))).write_bytes(
                cint_receipts.canonical(receipt) + b"\n")
        code, lines = self.import_set()
        self.assertEqual((code, lines[-1]), (1, "acceptance T1: fail, 24 receipts, 2 receipt identities"))

    def test_apple_receipts_need_the_apple_flags_host_and_sdk(self):
        cfg = ("apple-clang", "2", "builtin", False)
        good = self.configured(cfg)

        def problems(receipt):
            receipt["observations"]["receipt_identity_sha256"] = cint_receipts.receipt_identity(receipt)
            self.path.write_bytes(cint_receipts.canonical(receipt) + b"\n")
            return cint_accept.check_conformance(self.path, cfg, "seed", self.required,
                                                 self.entries, self.current)["problems"]

        self.assertEqual(problems(copy.deepcopy(good)), [])
        flags = good["observations"]["c0"]["flags"]
        for part, field, value in (("c0", "flags", flags + " -ldl"),
                                   ("c0", "flags", flags.replace(" -Wimplicit-int-conversion", "")),
                                   ("c0", "name", "clang"), ("c0", "sdk", ""),
                                   ("c0", "full_version", "clang version 21.0.0"),
                                   ("host", "arch", "x86-64"), ("host", "os", "Ubuntu 24.04.4 LTS")):
            with self.subTest(field=field, value=value):
                receipt = copy.deepcopy(good)
                receipt["observations"][part][field] = value
                self.assertTrue(problems(receipt))

    def test_t1_runner_validates_its_receipts_and_is_provisional_per_host(self):
        commands = []

        def producer(command, **kwargs):
            commands.append(command)
            cfg = (command[command.index("--leg") + 1], command[command.index("--opt") + 1],
                   command[command.index("--helpers") + 1], "--sanitize" in command)
            path = pathlib.Path(command[command.index("--receipt") + 1])
            path.write_bytes(cint_receipts.canonical(self.configured(cfg)) + b"\n")
            return types.SimpleNamespace(returncode=0, stdout="synthetic receipt\n", stderr="")

        def run(legs, out):
            output = io.StringIO()
            with patch.object(cint_accept, "current_identity", return_value=self.current), \
                    patch.object(cint_accept.subprocess, "run", side_effect=producer), \
                    contextlib.redirect_stdout(output):
                code = cint_accept.main(["--stage", "T1", "--required", str(self.required),
                                         "--out", str(out), "--legs", legs])
            return code, output.getvalue().splitlines()

        out = self.directory / "t1-mac"
        code, lines = run("apple-clang", out)
        self.assertEqual((code, lines[-1]),
                         (0, "acceptance T1 (provisional): pass, 4 receipts, 1 receipt identity"))
        self.assertTrue(all("--verify-tables" in c and "--required" in c for c in commands))
        summary = json.loads((out / "acceptance.json").read_bytes())
        self.assertEqual((summary["pass"], summary["complete"], summary["scope"], len(summary["receipts"])),
                         (True, False, "provisional", 4))
        self.assertEqual(sorted(p.name for p in out.iterdir())[:2],
                         ["acceptance.json", "receipt-apple-clang-0-builtin.json"])
        # Receipts and summaries are never overwritten.
        self.assertEqual(run("apple-clang", out)[0], 3)
        # A receipt without its table check fails, whatever the producer's exit status.
        self.receipt["outcome"].pop("tables_regenerated")
        code, lines = run("msvc", self.directory / "t1-msvc")
        self.assertEqual((code, lines[-1]),
                         (1, "acceptance T1 (provisional): fail, 4 receipts, 1 receipt identity"))
        self.assertFalse(any("--emit-cache" in c for c in commands))

    def test_b1_runs_share_one_emit_cache_for_the_run_only(self):
        commands = []

        def producer(command, **kwargs):
            commands.append(command)
            self.assertTrue(pathlib.Path(command[command.index("--emit-cache") + 1]).is_dir())
            return types.SimpleNamespace(returncode=1, stdout="", stderr="")

        with patch.object(cint_accept, "current_identity", return_value=self.current), \
                patch.object(cint_accept, "b1_binding_inputs", return_value={}), \
                patch.object(cint_accept.subprocess, "run", side_effect=producer), \
                contextlib.redirect_stdout(io.StringIO()):
            cint_accept.main(["--stage", "T1", "--compiler", "b1", "--required", str(self.required),
                              "--out", str(self.directory / "t1-b1"), "--legs", "apple-clang"])
        caches = {c[c.index("--emit-cache") + 1] for c in commands}
        self.assertEqual((len(commands), len(caches)), (4, 1))
        self.assertFalse(pathlib.Path(caches.pop()).exists())

    def test_a_set_is_accepted_from_its_own_receipts_on_b1(self):
        # A conformance set (t27 scoping note 7.2, TT-04): each receipt records the set in place
        # of the stage and has no table check; stage and set receipts are not interchangeable.
        self.bound_b1()
        self.current.pop("stage")   # current_identity() has no stage; the fixture copies it

        def set_problems():
            self.write_receipt()
            return cint_accept.check_conformance(
                self.path, ("msvc", "0", "portable", False), "b1", self.required, self.entries,
                self.current, set_name="t27-1")["problems"]

        self.assertEqual(set_problems(), ["expected set t27-1; the receipt is a stage receipt"])
        identity = self.receipt["identity"]
        identity.pop("stage")
        identity["set"] = "t27-1"
        self.assertEqual(self.b1_problems(), ["expected a stage receipt; the receipt is for set t27-1"])
        self.receipt["outcome"].pop("tables_regenerated")
        self.assertEqual(set_problems(), [])
        for cfg in cint_accept.configurations("T1", cint_accept.LEGS):
            (self.directory / cint_accept.receipt_name("t27-1", "b1", cfg)).write_bytes(
                cint_receipts.canonical(self.configured(cfg)) + b"\n")

        def accept(*extra):
            output = io.StringIO()
            with patch.object(cint_accept, "current_identity", return_value=self.current), \
                    patch.object(cint_accept.subprocess, "run", side_effect=AssertionError("spawn")), \
                    contextlib.redirect_stdout(output):
                code = cint_accept.main(["--set", "t27-1", "--dry-run", "--receipts",
                                         str(self.directory), "--required", str(self.required),
                                         *extra])
            return code, output.getvalue().splitlines()

        with tempfile.TemporaryDirectory(prefix="synthetic-accept-output-") as out:
            code, lines = accept("--out", out)
            summary = json.loads((pathlib.Path(out) / "acceptance.json").read_bytes())
        self.assertEqual((code, lines[-1]), (0, "acceptance t27-1: pass, 24 receipts, 1 receipt identity"))
        self.assertEqual((summary["set"], "stage" in summary, summary["compilers"], summary["complete"]),
                         ("t27-1", False, ["b1"], True))
        self.assertEqual(summary["supplemental"]["missing"], [])
        # A Draft list is not an approved inventory.
        self.required.write_bytes(b"# Status: Draft.\n" + self.required.read_bytes())
        self.entries = cint_check.read_required(self.required)
        code, lines = accept()
        self.assertEqual(code, 1)
        self.assertIn("required case list is Draft; set inventory is not approved", lines)

    def test_a_set_run_selects_the_set_and_skips_the_table_check(self):
        commands = []

        def producer(command, **kwargs):
            commands.append(command)
            return types.SimpleNamespace(returncode=1, stdout="", stderr="")

        with patch.object(cint_accept, "current_identity", return_value=self.current), \
                patch.object(cint_accept, "b1_binding_inputs", return_value={}), \
                patch.object(cint_accept.subprocess, "run", side_effect=producer), \
                contextlib.redirect_stdout(io.StringIO()):
            cint_accept.main(["--set", "t27-1", "--required", str(self.required),
                              "--out", str(self.directory / "t27"), "--legs", "apple-clang"])
        self.assertEqual(len(commands), 4)
        for command in commands:
            self.assertEqual(command[command.index("--set") + 1], "t27-1")
            self.assertEqual(command[command.index("--compiler") + 1], "b1")
            self.assertNotIn("--verify-tables", command)
            self.assertIn("--emit-cache", command)
            self.assertTrue(pathlib.Path(command[command.index("--receipt") + 1]).name.startswith(
                "receipt-b1-apple-clang-"))
        for argv in (["--set", "t27-1", "--stage", "T1", "--dry-run", "--receipts", "x"],
                     ["--set", "t27-1", "--compiler", "seed", "--out", "x"],
                     ["--set", "t27-1"]):
            with self.subTest(argv=argv), contextlib.redirect_stderr(io.StringIO()), \
                    self.assertRaises(SystemExit):
                cint_accept.main(argv)

    def test_amendment_comparison_names_itself_and_allows_only_the_reviewed_fields(self):
        original, amended = self.directory / "original", self.directory / "amended"
        original.mkdir()
        amended.mkdir()
        base = json.loads((ROOT / "results/cint/t1/receipt-clang-0-portable.json").read_bytes())

        def write(directory, cfg, change=None):
            receipt = copy.deepcopy(base)
            receipt["observations"].update(leg=cfg[0], opt=int(cfg[1]), helpers=cfg[2],
                                           sanitize=cfg[3], host={"arch": "arm64", "os": "Darwin"})
            if change:
                change(receipt)
            receipt["observations"]["receipt_identity_sha256"] = cint_receipts.receipt_identity(receipt)
            (directory / ("receipt-%s.json" % cint_accept.tag(cfg))).write_bytes(
                cint_receipts.canonical(receipt) + b"\n")

        def compare():
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                code = cint_accept.main(["--amendment", "e8fbc37", "--receipts", str(amended),
                                         "--original", str(original)])
            return code, output.getvalue().splitlines()[-1]

        configs = cint_accept.AMENDMENTS["e8fbc37"]["configurations"]
        for cfg in configs:
            write(original, cfg)
        for change, passes in ((lambda r: r["identity"].update(bridge_sha256="1" * 64,
                                                               tool_sha256="2" * 64), True),
                               (lambda r: r["identity"].update(seed_sha256="3" * 64), False),
                               (lambda r: r["identity"].update(suite_sha256="4" * 64), False),
                               (lambda r: r["outcome"]["agreement"].update(agree=1), False)):
            for cfg in configs:
                write(amended, cfg, change)
            code, last = compare()
            with self.subTest(last=last):
                self.assertEqual(code, 0 if passes else 1)
                self.assertTrue(last.startswith("qualification amendment e8fbc37 (decisions 24 and 25; "
                                                "not stage acceptance): " + ("pass" if passes else "fail")))
        (amended / ("receipt-%s.json" % cint_accept.tag(configs[0]))).unlink()
        self.assertEqual(compare()[0], 1)

    def test_committed_e8fbc37_amendment_differs_only_in_bridge_and_tool(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            code = cint_accept.main(["--amendment", "e8fbc37", "--receipts",
                                     str(ROOT / "results/cint/slice2/t1-mac-e8fbc37"),
                                     "--original", str(ROOT / "results/cint/t1")])
        self.assertEqual(code, 0, output.getvalue())
        self.assertEqual(output.getvalue().splitlines()[-1],
                         "qualification amendment e8fbc37 (decisions 24 and 25; not stage acceptance): "
                         "pass, 4 receipts, receipt identity "
                         "f37921d1b05a7b687951a4be6d6e2fd0b67240aec92b80c96ba6e3d120c458b8 against original "
                         "e57af3e60c50465aa1de8fbe4959ad09e2ef2778776e5e9652cfcd6bcb471651; identity differs "
                         "in bridge_sha256, tool_sha256")


if __name__ == "__main__":
    unittest.main()
