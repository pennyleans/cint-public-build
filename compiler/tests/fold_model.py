"""The cases of the `fold` suite: compiler/fold.ci against cint_ref and the frozen files.

Slice 2 plan task 2.10b. Every expected value comes from the reference interpreter
(ref/cint_ref: arith.op for the operation, Diagnostic.to_expect_lines for the fault lines,
encode_fault_record for the canonical bytes of the compile-time kind) or from a frozen
file (the T1 integer-machine tables, the anchors, and the C6001 lines of conformance/), and
the reference is checked against those frozen files before any case runs. compiler/fold.ci
returns FNV-1a 64 digests of what it renders, and this module renders the same bytes.

Cases (each line of the harness case list is one entry call):
  - exhaustive at 8 bits: for I8 and U8, every first operand of every binary operator and
    form (add, sub, mul checked, wrapping, saturating; div, rem; shl checked and wrapping;
    shr; and, or, xor), one row_test call over all 256 second operands; neg and not over
    all 256 operands; `as` and `as%` from I8 and U8 to each of the eight types; and the six
    comparisons for a stride of first operands;
  - the T1 tables (conformance/tables, SPEC-09 CONF-10): every record of the 14 boundary
    tables of I64 and every 257th record of the 28 exhaustive 8-bit tables (22 of T1 and the
    six saturating ones of T2, task 2.14), each the pair
    of its .cases line and its integer-machine record;
  - the integer-machine anchors on the eight widths;
  - boundary samples of I16, I32, U16, U32, U64 for every operator, and every conversion
    between the eight types;
  - every C6001 expectation under conformance/ with a fault, compared line for line with
    the frozen text (SEED-08 row 3, exact 9223372036854775808, among them);
  - literals and the D-17 bounds: decimal, hexadecimal, octal, and binary literals around
    4,096 bits, the C1023 boundary, stored bytes, decimal rendering read back, conversions
    of literals with the operation `unassigned`, and the 520-byte OQ-147 exact;
  - chunk boundaries (LS-27, D-17): literal digits with a leading zero, a separator, or a
    zero value, positive and negative, fed in one call and split at every offset with and
    without empty chunks, all giving the value of the whole text (negative zero stores no
    bytes and renders `0`).
"""
import functools
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.set_int_max_str_digits(0)   # literals of 5,000 decimal digits
sys.path.insert(0, str(ROOT / "ref"))

from cint_ref import arith  # noqa: E402
from cint_ref.faults import (COMPILE_TIME, Diagnostic, Fault, FaultRecord, Position,  # noqa: E402
                             encode_fault_record, z_bytes)
from cint_ref.types import Value, int_type  # noqa: E402

TY = {"Bool": 0x01, "I8": 0x11, "I16": 0x12, "I32": 0x13, "I64": 0x14,
      "U8": 0x21, "U16": 0x22, "U32": 0x23, "U64": 0x24}
INTS = ["I8", "I16", "I32", "I64", "U8", "U16", "U32", "U64"]
NAMES = {"add": 1, "sub": 2, "mul": 3, "div": 4, "rem": 5, "shl": 6, "shr": 7, "and": 8,
         "or": 9, "xor": 10, "neg": 11, "not": 12, "as": 13}
FORMS = {"checked": 0, "wrap": 1, "sat": 2}
CMPS = [(1, lambda a, b: a == b), (2, lambda a, b: a != b), (3, lambda a, b: a < b),
        (4, lambda a, b: a <= b), (5, lambda a, b: a > b), (6, lambda a, b: a >= b)]
BINARY = [("add", "checked"), ("add", "wrap"), ("add", "sat"), ("sub", "checked"), ("sub", "wrap"),
          ("sub", "sat"), ("mul", "checked"), ("mul", "wrap"), ("mul", "sat"), ("div", "checked"),
          ("rem", "checked"), ("shl", "checked"), ("shl", "wrap"), ("shr", "checked"),
          ("and", "checked"), ("or", "checked"), ("xor", "checked")]
SHIFTS = ("shl", "shr")
POS = Position("t.ci", 7, 9)
MASK = (1 << 64) - 1
FNV_BASIS = 0xcbf29ce484222325


def fnv(data: bytes, h: int = FNV_BASIS) -> int:
    for byte in data:
        h = ((h ^ byte) * 0x100000001b3) & MASK
    return h


def ret(v: int) -> list:
    return ["outcome value", "return U64 %d" % (v & MASK)]


# ------------------------------------------------------------------------------ outcomes

