/* wb_fault.c: fault records and fault reports of the workbench (box 14, SPEC-06 8).
 *
 * The decoder reads the canonical record, `cint-core-1/fault/v2`, or `/v3` for a run-time
 * record of dot over I1024 elements (SPEC-01 IM-149), with the bounds of its kind (IM-108) and the checks of the `cint` command's decoder
 * (cli/cint_main.c, decode_fault). The human form prints the field lines that `cint run`
 * prints (decision 26; box 14 ruling BX14-05 A extends them to every tool), byte for byte for
 * the same record, then the lines that are not canonical (SPEC-06 8.1; default BX14-13). The
 * JSON form is CINT-FAULT-1 with those additions under `noncanonical`.
 */
#include <stdio.h>
#include <string.h>

#include "wb.h"
#include "wb_array.h"

const char *const WB_CODE_NAMES[14] = {"", "E_OVERFLOW", "E_DIV_ZERO", "E_BOUNDS", "E_SHAPE", "E_SHIFT",
                                       "E_NARROW", "E_ALIAS", "E_STALE_HANDLE", "E_FUEL", "E_UNSUPPORTED",
                                       "E_DOMAIN", "E_DEPTH", "E_ASSERT"};
/* The headline of each code, as the `cint` command words it. */
static const char *const CODE_TEXT[14] = {"", "the exact result is outside the type's range", "division by zero",
                                          "index out of bounds", "shapes do not match", "shift count outside 0..w-1",
                                          "the value does not fit the destination type", "views overlap",
                                          "stale handle", "fuel allowance exhausted",
                                          "not supported by this backend", "operand outside the operation's domain",
                                          "call depth limit exceeded", "assertion failed"};
/* How a CPU backend, the only kind a session runs, finds a dispatch's canonical fault (SPEC-06 8.3) */
#define FOUND "; the dispatch ran in F-3 order and stopped at its first fault (SPEC-02 B-5)"

/* ------------------------------------------------------------------------- */
/* Decoding.                                                                  */

typedef struct rd {
    const uint8_t *p;
    size_t n, at;
    int bad;
} rd;

static uint64_t rd_u(rd *r, unsigned k)
{
    uint64_t v = 0u;
    if (r->bad || r->n - r->at < k) {
        r->bad = 1;
        return 0u;
    }
    for (unsigned i = 0; i < k; i++) v |= (uint64_t)r->p[r->at + i] << (8u * i);
    r->at += k;
    return v;
}

static int rd_flag(rd *r)
{
    uint64_t v = rd_u(r, 1u);
    if (v > 1u) r->bad = 1;
    return (int)v;
}

static const uint8_t *rd_take(rd *r, uint64_t k)
{
    const uint8_t *p = r->p + r->at;
    if (r->bad || (uint64_t)(r->n - r->at) < k) {
        r->bad = 1;
        return NULL;
    }
    r->at += (size_t)k;
    return p;
}

/* A tagged value: Bool, I8 to I1024, U8 to U512 (IM-146) or Z. A `Z` is minimal: no redundant
 * sign byte, and zero is one byte. */
static void rd_value(rd *r, wb_tval *v, unsigned zmax)
{
    size_t start = r->at;
    unsigned tag = (unsigned)rd_u(r, 1u);
    if (tag == CINT_TAG_Z) {
        uint64_t n = rd_u(r, 4u);
        const uint8_t *p;
        if (n > zmax) r->bad = 1;
        p = rd_take(r, n);
        if (p != NULL && ((n == 1u && p[0] == 0u) ||
                          (n >= 2u && ((p[n - 1u] == 0u && p[n - 2u] < 0x80u) ||
                                       (p[n - 1u] == 0xffu && p[n - 2u] >= 0x80u))))) {
            r->bad = 1;
        }
    } else if (tag == CINT_TAG_BOOL || (tag >= CINT_TAG_I8 && tag <= CINT_TAG_I8 + 7u) ||
               (tag >= CINT_TAG_U8 && tag <= CINT_TAG_U8 + 6u)) {
        const uint8_t *p = rd_take(r, tag == CINT_TAG_BOOL ? 1u : 1u << ((tag & 0x0fu) - 1u));
        if (p != NULL && tag == CINT_TAG_BOOL && *p > 1u) r->bad = 1;
    } else {
        r->bad = 1;
    }
    if (!r->bad) {
        v->len = (uint16_t)(r->at - start);
        memcpy(v->bytes, r->p + start, r->at - start);
    }
}

static void rd_pos(rd *r, wb_pos *p)
{
    p->path_len = (uint32_t)rd_u(r, 4u);
    if (p->path_len == 0u || p->path_len > CINT_FAULT_MAX_PATH) r->bad = 1;
    p->path = (const char *)rd_take(r, p->path_len);
    if (p->path == NULL || !wb_utf8(p->path, p->path_len)) r->bad = 1;
    p->line = (uint32_t)rd_u(r, 4u);
    p->column = (uint32_t)rd_u(r, 4u);
    if (p->line == 0u || p->column == 0u) r->bad = 1;
}

