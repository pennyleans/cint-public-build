/* rt/tests/test_rt.c: tests of cint-rt-3 (rt/cint_rt.h) and the cint_rt library.
 *
 * Usage: test_rt <repository-root> [--no-exh8] [--pairs N]
 *        test_rt --run <scenario> <destination> [<fuel-dest> <state-dest>]
 *        test_rt --record <scenario>
 *
 * What it checks:
 *   1. Every checked and wrapping helper against the CIF-1 records of
 *      conformance/integer-machine/bnd64 (the I64 boundary matrix of SPEC-09
 *      9.3), read at test time, and, unless --no-exh8, against the exhaustive
 *      8-bit tables in conformance/integer-machine/exh8. Saturating files are
 *      skipped (saturating operators are outside cint-boot-1, SPEC-09 SEED-14).
 *   2. The anchor records of conformance/integer-machine/anchors.cif1.jsonl
 *      whose operation the runtime provides (others are counted as skipped).
 *   3. The sticky-fault rule: once a context holds a fault record, every later
 *      checked helper returns false and the record does not change.
 *   4. The canonical bytes of the CONF-01 fault record (SPEC-09 9.2) against a
 *      hex literal derived by hand from SPEC-01 9.2, 11.1 and 11.2 below.
 *   5. Entries, fuel, call depth and the busy flag (SPEC-01 9.4, 10.2;
 *      SPEC-03 A-11).
 *   6. When the builtin helper bodies exist, agreement of the builtin and the
 *      portable bodies on N seeded pseudo-random pairs (SPEC-09 EMIT-05).
 *   7. cint-rt-2 (slice 2 task 2.4): status-returning cint_ctx_create and
 *      cint_ctx_clear_fault, the configured depth limit, the buffer registry
 *      and every refusal of cint_view_bind, the entry faults E_SHAPE and
 *      E_ALIAS, staged print output and exact formatting, and, through
 *      `--run` and `--record` (driven by test_rt.py), cint_program_run.
 *   8. Error results (rt/OPEN.md RT-OQ-33): cint_rt_error_result and
 *      cint_ctx_error, and through `--run`, the error record and the state
 *      destination of cint_program_run_state.
 *   9. The result-returning forms cint_<op>_result_<t> (SPEC-01 IM-30,
 *      IM-188) against the checked forms on edge operands of every type.
 *  10. cint-rt-3 (box 10 unit 2): ABI 3.0, the T3 entry checks of a
 *      cint-abi-1 wrapper, and the registry of SPEC-03 5.3 (descriptor
 *      registration, created buffers, reads, export leases, max_buffers).
 *
 * The JSON reading below handles only the CIF-1 line shape of SPEC-01 13.1
 * (keys in order, every integer a JSON string) and is confined to this test.
 */
#if defined(_MSC_VER)
/* The test uses fopen and sscanf, which MSVC deprecates in favor of its
 * _s variants (warning C4996); the runtime itself uses neither. */
#define _CRT_SECURE_NO_WARNINGS 1
#endif
#include "cint_rt.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#if defined(_MSC_VER)
#pragma comment(lib, "shell32.lib")
#endif
#endif

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static long g_pass;
static long g_fail;
static long g_skip;

#define CHECK(cond, what)                                                         \
    do {                                                                          \
        if (cond) {                                                               \
            g_pass++;                                                             \
        } else {                                                                  \
            g_fail++;                                                             \
            if (g_fail <= 40) {                                                   \
                fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));  \
            }                                                                     \
        }                                                                         \
    } while (0)

/* ------------------------------------------------------------------------- */
/* A program descriptor for the tests: one module, sites numbered from 1.     */

static const cint_site_info t_sites[] = {
    {0u, 0u, NULL},                 /* site 0: no site (SPEC-09 EMIT-23) */
    {5u, 15u, "add.checked.i64"},   /* site 1: the CONF-01 operator */
    {2u, 1u, "fuel.charge"},        /* site 2: an entry or loop charge */
    {7u, 9u, "call.enter"},         /* site 3: a call */
    {9u, 13u, "call.enter"},        /* site 4: a nested call */
};
static const cint_module t_module = {
    "arith/add_i64_overflow.ci", 25u, 5u, t_sites
};
static const cint_module *const t_modules[] = {&t_module};
static const cint_program t_program = {1u, 0u, t_modules, NULL};
static const cint_program t_program_other = {1u, 0u, t_modules, NULL};
/* Root module descriptors for cint_ctx_config.module: record 0 has 16 bytes,
 * record 1 none. */
static const cint_record_layout t_records[2] = {{16u, 8u, 0u, 0u, NULL}, {0u, 0u, 0u, 0u, NULL}};
static const cint_module_info t_module_info = {CINT_ABI_VERSION, 2u, &t_program, t_records};
static const cint_module_info t_module_old = {0x00020000u, 0u, &t_program, NULL};
static const cint_module_info t_module_other = {CINT_ABI_VERSION, 0u, &t_program_other, NULL};

static const cint_site S_OP = {0u, 1u};
static const cint_site S_FUEL = {0u, 2u};
static const cint_site S_CALL = {0u, 3u};
static const cint_site S_CALL2 = {0u, 4u};

static cint_ctx *new_ctx(void)
{
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL;
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = &t_program;
    return cint_ctx_create(&cfg, &ctx) == CINT_OK ? ctx : NULL;
}

static cint_fault_record g_rec;   /* large; kept out of the stack */
static cint_fault_record g_rec2;

static void get_fault(cint_ctx *ctx, cint_fault_record *rec)
{
    cint_status st = cint_ctx_fault(ctx, rec);
    CHECK(st == CINT_OK, "cint_ctx_fault returns CINT_OK outside an entry");
}

/* Renders tagged value `v` as "<Type> <decimal>" into buf. */
static const char *render(const cint_tvalue *v, char *buf, size_t cap)
{
    size_t n = cint_tvalue_render(v, buf, cap);
    if (n == 0u || n >= cap) {
        buf[0] = '\0';
    }
    return buf;
}

static const char *render_dec(const cint_tvalue *v, char *buf, size_t cap)
{
    size_t n = cint_tvalue_render_decimal(v, buf, cap);
    if (n == 0u || n >= cap) {
        buf[0] = '\0';
    }
    return buf;
}

/* ------------------------------------------------------------------------- */
/* CIF-1 reading.                                                             */

typedef struct arg {
    char type[8];       /* "I64", "U8", ... */
    char text[48];      /* decimal as written */
    int negative;
    uint64_t magnitude;
} arg;

typedef struct cif1 {
    char id[64];
    char op[80];
    int nargs;
    arg args[3];
    int is_value;
    char value_type[8];
    char value_text[48];
    char code[24];
    int has_exact;
    char exact[160];
    int has_limit;
    char limit[48];
} cif1;

/* Copies the JSON string that starts right after `key` (which ends in a quote). */
static const char *json_str(const char *s, const char *key, char *out, size_t cap)
{
    const char *p = strstr(s, key);
    size_t n = 0;
    if (p == NULL) {
        return NULL;
    }
    p += strlen(key);
    while (*p != '"' && *p != '\0') {
        if (n + 1u < cap) {
            out[n++] = *p;
        }
        p++;
    }
    out[n] = '\0';
    return *p == '"' ? p + 1 : NULL;
}

static int parse_decimal(const char *t, int *negative, uint64_t *magnitude)
{
    uint64_t m = 0;
    *negative = 0;
    if (*t == '-') {
        *negative = 1;
        t++;
    }
    if (*t == '\0') {
        return 0;
    }
    for (; *t != '\0'; t++) {
        uint64_t d;
        if (*t < '0' || *t > '9') {
            return 0;
        }
        d = (uint64_t)(*t - '0');
        if (m > (UINT64_MAX - d) / 10u) {
            return 0;
        }
        m = m * 10u + d;
    }
    *magnitude = m;
    return 1;
}

static int parse_cif1(const char *line, cif1 *r)
{
    const char *p;
    const char *expect;
    memset(r, 0, sizeof *r);
    if (json_str(line, "\"id\":\"", r->id, sizeof r->id) == NULL ||
        json_str(line, "\"op\":\"", r->op, sizeof r->op) == NULL) {
        return 0;
    }
    p = strstr(line, "\"args\":[");
    expect = strstr(line, "\"expect\":{");
    if (p == NULL || expect == NULL) {
        return 0;
    }
    while (r->nargs < 3) {
        const char *t = strstr(p, "{\"t\":\"");
        arg *a = &r->args[r->nargs];
        if (t == NULL || t > expect) {
            break;
        }
        p = json_str(t, "{\"t\":\"", a->type, sizeof a->type);
        if (p == NULL || strncmp(p, ",\"v\":\"", 6) != 0) {
            return 0;     /* an array or fixed-point argument: not a scalar case */
        }
        p = json_str(p, ",\"v\":\"", a->text, sizeof a->text);
        if (p == NULL || !parse_decimal(a->text, &a->negative, &a->magnitude)) {
            return 0;
        }
        r->nargs++;
    }
    if (strncmp(expect, "\"expect\":{\"value\":{", 19) == 0) {
        r->is_value = 1;
        if (json_str(expect, "{\"t\":\"", r->value_type, sizeof r->value_type) == NULL ||
            json_str(expect, ",\"v\":\"", r->value_text, sizeof r->value_text) == NULL) {
            return 0;
        }
        return 1;
    }
    if (strncmp(expect, "\"expect\":{\"fault\":{", 19) != 0) {
        return 0;
    }
    if (json_str(expect, "\"code\":\"", r->code, sizeof r->code) == NULL) {
        return 0;
    }
    r->has_exact = json_str(expect, "\"exact\":\"", r->exact, sizeof r->exact) != NULL;
    r->has_limit = json_str(expect, "\"limit\":\"", r->limit, sizeof r->limit) != NULL;
    return 1;
}

/* The argument as the 64-bit pattern its type uses (sign- or zero-extended). */
static uint64_t arg_bits(const arg *a)
{
    return a->negative ? (uint64_t)0 - a->magnitude : a->magnitude;
}

static int type_tag(const char *t)
{
    static const char *const names[8] = {"I8", "I16", "I32", "I64", "U8", "U16", "U32", "U64"};
    static const int tags[8] = {CINT_TAG_I8, CINT_TAG_I16, CINT_TAG_I32, CINT_TAG_I64,
                                CINT_TAG_U8, CINT_TAG_U16, CINT_TAG_U32, CINT_TAG_U64};
    int i;
    for (i = 0; i < 8; i++) {
        if (strcmp(t, names[i]) == 0) {
            return tags[i];
        }
    }
    return 0;
}

static cint_count make_count(const arg *a)
{
    uint64_t b = arg_bits(a);
    switch (type_tag(a->type)) {
    case CINT_TAG_I8: return cint_count_i8((int8_t)cint_rt_sext(b, 8u));
    case CINT_TAG_I16: return cint_count_i16((int16_t)cint_rt_sext(b, 16u));
    case CINT_TAG_I32: return cint_count_i32((int32_t)cint_rt_sext(b, 32u));
    case CINT_TAG_I64: return cint_count_i64(cint_rt_sext(b, 64u));
    case CINT_TAG_U8: return cint_count_u8((uint8_t)b);
    case CINT_TAG_U16: return cint_count_u16((uint16_t)b);
    case CINT_TAG_U32: return cint_count_u32((uint32_t)b);
    default: return cint_count_u64(b);
    }
}

/* Outcome of one helper call: ok (value in `bits`, rendered by type) or not ok. */
typedef struct outcome {
    int supported;
    int ok;
    int result_tag;
    uint64_t bits;
} outcome;

#define BINARY_OPS(T, CT, SIGNED)                                                           \
    if (strcmp(ty, #T) == 0 && nargs == 2) {                                                \
        CT a = (CT)(SIGNED ? (CT)cint_rt_sext(arg_bits(&args[0]), (unsigned)(sizeof(CT) * 8u)) \
                           : (CT)arg_bits(&args[0]));                                       \
        CT b = (CT)(SIGNED ? (CT)cint_rt_sext(arg_bits(&args[1]), (unsigned)(sizeof(CT) * 8u)) \
                           : (CT)arg_bits(&args[1]));                                       \
        CT r = 0;                                                                           \
        o.supported = 1;                                                                    \
        o.result_tag = type_tag(args[0].type);                                              \
        if (strcmp(name, "add") == 0 && checked) {                                          \
            o.ok = cint_add_##T(ctx, S_OP, a, b, &r);                                       \
        } else if (strcmp(name, "add") == 0 && wrap) {                                      \
            r = cint_add_wrap_##T(a, b);                                                    \
            o.ok = 1;                                                                       \
        } else if (strcmp(name, "sub") == 0 && checked) {                                   \
            o.ok = cint_sub_##T(ctx, S_OP, a, b, &r);                                       \
        } else if (strcmp(name, "sub") == 0 && wrap) {                                      \
            r = cint_sub_wrap_##T(a, b);                                                    \
            o.ok = 1;                                                                       \
        } else if (strcmp(name, "mul") == 0 && checked) {                                   \
            o.ok = cint_mul_##T(ctx, S_OP, a, b, &r);                                       \
        } else if (strcmp(name, "mul") == 0 && wrap) {                                      \
            r = cint_mul_wrap_##T(a, b);                                                    \
            o.ok = 1;                                                                       \
        } else if (strcmp(name, "div") == 0 && checked) {                                   \
            o.ok = cint_div_##T(ctx, S_OP, a, b, &r);                                       \
        } else if (strcmp(name, "rem") == 0 && checked) {                                   \
            o.ok = cint_rem_##T(ctx, S_OP, a, b, &r);                                       \
        } else if (strcmp(name, "shl") == 0 && checked) {                                   \
            o.ok = cint_shl_##T(ctx, S_OP, a, make_count(&args[1]), &r);                    \
        } else if (strcmp(name, "shl") == 0 && wrap) {                                      \
            o.ok = cint_shl_wrap_##T(ctx, S_OP, a, make_count(&args[1]), &r);               \
        } else if (strcmp(name, "shr") == 0 && checked) {                                   \
            o.ok = cint_shr_##T(ctx, S_OP, a, make_count(&args[1]), &r);                    \
        } else {                                                                            \
            o.supported = 0;                                                                \
        }                                                                                   \
        o.bits = SIGNED ? (uint64_t)(int64_t)r : (uint64_t)r;                               \
        return o;                                                                           \
    }                                                                                       \
    if (strcmp(ty, #T) == 0 && nargs == 1 && strcmp(name, "neg") == 0 && checked) {         \
        CT a = (CT)(SIGNED ? (CT)cint_rt_sext(arg_bits(&args[0]), (unsigned)(sizeof(CT) * 8u)) \
                           : (CT)arg_bits(&args[0]));                                       \
        CT r = 0;                                                                           \
        o.supported = 1;                                                                    \
        o.result_tag = type_tag(args[0].type);                                              \
        o.ok = cint_neg_##T(ctx, S_OP, a, &r);                                              \
        o.bits = SIGNED ? (uint64_t)(int64_t)r : (uint64_t)r;                               \
        return o;                                                                           \
    }

static outcome run_arith(cint_ctx *ctx, const char *name, const char *form, const char *ty,
                         const arg *args, int nargs)
{
    outcome o;
    int checked = strcmp(form, "checked") == 0;
    int wrap = strcmp(form, "wrap") == 0;
    memset(&o, 0, sizeof o);
    BINARY_OPS(i8, int8_t, 1)
    BINARY_OPS(i16, int16_t, 1)
    BINARY_OPS(i32, int32_t, 1)
    BINARY_OPS(i64, int64_t, 1)
    BINARY_OPS(u8, uint8_t, 0)
    BINARY_OPS(u16, uint16_t, 0)
    BINARY_OPS(u32, uint32_t, 0)
    BINARY_OPS(u64, uint64_t, 0)
    return o;
}

/* Conversions: every (source, target) pair, checked and wrapping. */
#define CONV_TO(D, CD, DSIGNED)                                                             \
    if (strcmp(dt, #D) == 0) {                                                              \
        CD r = 0;                                                                           \
        o.supported = 1;                                                                    \
        o.result_tag = dtag;                                                                \
        CONV_FROM(D, CD, i8, int8_t, 1)                                                     \
        CONV_FROM(D, CD, i16, int16_t, 1)                                                   \
        CONV_FROM(D, CD, i32, int32_t, 1)                                                   \
        CONV_FROM(D, CD, i64, int64_t, 1)                                                   \
        CONV_FROM(D, CD, u8, uint8_t, 0)                                                    \
        CONV_FROM(D, CD, u16, uint16_t, 0)                                                  \
        CONV_FROM(D, CD, u32, uint32_t, 0)                                                  \
        CONV_FROM(D, CD, u64, uint64_t, 0)                                                  \
        o.bits = DSIGNED ? (uint64_t)(int64_t)r : (uint64_t)r;                              \
        return o;                                                                           \
    }

#define CONV_FROM(D, CD, S, CS, SSIGNED)                                                    \
    if (strcmp(st, #S) == 0) {                                                              \
        CS x = (CS)(SSIGNED ? (CS)cint_rt_sext(bits, (unsigned)(sizeof(CS) * 8u)) : (CS)bits); \
        if (checked) {                                                                      \
            o.ok = cint_as_##D##_from_##S(ctx, S_OP, x, &r);                                \
        } else {                                                                            \
            r = cint_as_wrap_##D##_from_##S(x);                                             \
            o.ok = 1;                                                                       \
        }                                                                                   \
    }

static outcome run_conv(cint_ctx *ctx, int checked, const char *st, const char *dt, int dtag,
                        uint64_t bits)
{
    outcome o;
    memset(&o, 0, sizeof o);
    CONV_TO(i8, int8_t, 1)
    CONV_TO(i16, int16_t, 1)
    CONV_TO(i32, int32_t, 1)
    CONV_TO(i64, int64_t, 1)
    CONV_TO(u8, uint8_t, 0)
    CONV_TO(u16, uint16_t, 0)
    CONV_TO(u32, uint32_t, 0)
    CONV_TO(u64, uint64_t, 0)
    return o;
}

static void upper(char *dst, const char *src, size_t cap)
{
    size_t i;
    for (i = 0; i + 1u < cap && src[i] != '\0'; i++) {
        char c = src[i];
        dst[i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    }
    dst[i] = '\0';
}

static void bits_text(int tag, uint64_t bits, char *buf, size_t cap)
{
    int is_signed = tag >= CINT_TAG_I8 && tag <= CINT_TAG_I64;
    if (is_signed) {
        snprintf(buf, cap, "%" PRId64, cint_rt_sext(bits, 64u));
    } else {
        snprintf(buf, cap, "%" PRIu64, bits);
    }
}

/* Runs one CIF-1 record. Returns 1 when supported (and checked), 0 when skipped. */
static int run_record(cint_ctx *ctx, const cif1 *r)
{
    char name[24], form[16], t1[8], t2[8];
    char buf[700], want[64];
    outcome o;
    int fields;
    const char *rest = r->op;
    memset(t2, 0, sizeof t2);
    fields = sscanf(rest, "%23[a-z_].%15[a-z].%7[a-z0-9].%7[a-z0-9]", name, form, t1, t2);
    if (fields < 3) {
        return 0;
    }
    if (strcmp(name, "as") == 0) {
        char dt_upper[8];
        if (fields != 4 || r->nargs != 1 || (strcmp(form, "checked") != 0 && strcmp(form, "wrap") != 0)) {
            return 0;
        }
        upper(dt_upper, t2, sizeof dt_upper);
        o = run_conv(ctx, strcmp(form, "checked") == 0, t1, t2, type_tag(dt_upper), arg_bits(&r->args[0]));
    } else {
        if (fields != 3) {
            return 0;
        }
        o = run_arith(ctx, name, form, t1, r->args, r->nargs);
    }
    if (!o.supported) {
        return 0;
    }
    if (r->is_value) {
        bits_text(o.result_tag, o.bits, buf, sizeof buf);
        CHECK(o.ok, r->id);
        if (o.ok) {
            CHECK(strcmp(buf, r->value_text) == 0, r->id);
            CHECK(type_tag(r->value_type) == o.result_tag, r->id);
        } else {
            cint_ctx_clear_fault(ctx);
        }
        return 1;
    }
    CHECK(!o.ok, r->id);
    if (o.ok) {
        return 1;
    }
    get_fault(ctx, &g_rec);
    {
        static const char *const codes[] = {"", "E_OVERFLOW", "E_DIV_ZERO", "E_BOUNDS", "E_SHAPE",
                                            "E_SHIFT", "E_NARROW", "E_ALIAS", "E_STALE_HANDLE",
                                            "E_FUEL", "E_UNSUPPORTED", "E_DOMAIN", "E_DEPTH",
                                            "E_ASSERT"};
        int i;
        CHECK(g_rec.code < 14u && strcmp(codes[g_rec.code], r->code) == 0, r->id);
        CHECK(g_rec.operation_len == strlen(r->op) &&
                  memcmp(g_rec.operation, r->op, g_rec.operation_len) == 0, r->id);
        CHECK(g_rec.operand_count == (uint8_t)r->nargs, r->id);
        for (i = 0; i < r->nargs && i < (int)g_rec.operand_count; i++) {
            snprintf(want, sizeof want, "%s %s", r->args[i].type, r->args[i].text);
            CHECK(strcmp(render(&g_rec.operands[i], buf, sizeof buf), want) == 0, r->id);
        }
        CHECK(g_rec.has_exact == (uint8_t)r->has_exact, r->id);
        if (r->has_exact && g_rec.has_exact) {
            CHECK(strcmp(render_dec(&g_rec.exact, buf, sizeof buf), r->exact) == 0, r->id);
        }
        if (r->has_limit) {
            CHECK(g_rec.has_limit && strcmp(render_dec(&g_rec.limit, buf, sizeof buf), r->limit) == 0, r->id);
        } else if (strcmp(r->code, "E_SHIFT") != 0) {
            CHECK(!g_rec.has_limit, r->id);
        }
        /* A negative shift count: the fixtures assert no limit (ref/OPEN.md O-2);
         * the runtime writes w - 1 as cint_ref does (REF-OQ-04). */
        CHECK(g_rec.position.module == S_OP.module && g_rec.position.index == S_OP.index, r->id);
        CHECK(g_rec.stack_count == 0u, r->id);
    }
    cint_ctx_clear_fault(ctx);
    return 1;
}

static long run_file(cint_ctx *ctx, const char *path, int require_all)
{
    static char line[8192];
    FILE *f = fopen(path, "rb");
    long n = 0, skipped = 0;
    cif1 r;
    if (f == NULL) {
        fprintf(stderr, "FAIL cannot open %s\n", path);
        g_fail++;
        return 0;
    }
    while (fgets(line, (int)sizeof line, f) != NULL) {
        if (line[0] != '{') {
            continue;
        }
        if (!parse_cif1(line, &r)) {
            skipped++;
            continue;
        }
        if (run_record(ctx, &r)) {
            n++;
        } else {
            skipped++;
        }
    }
    fclose(f);
    if (require_all) {
        CHECK(skipped == 0, path);
    }
    g_skip += skipped;
    return n;
}

static void test_tables(const char *root, int exh8)
{
    static const char *const bnd64[] = {
        "add.checked.i64", "add.wrap.i64", "sub.checked.i64", "sub.wrap.i64",
        "mul.checked.i64", "mul.wrap.i64", "div.checked.i64", "rem.checked.i64",
        "shl.checked.i64", "shl.wrap.i64", "shr.checked.i64",
    };
    static const char *const ops8[] = {
        "add.checked", "add.wrap", "sub.checked", "sub.wrap", "mul.checked", "mul.wrap",
        "div.checked", "rem.checked", "shl.checked", "shl.wrap", "shr.checked",
    };
    char path[1024];
    size_t i;
    long n;
    cint_ctx *ctx = new_ctx();
    CHECK(ctx != NULL, "cint_ctx_create");
    if (ctx == NULL) {
        return;
    }
    for (i = 0; i < sizeof bnd64 / sizeof bnd64[0]; i++) {
        snprintf(path, sizeof path, "%s/conformance/integer-machine/bnd64/%s.cif1.jsonl", root, bnd64[i]);
        n = run_file(ctx, path, 1);
        CHECK(n == 256, path);
    }
    printf("bnd64: %d files, 256 records each\n", (int)(sizeof bnd64 / sizeof bnd64[0]));
    if (exh8) {
        long total = 0;
        for (i = 0; i < sizeof ops8 / sizeof ops8[0]; i++) {
            snprintf(path, sizeof path, "%s/conformance/integer-machine/exh8/%s.i8.cif1.jsonl", root, ops8[i]);
            n = run_file(ctx, path, 1);
            CHECK(n == 65536, path);
            total += n;
            snprintf(path, sizeof path, "%s/conformance/integer-machine/exh8/%s.u8.cif1.jsonl", root, ops8[i]);
            n = run_file(ctx, path, 1);
            CHECK(n == 65536, path);
            total += n;
        }
        printf("exh8: %ld records\n", total);
    }
    {
        long skip_before = g_skip;
        snprintf(path, sizeof path, "%s/conformance/integer-machine/anchors.cif1.jsonl", root);
        n = run_file(ctx, path, 0);
        printf("anchors: %ld records run, %ld outside the runtime skipped\n", n, g_skip - skip_before);
        CHECK(n >= 60, "anchors: at least 60 records in scope");
    }
    cint_ctx_destroy(ctx);
}

/* ------------------------------------------------------------------------- */
/* Sticky faults (SPEC-01 9.4).                                               */

static void test_sticky(void)
{
    cint_ctx *ctx = new_ctx();
    int64_t r = 77;
    cint_fault_record before;
    if (ctx == NULL) {
        CHECK(0, "cint_ctx_create");
        return;
    }
    get_fault(ctx, &g_rec);
    CHECK(g_rec.code == 0u, "a new context holds no fault record");
    CHECK(!cint_add_i64(ctx, S_OP, INT64_MAX, 1, &r), "first fault returns false");
    CHECK(r == 77, "out is not written on a fault");
    get_fault(ctx, &g_rec);
    before = g_rec;
    CHECK(g_rec.code == CINT_E_OVERFLOW, "first fault is E_OVERFLOW");
    CHECK(!cint_div_i64(ctx, S_CALL, 5, 0, &r), "second faulting helper returns false");
    get_fault(ctx, &g_rec2);
    CHECK(memcmp(&before, &g_rec2, sizeof before) == 0, "the record is not changed by a second fault");
    CHECK(!cint_add_i64(ctx, S_OP, 1, 1, &r), "a non-faulting helper on a faulted context returns false");
    CHECK(r == 77, "out is not written on a faulted context");
    {
        uint8_t u = 9;
        CHECK(!cint_as_u8_from_i64(ctx, S_OP, 3, &u) && u == 9, "conversions are sticky too");
        CHECK(!cint_shl_u32(ctx, S_OP, 1u, cint_count_i64(1), &(uint32_t){0}), "shifts are sticky too");
    }
    get_fault(ctx, &g_rec2);
    CHECK(memcmp(&before, &g_rec2, sizeof before) == 0, "the record is still the first one");
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, CINT_FUEL_UNBOUNDED, 8) == CINT_FAULTED,
          "entry into a faulted context is refused with CINT_FAULTED");
    cint_ctx_clear_fault(ctx);
    get_fault(ctx, &g_rec2);
    CHECK(g_rec2.code == 0u, "clear_fault clears the record");
    CHECK(cint_add_i64(ctx, S_OP, 1, 1, &r) && r == 2, "helpers work again after clear_fault");
    cint_ctx_destroy(ctx);
}

/* A false return always leaves the context faulted (RT-OQ-20): a count with
 * a tag that is not a scalar tag, and an index operation identifier that does
 * not fit, are refused with E_UNSUPPORTED dispatch.admit, never dropped. */
static void test_bad_args(void)
{
    cint_ctx *ctx = new_ctx();
    cint_count k;
    int64_t r = 12345;
    char op[80];
    uint8_t buf[512];
    if (ctx == NULL) {
        CHECK(0, "cint_ctx_create");
        return;
    }
    memset(&k, 0, sizeof k);
    k.bits = 70u;
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 4) == CINT_OK, "entry begins");
    CHECK(!cint_shl_i64(ctx, S_OP, 1, k, &r) && r == 12345, "a count with tag 0 fails, out unwritten");
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT, "the entry ends faulted, never CINT_OK");
    get_fault(ctx, &g_rec);
    CHECK(g_rec.code == CINT_E_UNSUPPORTED && g_rec.operation_len == 14u &&
          memcmp(g_rec.operation, "dispatch.admit", 14u) == 0, "the record is E_UNSUPPORTED dispatch.admit");
    CHECK(cint_fault_encode(&g_rec, buf, sizeof buf) > 0u, "the record encodes");
    cint_ctx_clear_fault(ctx);
    memset(op, 'a', sizeof op);
    memcpy(op, "index.checked.", 14u);
    op[70] = '\0';
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 4) == CINT_OK, "entry begins again");
    CHECK(!cint_index_check(ctx, S_OP, op, 5, 3), "an index check with a 70-byte identifier fails");
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT, "and the entry ends faulted");
    get_fault(ctx, &g_rec);
    CHECK(g_rec.code == CINT_E_UNSUPPORTED && g_rec.operation_len == 14u, "the identifier is refused, not dropped");
    g_rec.operation_len = 0u;
    CHECK(cint_fault_encode(&g_rec, buf, sizeof buf) == 0u, "a record with an empty operation does not encode");
    cint_ctx_destroy(ctx);
}

/* ------------------------------------------------------------------------- */
/* Canonical bytes of the CONF-01 fault record (SPEC-09 9.2 CONF-01).          */

/* Derivation by hand. Layout: SPEC-01 11.2 (content 9.2); values: 11.1. All
 * integers little-endian (11.1). The record is the CONF-01 example: code
 * E_OVERFLOW, operation add.checked.i64, operands I64 9223372036854775807 and
 * I64 1, exact 9223372036854775808, limit I64 9223372036854775807, position
 * arith/add_i64_overflow.ci:5:15, revision absent (the seed builds no SIR,
 * SEED-05), source-map digest absent, address none, stack empty (fault in the
 * entry; SPEC-01 IM-107). Version 2 of the layout (SPEC-01 IM-149; slice 2
 * patch D-10): an absent revision or source-map digest is one presence byte
 * 00, never 32 zero bytes (OQ-159).
 *
 *   domain string: U32 length 20 = 14 00 00 00, then ASCII
 *     "cint-core-1/fault/v2" = 63 69 6e 74 2d 63 6f 72 65 2d 31 2f 66 61 75 6c 74 2f 76 32
 *   code: U16, E_OVERFLOW is number 1 (9.1)          = 01 00
 *   operation: U32 length 15 = 0f 00 00 00, then
 *     "add.checked.i64" = 61 64 64 2e 63 68 65 63 6b 65 64 2e 69 36 34
 *   operand count: U32 2                              = 02 00 00 00
 *   operand 1, tagged I64 2^63-1: tag 14, 8 bytes     = 14 ff ff ff ff ff ff ff 7f
 *   operand 2, tagged I64 1                           = 14 01 00 00 00 00 00 00 00
 *   exact: presence 01, tagged Z 2^63: tag 0f, U32 length 9 (minimal two's
 *     complement needs a 00 sign byte above 0x80), bytes 00 x7, 80, 00
 *                                                     = 01 0f 09 00 00 00 00 00 00 00 00 00 00 80 00
 *     (the 11.1 table lists Z 9223372036854775808 as 0f 09 00 00 00 00 00 00 00 00 00 00 80 00)
 *   limit: presence 01, tagged I64 2^63-1             = 01 14 ff ff ff ff ff ff ff 7f
 *   position: path U32 length 25 = 19 00 00 00, then "arith/add_i64_overflow.ci"
 *     = 61 72 69 74 68 2f 61 64 64 5f 69 36 34 5f 6f 76 65 72 66 6c 6f 77 2e 63 69,
 *     line U32 5 = 05 00 00 00, column U32 15 = 0f 00 00 00
 *   revision: presence 00
 *   source-map digest: presence 00
 *   address: presence 00
 *   stack: count U32 0                                = 00 00 00 00
 *
 * Total: 24 + 2 + 19 + 4 + 9 + 9 + 15 + 10 + 37 + 1 + 1 + 1 + 4 = 136 bytes.
 * (Version 1 wrote the revision as 32 bytes with no presence byte and had no
 * source-map digest: 166 bytes.)
 */
static const char conf01_hex[] =
    "14000000" "63696e742d636f72652d312f6661756c742f7632"
    "0100"
    "0f000000" "6164642e636865636b65642e693634"
    "02000000"
    "14ffffffffffffff7f"
    "140100000000000000"
    "01" "0f09000000" "000000000000008000"
    "01" "14ffffffffffffff7f"
    "19000000" "61726974682f6164645f6936345f6f766572666c6f772e6369" "05000000" "0f000000"
    "00"
    "00"
    "00"
    "00000000";

