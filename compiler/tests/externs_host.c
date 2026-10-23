/* compiler/tests/externs_host.c: the host of compiler/tests/golden/externs.ci in the golden suite
 * (compiler/tests/golden_suite.py PUBLIC_HOSTS; I/O services lane U2; SPEC-03 A-10, A-15;
 * rt/OPEN.md RT-OQ-70), linked with the program's C and the runtime object. It supplies the
 * program's externs through a host table whose signature hashes it takes from the program's
 * import descriptor, cint_imports_desc, enters the exports, and checks what each call returned,
 * the fault of a refused call, and the refusals of a host table that lacks a function or
 * declares another class. cli/tests/fixtures/extern_host.c is the same host of the library
 * `cint build --lib` writes, printing instead of checking. */
#include <stdio.h>
#include <string.h>

#include "cint_rt.h"

extern const cint_module_info cm_7_externs;
extern const cint_host_desc cint_imports_desc;
cint_status cx_7_externs_5_total(cint_ctx *ctx, int64_t fuel, int64_t p_key, int64_t *result);
cint_status cx_7_externs_6_pulled(cint_ctx *ctx, int64_t fuel, int64_t p_n, int64_t *result);
cint_status cx_7_externs_5_flush(cint_ctx *ctx, int64_t fuel);

static int passed, total;
static void check(const char *name, int ok)
{
    total++;
    passed += ok != 0;
    if (!ok) { fprintf(stderr, "FAILED %s\n", name); }
}

static int64_t g_pulls;
static char g_log[64];   /* what `put` wrote and when `sync` ran, in order */

static void note(const char *text, size_t n)
{
    size_t at = strlen(g_log);
    if (at + n < sizeof g_log) { memcpy(g_log + at, text, n); g_log[at + n] = '\0'; }
}

/* fetch(key, in keys, out buf): writes key + keys[i] into buf[i] and returns the count. */
static cint_status h_fetch(void *user, const cint_value *args, uint32_t nargs, cint_value *result)
{
    (void)user;
    if (nargs != 3u || args[1].as.view.shape[0] != args[2].as.view.shape[0]) return CINT_REFUSED;
    for (int64_t i = 0; i < args[1].as.view.shape[0]; i++) {
        const int64_t *k = (const int64_t *)((const uint8_t *)args[1].as.view.mem.u.host.ptr +
                                             i * args[1].as.view.byte_stride[0]);
        int64_t *b = (int64_t *)((uint8_t *)args[2].as.view.mem.u.host.ptr + i * args[2].as.view.byte_stride[0]);
        *b = args[0].as.i64 + *k;
    }
    result->as.i64 = args[1].as.view.shape[0];
    return CINT_OK;
}

static cint_status h_square(void *user, const cint_value *args, uint32_t nargs, cint_value *result)
{
    (void)user;
    (void)nargs;
    result->as.i64 = args[0].as.i64 * args[0].as.i64;
    return CINT_OK;
}

static cint_status h_sync(void *user, const cint_value *args, uint32_t nargs, cint_value *result)
{
    (void)user;
    (void)args;
    (void)nargs;
    (void)result;
    note("[sync]", 6u);
    return CINT_OK;
}

/* pull(n, strict): n > 0 the value 10 * n; 0 the error `closed` (tag 1); -1 refused by the host;
 * -2 a result outside the declared type (tag 3 of a set of 2). */
static cint_status h_pull(void *user, const cint_value *args, uint32_t nargs, cint_value *result)
{
    (void)user;
    (void)nargs;
    g_pulls++;
    if (args[0].as.i64 == -1) return CINT_BUSY;
    if (args[0].as.i64 == -2) {
        result->is_error = 1u;
        result->error = 3;
        return CINT_OK;
    }
    if (args[0].as.i64 == 0) {
        result->is_error = 1u;
        result->error = 1;
        return CINT_OK;
    }
    result->as.i64 = 10 * args[0].as.i64 + (args[1].as.i64 != 0 ? 1 : 0);
    return CINT_OK;
}

static cint_status h_put(void *user, const cint_value *args, uint32_t nargs, cint_value *result)
{
    (void)user;
    (void)nargs;
    (void)result;
    note((const char *)args[0].as.view.mem.u.host.ptr, (size_t)args[0].as.view.shape[0]);
    return CINT_OK;
}

/* The host table from the descriptor's rows: each name with its class and signature hash. */
static uint32_t table_of(cint_host_fn *fns, uint32_t cap)
{
    uint32_t n = 0u;
    for (uint32_t m = 0u; m < cint_imports_desc.module_count; m++) {
        const cint_host_table *t = cint_imports_desc.tables[m];
        for (uint32_t k = 0u; t != NULL && k < t->count && n < cap; k++) {
            const cint_host_import *r = &t->imports[k];
            cint_host_fn *f = &fns[n++];
            memset(f, 0, sizeof *f);
            f->effect = r->effect;
            cint_signature_hash(r->type_signature, (size_t)r->type_signature_len, f->signature);
            if (r->name.len == 5u && memcmp(r->name.bytes, "fetch", 5u) == 0) { f->name = "fetch"; f->call = h_fetch; }
            else if (r->name.len == 6u && memcmp(r->name.bytes, "square", 6u) == 0) { f->name = "square"; f->call = h_square; }
            else if (r->name.len == 4u && memcmp(r->name.bytes, "sync", 4u) == 0) { f->name = "sync"; f->call = h_sync; }
            else if (r->name.len == 4u && memcmp(r->name.bytes, "pull", 4u) == 0) { f->name = "pull"; f->call = h_pull; }
            else if (r->name.len == 3u && memcmp(r->name.bytes, "put", 3u) == 0) { f->name = "put"; f->call = h_put; }
            else { f->name = "?"; f->call = h_sync; }
        }
    }
    return n;
}

