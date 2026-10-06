/* cint-core-1, cint-rt-3, emitted by cintc */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cint_rt.h"

CINT_RT_INTERNAL const cint_site_info cg_17_public_x5Frecords_sites[16] = {
    {0u, 0u, ""},
    {14u, 12u, "fuel.charge"},
    {15u, 6u, "index.checked.struct"},
    {15u, 16u, "add.checked.i32"},
    {16u, 13u, "index.checked.struct"},
    {16u, 23u, "as.checked.i32.i64"},
    {19u, 15u, "fuel.charge"},
    {20u, 31u, "add.checked.i64"},
    {23u, 15u, "fuel.charge"},
    {25u, 22u, "div.checked.i64"},
    {28u, 12u, "fuel.charge"},
    {31u, 20u, "as.checked.u8.i64"},
    {31u, 38u, "as.checked.u8.i64"},
    {31u, 28u, "add.checked.i64"},
    {31u, 59u, "call.enter"},
    {31u, 46u, "add.checked.i64"},
};
CINT_RT_INTERNAL const cint_module cg_17_public_x5Frecords_module = {"public_records.ci", 17u, 16u, cg_17_public_x5Frecords_sites};
CINT_RT_INTERNAL extern const cint_program cg_program;
typedef struct {
    bool f3;
    uint8_t f4;
} cg_17_public_x5Frecords_r2;
static const cint_record_field cg_17_public_x5Frecords_rf2[2] = {
    {(uint32_t)offsetof(cg_17_public_x5Frecords_r2, f3), 1u, {CINT_TAG_BOOL, 0u, 0u, 0u, 0u, 0u, 0u}},
    {(uint32_t)offsetof(cg_17_public_x5Frecords_r2, f4), 1u, {CINT_TAG_U8, 0u, 0u, 0u, 0u, 0u, 0u}},
};
typedef struct {
    uint16_t f6;
    bool f7[3];
} cg_17_public_x5Frecords_r5;
static const cint_record_field cg_17_public_x5Frecords_rf5[2] = {
    {(uint32_t)offsetof(cg_17_public_x5Frecords_r5, f6), 1u, {CINT_TAG_U16, 0u, 0u, 0u, 0u, 0u, 0u}},
    {(uint32_t)offsetof(cg_17_public_x5Frecords_r5, f7), 3u, {CINT_TAG_BOOL, 0u, 0u, 0u, 0u, 0u, 0u}},
};
typedef struct {
    uint32_t f13;
} cg_17_public_x5Frecords_r12;
static const cint_record_field cg_17_public_x5Frecords_rf12[1] = {
    {(uint32_t)offsetof(cg_17_public_x5Frecords_r12, f13), 1u, {CINT_TAG_U32, 0u, 0u, 0u, 0u, 0u, 0u}},
};
typedef struct {
    uint8_t f17;
    int32_t f18;
    bool f19;
    bool f20[2];
    cg_17_public_x5Frecords_r2 f21;
} cg_17_public_x5Frecords_r16;
static const cint_record_field cg_17_public_x5Frecords_rf16[5] = {
    {(uint32_t)offsetof(cg_17_public_x5Frecords_r16, f17), 1u, {CINT_TAG_U8, 0u, 0u, 0u, 0u, 0u, 0u}},
    {(uint32_t)offsetof(cg_17_public_x5Frecords_r16, f18), 1u, {CINT_TAG_I32, 0u, 0u, 0u, 0u, 0u, 0u}},
    {(uint32_t)offsetof(cg_17_public_x5Frecords_r16, f19), 1u, {CINT_TAG_BOOL, 0u, 0u, 0u, 0u, 0u, 0u}},
    {(uint32_t)offsetof(cg_17_public_x5Frecords_r16, f20), 2u, {CINT_TAG_BOOL, 0u, 0u, 0u, 0u, 0u, 0u}},
    {(uint32_t)offsetof(cg_17_public_x5Frecords_r16, f21), 1u, {CINT_TAG_RECORD, 0u, 0u, 0u, 0u, 3u, 0u}},
};
typedef struct {
    int64_t f23;
    bool f24;
} cg_17_public_x5Frecords_r22;
static const cint_record_field cg_17_public_x5Frecords_rf22[2] = {
    {(uint32_t)offsetof(cg_17_public_x5Frecords_r22, f23), 1u, {CINT_TAG_I64, 0u, 0u, 0u, 0u, 0u, 0u}},
    {(uint32_t)offsetof(cg_17_public_x5Frecords_r22, f24), 1u, {CINT_TAG_BOOL, 0u, 0u, 0u, 0u, 0u, 0u}},
};
static const cint_record_layout cg_17_public_x5Frecords_records[5] = {
    {(uint32_t)sizeof(cg_17_public_x5Frecords_r16), CINT_ELEM_ALIGN(sizeof(cg_17_public_x5Frecords_r16)), 5u, 0u, cg_17_public_x5Frecords_rf16},
    {(uint32_t)sizeof(cg_17_public_x5Frecords_r22), CINT_ELEM_ALIGN(sizeof(cg_17_public_x5Frecords_r22)), 2u, 0u, cg_17_public_x5Frecords_rf22},
    {(uint32_t)sizeof(cg_17_public_x5Frecords_r12), CINT_ELEM_ALIGN(sizeof(cg_17_public_x5Frecords_r12)), 1u, 0u, cg_17_public_x5Frecords_rf12},
    {(uint32_t)sizeof(cg_17_public_x5Frecords_r2), CINT_ELEM_ALIGN(sizeof(cg_17_public_x5Frecords_r2)), 2u, 0u, cg_17_public_x5Frecords_rf2},
    {(uint32_t)sizeof(cg_17_public_x5Frecords_r5), CINT_ELEM_ALIGN(sizeof(cg_17_public_x5Frecords_r5)), 2u, 0u, cg_17_public_x5Frecords_rf5},
};
CINT_RT_EXPORT const cint_module_info cm_17_public_x5Frecords = {CINT_ABI_VERSION, 5u, &cg_program, cg_17_public_x5Frecords_records};