static size_t from_hex(const char *hex, uint8_t *out, size_t cap)
{
    size_t n = 0;
    while (hex[0] != '\0' && hex[1] != '\0' && n < cap) {
        unsigned v = 0;
        int i;
        for (i = 0; i < 2; i++) {
            char c = hex[i];
            v = v * 16u + (unsigned)(c <= '9' ? c - '0' : c - 'a' + 10);
        }
        out[n++] = (uint8_t)v;
        hex += 2;
    }
    return n;
}

static void test_encoder(void)
{
    static uint8_t want[256], got[256];
    size_t want_len = from_hex(conf01_hex, want, sizeof want);
    size_t len;
    int64_t r = 0;
    cint_ctx *ctx = new_ctx();
    char buf[64];
    if (ctx == NULL) {
        CHECK(0, "cint_ctx_create");
        return;
    }
    CHECK(want_len == 136u, "the hand-derived CONF-01 record is 136 bytes");
    CHECK(!cint_add_i64(ctx, S_OP, INT64_MAX, 1, &r), "CONF-01 overflow");
    get_fault(ctx, &g_rec);
    len = cint_fault_encode(&g_rec, NULL, 0u);
    CHECK(len == want_len, "encoded length (query) is 136");
    memset(got, 0xAA, sizeof got);
    CHECK(cint_fault_encode(&g_rec, got, 10u) == want_len && got[0] == 0xAA,
          "a short buffer receives nothing and the length is reported");
    len = cint_fault_encode(&g_rec, got, sizeof got);
    CHECK(len == want_len && memcmp(got, want, want_len) == 0, "CONF-01 bytes equal the hand derivation");
    if (len != want_len || memcmp(got, want, want_len) != 0) {
        size_t i;
        fprintf(stderr, "got:  ");
        for (i = 0; i < len && i < sizeof got; i++) {
            fprintf(stderr, "%02x", got[i]);
        }
        fprintf(stderr, "\n");
    }
    /* SPEC-01 11.1 value table, through records the helpers produce. */
    {
        static const uint8_t z_2_63[] = {0x0f, 0x09, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x80, 0x00};
        static const uint8_t i64_max[] = {0x14, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f};
        static const uint8_t z_m1[] = {0x0f, 0x01, 0, 0, 0, 0xff};
        static const uint8_t u8_255[] = {0x21, 0xff};
        static const uint8_t i64_m2[] = {0x14, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
        static const uint8_t i64_1000[] = {0x14, 0xe8, 0x03, 0, 0, 0, 0, 0, 0};
        uint8_t u = 0;
        CHECK(g_rec.exact.len == sizeof z_2_63 && memcmp(g_rec.exact.bytes, z_2_63, sizeof z_2_63) == 0,
              "Z 2^63 bytes (SPEC-01 11.1)");
        CHECK(g_rec.limit.len == sizeof i64_max && memcmp(g_rec.limit.bytes, i64_max, sizeof i64_max) == 0,
              "I64 MAX bytes");
        cint_ctx_clear_fault(ctx);
        CHECK(!cint_sub_u8(ctx, S_OP, 0u, 1u, &u), "U8 0 - 1 faults");
        get_fault(ctx, &g_rec);
        CHECK(g_rec.exact.len == sizeof z_m1 && memcmp(g_rec.exact.bytes, z_m1, sizeof z_m1) == 0,
              "Z -1 bytes (SPEC-01 11.1)");
        cint_ctx_clear_fault(ctx);
        CHECK(!cint_add_u8(ctx, S_OP, 255u, 1u, &u), "U8 255 + 1 faults");
        get_fault(ctx, &g_rec);
        CHECK(g_rec.operands[0].len == sizeof u8_255 && memcmp(g_rec.operands[0].bytes, u8_255, 2) == 0,
              "U8 255 bytes (SPEC-01 11.1)");
        cint_ctx_clear_fault(ctx);
        CHECK(!cint_mul_i64(ctx, S_OP, -2, INT64_MAX, &r), "I64 -2 * MAX faults");
        get_fault(ctx, &g_rec);
        CHECK(g_rec.operands[0].len == sizeof i64_m2 &&
                  memcmp(g_rec.operands[0].bytes, i64_m2, sizeof i64_m2) == 0, "I64 -2 bytes (SPEC-01 11.1)");
        CHECK(strcmp(render_dec(&g_rec.exact, buf, sizeof buf), "-18446744073709551614") == 0,
              "exact of -2 * MAX");
        CHECK(strcmp(render(&g_rec.limit, buf, sizeof buf), "I64 -9223372036854775808") == 0,
              "limit of a negative overflow is MIN");
        cint_ctx_clear_fault(ctx);
        CHECK(!cint_div_i64(ctx, S_OP, 1000, 0, &r), "1000 / 0 faults");
        get_fault(ctx, &g_rec);
        CHECK(g_rec.operands[0].len == sizeof i64_1000 &&
                  memcmp(g_rec.operands[0].bytes, i64_1000, sizeof i64_1000) == 0, "I64 1000 bytes (SPEC-01 11.1)");
        CHECK(g_rec.code == CINT_E_DIV_ZERO && !g_rec.has_exact && !g_rec.has_limit, "E_DIV_ZERO has no exact or limit");
    }
    /* A record with a stack: the encoder writes each position after the count. */
    {
        static const uint8_t tail[] = {
            0x00,                                   /* address: absent */
            0x01, 0x00, 0x00, 0x00,                 /* stack count 1 */
            0x19, 0x00, 0x00, 0x00,                 /* path length 25 */
            'a', 'r', 'i', 't', 'h', '/', 'a', 'd', 'd', '_', 'i', '6', '4', '_',
            'o', 'v', 'e', 'r', 'f', 'l', 'o', 'w', '.', 'c', 'i',
            0x07, 0x00, 0x00, 0x00,                 /* line 7 */
            0x09, 0x00, 0x00, 0x00,                 /* column 9 */
        };
        cint_ctx_clear_fault(ctx);
        CHECK(cint_rt_entry_begin(ctx, S_FUEL, CINT_FUEL_UNBOUNDED, 2) == CINT_OK, "entry for the stack case");
        CHECK(cint_rt_call_enter(ctx, S_CALL), "call at site 3");
        CHECK(!cint_add_i64(ctx, S_OP, INT64_MAX, 1, &r), "fault in the callee");
        CHECK(cint_rt_entry_end(ctx) == CINT_FAULT, "entry ends with CINT_FAULT");
        get_fault(ctx, &g_rec);
        CHECK(g_rec.stack_count == 1u && g_rec.stack[0].index == 3u, "stack holds the call site");
        len = cint_fault_encode(&g_rec, got, sizeof got);
        CHECK(len == want_len + 37u, "a stack position adds 4 + 25 + 4 + 4 = 37 bytes");
        CHECK(len >= sizeof tail && memcmp(got + len - sizeof tail, tail, sizeof tail) == 0, "stack tail bytes");
    }
    cint_ctx_destroy(ctx);
    /* A present revision, in a fresh context: presence 01 and the 32 bytes,
     * then the source-map presence byte 00 (SPEC-01 IM-149 version 2). In the
     * CONF-01 record the revision presence byte is the 7th byte from the end
     * (revision, source map, address, then the 4-byte stack count). */
    ctx = new_ctx();
    if (ctx != NULL) {
        size_t at = want_len - 4u - 1u - 1u - 1u;
        uint32_t k;
        CHECK(!cint_add_i64(ctx, S_OP, INT64_MAX, 1, &r), "CONF-01 overflow again");
        get_fault(ctx, &g_rec);
        g_rec.has_revision = 1u;
        for (k = 0; k < 32u; k++) {
            g_rec.revision[k] = (uint8_t)(0xa0u + k);
        }
        len = cint_fault_encode(&g_rec, got, sizeof got);
        CHECK(len == want_len + 32u && memcmp(got, want, at) == 0 && got[at] == 0x01u &&
                  memcmp(got + at + 1u, g_rec.revision, 32u) == 0 && got[at + 33u] == 0x00u &&
                  memcmp(got + at + 34u, want + at + 2u, want_len - at - 2u) == 0,
              "a present revision is presence 01 and 32 bytes; the source-map digest stays absent");
        /* What the IM-148 reader rejects is not encoded (review of 2026-10-03). */
        memcpy(g_rec.operation, "DIV", 3u);
        g_rec.operation_len = 3u;
        CHECK(cint_fault_encode(&g_rec, got, sizeof got) == 0u, "an operation outside [a-z0-9._] does not encode");
        memcpy(g_rec.operation, "div", 3u);
        CHECK(cint_fault_encode(&g_rec, got, sizeof got) > 0u, "a lowercase operation encodes");
        g_rec.has_address = 1u;
        memcpy(g_rec.address.name, "k", 1u);
        g_rec.address.name_len = 1u;
        g_rec.address.phase = 0u;
        g_rec.address.has_work_item = 1u;
        CHECK(cint_fault_encode(&g_rec, got, sizeof got) == 0u, "a work item in phase 0 does not encode");
        g_rec.address.phase = 7u;
        g_rec.address.has_work_item = 0u;
        CHECK(cint_fault_encode(&g_rec, got, sizeof got) == 0u, "phase 7 does not encode");
        g_rec.address.phase = 1u;
        g_rec.address.has_work_item = 1u;
        CHECK(cint_fault_encode(&g_rec, got, sizeof got) > 0u, "a work item in phase 1 encodes");
        cint_ctx_destroy(ctx);
    }
}

/* ------------------------------------------------------------------------- */
/* Entries, fuel, depth and the busy flag.                                    */

static void test_entries(void)
{
    cint_ctx *ctx = new_ctx();
    int64_t used = -5;
    char buf[64];
    if (ctx == NULL) {
        CHECK(0, "cint_ctx_create");
        return;
    }
    CHECK(cint_abi_version() == CINT_ABI_VERSION, "ABI version");
    /* Fuel 0: the entry's own charge faults (SPEC-01 10.2). */
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 0, 8) == CINT_FAULT, "fuel 0 faults at the entry charge");
    get_fault(ctx, &g_rec);
    CHECK(g_rec.code == CINT_E_FUEL, "E_FUEL");
    CHECK(g_rec.operation_len == 11u && memcmp(g_rec.operation, "fuel.charge", 11u) == 0, "fuel.charge");
    CHECK(g_rec.operand_count == 0u && !g_rec.has_exact && g_rec.has_limit, "E_FUEL fields");
    CHECK(strcmp(render(&g_rec.limit, buf, sizeof buf), "I64 0") == 0, "E_FUEL limit is the allowance");
    CHECK(g_rec.position.index == S_FUEL.index && g_rec.stack_count == 0u, "E_FUEL position");
    CHECK(cint_fuel_consumed(ctx, &used) == CINT_OK && used == 0, "a charge that faults is not counted");
    cint_ctx_clear_fault(ctx);

    /* Loop charges: allowance 3 = entry + 2 iterations; the third iteration faults. */
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 3, 8) == CINT_OK, "entry with fuel 3");
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 3, 8) == CINT_BUSY, "a second entry while busy gets CINT_BUSY");
    CHECK(cint_ctx_fault(ctx, &g_rec) == CINT_BUSY, "cint_ctx_fault during an entry gets CINT_BUSY");
    CHECK(cint_fuel_consumed(ctx, &used) == CINT_BUSY, "cint_fuel_consumed during an entry gets CINT_BUSY");
    CHECK(cint_fuel_charge(ctx, S_FUEL, 1u), "iteration 1");
    CHECK(cint_fuel_charge(ctx, S_FUEL, 1u), "iteration 2");
    CHECK(!cint_fuel_charge(ctx, S_FUEL, 1u), "iteration 3 faults E_FUEL");
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT, "entry ends faulted");
    CHECK(cint_fuel_consumed(ctx, &used) == CINT_OK && used == 3, "fuel consumed 3");
    get_fault(ctx, &g_rec);
    CHECK(g_rec.code == CINT_E_FUEL && strcmp(render(&g_rec.limit, buf, sizeof buf), "I64 3") == 0, "limit 3");
    cint_ctx_clear_fault(ctx);
    CHECK(cint_fuel_consumed(ctx, &used) == CINT_OK && used == 3, "clear_fault changes no counter (A-7a)");

    /* Depth: D = 2 admits one call from the entry; the second nested call faults. */
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 2) == CINT_OK, "entry with depth 2");
    CHECK(cint_rt_call_enter(ctx, S_CALL), "call to depth 2");
    CHECK(!cint_rt_call_enter(ctx, S_CALL2), "call to depth 3 faults E_DEPTH");
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT, "entry ends faulted");
    CHECK(cint_fuel_consumed(ctx, &used) == CINT_OK && used == 2, "E_DEPTH consumes no fuel");
    get_fault(ctx, &g_rec);
    CHECK(g_rec.code == CINT_E_DEPTH, "E_DEPTH");
    CHECK(g_rec.operation_len == 10u && memcmp(g_rec.operation, "call.enter", 10u) == 0, "call.enter");
    CHECK(strcmp(render(&g_rec.limit, buf, sizeof buf), "I64 2") == 0, "E_DEPTH limit is D");
    CHECK(g_rec.position.index == S_CALL2.index, "E_DEPTH position is the call");
    CHECK(g_rec.stack_count == 1u && g_rec.stack[0].index == S_CALL.index, "stack holds the active call");
    cint_ctx_clear_fault(ctx);

    /* Leaving calls restores depth. */
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, CINT_FUEL_UNBOUNDED, 2) == CINT_OK, "entry");
    CHECK(cint_rt_call_enter(ctx, S_CALL), "call");
    cint_rt_call_leave(ctx);
    CHECK(cint_rt_call_enter(ctx, S_CALL2), "call again after leave");
    cint_rt_call_leave(ctx);
    CHECK(cint_rt_entry_end(ctx) == CINT_OK, "entry ends OK");
    CHECK(cint_fuel_consumed(ctx, &used) == CINT_OK && used == 3, "fuel 3 with no allowance");

    /* Invalid budgets are refused; a depth above the runtime's capacity is E_UNSUPPORTED. */
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, -2, 8) == CINT_REFUSED, "negative allowance refused");
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 10, -1) == CINT_REFUSED, "negative depth refused");
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 10, INT64_MIN) == CINT_REFUSED, "depth INT64_MIN refused");
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 10, (int64_t)CINT_RT_MAX_DEPTH + 1) == CINT_FAULT,
          "depth above CINT_RT_MAX_DEPTH faults");
    get_fault(ctx, &g_rec);
    CHECK(g_rec.code == CINT_E_UNSUPPORTED, "E_UNSUPPORTED for a depth the runtime cannot provide");
    cint_ctx_clear_fault(ctx);
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 10, (int64_t)CINT_RT_MAX_DEPTH) == CINT_OK, "maximum depth admitted");
    CHECK(cint_rt_entry_end(ctx) == CINT_OK, "end");

    /* Index checks (provisional record shape, rt/OPEN.md RT-OQ-05). */
    CHECK(cint_index_check(ctx, S_OP, "index.checked.i64", 0, 1), "index 0 of 1");
    CHECK(!cint_index_check(ctx, S_OP, "index.checked.i64", 1, 1), "index 1 of 1 faults");
    get_fault(ctx, &g_rec);
    CHECK(g_rec.code == CINT_E_BOUNDS && strcmp(render(&g_rec.limit, buf, sizeof buf), "I64 1") == 0, "E_BOUNDS");
    cint_ctx_clear_fault(ctx);
    cint_ctx_destroy(ctx);
}

/* ------------------------------------------------------------------------- */
/* Hand-checked cases outside the tables: count types, conversions (EMIT-11),  */
/* shifts (EMIT-10), floor division (SPEC-01 4.4).                            */

static void test_hand(void)
{
    cint_ctx *ctx = new_ctx();
    char buf[64];
    int64_t r = 0;
    if (ctx == NULL) {
        CHECK(0, "cint_ctx_create");
        return;
    }
    CHECK(!cint_shl_i64(ctx, S_OP, 1, cint_count_u64(64u), &r), "1 << 64 (U64 count) faults");
    get_fault(ctx, &g_rec);
    CHECK(g_rec.code == CINT_E_SHIFT, "E_SHIFT");
    CHECK(strcmp(render(&g_rec.operands[1], buf, sizeof buf), "U64 64") == 0, "the count keeps its type");
    CHECK(strcmp(render(&g_rec.limit, buf, sizeof buf), "I64 63") == 0, "limit I64 63 (REF-OQ-04)");
    cint_ctx_clear_fault(ctx);
    CHECK(!cint_shr_i64(ctx, S_OP, 1, cint_count_i8(-1), &r), "x >> -1 faults");
    get_fault(ctx, &g_rec);
    CHECK(strcmp(render(&g_rec.operands[1], buf, sizeof buf), "I8 -1") == 0, "negative I8 count operand");
    cint_ctx_clear_fault(ctx);
    CHECK(!cint_shl_i64(ctx, S_OP, 1, cint_count_u64(UINT64_MAX), &r), "count 2^64-1 faults");
    cint_ctx_clear_fault(ctx);
    CHECK(cint_shl_i64(ctx, S_OP, -1, cint_count_u8(63u), &r) && r == INT64_MIN, "-1 << 63 (EMIT-10)");
    CHECK(!cint_shl_i64(ctx, S_OP, 1, cint_count_u8(63u), &r), "1 << 63 overflows (EMIT-10)");
    cint_ctx_clear_fault(ctx);
    {
        uint64_t u = 0;
        int8_t s8 = 0;
        CHECK(cint_shl_u64(ctx, S_OP, 1u, cint_count_i32(63), &u) && u == (uint64_t)1 << 63, "U64 1 << 63");
        CHECK(!cint_shl_i8(ctx, S_OP, 64, cint_count_i64(1), &s8), "I8 64 << 1 overflows (EMIT-10)");
        cint_ctx_clear_fault(ctx);
        CHECK(cint_shr_i64(ctx, S_OP, -7, cint_count_i64(1), &r) && r == -4, "-7 >> 1 = -4");
        CHECK(cint_shr_i64(ctx, S_OP, -1, cint_count_i64(63), &r) && r == -1, "-1 >> 63 = -1");
        CHECK(cint_shr_u64(ctx, S_OP, UINT64_MAX, cint_count_i64(63), &u) && u == 1u, "U64 MAX >> 63 = 1");
    }
    {
        uint64_t u = 0;
        int64_t s = 0;
        int8_t s8 = 0;
        uint8_t u8 = 0;
        CHECK(!cint_as_u64_from_i32(ctx, S_OP, -1, &u), "(-1 as U64) from I32 (EMIT-11)");
        get_fault(ctx, &g_rec);
        CHECK(g_rec.code == CINT_E_NARROW, "E_NARROW");
        CHECK(g_rec.operation_len == 18u && memcmp(g_rec.operation, "as.checked.i32.u64", 18u) == 0, "op id");
        CHECK(strcmp(render(&g_rec.operands[0], buf, sizeof buf), "I32 -1") == 0, "operand");
        CHECK(strcmp(render(&g_rec.limit, buf, sizeof buf), "U64 0") == 0, "limit U64 0");
        cint_ctx_clear_fault(ctx);
        CHECK(!cint_as_u64_from_i64(ctx, S_OP, -1, &u), "(-1 as U64) from I64");
        cint_ctx_clear_fault(ctx);
        CHECK(!cint_as_i64_from_u64(ctx, S_OP, (uint64_t)1 << 63, &s), "U64 2^63 as I64");
        cint_ctx_clear_fault(ctx);
        CHECK(cint_as_i64_from_u64(ctx, S_OP, ((uint64_t)1 << 63) - 1u, &s) && s == INT64_MAX, "2^63-1 as I64");
        CHECK(cint_as_i8_from_i64(ctx, S_OP, -128, &s8) && s8 == -128, "-128 as I8");
        CHECK(!cint_as_i8_from_u8(ctx, S_OP, 255u, &s8), "U8 255 as I8");
        cint_ctx_clear_fault(ctx);
        CHECK(cint_as_wrap_i32_from_i64(2147483648LL) == INT32_MIN, "2^31 as% I32");
        CHECK(cint_as_wrap_u64_from_i64(-1) == UINT64_MAX, "-1 as% U64");
        CHECK(cint_as_wrap_i64_from_u64((uint64_t)1 << 63) == INT64_MIN, "2^63 as% I64");
        CHECK(cint_as_wrap_i8_from_i64(-129) == 127, "-129 as% I8");
        CHECK(cint_as_u8_from_i16(ctx, S_OP, 255, &u8) && u8 == 255u, "I16 255 as U8");
    }
    {
        /* SPEC-01 4.4 table rows for / and %. */
        static const int64_t rows[][4] = {
            {7, 2, 3, 1}, {-7, 2, -4, 1}, {7, -2, -4, -1}, {-7, -2, 3, -1}, {6, -3, -2, 0},
            {INT64_MIN, 2, -4611686018427387904LL, 0}, {INT64_MIN, 3, -3074457345618258603LL, 1},
        };
        size_t i;
        for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
            int64_t q = 0, m = 0;
            CHECK(cint_div_i64(ctx, S_OP, rows[i][0], rows[i][1], &q) && q == rows[i][2], "floor quotient");
            CHECK(cint_rem_i64(ctx, S_OP, rows[i][0], rows[i][1], &m) && m == rows[i][3], "floor remainder");
        }
        CHECK(cint_rem_i64(ctx, S_OP, INT64_MIN, -1, &r) && r == 0, "MIN % -1 = 0");
        CHECK(!cint_div_i64(ctx, S_OP, INT64_MIN, -1, &r), "MIN / -1 overflows");
        get_fault(ctx, &g_rec);
        CHECK(strcmp(render_dec(&g_rec.exact, buf, sizeof buf), "9223372036854775808") == 0, "exact 2^63");
        cint_ctx_clear_fault(ctx);
    }
    {
        /* SPEC-01 4.2 and 4.3 tables. */
        int32_t i32 = 0;
        int8_t i8 = 0;
        uint8_t u8 = 0;
        uint32_t u32 = 0;
        CHECK(cint_add_wrap_i64(INT64_MAX, 1) == INT64_MIN, "M64 +% 1");
        CHECK(cint_sub_wrap_i64(INT64_MIN, 1) == INT64_MAX, "m64 -% 1");
        CHECK(cint_sub_wrap_u8(3u, 5u) == 254u, "U8 3 -% 5");
        CHECK(cint_mul_wrap_i64(INT64_MIN, -1) == INT64_MIN, "m64 *% -1");
        CHECK(!cint_mul_i32(ctx, S_OP, 65536, 32768, &i32), "65536 * 32768 overflows I32");
        cint_ctx_clear_fault(ctx);
        CHECK(cint_mul_i32(ctx, S_OP, -65536, 32768, &i32) && i32 == INT32_MIN, "-65536 * 32768");
        CHECK(cint_mul_wrap_i8(100, 3) == 44, "I8 100 *% 3");
        CHECK(cint_add_wrap_i8(100, 100) == -56, "I8 100 +% 100 (fixture 99)");
        CHECK(cint_mul_wrap_u16(65535u, 65535u) == 1u, "U16 65535 *% 65535 (EMIT-08)");
        CHECK(!cint_neg_i64(ctx, S_OP, INT64_MIN, &r), "-m64 overflows");
        cint_ctx_clear_fault(ctx);
        CHECK(cint_neg_wrap_i64(INT64_MIN) == INT64_MIN, "0 -% m64");
        CHECK(!cint_neg_u32(ctx, S_OP, 1u, &u32), "-1 on U32 overflows");
        get_fault(ctx, &g_rec);
        CHECK(strcmp(render_dec(&g_rec.exact, buf, sizeof buf), "-1") == 0, "exact -1");
        cint_ctx_clear_fault(ctx);
        CHECK(cint_neg_u8(ctx, S_OP, 0u, &u8) && u8 == 0u, "-0 on U8");
        CHECK(cint_neg_wrap_u8(1u) == 255u, "0 -% 1 on U8");
        CHECK(cint_neg_i8(ctx, S_OP, -127, &i8) && i8 == 127, "-(-127) on I8");
    }
    {
        /* Unsigned 64-bit multiply exact needs 129 bits. */
        uint64_t u = 0;
        CHECK(!cint_mul_u64(ctx, S_OP, UINT64_MAX, UINT64_MAX, &u), "U64 MAX * MAX overflows");
        get_fault(ctx, &g_rec);
        CHECK(strcmp(render_dec(&g_rec.exact, buf, sizeof buf), "340282366920938463426481119284349108225") == 0,
              "exact (2^64-1)^2");
        cint_ctx_clear_fault(ctx);
    }
    {
        cint_position pos;
        CHECK(cint_site_resolve(&t_program, S_OP, &pos), "site resolves");
        CHECK(pos.line == 5u && pos.column == 15u && pos.path_len == 25u &&
                  memcmp(pos.path, "arith/add_i64_overflow.ci", 25u) == 0 &&
                  strcmp(pos.operation, "add.checked.i64") == 0, "site fields");
        CHECK(!cint_site_resolve(&t_program, (cint_site){0u, 0u}, &pos), "site 0 does not resolve");
        CHECK(!cint_site_resolve(&t_program, (cint_site){0u, 5u}, &pos), "site past the table does not resolve");
        CHECK(!cint_site_resolve(&t_program, (cint_site){1u, 1u}, &pos), "unknown module does not resolve");
    }
    cint_ctx_destroy(ctx);
}

/* ------------------------------------------------------------------------- */
/* cint-rt-2: contexts (D-16, D-2), buffers and views (D-3), entry faults,    */
/* print output (LS-193 to LS-197, IM-90), and program processes (D-19).      */

static int g_fail_alloc;  /* when nonzero, test_alloc fails */

/* Counts live allocations in *(int *)user. */
static void *test_alloc(void *user, size_t bytes, size_t align)
{
    void *p;
    (void)align;
    if (g_fail_alloc != 0) {
        return NULL;
    }
    p = malloc(bytes);
    if (p != NULL) {
        (*(int *)user)++;
    }
    return p;
}

static void test_release(void *user, void *ptr, size_t bytes, size_t align)
{
    (void)bytes;
    (void)align;
    (*(int *)user)--;
    free(ptr);
}

static uint32_t refusal_of(cint_ctx *ctx)
{
    uint32_t reason = 99u;
    CHECK(cint_ctx_refusal(ctx, &reason) == CINT_OK, "cint_ctx_refusal outside an entry");
    return reason;
}

static int64_t fuel_of(cint_ctx *ctx)
{
    int64_t used = -1;
    CHECK(cint_fuel_consumed(ctx, &used) == CINT_OK, "cint_fuel_consumed outside an entry");
    return used;
}

static void test_context(void)
{
    cint_ctx_config cfg;
    cint_ctx *sentinel = (cint_ctx *)(void *)&cfg;  /* stays in *out unless CINT_OK */
    cint_ctx *ctx = sentinel;
    int live = 0, i;
    int64_t r = 0;
    char buf[64];
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = &t_program;
    CHECK(strcmp(CINT_RT_CONTRACT, "cint-rt-3") == 0 && cint_abi_version() == 0x00030000u, "contract and ABI 3.0");
    CHECK(cint_ctx_create(NULL, &ctx) == CINT_REFUSED && ctx == sentinel, "NULL config refused, out unwritten");
    CHECK(cint_ctx_create(&cfg, NULL) == CINT_REFUSED, "NULL out refused");
    cfg.size = 40u;
    CHECK(cint_ctx_create(&cfg, &ctx) == CINT_REFUSED && ctx == sentinel, "the cint-rt-1 config size is refused");
    cfg.size = 72u;
    CHECK(cint_ctx_create(&cfg, &ctx) == CINT_REFUSED && ctx == sentinel, "the cint-rt-2 config size is refused");
    cfg.size = (uint32_t)sizeof cfg;
    cfg.module = &t_module_old;
    CHECK(cint_ctx_create(&cfg, &ctx) == CINT_REFUSED && ctx == sentinel, "a module of ABI 2 is refused");
    cfg.module = &t_module_other;
    CHECK(cint_ctx_create(&cfg, &ctx) == CINT_REFUSED && ctx == sentinel, "a module of another program is refused");
    cfg.module = NULL;
    cfg.program = NULL;
    CHECK(cint_ctx_create(&cfg, &ctx) == CINT_REFUSED && ctx == sentinel, "no program refused");
    cfg.program = &t_program;
    cfg.depth_limit = -1;
    CHECK(cint_ctx_create(&cfg, &ctx) == CINT_REFUSED && ctx == sentinel, "negative depth limit refused");
    cfg.depth_limit = 0;
    cfg.frame_arena_bytes = -1;
    CHECK(cint_ctx_create(&cfg, &ctx) == CINT_REFUSED && ctx == sentinel, "negative frame arena refused");
    cfg.frame_arena_bytes = 0;
    cfg.allocator.alloc = test_alloc;
    CHECK(cint_ctx_create(&cfg, &ctx) == CINT_REFUSED && ctx == sentinel, "an allocator without release refused");
    cfg.allocator.release = test_release;
    cfg.allocator.user = &live;
    g_fail_alloc = 1;
    CHECK(cint_ctx_create(&cfg, &ctx) == CINT_RESOURCE && ctx == sentinel, "allocation failure is CINT_RESOURCE");
    g_fail_alloc = 0;

    /* A configured depth limit applies to entries that take it from the context. */
    cfg.depth_limit = 3;
    CHECK(cint_ctx_create(&cfg, &ctx) == CINT_OK && ctx != sentinel && live == 3,
          "create with depth 3: the context, its registry and its lease table");
    if (ctx == sentinel) {
        return;
    }
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, CINT_DEPTH_FROM_CONFIG) == CINT_OK, "entry with the configured D");
    CHECK(cint_rt_call_enter(ctx, S_CALL) && cint_rt_call_enter(ctx, S_CALL2), "two nested calls under D = 3");
    CHECK(!cint_rt_call_enter(ctx, S_CALL), "a third nested call faults E_DEPTH");
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT, "entry ends faulted");
    get_fault(ctx, &g_rec);
    CHECK(g_rec.code == CINT_E_DEPTH && strcmp(render(&g_rec.limit, buf, sizeof buf), "I64 3") == 0,
          "E_DEPTH limit is the configured D");
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_FAULTED, "a faulted context runs nothing (A-7)");
    CHECK(cint_ctx_clear_fault(ctx) == CINT_OK, "cint_ctx_clear_fault returns CINT_OK");
    get_fault(ctx, &g_rec);
    CHECK(g_rec.code == 0u, "the record is cleared");

    /* cint_ctx_clear_fault during an entry is CINT_BUSY and changes nothing (A-11). */
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK, "entry");
    CHECK(!cint_add_i64(ctx, S_OP, INT64_MAX, 1, &r), "a fault inside the entry");
    CHECK(cint_ctx_clear_fault(ctx) == CINT_BUSY, "clear_fault during an entry is CINT_BUSY");
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT, "the entry still ends faulted");
    get_fault(ctx, &g_rec);
    CHECK(g_rec.code == CINT_E_OVERFLOW, "the busy clear left the record");
    CHECK(cint_ctx_clear_fault(ctx) == CINT_OK && cint_ctx_clear_fault(NULL) == CINT_REFUSED, "clear statuses");
    CHECK(refusal_of(ctx) == CINT_REFUSAL_NONE, "no refusal recorded");
    cint_ctx_destroy(ctx);
    CHECK(live == 0, "destroy releases what the context allocated");

    /* With depth_limit 0 the SPEC-06 1.4 default, 256, applies. */
    cfg.depth_limit = 0;
    ctx = NULL;
    CHECK(cint_ctx_create(&cfg, &ctx) == CINT_OK, "create with the default depth");
    if (ctx == NULL) {
        return;
    }
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, CINT_FUEL_UNBOUNDED, CINT_DEPTH_FROM_CONFIG) == CINT_OK, "entry");
    for (i = 1; i < 256; i++) {
        if (!cint_rt_call_enter(ctx, S_CALL)) {
            break;
        }
    }
    CHECK(i == 256 && !cint_rt_call_enter(ctx, S_CALL2), "255 nested calls fit D = 256, the 256th faults");
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT, "faulted");
    get_fault(ctx, &g_rec);
    CHECK(strcmp(render(&g_rec.limit, buf, sizeof buf), "I64 256") == 0, "default D is 256 (SPEC-06 1.4)");
    cint_ctx_destroy(ctx);
    CHECK(live == 0, "nothing leaked");
}

static const cint_type T_I64 = {CINT_TAG_I64, 0u, 0u, 0u, 0u, 0u, 0u};
static int64_t g_words[8];

static cint_view view_of(cint_buffer_id id, int64_t origin, int64_t n, uint8_t perm)
{
    cint_view v;
    memset(&v, 0, sizeof v);
    v.buffer = id;
    v.generation = CINT_BUFFER_GENERATION_FIRST;
    v.type = T_I64;
    v.rank = 1u;
    v.perm = perm;
    v.origin = origin;
    v.shape[0] = n;
    v.stride[0] = 1;
    return v;
}

/* One wrapper-shaped call: entry, bind `v` for `mode` as I64, end. */
static cint_status bind_call(cint_ctx *ctx, const cint_view *v, uint8_t mode, void **p, int64_t *n)
{
    cint_status st = cint_rt_entry_begin(ctx, S_FUEL, 100, 8);
    if (st != CINT_OK) {
        return st;
    }
    (void)cint_view_bind(ctx, v, &T_I64, mode, 8, p, n);
    return cint_rt_entry_end(ctx);
}

