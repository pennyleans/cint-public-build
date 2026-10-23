/* cint-core-1, cint-rt-3, emitted by cintc */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cint_rt.h"

CINT_RT_INTERNAL const cint_site_info cg_7_externs_sites[14] = {
    {0u, 0u, ""},
    {14u, 12u, "fuel.charge"},
    {17u, 13u, "host.call"},
    {18u, 19u, "index.checked.i64"},
    {18u, 14u, "add.checked.i64"},
    {18u, 28u, "index.checked.i64"},
    {18u, 23u, "add.checked.i64"},
    {18u, 34u, "host.call"},
    {18u, 32u, "add.checked.i64"},
    {21u, 12u, "fuel.charge"},
    {22u, 13u, "host.call"},
    {26u, 13u, "fuel.charge"},
    {27u, 5u, "host.call"},
    {28u, 5u, "host.call"},
};
CINT_RT_INTERNAL const cint_module cg_7_externs_module = {"externs.ci", 10u, 14u, cg_7_externs_sites};
CINT_RT_INTERNAL extern const cint_program cg_program;
CINT_RT_EXPORT const cint_module_info cm_7_externs = {CINT_ABI_VERSION, 0u, &cg_program, NULL};
static const char *const cg_7_externs_es4_v[2] = {"closed", "refused"};
CINT_RT_INTERNAL const cint_error_set cg_7_externs_es4 = {"externs.IoError", CINT_TAG_U16, 2u, cg_7_externs_es4_v};

static bool ci_7_externs_5_total(cint_ctx *ctx, int64_t v_key, int64_t *result);
static bool ci_7_externs_6_pulled(cint_ctx *ctx, int64_t v_n, int64_t *result);
static bool ci_7_externs_5_flush(cint_ctx *ctx);

static bool ci_7_externs_5_total(cint_ctx *ctx, int64_t v_key, int64_t *result)
{
    int64_t t0 = 0;
    int64_t t1 = 0;
    int64_t t2 = 0;
    int64_t t3 = 0;
    int64_t t4 = 0;
    int64_t t5 = 0;
    int64_t t6 = 0;
    int64_t t7 = 0;
    int64_t t8 = 0;
    int64_t t9 = 0;
    int64_t t10 = 0;
    int64_t t11 = 0;
    int64_t t12 = 0;
    int64_t t13 = 0;
    int64_t t14 = 0;
    int64_t l0_keys[2] = {0};
    int64_t l1_buf[2] = {0};
    int64_t l2_n = 0;
    (void)ctx;
    (void)v_key;
    (void)l0_keys;
    (void)l1_buf;
    (void)l2_n;
    (void)result;
    static const cint_site cs2 = {0u, 2u}; (void)cs2;
    static const cint_site cs3 = {0u, 3u}; (void)cs3;
    static const cint_site cs4 = {0u, 4u}; (void)cs4;
    static const cint_site cs5 = {0u, 5u}; (void)cs5;
    static const cint_site cs6 = {0u, 6u}; (void)cs6;
    static const cint_site cs7 = {0u, 7u}; (void)cs7;
    static const cint_site cs8 = {0u, 8u}; (void)cs8;
    for (int64_t z = 0; z < INT64_C(2); z++) { l0_keys[z] = 0; }
    t0 = INT64_C(1);
    l0_keys[0] = t0;
    t1 = INT64_C(2);
    l0_keys[1] = t1;
    for (int64_t z = 0; z < INT64_C(2); z++) { l1_buf[z] = 0; }
    t2 = INT64_C(0);
    l1_buf[0] = t2;
    t3 = INT64_C(0);
    l1_buf[1] = t3;
    {
        cint_value hv[3], hr;
        cint_rt_arg_scalar(&hv[0], CINT_TAG_I64, (uint64_t)v_key);
        cint_rt_arg_view(&hv[1], CINT_TAG_I64, l0_keys, INT64_C(0), INT64_C(2), INT64_C(1), CINT_VIEW_READ);
        cint_rt_arg_view(&hv[2], CINT_TAG_I64, l1_buf, INT64_C(0), INT64_C(2), INT64_C(1), CINT_VIEW_WRITE);
        cint_rt_result_init(&hr, CINT_TAG_I64);
        if (!cint_rt_host_call(ctx, cs2, 0u, hv, 3u, &hr, 0u)) goto fault;
        t4 = (int64_t)hr.as.i64;
    }
    l2_n = t4;
    t5 = l2_n;
    t6 = INT64_C(0);
    if (!(t6 >= 0 && t6 < INT64_C(2))) {
        (void)cint_index_check(ctx, cs3, "index.checked.i64", t6, INT64_C(2));
        goto fault;
    }
    t7 = l1_buf[t6];
    if (!cint_add_i64(ctx, cs4, t5, t7, &t8)) goto fault;
    t9 = INT64_C(1);
    if (!(t9 >= 0 && t9 < INT64_C(2))) {
        (void)cint_index_check(ctx, cs5, "index.checked.i64", t9, INT64_C(2));
        goto fault;
    }
    t10 = l1_buf[t9];
    if (!cint_add_i64(ctx, cs6, t8, t10, &t11)) goto fault;
    t12 = INT64_C(3);
    {
        cint_value hv[1], hr;
        cint_rt_arg_scalar(&hv[0], CINT_TAG_I64, (uint64_t)t12);
        cint_rt_result_init(&hr, CINT_TAG_I64);
        if (!cint_rt_host_call(ctx, cs7, 1u, hv, 1u, &hr, 0u)) goto fault;
        t13 = (int64_t)hr.as.i64;
    }
    if (!cint_add_i64(ctx, cs8, t11, t13, &t14)) goto fault;
    *result = t14;
    return true;
fault:
    return false;
}

