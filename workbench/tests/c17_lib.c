/* c17_lib.c: a hand-written library for workbench/tests/wbt_c17.py (box 14 unit 5), so the
 * cpu-c17 loader is checked on every leg without B1, the Windows CI leg's LoadLibraryW among
 * them. It follows what cintc writes for `cint build --lib` (rt/cint_rt.h 6b'', cint-abi-1) and
 * stands for this CINT source (module path bank.ci), which wbt_c17.py also builds with B1:
 *
 *    1  // The workbench's cpu-c17 fixture (box 14 unit 5).
 *    2  I64 total = 0;
 *    3
 *    4  export I64 add(I64 a, I64 b) {
 *    5      return a + b;
 *    6  }
 *    7
 *    8  export void deposit(I64 amount) {
 *    9      total = total + amount;
 *   10      "deposit\n";
 *   11  }
 *   12
 *   13  export I64 balance() {
 *   14      return total;
 *   15  }
 *   16
 *   17  export I8 neg8(I8 a) {
 *   18      return -a;
 *   19  }
 *   20
 *   21  export Bool flip(Bool b) {
 *   22      return !b;
 *   23  }
 *   24
 *   25  export U16 twice16(U16 a) {
 *   26      return a * 2;
 *   27  }
 *   28
 *   29  export I64 sum3(I64[3] v) {
 *   30      return v[0] + v[1] + v[2];
 *   31  }
 *
 * The wrapper of sum3 refuses every call: the workbench lists it and never calls it, since it
 * takes an array. The revision identity is a placeholder; it only has to be present for a
 * checkpoint (rt/cint_state.h). Built with C17_NO_TABLE, the library exports no reflection table.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cint_rt.h"

static const cint_site_info sites[13] = {
    {0u, 0u, ""},
    {4u, 12u, "fuel.charge"},
    {5u, 14u, "add.checked.i64"},
    {8u, 13u, "fuel.charge"},
    {9u, 19u, "add.checked.i64"},
    {10u, 5u, "print"},
    {13u, 12u, "fuel.charge"},
    {17u, 11u, "fuel.charge"},
    {18u, 12u, "neg.checked.i8"},
    {21u, 13u, "fuel.charge"},
    {25u, 12u, "fuel.charge"},
    {26u, 14u, "mul.checked.u16"},
    {29u, 12u, "fuel.charge"},
};
static const cint_module module0 = {"bank.ci", 7u, 13u, sites};
static const cint_module *const modules[1] = {&module0};
static const uint8_t revision[32] = {0xc1u, 0x7fu};
static const cint_program program = {1u, 0u, modules, revision};
CINT_RT_EXPORT const cint_module_info cm_4_bank = {CINT_ABI_VERSION, 0u, &program, NULL};

typedef struct state0 {
    int64_t g_total;
} state0;
static const state0 state0_init = {INT64_C(0)};
static const cint_state_var state0_vars[1] = {{"total", 5u, CINT_TAG_I64, (uint32_t)offsetof(state0, g_total), 0u, NULL, NULL}};
static const cint_state state0_desc = {0u, 1u, (uint32_t)sizeof(state0), 0u, &state0_init, state0_vars};
static const cint_state *const states[1] = {&state0_desc};
CINT_RT_EXPORT const cint_state_table cint_observer_state = {1u, 0u, states, NULL};

static cint_site site(uint32_t index)
{
    cint_site s;
    s.module = 0u;
    s.index = index;
    return s;
}

/* The entry of a wrapper: checks, then the leading fuel charge (rt/cint_rt.h 6a'). */
static bool enter(cint_ctx *ctx, uint32_t at, int64_t fuel, cint_status *st, bool has_result, bool refuse)
{
    *st = cint_rt_entry_open(ctx, site(at), fuel, CINT_DEPTH_FROM_CONFIG);
    if (*st != CINT_OK) {
        return false;
    }
    if (refuse || !has_result || !cint_fuel_charge(ctx, site(at), 1u)) {
        if (refuse || !has_result) {
            (void)cint_rt_refuse(ctx, refuse ? CINT_REFUSAL_BOOL : CINT_REFUSAL_RESULT);
        }
        *st = cint_rt_entry_end(ctx);
        return false;
    }
    return true;
}

