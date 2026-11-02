/* cint_rt_lean.h: the lean checked forms of the cint-rt-3 runtime (OQ-321;
 * compiler/OPEN.md CINTC-OQ-76; docs/design/notes/2026-11-01-checked-arith-fast-path.md).
 *
 * The full forms cint_add_<t>, cint_sub_<t>, cint_mul_<t> and cint_neg_<t> of
 * rt/cint_rt.h section 10 read the context's faulted flag after the overflow
 * test. A lean form does not: it makes one overflow test and takes one
 * unlikely branch to a cold writer out of line, which writes the record the
 * full form writes, by way of cint_rt_fault_arith.
 *
 * The one difference. On a context that already holds a record, a lean form
 * without overflow returns true where the full form returns false; one with
 * overflow returns false and leaves the first record as it was. So a lean form
 * is sound only where no record can already be held, and the paths where that
 * is shown are listed in CINTC-OQ-76 item 4.
 *
 * rt/cint_rt.h includes this header, inside its extern "C" block and its
 * library visibility, after the instances of CINT_RT_ARITH: these forms use
 * CINT_RT_OVF, CINT_RT_BITS_* and the operation codes. Including this header
 * on its own is the same as including rt/cint_rt.h.
 */
#ifndef CINT_RT_H
#include "cint_rt.h"  /* which includes this header */
#elif !defined(CINT_RT_LEAN_H)
#define CINT_RT_LEAN_H

#if defined(CINT_RT_BUILTINS_GNU)
#define CINT_RT_COLD __attribute__((cold, noinline))
#define CINT_RT_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define CINT_RT_UNLIKELY(x) (x)
#ifdef _MSC_VER
#define CINT_RT_COLD __declspec(noinline)
#else
#define CINT_RT_COLD
#endif
#endif

/* The cold writer (rt/cint_rt.c). x is r for add and sub, as the record names a
 * by wrap(T, r - b) or wrap(T, r + b) (rt/cint_rt.h section 10), and a otherwise. */
CINT_RT_COLD bool cint_rt_fault_arith_cold(cint_ctx *ctx, cint_site site, uint32_t op, uint32_t tag, uint64_t x, uint64_t b);

#define CINT_RT_ARITH_LEAN(T, CT, TAG, K)                                                     \
    static inline bool cint_add_lean_##T(cint_ctx *ctx, cint_site site, CT a, CT b, CT *out)  \
    {                                                                                         \
        CT r;                                                                                 \
        if (CINT_RT_UNLIKELY(CINT_RT_OVF(add, T)(a, b, &r)))                                  \
            return (void)cint_rt_fault_arith_cold(ctx, site, CINT_RT_OP_ADD, (TAG),           \
                                                  CINT_RT_BITS_##K(r), CINT_RT_BITS_##K(b)), false; \
        *out = r;                                                                             \
        return true;                                                                          \
    }                                                                                         \
    static inline bool cint_sub_lean_##T(cint_ctx *ctx, cint_site site, CT a, CT b, CT *out)  \
    {                                                                                         \
        CT r;                                                                                 \
        if (CINT_RT_UNLIKELY(CINT_RT_OVF(sub, T)(a, b, &r)))                                  \
            return (void)cint_rt_fault_arith_cold(ctx, site, CINT_RT_OP_SUB, (TAG),           \
                                                  CINT_RT_BITS_##K(r), CINT_RT_BITS_##K(b)), false; \
        *out = r;                                                                             \
        return true;                                                                          \
    }                                                                                         \
    static inline bool cint_mul_lean_##T(cint_ctx *ctx, cint_site site, CT a, CT b, CT *out)  \
    {                                                                                         \
        CT r;                                                                                 \
        if (CINT_RT_UNLIKELY(CINT_RT_OVF(mul, T)(a, b, &r)))                                  \
            return (void)cint_rt_fault_arith_cold(ctx, site, CINT_RT_OP_MUL, (TAG),           \
                                                  CINT_RT_BITS_##K(a), CINT_RT_BITS_##K(b)), false; \
        *out = r;                                                                             \
        return true;                                                                          \
    }                                                                                         \
    static inline bool cint_neg_lean_##T(cint_ctx *ctx, cint_site site, CT a, CT *out)        \
    {                                                                                         \
        CT r;                                                                                 \
        if (CINT_RT_UNLIKELY(CINT_RT_OVF(sub, T)((CT)0, a, &r)))                              \
            return (void)cint_rt_fault_arith_cold(ctx, site, CINT_RT_OP_NEG, (TAG),           \
                                                  CINT_RT_BITS_##K(a), 0u), false;            \
        *out = r;                                                                             \
        return true;                                                                          \
    }

CINT_RT_ARITH_LEAN(i8, int8_t, CINT_TAG_I8, S)
CINT_RT_ARITH_LEAN(i16, int16_t, CINT_TAG_I16, S)
CINT_RT_ARITH_LEAN(i32, int32_t, CINT_TAG_I32, S)
CINT_RT_ARITH_LEAN(i64, int64_t, CINT_TAG_I64, S)
CINT_RT_ARITH_LEAN(u8, uint8_t, CINT_TAG_U8, U)
CINT_RT_ARITH_LEAN(u16, uint16_t, CINT_TAG_U16, U)
CINT_RT_ARITH_LEAN(u32, uint32_t, CINT_TAG_U32, U)
CINT_RT_ARITH_LEAN(u64, uint64_t, CINT_TAG_U64, U)

#endif /* CINT_RT_LEAN_H */
