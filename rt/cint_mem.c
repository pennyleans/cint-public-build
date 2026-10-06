/* cint_mem.c: arenas, pools and handles (rt/cint_mem.h; SPEC-03 M-12 to M-23;
 * ref/OPEN.md REF-OQ-45; decision 2026-10-06 on OQ-212).
 *
 * Part of the cint_rt library: rt/cint_rt.c includes this file at its end, so
 * it shares that file's context structure and fault writers, and every build
 * that compiles cint_rt.c carries it. It keeps a size row of its own (SPEC-09
 * 4.2, tools/cint_lint.py). It is not compiled alone.
 *
 * A context's arenas are one table: the module-level arenas first, in
 * declaration order, then every child in the order it was made, so arena k
 * has identifier k + 1 (M-13, M-20). A child shares the storage of its
 * top-level arena from its offset. A child is retired when its parent's
 * generation is no longer the one it was carved at, or its parent is retired:
 * a reset retires every descendant without visiting it (M-20). Every integer
 * computation is on values proven in range.
 */
#include "cint_mem.h"

/* Defined later in cint_rt.c (section 14). */
static bool fault_i64s(cint_ctx *ctx, cint_site site, uint32_t code, const char *operation, const int64_t *ops,
                       unsigned n, bool has_limit, int64_t limit);

#define MEM_GEN_RETIRED UINT64_MAX  /* M-16: no generation reaches 2^64 - 1 */
#define MEM_OCCUPIED 1u
#define MEM_RETIRED 2u
/* SPEC-03 M-17 reasons, numbered by precedence, and `container` (REF-OQ-45). */
#define MEM_NULL 1u
#define MEM_RETIRED_REASON 2u
#define MEM_GENERATION 3u
#define MEM_EXTENT 4u
#define MEM_VACANT 5u
#define MEM_CONTAINER 6u

typedef struct mem_arena {
    uint32_t root;               /* the index of the top-level arena whose storage it shares */
    uint32_t parent;             /* the index of its parent plus 1; 0 for a top-level arena */
    int64_t offset;              /* its element 0 in the root's storage */
    int64_t capacity;
    int64_t used;
    uint64_t generation;
    uint64_t parent_generation;  /* the parent's generation when it was carved */
    unsigned char *storage;      /* a top-level arena's elements; NULL for a child or capacity 0 */
    uint32_t elem_bytes;
} mem_arena;

typedef struct mem_pool {
    unsigned char *storage;
    uint64_t *gens;
    uint8_t *marks;              /* MEM_OCCUPIED, MEM_RETIRED */
    int64_t *free_list;          /* last in, first out */
    int64_t free_count;
    int64_t fresh;               /* the next slot never used */
    int64_t occupied;
    int64_t retired;
    int64_t capacity;
    uint32_t elem_bytes;
} mem_pool;

struct mem_state {
    const cint_mem_table *table;
    mem_arena *arenas;
    uint32_t arena_count;
    uint32_t arena_cap;
    mem_pool *pools;
    uint32_t pool_count;
    uint32_t pool_cap;
};

static void *mem_take(cint_ctx *ctx, size_t bytes)
{
    void *p = ctx->allocator.alloc(ctx->allocator.user, bytes, 16u);
    if (p != NULL) {
        memset(p, 0, bytes);
    }
    return p;
}

static void mem_give(cint_ctx *ctx, void *p, size_t bytes)
{
    if (p != NULL) {
        ctx->allocator.release(ctx->allocator.user, p, bytes, 16u);
    }
}

/* The bytes of `count` elements of `size` bytes, or SIZE_MAX when they do not fit. */
static size_t mem_bytes(int64_t count, size_t size)
{
    if (count < 0 || (size != 0u && (uint64_t)count > (SIZE_MAX - 1u) / size)) {
        return SIZE_MAX;
    }
    return (size_t)count * size;
}

