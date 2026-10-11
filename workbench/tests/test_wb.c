/* test_wb.c: unit tests of the workbench library (box 14 unit 1).
 *
 * Run by workbench/tests/test_workbench.py on each leg; it prints one line per failed check
 * and a summary, and exits 1 when any check failed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wb.h"

static int g_checks, g_failed;

static void check(int ok, const char *what, int line)
{
    g_checks++;
    if (!ok) {
        g_failed++;
        printf("FAIL test_wb.c:%d: %s\n", line, what);
    }
}

#define CHECK(cond) check((cond) != 0, #cond, __LINE__)

static void check_text(const wb_buf *b, const char *want, int line)
{
    int ok = !b->failed && b->p != NULL && strcmp(b->p, want) == 0;
    g_checks++;
    if (!ok) {
        g_failed++;
        printf("FAIL test_wb.c:%d: text differs\n--- got\n%s\n--- want\n%s\n---\n", line,
               b->p != NULL ? b->p : "(null)", want);
    }
}

#define CHECK_TEXT(buf, want) check_text((buf), (want), __LINE__)

/* ------------------------------------------------------------------------- */
/* Multiword integers.                                                        */

static void dec_of(wb_buf *b, const wb_num *x, int group)
{
    b->n = 0u;
    if (b->p != NULL) {
        b->p[0] = '\0';
    }
    wb_num_dec(b, x, group);
}