CINT_RT_EXPORT cint_status cx_4_bank_3_add(cint_ctx *ctx, int64_t fuel, int64_t p_a, int64_t p_b, int64_t *result)
{
    cint_status st;
    int64_t r = 0;
    if (!enter(ctx, 1u, fuel, &st, result != NULL, false)) {
        return st;
    }
    bool ok = cint_add_i64(ctx, site(2u), p_a, p_b, &r);
    st = cint_rt_entry_end(ctx);
    if (ok && st == CINT_OK) {
        *result = r;
    }
    return st;
}

CINT_RT_EXPORT cint_status cx_4_bank_7_deposit(cint_ctx *ctx, int64_t fuel, int64_t p_amount)
{
    cint_status st;
    if (!enter(ctx, 3u, fuel, &st, true, false)) {
        return st;
    }
    state0 *g = (state0 *)cint_rt_state(ctx, &state0_desc);
    if (g != NULL && cint_add_i64(ctx, site(4u), g->g_total, p_amount, &g->g_total) &&
        cint_rt_print_begin(ctx, site(5u))) {
        (void)(cint_rt_print_bytes(ctx, "deposit\n", 8u) && cint_rt_print_end(ctx));
    }
    return cint_rt_entry_end(ctx);
}

CINT_RT_EXPORT cint_status cx_4_bank_7_balance(cint_ctx *ctx, int64_t fuel, int64_t *result)
{
    cint_status st;
    if (!enter(ctx, 6u, fuel, &st, result != NULL, false)) {
        return st;
    }
    state0 *g = (state0 *)cint_rt_state(ctx, &state0_desc);
    int64_t r = g != NULL ? g->g_total : 0;
    st = cint_rt_entry_end(ctx);
    if (g != NULL && st == CINT_OK) {
        *result = r;
    }
    return st;
}

CINT_RT_EXPORT cint_status cx_4_bank_4_neg8(cint_ctx *ctx, int64_t fuel, int8_t p_a, int8_t *result)
{
    cint_status st;
    int8_t r = 0;
    if (!enter(ctx, 7u, fuel, &st, result != NULL, false)) {
        return st;
    }
    bool ok = cint_neg_i8(ctx, site(8u), p_a, &r);
    st = cint_rt_entry_end(ctx);
    if (ok && st == CINT_OK) {
        *result = r;
    }
    return st;
}

CINT_RT_EXPORT cint_status cx_4_bank_4_flip(cint_ctx *ctx, int64_t fuel, uint8_t p_b, uint8_t *result)
{
    cint_status st;
    if (!enter(ctx, 9u, fuel, &st, result != NULL, p_b > 1u)) {
        return st;
    }
    st = cint_rt_entry_end(ctx);
    if (st == CINT_OK) {
        *result = (uint8_t)(p_b != 0u ? 0u : 1u);
    }
    return st;
}

CINT_RT_EXPORT cint_status cx_4_bank_7_twice16(cint_ctx *ctx, int64_t fuel, uint16_t p_a, uint16_t *result)
{
    cint_status st;
    uint16_t r = 0;
    if (!enter(ctx, 10u, fuel, &st, result != NULL, false)) {
        return st;
    }
    bool ok = cint_mul_u16(ctx, site(11u), p_a, 2u, &r);
    st = cint_rt_entry_end(ctx);
    if (ok && st == CINT_OK) {
        *result = r;
    }
    return st;
}

