/* cint_rec.c: recordings and replay of a context (rt/cint_state.h; roadmap box 13, unit 5;
 * decision 2026-10-08 on OQ-240).
 *
 * The recording CINT-REC-1 that unit 5 writes (SPEC-03 H-14; SPEC-06 10.5; rt/OPEN.md
 * RT-OQ-44), its decoder, and replay (H-16, H-17), in the byte layout below. Checkpoints, the
 * state hash, and the helpers both files call are in rt/cint_state.c (rt/cint_state_internal.h).
 * Integers are little-endian; a string is a U32 length and its bytes.
 *
 *   recording   "cint-core-1/recording/v1"; "cint-core-1"; "fuel-v1"; the runtime contract
 *               version; the revision identity (32 bytes); the source-map digest of the code, a
 *               presence byte, 1 exactly when the code gives one, and 32 bytes when 1; the mode U8
 *               (0 full, 1 digest); the state-hash interval U64 (1, every entry); the density U8
 *               (0 entry, 1 dispatch); the code; the U64 length and bytes of a CINT-CKPT-1 of the
 *               context when recording began (rt/cint_state.c); the events, each a kind byte and
 *               its fields, the end event last; nothing after it
 *   case        a recording with "cint-core-1/case/v1" for its domain and mode 0 (full), whose
 *               code, and the code of each revision event, has each module's source (SPEC-06 9.2;
 *               rt/OPEN.md RT-OQ-45)
 *   source map  SHA-256 over "cint-core-1/source-map/v1" and the U32 count of modules, then for
 *               each module in byte order of path: its path, the U32 count of its sites, and for
 *               each site of its `sites` line (SPEC-09 SIR-17), in the line's order, the ordinal,
 *               the line and the column, U32 each, and the operation; none when a module has no
 *               such line
 *   code        a U32 count of modules (0 when the host gave none) and for each its path, its
 *               SIR text with the sites line (a U64 length and the bytes), and its source (a
 *               presence byte, then a U64 length and the bytes when 1)
 *   1 entry     the entry sequence number U64; the form U8 (0 an exported function, 1 the
 *               program entry); the module path and the function name (both empty for form 1);
 *               a U32 count and the tagged arguments (SPEC-01 11.1); the fuel budget, the depth
 *               limit and the frame-arena capacity, I64 each; a U32 count of input records (0:
 *               no borrowed inputs before box 13 unit 7)
 *   2 effect    the ordinal I64 (the effect-log position); the site, module U32 and index U32;
 *               the name "print"; the status I32 the host returned; the request bytes (a U64
 *               length and the bytes)
 *   3 outcome   the status I32; the fault record (a presence byte, then a U32 length and the
 *               IM-149 bytes); the fuel consumed I64, 0 when the entry did not run; the result
 *               (a presence byte and the tagged value); the error result (a presence byte, then
 *               the set's name and the tag U64); a U32 count of output records (0 before unit
 *               7); the state hash (a presence byte and 32 bytes, present when the entry ran:
 *               CINT_OK or CINT_FAULT); the allowance in force at the end, I64
 *   4 clear     the SHA-256 of the fault record cleared
 *   5 checkpoint  the state hash
 *   6 restore   the state hash of the checkpoint; the next buffer, arena and pool identifiers
 *               after the restore, U64 each; the U64 length and bytes of the checkpoint
 *   7 revision  the new revision identity; its source-map digest as in the header; the code of
 *               that revision
 *   8 end       the reason U8 (0 closed by the host, 1 CINT_RESOURCE); the head of the digest
 *               chain of SPEC-06 10.5 (32 bytes); the U64 count of the events before it
 *   9 dispatch  at dispatch density, between an entry and its outcome: the dispatch number I64
 *               and the state hash after its publication (cint_rt_dispatch_end; RT-OQ-48)
 *
 * The digest chain starts from the state hash of the initial checkpoint and takes one boundary
 * per entry that ran, at its outcome, and one per dispatch event, kind 1 with its number. A
 * dispatch event that its entry's outcome follows at the same state hash is no boundary of its
 * own: the outcome's boundary carries its number (SPEC-06 1.5).
 */
#include "cint_rt_internal.h"
#include "cint_state.h"
#include "cint_state_internal.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Recordings (SPEC-03 H-14 to H-18; SPEC-06 10.5).                           */

static const char REC_DOMAIN[] = "cint-core-1/recording/v1";
static const char CASE_DOMAIN[] = "cint-core-1/case/v1";
static const char MAP_DOMAIN[] = "cint-core-1/source-map/v1";
static const char CHAIN_DOMAIN[] = "cint-core-1/recording-chain/v1";
static const char PRINT_NAME[] = "print";

struct cint_recorder {
    cint_ctx *ctx;
    const cint_observer *entries;
    cint_sink sink;
    cint_output_fn output;  /* the host's, to which print statements still go */
    void *output_user;
    uint8_t chain[32];
    uint64_t events;
    cint_status failed;     /* the first status other than CINT_OK the sink returned */
    bool ended;
    bool is_case;
    bool pending;           /* a dispatch event whose boundary waits for the next event */
    int64_t number;         /* its dispatch number */
    uint8_t hash[32];       /* and its state hash */
    uint64_t entry;         /* the event index of the entry that runs */
    size_t fill;
    uint8_t buf[512];
};

static bool ran(cint_status st)
{
    return st == CINT_OK || st == CINT_FAULT;
}

static int32_t as_i32(uint64_t u)
{
    uint32_t v = (uint32_t)u;
    int32_t s;
    memcpy(&s, &v, sizeof s);
    return s;
}

/* True when bits is a pattern of type tag (SPEC-09 CONF-13 X-5). */
static bool pattern_ok(uint64_t bits, uint32_t tag)
{
    unsigned w = cint_rt_tag_width(tag);
    uint64_t hi;
    if (tag == CINT_TAG_BOOL) {
        return bits <= 1u;
    }
    if (w >= 64u) {
        return true;
    }
    hi = bits >> (w - 1u);
    return cint_rt_tag_signed(tag) ? hi == 0u || hi == UINT64_MAX >> (w - 1u) : hi <= 1u;
}

/* The pattern of a scalar tagged value. */
static uint64_t bits_of(const cint_tvalue *v)
{
    uint32_t tag = v->bytes[0], n = cint_state_width_of(tag);
    uint64_t bits = 0u;
    for (uint32_t i = n; i > 0u; i--) {
        bits = bits << 8 | v->bytes[i];
    }
    if (cint_rt_tag_signed(tag) && n < 8u && (v->bytes[n] & 0x80u) != 0u) {
        bits |= UINT64_MAX << (8u * n);
    }
    return bits;
}

static void chain_start(uint8_t chain[32], const uint8_t state_hash[32])
{
    uint8_t b[4u + sizeof CHAIN_DOMAIN - 1u + 32u];
    cint_state_wr w = cint_state_wr_of(b, sizeof b);
    cint_state_w_str(&w, CINT_STATE_LIT(CHAIN_DOMAIN));
    cint_state_w_bytes(&w, state_hash, 32u);
    cint_sha256(b, w.len, chain);
}

/* D_k from D_(k-1) and a boundary of the entry event at `index` (SPEC-06 10.5): kind 0 at its
 * outcome, 1 at a dispatch publication, with dispatch `number` when it is 0 or more; false when
 * memory runs out. */
static bool chain_next(uint8_t chain[32], uint64_t index, uint32_t kind, int64_t number, const uint8_t hash[32],
                       const uint8_t *fault, size_t len)
{
    uint8_t small[512], *b = small;
    size_t n = 4u + sizeof CHAIN_DOMAIN - 1u + 32u + 8u + 3u + (number >= 0 ? 8u : 0u) + 32u +
               (fault != NULL ? 4u + len : 0u);
    cint_state_wr w;
    if (n > sizeof small && (b = (uint8_t *)malloc(n)) == NULL) {
        return false;
    }
    w = cint_state_wr_of(b, n);
    cint_state_w_str(&w, CINT_STATE_LIT(CHAIN_DOMAIN));
    cint_state_w_bytes(&w, chain, 32u);
    cint_state_w_le(&w, index, 8u);
    cint_state_w_le(&w, kind, 1u);
    cint_state_w_le(&w, number >= 0 ? 1u : 0u, 1u);
    if (number >= 0) {
        cint_state_w_le(&w, (uint64_t)number, 8u);
    }
    cint_state_w_bytes(&w, hash, 32u);
    cint_state_w_le(&w, fault != NULL ? 1u : 0u, 1u);
    if (fault != NULL) {
        cint_state_w_str(&w, fault, len);
    }
    cint_sha256(b, w.len, chain);
    if (b != small) {
        free(b);
    }
    return true;
}

static void r_flush(cint_recorder *r)
{
    if (r->fill > 0u && r->failed == CINT_OK) {
        r->failed = r->sink.write(r->sink.user, r->buf, r->fill);
    }
    r->fill = 0u;
}

static void r_put(cint_recorder *r, const void *p, size_t n)
{
    if (n > sizeof r->buf - r->fill) {
        r_flush(r);
        if (n > sizeof r->buf) {
            if (r->failed == CINT_OK) {
                r->failed = r->sink.write(r->sink.user, (const uint8_t *)p, n);
            }
            return;
        }
    }
    if (n > 0u) {
        memcpy(r->buf + r->fill, p, n);
    }
    r->fill += n;
}

