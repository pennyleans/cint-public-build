/* cint_wide.h: the wide integers I128 to I1024 and U128 to U512 of cint-rt-3
 * (roadmap box 15, unit 3; SPEC-01 IM-6, IM-11 to IM-17; SPEC-03 A-13,
 * A-13a; SPEC-09 EMIT-12, EMIT-29).
 *
 * A wide value is a struct of w / 64 uint64_t limbs, limb 0 least
 * significant, in two's complement for I128 to I1024 and in binary for U128
 * to U512, with one representation per value (IM-11 to IM-14), aligned to 8
 * (A-13a). Generated code includes this header only in modules that use a
 * wide type; the runtime library compiles rt/cint_wide.c, which rt/cint_rt.c
 * includes.
 *
 * Every helper takes a wide input as `const T *` and writes a `T *` result
 * slot, which may alias an input (EMIT-29): it computes into a local and
 * copies once. Narrow operands and results pass by value, as in cint_rt.h. For
 * t in i128 i256 i512 i1024 u128 u256 u512, with T the struct cint_<t>:
 *   checked     bool cint_<op>_<t>(ctx, site, const T *a, const T *b, T *out)
 *               for add, sub, mul, div, rem (floor, SPEC-01 4.4);
 *               bool cint_neg_<t>(ctx, site, const T *a, T *out);
 *               bool cint_shl_<t>, cint_shl_wrap_<t>, cint_shr_<t>(ctx, site,
 *               const T *a, cint_count k, T *out)
 *   counts      cint_count cint_count_<t>(const T *k): a shift count of a
 *               wide type for any shift or rotation, narrow or wide (RT-OQ-52);
 *               it points at *k, which must outlive the call that takes it
 *   wrapping    void cint_<op>_wrap_<t>(const T *a, const T *b, T *out) for
 *               add, sub, mul; void cint_neg_wrap_<t>(const T *a, T *out)
 *   saturating  void cint_<op>_sat_<t>(const T *a, const T *b, T *out) for
 *               add, sub, mul
 *   results     uint16_t cint_<op>_result_<t>(const T *a, const T *b, T *out)
 *               for add, sub, mul, div, rem, and cint_shl_result_<t>(const T
 *               *a, cint_count k, T *out): the ArithError tag of cint_rt.h 10a
 *   bitwise     void cint_and_<t>, cint_or_<t>, cint_xor_<t>(a, b, out),
 *               void cint_not_<t>(a, out)
 *   comparison  bool cint_eq_<t>, cint_ne_<t>, cint_lt_<t>, cint_le_<t>,
 *               cint_gt_<t>, cint_ge_<t>(const T *a, const T *b)
 *   built-ins   bool cint_builtin_<t>(ctx, site, op, mode, a, b, c, T *out),
 *               op and mode numbered as for cint_rt_builtin: 1 abs, 5 clamp,
 *               6 div_trunc, 7 rem_trunc, 8 div_euclid, 9 rem_euclid,
 *               10 div_round, 13 isqrt, 14 isqrt_round (unused operands may be
 *               NULL); bool cint_muldiv_<t>(ctx, site, rtag, mode, a, b, c,
 *               void *out), whose result of type rtag (I8 to U64, I128 to
 *               I1024 or U128 to U512) is written to the C object of that
 *               type at out;
 *               void cint_min_<t>, cint_max_<t>(a, b, out);
 *               void cint_mul_full_<t>(a, b, out) for i64, i128, i256, i512,
 *               u64, u128 and u256 (to twice the width), and
 *               void cint_uabs_<t>(a, out) for i128, i256 and i512 (to the
 *               unsigned type of the width), which never fault; and
 *               bool cint_rotl_<t>, cint_rotr_<t>(ctx, site, const T *a,
 *               cint_count k, T *out) for u128, u256 and u512
 *   conversions bool cint_as_<to>_from_<from>(ctx, site, x, out),
 *               cint_as_wrap_<to>_from_<from>(x[, out]) and
 *               cint_as_sat_<to>_from_<from>(x[, out]) for every pair with
 *               a wide side, among I8 to U64, I128 to I1024 and U128 to
 *               U512; a narrow source passes by value, a narrow wrapping or
 *               saturating result is returned
 *   bytes       void cint_wide_store(uint8_t *dst, const uint64_t *limb,
 *               unsigned n) and cint_wide_load(uint64_t *limb, const uint8_t
 *               *src, unsigned n): the canonical little-endian bytes (IM-14)
 *   print and   bool cint_rt_print_wide(ctx, tag, limb) and
 *   assert      cint_rt_print_format_wide(ctx, tag, limb, form, fill, width,
 *               scale); bool cint_rt_fault_assert_wide(ctx, site, tag, a, b)
 *   reductions  bool cint_rt_reduce_wide(ctx, site, op, elem, result, xs,
 *               ys, init, out): sum, sum_wrap, sum_sat, fold_checked, min,
 *               max and dot over wide elements, and sum and dot of narrow
 *               ones into a wide result, with their steps for kernels
 *               (section 2)
 *
 * A failed check calls cint_rt_wide_fault, which recomputes the exact result
 * of SPEC-01 4.1 from the operands and writes the canonical record with wide
 * typed operands; every helper returns false after it, as in cint_rt.h. The
 * limb product uses 32-bit halves in the portable helper mode and the
 * compiler's 128-bit product (GCC and Clang) or _umul128 and __umulh (MSVC) in
 * the builtin one (EMIT-12). Division takes cint_rt_div_wide_b's 128-by-64
 * quotient where it divides in hardware, which under GCC and Clang calls
 * __udivti3 (EMIT-31, OQ-271), and cint_rt_div_wide's two-digit step
 * elsewhere. I128, U128, I256 and U256 take the straight-line steps of
 * section 3a (rt/OPEN.md RT-OQ-62).
 */
#ifndef CINT_WIDE_H
#define CINT_WIDE_H

#include "cint_rt.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CINT_RT_LIBRARY) && !defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
#pragma GCC visibility push(hidden)
#endif

/* ------------------------------------------------------------------------- */
/* 1. Types (SPEC-01 IM-146 tags 15 to 18 and 25 to 27; SPEC-03 A-13a).      */

#define CINT_TAG_I128 0x15
#define CINT_TAG_I256 0x16
#define CINT_TAG_I512 0x17
#define CINT_TAG_I1024 0x18
#define CINT_TAG_U128 0x25
#define CINT_TAG_U256 0x26
#define CINT_TAG_U512 0x27

typedef struct cint_i128 {
    uint64_t limb[2];
} cint_i128;
typedef struct cint_i256 {
    uint64_t limb[4];
} cint_i256;
typedef struct cint_i512 {
    uint64_t limb[8];
} cint_i512;
typedef struct cint_i1024 {
    uint64_t limb[16];
} cint_i1024;
_Static_assert(sizeof(cint_i128) == 16 && _Alignof(cint_i128) == 8, "cint_i128");
_Static_assert(sizeof(cint_i256) == 32 && _Alignof(cint_i256) == 8, "cint_i256");
_Static_assert(sizeof(cint_i512) == 64 && _Alignof(cint_i512) == 8, "cint_i512");
_Static_assert(sizeof(cint_i1024) == 128 && _Alignof(cint_i1024) == 8, "cint_i1024");
typedef struct cint_u128 {
    uint64_t limb[2];
} cint_u128;
typedef struct cint_u256 {
    uint64_t limb[4];
} cint_u256;
typedef struct cint_u512 {
    uint64_t limb[8];
} cint_u512;
_Static_assert(sizeof(cint_u128) == 16 && _Alignof(cint_u128) == 8, "cint_u128");
_Static_assert(sizeof(cint_u256) == 32 && _Alignof(cint_u256) == 8, "cint_u256");
_Static_assert(sizeof(cint_u512) == 64 && _Alignof(cint_u512) == 8, "cint_u512");

/* The limb count of a wide tag, or 0 for any other tag. */
static inline unsigned cint_wide_limbs(uint32_t tag)
{
    return (tag >= CINT_TAG_I128 && tag <= CINT_TAG_I1024) || (tag >= CINT_TAG_U128 && tag <= CINT_TAG_U512)
               ? cint_rt_tag_width(tag) / 64u
               : 0u;
}

/* ------------------------------------------------------------------------- */
/* 2. Out-of-line functions (rt/cint_wide.c).                                 */

/* The operation numbers of cint_rt_wide_fault: CINT_RT_OP_ADD to
 * CINT_RT_OP_SHR of cint_rt.h, CINT_W_AS for a conversion (tag the source,
 * which may be narrow, aux the target), and CINT_W_BUILTIN plus a
 * cint_rt_builtin number, CINT_W_ROTL and CINT_W_ROTR among them. */
#define CINT_W_AS 16u
#define CINT_W_BUILTIN 32u
#define CINT_W_ROTL (CINT_W_BUILTIN + 15u)
#define CINT_W_ROTR (CINT_W_BUILTIN + 16u)

/* Writes the canonical record of the operation `op` on the wide type `tag`
 * (mode a rounding mode or 0; aux the count's tag for a shift or rotation,
 * whose `b` then points to the count's 64-bit pattern, the target's tag for a
 * conversion and for muldiv, otherwise 0), with operands a, b, c of w / 64
 * limbs, or a's one 64-bit pattern for a narrow source, unless the context
 * holds a record already. Returns false. */
bool cint_rt_wide_fault(cint_ctx *ctx, cint_site site, uint32_t op, uint32_t tag, uint32_t mode, uint32_t aux,
                        const uint64_t *a, const uint64_t *b, const uint64_t *c);

/* Divides the m-limb magnitude u by the n-limb magnitude v != 0 (Knuth's
 * algorithm D on 64-bit digits): the quotient's m limbs in q, the remainder's n
 * limbs in r. m and n are at most 32; q and r may not overlap u or v. */
void cint_rt_wide_udivmod(uint64_t *q, uint64_t *r, const uint64_t *u, unsigned m, const uint64_t *v, unsigned n);

/* The same over significant limbs only: v[n - 1] != 0 and m >= n >= 1; the
 * quotient's m - n + 1 limbs in q, the remainder's n limbs in r. */
void cint_rt_wide_udivrem(uint64_t *q, uint64_t *r, const uint64_t *u, unsigned m, const uint64_t *v, unsigned n);

/* cint_w_mul_ovf and cint_w_divide (section 3) at 4 limbs for operands that
 * do not both lie in 128 bits (rt/OPEN.md RT-OQ-62). */
bool cint_rt_wide_mul4(uint64_t *r, const uint64_t *a, const uint64_t *b, bool s);
void cint_rt_wide_divide4(uint64_t *q, uint64_t *r, const uint64_t *a, const uint64_t *b, unsigned kind, bool s);

/* The largest root with root * root <= x, for the n-limb magnitude x (n <= 16). */
void cint_rt_wide_isqrt(uint64_t *root, const uint64_t *x, unsigned n);

/* muldiv on the wide operand type `tag` with a result of type rtag, written
 * to the C object at out (cint_muldiv_<t>). */
bool cint_rt_wide_muldiv(cint_ctx *ctx, cint_site site, uint32_t tag, uint32_t rtag, uint32_t mode,
                         const uint64_t *a, const uint64_t *b, const uint64_t *c, void *out);

/* Appends the decimal of a wide value to the open print statement. */
bool cint_rt_print_wide(cint_ctx *ctx, uint32_t tag, const uint64_t *limb);

/* cint_rt_print_format (rt/cint_rt.h section 6c) for a value of the wide type
 * tag, given as its limbs: the same form, fill, width and scale, the same
 * refusals of LS-213, the same host.resource fault (rt/OPEN.md RT-OQ-55). */
bool cint_rt_print_format_wide(cint_ctx *ctx, uint32_t tag, const uint64_t *limb, uint32_t form, uint32_t fill,
                               uint64_t width, uint64_t scale);

/* cint_rt_fault_assert for a failed comparison of the wide type tag: E_ASSERT
 * assert.checked.bool with operands a and b, given as limbs (RT-OQ-55). */
bool cint_rt_fault_assert_wide(cint_ctx *ctx, cint_site site, uint32_t tag, const uint64_t *a, const uint64_t *b);

/* Reductions with a wide side (rt/OPEN.md RT-OQ-54; SPEC-01 IM-77 to IM-87;
 * SPEC-02 R-5; box 15 rulings BX15-06 and BX15-07 C). An element or init is
 * passed as its limbs, or as the 64-bit pattern of I8 to U64; a result is
 * written to the C object of its type at out, as cint_rt_wide_muldiv writes
 * one, and only on success.
 *
 * cint_wzacc is the exact total of sum and dot, in two's complement over its
 * first L limbs: L = m + 1 for sum and 2m + 1 for dot, m the element's limb
 * count (1 for I8 to U64). A sum of I1024 elements takes the 17 limbs (1,088
 * bits) of R-5, and their dot the 33 (2,112 bits) of fault record version 3.
 * It starts zeroed and takes one element type and one of sum and dot. */
typedef struct cint_wzacc {
    uint64_t limb[33];
} cint_wzacc;
/* Adds x, or the exact product of x and y, both of type `tag`. */
void cint_wzacc_add(cint_wzacc *a, uint32_t tag, const uint64_t *x);
void cint_wzacc_add_product(cint_wzacc *a, uint32_t tag, const uint64_t *x, const uint64_t *y);
/* cint_rt_zacc_fit's final check (`name` sum or dot) with elem or result
 * wide: the total written when it fits result, else E_OVERFLOW
 * <name>.checked.<elem>.<result>, operand I64 n, exact the total (of
 * version 3 for dot over I1024), limit the bound of result crossed. */
bool cint_rt_wzacc_fit(cint_ctx *ctx, cint_site site, const char *name, uint32_t elem, uint32_t result, int64_t n,
                       const cint_wzacc *a, void *out);
/* cint_rt_fold_step on a wide type: *acc (m limbs) + x or *acc * x, with
 * operands j, <T> *acc and <T> x and exact the full result when it faults. */
