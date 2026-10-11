/* wb_inspect.c: the exact inspector of the workbench (box 14, SPEC-06 6; default BX14-11).
 *
 * Unit 1 covers the scalar types of column 02, the integers and `Bool`, and structs of them:
 * the exact value with its type, /raw, /hex, /bin, /bytes, /scale, /group, and /all. Unit 4a
 * adds box 09's arrays and views: a summary and the first elements, /grid, /stats, and /view
 * (SPEC-06 6.2, 6.3). Fixed point and wide integers (/ratio, /limbs, /approx, /digits) follow
 * with box 15, and the ternary and packed types (/trits, /lanes) with box 20 (t27 ruling
 * TT-03). Every displayed value is exact and carries its type (SPEC-06 6.1, rules 1 and 3); a
 * stored value that is not valid in its type is shown and flagged, never normalized (rule 5).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wb.h"
#include "wb_array.h"

static const struct {
    const char *word;
    const char *waits;
} LATER[] = {
    {"/ratio", "fixed point"},
    {"/digits", "fixed point"},
    {"/approx", "fixed point"},
    {"/limbs", "the wide integers"},
    {"/trits", "the ternary types"},
    {"/lanes", "the packed ternary types"},
};

/* A decimal count from 1 to 2^64 - 1, with no sign, spaces, or leading zeros. */
static int parse_count(const char *s, uint64_t *out)
{
    uint64_t v = 0u;
    if (s == NULL || s[0] < '1' || s[0] > '9') return -1;
    for (; *s != '\0'; s++) {
        unsigned d;
        if (*s < '0' || *s > '9') return -1;
        d = (unsigned)(*s - '0');
        if (v > (UINT64_MAX - d) / 10u) return -1;
        v = v * 10u + d;
    }
    *out = v;
    return 0;
}

int wb_formats_parse(int argc, const char *const *argv, wb_formats *fmt, wb_buf *err)
{
    memset(fmt, 0, sizeof *fmt);
    for (int i = 0; i < argc; i++) {
        const char *w = argv[i];
        size_t k;
        if (strcmp(w, "/raw") == 0) {
            fmt->show |= WB_FMT_RAW;
        } else if (strcmp(w, "/hex") == 0) {
            fmt->show |= WB_FMT_HEX;
        } else if (strcmp(w, "/bin") == 0) {
            fmt->show |= WB_FMT_BIN;
        } else if (strcmp(w, "/bytes") == 0) {
            fmt->show |= WB_FMT_BYTES;
        } else if (strcmp(w, "/all") == 0) {
            fmt->show |= WB_FMT_ALL;
        } else if (strcmp(w, "/group") == 0) {
            fmt->group = 1;
        } else if (strcmp(w, "/scale") == 0) {
            if (i + 1 >= argc || parse_count(argv[i + 1], &fmt->scale) != 0) {
                wb_s(err, "/scale takes a count from 1 to 18446744073709551615");
                return -1;
            }
            i++;
        } else {
            if (strcmp(w, "/grid") == 0 || strcmp(w, "/stats") == 0 || strcmp(w, "/view") == 0) {
                wb_f(err, "%s shows an array or a view", w);
                return -1;
            }
            for (k = 0u; k < sizeof LATER / sizeof LATER[0]; k++) {
                if (strcmp(w, LATER[k].word) == 0) {
                    wb_f(err, "%s waits for %s", w, LATER[k].waits);
                    return -1;
                }
            }
            wb_f(err, "unknown format %s; the formats are /raw, /hex, /bin, /bytes, /scale <n>, /group, and /all", w);
            return -1;
        }
    }
    return 0;
}

/* The type of a tagged value and its storage width in bytes; NULL for a tag the inspector does
 * not show (a `Z` is a record's value, never a program's). */
static const char *type_name(const wb_tval *v, unsigned *size)
{
    static const char *const NAMES[] = {"I8", "I16", "I32", "I64", "U8", "U16", "U32", "U64"};
    unsigned tag = v->bytes[0];
    if (tag == CINT_TAG_BOOL && v->len == 2u) {
        *size = 1u;
        return "Bool";
    }
    if ((tag >= CINT_TAG_I8 && tag <= CINT_TAG_I64) || (tag >= CINT_TAG_U8 && tag <= CINT_TAG_U64)) {
        *size = 1u << ((tag & 15u) - 1u);
        if (v->len != *size + 1u) return NULL;
        return NAMES[(tag & 15u) - 1u + (tag >= CINT_TAG_U8 ? 4u : 0u)];
    }
    return NULL;
}