static void r_le(cint_recorder *r, uint64_t v, unsigned n)
{
    uint8_t b[8];
    for (unsigned i = 0u; i < n; i++, v >>= 8u) {
        b[i] = (uint8_t)v;
    }
    r_put(r, b, n);
}

static void r_str(cint_recorder *r, const void *s, size_t n)
{
    r_le(r, n, 4u);
    r_put(r, s, n);
}

static bool code_ok(const cint_code_module *code, uint32_t n)
{
    for (uint32_t i = 0u; i < n; i++) {
        const cint_name *p = &code[i].path;
        if (p->bytes == NULL || p->len < 4u || p->len > CINT_FAULT_MAX_PATH ||
            memcmp(p->bytes + p->len - 3u, ".ci", 3u) != 0 || !cint_state_utf8_ok((const uint8_t *)p->bytes, p->len) ||
            (code[i].sir == NULL && code[i].sir_len > 0u) || (code[i].source == NULL && code[i].source_len > 0u)) {
            return false;
        }
    }
    return n == 0u || code != NULL;
}

/* One module's part of the source map: its path, the count of its sites, and each site of its
 * `sites` line, the second line of its SIR text; false unless that line is `sites <path>` and
 * sites <ordinal>:<line>:<column>:<operation>. */
static bool map_module(cint_state_wr *w, const cint_code_module *m)
{
    const uint8_t *p, *q, *s;
    uint64_t count = 0u;
    size_t at;
    if (m->sir == NULL || (p = (const uint8_t *)memchr(m->sir, '\n', m->sir_len)) == NULL) {
        return false;
    }
    p++;
    q = (const uint8_t *)memchr(p, '\n', m->sir_len - (size_t)(p - m->sir));
    if (q == NULL || (size_t)(q - p) < 6u + m->path.len || memcmp(p, "sites ", 6u) != 0 ||
        memcmp(p + 6u, m->path.bytes, m->path.len) != 0) {
        return false;
    }
    p += 6u + m->path.len;
    cint_state_w_str(w, m->path.bytes, m->path.len);
    at = w->len;
    cint_state_w_le(w, 0u, 4u);
    while (p < q) {
        if (*p++ != ' ') {
            return false;
        }
        for (int f = 0; f < 3; f++) {
            uint64_t v = 0u;
            for (s = p; p < q && *p >= '0' && *p <= '9' && v <= UINT32_MAX; p++) {
                v = v * 10u + (uint64_t)(*p - '0');
            }
            if (p == s || v > UINT32_MAX || p == q || *p++ != ':') {
                return false;
            }
            cint_state_w_le(w, v, 4u);
        }
        for (s = p; p < q && *p > ' ' && *p < 0x7f && *p != ':'; p++) {
        }
        if (p == s || ++count > UINT32_MAX) {
            return false;
        }
        cint_state_w_str(w, s, (size_t)(p - s));
    }
    for (unsigned i = 0u; w->buf != NULL && i < 4u; i++) {
        w->buf[at + i] = (uint8_t)(count >> (8u * i));
    }
    return true;
}

static int by_path(const void *a, const void *b)
{
    const cint_name *x = &(*(const cint_code_module *const *)a)->path, *y = &(*(const cint_code_module *const *)b)->path;
    return cint_state_name_cmp((const uint8_t *)x->bytes, x->len, (const uint8_t *)y->bytes, y->len);
}

cint_status cint_code_source_map(const cint_code_module *code, uint32_t n, uint8_t out[32])
{
    const cint_code_module **order;
    uint8_t *b = NULL;
    size_t need = 0u;
    cint_status st = CINT_OK;
    if (code == NULL || n == 0u || out == NULL) {
        return CINT_REFUSED;
    }
    if ((order = (const cint_code_module **)malloc((size_t)n * sizeof *order)) == NULL) {
        return CINT_RESOURCE;
    }
    for (uint32_t i = 0u; i < n; i++) {
        order[i] = &code[i];
    }
    qsort((void *)order, n, sizeof *order, by_path);
    for (int pass = 0; pass < 2 && st == CINT_OK; pass++) {  /* measure, then write */
        cint_state_wr w = cint_state_wr_of(b, need);
        cint_state_w_str(&w, CINT_STATE_LIT(MAP_DOMAIN));
        cint_state_w_le(&w, n, 4u);
        for (uint32_t i = 0u; i < n && st == CINT_OK; i++) {
            if ((i > 0u && by_path(&order[i - 1u], &order[i]) == 0) || !map_module(&w, order[i])) {
                st = CINT_REFUSED;
            }
        }
        if (st == CINT_OK && pass == 0 && (b = (uint8_t *)malloc(need = w.len)) == NULL) {
            st = CINT_RESOURCE;
        } else if (st == CINT_OK && pass == 1) {
            cint_sha256(b, w.len, out);
        }
    }
    free(b);
    free((void *)order);
    return st;
}

/* Each module's source, as a case needs. */
static bool sources_ok(const cint_code_module *code, uint32_t n)
{
    for (uint32_t i = 0u; i < n; i++) {
        if (code[i].source == NULL) {
            return false;
        }
    }
    return n > 0u;
}

/* The source-map digest that cint_code_source_map gave with status st, or its absence. */
static void r_map(cint_recorder *r, cint_status st, const uint8_t map[32])
{
    r_le(r, st == CINT_OK ? 1u : 0u, 1u);
    if (st == CINT_OK) {
        r_put(r, map, 32u);
    }
}

static void r_code(cint_recorder *r, const cint_code_module *code, uint32_t n)
{
    r_le(r, n, 4u);
    for (uint32_t i = 0u; i < n; i++) {
        r_str(r, code[i].path.bytes, code[i].path.len);
        r_le(r, code[i].sir_len, 8u);
        r_put(r, code[i].sir, code[i].sir_len);
        r_le(r, code[i].source != NULL ? 1u : 0u, 1u);
        if (code[i].source != NULL) {
            r_le(r, code[i].source_len, 8u);
            r_put(r, code[i].source, code[i].source_len);
        }
    }
}

static void r_end(cint_recorder *r, uint32_t reason)
{
    r_le(r, CINT_EVENT_END, 1u);
    r_le(r, reason, 1u);
    r_put(r, r->chain, 32u);
    r_le(r, r->events, 8u);
    r_flush(r);
    r->ended = true;
}

static void r_fail(cint_recorder *r)
{
    r->failed = r->failed != CINT_OK ? r->failed : CINT_RESOURCE;
}

/* The boundary of a pending dispatch event, kind 1, once an event other than its entry's outcome
 * at the same state follows it (SPEC-06 1.5). */
static void r_settle(cint_recorder *r)
{
    if (r->pending && !chain_next(r->chain, r->entry, 1u, r->number, r->hash, NULL, 0u)) {
        r_fail(r);
    }
    r->pending = false;
}

/* cint_rt_dispatch_end while a recorder records at dispatch density: a `dispatch` event with the
 * state hash after the publication, whose boundary waits for the next event. */
static void rec_dispatch(cint_ctx *ctx, int64_t number)
{
    cint_recorder *r = (cint_recorder *)ctx->boundary_user;
    if (r->ended) {
        return;
    }
    r_settle(r);
    if (cint_state_boundary(ctx, r->hash, NULL, NULL) != CINT_OK) {
        r_fail(r);
    }
    r_le(r, CINT_EVENT_DISPATCH, 1u);
    r_le(r, (uint64_t)number, 8u);
    r_put(r, r->hash, 32u);
    r_flush(r);
    r->events++;
    r->pending = true;
    r->number = number;
}

/* The context's print statements while a recorder records it: delivered to the host's
 * output, then recorded as `effect` events with the host's status (SPEC-06 10.2). */
static cint_status rec_output(void *user, const uint8_t *bytes, size_t len)
{
    cint_recorder *r = (cint_recorder *)user;
    cint_ctx *ctx = r->ctx;
    cint_status st = r->output != NULL ? r->output(r->output_user, bytes, len) : CINT_OK;
    r_settle(r);
    r_le(r, CINT_EVENT_EFFECT, 1u);
    r_le(r, (uint64_t)ctx->effects, 8u);
    r_le(r, ctx->print_site.module, 4u);
    r_le(r, ctx->print_site.index, 4u);
    r_str(r, CINT_STATE_LIT(PRINT_NAME));
    r_le(r, (uint32_t)st, 4u);
    r_le(r, len, 8u);
    r_put(r, bytes, len);
    r_flush(r);
    r->events++;
    return st;
}