bool cint_rt_wide_fold_step(cint_ctx *ctx, cint_site site, uint32_t op, uint32_t tag, int64_t j, uint64_t *acc,
                            const uint64_t *x);
/* cint_rt_reduce (rt/cint_rt.h) where elem or result is wide: every op but
 * count over wide elements, with result elem except for sum and dot, and sum
 * and dot of I8 to U64 elements into a wide result. init is NULL for none, and
 * fold_checked needs one. The order is cint_rt_reduce's: dot's shape check,
 * ceil(N / 64) fuel units, then the elements in logical order. */
bool cint_rt_reduce_wide(cint_ctx *ctx, cint_site site, uint32_t op, uint32_t elem, uint32_t result,
                         const cint_vdesc *xs, const cint_vdesc *ys, const uint64_t *init, void *out);

/* The width report (games G3; rt/OPEN.md RT-OQ-64). C compiled with
 * CINT_WIDTH_REPORT, and with cint_program_run_args defined as
 * cint_program_run_width_args (RT-OQ-74; older entries: cint_program_run_width*), records
 * at each checked helper below that takes a site its hits (hit 1, operands a,
 * b, c) and the largest width, in bits, of its operands and its result (hit 0,
 * a alone), each of the wide type tag (values of another tag are skipped); the
 * report goes to stderr when the program ends (`cint run --width-report`).
 * Otherwise the hooks are empty. */
void cint_rt_width_note(cint_ctx *ctx, cint_site site, uint32_t tag, uint32_t hit, const uint64_t *a,
                        const uint64_t *b, const uint64_t *c);
#if defined(CINT_WIDTH_REPORT)
#define CINT_W_IN(t, a, b, c) cint_rt_width_note(ctx, site, (t), 1u, (a), (b), (c))
#define CINT_W_OUT(t, r) cint_rt_width_note(ctx, site, (t), 0u, (r), NULL, NULL)
#else
#define CINT_W_IN(t, a, b, c) ((void)0)
#define CINT_W_OUT(t, r) ((void)0)
#endif

/* ------------------------------------------------------------------------- */
/* 3. Limb arithmetic. n is a constant at every call site of section 4.       */

#if CINT_RT_USE_BUILTINS && defined(CINT_RT_BUILTINS_GNU)
__extension__ typedef unsigned __int128 cint_w_u128;
__extension__ typedef __int128 cint_w_s128;
#endif

/* Whether cint_rt_div_wide_b divides 128 bits by 64 in hardware: _udiv128 on
 * MSVC x64, and on x86-64 outside macOS the 128-bit quotient of GCC and Clang,
 * which calls __udivti3 (SPEC-09 EMIT-31, OQ-271; rt/OPEN.md RT-OQ-62). */
#if CINT_RT_USE_BUILTINS && !defined(CINT_W_GENERIC) &&                                                  \
    ((defined(CINT_RT_BUILTINS_MSVC) && defined(_M_X64)) ||                                              \
     (defined(CINT_RT_BUILTINS_GNU) && defined(__x86_64__) && !defined(__APPLE__)))
#define CINT_W_DIV_NATIVE 1
#else
#define CINT_W_DIV_NATIVE 0
#endif

/* The product of two limbs: the low limb returned, the high one in *hi. */
static inline uint64_t cint_w_mul64(uint64_t a, uint64_t b, uint64_t *hi)
{
#if CINT_RT_USE_BUILTINS && defined(CINT_RT_BUILTINS_GNU)
    cint_w_u128 p = (cint_w_u128)a * b;
    *hi = (uint64_t)(p >> 64);
    return (uint64_t)p;
#elif CINT_RT_USE_BUILTINS && defined(_M_X64)
    return _umul128(a, b, hi);
#elif CINT_RT_USE_BUILTINS
    *hi = __umulh(a, b);
    return a * b;
#else
    return cint_rt_mul_wide(a, b, hi);
#endif
}

/* hi:lo divided by d, with d's top bit set and hi < d: the quotient
 * returned, the remainder in *rem. Where CINT_W_DIV_NATIVE holds this is
 * cint_rt_div_wide_b (EMIT-05, BX15-15, OQ-271); elsewhere it is the portable
 * step, cint_rt_div_wide's long division in base 2^32 without its
 * normalization. */
static inline uint64_t cint_w_div21(uint64_t hi, uint64_t lo, uint64_t d, uint64_t *rem)
{
#if CINT_W_DIV_NATIVE
    return cint_rt_div_wide_b(hi, lo, d, rem);
#else
    uint64_t q = 0u, d1 = d >> 32, d0 = d & 0xFFFFFFFFu;
    unsigned k;
    if (hi == 0u) {  /* lo < 2d: the quotient is 0 or 1 */
        q = lo >= d ? 1u : 0u;
        *rem = lo - (q != 0u ? d : 0u);
        return q;
    }
    for (k = 0u; k < 2u; k++) {
        uint64_t w = k == 0u ? lo >> 32 : lo & 0xFFFFFFFFu, digit = hi / d1, rhat = hi - digit * d1;
        while ((digit >> 32) != 0u || digit * d0 > ((rhat << 32) | w)) {
            digit--;
            rhat += d1;
            if ((rhat >> 32) != 0u) break;
        }
        hi = ((hi << 32) | w) - digit * d;  /* the partial remainder, below d */
        q = (q << 32) | digit;
    }
    *rem = hi;
    return q;
#endif
}

/* ------------------------------------------------------------------------- */
/* 3a. Two and four limbs (rt/OPEN.md RT-OQ-62).                              */
/*                                                                            */
/* I128 and U128 (n = 2) and I256 and U256 (n = 4) take the straight-line     */
/* steps of this section in place of the limb loops: the functions below      */
/* dispatch on n, a constant at every call site of section 4, so the compiler */
/* keeps one path, and a division or a product whose operands' magnitudes fit */
/* 128 bits takes the 2-limb step at either width. A product of more than 4   */
/* limbs whose operands both lie in 128 bits takes the 4-limb step            */
/* (RT-OQ-66). The results, overflow flags and fault records are the loops'.  */
/* CINT_W_GENERIC, defined when the runtime is built, keeps the loops for     */
/* every n and the portable quotient; rt/tests/test_wide_fast.c compares the  */
/* runtime built with and without it.                                         */

#if defined(CINT_W_GENERIC)
#define CINT_W_FAST 0
#else
#define CINT_W_FAST 1
#endif

/* a + b + *carry, the carry out (0 or 1) in *carry. */
static inline uint64_t cint_w_adc(uint64_t a, uint64_t b, uint64_t *carry)
{
    uint64_t t = a + b, r = t + *carry;
    *carry = (t < a ? 1u : 0u) | (r < t ? 1u : 0u);
    return r;
}

/* a - b - *borrow, the borrow out (0 or 1) in *borrow. */
static inline uint64_t cint_w_sbb(uint64_t a, uint64_t b, uint64_t *borrow)
{
    uint64_t t = a - b, r = t - *borrow;
    *borrow = (a < b ? 1u : 0u) | (t < *borrow ? 1u : 0u);
    return r;
}

#if CINT_RT_USE_BUILTINS && defined(CINT_RT_BUILTINS_GNU)
/* The builtin helper mode of GCC and Clang takes a 2-limb value as one 128-bit
 * integer for sums, differences, negations and comparisons, which they compile
 * to carries with no branch (EMIT-12, as for the limb product). */
static inline cint_w_u128 cint_w2_get(const uint64_t *a)
{
    return ((cint_w_u128)a[1] << 64) | a[0];
}

static inline void cint_w2_put(uint64_t *r, cint_w_u128 x)
{
    r[0] = (uint64_t)x;
    r[1] = (uint64_t)(x >> 64);
}
#endif

static inline uint64_t cint_w2_addc(uint64_t *r, const uint64_t *a, const uint64_t *b)
{
#if CINT_RT_USE_BUILTINS && defined(CINT_RT_BUILTINS_GNU)
    cint_w_u128 x = cint_w2_get(a), z = x + cint_w2_get(b);
    cint_w2_put(r, z);
    return z < x ? 1u : 0u;
#else
    uint64_t c = 0u, r0, r1;
    r0 = cint_w_adc(a[0], b[0], &c);
    r1 = cint_w_adc(a[1], b[1], &c);
    r[0] = r0;
    r[1] = r1;
    return c;
#endif
}

static inline uint64_t cint_w4_addc(uint64_t *r, const uint64_t *a, const uint64_t *b)
{
    uint64_t c = 0u, r0, r1, r2, r3;
    r0 = cint_w_adc(a[0], b[0], &c);
    r1 = cint_w_adc(a[1], b[1], &c);
    r2 = cint_w_adc(a[2], b[2], &c);
    r3 = cint_w_adc(a[3], b[3], &c);
    r[0] = r0;
    r[1] = r1;
    r[2] = r2;
    r[3] = r3;
    return c;
}

static inline uint64_t cint_w2_subb(uint64_t *r, const uint64_t *a, const uint64_t *b)
{
#if CINT_RT_USE_BUILTINS && defined(CINT_RT_BUILTINS_GNU)
    cint_w_u128 x = cint_w2_get(a), y = cint_w2_get(b);
    cint_w2_put(r, x - y);
    return x < y ? 1u : 0u;
#else
    uint64_t c = 0u, r0, r1;
    r0 = cint_w_sbb(a[0], b[0], &c);
    r1 = cint_w_sbb(a[1], b[1], &c);
    r[0] = r0;
    r[1] = r1;
    return c;
#endif
}

static inline uint64_t cint_w4_subb(uint64_t *r, const uint64_t *a, const uint64_t *b)
{
    uint64_t c = 0u, r0, r1, r2, r3;
    r0 = cint_w_sbb(a[0], b[0], &c);
    r1 = cint_w_sbb(a[1], b[1], &c);
    r2 = cint_w_sbb(a[2], b[2], &c);
    r3 = cint_w_sbb(a[3], b[3], &c);
    r[0] = r0;
    r[1] = r1;
    r[2] = r2;
    r[3] = r3;
    return c;
}

/* The sign mask of a flag: all ones when it is set, 0 when not. */
static inline uint64_t cint_w_mask(bool negative)
{
    return (uint64_t)0 - (negative ? 1u : 0u);
}

/* r = (a ^ m) - m for a sign mask m: a, or -a modulo 2^(64n) when m is all
 * ones, without a branch on the sign. */
static inline void cint_w2_cneg(uint64_t *r, const uint64_t *a, uint64_t m)
{
#if CINT_RT_USE_BUILTINS && defined(CINT_RT_BUILTINS_GNU)
    cint_w_u128 mm = ((cint_w_u128)m << 64) | m;
    cint_w2_put(r, (cint_w2_get(a) ^ mm) - mm);
#else
    uint64_t b = 0u, r0, r1;
    r0 = cint_w_sbb(a[0] ^ m, m, &b);
    r1 = cint_w_sbb(a[1] ^ m, m, &b);
    r[0] = r0;
    r[1] = r1;
#endif
}

static inline void cint_w4_cneg(uint64_t *r, const uint64_t *a, uint64_t m)
{
    uint64_t b = 0u, r0, r1, r2, r3;
    r0 = cint_w_sbb(a[0] ^ m, m, &b);
    r1 = cint_w_sbb(a[1] ^ m, m, &b);
    r2 = cint_w_sbb(a[2] ^ m, m, &b);
    r3 = cint_w_sbb(a[3] ^ m, m, &b);
    r[0] = r0;
    r[1] = r1;
    r[2] = r2;
    r[3] = r3;
}

/* -1, 0 or 1 as a < b, a == b or a > b, the top limbs compared as signed when
 * s: the borrows of a - b and b - a, with the sign bits flipped for s. */
static inline int cint_w2_cmp(const uint64_t *a, const uint64_t *b, bool s)
{
#if CINT_RT_USE_BUILTINS && defined(CINT_RT_BUILTINS_GNU)
    cint_w_u128 flip = (cint_w_u128)(s ? 1u : 0u) << 127, x = cint_w2_get(a) ^ flip, y = cint_w2_get(b) ^ flip;
    return (x > y ? 1 : 0) - (x < y ? 1 : 0);
#else
    uint64_t flip = s ? (uint64_t)1 << 63 : 0u, a1 = a[1] ^ flip, b1 = b[1] ^ flip, lt = 0u, gt = 0u;
    (void)cint_w_sbb(a[0], b[0], &lt);
    (void)cint_w_sbb(a1, b1, &lt);
    (void)cint_w_sbb(b[0], a[0], &gt);
    (void)cint_w_sbb(b1, a1, &gt);
    return (int)gt - (int)lt;
#endif
}

static inline int cint_w4_cmp(const uint64_t *a, const uint64_t *b, bool s)
{
    uint64_t flip = s ? (uint64_t)1 << 63 : 0u, a3 = a[3] ^ flip, b3 = b[3] ^ flip, lt = 0u, gt = 0u;
    unsigned i;
    for (i = 0u; i < 3u; i++) {
        (void)cint_w_sbb(a[i], b[i], &lt);
        (void)cint_w_sbb(b[i], a[i], &gt);
    }
    (void)cint_w_sbb(a3, b3, &lt);
    (void)cint_w_sbb(b3, a3, &gt);
    return (int)gt - (int)lt;
}

/* The product of two 2-limb magnitudes, all four limbs. p does not overlap a
 * or b. */
static inline void cint_w2_umul(uint64_t *p, const uint64_t *a, const uint64_t *b)
{
    uint64_t h00, h01, h10, h11, l00, l01, l10, l11, p1, p2, ca = 0u, cb = 0u;
    l00 = cint_w_mul64(a[0], b[0], &h00);
    l01 = cint_w_mul64(a[0], b[1], &h01);
    l10 = cint_w_mul64(a[1], b[0], &h10);
    l11 = cint_w_mul64(a[1], b[1], &h11);
    /* Two carry chains, each carrying 0 or 1 from limb to limb. */
    p1 = cint_w_adc(h00, l01, &ca);
    p1 = cint_w_adc(p1, l10, &cb);
    p2 = cint_w_adc(h01, h10, &ca);
    p2 = cint_w_adc(p2, l11, &cb);
    p[0] = l00;
    p[1] = p1;
    p[2] = p2;
    p[3] = h11 + ca + cb;  /* the product is below 2^256 */
}

