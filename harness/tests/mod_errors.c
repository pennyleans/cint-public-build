/* mod_errors.c: a hand-written program with error results for
 * harness/tests/test_harness.py. It follows the observer interface
 * cint-observe-1 (SPEC-09 CONF-13) and leaves its error results with
 * cint_rt_error_result (rt/cint_rt.h 6c'; rt/OPEN.md RT-OQ-33). It stands for
 * this CINT source (module path harness/errors.ci):
 *
 *   1  error IoError { closed, full }
 *   2  error ParseError : U8 { empty, bad_digit, full }
 *   3  error IoOrParse : U32 = IoError | ParseError;
 *   4  I64 calls = 0;
 *   5  export IoError!I64 open(I64 n) { calls = calls + 1; if (n < 0) { return .full; } return n; }
 *   6  export ParseError!void parse() { "bad\n"; return .bad_digit; }
 *   7  export IoOrParse!void either(Bool io) { if (io) { return IoError.full; } return ParseError.full; }
 *   8  export ArithError!I8 add8(I8 a, I8 b) { return try add_result(a, b); }
 *
 * IoOrParse numbers IoError.closed 1, IoError.full 2, ParseError.empty 3,
 * ParseError.bad_digit 4 and ParseError.full 5, and names the two `full`
 * values with their sets (SPEC-04 LS-97). The entry `bad_set` leaves an error
 * result whose set has a signed underlying type, so that the harness's check
 * of the contract is exercised.
 */
#include "cint_rt.h"

#include <stddef.h>

#define FIXTURE_EXPORT CINT_RT_EXPORT

static const cint_site_info sites[7] = {
    {0u, 0u, ""},                 /* 0: no site */
    {5u, 1u, "call.enter"},       /* 1: entry open */
    {5u, 48u, "add.checked.i64"}, /* 2: calls + 1 */
    {6u, 1u, "call.enter"},       /* 3: entry parse */
    {6u, 34u, "print"},           /* 4: "bad\n"; */
    {7u, 1u, "call.enter"},       /* 5: entry either */
    {8u, 1u, "call.enter"},       /* 6: entry add8 */
};
static const cint_module module0 = {"harness/errors.ci", 17u, 7u, sites};
static const cint_module *const modules[1] = {&module0};
static const cint_program program = {1u, 0u, modules, NULL};

typedef struct state0 {
    int64_t g_calls;
} state0;
static const state0 state0_init = {INT64_C(0)};
static const cint_state_var state0_vars[1] = {{"calls", 5u, CINT_TAG_I64, (uint32_t)offsetof(state0, g_calls), 0u}};
static const cint_state state0_desc = {0u, 1u, (uint32_t)sizeof(state0), 0u, &state0_init, state0_vars};
static const cint_state *const states[1] = {&state0_desc};

static const char *const io_values[2] = {"closed", "full"};
static const cint_error_set io_error = {"harness.errors.IoError", CINT_TAG_U16, 2u, io_values};
static const char *const parse_values[3] = {"empty", "bad_digit", "full"};
static const cint_error_set parse_error = {"harness.errors.ParseError", CINT_TAG_U8, 3u, parse_values};
static const char *const either_values[5] = {"closed", "IoError.full", "empty", "bad_digit", "ParseError.full"};
static const cint_error_set io_or_parse = {"harness.errors.IoOrParse", CINT_TAG_U32, 5u, either_values};
static const char *const arith_values[4] = {"overflow", "div_zero", "shift", "narrow"};
static const cint_error_set arith_error = {"ArithError", CINT_TAG_U16, 4u, arith_values};  /* SPEC-01 IM-188 */
static const char *const bad_values[1] = {"odd"};
static const cint_error_set bad_error = {"harness.errors.Bad", CINT_TAG_I16, 1u, bad_values};

static cint_site site(uint32_t index)
{
    cint_site s;
    s.module = 0u;
    s.index = index;
    return s;
}