cint_status cint_rec_begin(cint_ctx *ctx, const cint_observer *entries, const cint_code_module *code,
                           uint32_t code_count, uint32_t mode, cint_sink *sink, cint_recorder **out)
{
    uint8_t hash[32], map[32], *ckpt = NULL;
    size_t ckpt_len = 0u;
    cint_recorder *r = NULL;
    cint_status st, ms = CINT_REFUSED;
    bool dispatch = (mode & CINT_RECORD_DISPATCH) != 0u;
    mode &= ~CINT_RECORD_DISPATCH;
    if (ctx == NULL || entries == NULL || sink == NULL || sink->write == NULL || out == NULL ||
        mode > CINT_RECORD_CASE || !code_ok(code, code_count) ||
        (mode == CINT_RECORD_CASE && !sources_ok(code, code_count))) {
        return CINT_REFUSED;
    }
    if (!cint_rt_busy_take(ctx)) {
        return CINT_BUSY;
    }
    st = ctx->output == rec_output ? CINT_REFUSED : cint_state_boundary(ctx, hash, &ckpt, &ckpt_len);
    if (st == CINT_OK && (ms = cint_code_source_map(code, code_count, map)) == CINT_RESOURCE) {
        st = CINT_RESOURCE;
    }
    if (st == CINT_OK && (r = (cint_recorder *)cint_state_mem_alloc(ctx, sizeof *r, _Alignof(cint_recorder))) == NULL) {
        st = CINT_RESOURCE;
    }
    if (st == CINT_OK) {
        memset(r, 0, sizeof *r);
        r->ctx = ctx;
        r->entries = entries;
        r->sink = *sink;
        r->output = ctx->output;
        r->output_user = ctx->output_user;
        r->is_case = mode == CINT_RECORD_CASE;
        if (r->is_case) {
            r_str(r, CINT_STATE_LIT(CASE_DOMAIN));
        } else {
            r_str(r, CINT_STATE_LIT(REC_DOMAIN));
        }
        r_str(r, CINT_STATE_LIT(CINT_STATE_PROFILE));
        r_str(r, CINT_STATE_LIT(CINT_STATE_FUEL_MODEL));
        r_str(r, CINT_STATE_LIT(CINT_RT_CONTRACT));
        r_put(r, ctx->program->revision, 32u);
        r_map(r, ms, map);
        r_le(r, r->is_case ? CINT_RECORD_FULL : mode, 1u);
        r_le(r, 1u, 8u);  /* the state hash at every entry (H-14a) */
        r_le(r, dispatch ? 1u : 0u, 1u);  /* the density */
        r_code(r, code, code_count);
        r_le(r, ckpt_len, 8u);
        r_put(r, ckpt, ckpt_len);
        r_flush(r);
        chain_start(r->chain, hash);
        if ((st = r->failed) != CINT_OK) {
            cint_state_mem_free(ctx, r, sizeof *r, _Alignof(cint_recorder));
        } else {
            ctx->output = rec_output;
            ctx->output_user = r;
            ctx->boundary = dispatch ? rec_dispatch : NULL;
            ctx->boundary_user = r;
            *out = r;
        }
    }
    free(ckpt);
    cint_rt_busy_give(ctx);
    return st;
}

/* The `outcome` of the entry event at `index`, called with budget `fuel`: status st, result
 * res of type rtag (0: none). */
static void r_outcome(cint_recorder *r, uint64_t index, cint_status st, uint32_t rtag, uint64_t res, int64_t fuel)
{
    cint_ctx *ctx = r->ctx;
    const cint_error_set *set = NULL;
    uint64_t tag = 0u;
    int64_t used = 0;
    uint8_t hash[32], *fault = NULL;
    size_t flen = 0u;
    cint_tvalue v;
    bool merged;
    memset(hash, 0, sizeof hash);
    if ((st == CINT_FAULT || st == CINT_RESOURCE) && ctx->head.faulted != 0u &&
        ((flen = cint_fault_encode(&ctx->fault, NULL, 0u)) == 0u || (fault = (uint8_t *)malloc(flen)) == NULL ||
         cint_fault_encode(&ctx->fault, fault, flen) != flen)) {
        r->failed = r->failed != CINT_OK ? r->failed : CINT_RESOURCE;
    }
    if (cint_fuel_consumed(ctx, &used) != CINT_OK || cint_ctx_error(ctx, &set, &tag) != CINT_OK ||
        (ran(st) && cint_ctx_state_hash(ctx, hash) != CINT_OK)) {
        r->failed = r->failed != CINT_OK ? r->failed : CINT_RESOURCE;
    }
    merged = r->pending && ran(st) && memcmp(hash, r->hash, 32u) == 0;
    if (!merged) {
        r_settle(r);
    }
    r_le(r, CINT_EVENT_OUTCOME, 1u);
    r_le(r, (uint32_t)st, 4u);
    r_le(r, fault != NULL ? 1u : 0u, 1u);
    if (fault != NULL) {
        r_str(r, fault, flen);
    }
    r_le(r, ran(st) ? (uint64_t)used : 0u, 8u);
    r_le(r, st == CINT_OK && rtag != 0u && set == NULL ? 1u : 0u, 1u);
    if (st == CINT_OK && rtag != 0u && set == NULL) {
        cint_rt_tvalue_scalar(&v, rtag, res);
        r_put(r, v.bytes, v.len);
    }
    r_le(r, st == CINT_OK && set != NULL ? 1u : 0u, 1u);
    if (st == CINT_OK && set != NULL) {
        r_str(r, set->name, strlen(set->name));
        r_le(r, tag, 8u);
    }
    r_le(r, 0u, 4u);  /* no output records before box 13 unit 7 */
    r_le(r, ran(st) ? 1u : 0u, 1u);
    if (ran(st)) {
        r_put(r, hash, 32u);
    }
    r_le(r, (uint64_t)(ran(st) ? ctx->head.fuel_limit : fuel), 8u);
    r->events++;
    if (ran(st) && !chain_next(r->chain, index, 0u, merged ? r->number : -1, hash, st == CINT_FAULT ? fault : NULL,
                               flen)) {
        r->failed = r->failed != CINT_OK ? r->failed : CINT_RESOURCE;
    }
    r->pending = false;
    free(fault);
    r_flush(r);
    if (st == CINT_RESOURCE) {
        r_end(r, 1u);  /* the recording ends at this entry (A-6) */
    }
}

/* The `entry` event of a call with budget fuel and depth limit depth. */
static uint64_t r_entry(cint_recorder *r, uint32_t form, const cint_observer_entry *e, const uint64_t *args,
                        int64_t fuel, int64_t depth)
{
    cint_ctx *ctx = r->ctx;
    const cint_module *m = e != NULL ? r->entries->program->modules[e->module] : NULL;
    uint32_t n = e != NULL ? e->types[1] : 0u;
    cint_tvalue v;
    r_le(r, CINT_EVENT_ENTRY, 1u);
    r_le(r, ctx->entries + 1u, 8u);
    r_le(r, form, 1u);
    r_str(r, m != NULL ? m->path : "", m != NULL ? m->path_len : 0u);
    r_str(r, e != NULL ? e->name : "", e != NULL ? e->name_len : 0u);
    r_le(r, n, 4u);
    for (uint32_t i = 0u; i < n; i++) {
        cint_rt_tvalue_scalar(&v, e->types[2u + i], args[i]);
        r_put(r, v.bytes, v.len);
    }
    r_le(r, (uint64_t)fuel, 8u);
    r_le(r, (uint64_t)depth, 8u);
    r_le(r, (uint64_t)ctx->frame_arena_bytes, 8u);
    r_le(r, 0u, 4u);  /* no input records before box 13 unit 7 */
    r_flush(r);
    r->entry = r->events;
    return r->events++;
}

cint_status cint_rec_entry(cint_recorder *r, uint32_t entry, const uint64_t *args, int64_t fuel, int64_t depth,
                           uint64_t *result)
{
    const cint_observer_entry *e;
    uint64_t res = 0u, index;
    cint_status st;
    if (r == NULL || r->ended || entry >= r->entries->entry_count) {
        return CINT_REFUSED;
    }
    e = &r->entries->entries[entry];
    if (e->types == NULL || e->types[1] > 16u || (e->types[1] > 0u && args == NULL) ||
        (e->types[0] != 0u && cint_state_width_of(e->types[0]) == 0u) ||
        e->module >= r->entries->program->module_count) {
        return CINT_REFUSED;
    }
    for (uint32_t i = 0u; i < e->types[1]; i++) {
        if (cint_state_width_of(e->types[2u + i]) == 0u || !pattern_ok(args[i], e->types[2u + i])) {
            return CINT_REFUSED;
        }
    }
    depth = depth == CINT_DEPTH_FROM_CONFIG ? r->ctx->config_depth : depth;
    index = r_entry(r, 0u, e, args, fuel, depth);
    st = e->call(r->ctx, fuel, depth, args, &res);
    r_outcome(r, index, st, e->types[0], res, fuel);
    if (st == CINT_OK && result != NULL) {
        *result = res;
    }
    return st;
}

cint_status cint_rec_main(cint_recorder *r, cint_main_fn main_fn, int64_t fuel)
{
    uint64_t index;
    cint_status st;
    if (r == NULL || r->ended || main_fn == NULL) {
        return CINT_REFUSED;
    }
    index = r_entry(r, 1u, NULL, NULL, fuel, r->ctx->config_depth);
    st = main_fn(r->ctx, fuel);
    r_outcome(r, index, st, 0u, 0u, fuel);
    return st;
}

cint_status cint_rec_checkpoint(cint_recorder *r, cint_sink *sink)
{
    uint8_t hash[32];
    cint_status st;
    if (r == NULL || r->ended) {
        return CINT_REFUSED;
    }
    if ((st = cint_ctx_checkpoint(r->ctx, sink)) == CINT_OK && (st = cint_ctx_state_hash(r->ctx, hash)) == CINT_OK) {
        r_le(r, CINT_EVENT_CHECKPOINT, 1u);
        r_put(r, hash, 32u);
        r_flush(r);
        r->events++;
    }
    return st;
}

