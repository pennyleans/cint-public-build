/* test_wide_fast.c: the 2- and 4-limb paths of rt/cint_wide.h and
 * rt/cint_wide.c against the limb loops (rt/OPEN.md RT-OQ-62, RT-OQ-66,
 * RT-OQ-69).
 *
 * rt/tests/test_rt.py builds this driver with the runtime twice in every
 * configuration of a leg, once as is and once with CINT_W_GENERIC, which keeps
 * the loops for every limb count, and requires the two builds, and every
 * configuration, to print the same digest lines. A line is one family of
 * helpers of one of I128, U128, I256 and U256: its call count and a digest of
 * every outcome, the returned flag or ArithError tag, the result slot (filled
 * with a sentinel first, so a slot that a fault leaves unwritten counts), and
 * the canonical bytes of each fault record (cint_fault_encode). The operands
 * are every pair of boundary values (0, and 2^k - 1, 2^k and 2^k + 1 for k in
 * 1, 2, 31 to 33, 62 to 65, 126 to 129, 190 to 193, 254 and 255 below the
 * width, and their negations) and `pairs` seeded pseudo-random pairs (default
 * 2^20), drawn from uniform limbs, values of a random bit length and either
 * sign, limbs of carry and borrow patterns, and boundary values with small
 * offsets. The driver itself checks that every helper whose slot may alias an
 * input (SPEC-09 EMIT-29) gives the same outcome when it does, and that
 * cint_q_up and cint_q_shr of rt/cint_fixed.h agree with the forms they
 * replaced, cint_q_up exhaustively. Prints the helper mode, the path, the
 * lines and the checks, and exits 1 on a failed check.
 *
 * The products of I512, U512 and I1024 are compared the same way (RT-OQ-66),
 * and so are their quotients (RT-OQ-69).
 *
 * Usage: test_wide_fast [pairs [128|256|1024]], where a width runs the types of
 * that width alone (and cint_q_shr's checks with 128); 1024 runs the products
 * and quotients of I512, U512 and I1024.
 */
#include "cint_rt.h"
#include "cint_rt_host.h"
#include "cint_fixed.h"
#include "cint_wide.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const cint_site_info f_sites[] = {{0u, 0u, NULL}, {1u, 1u, "wide"}};
static const cint_module f_module = {"wide.ci", 7u, 2u, f_sites};
static const cint_module *const f_modules[] = {&f_module};
static const cint_program f_program = {1u, 0u, f_modules, NULL};
static const cint_site F_SITE = {0u, 1u};

#define SENTINEL 0xA5A5A5A5A5A5A5A5u
#define BOUNDS 128

enum {
    F_ADD, F_SUB, F_MUL, F_DIV, F_REM, F_NEG, F_WRAP, F_SAT, F_RESULT, F_CMP, F_ABS, F_DIVK, F_ROUND, F_ISQRT,
    F_SHIFT, F_NARROW, F_WIDE, F_MULDIV, F_FULL, FAMILIES
};
static const char *const family_names[FAMILIES] = {
    "add", "sub", "mul", "div", "rem", "neg", "wrap", "sat", "result", "compare", "abs-clamp", "div-kinds",
    "div-round", "isqrt", "shift", "narrow", "wide", "muldiv", "full"};
static const char *const type_names[4] = {"i128", "u128", "i256", "u256"};

typedef struct digest {
    uint64_t h;
    uint64_t calls;
} digest;

/* A result slot of muldiv, of any result type. */
typedef union mout {
    int8_t i8;
    int32_t i32;
    int64_t i64;
    uint8_t u8;
    uint64_t u64;
    uint64_t limb[8];
} mout;

static digest g_digest[4][FAMILIES];
static long g_checks;
static long g_failed;
static uint64_t g_state;
static cint_fault_record g_rec;
static uint8_t g_bytes[4096];
static uint64_t g_bound[2][BOUNDS][4];
static unsigned g_nbound[2];

static void check(int ok, const char *what)
{
    g_checks++;
    if (!ok && ++g_failed <= 20) printf("FAIL %s\n", what);
}

static void mix(digest *d, uint64_t x)
{
    uint64_t h = (d->h ^ x) * 0x9E3779B97F4A7C15u;
    d->h = h ^ (h >> 32);
}

/* One call's outcome: its flag, the n limbs of its slot, and the bytes of the
 * fault record it left, which is then cleared. */
static void outcome(digest *d, cint_ctx *ctx, uint64_t flag, const uint64_t *limb, size_t n)
{
    size_t i, len;
    uint64_t w = 0u;
    d->calls++;
    mix(d, flag);
    for (i = 0u; i < n; i++) mix(d, limb[i]);
    if (!cint_rt_faulted(ctx) || cint_ctx_fault(ctx, &g_rec) != CINT_OK) {
        mix(d, 0u);
        return;
    }
    len = cint_fault_encode(&g_rec, g_bytes, sizeof g_bytes);
    mix(d, (uint64_t)len + 1u);
    for (i = 0u; i < len; i++) {
        w = (w << 8) | g_bytes[i];
        if (i % 8u == 7u || i + 1u == len) {
            mix(d, w);
            w = 0u;
        }
    }
    (void)cint_ctx_clear_fault(ctx);
}

/* A narrow result as its 64-bit pattern. */
static void narrow(digest *d, cint_ctx *ctx, uint64_t flag, uint64_t bits)
{
    outcome(d, ctx, flag, &bits, 1u);
}

/* The call with its slot aliasing an input against the plain call: the same
 * flag and value, or on a fault the input unwritten. */