static void test_num(void)
{
    wb_buf b = {0};
    wb_num x, y;
    uint8_t bytes[520];

    wb_num_from_i64(&x, 0);
    dec_of(&b, &x, 0);
    CHECK_TEXT(&b, "0");
    CHECK(wb_num_bits(&x) == 0u);
    wb_num_from_i64(&x, INT64_MIN);
    dec_of(&b, &x, 0);
    CHECK_TEXT(&b, "-9223372036854775808");
    CHECK(wb_num_bits(&x) == 64u);
    wb_num_from_u64(&x, UINT64_MAX);
    dec_of(&b, &x, 1);
    CHECK_TEXT(&b, "18_446_744_073_709_551_615");
    wb_num_from_i64(&x, -1000);
    dec_of(&b, &x, 1);
    CHECK_TEXT(&b, "-1_000");
    wb_num_from_i64(&x, 999);
    dec_of(&b, &x, 1);
    CHECK_TEXT(&b, "999");
    wb_num_from_u64(&x, 1000000000u);
    dec_of(&b, &x, 0);
    CHECK_TEXT(&b, "1000000000");

    /* Two's complement of every width, least significant byte first. */
    bytes[0] = 0x80u;
    CHECK(wb_num_from_le(&x, bytes, 1u, 1) == 0);
    dec_of(&b, &x, 0);
    CHECK_TEXT(&b, "-128");
    CHECK(wb_num_from_le(&x, bytes, 1u, 0) == 0);
    dec_of(&b, &x, 0);
    CHECK_TEXT(&b, "128");
    memset(bytes, 0xff, 9u);
    CHECK(wb_num_from_le(&x, bytes, 9u, 1) == 0);
    dec_of(&b, &x, 0);
    CHECK_TEXT(&b, "-1");
    CHECK(wb_num_from_le(&x, bytes, 9u, 0) == 0);
    dec_of(&b, &x, 0);
    CHECK_TEXT(&b, "4722366482869645213695");   /* 2^72 - 1 */
    /* 2^4095, the magnitude of the least 4,096-bit value: 512 bytes, top byte 0x80. */
    memset(bytes, 0, 512u);
    bytes[511] = 0x80u;
    CHECK(wb_num_from_le(&x, bytes, 512u, 1) == 0);
    CHECK(x.neg == 1 && wb_num_bits(&x) == 4096u);
    CHECK(wb_num_from_le(&x, bytes, 520u, 0) == 0);
    CHECK(wb_num_from_le(&x, bytes, (size_t)WB_NUM_LIMBS * 4u + 1u, 0) == -1);

    /* Sums of mixed signs. */
    wb_num_from_i64(&x, 5);
    wb_num_from_i64(&y, -7);
    CHECK(wb_num_add(&x, &y) == 0);
    dec_of(&b, &x, 0);
    CHECK_TEXT(&b, "-2");
    wb_num_from_i64(&y, 2);
    CHECK(wb_num_add(&x, &y) == 0);
    dec_of(&b, &x, 0);
    CHECK_TEXT(&b, "0");
    CHECK(x.neg == 0 && x.n == 0u);
    wb_num_from_i64(&x, INT64_MIN);
    wb_num_from_i64(&y, INT64_MIN);
    CHECK(wb_num_add(&x, &y) == 0);
    dec_of(&b, &x, 0);
    CHECK_TEXT(&b, "-18446744073709551616");
    CHECK(wb_num_bits(&x) == 65u);
    wb_num_from_u64(&y, UINT64_MAX);
    CHECK(wb_num_add(&x, &y) == 0);
    dec_of(&b, &x, 0);
    CHECK_TEXT(&b, "-1");
    /* 2^64 sums of UINT64_MAX would need 128 bits; 2^63 of them stay exact. */
    wb_num_from_u64(&x, UINT64_MAX);
    for (int i = 0; i < 63; i++) {
        CHECK(wb_num_mul_small(&x, 2u) == 0);
    }
    dec_of(&b, &x, 0);
    CHECK_TEXT(&b, "170141183460469231722463931679029329920");
    CHECK(wb_num_bits(&x) == 127u);
    wb_num_from_i64(&x, 3);
    wb_num_from_i64(&y, -4);
    CHECK(wb_num_cmp(&x, &y) > 0 && wb_num_cmp(&y, &x) < 0 && wb_num_cmp(&x, &x) == 0);
    wb_num_from_i64(&x, -3);
    CHECK(wb_num_cmp(&x, &y) > 0);

    /* Exact decimals of x / d. */
    wb_num_from_i64(&x, 2434);
    b.n = 0u;
    CHECK(wb_num_dec_over(&b, &x, 100u, 0) == 0);
    CHECK_TEXT(&b, "24.34");
    b.n = 0u;
    wb_num_from_i64(&x, 2400);
    CHECK(wb_num_dec_over(&b, &x, 100u, 0) == 0);
    CHECK_TEXT(&b, "24.0");
    b.n = 0u;
    wb_num_from_i64(&x, -5);
    CHECK(wb_num_dec_over(&b, &x, 8u, 0) == 0);
    CHECK_TEXT(&b, "-0.625");
    b.n = 0u;
    wb_num_from_i64(&x, 1234567);
    CHECK(wb_num_dec_over(&b, &x, 1u, 1) == 0);
    CHECK_TEXT(&b, "1_234_567");
    b.n = 0u;
    CHECK(wb_num_dec_over(&b, &x, 1000u, 1) == 0);
    CHECK_TEXT(&b, "1_234.567");
    b.n = 0u;
    wb_num_from_i64(&x, 0);
    CHECK(wb_num_dec_over(&b, &x, 1000u, 0) == 0);
    CHECK_TEXT(&b, "0.0");
    b.n = 0u;
    wb_num_from_i64(&x, 1);
    CHECK(wb_num_dec_over(&b, &x, (uint64_t)1 << 63, 0) == 0);
    CHECK_TEXT(&b, "0.000000000000000000108420217248550443400745280086994171142578125");
    b.n = 0u;
    if (b.p != NULL) {
        b.p[0] = '\0';
    }
    CHECK(wb_num_dec_over(&b, &x, 3u, 0) == -1 && b.n == 0u);
    CHECK(wb_num_dec_over(&b, &x, 0u, 0) == -1 && b.n == 0u);
    wb_free(&b);
}

/* ------------------------------------------------------------------------- */
/* Fault records.                                                             */

typedef struct enc {
    uint8_t p[8192];
    size_t n;
} enc;

static void e_bytes(enc *e, const void *s, size_t n)
{
    memcpy(e->p + e->n, s, n);
    e->n += n;
}

static void e_u(enc *e, uint64_t v, unsigned k)
{
    for (unsigned i = 0; i < k; i++) {
        e->p[e->n++] = (uint8_t)((v >> (8u * i)) & 0xffu);
    }
}

static void e_text(enc *e, const char *s)
{
    e_u(e, strlen(s), 4u);
    e_bytes(e, s, strlen(s));
}