static void test_buffers(void)
{
    cint_ctx *ctx = new_ctx();
    cint_buffer_id a = 0u, ro = 0u, empty = 0u, odd = 0u, extra = 0u, id = 0u;
    cint_view v;
    void *p = NULL;
    int64_t n = -1, before;
    uint32_t i, count = 0u;
    int k;
    struct refusal_case {
        const char *what;
        int field;
        int64_t value;
        uint8_t mode;
        uint32_t reason;
    };
    /* field: 0 buffer id, 1 generation, 2 type code, 3 storage, 4 reserved
     * byte, 5 rank, 6 stride, 7 origin, 8 extent, 9 perm. */
    static const struct refusal_case cases[] = {
        {"unknown id", 0, 99, CINT_MODE_IN, CINT_REFUSAL_BUFFER},
        {"id 0", 0, 0, CINT_MODE_IN, CINT_REFUSAL_BUFFER},
        {"stale generation", 1, 2, CINT_MODE_IN, CINT_REFUSAL_GENERATION},
        {"wrong type tag", 2, CINT_TAG_U64, CINT_MODE_IN, CINT_REFUSAL_TYPE},
        {"fixed-point storage on an integer type", 3, CINT_TAG_I64, CINT_MODE_IN, CINT_REFUSAL_TYPE},
        {"nonzero reserved byte", 4, 1, CINT_MODE_IN, CINT_REFUSAL_TYPE},
        {"rank 2", 5, 2, CINT_MODE_IN, CINT_REFUSAL_RANK},
        {"rank 0", 5, 0, CINT_MODE_IN, CINT_REFUSAL_RANK},
        {"stride 2", 6, 2, CINT_MODE_IN, CINT_REFUSAL_STRIDE},
        {"stride 0", 6, 0, CINT_MODE_IN, CINT_REFUSAL_STRIDE},
        {"negative origin", 7, -1, CINT_MODE_IN, CINT_REFUSAL_EXTENT},
        {"negative extent", 8, -1, CINT_MODE_IN, CINT_REFUSAL_EXTENT},
        {"extent past the buffer", 8, 9, CINT_MODE_IN, CINT_REFUSAL_EXTENT},
        {"origin past the buffer", 7, 6, CINT_MODE_IN, CINT_REFUSAL_EXTENT},
        {"byte size overflow", 8, INT64_MAX / 4, CINT_MODE_IN, CINT_REFUSAL_SIZE},
        {"origin plus extent overflow", 7, INT64_MAX, CINT_MODE_IN, CINT_REFUSAL_SIZE},
        {"a read view for an inout parameter", 9, CINT_VIEW_READ, CINT_MODE_INOUT, CINT_REFUSAL_PERMISSION},
        {"a read view for an out parameter", 9, CINT_VIEW_READ, CINT_MODE_OUT, CINT_REFUSAL_PERMISSION},
        {"permission 2", 9, 2, CINT_MODE_IN, CINT_REFUSAL_PERMISSION},
    };
    if (ctx == NULL) {
        CHECK(0, "cint_ctx_create");
        return;
    }
    CHECK(cint_buffer_register_bytes(ctx, g_words, 64, CINT_VIEW_WRITE, NULL) == CINT_REFUSED, "NULL out refused");
    CHECK(cint_buffer_register_bytes(ctx, g_words, -1, CINT_VIEW_WRITE, &id) == CINT_REFUSED && id == 0u,
          "negative size refused");
    CHECK(cint_buffer_register_bytes(ctx, NULL, 8, CINT_VIEW_WRITE, &id) == CINT_REFUSED, "NULL base with bytes refused");
    CHECK(cint_buffer_register_bytes(ctx, g_words, 64, 2u, &id) == CINT_REFUSED, "permission 2 refused");
    CHECK(cint_buffer_register_bytes(ctx, g_words, 64, CINT_VIEW_WRITE, &a) == CINT_OK && a == 1u, "first id is 1");
    CHECK(cint_buffer_register_bytes(ctx, g_words, 64, CINT_VIEW_READ, &ro) == CINT_OK && ro == 2u,
          "overlapping registrations are accepted (H-13 is decided at binding)");
    CHECK(cint_buffer_register_bytes(ctx, NULL, 0, CINT_VIEW_READ, &empty) == CINT_OK && empty == 3u, "empty registration");
    CHECK(cint_buffer_register_bytes(ctx, (unsigned char *)(void *)g_words + 1, 16, CINT_VIEW_READ, &odd) == CINT_OK,
          "a registration at an odd address");

    /* Successful binds write the element pointer and the extent. */
    v = view_of(a, 2, 3, CINT_VIEW_READ);
    CHECK(bind_call(ctx, &v, CINT_MODE_IN, &p, &n) == CINT_OK && p == (void *)&g_words[2] && n == 3,
          "a read view binds for in");
    CHECK(fuel_of(ctx) == 1 && refusal_of(ctx) == CINT_REFUSAL_NONE, "a bound entry charges its unit");
    v.perm = CINT_VIEW_WRITE;
    CHECK(bind_call(ctx, &v, CINT_MODE_INOUT, &p, &n) == CINT_OK, "a write view binds for inout");
    CHECK(bind_call(ctx, &v, CINT_MODE_IN, &p, &n) == CINT_OK, "a write view also serves in");
    v = view_of(a, 0, 8, CINT_VIEW_WRITE);
    CHECK(bind_call(ctx, &v, CINT_MODE_OUT, &p, &n) == CINT_OK && p == (void *)g_words && n == 8,
          "a view of the whole buffer");
    v = view_of(a, 8, 0, CINT_VIEW_READ);
    CHECK(bind_call(ctx, &v, CINT_MODE_IN, &p, &n) == CINT_OK && n == 0, "an empty view at the end");
    v = view_of(empty, 0, 0, CINT_VIEW_READ);
    CHECK(bind_call(ctx, &v, CINT_MODE_IN, &p, &n) == CINT_OK && p == NULL && n == 0, "a view of an empty buffer");

    /* Each refusal: CINT_REFUSED, its reason, the context unfaulted, the last
     * entry's fuel as it was, and nothing written (H-12). */
    for (k = 0; k < (int)(sizeof cases / sizeof cases[0]); k++) {
        const struct refusal_case *c = &cases[k];
        cint_status st;
        v = view_of(a, 0, 4, (uint8_t)(c->mode == CINT_MODE_IN ? CINT_VIEW_READ : CINT_VIEW_WRITE));
        switch (c->field) {
        case 0: v.buffer = (cint_buffer_id)c->value; break;
        case 1: v.generation = (uint64_t)c->value; break;
        case 2: v.type.code = (uint16_t)c->value; break;
        case 3: v.type.storage = (uint8_t)c->value; break;
        case 4: v.reserved[5] = (uint8_t)c->value; break;
        case 5: v.rank = (uint8_t)c->value; break;
        case 6: v.stride[0] = c->value; break;
        case 7: v.origin = c->value; break;
        case 8: v.shape[0] = c->value; break;
        default: v.perm = (uint8_t)c->value; break;
        }
        before = fuel_of(ctx);
        p = (void *)&v;
        n = -7;
        st = bind_call(ctx, &v, c->mode, &p, &n);
        CHECK(st == CINT_REFUSED, c->what);
        CHECK(refusal_of(ctx) == c->reason, c->what);
        CHECK(p == (void *)&v && n == -7 && fuel_of(ctx) == before, c->what);
        get_fault(ctx, &g_rec);
        CHECK(g_rec.code == 0u, c->what);
    }
    v = view_of(ro, 0, 1, CINT_VIEW_WRITE);
    CHECK(bind_call(ctx, &v, CINT_MODE_IN, &p, &n) == CINT_REFUSED && refusal_of(ctx) == CINT_REFUSAL_PERMISSION,
          "a write view of a read-only registration is refused (M-27)");
    v = view_of(odd, 0, 1, CINT_VIEW_READ);
    CHECK(bind_call(ctx, &v, CINT_MODE_IN, &p, &n) == CINT_REFUSED && refusal_of(ctx) == CINT_REFUSAL_ALIGN,
          "a misaligned I64 view is refused (A-13a)");
    {
        static const cint_type u8 = {CINT_TAG_U8, 0u, 0u, 0u, 0u, 0u, 0u};
        v = view_of(odd, 3, 13, CINT_VIEW_READ);
        v.type = u8;
        CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK, "entry");
        CHECK(cint_view_bind(ctx, &v, &u8, CINT_MODE_IN, 1, &p, &n) &&
                  p == (void *)((unsigned char *)(void *)g_words + 4) && n == 13,
              "a U8 view needs no alignment");
        CHECK(!cint_view_bind(ctx, &v, &T_I64, CINT_MODE_IN, 8, &p, &n), "the same view as I64 is refused");
        CHECK(!cint_view_bind(ctx, &v, &u8, CINT_MODE_IN, 1, &p, &n), "after a refusal every bind fails");
        CHECK(cint_rt_entry_end(ctx) == CINT_REFUSED && refusal_of(ctx) == CINT_REFUSAL_TYPE,
              "the first reason is kept");
        CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK, "entry");
        CHECK(!cint_view_bind(ctx, &v, &u8, 3u, 1, &p, &n), "mode 3 is refused");
        CHECK(cint_rt_entry_end(ctx) == CINT_REFUSED && refusal_of(ctx) == CINT_REFUSAL_CALL, "a bad call");
        CHECK(!cint_view_bind(ctx, &v, &u8, CINT_MODE_IN, 1, &p, &n) && refusal_of(ctx) == CINT_REFUSAL_CALL,
              "outside an entry a bind fails and changes nothing");
    }

    /* Release advances the generation; ids are not reused. */
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK, "entry");
    CHECK(cint_buffer_register_bytes(ctx, g_words, 8, CINT_VIEW_READ, &id) == CINT_BUSY, "register during an entry");
    CHECK(cint_buffer_release(ctx, a) == CINT_BUSY, "release during an entry");
    CHECK(cint_rt_entry_end(ctx) == CINT_OK, "end");
    CHECK(cint_buffer_release(ctx, a) == CINT_OK, "release");
    CHECK(cint_buffer_release(ctx, a) == CINT_REFUSED, "a second release is refused");
    CHECK(cint_buffer_release(ctx, 99u) == CINT_REFUSED && cint_buffer_release(ctx, 0u) == CINT_REFUSED,
          "releasing an unknown id is refused");
    CHECK(cint_buffer_release(NULL, ro) == CINT_REFUSED, "NULL context");
    v = view_of(a, 0, 1, CINT_VIEW_READ);
    CHECK(bind_call(ctx, &v, CINT_MODE_IN, &p, &n) == CINT_REFUSED && refusal_of(ctx) == CINT_REFUSAL_GENERATION,
          "a view of a released registration is refused");
    v.generation = CINT_BUFFER_GENERATION_FIRST + 1u;
    CHECK(bind_call(ctx, &v, CINT_MODE_IN, &p, &n) == CINT_REFUSED && refusal_of(ctx) == CINT_REFUSAL_GENERATION,
          "so is one at the advanced generation");

    /* A fault and its clearing leave registrations alone (A-7a). */
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK, "entry");
    CHECK(!cint_add_i64(ctx, S_OP, INT64_MAX, 1, &n), "fault");
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT, "faulted");
    v = view_of(ro, 0, 8, CINT_VIEW_READ);
    CHECK(bind_call(ctx, &v, CINT_MODE_IN, &p, &n) == CINT_FAULTED, "a faulted context binds nothing");
    CHECK(cint_ctx_clear_fault(ctx) == CINT_OK, "clear");
    CHECK(bind_call(ctx, &v, CINT_MODE_IN, &p, &n) == CINT_OK && n == 8, "the registration survived the clear");

    /* Capacity: CINT_RT_MAX_BUFFERS live registrations, then CINT_RESOURCE. */
    for (i = 0u; i < CINT_RT_MAX_BUFFERS; i++) {
        if (cint_buffer_register_bytes(ctx, g_words, 8, CINT_VIEW_READ, &extra) != CINT_OK) {
            break;
        }
        count++;
    }
    CHECK(count == CINT_RT_MAX_BUFFERS - 3u, "253 more registrations fit beside 3 live ones");
    CHECK(cint_buffer_register_bytes(ctx, g_words, 8, CINT_VIEW_READ, &id) == CINT_RESOURCE, "a full registry");
    CHECK(cint_buffer_release(ctx, extra) == CINT_OK, "release one");
    CHECK(cint_buffer_register_bytes(ctx, g_words, 8, CINT_VIEW_READ, &id) == CINT_OK && id == extra + 1u,
          "its slot is reused under a new id");
    v = view_of(extra, 0, 1, CINT_VIEW_READ);
    CHECK(bind_call(ctx, &v, CINT_MODE_IN, &p, &n) == CINT_REFUSED && refusal_of(ctx) == CINT_REFUSAL_BUFFER,
          "an id whose slot was reused is unknown");
    cint_ctx_destroy(ctx);
}

static const cint_type T_ZERO_RECORD = {CINT_TAG_RECORD, 0u, 0u, 0u, 0u, 7u, 0u};

static cint_view zero_view(cint_buffer_id id, int64_t origin, int64_t n, uint8_t perm)
{
    cint_view v = view_of(id, origin, n, perm);
    v.type = T_ZERO_RECORD;
    return v;
}

static cint_status bind_elements_call(cint_ctx *ctx, const cint_view *v, const cint_type *want,
                                      uint8_t mode, int64_t bytes, void **p, int64_t *n)
{
    cint_status st = cint_rt_entry_begin(ctx, S_FUEL, 100, 8);
    if (st != CINT_OK) {
        return st;
    }
    (void)cint_view_bind(ctx, v, want, mode, bytes, p, n);
    return cint_rt_entry_end(ctx);
}

static void logical_refusal(cint_ctx *ctx, const cint_view *v, const cint_type *want,
                             uint8_t mode, int64_t bytes, uint32_t reason)
{
    void *p = (void *)g_words;
    int64_t n = -7, before = fuel_of(ctx);
    CHECK(bind_elements_call(ctx, v, want, mode, bytes, &p, &n) == CINT_REFUSED, "logical bind refuses");
    CHECK(refusal_of(ctx) == reason, "logical bind refusal reason");
    CHECK(p == (void *)g_words && n == -7 && fuel_of(ctx) == before, "refusal preserves outputs and fuel");
    get_fault(ctx, &g_rec);
    CHECK(g_rec.code == 0u, "logical refusal leaves context unfaulted");
}

static bool logical_overlap(cint_ctx *ctx, const cint_view *a, const cint_view *b,
                            int64_t a_bytes, int64_t b_bytes)
{
    void *ap = NULL, *bp = NULL;
    int64_t an = -1, bn = -1;
    CHECK(bind_elements_call(ctx, a, &a->type, CINT_MODE_IN, a_bytes, &ap, &an) == CINT_OK,
          "first overlap view binds");
    CHECK(bind_elements_call(ctx, b, &b->type, CINT_MODE_IN, b_bytes, &bp, &bn) == CINT_OK,
          "second overlap view binds");
    CHECK(an == a->shape[0] && bn == b->shape[0], "overlap uses checked extents");
    return cint_rt_view_overlap(a, ap, a_bytes, b, bp, b_bytes);
}

static void test_logical_buffers(void)
{
    cint_ctx *ctx = new_ctx();
    cint_buffer_id a = 0u, same_base = 0u, other_base = 0u, ro = 0u, empty = 0u, huge = 0u;
    cint_buffer_id positive = 0u, legacy = 0u, id = 99u, last = 0u;
    cint_view v, b;
    void *p = NULL;
    int64_t n = -1;
    uint32_t registrations = 0u;
    if (ctx == NULL) {
        CHECK(0, "logical context create");
        return;
    }
    /* Refuses malformed registrations before assigning an id or a slot. */
    for (int k = 0; k < 19; k++) {
        cint_type type = T_ZERO_RECORD;
        cint_ctx *arg_ctx = ctx;
        const cint_type *arg_type = &type;
        cint_buffer_id *out = &id;
        void *base = NULL;
        int64_t bytes = 0, extent = 4;
        uint8_t perm = CINT_VIEW_WRITE;
        switch (k) {
        case 0: arg_ctx = NULL; break;
        case 1: arg_type = NULL; break;
        case 2: out = NULL; break;
        case 3: bytes = -1; break;
        case 4: extent = -1; break;
        case 5: perm = 2u; break;
        case 6: bytes = 1; break;
        case 7: bytes = 8; extent = INT64_MAX; base = g_words; break;
        case 8: type.reserved0 = 1u; break;
        case 9: type.reserved1 = 1u; break;
        case 10: type.reserved2 = 1u; break;
        case 11: type.storage = CINT_TAG_I64; break;
        case 12: type.frac_bits = 1u; break;
        case 13: type.code = 0xffffu; break;
        case 14: type = T_I64; break;
        case 15: type = T_I64; bytes = 4; base = g_words; break;
        case 16: type = T_I64; type.record_id = 1u; bytes = 8; base = g_words; break;
        case 17: type = T_I64; bytes = 8; base = (void *)(UINTPTR_MAX - 3u); break;
        default: type.code = CINT_TAG_BOOL; type.record_id = 0u; break;
        }
        CHECK(cint_buffer_register_elements(arg_ctx, base, arg_type, bytes, extent, perm, out) == CINT_REFUSED,
              "malformed typed registration refused");
        CHECK(id == 99u, "failed registration leaves id untouched");
    }
    CHECK(cint_buffer_register_elements(ctx, g_words, &T_ZERO_RECORD, 0, 4, CINT_VIEW_WRITE, &a) == CINT_OK && a == 1u,
          "first typed zero registration has finite capacity and first id");
    CHECK(cint_buffer_register_elements(ctx, g_words, &T_ZERO_RECORD, 0, 4, CINT_VIEW_WRITE, &same_base) == CINT_OK &&
              same_base == a + 1u, "same address creates a distinct logical registration");
    CHECK(cint_buffer_register_elements(ctx, &n, &T_ZERO_RECORD, 0, 4, CINT_VIEW_WRITE, &other_base) == CINT_OK,
          "another address also creates a distinct logical registration");
    CHECK(cint_buffer_register_elements(ctx, NULL, &T_ZERO_RECORD, 0, 4, CINT_VIEW_READ, &ro) == CINT_OK,
          "readonly zero registration");
    CHECK(cint_buffer_register_elements(ctx, NULL, &T_ZERO_RECORD, 0, 0, CINT_VIEW_READ, &empty) == CINT_OK,
          "zero logical capacity");
    CHECK(cint_buffer_register_elements(ctx, NULL, &T_ZERO_RECORD, 0, INT64_MAX, CINT_VIEW_WRITE, &huge) == CINT_OK,
          "finite maximum logical capacity without payload allocation");
    CHECK(cint_buffer_register_elements(ctx, g_words, &T_I64, 8, 8, CINT_VIEW_WRITE, &positive) == CINT_OK,
          "positive typed registration");
    CHECK(cint_buffer_register_bytes(ctx, NULL, 0, CINT_VIEW_WRITE, &legacy) == CINT_OK, "legacy zero byte registration");
    registrations = 8u;
    v = zero_view(a, 1, 3, CINT_VIEW_WRITE);
    CHECK(bind_elements_call(ctx, &v, &T_ZERO_RECORD, CINT_MODE_INOUT, 0, &p, &n) == CINT_OK && p == NULL && n == 3,
          "zero binding returns null and the logical extent");
    CHECK(fuel_of(ctx) == 1 && refusal_of(ctx) == CINT_REFUSAL_NONE, "zero binding keeps ordinary entry fuel");
    v = zero_view(a, 4, 0, CINT_VIEW_READ);
    CHECK(bind_elements_call(ctx, &v, &T_ZERO_RECORD, CINT_MODE_IN, 0, &p, &n) == CINT_OK && p == NULL && n == 0,
          "empty zero view at capacity");
    v = zero_view(empty, 0, 0, CINT_VIEW_READ);
    CHECK(bind_elements_call(ctx, &v, &T_ZERO_RECORD, CINT_MODE_IN, 0, &p, &n) == CINT_OK && p == NULL && n == 0,
          "empty logical allocation binds an empty view");
    v = zero_view(huge, INT64_MAX - 1, 1, CINT_VIEW_WRITE);
    CHECK(bind_elements_call(ctx, &v, &T_ZERO_RECORD, CINT_MODE_OUT, 0, &p, &n) == CINT_OK && p == NULL && n == 1,
          "last logical element at maximum capacity");
    v.origin = INT64_MAX;
    v.shape[0] = 0;
    CHECK(bind_elements_call(ctx, &v, &T_ZERO_RECORD, CINT_MODE_IN, 0, &p, &n) == CINT_OK && n == 0,
          "empty logical view at maximum origin");
    for (int k = 0; k < 18; k++) {
        uint32_t reason = CINT_REFUSAL_TYPE;
        cint_type want = T_ZERO_RECORD;
        uint8_t mode = CINT_MODE_IN;
        int64_t bytes = 0;
        v = zero_view(a, 0, 2, CINT_VIEW_READ);
        switch (k) {
        case 0: v.buffer = 0u; reason = CINT_REFUSAL_BUFFER; break;
        case 1: v.generation++; reason = CINT_REFUSAL_GENERATION; break;
        case 2: v.type.record_id++; break;
        case 3: v.type.record_id++; want = v.type; break;
        case 4: v.type.reserved2 = 1u; break;
        case 5: v.reserved[5] = 1u; break;
        case 6: v.rank = 2u; reason = CINT_REFUSAL_RANK; break;
        case 7: v.stride[0] = 0; reason = CINT_REFUSAL_STRIDE; break;
        case 8: v.origin = -1; reason = CINT_REFUSAL_EXTENT; break;
        case 9: v.shape[0] = -1; reason = CINT_REFUSAL_EXTENT; break;
        case 10: v.origin = 4; reason = CINT_REFUSAL_EXTENT; break;
        case 11: v.origin = 5; v.shape[0] = 0; reason = CINT_REFUSAL_EXTENT; break;
        case 12: v.origin = INT64_MAX; v.shape[0] = INT64_MAX; reason = CINT_REFUSAL_EXTENT; break;
        case 13: v.perm = 2u; reason = CINT_REFUSAL_PERMISSION; break;
        case 14: mode = CINT_MODE_INOUT; reason = CINT_REFUSAL_PERMISSION; break;
        case 15: v.buffer = ro; v.perm = CINT_VIEW_WRITE; reason = CINT_REFUSAL_PERMISSION; break;
        case 16: v.buffer = legacy; break;
        default: bytes = 1; break;
        }
        logical_refusal(ctx, &v, &want, mode, bytes, reason);
    }
    v = zero_view(huge, INT64_MAX, 1, CINT_VIEW_READ);
    logical_refusal(ctx, &v, &T_ZERO_RECORD, CINT_MODE_IN, 0, CINT_REFUSAL_EXTENT);
    v = zero_view(empty, 0, 1, CINT_VIEW_READ);
    logical_refusal(ctx, &v, &T_ZERO_RECORD, CINT_MODE_IN, 0, CINT_REFUSAL_EXTENT);
    v = zero_view(huge, 1, INT64_MAX, CINT_VIEW_READ);
    logical_refusal(ctx, &v, &T_ZERO_RECORD, CINT_MODE_IN, 0, CINT_REFUSAL_EXTENT);
    v = zero_view(a, 0, 1, CINT_VIEW_WRITE);
    logical_refusal(ctx, &v, &T_I64, CINT_MODE_IN, 0, CINT_REFUSAL_CALL);
    p = g_words;
    n = -7;
    CHECK(!cint_view_bind(ctx, &v, &T_ZERO_RECORD, CINT_MODE_IN, 0, &p, &n) && p == g_words && n == -7 &&
              refusal_of(ctx) == CINT_REFUSAL_CALL, "outside-entry zero bind changes nothing");
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK, "entry for sticky refusal");
    b = v;
    b.type.record_id++;
    CHECK(!cint_view_bind(ctx, &b, &T_ZERO_RECORD, CINT_MODE_IN, 0, &p, &n), "bad zero nominal type refuses");
    CHECK(!cint_view_bind(ctx, &v, &T_ZERO_RECORD, CINT_MODE_IN, 0, &p, &n) && p == g_words && n == -7,
          "later valid zero binding cannot overwrite a refusal");
    CHECK(cint_rt_entry_end(ctx) == CINT_REFUSED && refusal_of(ctx) == CINT_REFUSAL_TYPE, "first zero refusal stays");
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK, "entry for zero-registration lifetime");
    CHECK(!cint_fault_shape(ctx, S_FUEL, 1u, 0u, 1, 2), "shape fault with live logical registration");
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT, "fault retained");
    CHECK(bind_elements_call(ctx, &v, &T_ZERO_RECORD, CINT_MODE_IN, 0, &p, &n) == CINT_FAULTED && p == g_words && n == -7,
          "faulted context binds nothing");
    CHECK(cint_ctx_clear_fault(ctx) == CINT_OK, "clear fault");
    CHECK(bind_elements_call(ctx, &v, &T_ZERO_RECORD, CINT_MODE_IN, 0, &p, &n) == CINT_OK && p == NULL && n == 1,
          "clear fault preserves logical registration and generation");
    v = view_of(positive, 2, 3, CINT_VIEW_READ);
    CHECK(bind_elements_call(ctx, &v, &T_I64, CINT_MODE_IN, 8, &p, &n) == CINT_OK && p == &g_words[2] && n == 3,
          "positive typed binding retains byte address");
    v.type.code = CINT_TAG_U64;
    logical_refusal(ctx, &v, &v.type, CINT_MODE_IN, 8, CINT_REFUSAL_TYPE);
    v = zero_view(a, 1, 2, CINT_VIEW_READ);
    b = v;
    CHECK(logical_overlap(ctx, &v, &b, 0, 0), "identical nonempty logical intervals overlap");
    b.origin = 2;
    CHECK(logical_overlap(ctx, &v, &b, 0, 0), "partial logical overlap");
    b.origin = 3;
    b.shape[0] = 1;
    CHECK(!logical_overlap(ctx, &v, &b, 0, 0), "adjacent logical intervals are disjoint");
    b.origin = 2;
    b.shape[0] = 0;
    CHECK(!logical_overlap(ctx, &v, &b, 0, 0), "empty logical interval overlaps nothing");
    b = v;
    b.buffer = same_base;
    CHECK(!logical_overlap(ctx, &v, &b, 0, 0), "same base with distinct ids is distinct logical storage");
    b.buffer = other_base;
    CHECK(!logical_overlap(ctx, &v, &b, 0, 0), "different base with distinct ids is distinct logical storage");
    v = zero_view(huge, INT64_MAX - 2, 2, CINT_VIEW_READ);
    b = zero_view(huge, INT64_MAX - 1, 1, CINT_VIEW_READ);
    CHECK(logical_overlap(ctx, &v, &b, 0, 0), "logical overlap near INT64_MAX has no sum overflow");
    b.origin = INT64_MAX;
    b.shape[0] = 0;
    CHECK(!logical_overlap(ctx, &v, &b, 0, 0), "empty maximum-origin interval");
    CHECK(cint_buffer_register_bytes(ctx, g_words, 64, CINT_VIEW_READ, &id) == CINT_OK, "positive overlapping byte registration");
    registrations++;
    v = view_of(positive, 1, 3, CINT_VIEW_READ);
    b = view_of(id, 2, 2, CINT_VIEW_READ);
    CHECK(logical_overlap(ctx, &v, &b, 8, 8), "positive typed and legacy registrations overlap by address");
    b = zero_view(a, 1, 2, CINT_VIEW_READ);
    CHECK(!logical_overlap(ctx, &v, &b, 8, 0), "zero storage has no byte overlap with positive storage");
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK, "entry for busy registration");
    id = 99u;
    CHECK(cint_buffer_register_elements(ctx, NULL, &T_ZERO_RECORD, 0, 1, CINT_VIEW_READ, &id) == CINT_BUSY && id == 99u,
          "typed registration is busy and publishes no id during entry");
    CHECK(cint_rt_entry_end(ctx) == CINT_OK, "busy request leaves active entry intact");
    CHECK(cint_buffer_release(ctx, a) == CINT_OK, "release zero storage");
    registrations--;
    v = zero_view(a, 0, 1, CINT_VIEW_READ);
    logical_refusal(ctx, &v, &T_ZERO_RECORD, CINT_MODE_IN, 0, CINT_REFUSAL_GENERATION);
    while (registrations < CINT_RT_MAX_BUFFERS) {
        CHECK(cint_buffer_register_elements(ctx, NULL, &T_ZERO_RECORD, 0, 1, CINT_VIEW_READ, &last) == CINT_OK,
              "logical registration occupies one finite registry slot");
        registrations++;
    }
    id = 99u;
    CHECK(cint_buffer_register_elements(ctx, NULL, &T_ZERO_RECORD, 0, 1, CINT_VIEW_READ, &id) == CINT_RESOURCE && id == 99u,
          "full logical registry preserves the output id");
    logical_refusal(ctx, &v, &T_ZERO_RECORD, CINT_MODE_IN, 0, CINT_REFUSAL_BUFFER);
    CHECK(cint_buffer_release(ctx, last) == CINT_OK, "release full-registry slot");
    CHECK(cint_buffer_register_elements(ctx, NULL, &T_ZERO_RECORD, 0, 1, CINT_VIEW_READ, &id) == CINT_OK && id == last + 1u,
          "released slot is reused with fresh monotonic id");
    cint_ctx_destroy(ctx);
}

static void test_copy_shape(void)
{
    static const int64_t cases[][3] = {
        {0, 2, 3}, {0, 0, 1}, {0, 2, 0}, {1, INT64_MAX, 0},
        {INT64_MAX, 0, INT64_MAX}, {0, 0, 0}, {0, 7, 7}, {0, INT64_MAX, INT64_MAX}
    };
    cint_ctx *ctx = new_ctx();
    size_t i;
    char actual[64], expected[64];
    uint8_t encoded[512];
    if (ctx == NULL) { CHECK(0, "copy shape context"); return; }
    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        bool equal = cases[i][1] == cases[i][2];
        CHECK(cint_ctx_clear_fault(ctx) == CINT_OK, "clear copy shape fault");
        CHECK(cint_rt_entry_begin(ctx, S_FUEL, 4, 8) == CINT_OK, "copy shape entry");
        CHECK(cint_rt_call_enter(ctx, S_CALL), "copy shape nested call");
        CHECK(cint_fuel_charge(ctx, S_FUEL, 2u), "copy shape prior effects consume fuel");
        CHECK(cint_copy_shape_check(ctx, S_OP, cases[i][0], cases[i][1], cases[i][2]) == equal,
              "copy shape compares logical extents");
        if (!equal) {
            CHECK(!cint_copy_shape_check(ctx, S_CALL2, 9, 6, 5), "later mismatch keeps the first fault");
            CHECK(!cint_copy_shape_check(ctx, S_CALL2, 9, 5, 5), "equal counts on a faulted context fail");
        }
        cint_rt_call_leave(ctx);
        CHECK(cint_rt_entry_end(ctx) == (equal ? CINT_OK : CINT_FAULT), "copy shape entry result");
        CHECK(fuel_of(ctx) == 4, "copy shape adds no fuel charge");
        get_fault(ctx, &g_rec);
        if (equal) {
            CHECK(g_rec.code == 0u, "equal copy shape has no fault");
            continue;
        }
        CHECK(g_rec.code == CINT_E_SHAPE && g_rec.operation_len == 10u &&
              memcmp(g_rec.operation, "copy.shape", 10u) == 0, "copy shape operation");
        CHECK(g_rec.operand_count == 2u && !g_rec.has_exact && g_rec.has_limit,
              "copy shape has two operands, a limit and no exact");
        snprintf(expected, sizeof expected, "I64 %" PRId64, cases[i][0]);
        CHECK(strcmp(render(&g_rec.operands[0], actual, sizeof actual), expected) == 0,
              "copy shape dimension is I64");
        snprintf(expected, sizeof expected, "I64 %" PRId64, cases[i][2]);
        CHECK(strcmp(render(&g_rec.operands[1], actual, sizeof actual), expected) == 0,
              "copy shape source extent is I64");
        snprintf(expected, sizeof expected, "I64 %" PRId64, cases[i][1]);
        CHECK(strcmp(render(&g_rec.limit, actual, sizeof actual), expected) == 0,
              "copy shape destination extent is the I64 limit");
        CHECK(g_rec.position.module == S_OP.module && g_rec.position.index == S_OP.index &&
              g_rec.stack_count == 1u && g_rec.stack[0].index == S_CALL.index,
              "copy shape keeps its site and call stack");
        CHECK(cint_fault_encode(&g_rec, encoded, sizeof encoded) > 0u, "copy shape record encodes");
    }
    cint_ctx_destroy(ctx);
}

