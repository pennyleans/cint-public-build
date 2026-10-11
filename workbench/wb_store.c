/* wb_store.c: a session's object store and its session checkpoint records (SPEC-06 5.4 and
 * 5.6; box 14 default BX14-19; box 14 unit 2b).
 *
 * The store holds content-addressed blobs (sources, declarations, entries, and box 13's
 * CINT-CKPT-1 of the context), each named by the SHA-256 of its bytes. A session checkpoint
 * is a record around one CINT-CKPT-1, in the order of the table of SPEC-06 5.4, then what
 * the session needs to go on from it, which is in neither identity. Integers are
 * little-endian; a string is a U32 length and its bytes; an object is its 32-byte name.
 *
 *   record     "cint-core-1/session-checkpoint/v1"; the revision identity of the program the
 *              context is in (SPEC-01 IM-157); a U32 count and each module of the program, its
 *              path and its source object (the revision set); a presence byte and the
 *              CINT-CKPT-1 object (absent before the session's first entry); the effect
 *              position, an I64; the grants, an empty string until roadmap box 27 (SPEC-06
 *              13.1); the fuel allowance and the depth limit, I64 each, and the backend; a
 *              presence byte and the parent's node identity; the entry ordinal, a U64; then
 *              the session's own part: the session module's revision number (U32), its
 *              declarations (U32 count; name, kind U8, entry U32, revision U32, line U32, line
 *              count U32, text object), the loaded modules (U32 count; path, revision U32), the
 *              entry unit (line, line count, first line, first column, U32 each, and its
 *              path), and the checkpoint's name. Nothing follows.
 *
 * The decoder takes its input as untrusted (SPEC-06 5.4 and 13.2): it checks every length,
 * count, name, and path, and refuses anything after the record. */
#include <stdlib.h>
#include <string.h>

#include "wb.h"

static const char DOMAIN[] = "cint-core-1/session-checkpoint/v1";
#define DOMAIN_LEN (sizeof DOMAIN - 1u)

/* ------------------------------------------------------------------------- */
/* The object store.                                                          */

static uint32_t slot_of(const uint8_t id[32], uint32_t nslot)
{
    uint32_t h = (uint32_t)id[0] | (uint32_t)id[1] << 8 | (uint32_t)id[2] << 16 | (uint32_t)id[3] << 24;
    return h & (nslot - 1u);
}

static int rehash(wb_store *st, uint32_t nslot)
{
    uint32_t *slot = calloc(nslot, sizeof *slot);
    if (slot == NULL) return -1;
    for (uint32_t i = 0; i < st->n; i++) {
        uint32_t k = slot_of(st->obj[i].id, nslot);
        while (slot[k] != 0) k = (k + 1u) & (nslot - 1u);
        slot[k] = i + 1u;
    }
    free(st->slot);
    st->slot = slot;
    st->nslot = nslot;
    return 0;
}

const wb_obj *wb_store_get(const wb_store *st, const uint8_t id[32])
{
    for (uint32_t k = st->nslot != 0 ? slot_of(id, st->nslot) : 0; st->nslot != 0 && st->slot[k] != 0;
         k = (k + 1u) & (st->nslot - 1u)) {
        const wb_obj *o = &st->obj[st->slot[k] - 1u];
        if (memcmp(o->id, id, 32u) == 0) return o;
    }
    return NULL;
}

int wb_store_put(wb_store *st, const void *p, size_t n, uint8_t id[32])
{
    uint8_t mine[32];
    cint_sha256(p, n, mine);
    if (id != NULL) memcpy(id, mine, 32u);
    if (wb_store_get(st, mine) != NULL) return 0;
    if ((st->n + 1u) * 2u > st->nslot && rehash(st, st->nslot != 0 ? st->nslot * 2u : 64u) != 0) return -1;
    if (st->n == st->cap) {
        wb_obj *grown = realloc(st->obj, (st->cap = st->cap * 2u + 64u) * sizeof *grown);
        if (grown == NULL) {
            st->cap = st->n;
            return -1;
        }
        st->obj = grown;
    }
    wb_obj *o = &st->obj[st->n];
    o->p = malloc(n != 0 ? n : 1u);
    if (o->p == NULL) return -1;
    if (n != 0) memcpy(o->p, p, n);
    o->n = n;
    memcpy(o->id, mine, 32u);
    uint32_t k = slot_of(mine, st->nslot);
    while (st->slot[k] != 0) k = (k + 1u) & (st->nslot - 1u);
    st->slot[k] = ++st->n;
    return 0;
}

