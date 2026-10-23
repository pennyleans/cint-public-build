/* cint_fixed.h: signed binary fixed point Q<i>.<f> with storage of 8 to 64
 * bits (roadmap box 15; SPEC-01 IM-54 to IM-69; box 15 note 5.3, BX15-15), and
 * of 128 bits through the out-of-line helpers of section 5 (OQ-292).
 *
 * A Q<i>.<f> value is its raw storage integer, held in the C type of I8 to
 * I64 for w = i + f. A type word names an operand type: CINT_QT(storage tag,
 * f) for Q<w - f>.<f>, and the tag itself for I8 to U64. Emitted code passes
 * f, the type words and the rounding mode (1 to 9, numbered as for
 * cint_rt_builtin) as constants (note 6.2), so each helper folds to the
 * arithmetic of one type. For S in i8 i16 i32 i64 with C type CT, the storage:
 *   checked     bool cint_q_add_<S>, cint_q_sub_<S>(ctx, site, f, CT a, CT b, CT *out)
 *               bool cint_q_neg_<S>, cint_q_abs_<S>(ctx, site, f, CT a, CT *out)
 *               bool cint_q_mul_<S>, cint_q_div_<S>(ctx, site, f, mode, CT a, CT b, CT *out)
 *               bool cint_q_sqrt_<S>(ctx, site, f, mode, CT a, CT *out)
 *               bool cint_q_mul_int_<S>(ctx, site, f, ntag, CT a, uint64_t n, CT *out):
 *                   a times the integer n of type ntag, exact (IM-64)
 *               bool cint_qmul_<S>(ctx, site, ta, tb, f, mode, int64_t a, int64_t b, CT *out):
 *                   qmul(Q<w - f>.<f>, a, b) for a and b of the types ta and tb
 *                   (IM-63), with mode 0 when fa + fb <= f
 *   wrapping    CT cint_q_mul_wrap_<S>(f, mode, CT a, CT b)
 *   saturating  CT cint_q_mul_sat_<S>(f, mode, CT a, CT b)
 *   result      uint16_t cint_q_mul_result_<S>, cint_q_div_result_<S>(f, mode, CT a, CT b, CT *out)
 *               uint16_t cint_q_sqrt_result_<S>(f, mode, CT a, CT *out):
 *                   0 with the checked form's value, or with *out unwritten the tag
 *                   mirroring its fault (IM-188): CINT_ARITH_OVERFLOW, CINT_ARITH_DIV_ZERO
 *                   (b == 0, first) or CINT_ARITH_DOMAIN (a < 0)
 * `+%`, `+|`, `-%`, `-|`, `min`, `max`, `add_result` and `sub_result` act on the
 * raw values exactly as on the storage integer (IM-60), so emitted code calls
 * cint_add_wrap_<S>, cint_add_result_<S> and the others of cint_rt.h for them. For R in i8 to u64 with C type CR, the target
 * or the target's storage, conversions with fixed point on one side (SPEC-01
 * 5.6, IM-68, IM-69), x the source's value as a 64-bit pattern (CINT_RT_BITS_S
 * or CINT_RT_BITS_U), mode 0 when the conversion is exact:
 *   checked     bool cint_q_as_<R>(ctx, site, from, to, mode, uint64_t x, CR *out)
 *   wrapping    CR cint_q_as_wrap_<R>(from, to, mode, uint64_t x)
 *   `as|`       CR cint_q_as_sat_<R>(from, to, mode, uint64_t x): the bound on the
 *               value's side when it does not fit; never faults
 *   `as?`       uint16_t cint_q_as_result_<R>(from, to, mode, uint64_t x, CR *out):
 *               0, or CINT_ARITH_NARROW with *out unwritten
 * and print with a format specification (rt/cint_fixed.c):
 *   print       bool cint_rt_print_format_fixed(ctx, qt, raw, form, fill, width, precision)
 * The reductions act on the raw values with the result in the element's format
 * (SPEC-02 R-4; rt/OPEN.md RT-OQ-57), and dot rounds once (SPEC-01 IM-87):
 *   sequential  bool cint_reduce_q_<S>(ctx, site, op, f, xs, ys, has_init, init, CT *out):
 *               cint_rt_reduce with elem and result Q<w - f>.<f>, init raw; not count or
 *               fold_checked(mul)
 *               bool cint_dot_q_<S>(ctx, site, elem, f, mode, xs, ys, CT *out):
 *               dot(Q<w - f>.<f>, a, b[, mode]) over elements of type elem (cint_rt_dot_fixed)
 *   kernel      bool cint_ksum_q_<S>(ctx, site, f, n, a, CT *out)
 *               bool cint_kfold_q_<S>(ctx, site, f, n, CT *acc, CT x)
 *               cint_ksum_<t> and cint_kfold_<t> of rt/cint_rt.h for Q<w - f>.<f>; sum_wrap,
 *               sum_sat, min and max use cint_kwrap_<S>, cint_add_sat_<S> and
 *               cint_rt_fault_reduce_empty with the fixed-point type word.
 *
 * Each product, scaled dividend and scaled radicand is formed exactly as a
 * sign and a 128-bit magnitude (the 2w-bit intermediate of IM-54; one 64-bit
 * word for storage of up to 32 bits), rounded once, then range-checked. A
 * failed check calls cint_rt_fixed_fault (rt/cint_fixed.c), which recomputes
 * the exact result as an exact integer and writes the record of IM-106: fixed-point
 * operands as tag 31 values (IM-146), `exact` the rounded raw value, and
 * `limit` the bound of the result type on exact's side. Division and the
 * square root use cint_rt_div_wide and cint_rt_isqrt64 of cint_rt.h, so no
 * helper needs a compiler support routine.
 */
