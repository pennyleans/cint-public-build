/* cint-core-1, cint-rt-3, emitted by cintc */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cint_rt.h"

CINT_RT_INTERNAL const cint_site_info cg_5_hello_sites[5] = {
    {0u, 0u, ""},
    {1u, 1u, "fuel.charge"},
    {1u, 1u, "print"},
    {2u, 16u, "mul.checked.i64"},
    {3u, 1u, "print"},
};
CINT_RT_INTERNAL const cint_module cg_5_hello_module = {"hello.ci", 8u, 5u, cg_5_hello_sites};
CINT_RT_INTERNAL extern const cint_program cg_program;
CINT_RT_EXPORT const cint_module_info cm_5_hello = {CINT_ABI_VERSION, 0u, &cg_program, NULL};

static bool cg_5_hello_script(cint_ctx *ctx);

static bool cg_5_hello_script(cint_ctx *ctx)
{
    int64_t t0 = 0;
    int64_t t1 = 0;
    int64_t t2 = 0;
    int64_t t3 = 0;
    int64_t l0_answer = 0;
    (void)ctx;
    (void)l0_answer;
    if (!cint_rt_print_begin(ctx, (cint_site){0u, 2u})) goto fault;
    if (!cint_rt_print_bytes(ctx, "hello, world\n", 13u)) goto fault;
    if (!cint_rt_print_end(ctx)) goto fault;
    t0 = INT64_C(6);
    t1 = INT64_C(7);
    if (!cint_mul_i64(ctx, (cint_site){0u, 3u}, t0, t1, &t2)) goto fault;
    l0_answer = t2;
    t3 = l0_answer;
    if (!cint_rt_print_begin(ctx, (cint_site){0u, 4u})) goto fault;
    if (!cint_rt_print_bytes(ctx, "answer=", 7u)) goto fault;
    if (!cint_rt_print_value(ctx, CINT_TAG_I64, (uint64_t)t3)) goto fault;
    if (!cint_rt_print_bytes(ctx, "\n", 1u)) goto fault;
    if (!cint_rt_print_end(ctx)) goto fault;
    return true;
fault:
    return false;
}

CINT_RT_INTERNAL const cint_module_table cg_5_hello_table = {&cm_5_hello, 0u, 0u, NULL, NULL};

CINT_RT_INTERNAL cint_status cg_5_hello_run(cint_ctx *ctx, int64_t fuel)
{
    cint_status st = cint_rt_entry_begin(ctx, (cint_site){0u, 1u}, fuel, CINT_DEPTH_FROM_CONFIG);
    if (st != CINT_OK) {
        return st;
    }
    (void)cg_5_hello_script(ctx);
    return cint_rt_entry_end(ctx);
}
