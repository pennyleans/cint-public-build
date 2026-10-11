/* test_reduce_wide.c: the reductions of rt/cint_wide.h with a wide side
 * against cint_ref (rt/OPEN.md RT-OQ-54; roadmap box 15).
 *
 * tests/test_cint_rt_reduce_wide.py writes the cases, one per line: `<id>
 * <op> <elem> <result> <init> X <origin> <rank> <extent>... <stride>...
 * <count> <element>... [Y <view of the same form>] = <outcome> fuel <units>`,
 * each value the hexadecimal of its little-endian
 * bytes. Each case builds the buffers (a wide element as its struct of limbs,
 * a narrow one as its C object), calls cint_rt_reduce_wide at site 1 of
 * module `reduce.ci` (line 1, column 1), and renders the result, or the
 * canonical bytes of the fault record (cint_fault_encode), with the fuel the
 * call charged. A case whose rendering differs from the expected text fails,
 * and so does a faulting call that writes its result. Then the steps of
 * kernels, E_FUEL inside an entry and a faulted context are checked directly.
 * Prints the helper mode and the counts, and exits 1 on any failure.
 *
 * Usage: test_reduce_wide <cases>.
 */
/* fopen and sprintf over bounded buffers, which MSVC deprecates. */
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS 1
#endif
#include "cint_rt.h"
#include "cint_rt_host.h"
#include "cint_rt_internal.h"
#include "cint_wide.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static long g_pass;
static long g_fail;
static long g_direct;

static const cint_site_info r_sites[] = {{0u, 0u, NULL}, {1u, 1u, "reduce"}};
static const cint_module r_module = {"reduce.ci", 9u, 2u, r_sites};
static const cint_module *const r_modules[] = {&r_module};
static const cint_program r_program = {1u, 0u, r_modules, NULL};
static const cint_site R_SITE = {0u, 1u};

#define FILL 0xA5u
#define DCHECK(cond, what)                                    \
    do {                                                      \
        if (!(cond)) {                                        \
            g_direct++;                                       \
            fprintf(stderr, "FAIL direct: %s\n", (what));    \
        }                                                     \
    } while (0)

/* The result of any type, filled with FILL before each call. */
typedef union result_slot {
    int8_t i8;
    int16_t i16;
    int32_t i32;
    int64_t i64;
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
    uint64_t u64;
    uint64_t limb[16];
} result_slot;

/* A view's buffer: count elements of `size` bytes, 8-aligned. */
typedef struct view_buf {
    uint64_t *mem;
    cint_vdesc v;
} view_buf;

