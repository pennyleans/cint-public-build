/* mod_basic.c: a hand-written two-module program for harness/tests/test_harness.py.
 * It follows the observer interface cint-observe-1 (SPEC-09 CONF-13): static
 * entries listed in `cint_observer_desc`, root module first. It stands for this
 * CINT source (module path harness/basic.ci):
 *
 *   1  export I64 f() { return 42; }
 *   2  export I64 add(I64 a, I64 b) { return a + b; }
 *   3  export I8 neg8(I8 a) { return -a; }
 *   4  export Bool is_big(U64 x) { return x > 9223372036854775807; }
 *   5  export void nothing() { }
 *   6  export void spin() { while (true) { } }
 *   7  export I64 deep(I64 n) { if (n <= 0) { return 0; } return 1 + deep(n - 1); }
 *   8  export I64 mix(I8 a, U16 b, I32 c, U64 d) { return a + (b as I64) + c; }
 *   9  export U64 id_u64(U64 x) { return x; }
 *
 * and module 1, harness/lib.ci, which the harness reaches from a case list:
 *
 *   1  export I64 twice(I64 v) { return v * 2; }
 *
 * The entries call the cint_rt helpers the way generated code does. The entry
 * `seven` has no type signature, and `bad_ret` returns a pattern outside its
 * type, so that the harness's checks of the contract are exercised.
 */
#include "cint_rt.h"

#define FIXTURE_EXPORT CINT_RT_EXPORT

static const cint_site_info sites[16] = {
    {0u, 0u, ""},                 /* 0: no site */
    {1u, 1u, "call.enter"},       /* 1: entry f */
    {2u, 1u, "call.enter"},       /* 2: entry add */
    {2u, 41u, "add.checked.i64"}, /* 3: a + b */
    {3u, 1u, "call.enter"},       /* 4: entry neg8 */
    {3u, 31u, "neg.checked.i8"},  /* 5: -a */
    {4u, 1u, "call.enter"},       /* 6: entry is_big */
    {5u, 1u, "call.enter"},       /* 7: entry nothing */
    {6u, 1u, "call.enter"},       /* 8: entry spin */
    {6u, 26u, "fuel.charge"},     /* 9: loop iteration charge */
    {7u, 1u, "call.enter"},       /* 10: entry deep */
    {7u, 62u, "call.enter"},      /* 11: deep(n - 1) */
    {7u, 58u, "add.checked.i64"}, /* 12: 1 + deep(n - 1) */
    {8u, 1u, "call.enter"},       /* 13: entry mix */
    {8u, 55u, "add.checked.i64"}, /* 14: a + (b as I64) + c */
    {9u, 1u, "call.enter"},       /* 15: entry id_u64 */
};
static const cint_module module0 = {"harness/basic.ci", 16u, 16u, sites};
static const cint_site_info lib_sites[3] = {
    {0u, 0u, ""},                 /* 0: no site */
    {1u, 1u, "call.enter"},       /* 1: entry twice */
    {1u, 36u, "mul.checked.i64"}, /* 2: v * 2 */
};
static const cint_module module1 = {"harness/lib.ci", 14u, 3u, lib_sites};
static const cint_module *const modules[2] = {&module0, &module1};
static const cint_program program = {2u, 0u, modules, NULL};

static cint_site site(uint32_t index)
{
    cint_site s;
    s.module = 0u;
    s.index = index;
    return s;
}

static const uint32_t sig_f[2] = {CINT_TAG_I64, 0u};
static cint_status entry_f(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args,
                           uint64_t *result)
{
    cint_status st = cint_rt_entry_begin(ctx, site(1u), fuel, depth);
    (void)args;
    if (st != CINT_OK) {
        return st;
    }
    *result = (uint64_t)(int64_t)42;
    return cint_rt_entry_end(ctx);
}