def model(name: str, form: str, ty: str, a: int, tb: str, b: int):
    if name in ("neg", "not"):
        return arith.op(name, "checked", ty, Value(ty, a))
    if name == "as":
        return arith.op("as", form, ty, Value(ty, a), target=tb)
    return arith.op(name, form, ty, Value(ty, a), Value(tb, b))


def text(r) -> bytes:
    if isinstance(r, Value):
        return ("value %s\n" % r.render()).encode("ascii")
    lines = Diagnostic("C6001", POS, "", r).to_expect_lines()[2:]
    return ("\n".join(lines) + "\n").encode("ascii")


def record(r) -> bytes:
    if isinstance(r, Value):
        return b""
    return encode_fault_record(FaultRecord.from_fault(r, POS), COMPILE_TIME, 2)


def digest(r, what: int = 4) -> int:
    t, rec = text(r), record(r)
    return [fnv(t), fnv(rec), len(t), len(rec), fnv(rec, fnv(t))][what]


def op_case(name, form, ty, a, tb, b, r=None, what=4) -> tuple:
    if r is None:
        r = model(name, form, ty, a, tb, b)
    line = "fold op_test I64 %d I64 %d I64 %d U64 %d I64 %d U64 %d I64 %d" % (
        NAMES[name], FORMS[form], TY[ty], a & MASK, TY[tb], b & MASK, what)
    return line, ret(digest(r, what))


def row_case(name, form, ty, a, tb) -> tuple:
    """row_test: a op x for every x of the 8-bit type tb (for neg, not, and as: every
    operand x of ty; for a comparison, name is 100 + its code)."""
    h = FNV_BASIS
    src = ty if name in ("neg", "not", "as") else tb
    t = int_type(src)
    for x in range(t.min, t.max + 1):
        if isinstance(name, int):
            r = Value("Bool", dict(CMPS)[name - 100](a, x))
        elif name in ("neg", "not", "as"):
            r = model(name, form, ty, x, tb, 0)
        else:
            r = model(name, form, ty, a, tb, x)
        h = fnv(record(r), fnv(text(r), h))
    code = name if isinstance(name, int) else NAMES[name]
    line = "fold row_test I64 %d I64 %d I64 %d U64 %d I64 %d" % (code, FORMS[form], TY[ty], a & MASK, TY[tb])
    return line, ret(h)


# ------------------------------------------------------------------------------ frozen tables

def parse_op(op: str):
    parts = op.split(".")
    if len(parts) < 3 or parts[0] not in NAMES or parts[1] not in FORMS:
        return None
    types = [p.upper() for p in parts[2:]]
    if any(t not in INTS for t in types) or len(types) != (2 if parts[0] == "as" else 1):
        return None
    return parts[0], parts[1], types


def agrees(r, expect: dict) -> bool:
    """cint_ref's outcome against a CIF-1 `expect` object (SPEC-09 CONF-10)."""
    if "value" in expect:
        v = expect["value"]
        return isinstance(r, Value) and r.type == v["t"] and r.value == int(v["v"])
    f = expect["fault"]
    if not isinstance(r, Fault) or r.code != f["code"]:
        return False
    # A record asserts the fields it names (some anchors assert only the code).
    if "exact" in f and (r.exact is None or int(f["exact"]) != r.exact):
        return False
    return "limit" not in f or (r.limit is not None and int(f["limit"]) == r.limit.value)


def record_case(rec: dict):
    """The op_test case of one CIF-1 record on the eight widths, or None."""
    if rec.get("status") != "S":
        return None
    parsed = parse_op(rec["op"])
    if parsed is None:
        return None
    name, form, types = parsed
    args = rec["args"]
    if any(a["t"] not in INTS for a in args):
        return None
    ty = types[0]
    a = int(args[0]["v"])
    if name == "as":
        tb, b = types[1], 0
    elif name in ("neg", "not"):
        tb, b = ty, 0
    else:
        tb, b = args[1]["t"], int(args[1]["v"])
    r = model(name, form, ty, a, tb, b)
    if not agrees(r, rec["expect"]):
        raise SystemExit("cint_ref disagrees with frozen record %s" % rec["id"])
    return op_case(name, form, ty, a, tb, b, r)