static unsigned tag_bytes(uint32_t tag)
{
    return cint_rt_tag_width(tag) / 8u;
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
 * zero-extended pattern in limb[0]. */
static int parse_value(const char *s, uint32_t tag, uint64_t *limb)
{
    uint8_t bytes[128];
    unsigned n = tag_bytes(tag), i;
    if (n == 0u || n > sizeof bytes || !parse_hex(s, bytes, n)) return 0;
    memset(limb, 0, 16u * sizeof limb[0]);
    if (cint_wide_limbs(tag) != 0u) {
        cint_wide_load(limb, bytes, n / 8u);
    } else {
        for (i = 0u; i < n; i++) limb[0] |= (uint64_t)bytes[i] << (8u * i);
    }
    return 1;
}

/* Reads `<count> <element>...` into a buffer whose element i is the C object
 * of tag (the struct of limbs for a wide tag). */
static int parse_buf(char **field, int nf, int *at, uint32_t tag, view_buf *b)
{
    unsigned size = tag_bytes(tag);
    long count, i;
    if (*at >= nf) return 0;
    count = strtol(field[(*at)++], NULL, 10);
    if (count < 0 || count > nf - *at) return 0;
    b->mem = (uint64_t *)calloc((size_t)count * size / 8u + 1u, sizeof(uint64_t));
    if (b->mem == NULL) return 0;
    for (i = 0; i < count; i++) {
        uint64_t limb[16];
        if (!parse_value(field[(*at)++], tag, limb)) return 0;
        if (cint_wide_limbs(tag) != 0u) {
            memcpy(b->mem + (size_t)i * (size / 8u), limb, size);
        } else {
            unsigned k;
            for (k = 0u; k < size; k++) ((uint8_t *)b->mem)[(size_t)i * size + k] = (uint8_t)(limb[0] >> (8u * k));
        }
    }
    b->v.base = b->mem;
    return 1;
}

/* `<origin> <rank> <extent>... <stride>... <count> <element>...` */
static int parse_view(char **field, int nf, int *at, uint32_t tag, view_buf *b)
{
    long rank, k;
    if (*at + 2 > nf) return 0;
    memset(&b->v, 0, sizeof b->v);
    b->v.origin = strtoll(field[(*at)++], NULL, 10);
    rank = strtol(field[(*at)++], NULL, 10);
    if (rank < 1 || rank > CINT_MAX_RANK || *at + 2 * rank > nf) return 0;
    b->v.rank = rank;
    for (k = 0; k < rank; k++) b->v.shape[k] = strtoll(field[(*at)++], NULL, 10);
    for (k = 0; k < rank; k++) b->v.stride[k] = strtoll(field[(*at)++], NULL, 10);
    return parse_buf(field, nf, at, tag, b);
}

static uint64_t narrow_bits(uint32_t tag, const result_slot *o)
{
    switch (tag) {
    case CINT_TAG_I8: return (uint64_t)(int64_t)o->i8;
    case CINT_TAG_I16: return (uint64_t)(int64_t)o->i16;
    case CINT_TAG_I32: return (uint64_t)(int64_t)o->i32;
    case CINT_TAG_I64: return (uint64_t)o->i64;
    case CINT_TAG_U8: return o->u8;
    case CINT_TAG_U16: return o->u16;
    case CINT_TAG_U32: return o->u32;
    default: return o->u64;
    }
}

/* "V <tag> <hex>" for a result of type tag. */
static void put_result(char *dst, uint32_t tag, const result_slot *o)
{
    uint8_t bytes[128];
    unsigned n = tag_bytes(tag), i;
    int len = sprintf(dst, "V %u ", (unsigned)tag);
    if (cint_wide_limbs(tag) != 0u) {
        cint_wide_store(bytes, o->limb, n / 8u);
    } else {
        uint64_t bits = narrow_bits(tag, o);
        for (i = 0u; i < n; i++) bytes[i] = (uint8_t)(bits >> (8u * i));
    }
    put_hex(dst + len, bytes, n);
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

static int unwritten(const result_slot *o)
{
    size_t i;
    for (i = 0u; i < sizeof *o; i++) {
        if (((const uint8_t *)o)[i] != FILL) return 0;
    }
    return 1;
}

static int run_case(cint_ctx *ctx, char *line, char *got, const char **want, int *wrote)
{
    static char *field[8192];
    int nf = 0, at = 6, ok = 0;
    char *p = line, *eq = strstr(line, " = ");
    unsigned long op, elem, result;
    uint64_t init[16];
    int has_init;
    view_buf xs, ys;
    result_slot out;
    int64_t fuel;
    bool done;
    if (eq == NULL) return 0;
    *eq = '\0';
    *want = eq + 3;
    while (*p != '\0' && nf < (int)(sizeof field / sizeof field[0])) {
        field[nf++] = p;
        while (*p != '\0' && *p != ' ') p++;
        if (*p == ' ') *p++ = '\0';
    }
    if (nf < 7 || strcmp(field[5], "X") != 0) return 0;
    op = strtoul(field[1], NULL, 10);
    elem = strtoul(field[2], NULL, 10);
    result = strtoul(field[3], NULL, 10);
    has_init = strcmp(field[4], "-") != 0;
    memset(init, 0, sizeof init);
    if (has_init && !parse_value(field[4], (uint32_t)elem, init)) return 0;
    memset(&xs, 0, sizeof xs);
    memset(&ys, 0, sizeof ys);
    if (parse_view(field, nf, &at, (uint32_t)elem, &xs) &&
        (at == nf || (strcmp(field[at++], "Y") == 0 && parse_view(field, nf, &at, (uint32_t)elem, &ys))) && at == nf) {
        memset(&out, FILL, sizeof out);
        fuel = cint_rt_head_of(ctx)->fuel_used;
        done = cint_rt_reduce_wide(ctx, R_SITE, (uint32_t)op, (uint32_t)elem, (uint32_t)result, &xs.v,
                                   ys.mem != NULL ? &ys.v : NULL, has_init ? init : NULL, &out);
        fuel = cint_rt_head_of(ctx)->fuel_used - fuel;
        if (done) {
            put_result(got, (uint32_t)result, &out);
        } else {
            *wrote = !unwritten(&out);
            put_fault(ctx, got);
        }
        sprintf(got + strlen(got), " fuel %lld", (long long)fuel);
        ok = 1;
    }
    free(xs.mem);
    free(ys.mem);
    return ok;
}

/* A view of n elements of stride s over the I1024 values at base. */
static cint_vdesc vd1(void *base, int64_t n, int64_t s)
{
    cint_vdesc v;
    memset(&v, 0, sizeof v);
    v.base = base;
    v.rank = 1;
    v.shape[0] = n;
    v.stride[0] = s;
    return v;
}

static int record_is(cint_ctx *ctx, uint16_t code, const char *operation)
{
    static cint_fault_record rec;
    size_t n = strlen(operation);
    return cint_ctx_fault(ctx, &rec) == CINT_OK && rec.code == code && rec.operation_len == n &&
           memcmp(rec.operation, operation, n) == 0;
}

/* The steps of kernels, fuel and a faulted context. */
static void test_direct(cint_ctx *ctx)
{
    static cint_i1024 xs[5], ys[5];
    cint_wzacc za, zb;
    cint_i1024 a, b;
    cint_i256 acc, one;
    result_slot r1, r2;
    cint_vdesc vx = vd1(xs, 5, 1), vy = vd1(ys, 5, 1);
    unsigned i, k;
    int64_t sum = 0;
    for (i = 0u; i < 5u; i++) {
        for (k = 0u; k < 16u; k++) {
            xs[i].limb[k] = i % 2u == 0u ? 0u : UINT64_MAX;
            ys[i].limb[k] = 0u;
        }
        xs[i].limb[0] = i % 2u == 0u ? (uint64_t)(i * 1000u + 7u) : (uint64_t)0 - (uint64_t)(i * 333u);
        ys[i].limb[0] = (uint64_t)(i + 2u);
        sum += i % 2u == 0u ? (int64_t)(i * 1000u + 7u) : -(int64_t)(i * 333u);
    }
    /* A kernel's sum and dot, contribution by contribution, agree with the reduction. */
    memset(&za, 0, sizeof za);
    memset(&zb, 0, sizeof zb);
    for (i = 0u; i < 5u; i++) {
        cint_wzacc_add(&za, CINT_TAG_I1024, xs[i].limb);
        cint_wzacc_add_product(&zb, CINT_TAG_I1024, xs[i].limb, ys[i].limb);
    }
    memset(&r1, FILL, sizeof r1);
    memset(&r2, FILL, sizeof r2);
    DCHECK(cint_rt_wzacc_fit(ctx, R_SITE, "sum", CINT_TAG_I1024, CINT_TAG_I64, 5, &za, &r1) && r1.i64 == sum &&
               cint_rt_reduce_wide(ctx, R_SITE, CINT_REDUCE_SUM, CINT_TAG_I1024, CINT_TAG_I64, &vx, NULL, NULL, &r2) &&
               r2.i64 == sum,
           "kernel sum of I1024 into I64");
    memset(&r1, FILL, sizeof r1);
    memset(&r2, FILL, sizeof r2);
    DCHECK(cint_rt_wzacc_fit(ctx, R_SITE, "dot", CINT_TAG_I1024, CINT_TAG_I512, 5, &zb, &r1) &&
               cint_rt_reduce_wide(ctx, R_SITE, CINT_REDUCE_DOT, CINT_TAG_I1024, CINT_TAG_I512, &vx, &vy, NULL, &r2) &&
               memcmp(r1.limb, r2.limb, 64u) == 0 && r1.limb[8] == 0xA5A5A5A5A5A5A5A5u,
           "kernel dot of I1024 into I512 writes 8 limbs");
    {
        /* A narrow contribution is read as its type: the I8 pattern 0xFF is -1, the U8 pattern 2^64 - 1 is 255. */
        uint64_t m1 = 0xFFu, all = UINT64_MAX;
        memset(&za, 0, sizeof za);
        memset(&zb, 0, sizeof zb);
        cint_wzacc_add(&za, CINT_TAG_I8, &m1);
        cint_wzacc_add(&zb, CINT_TAG_U8, &all);
        DCHECK(cint_rt_wzacc_fit(ctx, R_SITE, "sum", CINT_TAG_I8, CINT_TAG_I128, 1, &za, &r1) &&
                   r1.limb[0] == UINT64_MAX && r1.limb[1] == UINT64_MAX,
               "an I8 contribution of pattern 0xFF is -1");
        DCHECK(zb.limb[0] == 255u && zb.limb[1] == 0u, "a U8 contribution of pattern 2^64 - 1 is 255");
        memset(&zb, 0, sizeof zb);
        cint_wzacc_add_product(&zb, CINT_TAG_U8, &all, &m1);
        DCHECK(zb.limb[0] == 255u * 255u && zb.limb[1] == 0u && zb.limb[2] == 0u, "a U8 product of the same");
    }
    DCHECK(!cint_rt_wzacc_fit(ctx, R_SITE, "mul", CINT_TAG_I1024, CINT_TAG_I64, 5, &za, &r1) &&
               record_is(ctx, (uint16_t)CINT_E_UNSUPPORTED, "dispatch.admit"),
           "a fit other than sum and dot is refused");
    (void)cint_ctx_clear_fault(ctx);
    /* fold_checked steps on I256. */
    memset(&acc, 0xFF, sizeof acc);
    acc.limb[3] = UINT64_MAX >> 1;
    acc.limb[0] = UINT64_MAX - 1u;  /* MAX - 1 */
    memset(&one, 0, sizeof one);
    one.limb[0] = 1u;
    DCHECK(cint_rt_wide_fold_step(ctx, R_SITE, CINT_RT_OP_ADD, CINT_TAG_I256, 0, acc.limb, one.limb) &&
               acc.limb[0] == UINT64_MAX,
           "fold step to MAX");
    DCHECK(!cint_rt_wide_fold_step(ctx, R_SITE, CINT_RT_OP_ADD, CINT_TAG_I256, 4, acc.limb, one.limb) &&
               record_is(ctx, (uint16_t)CINT_E_OVERFLOW, "fold_checked.add.i256") && acc.limb[0] == UINT64_MAX,
           "fold step past MAX faults and keeps the accumulator");
    (void)cint_ctx_clear_fault(ctx);
    DCHECK(!cint_rt_wide_fold_step(ctx, R_SITE, CINT_RT_OP_SUB, CINT_TAG_I256, 0, acc.limb, one.limb) &&
               record_is(ctx, (uint16_t)CINT_E_UNSUPPORTED, "dispatch.admit"),
           "a fold step other than add and mul is refused");
    (void)cint_ctx_clear_fault(ctx);
    DCHECK(!cint_rt_wide_fold_step(ctx, R_SITE, CINT_RT_OP_ADD, CINT_TAG_I64, 0, acc.limb, one.limb) &&
               record_is(ctx, (uint16_t)CINT_E_UNSUPPORTED, "dispatch.admit"),
           "a narrow fold step is cint_rt_fold_step's");
    (void)cint_ctx_clear_fault(ctx);
    DCHECK(!cint_rt_fault_reduce_empty(ctx, R_SITE, true, CINT_TAG_U512) &&
               record_is(ctx, (uint16_t)CINT_E_SHAPE, "reduce_max.checked.u512"),
           "an empty max of U512");
    (void)cint_ctx_clear_fault(ctx);
    /* Fuel: ceil(N / 64) units inside an entry, and E_FUEL when they do not fit. */
    {
        static cint_i128 many[65];
        cint_vdesc v = vd1(many, 65, 1);
        memset(many, 0, sizeof many);
        memset(&r1, FILL, sizeof r1);
        DCHECK(cint_rt_entry_begin(ctx, R_SITE, 3, 8) == CINT_OK &&
                   cint_rt_reduce_wide(ctx, R_SITE, CINT_REDUCE_SUM, CINT_TAG_I128, CINT_TAG_I128, &v, NULL, NULL, &r1) &&
                   cint_rt_entry_end(ctx) == CINT_OK,
               "65 elements in 2 units, with the entry's own");
        memset(&r1, FILL, sizeof r1);
        DCHECK(cint_rt_entry_begin(ctx, R_SITE, 2, 8) == CINT_OK &&
                   !cint_rt_reduce_wide(ctx, R_SITE, CINT_REDUCE_SUM, CINT_TAG_I128, CINT_TAG_I128, &v, NULL, NULL,
                                        &r1) &&
                   cint_rt_entry_end(ctx) == CINT_FAULT && record_is(ctx, (uint16_t)CINT_E_FUEL, "fuel.charge") &&
                   unwritten(&r1),
               "65 elements in 1 unit: E_FUEL");
        (void)cint_ctx_clear_fault(ctx);
    }
    /* A context that holds a fault keeps it, and the result stays unwritten. */
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    DCHECK(!cint_rt_reduce_wide(ctx, R_SITE, CINT_REDUCE_COUNT, CINT_TAG_I1024, CINT_TAG_I64, &vx, NULL, NULL, &r1),
           "count is refused");
    memset(&r1, FILL, sizeof r1);
    DCHECK(!cint_rt_reduce_wide(ctx, R_SITE, CINT_REDUCE_SUM, CINT_TAG_I1024, CINT_TAG_I1024, &vx, NULL, NULL, &r1) &&
               !cint_rt_wide_fold_step(ctx, R_SITE, CINT_RT_OP_ADD, CINT_TAG_I1024, 0, a.limb, b.limb) &&
               !cint_rt_wzacc_fit(ctx, R_SITE, "sum", CINT_TAG_I1024, CINT_TAG_I1024, 5, &za, &r1) &&
               record_is(ctx, (uint16_t)CINT_E_UNSUPPORTED, "dispatch.admit") && unwritten(&r1),
           "a faulted context");
    vx.shape[0] = 0;
    DCHECK(!cint_rt_reduce_wide(ctx, R_SITE, CINT_REDUCE_SUM_WRAP, CINT_TAG_I1024, CINT_TAG_I1024, &vx, NULL, NULL,
                                &r1) &&
               unwritten(&r1),
           "a faulted context, with no elements to charge for");
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
    static char got[16384];
    static char copy[256];
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL;
    char *text, *line, *next;
    long n = 0;
    printf("helper mode: %s\n", CINT_RT_HELPER_MODE);
    if (argc != 2) {
        fprintf(stderr, "usage: test_reduce_wide <cases>\n");
        return 2;
    }
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = &r_program;
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
        size_t len;
        next = strchr(line, '\n');
        if (next != NULL) {
            *next++ = '\0';
        } else {
            next = line + strlen(line);
        }
        len = strlen(line);
        if (len == 0u) continue;
        snprintf(copy, sizeof copy, "%s", line);
        got[0] = '\0';
        n++;
        if (!run_case(ctx, line, got, &want, &wrote)) {
            g_fail++;
            if (g_fail <= 40) fprintf(stderr, "FAIL unsupported case: %.200s\n", copy);
        } else if (strcmp(got, want) != 0 || wrote) {
            g_fail++;
            if (g_fail <= 40) {
                fprintf(stderr, "FAIL %.200s\n  want %.1200s\n  got  %.1200s%s\n", copy, want, got,
                        wrote ? "\n  also: a faulting call wrote its result" : "");
            }
        } else {
            g_pass++;
        }
    }
    free(text);
    test_direct(ctx);
    cint_ctx_destroy(ctx);
    printf("reduce cases: %ld, passed %ld, failed %ld\n", n, g_pass, g_fail);
    printf("direct checks: failed %ld\n", g_direct);
    return g_fail == 0 && g_direct == 0 && n > 0 ? 0 : 1;
}