static void alias(cint_ctx *ctx, bool ok, bool ok2, const uint64_t *r, const uint64_t *t, const uint64_t *in,
                  size_t n, const char *what)
{
    check(ok == ok2 && memcmp(t, ok ? r : in, 8u * n) == 0, what);
    if (cint_rt_faulted(ctx)) (void)cint_ctx_clear_fault(ctx);
}

static void fill(uint64_t *limb, size_t n)
{
    size_t i;
    for (i = 0u; i < n; i++) limb[i] = SENTINEL;
}

static uint64_t next(void)
{
    uint64_t z = (g_state += 0x9E3779B97F4A7C15u);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9u;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBu;
    return z ^ (z >> 31);
}

/* -v over n limbs. */
static void negate(uint64_t *v, unsigned n)
{
    uint64_t carry = 1u;
    unsigned i;
    for (i = 0u; i < n; i++) {
        v[i] = ~v[i] + carry;
        carry = carry != 0u && v[i] == 0u ? 1u : 0u;
    }
}

/* v + k over n limbs. */
static void add_small(uint64_t *v, unsigned n, int64_t k)
{
    uint64_t fill_limb = k < 0 ? UINT64_MAX : 0u, carry = 0u;
    unsigned i;
    for (i = 0u; i < n; i++) {
        uint64_t b = i == 0u ? (uint64_t)k : fill_limb, t = v[i] + b, r = t + carry;
        carry = (t < b ? 1u : 0u) | (r < t ? 1u : 0u);
        v[i] = r;
    }
}

static void make_bounds(void)
{
    static const unsigned ks[] = {1u,   2u,   31u,  32u,  33u,  62u,  63u,  64u,  65u, 126u,
                                  127u, 128u, 129u, 190u, 191u, 192u, 193u, 254u, 255u};
    unsigned w, i, j, c, half;
    for (w = 0u; w < 2u; w++) {
        unsigned n = w == 0u ? 2u : 4u;
        memset(g_bound[w], 0, sizeof g_bound[w]);
        c = 1u;  /* g_bound[w][0] = 0 */
        for (i = 0u; i < sizeof ks / sizeof ks[0]; i++) {
            if (ks[i] >= 64u * n) continue;
            for (j = 0u; j < 3u; j++) {
                g_bound[w][c][ks[i] / 64u] = (uint64_t)1 << (ks[i] % 64u);
                add_small(g_bound[w][c], n, (int64_t)j - 1);
                c++;
            }
        }
        half = c;
        for (i = 1u; i < half; i++) {
            memcpy(g_bound[w][c], g_bound[w][i], sizeof g_bound[w][c]);
            negate(g_bound[w][c], n);
            c++;
        }
        g_nbound[w] = c;
    }
}

/* A pseudo-random operand of n limbs (w = 0 for 2, 1 for 4). */
static void draw(uint64_t *v, unsigned n, unsigned w)
{
    static const uint64_t limbs[] = {0u, 1u, 2u, 0xFFFFFFFFu, 0x100000000u, 0x7FFFFFFFFFFFFFFFu,
                                     0x8000000000000000u, 0x8000000000000001u, 0xFFFFFFFFFFFFFFFEu,
                                     0xFFFFFFFFFFFFFFFFu};
    uint64_t pick = next();
    unsigned i, bits;
    memset(v, 0, 4u * sizeof *v);
    switch (pick & 3u) {
    case 0u:
        for (i = 0u; i < n; i++) v[i] = next();
        break;
    case 1u:
        bits = (unsigned)((pick >> 2) % (64u * n + 1u));
        for (i = 0u; i < n && 64u * i < bits; i++) {
            v[i] = next();
            if (bits - 64u * i < 64u) v[i] &= ((uint64_t)1 << (bits - 64u * i)) - 1u;
        }
        if (((pick >> 12) & 1u) != 0u) negate(v, n);
        break;
    case 2u:
        for (i = 0u; i < n; i++) v[i] = limbs[next() % (sizeof limbs / sizeof limbs[0])];
        break;
    default:
        memcpy(v, g_bound[w][(pick >> 2) % g_nbound[w]], 4u * sizeof *v);
        add_small(v, n, (int64_t)((pick >> 20) % 7u) - 3);
        break;
    }
}

/* cint_q_up and cint_q_shr of rt/cint_fixed.h as they stood before RT-OQ-62. */
static bool ref_q_up(bool half, bool sticky, bool odd, bool negative, uint32_t mode)
{
    if (!half && !sticky) return false;
    if (mode <= 4u) return mode == 1u ? negative : mode == 2u ? !negative : mode == 4u;
    if (!half) return false;
    if (sticky) return true;
    return mode == 5u ? odd : mode == 6u || (mode == 8u ? !negative : mode == 9u && negative);
}

static cint_q_mag ref_q_shr(cint_q_mag m, unsigned k, uint32_t mode)
{
    cint_q_mag q = m;
    bool half, sticky;
    if (k == 0u) return m;
    if (k < 64u) {
        half = ((m.lo >> (k - 1u)) & 1u) != 0u;
        sticky = (m.lo & (((uint64_t)1 << (k - 1u)) - 1u)) != 0u;
        q.lo = (m.lo >> k) | (m.hi << (64u - k));
        q.hi = m.hi >> k;
    } else if (k == 64u) {
        half = (m.lo >> 63) != 0u;
        sticky = (m.lo << 1) != 0u;
        q.lo = m.hi;
        q.hi = 0u;
    } else {
        half = ((m.hi >> (k - 65u)) & 1u) != 0u;
        sticky = m.lo != 0u || (m.hi & (((uint64_t)1 << (k - 65u)) - 1u)) != 0u;
        q.lo = m.hi >> (k - 64u);
        q.hi = 0u;
    }
    if (ref_q_up(half, sticky, (q.lo & 1u) != 0u, m.negative, mode) && ++q.lo == 0u) q.hi++;
    return q;
}

