"""The independent reference of t27 set 1 (TT-05).

The 8 cases of `conformance/ternary/` print their results and return the number
of mismatches their own checks found. This script regenerates, for each case,
the exact bytes it prints and the I64 it returns, from the integer definitions
of SPEC-05, with Python integers. It executes no CINT code (SPEC-05 12.1) and
shares no code with the cases or with `cint_ref`: the inputs of each case
(operand lists, SplitMix64 seeds and streams, lengths, lane assignments) and
the layout of its output are restated here from the case's header and source,
and every printed value is computed here:

- balanced digits by TR-DIG-1, as the base-3 digits of `x + R(n)` less one,
  checked against repeated balanced residue by 3;
- `rescale3` by TR-DIG-4, as `floor((x + R(k)) / 3^k)`, the unique `q` with
  `x - q 3^k` in `[-R(k), R(k)]`, checked against digit deletion and the `U64`
  algorithm of SPEC-05 9.4; `rescale3_rem`, `shl3` and `trit` from it;
- `T27` addition with wrap and carry by TR-T27-2, as `balres` modulo `3^27`,
  checked against a trit-by-trit ripple with balanced carries;
- the `PT5` codec of TR-PT5-1, its 256-entry validity (TR-PT5-2, TR-PT5-3) and
  the legacy word relation of 11.4; the `PT4` fields of TR-PT4-1 to TR-PT4-3
  and their bit-plane reading of 9.1, with each plane word built from its
  field values by its stated layout;
- dot products by TR-DOT-1, checked against the four counts of TR-DOT-7, and
  16-bit lane sums as values modulo 2^16 read as signed, against the TR-IMP-2
  bounds recomputed from `j * P <= 32767`;
- SplitMix64 output `i` for seed `s`: the standard finalizer applied to
  `s + (i + 1) * 0x9E3779B97F4A7C15` modulo 2^64.

A case's return value here is the number of mismatches that the definitions
imply for the checks the case makes, which is 0 for every case.

Every run also checks, before any case, each SPEC-05 value a case names: the
12.2 fixtures PT5-01 to PT5-13, PT4-01 to PT4-06, LIT-01, LIT-02, LIT-04,
FMT-01, CONV-02, CONV-03, LEG-01, TR27-01 to TR27-09, RS-01 to RS-06, RS-08 to
RS-10, RS-12 to RS-14, RS-16, RS-18, RS-20 to RS-24, TDOT-01, TDOT-03 and
TDOT-05; the tables of 4.3, 5.2, 5.3 and 11.4; the operands of the
`ternary/planes_add27` lanes that carry TR27-01 to TR27-09; and the TR-IMP-2
lane bounds 255, 258, 127 and 129. It cross-checks the `T27` values of the
cases against the legacy oracle `tools/ternary_oracle.py`, read only, as
decision 1 permits: `balanced_digits`, `word_pack` and `word_unpack` under the
byte relation of 11.4, `shr3_value` against `rescale3` for every `k` from 0 to
27, and `word_eval` for `T27` addition, subtraction, multiplication, `trit`
and `shl3`, with their overflow faults. A failed check prints its name and the
run exits 1. One 4.3 entry is checked as corrected: the row for `U64_MAX` at
`k = 40` gives 41 digits, but `U64_MAX` exceeds `R(41)` and has 42, as the row
for `k = 41` says.

Usage (from the repository root):
    python conformance/tools/t27_ref.py [--check] [--write-stdout DIR] [case ...]

A case is named `ternary/<name>` (or `<name>`); the default is all 8. Without
`--check` it prints one line per case,
`<case> stdout-bytes <n> stdout-sha256 <hex> return I64 <v>`. `--write-stdout
DIR` writes each regenerated text to `DIR/<name>.out` for diagnosis. With
`--check` it reads the frozen `conformance/ternary/<name>.expect` (SPEC-09
CONF-11) and exits 1 if its `outcome` is not `value` or its `stdout-bytes`,
`stdout-sha256` or `return` line differs, naming each difference. It never
writes an `.expect` file.

Python standard library only; integers only, no floating point; LF line
endings.
"""

import argparse
import hashlib
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
TERNARY = os.path.join(ROOT, "conformance", "ternary")
ORACLE_PATH = os.path.join(ROOT, "tools", "ternary_oracle.py")

CASES = ("planes_adders", "planes_add27", "planes_digits", "planes_rescale3",
         "pt5_table", "pt4_planes", "tdot_planes", "tdot_i8_lanes")

I64_MIN = -(2 ** 63)
I64_MAX = 2 ** 63 - 1
U64_MAX = 2 ** 64 - 1


# ---------------------------------------------------------------------------
# Definitions (SPEC-05 1.2, 2, 3, 4, 5.2 to 5.4, 6, 7, 9)

def R(n):
    """SPEC-05 1.2: (3^n - 1) / 2, the largest magnitude of n balanced trits."""
    return (3 ** n - 1) // 2


M = R(27)
P27 = 3 ** 27

# SPEC-05 section 2: value ranges of the types the cases use.
RANGES = {
    "I8": (-128, 127),
    "I16": (-32768, 32767),
    "I32": (-(2 ** 31), 2 ** 31 - 1),
    "I64": (I64_MIN, I64_MAX),
    "U64": (0, U64_MAX),
    "I128": (-(2 ** 127), 2 ** 127 - 1),
    "T27": (-M, M),
}


def capacity(type_name):
    """SPEC-05 section 2: K, the least n with R(n) at least the largest magnitude."""
    lo, hi = RANGES[type_name]
    big = max(-lo, hi)
    n = 0
    while R(n) < big:
        n += 1
    return n


def balres(z, n):
    """SPEC-05 1.2: for odd n, the r with r = z (mod n) and |r| <= (n - 1) / 2."""
    r = z % n
    if r > (n - 1) // 2:
        r -= n
    return r


def trits(x, n):
    """TR-DIG-1: the n balanced trits of x, index 0 first; requires |x| <= R(n).

    x + R(n) lies in [0, 3^n - 1]; its base-3 digits d_i give
    sum((d_i - 1) 3^i) = x + R(n) - R(n) = x, and the representation is unique.
    """
    if not -R(n) <= x <= R(n):
        raise ValueError("%d needs more than %d balanced trits" % (x, n))
    u = x + R(n)
    out = []
    for _ in range(n):
        u, d = divmod(u, 3)
        out.append(d - 1)
    return out


def trits_by_residue(x, n):
    """TR-DIG-1 a second way: repeated balanced residue by 3 (SPEC-05 9.1)."""
    out = []
    v = x
    for _ in range(n):
        t = balres(v, 3)
        out.append(t)
        v = (v - t) // 3
    if v != 0:
        raise ValueError("%d needs more than %d balanced trits" % (x, n))
    return out


def value_of(ts):
    """sum(t_i 3^i), index 0 first."""
    total = 0
    for i, t in enumerate(ts):
        total += t * 3 ** i
    return total


def width(x):
    """The number of balanced trits of x up to its highest nonzero one: the least n with |x| <= R(n)."""
    n = 0
    while R(n) < abs(x):
        n += 1
    return n


def fmt_t(x):
    """TR-FMT-2, SPEC-04 LS-205: the minimal balanced-ternary literal of x."""
    if x == 0:
        return "0t0"
    ts = trits(x, width(x))
    return "0t" + "".join({1: "P", 0: "0", -1: "N"}[t] for t in reversed(ts))