typedef struct cs_13_record_x5Flib_4_Flag {
    bool f_enabled;
    uint8_t f_code;
} cs_13_record_x5Flib_4_Flag;
typedef struct cs_17_public_x5Frecords_6_Packet {
    uint8_t f_marker;
    int32_t f_value;
    bool f_flag;
    bool f_bits[2];
    cs_13_record_x5Flib_4_Flag f_nested;
} cs_17_public_x5Frecords_6_Packet;
typedef struct cs_17_public_x5Frecords_6_Simple {
    int64_t f_value;
    bool f_flag;
} cs_17_public_x5Frecords_6_Simple;
static bool ci_17_public_x5Frecords_5_touch(cint_ctx *ctx, cs_17_public_x5Frecords_6_Packet *v_a, int64_t o_a, int64_t n_a, int64_t s_a, int64_t *result);
static bool ci_17_public_x5Frecords_6_copied(cint_ctx *ctx, cs_17_public_x5Frecords_6_Simple v_input, cs_17_public_x5Frecords_6_Simple *result);
static bool ci_17_public_x5Frecords_4_both(cint_ctx *ctx, cs_17_public_x5Frecords_6_Simple * v_a, cs_17_public_x5Frecords_6_Simple v_b, int64_t v_divisor, cs_17_public_x5Frecords_6_Simple *result);
static bool ci_17_public_x5Frecords_7_aliases(cint_ctx *ctx, int64_t *result);
CINT_RT_INTERNAL extern bool ci_13_record_x5Fmid_8_observed(cint_ctx *ctx, int64_t *result);

static bool ci_17_public_x5Frecords_5_touch(cint_ctx *ctx, cs_17_public_x5Frecords_6_Packet *v_a, int64_t o_a, int64_t n_a, int64_t s_a, int64_t *result)
{
    int64_t t0 = 0;
    int32_t t1 = 0;
    int32_t t2 = 0;
    int32_t t3 = 0;
    int64_t t4 = 0;
    int32_t t5 = 0;
    int64_t t6 = 0;
    (void)ctx;
    (void)v_a;
    (void)o_a;
    (void)n_a;
    (void)s_a;
    (void)result;
    t0 = INT64_C(0);
    if (!(t0 >= 0 && t0 < n_a)) {
        (void)cint_index_check(ctx, (cint_site){2u, 2u}, "index.checked.struct", t0, n_a);
        goto fault;
    }
    t1 = v_a[o_a + t0 * s_a].f_value;
    t2 = 1;
    if (!cint_add_i32(ctx, (cint_site){2u, 3u}, t1, t2, &t3)) goto fault;
    v_a[o_a + t0 * s_a].f_value = t3;
    t4 = INT64_C(0);
    if (!(t4 >= 0 && t4 < n_a)) {
        (void)cint_index_check(ctx, (cint_site){2u, 4u}, "index.checked.struct", t4, n_a);
        goto fault;
    }
    t5 = v_a[o_a + t4 * s_a].f_value;
    if (!cint_as_i64_from_i32(ctx, (cint_site){2u, 5u}, t5, &t6)) goto fault;
    *result = t6;
    return true;
fault:
    return false;
}

