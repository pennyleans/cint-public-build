/* cint_state.c: checkpoints of a context (rt/cint_state.h; roadmap box 13, unit 4).
 *
 * The canonical state of SPEC-01 IM-156 in the byte layout below, which unit 4 writes
 * (SPEC-03 question 10; rt/OPEN.md RT-OQ-42), the CINT-CKPT-1 envelope (SPEC-03 H-19; SPEC-06
 * 15), the validating decoder (H-19a), restore (M-11, M-11a), the interrupt, and the change of
 * revision (H-19b). Integers are little-endian; a string is a U32 length and its bytes.
 *
 *   checkpoint  "cint-core-1/checkpoint/v1"; the runtime contract version; the presence bytes
 *               of the payload digest, the build record digest and the build identity, each
 *               followed by 32 bytes when 1 (written 0 until build records exist, SPEC-07
 *               SEC-REC-11); the U64 length of the state; the state; nothing after it
 *   state       "cint-core-1/state/v1"; the revision identity (32 bytes); "cint-core-1"; a U64
 *               count and the globals, sorted by the bytes of their names; the arena, pool and
 *               registry blocks, each a U64 length (0 for the arena and pool blocks until
 *               roadmap box 12, and for an empty registry) and its bytes; the next buffer,
 *               arena and pool identifiers and the entry sequence number, U64 each;
 *               "fuel-v1"; the fault record, a presence byte and its IM-149 bytes; and the
 *               effect-log position, an I64
 *   global      the fully qualified name (variable `x` of `a/b.ci` is `a.b.x`, SPEC-04 LS-225)
 *               and the tagged value (SPEC-01 11.1)
 *   registry    "cint-core-1/registry/v1"; a U64 count, at least 1; each live registration in
 *               ascending identifier order, 37 bytes: identifier U64, generation U64, owner U8
 *               (CINT_OWNER_*), element type code U16, storage U8, frac_bits U16 and record_id
 *               U32, extent I64 (bytes for a registration without an element type, code 0),
 *               ceiling U8, publish U8, state U8 (0, live). No contents: none of these buffers
 *               is context-owned (M-3).
 */
#define _CRT_SECURE_NO_WARNINGS 1  /* fopen (cint_checkpoint_show) */
#include "cint_rt_internal.h"
#include "cint_state.h"

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char CKPT_DOMAIN[] = "cint-core-1/checkpoint/v1";
static const char STATE_DOMAIN[] = "cint-core-1/state/v1";
static const char REGISTRY_DOMAIN[] = "cint-core-1/registry/v1";
static const char FAULT_DOMAIN[] = "cint-core-1/fault/v2";
static const char PROFILE[] = "cint-core-1";
static const char FUEL_MODEL[] = "fuel-v1";
#define LIT(s) (s), (sizeof (s) - 1u)
#define ENTRY_BYTES 37u
#define CONTRACT_MAX 64u
#define KERNEL_MAX 256u
_Static_assert(sizeof CINT_RT_CONTRACT - 1u <= CONTRACT_MAX, "contract");
_Static_assert(sizeof ((cint_fault_address *)0)->name == KERNEL_MAX, "kernel name");

/* The table of a context bound to a program without module state. */
static const cint_state_table NO_STATE = {0u, 0u, NULL};

static void *mem_alloc(cint_ctx *ctx, size_t bytes, size_t align)
{
    return bytes == 0u ? NULL : ctx->allocator.alloc(ctx->allocator.user, bytes, align);
}

static void mem_free(cint_ctx *ctx, void *p, size_t bytes, size_t align)
{
    if (p != NULL) {
        ctx->allocator.release(ctx->allocator.user, p, bytes, align);
    }
}

static int64_t as_i64(uint64_t u)
{
    int64_t v;
    memcpy(&v, &u, sizeof v);
    return v;
}

/* Bool, or I8 to U64: the bytes of the value; 0 for any other tag. */
static uint32_t width_of(uint32_t tag)
{
    if (tag == CINT_TAG_BOOL) {
        return 1u;
    }
    if ((tag >= CINT_TAG_I8 && tag <= CINT_TAG_I64) || (tag >= CINT_TAG_U8 && tag <= CINT_TAG_U64)) {
        return cint_rt_tag_width(tag) / 8u;
    }
    return 0u;
}

static bool utf8_ok(const uint8_t *s, size_t n)
{
    size_t i = 0u;
    while (i < n) {
        uint32_t c = s[i], k, least;
        if (c < 0x80u) {
            i++;
            continue;
        }
        if (c >= 0xc2u && c <= 0xdfu) {
            k = 1u, c &= 0x1fu, least = 0x80u;
        } else if (c >= 0xe0u && c <= 0xefu) {
            k = 2u, c &= 0x0fu, least = 0x800u;
        } else if (c >= 0xf0u && c <= 0xf4u) {
            k = 3u, c &= 0x07u, least = 0x10000u;
        } else {
            return false;
        }
        if (n - i - 1u < k) {
            return false;
        }
        for (uint32_t j = 1u; j <= k; j++) {
            uint32_t b = s[i + j];
            if ((b & 0xc0u) != 0x80u) {
                return false;
            }
            c = (c << 6) | (b & 0x3fu);
        }
        if (c < least || c > 0x10ffffu || (c >= 0xd800u && c <= 0xdfffu)) {
            return false;
        }
        i += k + 1u;
    }
    return true;
}

/* Byte order, a prefix before the longer name. */
static int name_cmp(const uint8_t *a, size_t an, const uint8_t *b, size_t bn)
{
    int c = memcmp(a, b, an < bn ? an : bn);
    return c != 0 ? c : an < bn ? -1 : an > bn ? 1 : 0;
}

/* ------------------------------------------------------------------------- */
/* Writing and reading bytes.                                                 */

typedef struct wr {
    uint8_t *buf;  /* NULL: count only */
    size_t len, cap;
} wr;

static wr wr_of(uint8_t *buf, size_t cap)
{
    wr w;
    w.buf = buf;
    w.len = 0u;
    w.cap = cap;
    return w;
}

static void w_bytes(wr *w, const void *p, size_t n)
{
    if (w->buf != NULL && n > 0u) {
        memcpy(w->buf + w->len, p, n);
    }
    w->len += n;
}

static void w_le(wr *w, uint64_t v, unsigned n)
{
    uint8_t b[8];
    for (unsigned i = 0u; i < n; i++) {
        b[i] = (uint8_t)(v >> (8u * i));
    }
    w_bytes(w, b, n);
}

static void w_str(wr *w, const void *s, size_t n)
{
    w_le(w, n, 4u);
    w_bytes(w, s, n);
}

typedef struct rd {
    const uint8_t *p;
    size_t n, at;
    bool bad;
} rd;

static rd rd_of(const uint8_t *p, size_t n)
{
    rd r;
    r.p = p;
    r.n = n;
    r.at = 0u;
    r.bad = false;
    return r;
}