static bool ci_7_externs_6_pulled(cint_ctx *ctx, int64_t v_n, int64_t *result)
{
    bool t0 = false;
    int64_t t1 = 0;
    uint16_t t2 = 0;
    uint16_t t3 = 0;
    bool t4 = false;
    int64_t t5 = 0;
    int64_t t6 = 0;
    int64_t t7 = 0;
    int64_t l0 = 0;
    int64_t l1_v = 0;
    (void)ctx;
    (void)v_n;
    (void)l0;
    (void)l1_v;
    (void)result;
    static const cint_site cs10 = {0u, 10u}; (void)cs10;
    t0 = true;
    {
        cint_value hv[2], hr;
        cint_rt_arg_scalar(&hv[0], CINT_TAG_I64, (uint64_t)v_n);
        cint_rt_arg_scalar(&hv[1], CINT_TAG_BOOL, (uint64_t)t0);
        cint_rt_result_init(&hr, CINT_TAG_I64);
        if (!cint_rt_host_call(ctx, cs10, 3u, hv, 2u, &hr, 2u)) goto fault;
        t1 = (int64_t)hr.as.i64;
        t2 = hr.is_error != 0u ? (uint16_t)hr.error : (uint16_t)0;
    }
    t3 = 0u;
    t4 = t2 != t3;
    if (!t4) goto L11_f;
    t5 = INT64_C(-1);
    l0 = t5;
    goto L9_e;
L11_f:;
    l0 = t1;
L9_e:;
    t6 = l0;
    l1_v = t6;
    t7 = l1_v;
    *result = t7;
    return true;
fault:
    return false;
}

static bool ci_7_externs_5_flush(cint_ctx *ctx)
{
    (void)ctx;
    static const cint_site cs12 = {0u, 12u}; (void)cs12;
    static const cint_site cs13 = {0u, 13u}; (void)cs13;
    static uint8_t s1[5] = {100, 111, 110, 101, 10};
    {
        cint_value hv[1], hr;
        cint_rt_arg_view(&hv[0], CINT_TAG_U8, s1, INT64_C(0), INT64_C(5), INT64_C(1), CINT_VIEW_READ);
        cint_rt_result_init(&hr, 0u);
        if (!cint_rt_host_call(ctx, cs12, 4u, hv, 1u, &hr, 0u)) goto fault;
    }
    {
        cint_value hv[1], hr;
        cint_rt_result_init(&hr, 0u);
        if (!cint_rt_host_call(ctx, cs13, 2u, hv, 0u, &hr, 0u)) goto fault;
    }
    return true;
fault:
    return false;
}