#ifndef CINT_FIXED_H
#define CINT_FIXED_H

#include "cint_wide.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CINT_RT_LIBRARY) && !defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
#pragma GCC visibility push(hidden)
#endif

/* ------------------------------------------------------------------------- */
/* 1. Type words and the out-of-line record writer.                           */

#define CINT_TAG_Q 0x31
#define CINT_QT(STORAGE, F) (((uint32_t)(F) << 16) | ((uint32_t)CINT_TAG_Q << 8) | (uint32_t)(STORAGE))

/* Whether a type word names a fixed-point type. cint_rt_tag_width and
 * cint_rt_tag_signed read a fixed-point word as its storage tag. */
static inline bool cint_q_is(uint32_t t)
{
    return ((t >> 8) & 0xffu) == (uint32_t)CINT_TAG_Q;
}

static inline unsigned cint_q_frac(uint32_t t)
{
    return cint_q_is(t) ? (unsigned)(t >> 16) : 0u;
}

/* The operation numbers of cint_rt_fixed_fault. */
#define CINT_Q_ADD 1u
#define CINT_Q_SUB 2u
#define CINT_Q_MUL 3u
#define CINT_Q_NEG 4u
#define CINT_Q_DIV 5u
#define CINT_Q_ABS 6u
#define CINT_Q_SQRT 7u
#define CINT_Q_MUL_INT 8u
#define CINT_Q_QMUL 9u
#define CINT_Q_AS 10u

/* The operator forms. */
#define CINT_Q_CHECKED 0u
#define CINT_Q_WRAP 1u
#define CINT_Q_SAT 2u

/* Writes the canonical record of the checked operation `op` (mode a rounding
 * mode or 0) on a of type ta and b of type tb (0 for one operand) with a
 * result of type tr, a and b as 64-bit patterns, unless the context holds a
 * record already. Returns false. */
bool cint_rt_fixed_fault(cint_ctx *ctx, cint_site site, uint32_t op, uint32_t mode, uint32_t ta, uint32_t tb,
                         uint32_t tr, uint64_t a, uint64_t b);

/* cint_rt_print_format (rt/cint_rt.h section 6c) for a value of the fixed-point type qt, given as its raw
 * storage (SPEC-04 LS-207, LS-208; rt/OPEN.md RT-OQ-56): the form has no kind, `#`, group or
 * CINT_FORMAT_BITS, and with CINT_FORMAT_PRECISION, precision is the `.precision` (otherwise 0, for the
 * shortest exact decimal). A hole without a specification passes form 0, fill 32 and width 0; `!raw`
 * and `!bits` print the storage integer with its tag through cint_rt_print_value and
 * cint_rt_print_format. Another type word faults E_UNSUPPORTED dispatch.admit, as an excluded form does. */
bool cint_rt_print_format_fixed(cint_ctx *ctx, uint32_t qt, uint64_t raw, uint32_t form, uint32_t fill,
                                uint64_t width, uint64_t precision);

