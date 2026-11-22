/* cint_qbe_kern.c: the cpu-qbe backend's kernel entry points (SPEC-09 BACK-18, SIMD-7;
 * rt/OPEN.md RT-OQ-82), included by cint_rt.c after cint_qbe_fixed.c, whose CINT_QW_FN it uses.
 *
 * Emitted C writes a kernel's dispatch (SPEC-02 F-1 to F-8, R-1 to R-7) with the runtime's
 * dispatch functions and with the static inline accumulator helpers of cint_rt.h section 14 and
 * cint_fixed.h. QBE SSA calls a dispatch function that returns nothing or an integer directly;
 * cint_qk_<name> is a helper QBE cannot call, out of line, in cint_qbe.c's uniform form: a value
 * is an int64_t holding it extended from its type, a success flag is an int32_t 0 or 1, never a
 * C bool, and a result goes through int64_t *out. The body is the helper, so every record,
 * operand, exact total and fuel charge is the one emitted C makes (BACK-03). No helper is
 * reimplemented here. */

#if defined(CINT_RT_LIBRARY) && !defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
#pragma GCC visibility push(hidden)
#endif

/* The sizes compiler/back_qbe.ci allocates for a cint_zacc, a cint_wzacc and a cint_vdesc. */
CINT_RT_STATIC_ASSERT(sizeof(cint_zacc) == 32, "cint_qbe_kern: cint_zacc is 32 bytes");
CINT_RT_STATIC_ASSERT(sizeof(cint_wzacc) == 264, "cint_qbe_kern: cint_wzacc is 264 bytes");
CINT_RT_STATIC_ASSERT(sizeof(cint_vdesc) == 88, "cint_qbe_kern: cint_vdesc is 88 bytes");

/* The context's fault flag after cint_rt_dispatch_push, as emitted C tests it. */
CINT_QW_FN(int32_t, cint_qk_faulted, (cint_ctx * ctx))
{
    return cint_rt_faulted(ctx) ? 1 : 0;
}

/* F-5 checks 5 to 9 and the staging of a written array parameter (cint_rt.h section 14). */
CINT_QW_FN(int32_t, cint_qk_bound, (cint_ctx * ctx, cint_site site))
{
    return cint_rt_dispatch_bound(ctx, site) ? 1 : 0;
}
CINT_QW_FN(int32_t, cint_qk_where, (cint_ctx * ctx, cint_site site, int64_t index, int64_t left, int64_t right))
{
    return cint_rt_fault_where(ctx, site, index, left, right) ? 1 : 0;
}
CINT_QW_FN(int32_t, cint_qk_alias,
           (cint_ctx * ctx, cint_site site, int32_t i, int32_t x, const cint_vdesc *p, const cint_vdesc *q,
            int32_t elem, int32_t writable))
{
    return cint_rt_dispatch_alias(ctx, site, (uint32_t)i, (uint32_t)x, p, q, (uint32_t)elem, (uint32_t)writable) ? 1
                                                                                                                 : 0;
}
CINT_QW_FN(int32_t, cint_qk_stage,
           (cint_ctx * ctx, cint_site site, cint_vdesc *stage, const cint_vdesc *arg, int64_t size, int32_t copy_in))
{
    return cint_rt_dispatch_stage(ctx, site, stage, arg, (size_t)size, copy_in != 0) ? 1 : 0;
}

/* The exact total of a sum: a contribution of `tag` (cint_zacc_add), and the epilogue of a
 * `min` or `max` with no contribution (always 0, with its record). */
CINT_QW_FN(void, cint_qk_zacc_add, (cint_zacc * a, int32_t tag, int64_t bits))
{
    cint_zacc_add(a, (uint32_t)tag, (uint64_t)bits);
}
CINT_QW_FN(int32_t, cint_qk_empty, (cint_ctx * ctx, cint_site site, int32_t max, int32_t tag))
{
    return cint_rt_fault_reduce_empty(ctx, site, max != 0, (uint32_t)tag) ? 1 : 0;
}

/* The accumulator of a scalar `out` of type T (CINT_RT_KERNEL_ACC): a fold_checked step on *acc,
 * the epilogue of `sum` into *out, and the total of `sum_wrap`. */
