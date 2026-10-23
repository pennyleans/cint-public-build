/* test_wide_calls.c: cint_builtin_<t> of rt/cint_wide.h called as emitted code calls it, one call
 * per function with NULL for each operand the built-in does not take (compiler/back_c.ci), in a
 * translation unit with no other call of it (rt/OPEN.md RT-OQ-60).
 *
 * gcc 13.3 at -O2 gives cint_builtin_<t> a clone for an argument that every call of a unit passes
 * alike. In that clone c is NULL, or b and c are, and it still holds the operations that read
 * them, so gcc refused the unit with -Werror=array-bounds until those reads tested the pointer.
 * The I128 calls are those of a program that calls abs, div_round, div_trunc and rem_euclid and
 * never clamp, so c is always NULL; the I256 and U256 calls take a only, so b is NULL too.
 * Building this unit warning-free at -O2 with the flags of tools/cint_check.py is the test;
 * running it checks each value, and that a call missing an operand its operation takes, which
 * no emitted code makes, is refused (RT-OQ-20) with its result slot unwritten.
 *
 * test_wide.c calls test_wide_calls once, on a fresh context, and counts its failures.
 */
#include "cint_rt.h"
#include "cint_rt_host.h"
#include "cint_wide.h"

#include <stdio.h>
#include <string.h>

int test_wide_calls(cint_ctx *ctx, cint_site site);

