/* target_cint.c: bench_target.h for a cint program's exported function.
 *
 * Linked with a program's emitted C (a library: no `main`) and the runtime.
 * The function is found by name in cint_observer_desc, whose type signature
 * must be (I64, ..., I64) -> I64, and called through its cx wrapper from
 * cint_library_desc, the call a host makes (SPEC-03 5.6). One context serves
 * every call; it is created by bench_open, outside any timed interval.
 */
#include <stdlib.h>
#include <string.h>

#include "cint_rt.h"
#include "bench_target.h"

extern const cint_observer cint_observer_desc;
extern const cint_library cint_library_desc;

typedef cint_status (*cx0)(cint_ctx *, int64_t, int64_t *);
typedef cint_status (*cx1)(cint_ctx *, int64_t, int64_t, int64_t *);
typedef cint_status (*cx2)(cint_ctx *, int64_t, int64_t, int64_t, int64_t *);
typedef cint_status (*cx3)(cint_ctx *, int64_t, int64_t, int64_t, int64_t, int64_t *);
typedef cint_status (*cx4)(cint_ctx *, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t *);

struct bench_target {
    cint_ctx *ctx;
    cint_entry_fn call;
    uint32_t nargs;
};

static bool same_name(const char *bytes, uint32_t len, const char *name)
{
    size_t n = strlen(name);
    return bytes != NULL && len == n && memcmp(bytes, name, n) == 0;
}

/* The one observer entry named `entry`, or NULL when none or two share it. */
static const cint_observer_entry *find_observer(const char *entry, const char **why)
{
    const cint_observer_entry *found = NULL;
    for (uint32_t i = 0u; i < cint_observer_desc.entry_count; i++) {
        const cint_observer_entry *e = &cint_observer_desc.entries[i];
        if (!same_name(e->name, e->name_len, entry)) continue;
        if (found != NULL) {
            *why = "two modules export a function of that name";
            return NULL;
        }
        found = e;
    }
    if (found == NULL) *why = "the program exports no function of that name";
    return found;
}

/* The cx wrapper of module `module`'s export `entry`, or NULL. */
static cint_entry_fn find_call(uint32_t module, const char *entry)
{
    if (module >= cint_library_desc.module_count) return NULL;
    const cint_module_table *t = cint_library_desc.modules[module];
    for (uint32_t i = 0u; t != NULL && i < t->export_count; i++) {
        const cint_export *x = &t->exports[i];
        if (x->kind == CINT_EXPORT_FUNCTION && same_name(x->name.bytes, x->name.len, entry)) return x->call;
    }
    return NULL;
}

bench_target *bench_open(const char *entry, uint32_t nargs, const char **why)
{
    const cint_observer_entry *e = find_observer(entry, why);
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL;
    bench_target *t = NULL;
    if (e == NULL) return NULL;
    if (nargs > BENCH_MAX_ARGS || e->types[0] != CINT_TAG_I64 || e->types[1] != nargs) {
        *why = "the function's type signature is not that number of I64 arguments and an I64 result";
        return NULL;
    }
    for (uint32_t i = 0u; i < nargs; i++) {
        if (e->types[2u + i] != CINT_TAG_I64) {
            *why = "an argument of the function is not an I64";
            return NULL;
        }
    }
    cint_entry_fn call = find_call(e->module, entry);
    if (call == NULL) {
        *why = "cint_library_desc has no cx wrapper for the function";
        return NULL;
    }
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = cint_observer_desc.program;
    if (cint_ctx_create(&cfg, &ctx) != CINT_OK) {
        *why = "cint_ctx_create refused the program";
        return NULL;
    }
    t = (bench_target *)malloc(sizeof *t);
    if (t == NULL) {
        cint_ctx_destroy(ctx);
        *why = "out of memory";
        return NULL;
    }
    t->ctx = ctx;
    t->call = call;
    t->nargs = nargs;
    return t;
}

int bench_call(bench_target *t, int64_t fuel, const int64_t *a, int64_t *out)
{
    cint_status st = CINT_REFUSED;
    switch (t->nargs) {
    case 0: st = ((cx0)t->call)(t->ctx, fuel, out); break;
    case 1: st = ((cx1)t->call)(t->ctx, fuel, a[0], out); break;
    case 2: st = ((cx2)t->call)(t->ctx, fuel, a[0], a[1], out); break;
    case 3: st = ((cx3)t->call)(t->ctx, fuel, a[0], a[1], a[2], out); break;
    case 4: st = ((cx4)t->call)(t->ctx, fuel, a[0], a[1], a[2], a[3], out); break;
    default: break;
    }
    return st == CINT_OK ? 0 : (int)st;
}

int64_t bench_fuel(const bench_target *t)
{
    int64_t used = -1;
    if (cint_fuel_consumed(t->ctx, &used) != CINT_OK) return -1;
    return used;
}

void bench_close(bench_target *t)
{
    if (t == NULL) return;
    cint_ctx_destroy(t->ctx);
    free(t);
}
