/* cint-core-1, cint-rt-3, emitted by cintc */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cint_rt.h"

CINT_RT_INTERNAL const cint_site_info cg_15_public_x5Fviews_sites[27] = {
    {0u, 0u, ""},
    {2u, 12u, "fuel.charge"},
    {4u, 6u, "index.checked.i64"},
    {4u, 13u, "index.checked.i64"},
    {4u, 17u, "add.checked.i64"},
    {5u, 13u, "index.checked.i64"},
    {5u, 25u, "index.checked.u8"},
    {5u, 29u, "as.checked.u8.i64"},
    {5u, 17u, "add.checked.i64"},
    {5u, 44u, "index.checked.u8"},
    {5u, 48u, "as.checked.u8.i64"},
    {5u, 37u, "add.checked.i64"},
    {8u, 12u, "fuel.charge"},
    {9u, 6u, "index.checked.i32"},
    {9u, 13u, "index.checked.u8"},
    {9u, 17u, "as.checked.u8.i32"},
    {10u, 13u, "index.checked.i32"},
    {10u, 17u, "as.checked.i32.i64"},
    {13u, 13u, "fuel.charge"},
    {14u, 18u, "index.checked.bool"},
    {17u, 12u, "fuel.charge"},
    {18u, 6u, "index.checked.i64"},
    {19u, 15u, "div.checked.i64"},
    {22u, 12u, "fuel.charge"},
    {24u, 13u, "index.checked.i64"},
    {24u, 20u, "index.checked.i64"},
    {24u, 17u, "add.checked.i64"},
};
CINT_RT_INTERNAL const cint_module cg_15_public_x5Fviews_module = {"public_views.ci", 15u, 27u, cg_15_public_x5Fviews_sites};
CINT_RT_INTERNAL extern const cint_program cg_program;
CINT_RT_EXPORT const cint_module_info cm_15_public_x5Fviews = {CINT_ABI_VERSION, 0u, &cg_program, NULL};

static bool ci_15_public_x5Fviews_6_paired(cint_ctx *ctx, int64_t v_n, int64_t v_m, int64_t *v_a, int64_t o_a, int64_t n_a, int64_t s_a, int64_t v_bias, int64_t *v_b, int64_t o_b, int64_t n_b, int64_t s_b, uint8_t *v_bytes, int64_t o_bytes, int64_t n_bytes, int64_t s_bytes, uint8_t *v_more, int64_t o_more, int64_t n_more, int64_t s_more, int64_t *result);
static bool ci_15_public_x5Fviews_5_fixed(cint_ctx *ctx, int32_t *v_a, int64_t o_a, int64_t n_a, int64_t s_a, uint8_t *v_b, int64_t o_b, int64_t n_b, int64_t s_b, int64_t *result);
static bool ci_15_public_x5Fviews_4_bits(cint_ctx *ctx, bool v_a, bool *v_b, int64_t o_b, int64_t n_b, int64_t s_b, bool *result);
static bool ci_15_public_x5Fviews_8_faulting(cint_ctx *ctx, int64_t *v_a, int64_t o_a, int64_t n_a, int64_t s_a, int64_t v_divisor, int64_t *result);
static bool ci_15_public_x5Fviews_8_readonly(cint_ctx *ctx, int64_t v_n, int64_t *v_a, int64_t o_a, int64_t n_a, int64_t s_a, int64_t *v_b, int64_t o_b, int64_t n_b, int64_t s_b, int64_t *result);

