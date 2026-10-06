/* mod_kernel.c: a hand-written program with kernel dispatches for
 * harness/tests/test_harness.py, the cases of `.expect` format 4 (SPEC-09
 * CONF-11 rule 13; box 09 ruling R9). It follows the observer interface
 * cint-observe-1 (SPEC-09 CONF-13) and stands for this CINT source (module
 * path harness/kernel.ci):
 *
 *   1  kernel scale[n](in I64[n] u, in I64 k, out I64[n] v) over [i: n] {
 *   2      v[i] = (u[i] + 1) * k;
 *   3  }
 *   4  kernel copy2[h, w](in I32[h, w] u, out I32[h, w] v) over [y: h, x: w] {
 *   5      v[y, x] = u[y, x];
 *   6  }
 *   7  kernel total[n](in I32[n] u, out I32 s) over [i: n] {
 *   8      reduce s = sum(u[i]);
 *   9  }
 *  10  I64 runs = 0;
 *  11  export I64 hot(I64 k) {
 *  12      I64[4] u;
 *  13      I64[4] v;
 *  14      u[2] = 4611686018427387903;
 *  15      scale(u, 1, v);
 *  16      runs = runs + 1;
 *  17      scale(u, k, v);
 *  18      return v[3];
 *  19  }
 *  20  export I32 overlap() {
 *  21      I32[8, 16] m;
 *  22      copy2(m[.., 0..8], m[.., 8..16]);
 *  23      return m[0, 8];
 *  24  }
 *  25  export I32 sum_max() {
 *  26      I32[2] u;
 *  27      u[0] = 2147483647;
 *  28      u[1] = 1;
 *  29      I32 s = 0;
 *  30      total(u, s);
 *  31      return s;
 *  32  }
 *
 * The entries call the cint_rt helpers the way generated code does (rt/cint_rt.h
 * section 14): a dispatch takes its number, checks its array arguments for
 * aliasing (F-5 check 7), charges one fuel unit per work item (check 9), pushes
 * its site for the work items and the epilogue, and on a fault gives the record
 * its kernel address. A faulting dispatch is not popped, because the record
 * already holds the stack. The entries `read_alias`, `bad_kernel` and
 * `bool_alias` stand for no source: they reach a read descriptor, malformed
 * kernel names and a Bool element type, so that the harness's checks of the
 * format 4 contract are exercised.
 */
#include "cint_rt.h"

#define FIXTURE_EXPORT CINT_RT_EXPORT

static const cint_site_info sites[12] = {
    {0u, 0u, ""},                     /* 0: no site */
    {2u, 18u, "add.checked.i64"},     /* 1: u[i] + 1 */
    {2u, 23u, "mul.checked.i64"},     /* 2: (u[i] + 1) * k */
    {8u, 16u, "sum.checked.i32.i32"}, /* 3: sum(u[i]) */
    {11u, 1u, "call.enter"},          /* 4: entry hot */
    {15u, 5u, "bind.alias"},          /* 5: scale(u, 1, v) */
    {16u, 17u, "add.checked.i64"},    /* 6: runs + 1 */
    {17u, 5u, "bind.alias"},          /* 7: scale(u, k, v) */
    {20u, 1u, "call.enter"},          /* 8: entry overlap */
    {22u, 5u, "bind.alias"},          /* 9: copy2(m[.., 0..8], m[.., 8..16]) */
    {25u, 1u, "call.enter"},          /* 10: entry sum_max */
    {30u, 5u, "bind.alias"},          /* 11: total(u, s) */
};
static const cint_module module0 = {"harness/kernel.ci", 17u, 12u, sites};
static const cint_module *const modules[1] = {&module0};
static const cint_program program = {1u, 0u, modules, NULL};

typedef struct state0 {
    int64_t runs;
} state0;
static const state0 state0_init = {0};
static const cint_state_var state0_vars[1] = {{"runs", 4u, CINT_TAG_I64, (uint32_t)offsetof(state0, runs), 0u}};
static const cint_state state0_desc = {0u, 1u, (uint32_t)sizeof(state0), 0u, &state0_init, state0_vars};
static const cint_state *const states[1] = {&state0_desc};

static cint_site site(uint32_t index)
{
    cint_site s;
    s.module = 0u;
    s.index = index;
    return s;
}

/* A view of rank 1 or 2 (rt/cint_rt.h section 14). */
static cint_vdesc view(void *base, int64_t origin, int64_t rank, int64_t n0, int64_t n1, int64_t s0, int64_t s1)
{
    cint_vdesc v = {base, origin, rank, {n0, n1, 0, 0}, {s0, s1, 0, 0}};
    return v;
}

