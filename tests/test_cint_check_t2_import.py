"""Synthetic T2 import boundaries; no compiler or measurement is run."""
import contextlib
import copy
import hashlib
import io
import json
import os
import pathlib
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import cint_accept
import cint_check
import cint_receipts

def temporary_directory(prefix):
    """Uses the platform temp root unless CINT_TEST_TEMP_ROOT is supplied."""
    value = os.environ.get("CINT_TEST_TEMP_ROOT")
    parent = pathlib.Path(value) if value else None
    if parent is not None:
        parent.mkdir(parents=True, exist_ok=True)
    return tempfile.TemporaryDirectory(prefix=prefix, dir=parent)


class TemporaryEvidence(unittest.TestCase):
    def test_unset_or_empty_override_uses_the_standard_temp_directory(self):
        make_temp = globals().get("temporary_directory")
        self.assertIsNotNone(make_temp, "portable temporary directory helper is missing")
        for value in (None, ""):
            environment = dict(os.environ)
            environment.pop("CINT_TEST_TEMP_ROOT", None)
            if value is not None:
                environment["CINT_TEST_TEMP_ROOT"] = value
            with self.subTest(value=value), patch.dict(os.environ, environment, clear=True):
                with make_temp("portable-default-") as directory:
                    path = pathlib.Path(directory)
                    self.assertEqual(path.parent.resolve(), pathlib.Path(tempfile.gettempdir()).resolve())
                    (path / "marker").write_bytes(b"synthetic\n")
                self.assertFalse(path.exists())

    def test_absent_override_parent_supports_both_fixture_consumers(self):
        make_temp = globals().get("temporary_directory")
        self.assertIsNotNone(make_temp, "portable temporary directory helper is missing")
        with tempfile.TemporaryDirectory(prefix="override-regression-") as directory:
            parent = pathlib.Path(directory) / "absent" / "nested"
            self.assertFalse(parent.exists())
            with patch.dict(os.environ, {"CINT_TEST_TEMP_ROOT": str(parent)}):
                fixture = T2Import("test_all_48_rows_are_checked_with_two_identities_and_b1_stays_blocked")
                try:
                    fixture.setUp()
                    self.assertEqual(fixture.directory.parent, parent)
                    fixture.test_all_48_rows_are_checked_with_two_identities_and_b1_stays_blocked()
                finally:
                    fixture.doCleanups()
                B1BindingStub().test_source_handoff_uses_import_closure_and_existing_manifest_framing()
            self.assertEqual(list(parent.iterdir()), [])


