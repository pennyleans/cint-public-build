/* cint_rt.c: the cint_rt library for cint-rt-3 (rt/cint_rt.h).
 *
 * Contexts and entries (SPEC-01 9.4, SPEC-03 A-2 to A-11), fuel (SPEC-01
 * 10.2), call depth, canonical fault records (SPEC-01 9.2, 11.1, 11.2), site
 * resolution (SPEC-09 EMIT-23), the rendering of tagged values, the minimal
 * buffer registry and view binding (SPEC-03 A-8, A-12), staged print output
 * (SPEC-04 LS-193 to LS-197), error results (SPEC-03 A-18), and
 * cint_program_run (SPEC-06 3.4a).
 *
 * Every integer computation here is on unsigned 64-bit limbs or on signed
 * values proven in range; no binary floating-point type appears (SPEC-09
 * EMIT-01). The busy flag (SPEC-03 A-11) is confined to this file: it uses
 * _InterlockedExchange on MSVC and C17 atomic_exchange elsewhere.
 * cint_program_run is the only code here that calls the host's file API; it
 * runs in a program process, never in generated code (SPEC-03 A-3).
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L /* open, write, close, O_CLOEXEC */
#endif

#include "cint_rt_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif

/* The largest print statement the staging buffer holds (1 GiB). */
#define PRINT_MAX ((size_t)1 << 30)

/* ------------------------------------------------------------------------- */
/* Type tags.                                                                 */

static bool is_scalar_tag(uint32_t tag)
{
    return (tag >= CINT_TAG_I8 && tag <= CINT_TAG_I64) || (tag >= CINT_TAG_U8 && tag <= CINT_TAG_U64);
}

const char *cint_rt_type_ident(uint32_t tag)
{
    static const char *const signed_names[4] = {"i8", "i16", "i32", "i64"};
    static const char *const unsigned_names[4] = {"u8", "u16", "u32", "u64"};
    if (!is_scalar_tag(tag)) {
        return NULL;
    }
    return cint_rt_tag_signed(tag) ? signed_names[(tag & 0x0fu) - 1u] : unsigned_names[(tag & 0x0fu) - 1u];
}

const char *cint_rt_type_name(uint32_t tag)
{
    static const char *const signed_names[4] = {"I8", "I16", "I32", "I64"};
    static const char *const unsigned_names[4] = {"U8", "U16", "U32", "U64"};
    if (!is_scalar_tag(tag)) {
        return NULL;
    }
    return cint_rt_tag_signed(tag) ? signed_names[(tag & 0x0fu) - 1u] : unsigned_names[(tag & 0x0fu) - 1u];
}

/* ------------------------------------------------------------------------- */
/* Exact integers below 2^128 in magnitude.                                   */

static cint_rt_z128 z_make(bool negative, uint64_t hi, uint64_t lo)
{
    cint_rt_z128 z;
    z.lo = lo;
    z.hi = hi;
    z.negative = negative && (lo != 0u || hi != 0u);
    return z;
}

cint_rt_z128 cint_rt_z_from_bits(uint32_t tag, uint64_t bits)
{
    if (cint_rt_tag_signed(tag) && (bits >> 63) != 0u) {
        return z_make(true, 0u, (uint64_t)0 - bits);
    }
    return z_make(false, 0u, bits);
}

cint_rt_z128 cint_rt_z_neg(cint_rt_z128 x)
{
    return z_make(!x.negative, x.hi, x.lo);
}

/* Compares magnitudes. */
static int mag_cmp(cint_rt_z128 x, cint_rt_z128 y)
{
    if (x.hi != y.hi) {
        return x.hi < y.hi ? -1 : 1;
    }
    if (x.lo != y.lo) {
        return x.lo < y.lo ? -1 : 1;
    }
    return 0;
}

cint_rt_z128 cint_rt_z_add(cint_rt_z128 x, cint_rt_z128 y)
{
    uint64_t lo, hi;
    if (x.negative == y.negative) {
        lo = x.lo + y.lo;
        hi = x.hi + y.hi + (lo < x.lo ? 1u : 0u);
        return z_make(x.negative, hi, lo);
    }
    if (mag_cmp(x, y) < 0) {
        cint_rt_z128 t = x;
        x = y;
        y = t;
    }
    /* |x| >= |y|: the result has the sign of x and magnitude |x| - |y|. */
    lo = x.lo - y.lo;
    hi = x.hi - y.hi - (x.lo < y.lo ? 1u : 0u);
    return z_make(x.negative, hi, lo);
}

cint_rt_z128 cint_rt_z_mul(cint_rt_z128 x, cint_rt_z128 y)
{
    /* 64 x 64 -> 128 bits by 32-bit halves. */
    uint64_t a0 = x.lo & 0xFFFFFFFFu, a1 = x.lo >> 32;
    uint64_t b0 = y.lo & 0xFFFFFFFFu, b1 = y.lo >> 32;
    uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    uint64_t mid = (p00 >> 32) + (p01 & 0xFFFFFFFFu) + (p10 & 0xFFFFFFFFu);
    uint64_t lo = (p00 & 0xFFFFFFFFu) | (mid << 32);
    uint64_t hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
    return z_make(x.negative != y.negative, hi, lo);
}

cint_rt_z128 cint_rt_z_shl(cint_rt_z128 x, unsigned k)
{
    uint64_t hi = k == 0u ? 0u : x.lo >> (64u - k);
    return z_make(x.negative, hi, x.lo << k);
}

int cint_rt_z_cmp(cint_rt_z128 x, cint_rt_z128 y)
{
    if (x.negative != y.negative) {
        return x.negative ? -1 : 1;
    }
    return x.negative ? -mag_cmp(x, y) : mag_cmp(x, y);
}

cint_rt_z128 cint_rt_z_min(uint32_t tag)
{
    unsigned w = cint_rt_tag_width(tag);
    if (!cint_rt_tag_signed(tag)) {
        return z_make(false, 0u, 0u);
    }
    return z_make(true, 0u, (uint64_t)1 << (w - 1u));
}

cint_rt_z128 cint_rt_z_max(uint32_t tag)
{
    unsigned w = cint_rt_tag_width(tag);
    if (!cint_rt_tag_signed(tag)) {
        return z_make(false, 0u, cint_rt_mask(w));
    }
    return z_make(false, 0u, ((uint64_t)1 << (w - 1u)) - 1u);
}

/* The 64-bit pattern of a value known to lie in the range of `tag`. */
static uint64_t z_bits(cint_rt_z128 z)
{
    return z.negative ? (uint64_t)0 - z.lo : z.lo;
}

/* ------------------------------------------------------------------------- */
/* Canonical tagged values (SPEC-01 11.1).                                    */

void cint_rt_tvalue_scalar(cint_tvalue *v, uint32_t tag, uint64_t bits)
{
    unsigned n = cint_rt_tag_width(tag) / 8u;
    unsigned i;
    memset(v, 0, sizeof *v);
    v->bytes[0] = (uint8_t)tag;
    for (i = 0; i < n; i++) {
        v->bytes[1u + i] = (uint8_t)(bits >> (8u * i));
    }
    v->len = (uint16_t)(1u + n);
}

/* Z: U32 byte count n, then n bytes of minimal little-endian two's complement;
 * zero has n = 0; no redundant sign byte. b holds the value in n <= 33 bytes. */
static void tvalue_zbytes(cint_tvalue *v, const uint8_t *b, unsigned n)
{
    while (n > 0u) {
        uint8_t top = b[n - 1u];
        bool below_negative = n >= 2u && (b[n - 2u] & 0x80u) != 0u;
        if (top == 0x00u && !below_negative) {
            n--;
        } else if (top == 0xFFu && n >= 2u && below_negative) {
            n--;
        } else {
            break;
        }
    }
    memset(v, 0, sizeof *v);
    v->bytes[0] = CINT_TAG_Z;
    v->bytes[1] = (uint8_t)n;  /* n <= 33: the upper three length bytes are 0 */
    memcpy(v->bytes + 5, b, n);
    v->len = (uint16_t)(5u + n);
}

void cint_rt_tvalue_z(cint_tvalue *v, cint_rt_z128 z)
{
    uint8_t b[17];
    unsigned i;
    for (i = 0; i < 8u; i++) {
        b[i] = (uint8_t)(z.lo >> (8u * i));
        b[8u + i] = (uint8_t)(z.hi >> (8u * i));
    }
    b[16] = 0u;
    if (z.negative) {
        unsigned carry = 1u;
        for (i = 0; i < 17u; i++) {
            unsigned t = (unsigned)(uint8_t)~b[i] + carry;
            b[i] = (uint8_t)t;
            carry = t >> 8;
        }
    }
    tvalue_zbytes(v, b, 17u);
}

/* A Z from a 256-bit two's complement integer (a cint_zacc). */
static void tvalue_z256(cint_tvalue *v, const cint_zacc *a)
{
    uint8_t b[32];
    unsigned i;
    for (i = 0; i < 32u; i++) {
        b[i] = (uint8_t)(a->limb[i / 8u] >> (8u * (i % 8u)));
    }
    tvalue_zbytes(v, b, 32u);
}

/* ------------------------------------------------------------------------- */
/* Fault records.                                                             */

cint_fault_record *cint_rt_record_begin(cint_ctx *ctx, cint_site site, uint16_t code)
{
    cint_fault_record *rec = &ctx->fault;
    if (ctx->head.faulted != 0u) {
        return NULL;
    }
    ctx->desc_count = 0u;  /* descriptors belong to the record that set them */
    memset(rec, 0, sizeof *rec);
    rec->program = ctx->program;
    rec->code = code;
    rec->position = site;
    if (ctx->program != NULL && ctx->program->revision != NULL) {
        rec->has_revision = 1u;
        memcpy(rec->revision, ctx->program->revision, sizeof rec->revision);
    }
    rec->stack_count = ctx->stack_count;
    memcpy(rec->stack, ctx->stack, (size_t)ctx->stack_count * sizeof ctx->stack[0]);
    ctx->head.faulted = 1u;
    return rec;
}

void cint_rt_record_op(cint_fault_record *rec, const char *part)
{
    size_t n = strlen(part);
    size_t used = rec->operation_len;
    size_t dot = used > 0u ? 1u : 0u;
    if (used + dot + n > CINT_FAULT_MAX_OPERATION) {
        rec->operation_len = 0u;  /* never a partial identifier; encode refuses it */
        return;
    }
    if (dot != 0u) {
        rec->operation[used++] = '.';
    }
    memcpy(rec->operation + used, part, n);
    rec->operation_len = (uint8_t)(used + n);
}

void cint_rt_record_operand(cint_fault_record *rec, uint32_t tag, uint64_t bits)
{
    if (rec->operand_count < CINT_FAULT_MAX_OPERANDS) {
        cint_rt_tvalue_scalar(&rec->operands[rec->operand_count], tag, bits);
        rec->operand_count++;
    }
}

void cint_rt_record_range(cint_fault_record *rec, cint_rt_z128 exact, uint32_t tag)
{
    cint_rt_z128 max = cint_rt_z_max(tag);
    cint_rt_z128 bound = cint_rt_z_cmp(exact, max) > 0 ? max : cint_rt_z_min(tag);
    rec->has_exact = 1u;
    cint_rt_tvalue_z(&rec->exact, exact);
    rec->has_limit = 1u;
    cint_rt_tvalue_scalar(&rec->limit, tag, z_bits(bound));
}

static void record_limit_i64(cint_fault_record *rec, int64_t limit)
{
    rec->has_limit = 1u;
    cint_rt_tvalue_scalar(&rec->limit, CINT_TAG_I64, (uint64_t)limit);
}

/* A boundary fault (SPEC-03 A-6, Proposed encoding): E_UNSUPPORTED with
 * operation `op`, and the host status as an I64 operand when has_status. */
static bool fault_host(cint_ctx *ctx, cint_site site, const char *op, bool has_status, cint_status status)
{
    cint_fault_record *rec = cint_rt_record_begin(ctx, site, (uint16_t)CINT_E_UNSUPPORTED);
    if (rec != NULL) {
        cint_rt_record_op(rec, op);
        if (has_status) {
            cint_rt_record_operand(rec, CINT_TAG_I64, (uint64_t)(int64_t)status);
        }
    }
    return false;
}

/* A fault writer given arguments no emitted code passes (a tag that is not a
 * scalar tag, an unknown operator, an operation identifier that is absent or
 * longer than CINT_FAULT_MAX_OPERATION) still leaves the context faulted, so a
 * false return always means a record is written: E_UNSUPPORTED with the
 * operation dispatch.admit (RT-OQ-20). */
static bool fault_refused(cint_ctx *ctx, cint_site site)
{
    return fault_host(ctx, site, "dispatch.admit", false, CINT_OK);
}

bool cint_rt_fault_arith(cint_ctx *ctx, cint_site site, uint32_t op, uint32_t tag, uint64_t a, uint64_t b)
{
    static const char *const names[7] = {"", "add", "sub", "mul", "neg", "div", "rem"};
    cint_fault_record *rec;
    cint_rt_z128 za, zb, exact;
    bool divides = op == CINT_RT_OP_DIV || op == CINT_RT_OP_REM;
    if (op < CINT_RT_OP_ADD || op > CINT_RT_OP_REM || !is_scalar_tag(tag)) {
        return fault_refused(ctx, site);
    }
    rec = cint_rt_record_begin(ctx, site, (uint16_t)(divides && b == 0u ? CINT_E_DIV_ZERO : CINT_E_OVERFLOW));
    if (rec == NULL) {
        return false;
    }
    cint_rt_record_op(rec, names[op]);
    cint_rt_record_op(rec, "checked");
    cint_rt_record_op(rec, cint_rt_type_ident(tag));
    cint_rt_record_operand(rec, tag, a);
    if (op != CINT_RT_OP_NEG) {
        cint_rt_record_operand(rec, tag, b);
    }
    if (rec->code == CINT_E_DIV_ZERO) {
        return false;  /* no exact and no limit (SPEC-01 9.2) */
    }
    za = cint_rt_z_from_bits(tag, a);
    zb = cint_rt_z_from_bits(tag, b);
    switch (op) {
    case CINT_RT_OP_ADD:
        exact = cint_rt_z_add(za, zb);
        break;
    case CINT_RT_OP_SUB:
        exact = cint_rt_z_add(za, cint_rt_z_neg(zb));
        break;
    case CINT_RT_OP_MUL:
        exact = cint_rt_z_mul(za, zb);
        break;
    default:
        /* neg; and div, whose only overflow is MIN / -1 = -MIN (SPEC-01 4.4). */
        exact = cint_rt_z_neg(za);
        break;
    }
    cint_rt_record_range(rec, exact, tag);
    return false;
}

bool cint_rt_fault_shift(cint_ctx *ctx, cint_site site, uint32_t op, uint32_t tag, uint64_t a, cint_count k)
{
    cint_fault_record *rec;
    unsigned w;
    bool bad_count;
    if (op < CINT_RT_OP_SHL || op > CINT_RT_OP_SHR || !is_scalar_tag(tag) || !is_scalar_tag(k.tag)) {
        return fault_refused(ctx, site);
    }
    w = cint_rt_tag_width(tag);
    bad_count = k.bits > (uint64_t)(w - 1u);
    rec = cint_rt_record_begin(ctx, site, (uint16_t)(bad_count ? CINT_E_SHIFT : CINT_E_OVERFLOW));
    if (rec == NULL) {
        return false;
    }
    cint_rt_record_op(rec, op == CINT_RT_OP_SHR ? "shr" : "shl");
    cint_rt_record_op(rec, op == CINT_RT_OP_SHL_WRAP ? "wrap" : "checked");
    cint_rt_record_op(rec, cint_rt_type_ident(tag));
    cint_rt_record_operand(rec, tag, a);
    cint_rt_record_operand(rec, k.tag, k.bits);
    if (bad_count) {
        /* limit: the largest permitted count, typed I64 (REF-OQ-04, RT-OQ-01). */
        record_limit_i64(rec, (int64_t)(w - 1u));
        return false;
    }
    cint_rt_record_range(rec, cint_rt_z_shl(cint_rt_z_from_bits(tag, a), (unsigned)k.bits), tag);
    return false;
}

bool cint_rt_fault_narrow(cint_ctx *ctx, cint_site site, uint32_t from_tag, uint32_t to_tag, uint64_t x)
{
    cint_fault_record *rec;
    if (!is_scalar_tag(from_tag) || !is_scalar_tag(to_tag)) {
        return fault_refused(ctx, site);
    }
    rec = cint_rt_record_begin(ctx, site, (uint16_t)CINT_E_NARROW);
    if (rec == NULL) {
        return false;
    }
    cint_rt_record_op(rec, "as");
    cint_rt_record_op(rec, "checked");
    cint_rt_record_op(rec, cint_rt_type_ident(from_tag));
    cint_rt_record_op(rec, cint_rt_type_ident(to_tag));
    cint_rt_record_operand(rec, from_tag, x);
    cint_rt_record_range(rec, cint_rt_z_from_bits(from_tag, x), to_tag);
    return false;
}

bool cint_rt_fault_index(cint_ctx *ctx, cint_site site, const char *operation, int64_t index, int64_t extent)
{
    cint_fault_record *rec;
    if (operation == NULL || operation[0] == '\0' || strlen(operation) > CINT_FAULT_MAX_OPERATION) {
        return fault_refused(ctx, site);
    }
    rec = cint_rt_record_begin(ctx, site, (uint16_t)CINT_E_BOUNDS);
    if (rec == NULL) {
        return false;
    }
    cint_rt_record_op(rec, operation);
    cint_rt_record_operand(rec, CINT_TAG_I64, (uint64_t)index);
    record_limit_i64(rec, extent);
    return false;
}

bool cint_rt_fault_fuel(cint_ctx *ctx, cint_site site, uint64_t units)
{
    cint_fault_record *rec;
    (void)units;
    rec = cint_rt_record_begin(ctx, site, (uint16_t)CINT_E_FUEL);
    if (rec == NULL) {
        return false;
    }
    cint_rt_record_op(rec, "fuel.charge");
    /* limit: the allowance (SPEC-01 10.2); with no allowance, the largest
     * count an I64 can hold (RT-OQ-04). */
    record_limit_i64(rec, ctx->head.fuel_limit >= 0 ? ctx->head.fuel_limit : INT64_MAX);
    return false;
}

bool cint_rt_fault_assert(cint_ctx *ctx, cint_site site, uint32_t tag, uint64_t a, uint64_t b)
{
    cint_fault_record *rec;
    if (tag != 0u && tag != CINT_TAG_BOOL && !is_scalar_tag(tag)) {
        return fault_refused(ctx, site);
    }
    rec = cint_rt_record_begin(ctx, site, (uint16_t)CINT_E_ASSERT);
    if (rec == NULL) {
        return false;
    }
    cint_rt_record_op(rec, "assert");
    cint_rt_record_op(rec, "checked");
    cint_rt_record_op(rec, "bool");
    if (tag != 0u) {
        cint_rt_record_operand(rec, tag, a);
        cint_rt_record_operand(rec, tag, b);
    }
    return false;
}

/* ------------------------------------------------------------------------- */
/* Integer built-ins (cint_rt.h section 13; SPEC-01 2.4, 4.3 to 4.10).        */