static void mem_free_state(cint_ctx *ctx, struct mem_state *s)
{
    for (uint32_t k = 0u; k < s->arena_count; k++) {
        const mem_arena *a = &s->arenas[k];
        if (a->parent == 0u) {
            mem_give(ctx, a->storage, mem_bytes(a->capacity, a->elem_bytes));
        }
    }
    for (uint32_t k = 0u; k < s->pool_count; k++) {
        const mem_pool *p = &s->pools[k];
        mem_give(ctx, p->storage, mem_bytes(p->capacity, p->elem_bytes));
        mem_give(ctx, p->gens, mem_bytes(p->capacity, sizeof(uint64_t)));
        mem_give(ctx, p->marks, mem_bytes(p->capacity, 1u));
        mem_give(ctx, p->free_list, mem_bytes(p->capacity, sizeof(int64_t)));
    }
    mem_give(ctx, s->arenas, (size_t)s->arena_cap * sizeof(mem_arena));
    mem_give(ctx, s->pools, (size_t)s->pool_cap * sizeof(mem_pool));
    mem_give(ctx, s, sizeof *s);
}

/* cint_ctx_destroy: the context's arenas and pools. */
static void mem_destroy(cint_ctx *ctx)
{
    if (ctx->mem != NULL) {
        mem_free_state(ctx, ctx->mem);
        ctx->mem = NULL;
    }
}

/* Storage of `bytes` bytes for a declaration, or false; capacity 0 takes none. */
static bool mem_storage(cint_ctx *ctx, size_t bytes, void **out)
{
    *out = NULL;
    if (bytes == SIZE_MAX) {
        return false;
    }
    if (bytes == 0u) {
        return true;
    }
    *out = mem_take(ctx, bytes);
    return *out != NULL;
}

/* The arenas and pools of table t, made at the context's first call that names
 * one (M-13, M-20); NULL with the context faulted. */
static struct mem_state *mem_open(cint_ctx *ctx, cint_site site, const cint_mem_table *t)
{
    struct mem_state *s = ctx->mem;
    uint32_t arenas = 0u, pools = 0u;
    bool ok = true;
    if (cint_rt_faulted(ctx)) {
        return NULL;
    }
    if (s != NULL) {
        if (s->table != t) {
            (void)fault_refused(ctx, site);
            return NULL;
        }
        return s;
    }
    if (t == NULL || (t->count > 0u && t->decls == NULL) || t->reserved != 0u) {
        (void)fault_refused(ctx, site);
        return NULL;
    }
    for (uint32_t k = 0u; k < t->count; k++) {
        const cint_mem_decl *d = &t->decls[k];
        if ((d->kind != CINT_HANDLE_ARENA && d->kind != CINT_HANDLE_POOL) || d->elem_bytes == 0u || d->capacity < 0) {
            (void)fault_refused(ctx, site);
            return NULL;
        }
        arenas += d->kind == CINT_HANDLE_ARENA ? 1u : 0u;
    }
    pools = t->count - arenas;
    s = (struct mem_state *)mem_take(ctx, sizeof *s);
    if (s == NULL) {
        (void)fault_host(ctx, site, "host.resource", false, CINT_OK);
        return NULL;
    }
    s->table = t;
    s->arena_cap = arenas < 4u ? 4u : arenas;
    s->pool_cap = pools;
    s->arenas = (mem_arena *)mem_take(ctx, (size_t)s->arena_cap * sizeof(mem_arena));
    s->pools = pools == 0u ? NULL : (mem_pool *)mem_take(ctx, (size_t)pools * sizeof(mem_pool));
    ok = s->arenas != NULL && (pools == 0u || s->pools != NULL);
    for (uint32_t k = 0u; ok && k < t->count; k++) {
        const cint_mem_decl *d = &t->decls[k];
        void *p = NULL;
        if (d->kind == CINT_HANDLE_ARENA) {
            mem_arena *a = &s->arenas[s->arena_count];
            ok = mem_storage(ctx, mem_bytes(d->capacity, d->elem_bytes), &p);
            a->root = s->arena_count;
            a->capacity = d->capacity;
            a->storage = (unsigned char *)p;
            a->elem_bytes = d->elem_bytes;
            s->arena_count++;
        } else {
            mem_pool *q = &s->pools[s->pool_count];
            q->capacity = d->capacity;
            q->elem_bytes = d->elem_bytes;
            s->pool_count++;
            ok = mem_storage(ctx, mem_bytes(d->capacity, d->elem_bytes), &p);
            q->storage = (unsigned char *)p;
            ok = ok && mem_storage(ctx, mem_bytes(d->capacity, sizeof(uint64_t)), &p);
            q->gens = (uint64_t *)p;
            ok = ok && mem_storage(ctx, mem_bytes(d->capacity, 1u), &p);
            q->marks = (uint8_t *)p;
            ok = ok && mem_storage(ctx, mem_bytes(d->capacity, sizeof(int64_t)), &p);
            q->free_list = (int64_t *)p;
        }
    }
    if (!ok) {
        if (s->arenas == NULL) {
            s->arena_cap = 0u;
        }
        mem_free_state(ctx, s);
        (void)fault_host(ctx, site, "host.resource", false, CINT_OK);
        return NULL;
    }
    ctx->mem = s;
    return s;
}

