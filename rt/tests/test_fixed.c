/* test_fixed.c: the fixed-point helpers of rt/cint_fixed.h against cint_ref
 * (roadmap box 15; SPEC-01 IM-54 to IM-69; box 15 note 6.4).
 *
 * tests/test_cint_rt_fixed.py writes the cases from cint_ref, one per line,
 * `<id> <kind> <ta> <tb> <tr> <mode> <a> <b> = <result>`, and the exhaustive
 * table of the 8-bit types, and runs this driver on every leg. Each case calls
 * one helper at site 1 of module `fixed.ci` (line 1, column 1) and renders its
 * outcome as the generator does: the value as a 64-bit pattern, the `as?`
 * ArithError tag and value, or the canonical bytes of the fault record
 * (cint_fault_encode). A helper that faults must leave its result unwritten.
 * Each block of the table gives the outcome of every input of one operation,
 * type, mode and form: an input whose outcome is a value runs through the
 * public helper, and one whose outcome is a fault through the arithmetic core
 * that the helper calls (cint_q_mul_raw and the others), every 64th of them
 * also through the helper with its record, whose code must match; so millions
 * of faults do not each write a record. Then a fixed-point operand, exact and
 * limit render as `Q16.16 raw 98304`, a helper on a faulted context writes
 * nothing, and a record with fixed-point operands survives a checkpoint and a
 * restore. Prints the helper mode and the counts, and exits 1 on any failure.
 *
 * Usage: test_fixed <cases> <table>.
 */
/* fopen, sprintf and strcpy over bounded buffers, which MSVC deprecates. */
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS 1
#endif
#include "cint_rt.h"
#include "cint_rt_host.h"
#include "cint_rt_internal.h"
#include "cint_state.h"
#include "cint_fixed.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static long g_pass;
static long g_fail;

static const cint_site_info f_sites[] = {{0u, 0u, NULL}, {1u, 1u, "fixed"}};
static const cint_module f_module = {"fixed.ci", 8u, 2u, f_sites};
static const cint_module *const f_modules[] = {&f_module};
static const cint_program f_program = {1u, 0u, f_modules, NULL};
static const cint_site F_SITE = {0u, 1u};

#define SENTINEL 0x5Au

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

/* The fault record of a failed helper, as "F <hex>", and the context cleared. */
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

static void put_value(char *dst, uint64_t bits)
{
    sprintf(dst, "V %016llx", (unsigned long long)bits);
}

/* The value of a type word's pattern, sign- or zero-extended to 64 bits. */
static uint64_t extend(uint32_t t, uint64_t bits)
{
    unsigned w = cint_rt_tag_width(t);
    return cint_rt_tag_signed(t) ? (uint64_t)cint_rt_sext(bits, w) : bits & cint_rt_mask(w);
}

/* Whether kind names a result form: add_result, sub_result, mul_result, div_result or sqrt_result. */
static int is_result(const char *kind)
{
    size_t n = strlen(kind);
    return n > 7u && strcmp(kind + n - 7u, "_result") == 0;
}

