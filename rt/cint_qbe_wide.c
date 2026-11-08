/* cint_qbe_wide.c: the cpu-qbe backend's wide-integer entry points (SPEC-09 BACK-18, Q3;
 * rt/OPEN.md RT-OQ-82), included by cint_rt.c after cint_qbe.c.
 *
 * Emitted C calls the static inline helpers of cint_wide.h; QBE SSA can only call functions with
 * a symbol. cint_qw_<name> is cint_<name> out of line, with the same operands, except that a
 * success flag or a Bool is an int32_t 0 or 1, an integer of 64 bits or fewer is an int64_t
 * holding its value (sign- or zero-extended from its type), and a shift or rotation count is its
 * operand's tag and address (cint_qw_count). The body is the helper, so every record, site and
 * width note is the one emitted C makes (BACK-03). No helper is reimplemented here. */

#if defined(CINT_RT_LIBRARY) && !defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
#pragma GCC visibility push(hidden)
#endif

/* A function and its prototype (the runtime builds with -Wmissing-prototypes). */
#define CINT_QW_FN(R, NAME, ARGS) R NAME ARGS; R NAME ARGS

/* The count of a shift or rotation from its operand: a narrow value in an int64_t cell, or the
 * limbs of a wide one, by the operand's type tag. */
static cint_count cint_qw_count(int32_t tag, const void *k)
{
    const int64_t *v = (const int64_t *)k;
    switch ((uint32_t)tag) {
    case CINT_TAG_I8: return cint_count_i8((int8_t)*v);
    case CINT_TAG_I16: return cint_count_i16((int16_t)*v);
    case CINT_TAG_I32: return cint_count_i32((int32_t)*v);
    case CINT_TAG_I64: return cint_count_i64(*v);
    case CINT_TAG_U8: return cint_count_u8((uint8_t)*v);
    case CINT_TAG_U16: return cint_count_u16((uint16_t)*v);
    case CINT_TAG_U32: return cint_count_u32((uint32_t)*v);
    case CINT_TAG_U64: return cint_count_u64((uint64_t)*v);
    case CINT_TAG_I128: return cint_count_i128((const cint_i128 *)k);
    case CINT_TAG_I256: return cint_count_i256((const cint_i256 *)k);
    case CINT_TAG_I512: return cint_count_i512((const cint_i512 *)k);
    case CINT_TAG_I1024: return cint_count_i1024((const cint_i1024 *)k);
    case CINT_TAG_U128: return cint_count_u128((const cint_u128 *)k);
    case CINT_TAG_U256: return cint_count_u256((const cint_u256 *)k);
    default: return cint_count_u512((const cint_u512 *)k);
    }
}