static bool mem_stale(cint_ctx *ctx, cint_site site, const char *operation, const cint_handle *h, uint64_t current,
                      uint32_t reason)
{
    cint_fault_record *rec = cint_rt_record_begin(ctx, site, (uint16_t)CINT_E_STALE_HANDLE);
    if (rec != NULL) {
        cint_rt_record_op(rec, operation);
        cint_rt_record_operand(rec, CINT_TAG_U64, h->slot);
        cint_rt_record_operand(rec, CINT_TAG_I64, (uint64_t)h->length);
        cint_rt_record_operand(rec, CINT_TAG_U64, h->generation);
        cint_rt_record_operand(rec, CINT_TAG_U32, h->container);
        cint_rt_record_operand(rec, CINT_TAG_U8, h->kind);
        cint_rt_record_operand(rec, CINT_TAG_U64, current);
        cint_rt_record_operand(rec, CINT_TAG_U8, reason);
    }
    return false;
}

static bool mem_null(const cint_handle *h)
{
    return h->slot == 0u && h->length == 0 && h->generation == 0u && h->container == 0u && h->kind == 0u;
}

/* Arena `id` of state s, or NULL with the context faulted. */
static mem_arena *mem_arena_of(cint_ctx *ctx, cint_site site, struct mem_state *s, uint32_t id)
{
    if (s == NULL) {
        return NULL;
    }
    if (id == 0u || id > s->arena_count) {
        (void)fault_refused(ctx, site);
        return NULL;
    }
    return &s->arenas[id - 1u];
}

static mem_pool *mem_pool_of(cint_ctx *ctx, cint_site site, struct mem_state *s, uint32_t id)
{
    if (s == NULL) {
        return NULL;
    }
    if (id == 0u || id > s->pool_count) {
        (void)fault_refused(ctx, site);
        return NULL;
    }
    return &s->pools[id - 1u];
}

/* Whether arena k or one of its ancestors is retired (M-20). */
static bool mem_retired(const struct mem_state *s, uint32_t k)
{
    while (s->arenas[k].parent != 0u) {
        const mem_arena *a = &s->arenas[k];
        if (s->arenas[a->parent - 1u].generation != a->parent_generation) {
            return true;
        }
        k = a->parent - 1u;
    }
    return false;
}