/* hi:lo divided by d with hi < d, d not necessarily normalized: the quotient
 * returned, the remainder in *rem. */
static inline uint64_t cint_w_div21_any(uint64_t hi, uint64_t lo, uint64_t d, uint64_t *rem)
{
#if CINT_W_DIV_NATIVE
    return cint_rt_div_wide_b(hi, lo, d, rem);
#else
    unsigned s;
    uint64_t q;
    if (hi == 0u) {
        *rem = lo % d;
        return lo / d;
    }
    s = cint_rt_clz64(d);
    if (s != 0u) {
        hi = (hi << s) | (lo >> (64u - s));
        lo <<= s;
        d <<= s;
    }
    q = cint_w_div21(hi, lo, d, rem);
    *rem >>= s;
    return q;
#endif
}

/* The 2-limb magnitude u divided by the 2-limb magnitude v != 0: the quotient
 * in q and the remainder in r, 2 limbs each; q and r may not overlap u or v.
 * A divisor of two limbs gives a quotient below 2^64, estimated from the top
 * 64 bits of the normalized divisor and u / 2, then corrected once (Warren,
 * Hacker's Delight, 2nd ed., 9-5, on 64-bit digits). */
static inline void cint_w2_udivmod(uint64_t *q, uint64_t *r, const uint64_t *u, const uint64_t *v)
{
    uint64_t u0 = u[0], u1 = u[1], v0 = v[0], v1 = v[1];
    if (v1 == 0u) {
        uint64_t q1 = 0u, rest = u1;
        if (u1 >= v0) {
            q1 = u1 / v0;
            rest = u1 % v0;
        }
        q[0] = cint_w_div21_any(rest, u0, v0, &r[0]);
        q[1] = q1;
        r[1] = 0u;
    } else {
        unsigned s = cint_rt_clz64(v1);
        uint64_t top = s == 0u ? v1 : (v1 << s) | (v0 >> (64u - s)), dummy, est, ph, pl, borrow = 0u, r0, r1;
        /* u / 2 has its top limb below 2^63 <= top, so the 2-by-1 step applies */
        est = cint_w_div21(u1 >> 1, (u0 >> 1) | (u1 << 63), top, &dummy) >> (63u - s);
        if (est != 0u) est--;  /* now the quotient or one below it */
        pl = cint_w_mul64(est, v0, &ph);
        ph += est * v1;  /* est * v <= u */
        r0 = cint_w_sbb(u0, pl, &borrow);
        r1 = cint_w_sbb(u1, ph, &borrow);
        if (r1 > v1 || (r1 == v1 && r0 >= v0)) {
            est++;
            borrow = 0u;
            r0 = cint_w_sbb(r0, v0, &borrow);
            r1 = cint_w_sbb(r1, v1, &borrow);
        }
        q[0] = est;
        q[1] = 0u;
        r[0] = r0;
        r[1] = r1;
    }
}

/* Whether the 4-limb value a (signed when s) lies in 128 bits; m receives the
 * magnitude of its low 2 limbs, which is |a| when it does (2^127 at most when
 * signed), and *sign their sign mask. */
static inline bool cint_w4_small(uint64_t *m, const uint64_t *a, bool s, uint64_t *sign)
{
    uint64_t fill = s ? (uint64_t)0 - (a[1] >> 63) : 0u;
    *sign = fill;
    cint_w2_cneg(m, a, fill);
    return ((a[2] ^ fill) | (a[3] ^ fill)) == 0u;
}

/* r = the 4-limb value of sign mask `sign` and 2-limb magnitude m. */
static inline void cint_w4_put_small(uint64_t *r, const uint64_t *m, uint64_t sign)
{
    uint64_t fill = sign & cint_w_mask((m[0] | m[1]) != 0u);
    cint_w2_cneg(r, m, sign);
    r[2] = fill;
    r[3] = fill;
}

/* The signs of a division (kind as for cint_w_divide, section 3) from the
 * 2-limb magnitudes qm and rm of |a| / |b|, mb = |b|, and the operands' sign
 * masks sa and sb: qm and rm become the magnitudes of the quotient and the
 * remainder, *rs the remainder's sign mask, and the quotient's is sa ^ sb.
 * Floor with unequal signs and Euclidean division of a negative dividend step
 * one further from zero when the remainder is not 0, which leaves the
 * remainder mb - rm, of the divisor's sign for floor and positive for
 * Euclidean; otherwise the remainder takes the dividend's sign, except for
 * Euclidean. All without a branch on the signs. */
static inline void cint_w2_div_signs(uint64_t *qm, uint64_t *rm, const uint64_t *mb, unsigned kind, uint64_t sa,
                                     uint64_t sb, uint64_t *rs)
{
    uint64_t step = (kind == 0u ? sa ^ sb : kind == 2u ? sa : 0u) & cint_w_mask((rm[0] | rm[1]) != 0u);
    uint64_t c = step & 1u, b = 0u, t0, t1;
    qm[0] = cint_w_adc(qm[0], 0u, &c);
    qm[1] += c;
    t0 = cint_w_sbb(mb[0], rm[0], &b);
    t1 = cint_w_sbb(mb[1], rm[1], &b);
    rm[0] = (t0 & step) | (rm[0] & ~step);
    rm[1] = (t1 & step) | (rm[1] & ~step);
    *rs = (step & (kind == 0u ? sb : 0u)) | (~step & (kind != 2u ? sa : 0u));
}

static inline void cint_w_copy(uint64_t *r, const uint64_t *a, unsigned n)
{
    unsigned i;
    for (i = 0u; i < n; i++) r[i] = a[i];
}

static inline bool cint_w_negative(const uint64_t *a, unsigned n)
{
    return (a[n - 1u] >> 63) != 0u;
}

static inline bool cint_w_is_zero(const uint64_t *a, unsigned n)
{
    uint64_t any = 0u;
    unsigned i;
#if CINT_W_FAST
    if (n == 2u) return (a[0] | a[1]) == 0u;
    if (n == 4u) return (a[0] | a[1] | a[2] | a[3]) == 0u;
#endif
    for (i = 0u; i < n; i++) any |= a[i];
    return any == 0u;
}

/* The most negative value: 2^(w - 1) as a pattern. */
static inline bool cint_w_is_min(const uint64_t *a, unsigned n)
{
    return a[n - 1u] == (uint64_t)1 << 63 && cint_w_is_zero(a, n - 1u);
}

static inline bool cint_w_is_minus_one(const uint64_t *a, unsigned n)
{
    uint64_t all = UINT64_MAX;
    unsigned i;
#if CINT_W_FAST
    if (n == 2u) return (a[0] & a[1]) == UINT64_MAX;
    if (n == 4u) return (a[0] & a[1] & a[2] & a[3]) == UINT64_MAX;
#endif
    for (i = 0u; i < n; i++) all &= a[i];
    return all == UINT64_MAX;
}

/* Whether a / b overflows: the pair (MIN, -1), for a signed type only. The
 * sign is a function argument, not a condition of the type macros, so MSVC
 * sees no constant condition (C4127) in an unsigned instantiation. */
static inline bool cint_w_div_overflows(const uint64_t *a, const uint64_t *b, unsigned n, bool s)
{
    return s && cint_w_is_min(a, n) && cint_w_is_minus_one(b, n);
}

/* r = a + b modulo 2^(64n); the carry out of the top limb returned. */
static inline uint64_t cint_w_addc(uint64_t *r, const uint64_t *a, const uint64_t *b, unsigned n)
{
    uint64_t carry = 0u;
    unsigned i;
#if CINT_W_FAST
    if (n == 2u) return cint_w2_addc(r, a, b);
    if (n == 4u) return cint_w4_addc(r, a, b);
#endif
    for (i = 0u; i < n; i++) {
        uint64_t s = a[i] + b[i];
        uint64_t t = s + carry;
        carry = (s < a[i] ? 1u : 0u) | (t < s ? 1u : 0u);
        r[i] = t;
    }
    return carry;
}

/* r = a - b modulo 2^(64n); the borrow out of the top limb returned. */
static inline uint64_t cint_w_subb(uint64_t *r, const uint64_t *a, const uint64_t *b, unsigned n)
{
    uint64_t borrow = 0u;
    unsigned i;
#if CINT_W_FAST
    if (n == 2u) return cint_w2_subb(r, a, b);
    if (n == 4u) return cint_w4_subb(r, a, b);
#endif
    for (i = 0u; i < n; i++) {
        uint64_t d = a[i] - b[i];
        uint64_t t = d - borrow;
        borrow = (a[i] < b[i] ? 1u : 0u) | (d < borrow ? 1u : 0u);
        r[i] = t;
    }
    return borrow;
}

/* r = -a modulo 2^(64n). */
static inline void cint_w_negate(uint64_t *r, const uint64_t *a, unsigned n)
{
    uint64_t carry = 1u;
    unsigned i;
#if CINT_W_FAST
    if (n == 2u) {
        cint_w2_cneg(r, a, UINT64_MAX);
        return;
    }
    if (n == 4u) {
        cint_w4_cneg(r, a, UINT64_MAX);
        return;
    }
#endif
    for (i = 0u; i < n; i++) {
        uint64_t t = ~a[i] + carry;
        carry = carry != 0u && t == 0u ? 1u : 0u;
        r[i] = t;
    }
}

/* r = a, or -a modulo 2^(64n) when negate. */
static inline void cint_w_cneg(uint64_t *r, const uint64_t *a, unsigned n, bool negate)
{
#if CINT_W_FAST
    if (n == 2u) {
        cint_w2_cneg(r, a, cint_w_mask(negate));
        return;
    }
    if (n == 4u) {
        cint_w4_cneg(r, a, cint_w_mask(negate));
        return;
    }
#endif
    if (negate) {
        cint_w_negate(r, a, n);
    } else {
        cint_w_copy(r, a, n);
    }
}

/* m = |a| as an n-limb magnitude (2^(w - 1) for the most negative value);
 * returns whether a is negative. s: whether a is signed (a constant at every
 * call site of section 4, here and below). */
static inline bool cint_w_magnitude(uint64_t *m, const uint64_t *a, unsigned n, bool s)
{
    if (s && cint_w_negative(a, n)) {
        cint_w_negate(m, a, n);
        return true;
    }
    cint_w_copy(m, a, n);
    return false;
}