int wb_fault_decode(const uint8_t *data, size_t n, wb_fault *f, int kind)
{
    static const char v2[] = "cint-core-1/fault/v2", v3[] = "cint-core-1/fault/v3";
    unsigned version, zmax, zmax_operand, zmax_exact;
    rd r = {data, n, 0u, 0};
    uint64_t len, u;
    const uint8_t *s;
    memset(f, 0, sizeof *f);
    len = rd_u(&r, 4u);
    s = rd_take(&r, len);
    if (s == NULL || len != sizeof v2 - 1u) return -1;
    /* Version 3 only for a run-time record of dot.checked.i1024.<R> (IM-148, IM-149). */
    version = memcmp(s, v2, sizeof v2 - 1u) == 0 ? 2u : memcmp(s, v3, sizeof v3 - 1u) == 0 ? 3u : 0u;
    if (version == 0u || (version == 3u && kind == WB_RECORD_COMPILE)) return -1;
    zmax = version == 3u ? CINT_Z_MAX_BYTES : CINT_Z_V2_MAX_BYTES;
    zmax_operand = kind == WB_RECORD_COMPILE ? 513u : zmax;
    zmax_exact = kind == WB_RECORD_COMPILE ? 520u : zmax;
    f->code = (unsigned)rd_u(&r, 2u);
    len = rd_u(&r, 4u);
    s = rd_take(&r, len);
    if (f->code == 0u || f->code > 13u || s == NULL || len == 0u || len > CINT_FAULT_MAX_OPERATION) return -1;
    for (size_t i = 0; i < (size_t)len; i++) {
        unsigned c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_')) return -1;
    }
    memcpy(f->op, s, (size_t)len);
    if (kind != WB_RECORD_COMPILE && cint_fault_version(f->op, (size_t)len) != version) return -1;
    f->nop = (uint32_t)rd_u(&r, 4u);
    if (f->nop > CINT_FAULT_MAX_OPERANDS) return -1;
    for (uint32_t i = 0; i < f->nop; i++) rd_value(&r, &f->opnd[i], zmax_operand);
    if ((f->has_exact = rd_flag(&r)) != 0) {
        rd_value(&r, &f->exact, zmax_exact);
        if (f->exact.bytes[0] != CINT_TAG_Z) r.bad = 1;
    }
    if ((f->has_limit = rd_flag(&r)) != 0) rd_value(&r, &f->limit, zmax);
    rd_pos(&r, &f->pos);
    if ((f->has_rev = rd_flag(&r)) != 0 && (s = rd_take(&r, 32u)) != NULL) memcpy(f->rev, s, 32u);
    if ((f->has_map = rd_flag(&r)) != 0 && (s = rd_take(&r, 32u)) != NULL) memcpy(f->map, s, 32u);
    if ((f->has_addr = rd_flag(&r)) != 0) {
        f->kernel_len = (uint32_t)rd_u(&r, 4u);
        f->kernel = (const char *)rd_take(&r, f->kernel_len);
        if (f->kernel == NULL || !wb_utf8(f->kernel, f->kernel_len)) r.bad = 1;
        u = rd_u(&r, 8u);
        memcpy(&f->dispatch, &u, sizeof u);
        f->phase = (uint32_t)rd_u(&r, 1u);
        if (f->phase > 2u) r.bad = 1;
        if ((f->has_item = rd_flag(&r)) != 0) {
            if (f->phase != 1u) {
                r.bad = 1;   /* a work-item index only in the work-item phase */
            }
            u = rd_u(&r, 8u);
            memcpy(&f->item, &u, sizeof u);
            u = rd_u(&r, 8u);
            memcpy(&f->step, &u, sizeof u);
        }
    }
    f->nstack = (uint32_t)rd_u(&r, 4u);
    if (f->nstack > CINT_RT_MAX_DEPTH) return -1;
    for (uint32_t i = 0; i < f->nstack; i++) rd_pos(&r, &f->stack[i]);
    return r.bad || r.at != r.n ? -1 : 0;
}

size_t wb_tval_text(const wb_tval *v, char *text, size_t cap, int with_type)
{
    unsigned tag = v->bytes[0];
    wb_buf b = {0};
    wb_num x;
    size_t n;
    if (cap == 0u) return 0u;
    if (tag == CINT_TAG_BOOL) {
        wb_s(&b, with_type ? "Bool " : "");
        wb_s(&b, v->bytes[1] ? "true" : "false");
    } else if (tag == CINT_TAG_Z) {
        wb_s(&b, with_type ? "Z " : "");
        (void)wb_num_from_le(&x, v->bytes + 5u, (size_t)v->len - 5u, 1);
        wb_num_dec(&b, &x, 0);
    } else if (tag == 0x31u) {   /* Q<i>.<f>: the storage tag, f as U16, the raw integer (IM-146) */
        unsigned w = 8u << (v->bytes[1] - CINT_TAG_I8), f = v->bytes[2] | (unsigned)v->bytes[3] << 8;
        if (with_type) wb_f(&b, "Q%u.%u ", w - f, f);
        wb_s(&b, "raw ");
        (void)wb_num_from_le(&x, v->bytes + 4u, (size_t)v->len - 4u, 1);
        wb_num_dec(&b, &x, 0);
    } else {
        if (with_type) wb_f(&b, "%c%u ", tag < 0x20u ? 'I' : 'U', 8u << ((tag & 15u) - 1u));
        (void)wb_num_from_le(&x, v->bytes + 1u, (size_t)v->len - 1u, tag < 0x20u);
        wb_num_dec(&b, &x, 0);
    }
    n = b.failed || b.p == NULL ? 0u : b.n < cap ? b.n : cap - 1u;
    if (n > 0u) memcpy(text, b.p, n);
    text[n] = '\0';
    wb_free(&b);
    return n;
}

