/* test_print_wide.c: formatted print and failed asserts of the wide integers,
 * formatted print of fixed point, and `!bits`, against cint_ref (rt/OPEN.md
 * RT-OQ-55, RT-OQ-56; roadmap box 15).
 *
 * tests/test_cint_rt_print_wide.py writes the cases, one per line: `<id> P
 * <entry> <tag> <form> <fill> <width> <scale> <value> = <outcome>` or `<id> A
 * <tag> <a> <b> = <outcome>`, each value the hexadecimal of its little-endian
 * bytes. A P case prints the value in a statement of its own, in an entry at
 * site 1 of module `print.ci` (line 1, column 1), through
 * cint_rt_print_format_wide (entry W, the value as its limbs),
 * cint_rt_print_format (entry N, the value as its 64-bit pattern) or
 * cint_rt_print_format_fixed (entry Q, the raw storage, with the scale field
 * as the precision), and renders
 * the bytes the statement delivered; an A case calls cint_rt_fault_assert_wide
 * at that site in an entry. A fault renders as the canonical bytes of its
 * record (cint_fault_encode). A case whose rendering differs from the expected
 * text fails, and so does a faulting statement that delivers bytes. Then a
 * statement that mixes the entries, the arguments no emitted code passes, the
 * order of calls and a faulted context are checked directly. Prints the helper
 * mode and the counts, and exits 1 on any failure.
 *
 * Usage: test_print_wide <cases>.
 */
/* fopen and strcpy over bounded buffers, which MSVC deprecates. */
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS 1
#endif
#include "cint_rt.h"
#include "cint_rt_host.h"
#include "cint_rt_internal.h"
#include "cint_fixed.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static long g_pass;
static long g_fail;
static long g_direct;

static const cint_site_info p_sites[] = {{0u, 0u, NULL}, {1u, 1u, "print"}};
static const cint_module p_module = {"print.ci", 8u, 2u, p_sites};
static const cint_module *const p_modules[] = {&p_module};
static const cint_program p_program = {1u, 0u, p_modules, NULL};
static const cint_site P_SITE = {0u, 1u};

#define DCHECK(cond, what)                                    \
    do {                                                      \
        if (!(cond)) {                                        \
            g_direct++;                                       \
            fprintf(stderr, "FAIL direct: %s\n", (what));    \
        }                                                     \
    } while (0)

/* The bytes delivered to the output callback. */
typedef struct sink {
    uint8_t bytes[1u << 16];
    size_t total;
    int calls;
} sink;

static sink g_sink;

static cint_status sink_write(void *user, const uint8_t *bytes, size_t len)
{
    sink *s = (sink *)user;
    size_t room = sizeof s->bytes - s->total;
    s->calls++;
    if (len > room) return CINT_RESOURCE;
    memcpy(s->bytes + s->total, bytes, len);
    s->total += len;
    return CINT_OK;
}

static int hexval(int ch)
{
    return ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1;
}

static int parse_hex(const char *s, uint8_t *out, size_t n)
{
    size_t i;
    if (strlen(s) != 2u * n) return 0;
    for (i = 0u; i < n; i++) {
        int hi = hexval(s[2 * i]), lo = hexval(s[2 * i + 1]);
        if (hi < 0 || lo < 0) return 0;
        out[i] = (uint8_t)(hi * 16 + lo);
    }
    return 1;
}

static void put_hex(char *dst, const uint8_t *bytes, size_t n)
{
    static const char digits[] = "0123456789abcdef";
    size_t i;
    for (i = 0u; i < n; i++) {
        dst[2 * i] = digits[bytes[i] >> 4];
        dst[2 * i + 1] = digits[bytes[i] & 15u];
    }
    dst[2 * n] = '\0';
}

/* A value of tag from its hex bytes: limbs for a wide tag, else the
 * zero-extended pattern in limb[0] (one byte for Bool). */
