/* cint_mem.c: arenas, pools and handles (rt/cint_mem.h; SPEC-03 M-12 to M-23; ref/OPEN.md REF-OQ-45;
 * decision 2026-10-06 on OQ-212).
 *
 * Part of the cint_rt library: rt/cint_rt.c includes this file at its end, so it shares that file's context
 * structure and fault writers, and every build that compiles cint_rt.c carries it. It keeps a size row of
 * its own (SPEC-09 4.2, tools/cint_lint.py). It is not compiled alone.
 *
 * A context's arenas are one table in ascending identifier order: the module-level arenas 1 to n in
 * declaration order, then every child in the order it was made, with the next identifier the context issues
 * (M-13, M-20); after a restore that identifier can lie past the table's last, since none is issued twice
 * (M-11). A child shares the storage of its top-level arena from its offset. A child is retired when its
 * parent's generation is no longer the one it was carved at, or its parent is retired: a reset retires every
 * descendant without visiting it (M-20). Every integer computation is on values proven in range.
 */
#define CINT_MEM_STATE 1  /* the structures of cint_mem.h that rt/cint_state.c shares */
#include "cint_mem.h"

/* Defined later in cint_rt.c (section 14). */
static bool fault_i64s(cint_ctx *ctx, cint_site site, uint32_t code, const char *operation, const int64_t *ops,
                       unsigned n, bool has_limit, int64_t limit);

/* SPEC-03 M-17 reasons, numbered by precedence, and `container` (REF-OQ-45). */
#define MEM_NULL 1u
#define MEM_RETIRED_REASON 2u
#define MEM_GENERATION 3u
#define MEM_EXTENT 4u
#define MEM_VACANT 5u
#define MEM_CONTAINER 6u

void *cint_mem_take(cint_ctx *ctx, size_t bytes)
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
    return count < 0 || (size != 0u && (uint64_t)count > (SIZE_MAX - 1u) / size) ? SIZE_MAX : (size_t)count * size;
}

void cint_mem_state_free(cint_ctx *ctx, struct mem_state *s)
{
    for (uint32_t k = 0u; k < s->arena_count; k++) {  /* a child's storage is NULL */
        mem_give(ctx, s->arenas[k].storage, mem_bytes(s->arenas[k].room, s->arenas[k].elem_bytes));
    }
    for (uint32_t k = 0u; k < s->pool_count; k++) {
        const mem_pool *p = &s->pools[k];
        mem_give(ctx, p->storage, mem_bytes(p->room, p->elem_bytes));
        mem_give(ctx, p->gens, mem_bytes(p->room, sizeof(uint64_t)));
        mem_give(ctx, p->marks, mem_bytes(p->room, 1u));
        mem_give(ctx, p->free_list, mem_bytes(p->room, sizeof(int64_t)));
    }
    mem_give(ctx, s->held, s->held_len);
    mem_give(ctx, s->arenas, (size_t)s->arena_cap * sizeof(mem_arena));
    mem_give(ctx, s->pools, (size_t)s->pool_cap * sizeof(mem_pool));
    mem_give(ctx, s, sizeof *s);
}

/* cint_ctx_destroy: the context's arenas and pools. */
static void mem_destroy(cint_ctx *ctx)
{
    if (ctx->mem != NULL) {
        cint_mem_state_free(ctx, ctx->mem);
        ctx->mem = NULL;
    }
}

bool cint_mem_table_ok(const cint_mem_table *t, uint32_t *arenas, uint32_t *pools)
{
    *arenas = *pools = 0u;
    if (t == NULL || (t->count > 0u && t->decls == NULL)) {
        return false;
    }
    for (uint32_t k = 0u; k < t->count; k++) {
        const cint_mem_decl *d = &t->decls[k];
        if ((d->kind != CINT_HANDLE_ARENA && d->kind != CINT_HANDLE_POOL) || d->elem_bytes == 0u || d->capacity < 0 ||
            d->type == NULL || d->type_len < 5u || (d->layout == NULL) != (d->layout_len == 0u) ||
            (d->cell != 0u && d->cell != 8u) || (d->name == NULL) != (d->name_len == 0u)) {
            return false;
        }
        *arenas += d->kind == CINT_HANDLE_ARENA ? 1u : 0u;
    }
    *pools = t->count - *arenas;
    return true;
}

