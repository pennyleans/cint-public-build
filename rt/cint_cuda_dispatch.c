/* cint_cuda_dispatch.c: the dispatch path of a gpu-cuda build (rt/cint_cuda.h; box 11 unit 4).
 *
 * Status: Proposed, with rt/cint_cuda.h. A module's C file compiled with CINT_GPU_CUDA calls
 * these functions from a kernel's function at its first work-item (compiler/back_c.ci): the
 * entry checks, the dispatch charge, staging and the push have run on the host, as on
 * cpu-c17, and the host's item loop then runs at most the one work-item F-11 names. Design
 * note: docs/design/notes/2026-10-05-box11-cuda-backend.md, sections 4.2, 5 and 6.
 *
 * A context's CUDA handle (ordinal 0: SPEC-03 A-16a, a gpu-cuda library with device_count 0)
 * and the modules and entries it has loaded live in the context's device slot
 * (cint_rt_internal.h) from its first dispatch to cint_ctx_destroy. A dispatch page-locks and
 * maps the pages its views reach (SPEC-03 M-28a), takes mapped scratch for the charges C(i),
 * the contributions and the presence marks (B-16c), and releases both in
 * cint_cuda_dispatch_end. A failed driver call ends the entry CINT_RESOURCE with a boundary
 * record, host.resource (B-16e); the driver's error stays in the handle's
 * cint_cuda_last_error, a diagnostic, never canonical state.
 *
 * CINT_CUDA_BLOCK, when defined, is the block size of every launch: the runner's schedule
 * (SPEC-02 Q-3; 32 or 256). Undefined or 0, the default of 256 (B-16a). */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L /* sysconf */
#endif

#include "cint_cuda.h"
#include "cint_rt_internal.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#ifndef CINT_CUDA_BLOCK
#define CINT_CUDA_BLOCK 0u
#endif

#define MAX_REDUCTIONS (CINT_CUDA_MAX_PARAMS / 2u)

/* One loaded module, or one entry of it, keyed by the address of the module's PTX lines. */
typedef struct gpu_module {
    struct gpu_module *next;
    const char *const *ptx;
    cint_cuda_module module;
} gpu_module;

typedef struct gpu_entry {
    struct gpu_entry *next;
    const char *const *ptx;
    const char *name;
    cint_cuda_kernel kernel;
} gpu_entry;

typedef struct gpu_state {
    void (*release)(void *); /* first: cint_ctx_destroy calls it (cint_rt_device) */
    cint_cuda *cuda;
    gpu_module *modules;
    gpu_entry *entries;
} gpu_state;

/* A page range registered for one dispatch: host [lo, hi), device address of lo. */
typedef struct gpu_range {
    uintptr_t lo;
    uintptr_t hi;
    uint64_t device;
} gpu_range;

typedef struct gpu_run {
    gpu_state *g;
    uint32_t nranges;
    gpu_range ranges[CINT_CUDA_MAX_PARAMS];
    void *scratch;  /* mapped: C(i), then each reduction's contributions, then its marks */
    uint64_t scratch_device;
    int64_t *charges;
    const int64_t *contrib[MAX_REDUCTIONS];
    const uint32_t *mark[MAX_REDUCTIONS];
} gpu_run;

static size_t page_bytes(void)
{
#if defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize > 0u ? (size_t)si.dwPageSize : 4096u;
#else
    long p = sysconf(_SC_PAGESIZE);
    return p > 0 ? (size_t)p : 4096u;
#endif
}

static void state_release(void *p)
{
    gpu_state *g = (gpu_state *)p;
    while (g->entries != NULL) {
        gpu_entry *e = g->entries;
        g->entries = e->next;
        free(e);
    }
    while (g->modules != NULL) {
        gpu_module *m = g->modules;
        g->modules = m->next;
        (void)cint_cuda_module_unload(g->cuda, &m->module);
        free(m);
    }
    cint_cuda_close(g->cuda);
    free(g);
}

/* A boundary record (SPEC-03 A-6, Proposed encoding): E_UNSUPPORTED with operation `op`, and
 * faulted 2, so that the entry ends CINT_RESOURCE, not CINT_FAULT (SPEC-02 B-16b, B-16e). */
