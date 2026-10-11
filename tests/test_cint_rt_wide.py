"""The runtime's wide arithmetic against Python integers (OQ-227).

rt/cint_rt.c divides a 128-bit magnitude by a 64-bit one in div128 (long division by 32-bit
digits in rt/cint_rt.h's cint_rt_div_wide), and rt/cint_rt.h takes integer square roots in
cint_rt_isqrt64 (two Newton steps from rt/cint_rt.c's cint_rt_isqrt_seed) and counts leading
zero bits in cint_rt_clz64 (the compiler's count in the builtin helper mode, a binary search in
the portable one). div_round, muldiv, isqrt and isqrt_round go through them, and an I64 muldiv
whose result is in range returns before the generic checks. cint_rt_builtin_fast, the fast path of every cint_builtin_<r> call, must succeed
with the same bits exactly when cint_rt_builtin succeeds, on every case below that is not made on
a faulted context. A driver that includes rt/cint_rt.c, so that its static functions can be
called, is built for each leg of this host (gcc, and clang when installed, on
Linux; apple-clang on macOS; msvc on Windows) at -O0 and -O2 (MSVC /Od and /O2) in both helper
modes, with the flags of tools/cint_check.py and, under `workflow.py check --sanitize`, its
sanitizers on gcc and clang. Every build runs the same cases, and each answer must equal the one
computed here:

- div128: quotient and remainder of hi:lo by d on random and adversarial operands (divisors with
  the top bit set, near 2^63 and 2^64, below 2^32, with either half all ones; the largest
  quotients; partial remainders one below the divisor), which reach 0, 1 and 2 digit corrections
  at both digits; where hi < d, also cint_rt_div_wide_b and the product lo * d by
  cint_rt_mul_wide_b, the steps of the fast path in the builtin helper mode;
- isqrt64: every n below 2^12, k^2 - 1, k^2 and k^2 + 1 for random and extreme k, powers of two
  and their neighbors, both ends of every seed cell at every even shift, random n; clz64 on every power of two and the value below it, and random x;
- cint_rt_builtin: div_round, muldiv (to every result type) and isqrt_round in every rounding
  mode, and isqrt, over every operand type, with operand patterns that also carry bits above the
  type's width: the result, or the fault record's code, operation, operands, exact and limit, with
  *out unwritten. The cases include ties in every mode, products near 2^127, exact results at each
  bound of the result type and one past it, ties on both sides of those bounds, the divisors 0 and
  1, and isqrt of 0 and -1. I64 muldiv adds random full-width operands with results in range, the
  refused modes 0 and 10, and calls on a context that already holds a fault.

Build outputs go under <build>/rt-wide, where <build> is CINT_BUILD or cint-build in the system
temporary directory.
"""
import os
import pathlib
import random
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import cint_check as cc  # noqa: E402

MASK = (1 << 64) - 1
SENTINEL = 0xA5A5A5A5A5A5A5A5
TYPES = {"I8": 0x11, "I16": 0x12, "I32": 0x13, "I64": 0x14, "U8": 0x21, "U16": 0x22, "U32": 0x23, "U64": 0x24}
OPS = {"div_round": 10, "muldiv": 11, "isqrt": 13, "isqrt_round": 14}
MODES = ("floor", "ceil", "trunc", "away", "half_even", "half_away", "half_trunc", "half_up", "half_down")
CODES = {m.group(1): int(m.group(2)) for m in re.finditer(r"#define CINT_E_(\w+) (\d+)u",
                                                          (ROOT / "rt" / "cint_rt.h").read_text(encoding="utf-8"))}
# Odd divisors for splitting 2t + 1 into two operands: the small primes, and the factors of
# 2^32 + 1 and 2^64 + 1 (2^64 - 1 is 3 * 5 * 17 * 257 * 641 * 65537 * 6700417).
SPLITS = [p for p in range(3, 400, 2) if all(p % q for q in range(3, p, 2) if q * q <= p)] + \
    [641, 6700417, 274177, 67280421310721]
SEED = 20261008