void wb_store_free(wb_store *st)
{
    for (uint32_t i = 0; i < st->n; i++) free(st->obj[i].p);
    free(st->obj);
    free(st->slot);
    memset(st, 0, sizeof *st);
}

/* ------------------------------------------------------------------------- */
/* Framing.                                                                   */

static void put_le(wb_buf *b, uint64_t v, unsigned n)
{
    uint8_t x[8];
    for (unsigned i = 0; i < n; i++) x[i] = (uint8_t)(v >> (8u * i));
    wb_put(b, x, n);
}

static void put_str(wb_buf *b, const char *s, size_t n)
{
    put_le(b, n, 4u);
    wb_put(b, s, n);
}

typedef struct rd {
    const uint8_t *p;
    size_t n, at;
    int bad;
} rd;

static uint64_t get_le(rd *r, unsigned n)
{
    uint64_t v = 0;
    if (r->bad || r->n - r->at < n) {
        r->bad = 1;
        return 0;
    }
    for (unsigned i = 0; i < n; i++) v |= (uint64_t)r->p[r->at + i] << (8u * i);
    r->at += n;
    return v;
}

static void get_bytes(rd *r, uint8_t *out, size_t n)
{
    if (r->bad || r->n - r->at < n) {
        r->bad = 1;
        memset(out, 0, n);
        return;
    }
    memcpy(out, r->p + r->at, n);
    r->at += n;
}

/* A string of at most cap - 1 bytes into out, NUL-terminated; a NUL inside refuses it. */
static void get_str(rd *r, char *out, size_t cap)
{
    uint64_t n = get_le(r, 4u);
    if (r->bad || n >= cap || r->n - r->at < n || memchr(r->p + r->at, '\0', (size_t)n) != NULL) {
        r->bad = 1;
        out[0] = '\0';
        return;
    }
    memcpy(out, r->p + r->at, (size_t)n);
    out[n] = '\0';
    r->at += (size_t)n;
}

int wb_name_ok(const char *s, size_t max)
{
    size_t n = strlen(s);
    if (n == 0 || n > max) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!(c == '_' || c == '-' || c == '.' || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z'))) {
            return 0;
        }
    }
    return 1;
}