/* ------------------------------------------------------------------------- */
/* The human form.                                                            */

/* The source excerpt of SPEC-04 LS-276, as `cint run` prints it: the line, then a caret under
 * the column (tabs kept, so it aligns at any tab width; columns count scalar values), then
 * the label. Nothing when the module is not loaded or has no such line. */
void wb_excerpt(wb_buf *b, const wb_pos *p, const char *label, wb_source_fn source, void *user)
{
    size_t len = 0u;
    const uint8_t *src = source != NULL ? source(user, p->path, p->path_len, &len) : NULL;
    const char *s, *end, *e;
    uint32_t k = 1u, col = 1u;
    int w = p->line >= 100000u ? 6 : p->line >= 10000u ? 5 : p->line >= 1000u ? 4 : p->line >= 100u ? 3 : 2;
    if (src == NULL || p->line == 0u) return;
    s = (const char *)src;
    end = s + len;
    while (s < end && k < p->line) {
        if (*s++ == '\n') k++;
    }
    if (k != p->line) return;
    for (e = s; e < end && *e != '\n' && *e != '\r'; e++) {
    }
    wb_f(b, "%*s |\n%*u | ", w, "", w, (unsigned)p->line);
    wb_put(b, s, (size_t)(e - s));
    wb_f(b, "\n%*s | ", w, "");
    for (; s < e && col < p->column; s++) {
        if (((unsigned char)*s & 0xc0u) == 0x80u) {
            continue;   /* a continuation byte */
        }
        wb_s(b, *s == '\t' ? "\t" : " ");
        col++;
    }
    wb_f(b, "^%s%s\n%*s |\n", label[0] != '\0' ? " " : "", label, w, "");
}

static void put_pos(wb_buf *b, const wb_pos *p)
{
    wb_put(b, p->path, p->path_len);
    wb_f(b, ":%u:%u", (unsigned)p->line, (unsigned)p->column);
}

/* The operation, operands, descriptors, exact, and limit lines. */
static void put_fields(wb_buf *b, const wb_fault *f, const wb_expect *e)
{
    char text[1400];
    wb_f(b, "   = operation: %s\n   = operand-count: %u\n", f->op, (unsigned)f->nop);
    for (uint32_t i = 0; i < f->nop; i++) {
        (void)wb_tval_text(&f->opnd[i], text, sizeof text, 1);
        wb_f(b, "   = operand: %s\n", text);
    }
    for (uint32_t i = 0; e != NULL && i < e->ndesc; i++)
        wb_f(b, "   = descriptor: %.*s\n", (int)e->desc_len[i], e->desc[i]);
    for (int k = 0; k < 2; k++) {
        const wb_tval *v = k == 0 ? (f->has_exact ? &f->exact : NULL) : (f->has_limit ? &f->limit : NULL);
        memcpy(text, "none", 5u);
        if (v != NULL) (void)wb_tval_text(v, text, sizeof text, k);
        wb_f(b, k == 0 ? "   = exact:   %s\n" : "   = limit:   %s\n", text);
    }
}

static void put_digest(wb_buf *b, const char *label, int present, const uint8_t *d, int self)
{
    wb_s(b, label);
    if (present) wb_hex(b, d, 32u);
    else wb_s(b, self ? "self" : "none");
}

/* The index tuple of f's work-item over r's domain, row-major with the last extent innermost
 * (BX14-13): its rank, or 0 when there is no work-item or domain, or the item is outside it. */
static uint32_t tuple_of(const wb_fault *f, const wb_report *r, int64_t t[4])
{
    int64_t w = f->has_item ? f->item : -1;
    uint32_t n = r != NULL && w >= 0 && r->rank <= 4u ? r->rank : 0u;
    for (uint32_t k = n; k-- > 0u; w /= r->extents[k]) {
        if (r->extents[k] <= 0) return 0u;
        t[k] = w % r->extents[k];
    }
    return w == 0 ? n : 0u;
}

/* The report of a record, or with e of an outcome parsed from `.expect` text: there the revision
 * and the source map are `self`, the descriptors follow the operands, a format 1 file has no
 * stack positions, and the phase is named among the lines that are not canonical. */
