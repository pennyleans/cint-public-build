/* recovery_fixture.c: the runtime recovery fixture of slice 2 decision patch
 * D-25 (slice 2 plan task 2.4, correction pass), built and run by
 * harness/tests/test_harness.py on the msvc, gcc and clang legs, sanitized on
 * gcc and clang.
 *
 * A hand-written program module in the style of mod_basic.c, standing for
 * harness/recovery.ci, the D-24 probe:
 *
 *   1  // The D-24 probe: a store and a print, then a fault (SPEC-03 A-7, A-7b).
 *   2  I64 balance = 5;
 *   3  export void deposit_then_fault(I64 top) {
 *   4      balance = balance + 1;
 *   5      "partial\n";
 *   6      I64 next = top + 1;
 *   7  }
 *   8  export I64 read_balance() { return balance; }
 *   9  export void reset() { balance = 5; }
 *
 * The entries have the shape of cint-abi-1 wrappers (SPEC-03 A-12): context,
 * fuel, parameters, result pointer, with the depth limit taken from the
 * context. A host main below checks the steps of D-25 in order:
 *   1. the context is created with an output callback;
 *   2. deposit_then_fault(I64 MAX) returns CINT_FAULT, and the callback got
 *      exactly "partial\n" (8 bytes, SHA-256 95aebb28...181e);
 *   3. the record read with cint_ctx_fault (the runtime call beneath SPEC-03
 *      cint_fault_get, RT-5) has the fields cint_ref reports;
 *   4. read_balance returns CINT_FAULTED, with no output, the record unchanged;
 *   5. reset, the reinitializing entry, also returns CINT_FAULTED, so the fault
 *      is cleared first (A-7b);
 *   6. cint_ctx_clear_fault returns CINT_OK and the record code is 0;
 *   7. read_balance returns I64 6: the store is not rolled back (A-7, IM-121);
 *   8. reset, then read_balance, returns I64 5;
 *   9. a buffer registered before step 2 is still registered after step 6
 *      (clearing releases nothing, A-7a), and the host releases it and
 *      destroys the context on every path, with no sanitizer report.
 * Steps 7 and 8 use module state in the context (task 2.14): `balance` lives
 * in the block that cint_rt_state gives for the module's cint_state, as in
 * the C that B1 emits for a module-level variable (compiler/back_c.ci
 * c_state), so the store of line 4 survives the fault and the clear.
 *
 * Expected values (Reported: cint_ref, 2026-10-03; test_harness.py runs the
 * same command and compares): `python -m cint_ref run harness/recovery.ci
 * --entry deposit_then_fault --arg "I64 9223372036854775807" --path
 * harness/recovery.ci` gives outcome fault, stdout-bytes 8, stdout-sha256
 * 95aebb28195b8d737effe0df18d71d39c8d8ba6569286fd3930fbc9f9767181e,
 * fuel-consumed 1, E_OVERFLOW, add.checked.i64, operands I64
 * 9223372036854775807 and I64 1, exact 9223372036854775808, limit I64
 * 9223372036854775807, position harness/recovery.ci:6:20, stack depth 0.
 */
#include "cint_rt.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* The program module.                                                        */

static const cint_site_info sites[7] = {
    {0u, 0u, ""},                 /* 0: no site */
    {3u, 1u, "call.enter"},       /* 1: entry deposit_then_fault */
    {4u, 23u, "add.checked.i64"}, /* 2: balance + 1 (waits for module state) */
    {5u, 5u, "print"},            /* 3: "partial\n"; */
    {6u, 20u, "add.checked.i64"}, /* 4: top + 1 */
    {8u, 1u, "call.enter"},       /* 5: entry read_balance */
    {9u, 1u, "call.enter"},       /* 6: entry reset */
};
static const cint_module module0 = {"harness/recovery.ci", 19u, 7u, sites};
typedef struct state0 {
    int64_t g_balance;
} state0;
static const state0 state0_init = {INT64_C(5)};
static const cint_state_var state0_vars[1] = {{"balance", 7u, 0x14u, (uint32_t)offsetof(state0, g_balance), 0u}};
static const cint_state state0_desc = {0u, 1u, (uint32_t)sizeof(state0), 0u, &state0_init, state0_vars};
static const cint_module *const modules[1] = {&module0};
static const cint_program program = {1u, 0u, modules, NULL};