static bool boundary(cint_ctx *ctx, cint_site site, const char *op)
{
    cint_fault_record *rec = cint_rt_record_begin(ctx, site, (uint16_t)CINT_E_UNSUPPORTED);
    if (rec != NULL) {
        cint_rt_record_op(rec, op);
        cint_rt_head_of(ctx)->faulted = 2u;
    }
    return false;
}

static bool resource(cint_ctx *ctx, cint_site site)
{
    return boundary(ctx, site, "host.resource");
}

/* The context's handle, opened at its first dispatch. */
static gpu_state *state_of(cint_ctx *ctx)
{
    void **slot = cint_rt_device(ctx);
    gpu_state *g = (gpu_state *)*slot;
    if (g == NULL) {
        g = (gpu_state *)calloc(1u, sizeof *g);
        if (g == NULL) {
            return NULL;
        }
        g->release = state_release;
#if defined(CINT_CUDA_TESTING)
        /* Test builds: the stand-in driver of rt/tests, when CINT_CUDA_DRIVER names it. */
        char path[1024];
#if defined(_WIN32)
        DWORD got = GetEnvironmentVariableA("CINT_CUDA_DRIVER", path, (DWORD)sizeof path);
        bool named = got > 0u && got < sizeof path;
#else
        const char *env = getenv("CINT_CUDA_DRIVER");
        bool named = env != NULL && strlen(env) < sizeof path;
        if (named) {
            memcpy(path, env, strlen(env) + 1u);
        }
#endif
        if (named ? cint_cuda_open_driver(path, NULL, NULL, &g->cuda, NULL) != CINT_OK
                  : cint_cuda_open(NULL, NULL, &g->cuda, NULL) != CINT_OK) {
#else
        if (cint_cuda_open(NULL, NULL, &g->cuda, NULL) != CINT_OK) {
#endif
            free(g);
            return NULL;
        }
        *slot = g;
    }
    return g;
}

/* Entry `name` of the module whose PTX is `ptx`, loading the module at its first use. */
static const cint_cuda_kernel *entry_of(gpu_state *g, const char *const *ptx, const char *name)
{
    gpu_entry *e;
    gpu_module *m;
    for (e = g->entries; e != NULL; e = e->next) {
        if (e->ptx == ptx && strcmp(e->name, name) == 0) {
            return &e->kernel;
        }
    }
    for (m = g->modules; m != NULL && m->ptx != ptx; m = m->next) {
    }
    if (m == NULL) {
        size_t bytes = 1u, at = 0u, i;
        char *text;
        cint_status s;
        for (i = 0u; ptx[i][0] != '\0'; i++) {
            bytes += strlen(ptx[i]);
        }
        m = (gpu_module *)calloc(1u, sizeof *m);
        text = (char *)malloc(bytes);
        if (m == NULL || text == NULL) {
            free(m);
            free(text);
            return NULL;
        }
        for (i = 0u; ptx[i][0] != '\0'; i++) {
            size_t n = strlen(ptx[i]);
            memcpy(text + at, ptx[i], n);
            at += n;
        }
        text[at] = '\0';
        s = cint_cuda_module_load(g->cuda, text, &m->module);
        free(text);
        if (s != CINT_OK) {
            free(m);
            return NULL;
        }
        m->ptx = ptx;
        m->next = g->modules;
        g->modules = m;
    }
    e = (gpu_entry *)calloc(1u, sizeof *e);
    if (e == NULL) {
        return NULL;
    }
    if (cint_cuda_kernel_get(g->cuda, &m->module, name, &e->kernel) != CINT_OK) {
        free(e);
        return NULL;
    }
    e->ptx = ptx;
    e->name = name;
    e->next = g->entries;
    g->entries = e;
    return &e->kernel;
}

/* Releases a dispatch's registrations and scratch; false when the driver refuses one. */
static bool run_release(gpu_run *r)
{
    uint32_t i;
    bool ok = true;
    for (i = 0u; i < r->nranges; i++) {
        ok = cint_cuda_host_unregister(r->g->cuda, (void *)r->ranges[i].lo) == CINT_OK && ok;
    }
    if (r->scratch != NULL) {
        ok = cint_cuda_host_free(r->g->cuda, r->scratch) == CINT_OK && ok;
    }
    free(r);
    return ok;
}

static bool run_failed(cint_ctx *ctx, cint_site site, cint_cuda_run *run)
{
    (void)run_release((gpu_run *)run->state);
    memset(run, 0, sizeof *run);
    return resource(ctx, site);
}

/* The bytes a view of `elem`-byte elements reaches, [*lo, *hi); false for an empty view. Its
 * offsets were admitted when it was bound or staged (SPEC-02 V-6), so they fit an I64. */
static bool view_bytes(const cint_vdesc *v, size_t elem, uintptr_t *lo, uintptr_t *hi)
{
    int64_t low = v->origin, high = v->origin, d;
    for (d = 0; d < v->rank && d < 4; d++) {
        int64_t reach;
        if (v->shape[d] <= 0) {
            return false;
        }
        reach = (v->shape[d] - 1) * v->stride[d];
        if (reach < 0) {
            low += reach;
        } else {
            high += reach;
        }
    }
    *lo = (uintptr_t)v->base + (uintptr_t)low * elem;
    *hi = (uintptr_t)v->base + ((uintptr_t)high + 1u) * elem;
    return true;
}

/* Registers the pages of every view argument, merged, so that no page is registered twice. */
static bool map_views(gpu_run *r, const cint_cuda_arg *args, uint32_t nargs)
{
    const uintptr_t page = (uintptr_t)page_bytes();
    gpu_range want[CINT_CUDA_MAX_PARAMS];
    uint32_t n = 0u, i, j;
    for (i = 0u; i < nargs; i++) {
        uintptr_t lo = 0u, hi = 0u;
        if (args[i].view != NULL && view_bytes(args[i].view, args[i].elem, &lo, &hi)) {
            gpu_range g;
            g.lo = lo / page * page;
            g.hi = (hi + page - 1u) / page * page;
            g.device = 0u;
            for (j = n; j > 0u && want[j - 1u].lo > g.lo; j--) {
                want[j] = want[j - 1u];
            }
            want[j] = g;
            n++;
        }
    }
    for (i = 0u; i < n; i++) {
        gpu_range g = want[i];
        if (r->nranges > 0u && g.lo <= r->ranges[r->nranges - 1u].hi) {
            if (g.hi > r->ranges[r->nranges - 1u].hi) {
                r->ranges[r->nranges - 1u].hi = g.hi;
            }
        } else {
            r->ranges[r->nranges++] = g;
        }
    }
    for (i = 0u; i < r->nranges; i++) {
        gpu_range *g = &r->ranges[i];
        if (cint_cuda_host_register(r->g->cuda, (void *)g->lo, (size_t)(g->hi - g->lo), &g->device) != CINT_OK) {
            r->nranges = i;
            return false;
        }
    }
    return true;
}

/* The device address of host address p, inside a registered range. */
static uint64_t device_of(const gpu_run *r, uintptr_t p)
{
    uint32_t i;
    for (i = 0u; i + 1u < r->nranges && p >= r->ranges[i + 1u].lo; i++) {
    }
    return r->ranges[i].device + (uint64_t)(p - r->ranges[i].lo);
}

bool cint_cuda_dispatch(cint_ctx *ctx, cint_site site, const char *const *ptx, const char *entry, int64_t n,
                        bool fuel, int64_t reductions, const cint_cuda_arg *args, uint32_t nargs,
                        cint_cuda_run *run)
{
    const cint_rt_head *h = cint_rt_head_of(ctx);
    const uint32_t extra = (fuel ? 2u : 0u) + 2u * (uint32_t)reductions;
    void *params[CINT_CUDA_MAX_PARAMS];
    cint_vdesc views[CINT_CUDA_MAX_PARAMS];
    uint64_t scalars[CINT_CUDA_MAX_PARAMS];
    uint64_t addr[2u + 2u * MAX_REDUCTIONS];
    const cint_cuda_kernel *k = NULL;
    cint_cuda_launch_info info;
    gpu_run *r = NULL;
    gpu_state *g = NULL;
    size_t per, bytes, at;
    int64_t allow, i, kb, kf, prefix = 0;
    uint32_t p = 0u, j;
    if (cint_rt_faulted(ctx)) {
        return false;
    }
    memset(run, 0, sizeof *run);
    if (n < 0 || reductions < 0 || reductions > (int64_t)MAX_REDUCTIONS || nargs + extra > CINT_CUDA_MAX_PARAMS ||
        (g = state_of(ctx)) == NULL || (k = entry_of(g, ptx, entry)) == NULL ||
        (r = (gpu_run *)calloc(1u, sizeof *r)) == NULL) {
        return resource(ctx, site);
    }
    r->g = g;
    run->n = n;
    run->limit = n;
    run->reductions = reductions;
    run->state = r;
    if (n == 0) {
        return true;
    }
    /* Scratch: n I64 charges (with fuel), then per reduction n I64 contributions and n U32
     * marks, which start at zero. */
    per = (fuel ? 8u : 0u) + 12u * (size_t)reductions;
    if (per > 0u) {
        if ((uint64_t)n > (uint64_t)(SIZE_MAX / per)) {
            return run_failed(ctx, site, run);
        }
        bytes = per * (size_t)n;
        if (cint_cuda_host_alloc(g->cuda, bytes, &r->scratch, &r->scratch_device) != CINT_OK) {
            return run_failed(ctx, site, run);
        }
        memset(r->scratch, 0, bytes);
    }
    if (!map_views(r, args, nargs)) {
        return run_failed(ctx, site, run);
    }
    allow = h->fuel_limit >= 0 ? h->fuel_limit - h->fuel_used : INT64_MAX - h->fuel_used;
    at = 0u;
    if (fuel) {
        r->charges = (int64_t *)r->scratch;
        addr[0] = r->scratch_device;
        addr[1] = (uint64_t)allow;
        params[p++] = &addr[0];
        params[p++] = &addr[1];
        at = 8u * (size_t)n;
    }
    for (j = 0u; j < (uint32_t)reductions; j++) {
        r->contrib[j] = (const int64_t *)(void *)((char *)r->scratch + at);
        addr[2u + 2u * j] = r->scratch_device + at;
        at += 8u * (size_t)n;
    }
    for (j = 0u; j < (uint32_t)reductions; j++) {
        r->mark[j] = (const uint32_t *)(void *)((char *)r->scratch + at);
        addr[3u + 2u * j] = r->scratch_device + at;
        at += 4u * (size_t)n;
        params[p++] = &addr[2u + 2u * j];
        params[p++] = &addr[3u + 2u * j];
    }
    for (j = 0u; j < nargs; j++) {
        if (args[j].view != NULL) {
            uintptr_t lo = 0u, hi = 0u;
            views[j] = *args[j].view;
            views[j].base = view_bytes(args[j].view, args[j].elem, &lo, &hi)
                                ? (void *)(uintptr_t)device_of(r, (uintptr_t)args[j].view->base)
                                : NULL;
            params[p++] = &views[j];
        } else {
            scalars[j] = args[j].value;
            params[p++] = &scalars[j];
        }
    }
    if (cint_cuda_launch(g->cuda, k, n, CINT_CUDA_BLOCK, params, p, &info) != CINT_OK) {
        return run_failed(ctx, site, run);
    }
    /* k_b, then k_f by the exact prefix of C(i) over the items below it (B-16c). */
    kb = info.fault < (uint64_t)n ? (int64_t)info.fault : n;
    kf = kb;
    if (fuel) {
        for (i = 0; i < kb; i++) {
            if (r->charges[i] > allow - prefix) {
                kf = i;
                break;
            }
            prefix += r->charges[i];
        }
    }
    run->limit = kf;
    run->contrib = r->contrib;
    run->mark = r->mark;
    return true;
}

bool cint_cuda_dispatch_end(cint_ctx *ctx, cint_site site, cint_cuda_run *run, int64_t limit)
{
    gpu_run *r = (gpu_run *)run->state;
    uint64_t sum = 0u;
    int64_t i;
    bool ok;
    if (r == NULL) {
        return !cint_rt_faulted(ctx);
    }
    if (r->charges != NULL) {
        for (i = 0; i < limit && i < run->limit; i++) {
            sum += (uint64_t)r->charges[i];
        }
    }
    ok = sum == 0u || cint_fuel_charge(ctx, site, sum);
    memset(run, 0, sizeof *run);
    if (!run_release(r)) {
        return resource(ctx, site);
    }
    return ok;
}

bool cint_cuda_dispatch_diverged(cint_ctx *ctx, cint_site site)
{
    return boundary(ctx, site, "host.divergence");
}
