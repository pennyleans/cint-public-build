"""task 2.12 run/channel/status/receipt checks; run on each leg after bootstrap."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

import parity

ROOT = pathlib.Path(__file__).resolve().parents[2]
CONFIG = None


def canonical(value):
    return json.dumps(value, sort_keys=True, ensure_ascii=True,
                      separators=(",", ":")).encode("ascii")


def sha(data):
    return hashlib.sha256(data).hexdigest()


def sir17_revision(path, sir):
    """SPEC-09 SIR-17 and SPEC-01 IM-157, assembled here from the canonical SIR text of a
    one-module program (the compiler's `.sites` output): the `sites` line and each
    ` @<path>:<line>:<column>` removed, then the domain string, profile, runtime contract
    version, module count, path and encoded SIR, hashed with SHA-256."""
    lines = sir.split(b"\n")
    if len(lines) < 2 or not lines[1].startswith(b"sites "):
        raise ValueError("no sites line")
    del lines[1]
    encoded = b"\n".join(re.sub(rb"(#[0-9]+) @[^ ]+:[0-9]+:[0-9]+$", rb"\1", line) for line in lines)
    text = lambda value: struct.pack("<I", len(value)) + value
    data = (text(b"cint-core-1/revision/v1") + text(b"cint-core-1") + text(b"cint-rt-3") + struct.pack("<I", 1) +
            text(path.encode("utf-8")) + struct.pack("<Q", len(encoded)) + encoded)
    return hashlib.sha256(data).hexdigest()


def reference_fuel(root, relative):
    """The `fuel-consumed` line of `python -m cint_ref run` for the module, as a string."""
    result = subprocess.run([sys.executable, "-m", "cint_ref", "run", "--path", relative, str(root / relative)],
                            cwd=ROOT / "ref", env=dict(os.environ, PYTHONPATH=str(ROOT / "ref")),
                            capture_output=True, check=False, timeout=120)
    for line in result.stdout.decode("ascii", "replace").splitlines():
        if line.startswith("fuel-consumed "):
            return line.split(" ", 1)[1]
    raise RuntimeError("cint_ref gave no fuel-consumed line for %s: %r" % (relative, result.stderr[-400:]))


def toolchain(path):
    data = path.read_bytes()
    if not data.startswith(b"cint-toolchain-1\n"):
        raise ValueError("toolchain must start with its 16 byte LF header")
    rows = [line.split(" ", 1) for line in data.decode("utf-8").splitlines()[1:]
            if " " in line]
    return rows


class RunTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.exe = CONFIG.exe.resolve()
        cls.out = CONFIG.run_out
        cls.rows = toolchain(cls.exe.with_name("cint.toolchain"))
        cls.values = dict(cls.rows)
        cls.env = os.environ.copy()
        for key, value in cls.rows:
            if key == "env":
                name, text = value.split("=", 1)
                cls.env[name] = text
        cls.serial = 0
        helper = cls.out / ("fake host.exe" if os.name == "nt" else "fake host")
        flags = [value for key, value in cls.rows if key == "flag"]
        command = [cls.values["cc"], *flags,
                   str(ROOT / "cli/tests/fixtures/fake_host.c")]
        if CONFIG.leg == "msvc":
            command += ["/Fe" + str(helper), "/Fo" + str(cls.out / "fake_host.obj")]
        else:
            command += ["-o", str(helper)]
        build = subprocess.run(command, cwd=cls.out, env=cls.env, capture_output=True,
                               check=False, timeout=60)
        (cls.out / "fixture-build.stdout").write_bytes(build.stdout)
        (cls.out / "fixture-build.stderr").write_bytes(build.stderr)
        if build.returncode != 0:
            raise RuntimeError("fake host fixture build failed: " +
                               (build.stdout + build.stderr).decode("utf-8", "replace"))
        cls.helper = helper
        unit = cls.out / ("cli_unit.exe" if os.name == "nt" else "cli_unit")
        build_dir = pathlib.Path(cls.values["runtime_object"]).parent.parent
        suffix = ".obj" if CONFIG.leg == "msvc" else ".o"
        objects = [build_dir / "s1" / ("main" + suffix)] + [
            build_dir / "rt" / (name + suffix) for name in ("cint_rt", "cint_bridge", "cint_build")]
        macro = ('/D' if CONFIG.leg == "msvc" else '-D') + 'CINT_COMPILER_SOURCE_IDENTITY="' + sha(
            pathlib.Path(cls.values["compiler_sources"]).read_bytes()) + '"'
        include = ("/I" if CONFIG.leg == "msvc" else "-I") + cls.values["include"]
        proc = "cint_proc_win.c" if CONFIG.leg == "msvc" else "cint_proc_posix.c"
        command = [cls.values["cc"], *flags, macro, include,
                   str(ROOT / "cli/tests/fixtures/cli_unit.c"), str(ROOT / "cli" / proc),
                   *map(str, objects)]
        if CONFIG.leg == "msvc":
            command += ["/Fe" + str(unit), "/Fo" + str(cls.out) + os.sep]
        else:
            command += ["-o", str(unit)]
        build = subprocess.run(command, cwd=cls.out, env=cls.env, capture_output=True,
                               check=False, timeout=90)
        (cls.out / "unit-build.stdout").write_bytes(build.stdout)
        (cls.out / "unit-build.stderr").write_bytes(build.stderr)
        if build.returncode != 0:
            raise RuntimeError("cli unit fixture build failed: " +
                               (build.stdout + build.stderr).decode("utf-8", "replace"))
        cls.unit = unit

    def invoke(self, args, *, tc=None, env=None, input_bytes=None, cache=None):
        type(self).serial += 1
        name = "%03d-%s" % (self.serial, self._testMethodName)
        options = ["--cache", str(cache or self.out / "cache")]
        if tc is not None:
            options += ["--toolchain", str(tc)]
        command = [str(self.exe), "run", *options, *map(str, args)]
        result = subprocess.run(command, cwd=ROOT, env=env or self.env,
                                input=input_bytes, capture_output=True, check=False,
                                timeout=120)
        (self.out / (name + ".stdout")).write_bytes(result.stdout)
        (self.out / (name + ".stderr")).write_bytes(result.stderr)
        (self.out / (name + ".json")).write_bytes(canonical({
            "command": command, "exit_status": result.returncode,
            "stdout_sha256": sha(result.stdout), "stderr_sha256": sha(result.stderr)}) + b"\n")
        return result

    def clone_toolchain(self, cc, name="custom.toolchain", overrides=None):
        path = self.out / name
        changes = {"cc": str(cc), **(overrides or {})}
        rows = [(key, str(changes.get(key, value))) for key, value in self.rows]
        path.write_bytes(("cint-toolchain-1\n" + "".join(
            "%s %s\n" % row for row in rows)).encode("utf-8"))
        return path

    def one_json(self, result, schema, status):
        self.assertEqual(result.returncode, status, result.stderr)
        lines = result.stderr.splitlines()
        self.assertEqual(len(lines), 1, result.stderr)
        record = json.loads(lines[0])
        self.assertEqual(record["schema"], schema)
        self.assertNotIn(b"receipt:", result.stderr)
        return record

    def tool_error(self, result, status):
        record = self.one_json(result, "CINT-TOOL-1", status)
        self.assertEqual(record["exit_status"], status)
        self.assertEqual(record["category"], {4: "usage", 5: "environment", 7: "internal"}[status])
        self.assertIsInstance(record["message"], str)
        self.assertTrue(record["message"])
        return record

    def check_receipt(self, path, stdout, status, source, module, fuel="1"):
        raw = path.read_bytes()
        receipt = json.loads(raw)
        self.assertEqual(raw, canonical(receipt) + b"\n")
        identity = receipt["identity"]
        outcome = receipt["outcome"]
        self.assertEqual(identity["schema"], "cint-receipt-1/run")
        self.assertEqual(identity["kind"], "run")
        self.assertEqual(identity["identities"], {"build": None, "execution": None,
                         "meaning": None, "revision": None, "source_map": None})
        self.assertEqual(identity["source"]["files"][module], sha(source.read_bytes()))
        self.assertEqual(identity["method"], {"depth_limit": 256, "frame_arena_bytes": 16777216,
                         "fuel_budget": "none", "fuel_model": "fuel-v1", "stdin": "null"})
        self.assertEqual(outcome["exit_status"], status)
        self.assertEqual(outcome["fuel_consumed"], fuel)
        self.assertEqual(outcome["stdout"], {"bytes": len(stdout), "sha256": sha(stdout)})
        self.assertEqual(receipt["observations"]["receipt_identity_sha256"],
                         sha(canonical({"identity": identity, "outcome": outcome})))
        for key, value in self.rows:
            if key == "runtime_source":
                path_ = pathlib.Path(value)
                self.assertEqual(identity["runtime"]["files"][path_.name], sha(path_.read_bytes()))
        archive = pathlib.Path(self.values["compiler_sources"]).read_bytes()
        self.assertEqual(identity["tool"]["compiler_source_identity"], sha(archive))
        self.assertTrue(archive.startswith(b"CISRC001"))
        at = 8
        count, = struct.unpack_from("<I", archive, at)
        at += 4
        paths = []
        for _ in range(count):
            size, = struct.unpack_from("<H", archive, at)
            at += 2
            relative = archive[at:at + size].decode("utf-8")
            at += size
            content_size, = struct.unpack_from("<Q", archive, at)
            at += 8
            digest = archive[at:at + 32]
            at += 32
            content = (ROOT / relative).read_bytes()
            self.assertEqual(content_size, len(content))
            self.assertEqual(digest, hashlib.sha256(content).digest())
            paths.append(relative)
        self.assertEqual(at, len(archive))
        self.assertEqual(paths, sorted(set(paths), key=lambda value: value.encode("utf-8")))
        self.assertIn("compiler/main.ci", paths)
        generated = identity["generated"]
        if generated is not None:
            cachedir = pathlib.Path(receipt["observations"]["cache"])
            reference = (cachedir / "out/MANIFEST.ref").read_text().split(" ", 1)[0]
            stage = cachedir / reference
            manifest = stage / "MANIFEST"
            self.assertEqual(generated["manifest"], sha(manifest.read_bytes()))
            for relative, digest in generated["files"].items():
                self.assertEqual(digest, sha((stage / relative).read_bytes()))
        executables = receipt["observations"]["executables"]
        self.assertEqual(executables["cint"], sha(self.exe.read_bytes()))
        self.assertEqual(executables["runtime"], sha(pathlib.Path(self.values["runtime_object"]).read_bytes()))
        if status in (0, 1):
            program = pathlib.Path(receipt["observations"]["cache"]) / "bin" / (
                "program.exe" if CONFIG.leg == "msvc" else "program")
            self.assertEqual(executables["program"], sha(program.read_bytes()))
        return receipt

    def test_hello_no_implicit_receipt(self):
        cwd = self.out / "no implicit receipt"
        cwd.mkdir(exist_ok=True)
        before = set(cwd.rglob("*"))
        result = subprocess.run([str(self.exe), "run", "--cache", str(self.out / "cache"),
                                 str(ROOT / "examples/hello.ci")], cwd=cwd, env=self.env,
                                capture_output=True, check=False, timeout=120)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, (ROOT / "examples/hello.stdout").read_bytes())
        self.assertEqual(len(result.stdout), 23)
        self.assertEqual(result.stderr, b"")
        self.assertEqual(set(cwd.rglob("*")), before)

    def test_hello_human_receipt(self):
        receipt = self.out / "hello human.json"
        result = self.invoke(["--receipt", receipt, "examples/hello.ci"])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, (ROOT / "examples/hello.stdout").read_bytes())
        self.assertEqual(result.stderr, ("receipt: %s\n" % receipt).encode("utf-8"))
        self.check_receipt(receipt, result.stdout, 0, ROOT / "examples/hello.ci", "hello.ci")

    def test_hello_json_receipt(self):
        receipt = self.out / "hello json.json"
        result = self.invoke(["--json", "--receipt", receipt, "examples/hello.ci"])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, (ROOT / "examples/hello.stdout").read_bytes())
        self.assertEqual(result.stderr, b"")
        self.check_receipt(receipt, result.stdout, 0, ROOT / "examples/hello.ci", "hello.ci")

    def git_command(self, directory, *arguments):
        executable = shutil.which("git", path=self.env.get("PATH"))
        if executable is None:
            self.skipTest("git is unavailable for repository metadata fixtures")
        type(self).serial += 1
        name = "%03d-%s-git" % (self.serial, self._testMethodName)
        command = [str(pathlib.Path(executable).resolve()), "-C", str(directory), *arguments]
        result = subprocess.run(command, env=self.env, capture_output=True, check=False, timeout=30)
        (self.out / (name + ".stdout")).write_bytes(result.stdout)
        (self.out / (name + ".stderr")).write_bytes(result.stderr)
        (self.out / (name + ".json")).write_bytes(canonical({"command": command,
                                                            "exit_status": result.returncode}) + b"\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout.strip()

    def source_repository(self):
        directory = self.out / (self._testMethodName + "-repo")
        directory.mkdir()
        source = directory / "hello.ci"
        source.write_bytes((ROOT / "examples/hello.ci").read_bytes())
        self.git_command(directory, "init", "-q")
        self.git_command(directory, "config", "core.autocrlf", "false")
        self.git_command(directory, "add", "-f", "hello.ci")
        self.git_command(directory, "-c", "user.name=cli test", "-c", "user.email=cli-test@example.invalid",
                         "-c", "commit.gpgsign=false", "commit", "-qm", "source fixture")
        revision = self.git_command(directory, "rev-parse", "--verify", "HEAD").decode("ascii")
        return directory, source, revision

    def test_receipt_source_git_clean_dirty_untracked(self):
        directory, source, revision = self.source_repository()
        original = source.read_bytes()
        # dirty covers the compiled source only (D-19 as amended by the M1 review): an unrelated
        # untracked file, such as a receipt written into the tree, leaves it false.
        for state in ("clean", "unrelated", "dirty", "untracked"):
            with self.subTest(state=state):
                if state == "unrelated":
                    (directory / "run-receipt.json").write_bytes(b"{}\n")
                elif state == "dirty":
                    source.write_bytes(original + b"\n")
                elif state == "untracked":
                    source.write_bytes(original)
                    source = directory / "untracked.ci"
                    source.write_bytes(original)
                dirty = bool(self.git_command(directory, "status", "--porcelain=v1", "--untracked-files=normal",
                                              "--", source.name))
                self.assertEqual(dirty, state not in ("clean", "unrelated"))
                receipt = self.out / ("source-%s.json" % state)
                result = self.invoke(["--json", "--receipt", receipt, "--root", directory, source.name])
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, (ROOT / "examples/hello.stdout").read_bytes())
                self.assertEqual(result.stderr, b"")
                record = self.check_receipt(receipt, result.stdout, 0, source, source.name)
                self.assertEqual(record["identity"]["source"]["revision"], revision)
                self.assertEqual(record["identity"]["source"]["dirty"], dirty)

    def test_receipt_source_outside_git(self):
        directory = self.out / "outside_git"
        directory.mkdir()
        source = directory / "hello.ci"
        source.write_bytes((ROOT / "examples/hello.ci").read_bytes())
        receipt = self.out / "outside-git.json"
        result = self.invoke(["--json", "--receipt", receipt, "--root", directory, source.name])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, (ROOT / "examples/hello.stdout").read_bytes())
        self.assertEqual(result.stderr, b"")
        record = self.check_receipt(receipt, result.stdout, 0, source, source.name)
        self.assertIsNone(record["identity"]["source"]["revision"])
        self.assertIsNone(record["identity"]["source"]["dirty"])

    def test_receipt_source_missing_git_continues(self):
        directory, source, _ = self.source_repository()
        missing_path = self.out / "empty_path"
        missing_path.mkdir()
        tc = self.clone_toolchain(self.helper, "missing_git.toolchain")
        with tc.open("ab") as stream:
            stream.write(("env PATH=%s\n" % missing_path).encode("utf-8"))
        receipt = self.out / "missing-git.json"
        env = dict(self.env, CINT_TEST_MODE="stdin", PATH=str(missing_path))
        result = self.invoke(["--json", "--receipt", receipt, "--root", directory, source.name],
                             tc=tc, env=env)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, b"stdin:null\n")
        self.assertEqual(result.stderr, b"")
        record = self.check_receipt(receipt, result.stdout, 0, source, source.name, fuel="0")
        self.assertIsNone(record["identity"]["source"]["revision"])
        self.assertIsNone(record["identity"]["source"]["dirty"])

    def generated_sir(self, receipt):
        """The module's canonical SIR text (`<P>.sites`) of the run's committed output set."""
        record = json.loads(receipt.read_bytes())
        cachedir = pathlib.Path(record["observations"]["cache"])
        stage = cachedir / (cachedir / "out/MANIFEST.ref").read_text().split(" ", 1)[0]
        names = [name for name in record["identity"]["generated"]["files"]
                 if name.endswith(".sites") and name != "cint-program.sites"]
        self.assertEqual(len(names), 1, names)
        return (stage / names[0]).read_bytes()

    def frozen_fault(self, relative, stdout):
        source = ROOT / "conformance" / relative
        fields = {}
        operands = []
        for line in source.with_suffix(".expect").read_text().splitlines():
            key, _, value = line.partition(" ")
            if key == "fault.operand":
                tag, number = value.split(" ", 1)
                operands.append({"t": tag, "v": number})
            else:
                fields[key] = value
        self.assertEqual(len(stdout), int(fields["stdout-bytes"]))
        self.assertEqual(sha(stdout), fields["stdout-sha256"])
        receipt = self.out / (source.stem + ".json")
        result = self.invoke(["--json", "--root", ROOT / "conformance", "--receipt", receipt, relative])
        self.assertEqual(result.stdout, stdout)
        fault = self.one_json(result, "CINT-FAULT-1", 1)
        self.assertEqual(fault.pop("noncanonical", None), {"fuel_consumed": fields["fuel-consumed"]})
        path, line, column = fields["fault.position"].rsplit(":", 2)
        expected = {"code": fields["fault.code"], "operation": fields["fault.operation"],
                    "file": path, "line": int(line), "column": int(column), "operands": operands,
                    "exact": {"t": "Z", "v": fields["fault.exact"]},
                    "limit": dict(zip(("t", "v"), fields["fault.limit"].split(" ", 1))),
                    "address": None, "stack": [], "revision": fault["revision"],
                    "schema": "CINT-FAULT-1", "severity": "fault", "source_map": None}
        self.assertEqual(fault, expected)
        record = self.check_receipt(receipt, stdout, 1, source, relative, fuel=fields["fuel-consumed"])
        self.assertEqual(record["outcome"]["fault"], fault)
        self.assertEqual(fault["revision"], sir17_revision(relative, self.generated_sir(receipt)))
        human = self.invoke(["--root", ROOT / "conformance", relative])
        self.assertEqual(human.returncode, 1, human.stderr)
        self.assertEqual(human.stdout, stdout)
        self.assertIn(b"fault[E_OVERFLOW]", human.stderr)
        self.assertIn(fields["fault.position"].encode(), human.stderr)
        for operand in operands:
            self.assertIn(("operand: %s %s" % (operand["t"], operand["v"])).encode(), human.stderr)
        self.assertIn(("exact:   %s" % fields["fault.exact"]).encode(), human.stderr)
        self.assertIn(("limit:   %s" % fields["fault.limit"]).encode(), human.stderr)
        self.assertNotIn(b"receipt:", human.stderr)
        self.assertIn(("= revision: %s" % fault["revision"]).encode(), human.stderr)
        self.assertIn(("= fuel-consumed: %s " % fields["fuel-consumed"]).encode(), human.stderr)

    def test_print_then_fault(self):
        self.frozen_fault("control/print_no_newline_then_fault.ci", b"partial: big=9223372036854775807")

    def test_hole_fault_partial(self):
        self.frozen_fault("control/print_hole_fault_partial.ci", b"start ")

    def test_example_overflow(self):
        result = self.invoke(["--json", "examples/hello_overflow.ci"])
        self.assertEqual(result.stdout, b"counting past the largest I64\nbig=9223372036854775807\n")
        fault = self.one_json(result, "CINT-FAULT-1", 1)
        self.assertEqual(fault["noncanonical"], {"fuel_consumed": reference_fuel(ROOT, "examples/hello_overflow.ci")})
        self.assertEqual((fault["code"], fault["operation"], fault["line"], fault["column"]),
                         ("E_OVERFLOW", "add.checked.i64", 4, 16))
        self.assertEqual(fault["exact"], {"t": "Z", "v": "9223372036854775808"})

    def test_usage_json(self):
        for arguments in (["--json"], ["--json", "--bogus"],
                          ["--json", "--receipt"], ["--json", "a.ci", "b.ci"]):
            with self.subTest(arguments=arguments):
                result = self.invoke(arguments)
                self.assertEqual(result.stdout, b"")
                self.tool_error(result, 4)

    def test_environment_escaped_path(self):
        hostile = 'missing "quote"\\tab\tline\n.ci'
        result = self.invoke(["--json", "--root", self.out, hostile])
        self.assertEqual(result.stdout, b"")
        record = self.tool_error(result, 5)
        self.assertIn("missing", record["message"])
        self.assertIn(b"\\\"", result.stderr)
        self.assertIn(b"\\t", result.stderr)
        self.assertIn(b"\\n", result.stderr)

    def test_missing_host_compiler(self):
        tc = self.clone_toolchain(self.out / "missing compiler.exe")
        result = self.invoke(["--json", "examples/hello.ci"], tc=tc)
        self.assertEqual(result.stdout, b"")
        record = self.tool_error(result, 5)
        self.assertEqual(record["tool"], str(self.out / "missing compiler.exe"))

    def test_toolchain_archive_mismatch_is_environment_error(self):
        archive = self.out / "tampered.cisrc"
        data = bytearray(pathlib.Path(self.values["compiler_sources"]).read_bytes())
        data[-1] ^= 1
        archive.write_bytes(data)
        tc = self.clone_toolchain(self.values["cc"], "tampered.toolchain",
                                  {"compiler_sources": archive})
        result = self.invoke(["--json", "examples/hello.ci"], tc=tc)
        self.assertEqual(result.stdout, b"")
        self.tool_error(result, 5)

    def test_toolchain_executable_mismatch_is_environment_error(self):
        tc = self.clone_toolchain(self.values["cc"], "executable_bad.toolchain",
                                  {"executable_sha256": "0" * 64})
        result = self.invoke(["--json", "examples/hello.ci"], tc=tc)
        self.assertEqual(result.stdout, b"")
        self.tool_error(result, 5)

    def test_receipt_destination_is_environment_error(self):
        directory = self.out / "receipt_directory"
        directory.mkdir(exist_ok=True)
        result = self.invoke(["--json", "--receipt", directory, "examples/hello.ci"])
        self.assertEqual(result.stdout, (ROOT / "examples/hello.stdout").read_bytes())
        self.tool_error(result, 5)

    def test_bridge_rejects_parent_path(self):
        result = self.invoke(["--json", "--root", ROOT / "examples", "../examples/hello.ci"])
        self.assertEqual(result.stdout, b"")
        self.tool_error(result, 5)

    def test_output_not_writable(self):
        blocker = self.out / "cache is file"
        blocker.write_bytes(b"x")
        result = self.invoke(["--json", "examples/hello.ci"], cache=blocker)
        self.assertEqual(result.stdout, b"")
        self.tool_error(result, 5)

    def test_compile_diagnostic_channels(self):
        source = self.out / "invalid.ci"
        source.write_bytes(b"I64 x = ;\n")
        result = self.invoke(["--json", source])
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(result.stdout, b"")
        records = [json.loads(line) for line in result.stderr.splitlines()]
        self.assertTrue(records)
        for record in records:
            self.assertEqual(record["schema"], "CINT-DIAG-1")
            self.assertRegex(record["code"], r"^C\d{4}$")
            self.assertGreater(record["line"], 0)
            self.assertGreater(record["column"], 0)
        self.assertEqual(records, sorted(records, key=lambda r: (r["file"].encode(), r["line"], r["column"], r["code"])))
        human = self.invoke([source])
        self.assertEqual(human.returncode, 2, human.stderr)
        self.assertEqual(human.stdout, b"")
        self.assertIn(records[0]["code"].encode(), human.stderr)

    def test_compiler_out_of_range_literal_is_c2003(self):
        source = self.out / "literal_bad.ci"
        source.write_bytes(b'I8 x = 256;\n"x={x}\\n";\n')
        result = self.invoke(["--json", source])
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(result.stdout, b"")
        records = [json.loads(line) for line in result.stderr.splitlines()]
        self.assertTrue(any(r.get("code") == "C2003" for r in records), result.stderr)

    def test_compiler_script_note_only_on_module_initializer(self):
        # SPEC-04 LS-312: of the C6004 diagnostics, only a module-level initializer's carries
        # the script-mode note (G-C2 review COR-9).
        module_note = "this file is a module, not a script, because every top-level item is a declaration (LS-218)"
        cases = ((b'I64 f() {\n    return 1;\n}\nI64 g = f();\n\nvoid main() {\n    "{g}\\n";\n}\n', 4, 9,
                  [{"text": module_note}]),
                 (b"void main() {\n    I64 x = 1;\n    static_assert(x == 1);\n}\n", 3, 19, None),
                 (b"const I64 K = 2;\nvoid main() {\n    I64 x = 1;\n    const I64 y = x + K;\n}\n", 4, 19, None))
        for k, (text, line, column, notes) in enumerate(cases):
            with self.subTest(k=k):
                source = self.out / ("script_note_%d.ci" % k)
                source.write_bytes(text)
                result = self.invoke(["--json", source])
                self.assertEqual(result.returncode, 2, result.stderr)
                records = [json.loads(line_) for line_ in result.stderr.splitlines()]
                self.assertEqual([(r["code"], r["line"], r["column"], r.get("notes")) for r in records],
                                 [("C6004", line, column, notes)], result.stderr)

    def test_compiler_type_mismatch_is_c2001(self):
        source = self.out / "type_bad.ci"
        source.write_bytes(b'I8 marker = 1;\nI64 x = true;\n"{x}\\n";\n')
        result = self.invoke(["--json", source])
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(result.stdout, b"")
        records = [json.loads(line) for line in result.stderr.splitlines()]
        self.assertTrue(any(r.get("code") == "C2001" for r in records), result.stderr)

    def test_compiler_import_of_no_module(self):
        # C3009 names the import token; C3030 names the rejected candidate's 1:1.
        cases = ((b"import gone.away;\n", "C3009", "imports_fold.ci", 1, 8),
                 (b"// x\nimport lib.aux;\n", "C3030", "lib/aux.ci", 1, 1),
                 (b"import Imports_fold;\n", "C3030", "Imports_fold.ci", 1, 1))
        for k, (text, code, path, line, column) in enumerate(cases):
            with self.subTest(code=code, k=k):
                source = self.out / "imports_fold.ci"
                source.write_bytes(text + b"export I64 run() {\n    return 1;\n}\n")
                result = self.invoke(["--json", source])
                self.assertEqual(result.returncode, 2, result.stderr)
                records = [json.loads(line) for line in result.stderr.splitlines()]
                self.assertEqual([(r["code"], r["file"], r["line"], r["column"]) for r in records],
                                 [(code, path, line, column)], result.stderr)

    def test_compiler_case_fold_uses_loaded_paths(self):
        # CINTC-12 and OQ-178: a reached target joins the path set before its own imports.
        project = self.out / "loaded-paths"
        (project / "lib").mkdir(parents=True)
        (project / "lib" / "math.ci").write_bytes(b"export const I64 SCALE = 1;\n")
        (project / "lib" / "dep.ci").write_bytes(b"import lib.math;\nexport I64 value() { return math.SCALE; }\n")
        cases = ((b"import lib.dep;\nimport lib.Math;\n", "C3009", "main.ci", 2, 8),
                 (b"import lib.math;\nimport lib.Math;\n", "C3030", "lib/Math.ci", 1, 1),
                 (b"import lib.dep;\nimport lib.math;\nimport lib.Math;\n", "C3030", "lib/Math.ci", 1, 1),
                 (b"import Main;\n", "C3030", "Main.ci", 1, 1))
        for k, (imports, code, path, line, column) in enumerate(cases):
            with self.subTest(k=k):
                source = project / "main.ci"
                source.write_bytes(imports + b"export I64 run() { return 0; }\n")
                ref = subprocess.run([sys.executable, "-m", "cint_ref", "run", str(source),
                                      "--entry", "run", "--path", "main.ci"], cwd=ROOT / "ref",
                                     env=dict(os.environ, PYTHONPATH=str(ROOT / "ref")),
                                     capture_output=True, check=False, timeout=120)
                self.assertEqual(ref.returncode, 0, ref.stderr)
                fields = dict(v.split(" ", 1) for v in ref.stdout.decode("ascii").splitlines() if " " in v)
                self.assertEqual((fields["diagnostic.code"], fields["diagnostic.position"]),
                                 (code, "%s:%d:%d" % (path, line, column)))
                out = self.out / ("loaded-paths-%d" % k)
                result = subprocess.run([str(self.exe), "--json", "emit-c", "--root", str(project),
                                         "--out", str(out), "lib/math.ci", "lib/dep.ci", "main.ci"],
                                        cwd=self.out, env=self.env, capture_output=True, check=False, timeout=120)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertEqual(result.stdout, b"")
                records = [json.loads(v) for v in result.stderr.splitlines()]
                self.assertEqual([(r["code"], r["file"], r["line"], r["column"]) for r in records],
                                 [(code, path, line, column)], result.stderr)

    def test_compiler_unreached_import_path_keeps_manifest_validation(self):
        # Explicit inputs outside the root's import graph keep their existing validation.
        project = self.out / "unreached-paths"
        (project / "lib").mkdir(parents=True)
        (project / "lib" / "math.ci").write_bytes(b"export const I64 SCALE = 1;\n")
        (project / "lib" / "bad.ci").write_bytes(b"import lib.Math;\n")
        (project / "main.ci").write_bytes(b"export I64 run() { return 0; }\n")
        result = subprocess.run([str(self.exe), "--json", "emit-c", "--root", str(project),
                                 "--out", str(self.out / "unreached-paths-out"),
                                 "lib/math.ci", "lib/bad.ci", "main.ci"],
                                cwd=self.out, env=self.env, capture_output=True, check=False, timeout=120)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(result.stdout, b"")
        records = [json.loads(v) for v in result.stderr.splitlines()]
        self.assertEqual([(r["code"], r["file"], r["line"], r["column"]) for r in records],
                         [("C3030", "lib/Math.ci", 1, 1)], result.stderr)

    def test_compiler_import_diagnostic_candidate_paths(self):
        # CINTC-12 diagnoses the candidate filename, including paths absent from the manifest.
        project = self.out / "candidate-paths"
        (project / "lib").mkdir(parents=True)
        (project / "lib" / "math.ci").write_bytes(b"export const I64 SCALE = 1;\n")
        device_rule = "a segment of a module path is not a Windows device name (SPEC-09 CINTC-12)"
        collision_rule = "two module paths of one program are equal under ASCII case folding (SPEC-09 CINTC-12)"
        cases = (("lib.aux", False, "C3030"), ("Com1.math", False, "C3030"),
                 ("lib.Math", True, "C3030"), ("Lib.math", True, "C3030"),
                 ("a" * 256, False, "C1012"), (".".join(["a" * 50] * 6 + ["aux"]), False, "C3030"))
        for k, (candidate, prior, code) in enumerate(cases):
            with self.subTest(candidate=candidate):
                source = project / "main.ci"
                text = ("import lib.math;\n" if prior else "") + "import " + candidate + ";\n"
                source.write_bytes((text + "export I64 run() { return 0; }\n").encode("ascii"))
                ref = subprocess.run([sys.executable, "-m", "cint_ref", "run", str(source),
                                      "--entry", "run", "--path", "main.ci"], cwd=ROOT / "ref",
                                     env=dict(os.environ, PYTHONPATH=str(ROOT / "ref")),
                                     capture_output=True, check=False, timeout=120)
                self.assertEqual(ref.returncode, 0, ref.stderr)
                fields = dict(v.split(" ", 1) for v in ref.stdout.decode("ascii").splitlines() if " " in v)
                path = candidate.replace(".", "/") + ".ci" if code == "C3030" else "main.ci"
                column = 1 if code == "C3030" else 8
                expected = (code, path, 1, column)
                self.assertEqual((fields["diagnostic.code"], fields["diagnostic.position"]),
                                 (code, "%s:1:%d" % (path, column)))
                command = [str(self.exe), "emit-c", "--root", str(project),
                           "--out", str(self.out / ("candidate-path-out-%d" % k)), "lib/math.ci", "main.ci"]
                for json_mode in (True, False):
                    result = subprocess.run(command + (["--json"] if json_mode else []), cwd=self.out,
                                            env=self.env, capture_output=True, check=False, timeout=120)
                    (self.out / ("candidate-%d-%s.stderr" % (k, json_mode))).write_bytes(result.stderr)
                    self.assertEqual(result.returncode, 2, result.stderr)
                    self.assertEqual(result.stdout, b"")
                    if json_mode:
                        records = [json.loads(v) for v in result.stderr.splitlines()]
                        self.assertEqual([(r["code"], r["file"], r["line"], r["column"]) for r in records],
                                         [expected], result.stderr)
                        if code == "C3030":
                            self.assertEqual(records[0]["message"], collision_rule if prior else device_rule)
                    else:
                        self.assertIn(("--> %s:1:%d" % (path, column)).encode("ascii"), result.stderr)
                        self.assertIn(("error[" + code + "]").encode("ascii"), result.stderr)
                        if code == "C3030":
                            self.assertIn((collision_rule if prior else device_rule).encode("ascii"), result.stderr)

    def test_compiler_listed_path_rules(self):
        # CINTC-12 reports the rejected rule before reading a listed module.
        project = self.out / "listed-path-rules"
        project.mkdir()
        cases = (("lib/bad-name.ci", "every segment of a module path, without the final .ci, is an identifier "
                  "(SPEC-04 LS-15) (SPEC-09 CINTC-12)"),
                 ("lib/aux.ci", "a segment of a module path is not a Windows device name (SPEC-09 CINTC-12)"),
                 ("lib/Math.ci", "two module paths of one program are equal under ASCII case folding "
                  "(SPEC-09 CINTC-12)"))
        for k, (path, rule) in enumerate(cases):
            for json_mode in (True, False):
                with self.subTest(path=path, json_mode=json_mode):
                    command = [str(self.exe), "emit-c", "--root", str(project),
                               "--out", str(self.out / ("listed-path-out-%d" % k)), "lib/math.ci", path, "main.ci"]
                    result = subprocess.run(command + (["--json"] if json_mode else []), cwd=self.out,
                                            env=self.env, capture_output=True, check=False, timeout=120)
                    self.assertEqual(result.returncode, 2, result.stderr)
                    self.assertEqual(result.stdout, b"")
                    if json_mode:
                        records = [json.loads(v) for v in result.stderr.splitlines()]
                        self.assertEqual([(r["code"], r["file"], r["line"], r["column"]) for r in records],
                                         [("C3030", path, 1, 1)], result.stderr)
                        self.assertEqual(records[0]["message"], rule)
                    else:
                        self.assertIn(("--> %s:1:1" % path).encode("ascii"), result.stderr)
                        self.assertIn(rule.encode("ascii"), result.stderr)

    def test_compiler_import_diagnostic_path_in_receipt(self):
        source = self.out / "path_receipt.ci"
        source.write_bytes(b"import lib.aux;\n")
        receipt = self.out / "path-receipt.json"
        result = self.invoke(["--json", "--receipt", receipt, source])
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(result.stdout, b"")
        records = [json.loads(v) for v in result.stderr.splitlines()]
        self.assertEqual([(r["code"], r["file"], r["line"], r["column"]) for r in records],
                         [("C3030", "lib/aux.ci", 1, 1)], result.stderr)
        saved = json.loads(receipt.read_bytes())
        self.assertEqual(saved["outcome"]["diagnostics"],
                         [{"code": "C3030", "column": 1, "file": "lib/aux.ci", "line": 1}])

    def test_compiler_import_cycle_and_order(self):
        # G-C1 findings COR-3, COR-8, FX-1 (compiler/OPEN.md CINTC-OQ-50): imports are resolved
        # against every path of the build after the last discovery call. A cycle is C3003 at
        # the import that closes it, depth first from the root (cint_ref's position, in either
        # module order); an import of a module listed after its importer is C9102
        # U_IMPORT_ORDER at the import, not a false C3009.
        project = self.out / "import-order"
        shutil.rmtree(project, ignore_errors=True)
        (project / "lib").mkdir(parents=True)
        (project / "lib" / "a.ci").write_bytes(b"import lib.b;\n\nexport I64 fa() {\n    return 1;\n}\n")
        (project / "lib" / "b.ci").write_bytes(b"import lib.a;\n\nexport I64 fb() {\n    return 2;\n}\n")
        (project / "lib" / "c.ci").write_bytes(b"export I64 fc() {\n    return 3;\n}\n")
        (project / "m.ci").write_bytes(b"import lib.a;\n\nexport I64 run() {\n    return a.fa();\n}\n")
        (project / "n.ci").write_bytes(b"import lib.c;\n\nexport I64 run() {\n    return c.fc();\n}\n")
        cases = ((("lib/a.ci", "lib/b.ci", "m.ci"), ("C3003", "lib/b.ci", 1, 8)),
                 (("lib/b.ci", "lib/a.ci", "m.ci"), ("C3003", "lib/b.ci", 1, 8)),
                 (("n.ci", "lib/c.ci"), ("C9102", "n.ci", 1, 8)))
        for k, (rels, want) in enumerate(cases):
            with self.subTest(k=k):
                out = self.out / ("import-order-%d" % k)
                result = subprocess.run([str(self.exe), "--json", "emit-c", "--root", str(project), "--out", str(out),
                                         *rels], cwd=self.out, env=self.env, capture_output=True, check=False,
                                        timeout=120)
                self.assertEqual(result.returncode, 2, result.stderr)
                records = [json.loads(line) for line in result.stderr.splitlines()]
                got = [(r["code"], r["file"], r["line"], r["column"]) for r in records if "code" in r][:1]
                self.assertEqual(got, [want], result.stderr)
        out = self.out / "import-order-ok"
        result = subprocess.run([str(self.exe), "--json", "emit-c", "--root", str(project), "--out", str(out),
                                 "lib/c.ci", "n.ci"], cwd=self.out, env=self.env, capture_output=True, check=False,
                                timeout=120)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_compiler_public_names(self):
        directory = self.out / "public-names"
        directory.mkdir()
        rows = [("_module.ci", "safe", "C5051", 1), ("safe.ci", "_bad", "C5051", 12),
                ("safe.ci", "x" * 233, None, None), ("safe.ci", "x" * 234, "C9002", 12),
                ("a/b.ci", "safe", None, None), ("a_b.ci", "safe", None, None)]
        symbols = []
        for index, (relative, name, code, column) in enumerate(rows):
            with self.subTest(relative=relative, length=len(name)):
                path = directory / relative
                path.parent.mkdir(exist_ok=True)
                path.write_text("export I64 %s() { return 1; }\n" % name, encoding="ascii")
                out = directory / ("out%d" % index)
                result = subprocess.run([str(self.exe), "--json", "emit-c", "--root", str(directory),
                                         "--out", str(out), relative], cwd=directory, env=self.env,
                                        capture_output=True, timeout=120)
                self.assertEqual(result.returncode, 2 if code else 0, result.stderr)
                if code:
                    record = json.loads(result.stderr.splitlines()[0])
                    self.assertEqual((record["code"], record["line"], record["column"]), (code, 1, column))
                else:
                    stage = directory / (out / "MANIFEST.ref").read_text().split()[0]
                    data = (stage / relative).with_suffix(".c").read_bytes()
                    symbol = re.search(rb"cint_status (cx\w+)\(", data).group(1)
                    symbols.append(symbol)
                    if index == 2:
                        self.assertEqual(len(symbol), 247)
        self.assertNotEqual(symbols[-1], symbols[-2])
        self.assertIn(b"_x2F", symbols[-2])
        self.assertIn(b"_x5F", symbols[-1])

    def test_compiler_public_views_through_harness(self):
        sys.path.insert(0, str(ROOT / "tools"))
        sys.path.insert(0, str(ROOT / "compiler/tests"))
        import cint_check as cc
        import golden_suite
        for name, dependencies, expected in (("public_views", [], 35),
                                              ("public_records", ["record_lib.ci", "record_mid.ci"], 30)):
            with self.subTest(name=name):
                directory = self.out / name
                directory.mkdir()
                result = subprocess.run([str(self.exe), "--json", "emit-c", "--root",
                                         str(ROOT / "compiler/tests/golden"), "--out", str(directory / "out"),
                                         *dependencies, name + ".ci"], cwd=directory, env=self.env, capture_output=True, timeout=120)
                self.assertEqual(result.returncode, 0, result.stderr)
                files = golden_suite.output_set(directory / "out")
                sources = []
                for relative, data in files.items():
                    if relative.endswith(".c"):
                        path = directory / relative
                        if relative == "cint-program.c":
                            data = b"#define cint_observer_desc cg_original_observer\n" + data
                        path.write_bytes(data)
                        sources.append(path)
                adapter = directory / "adapter.c"
                adapter.write_text('#define %s_ADAPTER\n#include "%s"\n' %
                                   (name.upper(), (ROOT / ("compiler/tests/" + name + "_host.c")).as_posix()), encoding="ascii")
                tool = cc.Toolchain(CONFIG.leg, "0", "portable", False)
                objects = tool.compile(sources + [adapter], directory, name)
                library = tool.link_shared(objects + [pathlib.Path(self.values["runtime_object"])],
                                           directory / name, name)
                objects = tool.compile([ROOT / "harness/cint_harness.c"], directory, "harness")
                harness = tool.link_exe(objects + [pathlib.Path(self.values["runtime_object"])],
                                        directory / "harness", "harness")
                cases = directory / "cases.txt"
                cases.write_bytes((name + " check\n").encode("ascii"))
                result = subprocess.run([str(harness), str(library), "--cases", str(cases)],
                                        cwd=directory, env=tool.env, capture_output=True, timeout=60)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stderr, b"")
                self.assertIn(("return I64 %d" % expected).encode("ascii"), result.stdout)

    def test_compiler_missing_import_before_dependency_check(self):
        # Decision 27, item 3 (compiler/OPEN.md CINTC-OQ-50): the discovery pass resolves
        # every import of the build before any module is checked, so a missing import in the
        # root is reported before the type error of the module it imports, which compiles
        # first, as cint_ref does; the code and position of each come from cint_ref.
        project = self.out / "discovery"
        (project / "lib").mkdir(parents=True, exist_ok=True)
        (project / "main.ci").write_bytes(b"import lib.dep;\nimport gone.away;\n\n"
                                          b"export I64 run() {\n    return dep.f();\n}\n")
        (project / "lib" / "dep.ci").write_bytes(b"export I64 f() {\n    I64 x = true;\n    return x;\n}\n")

        def reference(relative, entry):
            result = subprocess.run([sys.executable, "-m", "cint_ref", "run", "--entry", entry, "--path", relative,
                                     str(project / relative)], cwd=ROOT / "ref", capture_output=True, check=False,
                                    env=dict(os.environ, PYTHONPATH=str(ROOT / "ref")), timeout=120)
            lines = dict(line.split(" ", 1) for line in result.stdout.decode("ascii").splitlines() if " " in line)
            path, line, column = lines["diagnostic.position"].rsplit(":", 2)
            return lines["diagnostic.code"], path, int(line), int(column)

        for name, rels, entry in (("both", ("lib/dep.ci", "main.ci"), "run"), ("dependency", ("lib/dep.ci",), "f")):
            with self.subTest(build=name):
                out = self.out / ("discovery-" + name)
                result = subprocess.run([str(self.exe), "--json", "emit-c", "--root", str(project), "--out", str(out),
                                         *rels], cwd=self.out, env=self.env, capture_output=True, check=False,
                                        timeout=120)
                self.assertEqual(result.returncode, 2, result.stderr)
                records = [json.loads(line) for line in result.stderr.splitlines()]
                got = [(r["code"], r["file"], r["line"], r["column"]) for r in records if "code" in r][:1]
                self.assertEqual(got, [reference(rels[-1], entry)], result.stderr)
                self.assertFalse(out.exists() and any(out.iterdir()), "a failed build committed an output set")

    def test_compiler_import_keeps_previous_output_set(self):
        # Slice 2 decision patch D-13 on B1 with imports between the modules (task 2.13 part
        # (iii), CINTC-16): three modules in a row, lib/b.ci large enough that its scratch
        # tables overwrite every row lib/a.ci used, and m.ci reading lib/a.ci's declarations
        # after both; then a diagnostic in m.ci (C3005 at a.missing) stops the build before
        # lowering, and MANIFEST.ref and the committed set stay byte for byte.
        root = self.out / "d13-imports"
        shutil.rmtree(root, ignore_errors=True)
        (root / "lib").mkdir(parents=True)
        (root / "lib" / "a.ci").write_bytes(b"export const I64 K = 5;\n\nexport I64 inc(I64 x) {\n"
                                            b"    return x + K;\n}\n")
        (root / "lib" / "b.ci").write_bytes(b"".join(
            b"export I64 f%d(I64 x) {\n    I64 y = x + %d;\n    return y * 2;\n}\n\n" % (k, k) for k in range(200)))
        (root / "m.ci").write_bytes(b"import lib.a;\nimport lib.b;\n\nexport I64 run() {\n"
                                    b"    return a.inc(b.f7(1)) + a.K;\n}\n")
        out = root / "out"

        def emit():
            return subprocess.run([str(self.exe), "--json", "emit-c", "--root", str(root), "--out", str(out),
                                   "lib/a.ci", "lib/b.ci", "m.ci"], cwd=root, capture_output=True, timeout=120)

        def committed():
            ref = (out / "MANIFEST.ref").read_bytes()
            stage = root / ref.decode("ascii").split(" ")[0]
            files = {"MANIFEST.ref": ref, "MANIFEST": (stage / "MANIFEST").read_bytes()}
            for line in files["MANIFEST"].decode("ascii").split("\n")[1:]:
                if line:
                    rel = line.split(" ", 2)[2]
                    files[rel] = (stage / rel).read_bytes()
            return files

        first = emit()
        self.assertEqual(first.returncode, 0, first.stderr)
        before = committed()
        importer = [data for rel, data in before.items() if rel.startswith("m") and rel.endswith(".c")]
        self.assertEqual(len(importer), 1, sorted(before))
        self.assertIn(b"ci_8_lib_x2Fa_3_inc(", importer[0])
        self.assertIn(b"ci_8_lib_x2Fb_2_f7(", importer[0])
        (root / "m.ci").write_bytes(b"import lib.a;\nimport lib.b;\n\nexport I64 run() {\n"
                                    b"    return a.missing(1);\n}\n")
        second = emit()
        self.assertEqual(second.returncode, 2, second.stderr)
        records = [json.loads(line) for line in second.stderr.splitlines() if line.startswith(b"{")]
        self.assertEqual([(r["code"], r["file"], r["line"], r["column"]) for r in records if "code" in r][:1],
                         [("C3005", "m.ci", 5, 14)], second.stderr)
        self.assertEqual(committed(), before)

    def test_compiler_runtime_revision_present(self):

        receipt = self.out / "revision-overflow.json"
        result = self.invoke(["--json", "--receipt", receipt, "examples/hello_overflow.ci"])
        fault = self.one_json(result, "CINT-FAULT-1", 1)
        self.assertIsNotNone(fault["revision"], "CONF-01: cintc fault.revision self is absent")
        self.assertRegex(fault["revision"], r"^[0-9a-f]{64}$")
        # CONF-01 `fault.revision self`: the SIR-17 revision of the program under test,
        # assembled here from its SIR text, not read from the compiler.
        self.assertEqual(fault["revision"], sir17_revision("hello_overflow.ci", self.generated_sir(receipt)))

    def test_compiler_revision_ignores_blank_lines(self):
        source = self.out / "revision-blank"
        source.mkdir(exist_ok=True)
        text = (ROOT / "examples/hello_overflow.ci").read_bytes()
        revisions = []
        for name, data in (("a", text), ("b", b"\n\n" + text.replace(b"\n", b"\n\n", 1))):
            (source / name).mkdir(exist_ok=True)
            (source / name / "hello_overflow.ci").write_bytes(data)
            result = self.invoke(["--json", "--root", source / name, "hello_overflow.ci"])
            revisions.append(self.one_json(result, "CINT-FAULT-1", 1)["revision"])
        # SIR-17: positions are not in the revision identity.
        self.assertEqual(revisions[0], revisions[1])

    def test_fuel_consumed_matches_reference(self):
        for relative, status in (("examples/hello.ci", 0), ("examples/hello_overflow.ci", 1)):
            with self.subTest(case=relative):
                receipt = self.out / ("fuel-%s.json" % pathlib.Path(relative).stem)
                result = self.invoke(["--json", "--receipt", receipt, relative])
                self.assertEqual(result.returncode, status, result.stderr)
                record = json.loads(receipt.read_bytes())
                self.assertEqual(record["outcome"]["fuel_consumed"], reference_fuel(ROOT, relative))

    def test_program_without_fuel_record(self):
        result = self.fake("no-fuel")
        self.tool_error(result, 7)

    def fake(self, mode):
        tc = self.clone_toolchain(self.helper, "fake.toolchain")
        env = dict(self.env, CINT_TEST_MODE=mode)
        return self.invoke(["--json", "examples/hello.ci"], tc=tc, env=env,
                           input_bytes=b"the program must not read this\n")

    def test_host_rejection_captures_both_channels(self):
        result = self.fake("reject")
        self.assertEqual(result.stdout, b"")
        error = self.tool_error(result, 7)
        self.assertEqual(error["tool_exit_status"], 19)
        log = pathlib.Path(error["log_path"]).read_bytes()
        self.assertIn(b"compiler stdout marker", log)
        self.assertIn(b"compiler stderr marker", log)
        self.assertNotIn(b"compiler stdout marker", result.stderr)

    def test_host_warning_is_internal_error(self):
        result = self.fake("warn")
        self.assertEqual(result.stdout, b"")
        error = self.tool_error(result, 7)
        self.assertEqual(error["tool_exit_status"], 0)

    def test_host_command_line_and_linker_warnings_are_internal_errors(self):
        for mode in ("warn-cmdline", "warn-link"):
            with self.subTest(mode=mode):
                result = self.fake(mode)
                self.assertEqual(result.stdout, b"")
                error = self.tool_error(result, 7)
                self.assertEqual(error["tool_exit_status"], 0)

    def test_compiler_option_variables_are_removed(self):
        tc = self.clone_toolchain(self.helper, "fake.toolchain")
        env = dict(self.env, CINT_TEST_MODE="env-clean", CL="/w", _CL_="/O2", LINK="/badlinkopt",
                   _LINK_="/badlinkopt", CCC_OVERRIDE_OPTIONS="x-Werror")
        result = self.invoke(["--json", "examples/hello.ci"], tc=tc, env=env)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, b"stdin:null\n")
        self.assertEqual(result.stderr, b"")

    def test_json_before_subcommand(self):
        hello = ["run", "--cache", str(self.out / "cache"), str(ROOT / "examples/hello.ci")]
        for arguments in (["--json"], ["--json", *hello]):
            with self.subTest(arguments=arguments[:2]):
                result = subprocess.run([str(self.exe), *arguments], cwd=ROOT, env=self.env,
                                        capture_output=True, check=False, timeout=120)
                if len(arguments) == 1:
                    self.assertIn("no subcommand", self.tool_error(result, 4)["message"])
                else:
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(result.stdout, (ROOT / "examples/hello.stdout").read_bytes())
                    self.assertEqual(result.stderr, b"")

    def test_program_stdin_is_null_compiler_output_captured(self):
        result = self.fake("stdin")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, b"stdin:null\n")
        self.assertEqual(result.stderr, b"")

    def test_program_unrecognized_status(self):
        result = self.fake("bad-status")
        self.assertEqual(result.stdout, b"partial")
        error = self.tool_error(result, 7)
        self.assertEqual(error["tool_exit_status"], 23)

    def test_program_fault_without_record(self):
        result = self.fake("fault-empty")
        self.assertEqual(result.stdout, b"partial")
        self.tool_error(result, 7)

    def test_program_fault_malformed_record(self):
        result = self.fake_record(b"bad-record")
        self.assertEqual(result.stdout, b"")
        self.tool_error(result, 7)

    def test_program_stderr_is_internal_error(self):
        result = self.fake("stderr")
        self.assertEqual(result.stdout, b"")
        error = self.tool_error(result, 7)
        self.assertIn(b"program diagnostic", pathlib.Path(error["log_path"]).read_bytes())

    def test_source_name_warning_is_not_a_warning(self):
        source = self.out / "warning.ci"
        source.write_bytes((ROOT / "examples/hello.ci").read_bytes())
        result = self.invoke(["--json", source])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, (ROOT / "examples/hello.stdout").read_bytes())
        self.assertEqual(result.stderr, b"")

    def test_concurrent_hello_cache_isolation(self):
        receipts = [self.out / ("concurrent-%d.json" % i) for i in range(2)]
        commands = [[str(self.exe), "run", "--json", "--receipt", str(receipt),
                     "--cache", str(self.out / "concurrent cache"),
                     str(ROOT / "examples/hello.ci")] for receipt in receipts]
        processes = [subprocess.Popen(command, cwd=ROOT, env=self.env,
                                      stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE) for command in commands]
        records = []
        for index, (process, receipt) in enumerate(zip(processes, receipts)):
            stdout, stderr = process.communicate(timeout=120)
            (self.out / ("concurrent-%d.stdout" % index)).write_bytes(stdout)
            (self.out / ("concurrent-%d.stderr" % index)).write_bytes(stderr)
            self.assertEqual(process.returncode, 0, stderr)
            self.assertEqual(stdout, (ROOT / "examples/hello.stdout").read_bytes())
            self.assertEqual(stderr, b"")
            records.append(self.check_receipt(receipt, stdout, 0, ROOT / "examples/hello.ci", "hello.ci"))
        self.assertNotEqual(records[0]["observations"]["cache"], records[1]["observations"]["cache"])
        self.assertEqual(records[0]["identity"], records[1]["identity"])
        self.assertEqual(records[0]["outcome"], records[1]["outcome"])

    def fault_record(self, operands=(), exact=None, path=b"hello.ci", kernel=None):
        text = lambda value: struct.pack("<I", len(value)) + value
        position = text(path) + struct.pack("<II", 1, 1)
        address = b"\x00" if kernel is None else b"\x01" + text(kernel) + struct.pack("<qBB", 0, 0, 0)
        return (text(b"cint-core-1/fault/v2") + struct.pack("<H", 1) + text(b"add.checked.i64") +
                struct.pack("<I", len(operands)) + b"".join(operands) +
                (b"\x00" if exact is None else b"\x01" + exact) +
                b"\x00" + position + b"\x00\x00" + address + struct.pack("<I", 0))

    def fake_record(self, data, *, json_mode=True, mode="fault-file", extra=()):
        path = self.out / "injected-fault.bin"
        path.write_bytes(data)
        env = dict(self.env, CINT_TEST_MODE=mode, CINT_TEST_FAULT_FILE=str(path))
        return self.invoke((["--json"] if json_mode else []) + list(extra) + ["examples/hello.ci"],
                           tc=self.clone_toolchain(self.helper, "fake.toolchain"), env=env)

    def error_record(self, set_name=b"hello.LedgerError", value=b"overdrawn", code=0x22, tag=1,
                     domain=b"cint-core-1/error-result/v1"):
        # rt/cint_rt.h 6d (rt/OPEN.md RT-OQ-33): domain, set and value names, tag type code, tag.
        text = lambda value: struct.pack("<I", len(value)) + value
        return text(domain) + text(set_name) + text(value) + struct.pack("<IQ", code, tag)

    def test_program_error_result(self):
        # Status 6 (SPEC-06 3.4a, BX12-12, BX12-22): one stderr line, or one CINT-ERROR-1 object.
        result = self.fake_record(self.error_record(), json_mode=False, mode="error-file")
        self.assertEqual((result.returncode, result.stdout), (6, b""), result.stderr)
        self.assertEqual(result.stderr, b"error: hello.LedgerError.overdrawn (tag 1)\n")
        result = self.fake_record(self.error_record(b"ArithError", b"overflow", 0x24, (1 << 64) - 1),
                                  json_mode=False, mode="error-file")
        self.assertEqual(result.stderr, b"error: ArithError.overflow (tag 18446744073709551615)\n")
        receipt_path = self.out / "error-receipt.json"
        result = self.fake_record(self.error_record(b"ingest.IoOrParse", b"ParseError.full", 0x23, 5),
                                  mode="error-file", extra=["--receipt", receipt_path])
        line = self.one_json(result, "CINT-ERROR-1", 6)
        self.assertEqual(result.stderr, canonical(line) + b"\n")
        self.assertEqual(line, {"schema": "CINT-ERROR-1", "set": "ingest.IoOrParse",
                                "tag": {"t": "U32", "v": "5"}, "value": "ParseError.full"})
        outcome = self.receipt_of(receipt_path, "run")["outcome"]
        self.assertEqual((outcome["exit_status"], outcome["kind"], outcome["fault"], outcome["fuel_consumed"]),
                         (6, "error", None, "0"))
        self.assertEqual(outcome["error"], line)

    def test_program_error_malformed_record(self):
        good = self.error_record
        for name, data in (("domain", good(domain=b"cint-core-1/error-result/v2")), ("trailing", good() + b"\x00"),
                           ("short", good()[:-1]), ("tag zero", good(tag=0)), ("tag above U8", good(code=0x21, tag=256)),
                           ("signed type", good(code=0x12)), ("empty set", good(set_name=b"")),
                           ("space", good(set_name=b"hello.Ledger Error")), ("two dots", good(value=b"A.B.c")),
                           ("digit first", good(value=b"1st")), ("trailing dot", good(set_name=b"hello.")),
                           ("quote", good(value=b'a"b'))):
            with self.subTest(case=name):
                result = self.fake_record(data, mode="error-file")
                self.assertEqual(result.stdout, b"")
                self.tool_error(result, 7)

    def test_human_fault_canonical_fields(self):
        text = lambda value: struct.pack("<I", len(value)) + value
        position = lambda path, line, column: text(path) + struct.pack("<II", line, column)
        left = b"\x14" + struct.pack("<q", (1 << 63) - 1)
        right = b"\x14" + struct.pack("<q", 1)
        exact = b"\x0f" + struct.pack("<I", 9) + (1 << 63).to_bytes(9, "little", signed=True)
        record = (text(b"cint-core-1/fault/v2") + struct.pack("<H", 1) + text(b"add.checked.i64") +
                  struct.pack("<I", 2) + left + right + b"\x01" + exact + b"\x01" + left +
                  position(b"other.ci", 4, 7) + b"\x01" + b"\x11" * 32 +
                  b"\x01" + b"\x22" * 32 + b"\x01" + text(b"kernel") +
                  struct.pack("<qBBqq", 7, 1, 1, 3, 5) + struct.pack("<I", 1) +
                  position(b"outer.ci", 2, 9))
        result = self.fake_record(record, json_mode=False)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(result.stdout, b"")
        lines = result.stderr.decode("utf-8").splitlines()
        self.assertTrue(lines[0].startswith("fault[E_OVERFLOW]:"), result.stderr)
        for line in ("  --> other.ci:4:7", "   = operation: add.checked.i64",
                     "   = operand-count: 2", "   = operand: I64 9223372036854775807",
                     "   = operand: I64 1", "   = exact:   9223372036854775808",
                     "   = limit:   I64 9223372036854775807", "   = revision: " + "11" * 32,
                     "   = source-map: " + "22" * 32,
                     "   = address: kernel, dispatch 7, phase 1, work-item 3, step 5",
                     "   = stack-depth: 1", "   = called from: outer.ci:2:9",
                     "   = state: the program stopped; stdout holds what it wrote before the fault"):
            self.assertIn(line, lines)
        self.assertNotIn(b" |", result.stderr)
        self.assertNotIn(b"receipt:", result.stderr)

    def unit_invoke(self, arguments):
        type(self).serial += 1
        name = "%03d-%s-unit" % (self.serial, self._testMethodName)
        result = subprocess.run([str(self.unit), *map(str, arguments)], cwd=self.out,
                                env=self.env, stdin=subprocess.DEVNULL,
                                capture_output=True, check=False, timeout=60)
        (self.out / (name + ".stdout")).write_bytes(result.stdout)
        (self.out / (name + ".stderr")).write_bytes(result.stderr)
        return result

    def test_runtime_fault_decoder_rejects_invalid_values(self):
        bad_values = [b"\x01\x02", b"\x0f" + struct.pack("<I", 1) + b"\x00",
                      b"\x0f" + struct.pack("<I", 2) + b"\x01\x00",
                      b"\x0f" + struct.pack("<I", 2) + b"\xff\xff",
                      b"\x0f" + struct.pack("<I", 258) + b"\x00" * 257 + b"\x01"]
        for value in bad_values:
            with self.subTest(value=value[:8].hex()):
                self.tool_error(self.fake_record(self.fault_record([value])), 7)

    def test_runtime_fault_decoder_zero_and_bound(self):
        zero = b"\x0f" + struct.pack("<I", 0)
        fault = self.one_json(self.fake_record(self.fault_record(exact=zero)), "CINT-FAULT-1", 1)
        self.assertEqual(fault["exact"], {"t": "Z", "v": "0"})
        number = b"\x00" * 256 + b"\x01"
        maximum = b"\x0f" + struct.pack("<I", 257) + number
        fault = self.one_json(self.fake_record(self.fault_record(exact=maximum)), "CINT-FAULT-1", 1)
        self.assertEqual(fault["exact"], {"t": "Z", "v": str(1 << 2048)})

    def test_runtime_fault_decoder_rejects_trailing_bytes(self):
        self.tool_error(self.fake_record(self.fault_record() + b"x"), 7)

    def test_runtime_fault_decoder_exact_requires_z(self):
        self.tool_error(self.fake_record(self.fault_record(exact=b"\x14" + struct.pack("<q", 1))), 7)

    def test_runtime_fault_decoder_utf8_fields(self):
        for data in (self.fault_record(path=b"\xff.ci"), self.fault_record(kernel=b"\xff")):
            with self.subTest(record=data.hex()):
                self.tool_error(self.fake_record(data), 7)

    def test_compile_fault_decoder_large_z_bounds(self):
        path = self.out / "compile-record.bin"
        operand = b"\x0f" + struct.pack("<I", 513) + b"\xff" * 512 + b"\x00"
        exact = b"\x0f" + struct.pack("<I", 520) + b"\x00" * 519 + b"\x01"
        path.write_bytes(self.fault_record([operand], exact=exact) + b"\x00" * 32)
        result = self.unit_invoke(["--decode", path, "1"])
        self.assertEqual(result.returncode, 0, result.stderr)
        record = json.loads(result.stdout)
        self.assertEqual(record["operands"], [{"t": "Z", "v": str((1 << 4096) - 1)}])
        self.assertEqual(record["exact"], {"t": "Z", "v": str(1 << 4152)})
        self.assertEqual(self.unit_invoke(["--decode", path, "0"]).returncode, 7)
        bad_operand = b"\x0f" + struct.pack("<I", 514) + b"\x00" * 513 + b"\x01"
        bad_exact = b"\x0f" + struct.pack("<I", 521) + b"\x00" * 520 + b"\x01"
        for data in (self.fault_record([bad_operand]), self.fault_record(exact=bad_exact)):
            path.write_bytes(data)
            self.assertEqual(self.unit_invoke(["--decode", path, "1"]).returncode, 7)

    def test_internal_compiler_fault_retains_record_and_identities(self):
        destination = self.out / "internal compiler fault"
        result = self.unit_invoke(["--internal", destination])
        self.assertEqual(result.stdout, b"")
        error = self.tool_error(result, 7)
        log = pathlib.Path(error["log_path"])
        raw = log.read_bytes()
        retained = json.loads(raw)
        self.assertEqual(raw, canonical(retained) + b"\n")
        self.assertEqual(retained["schema"], "CINT-COMPILER-FAULT-1")
        self.assertEqual(retained["compiler_source_identity"],
                         sha(pathlib.Path(self.values["compiler_sources"]).read_bytes()))
        self.assertEqual(retained["inputs"], {"unit_input.ci": "a" * 64})
        binary = destination / "compiler-fault.bin"
        self.assertEqual(bytes.fromhex(retained["fault_record_hex"]), binary.read_bytes())
        decoded = self.unit_invoke(["--decode", binary, "0"])
        self.assertEqual(decoded.returncode, 0, decoded.stderr)
        self.assertEqual(json.loads(decoded.stdout), retained["fault"])
        fault = retained["fault"]
        self.assertEqual((fault["code"], fault["operation"], fault["file"], fault["line"], fault["column"]),
                         ("E_OVERFLOW", "add.checked.i64", "unit_compiler.ci", 3, 13))
        self.assertEqual(fault["operands"], [{"t": "I64", "v": "9223372036854775807"},
                                            {"t": "I64", "v": "1"}])
        self.assertEqual(fault["exact"], {"t": "Z", "v": "9223372036854775808"})

    def test_broken_stdout_is_environment_error(self):
        read_fd, write_fd = os.pipe()
        os.close(read_fd)
        try:
            process = subprocess.Popen([str(self.exe), "run", "--json", "--cache",
                                        str(self.out / "cache"), str(ROOT / "examples/hello.ci")],
                                       cwd=ROOT, env=self.env, stdin=subprocess.DEVNULL,
                                       stdout=write_fd, stderr=subprocess.PIPE)
        finally:
            os.close(write_fd)
        _, stderr = process.communicate(timeout=120)
        result = subprocess.CompletedProcess(process.args, process.returncode, b"", stderr)
        self.tool_error(result, 5)

    # Task 2.15: `cint build` and `cint test` (SPEC-06 3.3, 3.5; SPEC-09 RCPT-07).
    def command(self, subcommand, args, cwd=ROOT, tc=None, env=None):
        type(self).serial += 1
        name = "%03d-%s" % (self.serial, self._testMethodName)
        command = [str(self.exe), subcommand, "--cache", str(self.out / "cache"),
                   *(["--toolchain", str(tc)] if tc is not None else []), *map(str, args)]
        result = subprocess.run(command, cwd=cwd, env=env or self.env, stdin=subprocess.DEVNULL,
                                capture_output=True, check=False, timeout=300)
        (self.out / (name + ".stdout")).write_bytes(result.stdout)
        (self.out / (name + ".stderr")).write_bytes(result.stderr)
        return result

    def project(self, name, files):
        root = self.out / name
        shutil.rmtree(root, ignore_errors=True)
        for relative, text in files.items():
            (root / relative).parent.mkdir(parents=True, exist_ok=True)
            (root / relative).write_bytes(text)
        return root

    def receipt_of(self, path, kind):
        raw = path.read_bytes()
        receipt = json.loads(raw)
        self.assertEqual(raw, canonical(receipt) + b"\n")
        self.assertEqual(receipt["identity"]["schema"], "cint-receipt-1/" + kind)
        self.assertEqual(receipt["identity"]["kind"], kind)
        self.assertEqual(receipt["observations"]["receipt_identity_sha256"],
                         sha(canonical({"identity": receipt["identity"], "outcome": receipt["outcome"]})))
        return receipt

    def test_test_three_tests_fixture(self):
        # Plan task 2.15 acceptance, and each test against `cint_ref run --test` (SPEC-06 1.4 fuel).
        fixture = ROOT / "cli/tests/fixtures/three_tests.ci"
        result = self.command("test", [fixture])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, b"")
        lines = result.stdout.decode("ascii").splitlines()
        self.assertEqual(lines[-1], "3 of 3 tests passed")
        self.assertEqual(lines[:-1], ['test three_tests.ci:8 "adds small numbers" ... ok',
                                      'test three_tests.ci:12 "checked add overflows" ... ok',
                                      'test three_tests.ci:17 "adds a negative number" ... ok'])
        result = self.command("test", ["--json", fixture])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, b"")
        records = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual([r["schema"] for r in records], ["CINT-TEST-1"] * 3)
        for record in records:
            ref = subprocess.run([sys.executable, "-m", "cint_ref", "run", "--test", record["name"], "--fuel",
                                  "1000000000", "--path", "three_tests.ci", str(fixture)], cwd=ROOT / "ref",
                                 env=dict(os.environ, PYTHONPATH=str(ROOT / "ref")), capture_output=True,
                                 check=False, timeout=120)
            fields = dict(v.split(" ", 1) for v in ref.stdout.decode("ascii").splitlines() if " " in v)
            self.assertEqual(fields["fuel-consumed"], record["fuel_consumed"])
            self.assertEqual(fields["outcome"], "value" if record["fault"] is None else "fault")
            if record["fault"] is not None:
                self.assertEqual(fields["fault.code"], record["fault"]["code"])
                self.assertEqual(fields["fault.position"], "%s:%d:%d" % (
                    record["fault"]["file"], record["fault"]["line"], record["fault"]["column"]))

    def test_test_failures_exit_3(self):
        # SPEC-04 LS-235, LS-236, LS-240: each verdict as cint_ref's test_verdict gives it.
        root = self.project("test-failures", {"cases.ci": (
            b'I64 twice(I64 x) {\n    return x * 2;\n}\n\n'
            b'test "assert \\u{e9}\\x41" {\n    assert(twice(2) == 5);\n}\n\n'
            b'test "wrong code" expect_fault E_DIV_ZERO {\n    I64 r = twice(9223372036854775807);\n}\n\n'
            b'test "wrong line" expect_fault E_OVERFLOW at 99 {\n    I64 r = twice(9223372036854775807);\n}\n\n'
            b'test "completes" expect_fault E_OVERFLOW {\n    I64 r = twice(1);\n}\n\n'
            b'test "right line" expect_fault E_OVERFLOW at 2 {\n    I64 r = twice(9223372036854775807);\n}\n')})
        result = self.command("test", [root / "cases.ci"])
        self.assertEqual(result.returncode, 3, result.stderr)
        out = result.stdout.decode("ascii")
        self.assertEqual(out.splitlines()[-1], "1 of 5 tests passed")
        self.assertEqual([line.rsplit(" ", 1)[1] for line in out.splitlines() if line.startswith("test ")],
                         ["FAILED", "FAILED", "FAILED", "FAILED", "ok"])
        self.assertIn("  expected fault E_OVERFLOW; the test completed\n", out)
        self.assertIn("  expected fault E_OVERFLOW at line 99\n", out)
        self.assertIn(b"fault[E_ASSERT]", result.stderr)
        receipt_path = self.out / "test-failures.json"
        result = self.command("test", ["--json", "--receipt", receipt_path, root / "cases.ci"])
        self.assertEqual(result.returncode, 3, result.stderr)
        self.assertEqual(result.stderr, b"")
        records = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual([(r["name"], r["passed"], r["expect_fault"], r["expect_line"]) for r in records],
                         [("assert \u00e9A", False, None, None), ("wrong code", False, "E_DIV_ZERO", None),
                          ("wrong line", False, "E_OVERFLOW", 99), ("completes", False, "E_OVERFLOW", None),
                          ("right line", True, "E_OVERFLOW", 2)])
        receipt = self.receipt_of(receipt_path, "test")
        self.assertEqual(receipt["identity"]["method"]["fuel_budget"], "1000000000")
        self.assertEqual(receipt["outcome"]["exit_status"], 3)
        self.assertEqual(receipt["outcome"]["kind"], "fail")
        self.assertEqual(receipt["outcome"]["tests"], [{k: v for k, v in r.items() if k != "schema"} for r in records])

    def test_test_error_leaves_test(self):
        # SPEC-04 LS-315, SPEC-06 3.5 (BX12-13): a test that ends with status 6 fails, and its report
        # names the error as `cint run` does; the fake host exits 6 with the injected error record.
        path = self.out / "injected-error.bin"
        path.write_bytes(self.error_record(b"three_tests.ParseError", b"bad_digit", 0x21, 2))
        tc = self.clone_toolchain(self.helper, "fake.toolchain")
        env = dict(self.env, CINT_TEST_MODE="error-file", CINT_TEST_FAULT_FILE=str(path))
        fixture = ROOT / "cli/tests/fixtures/three_tests.ci"
        result = self.command("test", [fixture], tc=tc, env=env)
        self.assertEqual(result.returncode, 3, result.stderr)
        lines = result.stdout.decode("ascii").splitlines()
        self.assertEqual(lines[-1], "0 of 3 tests passed")
        self.assertEqual([line.rsplit(" ", 1)[1] for line in lines if line.startswith("test ")], ["FAILED"] * 3)
        self.assertIn("  expected fault E_OVERFLOW at line 5; an error left the test", lines)
        self.assertEqual(result.stderr, b"error: three_tests.ParseError.bad_digit (tag 2)\n" * 3)
        receipt_path = self.out / "test-error.json"
        result = self.command("test", ["--json", "--receipt", receipt_path, fixture], tc=tc, env=env)
        self.assertEqual((result.returncode, result.stderr), (3, b""))
        records = [json.loads(line) for line in result.stdout.splitlines()]
        error = {"schema": "CINT-ERROR-1", "set": "three_tests.ParseError", "tag": {"t": "U8", "v": "2"},
                 "value": "bad_digit"}
        self.assertEqual([(r["passed"], r["fault"], r["error"]) for r in records], [(False, None, error)] * 3)
        receipt = self.receipt_of(receipt_path, "test")
        self.assertEqual(receipt["outcome"]["tests"], [{k: v for k, v in r.items() if k != "schema"} for r in records])

    def test_test_project_path_byte_order(self):
        # SPEC-04 LS-234: modules in path byte order, tests in source order; DIR/src is the root.
        root = self.project("test-order", {
            "src/main.ci": b'import lib.z;\nimport Alpha;\n\nvoid main() {\n    I64 v = z.f() + Alpha.g();\n'
                           b'    "{v}\\n";\n}\n\ntest "main" {\n    assert(z.f() == 1);\n}\n',
            "src/lib/z.ci": b'export I64 f() {\n    return 1;\n}\n\ntest "z one" {\n    assert(f() == 1);\n}\n\n'
                            b'test "z two" {\n    assert(f() + f() == 2);\n}\n',
            "src/Alpha.ci": b'export I64 g() {\n    return 2;\n}\n\ntest "alpha" {\n    assert(g() == 2);\n}\n'})
        receipt_path = self.out / "test-order.json"
        result = self.command("test", ["--receipt", receipt_path], cwd=root)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([line.split(" ")[1] for line in result.stdout.decode("ascii").splitlines()[:-1]],
                         ["Alpha.ci:5", "lib/z.ci:5", "lib/z.ci:9", "main.ci:9"])
        self.assertEqual(result.stdout.decode("ascii").splitlines()[-1], "4 of 4 tests passed")
        identity = self.receipt_of(receipt_path, "test")["identity"]
        self.assertEqual(identity["lookups"], {"Alpha.ci": True, "lib/z.ci": True})
        self.assertEqual(sorted(identity["source"]["files"]), ["Alpha.ci", "lib/z.ci", "main.ci"])
        self.assertEqual(identity["source"]["files"]["lib/z.ci"], sha((root / "src/lib/z.ci").read_bytes()))
        result = self.command("run", [root / "src/main.ci"])
        self.assertEqual((result.returncode, result.stdout), (0, b"3\n"), result.stderr)
        result = self.command("test", [self.project("no-tests", {"main.ci": b"void main() {\n}\n"})])
        self.assertEqual((result.returncode, result.stdout), (0, b"0 of 0 tests passed\n"), result.stderr)

    def test_build_project_and_lookups(self):
        # SPEC-06 3.3 and SPEC-09 RCPT-07: every lookup is recorded, an absent one as false.
        root = self.project("build-project", {
            "main.ci": b'import util.math;\n\nvoid main() {\n    I64 v = math.twice(21);\n    "{v}\\n";\n}\n',
            "util/math.ci": b'export I64 twice(I64 x) {\n    return x * 2;\n}\n'})
        receipt_path = self.out / "build-project.json"
        result = self.command("build", ["--receipt", receipt_path, root])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, b"")
        program = root / "build" / ("main.exe" if CONFIG.leg == "msvc" else "main")
        self.assertIn(("built: " + str(program)).encode("utf-8"), result.stderr)
        ran = subprocess.run([str(program)], capture_output=True, check=False, timeout=60)
        self.assertEqual((ran.returncode, ran.stdout), (0, b"42\n"))
        receipt = self.receipt_of(receipt_path, "build")
        self.assertIsNone(receipt["identity"]["method"])
        self.assertEqual(receipt["identity"]["lookups"], {"util/math.ci": True})
        self.assertEqual(receipt["observations"]["executables"]["program"], sha(program.read_bytes()))
        self.assertEqual((receipt["outcome"]["kind"], receipt["outcome"]["exit_status"]), ("built", 0))
        (root / "util/math.ci").write_bytes(b'import util.gone;\n\nexport I64 twice(I64 x) {\n    return x * 2;\n}\n')
        result = self.command("build", ["--json", "--receipt", receipt_path, "--out", self.out / "build-out", root])
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual([(r["code"], r["file"], r["line"]) for r in map(json.loads, result.stderr.splitlines())],
                         [("C3009", "util/math.ci", 1)])
        receipt = self.receipt_of(receipt_path, "build")
        self.assertEqual(receipt["identity"]["lookups"], {"util/gone.ci": False, "util/math.ci": True})
        self.assertFalse((self.out / "build-out").exists())

    def test_build_library(self):
        # `cint build --lib` (SPEC-06 3.1; box 10 default BX10-07): one shared library per program,
        # exporting the cx wrappers, the cm descriptors, the library descriptor (SPEC-03 A-19),
        # the observer symbols, and the runtime's host functions (CINT_RT_API in rt/cint_rt.h),
        # and nothing else (SPEC-09 EMIT-31).
        sys.path.insert(0, str(ROOT / "tools"))
        import cint_check as cc
        root = self.project("build-lib", {
            "shapes.ci": b'import util.math;\n\nexport I64 twice(I64 x) {\n    return math.twice(x);\n}\n\n'
                         b'export I64 first(in I64[_] a) {\n    return a[0];\n}\n',
            "util/math.ci": b'export I64 twice(I64 x) {\n    return x * 2;\n}\n',
            "main.ci": b'void main() {\n}\n',
            "grid.ci": b'export I64 corner[h, w](in I64[h, w] g) {\n    return g[0, 0];\n}\n\n'
                       b'export kernel scale[n](in I64[n] x, out I64[n] y) over [i: n] {\n    y[i] = x[i] * 2;\n}\n'})
        result = self.command("build", ["--lib", "--out", self.out / "build-lib-out", "shapes.ci"], cwd=root)
        self.assertEqual(result.returncode, 0, result.stderr)
        library = self.out / "build-lib-out" / {"msvc": "shapes.dll"}.get(
            CONFIG.leg, "libshapes" + (".dylib" if sys.platform == "darwin" else ".so"))
        self.assertIn(("built: " + str(library)).encode("utf-8"), result.stderr)
        header = (ROOT / "rt/cint_rt.h").read_text(encoding="utf-8")
        host = set(re.findall(r"^CINT_RT_API [^(]*?\b(cint_\w+)\(", header, re.M))
        self.assertIn("cint_fault_get", host)
        tool = cc.Toolchain(CONFIG.leg, "0", "portable", False)
        exports = tool.exports(library)
        self.assertEqual(exports - host, {"cx_6_shapes_5_twice", "cx_6_shapes_5_first", "cm_6_shapes",
                                          "cm_12_util_x2Fmath", "cint_library_desc", "cint_program_abi",
                                          "cint_observer_desc"})
        self.assertEqual(host - exports, set())
        self.tool_error(self.command("build", ["--json", "--lib", "main.ci"], cwd=root), 4)
        # A view of rank 2 and an exported kernel have wrappers since box 10 unit 3 (compiler/OPEN.md
        # CINTC-OQ-58 item 8; rt/OPEN.md RT-OQ-38, RT-OQ-40).
        result = self.command("build", ["--lib", "--out", self.out / "build-lib-grid", "grid.ci"], cwd=root)
        self.assertEqual(result.returncode, 0, result.stderr)
        library = self.out / "build-lib-grid" / {"msvc": "grid.dll"}.get(
            CONFIG.leg, "libgrid" + (".dylib" if sys.platform == "darwin" else ".so"))
        self.assertEqual(tool.exports(library) - host, {"cx_4_grid_6_corner", "cx_4_grid_5_scale", "cm_4_grid",
                                                        "cint_library_desc", "cint_program_abi",
                                                        "cint_observer_desc"})
        self.tool_error(self.command("run", ["--json", "--lib", root / "shapes.ci"]), 4)

    def test_imported_module_variables(self):
        # G-C2 review COR-2 and COR-6: `alias.x` reads and writes an exported module-level
        # variable in its module's state (`cint_ref`: 18 4, fuel 3); C5012 (LS-122) where this
        # module decides it, C9102 where it rests on what an imported function writes.
        root = self.project("imported-vars", {
            "lib/state.ci": b'export I64 counter = 3;\nI64 hidden = 0;\n\nexport void add(I64 v) {\n'
                            b'    counter += v;\n    hidden += 1;\n}\n',
            "main.ci": b'import lib.state;\n\nI64 mine = 4;\n\nvoid put(I64 v) {\n    state.counter = v;\n}\n\n'
                       b'void main() {\n    state.counter += 2;\n    state.add(mine);\n    put(state.counter * 2);\n'
                       b'    "{state.counter} {mine}\\n";\n}\n',
            "direct.ci": b'import lib.state;\n\nvoid put(I64 v) {\n    state.counter = v + 1;\n}\n\n'
                         b'void main() {\n    put(state.counter);\n}\n',
            "imported.ci": b'import lib.state;\n\nvoid main() {\n    state.add(state.counter);\n}\n'})
        result = self.command("run", [root / "main.ci"])
        self.assertEqual((result.returncode, result.stdout), (0, b"18 4\n"), result.stderr)
        for name, want in (("direct.ci", ("C5012", "direct.ci", 8, 9)),
                           ("imported.ci", ("C9102", "imported.ci", 4, 15))):
            with self.subTest(name=name):
                result = self.command("run", ["--json", root / name])
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertEqual([(r["code"], r["file"], r["line"], r["column"])
                                  for r in map(json.loads, result.stderr.splitlines())], [want])

    def test_build_and_test_usage(self):
        # A module without main or script statements has tests but no program.
        for subcommand, args in (("build", ["--root", ROOT, "examples"]), ("test", ["a.ci", "b.ci"]),
                                 ("test", ["--out", "x", "a.ci"]), ("build", [ROOT / "cli/tests/fixtures/three_tests.ci"]),
                                 ("run", [ROOT / "cli/tests/fixtures/three_tests.ci"])):
            with self.subTest(subcommand=subcommand, args=args):
                self.tool_error(self.command(subcommand, ["--json", *args]), 4)


class ParityTests(RunTests):
    @classmethod
    def setUpClass(cls):
        cls.exe = CONFIG.exe.resolve()
        cls.out = CONFIG.run_out
        cls.rows = toolchain(cls.exe.with_name("cint.toolchain"))
        cls.values = dict(cls.rows)
        cls.env = os.environ.copy()
        for key, value in cls.rows:
            if key == "env":
                name, text = value.split("=", 1)
                cls.env[name] = text
        cls.serial = 0
        archive = pathlib.Path(cls.values["compiler_sources"])
        source_identity = parity.verify_source_archive(archive)
        bootstrap = archive.with_name("bootstrap.json")
        boot_record = json.loads(bootstrap.read_bytes())
        if boot_record["identity"]["compiler_source_identity"] != source_identity:
            raise ValueError("bootstrap receipt compiler identity differs")
        if boot_record["observations"]["cint_sha256"] != sha(cls.exe.read_bytes()):
            raise ValueError("bootstrap receipt executable identity differs")
        cls.parity_report = {"schema": "cint-scoped-parity-1", "leg": CONFIG.leg,
                             "compiler_source_identity": source_identity,
                             "exe_sha256": sha(cls.exe.read_bytes()),
                             "bootstrap_receipt": str(bootstrap),
                             "bootstrap_receipt_sha256": sha(bootstrap.read_bytes()),
                             "inventory": parity.inventory(), "observations": []}
        cls.write_parity_report()
        if cls.values["executable_sha256"] != cls.parity_report["exe_sha256"]:
            raise ValueError("bootstrap executable differs from toolchain identity")

    @classmethod
    def write_parity_report(cls):
        (cls.out / "parity.json").write_bytes(canonical(cls.parity_report) + b"\n")

    def reference_run(self, source, relative, entry, directory, name, frozen=None):
        output = directory / (name + ".program.stdout")
        command = [sys.executable, "-m", "cint_ref", "run", str(source),
                   "--path", relative, "--entry", entry, "--stdout", str(output)]
        if frozen is not None:
            command += ["--case", frozen["case"]]
            if frozen.get("clause"):
                command += ["--clause", frozen["clause"]]
            if frozen.get("format"):
                command += ["--format", frozen["format"]]
        result = subprocess.run(command, cwd=ROOT / "ref",
                                env=dict(self.env, PYTHONPATH=str(ROOT / "ref")),
                                stdin=subprocess.DEVNULL, capture_output=True,
                                check=False, timeout=120)
        (directory / (name + ".stdout")).write_bytes(result.stdout)
        (directory / (name + ".stderr")).write_bytes(result.stderr)
        details = {"command": command, "process_exit_status": result.returncode,
                   "stdout_sha256": sha(result.stdout), "stderr_sha256": sha(result.stderr)}
        (directory / (name + ".json")).write_bytes(canonical(details) + b"\n")
        program_stdout = output.read_bytes() if output.is_file() else b""
        observed = parity.reference_observation(result, program_stdout)
        return dict(observed, process=details), program_stdout

    def parity_case(self, group, row):
        case, entry = row["case"], row["entry"]
        relative = case + ".ci"
        directory = self.out / "parity" / (case.replace("/", "-") + "-" + entry)
        directory.mkdir(parents=True)
        observed = {"group": group, "case": case, "entry": entry,
                    "source_sha256": row["source_sha256"],
                    "expectation_sha256": row["expectation_sha256"], "pass": False,
                    "evidence": str(directory)}
        try:
            original, original_stdout = self.reference_run(
                pathlib.Path(row["source"]), relative, entry, directory,
                "original-reference", row["expected"])
            observed["original_export"] = original
            self.assertEqual(original["fields"], row["expected"])
            data = pathlib.Path(row["source"]).read_bytes()
            self.assertEqual(sha(data), row["source_sha256"])
            staged_root = directory / "source"
            staged = staged_root / relative
            staged.parent.mkdir(parents=True)
            compile_error = row["expected"]["outcome"] == "compile-error"
            if compile_error:
                staged.write_bytes(data)
                reference, stdout = original, original_stdout
                observed["unmodified_reference"] = reference
            else:
                label = "cint-parity:" + case + ":" + entry + ":"
                wrapper = ('\nvoid main() {\n    I64 parity_result = ' + entry +
                           '();\n    "' + label + '{parity_result}\\n";\n}\n').encode("ascii")
                staged.write_bytes(data + wrapper)
                self.assertEqual(staged.read_bytes()[:len(data)], data)
                reference, stdout = self.reference_run(
                    staged, relative, "main", directory, "wrapped-reference")
                observed["wrapped_reference"] = reference
                self.assertEqual(reference["outcome"], "value")
                self.assertEqual(row["expected"]["return"].split(" ", 1)[0], "I64")
                self.assertEqual(stdout, original_stdout + (
                    label + row["expected"]["return"].split(" ", 1)[1] + "\n").encode("ascii"))
            observed["staged_source_sha256"] = sha(staged.read_bytes())
            observed["staged_source"] = str(staged)
            receipt_path = directory / "cli-receipt.json"
            result = self.invoke(["--json", "--root", staged_root,
                                  "--receipt", receipt_path, relative])
            invocation = self.out / ("%03d-%s.json" % (self.serial, self._testMethodName))
            for channel in ("stdout", "stderr"):
                (directory / ("cli." + channel)).write_bytes(getattr(result, channel))
            observed["cli"] = {"exit_status": result.returncode,
                               "stdout_bytes": len(result.stdout), "stdout_sha256": sha(result.stdout),
                               "stderr_sha256": sha(result.stderr), "receipt": str(receipt_path),
                               "invocation": str(invocation)}
            if receipt_path.is_file():
                receipt = json.loads(receipt_path.read_bytes())
                observed["cli"]["receipt_identity_sha256"] = receipt["observations"]["receipt_identity_sha256"]
                observed["cli"]["fuel_consumed"] = receipt["outcome"]["fuel_consumed"]
                observed["cli"]["generated_modules"] = parity.generated_modules(receipt)
            self.assertEqual(result.returncode, reference["exit_status"], result.stderr)
            self.assertEqual(result.stdout, stdout)
            fuel = reference["fields"].get("fuel-consumed")
            receipt = self.check_receipt(receipt_path, result.stdout, result.returncode,
                                         staged, relative, fuel=fuel)
            if compile_error:
                diagnostic = self.one_json(result, "CINT-DIAG-1", 2)
                observed["cli"]["diagnostic"] = diagnostic
                self.assertEqual(diagnostic["code"], reference["fields"]["diagnostic.code"])
                path, line, column = reference["fields"]["diagnostic.position"].rsplit(":", 2)
                self.assertEqual((diagnostic["file"], diagnostic["line"], diagnostic["column"]),
                                 (path, int(line), int(column)))
                self.assertIsNone(receipt["identity"]["generated"])
            else:
                self.assertEqual(result.stderr, b"")
                modules = observed["cli"]["generated_modules"]
                c_files = [item for name, item in modules.items() if name.endswith(".c")]
                sir_files = [name for name in modules if name.endswith(".sites")]
                self.assertEqual(len(c_files), 1, modules)
                self.assertEqual(len(sir_files), 1, modules)
                if case in parity.BRACE_CASES:
                    self.assertLessEqual(c_files[0]["brace_depth"], 2)
            observed["pass"] = True
        except Exception as error:
            observed["failure"] = str(error)
            raise
        finally:
            self.parity_report["observations"].append(observed)
            self.write_parity_report()


def parity_suite(names=None):
    tests = []
    for group, cases in (("boot", parity.BOOT), ("boundary", parity.BOUNDARIES)):
        for case in cases:
            for row in parity.frozen_cases(case):
                name = "test_parity_" + case.replace("/", "_") + "_" + row["entry"]
                def test(self, group=group, row=row):
                    self.parity_case(group, row)
                setattr(ParityTests, name, test)
                if names is None or name in names:
                    tests.append(name)
    if names is not None and set(names) - set(tests):
        raise ValueError("unknown parity case: %s" % sorted(set(names) - set(tests)))
    return tests, unittest.TestSuite(ParityTests(name) for name in tests)


class EvidenceResult(unittest.TextTestResult):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.successes = []

    def addSuccess(self, test):
        super().addSuccess(test)
        self.successes.append(str(test))


def main():
    global CONFIG
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--leg", required=True, choices=("msvc", "gcc", "clang"))
    parser.add_argument("--exe", type=pathlib.Path)
    base = pathlib.Path(os.environ.get("CINT_BUILD") or os.path.join(tempfile.gettempdir(), "cint-build"))
    parser.add_argument("--out", type=pathlib.Path, default=base / "cli")
    parser.add_argument("--case", action="append", help="run one named test method")
    parser.add_argument("--suite", choices=("all", "cli", "compiler", "parity"), default="all",
                        help="all includes inherited compiler contract checks")
    CONFIG = parser.parse_args()
    if CONFIG.exe is None:
        CONFIG.exe = base / "boot" / CONFIG.leg / ("cint.exe" if CONFIG.leg == "msvc" else "cint")
    if not CONFIG.exe.is_file():
        parser.error("bootstrap executable does not exist: %s" % CONFIG.exe)
    tag = "cases" if CONFIG.case else CONFIG.suite
    leg_out = CONFIG.out.resolve() / CONFIG.leg
    leg_out.mkdir(parents=True, exist_ok=True)
    CONFIG.run_out = pathlib.Path(tempfile.mkdtemp(prefix=tag + "-", dir=leg_out))
    exe_before = sha(CONFIG.exe.read_bytes())
    loader = unittest.TestLoader()
    if CONFIG.suite == "parity":
        names, suite = parity_suite(CONFIG.case)
    else:
        names = CONFIG.case or loader.getTestCaseNames(RunTests)
        if CONFIG.suite != "all":
            names = [name for name in names if name.startswith("test_compiler_") ==
                     (CONFIG.suite == "compiler")]
        suite = unittest.TestSuite(RunTests(name) for name in names)
    result = unittest.TextTestRunner(verbosity=2, resultclass=EvidenceResult).run(suite)
    exe_after = sha(CONFIG.exe.read_bytes())
    report = {"leg": CONFIG.leg, "suite": CONFIG.suite, "exe": str(CONFIG.exe.resolve()),
              "exe_sha256_before": exe_before, "exe_sha256_after": exe_after,
              "exe_unchanged": exe_before == exe_after, "cases": names,
              "evidence": str(CONFIG.run_out), "tests": result.testsRun,
              "successes": result.successes,
              "failures": [{"test": str(test), "traceback": trace,
                            "boundary": "compiler" if test._testMethodName.startswith("test_compiler_") else "cli"}
                           for test, trace in result.failures + result.errors],
              "skipped": [{"test": str(test), "reason": reason} for test, reason in result.skipped],
              "pass": result.wasSuccessful() and exe_before == exe_after}
    if exe_before != exe_after:
        report["failures"].append({"test": "executable changed during suite", "boundary": "environment",
                                  "traceback": "the executable hashes before and after differ"})
    raw = canonical(report) + b"\n"
    (CONFIG.run_out / "test-summary.json").write_bytes(raw)
    (leg_out / ("test-summary-%s.json" % tag)).write_bytes(raw)
    failed_methods = {str(getattr(test, "test_case", test))
                      for test, _ in result.failures + result.errors}
    print("cli: %d passed, %d failed, %d skipped" %
          (len(result.successes), len(failed_methods) + int(exe_before != exe_after), len(result.skipped)))
    print("evidence: %s" % CONFIG.run_out)
    if CONFIG.suite == "parity" and hasattr(ParityTests, "parity_report"):
        observations = ParityTests.parity_report["observations"]
        print("parity: boot %d/3, boundary %d/18, waiting 2, seed-only excluded 16" % (
            sum(row["pass"] for row in observations if row["group"] == "boot"),
            sum(row["pass"] for row in observations if row["group"] == "boundary")))
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    sys.exit(main())