/* Lines 1 to 3: scale(u, k, v) dispatched at `at` over 4 work items; false
 * after a fault. The steps of work item i (SPEC-01 IM-113, IM-114): the place
 * v[i] 0, u[i] 1, + 2, * 3; the subscripts cannot fault here. */
static bool scale(cint_ctx *ctx, cint_site at, int64_t *u, int64_t k, int64_t *v)
{
    static const char kernel[] = "harness.kernel.scale";
    int64_t number = cint_rt_dispatch_number(ctx);
    cint_vdesc pu = view(u, 0, 1, 4, 0, 1, 0);
    cint_vdesc pv = view(v, 0, 1, 4, 0, 1, 0);
    int64_t i, t = 0;
    /* v (parameter 2) against u (parameter 0): separate arrays, disjoint. */
    if (!cint_rt_dispatch_alias(ctx, at, 2u, 0u, &pv, &pu, CINT_TAG_I64, 3u) || !cint_fuel_charge(ctx, at, 4u)) {
        cint_rt_dispatch_fault(ctx, kernel, number, CINT_PHASE_ENTRY, 0, 0);
        return false;
    }
    cint_rt_dispatch_push(ctx, at);
    for (i = 0; i < 4; i++) {
        if (!cint_add_i64(ctx, site(1u), u[i], 1, &t)) {
            cint_rt_dispatch_fault(ctx, kernel, number, CINT_PHASE_WORK_ITEM, i, 2);
            return false;
        }
        if (!cint_mul_i64(ctx, site(2u), t, k, &v[i])) {
            cint_rt_dispatch_fault(ctx, kernel, number, CINT_PHASE_WORK_ITEM, i, 3);
            return false;
        }
    }
    cint_rt_dispatch_pop(ctx);
    return true;
}

static const uint32_t sig_hot[3] = {CINT_TAG_I64, 1u, CINT_TAG_I64};
static cint_status entry_hot(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args, uint64_t *result)
{
    int64_t k = cint_rt_sext(args[0], 64u);
    int64_t u[4] = {0, 0, INT64_C(4611686018427387903), 0};
    int64_t v[4] = {0, 0, 0, 0};
    state0 *g;
    cint_status st = cint_rt_entry_begin(ctx, site(4u), fuel, depth);
    if (st != CINT_OK) {
        return st;
    }
    g = (state0 *)cint_rt_state(ctx, &state0_desc);
    if (g != NULL && scale(ctx, site(5u), u, 1, v) && cint_add_i64(ctx, site(6u), g->runs, 1, &g->runs) &&
        scale(ctx, site(7u), u, k, v)) {
        *result = (uint64_t)v[3];
    }
    return cint_rt_entry_end(ctx);
}

/* Line 22 (as kernel/alias_uncertain_columns): the two column blocks of m
 * overlap in their bounding intervals and gcd 1 proves nothing (SPEC-02 A-5),
 * so the dispatch of copy2 faults at entry with both descriptors, and its work
 * items never run. The other callers pass their own name, type and views. */
static void copy2_alias(cint_ctx *ctx, const char *name, uint32_t elem, const cint_vdesc *v, const cint_vdesc *u,
                        uint32_t writable)
{
    int64_t number = cint_rt_dispatch_number(ctx);
    if (!cint_rt_dispatch_alias(ctx, site(9u), 1u, 0u, v, u, elem, writable)) {
        cint_rt_dispatch_fault(ctx, name, number, CINT_PHASE_ENTRY, 0, 0);
    }
}

static const uint32_t sig_overlap[2] = {CINT_TAG_I32, 0u};
static cint_status entry_overlap(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args,
                                 uint64_t *result)
{
    int32_t m[8 * 16] = {0};
    cint_vdesc u = view(m, 0, 2, 8, 8, 16, 1);
    cint_vdesc v = view(m, 8, 2, 8, 8, 16, 1);
    cint_status st = cint_rt_entry_begin(ctx, site(8u), fuel, depth);
    (void)args;
    if (st != CINT_OK) {
        return st;
    }
    copy2_alias(ctx, "harness.kernel.copy2", CINT_TAG_I32, &v, &u, 3u);
    if (!cint_rt_faulted(ctx)) {
        *result = (uint64_t)(int64_t)m[8];
    }
    return cint_rt_entry_end(ctx);
}