static int parse_value(const char *s, uint32_t tag, uint64_t *limb)
{
    uint8_t bytes[128];
    unsigned n = tag == CINT_TAG_BOOL ? 1u : cint_rt_tag_width(tag) / 8u, i;
    if (n == 0u || n > sizeof bytes || !parse_hex(s, bytes, n)) return 0;
    memset(limb, 0, 16u * sizeof limb[0]);
    if (cint_wide_limbs(tag) != 0u) {
        cint_wide_load(limb, bytes, n / 8u);
    } else {
        for (i = 0u; i < n; i++) limb[0] |= (uint64_t)bytes[i] << (8u * i);
    }
    return 1;
}

/* "F <hex>" for the context's record, which is then cleared. */
static void put_fault(cint_ctx *ctx, char *dst)
{
    static cint_fault_record rec;
    static uint8_t bytes[4096];
    size_t n;
    if (cint_ctx_fault(ctx, &rec) != CINT_OK) {
        strcpy(dst, "F none");
        return;
    }
    n = cint_fault_encode(&rec, bytes, sizeof bytes);
    dst[0] = 'F';
    dst[1] = ' ';
    put_hex(dst + 2, bytes, n);
    (void)cint_ctx_clear_fault(ctx);
}

static int split(char *line, char **field, int max)
{
    int nf = 0;
    char *p = strtok(line, " ");
    while (p != NULL && nf < max) {
        field[nf++] = p;
        p = strtok(NULL, " ");
    }
    return p == NULL ? nf : -1;
}

/* Runs one case: its rendering to got, the expected text to *want, and
 * whether a faulting statement delivered bytes to *wrote. 0 if malformed. */
static int run_case(cint_ctx *ctx, char *line, char *got, const char **want, int *wrote)
{
    static uint64_t a[16], b[16];
    char *field[16];
    char *end;
    int nf = split(line, field, 16);
    uint32_t tag;
    bool ok;
    cint_status st;
    if (nf < 5 || strcmp(field[nf - 3], "=") != 0) return 0;
    *want = field[nf - 1] - 2;  /* "T <hex>" or "F <hex>": the kind is two bytes back */
    field[nf - 1][-1] = ' ';
    tag = (uint32_t)strtoul(field[strcmp(field[1], "P") == 0 ? 3 : 2], &end, 10);
    if (strcmp(field[1], "P") == 0 && nf == 12) {
        uint32_t form = (uint32_t)strtoul(field[4], &end, 10), fill = (uint32_t)strtoul(field[5], &end, 10);
        uint64_t width = strtoull(field[6], &end, 10), scale = strtoull(field[7], &end, 10);
        if (!parse_value(field[8], tag, a)) return 0;
        memset(&g_sink, 0, sizeof g_sink);
        if (cint_rt_entry_begin(ctx, P_SITE, CINT_FUEL_UNBOUNDED, 8) != CINT_OK) return 0;
        ok = cint_rt_print_begin(ctx, P_SITE) &&
             (field[2][0] == 'W'   ? cint_rt_print_format_wide(ctx, tag, a, form, fill, width, scale)
              : field[2][0] == 'Q' ? cint_rt_print_format_fixed(ctx, tag, a[0], form, fill, width, scale)
                                   : cint_rt_print_format(ctx, tag, a[0], form, fill, width, scale)) &&
             cint_rt_print_end(ctx);
        st = cint_rt_entry_end(ctx);
        if (ok && st == CINT_OK) {
            got[0] = 'T';
            got[1] = ' ';
            put_hex(got + 2, g_sink.bytes, g_sink.total);
            return 1;
        }
        if (ok || st != CINT_FAULT) return 0;
        *wrote = g_sink.calls != 0;
        put_fault(ctx, got);
        return 1;
    }
    if (strcmp(field[1], "A") == 0 && nf == 8) {
        if (!parse_value(field[3], tag, a) || !parse_value(field[4], tag, b)) return 0;
        if (cint_rt_entry_begin(ctx, P_SITE, CINT_FUEL_UNBOUNDED, 8) != CINT_OK) return 0;
        ok = cint_rt_fault_assert_wide(ctx, P_SITE, tag, a, b);
        if (ok || cint_rt_entry_end(ctx) != CINT_FAULT) return 0;
        put_fault(ctx, got);
        return 1;
    }
    return 0;
}