static const uint32_t sig_add[4] = {CINT_TAG_I64, 2u, CINT_TAG_I64, CINT_TAG_I64};
static cint_status entry_add(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args,
                             uint64_t *result)
{
    int64_t a = cint_rt_sext(args[0], 64u);
    int64_t b = cint_rt_sext(args[1], 64u);
    int64_t r = 0;
    cint_status st = cint_rt_entry_begin(ctx, site(2u), fuel, depth);
    if (st != CINT_OK) {
        return st;
    }
    if (cint_add_i64(ctx, site(3u), a, b, &r)) {
        *result = (uint64_t)r;
    }
    return cint_rt_entry_end(ctx);
}

static const uint32_t sig_neg8[3] = {CINT_TAG_I8, 1u, CINT_TAG_I8};
static cint_status entry_neg8(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args,
                              uint64_t *result)
{
    int8_t a = (int8_t)cint_rt_sext(args[0], 8u);
    int8_t r = 0;
    cint_status st = cint_rt_entry_begin(ctx, site(4u), fuel, depth);
    if (st != CINT_OK) {
        return st;
    }
    if (cint_neg_i8(ctx, site(5u), a, &r)) {
        *result = (uint64_t)(int64_t)r;
    }
    return cint_rt_entry_end(ctx);
}

static const uint32_t sig_is_big[3] = {CINT_TAG_BOOL, 1u, CINT_TAG_U64};
static cint_status entry_is_big(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args,
                                uint64_t *result)
{
    uint64_t x = args[0];
    cint_status st = cint_rt_entry_begin(ctx, site(6u), fuel, depth);
    if (st != CINT_OK) {
        return st;
    }
    *result = x > (uint64_t)INT64_MAX ? 1u : 0u;
    return cint_rt_entry_end(ctx);
}

static const uint32_t sig_nothing[2] = {0u, 0u};
static cint_status entry_nothing(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args,
                                 uint64_t *result)
{
    cint_status st = cint_rt_entry_begin(ctx, site(7u), fuel, depth);
    (void)args;
    (void)result;
    if (st != CINT_OK) {
        return st;
    }
    return cint_rt_entry_end(ctx);
}

static const uint32_t sig_spin[2] = {0u, 0u};
static cint_status entry_spin(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args,
                              uint64_t *result)
{
    cint_status st = cint_rt_entry_begin(ctx, site(8u), fuel, depth);
    (void)args;
    (void)result;
    if (st != CINT_OK) {
        return st;
    }
    while (cint_fuel_charge(ctx, site(9u), 1u)) {
    }
    return cint_rt_entry_end(ctx);
}

/* The body of deep: false on a fault. A faulting call is not left, because
 * the record already holds the stack of active calls (SPEC-01 IM-107). */
static bool deep_body(cint_ctx *ctx, int64_t n, int64_t *out)
{
    int64_t inner = 0;
    if (n <= 0) {
        *out = 0;
        return true;
    }
    if (!cint_rt_call_enter(ctx, site(11u))) {
        return false;
    }
    if (!deep_body(ctx, n - 1, &inner)) {
        return false;
    }
    cint_rt_call_leave(ctx);
    return cint_add_i64(ctx, site(12u), 1, inner, out);
}

static const uint32_t sig_deep[3] = {CINT_TAG_I64, 1u, CINT_TAG_I64};
static cint_status entry_deep(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args,
                              uint64_t *result)
{
    int64_t n = cint_rt_sext(args[0], 64u);
    int64_t r = 0;
    cint_status st = cint_rt_entry_begin(ctx, site(10u), fuel, depth);
    if (st != CINT_OK) {
        return st;
    }
    if (deep_body(ctx, n, &r)) {
        *result = (uint64_t)r;
    }
    return cint_rt_entry_end(ctx);
}

static const uint32_t sig_mix[6] = {CINT_TAG_I64, 4u, CINT_TAG_I8, CINT_TAG_U16,
                                      CINT_TAG_I32, CINT_TAG_U64};