/* Compares two n-limb magnitudes: -1, 0 or 1. */
static inline int cint_w_ucmp(const uint64_t *a, const uint64_t *b, unsigned n)
{
    unsigned i = n;
#if CINT_W_FAST
    if (n == 2u) return cint_w2_cmp(a, b, false);
    if (n == 4u) return cint_w4_cmp(a, b, false);
#endif
    while (i > 0u) {
        i--;
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

/* Compares two n-limb values: -1, 0 or 1. */
static inline int cint_w_cmp(const uint64_t *a, const uint64_t *b, unsigned n, bool s)
{
    bool an = s && cint_w_negative(a, n), bn = s && cint_w_negative(b, n);
#if CINT_W_FAST
    if (n == 2u) return cint_w2_cmp(a, b, s);
    if (n == 4u) return cint_w4_cmp(a, b, s);
#endif
    if (an != bn) return an ? -1 : 1;
    return cint_w_ucmp(a, b, n);
}

/* Whether two n-limb patterns are equal. */
static inline bool cint_w_eq(const uint64_t *a, const uint64_t *b, unsigned n)
{
#if CINT_W_FAST
    if (n == 2u) return ((a[0] ^ b[0]) | (a[1] ^ b[1])) == 0u;
    if (n == 4u) return ((a[0] ^ b[0]) | (a[1] ^ b[1]) | (a[2] ^ b[2]) | (a[3] ^ b[3])) == 0u;
#endif
    return cint_w_ucmp(a, b, n) == 0;
}

/* The significant limbs of an n-limb magnitude: n less its zero top limbs. */
static inline unsigned cint_w_len(const uint64_t *a, unsigned n)
{
    while (n > 0u && a[n - 1u] == 0u) n--;
    return n;
}

/* m = |a| in its significant limbs, their count returned (0 for zero) and the
 * sign in *negative. Only the limbs a needs as a signed value are read past
 * the top one, and only those are negated. */
static inline unsigned cint_w_mag(uint64_t *m, const uint64_t *a, unsigned n, bool s, bool *negative)
{
    uint64_t fill = (uint64_t)0 - (a[n - 1u] >> 63);
    unsigned len = n, i;
    if (!s) {
        *negative = false;
        cint_w_copy(m, a, n);
        return cint_w_len(m, n);
    }
    while (len > 1u && a[len - 1u] == fill && ((a[len - 2u] ^ fill) >> 63) == 0u) len--;
    *negative = fill != 0u;
    if (fill != 0u) {
        uint64_t carry = 1u;
        for (i = 0u; i < len; i++) {
            uint64_t t = ~a[i] + carry;
            carry = t < carry ? 1u : 0u;
            m[i] = t;
        }
    } else {
        cint_w_copy(m, a, len);
    }
    return cint_w_len(m, len);
}

/* r = the n-limb value of sign `negative` and magnitude m[0 .. len): whether
 * it fits, r written only when it does. An unsigned r is never negative. */
static inline bool cint_w_store(uint64_t *r, const uint64_t *m, unsigned len, unsigned n, bool negative, bool s)
{
    unsigned i;
    if (len > n) return false;
    if (s && len == n && (m[n - 1u] >> 63) != 0u && !(negative && cint_w_is_min(m, n))) return false;
    if (negative) {
        uint64_t carry = 1u;
        for (i = 0u; i < len; i++) {
            uint64_t t = ~m[i] + carry;
            carry = t < carry ? 1u : 0u;
            r[i] = t;
        }
        for (; i < n; i++) r[i] = carry - 1u;  /* all ones unless m is zero */
    } else {
        cint_w_copy(r, m, len);
        for (i = len; i < n; i++) r[i] = 0u;
    }
    return true;
}

/* p[0 .. la + lb) = a[0 .. la) * b[0 .. lb), la and lb at least 1: schoolbook
 * over the significant limbs. p does not overlap a or b. */
static inline void cint_w_umul_len(uint64_t *p, const uint64_t *a, unsigned la, const uint64_t *b, unsigned lb)
{
    uint64_t carry = 0u;
    unsigned i, j;
    for (j = 0u; j < lb; j++) {
        uint64_t hi, lo = cint_w_mul64(a[0], b[j], &hi);
        lo += carry;
        hi += lo < carry ? 1u : 0u;
        p[j] = lo;
        carry = hi;
    }
    p[lb] = carry;
    for (i = 1u; i < la; i++) {
        carry = 0u;
        for (j = 0u; j < lb; j++) {
            uint64_t hi, lo = cint_w_mul64(a[i], b[j], &hi);
            lo += carry;
            hi += lo < carry ? 1u : 0u;
            lo += p[i + j];
            hi += lo < p[i + j] ? 1u : 0u;
            p[i + j] = lo;
            carry = hi;
        }
        p[i + lb] = carry;
    }
}

/* p = a * b, the 2n-limb product of two n-limb magnitudes. */
static inline void cint_w_umul(uint64_t *p, const uint64_t *a, const uint64_t *b, unsigned n)
{
    unsigned la, lb, i;
#if CINT_W_FAST
    if (n == 2u) {
        cint_w2_umul(p, a, b);
        return;
    }
#endif
    la = cint_w_len(a, n);
    lb = cint_w_len(b, n);
    if (la == 0u || lb == 0u) {
        la = 0u;
        lb = 0u;
    } else {
        cint_w_umul_len(p, a, la, b, lb);
    }
    for (i = la + lb; i < 2u * n; i++) p[i] = 0u;
}

/* r = a * b modulo 2^(64n): the low n limbs of the product of two patterns. */
static inline void cint_w_mul_low(uint64_t *r, const uint64_t *a, const uint64_t *b, unsigned n)
{
    unsigned i, j;
#if CINT_W_FAST
    if (n == 2u) {
        uint64_t hi, lo = cint_w_mul64(a[0], b[0], &hi);
        hi += a[0] * b[1] + a[1] * b[0];
        r[0] = lo;
        r[1] = hi;
        return;
    }
#endif
    for (i = 0u; i < n; i++) r[i] = 0u;
    for (i = 0u; i < n; i++) {
        uint64_t carry = 0u;
        if (a[i] == 0u) continue;
        for (j = 0u; i + j < n; j++) {
            uint64_t hi, lo = cint_w_mul64(a[i], b[j], &hi);
            lo += carry;
            hi += lo < carry ? 1u : 0u;
            r[i + j] += lo;
            hi += r[i + j] < lo ? 1u : 0u;
            carry = hi;
        }
    }
}

/* r = a * 2^k modulo 2^(64n), k < 64n. */
static inline void cint_w_shl_raw(uint64_t *r, const uint64_t *a, unsigned k, unsigned n)
{
    unsigned limbs = k / 64u, bits = k % 64u, i = n;
    while (i > 0u) {
        uint64_t v = 0u;
        i--;
        if (i >= limbs) {
            v = a[i - limbs] << bits;
            if (bits != 0u && i > limbs) v |= a[i - limbs - 1u] >> (64u - bits);
        }
        r[i] = v;
    }
}

/* r = floor(a / 2^k), k < 64n: arithmetic for a signed a (EMIT-10), logical
 * for an unsigned one. */
static inline void cint_w_shr_raw(uint64_t *r, const uint64_t *a, unsigned k, unsigned n, bool s)
{
    unsigned limbs = k / 64u, bits = k % 64u, i;
    uint64_t fill = s && cint_w_negative(a, n) ? UINT64_MAX : 0u;
    for (i = 0u; i < n; i++) {
        uint64_t lo = i + limbs < n ? a[i + limbs] : fill;
        uint64_t hi = i + limbs + 1u < n ? a[i + limbs + 1u] : fill;
        r[i] = bits == 0u ? lo : (lo >> bits) | (hi << (64u - bits));
    }
}

/* r = a rotated left by k < 64n, n-limb unsigned. */
static inline void cint_w_rotl(uint64_t *r, const uint64_t *a, unsigned k, unsigned n)
{
    uint64_t lo[16], hi[16];
    unsigned i;
    if (k == 0u) {
        cint_w_copy(r, a, n);
        return;
    }
    cint_w_shl_raw(lo, a, k, n);
    cint_w_shr_raw(hi, a, 64u * n - k, n, false);
    for (i = 0u; i < n; i++) r[i] = lo[i] | hi[i];
}

/* r (nr limbs) = a (na <= nr limbs) extended by its sign, or by zeros. */
static inline void cint_w_extend(uint64_t *r, unsigned nr, const uint64_t *a, unsigned na, bool s)
{
    uint64_t fill = s && cint_w_negative(a, na) ? UINT64_MAX : 0u;
    unsigned i;
    cint_w_copy(r, a, na);
    for (i = na; i < nr; i++) r[i] = fill;
}

/* r (nr limbs) = a (na limbs): extended, or its low nr limbs. */
static inline void cint_w_resize(uint64_t *r, unsigned nr, const uint64_t *a, unsigned na, bool s)
{
    if (nr >= na) {
        cint_w_extend(r, nr, a, na, s);
    } else {
        cint_w_copy(r, a, nr);
    }
}

/* Whether the na-limb value a (signed when as) lies in the range of nr limbs
 * (signed when rs), any nr. */
static inline bool cint_w_fits_into(const uint64_t *a, unsigned na, bool as, unsigned nr, bool rs)
{
    bool negative = as && cint_w_negative(a, na);
    uint64_t fill = negative ? UINT64_MAX : 0u;
    unsigned i;
    if (negative && !rs) return false;
    for (i = nr; i < na; i++) {
        if (a[i] != fill) return false;
    }
    /* A signed range of at most na limbs also needs a's sign in its top bit. */
    return !rs || nr > na || ((a[nr - 1u] >> 63) != 0u) == negative;
}

/* Whether the n-limb value a lies in the range of the narrow type `tag`. */
static inline bool cint_w_fits_narrow(const uint64_t *a, unsigned n, bool s, uint32_t tag)
{
    uint64_t high = a[n - 1u];
    unsigned i;
#if CINT_W_FAST
    if (n == 2u && s) {
        /* a lies in I64 when its high limb is the sign of its low one, and
         * otherwise only U64 holds it, from 2^63 to 2^64 - 1 */
        if (a[1] + (a[0] >> 63) == 0u) return cint_rt_fits(a[0], true, tag);
        return a[1] == 0u && !cint_rt_tag_signed(tag) && cint_rt_tag_width(tag) == 64u;
    }
#endif
    if (!s && high != 0u) return false;
    for (i = 1u; i < n; i++) {
        if (a[i] != high) return false;
    }
    if (high == 0u) return cint_rt_fits(a[0], false, tag);
    return high == UINT64_MAX && (a[0] >> 63) != 0u && cint_rt_fits(a[0], true, tag);
}

/* The n-limb pattern of the narrow value `bits` of type `tag`. */
static inline void cint_w_from_narrow(uint64_t *r, unsigned n, uint32_t tag, uint64_t bits)
{
    unsigned w = cint_rt_tag_width(tag), i;
    uint64_t v = cint_rt_tag_signed(tag) ? (uint64_t)cint_rt_sext(bits, w) : bits & cint_rt_mask(w);
    uint64_t fill = cint_rt_tag_signed(tag) ? (uint64_t)0 - (v >> 63) : 0u;
    r[0] = v;
    for (i = 1u; i < n; i++) r[i] = fill;
}

/* The largest (negative false) or smallest value of n limbs. */
static inline void cint_w_bound(uint64_t *r, unsigned n, bool negative, bool s)
{
    unsigned i;
    for (i = 0u; i < n; i++) r[i] = negative ? 0u : UINT64_MAX;
    if (s) r[n - 1u] = negative ? (uint64_t)1 << 63 : UINT64_MAX >> 1;
}

#if CINT_RT_USE_BUILTINS && defined(CINT_RT_BUILTINS_GNU)
/* cint_w_add_ovf and cint_w_sub_ovf at 2 limbs: the overflow builtins on the
 * compiler's 128-bit integers, an add or subtract with carry and the overflow
 * or carry flag. Converting the unsigned value to the signed type keeps its
 * bits on GCC and Clang. */
static inline bool cint_w2_add_ovf(uint64_t *r, const uint64_t *a, const uint64_t *b, bool s)
{
    cint_w_u128 z;
    bool over;
    if (s) {
        cint_w_s128 t;
        over = __builtin_add_overflow((cint_w_s128)cint_w2_get(a), (cint_w_s128)cint_w2_get(b), &t);
        z = (cint_w_u128)t;
    } else {
        over = __builtin_add_overflow(cint_w2_get(a), cint_w2_get(b), &z);
    }
    cint_w2_put(r, z);
    return over;
}

static inline bool cint_w2_sub_ovf(uint64_t *r, const uint64_t *a, const uint64_t *b, bool s)
{
    cint_w_u128 z;
    bool over;
    if (s) {
        cint_w_s128 t;
        over = __builtin_sub_overflow((cint_w_s128)cint_w2_get(a), (cint_w_s128)cint_w2_get(b), &t);
        z = (cint_w_u128)t;
    } else {
        over = __builtin_sub_overflow(cint_w2_get(a), cint_w2_get(b), &z);
    }
    cint_w2_put(r, z);
    return over;
}
#endif

/* Overflow of a + b and a - b; *r holds the wrapped result. */
static inline bool cint_w_add_ovf(uint64_t *r, const uint64_t *a, const uint64_t *b, unsigned n, bool s)
{
#if CINT_W_FAST && CINT_RT_USE_BUILTINS && defined(CINT_RT_BUILTINS_GNU)
    if (n == 2u) return cint_w2_add_ovf(r, a, b, s);
#endif
#if CINT_W_FAST
    if (n == 2u || n == 4u) {
        /* signed: a and b of one sign, r of the other */
        uint64_t top_a = a[n - 1u], top_b = b[n - 1u], carry = cint_w_addc(r, a, b, n);
        return s ? (((top_a ^ r[n - 1u]) & (top_b ^ r[n - 1u])) >> 63) != 0u : carry != 0u;
    }
#endif
    bool an = cint_w_negative(a, n), bn = cint_w_negative(b, n);
    uint64_t carry = cint_w_addc(r, a, b, n);
    return s ? an == bn && cint_w_negative(r, n) != an : carry != 0u;
}

static inline bool cint_w_sub_ovf(uint64_t *r, const uint64_t *a, const uint64_t *b, unsigned n, bool s)
{
#if CINT_W_FAST && CINT_RT_USE_BUILTINS && defined(CINT_RT_BUILTINS_GNU)
    if (n == 2u) return cint_w2_sub_ovf(r, a, b, s);
#endif
#if CINT_W_FAST
    if (n == 2u || n == 4u) {
        /* signed: a and b of opposite signs, r of b's */
        uint64_t top_a = a[n - 1u], top_b = b[n - 1u], borrow = cint_w_subb(r, a, b, n);
        return s ? (((top_a ^ top_b) & (top_a ^ r[n - 1u])) >> 63) != 0u : borrow != 0u;
    }
#endif
    bool an = cint_w_negative(a, n), bn = cint_w_negative(b, n);
    uint64_t borrow = cint_w_subb(r, a, b, n);
    return s ? an != bn && cint_w_negative(r, n) != an : borrow != 0u;
}

/* cint_w_mul_ovf at 2 limbs. The builtin mode of GCC and Clang multiplies the
 * magnitudes with the overflow builtin on the unsigned 128-bit integer, which
 * calls no support routine (the signed one would, __muloti4 under Clang).
 * Otherwise operands whose high limbs are both nonzero overflow, and one
 * cross product completes the low one. */
static inline bool cint_w2_mul_ovf(uint64_t *r, const uint64_t *a, const uint64_t *b, bool s)
{
    uint64_t sa = s ? (uint64_t)0 - (a[1] >> 63) : 0u, sb = s ? (uint64_t)0 - (b[1] >> 63) : 0u, sr = sa ^ sb;
    uint64_t ma[2], mb[2], p[2];
    cint_w2_cneg(ma, a, sa);
    cint_w2_cneg(mb, b, sb);
#if CINT_RT_USE_BUILTINS && defined(CINT_RT_BUILTINS_GNU)
    {
        cint_w_u128 z;
        if (__builtin_mul_overflow(cint_w2_get(ma), cint_w2_get(mb), &z)) return true;
        cint_w2_put(p, z);
    }
#else
    {
        uint64_t hi, cross_hi, cross, x, y;
        if (ma[1] != 0u && mb[1] != 0u) return true;
        x = ma[1] | mb[1];  /* the one high limb, or 0 */
        y = ma[1] != 0u ? mb[0] : ma[0];
        p[0] = cint_w_mul64(ma[0], mb[0], &hi);
        cross = cint_w_mul64(x, y, &cross_hi);
        p[1] = hi + cross;
        if (cross_hi != 0u || p[1] < hi) return true;
    }
#endif
    /* p = |a| * |b| < 2^128: up to 2^127 - 1, or 2^127 when negative, if signed */
    if (s && (p[1] >> 63) != 0u && !(sr != 0u && p[1] == (uint64_t)1 << 63 && p[0] == 0u)) return true;
    cint_w2_cneg(r, p, sr);
    return false;
}

/* cint_w_mul_ovf at 4 limbs: operands in 128 bits take the 2-limb product,
 * which always fits (|a| * |b| <= 2^254 when signed, below 2^256 when not),
 * and others cint_rt_wide_mul4 (section 2). */
static inline bool cint_w4_mul_ovf(uint64_t *r, const uint64_t *a, const uint64_t *b, bool s)
{
    uint64_t ma[2], mb[2], p[4], sa, sb;
    bool small = cint_w4_small(ma, a, s, &sa);
    if (!(cint_w4_small(mb, b, s, &sb) && small)) return cint_rt_wide_mul4(r, a, b, s);
    if ((ma[1] | mb[1]) == 0u) {  /* the product lies in 128 bits */
        p[0] = cint_w_mul64(ma[0], mb[0], &p[1]);
        cint_w4_put_small(r, p, sa ^ sb);
        return false;
    }
    cint_w2_umul(p, ma, mb);
    cint_w4_cneg(r, p, sa ^ sb);
    return false;
}

/* Whether a and b of n limbs lie in 128 bits (limbs 2 and up the sign fill of
 * limb 1, or 0); cint_wn_extend4 below writes the 4-limb t as n limbs. */
CINT_RT_FOLD bool cint_wn_in128(const uint64_t *a, const uint64_t *b, unsigned n, bool s)
{
    uint64_t fa = s ? (uint64_t)0 - (a[1] >> 63) : 0u, fb = s ? (uint64_t)0 - (b[1] >> 63) : 0u, acc = 0u;
    unsigned k;
    for (k = 2u; k < n; k++) acc |= (a[k] ^ fa) | (b[k] ^ fb);
    return acc == 0u;
}

CINT_RT_FOLD void cint_wn_extend4(uint64_t *r, const uint64_t *t, unsigned n, bool s)
{
    uint64_t f = s ? (uint64_t)0 - (t[3] >> 63) : 0u;
    unsigned k;
    for (k = 0u; k < 4u; k++) r[k] = t[k];
    for (k = 4u; k < n; k++) r[k] = f;
}

/* Overflow of a * b; *r holds the product when it fits and is not written
 * when it does not. Operands of la and lb significant limbs with la + lb >
 * n + 1 overflow before any limb product. */
static inline bool cint_wn_mul_ovf(uint64_t *r, const uint64_t *a, const uint64_t *b, unsigned n, bool s)
{
    uint64_t ma[16], mb[16], p[17];
    bool an, bn;
    unsigned la = cint_w_mag(ma, a, n, s, &an), lb = cint_w_mag(mb, b, n, s, &bn), i;
    if (la == 0u || lb == 0u) {
        for (i = 0u; i < n; i++) r[i] = 0u;
        return false;
    }
    if (la + lb > n + 1u) return true;
    cint_w_umul_len(p, ma, la, mb, lb);
    return !cint_w_store(r, p, cint_w_len(p, la + lb), n, an != bn, s);
}

/* cint_w_mul_ovf folds into each helper with its n (rt/OPEN.md RT-OQ-69).
 * Above 4 limbs, operands both in 128 bits have a product of at most 2^254 in
 * magnitude, below 2^256 unsigned, which the 4-limb step gives (RT-OQ-66). */
CINT_RT_FOLD bool cint_w_mul_ovf(uint64_t *r, const uint64_t *a, const uint64_t *b, unsigned n, bool s)
{
#if CINT_W_FAST
    uint64_t t[4];
    if (n == 2u) return cint_w2_mul_ovf(r, a, b, s);
    if (n == 4u) return cint_w4_mul_ovf(r, a, b, s);
    if (cint_wn_in128(a, b, n, s)) {
        (void)cint_w4_mul_ovf(t, a, b, s);
        cint_wn_extend4(r, t, n, s);
        return false;
    }
#endif
    return cint_wn_mul_ovf(r, a, b, n, s);
}

/* The magnitude parts of a division of a by b != 0 (the pair (MIN, -1)
 * included): kind 0 floor (SPEC-01 4.4), 1 truncating, 2 Euclidean. q and r
 * receive the n-limb quotient and remainder; the quotient of (MIN, -1), 2^(w -
 * 1), comes back wrapped to MIN, and the callers check that pair first. */
static inline void cint_wn_divide(uint64_t *q, uint64_t *r, const uint64_t *a, const uint64_t *b, unsigned n,
                                  unsigned kind, bool s)
{
    /* ma and mb start zeroed: GCC 16 cannot tell that n > 0 in its outlined copy of this body. */
    uint64_t ma[16] = {0}, mb[16] = {0}, qm[16], rm[16];
    bool an = cint_w_magnitude(ma, a, n, s), bn = cint_w_magnitude(mb, b, n, s);
    bool exact;
    unsigned i;
    cint_rt_wide_udivmod(qm, rm, ma, n, mb, n);
    exact = cint_w_is_zero(rm, n);
    if (!exact && ((kind == 0u && an != bn) || (kind == 2u && an))) {
        /* One step further from zero, the remainder taken from the divisor's side. */
        uint64_t one[16];
        for (i = 0u; i < n; i++) one[i] = i == 0u ? 1u : 0u;
        (void)cint_w_addc(qm, qm, one, n);
        (void)cint_w_subb(rm, mb, rm, n);
        if (kind == 0u && bn) cint_w_negate(rm, rm, n);  /* floor: the divisor's sign */
    } else if (an && kind != 2u) {
        cint_w_negate(rm, rm, n);  /* truncating, or floor with equal signs: the dividend's */
    }
    if (an != bn) {
        cint_w_negate(q, qm, n);
    } else {
        cint_w_copy(q, qm, n);
    }
    cint_w_copy(r, rm, n);
}

/* cint_w_divide at 2 limbs. */
static inline void cint_w2_divide(uint64_t *q, uint64_t *r, const uint64_t *a, const uint64_t *b, unsigned kind,
                                  bool s)
{
    uint64_t ma[2], mb[2], qm[2], rm[2], rs;
    uint64_t sa = s ? (uint64_t)0 - (a[1] >> 63) : 0u, sb = s ? (uint64_t)0 - (b[1] >> 63) : 0u;
    cint_w2_cneg(ma, a, sa);
    cint_w2_cneg(mb, b, sb);
    cint_w2_udivmod(qm, rm, ma, mb);
    cint_w2_div_signs(qm, rm, mb, kind, sa, sb, &rs);
    cint_w2_cneg(q, qm, sa ^ sb);
    cint_w2_cneg(r, rm, rs);
}

/* cint_w_divide at 4 limbs: operands in 128 bits take cint_w2_udivmod and the
 * signs on 2 limbs, where a quotient one step further from zero stays below
 * 2^127, as the divisor's magnitude is at least 2 when the remainder is not 0;
 * others take cint_rt_wide_divide4 (section 2). */
static inline void cint_w4_divide(uint64_t *q, uint64_t *r, const uint64_t *a, const uint64_t *b, unsigned kind,
                                  bool s)
{
    uint64_t ma[2], mb[2], qm[2], rm[2], sa, sb, rs;
    bool small = cint_w4_small(ma, a, s, &sa);
    if (!(cint_w4_small(mb, b, s, &sb) && small)) {
        cint_rt_wide_divide4(q, r, a, b, kind, s);
        return;
    }
    cint_w2_udivmod(qm, rm, ma, mb);
    cint_w2_div_signs(qm, rm, mb, kind, sa, sb, &rs);
    cint_w4_put_small(q, qm, sa ^ sb);
    cint_w4_put_small(r, rm, rs);
}

/* cint_w_divide folds into each helper with its n. Above 4 limbs, operands
 * both in 128 bits have a quotient of magnitude at most 2^127 + 1 and a
 * remainder below the divisor's, which the 4-limb step gives (RT-OQ-69). */
CINT_RT_FOLD void cint_w_divide(uint64_t *q, uint64_t *r, const uint64_t *a, const uint64_t *b, unsigned n,
                               unsigned kind, bool s)
{
#if CINT_W_FAST
    uint64_t qt[4], rt[4];
    if (n == 2u) cint_w2_divide(q, r, a, b, kind, s);
    else if (n == 4u) cint_w4_divide(q, r, a, b, kind, s);
    else if (cint_wn_in128(a, b, n, s)) {
        cint_w4_divide(qt, rt, a, b, kind, s);
        cint_wn_extend4(q, qt, n, s);
        cint_wn_extend4(r, rt, n, s);
    } else
#endif
        cint_wn_divide(q, r, a, b, n, kind, s);
}

/* Whether a quotient of sign `negative` with truncated magnitude q, nonzero
 * remainder rem and divisor magnitude d rounds away from zero under mode
 * (SPEC-01 4.5, numbered as in cint_rt_builtin; cint_rt_round_up's rule). */
static inline bool cint_w_round_up(const uint64_t *q, const uint64_t *rem, const uint64_t *d, unsigned n,
                                   bool negative, uint32_t mode)
{
    uint64_t rest[32];
    int c;
    if (mode <= 4u) return mode == 1u ? negative : mode == 2u ? !negative : mode == 4u;
    (void)cint_w_subb(rest, d, rem, n);
    c = cint_w_ucmp(rem, rest, n);
    if (c != 0) return c > 0;
    return mode == 5u ? (q[0] & 1u) != 0u : mode == 6u || (mode == 8u ? !negative : mode == 9u && negative);
}

/* cint_w_div_round for n = 2 and n = 4 when both magnitudes are below 2^128:
 * whether it decided, with the outcome in *ok. */
static inline bool cint_w_div_round_small(uint64_t *r, const uint64_t *a, const uint64_t *b, unsigned n,
                                          uint32_t mode, bool s, bool *ok)
{
    uint64_t ma[4] = {0u, 0u, 0u, 0u}, mb[4] = {0u, 0u, 0u, 0u}, q[4] = {0u, 0u, 0u, 0u}, rem[2] = {0u, 0u};
    bool an = s && cint_w_negative(a, n), bn = s && cint_w_negative(b, n), negative = an != bn;
    cint_w_cneg(ma, a, n, an);
    cint_w_cneg(mb, b, n, bn);
    if (n == 4u && (ma[2] | ma[3] | mb[2] | mb[3]) != 0u) return false;
    *ok = false;
    if ((mb[0] | mb[1]) == 0u) return true;
    cint_w2_udivmod(q, rem, ma, mb);
    if ((rem[0] | rem[1]) != 0u && cint_w_round_up(q, rem, mb, 2u, negative, mode)) {
        uint64_t c = 1u;
        q[0] = cint_w_adc(q[0], 0u, &c);
        q[1] = cint_w_adc(q[1], 0u, &c);
        q[2] = c;
    }
    /* q < 2^129, which 4 limbs hold either way */
    if (n == 2u && (q[2] != 0u || (s && (q[1] >> 63) != 0u && !(negative && cint_w_is_min(q, 2u))))) return true;
    cint_w_cneg(r, q, n, negative);
    *ok = true;
    return true;
}

/* a / b rounded under mode (built-in op 10, SPEC-01 4.5) over the operands'
 * significant limbs: false for b == 0 or a quotient that does not fit, r
 * written only when it does. a and b are read before r is written. */
static inline bool cint_wn_div_round(uint64_t *r, const uint64_t *a, const uint64_t *b, unsigned n, uint32_t mode,
                                     bool s)
{
    uint64_t ma[16], mb[16], q[17], rem[16];
    bool an, bn;
    unsigned la = cint_w_mag(ma, a, n, s, &an), lb = cint_w_mag(mb, b, n, s, &bn), lq, i;
    if (lb == 0u) return false;
    if (la < lb) {  /* |a| < |b|: quotient 0, remainder |a| */
        q[0] = 0u;
        lq = 1u;
        cint_w_copy(rem, ma, la);
        for (i = la; i < lb; i++) rem[i] = 0u;
    } else {
        cint_rt_wide_udivrem(q, rem, ma, la, mb, lb);
        lq = la - lb + 1u;
    }
    if (!cint_w_is_zero(rem, lb) && cint_w_round_up(q, rem, mb, lb, an != bn, mode)) {
        q[lq] = 0u;
        i = 0u;
        while (++q[i] == 0u) i++;
        lq++;
    }
    return cint_w_store(r, q, cint_w_len(q, lq), n, an != bn, s);
}

/* cint_w_div_round folds likewise; above 4 limbs, operands both in 128 bits
 * take the 4-limb step, as |q| <= 2^128 + 1 (RT-OQ-69). */
CINT_RT_FOLD bool cint_w_div_round(uint64_t *r, const uint64_t *a, const uint64_t *b, unsigned n, uint32_t mode,
                                  bool s)
{
#if CINT_W_FAST
    bool ok = false;
    uint64_t t[4];
    if ((n == 2u || n == 4u) && cint_w_div_round_small(r, a, b, n, mode, s, &ok)) return ok;
    if (n > 4u && cint_wn_in128(a, b, n, s)) {
        (void)cint_w_div_round_small(t, a, b, 4u, mode, s, &ok);
        if (ok) cint_wn_extend4(r, t, n, s);
        return ok;
    }
#endif
    return cint_wn_div_round(r, a, b, n, mode, s);
}

/* ------------------------------------------------------------------------- */
/* 4. The helpers of each width.                                              */
/*                                                                            */
/* A failed check passes copies of its operands to cint_rt_wide_fault, made   */
/* where it fails: no operand's address leaves the path that does not fault,  */
/* so the compiler can keep the operands of an inlined helper in registers    */
/* there (rt/OPEN.md RT-OQ-62). The record is the same.                       */

#define CINT_W_BODY(T, N, TAG, S)                                                                                  \
    static inline bool cint_add_##T(cint_ctx *ctx, cint_site site, const cint_##T *a, const cint_##T *b,           \
                                    cint_##T *out)                                                                 \
    {                                                                                                              \
        cint_##T r;                                                                                                \
        CINT_W_IN((TAG), a->limb, b->limb, NULL);                                                                  \
        bool over = cint_w_add_ovf(r.limb, a->limb, b->limb, (N), (S));                                            \
        if (cint_rt_faulted(ctx)) return false;                                                                    \
        if (over) {                                                                                                \
            cint_##T fa = *a, fb = *b; /* copies: see CINT_W_BODY */                                               \
            (void)cint_rt_wide_fault(ctx, site, CINT_RT_OP_ADD, (TAG), 0u, 0u, fa.limb, fb.limb, NULL);            \
            return false;                                                                                          \
        }                                                                                                          \
        CINT_W_OUT((TAG), r.limb);                                                                                 \
        *out = r;                                                                                                  \
        return true;                                                                                               \
    }                                                                                                              \
    static inline bool cint_sub_##T(cint_ctx *ctx, cint_site site, const cint_##T *a, const cint_##T *b,           \
                                    cint_##T *out)                                                                 \
    {                                                                                                              \
        cint_##T r;                                                                                                \
        CINT_W_IN((TAG), a->limb, b->limb, NULL);                                                                  \
        bool over = cint_w_sub_ovf(r.limb, a->limb, b->limb, (N), (S));                                            \
        if (cint_rt_faulted(ctx)) return false;                                                                    \
        if (over) {                                                                                                \
            cint_##T fa = *a, fb = *b; /* copies: see CINT_W_BODY */                                               \
            (void)cint_rt_wide_fault(ctx, site, CINT_RT_OP_SUB, (TAG), 0u, 0u, fa.limb, fb.limb, NULL);            \
            return false;                                                                                          \
        }                                                                                                          \
        CINT_W_OUT((TAG), r.limb);                                                                                 \
        *out = r;                                                                                                  \
        return true;                                                                                               \
    }                                                                                                              \
    static inline bool cint_mul_##T(cint_ctx *ctx, cint_site site, const cint_##T *a, const cint_##T *b,           \
                                    cint_##T *out)                                                                 \
    {                                                                                                              \
        /* cint_w_mul_ovf reads both operands before it writes out, and not on overflow. */                        \
        if (cint_rt_faulted(ctx)) return false;                                                                    \
        CINT_W_IN((TAG), a->limb, b->limb, NULL);                                                                  \
        if (cint_w_mul_ovf(out->limb, a->limb, b->limb, (N), (S))) {                                               \
            cint_##T fa = *a, fb = *b; /* copies: see CINT_W_BODY */                                               \
            (void)cint_rt_wide_fault(ctx, site, CINT_RT_OP_MUL, (TAG), 0u, 0u, fa.limb, fb.limb, NULL);            \
            return false;                                                                                          \
        }                                                                                                          \
        CINT_W_OUT((TAG), out->limb);                                                                              \
        return true;                                                                                               \
    }                                                                                                              \
    static inline bool cint_neg_##T(cint_ctx *ctx, cint_site site, const cint_##T *a, cint_##T *out)               \
    {                                                                                                              \
        cint_##T r;                                                                                                \
        CINT_W_IN((TAG), a->limb, NULL, NULL);                                                                     \
        bool over = (S) ? cint_w_is_min(a->limb, (N)) : !cint_w_is_zero(a->limb, (N));                             \
        cint_w_negate(r.limb, a->limb, (N));                                                                       \
        if (cint_rt_faulted(ctx)) return false;                                                                    \
        if (over) {                                                                                                \
            cint_##T fa = *a; /* a copy: see CINT_W_BODY */                                                        \
            (void)cint_rt_wide_fault(ctx, site, CINT_RT_OP_NEG, (TAG), 0u, 0u, fa.limb, NULL, NULL);               \
            return false;                                                                                          \
        }                                                                                                          \
        CINT_W_OUT((TAG), r.limb);                                                                                 \
        *out = r;                                                                                                  \
        return true;                                                                                               \
    }                                                                                                              \
    static inline void cint_add_wrap_##T(const cint_##T *a, const cint_##T *b, cint_##T *out)                      \
    {                                                                                                              \
        cint_##T r;                                                                                                \
        (void)cint_w_addc(r.limb, a->limb, b->limb, (N));                                                          \
        *out = r;                                                                                                  \
    }                                                                                                              \
    static inline void cint_sub_wrap_##T(const cint_##T *a, const cint_##T *b, cint_##T *out)                      \
    {                                                                                                              \
        cint_##T r;                                                                                                \
        (void)cint_w_subb(r.limb, a->limb, b->limb, (N));                                                          \
        *out = r;                                                                                                  \
    }                                                                                                              \
    static inline void cint_mul_wrap_##T(const cint_##T *a, const cint_##T *b, cint_##T *out)                      \
    {                                                                                                              \
        cint_##T r;                                                                                                \
        cint_w_mul_low(r.limb, a->limb, b->limb, (N));                                                             \
        *out = r;                                                                                                  \
    }                                                                                                              \
    static inline void cint_neg_wrap_##T(const cint_##T *a, cint_##T *out)                                         \
    {                                                                                                              \
        cint_##T r;                                                                                                \
        cint_w_negate(r.limb, a->limb, (N));                                                                       \
        *out = r;                                                                                                  \
    }                                                                                                              \
    /* Saturation: a signed sum or difference lies on a's side, a product on */                                    \
    /* the side of the operands' signs (cint_rt.h CINT_RT_SAT_SIGNED); an */                                       \
    /* unsigned difference below 0, a sum or product above the maximum. */                                         \
    static inline void cint_add_sat_##T(const cint_##T *a, const cint_##T *b, cint_##T *out)                       \
    {                                                                                                              \
        cint_##T r;                                                                                                \
        if (cint_w_add_ovf(r.limb, a->limb, b->limb, (N), (S)))                                                    \
            cint_w_bound(r.limb, (N), (S) && cint_w_negative(a->limb, (N)), (S));                                  \
        *out = r;                                                                                                  \
    }                                                                                                              \
    static inline void cint_sub_sat_##T(const cint_##T *a, const cint_##T *b, cint_##T *out)                       \
    {                                                                                                              \
        cint_##T r;                                                                                                \
        if (cint_w_sub_ovf(r.limb, a->limb, b->limb, (N), (S)))                                                    \
            cint_w_bound(r.limb, (N), !(S) || cint_w_negative(a->limb, (N)), (S));                                 \
        *out = r;                                                                                                  \
    }                                                                                                              \
    static inline void cint_mul_sat_##T(const cint_##T *a, const cint_##T *b, cint_##T *out)                       \
    {                                                                                                              \
        cint_##T r;                                                                                                \
        if (cint_w_mul_ovf(r.limb, a->limb, b->limb, (N), (S)))                                                    \
            cint_w_bound(r.limb, (N), (S) && cint_w_negative(a->limb, (N)) != cint_w_negative(b->limb, (N)), (S)); \
        *out = r;                                                                                                  \
    }                                                                                                              \
    static inline uint16_t cint_add_result_##T(const cint_##T *a, const cint_##T *b, cint_##T *out)                \
    {                                                                                                              \
        cint_##T r;                                                                                                \
        if (cint_w_add_ovf(r.limb, a->limb, b->limb, (N), (S))) return CINT_ARITH_OVERFLOW;                        \
        *out = r;                                                                                                  \
        return 0u;                                                                                                 \
    }                                                                                                              \
    static inline uint16_t cint_sub_result_##T(const cint_##T *a, const cint_##T *b, cint_##T *out)                \
    {                                                                                                              \
        cint_##T r;                                                                                                \
        if (cint_w_sub_ovf(r.limb, a->limb, b->limb, (N), (S))) return CINT_ARITH_OVERFLOW;                        \
        *out = r;                                                                                                  \
        return 0u;                                                                                                 \
    }                                                                                                              \
    static inline uint16_t cint_mul_result_##T(const cint_##T *a, const cint_##T *b, cint_##T *out)                \
    {                                                                                                              \
        cint_##T r;                                                                                                \
        if (cint_w_mul_ovf(r.limb, a->limb, b->limb, (N), (S))) return CINT_ARITH_OVERFLOW;                        \
        *out = r;                                                                                                  \
        return 0u;                                                                                                 \
    }                                                                                                              \
    static inline void cint_and_##T(const cint_##T *a, const cint_##T *b, cint_##T *out)                           \
    {                                                                                                              \
        unsigned i;                                                                                                \
        for (i = 0u; i < (N); i++) out->limb[i] = a->limb[i] & b->limb[i];                                         \
    }                                                                                                              \
    static inline void cint_or_##T(const cint_##T *a, const cint_##T *b, cint_##T *out)                            \
    {                                                                                                              \
        unsigned i;                                                                                                \
        for (i = 0u; i < (N); i++) out->limb[i] = a->limb[i] | b->limb[i];                                         \
    }                                                                                                              \
    static inline void cint_xor_##T(const cint_##T *a, const cint_##T *b, cint_##T *out)                           \
    {                                                                                                              \
        unsigned i;                                                                                                \
        for (i = 0u; i < (N); i++) out->limb[i] = a->limb[i] ^ b->limb[i];                                         \
    }                                                                                                              \
    static inline void cint_not_##T(const cint_##T *a, cint_##T *out)                                              \
    {                                                                                                              \
        unsigned i;                                                                                                \
        for (i = 0u; i < (N); i++) out->limb[i] = ~a->limb[i];                                                     \
    }                                                                                                              \
    static inline bool cint_eq_##T(const cint_##T *a, const cint_##T *b)                                           \
    {                                                                                                              \
        return cint_w_eq(a->limb, b->limb, (N));                                                                   \
    }                                                                                                              \
    static inline bool cint_ne_##T(const cint_##T *a, const cint_##T *b)                                           \
    {                                                                                                              \
        return !cint_w_eq(a->limb, b->limb, (N));                                                                  \
    }                                                                                                              \
    static inline bool cint_lt_##T(const cint_##T *a, const cint_##T *b)                                           \
    {                                                                                                              \
        return cint_w_cmp(a->limb, b->limb, (N), (S)) < 0;                                                         \
    }                                                                                                              \
    static inline bool cint_le_##T(const cint_##T *a, const cint_##T *b)                                           \
    {                                                                                                              \
        return cint_w_cmp(a->limb, b->limb, (N), (S)) <= 0;                                                        \
    }                                                                                                              \
    static inline bool cint_gt_##T(const cint_##T *a, const cint_##T *b)                                           \
    {                                                                                                              \
        return cint_w_cmp(a->limb, b->limb, (N), (S)) > 0;                                                         \
    }                                                                                                              \
    static inline bool cint_ge_##T(const cint_##T *a, const cint_##T *b)                                           \
    {                                                                                                              \
        return cint_w_cmp(a->limb, b->limb, (N), (S)) >= 0;                                                        \
    }                                                                                                              \
    static inline void cint_min_##T(const cint_##T *a, const cint_##T *b, cint_##T *out)                           \
    {                                                                                                              \
        *out = cint_w_cmp(a->limb, b->limb, (N), (S)) <= 0 ? *a : *b;                                              \
    }                                                                                                              \
    static inline void cint_max_##T(const cint_##T *a, const cint_##T *b, cint_##T *out)                           \
    {                                                                                                              \
        *out = cint_w_cmp(a->limb, b->limb, (N), (S)) >= 0 ? *a : *b;                                              \
    }

/* A shift count of n limbs (RT-OQ-52): the pattern is the count when it lies in
 * 0 to 2^64 - 1 and all ones otherwise, so the test bits <= w - 1 of every
 * shift stays exact; a negative count has a nonzero top limb. */
static inline cint_count cint_w_count(const uint64_t *k, unsigned n, uint32_t tag)
{
    cint_count c;
    c.bits = cint_w_is_zero(k + 1, n - 1u) ? k[0] : UINT64_MAX;
    c.tag = tag;
    c.reserved = 0u;
    c.limbs = k;
    return c;
}

/* What cint_rt_wide_fault reads for a count: a wide count's limbs, or the pattern. */
static inline const uint64_t *cint_w_count_at(const cint_count *k)
{
    return cint_wide_limbs(k->tag) != 0u ? k->limbs : &k->bits;
}

#define CINT_W_SHIFT(T, N, TAG, S)                                                                         \
    static inline cint_count cint_count_##T(const cint_##T *k)                                             \
    {                                                                                                      \
        return cint_w_count(k->limb, (N), (TAG));                                                          \
    }                                                                                                      \
    static inline bool cint_shl_##T(cint_ctx *ctx, cint_site site, const cint_##T *a, cint_count k,        \
                                    cint_##T *out)                                                         \
    {                                                                                                      \
        cint_##T r = {{0u}}, back;                                                                         \
        CINT_W_IN((TAG), a->limb, NULL, NULL);                                                             \
        bool bad = k.bits > (uint64_t)(64u * (N) - 1u), over = false;                                      \
        if (!bad) {                                                                                        \
            cint_w_shl_raw(r.limb, a->limb, (unsigned)k.bits, (N));                                        \
            cint_w_shr_raw(back.limb, r.limb, (unsigned)k.bits, (N), (S));                                 \
            over = cint_w_ucmp(back.limb, a->limb, (N)) != 0;                                              \
        }                                                                                                  \
        if (cint_rt_faulted(ctx)) return false;                                                            \
        if (bad || over) {                                                                                 \
            (void)cint_rt_wide_fault(ctx, site, CINT_RT_OP_SHL, (TAG), 0u, k.tag, a->limb,                 \
                                     cint_w_count_at(&k), NULL);                                           \
            return false;                                                                                  \
        }                                                                                                  \
        CINT_W_OUT((TAG), r.limb);                                                                         \
        *out = r;                                                                                          \
        return true;                                                                                       \
    }                                                                                                      \
    static inline bool cint_shl_wrap_##T(cint_ctx *ctx, cint_site site, const cint_##T *a, cint_count k,   \
                                         cint_##T *out)                                                    \
    {                                                                                                      \
        cint_##T r = {{0u}};                                                                               \
        CINT_W_IN((TAG), a->limb, NULL, NULL);                                                             \
        bool bad = k.bits > (uint64_t)(64u * (N) - 1u);                                                    \
        if (!bad) cint_w_shl_raw(r.limb, a->limb, (unsigned)k.bits, (N));                                  \
        if (cint_rt_faulted(ctx)) return false;                                                            \
        if (bad) {                                                                                         \
            (void)cint_rt_wide_fault(ctx, site, CINT_RT_OP_SHL_WRAP, (TAG), 0u, k.tag, a->limb,            \
                                     cint_w_count_at(&k), NULL);                                           \
            return false;                                                                                  \
        }                                                                                                  \
        CINT_W_OUT((TAG), r.limb);                                                                         \
        *out = r;                                                                                          \
        return true;                                                                                       \
    }                                                                                                      \
    static inline bool cint_shr_##T(cint_ctx *ctx, cint_site site, const cint_##T *a, cint_count k,        \
                                    cint_##T *out)                                                         \
    {                                                                                                      \
        cint_##T r = {{0u}};                                                                               \
        CINT_W_IN((TAG), a->limb, NULL, NULL);                                                             \
        bool bad = k.bits > (uint64_t)(64u * (N) - 1u);                                                    \
        if (!bad) cint_w_shr_raw(r.limb, a->limb, (unsigned)k.bits, (N), (S));                             \
        if (cint_rt_faulted(ctx)) return false;                                                            \
        if (bad) {                                                                                         \
            (void)cint_rt_wide_fault(ctx, site, CINT_RT_OP_SHR, (TAG), 0u, k.tag, a->limb,                 \
                                     cint_w_count_at(&k), NULL);                                           \
            return false;                                                                                  \
        }                                                                                                  \
        CINT_W_OUT((TAG), r.limb);                                                                         \
        *out = r;                                                                                          \
        return true;                                                                                       \
    }                                                                                                      \
    static inline uint16_t cint_shl_result_##T(const cint_##T *a, cint_count k, cint_##T *out)             \
    {                                                                                                      \
        cint_##T r, back;                                                                                  \
        if (k.bits > (uint64_t)(64u * (N) - 1u)) return CINT_ARITH_SHIFT;                                  \
        cint_w_shl_raw(r.limb, a->limb, (unsigned)k.bits, (N));                                            \
        cint_w_shr_raw(back.limb, r.limb, (unsigned)k.bits, (N), (S));                                     \
        if (cint_w_ucmp(back.limb, a->limb, (N)) != 0) return CINT_ARITH_OVERFLOW;                         \
        *out = r;                                                                                          \
        return 0u;                                                                                         \
    }

/* Floor division, its result forms, and the built-ins of one wide type. A    */
/* division by zero and the pair (MIN, -1) are decided before any limb step. */
#define CINT_W_DIVIDE(T, N, TAG, S)                                                                                     \
    static inline bool cint_div_##T(cint_ctx *ctx, cint_site site, const cint_##T *a, const cint_##T *b,                \
                                    cint_##T *out)                                                                      \
    {                                                                                                                   \
        cint_##T q = {{0u}}, r;                                                                                         \
        CINT_W_IN((TAG), a->limb, b->limb, NULL);                                                                       \
        bool bad = cint_w_is_zero(b->limb, (N)) ||                                                                      \
                   cint_w_div_overflows(a->limb, b->limb, (N), (S));                                                    \
        if (!bad) cint_w_divide(q.limb, r.limb, a->limb, b->limb, (N), 0u, (S));                                        \
        if (cint_rt_faulted(ctx)) return false;                                                                         \
        if (bad) {                                                                                                      \
            cint_##T fa = *a, fb = *b; /* copies: see CINT_W_BODY */                                                    \
            (void)cint_rt_wide_fault(ctx, site, CINT_RT_OP_DIV, (TAG), 0u, 0u, fa.limb, fb.limb, NULL);                 \
            return false;                                                                                               \
        }                                                                                                               \
        CINT_W_OUT((TAG), q.limb);                                                                                      \
        *out = q;                                                                                                       \
        return true;                                                                                                    \
    }                                                                                                                   \
    static inline bool cint_rem_##T(cint_ctx *ctx, cint_site site, const cint_##T *a, const cint_##T *b,                \
                                    cint_##T *out)                                                                      \
    {                                                                                                                   \
        cint_##T q, r = {{0u}};                                                                                         \
        CINT_W_IN((TAG), a->limb, b->limb, NULL);                                                                       \
        bool bad = cint_w_is_zero(b->limb, (N));                                                                        \
        if (!bad) cint_w_divide(q.limb, r.limb, a->limb, b->limb, (N), 0u, (S));                                        \
        if (cint_rt_faulted(ctx)) return false;                                                                         \
        if (bad) {                                                                                                      \
            cint_##T fa = *a, fb = *b; /* copies: see CINT_W_BODY */                                                    \
            (void)cint_rt_wide_fault(ctx, site, CINT_RT_OP_REM, (TAG), 0u, 0u, fa.limb, fb.limb, NULL);                 \
            return false;                                                                                               \
        }                                                                                                               \
        CINT_W_OUT((TAG), r.limb);                                                                                      \
        *out = r;                                                                                                       \
        return true;                                                                                                    \
    }                                                                                                                   \
    static inline uint16_t cint_div_result_##T(const cint_##T *a, const cint_##T *b, cint_##T *out)                     \
    {                                                                                                                   \
        cint_##T q, r;                                                                                                  \
        if (cint_w_is_zero(b->limb, (N))) return CINT_ARITH_DIV_ZERO;                                                   \
        if (cint_w_div_overflows(a->limb, b->limb, (N), (S))) return CINT_ARITH_OVERFLOW;                               \
        cint_w_divide(q.limb, r.limb, a->limb, b->limb, (N), 0u, (S));                                                  \
        *out = q;                                                                                                       \
        return 0u;                                                                                                      \
    }                                                                                                                   \
    static inline uint16_t cint_rem_result_##T(const cint_##T *a, const cint_##T *b, cint_##T *out)                     \
    {                                                                                                                   \
        cint_##T q, r;                                                                                                  \
        if (cint_w_is_zero(b->limb, (N))) return CINT_ARITH_DIV_ZERO;                                                   \
        cint_w_divide(q.limb, r.limb, a->limb, b->limb, (N), 0u, (S));                                                  \
        *out = r;                                                                                                       \
        return 0u;                                                                                                      \
    }                                                                                                                   \
    static inline bool cint_builtin_##T(cint_ctx *ctx, cint_site site, uint32_t op, uint32_t mode,                      \
                                        const cint_##T *a, const cint_##T *b, const cint_##T *c,                        \
                                        cint_##T *out)                                                                  \
    {                                                                                                                   \
        cint_##T r = {{0u}}, s = {{0u}};                                                                                \
        bool ok = false;                                                                                                \
        CINT_W_IN((TAG), a->limb, b != NULL ? b->limb : NULL, c != NULL ? c->limb : NULL);                              \
        /* Emitted code passes NULL only for an operand the operation does not take. Testing b and c                    \
         * before reading them keeps gcc from reading through a NULL that it propagates into a clone                    \
         * of this function (rt/OPEN.md RT-OQ-60); a call missing one is refused by the fault path. */                  \
        if (op == 10u && b != NULL) {                                                                                   \
            /* cint_w_div_round reads both operands before it writes out, and not on a fault. */                        \
            if (cint_rt_faulted(ctx)) return false;                                                                     \
            if (cint_w_div_round(out->limb, a->limb, b->limb, (N), mode, (S))) {                                        \
                CINT_W_OUT((TAG), out->limb);                                                                           \
                return true;                                                                                            \
            }                                                                                                           \
            cint_##T fa = *a, fb = *b; /* copies: see CINT_W_BODY */                                                    \
            (void)cint_rt_wide_fault(ctx, site, CINT_W_BUILTIN + op, (TAG), mode, 0u, fa.limb, fb.limb,                 \
                                     NULL);                                                                             \
            return false;                                                                                               \
        }                                                                                                               \
        if (op == 1u) {                                                                                                 \
            ok = !(S) || !cint_w_is_min(a->limb, (N));                                                                  \
            (void)cint_w_magnitude(r.limb, a->limb, (N), (S));                                                          \
        } else if (op == 5u && b != NULL && c != NULL) {                                                                \
            ok = cint_w_cmp(b->limb, c->limb, (N), (S)) <= 0;                                                           \
            r = cint_w_cmp(a->limb, b->limb, (N), (S)) < 0 ? *b : cint_w_cmp(a->limb, c->limb, (N), (S)) > 0 ? *c : *a; \
        } else if (op >= 6u && op <= 9u && b != NULL) {                                                                 \
            bool rem = op == 7u || op == 9u;                                                                            \
            ok = !cint_w_is_zero(b->limb, (N)) &&                                                                       \
                 (rem || !cint_w_div_overflows(a->limb, b->limb, (N), (S)));                                            \
            if (ok) {                                                                                                   \
                cint_w_divide(s.limb, r.limb, a->limb, b->limb, (N), op <= 7u ? 1u : 2u, (S));                          \
                if (!rem) r = s;                                                                                        \
            }                                                                                                           \
        } else if (op == 13u || op == 14u) {                                                                            \
            ok = !(S) || !cint_w_negative(a->limb, (N));                                                                \
            if (ok) {                                                                                                   \
                cint_rt_wide_isqrt(r.limb, a->limb, (N));                                                               \
                if (op == 14u && mode >= 2u && mode != 3u) {                                                            \
                    uint64_t sq[32];                                                                                    \
                    cint_w_umul(sq, r.limb, r.limb, (N));                                                               \
                    if (cint_w_ucmp(sq, a->limb, (N)) != 0) {                                                           \
                        bool up = mode == 2u || mode == 4u;                                                             \
                        if (!up) {                                                                                      \
                            (void)cint_w_addc(sq, sq, r.limb, (N));                                                     \
                            up = cint_w_ucmp(a->limb, sq, (N)) > 0;                                                     \
                        }                                                                                               \
                        if (up) {                                                                                       \
                            unsigned i = 0u;                                                                            \
                            while (++r.limb[i] == 0u && i + 1u < (N)) i++;                                              \
                        }                                                                                               \
                    }                                                                                                   \
                }                                                                                                       \
            }                                                                                                           \
        }                                                                                                               \
        if (cint_rt_faulted(ctx)) return false;                                                                         \
        if (!ok) {                                                                                                      \
            cint_##T fa = *a, fb = b != NULL ? *b : *a, fc = c != NULL ? *c : *a; /* see CINT_W_BODY */                 \
            (void)cint_rt_wide_fault(ctx, site, CINT_W_BUILTIN + op, (TAG), mode, 0u, fa.limb,                          \
                                     b != NULL ? fb.limb : NULL, c != NULL ? fc.limb : NULL);                           \
            return false;                                                                                               \
        }                                                                                                               \
        CINT_W_OUT((TAG), r.limb);                                                                                      \
        *out = r;                                                                                                       \
        return true;                                                                                                    \
    }                                                                                                                   \
    static inline bool cint_muldiv_##T(cint_ctx *ctx, cint_site site, uint32_t rtag, uint32_t mode,                     \
                                       const cint_##T *a, const cint_##T *b, const cint_##T *c, void *out)              \
    {                                                                                                                   \
        bool ok;                                                                                                        \
        CINT_W_IN((TAG), a->limb, b->limb, c->limb);                                                                    \
        ok = cint_rt_wide_muldiv(ctx, site, (TAG), rtag, mode, a->limb, b->limb, c->limb, out);                         \
        if (ok) CINT_W_OUT(rtag, (const uint64_t *)out);                                                                \
        return ok;                                                                                                      \
    }

/* Wide to wide conversions (an extension, or a checked narrowing). */
#define CINT_W_CONV_WW(D, ND, DTAG, DS, SRC, NS, STAG, SS)                                           \
    static inline bool cint_as_##D##_from_##SRC(cint_ctx *ctx, cint_site site, const cint_##SRC *x,  \
                                                cint_##D *out)                                       \
    {                                                                                                \
        cint_##D r;                                                                                  \
        CINT_W_IN((STAG), x->limb, NULL, NULL);                                                      \
        bool fits = cint_w_fits_into(x->limb, (NS), (SS), (ND), (DS));                               \
        cint_w_resize(r.limb, (ND), x->limb, (NS), (SS));                                            \
        if (cint_rt_faulted(ctx)) return false;                                                      \
        if (!fits) {                                                                                 \
            cint_##SRC fx = *x; /* a copy: see CINT_W_BODY */                                        \
            (void)cint_rt_wide_fault(ctx, site, CINT_W_AS, (STAG), 0u, (DTAG), fx.limb, NULL, NULL); \
            return false;                                                                            \
        }                                                                                            \
        CINT_W_OUT((DTAG), r.limb);                                                                  \
        *out = r;                                                                                    \
        return true;                                                                                 \
    }                                                                                                \
    static inline void cint_as_wrap_##D##_from_##SRC(const cint_##SRC *x, cint_##D *out)             \
    {                                                                                                \
        cint_##D r;                                                                                  \
        cint_w_resize(r.limb, (ND), x->limb, (NS), (SS));                                            \
        *out = r;                                                                                    \
    }                                                                                                \
    static inline void cint_as_sat_##D##_from_##SRC(const cint_##SRC *x, cint_##D *out)              \
    {                                                                                                \
        cint_##D r;                                                                                  \
        if (cint_w_fits_into(x->limb, (NS), (SS), (ND), (DS))) {                                     \
            cint_w_resize(r.limb, (ND), x->limb, (NS), (SS));                                        \
        } else {                                                                                     \
            cint_w_bound(r.limb, (ND), (SS) && cint_w_negative(x->limb, (NS)), (DS));                \
        }                                                                                            \
        *out = r;                                                                                    \
    }

/* A narrow type to a wide one faults only for a negative value into U128 to */
/* U512, which saturates to 0; a wide type to a narrow one is checked against */
/* the narrow range, wraps to its low bits, or saturates to its bound. DS and */
/* SK are constants, so the test is held in bad or neg, not written as a      */
/* condition (MSVC C4127).                                                    */
#define CINT_W_CONV_NW(D, ND, DTAG, DS, SRC, CS, STAG, SK)                                          \
    static inline bool cint_as_##D##_from_##SRC(cint_ctx *ctx, cint_site site, CS x, cint_##D *out) \
    {                                                                                               \
        uint64_t bits = CINT_RT_BITS_##SK(x);                                                       \
        bool bad = !(DS) && CINT_RT_SIGNED_##SK && (bits >> 63) != 0u;                              \
        CINT_W_IN((DTAG), NULL, NULL, NULL);                                                        \
        if (cint_rt_faulted(ctx)) return false;                                                     \
        if (bad) {                                                                                  \
            (void)cint_rt_wide_fault(ctx, site, CINT_W_AS, (STAG), 0u, (DTAG), &bits, NULL, NULL);  \
            return false;                                                                           \
        }                                                                                           \
        cint_w_from_narrow(out->limb, (ND), (STAG), bits);                                          \
        CINT_W_OUT((DTAG), out->limb);                                                              \
        return true;                                                                                \
    }                                                                                               \
    static inline void cint_as_wrap_##D##_from_##SRC(CS x, cint_##D *out)                           \
    {                                                                                               \
        cint_w_from_narrow(out->limb, (ND), (STAG), CINT_RT_BITS_##SK(x));                          \
    }                                                                                               \
    static inline void cint_as_sat_##D##_from_##SRC(CS x, cint_##D *out)                            \
    {                                                                                               \
        uint64_t bits = CINT_RT_BITS_##SK(x);                                                       \
        bool neg = !(DS) && CINT_RT_SIGNED_##SK && (bits >> 63) != 0u;                              \
        cint_w_from_narrow(out->limb, (ND), (STAG), neg ? 0u : bits);                               \
    }
#define CINT_W_CONV_WN(D, CD, DTAG, DK, DW, SRC, NS, STAG, SS)                                               \
    static inline bool cint_as_##D##_from_##SRC(cint_ctx *ctx, cint_site site, const cint_##SRC *x, CD *out) \
    {                                                                                                        \
        bool fits = cint_w_fits_narrow(x->limb, (NS), (SS), (DTAG));                                         \
        CINT_W_IN((STAG), x->limb, NULL, NULL);                                                              \
        if (cint_rt_faulted(ctx)) return false;                                                              \
        if (!fits) {                                                                                         \
            cint_##SRC fx = *x; /* a copy: see CINT_W_BODY */                                                \
            (void)cint_rt_wide_fault(ctx, site, CINT_W_AS, (STAG), 0u, (DTAG), fx.limb, NULL, NULL);         \
            return false;                                                                                    \
        }                                                                                                    \
        *out = (CD)CINT_RT_NARROW_##DK(x->limb[0], (DW));                                                    \
        return true;                                                                                         \
    }                                                                                                        \
    static inline CD cint_as_wrap_##D##_from_##SRC(const cint_##SRC *x)                                      \
    {                                                                                                        \
        return (CD)CINT_RT_NARROW_##DK(x->limb[0], (DW));                                                    \
    }                                                                                                        \
    static inline CD cint_as_sat_##D##_from_##SRC(const cint_##SRC *x)                                       \
    {                                                                                                        \
        bool negative = (SS) && cint_w_negative(x->limb, (NS));                                              \
        bool fits = cint_w_fits_narrow(x->limb, (NS), (SS), (DTAG));                                         \
        uint64_t bits = fits ? x->limb[0] : cint_rt_bound_bits((DTAG), negative);                            \
        return (CD)CINT_RT_NARROW_##DK(bits, (DW));                                                          \
    }

#define CINT_W_CONV_NARROW(T, N, TAG, SG)                             \
    CINT_W_CONV_NW(T, N, TAG, SG, i8, int8_t, CINT_TAG_I8, S)          \
    CINT_W_CONV_NW(T, N, TAG, SG, i16, int16_t, CINT_TAG_I16, S)       \
    CINT_W_CONV_NW(T, N, TAG, SG, i32, int32_t, CINT_TAG_I32, S)       \
    CINT_W_CONV_NW(T, N, TAG, SG, i64, int64_t, CINT_TAG_I64, S)       \
    CINT_W_CONV_NW(T, N, TAG, SG, u8, uint8_t, CINT_TAG_U8, U)         \
    CINT_W_CONV_NW(T, N, TAG, SG, u16, uint16_t, CINT_TAG_U16, U)      \
    CINT_W_CONV_NW(T, N, TAG, SG, u32, uint32_t, CINT_TAG_U32, U)      \
    CINT_W_CONV_NW(T, N, TAG, SG, u64, uint64_t, CINT_TAG_U64, U)      \
    CINT_W_CONV_WN(i8, int8_t, CINT_TAG_I8, S, 8u, T, N, TAG, SG)      \
    CINT_W_CONV_WN(i16, int16_t, CINT_TAG_I16, S, 16u, T, N, TAG, SG)  \
    CINT_W_CONV_WN(i32, int32_t, CINT_TAG_I32, S, 32u, T, N, TAG, SG)  \
    CINT_W_CONV_WN(i64, int64_t, CINT_TAG_I64, S, 64u, T, N, TAG, SG)  \
    CINT_W_CONV_WN(u8, uint8_t, CINT_TAG_U8, U, 8u, T, N, TAG, SG)     \
    CINT_W_CONV_WN(u16, uint16_t, CINT_TAG_U16, U, 16u, T, N, TAG, SG) \
    CINT_W_CONV_WN(u32, uint32_t, CINT_TAG_U32, U, 32u, T, N, TAG, SG) \
    CINT_W_CONV_WN(u64, uint64_t, CINT_TAG_U64, U, 64u, T, N, TAG, SG)

#define CINT_W_CONV_FROM(D, ND, DTAG, DS)                          \
    CINT_W_CONV_WW(D, ND, DTAG, DS, i128, 2u, CINT_TAG_I128, 1)    \
    CINT_W_CONV_WW(D, ND, DTAG, DS, i256, 4u, CINT_TAG_I256, 1)    \
    CINT_W_CONV_WW(D, ND, DTAG, DS, i512, 8u, CINT_TAG_I512, 1)    \
    CINT_W_CONV_WW(D, ND, DTAG, DS, i1024, 16u, CINT_TAG_I1024, 1) \
    CINT_W_CONV_WW(D, ND, DTAG, DS, u128, 2u, CINT_TAG_U128, 0)    \
    CINT_W_CONV_WW(D, ND, DTAG, DS, u256, 4u, CINT_TAG_U256, 0)    \
    CINT_W_CONV_WW(D, ND, DTAG, DS, u512, 8u, CINT_TAG_U512, 0)

/* Rotations of U128 to U512 (built-ins 15 and 16; SPEC-01 IM-16): a count */
/* outside 0 to w - 1 faults E_SHIFT, as for a shift.                         */
#define CINT_W_ROTATE(T, N, TAG)                                                                              \
    static inline bool cint_rotl_##T(cint_ctx *ctx, cint_site site, const cint_##T *a, cint_count k,          \
                                     cint_##T *out)                                                           \
    {                                                                                                         \
        cint_##T r = {{0u}};                                                                                  \
        CINT_W_IN((TAG), a->limb, NULL, NULL);                                                                \
        bool bad = k.bits > (uint64_t)(64u * (N) - 1u);                                                       \
        if (!bad) cint_w_rotl(r.limb, a->limb, (unsigned)k.bits, (N));                                        \
        if (cint_rt_faulted(ctx)) return false;                                                               \
        if (bad) {                                                                                            \
            return cint_rt_wide_fault(ctx, site, CINT_W_ROTL, (TAG), 0u, k.tag, a->limb, cint_w_count_at(&k), \
                                      NULL);                                                                  \
        }                                                                                                     \
        CINT_W_OUT((TAG), r.limb);                                                                            \
        *out = r;                                                                                             \
        return true;                                                                                          \
    }                                                                                                         \
    static inline bool cint_rotr_##T(cint_ctx *ctx, cint_site site, const cint_##T *a, cint_count k,          \
                                     cint_##T *out)                                                           \
    {                                                                                                         \
        cint_##T r = {{0u}};                                                                                  \
        CINT_W_IN((TAG), a->limb, NULL, NULL);                                                                \
        bool bad = k.bits > (uint64_t)(64u * (N) - 1u);                                                       \
        if (!bad) cint_w_rotl(r.limb, a->limb, (unsigned)((64u * (N) - k.bits) % (64u * (N))), (N));          \
        if (cint_rt_faulted(ctx)) return false;                                                               \
        if (bad) {                                                                                            \
            return cint_rt_wide_fault(ctx, site, CINT_W_ROTR, (TAG), 0u, k.tag, a->limb, cint_w_count_at(&k), \
                                      NULL);                                                                  \
        }                                                                                                     \
        CINT_W_OUT((TAG), r.limb);                                                                            \
        *out = r;                                                                                             \
        return true;                                                                                          \
    }

#define CINT_W_TYPE(T, N, TAG, S)    \
    CINT_W_BODY(T, N, TAG, S)        \
    CINT_W_SHIFT(T, N, TAG, S)       \
    CINT_W_DIVIDE(T, N, TAG, S)      \
    CINT_W_CONV_NARROW(T, N, TAG, S)

CINT_W_TYPE(i128, 2u, CINT_TAG_I128, 1)
CINT_W_TYPE(i256, 4u, CINT_TAG_I256, 1)
CINT_W_TYPE(i512, 8u, CINT_TAG_I512, 1)
CINT_W_TYPE(i1024, 16u, CINT_TAG_I1024, 1)
CINT_W_TYPE(u128, 2u, CINT_TAG_U128, 0)
CINT_W_TYPE(u256, 4u, CINT_TAG_U256, 0)
CINT_W_TYPE(u512, 8u, CINT_TAG_U512, 0)
CINT_W_CONV_FROM(i128, 2u, CINT_TAG_I128, 1)
CINT_W_CONV_FROM(i256, 4u, CINT_TAG_I256, 1)
CINT_W_CONV_FROM(i512, 8u, CINT_TAG_I512, 1)
CINT_W_CONV_FROM(i1024, 16u, CINT_TAG_I1024, 1)
CINT_W_CONV_FROM(u128, 2u, CINT_TAG_U128, 0)
CINT_W_CONV_FROM(u256, 4u, CINT_TAG_U256, 0)
CINT_W_CONV_FROM(u512, 8u, CINT_TAG_U512, 0)
CINT_W_ROTATE(u128, 2u, CINT_TAG_U128)
CINT_W_ROTATE(u256, 4u, CINT_TAG_U256)
CINT_W_ROTATE(u512, 8u, CINT_TAG_U512)

/* mul_full.checked.<t>.<t2>: the exact product in the type of twice the width
 * (SPEC-01 IM-16; I1024 and U512 have none). */
static inline void cint_mul_full_i64(int64_t a, int64_t b, cint_i128 *out)
{
    uint64_t ma = a < 0 ? (uint64_t)0 - (uint64_t)a : (uint64_t)a;
    uint64_t mb = b < 0 ? (uint64_t)0 - (uint64_t)b : (uint64_t)b;
    cint_i128 r;
    r.limb[0] = cint_w_mul64(ma, mb, &r.limb[1]);
    if ((a < 0) != (b < 0)) cint_w_negate(r.limb, r.limb, 2u);
    *out = r;
}

static inline void cint_mul_full_u64(uint64_t a, uint64_t b, cint_u128 *out)
{
    cint_u128 r;
    r.limb[0] = cint_w_mul64(a, b, &r.limb[1]);
    *out = r;
}

#define CINT_W_MUL_FULL(T, N, T2, S)                                                                        \
    static inline void cint_mul_full_##T(const cint_##T *a, const cint_##T *b, cint_##T2 *out)              \
    {                                                                                                       \
        uint64_t ma[16], mb[16];                                                                            \
        cint_##T2 r;                                                                                        \
        bool negative = cint_w_magnitude(ma, a->limb, (N), (S)) != cint_w_magnitude(mb, b->limb, (N), (S)); \
        cint_w_umul(r.limb, ma, mb, (N));                                                                   \
        if (negative) cint_w_negate(r.limb, r.limb, 2u * (N));                                              \
        *out = r;                                                                                           \
    }
CINT_W_MUL_FULL(i128, 2u, i256, 1)
CINT_W_MUL_FULL(i256, 4u, i512, 1)
CINT_W_MUL_FULL(i512, 8u, i1024, 1)
CINT_W_MUL_FULL(u128, 2u, u256, 0)
CINT_W_MUL_FULL(u256, 4u, u512, 0)

/* uabs.checked.<t>: |a| in the unsigned type of the width, which holds it (SPEC-01 IM-16). */
#define CINT_W_UABS(T, N, U)                                           \
    static inline void cint_uabs_##T(const cint_##T *a, cint_##U *out) \
    {                                                                  \
        cint_##U r;                                                    \
        (void)cint_w_magnitude(r.limb, a->limb, (N), true);            \
        *out = r;                                                      \
    }
CINT_W_UABS(i128, 2u, u128)
CINT_W_UABS(i256, 4u, u256)
CINT_W_UABS(i512, 8u, u512)

/* Canonical bytes (IM-14): limb 0 first, each limb little-endian. */
static inline void cint_wide_store(uint8_t *dst, const uint64_t *limb, unsigned n)
{
    unsigned i;
    for (i = 0u; i < 8u * n; i++) dst[i] = (uint8_t)(limb[i / 8u] >> (8u * (i % 8u)));
}

static inline void cint_wide_load(uint64_t *limb, const uint8_t *src, unsigned n)
{
    unsigned i;
    for (i = 0u; i < n; i++) limb[i] = 0u;
    for (i = 0u; i < 8u * n; i++) limb[i / 8u] |= (uint64_t)src[i] << (8u * (i % 8u));
}

#if defined(CINT_RT_LIBRARY) && !defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
#pragma GCC visibility pop
#endif

#ifdef __cplusplus
}
#endif

#endif /* CINT_WIDE_H */