/* The exact value: a decimal, `true` or `false`, or a flagged invalid Bool. */
static void exact_text(wb_buf *b, const wb_tval *v, unsigned size, int group)
{
    unsigned tag = v->bytes[0];
    wb_num x;
    if (tag == CINT_TAG_BOOL) {
        if (v->bytes[1] > 1u) wb_f(b, "invalid   FLAG: storage byte 0x%02x is neither 0 nor 1", (unsigned)v->bytes[1]);
        else wb_s(b, v->bytes[1] ? "true" : "false");
        return;
    }
    (void)wb_num_from_le(&x, v->bytes + 1u, size, tag < 0x20u);
    wb_num_dec(b, &x, group);
}

int wb_value_line(wb_buf *b, const char *name, const wb_tval *v)
{
    unsigned size = 0u;
    const char *type = type_name(v, &size);
    if (type == NULL) return -1;
    if (name != NULL) {
        wb_s(b, name);
        wb_s(b, " ");
    }
    wb_s(b, "= ");
    exact_text(b, v, size, 0);
    wb_f(b, " : %s\n", type);
    return 0;
}

/* One format line: two spaces of indent and the label in six columns, then two spaces. */
static void label(wb_buf *b, const char *indent, const char *name)
{
    wb_f(b, "%s  %-6s  ", indent, name);
}

static void scalar_lines(wb_buf *b, const char *indent, const wb_tval *v, unsigned size, const wb_formats *fmt)
{
    const uint8_t *p = v->bytes + 1u;
    unsigned tag = v->bytes[0];
    wb_num x;
    label(b, indent, "exact");
    if (fmt->scale > 0u && tag != CINT_TAG_BOOL) {
        (void)wb_num_from_le(&x, p, size, tag < 0x20u);
        (void)wb_num_dec_over(b, &x, fmt->scale, fmt->group);
    } else {
        exact_text(b, v, size, fmt->group);
    }
    wb_s(b, "\n");
    if ((fmt->show & WB_FMT_RAW) != 0u || fmt->scale > 0u) {
        label(b, indent, "raw");
        if (tag == CINT_TAG_BOOL) {
            wb_f(b, "%u", (unsigned)p[0]);
        } else {
            (void)wb_num_from_le(&x, p, size, tag < 0x20u);
            wb_num_dec(b, &x, fmt->group);
        }
        wb_s(b, "\n");
    }
    if ((fmt->show & WB_FMT_HEX) != 0u) {
        label(b, indent, "hex");
        wb_s(b, "0x");
        for (unsigned i = size; i > 0u; i--) wb_hex(b, &p[i - 1u], 1u);
        wb_s(b, "\n");
    }
    if ((fmt->show & WB_FMT_BIN) != 0u) {
        label(b, indent, "bin");
        wb_s(b, "0b");
        for (unsigned i = size; i > 0u; i--) {
            for (unsigned bit = 8u; bit > 0u; bit--) wb_s(b, ((p[i - 1u] >> (bit - 1u)) & 1u) != 0u ? "1" : "0");
            wb_s(b, i > 1u ? "_" : "");
        }
        wb_s(b, "\n");
    }
    if ((fmt->show & WB_FMT_BYTES) != 0u) {
        label(b, indent, "bytes");
        wb_s(b, "+0:");
        for (unsigned i = 0; i < size; i++) {
            wb_s(b, " ");
            wb_hex(b, &p[i], 1u);
        }
        wb_s(b, "\n");
    }
}