uint32_t cint_mem_row(const struct mem_state *s, uint32_t id)
{
    uint32_t lo = 0u, hi = s->arena_count;
    if (id >= 1u && id <= hi && s->arenas[id - 1u].id == id) {
        return id - 1u;  /* no identifier skipped below it */
    }
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (s->arenas[mid].id < id) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return lo < s->arena_count && s->arenas[lo].id == id ? lo : UINT32_MAX;
}

/* Grows `n` arrays of a container, of the element sizes given, from `room` elements to its capacity: all or
 * none. */
static bool mem_regrow(cint_ctx *ctx, int64_t *room, int64_t capacity, unsigned n, void **arrays, const size_t *sizes)
{
    void *grown[4] = {NULL, NULL, NULL, NULL};
    bool ok = true;
    if (*room >= capacity) {
        return true;
    }
    for (unsigned i = 0u; ok && i < n; i++) {
        size_t bytes = mem_bytes(capacity, sizes[i]);
        ok = bytes != SIZE_MAX && (grown[i] = cint_mem_take(ctx, bytes)) != NULL;
    }
    for (unsigned i = 0u; i < n; i++) {
        if (ok && *room > 0) {
            memcpy(grown[i], arrays[i], (size_t)*room * sizes[i]);
        }
        mem_give(ctx, ok ? arrays[i] : grown[i], mem_bytes(ok ? *room : capacity, sizes[i]));
        arrays[i] = ok ? grown[i] : arrays[i];
    }
    *room = ok ? capacity : *room;
    return ok;
}

/* Whether tables a and b give their containers the same element types, stored alike. */
static bool mem_same_layout(const cint_mem_table *a, const cint_mem_table *b)
{
    for (uint32_t k = 0u; a->count == b->count && k < a->count; k++) {
        const cint_mem_decl *x = &a->decls[k], *y = &b->decls[k];
        if (x->kind != y->kind || x->elem_bytes != y->elem_bytes || x->cell != y->cell || x->type_len != y->type_len ||
            x->layout_len != y->layout_len || memcmp(x->type, y->type, x->type_len) != 0 ||
            (x->layout_len > 0u && memcmp(x->layout, y->layout, (size_t)x->layout_len * sizeof *x->layout) != 0)) {
            return false;
        }
    }
    return a->count == b->count;
}

cint_status cint_mem_adopt(cint_ctx *ctx, struct mem_state *s, const cint_mem_table *t)
{
    uint32_t a = 0u, p = 0u, arenas, pools;
    cint_status st = CINT_OK;
    if (!cint_mem_table_ok(t, &arenas, &pools) || pools != s->pool_count || arenas > s->arena_count ||
        (s->lay_out == NULL && s->laid != NULL && s->laid != t && !mem_same_layout(s->laid, t))) {
        return CINT_REFUSED;  /* a state laid out by a table that a failed adoption named, and t another */
    }
    if (s->lay_out != NULL && (st = s->lay_out(ctx, s, t)) != CINT_OK) {
        return st;
    }
    for (uint32_t k = 0u; k < t->count; k++) {
        const cint_mem_decl *d = &t->decls[k];
        const mem_arena *x = d->kind == CINT_HANDLE_ARENA ? &s->arenas[a++] : NULL;
        const mem_pool *q = x == NULL ? &s->pools[p++] : NULL;
        if (x != NULL ? x->id != a || x->parent != 0u || x->elem_bytes != d->elem_bytes || x->capacity != d->capacity
                      : q->elem_bytes != d->elem_bytes || q->capacity != d->capacity) {
            return CINT_REFUSED;
        }
    }
    for (; a < s->arena_count; a++) {
        if (s->arenas[a].parent == 0u) {
            return CINT_REFUSED;  /* a module-level arena that t does not declare */
        }
    }
    for (uint32_t k = 0u; k < s->arena_count + s->pool_count; k++) {
        mem_arena *x = k < s->arena_count ? &s->arenas[k] : NULL;
        mem_pool *q = x == NULL ? &s->pools[k - s->arena_count] : NULL;
        void *arrays[4] = {x != NULL ? (void *)x->storage : q->storage, x != NULL ? NULL : q->gens,
                           x != NULL ? NULL : q->marks, x != NULL ? NULL : q->free_list};
        size_t sizes[4] = {x != NULL ? x->elem_bytes : q->elem_bytes, sizeof(uint64_t), 1u, sizeof(int64_t)};
        if (x != NULL ? x->parent == 0u && !mem_regrow(ctx, &x->room, x->capacity, 1u, arrays, sizes)
                      : !mem_regrow(ctx, &q->room, q->capacity, 4u, arrays, sizes)) {
            return CINT_RESOURCE;
        }
        if (x != NULL) {
            x->storage = (unsigned char *)arrays[0];
        } else {
            q->storage = (unsigned char *)arrays[0];
            q->gens = (uint64_t *)arrays[1];
            q->marks = (uint8_t *)arrays[2];
            q->free_list = (int64_t *)arrays[3];
        }
    }
    s->table = s->laid = t;
    return CINT_OK;
}