cint_status cint_rec_restore(cint_recorder *r, const uint8_t *bytes, size_t length)
{
    cint_checkpoint_info c;
    uint8_t hash[32];
    cint_status st;
    if (r == NULL || r->ended) {
        return CINT_REFUSED;
    }
    if ((st = cint_ctx_restore(r->ctx, bytes, length)) == CINT_OK && cint_checkpoint_decode(bytes, length, &c) == CINT_OK) {
        cint_sha256(c.state, c.state_len, hash);
        r_le(r, CINT_EVENT_RESTORE, 1u);
        r_put(r, hash, 32u);
        r_le(r, r->ctx->last_id + 1u, 8u);
        r_le(r, r->ctx->last_arena + 1u, 8u);
        r_le(r, r->ctx->last_pool + 1u, 8u);
        r_le(r, length, 8u);
        r_put(r, bytes, length);
        r_flush(r);
        r->events++;
    }
    return st;
}

cint_status cint_rec_clear_fault(cint_recorder *r)
{
    uint8_t hash[32], *fault = NULL;
    size_t n = 0u;
    cint_status st;
    memset(hash, 0, sizeof hash);
    if (r == NULL || r->ended) {
        return CINT_REFUSED;
    }
    if (r->ctx->head.faulted != 0u && ((n = cint_fault_encode(&r->ctx->fault, NULL, 0u)) == 0u ||
                                       (fault = (uint8_t *)malloc(n)) == NULL)) {
        return CINT_RESOURCE;
    }
    if (fault != NULL) {
        (void)cint_fault_encode(&r->ctx->fault, fault, n);
        cint_sha256(fault, n, hash);
        free(fault);
    }
    if ((st = cint_ctx_clear_fault(r->ctx)) == CINT_OK && n > 0u) {  /* clearing no fault changes nothing */
        r_le(r, CINT_EVENT_CLEAR_FAULT, 1u);
        r_put(r, hash, 32u);
        r_flush(r);
        r->events++;
    }
    return st;
}

cint_status cint_rec_change_revision(cint_recorder *r, const cint_observer *entries, const cint_state_table *t,
                                     const cint_code_module *code, uint32_t code_count)
{
    cint_status st;
    uint8_t map[32];
    cint_status ms;
    if (r == NULL || r->ended || entries == NULL || !code_ok(code, code_count) ||
        (r->is_case && !sources_ok(code, code_count))) {
        return CINT_REFUSED;
    }
    if ((ms = code_count > 0u ? cint_code_source_map(code, code_count, map) : CINT_REFUSED) == CINT_RESOURCE) {
        return CINT_RESOURCE;
    }
    if ((st = cint_ctx_change_revision(r->ctx, entries->program, t)) == CINT_OK) {
        r->entries = entries;
        r_le(r, CINT_EVENT_REVISION, 1u);
        r_put(r, entries->program->revision, 32u);
        r_map(r, ms, map);
        r_code(r, code, code_count);
        r_flush(r);
        r->events++;
    }
    return st;
}

cint_status cint_rec_end(cint_recorder *r)
{
    cint_ctx *ctx;
    cint_status st;
    if (r == NULL) {
        return CINT_REFUSED;
    }
    ctx = r->ctx;
    if (!cint_rt_busy_take(ctx)) {
        return CINT_BUSY;
    }
    if (!r->ended) {
        r_end(r, 0u);
    }
    ctx->output = r->output;
    ctx->output_user = r->output_user;
    ctx->boundary = NULL;
    ctx->boundary_user = NULL;
    st = r->failed;
    cint_state_mem_free(ctx, r, sizeof *r, _Alignof(cint_recorder));
    cint_rt_busy_give(ctx);
    return st;
}

/* ------------------------------------------------------------------------- */
/* Reading a recording.                                                       */

/* A string of at most max bytes of UTF-8, at least one when `nonempty`. */
static void rd_name(cint_state_rd *r, cint_name *out, uint64_t max, bool nonempty)
{
    uint64_t n = cint_state_rd_u(r, 4u);
    const uint8_t *p = n <= max ? cint_state_rd_take(r, n) : NULL;
    out->bytes = (const char *)p;
    out->len = (uint32_t)n;
    out->reserved = 0u;
    if (p == NULL || (nonempty && n == 0u) || !cint_state_utf8_ok(p, (size_t)n)) {
        r->bad = true;
    }
}

/* A code block: modules with paths ending in .ci, their SIR text, and their source. */
static void rd_code(cint_state_rd *r, uint32_t *count)
{
    uint64_t n = cint_state_rd_u(r, 4u);
    for (uint64_t i = 0u; i < n && !r->bad; i++) {
        cint_name path;
        rd_name(r, &path, CINT_FAULT_MAX_PATH, true);
        if (r->bad || path.len < 4u || memcmp(path.bytes + path.len - 3u, ".ci", 3u) != 0) {
            r->bad = true;
            return;
        }
        (void)cint_state_rd_take(r, cint_state_rd_u(r, 8u));
        if (cint_state_rd_flag(r)) {
            (void)cint_state_rd_take(r, cint_state_rd_u(r, 8u));
        }
    }
    *count = (uint32_t)n;
}

static void rd_hash(cint_state_rd *r, uint8_t out[32])
{
    const uint8_t *p = cint_state_rd_take(r, 32u);
    if (p != NULL) {
        memcpy(out, p, 32u);
    }
}

/* One event, its fields checked alone. */
static void rd_event(cint_state_rd *r, cint_recording_event *e)
{
    cint_checkpoint_info c;
    uint8_t hash[32];
    uint64_t n;
    size_t at;
    memset(e, 0, sizeof *e);
    e->kind = (uint32_t)cint_state_rd_u(r, 1u);
    if (e->kind == CINT_EVENT_ENTRY) {
        e->sequence = cint_state_rd_u(r, 8u);
        e->form = (uint32_t)cint_state_rd_u(r, 1u);
        rd_name(r, &e->module, CINT_FAULT_MAX_PATH, e->form == 0u);
        rd_name(r, &e->name, 255u, e->form == 0u);
        n = cint_state_rd_u(r, 4u);
        e->arg_count = (uint32_t)n;
        e->args_at = r->at;
        for (uint64_t i = 0u; i < n && n <= 16u && !r->bad; i++) {
            cint_state_rd_value(r, NULL, true);
        }
        e->fuel = cint_state_as_i64(cint_state_rd_u(r, 8u));
        e->depth = cint_state_as_i64(cint_state_rd_u(r, 8u));
        e->arena = cint_state_as_i64(cint_state_rd_u(r, 8u));
        r->bad = r->bad || e->form > 1u || n > 16u || (e->form == 1u && (e->module.len > 0u || e->name.len > 0u || n > 0u)) ||
                 e->sequence == 0u || e->fuel < CINT_FUEL_UNBOUNDED || e->depth < 1 || e->arena < 0 ||
                 cint_state_rd_u(r, 4u) != 0u;
    } else if (e->kind == CINT_EVENT_EFFECT) {
        e->ordinal = cint_state_as_i64(cint_state_rd_u(r, 8u));
        e->site.module = (uint32_t)cint_state_rd_u(r, 4u);
        e->site.index = (uint32_t)cint_state_rd_u(r, 4u);
        rd_name(r, &e->name, 255u, true);
        e->status = as_i32(cint_state_rd_u(r, 4u));
        n = cint_state_rd_u(r, 8u);
        e->data = cint_state_rd_take(r, n);
        e->data_len = (size_t)n;
        r->bad = r->bad || e->ordinal < 0 || e->name.len != sizeof PRINT_NAME - 1u ||
                 memcmp(e->name.bytes, PRINT_NAME, e->name.len) != 0;
    } else if (e->kind == CINT_EVENT_OUTCOME) {
        e->status = as_i32(cint_state_rd_u(r, 4u));
        if (cint_state_rd_flag(r)) {
            cint_state_fault_view f;
            n = cint_state_rd_u(r, 4u);
            at = r->at;
            cint_state_rd_fault(r, &f);
            e->data = r->p + at;
            e->data_len = r->at - at;
            r->bad = r->bad || e->data_len != n;
        }
        e->consumed = cint_state_as_i64(cint_state_rd_u(r, 8u));
        if ((e->has_result = cint_state_rd_flag(r)) != 0u) {
            cint_state_rd_value(r, &e->result, true);
        }
        if ((e->has_error = cint_state_rd_flag(r)) != 0u) {
            rd_name(r, &e->error_set, 1300u, true);
            e->error_tag = cint_state_rd_u(r, 8u);
            r->bad = r->bad || e->error_tag == 0u;
        }
        r->bad = r->bad || cint_state_rd_u(r, 4u) != 0u;
        if ((e->has_hash = cint_state_rd_flag(r)) != 0u) {
            rd_hash(r, e->hash);
        }
        e->allowance = cint_state_as_i64(cint_state_rd_u(r, 8u));
        r->bad = r->bad || e->status < CINT_OK || e->status > CINT_RESOURCE || e->consumed < 0 ||
                 e->allowance < CINT_FUEL_UNBOUNDED;
    } else if (e->kind == CINT_EVENT_CLEAR_FAULT || e->kind == CINT_EVENT_CHECKPOINT) {
        rd_hash(r, e->hash);
    } else if (e->kind == CINT_EVENT_RESTORE) {
        rd_hash(r, e->hash);
        e->next_buffer = cint_state_rd_u(r, 8u);
        e->next_arena = cint_state_rd_u(r, 8u);
        e->next_pool = cint_state_rd_u(r, 8u);
        n = cint_state_rd_u(r, 8u);
        e->data = cint_state_rd_take(r, n);
        e->data_len = (size_t)n;
        if (!r->bad && cint_checkpoint_decode(e->data, e->data_len, &c) == CINT_OK) {
            cint_sha256(c.state, c.state_len, hash);
            r->bad = memcmp(hash, e->hash, 32u) != 0 || e->next_buffer < c.next_buffer ||
                     e->next_arena < c.next_arena || e->next_pool < c.next_pool;
        } else {
            r->bad = true;
        }
    } else if (e->kind == CINT_EVENT_REVISION) {
        rd_hash(r, e->revision);
        if ((e->has_hash = cint_state_rd_flag(r)) != 0u) {
            rd_hash(r, e->hash);
        }
        at = r->at;
        rd_code(r, &e->module_count);
        e->data = r->p + at;
        e->data_len = r->at - at;
    } else if (e->kind == CINT_EVENT_END) {
        e->reason = (uint32_t)cint_state_rd_u(r, 1u);
        rd_hash(r, e->hash);
        e->sequence = cint_state_rd_u(r, 8u);
        r->bad = r->bad || e->reason > 1u;
    } else if (e->kind == CINT_EVENT_DISPATCH) {
        e->ordinal = cint_state_as_i64(cint_state_rd_u(r, 8u));
        rd_hash(r, e->hash);
        e->has_hash = 1u;
        r->bad = r->bad || e->ordinal < 0;
    } else {
        r->bad = true;
    }
}