DRIVER = r"""/* The driver of tests/test_cint_rt_wide.py: the runtime itself, so that its static
 * functions can be called. Reads cases from stdin and writes one answer line per case. */
#include "cint_rt.c"

#include <inttypes.h>

static const cint_site_info wd_sites[2] = {{0u, 0u, NULL}, {1u, 1u, "builtin"}};
static const cint_module wd_module = {"wide.ci", 7u, 2u, wd_sites};
static const cint_module *const wd_modules[1] = {&wd_module};
static const cint_program wd_program = {1u, 0u, wd_modules, NULL};
static cint_fault_record wd_rec;

static void wd_value(const cint_tvalue *v)
{
    char text[720];
    (void)cint_tvalue_render(v, text, sizeof text);
    printf("%s", text);
}

/* "ok;<bits>", or "fault;<code>;<operation>;<operands>;<exact>;<limit>;<1 when *out is unwritten>".
 * With `faulted`, the call is made on a context that already holds a muldiv E_DIV_ZERO. On a
 * context without a fault, cint_rt_builtin_fast must return the same bits exactly when the call
 * succeeds; otherwise the line is "fast;<bits or ->". */
static void wd_builtin(cint_ctx *ctx, const uint64_t *f, bool faulted)
{
    cint_site site = {0u, 1u};
    uint64_t out = 0xA5A5A5A5A5A5A5A5u, first = 0u, fast = 0u;
    bool fast_ok = false, ok;
    unsigned i;
    if (faulted) {
        (void)cint_rt_builtin(ctx, site, BI_MULDIV, CINT_TAG_I64, 1u, CINT_TAG_I64, 1u, 1u, 0u, &first);
    } else {
        fast_ok = cint_rt_builtin_fast((uint32_t)f[0], (uint32_t)f[1], (uint32_t)f[2], (uint32_t)f[3], f[4], f[5],
                                       f[6], &fast);
    }
    ok = cint_rt_builtin(ctx, site, (uint32_t)f[0], (uint32_t)f[1], (uint32_t)f[2], (uint32_t)f[3], f[4], f[5], f[6],
                         &out);
    if (!faulted && (fast_ok != ok || (ok && fast != out))) {
        if (fast_ok) {
            printf("fast;%016" PRIx64 "\n", fast);
        } else {
            printf("fast;-\n");
        }
        (void)cint_ctx_clear_fault(ctx);
        return;
    }
    if (ok) {
        printf("ok;%016" PRIx64 "\n", out);
        return;
    }
    (void)cint_ctx_fault(ctx, &wd_rec);
    printf("fault;%u;%.*s;", (unsigned)wd_rec.code, (int)wd_rec.operation_len, wd_rec.operation);
    for (i = 0; i < wd_rec.operand_count; i++) {
        printf("%s", i == 0u ? "" : ",");
        wd_value(&wd_rec.operands[i]);
    }
    printf(";");
    if (wd_rec.has_exact != 0u) {
        wd_value(&wd_rec.exact);
    } else {
        printf("-");
    }
    printf(";");
    if (wd_rec.has_limit != 0u) {
        wd_value(&wd_rec.limit);
    } else {
        printf("-");
    }
    printf(";%d\n", out == 0xA5A5A5A5A5A5A5A5u ? 1 : 0);
    (void)cint_ctx_clear_fault(ctx);
}

int main(void)
{
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL;
    size_t cap = (size_t)1 << 20, len = 0u, got;
    char *buf = (char *)malloc(cap + 1u), *p, *end;
    if (buf == NULL) {
        return 2;
    }
    while ((got = fread(buf + len, 1u, cap - len, stdin)) > 0u) {
        len += got;
        if (len == cap) {
            char *grown = (char *)realloc(buf, 2u * cap + 1u);
            if (grown == NULL) {
                return 2;
            }
            buf = grown;
            cap *= 2u;
        }
    }
    buf[len] = '\0';
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = &wd_program;
    if (cint_ctx_create(&cfg, &ctx) != CINT_OK) {
        return 2;
    }
    for (p = buf; *p != '\0';) {
        uint64_t f[7] = {0u, 0u, 0u, 0u, 0u, 0u, 0u};
        cint_rt_z128 q = z_make(false, 0u, 0u);
        char kind;
        unsigned i, n;
        while (*p == ' ' || *p == '\n' || *p == '\r') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        kind = *p++;
        n = kind == 'd' || kind == 'w' ? 3u : kind == 's' || kind == 'z' ? 1u : 7u;
        for (i = 0; i < n; i++) {
            f[i] = (uint64_t)strtoull(p, &end, 16);
            p = end;
        }
        if (kind == 'd') {
            uint64_t rem = div128(f[0], f[1], f[2], &q);
            printf("%016" PRIx64 " %016" PRIx64 " %016" PRIx64 "\n", q.hi, q.lo, rem);
        } else if (kind == 'w') {
            uint64_t rem, hi, quot = cint_rt_div_wide_b(f[0], f[1], f[2], &rem);
            uint64_t lo = cint_rt_mul_wide_b(f[1], f[2], &hi);
            printf("%016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %016" PRIx64 "\n", quot, rem, hi, lo);
        } else if (kind == 's') {
            printf("%016" PRIx64 "\n", cint_rt_isqrt64(f[0]));
        } else if (kind == 'z') {
            printf("%u\n", cint_rt_clz64(f[0]));
        } else {
            wd_builtin(ctx, f, kind == 'f');
        }
    }
    cint_ctx_destroy(ctx);
    free(buf);
    return 0;
}
"""