/* cint_rt_reduce's dot (rt/cint_rt.h) with a rounding mode: over elements of a
 * fixed-point type elem = Q<i>.<f> into result = Q<j>.<g>, the exact sum of the
 * products at scale 2^(2f), rounded once to 2^g under mode (1 to 9) when
 * 2f > g, shifted exactly otherwise, then range-checked (SPEC-01 IM-87,
 * BX15-04). Records name the types, then the mode when it rounds:
 * dot.checked.q8_8.q8_8.floor, limit the bound as a Q value. mode is 0 when
 * dot does not round, and for integer types, where it is cint_rt_reduce's dot;
 * any other mode faults E_UNSUPPORTED dispatch.admit. */
bool cint_rt_dot_fixed(cint_ctx *ctx, cint_site site, uint32_t elem, uint32_t result, uint32_t mode,
                       const cint_vdesc *xs, const cint_vdesc *ys, uint64_t *out);

/* ------------------------------------------------------------------------- */
/* 2. Exact results as a sign and a 128-bit magnitude.                        */

typedef struct cint_q_mag {
    uint64_t lo;
    uint64_t hi;
    bool negative;
} cint_q_mag;

/* The value of a 64-bit pattern, signed when s. */
CINT_RT_FOLD cint_q_mag cint_q_of(uint64_t bits, bool s)
{
    cint_q_mag m;
    m.negative = s && (bits >> 63) != 0u;
    m.lo = m.negative ? (uint64_t)0 - bits : bits;
    m.hi = 0u;
    return m;
}

/* 1 when a magnitude whose bits below the cut are not all zero (`half` the
 * bit just below the cut, `sticky` 1 when any bit under it is set) moves one
 * step away from zero under mode (SPEC-01 4.5; cint_rt_round_up's rule), and
 * 0 otherwise; odd is the parity of the truncated magnitude and negative its
 * sign. Every flag is 0 or 1 and they combine bitwise, so that with mode a
 * constant no branch remains on the bits cut off (rt/OPEN.md RT-OQ-62). */
CINT_RT_FOLD uint64_t cint_q_up(uint64_t half, uint64_t sticky, uint64_t odd, uint64_t negative, uint32_t mode)
{
    uint64_t tie;
    if (mode <= 4u)
        return (half | sticky) & (mode == 1u ? negative : mode == 2u ? negative ^ 1u : mode == 4u ? 1u : 0u);
    tie = mode == 5u ? odd : mode == 6u ? 1u : mode == 8u ? negative ^ 1u : mode == 9u ? negative : 0u;
    return half & (sticky | tie);
}

/* m / 2^k rounded once under mode, 0 <= k <= 127. */
CINT_RT_FOLD cint_q_mag cint_q_shr(cint_q_mag m, unsigned k, uint32_t mode)
{
    cint_q_mag q = m;
    uint64_t half, sticky, up;
    if (k == 0u) return m;
    if (k < 64u) {
        half = (m.lo >> (k - 1u)) & 1u;
        sticky = (m.lo & (((uint64_t)1 << (k - 1u)) - 1u)) != 0u ? 1u : 0u;
        q.lo = (m.lo >> k) | (m.hi << (64u - k));
        q.hi = m.hi >> k;
    } else if (k == 64u) {
        half = m.lo >> 63;
        sticky = (m.lo << 1) != 0u ? 1u : 0u;
        q.lo = m.hi;
        q.hi = 0u;
    } else {
        half = (m.hi >> (k - 65u)) & 1u;
        sticky = (m.lo | (m.hi & (((uint64_t)1 << (k - 65u)) - 1u))) != 0u ? 1u : 0u;
        q.lo = m.hi >> (k - 64u);
        q.hi = 0u;
    }
    up = cint_q_up(half, sticky, q.lo & 1u, m.negative ? 1u : 0u, mode);
    q.lo += up;
    q.hi += q.lo < up ? 1u : 0u;
    return q;
}

/* m * 2^k, 0 <= k <= 63. A product of 2^128 or more keeps the high word all
 * ones, a magnitude outside every storage. */
CINT_RT_FOLD cint_q_mag cint_q_shl(cint_q_mag m, unsigned k)
{
    if (k == 0u) return m;
    if ((m.hi >> (64u - k)) != 0u) {
        m.hi = UINT64_MAX;
        return m;
    }
    m.hi = (m.hi << k) | (m.lo >> (64u - k));
    m.lo <<= k;
    return m;
}