def table_cases() -> list:
    cases = []
    tables = sorted((ROOT / "conformance" / "tables").glob("*.ci"))
    if len(tables) != 42:
        raise SystemExit("expected 42 tables, found %d" % len(tables))
    for ci in tables:
        head = ci.read_text(encoding="ascii").split("\n")
        source = next(line.split(": ", 1)[1] for line in head if line.startswith("// source: "))
        records = (ROOT / "conformance" / source).read_text(encoding="ascii").splitlines()
        lines = ci.with_suffix(".cases").read_text(encoding="ascii").splitlines()
        if len(lines) != len(records):
            raise SystemExit("%s: %d cases for %d records" % (ci.name, len(lines), len(records)))
        stride = 1 if "/bnd64/" in source else 257
        for i in range(0, len(records), stride):
            rec = json.loads(records[i])
            words = lines[i].split()
            if [words[2], words[4]] != [a["t"] for a in rec["args"]] or \
                    [int(words[3]), int(words[5])] != [int(a["v"]) for a in rec["args"]]:
                raise SystemExit("%s line %d differs from its record" % (ci.name, i + 1))
            case = record_case(rec)
            if case is None:
                raise SystemExit("%s line %d: not on the eight widths" % (ci.name, i + 1))
            cases.append(case)
    return cases


def anchor_cases() -> list:
    path = ROOT / "conformance" / "integer-machine" / "anchors.cif1.jsonl"
    cases = []
    for line in path.read_text(encoding="ascii").splitlines():
        case = record_case(json.loads(line))
        if case is not None:
            cases.append(case)
    return cases


# ------------------------------------------------------------------------------ samples

