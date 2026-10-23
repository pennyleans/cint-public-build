/* test_wide.c: the wide integer helpers of rt/cint_wide.h, I128 to I1024 and
 * U128 to U512, against cint_ref (roadmap box 15, unit 3).
 *
 * tests/test_cint_rt_wide_types.py writes the cases from cint_ref, one per
 * line, `<id> <kind> <tag> <mode> <aux> <arg>... = <result>`, and runs this
 * driver on every leg. Each case calls one helper at site 1 of module `wide.ci`
 * (line 1, column 1) and renders its outcome as the generator does: the value,
 * the result form's ArithError tag and value, the comparison, or the canonical
 * bytes of the fault record (cint_fault_encode). A case whose rendering differs
 * from the expected text fails. Every helper with a result slot also runs with
 * the slot aliasing each wide input (SPEC-09 EMIT-29) and must give the same
 * value, and a helper that faults must leave its slot unwritten. Then the limb
 * steps (the 64-bit product and the normalized 2-by-1 quotient) are checked
 * against cint_rt.h's portable ones on boundary values and on `pairs` seeded
 * pseudo-random pairs (default 1,000,000), which is SPEC-09 EMIT-05's agreement
 * test of the builtin bodies (EMIT-12). First, test_wide_calls.c calls the
 * built-ins as emitted code does (RT-OQ-60). Prints the helper mode and the
 * counts, and exits 1 on any failure.
 *
 * Usage: test_wide <cases> [pairs].
 */
/* fopen, sprintf and strcpy over bounded buffers, which MSVC deprecates. */
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS 1
#endif
#include "cint_rt.h"
#include "cint_rt_host.h"
#include "cint_rt_internal.h"
#include "cint_state.h"
#include "cint_wide.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static long g_pass;
static long g_fail;

static const cint_site_info w_sites[] = {{0u, 0u, NULL}, {1u, 1u, "wide"}};
static const cint_module w_module = {"wide.ci", 7u, 2u, w_sites};
static const cint_module *const w_modules[] = {&w_module};
static const cint_program w_program = {1u, 0u, w_modules, NULL};
static const cint_site W_SITE = {0u, 1u};

#define SENTINEL 0xA5A5A5A5A5A5A5A5u

/* test_wide_calls.c: the built-ins called as emitted code calls them; returns its failures. */
int test_wide_calls(cint_ctx *ctx, cint_site site);

typedef struct warg {
    uint32_t tag;
    uint64_t limb[16];  /* sign-extended to 16 limbs */
} warg;

typedef union narrow_out {
    int8_t i8;
    int16_t i16;
    int32_t i32;
    int64_t i64;
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
    uint64_t u64;
} narrow_out;

static unsigned tag_bytes(uint32_t tag)
{
    return cint_rt_tag_width(tag) / 8u;
}

static int hexval(int ch)
{
    return ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1;
}