static bool ci_15_public_x5Fviews_6_paired(cint_ctx *ctx, int64_t v_n, int64_t v_m, int64_t *v_a, int64_t o_a, int64_t n_a, int64_t s_a, int64_t v_bias, int64_t *v_b, int64_t o_b, int64_t n_b, int64_t s_b, uint8_t *v_bytes, int64_t o_bytes, int64_t n_bytes, int64_t s_bytes, uint8_t *v_more, int64_t o_more, int64_t n_more, int64_t s_more, int64_t *result)
{
    int64_t t0 = 0;
    bool t1 = false;
    int64_t t2 = 0;
    int64_t t3 = 0;
    int64_t t4 = 0;
    int64_t t5 = 0;
    int64_t t6 = 0;
    int64_t t7 = 0;
    int64_t t8 = 0;
    uint8_t t9 = 0;
    int64_t t10 = 0;
    int64_t t11 = 0;
    int64_t t12 = 0;
    uint8_t t13 = 0;
    int64_t t14 = 0;
    int64_t t15 = 0;
    (void)ctx;
    (void)v_n;
    (void)v_m;
    (void)v_a;
    (void)o_a;
    (void)n_a;
    (void)s_a;
    (void)v_bias;
    (void)v_b;
    (void)o_b;
    (void)n_b;
    (void)s_b;
    (void)v_bytes;
    (void)o_bytes;
    (void)n_bytes;
    (void)s_bytes;
    (void)v_more;
    (void)o_more;
    (void)n_more;
    (void)s_more;
    (void)result;
    t0 = INT64_C(0);
    t1 = v_n == t0;
    if (!t1) goto L5_f;
    *result = v_m;
    return true;
L5_f:;
    t2 = INT64_C(0);
    if (!(t2 >= 0 && t2 < n_a)) {
        (void)cint_index_check(ctx, (cint_site){0u, 2u}, "index.checked.i64", t2, n_a);
        goto fault;
    }
    t3 = INT64_C(0);
    if (!(t3 >= 0 && t3 < n_b)) {
        (void)cint_index_check(ctx, (cint_site){0u, 3u}, "index.checked.i64", t3, n_b);
        goto fault;
    }
    t4 = v_b[o_b + t3 * s_b];
    if (!cint_add_i64(ctx, (cint_site){0u, 4u}, t4, v_bias, &t5)) goto fault;
    v_a[o_a + t2 * s_a] = t5;
    t6 = INT64_C(0);
    if (!(t6 >= 0 && t6 < n_a)) {
        (void)cint_index_check(ctx, (cint_site){0u, 5u}, "index.checked.i64", t6, n_a);
        goto fault;
    }
    t7 = v_a[o_a + t6 * s_a];
    t8 = INT64_C(0);
    if (!(t8 >= 0 && t8 < n_bytes)) {
        (void)cint_index_check(ctx, (cint_site){0u, 6u}, "index.checked.u8", t8, n_bytes);
        goto fault;
    }
    t9 = v_bytes[o_bytes + t8 * s_bytes];
    if (!cint_as_i64_from_u8(ctx, (cint_site){0u, 7u}, t9, &t10)) goto fault;
    if (!cint_add_i64(ctx, (cint_site){0u, 8u}, t7, t10, &t11)) goto fault;
    t12 = INT64_C(0);
    if (!(t12 >= 0 && t12 < n_more)) {
        (void)cint_index_check(ctx, (cint_site){0u, 9u}, "index.checked.u8", t12, n_more);
        goto fault;
    }
    t13 = v_more[o_more + t12 * s_more];
    if (!cint_as_i64_from_u8(ctx, (cint_site){0u, 10u}, t13, &t14)) goto fault;
    if (!cint_add_i64(ctx, (cint_site){0u, 11u}, t11, t14, &t15)) goto fault;
    *result = t15;
    return true;
fault:
    return false;
}

static bool ci_15_public_x5Fviews_5_fixed(cint_ctx *ctx, int32_t *v_a, int64_t o_a, int64_t n_a, int64_t s_a, uint8_t *v_b, int64_t o_b, int64_t n_b, int64_t s_b, int64_t *result)
{
    int64_t t0 = 0;
    int64_t t1 = 0;
    uint8_t t2 = 0;
    int32_t t3 = 0;
    int64_t t4 = 0;
    int32_t t5 = 0;
    int64_t t6 = 0;
    (void)ctx;
    (void)v_a;
    (void)o_a;
    (void)n_a;
    (void)s_a;
    (void)v_b;
    (void)o_b;
    (void)n_b;
    (void)s_b;
    (void)result;
    t0 = INT64_C(0);
    if (!(t0 >= 0 && t0 < n_a)) {
        (void)cint_index_check(ctx, (cint_site){0u, 13u}, "index.checked.i32", t0, n_a);
        goto fault;
    }
    t1 = INT64_C(0);
    if (!(t1 >= 0 && t1 < n_b)) {
        (void)cint_index_check(ctx, (cint_site){0u, 14u}, "index.checked.u8", t1, n_b);
        goto fault;
    }
    t2 = v_b[o_b + t1 * s_b];
    if (!cint_as_i32_from_u8(ctx, (cint_site){0u, 15u}, t2, &t3)) goto fault;
    v_a[o_a + t0 * s_a] = t3;
    t4 = INT64_C(0);
    if (!(t4 >= 0 && t4 < n_a)) {
        (void)cint_index_check(ctx, (cint_site){0u, 16u}, "index.checked.i32", t4, n_a);
        goto fault;
    }
    t5 = v_a[o_a + t4 * s_a];
    if (!cint_as_i64_from_i32(ctx, (cint_site){0u, 17u}, t5, &t6)) goto fault;
    *result = t6;
    return true;
fault:
    return false;
}

