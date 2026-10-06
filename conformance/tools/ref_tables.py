"""Run every table program through `cint_ref` and compare with its CIF-1 source.

Each `conformance/tables/<name>.ci` applies one operator to its two
parameters, and line n of `<name>.cases` is record n of the CIF-1 file named in
the program's `// source:` header (`tools/gen_tables.py`). This script parses
each table program once with `cint_ref`, calls the exported function for every
case line with a fresh entry (SPEC-01 9.4: fuel and depth per entry), and checks
that the program-level outcome carries the CIF-1 record's operation-level
values (SPEC-01 13.1, the mapping between the two layers):

- a `value` record: outcome `value`, and the return value equals it;
- a `fault` record: outcome `fault`, the same code, the record's operation
  identifier, the two arguments as operands, and `exact` and `limit` where
  the record asserts them.

It also checks that every case line names the record's arguments, and it
reports the fuel consumed and the fault positions seen, for review.

Usage (from the repository root):
    python conformance/tools/ref_tables.py [conformance/tables/<name>.ci ...]

Python standard library only. No floating point. Exit status 0 when every case
agrees.
"""

import collections
import glob
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "ref"))

from cint_ref import cif1  # noqa: E402
from cint_ref import parser as P  # noqa: E402
from cint_ref.check import check_module  # noqa: E402
from cint_ref.exec import DEFAULT_DEPTH, _check_args, _run  # noqa: E402
from cint_ref.lexer import decode_source  # noqa: E402
from cint_ref.types import Value  # noqa: E402


def header(src: str, key: str) -> str:
    for line in src.split("\n"):
        if line.startswith("// %s: " % key):
            return line[len("// %s: " % key):]
    raise ValueError("no `// %s:` header" % key)


def compare(rec, outcome):
    """Differences between a CIF-1 record and a program-level outcome (empty when they agree)."""
    kind, payload = next(iter(rec.expect.items()))
    if outcome.kind not in ("value", "fault"):
        return ["outcome %s: %s" % (outcome.kind, outcome.message)]
    if kind == "value":
        if outcome.kind != "value":
            return ["expected value, got fault %s" % outcome.record.code]
        want = cif1._value(payload)
        if outcome.value != want:
            return ["expected %s, got %s" % (want.render(), outcome.value.render())]
        return []
    if outcome.kind != "fault":
        return ["expected fault %s, got value %s" % (payload["code"], outcome.value.render())]
    f = outcome.record
    problems = []
    if f.code != payload["code"]:
        problems.append("code %s, expected %s" % (f.code, payload["code"]))
    if f.operation != rec.op:
        problems.append("operation %s, expected %s" % (f.operation, rec.op))
    args = tuple(cif1._value(a) for a in rec.args)
    if tuple(f.operands) != args:
        problems.append("operands %s, expected %s" % ([o.render() for o in f.operands], [a.render() for a in args]))
    if "exact" in payload and f.exact != int(payload["exact"]):
        problems.append("exact %s, expected %s" % (f.exact, payload["exact"]))
    if "limit" in payload and (f.limit is None or f.limit.value != int(payload["limit"])):
        problems.append("limit %s, expected %s" % (f.limit, payload["limit"]))
    return problems


def run_table(ci_path: str, report) -> tuple:
    with open(ci_path, "rb") as fh:
        data = fh.read()
    text = data.decode("ascii")
    rel = os.path.relpath(ci_path, os.path.join(ROOT, "conformance")).replace(os.sep, "/")
    source = os.path.join(ROOT, "conformance", header(text, "source"))
    cases_path = ci_path[:-3] + ".cases"
    with open(cases_path, "rb") as fh:
        case_lines = fh.read().decode("ascii").split("\n")
    if case_lines[-1] != "":
        raise ValueError("%s: no final LF" % cases_path)
    case_lines.pop()
    records = [r for _, r in cif1.read_file(source)]
    if len(records) != len(case_lines):
        raise ValueError("%s: %d cases for %d records" % (rel, len(case_lines), len(records)))

    prog = check_module(P.parse_module(decode_source(data, rel), rel))
    agree = 0
    fuel = collections.Counter()
    positions = collections.Counter()
    for n, (line, rec) in enumerate(zip(case_lines, records), start=1):
        parts = line.split(" ")
        if len(parts) != 6:
            raise ValueError("%s line %d: expected `<module> <function> <T> <a> <T> <b>`" % (cases_path, n))
        module, fname, ta, a, tb, b = parts
        args = [Value(ta, int(a)), Value(tb, int(b))]
        if args != [cif1._value(x) for x in rec.args]:
            report("%s line %d: case arguments differ from record %s" % (rel, n, rec.id))
            continue
        f = prog.functions[fname]
        outcome = _run(prog, "function", f, _check_args(f, args), None, DEFAULT_DEPTH)
        problems = compare(rec, outcome)
        fuel[outcome.fuel] += 1
        if outcome.record is not None:
            positions[str(outcome.record.position)] += 1
        if problems:
            report("%s line %d (%s): %s" % (rel, n, rec.id, "; ".join(problems)))
        else:
            agree += 1
    return len(records), agree, fuel, positions


def main(argv=None):
    paths = argv if argv else sorted(glob.glob(os.path.join(ROOT, "conformance", "tables", "*.ci")))
    total = agree = 0
    shown = [0]

    def report(msg):
        shown[0] += 1
        if shown[0] <= 50:
            print(msg)

    for p in paths:
        n, a, fuel, positions = run_table(p, report)
        total += n
        agree += a
        print("%s: %d cases, %d agree; fuel %s; fault positions %s" % (
            os.path.basename(p), n, a, dict(sorted(fuel.items())), dict(sorted(positions.items()))))
    print("tables: %d programs, %d cases, %d agree, %d disagree" % (len(paths), total, agree, total - agree))
    return 0 if agree == total else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