/* One case on the storage S of the result type tr. */
#define RUN_Q(S, CT, W)                                                                                      \
    static int run_q_##S(cint_ctx *ctx, const char *kind, uint32_t ta, uint32_t tb, uint32_t tr, uint32_t mode, \
                         uint64_t a, uint64_t b, char *got)                                                 \
    {                                                                                                       \
        unsigned f = cint_q_frac(tr);                                                                       \
        CT x = (CT)cint_rt_sext(a, (W)), y = (CT)cint_rt_sext(b, (W)), r = (CT)SENTINEL;                     \
        bool ok;                                                                                            \
        if (strcmp(kind, "add") == 0) {                                                                     \
            ok = cint_q_add_##S(ctx, F_SITE, f, x, y, &r);                                                  \
        } else if (strcmp(kind, "sub") == 0) {                                                              \
            ok = cint_q_sub_##S(ctx, F_SITE, f, x, y, &r);                                                  \
        } else if (strcmp(kind, "neg") == 0) {                                                              \
            ok = cint_q_neg_##S(ctx, F_SITE, f, x, &r);                                                     \
        } else if (strcmp(kind, "abs") == 0) {                                                              \
            ok = cint_q_abs_##S(ctx, F_SITE, f, x, &r);                                                     \
        } else if (strcmp(kind, "mul") == 0) {                                                              \
            ok = cint_q_mul_##S(ctx, F_SITE, f, mode, x, y, &r);                                            \
        } else if (strcmp(kind, "mul_wrap") == 0) {                                                         \
            r = cint_q_mul_wrap_##S(f, mode, x, y);                                                         \
            ok = true;                                                                                      \
        } else if (strcmp(kind, "mul_sat") == 0) {                                                          \
            r = cint_q_mul_sat_##S(f, mode, x, y);                                                          \
            ok = true;                                                                                      \
        } else if (strcmp(kind, "div") == 0) {                                                              \
            ok = cint_q_div_##S(ctx, F_SITE, f, mode, x, y, &r);                                            \
        } else if (strcmp(kind, "sqrt") == 0) {                                                             \
            ok = cint_q_sqrt_##S(ctx, F_SITE, f, mode, x, &r);                                              \
        } else if (strcmp(kind, "mul_int") == 0) {                                                          \
            ok = cint_q_mul_int_##S(ctx, F_SITE, f, tb, x, extend(tb, b), &r);                              \
        } else if (strcmp(kind, "qmul") == 0) {                                                             \
            ok = cint_qmul_##S(ctx, F_SITE, ta, tb, f, mode, (int64_t)extend(ta, a), (int64_t)extend(tb, b), &r); \
        } else if (is_result(kind)) {                                                                       \
            uint16_t e;                                                                                     \
            if (strcmp(kind, "mul_result") == 0) {                                                          \
                e = cint_q_mul_result_##S(f, mode, x, y, &r);                                               \
            } else if (strcmp(kind, "div_result") == 0) {                                                   \
                e = cint_q_div_result_##S(f, mode, x, y, &r);                                               \
            } else if (strcmp(kind, "sqrt_result") == 0) {                                                  \
                e = cint_q_sqrt_result_##S(f, mode, x, &r);                                                 \
            } else if (strcmp(kind, "add_result") == 0) {                                                   \
                e = cint_add_result_##S(x, y, &r);                                                          \
            } else {                                                                                        \
                e = cint_sub_result_##S(x, y, &r);                                                          \
            }                                                                                               \
            if (e != 0u) {                                                                                  \
                sprintf(got, "R %u%s", (unsigned)e, r != (CT)SENTINEL ? " (result written)" : "");          \
            } else {                                                                                        \
                sprintf(got, "R 0 %016llx", (unsigned long long)(int64_t)r);                                \
            }                                                                                               \
            return 1;                                                                                       \
        } else {                                                                                            \
            return 0;                                                                                       \
        }                                                                                                   \
        if (ok) {                                                                                           \
            put_value(got, (uint64_t)(int64_t)r);                                                           \
        } else {                                                                                            \
            put_fault(ctx, got);                                                                            \
            if (r != (CT)SENTINEL) strcat(got, " (result written)");                                        \
        }                                                                                                   \
        return 1;                                                                                           \
    }
RUN_Q(i8, int8_t, 8u)
RUN_Q(i16, int16_t, 16u)
RUN_Q(i32, int32_t, 32u)
RUN_Q(i64, int64_t, 64u)