static bool ci_17_public_x5Frecords_6_copied(cint_ctx *ctx, cs_17_public_x5Frecords_6_Simple v_input, cs_17_public_x5Frecords_6_Simple *result)
{
    int64_t t0 = 0;
    int64_t t1 = 0;
    int64_t t2 = 0;
    bool t3 = false;
    cs_17_public_x5Frecords_6_Simple l0 = {0};
    (void)ctx;
    (void)v_input;
    (void)l0;
    (void)result;
    l0.f_value = 0;
    l0.f_flag = false;
    t0 = v_input.f_value;
    t1 = INT64_C(1);
    if (!cint_add_i64(ctx, (cint_site){2u, 7u}, t0, t1, &t2)) goto fault;
    t3 = v_input.f_flag;
    l0.f_value = t2;
    l0.f_flag = t3;
    *result = l0;
    return true;
fault:
    return false;
}

static bool ci_17_public_x5Frecords_4_both(cint_ctx *ctx, cs_17_public_x5Frecords_6_Simple * v_a, cs_17_public_x5Frecords_6_Simple v_b, int64_t v_divisor, cs_17_public_x5Frecords_6_Simple *result)
{
    int64_t t0 = 0;
    int64_t t1 = 0;
    int64_t t2 = 0;
    bool t3 = false;
    cs_17_public_x5Frecords_6_Simple l0 = {0};
    (void)ctx;
    (void)v_a;
    (void)v_b;
    (void)v_divisor;
    (void)l0;
    (void)result;
    t0 = INT64_C(42);
    (*v_a).f_value = t0;
    l0.f_value = 0;
    l0.f_flag = false;
    t1 = INT64_C(10);
    if (!cint_div_i64(ctx, (cint_site){2u, 9u}, t1, v_divisor, &t2)) goto fault;
    t3 = v_b.f_flag;
    l0.f_value = t2;
    l0.f_flag = t3;
    *result = l0;
    return true;
fault:
    return false;
}

static bool ci_17_public_x5Frecords_7_aliases(cint_ctx *ctx, int64_t *result)
{
    bool t0 = false;
    uint8_t t1 = 0;
    bool t2 = false;
    uint8_t t3 = 0;
    uint8_t t4 = 0;
    int64_t t5 = 0;
    uint8_t t6 = 0;
    int64_t t7 = 0;
    int64_t t8 = 0;
    int64_t t9 = 0;
    int64_t t10 = 0;
    cs_13_record_x5Flib_4_Flag l0 = {0};
    cs_13_record_x5Flib_4_Flag l1_a = {0};
    cs_13_record_x5Flib_4_Flag l2 = {0};
    cs_13_record_x5Flib_4_Flag l3_b = {0};
    (void)ctx;
    (void)l0;
    (void)l1_a;
    (void)l2;
    (void)l3_b;
    (void)result;
    l0.f_enabled = false;
    l0.f_code = 0;
    t0 = true;
    t1 = 3u;
    l0.f_enabled = t0;
    l0.f_code = t1;
    l1_a.f_enabled = false;
    l1_a.f_code = 0;
    l1_a = l0;
    l2.f_enabled = false;
    l2.f_code = 0;
    t2 = false;
    t3 = 4u;
    l2.f_enabled = t2;
    l2.f_code = t3;
    l3_b.f_enabled = false;
    l3_b.f_code = 0;
    l3_b = l2;
    t4 = l1_a.f_code;
    if (!cint_as_i64_from_u8(ctx, (cint_site){2u, 11u}, t4, &t5)) goto fault;
    t6 = l3_b.f_code;
    if (!cint_as_i64_from_u8(ctx, (cint_site){2u, 12u}, t6, &t7)) goto fault;
    if (!cint_add_i64(ctx, (cint_site){2u, 13u}, t5, t7, &t8)) goto fault;
    if (!cint_rt_call_enter(ctx, (cint_site){2u, 14u})) goto fault;
    if (!ci_13_record_x5Fmid_8_observed(ctx, &t9)) goto fault;
    cint_rt_call_leave(ctx);
    if (!cint_add_i64(ctx, (cint_site){2u, 15u}, t8, t9, &t10)) goto fault;
    *result = t10;
    return true;
fault:
    return false;
}