/* The header's notes: the storage, when a storage format is shown, and the scale. */
static void header_notes(wb_buf *b, const wb_tval *v, unsigned size, const wb_formats *fmt)
{
    unsigned tag = v->bytes[0];
    if (fmt->show != 0u) {
        if (tag == CINT_TAG_BOOL) wb_s(b, "   (1 byte, 0 or 1)");
        else wb_f(b, "   (%s, %u bits)", tag < 0x20u ? "signed" : "unsigned", size * 8u);
    }
    if (fmt->scale > 0u) wb_f(b, "   shown with scale %llu", (unsigned long long)fmt->scale);
    wb_s(b, "\n");
}

/* /scale applies to integers, and only to a scale whose decimal terminates. */
static int check_scale(const wb_tval *v, const wb_formats *fmt, wb_buf *err)
{
    wb_buf probe = {0};
    wb_num zero;
    int ok;
    if (fmt->scale == 0u) return 0;
    if (v->bytes[0] == CINT_TAG_BOOL) {
        wb_s(err, "/scale shows integers; this value is a Bool");
        return -1;
    }
    wb_num_zero(&zero);
    ok = wb_num_dec_over(&probe, &zero, fmt->scale, 0) == 0;
    wb_free(&probe);
    if (!ok) {
        wb_f(err, "/scale %llu has a prime factor other than 2 and 5, so the decimal would not terminate",
             (unsigned long long)fmt->scale);
        return -1;
    }
    return 0;
}

int wb_inspect_scalar(wb_buf *b, const char *name, const wb_tval *v, const wb_formats *fmt, wb_buf *err)
{
    unsigned size = 0u;
    const char *type = type_name(v, &size);
    if (type == NULL) {
        wb_s(err, "the value has no type the inspector shows");
        return -1;
    }
    if (check_scale(v, fmt, err) != 0) return -1;
    wb_f(b, "%s : %s", name, type);
    header_notes(b, v, size, fmt);
    scalar_lines(b, "", v, size, fmt);
    return 0;
}

int wb_inspect_struct(wb_buf *b, const char *name, const char *type_name_text, const wb_field *fields,
                      size_t count, const wb_formats *fmt, wb_buf *err)
{
    for (size_t i = 0; i < count; i++) {
        unsigned size = 0u;
        if (type_name(&fields[i].value, &size) == NULL) {
            wb_f(err, "field %s has no type the inspector shows", fields[i].name);
            return -1;
        }
        if (fmt->scale > 0u && fields[i].value.bytes[0] != CINT_TAG_BOOL && check_scale(&fields[i].value, fmt, err) != 0)
            return -1;
    }
    wb_f(b, "%s : %s   %llu field%s\n", name, type_name_text, (unsigned long long)count, count == 1u ? "" : "s");
    for (size_t i = 0; i < count; i++) {
        unsigned size = 0u;
        const char *type = type_name(&fields[i].value, &size);
        if (fmt->show == 0u && fmt->scale == 0u) {
            wb_f(b, "  .%s = ", fields[i].name);
            exact_text(b, &fields[i].value, size, fmt->group);
            wb_f(b, " : %s\n", type);
            continue;
        }
        wb_f(b, "  .%s : %s", fields[i].name, type);
        if (fields[i].value.bytes[0] == CINT_TAG_BOOL && fmt->scale > 0u) {
            /* /scale leaves a Bool field as it is. */
            wb_formats plain = *fmt;
            plain.scale = 0u;
            header_notes(b, &fields[i].value, size, &plain);
            scalar_lines(b, "  ", &fields[i].value, size, &plain);
        } else {
            header_notes(b, &fields[i].value, size, fmt);
            scalar_lines(b, "  ", &fields[i].value, size, fmt);
        }
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Arrays and views (SPEC-06 6.2, 6.3; box 14 unit 4a).                        */

#define FIRST_SHOWN 8u   /* the elements the default display lists */
#define GRID_ROWS 20     /* the rows of /grid without a window */

uint32_t wb_sum_bits(uint32_t w, uint64_t n)
{
    uint32_t k = 0;
    while (k < 64u && (1ull << k) < n) k++;
    return w + k;
}

/* `<a>..<b>` at *s, then `sep`. */
static int range_at(const char **s, int64_t *a, int64_t *b, char sep)
{
    char *end;
    *a = strtoll(*s, &end, 10);
    if (end == *s || end[0] != '.' || end[1] != '.') return -1;
    *s = end + 2;
    *b = strtoll(*s, &end, 10);
    if (end == *s || *end != sep) return -1;
    *s = end + 1;
    return 0;
}

/* `[a..b, c..d]` or `[c..d]` (declared bounds, half-open, SPEC-04 LS-171). */
static int parse_window(const char *s, wb_aformats *fmt)
{
    const char *p = s + 1;
    fmt->window = 1;
    fmt->r0 = 0;
    fmt->r1 = 1;
    if (s[0] != '[') return -1;
    if (range_at(&p, &fmt->r0, &fmt->r1, ',') == 0) {
        p += *p == ' ';
    } else {
        p = s + 1;
        fmt->r0 = 0;
        fmt->r1 = 1;
    }
    if (range_at(&p, &fmt->c0, &fmt->c1, ']') != 0 || *p != '\0') return -1;
    return fmt->r0 <= fmt->r1 && fmt->c0 <= fmt->c1 ? 0 : -1;
}

int wb_aformats_parse(int argc, const char *const *argv, wb_aformats *fmt, wb_buf *err)
{
    const char *rest[64];
    int n = 0;
    memset(fmt, 0, sizeof *fmt);
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "/grid") == 0) {
            char window[128] = "";
            size_t len = 0;
            fmt->array |= WB_FMT_GRID;
            while (i + 1 < argc && (len > 0 || argv[i + 1][0] == '[') && len < sizeof window - 2u) {
                len += (size_t)snprintf(window + len, sizeof window - len, "%s%s", len > 0 ? " " : "", argv[++i]);
                if (window[len - 1u] == ']') break;
            }
            if (len > 0 && parse_window(window, fmt) != 0) {
                wb_f(err, "/grid takes a window [r0..r1, c0..c1] or [c0..c1], half-open, not %s", window);
                return -1;
            }
        } else if (strcmp(argv[i], "/stats") == 0 || strcmp(argv[i], "/view") == 0) {
            fmt->array |= argv[i][1] == 's' ? WB_FMT_STATS : WB_FMT_VIEW;
        } else if (n < 64) {
            fmt->array |= strcmp(argv[i], "/all") == 0 ? WB_FMT_GRID | WB_FMT_STATS | WB_FMT_VIEW : 0u;
            rest[n++] = argv[i];
        }
    }
    return wb_formats_parse(n, rest, &fmt->f, err);
}