static uint64_t rd_u(rd *r, unsigned k)
{
    uint64_t v = 0u;
    if (r->bad || r->n - r->at < k) {
        r->bad = true;
        return 0u;
    }
    for (unsigned i = 0u; i < k; i++) {
        v |= (uint64_t)r->p[r->at + i] << (8u * i);
    }
    r->at += k;
    return v;
}

static const uint8_t *rd_take(rd *r, uint64_t k)
{
    const uint8_t *p;
    if (r->bad || (uint64_t)(r->n - r->at) < k) {
        r->bad = true;
        return NULL;
    }
    p = r->p + r->at;
    r->at += (size_t)k;
    return p;
}

static bool rd_flag(rd *r)
{
    uint64_t v = rd_u(r, 1u);
    r->bad = r->bad || v > 1u;
    return v == 1u;
}

static void rd_lit(rd *r, const char *s, size_t n)
{
    const uint8_t *p = rd_u(r, 4u) == n ? rd_take(r, n) : NULL;
    if (p == NULL || memcmp(p, s, n) != 0) {
        r->bad = true;
    }
}

/* A tagged value (SPEC-01 11.1) into v when v is not NULL. A Z is minimal and at most 257
 * bytes, the bound of a run-time record (IM-108); `scalar` admits Bool and I8 to U64 only. */
static void rd_value(rd *r, cint_tvalue *v, bool scalar)
{
    size_t start = r->at;
    uint32_t tag = (uint32_t)rd_u(r, 1u);
    if (tag == CINT_TAG_Z && !scalar) {
        uint64_t n = rd_u(r, 4u);
        const uint8_t *p = n <= CINT_Z_MAX_BYTES ? rd_take(r, n) : NULL;
        if (p == NULL || (n == 1u && p[0] == 0u) ||
            (n >= 2u && ((p[n - 1u] == 0u && p[n - 2u] < 0x80u) || (p[n - 1u] == 0xffu && p[n - 2u] >= 0x80u)))) {
            r->bad = true;
        }
    } else if (width_of(tag) != 0u) {
        const uint8_t *p = rd_take(r, width_of(tag));
        if (p != NULL && tag == CINT_TAG_BOOL && p[0] > 1u) {
            r->bad = true;
        }
    } else {
        r->bad = true;
    }
    if (!r->bad && v != NULL) {
        v->len = (uint16_t)(r->at - start);
        memcpy(v->bytes, r->p + start, r->at - start);
    }
}

/* A position of a fault record: path, line, column (SPEC-01 11.2). */
static void rd_pos(rd *r, cint_position *p)
{
    uint64_t n = rd_u(r, 4u);
    const uint8_t *s = n >= 1u && n <= CINT_FAULT_MAX_PATH ? rd_take(r, n) : NULL;
    memset(p, 0, sizeof *p);
    if (s == NULL || !utf8_ok(s, (size_t)n)) {
        r->bad = true;
        return;
    }
    p->path = (const char *)s;
    p->path_len = (uint32_t)n;
    p->line = (uint32_t)rd_u(r, 4u);
    p->column = (uint32_t)rd_u(r, 4u);
    if (p->line == 0u || p->column == 0u) {
        r->bad = true;
    }
}

/* A fault record of encoding `cint-core-1/fault/v2` (SPEC-01 IM-149), read as the
 * workbench's decoder reads it (workbench/wb_fault.c) with the run-time bounds. */
typedef struct fault_view {
    uint16_t code;
    uint8_t operation_len, operand_count, phase;
    bool has_exact, has_limit, has_revision, has_map, has_address, has_work_item;
    const uint8_t *operation, *revision, *map, *kernel;
    cint_tvalue operands[CINT_FAULT_MAX_OPERANDS], exact, limit;
    cint_position position;
    uint32_t kernel_len, stack_count;
    int64_t dispatch, work_item, step;
    size_t stack_at;  /* where the call positions begin */
} fault_view;

static void rd_fault(rd *r, fault_view *f)
{
    uint64_t n;
    memset(f, 0, sizeof *f);
    rd_lit(r, LIT(FAULT_DOMAIN));
    f->code = (uint16_t)rd_u(r, 2u);
    n = rd_u(r, 4u);
    f->operation = n >= 1u && n <= CINT_FAULT_MAX_OPERATION ? rd_take(r, n) : NULL;
    if (f->code == 0u || f->code > CINT_E_ASSERT || f->operation == NULL) {
        r->bad = true;
        return;
    }
    f->operation_len = (uint8_t)n;
    for (uint32_t i = 0u; i < f->operation_len; i++) {
        uint8_t c = f->operation[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_')) {
            r->bad = true;
        }
    }
    n = rd_u(r, 4u);
    if (n > CINT_FAULT_MAX_OPERANDS) {
        r->bad = true;
        return;
    }
    f->operand_count = (uint8_t)n;
    for (uint32_t i = 0u; i < f->operand_count; i++) {
        rd_value(r, &f->operands[i], false);
    }
    f->has_exact = rd_flag(r);
    if (f->has_exact) {
        rd_value(r, &f->exact, false);
        r->bad = r->bad || f->exact.bytes[0] != CINT_TAG_Z;
    }
    f->has_limit = rd_flag(r);
    if (f->has_limit) {
        rd_value(r, &f->limit, false);
    }
    rd_pos(r, &f->position);
    f->has_revision = rd_flag(r);
    f->revision = f->has_revision ? rd_take(r, 32u) : NULL;
    f->has_map = rd_flag(r);
    f->map = f->has_map ? rd_take(r, 32u) : NULL;
    f->has_address = rd_flag(r);
    if (f->has_address) {
        n = rd_u(r, 4u);
        f->kernel = n <= KERNEL_MAX ? rd_take(r, n) : NULL;
        f->kernel_len = (uint32_t)n;
        r->bad = r->bad || f->kernel == NULL || !utf8_ok(f->kernel, (size_t)n);
        f->dispatch = as_i64(rd_u(r, 8u));
        f->phase = (uint8_t)rd_u(r, 1u);
        r->bad = r->bad || f->phase > 2u;
        f->has_work_item = rd_flag(r);
        if (f->has_work_item) {
            r->bad = r->bad || f->phase != 1u;  /* a work item only in the work-item phase */
            f->work_item = as_i64(rd_u(r, 8u));
            f->step = as_i64(rd_u(r, 8u));
        }
    }
    n = rd_u(r, 4u);
    if (n > CINT_RT_MAX_DEPTH) {
        r->bad = true;
        return;
    }
    f->stack_count = (uint32_t)n;
    f->stack_at = r->at;
    for (uint32_t i = 0u; i < f->stack_count && !r->bad; i++) {
        cint_position p;
        rd_pos(r, &p);
    }
}