/* The code block at `at`, of n modules: CINT_OK when its source-map digest is present exactly
 * when cint_code_source_map gives one and equals it, and in a case each module has its source. */
static cint_status code_check(const cint_recording_info *c, size_t at, uint32_t n, uint32_t has, const uint8_t map[32])
{
    cint_code_module *m = n > 0u ? (cint_code_module *)calloc(n, sizeof *m) : NULL;
    uint8_t d[32];
    cint_status st = n > 0u && m == NULL ? CINT_RESOURCE : c->is_case && n == 0u ? CINT_REFUSED : CINT_OK;
    cint_state_rd r = cint_state_rd_of(c->bytes, c->len);
    r.at = at + 4u;
    for (uint32_t i = 0u; st == CINT_OK && i < n; i++) {
        uint64_t k;
        rd_name(&r, &m[i].path, CINT_FAULT_MAX_PATH, true);
        k = cint_state_rd_u(&r, 8u);
        m[i].sir = cint_state_rd_take(&r, k);
        m[i].sir_len = (size_t)k;
        if (cint_state_rd_flag(&r)) {
            k = cint_state_rd_u(&r, 8u);
            m[i].source = cint_state_rd_take(&r, k);
            m[i].source_len = (size_t)k;
        }
        st = r.bad || (c->is_case && m[i].source == NULL) ? CINT_REFUSED : CINT_OK;
    }
    if (st == CINT_OK) {
        cint_status ms = n > 0u ? cint_code_source_map(m, n, d) : CINT_REFUSED;
        st = ms == CINT_RESOURCE                                          ? CINT_RESOURCE
             : (ms == CINT_OK) != (has != 0u) || (has != 0u && memcmp(d, map, 32u) != 0) ? CINT_REFUSED
                                                                          : CINT_OK;
    }
    free(m);
    return st;
}

/* An outcome that fits its entry and its status. */
static bool outcome_ok(const cint_recording_event *entry, const cint_recording_event *o)
{
    bool fault = o->data != NULL;
    if (!ran(o->status)) {
        return (o->status == CINT_RESOURCE || !fault) && !o->has_result && !o->has_error && !o->has_hash &&
               o->consumed == 0 && o->allowance == entry->fuel;
    }
    return o->has_hash && fault == (o->status == CINT_FAULT) && !(o->has_result && o->has_error) &&
           (o->status == CINT_OK || (!o->has_result && !o->has_error)) &&
           (o->allowance == entry->fuel || (o->allowance >= 0 && (entry->fuel < 0 || o->allowance < entry->fuel))) &&
           (o->allowance < 0 || o->consumed <= o->allowance);
}

cint_status cint_recording_decode(const uint8_t *bytes, size_t len, cint_recording_info *out)
{
    cint_recording_info c;
    cint_checkpoint_info k;
    cint_recording_event e, entry, last;
    uint8_t hash[32], chain[32];
    cint_state_rd r = cint_state_rd_of(bytes, len);
    uint64_t n, count = 0u;
    bool in_entry = false, end = false, resource = false;
    int64_t merged;  /* the dispatch number an outcome's boundary carries, or -1 */
    cint_status st;
    if (bytes == NULL || out == NULL) {
        return CINT_REFUSED;
    }
    memset(&c, 0, sizeof c);
    memset(&entry, 0, sizeof entry);
    memset(&last, 0, sizeof last);
    memset(hash, 0, sizeof hash);
    c.bytes = bytes;
    c.len = len;
    c.is_case = len > 0u && bytes[0] == sizeof CASE_DOMAIN - 1u;
    if (c.is_case) {
        cint_state_rd_lit(&r, CINT_STATE_LIT(CASE_DOMAIN));
    } else {
        cint_state_rd_lit(&r, CINT_STATE_LIT(REC_DOMAIN));
    }
    cint_state_rd_lit(&r, CINT_STATE_LIT(CINT_STATE_PROFILE));
    cint_state_rd_lit(&r, CINT_STATE_LIT(CINT_STATE_FUEL_MODEL));
    n = cint_state_rd_u(&r, 4u);
    c.contract.bytes = (const char *)(n >= 1u && n <= CINT_STATE_CONTRACT_MAX ? cint_state_rd_take(&r, n) : NULL);
    c.contract.len = (uint32_t)n;
    for (uint32_t i = 0u; c.contract.bytes != NULL && i < c.contract.len; i++) {
        r.bad = r.bad || c.contract.bytes[i] < 0x21 || c.contract.bytes[i] > 0x7e;
    }
    r.bad = r.bad || c.contract.bytes == NULL;
    rd_hash(&r, c.revision);
    if ((c.has_map = cint_state_rd_flag(&r)) != 0u) {
        rd_hash(&r, c.map);
    }
    c.mode = (uint32_t)cint_state_rd_u(&r, 1u);
    c.interval = cint_state_rd_u(&r, 8u);
    c.density = (uint32_t)cint_state_rd_u(&r, 1u);
    c.code_at = r.at;
    rd_code(&r, &c.module_count);
    n = cint_state_rd_u(&r, 8u);
    c.initial = cint_state_rd_take(&r, n);
    c.initial_len = (size_t)n;
    if (r.bad || c.mode > (c.is_case ? CINT_RECORD_FULL : CINT_RECORD_DIGEST) || c.interval != 1u || c.density > 1u ||
        cint_checkpoint_decode(c.initial, c.initial_len, &k) != CINT_OK || memcmp(k.revision, c.revision, 32u) != 0) {
        return CINT_REFUSED;
    }
    if ((st = code_check(&c, c.code_at, c.module_count, c.has_map, c.map)) != CINT_OK) {
        return st;
    }
    cint_sha256(k.state, k.state_len, hash);
    chain_start(chain, hash);
    c.events_at = r.at;
    while (!end && !r.bad) {
        if (r.at == r.n) {
            return CINT_REFUSED;  /* incomplete: no end event (box 13 default BX13-12) */
        }
        rd_event(&r, &e);
        if (r.bad || resource != (e.kind == CINT_EVENT_END && e.reason == 1u && !in_entry)) {
            return CINT_REFUSED;  /* after CINT_RESOURCE only the end, with reason 1 */
        }
        if (in_entry !=
                (e.kind == CINT_EVENT_EFFECT || e.kind == CINT_EVENT_OUTCOME || e.kind == CINT_EVENT_DISPATCH) ||
            (e.kind == CINT_EVENT_DISPATCH && c.density == 0u)) {
            return CINT_REFUSED;
        }
        /* A dispatch event is a boundary of its own unless its entry's outcome follows at its state. */
        merged = last.kind == CINT_EVENT_DISPATCH && e.kind == CINT_EVENT_OUTCOME && e.has_hash &&
                         memcmp(e.hash, last.hash, 32u) == 0
                     ? last.ordinal
                     : -1;
        if (last.kind == CINT_EVENT_DISPATCH && merged < 0) {
            if (!chain_next(chain, entry.index, 1u, last.ordinal, last.hash, NULL, 0u)) {
                return CINT_RESOURCE;
            }
            c.boundaries++;
        }
        last = e;
        if (e.kind == CINT_EVENT_ENTRY) {
            entry = e;
            entry.index = count;
            in_entry = true;
        } else if (e.kind == CINT_EVENT_REVISION &&
                   (st = code_check(&c, (size_t)(e.data - bytes), e.module_count, e.has_hash, e.hash)) != CINT_OK) {
            return st;
        } else if (e.kind == CINT_EVENT_OUTCOME) {
            in_entry = false;
            resource = e.status == CINT_RESOURCE;
            if (!outcome_ok(&entry, &e)) {
                return CINT_REFUSED;
            }
            if (ran(e.status)) {
                if (!chain_next(chain, entry.index, 0u, merged, e.hash, e.data, e.data_len)) {
                    return CINT_RESOURCE;
                }
                c.boundaries++;
            }
        } else if (e.kind == CINT_EVENT_END) {
            end = true;
            c.end_reason = e.reason;
            c.event_count = count;
            if (e.sequence != count || memcmp(e.hash, chain, 32u) != 0) {
                return CINT_REFUSED;
            }
            memcpy(c.chain, chain, 32u);
        }
        count++;
    }
    if (r.bad || r.at != r.n) {
        return CINT_REFUSED;
    }
    *out = c;
    return CINT_OK;
}