static bool ci_15_public_x5Fviews_4_bits(cint_ctx *ctx, bool v_a, bool *v_b, int64_t o_b, int64_t n_b, int64_t s_b, bool *result)
{
    int64_t t0 = 0;
    bool t1 = false;
    bool t2 = false;
    bool l0 = false;
    (void)ctx;
    (void)v_a;
    (void)v_b;
    (void)o_b;
    (void)n_b;
    (void)s_b;
    (void)l0;
    (void)result;
    l0 = v_a;
    if (!v_a) goto L5_f;
    t0 = INT64_C(0);
    if (!(t0 >= 0 && t0 < n_b)) {
        (void)cint_index_check(ctx, (cint_site){0u, 19u}, "index.checked.bool", t0, n_b);
        goto fault;
    }
    t1 = v_b[o_b + t0 * s_b];
    l0 = t1;
    goto L3_e;
L5_f:;
L3_e:;
    t2 = l0;
    *result = t2;
    return true;
fault:
    return false;
}

static bool ci_15_public_x5Fviews_8_faulting(cint_ctx *ctx, int64_t *v_a, int64_t o_a, int64_t n_a, int64_t s_a, int64_t v_divisor, int64_t *result)
{
    int64_t t0 = 0;
    int64_t t1 = 0;
    int64_t t2 = 0;
    int64_t t3 = 0;
    (void)ctx;
    (void)v_a;
    (void)o_a;
    (void)n_a;
    (void)s_a;
    (void)v_divisor;
    (void)result;
    t0 = INT64_C(0);
    if (!(t0 >= 0 && t0 < n_a)) {
        (void)cint_index_check(ctx, (cint_site){0u, 21u}, "index.checked.i64", t0, n_a);
        goto fault;
    }
    t1 = INT64_C(42);
    v_a[o_a + t0 * s_a] = t1;
    t2 = INT64_C(10);
    if (!cint_div_i64(ctx, (cint_site){0u, 22u}, t2, v_divisor, &t3)) goto fault;
    *result = t3;
    return true;
fault:
    return false;
}

static bool ci_15_public_x5Fviews_8_readonly(cint_ctx *ctx, int64_t v_n, int64_t *v_a, int64_t o_a, int64_t n_a, int64_t s_a, int64_t *v_b, int64_t o_b, int64_t n_b, int64_t s_b, int64_t *result)
{
    int64_t t0 = 0;
    bool t1 = false;
    int64_t t2 = 0;
    int64_t t3 = 0;
    int64_t t4 = 0;
    int64_t t5 = 0;
    int64_t t6 = 0;
    int64_t t7 = 0;
    (void)ctx;
    (void)v_n;
    (void)v_a;
    (void)o_a;
    (void)n_a;
    (void)s_a;
    (void)v_b;
    (void)o_b;
    (void)n_b;
    (void)s_b;
    (void)result;
    t0 = INT64_C(0);
    t1 = v_n == t0;
    if (!t1) goto L5_f;
    t2 = INT64_C(0);
    *result = t2;
    return true;
L5_f:;
    t3 = INT64_C(0);
    if (!(t3 >= 0 && t3 < n_a)) {
        (void)cint_index_check(ctx, (cint_site){0u, 24u}, "index.checked.i64", t3, n_a);
        goto fault;
    }
    t4 = v_a[o_a + t3 * s_a];
    t5 = INT64_C(0);
    if (!(t5 >= 0 && t5 < n_b)) {
        (void)cint_index_check(ctx, (cint_site){0u, 25u}, "index.checked.i64", t5, n_b);
        goto fault;
    }
    t6 = v_b[o_b + t5 * s_b];
    if (!cint_add_i64(ctx, (cint_site){0u, 26u}, t4, t6, &t7)) goto fault;
    *result = t7;
    return true;
fault:
    return false;
}