/* The first M-17 reason that applies to h in arena `id` (index k), or 0. */
static uint32_t mem_arena_reason(const struct mem_state *s, uint32_t id, const cint_handle *h)
{
    const mem_arena *a = &s->arenas[id - 1u];
    if (mem_null(h)) {
        return MEM_NULL;
    }
    if (h->kind != CINT_HANDLE_ARENA || h->container != id) {
        return MEM_CONTAINER;
    }
    if (mem_retired(s, id - 1u)) {
        return MEM_RETIRED_REASON;
    }
    if (h->generation != a->generation) {
        return MEM_GENERATION;
    }
    if (h->length < 0 || h->slot > (uint64_t)a->used || (uint64_t)h->length > (uint64_t)a->used - h->slot) {
        return MEM_EXTENT;
    }
    return 0u;
}

static uint32_t mem_pool_reason(const mem_pool *p, uint32_t id, const cint_handle *h)
{
    bool inside = h->slot < (uint64_t)p->capacity;
    if (mem_null(h)) {
        return MEM_NULL;
    }
    if (h->kind != CINT_HANDLE_POOL || h->container != id) {
        return MEM_CONTAINER;
    }
    if (inside && (p->marks[h->slot] & MEM_RETIRED) != 0u) {
        return MEM_RETIRED_REASON;
    }
    if (inside && h->generation != p->gens[h->slot]) {
        return MEM_GENERATION;
    }
    if (!inside) {
        return MEM_EXTENT;
    }
    if ((p->marks[h->slot] & MEM_OCCUPIED) == 0u) {
        return MEM_VACANT;
    }
    return 0u;
}

/* A member of arena `id` (M-14): faulted when it or an ancestor is retired. */
static mem_arena *mem_member(cint_ctx *ctx, cint_site site, const cint_mem_table *t, uint32_t id,
                             const char *operation, struct mem_state **out)
{
    struct mem_state *s = mem_open(ctx, site, t);
    mem_arena *a = mem_arena_of(ctx, site, s, id);
    if (a == NULL) {
        return NULL;
    }
    if (mem_retired(s, id - 1u)) {
        cint_handle own = {0u, a->capacity, a->generation, id, CINT_HANDLE_ARENA, {0u, 0u, 0u}};
        (void)mem_stale(ctx, site, operation, &own, a->generation, MEM_RETIRED_REASON);
        return NULL;
    }
    *out = s;
    return a;
}

/* The extent check of an allocation or a child of n elements (M-14). */
static bool mem_fits(cint_ctx *ctx, cint_site site, const mem_arena *a, int64_t n, bool result, uint16_t *tag)
{
    if (n >= 0 && n <= a->capacity - a->used) {
        return true;
    }
    if (result && n >= 0) {
        *tag = 1u;  /* AllocError.full */
        return false;
    }
    int64_t ops[3] = {n, a->used, a->capacity};
    return fault_i64s(ctx, site, CINT_E_BOUNDS, "arena.alloc", ops, 3u, false, 0);
}

bool cint_mem_alloc(cint_ctx *ctx, cint_site site, const cint_mem_table *t, uint32_t arena, int64_t n, bool result,
                    cint_handle *out, uint16_t *tag)
{
    struct mem_state *s = NULL;
    mem_arena *a = mem_member(ctx, site, t, arena, "arena.alloc", &s);
    uint16_t full = 0u;
    if (a == NULL) {
        return false;
    }
    if (!mem_fits(ctx, site, a, n, result, &full)) {
        if (full == 0u) {
            return false;
        }
        memset(out, 0, sizeof *out);
        *tag = full;
        return true;
    }
    const mem_arena *r = &s->arenas[a->root];
    if (n > 0) {
        memset(r->storage + (size_t)(a->offset + a->used) * a->elem_bytes, 0, (size_t)n * a->elem_bytes);
    }
    memset(out, 0, sizeof *out);
    out->slot = (uint64_t)a->used;
    out->length = n;
    out->generation = a->generation;
    out->container = arena;
    out->kind = CINT_HANDLE_ARENA;
    a->used += n;
    if (tag != NULL) {
        *tag = 0u;
    }
    return true;
}