int wb_path_ok(const char *s)
{
    size_t n = strlen(s);
    if (n < 4u || n > WB_PATH_MAX || strcmp(s + n - 3u, ".ci") != 0 || s[0] == '/' || strstr(s, "..") != NULL ||
        strstr(s, "//") != NULL) {
        return 0;
    }
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!(c == '_' || c == '-' || c == '.' || c == '/' || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z'))) {
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Records.                                                                   */

void wb_record_free(wb_record *r)
{
    free(r->mod);
    free(r->decl);
    free(r->loaded);
    memset(r, 0, sizeof *r);
}

int wb_record_encode(wb_buf *b, const wb_record *r)
{
    put_str(b, DOMAIN, DOMAIN_LEN);
    wb_put(b, r->revision, 32u);
    put_le(b, r->nmod, 4u);
    for (uint32_t i = 0; i < r->nmod; i++) {
        put_str(b, r->mod[i].path, strlen(r->mod[i].path));
        wb_put(b, r->mod[i].text, 32u);
    }
    put_le(b, (uint64_t)r->has_state, 1u);
    if (r->has_state) wb_put(b, r->state, 32u);
    put_le(b, (uint64_t)r->effects, 8u);
    put_str(b, "", 0u);   /* the grants */
    put_le(b, (uint64_t)r->fuel, 8u);
    put_le(b, (uint64_t)r->depth, 8u);
    put_str(b, r->backend, strlen(r->backend));
    put_le(b, (uint64_t)r->has_parent, 1u);
    if (r->has_parent) wb_put(b, r->parent, 32u);
    put_le(b, r->ordinal, 8u);
    put_le(b, r->rev, 4u);
    put_le(b, r->ndecl, 4u);
    for (uint32_t i = 0; i < r->ndecl; i++) {
        const wb_rdecl *d = &r->decl[i];
        put_str(b, d->name, strlen(d->name));
        put_le(b, (uint64_t)d->kind, 1u);
        put_le(b, d->entry, 4u);
        put_le(b, d->rev, 4u);
        put_le(b, d->line, 4u);
        put_le(b, d->nlines, 4u);
        wb_put(b, d->text, 32u);
    }
    put_le(b, r->nloaded, 4u);
    for (uint32_t i = 0; i < r->nloaded; i++) {
        put_str(b, r->loaded[i].path, strlen(r->loaded[i].path));
        put_le(b, r->loaded[i].rev, 4u);
    }
    put_le(b, r->unit_line, 4u);
    put_le(b, r->unit_lines, 4u);
    put_le(b, r->first_line, 4u);
    put_le(b, r->first_col, 4u);
    put_str(b, r->unit_path, strlen(r->unit_path));
    put_str(b, r->name, strlen(r->name));
    return b->failed ? -1 : 0;
}

/* An entry unit's path: empty, "entry N", "inspect", or a script's path. */
static int unit_path_ok(const char *p)
{
    if (*p == '\0' || strcmp(p, "inspect") == 0 || wb_path_ok(p)) return 1;   /* a path: a script's (BX14-04) */
    if (strncmp(p, "entry ", 6u) != 0 || p[6] < '1' || p[6] > '9') return 0;
    for (p += 6; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') return 0;
    }
    return 1;
}

int wb_record_decode(const uint8_t *p, size_t n, wb_record *out)
{
    rd r = {p, n, 0, 0};
    char domain[sizeof DOMAIN];
    wb_record x;
    memset(&x, 0, sizeof x);
    memset(out, 0, sizeof *out);
    get_str(&r, domain, sizeof domain);
    if (r.bad || strcmp(domain, DOMAIN) != 0) return -1;
    get_bytes(&r, x.revision, 32u);
    x.nmod = (uint32_t)get_le(&r, 4u);
    if (r.bad || x.nmod == 0 || x.nmod > WB_MODULES_MAX || (x.mod = calloc(x.nmod, sizeof *x.mod)) == NULL) return -1;
    for (uint32_t i = 0; i < x.nmod && !r.bad; i++) {
        get_str(&r, x.mod[i].path, sizeof x.mod[i].path);
        get_bytes(&r, x.mod[i].text, 32u);
        r.bad |= !wb_path_ok(x.mod[i].path);
        for (uint32_t k = 0; k < i && !r.bad; k++) r.bad |= strcmp(x.mod[k].path, x.mod[i].path) == 0;
    }
    x.has_state = (int)get_le(&r, 1u);
    r.bad |= x.has_state > 1;
    if (x.has_state) get_bytes(&r, x.state, 32u);
    x.effects = (int64_t)get_le(&r, 8u);
    char grants[2];
    get_str(&r, grants, sizeof grants);
    r.bad |= grants[0] != '\0';   /* none before roadmap box 27 (SPEC-06 13.1) */
    x.fuel = (int64_t)get_le(&r, 8u);
    x.depth = (int64_t)get_le(&r, 8u);
    get_str(&r, x.backend, sizeof x.backend);
    r.bad |= x.fuel < CINT_FUEL_UNBOUNDED || x.depth < 1 ||
             (strcmp(x.backend, "cpu-sir-interp") != 0 && strcmp(x.backend, "cpu-c17") != 0);
    x.has_parent = (int)get_le(&r, 1u);
    r.bad |= x.has_parent > 1;
    if (x.has_parent) get_bytes(&r, x.parent, 32u);
    x.ordinal = get_le(&r, 8u);
    x.rev = (uint32_t)get_le(&r, 4u);
    x.ndecl = (uint32_t)get_le(&r, 4u);
    r.bad |= x.ordinal < 1u || x.ordinal > 65537u || x.ndecl > 1024u;
    if (!r.bad && x.ndecl != 0 && (x.decl = calloc(x.ndecl, sizeof *x.decl)) == NULL) r.bad = 1;
    for (uint32_t i = 0; i < x.ndecl && !r.bad; i++) {
        wb_rdecl *d = &x.decl[i];
        get_str(&r, d->name, sizeof d->name);
        d->kind = (int)get_le(&r, 1u);
        d->entry = (uint32_t)get_le(&r, 4u);
        d->rev = (uint32_t)get_le(&r, 4u);
        d->line = (uint32_t)get_le(&r, 4u);
        d->nlines = (uint32_t)get_le(&r, 4u);
        get_bytes(&r, d->text, 32u);
        r.bad |= !(wb_name_ok(d->name, 255u) || (d->kind == 7 && wb_path_ok(d->name))) || d->kind < 1 || d->kind > 9 ||
                 d->entry < 1u || d->entry >= x.ordinal || d->rev < 1u || d->rev > x.rev || d->nlines < 1u;
        for (uint32_t k = 0; k < i && !r.bad; k++) r.bad |= strcmp(x.decl[k].name, d->name) == 0;
    }
    x.nloaded = (uint32_t)get_le(&r, 4u);
    r.bad |= x.nloaded > 64u;
    if (!r.bad && x.nloaded != 0 && (x.loaded = calloc(x.nloaded, sizeof *x.loaded)) == NULL) r.bad = 1;
    for (uint32_t i = 0; i < x.nloaded && !r.bad; i++) {
        int found = 0;
        get_str(&r, x.loaded[i].path, sizeof x.loaded[i].path);
        x.loaded[i].rev = (uint32_t)get_le(&r, 4u);
        for (uint32_t k = 0; k < x.nmod; k++) found |= strcmp(x.mod[k].path, x.loaded[i].path) == 0;
        r.bad |= !found || x.loaded[i].rev < 1u;
    }
    x.unit_line = (uint32_t)get_le(&r, 4u);
    x.unit_lines = (uint32_t)get_le(&r, 4u);
    x.first_line = (uint32_t)get_le(&r, 4u);
    x.first_col = (uint32_t)get_le(&r, 4u);
    get_str(&r, x.unit_path, sizeof x.unit_path);
    get_str(&r, x.name, sizeof x.name);
    r.bad |= !unit_path_ok(x.unit_path) || (x.name[0] != '\0' && !wb_name_ok(x.name, WB_NAME_MAX)) ||
             strcmp(x.mod[x.nmod - 1u].path, "session.ci") != 0 || r.at != r.n;
    if (r.bad) {
        wb_record_free(&x);
        return -1;
    }
    *out = x;
    return 0;
}

/* The state identity covers the format, the revision set, the canonical state, the effect
 * position, and the grants; the node identity adds the settings, the parent, and the entry
 * ordinal (SPEC-06 5.4). `state` is the canonical state of the CINT-CKPT-1, without its
 * envelope; NULL with 0 before the first entry. */
int wb_record_ids(const wb_record *r, const uint8_t *state, size_t state_len, uint8_t sid[32], uint8_t nid[32])
{
    wb_buf b = {0};
    put_str(&b, DOMAIN, DOMAIN_LEN);
    wb_put(&b, r->revision, 32u);
    put_le(&b, r->nmod, 4u);
    for (uint32_t i = 0; i < r->nmod; i++) {
        put_str(&b, r->mod[i].path, strlen(r->mod[i].path));
        wb_put(&b, r->mod[i].text, 32u);
    }
    put_le(&b, state_len, 8u);
    wb_put(&b, state, state_len);
    put_le(&b, (uint64_t)r->effects, 8u);
    put_str(&b, "", 0u);
    if (b.failed) {
        wb_free(&b);
        return -1;
    }
    cint_sha256((const uint8_t *)b.p, b.n, sid);
    b.n = 0;
    put_str(&b, DOMAIN, DOMAIN_LEN);
    wb_put(&b, sid, 32u);
    put_le(&b, (uint64_t)r->fuel, 8u);
    put_le(&b, (uint64_t)r->depth, 8u);
    put_str(&b, r->backend, strlen(r->backend));
    put_le(&b, (uint64_t)r->has_parent, 1u);
    if (r->has_parent) wb_put(&b, r->parent, 32u);
    put_le(&b, r->ordinal, 8u);
    int bad = b.failed;
    if (!bad) cint_sha256((const uint8_t *)b.p, b.n, nid);
    wb_free(&b);
    return bad ? -1 : 0;
}