static void fault_text(wb_buf *b, const wb_fault *f, const wb_report *r, const wb_expect *e, wb_source_fn source,
                       void *user)
{
    static const char *const phases[3] = {"entry", "work-item", "epilogue"};
    size_t len = 0u;
    uint32_t shown = e != NULL && e->format < 2u ? 0u : f->nstack;
    wb_f(b, "fault[%s]: %s\n  --> ", WB_CODE_NAMES[f->code], CODE_TEXT[f->code]);
    put_pos(b, &f->pos);
    wb_s(b, "\n");
    wb_excerpt(b, &f->pos, f->op, source, user);
    put_fields(b, f, e);
    put_digest(b, "   = revision: ", f->has_rev, f->rev, e != NULL && e->self_rev);
    put_digest(b, "\n   = source-map: ", f->has_map, f->map, e != NULL && e->self_map);
    wb_s(b, "\n   = address: ");
    if (f->has_addr) {
        wb_put(b, f->kernel, f->kernel_len);
        wb_f(b, ", dispatch %lld, phase %u", (long long)f->dispatch, (unsigned)f->phase);
        if (f->has_item) wb_f(b, ", work-item %lld, step %lld", (long long)f->item, (long long)f->step);
        else wb_s(b, ", work-item none, step none");
    } else {
        wb_s(b, "none");
    }
    wb_f(b, "\n   = stack-depth: %u\n", (unsigned)f->nstack);
    for (uint32_t i = 0; i < shown; i++) {
        wb_s(b, "   = called from: ");
        put_pos(b, &f->stack[i]);
        wb_s(b, "\n");
    }
    if (r == NULL) return;
    if (r->fuel >= 0) wb_f(b, "   = fuel-consumed: %lld (not part of the fault record)\n", (long long)r->fuel);
    if (r->state != NULL) wb_f(b, "   = state: %s\n", r->state);
    /* Not canonical (SPEC-06 8.1; BX14-13): the phase of a kernel fault by name, the source of
     * each stack entry, outermost first, the work-item's index tuple, then the workbench
     * position, the site, and the backend. */
    int phase = e != NULL && f->has_addr && f->kernel_len > 0u;
    int64_t t[4] = {0, 0, 0, 0};
    uint32_t nt = tuple_of(f, r, t);
    if (shown == 0u && nt == 0u && r->position == NULL && r->site == NULL && r->backend == NULL && !phase) return;
    wb_s(b, "   = noncanonical: not part of the fault record\n");
    if (phase) wb_f(b, "   = phase: %s (SPEC-02 F-3)\n", phases[f->phase]);
    for (uint32_t i = 0; i < shown; i++) {
        int loaded = source != NULL && source(user, f->stack[i].path, f->stack[i].path_len, &len) != NULL;
        wb_s(b, "   = stack-source: ");
        put_pos(b, &f->stack[i]);
        wb_s(b, loaded ? "\n" : " (source not loaded)\n");
        wb_excerpt(b, &f->stack[i], "", source, user);
    }
    if (nt != 0u) wb_s(b, "   = work-item-tuple: [");
    for (uint32_t k = 0; k < nt; k++) wb_f(b, "%s%lld", k > 0u ? ", " : "", (long long)t[k]);
    if (nt != 0u) wb_s(b, "]\n");
    if (r->position != NULL) wb_f(b, "   = workbench-position: %s\n", r->position);
    if (r->site != NULL) wb_f(b, "   = site: %s\n", r->site);
    if (r->backend != NULL) wb_f(b, "   = backend: %s%s\n", r->backend, f->has_addr ? FOUND : "");
}

void wb_fault_text(wb_buf *b, const wb_fault *f, const wb_report *r, wb_source_fn source, void *user)
{
    fault_text(b, f, r, NULL, source, user);
}

/* ------------------------------------------------------------------------- */
/* The JSON form (SPEC-06 8.4 and 15, SPEC-04 LS-289): keys in byte order.    */

void wb_tval_json(wb_buf *b, const wb_tval *v)
{
    char text[1400];
    size_t n = wb_tval_text(v, text, sizeof text, 1);
    char *sp = n > 0u ? strchr(text, ' ') : NULL;
    if (sp == NULL) {
        wb_s(b, "null");
        return;
    }
    *sp = '\0';
    wb_s(b, "{\"t\":");
    wb_jstr(b, text, strlen(text));
    wb_s(b, ",\"v\":");
    wb_jstr(b, sp + 1, strlen(sp + 1));
    wb_s(b, "}");
}

static void j_pos(wb_buf *b, const wb_pos *p)
{
    wb_f(b, "{\"column\":%u,\"file\":", (unsigned)p->column);
    wb_jstr(b, p->path, p->path_len);
    wb_f(b, ",\"line\":%u}", (unsigned)p->line);
}

static void j_digest(wb_buf *b, int present, const uint8_t d[32])
{
    if (present) {
        wb_s(b, "\"");
        wb_hex(b, d, 32u);
        wb_s(b, "\"");
    } else {
        wb_s(b, "null");
    }
}