static cint_status entry_mix(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args,
                             uint64_t *result)
{
    int64_t a = cint_rt_sext(args[0], 8u);
    int64_t b = (int64_t)(args[1] & 0xffffu);
    int64_t c = cint_rt_sext(args[2], 32u);
    int64_t t = 0;
    int64_t r = 0;
    cint_status st = cint_rt_entry_begin(ctx, site(13u), fuel, depth);
    if (st != CINT_OK) {
        return st;
    }
    if (cint_add_i64(ctx, site(14u), a, b, &t) && cint_add_i64(ctx, site(14u), t, c, &r)) {
        *result = (uint64_t)r;
    }
    return cint_rt_entry_end(ctx);
}

static const uint32_t sig_id_u64[3] = {CINT_TAG_U64, 1u, CINT_TAG_U64};
static cint_status entry_id_u64(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args,
                                uint64_t *result)
{
    uint64_t x = args[0];
    cint_status st = cint_rt_entry_begin(ctx, site(15u), fuel, depth);
    if (st != CINT_OK) {
        return st;
    }
    *result = x;
    return cint_rt_entry_end(ctx);
}

/* An entry without a type signature: the harness must not call it. */
static cint_status entry_seven(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args,
                               uint64_t *result)
{
    cint_status st = cint_rt_entry_begin(ctx, site(1u), fuel, depth);
    (void)args;
    if (st != CINT_OK) {
        return st;
    }
    *result = 7u;
    return cint_rt_entry_end(ctx);
}

/* An I8 entry that returns the pattern of 300, outside I8: a contract breach
 * the harness must report instead of printing a value. */
static const uint32_t sig_bad_ret[2] = {CINT_TAG_I8, 0u};
static cint_status entry_bad_ret(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args,
                                 uint64_t *result)
{
    cint_status st = cint_rt_entry_begin(ctx, site(1u), fuel, depth);
    (void)args;
    if (st != CINT_OK) {
        return st;
    }
    *result = 300u;
    return cint_rt_entry_end(ctx);
}

static const uint32_t sig_twice[3] = {CINT_TAG_I64, 1u, CINT_TAG_I64};
static cint_status entry_twice(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args,
                               uint64_t *result)
{
    static const cint_site entry_site = {1u, 1u};
    static const cint_site mul_site = {1u, 2u};
    int64_t r = 0;
    cint_status st = cint_rt_entry_begin(ctx, entry_site, fuel, depth);
    if (st != CINT_OK) {
        return st;
    }
    if (cint_mul_i64(ctx, mul_site, cint_rt_sext(args[0], 64u), 2, &r)) {
        *result = (uint64_t)r;
    }
    return cint_rt_entry_end(ctx);
}

/* Module order, then declaration order (X-2). */
static const cint_observer_entry entries[12] = {
    {0u, 1u, "f", sig_f, entry_f},
    {0u, 3u, "add", sig_add, entry_add},
    {0u, 4u, "neg8", sig_neg8, entry_neg8},
    {0u, 6u, "is_big", sig_is_big, entry_is_big},
    {0u, 7u, "nothing", sig_nothing, entry_nothing},
    {0u, 4u, "spin", sig_spin, entry_spin},
    {0u, 4u, "deep", sig_deep, entry_deep},
    {0u, 3u, "mix", sig_mix, entry_mix},
    {0u, 6u, "id_u64", sig_id_u64, entry_id_u64},
    {0u, 5u, "seven", NULL, entry_seven},
    {0u, 7u, "bad_ret", sig_bad_ret, entry_bad_ret},
    {1u, 5u, "twice", sig_twice, entry_twice},
};
FIXTURE_EXPORT const cint_observer cint_observer_desc = {12u, 0u, &program, entries};
FIXTURE_EXPORT const uint32_t cint_program_abi = CINT_ABI_VERSION;