static const uint32_t cg_7_externs_t0[3] = {CINT_TAG_I64, 1u, CINT_TAG_I64};

static cint_status cg_7_externs_o0(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args, uint64_t *result)
{
    cint_status st;
    bool ok;
    int64_t r = 0;
    st = cint_rt_entry_begin(ctx, (cint_site){0u, 1u}, fuel, depth);
    if (st != CINT_OK) {
        return st;
    }
    ok = ci_7_externs_5_total(ctx, (int64_t)cint_rt_sext(args[0], 64u), &r);
    st = cint_rt_entry_end(ctx);
    if (ok && st == CINT_OK) {
        *result = (uint64_t)r;
    }
    return st;
}

static const uint32_t cg_7_externs_t1[3] = {CINT_TAG_I64, 1u, CINT_TAG_I64};

static cint_status cg_7_externs_o1(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args, uint64_t *result)
{
    cint_status st;
    bool ok;
    int64_t r = 0;
    st = cint_rt_entry_begin(ctx, (cint_site){0u, 9u}, fuel, depth);
    if (st != CINT_OK) {
        return st;
    }
    ok = ci_7_externs_6_pulled(ctx, (int64_t)cint_rt_sext(args[0], 64u), &r);
    st = cint_rt_entry_end(ctx);
    if (ok && st == CINT_OK) {
        *result = (uint64_t)r;
    }
    return st;
}

static const uint32_t cg_7_externs_t2[2] = {0u, 0u};

static cint_status cg_7_externs_o2(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args, uint64_t *result)
{
    cint_status st;
    (void)args;
    (void)result;
    st = cint_rt_entry_begin(ctx, (cint_site){0u, 11u}, fuel, depth);
    if (st != CINT_OK) {
        return st;
    }
    (void)ci_7_externs_5_flush(ctx);
    st = cint_rt_entry_end(ctx);
    return st;
}

CINT_RT_EXPORT cint_status cx_7_externs_5_total(cint_ctx *ctx, int64_t fuel, int64_t p_key, int64_t *result)
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
    ok = ci_7_externs_5_total(ctx, p_key, &r);
    st = cint_rt_entry_end(ctx);
    if (ok && st == CINT_OK) {
        *result = r;
    }
    return st;
}

CINT_RT_EXPORT cint_status cx_7_externs_6_pulled(cint_ctx *ctx, int64_t fuel, int64_t p_n, int64_t *result)
{
    cint_status st;
    bool ok;
    int64_t r = 0;
    st = cint_rt_entry_open(ctx, (cint_site){0u, 9u}, fuel, CINT_DEPTH_FROM_CONFIG);
    if (st != CINT_OK) {
        return st;
    }
    if (result == NULL) {
        (void)cint_rt_refuse(ctx, CINT_REFUSAL_RESULT);
        return cint_rt_entry_end(ctx);
    }
    if (!cint_fuel_charge(ctx, (cint_site){0u, 9u}, 1u)) {
        return cint_rt_entry_end(ctx);
    }
    ok = ci_7_externs_6_pulled(ctx, p_n, &r);
    st = cint_rt_entry_end(ctx);
    if (ok && st == CINT_OK) {
        *result = r;
    }
    return st;
}

CINT_RT_EXPORT cint_status cx_7_externs_5_flush(cint_ctx *ctx, int64_t fuel)
{
    cint_status st;
    st = cint_rt_entry_open(ctx, (cint_site){0u, 11u}, fuel, CINT_DEPTH_FROM_CONFIG);
    if (st != CINT_OK) {
        return st;
    }
    if (!cint_fuel_charge(ctx, (cint_site){0u, 11u}, 1u)) {
        return cint_rt_entry_end(ctx);
    }
    (void)ci_7_externs_5_flush(ctx);
    return cint_rt_entry_end(ctx);
}