/* The product of two values. narrow: both magnitudes are below 2^32. */
CINT_RT_FOLD cint_q_mag cint_q_mul_mag(cint_q_mag a, cint_q_mag b, bool narrow)
{
    cint_q_mag m;
    m.negative = a.negative != b.negative;
    if (narrow) {
        m.lo = a.lo * b.lo;
        m.hi = 0u;
    } else {
        m.lo = cint_w_mul64(a.lo, b.lo, &m.hi);
    }
    return m;
}

/* Whether m lies in w bits, signed when s. When it does, or when form wraps
 * or saturates, *bits receives its pattern, the low w bits of its two's
 * complement, or the bound on its side; the caller narrows it to w bits. */
CINT_RT_FOLD bool cint_q_fit(cint_q_mag m, unsigned w, bool s, uint32_t form, uint64_t *bits)
{
    uint64_t lim = !s ? (m.negative ? 0u : cint_rt_mask(w)) : ((uint64_t)1 << (w - 1u)) - (m.negative ? 0u : 1u);
    if ((m.hi == 0u && m.lo <= lim) || form == CINT_Q_WRAP) {
        *bits = m.negative ? (uint64_t)0 - m.lo : m.lo;
        return true;
    }
    if (form == CINT_Q_SAT) {
        *bits = m.negative ? (uint64_t)0 - lim : lim;
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------------- */
/* 3. The operations of one storage width w, on sign-extended raw values.     */

/* mul: round(a * b / 2^f) (SPEC-01 5.3, IM-61, IM-62). */
CINT_RT_FOLD bool cint_q_mul_raw(int64_t a, int64_t b, unsigned w, unsigned f, uint32_t mode, uint32_t form,
                                 uint64_t *bits)
{
    cint_q_mag m = cint_q_mul_mag(cint_q_of((uint64_t)a, true), cint_q_of((uint64_t)b, true), w <= 32u);
    return cint_q_fit(cint_q_shr(m, f, mode), w, true, form, bits);
}

/* div: round(a * 2^f / b) for b != 0 (5.4, IM-65, IM-66). */
CINT_RT_FOLD bool cint_q_div_raw(int64_t a, int64_t b, unsigned w, unsigned f, uint32_t mode, uint64_t *bits)
{
    cint_q_mag n = cint_q_shl(cint_q_of((uint64_t)a, true), f), d = cint_q_of((uint64_t)b, true), q;
    uint64_t rem;
    q.negative = n.negative != d.negative;
    if (w <= 32u) {  /* |a| * 2^f < 2^62 */
        q.lo = n.lo / d.lo;
        q.hi = 0u;
        rem = n.lo % d.lo;
    } else {
        q.hi = n.hi / d.lo;
        q.lo = cint_rt_div_wide(n.hi % d.lo, n.lo, d.lo, &rem);
    }
    if (rem != 0u && cint_rt_round_up(q.lo, rem, q.negative, d.lo, mode) && ++q.lo == 0u) q.hi++;
    return cint_q_fit(q, w, true, CINT_Q_CHECKED, bits);
}

/* sqrt: the root of a * 2^f for a >= 0, rounded once (5.5, IM-67). Half modes
 * have no ties, since the root of a non-square is irrational: they round up
 * exactly when a * 2^f > r^2 + r. */
CINT_RT_FOLD bool cint_q_sqrt_raw(int64_t a, unsigned w, unsigned f, uint32_t mode, uint64_t *bits)
{
    cint_q_mag x = cint_q_shl(cint_q_of((uint64_t)a, true), f), r;
    uint64_t sq_hi, sq_lo;
    r.hi = 0u;
    r.negative = false;
    if (w <= 32u || x.hi == 0u) {
        r.lo = cint_rt_isqrt64(x.lo);
    } else {
        uint64_t root[2], radicand[2];
        radicand[0] = x.lo;
        radicand[1] = x.hi;
        cint_rt_wide_isqrt(root, radicand, 2u);
        r.lo = root[0];
    }
    sq_lo = cint_w_mul64(r.lo, r.lo, &sq_hi);
    if (sq_lo != x.lo || sq_hi != x.hi) {
        bool up = mode == 2u || mode == 4u;
        if (mode >= 5u) {
            uint64_t t_lo = sq_lo + r.lo, t_hi = sq_hi + (t_lo < sq_lo ? 1u : 0u);
            up = x.hi > t_hi || (x.hi == t_hi && x.lo > t_lo);
        }
        if (up) r.lo++;  /* r < 2^63 */
    }
    return cint_q_fit(r, w, true, CINT_Q_CHECKED, bits);
}

/* qmul: the product of a of type ta and b of type tb at scale 2^(fa + fb),
 * rounded to 2^f when fa + fb > f and exact otherwise (IM-63). */
CINT_RT_FOLD bool cint_q_qmul_raw(uint32_t ta, uint32_t tb, int64_t a, int64_t b, unsigned w, unsigned f,
                                  uint32_t mode, uint64_t *bits)
{
    unsigned fa = cint_q_frac(ta), fb = cint_q_frac(tb);
    cint_q_mag m = cint_q_mul_mag(cint_q_of((uint64_t)a, true), cint_q_of((uint64_t)b, true),
                                  cint_rt_tag_width(ta) <= 32u && cint_rt_tag_width(tb) <= 32u);
    m = fa + fb > f ? cint_q_shr(m, fa + fb - f, mode) : cint_q_shl(m, f - fa - fb);
    return cint_q_fit(m, w, true, CINT_Q_CHECKED, bits);
}

/* A conversion from type `from` to type `to`: the source's value times 2^(fd
 * - fs), rounded once under mode when fd < fs (5.6). */
CINT_RT_FOLD bool cint_q_convert(uint32_t from, uint32_t to, uint32_t mode, uint32_t form, uint64_t x,
                                 uint64_t *bits)
{
    unsigned fs = cint_q_frac(from), fd = cint_q_frac(to);
    cint_q_mag m = cint_q_of(x, cint_rt_tag_signed(from));
    m = fd >= fs ? cint_q_shl(m, fd - fs) : cint_q_shr(m, fs - fd, mode);
    return cint_q_fit(m, cint_rt_tag_width(to), cint_rt_tag_signed(to), form, bits);
}

/* ------------------------------------------------------------------------- */
/* 4. The helpers of each storage width.                                      */

/* The end of a checked helper: no record on a faulted context, the record
 * when the result does not fit, otherwise the result narrowed to w bits. */
#define CINT_Q_END(CT, OP, MODE, TA, TB, TR, A, B, W)                                                         \
    if (cint_rt_faulted(ctx)) return false;                                                                   \
    if (!fits) return cint_rt_fixed_fault(ctx, site, (OP), (MODE), (TA), (TB), (TR), (A), (B));               \
    *out = (CT)cint_rt_sext(bits, (W));                                                                       \
    return true;

/* add and sub act on the raw values (IM-60), neg and abs on 0 - a. */
#define CINT_Q_RAW(NAME, OP, S, CT, TAG, W, ARGS, EXPR, TB, B)                                                 \
    static inline bool cint_q_##NAME##_##S(cint_ctx *ctx, cint_site site, unsigned f, ARGS, CT *out)           \
    {                                                                                                         \
        CT r = a;                                                                                             \
        bool fits = !(EXPR);                                                                                  \
        uint64_t bits = (uint64_t)(int64_t)r;                                                                 \
        CINT_Q_END(CT, OP, 0u, CINT_QT(TAG, f), TB, CINT_QT(TAG, f), (uint64_t)(int64_t)a, B, W)                   \
    }

#define CINT_Q_STORAGE(S, CT, TAG, W)                                                                          \
    CINT_Q_RAW(add, CINT_Q_ADD, S, CT, TAG, W, CT a CINT_Q_COMMA CT b, CINT_RT_OVF(add, S)(a, b, &r),          \
               CINT_QT(TAG, f), (uint64_t)(int64_t)b)                                                         \
    CINT_Q_RAW(sub, CINT_Q_SUB, S, CT, TAG, W, CT a CINT_Q_COMMA CT b, CINT_RT_OVF(sub, S)(a, b, &r),          \
               CINT_QT(TAG, f), (uint64_t)(int64_t)b)                                                         \
    CINT_Q_RAW(neg, CINT_Q_NEG, S, CT, TAG, W, CT a, CINT_RT_OVF(sub, S)((CT)0, a, &r), 0u, 0u)                \
    CINT_Q_RAW(abs, CINT_Q_ABS, S, CT, TAG, W, CT a, a < 0 && CINT_RT_OVF(sub, S)((CT)0, a, &r), 0u, 0u)       \
    static inline bool cint_q_mul_##S(cint_ctx *ctx, cint_site site, unsigned f, uint32_t mode, CT a, CT b,    \
                                      CT *out)                                                                \
    {                                                                                                         \
        uint64_t bits = 0u;                                                                                   \
        bool fits = cint_q_mul_raw(a, b, (W), f, mode, CINT_Q_CHECKED, &bits);                                \
        CINT_Q_END(CT, CINT_Q_MUL, mode, CINT_QT(TAG, f), CINT_QT(TAG, f), CINT_QT(TAG, f), (uint64_t)(int64_t)a,  \
                   (uint64_t)(int64_t)b, W)                                                                   \
    }                                                                                                         \
    static inline CT cint_q_mul_wrap_##S(unsigned f, uint32_t mode, CT a, CT b)                               \
    {                                                                                                         \
        uint64_t bits = 0u;                                                                                   \
        (void)cint_q_mul_raw(a, b, (W), f, mode, CINT_Q_WRAP, &bits);                                         \
        return (CT)cint_rt_sext(bits, (W));                                                                   \
    }                                                                                                         \
    static inline CT cint_q_mul_sat_##S(unsigned f, uint32_t mode, CT a, CT b)                                \
    {                                                                                                         \
        uint64_t bits = 0u;                                                                                   \
        (void)cint_q_mul_raw(a, b, (W), f, mode, CINT_Q_SAT, &bits);                                          \
        return (CT)cint_rt_sext(bits, (W));                                                                   \
    }                                                                                                         \
    static inline bool cint_q_div_##S(cint_ctx *ctx, cint_site site, unsigned f, uint32_t mode, CT a, CT b,    \
                                      CT *out)                                                                \
    {                                                                                                         \
        uint64_t bits = 0u;                                                                                   \
        bool fits = b != 0 && cint_q_div_raw(a, b, (W), f, mode, &bits);                                      \
        CINT_Q_END(CT, CINT_Q_DIV, mode, CINT_QT(TAG, f), CINT_QT(TAG, f), CINT_QT(TAG, f), (uint64_t)(int64_t)a,  \
                   (uint64_t)(int64_t)b, W)                                                                   \
    }                                                                                                         \
    static inline bool cint_q_sqrt_##S(cint_ctx *ctx, cint_site site, unsigned f, uint32_t mode, CT a, CT *out) \
    {                                                                                                         \
        uint64_t bits = 0u;                                                                                   \
        bool fits = a >= 0 && cint_q_sqrt_raw(a, (W), f, mode, &bits);                                        \
        CINT_Q_END(CT, CINT_Q_SQRT, mode, CINT_QT(TAG, f), 0u, CINT_QT(TAG, f), (uint64_t)(int64_t)a, 0u, W)       \
    }                                                                                                         \
    static inline uint16_t cint_q_mul_result_##S(unsigned f, uint32_t mode, CT a, CT b, CT *out)              \
    {                                                                                                         \
        uint64_t bits = 0u;                                                                                   \
        if (!cint_q_mul_raw(a, b, (W), f, mode, CINT_Q_CHECKED, &bits)) return CINT_ARITH_OVERFLOW;           \
        *out = (CT)cint_rt_sext(bits, (W));                                                                   \
        return 0u;                                                                                            \
    }                                                                                                         \
    static inline uint16_t cint_q_div_result_##S(unsigned f, uint32_t mode, CT a, CT b, CT *out)              \
    {                                                                                                         \
        uint64_t bits = 0u;                                                                                   \
        if (b == 0) return CINT_ARITH_DIV_ZERO;                                                               \
        if (!cint_q_div_raw(a, b, (W), f, mode, &bits)) return CINT_ARITH_OVERFLOW;                           \
        *out = (CT)cint_rt_sext(bits, (W));                                                                   \
        return 0u;                                                                                            \
    }                                                                                                         \
    static inline uint16_t cint_q_sqrt_result_##S(unsigned f, uint32_t mode, CT a, CT *out)                   \
    {                                                                                                         \
        uint64_t bits = 0u;                                                                                   \
        if (a < 0) return CINT_ARITH_DOMAIN;                                                                  \
        if (!cint_q_sqrt_raw(a, (W), f, mode, &bits)) return CINT_ARITH_OVERFLOW;                             \
        *out = (CT)cint_rt_sext(bits, (W));                                                                   \
        return 0u;                                                                                            \
    }                                                                                                         \
    static inline bool cint_q_mul_int_##S(cint_ctx *ctx, cint_site site, unsigned f, uint32_t ntag, CT a,      \
                                          uint64_t n, CT *out)                                                \
    {                                                                                                         \
        uint64_t bits = 0u;                                                                                   \
        cint_q_mag m = cint_q_mul_mag(cint_q_of((uint64_t)(int64_t)a, true),                                  \
                                      cint_q_of(n, cint_rt_tag_signed(ntag)), false);                         \
        bool fits = cint_q_fit(m, (W), true, CINT_Q_CHECKED, &bits);                                          \
        CINT_Q_END(CT, CINT_Q_MUL_INT, 0u, CINT_QT(TAG, f), ntag, CINT_QT(TAG, f), (uint64_t)(int64_t)a, n, W)     \
    }                                                                                                         \
    static inline bool cint_qmul_##S(cint_ctx *ctx, cint_site site, uint32_t ta, uint32_t tb, unsigned f,      \
                                     uint32_t mode, int64_t a, int64_t b, CT *out)                            \
    {                                                                                                         \
        uint64_t bits = 0u;                                                                                   \
        bool fits = cint_q_qmul_raw(ta, tb, a, b, (W), f, mode, &bits);                                       \
        CINT_Q_END(CT, CINT_Q_QMUL, mode, ta, tb, CINT_QT(TAG, f), (uint64_t)a, (uint64_t)b, W)                   \
    }