static const uint32_t sig_sum_max[2] = {CINT_TAG_I32, 0u};
static cint_status entry_sum_max(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args,
                                 uint64_t *result)
{
    static const char kernel[] = "harness.kernel.total";
    int32_t u[2] = {INT32_C(2147483647), 1};
    cint_zacc acc = {{0u, 0u, 0u, 0u}};
    uint64_t s = 0u;
    int64_t i, number;
    cint_status st = cint_rt_entry_begin(ctx, site(10u), fuel, depth);
    (void)args;
    if (st != CINT_OK) {
        return st;
    }
    number = cint_rt_dispatch_number(ctx);
    if (!cint_fuel_charge(ctx, site(11u), 2u)) {
        cint_rt_dispatch_fault(ctx, kernel, number, CINT_PHASE_ENTRY, 0, 0);
        return cint_rt_entry_end(ctx);
    }
    cint_rt_dispatch_push(ctx, site(11u));
    for (i = 0; i < 2; i++) {  /* each work item's contribution to `reduce s` */
        cint_zacc_add(&acc, CINT_TAG_I32, (uint64_t)(int64_t)u[i]);
    }
    /* The epilogue: the exact sum, 2147483648, does not fit I32. */
    if (!cint_rt_zacc_fit(ctx, site(3u), "sum", CINT_TAG_I32, CINT_TAG_I32, 2, &acc, &s)) {
        cint_rt_dispatch_fault(ctx, kernel, number, CINT_PHASE_EPILOGUE, 0, 0);
        return cint_rt_entry_end(ctx);
    }
    cint_rt_dispatch_pop(ctx);
    *result = s;
    return cint_rt_entry_end(ctx);
}

/* No source: two rank-1 U8 views of one buffer, written p over read q. */
static const uint32_t sig_read_alias[2] = {CINT_TAG_I32, 0u};
static cint_status entry_read_alias(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args,
                                    uint64_t *result)
{
    uint8_t b[8] = {0};
    cint_vdesc q = view(b, 0, 1, 4, 0, 1, 0);
    cint_vdesc p = view(b, 2, 1, 4, 0, 1, 0);
    cint_status st = cint_rt_entry_begin(ctx, site(8u), fuel, depth);
    (void)args;
    (void)result;
    if (st != CINT_OK) {
        return st;
    }
    copy2_alias(ctx, "harness.kernel.copy2", CINT_TAG_U8, &p, &q, 1u);
    return cint_rt_entry_end(ctx);
}

/* No source: the alias fault of `overlap` with the kernel name `which`
 * selects; only name 3 is a kernel-name of CONF-11. */
static const uint32_t sig_bad_kernel[3] = {CINT_TAG_I32, 1u, CINT_TAG_I64};
static cint_status entry_bad_kernel(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args,
                                    uint64_t *result)
{
    static const char *const names[4] = {"copy2", "harness..copy2", "harness.kernel.2copy", "harness.kernel.copy2"};
    int64_t which = cint_rt_sext(args[0], 64u);
    int32_t m[8 * 16] = {0};
    cint_vdesc u = view(m, 0, 2, 8, 8, 16, 1);
    cint_vdesc v = view(m, 8, 2, 8, 8, 16, 1);
    cint_status st = cint_rt_entry_begin(ctx, site(8u), fuel, depth);
    (void)result;
    if (st != CINT_OK) {
        return st;
    }
    copy2_alias(ctx, names[which >= 0 && which < 4 ? which : 0], CINT_TAG_I32, &v, &u, 3u);
    return cint_rt_entry_end(ctx);
}

/* No source: descriptors of Bool elements, which format 4 cannot write. */
static const uint32_t sig_bool_alias[2] = {CINT_TAG_I32, 0u};
static cint_status entry_bool_alias(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args,
                                    uint64_t *result)
{
    bool b[8] = {false};
    cint_vdesc q = view(b, 0, 1, 4, 0, 1, 0);
    cint_vdesc p = view(b, 2, 1, 4, 0, 1, 0);
    cint_status st = cint_rt_entry_begin(ctx, site(8u), fuel, depth);
    (void)args;
    (void)result;
    if (st != CINT_OK) {
        return st;
    }
    copy2_alias(ctx, "harness.kernel.copy2", CINT_TAG_BOOL, &p, &q, 3u);
    return cint_rt_entry_end(ctx);
}

/* Module order, then declaration order (X-2). */
static const cint_observer_entry entries[6] = {
    {0u, 3u, "hot", sig_hot, entry_hot},
    {0u, 7u, "overlap", sig_overlap, entry_overlap},
    {0u, 7u, "sum_max", sig_sum_max, entry_sum_max},
    {0u, 10u, "read_alias", sig_read_alias, entry_read_alias},
    {0u, 10u, "bad_kernel", sig_bad_kernel, entry_bad_kernel},
    {0u, 10u, "bool_alias", sig_bool_alias, entry_bool_alias},
};
FIXTURE_EXPORT const cint_observer cint_observer_desc = {6u, 0u, &program, entries};
FIXTURE_EXPORT const uint32_t cint_program_abi = CINT_ABI_VERSION;
FIXTURE_EXPORT const cint_state_table cint_observer_state = {1u, 0u, states};