CINT_RT_EXPORT cint_status cx_15_public_x5Fviews_6_paired(cint_ctx *ctx, int64_t fuel, cint_view p_a, int64_t p_bias, cint_view p_b, cint_view p_bytes, cint_view p_more, int64_t *result)
{
    static const cint_type t_a = {CINT_TAG_I64, 0u, 0u, 0u, 0u, 0u, 0u};
    static const cint_type t_b = {CINT_TAG_I64, 0u, 0u, 0u, 0u, 0u, 0u};
    static const cint_type t_bytes = {CINT_TAG_U8, 0u, 0u, 0u, 0u, 0u, 0u};
    static const cint_type t_more = {CINT_TAG_U8, 0u, 0u, 0u, 0u, 0u, 0u};
    cint_bind g[4] = {
        {&p_a, &t_a, (int64_t)sizeof(int64_t), NULL, 0u, 0u, CINT_MODE_INOUT, 1u, NULL},
        {&p_b, &t_b, (int64_t)sizeof(int64_t), NULL, 0u, 2u, CINT_MODE_IN, 1u, NULL},
        {&p_bytes, &t_bytes, (int64_t)sizeof(uint8_t), NULL, 0u, 3u, CINT_MODE_IN, 1u, NULL},
        {&p_more, &t_more, (int64_t)sizeof(uint8_t), NULL, 0u, 4u, CINT_MODE_IN, 1u, NULL},
    };
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
    if (!cint_rt_bind_views(ctx, (cint_site){0u, 1u}, g, 4u)) {
        return cint_rt_entry_end(ctx);
    }
    if (p_b.shape[0] != p_a.shape[0]) {
        (void)cint_fault_shape(ctx, (cint_site){0u, 1u}, 2u, 0u, p_a.shape[0], p_b.shape[0]);
        return cint_rt_entry_end(ctx);
    }
    if (p_more.shape[0] != p_bytes.shape[0]) {
        (void)cint_fault_shape(ctx, (cint_site){0u, 1u}, 4u, 0u, p_bytes.shape[0], p_more.shape[0]);
        return cint_rt_entry_end(ctx);
    }
    if (!cint_rt_bind_finish(ctx, (cint_site){0u, 1u}, g, 4u) || !cint_fuel_charge(ctx, (cint_site){0u, 1u}, 1u)) {
        return cint_rt_entry_end(ctx);
    }
    ok = ci_15_public_x5Fviews_6_paired(ctx, p_a.shape[0], p_bytes.shape[0], g[0].ptr, INT64_C(0), p_a.shape[0], p_a.stride[0], p_bias, g[1].ptr, INT64_C(0), p_b.shape[0], p_b.stride[0], g[2].ptr, INT64_C(0), p_bytes.shape[0], p_bytes.stride[0], g[3].ptr, INT64_C(0), p_more.shape[0], p_more.stride[0], &r);
    st = cint_rt_entry_end(ctx);
    if (ok && st == CINT_OK) {
        *result = r;
    }
    return st;
}

CINT_RT_EXPORT cint_status cx_15_public_x5Fviews_5_fixed(cint_ctx *ctx, int64_t fuel, cint_view p_a, cint_view p_b, int64_t *result)
{
    static const cint_type t_a = {CINT_TAG_I32, 0u, 0u, 0u, 0u, 0u, 0u};
    static const cint_type t_b = {CINT_TAG_U8, 0u, 0u, 0u, 0u, 0u, 0u};
    cint_bind g[2] = {
        {&p_a, &t_a, (int64_t)sizeof(int32_t), NULL, 0u, 0u, CINT_MODE_INOUT, 1u, NULL},
        {&p_b, &t_b, (int64_t)sizeof(uint8_t), NULL, 0u, 1u, CINT_MODE_IN, 1u, NULL},
    };
    cint_status st;
    bool ok;
    int64_t r = 0;
    st = cint_rt_entry_open(ctx, (cint_site){0u, 12u}, fuel, CINT_DEPTH_FROM_CONFIG);
    if (st != CINT_OK) {
        return st;
    }
    if (result == NULL) {
        (void)cint_rt_refuse(ctx, CINT_REFUSAL_RESULT);
        return cint_rt_entry_end(ctx);
    }
    if (!cint_rt_bind_views(ctx, (cint_site){0u, 12u}, g, 2u)) {
        return cint_rt_entry_end(ctx);
    }
    if (p_a.shape[0] != 4) {
        (void)cint_fault_shape(ctx, (cint_site){0u, 12u}, 0u, 0u, 4, p_a.shape[0]);
        return cint_rt_entry_end(ctx);
    }
    if (!cint_rt_bind_finish(ctx, (cint_site){0u, 12u}, g, 2u) || !cint_fuel_charge(ctx, (cint_site){0u, 12u}, 1u)) {
        return cint_rt_entry_end(ctx);
    }
    ok = ci_15_public_x5Fviews_5_fixed(ctx, g[0].ptr, INT64_C(0), p_a.shape[0], p_a.stride[0], g[1].ptr, INT64_C(0), p_b.shape[0], p_b.stride[0], &r);
    st = cint_rt_entry_end(ctx);
    if (ok && st == CINT_OK) {
        *result = r;
    }
    return st;
}