def parse_t(literal):
    """TR-LIT-1: the value of a 0t literal, most significant digit first."""
    if not literal.startswith("0t") or len(literal) < 3:
        raise ValueError(literal)
    v = 0
    for c in literal[2:]:
        v = 3 * v + {"P": 1, "0": 0, "N": -1}[c]
    return v


def in_range(type_name, v):
    lo, hi = RANGES[type_name]
    return lo <= v <= hi


def rescale3(x, k):
    """TR-DIG-4: the unique q with x - q 3^k in [-R(k), R(k)].

    q = floor((x + R(k)) / 3^k) gives 0 <= x + R(k) - q 3^k <= 3^k - 1 = 2 R(k).
    """
    q = (x + R(k)) // 3 ** k
    r = x - q * 3 ** k
    if not -R(k) <= r <= R(k):
        raise AssertionError("rescale3 definition")
    return q


def rescale3_rem(x, k):
    """TR-DIG-5: the deleted part x - rescale3(x, k) 3^k."""
    return x - rescale3(x, k) * 3 ** k


def rescale3_by_deletion(x, k):
    """TR-DIG-4, equivalently: delete the k least significant balanced trits."""
    ts = trits(x, max(width(x), k))
    return value_of(ts[k:])


def rescale3_9_4(x, k):
    """SPEC-05 9.4: the binary algorithm on the magnitude, as the text states it."""
    a = -x if x < 0 else x
    if k >= 42:
        q = 0                                   # k = 42 on U64 gives 0 by P3
    elif k == 41:
        q = 1 if a > R(41) else 0
    else:
        d = 3 ** k
        q = a // d
        r = a - q * d
        if r > R(k):
            q = q + 1
    return -q if x < 0 else q


def typed_rescale3(type_name, x, k):
    """("value", q) or ("fault", "E_SHIFT"): valid k is 0 <= k <= K (TR-DIG-4, TR-DIG-6)."""
    if not 0 <= k <= capacity(type_name):
        return ("fault", "E_SHIFT")
    return ("value", rescale3(x, k))


def typed_shl3(type_name, x, k):
    """TR-DIG-3: x 3^k, checked against the type's range; valid k is 0 <= k <= K."""
    if not 0 <= k <= capacity(type_name):
        return ("fault", "E_SHIFT")
    v = x * 3 ** k
    if not in_range(type_name, v):
        return ("fault", "E_OVERFLOW", v)
    return ("value", v)


def typed_trit(type_name, x, i):
    """TR-DIG-2: trit i of x; valid i is 0 <= i < K."""
    k = capacity(type_name)
    if not 0 <= i < k:
        return ("fault", "E_SHIFT")
    return ("value", trits(x, k)[i])


def t27(op, a, b=None):
    """TR-T27-2 and TR-T27-3 on T27 operands: ("value", v) or ("fault", code, exact)."""
    exact = {"+": lambda: a + b, "-": lambda: a - b, "*": lambda: a * b,
             "neg": lambda: -a, "+%": lambda: a + b, "+|": lambda: a + b,
             "as": lambda: a, "as%": lambda: a}[op]()
    if op in ("+%", "as%"):
        return ("value", balres(exact, P27))
    if op == "+|":
        return ("value", max(-M, min(M, exact)))
    if not -M <= exact <= M:
        return ("fault", "E_NARROW" if op == "as" else "E_OVERFLOW", exact)
    return ("value", exact)


def from_trits(type_name, ts):
    """Section 6: sum(t_i 3^i), E_NARROW outside the type."""
    v = value_of(ts)
    if not in_range(type_name, v):
        return ("fault", "E_NARROW", v)
    return ("value", v)


def wrap_signed(v, bits):
    """SPEC-01 IM-50 wrap into a signed type of the given width."""
    m = 2 ** bits
    r = v % m
    return r - m if r >= m // 2 else r


def signed8(u):
    return wrap_signed(u, 8)


