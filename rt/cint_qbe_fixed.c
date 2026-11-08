/* cint_qbe_fixed.c: the cpu-qbe backend's fixed-point entry points (SPEC-09 BACK-18, Q3;
 * rt/OPEN.md RT-OQ-82), included by cint_rt.c after cint_qbe_wide.c, whose CINT_QW_FN it uses.
 *
 * Emitted C calls the static inline helpers of cint_fixed.h; QBE SSA can only call functions with
 * a symbol. cint_qf_<name>_<s> is cint_q_<name>_<s> (cint_qmul_<s> for qmul) out of line, for
 * storage s of 8 to 64 bits, with the operands emitted C gives it, except that a value is an
 * int64_t holding it extended from its type, a checked helper returns int32_t 0 or 1 and writes
 * its result through int64_t *out, and an error tag is an int32_t. The body is the helper, so
 * every record and rounding is the one emitted C makes (BACK-03). `+%`, `+|`, `-%`, `-|`,
 * add_result, sub_result, min and max are the storage integer's (IM-60) and go through
 * cint_qbe.c. No helper is reimplemented here. cint_qbe_rshr_count is rshr's count check (IM-192),
 * whose shift emitted QBE writes inline, as emitted C does. */

#if defined(CINT_RT_LIBRARY) && !defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
#pragma GCC visibility push(hidden)
#endif

#define CINT_QF_ID(...) __VA_ARGS__