#define CINT_Q_COMMA ,
CINT_Q_STORAGE(i8, int8_t, CINT_TAG_I8, 8u)
CINT_Q_STORAGE(i16, int16_t, CINT_TAG_I16, 16u)
CINT_Q_STORAGE(i32, int32_t, CINT_TAG_I32, 32u)
CINT_Q_STORAGE(i64, int64_t, CINT_TAG_I64, 64u)

#define CINT_Q_CONV(R, CR, K, W)                                                                               \
    static inline bool cint_q_as_##R(cint_ctx *ctx, cint_site site, uint32_t from, uint32_t to, uint32_t mode, \
                                     uint64_t x, CR *out)                                                     \
    {                                                                                                         \
        uint64_t bits = 0u;                                                                                   \
        bool fits = cint_q_convert(from, to, mode, CINT_Q_CHECKED, x, &bits);                                 \
        if (cint_rt_faulted(ctx)) return false;                                                               \
        if (!fits) return cint_rt_fixed_fault(ctx, site, CINT_Q_AS, mode, from, 0u, to, x, 0u);               \
        *out = (CR)CINT_RT_NARROW_##K(bits, (W));                                                             \
        return true;                                                                                          \
    }                                                                                                         \
    static inline CR cint_q_as_wrap_##R(uint32_t from, uint32_t to, uint32_t mode, uint64_t x)                \
    {                                                                                                         \
        uint64_t bits = 0u;                                                                                   \
        (void)cint_q_convert(from, to, mode, CINT_Q_WRAP, x, &bits);                                          \
        return (CR)CINT_RT_NARROW_##K(bits, (W));                                                             \
    }                                                                                                         \
    static inline CR cint_q_as_sat_##R(uint32_t from, uint32_t to, uint32_t mode, uint64_t x)                 \
    {                                                                                                         \
        uint64_t bits = 0u;                                                                                   \
        (void)cint_q_convert(from, to, mode, CINT_Q_SAT, x, &bits);                                           \
        return (CR)CINT_RT_NARROW_##K(bits, (W));                                                             \
    }                                                                                                         \
    static inline uint16_t cint_q_as_result_##R(uint32_t from, uint32_t to, uint32_t mode, uint64_t x, CR *out) \
    {                                                                                                         \
        uint64_t bits = 0u;                                                                                   \
        if (!cint_q_convert(from, to, mode, CINT_Q_CHECKED, x, &bits)) return CINT_ARITH_NARROW;              \
        *out = (CR)CINT_RT_NARROW_##K(bits, (W));                                                             \
        return 0u;                                                                                            \
    }