static void e_i64(enc *e, int64_t v)
{
    e->p[e->n++] = CINT_TAG_I64;
    e_u(e, (uint64_t)v, 8u);
}

static void e_z(enc *e, const uint8_t *le, uint32_t n)
{
    e->p[e->n++] = CINT_TAG_Z;
    e_u(e, n, 4u);
    e_bytes(e, le, n);
}

static void e_pos(enc *e, const char *path, uint32_t line, uint32_t column)
{
    e_text(e, path);
    e_u(e, line, 4u);
    e_u(e, column, 4u);
}

/* An E_OVERFLOW of `op` in encoding `domain` with the I64 operand 2^62, an exact that is a Z of
 * n bytes (n <= 300) and the limit MAX(I1024), at a.ci:1:1 with no revision, source map,
 * address or stack. */
static void z_record(enc *e, const char *domain, const char *op, uint32_t n)
{
    uint8_t z[300];
    memset(z, 0x11, n);
    e->n = 0u;
    e_text(e, domain);
    e_u(e, 1u, 2u);
    e_text(e, op);
    e_u(e, 1u, 4u);
    e_i64(e, INT64_C(1) << 62);
    e_u(e, 1u, 1u);
    e_z(e, z, n);
    e_u(e, 1u, 1u);
    e->p[e->n++] = 0x18u; /* I1024 */
    memset(e->p + e->n, 0xff, 127u);
    e->n += 127u;
    e->p[e->n++] = 0x7fu;
    e_pos(e, "a.ci", 1u, 1u);
    e_u(e, 0u, 1u);
    e_u(e, 0u, 1u);
    e_u(e, 0u, 1u);
    e_u(e, 0u, 4u);
}

/* An E_OVERFLOW of add.checked.i64 at lib.ci:2:14 inside two calls; the revision present, the
 * source map absent, no address. */
static void overflow_record(enc *e)
{
    static const uint8_t exact[] = {0x08, 0, 0, 0, 0, 0, 0, 0x80, 0};   /* 2^63 + 8 */
    uint8_t rev[32];
    for (unsigned i = 0; i < 32u; i++) {
        rev[i] = (uint8_t)(i * 7u);
    }
    e->n = 0u;
    e_text(e, "cint-core-1/fault/v2");
    e_u(e, 1u, 2u);
    e_text(e, "add.checked.i64");
    e_u(e, 2u, 4u);
    e_i64(e, INT64_MAX);
    e_i64(e, 9);
    e_u(e, 1u, 1u);
    e_z(e, exact, sizeof exact);
    e_u(e, 1u, 1u);
    e_i64(e, INT64_MAX);
    e_pos(e, "lib.ci", 2u, 14u);
    e_u(e, 1u, 1u);
    e_bytes(e, rev, 32u);
    e_u(e, 0u, 1u);
    e_u(e, 0u, 1u);
    e_u(e, 2u, 4u);
    e_pos(e, "main.ci", 9u, 5u);
    e_pos(e, "lib.ci", 6u, 12u);
}

static const char LIB[] = "I64 add9(I64 a) {\n    return a + 9;\n}\n\n\nI64 twice(I64 a) {\n"
                          "    return add9(a) + 9;\n}\n";
static const char MAIN[] = "import lib;\n\nvoid main() {\n    I64 x = 1;\n\n\n\n\n    lib.twice(x);\n}\n";

static const uint8_t *sources(void *user, const char *path, size_t len, size_t *n)
{
    (void)user;
    if (len == 6u && memcmp(path, "lib.ci", 6u) == 0) {
        *n = sizeof LIB - 1u;
        return (const uint8_t *)LIB;
    }
    if (len == 7u && memcmp(path, "main.ci", 7u) == 0) {
        *n = sizeof MAIN - 1u;
        return (const uint8_t *)MAIN;
    }
    return NULL;
}