void wb_fault_json(wb_buf *b, const wb_fault *f, const wb_report *r)
{
    const char *sep = "";
    int64_t t[4] = {0, 0, 0, 0};
    uint32_t nt = tuple_of(f, r, t);
    wb_s(b, "{\"address\":");
    if (f->has_addr) {
        wb_f(b, "{\"dispatch\":\"%lld\",\"kernel\":", (long long)f->dispatch);
        wb_jstr(b, f->kernel, f->kernel_len);
        wb_f(b, ",\"phase\":%u,\"step\":", (unsigned)f->phase);
        if (f->has_item) wb_f(b, "\"%lld\",\"work_item\":\"%lld\"}", (long long)f->step, (long long)f->item);
        else wb_s(b, "null,\"work_item\":null}");
    } else {
        wb_s(b, "null");
    }
    wb_f(b, ",\"code\":\"%s\",\"column\":%u,\"exact\":", WB_CODE_NAMES[f->code], (unsigned)f->pos.column);
    if (f->has_exact) wb_tval_json(b, &f->exact);
    else wb_s(b, "null");
    wb_s(b, ",\"file\":");
    wb_jstr(b, f->pos.path, f->pos.path_len);
    wb_s(b, ",\"limit\":");
    if (f->has_limit) wb_tval_json(b, &f->limit);
    else wb_s(b, "null");
    wb_f(b, ",\"line\":%u", (unsigned)f->pos.line);
    if (r != NULL && (r->backend != NULL || r->fuel >= 0 || r->site != NULL || nt != 0u || r->position != NULL)) {
        wb_s(b, ",\"noncanonical\":{");
        if (r->backend != NULL) {
            char be[256];
            (void)snprintf(be, sizeof be, "%s%s", r->backend, f->has_addr ? FOUND : "");
            wb_s(b, "\"backend\":");
            wb_jstr(b, be, strlen(be));
            sep = ",";
        }
        if (r->fuel >= 0) {
            wb_f(b, "%s\"fuel_consumed\":\"%lld\"", sep, (long long)r->fuel);
            sep = ",";
        }
        if (r->site != NULL) {
            wb_f(b, "%s\"site\":", sep);
            wb_jstr(b, r->site, strlen(r->site));
            sep = ",";
        }
        if (nt != 0u) {
            wb_f(b, "%s\"work_item_tuple\":[", sep);
            for (uint32_t k = 0; k < nt; k++) wb_f(b, "%s\"%lld\"", k > 0u ? "," : "", (long long)t[k]);
            wb_s(b, "]");
            sep = ",";
        }
        if (r->position != NULL) {
            wb_f(b, "%s\"workbench_position\":", sep);
            wb_jstr(b, r->position, strlen(r->position));
        }
        wb_s(b, "}");
    }
    wb_s(b, ",\"operands\":[");
    for (uint32_t i = 0; i < f->nop; i++) {
        wb_s(b, i > 0u ? "," : "");
        wb_tval_json(b, &f->opnd[i]);
    }
    wb_s(b, "],\"operation\":");
    wb_jstr(b, f->op, strlen(f->op));
    wb_s(b, ",\"revision\":");
    j_digest(b, f->has_rev, f->rev);
    wb_s(b, ",\"schema\":\"CINT-FAULT-1\",\"severity\":\"fault\",\"source_map\":");
    j_digest(b, f->has_map, f->map);
    wb_s(b, ",\"stack\":[");
    for (uint32_t i = 0; i < f->nstack; i++) {
        wb_s(b, i > 0u ? "," : "");
        j_pos(b, &f->stack[i]);
    }
    wb_s(b, "]}");
}

/* ------------------------------------------------------------------------- */
/* Outcomes from `.expect` text (SPEC-09 CONF-11; box 14 unit 4a).             */

/* A `count` of CONF-11 (at most 18 digits), or -1. */
static int64_t count_of(const char *s, size_t n)
{
    int64_t v = 0;
    if (n == 0u || n > 18u || (s[0] == '0' && n > 1u)) return -1;
    for (size_t k = 0; k < n; k++) {
        if (s[k] < '0' || s[k] > '9') return -1;
        v = v * 10 + (s[k] - '0');
    }
    return v;
}

/* x as k bytes of two's complement, least significant first. Returns 1 when it fits k bytes,
 * signed or unsigned. */
static int num_to_le(const wb_num *x, uint8_t *p, size_t k, int is_signed)
{
    unsigned carry = 1u;
    for (size_t i = 0; i < k; i++) {
        unsigned m = i / 4u < x->n ? (x->limb[i / 4u] >> (8u * (i % 4u))) & 0xffu : 0u;
        m = x->neg ? (~m & 0xffu) + carry : m;
        carry = m >> 8;
        p[i] = (uint8_t)m;
    }
    return wb_num_bits(x) <= 8u * k && (is_signed ? x->n == 0u || ((p[k - 1u] & 0x80u) != 0u) == (x->neg != 0)
                                                  : !x->neg);
}

/* A typed value of CONF-11 (`I64 -5`, `Bool true`, `Z 9`, `Q16.16 raw 98304`), or with exact a
 * bare `int`, which is a Z, as SPEC-01 IM-149 tags it: a Z minimal, with its length first.
 * Returns 0, or -1. */