/* `<tag>:<hex>`, the little-endian bytes of a value of the tag. */
static int parse_arg(const char *s, warg *out)
{
    char *end;
    unsigned long tag = strtoul(s, &end, 10);
    uint8_t bytes[128];
    unsigned n, i;
    if (*end != ':' || (tag & 0xF0u) == 0u || (tag & 0xF0u) > 0x20u) return 0;
    out->tag = (uint32_t)tag;
    n = tag_bytes(out->tag);
    if (n == 0u || n > sizeof bytes || strlen(end + 1) != 2u * n) return 0;
    for (i = 0u; i < n; i++) {
        int hi = hexval(end[1 + 2 * i]), lo = hexval(end[2 + 2 * i]);
        if (hi < 0 || lo < 0) return 0;
        bytes[i] = (uint8_t)(hi * 16 + lo);
    }
    memset(out->limb, 0, sizeof out->limb);
    if (n >= 16u) {
        cint_wide_load(out->limb, bytes, n / 8u);
    } else {
        for (i = 0u; i < n; i++) out->limb[0] |= (uint64_t)bytes[i] << (8u * i);
    }
    {
        bool negative = (out->tag & 0xF0u) == 0x10u && ((bytes[n - 1u] >> 7) & 1u) != 0u;
        if (negative) {
            if (n < 8u) out->limb[0] |= ~(uint64_t)0 << (8u * n);
            for (i = (n + 7u) / 8u; i < 16u; i++) out->limb[i] = ~(uint64_t)0;
        }
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

/* "<tag> <hex>" for the value of a tag whose low bits are in limb. */
static void put_value(char *dst, uint32_t tag, const uint64_t *limb)
{
    uint8_t bytes[128];
    unsigned n = tag_bytes(tag), i;
    int len = sprintf(dst, "%u ", (unsigned)tag);
    if (n >= 16u) {
        cint_wide_store(bytes, limb, n / 8u);
    } else {
        for (i = 0u; i < n; i++) bytes[i] = (uint8_t)(limb[0] >> (8u * i));
    }
    put_hex(dst + len, bytes, n);
}

static uint64_t narrow_bits(uint32_t tag, const narrow_out *o)
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

static void fill(uint64_t *limb, unsigned n)
{
    unsigned i;
    for (i = 0u; i < n; i++) limb[i] = SENTINEL;
}

static int unwritten(const uint64_t *limb, unsigned n)
{
    unsigned i;
    for (i = 0u; i < n; i++) {
        if (limb[i] != SENTINEL) return 0;
    }
    return 1;
}

/* The form of a conversion kind: 0 `as`, 1 `as%`, 2 `as|`, -1 for another kind. */
static int conv_form(const char *kind)
{
    if (strcmp(kind, "as") == 0) return 0;
    if (strcmp(kind, "as_wrap") == 0) return 1;
    return strcmp(kind, "as_sat") == 0 ? 2 : -1;
}

/* A checked helper's outcome: the value, or the record with the slot unwritten. */
#define OUTCOME(ok, r, N, TAG)                                                         \
    do {                                                                               \
        if (ok) {                                                                      \
            res[0] = 'V';                                                              \
            res[1] = ' ';                                                              \
            put_value(res + 2, (TAG), (r).limb);                                       \
        } else {                                                                       \
            if (!unwritten((r).limb, (N))) strcpy(*alias, "a faulting helper wrote its slot"); \
            put_fault(ctx, res);                                                       \
        }                                                                              \
    } while (0)

/* A helper's value, which never faults. */
#define VALUE(r, TAG)                          \
    do {                                       \
        res[0] = 'V';                          \
        res[1] = ' ';                          \
        put_value(res + 2, (TAG), (r).limb);   \
    } while (0)

/* The call `expr` with its slot aliasing operand `a` (then `b`) gives r too. */
#define ALIAS2(T, N, CALL)                                                            \
    do {                                                                              \
        cint_##T s_ = a;                                                              \
        CALL(&s_, &b, &s_);                                                           \
        if (memcmp(&s_, &r, sizeof r) != 0) strcpy(*alias, "slot aliasing a");        \
        s_ = b;                                                                       \
        CALL(&a, &s_, &s_);                                                           \
        if (memcmp(&s_, &r, sizeof r) != 0) strcpy(*alias, "slot aliasing b");        \
    } while (0)

/* Room for a wide count, which a cint_count points at while it is in use. */
typedef union wcount {
    cint_i1024 i1024;
    cint_i512 i512;
    cint_i256 i256;
    cint_i128 i128;
    cint_u512 u512;
    cint_u256 u256;
    cint_u128 u128;
} wcount;

/* The shift count of x: its pattern, or a wide count kept at *at (RT-OQ-52). */
static cint_count make_count(const warg *x, wcount *at)
{
    cint_count k = {x->limb[0], x->tag, 0u, NULL};
    memcpy(at->i1024.limb, x->limb, sizeof at->i1024.limb);
    switch (x->tag) {
    case CINT_TAG_I128: return cint_count_i128(&at->i128);
    case CINT_TAG_I256: return cint_count_i256(&at->i256);
    case CINT_TAG_I512: return cint_count_i512(&at->i512);
    case CINT_TAG_I1024: return cint_count_i1024(&at->i1024);
    case CINT_TAG_U128: return cint_count_u128(&at->u128);
    case CINT_TAG_U256: return cint_count_u256(&at->u256);
    case CINT_TAG_U512: return cint_count_u512(&at->u512);
    default: return k;
    }
}

#define RUN_WIDE(T, N, TAG)                                                                              \
    static int run_##T(cint_ctx *ctx, const char *kind, uint32_t mode, uint32_t aux, const warg *x,      \
                       int nargs, char *res, char (*alias)[64])                                          \
    {                                                                                                    \
        cint_##T a = {{0u}}, b = {{0u}}, c = {{0u}}, r;                                                  \
        cint_count k = {0u, 0u, 0u, NULL};                                                               \
        wcount kw;                                                                                       \
        bool ok = true;                                                                                  \
        fill(r.limb, (N));                                                                               \
        if (nargs > 0) memcpy(a.limb, x[0].limb, sizeof a.limb);                                         \
        if (nargs > 1) {                                                                                 \
            memcpy(b.limb, x[1].limb, sizeof b.limb);                                                    \
            k = make_count(&x[1], &kw);                                                                  \
        }                                                                                                \
        if (nargs > 2) memcpy(c.limb, x[2].limb, sizeof c.limb);                                         \
        if (strcmp(kind, "add") == 0 || strcmp(kind, "sub") == 0 || strcmp(kind, "mul") == 0 ||         \
            strcmp(kind, "div") == 0 || strcmp(kind, "rem") == 0) {                                      \
            bool (*f)(cint_ctx *, cint_site, const cint_##T *, const cint_##T *, cint_##T *) =           \
                kind[0] == 'a' ? cint_add_##T : kind[0] == 's' ? cint_sub_##T : kind[0] == 'm' ? cint_mul_##T \
                : kind[0] == 'd' ? cint_div_##T : cint_rem_##T;                                          \
            ok = f(ctx, W_SITE, &a, &b, &r);                                                                  \
            OUTCOME(ok, r, (N), (TAG));                                                                  \
            if (ok) {                                                                                    \
                cint_##T s_ = a;                                                                         \
                if (!f(ctx, W_SITE, &s_, &b, &s_) || memcmp(&s_, &r, sizeof r) != 0)                         \
                    strcpy(*alias, "slot aliasing a");                                                   \
                s_ = b;                                                                                  \
                if (!f(ctx, W_SITE, &a, &s_, &s_) || memcmp(&s_, &r, sizeof r) != 0)                         \
                    strcpy(*alias, "slot aliasing b");                                                   \
            }                                                                                            \
        } else if (strcmp(kind, "add_wrap") == 0 || strcmp(kind, "sub_wrap") == 0 ||                    \
                   strcmp(kind, "mul_wrap") == 0 || strcmp(kind, "add_sat") == 0 ||                      \
                   strcmp(kind, "sub_sat") == 0 || strcmp(kind, "mul_sat") == 0 ||                       \
                   strcmp(kind, "and") == 0 || strcmp(kind, "or") == 0 || strcmp(kind, "xor") == 0 ||    \
                   strcmp(kind, "min") == 0 || strcmp(kind, "max") == 0) {                               \
            void (*g)(const cint_##T *, const cint_##T *, cint_##T *) =                                  \
                strcmp(kind, "add_wrap") == 0 ? cint_add_wrap_##T : strcmp(kind, "sub_wrap") == 0 ? cint_sub_wrap_##T \
                : strcmp(kind, "mul_wrap") == 0 ? cint_mul_wrap_##T : strcmp(kind, "add_sat") == 0 ? cint_add_sat_##T \
                : strcmp(kind, "sub_sat") == 0 ? cint_sub_sat_##T : strcmp(kind, "mul_sat") == 0 ? cint_mul_sat_##T \
                : strcmp(kind, "and") == 0 ? cint_and_##T : strcmp(kind, "or") == 0 ? cint_or_##T              \
                : strcmp(kind, "xor") == 0 ? cint_xor_##T : strcmp(kind, "min") == 0 ? cint_min_##T : cint_max_##T; \
            g(&a, &b, &r);                                                                               \
            VALUE(r, (TAG));                                                                \
            ALIAS2(T, N, g);                                                                             \
        } else if (strcmp(kind, "add_result") == 0 || strcmp(kind, "sub_result") == 0 ||                \
                   strcmp(kind, "mul_result") == 0 || strcmp(kind, "div_result") == 0 ||                 \
                   strcmp(kind, "rem_result") == 0 || strcmp(kind, "shl_result") == 0) {                 \
            uint16_t e;                                                                                  \
            if (kind[0] == 'a') e = cint_add_result_##T(&a, &b, &r);                                     \
            else if (kind[0] == 'm') e = cint_mul_result_##T(&a, &b, &r);                                \
            else if (kind[0] == 'd') e = cint_div_result_##T(&a, &b, &r);                                \
            else if (kind[0] == 'r') e = cint_rem_result_##T(&a, &b, &r);                                \
            else if (kind[1] == 'u') e = cint_sub_result_##T(&a, &b, &r);                                \
            else e = cint_shl_result_##T(&a, k, &r);                                                     \
            if (e == 0u) {                                                                               \
                strcpy(res, "R 0 ");                                                                     \
                put_value(res + 4, (TAG), r.limb);                                                       \
            } else {                                                                                     \
                sprintf(res, "R %u", (unsigned)e);                                                       \
                if (!unwritten(r.limb, (N))) strcpy(*alias, "a failed result form wrote its slot");      \
            }                                                                                            \
        } else if (strcmp(kind, "neg") == 0) {                                                           \
            ok = cint_neg_##T(ctx, W_SITE, &a, &r);                                                           \
            OUTCOME(ok, r, (N), (TAG));                                                                  \
        } else if (strcmp(kind, "neg_wrap") == 0 || strcmp(kind, "not") == 0) {                         \
            cint_##T s_ = a;                                                                             \
            if (kind[0] == 'n' && kind[1] == 'e') {                                                      \
                cint_neg_wrap_##T(&a, &r);                                                               \
                cint_neg_wrap_##T(&s_, &s_);                                                             \
            } else {                                                                                     \
                cint_not_##T(&a, &r);                                                                    \
                cint_not_##T(&s_, &s_);                                                                  \
            }                                                                                            \
            VALUE(r, (TAG));                                                                \
            if (memcmp(&s_, &r, sizeof r) != 0) strcpy(*alias, "slot aliasing a");                       \
        } else if (strcmp(kind, "shl") == 0 || strcmp(kind, "shl_wrap") == 0 || strcmp(kind, "shr") == 0) { \
            bool (*h)(cint_ctx *, cint_site, const cint_##T *, cint_count, cint_##T *) =                 \
                strcmp(kind, "shl") == 0 ? cint_shl_##T : strcmp(kind, "shr") == 0 ? cint_shr_##T        \
                                                                                 : cint_shl_wrap_##T;     \
            ok = h(ctx, W_SITE, &a, k, &r);                                                                   \
            OUTCOME(ok, r, (N), (TAG));                                                                  \
            if (ok) {                                                                                    \
                cint_##T s_ = a;                                                                         \
                if (!h(ctx, W_SITE, &s_, k, &s_) || memcmp(&s_, &r, sizeof r) != 0)                           \
                    strcpy(*alias, "slot aliasing a");                                                   \
            }                                                                                            \
        } else if (kind[0] == 'e' || (kind[0] == 'n' && kind[1] == 'e') || kind[0] == 'l' ||             \
                   kind[0] == 'g') {                                                                     \
            bool v = strcmp(kind, "eq") == 0   ? cint_eq_##T(&a, &b)                                    \
                     : strcmp(kind, "ne") == 0 ? cint_ne_##T(&a, &b)                                    \
                     : strcmp(kind, "lt") == 0 ? cint_lt_##T(&a, &b)                                    \
                     : strcmp(kind, "le") == 0 ? cint_le_##T(&a, &b)                                    \
                     : strcmp(kind, "gt") == 0 ? cint_gt_##T(&a, &b)                                    \
                                               : cint_ge_##T(&a, &b);                                   \
            sprintf(res, "B %d", v ? 1 : 0);                                                             \
        } else if (strcmp(kind, "muldiv") == 0) {                                                        \
            union {                                                                                      \
                narrow_out n;                                                                            \
                cint_i1024 w;                                                                            \
            } o;                                                                                         \
            fill(o.w.limb, 16u);                                                                         \
            ok = cint_muldiv_##T(ctx, W_SITE, aux, mode, &a, &b, &c, &o);                                     \
            if (!ok) {                                                                                   \
                if (!unwritten(o.w.limb, 16u)) strcpy(*alias, "a faulting muldiv wrote its slot");       \
                put_fault(ctx, res);                                                                     \
            } else if (tag_bytes(aux) >= 16u) {                                                          \
                res[0] = 'V';                                                                            \
                res[1] = ' ';                                                                            \
                put_value(res + 2, aux, o.w.limb);                                                       \
            } else {                                                                                     \
                uint64_t bits = narrow_bits(aux, &o.n);                                                  \
                res[0] = 'V';                                                                            \
                res[1] = ' ';                                                                            \
                put_value(res + 2, aux, &bits);                                                          \
            }                                                                                            \
        } else if (aux != 0u && aux < 16u) { /* a built-in's number; a target's tag is above */          \
            const cint_##T *pb = nargs > 1 ? &b : NULL, *pc = nargs > 2 ? &c : NULL;                      \
            ok = cint_builtin_##T(ctx, W_SITE, aux, mode, &a, pb, pc, &r);                                    \
            OUTCOME(ok, r, (N), (TAG));                                                                  \
            if (ok) {                                                                                    \
                cint_##T s_ = a;                                                                         \
                if (!cint_builtin_##T(ctx, W_SITE, aux, mode, &s_, pb, pc, &s_) || memcmp(&s_, &r, sizeof r) != 0) \
                    strcpy(*alias, "slot aliasing a");                                                   \
            }                                                                                            \
        } else {                                                                                         \
            return run_conv_from_##T(ctx, kind, aux, &a, &b, k, res, alias);                                 \
        }                                                                                                \
        return 1;                                                                                        \
    }

/* A conversion from a wide type to every target: form 0 checked, 1 wrapping, 2 saturating. */
#define AS_WIDE(D, ND, DTAG, SRC)                                                              \
    case DTAG: {                                                                               \
        cint_##D r;                                                                            \
        fill(r.limb, (ND));                                                                    \
        if (form == 0) {                                                                       \
            bool ok = cint_as_##D##_from_##SRC(ctx, W_SITE, a, &r);                            \
            OUTCOME(ok, r, (ND), (DTAG));                                                      \
        } else {                                                                               \
            if (form == 1) {                                                                   \
                cint_as_wrap_##D##_from_##SRC(a, &r);                                          \
            } else {                                                                           \
                cint_as_sat_##D##_from_##SRC(a, &r);                                           \
            }                                                                                  \
            VALUE(r, (DTAG));                                                                  \
        }                                                                                      \
        return 1;                                                                              \
    }
#define AS_NARROW(D, CD, DTAG, SRC)                                                            \
    case DTAG: {                                                                               \
        narrow_out o;                                                                          \
        uint64_t bits;                                                                         \
        o.u64 = SENTINEL;                                                                      \
        if (form == 0) {                                                                       \
            CD v = 0;                                                                          \
            if (!cint_as_##D##_from_##SRC(ctx, W_SITE, a, &v)) {                               \
                put_fault(ctx, res);                                                           \
                return 1;                                                                      \
            }                                                                                  \
            o.D = v;                                                                           \
        } else if (form == 1) {                                                                \
            o.D = cint_as_wrap_##D##_from_##SRC(a);                                            \
        } else {                                                                               \
            o.D = cint_as_sat_##D##_from_##SRC(a);                                             \
        }                                                                                      \
        bits = narrow_bits((DTAG), &o);                                                        \
        res[0] = 'V';                                                                          \
        res[1] = ' ';                                                                          \
        put_value(res + 2, (DTAG), &bits);                                                     \
        return 1;                                                                              \
    }
#define RUN_CONV_FROM(SRC)                                                                      \
    static int run_conv_from_##SRC(cint_ctx *ctx, const char *kind, uint32_t aux,              \
                                   const cint_##SRC *a, const cint_##SRC *b, cint_count k,     \
                                   char *res, char (*alias)[64])                               \
    {                                                                                           \
        int form = conv_form(kind);                                                             \
        if (form < 0) return extra_##SRC(ctx, kind, a, b, k, res, alias);                       \
        switch (aux) {                                                                          \
            AS_WIDE(i128, 2u, CINT_TAG_I128, SRC)                                               \
            AS_WIDE(i256, 4u, CINT_TAG_I256, SRC)                                               \
            AS_WIDE(i512, 8u, CINT_TAG_I512, SRC)                                               \
            AS_WIDE(i1024, 16u, CINT_TAG_I1024, SRC)                                            \
            AS_WIDE(u128, 2u, CINT_TAG_U128, SRC)                                               \
            AS_WIDE(u256, 4u, CINT_TAG_U256, SRC)                                               \
            AS_WIDE(u512, 8u, CINT_TAG_U512, SRC)                                               \
            AS_NARROW(i8, int8_t, CINT_TAG_I8, SRC)                                             \
            AS_NARROW(i16, int16_t, CINT_TAG_I16, SRC)                                          \
            AS_NARROW(i32, int32_t, CINT_TAG_I32, SRC)                                          \
            AS_NARROW(i64, int64_t, CINT_TAG_I64, SRC)                                          \
            AS_NARROW(u8, uint8_t, CINT_TAG_U8, SRC)                                            \
            AS_NARROW(u16, uint16_t, CINT_TAG_U16, SRC)                                         \
            AS_NARROW(u32, uint32_t, CINT_TAG_U32, SRC)                                         \
            AS_NARROW(u64, uint64_t, CINT_TAG_U64, SRC)                                         \
        default:                                                                                \
            return 0;                                                                           \
        }                                                                                       \
    }

/* The helpers one width has and another lacks: mul_full of a type of twice the
 * width, uabs of a signed type, and the rotations of an unsigned one. */
#define EXTRA_MUL_FULL(T, T2, TAG2)                         \
    if (strcmp(kind, "mul_full") == 0) {                    \
        cint_##T2 r2;                                       \
        cint_mul_full_##T(a, b, &r2);                       \
        VALUE(r2, (TAG2));                                  \
        return 1;                                           \
    }
#define EXTRA_UABS(T, U, UTAG)                              \
    if (strcmp(kind, "uabs") == 0) {                        \
        cint_##U r2;                                        \
        cint_uabs_##T(a, &r2);                              \
        VALUE(r2, (UTAG));                                  \
        return 1;                                           \
    }
#define EXTRA_ROTATE(T, N, TAG)                                                                       \
    if (strcmp(kind, "rotl") == 0 || strcmp(kind, "rotr") == 0) {                                    \
        bool (*h)(cint_ctx *, cint_site, const cint_##T *, cint_count, cint_##T *) =                   \
            kind[3] == 'l' ? cint_rotl_##T : cint_rotr_##T;                                            \
        cint_##T r;                                                                                    \
        bool ok;                                                                                       \
        fill(r.limb, (N));                                                                             \
        ok = h(ctx, W_SITE, a, k, &r);                                                                 \
        OUTCOME(ok, r, (N), (TAG));                                                                    \
        if (ok) {                                                                                      \
            cint_##T s_ = *a;                                                                          \
            if (!h(ctx, W_SITE, &s_, k, &s_) || memcmp(&s_, &r, sizeof r) != 0) strcpy(*alias, "slot aliasing a"); \
        }                                                                                              \
        return 1;                                                                                      \
    }
#define EXTRA_BEGIN(T)                                                                                  \
    static int extra_##T(cint_ctx *ctx, const char *kind, const cint_##T *a, const cint_##T *b,        \
                         cint_count k, char *res, char (*alias)[64])                                    \
    {                                                                                                   \
        (void)ctx;                                                                                      \
        (void)b;                                                                                        \
        (void)k;                                                                                        \
        (void)alias;
#define EXTRA_END \
    return 0;     \
    }

EXTRA_BEGIN(i128) EXTRA_MUL_FULL(i128, i256, CINT_TAG_I256) EXTRA_UABS(i128, u128, CINT_TAG_U128) EXTRA_END
EXTRA_BEGIN(i256) EXTRA_MUL_FULL(i256, i512, CINT_TAG_I512) EXTRA_UABS(i256, u256, CINT_TAG_U256) EXTRA_END
EXTRA_BEGIN(i512) EXTRA_MUL_FULL(i512, i1024, CINT_TAG_I1024) EXTRA_UABS(i512, u512, CINT_TAG_U512) EXTRA_END
EXTRA_BEGIN(i1024) (void)a; (void)kind; (void)res; EXTRA_END
EXTRA_BEGIN(u128) EXTRA_MUL_FULL(u128, u256, CINT_TAG_U256) EXTRA_ROTATE(u128, 2u, CINT_TAG_U128) EXTRA_END
EXTRA_BEGIN(u256) EXTRA_MUL_FULL(u256, u512, CINT_TAG_U512) EXTRA_ROTATE(u256, 4u, CINT_TAG_U256) EXTRA_END
EXTRA_BEGIN(u512) EXTRA_ROTATE(u512, 8u, CINT_TAG_U512) EXTRA_END

RUN_CONV_FROM(i128)
RUN_CONV_FROM(i256)
RUN_CONV_FROM(i512)
RUN_CONV_FROM(i1024)
RUN_CONV_FROM(u128)
RUN_CONV_FROM(u256)
RUN_CONV_FROM(u512)
RUN_WIDE(i128, 2u, CINT_TAG_I128)
RUN_WIDE(i256, 4u, CINT_TAG_I256)
RUN_WIDE(i512, 8u, CINT_TAG_I512)
RUN_WIDE(i1024, 16u, CINT_TAG_I1024)
RUN_WIDE(u128, 2u, CINT_TAG_U128)
RUN_WIDE(u256, 4u, CINT_TAG_U256)
RUN_WIDE(u512, 8u, CINT_TAG_U512)

/* A narrow source of tag x->tag, s or u its value, to the type D in the form of
 * conv_form: into the slot r, or as the value v of a narrow D. */
#define FROM_ONE(D, S, CS, V)                                              \
    (form == 0   ? cint_as_##D##_from_##S(ctx, W_SITE, (CS)(V), &r)        \
     : form == 1 ? (cint_as_wrap_##D##_from_##S((CS)(V), &r), true)        \
                 : (cint_as_sat_##D##_from_##S((CS)(V), &r), true))
#define NARROW_ONE(D, S, CS, V)                                            \
    (form == 0   ? cint_as_##D##_from_##S(ctx, W_SITE, (CS)(V), &v)        \
     : form == 1 ? (v = cint_as_wrap_##D##_from_##S((CS)(V)), true)         \
                 : (v = cint_as_sat_##D##_from_##S((CS)(V)), true))
#define FROM_EACH(ONE, D)                                                  \
    switch (x->tag) {                                                      \
    case CINT_TAG_I8: ok = ONE(D, i8, int8_t, s); break;                   \
    case CINT_TAG_I16: ok = ONE(D, i16, int16_t, s); break;                \
    case CINT_TAG_I32: ok = ONE(D, i32, int32_t, s); break;                \
    case CINT_TAG_I64: ok = ONE(D, i64, int64_t, s); break;                \
    case CINT_TAG_U8: ok = ONE(D, u8, uint8_t, u); break;                  \
    case CINT_TAG_U16: ok = ONE(D, u16, uint16_t, u); break;               \
    case CINT_TAG_U32: ok = ONE(D, u32, uint32_t, u); break;               \
    default: ok = ONE(D, u64, uint64_t, u); break;                         \
    }

/* A narrow source: a conversion to a wide type (a fault only for a negative value
 * into U128 to U512), or to a narrow one, or mul_full of I64 or U64. */
#define FROM_NARROW(D, ND, DTAG)                                           \
    case DTAG: {                                                           \
        cint_##D r;                                                        \
        fill(r.limb, (ND));                                                \
        FROM_EACH(FROM_ONE, D)                                             \
        OUTCOME(ok, r, (ND), (DTAG));                                      \
        return 1;                                                          \
    }
#define NARROW_TO(D, CD, DTAG)                                             \
    case DTAG: {                                                           \
        narrow_out o;                                                      \
        uint64_t bits;                                                     \
        CD v = 0;                                                          \
        FROM_EACH(NARROW_ONE, D)                                           \
        if (!ok) {                                                         \
            put_fault(ctx, res);                                           \
            return 1;                                                      \
        }                                                                  \
        o.u64 = SENTINEL;                                                  \
        o.D = v;                                                           \
        bits = narrow_bits((DTAG), &o);                                    \
        res[0] = 'V';                                                      \
        res[1] = ' ';                                                      \
        put_value(res + 2, (DTAG), &bits);                                 \
        return 1;                                                          \
    }

/* A narrow left operand shifted by a count of any type, narrow or wide (RT-OQ-52). */
#define NARROW_SHIFT(T, CT, TAG, K, V)                                                                \
    case TAG: {                                                                                       \
        CT a = (CT)(V), r, r0;                                                                        \
        uint64_t bits;                                                                                \
        bool ok;                                                                                      \
        memset(&r, 0xA5, sizeof r);                                                                   \
        r0 = r;                                                                                       \
        if (strcmp(kind, "shl_result") == 0) {                                                        \
            uint16_t e = cint_shl_result_##T(a, k, &r);                                               \
            if (e == 0u) {                                                                            \
                bits = CINT_RT_BITS_##K(r);                                                           \
                strcpy(res, "R 0 ");                                                                  \
                put_value(res + 4, (TAG), &bits);                                                     \
            } else {                                                                                  \
                sprintf(res, "R %u", (unsigned)e);                                                    \
                if (memcmp(&r, &r0, sizeof r) != 0) strcpy(*alias, "a failed result form wrote its slot"); \
            }                                                                                         \
            return 1;                                                                                 \
        }                                                                                             \
        ok = strcmp(kind, "shl") == 0   ? cint_shl_##T(ctx, W_SITE, a, k, &r)                         \
             : strcmp(kind, "shr") == 0 ? cint_shr_##T(ctx, W_SITE, a, k, &r)                         \
                                        : cint_shl_wrap_##T(ctx, W_SITE, a, k, &r);                   \
        if (ok) {                                                                                     \
            bits = CINT_RT_BITS_##K(r);                                                               \
            res[0] = 'V';                                                                             \
            res[1] = ' ';                                                                             \
            put_value(res + 2, (TAG), &bits);                                                         \
        } else {                                                                                      \
            if (memcmp(&r, &r0, sizeof r) != 0) strcpy(*alias, "a faulting helper wrote its slot");   \
            put_fault(ctx, res);                                                                      \
        }                                                                                             \
        return 1;                                                                                     \
    }

static int run_narrow_shift(cint_ctx *ctx, const char *kind, const warg *x, char *res, char (*alias)[64])
{
    uint64_t u = x->limb[0];
    int64_t s;
    wcount kw;
    cint_count k = make_count(&x[1], &kw);
    memcpy(&s, &u, sizeof s);
    switch (x->tag) {
        NARROW_SHIFT(i8, int8_t, CINT_TAG_I8, S, s)
        NARROW_SHIFT(i16, int16_t, CINT_TAG_I16, S, s)
        NARROW_SHIFT(i32, int32_t, CINT_TAG_I32, S, s)
        NARROW_SHIFT(i64, int64_t, CINT_TAG_I64, S, s)
        NARROW_SHIFT(u8, uint8_t, CINT_TAG_U8, U, u)
        NARROW_SHIFT(u16, uint16_t, CINT_TAG_U16, U, u)
        NARROW_SHIFT(u32, uint32_t, CINT_TAG_U32, U, u)
        NARROW_SHIFT(u64, uint64_t, CINT_TAG_U64, U, u)
    default: return 0;
    }
}

static int run_narrow(cint_ctx *ctx, const char *kind, uint32_t aux, const warg *x, int nargs, char *res,
                      char (*alias)[64])
{
    bool ok;
    int form = conv_form(kind);
    uint64_t u = x->limb[0];
    int64_t s;
    memcpy(&s, &u, sizeof s);
    if (strcmp(kind, "mul_full") == 0 && x->tag == CINT_TAG_I64 && nargs == 2) {
        cint_i128 r;
        int64_t t;
        memcpy(&t, &x[1].limb[0], sizeof t);
        cint_mul_full_i64(s, t, &r);
        VALUE(r, CINT_TAG_I128);
        return 1;
    }
    if (strcmp(kind, "mul_full") == 0 && x->tag == CINT_TAG_U64 && nargs == 2) {
        cint_u128 r;
        cint_mul_full_u64(u, x[1].limb[0], &r);
        VALUE(r, CINT_TAG_U128);
        return 1;
    }
    if (nargs == 2 && (strcmp(kind, "shl") == 0 || strcmp(kind, "shl_wrap") == 0 || strcmp(kind, "shr") == 0 ||
                       strcmp(kind, "shl_result") == 0)) {
        return run_narrow_shift(ctx, kind, x, res, alias);
    }
    if (form < 0) return 0;
    switch (aux) {
        FROM_NARROW(i128, 2u, CINT_TAG_I128)
        FROM_NARROW(i256, 4u, CINT_TAG_I256)
        FROM_NARROW(i512, 8u, CINT_TAG_I512)
        FROM_NARROW(i1024, 16u, CINT_TAG_I1024)
        FROM_NARROW(u128, 2u, CINT_TAG_U128)
        FROM_NARROW(u256, 4u, CINT_TAG_U256)
        FROM_NARROW(u512, 8u, CINT_TAG_U512)
        NARROW_TO(i8, int8_t, CINT_TAG_I8)
        NARROW_TO(i16, int16_t, CINT_TAG_I16)
        NARROW_TO(i32, int32_t, CINT_TAG_I32)
        NARROW_TO(i64, int64_t, CINT_TAG_I64)
        NARROW_TO(u8, uint8_t, CINT_TAG_U8)
        NARROW_TO(u16, uint16_t, CINT_TAG_U16)
        NARROW_TO(u32, uint32_t, CINT_TAG_U32)
        NARROW_TO(u64, uint64_t, CINT_TAG_U64)
    default:
        return 0;
    }
}

static int run_case(cint_ctx *ctx, char *line, char *got, const char **want, char (*alias)[64])
{
    char *field[24];
    int nf = 0, nargs, i;
    char *p = line, *eq = strstr(line, " = ");
    char kind[24];
    unsigned long tag, mode, aux;
    warg x[3];
    if (eq == NULL) return 0;
    *eq = '\0';
    *want = eq + 3;
    while (*p != '\0' && nf < 24) {
        field[nf++] = p;
        while (*p != '\0' && *p != ' ') p++;
        if (*p == ' ') *p++ = '\0';
    }
    if (nf < 6 || strlen(field[1]) >= sizeof kind) return 0;
    strcpy(kind, field[1]);
    tag = strtoul(field[2], NULL, 10);
    mode = strtoul(field[3], NULL, 10);
    aux = strtoul(field[4], NULL, 10);
    nargs = nf - 5;
    if (nargs > 3) return 0;
    for (i = 0; i < nargs; i++) {
        if (!parse_arg(field[5 + i], &x[i])) return 0;
    }
    (*alias)[0] = '\0';
    switch (tag) {
    case CINT_TAG_I128: return run_i128(ctx, kind, (uint32_t)mode, (uint32_t)aux, x, nargs, got, alias);
    case CINT_TAG_I256: return run_i256(ctx, kind, (uint32_t)mode, (uint32_t)aux, x, nargs, got, alias);
    case CINT_TAG_I512: return run_i512(ctx, kind, (uint32_t)mode, (uint32_t)aux, x, nargs, got, alias);
    case CINT_TAG_I1024: return run_i1024(ctx, kind, (uint32_t)mode, (uint32_t)aux, x, nargs, got, alias);
    case CINT_TAG_U128: return run_u128(ctx, kind, (uint32_t)mode, (uint32_t)aux, x, nargs, got, alias);
    case CINT_TAG_U256: return run_u256(ctx, kind, (uint32_t)mode, (uint32_t)aux, x, nargs, got, alias);
    case CINT_TAG_U512: return run_u512(ctx, kind, (uint32_t)mode, (uint32_t)aux, x, nargs, got, alias);
    default: return run_narrow(ctx, kind, (uint32_t)aux, x, nargs, got, alias);
    }
}

/* A wide operand, exact and limit render as cint_ref renders them (cint_tvalue_render). */
static void test_render(cint_ctx *ctx)
{
    static const char min1024[] =
        "89884656743115795386465259539451236680898848947115328636715040578866337902750481566354238661203768010560056939935696678829394884407208311246423715319737062188883946712432742638151109800623047059726541476042502884419075341171231440736956555270413618581675255342293149119973622969239858152417678164812112068608";
    static const char max1024[] =
        "89884656743115795386465259539451236680898848947115328636715040578866337902750481566354238661203768010560056939935696678829394884407208311246423715319737062188883946712432742638151109800623047059726541476042502884419075341171231440736956555270413618581675255342293149119973622969239858152417678164812112068607";
    static cint_fault_record rec;
    char buf[400], want[400];
    cint_i1024 a, r;
    cint_i128 b, one = {{1u, 0u}};
    memset(&a, 0, sizeof a);
    a.limb[15] = (uint64_t)1 << 63;
    if (cint_neg_i1024(ctx, W_SITE, &a, &r) || cint_ctx_fault(ctx, &rec) != CINT_OK) {
        g_fail++;
        fprintf(stderr, "FAIL neg of MIN(I1024) faults\n");
        return;
    }
    snprintf(want, sizeof want, "I1024 -%s", min1024);
    if (rec.operand_count != 1u || cint_tvalue_render(&rec.operands[0], buf, sizeof buf) == 0u ||
        strcmp(buf, want) != 0) {
        g_fail++;
        fprintf(stderr, "FAIL the I1024 operand renders as %s\n", want);
    }
    if (!rec.has_exact || cint_tvalue_render_decimal(&rec.exact, buf, sizeof buf) == 0u || strcmp(buf, min1024) != 0) {
        g_fail++;
        fprintf(stderr, "FAIL the exact renders as 2^1023\n");
    }
    snprintf(want, sizeof want, "I1024 %s", max1024);
    if (!rec.has_limit || cint_tvalue_render(&rec.limit, buf, sizeof buf) == 0u || strcmp(buf, want) != 0) {
        g_fail++;
        fprintf(stderr, "FAIL the limit renders as MAX(I1024)\n");
    }
    (void)cint_ctx_clear_fault(ctx);
    b.limb[0] = ~(uint64_t)0;
    b.limb[1] = ~(uint64_t)0 >> 1;
    if (cint_add_i128(ctx, W_SITE, &b, &one, &b) || cint_ctx_fault(ctx, &rec) != CINT_OK ||
        cint_tvalue_render(&rec.operands[0], buf, sizeof buf) == 0u ||
        strcmp(buf, "I128 170141183460469231731687303715884105727") != 0) {
        g_fail++;
        fprintf(stderr, "FAIL MAX(I128) + 1 faults and its operand renders\n");
    }
    (void)cint_ctx_clear_fault(ctx);
    {
        cint_u128 z = {{0u, 0u}}, u1 = {{1u, 0u}}, ur;
        if (cint_sub_u128(ctx, W_SITE, &z, &u1, &ur) || cint_ctx_fault(ctx, &rec) != CINT_OK ||
            rec.operand_count != 2u || cint_tvalue_render(&rec.operands[0], buf, sizeof buf) == 0u ||
            strcmp(buf, "U128 0") != 0 || !rec.has_exact ||
            cint_tvalue_render_decimal(&rec.exact, buf, sizeof buf) == 0u || strcmp(buf, "-1") != 0 ||
            !rec.has_limit || cint_tvalue_render(&rec.limit, buf, sizeof buf) == 0u || strcmp(buf, "U128 0") != 0) {
            g_fail++;
            fprintf(stderr, "FAIL 0 - 1 on U128 faults with exact -1 and limit U128 0\n");
        }
        (void)cint_ctx_clear_fault(ctx);
        memset(&z, 0xff, sizeof z);
        if (cint_add_u128(ctx, W_SITE, &z, &u1, &ur) || cint_ctx_fault(ctx, &rec) != CINT_OK ||
            cint_tvalue_render(&rec.operands[0], buf, sizeof buf) == 0u ||
            strcmp(buf, "U128 340282366920938463463374607431768211455") != 0) {
            g_fail++;
            fprintf(stderr, "FAIL MAX(U128) + 1 faults and its operand renders\n");
        }
        (void)cint_ctx_clear_fault(ctx);
    }
}

/* Module state of wide types in a checkpoint (rt/cint_state.c; SPEC-01 IM-146, IM-156): app/w.ci
 * holds big I128 at 0 (-2), huge I1024 at 16 (2^1000), xs I256[2] at 144 ([1, -1]) and z U256
 * at 208 (2^255 + 1). The
 * checkpoint carries each payload as its w / 8 little-endian bytes, the decoder and its text
 * read them, cint_state_render writes their lines, a restore brings a value back, and fault
 * records with I1024 and U512 operands survive a checkpoint and a restore into another context. */
static const cint_site_info ws_sites[] = {{0u, 0u, NULL}, {2u, 7u, "add.checked.i1024"}};
static const cint_module ws_module = {"app/w.ci", 8u, 2u, ws_sites};
static const cint_module *const ws_modules[] = {&ws_module};
static const uint8_t ws_rev[32] = {0x15};
static const cint_program ws_program = {1u, 0u, ws_modules, ws_rev};
static const int64_t ws_two[] = {2};
static const cint_state_var ws_vars[] = {{"big", 3u, CINT_TAG_I128, 0u, 0u, NULL, NULL},
                                         {"huge", 4u, CINT_TAG_I1024, 16u, 0u, NULL, NULL},
                                         {"xs", 2u, CINT_TAG_I256, 144u, 1u, ws_two, NULL},
                                         {"z", 1u, CINT_TAG_U256, 208u, 0u, NULL, NULL}};

typedef struct ws_bytes {
    uint8_t b[4096];
    size_t n;
} ws_bytes;

static cint_status ws_write(void *user, const uint8_t *bytes, size_t len)
{
    ws_bytes *x = (ws_bytes *)user;
    if (len > sizeof x->b - x->n) return CINT_RESOURCE;
    memcpy(x->b + x->n, bytes, len);
    x->n += len;
    return CINT_OK;
}

static int ws_take(cint_ctx *ctx, ws_bytes *x)
{
    cint_sink sink;
    x->n = 0u;
    sink.write = ws_write;
    sink.user = x;
    return cint_ctx_checkpoint(ctx, &sink) == CINT_OK;
}

#define WCHECK(cond, what)                                    \
    do {                                                      \
        if (!(cond)) {                                        \
            g_fail++;                                         \
            fprintf(stderr, "FAIL state: %s\n", (what));      \
        }                                                     \
    } while (0)

static void test_state(void)
{
    static const char z_text[] = "57896044618658097711785492504343953926634992332820282019728792003956564819969";
    static const char huge_text[] = "10715086071862673209484250490600018105614048117055336074437503883703510511249361224931983788156958581275946729175531468251871452856923140435984577574698574803934567774824230985421074605062371141877954182153046474983581941267398767559165543946077062914571196477686542167660429831652624386837205668069376";
    static uint8_t init[240];
    static ws_bytes c1, c2, c3;
    static char text[8192], want[800];
    static cint_fault_record rec, back;
    static uint8_t enc1[4096], enc2[4096];
    cint_state st = {0u, 4u, 240u, 0u, init, ws_vars};
    const cint_state *states[1];
    cint_state_table table;
    cint_checkpoint_info info;
    cint_checkpoint_global g;
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL, *other = NULL;
    size_t cur = 0u, n1, n2;
    cint_i1024 max, one;
    uint8_t *block;
    memset(init, 0, sizeof init);
    memset(init, 0xff, 16u);
    init[0] = 0xfe;                    /* big = -2 */
    init[16 + 125] = 1u;               /* huge = 2^1000 */
    init[144] = 1u;                    /* xs[0] = 1 */
    memset(init + 176, 0xff, 32u);     /* xs[1] = -1 */
    init[208] = 1u;
    init[208 + 31] = 0x80u;            /* z = 2^255 + 1 */
    states[0] = &st;
    table.count = 1u;
    table.reserved = 0u;
    table.states = states;
    table.memory = NULL;
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = &ws_program;
    if (cint_ctx_create(&cfg, &ctx) != CINT_OK || cint_ctx_create(&cfg, &other) != CINT_OK) {
        WCHECK(0, "cint_ctx_create");
        cint_ctx_destroy(ctx);
        return;
    }
    WCHECK(cint_ctx_bind_state(ctx, &table) == CINT_OK && cint_ctx_bind_state(other, &table) == CINT_OK,
           "a table with I128, I1024, I256[2] and U256 binds");
    WCHECK(ws_take(ctx, &c1), "a checkpoint");
    WCHECK(cint_checkpoint_decode(c1.b, c1.n, &info) == CINT_OK && info.global_count == 4u, "it decodes");
    WCHECK(cint_checkpoint_global_next(&info, &cur, &g) == CINT_OK && g.elements == NULL && g.value.len == 17u &&
               g.value.bytes[0] == CINT_TAG_I128 && memcmp(g.value.bytes + 1, init, 16u) == 0,
           "big: tag 15 and 16 bytes");
    WCHECK(cint_checkpoint_global_next(&info, &cur, &g) == CINT_OK && g.elements == NULL && g.value.len == 129u &&
               g.value.bytes[0] == CINT_TAG_I1024 && memcmp(g.value.bytes + 1, init + 16, 128u) == 0,
           "huge: tag 18 and 128 bytes");
    WCHECK(cint_checkpoint_global_next(&info, &cur, &g) == CINT_OK && g.value.bytes[0] == 0x51u &&
               g.value.bytes[1] == CINT_TAG_I256 && g.element_count == 2u && g.elements != NULL &&
               memcmp(g.elements, init + 144, 64u) == 0,
           "xs: an array of I256, 32 bytes an element");
    WCHECK(cint_checkpoint_global_next(&info, &cur, &g) == CINT_OK && g.elements == NULL && g.value.len == 33u &&
               g.value.bytes[0] == CINT_TAG_U256 && memcmp(g.value.bytes + 1, init + 208, 32u) == 0 &&
               cint_checkpoint_global_next(&info, &cur, &g) == CINT_REFUSED,
           "z: tag 26 and 32 bytes");
    n1 = cint_checkpoint_render(&info, text, sizeof text);
    snprintf(want, sizeof want, "\nglobal app.w.huge I1024 %s\n", huge_text);
    WCHECK(n1 > 0u && n1 < sizeof text && strstr(text, "\nglobal app.w.big I128 -2\n") != NULL &&
               strstr(text, want) != NULL && strstr(text, "\nglobal app.w.xs I256[2] elements-sha256 ") != NULL,
           "the decoder's text names each wide type");
    snprintf(want, sizeof want, "\nglobal app.w.z U256 %s\n", z_text);
    WCHECK(strstr(text, want) != NULL, "the decoder's text renders U256 unsigned");
    n2 = cint_state_render(ctx, &table, text, sizeof text);
    snprintf(want, sizeof want, "state.global app.w big I128 -2\nstate.global app.w huge I1024 %s\n"
             "state.global app.w z U256 %s\n", huge_text, z_text);
    WCHECK(n2 == strlen(want) && memcmp(text, want, n2) == 0, "state.global lines of wide scalars");

    block = (uint8_t *)cint_rt_state(ctx, &st);
    WCHECK(block != NULL, "the block of app/w.ci");
    if (block != NULL) {
        memset(block, 0, 16u);
        block[0] = 5u;                 /* big = 5 */
    }
    WCHECK(ws_take(ctx, &c2) && c2.n == c1.n && memcmp(c2.b, c1.b, c1.n) != 0, "a store to big changes the state");
    WCHECK(cint_ctx_restore(ctx, c1.b, c1.n) == CINT_OK && ws_take(ctx, &c3) && c3.n == c1.n &&
               memcmp(c3.b, c1.b, c1.n) == 0,
           "a restore brings big back");

    memset(&max, 0xff, sizeof max);
    max.limb[15] >>= 1;
    memset(&one, 0, sizeof one);
    one.limb[0] = 1u;
    {
        const cint_site site = {0u, 1u};
        WCHECK(!cint_add_i1024(ctx, site, &max, &one, &max) && cint_ctx_fault(ctx, &rec) == CINT_OK,
               "MAX(I1024) + 1 faults");
    }
    n1 = cint_fault_encode(&rec, enc1, sizeof enc1);
    WCHECK(ws_take(ctx, &c2) && cint_checkpoint_decode(c2.b, c2.n, &info) == CINT_OK && info.fault != NULL &&
               info.fault_len == n1 && memcmp(info.fault, enc1, n1) == 0,
           "a faulted context's checkpoint holds the record with its I1024 operands, exact and limit");
    WCHECK(cint_ctx_restore(other, c2.b, c2.n) == CINT_OK && cint_ctx_fault(other, &back) == CINT_OK &&
               back.operand_count == 2u && back.operands[0].bytes[0] == CINT_TAG_I1024 &&
               (n2 = cint_fault_encode(&back, enc2, sizeof enc2)) == n1 && memcmp(enc1, enc2, n1) == 0,
           "the record comes back from a restore byte for byte");
    {
        const cint_site site = {0u, 1u};
        cint_u512 uz, u1, ur;
        memset(&uz, 0, sizeof uz);
        memset(&u1, 0, sizeof u1);
        u1.limb[0] = 1u;
        (void)cint_ctx_clear_fault(ctx);
        WCHECK(!cint_sub_u512(ctx, site, &uz, &u1, &ur) && cint_ctx_fault(ctx, &rec) == CINT_OK,
               "0 - 1 on U512 faults");
    }
    n1 = cint_fault_encode(&rec, enc1, sizeof enc1);
    WCHECK(ws_take(ctx, &c2) && cint_ctx_restore(other, c2.b, c2.n) == CINT_OK &&
               cint_ctx_fault(other, &back) == CINT_OK && back.operands[0].bytes[0] == CINT_TAG_U512 &&
               (n2 = cint_fault_encode(&back, enc2, sizeof enc2)) == n1 && memcmp(enc1, enc2, n1) == 0,
           "a record with U512 operands comes back from a restore byte for byte");
    {
        /* A run-time record of dot over I1024 elements is of version 3 (BX15-07 C; ABI 3.2,
         * OQ-263): 2^62 products of MIN(I1024) with itself sum to 2^2108, a Z of 264 bytes. */
        const cint_site site = {0u, 1u};
        cint_fault_record *r;
        (void)cint_ctx_clear_fault(ctx);
        r = cint_rt_record_begin(ctx, site, (uint16_t)CINT_E_OVERFLOW);
        if (r != NULL) {
            cint_rt_record_op(r, "dot");
            cint_rt_record_op(r, "checked");
            cint_rt_record_op(r, "i1024");
            cint_rt_record_op(r, "i1024");
            cint_rt_record_operand(r, CINT_TAG_I64, (uint64_t)1u << 62);
            r->has_exact = 1u;
            r->exact.len = 5u + 264u;
            memset(r->exact.bytes, 0, sizeof r->exact.bytes);
            r->exact.bytes[0] = CINT_TAG_Z;
            r->exact.bytes[1] = 8u; /* 264 = 0x108 */
            r->exact.bytes[2] = 1u;
            r->exact.bytes[5u + 263u] = 0x10u;
            r->has_limit = 1u;
            r->limit.len = 129u;
            r->limit.bytes[0] = CINT_TAG_I1024;
            memset(r->limit.bytes + 1, 0xff, 128u);
            r->limit.bytes[128] = 0x7fu;
        }
        WCHECK(r != NULL && cint_ctx_fault(ctx, &rec) == CINT_OK, "a record of dot.checked.i1024.i1024 begins");
    }
    n1 = cint_fault_encode(&rec, enc1, sizeof enc1);
    WCHECK(n1 > 0u && memcmp(enc1 + 4, "cint-core-1/fault/v3", 20u) == 0, "it encodes as version 3");
    WCHECK(ws_take(ctx, &c2) && cint_checkpoint_decode(c2.b, c2.n, &info) == CINT_OK && info.fault != NULL &&
               info.fault_len == n1 && memcmp(info.fault, enc1, n1) == 0 &&
               cint_checkpoint_render(&info, text, sizeof text) > 0u,
           "a checkpoint holds the version 3 record and renders");
    WCHECK(cint_ctx_restore(other, c2.b, c2.n) == CINT_OK && cint_ctx_fault(other, &back) == CINT_OK &&
               back.exact.len == 269u && (n2 = cint_fault_encode(&back, enc2, sizeof enc2)) == n1 &&
               memcmp(enc1, enc2, n1) == 0,
           "a version 3 record comes back from a restore byte for byte");
    cint_ctx_destroy(other);
    cint_ctx_destroy(ctx);
}

/* ------------------------------------------------------------------------- */
/* The limb steps against the portable ones (SPEC-09 EMIT-05, EMIT-12).       */

static uint64_t g_state = 0x9E3779B97F4A7C15u;

static uint64_t next_u64(void)
{
    /* xorshift64* (Vigna, 2016), as in test_rt.c. */
    g_state ^= g_state >> 12;
    g_state ^= g_state << 25;
    g_state ^= g_state >> 27;
    return g_state * 2685821657736338717u;
}

static const uint64_t step_special[] = {
    0u, 1u, 2u, 0x7fu, 0x80u, 0xffu, 0x7fffffffu, 0x80000000u, 0xffffffffu, 0x100000000u,
    0x7fffffffffffffffu, 0x8000000000000000u, 0x8000000000000001u, 0xfffffffffffffffeu, 0xffffffffffffffffu,
};
#define STEP_SPECIALS (sizeof step_special / sizeof step_special[0])

static uint64_t pick(void)
{
    uint64_t x = next_u64();
    switch (x & 7u) {
    case 0: return step_special[(x >> 8) % STEP_SPECIALS];
    case 1: return x >> (x >> 58);  /* small magnitudes */
    case 2: return (uint64_t)0 - (x >> (x >> 58));
    default: return next_u64();
    }
}

/* One product a * b and one quotient hi:lo / d (d normalized, hi < d) by both
 * bodies: 1 when they differ. */
static long step_differs(uint64_t a, uint64_t b, uint64_t hi, uint64_t lo, uint64_t d)
{
    uint64_t h1, h2, r1, r2;
    uint64_t p1 = cint_w_mul64(a, b, &h1), p2 = cint_rt_mul_wide(a, b, &h2);
    uint64_t q1 = cint_w_div21(hi, lo, d, &r1), q2 = cint_rt_div_wide(hi, lo, d, &r2);
    return p1 != p2 || h1 != h2 || q1 != q2 || r1 != r2 ? 1 : 0;
}

static long test_limb_steps(long pairs)
{
    long bad = 0, i;
    size_t x, y;
    for (x = 0u; x < STEP_SPECIALS; x++) {
        for (y = 0u; y < STEP_SPECIALS; y++) {
            uint64_t d = step_special[y] | (uint64_t)1 << 63, a = step_special[x];
            bad += step_differs(a, step_special[y], 0u, a, d);
            bad += step_differs(a, step_special[y], a % d, UINT64_MAX, d);
            bad += step_differs(a, step_special[y], d - 1u, a, d);
        }
    }
    for (i = 0; i < pairs; i++) {
        uint64_t a = pick(), b = pick(), d = b | (uint64_t)1 << 63;
        bad += step_differs(a, b, a % d, pick(), d);
    }
    printf("limb steps: %ld pairs, %ld disagree\n", pairs, bad);
    return bad;
}

int main(int argc, char **argv)
{
    static char line[8192];
    static char got[8192];
    static char copy[8192];
    char alias[64];
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL;
    FILE *f;
    long n = 0, pairs = 1000000;
    printf("helper mode: %s\n", CINT_RT_HELPER_MODE);
    if (argc != 2 && argc != 3) {
        fprintf(stderr, "usage: test_wide <cases> [pairs]\n");
        return 2;
    }
    if (argc == 3) pairs = strtol(argv[2], NULL, 10);
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = &w_program;
    if (cint_ctx_create(&cfg, &ctx) != CINT_OK) {
        fprintf(stderr, "FAIL cint_ctx_create\n");
        return 1;
    }
    if (test_wide_calls(ctx, W_SITE) != 0) g_fail++;
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
        if (!run_case(ctx, line, got, &want, &alias)) {
            g_fail++;
            if (g_fail <= 40) fprintf(stderr, "FAIL unsupported case: %.200s\n", copy);
        } else if (strcmp(got, want) != 0 || alias[0] != '\0') {
            g_fail++;
            if (g_fail <= 40) {
                fprintf(stderr, "FAIL %.200s\n  want %.600s\n  got  %.600s%s%s\n", copy, want, got,
                        alias[0] != '\0' ? "\n  also " : "", alias);
            }
        } else {
            g_pass++;
        }
    }
    fclose(f);
    test_render(ctx);
    cint_ctx_destroy(ctx);
    test_state();
    if (test_limb_steps(pairs) != 0) g_fail++;
    printf("wide cases: %ld, passed %ld, failed %ld\n", n, g_pass, g_fail);
    return g_fail == 0 && n > 0 ? 0 : 1;
}
