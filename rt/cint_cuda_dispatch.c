/* cint_cuda_dispatch.c: the dispatch path of a gpu-cuda build (rt/cint_cuda.h; box 11 units 4, 5).
 *
 * Status: Proposed, with rt/cint_cuda.h. A module's C file compiled with CINT_GPU_CUDA calls
 * these functions from a kernel's function at its first work-item (compiler/back_c.ci): the
 * entry checks, the dispatch charge, staging and the push have run on the host, as on
 * cpu-c17, and the host's item loop then runs at most the one work-item F-11 names. Design
 * note: docs/design/notes/2026-10-05-box11-cuda-backend.md, sections 4.2, 5 and 6.
 *
 * A context's CUDA handle (on the ordinal of config.devices[0], or 0 with device_count 0: SPEC-03
 * A-16a) and the modules and entries it has loaded live in the context's device slot
 * (cint_rt_internal.h) from its first use to cint_ctx_destroy. A dispatch page-locks and maps
 * the pages its host views reach (SPEC-03 M-28a), passes views of device(1) registrations as
 * they are, takes mapped scratch for the charges C(i), the contributions and the presence
 * marks (B-16c), and releases both in cint_cuda_dispatch_end. Host copies of device memory
 * (cint_cuda_copy_view; the fault-readback of B-16b) go through malloc. A failed driver call
 * ends the entry CINT_RESOURCE with a boundary record, host.resource (B-16e); the driver's
 * error stays in the handle's cint_cuda_last_error, a diagnostic, never canonical state.
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
    uint32_t nshadows; /* the fault-readback copies of the last dispatch (B-16b) */
    void *shadows[CINT_CUDA_MAX_PARAMS];
    uint64_t stage_entry;   /* the entry (cint_ctx.entries) and the dispatch (cint_ctx.dispatches) */
    int64_t stage_dispatch; /* of the device staging (B-16d), which lives until another stages */
    uint32_t nstages;
    struct {
        uint64_t device;
        size_t bytes;
    } stages[CINT_CUDA_MAX_PARAMS];
} gpu_state;

/* A page range registered for one dispatch: host [lo, hi), device address of lo. */
typedef struct gpu_range {
    uintptr_t lo;
    uintptr_t hi;
    uint64_t device;
} gpu_range;