static int tval_parse(const char *s, size_t n, wb_tval *v, int exact)
{
    /* the tags of IM-146: I8 to I1024 are 11 to 18, U8 to U512 are 21 to 27 (U128 to U512 in format 5) */
    static const char *const names[] = {"I8", "I16", "I32", "I64", "I128", "I256", "I512", "I1024",
                                        "U8", "U16", "U32", "U64", "U128", "U256", "U512"};
    const char *sp = exact ? NULL : memchr(s, ' ', n), *d = sp != NULL ? sp + 1 : s;
    size_t tl = sp != NULL ? (size_t)(sp - s) : 0u, dl = n - (size_t)(d - s), k = 0;
    int64_t qi = 0, qf = 0;
    wb_num x, digit;
    memset(v, 0, sizeof *v);
    if (sp == NULL && !exact) return -1;
    if (tl == 4u && memcmp(s, "Bool", 4) == 0) {
        v->bytes[0] = CINT_TAG_BOOL;
        v->bytes[1] = dl == 4u && memcmp(d, "true", 4) == 0;
        v->len = 2u;
        return v->bytes[1] || (dl == 5u && memcmp(d, "false", 5) == 0) ? 0 : -1;
    }
    if (tl >= 4u && s[0] == 'Q') {   /* format 5 (rule 14): `Q<i>.<f> raw <n>`, i + f of IM-54 */
        const char *dot = memchr(s, '.', tl);
        qi = dot != NULL ? count_of(s + 1, (size_t)(dot - s - 1)) : -1;
        qf = dot != NULL ? count_of(dot + 1, (size_t)(s + tl - dot - 1)) : -1;
        if (qi < 1 || qf < 0 || (qi + qf != 8 && qi + qf != 16 && qi + qf != 32 && qi + qf != 64) || dl < 5u ||
            memcmp(d, "raw ", 4) != 0) {
            return -1;
        }
        d += 4;
        dl -= 4u;
    }
    k = dl > 0u && d[0] == '-';
    if (k == dl || (d[k] == '0' && dl > k + 1u)) return -1;
    wb_num_zero(&x);
    for (size_t i = k; i < dl; i++) {
        wb_num_from_u64(&digit, (uint64_t)(d[i] - '0'));
        if (d[i] < '0' || d[i] > '9' || wb_num_mul_small(&x, 10u) != 0 || wb_num_add(&x, &digit) != 0) return -1;
    }
    x.neg = k == 1u && x.n > 0u;
    if (exact || (tl == 1u && s[0] == 'Z')) {
        if ((k = wb_num_bits(&x) / 8u + 1u) > 520u) return -1;
        (void)num_to_le(&x, v->bytes + 5u, k, 1);
        while (k >= 2u && ((v->bytes[4u + k] == 0u && v->bytes[3u + k] < 0x80u) ||
                           (v->bytes[4u + k] == 0xffu && v->bytes[3u + k] >= 0x80u))) {
            k--;
        }
        v->bytes[0] = CINT_TAG_Z;
        for (unsigned i = 0; i < 4u; i++) v->bytes[1u + i] = (uint8_t)(k >> (8u * i));
        v->len = (uint16_t)(5u + k);
        return 0;
    }
    if (qi > 0) {   /* tag 31: the storage tag, f as U16, then the raw storage integer (IM-146) */
        size_t w = (size_t)(qi + qf) / 8u;
        v->bytes[0] = 0x31u;
        v->bytes[1] = (uint8_t)(CINT_TAG_I8 + (w == 1u ? 0u : w == 2u ? 1u : w == 4u ? 2u : 3u));
        v->bytes[2] = (uint8_t)qf;
        v->bytes[3] = 0u;
        v->len = (uint16_t)(4u + w);
        return num_to_le(&x, v->bytes + 4u, w, 1) ? 0 : -1;
    }
    for (unsigned i = 0; i < 15u; i++) {
        if (strlen(names[i]) == tl && memcmp(s, names[i], tl) == 0) {
            unsigned j = i < 8u ? i : i - 8u;
            v->bytes[0] = (uint8_t)((i < 8u ? CINT_TAG_I8 : CINT_TAG_U8) + j);
            v->len = (uint16_t)(1u + (1u << j));
            return num_to_le(&x, v->bytes + 1u, (size_t)1 << j, i < 8u) ? 0 : -1;
        }
    }
    return -1;
}

/* A position of DIAG-01, `path:line:column`. */
static int pos_parse(const char *s, size_t n, wb_pos *p)
{
    size_t c2 = n, c1;
    while (c2 > 0u && s[c2 - 1u] != ':') c2--;
    for (c1 = c2 > 1u ? c2 - 1u : 0u; c1 > 0u && s[c1 - 1u] != ':'; c1--) {
    }
    int64_t line = c1 > 1u ? count_of(s + c1, c2 - c1 - 1u) : -1, col = count_of(s + c2, n - c2);
    p->path = s;
    p->path_len = c1 > 1u ? (uint32_t)(c1 - 1u) : 0u;
    p->line = (uint32_t)line;
    p->column = (uint32_t)col;
    return line > 0 && col > 0 && line <= UINT32_MAX && col <= UINT32_MAX ? 0 : -1;
}

/* The index of the word [s, s + n) in a list, or -1. */
static int word_of(const char *s, size_t n, const char *const *words, int count)
{
    for (int i = 0; i < count; i++) {
        if (strlen(words[i]) == n && memcmp(s, words[i], n) == 0) return i;
    }
    return -1;
}

/* One fault line, its key without `fault.`; a diagnostic's lines are those of its fault. The
 * stack lines counted so far are in *stacks. */