static void test_entry_faults(void)
{
    cint_ctx *ctx = new_ctx();
    char buf[64];
    uint8_t bytes[512];
    if (ctx == NULL) {
        CHECK(0, "cint_ctx_create");
        return;
    }
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK, "entry");
    CHECK(!cint_fault_shape(ctx, S_FUEL, 3u, 1u, 8, 9), "cint_fault_shape returns false");
    CHECK(!cint_fault_alias(ctx, S_FUEL, 3u, 0u), "a second writer on a faulted context");
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT, "the entry ends faulted");
    CHECK(fuel_of(ctx) == 0, "an entry fault charges no fuel (H-12)");
    get_fault(ctx, &g_rec);
    CHECK(g_rec.code == CINT_E_SHAPE && g_rec.operation_len == 10u && memcmp(g_rec.operation, "bind.shape", 10u) == 0,
          "E_SHAPE bind.shape, and the first fault is kept");
    CHECK(g_rec.operand_count == 3u && strcmp(render(&g_rec.operands[0], buf, sizeof buf), "I64 3") == 0 &&
              strcmp(render(&g_rec.operands[1], buf, sizeof buf), "I64 1") == 0 &&
              strcmp(render(&g_rec.operands[2], buf, sizeof buf), "I64 9") == 0,
          "operands: parameter, dimension, actual extent");
    CHECK(!g_rec.has_exact && strcmp(render(&g_rec.limit, buf, sizeof buf), "I64 8") == 0, "limit: expected extent");
    CHECK(g_rec.position.index == S_FUEL.index && g_rec.stack_count == 0u, "at the entry, empty stack");
    CHECK(cint_fault_encode(&g_rec, bytes, sizeof bytes) > 0u, "the record encodes");
    {
        /* cint_fault_get: the same canonical bytes, queried, whole, or cut at cap. */
        uint8_t got[512], part[10];
        size_t want = cint_fault_encode(&g_rec, bytes, sizeof bytes), len = 0u;
        memset(got, 0xAA, sizeof got);
        memset(part, 0xAA, sizeof part);
        CHECK(cint_fault_get(ctx, NULL, 0u, &len) == CINT_OK && len == want, "the query gives the length");
        CHECK(cint_fault_get(ctx, got, sizeof got, &len) == CINT_OK && len == want &&
                  memcmp(got, bytes, want) == 0 && got[want] == 0xAA,
              "a whole copy is the record's encoding, nothing after it");
        CHECK(cint_fault_get(ctx, part, sizeof part, &len) == CINT_OK && len == want &&
                  memcmp(part, bytes, sizeof part) == 0,
              "a short buffer takes the first cap bytes and the full length");
        len = 7u;
        CHECK(cint_fault_get(NULL, got, sizeof got, &len) == CINT_REFUSED &&
                  cint_fault_get(ctx, got, sizeof got, NULL) == CINT_REFUSED &&
                  cint_fault_get(ctx, NULL, 1u, &len) == CINT_REFUSED && len == 7u,
              "NULL context, length, or buffer with cap above 0 refused, length unwritten");
    }
    CHECK(cint_ctx_clear_fault(ctx) == CINT_OK, "clear");
    {
        size_t len = 7u;
        uint8_t got[8];
        CHECK(cint_fault_get(ctx, got, sizeof got, &len) == CINT_OK && len == 0u, "no fault: length 0");
        CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK, "entry");
        len = 7u;
        CHECK(cint_fault_get(ctx, got, sizeof got, &len) == CINT_BUSY && len == 7u, "during an entry: CINT_BUSY");
        CHECK(cint_rt_entry_end(ctx) == CINT_OK, "the entry ends");
    }
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK, "entry");
    CHECK(!cint_fault_alias(ctx, S_FUEL, 3u, 0u), "cint_fault_alias returns false");
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT && fuel_of(ctx) == 0, "faulted, no fuel");
    get_fault(ctx, &g_rec);
    CHECK(g_rec.code == CINT_E_ALIAS && g_rec.operation_len == 10u && memcmp(g_rec.operation, "bind.alias", 10u) == 0,
          "E_ALIAS bind.alias");
    CHECK(g_rec.operand_count == 2u && strcmp(render(&g_rec.operands[0], buf, sizeof buf), "I64 3") == 0 &&
              strcmp(render(&g_rec.operands[1], buf, sizeof buf), "I64 0") == 0 && !g_rec.has_exact &&
              !g_rec.has_limit,
          "operands: the writable parameter and the one it overlaps");
    CHECK(cint_fault_encode(&g_rec, bytes, sizeof bytes) > 0u, "the record encodes");
    /* The internal call-site variant (task 2.14): no record when the extents agree; the
     * same record when they differ, at its site, with the fuel charged so far kept. */
    CHECK(cint_ctx_clear_fault(ctx) == CINT_OK, "clear");
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK, "entry");
    CHECK(cint_fuel_charge(ctx, S_FUEL, 2u), "two units");
    CHECK(cint_shape_check(ctx, S_FUEL, 1u, 4, 4), "equal extents pass");
    CHECK(!cint_shape_check(ctx, S_FUEL, 1u, 4, 3), "unequal extents fault");
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT && fuel_of(ctx) == 3, "faulted, fuel kept (entry and two units)");
    get_fault(ctx, &g_rec);
    CHECK(g_rec.code == CINT_E_SHAPE && g_rec.operand_count == 3u &&
              strcmp(render(&g_rec.operands[0], buf, sizeof buf), "I64 1") == 0 &&
              strcmp(render(&g_rec.operands[1], buf, sizeof buf), "I64 0") == 0 &&
              strcmp(render(&g_rec.operands[2], buf, sizeof buf), "I64 3") == 0 &&
              strcmp(render(&g_rec.limit, buf, sizeof buf), "I64 4") == 0 && g_rec.position.index == S_FUEL.index,
          "E_SHAPE at the call site: parameter, dimension 0, actual extent; limit the expected extent");
    cint_ctx_destroy(ctx);

    /* Byte-range overlap for the alias test (H-13 T1). */
    {
        const unsigned char *w = (const unsigned char *)(const void *)g_words;
        CHECK(cint_rt_bytes_overlap(w, 16, w + 8, 16) && cint_rt_bytes_overlap(w + 8, 16, w, 16), "overlap");
        CHECK(cint_rt_bytes_overlap(w, 8, w, 8) && cint_rt_bytes_overlap(w, 64, w + 63, 1), "contained");
        CHECK(!cint_rt_bytes_overlap(w, 8, w + 8, 8) && !cint_rt_bytes_overlap(w + 8, 8, w, 8), "adjacent");
        CHECK(!cint_rt_bytes_overlap(w, 0, w, 8) && !cint_rt_bytes_overlap(w, 8, w + 4, 0), "empty ranges");
    }
}

/* The T3 entry checks of a cint-abi-1 wrapper (cint_rt.h section 6a'). */
static const cint_type T_U8 = {CINT_TAG_U8, 0u, 0u, 0u, 0u, 0u, 0u};
static const cint_type T_BOOL = {CINT_TAG_BOOL, 0u, 0u, 0u, 0u, 0u, 0u};

/* The bind of an I64 view for parameter `param` of mode `mode`. */
static cint_bind bind_of(const cint_view *v, uint32_t param, uint32_t mode)
{
    cint_bind b;
    memset(&b, 0, sizeof b);
    b.view = v;
    b.want = &T_I64;
    b.elem_bytes = 8;
    b.param = param;
    b.mode = mode;
    b.rank = 1u;
    return b;
}

/* One wrapper-shaped call over n binds: open, views, finish, the entry's unit. */
static cint_status binds_call(cint_ctx *ctx, int64_t fuel, cint_bind *b, uint32_t n)
{
    cint_status st = cint_rt_entry_open(ctx, S_FUEL, fuel, 8);
    if (st != CINT_OK) {
        return st;
    }
    if (cint_rt_bind_views(ctx, S_CALL, b, n) && cint_rt_bind_finish(ctx, S_CALL, b, n)) {
        (void)cint_fuel_charge(ctx, S_FUEL, 1u);
    }
    return cint_rt_entry_end(ctx);
}

/* The faulted context's record: code, operation, rendered operands, limit. */
static int bind_record_is(cint_ctx *ctx, uint32_t code, const char *op, const char *operands, const char *limit)
{
    char buf[64], got[256] = "";
    size_t used = 0u;
    get_fault(ctx, &g_rec);
    for (uint32_t i = 0u; i < g_rec.operand_count; i++) {
        used += (size_t)snprintf(got + used, sizeof got - used, "%s%s", i > 0u ? ", " : "",
                                 render(&g_rec.operands[i], buf, sizeof buf));
    }
    if (g_rec.code != code || g_rec.operation_len != strlen(op) || memcmp(g_rec.operation, op, strlen(op)) != 0 ||
        strcmp(got, operands) != 0 || g_rec.has_exact || g_rec.position.index != S_CALL.index) {
        return 0;
    }
    if (limit == NULL ? g_rec.has_limit : strcmp(render(&g_rec.limit, buf, sizeof buf), limit) != 0) {
        return 0;
    }
    return cint_ctx_clear_fault(ctx) == CINT_OK;
}

static void test_bind(void)
{
    cint_ctx *ctx = new_ctx();
    static uint8_t flags[4] = {1u, 0u, 2u, 1u};
    static const uint32_t bool_at[1] = {0u};
    cint_buffer_id a = 0u, ro = 0u, gone = 0u, bytes = 0u, bools = 0u, rec = 0u;
    cint_view v, w;
    cint_bind b[3];
    static const cint_type T_REC = {CINT_TAG_RECORD, 0u, 0u, 0u, 0u, 3u, 0u};
    if (ctx == NULL) {
        CHECK(0, "cint_ctx_create");
        return;
    }
    CHECK(cint_buffer_register_bytes(ctx, g_words, 64, CINT_VIEW_WRITE, &a) == CINT_OK &&
              cint_buffer_register_bytes(ctx, g_words, 64, CINT_VIEW_READ, &ro) == CINT_OK &&
              cint_buffer_register_bytes(ctx, g_words, 8, CINT_VIEW_WRITE, &gone) == CINT_OK &&
              cint_buffer_release(ctx, gone) == CINT_OK &&
              cint_buffer_register_bytes(ctx, (unsigned char *)(void *)g_words + 1, 16, CINT_VIEW_READ, &bytes) == CINT_OK &&
              cint_buffer_register_bytes(ctx, flags, 4, CINT_VIEW_READ, &bools) == CINT_OK &&
              cint_buffer_register_elements(ctx, NULL, &T_REC, 0, 5, CINT_VIEW_WRITE, &rec) == CINT_OK,
          "registrations");

    /* A bound entry: pointers written, one unit charged after the checks. */
    v = view_of(a, 2, 3, CINT_VIEW_WRITE);
    w = view_of(a, 5, 3, CINT_VIEW_READ);
    b[0] = bind_of(&v, 0u, CINT_MODE_INOUT);
    b[1] = bind_of(&w, 2u, CINT_MODE_IN);
    CHECK(binds_call(ctx, 100, b, 2u) == CINT_OK && b[0].ptr == (void *)&g_words[2] &&
              b[1].ptr == (void *)&g_words[5] && fuel_of(ctx) == 1,
          "disjoint views bind; the entry's unit is charged");

    /* Refusals come first, whatever later checks would find (H-12). */
    {
        static const struct { const char *what; int field; int64_t value; uint32_t reason; } cases[] = {
            {"a type this runtime does not know", 0, 0x41, CINT_REFUSAL_TYPE},
            {"nonzero reserved bytes", 1, 1, CINT_REFUSAL_TYPE},
            {"rank 0", 2, 0, CINT_REFUSAL_RANK},
            {"rank 5", 2, 5, CINT_REFUSAL_RANK},
            {"an unknown id", 3, 99, CINT_REFUSAL_BUFFER},
            {"a released registration", 4, 0, CINT_REFUSAL_GENERATION},
            {"a write view of a read ceiling", 5, 0, CINT_REFUSAL_PERMISSION},
            {"a negative origin", 6, -1, CINT_REFUSAL_EXTENT},
            {"past the registration", 7, 9, CINT_REFUSAL_EXTENT},
            {"an element offset beyond I64", 8, INT64_MAX, CINT_REFUSAL_SIZE},
            {"a misaligned origin", 9, 0, CINT_REFUSAL_ALIGN},
            {"a lower bound whose last index passes I64", 10, INT64_MAX - 2, CINT_REFUSAL_SIZE},
        };
        for (size_t k = 0u; k < sizeof cases / sizeof cases[0]; k++) {
            int64_t before = fuel_of(ctx);
            v = view_of(a, 0, 4, CINT_VIEW_WRITE);
            w = view_of(a, 0, 4, CINT_VIEW_READ);
            w.generation = 9u;  /* a later check would fault check 1 */
            switch (cases[k].field) {
            case 0: v.type.code = (uint16_t)cases[k].value; break;
            case 1: v.reserved[2] = 1u; break;
            case 2: v.rank = (uint8_t)cases[k].value; break;
            case 3: v.buffer = (cint_buffer_id)cases[k].value; break;
            case 4: v.buffer = gone; v.generation = 2u; break;
            case 5: v.buffer = ro; break;
            case 6: v.origin = cases[k].value; break;
            case 7: v.shape[0] = cases[k].value; break;
            case 8: v.shape[0] = 3; v.stride[0] = cases[k].value; break;
            case 10: v.lower[0] = cases[k].value; break;
            default: v.buffer = bytes; v.perm = CINT_VIEW_READ; v.shape[0] = 1; break;
            }
            b[0] = bind_of(&w, 0u, CINT_MODE_IN);
            b[1] = bind_of(&v, 1u, CINT_MODE_IN);
            CHECK(binds_call(ctx, 0, b, 2u) == CINT_REFUSED && refusal_of(ctx) == cases[k].reason &&
                      fuel_of(ctx) == before,
                  cases[k].what);
        }
    }
    v = view_of(bools, 0, 4, CINT_VIEW_READ);
    v.type = T_BOOL;
    b[0] = bind_of(&v, 0u, CINT_MODE_IN);
    b[0].want = &T_BOOL;
    b[0].elem_bytes = 1;
    b[0].bools = bool_at;
    b[0].bool_count = 1u;
    CHECK(binds_call(ctx, 100, b, 1u) == CINT_REFUSED && refusal_of(ctx) == CINT_REFUSAL_BOOL,
          "a Bool byte of 2 is refused");
    v.shape[0] = 2;
    v.stride[0] = 3;  /* elements 0 and 3: no 2 among them */
    CHECK(binds_call(ctx, 100, b, 1u) == CINT_OK && b[0].ptr == (void *)flags,
          "Bools of a strided view are read where the view reaches, and a view of rank 1 takes stride 3");

    /* F-5 checks 1 to 3 run each over every parameter: the first check decides. */
    v = view_of(a, 0, 2, CINT_VIEW_WRITE);
    v.type = T_U8;
    w = view_of(a, 4, 2, CINT_VIEW_READ);
    w.generation = 7u;
    b[0] = bind_of(&v, 0u, CINT_MODE_INOUT);
    b[1] = bind_of(&w, 3u, CINT_MODE_INOUT);
    CHECK(binds_call(ctx, 100, b, 2u) == CINT_FAULT && fuel_of(ctx) == 0 &&
              bind_record_is(ctx, CINT_E_STALE_HANDLE, "bind.stale", "I64 3, U64 7", "U64 1"),
          "a stale generation (check 1) before a type mismatch (check 3) and a read view (check 2)");
    w.generation = 1u;
    CHECK(binds_call(ctx, 100, b, 2u) == CINT_FAULT &&
              bind_record_is(ctx, CINT_E_ALIAS, "bind.permission", "I64 3", NULL),
          "a read view for inout (check 2) before the type of an earlier parameter");
    w.perm = CINT_VIEW_WRITE;
    CHECK(binds_call(ctx, 100, b, 2u) == CINT_FAULT &&
              bind_record_is(ctx, CINT_E_UNSUPPORTED, "bind.type", "I64 0", NULL),
          "an element type other than the declared one");
    v.type = T_I64;
    w.rank = 2u;
    w.shape[1] = 1;
    w.stride[0] = 1;
    w.stride[1] = 1;
    CHECK(binds_call(ctx, 100, b, 2u) == CINT_FAULT &&
              bind_record_is(ctx, CINT_E_SHAPE, "bind.type", "I64 3, I64 2", "I64 1"),
          "a rank other than the declared one");

    /* Checks 6 to 8. */
    v = view_of(a, 0, 3, CINT_VIEW_WRITE);
    v.stride[0] = 0;
    b[0] = bind_of(&v, 0u, CINT_MODE_OUT);
    CHECK(binds_call(ctx, 100, b, 1u) == CINT_FAULT &&
              bind_record_is(ctx, CINT_E_ALIAS, "bind.injective", "I64 0", NULL),
          "a write view with stride 0 is not injective (A-7)");
    b[0].mode = CINT_MODE_IN;
    CHECK(binds_call(ctx, 100, b, 1u) == CINT_OK, "an in view need not be injective, and stride 0 binds");
    v = view_of(a, 0, 4, CINT_VIEW_WRITE);
    w = view_of(a, 3, 2, CINT_VIEW_READ);
    b[0] = bind_of(&w, 0u, CINT_MODE_IN);
    b[1] = bind_of(&v, 1u, CINT_MODE_INOUT);
    CHECK(binds_call(ctx, 100, b, 2u) == CINT_FAULT && bind_record_is(ctx, CINT_E_ALIAS, "bind.alias", "I64 1, I64 0",
                                                                      NULL),
          "overlap: the writable parameter, then the one it overlaps");
    w.buffer = ro;
    CHECK(binds_call(ctx, 100, b, 2u) == CINT_FAULT && bind_record_is(ctx, CINT_E_ALIAS, "bind.alias", "I64 1, I64 0",
                                                                      NULL),
          "two registrations of the same bytes overlap (H-13 T1)");
    v = view_of(a, 0, 4, CINT_VIEW_WRITE);
    v.stride[0] = 2;
    w = view_of(a, 1, 4, CINT_VIEW_READ);
    w.stride[0] = 2;
    CHECK(binds_call(ctx, 100, b, 2u) == CINT_OK && b[0].ptr == (void *)(g_words + 1) && b[1].ptr == (void *)g_words,
          "interleaved views of one registration are disjoint by T2");
    w.buffer = ro;
    CHECK(binds_call(ctx, 100, b, 2u) == CINT_FAULT && bind_record_is(ctx, CINT_E_ALIAS, "bind.alias", "I64 1, I64 0",
                                                                      NULL),
          "the same layout over two registrations is uncertain (A-5)");
    v = view_of(a, 0, 2, CINT_VIEW_READ);
    v.rank = 2u;
    v.shape[1] = 2;
    v.stride[1] = 2;  /* column-major */
    b[0] = bind_of(&v, 0u, CINT_MODE_IN);
    b[0].rank = 2u;
    CHECK(binds_call(ctx, 100, b, 1u) == CINT_OK && b[0].ptr == (void *)g_words,
          "a column-major view of rank 2 binds: a function's check 8 refuses nothing (RT-OQ-38)");

    /* Records with no storage: element intervals within one registration. */
    v = view_of(rec, 0, 2, CINT_VIEW_WRITE);
    v.type = T_REC;
    w = view_of(rec, 2, 3, CINT_VIEW_READ);
    w.type = T_REC;
    b[0] = bind_of(&v, 0u, CINT_MODE_INOUT);
    b[1] = bind_of(&w, 1u, CINT_MODE_IN);
    b[0].want = b[1].want = &T_REC;
    b[0].elem_bytes = b[1].elem_bytes = 0;
    CHECK(binds_call(ctx, 100, b, 2u) == CINT_OK && b[0].ptr == NULL && b[1].ptr == NULL,
          "disjoint element intervals of a record with no storage");
    w.origin = 1;
    CHECK(binds_call(ctx, 100, b, 2u) == CINT_FAULT && bind_record_is(ctx, CINT_E_ALIAS, "bind.alias", "I64 0, I64 1",
                                                                      NULL),
          "overlapping element intervals of a record with no storage");

    /* With fuel 0 a refusal is still a refusal, and a bound entry faults E_FUEL. */
    v = view_of(a, 0, 1, CINT_VIEW_READ);
    v.buffer = 99u;
    b[0] = bind_of(&v, 0u, CINT_MODE_IN);
    CHECK(binds_call(ctx, 0, b, 1u) == CINT_REFUSED, "fuel 0: refused before any charge");
    v.buffer = a;
    CHECK(binds_call(ctx, 0, b, 1u) == CINT_FAULT && fuel_of(ctx) == 0, "fuel 0: E_FUEL at the entry's unit");
    get_fault(ctx, &g_rec);
    CHECK(g_rec.code == CINT_E_FUEL && cint_ctx_clear_fault(ctx) == CINT_OK, "E_FUEL, cleared");

    /* Misuse and context states. */
    CHECK(!cint_rt_bind_views(ctx, S_CALL, b, 1u) && !cint_rt_refuse(ctx, CINT_REFUSAL_RESULT),
          "outside an entry nothing happens");
    CHECK(cint_rt_entry_open(ctx, S_FUEL, 100, 8) == CINT_OK && !cint_rt_bind_views(ctx, S_CALL, NULL, 1u) &&
              cint_rt_entry_end(ctx) == CINT_REFUSED && refusal_of(ctx) == CINT_REFUSAL_CALL,
          "no binds refused");
    CHECK(cint_rt_entry_open(ctx, S_FUEL, 100, 8) == CINT_OK && !cint_rt_refuse(ctx, CINT_REFUSAL_RESULT) &&
              cint_rt_entry_end(ctx) == CINT_REFUSED && refusal_of(ctx) == CINT_REFUSAL_RESULT,
          "cint_rt_refuse records its reason");
    b[0].rank = 0u;
    CHECK(binds_call(ctx, 100, b, 1u) == CINT_REFUSED && refusal_of(ctx) == CINT_REFUSAL_CALL,
          "a declared rank of 0 is a wrapper error");
    cint_ctx_destroy(ctx);
}

/* An output callback that keeps what it receives. */
typedef struct sink {
    uint8_t bytes[256];
    size_t total;     /* bytes received */
    size_t last_len;  /* length of the last call */
    int calls;
    cint_status status;    /* returned to the runtime */
    cint_ctx *reenter;     /* when set, the callback tries to clear its context */
    cint_status reentered; /* what that returned */
} sink;

static cint_status sink_write(void *user, const uint8_t *bytes, size_t len)
{
    sink *s = (sink *)user;
    size_t room = sizeof s->bytes - (s->total < sizeof s->bytes ? s->total : sizeof s->bytes);
    s->calls++;
    s->last_len = len;
    if (s->reenter != NULL) {
        s->reentered = cint_ctx_clear_fault(s->reenter);
    }
    if (s->status != CINT_OK) {
        return s->status;
    }
    memcpy(s->bytes + (sizeof s->bytes - room), bytes, len < room ? len : room);
    s->total += len;
    return CINT_OK;
}

static cint_ctx *sink_ctx(sink *s, int *live)
{
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL;
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = &t_program;
    cfg.allocator.alloc = test_alloc;
    cfg.allocator.release = test_release;
    cfg.allocator.user = live;
    cfg.output = s != NULL ? sink_write : NULL;
    cfg.output_user = s;
    return cint_ctx_create(&cfg, &ctx) == CINT_OK ? ctx : NULL;
}

#define SINK_IS(s, text) ((s).total == sizeof(text) - 1u && memcmp((s).bytes, (text), sizeof(text) - 1u) == 0)

static cint_buffer_desc host_desc(void *ptr, cint_type type, int64_t extent, uint32_t writable)
{
    cint_buffer_desc d;
    memset(&d, 0, sizeof d);
    d.size = (uint32_t)sizeof d;
    d.writable = writable;
    d.type = type;
    d.mem.kind = CINT_MEM_HOST;
    d.mem.u.host.ptr = ptr;
    d.extent = extent;
    return d;
}

/* A view of n elements from origin with one stride, read into out. */
static cint_status read_of(cint_ctx *ctx, cint_buffer_id id, int64_t origin, int64_t n, int64_t stride, int64_t *out,
                           size_t bytes)
{
    cint_view v = view_of(id, origin, n, CINT_VIEW_READ);
    v.stride[0] = stride;
    return cint_buffer_read(ctx, v, out, bytes);
}

/* SPEC-03 5.3: descriptor registration with its generation, created buffers,
 * reads, export leases, config.max_buffers, and lower bounds (RT-OQ-39). */
static void test_registry(void)
{
    static int64_t words[8] = {10, 11, 12, 13, 14, 15, 16, 17};
    static const cint_type rec16 = {CINT_TAG_RECORD, 0u, 0u, 0u, 0u, 0u, 0u};
    static const cint_type rec0 = {CINT_TAG_RECORD, 0u, 0u, 0u, 0u, 1u, 0u};
    static const cint_type u8 = {CINT_TAG_U8, 0u, 0u, 0u, 0u, 0u, 0u};
    static const struct { const char *what; int field; uint32_t reason; } cases[] = {
        {"a descriptor of another size", 0, CINT_REFUSAL_CALL},
        {"writable 2", 1, CINT_REFUSAL_CALL},
        {"a nonzero memory reserved field", 2, CINT_REFUSAL_CALL},
        {"publish copy of a read-only buffer", 3, CINT_REFUSAL_CALL},
        {"publish 2", 4, CINT_REFUSAL_CALL},
        {"a nonzero synchronization reserved field", 5, CINT_REFUSAL_CALL},
        {"CUDA memory", 6, CINT_REFUSAL_UNSUPPORTED},
        {"a device", 7, CINT_REFUSAL_UNSUPPORTED},
        {"a synchronization object", 8, CINT_REFUSAL_UNSUPPORTED},
        {"fixed-point storage on an integer type", 9, CINT_REFUSAL_TYPE},
        {"a record outside the layout table", 10, CINT_REFUSAL_TYPE},
        {"a negative extent", 11, CINT_REFUSAL_EXTENT},
        {"a byte size beyond I64", 12, CINT_REFUSAL_SIZE},
        {"a NULL address", 13, CINT_REFUSAL_ALIGN},
        {"a misaligned address", 14, CINT_REFUSAL_ALIGN},
    };
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL, *small = NULL;
    cint_buffer_desc d;
    cint_buffer_id id = 0u, ro = 0u, rec = 0u, zero = 0u, made = 0u, empty = 0u, extra = 0u;
    uint64_t gen = 0u, lease = 0u, lease2 = 0u;
    const void *lp = NULL;
    int64_t out[8], n = -1, before;
    void *p = NULL;
    cint_view v;
    cint_bind b[1];
    int live = 0;
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = &t_program;
    cfg.module = &t_module_info;
    cfg.allocator.alloc = test_alloc;
    cfg.allocator.release = test_release;
    cfg.allocator.user = &live;
    if (cint_ctx_create(&cfg, &ctx) != CINT_OK) {
        CHECK(0, "cint_ctx_create with a module");
        return;
    }

    /* Refused descriptors register nothing and leave the reason (A-8). */
    for (size_t k = 0u; k < sizeof cases / sizeof cases[0]; k++) {
        d = host_desc(words, T_I64, 8, 1u);
        switch (cases[k].field) {
        case 0: d.size = 104u; break;
        case 1: d.writable = 2u; break;
        case 2: d.mem.reserved = 1u; break;
        case 3: d.writable = 0u; d.publish = CINT_PUBLISH_COPY; break;
        case 4: d.publish = 2u; break;
        case 5: d.sync.reserved = 1u; break;
        case 6: d.mem.kind = CINT_MEM_CUDA; break;
        case 7: d.device = 1u; break;
        case 8: d.sync.kind = CINT_MEM_CUDA; break;
        case 9: d.type.storage = CINT_TAG_I64; break;
        case 10: d.type = rec16; d.type.record_id = 2u; break;
        case 11: d.extent = -1; break;
        case 12: d.extent = INT64_MAX / 4; break;
        case 13: d.mem.u.host.ptr = NULL; break;
        default: d.mem.u.host.ptr = (unsigned char *)(void *)words + 4; break;
        }
        id = 0u;
        gen = 0u;
        CHECK(cint_buffer_register(ctx, &d, &id, &gen) == CINT_REFUSED && refusal_of(ctx) == cases[k].reason &&
                  id == 0u && gen == 0u,
              cases[k].what);
    }
    d = host_desc(words, T_I64, 8, 1u);
    CHECK(cint_buffer_register(ctx, NULL, &id, &gen) == CINT_REFUSED && refusal_of(ctx) == CINT_REFUSAL_CALL,
          "a NULL descriptor");
    CHECK(cint_buffer_register(ctx, &d, &id, NULL) == CINT_REFUSED, "a NULL generation pointer");

    /* A registration returns its id and generation and is typed. */
    d.publish = CINT_PUBLISH_COPY;
    CHECK(cint_buffer_register(ctx, &d, &id, &gen) == CINT_OK && id == 1u && gen == CINT_BUFFER_GENERATION_FIRST,
          "the first registration: id 1 at the first generation");
    d = host_desc(words, T_I64, 8, 0u);
    CHECK(cint_buffer_register(ctx, &d, &ro, &gen) == CINT_OK && ro == 2u, "a read-only registration");
    v = view_of(id, 1, 3, CINT_VIEW_WRITE);
    CHECK(bind_call(ctx, &v, CINT_MODE_INOUT, &p, &n) == CINT_OK && p == (void *)&words[1] && n == 3,
          "a view of a descriptor registration binds");
    v.type = u8;
    CHECK(bind_call(ctx, &v, CINT_MODE_IN, &p, &n) == CINT_REFUSED && refusal_of(ctx) == CINT_REFUSAL_TYPE,
          "a view of another type than the registration's is refused (V-16)");
    v = view_of(ro, 0, 1, CINT_VIEW_WRITE);
    CHECK(bind_call(ctx, &v, CINT_MODE_INOUT, &p, &n) == CINT_REFUSED && refusal_of(ctx) == CINT_REFUSAL_PERMISSION,
          "a write view of a read-only registration is refused");
    d = host_desc(words, rec16, 4, 1u);
    CHECK(cint_buffer_register(ctx, &d, &rec, &gen) == CINT_OK, "a record registration from the module's layout");
    d = host_desc(NULL, rec0, 5, 1u);
    CHECK(cint_buffer_register(ctx, &d, &zero, &gen) == CINT_OK, "a record with no storage needs no address");

    /* cint_buffer_read: row-major order of the view's index tuples. */
    memset(out, 0, sizeof out);
    CHECK(read_of(ctx, id, 1, 3, 2, out, 24u) == CINT_OK && out[0] == 11 && out[1] == 13 && out[2] == 15,
          "a strided read");
    CHECK(read_of(ctx, id, 7, 4, -2, out, 32u) == CINT_OK && out[0] == 17 && out[1] == 15 && out[3] == 11,
          "a read with a negative stride (A-8a)");
    v = view_of(id, 0, 2, CINT_VIEW_READ);
    v.rank = 2u;
    v.shape[1] = 3;
    v.stride[0] = 1;
    v.stride[1] = 2;
    v.lower[0] = -5;
    CHECK(cint_buffer_read(ctx, v, out, 48u) == CINT_OK && out[0] == 10 && out[1] == 12 && out[2] == 14 &&
              out[3] == 11 && out[5] == 15,
          "a rank 2 read; a lower bound moves no element (V-7)");
    v.lower[0] = INT64_MAX;
    CHECK(cint_buffer_read(ctx, v, out, 48u) == CINT_REFUSED && refusal_of(ctx) == CINT_REFUSAL_SIZE,
          "a lower bound whose last index passes I64 is refused");
    CHECK(read_of(ctx, id, 0, 3, 1, out, 16u) == CINT_REFUSED && refusal_of(ctx) == CINT_REFUSAL_SIZE,
          "a destination of another size is refused");
    CHECK(read_of(ctx, id, 7, 2, 1, out, 16u) == CINT_REFUSED && refusal_of(ctx) == CINT_REFUSAL_EXTENT,
          "a read past the registration is refused");
    CHECK(read_of(ctx, id, 8, 0, 1, NULL, 0u) == CINT_OK, "an empty read at the end");
    CHECK(read_of(ctx, 99u, 0, 1, 1, out, 8u) == CINT_REFUSED && refusal_of(ctx) == CINT_REFUSAL_BUFFER,
          "a read of an unknown id");
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK && read_of(ctx, id, 0, 1, 1, out, 8u) == CINT_BUSY &&
              cint_rt_entry_end(ctx) == CINT_OK,
          "a read during an entry is CINT_BUSY (A-11)");

    /* cint_buffer_create: zero-filled runtime-owned storage (A-9). */
    CHECK(cint_buffer_create(ctx, T_I64, 4, 1u, &made, &gen) == CINT_REFUSED &&
              refusal_of(ctx) == CINT_REFUSAL_UNSUPPORTED,
          "create on a device is refused");
    CHECK(cint_buffer_create(ctx, T_I64, -1, 0u, &made, &gen) == CINT_REFUSED && refusal_of(ctx) == CINT_REFUSAL_EXTENT,
          "create with a negative extent is refused");
    CHECK(cint_buffer_create(ctx, T_I64, 4, 0u, &made, &gen) == CINT_OK && made == 5u &&
              gen == CINT_BUFFER_GENERATION_FIRST && live == 4,
          "create allocates through the context's allocator");
    CHECK(read_of(ctx, made, 0, 4, 1, out, 32u) == CINT_OK && out[0] == 0 && out[3] == 0, "created storage is zero");
    v = view_of(made, 0, 4, CINT_VIEW_WRITE);
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK &&
              cint_view_bind(ctx, &v, &T_I64, CINT_MODE_INOUT, 8, &p, &n) && n == 4,
          "a created buffer binds for inout");
    ((int64_t *)p)[2] = 42;
    CHECK(cint_rt_entry_end(ctx) == CINT_OK, "end");

    /* Export leases (A-9). */
    CHECK(cint_buffer_lease(ctx, id, &lp, &lease) == CINT_REFUSED && refusal_of(ctx) == CINT_REFUSAL_LEASE,
          "borrowed memory has no lease");
    CHECK(cint_buffer_lease(ctx, made, &lp, &lease) == CINT_OK && lease == 1u && lp == p &&
              ((const int64_t *)lp)[2] == 42,
          "a lease gives the created buffer's storage");
    CHECK(cint_buffer_lease(ctx, made, &lp, &lease2) == CINT_OK && lease2 == 2u, "a second lease");
    before = fuel_of(ctx);
    CHECK(bind_call(ctx, &v, CINT_MODE_INOUT, &p, &n) == CINT_BUSY && refusal_of(ctx) == CINT_REFUSAL_LEASED &&
              fuel_of(ctx) == before,
          "binding a leased buffer for inout is CINT_BUSY, fuel as it was");
    b[0] = bind_of(&v, 0u, CINT_MODE_INOUT);
    CHECK(binds_call(ctx, 100, b, 1u) == CINT_BUSY && refusal_of(ctx) == CINT_REFUSAL_LEASED,
          "so is a T3 binding");
    b[0] = bind_of(&v, 0u, CINT_MODE_IN);
    CHECK(binds_call(ctx, 100, b, 1u) == CINT_OK, "a leased buffer still binds for in");
    CHECK(cint_buffer_release(ctx, made) == CINT_BUSY, "a leased buffer is not released");
    CHECK(cint_lease_end(ctx, lease) == CINT_OK && cint_lease_end(ctx, lease) == CINT_REFUSED &&
              refusal_of(ctx) == CINT_REFUSAL_LEASE && cint_lease_end(ctx, 0u) == CINT_REFUSED,
          "a lease ends once");
    CHECK(bind_call(ctx, &v, CINT_MODE_INOUT, &p, &n) == CINT_BUSY, "one lease is still live");
    CHECK(cint_lease_end(ctx, lease2) == CINT_OK && bind_call(ctx, &v, CINT_MODE_INOUT, &p, &n) == CINT_OK,
          "with no lease the buffer binds for inout again");
    CHECK(cint_buffer_release(ctx, made) == CINT_OK && live == 3, "release frees created storage");
    CHECK(read_of(ctx, made, 0, 1, 1, out, 8u) == CINT_REFUSED && refusal_of(ctx) == CINT_REFUSAL_GENERATION,
          "a read of a released buffer is refused");
    CHECK(cint_buffer_create(ctx, T_I64, 0, 0u, &empty, &gen) == CINT_OK && live == 3 &&
              cint_buffer_lease(ctx, empty, &lp, &lease) == CINT_OK && lp == NULL &&
              cint_lease_end(ctx, lease) == CINT_OK,
          "an empty created buffer has no storage");
    CHECK(cint_buffer_create(ctx, T_I64, 2, 0u, &made, &gen) == CINT_OK && live == 4, "one more created buffer");
    cint_ctx_destroy(ctx);
    CHECK(live == 0, "destroy frees created storage, the registry and the lease table");

    /* config.max_buffers bounds live registrations; a module is needed for records. */
    cfg.max_buffers = 2u;
    cfg.module = NULL;
    if (cint_ctx_create(&cfg, &small) != CINT_OK) {
        CHECK(0, "cint_ctx_create with max_buffers 2");
        return;
    }
    d = host_desc(words, rec16, 4, 1u);
    CHECK(cint_buffer_register(small, &d, &id, &gen) == CINT_REFUSED && refusal_of(small) == CINT_REFUSAL_TYPE,
          "with no module a record type is refused");
    d = host_desc(words, T_I64, 8, 1u);
    CHECK(cint_buffer_register(small, &d, &id, &gen) == CINT_OK && cint_buffer_create(small, T_I64, 1, 0u, &made, &gen) == CINT_OK,
          "two registrations");
    CHECK(cint_buffer_register(small, &d, &extra, &gen) == CINT_RESOURCE &&
              cint_buffer_create(small, T_I64, 1, 0u, &extra, &gen) == CINT_RESOURCE && live == 4,
          "a third is CINT_RESOURCE, nothing allocated");
    CHECK(cint_buffer_release(small, id) == CINT_OK && cint_buffer_register(small, &d, &extra, &gen) == CINT_OK &&
              extra == 3u,
          "a released slot takes a new id");
    cint_ctx_destroy(small);
    CHECK(live == 0, "nothing leaked");
}