CINT_RT_EXPORT cint_status cx_4_bank_4_sum3(cint_ctx *ctx, int64_t fuel, cint_view p_v, int64_t *result)
{
    cint_status st = cint_rt_entry_open(ctx, site(12u), fuel, CINT_DEPTH_FROM_CONFIG);
    (void)p_v;
    (void)result;
    if (st != CINT_OK) {
        return st;
    }
    (void)cint_rt_refuse(ctx, CINT_REFUSAL_CALL);
    return cint_rt_entry_end(ctx);
}

/* The reflection table: names and canonical type signatures (SPEC-03 A-19, A-20). */
#define SIG_HEAD 0x1du, 0u, 0u, 0u, 'c', 'i', 'n', 't', '-', 'c', 'o', 'r', 'e', '-', '1', '/', 't', 'y', 'p', 'e', \
                 '-', 's', 'i', 'g', 'n', 'a', 't', 'u', 'r', 'e', '/', 'v', '1', 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u
static const uint8_t sig_add[] = {SIG_HEAD, 2u, 0u, 0u, 0u, 0u, 0x14u, 0u, 0x14u, 0x14u};
static const uint8_t sig_deposit[] = {SIG_HEAD, 1u, 0u, 0u, 0u, 0u, 0x14u, 0u};
static const uint8_t sig_balance[] = {SIG_HEAD, 0u, 0u, 0u, 0u, 0x14u};
static const uint8_t sig_neg8[] = {SIG_HEAD, 1u, 0u, 0u, 0u, 0u, 0x11u, 0x11u};
static const uint8_t sig_flip[] = {SIG_HEAD, 1u, 0u, 0u, 0u, 0u, 0x01u, 0x01u};
static const uint8_t sig_twice16[] = {SIG_HEAD, 1u, 0u, 0u, 0u, 0u, 0x22u, 0x22u};
static const uint8_t sig_sum3[] = {SIG_HEAD, 1u, 0u, 0u, 0u, 0u, 0x52u, 0x14u, 0x01u, 0x01u, 0xffu, 0xffu, 0xffu,
                                   0xffu, 0x03u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0x14u};
static const cint_name names_ab[2] = {{"a", 1u, 0u}, {"b", 1u, 0u}};
static const cint_name names_amount[1] = {{"amount", 6u, 0u}};
static const cint_name names_v[1] = {{"v", 1u, 0u}};
#define ROW(n, len, effect, np, pn, sig, fn) \
    {{n, len, 0u}, CINT_EXPORT_FUNCTION, effect, 0u, np, NULL, pn, sig, (uint64_t)sizeof(sig), (cint_entry_fn)fn}
static const cint_export exports[7] = {
    ROW("add", 3u, CINT_PURE, 2u, names_ab, sig_add, cx_4_bank_3_add),
    ROW("deposit", 7u, CINT_OBSERVE, 1u, names_amount, sig_deposit, cx_4_bank_7_deposit),
    ROW("balance", 7u, CINT_PURE, 0u, NULL, sig_balance, cx_4_bank_7_balance),
    ROW("neg8", 4u, CINT_PURE, 1u, names_ab, sig_neg8, cx_4_bank_4_neg8),
    ROW("flip", 4u, CINT_PURE, 1u, &names_ab[1], sig_flip, cx_4_bank_4_flip),
    ROW("twice16", 7u, CINT_PURE, 1u, names_ab, sig_twice16, cx_4_bank_7_twice16),
    ROW("sum3", 4u, CINT_PURE, 1u, names_v, sig_sum3, cx_4_bank_4_sum3),
};
static const cint_module_table table0 = {&cm_4_bank, 0u, 7u, exports, NULL};
static const cint_module_table *const tables[1] = {&table0};
#if !defined(C17_NO_TABLE)
CINT_RT_EXPORT const cint_library cint_library_desc = {CINT_ABI_VERSION, 1u, tables};
#else
CINT_RT_EXPORT const cint_module_table *const *c17_tables = tables;
#endif