cint_status cint_recording_next(const cint_recording_info *info, cint_recording_event *e)
{
    cint_recording_event x;
    cint_state_rd r;
    if (info == NULL || e == NULL || info->bytes == NULL || (e->next != 0u && e->kind == CINT_EVENT_END)) {
        return CINT_REFUSED;
    }
    r = cint_state_rd_of(info->bytes, info->len);
    r.at = e->next == 0u ? info->events_at : e->next;
    rd_event(&r, &x);
    if (r.bad) {
        return CINT_REFUSED;
    }
    x.index = e->next == 0u ? 0u : e->index + 1u;
    x.next = r.at;
    *e = x;
    return CINT_OK;
}

cint_status cint_recording_arg(const cint_recording_info *info, const cint_recording_event *e, uint32_t k,
                               cint_tvalue *out)
{
    cint_state_rd r;
    cint_tvalue v;
    if (info == NULL || e == NULL || out == NULL || e->kind != CINT_EVENT_ENTRY || k >= e->arg_count) {
        return CINT_REFUSED;
    }
    memset(&v, 0, sizeof v);
    r = cint_state_rd_of(info->bytes, info->len);
    r.at = e->args_at;
    for (uint32_t i = 0u; i <= k; i++) {
        cint_state_rd_value(&r, &v, true);
    }
    if (r.bad) {
        return CINT_REFUSED;
    }
    *out = v;
    return CINT_OK;
}

cint_status cint_recording_module(const cint_recording_info *info, const cint_recording_event *e, uint32_t k,
                                  cint_code_module *out)
{
    cint_code_module m;
    cint_state_rd r;
    uint64_t n;
    if (info == NULL || out == NULL || (e != NULL && e->kind != CINT_EVENT_REVISION)) {
        return CINT_REFUSED;
    }
    memset(&m, 0, sizeof m);
    r = cint_state_rd_of(info->bytes, info->len);
    r.at = e != NULL ? (size_t)(e->data - info->bytes) : info->code_at;
    if (k >= cint_state_rd_u(&r, 4u)) {
        return CINT_REFUSED;
    }
    for (uint32_t i = 0u; i <= k && !r.bad; i++) {
        memset(&m, 0, sizeof m);
        rd_name(&r, &m.path, CINT_FAULT_MAX_PATH, true);
        n = cint_state_rd_u(&r, 8u);
        m.sir = cint_state_rd_take(&r, n);
        m.sir_len = (size_t)n;
        if (cint_state_rd_flag(&r)) {
            n = cint_state_rd_u(&r, 8u);
            m.source = cint_state_rd_take(&r, n);
            m.source_len = (size_t)n;
        }
    }
    if (r.bad) {
        return CINT_REFUSED;
    }
    *out = m;
    return CINT_OK;
}

/* ------------------------------------------------------------------------- */
/* Replay (SPEC-03 H-16, H-17).                                               */

typedef struct replay_run {
    cint_ctx *ctx;
    const cint_recording_info *rec;
    cint_recording_event last;  /* the entry event, then each effect and dispatch event matched */
    uint32_t left;              /* the entry's effect and dispatch events not matched yet */
    bool diverged;
    uint32_t what;              /* what did not match: CINT_REPLAY_EFFECT, _DISPATCH or _STATE */
    uint64_t at;                /* the event that did not match */
    bool echo;
    cint_output_fn output;      /* the context's own */
    void *output_user;
    cint_replay_report *rep;
    uint64_t entry;             /* the index of the entry event replayed */
    uint64_t stop;              /* cint_replay_config.stop */
    bool stopped;               /* at a dispatch boundary, the stop, whose state is kept */
    bool resource;
    uint8_t *kept;              /* that state's CINT-CKPT-1 */
    size_t kept_len;
} replay_run;

/* Each print statement of a replayed entry: compared with the next recorded effect, whose
 * status it returns; the bytes are delivered only with echo (SPEC-06 10.2). */
static cint_status replay_output(void *user, const uint8_t *bytes, size_t len)
{
    replay_run *x = (replay_run *)user;
    cint_recording_event e = x->last;
    if (x->diverged || x->stopped) {  /* past a stop at a dispatch boundary: neither compared nor echoed */
        return CINT_OK;
    }
    if (x->left == 0u || cint_recording_next(x->rec, &e) != CINT_OK) {
        x->diverged = true;
        x->what = CINT_REPLAY_EFFECT;
        x->at = x->last.index + 1u;  /* the outcome: one print too many */
        return CINT_OK;
    }
    x->what = CINT_REPLAY_EFFECT;
    if (e.kind != CINT_EVENT_EFFECT || e.ordinal != x->ctx->effects || e.site.module != x->ctx->print_site.module ||
        e.site.index != x->ctx->print_site.index || e.data_len != len || (len > 0u && memcmp(e.data, bytes, len) != 0)) {
        x->diverged = true;
        x->at = e.index;
        return CINT_OK;
    }
    x->last = e;
    x->left--;
    if (x->echo && x->output != NULL) {
        (void)x->output(x->output_user, bytes, len);
    }
    return e.status;
}

/* cint_rt_dispatch_end in a replayed entry: compared with the next recorded event, a `dispatch`
 * event, and a boundary of its own unless the entry's outcome follows it at its state. At the
 * stop boundary the state is kept, for cint_replay to restore once the entry has run. */
static void replay_dispatch(cint_ctx *ctx, int64_t number)
{
    replay_run *x = (replay_run *)ctx->boundary_user;
    cint_recording_event e = x->last, o;
    uint8_t hash[32];
    bool own, keep;
    if (x->diverged || x->stopped || x->resource) {
        return;
    }
    x->what = CINT_REPLAY_DISPATCH;
    if (x->left == 0u || cint_recording_next(x->rec, &e) != CINT_OK || e.kind != CINT_EVENT_DISPATCH ||
        e.ordinal != number) {
        x->diverged = true;
        x->at = x->left == 0u ? x->last.index + 1u : e.index;
        return;
    }
    o = e;
    own = cint_recording_next(x->rec, &o) == CINT_OK &&
          !(o.kind == CINT_EVENT_OUTCOME && o.has_hash && memcmp(o.hash, e.hash, 32u) == 0);
    keep = own && x->stop != 0u && x->rep->boundaries + 1u == x->stop;
    if (cint_state_boundary(ctx, hash, keep ? &x->kept : NULL, &x->kept_len) != CINT_OK) {
        x->resource = true;
        return;
    }
    x->last = e;
    x->left--;
    if (memcmp(hash, e.hash, 32u) != 0) {
        x->diverged = true;
        x->what = CINT_REPLAY_STATE;
        x->at = e.index;
    } else if (own) {
        if (!chain_next(x->rep->chain, x->entry, 1u, number, hash, NULL, 0u)) {
            x->resource = true;
            return;
        }
        x->rep->boundaries++;
        x->rep->event = e.index + 1u;
        x->stopped = keep;
    }
}

static void diverge(cint_replay_report *rep, uint32_t what, uint64_t event)
{
    rep->status = CINT_DIVERGED;
    rep->what = what;
    rep->event = event;
}

static const cint_observer_entry *find_entry(const cint_observer *obs, const cint_recording_event *e)
{
    for (uint32_t i = 0u; i < obs->entry_count; i++) {
        const cint_observer_entry *f = &obs->entries[i];
        const cint_module *m = f->module < obs->program->module_count ? obs->program->modules[f->module] : NULL;
        if (m != NULL && f->name_len == e->name.len && memcmp(f->name, e->name.bytes, e->name.len) == 0 &&
            m->path_len == e->module.len && memcmp(m->path, e->module.bytes, e->module.len) == 0) {
            return f;
        }
    }
    return NULL;
}