static void test_decode(void)
{
    static wb_fault f;
    enc e;
    char text[1400];
    overflow_record(&e);
    CHECK(wb_fault_decode(e.p, e.n, &f, WB_RECORD_RUN) == 0);
    CHECK(f.code == 1u && strcmp(f.op, "add.checked.i64") == 0 && f.nop == 2u && f.nstack == 2u);
    CHECK(f.has_exact && f.has_limit && f.has_rev && !f.has_map && !f.has_addr);
    CHECK(f.pos.line == 2u && f.pos.column == 14u && f.pos.path_len == 6u);
    CHECK(wb_tval_text(&f.exact, text, sizeof text, 1) == 21u && strcmp(text, "Z 9223372036854775816") == 0);
    CHECK(wb_tval_text(&f.opnd[1], text, sizeof text, 0) == 1u && strcmp(text, "9") == 0);
    CHECK(wb_tval_text(&f.limit, text, 4u, 1) == 3u && strcmp(text, "I64") == 0);   /* cut to fit */

    /* Every proper prefix is refused, and so is a trailing byte. */
    for (size_t k = 0; k < e.n; k++) {
        if (wb_fault_decode(e.p, k, &f, WB_RECORD_RUN) != -1) {
            check(0, "a prefix decoded", __LINE__);
            break;
        }
    }
    e.p[e.n] = 0u;
    CHECK(wb_fault_decode(e.p, e.n + 1u, &f, WB_RECORD_RUN) == -1);

    /* A non-minimal Z (a redundant zero byte above 0x08) and a wrong domain are refused. */
    overflow_record(&e);
    {
        enc bad = e;
        size_t at = 4u + 20u + 2u + 4u + 15u + 4u + 9u + 9u + 1u + 1u + 4u + 8u;   /* the Z's sign byte */
        CHECK(bad.p[at] == 0u && bad.p[at - 1u] == 0x80u);
        bad.p[at - 1u] = 0x08u;   /* now 0x08 then 0x00: the 0x00 is redundant */
        CHECK(wb_fault_decode(bad.p, bad.n, &f, WB_RECORD_RUN) == -1);
        bad = e;
        bad.p[4] = 'C';
        CHECK(wb_fault_decode(bad.p, bad.n, &f, WB_RECORD_RUN) == -1);
        bad = e;
        bad.p[24] = 14u;   /* code 14 does not exist */
        CHECK(wb_fault_decode(bad.p, bad.n, &f, WB_RECORD_RUN) == -1);
        bad = e;
        bad.p[30] = 'A';   /* operations are lowercase */
        CHECK(wb_fault_decode(bad.p, bad.n, &f, WB_RECORD_RUN) == -1);
    }

    /* The Z bound depends on the kind: 300 bytes is a compile-time operand only. */
    {
        uint8_t big[300];
        memset(big, 0x11, sizeof big);
        e.n = 0u;
        e_text(&e, "cint-core-1/fault/v2");
        e_u(&e, 1u, 2u);
        e_text(&e, "mul.checked.i64");
        e_u(&e, 1u, 4u);
        e_z(&e, big, (uint32_t)sizeof big);
        e_u(&e, 0u, 1u);
        e_u(&e, 0u, 1u);
        e_pos(&e, "a.ci", 1u, 1u);
        e_u(&e, 0u, 1u);
        e_u(&e, 0u, 1u);
        e_u(&e, 0u, 1u);
        e_u(&e, 0u, 4u);
        CHECK(wb_fault_decode(e.p, e.n, &f, WB_RECORD_RUN) == -1);
        CHECK(wb_fault_decode(e.p, e.n, &f, WB_RECORD_COMPILE) == 0);
    }

    /* Version 3 (SPEC-01 IM-148, IM-149; BX15-07 C): a run-time record of dot.checked.i1024.<R>
     * holds a Z of up to 264 bytes. Version 2 refuses that operation at run time and version 3
     * every other, and no compile-time record is of version 3. */
    {
        static const char v2[] = "cint-core-1/fault/v2", v3[] = "cint-core-1/fault/v3";
        static const char dot[] = "dot.checked.i1024.i1024", mul[] = "mul.checked.i1024";
        z_record(&e, v3, dot, 264u);
        CHECK(wb_fault_decode(e.p, e.n, &f, WB_RECORD_RUN) == 0 && f.has_exact && f.exact.len == 269u &&
              f.has_limit && f.limit.len == 129u);
        CHECK(wb_tval_text(&f.limit, text, sizeof text, 1) > 6u && memcmp(text, "I1024 8988", 10) == 0);
        CHECK(wb_fault_decode(e.p, e.n, &f, WB_RECORD_COMPILE) == -1);
        z_record(&e, v3, dot, 265u);
        CHECK(wb_fault_decode(e.p, e.n, &f, WB_RECORD_RUN) == -1);
        z_record(&e, v3, mul, 257u);
        CHECK(wb_fault_decode(e.p, e.n, &f, WB_RECORD_RUN) == -1);
        z_record(&e, v3, "dot.checked.i1024.", 257u);
        CHECK(wb_fault_decode(e.p, e.n, &f, WB_RECORD_RUN) == -1);
        z_record(&e, v2, dot, 257u);
        CHECK(wb_fault_decode(e.p, e.n, &f, WB_RECORD_RUN) == -1);
        CHECK(wb_fault_decode(e.p, e.n, &f, WB_RECORD_COMPILE) == 0);
        z_record(&e, v2, mul, 257u);
        CHECK(wb_fault_decode(e.p, e.n, &f, WB_RECORD_RUN) == 0);
        z_record(&e, v2, mul, 258u);
        CHECK(wb_fault_decode(e.p, e.n, &f, WB_RECORD_RUN) == -1);
    }

    /* A kernel address: a work-item index only in the work-item phase. */
    e.n = 0u;
    e_text(&e, "cint-core-1/fault/v2");
    e_u(&e, 3u, 2u);
    e_text(&e, "index.checked");
    e_u(&e, 0u, 4u);
    e_u(&e, 0u, 1u);
    e_u(&e, 0u, 1u);
    e_pos(&e, "k.ci", 4u, 9u);
    e_u(&e, 0u, 1u);
    e_u(&e, 0u, 1u);
    e_u(&e, 1u, 1u);
    e_text(&e, "k.blur");
    e_u(&e, 0u, 8u);
    e_u(&e, 1u, 1u);
    e_u(&e, 1u, 1u);
    e_u(&e, 17u, 8u);
    e_u(&e, (uint64_t)-1, 8u);
    e_u(&e, 0u, 4u);
    CHECK(wb_fault_decode(e.p, e.n, &f, WB_RECORD_RUN) == 0);
    CHECK(f.has_addr && f.has_item && f.item == 17 && f.step == -1 && f.phase == 1u);
    {   /* work-item 17 over the domain [4, 5] is [3, 2], row-major (BX14-13); over [3, 5] it is outside */
        wb_buf b = {0};
        wb_report r = {-1, NULL, NULL, NULL, NULL, 2u, {4, 5, 0, 0}};
        wb_fault_text(&b, &f, &r, NULL, NULL);
        CHECK(b.p != NULL && strstr(b.p, "   = stack-depth: 0\n   = noncanonical: not part of the fault record\n"
                                         "   = work-item-tuple: [3, 2]\n") != NULL);
        wb_free(&b);
        wb_fault_json(&b, &f, &r);
        CHECK(b.p != NULL && strstr(b.p, "\"noncanonical\":{\"work_item_tuple\":[\"3\",\"2\"]},") != NULL);
        wb_free(&b);
        r.extents[0] = 3;
        wb_fault_text(&b, &f, &r, NULL, NULL);
        CHECK(b.p != NULL && strstr(b.p, "noncanonical") == NULL);
        wb_free(&b);
    }
    {
        enc bad = e;
        bad.p[e.n - 4u - 8u - 8u - 1u - 1u] = 2u;   /* the epilogue phase, with an item */
        CHECK(wb_fault_decode(bad.p, bad.n, &f, WB_RECORD_RUN) == -1);
    }
}