/* The held fault: its code, operation and I64 operand (0 for none), cleared. */
static int fault_is(cint_ctx *ctx, unsigned code, const char *operation, int64_t operand)
{
    static cint_fault_record record;
    int64_t v = 0;
    int ok;
    if (cint_ctx_fault(ctx, &record) != CINT_OK || record.code == 0u) return 0;
    if (record.operand_count > 0u && record.operands[0].len == 9u && record.operands[0].bytes[0] == CINT_TAG_I64) {
        for (int i = 8; i > 0; i--) v = (v << 8) | record.operands[0].bytes[i];
    }
    ok = record.code == code && record.operation_len == strlen(operation) &&
         memcmp(record.operation, operation, record.operation_len) == 0 && v == operand;
    (void)cint_ctx_clear_fault(ctx);
    return ok;
}

static int run_checks(void)
{
    static const char *const names[5] = {"fetch", "square", "sync", "pull", "put"};
    static const uint32_t classes[5] = {CINT_OBSERVE, CINT_PURE, CINT_UNRESTRICTED, CINT_OBSERVE, CINT_OBSERVE};
    static const uint32_t params[5] = {3u, 1u, 0u, 2u, 1u};
    cint_host_fn fns[8];
    cint_ctx_config config;
    cint_ctx *ctx = NULL;
    int64_t result = 0;
    uint32_t n = table_of(fns, 8u);
    const cint_host_table *t = cint_imports_desc.module_count == 1u ? cint_imports_desc.tables[0] : NULL;
    check("descriptor", cint_imports_desc.module_count == 1u && cint_imports_desc.reserved == 0u && t != NULL &&
          t->count == 5u && n == 5u);
    for (uint32_t k = 0u; t != NULL && k < t->count && k < 5u; k++) {
        const cint_host_import *r = &t->imports[k];
        check(names[k], r->name.len == strlen(names[k]) && memcmp(r->name.bytes, names[k], r->name.len) == 0 &&
              r->effect == classes[k] && r->param_count == params[k] && r->type_signature_len > 33u &&
              memcmp(r->type_signature + 4, "cint-core-1/type-signature/v1", 29u) == 0);
    }
    memset(&config, 0, sizeof config);
    config.size = (uint32_t)sizeof config;
    config.program = cm_7_externs.program;
    config.module = &cm_7_externs;
    config.host_count = n;
    config.host = fns;
    config.imports = &cint_imports_desc;
    check("create", cint_ctx_create(&config, &ctx) == CINT_OK);
    if (ctx == NULL) return 1;
    check("total 5", cx_7_externs_5_total(ctx, CINT_FUEL_UNBOUNDED, 5, &result) == CINT_OK && result == 24);
    check("pulled 4", cx_7_externs_6_pulled(ctx, CINT_FUEL_UNBOUNDED, 4, &result) == CINT_OK && result == 41);
    check("pulled 0", cx_7_externs_6_pulled(ctx, CINT_FUEL_UNBOUNDED, 0, &result) == CINT_OK && result == -1);
    check("pulled -1", cx_7_externs_6_pulled(ctx, CINT_FUEL_UNBOUNDED, -1, &result) == CINT_FAULT &&
          fault_is(ctx, CINT_E_UNSUPPORTED, "host.error.pull", CINT_BUSY));
    check("pulled -2", cx_7_externs_6_pulled(ctx, CINT_FUEL_UNBOUNDED, -2, &result) == CINT_FAULT &&
          fault_is(ctx, CINT_E_UNSUPPORTED, "host.result.pull", 0));
    check("flush", cx_7_externs_5_flush(ctx, CINT_FUEL_UNBOUNDED) == CINT_OK && strcmp(g_log, "done\n[sync]") == 0 &&
          g_pulls == 4);
    /* Fuel: the entry's unit, then one per host call (SPEC-01 IM-139). */
    check("fuel 3", cx_7_externs_5_total(ctx, 3, 5, &result) == CINT_OK && result == 24);
    check("fuel 2", cx_7_externs_5_total(ctx, 2, 5, &result) == CINT_FAULT && fault_is(ctx, CINT_E_FUEL, "fuel.charge", 0));
    cint_ctx_destroy(ctx);
    ctx = NULL;
    /* A table that lacks `put`, then one whose `square` is `observe`: refused at creation. */
    config.host_count = n - 1u;
    check("short table", cint_ctx_create(&config, &ctx) == CINT_REFUSED && ctx == NULL);
    config.host_count = n;
    fns[1].effect = CINT_OBSERVE;
    check("other class", cint_ctx_create(&config, &ctx) == CINT_REFUSED && ctx == NULL);
    fns[1].effect = CINT_PURE;
    fns[0].signature[0] ^= 1u;
    check("other signature", cint_ctx_create(&config, &ctx) == CINT_REFUSED && ctx == NULL);
    fns[0].signature[0] ^= 1u;
    /* No descriptor: the context is created and every extern call faults. */
    config.imports = NULL;
    check("no imports", cint_ctx_create(&config, &ctx) == CINT_OK && ctx != NULL);
    if (ctx != NULL) {
        check("unresolved", cx_7_externs_5_total(ctx, CINT_FUEL_UNBOUNDED, 5, &result) == CINT_FAULT &&
              fault_is(ctx, CINT_E_UNSUPPORTED, "host.call", 0));
        cint_ctx_destroy(ctx);
    }
    return total - passed;
}

int main(void)
{
    int failed = run_checks();
    printf("externs: %d of %d pass\n", passed, total);
    return failed;
}