static void test_output(void)
{
    static const struct {
        uint32_t tag;
        uint64_t bits;
        const char *text;
    } formats[] = {
        {CINT_TAG_I8, (uint64_t)(int64_t)-128, "-128"}, {CINT_TAG_I8, 127u, "127"},
        {CINT_TAG_I8, 0x80u, "-128"},  /* the low 8 bits, sign-extended */
        {CINT_TAG_U8, 255u, "255"}, {CINT_TAG_U8, 0x1FFu, "255"}, {CINT_TAG_U8, 0u, "0"},
        {CINT_TAG_I16, (uint64_t)(int64_t)-32768, "-32768"}, {CINT_TAG_I16, 32767u, "32767"},
        {CINT_TAG_U16, 65535u, "65535"},
        {CINT_TAG_I32, (uint64_t)(int64_t)INT32_MIN, "-2147483648"}, {CINT_TAG_I32, 2147483647u, "2147483647"},
        {CINT_TAG_U32, 4294967295u, "4294967295"},
        {CINT_TAG_I64, (uint64_t)INT64_MIN, "-9223372036854775808"},
        {CINT_TAG_I64, (uint64_t)INT64_MAX, "9223372036854775807"},
        {CINT_TAG_I64, (uint64_t)(int64_t)-1, "-1"}, {CINT_TAG_I64, 0u, "0"}, {CINT_TAG_I64, 1000u, "1000"},
        {CINT_TAG_U64, UINT64_MAX, "18446744073709551615"}, {CINT_TAG_U64, 10u, "10"},
        {CINT_TAG_BOOL, 0u, "false"}, {CINT_TAG_BOOL, 1u, "true"},
        {CINT_TAG_BOOL, 2u, ""}, {CINT_TAG_Z, 1u, ""}, {0u, 1u, ""},
    };
    char text[CINT_FORMAT_MAX];
    sink s;
    cint_ctx *ctx;
    int live = 0;
    int64_t c = 0;
    size_t i, len;
    char buf[64];
    for (i = 0; i < sizeof formats / sizeof formats[0]; i++) {
        len = cint_format_value(formats[i].tag, formats[i].bits, text);
        CHECK(len == strlen(formats[i].text) && memcmp(text, formats[i].text, len) == 0, formats[i].text);
    }

    memset(&s, 0, sizeof s);
    ctx = sink_ctx(&s, &live);
    if (ctx == NULL) {
        CHECK(0, "cint_ctx_create with an output callback");
        return;
    }
    /* One statement, delivered whole at its end. */
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK, "entry");
    CHECK(cint_rt_print_begin(ctx, S_OP) && cint_rt_print_bytes(ctx, "x=", 2u) &&
              cint_rt_print_value(ctx, CINT_TAG_I64, (uint64_t)INT64_MIN) && cint_rt_print_bytes(ctx, " y=", 3u) &&
              cint_rt_print_value(ctx, CINT_TAG_U64, UINT64_MAX) && cint_rt_print_bytes(ctx, " ", 1u) &&
              cint_rt_print_value(ctx, CINT_TAG_BOOL, 1u) && cint_rt_print_bytes(ctx, "\n", 1u),
          "a statement is staged");
    CHECK(s.calls == 0, "nothing is written before the statement ends");
    CHECK(cint_rt_print_end(ctx), "print_end");
    CHECK(s.calls == 1 && SINK_IS(s, "x=-9223372036854775808 y=18446744073709551615 true\n"),
          "one callback with the exact bytes (IM-90)");
    /* A hole that faults writes nothing from its statement (LS-197). */
    CHECK(cint_rt_print_begin(ctx, S_OP) && cint_rt_print_bytes(ctx, "sum=", 4u), "a second statement");
    CHECK(!cint_add_i64(ctx, S_OP, INT64_MAX, 1, &c), "its hole faults");
    CHECK(!cint_rt_print_value(ctx, CINT_TAG_I64, 1u) && !cint_rt_print_end(ctx), "printing on a faulted context");
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT, "faulted");
    CHECK(s.calls == 1 && s.total == 51u, "the faulting statement wrote nothing");
    CHECK(cint_ctx_clear_fault(ctx) == CINT_OK, "clear");
    memset(&s, 0, sizeof s);
    /* Print, then fault: the bytes before the fault stay written (D-19). */
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK, "entry");
    CHECK(cint_rt_print_begin(ctx, S_OP) && cint_rt_print_bytes(ctx, "partial: big=", 13u) &&
              cint_rt_print_value(ctx, CINT_TAG_I64, (uint64_t)INT64_MAX) && cint_rt_print_end(ctx),
          "a statement with no line feed");
    CHECK(!cint_add_i64(ctx, S_OP, INT64_MAX, 1, &c), "then a fault");
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT, "faulted");
    CHECK(s.calls == 1 && SINK_IS(s, "partial: big=9223372036854775807"), "32 bytes, no LF");
    CHECK(cint_ctx_clear_fault(ctx) == CINT_OK, "clear");
    /* An empty statement still reaches the callback; the callback cannot
     * re-enter the context (A-11, H-9). */
    memset(&s, 0, sizeof s);
    s.reenter = ctx;
    s.reentered = CINT_OK;
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK, "entry");
    CHECK(cint_rt_print_begin(ctx, S_OP) && cint_rt_print_end(ctx), "an empty statement");
    CHECK(cint_rt_entry_end(ctx) == CINT_OK, "end");
    CHECK(s.calls == 1 && s.last_len == 0u && s.reentered == CINT_BUSY, "called with 0 bytes; re-entry is BUSY");
    s.reenter = NULL;
    /* A statement larger than the staging buffer's first size, still in one call. */
    memset(&s, 0, sizeof s);
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK, "entry");
    CHECK(cint_rt_print_begin(ctx, S_OP), "begin");
    for (i = 0; i < 1000u; i++) {
        uint8_t chunk[100];
        memset(chunk, (int)('0' + i % 10u), sizeof chunk);
        if (!cint_rt_print_bytes(ctx, chunk, sizeof chunk)) {
            break;
        }
    }
    CHECK(i == 1000u && cint_rt_print_end(ctx), "100,000 bytes staged");
    CHECK(cint_rt_entry_end(ctx) == CINT_OK && s.calls == 1 && s.last_len == 100000u && s.total == 100000u,
          "and delivered in one call");
    /* Calls out of order, and an invalid value, fault E_UNSUPPORTED dispatch.admit. */
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK, "entry");
    CHECK(!cint_rt_print_bytes(ctx, "x", 1u), "print_bytes without print_begin");
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT, "faulted");
    get_fault(ctx, &g_rec);
    CHECK(g_rec.code == CINT_E_UNSUPPORTED && g_rec.operation_len == 14u &&
              memcmp(g_rec.operation, "dispatch.admit", 14u) == 0,
          "dispatch.admit");
    CHECK(cint_ctx_clear_fault(ctx) == CINT_OK, "clear");
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK, "entry");
    CHECK(cint_rt_print_begin(ctx, S_OP) && !cint_rt_print_value(ctx, CINT_TAG_BOOL, 2u), "Bool 2 does not print");
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT, "faulted");
    CHECK(cint_ctx_clear_fault(ctx) == CINT_OK, "clear");
    /* A callback that fails faults the entry: host.error.print, with its status. */
    memset(&s, 0, sizeof s);
    s.status = CINT_RESOURCE;
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK, "entry");
    CHECK(cint_rt_print_begin(ctx, S_OP) && cint_rt_print_bytes(ctx, "x\n", 2u) && !cint_rt_print_end(ctx),
          "print_end fails when the callback does");
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT, "faulted");
    get_fault(ctx, &g_rec);
    CHECK(g_rec.code == CINT_E_UNSUPPORTED && g_rec.operation_len == 16u &&
              memcmp(g_rec.operation, "host.error.print", 16u) == 0 && g_rec.operand_count == 1u &&
              strcmp(render(&g_rec.operands[0], buf, sizeof buf), "I64 5") == 0,
          "E_UNSUPPORTED host.error.print, operand I64 5");
    CHECK(cint_ctx_clear_fault(ctx) == CINT_OK, "clear");
    cint_ctx_destroy(ctx);
    CHECK(live == 0, "the staging buffer is released with the context");

    /* Staging that cannot grow faults host.resource. */
    ctx = sink_ctx(&s, &live);
    if (ctx != NULL) {
        memset(&s, 0, sizeof s);
        CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK, "entry");
        g_fail_alloc = 1;
        CHECK(cint_rt_print_begin(ctx, S_OP) && !cint_rt_print_bytes(ctx, "x", 1u), "staging fails");
        g_fail_alloc = 0;
        CHECK(cint_rt_entry_end(ctx) == CINT_FAULT && s.calls == 0, "faulted, nothing written");
        get_fault(ctx, &g_rec);
        CHECK(g_rec.code == CINT_E_UNSUPPORTED && g_rec.operation_len == 13u &&
                  memcmp(g_rec.operation, "host.resource", 13u) == 0 && g_rec.operand_count == 0u,
              "E_UNSUPPORTED host.resource");
        cint_ctx_destroy(ctx);
    }
    /* With no callback, statements are staged and discarded. */
    ctx = sink_ctx(NULL, &live);
    if (ctx != NULL) {
        CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK, "entry");
        CHECK(cint_rt_print_begin(ctx, S_OP) && cint_rt_print_bytes(ctx, "x\n", 2u) && cint_rt_print_end(ctx),
              "a statement with no callback");
        CHECK(cint_rt_entry_end(ctx) == CINT_OK, "end");
        cint_ctx_destroy(ctx);
    }
    CHECK(live == 0, "nothing leaked");
}

/* Holes with a format specification (SPEC-04 LS-202 to LS-212): the text of cint_ref's
 * fmt.render for each row, the forms the static rules exclude, and the 1 GiB of staging. */
static void test_print_format(void)
{
    static const struct {
        uint32_t tag;
        uint64_t bits;
        uint32_t form, fill;
        uint64_t width, scale;
        const char *text;
    } rows[] = {
        {CINT_TAG_I64, 123456u, 1u, ' ', 0u, 2u, "1234.56"},  /* {:/100} */
        {CINT_TAG_I64, (uint64_t)(int64_t)-5, 1u, ' ', 0u, 2u, "-0.05"},  /* {:/100} */
        {CINT_TAG_I64, 0u, 1u, ' ', 0u, 3u, "0.000"},  /* {:/1000} */
        {CINT_TAG_I64, (uint64_t)INT64_MIN, 1u, ' ', 0u, 2u, "-92233720368547758.08"},  /* {:/100} */
        {CINT_TAG_U64, UINT64_MAX, 1u, ' ', 0u, 24u, "0.000018446744073709551615"},  /* a scale of 10^24 */
        {CINT_TAG_I64, 123456u, 1u | CINT_FORMAT_RIGHT, ' ', 10u, 0u, "    123456"},  /* {:>10} */
        {CINT_TAG_I64, 123456u, 1u | CINT_FORMAT_LEFT, '*', 10u, 0u, "123456****"},  /* {:*<10} */
        {CINT_TAG_I64, 123456u, 1u | CINT_FORMAT_CENTER, ' ', 11u, 0u, "  123456   "},  /* {:^11} */
        {CINT_TAG_I64, 1234567u, 1u | CINT_FORMAT_UNDERSCORE, ' ', 0u, 0u, "1_234_567"},  /* {:_} */
        {CINT_TAG_I64, (uint64_t)(int64_t)-1234567, 1u | CINT_FORMAT_COMMA, ' ', 0u, 0u, "-1,234,567"},  /* {:,} */
        {CINT_TAG_U8, 200u, 2u | CINT_FORMAT_ALT, ' ', 0u, 0u, "0xc8"},  /* {:#x} */
        {CINT_TAG_U8, 200u, 3u | CINT_FORMAT_ALT, ' ', 0u, 0u, "0xC8"},  /* {:#X} */
        {CINT_TAG_U8, 200u, 4u | CINT_FORMAT_ALT, ' ', 0u, 0u, "0o310"},  /* {:#o} */
        {CINT_TAG_U8, 200u, 5u | CINT_FORMAT_ALT | CINT_FORMAT_ZERO, ' ', 12u, 0u, "0b0011001000"},  /* {:#012b} */
        {CINT_TAG_I32, (uint64_t)(int64_t)-255, 2u | CINT_FORMAT_ALT | CINT_FORMAT_ZERO, ' ', 8u, 0u, "-0x000ff"},
        {CINT_TAG_U32, 0xdeadbeefu, 2u | CINT_FORMAT_UNDERSCORE, ' ', 0u, 0u, "dead_beef"},  /* {:_x} */
        {CINT_TAG_U64, UINT64_MAX, 5u | CINT_FORMAT_UNDERSCORE, ' ', 0u, 0u,
         "1111_1111_1111_1111_1111_1111_1111_1111_1111_1111_1111_1111_1111_1111_1111_1111"},  /* {:_b} */
        {CINT_TAG_I64, 5u, 1u | CINT_FORMAT_PLUS, ' ', 0u, 0u, "+5"},  /* {:+} */
        {CINT_TAG_I64, 5u, 1u | CINT_FORMAT_SPACE, ' ', 0u, 0u, " 5"},  /* {: } */
        {CINT_TAG_I64, (uint64_t)(int64_t)-5, 1u | CINT_FORMAT_PLUS | CINT_FORMAT_ZERO, ' ', 8u, 2u, "-0000.05"},
        {CINT_TAG_I64, (uint64_t)(int64_t)-5, 6u, ' ', 0u, 0u, "0tNPP"},  /* {:t} */
        {CINT_TAG_I64, 0u, 6u | CINT_FORMAT_RIGHT, '.', 6u, 0u, "...0t0"},  /* {:.>6t} */
        {CINT_TAG_U64, UINT64_MAX, 6u, ' ', 0u, 0u, "0tPNNNN00N0P00N00NN0PPNNPPPNPNNPNPPNPP0NN0N0"},
        {CINT_TAG_I64, (uint64_t)INT64_MIN, 6u, ' ', 0u, 0u, "0tNPNPNNN00NNN00PN0NPPNNP0N0N0P0P0NNP0PN00P"},
        {CINT_TAG_BOOL, 1u, 1u | CINT_FORMAT_CENTER, 0xe9u, 7u, 0u, "\xc3\xa9true\xc3\xa9\xc3\xa9"},  /* U+00E9 */
        {CINT_TAG_BOOL, 0u, 1u | CINT_FORMAT_LEFT, 0x1f600u, 6u, 0u, "false\xf0\x9f\x98\x80"},  /* U+1F600 */
        {CINT_TAG_I8, (uint64_t)(int64_t)-128, 1u, ' ', 0u, 0u, "-128"},  /* {:} */
    };
    /* Forms no compiled program passes: each faults E_UNSUPPORTED dispatch.admit. */
    static const struct {
        uint32_t tag;
        uint64_t bits;
        uint32_t form, fill;
        uint64_t width, scale;
    } refused[] = {
        {CINT_TAG_I64, 1u, 0u, ' ', 0u, 0u},  /* no kind */
        {CINT_TAG_I64, 1u, 7u, ' ', 0u, 0u},
        {CINT_TAG_I64, 1u, 4096u | 1u, ' ', 0u, 0u},
        {CINT_TAG_I64, 1u, 1u | CINT_FORMAT_PLUS | CINT_FORMAT_SPACE, ' ', 0u, 0u},
        {CINT_TAG_I64, 1u, 1u | CINT_FORMAT_UNDERSCORE | CINT_FORMAT_COMMA, ' ', 0u, 0u},
        {CINT_TAG_I64, 1u, 1u | CINT_FORMAT_RIGHT | CINT_FORMAT_ZERO, ' ', 4u, 0u},  /* `0` with `>` */
        {CINT_TAG_I64, 1u, 1u, ' ', 4u, 0u},  /* a width with no alignment */
        {CINT_TAG_I64, 1u, 1u | CINT_FORMAT_ZERO | CINT_FORMAT_UNDERSCORE, ' ', 4u, 0u},
        {CINT_TAG_I64, 1u, 1u | CINT_FORMAT_ALT, ' ', 0u, 0u},  /* `#` with decimal */
        {CINT_TAG_I64, 1u, 1u | CINT_FORMAT_UNDERSCORE, ' ', 0u, 2u},  /* a group with a scale */
        {CINT_TAG_I64, 1u, 2u, ' ', 0u, 2u},  /* a scale with `x` */
        {CINT_TAG_I64, 1u, 2u | CINT_FORMAT_COMMA, ' ', 0u, 0u},
        {CINT_TAG_I64, 1u, 6u | CINT_FORMAT_PLUS, ' ', 0u, 0u},
        {CINT_TAG_I64, 1u, 6u | CINT_FORMAT_ZERO, ' ', 0u, 0u},
        {CINT_TAG_I64, 1u, 1u | CINT_FORMAT_RIGHT, 0xd800u, 4u, 0u},  /* a surrogate fill */
        {CINT_TAG_I64, 1u, 1u | CINT_FORMAT_RIGHT, 0x110000u, 4u, 0u},
        {CINT_TAG_BOOL, 1u, 2u, ' ', 0u, 0u},
        {CINT_TAG_BOOL, 1u, 1u | CINT_FORMAT_PLUS, ' ', 0u, 0u},
        {CINT_TAG_BOOL, 1u, 1u, ' ', 0u, 2u},
        {CINT_TAG_BOOL, 2u, 1u, ' ', 0u, 0u},
        {CINT_TAG_Z, 1u, 1u, ' ', 0u, 0u},
    };
    /* Renderings past the statement's 1 GiB: each faults E_UNSUPPORTED host.resource. */
    static const struct {
        uint32_t form, fill;
        uint64_t width, scale;
    } large[] = {
        {1u | CINT_FORMAT_RIGHT, ' ', ((uint64_t)1 << 30) + 1u, 0u},
        {1u | CINT_FORMAT_ZERO, ' ', UINT64_MAX, 0u},
        {1u | CINT_FORMAT_CENTER, 0x1f600u, ((uint64_t)1 << 28) + 1u, 0u},  /* 4 bytes a fill */
        {1u, ' ', 0u, (uint64_t)1 << 31},
        {1u, ' ', 0u, UINT64_MAX},
    };
    sink s;
    cint_ctx *ctx;
    int live = 0;
    size_t i;
    memset(&s, 0, sizeof s);
    ctx = sink_ctx(&s, &live);
    if (ctx == NULL) {
        CHECK(0, "cint_ctx_create with an output callback");
        return;
    }
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        size_t len = strlen(rows[i].text);
        memset(&s, 0, sizeof s);
        CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK && cint_rt_print_begin(ctx, S_OP) &&
                  cint_rt_print_format(ctx, rows[i].tag, rows[i].bits, rows[i].form, rows[i].fill, rows[i].width,
                                       rows[i].scale) &&
                  cint_rt_print_end(ctx) && cint_rt_entry_end(ctx) == CINT_OK,
              rows[i].text);
        CHECK(s.calls == 1 && s.total == len && memcmp(s.bytes, rows[i].text, len) == 0, rows[i].text);
    }
    for (i = 0; i < sizeof refused / sizeof refused[0]; i++) {
        memset(&s, 0, sizeof s);
        CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK && cint_rt_print_begin(ctx, S_OP) &&
                  !cint_rt_print_format(ctx, refused[i].tag, refused[i].bits, refused[i].form, refused[i].fill,
                                        refused[i].width, refused[i].scale) &&
                  cint_rt_entry_end(ctx) == CINT_FAULT && s.calls == 0,
              "a refused form faults");
        get_fault(ctx, &g_rec);
        CHECK(g_rec.code == CINT_E_UNSUPPORTED && g_rec.operation_len == 14u &&
                  memcmp(g_rec.operation, "dispatch.admit", 14u) == 0,
              "dispatch.admit");
        CHECK(cint_ctx_clear_fault(ctx) == CINT_OK, "clear");
    }
    for (i = 0; i < sizeof large / sizeof large[0]; i++) {
        memset(&s, 0, sizeof s);
        CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK && cint_rt_print_begin(ctx, S_OP) &&
                  cint_rt_print_bytes(ctx, "x", 1u) &&
                  !cint_rt_print_format(ctx, CINT_TAG_I64, 7u, large[i].form, large[i].fill, large[i].width,
                                        large[i].scale) &&
                  cint_rt_entry_end(ctx) == CINT_FAULT && s.calls == 0,
              "a rendering past 1 GiB faults");
        get_fault(ctx, &g_rec);
        CHECK(g_rec.code == CINT_E_UNSUPPORTED && g_rec.operation_len == 13u &&
                  memcmp(g_rec.operation, "host.resource", 13u) == 0 && g_rec.operand_count == 0u,
              "host.resource");
        CHECK(cint_ctx_clear_fault(ctx) == CINT_OK, "clear");
    }
    /* A wide fill is staged in pieces and delivered in one call. */
    memset(&s, 0, sizeof s);
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK && cint_rt_print_begin(ctx, S_OP) &&
              cint_rt_print_format(ctx, CINT_TAG_I64, 7u, 1u | CINT_FORMAT_LEFT, 0xe9u, 100000u, 0u) &&
              cint_rt_print_end(ctx) && cint_rt_entry_end(ctx) == CINT_OK,
          "a width of 100,000");
    CHECK(s.calls == 1 && s.total == 1u + 2u * 99999u && s.bytes[0] == '7' && s.bytes[1] == 0xc3u &&
              s.bytes[2] == 0xa9u && s.bytes[253] == 0xc3u && s.bytes[254] == 0xa9u,
          "199,999 bytes in one call");
    /* Fills of fewer copies than a 256-byte block holds, as many, and more (G-C2 review PORT-1). */
    {
        static const struct {
            uint32_t fill;
            uint64_t width;
            size_t unit;
        } fills[] = {
            {'-', 256u, 1u}, {'-', 257u, 1u}, {'-', 300u, 1u},
            {0x20acu, 85u, 3u}, {0x20acu, 86u, 3u}, {0x20acu, 100u, 3u},  /* U+20AC */
        };
        static const uint8_t euro[3] = {0xe2u, 0x82u, 0xacu};
        size_t k;
        for (i = 0; i < sizeof fills / sizeof fills[0]; i++) {
            size_t total = 1u + fills[i].unit * (size_t)(fills[i].width - 1u);
            int same = 1;
            memset(&s, 0, sizeof s);
            CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK && cint_rt_print_begin(ctx, S_OP) &&
                      cint_rt_print_format(ctx, CINT_TAG_I64, 7u, 1u | CINT_FORMAT_RIGHT, fills[i].fill,
                                           fills[i].width, 0u) &&
                      cint_rt_print_end(ctx) && cint_rt_entry_end(ctx) == CINT_OK,
                  "a fill near a block's size");
            for (k = 0; k + 1u < total && k < sizeof s.bytes; k++) {
                uint8_t want = fills[i].unit == 1u ? (uint8_t)fills[i].fill : euro[k % 3u];
                same = same && s.bytes[k] == want;
            }
            CHECK(s.calls == 1 && s.total == total && same && (total > sizeof s.bytes || s.bytes[total - 1u] == '7'),
                  "the fill's copies and the value");
        }
    }
    /* Without print_begin, and on a faulted context. */
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK &&
              !cint_rt_print_format(ctx, CINT_TAG_I64, 1u, 1u, ' ', 0u, 0u) && cint_rt_entry_end(ctx) == CINT_FAULT,
          "print_format without print_begin");
    get_fault(ctx, &g_rec);
    CHECK(g_rec.code == CINT_E_UNSUPPORTED && g_rec.operation_len == 14u &&
              memcmp(g_rec.operation, "dispatch.admit", 14u) == 0,
          "dispatch.admit");
    CHECK(!cint_rt_print_format(ctx, CINT_TAG_I64, 1u, 1u, ' ', 0u, 0u), "printing on a faulted context");
    CHECK(cint_ctx_clear_fault(ctx) == CINT_OK, "clear");
    cint_ctx_destroy(ctx);
    CHECK(live == 0, "nothing leaked");
}

/* ------------------------------------------------------------------------- */
/* Error results (cint_rt.h 6c'; SPEC-03 A-18; rt/OPEN.md RT-OQ-33).          */

static const char *const e_values[2] = {"closed", "full"};
static const cint_error_set e_set = {"rt.program.IoError", CINT_TAG_U16, 2u, e_values};
static const cint_error_set e_other = {"ArithError", CINT_TAG_U8, 2u, e_values};

/* An entry that records `tag` in `set` (nothing for set NULL), then faults
 * when `fault` is set. */
static cint_status error_call(cint_ctx *ctx, const cint_error_set *set, uint64_t tag, bool fault)
{
    int64_t r = 0;
    cint_status st = cint_rt_entry_begin(ctx, S_FUEL, 100, 8);
    if (st != CINT_OK) {
        return st;
    }
    if (set != NULL) {
        cint_rt_error_result(ctx, set, tag);
    }
    if (fault) {
        (void)cint_add_i64(ctx, S_OP, INT64_MAX, 1, &r);
    }
    return cint_rt_entry_end(ctx);
}

static bool error_is(cint_ctx *ctx, const cint_error_set *want, uint64_t tag)
{
    const cint_error_set *set = &e_other;
    uint64_t got = 99u;
    return cint_ctx_error(ctx, &set, &got) == CINT_OK && set == want && got == tag;
}

static void test_errors(void)
{
    cint_ctx *ctx = new_ctx();
    const cint_error_set *set = &e_other;
    uint64_t tag = 99u;
    cint_view v;
    void *p = NULL;
    int64_t n = 0;
    CHECK(ctx != NULL, "context for error results");
    if (ctx == NULL) {
        return;
    }
    CHECK(cint_ctx_error(NULL, &set, &tag) == CINT_REFUSED && cint_ctx_error(ctx, NULL, &tag) == CINT_REFUSED &&
          cint_ctx_error(ctx, &set, NULL) == CINT_REFUSED && set == &e_other && tag == 99u,
          "cint_ctx_error refuses NULL arguments and writes nothing");
    CHECK(error_is(ctx, NULL, 0u), "a fresh context holds no error result");
    cint_rt_error_result(ctx, &e_set, 1u);
    CHECK(error_is(ctx, NULL, 0u), "outside an entry cint_rt_error_result is ignored");

    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK, "entry");
    cint_rt_error_result(ctx, &e_set, 2u);
    CHECK(cint_ctx_error(ctx, &set, &tag) == CINT_BUSY, "cint_ctx_error during an entry is CINT_BUSY");
    cint_rt_error_result(ctx, &e_other, 1u);
    CHECK(cint_rt_entry_end(ctx) == CINT_OK, "an entry that leaves with an error ends CINT_OK (A-18)");
    CHECK(error_is(ctx, &e_set, 2u), "the first error result of the entry is kept");
    get_fault(ctx, &g_rec);
    CHECK(g_rec.code == 0u && fuel_of(ctx) == 1, "no fault record; the entry's fuel is counted");
    CHECK(cint_ctx_clear_fault(ctx) == CINT_OK && error_is(ctx, &e_set, 2u),
          "cint_ctx_clear_fault leaves the error result");

    CHECK(error_call(ctx, NULL, 0u, false) == CINT_OK && error_is(ctx, NULL, 0u),
          "the next entry begins with no error result");
    CHECK(error_call(ctx, &e_set, 0u, false) == CINT_OK && error_is(ctx, NULL, 0u), "tag 0 is ignored");
    CHECK(error_call(ctx, &e_set, 3u, false) == CINT_OK && error_is(ctx, NULL, 0u),
          "a tag above count is ignored");
    CHECK(error_call(ctx, &e_set, 1u, false) == CINT_OK && error_is(ctx, &e_set, 1u), "tag 1");

    /* A refused call is not an entry (H-12): the last entry's error result stays. */
    v = view_of(77u, 0, 1, CINT_VIEW_READ);
    CHECK(bind_call(ctx, &v, CINT_MODE_IN, &p, &n) == CINT_REFUSED && error_is(ctx, &e_set, 1u),
          "a refused call leaves the error result as it was");
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, -2, 8) == CINT_REFUSED && error_is(ctx, &e_set, 1u),
          "a refused budget leaves it too");

    /* An entry that faults after recording an error leaves none, also after a clear. */
    CHECK(error_call(ctx, &e_set, 2u, true) == CINT_FAULT && error_is(ctx, NULL, 0u),
          "an entry that faults leaves no error result");
    CHECK(error_call(ctx, &e_set, 1u, false) == CINT_FAULTED && error_is(ctx, NULL, 0u),
          "a faulted context runs nothing");
    CHECK(cint_ctx_clear_fault(ctx) == CINT_OK && error_is(ctx, NULL, 0u), "and the clear does not restore one");
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK, "entry");
    {
        int64_t r = 0;
        CHECK(!cint_add_i64(ctx, S_OP, INT64_MAX, 1, &r), "fault first");
    }
    cint_rt_error_result(ctx, &e_set, 1u);
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT && error_is(ctx, NULL, 0u),
          "on a faulted context cint_rt_error_result is ignored");
    CHECK(cint_ctx_clear_fault(ctx) == CINT_OK, "clear");
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 0, 8) == CINT_FAULT && error_is(ctx, NULL, 0u),
          "an entry whose own fuel charge faults leaves none");
    cint_ctx_destroy(ctx);
}