static const uint32_t cg_17_public_x5Frecords_t0[2] = {CINT_TAG_I64, 0u};

static cint_status cg_17_public_x5Frecords_o0(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args, uint64_t *result)
{
    cint_status st;
    bool ok;
    int64_t r = 0;
    (void)args;
    st = cint_rt_entry_begin(ctx, (cint_site){2u, 10u}, fuel, depth);
    if (st != CINT_OK) {
        return st;
    }
    ok = ci_17_public_x5Frecords_7_aliases(ctx, &r);
    st = cint_rt_entry_end(ctx);
    if (ok && st == CINT_OK) {
        *result = (uint64_t)r;
    }
    return st;
}

CINT_RT_EXPORT cint_status cx_17_public_x5Frecords_5_touch(cint_ctx *ctx, int64_t fuel, cint_view p_a, int64_t *result)
{
    static const cint_type t_a = {CINT_TAG_RECORD, 0u, 0u, 0u, 0u, 0u, 0u};
    static const uint32_t b_a[4] = {8u, 9u, 10u, 11u};
    cint_bind g[1] = {
        {&p_a, &t_a, (int64_t)sizeof(cs_17_public_x5Frecords_6_Packet), b_a, 4u, 0u, CINT_MODE_INOUT, 1u, NULL},
    };
    cint_status st;
    bool ok;
    int64_t r = 0;
    st = cint_rt_entry_open(ctx, (cint_site){2u, 1u}, fuel, CINT_DEPTH_FROM_CONFIG);
    if (st != CINT_OK) {
        return st;
    }
    if (result == NULL) {
        (void)cint_rt_refuse(ctx, CINT_REFUSAL_RESULT);
        return cint_rt_entry_end(ctx);
    }
    if (!cint_rt_bind_views(ctx, (cint_site){2u, 1u}, g, 1u)) {
        return cint_rt_entry_end(ctx);
    }
    if (!cint_rt_bind_finish(ctx, (cint_site){2u, 1u}, g, 1u) || !cint_fuel_charge(ctx, (cint_site){2u, 1u}, 1u)) {
        return cint_rt_entry_end(ctx);
    }
    ok = ci_17_public_x5Frecords_5_touch(ctx, g[0].ptr, INT64_C(0), p_a.shape[0], p_a.stride[0], &r);
    st = cint_rt_entry_end(ctx);
    if (ok && st == CINT_OK) {
        *result = r;
    }
    return st;
}

CINT_RT_EXPORT cint_status cx_17_public_x5Frecords_6_copied(cint_ctx *ctx, int64_t fuel, cint_view p_input, void *result)
{
    static const cint_type t_input = {CINT_TAG_RECORD, 0u, 0u, 0u, 0u, 1u, 0u};
    static const uint32_t b_input[1] = {8u};
    cint_bind g[1] = {
        {&p_input, &t_input, (int64_t)sizeof(cs_17_public_x5Frecords_6_Simple), b_input, 1u, 0u, CINT_MODE_IN, 1u, NULL},
    };
    cint_status st;
    bool ok;
    cs_17_public_x5Frecords_6_Simple r = {0};
    st = cint_rt_entry_open(ctx, (cint_site){2u, 6u}, fuel, CINT_DEPTH_FROM_CONFIG);
    if (st != CINT_OK) {
        return st;
    }
    if (result == NULL) {
        (void)cint_rt_refuse(ctx, CINT_REFUSAL_RESULT);
        return cint_rt_entry_end(ctx);
    }
    if (!cint_rt_bind_views(ctx, (cint_site){2u, 6u}, g, 1u)) {
        return cint_rt_entry_end(ctx);
    }
    if (p_input.shape[0] != 1) {
        (void)cint_fault_shape(ctx, (cint_site){2u, 6u}, 0u, 0u, 1, p_input.shape[0]);
        return cint_rt_entry_end(ctx);
    }
    if (!cint_rt_bind_finish(ctx, (cint_site){2u, 6u}, g, 1u) || !cint_fuel_charge(ctx, (cint_site){2u, 6u}, 1u)) {
        return cint_rt_entry_end(ctx);
    }
    ok = ci_17_public_x5Frecords_6_copied(ctx, *(cs_17_public_x5Frecords_6_Simple *)g[0].ptr, &r);
    st = cint_rt_entry_end(ctx);
    if (ok && st == CINT_OK) {
        *(cs_17_public_x5Frecords_6_Simple *)result = r;
    }
    return st;
}

