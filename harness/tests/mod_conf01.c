/* mod_conf01.c: a hand-written stand-in for the seed's output for
 * conformance/arith/add_i64_overflow.ci (SPEC-09 9.2 CONF-01), built as a
 * shared library and observed by cint-harness in harness/tests/test_harness.py.
 * It follows the observer interface cint-observe-1 (SPEC-09 CONF-13). The test
 * block is the entry `main`; it takes no arguments and returns no value.
 */
#include "cint_rt.h"

#define FIXTURE_EXPORT CINT_RT_EXPORT

static const cint_site_info sites[3] = {
    {0u, 0u, ""},                 /* 0: no site */
    {2u, 1u, "call.enter"},       /* 1: the test block (the entry) */
    {5u, 15u, "add.checked.i64"}, /* 2: a + b */
};
static const cint_module module0 = {"arith/add_i64_overflow.ci", 25u, 3u, sites};
static const cint_module *const modules[1] = {&module0};
static const cint_program program = {1u, 0u, modules, NULL};
static const uint32_t sig_main[2] = {0u, 0u};

static cint_status entry_main(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args,
                              uint64_t *result)
{
    static const cint_site entry_site = {0u, 1u};
    static const cint_site add_site = {0u, 2u};
    int64_t a;
    int64_t b;
    int64_t c = 0;
    cint_status st = cint_rt_entry_begin(ctx, entry_site, fuel, depth);
    (void)args;
    (void)result;
    if (st != CINT_OK) {
        return st;
    }
    a = INT64_MAX;
    b = 1;
    if (!cint_add_i64(ctx, add_site, a, b, &c)) {
        goto fault;
    }
    (void)c;
fault:
    return cint_rt_entry_end(ctx);
}

static const cint_observer_entry entries[1] = {{0u, 4u, "main", sig_main, entry_main}};
FIXTURE_EXPORT const cint_observer cint_observer_desc = {1u, 0u, &program, entries};
FIXTURE_EXPORT const uint32_t cint_program_abi = CINT_ABI_VERSION;
