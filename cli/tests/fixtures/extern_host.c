/* A host of the library that cli/tests/run.py builds from compiler/tests/golden/externs.ci with `cint build --lib` (I/O
 * services lane U2; SPEC-03 A-10, A-15; rt/OPEN.md RT-OQ-70). It supplies the program's externs
 * through a host table whose signature hashes it takes from the library's import descriptor,
 * cint_imports_desc, enters the exports, and prints what each call returned, the fault of a
 * refused call, and the refusals of a host table that lacks a function or declares another
 * class. */
#include <stdio.h>
#include <string.h>

#include "externs.h"

static int64_t g_pulls;

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
    printf("sync\n");
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
    printf("put %.*s", (int)args[0].as.view.shape[0], (const char *)args[0].as.view.mem.u.host.ptr);
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

static void fault(cint_ctx *ctx)
{
    static cint_fault_record record;
    if (cint_ctx_fault(ctx, &record) != CINT_OK || record.code == 0u) {
        printf("no fault\n");
        return;
    }
    printf("fault %u %.*s", (unsigned)record.code, (int)record.operation_len, record.operation);
    if (record.operand_count > 0u && record.operands[0].len == 9u) {
        /* The status operand of host.error: an I64 tagged value, its tag then 8 bytes little-endian. */
        int64_t v = 0;
        for (int i = 8; i > 0; i--) v = (v << 8) | record.operands[0].bytes[i];
        printf(" %lld", (long long)v);
    }
    printf("\n");
    (void)cint_ctx_clear_fault(ctx);
}

int main(void)
{
    cint_host_fn fns[8];
    cint_ctx_config config;
    cint_ctx *ctx = NULL;
    int64_t result = 0;
    cint_status st;
    uint32_t n = table_of(fns, 8u);
    printf("imports %u modules %u\n", (unsigned)n, (unsigned)cint_imports_desc.module_count);
    memset(&config, 0, sizeof config);
    config.size = (uint32_t)sizeof config;
    config.program = cm_7_externs.program;
    config.module = &cm_7_externs;
    config.host_count = n;
    config.host = fns;
    config.imports = &cint_imports_desc;
    if (cint_ctx_create(&config, &ctx) != CINT_OK) return 2;
    st = cx_7_externs_5_total(ctx, CINT_FUEL_UNBOUNDED, 5, &result);
    printf("total 5: %d %lld\n", (int)st, (long long)result);
    st = cx_7_externs_6_pulled(ctx, CINT_FUEL_UNBOUNDED, 4, &result);
    printf("pulled 4: %d %lld\n", (int)st, (long long)result);
    st = cx_7_externs_6_pulled(ctx, CINT_FUEL_UNBOUNDED, 0, &result);
    printf("pulled 0: %d %lld\n", (int)st, (long long)result);
    st = cx_7_externs_6_pulled(ctx, CINT_FUEL_UNBOUNDED, -1, &result);
    printf("pulled -1: %d\n", (int)st);
    fault(ctx);
    st = cx_7_externs_6_pulled(ctx, CINT_FUEL_UNBOUNDED, -2, &result);
    printf("pulled -2: %d\n", (int)st);
    fault(ctx);
    st = cx_7_externs_5_flush(ctx, CINT_FUEL_UNBOUNDED);
    printf("flush: %d pulls %lld\n", (int)st, (long long)g_pulls);
    cint_ctx_destroy(ctx);
    /* A table that lacks `put`: refused. A table whose `square` is `observe`: refused. */
    config.host_count = n - 1u;
    printf("short table: %d\n", (int)cint_ctx_create(&config, &ctx));
    config.host_count = n;
    fns[1].effect = CINT_OBSERVE;
    printf("other class: %d\n", (int)cint_ctx_create(&config, &ctx));
    fns[1].effect = CINT_PURE;
    config.imports = NULL;
    printf("no imports: %d\n", (int)cint_ctx_create(&config, &ctx));
    st = cx_7_externs_5_total(ctx, CINT_FUEL_UNBOUNDED, 5, &result);
    printf("unresolved total: %d\n", (int)st);
    fault(ctx);
    cint_ctx_destroy(ctx);
    return 0;
}