static const uint32_t sig_open[3] = {CINT_TAG_I64, 1u, CINT_TAG_I64};
static cint_status entry_open(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args,
                              uint64_t *result)
{
    int64_t n = cint_rt_sext(args[0], 64u);
    state0 *g;
    cint_status st = cint_rt_entry_begin(ctx, site(1u), fuel, depth);
    if (st != CINT_OK) {
        return st;
    }
    g = (state0 *)cint_rt_state(ctx, &state0_desc);
    if (g != NULL && cint_add_i64(ctx, site(2u), g->g_calls, 1, &g->g_calls)) {
        if (n < 0) {
            cint_rt_error_result(ctx, &io_error, 2u);
        } else {
            *result = (uint64_t)n;
        }
    }
    return cint_rt_entry_end(ctx);
}

static const uint32_t sig_parse[2] = {0u, 0u};
static cint_status entry_parse(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args,
                               uint64_t *result)
{
    cint_status st = cint_rt_entry_begin(ctx, site(3u), fuel, depth);
    (void)args;
    (void)result;
    if (st != CINT_OK) {
        return st;
    }
    if (cint_rt_print_begin(ctx, site(4u)) && cint_rt_print_bytes(ctx, "bad\n", 4u) && cint_rt_print_end(ctx)) {
        cint_rt_error_result(ctx, &parse_error, 2u);
    }
    return cint_rt_entry_end(ctx);
}

static const uint32_t sig_either[3] = {0u, 1u, CINT_TAG_BOOL};
static cint_status entry_either(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args,
                                uint64_t *result)
{
    cint_status st = cint_rt_entry_begin(ctx, site(5u), fuel, depth);
    (void)result;
    if (st != CINT_OK) {
        return st;
    }
    cint_rt_error_result(ctx, &io_or_parse, (args[0] & 1u) != 0u ? 2u : 5u);
    return cint_rt_entry_end(ctx);
}

static const uint32_t sig_add8[4] = {CINT_TAG_I8, 2u, CINT_TAG_I8, CINT_TAG_I8};
static cint_status entry_add8(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args,
                              uint64_t *result)
{
    int8_t r = 0;
    uint16_t e;
    cint_status st = cint_rt_entry_begin(ctx, site(6u), fuel, depth);
    if (st != CINT_OK) {
        return st;
    }
    e = cint_add_result_i8((int8_t)cint_rt_sext(args[0], 8u), (int8_t)cint_rt_sext(args[1], 8u), &r);
    if (e != 0u) {
        cint_rt_error_result(ctx, &arith_error, e);
    } else {
        *result = (uint64_t)(int64_t)r;
    }
    return cint_rt_entry_end(ctx);
}

/* An error result whose set has the underlying type I16, which LS-93 does not
 * allow: a contract breach the harness must report instead of printing it. */
static const uint32_t sig_bad_set[2] = {0u, 0u};
static cint_status entry_bad_set(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args,
                                 uint64_t *result)
{
    cint_status st = cint_rt_entry_begin(ctx, site(1u), fuel, depth);
    (void)args;
    (void)result;
    if (st != CINT_OK) {
        return st;
    }
    cint_rt_error_result(ctx, &bad_error, 1u);
    return cint_rt_entry_end(ctx);
}

static const cint_observer_entry entries[5] = {
    {0u, 4u, "open", sig_open, entry_open},
    {0u, 5u, "parse", sig_parse, entry_parse},
    {0u, 6u, "either", sig_either, entry_either},
    {0u, 4u, "add8", sig_add8, entry_add8},
    {0u, 7u, "bad_set", sig_bad_set, entry_bad_set},
};
FIXTURE_EXPORT const cint_observer cint_observer_desc = {5u, 0u, &program, entries};
FIXTURE_EXPORT const cint_state_table cint_observer_state = {1u, 0u, states};
FIXTURE_EXPORT const uint32_t cint_program_abi = CINT_ABI_VERSION;