/* The element type of tag, with its storage size; NULL for none the inspector shows. */
static const char *elem_type(unsigned tag, unsigned *size)
{
    wb_tval t;
    memset(&t, 0, sizeof t);
    t.bytes[0] = (uint8_t)tag;
    *size = tag == CINT_TAG_BOOL ? 1u : (tag & 15u) >= 1u && (tag & 15u) <= 4u ? 1u << ((tag & 15u) - 1u) : 0u;
    t.len = (uint16_t)(*size + 1u);
    return *size != 0u ? type_name(&t, size) : NULL;
}

/* The element count of v in *count, after checking that every element lies in its buffer. */
static int view_check(const wb_view *v, unsigned *size, uint64_t *count, wb_buf *err)
{
    int64_t lo = v->origin, hi = v->origin;
    uint64_t n = 1u;
    if (v->rank < 1u || v->rank > WB_RANK_MAX || elem_type(v->tag, size) == NULL || v->buffer_len < 0 ||
        (v->buffer == NULL && v->buffer_len > 0) || v->origin < 0 || v->origin > v->buffer_len) {
        wb_s(err, "the value has no element type, rank, or buffer the inspector shows");
        return -1;
    }
    for (uint32_t k = 0; k < v->rank; k++) {
        int64_t e = v->extent[k], st = v->stride[k];
        uint64_t m = st < 0 ? 0u - (uint64_t)st : (uint64_t)st;
        if (e < 0 || (n > 0u && (uint64_t)e > (UINT64_MAX >> 2) / n)) {
            wb_s(err, "the view's shape has a negative extent or too many elements to show");
            return -1;
        }
        n *= (uint64_t)e;
        if (e > 1 && m != 0u && (m > (uint64_t)v->buffer_len || (uint64_t)(e - 1) > (uint64_t)v->buffer_len / m)) {
            lo = -1;
        } else if (e > 1) {
            hi += st > 0 ? (e - 1) * st : 0;
            lo += st < 0 ? (e - 1) * st : 0;
        }
    }
    if (n > 0u && (lo < 0 || hi >= v->buffer_len)) {
        wb_f(err, "the view reaches outside its buffer of %lld elements", (long long)v->buffer_len);
        return -1;
    }
    *count = n;
    return 0;
}