/* ------------------------------------------------------------------------- */
/* Result-returning forms (cint_rt.h 10a; SPEC-01 IM-30, IM-188).             */

/* One result form against its checked form: tag 0 and the same value exactly
 * when the checked form succeeds; otherwise the ArithError tag that mirrors
 * the fault code, and *out unwritten. The fault is then cleared. */
static void result_agrees(cint_ctx *ctx, bool ok, uint64_t checked, uint16_t tag, uint64_t result,
                          bool untouched, const char *what)
{
    uint16_t want = 0u;
    if (ok) {
        CHECK(tag == 0u && result == checked, what);
        return;
    }
    get_fault(ctx, &g_rec);
    want = g_rec.code == CINT_E_OVERFLOW ? CINT_ARITH_OVERFLOW : g_rec.code == CINT_E_DIV_ZERO ? CINT_ARITH_DIV_ZERO
         : g_rec.code == CINT_E_SHIFT ? CINT_ARITH_SHIFT : (uint16_t)99u;
    CHECK(tag == want && untouched, what);
    CHECK(cint_ctx_clear_fault(ctx) == CINT_OK, "clear after a checked fault");
}

#define RESULT_PAIR(OP, T, CT, A, B)                                                               \
    do {                                                                                           \
        CT c_ = 0, r_ = (CT)0x5A;                                                                 \
        bool ok_ = cint_##OP##_##T(ctx, S_OP, (A), (B), &c_);                                      \
        uint16_t tag_ = cint_##OP##_result_##T((A), (B), &r_);                                     \
        result_agrees(ctx, ok_, (uint64_t)c_, tag_, (uint64_t)r_, r_ == (CT)0x5A, #OP "_result_" #T); \
        n_++;                                                                                      \
    } while (0)

#define RESULT_TEST(T, CT, TMIN, TMAX, W)                                                         \
    static long result_test_##T(cint_ctx *ctx)                                                     \
    {                                                                                              \
        const CT v[7] = {0, 1, (CT)-1, (CT)(TMIN), (CT)(TMAX), 2, (CT)((TMAX) / 2 + 1)};           \
        const cint_count k[9] = {cint_count_i64(-1), cint_count_##T((CT)-1), cint_count_u8(0),    \
                                 cint_count_i8(1), cint_count_u64((W) - 1u), cint_count_i32((W) - 2), \
                                 cint_count_u16((W)), cint_count_i64((W)), cint_count_u64(UINT64_MAX)}; \
        long n_ = 0;                                                                               \
        for (size_t i = 0; i < 7u; i++) {                                                          \
            for (size_t j = 0; j < 7u; j++) {                                                      \
                RESULT_PAIR(add, T, CT, v[i], v[j]);                                               \
                RESULT_PAIR(sub, T, CT, v[i], v[j]);                                               \
                RESULT_PAIR(mul, T, CT, v[i], v[j]);                                               \
                RESULT_PAIR(div, T, CT, v[i], v[j]);                                               \
                RESULT_PAIR(rem, T, CT, v[i], v[j]);                                               \
            }                                                                                      \
            for (size_t j = 0; j < 9u; j++) {                                                      \
                RESULT_PAIR(shl, T, CT, v[i], k[j]);                                               \
            }                                                                                      \
        }                                                                                          \
        return n_;                                                                                 \
    }
RESULT_TEST(i8, int8_t, INT8_MIN, INT8_MAX, 8u)
RESULT_TEST(i16, int16_t, INT16_MIN, INT16_MAX, 16u)
RESULT_TEST(i32, int32_t, INT32_MIN, INT32_MAX, 32u)
RESULT_TEST(i64, int64_t, INT64_MIN, INT64_MAX, 64u)
RESULT_TEST(u8, uint8_t, 0, UINT8_MAX, 8u)
RESULT_TEST(u16, uint16_t, 0, UINT16_MAX, 16u)
RESULT_TEST(u32, uint32_t, 0, UINT32_MAX, 32u)
RESULT_TEST(u64, uint64_t, 0, UINT64_MAX, 64u)

static void test_result_forms(void)
{
    cint_ctx *ctx = new_ctx();
    long n = 0;
    int8_t q8 = 0;
    int64_t q64 = 0;
    uint32_t r32 = 0u;
    CHECK(ctx != NULL, "context for the result forms");
    if (ctx == NULL) {
        return;
    }
    n += result_test_i8(ctx) + result_test_i16(ctx) + result_test_i32(ctx) + result_test_i64(ctx);
    n += result_test_u8(ctx) + result_test_u16(ctx) + result_test_u32(ctx) + result_test_u64(ctx);
    CHECK(n == 8 * (7 * 7 * 5 + 7 * 9), "every pair of every type compared");
    /* The tags themselves (SPEC-01 IM-188): ArithError { overflow, div_zero, shift, narrow }. */
    CHECK(CINT_ARITH_OVERFLOW == 1u && CINT_ARITH_DIV_ZERO == 2u && CINT_ARITH_SHIFT == 3u && CINT_ARITH_NARROW == 4u,
          "ArithError tags 1 to 4");
    CHECK(cint_div_result_i8(INT8_MIN, -1, &q8) == CINT_ARITH_OVERFLOW && q8 == 0, "I8 MIN / -1 is .overflow");
    CHECK(cint_rem_result_i64(INT64_MIN, -1, &q64) == 0u && q64 == 0, "I64 MIN % -1 is 0");
    CHECK(cint_div_result_i64(-7, 2, &q64) == 0u && q64 == -4, "floor division");
    CHECK(cint_rem_result_u32(7u, 0u, &r32) == CINT_ARITH_DIV_ZERO && r32 == 0u, "% 0 is .div_zero");
    CHECK(cint_shl_result_i8(1, cint_count_i64(-1), &q8) == CINT_ARITH_SHIFT, "a negative count is .shift");
    CHECK(cint_shl_result_i8(1, cint_count_u8(7), &q8) == CINT_ARITH_OVERFLOW && q8 == 0, "1 << 7 in I8 is .overflow");
    CHECK(cint_shl_result_i8(-1, cint_count_u8(7), &q8) == 0u && q8 == INT8_MIN, "-1 << 7 is MIN");
    get_fault(ctx, &g_rec);
    CHECK(g_rec.code == 0u, "the result forms leave the context unfaulted");
    cint_ctx_destroy(ctx);
}

/* Program processes: `test_rt --run <scenario> <dest>` calls cint_program_run,
 * and `test_rt --record <scenario>` prints, in hex, the fault record the
 * scenario leaves, computed in this process; rt/tests/test_rt.py compares
 * stdout, the exit status, and the record bytes at the destination (D-19). */
static const cint_site_info p_sites[] = {
    {0u, 0u, NULL},
    {1u, 1u, "call.enter"},       /* 1: the entry */
    {3u, 13u, "add.checked.i64"}, /* 2: big + 1 (control/print_no_newline_then_fault) */
    {3u, 11u, "add.checked.i64"}, /* 3: the hole {big + 1} (control/print_hole_fault_partial) */
    {2u, 9u, "mul.checked.i64"},  /* 4: 6 * 7 */
};
static const cint_module p_module = {"rt/program.ci", 13u, 5u, p_sites};
static const cint_module *const p_modules[] = {&p_module};
static const cint_program p_program = {1u, 0u, p_modules, NULL};

static cint_site p_site(uint32_t index)
{
    cint_site s;
    s.module = 0u;
    s.index = index;
    return s;
}

static bool p_text(cint_ctx *ctx, const char *text)
{
    return cint_rt_print_begin(ctx, p_site(1u)) && cint_rt_print_bytes(ctx, text, strlen(text)) &&
           cint_rt_print_end(ctx);
}

/* "hello, world\n"; I64 answer = 6 * 7; "answer={answer}\n"; */
static cint_status p_hello(cint_ctx *ctx, int64_t fuel)
{
    int64_t answer = 0;
    cint_status st = cint_rt_entry_begin(ctx, p_site(1u), fuel, CINT_DEPTH_FROM_CONFIG);
    if (st != CINT_OK) {
        return st;
    }
    if (p_text(ctx, "hello, world\n") && cint_mul_i64(ctx, p_site(4u), 6, 7, &answer) &&
        cint_rt_print_begin(ctx, p_site(1u)) && cint_rt_print_bytes(ctx, "answer=", 7u) &&
        cint_rt_print_value(ctx, CINT_TAG_I64, (uint64_t)answer) && cint_rt_print_bytes(ctx, "\n", 1u)) {
        (void)cint_rt_print_end(ctx);
    }
    return cint_rt_entry_end(ctx);
}

/* I64 big = MAX; "partial: big={big}"; I64 c = big + 1; "never\n"; */
static cint_status p_print_then_fault(cint_ctx *ctx, int64_t fuel)
{
    int64_t big = INT64_MAX, c = 0;
    cint_status st = cint_rt_entry_begin(ctx, p_site(1u), fuel, CINT_DEPTH_FROM_CONFIG);
    if (st != CINT_OK) {
        return st;
    }
    if (cint_rt_print_begin(ctx, p_site(1u)) && cint_rt_print_bytes(ctx, "partial: big=", 13u) &&
        cint_rt_print_value(ctx, CINT_TAG_I64, (uint64_t)big) && cint_rt_print_end(ctx) &&
        cint_add_i64(ctx, p_site(2u), big, 1, &c)) {
        (void)p_text(ctx, "never\n");
    }
    return cint_rt_entry_end(ctx);
}

/* I64 big = MAX; "start "; "sum={big + 1}\n"; */
static cint_status p_hole_fault(cint_ctx *ctx, int64_t fuel)
{
    int64_t big = INT64_MAX, c = 0;
    cint_status st = cint_rt_entry_begin(ctx, p_site(1u), fuel, CINT_DEPTH_FROM_CONFIG);
    if (st != CINT_OK) {
        return st;
    }
    if (p_text(ctx, "start ") && cint_rt_print_begin(ctx, p_site(1u)) && cint_rt_print_bytes(ctx, "sum=", 4u) &&
        cint_add_i64(ctx, p_site(3u), big, 1, &c) && cint_rt_print_value(ctx, CINT_TAG_I64, (uint64_t)c) &&
        cint_rt_print_bytes(ctx, "\n", 1u)) {
        (void)cint_rt_print_end(ctx);
    }
    return cint_rt_entry_end(ctx);
}

/* A main that reports a status no generated main returns. */
static cint_status p_refused(cint_ctx *ctx, int64_t fuel)
{
    (void)ctx;
    (void)fuel;
    return CINT_REFUSED;
}

/* "partial\n"; return IoError.full; (conformance errors/main_returns_error) */
static cint_status p_error(cint_ctx *ctx, int64_t fuel)
{
    cint_status st = cint_rt_entry_begin(ctx, p_site(1u), fuel, CINT_DEPTH_FROM_CONFIG);
    if (st != CINT_OK) {
        return st;
    }
    if (p_text(ctx, "partial\n")) {
        cint_rt_error_result(ctx, &e_set, 2u);
    }
    return cint_rt_entry_end(ctx);
}

/* An error set whose underlying type is not unsigned: the record does not
 * encode, which is an internal error. */
static const cint_error_set e_bad = {"rt.program.Bad", CINT_TAG_I64, 2u, e_values};
static cint_status p_error_bad(cint_ctx *ctx, int64_t fuel)
{
    cint_status st = cint_rt_entry_begin(ctx, p_site(1u), fuel, CINT_DEPTH_FROM_CONFIG);
    if (st != CINT_OK) {
        return st;
    }
    cint_rt_error_result(ctx, &e_bad, 1u);
    return cint_rt_entry_end(ctx);
}

/* Module state of rt/program.ci: I64 count = 5; Bool flag = true; */
static const struct {
    int64_t count;
    uint8_t flag;
    uint8_t pad[7];
} p_init = {5, 1u, {0u}};
static const cint_state_var p_vars[2] = {{"count", 5u, CINT_TAG_I64, 0u, 0u}, {"flag", 4u, CINT_TAG_BOOL, 8u, 0u}};
static const cint_state p_state = {0u, 2u, 16u, 0u, &p_init, p_vars};
static const cint_state *const p_state_list[1] = {&p_state};
static const cint_state_table p_states = {1u, 0u, p_state_list};
static const cint_state p_state_bad = {7u, 2u, 16u, 0u, &p_init, p_vars};  /* no module 7 */
static const cint_state *const p_state_bad_list[1] = {&p_state_bad};
static const cint_state_table p_states_bad = {1u, 0u, p_state_bad_list};

/* count = count + 1; then return IoError.closed, or, with `fault`, big + 1. */
static cint_status p_state_body(cint_ctx *ctx, int64_t fuel, bool fault)
{
    int64_t *count, c = 0;
    cint_status st = cint_rt_entry_begin(ctx, p_site(1u), fuel, CINT_DEPTH_FROM_CONFIG);
    if (st != CINT_OK) {
        return st;
    }
    count = (int64_t *)cint_rt_state(ctx, &p_state);
    if (count != NULL && cint_add_i64(ctx, p_site(2u), *count, 1, count)) {
        if (fault) {
            (void)cint_add_i64(ctx, p_site(2u), INT64_MAX, 1, &c);
        } else {
            cint_rt_error_result(ctx, &e_set, 1u);
        }
    }
    return cint_rt_entry_end(ctx);
}

static cint_status p_state_error(cint_ctx *ctx, int64_t fuel)
{
    return p_state_body(ctx, fuel, false);
}

static cint_status p_state_fault(cint_ctx *ctx, int64_t fuel)
{
    return p_state_body(ctx, fuel, true);
}

/* A fault at a site the program's table does not hold: the record cannot be
 * encoded, which is an internal error. */
static cint_status p_bad_site(cint_ctx *ctx, int64_t fuel)
{
    int64_t c = 0;
    cint_status st = cint_rt_entry_begin(ctx, p_site(1u), fuel, CINT_DEPTH_FROM_CONFIG);
    if (st != CINT_OK) {
        return st;
    }
    (void)cint_add_i64(ctx, p_site(9u), INT64_MAX, 1, &c);
    return cint_rt_entry_end(ctx);
}

static const struct {
    const char *name;
    cint_main_fn fn;
    int64_t fuel;
    const cint_state_table *state;  /* for cint_program_run_state */
} p_scenarios[] = {
    {"hello", p_hello, CINT_FUEL_UNBOUNDED, &p_states},
    {"print_then_fault", p_print_then_fault, CINT_FUEL_UNBOUNDED, NULL},
    {"hole_fault", p_hole_fault, CINT_FUEL_UNBOUNDED, NULL},
    {"fuel_zero", p_hello, 0, &p_states},
    {"refused", p_refused, CINT_FUEL_UNBOUNDED, &p_states},
    {"bad_site", p_bad_site, CINT_FUEL_UNBOUNDED, NULL},
    {"error", p_error, CINT_FUEL_UNBOUNDED, NULL},
    {"error_bad_set", p_error_bad, CINT_FUEL_UNBOUNDED, NULL},
    {"state_error", p_state_error, CINT_FUEL_UNBOUNDED, &p_states},
    {"state_fault", p_state_fault, CINT_FUEL_UNBOUNDED, &p_states},
    {"state_bad_table", p_state_error, CINT_FUEL_UNBOUNDED, &p_states_bad},
};

/* dests: the fault destination, then (cint_program_run_state) the fuel and
 * state destinations, or NULL; "-" is no destination. */
static int run_scenario(const char *name, const char *const *dests, bool record)
{
    size_t i;
    for (i = 0; i < sizeof p_scenarios / sizeof p_scenarios[0]; i++) {
        if (strcmp(name, p_scenarios[i].name) == 0) {
            break;
        }
    }
    if (i == sizeof p_scenarios / sizeof p_scenarios[0]) {
        fprintf(stderr, "unknown scenario: %s\n", name);
        return 2;
    }
    if (!record) {
        const char *d[3];
        for (size_t k = 0; k < 3u; k++) {
            d[k] = dests[k] == NULL || strcmp(dests[k], "-") == 0 ? NULL : dests[k];
        }
        if (dests[1] == NULL) {
            return cint_program_run(&p_program, p_scenarios[i].fn, p_scenarios[i].fuel, d[0]);
        }
        return cint_program_run_state(&p_program, p_scenarios[i].fn, p_scenarios[i].fuel, d[0], d[1], d[2],
                                      p_scenarios[i].state);
    }
    {
        cint_ctx_config cfg;
        cint_ctx *ctx = NULL;
        uint8_t bytes[1024];
        size_t len, k;
        memset(&cfg, 0, sizeof cfg);
        cfg.size = (uint32_t)sizeof cfg;
        cfg.program = &p_program;
        if (cint_ctx_create(&cfg, &ctx) != CINT_OK || p_scenarios[i].fn(ctx, p_scenarios[i].fuel) != CINT_FAULT ||
            cint_ctx_fault(ctx, &g_rec) != CINT_OK) {
            cint_ctx_destroy(ctx);
            return 3;
        }
        cint_ctx_destroy(ctx);
        len = cint_fault_encode(&g_rec, bytes, sizeof bytes);
        if (len == 0u || len > sizeof bytes) {
            return 3;
        }
        for (k = 0; k < len; k++) {
            printf("%02x", bytes[k]);
        }
        printf("\n");
        return 0;
    }
}

/* ------------------------------------------------------------------------- */
/* Builtin and portable bodies agree (SPEC-09 EMIT-05).                       */

static uint64_t g_state = 0x9E3779B97F4A7C15u;

static uint64_t next_u64(void)
{
    /* xorshift64* (Vigna, 2016); deterministic for a fixed initial state. */
    g_state ^= g_state >> 12;
    g_state ^= g_state << 25;
    g_state ^= g_state >> 27;
    return g_state * 2685821657736338717u;
}

static uint64_t pick(void)
{
    static const uint64_t special[] = {
        0u, 1u, 2u, 0x7fu, 0x80u, 0xffu, 0x7fffu, 0x8000u, 0xffffu, 0x7fffffffu, 0x80000000u,
        0xffffffffu, 3037000499u, 3037000500u, 0x7fffffffffffffffu, 0x8000000000000000u,
        0xffffffffffffffffu, 0xfffffffffffffffeu, 0x8000000000000001u, 0x100000000u,
    };
    uint64_t x = next_u64();
    switch (x & 7u) {
    case 0: return special[(x >> 8) % (sizeof special / sizeof special[0])];
    case 1: return x >> (x >> 58);          /* small magnitudes */
    case 2: return (uint64_t)0 - (x >> (x >> 58));
    default: return next_u64();
    }
}

#if CINT_RT_HAVE_BUILTINS
#define AGREE(OP, T, CT, SIGNED)                                                       \
    do {                                                                               \
        CT ra = 0, rb = 0;                                                             \
        CT a = SIGNED ? (CT)cint_rt_sext(x, (unsigned)sizeof(CT) * 8u) : (CT)x;        \
        CT b = SIGNED ? (CT)cint_rt_sext(y, (unsigned)sizeof(CT) * 8u) : (CT)y;        \
        bool fa = cint_rt_##OP##_ovf_##T##_p(a, b, &ra);                       \
        bool fb = cint_rt_##OP##_ovf_##T##_b(a, b, &rb);                       \
        if (fa != fb || ra != rb) {                                            \
            bad++;                                                             \
        }                                                                      \
    } while (0)

static void test_agreement(long pairs)
{
    long i, bad = 0;
    for (i = 0; i < pairs; i++) {
        uint64_t x = pick();
        uint64_t y = pick();
        AGREE(add, i64, int64_t, 1); AGREE(sub, i64, int64_t, 1); AGREE(mul, i64, int64_t, 1);
        AGREE(add, u64, uint64_t, 0); AGREE(sub, u64, uint64_t, 0); AGREE(mul, u64, uint64_t, 0);
        AGREE(add, i32, int32_t, 1); AGREE(sub, i32, int32_t, 1); AGREE(mul, i32, int32_t, 1);
        AGREE(add, u32, uint32_t, 0); AGREE(sub, u32, uint32_t, 0); AGREE(mul, u32, uint32_t, 0);
        AGREE(add, i16, int16_t, 1); AGREE(sub, i16, int16_t, 1); AGREE(mul, i16, int16_t, 1);
        AGREE(add, u16, uint16_t, 0); AGREE(sub, u16, uint16_t, 0); AGREE(mul, u16, uint16_t, 0);
        AGREE(add, i8, int8_t, 1); AGREE(sub, i8, int8_t, 1); AGREE(mul, i8, int8_t, 1);
        AGREE(add, u8, uint8_t, 0); AGREE(sub, u8, uint8_t, 0); AGREE(mul, u8, uint8_t, 0);
    }
    CHECK(bad == 0, "builtin and portable bodies agree on every pair");
    printf("agreement: %ld pairs x 24 primitives, %ld disagreements\n", pairs, bad);
}
#else
static void test_agreement(long pairs)
{
    (void)pairs;
    printf("agreement: builtin bodies not available on this compiler; skipped\n");
}
#endif

/* ------------------------------------------------------------------------- */
/* Box 09 (cint_rt.h section 14): views of rank 1 to 4, slices, copies,       */
/* reductions, kernel dispatch and scratch storage. The expected records are  */
/* those of ref/cint_ref (views.py, reduce.py, exec.py) and of the frozen     */
/* conformance/view, reduce and kernel files named beside the checks.         */

static void put_part(char *line, size_t cap, size_t *at, const char *key, const char *text)
{
    int n = snprintf(line + *at, cap - *at, " | %s%s", key, text);
    if (n > 0 && (size_t)n < cap - *at) {
        *at += (size_t)n;
    }
}

/* The context's record as one line: code and operation, then each operand,
 * "exact <decimal>" and "limit <value>" when present, separated by " | ". */
static const char *rec_line(cint_ctx *ctx)
{
    static char line[2048];
    char v[720];
    size_t at;
    unsigned i;
    int n;
    get_fault(ctx, &g_rec);
    n = snprintf(line, sizeof line, "%u %.*s", (unsigned)g_rec.code, (int)g_rec.operation_len, g_rec.operation);
    at = n > 0 ? (size_t)n : 0u;
    for (i = 0; i < g_rec.operand_count; i++) {
        put_part(line, sizeof line, &at, "", render(&g_rec.operands[i], v, sizeof v));
    }
    if (g_rec.has_exact) {
        put_part(line, sizeof line, &at, "exact ", render_dec(&g_rec.exact, v, sizeof v));
    }
    if (g_rec.has_limit) {
        put_part(line, sizeof line, &at, "limit ", render(&g_rec.limit, v, sizeof v));
    }
    return line;
}

/* Checks that the record of ctx reads `want`, then clears it (outside an entry). */
#define CHECK_REC(ctx, want, what)                                                  \
    do {                                                                            \
        const char *got_ = rec_line(ctx);                                           \
        CHECK(strcmp(got_, (want)) == 0, (what));                                   \
        if (strcmp(got_, (want)) != 0 && g_fail <= 40) {                            \
            fprintf(stderr, "  record: %s\n  wanted: %s\n", got_, (want));          \
        }                                                                           \
        (void)cint_ctx_clear_fault(ctx);                                            \
    } while (0)

static cint_vdesc vd(void *base, int64_t origin, int64_t rank, const int64_t *shape, const int64_t *stride)
{
    cint_vdesc v;
    int64_t k;
    memset(&v, 0, sizeof v);
    v.base = base;
    v.origin = origin;
    v.rank = rank;
    for (k = 0; k < rank && k < 4; k++) {
        v.shape[k] = shape[k];
        v.stride[k] = stride[k];
    }
    return v;
}

static cint_vdesc vd1(void *base, int64_t origin, int64_t n, int64_t stride)
{
    return vd(base, origin, 1, &n, &stride);
}

static cint_vdesc vd2(void *base, int64_t origin, int64_t n0, int64_t n1, int64_t s0, int64_t s1)
{
    int64_t shape[2], stride[2];
    shape[0] = n0;
    shape[1] = n1;
    stride[0] = s0;
    stride[1] = s1;
    return vd(base, origin, 2, shape, stride);
}

static void test_index_dim(void)
{
    cint_ctx *ctx = new_ctx();
    if (ctx == NULL) {
        CHECK(0, "index_dim context");
        return;
    }
    CHECK(cint_index_check_dim(ctx, S_OP, "index.checked.i64", 1, 2, 3), "an index inside its dimension");
    CHECK(!cint_index_check_dim(ctx, S_OP, "index.checked.i64", 1, 3, 3), "index 3 of extent 3 faults");
    CHECK(!cint_index_check_dim(ctx, S_OP, "index.checked.i64", 0, 0, 3), "a faulted context fails an index inside");
    CHECK_REC(ctx, "3 index.checked.i64 | I64 1 | I64 3 | limit I64 3",
              "R1: dimension, then index; limit the extent (view/rank2_index_dim1_bounds)");
    CHECK(!cint_index_check_dim(ctx, S_OP, "index.checked.u8", 3, INT64_MIN, 0), "a negative index");
    CHECK_REC(ctx, "3 index.checked.u8 | I64 3 | I64 -9223372036854775808 | limit I64 0", "operands as given");
    CHECK(!cint_rt_fault_index_dim(ctx, S_OP, NULL, 0, 5, 3), "no identifier");
    CHECK_REC(ctx, "10 dispatch.admit", "an absent identifier is refused (RT-OQ-20)");
    cint_ctx_destroy(ctx);
}

/* views.py slice_dim for small values, where no C operation can overflow. */
static bool ref_slice(int64_t n, int64_t lo, int64_t hi, uint32_t flags, int64_t step, int64_t *first,
                      int64_t *length, int64_t *last)
{
    bool inc = (flags & CINT_SLICE_INCLUSIVE) != 0u, valid;
    bool has_lo = (flags & CINT_SLICE_LO) != 0u, has_hi = (flags & CINT_SLICE_HI) != 0u;
    if (step > 0) {
        int64_t end;
        *first = has_lo ? lo : 0;
        *last = has_hi ? hi : n;
        end = inc ? *last + 1 : *last;
        valid = 0 <= *first && *first <= end && end <= n;
        *length = valid ? (end - *first + step - 1) / step : 0;
        return valid;
    }
    *first = has_lo ? lo : n - 1;
    if (inc) {
        *last = has_hi ? hi : 0;
        valid = 0 <= *last && *last <= *first + 1 && *first + 1 <= n;
        *length = valid && *first >= *last ? (*first - *last + 1 - step - 1) / -step : 0;
    } else {
        *last = has_hi ? hi : -1;
        valid = -1 <= *last && *last <= *first && *first <= n - 1;
        *length = valid ? (*first - *last - step - 1) / -step : 0;
    }
    return valid;
}

static void test_slices(void)
{
    static const int64_t steps[6] = {-3, -2, -1, 1, 2, 3};
    static const int64_t strides[3] = {1, 3, -2};
    static const struct {
        int64_t lo, hi, step;
        uint32_t flags;
        const char *bounds;
    } bad[] = {
        /* the examples of the box 09 semantics report, extent 10 */
        {2, 11, 1, CINT_SLICE_LO | CINT_SLICE_HI, "I64 2 | I64 11"},
        {11, 0, 1, CINT_SLICE_LO, "I64 11 | I64 10"},
        {0, 11, 1, CINT_SLICE_HI, "I64 0 | I64 11"},
        {10, 2, -1, CINT_SLICE_LO | CINT_SLICE_HI | CINT_SLICE_INCLUSIVE, "I64 10 | I64 2"},
        {10, 0, -1, CINT_SLICE_LO, "I64 10 | I64 -1"},
        {0, -2, -1, CINT_SLICE_HI, "I64 9 | I64 -2"},
        {5, 3, 1, CINT_SLICE_LO | CINT_SLICE_HI, "I64 5 | I64 3"},    /* view/slice_lo_above_hi */
        {-1, 3, 1, CINT_SLICE_LO | CINT_SLICE_HI, "I64 -1 | I64 3"},  /* view/slice_negative_lo */
        {2, 11, 1, CINT_SLICE_LO | CINT_SLICE_HI, "I64 2 | I64 11"},  /* view/slice_hi_above_extent */
        /* 0..=I64.max is E_BOUNDS, not an overflow (ruling R2) */
        {0, INT64_MAX, 1, CINT_SLICE_LO | CINT_SLICE_HI | CINT_SLICE_INCLUSIVE, "I64 0 | I64 9223372036854775807"},
        {INT64_MIN, 0, 1, CINT_SLICE_LO, "I64 -9223372036854775808 | I64 10"},
        {INT64_MAX, INT64_MIN, -1, CINT_SLICE_LO | CINT_SLICE_HI | CINT_SLICE_INCLUSIVE,
         "I64 9223372036854775807 | I64 -9223372036854775808"},
        {INT64_MAX, INT64_MIN, INT64_MIN, CINT_SLICE_LO | CINT_SLICE_HI,
         "I64 9223372036854775807 | I64 -9223372036854775808"},
    };
    cint_ctx *ctx = new_ctx();
    int64_t n, lo, hi, origin, length, stride;
    long mismatches = 0, faults = 0;
    unsigned flags, si, ti;
    size_t i;
    char want[200];
    if (ctx == NULL) {
        CHECK(0, "slice context");
        return;
    }
    /* Every item over extents 0 to 5 against views.py slice_dim. */
    for (n = 0; n <= 5; n++) {
        for (lo = -2; lo <= 7; lo++) {
            for (hi = -2; hi <= 7; hi++) {
                for (flags = 0u; flags < 8u; flags++) {
                    for (si = 0u; si < 6u; si++) {
                        for (ti = 0u; ti < 3u; ti++) {
                            int64_t first, len, last, dim = (int64_t)ti - 1;
                            bool valid = ref_slice(n, lo, hi, flags, steps[si], &first, &len, &last);
                            origin = 100;
                            length = -7;
                            stride = -7;
                            if (cint_rt_slice(ctx, S_OP, "slice.checked.i64", dim, n, strides[ti], lo, hi, steps[si],
                                              flags, &origin, &length, &stride) != valid) {
                                mismatches++;
                            } else if (valid) {
                                mismatches += length != len || origin != 100 + first * strides[ti] ||
                                              stride != strides[ti] * steps[si];
                            } else {
                                if (dim >= 0) {
                                    snprintf(want, sizeof want, "3 slice.checked.i64 | I64 %" PRId64 " | I64 %" PRId64
                                             " | I64 %" PRId64 " | limit I64 %" PRId64, dim, first, last, n);
                                } else {
                                    snprintf(want, sizeof want, "3 slice.checked.i64 | I64 %" PRId64 " | I64 %" PRId64
                                             " | limit I64 %" PRId64, first, last, n);
                                }
                                mismatches += strcmp(rec_line(ctx), want) != 0 || origin != 100 || length != -7 ||
                                              stride != -7;
                                faults++;
                                (void)cint_ctx_clear_fault(ctx);
                            }
                        }
                    }
                }
            }
        }
    }
    CHECK(mismatches == 0 && faults > 10000, "every small slice item agrees with views.py slice_dim");
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        CHECK(!cint_rt_slice(ctx, S_OP, "slice.checked.i64", -1, 10, 1, bad[i].lo, bad[i].hi, bad[i].step,
                             bad[i].flags, &origin, &length, &stride), "an invalid slice item");
        snprintf(want, sizeof want, "3 slice.checked.i64 | %s | limit I64 10", bad[i].bounds);
        CHECK_REC(ctx, want, "the R2 record: the bounds after the omitted ones are filled in");
    }
    /* Steps, strides and extents at the ends of I64. */
    origin = 0;
    CHECK(cint_rt_slice(ctx, S_OP, NULL, -1, 10, 1, 0, 0, INT64_MIN, 0u, &origin, &length, &stride) && origin == 9 &&
          length == 1 && stride == INT64_MIN, "step I64.min: one element, the stride exact");
    origin = 0;
    CHECK(cint_rt_slice(ctx, S_OP, NULL, -1, 10, -1, 0, 0, INT64_MIN, 0u, &origin, &length, &stride) &&
          origin == -9 && length == 1 && stride == INT64_MAX, "a stride above I64 saturates");
    origin = 0;
    CHECK(cint_rt_slice(ctx, S_OP, NULL, -1, 10, 2, 0, 0, INT64_MIN, 0u, &origin, &length, &stride) &&
          origin == 18 && length == 1 && stride == INT64_MIN, "a stride below I64 saturates");
    origin = 0;
    CHECK(cint_rt_slice(ctx, S_OP, NULL, -1, 10, 1, 3, 0, INT64_MAX, CINT_SLICE_LO, &origin, &length, &stride) &&
          origin == 3 && length == 1 && stride == INT64_MAX, "step I64.max");
    origin = 5;
    CHECK(cint_rt_slice(ctx, S_OP, NULL, -1, INT64_MAX, 1, 0, 0, 1, 0u, &origin, &length, &stride) && origin == 5 &&
          length == INT64_MAX && stride == 1, "all of I64.max elements");
    origin = 0;
    CHECK(cint_rt_slice(ctx, S_OP, NULL, -1, INT64_MAX, 1, 0, INT64_MAX - 1, 1,
                        CINT_SLICE_HI | CINT_SLICE_INCLUSIVE, &origin, &length, &stride) && length == INT64_MAX,
          "..=I64.max - 1 over I64.max elements");
    origin = 0;
    CHECK(cint_rt_slice(ctx, S_OP, NULL, -1, INT64_MAX, 1, 0, 0, -1, 0u, &origin, &length, &stride) &&
          origin == INT64_MAX - 1 && length == INT64_MAX && stride == -1, "reversed, I64.max elements");
    origin = INT64_MAX;
    CHECK(cint_rt_slice(ctx, S_OP, NULL, -1, 10, 1, 10, 0, 1, CINT_SLICE_LO, &origin, &length, &stride) &&
          origin == INT64_MAX && length == 0, "an empty item whose origin would overflow keeps the origin");
    origin = 0;
    CHECK(cint_rt_slice(ctx, S_OP, NULL, -1, 0, 1, 0, 0, -1, CINT_SLICE_HI | CINT_SLICE_INCLUSIVE, &origin, &length,
                        &stride) && origin == -1 && length == 0, "..=0 by -1 over no elements is empty");
    /* A faulted context writes nothing; arguments no emitted code passes are refused. */
    CHECK(!cint_rt_slice(ctx, S_OP, "slice.checked.i64", 2, 10, 1, 5, 3, 1, CINT_SLICE_LO | CINT_SLICE_HI, &origin,
                         &length, &stride), "5..3");
    origin = 1;
    length = 2;
    stride = 3;
    CHECK(!cint_rt_slice(ctx, S_OP, "slice.checked.i64", 0, 10, 1, 0, 3, 1, CINT_SLICE_LO | CINT_SLICE_HI, &origin,
                         &length, &stride) && origin == 1 && length == 2 && stride == 3,
          "a faulted context fails a valid item and writes nothing");
    CHECK_REC(ctx, "3 slice.checked.i64 | I64 2 | I64 5 | I64 3 | limit I64 10", "the first record is kept");
    CHECK(!cint_rt_slice(ctx, S_OP, "slice.checked.i64", 0, 10, 1, 0, 3, 0, 0u, &origin, &length, &stride), "step 0");
    CHECK_REC(ctx, "10 dispatch.admit", "step 0 is refused");
    CHECK(!cint_rt_slice(ctx, S_OP, "slice.checked.i64", 0, -1, 1, 0, 3, 1, 0u, &origin, &length, &stride), "extent");
    CHECK_REC(ctx, "10 dispatch.admit", "a negative extent is refused");
    CHECK(!cint_rt_slice(ctx, S_OP, "slice.checked.i64", 0, 10, 1, 0, 3, 1, 8u, &origin, &length, &stride), "flags");
    CHECK_REC(ctx, "10 dispatch.admit", "an unknown flag is refused");
    CHECK(!cint_rt_slice(ctx, S_OP, "slice.checked.i64", 0, 10, 1, 0, 3, 1, 0u, &origin, NULL, &stride), "output");
    CHECK_REC(ctx, "10 dispatch.admit", "an absent output is refused");
    CHECK(!cint_rt_slice(ctx, S_OP, "", 0, 10, 1, 0, 11, 1, CINT_SLICE_HI, &origin, &length, &stride), "identifier");
    CHECK_REC(ctx, "10 dispatch.admit", "an invalid item without an identifier is refused");
    cint_ctx_destroy(ctx);
}

