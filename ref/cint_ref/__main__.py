"""Command line of the reference: `python -m cint_ref run | check | cif1 | sig`.

Put `ref/` on the module path first (for example `PYTHONPATH=ref`, or run
from inside `ref/`).

  run   <case.ci> [options]                 print the `.expect` text of the outcome
  check <case.ci> <case.expect> [options]   exit 0 on agreement, else print the first differing line
  cif1  <fixtures.cif1.jsonl>               evaluate every CIF-1 record; print disagreements
                                            and invalid records (either exits 1)
  sig   <case.ci> [--path P]                print the canonical type signature (SPEC-03 A-20) of
                                            each export of the root module, one line
                                            `<name> <lowercase hex>` in declaration order;
                                            `<name> -` for an export whose type holds a form of
                                            SPEC-03 A-25, which gets no wrapper (sig.py)

Options for run and check: --entry NAME (a function), --test NAME (a test
block), --arg "<Type> <value>" (repeatable), --fuel N, --depth N, --path P
(the module-relative path used in positions; imported modules are read
relative to the directory it is relative to), and for run also --case,
--clause and --stdout FILE, which writes the bytes the program's print
statements wrote, unchanged, to FILE (SPEC-06 3.4a; decision
2026-10-03, slice 2 patch D-19), and --format 1|2|3|4, the `.expect` format
(SPEC-09 CONF-11; default 1). An `error` outcome has no text below format 3
(CONF-11 rule 12): `run` then exits 2 with a message. `check` compares in the
format of the file it is given. `sig` exits 1, after one line, when the program
is a compile error or cint_ref refuses it. Output is written as bytes with LF
line ends on every platform.
"""
from __future__ import annotations

import argparse
import os
import sys

from . import cif1, expect, sig
from .exec import DEFAULT_DEPTH, run_program
from .faults import UNASSIGNED, CompileError, Refused
from .types import BOOL, Value, is_int_type


def _write(text: str):
    sys.stdout.buffer.write(text.encode("ascii", "backslashreplace"))
    sys.stdout.buffer.flush()


def module_path(file_path: str) -> str:
    """Path relative to the nearest enclosing `conformance` directory, `/`-separated (ref/OPEN.md REF-OQ-13)."""
    parts = os.path.normpath(os.path.abspath(file_path)).split(os.sep)
    for i in range(len(parts) - 2, -1, -1):
        if parts[i] == "conformance":
            return "/".join(parts[i + 1:])
    return parts[-1]


def source_root(file_path: str, path: str):
    """The directory that `path`, the module-relative path of `file_path`, is relative to;
    imports are read from it (SPEC-04 LS-225). None when `path` is not a suffix of the file."""
    full = os.path.normpath(os.path.abspath(file_path))
    rel = os.path.normpath(path)
    if not full.endswith(os.sep + rel):
        return None
    return full[:-len(rel) - 1]


def parse_arg(text: str) -> Value:
    t, _, v = text.strip().partition(" ")
    v = v.strip()
    if t == BOOL and v in ("true", "false"):
        return Value(BOOL, v == "true")
    if is_int_type(t):
        try:
            return Value(t, int(v, 10))
        except ValueError:
            pass
    raise argparse.ArgumentTypeError("an argument is `<Type> <decimal>` or `Bool true|false`: %r" % text)


def _int(text):
    try:
        return int(text, 10)
    except ValueError:
        raise argparse.ArgumentTypeError("not a decimal integer: %r" % text)


def _add_run_options(p, with_names):
    p.add_argument("--entry")
    p.add_argument("--test")
    p.add_argument("--arg", action="append", type=parse_arg, default=[])
    p.add_argument("--fuel", type=_int)
    p.add_argument("--depth", type=_int, default=DEFAULT_DEPTH)
    p.add_argument("--path")
    if with_names:
        p.add_argument("--case")
        p.add_argument("--clause")
        p.add_argument("--stdout", metavar="FILE", help="also write the program's output bytes to FILE")
        p.add_argument("--format", type=int, choices=(1, 2, 3, 4), default=1, help="the .expect format (CONF-11)")