/* A state of table t's arenas and pools as a restore leaves them before cint_mem_adopt gives them their
 * capacities: none used and no storage. NULL when memory runs out. */
static struct mem_state *mem_new(cint_ctx *ctx, const cint_mem_table *t, uint32_t arenas, uint32_t pools)
{
    struct mem_state *s = (struct mem_state *)cint_mem_take(ctx, sizeof *s);
    if (s == NULL) {
        return NULL;
    }
    s->arena_cap = arenas < 4u ? 4u : arenas;
    s->pool_cap = pools;
    s->arenas = (mem_arena *)cint_mem_take(ctx, (size_t)s->arena_cap * sizeof(mem_arena));
    s->pools = pools == 0u ? NULL : (mem_pool *)cint_mem_take(ctx, (size_t)pools * sizeof(mem_pool));
    if (s->arenas == NULL || (pools != 0u && s->pools == NULL)) {
        cint_mem_state_free(ctx, s);
        return NULL;
    }
    for (uint32_t k = 0u; k < t->count; k++) {
        const cint_mem_decl *d = &t->decls[k];
        if (d->kind == CINT_HANDLE_ARENA) {
            mem_arena *a = &s->arenas[s->arena_count];
            a->id = s->arena_count + 1u;
            a->root = s->arena_count++;
            a->capacity = d->capacity;
            a->elem_bytes = d->elem_bytes;
        } else {
            mem_pool *q = &s->pools[s->pool_count++];
            q->capacity = d->capacity;
            q->elem_bytes = d->elem_bytes;
        }
    }
    return s;
}

/* The arenas and pools of table t, made at the context's first call that names one (M-13, M-20), or restored
 * (rt/cint_state.c), which t then has to fit; NULL with the context faulted. */
static struct mem_state *mem_open(cint_ctx *ctx, cint_site site, const cint_mem_table *t)
{
    struct mem_state *s = ctx->mem;
    uint32_t arenas = 0u, pools = 0u;
    if (cint_rt_faulted(ctx)) {
        return NULL;
    }
    if (s == NULL) {
        if (!cint_mem_table_ok(t, &arenas, &pools)) {
            (void)fault_refused(ctx, site);
            return NULL;
        }
        if ((s = mem_new(ctx, t, arenas, pools)) != NULL && cint_mem_adopt(ctx, s, t) != CINT_OK) {
            cint_mem_state_free(ctx, s);  /* CINT_RESOURCE: a state made from t fits it */
            s = NULL;
        }
        if (s == NULL) {
            (void)fault_host(ctx, site, "host.resource", false, CINT_OK);
            return NULL;
        }
        ctx->last_arena = ctx->last_arena > arenas ? ctx->last_arena : arenas;
        ctx->last_pool = ctx->last_pool > pools ? ctx->last_pool : pools;
        ctx->mem = s;
    }
    if (s->table == NULL && cint_mem_adopt(ctx, s, t) == CINT_RESOURCE) {
        (void)fault_host(ctx, site, "host.resource", false, CINT_OK);
        return NULL;
    }
    if (t == NULL || s->table != t) {  /* NULL never fits; a restored state keeps table NULL until one does */
        (void)fault_refused(ctx, site);
        return NULL;
    }
    return s;
}

