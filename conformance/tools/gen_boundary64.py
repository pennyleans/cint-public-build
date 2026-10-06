"""Boundary-matrix CIF-1 tables (SPEC-09 9.3, category "I64 boundary matrix").

All ordered pairs from the specified value set, for each binary operator form:
16 x 16 = 256 records per form, in the order the set is listed (left operand
outer, right operand inner; the set is listed in ascending order, so this is
also sorted operand order). One file per (op, form, type).

SPEC-09 9.3 gives the value set for I64 only. For U64, U32 and I32 it says
"analogous boundary matrices" without listing values, so this generator refuses
those types until the specification lists them (ref/OPEN.md, entry O-3).

Usage:
    python gen_boundary64.py --type I64 --form checked --op add --out conformance/integer-machine/bnd64/
    python gen_boundary64.py --batch all --out conformance/integer-machine/bnd64/

Python standard library only; no floating point; LF line endings.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import imeval  # noqa: E402
from gen_exhaustive8 import ALL_FORMS  # noqa: E402

M64 = (1 << 63) - 1
m64 = -(1 << 63)

# SPEC-09 9.3, verbatim order: {MIN, MIN+1, -2^32, -2^31, -2, -1, 0, 1, 2,
# 2^31-1, 2^31, 2^32, 3037000499, 3037000500, MAX-1, MAX}.
VALUES = {
    "I64": [m64, m64 + 1, -(1 << 32), -(1 << 31), -2, -1, 0, 1, 2,
            (1 << 31) - 1, 1 << 31, 1 << 32, 3037000499, 3037000500, M64 - 1, M64],
}


class Unspecified(Exception):
    """The specification does not list a value set for this type."""


def file_name(op, form, t):
    return "%s.cif1.jsonl" % imeval.op_id(op, form, t)


def records(op, form, t):
    if t not in VALUES:
        raise Unspecified("SPEC-09 9.3 lists no boundary values for %s (ref/OPEN.md O-3)" % t)
    vals = VALUES[t]
    n = 0
    for a in vals:
        for b in vals:
            rid = "%s.%03d" % (imeval.op_id(op, form, t), n)
            yield imeval.record(rid, "S", op, form, t, a, b)
            n += 1


def render(op, form, t):
    return imeval.render_records(records(op, form, t))


def write(op, form, t, out_dir):
    data = render(op, form, t)
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, file_name(op, form, t))
    imeval.write_bytes(path, data)
    return path


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--type", default="I64")
    p.add_argument("--form", choices=("checked", "wrap", "sat"))
    p.add_argument("--op", choices=sorted(imeval.BINARY_OPS))
    p.add_argument("--batch", choices=("all",))
    p.add_argument("--out", required=True)
    args = p.parse_args(argv)
    if args.batch:
        jobs = [(op, form, args.type) for op, form in ALL_FORMS]
    else:
        if not (args.form and args.op):
            p.error("give --form and --op, or --batch all")
        jobs = [(args.op, args.form, args.type)]
    try:
        for op, form, t in jobs:
            print(write(op, form, t, args.out))
    except Unspecified as e:
        print("refused: %s" % e, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