class T2Import(unittest.TestCase):
    def setUp(self):
        self.tmp = temporary_directory("synthetic-import-")
        self.addCleanup(self.tmp.cleanup)
        self.directory = pathlib.Path(self.tmp.name)
        self.inputs = self.directory / "inputs"
        self.inputs.mkdir()
        self.required = self.directory / "required.txt"
        self.required.write_bytes(b"arith/x cint-seed compared\narith/x cintc compared\n")
        self.base = json.loads((ROOT / "results/cint/slice2/t1-gb/"
                                "receipt-msvc-0-portable.json").read_bytes())
        self.base["outcome"]["required"].update(
            file="required.txt", sha256=hashlib.sha256(self.required.read_bytes()).hexdigest())
        self.base["outcome"]["not_compared"] = {
            name: [] for name in ("held", "not_applicable", "outside_subset", "unsupported")}
        self.base["outcome"]["tables_regenerated"] = {"differ": [], "files": 1, "match": 1}
        self.current = {k: v for k, v in self.base["identity"].items()
                        if k.endswith("sha256") and not k.startswith("emitted_c")}
        self.cfgs = [(leg, opt, helpers, san) for leg in ("msvc", "gcc", "clang", "apple-clang")
                     for opt in ("0", "2") for helpers in ("portable", "builtin")
                     for san in ((False,) if leg in ("msvc", "apple-clang") else (False, True))]
        for compiler in ("seed", "b1"):
            for cfg in self.cfgs:
                self.write(compiler, cfg)

    def write(self, compiler, cfg, change=None):
        receipt = copy.deepcopy(self.base)
        receipt["identity"]["compiler"] = {"seed": "cint-seed", "b1": "cintc"}[compiler]
        leg, opt, helpers, san = cfg
        obs = receipt["observations"]
        obs.update(leg=leg, opt=int(opt), helpers=helpers, sanitize=san)
        obs["c0"].update(name=leg, flags=cint_accept.expected_c0_flags(cfg))
        obs["symbol_audit"] = ({"exempt": "EMIT-20"} if san else
                               {"outside_namespace": [],
                                "program_objects": receipt["identity"]["emitted_c_count"]})
        if leg == "apple-clang":
            obs["host"] = {"arch": "arm64", "os": "macOS 15.0"}
            obs["c0"].update(full_version="Apple clang synthetic", sdk="synthetic SDK")
        if change:
            change(receipt)
        obs["receipt_identity_sha256"] = cint_receipts.receipt_identity(receipt)
        path = self.inputs / cint_accept.receipt_name("T2", compiler, cfg)
        path.write_bytes(cint_receipts.canonical(receipt) + b"\n")
        return path

    def run_import(self, extra=()):
        self.tmp_output = tempfile.TemporaryDirectory(prefix="summary-", dir=self.directory)
        self.addCleanup(self.tmp_output.cleanup)
        out = pathlib.Path(self.tmp_output.name)
        before = {p.name: p.read_bytes() for p in self.inputs.iterdir()}
        output = io.StringIO()
        with patch.object(cint_accept, "current_identity", return_value=self.current), \
                contextlib.redirect_stdout(output):
            code = cint_accept.main(["--stage", "T2", "--dry-run", "--receipts", str(self.inputs),
                                     "--required", str(self.required), "--out", str(out), *extra])
        self.assertEqual(before, {p.name: p.read_bytes() for p in self.inputs.iterdir()})
        return code, json.loads((out / "acceptance.json").read_bytes()), output.getvalue()

    def test_all_48_rows_are_checked_with_two_identities_and_b1_stays_blocked(self):
        code, summary, output = self.run_import()
        self.assertEqual(code, 1)
        self.assertEqual((summary["complete_receipts"], len(summary["receipts"])), (48, 48))
        self.assertEqual(sum("apple-clang" in row["file"] for row in summary["receipts"]), 8)
        self.assertEqual({k: len(v) for k, v in summary["receipt_identities"].items()},
                         {"seed": 1, "b1": 1})
        self.assertTrue(all(row["pass"] for row in summary["receipts"] if row["compiler"] == "seed"))
        self.assertTrue(all(any("B1 compiler-source" in p for p in row["problems"])
                            for row in summary["receipts"] if row["compiler"] == "b1"))
        self.assertFalse(summary["complete"])
        self.assertIn("missing K, phase-fuel, and fuzz", output)

    def test_missing_each_of_48_configurations_is_named(self):
        for compiler in ("seed", "b1"):
            for cfg in self.cfgs:
                path = self.inputs / cint_accept.receipt_name("T2", compiler, cfg)
                saved = path.read_bytes()
                path.unlink()
                try:
                    with self.subTest(compiler=compiler, cfg=cfg):
                        code, summary, _ = self.run_import()
                        row = next(r for r in summary["receipts"] if r["file"] == path.name)
                        self.assertEqual(code, 1)
                        self.assertIsNone(row["identity"])
                        self.assertIn("missing or malformed evidence", " ".join(row["problems"]))
                finally:
                    path.write_bytes(saved)

    def test_duplicate_legacy_seed_alias_fails_even_when_both_receipts_are_identical(self):
        path = self.inputs / "receipt-seed-msvc-0-portable.json"
        (self.inputs / "receipt-msvc-0-portable.json").write_bytes(path.read_bytes())
        _, summary, _ = self.run_import()
        self.assertTrue(any("duplicate" in problem for problem in summary["problems"]))

    def test_unrecognized_receipt_cannot_be_silently_ignored(self):
        (self.inputs / "receipt-seed-apple-clang-0-portable-san.json").write_bytes(b"{}\n")
        _, summary, _ = self.run_import()
        self.assertTrue(any("unrequested" in problem for problem in summary["problems"]))

    def test_mutations_are_checked_on_both_compilers(self):
        changes = [
            ("wrong configuration", lambda r: r["observations"].update(opt=2)),
            ("wrong compiler", lambda r: r["identity"].update(compiler="wrong")),
            ("stale or missing identity.seed_sha256", lambda r: r["identity"].update(seed_sha256="0" * 64)),
            ("required-case list digest differs", lambda r: r["outcome"]["required"].update(sha256="0" * 64)),
            ("required-case list path differs", lambda r: r["outcome"]["required"].update(file="wrong.txt")),
            ("agreement counts", lambda r: r["outcome"]["agreement"].update(disagree=1)),
            ("fewer compared", lambda r: r["outcome"]["agreement"].update(agree=1, compared=1)),
            ("expected full scope", lambda r: r["identity"].update(scope="quick")),
            ("malformed", lambda r: r["observations"].update(c0=[])),
        ]
        self.required.write_bytes(b"arith/x cint-seed compared\narith/y cint-seed compared\n"
                                  b"arith/x cintc compared\narith/y cintc compared\n")
        self.base["outcome"]["required"]["sha256"] = hashlib.sha256(self.required.read_bytes()).hexdigest()
        for compiler in ("seed", "b1"):
            for message, change in changes:
                with self.subTest(compiler=compiler, mutation=message):
                    path = self.write(compiler, self.cfgs[0], change)
                    row = cint_accept.check_conformance(path, self.cfgs[0], compiler, self.required,
                                                        cint_check.read_required(self.required), self.current)
                    self.assertFalse(row["pass"])
                    self.assertIn(message, " ".join(row["problems"]))

    def test_mixed_identity_is_reported_per_compiler(self):
        self.write("seed", self.cfgs[0], lambda r: r["identity"].update(seed_sha256="0" * 64))
        _, summary, _ = self.run_import()
        self.assertEqual(len(summary["receipt_identities"]["seed"]), 2)
        self.assertEqual(len(summary["receipt_identities"]["b1"]), 1)
        self.assertIn("seed: expected one receipt identity, found 2", summary["problems"])

    def test_partial_import_remains_provisional(self):
        _, summary, output = self.run_import(["--compiler", "seed", "--legs", "apple-clang",
                                             "--skip-measurements"])
        self.assertEqual((summary["scope"], summary["complete"], len(summary["receipts"])),
                         ("provisional", False, 4))
        self.assertIn("provisional", output.splitlines()[-1])