def samples(ty: str) -> list:
    t = int_type(ty)
    half = 1 << (t.width // 2)
    if t.signed:
        vals = [t.min, t.min + 1, -half, -2, -1, 0, 1, 2, half, t.max - 1, t.max]
    else:
        vals = [0, 1, 2, 3, half - 1, half, t.max // 2, t.max // 2 + 1, t.max - 1, t.max]
    return vals


def sample_cases() -> list:
    cases = []
    for ty in ("I16", "I32", "U16", "U32", "U64"):
        vals = samples(ty)
        few = vals[::2] + [vals[-1]] if vals[-1] not in vals[::2] else vals[::2]
        for name, form in BINARY:
            for a in few:
                if name in SHIFTS:
                    w = int_type(ty).width
                    for tb in (ty, "I64", "U8"):
                        t = int_type(tb)
                        for b in (-1, 0, 1, w // 2, w - 1, w, 64, 200):
                            if t.contains(b):
                                cases.append(op_case(name, form, ty, a, tb, b))
                else:
                    for b in few:
                        cases.append(op_case(name, form, ty, a, ty, b))
    for ty in INTS:
        for a in samples(ty):
            for name in ("neg", "not"):
                cases.append(op_case(name, "checked", ty, a, ty, 0))
            for to in INTS:
                for form in ("checked", "wrap"):
                    cases.append(op_case("as", form, ty, a, to, 0))
    return cases


def row_cases() -> list:
    cases = []
    for ty in ("I8", "U8"):
        t = int_type(ty)
        for name, form in BINARY:
            for a in range(t.min, t.max + 1):
                cases.append(row_case(name, form, ty, a, ty))
        for name in ("neg", "not"):
            cases.append(row_case(name, "checked", ty, 0, ty))
        for to in INTS:
            for form in ("checked", "wrap"):
                cases.append(row_case("as", form, ty, 0, to))
        for code, _ in CMPS:
            for a in sorted(set(list(range(t.min, t.max + 1, 7)) + [t.min, t.min + 1, -1 if t.signed else 0, 0, 1, t.max - 1, t.max])):
                cases.append(row_case(100 + code, "checked", ty, a, ty))
    return cases


# ------------------------------------------------------------------------------ conformance

def lit_digits(kind: int, n: int, seed: int) -> str:
    """The digits fold.ci's build_literal generates (no separators)."""
    if kind == 4:
        return "1" + "0" * (n - 1)
    if kind == 5:
        return "f" * n
    if kind == 6:
        return str(abs(seed))
    if kind == 8:
        return "P" * n
    base = {0: 10, 1: 16, 2: 8, 3: 2, 7: 3}[kind]
    out = []
    for i in range(n):
        d = (seed % base + 7 * i) % base
        if i == 0 and d == 0:
            d = 1
        out.append(("0PN" if kind == 7 else "0123456789abcdef")[d])
    return "".join(out)


def digits_value(digits: str, base: int) -> int:
    """The value of digits without separators; base 3 is balanced ternary (SPEC-04 LS-30)."""
    if base != 3:
        return int(digits, base)
    v = 0
    for c in digits:
        v = v * 3 + {"N": -1, "0": 0, "P": 1}[c]
    return v


def lit_value(kind: int, n: int, seed: int) -> int:
    base = {0: 10, 1: 16, 2: 8, 3: 2, 4: 16, 5: 16, 6: 10, 7: 3, 8: 3}[kind]
    v = digits_value(lit_digits(kind, n, seed), base)
    return -v if seed < 0 else v


def lit_spec(v: int):
    """A (kind, n, seed) whose value is v, for the Z operands of frozen cases."""
    m, sign = abs(v), (-1 if v < 0 else 0)
    if m < (1 << 63):
        return 6, 0, v
    h = "%x" % m
    if h == "1" + "0" * (len(h) - 1):
        return 4, len(h), sign
    if h == "f" * len(h):
        return 5, len(h), sign
    raise SystemExit("no test literal has the value %d" % v)


def typed(word: str) -> Value:
    t, v = word.split(" ")
    return Value(t, int(v))


def conformance_cases() -> list:
    cases = []
    for path in sorted((ROOT / "conformance").rglob("*.expect")):
        lines = path.read_text(encoding="ascii").split("\n")
        fault = [line for line in lines if line.startswith("diagnostic.fault.")]
        if not fault:
            continue
        want = ("\n".join(fault) + "\n").encode("ascii")
        fields = {}
        operands = []
        for line in fault:
            key, rest = line[len("diagnostic.fault."):].split(" ", 1)
            if key == "operand":
                operands.append(rest)
            else:
                fields[key] = rest
        exact = None if fields["exact"] == "none" else int(fields["exact"])
        limit = None if fields["limit"] == "none" else typed(fields["limit"])
        if fields["operation"] == "unassigned":
            z = int(operands[0].split(" ")[1])
            r = Fault(fields["code"], "unassigned", (Value("Z", z),), exact, limit)
            kind, n, seed = lit_spec(z)
            for what in (0, 1):
                line = "fold lit_convert_test I64 %d I64 %d I64 %d I64 0 I64 %d I64 %d" % (
                    kind, n, seed, TY[limit.type], what)
                cases.append((line, ret(fnv(want) if what == 0 else digest(r, 1))))
            continue
        parsed = parse_op(fields["operation"])
        if parsed is None:
            # A built-in's fault (box 07, for example div_trunc.checked.i32): this model covers
            # the operators; the check suite compares the built-in's record with cint_ref's.
            continue
        name, form, types = parsed
        ops = [typed(o) for o in operands]
        r = model(name, form, types[0], ops[0].value, types[-1] if name == "as" else ops[1].type,
                  0 if name == "as" else ops[1].value)
        if text(r) != want:
            raise SystemExit("cint_ref disagrees with %s" % path.relative_to(ROOT))
        ty = types[0]
        tb, b = (types[1], 0) if name == "as" else (ops[1].type, ops[1].value)
        cases.append(op_case(name, form, ty, ops[0].value, tb, b, r, 0)[:1] + (ret(fnv(want)),))
        cases.append(op_case(name, form, ty, ops[0].value, tb, b, r, 1))
    if len(cases) != 30:
        raise SystemExit("expected 15 frozen C6001 faults of operators and literal conversions, found %d"
                         % (len(cases) // 2))
    return cases


# ------------------------------------------------------------------------------ literals

LIT_SPECS = (
    [(0, n, s) for n in list(range(1, 41)) + [100, 500, 1000, 1231, 1232, 1233, 1234, 1235, 1240, 5000]
     for s in (0, 3, -2)]
    + [(1, n, s) for n in list(range(1, 21)) + [1023, 1024, 1025, 4500] for s in (0, 5, -4)]
    + [(2, n, s) for n in (1, 21, 22, 1364, 1365, 1366, 1367) for s in (0, 1, -6)]
    + [(3, n, s) for n in (1, 63, 64, 65, 4095, 4096, 4097, 9000) for s in (1, -3, 0)]
    + [(4, n, s) for n in (1, 2, 16, 17, 751, 1024, 1025) for s in (0, -1)]
    + [(5, n, s) for n in (1, 2, 16, 17, 1023, 1024, 1025) for s in (0, -1)]
    + [(6, 0, s) for s in (0, 1, -1, 127, 128, -128, -129, 255, 256, 300, -300, 65535, 65536,
                            (1 << 31) - 1, 1 << 31, -(1 << 31) - 1, (1 << 63) - 1, -(1 << 63) + 1)]
    + [(7, n, s) for n in (1, 2, 3, 40, 41, 2584, 2585, 2586, 4500) for s in (0, 1, 2, -1, -2)]
    + [(8, n, s) for n in (1, 40, 41, 2584, 2585) for s in (0, -1)]
)


def lit_cases() -> list:
    cases = []
    for kind, n, seed in LIT_SPECS:
        v = lit_value(kind, n, seed)
        bits = abs(v).bit_length()
        status = 1 if bits > 4096 else 0
        b = z_bytes(v)
        dec = str(v).encode("ascii")
        want = [status, len(b), fnv(b), fnv(dec), len(dec), 1, bits]
        for what in range(7):
            line = "fold lit_test I64 %d I64 %d I64 %d I64 %d" % (kind, n, seed, what)
            cases.append((line, ret(want[what] if status == 0 or what == 0 else 0)))
        if status:
            continue
        for to in INTS:
            t = int_type(to)
            for form in ("checked", "wrap"):
                if form == "wrap":
                    r = Value(to, t.wrap(v))
                elif t.contains(v):
                    r = Value(to, v)
                else:
                    r = Fault("E_NARROW", "unassigned", (Value("Z", v),), v,
                              Value(to, t.max if v > t.max else t.min))
                prefix = "fold lit_convert_test I64 %d I64 %d I64 %d I64 %d I64 %d" % (
                    kind, n, seed, FORMS[form], TY[to])
                cases.append((prefix + " I64 4", ret(digest(r))))
                cases.append((prefix + " I64 6", ret(r.value if isinstance(r, Value) else 0)))
            cases.append(("fold lit_convert_test I64 %d I64 %d I64 %d I64 0 I64 %d I64 5" % (
                kind, n, seed, TY[to]), ret(1 if t.contains(v) else 0)))
    return cases


def bound_cases() -> list:
    big = (1 << 4096) - 1
    exact = big << 63
    if len(z_bytes(exact)) != 520 or len(str(exact)) != 1252 or len(z_bytes(-big)) != 513:
        raise SystemExit("the D-17 bounds are not 520 bytes, 1,252 digits, and 513 bytes")
    cases = []
    for k, v in ((0, exact), (1, -exact), (2, big), (3, big)):
        enc = b"\x0f" + len(z_bytes(v)).to_bytes(4, "little") + z_bytes(v)
        dec = str(v).encode("ascii")
        want = {0: len(enc), 1: fnv(enc), 2: len(dec), 3: fnv(dec), 5: 1}
        if k >= 2:
            want[4] = 0 if k == 2 else 1
        if k == 2:
            want[6] = 1
        for what, w in sorted(want.items()):
            cases.append(("fold bound_test I64 %d I64 %d" % (k, what), ret(w)))
    return cases


CHUNK_TEXTS = (("01", 16), ("0_1", 16), ("00f_f", 16), ("0_0_1_0", 2), ("0017", 8),
               ("0", 16), ("0_0", 16), ("0", 10), ("1_0", 10), ("0P_N", 3), ("N0P", 3), ("0_0", 3))


def chunk_cases() -> list:
    """chunk_test: text k of CHUNK_TEXTS in one lit_digits call (split -1) or two calls at
    every offset, with and without empty chunks; every way gives the whole text's value."""
    cases = []
    for k, (digits, base) in enumerate(CHUNK_TEXTS):
        for neg in (0, 1):
            v = digits_value(digits.replace("_", ""), base) * (-1 if neg else 1)
            b = z_bytes(v)
            dec = str(v).encode("ascii")
            want = [0, len(b), fnv(b), fnv(dec), len(dec), v]
            for split in [-1] + list(range(len(digits) + 1)):
                for empty in (0, 1):
                    for what in range(6):
                        line = "fold chunk_test I64 %d I64 %d I64 %d I64 %d I64 %d" % (
                            k, neg, split, empty, what)
                        cases.append((line, ret(want[what])))
    # The minimal sequence of the 2.10b review: -0x01 fed as "0" then "1" is -1.
    if ("fold chunk_test I64 0 I64 1 I64 1 I64 0 I64 5", ret(-1)) not in cases:
        raise SystemExit("chunk cases miss the -0x01 split case")
    return cases


def seed08_cases() -> list:
    """SEED-08 row 3: 9223372036854775807 + 1 in I64, exact 9223372036854775808."""
    r = model("add", "checked", "I64", (1 << 63) - 1, "I64", 1)
    if b"diagnostic.fault.exact 9223372036854775808\n" not in text(r):
        raise SystemExit("cint_ref does not give SEED-08 row 3")
    return [op_case("add", "checked", "I64", (1 << 63) - 1, "I64", 1, r, what) for what in range(5)]


@functools.lru_cache(maxsize=None)
def fold_cases() -> tuple:
    return tuple(seed08_cases() + conformance_cases() + bound_cases() + lit_cases() + chunk_cases()
                 + anchor_cases() + sample_cases() + table_cases() + row_cases())