CINT_RT_INTERNAL const cint_observer_entry cg_7_externs_obs[3] = {
    {0u, 5u, "total", cg_7_externs_t0, cg_7_externs_o0},
    {0u, 6u, "pulled", cg_7_externs_t1, cg_7_externs_o1},
    {0u, 5u, "flush", cg_7_externs_t2, cg_7_externs_o2},
};

static const cint_name cg_7_externs_xn0[1] = {
    {"key", 3u, 0u},
};
static const uint8_t cg_7_externs_xs0[] = {
    0x1d, 0x00, 0x00, 0x00, 0x63, 0x69, 0x6e, 0x74, 0x2d, 0x63, 0x6f, 0x72, 0x65, 0x2d, 0x31, 0x2f,
    0x74, 0x79, 0x70, 0x65, 0x2d, 0x73, 0x69, 0x67, 0x6e, 0x61, 0x74, 0x75, 0x72, 0x65, 0x2f, 0x76,
    0x31, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x14, 0x14,
};

static const cint_name cg_7_externs_xn1[1] = {
    {"n", 1u, 0u},
};
static const uint8_t cg_7_externs_xs1[] = {
    0x1d, 0x00, 0x00, 0x00, 0x63, 0x69, 0x6e, 0x74, 0x2d, 0x63, 0x6f, 0x72, 0x65, 0x2d, 0x31, 0x2f,
    0x74, 0x79, 0x70, 0x65, 0x2d, 0x73, 0x69, 0x67, 0x6e, 0x61, 0x74, 0x75, 0x72, 0x65, 0x2f, 0x76,
    0x31, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x14, 0x14,
};