/* One registry entry, checked as H-19a requires of it alone. */
static void rd_entry(rd *r, cint_checkpoint_buffer *b)
{
    uint64_t state;
    uint16_t code;
    memset(b, 0, sizeof *b);
    b->id = rd_u(r, 8u);
    b->generation = rd_u(r, 8u);
    b->owner = (uint32_t)rd_u(r, 1u);
    b->type.code = code = (uint16_t)rd_u(r, 2u);
    b->type.storage = (uint8_t)rd_u(r, 1u);
    b->type.frac_bits = (uint16_t)rd_u(r, 2u);
    b->type.record_id = (uint32_t)rd_u(r, 4u);
    b->extent = as_i64(rd_u(r, 8u));
    b->ceiling = (uint8_t)rd_u(r, 1u);
    b->publish = (uint8_t)rd_u(r, 1u);
    state = rd_u(r, 1u);
    if (b->id == 0u || b->generation == 0u || b->generation > UINT64_MAX - 1u ||
        (b->owner != CINT_OWNER_BORROWED && b->owner != CINT_OWNER_BRIDGE) || b->type.storage != 0u ||
        b->type.frac_bits != 0u || (code != CINT_TAG_RECORD && b->type.record_id != 0u) ||
        (code != 0u && code != CINT_TAG_RECORD && width_of(code) == 0u) ||
        (code == 0u && b->owner != CINT_OWNER_BORROWED) || b->extent < 0 || b->ceiling > CINT_VIEW_WRITE ||
        b->publish > CINT_PUBLISH_COPY || (b->publish != CINT_PUBLISH_NONE && b->ceiling != CINT_VIEW_WRITE) ||
        state != 0u) {
        r->bad = true;
    }
}

/* The canonical state bytes of c->state, checked as H-19a requires of everything that does
 * not need the program, with the offsets of c filled. */
static bool rd_state(cint_checkpoint_info *c)
{
    rd r = rd_of(c->state, c->state_len);
    const uint8_t *p, *prev = NULL;
    uint64_t n, prev_len = 0u, last_id = 0u;
    rd_lit(&r, LIT(STATE_DOMAIN));
    if ((p = rd_take(&r, 32u)) != NULL) {
        memcpy(c->revision, p, 32u);
    }
    rd_lit(&r, LIT(PROFILE));
    c->global_count = rd_u(&r, 8u);
    c->globals_at = r.at;
    for (uint64_t i = 0u; i < c->global_count && !r.bad; i++) {
        n = rd_u(&r, 4u);
        p = n >= 1u ? rd_take(&r, n) : NULL;
        if (p == NULL || !utf8_ok(p, (size_t)n) || (prev != NULL && name_cmp(prev, (size_t)prev_len, p, (size_t)n) >= 0)) {
            r.bad = true;
        }
        prev = p, prev_len = n;
        rd_value(&r, NULL, true);
    }
    c->globals_end = r.at;
    if (rd_u(&r, 8u) != 0u || rd_u(&r, 8u) != 0u) {
        r.bad = true;  /* the arena and pool blocks hold nothing before roadmap box 12 */
    }
    n = rd_u(&r, 8u);
    c->buffers_at = c->buffers_end = r.at;
    if (n > 0u && !r.bad) {
        size_t end = n <= (uint64_t)(r.n - r.at) ? r.at + (size_t)n : 0u;
        rd_lit(&r, LIT(REGISTRY_DOMAIN));
        c->buffer_count = rd_u(&r, 8u);
        if (r.bad || r.at > end || c->buffer_count == 0u || (end - r.at) % ENTRY_BYTES != 0u ||
            (end - r.at) / ENTRY_BYTES != c->buffer_count) {
            r.bad = true;
        }
        c->buffers_at = r.at;
        for (uint64_t i = 0u; i < c->buffer_count && !r.bad; i++) {
            cint_checkpoint_buffer b;
            rd_entry(&r, &b);
            r.bad = r.bad || b.id <= last_id;
            last_id = b.id;
        }
        c->buffers_end = r.at;
    }
    c->next_buffer = rd_u(&r, 8u);
    c->next_arena = rd_u(&r, 8u);
    c->next_pool = rd_u(&r, 8u);
    c->entries = rd_u(&r, 8u);
    if (c->next_buffer <= last_id || c->next_buffer == 0u || c->next_arena == 0u || c->next_pool == 0u) {
        r.bad = true;  /* identifier counters above every identifier present */
    }
    rd_lit(&r, LIT(FUEL_MODEL));
    if (rd_flag(&r) && !r.bad) {
        fault_view f;
        size_t at = r.at;
        rd_fault(&r, &f);
        c->fault = c->state + at;
        c->fault_len = r.at - at;
    }
    c->effects = as_i64(rd_u(&r, 8u));
    return !r.bad && c->effects >= 0 && r.at == r.n;
}

cint_status cint_checkpoint_decode(const uint8_t *bytes, size_t len, cint_checkpoint_info *out)
{
    cint_checkpoint_info c;
    rd r = rd_of(bytes, len);
    uint64_t n;
    if (bytes == NULL || out == NULL) {
        return CINT_REFUSED;
    }
    memset(&c, 0, sizeof c);
    rd_lit(&r, LIT(CKPT_DOMAIN));
    n = rd_u(&r, 4u);
    c.contract.bytes = (const char *)(n >= 1u && n <= CONTRACT_MAX ? rd_take(&r, n) : NULL);
    c.contract.len = (uint32_t)n;
    for (uint32_t i = 0u; c.contract.bytes != NULL && i < c.contract.len; i++) {
        r.bad = r.bad || c.contract.bytes[i] < 0x21 || c.contract.bytes[i] > 0x7e;
    }
    r.bad = r.bad || c.contract.bytes == NULL;
    c.payload_digest = rd_flag(&r) ? rd_take(&r, 32u) : NULL;
    c.build_record_digest = rd_flag(&r) ? rd_take(&r, 32u) : NULL;
    c.build_identity = rd_flag(&r) ? rd_take(&r, 32u) : NULL;
    n = rd_u(&r, 8u);
    if (r.bad || n != (uint64_t)(r.n - r.at)) {
        return CINT_REFUSED;
    }
    c.state = bytes + r.at;
    c.state_len = (size_t)n;
    if (!rd_state(&c)) {
        return CINT_REFUSED;
    }
    *out = c;
    return CINT_OK;
}

cint_status cint_checkpoint_global_next(const cint_checkpoint_info *c, size_t *cursor, cint_checkpoint_global *out)
{
    cint_checkpoint_global g;
    rd r;
    uint64_t n;
    if (c == NULL || cursor == NULL || out == NULL || c->state == NULL || c->globals_end > c->state_len) {
        return CINT_REFUSED;
    }
    r = rd_of(c->state, c->globals_end);
    r.at = *cursor == 0u ? c->globals_at : *cursor;
    if (r.at < c->globals_at || r.at >= c->globals_end) {
        return CINT_REFUSED;
    }
    n = rd_u(&r, 4u);
    g.name.bytes = (const char *)(n >= 1u ? rd_take(&r, n) : NULL);
    g.name.len = (uint32_t)n;
    g.name.reserved = 0u;
    rd_value(&r, &g.value, true);
    if (r.bad || g.name.bytes == NULL) {
        return CINT_REFUSED;
    }
    *cursor = r.at;
    *out = g;
    return CINT_OK;
}