static cint_site site(uint32_t index)
{
    cint_site s;
    s.module = 0u;
    s.index = index;
    return s;
}

static cint_status deposit_then_fault(cint_ctx *ctx, int64_t fuel, int64_t top)
{
    int64_t next = 0, sum = 0;
    state0 *g;
    cint_status st = cint_rt_entry_begin(ctx, site(1u), fuel, CINT_DEPTH_FROM_CONFIG);
    if (st != CINT_OK) {
        return st;
    }
    g = (state0 *)cint_rt_state(ctx, &state0_desc);
    /* Line 4, `balance = balance + 1;`, stores into module state. */
    if (g != NULL && cint_add_i64(ctx, site(2u), g->g_balance, 1, &sum)) {
        g->g_balance = sum;
        if (cint_rt_print_begin(ctx, site(3u)) && cint_rt_print_bytes(ctx, "partial\n", 8u) && cint_rt_print_end(ctx)) {
            (void)cint_add_i64(ctx, site(4u), top, 1, &next);
        }
    }
    return cint_rt_entry_end(ctx);
}

/* Lines 8 and 9 read and store module state. */
static cint_status read_balance(cint_ctx *ctx, int64_t fuel, int64_t *result)
{
    state0 *g;
    cint_status st = cint_rt_entry_begin(ctx, site(5u), fuel, CINT_DEPTH_FROM_CONFIG);
    if (st != CINT_OK) {
        return st;
    }
    g = (state0 *)cint_rt_state(ctx, &state0_desc);
    if (g != NULL) {
        *result = g->g_balance;
    }
    return cint_rt_entry_end(ctx);
}

static cint_status reset(cint_ctx *ctx, int64_t fuel)
{
    state0 *g;
    cint_status st = cint_rt_entry_begin(ctx, site(6u), fuel, CINT_DEPTH_FROM_CONFIG);
    if (st != CINT_OK) {
        return st;
    }
    g = (state0 *)cint_rt_state(ctx, &state0_desc);
    if (g != NULL) {
        g->g_balance = 5;
    }
    return cint_rt_entry_end(ctx);
}

/* ------------------------------------------------------------------------- */
/* The host.                                                                  */

typedef struct capture {
    uint8_t bytes[64];
    size_t len;
    int calls;
} capture;

static cint_status capture_write(void *user, const uint8_t *bytes, size_t len)
{
    capture *c = (capture *)user;
    c->calls++;
    if (len > sizeof c->bytes - c->len) {
        return CINT_RESOURCE;
    }
    memcpy(c->bytes + c->len, bytes, len);
    c->len += len;
    return CINT_OK;
}

static int g_passed;
static int g_failed;

static void step(int n, int ok, const char *what)
{
    printf("step %d: %s: %s\n", n, ok ? "ok" : "FAILED", what);
    if (ok) {
        g_passed++;
    } else {
        g_failed++;
    }
}

static int rendered(const cint_tvalue *v, const char *want, int with_type)
{
    char text[64];
    size_t n = with_type ? cint_tvalue_render(v, text, sizeof text) : cint_tvalue_render_decimal(v, text, sizeof text);
    return n > 0u && n < sizeof text && strcmp(text, want) == 0;
}

static int record_matches_reference(const cint_fault_record *r)
{
    cint_position pos;
    return r->code == CINT_E_OVERFLOW && r->operation_len == 15u &&
           memcmp(r->operation, "add.checked.i64", 15u) == 0 && r->operand_count == 2u &&
           rendered(&r->operands[0], "I64 9223372036854775807", 1) && rendered(&r->operands[1], "I64 1", 1) &&
           r->has_exact && rendered(&r->exact, "9223372036854775808", 0) && r->has_limit &&
           rendered(&r->limit, "I64 9223372036854775807", 1) && cint_site_resolve(r->program, r->position, &pos) &&
           pos.path_len == 19u && memcmp(pos.path, "harness/recovery.ci", 19u) == 0 && pos.line == 6u &&
           pos.column == 20u && !r->has_address && r->stack_count == 0u;
}