static int record_is(cint_ctx *ctx, uint16_t code, const char *operation)
{
    cint_fault_record rec;
    size_t n = strlen(operation);
    if (cint_ctx_fault(ctx, &rec) != CINT_OK) return 0;
    return rec.code == code && rec.operation_len == n && memcmp(rec.operation, operation, n) == 0;
}

static int delivered(const char *text)
{
    size_t n = strlen(text);
    return g_sink.calls == 1 && g_sink.total == n && memcmp(g_sink.bytes, text, n) == 0;
}

static void test_direct(cint_ctx *ctx)
{
    uint64_t umax[2] = {UINT64_MAX, UINT64_MAX}, minus[16], one[4] = {1u, 0u, 0u, 0u};
    memset(minus, 0xff, sizeof minus);
    /* Both entries and literal pieces in one statement, delivered in one call. */
    memset(&g_sink, 0, sizeof g_sink);
    DCHECK(cint_rt_entry_begin(ctx, P_SITE, CINT_FUEL_UNBOUNDED, 8) == CINT_OK && cint_rt_print_begin(ctx, P_SITE) &&
               cint_rt_print_bytes(ctx, "v=", 2u) &&
               cint_rt_print_format_wide(ctx, CINT_TAG_U128, umax, 3u | CINT_FORMAT_ALT, ' ', 0u, 0u) &&
               cint_rt_print_bytes(ctx, ";", 1u) &&
               cint_rt_print_format(ctx, CINT_TAG_I64, (uint64_t)(int64_t)-7, 1u | CINT_FORMAT_PLUS, ' ', 0u, 0u) &&
               cint_rt_print_bytes(ctx, ";", 1u) &&
               cint_rt_print_format_wide(ctx, CINT_TAG_I1024, minus, 6u | CINT_FORMAT_LEFT, '.', 5u, 0u) &&
               cint_rt_print_wide(ctx, CINT_TAG_I256, one) && cint_rt_print_end(ctx) &&
               cint_rt_entry_end(ctx) == CINT_OK && delivered("v=0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF;-7;0tN..1"),
           "a statement of both entries and pieces");
    /* Fixed point and `!bits` beside them: Q16.16 -1.5, unformatted and to two digits, and I16 -1 in
     * binary bits. */
    memset(&g_sink, 0, sizeof g_sink);
    DCHECK(cint_rt_entry_begin(ctx, P_SITE, CINT_FUEL_UNBOUNDED, 8) == CINT_OK && cint_rt_print_begin(ctx, P_SITE) &&
               cint_rt_print_format_fixed(ctx, CINT_QT(CINT_TAG_I32, 16u), 0xfffe8000u, 0u, ' ', 0u, 0u) &&
               cint_rt_print_bytes(ctx, ";", 1u) &&
               cint_rt_print_format_fixed(ctx, CINT_QT(CINT_TAG_I32, 16u), 0xfffe8000u,
                                          CINT_FORMAT_PRECISION | CINT_FORMAT_PLUS, ' ', 0u, 2u) &&
               cint_rt_print_bytes(ctx, ";", 1u) &&
               cint_rt_print_format(ctx, CINT_TAG_I16, 0xffffu, 5u | CINT_FORMAT_BITS | CINT_FORMAT_UNDERSCORE, ' ',
                                    0u, 0u) &&
               cint_rt_print_end(ctx) && cint_rt_entry_end(ctx) == CINT_OK &&
               delivered("-1.5;-1.50;1111_1111_1111_1111"),
           "a statement of fixed point and bits");
    /* A precision whose digits fit while the whole part and the point pass 1 GiB, a byte staged. */
    memset(&g_sink, 0, sizeof g_sink);
    DCHECK(cint_rt_entry_begin(ctx, P_SITE, CINT_FUEL_UNBOUNDED, 8) == CINT_OK && cint_rt_print_begin(ctx, P_SITE) &&
               cint_rt_print_bytes(ctx, "x", 1u) &&
               !cint_rt_print_format_fixed(ctx, CINT_QT(CINT_TAG_I8, 4u), 0x10u, CINT_FORMAT_PRECISION, ' ', 0u,
                                           ((uint64_t)1 << 30) - 2u) &&
               cint_rt_entry_end(ctx) == CINT_FAULT && g_sink.calls == 0 &&
               record_is(ctx, CINT_E_UNSUPPORTED, "host.resource"),
           "a precision that passes 1 GiB with a byte staged");
    (void)cint_ctx_clear_fault(ctx);
    DCHECK(cint_rt_entry_begin(ctx, P_SITE, CINT_FUEL_UNBOUNDED, 8) == CINT_OK &&
               !cint_rt_print_format_fixed(ctx, CINT_QT(CINT_TAG_I32, 16u), 1u, 0u, ' ', 0u, 0u) &&
               cint_rt_entry_end(ctx) == CINT_FAULT && record_is(ctx, CINT_E_UNSUPPORTED, "dispatch.admit"),
           "a fixed-point hole without print_begin");
    (void)cint_ctx_clear_fault(ctx);
    /* A hole that faults after pieces were staged: the statement delivers nothing. */
    memset(&g_sink, 0, sizeof g_sink);
    DCHECK(cint_rt_entry_begin(ctx, P_SITE, CINT_FUEL_UNBOUNDED, 8) == CINT_OK && cint_rt_print_begin(ctx, P_SITE) &&
               cint_rt_print_bytes(ctx, "x", 1u) &&
               !cint_rt_print_format_wide(ctx, CINT_TAG_I512, minus, 1u | CINT_FORMAT_RIGHT, ' ',
                                          ((uint64_t)1 << 30), 0u) &&
               cint_rt_entry_end(ctx) == CINT_FAULT && g_sink.calls == 0 &&
               record_is(ctx, CINT_E_UNSUPPORTED, "host.resource"),
           "a width that passes 1 GiB with a byte staged");
    (void)cint_ctx_clear_fault(ctx);
    /* Arguments no emitted code passes, and the order of calls. */
    DCHECK(cint_rt_entry_begin(ctx, P_SITE, CINT_FUEL_UNBOUNDED, 8) == CINT_OK && cint_rt_print_begin(ctx, P_SITE) &&
               !cint_rt_print_format_wide(ctx, CINT_TAG_I128, NULL, 1u, ' ', 0u, 0u) &&
               cint_rt_entry_end(ctx) == CINT_FAULT && record_is(ctx, CINT_E_UNSUPPORTED, "dispatch.admit"),
           "no limbs");
    (void)cint_ctx_clear_fault(ctx);
    DCHECK(cint_rt_entry_begin(ctx, P_SITE, CINT_FUEL_UNBOUNDED, 8) == CINT_OK &&
               !cint_rt_print_format_wide(ctx, CINT_TAG_I128, one, 1u, ' ', 0u, 0u) &&
               cint_rt_entry_end(ctx) == CINT_FAULT && record_is(ctx, CINT_E_UNSUPPORTED, "dispatch.admit"),
           "a wide hole without print_begin");
    (void)cint_ctx_clear_fault(ctx);
    DCHECK(!cint_rt_fault_assert_wide(ctx, P_SITE, CINT_TAG_I256, NULL, one) &&
               record_is(ctx, CINT_E_UNSUPPORTED, "dispatch.admit"),
           "an assert with no first operand");
    (void)cint_ctx_clear_fault(ctx);
    DCHECK(!cint_rt_fault_assert_wide(ctx, P_SITE, CINT_TAG_I256, one, NULL) &&
               record_is(ctx, CINT_E_UNSUPPORTED, "dispatch.admit"),
           "an assert with no second operand");
    (void)cint_ctx_clear_fault(ctx);
    DCHECK(!cint_rt_fault_assert_wide(ctx, P_SITE, 0u, one, one) && record_is(ctx, CINT_E_UNSUPPORTED, "dispatch.admit"),
           "an assert with no operand type");
    (void)cint_ctx_clear_fault(ctx);
    /* A context that holds a fault keeps it: each call returns false and writes nothing. */
    DCHECK(!cint_rt_fault_assert_wide(ctx, P_SITE, CINT_TAG_U256, one, minus) &&
               record_is(ctx, CINT_E_ASSERT, "assert.checked.bool"),
           "a failed assert");
    memset(&g_sink, 0, sizeof g_sink);
    DCHECK(!cint_rt_fault_assert_wide(ctx, P_SITE, CINT_TAG_I128, NULL, NULL) &&
               !cint_rt_fault_assert_wide(ctx, P_SITE, CINT_TAG_I128, one, one) &&
               !cint_rt_print_format_wide(ctx, CINT_TAG_I128, one, 1u, ' ', 0u, 0u) &&
               !cint_rt_print_format_wide(ctx, CINT_TAG_I64, NULL, 0u, ' ', 0u, 0u) &&
               !cint_rt_print_format(ctx, CINT_TAG_I128, 1u, 1u, ' ', 0u, 0u) &&
               !cint_rt_print_format_fixed(ctx, CINT_QT(CINT_TAG_I32, 16u), 1u, 0u, ' ', 0u, 0u) &&
               !cint_rt_print_format_fixed(ctx, CINT_TAG_I32, 1u, 0u, ' ', 0u, 0u) &&
               record_is(ctx, CINT_E_ASSERT, "assert.checked.bool") && g_sink.calls == 0,
           "a faulted context keeps its record");
    (void)cint_ctx_clear_fault(ctx);
}