static void check_fixed(unsigned long count)
{
    unsigned f, mode, k;
    unsigned long i;
    for (f = 0u; f < 16u; f++) {
        for (mode = 0u; mode < 16u; mode++) {
            bool half = (f & 1u) != 0u, sticky = (f & 2u) != 0u, odd = (f & 4u) != 0u, neg = (f & 8u) != 0u;
            uint64_t up = cint_q_up(f & 1u, (f >> 1) & 1u, (f >> 2) & 1u, (f >> 3) & 1u, mode);
            check(up == (ref_q_up(half, sticky, odd, neg, mode) ? 1u : 0u), "cint_q_up");
        }
    }
    for (i = 0u; i < count; i++) {
        uint64_t v[4];
        cint_q_mag m;
        draw(v, 2u, 0u);
        m.lo = v[0];
        m.hi = v[1];
        m.negative = (next() & 1u) != 0u;
        for (k = 0u; k < 128u; k++) {
            for (mode = 0u; mode <= 9u; mode++) {
                cint_q_mag got = cint_q_shr(m, k, mode), want = ref_q_shr(m, k, mode);
                check(got.lo == want.lo && got.hi == want.hi && got.negative == want.negative, "cint_q_shr");
            }
        }
    }
}

/* The result tags muldiv is called with, beside the operands' own. */
static const uint32_t muldiv_tags[] = {CINT_TAG_I64,  CINT_TAG_U64,  CINT_TAG_I32,  CINT_TAG_U8,
                                       CINT_TAG_I128, CINT_TAG_U128, CINT_TAG_I256, CINT_TAG_U256,
                                       CINT_TAG_I512, CINT_TAG_U512};

static void extra_i128(cint_ctx *ctx, digest *d, const cint_i128 *a, uint64_t pick)
{
    cint_u128 u;
    (void)pick;
    fill(u.limb, 2u);
    cint_uabs_i128(a, &u);
    outcome(d + F_FULL, ctx, 0u, u.limb, 2u);
}

static void extra_i256(cint_ctx *ctx, digest *d, const cint_i256 *a, uint64_t pick)
{
    cint_u256 u;
    (void)pick;
    fill(u.limb, 4u);
    cint_uabs_i256(a, &u);
    outcome(d + F_FULL, ctx, 0u, u.limb, 4u);
}

#define ROTATIONS(T, N)                                                               \
    static void extra_##T(cint_ctx *ctx, digest *d, const cint_##T *a, uint64_t pick) \
    {                                                                                 \
        uint64_t kb = (pick >> 8) % (64u * (N) + 2u);                                 \
        cint_##T r;                                                                   \
        bool ok;                                                                      \
        fill(r.limb, (N));                                                            \
        ok = cint_rotl_##T(ctx, F_SITE, a, cint_count_u64(kb), &r);                   \
        outcome(d + F_SHIFT, ctx, ok, r.limb, (N));                                   \
        fill(r.limb, (N));                                                            \
        ok = cint_rotr_##T(ctx, F_SITE, a, cint_count_u64(kb), &r);                   \
        outcome(d + F_SHIFT, ctx, ok, r.limb, (N));                                   \
    }
ROTATIONS(u128, 2u)
ROTATIONS(u256, 4u)

/* Every family of one type over the operands x, y and z; pick chooses the
 * shift count, the rounding mode and muldiv's result tag. */