#define CINT_QW_C3(T, OP)                                                                                  \
    CINT_QW_FN(int32_t, cint_qw_##OP##_##T,                                                                \
               (cint_ctx * ctx, cint_site site, const cint_##T *a, const cint_##T *b, cint_##T *out))      \
    {                                                                                                      \
        return cint_##OP##_##T(ctx, site, a, b, out) ? 1 : 0;                                              \
    }
#define CINT_QW_V3(T, OP)                                                                            \
    CINT_QW_FN(void, cint_qw_##OP##_##T, (const cint_##T *a, const cint_##T *b, cint_##T *out))      \
    {                                                                                                \
        cint_##OP##_##T(a, b, out);                                                                  \
    }
#define CINT_QW_V2(T, OP)                                                    \
    CINT_QW_FN(void, cint_qw_##OP##_##T, (const cint_##T *a, cint_##T *out)) \
    {                                                                        \
        cint_##OP##_##T(a, out);                                             \
    }
#define CINT_QW_R3(T, OP)                                                                              \
    CINT_QW_FN(int64_t, cint_qw_##OP##_result_##T, (const cint_##T *a, const cint_##T *b, cint_##T *out)) \
    {                                                                                                  \
        return (int64_t)cint_##OP##_result_##T(a, b, out);                                             \
    }
#define CINT_QW_CMP(T, OP)                                                               \
    CINT_QW_FN(int32_t, cint_qw_##OP##_##T, (const cint_##T *a, const cint_##T *b))      \
    {                                                                                    \
        return cint_##OP##_##T(a, b) ? 1 : 0;                                            \
    }
#define CINT_QW_SH(T, OP)                                                                                    \
    CINT_QW_FN(int32_t, cint_qw_##OP##_##T,                                                                  \
               (cint_ctx * ctx, cint_site site, const cint_##T *a, int32_t ktag, const void *k, cint_##T *out)) \
    {                                                                                                        \
        return cint_##OP##_##T(ctx, site, a, cint_qw_count(ktag, k), out) ? 1 : 0;                           \
    }

/* Every operation of one wide type T. */
#define CINT_QW_TYPE(T)                                                                                          \
    CINT_QW_C3(T, add) CINT_QW_C3(T, sub) CINT_QW_C3(T, mul) CINT_QW_C3(T, div) CINT_QW_C3(T, rem)               \
    CINT_QW_V3(T, add_wrap) CINT_QW_V3(T, sub_wrap) CINT_QW_V3(T, mul_wrap) CINT_QW_V3(T, add_sat)               \
    CINT_QW_V3(T, sub_sat) CINT_QW_V3(T, mul_sat) CINT_QW_V3(T, and) CINT_QW_V3(T, or) CINT_QW_V3(T, xor)        \
    CINT_QW_V3(T, min) CINT_QW_V3(T, max) CINT_QW_V2(T, neg_wrap) CINT_QW_V2(T, not)                             \
    CINT_QW_R3(T, add) CINT_QW_R3(T, sub) CINT_QW_R3(T, mul) CINT_QW_R3(T, div) CINT_QW_R3(T, rem)               \
    CINT_QW_CMP(T, eq) CINT_QW_CMP(T, ne) CINT_QW_CMP(T, lt) CINT_QW_CMP(T, le) CINT_QW_CMP(T, gt)               \
    CINT_QW_CMP(T, ge) CINT_QW_SH(T, shl) CINT_QW_SH(T, shl_wrap) CINT_QW_SH(T, shr)                             \
    CINT_QW_FN(int32_t, cint_qw_neg_##T, (cint_ctx * ctx, cint_site site, const cint_##T *a, cint_##T *out))     \
    {                                                                                                            \
        return cint_neg_##T(ctx, site, a, out) ? 1 : 0;                                                          \
    }                                                                                                            \
    CINT_QW_FN(int64_t, cint_qw_shl_result_##T, (const cint_##T *a, int32_t ktag, const void *k, cint_##T *out)) \
    {                                                                                                            \
        return (int64_t)cint_shl_result_##T(a, cint_qw_count(ktag, k), out);                                     \
    }                                                                                                            \
    CINT_QW_FN(int32_t, cint_qw_builtin_##T,                                                                     \
               (cint_ctx * ctx, cint_site site, int32_t op, int32_t mode, const cint_##T *a, const cint_##T *b,  \
                const cint_##T *c, cint_##T *out))                                                               \
    {                                                                                                            \
        return cint_builtin_##T(ctx, site, (uint32_t)op, (uint32_t)mode, a, b, c, out) ? 1 : 0;                  \
    }                                                                                                            \
    CINT_QW_FN(int32_t, cint_qw_muldiv_##T,                                                                      \
               (cint_ctx * ctx, cint_site site, int32_t rtag, int32_t mode, const cint_##T *a, const cint_##T *b, \
                const cint_##T *c, void *out))                                                                   \
    {                                                                                                            \
        return cint_muldiv_##T(ctx, site, (uint32_t)rtag, (uint32_t)mode, a, b, c, out) ? 1 : 0;                 \
    }

/* Conversions between wide type T and narrow type N (C type CT, extension K of cint_qbe.c). */
#define CINT_QW_NARROW(T, N, CT, K)                                                                            \
    CINT_QW_FN(int32_t, cint_qw_as_##T##_from_##N, (cint_ctx * ctx, cint_site site, int64_t x, cint_##T *out)) \
    {                                                                                                          \
        return cint_as_##T##_from_##N(ctx, site, (CT)x, out) ? 1 : 0;                                          \
    }                                                                                                          \
    CINT_QW_FN(void, cint_qw_as_wrap_##T##_from_##N, (int64_t x, cint_##T *out))                               \
    {                                                                                                          \
        cint_as_wrap_##T##_from_##N((CT)x, out);                                                               \
    }                                                                                                          \
    CINT_QW_FN(void, cint_qw_as_sat_##T##_from_##N, (int64_t x, cint_##T *out))                                \
    {                                                                                                          \
        cint_as_sat_##T##_from_##N((CT)x, out);                                                                \
    }                                                                                                          \
    CINT_QW_FN(int32_t, cint_qw_as_##N##_from_##T,                                                             \
               (cint_ctx * ctx, cint_site site, const cint_##T *x, int64_t *out))                              \
    {                                                                                                          \
        CT r = 0;                                                                                              \
        bool ok = cint_as_##N##_from_##T(ctx, site, x, &r);                                                    \
        if (ok) *out = CINT_QBE_##K(r);                                                                        \
        return ok ? 1 : 0;                                                                                     \
    }                                                                                                          \
    CINT_QW_FN(int64_t, cint_qw_as_wrap_##N##_from_##T, (const cint_##T *x))                                   \
    {                                                                                                          \
        return CINT_QBE_##K(cint_as_wrap_##N##_from_##T(x));                                                   \
    }                                                                                                          \
    CINT_QW_FN(int64_t, cint_qw_as_sat_##N##_from_##T, (const cint_##T *x))                                    \
    {                                                                                                          \
        return CINT_QBE_##K(cint_as_sat_##N##_from_##T(x));                                                   \
    }
#define CINT_QW_NARROWS(T)                                                                                  \
    CINT_QW_NARROW(T, i8, int8_t, S) CINT_QW_NARROW(T, i16, int16_t, S) CINT_QW_NARROW(T, i32, int32_t, S)   \
    CINT_QW_NARROW(T, i64, int64_t, S) CINT_QW_NARROW(T, u8, uint8_t, U) CINT_QW_NARROW(T, u16, uint16_t, U) \
    CINT_QW_NARROW(T, u32, uint32_t, U) CINT_QW_NARROW(T, u64, uint64_t, U)

/* Conversions from wide type S to wide type D. */
#define CINT_QW_WW(D, S)                                                                                         \
    CINT_QW_FN(int32_t, cint_qw_as_##D##_from_##S, (cint_ctx * ctx, cint_site site, const cint_##S *x, cint_##D *out)) \
    {                                                                                                            \
        return cint_as_##D##_from_##S(ctx, site, x, out) ? 1 : 0;                                                \
    }                                                                                                            \
    CINT_QW_FN(void, cint_qw_as_wrap_##D##_from_##S, (const cint_##S *x, cint_##D *out))                         \
    {                                                                                                            \
        cint_as_wrap_##D##_from_##S(x, out);                                                                     \
    }                                                                                                            \
    CINT_QW_FN(void, cint_qw_as_sat_##D##_from_##S, (const cint_##S *x, cint_##D *out))                          \
    {                                                                                                            \
        cint_as_sat_##D##_from_##S(x, out);                                                                      \
    }
#define CINT_QW_FROM(D)                                                                                    \
    CINT_QW_WW(D, i128) CINT_QW_WW(D, i256) CINT_QW_WW(D, i512) CINT_QW_WW(D, i1024) CINT_QW_WW(D, u128) \
    CINT_QW_WW(D, u256) CINT_QW_WW(D, u512)
#define CINT_QW_ALL(T) CINT_QW_TYPE(T) CINT_QW_NARROWS(T) CINT_QW_FROM(T)

CINT_QW_ALL(i128)
CINT_QW_ALL(i256)
CINT_QW_ALL(i512)
CINT_QW_ALL(i1024)
CINT_QW_ALL(u128)
CINT_QW_ALL(u256)
CINT_QW_ALL(u512)
#define CINT_QW_UABS(T, U)                                                   \
    CINT_QW_FN(void, cint_qw_uabs_##T, (const cint_##T *a, cint_##U *out)) \
    {                                                                      \
        cint_uabs_##T(a, out);                                             \
    }
CINT_QW_UABS(i128, u128)
CINT_QW_UABS(i256, u256)
CINT_QW_UABS(i512, u512)

/* Rotations of U128 to U512, and the exact products of twice the width. */
#define CINT_QW_ROT(T) CINT_QW_SH(T, rotl) CINT_QW_SH(T, rotr)
CINT_QW_ROT(u128)
CINT_QW_ROT(u256)
CINT_QW_ROT(u512)
#define CINT_QW_FULL(T, T2)                                                                         \
    CINT_QW_FN(void, cint_qw_mul_full_##T, (const cint_##T *a, const cint_##T *b, cint_##T2 *out)) \
    {                                                                                               \
        cint_mul_full_##T(a, b, out);                                                               \
    }
CINT_QW_FULL(i128, i256)
CINT_QW_FULL(i256, i512)
CINT_QW_FULL(i512, i1024)
CINT_QW_FULL(u128, u256)
CINT_QW_FULL(u256, u512)
CINT_QW_FN(void, cint_qw_mul_full_i64, (int64_t a, int64_t b, cint_i128 *out))
{
    cint_mul_full_i64(a, b, out);
}
CINT_QW_FN(void, cint_qw_mul_full_u64, (int64_t a, int64_t b, cint_u128 *out))
{
    cint_mul_full_u64((uint64_t)a, (uint64_t)b, out);
}

#if defined(CINT_RT_LIBRARY) && !defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
#pragma GCC visibility pop
#endif