int main(void)
{
    static cint_fault_record record, again;
    static int64_t host_words[4];
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL;
    cint_buffer_id held = 0u;
    int registered = 0;
    capture out;
    int64_t fuel = -1, value = 0;
    memset(&out, 0, sizeof out);
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = &program;
    cfg.output = capture_write;
    cfg.output_user = &out;

    step(1, cint_ctx_create(&cfg, &ctx) == CINT_OK && ctx != NULL, "the context is created with an output callback");
    if (ctx == NULL) {
        goto done;
    }
    registered = cint_buffer_register_bytes(ctx, host_words, (int64_t)sizeof host_words, CINT_VIEW_WRITE, &held) == CINT_OK;
    if (!registered) {
        printf("setup: FAILED: the host buffer of step 9 is not registered\n");
        g_failed++;
        goto done;
    }

    step(2,
         deposit_then_fault(ctx, CINT_FUEL_UNBOUNDED, INT64_MAX) == CINT_FAULT && out.calls == 1 && out.len == 8u &&
             memcmp(out.bytes, "partial\n", 8u) == 0,
         "deposit_then_fault returns CINT_FAULT; the callback got exactly \"partial\\n\"");

    step(3,
         cint_ctx_fault(ctx, &record) == CINT_OK && record_matches_reference(&record) &&
             cint_fuel_consumed(ctx, &fuel) == CINT_OK && fuel == 1,
         "cint_ctx_fault gives the record cint_ref reports, fuel consumed 1");

    step(4,
         read_balance(ctx, CINT_FUEL_UNBOUNDED, &value) == CINT_FAULTED && out.calls == 1 && out.len == 8u &&
             cint_ctx_fault(ctx, &again) == CINT_OK && memcmp(&record, &again, sizeof record) == 0,
         "read_balance returns CINT_FAULTED, writes nothing, leaves the record");

    step(5,
         reset(ctx, CINT_FUEL_UNBOUNDED) == CINT_FAULTED && out.calls == 1 && cint_ctx_fault(ctx, &again) == CINT_OK &&
             memcmp(&record, &again, sizeof record) == 0,
         "reset returns CINT_FAULTED before the fault is cleared (A-7b)");

    step(6,
         cint_ctx_clear_fault(ctx) == CINT_OK && cint_ctx_fault(ctx, &again) == CINT_OK && again.code == 0u,
         "cint_ctx_clear_fault returns CINT_OK; the record code is 0");

    step(7, read_balance(ctx, CINT_FUEL_UNBOUNDED, &value) == CINT_OK && value == 6,
         "read_balance returns I64 6: the store before the fault is not rolled back (A-7, IM-121)");
    value = 0;
    step(8,
         reset(ctx, CINT_FUEL_UNBOUNDED) == CINT_OK && read_balance(ctx, CINT_FUEL_UNBOUNDED, &value) == CINT_OK &&
             value == 5 && out.calls == 1,
         "reset, then read_balance, returns I64 5");

    /* Release succeeds only for a live registration, so a first CINT_OK shows
     * that clearing the fault kept it, and a second release is refused. */
    step(9,
         cint_buffer_release(ctx, held) == CINT_OK && cint_buffer_release(ctx, held) == CINT_REFUSED,
         "the buffer registered before step 2 survived the clear; the host released it");
    registered = 0;

done:
    /* Cleanup on every path: the host releases what it acquired (A-7b). */
    if (ctx != NULL) {
        if (registered) {
            (void)cint_buffer_release(ctx, held);
        }
        cint_ctx_destroy(ctx);
    }
    printf("recovery: %d of 9 steps passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 && g_passed == 9 ? 0 : 1;
}