/* Replays the entry event *e, its effects and its outcome, leaving *e at the outcome. */
static cint_status replay_entry(replay_run *x, const cint_observer *obs, cint_main_fn main_fn,
                                cint_recording_event *e, cint_replay_report *rep)
{
    cint_ctx *ctx = x->ctx;
    const cint_observer_entry *f = e->form == 0u ? find_entry(obs, e) : NULL;
    cint_recording_event o = *e;
    const cint_error_set *set = NULL;
    uint64_t args[16], res = 0u, tag = 0u, at = e->index;
    uint32_t effects = 0u;
    int64_t fuel, used = 0, merged;
    uint8_t hash[32], *fault = NULL;
    size_t flen = 0u;
    cint_tvalue v;
    cint_status st;
    bool ok;
    memset(&v, 0, sizeof v);
    memset(hash, 0, sizeof hash);
    ok = e->arena == ctx->frame_arena_bytes &&
              (e->form == 0u ? f != NULL && f->types != NULL && f->types[1] == e->arg_count && e->arg_count <= 16u &&
                                   (f->types[0] == 0u || cint_state_width_of(f->types[0]) != 0u)
                             : main_fn != NULL && e->depth == ctx->config_depth);
    for (uint32_t i = 0u; ok && i < e->arg_count; i++) {
        ok = cint_recording_arg(x->rec, e, i, &v) == CINT_OK && v.bytes[0] == f->types[2u + i];
        args[i] = bits_of(&v);
    }
    while (cint_recording_next(x->rec, &o) == CINT_OK &&
           (o.kind == CINT_EVENT_EFFECT || o.kind == CINT_EVENT_DISPATCH)) {
        effects++;
    }
    if (!ok) {
        diverge(rep, CINT_REPLAY_ENTRY, e->index);
        return CINT_OK;
    }
    if (o.status == CINT_RESOURCE) {
        rep->status = CINT_UNREPLAYABLE;  /* it depends on the machine (A-6) */
        rep->event = e->index;
        return CINT_OK;
    }
    fuel = ran(o.status) ? o.allowance : e->fuel;  /* an interrupt lowered it (box 13 default BX13-09) */
    x->last = *e;
    x->left = effects;
    x->diverged = false;
    x->entry = e->index;
    st = f != NULL ? f->call(ctx, fuel, e->depth, args, &res) : main_fn(ctx, fuel);
    *e = o;
    if (x->resource || x->stopped) {  /* the stop was a dispatch boundary: its state comes back */
        st = x->resource ? CINT_RESOURCE : cint_ctx_restore(ctx, x->kept, x->kept_len);
        free(x->kept);
        x->kept = NULL;
        return st;
    }
    if (x->diverged || x->left > 0u) {
        cint_recording_event u = x->last;
        if (!x->diverged) {
            (void)cint_recording_next(x->rec, &u);
            x->what = u.kind == CINT_EVENT_DISPATCH ? CINT_REPLAY_DISPATCH : CINT_REPLAY_EFFECT;
        }
        diverge(rep, x->what, x->diverged ? x->at : x->last.index + 1u);
        return CINT_OK;
    }
    merged = x->last.kind == CINT_EVENT_DISPATCH && o.has_hash && memcmp(o.hash, x->last.hash, 32u) == 0
                 ? x->last.ordinal
                 : -1;
    if (st != o.status) {
        diverge(rep, CINT_REPLAY_STATUS, o.index);
        return CINT_OK;
    }
    if (st == CINT_FAULT && ((flen = cint_fault_encode(&ctx->fault, NULL, 0u)) == 0u ||
                             (fault = (uint8_t *)malloc(flen)) == NULL)) {
        return CINT_RESOURCE;
    }
    if (fault != NULL) {
        (void)cint_fault_encode(&ctx->fault, fault, flen);
    }
    if (cint_fuel_consumed(ctx, &used) != CINT_OK || cint_ctx_error(ctx, &set, &tag) != CINT_OK ||
        (ran(st) && cint_ctx_state_hash(ctx, hash) != CINT_OK)) {
        free(fault);
        return CINT_RESOURCE;
    }
    if (st == CINT_OK && f != NULL && f->types[0] != 0u && set == NULL) {
        cint_rt_tvalue_scalar(&v, f->types[0], res);
    }
    if ((fault != NULL) != (o.data != NULL) || flen != o.data_len || (flen > 0u && memcmp(fault, o.data, flen) != 0)) {
        diverge(rep, CINT_REPLAY_FAULT, o.index);
    } else if ((ran(st) ? used : 0) != o.consumed) {
        diverge(rep, CINT_REPLAY_FUEL, o.index);
    } else if ((st == CINT_OK && f != NULL && f->types[0] != 0u && set == NULL) != (o.has_result != 0u) ||
               (o.has_result && (v.len != o.result.len || memcmp(v.bytes, o.result.bytes, v.len) != 0))) {
        diverge(rep, CINT_REPLAY_RESULT, o.index);
    } else if ((st == CINT_OK && set != NULL) != (o.has_error != 0u) ||
               (o.has_error && (strlen(set->name) != o.error_set.len ||
                                memcmp(set->name, o.error_set.bytes, o.error_set.len) != 0 || tag != o.error_tag))) {
        diverge(rep, CINT_REPLAY_ERROR, o.index);
    } else if (ran(st) && memcmp(hash, o.hash, 32u) != 0) {
        diverge(rep, CINT_REPLAY_STATE, o.index);
    } else if ((ran(st) ? ctx->head.fuel_limit : fuel) != o.allowance) {
        diverge(rep, CINT_REPLAY_ALLOWANCE, o.index);
    } else if (ran(st)) {
        if (!chain_next(rep->chain, at, 0u, merged, hash, fault, flen)) {
            free(fault);
            return CINT_RESOURCE;
        }
        rep->boundaries++;
    }
    free(fault);
    return CINT_OK;
}

cint_status cint_replay(cint_ctx *ctx, const cint_recording_info *rec, const cint_replay_config *cfg,
                        cint_replay_report *out)
{
    const cint_observer *obs;
    const cint_state_table *table = NULL;
    cint_main_fn main_fn;
    cint_recording_event e;
    cint_replay_report rep;
    cint_checkpoint_info k;
    replay_run x;
    uint8_t hash[32], now[32];
    cint_status st;
    if (ctx == NULL || rec == NULL || rec->bytes == NULL || cfg == NULL || cfg->size != sizeof *cfg ||
        cfg->entries == NULL || out == NULL || ctx->output == rec_output || ctx->output == replay_output) {
        return CINT_REFUSED;
    }
    if (cint_checkpoint_decode(rec->initial, rec->initial_len, &k) != CINT_OK) {
        return CINT_REFUSED;
    }
    cint_sha256(k.state, k.state_len, hash);
    memset(now, 0, sizeof now);
    if ((st = cint_ctx_restore(ctx, rec->initial, rec->initial_len)) != CINT_OK ||
        (st = cint_ctx_state_hash(ctx, now)) != CINT_OK) {
        return st;
    }
    if (memcmp(hash, now, 32u) != 0) {
        return CINT_REFUSED;  /* counters above the recording's: not a context made for it */
    }
    memset(&rep, 0, sizeof rep);
    chain_start(rep.chain, hash);
    memset(&x, 0, sizeof x);
    x.ctx = ctx;
    x.rec = rec;
    x.echo = cfg->echo != 0u;
    x.output = ctx->output;
    x.output_user = ctx->output_user;
    x.rep = &rep;
    x.stop = cfg->stop;
    ctx->output = replay_output;
    ctx->output_user = &x;
    ctx->boundary = rec->density == 1u ? replay_dispatch : NULL;
    ctx->boundary_user = &x;
    obs = cfg->entries;
    main_fn = cfg->main_fn;
    memset(&e, 0, sizeof e);
    st = CINT_OK;
    while (st == CINT_OK && rep.status == CINT_OK && cint_recording_next(rec, &e) == CINT_OK) {
        uint64_t before = rep.boundaries;
        uint8_t *fault = NULL;
        size_t n = 0u;
        if (e.kind == CINT_EVENT_ENTRY) {
            st = replay_entry(&x, obs, main_fn, &e, &rep);
        } else if (e.kind == CINT_EVENT_CHECKPOINT) {
            if ((st = cint_ctx_state_hash(ctx, now)) == CINT_OK && memcmp(now, e.hash, 32u) != 0) {
                diverge(&rep, CINT_REPLAY_STATE, e.index);
            }
        } else if (e.kind == CINT_EVENT_CLEAR_FAULT) {
            if (ctx->head.faulted == 0u) {
                diverge(&rep, CINT_REPLAY_CLEAR, e.index);
            } else if ((n = cint_fault_encode(&ctx->fault, NULL, 0u)) == 0u || (fault = (uint8_t *)malloc(n)) == NULL) {
                st = CINT_RESOURCE;
            } else {
                (void)cint_fault_encode(&ctx->fault, fault, n);
                cint_sha256(fault, n, now);
                free(fault);
                if (memcmp(now, e.hash, 32u) != 0 || cint_ctx_clear_fault(ctx) != CINT_OK) {
                    diverge(&rep, CINT_REPLAY_CLEAR, e.index);
                }
            }
        } else if (e.kind == CINT_EVENT_RESTORE) {
            if ((st = cint_ctx_restore(ctx, e.data, e.data_len)) != CINT_OK || ctx->last_id + 1u != e.next_buffer ||
                ctx->last_arena + 1u != e.next_arena || ctx->last_pool + 1u != e.next_pool) {
                diverge(&rep, CINT_REPLAY_RESTORE, e.index);
                st = st == CINT_RESOURCE ? st : CINT_OK;
            }
        } else if (e.kind == CINT_EVENT_REVISION) {
            cint_main_fn next_main = NULL;
            const cint_observer *next = cfg->revision != NULL ? cfg->revision(cfg->user, &e, &next_main, &table) : NULL;
            if (next == NULL || next->program == NULL || next->program->revision == NULL ||
                memcmp(next->program->revision, e.revision, 32u) != 0 ||
                cint_ctx_change_revision(ctx, next->program, table) != CINT_OK) {
                diverge(&rep, CINT_REPLAY_REVISION, e.index);
            } else {
                obs = next;
                main_fn = next_main;
            }
        } else if (e.kind == CINT_EVENT_END && memcmp(e.hash, rep.chain, 32u) != 0) {
            diverge(&rep, CINT_REPLAY_CHAIN, e.index);
        }
        if (st == CINT_OK && rep.status == CINT_OK && x.stopped) {
            break;  /* at a dispatch boundary, which set rep.event */
        }
        if (st == CINT_OK && rep.status == CINT_OK) {
            rep.event = e.kind == CINT_EVENT_END ? e.index : e.index + 1u;
            if (cfg->stop != 0u && rep.boundaries == cfg->stop && rep.boundaries != before) {
                break;
            }
        }
    }
    ctx->output = x.output;
    ctx->output_user = x.output_user;
    ctx->boundary = NULL;
    ctx->boundary_user = NULL;
    if (st == CINT_OK) {
        *out = rep;
    }
    return st;
}