static int fault_line(wb_expect *e, const char *key, size_t k, const char *v, size_t n, uint32_t *stacks)
{
    static const char *const keys[] = {"code", "operation", "operand", "descriptor", "exact", "limit", "position",
                                       "revision", "source-map", "phase", "kernel", "address", "stack-depth", "stack"};
    static const char *const phases[] = {"entry", "work-item", "epilogue"};
    wb_fault *f = &e->fault;
    int none = n == 4u && memcmp(v, "none", 4) == 0, self = n == 4u && memcmp(v, "self", 4) == 0, c = 0;
    const char *s1 = memchr(v, ' ', n), *s2 = s1 != NULL ? memchr(s1 + 1, ' ', n - (size_t)(s1 + 1 - v)) : NULL;
    int64_t a = 0;
    switch (word_of(key, k, keys, 14)) {
    case 0:
        return (f->code = (unsigned)(word_of(v, n, WB_CODE_NAMES + 1, 13) + 1)) > 0u ? 0 : -1;
    case 1:
        for (size_t i = 0; i < n; i++) {
            if (!((v[i] >= 'a' && v[i] <= 'z') || (v[i] >= '0' && v[i] <= '9') || v[i] == '.' || v[i] == '_'))
                return -1;
        }
        memcpy(f->op, v, n <= CINT_FAULT_MAX_OPERATION ? n : 0u);
        return n > 0u && n <= CINT_FAULT_MAX_OPERATION ? 0 : -1;
    case 2:
        return f->nop < CINT_FAULT_MAX_OPERANDS ? tval_parse(v, n, &f->opnd[f->nop++], 0) : -1;
    case 3:
        if (e->ndesc >= WB_DESC_MAX) return -1;
        e->desc[e->ndesc] = v;
        e->desc_len[e->ndesc++] = (uint32_t)n;
        return 0;
    case 4:
        return none ? 0 : (f->has_exact = 1, tval_parse(v, n, &f->exact, 1));
    case 5:
        return none ? 0 : (f->has_limit = 1, tval_parse(v, n, &f->limit, 0));
    case 6:
        return pos_parse(v, n, &f->pos);
    case 7:
        return (e->self_rev = self) != 0 ? 0 : -1;
    case 8:
        return (e->self_map = self) != 0 ? 0 : -1;
    case 9:
        return (c = word_of(v, n, phases, 3)) >= 0 ? (f->phase = (uint32_t)c, 0) : -1;
    case 10:
        f->kernel = v;
        f->kernel_len = (uint32_t)n;
        return 0;
    case 11:   /* `none`, or `<dispatch> <work-item> <step>` with `none none` outside a work-item */
        if (none || s2 == NULL || (f->dispatch = count_of(v, (size_t)(s1 - v))) < 0) return none ? 0 : -1;
        f->has_addr = 1;
        f->kernel = f->kernel != NULL ? f->kernel : "";
        f->has_item = (size_t)(v + n - s1) != 10u || memcmp(s1, " none none", 10) != 0;
        f->item = count_of(s1 + 1, (size_t)(s2 - s1 - 1));
        f->step = count_of(s2 + 1, (size_t)(v + n - s2 - 1));
        return !f->has_item || (f->item >= 0 && f->step >= 0) ? 0 : -1;
    case 12:
        return (a = count_of(v, n)) >= 0 && a <= CINT_RT_MAX_DEPTH ? (f->nstack = (uint32_t)a, 0) : -1;
    case 13:
        return *stacks < CINT_RT_MAX_DEPTH ? pos_parse(v, n, &f->stack[(*stacks)++]) : -1;
    }
    return -1;
}