#define BI_ABS 1u
#define BI_UABS 2u
#define BI_MIN 3u
#define BI_MAX 4u
#define BI_CLAMP 5u
#define BI_DIV_TRUNC 6u
#define BI_REM_TRUNC 7u
#define BI_DIV_EUCLID 8u
#define BI_REM_EUCLID 9u
#define BI_DIV_ROUND 10u
#define BI_MULDIV 11u
#define BI_MUL_FULL 12u
#define BI_ISQRT 13u
#define BI_ISQRT_ROUND 14u
#define BI_ROTL 15u
#define BI_ROTR 16u

/* The magnitude hi:lo divided by d != 0: the quotient in *q, the remainder
 * returned. */
static uint64_t div128(uint64_t hi, uint64_t lo, uint64_t d, cint_rt_z128 *q)
{
    uint64_t qh = 0u, ql = 0u, rem = 0u;
    int i;
    if (hi == 0u) {
        *q = z_make(false, 0u, lo / d);
        return lo % d;
    }
    for (i = 127; i >= 0; i--) {
        uint64_t bit = i >= 64 ? (hi >> (i - 64)) & 1u : (lo >> i) & 1u;
        bool carry = (rem >> 63) != 0u;
        rem = (rem << 1) | bit;
        qh = (qh << 1) | (ql >> 63);
        ql <<= 1;
        if (carry || rem >= d) {
            rem -= d;
            ql |= 1u;
        }
    }
    *q = z_make(false, qh, ql);
    return rem;
}

/* A rational of sign `negative` rounded by mode (SPEC-01 4.5, cint_ref
 * round_div), from its truncated quotient magnitude q and the remainder rem of
 * the divisor magnitude d. */
static cint_rt_z128 round_quotient(cint_rt_z128 q, uint64_t rem, bool negative, uint64_t d, uint32_t mode)
{
    bool up;
    switch (mode) {
    case 1u:
        up = negative;  /* floor */
        break;
    case 2u:
        up = !negative;  /* ceil */
        break;
    case 3u:
        up = false;  /* trunc */
        break;
    case 4u:
        up = true;  /* away */
        break;
    default:
        if (rem != d - rem) {
            up = rem > d - rem;
        } else if (mode == 5u) {
            up = (q.lo & 1u) != 0u;  /* half_even */
        } else {
            /* half_away, half_trunc, half_up, half_down */
            up = mode == 6u ? true : mode == 7u ? false : mode == 8u ? !negative : negative;
        }
        break;
    }
    if (rem != 0u && up) {
        q.lo++;
        q.hi += q.lo == 0u ? 1u : 0u;
    }
    return z_make(negative, q.hi, q.lo);
}

