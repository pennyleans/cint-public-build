"""Identifies the executable that actually emitted a policy unit."""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ManifestTests(unittest.TestCase):
    def test_policy_manifest_identifies_actual_compiler(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            files = {}
            for name, content in [("source", b"source"), ("compiler", b"product-stage-two"),
                                  ("bundle", b"bundle"), ("emitted", b"generated")]:
                files[name] = directory / name
                files[name].write_bytes(content)
            output = directory / "manifest.json"
            result = subprocess.run(["cmake", f"-DOUTPUT={output}", f"-DROOT={directory}",
                f"-DSOURCES={files['source']}", f"-DCOMPILER={files['compiler']}",
                f"-DBUNDLE={files['bundle']}", f"-DEMITTED={files['emitted']}",
                "-P", str(ROOT / "cmake/cint_manifest.cmake")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            manifest = json.loads(output.read_text(encoding="ascii"))
            self.assertEqual(manifest["compiler_sha256"], hashlib.sha256(files["compiler"].read_bytes()).hexdigest())
            self.assertNotIn("bootstrap_sha256", manifest)
            self.assertEqual(manifest["generated_sha256"], hashlib.sha256(files["emitted"].read_bytes()).hexdigest())


if __name__ == "__main__":
    unittest.main()