def bounds(t):
    w = int(t[1:])
    return (-(1 << (w - 1)), (1 << (w - 1)) - 1) if t[0] == "I" else (0, (1 << w) - 1)


def isqrt(n):
    """The largest r with r * r <= n, by bisection on Python integers."""
    lo, hi = 0, 1 << ((n.bit_length() + 1) // 2)
    while lo < hi:
        mid = (lo + hi + 1) // 2
        lo, hi = (mid, hi) if mid * mid <= n else (lo, mid - 1)
    return lo


def round_div(n, d, mode):
    """The rational n / d (d != 0) rounded by `mode` (SPEC-01 4.5)."""
    if d < 0:
        n, d = -n, -d
    q, r = divmod(n, d)  # n / d = q + r / d with 0 <= r < d; the rational is positive when q >= 0
    if r == 0:
        return q
    if mode in ("floor", "ceil", "trunc", "away"):
        up = {"floor": False, "ceil": True, "trunc": q < 0, "away": q >= 0}[mode]
    elif 2 * r != d:
        up = 2 * r > d
    else:
        up = {"half_even": q % 2 == 1, "half_away": q >= 0, "half_trunc": q < 0, "half_up": True,
              "half_down": False}[mode]
    return q + 1 if up else q


def isqrt_round(n, mode):
    r = isqrt(n)
    if r * r == n or mode in ("floor", "trunc"):
        return r
    if mode in ("ceil", "away"):
        return r + 1
    return r + 1 if n > r * r + r else r  # no ties: n > (r + 1/2)^2 exactly when n > r^2 + r


def corrections(hi, lo, d):
    """The digit corrections of div128's two digits (Knuth's algorithm D on the normalized
    divisor), or None when the division takes no digit step."""
    hi %= d
    if hi == 0:
        return None
    s = 64 - d.bit_length()
    d <<= s
    hi, lo = (((hi << 64) | lo) << s) >> 64, (lo << s) & MASK
    found = []
    for w in (lo >> 32, lo & 0xFFFFFFFF):
        digit, rhat = divmod(hi, d >> 32)
        n = 0
        while digit >> 32 or digit * (d & 0xFFFFFFFF) > ((rhat << 32) | w):
            digit, rhat, n = digit - 1, rhat + (d >> 32), n + 1
            if rhat >> 32:
                break
        hi = ((hi << 32) | w) - digit * d
        found.append(n)
    return tuple(found)


def expect_builtin(op, t, mode, r, a, b, c):
    """The driver's line for one call on the operand values a, b, c of type t."""
    parts = [op, "checked", t.lower()] + ([r.lower()] if op == "muldiv" else []) + \
        ([MODES[mode - 1]] if mode else [])
    operands = {"div_round": (a, b), "muldiv": (a, b, c)}.get(op, (a,))

    def fault(code, exact="-", limit="-"):
        return "fault;%d;%s;%s;%s;%s;1" % (CODES[code], ".".join(parts),
                                           ",".join("%s %d" % (t, v) for v in operands), exact, limit)

    if (op == "div_round" and b == 0) or (op == "muldiv" and c == 0):
        return fault("DIV_ZERO")
    if op.startswith("isqrt") and a < 0:
        return fault("DOMAIN")
    exact = (round_div(a, b, MODES[mode - 1]) if op == "div_round"
             else round_div(a * b, c, MODES[mode - 1]) if op == "muldiv"
             else isqrt(a) if op == "isqrt" else isqrt_round(a, MODES[mode - 1]))
    rt = r if op == "muldiv" else t
    lo, hi = bounds(rt)
    if lo <= exact <= hi:
        return "ok;%016x" % (exact & MASK)
    return fault("OVERFLOW", "Z %d" % exact, "%s %d" % (rt, hi if exact > hi else lo))


class Cases:
    """The case lines and the answer lines, in step."""

    def __init__(self):
        self.rng = random.Random(SEED)
        self.lines, self.answers, self.digits = [], [], []

    def add(self, line, answer):
        self.lines.append(line)
        self.answers.append(answer)

    def div(self, hi, lo, d):
        q, r = divmod((hi << 64) | lo, d)
        self.add("d %x %x %x" % (hi, lo, d), "%016x %016x %016x" % (q >> 64, q & MASK, r))
        self.digits.append(corrections(hi, lo, d))
        if hi < d:
            self.add("w %x %x %x" % (hi, lo, d), "%016x %016x %016x %016x" % (q, r, lo * d >> 64, lo * d & MASK))

    def pattern(self, t, v):
        """v's 64-bit pattern under t, with random bits above t's width half of the time."""
        w = int(t[1:])
        if w == 64 or self.rng.getrandbits(1):
            return v & MASK
        return (v & ((1 << w) - 1)) | (self.rng.getrandbits(64 - w) << w)

    def builtin(self, op, t, mode, r, a, b=0, c=0, faulted=False):
        aux = TYPES[r] if op == "muldiv" else 0
        self.add("%s %x %x %x %x %x %x %x" % ("f" if faulted else "b", OPS[op], TYPES[t], mode, aux,
                                               self.pattern(t, a), self.pattern(t, b), self.pattern(t, c)),
                 expect_builtin(op, t, mode, r, a, b, c))

    def value(self, t):
        """A random value of t: uniform, of a random bit length, or near a bound."""
        lo, hi = bounds(t)
        kind = self.rng.randrange(4)
        if kind == 2:
            v = self.rng.getrandbits(self.rng.randrange(int(t[1:]) + 1))
            v = -v if lo < 0 and self.rng.getrandbits(1) else v
            if lo <= v <= hi:
                return v
        if kind == 3:
            near = self.rng.randrange(min(1000, hi - lo + 1))
            return hi - near if self.rng.getrandbits(1) else lo + near
        return lo + self.rng.randrange(hi - lo + 1)


def edges(t):
    """A few values of t at its bounds, around zero and around half its range."""
    lo, hi = bounds(t)
    return sorted({v for v in (lo, lo + 1, lo // 2, -3, -2, -1, 0, 1, 2, 3, 7, hi // 2, hi // 2 + 1, hi - 1, hi)
                   if lo <= v <= hi})


def powers(t):
    """Every power of two of t, its neighbors and their negations, within t."""
    lo, hi = bounds(t)
    found = set()
    for k in range(int(t[1:])):
        found |= {(1 << k) - 1, 1 << k, (1 << k) + 1, -(1 << k) + 1, -(1 << k), -(1 << k) - 1}
    return sorted(v for v in found | {lo, hi} if lo <= v <= hi)


def div128_cases(cs):
    rng = cs.rng
    hard = [(1 << 63) | (1 << 32) - 1, (1 << 63) | (1 << 32) - 2, (1 << 63) | (1 << 31) + 1, (1 << 64) - 1,
            1 << 63, (1 << 63) + 1, (1 << 63) - 1, (1 << 32) + 1, 1 << 32, (1 << 32) - 1, 3, 2, 1,
            0xFFFFFFFF00000000, 0x80000000FFFFFF00, 0x00000000FFFFFFFF, 0x0000000180000001]
    hard += [(1 << 63) | (rng.getrandbits(31) << 32) | (1 << 31) | rng.getrandbits(31) for _ in range(8)]
    for d in hard:
        for hi in (0, 1, d - 1, d, d + 1, MASK, d // 2, rng.getrandbits(64)):
            for lo in (0, 1, MASK, MASK - 1, 1 << 63, rng.getrandbits(64)):
                if 0 <= hi <= MASK:
                    cs.div(hi, lo, d)
    # Partial remainders one below the divisor at the second digit: hi:lo = ((q1 d + d - 1) << 32) | w0
    # for a normalized d, then the same shifted right so that the divisor is not normalized.
    for _ in range(300):
        d = (1 << 63) | (rng.choice((0, rng.getrandbits(31))) << 32) | rng.choice(
            ((1 << 32) - 1, (1 << 32) - 2, (1 << 31) + rng.getrandbits(31), rng.getrandbits(32)))
        n = (((rng.getrandbits(32) * d + d - 1 - rng.randrange(3)) << 32) | rng.getrandbits(32))
        s = rng.choice((0, 0, rng.randrange(1, 32)))
        d >>= s
        cs.div(n >> (64 + s), (n >> s) & MASK, d)
    for _ in range(6000):
        d = rng.getrandbits(rng.choice((64, 64, 63, 33, 32, 20, 8))) or 1
        hi = rng.choice((rng.getrandbits(64), rng.randrange(d), d - 1 - rng.randrange(min(d, 16))))
        cs.div(hi, rng.getrandbits(64), d)


def isqrt_cases(cs):
    rng = cs.rng
    values = set(range(1 << 12)) | {MASK, MASK - 1}
    for k in range(64):
        values |= {(1 << k) - 1, 1 << k, (1 << k) + 1}
    roots = [(1 << 32) - 1, (1 << 32) - 2, 1 << 31, (1 << 31) - 1, 3037000499, 3037000500, 4294967295 // 3]
    roots += [rng.getrandbits(rng.randrange(1, 33)) for _ in range(1500)]
    for k in roots:
        values |= {v for v in (k * k - 1, k * k, k * k + 1, k * k + 2 * k, k * k + k, k * k + k + 1) if 0 <= v <= MASK}
    values |= {rng.getrandbits(rng.randrange(1, 65)) for _ in range(1500)}
    # The seed table's cells: n << s in [t * 2^54, (t + 1) * 2^54) for even s.
    for t in range(256, 1024):
        for s in range(0, 64, 2):
            values |= {v for v in ((t << 54) >> s, (((t + 1) << 54) - 1) >> s) if v >= 2}
    for n in sorted(values):
        cs.add("s %x" % n, "%016x" % isqrt(n))
    for k in range(64):
        for x in (1 << k, (1 << (k + 1)) - 1, (1 << k) | rng.getrandbits(k) if k else 1):
            cs.add("z %x" % x, "%d" % (64 - x.bit_length()))


def split(n, t):
    """(a, b) of type t with a * b = n, or None."""
    lo, hi = bounds(t)
    for d in [1] + SPLITS:
        if n % d == 0:
            for a, b in ((n // d, d), (d, n // d), (-(n // d), -d)):
                if lo <= a <= hi and lo <= b <= hi:
                    return a, b
    return None


def builtin_cases(cs):
    rng = cs.rng
    for t in TYPES:
        lo, hi = bounds(t)
        small = edges(t)
        # isqrt and isqrt_round: every 8-bit value; elsewhere bounds, powers, squares and random.
        values = set(range(lo, hi + 1)) if hi - lo < 256 else set(powers(t)) | set(small)
        if hi - lo >= 256:
            for _ in range(120):
                k = rng.randrange(isqrt(hi) + 1)
                values |= {v for v in (k * k - 1, k * k, k * k + 1, k * k + k, k * k + k + 1) if lo <= v <= hi}
                values.add(cs.value(t))
        for a in sorted(values):
            cs.builtin("isqrt", t, 0, t, a)
            for mode in range(1, 10):
                cs.builtin("isqrt_round", t, mode, t, a)
        for mode in range(1, 10):
            # div_round: every pair of edge values, random pairs, and ties a / b = m / 2 with m odd.
            for a in small:
                for b in small:
                    cs.builtin("div_round", t, mode, t, a, b)
            for _ in range(40):
                cs.builtin("div_round", t, mode, t, cs.value(t), cs.value(t))
            for _ in range(24):
                k = rng.randrange(1, hi // 2 + 1)
                m = (rng.randrange(hi // k + 1) | 1) * (-1 if lo < 0 and rng.getrandbits(1) else 1)
                if lo <= k * m <= hi:
                    cs.builtin("div_round", t, mode, t, k * m, 2 * k * (-1 if lo < 0 and rng.getrandbits(1) else 1))
        for r in TYPES:
            rlo, rhi = bounds(r)
            pairs = [(1, 1), (1, 2), (3, 2), (2, 3), (7, 5), (5, 7), (10, 3), (1000, 999), (999, 1000),
                     (hi, hi - 1), (hi - 1, hi), (hi, 2), (2, hi), ((1 << 31) + 1, 1 << 31), (1 << 31, (1 << 31) + 1)]
            pairs = [(sb * b, sc * c) for b, c in pairs for sb in (1, -1) for sc in (1, -1)
                     if lo <= sb * b <= hi and lo <= sc * c <= hi and b * c != 0]
            for mode in range(1, 10):
                for _ in range(12):
                    cs.builtin("muldiv", t, mode, r, rng.choice(small), rng.choice(small), rng.choice(small))
                for _ in range(16):
                    cs.builtin("muldiv", t, mode, r, cs.value(t), cs.value(t), cs.value(t))
                # Exact results at each bound of r, one past it and one before: a * b / c near the bound.
                for target in (rlo, rhi):
                    for b, c in pairs:
                        a0 = target * c // b
                        for a in range(a0 - 2, a0 + 3):
                            if lo <= a <= hi:
                                cs.builtin("muldiv", t, mode, r, a, b, c)
                    # Ties on both sides of the bound: a * b / c = target +- 1/2.
                    for n in (2 * target + 1, 2 * target - 1):
                        ab = split(n, t)
                        if ab:
                            cs.builtin("muldiv", t, mode, r, ab[0], ab[1], 2)
                            if lo <= -2:
                                cs.builtin("muldiv", t, mode, r, -ab[0], ab[1], -2)
                # Ties away from the bounds: a * b / c = m / 2 with c = 2k.
                for _ in range(6):
                    k = rng.randrange(1, hi // 2 + 1)
                    sign = -1 if lo < 0 and rng.getrandbits(1) else 1
                    cs.builtin("muldiv", t, mode, r, sign * k, rng.randrange(hi + 1) | 1, 2 * k)
                # The divisors 0 and 1.
                cs.builtin("muldiv", t, mode, r, cs.value(t), cs.value(t), 0)
                cs.builtin("muldiv", t, mode, r, cs.value(t), cs.value(t), 1)


def fast_path_cases(cs):
    """I64 muldiv to I64: full-width operands whose results are in range, then the inputs that the
    fast path must leave to the generic path."""
    rng = cs.rng
    lo, hi = bounds("I64")
    for mode in range(1, 10):
        for _ in range(800):
            a, b = cs.value("I64"), cs.value("I64")
            y = rng.getrandbits(rng.randrange(1, 64)) or 1
            c = max(1, min(hi, abs(a * b) // y + rng.randrange(-2, 3)))
            cs.builtin("muldiv", "I64", mode, "I64", a, b, -c if rng.getrandbits(1) else c)
        for _ in range(200):
            m = rng.randrange(1, 1 << 62)
            cs.builtin("muldiv", "I64", mode, "I64", m * rng.choice((1, -1)), rng.randrange(hi) | 1, 2 * m)
    for a, b, c in ((5, 7, 3), (lo, -1, 1), (hi, hi, hi), (1, 1, 0)):
        for mode in (0, 10):
            line = "b %x %x %x %x %x %x %x" % (OPS["muldiv"], TYPES["I64"], mode, TYPES["I64"], a & MASK,
                                                b & MASK, c & MASK)
            cs.add(line, "fault;%d;dispatch.admit;;-;-;1" % CODES["UNSUPPORTED"])
    first = expect_builtin("muldiv", "I64", 1, "I64", 1, 1, 0)
    for _ in range(40):
        cs.add("f %x %x %x %x %x %x %x" % (OPS["muldiv"], TYPES["I64"], rng.randrange(1, 10), TYPES["I64"],
                                            cs.value("I64") & MASK, cs.value("I64") & MASK,
                                            (cs.value("I64") | 1) & MASK), first)
    cs.add("f %x %x 1 0 %x 3 0" % (OPS["div_round"], TYPES["I32"], 9), first)
    cs.add("f %x %x 0 0 %x 0 0" % (OPS["isqrt"], TYPES["U64"], 16), first)


def legs():
    if os.name == "nt":
        return ["msvc"]
    if sys.platform == "darwin":
        return ["apple-clang"]
    return [leg for leg in ("gcc", "clang") if shutil.which(leg)]


class WideArithmetic(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cases = Cases()
        div128_cases(cls.cases)
        isqrt_cases(cls.cases)
        builtin_cases(cls.cases)
        fast_path_cases(cls.cases)

    def test_div128_cases_reach_every_correction(self):
        steps = [d for d in self.cases.digits if d is not None]
        for k in (0, 1):
            self.assertEqual({d[k] for d in steps}, {0, 1, 2}, "digit %d" % (k + 1))

    def test_every_build_agrees(self):
        found = legs()
        if not found:
            self.skipTest("no gcc or clang on this host")
        root = cc.build_root() / "rt-wide"
        root.mkdir(parents=True, exist_ok=True)
        sanitize = os.environ.get("INTOS_SANITIZE") == "1"
        text = "".join(line + "\n" for line in self.cases.lines).encode("ascii")
        with tempfile.TemporaryDirectory(prefix="wide-", dir=root) as tmp:
            work = pathlib.Path(tmp)
            driver = work / "wide_driver.c"
            driver.write_bytes(DRIVER.encode("ascii"))
            for leg in found:
                for opt in ("0", "2"):
                    for helpers in ("portable", "builtin"):
                        name = "%s-O%s-%s" % (leg, opt, helpers)
                        with self.subTest(build=name):
                            out = work / name
                            out.mkdir()
                            tc = cc.Toolchain(leg, opt, helpers, sanitize and leg in ("gcc", "clang"))
                            exe = tc.link_exe(tc.compile([driver], out, name), out / "wide", name)
                            run = subprocess.run([str(exe)], input=text, capture_output=True, timeout=300)
                            self.assertEqual(run.returncode, 0, run.stderr[-2000:])
                            self.assertEqual(run.stderr, b"")
                            got = run.stdout.decode("ascii").splitlines()
                            self.assertEqual(len(got), len(self.cases.answers))
                            wrong = [(line, want, have) for line, want, have in
                                     zip(self.cases.lines, self.cases.answers, got) if want != have]
                            self.assertEqual(wrong[:5], [], "%d of %d cases differ" % (len(wrong), len(got)))


if __name__ == "__main__":
    unittest.main()