/* The element at canonical position p (row-major over the logical indices), and its indices. */
static void element_at(const wb_view *v, unsigned size, uint64_t p, int64_t *ix, wb_tval *out)
{
    int64_t at = v->origin;
    for (uint32_t k = v->rank; k-- > 0;) {
        ix[k] = (int64_t)(p % (uint64_t)v->extent[k]);
        p /= (uint64_t)v->extent[k];
        at += ix[k] * v->stride[k];
    }
    out->bytes[0] = (uint8_t)v->tag;
    memcpy(out->bytes + 1, v->buffer + (size_t)at * size, size);
    out->len = (uint16_t)(size + 1u);
}

/* Indices in declared bounds, `[i, j]`. */
static void put_index(wb_buf *b, const wb_view *v, const int64_t *ix)
{
    for (uint32_t k = 0; k < v->rank; k++) wb_f(b, "%s%lld", k ? ", " : "[", (long long)(v->lower[k] + ix[k]));
    wb_s(b, "]");
}

static void put_list(wb_buf *b, const wb_view *v, const int64_t *x)
{
    for (uint32_t k = 0; k < v->rank; k++) wb_f(b, "%s%lld", k ? ", " : "[", (long long)x[k]);
    wb_s(b, "]");
}

/* A value as a cell or a statistic shows it: exact, or over the scale. */
static void value_text(wb_buf *b, const wb_tval *e, unsigned size, const wb_formats *f)
{
    wb_num x;
    if (f->scale > 0u && e->bytes[0] != CINT_TAG_BOOL) {
        (void)wb_num_from_le(&x, e->bytes + 1u, size, e->bytes[0] < 0x20u);
        (void)wb_num_dec_over(b, &x, f->scale, f->group);
    } else {
        exact_text(b, e, size, f->group);
    }
}

/* /grid: rows of the first dimension and columns of the last, indices in declared bounds, values
 * right-aligned and in full; columns that do not fit the width are left out and named. */
