/* test_reduce_fixed.c: the reductions over fixed-point elements and dot with
 * a rounding mode against cint_ref (rt/OPEN.md RT-OQ-57; rt/cint_fixed.h;
 * roadmap box 15).
 *
 * tests/test_cint_rt_reduce_fixed.py writes the cases, one per line: `<id>
 * <op> <elem> <result> <mode> <init> X <origin> <rank> <extent>...
 * <stride>... <count> <element>... [Y <view of the same form>] = <outcome>
 * fuel <units>`, each value the hexadecimal of its little-endian bytes. Each
 * case builds the buffers, calls cint_rt_reduce (cint_rt_dot_fixed for dot,
 * and also cint_rt_reduce when the mode is 0) and the wrapper of the result
 * type where one applies (cint_reduce_q_<S>, cint_dot_q_<S>, cint_reduce_<t>)
 * at site 1 of module `reduce.ci` (line 1, column 1), and renders each result,
 * or the canonical bytes of the fault record (cint_fault_encode), with the
 * fuel the call charged. A call whose rendering differs from the expected text
 * fails, and so does a faulting call that writes its result. Then the steps
 * of kernels, E_FUEL inside an entry and a faulted context are checked
 * directly. Prints the helper mode and the counts, and exits 1 on any failure.
 *
 * Usage: test_reduce_fixed <cases>.
 */
/* fopen and sprintf over bounded buffers, which MSVC deprecates. */
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

static const cint_site_info r_sites[] = {{0u, 0u, NULL}, {1u, 1u, "reduce"}};
static const cint_module r_module = {"reduce.ci", 9u, 2u, r_sites};
static const cint_module *const r_modules[] = {&r_module};
static const cint_program r_program = {1u, 0u, r_modules, NULL};
static const cint_site R_SITE = {0u, 1u};

#define FILL 0xA5u
#define FILL64 0xA5A5A5A5A5A5A5A5u
#define Q8_8 CINT_QT(CINT_TAG_I16, 8)
#define Q16_16 CINT_QT(CINT_TAG_I32, 16)
#define Q32_32 CINT_QT(CINT_TAG_I64, 32)
#define DCHECK(cond, what)                                    \
    do {                                                      \
        if (!(cond)) {                                        \
            g_direct++;                                       \
            fprintf(stderr, "FAIL direct: %s\n", (what));    \
        }                                                     \
    } while (0)

/* The result of any width, filled with FILL before each call. */
typedef union result_slot {
    int8_t i8;
    int16_t i16;
    int32_t i32;
    int64_t i64;
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
    uint64_t u64;
} result_slot;

/* A view's buffer: count elements, 8-aligned. */
typedef struct view_buf {
    uint64_t *mem;
    cint_vdesc v;
} view_buf;

typedef struct reduce_case {
    uint32_t op, elem, result, mode;
    bool has_init, has_ys;
    uint64_t init;
    view_buf xs, ys;
} reduce_case;

static int hexval(int ch)
{
    return ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1;
}

/* A value of `bytes` bytes from its hex, zero-extended. */
static int parse_value(const char *s, unsigned bytes, uint64_t *out)
{
    unsigned i;
    *out = 0u;
    if (strlen(s) != 2u * bytes) return 0;
    for (i = 0u; i < bytes; i++) {
        int hi = hexval(s[2 * i]), lo = hexval(s[2 * i + 1]);
        if (hi < 0 || lo < 0) return 0;
        *out |= (uint64_t)(hi * 16 + lo) << (8u * i);
    }
    return 1;
}

/* `<origin> <rank> <extent>... <stride>... <count> <element>...` */
static int parse_view(char **field, int nf, int *at, unsigned bytes, view_buf *b)
{
    long rank, k, count, i;
    if (*at + 2 > nf) return 0;
    memset(&b->v, 0, sizeof b->v);
    b->v.origin = strtoll(field[(*at)++], NULL, 10);
    rank = strtol(field[(*at)++], NULL, 10);
    if (rank < 1 || rank > CINT_MAX_RANK || *at + 2 * rank + 1 > nf) return 0;
    b->v.rank = rank;
    for (k = 0; k < rank; k++) b->v.shape[k] = strtoll(field[(*at)++], NULL, 10);
    for (k = 0; k < rank; k++) b->v.stride[k] = strtoll(field[(*at)++], NULL, 10);
    count = strtol(field[(*at)++], NULL, 10);
    if (count < 0 || count > nf - *at) return 0;
    b->mem = (uint64_t *)calloc((size_t)count + 1u, sizeof(uint64_t));
    if (b->mem == NULL) return 0;
    for (i = 0; i < count; i++) {
        uint64_t x;
        if (!parse_value(field[(*at)++], bytes, &x)) return 0;
        for (k = 0; k < (long)bytes; k++) ((uint8_t *)b->mem)[(size_t)i * bytes + (size_t)k] = (uint8_t)(x >> (8 * k));
    }
    b->v.base = b->mem;
    return 1;
}