typedef struct gpu_run {
    gpu_state *g;
    const cint_cuda_arg *args; /* the caller's, valid until cint_cuda_dispatch_end */
    uint32_t nargs;
    bool dev[CINT_CUDA_MAX_PARAMS]; /* argument j is a view of a device(1) registration */
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

static void shadows_free(gpu_state *g)
{
    while (g->nshadows > 0u) free(g->shadows[--g->nshadows]);
}

static void stages_free(gpu_state *g)
{
    while (g->nstages > 0u) (void)cint_cuda_free(g->cuda, g->stages[--g->nstages].device);
}

static void state_release(void *p)
{
    gpu_state *g = (gpu_state *)p;
    shadows_free(g);
    stages_free(g);
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
    const cint_cuda_device *dev = (const cint_cuda_device *)ctx->device_record;
    if (g == NULL) {
        g = (gpu_state *)calloc(1u, sizeof *g);
        if (g == NULL) return NULL;
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
        if (named) memcpy(path, env, strlen(env) + 1u);
#endif
        if (named ? cint_cuda_open_driver(path, dev, NULL, &g->cuda, NULL) != CINT_OK
                  : cint_cuda_open(dev, NULL, &g->cuda, NULL) != CINT_OK) {
#else
        if (cint_cuda_open(dev, NULL, &g->cuda, NULL) != CINT_OK) {
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
    for (e = g->entries; e != NULL; e = e->next)
        if (e->ptx == ptx && strcmp(e->name, name) == 0) return &e->kernel;
    for (m = g->modules; m != NULL && m->ptx != ptx; m = m->next) {}
    if (m == NULL) {
        size_t bytes = 1u, at = 0u, i;
        char *text;
        cint_status s;
        for (i = 0u; ptx[i][0] != '\0'; i++) bytes += strlen(ptx[i]);
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
    if (e == NULL) return NULL;
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
    for (i = 0u; i < r->nranges; i++)
        ok = cint_cuda_host_unregister(r->g->cuda, (void *)r->ranges[i].lo) == CINT_OK && ok;
    if (r->scratch != NULL) ok = cint_cuda_host_free(r->g->cuda, r->scratch) == CINT_OK && ok;
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
        if (v->shape[d] <= 0) return false;
        reach = (v->shape[d] - 1) * v->stride[d];
        if (reach < 0) low += reach;
        else high += reach;
    }
    *lo = (uintptr_t)v->base + (uintptr_t)low * elem;
    *hi = (uintptr_t)v->base + ((uintptr_t)high + 1u) * elem;
    return true;
}

/* Registers the pages of every host view argument, merged, so that no page is registered twice. */
static bool map_views(gpu_run *r, const cint_cuda_arg *args, const bool *dev, uint32_t nargs)
{
    const uintptr_t page = (uintptr_t)page_bytes();
    gpu_range want[CINT_CUDA_MAX_PARAMS];
    uint32_t n = 0u, i, j;
    for (i = 0u; i < nargs; i++) {
        uintptr_t lo = 0u, hi = 0u;
        if (args[i].view != NULL && !dev[i] && view_bytes(args[i].view, args[i].elem, &lo, &hi)) {
            gpu_range g;
            g.lo = lo / page * page;
            g.hi = (hi + page - 1u) / page * page;
            g.device = 0u;
            for (j = n; j > 0u && want[j - 1u].lo > g.lo; j--) want[j] = want[j - 1u];
            want[j] = g;
            n++;
        }
    }
    for (i = 0u; i < n; i++) {
        gpu_range g = want[i];
        if (r->nranges > 0u && g.lo <= r->ranges[r->nranges - 1u].hi) {
            if (g.hi > r->ranges[r->nranges - 1u].hi) r->ranges[r->nranges - 1u].hi = g.hi;
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
    for (i = 0u; i + 1u < r->nranges && p >= r->ranges[i + 1u].lo; i++) {}
    return r->ranges[i].device + (uint64_t)(p - r->ranges[i].lo);
}

bool cint_cuda_placed(cint_ctx *ctx, const cint_vdesc *v)
{
    const uintptr_t p = (uintptr_t)v->base;
    const gpu_state *g = ctx->device_record != NULL ? (const gpu_state *)*cint_rt_device(ctx) : NULL;
    uint32_t i;
    for (i = 0u; ctx->device_record != NULL && p != 0u && i < ctx->buffer_used; i++) {
        const buffer_slot *s = &ctx->buffers[i];
        if (s->live != 0u && s->device != 0u && p - (uintptr_t)s->base < (uintptr_t)s->bytes) return true;
    }
    for (i = 0u; g != NULL && i < g->nstages; i++)
        if (p - (uintptr_t)g->stages[i].device < g->stages[i].bytes) return true;
    return false;
}

bool cint_cuda_stage(cint_ctx *ctx, cint_site site, int64_t count, size_t size, void **out)
{
    gpu_state *g = state_of(ctx);
    uint64_t device = 0u;
    size_t bytes;
    if (g != NULL && (g->stage_entry != ctx->entries || g->stage_dispatch != ctx->dispatches)) {
        stages_free(g); /* the stages of an earlier dispatch, published when it returned */
        g->stage_entry = ctx->entries;
        g->stage_dispatch = ctx->dispatches;
    }
    if (g == NULL || count < 0 || size == 0u || (uint64_t)count > (SIZE_MAX - 8u) / size ||
        g->nstages == CINT_CUDA_MAX_PARAMS) {
        return resource(ctx, site);
    }
    bytes = ((size_t)count * size + 8u) / 8u * 8u; /* at least 8, a multiple of 4 */
    if (cint_cuda_alloc(g->cuda, bytes, &device) != CINT_OK) return resource(ctx, site);
    g->stages[g->nstages].device = device;
    g->stages[g->nstages++].bytes = bytes;
    if (cint_cuda_zero(g->cuda, device, bytes) != CINT_OK) return resource(ctx, site);
    *out = (void *)(uintptr_t)device;
    return true;
}

/* A malloc'd host copy of the bytes device view v of `elem`-byte elements reaches, read from the
 * device, and *h, v over it; NULL when malloc or the driver fails. An empty view gets a 1-byte
 * copy, which no element addresses. */
static void *shadow(gpu_state *g, const cint_vdesc *v, size_t elem, cint_vdesc *h)
{
    uintptr_t lo = (uintptr_t)v->base, hi = lo;
    unsigned char *p;
    (void)view_bytes(v, elem, &lo, &hi);
    p = (unsigned char *)malloc(hi > lo ? (size_t)(hi - lo) : 1u);
    if (p != NULL && hi > lo && cint_cuda_copy_to_host(g->cuda, p, (uint64_t)lo, (size_t)(hi - lo)) != CINT_OK) {
        free(p);
        return NULL;
    }
    *h = *v;
    h->base = p + ((uintptr_t)v->base - lo);
    return p;
}

/* The elements of a view, at most INT64_MAX (its extents were admitted when it was bound). */
static uint64_t view_count(const cint_vdesc *v)
{
    uint64_t n = 1u;
    int64_t d;
    for (d = 0; d < v->rank && d < 4; d++) n *= v->shape[d] > 0 ? (uint64_t)v->shape[d] : 0u;
    return n;
}

/* The address of v's element at index tuple idx, modulo 2^64. */
static uint64_t elem_at(const cint_vdesc *v, const int64_t *idx, size_t size)
{
    uint64_t off = (uint64_t)v->origin;
    int64_t d;
    for (d = 0; d < v->rank && d < 4; d++) off += (uint64_t)idx[d] * (uint64_t)v->stride[d];
    return (uint64_t)(uintptr_t)v->base + off * (uint64_t)size;
}

/* Copies the elements of src to dst (of equal extents, dd and ds telling which are in device
 * memory) in row-major order, one driver copy per run of elements adjacent in both, then waits
 * for the stream: a copy between device views stays on the device (B-16d). */
static bool copy_runs(gpu_state *g, const cint_vdesc *dst, bool dd, const cint_vdesc *src, bool ds, size_t size)
{
    int64_t idx[4] = {0, 0, 0, 0}, d;
    uint64_t n = view_count(dst), to = 0u, from = 0u, len = 0u, k;
    cint_status st = CINT_OK;
    for (k = 0u; k <= n && st == CINT_OK; k++) {
        const uint64_t a = k < n ? elem_at(dst, idx, size) : 0u, b = k < n ? elem_at(src, idx, size) : 0u;
        if (len > 0u && (k == n || a != to + len || b != from + len)) {
            st = dd && ds ? cint_cuda_copy_on_device(g->cuda, to, from, (size_t)len)
                 : dd     ? cint_cuda_copy_to_device(g->cuda, to, (const void *)(uintptr_t)from, (size_t)len)
                          : cint_cuda_copy_to_host(g->cuda, (void *)(uintptr_t)to, from, (size_t)len);
            len = 0u;
        }
        if (len == 0u) {
            to = a;
            from = b;
        }
        len += size;
        for (d = dst->rank - 1; d >= 0 && ++idx[d] == dst->shape[d]; d--) idx[d] = 0;
    }
    return st == CINT_OK && cint_cuda_sync(g->cuda) == CINT_OK;
}

bool cint_cuda_copy_view(cint_ctx *ctx, cint_site site, const cint_vdesc *dst, const cint_vdesc *src, size_t size)
{
    gpu_state *g = state_of(ctx);
    return (g != NULL && copy_runs(g, dst, cint_cuda_placed(ctx, dst), src, cint_cuda_placed(ctx, src), size)) ||
           resource(ctx, site);
}

bool cint_cuda_bind_finish(cint_ctx *ctx, cint_site site, const cint_bind *b, uint32_t count, uint64_t marks)
{
    bool ok;
    if (ctx == NULL) return false;
    ctx->bind_marks = ctx->device_record != NULL ? marks : 0u;
    ok = cint_rt_bind_finish(ctx, site, b, count);
    ctx->bind_marks = 0u;
    return ok;
}

/* Where the view v of a dispatch issued from `.ci` lies: 0 in device(1) memory, 1 in a host
 * registration, 2 elsewhere, in storage the context owns (SPEC-03 M-28a). */
static int placement(cint_ctx *ctx, const cint_vdesc *v)
{
    const uintptr_t p = (uintptr_t)v->base;
    uint32_t i;
    if (cint_cuda_placed(ctx, v)) return 0;
    for (i = 0u; p != 0u && i < ctx->buffer_used; i++) {
        const buffer_slot *s = &ctx->buffers[i];
        if (s->live != 0u && p - (uintptr_t)s->base < (uintptr_t)s->bytes) return 1;
    }
    return 2;
}

bool cint_cuda_dispatch_placed(cint_ctx *ctx, cint_site site, const cint_vdesc *const *views, uint32_t count)
{
    int first = -1, at;
    uint32_t j;
    if (cint_rt_faulted(ctx)) return false;
    for (j = 0u; ctx->host_binds == NULL && ctx->device_record != NULL && j < count; j++) {
        if (views[j] == NULL) continue;
        at = placement(ctx, views[j]);
        if (at == 1 || (first >= 0 && at != first)) {
            cint_fault_record *rec = cint_rt_record_begin(ctx, site, (uint16_t)CINT_E_UNSUPPORTED);
            if (rec != NULL) {
                cint_rt_record_op(rec, "bind.placement");
                cint_rt_record_operand(rec, CINT_TAG_I64, (uint64_t)j);
            }
            return false;
        }
        first = at;
    }
    return true;
}

cint_status cint_ctx_device_stream(cint_ctx *ctx, uint32_t device, cint_sync *out)
{
    gpu_state *g;
    if (ctx == NULL || out == NULL) return CINT_REFUSED;
    if (!cint_rt_busy_take(ctx)) return CINT_BUSY;
    if (device != 1u || ctx->device_record == NULL) {
        cint_rt_busy_give(ctx);
        return CINT_REFUSED;
    }
    g = state_of(ctx);
    cint_rt_busy_give(ctx);
    if (g == NULL) return CINT_RESOURCE;
    memset(out, 0, sizeof *out);
    out->kind = CINT_MEM_CUDA;
    out->u.cuda.stream = (void *)cint_cuda_stream(g->cuda);
    return CINT_OK;
}

/* B-16b: before the host re-executes an item, each device view argument is read back into a
 * host copy, and the caller's descriptor of it is pointed there; the copies live until the
 * context's next dispatch. */
static bool read_back(const gpu_run *r)
{
    uint32_t j;
    for (j = 0u; j < r->nargs; j++) {
        if (r->dev[j]) {
            cint_vdesc *v = (cint_vdesc *)(uintptr_t)r->args[j].view; /* the caller's own, not const */
            void *p = shadow(r->g, v, r->args[j].elem, v);
            if (p == NULL) return false;
            r->g->shadows[r->g->nshadows++] = p;
        }
    }
    return true;
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
    if (cint_rt_faulted(ctx)) return false;
    memset(run, 0, sizeof *run);
    if (n < 0 || reductions < 0 || reductions > (int64_t)MAX_REDUCTIONS || nargs + extra > CINT_CUDA_MAX_PARAMS ||
        (g = state_of(ctx)) == NULL || (k = entry_of(g, ptx, entry)) == NULL ||
        (r = (gpu_run *)calloc(1u, sizeof *r)) == NULL) {
        return resource(ctx, site);
    }
    shadows_free(g);
    for (j = 0u; j < nargs; j++) r->dev[j] = args[j].view != NULL && cint_cuda_placed(ctx, args[j].view);
    r->g = g;
    r->args = args;
    r->nargs = nargs;
    run->n = n;
    run->limit = n;
    run->reductions = reductions;
    run->state = r;
    if (n == 0) return true;
    /* Scratch: n I64 charges (with fuel), then per reduction n I64 contributions and n U32
     * marks, which start at zero. */
    per = (fuel ? 8u : 0u) + 12u * (size_t)reductions;
    if (per > 0u) {
        if ((uint64_t)n > (uint64_t)(SIZE_MAX / per)) return run_failed(ctx, site, run);
        bytes = per * (size_t)n;
        if (cint_cuda_host_alloc(g->cuda, bytes, &r->scratch, &r->scratch_device) != CINT_OK)
            return run_failed(ctx, site, run);
        memset(r->scratch, 0, bytes);
    }
    if (!map_views(r, args, r->dev, nargs)) return run_failed(ctx, site, run);
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
            if (!r->dev[j]) {
                views[j].base = view_bytes(args[j].view, args[j].elem, &lo, &hi)
                                    ? (void *)(uintptr_t)device_of(r, (uintptr_t)args[j].view->base)
                                    : NULL;
            }
            params[p++] = &views[j];
        } else {
            scalars[j] = args[j].value;
            params[p++] = &scalars[j];
        }
    }
    if (cint_cuda_launch(g->cuda, k, n, CINT_CUDA_BLOCK, params, p, &info) != CINT_OK)
        return run_failed(ctx, site, run);
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
    if (r == NULL) return !cint_rt_faulted(ctx);
    if (r->charges != NULL)
        for (i = 0; i < limit && i < run->limit; i++) sum += (uint64_t)r->charges[i];
    ok = sum == 0u || cint_fuel_charge(ctx, site, sum);
    if (ok && limit < run->n && !read_back(r)) ok = resource(ctx, site); /* item `limit` is the host's (F-11) */
    memset(run, 0, sizeof *run);
    if (!run_release(r)) return resource(ctx, site);
    return ok;
}

bool cint_cuda_dispatch_diverged(cint_ctx *ctx, cint_site site)
{
    return boundary(ctx, site, "host.divergence");
}