CINT_RT_EXPORT cint_status cx_15_public_x5Fviews_4_bits(cint_ctx *ctx, int64_t fuel, uint8_t p_a, cint_view p_b, uint8_t *result)
{
    static const cint_type t_b = {CINT_TAG_BOOL, 0u, 0u, 0u, 0u, 0u, 0u};
    static const uint32_t b_b[1] = {0u};
    cint_bind g[1] = {
        {&p_b, &t_b, (int64_t)sizeof(bool), b_b, 1u, 1u, CINT_MODE_IN, 1u, NULL},
    };
    cint_status st;
    bool ok;
    bool r = false;
    st = cint_rt_entry_open(ctx, (cint_site){0u, 18u}, fuel, CINT_DEPTH_FROM_CONFIG);
    if (st != CINT_OK) {
        return st;
    }
    if (p_a > 1u) {
        (void)cint_rt_refuse(ctx, CINT_REFUSAL_BOOL);
        return cint_rt_entry_end(ctx);
    }
    if (result == NULL) {
        (void)cint_rt_refuse(ctx, CINT_REFUSAL_RESULT);
        return cint_rt_entry_end(ctx);
    }
    if (!cint_rt_bind_views(ctx, (cint_site){0u, 18u}, g, 1u)) {
        return cint_rt_entry_end(ctx);
    }
    if (!cint_rt_bind_finish(ctx, (cint_site){0u, 18u}, g, 1u) || !cint_fuel_charge(ctx, (cint_site){0u, 18u}, 1u)) {
        return cint_rt_entry_end(ctx);
    }
    ok = ci_15_public_x5Fviews_4_bits(ctx, p_a != 0u, g[0].ptr, INT64_C(0), p_b.shape[0], p_b.stride[0], &r);
    st = cint_rt_entry_end(ctx);
    if (ok && st == CINT_OK) {
        *result = r ? 1u : 0u;
    }
    return st;
}

CINT_RT_EXPORT cint_status cx_15_public_x5Fviews_8_faulting(cint_ctx *ctx, int64_t fuel, cint_view p_a, int64_t p_divisor, int64_t *result)
{
    static const cint_type t_a = {CINT_TAG_I64, 0u, 0u, 0u, 0u, 0u, 0u};
    cint_bind g[1] = {
        {&p_a, &t_a, (int64_t)sizeof(int64_t), NULL, 0u, 0u, CINT_MODE_INOUT, 1u, NULL},
    };
    cint_status st;
    bool ok;
    int64_t r = 0;
    st = cint_rt_entry_open(ctx, (cint_site){0u, 20u}, fuel, CINT_DEPTH_FROM_CONFIG);
    if (st != CINT_OK) {
        return st;
    }
    if (result == NULL) {
        (void)cint_rt_refuse(ctx, CINT_REFUSAL_RESULT);
        return cint_rt_entry_end(ctx);
    }
    if (!cint_rt_bind_views(ctx, (cint_site){0u, 20u}, g, 1u)) {
        return cint_rt_entry_end(ctx);
    }
    if (!cint_rt_bind_finish(ctx, (cint_site){0u, 20u}, g, 1u) || !cint_fuel_charge(ctx, (cint_site){0u, 20u}, 1u)) {
        return cint_rt_entry_end(ctx);
    }
    ok = ci_15_public_x5Fviews_8_faulting(ctx, g[0].ptr, INT64_C(0), p_a.shape[0], p_a.stride[0], p_divisor, &r);
    st = cint_rt_entry_end(ctx);
    if (ok && st == CINT_OK) {
        *result = r;
    }
    return st;
}