#define FAST_TYPE(T, N, TAG, T2)                                                                                \
    static void run_##T(cint_ctx *ctx, digest *d, const uint64_t *x, const uint64_t *y, const uint64_t *z,      \
                        uint64_t pick)                                                                          \
    {                                                                                                           \
        cint_##T a, b, c, r, t, kb;                                                                             \
        cint_##T2 full;                                                                                         \
        cint_i128 w1;                                                                                           \
        cint_u128 w2;                                                                                           \
        cint_i256 w3;                                                                                           \
        cint_u256 w4;                                                                                           \
        cint_i512 w5;                                                                                           \
        mout m, m2;                                                                                             \
        int64_t o64;                                                                                            \
        uint64_t u64, bits[6];                                                                                  \
        int32_t o32;                                                                                            \
        uint8_t o8;                                                                                             \
        uint32_t op, mode = (uint32_t)(pick % 10u), mmode = 1u + (uint32_t)((pick >> 4) % 9u);                  \
        uint32_t rtag = muldiv_tags[(pick >> 12) % (sizeof muldiv_tags / sizeof muldiv_tags[0])];               \
        bool ok, ok2;                                                                                           \
        uint16_t e;                                                                                             \
        memcpy(a.limb, x, sizeof a.limb);                                                                       \
        memcpy(b.limb, y, sizeof b.limb);                                                                       \
        memcpy(c.limb, z, sizeof c.limb);                                                                       \
        memset(kb.limb, 0, sizeof kb.limb);                                                                     \
        kb.limb[0] = (pick >> 8) % (64u * (N) + 4u);                                                            \
        fill(r.limb, (N));                                                                                      \
        ok = cint_add_##T(ctx, F_SITE, &a, &b, &r);                                                             \
        outcome(d + F_ADD, ctx, ok, r.limb, (N));                                                               \
        t = b;                                                                                                  \
        ok2 = cint_add_##T(ctx, F_SITE, &a, &t, &t);                                                            \
        alias(ctx, ok, ok2, r.limb, t.limb, b.limb, (N), #T " add");                                            \
        fill(r.limb, (N));                                                                                      \
        ok = cint_sub_##T(ctx, F_SITE, &a, &b, &r);                                                             \
        outcome(d + F_SUB, ctx, ok, r.limb, (N));                                                               \
        t = a;                                                                                                  \
        ok2 = cint_sub_##T(ctx, F_SITE, &t, &b, &t);                                                            \
        alias(ctx, ok, ok2, r.limb, t.limb, a.limb, (N), #T " sub");                                            \
        fill(r.limb, (N));                                                                                      \
        ok = cint_mul_##T(ctx, F_SITE, &a, &b, &r);                                                             \
        outcome(d + F_MUL, ctx, ok, r.limb, (N));                                                               \
        t = a;                                                                                                  \
        ok2 = cint_mul_##T(ctx, F_SITE, &t, &b, &t);                                                            \
        alias(ctx, ok, ok2, r.limb, t.limb, a.limb, (N), #T " mul into a");                                     \
        t = b;                                                                                                  \
        ok2 = cint_mul_##T(ctx, F_SITE, &a, &t, &t);                                                            \
        alias(ctx, ok, ok2, r.limb, t.limb, b.limb, (N), #T " mul into b");                                     \
        fill(r.limb, (N));                                                                                      \
        ok = cint_div_##T(ctx, F_SITE, &a, &b, &r);                                                             \
        outcome(d + F_DIV, ctx, ok, r.limb, (N));                                                               \
        t = b;                                                                                                  \
        ok2 = cint_div_##T(ctx, F_SITE, &a, &t, &t);                                                            \
        alias(ctx, ok, ok2, r.limb, t.limb, b.limb, (N), #T " div");                                            \
        fill(r.limb, (N));                                                                                      \
        ok = cint_rem_##T(ctx, F_SITE, &a, &b, &r);                                                             \
        outcome(d + F_REM, ctx, ok, r.limb, (N));                                                               \
        t = a;                                                                                                  \
        ok2 = cint_rem_##T(ctx, F_SITE, &t, &b, &t);                                                            \
        alias(ctx, ok, ok2, r.limb, t.limb, a.limb, (N), #T " rem");                                            \
        fill(r.limb, (N));                                                                                      \
        ok = cint_neg_##T(ctx, F_SITE, &a, &r);                                                                 \
        outcome(d + F_NEG, ctx, ok, r.limb, (N));                                                               \
        t = a;                                                                                                  \
        ok2 = cint_neg_##T(ctx, F_SITE, &t, &t);                                                                \
        alias(ctx, ok, ok2, r.limb, t.limb, a.limb, (N), #T " neg");                                            \
        cint_add_wrap_##T(&a, &b, &r);                                                                          \
        outcome(d + F_WRAP, ctx, 0u, r.limb, (N));                                                              \
        cint_sub_wrap_##T(&a, &b, &r);                                                                          \
        outcome(d + F_WRAP, ctx, 0u, r.limb, (N));                                                              \
        cint_mul_wrap_##T(&a, &b, &r);                                                                          \
        outcome(d + F_WRAP, ctx, 0u, r.limb, (N));                                                              \
        cint_neg_wrap_##T(&a, &r);                                                                              \
        outcome(d + F_WRAP, ctx, 0u, r.limb, (N));                                                              \
        cint_add_sat_##T(&a, &b, &r);                                                                           \
        outcome(d + F_SAT, ctx, 0u, r.limb, (N));                                                               \
        cint_sub_sat_##T(&a, &b, &r);                                                                           \
        outcome(d + F_SAT, ctx, 0u, r.limb, (N));                                                               \
        cint_mul_sat_##T(&a, &b, &r);                                                                           \
        outcome(d + F_SAT, ctx, 0u, r.limb, (N));                                                               \
        fill(r.limb, (N));                                                                                      \
        e = cint_add_result_##T(&a, &b, &r);                                                                    \
        outcome(d + F_RESULT, ctx, e, r.limb, (N));                                                             \
        fill(r.limb, (N));                                                                                      \
        e = cint_sub_result_##T(&a, &b, &r);                                                                    \
        outcome(d + F_RESULT, ctx, e, r.limb, (N));                                                             \
        fill(r.limb, (N));                                                                                      \
        e = cint_mul_result_##T(&a, &b, &r);                                                                    \
        outcome(d + F_RESULT, ctx, e, r.limb, (N));                                                             \
        fill(r.limb, (N));                                                                                      \
        e = cint_div_result_##T(&a, &b, &r);                                                                    \
        outcome(d + F_RESULT, ctx, e, r.limb, (N));                                                             \
        fill(r.limb, (N));                                                                                      \
        e = cint_rem_result_##T(&a, &b, &r);                                                                    \
        outcome(d + F_RESULT, ctx, e, r.limb, (N));                                                             \
        bits[0] = cint_eq_##T(&a, &b) ? 1u : 0u;                                                                \
        bits[0] |= cint_ne_##T(&a, &b) ? 2u : 0u;                                                               \
        bits[0] |= cint_lt_##T(&a, &b) ? 4u : 0u;                                                               \
        bits[0] |= cint_le_##T(&a, &b) ? 8u : 0u;                                                               \
        bits[0] |= cint_gt_##T(&a, &b) ? 16u : 0u;                                                              \
        bits[0] |= cint_ge_##T(&a, &b) ? 32u : 0u;                                                              \
        cint_min_##T(&a, &b, &r);                                                                               \
        outcome(d + F_CMP, ctx, bits[0], r.limb, (N));                                                          \
        cint_max_##T(&a, &b, &r);                                                                               \
        outcome(d + F_CMP, ctx, 0u, r.limb, (N));                                                               \
        fill(r.limb, (N));                                                                                      \
        ok = cint_builtin_##T(ctx, F_SITE, 1u, 0u, &a, NULL, NULL, &r);                                         \
        outcome(d + F_ABS, ctx, ok, r.limb, (N));                                                               \
        fill(r.limb, (N));                                                                                      \
        ok = cint_builtin_##T(ctx, F_SITE, 5u, 0u, &a, &b, &c, &r);                                             \
        outcome(d + F_ABS, ctx, ok, r.limb, (N));                                                               \
        for (op = 6u; op <= 9u; op++) {                                                                         \
            fill(r.limb, (N));                                                                                  \
            ok = cint_builtin_##T(ctx, F_SITE, op, 0u, &a, &b, NULL, &r);                                       \
            outcome(d + F_DIVK, ctx, ok, r.limb, (N));                                                          \
        }                                                                                                       \
        fill(r.limb, (N));                                                                                      \
        ok = cint_builtin_##T(ctx, F_SITE, 10u, mode, &a, &b, NULL, &r);                                        \
        outcome(d + F_ROUND, ctx, ok, r.limb, (N));                                                             \
        t = a;                                                                                                  \
        ok2 = cint_builtin_##T(ctx, F_SITE, 10u, mode, &t, &b, NULL, &t);                                       \
        alias(ctx, ok, ok2, r.limb, t.limb, a.limb, (N), #T " div_round into a");                               \
        t = b;                                                                                                  \
        ok2 = cint_builtin_##T(ctx, F_SITE, 10u, mode, &a, &t, NULL, &t);                                       \
        alias(ctx, ok, ok2, r.limb, t.limb, b.limb, (N), #T " div_round into b");                               \
        fill(r.limb, (N));                                                                                      \
        ok = cint_builtin_##T(ctx, F_SITE, 13u, 0u, &a, NULL, NULL, &r);                                        \
        outcome(d + F_ISQRT, ctx, ok, r.limb, (N));                                                             \
        fill(r.limb, (N));                                                                                      \
        ok = cint_builtin_##T(ctx, F_SITE, 14u, mode, &a, NULL, NULL, &r);                                      \
        outcome(d + F_ISQRT, ctx, ok, r.limb, (N));                                                             \
        fill(r.limb, (N));                                                                                      \
        ok = cint_shl_##T(ctx, F_SITE, &a, cint_count_##T(&kb), &r);                                            \
        outcome(d + F_SHIFT, ctx, ok, r.limb, (N));                                                             \
        fill(r.limb, (N));                                                                                      \
        ok = cint_shl_wrap_##T(ctx, F_SITE, &a, cint_count_##T(&kb), &r);                                       \
        outcome(d + F_SHIFT, ctx, ok, r.limb, (N));                                                             \
        fill(r.limb, (N));                                                                                      \
        ok = cint_shr_##T(ctx, F_SITE, &a, cint_count_##T(&kb), &r);                                            \
        outcome(d + F_SHIFT, ctx, ok, r.limb, (N));                                                             \
        fill(r.limb, (N));                                                                                      \
        e = cint_shl_result_##T(&a, cint_count_##T(&kb), &r);                                                   \
        outcome(d + F_SHIFT, ctx, e, r.limb, (N));                                                              \
        fill(r.limb, (N));                                                                                      \
        ok = cint_shr_##T(ctx, F_SITE, &a, cint_count_##T(&b), &r);                                             \
        outcome(d + F_SHIFT, ctx, ok, r.limb, (N));                                                             \
        memset(&o64, 0xA5, sizeof o64);                                                                         \
        ok = cint_as_i64_from_##T(ctx, F_SITE, &a, &o64);                                                       \
        narrow(d + F_NARROW, ctx, ok, (uint64_t)o64);                                                           \
        memset(&u64, 0xA5, sizeof u64);                                                                         \
        ok = cint_as_u64_from_##T(ctx, F_SITE, &a, &u64);                                                       \
        narrow(d + F_NARROW, ctx, ok, u64);                                                                     \
        memset(&o32, 0xA5, sizeof o32);                                                                         \
        ok = cint_as_i32_from_##T(ctx, F_SITE, &a, &o32);                                                       \
        narrow(d + F_NARROW, ctx, ok, (uint64_t)(int64_t)o32);                                                  \
        memset(&o8, 0xA5, sizeof o8);                                                                           \
        ok = cint_as_u8_from_##T(ctx, F_SITE, &a, &o8);                                                         \
        narrow(d + F_NARROW, ctx, ok, o8);                                                                      \
        bits[0] = (uint64_t)cint_as_wrap_i64_from_##T(&a);                                                      \
        bits[1] = cint_as_wrap_u64_from_##T(&a);                                                                \
        bits[2] = (uint64_t)(int64_t)cint_as_wrap_i32_from_##T(&a);                                             \
        bits[3] = (uint64_t)cint_as_sat_i64_from_##T(&a);                                                       \
        bits[4] = cint_as_sat_u64_from_##T(&a);                                                                 \
        bits[5] = (uint64_t)cint_as_sat_u8_from_##T(&a) | (uint64_t)(int64_t)cint_as_sat_i32_from_##T(&a) << 8; \
        outcome(d + F_NARROW, ctx, 0u, bits, 6u);                                                               \
        fill(r.limb, (N));                                                                                      \
        ok = cint_as_##T##_from_i64(ctx, F_SITE, (int64_t)x[0], &r);                                            \
        outcome(d + F_NARROW, ctx, ok, r.limb, (N));                                                            \
        fill(r.limb, (N));                                                                                      \
        ok = cint_as_##T##_from_u64(ctx, F_SITE, x[0], &r);                                                     \
        outcome(d + F_NARROW, ctx, ok, r.limb, (N));                                                            \
        fill(r.limb, (N));                                                                                      \
        ok = cint_as_##T##_from_i32(ctx, F_SITE, (int32_t)(uint32_t)x[0], &r);                                  \
        outcome(d + F_NARROW, ctx, ok, r.limb, (N));                                                            \
        cint_as_wrap_##T##_from_i64((int64_t)y[0], &r);                                                         \
        outcome(d + F_NARROW, ctx, 0u, r.limb, (N));                                                            \
        cint_as_sat_##T##_from_i64((int64_t)y[0], &r);                                                          \
        outcome(d + F_NARROW, ctx, 0u, r.limb, (N));                                                            \
        fill(w1.limb, 2u);                                                                                      \
        ok = cint_as_i128_from_##T(ctx, F_SITE, &a, &w1);                                                       \
        outcome(d + F_WIDE, ctx, ok, w1.limb, 2u);                                                              \
        fill(w2.limb, 2u);                                                                                      \
        ok = cint_as_u128_from_##T(ctx, F_SITE, &a, &w2);                                                       \
        outcome(d + F_WIDE, ctx, ok, w2.limb, 2u);                                                              \
        fill(w3.limb, 4u);                                                                                      \
        ok = cint_as_i256_from_##T(ctx, F_SITE, &a, &w3);                                                       \
        outcome(d + F_WIDE, ctx, ok, w3.limb, 4u);                                                              \
        fill(w4.limb, 4u);                                                                                      \
        ok = cint_as_u256_from_##T(ctx, F_SITE, &a, &w4);                                                       \
        outcome(d + F_WIDE, ctx, ok, w4.limb, 4u);                                                              \
        fill(w5.limb, 8u);                                                                                      \
        ok = cint_as_i512_from_##T(ctx, F_SITE, &a, &w5);                                                       \
        outcome(d + F_WIDE, ctx, ok, w5.limb, 8u);                                                              \
        cint_as_wrap_i128_from_##T(&a, &w1);                                                                    \
        outcome(d + F_WIDE, ctx, 0u, w1.limb, 2u);                                                              \
        cint_as_wrap_u256_from_##T(&a, &w4);                                                                    \
        outcome(d + F_WIDE, ctx, 0u, w4.limb, 4u);                                                              \
        cint_as_sat_i128_from_##T(&a, &w1);                                                                     \
        outcome(d + F_WIDE, ctx, 0u, w1.limb, 2u);                                                              \
        cint_as_sat_u128_from_##T(&a, &w2);                                                                     \
        outcome(d + F_WIDE, ctx, 0u, w2.limb, 2u);                                                              \
        cint_as_sat_i256_from_##T(&a, &w3);                                                                     \
        outcome(d + F_WIDE, ctx, 0u, w3.limb, 4u);                                                              \
        cint_as_sat_u256_from_##T(&a, &w4);                                                                     \
        outcome(d + F_WIDE, ctx, 0u, w4.limb, 4u);                                                              \
        memset(&m, 0xA5, sizeof m);                                                                             \
        ok = cint_muldiv_##T(ctx, F_SITE, rtag, mmode, &a, &b, &c, &m);                                         \
        outcome(d + F_MULDIV, ctx, ok, m.limb, 8u);                                                             \
        memset(&m2, 0xA5, sizeof m2);                                                                           \
        ok = cint_muldiv_##T(ctx, F_SITE, (TAG), mmode, &a, &b, &c, &m2);                                       \
        outcome(d + F_MULDIV, ctx, ok, m2.limb, 8u);                                                            \
        t = c;                                                                                                  \
        ok2 = cint_muldiv_##T(ctx, F_SITE, (TAG), mmode, &a, &b, &t, &t);                                       \
        alias(ctx, ok, ok2, m2.limb, t.limb, c.limb, (N), #T " muldiv into c");                                 \
        fill(full.limb, 2u * (N));                                                                              \
        cint_mul_full_##T(&a, &b, &full);                                                                       \
        outcome(d + F_FULL, ctx, 0u, full.limb, 2u * (N));                                                      \
        extra_##T(ctx, d, &a, pick);                                                                            \
    }
FAST_TYPE(i128, 2u, CINT_TAG_I128, i256)
FAST_TYPE(u128, 2u, CINT_TAG_U128, u256)
FAST_TYPE(i256, 4u, CINT_TAG_I256, i512)
FAST_TYPE(u256, 4u, CINT_TAG_U256, u512)

/* The products of I512, U512 and I1024 (width 1024): the helpers of cint_w_mul_ovf
 * (cint_mul_<t>, cint_mul_sat_<t>, cint_mul_result_<t>), whose operands both in
 * 128 bits take the 4-limb step above 4 limbs (rt/OPEN.md RT-OQ-66), and their
 * quotients: the helpers of cint_w_divide and cint_w_div_round (cint_div_<t>,
 * cint_rem_<t>, their result forms, the division kinds and div_round in a mode
 * drawn from the operands), which take the 4-limb step the same way (RT-OQ-69),
 * each on its own line. Operands are
 * every pair of the boundary values 0, 2^k - 1, 2^k and 2^k + 1 for k in 1, 2,
 * 63 to 65, 126 to 129, 254 to 257 and 510 to 513 below the width, and their
 * negations, then `pairs` seeded pairs, most of them within 129 bits. */
#define PRODUCT_BOUNDS 104
static const char *const product_names[3] = {"i512", "u512", "i1024"};
static uint64_t g_pbound[3][PRODUCT_BOUNDS][16];
static unsigned g_npbound[3];
static digest g_product[3], g_quotient[3];

static void make_product_bounds(void)
{
    static const unsigned ks[] = {1u, 2u, 63u, 64u, 65u, 126u, 127u, 128u, 129u, 254u, 255u, 256u, 257u,
                                  510u, 511u, 512u, 513u};
    unsigned t, i, j, c, half;
    for (t = 0u; t < 3u; t++) {
        unsigned n = t == 2u ? 16u : 8u;
        memset(g_pbound[t], 0, sizeof g_pbound[t]);
        c = 1u;
        for (i = 0u; i < sizeof ks / sizeof ks[0]; i++) {
            if (ks[i] >= 64u * n) continue;
            for (j = 0u; j < 3u; j++) {
                g_pbound[t][c][ks[i] / 64u] = (uint64_t)1 << (ks[i] % 64u);
                add_small(g_pbound[t][c], n, (int64_t)j - 1);
                c++;
            }
        }
        half = c;
        for (i = 1u; i < half; i++) {
            memcpy(g_pbound[t][c], g_pbound[t][i], sizeof g_pbound[t][c]);
            negate(g_pbound[t][c], n);
            c++;
        }
        g_npbound[t] = c;
    }
}

/* A pseudo-random operand of n limbs: in three draws of four, a value of at most
 * 129 bits of either sign, about the bound of the 4-limb step. */
static void draw_product(uint64_t *v, unsigned n, unsigned t)
{
    uint64_t pick = next();
    unsigned i, bits;
    memset(v, 0, 16u * sizeof *v);
    switch (pick & 3u) {
    case 0u:
        for (i = 0u; i < n; i++) v[i] = next();
        break;
    case 3u:
        memcpy(v, g_pbound[t][(pick >> 2) % g_npbound[t]], 16u * sizeof *v);
        add_small(v, n, (int64_t)((pick >> 20) % 7u) - 3);
        break;
    default:
        bits = (unsigned)((pick >> 2) % 130u);
        for (i = 0u; i < 3u && 64u * i < bits; i++) {
            v[i] = next();
            if (bits - 64u * i < 64u) v[i] &= ((uint64_t)1 << (bits - 64u * i)) - 1u;
        }
        if (((pick >> 12) & 1u) != 0u) negate(v, n);
        break;
    }
}

#define PRODUCT_TYPE(T, N)                                                                                   \
    static void product_##T(cint_ctx *ctx, digest *d, digest *q, const uint64_t *x, const uint64_t *y)     \
    {                                                                                                         \
        cint_##T a, b, r, t;                                                                                  \
        uint32_t op, mode = (uint32_t)((x[0] ^ y[1]) % 10u);                                                  \
        bool ok, ok2;                                                                                         \
        memcpy(a.limb, x, 8u * (N));                                                                          \
        memcpy(b.limb, y, 8u * (N));                                                                          \
        fill(r.limb, (N));                                                                                    \
        ok = cint_mul_##T(ctx, F_SITE, &a, &b, &r);                                                           \
        outcome(d, ctx, ok, r.limb, (N));                                                                     \
        t = a;                                                                                                \
        ok2 = cint_mul_##T(ctx, F_SITE, &t, &b, &t);                                                          \
        alias(ctx, ok, ok2, r.limb, t.limb, a.limb, (N), #T " mul, slot aliasing a");                       \
        t = b;                                                                                                \
        ok2 = cint_mul_##T(ctx, F_SITE, &a, &t, &t);                                                          \
        alias(ctx, ok, ok2, r.limb, t.limb, b.limb, (N), #T " mul, slot aliasing b");                       \
        fill(r.limb, (N));                                                                                    \
        cint_mul_sat_##T(&a, &b, &r);                                                                         \
        outcome(d, ctx, 0u, r.limb, (N));                                                                     \
        fill(r.limb, (N));                                                                                    \
        outcome(d, ctx, cint_mul_result_##T(&a, &b, &r), r.limb, (N));                                        \
        fill(r.limb, (N));                                                                                    \
        ok = cint_div_##T(ctx, F_SITE, &a, &b, &r);                                                           \
        outcome(q, ctx, ok, r.limb, (N));                                                                     \
        t = a;                                                                                                \
        ok2 = cint_div_##T(ctx, F_SITE, &t, &b, &t);                                                          \
        alias(ctx, ok, ok2, r.limb, t.limb, a.limb, (N), #T " div, slot aliasing a");                       \
        t = b;                                                                                                \
        ok2 = cint_div_##T(ctx, F_SITE, &a, &t, &t);                                                          \
        alias(ctx, ok, ok2, r.limb, t.limb, b.limb, (N), #T " div, slot aliasing b");                       \
        fill(r.limb, (N));                                                                                    \
        ok = cint_rem_##T(ctx, F_SITE, &a, &b, &r);                                                           \
        outcome(q, ctx, ok, r.limb, (N));                                                                     \
        t = a;                                                                                                \
        ok2 = cint_rem_##T(ctx, F_SITE, &t, &b, &t);                                                          \
        alias(ctx, ok, ok2, r.limb, t.limb, a.limb, (N), #T " rem, slot aliasing a");                       \
        fill(r.limb, (N));                                                                                    \
        outcome(q, ctx, cint_div_result_##T(&a, &b, &r), r.limb, (N));                                        \
        fill(r.limb, (N));                                                                                    \
        outcome(q, ctx, cint_rem_result_##T(&a, &b, &r), r.limb, (N));                                        \
        for (op = 6u; op <= 9u; op++) {                                                                       \
            fill(r.limb, (N));                                                                                \
            ok = cint_builtin_##T(ctx, F_SITE, op, 0u, &a, &b, NULL, &r);                                     \
            outcome(q, ctx, ok, r.limb, (N));                                                                 \
        }                                                                                                     \
        fill(r.limb, (N));                                                                                    \
        ok = cint_builtin_##T(ctx, F_SITE, 10u, mode, &a, &b, NULL, &r);                                      \
        outcome(q, ctx, ok, r.limb, (N));                                                                     \
        t = a;                                                                                                \
        ok2 = cint_builtin_##T(ctx, F_SITE, 10u, mode, &t, &b, NULL, &t);                                     \
        alias(ctx, ok, ok2, r.limb, t.limb, a.limb, (N), #T " div_round, slot aliasing a");                 \
        t = b;                                                                                                \
        ok2 = cint_builtin_##T(ctx, F_SITE, 10u, mode, &a, &t, NULL, &t);                                     \
        alias(ctx, ok, ok2, r.limb, t.limb, b.limb, (N), #T " div_round, slot aliasing b");                 \
    }

PRODUCT_TYPE(i512, 8u)
PRODUCT_TYPE(u512, 8u)
PRODUCT_TYPE(i1024, 16u)

static void run_product(cint_ctx *ctx, unsigned t, const uint64_t *x, const uint64_t *y)
{
    if (t == 0u) product_i512(ctx, &g_product[0], &g_quotient[0], x, y);
    else if (t == 1u) product_u512(ctx, &g_product[1], &g_quotient[1], x, y);
    else product_i1024(ctx, &g_product[2], &g_quotient[2], x, y);
}

static void run_products(cint_ctx *ctx, unsigned long pairs, digest *all)
{
    static const uint64_t seeds[3] = {0xA4093822299F31D0u, 0x082EFA98EC4E6C89u, 0x452821E638D01377u};
    unsigned long p;
    unsigned t, i, j;
    make_product_bounds();
    for (t = 0u; t < 3u; t++) {
        unsigned nb = g_npbound[t], n = t == 2u ? 16u : 8u;
        g_state = seeds[t];
        for (i = 0u; i < nb; i++) {
            for (j = 0u; j < nb; j++) run_product(ctx, t, g_pbound[t][i], g_pbound[t][j]);
        }
        for (p = 0u; p < pairs; p++) {
            uint64_t x[16], y[16];
            draw_product(x, n, t);
            draw_product(y, n, t);
            run_product(ctx, t, x, y);
        }
        printf("operands: %u boundary values and %lu pairs of %u bits\n", nb, pairs, 64u * n);
        printf("%s mul %llu %016llx\n", product_names[t], (unsigned long long)g_product[t].calls,
               (unsigned long long)g_product[t].h);
        printf("%s div %llu %016llx\n", product_names[t], (unsigned long long)g_quotient[t].calls,
               (unsigned long long)g_quotient[t].h);
        mix(all, g_product[t].h);
        mix(all, g_quotient[t].h);
        all->calls += g_product[t].calls + g_quotient[t].calls;
    }
}

static void run_all(cint_ctx *ctx, unsigned w, const uint64_t *x, const uint64_t *y, const uint64_t *z,
                    uint64_t pick)
{
    if (w == 0u) {
        run_i128(ctx, g_digest[0], x, y, z, pick);
        run_u128(ctx, g_digest[1], x, y, z, pick);
    } else {
        run_i256(ctx, g_digest[2], x, y, z, pick);
        run_u256(ctx, g_digest[3], x, y, z, pick);
    }
}

int main(int argc, char **argv)
{
    static const uint64_t seeds[2] = {0x243F6A8885A308D3u, 0x13198A2E03707344u};
    unsigned long pairs = argc > 1 ? strtoul(argv[1], NULL, 10) : 1048576ul, p, width;
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL;
    digest all = {0u, 0u};
    unsigned w, i, j, f;
    width = argc > 2 ? strtoul(argv[2], NULL, 10) : 0ul;
    if (argc > 3 || (width != 0ul && width != 128ul && width != 256ul && width != 1024ul)) {
        printf("usage: test_wide_fast [pairs [128|256|1024]]\n");
        return 2;
    }
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = &f_program;
    if (cint_ctx_create(&cfg, &ctx) != CINT_OK) {
        printf("FAIL cint_ctx_create\n");
        return 1;
    }
    printf("helper mode: %s\n", CINT_RT_HELPER_MODE);
    printf("paths: %s\n", CINT_W_FAST ? "fast" : "generic");
    make_bounds();
    for (w = 0u; w < 2u; w++) {
        unsigned nb = g_nbound[w], n = w == 0u ? 2u : 4u;
        if (width == (w == 0u ? 256ul : 128ul) || width == 1024ul) continue;
        g_state = seeds[w];
        for (i = 0u; i < nb; i++) {
            for (j = 0u; j < nb; j++) {
                run_all(ctx, w, g_bound[w][i], g_bound[w][j], g_bound[w][(i + j) % nb], (uint64_t)i * nb + j);
            }
        }
        for (p = 0u; p < pairs; p++) {
            uint64_t x[4], y[4], z[4];
            draw(x, n, w);
            draw(y, n, w);
            draw(z, n, w);
            run_all(ctx, w, x, y, z, next());
        }
        printf("operands: %u boundary values and %lu pairs of %u bits\n", nb, pairs, 64u * n);
        for (i = 2u * w; i < 2u * w + 2u; i++) {
            for (f = 0u; f < FAMILIES; f++) {
                const digest *d = &g_digest[i][f];
                printf("%s %s %llu %016llx\n", type_names[i], family_names[f], (unsigned long long)d->calls,
                       (unsigned long long)d->h);
                mix(&all, d->h);
                all.calls += d->calls;
            }
        }
    }
    if (width == 0ul || width == 1024ul) run_products(ctx, pairs, &all);
    printf("digest: %llu calls %016llx\n", (unsigned long long)all.calls, (unsigned long long)all.h);
    if (width == 0ul || width == 128ul) check_fixed(pairs / 256u + 64u);
    printf("checks: %ld passed, %ld failed\n", g_checks - g_failed, g_failed);
    cint_ctx_destroy(ctx);
    return g_failed == 0 ? 0 : 1;
}