cint_status cint_checkpoint_buffer_next(const cint_checkpoint_info *c, size_t *cursor, cint_checkpoint_buffer *out)
{
    cint_checkpoint_buffer b;
    rd r;
    if (c == NULL || cursor == NULL || out == NULL || c->state == NULL || c->buffers_end > c->state_len) {
        return CINT_REFUSED;
    }
    r = rd_of(c->state, c->buffers_end);
    r.at = *cursor == 0u ? c->buffers_at : *cursor;
    if (r.at < c->buffers_at || r.at >= c->buffers_end) {
        return CINT_REFUSED;
    }
    rd_entry(&r, &b);
    if (r.bad) {
        return CINT_REFUSED;
    }
    *cursor = r.at;
    *out = b;
    return CINT_OK;
}

/* ------------------------------------------------------------------------- */
/* The globals of a state table.                                              */

typedef struct gref {
    const uint8_t *name;  /* the qualified name, in the list's name bytes */
    uint32_t len, tag, width, offset;
    const cint_state *state;
} gref;

typedef struct glist {
    gref *g;
    uint8_t *names;
    size_t count, name_bytes;
} glist;

static int gref_cmp(const void *a, const void *b)
{
    const gref *x = (const gref *)a, *y = (const gref *)b;
    return name_cmp(x->name, x->len, y->name, y->len);
}

static void glist_free(cint_ctx *ctx, glist *l)
{
    mem_free(ctx, l->g, l->count * sizeof(gref), _Alignof(gref));
    mem_free(ctx, l->names, l->name_bytes, 1u);
    memset(l, 0, sizeof *l);
}

/* Every variable of table t over program p, sorted by qualified name. CINT_REFUSED for a
 * table that does not fit p (cint_ctx_bind_state lists the cases). */
static cint_status globals(cint_ctx *ctx, const cint_program *p, const cint_state_table *t, glist *out)
{
    size_t count = 0u, bytes = 0u, at = 0u, k = 0u;
    memset(out, 0, sizeof *out);
    if (t->reserved != 0u || (t->count > 0u && t->states == NULL)) {
        return CINT_REFUSED;
    }
    for (uint32_t i = 0u; i < t->count; i++) {
        const cint_state *s = t->states[i];
        const cint_module *m;
        if (s == NULL || s->module >= p->module_count || s->bytes == 0u || s->init == NULL || s->reserved != 0u ||
            (s->var_count > 0u && s->vars == NULL)) {
            return CINT_REFUSED;
        }
        m = p->modules[s->module];
        if (m->path_len < 4u || memcmp(m->path + m->path_len - 3u, ".ci", 3u) != 0) {
            return CINT_REFUSED;
        }
        for (uint32_t j = 0u; j < i; j++) {
            if (t->states[j]->module == s->module) {
                return CINT_REFUSED;
            }
        }
        for (uint32_t j = 0u; j < s->var_count; j++) {
            const cint_state_var *v = &s->vars[j];
            uint32_t w = width_of(v->tag);
            if (v->name == NULL || v->name_len == 0u || w == 0u || v->reserved != 0u || v->offset > s->bytes ||
                w > s->bytes - v->offset || !utf8_ok((const uint8_t *)v->name, v->name_len)) {
                return CINT_REFUSED;
            }
            bytes += (size_t)m->path_len - 2u + v->name_len;
        }
        count += s->var_count;
    }
    if (count == 0u) {
        return CINT_OK;
    }
    out->g = (gref *)mem_alloc(ctx, count * sizeof(gref), _Alignof(gref));
    out->names = (uint8_t *)mem_alloc(ctx, bytes, 1u);
    out->count = count;
    out->name_bytes = bytes;
    if (out->g == NULL || out->names == NULL) {
        glist_free(ctx, out);
        return CINT_RESOURCE;
    }
    for (uint32_t i = 0u; i < t->count; i++) {
        const cint_state *s = t->states[i];
        const cint_module *m = p->modules[s->module];
        for (uint32_t j = 0u; j < s->var_count; j++, k++) {
            const cint_state_var *v = &s->vars[j];
            gref *g = &out->g[k];
            g->name = out->names + at;
            g->len = m->path_len - 2u + v->name_len;
            for (uint32_t q = 0u; q + 3u < m->path_len; q++) {
                out->names[at++] = (uint8_t)(m->path[q] == '/' ? '.' : m->path[q]);
            }
            out->names[at++] = '.';
            memcpy(out->names + at, v->name, v->name_len);
            at += v->name_len;
            g->tag = v->tag;
            g->width = width_of(v->tag);
            g->offset = v->offset;
            g->state = s;
        }
    }
    qsort(out->g, count, sizeof(gref), gref_cmp);
    for (k = 1u; k < count; k++) {
        if (gref_cmp(&out->g[k - 1u], &out->g[k]) == 0) {
            glist_free(ctx, out);
            return CINT_REFUSED;
        }
    }
    return CINT_OK;
}

/* The bytes of a module's state: its block once the module has been accessed, before
 * that its initial image. */
static const uint8_t *block_of(const cint_ctx *ctx, const cint_state *s)
{
    return ctx->state != NULL && ctx->state[s->module] != NULL ? (const uint8_t *)ctx->state[s->module]
                                                                : (const uint8_t *)s->init;
}