static void test_relation(void)
{
    static int64_t buf[256], other[16];
    cint_vdesc p, q;
    p = vd1(buf, 0, 0, 1);
    q = vd1(buf, 0, 4, 1);
    CHECK(cint_rt_view_relation(&p, &q) == 0 && cint_rt_view_relation(&q, &p) == 0 &&
          cint_rt_view_relation(&p, &p) == 0, "T0: an empty view is disjoint from every view, itself included");
    p = vd2(buf, 0, 3, 0, 4, 1);
    CHECK(cint_rt_view_relation(&p, &q) == 0, "T0 in the second dimension");
    p = vd1(other, 0, 4, 1);
    CHECK(cint_rt_view_relation(&p, &q) == 0, "views of different buffers are disjoint");
    p = vd1(buf, 4, 4, 1);
    CHECK(cint_rt_view_relation(&p, &q) == 0 && cint_rt_view_relation(&q, &p) == 0, "T1: adjacent intervals");
    p = vd1(buf, 7, 4, -1);
    CHECK(cint_rt_view_relation(&p, &q) == 0, "T1: a reversed view's interval");
    p = vd1(buf, 3, 4, 1);
    CHECK(cint_rt_view_relation(&p, &q) == 2, "intervals that meet are uncertain");
    p = vd1(buf, 0, 8, 2);
    q = vd1(buf, 1, 8, 2);
    CHECK(cint_rt_view_relation(&p, &q) == 0, "T2: evens and odds");
    p = vd1(buf, 14, 8, -2);
    CHECK(cint_rt_view_relation(&p, &q) == 0, "T2: reversed evens and odds");
    p = vd1(buf, -3, 4, 2);
    q = vd1(buf, 0, 4, 2);
    CHECK(cint_rt_view_relation(&p, &q) == 0, "T2: the residue of a negative origin is nonnegative");
    p = vd1(buf, 0, 5, 4);
    q = vd1(buf, 3, 5, 6);
    CHECK(cint_rt_view_relation(&p, &q) == 0, "T2: gcd(4, 6) = 2 separates origins 0 and 3");
    p = vd2(buf, 8, 8, 8, 16, 1);
    q = vd2(buf, 0, 8, 8, 16, 1);
    CHECK(cint_rt_view_relation(&p, &q) == 2, "adjacent column blocks are uncertain (kernel/alias_uncertain_columns)");
    CHECK(cint_rt_view_relation(&p, &p) == 1, "T3: the same view is identical");
    q = vd2(buf, 8, 8, 8, 16, 1);
    CHECK(cint_rt_view_relation(&p, &q) == 1, "T3: equal descriptors are identical");
    p = vd1(buf, 5, 1, 1);
    q = vd1(buf, 5, 1, 7);
    CHECK(cint_rt_view_relation(&p, &q) == 2, "T4: one element each at one origin, strides apart");
    q = vd2(buf, 5, 1, 1, 1, 1);
    CHECK(cint_rt_view_relation(&p, &q) == 2, "T4: ranks apart");
    p = vd2(buf, 0, 2, 3, 3, 1);
    q = vd2(buf, 0, 3, 2, 1, 3);
    CHECK(cint_rt_view_relation(&p, &q) == 2, "a view and its transpose");
    p = vd2(buf, 0, 1, 4, INT64_MAX, 1);
    q = vd2(buf, 4, 1, 4, INT64_MIN, 1);
    CHECK(cint_rt_view_relation(&p, &q) == 0, "T1 ignores the saturated stride of a dimension of extent 1");
    p = vd2(buf, 0, 1, 4, INT64_MAX, 2);
    q = vd2(buf, 1, 1, 4, 16, 2);
    CHECK(cint_rt_view_relation(&p, &q) == 0, "T2 takes the strides of dimensions longer than 1 only");
    p = vd1(buf, 0, 3, INT64_MAX);
    q = vd1(buf, 0, 1, 1);
    CHECK(cint_rt_view_relation(&p, &q) == 2, "an interval beyond I64 is uncertain");
    CHECK(cint_rt_view_relation(NULL, &q) == 2, "no view is uncertain");
}

static void test_copies(void)
{
    static int32_t m[6], d[6], line[12];
    cint_ctx *ctx = new_ctx();
    cint_vdesc dst, src, far;
    int i;
    if (ctx == NULL) {
        CHECK(0, "copy context");
        return;
    }
    for (i = 0; i < 6; i++) {
        m[i] = i;
        d[i] = -1;
    }
    src = vd2(m, 0, 3, 2, 1, 3);  /* the transpose of a 2 x 3 matrix */
    dst = vd2(d, 0, 3, 2, 2, 1);
    CHECK(cint_rt_copy_view(ctx, S_OP, &dst, &src, sizeof m[0]), "a copy from a transposed view");
    CHECK(d[0] == 0 && d[1] == 3 && d[2] == 1 && d[3] == 4 && d[4] == 2 && d[5] == 5, "elements in logical order");
    dst = vd1(d, 5, 6, -1);
    src = vd1(m, 0, 6, 1);
    CHECK(cint_rt_copy_view(ctx, S_OP, &dst, &src, sizeof m[0]) && d[5] == 0 && d[2] == 3 && d[0] == 5,
          "a copy into a reversed view");
    CHECK(cint_rt_copy_view(ctx, S_OP, &src, &src, sizeof m[0]), "copy(a, a) is no copy (view/copy_same_view_noop)");
    for (i = 0; i < 12; i++) {
        line[i] = i;
    }
    dst = vd1(line, 0, 6, 2);
    src = vd1(line, 1, 6, 2);
    CHECK(cint_rt_copy_view(ctx, S_OP, &dst, &src, sizeof line[0]) && line[0] == 1 && line[2] == 3 &&
          line[10] == 11 && line[1] == 1, "evens from odds of one buffer, disjoint by T2");
    for (i = 0; i < 12; i++) {
        line[i] = i;
    }
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 10, 8) == CINT_OK, "entry");
    CHECK(cint_rt_call_enter(ctx, S_CALL) && cint_fuel_charge(ctx, S_FUEL, 3u), "a call and three units");
    dst = vd1(line, 0, 4, 1);
    src = vd1(line, 2, 4, 1);
    CHECK(!cint_rt_copy_view(ctx, S_OP, &dst, &src, sizeof line[0]), "an overlapping copy faults");
    cint_rt_call_leave(ctx);
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT && fuel_of(ctx) == 5, "fuel kept (view/copy_slices_runtime_alias)");
    get_fault(ctx, &g_rec);
    CHECK(g_rec.stack_count == 1u && g_rec.stack[0].index == S_CALL.index && g_rec.position.index == S_OP.index,
          "at the copy, with the caller's stack");
    CHECK_REC(ctx, "7 copy.alias", "copy.alias: no operands, exact or limit (ruling R8)");
    CHECK(line[0] == 0 && line[3] == 3, "nothing is written");
    dst = vd2(d, 0, 2, 3, 3, 1);
    src = vd2(m, 0, 2, 2, 2, 1);
    CHECK(!cint_rt_copy_view(ctx, S_OP, &dst, &src, sizeof m[0]), "unequal extents");
    CHECK_REC(ctx, "4 copy.shape | I64 1 | I64 2 | limit I64 3", "copy.shape: dimension, source extent, limit");
    dst = vd1(line, 0, 4, 1);
    src = vd1(line, 1, 3, 1);
    CHECK(!cint_rt_copy_view(ctx, S_OP, &dst, &src, sizeof line[0]), "unequal extents on one buffer");
    CHECK_REC(ctx, "4 copy.shape | I64 0 | I64 3 | limit I64 4", "the shape check comes before the alias check");
    dst = vd1(line, 0, 0, 1);
    src = vd1(line, 3, 0, 1);
    far = vd2(NULL, INT64_MIN, 0, 5, 1, 1);
    CHECK(cint_rt_copy_view(ctx, S_OP, &dst, &src, sizeof line[0]) && line[0] == 0, "an empty copy");
    CHECK(cint_rt_copy_view(ctx, S_OP, &far, &far, sizeof line[0]), "no pointer is formed for an empty view");
    CHECK(cint_rt_copy_view(ctx, S_OP, &dst, &src, 0u), "elements of 0 bytes");
    src = vd2(m, 0, 4, 1, 1, 1);
    CHECK(!cint_rt_copy_view(ctx, S_OP, &dst, &src, sizeof m[0]), "ranks apart");
    CHECK_REC(ctx, "10 dispatch.admit", "ranks apart are refused");
    CHECK(!cint_rt_copy_view(ctx, S_OP, NULL, &src, sizeof m[0]), "no view");
    CHECK_REC(ctx, "10 dispatch.admit", "an absent view is refused");
    CHECK(!cint_rt_fault_decl_shape(ctx, S_OP, 0, -1), "decl.shape");
    CHECK(!cint_rt_copy_view(ctx, S_OP, &dst, &dst, sizeof m[0]), "a faulted context copies nothing");
    CHECK_REC(ctx, "4 decl.shape | I64 0 | I64 -1 | limit I64 0", "R10 (view/negative_extent_runtime)");

    /* The entry alias check of a call keeps the fuel (view/call_slices_runtime_alias). */
    dst = vd1(line, 0, 4, 1);
    src = vd1(line, 2, 4, 1);
    far = vd1(line, 4, 4, 1);
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 10, 8) == CINT_OK && cint_fuel_charge(ctx, S_FUEL, 1u), "entry");
    CHECK(cint_rt_call_alias(ctx, S_CALL, 0u, 1u, &dst, &far), "disjoint arguments");
    CHECK(!cint_rt_call_alias(ctx, S_CALL, 0u, 1u, &dst, &src), "overlapping arguments");
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT && fuel_of(ctx) == 2, "the fuel consumed is kept");
    CHECK_REC(ctx, "7 bind.alias | I64 0 | I64 1", "bind.alias: the inout parameter, the other");
    CHECK(!cint_rt_call_alias(ctx, S_CALL, 1u, 0u, &dst, &dst), "the same view twice");
    CHECK_REC(ctx, "7 bind.alias | I64 1 | I64 0", "T3 is an alias for a call");
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 10, 8) == CINT_OK && cint_fuel_charge(ctx, S_FUEL, 2u), "entry");
    CHECK(cint_shape_check_dim(ctx, S_CALL, 1u, 1u, 4, 4), "equal extents");
    CHECK(!cint_shape_check_dim(ctx, S_CALL, 1u, 1u, 4, 3), "unequal extents");
    CHECK(!cint_shape_check_dim(ctx, S_CALL, 1u, 1u, 4, 4), "a faulted context fails equal extents");
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT && fuel_of(ctx) == 3, "fuel kept");
    CHECK_REC(ctx, "4 bind.shape | I64 1 | I64 1 | I64 3 | limit I64 4", "bind.shape with its dimension");
    cint_ctx_destroy(ctx);
}

static void test_reductions(void)
{
    static int64_t a64[80];
    static int8_t a8[4];
    static int16_t m16[6];
    static uint64_t u64s[3];
    static bool truth[4];
    cint_ctx *ctx = new_ctx();
    cint_vdesc xs, ys;
    cint_zacc acc;
    uint64_t out = 0u, bits;
    int8_t r8;
    int16_t r16 = 0;
    int64_t r64 = 0;
    uint64_t ru64 = 0u;
    int i;
    if (ctx == NULL) {
        CHECK(0, "reduction context");
        return;
    }
    /* Exact accumulation beyond 2^128 (R6: dot). */
    memset(&acc, 0, sizeof acc);
    for (i = 0; i < 3; i++) {
        cint_zacc_add(&acc, CINT_TAG_I8, 0x1FFu);  /* -1: the bits above the I8 are ignored */
    }
    CHECK(cint_rt_zacc_fit(ctx, S_OP, "sum", CINT_TAG_I8, CINT_TAG_I8, 3, &acc, &out) && out == (uint64_t)(int64_t)-3,
          "three I8 -1 patterns sum to -3");
    memset(&acc, 0, sizeof acc);
    for (i = 0; i < 3; i++) {
        cint_zacc_add(&acc, CINT_TAG_U64, UINT64_MAX);
    }
    CHECK(!cint_rt_zacc_fit(ctx, S_OP, "sum", CINT_TAG_U64, CINT_TAG_U64, 3, &acc, &out), "a U64 sum above MAX");
    CHECK_REC(ctx, "1 sum.checked.u64.u64 | I64 3 | exact 55340232221128654845 | limit U64 18446744073709551615",
              "sum: operand n, exact, limit MAX");
    memset(&acc, 0, sizeof acc);
    for (i = 0; i < 8; i++) {
        cint_zacc_add_product(&acc, CINT_TAG_I64, (uint64_t)INT64_MIN, (uint64_t)INT64_MIN);
    }
    CHECK(!cint_rt_zacc_fit(ctx, S_OP, "dot", CINT_TAG_I64, CINT_TAG_I64, 8, &acc, &out), "2^129");
    CHECK_REC(ctx, "1 dot.checked.i64.i64 | I64 8 | exact 680564733841876926926749214863536422912"
              " | limit I64 9223372036854775807", "eight products of I64.min: 2^129");
    memset(&acc, 0, sizeof acc);
    for (i = 0; i < 5; i++) {
        cint_zacc_add_product(&acc, CINT_TAG_U64, UINT64_MAX, UINT64_MAX);
    }
    CHECK(!cint_rt_zacc_fit(ctx, S_OP, "dot", CINT_TAG_U64, CINT_TAG_U64, 5, &acc, &out), "five U64.max squares");
    CHECK_REC(ctx, "1 dot.checked.u64.u64 | I64 5 | exact 1701411834604692317132405596421745541125"
              " | limit U64 18446744073709551615", "beyond 2^128, unsigned");
    memset(&acc, 0, sizeof acc);
    for (i = 0; i < 3; i++) {
        cint_zacc_add_product(&acc, CINT_TAG_I64, (uint64_t)INT64_MIN, (uint64_t)INT64_MAX);
    }
    CHECK(!cint_rt_zacc_fit(ctx, S_OP, "dot", CINT_TAG_I64, CINT_TAG_I64, 3, &acc, &out), "negative products");
    CHECK_REC(ctx, "1 dot.checked.i64.i64 | I64 3 | exact -255211775190703847569860839463261831168"
              " | limit I64 -9223372036854775808", "below MIN: limit MIN");
    memset(&acc, 0, sizeof acc);
    cint_zacc_add(&acc, CINT_TAG_I64, (uint64_t)INT64_MIN);
    CHECK(cint_rt_zacc_fit(ctx, S_OP, "sum", CINT_TAG_I64, CINT_TAG_I64, 1, &acc, &out) && out == (uint64_t)INT64_MIN,
          "I64.min fits");
    cint_zacc_add(&acc, CINT_TAG_I64, UINT64_MAX);
    CHECK(!cint_rt_zacc_fit(ctx, S_OP, "sum", CINT_TAG_I64, CINT_TAG_I64, 2, &acc, &out), "I64.min - 1");
    CHECK_REC(ctx, "1 sum.checked.i64.i64 | I64 2 | exact -9223372036854775809 | limit I64 -9223372036854775808",
              "one below I64.min");
    memset(&acc, 0, sizeof acc);
    cint_zacc_add(&acc, CINT_TAG_U64, UINT64_MAX);
    CHECK(cint_rt_zacc_fit(ctx, S_OP, "sum", CINT_TAG_U64, CINT_TAG_U64, 1, &acc, &out) && out == UINT64_MAX,
          "U64.max fits a U64");
    CHECK(!cint_rt_zacc_fit(ctx, S_OP, "sum", CINT_TAG_U64, CINT_TAG_I64, 1, &acc, &out), "not an I64");
    CHECK_REC(ctx, "1 sum.checked.u64.i64 | I64 1 | exact 18446744073709551615 | limit I64 9223372036854775807",
              "both types in the identifier");
    memset(&acc, 0, sizeof acc);
    cint_zacc_add(&acc, CINT_TAG_I32, (uint64_t)(int64_t)-5);
    CHECK(!cint_rt_zacc_fit(ctx, S_OP, "sum", CINT_TAG_I32, CINT_TAG_U8, 1, &acc, &out), "negative into U8");
    CHECK_REC(ctx, "1 sum.checked.i32.u8 | I64 1 | exact -5 | limit U8 0", "below an unsigned MIN");
    CHECK(!cint_rt_zacc_fit(ctx, S_OP, "mean", CINT_TAG_I32, CINT_TAG_I32, 1, &acc, &out), "an unknown name");
    CHECK_REC(ctx, "10 dispatch.admit", "names other than sum and dot are refused");

    /* fold_checked steps (ruling R5). */
    bits = 120u;
    CHECK(!cint_rt_fold_step(ctx, S_OP, CINT_RT_OP_ADD, CINT_TAG_I8, 1, &bits, 120u) && bits == 120u, "120 + 120");
    CHECK_REC(ctx, "1 fold_checked.add.i8 | I64 1 | I8 120 | I8 120 | exact 240 | limit I8 127",
              "reduce/fold_checked_i8_overflow");
    bits = (uint64_t)INT64_MAX;
    CHECK(!cint_rt_fold_step(ctx, S_OP, CINT_RT_OP_MUL, CINT_TAG_I64, 0, &bits, 2u), "I64.max * 2");
    CHECK_REC(ctx, "1 fold_checked.mul.i64 | I64 0 | I64 9223372036854775807 | I64 2 | exact 18446744073709551614"
              " | limit I64 9223372036854775807", "fold_checked.mul");
    bits = 0x80u;
    CHECK(!cint_rt_fold_step(ctx, S_OP, CINT_RT_OP_MUL, CINT_TAG_I8, 4, &bits, 2u), "-128 * 2");
    CHECK_REC(ctx, "1 fold_checked.mul.i8 | I64 4 | I8 -128 | I8 2 | exact -256 | limit I8 -128", "limit MIN");
    bits = 2u;
    CHECK(cint_rt_fold_step(ctx, S_OP, CINT_RT_OP_MUL, CINT_TAG_I64, 0, &bits, 3u) && bits == 6u, "2 * 3");
    bits = (uint64_t)(int64_t)-5;
    CHECK(cint_rt_fold_step(ctx, S_OP, CINT_RT_OP_ADD, CINT_TAG_I16, 0, &bits, 3u) && bits == (uint64_t)(int64_t)-2,
          "-5 + 3, sign-extended");
    CHECK(!cint_rt_fold_step(ctx, S_OP, CINT_RT_OP_SUB, CINT_TAG_I16, 0, &bits, 3u), "sub");
    CHECK_REC(ctx, "10 dispatch.admit", "an operator other than add and mul is refused");
    CHECK(!cint_rt_fault_reduce_empty(ctx, S_OP, false, CINT_TAG_I64), "min");
    CHECK_REC(ctx, "4 reduce_min.checked.i64 | I64 0", "an empty min");
    CHECK(!cint_rt_fault_reduce_empty(ctx, S_OP, true, CINT_TAG_U8), "max");
    CHECK_REC(ctx, "4 reduce_max.checked.u8 | I64 0", "an empty max");

    /* cint_rt_reduce. */
    for (i = 0; i < 5; i++) {
        a64[i] = i + 1;
    }
    xs = vd1(a64, 0, 5, 1);
    CHECK(cint_rt_reduce(ctx, S_OP, CINT_REDUCE_SUM, CINT_TAG_I64, CINT_TAG_I64, &xs, NULL, false, 0u, &out) &&
          out == 15u, "sum");
    a64[0] = 5;
    a64[1] = -3;
    a64[2] = 9;
    xs = vd1(a64, 0, 3, 1);
    CHECK(cint_rt_reduce(ctx, S_OP, CINT_REDUCE_MIN, CINT_TAG_I64, CINT_TAG_I64, &xs, NULL, false, 0u, &out) &&
          out == (uint64_t)(int64_t)-3, "min");
    CHECK(cint_rt_reduce(ctx, S_OP, CINT_REDUCE_MAX, CINT_TAG_I64, CINT_TAG_I64, &xs, NULL, false, 0u, &out) &&
          out == 9u, "max");
    CHECK(cint_rt_reduce(ctx, S_OP, CINT_REDUCE_MIN, CINT_TAG_I64, CINT_TAG_I64, &xs, NULL, true,
                         (uint64_t)(int64_t)-10, &out) && out == (uint64_t)(int64_t)-10, "min with init");
    CHECK(cint_rt_reduce(ctx, S_OP, CINT_REDUCE_MAX, CINT_TAG_I64, CINT_TAG_I64, &xs, NULL, true, 0u, &out) &&
          out == 9u, "max with init");
    xs = vd1(a64, 0, 0, 1);
    CHECK(!cint_rt_reduce(ctx, S_OP, CINT_REDUCE_MIN, CINT_TAG_I64, CINT_TAG_I64, &xs, NULL, false, 0u, &out), "empty");
    CHECK_REC(ctx, "4 reduce_min.checked.i64 | I64 0", "min of nothing (reduce/min_empty_e_shape)");
    CHECK(cint_rt_reduce(ctx, S_OP, CINT_REDUCE_MAX, CINT_TAG_I64, CINT_TAG_I64, &xs, NULL, true, 7u, &out) &&
          out == 7u, "the init alone");
    u64s[0] = UINT64_MAX;
    u64s[1] = 0u;
    u64s[2] = 5u;
    xs = vd1(u64s, 0, 3, 1);
    CHECK(cint_rt_reduce(ctx, S_OP, CINT_REDUCE_MAX, CINT_TAG_U64, CINT_TAG_U64, &xs, NULL, false, 0u, &out) &&
          out == UINT64_MAX, "unsigned max");
    CHECK(cint_rt_reduce(ctx, S_OP, CINT_REDUCE_MIN, CINT_TAG_U64, CINT_TAG_U64, &xs, NULL, false, 0u, &out) &&
          out == 0u, "unsigned min");
    a8[0] = 127;
    a8[1] = 127;
    a8[2] = 127;
    xs = vd1(a8, 0, 3, 1);
    CHECK(!cint_rt_reduce(ctx, S_OP, CINT_REDUCE_SUM, CINT_TAG_I8, CINT_TAG_I8, &xs, NULL, false, 0u, &out), "381");
    CHECK_REC(ctx, "1 sum.checked.i8.i8 | I64 3 | exact 381 | limit I8 127", "reduce/sum_narrow_overflow");
    CHECK(cint_rt_reduce(ctx, S_OP, CINT_REDUCE_SUM, CINT_TAG_I8, CINT_TAG_I64, &xs, NULL, false, 0u, &out) &&
          out == 381u, "sum(I64, xs)");
    CHECK(cint_rt_reduce(ctx, S_OP, CINT_REDUCE_SUM_WRAP, CINT_TAG_I8, CINT_TAG_I8, &xs, NULL, false, 0u, &out) &&
          out == 125u, "sum_wrap: 381 modulo 256");
    CHECK(cint_reduce_i16(ctx, S_OP, CINT_REDUCE_SUM, CINT_TAG_I8, &xs, NULL, false, 0u, &r16) && r16 == 381,
          "cint_reduce_i16");
    r8 = 9;
    CHECK(!cint_reduce_i8(ctx, S_OP, CINT_REDUCE_SUM, CINT_TAG_I8, &xs, NULL, false, 0u, &r8) && r8 == 9,
          "a failed wrapper writes nothing");
    (void)cint_ctx_clear_fault(ctx);
    a8[0] = 100;
    a8[1] = 100;
    a8[2] = -50;
    CHECK(cint_rt_reduce(ctx, S_OP, CINT_REDUCE_SUM_SAT, CINT_TAG_I8, CINT_TAG_I8, &xs, NULL, false, 0u, &out) &&
          out == 77u, "sum_sat is a left fold: 100, 127, 77");
    a8[0] = 127;
    a8[1] = 1;
    xs = vd1(a8, 0, 2, 1);
    CHECK(cint_reduce_i8(ctx, S_OP, CINT_REDUCE_SUM_WRAP, CINT_TAG_I8, &xs, NULL, false, 0u, &r8) && r8 == -128,
          "cint_reduce_i8 sign-extends");
    a8[0] = 120;
    a8[1] = 120;
    a8[2] = 1;
    xs = vd1(a8, 0, 3, 1);
    CHECK(!cint_rt_reduce(ctx, S_OP, CINT_REDUCE_FOLD_ADD, CINT_TAG_I8, CINT_TAG_I8, &xs, NULL, true, 0u, &out),
          "fold_checked(add)");
    CHECK_REC(ctx, "1 fold_checked.add.i8 | I64 1 | I8 120 | I8 120 | exact 240 | limit I8 127",
              "at element 1 (reduce/fold_checked_i8_overflow)");
    a64[0] = 2;
    a64[1] = 3;
    a64[2] = 4;
    xs = vd1(a64, 0, 3, 1);
    CHECK(cint_rt_reduce(ctx, S_OP, CINT_REDUCE_FOLD_MUL, CINT_TAG_I64, CINT_TAG_I64, &xs, NULL, true, 1u, &out) &&
          out == 24u, "fold_checked(mul)");
    a64[0] = INT64_MAX;
    a64[1] = 1;
    xs = vd1(a64, 0, 2, 1);
    CHECK(!cint_rt_reduce(ctx, S_OP, CINT_REDUCE_SUM, CINT_TAG_I64, CINT_TAG_I64, &xs, NULL, false, 0u, &out), "sum");
    CHECK_REC(ctx, "1 sum.checked.i64.i64 | I64 2 | exact 9223372036854775808 | limit I64 9223372036854775807",
              "reduce/sum_i64_overflow");
    truth[0] = true;
    truth[1] = false;
    truth[2] = true;
    truth[3] = true;
    xs = vd1(truth, 0, 4, 1);
    CHECK(cint_rt_reduce(ctx, S_OP, CINT_REDUCE_COUNT, CINT_TAG_BOOL, CINT_TAG_I64, &xs, NULL, false, 0u, &out) &&
          out == 3u, "count");
    CHECK(cint_reduce_i64(ctx, S_OP, CINT_REDUCE_COUNT, CINT_TAG_BOOL, &xs, NULL, false, 0u, &r64) && r64 == 3,
          "count through cint_reduce_i64");
    xs = vd1(truth, 3, 2, -2);
    CHECK(cint_rt_reduce(ctx, S_OP, CINT_REDUCE_COUNT, CINT_TAG_BOOL, CINT_TAG_I64, &xs, NULL, false, 0u, &out) &&
          out == 1u, "count over a reversed strided view");
    xs = vd1(u64s, 0, 3, 1);
    CHECK(cint_reduce_u64(ctx, S_OP, CINT_REDUCE_MAX, CINT_TAG_U64, &xs, NULL, false, 0u, &ru64) &&
          ru64 == UINT64_MAX, "cint_reduce_u64");

    /* dot (ruling R6). */
    for (i = 0; i < 6; i++) {
        a64[i] = i + 1;
    }
    xs = vd1(a64, 0, 3, 1);
    ys = vd1(a64, 3, 3, 1);
    CHECK(cint_rt_reduce(ctx, S_OP, CINT_REDUCE_DOT, CINT_TAG_I64, CINT_TAG_I64, &xs, &ys, false, 0u, &out) &&
          out == 32u, "dot");
    ys = vd1(a64, 3, 2, 1);
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 10, 8) == CINT_OK, "entry");
    CHECK(!cint_rt_reduce(ctx, S_OP, CINT_REDUCE_DOT, CINT_TAG_I64, CINT_TAG_I64, &xs, &ys, false, 0u, &out),
          "unequal extents");
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT && fuel_of(ctx) == 1, "dot's shape check charges no fuel");
    CHECK_REC(ctx, "4 dot.checked.i64.i64 | I64 0 | I64 2 | limit I64 3", "dot's E_SHAPE");
    a64[0] = 3037000500;
    xs = vd1(a64, 0, 1, 1);
    CHECK(!cint_rt_reduce(ctx, S_OP, CINT_REDUCE_DOT, CINT_TAG_I64, CINT_TAG_I64, &xs, &xs, false, 0u, &out), "dot");
    CHECK_REC(ctx, "1 dot.checked.i64.i64 | I64 1 | exact 9223372037000250000 | limit I64 9223372036854775807",
              "reduce/dot_overflow");
    for (i = 0; i < 8; i++) {
        a64[i] = INT64_MIN;
    }
    xs = vd1(a64, 0, 8, 1);
    CHECK(!cint_rt_reduce(ctx, S_OP, CINT_REDUCE_DOT, CINT_TAG_I64, CINT_TAG_I64, &xs, &xs, false, 0u, &out), "2^129");
    CHECK_REC(ctx, "1 dot.checked.i64.i64 | I64 8 | exact 680564733841876926926749214863536422912"
              " | limit I64 9223372036854775807", "an exact dot beyond 2^128");

    /* Rank 2: logical order over a transposed view. */
    a8[0] = 100;
    a8[1] = 0;
    a8[2] = 100;
    a8[3] = 0;
    xs = vd2(a8, 0, 2, 2, 2, 1);
    CHECK(!cint_rt_reduce(ctx, S_OP, CINT_REDUCE_FOLD_ADD, CINT_TAG_I8, CINT_TAG_I8, &xs, NULL, true, 0u, &out), "rows");
    CHECK_REC(ctx, "1 fold_checked.add.i8 | I64 2 | I8 100 | I8 100 | exact 200 | limit I8 127", "row-major: j = 2");
    xs = vd2(a8, 0, 2, 2, 1, 2);
    CHECK(!cint_rt_reduce(ctx, S_OP, CINT_REDUCE_FOLD_ADD, CINT_TAG_I8, CINT_TAG_I8, &xs, NULL, true, 0u, &out), "cols");
    CHECK_REC(ctx, "1 fold_checked.add.i8 | I64 1 | I8 100 | I8 100 | exact 200 | limit I8 127", "transposed: j = 1");
    for (i = 0; i < 6; i++) {
        m16[i] = (int16_t)(i % 2 == 0 ? i + 1 : -(i + 1));
    }
    xs = vd2(m16, 0, 3, 2, 1, 3);
    CHECK(cint_rt_reduce(ctx, S_OP, CINT_REDUCE_SUM, CINT_TAG_I16, CINT_TAG_I64, &xs, NULL, false, 0u, &out) &&
          out == (uint64_t)(int64_t)-3, "sum over a transposed I16 view");
    CHECK(cint_rt_reduce(ctx, S_OP, CINT_REDUCE_MIN, CINT_TAG_I16, CINT_TAG_I16, &xs, NULL, false, 0u, &out) &&
          out == (uint64_t)(int64_t)-6, "min over a transposed I16 view, sign-extended");

    /* Fuel: ceil(N / 64) units at the callee (SPEC-01 IM-139). */
    for (i = 0; i < 65; i++) {
        a64[i] = 1;
    }
    xs = vd1(a64, 0, 65, 1);
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 3, 8) == CINT_OK, "entry");
    CHECK(cint_rt_reduce(ctx, S_OP, CINT_REDUCE_SUM, CINT_TAG_I64, CINT_TAG_I64, &xs, NULL, false, 0u, &out), "65");
    CHECK(cint_rt_entry_end(ctx) == CINT_OK && fuel_of(ctx) == 3 && out == 65u, "two units (reduce/fuel_reduction_charge)");
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 2, 8) == CINT_OK, "entry");
    CHECK(!cint_rt_reduce(ctx, S_OP, CINT_REDUCE_SUM, CINT_TAG_I64, CINT_TAG_I64, &xs, NULL, false, 0u, &out), "65");
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT && fuel_of(ctx) == 1, "the failed charge is not counted");
    CHECK_REC(ctx, "9 fuel.charge | limit I64 2", "reduce/fuel_reduction_e_fuel");
    xs = vd1(a64, 0, 64, 1);
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 2, 8) == CINT_OK, "entry");
    CHECK(cint_rt_reduce(ctx, S_OP, CINT_REDUCE_SUM, CINT_TAG_I64, CINT_TAG_I64, &xs, NULL, false, 0u, &out), "64");
    CHECK(cint_rt_entry_end(ctx) == CINT_OK && fuel_of(ctx) == 2, "64 elements: one unit");
    xs = vd1(a64, 0, 0, 1);
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 1, 8) == CINT_OK, "entry");
    CHECK(cint_rt_reduce(ctx, S_OP, CINT_REDUCE_SUM, CINT_TAG_I64, CINT_TAG_I64, &xs, NULL, false, 0u, &out) &&
          out == 0u, "no elements");
    CHECK(cint_rt_entry_end(ctx) == CINT_OK && fuel_of(ctx) == 1, "no elements: no charge");

    /* Arguments no emitted code passes, and a faulted context. */
    xs = vd1(a64, 0, 3, 1);
    ys = vd2(a64, 0, 1, 3, 3, 1);
    CHECK(!cint_rt_reduce(ctx, S_OP, 0u, CINT_TAG_I64, CINT_TAG_I64, &xs, NULL, false, 0u, &out), "op 0");
    CHECK_REC(ctx, "10 dispatch.admit", "op 0 is refused");
    CHECK(!cint_rt_reduce(ctx, S_OP, CINT_REDUCE_FOLD_ADD, CINT_TAG_I64, CINT_TAG_I64, &xs, NULL, false, 0u, &out),
          "fold without init");
    CHECK_REC(ctx, "10 dispatch.admit", "fold_checked without init is refused");
    CHECK(!cint_rt_reduce(ctx, S_OP, CINT_REDUCE_MIN, CINT_TAG_I64, CINT_TAG_I32, &xs, NULL, false, 0u, &out), "min");
    CHECK_REC(ctx, "10 dispatch.admit", "min with a result type other than its element's is refused");
    CHECK(!cint_rt_reduce(ctx, S_OP, CINT_REDUCE_COUNT, CINT_TAG_I64, CINT_TAG_I64, &xs, NULL, false, 0u, &out), "count");
    CHECK_REC(ctx, "10 dispatch.admit", "count over I64 elements is refused");
    CHECK(!cint_rt_reduce(ctx, S_OP, CINT_REDUCE_DOT, CINT_TAG_I64, CINT_TAG_I64, &xs, &ys, false, 0u, &out), "dot");
    CHECK_REC(ctx, "10 dispatch.admit", "dot of rank 2 is refused");
    xs.rank = 5;
    CHECK(!cint_rt_reduce(ctx, S_OP, CINT_REDUCE_SUM, CINT_TAG_I64, CINT_TAG_I64, &xs, NULL, false, 0u, &out), "rank");
    CHECK_REC(ctx, "10 dispatch.admit", "rank 5 is refused");
    xs.rank = 1;
    CHECK(!cint_rt_fault_decl_shape(ctx, S_OP, 0, -1), "a fault");
    out = 77u;
    CHECK(!cint_rt_reduce(ctx, S_OP, CINT_REDUCE_SUM, CINT_TAG_I64, CINT_TAG_I64, &xs, NULL, false, 0u, &out) &&
          out == 77u, "a faulted context reduces nothing");
    CHECK_REC(ctx, "4 decl.shape | I64 0 | I64 -1 | limit I64 0", "the first record is kept");
    cint_ctx_destroy(ctx);
}