static uint64_t slot_bits(uint32_t tag, const result_slot *o)
{
    switch (tag & 0xffu) {
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

static int unwritten(const result_slot *o)
{
    size_t i;
    for (i = 0u; i < sizeof *o; i++) {
        if (((const uint8_t *)o)[i] != FILL) return 0;
    }
    return 1;
}

/* "F <hex>" for the context's record, which is then cleared. */
static void put_fault(cint_ctx *ctx, char *dst)
{
    static const char digits[] = "0123456789abcdef";
    static cint_fault_record rec;
    static uint8_t bytes[4096];
    size_t n, i;
    if (cint_ctx_fault(ctx, &rec) != CINT_OK) {
        strcpy(dst, "F none");
        return;
    }
    n = cint_fault_encode(&rec, bytes, sizeof bytes);
    dst[0] = 'F';
    dst[1] = ' ';
    for (i = 0u; i < n; i++) {
        dst[2u + 2u * i] = digits[bytes[i] >> 4];
        dst[3u + 2u * i] = digits[bytes[i] & 15u];
    }
    dst[2u + 2u * n] = '\0';
    (void)cint_ctx_clear_fault(ctx);
}

/* The wrapper of k's result type: -1 when none applies, else whether it succeeded. With as_reduce, a
 * fixed-point dot that does not round goes through cint_reduce_q_<S>. */
static int call_wrapper(cint_ctx *ctx, const reduce_case *k, result_slot *o, bool as_reduce)
{
    uint32_t r = k->result;
    unsigned f = cint_q_frac(r);
    const cint_vdesc *xs = &k->xs.v, *ys = k->has_ys ? &k->ys.v : NULL;
    bool dot = k->op == CINT_REDUCE_DOT && !as_reduce;
    if (as_reduce && (k->op != CINT_REDUCE_DOT || !cint_q_is(r) || k->elem != r || k->mode != 0u)) return -1;
#define QW(S)                                                                                                   \
    (dot ? cint_dot_q_##S(ctx, R_SITE, k->elem, f, k->mode, xs, ys, &o->S)                                     \
         : cint_reduce_q_##S(ctx, R_SITE, k->op, f, xs, ys, k->has_init, k->init, &o->S))
#define IW(T) cint_reduce_##T(ctx, R_SITE, k->op, k->elem, xs, ys, k->has_init, k->init, &o->T)
    if (cint_q_is(r)) {
        if (!dot && k->elem != r) return -1;
        switch (r & 0xffu) {
        case CINT_TAG_I8: return QW(i8);
        case CINT_TAG_I16: return QW(i16);
        case CINT_TAG_I32: return QW(i32);
        case CINT_TAG_I64: return QW(i64);
        default: return -1;
        }
    }
    if (k->mode != 0u) return -1;
    switch (r) {
    case CINT_TAG_I8: return IW(i8);
    case CINT_TAG_I16: return IW(i16);
    case CINT_TAG_I32: return IW(i32);
    case CINT_TAG_I64: return IW(i64);
    case CINT_TAG_U8: return IW(u8);
    case CINT_TAG_U16: return IW(u16);
    case CINT_TAG_U32: return IW(u32);
    case CINT_TAG_U64: return IW(u64);
    default: return -1;
    }
#undef QW
#undef IW
}

/* Call `kind` of case k (0: cint_rt_reduce or cint_rt_dot_fixed; 1: cint_rt_reduce's dot; 2: the
 * wrapper; 3: cint_reduce_q_<S>'s dot) and render it into got; 0 when the call does not apply. */
static int render_call(cint_ctx *ctx, const reduce_case *k, int kind, char *got, int *wrote)
{
    const cint_vdesc *ys = k->has_ys ? &k->ys.v : NULL;
    result_slot o;
    uint64_t bits = FILL64;
    int64_t fuel = cint_rt_head_of(ctx)->fuel_used;
    int done;
    unsigned i, n = cint_rt_tag_width(k->result) / 8u;
    memset(&o, FILL, sizeof o);
    if (kind == 0 && k->op == CINT_REDUCE_DOT) {
        done = cint_rt_dot_fixed(ctx, R_SITE, k->elem, k->result, k->mode, &k->xs.v, ys, &bits);
    } else if (kind == 0 || kind == 1) {
        if (kind == 1 && (k->op != CINT_REDUCE_DOT || k->mode != 0u)) return 0;
        done = cint_rt_reduce(ctx, R_SITE, k->op, k->elem, k->result, &k->xs.v, ys, k->has_init, k->init, &bits);
    } else {
        done = call_wrapper(ctx, k, &o, kind == 3);
        if (done < 0) return 0;
        if (done) bits = slot_bits(k->result, &o);
    }
    fuel = cint_rt_head_of(ctx)->fuel_used - fuel;
    if (done) {
        int len = sprintf(got, "V %lu ", (unsigned long)k->result);
        for (i = 0u; i < n; i++) len += sprintf(got + len, "%02x", (unsigned)((bits >> (8u * i)) & 0xffu));
    } else {
        *wrote = bits != FILL64 || !unwritten(&o);
        put_fault(ctx, got);
    }
    sprintf(got + strlen(got), " fuel %lld", (long long)fuel);
    return 1;
}

static int parse_case(char *line, reduce_case *k, const char **want)
{
    static char *field[8192];
    int nf = 0, at = 7;
    char *p = line, *eq = strstr(line, " = ");
    unsigned bytes;
    if (eq == NULL) return 0;
    *eq = '\0';
    *want = eq + 3;
    while (*p != '\0' && nf < (int)(sizeof field / sizeof field[0])) {
        field[nf++] = p;
        while (*p != '\0' && *p != ' ') p++;
        if (*p == ' ') *p++ = '\0';
    }
    if (nf < 8 || strcmp(field[6], "X") != 0) return 0;
    k->op = (uint32_t)strtoul(field[1], NULL, 10);
    k->elem = (uint32_t)strtoul(field[2], NULL, 10);
    k->result = (uint32_t)strtoul(field[3], NULL, 10);
    k->mode = (uint32_t)strtoul(field[4], NULL, 10);
    bytes = cint_rt_tag_width(k->elem) / 8u;
    k->has_init = strcmp(field[5], "-") != 0;
    if (k->has_init && !parse_value(field[5], bytes, &k->init)) return 0;
    if (!parse_view(field, nf, &at, bytes, &k->xs)) return 0;
    if (at < nf) {
        if (strcmp(field[at++], "Y") != 0 || !parse_view(field, nf, &at, bytes, &k->ys)) return 0;
        k->has_ys = true;
    }
    return at == nf;
}

static void run_case(cint_ctx *ctx, char *line)
{
    static char copy[256];
    static char got[16384];
    reduce_case k;
    const char *want = "";
    int kind, wrote;
    snprintf(copy, sizeof copy, "%s", line);
    memset(&k, 0, sizeof k);
    if (!parse_case(line, &k, &want)) {
        g_fail++;
        if (g_fail <= 40) fprintf(stderr, "FAIL unsupported case: %.200s\n", copy);
    } else {
        int ok = 1;
        for (kind = 0; kind < 4; kind++) {
            wrote = 0;
            got[0] = '\0';
            if (!render_call(ctx, &k, kind, got, &wrote)) continue;
            if (strcmp(got, want) != 0 || wrote) {
                ok = 0;
                if (g_fail <= 40) {
                    fprintf(stderr, "FAIL %.200s (call %d)\n  want %.1200s\n  got  %.1200s%s\n", copy, kind, want, got,
                            wrote ? "\n  also: a faulting call wrote its result" : "");
                }
            }
        }
        if (ok) {
            g_pass++;
        } else {
            g_fail++;
        }
    }
    free(k.xs.mem);
    free(k.ys.mem);
}

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
    static char seq_text[4096], ker_text[4096];
    static int32_t qa[3] = {98304, -16384, 131072}; /* 1.5, -0.25 and 2.0 in Q16.16 */
    static int16_t qb[2] = {25600, 25600};          /* 100.0 in Q8.8, twice */
    static int16_t qf[2] = {767, 1};
    static int32_t many[65];
    cint_vdesc va = vd1(qa, 3, 1), vb = vd1(qb, 2, 1), vf = vd1(qf, 2, 1), vm = vd1(many, 65, 1);
    cint_zacc z;
    int32_t r32 = 0, s32 = 0;
    int16_t r16 = (int16_t)0x5A5A, acc16;
    int64_t part = 0;
    uint64_t bits;
    unsigned i;
    /* A kernel's sum: contribution by contribution, or through an I64 partial, as the sequential sum. */
    memset(&z, 0, sizeof z);
    for (i = 0u; i < 3u; i++) cint_zacc_add(&z, Q16_16, (uint64_t)(int64_t)qa[i]);
    DCHECK(cint_ksum_q_i32(ctx, R_SITE, 16u, 3, &z, &r32) && r32 == 212992 &&
               cint_reduce_q_i32(ctx, R_SITE, CINT_REDUCE_SUM, 16u, &va, NULL, false, 0u, &s32) && s32 == r32,
           "kernel sum of Q16.16 (conformance/fixed/kernel_scale_reduce)");
    memset(&z, 0, sizeof z);
    for (i = 0u; i < 3u; i++) part += qa[i];
    cint_zacc_add(&z, CINT_TAG_I64, (uint64_t)part);
    r32 = 0;
    DCHECK(cint_ksum_q_i32(ctx, R_SITE, 16u, 3, &z, &r32) && r32 == 212992, "kernel sum through an I64 partial");
    /* Its overflow writes the sequential sum's record. */
    memset(&z, 0, sizeof z);
    for (i = 0u; i < 2u; i++) cint_zacc_add(&z, Q8_8, (uint64_t)(int64_t)qb[i]);
    DCHECK(!cint_ksum_q_i16(ctx, R_SITE, 8u, 2, &z, &r16) && r16 == (int16_t)0x5A5A &&
               record_is(ctx, (uint16_t)CINT_E_OVERFLOW, "sum.checked.q8_8.q8_8"),
           "kernel sum of Q8.8 overflows (conformance/fixed/sum_overflow_q8_8)");
    put_fault(ctx, ker_text);
    DCHECK(!cint_reduce_q_i16(ctx, R_SITE, CINT_REDUCE_SUM, 8u, &vb, NULL, false, 0u, &r16), "sequential sum faults");
    put_fault(ctx, seq_text);
    DCHECK(strcmp(ker_text, seq_text) == 0, "the kernel's record is the sequential one");
    /* fold_checked(add) steps: in range, then past MAX, keeping the accumulator, with the sequential
     * fold's record. */
    acc16 = 32000;
    DCHECK(cint_kfold_q_i16(ctx, R_SITE, 8u, 0, &acc16, 767) && acc16 == 32767, "fold step to MAX of Q8.8");
    DCHECK(!cint_kfold_q_i16(ctx, R_SITE, 8u, 1, &acc16, 1) && acc16 == 32767 &&
               record_is(ctx, (uint16_t)CINT_E_OVERFLOW, "fold_checked.add.q8_8"),
           "fold step past MAX of Q8.8");
    put_fault(ctx, ker_text);
    DCHECK(!cint_reduce_q_i16(ctx, R_SITE, CINT_REDUCE_FOLD_ADD, 8u, &vf, NULL, true, 32000u, &r16),
           "sequential fold faults");
    put_fault(ctx, seq_text);
    DCHECK(strcmp(ker_text, seq_text) == 0, "the kernel fold's record is the sequential one");
    bits = 1u;
    DCHECK(!cint_rt_fold_step(ctx, R_SITE, CINT_RT_OP_MUL, Q16_16, 0, &bits, 1u) &&
               record_is(ctx, (uint16_t)CINT_E_UNSUPPORTED, "dispatch.admit"),
           "a fold_checked(mul) step on fixed point is refused");
    (void)cint_ctx_clear_fault(ctx);
    /* min and max of no element. */
    DCHECK(!cint_rt_fault_reduce_empty(ctx, R_SITE, false, Q16_16) &&
               record_is(ctx, (uint16_t)CINT_E_SHAPE, "reduce_min.checked.q16_16"),
           "an empty min of Q16.16");
    (void)cint_ctx_clear_fault(ctx);
    DCHECK(!cint_rt_fault_reduce_empty(ctx, R_SITE, true, CINT_QT(CINT_TAG_I64, 63)) &&
               record_is(ctx, (uint16_t)CINT_E_SHAPE, "reduce_max.checked.q1_63"),
           "an empty max of Q1.63");
    (void)cint_ctx_clear_fault(ctx);
    DCHECK(!cint_rt_fault_reduce_empty(ctx, R_SITE, true, CINT_QT(CINT_TAG_U16, 4)) &&
               record_is(ctx, (uint16_t)CINT_E_UNSUPPORTED, "dispatch.admit"),
           "an empty max of a fixed-point word with U16 storage is refused");
    (void)cint_ctx_clear_fault(ctx);
    /* cint_rt_zacc_fit's admission: a fixed-point sum keeps its format, and a dot that rounds needs its mode. */
    memset(&z, 0, sizeof z);
    z.limb[0] = 3u;
    bits = 0u;
    DCHECK(cint_rt_zacc_fit(ctx, R_SITE, "dot", Q16_16, Q32_32, 2, &z, &bits) && bits == 3u,
           "a fixed-point dot that does not round");
    DCHECK(!cint_rt_zacc_fit(ctx, R_SITE, "dot", Q16_16, Q16_16, 2, &z, &bits) &&
               record_is(ctx, (uint16_t)CINT_E_UNSUPPORTED, "dispatch.admit"),
           "a fixed-point dot that rounds is cint_rt_dot_fixed's");
    (void)cint_ctx_clear_fault(ctx);
    DCHECK(!cint_rt_zacc_fit(ctx, R_SITE, "sum", Q16_16, Q32_32, 2, &z, &bits) &&
               record_is(ctx, (uint16_t)CINT_E_UNSUPPORTED, "dispatch.admit"),
           "a fixed-point sum into another format");
    (void)cint_ctx_clear_fault(ctx);
    DCHECK(!cint_rt_zacc_fit(ctx, R_SITE, "sum", Q16_16, CINT_TAG_I64, 2, &z, &bits) &&
               record_is(ctx, (uint16_t)CINT_E_UNSUPPORTED, "dispatch.admit"),
           "a fixed-point sum into an integer");
    (void)cint_ctx_clear_fault(ctx);
    /* Fuel: ceil(65 / 64) units for dot inside an entry, and E_FUEL when they do not fit. */
    memset(many, 0, sizeof many);
    r32 = 0x5A5A5A5A;
    DCHECK(cint_rt_entry_begin(ctx, R_SITE, 3, 8) == CINT_OK &&
               cint_dot_q_i32(ctx, R_SITE, Q16_16, 16u, 5u, &vm, &vm, &r32) && r32 == 0 &&
               cint_rt_entry_end(ctx) == CINT_OK,
           "65 products in 2 units, with the entry's own");
    r32 = 0x5A5A5A5A;
    DCHECK(cint_rt_entry_begin(ctx, R_SITE, 2, 8) == CINT_OK &&
               !cint_dot_q_i32(ctx, R_SITE, Q16_16, 16u, 5u, &vm, &vm, &r32) && cint_rt_entry_end(ctx) == CINT_FAULT &&
               record_is(ctx, (uint16_t)CINT_E_FUEL, "fuel.charge") && r32 == 0x5A5A5A5A,
           "65 products in 1 unit: E_FUEL");
    (void)cint_ctx_clear_fault(ctx);
    /* A context that holds a fault keeps it, and the results stay unwritten. */
    DCHECK(!cint_rt_dot_fixed(ctx, R_SITE, Q16_16, Q16_16, 0u, &va, &va, &bits), "a dot that rounds needs a mode");
    bits = FILL64;
    r32 = 0x5A5A5A5A;
    acc16 = 7;
    memset(&z, 0, sizeof z);
    DCHECK(!cint_rt_dot_fixed(ctx, R_SITE, Q16_16, Q16_16, 5u, &va, &va, &bits) &&
               !cint_reduce_q_i32(ctx, R_SITE, CINT_REDUCE_MAX, 16u, &va, NULL, false, 0u, &r32) &&
               !cint_ksum_q_i32(ctx, R_SITE, 16u, 0, &z, &r32) && !cint_kfold_q_i16(ctx, R_SITE, 8u, 0, &acc16, 1) &&
               record_is(ctx, (uint16_t)CINT_E_UNSUPPORTED, "dispatch.admit") && bits == FILL64 &&
               r32 == 0x5A5A5A5A && acc16 == 7,
           "a faulted context");
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
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL;
    char *text, *line, *next;
    long n = 0;
    printf("helper mode: %s\n", CINT_RT_HELPER_MODE);
    if (argc != 2) {
        fprintf(stderr, "usage: test_reduce_fixed <cases>\n");
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
        next = strchr(line, '\n');
        if (next != NULL) {
            *next++ = '\0';
        } else {
            next = line + strlen(line);
        }
        if (line[0] == '\0') continue;
        n++;
        run_case(ctx, line);
    }
    free(text);
    test_direct(ctx);
    cint_ctx_destroy(ctx);
    printf("reduce cases: %ld, passed %ld, failed %ld\n", n, g_pass, g_fail);
    printf("direct checks: failed %ld\n", g_direct);
    return g_fail == 0 && g_direct == 0 && n > 0 ? 0 : 1;
}