CINT_Q_CONV(i8, int8_t, S, 8u)
CINT_Q_CONV(i16, int16_t, S, 16u)
CINT_Q_CONV(i32, int32_t, S, 32u)
CINT_Q_CONV(i64, int64_t, S, 64u)
CINT_Q_CONV(u8, uint8_t, U, 8u)
CINT_Q_CONV(u16, uint16_t, U, 16u)
CINT_Q_CONV(u32, uint32_t, U, 32u)
CINT_Q_CONV(u64, uint64_t, U, 64u)

/* The end of a reduction: the raw result narrowed to w bits. */
#define CINT_Q_RED_END(CT, CALL, W)                                                                           \
    uint64_t bits = 0u;                                                                                       \
    if (!(CALL)) return false;                                                                                \
    *out = (CT)cint_rt_sext(bits, (W));                                                                       \
    return true;

#define CINT_Q_REDUCE(S, CT, TAG, W)                                                                          \
    static inline bool cint_reduce_q_##S(cint_ctx *ctx, cint_site site, uint32_t op, unsigned f,              \
                                         const cint_vdesc *xs, const cint_vdesc *ys, bool has_init,           \
                                         uint64_t init, CT *out)                                              \
    {                                                                                                         \
        CINT_Q_RED_END(CT, cint_rt_reduce(ctx, site, op, CINT_QT(TAG, f), CINT_QT(TAG, f), xs, ys,            \
                                          has_init, init, &bits), W)                                          \
    }                                                                                                         \
    static inline bool cint_dot_q_##S(cint_ctx *ctx, cint_site site, uint32_t elem, unsigned f,               \
                                      uint32_t mode, const cint_vdesc *xs, const cint_vdesc *ys, CT *out)     \
    {                                                                                                         \
        CINT_Q_RED_END(CT, cint_rt_dot_fixed(ctx, site, elem, CINT_QT(TAG, f), mode, xs, ys, &bits), W)       \
    }                                                                                                         \
    static inline bool cint_ksum_q_##S(cint_ctx *ctx, cint_site site, unsigned f, int64_t n,                  \
                                       const cint_zacc *a, CT *out)                                           \
    {                                                                                                         \
        CINT_Q_RED_END(CT, cint_rt_zacc_fit(ctx, site, "sum", CINT_QT(TAG, f), CINT_QT(TAG, f), n, a,         \
                                            &bits), W)                                                        \
    }                                                                                                         \
    static inline bool cint_kfold_q_##S(cint_ctx *ctx, cint_site site, unsigned f, int64_t n, CT *acc, CT x)  \
    {                                                                                                         \
        uint64_t bits = (uint64_t)(int64_t)*acc;                                                              \
        if (!cint_rt_fold_step(ctx, site, CINT_RT_OP_ADD, CINT_QT(TAG, f), n, &bits, (uint64_t)(int64_t)x))   \
            return false;                                                                                     \
        *acc = (CT)cint_rt_sext(bits, (W));                                                                   \
        return true;                                                                                          \
    }