CINT_RT_EXPORT cint_status cx_15_public_x5Fviews_8_readonly(cint_ctx *ctx, int64_t fuel, cint_view p_a, cint_view p_b, int64_t *result)
{
    static const cint_type t_a = {CINT_TAG_I64, 0u, 0u, 0u, 0u, 0u, 0u};
    static const cint_type t_b = {CINT_TAG_I64, 0u, 0u, 0u, 0u, 0u, 0u};
    cint_bind g[2] = {
        {&p_a, &t_a, (int64_t)sizeof(int64_t), NULL, 0u, 0u, CINT_MODE_IN, 1u, NULL},
        {&p_b, &t_b, (int64_t)sizeof(int64_t), NULL, 0u, 1u, CINT_MODE_IN, 1u, NULL},
    };
    cint_status st;
    bool ok;
    int64_t r = 0;
    st = cint_rt_entry_open(ctx, (cint_site){0u, 23u}, fuel, CINT_DEPTH_FROM_CONFIG);
    if (st != CINT_OK) {
        return st;
    }
    if (result == NULL) {
        (void)cint_rt_refuse(ctx, CINT_REFUSAL_RESULT);
        return cint_rt_entry_end(ctx);
    }
    if (!cint_rt_bind_views(ctx, (cint_site){0u, 23u}, g, 2u)) {
        return cint_rt_entry_end(ctx);
    }
    if (p_b.shape[0] != p_a.shape[0]) {
        (void)cint_fault_shape(ctx, (cint_site){0u, 23u}, 1u, 0u, p_a.shape[0], p_b.shape[0]);
        return cint_rt_entry_end(ctx);
    }
    if (!cint_rt_bind_finish(ctx, (cint_site){0u, 23u}, g, 2u) || !cint_fuel_charge(ctx, (cint_site){0u, 23u}, 1u)) {
        return cint_rt_entry_end(ctx);
    }
    ok = ci_15_public_x5Fviews_8_readonly(ctx, p_a.shape[0], g[0].ptr, INT64_C(0), p_a.shape[0], p_a.stride[0], g[1].ptr, INT64_C(0), p_b.shape[0], p_b.stride[0], &r);
    st = cint_rt_entry_end(ctx);
    if (ok && st == CINT_OK) {
        *result = r;
    }
    return st;
}

static const cint_name cg_15_public_x5Fviews_xn0[7] = {
    {"n", 1u, 0u},
    {"m", 1u, 0u},
    {"a", 1u, 0u},
    {"bias", 4u, 0u},
    {"b", 1u, 0u},
    {"bytes", 5u, 0u},
    {"more", 4u, 0u},
};
static const uint8_t cg_15_public_x5Fviews_xs0[] = {
    0x1d, 0x00, 0x00, 0x00, 0x63, 0x69, 0x6e, 0x74, 0x2d, 0x63, 0x6f, 0x72, 0x65, 0x2d, 0x31, 0x2f,
    0x74, 0x79, 0x70, 0x65, 0x2d, 0x73, 0x69, 0x67, 0x6e, 0x61, 0x74, 0x75, 0x72, 0x65, 0x2f, 0x76,
    0x31, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x01, 0x52, 0x14,
    0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14,
    0x00, 0x52, 0x14, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x52, 0x21, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x52, 0x21, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x14,
};

static const cint_name cg_15_public_x5Fviews_xn1[2] = {
    {"a", 1u, 0u},
    {"b", 1u, 0u},
};
static const uint8_t cg_15_public_x5Fviews_xs1[] = {
    0x1d, 0x00, 0x00, 0x00, 0x63, 0x69, 0x6e, 0x74, 0x2d, 0x63, 0x6f, 0x72, 0x65, 0x2d, 0x31, 0x2f,
    0x74, 0x79, 0x70, 0x65, 0x2d, 0x73, 0x69, 0x67, 0x6e, 0x61, 0x74, 0x75, 0x72, 0x65, 0x2f, 0x76,
    0x31, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x52, 0x13,
    0x01, 0x01, 0xff, 0xff, 0xff, 0xff, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x52,
    0x21, 0x01, 0x00, 0x14,
};