/* True when t lists the cint_state of every block the context holds. */
static bool covered(const cint_ctx *ctx, const cint_state_table *t)
{
    uint32_t n = ctx->program->module_count;
    for (uint32_t m = 0u; ctx->state != NULL && m < n; m++) {
        bool found = ctx->state[m] == NULL;
        for (uint32_t i = 0u; !found && i < t->count; i++) {
            found = t->states[i] == (const cint_state *)ctx->state[n + m];
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

static void blocks_free(cint_ctx *ctx, void **state, uint32_t n)
{
    for (uint32_t m = 0u; state != NULL && m < n; m++) {
        if (state[m] != NULL) {
            mem_free(ctx, state[m], ((const cint_state *)state[n + m])->bytes, 8u);
        }
    }
    mem_free(ctx, (void *)state, (size_t)n * 2u * sizeof(void *), _Alignof(void *));
}

/* Gives every module of t its block, as its first access would (cint_rt_state). A block
 * made here holds the module's initial image, which is the state the module already had. */
static cint_status blocks_make(cint_ctx *ctx, void ***state, uint32_t n, const cint_state_table *t)
{
    if (t->count > 0u && *state == NULL) {
        *state = (void **)mem_alloc(ctx, (size_t)n * 2u * sizeof(void *), _Alignof(void *));
        if (*state == NULL) {
            return CINT_RESOURCE;
        }
        for (size_t k = 0u; k < (size_t)n * 2u; k++) {
            (*state)[k] = NULL;
        }
    }
    for (uint32_t i = 0u; i < t->count; i++) {
        const cint_state *s = t->states[i];
        if ((*state)[s->module] == NULL) {
            void *b = mem_alloc(ctx, s->bytes, 8u);
            if (b == NULL) {
                return CINT_RESOURCE;
            }
            memcpy(b, s->init, s->bytes);
            (*state)[s->module] = b;
            (*state)[n + s->module] = (void *)(uintptr_t)s;
        }
    }
    return CINT_OK;
}

cint_status cint_ctx_bind_state(cint_ctx *ctx, const cint_state_table *t)
{
    glist g;
    cint_status st;
    if (ctx == NULL) {
        return CINT_REFUSED;
    }
    if (!cint_rt_busy_take(ctx)) {
        return CINT_BUSY;
    }
    t = t != NULL ? t : &NO_STATE;
    st = globals(ctx, ctx->program, t, &g);
    if (st == CINT_OK && !covered(ctx, t)) {
        st = CINT_REFUSED;
    }
    if (st == CINT_OK) {
        ctx->states = t;
    }
    glist_free(ctx, &g);
    cint_rt_busy_give(ctx);
    return st;
}

/* ------------------------------------------------------------------------- */
/* The canonical state, the state hash, and the checkpoint.                   */

typedef struct snap {
    glist g;
    const buffer_slot **live;  /* the live registrations, by identifier */
    size_t live_count;
    uint8_t *bytes;
    size_t len;
} snap;

static int slot_cmp(const void *a, const void *b)
{
    cint_buffer_id x = (*(const buffer_slot *const *)a)->id, y = (*(const buffer_slot *const *)b)->id;
    return x < y ? -1 : x > y ? 1 : 0;
}

static void snap_free(cint_ctx *ctx, snap *s)
{
    glist_free(ctx, &s->g);
    mem_free(ctx, (void *)s->live, s->live_count * sizeof *s->live, _Alignof(const buffer_slot *));
    mem_free(ctx, s->bytes, s->len, 8u);
    memset(s, 0, sizeof *s);
}

static void put_state(const cint_ctx *ctx, const snap *s, wr *w)
{
    w_str(w, LIT(STATE_DOMAIN));
    w_bytes(w, ctx->program->revision, 32u);
    w_str(w, LIT(PROFILE));
    w_le(w, s->g.count, 8u);
    for (size_t i = 0u; i < s->g.count; i++) {
        const gref *g = &s->g.g[i];
        w_str(w, g->name, g->len);
        w_le(w, g->tag, 1u);
        w_bytes(w, block_of(ctx, g->state) + g->offset, g->width);  /* little-endian hosts (IM-146) */
    }
    w_le(w, 0u, 8u);  /* the arena block */
    w_le(w, 0u, 8u);  /* the pool block */
    w_le(w, s->live_count == 0u ? 0u : 4u + sizeof REGISTRY_DOMAIN - 1u + 8u + ENTRY_BYTES * s->live_count, 8u);
    if (s->live_count > 0u) {
        w_str(w, LIT(REGISTRY_DOMAIN));
        w_le(w, s->live_count, 8u);
    }
    for (size_t i = 0u; i < s->live_count; i++) {
        const buffer_slot *b = s->live[i];
        w_le(w, b->id, 8u);
        w_le(w, b->generation, 8u);
        w_le(w, b->storage == STORAGE_CREATED ? CINT_OWNER_BRIDGE : CINT_OWNER_BORROWED, 1u);
        w_le(w, b->type.code, 2u);  /* 0 for a registration without an element type */
        w_le(w, b->type.storage, 1u);
        w_le(w, b->type.frac_bits, 2u);
        w_le(w, b->type.record_id, 4u);
        w_le(w, (uint64_t)(b->typed != 0u ? b->extent : b->bytes), 8u);
        w_le(w, b->perm, 1u);
        w_le(w, b->publish, 1u);
        w_le(w, 0u, 1u);
    }
    w_le(w, ctx->last_id + 1u, 8u);
    w_le(w, ctx->last_arena + 1u, 8u);
    w_le(w, ctx->last_pool + 1u, 8u);
    w_le(w, ctx->entries, 8u);
    w_str(w, LIT(FUEL_MODEL));
    w_le(w, ctx->head.faulted != 0u ? 1u : 0u, 1u);
    if (ctx->head.faulted != 0u) {
        w->len += cint_fault_encode(&ctx->fault, w->buf != NULL ? w->buf + w->len : NULL,
                                    w->buf != NULL ? w->cap - w->len : 0u);
    }
    w_le(w, (uint64_t)ctx->effects, 8u);
}

/* The context's canonical state bytes into s. CINT_REFUSED for a context with no bound
 * state, a program without a revision identity, or a fault record that does not encode. */
static cint_status state_bytes(cint_ctx *ctx, snap *s)
{
    wr w = wr_of(NULL, 0u);
    cint_status st;
    size_t k = 0u;
    memset(s, 0, sizeof *s);
    /* A counter at UINT64_MAX has no next identifier to write; 2^64 registrations do not occur.
     * Arenas and pools (rt/cint_mem.c) have no blocks in this layout yet (RT-OQ-42). */
    if (ctx->states == NULL || ctx->mem != NULL || ctx->program->revision == NULL || !covered(ctx, ctx->states) ||
        ctx->last_id == UINT64_MAX || ctx->last_arena == UINT64_MAX || ctx->last_pool == UINT64_MAX ||
        (ctx->head.faulted != 0u && cint_fault_encode(&ctx->fault, NULL, 0u) == 0u)) {
        return CINT_REFUSED;
    }
    if ((st = globals(ctx, ctx->program, ctx->states, &s->g)) != CINT_OK) {
        return st;
    }
    for (uint32_t i = 0u; i < ctx->buffer_used; i++) {
        if (ctx->buffers[i].live != 0u) {
            s->live_count++;
        }
    }
    s->live = (const buffer_slot **)mem_alloc(ctx, s->live_count * sizeof *s->live, _Alignof(const buffer_slot *));
    if (s->live_count > 0u && s->live == NULL) {
        return CINT_RESOURCE;
    }
    for (uint32_t i = 0u; i < ctx->buffer_used; i++) {
        if (ctx->buffers[i].live != 0u) {
            s->live[k++] = &ctx->buffers[i];
        }
    }
    if (s->live_count > 1u) {
        qsort((void *)s->live, s->live_count, sizeof *s->live, slot_cmp);
    }
    put_state(ctx, s, &w);
    if ((s->bytes = (uint8_t *)mem_alloc(ctx, w.len, 8u)) == NULL) {
        return CINT_RESOURCE;
    }
    s->len = w.len;
    w = wr_of(s->bytes, s->len);
    put_state(ctx, s, &w);
    return CINT_OK;
}

cint_status cint_ctx_checkpoint(cint_ctx *ctx, struct cint_sink *sink)
{
    uint8_t head[4u + sizeof CKPT_DOMAIN - 1u + 4u + CONTRACT_MAX + 3u + 8u];
    wr w = wr_of(head, sizeof head);
    snap s;
    cint_status st;
    if (ctx == NULL || sink == NULL || sink->write == NULL) {
        return CINT_REFUSED;
    }
    if (!cint_rt_busy_take(ctx)) {
        return CINT_BUSY;
    }
    st = state_bytes(ctx, &s);
    if (st == CINT_OK) {
        w_str(&w, LIT(CKPT_DOMAIN));
        w_str(&w, LIT(CINT_RT_CONTRACT));
        w_le(&w, 0u, 1u);  /* the payload digest, */
        w_le(&w, 0u, 1u);  /* the build record digest */
        w_le(&w, 0u, 1u);  /* and the build identity: absent (SEC-REC-11) */
        w_le(&w, s.len, 8u);
        st = sink->write(sink->user, head, w.len);
        if (st == CINT_OK) {
            st = sink->write(sink->user, s.bytes, s.len);
        }
    }
    snap_free(ctx, &s);
    cint_rt_busy_give(ctx);
    return st;
}

cint_status cint_ctx_state_hash(cint_ctx *ctx, uint8_t out[32])
{
    snap s;
    cint_status st;
    if (ctx == NULL || out == NULL) {
        return CINT_REFUSED;
    }
    if (!cint_rt_busy_take(ctx)) {
        return CINT_BUSY;
    }
    st = state_bytes(ctx, &s);
    if (st == CINT_OK) {
        cint_sha256(s.bytes, s.len, out);
    }
    snap_free(ctx, &s);
    cint_rt_busy_give(ctx);
    return st;
}

/* ------------------------------------------------------------------------- */
/* Restore (H-19a, M-11, M-11a).                                              */

/* The site of program p at a position: the lowest site of the module with that path whose
 * line and column match. Every such site encodes to the same position (SPEC-01 11.2). */
static bool site_back(const cint_program *p, const cint_position *pos, cint_site *out)
{
    for (uint32_t m = 0u; m < p->module_count; m++) {
        const cint_module *mod = p->modules[m];
        if (mod->path_len != pos->path_len || memcmp(mod->path, pos->path, pos->path_len) != 0) {
            continue;
        }
        for (uint32_t k = 1u; mod->sites != NULL && k < mod->site_count; k++) {
            if (mod->sites[k].line == pos->line && mod->sites[k].column == pos->column) {
                out->module = m;
                out->index = k;
                return true;
            }
        }
    }
    return false;
}

/* The fault record of a checkpoint as the context holds it, with its positions as sites of
 * the context's program. CINT_REFUSED unless the record encodes back to the same bytes. */
static cint_status fault_back(cint_ctx *ctx, const uint8_t *bytes, size_t len, cint_fault_record *rec)
{
    rd r = rd_of(bytes, len);
    fault_view f;
    uint8_t *again;
    bool same;
    rd_fault(&r, &f);
    memset(rec, 0, sizeof *rec);
    if (r.bad || f.has_map || !site_back(ctx->program, &f.position, &rec->position)) {
        return CINT_REFUSED;  /* cint-rt-3 has no carrier for a source-map digest (RT-OQ-28) */
    }
    rec->program = ctx->program;
    rec->code = f.code;
    rec->operation_len = f.operation_len;
    memcpy(rec->operation, f.operation, f.operation_len);
    rec->operand_count = f.operand_count;
    memcpy(rec->operands, f.operands, f.operand_count * sizeof f.operands[0]);
    rec->has_exact = (uint8_t)f.has_exact;
    rec->exact = f.exact;
    rec->has_limit = (uint8_t)f.has_limit;
    rec->limit = f.limit;
    rec->has_revision = (uint8_t)f.has_revision;
    if (f.has_revision) {
        memcpy(rec->revision, f.revision, 32u);
    }
    rec->has_address = (uint8_t)f.has_address;
    if (f.has_address) {
        rec->address.dispatch = f.dispatch;
        rec->address.work_item = f.work_item;
        rec->address.step = f.step;
        rec->address.phase = f.phase;
        rec->address.has_work_item = (uint8_t)f.has_work_item;
        rec->address.name_len = (uint16_t)f.kernel_len;
        memcpy(rec->address.name, f.kernel, f.kernel_len);
    }
    rec->stack_count = f.stack_count;
    r.at = f.stack_at;
    for (uint32_t i = 0u; i < f.stack_count; i++) {
        cint_position pos;
        rd_pos(&r, &pos);
        if (r.bad || !site_back(ctx->program, &pos, &rec->stack[i])) {
            return CINT_REFUSED;
        }
    }
    if (cint_fault_encode(rec, NULL, 0u) != len) {
        return CINT_REFUSED;
    }
    if ((again = (uint8_t *)mem_alloc(ctx, len, 1u)) == NULL) {
        return CINT_RESOURCE;
    }
    same = cint_fault_encode(rec, again, len) == len && memcmp(again, bytes, len) == 0;
    mem_free(ctx, again, len, 1u);
    return same ? CINT_OK : CINT_REFUSED;
}

/* Every check of a restore that needs the context. */
static cint_status restore_check(cint_ctx *ctx, const uint8_t *bytes, size_t length, cint_checkpoint_info *c,
                                 glist *g, cint_fault_record **rec)
{
    cint_checkpoint_global cg;
    cint_checkpoint_buffer cb;
    size_t cur = 0u;
    cint_status st;
    for (uint32_t i = 0u; i < ctx->buffer_cap; i++) {
        if (ctx->leases[i].lease != 0u) {
            return CINT_BUSY;  /* a leased address stays valid until cint_lease_end (A-9) */
        }
    }
    if (cint_checkpoint_decode(bytes, length, c) != CINT_OK || ctx->states == NULL || ctx->mem != NULL ||
        ctx->program->revision == NULL || c->contract.len != sizeof CINT_RT_CONTRACT - 1u ||
        memcmp(c->contract.bytes, CINT_RT_CONTRACT, c->contract.len) != 0 ||
        memcmp(c->revision, ctx->program->revision, 32u) != 0 || !covered(ctx, ctx->states)) {
        return CINT_REFUSED;
    }
    if ((st = globals(ctx, ctx->program, ctx->states, g)) != CINT_OK) {
        return st;
    }
    if (c->global_count != g->count) {
        return CINT_REFUSED;
    }
    for (size_t i = 0u; i < g->count; i++) {
        if (cint_checkpoint_global_next(c, &cur, &cg) != CINT_OK || cg.name.len != g->g[i].len ||
            memcmp(cg.name.bytes, g->g[i].name, cg.name.len) != 0 || (uint32_t)cg.value.bytes[0] != g->g[i].tag) {
            return CINT_REFUSED;
        }
    }
    cur = 0u;
    while (cint_checkpoint_buffer_next(c, &cur, &cb) == CINT_OK) {
        if (cb.type.code == CINT_TAG_RECORD && (ctx->module == NULL || cb.type.record_id >= ctx->module->record_count)) {
            return CINT_REFUSED;
        }
    }
    if (c->fault == NULL) {
        return CINT_OK;
    }
    if ((*rec = (cint_fault_record *)mem_alloc(ctx, sizeof **rec, _Alignof(cint_fault_record))) == NULL) {
        return CINT_RESOURCE;
    }
    return fault_back(ctx, c->fault, c->fault_len, *rec);
}

cint_status cint_ctx_restore(cint_ctx *ctx, const uint8_t *bytes, size_t length)
{
    cint_checkpoint_info c;
    cint_checkpoint_global cg;
    cint_fault_record *rec = NULL;
    glist g;
    size_t cur = 0u;
    cint_status st;
    if (ctx == NULL || bytes == NULL) {
        return CINT_REFUSED;
    }
    if (!cint_rt_busy_take(ctx)) {
        return CINT_BUSY;
    }
    memset(&g, 0, sizeof g);
    st = restore_check(ctx, bytes, length, &c, &g, &rec);
    if (st == CINT_OK) {
        st = blocks_make(ctx, &ctx->state, ctx->program->module_count, ctx->states);
    }
    if (st == CINT_OK) {
        for (size_t i = 0u; i < g.count; i++) {
            (void)cint_checkpoint_global_next(&c, &cur, &cg);
            memcpy((uint8_t *)ctx->state[g.g[i].state->module] + g.g[i].offset, cg.value.bytes + 1, g.g[i].width);
        }
        for (uint32_t i = 0u; i < ctx->buffer_used; i++) {
            buffer_slot *b = &ctx->buffers[i];
            if (b->live != 0u) {  /* none is context-owned (M-11a) */
                if (b->storage == STORAGE_CREATED) {
                    mem_free(ctx, b->base, (size_t)b->bytes, 8u);
                    b->base = NULL;
                }
                b->live = 0u;
                b->generation++;
            }
        }
        ctx->last_id = c.next_buffer - 1u > ctx->last_id ? c.next_buffer - 1u : ctx->last_id;
        ctx->last_arena = c.next_arena - 1u > ctx->last_arena ? c.next_arena - 1u : ctx->last_arena;
        ctx->last_pool = c.next_pool - 1u > ctx->last_pool ? c.next_pool - 1u : ctx->last_pool;
        ctx->entries = c.entries;
        ctx->effects = c.effects;
        if (rec != NULL) {
            memcpy(&ctx->fault, rec, sizeof *rec);
        } else {
            memset(&ctx->fault, 0, sizeof ctx->fault);
        }
        ctx->head.faulted = rec != NULL ? 1u : 0u;
        ctx->desc_count = 0u;
    }
    glist_free(ctx, &g);
    mem_free(ctx, rec, sizeof *rec, _Alignof(cint_fault_record));
    cint_rt_busy_give(ctx);
    return st;
}

/* ------------------------------------------------------------------------- */
/* The interrupt and the change of revision.                                  */

void cint_ctx_interrupt(cint_ctx *ctx)
{
    if (ctx != NULL) {
        ctx->interrupt = 1;
    }
}

void cint_rt_interrupt_poll(cint_ctx *ctx)
{
    if (ctx->interrupt != 0) {
        ctx->interrupt = 0;
        if (ctx->head.fuel_limit < 0 || ctx->head.fuel_limit > ctx->head.fuel_used) {
            ctx->head.fuel_limit = ctx->head.fuel_used;
        }
    }
}

/* What cint_ctx_create requires of a program. */
static bool program_ok(const cint_program *p)
{
    if (p->module_count > 0u && p->modules == NULL) {
        return false;
    }
    for (uint32_t i = 0u; i < p->module_count; i++) {
        const cint_module *m = p->modules[i];
        if (m == NULL || m->path == NULL || m->path_len > CINT_FAULT_MAX_PATH || (m->site_count > 0u && m->sites == NULL)) {
            return false;
        }
    }
    return true;
}

cint_status cint_ctx_change_revision(cint_ctx *ctx, const cint_program *program, const cint_state_table *t)
{
    glist from, to;
    void **state = NULL;
    cint_status st;
    if (ctx == NULL || program == NULL) {
        return CINT_REFUSED;
    }
    if (!cint_rt_busy_take(ctx)) {
        return CINT_BUSY;
    }
    t = t != NULL ? t : &NO_STATE;
    memset(&from, 0, sizeof from);
    memset(&to, 0, sizeof to);
    st = ctx->head.faulted != 0u || ctx->module != NULL || ctx->states == NULL || ctx->mem != NULL ||
                 program->revision == NULL || !program_ok(program) || !covered(ctx, ctx->states)
             ? CINT_REFUSED
             : CINT_OK;
    if (st == CINT_OK) {
        st = globals(ctx, ctx->program, ctx->states, &from);
    }
    if (st == CINT_OK) {
        st = globals(ctx, program, t, &to);
    }
    if (st == CINT_OK && (st = blocks_make(ctx, &state, program->module_count, t)) != CINT_OK) {
        blocks_free(ctx, state, program->module_count);
    }
    if (st == CINT_OK) {
        /* Both lists are sorted by name: a global of the same name and type keeps its value. */
        size_t i = 0u, j = 0u;
        while (i < from.count && j < to.count) {
            const gref *x = &from.g[i], *y = &to.g[j];
            int c = name_cmp(x->name, x->len, y->name, y->len);
            if (c == 0 && x->tag == y->tag) {
                memcpy((uint8_t *)state[y->state->module] + y->offset, block_of(ctx, x->state) + x->offset, y->width);
            }
            if (c <= 0) {
                i++;
            }
            if (c >= 0) {
                j++;
            }
        }
        blocks_free(ctx, ctx->state, ctx->program->module_count);
        ctx->state = state;
        ctx->program = program;
        ctx->states = t;
        ctx->error_set = ctx->saved_error_set = NULL;  /* the last entry's, of the old revision */
        ctx->error_tag = ctx->saved_error_tag = 0u;
    }
    glist_free(ctx, &from);
    glist_free(ctx, &to);
    cint_rt_busy_give(ctx);
    return st;
}

/* ------------------------------------------------------------------------- */
/* The text of `cint-interp show` (SPEC-06 6.1).                              */

typedef struct tx {
    char *out;
    size_t cap, len;
} tx;

static void t_put(tx *t, const void *s, size_t n)
{
    if (t->out != NULL && t->len < t->cap && n > 0u) {
        memcpy(t->out + t->len, s, n < t->cap - t->len ? n : t->cap - t->len);
    }
    t->len += n;
}

static void t_str(tx *t, const char *s)
{
    t_put(t, s, strlen(s));
}

static void t_u64(tx *t, uint64_t v)
{
    char b[20];
    size_t n = 0u;
    do {
        b[sizeof b - 1u - n++] = (char)('0' + (int)(v % 10u));
        v /= 10u;
    } while (v != 0u);
    t_put(t, b + sizeof b - n, n);
}

static void t_i64(tx *t, int64_t v)
{
    if (v < 0) {
        t_str(t, "-");
    }
    t_u64(t, v < 0 ? 0u - (uint64_t)v : (uint64_t)v);
}

static void t_hex(tx *t, const uint8_t *p, size_t n)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0u; i < n; i++) {
        char c[2];
        c[0] = hex[p[i] >> 4];
        c[1] = hex[p[i] & 15u];
        t_put(t, c, 2u);
    }
}

static void t_digest(tx *t, const char *label, const uint8_t *p)
{
    t_str(t, label);
    if (p != NULL) {
        t_hex(t, p, 32u);
    } else {
        t_str(t, "absent");
    }
    t_str(t, "\n");
}

static void t_value(tx *t, const char *label, const cint_tvalue *v)
{
    char b[700];  /* the longest is a Z of 257 bytes: 621 digits */
    t_str(t, label);
    t_put(t, b, cint_tvalue_render(v, b, sizeof b));
    t_str(t, "\n");
}

static void t_pos(tx *t, const char *label, const cint_position *p)
{
    t_str(t, label);
    t_put(t, p->path, p->path_len);
    t_str(t, ":");
    t_u64(t, p->line);
    t_str(t, ":");
    t_u64(t, p->column);
    t_str(t, "\n");
}

static void t_fault(tx *t, const cint_checkpoint_info *c)
{
    static const char *const codes[CINT_E_ASSERT + 1u] = {
        "", "E_OVERFLOW", "E_DIV_ZERO", "E_BOUNDS", "E_SHAPE", "E_SHIFT", "E_NARROW", "E_ALIAS",
        "E_STALE_HANDLE", "E_FUEL", "E_UNSUPPORTED", "E_DOMAIN", "E_DEPTH", "E_ASSERT"};
    rd r = rd_of(c->fault, c->fault_len);
    fault_view f;
    if (c->fault == NULL) {
        t_str(t, "fault absent\n");
        return;
    }
    rd_fault(&r, &f);
    if (r.bad) {
        return;  /* not from cint_checkpoint_decode */
    }
    t_str(t, "fault ");
    t_str(t, codes[f.code]);
    t_str(t, " ");
    t_put(t, f.operation, f.operation_len);
    t_str(t, "\n");
    for (uint32_t i = 0u; i < f.operand_count; i++) {
        t_value(t, "fault-operand ", &f.operands[i]);
    }
    if (f.has_exact) {
        t_value(t, "fault-exact ", &f.exact);
    }
    if (f.has_limit) {
        t_value(t, "fault-limit ", &f.limit);
    }
    t_pos(t, "fault-position ", &f.position);
    t_digest(t, "fault-revision ", f.revision);
    t_digest(t, "fault-source-map ", f.map);
    if (f.has_address) {
        t_str(t, "fault-address ");
        t_put(t, f.kernel, f.kernel_len);
        t_str(t, " dispatch ");
        t_i64(t, f.dispatch);
        t_str(t, " phase ");
        t_u64(t, f.phase);
        if (f.has_work_item) {
            t_str(t, " work-item ");
            t_i64(t, f.work_item);
            t_str(t, " step ");
            t_i64(t, f.step);
        }
        t_str(t, "\n");
    }
    r.at = f.stack_at;
    for (uint32_t i = 0u; i < f.stack_count; i++) {
        cint_position p;
        rd_pos(&r, &p);
        t_pos(t, "fault-call ", &p);
    }
}

size_t cint_checkpoint_render(const cint_checkpoint_info *c, char *out, size_t cap)
{
    tx t;
    cint_checkpoint_global g;
    cint_checkpoint_buffer b;
    uint8_t hash[32];
    size_t cur = 0u;
    t.out = out;
    t.cap = out != NULL ? cap : 0u;
    t.len = 0u;
    if (c == NULL || c->state == NULL || c->contract.bytes == NULL) {
        return 0u;
    }
    t_str(&t, "checkpoint cint-core-1/checkpoint/v1\ncontract ");
    t_put(&t, c->contract.bytes, c->contract.len);
    t_str(&t, "\n");
    t_digest(&t, "payload-digest ", c->payload_digest);
    t_digest(&t, "build-record-digest ", c->build_record_digest);
    t_digest(&t, "build-identity ", c->build_identity);
    t_str(&t, "state cint-core-1/state/v1\nrevision ");
    t_hex(&t, c->revision, 32u);
    t_str(&t, "\nprofile cint-core-1\nfuel-model fuel-v1\n");
    while (cint_checkpoint_global_next(c, &cur, &g) == CINT_OK) {
        t_str(&t, "global ");
        t_put(&t, g.name.bytes, g.name.len);
        t_value(&t, " ", &g.value);
    }
    t_str(&t, "arena-block 0\npool-block 0\n");
    cur = 0u;
    while (cint_checkpoint_buffer_next(c, &cur, &b) == CINT_OK) {
        t_str(&t, "buffer ");
        t_u64(&t, b.id);
        t_str(&t, " generation ");
        t_u64(&t, b.generation);
        t_str(&t, " type ");
        if (b.type.code == CINT_TAG_RECORD) {
            t_str(&t, "record ");
            t_u64(&t, b.type.record_id);
        } else {
            t_str(&t, b.type.code == 0u ? "bytes" : b.type.code == CINT_TAG_BOOL ? "Bool" : cint_rt_type_name(b.type.code));
        }
        t_str(&t, " extent ");
        t_i64(&t, b.extent);
        t_str(&t, b.owner == CINT_OWNER_BRIDGE ? " owner bridge" : " owner borrowed");
        t_str(&t, b.ceiling == CINT_VIEW_WRITE ? " ceiling write" : " ceiling read");
        t_str(&t, b.publish == CINT_PUBLISH_COPY ? " publish copy state live\n" : " publish none state live\n");
    }
    t_str(&t, "counters next-buffer ");
    t_u64(&t, c->next_buffer);
    t_str(&t, " next-arena ");
    t_u64(&t, c->next_arena);
    t_str(&t, " next-pool ");
    t_u64(&t, c->next_pool);
    t_str(&t, " entries ");
    t_u64(&t, c->entries);
    t_str(&t, "\n");
    t_fault(&t, c);
    t_str(&t, "effects ");
    t_i64(&t, c->effects);
    cint_sha256(c->state, c->state_len, hash);
    t_str(&t, "\nstate-hash ");
    t_hex(&t, hash, 32u);
    t_str(&t, "\n");
    return t.len;
}

int cint_checkpoint_show(const char *path)
{
    FILE *f = path != NULL ? fopen(path, "rb") : NULL;
    long size = f != NULL && fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1;
    uint8_t *data = size >= 0 && fseek(f, 0, SEEK_SET) == 0 ? (uint8_t *)malloc((size_t)size + 1u) : NULL;
    char *text = NULL;
    cint_checkpoint_info info;
    int st = data != NULL && fread(data, 1u, (size_t)size, f) == (size_t)size ? CINT_EXIT_OK : CINT_EXIT_ENVIRONMENT;
    if (st == CINT_EXIT_OK && cint_checkpoint_decode(data, (size_t)size, &info) != CINT_OK) {
        st = CINT_EXIT_CHECK;
    } else if (st == CINT_EXIT_OK) {
        size_t n = cint_checkpoint_render(&info, NULL, 0u);
#if defined(_WIN32)
        (void)_setmode(_fileno(stdout), _O_BINARY);  /* no CR */
#endif
        st = (text = (char *)malloc(n + 1u)) != NULL && cint_checkpoint_render(&info, text, n) == n &&
                     fwrite(text, 1u, n, stdout) == n && fflush(stdout) == 0
                 ? CINT_EXIT_OK
                 : CINT_EXIT_ENVIRONMENT;
    }
    if (f != NULL) {
        fclose(f);
    }
    free(data);
    free(text);
    return st;
}