/* A checked helper H_<s> with parameters P after ctx and site, called with arguments A. */
#define CINT_QF_CHECKED(S, CT, NAME, H, P, A)                                                       \
    CINT_QW_FN(int32_t, cint_qf_##NAME##_##S, (cint_ctx * ctx, cint_site site, CINT_QF_ID P, int64_t *out)) \
    {                                                                                               \
        CT r = 0;                                                                                   \
        if (!H##_##S(ctx, site, CINT_QF_ID A, &r)) return 0;                                        \
        *out = (int64_t)r;                                                                          \
        return 1;                                                                                   \
    }

/* A result-returning helper: its tag, and the value when the tag is 0. */
#define CINT_QF_RESULT(S, CT, NAME, P, A)                                                       \
    CINT_QW_FN(int32_t, cint_qf_##NAME##_result_##S, (CINT_QF_ID P, int64_t *out))              \
    {                                                                                           \
        CT r = 0;                                                                               \
        uint16_t e = cint_q_##NAME##_result_##S(CINT_QF_ID A, &r);                              \
        if (e == 0) *out = (int64_t)r;                                                          \
        return (int32_t)e;                                                                      \
    }

/* A form that never faults. */
#define CINT_QF_PLAIN(S, CT, NAME)                                                               \
    CINT_QW_FN(int64_t, cint_qf_##NAME##_##S, (uint32_t f, uint32_t mode, int64_t a, int64_t b)) \
    {                                                                                            \
        return (int64_t)cint_q_##NAME##_##S(f, mode, (CT)a, (CT)b);                              \
    }

/* Every helper of storage s (C type CT). */
#define CINT_QF_STORAGE(S, CT)                                                                                  \
    CINT_QF_CHECKED(S, CT, add, cint_q_add, (uint32_t f, int64_t a, int64_t b), (f, (CT)a, (CT)b))              \
    CINT_QF_CHECKED(S, CT, sub, cint_q_sub, (uint32_t f, int64_t a, int64_t b), (f, (CT)a, (CT)b))              \
    CINT_QF_CHECKED(S, CT, neg, cint_q_neg, (uint32_t f, int64_t a), (f, (CT)a))                                \
    CINT_QF_CHECKED(S, CT, abs, cint_q_abs, (uint32_t f, int64_t a), (f, (CT)a))                                \
    CINT_QF_CHECKED(S, CT, mul, cint_q_mul, (uint32_t f, uint32_t mode, int64_t a, int64_t b),                  \
                    (f, mode, (CT)a, (CT)b))                                                                    \
    CINT_QF_CHECKED(S, CT, div, cint_q_div, (uint32_t f, uint32_t mode, int64_t a, int64_t b),                  \
                    (f, mode, (CT)a, (CT)b))                                                                    \
    CINT_QF_CHECKED(S, CT, sqrt, cint_q_sqrt, (uint32_t f, uint32_t mode, int64_t a), (f, mode, (CT)a))         \
    CINT_QF_CHECKED(S, CT, mul_int, cint_q_mul_int, (uint32_t f, uint32_t ntag, int64_t a, int64_t n),          \
                    (f, ntag, (CT)a, (uint64_t)n))                                                              \
    CINT_QF_CHECKED(S, CT, qmul, cint_qmul,                                                                     \
                    (uint32_t ta, uint32_t tb, uint32_t f, uint32_t mode, int64_t a, int64_t b),                \
                    (ta, tb, f, mode, a, b))                                                                    \
    CINT_QF_PLAIN(S, CT, mul_wrap) CINT_QF_PLAIN(S, CT, mul_sat)                                                      \
    CINT_QF_RESULT(S, CT, mul, (uint32_t f, uint32_t mode, int64_t a, int64_t b), (f, mode, (CT)a, (CT)b))      \
    CINT_QF_RESULT(S, CT, div, (uint32_t f, uint32_t mode, int64_t a, int64_t b), (f, mode, (CT)a, (CT)b))

CINT_QF_STORAGE(i8, int8_t)
CINT_QF_STORAGE(i16, int16_t)
CINT_QF_STORAGE(i32, int32_t)
CINT_QF_STORAGE(i64, int64_t)

/* Conversions into type r (C type CR, extension K of cint_qbe.c) with fixed point on one side:
 * the source's 64-bit pattern and the two type words, as emitted C passes them. */
#define CINT_QF_CONV(R, CR, K)                                                                                   \
    CINT_QW_FN(int32_t, cint_qf_as_##R,                                                                          \
               (cint_ctx * ctx, cint_site site, uint32_t from, uint32_t to, uint32_t mode, int64_t x, int64_t *out)) \
    {                                                                                                            \
        CR r = 0;                                                                                                \
        if (!cint_q_as_##R(ctx, site, from, to, mode, (uint64_t)x, &r)) return 0;                                \
        *out = CINT_QBE_##K(r);                                                                                  \
        return 1;                                                                                                \
    }                                                                                                            \
    CINT_QW_FN(int64_t, cint_qf_as_wrap_##R, (uint32_t from, uint32_t to, uint32_t mode, int64_t x))             \
    {                                                                                                            \
        return CINT_QBE_##K(cint_q_as_wrap_##R(from, to, mode, (uint64_t)x));                                    \
    }                                                                                                            \
    CINT_QW_FN(int64_t, cint_qf_as_sat_##R, (uint32_t from, uint32_t to, uint32_t mode, int64_t x))              \
    {                                                                                                            \
        return CINT_QBE_##K(cint_q_as_sat_##R(from, to, mode, (uint64_t)x));                                     \
    }                                                                                                            \
    CINT_QW_FN(int32_t, cint_qf_as_result_##R, (uint32_t from, uint32_t to, uint32_t mode, int64_t x, int64_t *out)) \
    {                                                                                                            \
        CR r = 0;                                                                                                \
        uint16_t e = cint_q_as_result_##R(from, to, mode, (uint64_t)x, &r);                                      \
        if (e == 0) *out = CINT_QBE_##K(r);                                                                      \
        return (int32_t)e;                                                                                       \
    }
CINT_QF_CONV(i8, int8_t, S)
CINT_QF_CONV(i16, int16_t, S)
CINT_QF_CONV(i32, int32_t, S)
CINT_QF_CONV(i64, int64_t, S)
CINT_QF_CONV(u8, uint8_t, U)
CINT_QF_CONV(u16, uint16_t, U)
CINT_QF_CONV(u32, uint32_t, U)
CINT_QF_CONV(u64, uint64_t, U)

/* rshr's count (SPEC-01 IM-192): 1 when the count of pattern kbits and tag ktag is in 0 to
 * w - 1 for the type word t (an integer tag or a fixed-point word), else the record of
 * cint_rt_rshr_fault and 0. Emitted QBE calls it only for a count that is not a constant in
 * range; the shift itself is inline, as emitted C's is. */
CINT_QW_FN(int32_t, cint_qbe_rshr_count,
           (cint_ctx * ctx, cint_site site, uint32_t t, int64_t a, int64_t kbits, uint32_t ktag))
{
    cint_count k = {(uint64_t)kbits, ktag, 0u, NULL};
    if (k.bits <= (uint64_t)(cint_rt_tag_width(t) - 1u)) return 1;
    return cint_rt_rshr_fault(ctx, site, t, (uint64_t)a, k) ? 1 : 0;
}

#if defined(CINT_RT_LIBRARY) && !defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
#pragma GCC visibility pop
#endif
