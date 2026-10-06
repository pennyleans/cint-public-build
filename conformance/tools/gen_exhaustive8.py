"""Exhaustive 8-bit CIF-1 tables (SPEC-09 9.3, category "Exhaustive 8-bit").

Every pair of operands for each binary operator and form on I8 and U8. Both
operands range over the 8-bit type, shift counts included. Records are in
sorted operand order: left operand ascending, then right operand ascending.
One file per (op, form, type), named `<op>.<form>.<type>.cif1.jsonl`.

Usage:
    python gen_exhaustive8.py --type I8 --form checked --op add --out conformance/integer-machine/exh8/
    python gen_exhaustive8.py --batch t1 --out conformance/integer-machine/exh8/

`--batch t1` writes the 11 non-saturating forms for I8 and U8 (1,441,792
records), the T1 scope of SPEC-09 9.3. `--batch all` adds the 3 saturating
forms (T2 scope).

Python standard library only; no floating point; LF line endings.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import imeval  # noqa: E402

TYPES_8 = ("I8", "U8")

# SPEC-09 9.3: + - * / % << >> +% -% *% <<% are the 11 non-saturating forms.
T1_FORMS = (
    ("add", "checked"), ("sub", "checked"), ("mul", "checked"),
    ("div", "checked"), ("rem", "checked"), ("shl", "checked"), ("shr", "checked"),
    ("add", "wrap"), ("sub", "wrap"), ("mul", "wrap"), ("shl", "wrap"),
)
SAT_FORMS = (("add", "sat"), ("sub", "sat"), ("mul", "sat"))
ALL_FORMS = T1_FORMS + SAT_FORMS


def file_name(op, form, t):
    return "%s.cif1.jsonl" % imeval.op_id(op, form, t)


def records(op, form, t):
    if t not in TYPES_8:
        raise ValueError("exhaustive tables are defined for I8 and U8 only (SPEC-09 9.3)")
    values = range(imeval.lo(t), imeval.hi(t) + 1)
    n = 0
    for a in values:
        for b in values:
            rid = "%s.%05d" % (imeval.op_id(op, form, t), n)
            yield imeval.record(rid, "S", op, form, t, a, b)
            n += 1


def render(op, form, t):
    return imeval.render_records(records(op, form, t))


def write(op, form, t, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, file_name(op, form, t))
    imeval.write_bytes(path, render(op, form, t))
    return path


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--type", choices=TYPES_8)
    p.add_argument("--form", choices=("checked", "wrap", "sat"))
    p.add_argument("--op", choices=sorted(imeval.BINARY_OPS))
    p.add_argument("--batch", choices=("t1", "all"))
    p.add_argument("--out", required=True)
    args = p.parse_args(argv)
    if args.batch:
        forms = T1_FORMS if args.batch == "t1" else ALL_FORMS
        jobs = [(op, form, t) for t in TYPES_8 for op, form in forms]
    else:
        if not (args.type and args.form and args.op):
            p.error("give --type, --form and --op, or --batch")
        jobs = [(args.op, args.form, args.type)]
    for op, form, t in jobs:
        print(write(op, form, t, args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