def _outcome(ns):
    with open(ns.source, "rb") as f:
        data = f.read()
    path = ns.path or module_path(ns.source)
    o = run_program(data, path, ns.entry, ns.fuel, ns.depth, args=ns.arg, test=ns.test,
                    root=source_root(ns.source, path))
    case = path[:-3] if path.endswith(".ci") else path
    return o, case


def cmd_run(ns) -> int:
    o, case = _outcome(ns)
    if ns.stdout is not None:
        with open(ns.stdout, "wb") as f:
            f.write(o.stdout)
    try:
        e = expect.from_outcome(o, ns.case or case, ns.clause, fmt=ns.format)
    except ValueError as x:
        sys.stderr.write("%s\n" % x)
        return 2
    _write(expect.render(e))
    return 0



def cmd_check(ns) -> int:
    with open(ns.expect, "rb") as f:
        raw = f.read()
    try:
        want = expect.parse(raw.decode("ascii"))
    except (UnicodeDecodeError, ValueError) as x:
        _write("%s: not a valid .expect file: %s\n" % (ns.expect, x))
        return 2
    o, _ = _outcome(ns)
    try:
        got = expect.from_outcome(o, want.get("case"), want.get("clause"), want.get("source"), want.format)
    except ValueError as x:
        _write("%s: %s\n" % (ns.expect, x))
        return 1
    diff = expect.first_difference(want, got)
    if diff is None:
        return 0
    n, a, b = diff
    _write("%s: line %d differs\n  expected: %s\n  actual:   %s\n" % (ns.expect, n, a or "(no line)", b or "(no line)"))
    return 1


def cmd_cif1(ns) -> int:
    total = agree = disagree = unsupported = invalid = 0
    try:
        records = list(cif1.read_file(ns.fixtures))
    except (ValueError, UnicodeDecodeError) as x:
        _write("%s: %s\n" % (ns.fixtures, x))
        return 2
    for n, r in records:
        total += 1
        try:
            problems = cif1.check(r)
        except cif1.Unsupported as x:
            unsupported += 1
            if ns.verbose:
                _write("%s: unsupported: %s\n" % (r.id, x))
            continue
        except cif1.Invalid as x:
            invalid += 1
            _write("%s (line %d): invalid fixture: %s\n" % (r.id, n, x))
            continue
        except ValueError as x:
            problems = [str(x)]
        if problems:
            disagree += 1
            _write("%s (line %d): %s\n" % (r.id, n, "; ".join(problems)))
        else:
            agree += 1
    _write("cif1: %d records, %d agree, %d disagree, %d unsupported, %d invalid\n"
           % (total, agree, disagree, unsupported, invalid))
    return 1 if disagree or invalid else 0


def cmd_sig(ns) -> int:
    with open(ns.source, "rb") as f:
        data = f.read()
    path = ns.path or module_path(ns.source)
    try:
        exports = sig.program_exports(data, path, source_root(ns.source, path))
    except CompileError as e:
        d = e.diagnostic
        _write("compile-error %s %s\n" % (d.code or UNASSIGNED, d.position))
        return 1
    except Refused as r:
        _write("refused: %s\n" % r.message)
        return 1
    _write("".join("%s %s\n" % (e.name, "-" if e.signature is None else e.signature.hex()) for e in exports))
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="python -m cint_ref", description="cint_ref, the exact reference for cint-core-1")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("run", help="run a case and print its .expect text")
    p.add_argument("source")
    _add_run_options(p, True)
    p = sub.add_parser("check", help="compare a case with its .expect file")
    p.add_argument("source")
    p.add_argument("expect")
    _add_run_options(p, False)
    p = sub.add_parser("cif1", help="evaluate a CIF-1 fixture file")
    p.add_argument("fixtures")
    p.add_argument("--verbose", action="store_true", help="also list unsupported records")
    p = sub.add_parser("sig", help="print the canonical type signature of each export")
    p.add_argument("source")
    p.add_argument("--path", help="the module-relative path (default: below the nearest conformance directory)")
    ns = ap.parse_args(argv)
    return {"run": cmd_run, "check": cmd_check, "cif1": cmd_cif1, "sig": cmd_sig}[ns.cmd](ns)


if __name__ == "__main__":
    sys.exit(main())