#define WC_CALL(NAME, T, OP, MODE, B)                                                                 \
    bool NAME(cint_ctx *ctx, cint_site site, const cint_##T *a, const cint_##T *b, cint_##T *out); \
    bool NAME(cint_ctx *ctx, cint_site site, const cint_##T *a, const cint_##T *b, cint_##T *out)  \
    {                                                                                                \
        (void)b;                                                                                     \
        return cint_builtin_##T(ctx, site, (OP), (MODE), a, (B), NULL, out);                         \
    }

WC_CALL(wc_abs_i128, i128, 1u, 0u, NULL)
WC_CALL(wc_div_round_i128, i128, 10u, 5u, b)
WC_CALL(wc_div_trunc_i128, i128, 6u, 0u, b)
WC_CALL(wc_rem_euclid_i128, i128, 9u, 0u, b)
WC_CALL(wc_clamp_i128, i128, 5u, 0u, b)  /* no hi: refused */
WC_CALL(wc_abs_i256, i256, 1u, 0u, NULL)
WC_CALL(wc_isqrt_i256, i256, 13u, 0u, NULL)
WC_CALL(wc_div_trunc_i256, i256, 6u, 0u, NULL)  /* no divisor: refused */
WC_CALL(wc_isqrt_u256, u256, 13u, 0u, NULL)
WC_CALL(wc_isqrt_round_u256, u256, 14u, 5u, NULL)

static int g_calls;
static int g_failed;

#define ONES UINT64_MAX
#define SENTINEL 0xA5A5A5A5A5A5A5A5u

/* One call's outcome against the value wanted, or against a refusal when want is NULL. */
static void check(cint_ctx *ctx, const char *what, bool ok, const uint64_t *got, const uint64_t *want, unsigned n)
{
    bool pass = ok == (want != NULL);
    unsigned i;
    g_calls++;
    if (want == NULL) {
        cint_fault_record rec;
        memset(&rec, 0, sizeof rec);
        pass = pass && cint_ctx_fault(ctx, &rec) == CINT_OK && rec.code == (uint16_t)CINT_E_UNSUPPORTED &&
               rec.operation_len == 14u && memcmp(rec.operation, "dispatch.admit", 14u) == 0;
        (void)cint_ctx_clear_fault(ctx);
    }
    for (i = 0u; pass && i < n; i++) pass = got[i] == (want != NULL ? want[i] : SENTINEL);
    if (!pass) {
        g_failed++;
        fprintf(stderr, "FAIL builtin call %s\n", what);
    }
}

int test_wide_calls(cint_ctx *ctx, cint_site site)
{
    static const cint_i128 m7 = {{ONES - 6u, ONES}}, p7 = {{7u, 0u}}, two = {{2u, 0u}};
    static const cint_i128 m2_100 = {{0u, ONES << 36}}, lo = {{0u, 0u}};
    static const uint64_t abs_m7[2] = {7u, 0u}, abs_m2_100[2] = {0u, (uint64_t)1 << 36};
    static const uint64_t round_m7[2] = {ONES - 3u, ONES}, round_p7[2] = {4u, 0u};
    static const uint64_t trunc_m7[2] = {ONES - 2u, ONES}, euclid_m7[2] = {1u, 0u};
    static const cint_i256 m1 = {{ONES, ONES, ONES, ONES}}, p2_200 = {{0u, 0u, 0u, 256u}};
    static const uint64_t one4[4] = {1u, 0u, 0u, 0u}, p2_100[4] = {0u, (uint64_t)1 << 36, 0u, 0u};
    static const cint_u256 u15 = {{15u, 0u, 0u, 0u}}, u8 = {{8u, 0u, 0u, 0u}}, umax = {{ONES, ONES, ONES, ONES}};
    static const uint64_t three[4] = {3u, 0u, 0u, 0u}, root_max[4] = {ONES, ONES, 0u, 0u};
    cint_i128 r2 = {{0u}};
    cint_i256 r4 = {{0u}};
    cint_u256 u4 = {{0u}};
    g_calls = 0;
    g_failed = 0;
#define WC_RUN(CALL, R, N, WANT, WHAT)                                   \
    do {                                                                 \
        memset((R).limb, 0xA5, sizeof (R).limb);                         \
        check(ctx, (WHAT), (CALL), (R).limb, (WANT), (N));               \
    } while (0)
    WC_RUN(wc_abs_i128(ctx, site, &m7, NULL, &r2), r2, 2u, abs_m7, "abs(-7) i128");
    WC_RUN(wc_abs_i128(ctx, site, &m2_100, NULL, &r2), r2, 2u, abs_m2_100, "abs(-2^100) i128");
    WC_RUN(wc_div_round_i128(ctx, site, &m7, &two, &r2), r2, 2u, round_m7, "div_round(-7, 2, half_even) i128");
    WC_RUN(wc_div_round_i128(ctx, site, &p7, &two, &r2), r2, 2u, round_p7, "div_round(7, 2, half_even) i128");
    WC_RUN(wc_div_trunc_i128(ctx, site, &m7, &two, &r2), r2, 2u, trunc_m7, "div_trunc(-7, 2) i128");
    WC_RUN(wc_rem_euclid_i128(ctx, site, &m7, &two, &r2), r2, 2u, euclid_m7, "rem_euclid(-7, 2) i128");
    WC_RUN(wc_clamp_i128(ctx, site, &m7, &lo, &r2), r2, 2u, NULL, "clamp(-7, 0, NULL) i128");
    WC_RUN(wc_abs_i256(ctx, site, &m1, NULL, &r4), r4, 4u, one4, "abs(-1) i256");
    WC_RUN(wc_isqrt_i256(ctx, site, &p2_200, NULL, &r4), r4, 4u, p2_100, "isqrt(2^200) i256");
    WC_RUN(wc_div_trunc_i256(ctx, site, &m1, NULL, &r4), r4, 4u, NULL, "div_trunc(-1, NULL) i256");
    WC_RUN(wc_isqrt_u256(ctx, site, &u15, NULL, &u4), u4, 4u, three, "isqrt(15) u256");
    WC_RUN(wc_isqrt_u256(ctx, site, &umax, NULL, &u4), u4, 4u, root_max, "isqrt(2^256 - 1) u256");
    WC_RUN(wc_isqrt_round_u256(ctx, site, &u8, NULL, &u4), u4, 4u, three, "isqrt_round(8, half_even) u256");
#undef WC_RUN
    printf("builtin calls: %d, failed %d\n", g_calls, g_failed);
    return g_failed;
}