CINT_RT_EXPORT cint_status cx_17_public_x5Frecords_4_both(cint_ctx *ctx, int64_t fuel, cint_view p_a, cint_view p_b, int64_t p_divisor, void *result)
{
    static const cint_type t_a = {CINT_TAG_RECORD, 0u, 0u, 0u, 0u, 1u, 0u};
    static const uint32_t b_a[1] = {8u};
    static const cint_type t_b = {CINT_TAG_RECORD, 0u, 0u, 0u, 0u, 1u, 0u};
    static const uint32_t b_b[1] = {8u};
    cint_bind g[2] = {
        {&p_a, &t_a, (int64_t)sizeof(cs_17_public_x5Frecords_6_Simple), b_a, 1u, 0u, CINT_MODE_INOUT, 1u, NULL},
        {&p_b, &t_b, (int64_t)sizeof(cs_17_public_x5Frecords_6_Simple), b_b, 1u, 1u, CINT_MODE_IN, 1u, NULL},
    };
    cint_status st;
    bool ok;
    cs_17_public_x5Frecords_6_Simple r = {0};
    st = cint_rt_entry_open(ctx, (cint_site){2u, 8u}, fuel, CINT_DEPTH_FROM_CONFIG);
    if (st != CINT_OK) {
        return st;
    }
    if (result == NULL) {
        (void)cint_rt_refuse(ctx, CINT_REFUSAL_RESULT);
        return cint_rt_entry_end(ctx);
    }
    if (!cint_rt_bind_views(ctx, (cint_site){2u, 8u}, g, 2u)) {
        return cint_rt_entry_end(ctx);
    }
    if (p_a.shape[0] != 1) {
        (void)cint_fault_shape(ctx, (cint_site){2u, 8u}, 0u, 0u, 1, p_a.shape[0]);
        return cint_rt_entry_end(ctx);
    }
    if (p_b.shape[0] != 1) {
        (void)cint_fault_shape(ctx, (cint_site){2u, 8u}, 1u, 0u, 1, p_b.shape[0]);
        return cint_rt_entry_end(ctx);
    }
    if (!cint_rt_bind_finish(ctx, (cint_site){2u, 8u}, g, 2u) || !cint_fuel_charge(ctx, (cint_site){2u, 8u}, 1u)) {
        return cint_rt_entry_end(ctx);
    }
    ok = ci_17_public_x5Frecords_4_both(ctx, g[0].ptr, *(cs_17_public_x5Frecords_6_Simple *)g[1].ptr, p_divisor, &r);
    st = cint_rt_entry_end(ctx);
    if (ok && st == CINT_OK) {
        *(cs_17_public_x5Frecords_6_Simple *)result = r;
    }
    return st;
}

CINT_RT_EXPORT cint_status cx_17_public_x5Frecords_7_aliases(cint_ctx *ctx, int64_t fuel, int64_t *result)
{
    cint_status st;
    bool ok;
    int64_t r = 0;
    st = cint_rt_entry_open(ctx, (cint_site){2u, 10u}, fuel, CINT_DEPTH_FROM_CONFIG);
    if (st != CINT_OK) {
        return st;
    }
    if (result == NULL) {
        (void)cint_rt_refuse(ctx, CINT_REFUSAL_RESULT);
        return cint_rt_entry_end(ctx);
    }
    if (!cint_fuel_charge(ctx, (cint_site){2u, 10u}, 1u)) {
        return cint_rt_entry_end(ctx);
    }
    ok = ci_17_public_x5Frecords_7_aliases(ctx, &r);
    st = cint_rt_entry_end(ctx);
    if (ok && st == CINT_OK) {
        *result = r;
    }
    return st;
}

