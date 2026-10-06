"""run a command and compare stdout as bytes, including its exit status."""
from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expect", required=True, type=pathlib.Path)
    parser.add_argument("--exit-status", type=int, default=0)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("a command is required after --")
    try:
        expected = args.expect.read_bytes()
        result = subprocess.run(command, stdin=subprocess.DEVNULL, capture_output=True,
                                check=False)
    except OSError as error:
        print("comparison error: %s" % error, file=sys.stderr)
        return 2
    sys.stderr.buffer.write(result.stderr)
    if result.returncode != args.exit_status:
        print("exit status %d, expected %d" % (result.returncode, args.exit_status),
              file=sys.stderr)
        return 1
    if result.stdout != expected:
        offset = next((i for i, pair in enumerate(zip(expected, result.stdout))
                       if pair[0] != pair[1]), min(len(expected), len(result.stdout)))
        print("different at byte %d: expected %d bytes, got %d bytes" %
              (offset, len(expected), len(result.stdout)), file=sys.stderr)
        return 1
    sys.stdout.buffer.write(("identical: %d bytes\n" % len(expected)).encode("ascii"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