const char *cint_replay_what(uint32_t what)
{
    static const char *const names[] = {NULL,     "entry",  "effect", "status",    "fault",    "fuel",     "result",
                                        "error",  "state",  "allowance", "clear", "restore", "revision", "chain",
                                        "dispatch"};
    return what < sizeof names / sizeof names[0] ? names[what] : NULL;
}
/* Bytes as a quoted string: printable ASCII as it is, with `"` and the backslash escaped, and
 * every other byte as \xHH. */
static void t_quoted(cint_state_tx *t, const uint8_t *p, size_t n)
{
    static const char hex[] = "0123456789abcdef";
    cint_state_t_str(t, "\"");
    for (size_t i = 0u; i < n; i++) {
        char c[4] = {'\\', 'x', hex[p[i] >> 4], hex[p[i] & 15u]};
        if (p[i] == '"' || p[i] == '\\') {
            c[1] = (char)p[i];
            cint_state_t_put(t, c, 2u);
        } else if (p[i] >= 0x20u && p[i] < 0x7fu) {
            cint_state_t_put(t, p + i, 1u);
        } else {
            cint_state_t_put(t, c, 4u);
        }
    }
    cint_state_t_str(t, "\"");
}

static void t_code(cint_state_tx *t, const cint_recording_info *c, const cint_recording_event *e, uint32_t n)
{
    cint_code_module m;
    uint8_t hash[32];
    for (uint32_t k = 0u; k < n && cint_recording_module(c, e, k, &m) == CINT_OK; k++) {
        cint_state_t_str(t, "module ");
        cint_state_t_put(t, m.path.bytes, m.path.len);
        cint_state_t_str(t, " sir ");
        cint_state_t_u64(t, m.sir_len);
        cint_sha256(m.sir, m.sir_len, hash);
        cint_state_t_str(t, " sha256 ");
        cint_state_t_hex(t, hash, 32u);
        if (m.source != NULL) {
            cint_state_t_str(t, " source ");
            cint_state_t_u64(t, m.source_len);
        } else {
            cint_state_t_str(t, " source absent");
        }
        cint_state_t_str(t, "\n");
    }
}

size_t cint_recording_render(const cint_recording_info *c, char *out, size_t cap)
{
    static const char *const statuses[] = {"ok", "fault", "refused", "busy", "faulted", "resource"};
    cint_state_tx t;
    cint_checkpoint_info k;
    cint_recording_event e;
    cint_tvalue v;
    uint8_t hash[32];
    t.out = out;
    t.cap = out != NULL ? cap : 0u;
    t.len = 0u;
    if (c == NULL || c->bytes == NULL || cint_checkpoint_decode(c->initial, c->initial_len, &k) != CINT_OK) {
        return 0u;
    }
    cint_state_t_str(&t, c->is_case ? "case cint-core-1/case/v1\ncontract "
                                    : "recording cint-core-1/recording/v1\ncontract ");
    cint_state_t_put(&t, c->contract.bytes, c->contract.len);
    cint_state_t_str(&t, "\nrevision ");
    cint_state_t_hex(&t, c->revision, 32u);
    cint_state_t_str(&t, "\nsource-map ");
    if (c->has_map) {
        cint_state_t_hex(&t, c->map, 32u);
    } else {
        cint_state_t_str(&t, "absent");
    }
    cint_state_t_str(&t, c->mode == CINT_RECORD_FULL ? "\nmode full\nstate-hash every entry\n"
                                          : "\nmode digest\nstate-hash every entry\n");
    cint_state_t_str(&t, c->density == 1u ? "density dispatch\n" : "density entry\n");
    t_code(&t, c, NULL, c->module_count);
    cint_sha256(k.state, k.state_len, hash);
    cint_state_t_str(&t, "initial-state ");
    cint_state_t_hex(&t, hash, 32u);
    cint_state_t_str(&t, "\n");
    memset(&e, 0, sizeof e);
    while (cint_recording_next(c, &e) == CINT_OK) {
        cint_state_t_str(&t, "event ");
        cint_state_t_u64(&t, e.index);
        if (e.kind == CINT_EVENT_ENTRY) {
            cint_state_t_str(&t, " entry ");
            cint_state_t_u64(&t, e.sequence);
            if (e.form == 1u) {
                cint_state_t_str(&t, " program");
            } else {
                cint_state_t_str(&t, " ");
                cint_state_t_put(&t, e.module.bytes, e.module.len);
                cint_state_t_str(&t, " ");
                cint_state_t_put(&t, e.name.bytes, e.name.len);
            }
            for (uint32_t i = 0u; i < e.arg_count && cint_recording_arg(c, &e, i, &v) == CINT_OK; i++) {
                cint_state_t_str(&t, i == 0u ? " (" : ", ");
                cint_state_t_value(&t, "", &v);
                t.len--;  /* t_value ends the line */
            }
            cint_state_t_str(&t, e.arg_count > 0u ? ") fuel " : " fuel ");
            cint_state_t_i64(&t, e.fuel);
            cint_state_t_str(&t, " depth ");
            cint_state_t_i64(&t, e.depth);
            cint_state_t_str(&t, " frame-arena ");
            cint_state_t_i64(&t, e.arena);
        } else if (e.kind == CINT_EVENT_EFFECT) {
            cint_state_t_str(&t, " effect ");
            cint_state_t_i64(&t, e.ordinal);
            cint_state_t_str(&t, " site ");
            cint_state_t_u64(&t, e.site.module);
            cint_state_t_str(&t, ".");
            cint_state_t_u64(&t, e.site.index);
            cint_state_t_str(&t, " print status ");
            cint_state_t_i64(&t, e.status);
            cint_state_t_str(&t, " ");
            t_quoted(&t, e.data, e.data_len);
        } else if (e.kind == CINT_EVENT_OUTCOME) {
            cint_state_t_str(&t, " outcome ");
            cint_state_t_str(&t, statuses[e.status]);
            cint_state_t_str(&t, " fuel ");
            cint_state_t_i64(&t, e.consumed);
            cint_state_t_str(&t, " allowance ");
            cint_state_t_i64(&t, e.allowance);
            if (e.has_result) {
                cint_state_t_value(&t, " result ", &e.result);
                t.len--;
            }
            if (e.has_error) {
                cint_state_t_str(&t, " error ");
                cint_state_t_put(&t, e.error_set.bytes, e.error_set.len);
                cint_state_t_str(&t, " ");
                cint_state_t_u64(&t, e.error_tag);
            }
            if (e.has_hash) {
                cint_state_t_str(&t, " state ");
                cint_state_t_hex(&t, e.hash, 32u);
            }
            if (e.data != NULL) {
                cint_state_t_str(&t, "\n");
                cint_state_t_fault(&t, e.data, e.data_len);
                t.len--;
            }
        } else if (e.kind == CINT_EVENT_CLEAR_FAULT || e.kind == CINT_EVENT_CHECKPOINT) {
            cint_state_t_str(&t, e.kind == CINT_EVENT_CHECKPOINT ? " checkpoint state " : " clear-fault record ");
            cint_state_t_hex(&t, e.hash, 32u);
        } else if (e.kind == CINT_EVENT_RESTORE) {
            cint_state_t_str(&t, " restore state ");
            cint_state_t_hex(&t, e.hash, 32u);
            cint_state_t_str(&t, " next-buffer ");
            cint_state_t_u64(&t, e.next_buffer);
            cint_state_t_str(&t, " next-arena ");
            cint_state_t_u64(&t, e.next_arena);
            cint_state_t_str(&t, " next-pool ");
            cint_state_t_u64(&t, e.next_pool);
        } else if (e.kind == CINT_EVENT_REVISION) {
            cint_state_t_str(&t, " revision ");
            cint_state_t_hex(&t, e.revision, 32u);
            cint_state_t_str(&t, "\n");
            t_code(&t, c, &e, e.module_count);
            t.len--;
        } else if (e.kind == CINT_EVENT_DISPATCH) {
            cint_state_t_str(&t, " dispatch ");
            cint_state_t_i64(&t, e.ordinal);
            cint_state_t_str(&t, " state ");
            cint_state_t_hex(&t, e.hash, 32u);
        } else {
            cint_state_t_str(&t, e.reason == 0u ? " end host chain " : " end resource chain ");
            cint_state_t_hex(&t, e.hash, 32u);
            cint_state_t_str(&t, " events ");
            cint_state_t_u64(&t, e.sequence);
        }
        cint_state_t_str(&t, "\n");
    }
    return t.len;
}