/* The row of arena or pool `id` of state s, or UINT32_MAX, with the context faulted unless s is NULL. */
static uint32_t mem_find(cint_ctx *ctx, cint_site site, const struct mem_state *s, uint32_t kind, uint32_t id)
{
    uint32_t k = s == NULL ? UINT32_MAX : kind == CINT_HANDLE_ARENA ? cint_mem_row(s, id)
               : id >= 1u && id <= s->pool_count ? id - 1u : UINT32_MAX;
    if (s != NULL && k == UINT32_MAX) {
        (void)fault_refused(ctx, site);
    }
    return k;
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

/* Whether arena k or one of its ancestors is retired (M-20). */
static bool mem_retired(const struct mem_state *s, uint32_t k)
{
    for (const mem_arena *a = &s->arenas[k]; a->parent != 0u; a = &s->arenas[a->parent - 1u]) {
        if (s->arenas[a->parent - 1u].generation != a->parent_generation) {
            return true;
        }
    }
    return false;
}

/* The first M-17 reason that applies to h in arena or pool `id`, row k of s, or 0, with the generation the
 * record gives in *current. */
static uint32_t mem_reason(const struct mem_state *s, uint32_t kind, uint32_t k, uint32_t id, const cint_handle *h,
                           uint64_t *current)
{
    const mem_arena *a = kind == CINT_HANDLE_ARENA ? &s->arenas[k] : NULL;
    const mem_pool *p = kind == CINT_HANDLE_POOL ? &s->pools[k] : NULL;
    bool inside = p != NULL && h->slot < (uint64_t)p->capacity;
    *current = a != NULL ? a->generation : inside ? p->gens[h->slot] : 0u;
    if (h->slot == 0u && h->length == 0 && h->generation == 0u && h->container == 0u && h->kind == 0u) {
        return MEM_NULL;
    }
    if ((uint32_t)h->kind != kind || h->container != id) {
        return MEM_CONTAINER;
    }
    if (a != NULL) {
        uint64_t used = (uint64_t)a->used;
        if (mem_retired(s, k)) {
            return MEM_RETIRED_REASON;
        }
        if (h->generation != a->generation) {
            return MEM_GENERATION;
        }
        return h->length < 0 || h->slot > used || (uint64_t)h->length > used - h->slot ? MEM_EXTENT : 0u;
    }
    if (!inside) {
        return MEM_EXTENT;  /* a slot past the capacity has no mark or generation */
    }
    if ((p->marks[h->slot] & MEM_RETIRED) != 0u) {
        return MEM_RETIRED_REASON;
    }
    if (h->generation != p->gens[h->slot]) {
        return MEM_GENERATION;
    }
    return (p->marks[h->slot] & MEM_OCCUPIED) == 0u ? MEM_VACANT : 0u;
}

/* handle.check of h in arena `id`, or pool.deref of h in pool `id`: the container's row in *out's state, or
 * UINT32_MAX with the context faulted. */
static uint32_t mem_check(cint_ctx *ctx, cint_site site, const cint_mem_table *t, uint32_t kind, uint32_t id,
                          const cint_handle *h, struct mem_state **out)
{
    struct mem_state *s = mem_open(ctx, site, t);
    uint32_t k = mem_find(ctx, site, s, kind, id), reason = 0u;
    uint64_t current = 0u;
    if (k == UINT32_MAX) {
        return k;
    }
    if ((reason = mem_reason(s, kind, k, id, h, &current)) != 0u) {
        (void)mem_stale(ctx, site, kind == CINT_HANDLE_ARENA ? "handle.check" : "pool.deref", h, current, reason);
        return UINT32_MAX;
    }
    *out = s;
    return k;
}

/* A member of arena `id` (M-14): faulted when it or an ancestor is retired. */
static mem_arena *mem_member(cint_ctx *ctx, cint_site site, const cint_mem_table *t, uint32_t id,
                             const char *operation, struct mem_state **out)
{
    struct mem_state *s = mem_open(ctx, site, t);
    uint32_t k = mem_find(ctx, site, s, CINT_HANDLE_ARENA, id);
    mem_arena *a = k != UINT32_MAX ? &s->arenas[k] : NULL;
    if (a == NULL) {
        return NULL;
    }
    if (mem_retired(s, k)) {
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
    if (a == NULL || !mem_fits(ctx, site, a, n, result, &full)) {
        if (full != 0u) {
            memset(out, 0, sizeof *out);
            *tag = full;
        }
        return full != 0u;
    }
    const mem_arena *r = &s->arenas[a->root];
    if (n > 0) {
        memset(r->storage + (size_t)(a->offset + a->used) * a->elem_bytes, 0, (size_t)n * a->elem_bytes);
    }
    cint_handle made = {(uint64_t)a->used, n, a->generation, arena, CINT_HANDLE_ARENA, {0u, 0u, 0u}};
    *out = made;
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
    uint32_t k = (uint32_t)(a - s->arenas);
    if (ctx->last_arena >= UINT32_MAX) {
        return fault_refused(ctx, site);  /* no clause gives the fault (REF-OQ-45) */
    }
    if (s->arena_count == s->arena_cap) {
        uint32_t cap = s->arena_cap > UINT32_MAX / 2u ? UINT32_MAX : 2u * s->arena_cap;
        mem_arena *grown = (mem_arena *)cint_mem_take(ctx, (size_t)cap * sizeof(mem_arena));
        if (grown == NULL) {
            return fault_host(ctx, site, "host.resource", false, CINT_OK);
        }
        memcpy(grown, s->arenas, (size_t)s->arena_count * sizeof(mem_arena));
        mem_give(ctx, s->arenas, (size_t)s->arena_cap * sizeof(mem_arena));
        s->arenas = grown;
        s->arena_cap = cap;
        a = &s->arenas[k];
    }
    mem_arena c = {.id = (uint32_t)++ctx->last_arena, .root = a->root, .parent = k + 1u,
                   .offset = a->offset + a->used, .capacity = n, .parent_generation = a->generation,
                   .elem_bytes = a->elem_bytes};
    a->used += n;
    s->arenas[s->arena_count++] = c;
    *out = c.id;
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

bool cint_mem_view(cint_ctx *ctx, cint_site site, const cint_mem_table *t, uint32_t arena, cint_handle h,
                   cint_vdesc *v, cint_lease *lease)
{
    struct mem_state *s = NULL;
    uint32_t k = mem_check(ctx, site, t, CINT_HANDLE_ARENA, arena, &h, &s);
    if (k == UINT32_MAX) {
        return false;
    }
    memset(v, 0, sizeof *v);
    v->base = s->arenas[s->arenas[k].root].storage;
    v->origin = s->arenas[k].offset + (int64_t)h.slot;
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
    struct mem_state *s = NULL;
    return mem_check(ctx, site, t, CINT_HANDLE_ARENA, lease->arena, &lease->handle, &s) != UINT32_MAX;
}

bool cint_mem_insert(cint_ctx *ctx, cint_site site, const cint_mem_table *t, uint32_t pool, const void *value,
                     bool result, cint_handle *out, uint16_t *tag)
{
    struct mem_state *s = mem_open(ctx, site, t);
    uint32_t k = mem_find(ctx, site, s, CINT_HANDLE_POOL, pool);
    mem_pool *p = k != UINT32_MAX ? &s->pools[k] : NULL;
    int64_t slot = 0;
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
    cint_handle made = {(uint64_t)slot, 1, p->gens[slot], pool, CINT_HANDLE_POOL, {0u, 0u, 0u}};
    *out = made;
    if (tag != NULL) {
        *tag = 0u;
    }
    return true;
}

bool cint_mem_remove(cint_ctx *ctx, cint_site site, const cint_mem_table *t, uint32_t pool, cint_handle h)
{
    struct mem_state *s = NULL;
    uint32_t k = mem_check(ctx, site, t, CINT_HANDLE_POOL, pool, &h, &s);
    mem_pool *p = k != UINT32_MAX ? &s->pools[k] : NULL;
    if (p == NULL) {
        return false;
    }
    memset(p->storage + (size_t)h.slot * p->elem_bytes, 0, p->elem_bytes);
    p->marks[h.slot] = (uint8_t)(p->marks[h.slot] & ~MEM_OCCUPIED);
    p->occupied--;
    if (p->gens[h.slot] + 1u == MEM_GEN_RETIRED) {
        p->marks[h.slot] |= MEM_RETIRED;  /* never reused (M-16) */
        p->retired++;
    } else {
        p->gens[h.slot]++;
        p->free_list[p->free_count++] = (int64_t)h.slot;
    }
    return true;
}

void *cint_mem_deref(cint_ctx *ctx, cint_site site, const cint_mem_table *t, uint32_t pool, cint_handle h)
{
    struct mem_state *s = NULL;
    uint32_t k = mem_check(ctx, site, t, CINT_HANDLE_POOL, pool, &h, &s);
    return k != UINT32_MAX ? s->pools[k].storage + (size_t)h.slot * s->pools[k].elem_bytes : NULL;
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