static const uint8_t cg_7_externs_xs2[] = {
    0x1d, 0x00, 0x00, 0x00, 0x63, 0x69, 0x6e, 0x74, 0x2d, 0x63, 0x6f, 0x72, 0x65, 0x2d, 0x31, 0x2f,
    0x74, 0x79, 0x70, 0x65, 0x2d, 0x73, 0x69, 0x67, 0x6e, 0x61, 0x74, 0x75, 0x72, 0x65, 0x2f, 0x76,
    0x31, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const cint_export cg_7_externs_exports[3] = {
    {{"total", 5u, 0u}, CINT_EXPORT_FUNCTION, CINT_OBSERVE, 0u, 1u, NULL, &cg_7_externs_xn0[0], cg_7_externs_xs0, (uint64_t)sizeof(cg_7_externs_xs0), (cint_entry_fn)cx_7_externs_5_total},
    {{"pulled", 6u, 0u}, CINT_EXPORT_FUNCTION, CINT_OBSERVE, 0u, 1u, NULL, &cg_7_externs_xn1[0], cg_7_externs_xs1, (uint64_t)sizeof(cg_7_externs_xs1), (cint_entry_fn)cx_7_externs_6_pulled},
    {{"flush", 5u, 0u}, CINT_EXPORT_FUNCTION, CINT_UNRESTRICTED, 0u, 0u, NULL, NULL, cg_7_externs_xs2, (uint64_t)sizeof(cg_7_externs_xs2), (cint_entry_fn)cx_7_externs_5_flush},
};

CINT_RT_INTERNAL const cint_module_table cg_7_externs_table = {&cm_7_externs, 0u, 3u, cg_7_externs_exports, NULL};

static const uint8_t cg_7_externs_hs0[] = {
    0x1d, 0x00, 0x00, 0x00, 0x63, 0x69, 0x6e, 0x74, 0x2d, 0x63, 0x6f, 0x72, 0x65, 0x2d, 0x31, 0x2f,
    0x74, 0x79, 0x70, 0x65, 0x2d, 0x73, 0x69, 0x67, 0x6e, 0x61, 0x74, 0x75, 0x72, 0x65, 0x2f, 0x76,
    0x31, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00,
    0x52, 0x14, 0x01, 0x00, 0x02, 0x52, 0x14, 0x01, 0x00, 0x14,
};

static const uint8_t cg_7_externs_hs1[] = {
    0x1d, 0x00, 0x00, 0x00, 0x63, 0x69, 0x6e, 0x74, 0x2d, 0x63, 0x6f, 0x72, 0x65, 0x2d, 0x31, 0x2f,
    0x74, 0x79, 0x70, 0x65, 0x2d, 0x73, 0x69, 0x67, 0x6e, 0x61, 0x74, 0x75, 0x72, 0x65, 0x2f, 0x76,
    0x31, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x14, 0x14,
};

static const uint8_t cg_7_externs_hs2[] = {
    0x1d, 0x00, 0x00, 0x00, 0x63, 0x69, 0x6e, 0x74, 0x2d, 0x63, 0x6f, 0x72, 0x65, 0x2d, 0x31, 0x2f,
    0x74, 0x79, 0x70, 0x65, 0x2d, 0x73, 0x69, 0x67, 0x6e, 0x61, 0x74, 0x75, 0x72, 0x65, 0x2f, 0x76,
    0x31, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t cg_7_externs_hs3[] = {
    0x1d, 0x00, 0x00, 0x00, 0x63, 0x69, 0x6e, 0x74, 0x2d, 0x63, 0x6f, 0x72, 0x65, 0x2d, 0x31, 0x2f,
    0x74, 0x79, 0x70, 0x65, 0x2d, 0x73, 0x69, 0x67, 0x6e, 0x61, 0x74, 0x75, 0x72, 0x65, 0x2f, 0x76,
    0x31, 0x01, 0x00, 0x00, 0x00, 0x73, 0x0f, 0x00, 0x00, 0x00, 0x65, 0x78, 0x74, 0x65, 0x72, 0x6e,
    0x73, 0x2e, 0x49, 0x6f, 0x45, 0x72, 0x72, 0x6f, 0x72, 0x22, 0x00, 0x02, 0x00, 0x00, 0x00, 0x06,
    0x00, 0x00, 0x00, 0x63, 0x6c, 0x6f, 0x73, 0x65, 0x64, 0x07, 0x00, 0x00, 0x00, 0x72, 0x65, 0x66,
    0x75, 0x73, 0x65, 0x64, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00, 0x01,
    0x74, 0x00, 0x00, 0x00, 0x00, 0x14,
};

static const uint8_t cg_7_externs_hs4[] = {
    0x1d, 0x00, 0x00, 0x00, 0x63, 0x69, 0x6e, 0x74, 0x2d, 0x63, 0x6f, 0x72, 0x65, 0x2d, 0x31, 0x2f,
    0x74, 0x79, 0x70, 0x65, 0x2d, 0x73, 0x69, 0x67, 0x6e, 0x61, 0x74, 0x75, 0x72, 0x65, 0x2f, 0x76,
    0x31, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x52, 0x21,
    0x01, 0x00, 0x00,
};

static const cint_host_import cg_7_externs_himports[5] = {
    {{"fetch", 5u, 0u}, CINT_OBSERVE, 3u, cg_7_externs_hs0, (uint64_t)sizeof(cg_7_externs_hs0)},
    {{"square", 6u, 0u}, CINT_PURE, 1u, cg_7_externs_hs1, (uint64_t)sizeof(cg_7_externs_hs1)},
    {{"sync", 4u, 0u}, CINT_UNRESTRICTED, 0u, cg_7_externs_hs2, (uint64_t)sizeof(cg_7_externs_hs2)},
    {{"pull", 4u, 0u}, CINT_OBSERVE, 2u, cg_7_externs_hs3, (uint64_t)sizeof(cg_7_externs_hs3)},
    {{"put", 3u, 0u}, CINT_OBSERVE, 1u, cg_7_externs_hs4, (uint64_t)sizeof(cg_7_externs_hs4)},
};
CINT_RT_INTERNAL const cint_host_table cg_7_externs_host = {5u, 0u, cg_7_externs_himports};