int wb_expect_parse(const char *text, size_t n, wb_expect *e, size_t *used, wb_buf *err)
{
    static const char *const outcomes[] = {"value", "fault", "error", "refused", "compile-error"};
    uint32_t line = 0, stacks = 0;
    size_t b = 0, e0 = 0;
    int cases = 0, bad = 0;
    memset(e, 0, sizeof *e);
    e->stdout_bytes = e->fuel = -1;
    e->outcome = -1;
    for (; b < n && !bad; b = e0 + 1u) {
        const char *s = text + b, *v, *sp;
        size_t len = 0, k = 0, vl = 0;
        for (e0 = b; e0 < n && text[e0] != '\n'; e0++) {
        }
        line++;
        len = e0 - b;
        sp = memchr(s, ' ', len);
        k = sp != NULL ? (size_t)(sp - s) : len;
        v = s + k + (sp != NULL);
        vl = len - (size_t)(v - s);
#define KEY(w) (k == sizeof w - 1u && memcmp(s, w, k) == 0)
        if (KEY("case") && cases++ > 0) {
            break;
        } else if (KEY("case")) {
            e->name = v;
            e->name_len = (uint32_t)vl;
        } else if (KEY("format")) {
            bad = (e->format = (unsigned)count_of(v, vl)) < 2u || e->format > 5u;
        } else if (KEY("outcome")) {
            bad = (e->outcome = word_of(v, vl, outcomes, 5)) < 0;
        } else if (KEY("stdout-bytes") || KEY("fuel-consumed")) {
            bad = (*(KEY("fuel-consumed") ? &e->fuel : &e->stdout_bytes) = count_of(v, vl)) < 0;
        } else if (KEY("return")) {
            bad = tval_parse(v, vl, &e->ret, 0) != 0;
            e->has_return = 1;
        } else if (KEY("refused.reason") || KEY("diagnostic.code")) {
            e->text = v;
            e->text_len = (uint32_t)vl;
        } else if (KEY("diagnostic.position")) {
            bad = pos_parse(v, vl, &e->fault.pos) != 0;
        } else if (KEY("error.set") || KEY("error.value")) {
            *(KEY("error.set") ? &e->set : &e->value) = v;
            *(KEY("error.set") ? &e->set_len : &e->value_len) = (uint32_t)vl;
        } else if (KEY("error.tag")) {
            bad = tval_parse(v, vl, &e->tag, 0) != 0 || e->tag.bytes[0] < CINT_TAG_U8;
        } else if (k > 17u && memcmp(s, "diagnostic.fault.", 17) == 0) {
            e->has_dfault = 1;
            bad = fault_line(e, s + 17, k - 17u, v, vl, &stacks) != 0;
        } else if (k > 6u && memcmp(s, "fault.", 6) == 0) {
            bad = fault_line(e, s + 6, k - 6u, v, vl, &stacks) != 0;
        } else {
            e->nstate += KEY("state.global");
            bad = !(KEY("state.global") || KEY("clause") || KEY("source") || KEY("stdout-sha256"));
        }
#undef KEY
    }
    if (used != NULL) *used = b < n ? b : n;
    if (bad) {
        wb_f(err, "line %u is not a line of SPEC-09 CONF-11", (unsigned)line);
        return -1;
    }
    if (e->format == 0u) {   /* cint-harness --cases writes no format line */
        e->format = e->fault.kernel_len > 0u ? 4u : e->outcome == WB_OUT_ERROR ? 3u : e->self_map ? 2u : 1u;
    }
    const char *why = e->outcome < 0 ? "the text has no outcome line"
                    : (e->outcome == WB_OUT_FAULT || e->has_dfault) && (e->fault.code == 0u || e->fault.op[0] == 0)
                        ? "the fault has no code or operation"
                    : e->outcome == WB_OUT_FAULT && e->fault.pos.line == 0u ? "the fault has no position"
                    : e->format >= 2u && stacks != e->fault.nstack ? "fault.stack lines differ from fault.stack-depth"
                    : e->outcome == WB_OUT_ERROR && (e->set == NULL || e->value == NULL || e->tag.len == 0u)
                        ? "the error has no set, value, or tag"
                    : NULL;
    if (why != NULL) {
        wb_s(err, why);
        return -1;
    }
    return 0;
}

void wb_expect_text(wb_buf *b, const wb_expect *e, wb_source_fn source, void *user)
{
    char text[1400];
    wb_report r = {e->fuel, NULL, NULL, NULL, NULL, 0u, {0, 0, 0, 0}};
    if (e->outcome == WB_OUT_FAULT) {
        fault_text(b, &e->fault, &r, e, source, user);
        return;
    }
    if (e->outcome == WB_OUT_REFUSED || e->outcome == WB_OUT_COMPILE) {
        wb_f(b, e->outcome == WB_OUT_REFUSED ? "refused: %.*s\n" : "error[%.*s]: the .expect text keeps no message "
             "(SPEC-09 CONF-11 rule 6)\n  --> ", (int)e->text_len, e->text);
        if (e->outcome == WB_OUT_COMPILE) {
            put_pos(b, &e->fault.pos);
            wb_s(b, "\n");
            wb_excerpt(b, &e->fault.pos, "", source, user);
        }
        if (e->has_dfault) {
            wb_f(b, "fault[%s]: %s\n", WB_CODE_NAMES[e->fault.code], CODE_TEXT[e->fault.code]);
            put_fields(b, &e->fault, e);
        }
        return;
    }
    if (e->outcome == WB_OUT_ERROR) {
        (void)wb_tval_text(&e->tag, text, sizeof text, 0);
        wb_f(b, "error: %.*s.%.*s (tag %s)\n   = error.set: %.*s\n   = error.value: %.*s\n", (int)e->set_len, e->set,
             (int)e->value_len, e->value, text, (int)e->set_len, e->set, (int)e->value_len, e->value);
        (void)wb_tval_text(&e->tag, text, sizeof text, 1);
        wb_f(b, "   = error.tag: %s\n", text);
    } else if (e->has_return) {   /* as wb_value_line; the c17 host links this file without wb_inspect.c */
        const char *sp = memchr(text, ' ', wb_tval_text(&e->ret, text, sizeof text, 1));
        wb_f(b, "value = %s : %.*s\n", sp != NULL ? sp + 1 : "", sp != NULL ? (int)(sp - text) : 0, text);
    } else {
        wb_s(b, "value, none returned\n");
    }
    wb_f(b, "   = fuel-consumed: %lld\n   = stdout-bytes: %lld\n", (long long)e->fuel, (long long)e->stdout_bytes);
}