CINT_RT_INTERNAL const cint_observer_entry cg_17_public_x5Frecords_obs[1] = {
    {2u, 7u, "aliases", cg_17_public_x5Frecords_t0, cg_17_public_x5Frecords_o0},
};

static const cint_name cg_17_public_x5Frecords_xn0[1] = {
    {"a", 1u, 0u},
};
static const uint8_t cg_17_public_x5Frecords_xs0[] = {
    0x1d, 0x00, 0x00, 0x00, 0x63, 0x69, 0x6e, 0x74, 0x2d, 0x63, 0x6f, 0x72, 0x65, 0x2d, 0x31, 0x2f,
    0x74, 0x79, 0x70, 0x65, 0x2d, 0x73, 0x69, 0x67, 0x6e, 0x61, 0x74, 0x75, 0x72, 0x65, 0x2f, 0x76,
    0x31, 0x02, 0x00, 0x00, 0x00, 0x71, 0x0f, 0x00, 0x00, 0x00, 0x72, 0x65, 0x63, 0x6f, 0x72, 0x64,
    0x5f, 0x6c, 0x69, 0x62, 0x2e, 0x46, 0x6c, 0x61, 0x67, 0x00, 0x02, 0x00, 0x00, 0x00, 0x07, 0x00,
    0x00, 0x00, 0x65, 0x6e, 0x61, 0x62, 0x6c, 0x65, 0x64, 0x00, 0x01, 0x04, 0x00, 0x00, 0x00, 0x63,
    0x6f, 0x64, 0x65, 0x00, 0x21, 0x71, 0x15, 0x00, 0x00, 0x00, 0x70, 0x75, 0x62, 0x6c, 0x69, 0x63,
    0x5f, 0x72, 0x65, 0x63, 0x6f, 0x72, 0x64, 0x73, 0x2e, 0x50, 0x61, 0x63, 0x6b, 0x65, 0x74, 0x00,
    0x05, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x6d, 0x61, 0x72, 0x6b, 0x65, 0x72, 0x00, 0x21,
    0x05, 0x00, 0x00, 0x00, 0x76, 0x61, 0x6c, 0x75, 0x65, 0x00, 0x13, 0x04, 0x00, 0x00, 0x00, 0x66,
    0x6c, 0x61, 0x67, 0x00, 0x01, 0x04, 0x00, 0x00, 0x00, 0x62, 0x69, 0x74, 0x73, 0x00, 0x52, 0x01,
    0x01, 0x01, 0xff, 0xff, 0xff, 0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00,
    0x00, 0x00, 0x6e, 0x65, 0x73, 0x74, 0x65, 0x64, 0x00, 0x71, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x52, 0x71, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x14,
};

static const cint_name cg_17_public_x5Frecords_xn1[1] = {
    {"input", 5u, 0u},
};
static const uint8_t cg_17_public_x5Frecords_xs1[] = {
    0x1d, 0x00, 0x00, 0x00, 0x63, 0x69, 0x6e, 0x74, 0x2d, 0x63, 0x6f, 0x72, 0x65, 0x2d, 0x31, 0x2f,
    0x74, 0x79, 0x70, 0x65, 0x2d, 0x73, 0x69, 0x67, 0x6e, 0x61, 0x74, 0x75, 0x72, 0x65, 0x2f, 0x76,
    0x31, 0x01, 0x00, 0x00, 0x00, 0x71, 0x15, 0x00, 0x00, 0x00, 0x70, 0x75, 0x62, 0x6c, 0x69, 0x63,
    0x5f, 0x72, 0x65, 0x63, 0x6f, 0x72, 0x64, 0x73, 0x2e, 0x53, 0x69, 0x6d, 0x70, 0x6c, 0x65, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x76, 0x61, 0x6c, 0x75, 0x65, 0x00, 0x14, 0x04,
    0x00, 0x00, 0x00, 0x66, 0x6c, 0x61, 0x67, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x71, 0x00, 0x00, 0x00, 0x00, 0x71, 0x00, 0x00, 0x00, 0x00,
};

