"""A small exact-integer evaluator for the conformance generators.

Written from SPEC-01 sections 1.2, 2.1, 4.1, 4.2, 4.4, 4.6, 9.2 and 9.8 only.
It deliberately duplicates part of `ref/cint_ref` (plan Task 1.3): the two are
written separately and compared at the integration step. It covers the binary
operator forms that the exhaustive 8-bit tables and the boundary matrices use
(SPEC-09 9.3) and nothing else.

Python integers only. No floating point.
"""

import json

# SPEC-01 2.1: name -> (signed, width)
TYPES = {
    "I8": (True, 8), "I16": (True, 16), "I32": (True, 32), "I64": (True, 64),
    "U8": (False, 8), "U16": (False, 16), "U32": (False, 32), "U64": (False, 64),
    "I128": (True, 128), "I256": (True, 256), "I512": (True, 512), "I1024": (True, 1024),
}

# SPEC-01 4.1: the forms each binary operator has.
BINARY_OPS = {
    "add": ("checked", "wrap", "sat"),
    "sub": ("checked", "wrap", "sat"),
    "mul": ("checked", "wrap", "sat"),
    "div": ("checked",),
    "rem": ("checked",),
    "shl": ("checked", "wrap"),
    "shr": ("checked",),
}


def lo(t):
    """MIN(T), SPEC-01 1.2."""
    signed, w = TYPES[t]
    return -(1 << (w - 1)) if signed else 0


def hi(t):
    """MAX(T), SPEC-01 1.2."""
    signed, w = TYPES[t]
    return (1 << (w - 1)) - 1 if signed else (1 << w) - 1


def in_range(t, v):
    return lo(t) <= v <= hi(t)


def wrap(t, v):
    """wrap(T, v): the unique r in range with r = v (mod 2^w), SPEC-01 1.2."""
    _, w = TYPES[t]
    r = v % (1 << w)
    if r > hi(t):
        r -= 1 << w
    return r


def sat(t, v):
    """sat(T, v), SPEC-01 1.2."""
    return lo(t) if v < lo(t) else hi(t) if v > hi(t) else v


def overflow(t, v):
    """Checked-form fault: exact result and the violated bound (SPEC-01 4.1, 9.2)."""
    return ("fault", {"code": "E_OVERFLOW", "exact": v, "limit": hi(t) if v > hi(t) else lo(t)})


def apply_form(form, t, v):
    if form == "checked":
        return ("value", v) if in_range(t, v) else overflow(t, v)
    if form == "wrap":
        return ("value", wrap(t, v))
    if form == "sat":
        return ("value", sat(t, v))
    raise ValueError(form)


def floor_div(a, b):
    """floor(a / b) for b != 0 (SPEC-01 4.4). Python's // is floor division on ints."""
    return a // b


def eval_binary(op, form, t, a, b):
    """Evaluate `a <op>.<form> b` at type `t`.

    Returns ("value", int) or ("fault", dict). The fault dict holds `code` and,
    where SPEC-01 9.2 gives the record those fields, `exact` and `limit`.
    """
    if t not in TYPES:
        raise ValueError("unknown type %r" % t)
    if op not in BINARY_OPS or form not in BINARY_OPS[op]:
        raise ValueError("no form %s.%s (SPEC-01 4.1)" % (op, form))
    if not (in_range(t, a) and in_range(t, b)) and op not in ("shl", "shr"):
        raise ValueError("operand out of range of %s" % t)
    if op == "add":
        return apply_form(form, t, a + b)
    if op == "sub":
        return apply_form(form, t, a - b)
    if op == "mul":
        return apply_form(form, t, a * b)
    if op in ("div", "rem"):
        # SPEC-01 4.4: a zero divisor faults E_DIV_ZERO; no exact, no limit (9.2).
        if b == 0:
            return ("fault", {"code": "E_DIV_ZERO"})
        q = floor_div(a, b)
        if op == "div":
            return apply_form("checked", t, q)
        # a % b = a - b * (a / b); |r| < |b| so it is always in range
        # (MIN % -1 is 0, SPEC-01 4.4).
        return ("value", a - b * q)
    # Shifts, SPEC-01 4.6. The count is compared as a value in Z and must
    # satisfy 0 <= k <= w - 1 in every form; the count is checked first.
    if not in_range(t, a):
        raise ValueError("operand out of range of %s" % t)
    _, w = TYPES[t]
    if k_out_of_range(b, w):
        if b > w - 1:
            # SPEC-01 9.2: limit is the largest permitted shift count; fixture 41.
            return ("fault", {"code": "E_SHIFT", "limit": w - 1})
        # Negative count: which bound `limit` names is not stated (ref/OPEN.md O-2).
        return ("fault", {"code": "E_SHIFT"})
    if op == "shl":
        return apply_form(form, t, a * (1 << b))
    # a >> k is floor(a / 2^k) for signed and unsigned T; never overflows.
    return ("value", floor_div(a, 1 << b))


def k_out_of_range(k, w):
    return k < 0 or k > w - 1


# ---- CIF-1 rendering (SPEC-01 13.1) -----------------------------------------

def typed(t, v):
    return {"t": t, "v": str(v)}


def render_expect(result, t):
    """The CIF-1 `expect` object for a result of eval_binary at type t."""
    kind, payload = result
    if kind == "value":
        return {"value": typed(t, payload)}
    fault = {"code": payload["code"]}
    for key in ("exact", "limit", "index"):
        if key in payload:
            fault[key] = str(payload[key])
    return {"fault": fault}


def op_id(op, form, t):
    """SPEC-01 9.8: name "." form "." type, lowercase."""
    return "%s.%s.%s" % (op, form, t.lower())


def record(rid, status, op, form, t, a, b):
    rec = {
        "id": rid,
        "status": status,
        "op": op_id(op, form, t),
        "args": [typed(t, a), typed(t, b)],
        "expect": render_expect(eval_binary(op, form, t, a, b), t),
    }
    return rec


def dumps(rec):
    """One JSON Lines record: no insignificant whitespace, keys in insertion order."""
    return json.dumps(rec, separators=(",", ":"), ensure_ascii=True)


def render_records(records):
    """Bytes of a CIF-1 file: UTF-8 (ASCII here), LF after every record."""
    return "".join(dumps(r) + "\n" for r in records).encode("utf-8")


def write_bytes(path, data):
    """Write bytes exactly; binary mode so no platform newline translation."""
    with open(path, "wb") as fh:
        fh.write(data)