/* The largest r with r * r <= n. */
static uint64_t isqrt64(uint64_t n)
{
    uint64_t r = 0u, bit = (uint64_t)1 << 62;
    while (bit > n) {
        bit >>= 2;
    }
    while (bit != 0u) {
        if (n >= r + bit) {
            n -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

/* An operand pattern brought to its canonical 64-bit form under tag. */
static uint64_t canon(uint32_t tag, uint64_t bits)
{
    unsigned w = cint_rt_tag_width(tag);
    return cint_rt_tag_signed(tag) ? (uint64_t)cint_rt_sext(bits, w) : bits & cint_rt_mask(w);
}

bool cint_rt_builtin(cint_ctx *ctx, cint_site site, uint32_t op, uint32_t tag, uint32_t mode, uint32_t aux,
                     uint64_t a, uint64_t b, uint64_t c, uint64_t *out)
{
    static const char *const names[17] = {"",        "abs",       "uabs",       "min",        "max",
                                          "clamp",   "div_trunc", "rem_trunc",  "div_euclid", "rem_euclid",
                                          "div_round", "muldiv",  "mul_full",   "isqrt",      "isqrt_round",
                                          "rotl",    "rotr"};
    static const char *const modes[10] = {"",          "floor",     "ceil",       "trunc",   "away",
                                          "half_even", "half_away", "half_trunc", "half_up", "half_down"};
    bool rotates = op == BI_ROTL || op == BI_ROTR;
    bool rounds = op == BI_DIV_ROUND || op == BI_MULDIV || op == BI_ISQRT_ROUND;
    unsigned values = op == BI_ABS || op == BI_ISQRT || op == BI_ISQRT_ROUND ? 1u
                      : op == BI_CLAMP || op == BI_MULDIV                    ? 3u
                                                                             : 2u;
    uint32_t rtag = op == BI_MULDIV ? aux : tag;
    uint32_t btag = rotates ? aux : tag;
    cint_fault_record *rec;
    cint_rt_z128 za, zb, zc, q, exact = z_make(false, 0u, 0u);
    uint64_t rem, root;
    unsigned w;
    uint16_t code = 0u;
    if (cint_rt_faulted(ctx)) {
        return false;
    }
    if (op < BI_ABS || op > BI_ROTR || op == BI_UABS || op == BI_MIN || op == BI_MAX || op == BI_MUL_FULL ||
        !is_scalar_tag(tag) || !is_scalar_tag(rtag) || !is_scalar_tag(btag) || (rounds ? mode < 1u || mode > 9u : mode != 0u) ||
        (rotates && cint_rt_tag_signed(tag))) {
        return fault_refused(ctx, site);
    }
    w = cint_rt_tag_width(tag);
    a = canon(tag, a);
    b = canon(btag, b);
    c = canon(tag, c);
    za = cint_rt_z_from_bits(tag, a);
    zb = cint_rt_z_from_bits(tag, b);
    zc = cint_rt_z_from_bits(tag, c);
    switch (op) {
    case BI_ABS:
        exact = z_make(false, 0u, za.lo);
        break;
    case BI_CLAMP:
        /* lo > hi at run time: E_DOMAIN, no exact and no limit (OQ-06). */
        if (cint_rt_z_cmp(zb, zc) > 0) {
            code = (uint16_t)CINT_E_DOMAIN;
        } else {
            exact = cint_rt_z_cmp(za, zb) < 0 ? zb : cint_rt_z_cmp(za, zc) > 0 ? zc : za;
        }
        break;
    case BI_DIV_TRUNC:
    case BI_REM_TRUNC:
    case BI_DIV_EUCLID:
    case BI_REM_EUCLID:
        if (zb.lo == 0u) {
            code = (uint16_t)CINT_E_DIV_ZERO;
        } else {
            uint64_t qm = za.lo / zb.lo, rm = za.lo % zb.lo;
            bool rneg = za.negative;
            if ((op == BI_DIV_EUCLID || op == BI_REM_EUCLID) && za.negative && rm != 0u) {
                qm++;
                rm = zb.lo - rm;
                rneg = false;
            }
            exact = op == BI_DIV_TRUNC || op == BI_DIV_EUCLID ? z_make(za.negative != zb.negative, 0u, qm)
                                                              : z_make(rneg, 0u, rm);
        }
        break;
    case BI_DIV_ROUND:
        if (zb.lo == 0u) {
            code = (uint16_t)CINT_E_DIV_ZERO;
        } else {
            rem = div128(0u, za.lo, zb.lo, &q);
            exact = round_quotient(q, rem, za.negative != zb.negative, zb.lo, mode);
        }
        break;
    case BI_MULDIV:
        if (zc.lo == 0u) {
            code = (uint16_t)CINT_E_DIV_ZERO;
        } else {
            exact = cint_rt_z_mul(za, zb);
            rem = div128(exact.hi, exact.lo, zc.lo, &q);
            exact = round_quotient(q, rem, exact.negative != zc.negative, zc.lo, mode);
        }
        break;
    case BI_ISQRT:
    case BI_ISQRT_ROUND:
        if (za.negative) {
            code = (uint16_t)CINT_E_DOMAIN;
            break;
        }
        root = isqrt64(za.lo);
        /* No half-way ties: round up exactly when a > root^2 + root. */
        if (op == BI_ISQRT_ROUND && root * root != za.lo &&
            (mode == 2u || mode == 4u || (mode > 4u && za.lo > root * root + root))) {
            root++;
        }
        exact = z_make(false, 0u, root);
        break;
    default:
        /* rotations: the count is of any integer type, faulting outside 0 ..= w - 1. */
        if ((cint_rt_tag_signed(btag) && (b >> 63) != 0u) || b > (uint64_t)(w - 1u)) {
            code = (uint16_t)CINT_E_SHIFT;
        } else {
            unsigned s = op == BI_ROTL ? (unsigned)b : (w - (unsigned)b) % w;
            *out = s == 0u ? a : ((a << s) | (a >> ((w - s) & 63u))) & cint_rt_mask(w);
            return true;
        }
        break;
    }
    if (code == 0u) {
        if (cint_rt_z_cmp(exact, cint_rt_z_min(rtag)) >= 0 && cint_rt_z_cmp(exact, cint_rt_z_max(rtag)) <= 0) {
            *out = z_bits(exact);
            return true;
        }
        code = (uint16_t)CINT_E_OVERFLOW;
    }
    rec = cint_rt_record_begin(ctx, site, code);
    if (rec == NULL) {
        return false;
    }
    cint_rt_record_op(rec, names[op]);
    cint_rt_record_op(rec, "checked");
    cint_rt_record_op(rec, cint_rt_type_ident(tag));
    if (op == BI_MULDIV) {
        cint_rt_record_op(rec, cint_rt_type_ident(rtag));
    }
    if (mode != 0u) {
        cint_rt_record_op(rec, modes[mode]);
    }
    cint_rt_record_operand(rec, tag, a);
    if (values > 1u) {
        cint_rt_record_operand(rec, btag, b);
    }
    if (values > 2u) {
        cint_rt_record_operand(rec, tag, c);
    }
    if (code == CINT_E_OVERFLOW) {
        cint_rt_record_range(rec, exact, rtag);
    } else if (code == CINT_E_SHIFT) {
        record_limit_i64(rec, (int64_t)(w - 1u));  /* REF-OQ-04, as for shifts */
    }
    return false;
}

/* Entry-phase faults (SPEC-03 A-12, H-12): no fuel is charged, so the entry's
 * own unit, charged by cint_rt_entry_begin, is returned. */
bool cint_fault_shape(cint_ctx *ctx, cint_site site, uint32_t param, uint32_t dim, int64_t expected,
                      int64_t actual)
{
    cint_fault_record *rec = cint_rt_record_begin(ctx, site, (uint16_t)CINT_E_SHAPE);
    if (rec == NULL) {
        return false;
    }
    cint_rt_record_op(rec, "bind.shape");
    cint_rt_record_operand(rec, CINT_TAG_I64, (uint64_t)param);
    cint_rt_record_operand(rec, CINT_TAG_I64, (uint64_t)dim);
    cint_rt_record_operand(rec, CINT_TAG_I64, (uint64_t)actual);
    record_limit_i64(rec, expected);
    ctx->head.fuel_used = 0;
    return false;
}

bool cint_shape_check(cint_ctx *ctx, cint_site site, uint32_t param, int64_t expected, int64_t actual)
{
    int64_t used = ctx->head.fuel_used;
    if (actual == expected) {
        return true;
    }
    (void)cint_fault_shape(ctx, site, param, 0u, expected, actual);
    ctx->head.fuel_used = used;
    return false;
}

bool cint_copy_shape_check(cint_ctx *ctx, cint_site site, int64_t dimension,
                           int64_t destination_extent, int64_t source_extent)
{
    cint_fault_record *rec;
    if (cint_rt_faulted(ctx)) {
        return false;
    }
    if (source_extent == destination_extent) {
        return true;
    }
    rec = cint_rt_record_begin(ctx, site, (uint16_t)CINT_E_SHAPE);
    if (rec == NULL) {
        return false;
    }
    cint_rt_record_op(rec, "copy.shape");
    cint_rt_record_operand(rec, CINT_TAG_I64, (uint64_t)dimension);
    cint_rt_record_operand(rec, CINT_TAG_I64, (uint64_t)source_extent);
    record_limit_i64(rec, destination_extent);
    return false;
}

bool cint_fault_alias(cint_ctx *ctx, cint_site site, uint32_t param, uint32_t other)
{
    cint_fault_record *rec = cint_rt_record_begin(ctx, site, (uint16_t)CINT_E_ALIAS);
    if (rec == NULL) {
        return false;
    }
    cint_rt_record_op(rec, "bind.alias");
    cint_rt_record_operand(rec, CINT_TAG_I64, (uint64_t)param);
    cint_rt_record_operand(rec, CINT_TAG_I64, (uint64_t)other);
    ctx->head.fuel_used = 0;
    return false;
}

/* ------------------------------------------------------------------------- */
/* Contexts and entries.                                                      */

static void *default_alloc(void *user, size_t bytes, size_t align)
{
    (void)user;
    (void)align;  /* malloc's alignment suffices for struct cint_ctx */
    return malloc(bytes);
}

static void default_release(void *user, void *ptr, size_t bytes, size_t align)
{
    (void)user;
    (void)bytes;
    (void)align;
    free(ptr);
}

static bool program_valid(const cint_program *p)
{
    uint32_t i;
    if (p->module_count > 0u && p->modules == NULL) {
        return false;
    }
    for (i = 0; i < p->module_count; i++) {
        const cint_module *m = p->modules[i];
        if (m == NULL || m->path == NULL || m->path_len > CINT_FAULT_MAX_PATH ||
            (m->site_count > 0u && m->sites == NULL)) {
            return false;
        }
    }
    return true;
}

uint32_t cint_abi_version(void)
{
    return CINT_ABI_VERSION;
}

/* Frees the scratch allocations made after the first `mark` (cint_rt.h section 14). */
static void scratch_free(cint_ctx *ctx, int64_t mark)
{
    while (ctx->scratch != NULL && ctx->scratch_count > mark) {
        scratch_block *b = ctx->scratch;
        ctx->scratch = b->prev;
        ctx->scratch_count--;
        ctx->scratch_used -= b->elements;
        ctx->allocator.release(ctx->allocator.user, b, b->bytes, 16u);
    }
}

cint_status cint_ctx_create(const cint_ctx_config *config, cint_ctx **out)
{
    cint_allocator a;
    cint_ctx *ctx;
    uint32_t cap;
    if (config == NULL || out == NULL || config->size != (uint32_t)sizeof *config || config->program == NULL ||
        !program_valid(config->program) || config->depth_limit < 0 || config->frame_arena_bytes < 0 ||
        (config->module != NULL &&
         (config->module->abi != CINT_ABI_VERSION || config->module->program != config->program ||
          (config->module->record_count > 0u && config->module->records == NULL)))) {
        return CINT_REFUSED;
    }
    cap = config->max_buffers != 0u ? config->max_buffers : CINT_RT_MAX_BUFFERS;
    a = config->allocator;
    if (a.alloc == NULL) {
        a.alloc = default_alloc;
        a.release = default_release;
        a.user = NULL;
    } else if (a.release == NULL) {
        return CINT_REFUSED;
    }
    ctx = (cint_ctx *)a.alloc(a.user, sizeof *ctx, _Alignof(struct cint_ctx));
    if (ctx == NULL) {
        return CINT_RESOURCE;
    }
    memset(ctx, 0, sizeof *ctx);
#if !defined(_MSC_VER)
    atomic_init(&ctx->busy, 0);
#endif
    ctx->buffers = (buffer_slot *)a.alloc(a.user, cap * sizeof(buffer_slot), _Alignof(buffer_slot));
    ctx->leases = ctx->buffers == NULL ? NULL : (lease_slot *)a.alloc(a.user, cap * sizeof(lease_slot),
                                                                      _Alignof(lease_slot));
    if (ctx->leases == NULL) {
        if (ctx->buffers != NULL) {
            a.release(a.user, ctx->buffers, cap * sizeof(buffer_slot), _Alignof(buffer_slot));
        }
        a.release(a.user, ctx, sizeof *ctx, _Alignof(struct cint_ctx));
        return CINT_RESOURCE;
    }
    memset(ctx->buffers, 0, cap * sizeof(buffer_slot));
    memset(ctx->leases, 0, cap * sizeof(lease_slot));
    ctx->buffer_cap = cap;
    ctx->module = config->module;
    ctx->program = config->program;
    ctx->allocator = a;
    ctx->config_depth = config->depth_limit != 0 ? config->depth_limit : CINT_DEFAULT_DEPTH;
    ctx->frame_arena_bytes =
        config->frame_arena_bytes != 0 ? config->frame_arena_bytes : CINT_DEFAULT_FRAME_ARENA_BYTES;
    ctx->output = config->output;
    ctx->output_user = config->output_user;
    ctx->head.fuel_limit = CINT_FUEL_UNBOUNDED;
    *out = ctx;
    return CINT_OK;
}

#include "cint_mem.c"  /* arenas and pools (OQ-212), counted in their own 4.2 row */
void cint_ctx_destroy(cint_ctx *ctx)
{
    cint_allocator a;
    if (ctx == NULL) {
        return;
    }
    a = ctx->allocator;
    if (ctx->device != NULL) {  /* its first member is its release function */
        (*(void (**)(void *))ctx->device)(ctx->device);
    }
    scratch_free(ctx, 0);
    mem_destroy(ctx);
    if (ctx->print_buf != NULL) {
        a.release(a.user, ctx->print_buf, ctx->print_cap, 1u);
    }
    for (uint32_t i = 0u; i < ctx->buffer_used; i++) {
        const buffer_slot *s = &ctx->buffers[i];
        if (s->live != 0u && s->storage == STORAGE_CREATED && s->base != NULL) {
            a.release(a.user, s->base, (size_t)s->bytes, 8u);
        }
    }
    a.release(a.user, ctx->buffers, ctx->buffer_cap * sizeof(buffer_slot), _Alignof(buffer_slot));
    a.release(a.user, ctx->leases, ctx->buffer_cap * sizeof(lease_slot), _Alignof(lease_slot));
    if (ctx->state != NULL) {
        uint32_t n = ctx->program->module_count;
        for (uint32_t m = 0u; m < n; m++) {
            if (ctx->state[m] != NULL) {
                a.release(a.user, ctx->state[m], ((const cint_state *)ctx->state[n + m])->bytes, 8u);
            }
        }
        a.release(a.user, (void *)ctx->state, 2u * n * sizeof(void *), _Alignof(void *));
    }
    a.release(a.user, ctx, sizeof *ctx, _Alignof(struct cint_ctx));
}

/* Module state (cint_rt.h): the context's block of module s->module, created
 * from the module's initial image at its first access. */
void *cint_rt_state(cint_ctx *ctx, const cint_state *s)
{
    uint32_t n = ctx->program->module_count, m = s->module;
    cint_site none = {m, 0u};
    if (m >= n || s->bytes == 0u) {
        (void)fault_refused(ctx, none);
        return NULL;
    }
    if (ctx->state == NULL) {
        ctx->state = (void **)ctx->allocator.alloc(ctx->allocator.user, 2u * n * sizeof(void *), _Alignof(void *));
        if (ctx->state == NULL) {
            (void)fault_host(ctx, none, "host.resource", false, CINT_OK);
            return NULL;
        }
        for (uint32_t k = 0u; k < 2u * n; k++) {
            ctx->state[k] = NULL;
        }
    }
    if (ctx->state[m] == NULL) {
        void *b = ctx->allocator.alloc(ctx->allocator.user, s->bytes, 8u);
        if (b == NULL) {
            (void)fault_host(ctx, none, "host.resource", false, CINT_OK);
            return NULL;
        }
        memcpy(b, s->init, s->bytes);
        ctx->state[m] = b;
        ctx->state[n + m] = (void *)(uintptr_t)s;
    }
    return ctx->state[m];
}

/* CONF-11 rule 10: one `state.global` line per variable of each table. */
size_t cint_state_render(const cint_ctx *ctx, const cint_state_table *t, char *out, size_t cap)
{
    size_t at = 0u;
    for (uint32_t i = 0u; i < t->count; i++) {
        const cint_state *s = t->states[i];
        const cint_module *mod = s->module < ctx->program->module_count ? ctx->program->modules[s->module] : NULL;
        const uint8_t *b = ctx->state != NULL && mod != NULL && ctx->state[s->module] != NULL
                               ? (const uint8_t *)ctx->state[s->module] : (const uint8_t *)s->init;
        for (uint32_t j = 0u; mod != NULL && j < s->var_count; j++) {
            const cint_state_var *v = &s->vars[j];
            size_t w = v->tag == CINT_TAG_BOOL ? 1u : cint_rt_tag_width(v->tag) / 8u, n;
            cint_tvalue tv;
            if (mod->path_len < 4u || v->offset + w > s->bytes || cap - at < 16u + mod->path_len + v->name_len) {
                return SIZE_MAX;
            }
            memcpy(out + at, "state.global ", 13u);
            at += 13u;
            for (size_t k = 0u; k + 3u < mod->path_len; k++) {
                out[at++] = mod->path[k] == '/' ? '.' : mod->path[k];  /* a/b.ci is a.b (LS-225) */
            }
            out[at++] = ' ';
            memcpy(out + at, v->name, v->name_len);
            at += v->name_len;
            out[at++] = ' ';
            tv.len = (uint16_t)(1u + w);
            tv.bytes[0] = (uint8_t)v->tag;
            memcpy(tv.bytes + 1, b + v->offset, w);  /* little-endian hosts (SPEC-01 IM-146) */
            n = cint_tvalue_render(&tv, out + at, cap - at);
            if (n == 0u || n >= cap - at) {
                return SIZE_MAX;
            }
            at += n;
            out[at++] = '\n';
        }
        if (mod == NULL) {
            return SIZE_MAX;
        }
    }
    return at;
}

cint_status cint_ctx_fault(const cint_ctx *cctx, cint_fault_record *out)
{
    /* The context object is never defined const; reading the record takes the
     * busy flag, so the qualifier is removed for that write only. */
    cint_ctx *ctx = (cint_ctx *)(uintptr_t)cctx;
    if (!cint_rt_busy_take(ctx)) {
        return CINT_BUSY;
    }
    if (ctx->head.faulted != 0u) {
        memcpy(out, &ctx->fault, sizeof *out);
    } else {
        memset(out, 0, sizeof *out);
    }
    cint_rt_busy_give(ctx);
    return CINT_OK;
}

cint_status cint_fault_get(const cint_ctx *cctx, uint8_t *buf, size_t cap, size_t *len)
{
    cint_ctx *ctx = (cint_ctx *)(uintptr_t)cctx;
    cint_status st = CINT_OK;
    size_t n = 0u;
    if (ctx == NULL || len == NULL || (buf == NULL && cap > 0u)) {
        return CINT_REFUSED;
    }
    if (!cint_rt_busy_take(ctx)) {
        return CINT_BUSY;
    }
    if (ctx->head.faulted != 0u) {
        n = cint_fault_encode(&ctx->fault, NULL, 0u);
    }
    if (n > 0u && cap >= n) {
        (void)cint_fault_encode(&ctx->fault, buf, cap);
    } else if (n > 0u && cap > 0u) {
        uint8_t *tmp = (uint8_t *)ctx->allocator.alloc(ctx->allocator.user, n, 1u);
        if (tmp == NULL) {
            st = CINT_RESOURCE;
        } else {
            (void)cint_fault_encode(&ctx->fault, tmp, n);
            memcpy(buf, tmp, cap);
            ctx->allocator.release(ctx->allocator.user, tmp, n, 1u);
        }
    }
    if (st == CINT_OK) {
        *len = n;
    }
    cint_rt_busy_give(ctx);
    return st;
}

cint_status cint_ctx_clear_fault(cint_ctx *ctx)
{
    if (ctx == NULL) {
        return CINT_REFUSED;
    }
    if (!cint_rt_busy_take(ctx)) {
        return CINT_BUSY;
    }
    ctx->head.faulted = 0u;
    memset(&ctx->fault, 0, sizeof ctx->fault);
    ctx->desc_count = 0u;
    cint_rt_busy_give(ctx);
    return CINT_OK;
}

cint_status cint_ctx_refusal(const cint_ctx *cctx, uint32_t *reason)
{
    cint_ctx *ctx = (cint_ctx *)(uintptr_t)cctx;
    if (ctx == NULL || reason == NULL) {
        return CINT_REFUSED;
    }
    if (!cint_rt_busy_take(ctx)) {
        return CINT_BUSY;
    }
    *reason = ctx->refusal;
    cint_rt_busy_give(ctx);
    return CINT_OK;
}

cint_status cint_fuel_consumed(const cint_ctx *cctx, int64_t *out)
{
    cint_ctx *ctx = (cint_ctx *)(uintptr_t)cctx;
    if (!cint_rt_busy_take(ctx)) {
        return CINT_BUSY;
    }
    *out = ctx->head.fuel_used;
    cint_rt_busy_give(ctx);
    return CINT_OK;
}

cint_status cint_rt_entry_begin(cint_ctx *ctx, cint_site site, int64_t fuel, int64_t depth)
{
    cint_status st = cint_rt_entry_open(ctx, site, fuel, depth);
    if (st == CINT_OK && !cint_fuel_charge(ctx, site, 1u)) {
        ctx->head.in_entry = 0u;
        cint_rt_busy_give(ctx);
        return CINT_FAULT;
    }
    return st;
}

cint_status cint_rt_entry_open(cint_ctx *ctx, cint_site site, int64_t fuel, int64_t depth)
{
    if (!cint_rt_busy_take(ctx)) {
        return CINT_BUSY;
    }
    if (ctx->head.faulted != 0u) {
        cint_rt_busy_give(ctx);
        return CINT_FAULTED;
    }
    ctx->refusal = CINT_REFUSAL_NONE;
    if (depth == CINT_DEPTH_FROM_CONFIG) {
        depth = ctx->config_depth;
    }
    if (fuel < CINT_FUEL_UNBOUNDED || depth < 1) {
        cint_rt_busy_give(ctx);
        return CINT_REFUSED;
    }
    ctx->saved_fuel_limit = ctx->head.fuel_limit;
    ctx->saved_fuel_used = ctx->head.fuel_used;
    ctx->saved_error_set = ctx->error_set;
    ctx->saved_error_tag = ctx->error_tag;
    ctx->error_set = NULL;
    ctx->error_tag = 0u;
    ctx->print_open = 0u;
    ctx->print_len = 0u;
    scratch_free(ctx, 0);  /* what a faulted entry left */
    ctx->frame_charged = 0;
    ctx->dispatches = 0;
    ctx->host_binds = NULL;
    ctx->host_count = 0u;
    ctx->entries++;
    ctx->interrupt = 0;
    ctx->head.in_entry = 1u;
    ctx->head.fuel_limit = fuel;
    ctx->head.fuel_used = 0;
    ctx->depth_limit = depth;
    ctx->depth = 1;  /* the entry function has depth 1 (SPEC-01 9.4) */
    ctx->stack_count = 0u;
    if (depth > (int64_t)CINT_RT_MAX_DEPTH) {
        /* Native resources for D frames are not provided: refuse the entry with
         * E_UNSUPPORTED before execution (SPEC-01 9.4; RT-OQ-03). */
        cint_fault_record *rec = cint_rt_record_begin(ctx, site, (uint16_t)CINT_E_UNSUPPORTED);
        if (rec != NULL) {
            cint_rt_record_op(rec, "call.enter");
            cint_rt_record_operand(rec, CINT_TAG_I64, (uint64_t)depth);
            record_limit_i64(rec, (int64_t)CINT_RT_MAX_DEPTH);
        }
        ctx->head.in_entry = 0u;
        cint_rt_busy_give(ctx);
        return CINT_FAULT;
    }
    return CINT_OK;
}

cint_status cint_rt_entry_end(cint_ctx *ctx)
{
    cint_status st = ctx->head.faulted == 0u ? CINT_OK : ctx->head.faulted == 2u ? CINT_RESOURCE : CINT_FAULT;
    if (st == CINT_OK && ctx->refusal != CINT_REFUSAL_NONE) {
        /* A refused call is not an entry (SPEC-03 H-12): fuel and the error
         * result as they were. A leased buffer bound for writing is CINT_BUSY
         * (A-9). */
        ctx->head.fuel_limit = ctx->saved_fuel_limit;
        ctx->head.fuel_used = ctx->saved_fuel_used;
        ctx->error_set = ctx->saved_error_set;
        ctx->error_tag = ctx->saved_error_tag;
        ctx->entries--;
        st = ctx->refusal == CINT_REFUSAL_LEASED ? CINT_BUSY : CINT_REFUSED;
    } else if (st == CINT_FAULT) {
        ctx->error_set = NULL;  /* an entry that faults leaves no error result (A-18) */
        ctx->error_tag = 0u;
    }
    ctx->print_open = 0u;
    ctx->print_len = 0u;
    ctx->head.in_entry = 0u;
    cint_rt_busy_give(ctx);
    return st;
}

void **cint_rt_device(cint_ctx *ctx)
{
    return &ctx->device;
}

/* Error results (cint_rt.h 6c'; SPEC-03 A-18). */
void cint_rt_error_result(cint_ctx *ctx, const cint_error_set *set, uint64_t tag)
{
    if (ctx == NULL || ctx->head.faulted != 0u || ctx->head.in_entry == 0u || set == NULL || tag == 0u ||
        tag > set->count || ctx->error_set != NULL) {
        return;
    }
    ctx->error_set = set;
    ctx->error_tag = tag;
}

cint_status cint_ctx_error(const cint_ctx *cctx, const cint_error_set **set, uint64_t *tag)
{
    cint_ctx *ctx = (cint_ctx *)(uintptr_t)cctx;
    if (ctx == NULL || set == NULL || tag == NULL) {
        return CINT_REFUSED;
    }
    if (!cint_rt_busy_take(ctx)) {
        return CINT_BUSY;
    }
    *set = ctx->head.faulted != 0u ? NULL : ctx->error_set;
    *tag = *set != NULL ? ctx->error_tag : 0u;
    cint_rt_busy_give(ctx);
    return CINT_OK;
}

bool cint_rt_call_enter(cint_ctx *ctx, cint_site site)
{
    if (ctx->head.faulted != 0u) {
        return false;
    }
    if (ctx->depth >= ctx->depth_limit) {
        /* E_DEPTH before the callee's fuel charge, so the call consumes no fuel. */
        cint_fault_record *rec = cint_rt_record_begin(ctx, site, (uint16_t)CINT_E_DEPTH);
        if (rec != NULL) {
            cint_rt_record_op(rec, "call.enter");
            record_limit_i64(rec, ctx->depth_limit);
        }
        return false;
    }
    if (!cint_fuel_charge(ctx, site, 1u)) {
        return false;
    }
    ctx->stack[ctx->stack_count] = site;  /* stack_count = depth - 1 < D <= CINT_RT_MAX_DEPTH */
    ctx->stack_count++;
    ctx->depth++;
    return true;
}

void cint_rt_call_leave(cint_ctx *ctx)
{
    if (ctx->stack_count > 0u) {
        ctx->stack_count--;
        ctx->depth--;
    }
}

/* ------------------------------------------------------------------------- */
/* The minimal buffer registry and view binding (SPEC-03 A-8, A-12; D-3).     */

static bool type_equal(const cint_type *a, const cint_type *b)
{
    return a->code == b->code && a->storage == b->storage && a->frac_bits == b->frac_bits &&
           a->record_id == b->record_id && a->reserved0 == 0u && a->reserved1 == 0u && a->reserved2 == 0u;
}

static bool element_type_valid(const cint_type *type, int64_t bytes)
{
    if (type == NULL || bytes < 0 || type->reserved0 != 0u || type->reserved1 != 0u ||
        type->reserved2 != 0u || type->storage != 0u || type->frac_bits != 0u) {
        return false;
    }
    if (type->code == CINT_TAG_RECORD) {
        return true;
    }
    return type->record_id == 0u && ((type->code == CINT_TAG_BOOL && bytes == 1) ||
           (is_scalar_tag(type->code) && bytes == (int64_t)(cint_rt_tag_width(type->code) / 8u)));
}

/* A free slot: one never used first, then the lowest released one with no
 * lease; deterministic (M-11). NULL when the registry is full or ids ran out. */
static buffer_slot *slot_take(cint_ctx *ctx)
{
    if (ctx->last_id == UINT64_MAX) {
        return NULL;
    }
    if (ctx->buffer_used < ctx->buffer_cap) {
        return &ctx->buffers[ctx->buffer_used++];
    }
    for (uint32_t i = 0u; i < ctx->buffer_cap; i++) {
        if (ctx->buffers[i].live == 0u && ctx->buffers[i].leases == 0u) {
            return &ctx->buffers[i];
        }
    }
    return NULL;
}

/* A new registration in slot s, at the first generation, with a new id. */
static void slot_fill(cint_ctx *ctx, buffer_slot *s, void *base, int64_t bytes, uint8_t perm, const cint_type *type,
                      int64_t elem_bytes, int64_t extent)
{
    memset(s, 0, sizeof *s);
    s->id = ++ctx->last_id;
    s->generation = CINT_BUFFER_GENERATION_FIRST;
    s->base = (unsigned char *)base;
    s->bytes = bytes;
    if (type != NULL) {
        s->type = *type;
    }
    s->elem_bytes = elem_bytes;
    s->extent = extent;
    s->perm = perm;
    s->live = 1u;
    s->typed = (uint8_t)(type != NULL);
}

static cint_status register_buffer(cint_ctx *ctx, void *base, int64_t bytes, uint8_t perm,
                                   const cint_type *type, int64_t elem_bytes, int64_t extent, cint_buffer_id *out)
{
    buffer_slot *s;
    if (ctx == NULL || out == NULL || bytes < 0 || (base == NULL && bytes != 0) || perm > CINT_VIEW_WRITE ||
        (uint64_t)bytes > UINTPTR_MAX - (uintptr_t)base) {
        return CINT_REFUSED;
    }
    if (!cint_rt_busy_take(ctx)) {
        return CINT_BUSY;
    }
    s = slot_take(ctx);
    if (s == NULL) {
        cint_rt_busy_give(ctx);
        return CINT_RESOURCE;
    }
    slot_fill(ctx, s, base, bytes, perm, type, elem_bytes, extent);
    *out = s->id;
    cint_rt_busy_give(ctx);
    return CINT_OK;
}

cint_status cint_buffer_register_bytes(cint_ctx *ctx, void *base, int64_t bytes, uint8_t perm, cint_buffer_id *out)
{
    return register_buffer(ctx, base, bytes, perm, NULL, 0, 0, out);
}

cint_status cint_buffer_register_elements(cint_ctx *ctx, void *base, const cint_type *type,
                                          int64_t elem_bytes, int64_t extent, uint8_t perm, cint_buffer_id *out)
{
    if (!element_type_valid(type, elem_bytes) || extent < 0 ||
        (elem_bytes != 0 && extent > INT64_MAX / elem_bytes)) {
        return CINT_REFUSED;
    }
    return register_buffer(ctx, elem_bytes == 0 ? NULL : base, elem_bytes * extent, perm,
                           type, elem_bytes, extent, out);
}

static buffer_slot *find_buffer(cint_ctx *ctx, cint_buffer_id id)
{
    for (uint32_t i = 0u; i < ctx->buffer_used && id != 0u; i++) {
        if (ctx->buffers[i].id == id) {
            return &ctx->buffers[i];
        }
    }
    return NULL;
}

/* A registry function's refusal: the reason for cint_ctx_refusal. */
static cint_status registry_refused(cint_ctx *ctx, uint32_t reason)
{
    ctx->refusal = reason;
    cint_rt_busy_give(ctx);
    return CINT_REFUSED;
}

cint_status cint_buffer_release(cint_ctx *ctx, cint_buffer_id id)
{
    buffer_slot *s;
    if (ctx == NULL) {
        return CINT_REFUSED;
    }
    if (!cint_rt_busy_take(ctx)) {
        return CINT_BUSY;
    }
    s = find_buffer(ctx, id);
    if (s == NULL || s->live == 0u) {
        return registry_refused(ctx, s == NULL ? CINT_REFUSAL_BUFFER : CINT_REFUSAL_GENERATION);
    }
    if (s->leases > 0u) {
        cint_rt_busy_give(ctx);
        return CINT_BUSY;  /* a leased address stays valid until cint_lease_end (A-9) */
    }
    if (s->storage == STORAGE_CREATED && s->base != NULL) {
        ctx->allocator.release(ctx->allocator.user, s->base, (size_t)s->bytes, 8u);
        s->base = NULL;
    }
    s->live = 0u;
    s->generation++;
    cint_rt_busy_give(ctx);
    return CINT_OK;
}

static bool bind_refused(cint_ctx *ctx, uint32_t reason)
{
    ctx->refusal = reason;
    return false;
}

/* The largest power of two dividing es, at most 8 (A-13a); 1 for 0. */
static int64_t elem_align(int64_t es)
{
    int64_t align = 1;
    while (es > 0 && align < 8 && es % (align * 2) == 0) {
        align *= 2;
    }
    return align;
}

bool cint_view_bind(cint_ctx *ctx, const cint_view *v, const cint_type *want, uint8_t mode, int64_t elem_bytes,
                    void **ptr, int64_t *n)
{
    static const uint8_t zero[6] = {0};
    const buffer_slot *s;
    int64_t origin, extent;
    if (ctx == NULL || ctx->head.in_entry == 0u || ctx->head.faulted != 0u || ctx->refusal != CINT_REFUSAL_NONE) {
        return false;
    }
    if (v == NULL || want == NULL || ptr == NULL || n == NULL || mode > CINT_MODE_INOUT || elem_bytes < 0 ||
        (elem_bytes == 0 && !element_type_valid(want, 0))) {
        return bind_refused(ctx, CINT_REFUSAL_CALL);
    }
    s = find_buffer(ctx, v->buffer);
    if (s == NULL || s->live == 0u || s->generation != v->generation) {
        return bind_refused(ctx, s == NULL ? CINT_REFUSAL_BUFFER : CINT_REFUSAL_GENERATION);
    }
    if (!type_equal(&v->type, want) || memcmp(v->reserved, zero, sizeof zero) != 0 ||
        (s->typed != 0u && (!type_equal(&v->type, &s->type) || elem_bytes != s->elem_bytes)) ||
        (s->typed == 0u && elem_bytes == 0)) {
        return bind_refused(ctx, CINT_REFUSAL_TYPE);
    }
    if (v->rank != 1u) {
        return bind_refused(ctx, CINT_REFUSAL_RANK);
    }
    if (v->stride[0] != 1) {
        return bind_refused(ctx, CINT_REFUSAL_STRIDE);
    }
    origin = v->origin;
    extent = v->shape[0];
    if (origin < 0 || extent < 0) {
        return bind_refused(ctx, CINT_REFUSAL_EXTENT);
    }
    if (elem_bytes == 0) {
        if (origin > s->extent || extent > s->extent - origin) {
            return bind_refused(ctx, CINT_REFUSAL_EXTENT);
        }
    } else {
        if (origin > INT64_MAX - extent || origin + extent > INT64_MAX / elem_bytes) {
            return bind_refused(ctx, CINT_REFUSAL_SIZE);
        }
        if ((origin + extent) * elem_bytes > s->bytes) {
            return bind_refused(ctx, CINT_REFUSAL_EXTENT);
        }
        if (((uintptr_t)s->base + (uintptr_t)(origin * elem_bytes)) % (uintptr_t)elem_align(elem_bytes) != 0u) {
            return bind_refused(ctx, CINT_REFUSAL_ALIGN);
        }
    }
    if (v->perm > s->perm || (mode != CINT_MODE_IN && v->perm != CINT_VIEW_WRITE)) {
        return bind_refused(ctx, CINT_REFUSAL_PERMISSION);
    }
    if (mode != CINT_MODE_IN && s->leases > 0u) {
        return bind_refused(ctx, CINT_REFUSAL_LEASED);
    }
    *ptr = elem_bytes == 0 || s->base == NULL ? NULL : (void *)(s->base + origin * elem_bytes);
    *n = extent;
    return true;
}

bool cint_view_check_bools(cint_ctx *ctx, const void *p, int64_t n, int64_t elem_bytes,
                           const uint32_t *offsets, uint32_t count)
{
    const uint8_t *b = p;
    if (ctx == NULL || ctx->head.in_entry == 0u || ctx->head.faulted != 0u || ctx->refusal != CINT_REFUSAL_NONE) {
        return false;
    }
    for (int64_t i = 0; b != NULL && i < n; i++) {
        for (uint32_t k = 0u; k < count; k++) {
            if (b[i * elem_bytes + (int64_t)offsets[k]] > 1u) {
                return bind_refused(ctx, CINT_REFUSAL_BOOL);
            }
        }
    }
    return true;
}

/* ------------------------------------------------------------------------- */
/* The entry checks of a cint-abi-1 wrapper (cint_rt.h section 6a').          */

/* A binder may run: in an entry that has neither faulted nor refused, with b
 * given when count is not 0 (else the entry is refused, CALL). */
static bool bind_ready(cint_ctx *ctx, const cint_bind *b, uint32_t count)
{
    if (ctx == NULL || ctx->head.in_entry == 0u || ctx->head.faulted != 0u || ctx->refusal != CINT_REFUSAL_NONE) {
        return false;
    }
    return count == 0u || b != NULL || bind_refused(ctx, CINT_REFUSAL_CALL);
}

bool cint_rt_refuse(cint_ctx *ctx, uint32_t reason)
{
    return bind_ready(ctx, NULL, 0u) && bind_refused(ctx, reason);
}

/* A type this runtime knows: Bool, I8..U64, or a record, reserved fields 0. */
static bool type_known(const cint_type *t)
{
    if (t->reserved0 != 0u || t->reserved1 != 0u || t->reserved2 != 0u || t->storage != 0u || t->frac_bits != 0u) {
        return false;
    }
    return t->code == CINT_TAG_RECORD || (t->record_id == 0u && (t->code == CINT_TAG_BOOL || is_scalar_tag(t->code)));
}

/* The element size of parameter b's view over registration s: the typed
 * registration's, the declared one for a view of the declared type, a scalar
 * width, or -1 for a record of another type over untyped memory. */
static int64_t view_elem_bytes(const cint_bind *b, const buffer_slot *s)
{
    const cint_type *t = &b->view->type;
    if (s->typed != 0u) {
        return s->elem_bytes;
    }
    if (type_equal(t, b->want)) {
        return b->elem_bytes;
    }
    if (t->code == CINT_TAG_RECORD) {
        return -1;
    }
    return t->code == CINT_TAG_BOOL ? 1 : (int64_t)(cint_rt_tag_width(t->code) / 8u);
}

static bool view_empty(const cint_view *v)
{
    for (uint32_t d = 0u; d < v->rank; d++) {
        if (v->shape[d] == 0) {
            return true;
        }
    }
    return false;
}

/* The least and greatest element offsets of a nonempty view (shapes >= 1),
 * or false when one leaves the I64 range. */
static bool view_span(const cint_view *v, int64_t *lo, int64_t *hi)
{
    int64_t a = v->origin, z = v->origin;
    for (uint32_t d = 0u; d < v->rank; d++) {
        int64_t k = v->shape[d] - 1, st = v->stride[d];
        if (k == 0 || st == 0) {
            continue;
        }
        if (st == INT64_MIN || k > INT64_MAX / (st > 0 ? st : -st)) {
            return false;
        }
        if (st > 0) {
            if (z > INT64_MAX - k * st) {
                return false;
            }
            z += k * st;
        } else {
            if (a < INT64_MIN - k * st) {
                return false;
            }
            a += k * st;
        }
    }
    *lo = a;
    *hi = z;
    return true;
}

/* Every element of v lies in [0, cap): CINT_REFUSAL_NONE, or the reason. */
static uint32_t view_within(const cint_view *v, int64_t cap)
{
    int64_t lo, hi;
    if (view_empty(v)) {
        return v->origin > cap ? CINT_REFUSAL_EXTENT : CINT_REFUSAL_NONE;
    }
    if (!view_span(v, &lo, &hi)) {
        return CINT_REFUSAL_SIZE;
    }
    return lo < 0 || hi >= cap ? CINT_REFUSAL_EXTENT : CINT_REFUSAL_NONE;
}

/* Each Bool of each element of a nonempty view holds 0 or 1. */
static bool view_bools(const unsigned char *base, const cint_view *v, int64_t es, const uint32_t *off, uint32_t n)
{
    int64_t idx[CINT_MAX_RANK] = {0};
    uint32_t d;
    for (;;) {
        int64_t e = v->origin;
        for (d = 0u; d < v->rank; d++) {
            e += idx[d] * v->stride[d];
        }
        for (uint32_t k = 0u; k < n; k++) {
            if (base[e * es + (int64_t)off[k]] > 1u) {
                return false;
            }
        }
        for (d = v->rank; d > 0u; d--) {
            if (++idx[d - 1u] < v->shape[d - 1u]) {
                break;
            }
            idx[d - 1u] = 0;
        }
        if (d == 0u) {
            return true;
        }
    }
}

/* The constraints of SPEC-02 V-1 that need no registration: a nonnegative
 * origin and extents (EXTENT), a product of extents and declared lower bounds
 * whose last index base + n - 1 are I64 values (SIZE). A lower bound changes
 * no address (V-7, V-8), so nothing else reads it. */
static uint32_t view_dims(const cint_view *v)
{
    int64_t count = 1;
    bool wide = false;
    if (v->origin < 0) {
        return CINT_REFUSAL_EXTENT;
    }
    for (uint32_t d = 0u; d < v->rank; d++) {
        if (v->shape[d] < 0) {
            return CINT_REFUSAL_EXTENT;
        }
        if (v->shape[d] > 0 && v->lower[d] > INT64_MAX - (v->shape[d] - 1)) {
            return CINT_REFUSAL_SIZE;
        }
        if (v->shape[d] == 0) {
            count = 0;
        } else if (count != 0 && (wide || count > INT64_MAX / v->shape[d])) {
            wide = true;
        } else {
            count *= v->shape[d];
        }
    }
    return wide && count != 0 ? CINT_REFUSAL_SIZE : CINT_REFUSAL_NONE;
}

/* The refusal checks of one parameter: CINT_REFUSAL_NONE with b->ptr written,
 * or the reason (cint_rt.h section 6a'). */
static uint32_t bind_form(cint_ctx *ctx, cint_bind *b)
{
    static const uint8_t zero[6] = {0};
    const cint_view *v = b->view;
    const buffer_slot *s;
    int64_t es;
    uint32_t reason;
    if (v == NULL || b->want == NULL || b->mode > CINT_MODE_INOUT || b->rank < 1u || b->rank > CINT_MAX_RANK ||
        (b->bool_count > 0u && b->bools == NULL) || !element_type_valid(b->want, b->elem_bytes)) {
        return CINT_REFUSAL_CALL;
    }
    if (!type_known(&v->type) || memcmp(v->reserved, zero, sizeof zero) != 0) {
        return CINT_REFUSAL_TYPE;
    }
    if (v->rank < 1u || v->rank > CINT_MAX_RANK) {
        return CINT_REFUSAL_RANK;
    }
    s = find_buffer(ctx, v->buffer);
    if (s == NULL || s->live == 0u) {
        return s == NULL ? CINT_REFUSAL_BUFFER : CINT_REFUSAL_GENERATION;
    }
    if (v->perm > s->perm) {
        return CINT_REFUSAL_PERMISSION;
    }
    if (b->mode != CINT_MODE_IN && s->leases > 0u) {
        return CINT_REFUSAL_LEASED;
    }
    if (s->typed != 0u && (!type_equal(&v->type, &s->type) ||
                           (type_equal(&v->type, b->want) && s->elem_bytes != b->elem_bytes))) {
        return CINT_REFUSAL_TYPE;
    }
    es = view_elem_bytes(b, s);
    if (es == 0 && s->typed == 0u) {
        return CINT_REFUSAL_TYPE;  /* no storage: a typed registration only */
    }
    reason = view_dims(v);
    if (reason != CINT_REFUSAL_NONE) {
        return reason;
    }
    b->ptr = NULL;
    if (es < 0) {
        return CINT_REFUSAL_NONE;  /* check 3 faults it */
    }
    reason = view_within(v, s->typed != 0u || es == 0 ? s->extent : s->bytes / es);
    if (reason != CINT_REFUSAL_NONE) {
        return reason;
    }
    if (es > 0) {
        if (((uintptr_t)s->base + (uintptr_t)(v->origin * es)) % (uintptr_t)elem_align(es) != 0u) {
            return CINT_REFUSAL_ALIGN;
        }
        if (!view_empty(v) && b->bool_count > 0u && type_equal(&v->type, b->want) &&
            !view_bools(s->base, v, es, b->bools, b->bool_count)) {
            return CINT_REFUSAL_BOOL;
        }
        b->ptr = s->base == NULL ? NULL : (void *)(s->base + v->origin * es);
    }
    return CINT_REFUSAL_NONE;
}

static cint_fault_record *bind_fault(cint_ctx *ctx, cint_site site, uint16_t code, const char *op, uint32_t param)
{
    cint_fault_record *rec = cint_rt_record_begin(ctx, site, code);
    if (rec != NULL) {
        cint_rt_record_op(rec, op);
        cint_rt_record_operand(rec, CINT_TAG_I64, (uint64_t)param);
    }
    return rec;
}

bool cint_rt_bind_views(cint_ctx *ctx, cint_site site, cint_bind *b, uint32_t count)
{
    cint_fault_record *rec;
    uint32_t i, reason;
    if (!bind_ready(ctx, b, count)) {
        return false;
    }
    for (i = 0u; i < count; i++) {
        reason = bind_form(ctx, &b[i]);
        if (reason != CINT_REFUSAL_NONE) {
            return bind_refused(ctx, reason);
        }
    }
    for (i = 0u; i < count; i++) {  /* F-5 check 1 */
        const buffer_slot *s = find_buffer(ctx, b[i].view->buffer);
        if (s->generation != b[i].view->generation) {
            rec = bind_fault(ctx, site, (uint16_t)CINT_E_STALE_HANDLE, "bind.stale", b[i].param);
            if (rec != NULL) {
                cint_rt_record_operand(rec, CINT_TAG_U64, b[i].view->generation);
                rec->has_limit = 1u;
                cint_rt_tvalue_scalar(&rec->limit, CINT_TAG_U64, s->generation);
            }
            return false;
        }
    }
    for (i = 0u; i < count; i++) {  /* check 2 */
        if (b[i].mode != CINT_MODE_IN && b[i].view->perm != CINT_VIEW_WRITE) {
            (void)bind_fault(ctx, site, (uint16_t)CINT_E_ALIAS, "bind.permission", b[i].param);
            return false;
        }
    }
    for (i = 0u; i < count; i++) {  /* check 3: element type, then rank */
        if (!type_equal(&b[i].view->type, b[i].want)) {
            (void)bind_fault(ctx, site, (uint16_t)CINT_E_UNSUPPORTED, "bind.type", b[i].param);
            return false;
        }
        if (b[i].view->rank != b[i].rank) {
            rec = bind_fault(ctx, site, (uint16_t)CINT_E_SHAPE, "bind.type", b[i].param);
            if (rec != NULL) {
                cint_rt_record_operand(rec, CINT_TAG_I64, (uint64_t)b[i].view->rank);
                record_limit_i64(rec, (int64_t)b[i].rank);
            }
            return false;
        }
    }
    return true;
}

static int64_t magnitude(int64_t x)
{
    return x < 0 ? -x : x;  /* strides of a bound nonempty view are above INT64_MIN */
}

/* SPEC-02 A-7 for a bound view. */
static bool view_injective(const cint_view *v)
{
    uint32_t dims[CINT_MAX_RANK], n = 0u, k, j;
    int64_t reach = 0;
    if (view_empty(v)) {
        return true;
    }
    for (k = 0u; k < v->rank; k++) {
        if (v->shape[k] > 1) {
            for (j = n; j > 0u && magnitude(v->stride[dims[j - 1u]]) > magnitude(v->stride[k]); j--) {
                dims[j] = dims[j - 1u];
            }
            dims[j] = k;
            n++;
        }
    }
    for (k = 0u; k < n; k++) {
        if (magnitude(v->stride[dims[k]]) < 1 + reach) {
            return false;
        }
        reach += magnitude(v->stride[dims[k]]) * (v->shape[dims[k]] - 1);
    }
    return true;
}

static uint64_t gcd_u64(uint64_t a, uint64_t b)
{
    while (b != 0u) {
        uint64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

/* SPEC-02 A-5 T0 to T2 for two bound views: true when they are disjoint. */
static bool bind_disjoint(cint_ctx *ctx, const cint_bind *p, const cint_bind *q)
{
    const cint_view *a = p->view, *b = q->view;
    const buffer_slot *sa = find_buffer(ctx, a->buffer), *sb = find_buffer(ctx, b->buffer);
    int64_t ea = view_elem_bytes(p, sa), eb = view_elem_bytes(q, sb), alo, ahi, blo, bhi;
    uint64_t g = 0u;
    if (view_empty(a) || view_empty(b)) {
        return true;  /* T0 */
    }
    (void)view_span(a, &alo, &ahi);
    (void)view_span(b, &blo, &bhi);
    if (ea > 0 && eb > 0) {  /* T1 over bytes, across registrations (H-13) */
        uintptr_t x = (uintptr_t)sa->base + (uintptr_t)(alo * ea), y = (uintptr_t)sb->base + (uintptr_t)(blo * eb);
        if (x + (uintptr_t)((ahi - alo + 1) * ea) <= y || y + (uintptr_t)((bhi - blo + 1) * eb) <= x) {
            return true;
        }
    } else if (ea == 0 && eb == 0) {
        /* No storage: element offsets within one registration; distinct
         * registrations are distinct allocations. */
        if (a->buffer != b->buffer || ahi < blo || bhi < alo) {
            return true;
        }
    } else {
        return true;  /* a record with no storage shares no bytes */
    }
    if (a->buffer == b->buffer && ea == eb) {  /* T2 */
        for (uint32_t d = 0u; d < a->rank; d++) {
            g = a->shape[d] > 1 ? gcd_u64(g, (uint64_t)magnitude(a->stride[d])) : g;
        }
        for (uint32_t d = 0u; d < b->rank; d++) {
            g = b->shape[d] > 1 ? gcd_u64(g, (uint64_t)magnitude(b->stride[d])) : g;
        }
        if (g >= 2u && (uint64_t)a->origin % g != (uint64_t)b->origin % g) {
            return true;
        }
    }
    return false;
}

/* Descriptor k of an E_ALIAS record from a dispatch (rt/OPEN.md RT-OQ-35). */
static void describe_view(cint_ctx *ctx, uint32_t k, uint32_t elem, uint32_t write, int64_t rank, int64_t origin,
                          const int64_t *shape, const int64_t *stride)
{
    cint_fault_descriptor *d = &ctx->desc[k];
    memset(d, 0, sizeof *d);
    d->elem = elem;
    d->write = write;
    d->rank = rank;
    d->origin = origin;
    memcpy(d->shape, shape, sizeof d->shape);
    memcpy(d->stride, stride, sizeof d->stride);
    ctx->desc_count = k + 1u;
}

/* F-5 checks 6 and 7 of bound views; a bind.alias record gets the two views as
 * its descriptors when `describe` is set (a host-issued dispatch). */
static bool bind_disjoint_all(cint_ctx *ctx, cint_site site, const cint_bind *b, uint32_t count, bool describe)
{
    uint32_t i, j, k;
    for (i = 0u; i < count; i++) {  /* F-5 check 6 */
        if (b[i].mode != CINT_MODE_IN && !view_injective(b[i].view)) {
            (void)bind_fault(ctx, site, (uint16_t)CINT_E_ALIAS, "bind.injective", b[i].param);
            return false;
        }
    }
    for (i = 0u; i < count; i++) {  /* check 7: pairs already decided are skipped */
        for (j = 0u; j < count && b[i].mode != CINT_MODE_IN; j++) {
            if (j != i && (j > i || b[j].mode == CINT_MODE_IN) && !bind_disjoint(ctx, &b[i], &b[j])) {
                (void)cint_fault_alias(ctx, site, b[i].param, b[j].param);
                for (k = 0u; describe && ctx->head.faulted != 0u && k < 2u; k++) {
                    const cint_view *v = (k == 0u ? &b[i] : &b[j])->view;
                    describe_view(ctx, k, v->type.code, v->perm == CINT_VIEW_WRITE, v->rank, v->origin, v->shape,
                                  v->stride);
                }
                return false;
            }
        }
    }
    return true;
}

bool cint_rt_bind_finish(cint_ctx *ctx, cint_site site, const cint_bind *b, uint32_t count)
{
    /* Check 8 refuses nothing: a body takes a view of any rank and stride (RT-OQ-38). */
    return bind_ready(ctx, b, count) && bind_disjoint_all(ctx, site, b, count, false);
}

void cint_rt_dispatch_host(cint_ctx *ctx, const cint_bind *b, uint32_t count)
{
    if (ctx != NULL && ctx->head.in_entry != 0u && (count == 0u || b != NULL)) {
        ctx->host_binds = b;
        ctx->host_count = count;
    }
}

bool cint_rt_dispatch_bound(cint_ctx *ctx, cint_site site)
{
    const cint_bind *b = ctx->host_binds;
    uint32_t i, n = ctx->host_count;
    if (cint_rt_faulted(ctx)) {
        return false;
    }
    ctx->host_count = 0u;  /* once per entry */
    if (!bind_disjoint_all(ctx, site, b, n, true)) {
        return false;
    }
    for (i = 0u; i < n; i++) {  /* check 8: M-30a */
        const buffer_slot *s = find_buffer(ctx, b[i].view->buffer);
        if (b[i].mode != CINT_MODE_IN && s != NULL && s->storage == STORAGE_BORROWED &&
            s->publish != CINT_PUBLISH_COPY) {
            (void)bind_fault(ctx, site, (uint16_t)CINT_E_UNSUPPORTED, "bind.limit", b[i].param);
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------------------- */
/* The registry of SPEC-03 5.3 (cint_rt.h section 6a).                        */

/* The element size of a type the registry takes (A-13): Bool and I8..U64 by
 * width, a record by config.module's layout table; -1 for a type this runtime
 * does not have or a record it has no layout for. */
int64_t cint_rt_type_elem_bytes(const cint_ctx *ctx, const cint_type *t)
{
    if (!type_known(t)) {
        return -1;
    }
    if (t->code == CINT_TAG_RECORD) {
        if (ctx->module == NULL || t->record_id >= ctx->module->record_count) {
            return -1;
        }
        return (int64_t)ctx->module->records[t->record_id].bytes;
    }
    return t->code == CINT_TAG_BOOL ? 1 : (int64_t)(cint_rt_tag_width(t->code) / 8u);
}

cint_status cint_buffer_register(cint_ctx *ctx, const cint_buffer_desc *desc, cint_buffer_id *out_id,
                                 uint64_t *out_generation)
{
    static const uint8_t zero[sizeof(cint_sync)] = {0};
    buffer_slot *s;
    int64_t es;
    void *base;
    if (ctx == NULL || out_id == NULL || out_generation == NULL) {
        return CINT_REFUSED;
    }
    if (!cint_rt_busy_take(ctx)) {
        return CINT_BUSY;
    }
    if (desc == NULL || desc->size != (uint32_t)sizeof *desc || desc->writable > 1u || desc->mem.reserved != 0u ||
        desc->sync.reserved != 0u || desc->publish > CINT_PUBLISH_COPY ||
        (desc->publish != CINT_PUBLISH_NONE && desc->writable == 0u)) {
        return registry_refused(ctx, CINT_REFUSAL_CALL);
    }
    if (desc->mem.kind != CINT_MEM_HOST || desc->device != 0u || memcmp(&desc->sync, zero, sizeof zero) != 0) {
        return registry_refused(ctx, CINT_REFUSAL_UNSUPPORTED);
    }
    es = cint_rt_type_elem_bytes(ctx, &desc->type);
    if (es < 0) {
        return registry_refused(ctx, CINT_REFUSAL_TYPE);
    }
    if (desc->extent < 0) {
        return registry_refused(ctx, CINT_REFUSAL_EXTENT);
    }
    base = es == 0 ? NULL : desc->mem.u.host.ptr;
    if (es != 0 && (desc->extent > INT64_MAX / es ||
                    (uint64_t)(desc->extent * es) > UINTPTR_MAX - (uintptr_t)base)) {
        return registry_refused(ctx, CINT_REFUSAL_SIZE);
    }
    if ((base == NULL && es * desc->extent != 0) || (uintptr_t)base % (uintptr_t)elem_align(es) != 0u) {
        return registry_refused(ctx, CINT_REFUSAL_ALIGN);
    }
    s = slot_take(ctx);
    if (s == NULL) {
        cint_rt_busy_give(ctx);
        return CINT_RESOURCE;
    }
    slot_fill(ctx, s, base, es * desc->extent, (uint8_t)desc->writable, &desc->type, es, desc->extent);
    s->publish = (uint8_t)desc->publish;
    *out_id = s->id;
    *out_generation = s->generation;
    cint_rt_busy_give(ctx);
    return CINT_OK;
}

cint_status cint_buffer_create(cint_ctx *ctx, cint_type type, int64_t extent, uint32_t device,
                               cint_buffer_id *out_id, uint64_t *out_generation)
{
    buffer_slot *s;
    int64_t es;
    void *base = NULL;
    if (ctx == NULL || out_id == NULL || out_generation == NULL) {
        return CINT_REFUSED;
    }
    if (!cint_rt_busy_take(ctx)) {
        return CINT_BUSY;
    }
    if (device != 0u) {
        return registry_refused(ctx, CINT_REFUSAL_UNSUPPORTED);
    }
    es = cint_rt_type_elem_bytes(ctx, &type);
    if (es < 0) {
        return registry_refused(ctx, CINT_REFUSAL_TYPE);
    }
    if (extent < 0) {
        return registry_refused(ctx, CINT_REFUSAL_EXTENT);
    }
    if (es != 0 && extent > INT64_MAX / es) {
        return registry_refused(ctx, CINT_REFUSAL_SIZE);
    }
    if (ctx->last_id == UINT64_MAX || (ctx->buffer_used == ctx->buffer_cap && slot_take(ctx) == NULL)) {
        cint_rt_busy_give(ctx);
        return CINT_RESOURCE;
    }
    if (es * extent > 0) {
        base = ctx->allocator.alloc(ctx->allocator.user, (size_t)(es * extent), 8u);
        if (base == NULL) {
            cint_rt_busy_give(ctx);
            return CINT_RESOURCE;
        }
        memset(base, 0, (size_t)(es * extent));
    }
    s = slot_take(ctx);
    slot_fill(ctx, s, base, es * extent, CINT_VIEW_WRITE, &type, es, extent);
    s->storage = STORAGE_CREATED;
    *out_id = s->id;
    *out_generation = s->generation;
    cint_rt_busy_give(ctx);
    return CINT_OK;
}

cint_status cint_buffer_read(cint_ctx *ctx, cint_view view, void *dst, size_t dst_bytes)
{
    static const uint8_t zero[6] = {0};
    const cint_view *v = &view;
    const buffer_slot *s;
    int64_t idx[CINT_MAX_RANK] = {0}, es, cap, count;
    uint32_t reason, d;
    unsigned char *out = dst;
    if (ctx == NULL) {
        return CINT_REFUSED;
    }
    if (!cint_rt_busy_take(ctx)) {
        return CINT_BUSY;
    }
    s = find_buffer(ctx, v->buffer);
    if (s == NULL || s->live == 0u || s->generation != v->generation) {
        return registry_refused(ctx, s == NULL ? CINT_REFUSAL_BUFFER : CINT_REFUSAL_GENERATION);
    }
    if (!type_known(&v->type) || memcmp(v->reserved, zero, sizeof zero) != 0 ||
        (s->typed != 0u && !type_equal(&v->type, &s->type)) || (s->typed == 0u && v->type.code == CINT_TAG_RECORD)) {
        return registry_refused(ctx, CINT_REFUSAL_TYPE);
    }
    if (v->rank < 1u || v->rank > CINT_MAX_RANK) {
        return registry_refused(ctx, CINT_REFUSAL_RANK);
    }
    if (v->perm > s->perm) {
        return registry_refused(ctx, CINT_REFUSAL_PERMISSION);
    }
    reason = view_dims(v);
    if (reason != CINT_REFUSAL_NONE) {
        return registry_refused(ctx, reason);
    }
    es = s->typed != 0u ? s->elem_bytes : (v->type.code == CINT_TAG_BOOL ? 1 : (int64_t)(cint_rt_tag_width(v->type.code) / 8u));
    cap = s->typed != 0u || es == 0 ? s->extent : s->bytes / es;
    for (d = 0u, count = view_empty(v) ? 0 : 1; d < v->rank && count != 0; d++) {
        count *= v->shape[d];  /* view_dims bounds the product of nonzero extents */
    }
    if ((es != 0 && count > INT64_MAX / es) || (uint64_t)(count * es) != (uint64_t)dst_bytes ||
        (dst == NULL && dst_bytes != 0u)) {
        return registry_refused(ctx, CINT_REFUSAL_SIZE);
    }
    reason = view_within(v, cap);
    if (reason != CINT_REFUSAL_NONE) {
        return registry_refused(ctx, reason);
    }
    for (int64_t at = 0; es > 0 && count > 0;) {
        int64_t e = v->origin;
        for (d = 0u; d < v->rank; d++) {
            e += idx[d] * v->stride[d];
        }
        memcpy(out + at, s->base + e * es, (size_t)es);
        at += es;
        for (d = v->rank; d > 0u; d--) {
            if (++idx[d - 1u] < v->shape[d - 1u]) {
                break;
            }
            idx[d - 1u] = 0;
        }
        if (d == 0u) {
            break;
        }
    }
    cint_rt_busy_give(ctx);
    return CINT_OK;
}

cint_status cint_buffer_lease(cint_ctx *ctx, cint_buffer_id id, const void **out_ptr, uint64_t *out_lease)
{
    buffer_slot *s;
    if (ctx == NULL || out_ptr == NULL || out_lease == NULL) {
        return CINT_REFUSED;
    }
    if (!cint_rt_busy_take(ctx)) {
        return CINT_BUSY;
    }
    s = find_buffer(ctx, id);
    if (s == NULL || s->live == 0u || s->storage != STORAGE_CREATED) {
        return registry_refused(ctx, CINT_REFUSAL_LEASE);
    }
    for (uint32_t i = 0u; i < ctx->buffer_cap; i++) {
        if (ctx->leases[i].lease == 0u && ctx->last_lease < UINT64_MAX && s->leases < UINT32_MAX) {
            ctx->leases[i].lease = ++ctx->last_lease;
            ctx->leases[i].slot = (uint32_t)(s - ctx->buffers);
            s->leases++;
            *out_ptr = s->base;
            *out_lease = ctx->last_lease;
            cint_rt_busy_give(ctx);
            return CINT_OK;
        }
    }
    cint_rt_busy_give(ctx);
    return CINT_RESOURCE;
}

cint_status cint_lease_end(cint_ctx *ctx, uint64_t lease)
{
    if (ctx == NULL) {
        return CINT_REFUSED;
    }
    if (!cint_rt_busy_take(ctx)) {
        return CINT_BUSY;
    }
    for (uint32_t i = 0u; i < ctx->buffer_cap && lease != 0u; i++) {
        if (ctx->leases[i].lease == lease) {
            ctx->buffers[ctx->leases[i].slot].leases--;
            ctx->leases[i].lease = 0u;
            cint_rt_busy_give(ctx);
            return CINT_OK;
        }
    }
    return registry_refused(ctx, CINT_REFUSAL_LEASE);
}

/* ------------------------------------------------------------------------- */
/* Print output (SPEC-04 LS-193 to LS-197) and exact formatting (IM-90).      */

size_t cint_format_value(uint32_t tag, uint64_t bits, char out[CINT_FORMAT_MAX])
{
    char digits[CINT_FORMAT_MAX];
    size_t nd = 0u, len = 0u;
    uint64_t mag;
    bool negative = false;
    if (tag == CINT_TAG_BOOL) {
        if (bits > 1u) {
            return 0u;
        }
        memcpy(out, bits != 0u ? "true" : "false", bits != 0u ? 4u : 5u);
        return bits != 0u ? 4u : 5u;
    }
    if (!is_scalar_tag(tag)) {
        return 0u;
    }
    mag = canon(tag, bits);
    negative = cint_rt_tag_signed(tag) && (mag >> 63) != 0u;
    mag = negative ? (uint64_t)0 - mag : mag;
    do {
        digits[nd++] = (char)('0' + (int)(mag % 10u));
        mag /= 10u;
    } while (mag != 0u);
    if (negative) {
        out[len++] = '-';
    }
    while (nd > 0u) {
        out[len++] = digits[--nd];
    }
    return len;
}

bool cint_rt_print_begin(cint_ctx *ctx, cint_site site)
{
    if (ctx->head.faulted != 0u) {
        return false;
    }
    if (ctx->head.in_entry == 0u) {
        return fault_refused(ctx, site);
    }
    ctx->print_site = site;
    ctx->print_len = 0u;
    ctx->print_open = 1u;
    return true;
}

bool cint_rt_print_bytes(cint_ctx *ctx, const void *bytes, size_t len)
{
    if (ctx->head.faulted != 0u) {
        return false;
    }
    if (ctx->print_open == 0u || (bytes == NULL && len != 0u)) {
        return fault_refused(ctx, ctx->print_site);
    }
    if (len > PRINT_MAX - ctx->print_len) {
        return fault_host(ctx, ctx->print_site, "host.resource", false, CINT_OK);
    }
    if (ctx->print_len + len > ctx->print_cap) {
        size_t cap = ctx->print_cap > 0u ? ctx->print_cap : 256u;
        uint8_t *p;
        while (cap < ctx->print_len + len) {
            cap = cap <= PRINT_MAX / 2u ? cap * 2u : PRINT_MAX;
        }
        p = (uint8_t *)ctx->allocator.alloc(ctx->allocator.user, cap, 1u);
        if (p == NULL) {
            return fault_host(ctx, ctx->print_site, "host.resource", false, CINT_OK);
        }
        if (ctx->print_buf != NULL) {
            memcpy(p, ctx->print_buf, ctx->print_len);
            ctx->allocator.release(ctx->allocator.user, ctx->print_buf, ctx->print_cap, 1u);
        }
        ctx->print_buf = p;
        ctx->print_cap = cap;
    }
    if (len > 0u) {
        memcpy(ctx->print_buf + ctx->print_len, bytes, len);
        ctx->print_len += len;
    }
    return true;
}

bool cint_rt_print_value(cint_ctx *ctx, uint32_t tag, uint64_t bits)
{
    char text[CINT_FORMAT_MAX];
    size_t len;
    if (ctx->head.faulted != 0u) {
        return false;
    }
    len = cint_format_value(tag, bits, text);
    if (len == 0u) {
        return fault_refused(ctx, ctx->print_site);
    }
    return cint_rt_print_bytes(ctx, text, len);
}

/* Whether a form passes the static rules of SPEC-04 LS-213 (cint_rt.h section 6c). */
static bool format_admits(uint32_t tag, uint32_t form, uint32_t fill, uint64_t width, uint64_t scale)
{
    uint32_t kind = form & CINT_FORMAT_KIND;
    uint32_t align = form & CINT_FORMAT_CENTER;
    uint32_t sign = form & (CINT_FORMAT_PLUS | CINT_FORMAT_SPACE);
    uint32_t group = form & (CINT_FORMAT_UNDERSCORE | CINT_FORMAT_COMMA);
    bool alt = (form & CINT_FORMAT_ALT) != 0u;
    bool zero = (form & CINT_FORMAT_ZERO) != 0u;
    if (form > 4095u || kind < 1u || kind > 6u || sign == (CINT_FORMAT_PLUS | CINT_FORMAT_SPACE) ||
        group == (CINT_FORMAT_UNDERSCORE | CINT_FORMAT_COMMA) || fill > 0x10ffffu ||
        (fill >= 0xd800u && fill <= 0xdfffu) || (zero && align != 0u) || (width != 0u && align == 0u && !zero) ||
        (zero && group != 0u)) {
        return false;
    }
    if (tag == CINT_TAG_BOOL || kind == 6u) {
        return (tag == CINT_TAG_BOOL ? kind == 1u : is_scalar_tag(tag)) && sign == 0u && !alt && !zero &&
               group == 0u && scale == 0u;
    }
    if (!is_scalar_tag(tag)) {
        return false;
    }
    if (kind == 1u) {
        return !alt && (scale == 0u || group == 0u);
    }
    return group != CINT_FORMAT_COMMA && scale == 0u;
}

/* The UTF-8 encoding of scalar value c (at most 0x10FFFF, not a surrogate). */
static size_t utf8_encode(uint32_t c, uint8_t out[4])
{
    if (c < 0x80u) {
        out[0] = (uint8_t)c;
        return 1u;
    }
    if (c < 0x800u) {
        out[0] = (uint8_t)(0xc0u | (c >> 6));
        out[1] = (uint8_t)(0x80u | (c & 0x3fu));
        return 2u;
    }
    if (c < 0x10000u) {
        out[0] = (uint8_t)(0xe0u | (c >> 12));
        out[1] = (uint8_t)(0x80u | ((c >> 6) & 0x3fu));
        out[2] = (uint8_t)(0x80u | (c & 0x3fu));
        return 3u;
    }
    out[0] = (uint8_t)(0xf0u | (c >> 18));
    out[1] = (uint8_t)(0x80u | ((c >> 12) & 0x3fu));
    out[2] = (uint8_t)(0x80u | ((c >> 6) & 0x3fu));
    out[3] = (uint8_t)(0x80u | (c & 0x3fu));
    return 4u;
}

/* Prints count copies of the len bytes at unit (len from 1 to 4). Only the copies that are
 * printed are filled, so a count of 0 costs nothing (G-C2 review PORT-1). */
static bool print_repeat(cint_ctx *ctx, const uint8_t *unit, size_t len, uint64_t count)
{
    uint8_t block[256];
    size_t per = sizeof block / len, i;
    if (count < (uint64_t)per) {
        per = (size_t)count;
    }
    for (i = 0u; i < per; ++i) {
        memcpy(block + i * len, unit, len);
    }
    while (count > 0u) {
        size_t n = count < (uint64_t)per ? (size_t)count : per;
        if (!cint_rt_print_bytes(ctx, block, n * len)) {
            return false;
        }
        count -= (uint64_t)n;
    }
    return true;
}

/* A hole with a format specification (SPEC-04 LS-202 to LS-212), as cint_ref's fmt.render:
 * the fill on the left, the sign and the prefix of `#`, the zeros of `0`, the digits (with
 * their groups, or with the point and the fractional digits of a scale), the fill on the
 * right. The length is known before a byte is staged, so an oversized hole stages nothing. */
bool cint_rt_print_format(cint_ctx *ctx, uint32_t tag, uint64_t bits, uint32_t form, uint32_t fill,
                          uint64_t width, uint64_t scale)
{
    static const char digit_chars[] = "0123456789abcdef0123456789ABCDEF";
    static const uint8_t zero_unit[1] = {'0'};
    uint32_t kind = form & CINT_FORMAT_KIND;
    uint32_t align = form & CINT_FORMAT_CENTER;
    char head[3];   /* the sign, then the `0x`, `0o` or `0b` of `#` */
    char text[80];  /* at most 64 binary digits and 15 separators */
    char frac[20];  /* the fractional digits of a scale, after its run of zeros */
    char rev[64];   /* digits, least significant first */
    uint8_t unit[4];
    size_t nhead = 0u, ntext = 0u, nfrac = 0u, nd = 0u, nunit;
    uint64_t mag = 0u, room, length, zeros = 0u, zpad = 0u, left = 0u, right = 0u;
    bool negative = false;
    if (ctx->head.faulted != 0u) {
        return false;
    }
    if (!format_admits(tag, form, fill, width, scale) || (tag == CINT_TAG_BOOL && bits > 1u) || ctx->print_open == 0u) {
        return fault_refused(ctx, ctx->print_site);
    }
    if (tag == CINT_TAG_BOOL) {
        memcpy(text, bits != 0u ? "true" : "false", bits != 0u ? 4u : 5u);
        ntext = bits != 0u ? 4u : 5u;
    } else {
        mag = canon(tag, bits);
        negative = cint_rt_tag_signed(tag) && (mag >> 63) != 0u;
        mag = negative ? (uint64_t)0 - mag : mag;
    }
    if (kind == 6u && tag != CINT_TAG_BOOL) {
        /* The minimal balanced-ternary literal (SPEC-04 9.3 rule 3); a negative value
         * has the digits of its magnitude with N and P exchanged. */
        text[ntext++] = '0';
        text[ntext++] = 't';
        if (mag == 0u) {
            text[ntext++] = '0';
        }
        while (mag != 0u) {
            uint64_t r = mag % 3u;
            rev[nd++] = r == 0u ? '0' : (r == 1u) != negative ? 'P' : 'N';
            mag = r == 2u ? mag / 3u + 1u : mag / 3u;
        }
        while (nd > 0u) {
            text[ntext++] = rev[--nd];
        }
    } else if (tag != CINT_TAG_BOOL) {
        uint64_t base = kind == 1u ? 10u : kind <= 3u ? 16u : kind == 4u ? 8u : 2u;
        const char *chars = kind == 3u ? digit_chars + 16 : digit_chars;
        size_t every = kind == 1u ? 3u : 4u;
        char sep = (form & CINT_FORMAT_COMMA) != 0u ? ',' : '_';
        if (negative) {
            head[nhead++] = '-';
        } else if ((form & CINT_FORMAT_PLUS) != 0u) {
            head[nhead++] = '+';
        } else if ((form & CINT_FORMAT_SPACE) != 0u) {
            head[nhead++] = ' ';
        }
        if ((form & CINT_FORMAT_ALT) != 0u) {
            head[nhead++] = '0';
            head[nhead++] = base == 16u ? 'x' : base == 8u ? 'o' : 'b';
        }
        do {
            rev[nd++] = chars[mag % base];
            mag /= base;
        } while (mag != 0u);
        if (scale != 0u && (uint64_t)nd > scale) {
            while ((uint64_t)nd > scale) {
                text[ntext++] = rev[--nd];
            }
            text[ntext++] = '.';
        } else if (scale != 0u) {
            text[ntext++] = '0';
            text[ntext++] = '.';
            zeros = scale - (uint64_t)nd;
        }
        if (scale != 0u) {
            while (nd > 0u) {
                frac[nfrac++] = rev[--nd];
            }
        }
        while (nd > 0u) {
            text[ntext++] = rev[--nd];
            if ((form & (CINT_FORMAT_UNDERSCORE | CINT_FORMAT_COMMA)) != 0u && nd > 0u && nd % every == 0u) {
                text[ntext++] = sep;
            }
        }
    }
    /* Every byte so far is ASCII, so the length in bytes is the length in scalar values. */
    room = (uint64_t)(PRINT_MAX - ctx->print_len);
    if (zeros > room) {
        return fault_host(ctx, ctx->print_site, "host.resource", false, CINT_OK);
    }
    length = (uint64_t)(nhead + ntext + nfrac) + zeros;
    if (width > length && (form & CINT_FORMAT_ZERO) != 0u) {
        zpad = width - length;
    } else if (width > length && align == CINT_FORMAT_LEFT) {
        right = width - length;
    } else if (width > length && align == CINT_FORMAT_RIGHT) {
        left = width - length;
    } else if (width > length && align == CINT_FORMAT_CENTER) {
        left = (width - length) / 2u;
        right = width - length - left;
    }
    nunit = utf8_encode(fill, unit);
    if (length > room || zpad > room - length || left + right > (room - length - zpad) / nunit) {
        return fault_host(ctx, ctx->print_site, "host.resource", false, CINT_OK);
    }
    return print_repeat(ctx, unit, nunit, left) && cint_rt_print_bytes(ctx, head, nhead) &&
           print_repeat(ctx, zero_unit, 1u, zpad) && cint_rt_print_bytes(ctx, text, ntext) &&
           print_repeat(ctx, zero_unit, 1u, zeros) && cint_rt_print_bytes(ctx, frac, nfrac) &&
           print_repeat(ctx, unit, nunit, right);
}

bool cint_rt_print_end(cint_ctx *ctx)
{
    static const uint8_t none[1] = {0};
    cint_status st = CINT_OK;
    if (ctx->head.faulted != 0u) {
        return false;
    }
    if (ctx->print_open == 0u) {
        return fault_refused(ctx, ctx->print_site);
    }
    ctx->print_open = 0u;
    if (ctx->output != NULL) {
        st = ctx->output(ctx->output_user, ctx->print_buf != NULL ? ctx->print_buf : none, ctx->print_len);
    }
    ctx->print_len = 0u;
    if (st != CINT_OK) {
        return fault_host(ctx, ctx->print_site, "host.error.print", true, st);
    }
    ctx->effects++;
    return true;
}

/* ------------------------------------------------------------------------- */
/* Program processes (SPEC-06 3.2, 3.4a; D-19).                               */

static cint_status write_stdout(void *user, const uint8_t *bytes, size_t len)
{
    if (len > 0u && fwrite(bytes, 1u, len, stdout) != len) {
        *(bool *)user = true;
        return CINT_RESOURCE;
    }
    return CINT_OK;
}

/* "handle:<N>" sets *handle and returns 1; a nonempty path returns 2; NULL
 * returns 0; anything else -1. */
static int fault_dest_kind(const char *dest, uint64_t *handle)
{
    static const char prefix[] = "handle:";
    const char *p;
    uint64_t v = 0u;
#if defined(_WIN32)
    const uint64_t max = (uint64_t)INTPTR_MAX;
#else
    const uint64_t max = (uint64_t)INT_MAX;
#endif
    if (dest == NULL) {
        return 0;
    }
    if (strncmp(dest, prefix, sizeof prefix - 1u) != 0) {
        return dest[0] != '\0' ? 2 : -1;
    }
    p = dest + (sizeof prefix - 1u);
    if (*p == '\0') {
        return -1;
    }
    for (; *p != '\0'; p++) {
        if (*p < '0' || *p > '9' || v > (max - (uint64_t)(*p - '0')) / 10u) {
            return -1;
        }
        v = v * 10u + (uint64_t)(*p - '0');
    }
    *handle = v;
    return 1;
}

#if defined(_WIN32)
static bool write_handle(HANDLE h, const uint8_t *p, size_t n)
{
    while (n > 0u) {
        DWORD chunk = n > 0x40000000u ? 0x40000000u : (DWORD)n, done = 0;
        if (!WriteFile(h, p, chunk, &done, NULL) || done == 0u) {
            return false;
        }
        p += done;
        n -= done;
    }
    return true;
}

static bool write_record(int kind, const char *path, uint64_t handle, const uint8_t *bytes, size_t len)
{
    HANDLE h;
    wchar_t *wide;
    int wn;
    bool ok;
    if (kind == 1) {
        return write_handle((HANDLE)(uintptr_t)handle, bytes, len);
    }
    wn = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (wn <= 0) {
        return false;
    }
    wide = (wchar_t *)malloc((size_t)wn * sizeof *wide);
    if (wide == NULL) {
        return false;
    }
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, wn) != wn) {
        free(wide);
        return false;
    }
    h = CreateFileW(wide, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wide);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    ok = write_handle(h, bytes, len);
    return CloseHandle(h) != 0 && ok;
}
#else
static bool write_fd(int fd, const uint8_t *p, size_t n)
{
    while (n > 0u) {
        ssize_t done = write(fd, p, n);
        if (done < 0 && errno == EINTR) {
            continue;
        }
        if (done <= 0) {
            return false;
        }
        p += done;
        n -= (size_t)done;
    }
    return true;
}

static bool write_record(int kind, const char *path, uint64_t handle, const uint8_t *bytes, size_t len)
{
    int fd;
    bool ok;
    if (kind == 1) {
        return write_fd((int)handle, bytes, len);
    }
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    if (fd < 0) {
        return false;
    }
    ok = write_fd(fd, bytes, len);
    return close(fd) == 0 && ok;
}
#endif

static int report_fault(const cint_ctx *ctx, int kind, const char *dest, uint64_t handle)
{
    size_t len = cint_fault_encode(&ctx->fault, NULL, 0u);
    uint8_t *bytes;
    bool ok;
    if (len == 0u) {
        return CINT_EXIT_INTERNAL;
    }
    if (kind == 0) {
        return CINT_EXIT_FAULT;
    }
    bytes = (uint8_t *)malloc(len);
    if (bytes == NULL) {
        return CINT_EXIT_ENVIRONMENT;
    }
    (void)cint_fault_encode(&ctx->fault, bytes, len);
    ok = write_record(kind, dest, handle, bytes, len);
    free(bytes);
    return ok ? CINT_EXIT_FAULT : CINT_EXIT_ENVIRONMENT;
}

/* The fuel record (CINT_FUEL_RECORD_DOMAIN, then fuel consumed as I64,
 * little-endian, SPEC-01 IM-144) to the destination of kind `kind`. */
static bool report_fuel(const cint_ctx *ctx, int kind, const char *dest, uint64_t handle)
{
    static const char domain[] = CINT_FUEL_RECORD_DOMAIN;
    uint8_t bytes[CINT_FUEL_RECORD_BYTES];
    uint64_t used = (uint64_t)ctx->head.fuel_used, n = sizeof domain - 1u;
    size_t i;
    /* Shift the value itself: MSVC /O2 vectorized `x >> (8u * i)` with pooled
     * __xmm@ constants, which the EMIT-31 audit of the runtime object rejects. */
    for (i = 0; i < 4u; i++, n >>= 8u) {
        bytes[i] = (uint8_t)n;
    }
    memcpy(bytes + 4, domain, sizeof domain - 1u);
    for (i = 0; i < 8u; i++, used >>= 8u) {
        bytes[4u + (sizeof domain - 1u) + i] = (uint8_t)used;
    }
    return write_record(kind, dest, handle, bytes, sizeof bytes);
}

/* n little-endian bytes of v at p; returns p + n. The value itself is shifted,
 * as in report_fuel. */
static uint8_t *put_u(uint8_t *p, uint64_t v, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++, v >>= 8u) {
        p[i] = (uint8_t)v;
    }
    return p + n;
}

/* The error record (cint_rt.h 6d; RT-OQ-33) of the context's error result. */
static int report_error(const cint_ctx *ctx, int kind, const char *dest, uint64_t handle)
{
    static const char domain[] = CINT_ERROR_RECORD_DOMAIN;
    const cint_error_set *s = ctx->error_set;
    const char *value = s->values != NULL ? s->values[ctx->error_tag - 1u] : NULL;
    size_t nlen = s->name != NULL ? strlen(s->name) : 0u, vlen = value != NULL ? strlen(value) : 0u, len;
    uint8_t *bytes, *p;
    bool ok;
    if (nlen == 0u || vlen == 0u || nlen > UINT32_MAX || vlen > UINT32_MAX || s->tag < CINT_TAG_U8 ||
        s->tag > CINT_TAG_U64 || ctx->error_tag > cint_rt_mask(cint_rt_tag_width(s->tag))) {
        return CINT_EXIT_INTERNAL;
    }
    if (kind == 0) {
        return CINT_EXIT_ERROR_VALUE;
    }
    len = 4u + (sizeof domain - 1u) + 4u + nlen + 4u + vlen + 4u + 8u;
    bytes = (uint8_t *)malloc(len);
    if (bytes == NULL) {
        return CINT_EXIT_ENVIRONMENT;
    }
    p = put_u(bytes, sizeof domain - 1u, 4u);
    memcpy(p, domain, sizeof domain - 1u);
    p = put_u(p + (sizeof domain - 1u), nlen, 4u);
    memcpy(p, s->name, nlen);
    p = put_u(p + nlen, vlen, 4u);
    memcpy(p, value, vlen);
    p = put_u(p + vlen, s->tag, 4u);
    (void)put_u(p, ctx->error_tag, 8u);
    ok = write_record(kind, dest, handle, bytes, len);
    free(bytes);
    return ok ? CINT_EXIT_ERROR_VALUE : CINT_EXIT_ENVIRONMENT;
}

/* The `state.global` lines of `t` (cint_state_render) to the destination of
 * kind `kind`: CINT_EXIT_OK, CINT_EXIT_ENVIRONMENT when they cannot be
 * written, or CINT_EXIT_INTERNAL for a malformed table. */
static int report_state(const cint_ctx *ctx, const cint_state_table *t, int kind, const char *dest,
                        uint64_t handle)
{
    uint64_t cap = 1u;
    size_t n;
    char *text;
    bool ok;
    for (uint32_t i = 0u; i < t->count; i++) {
        const cint_state *s = t->states != NULL ? t->states[i] : NULL;
        if (s == NULL || (s->var_count > 0u && s->vars == NULL)) {
            return CINT_EXIT_INTERNAL;
        }
        for (uint32_t j = 0u; j < s->var_count; j++) {
            /* A line is at most 48 bytes besides its module path and name. */
            cap += 48u + CINT_FAULT_MAX_PATH + s->vars[j].name_len;
            if (cap > PRINT_MAX) {
                return CINT_EXIT_INTERNAL;
            }
        }
    }
    text = (char *)malloc((size_t)cap);
    if (text == NULL) {
        return CINT_EXIT_ENVIRONMENT;
    }
    n = cint_state_render(ctx, t, text, (size_t)cap);
    ok = n != SIZE_MAX && write_record(kind, dest, handle, (const uint8_t *)text, n);
    free(text);
    return n == SIZE_MAX ? CINT_EXIT_INTERNAL : ok ? CINT_EXIT_OK : CINT_EXIT_ENVIRONMENT;
}

int cint_program_run(const cint_program *program, cint_main_fn main_fn, int64_t fuel, const char *fault_dest)
{
    return cint_program_run_state(program, main_fn, fuel, fault_dest, NULL, NULL, NULL);
}

int cint_program_run_fuel(const cint_program *program, cint_main_fn main_fn, int64_t fuel, const char *fault_dest,
                          const char *fuel_dest)
{
    return cint_program_run_state(program, main_fn, fuel, fault_dest, fuel_dest, NULL, NULL);
}

int cint_program_run_state(const cint_program *program, cint_main_fn main_fn, int64_t fuel, const char *fault_dest,
                           const char *fuel_dest, const char *state_dest, const cint_state_table *state)
{
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL;
    cint_status st;
    uint64_t handle = 0u, fuel_handle = 0u, state_handle = 0u;
    bool out_failed = false, ran;
    int kind = fault_dest_kind(fault_dest, &handle), fuel_kind = fault_dest_kind(fuel_dest, &fuel_handle);
    int state_kind = fault_dest_kind(state_dest, &state_handle), status = CINT_EXIT_OK;
    if (kind < 0 || fuel_kind < 0 || state_kind < 0) {
        return CINT_EXIT_USAGE;
    }
    if (program == NULL || main_fn == NULL) {
        return CINT_EXIT_INTERNAL;
    }
#if defined(_WIN32)
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) {
        return CINT_EXIT_ENVIRONMENT;
    }
#endif
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = program;
    cfg.output = write_stdout;
    cfg.output_user = &out_failed;
    st = cint_ctx_create(&cfg, &ctx);
    if (st != CINT_OK) {
        return st == CINT_RESOURCE ? CINT_EXIT_ENVIRONMENT : CINT_EXIT_INTERNAL;
    }
    st = main_fn(ctx, fuel);
    /* stdout is flushed before a fault is reported and before exit (3.4a). */
    if (fflush(stdout) != 0 || ferror(stdout) != 0) {
        out_failed = true;
    }
    ran = st == CINT_OK || (st == CINT_FAULT && ctx->head.faulted != 0u);
    if (!out_failed && ran && fuel_kind != 0 && !report_fuel(ctx, fuel_kind, fuel_dest, fuel_handle)) {
        out_failed = true;  /* the fuel record could not be written: an environment failure */
    }
    if (!out_failed && ran && state_kind != 0 && state != NULL) {
        status = report_state(ctx, state, state_kind, state_dest, state_handle);
        out_failed = status == CINT_EXIT_ENVIRONMENT;
    }
    if (out_failed) {
        status = CINT_EXIT_ENVIRONMENT;
    } else if (status != CINT_EXIT_OK) {
        /* the state table is malformed: CINT_EXIT_INTERNAL */
    } else if (st == CINT_OK) {
        status = ctx->error_set != NULL ? report_error(ctx, kind, fault_dest, handle) : CINT_EXIT_OK;
    } else if (st == CINT_FAULT && ctx->head.faulted != 0u) {
        status = report_fault(ctx, kind, fault_dest, handle);
    } else {
        status = CINT_EXIT_INTERNAL;
    }
    cint_ctx_destroy(ctx);
    return status;
}

/* ------------------------------------------------------------------------- */
/* SHA-256 (FIPS 180-4), for the bridge's MANIFEST and receipts.              */

static const uint32_t sha_k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

static uint32_t sha_ror(uint32_t x, unsigned r)
{
    return (x >> r) | (x << (32u - r));
}

static void sha_block(uint32_t *h, const uint8_t *p)
{
    uint32_t w[64], s[8], t1, t2;
    unsigned i;
    for (i = 0; i < 16u; i++) {
        w[i] = (uint32_t)p[4u * i] << 24 | (uint32_t)p[4u * i + 1u] << 16 | (uint32_t)p[4u * i + 2u] << 8 | p[4u * i + 3u];
    }
    for (i = 16; i < 64u; i++) {
        w[i] = w[i - 16u] + (sha_ror(w[i - 15u], 7) ^ sha_ror(w[i - 15u], 18) ^ (w[i - 15u] >> 3)) + w[i - 7u] +
               (sha_ror(w[i - 2u], 17) ^ sha_ror(w[i - 2u], 19) ^ (w[i - 2u] >> 10));
    }
    memcpy(s, h, sizeof s);
    for (i = 0; i < 64u; i++) {
        t1 = s[7] + (sha_ror(s[4], 6) ^ sha_ror(s[4], 11) ^ sha_ror(s[4], 25)) + ((s[4] & s[5]) ^ (~s[4] & s[6])) +
             sha_k[i] + w[i];
        t2 = (sha_ror(s[0], 2) ^ sha_ror(s[0], 13) ^ sha_ror(s[0], 22)) + ((s[0] & s[1]) ^ (s[0] & s[2]) ^ (s[1] & s[2]));
        memmove(s + 1, s, 7u * sizeof *s);
        s[4] += t1;
        s[0] = t1 + t2;
    }
    for (i = 0; i < 8u; i++) {
        h[i] += s[i];
    }
}

void cint_sha256(const uint8_t *p, size_t n, uint8_t digest[32])
{
    uint32_t h[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    uint8_t tail[128];
    size_t full = n / 64u * 64u, rest = n - full, k = rest < 56u ? 64u : 128u, i;
    uint64_t bits = (uint64_t)n * 8u;
    for (i = 0; i < full; i += 64u) {
        sha_block(h, p + i);
    }
    memset(tail, 0, sizeof tail);
    if (rest > 0u) {
        memcpy(tail, p + full, rest);
    }
    tail[rest] = 0x80u;
    for (i = 0; i < 8u; i++) {
        tail[k - 1u - i] = (uint8_t)(bits >> (8u * i));
    }
    sha_block(h, tail);
    if (k == 128u) {
        sha_block(h, tail + 64);
    }
    for (i = 0; i < 32u; i++) {
        digest[i] = (uint8_t)(h[i / 4u] >> (24u - 8u * (i % 4u)));
    }
}

/* ------------------------------------------------------------------------- */
/* Sites.                                                                     */

bool cint_site_resolve(const cint_program *program, cint_site site, cint_position *out)
{
    const cint_module *m;
    const cint_site_info *s;
    if (program == NULL || program->modules == NULL || site.module >= program->module_count) {
        return false;
    }
    m = program->modules[site.module];
    if (m == NULL || m->sites == NULL || site.index == 0u || site.index >= m->site_count) {
        return false;
    }
    s = &m->sites[site.index];
    out->path = m->path;
    out->path_len = m->path_len;
    out->line = s->line;
    out->column = s->column;
    out->reserved = 0u;
    out->operation = s->operation;
    return true;
}

/* ------------------------------------------------------------------------- */
/* Canonical encoding (SPEC-01 11.2).                                         */

typedef struct writer {
    uint8_t *buf;  /* NULL: count only */
    size_t len;
} writer;

static void put_bytes(writer *w, const void *p, size_t n)
{
    if (w->buf != NULL && n > 0u) {
        memcpy(w->buf + w->len, p, n);
    }
    w->len += n;
}

static void put_le(writer *w, uint64_t v, unsigned n)
{
    uint8_t b[8];
    unsigned i;
    for (i = 0; i < n; i++) {
        b[i] = (uint8_t)(v >> (8u * i));
    }
    put_bytes(w, b, n);
}

static bool tvalue_valid(const cint_tvalue *v)
{
    return v->len >= 1u && v->len <= CINT_TVALUE_MAX;
}

static bool put_position(writer *w, const cint_program *program, cint_site site)
{
    cint_position pos;
    if (!cint_site_resolve(program, site, &pos) || pos.path_len > CINT_FAULT_MAX_PATH) {
        return false;
    }
    put_le(w, pos.path_len, 4u);
    put_bytes(w, pos.path, pos.path_len);
    put_le(w, pos.line, 4u);
    put_le(w, pos.column, 4u);
    return true;
}

static bool encode(const cint_fault_record *r, writer *w)
{
    static const char domain[] = "cint-core-1/fault/v2";
    uint32_t i;
    if (r->code == 0u || r->operation_len == 0u || r->operation_len > CINT_FAULT_MAX_OPERATION ||
        r->operand_count > CINT_FAULT_MAX_OPERANDS || r->stack_count > CINT_RT_MAX_DEPTH ||
        r->address.name_len > sizeof r->address.name) {
        return false;
    }
    /* What the IM-148 reader rejects: an operation outside [a-z0-9._] (IM-130), a
     * phase above 2, and a work item outside phase 1 (IM-106). */
    for (i = 0; i < r->operation_len; i++) {
        char c = r->operation[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_')) {
            return false;
        }
    }
    if (r->has_address && (r->address.phase > 2u || (r->address.has_work_item && r->address.phase != 1u))) {
        return false;
    }
    put_le(w, sizeof domain - 1u, 4u);
    put_bytes(w, domain, sizeof domain - 1u);
    put_le(w, r->code, 2u);
    put_le(w, r->operation_len, 4u);
    put_bytes(w, r->operation, r->operation_len);
    put_le(w, r->operand_count, 4u);
    for (i = 0; i < r->operand_count; i++) {
        if (!tvalue_valid(&r->operands[i])) {
            return false;
        }
        put_bytes(w, r->operands[i].bytes, r->operands[i].len);
    }
    put_le(w, r->has_exact ? 1u : 0u, 1u);
    if (r->has_exact) {
        if (!tvalue_valid(&r->exact) || r->exact.bytes[0] != CINT_TAG_Z) {
            return false;
        }
        put_bytes(w, r->exact.bytes, r->exact.len);
    }
    put_le(w, r->has_limit ? 1u : 0u, 1u);
    if (r->has_limit) {
        if (!tvalue_valid(&r->limit)) {
            return false;
        }
        put_bytes(w, r->limit.bytes, r->limit.len);
    }
    if (!put_position(w, r->program, r->position)) {
        return false;
    }
    /* Version 2 (SPEC-01 IM-149, D-10): a presence byte, then the 32-byte
     * revision; then a presence byte for the source-map digest, which
     * cint-rt-3 has no carrier for and always writes absent (RT-OQ-28). */
    put_le(w, r->has_revision ? 1u : 0u, 1u);
    if (r->has_revision) {
        put_bytes(w, r->revision, 32u);
    }
    put_le(w, 0u, 1u);
    put_le(w, r->has_address ? 1u : 0u, 1u);
    if (r->has_address) {
        put_le(w, r->address.name_len, 4u);
        put_bytes(w, r->address.name, r->address.name_len);
        put_le(w, (uint64_t)r->address.dispatch, 8u);
        put_le(w, r->address.phase, 1u);
        put_le(w, r->address.has_work_item ? 1u : 0u, 1u);
        if (r->address.has_work_item) {
            put_le(w, (uint64_t)r->address.work_item, 8u);
            put_le(w, (uint64_t)r->address.step, 8u);
        }
    }
    put_le(w, r->stack_count, 4u);
    for (i = 0; i < r->stack_count; i++) {
        if (!put_position(w, r->program, r->stack[i])) {
            return false;
        }
    }
    return true;
}

size_t cint_fault_encode(const cint_fault_record *record, uint8_t *buf, size_t cap)
{
    writer w;
    w.buf = NULL;
    w.len = 0u;
    if (record == NULL || !encode(record, &w)) {
        return 0u;
    }
    if (buf != NULL && cap >= w.len) {
        writer out;
        out.buf = buf;
        out.len = 0u;
        (void)encode(record, &out);
    }
    return w.len;
}

/* ------------------------------------------------------------------------- */
/* Rendering tagged values as decimal text.                                   */

/* Writes the decimal of the n-byte little-endian integer `le` (two's
 * complement when is_signed) into out, which has room for 640 bytes; returns
 * the length. n <= 257. */
static size_t decimal(const uint8_t *le, size_t n, bool is_signed, char *out)
{
    uint8_t mag[CINT_Z_MAX_BYTES];
    char digits[640];
    size_t nd = 0u, len = 0u, i;
    bool negative = is_signed && n > 0u && (le[n - 1u] & 0x80u) != 0u;
    bool nonzero = true;
    memcpy(mag, le, n);
    if (negative) {
        unsigned carry = 1u;
        for (i = 0; i < n; i++) {
            unsigned t = (unsigned)(uint8_t)~mag[i] + carry;
            mag[i] = (uint8_t)t;
            carry = t >> 8;
        }
    }
    while (nonzero) {
        unsigned rem = 0u;
        nonzero = false;
        for (i = n; i > 0u; i--) {
            unsigned cur = rem * 256u + mag[i - 1u];
            mag[i - 1u] = (uint8_t)(cur / 10u);
            rem = cur % 10u;
            nonzero = nonzero || mag[i - 1u] != 0u;
        }
        digits[nd++] = (char)('0' + rem);
    }
    if (negative) {
        out[len++] = '-';
    }
    while (nd > 0u) {
        out[len++] = digits[--nd];
    }
    return len;
}

/* Renders into text (at least 700 bytes): with or without the type name. */
static size_t render(const cint_tvalue *v, bool with_type, char *text)
{
    size_t len = 0u;
    uint32_t tag;
    if (v == NULL || !tvalue_valid(v)) {
        return 0u;
    }
    tag = v->bytes[0];
    if (tag == CINT_TAG_BOOL) {
        const char *s = v->len != 2u ? NULL : v->bytes[1] == 0u ? "false" : v->bytes[1] == 1u ? "true" : NULL;
        if (s == NULL) {
            return 0u;
        }
        if (with_type) {
            memcpy(text, "Bool ", 5u);
            len = 5u;
        }
        memcpy(text + len, s, strlen(s));
        return len + strlen(s);
    }
    if (tag == CINT_TAG_Z) {
        size_t n;
        if (v->len < 5u) {
            return 0u;
        }
        n = (size_t)v->bytes[1] | ((size_t)v->bytes[2] << 8) | ((size_t)v->bytes[3] << 16) |
            ((size_t)v->bytes[4] << 24);
        if (n > CINT_Z_MAX_BYTES || v->len != 5u + n) {
            return 0u;
        }
        if (with_type) {
            memcpy(text, "Z ", 2u);
            len = 2u;
        }
        return len + decimal(v->bytes + 5, n, true, text + len);
    }
    if (!is_scalar_tag(tag) || v->len != 1u + cint_rt_tag_width(tag) / 8u) {
        return 0u;
    }
    if (with_type) {
        const char *name = cint_rt_type_name(tag);
        len = strlen(name);
        memcpy(text, name, len);
        text[len++] = ' ';
    }
    return len + decimal(v->bytes + 1, v->len - 1u, cint_rt_tag_signed(tag), text + len);
}

static size_t render_to(const cint_tvalue *v, bool with_type, char *buf, size_t cap)
{
    char text[704];
    size_t len = render(v, with_type, text);
    if (len == 0u) {
        if (buf != NULL && cap > 0u) {
            buf[0] = '\0';
        }
        return 0u;
    }
    if (buf != NULL && cap > 0u) {
        size_t n = len < cap - 1u ? len : cap - 1u;
        memcpy(buf, text, n);
        buf[n] = '\0';
    }
    return len;
}

size_t cint_tvalue_render(const cint_tvalue *value, char *buf, size_t cap)
{
    return render_to(value, true, buf, cap);
}

size_t cint_tvalue_render_decimal(const cint_tvalue *value, char *buf, size_t cap)
{
    return render_to(value, false, buf, cap);
}

/* ------------------------------------------------------------------------- */
/* Views, slices, copies, reductions, kernels and scratch storage (cint_rt.h  */
/* section 14; roadmap box 09).                                               */

/* A record whose operands are I64 values: `operation` (refused when NULL,
 * empty or longer than CINT_FAULT_MAX_OPERATION), n operands, no exact, and
 * the limit I64 `limit` when has_limit. Returns false. */
static bool fault_i64s(cint_ctx *ctx, cint_site site, uint32_t code, const char *operation, const int64_t *ops,
                       unsigned n, bool has_limit, int64_t limit)
{
    cint_fault_record *rec;
    unsigned i;
    if (operation == NULL || operation[0] == '\0' || strlen(operation) > CINT_FAULT_MAX_OPERATION) {
        return fault_refused(ctx, site);
    }
    rec = cint_rt_record_begin(ctx, site, (uint16_t)code);
    if (rec != NULL) {
        cint_rt_record_op(rec, operation);
        for (i = 0; i < n; i++) {
            cint_rt_record_operand(rec, CINT_TAG_I64, (uint64_t)ops[i]);
        }
        if (has_limit) {
            record_limit_i64(rec, limit);
        }
    }
    return false;
}

bool cint_rt_fault_index_dim(cint_ctx *ctx, cint_site site, const char *operation, int64_t dim, int64_t index,
                             int64_t extent)
{
    int64_t ops[2] = {dim, index};
    return fault_i64s(ctx, site, CINT_E_BOUNDS, operation, ops, 2u, true, extent);
}

bool cint_rt_fault_decl_shape(cint_ctx *ctx, cint_site site, int64_t dim, int64_t extent)
{
    int64_t ops[2] = {dim, extent};
    return fault_i64s(ctx, site, CINT_E_SHAPE, "decl.shape", ops, 2u, true, 0);
}

bool cint_shape_check_dim(cint_ctx *ctx, cint_site site, uint32_t param, uint32_t dim, int64_t expected,
                          int64_t actual)
{
    int64_t used = ctx->head.fuel_used;
    if (cint_rt_faulted(ctx)) {
        return false;
    }
    if (actual == expected) {
        return true;
    }
    (void)cint_fault_shape(ctx, site, param, dim, expected, actual);
    ctx->head.fuel_used = used;
    return false;
}

bool cint_rt_slice(cint_ctx *ctx, cint_site site, const char *operation, int64_t dim, int64_t extent,
                   int64_t stride, int64_t lo, int64_t hi, int64_t step, uint32_t flags, int64_t *origin,
                   int64_t *length, int64_t *stride_out)
{
    bool inclusive = (flags & CINT_SLICE_INCLUSIVE) != 0u, valid;
    int64_t first, last, at, s;
    uint64_t k, d;
    if (cint_rt_faulted(ctx)) {
        return false;
    }
    if (extent < 0 || step == 0 || flags > 7u || origin == NULL || length == NULL || stride_out == NULL) {
        return fault_refused(ctx, site);
    }
    if (step > 0) {
        /* 0 <= lo <= end <= extent, with end = hi + 1 for `..=` never formed. */
        k = (uint64_t)step;
        first = (flags & CINT_SLICE_LO) != 0u ? lo : 0;
        last = (flags & CINT_SLICE_HI) != 0u ? hi : extent;
        valid = first >= 0 && (inclusive ? last >= first - 1 && last < extent : last >= first && last <= extent);
        d = valid ? (uint64_t)(last - first) + (inclusive ? 1u : 0u) : 0u;
    } else {
        k = (uint64_t)0 - (uint64_t)step;
        first = (flags & CINT_SLICE_LO) != 0u ? lo : extent - 1;
        if (inclusive) {  /* 0 <= last <= first + 1 <= extent */
            last = (flags & CINT_SLICE_HI) != 0u ? hi : 0;
            valid = last >= 0 && first >= last - 1 && first < extent;
            d = valid && first >= last ? (uint64_t)(first - last) + 1u : 0u;
        } else {  /* -1 <= last <= first <= extent - 1 */
            last = (flags & CINT_SLICE_HI) != 0u ? hi : -1;
            valid = last >= -1 && last <= first && first < extent;
            d = valid ? (uint64_t)(first - last) : 0u;
        }
    }
    if (!valid) {
        int64_t ops[3] = {dim, first, last};
        return fault_i64s(ctx, site, CINT_E_BOUNDS, operation, dim >= 0 ? ops : ops + 1, dim >= 0 ? 3u : 2u, true,
                          extent);
    }
    if (!cint_rt_mul_ovf_i64_p(first, stride, &at) && !cint_rt_add_ovf_i64_p(*origin, at, &at)) {
        *origin = at;
    }
    if (cint_rt_mul_ovf_i64_p(stride, step, &s)) {
        s = (stride < 0) != (step < 0) ? INT64_MIN : INT64_MAX;
    }
    *length = (int64_t)(d / k + (d % k != 0u ? 1u : 0u));  /* d <= INT64_MAX */
    *stride_out = s;
    return true;
}

/* The rank of a view clamped to 0 .. 4, so that no input reads outside its arrays. */
static unsigned view_rank(const cint_vdesc *v)
{
    return v->rank <= 0 ? 0u : v->rank >= CINT_MAX_RANK ? (unsigned)CINT_MAX_RANK : (unsigned)v->rank;
}

/* The element count of a view: 0 when an extent is 0 or below, else the
 * product of the extents, saturated at UINT64_MAX. */
static uint64_t view_count(const cint_vdesc *v)
{
    uint64_t n = 1u;
    unsigned k;
    for (k = 0; k < view_rank(v); k++) {
        uint64_t e = v->shape[k] > 0 ? (uint64_t)v->shape[k] : 0u;
        if (e == 0u) {
            return 0u;
        }
        n = n > UINT64_MAX / e ? UINT64_MAX : n * e;
    }
    return n;
}

/* Row-major iteration over a nonempty view, the last index fastest. The
 * element offset is kept modulo 2^64, so no step overflows; for a valid view
 * every offset it reaches lies in the buffer. */
typedef struct view_walk {
    const cint_vdesc *v;
    int64_t idx[CINT_MAX_RANK];
    uint64_t off;
} view_walk;

static void walk_start(view_walk *w, const cint_vdesc *v)
{
    memset(w, 0, sizeof *w);
    w->v = v;
    w->off = (uint64_t)v->origin;
}

static unsigned char *walk_at(const view_walk *w, size_t size)
{
    return (unsigned char *)w->v->base + cint_rt_sext(w->off * (uint64_t)size, 64u);
}

/* Advances to the next element; false after the last. */
static bool walk_next(view_walk *w)
{
    unsigned k = view_rank(w->v);
    while (k > 0u) {
        k--;
        w->off += (uint64_t)w->v->stride[k];
        if (++w->idx[k] < w->v->shape[k]) {
            return true;
        }
        w->off -= (uint64_t)w->v->stride[k] * (uint64_t)w->v->shape[k];
        w->idx[k] = 0;
    }
    return false;
}

/* The bounding interval of a nonempty view (SPEC-02 V-5); false when it does
 * not fit an I64. */
static bool view_bounds(const cint_vdesc *v, int64_t *lo, int64_t *hi)
{
    unsigned k;
    *lo = v->origin;
    *hi = v->origin;
    for (k = 0; k < view_rank(v); k++) {
        int64_t span;
        if (v->shape[k] > 1 && (cint_rt_mul_ovf_i64_p(v->stride[k], v->shape[k] - 1, &span) ||
                                (span < 0 ? cint_rt_add_ovf_i64_p(*lo, span, lo) : cint_rt_add_ovf_i64_p(*hi, span, hi)))) {
            return false;
        }
    }
    return true;
}

/* x modulo g in 0 .. g - 1, for g >= 1. */
static uint64_t residue(int64_t x, uint64_t g)
{
    uint64_t r = x >= 0 ? (uint64_t)x % g : ((uint64_t)0 - (uint64_t)x) % g;
    return x >= 0 || r == 0u ? r : g - r;
}

int cint_rt_view_relation(const cint_vdesc *p, const cint_vdesc *q)
{
    const cint_vdesc *both[2];
    int64_t plo, phi, qlo, qhi;
    uint64_t g = 0u;
    unsigned i, k;
    if (p == NULL || q == NULL) {
        return 2;
    }
    if (view_count(p) == 0u || view_count(q) == 0u || p->base != q->base) {
        return 0;  /* T0, and different buffers */
    }
    if (!view_bounds(p, &plo, &phi) || !view_bounds(q, &qlo, &qhi)) {
        return 2;
    }
    if (phi < qlo || qhi < plo) {
        return 0;  /* T1 */
    }
    both[0] = p;
    both[1] = q;
    for (i = 0; i < 2u; i++) {
        for (k = 0; k < view_rank(both[i]); k++) {
            int64_t s = both[i]->stride[k];
            uint64_t a = s < 0 ? (uint64_t)0 - (uint64_t)s : (uint64_t)s;
            while (both[i]->shape[k] > 1 && a != 0u) {  /* g := gcd(g, |s|) */
                uint64_t t = g % a;
                g = a;
                a = t;
            }
        }
    }
    if (g >= 2u && residue(p->origin, g) != residue(q->origin, g)) {
        return 0;  /* T2 */
    }
    if (p->origin != q->origin || p->rank != q->rank) {
        return 2;
    }
    for (k = 0; k < view_rank(p); k++) {
        if (p->shape[k] != q->shape[k] || p->stride[k] != q->stride[k]) {
            return 2;  /* T4 and the uncertain rest */
        }
    }
    return 1;  /* T3 */
}

bool cint_rt_copy_view(cint_ctx *ctx, cint_site site, const cint_vdesc *dst, const cint_vdesc *src, size_t size)
{
    view_walk to, from;
    unsigned k;
    int relation;
    if (cint_rt_faulted(ctx)) {
        return false;
    }
    if (dst == NULL || src == NULL || dst->rank < 1 || dst->rank > CINT_MAX_RANK || src->rank != dst->rank) {
        return fault_refused(ctx, site);
    }
    for (k = 0; k < view_rank(dst); k++) {
        if (dst->shape[k] != src->shape[k]) {
            return cint_copy_shape_check(ctx, site, (int64_t)k, dst->shape[k], src->shape[k]);
        }
    }
    relation = cint_rt_view_relation(dst, src);
    if (relation == 2) {
        return fault_i64s(ctx, site, CINT_E_ALIAS, "copy.alias", NULL, 0u, false, 0);
    }
    if (relation == 1 || size == 0u || view_count(src) == 0u) {
        return true;
    }
    walk_start(&to, dst);
    walk_start(&from, src);
    do {
        memcpy(walk_at(&to, size), walk_at(&from, size), size);
    } while (walk_next(&to) && walk_next(&from));
    return true;
}

/* The alias check of a call or a dispatch: 0 when the views are disjoint, 1
 * when it wrote the bind.alias record, 2 when the context was faulted or the
 * arguments are refused. */
static int alias_check(cint_ctx *ctx, cint_site site, uint32_t a, uint32_t b, const cint_vdesc *p,
                       const cint_vdesc *q)
{
    int64_t ops[2] = {(int64_t)a, (int64_t)b};
    if (cint_rt_faulted(ctx)) {
        return 2;
    }
    if (p == NULL || q == NULL) {
        (void)fault_refused(ctx, site);
        return 2;
    }
    if (cint_rt_view_relation(p, q) == 0) {
        return 0;
    }
    (void)fault_i64s(ctx, site, CINT_E_ALIAS, "bind.alias", ops, 2u, false, 0);
    return 1;
}

bool cint_rt_call_alias(cint_ctx *ctx, cint_site site, uint32_t param, uint32_t other, const cint_vdesc *p,
                        const cint_vdesc *q)
{
    return alias_check(ctx, site, param, other, p, q) == 0;
}

bool cint_rt_dispatch_alias(cint_ctx *ctx, cint_site site, uint32_t i, uint32_t x, const cint_vdesc *p,
                            const cint_vdesc *q, uint32_t elem, uint32_t writable)
{
    if (ctx->host_binds != NULL) {  /* host-issued: A-5 over registrations, as the binder (H-13) */
        return cint_rt_dispatch_bound(ctx, site);
    }
    int r = alias_check(ctx, site, i, x, p, q);
    unsigned k;
    for (k = 0; r == 1 && k < 2u; k++) {
        const cint_vdesc *v = k == 0u ? p : q;
        describe_view(ctx, k, elem, (writable >> k) & 1u, v->rank, v->origin, v->shape, v->stride);
    }
    return r == 0;
}

bool cint_rt_fault_where(cint_ctx *ctx, cint_site site, int64_t index, int64_t left, int64_t right)
{
    int64_t ops[3] = {index, left, right};
    return fault_i64s(ctx, site, CINT_E_SHAPE, "bind.where", ops, 3u, false, 0);
}

bool cint_rt_dispatch_stage(cint_ctx *ctx, cint_site site, cint_vdesc *stage, const cint_vdesc *arg, size_t size,
                            bool copy_in)
{
    void *p = NULL;
    int64_t count = 1, step = 1, k;
    if (cint_rt_faulted(ctx)) {
        return false;
    }
    if (stage == NULL || arg == NULL || arg->rank < 1 || arg->rank > 4) {
        return fault_refused(ctx, site);
    }
    for (k = 0; k < arg->rank; k++) {
        /* A valid view's extents are not negative, and their product counts its elements. */
        if (arg->shape[k] < 0 || (arg->shape[k] > 0 && count > INT64_MAX / arg->shape[k])) {
            return fault_refused(ctx, site);
        }
        count *= arg->shape[k];
    }
    if (!cint_rt_scratch(ctx, site, count, size, &p)) {
        return false;
    }
    memset(stage, 0, sizeof *stage);
    stage->base = p;
    stage->rank = arg->rank;
    for (k = arg->rank - 1; k >= 0; k--) {
        stage->shape[k] = arg->shape[k];
        stage->stride[k] = step;
        step = arg->shape[k] > 0 ? step * arg->shape[k] : step;
    }
    return !copy_in || cint_rt_copy_view(ctx, site, stage, arg, size);
}

cint_status cint_ctx_fault_descriptors(const cint_ctx *cctx, cint_fault_descriptor out[2], uint32_t *count)
{
    cint_ctx *ctx = (cint_ctx *)(uintptr_t)cctx;
    if (ctx == NULL || out == NULL || count == NULL) {
        return CINT_REFUSED;
    }
    if (!cint_rt_busy_take(ctx)) {
        return CINT_BUSY;
    }
    *count = ctx->head.faulted != 0u ? ctx->desc_count : 0u;
    memcpy(out, ctx->desc, (size_t)*count * sizeof out[0]);
    cint_rt_busy_give(ctx);
    return CINT_OK;
}

/* Adds z (|z| < 2^128) to a 256-bit two's complement accumulator. */
static void zacc_add_z(cint_zacc *a, cint_rt_z128 z)
{
    uint64_t v[4], carry = 1u;
    unsigned k;
    v[0] = z.lo;
    v[1] = z.hi;
    v[2] = 0u;
    v[3] = 0u;
    for (k = 0; z.negative && k < 4u; k++) {  /* -|z|: invert, then add 1 */
        v[k] = ~v[k] + carry;
        carry = carry != 0u && v[k] == 0u ? 1u : 0u;
    }
    carry = 0u;
    for (k = 0; k < 4u; k++) {
        uint64_t t = a->limb[k] + v[k];
        uint64_t over = t < v[k] ? 1u : 0u;
        a->limb[k] = t + carry;
        carry = over + (a->limb[k] < carry ? 1u : 0u);
    }
}

void cint_zacc_add_product(cint_zacc *a, uint32_t tag, uint64_t x, uint64_t y)
{
    zacc_add_z(a, cint_rt_z_mul(cint_rt_z_from_bits(tag, cint_rt_zcanon(tag, x)),
                                cint_rt_z_from_bits(tag, cint_rt_zcanon(tag, y))));
}

bool cint_rt_zacc_fit(cint_ctx *ctx, cint_site site, const char *name, uint32_t elem, uint32_t result, int64_t n,
                      const cint_zacc *a, uint64_t *out)
{
    cint_fault_record *rec;
    bool negative, fits;
    uint64_t ext;
    if (cint_rt_faulted(ctx)) {
        return false;
    }
    if (name == NULL || (strcmp(name, "sum") != 0 && strcmp(name, "dot") != 0) || !is_scalar_tag(elem) ||
        !is_scalar_tag(result) || a == NULL || out == NULL) {
        return fault_refused(ctx, site);
    }
    negative = (a->limb[3] >> 63) != 0u;
    ext = negative ? UINT64_MAX : 0u;
    fits = a->limb[1] == ext && a->limb[2] == ext && a->limb[3] == ext &&
           (negative ? (a->limb[0] >> 63) != 0u && cint_rt_fits(a->limb[0], true, result)
                     : cint_rt_fits(a->limb[0], false, result));
    if (fits) {
        *out = a->limb[0];
        return true;
    }
    rec = cint_rt_record_begin(ctx, site, (uint16_t)CINT_E_OVERFLOW);
    if (rec != NULL) {
        cint_rt_record_op(rec, name);
        cint_rt_record_op(rec, "checked");
        cint_rt_record_op(rec, cint_rt_type_ident(elem));
        cint_rt_record_op(rec, cint_rt_type_ident(result));
        cint_rt_record_operand(rec, CINT_TAG_I64, (uint64_t)n);
        rec->has_exact = 1u;
        tvalue_z256(&rec->exact, a);
        rec->has_limit = 1u;
        cint_rt_tvalue_scalar(&rec->limit, result, z_bits(negative ? cint_rt_z_min(result) : cint_rt_z_max(result)));
    }
    return false;
}

bool cint_rt_fold_step(cint_ctx *ctx, cint_site site, uint32_t op, uint32_t tag, int64_t j, uint64_t *acc,
                       uint64_t x)
{
    cint_fault_record *rec;
    cint_rt_z128 exact;
    uint64_t a;
    if (cint_rt_faulted(ctx)) {
        return false;
    }
    if ((op != CINT_RT_OP_ADD && op != CINT_RT_OP_MUL) || !is_scalar_tag(tag) || acc == NULL) {
        return fault_refused(ctx, site);
    }
    a = canon(tag, *acc);
    x = canon(tag, x);
    exact = op == CINT_RT_OP_ADD ? cint_rt_z_add(cint_rt_z_from_bits(tag, a), cint_rt_z_from_bits(tag, x))
                                 : cint_rt_z_mul(cint_rt_z_from_bits(tag, a), cint_rt_z_from_bits(tag, x));
    if (cint_rt_z_cmp(exact, cint_rt_z_min(tag)) >= 0 && cint_rt_z_cmp(exact, cint_rt_z_max(tag)) <= 0) {
        *acc = z_bits(exact);
        return true;
    }
    rec = cint_rt_record_begin(ctx, site, (uint16_t)CINT_E_OVERFLOW);
    if (rec != NULL) {
        cint_rt_record_op(rec, "fold_checked");
        cint_rt_record_op(rec, op == CINT_RT_OP_ADD ? "add" : "mul");
        cint_rt_record_op(rec, cint_rt_type_ident(tag));
        cint_rt_record_operand(rec, CINT_TAG_I64, (uint64_t)j);
        cint_rt_record_operand(rec, tag, a);
        cint_rt_record_operand(rec, tag, x);
        cint_rt_record_range(rec, exact, tag);
    }
    return false;
}

bool cint_rt_fault_reduce_empty(cint_ctx *ctx, cint_site site, bool max, uint32_t tag)
{
    cint_fault_record *rec;
    if (!is_scalar_tag(tag)) {
        return fault_refused(ctx, site);
    }
    rec = cint_rt_record_begin(ctx, site, (uint16_t)CINT_E_SHAPE);
    if (rec != NULL) {
        cint_rt_record_op(rec, max ? "reduce_max" : "reduce_min");
        cint_rt_record_op(rec, "checked");
        cint_rt_record_op(rec, cint_rt_type_ident(tag));
        cint_rt_record_operand(rec, CINT_TAG_I64, 0u);
    }
    return false;
}

/* The canonical pattern of the element of scalar type tag at p. */
static uint64_t load_elem(const unsigned char *p, uint32_t tag)
{
    uint8_t b8;
    uint16_t b16;
    uint32_t b32;
    uint64_t b64;
    switch (cint_rt_tag_width(tag)) {
    case 8u:
        memcpy(&b8, p, 1u);
        b64 = b8;
        break;
    case 16u:
        memcpy(&b16, p, 2u);
        b64 = b16;
        break;
    case 32u:
        memcpy(&b32, p, 4u);
        b64 = b32;
        break;
    default:
        memcpy(&b64, p, 8u);
        break;
    }
    return canon(tag, b64);
}

/* SPEC-01 IM-139: a reduction over n elements charges ceil(n / 64) units. */
#define REDUCE_FUEL_BLOCK 64u

bool cint_rt_reduce(cint_ctx *ctx, cint_site site, uint32_t op, uint32_t elem, uint32_t result,
                    const cint_vdesc *xs, const cint_vdesc *ys, bool has_init, uint64_t init, uint64_t *out)
{
    bool fold = op == CINT_REDUCE_FOLD_ADD || op == CINT_REDUCE_FOLD_MUL;
    bool minmax = op == CINT_REDUCE_MIN || op == CINT_REDUCE_MAX, any = minmax && has_init;
    cint_rt_z128 sat = z_make(false, 0u, 0u);
    cint_fault_record *rec;
    cint_zacc acc;
    view_walk wx, wy;
    uint64_t n, x, j = 0u, bits = 0u, count = 0u;
    size_t size;
    if (cint_rt_faulted(ctx)) {
        return false;
    }
    if (op < CINT_REDUCE_SUM || op > CINT_REDUCE_DOT || xs == NULL || out == NULL || xs->rank < 1 ||
        xs->rank > CINT_MAX_RANK || (fold && !has_init) ||
        (op == CINT_REDUCE_COUNT ? elem != CINT_TAG_BOOL || result != CINT_TAG_I64
                                 : !is_scalar_tag(elem) || !is_scalar_tag(result)) ||
        (op != CINT_REDUCE_SUM && op != CINT_REDUCE_DOT && op != CINT_REDUCE_COUNT && result != elem) ||
        (op == CINT_REDUCE_DOT && (ys == NULL || xs->rank != 1 || ys->rank != 1))) {
        return fault_refused(ctx, site);
    }
    if (op == CINT_REDUCE_DOT && xs->shape[0] != ys->shape[0]) {
        /* dot's shape check, before the fuel charge (ruling R6) */
        rec = cint_rt_record_begin(ctx, site, (uint16_t)CINT_E_SHAPE);
        if (rec != NULL) {
            cint_rt_record_op(rec, "dot");
            cint_rt_record_op(rec, "checked");
            cint_rt_record_op(rec, cint_rt_type_ident(elem));
            cint_rt_record_op(rec, cint_rt_type_ident(result));
            cint_rt_record_operand(rec, CINT_TAG_I64, 0u);
            cint_rt_record_operand(rec, CINT_TAG_I64, (uint64_t)ys->shape[0]);
            record_limit_i64(rec, xs->shape[0]);
        }
        return false;
    }
    n = view_count(xs);
    if (n > 0u && !cint_fuel_charge(ctx, site, n / REDUCE_FUEL_BLOCK + (n % REDUCE_FUEL_BLOCK != 0u ? 1u : 0u))) {
        return false;
    }
    memset(&acc, 0, sizeof acc);
    size = op == CINT_REDUCE_COUNT ? sizeof(bool) : cint_rt_tag_width(elem) / 8u;
    bits = fold || any ? canon(elem, init) : 0u;
    if (n > 0u) {
        walk_start(&wx, xs);
        walk_start(&wy, op == CINT_REDUCE_DOT ? ys : xs);
        do {
            const unsigned char *p = walk_at(&wx, size);
            x = op == CINT_REDUCE_COUNT ? (*p != 0u ? 1u : 0u) : load_elem(p, elem);
            switch (op) {
            case CINT_REDUCE_SUM:
            case CINT_REDUCE_SUM_WRAP:
                cint_zacc_add(&acc, elem, x);
                break;
            case CINT_REDUCE_SUM_SAT: {
                cint_rt_z128 t = cint_rt_z_add(sat, cint_rt_z_from_bits(elem, x));
                cint_rt_z128 lo = cint_rt_z_min(elem), hi = cint_rt_z_max(elem);
                sat = cint_rt_z_cmp(t, hi) > 0 ? hi : cint_rt_z_cmp(t, lo) < 0 ? lo : t;
                break;
            }
            case CINT_REDUCE_FOLD_ADD:
            case CINT_REDUCE_FOLD_MUL:
                if (!cint_rt_fold_step(ctx, site, op == CINT_REDUCE_FOLD_ADD ? CINT_RT_OP_ADD : CINT_RT_OP_MUL, elem,
                                       cint_rt_sext(j, 64u), &bits, x)) {
                    return false;
                }
                break;
            case CINT_REDUCE_MIN:
            case CINT_REDUCE_MAX: {
                bool below = cint_rt_tag_signed(elem) ? cint_rt_sext(x, 64u) < cint_rt_sext(bits, 64u) : x < bits;
                if (!any || (op == CINT_REDUCE_MIN ? below : !below && x != bits)) {
                    bits = x;
                }
                any = true;
                break;
            }
            case CINT_REDUCE_COUNT:
                count += x;
                break;
            default:  /* dot */
                cint_zacc_add_product(&acc, elem, x, load_elem(walk_at(&wy, size), elem));
                (void)walk_next(&wy);
                break;
            }
            j++;
        } while (walk_next(&wx));
    }
    switch (op) {
    case CINT_REDUCE_SUM:
    case CINT_REDUCE_DOT:
        return cint_rt_zacc_fit(ctx, site, op == CINT_REDUCE_SUM ? "sum" : "dot", elem, result, cint_rt_sext(n, 64u),
                                &acc, out);
    case CINT_REDUCE_SUM_WRAP:
        *out = canon(elem, acc.limb[0]);
        return true;
    case CINT_REDUCE_SUM_SAT:
        *out = z_bits(sat);
        return true;
    case CINT_REDUCE_COUNT:
        *out = count;
        return true;
    default:
        if (minmax && !any) {
            return cint_rt_fault_reduce_empty(ctx, site, op == CINT_REDUCE_MAX, elem);
        }
        *out = bits;
        return true;
    }
}

int64_t cint_rt_dispatch_number(cint_ctx *ctx)
{
    int64_t n = ctx->dispatches;
    ctx->dispatches = n < INT64_MAX ? n + 1 : n;
    return n;
}

void cint_rt_dispatch_push(cint_ctx *ctx, cint_site site)
{
    /* After the push, calls may still fill the stack up to index stack_count +
     * depth_limit - depth, which must stay below CINT_RT_MAX_DEPTH; a first
     * push (stack_count = depth - 1) always passes. */
    if (ctx->depth_limit > (int64_t)CINT_RT_MAX_DEPTH + ctx->depth - 1 - (int64_t)ctx->stack_count) {
        (void)fault_refused(ctx, site);
        return;
    }
    ctx->stack[ctx->stack_count] = site;
    ctx->stack_count++;
}

void cint_rt_dispatch_pop(cint_ctx *ctx)
{
    if (ctx->stack_count > 0u && (int64_t)ctx->stack_count >= ctx->depth) {  /* a pushed position */
        ctx->stack_count--;
    }
}

void cint_rt_dispatch_fault(cint_ctx *ctx, const char *kernel, int64_t number, uint32_t phase, int64_t work_item,
                            int64_t step)
{
    cint_fault_address *a = &ctx->fault.address;
    size_t n = kernel != NULL ? strlen(kernel) : 0u;
    if (ctx->head.faulted == 0u || ctx->fault.has_address != 0u || n == 0u || n > sizeof a->name ||
        phase > CINT_PHASE_EPILOGUE) {
        return;
    }
    memset(a, 0, sizeof *a);
    ctx->fault.has_address = 1u;
    a->dispatch = number;
    a->phase = (uint8_t)phase;
    if (phase == CINT_PHASE_WORK_ITEM) {
        a->has_work_item = 1u;
        a->work_item = work_item;
        a->step = step;
    }
    a->name_len = (uint16_t)n;
    memcpy(a->name, kernel, n);
}

bool cint_rt_scratch(cint_ctx *ctx, cint_site site, int64_t count, size_t size, void **out)
{
    static uint64_t none;  /* the storage of every request for 0 elements: never read */
    int64_t ops[3];
    scratch_block *b;
    size_t bytes;
    if (cint_rt_faulted(ctx)) {
        return false;
    }
    if (count < 0 || out == NULL) {
        return fault_refused(ctx, site);
    }
    ops[0] = count;
    ops[1] = ctx->scratch_used + ctx->frame_charged;
    ops[2] = ctx->frame_arena_bytes / 8;  /* the capacity in elements (rt/OPEN.md RT-OQ-34) */
    if (count > ops[2] - ops[1]) {
        return fault_i64s(ctx, site, CINT_E_BOUNDS, "arena.alloc", ops, 3u, false, 0);
    }
    if (count == 0) {
        *out = &none;
        return true;
    }
    if (size != 0u && (uint64_t)count > (SIZE_MAX - sizeof *b) / size) {
        return fault_i64s(ctx, site, CINT_E_UNSUPPORTED, "arena.alloc", ops, 3u, false, 0);
    }
    bytes = sizeof *b + (size_t)count * size;
    b = (scratch_block *)ctx->allocator.alloc(ctx->allocator.user, bytes, 16u);
    if (b == NULL) {
        return fault_i64s(ctx, site, CINT_E_UNSUPPORTED, "arena.alloc", ops, 3u, false, 0);
    }
    memset(b, 0, bytes);
    b->prev = ctx->scratch;
    b->bytes = bytes;
    b->elements = count;
    ctx->scratch = b;
    ctx->scratch_count++;
    ctx->scratch_used += count;
    *out = (unsigned char *)b + sizeof *b;
    return true;
}

int64_t cint_rt_scratch_mark(const cint_ctx *ctx)
{
    return ctx->scratch_count;
}

void cint_rt_scratch_release(cint_ctx *ctx, int64_t mark)
{
    scratch_free(ctx, mark);
}
