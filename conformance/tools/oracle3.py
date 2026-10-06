"""A third derivation of the generated CIF-1 tables, for the T0 receipt.

`gen_exhaustive8.py` and `gen_boundary64.py` write their records with
`imeval.py`, and `cint_ref` checks them. This script derives every record of
`integer-machine/exh8/*.cif1.jsonl` and `integer-machine/bnd64/*.cif1.jsonl`
a third time, straight from SPEC-01 4.1 (checked, wrapping and saturating
forms), 4.2 (wrapping and saturation), 4.4 (floor division), 4.6 (shifts) and
9.2 (the fault fields `exact` and `limit`), and compares the `expect` object
of each record with it. A negative shift count asserts only the code
(ref/OPEN.md O-2). It shares no code with `imeval.py` or `cint_ref`; it was
written by the same agent from the same text, so it is a cross-check, not an
independent oracle.

Usage (from the repository root):
    python conformance/tools/oracle3.py [conformance/integer-machine]

Prints `oracle3: <records> records, <mismatches> mismatches` and exits 1 on any
mismatch. Python standard library only; integers only, no floating point.
"""

import collections
import glob
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT = os.path.join(os.path.dirname(HERE), "integer-machine")

RANGES = {"i8": (-128, 127), "u8": (0, 255), "i64": (-2 ** 63, 2 ** 63 - 1)}
WIDTH = {"i8": 8, "u8": 8, "i64": 64}


def wrap(t, v):
    lo, hi = RANGES[t]
    m = hi - lo + 1
    return (v - lo) % m + lo


def sat(t, v):
    lo, hi = RANGES[t]
    return max(lo, min(hi, v))


def derive(op, form, t, a, b):
    """("value", v) or ("fault", code, exact, limit); limit "neg" for a negative shift count."""
    lo, hi = RANGES[t]
    w = WIDTH[t]
    if op in ("shl", "shr"):
        if not (0 <= b <= w - 1):
            return ("fault", "E_SHIFT", None, w - 1 if b > w - 1 else "neg")
        if op == "shr":
            return ("value", a // (2 ** b))          # floor: arithmetic shift (SPEC-01 4.6)
        v = a * 2 ** b
    elif op in ("div", "rem"):
        if b == 0:
            return ("fault", "E_DIV_ZERO", None, None)
        q = a // b                                  # floor division (SPEC-01 4.4)
        v = q if op == "div" else a - b * q
    else:
        v = {"add": a + b, "sub": a - b, "mul": a * b}[op]
    if form == "wrap":
        return ("value", wrap(t, v))
    if form == "sat":
        return ("value", sat(t, v))
    if lo <= v <= hi:
        return ("value", v)
    return ("fault", "E_OVERFLOW", v, hi if v > hi else lo)


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    base = argv[0] if argv else DEFAULT
    stats = collections.Counter()
    bad = []
    paths = sorted(glob.glob(os.path.join(base, "exh8", "*.cif1.jsonl"))) + \
        sorted(glob.glob(os.path.join(base, "bnd64", "*.cif1.jsonl")))
    for path in paths:
        with open(path, "rb") as fh:
            raw = fh.read()
        if b"\r" in raw:
            bad.append((path, "CR found"))
            continue
        for line in raw.decode("ascii").split("\n")[:-1]:
            r = json.loads(line)
            op, form, t = r["op"].split(".")
            a, b = (int(x["v"]) for x in r["args"])
            if not all(x["t"].lower() == t for x in r["args"]):
                bad.append((r["id"], "argument type"))
                continue
            e = derive(op, form, t, a, b)
            stats["records"] += 1
            if e[0] == "value":
                ok = r["expect"] == {"value": {"t": t.upper(), "v": str(e[1])}}
            else:
                want = {"code": e[1]}
                if e[2] is not None:
                    want["exact"] = str(e[2])
                if e[3] not in (None, "neg"):
                    want["limit"] = str(e[3])
                ok = r["expect"].get("fault") == want
            if not ok:
                bad.append((r["id"], r["expect"], e))
    for b in bad[:10]:
        print("mismatch", b)
    print("oracle3: %d records, %d mismatches" % (stats["records"], len(bad)))
    return 1 if bad or not stats["records"] else 0


if __name__ == "__main__":
    sys.exit(main())