class B1BindingStub(unittest.TestCase):
    def test_source_handoff_uses_import_closure_and_existing_manifest_framing(self):
        with temporary_directory("binding-") as directory:
            root = pathlib.Path(directory)
            for name, data in {"compiler/main.ci": b"import util;\n",
                               "compiler/util.ci": b"// retained\n",
                               "compiler/unused.ci": b"// excluded\n",
                               "seed/a.c": b"seed\n", "rt/r.c": b"runtime\n",
                               "cli/c.c": b"cli\n", "compiler/tests/golden_host.c": b"host\n"}.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            read_sources = getattr(cint_accept, "b1_binding_inputs", None)
            self.assertIsNotNone(read_sources, "expected B1 source handoff is missing")
            actual = read_sources(root)
            manifest = b"CISRC001" + struct.pack("<I", 2)
            for name, data in ((b"compiler/main.ci", b"import util;\n"),
                               (b"compiler/util.ci", b"// retained\n")):
                manifest += struct.pack("<H", len(name)) + name + struct.pack("<Q", len(data)) \
                    + hashlib.sha256(data).digest()
            self.assertEqual(actual["compiler_source_identity"], hashlib.sha256(manifest).hexdigest())
            self.assertEqual([r["path"] for r in actual["compiler_sources"]],
                             ["compiler/main.ci", "compiler/util.ci"])
            (root / "compiler/util.ci").write_bytes(b"// changed\n")
            self.assertNotEqual(read_sources(root)["compiler_source_identity"], actual["compiler_source_identity"])
            self.assertEqual(actual["build_inputs"]["host"][0]["sha256"], hashlib.sha256(b"host\n").hexdigest())

    def test_binding_requires_current_sources_and_agreeing_provenance(self):
        check = cint_accept.check_b1_binding
        sources = cint_accept.b1_binding_inputs()
        receipt = {"identity": {"schema": "cint-conformance-receipt-2", "compiler": "cintc",
                                "compiler_source_identity": sources["compiler_source_identity"],
                                "compiler_source_method": cint_accept.B1_SOURCE_METHOD,
                                "emitter": "cint emit-c",
                                "cli_sha256": cint_accept.records_digest(sources["build_inputs"]["cli"])},
                   "observations": {"b1": {"cint_sha256": "2" * 64, "how": "given"}}}
        self.assertEqual(check(receipt, sources), [])
        self.assertTrue(any("B1 compiler-source" in p for p in check(receipt, None)))
        self.assertTrue(any("B1 compiler-source" in p for p in
                            check({"identity": {"compiler": "cintc"}, "observations": {}},
                                  {"compiler_source_identity": "0" * 64})))
        agreeing = {"identity": {"compiler_source_identity": sources["compiler_source_identity"]}}
        self.assertEqual(check(receipt, sources, agreeing), [])
        other = {"identity": {"compiler_source_identity": "0" * 64}}
        self.assertTrue(any("bootstrap receipt" in p for p in check(receipt, sources, other)))


if __name__ == "__main__":
    unittest.main()
