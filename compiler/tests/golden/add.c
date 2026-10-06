/* cint-core-1, cint-rt-3, emitted by cintc */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cint_rt.h"

CINT_RT_INTERNAL const cint_site_info cg_3_add_sites[4] = {
    {0u, 0u, ""},
    {2u, 12u, "fuel.charge"},
    {3u, 14u, "add.checked.i64"},
    {3u, 18u, "add.checked.i64"},
};
CINT_RT_INTERNAL const cint_module cg_3_add_module = {"add.ci", 6u, 4u, cg_3_add_sites};
CINT_RT_INTERNAL extern const cint_program cg_program;
CINT_RT_EXPORT const cint_module_info cm_3_add = {CINT_ABI_VERSION, 0u, &cg_program, NULL};

static bool ci_3_add_4_add3(cint_ctx *ctx, int64_t v_a, int64_t v_b, int64_t v_c, int64_t *result);

static bool ci_3_add_4_add3(cint_ctx *ctx, int64_t v_a, int64_t v_b, int64_t v_c, int64_t *result)
{
    int64_t t0 = 0;
    int64_t t1 = 0;
    (void)ctx;
    (void)v_a;
    (void)v_b;
    (void)v_c;
    (void)result;
    if (!cint_add_i64(ctx, (cint_site){0u, 2u}, v_a, v_b, &t0)) goto fault;
    if (!cint_add_i64(ctx, (cint_site){0u, 3u}, t0, v_c, &t1)) goto fault;
    *result = t1;
    return true;
fault:
    return false;
}

static const uint32_t cg_3_add_t0[5] = {CINT_TAG_I64, 3u, CINT_TAG_I64, CINT_TAG_I64, CINT_TAG_I64};

static cint_status cg_3_add_o0(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args, uint64_t *result)
{
    cint_status st;
    bool ok;
    int64_t r = 0;
    st = cint_rt_entry_begin(ctx, (cint_site){0u, 1u}, fuel, depth);
    if (st != CINT_OK) {
        return st;
    }
    ok = ci_3_add_4_add3(ctx, (int64_t)cint_rt_sext(args[0], 64u), (int64_t)cint_rt_sext(args[1], 64u), (int64_t)cint_rt_sext(args[2], 64u), &r);
    st = cint_rt_entry_end(ctx);
    if (ok && st == CINT_OK) {
        *result = (uint64_t)r;
    }
    return st;
}

CINT_RT_EXPORT cint_status cx_3_add_4_add3(cint_ctx *ctx, int64_t fuel, int64_t p_a, int64_t p_b, int64_t p_c, int64_t *result)
{
    cint_status st;
    bool ok;
    int64_t r = 0;
    st = cint_rt_entry_open(ctx, (cint_site){0u, 1u}, fuel, CINT_DEPTH_FROM_CONFIG);
    if (st != CINT_OK) {
        return st;
    }
    if (result == NULL) {
        (void)cint_rt_refuse(ctx, CINT_REFUSAL_RESULT);
        return cint_rt_entry_end(ctx);
    }
    if (!cint_fuel_charge(ctx, (cint_site){0u, 1u}, 1u)) {
        return cint_rt_entry_end(ctx);
    }
    ok = ci_3_add_4_add3(ctx, p_a, p_b, p_c, &r);
    st = cint_rt_entry_end(ctx);
    if (ok && st == CINT_OK) {
        *result = r;
    }
    return st;
}

CINT_RT_INTERNAL const cint_observer_entry cg_3_add_obs[1] = {
    {0u, 4u, "add3", cg_3_add_t0, cg_3_add_o0},
};

static const cint_name cg_3_add_xn0[3] = {
    {"a", 1u, 0u},
    {"b", 1u, 0u},
    {"c", 1u, 0u},
};
static const uint8_t cg_3_add_xs0[] = {
    0x1d, 0x00, 0x00, 0x00, 0x63, 0x69, 0x6e, 0x74, 0x2d, 0x63, 0x6f, 0x72, 0x65, 0x2d, 0x31, 0x2f,
    0x74, 0x79, 0x70, 0x65, 0x2d, 0x73, 0x69, 0x67, 0x6e, 0x61, 0x74, 0x75, 0x72, 0x65, 0x2f, 0x76,
    0x31, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00,
    0x14, 0x00, 0x14, 0x14,
};

static const cint_export cg_3_add_exports[1] = {
    {{"add3", 4u, 0u}, CINT_EXPORT_FUNCTION, CINT_PURE, 0u, 3u, NULL, &cg_3_add_xn0[0], cg_3_add_xs0, (uint64_t)sizeof(cg_3_add_xs0), (cint_entry_fn)cx_3_add_4_add3},
};

CINT_RT_INTERNAL const cint_module_table cg_3_add_table = {&cm_3_add, 0u, 1u, cg_3_add_exports, NULL};
