"""Bootstrap source framing, snapshots and independently checked compiler digests."""
import hashlib
import pathlib
import struct
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

try:
    import cint_bootstrap as boot
except ImportError:
    boot = None


class Bootstrap(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(boot, "the bootstrap implementation is missing")

    def test_manifest_uses_sec_rec_3_framing_and_byte_order(self):
        framed = boot.source_manifest({"compiler/z.ci": b"abc", "compiler/a.ci": b""})
        expected = b"CISRC001" + struct.pack("<I", 2)
        for path, data in ((b"compiler/a.ci", b""), (b"compiler/z.ci", b"abc")):
            expected += struct.pack("<H", len(path)) + path
            expected += struct.pack("<Q", len(data)) + hashlib.sha256(data).digest()
        self.assertEqual(framed, expected)

    def test_manifest_rejects_paths_outside_the_source_root(self):
        for path in ("../x.ci", "/x.ci", "a/../x.ci", "a\\x.ci", "a//x.ci", "C:/x.ci"):
            with self.subTest(path=path), self.assertRaises(ValueError):
                boot.source_manifest({path: b""})

    def test_manifest_rejects_unrepresentable_path_length(self):
        with self.assertRaises(ValueError):
            boot.source_manifest({"a" * 65536: b""})

    def test_snapshot_retains_exact_inputs_after_checkout_changes(self):
        with tempfile.TemporaryDirectory() as temp:
            root, dest = pathlib.Path(temp) / "repo", pathlib.Path(temp) / "inputs"
            for name, data in (("compiler/main.ci", b"source\r\n"), ("seed/seed_main.c", b"seed"),
                               ("rt/cint_rt.c", b"runtime"), ("rt/cint_rt.h", b"header"),
                               ("cli/cint_main.c", b"cli"), ("compiler/tests/golden_host.c", b"host")):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            (root / "compiler" / "OPEN.md").write_bytes(b"excluded documentation")
            groups = boot.snapshot_inputs(root, dest)
            (root / "compiler" / "main.ci").write_bytes(b"changed")
            self.assertEqual((dest / "compiler" / "main.ci").read_bytes(), b"source\r\n")
            self.assertEqual(groups["compiler"], {"compiler/main.ci": b"source\r\n"})
            self.assertEqual(groups["runtime"], {"rt/cint_rt.c": b"runtime", "rt/cint_rt.h": b"header"})
            self.assertEqual(groups["host"], {"compiler/tests/golden_host.c": b"host"})
            self.assertFalse((dest / "compiler" / "OPEN.md").exists())

    def test_snapshot_refuses_linked_build_inputs(self):
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp) / "repo"
            (root / "compiler").mkdir(parents=True)
            target = pathlib.Path(temp) / "target.ci"
            target.write_bytes(b"source")
            try:
                (root / "compiler" / "main.ci").symlink_to(target)
            except OSError:
                self.skipTest("symlink creation is unavailable")
            with self.assertRaises(ValueError):
                boot.snapshot_inputs(root, pathlib.Path(temp) / "inputs")

    def test_compiler_manifest_only_contains_the_import_closure(self):
        sources = {"compiler/main.ci": b"// import absent;\nimport dep;\n",
                   "compiler/dep.ci": b"/* import absent; */\nexport I64 f() { return 0; }\n",
                   "compiler/unread.ci": b"unused"}
        self.assertEqual(boot.compiler_closure(sources),
                         {"compiler/dep.ci": sources["compiler/dep.ci"],
                          "compiler/main.ci": sources["compiler/main.ci"]})

    def test_compiler_import_missing_from_snapshot_is_an_error(self):
        with self.assertRaises(ValueError):
            boot.compiler_closure({"compiler/main.ci": b"import absent;\n"})

    def test_digest_verification_rejects_wrong_or_missing_compiler_results(self):
        with tempfile.TemporaryDirectory() as temp:
            path = pathlib.Path(temp) / "source"
            path.write_bytes(b"abc")
            expected = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
            self.assertEqual(boot.verify_digest_output(("digest " + expected + "\n").encode(), [path]),
                             {str(path): expected})
            for output in (b"", b"digest error unreadable\n", b"digest " + b"0" * 64 + b"\n",
                           ("digest " + expected + "\ndigest " + expected + "\n").encode()):
                with self.subTest(output=output), self.assertRaises(ValueError):
                    boot.verify_digest_output(output, [path])

    def test_toolchain_file_keeps_arguments_and_environment_values_separate(self):
        data = boot.toolchain_bytes({"cc": "C:/compiler path/cl.exe", "compiler_sources": "F:/inputs/compiler.cisrc"},
                                    ["/std:c17", "/DNAME=1"], ["F:/inputs/rt/cint_rt.c"],
                                    {"INCLUDE": "C:/sdk path/include;C:/include"})
        self.assertEqual(data, b"cint-toolchain-1\ncc C:/compiler path/cl.exe\n"
                         b"compiler_sources F:/inputs/compiler.cisrc\nflag /std:c17\nflag /DNAME=1\n"
                         b"runtime_source F:/inputs/rt/cint_rt.c\nenv INCLUDE=C:/sdk path/include;C:/include\n")

    def test_toolchain_file_refuses_line_injection(self):
        with self.assertRaises(ValueError):
            boot.toolchain_bytes({"cc": "cc\nflag malicious"}, [], [], {})

    def test_executable_binding_hashes_exact_linked_bytes(self):
        with tempfile.TemporaryDirectory() as temp:
            executable = pathlib.Path(temp) / "cint"
            executable.write_bytes(b"abc")
            config = boot.executable_toolchain_bytes(executable, {"cc": "gcc"}, [], [], {})
            self.assertEqual(config, b"cint-toolchain-1\ncc gcc\nexecutable_sha256 "
                             b"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n")
            executable.write_bytes(b"")
            changed = boot.executable_toolchain_bytes(executable, {"cc": "gcc"}, [], [], {})
            self.assertEqual(changed, b"cint-toolchain-1\ncc gcc\nexecutable_sha256 "
                             b"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\n")

    def test_cli_compiler_identity_binding_does_not_change_program_flags(self):
        original = ["-std=c17", "-O0"]
        digest = "a" * 64
        for leg, prefix in (("msvc", "/D"), ("gcc", "-D"), ("clang", "-D")):
            flags = boot.cli_compile_flags(original, leg, digest)
            self.assertEqual(flags, original + [prefix + 'CINT_COMPILER_SOURCE_IDENTITY="' + digest + '"'])
            self.assertEqual(original, ["-std=c17", "-O0"])

    def test_cli_binding_refuses_invalid_digests(self):
        for digest in ("", "a" * 63, "A" * 64, "x\nflag malicious"):
            with self.subTest(digest=digest), self.assertRaises(ValueError):
                boot.cli_compile_flags([], "gcc", digest)

    def test_output_root_cannot_overlap_checkout(self):
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp) / "repo"
            for out in (root, root / "build", pathlib.Path(temp)):
                with self.subTest(out=out), self.assertRaises(ValueError):
                    boot.validate_output_root(root, out)
            boot.validate_output_root(root, pathlib.Path(temp) / "build")


if __name__ == "__main__":
    unittest.main()