static void test_report(void)
{
    static wb_fault f;
    enc e;
    wb_buf b = {0};
    wb_report r = {-1, NULL, NULL, NULL, NULL, 0u, {0, 0, 0, 0}};
    overflow_record(&e);
    CHECK(wb_fault_decode(e.p, e.n, &f, WB_RECORD_RUN) == 0);

    /* The canonical lines alone, as `cint run` prints them; no source for the excerpt. */
    wb_fault_text(&b, &f, NULL, NULL, NULL);
    CHECK_TEXT(&b, "fault[E_OVERFLOW]: the exact result is outside the type's range\n"
                   "  --> lib.ci:2:14\n"
                   "   = operation: add.checked.i64\n"
                   "   = operand-count: 2\n"
                   "   = operand: I64 9223372036854775807\n"
                   "   = operand: I64 9\n"
                   "   = exact:   9223372036854775816\n"
                   "   = limit:   I64 9223372036854775807\n"
                   "   = revision: 00070e151c232a31383f464d545b626970777e858c939aa1a8afb6bdc4cbd2d9\n"
                   "   = source-map: none\n"
                   "   = address: none\n"
                   "   = stack-depth: 2\n"
                   "   = called from: main.ci:9:5\n"
                   "   = called from: lib.ci:6:12\n");

    /* With the sources and every addition (SPEC-06 8.1; BX14-13). */
    wb_free(&b);
    r.fuel = 12;
    r.state = "no store of entry 4 completed; the fault is cleared before the next entry";
    r.site = "add9#1";
    r.position = "branch main, entry 4";
    r.backend = "cpu-sir-interp, which raised the fault itself";
    wb_fault_text(&b, &f, &r, sources, NULL);
    CHECK_TEXT(&b, "fault[E_OVERFLOW]: the exact result is outside the type's range\n"
                   "  --> lib.ci:2:14\n"
                   "   |\n"
                   " 2 |     return a + 9;\n"
                   "   |              ^ add.checked.i64\n"
                   "   |\n"
                   "   = operation: add.checked.i64\n"
                   "   = operand-count: 2\n"
                   "   = operand: I64 9223372036854775807\n"
                   "   = operand: I64 9\n"
                   "   = exact:   9223372036854775816\n"
                   "   = limit:   I64 9223372036854775807\n"
                   "   = revision: 00070e151c232a31383f464d545b626970777e858c939aa1a8afb6bdc4cbd2d9\n"
                   "   = source-map: none\n"
                   "   = address: none\n"
                   "   = stack-depth: 2\n"
                   "   = called from: main.ci:9:5\n"
                   "   = called from: lib.ci:6:12\n"
                   "   = fuel-consumed: 12 (not part of the fault record)\n"
                   "   = state: no store of entry 4 completed; the fault is cleared before the next entry\n"
                   "   = noncanonical: not part of the fault record\n"
                   "   = stack-source: main.ci:9:5\n"
                   "   |\n"
                   " 9 |     lib.twice(x);\n"
                   "   |     ^\n"
                   "   |\n"
                   "   = stack-source: lib.ci:6:12\n"
                   "   |\n"
                   " 6 | I64 twice(I64 a) {\n"
                   "   |            ^\n"
                   "   |\n"
                   "   = workbench-position: branch main, entry 4\n"
                   "   = site: add9#1\n"
                   "   = backend: cpu-sir-interp, which raised the fault itself\n");

    /* A stack entry whose module is not loaded says so. */
    wb_free(&b);
    r.site = r.position = r.backend = NULL;
    r.state = NULL;
    r.fuel = -1;
    wb_fault_text(&b, &f, &r, NULL, NULL);
    CHECK(b.p != NULL && strstr(b.p, "   = stack-source: main.ci:9:5 (source not loaded)\n") != NULL);

    /* The JSON form: CINT-FAULT-1, keys in byte order, additions under noncanonical. */
    wb_free(&b);
    r.fuel = 12;
    r.site = "add9#1";
    r.backend = "cpu-sir-interp";
    wb_fault_json(&b, &f, &r);
    CHECK_TEXT(&b, "{\"address\":null,\"code\":\"E_OVERFLOW\",\"column\":14,"
                   "\"exact\":{\"t\":\"Z\",\"v\":\"9223372036854775816\"},\"file\":\"lib.ci\","
                   "\"limit\":{\"t\":\"I64\",\"v\":\"9223372036854775807\"},\"line\":2,"
                   "\"noncanonical\":{\"backend\":\"cpu-sir-interp\",\"fuel_consumed\":\"12\",\"site\":\"add9#1\"},"
                   "\"operands\":[{\"t\":\"I64\",\"v\":\"9223372036854775807\"},{\"t\":\"I64\",\"v\":\"9\"}],"
                   "\"operation\":\"add.checked.i64\","
                   "\"revision\":\"00070e151c232a31383f464d545b626970777e858c939aa1a8afb6bdc4cbd2d9\","
                   "\"schema\":\"CINT-FAULT-1\",\"severity\":\"fault\",\"source_map\":null,"
                   "\"stack\":[{\"column\":5,\"file\":\"main.ci\",\"line\":9},{\"column\":12,\"file\":\"lib.ci\",\"line\":6}]}");
    wb_free(&b);
    wb_fault_json(&b, &f, NULL);
    CHECK(b.p != NULL && strstr(b.p, "noncanonical") == NULL);
    wb_free(&b);
}