static const cint_name cg_17_public_x5Frecords_xn2[3] = {
    {"a", 1u, 0u},
    {"b", 1u, 0u},
    {"divisor", 7u, 0u},
};
static const uint8_t cg_17_public_x5Frecords_xs2[] = {
    0x1d, 0x00, 0x00, 0x00, 0x63, 0x69, 0x6e, 0x74, 0x2d, 0x63, 0x6f, 0x72, 0x65, 0x2d, 0x31, 0x2f,
    0x74, 0x79, 0x70, 0x65, 0x2d, 0x73, 0x69, 0x67, 0x6e, 0x61, 0x74, 0x75, 0x72, 0x65, 0x2f, 0x76,
    0x31, 0x01, 0x00, 0x00, 0x00, 0x71, 0x15, 0x00, 0x00, 0x00, 0x70, 0x75, 0x62, 0x6c, 0x69, 0x63,
    0x5f, 0x72, 0x65, 0x63, 0x6f, 0x72, 0x64, 0x73, 0x2e, 0x53, 0x69, 0x6d, 0x70, 0x6c, 0x65, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x76, 0x61, 0x6c, 0x75, 0x65, 0x00, 0x14, 0x04,
    0x00, 0x00, 0x00, 0x66, 0x6c, 0x61, 0x67, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x01, 0x71, 0x00, 0x00, 0x00, 0x00, 0x00, 0x71, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x71,
    0x00, 0x00, 0x00, 0x00,
};

static const uint8_t cg_17_public_x5Frecords_xs3[] = {
    0x1d, 0x00, 0x00, 0x00, 0x63, 0x69, 0x6e, 0x74, 0x2d, 0x63, 0x6f, 0x72, 0x65, 0x2d, 0x31, 0x2f,
    0x74, 0x79, 0x70, 0x65, 0x2d, 0x73, 0x69, 0x67, 0x6e, 0x61, 0x74, 0x75, 0x72, 0x65, 0x2f, 0x76,
    0x31, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14,
};

static const cint_export cg_17_public_x5Frecords_exports[4] = {
    {{"touch", 5u, 0u}, CINT_EXPORT_FUNCTION, CINT_PURE, 0u, 1u, NULL, &cg_17_public_x5Frecords_xn0[0], cg_17_public_x5Frecords_xs0, (uint64_t)sizeof(cg_17_public_x5Frecords_xs0), (cint_entry_fn)cx_17_public_x5Frecords_5_touch},
    {{"copied", 6u, 0u}, CINT_EXPORT_FUNCTION, CINT_PURE, 0u, 1u, NULL, &cg_17_public_x5Frecords_xn1[0], cg_17_public_x5Frecords_xs1, (uint64_t)sizeof(cg_17_public_x5Frecords_xs1), (cint_entry_fn)cx_17_public_x5Frecords_6_copied},
    {{"both", 4u, 0u}, CINT_EXPORT_FUNCTION, CINT_PURE, 0u, 3u, NULL, &cg_17_public_x5Frecords_xn2[0], cg_17_public_x5Frecords_xs2, (uint64_t)sizeof(cg_17_public_x5Frecords_xs2), (cint_entry_fn)cx_17_public_x5Frecords_4_both},
    {{"aliases", 7u, 0u}, CINT_EXPORT_FUNCTION, CINT_PURE, 0u, 0u, NULL, NULL, cg_17_public_x5Frecords_xs3, (uint64_t)sizeof(cg_17_public_x5Frecords_xs3), (cint_entry_fn)cx_17_public_x5Frecords_7_aliases},
};

static const cint_name cg_17_public_x5Frecords_rnames[5] = {
    {"public_records.Packet", 21u, 0u},
    {"public_records.Simple", 21u, 0u},
    {"record_mid.Middle", 17u, 0u},
    {"record_lib.Flag", 15u, 0u},
    {"record_lib.Unused", 17u, 0u},
};

CINT_RT_INTERNAL const cint_module_table cg_17_public_x5Frecords_table = {&cm_17_public_x5Frecords, 2u, 4u, cg_17_public_x5Frecords_exports, cg_17_public_x5Frecords_rnames};