CINT_Q_REDUCE(i8, int8_t, CINT_TAG_I8, 8u)
CINT_Q_REDUCE(i16, int16_t, CINT_TAG_I16, 16u)
CINT_Q_REDUCE(i32, int32_t, CINT_TAG_I32, 32u)
CINT_Q_REDUCE(i64, int64_t, CINT_TAG_I64, 64u)

/* ------------------------------------------------------------------------- */
/* 5. Operations with a side held in limbs (games wave G2b; RT-OQ-63).        */

/* Fixed point of storage I128 (OQ-292) is a cint_i128. An operation on it,
 * or on fixed point and a wide integer, forms the exact result in limbs, rounds
 * it once by mode and applies form (CINT_Q_CHECKED, _WRAP or _SAT); op and mode
 * are as for cint_rt_fixed_fault. a, b (NULL for one operand) and out point to
 * C objects, a narrow operand's a 64-bit pattern. False after a fault. */
bool cint_rt_q_limbs(cint_ctx *ctx, cint_site site, uint32_t op, uint32_t form, uint32_t mode, uint32_t ta, uint32_t tb,
                     uint32_t tr, const void *a, const void *b, void *out);

/* cint_rt_print_format_fixed for a value of 128-bit storage at raw. */
bool cint_rt_print_q_limbs(cint_ctx *ctx, uint32_t qt, const void *raw, uint32_t form, uint32_t fill, uint64_t width,
                           uint64_t precision);

/* cint_rt_reduce_wide for elem or result fixed point of 128-bit storage, with
 * dot's mode as for cint_rt_dot_fixed; init points to the element's C object. */
bool cint_rt_reduce_q_limbs(cint_ctx *ctx, cint_site site, uint32_t op, uint32_t elem, uint32_t result, uint32_t mode,
                            const cint_vdesc *xs, const cint_vdesc *ys, const void *init, void *out);
/* cint_ksum_q_<S> for 128-bit storage, the total in a cint_wzacc of the storage's elements. */
bool cint_rt_wzacc_fit_q(cint_ctx *ctx, cint_site site, uint32_t qt, int64_t n, const cint_wzacc *a, void *out);

#if defined(CINT_RT_LIBRARY) && !defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
#pragma GCC visibility pop
#endif

#ifdef __cplusplus
}
#endif

#endif