static int grid(wb_buf *b, const wb_view *v, unsigned size, const wb_aformats *fmt, wb_buf *err)
{
    int two = v->rank == 2u;
    int64_t lr = two ? v->lower[0] : 0, er = two ? v->extent[0] : 1, lc = v->lower[v->rank - 1u];
    int64_t ec = v->extent[v->rank - 1u], r0 = lr, r1 = lr + (er < GRID_ROWS ? er : GRID_ROWS), c0 = lc, c1 = lc + ec;
    int64_t ix[WB_RANK_MAX] = {0}, room = fmt->width > 0u ? (int64_t)fmt->width : 80, used, shown;
    size_t rw = 0, w[64];
    char tmp[32];
    wb_buf cell = {0};
    wb_tval e;
    if (v->rank > 2u) {
        wb_f(err, "/grid shows rank 1 or 2; this value has rank %u", (unsigned)v->rank);
        return -1;
    }
    if (fmt->window) {
        r0 = two ? fmt->r0 : 0, r1 = two ? fmt->r1 : 1, c0 = fmt->c0, c1 = fmt->c1;
        if (r0 < lr || r1 > lr + er || c0 < lc || c1 > lc + ec) {
            wb_s(err, "the /grid window lies outside the value's indices");
            return -1;
        }
    }
    for (int64_t r = r0; two && r < r1; r++) {
        size_t n = (size_t)snprintf(tmp, sizeof tmp, "%lld", (long long)r);
        rw = n > rw ? n : rw;
    }
    used = 2 + (int64_t)rw;
    for (shown = 0; c0 + shown < c1 && shown < 64; shown++) {   /* the widths, column by column */
        size_t cw = (size_t)snprintf(tmp, sizeof tmp, "%lld", (long long)(c0 + shown));
        for (int64_t r = r0; r < r1; r++) {
            ix[0] = r - lr;
            ix[v->rank - 1u] = c0 + shown - lc;
            element_at(v, size, (uint64_t)(two ? ix[0] * ec + ix[1] : ix[0]), ix, &e);
            cell.n = 0;
            value_text(&cell, &e, size, &fmt->f);
            cw = cell.n > cw ? cell.n : cw;
        }
        if (shown > 0 && used + 2 + (int64_t)cw > room) break;
        w[shown] = cw;
        used += 2 + (int64_t)cw;
    }
    wb_s(b, "  grid  ");
    if (two) {
        wb_f(b, " rows %lld to %lld of %lld to %lld,", (long long)r0, (long long)(r1 - 1), (long long)lr,
             (long long)(lr + er - 1));
    }
    wb_f(b, " columns %lld to %lld of %lld to %lld\n  %*s", (long long)c0, (long long)(c1 - 1), (long long)lc,
         (long long)(lc + ec - 1), (int)rw, "");
    for (int64_t c = 0; c < shown; c++) wb_f(b, "  %*lld", (int)w[c], (long long)(c0 + c));
    wb_s(b, "\n");
    for (int64_t r = r0; r < r1 && shown > 0; r++) {
        wb_s(b, "  ");
        if (two) wb_f(b, "%*lld", (int)rw, (long long)r);
        for (int64_t c = 0; c < shown; c++) {
            ix[0] = r - lr;
            ix[v->rank - 1u] = c0 + c - lc;
            element_at(v, size, (uint64_t)(two ? ix[0] * ec + ix[1] : ix[0]), ix, &e);
            cell.n = 0;
            value_text(&cell, &e, size, &fmt->f);
            wb_f(b, "  %*s%.*s", (int)(w[c] - cell.n), "", (int)cell.n, cell.p != NULL ? cell.p : "");
        }
        wb_s(b, "\n");
    }
    if (c0 + shown < c1) {
        wb_f(b, "  columns %lld to %lld left out to fit %lld characters; a window shows them\n",
             (long long)(c0 + shown), (long long)(c1 - 1), (long long)room);
    }
    wb_free(&cell);
    return 0;
}

/* /stats with the workbench's own multiword accumulator (BX14-02 B): it never narrows, never
 * faults, and writes no program state. */
static void stats(wb_buf *b, const wb_view *v, unsigned size, uint64_t n, const wb_formats *f)
{
    wb_num sum, x, lo, hi;
    uint64_t nonzero = 0, at[2] = {0, 0};
    int64_t ix[WB_RANK_MAX];
    wb_tval e, ext[2];
    wb_buf t[3] = {{0}, {0}, {0}};
    size_t vw = 0;
    wb_num_zero(&sum);
    wb_num_zero(&lo);
    wb_num_zero(&hi);
    memset(ext, 0, sizeof ext);
    for (uint64_t p = 0; p < n; p++) {
        element_at(v, size, p, ix, &e);
        (void)wb_num_from_le(&x, e.bytes + 1u, size, e.bytes[0] < 0x20u);
        (void)wb_num_add(&sum, &x);
        nonzero += x.n != 0u;
        if (p == 0 || wb_num_cmp(&x, &lo) < 0) {
            lo = x;
            at[0] = p;
            ext[0] = e;
        }
        if (p == 0 || wb_num_cmp(&x, &hi) > 0) {
            hi = x;
            at[1] = p;
            ext[1] = e;
        }
    }
    if (f->scale > 0u && v->tag != CINT_TAG_BOOL) (void)wb_num_dec_over(&t[0], &sum, f->scale, f->group);
    else wb_num_dec(&t[0], &sum, f->group);
    for (int k = 0; k < 2 && n > 0u; k++) value_text(&t[k + 1], &ext[k], size, f);
    for (int k = 0; k < 3; k++) vw = t[k].n > vw ? t[k].n : vw;
    wb_f(b, "  count    %llu\n  sum      %-*.*s   accumulator %c%u (SPEC-01 IM-80)", (unsigned long long)n, (int)vw,
         (int)t[0].n, t[0].p != NULL ? t[0].p : "", v->tag >= CINT_TAG_U8 || v->tag == CINT_TAG_BOOL ? 'U' : 'I',
         wb_sum_bits(v->tag == CINT_TAG_BOOL ? 1u : size * 8u, n));
    if (f->scale > 0u && v->tag != CINT_TAG_BOOL) {
        wb_s(b, "   raw ");
        wb_num_dec(b, &sum, f->group);
    }
    for (int k = 0; k < 2; k++) {
        wb_f(b, "\n  %s      ", k ? "max" : "min");
        if (n == 0u) {
            wb_s(b, "none, no elements");
            continue;
        }
        element_at(v, size, at[k], ix, &e);
        wb_f(b, "%-*.*s   first at ", (int)vw, (int)t[k + 1].n, t[k + 1].p != NULL ? t[k + 1].p : "");
        put_index(b, v, ix);
    }
    wb_f(b, "\n  nonzero  %llu\n", (unsigned long long)nonzero);
    for (int k = 0; k < 3; k++) wb_free(&t[k]);
}