static void test_json_strings(void)
{
    wb_buf b = {0};
    const char s[] = "a\"\\\n\x01\x7f\xc3\xa9\xf0\x9f\x98\x80\xff";
    wb_jstr(&b, s, sizeof s - 1u);
    CHECK_TEXT(&b, "\"a\\\"\\\\\\n\\u0001\x7f\\u00e9\\ud83d\\ude00\\ufffd\"");
    wb_free(&b);
    CHECK(wb_utf8("\xc3\xa9", 2u) && !wb_utf8("\xc0\xaf", 2u) && !wb_utf8("\xed\xa0\x80", 3u) &&
          !wb_utf8("\xe2\x82", 2u));
}

/* ------------------------------------------------------------------------- */
/* The inspector.                                                             */

static wb_tval tv(unsigned tag, int64_t v)
{
    wb_tval t;
    unsigned size = tag == CINT_TAG_BOOL ? 1u : 1u << ((tag & 15u) - 1u);
    memset(&t, 0, sizeof t);
    t.bytes[0] = (uint8_t)tag;
    for (unsigned i = 0; i < size; i++) {
        t.bytes[1u + i] = (uint8_t)(((uint64_t)v >> (8u * i)) & 0xffu);
    }
    t.len = (uint16_t)(size + 1u);
    return t;
}

