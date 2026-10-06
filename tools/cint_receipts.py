"""Compare conformance receipts and report their receipt identities (SPEC-09 RCPT-02, RCPT-03).

Usage (from the repository root):

    python tools/cint_receipts.py compare [--outcome] A B
    python tools/cint_receipts.py identity DIR

`compare` reports where two receipts differ in `identity` and `outcome` (with --outcome, in
`outcome` only), one JSON path per line; exit status 0 when they are the same, 1 otherwise.
When A and B are directories, it pairs their `receipt-*.json` files by name, prints each pair's
differences after the file name, and ends with `<what> identical in <k> of <n> receipt pairs`
(a file with no partner is a pair that differs). Plan task 2.3 uses this to show that the
harness reduction kept every T1 outcome; later tasks, that B1 and the seed, or two legs, reach
the same outcome.

`identity` reads every `receipt-*.json` (conformance receipts) and `run-*.json` (`cint run`
receipts, kind `run`, D-16) in DIR. For each it recomputes the receipt identity, SHA-256 over
the RFC 8785 bytes of `{"identity": ..., "outcome": ...}` (RCPT-03), checks it against
`observations.receipt_identity_sha256`, and checks that the file is its own RFC 8785
serialization followed by one LF (RCPT-02). A conformance receipt also needs `outcome.pass`;
a run receipt has no pass flag, so its row shows the outcome kind and exit status instead.
The last line is `<m> receipt identit(y|ies) in <n> receipt(s)`, for example `1 receipt
identity in 3 receipts` (plan milestone M1); exit status 0 when every file is well formed and
every conformance receipt passed, 1 otherwise.
Python standard library only; no floating point.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import sys


def canonical(obj) -> bytes:
    """RFC 8785 bytes of a receipt value (ASCII keys, strings, integers, booleans, null)."""
    return json.dumps(obj, sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode("ascii")


def receipt_identity(receipt: dict) -> str:
    return hashlib.sha256(canonical({"identity": receipt["identity"],
                                     "outcome": receipt["outcome"]})).hexdigest()


def load(path: pathlib.Path) -> dict:
    return json.loads(path.read_bytes().decode("ascii"))


def differences(a, b, path="") -> list:
    """JSON paths at which a and b differ, in key order."""
    if isinstance(a, dict) and isinstance(b, dict):
        out = []
        for key in sorted(set(a) | set(b)):
            sub = "%s/%s" % (path, key)
            if key not in a or key not in b:
                out.append("%s: only in %s" % (sub, "B" if key not in a else "A"))
            else:
                out += differences(a[key], b[key], sub)
        return out
    if isinstance(a, list) and isinstance(b, list) and len(a) == len(b):
        out = []
        for i, (x, y) in enumerate(zip(a, b)):
            out += differences(x, y, "%s/%d" % (path, i))
        return out
    if a == b and type(a) is type(b):
        return []
    return ["%s: %s != %s" % (path or "/", json.dumps(a)[:120], json.dumps(b)[:120])]


def check_file(path: pathlib.Path) -> dict:
    """One receipt: its recomputed identity, its pass flag, and any problem with its form."""
    data = path.read_bytes()
    problems = []
    try:
        receipt = json.loads(data.decode("ascii"))
    except (UnicodeDecodeError, ValueError) as e:
        return {"file": path.name, "identity": None, "pass": False,
                "problems": ["not ASCII JSON: %s" % e]}
    if data != canonical(receipt) + b"\n":
        problems.append("not its RFC 8785 serialization followed by one LF")
    ident = receipt_identity(receipt)
    if receipt.get("observations", {}).get("receipt_identity_sha256") != ident:
        problems.append("observations.receipt_identity_sha256 differs from the recomputed identity")
    schema = receipt["identity"].get("schema")
    if isinstance(schema, str) and schema.startswith("cint-receipt-1/"):
        outcome = receipt["outcome"]
        return {"file": path.name, "identity": ident, "pass": True, "problems": problems,
                "schema": schema, "status": "%s %s" % (outcome.get("kind"), outcome.get("exit_status"))}
    passed = receipt["outcome"].get("pass") is True and \
        receipt["outcome"].get("required", {}).get("pass", True) is True
    return {"file": path.name, "identity": ident, "pass": passed, "problems": problems,
            "schema": schema, "status": "pass" if passed else "fail"}


DEFAULT_PATTERNS = ("receipt-*.json", "run-*.json")


def identities(directory: pathlib.Path, names=None, patterns=DEFAULT_PATTERNS) -> list:
    """check_file() for each receipt in directory matching patterns (or each of names), sorted by
    file name."""
    paths = [directory / n for n in names] if names is not None else \
        sorted({p for pattern in patterns for p in directory.glob(pattern)},
               key=lambda p: p.name.encode("utf-8"))
    return [check_file(p) if p.is_file() else
            {"file": p.name, "identity": None, "pass": False, "problems": ["missing"], "status": "fail"}
            for p in paths]


def pair_differences(a: pathlib.Path, b: pathlib.Path, parts) -> list:
    ra, rb = load(a), load(b)
    return differences({k: ra.get(k) for k in parts}, {k: rb.get(k) for k in parts})


def compare(a: pathlib.Path, b: pathlib.Path, outcome_only: bool) -> int:
    parts = ("outcome",) if outcome_only else ("identity", "outcome")
    what = "outcome" if outcome_only else "identity and outcome"
    if not (a.is_dir() and b.is_dir()):
        diff = pair_differences(a, b, parts)
        for line in diff[:60]:
            print(line)
        print("compare: same %s" % what if not diff else
              "compare: %d differences in %s" % (len(diff), what))
        return 0 if not diff else 1
    names = sorted({p.name for d in (a, b) for p in d.glob("receipt-*.json")},
                   key=lambda n: n.encode("utf-8"))
    same = 0
    for name in names:
        if not (a / name).is_file() or not (b / name).is_file():
            print("%s: only in %s" % (name, "A" if (a / name).is_file() else "B"))
            continue
        diff = pair_differences(a / name, b / name, parts)
        for line in diff[:20]:
            print("%s: %s" % (name, line))
        same += not diff
    print("%s identical in %d of %d receipt pairs" % (what, same, len(names)))
    return 0 if names and same == len(names) else 1


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="command", required=True)
    cmp_ = sub.add_parser("compare", help="where two receipts differ")
    cmp_.add_argument("--outcome", action="store_true", help="compare `outcome` only")
    cmp_.add_argument("a")
    cmp_.add_argument("b")
    ident = sub.add_parser("identity", help="the receipt identities of a directory")
    ident.add_argument("--pattern", action="append",
                       help="file name glob, repeatable (default: receipt-*.json and run-*.json)")
    ident.add_argument("--one", action="store_true",
                       help="exit 1 unless the receipts share exactly one identity")
    ident.add_argument("directory")
    ns = ap.parse_args(argv)
    if ns.command == "compare":
        return compare(pathlib.Path(ns.a), pathlib.Path(ns.b), ns.outcome)
    rows = identities(pathlib.Path(ns.directory), patterns=tuple(ns.pattern or DEFAULT_PATTERNS))
    for r in rows:
        print("%s %s %s%s" % (r["file"], (r["identity"] or "-" * 64)[:16], r.get("status", "fail"),
                              "; " + "; ".join(r["problems"]) if r["problems"] else ""))
    distinct = {r["identity"] for r in rows if r["identity"]}
    print("%d receipt identit%s in %d receipt%s" % (len(distinct), "y" if len(distinct) == 1 else "ies",
                                                   len(rows), "" if len(rows) == 1 else "s"))
    if ns.one and len(distinct) != 1:
        return 1
    return 0 if rows and all(r["pass"] and not r["problems"] for r in rows) else 1


if __name__ == "__main__":
    sys.exit(main())