def ceil_div(a, b):
    return -(-a // b)


# PT5 (TR-PT5-1 to TR-PT5-4)

def pt5_value(ts5):
    """TR-PT5-1: t_0 + 3 t_1 + 9 t_2 + 27 t_3 + 81 t_4."""
    return value_of(ts5)


def pt5_byte(ts5):
    """TR-PT5-1: the byte holding trits t_0 .. t_4, as an I8 in two's complement."""
    return pt5_value(ts5) % 256


def pack5(ts):
    """Section 6 pack5 of a row: ceil(n / 5) bytes, padding trits zero (TR-PT5-3)."""
    padded = list(ts) + [0] * (5 * ceil_div(len(ts), 5) - len(ts))
    return [pt5_byte(padded[5 * j:5 * j + 5]) for j in range(len(padded) // 5)]


def pt5_decode_byte(u):
    """The five trits of byte u, or None for one of the 13 unused values (TR-PT5-2)."""
    s = signed8(u)
    if not -R(5) <= s <= R(5):
        return None
    return trits(s, 5)


def decode_pt5(row, k):
    """The verdict of decode_pt5 (TR-VAL-2): ("valid", trits), ("shape",), or
    ("invalid", byte in row, reason) for the first invalid byte in row order."""
    if len(row) != ceil_div(k, 5):
        return ("shape",)
    out = []
    for j, u in enumerate(row):
        ts = pt5_decode_byte(u)
        if ts is None:
            return ("invalid", j, "unused value")
        r = k - 5 * j
        if r < 5 and abs(signed8(u)) > R(r):
            return ("invalid", j, "nonzero padding")
        out.extend(ts)
    return ("valid", out[:k])


# PT4 (TR-PT4-1 to TR-PT4-3, 9.1)

PT4_FIELD = {0: 0b00, 1: 0b01, -1: 0b11}          # TR-PT4-2, field of each trit
PT4_TRIT = {0b00: 0, 0b01: 1, 0b11: -1}           # TR-PT4-2; 0b10 is invalid
# 9.1: nonzero = bit 0, negative = bit 1, P = nonzero & ~negative, N = nonzero & negative,
# so the field reads as P - N: 00 -> 0, 01 -> +1, 11 -> -1, and the invalid 10 -> 0.
PT4_PLANE_READ = {0b00: 0, 0b01: 1, 0b11: -1, 0b10: 0}


def pt4_fields(u, count=4):
    """TR-PT4-1: field d of u is bits 2d and 2d + 1."""
    return [(u >> (2 * d)) & 3 for d in range(count)]


def pack4(ts):
    """Section 6 pack4 of a row: ceil(n / 4) bytes, padding fields 00."""
    out = [0] * ceil_div(len(ts), 4)
    for i, t in enumerate(ts):
        out[i // 4] |= PT4_FIELD[t] << (2 * (i % 4))
    return out


def decode_pt4(row, k):
    """The verdict of decode_pt4 (TR-VAL-2, TR-PT4-3), as decode_pt5."""
    if len(row) != ceil_div(k, 4):
        return ("shape",)
    out = []
    for j, u in enumerate(row):
        fields = pt4_fields(u)
        if 0b10 in fields:
            return ("invalid", j, "invalid field")
        for d, f in enumerate(fields):
            if 4 * j + d >= k and f != 0:
                return ("invalid", j, "nonzero padding")
        out.extend(PT4_TRIT[f] for f in fields)
    return ("valid", out[:k])


def repack5(pt4_row, n):
    """Section 6 repack5: the PT5 bytes of the same n trits."""
    verdict = decode_pt4(pt4_row, n)
    if verdict[0] != "valid":
        raise ValueError("repack5 of an invalid PT4 row")
    return pack5(verdict[1])


# Dot products (TR-DOT-1, TR-DOT-7) and lanes (TR-IMP-2)

def tdot(type_name, w, x):
    """TR-DOT-1: one exact sum, one final checked narrowing to the accumulator type."""
    if len(w) != len(x):
        return ("fault", "E_SHAPE")
    s = 0
    for wi, xi in zip(w, x):
        s += wi * xi
    if not in_range(type_name, s):
        return ("fault", "E_OVERFLOW", s)
    return ("value", s)


def dot_by_planes(a, b):
    """TR-DOT-7: |Pa & Pb| + |Na & Nb| - |Pa & Nb| - |Na & Pb|, with the planes as index sets."""
    pa = {i for i, t in enumerate(a) if t == 1}
    na = {i for i, t in enumerate(a) if t == -1}
    pb = {i for i, t in enumerate(b) if t == 1}
    nb = {i for i, t in enumerate(b) if t == -1}
    return len(pa & pb) + len(na & nb) - len(pa & nb) - len(na & pb)


def lane16(v):
    """A 16-bit lane holding v: v modulo 2^16, read as signed."""
    return wrap_signed(v, 16)


def lane_bound(p):
    """TR-IMP-2: the most products of magnitude p that a 16-bit lane takes, j p <= 2^15 - 1."""
    return (2 ** 15 - 1) // p


# SplitMix64

GAMMA = 0x9E3779B97F4A7C15
MASK64 = 2 ** 64 - 1


def mix(z):
    """The SplitMix64 finalizer (the 64-bit variant 13 mixer)."""
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK64
    return z ^ (z >> 31)


def sm(seed, i):
    """Output i (from 0) of SplitMix64 seeded with seed."""
    return mix((seed + (i + 1) * GAMMA) & MASK64)


# ---------------------------------------------------------------------------
# The cases. Each returns (text, return value).

class Out:
    def __init__(self):
        self.parts = []

    def __call__(self, s):
        self.parts.append(s)

    def text(self):
        return "".join(self.parts)


def plane_word(values, sign):
    """The plane word of a list of trits: bit L set where lane L holds `sign`."""
    w = 0
    for lane, t in enumerate(values):
        if t == sign:
            w |= 1 << lane
    return w


def half_add(total):
    """a + b (+ c) = s + 3 carry with s = balres_3(total) (TR-DIG-1 for two trits)."""
    s = balres(total, 3)
    return s, (total - s) // 3


def case_planes_adders():
    out = Out()
    bad = 0
    # Half adder: lane L holds a = L % 3 - 1, b = L / 3 - 1, L from 0 to 8.
    lanes = []
    for lane in range(9):
        a = lane % 3 - 1
        b = lane // 3 - 1
        s, c = half_add(a + b)
        lanes.append((a, b, s, c))
        bad += 0 if s in (-1, 0, 1) and c in (-1, 0, 1) and a + b == s + 3 * c else 1
    s_vals = [v[2] for v in lanes]
    c_vals = [v[3] for v in lanes]
    words = (plane_word(s_vals, 1), plane_word(s_vals, -1), plane_word(c_vals, 1), plane_word(c_vals, -1))
    bad += 0 if words[0] & words[1] == 0 and words[2] & words[3] == 0 else 1
    bad += 0 if (words[0] | words[1] | words[2] | words[3]) >> 9 == 0 else 1
    out("half adder, 9 lanes: s+ %s s- %s c+ %s c- %s\n" % tuple(format(w, "#x") for w in words))
    for lane, (a, b, s, c) in enumerate(lanes):
        out("lane %d: a %d b %d s %d c %d\n" % (lane, a, b, s, c))
    # Full adder: lane L holds a = L % 3 - 1, b = L / 3 % 3 - 1, carry in c = L / 9 - 1, L from 0 to 26.
    lanes = []
    for lane in range(27):
        a = lane % 3 - 1
        b = lane // 3 % 3 - 1
        c = lane // 9 - 1
        s, k = half_add(a + b + c)
        lanes.append((a, b, c, s, k))
        bad += 0 if s in (-1, 0, 1) and k in (-1, 0, 1) and a + b + c == s + 3 * k else 1
    s_vals = [v[3] for v in lanes]
    k_vals = [v[4] for v in lanes]
    words = (plane_word(s_vals, 1), plane_word(s_vals, -1), plane_word(k_vals, 1), plane_word(k_vals, -1))
    bad += 0 if words[0] & words[1] == 0 and words[2] & words[3] == 0 else 1
    bad += 0 if (words[0] | words[1] | words[2] | words[3]) >> 27 == 0 else 1
    out("full adder, 27 lanes: s+ %s s- %s carry+ %s carry- %s\n" % tuple(format(w, "#x") for w in words))
    for lane, (a, b, c, s, k) in enumerate(lanes):
        out("lane %d: a %d b %d c %d s %d carry %d\n" % (lane, a, b, c, s, k))
    out("mismatches %d\n" % bad)
    return out.text(), bad


def add27_operands():
    """planes_add27: lanes 0 to 11 as listed in its run(); lane L from 12 to 63 from SplitMix64 seed 27."""
    pairs = [
        (M, 0),                                   # TR27-01
        (M, 1),                                   # TR27-02, -05, -06, -08, -09
        (-M, 1),                                  # TR27-03 (difference)
        (0, -M),                                  # TR27-04 (difference)
        (2541865828330, 1270932914165),           # TR27-07 (sum)
        (M, M),
        (-M, -M),
        (M, -M),
        (0, 0),
        (1, -1),
        (R(26), R(26)),
        (3 ** 26, 3 ** 26),
    ]
    for lane in range(12, 64):
        pairs.append((sm(27, 2 * lane) % P27 - M, sm(27, 2 * lane + 1) % P27 - M))
    return pairs


def t27_add_with_carry(a, b):
    """TR-T27-2: the 27-trit sum balres_(3^27)(a + b) and the carry out of trit 26."""
    e = a + b
    s = balres(e, P27)
    return s, (e - s) // P27


def ripple27(a, b):
    """The same by trits: s_i = balres_3(a_i + b_i + c_i), c_(i+1) = (a_i + b_i + c_i - s_i) / 3."""
    ta = trits(a, 27)
    tb = trits(b, 27)
    c = 0
    ts = []
    for i in range(27):
        s, c = half_add(ta[i] + tb[i] + c)
        ts.append(s)
    return value_of(ts), c


def case_planes_add27():
    out = Out()
    bad = 0
    for lane, (a, b) in enumerate(add27_operands()):
        if not (-M <= a <= M and -M <= b <= M):
            bad += 1
        s, k = t27_add_with_carry(a, b)
        d, kd = t27_add_with_carry(a, -b)
        out("lane %d: a %d b %d sum %d carry %d difference %d borrow %d\n" % (lane, a, b, s, k, d, kd))
        for (x, y, v, c) in ((a, b, s, k), (a, -b, d, kd)):
            e = x + y
            want = 1 if e > M else (-1 if e < -M else 0)
            bad += 0 if c == want and v == e - want * P27 else 1
            bad += 0 if ripple27(x, y) == (v, c) else 1
    out("mismatches %d\n" % bad)
    return out.text(), bad


def digits_values():
    """planes_digits part 1: values 0 to 11 as listed in its run(); value L from 12 to 63 from seed 5."""
    vals = [1000000, -M, M, 0, 8, -8, 1, -1, R(26), -R(26), 3 ** 26, -(3 ** 26)]
    for lane in range(12, 64):
        vals.append(sm(5, lane) % P27 - M)
    return vals


def legacy_word(x):
    """SPEC-05 11.4: the legacy word stores x + M in base 243, little-endian, in 6 bytes."""
    u = x + M
    out = []
    for _ in range(6):
        u, g = divmod(u, 243)
        out.append(g)
    if u != 0:
        raise ValueError("legacy word overflow")
    return out


def legacy_to_core(legacy):
    """SPEC-05 11.4: core_byte[j] = legacy_byte[j] - 121 (j < 5), core_byte[5] = legacy_byte[5] - 4."""
    return [(legacy[j] - (121 if j < 5 else 4)) % 256 for j in range(6)]


def case_planes_digits():
    out = Out()
    bad = 0
    for x in digits_values():
        ts = trits(x, 27)
        bad += 0 if ts == trits_by_residue(x, 27) and value_of(ts) == x else 1
        core = pack5(ts)
        legacy = legacy_word(x)
        bad += 0 if len(core) == 6 else 1
        # TR-PT5-4: the bytes are balanced base-243 digits of x; |b_5| <= 4.
        back = sum(signed8(core[j]) * 243 ** j for j in range(6))
        bad += 0 if back == x and abs(signed8(core[5])) <= 4 else 1
        bad += 0 if legacy_to_core(legacy) == core and legacy[5] < 9 else 1
        for j in range(6):
            bad += 0 if trits(signed8(core[j]), 5) == (ts + [0, 0, 0])[5 * j:5 * j + 5] else 1
        out("value %d %s pt5" % (x, fmt_t(x)))
        out("".join(" %02x" % c for c in core))
        out(" legacy")
        out("".join(" %02x" % g for g in legacy))
        out("\n")
    leg01 = [0xb0, 0x69, 0x8a, 0x79, 0x79, 0x04]
    core = legacy_to_core(leg01)
    w = sum(signed8(core[j]) * 243 ** j for j in range(6))
    out("legacy b0 69 8a 79 79 04 is pt5")
    out("".join(" %02x" % c for c in core))
    out(" value %d\n" % w)
    bad += 0 if w == 1000000 and core == pack5(trits(1000000, 27)) else 1
    for x in (I64_MAX, I64_MIN, R(40), R(40) + 1, -R(40), -R(40) - 1, 3 ** 39, 0):
        ts = trits(x, 41)
        bad += 0 if value_of(ts) == x and ts == trits_by_residue(x, 41) else 1
        out("boundary %d: %d trits %s\n" % (x, width(x), fmt_t(x)))
    r40 = from_trits("I64", [1] * 40)
    out("40 trits all +1: %d\n" % r40[1])
    bad += 0 if r40 == ("value", 6078832729528464400) else 1
    r41 = value_of([1] * 41)
    above = r41 > I64_MAX
    out("41 trits all +1: %d as U64, above I64.max: %s\n" % (r41, "true" if above else "false"))
    bad += 0 if in_range("U64", r41) and from_trits("I64", [1] * 41)[0] == "fault" else 1
    out("mismatches %d\n" % bad)
    return out.text(), bad


RESCALE_ROWS_I64 = ((5, 1), (-5, 1), (14, 2), (-14, 2), (M, 26), (M, 27), (I64_MAX, 40),
                    (I64_MIN, 40), (I64_MIN, 41), (122, 5), (4, 1), (-4, 1), (13, 2), (8, 0),
                    (-M, 26), (127, 5), (121, 5))
RESCALE_ROWS_U64 = ((U64_MAX, 41), (U64_MAX, 42), (2 ** 63, 40), (U64_MAX, 40),
                    (R(40), 40), (R(40) + 1, 40))
SWEEP_KS = (1, 3, 4, 7, 20, 27, 40)


def rescale_agree(type_name, x, k):
    """1 when rescale3 by its definition, by digit deletion and by 9.4 disagree, else 0."""
    r = typed_rescale3(type_name, x, k)
    if r[0] != "value":
        return 1
    q = r[1]
    return 0 if q == rescale3_by_deletion(x, k) == rescale3_9_4(x, k) and in_range(type_name, q) else 1


def case_planes_rescale3():
    out = Out()
    bad = 0
    for x, k in RESCALE_ROWS_I64:
        q = rescale3(x, k)
        r = rescale3_rem(x, k)
        out("rescale3(%d, %d) = %d, rescale3_rem = %d\n" % (x, k, q, r))
        bad += rescale_agree("I64", x, k)
        bad += 0 if q * 3 ** k + r == x and abs(r) <= R(k) and in_range("I64", r) else 1
    for x, k in RESCALE_ROWS_U64:
        q = rescale3(x, k)
        out("rescale3(%d as U64, %d) = %d\n" % (x, k, q))
        bad += rescale_agree("U64", x, k)
    e = typed_shl3("I64", 1, 26)
    out("shl3(1, 26) = %d\n" % e[1])
    bad += 0 if e == ("value", 2541865828329) else 1
    f = typed_shl3("I64", -5, 3)
    out("shl3(-5, 3) = %d\n" % f[1])
    bad += 0 if f == ("value", -135) else 1
    out("trit(8, 0..2) = %d %d %d\n" % tuple(typed_trit("I64", 8, i)[1] for i in range(3)))
    out("trit(%d, 40) = %d\n" % (I64_MAX, typed_trit("I64", I64_MAX, 40)[1]))
    out("trit(%d, 26) = %d\n" % (M, typed_trit("I64", M, 26)[1]))
    sweep_i64 = [5, -5, 14, M, -M, I64_MAX, I64_MIN, 0, 1, -1, R(40), -R(40) - 1]
    sweep_i64 += [wrap_signed(sm(3, i), 64) for i in range(12)]
    for x in sweep_i64:
        for k in range(capacity("I64") + 1):
            bad += rescale_agree("I64", x, k)
        bad += 0 if rescale3(rescale3(x, 3), 4) == rescale3(x, 7) else 1      # 4.2 P5
        out("sweep %d: %s\n" % (x, " ".join("%d" % rescale3(x, k) for k in SWEEP_KS)))
    sweep_u64 = [U64_MAX, 2 ** 63, R(41), R(41) + 1] + [sm(4, i) for i in range(12)]
    for x in sweep_u64:
        for k in range(capacity("U64") + 1):
            bad += rescale_agree("U64", x, k)
        out("sweep %d as U64: %s\n" % (x, " ".join("%d" % rescale3(x, k) for k in SWEEP_KS)))
    out("mismatches %d\n" % bad)
    return out.text(), bad


def case_pt5_table():
    out = Out()
    bad = 0
    nvalid = 0
    for u in range(256):
        ts = pt5_decode_byte(u)
        if ts is None:
            out("%02x: unused\n" % u)
            continue
        nvalid += 1
        bad += 0 if ts == trits_by_residue(signed8(u), 5) and pt5_byte(ts) == u else 1
        out("%02x: %d %d %d %d %d r %d\n" % (u, ts[0], ts[1], ts[2], ts[3], ts[4], width(signed8(u))))
    out("valid %d, unused %d\n" % (nvalid, 256 - nvalid))
    bad += 0 if nvalid == 243 else 1
    # TR-PT5-3: a valid last byte holding r trits has |b| <= R(r).
    last = [sum(1 for u in range(256) if pt5_decode_byte(u) is not None and abs(signed8(u)) <= R(r))
            for r in range(6)]
    out("valid last bytes for r = 1 to 4: %d %d %d %d\n" % (last[1], last[2], last[3], last[4]))
    for r in range(1, 6):
        bad += 0 if last[r] == 3 ** r else 1
    p1 = pack5([1, 0, -1, 1, 1])
    p2 = pack5([1, 1, 1, 1, 1])
    p3 = pack5([-1, -1, -1, -1, -1])
    p4 = pack5([1, 1, 1, 1, 1, -1, 1])
    out("PT5-01 [1, 0, -1, 1, 1]: %02x\n" % p1[0])
    out("PT5-02 [1, 1, 1, 1, 1]: %02x\n" % p2[0])
    out("PT5-03 [-1, -1, -1, -1, -1]: %02x\n" % p3[0])
    out("PT5-04 [1, 1, 1, 1, 1, -1, 1]: %02x %02x\n" % (p4[0], p4[1]))
    bad += 0 if (p1, p2, p3, p4) == ([0x64], [0x79], [0x87], [0x79, 0x02]) else 1
    flagged = sum(1 for u in range(0x7a, 0x87) if decode_pt5([u], 5) == ("invalid", 0, "unused value"))
    out("PT5-07 the 13 values 7a to 86, k 5: %d unused\n" % flagged)
    bad += 0 if flagged == 13 else 1
    for label, row, k, want in (("PT5-09", [0x79, 0x03], 6, 1), ("PT5-10", [0x79, 0x01], 6, -1),
                                ("PT5-11", [0x00, 0x29], 9, 1), ("PT5-13", [0x00, 0x7a, 0x7a], 15, 1)):
        v = decode_pt5(row, k)
        out(label + "".join(" %02x" % u for u in row) + " k %d:" % k)
        if v[0] == "valid":
            out(" valid, trits" + "".join(" %d" % t for t in v[1]) + "\n")
            at = -1
        else:
            out(" byte %d %s\n" % (v[1], v[2]))
            at = v[1]
        bad += 0 if at == want else 1
    out("mismatches %d\n" % bad)
    return out.text(), bad


def pt4_words():
    """pt4_planes: 8 raw words from seed 44, then 8 valid words whose field j holds sm(45, 32i + j) % 3 - 1."""
    words = [sm(44, i) for i in range(8)]
    for i in range(8):
        w = 0
        for j in range(32):
            w |= PT4_FIELD[sm(45, 32 * i + j) % 3 - 1] << (2 * j)
        words.append(w)
    return words


def case_pt4_planes():
    out = Out()
    bad = 0
    nvalid = 0
    for u in range(256):
        fields = pt4_fields(u)
        if 0b10 not in fields:
            nvalid += 1
            ts = [PT4_TRIT[f] for f in fields]
            out("%02x: %d %d %d %d\n" % (u, ts[0], ts[1], ts[2], ts[3]))
            # 2-bit two's complement reading (TR-PT4-2) and plane reading agree on valid fields.
            for f, t in zip(fields, ts):
                bad += 0 if wrap_signed(f, 2) == t == PT4_PLANE_READ[f] else 1
            bad += 0 if pack4(ts) == [u] else 1
        else:
            reads = [PT4_PLANE_READ[f] for f in fields]
            out("%02x: invalid, planes read %d %d %d %d\n" % tuple([u] + reads))
    out("valid %d, invalid %d\n" % (nvalid, 256 - nvalid))
    bad += 0 if nvalid == 81 else 1
    for w in pt4_words():
        fields = pt4_fields(w, 32)
        reads = [PT4_PLANE_READ[f] for f in fields]
        plus = plane_word(reads, 1)
        minus = plane_word(reads, -1)
        invalid = sum(1 for f in fields if f == 0b10)
        out("word %016x: plus %08x minus %08x invalid fields %d" % (w, plus, minus, invalid))
        if invalid == 0:
            ts = [PT4_TRIT[f] for f in fields]
            bad += 0 if pack4(ts) == [(w >> (8 * j)) & 0xff for j in range(8)] else 1
            out(" pt5" + "".join(" %02x" % b for b in pack5(ts)))
        out("\n")
    a = pack4([1, -1, 0, 1])
    b = pack4([1, -1, 0, 1, -1])
    out("PT4-01 [1, -1, 0, 1]: %02x\n" % a[0])
    out("PT4-02 [1, -1, 0, 1, -1]: %02x %02x\n" % (b[0], b[1]))
    bad += 0 if a == [0x4d] and b == [0x4d, 0x03] else 1
    for label, row, k, want in (("PT4-03", [0x02], 4, 0), ("PT4-04", [0x4d, 0x07], 5, 1)):
        v = decode_pt4(row, k)
        out(label + "".join(" %02x" % u for u in row) + " k %d:" % k)
        if v[0] == "valid":
            out(" valid, trits" + "".join(" %d" % t for t in v[1]) + "\n")
            at = -1
        else:
            out(" byte %d %s\n" % (v[1], v[2]))
            at = v[1]
        bad += 0 if at == want else 1
    r5 = repack5([0x71, 0x01], 5)
    out("PT4-05 71 01 n 5: pt5 %02x\n" % r5[0])
    bad += 0 if r5 == [0x64] else 1
    trips = sum(1 for u in range(256) if decode_pt4([u], 4)[0] == "valid" and pack4(decode_pt4([u], 4)[1]) == [u])
    out("PT4-06 the valid bytes round trip: %d\n" % trips)
    out("mismatches %d\n" % bad)
    return out.text(), bad


TDOT_LENGTHS = (0, 1, 63, 64, 65, 127, 128, 129, 4096)


def case_tdot_planes():
    out = Out()
    bad = 0
    x = [1, -1, 0, 1, 1]
    y = [1, 1, -1, -1, 0]
    s5 = tdot("I16", x, y)
    out("TDOT-05 [1, -1, 0, 1, 1] by [1, 1, -1, -1, 0]: %d\n" % s5[1])
    bad += 0 if s5 == ("value", -1) and dot_by_planes(x, y) == -1 else 1
    a = [sm(70, i) % 3 - 1 for i in range(4096)]
    b = [sm(71, i) % 3 - 1 for i in range(4096)]
    for n in TDOT_LENGTHS:
        s = tdot("I64", a[:n], b[:n])[1]
        bad += 0 if dot_by_planes(a[:n], b[:n]) == s and -n <= s <= n else 1
        out("n %d (words %d): fused %d, four counts %d, scalar %d\n" % (n, ceil_div(n, 64), s, s, s))
    nonzero = sum(1 for i in range(4096) if a[i] * b[i] != 0)
    out("nonzero products at 4096: %d\n" % nonzero)
    out("mismatches %d\n" % bad)
    return out.text(), bad


# tdot_i8_lanes part 1: (w, x) per lane, and the x domain's mx (128 for full-range I8, which
# x = -128 needs; 127 for the domain [-127, 127]).
LANES_DIRECT = ((-1, -128, 128), (1, -128, 128), (1, 127, 127), (-1, 127, 127))
LANES_OFFSET = ((1, -128, 128), (1, 127, 127), (1, -127, 127), (0, -128, 128))


def case_tdot_i8_lanes():
    out = Out()
    bad = 0
    t3 = tdot("I8", [1, 1, -1], [100, 100, 100])
    out("TDOT-03 [1, 1, -1] by [100, 100, 100]: %d\n" % lane16(t3[1]))
    bad += 0 if t3 == ("value", 100) else 1
    for offset, lanes in ((0, LANES_DIRECT), (1, LANES_OFFSET)):
        for lane, (w, x, mx) in enumerate(lanes):
            p = (w + offset) * x
            bound = lane_bound(mx if offset == 0 else 2 * mx)
            first = 0
            for j in range(1, 260):
                if lane16(j * p) != j * p:
                    first = j
                    break
            out("%s lane %d: w %d x %d, product %d, bound %d: "
                % ("direct" if offset == 0 else "offset", lane, w, x, p, bound))
            if first == 0:
                out("exact through 259\n")
            else:
                out("first disagreement at %d (lane %d, exact %d)\n" % (first, lane16(first * p), first * p))
                bad += 1 if first <= bound else 0
    rw = [sm(81, i) % 3 - 1 for i in range(1032)]
    rx = [wrap_signed(sm(82, i), 8) for i in range(1032)]
    ry = [sm(83, i) % 255 - 127 for i in range(1032)]

    def lanes_sum(w, x, c, offset):
        # Lane l takes inputs l c to l c + c - 1; each lane holds its sum modulo 2^16.
        s = 0
        for lane in range(4):
            s += lane16(sum((w[i] + offset) * x[i] for i in range(lane * c, lane * c + c)))
        if offset:
            s -= sum(x[:4 * c])
        return s

    for label, x, c, offset in (("direct, full-range x, 255 per lane", rx, 255, 0),
                                ("offset, full-range x, 127 per lane", rx, 127, 1),
                                ("offset, x in [-127, 127], 129 per lane", ry, 129, 1)):
        got = lanes_sum(rw, x, c, offset)
        exact = tdot("I64", rw[:4 * c], x[:4 * c])[1]
        out("%s: lanes %d, exact %d\n" % (label, got, exact))
        bad += 0 if got == exact else 1
    out("mismatches %d\n" % bad)
    return out.text(), bad


GENERATORS = {
    "planes_adders": case_planes_adders,
    "planes_add27": case_planes_add27,
    "planes_digits": case_planes_digits,
    "planes_rescale3": case_planes_rescale3,
    "pt5_table": case_pt5_table,
    "pt4_planes": case_pt4_planes,
    "tdot_planes": case_tdot_planes,
    "tdot_i8_lanes": case_tdot_i8_lanes,
}


def regenerate(name):
    """(stdout bytes, I64 return value) of case `ternary/<name>`."""
    text, ret = GENERATORS[name]()
    return text.encode("ascii"), ret


# ---------------------------------------------------------------------------
# SPEC-05 values the cases name, and the legacy oracle cross-check

def hexbytes(bs):
    return " ".join("%02x" % b for b in bs)


# SPEC-05 4.3: (x, k, type, digits most significant first or a digit count, rescale3,
# rescale3_rem or None where TR-DIG-5 does not define it, div_trunc, floor).
TABLE_4_3 = (
    (5, 1, "I64", "PNN", 2, -1, 1, 1),
    (-5, 1, "I64", "NPP", -2, 1, -1, -2),
    (4, 1, "I64", "PP", 1, 1, 1, 1),
    (-4, 1, "I64", "NN", -1, -1, -1, -2),
    (13, 2, "I64", "PPP", 1, 4, 1, 1),
    (14, 2, "I64", "PNNN", 2, -4, 1, 1),
    (-14, 2, "I64", "NPPP", -2, 4, -1, -2),
    (8, 0, "I64", "P0N", 8, 0, 8, 8),
    (M, 26, "T27", "P" * 27, 1, 1270932914164, 1, 1),
    (-M, 26, "T27", "N" * 27, -1, -1270932914164, -1, -2),
    (M, 27, "T27", "P" * 27, 0, M, 0, 0),
    (I64_MAX, 40, "I64", 41, 1, -2934293422202152994, 0, 0),
    (I64_MIN, 40, "I64", 41, -1, 2934293422202152993, 0, -1),
    (I64_MIN, 41, "I64", 41, 0, I64_MIN, 0, -1),
    (2 ** 63, 40, "U64", 41, 1, None, 0, 0),
    # The 4.3 text gives this row "41 digits"; U64_MAX > R(41) needs 42, as its k = 41 row says.
    (U64_MAX, 40, "U64", 42, 2, None, 1, 1),
    (U64_MAX, 41, "U64", 42, 1, None, 0, 0),
    (127, 5, "I8", "PNNN0P", 1, -116, 0, 0),
    (121, 5, "I8", "PPPPP", 0, 121, 0, 0),
    (122, 5, "I8", "PNNNNN", 1, -121, 0, 0),
)

# SPEC-05 5.2: trits (index 0 first) and their PT5 bytes; T27 values and their 6 bytes.
TABLE_5_2 = (
    ([1, 0, -1, 1, 1], "64"),
    ([1, 1, 1, 1, 1], "79"),
    ([-1, -1, -1, -1, -1], "87"),
    ([0, 0, 0, 0, 0], "00"),
    ([1, 1, 1, 1, 1, -1, 1], "79 02"),
    ([-1, -1, -1, -1, -1, 1], "87 01"),
    ([1, -1, 0, 1, 1], "6a"),
    ([1, 1, -1, -1, 0], "e0"),
)
TABLE_5_2_T27 = (
    (0, "00 00 00 00 00 00"),
    (8, "08 00 00 00 00 00"),
    (M, "79 79 79 79 79 04"),
    (-M, "87 87 87 87 87 fc"),
    (1000000, "37 f0 11 00 00 00"),
)

# SPEC-05 5.3: trits and their PT4 bytes.
TABLE_5_3 = (
    ([1, -1, 0, 1], "4d"),
    ([1, -1, 0, 1, -1], "4d 03"),
    ([1, 0, -1, 1, 1], "71 01"),
    ([1, 1, 1, 1, 1, -1, 1], "55 1d"),
    ([-1, -1, -1, -1, -1, 1], "ff 07"),
    ([0, 0, 0, 0, 0], "00 00"),
)

# SPEC-05 11.4: value, legacy bytes, PT5[27] bytes.
TABLE_11_4 = (
    (0, "79 79 79 79 79 04", "00 00 00 00 00 00"),
    (M, "f2 f2 f2 f2 f2 08", "79 79 79 79 79 04"),
    (-M, "00 00 00 00 00 00", "87 87 87 87 87 fc"),
    (1000000, "b0 69 8a 79 79 04", "37 f0 11 00 00 00"),
)

# SPEC-05 9.2 TR-IMP-2: (technique, P per mx, mx, products per 16-bit lane).
TABLE_IMP_2 = (
    ("direct", 1, 128, 255),
    ("direct", 1, 127, 258),
    ("offset", 2, 128, 127),
    ("offset", 2, 127, 129),
)


def fixture_failures():
    """The SPEC-05 values the cases name, recomputed; returns the names of those that fail."""
    fails = []

    def check(name, ok):
        if not ok:
            fails.append(name)

    for x, k, ty, digits, q, r, dt, fl in TABLE_4_3:
        if isinstance(digits, str):
            check("4.3 digits %d" % x, fmt_t(x) == "0t" + digits)
        else:
            check("4.3 width %d" % x, width(x) == digits)
        check("4.3 rescale3(%d, %d)" % (x, k), typed_rescale3(ty, x, k) == ("value", q))
        check("4.3 deletion and 9.4 (%d, %d)" % (x, k),
              rescale3_by_deletion(x, k) == q and rescale3_9_4(x, k) == q)
        if r is not None:
            check("4.3 rescale3_rem(%d, %d)" % (x, k), rescale3_rem(x, k) == r)
        trunc = (abs(x) // 3 ** k) * (1 if x >= 0 else -1)
        check("4.3 div_trunc(%d, %d)" % (x, k), trunc == dt)
        check("4.3 floor(%d, %d)" % (x, k), x // 3 ** k == fl)
    for ts, want in TABLE_5_2:
        check("5.2 pack5 %s" % ts, hexbytes(pack5(ts)) == want)
    for v, want in TABLE_5_2_T27:
        check("5.2 PT5[27] of %d" % v, hexbytes(pack5(trits(v, 27))) == want)
    for ts, want in TABLE_5_3:
        check("5.3 pack4 %s" % ts, hexbytes(pack4(ts)) == want)
    for v, legacy, core in TABLE_11_4:
        check("11.4 legacy %d" % v, hexbytes(legacy_word(v)) == legacy)
        check("11.4 core %d" % v, hexbytes(legacy_to_core(legacy_word(v))) == core)
    # TR-PT5-1, TR-PT5-2: valid bytes 0x87 .. 0xff and 0x00 .. 0x79; the 13 unused values.
    unused = [u for u in range(256) if pt5_decode_byte(u) is None]
    check("TR-PT5-2 unused values", unused == list(range(0x7a, 0x87)))
    # 12.2 packed encodings.
    check("PT5-01", hexbytes(pack5([1, 0, -1, 1, 1])) == "64")
    check("PT5-02", hexbytes(pack5([1, 1, 1, 1, 1])) == "79")
    check("PT5-03", hexbytes(pack5([-1, -1, -1, -1, -1])) == "87")
    check("PT5-04", hexbytes(pack5([1, 1, 1, 1, 1, -1, 1])) == "79 02")
    check("PT5-05", hexbytes(pack5(trits(1000000, 27))) == "37 f0 11 00 00 00")
    check("PT5-06", hexbytes(pack5(trits(-M, 27))) == "87 87 87 87 87 fc")
    check("PT5-07", all(decode_pt5([u], 5) == ("invalid", 0, "unused value")
                        for u in (0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86)))
    check("PT5-08", sum(1 for u in range(256) if decode_pt5([u], 5)[0] == "valid"
                        and pack5(decode_pt5([u], 5)[1]) == [u]) == 243)
    check("PT5-09", decode_pt5([0x79, 0x03], 6) == ("invalid", 1, "nonzero padding"))
    check("PT5-10", decode_pt5([0x79, 0x01], 6) == ("valid", [1, 1, 1, 1, 1, 1]))
    check("PT5-11", decode_pt5([0x00, 0x29], 9) == ("invalid", 1, "nonzero padding") and R(4) == 40)
    check("PT5-12", decode_pt5([0, 0, 0], 9) == ("shape",))
    check("PT5-13", decode_pt5([0x00, 0x7a, 0x7a], 15) == ("invalid", 1, "unused value"))
    check("PT4-01", hexbytes(pack4([1, -1, 0, 1])) == "4d")
    check("PT4-02", hexbytes(pack4([1, -1, 0, 1, -1])) == "4d 03")
    check("PT4-03", decode_pt4([0x02], 4) == ("invalid", 0, "invalid field"))
    check("PT4-04", decode_pt4([0x4d, 0x07], 5) == ("invalid", 1, "nonzero padding"))
    check("PT4-05", hexbytes(repack5([0x71, 0x01], 5)) == "64")
    check("PT4-06", sum(1 for u in range(256) if decode_pt4([u], 4)[0] == "valid"
                        and pack4(decode_pt4([u], 4)[1]) == [u]) == 81)
    # 12.2 literals and conversions.
    check("LIT-01", parse_t("0tP0N") == 8)
    check("LIT-02", parse_t("0t" + "P" * 27) == 3812798742493 and in_range("T27", parse_t("0t" + "P" * 27)))
    check("LIT-04", parse_t("0tNPP") == -5)
    check("FMT-01", (fmt_t(8), fmt_t(0), fmt_t(-5)) == ("0tP0N", "0t0", "0tNPP"))
    check("CONV-02", from_trits("I64", [1] * 41) == ("fault", "E_NARROW", R(41)))
    check("CONV-03", from_trits("I64", [1] * 40) == ("value", 6078832729528464400))
    leg = [0xb0, 0x69, 0x8a, 0x79, 0x79, 0x04]
    check("LEG-01", hexbytes(legacy_to_core(leg)) == "37 f0 11 00 00 00"
          and legacy_word(1000000) == leg)
    # 12.2 T27 scalars.
    check("TR27-01", t27("+", M, 0) == ("value", M))
    check("TR27-02", t27("+", M, 1) == ("fault", "E_OVERFLOW", M + 1))
    check("TR27-03", t27("-", -M, 1) == ("fault", "E_OVERFLOW", -M - 1))
    check("TR27-04", t27("neg", -M) == ("value", M))
    check("TR27-05", t27("+%", M, 1) == ("value", -M))
    check("TR27-06", t27("+|", M, 1) == ("value", M))
    check("TR27-07", t27("*", 1270932914165, 3) == ("fault", "E_OVERFLOW", 3812798742495))
    check("TR27-08", t27("as", 3812798742494) == ("fault", "E_NARROW", 3812798742494))
    check("TR27-09", t27("as%", 3812798742494) == ("value", -3812798742493))
    # The planes_add27 lanes that carry the TR27 operands.
    lanes = add27_operands()
    check("TR27 lanes 0, 1", lanes[0] == (M, 0) and lanes[1] == (M, 1)
          and t27_add_with_carry(M, 1) == (-M, 1) and 3812798742494 == M + 1)
    check("TR27 lane 2", t27_add_with_carry(-M, -1) == (M, -1))
    check("TR27 lane 4", sum(lanes[4]) == 1270932914165 * 3
          and t27_add_with_carry(*lanes[4]) == (3812798742495 - P27, 1))
    # 12.2 digit operations.
    check("RS-01", typed_rescale3("I64", 5, 1) == ("value", 2))
    check("RS-02", typed_rescale3("I64", -5, 1) == ("value", -2))
    check("RS-03", typed_rescale3("I64", 14, 2) == ("value", 2))
    check("RS-04", typed_rescale3("I64", -14, 2) == ("value", -2))
    check("RS-05", typed_rescale3("T27", M, 26) == ("value", 1))
    check("RS-06", typed_rescale3("T27", M, 27) == ("value", 0))
    check("RS-08", typed_rescale3("I64", I64_MAX, 40) == ("value", 1))
    check("RS-09", typed_rescale3("I64", I64_MIN, 40) == ("value", -1))
    check("RS-10", typed_rescale3("I64", I64_MIN, 41) == ("value", 0))
    check("RS-12", typed_rescale3("U64", U64_MAX, 41) == ("value", 1))
    check("RS-13", typed_rescale3("I8", 122, 5) == ("value", 1))
    check("RS-14", rescale3_rem(I64_MAX, 40) == -2934293422202152994)
    check("RS-16", typed_shl3("T27", 1, 26) == ("value", 2541865828329))
    check("RS-18", [typed_trit("T27", 8, i) for i in range(3)] == [("value", -1), ("value", 0), ("value", 1)])
    check("RS-20", typed_trit("I64", I64_MAX, 40) == ("value", 1))
    check("RS-21", typed_rescale3("U64", 9223372036854775808, 40) == ("value", 1))
    check("RS-22", typed_rescale3("U64", U64_MAX, 40) == ("value", 2))
    check("RS-23", typed_rescale3("U64", 6078832729528464400, 40) == ("value", 0) and R(40) == 6078832729528464400)
    check("RS-24", typed_rescale3("U64", 6078832729528464401, 40) == ("value", 1))
    # 12.2 dot products.
    check("TDOT-01", tdot("I32", [], []) == ("value", 0))
    check("TDOT-03", tdot("I8", [1, 1, -1], [100, 100, 100]) == ("value", 100))
    check("TDOT-05", tdot("I16", [1, -1, 0, 1, 1], [1, 1, -1, -1, 0]) == ("value", -1))
    # TR-IMP-2 lane bounds.
    for technique, factor, mx, want in TABLE_IMP_2:
        check("TR-IMP-2 %s mx %d" % (technique, mx), lane_bound(factor * mx) == want)
    return fails


ORACLE_MODULE = "t27_ref_ternary_oracle"


def load_oracle():
    """tools/ternary_oracle.py, imported read only from its path (once per process)."""
    if ORACLE_MODULE in sys.modules:
        return sys.modules[ORACLE_MODULE]
    spec = importlib.util.spec_from_file_location(ORACLE_MODULE, ORACLE_PATH)
    module = importlib.util.module_from_spec(spec)
    sys.modules[ORACLE_MODULE] = module      # its dataclasses look their module up here
    try:
        spec.loader.exec_module(module)
    except BaseException:
        del sys.modules[ORACLE_MODULE]
        raise
    return module


def oracle_failures():
    """The T27 values of the cases against the legacy oracle; returns the names of those that fail."""
    o = load_oracle()
    fails = []
    values = set(digits_values())
    for a, b in add27_operands():
        values.update((a, b))
    values.update(x for x, _ in RESCALE_ROWS_I64 if -M <= x <= M)
    values.update(x for x in (5, -5, 14, M, -M, 0, 1, -1))
    for x in sorted(values):
        if list(o.balanced_digits(x)) != trits(x, 27):
            fails.append("oracle balanced_digits %d" % x)
        if list(o.word_pack(x)) != legacy_word(x) or o.word_unpack(bytes(legacy_word(x))) != x:
            fails.append("oracle word_pack %d" % x)
        if legacy_to_core(list(o.word_pack(x))) != pack5(trits(x, 27)):
            fails.append("oracle 11.4 relation %d" % x)
        for k in range(28):
            if o.shr3_value(x, k) != rescale3(x, k):
                fails.append("oracle shr3_value %d %d" % (x, k))
        for i in range(27):
            if o.word_eval(17, x, i) != (o.FAULT_NONE, trits(x, 27)[i]):
                fails.append("oracle trit %d %d" % (x, i))
    for a, b in add27_operands():
        for op, y in ((4, b), (5, b)):
            s, carry = t27_add_with_carry(a, y if op == 4 else -y)
            fault, value = o.word_eval(op, a, y)
            if carry == 0:
                ok = (fault, value) == (o.FAULT_NONE, s)
            else:
                ok = fault == o.FAULT_OVERFLOW and value is None
            if not ok:
                fails.append("oracle word_eval %d %d %d" % (op, a, y))
    if o.word_eval(6, 1270932914165, 3) != (o.FAULT_OVERFLOW, None):
        fails.append("oracle TR27-07")
    if o.word_eval(18, 1, 26) != (o.FAULT_NONE, 2541865828329):
        fails.append("oracle RS-16")
    if o.word_eval(18, 2541865828329, 1) != (o.FAULT_OVERFLOW, None):
        fails.append("oracle RS-17")
    if o.M != M:
        fails.append("oracle M")
    return fails


# ---------------------------------------------------------------------------
# Frozen outcomes

def read_expect(path):
    """The `key value` lines of a CONF-11 file; the first value of each key."""
    fields = {}
    with open(path, "rb") as fh:
        for raw in fh.read().decode("utf-8").split("\n"):
            if not raw:
                continue
            key, _, value = raw.partition(" ")
            fields.setdefault(key, value)
    return fields


def compare_frozen(name, data, ret):
    """The differences between the frozen `.expect` of a case and the regenerated outcome."""
    path = os.path.join(TERNARY, name + ".expect")
    if not os.path.isfile(path):
        return ["no frozen file conformance/ternary/%s.expect" % name]
    fields = read_expect(path)
    want = (("outcome", "value"),
            ("stdout-bytes", "%d" % len(data)),
            ("stdout-sha256", hashlib.sha256(data).hexdigest()),
            ("return", "I64 %d" % ret))
    diffs = []
    for key, value in want:
        got = fields.get(key)
        if got != value:
            diffs.append("%s: frozen %s, reference %s" % (key, "(absent)" if got is None else got, value))
    return diffs


def check_cases(names):
    """Every difference against the frozen files, as `<case> <difference>` strings."""
    out = []
    for name in names:
        data, ret = regenerate(name)
        for diff in compare_frozen(name, data, ret):
            out.append("ternary/%s %s" % (name, diff))
    return out


def case_name(arg):
    name = arg[len("ternary/"):] if arg.startswith("ternary/") else arg
    if name not in GENERATORS:
        raise argparse.ArgumentTypeError("unknown case %s" % arg)
    return name


def main(argv=None):
    parser = argparse.ArgumentParser(description="Independent reference of t27 set 1 (TT-05).")
    parser.add_argument("--check", action="store_true",
                        help="compare with the frozen conformance/ternary/<name>.expect files")
    parser.add_argument("--write-stdout", metavar="DIR",
                        help="write each regenerated text to DIR/<name>.out")
    parser.add_argument("cases", nargs="*", type=case_name, metavar="case",
                        help="ternary/<name>; default: all 8")
    args = parser.parse_args(argv)
    names = args.cases or list(CASES)

    fails = fixture_failures() + oracle_failures()
    for f in fails:
        print("t27_ref: check failed: %s" % f)
    status = 1 if fails else 0

    differ = 0
    for name in names:
        data, ret = regenerate(name)
        if args.write_stdout:
            os.makedirs(args.write_stdout, exist_ok=True)
            with open(os.path.join(args.write_stdout, name + ".out"), "wb") as fh:
                fh.write(data)
        if args.check:
            diffs = compare_frozen(name, data, ret)
            for diff in diffs:
                print("ternary/%s differs: %s" % (name, diff))
            if not diffs:
                print("ternary/%s agrees" % name)
            differ += 1 if diffs else 0
        else:
            print("ternary/%s stdout-bytes %d stdout-sha256 %s return I64 %d"
                  % (name, len(data), hashlib.sha256(data).hexdigest(), ret))
    if args.check:
        print("t27_ref: %d cases, %d differ" % (len(names), differ))
        if differ:
            status = 1
    return status


if __name__ == "__main__":
    sys.exit(main())