/* One conversion to the C type R of the target (the storage of a fixed-point one). */
#define RUN_AS(R, CR, K)                                                                                     \
    static int run_as_##R(cint_ctx *ctx, const char *kind, uint32_t ta, uint32_t tr, uint32_t mode, uint64_t a, \
                          char *got)                                                                        \
    {                                                                                                       \
        uint64_t x = extend(ta, a);                                                                         \
        CR r = (CR)SENTINEL;                                                                                \
        if (strcmp(kind, "as") == 0) {                                                                      \
            if (cint_q_as_##R(ctx, F_SITE, ta, tr, mode, x, &r)) {                                          \
                put_value(got, CINT_RT_BITS_##K(r));                                                        \
            } else {                                                                                        \
                put_fault(ctx, got);                                                                        \
                if (r != (CR)SENTINEL) strcat(got, " (result written)");                                    \
            }                                                                                               \
        } else if (strcmp(kind, "as_wrap") == 0) {                                                          \
            put_value(got, CINT_RT_BITS_##K(cint_q_as_wrap_##R(ta, tr, mode, x)));                          \
        } else if (strcmp(kind, "as_sat") == 0) {                                                           \
            put_value(got, CINT_RT_BITS_##K(cint_q_as_sat_##R(ta, tr, mode, x)));                           \
        } else if (strcmp(kind, "as_result") == 0) {                                                        \
            uint16_t tag = cint_q_as_result_##R(ta, tr, mode, x, &r);                                       \
            if (tag != 0u) {                                                                                \
                sprintf(got, "R %u%s", (unsigned)tag, r != (CR)SENTINEL ? " (result written)" : "");        \
            } else {                                                                                        \
                sprintf(got, "R 0 %016llx", (unsigned long long)CINT_RT_BITS_##K(r));                       \
            }                                                                                               \
        } else {                                                                                            \
            return 0;                                                                                       \
        }                                                                                                   \
        return 1;                                                                                           \
    }
RUN_AS(i8, int8_t, S)
RUN_AS(i16, int16_t, S)
RUN_AS(i32, int32_t, S)
RUN_AS(i64, int64_t, S)
RUN_AS(u8, uint8_t, U)
RUN_AS(u16, uint16_t, U)
RUN_AS(u32, uint32_t, U)
RUN_AS(u64, uint64_t, U)

static int run_case(cint_ctx *ctx, char *line, char *got, const char **want)
{
    char kind[16];
    unsigned long ta, tb, tr, mode;
    unsigned long long a, b;
    char *eq = strstr(line, " = ");
    uint32_t rt;
    if (eq == NULL) return 0;
    *eq = '\0';
    *want = eq + 3;
    if (sscanf(line, "%*d %15s %lu %lu %lu %lu %llx %llx", kind, &ta, &tb, &tr, &mode, &a, &b) != 7) return 0;
    rt = (uint32_t)tr & 0xffu;
    if (strncmp(kind, "as", 2) == 0) {
        switch (rt) {
        case CINT_TAG_I8: return run_as_i8(ctx, kind, (uint32_t)ta, (uint32_t)tr, (uint32_t)mode, a, got);
        case CINT_TAG_I16: return run_as_i16(ctx, kind, (uint32_t)ta, (uint32_t)tr, (uint32_t)mode, a, got);
        case CINT_TAG_I32: return run_as_i32(ctx, kind, (uint32_t)ta, (uint32_t)tr, (uint32_t)mode, a, got);
        case CINT_TAG_I64: return run_as_i64(ctx, kind, (uint32_t)ta, (uint32_t)tr, (uint32_t)mode, a, got);
        case CINT_TAG_U8: return run_as_u8(ctx, kind, (uint32_t)ta, (uint32_t)tr, (uint32_t)mode, a, got);
        case CINT_TAG_U16: return run_as_u16(ctx, kind, (uint32_t)ta, (uint32_t)tr, (uint32_t)mode, a, got);
        case CINT_TAG_U32: return run_as_u32(ctx, kind, (uint32_t)ta, (uint32_t)tr, (uint32_t)mode, a, got);
        case CINT_TAG_U64: return run_as_u64(ctx, kind, (uint32_t)ta, (uint32_t)tr, (uint32_t)mode, a, got);
        default: return 0;
        }
    }
    if (!cint_q_is((uint32_t)tr)) return 0;
    switch (rt) {
    case CINT_TAG_I8: return run_q_i8(ctx, kind, (uint32_t)ta, (uint32_t)tb, (uint32_t)tr, (uint32_t)mode, a, b, got);
    case CINT_TAG_I16: return run_q_i16(ctx, kind, (uint32_t)ta, (uint32_t)tb, (uint32_t)tr, (uint32_t)mode, a, b, got);
    case CINT_TAG_I32: return run_q_i32(ctx, kind, (uint32_t)ta, (uint32_t)tb, (uint32_t)tr, (uint32_t)mode, a, b, got);
    case CINT_TAG_I64: return run_q_i64(ctx, kind, (uint32_t)ta, (uint32_t)tb, (uint32_t)tr, (uint32_t)mode, a, b, got);
    default: return 0;
    }
}

/* ------------------------------------------------------------------------- */
/* The exhaustive table of the 8-bit types (note 6.4).                        */

#define K_MUL 1u
#define K_DIV 2u
#define K_SQRT 3u
#define K_AS 4u

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* The status of the context's record, by the table's numbering, and the context cleared. */
static unsigned record_status(cint_ctx *ctx)
{
    static cint_fault_record rec;
    unsigned st = 0u;
    if (cint_ctx_fault(ctx, &rec) == CINT_OK) {
        st = rec.code == CINT_E_OVERFLOW ? 1u : rec.code == CINT_E_DIV_ZERO ? 2u : rec.code == CINT_E_DOMAIN ? 3u
             : rec.code == CINT_E_NARROW ? 4u : 9u;
    }
    (void)cint_ctx_clear_fault(ctx);
    return st;
}

/* The outcome of entry i of a block, as the table writes it: status * 256 + raw byte. */
static unsigned table_entry(cint_ctx *ctx, uint32_t kind, uint32_t form, uint32_t mode, uint32_t ta, uint32_t tr,
                            uint32_t i, unsigned want, long *helper_faults)
{
    unsigned f = cint_q_frac(ta);
    int8_t a = (int8_t)((int)(i / 256u) - 128), b = (int8_t)((int)(i % 256u) - 128), r = (int8_t)SENTINEL;
    uint64_t bits = 0u;
    bool expect_fault = (want >> 8) != 0u, full = !expect_fault || (i % 64u) == 0u, ok = false;
    unsigned st = 0u;
    if (kind == K_MUL) {
        if (form == CINT_Q_WRAP) return (uint8_t)cint_q_mul_wrap_i8(f, mode, a, b);
        if (form == CINT_Q_SAT) return (uint8_t)cint_q_mul_sat_i8(f, mode, a, b);
        if (!full) return cint_q_mul_raw(a, b, 8u, f, mode, CINT_Q_CHECKED, &bits) ? (uint8_t)bits : 256u;
        ok = cint_q_mul_i8(ctx, F_SITE, f, mode, a, b, &r);
    } else if (kind == K_DIV) {
        if (!full) return b == 0 ? 512u : cint_q_div_raw(a, b, 8u, f, mode, &bits) ? (uint8_t)bits : 256u;
        ok = cint_q_div_i8(ctx, F_SITE, f, mode, a, b, &r);
    } else if (kind == K_SQRT) {
        a = (int8_t)((int)i - 128);
        if (!full) return a < 0 ? 768u : cint_q_sqrt_raw(a, 8u, f, mode, &bits) ? (uint8_t)bits : 256u;
        ok = cint_q_sqrt_i8(ctx, F_SITE, f, mode, a, &r);
    } else {
        uint64_t x = cint_rt_tag_signed(ta) ? (uint64_t)(int64_t)((int)i - 128) : (uint64_t)i;
        if (form == CINT_Q_WRAP) {
            return (tr & 0xffu) == CINT_TAG_U8 ? (unsigned)cint_q_as_wrap_u8(ta, tr, mode, x)
                                               : (unsigned)(uint8_t)cint_q_as_wrap_i8(ta, tr, mode, x);
        }
        if (form == CINT_Q_SAT) {
            return (tr & 0xffu) == CINT_TAG_U8 ? (unsigned)cint_q_as_sat_u8(ta, tr, mode, x)
                                               : (unsigned)(uint8_t)cint_q_as_sat_i8(ta, tr, mode, x);
        }
        if (!full) return cint_q_convert(ta, tr, mode, CINT_Q_CHECKED, x, &bits) ? (uint8_t)bits : 1024u;
        if ((tr & 0xffu) == CINT_TAG_U8) {
            uint8_t u = (uint8_t)SENTINEL;
            ok = cint_q_as_u8(ctx, F_SITE, ta, tr, mode, x, &u);
            r = (int8_t)u;
        } else {
            ok = cint_q_as_i8(ctx, F_SITE, ta, tr, mode, x, &r);
        }
    }
    if (ok) return (uint8_t)r;
    st = record_status(ctx);
    if (expect_fault) ++*helper_faults;
    return r != (int8_t)SENTINEL ? 9u * 256u : st * 256u;
}

static long run_table(cint_ctx *ctx, const char *path, long *blocks, long *entries)
{
    static uint8_t body[2u * 65536u];
    uint8_t head[24];
    long bad = 0, helper_faults = 0;
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "FAIL cannot open %s\n", path);
        return 1;
    }
    while (fread(head, 1u, sizeof head, f) == sizeof head) {
        uint32_t kind = le32(head), form = le32(head + 4), mode = le32(head + 8), ta = le32(head + 12),
                 tr = le32(head + 16), count = le32(head + 20), i;
        if (count == 0u || count > 65536u || fread(body, 2u, count, f) != count) {
            fprintf(stderr, "FAIL a table block does not read\n");
            fclose(f);
            return bad + 1;
        }
        for (i = 0u; i < count; i++) {
            unsigned want = (unsigned)body[2u * i] * 256u + body[2u * i + 1u];
            unsigned got = table_entry(ctx, kind, form, mode, ta, tr, i, want, &helper_faults);
            if (got != want) {
                if (++bad <= 20) {
                    fprintf(stderr, "FAIL table kind %u form %u mode %u types %x %x entry %u: want %04x got %04x\n",
                            (unsigned)kind, (unsigned)form, (unsigned)mode, (unsigned)ta, (unsigned)tr, (unsigned)i,
                            want, got);
                }
            }
        }
        ++*blocks;
        *entries += (long)count;
    }
    fclose(f);
    if (helper_faults == 0) bad++;
    printf("exhaustive 8-bit: %ld blocks, %ld entries, %ld differ (%ld faults through the helpers)\n", *blocks,
           *entries, bad, helper_faults);
    return bad;
}

/* ------------------------------------------------------------------------- */
/* Rendering, a faulted context, and a checkpoint.                            */

#define FCHECK(cond, what)                                 \
    do {                                                   \
        if (!(cond)) {                                     \
            g_fail++;                                      \
            fprintf(stderr, "FAIL %s\n", (what));          \
        }                                                  \
    } while (0)

static void test_render(cint_ctx *ctx)
{
    static cint_fault_record rec;
    char buf[200];
    int32_t r = 7;
    int8_t r8 = 7;
    /* 32767.5 * 2 in Q16.16: exact raw 4294901760, limit Q16.16 raw 2147483647 (IM-106). */
    FCHECK(!cint_q_mul_i32(ctx, F_SITE, 16u, 5u, 2147450880, 131072, &r) && r == 7 &&
               cint_ctx_fault(ctx, &rec) == CINT_OK,
           "32767.5 * 2 faults in Q16.16 and leaves its result unwritten");
    FCHECK(rec.operand_count == 2u && cint_tvalue_render(&rec.operands[0], buf, sizeof buf) > 0u &&
               strcmp(buf, "Q16.16 raw 2147450880") == 0,
           "a Q16.16 operand renders as `Q16.16 raw 2147450880`");
    FCHECK(cint_tvalue_render_decimal(&rec.operands[1], buf, sizeof buf) > 0u && strcmp(buf, "raw 131072") == 0,
           "without its type it renders as `raw 131072`");
    FCHECK(rec.has_exact && cint_tvalue_render_decimal(&rec.exact, buf, sizeof buf) > 0u &&
               strcmp(buf, "4294901760") == 0,
           "the exact is the rounded raw value");
    FCHECK(rec.has_limit && cint_tvalue_render(&rec.limit, buf, sizeof buf) > 0u &&
               strcmp(buf, "Q16.16 raw 2147483647") == 0,
           "the limit renders as `Q16.16 raw 2147483647`");
    (void)cint_ctx_clear_fault(ctx);
    FCHECK(!cint_q_neg_i8(ctx, F_SITE, 0u, -128, &r8) && cint_ctx_fault(ctx, &rec) == CINT_OK &&
               cint_tvalue_render(&rec.operands[0], buf, sizeof buf) > 0u && strcmp(buf, "Q8.0 raw -128") == 0 &&
               cint_tvalue_render(&rec.limit, buf, sizeof buf) > 0u && strcmp(buf, "Q8.0 raw 127") == 0,
           "-MIN(Q8.0) faults with operand `Q8.0 raw -128` and limit `Q8.0 raw 127`");
    /* A helper on a faulted context returns false and writes nothing (RT-10). */
    FCHECK(!cint_q_add_i8(ctx, F_SITE, 4u, 1, 1, &r8) && r8 == 7 && !cint_q_sqrt_i8(ctx, F_SITE, 4u, 5u, 16, &r8) &&
               !cint_q_as_i8(ctx, F_SITE, CINT_TAG_I8, CINT_QT(CINT_TAG_I8, 4u), 0u, 1u, &r8) && r8 == 7,
           "helpers on a faulted context write nothing");
    FCHECK(cint_ctx_fault(ctx, &rec) == CINT_OK && rec.operand_count == 1u && rec.operands[0].bytes[0] == CINT_TAG_Q,
           "the first record stays");
    (void)cint_ctx_clear_fault(ctx);
    /* Arguments no emitted code passes: a record of E_UNSUPPORTED dispatch.admit (RT-OQ-20). */
    FCHECK(!cint_rt_fixed_fault(ctx, F_SITE, CINT_Q_ADD, 0u, CINT_QT(CINT_TAG_I8, 4u), CINT_QT(CINT_TAG_I8, 4u),
                                CINT_QT(CINT_TAG_I8, 4u), 1u, 1u) &&
               cint_ctx_fault(ctx, &rec) == CINT_OK && rec.code == CINT_E_UNSUPPORTED,
           "a fault writer called for a sum in range refuses");
    (void)cint_ctx_clear_fault(ctx);
    FCHECK(!cint_rt_fixed_fault(ctx, F_SITE, CINT_Q_MUL, 0u, CINT_QT(CINT_TAG_I8, 8u), CINT_QT(CINT_TAG_I8, 8u),
                                CINT_QT(CINT_TAG_I8, 8u), 1u, 1u) &&
               cint_ctx_fault(ctx, &rec) == CINT_OK && rec.code == CINT_E_UNSUPPORTED,
           "a fault writer called with Q0.8 (no sign bit) or no mode refuses");
    (void)cint_ctx_clear_fault(ctx);
    FCHECK(!cint_rt_fixed_fault(ctx, F_SITE, CINT_Q_ADD, 0u, CINT_QT(CINT_TAG_I8, 4u), CINT_QT(CINT_TAG_I8, 4u),
                                CINT_QT(CINT_TAG_I8, 4u), 126u, 1u) &&
               cint_ctx_fault(ctx, &rec) == CINT_OK && rec.code == CINT_E_UNSUPPORTED &&
               cint_ctx_clear_fault(ctx) == CINT_OK &&
               !cint_rt_fixed_fault(ctx, F_SITE, CINT_Q_SUB, 0u, CINT_QT(CINT_TAG_I8, 4u), CINT_QT(CINT_TAG_I8, 4u),
                                    CINT_QT(CINT_TAG_I8, 4u), (uint64_t)0 - 127u, 1u) &&
               cint_ctx_fault(ctx, &rec) == CINT_OK && rec.code == CINT_E_UNSUPPORTED,
           "a fault writer called for a result equal to a bound refuses");
    (void)cint_ctx_clear_fault(ctx);
    /* sqrt(127 / 128) is 127.498 / 128: half modes keep raw 127, ceil reaches 128. */
    FCHECK(cint_q_sqrt_i8(ctx, F_SITE, 7u, 5u, 127, &r8) && r8 == 127 &&
               !cint_rt_fixed_fault(ctx, F_SITE, CINT_Q_SQRT, 5u, CINT_QT(CINT_TAG_I8, 7u), 0u,
                                    CINT_QT(CINT_TAG_I8, 7u), 127u, 0u) &&
               cint_ctx_fault(ctx, &rec) == CINT_OK && rec.code == CINT_E_UNSUPPORTED,
           "sqrt of Q1.7 raw 127 under half_even fits, and its fault writer refuses");
    (void)cint_ctx_clear_fault(ctx);
    FCHECK(!cint_q_sqrt_i8(ctx, F_SITE, 7u, 2u, 127, &r8) && cint_ctx_fault(ctx, &rec) == CINT_OK &&
               rec.code == CINT_E_OVERFLOW && cint_tvalue_render_decimal(&rec.exact, buf, sizeof buf) > 0u &&
               strcmp(buf, "128") == 0 && cint_tvalue_render(&rec.limit, buf, sizeof buf) > 0u &&
               strcmp(buf, "Q1.7 raw 127") == 0,
           "sqrt of Q1.7 raw 127 under ceil overflows with exact 128");
    (void)cint_ctx_clear_fault(ctx);
}

/* A record with Q16.16 operands in a checkpoint: it is held, decodes, and comes
 * back from a restore into another context byte for byte. */
static const cint_site_info fs_sites[] = {{0u, 0u, NULL}, {2u, 7u, "mul.checked.q16_16.half_even"}};
static const cint_module fs_module = {"app/f.ci", 8u, 2u, fs_sites};
static const cint_module *const fs_modules[] = {&fs_module};
static const uint8_t fs_rev[32] = {0x31};
static const cint_program fs_program = {1u, 0u, fs_modules, fs_rev};
static const cint_state_var fs_vars[] = {{"n", 1u, CINT_TAG_I64, 0u, 0u, NULL, NULL}};

typedef struct fs_bytes {
    uint8_t b[8192];
    size_t n;
} fs_bytes;

static cint_status fs_write(void *user, const uint8_t *bytes, size_t len)
{
    fs_bytes *x = (fs_bytes *)user;
    if (len > sizeof x->b - x->n) return CINT_RESOURCE;
    memcpy(x->b + x->n, bytes, len);
    x->n += len;
    return CINT_OK;
}

static void test_state(void)
{
    static fs_bytes c;
    static cint_fault_record rec, back;
    static uint8_t enc1[4096], enc2[4096];
    static char text[8192];
    static uint8_t init[8];
    cint_state st = {0u, 1u, 8u, 0u, init, fs_vars};
    const cint_state *states[1];
    cint_state_table table;
    cint_checkpoint_info info;
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL, *other = NULL;
    cint_sink sink;
    size_t n1, n2;
    int32_t r = 0;
    bool made;
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = &fs_program;
    made = cint_ctx_create(&cfg, &ctx) == CINT_OK && cint_ctx_create(&cfg, &other) == CINT_OK;
    FCHECK(made, "cint_ctx_create");
    if (!made) {
        cint_ctx_destroy(ctx);
        return;
    }
    states[0] = &st;
    table.count = 1u;
    table.reserved = 0u;
    table.states = states;
    table.memory = NULL;
    FCHECK(cint_ctx_bind_state(ctx, &table) == CINT_OK && cint_ctx_bind_state(other, &table) == CINT_OK,
           "a table with one I64 binds");
    {
        const cint_site site = {0u, 1u};
        FCHECK(!cint_q_mul_i32(ctx, site, 16u, 5u, INT32_MIN, INT32_MIN, &r) && cint_ctx_fault(ctx, &rec) == CINT_OK,
               "MIN * MIN faults in Q16.16");
    }
    n1 = cint_fault_encode(&rec, enc1, sizeof enc1);
    c.n = 0u;
    sink.write = fs_write;
    sink.user = &c;
    FCHECK(n1 > 0u && cint_ctx_checkpoint(ctx, &sink) == CINT_OK && cint_checkpoint_decode(c.b, c.n, &info) == CINT_OK &&
               info.fault != NULL && info.fault_len == n1 && memcmp(info.fault, enc1, n1) == 0 &&
               cint_checkpoint_render(&info, text, sizeof text) > 0u &&
               strstr(text, "fault-operand Q16.16 raw -2147483648") != NULL &&
               strstr(text, "fault-limit Q16.16 raw 2147483647") != NULL,
           "a faulted context's checkpoint holds the record with its Q16.16 operands and renders them");
    FCHECK(cint_ctx_restore(other, c.b, c.n) == CINT_OK && cint_ctx_fault(other, &back) == CINT_OK &&
               back.operands[0].bytes[0] == CINT_TAG_Q && (n2 = cint_fault_encode(&back, enc2, sizeof enc2)) == n1 &&
               memcmp(enc1, enc2, n1) == 0,
           "the record comes back from a restore byte for byte");
    cint_ctx_destroy(other);
    cint_ctx_destroy(ctx);
}

int main(int argc, char **argv)
{
    static char line[8192];
    static char got[8192];
    static char copy[8192];
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL;
    FILE *f;
    long n = 0, blocks = 0, entries = 0;
    printf("helper mode: %s\n", CINT_RT_HELPER_MODE);
    if (argc != 3) {
        fprintf(stderr, "usage: test_fixed <cases> <table>\n");
        return 2;
    }
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = &f_program;
    if (cint_ctx_create(&cfg, &ctx) != CINT_OK) {
        fprintf(stderr, "FAIL cint_ctx_create\n");
        return 1;
    }
    f = fopen(argv[1], "rb");
    if (f == NULL) {
        fprintf(stderr, "FAIL cannot open %s\n", argv[1]);
        cint_ctx_destroy(ctx);
        return 1;
    }
    while (fgets(line, (int)sizeof line, f) != NULL) {
        const char *want = "";
        size_t len = strlen(line);
        if (len > 0u && line[len - 1u] == '\n') line[--len] = '\0';
        if (len == 0u) continue;
        memcpy(copy, line, len + 1u);
        got[0] = '\0';
        n++;
        if (!run_case(ctx, line, got, &want)) {
            g_fail++;
            if (g_fail <= 40) fprintf(stderr, "FAIL unsupported case: %.200s\n", copy);
        } else if (strcmp(got, want) != 0) {
            g_fail++;
            if (g_fail <= 40) fprintf(stderr, "FAIL %.200s\n  want %.600s\n  got  %.600s\n", copy, want, got);
        } else {
            g_pass++;
        }
    }
    fclose(f);
    printf("fixed cases: %ld, passed %ld, failed %ld\n", n, g_pass, g_fail);
    if (run_table(ctx, argv[2], &blocks, &entries) != 0) g_fail++;
    test_render(ctx);
    cint_ctx_destroy(ctx);
    test_state();
    printf("fixed checks: %ld failed\n", g_fail);
    return g_fail == 0 && n > 0 ? 0 : 1;
}