/* The whole file, NUL-terminated. */
static char *read_all(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *text;
    long n;
    if (f == NULL || fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        if (f != NULL) fclose(f);
        return NULL;
    }
    text = (char *)malloc((size_t)n + 1u);
    if (text != NULL && fread(text, 1u, (size_t)n, f) != (size_t)n) {
        free(text);
        text = NULL;
    }
    if (text != NULL) text[n] = '\0';
    fclose(f);
    return text;
}

int main(int argc, char **argv)
{
    static char got[(1u << 17) + 16u];
    static char copy[256];
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL;
    char *text, *line, *next;
    long n = 0;
    printf("helper mode: %s\n", CINT_RT_HELPER_MODE);
    if (argc != 2) {
        fprintf(stderr, "usage: test_print_wide <cases>\n");
        return 2;
    }
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = &p_program;
    cfg.output = sink_write;
    cfg.output_user = &g_sink;
    if (cint_ctx_create(&cfg, &ctx) != CINT_OK) {
        fprintf(stderr, "FAIL cint_ctx_create\n");
        return 1;
    }
    text = read_all(argv[1]);
    if (text == NULL) {
        fprintf(stderr, "FAIL cannot read %s\n", argv[1]);
        cint_ctx_destroy(ctx);
        return 1;
    }
    for (line = text; *line != '\0'; line = next) {
        const char *want = "";
        int wrote = 0;
        next = strchr(line, '\n');
        if (next != NULL) {
            *next++ = '\0';
        } else {
            next = line + strlen(line);
        }
        if (*line == '\0') continue;
        snprintf(copy, sizeof copy, "%s", line);
        got[0] = '\0';
        n++;
        if (!run_case(ctx, line, got, &want, &wrote)) {
            g_fail++;
            if (g_fail <= 40) fprintf(stderr, "FAIL unsupported case: %.200s\n", copy);
        } else if (strcmp(got, want) != 0 || wrote) {
            g_fail++;
            if (g_fail <= 40) {
                fprintf(stderr, "FAIL %.200s\n  want %.600s\n  got  %.600s%s\n", copy, want, got,
                        wrote ? "\n  also: a faulting statement delivered bytes" : "");
            }
        } else {
            g_pass++;
        }
    }
    free(text);
    test_direct(ctx);
    cint_ctx_destroy(ctx);
    printf("print cases: %ld, passed %ld, failed %ld\n", n, g_pass, g_fail);
    printf("direct checks: failed %ld\n", g_direct);
    return g_fail == 0 && g_direct == 0 ? 0 : 1;
}