bool cint_mem_child(cint_ctx *ctx, cint_site site, const cint_mem_table *t, uint32_t arena, int64_t n,
                    uint32_t *out)
{
    struct mem_state *s = NULL;
    mem_arena *a = mem_member(ctx, site, t, arena, "arena.alloc", &s);
    uint16_t full = 0u;
    if (a == NULL || !mem_fits(ctx, site, a, n, false, &full)) {
        return false;
    }
    if (s->arena_count == UINT32_MAX) {
        return fault_refused(ctx, site);  /* no clause gives the fault (REF-OQ-45) */
    }
    if (s->arena_count == s->arena_cap) {
        uint32_t cap = s->arena_cap > UINT32_MAX / 2u ? UINT32_MAX : 2u * s->arena_cap;
        mem_arena *grown = (mem_arena *)mem_take(ctx, (size_t)cap * sizeof(mem_arena));
        if (grown == NULL) {
            return fault_host(ctx, site, "host.resource", false, CINT_OK);
        }
        memcpy(grown, s->arenas, (size_t)s->arena_count * sizeof(mem_arena));
        mem_give(ctx, s->arenas, (size_t)s->arena_cap * sizeof(mem_arena));
        s->arenas = grown;
        s->arena_cap = cap;
        a = &s->arenas[arena - 1u];
    }
    mem_arena *c = &s->arenas[s->arena_count];
    c->root = a->root;
    c->parent = arena;
    c->offset = a->offset + a->used;
    c->capacity = n;
    c->used = 0;
    c->generation = 0u;
    c->parent_generation = a->generation;
    c->storage = NULL;
    c->elem_bytes = a->elem_bytes;
    a->used += n;
    s->arena_count++;
    *out = s->arena_count;
    return true;
}

bool cint_mem_reset(cint_ctx *ctx, cint_site site, const cint_mem_table *t, uint32_t arena)
{
    struct mem_state *s = NULL;
    mem_arena *a = mem_member(ctx, site, t, arena, "arena.reset", &s);
    if (a == NULL) {
        return false;
    }
    if (a->generation + 1u == MEM_GEN_RETIRED) {
        return fault_refused(ctx, site);  /* M-16 gives E_OVERFLOW without its operands (REF-OQ-45) */
    }
    a->generation++;
    a->used = 0;
    return true;
}

static bool mem_check(cint_ctx *ctx, cint_site site, struct mem_state *s, uint32_t arena, const cint_handle *h)
{
    mem_arena *a = mem_arena_of(ctx, site, s, arena);
    uint32_t reason;
    if (a == NULL) {
        return false;
    }
    reason = mem_arena_reason(s, arena, h);
    if (reason != 0u) {
        return mem_stale(ctx, site, "handle.check", h, a->generation, reason);
    }
    return true;
}

bool cint_mem_view(cint_ctx *ctx, cint_site site, const cint_mem_table *t, uint32_t arena, cint_handle h,
                   cint_vdesc *v, cint_lease *lease)
{
    struct mem_state *s = mem_open(ctx, site, t);
    if (!mem_check(ctx, site, s, arena, &h)) {
        return false;
    }
    const mem_arena *a = &s->arenas[arena - 1u];
    memset(v, 0, sizeof *v);
    v->base = s->arenas[a->root].storage;
    v->origin = a->offset + (int64_t)h.slot;
    v->rank = 1;
    v->shape[0] = h.length;
    v->stride[0] = 1;
    memset(lease, 0, sizeof *lease);
    lease->arena = arena;
    lease->handle = h;
    return true;
}

bool cint_mem_lease(cint_ctx *ctx, cint_site site, const cint_mem_table *t, const cint_lease *lease)
{
    return mem_check(ctx, site, mem_open(ctx, site, t), lease->arena, &lease->handle);
}