static int formats(wb_formats *fmt, const char *a, const char *b, const char *c, wb_buf *err)
{
    const char *const argv[3] = {a, b, c};
    int argc = c != NULL ? 3 : b != NULL ? 2 : a != NULL ? 1 : 0;
    return wb_formats_parse(argc, argv, fmt, err);
}

static void test_inspect(void)
{
    wb_buf b = {0}, err = {0};
    wb_formats fmt;
    wb_tval answer = tv(CINT_TAG_I64, 42), subtotal = tv(CINT_TAG_I64, 2434), flag = tv(CINT_TAG_BOOL, 1);
    wb_tval small = tv(CINT_TAG_I8, -2), wide = tv(CINT_TAG_U64, -1);
    wb_field point[2];

    CHECK(wb_value_line(&b, "answer", &answer) == 0);
    CHECK(wb_value_line(&b, NULL, &flag) == 0);
    CHECK_TEXT(&b, "answer = 42 : I64\n= true : Bool\n");
    wb_free(&b);

    CHECK(formats(&fmt, NULL, NULL, NULL, &err) == 0);
    CHECK(wb_inspect_scalar(&b, "answer", &answer, &fmt, &err) == 0);
    CHECK_TEXT(&b, "answer : I64\n  exact   42\n");
    wb_free(&b);

    CHECK(formats(&fmt, "/scale", "100", NULL, &err) == 0);
    CHECK(wb_inspect_scalar(&b, "subtotal", &subtotal, &fmt, &err) == 0);
    CHECK_TEXT(&b, "subtotal : I64   shown with scale 100\n  exact   24.34\n  raw     2434\n");
    wb_free(&b);

    CHECK(formats(&fmt, "/all", NULL, NULL, &err) == 0);
    CHECK(wb_inspect_scalar(&b, "small", &small, &fmt, &err) == 0);
    CHECK_TEXT(&b, "small : I8   (signed, 8 bits)\n"
                   "  exact   -2\n  raw     -2\n  hex     0xfe\n  bin     0b11111110\n  bytes   +0: fe\n");
    wb_free(&b);

    CHECK(formats(&fmt, "/hex", "/bin", "/group", &err) == 0);
    CHECK(wb_inspect_scalar(&b, "wide", &wide, &fmt, &err) == 0);
    CHECK_TEXT(&b, "wide : U64   (unsigned, 64 bits)\n"
                   "  exact   18_446_744_073_709_551_615\n"
                   "  hex     0xffffffffffffffff\n"
                   "  bin     0b11111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111\n");
    wb_free(&b);

    CHECK(formats(&fmt, "/bytes", NULL, NULL, &err) == 0);
    CHECK(wb_inspect_scalar(&b, "subtotal", &subtotal, &fmt, &err) == 0);
    CHECK_TEXT(&b, "subtotal : I64   (signed, 64 bits)\n  exact   2434\n  bytes   +0: 82 09 00 00 00 00 00 00\n");
    wb_free(&b);

    /* A Bool byte that is neither 0 nor 1 is shown and flagged (SPEC-06 6.1, rule 5). */
    flag.bytes[1] = 2u;
    CHECK(formats(&fmt, "/raw", NULL, NULL, &err) == 0);
    CHECK(wb_inspect_scalar(&b, "flag", &flag, &fmt, &err) == 0);
    CHECK_TEXT(&b, "flag : Bool   (1 byte, 0 or 1)\n"
                   "  exact   invalid   FLAG: storage byte 0x02 is neither 0 nor 1\n  raw     2\n");
    wb_free(&b);
    flag.bytes[1] = 1u;

    /* Refusals. */
    CHECK(formats(&fmt, "/scale", "3", NULL, &err) == 0);
    CHECK(wb_inspect_scalar(&b, "subtotal", &subtotal, &fmt, &err) == -1);
    CHECK_TEXT(&err, "/scale 3 has a prime factor other than 2 and 5, so the decimal would not terminate");
    wb_free(&err);
    CHECK(formats(&fmt, "/scale", "100", NULL, &err) == 0);
    CHECK(wb_inspect_scalar(&b, "flag", &flag, &fmt, &err) == -1);
    CHECK_TEXT(&err, "/scale shows integers; this value is a Bool");
    wb_free(&err);
    CHECK(formats(&fmt, "/scale", "0", NULL, &err) == -1);
    wb_free(&err);
    CHECK(formats(&fmt, "/scale", "18446744073709551616", NULL, &err) == -1);
    wb_free(&err);
    CHECK(formats(&fmt, "/scale", NULL, NULL, &err) == -1);
    wb_free(&err);
    CHECK(formats(&fmt, "/stats", NULL, NULL, &err) == -1);
    CHECK_TEXT(&err, "/stats shows an array or a view");
    wb_free(&err);
    CHECK(formats(&fmt, "/trits", NULL, NULL, &err) == -1);
    CHECK_TEXT(&err, "/trits waits for the ternary types");
    wb_free(&err);
    CHECK(formats(&fmt, "/hexx", NULL, NULL, &err) == -1);
    wb_free(&err);

    /* A struct: one line per field by default, a block per field with formats. */
    point[0].name = "x";
    point[0].value = tv(CINT_TAG_I32, 3);
    point[1].name = "on";
    point[1].value = flag;
    CHECK(formats(&fmt, NULL, NULL, NULL, &err) == 0);
    CHECK(wb_inspect_struct(&b, "p", "Point", point, 2u, &fmt, &err) == 0);
    CHECK_TEXT(&b, "p : Point   2 fields\n  .x = 3 : I32\n  .on = true : Bool\n");
    wb_free(&b);
    CHECK(formats(&fmt, "/hex", "/scale", "10", &err) == 0);
    CHECK(wb_inspect_struct(&b, "p", "Point", point, 2u, &fmt, &err) == 0);
    CHECK_TEXT(&b, "p : Point   2 fields\n"
                   "  .x : I32   (signed, 32 bits)   shown with scale 10\n"
                   "    exact   0.3\n    raw     3\n    hex     0x00000003\n"
                   "  .on : Bool   (1 byte, 0 or 1)\n"
                   "    exact   true\n    hex     0x01\n");
    wb_free(&b);
    wb_free(&err);
}

int main(void)
{
    test_num();
    test_json_strings();
    test_decode();
    test_report();
    test_inspect();
    printf("%s test_wb: %d checks, %d failed\n", g_failed == 0 ? "pass" : "FAIL", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