#define CINT_QK_ACC(T, CT)                                                                                    \
    CINT_QW_FN(int32_t, cint_qk_kfold_##T, (cint_ctx * ctx, cint_site site, int64_t n, int64_t *acc, int64_t x)) \
    {                                                                                                         \
        CT r = (CT)*acc;                                                                                      \
        if (!cint_kfold_##T(ctx, site, n, &r, (CT)x)) return 0;                                               \
        *acc = (int64_t)r;                                                                                    \
        return 1;                                                                                             \
    }                                                                                                         \
    CINT_QW_FN(int32_t, cint_qk_ksum_##T,                                                                     \
               (cint_ctx * ctx, cint_site site, int32_t elem, int64_t n, const cint_zacc *a, int64_t *out))   \
    {                                                                                                         \
        CT r = 0;                                                                                             \
        if (!cint_ksum_##T(ctx, site, (uint32_t)elem, n, a, &r)) return 0;                                    \
        *out = (int64_t)r;                                                                                    \
        return 1;                                                                                             \
    }                                                                                                         \
    CINT_QW_FN(int64_t, cint_qk_kwrap_##T, (const cint_zacc *a))                                              \
    {                                                                                                         \
        return (int64_t)cint_kwrap_##T(a);                                                                    \
    }
CINT_QK_ACC(i8, int8_t)
CINT_QK_ACC(i16, int16_t)
CINT_QK_ACC(i32, int32_t)
CINT_QK_ACC(i64, int64_t)
CINT_QK_ACC(u8, uint8_t)
CINT_QK_ACC(u16, uint16_t)
CINT_QK_ACC(u32, uint32_t)
CINT_QK_ACC(u64, uint64_t)

/* The same for fixed point of storage S of 8 to 64 bits and f fraction bits (cint_fixed.h). */
#define CINT_QK_QACC(S, CT)                                                                                 \
    CINT_QW_FN(int32_t, cint_qk_kfold_q_##S,                                                                \
               (cint_ctx * ctx, cint_site site, int32_t f, int64_t n, int64_t *acc, int64_t x))             \
    {                                                                                                       \
        CT r = (CT)*acc;                                                                                    \
        if (!cint_kfold_q_##S(ctx, site, (unsigned)f, n, &r, (CT)x)) return 0;                              \
        *acc = (int64_t)r;                                                                                  \
        return 1;                                                                                           \
    }                                                                                                       \
    CINT_QW_FN(int32_t, cint_qk_ksum_q_##S,                                                                 \
               (cint_ctx * ctx, cint_site site, int32_t f, int64_t n, const cint_zacc *a, int64_t *out))    \
    {                                                                                                       \
        CT r = 0;                                                                                           \
        if (!cint_ksum_q_##S(ctx, site, (unsigned)f, n, a, &r)) return 0;                                   \
        *out = (int64_t)r;                                                                                  \
        return 1;                                                                                           \
    }
CINT_QK_QACC(i8, int8_t)
CINT_QK_QACC(i16, int16_t)
CINT_QK_QACC(i32, int32_t)
CINT_QK_QACC(i64, int64_t)

/* A wide side (cint_wide.h, cint_fixed.h): the epilogue of `sum` into out, a C object of the
 * result's type (fixed point of 128-bit storage by its type word qt), and a fold_checked step. */
CINT_QW_FN(int32_t, cint_qk_wsum,
           (cint_ctx * ctx, cint_site site, int32_t elem, int32_t result, int64_t n, const cint_wzacc *a, void *out))
{
    return cint_rt_wzacc_fit(ctx, site, "sum", (uint32_t)elem, (uint32_t)result, n, a, out) ? 1 : 0;
}
CINT_QW_FN(int32_t, cint_qk_wsum_q,
           (cint_ctx * ctx, cint_site site, int32_t qt, int64_t n, const cint_wzacc *a, void *out))
{
    return cint_rt_wzacc_fit_q(ctx, site, (uint32_t)qt, n, a, out) ? 1 : 0;
}
CINT_QW_FN(int32_t, cint_qk_wfold,
           (cint_ctx * ctx, cint_site site, int32_t tag, int64_t j, uint64_t *acc, const uint64_t *x))
{
    return cint_rt_wide_fold_step(ctx, site, CINT_RT_OP_ADD, (uint32_t)tag, j, acc, x) ? 1 : 0;
}

#if defined(CINT_RT_LIBRARY) && !defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
#pragma GCC visibility pop
#endif