int wb_inspect_array(wb_buf *b, const char *name, const wb_view *v, const wb_aformats *fmt, wb_buf *err)
{
    unsigned size = 0u;
    uint64_t n = 0u;
    int64_t ix[WB_RANK_MAX];
    wb_tval e;
    if (view_check(v, &size, &n, err) != 0) return -1;
    const char *type = elem_type(v->tag, &size);
    memset(&e, 0, sizeof e);
    e.bytes[0] = (uint8_t)v->tag;
    e.len = (uint16_t)(size + 1u);
    if (check_scale(&e, &fmt->f, err) != 0) return -1;
    wb_f(b, "%s : %s", name, type);
    put_list(b, v, v->extent);
    wb_f(b, "   %llu element%s   %s", (unsigned long long)n, n == 1u ? "" : "s", v->placement != NULL ? v->placement
                                                                                                     : "host");
    for (uint32_t k = 0; k < v->rank; k++) {
        if (v->lower[k] != 0) {
            wb_s(b, "   lower bounds ");
            put_list(b, v, v->lower);
            break;
        }
    }
    wb_s(b, "\n");
    for (uint64_t p = 0; fmt->array == 0u && p < n && p < FIRST_SHOWN; p++) {
        element_at(v, size, p, ix, &e);
        wb_s(b, "  ");
        put_index(b, v, ix);
        if (fmt->f.show == 0u && fmt->f.scale == 0u) {
            wb_s(b, " = ");
            exact_text(b, &e, size, fmt->f.group);
            wb_s(b, "\n");
        } else {
            wb_f(b, " : %s", type);
            header_notes(b, &e, size, &fmt->f);
            scalar_lines(b, "  ", &e, size, &fmt->f);
        }
    }
    if (fmt->array == 0u && n > FIRST_SHOWN)
        wb_f(b, "  ... %llu more; /grid and /stats show them\n", (unsigned long long)(n - FIRST_SHOWN));
    if ((fmt->array & WB_FMT_GRID) != 0u && n > 0u && grid(b, v, size, fmt, err) != 0) return -1;
    if ((fmt->array & WB_FMT_STATS) != 0u) stats(b, v, size, n, &fmt->f);
    if ((fmt->array & WB_FMT_VIEW) != 0u) {
        wb_s(b, "  view\n    buffer      ");
        if (v->has_id) wb_f(b, "%llu, generation %llu\n", (unsigned long long)v->id, (unsigned long long)v->generation);
        else wb_s(b, "none: this buffer has no canonical identity (SPEC-01 IM-81)\n");
        wb_f(b, "    element     %s\n    rank        %u\n    shape       ", type, (unsigned)v->rank);
        put_list(b, v, v->extent);
        wb_s(b, "\n    lower       ");
        put_list(b, v, v->lower);
        wb_f(b, "\n    origin      %lld\n    strides     ", (long long)v->origin);
        put_list(b, v, v->stride);
        wb_f(b, " elements\n    permission  %s\n    placement   %s\n", v->write ? "write" : "read",
             v->placement != NULL ? v->placement : "host");
    }
    return b->failed ? -1 : 0;
}