bool cint_mem_insert(cint_ctx *ctx, cint_site site, const cint_mem_table *t, uint32_t pool, const void *value,
                     bool result, cint_handle *out, uint16_t *tag)
{
    mem_pool *p = mem_pool_of(ctx, site, mem_open(ctx, site, t), pool);
    int64_t slot;
    if (p == NULL) {
        return false;
    }
    memset(out, 0, sizeof *out);
    if (p->free_count > 0) {
        slot = p->free_list[--p->free_count];  /* last in, first out (M-18) */
    } else if (p->fresh < p->capacity) {
        slot = p->fresh++;
    } else if (result) {
        *tag = 1u;  /* AllocError.full */
        return true;
    } else {
        int64_t ops[3] = {p->occupied, p->retired, p->capacity};
        return fault_i64s(ctx, site, CINT_E_BOUNDS, "pool.insert", ops, 3u, false, 0);
    }
    memcpy(p->storage + (size_t)slot * p->elem_bytes, value, p->elem_bytes);
    p->marks[slot] |= MEM_OCCUPIED;
    p->occupied++;
    out->slot = (uint64_t)slot;
    out->length = 1;
    out->generation = p->gens[slot];
    out->container = pool;
    out->kind = CINT_HANDLE_POOL;
    if (tag != NULL) {
        *tag = 0u;
    }
    return true;
}

/* pool.deref of h in pool `id`: the slot, or -1 with the context faulted. */
static int64_t mem_slot(cint_ctx *ctx, cint_site site, const cint_mem_table *t, uint32_t id, const cint_handle *h,
                        mem_pool **out)
{
    mem_pool *p = mem_pool_of(ctx, site, mem_open(ctx, site, t), id);
    uint32_t reason;
    if (p == NULL) {
        return -1;
    }
    reason = mem_pool_reason(p, id, h);
    if (reason != 0u) {
        (void)mem_stale(ctx, site, "pool.deref", h, h->slot < (uint64_t)p->capacity ? p->gens[h->slot] : 0u, reason);
        return -1;
    }
    *out = p;
    return (int64_t)h->slot;
}

bool cint_mem_remove(cint_ctx *ctx, cint_site site, const cint_mem_table *t, uint32_t pool, cint_handle h)
{
    mem_pool *p = NULL;
    int64_t slot = mem_slot(ctx, site, t, pool, &h, &p);
    if (slot < 0) {
        return false;
    }
    memset(p->storage + (size_t)slot * p->elem_bytes, 0, p->elem_bytes);
    p->marks[slot] = (uint8_t)(p->marks[slot] & ~MEM_OCCUPIED);
    p->occupied--;
    if (p->gens[slot] + 1u == MEM_GEN_RETIRED) {
        p->marks[slot] |= MEM_RETIRED;  /* never reused (M-16) */
        p->retired++;
    } else {
        p->gens[slot]++;
        p->free_list[p->free_count++] = slot;
    }
    return true;
}

void *cint_mem_deref(cint_ctx *ctx, cint_site site, const cint_mem_table *t, uint32_t pool, cint_handle h)
{
    mem_pool *p = NULL;
    int64_t slot = mem_slot(ctx, site, t, pool, &h, &p);
    if (slot < 0) {
        return NULL;
    }
    return p->storage + (size_t)slot * p->elem_bytes;
}

bool cint_mem_frame_charge(cint_ctx *ctx, cint_site site, int64_t n)
{
    int64_t ops[3];
    if (cint_rt_faulted(ctx)) {
        return false;
    }
    ops[0] = n;
    ops[1] = ctx->scratch_used + ctx->frame_charged;
    ops[2] = ctx->frame_arena_bytes / 8;
    if (n < 0 || n > ops[2] - ops[1]) {
        return fault_i64s(ctx, site, CINT_E_BOUNDS, "arena.alloc", ops, 3u, false, 0);
    }
    ctx->frame_charged += n;
    return true;
}

int64_t cint_mem_frame_mark(const cint_ctx *ctx)
{
    return ctx->frame_charged;
}

void cint_mem_frame_release(cint_ctx *ctx, int64_t mark)
{
    if (mark >= 0 && mark < ctx->frame_charged) {
        ctx->frame_charged = mark;
    }
}
