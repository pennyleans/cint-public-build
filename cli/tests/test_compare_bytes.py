"""byte comparison uses subprocess pipes rather than shell text redirection."""
from __future__ import annotations

import pathlib
import subprocess
import sys
import tempfile
import unittest

SCRIPT = pathlib.Path(__file__).with_name("compare_bytes.py")


class CompareBytesTests(unittest.TestCase):
    def invoke(self, expected, actual, status=0, expected_status=0, stderr=b""):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "expected.stdout"
            path.write_bytes(expected)
            child = ("import sys; sys.stdout.buffer.write(%r); "
                     "sys.stderr.buffer.write(%r); sys.exit(%d)") % (actual, stderr, status)
            return subprocess.run(
                [sys.executable, str(SCRIPT), "--expect", str(path),
                 "--exit-status", str(expected_status), "--", sys.executable, "-c", child],
                capture_output=True, check=False)

    def test_exact_binary_bytes(self):
        result = self.invoke(b"hello\n\x00\xff\r", b"hello\n\x00\xff\r", stderr=b"diagnostic\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, b"identical: 9 bytes\n")
        self.assertEqual(result.stderr, b"diagnostic\n")

    def test_crlf_is_not_lf(self):
        result = self.invoke(b"hello\n", b"hello\r\n")
        self.assertEqual(result.returncode, 1)
        self.assertIn(b"byte 5", result.stderr)
        self.assertNotIn(b"identical:", result.stdout)

    def test_equal_bytes_do_not_hide_wrong_status(self):
        result = self.invoke(b"partial", b"partial", status=1)
        self.assertEqual(result.returncode, 1)
        self.assertIn(b"exit status 1", result.stderr)

    def test_explicit_fault_status(self):
        result = self.invoke(b"partial", b"partial", status=1, expected_status=1)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, b"identical: 7 bytes\n")

    def test_missing_command_is_usage(self):
        result = subprocess.run([sys.executable, str(SCRIPT), "--expect", "unused", "--"],
                                capture_output=True, check=False)
        self.assertEqual(result.returncode, 2)


if __name__ == "__main__":
    unittest.main()