static void test_dispatch(void)
{
    static int32_t m[256];
    static char name[300];
    cint_ctx *ctx = new_ctx();
    cint_fault_descriptor desc[2];
    cint_vdesc p, q, r;
    uint32_t count = 9u;
    int64_t sum = 0, n;
    size_t with, without;
    if (ctx == NULL) {
        CHECK(0, "dispatch context");
        return;
    }
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK, "entry");
    CHECK(cint_rt_dispatch_number(ctx) == 0 && cint_rt_dispatch_number(ctx) == 1 && cint_rt_dispatch_number(ctx) == 2,
          "dispatch numbers 0, 1, 2");
    CHECK(cint_rt_entry_end(ctx) == CINT_OK, "entry ends");
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK && cint_rt_dispatch_number(ctx) == 0,
          "each entry starts from 0");
    /* A work-item fault: the dispatch site is the innermost stack position. */
    CHECK(cint_rt_call_enter(ctx, S_CALL), "a call");
    cint_rt_dispatch_push(ctx, S_CALL2);
    CHECK(!cint_add_i64(ctx, S_OP, INT64_MAX, 1, &sum), "a fault in a work-item");
    cint_rt_dispatch_fault(ctx, "kernel.bounds_in_block.rows", 1, CINT_PHASE_WORK_ITEM, 5, 7);
    cint_rt_dispatch_fault(ctx, "kernel.other.k", 9, CINT_PHASE_ENTRY, 0, 0);
    cint_rt_dispatch_pop(ctx);
    cint_rt_call_leave(ctx);
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT, "faulted");
    get_fault(ctx, &g_rec);
    CHECK(g_rec.stack_count == 2u && g_rec.stack[0].index == S_CALL.index && g_rec.stack[1].index == S_CALL2.index,
          "the caller's call, then the dispatch site");
    CHECK(g_rec.has_address && g_rec.address.dispatch == 1 && g_rec.address.phase == 1u &&
          g_rec.address.has_work_item && g_rec.address.work_item == 5 && g_rec.address.step == 7 &&
          g_rec.address.name_len == 27u && memcmp(g_rec.address.name, "kernel.bounds_in_block.rows", 27u) == 0,
          "the work-item address; the first address stays");
    memcpy(&g_rec2, &g_rec, sizeof g_rec);
    g_rec2.has_address = 0u;
    with = cint_fault_encode(&g_rec, NULL, 0u);
    without = cint_fault_encode(&g_rec2, NULL, 0u);
    CHECK(with > 0u && without > 0u && with - without == 4u + 27u + 8u + 1u + 1u + 16u,
          "the address is encoded: name, dispatch, phase, work-item and step");
    CHECK(cint_ctx_clear_fault(ctx) == CINT_OK, "clear");
    /* An entry-phase fault: the caller's stack, no work-item. */
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK, "entry");
    n = cint_rt_dispatch_number(ctx);
    CHECK(!cint_shape_check_dim(ctx, S_CALL2, 0u, 0u, 4, 5), "bind.shape at the dispatch site");
    cint_rt_dispatch_fault(ctx, "kernel.shape_two_pass.k", n, CINT_PHASE_ENTRY, 3, 4);
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT, "faulted");
    get_fault(ctx, &g_rec);
    CHECK(g_rec.has_address && g_rec.address.dispatch == 0 && g_rec.address.phase == 0u &&
          !g_rec.address.has_work_item && g_rec.address.work_item == 0 && g_rec.address.step == 0 &&
          g_rec.stack_count == 0u && cint_fault_encode(&g_rec, NULL, 0u) > 0u, "the entry address (kernel/shape_two_pass)");
    CHECK(cint_ctx_clear_fault(ctx) == CINT_OK, "clear");
    /* An epilogue fault. */
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK, "entry");
    cint_rt_dispatch_push(ctx, S_CALL2);
    CHECK(!cint_rt_fault_reduce_empty(ctx, S_OP, false, CINT_TAG_I64), "an empty min in the epilogue");
    cint_rt_dispatch_fault(ctx, "kernel.min_no_contributions.k", 0, CINT_PHASE_EPILOGUE, 2, 2);
    cint_rt_dispatch_pop(ctx);
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT, "faulted");
    get_fault(ctx, &g_rec);
    CHECK(g_rec.has_address && g_rec.address.phase == 2u && !g_rec.address.has_work_item && g_rec.stack_count == 1u &&
          g_rec.stack[0].index == S_CALL2.index, "the epilogue address (kernel/min_no_contributions)");
    CHECK(cint_ctx_clear_fault(ctx) == CINT_OK, "clear");
    /* No address without a record, for a phase above 2, or for a name of 0 or more than 256 bytes. */
    memset(name, 'k', 257u);
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK, "entry");
    cint_rt_dispatch_fault(ctx, "kernel.k.f", 0, CINT_PHASE_WORK_ITEM, 0, 0);
    CHECK(!cint_rt_faulted(ctx), "no record, no address");
    CHECK(!cint_add_i64(ctx, S_OP, INT64_MAX, 1, &sum), "a fault");
    cint_rt_dispatch_fault(ctx, "kernel.k.f", 0, 3u, 0, 0);
    cint_rt_dispatch_fault(ctx, "", 0, CINT_PHASE_ENTRY, 0, 0);
    cint_rt_dispatch_fault(ctx, NULL, 0, CINT_PHASE_ENTRY, 0, 0);
    cint_rt_dispatch_fault(ctx, name, 0, CINT_PHASE_ENTRY, 0, 0);
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT, "faulted");
    get_fault(ctx, &g_rec);
    CHECK(!g_rec.has_address, "no address was set");
    CHECK(cint_ctx_clear_fault(ctx) == CINT_OK, "clear");
    name[256] = '\0';
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK && !cint_add_i64(ctx, S_OP, INT64_MAX, 1, &sum),
          "a fault");
    cint_rt_dispatch_fault(ctx, name, 0, CINT_PHASE_ENTRY, 0, 0);
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT, "faulted");
    get_fault(ctx, &g_rec);
    CHECK(g_rec.has_address && g_rec.address.name_len == 256u, "a name of 256 bytes");
    CHECK(cint_ctx_clear_fault(ctx) == CINT_OK, "clear");
    /* Pushes stay inside the stack; a pop removes no call. */
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK, "entry");
    cint_rt_dispatch_push(ctx, S_CALL2);
    cint_rt_dispatch_push(ctx, S_CALL2);
    CHECK(cint_rt_call_enter(ctx, S_CALL), "a call inside");
    cint_rt_call_leave(ctx);
    cint_rt_dispatch_pop(ctx);
    cint_rt_dispatch_pop(ctx);
    cint_rt_dispatch_pop(ctx);
    CHECK(cint_rt_call_enter(ctx, S_CALL), "a call");
    cint_rt_dispatch_pop(ctx);
    CHECK(!cint_add_i64(ctx, S_OP, INT64_MAX, 1, &sum), "a fault");
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT, "faulted");
    get_fault(ctx, &g_rec);
    CHECK(g_rec.stack_count == 1u && g_rec.stack[0].index == S_CALL.index, "pops removed the pushes and no call");
    CHECK(cint_ctx_clear_fault(ctx) == CINT_OK, "clear");
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 1024) == CINT_OK, "entry with D = 1024");
    cint_rt_dispatch_push(ctx, S_CALL2);
    CHECK(!cint_rt_faulted(ctx), "a first push at D = 1024");
    cint_rt_dispatch_push(ctx, S_CALL2);
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT, "a second push at D = 1024 could overrun the stack");
    get_fault(ctx, &g_rec);
    CHECK(g_rec.stack_count == 1u, "the record has the first push only");
    CHECK_REC(ctx, "10 dispatch.admit", "refused");

    /* Descriptors (SPEC-02 A-8; kernel/alias_uncertain_columns). */
    p = vd2(m, 8, 8, 8, 16, 1);
    q = vd2(m, 0, 8, 8, 16, 1);
    r = vd2(m, 128, 4, 8, 16, 1);
    CHECK(cint_ctx_fault_descriptors(ctx, desc, &count) == CINT_OK && count == 0u, "no record, no descriptors");
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK, "entry");
    CHECK(cint_ctx_fault_descriptors(ctx, desc, &count) == CINT_BUSY, "CINT_BUSY during an entry");
    CHECK(cint_rt_dispatch_alias(ctx, S_CALL2, 1u, 0u, &r, &q, CINT_TAG_I32, 3u), "disjoint by T1");
    CHECK(!cint_rt_dispatch_alias(ctx, S_CALL2, 1u, 0u, &p, &q, CINT_TAG_I32, 3u), "uncertain columns");
    cint_rt_dispatch_fault(ctx, "kernel.alias_uncertain_columns.copy2", 0, CINT_PHASE_ENTRY, 0, 0);
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT && fuel_of(ctx) == 1, "fuel kept");
    CHECK(cint_ctx_fault_descriptors(ctx, desc, &count) == CINT_OK && count == 2u, "two descriptors");
    CHECK(desc[0].elem == CINT_TAG_I32 && desc[0].write == 1u && desc[0].rank == 2 && desc[0].origin == 8 &&
          desc[0].shape[0] == 8 && desc[0].shape[1] == 8 && desc[0].stride[0] == 16 && desc[0].stride[1] == 1 &&
          desc[0].shape[2] == 0 && desc[0].stride[3] == 0, "p's descriptor: I32 none 2 8 8 0 16 8 0 1 write");
    CHECK(desc[1].origin == 0 && desc[1].write == 1u && desc[1].shape[1] == 8, "q's descriptor");
    CHECK(cint_ctx_fault_descriptors(NULL, desc, &count) == CINT_REFUSED &&
          cint_ctx_fault_descriptors(ctx, desc, NULL) == CINT_REFUSED, "absent arguments are refused");
    CHECK_REC(ctx, "7 bind.alias | I64 1 | I64 0", "bind.alias at entry");
    CHECK(cint_ctx_fault_descriptors(ctx, desc, &count) == CINT_OK && count == 0u, "clearing the fault clears them");
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK, "entry");
    CHECK(!cint_rt_dispatch_alias(ctx, S_CALL2, 0u, 1u, &q, &p, CINT_TAG_U8, 2u), "read then write");
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT, "faulted");
    CHECK(cint_ctx_fault_descriptors(ctx, desc, &count) == CINT_OK && count == 2u && desc[0].write == 0u &&
          desc[1].write == 1u && desc[0].elem == CINT_TAG_U8 && desc[0].origin == 0, "bit 0 for p, bit 1 for q");
    CHECK(cint_ctx_clear_fault(ctx) == CINT_OK, "clear");
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK && !cint_add_i64(ctx, S_OP, INT64_MAX, 1, &sum),
          "another fault first");
    CHECK(!cint_rt_dispatch_alias(ctx, S_CALL2, 1u, 0u, &p, &q, CINT_TAG_I32, 3u), "a faulted context");
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT, "faulted");
    CHECK(cint_ctx_fault_descriptors(ctx, desc, &count) == CINT_OK && count == 0u, "no descriptors on that record");
    CHECK_REC(ctx, "1 add.checked.i64 | I64 9223372036854775807 | I64 1 | exact 9223372036854775808"
              " | limit I64 9223372036854775807", "the first record");
    cint_ctx_destroy(ctx);
}

/* The where record, staging buffers and kernel accumulators (SPEC-02 F-5, P-4, R-2, R-7). */
static void test_kernel_entry(void)
{
    static int32_t m[24];
    cint_ctx *ctx = new_ctx();
    cint_vdesc src, st;
    cint_zacc za;
    int32_t acc = 0;
    int64_t k, sum = 0;
    int ok = 1;
    if (ctx == NULL) {
        CHECK(0, "kernel context");
        return;
    }
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK, "entry");
    CHECK(!cint_rt_fault_where(ctx, S_CALL2, 0, 1, 2), "a false constraint");
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT && fuel_of(ctx) == 1, "fuel kept");
    CHECK_REC(ctx, "4 bind.where | I64 0 | I64 1 | I64 2", "E_SHAPE bind.where: index, left, right (OQ-203)");
    for (k = 0; k < 24; k++) {
        m[k] = (int32_t)k;
    }
    /* The columns 1 and 2 of a 4 by 6 array, rows reversed: an `inout` copy-in. */
    src = vd2(m, 19, 4, 2, -6, 1);
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK, "entry");
    CHECK(cint_rt_dispatch_stage(ctx, S_OP, &st, &src, sizeof m[0], true), "an inout staging buffer");
    CHECK(st.origin == 0 && st.rank == 2 && st.shape[0] == 4 && st.shape[1] == 2 && st.stride[0] == 2 &&
          st.stride[1] == 1 && st.shape[2] == 0 && st.stride[2] == 0, "row-major, the argument's extents");
    for (k = 0; k < 8; k++) {
        ok = ok && ((int32_t *)st.base)[k] == (int32_t)(19 - 6 * (k / 2) + k % 2);
    }
    CHECK(ok, "copied in logical order");
    CHECK(cint_rt_dispatch_stage(ctx, S_OP, &st, &src, sizeof m[0], false) && ((int32_t *)st.base)[7] == 0,
          "an out staging buffer starts at zero");
    src = vd2(m, 0, 0, 5, 5, 1);
    CHECK(cint_rt_dispatch_stage(ctx, S_OP, &st, &src, sizeof m[0], true) && st.shape[0] == 0 && st.shape[1] == 5,
          "an empty view");
    CHECK(cint_rt_scratch_mark(ctx) == 2, "two allocations");
    CHECK(cint_rt_entry_end(ctx) == CINT_OK, "entry ends");
    src.rank = 5;
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK &&
          !cint_rt_dispatch_stage(ctx, S_OP, &st, &src, sizeof m[0], false), "rank 5");
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT, "faulted");
    CHECK_REC(ctx, "10 dispatch.admit", "refused");
    /* Accumulators: fold_checked at its step, sum at the epilogue, sum_wrap wrapped. */
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK, "entry");
    acc = INT32_MAX - 1;
    CHECK(cint_kfold_i32(ctx, S_OP, 0, &acc, 1) && acc == INT32_MAX, "fold_checked adds");
    CHECK(!cint_kfold_i32(ctx, S_OP, 1, &acc, 1) && acc == INT32_MAX, "and faults at the bound");
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT, "faulted");
    CHECK_REC(ctx, "1 fold_checked.add.i32 | I64 1 | I32 2147483647 | I32 1 | exact 2147483648"
              " | limit I32 2147483647", "fold_checked.add.i32: n, acc, x");
    memset(&za, 0, sizeof za);
    cint_zacc_add(&za, CINT_TAG_I64, (uint64_t)INT64_MAX);
    cint_zacc_add(&za, CINT_TAG_I64, 1u);
    CHECK(cint_kwrap_i64(&za) == INT64_MIN && cint_kwrap_i8(&za) == 0, "sum_wrap wraps the total");
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK && !cint_ksum_i64(ctx, S_OP, CINT_TAG_I64, 2, &za, &sum),
          "sum faults past INT64_MAX");
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT, "faulted");
    CHECK_REC(ctx, "1 sum.checked.i64.i64 | I64 2 | exact 9223372036854775808 | limit I64 9223372036854775807",
              "sum.checked.i64.i64: n (kernel/total_exact_epilogue_overflow)");
    cint_zacc_add(&za, CINT_TAG_I64, (uint64_t)-2);
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK && cint_ksum_i64(ctx, S_OP, CINT_TAG_I64, 3, &za, &sum) &&
          sum == INT64_MAX - 1, "a total that fits");
    CHECK(cint_rt_entry_end(ctx) == CINT_OK, "entry ends");
    cint_ctx_destroy(ctx);
}

/* A host-issued dispatch (rt/OPEN.md RT-OQ-40): the wrapper makes checks 1 to
 * 3 and hands its binds over; the kernel's first KALIAS or KFUEL makes checks
 * 6 to 8, once per entry. */
static void test_host_dispatch(void)
{
    static int64_t words[8];
    cint_ctx *ctx = new_ctx();
    cint_buffer_desc d;
    cint_buffer_id copy = 0u, copy2 = 0u, none = 0u;
    cint_fault_descriptor desc[2];
    uint64_t gen;
    uint32_t count = 0u;
    cint_view v, w;
    cint_bind b[2];
    cint_vdesc p, q;
    if (ctx == NULL) {
        CHECK(0, "host dispatch context");
        return;
    }
    d = host_desc(words, T_I64, 8, 1u);
    d.publish = CINT_PUBLISH_COPY;
    CHECK(cint_buffer_register(ctx, &d, &copy, &gen) == CINT_OK &&
              cint_buffer_register(ctx, &d, &copy2, &gen) == CINT_OK,
          "two publish copy registrations of one array");
    d.publish = CINT_PUBLISH_NONE;
    CHECK(cint_buffer_register(ctx, &d, &none, &gen) == CINT_OK, "a publish none registration");
    v = view_of(copy, 0, 4, CINT_VIEW_READ);
    w = view_of(none, 4, 4, CINT_VIEW_WRITE);
    b[0] = bind_of(&v, 0u, CINT_MODE_IN);
    b[1] = bind_of(&w, 1u, CINT_MODE_OUT);
    CHECK(cint_rt_entry_open(ctx, S_FUEL, 100, 8) == CINT_OK && cint_rt_bind_views(ctx, S_CALL, b, 2u),
          "checks 1 to 3 at the wrapper");
    cint_rt_dispatch_host(ctx, b, 2u);
    CHECK(!cint_rt_dispatch_bound(ctx, S_CALL) && cint_rt_entry_end(ctx) == CINT_FAULT && fuel_of(ctx) == 0 &&
              bind_record_is(ctx, CINT_E_UNSUPPORTED, "bind.limit", "I64 1", NULL),
          "an out view of a borrowed registration with publish none (check 8, M-30a); no fuel");
    w = view_of(copy, 4, 4, CINT_VIEW_WRITE);
    CHECK(cint_rt_entry_open(ctx, S_FUEL, 100, 8) == CINT_OK && cint_rt_bind_views(ctx, S_CALL, b, 2u),
          "bind");
    cint_rt_dispatch_host(ctx, b, 2u);
    CHECK(cint_rt_dispatch_bound(ctx, S_CALL) && cint_rt_dispatch_bound(ctx, S_CALL) &&
              cint_rt_entry_end(ctx) == CINT_OK,
          "publish copy, and halves of one registration disjoint by T1; the checks run once");
    /* The same bytes through two registrations: uncertain across registrations
     * (H-13), found by the dispatch's alias step whatever its descriptors say. */
    w = view_of(copy2, 2, 4, CINT_VIEW_WRITE);
    memset(&p, 0, sizeof p);
    memset(&q, 0, sizeof q);
    p.base = &words[4];
    q.base = &words[0];
    p.rank = q.rank = 1;
    p.shape[0] = q.shape[0] = 1;
    p.stride[0] = q.stride[0] = 1;
    CHECK(cint_rt_entry_open(ctx, S_FUEL, 100, 8) == CINT_OK && cint_rt_bind_views(ctx, S_CALL, b, 2u),
          "bind");
    cint_rt_dispatch_host(ctx, b, 2u);
    CHECK(!cint_rt_dispatch_alias(ctx, S_CALL, 1u, 0u, &p, &q, CINT_TAG_I64, 1u) &&
              cint_rt_entry_end(ctx) == CINT_FAULT && cint_ctx_fault_descriptors(ctx, desc, &count) == CINT_OK &&
              count == 2u && desc[0].write == 1u && desc[0].origin == 2 && desc[1].write == 0u &&
              desc[1].shape[0] == 4 && bind_record_is(ctx, CINT_E_ALIAS, "bind.alias", "I64 1, I64 0", NULL),
          "checks 6 and 7 of the handed binds at the first KALIAS, the two views as descriptors (RT-OQ-35)");
    CHECK(cint_rt_entry_open(ctx, S_FUEL, 100, 8) == CINT_OK && cint_rt_dispatch_bound(ctx, S_CALL) &&
              cint_rt_entry_end(ctx) == CINT_OK,
          "a dispatch from .ci: no binds were handed over");
    cint_ctx_destroy(ctx);
}

static void test_scratch(void)
{
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL;
    int live = 0, base = 0;
    void *p = NULL, *q = NULL;
    int64_t mark;
    size_t i, nonzero = 0u;
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = &t_program;
    cfg.allocator.alloc = test_alloc;
    cfg.allocator.release = test_release;
    cfg.allocator.user = &live;
    cfg.frame_arena_bytes = 80;  /* 10 elements */
    /* The context and its buffer and lease tables (RT-OQ-39) are allocations of their own. */
    CHECK(cint_ctx_create(&cfg, &ctx) == CINT_OK && (base = live) > 0,
          "a context with an arena of 10 elements");
    if (ctx == NULL) {
        return;
    }
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK, "entry");
    CHECK(cint_rt_scratch(ctx, S_OP, 3, 8u, &p) && p != NULL && live == base + 1 && (uintptr_t)p % 16u == 0u,
          "3 elements of 8 bytes, aligned");
    for (i = 0; p != NULL && i < 24u; i++) {
        nonzero += ((unsigned char *)p)[i] != 0u;
    }
    CHECK(nonzero == 0u && cint_rt_scratch_mark(ctx) == 1, "zero-filled");
    CHECK(cint_rt_scratch(ctx, S_OP, 7, 2u, &q) && live == base + 2, "7 more elements");
    q = NULL;
    CHECK(cint_rt_scratch(ctx, S_OP, 0, 8u, &q) && q != NULL && live == base + 2,
          "0 elements: a pointer, no allocation");
    CHECK(!cint_rt_scratch(ctx, S_OP, 1, 1u, &q), "the eleventh element");
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT && live == base + 2, "a faulted entry leaves its allocations");
    CHECK_REC(ctx, "3 arena.alloc | I64 1 | I64 10 | I64 10", "E_BOUNDS arena.alloc: requested, in use, capacity");
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK && live == base, "the next entry frees them");
    CHECK(cint_rt_scratch(ctx, S_OP, 4, 4u, &p), "4 elements");
    mark = cint_rt_scratch_mark(ctx);
    CHECK(mark == 1 && cint_rt_scratch(ctx, S_OP, 6, 4u, &q) && live == base + 2, "6 more after the mark");
    cint_rt_scratch_release(ctx, mark);
    CHECK(live == base + 1 && cint_rt_scratch(ctx, S_OP, 6, 4u, &q) && live == base + 2,
          "release frees them and their elements");
    cint_rt_scratch_release(ctx, 0);
    CHECK(live == base && cint_rt_scratch_mark(ctx) == 0, "release to 0 frees everything");
    g_fail_alloc = 1;
    CHECK(!cint_rt_scratch(ctx, S_OP, 2, 8u, &p), "the allocator fails");
    g_fail_alloc = 0;
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT, "faulted");
    CHECK_REC(ctx, "10 arena.alloc | I64 2 | I64 0 | I64 10", "E_UNSUPPORTED arena.alloc");
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK && !cint_rt_scratch(ctx, S_OP, 3, SIZE_MAX / 2u, &p),
          "a byte size beyond size_t");
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT, "faulted");
    CHECK_REC(ctx, "10 arena.alloc | I64 3 | I64 0 | I64 10", "E_UNSUPPORTED arena.alloc");
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK && !cint_rt_scratch(ctx, S_OP, -1, 8u, &p), "count -1");
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT, "faulted");
    CHECK_REC(ctx, "10 dispatch.admit", "a negative count is refused");
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK && cint_rt_scratch(ctx, S_OP, 5, 8u, &p) &&
              live == base + 1,
          "an allocation left at the end of an entry");
    CHECK(cint_rt_entry_end(ctx) == CINT_OK, "entry ends");
    cint_ctx_destroy(ctx);
    CHECK(live == 0, "cint_ctx_destroy frees it");
    cfg.frame_arena_bytes = 0;
    ctx = NULL;
    CHECK(cint_ctx_create(&cfg, &ctx) == CINT_OK, "the default arena");
    if (ctx == NULL) {
        return;
    }
    CHECK(cint_rt_entry_begin(ctx, S_FUEL, 100, 8) == CINT_OK && !cint_rt_scratch(ctx, S_OP, 2097153, 1u, &p),
          "one element beyond 2,097,152");
    CHECK(cint_rt_entry_end(ctx) == CINT_FAULT, "faulted");
    CHECK_REC(ctx, "3 arena.alloc | I64 2097153 | I64 0 | I64 2097152", "the default capacity (BX12-23)");
    cint_ctx_destroy(ctx);
    CHECK(live == 0, "nothing leaked");
}

/* rt/tests/test_state.c: checkpoints (rt/cint_state.h; box 13 unit 4). */
void test_state(long *pass, long *fail);

int main(int argc, char **argv)
{
    int exh8 = 1;
    long pairs = 1000000;
    int i;
    if (argc < 2) {
        fprintf(stderr, "usage: test_rt <repository-root> [--no-exh8] [--pairs N]\n"
                        "       test_rt --run <scenario> <fault-destination or -> [<fuel> <state>]\n"
                        "       test_rt --record <scenario>\n");
        return 2;
    }
    if ((argc == 4 || argc == 6) && strcmp(argv[1], "--run") == 0) {
        const char *dests[3] = {NULL, NULL, NULL};
#if defined(_WIN32)
        /* argv is in the ANSI code page on Windows; the destinations are UTF-8
         * (cint_program_run), so they are taken from the UTF-16 command line. */
        static char dest[3][4096];
        int wargc = 0;
        wchar_t **wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
        if (wargv == NULL || wargc != argc) {
            fprintf(stderr, "test_rt: cannot read the destinations as UTF-8\n");
            return 2;
        }
        for (i = 3; i < argc; i++) {
            if (WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, dest[i - 3], (int)sizeof dest[i - 3], NULL, NULL) <= 0) {
                fprintf(stderr, "test_rt: cannot read the destinations as UTF-8\n");
                return 2;
            }
            dests[i - 3] = dest[i - 3];
        }
        LocalFree(wargv);
#else
        for (i = 3; i < argc; i++) {
            dests[i - 3] = argv[i];
        }
#endif
        return run_scenario(argv[2], dests, false);
    }
    if (argc == 3 && strcmp(argv[1], "--record") == 0) {
        return run_scenario(argv[2], NULL, true);
    }
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--no-exh8") == 0) {
            exh8 = 0;
        } else if (strcmp(argv[i], "--pairs") == 0 && i + 1 < argc) {
            pairs = strtol(argv[++i], NULL, 10);
        } else {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            return 2;
        }
    }
    printf("helper mode: %s\n", CINT_RT_HELPER_MODE);
    test_sticky();
    test_bad_args();
    test_encoder();
    test_entries();
    test_context();
    test_buffers();
    test_logical_buffers();
    test_copy_shape();
    test_entry_faults();
    test_bind();
    test_registry();
    test_output();
    test_print_format();
    test_index_dim();
    test_slices();
    test_relation();
    test_copies();
    test_reductions();
    test_dispatch();
    test_kernel_entry();
    test_host_dispatch();
    test_scratch();
    test_state(&g_pass, &g_fail);
    test_errors();
    test_result_forms();
    test_hand();
    test_agreement(pairs);
    test_tables(argv[1], exh8);
    printf("checks: %ld passed, %ld failed; records skipped: %ld\n", g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