static const cint_name cg_15_public_x5Fviews_xn2[2] = {
    {"a", 1u, 0u},
    {"b", 1u, 0u},
};
static const uint8_t cg_15_public_x5Fviews_xs2[] = {
    0x1d, 0x00, 0x00, 0x00, 0x63, 0x69, 0x6e, 0x74, 0x2d, 0x63, 0x6f, 0x72, 0x65, 0x2d, 0x31, 0x2f,
    0x74, 0x79, 0x70, 0x65, 0x2d, 0x73, 0x69, 0x67, 0x6e, 0x61, 0x74, 0x75, 0x72, 0x65, 0x2f, 0x76,
    0x31, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x52, 0x01, 0x01, 0x00, 0x01,
};

static const cint_name cg_15_public_x5Fviews_xn3[2] = {
    {"a", 1u, 0u},
    {"divisor", 7u, 0u},
};
static const uint8_t cg_15_public_x5Fviews_xs3[] = {
    0x1d, 0x00, 0x00, 0x00, 0x63, 0x69, 0x6e, 0x74, 0x2d, 0x63, 0x6f, 0x72, 0x65, 0x2d, 0x31, 0x2f,
    0x74, 0x79, 0x70, 0x65, 0x2d, 0x73, 0x69, 0x67, 0x6e, 0x61, 0x74, 0x75, 0x72, 0x65, 0x2f, 0x76,
    0x31, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x52, 0x14,
    0x01, 0x00, 0x00, 0x14, 0x14,
};

static const cint_name cg_15_public_x5Fviews_xn4[3] = {
    {"n", 1u, 0u},
    {"a", 1u, 0u},
    {"b", 1u, 0u},
};
static const uint8_t cg_15_public_x5Fviews_xs4[] = {
    0x1d, 0x00, 0x00, 0x00, 0x63, 0x69, 0x6e, 0x74, 0x2d, 0x63, 0x6f, 0x72, 0x65, 0x2d, 0x31, 0x2f,
    0x74, 0x79, 0x70, 0x65, 0x2d, 0x73, 0x69, 0x67, 0x6e, 0x61, 0x74, 0x75, 0x72, 0x65, 0x2f, 0x76,
    0x31, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x52, 0x14,
    0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x52,
    0x14, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14,
};

static const cint_export cg_15_public_x5Fviews_exports[5] = {
    {{"paired", 6u, 0u}, CINT_EXPORT_FUNCTION, CINT_PURE, 2u, 5u, &cg_15_public_x5Fviews_xn0[0], &cg_15_public_x5Fviews_xn0[2], cg_15_public_x5Fviews_xs0, (uint64_t)sizeof(cg_15_public_x5Fviews_xs0), (cint_entry_fn)cx_15_public_x5Fviews_6_paired},
    {{"fixed", 5u, 0u}, CINT_EXPORT_FUNCTION, CINT_PURE, 0u, 2u, NULL, &cg_15_public_x5Fviews_xn1[0], cg_15_public_x5Fviews_xs1, (uint64_t)sizeof(cg_15_public_x5Fviews_xs1), (cint_entry_fn)cx_15_public_x5Fviews_5_fixed},
    {{"bits", 4u, 0u}, CINT_EXPORT_FUNCTION, CINT_PURE, 0u, 2u, NULL, &cg_15_public_x5Fviews_xn2[0], cg_15_public_x5Fviews_xs2, (uint64_t)sizeof(cg_15_public_x5Fviews_xs2), (cint_entry_fn)cx_15_public_x5Fviews_4_bits},
    {{"faulting", 8u, 0u}, CINT_EXPORT_FUNCTION, CINT_PURE, 0u, 2u, NULL, &cg_15_public_x5Fviews_xn3[0], cg_15_public_x5Fviews_xs3, (uint64_t)sizeof(cg_15_public_x5Fviews_xs3), (cint_entry_fn)cx_15_public_x5Fviews_8_faulting},
    {{"readonly", 8u, 0u}, CINT_EXPORT_FUNCTION, CINT_PURE, 1u, 2u, &cg_15_public_x5Fviews_xn4[0], &cg_15_public_x5Fviews_xn4[1], cg_15_public_x5Fviews_xs4, (uint64_t)sizeof(cg_15_public_x5Fviews_xs4), (cint_entry_fn)cx_15_public_x5Fviews_8_readonly},
};

CINT_RT_INTERNAL const cint_module_table cg_15_public_x5Fviews_table = {&cm_15_public_x5Fviews, 0u, 5u, cg_15_public_x5Fviews_exports, NULL};
