/* cint_qbe.c: the cpu-qbe backend's runtime entry points (SPEC-09 BACK-18, rt/OPEN.md RT-OQ-82),
 * included by cint_rt.c so they are part of the one runtime object.
 *
 * Emitted C calls the helpers of cint_rt.h sections 6 to 13, which are static inline; QBE SSA can
 * only call functions with a symbol. Each function here is one helper, out of line, with the
 * operands in the backend's uniform form: every integer of 64 bits or fewer is an int64_t holding
 * its value (sign-extended for a signed type, zero-extended for an unsigned one), Bool is 0 or 1,
 * and a success flag is an int32_t 0 or 1, never a C bool (whose upper return bits the C ABI
 * leaves unspecified). The body is the helper, so every record, site, count and fuel charge is the
 * one emitted C makes (BACK-03). No helper is reimplemented here. A runtime library build
 * (CINT_RT_LIBRARY) keeps them unexported, as cint_rt.h keeps its own declarations. */

#if defined(CINT_RT_LIBRARY) && !defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
#pragma GCC visibility push(hidden)
#endif

#define CINT_QBE_S(x) ((int64_t)(x))
#define CINT_QBE_U(x) ((int64_t)(uint64_t)(x))

/* Checked add, sub, mul, div and rem; neg; the three shifts; the result-returning forms, whose
 * ArithError tag is the return value; the saturating forms. */
#define CINT_QBE_ARITH(T, CT, K)                                                                 \
    int32_t cint_qbe_add_##T(cint_ctx *ctx, cint_site site, int64_t a, int64_t b, int64_t *out); \
    int32_t cint_qbe_add_##T(cint_ctx *ctx, cint_site site, int64_t a, int64_t b, int64_t *out)  \
    {                                                                                            \
        CT r = 0;                                                                                \
        bool ok = cint_add_##T(ctx, site, (CT)a, (CT)b, &r);                                     \
        if (ok) *out = CINT_QBE_##K(r);                                                          \
        return ok ? 1 : 0;                                                                       \
    }                                                                                            \
    int32_t cint_qbe_sub_##T(cint_ctx *ctx, cint_site site, int64_t a, int64_t b, int64_t *out); \
    int32_t cint_qbe_sub_##T(cint_ctx *ctx, cint_site site, int64_t a, int64_t b, int64_t *out)  \
    {                                                                                            \
        CT r = 0;                                                                                \
        bool ok = cint_sub_##T(ctx, site, (CT)a, (CT)b, &r);                                     \
        if (ok) *out = CINT_QBE_##K(r);                                                          \
        return ok ? 1 : 0;                                                                       \
    }                                                                                            \
    int32_t cint_qbe_mul_##T(cint_ctx *ctx, cint_site site, int64_t a, int64_t b, int64_t *out); \
    int32_t cint_qbe_mul_##T(cint_ctx *ctx, cint_site site, int64_t a, int64_t b, int64_t *out)  \
    {                                                                                            \
        CT r = 0;                                                                                \
        bool ok = cint_mul_##T(ctx, site, (CT)a, (CT)b, &r);                                     \
        if (ok) *out = CINT_QBE_##K(r);                                                          \
        return ok ? 1 : 0;                                                                       \
    }                                                                                            \
    int32_t cint_qbe_div_##T(cint_ctx *ctx, cint_site site, int64_t a, int64_t b, int64_t *out); \
    int32_t cint_qbe_div_##T(cint_ctx *ctx, cint_site site, int64_t a, int64_t b, int64_t *out)  \
    {                                                                                            \
        CT r = 0;                                                                                \
        bool ok = cint_div_##T(ctx, site, (CT)a, (CT)b, &r);                                     \
        if (ok) *out = CINT_QBE_##K(r);                                                          \
        return ok ? 1 : 0;                                                                       \
    }                                                                                            \
    int32_t cint_qbe_rem_##T(cint_ctx *ctx, cint_site site, int64_t a, int64_t b, int64_t *out); \
    int32_t cint_qbe_rem_##T(cint_ctx *ctx, cint_site site, int64_t a, int64_t b, int64_t *out)  \
    {                                                                                            \
        CT r = 0;                                                                                \
        bool ok = cint_rem_##T(ctx, site, (CT)a, (CT)b, &r);                                     \
        if (ok) *out = CINT_QBE_##K(r);                                                          \
        return ok ? 1 : 0;                                                                       \
    }                                                                                            \
    int32_t cint_qbe_neg_##T(cint_ctx *ctx, cint_site site, int64_t a, int64_t *out);            \
    int32_t cint_qbe_neg_##T(cint_ctx *ctx, cint_site site, int64_t a, int64_t *out)             \
    {                                                                                            \
        CT r = 0;                                                                                \
        bool ok = cint_neg_##T(ctx, site, (CT)a, &r);                                            \
        if (ok) *out = CINT_QBE_##K(r);                                                          \
        return ok ? 1 : 0;                                                                       \
    }                                                                                            \
    int32_t cint_qbe_shift_##T(cint_ctx *ctx, cint_site site, int32_t form, int64_t a,           \
                               int64_t kbits, int32_t ktag, int64_t *out);                       \
    int32_t cint_qbe_shift_##T(cint_ctx *ctx, cint_site site, int32_t form, int64_t a,           \
                               int64_t kbits, int32_t ktag, int64_t *out)                        \
    {                                                                                            \
        cint_count k = {(uint64_t)kbits, (uint32_t)ktag, 0u, NULL};                              \
        CT r = 0;                                                                                \
        bool ok = form == 0   ? cint_shl_##T(ctx, site, (CT)a, k, &r)                            \
                  : form == 1 ? cint_shl_wrap_##T(ctx, site, (CT)a, k, &r)                       \
                              : cint_shr_##T(ctx, site, (CT)a, k, &r);                           \
        if (ok) *out = CINT_QBE_##K(r);                                                          \
        return ok ? 1 : 0;                                                                       \
    }                                                                                            \
    int32_t cint_qbe_result_##T(int32_t op, int64_t a, int64_t b, int32_t ktag, int64_t *out);  \
    int32_t cint_qbe_result_##T(int32_t op, int64_t a, int64_t b, int32_t ktag, int64_t *out)   \
    {                                                                                            \
        cint_count k = {(uint64_t)b, (uint32_t)ktag, 0u, NULL};                                  \
        CT r = 0;                                                                                \
        uint16_t e = op == 1   ? cint_add_result_##T((CT)a, (CT)b, &r)                           \
                     : op == 2 ? cint_sub_result_##T((CT)a, (CT)b, &r)                           \
                     : op == 3 ? cint_mul_result_##T((CT)a, (CT)b, &r)                           \
                     : op == 4 ? cint_div_result_##T((CT)a, (CT)b, &r)                           \
                     : op == 5 ? cint_rem_result_##T((CT)a, (CT)b, &r)                           \
                               : cint_shl_result_##T((CT)a, k, &r);                              \
        if (e == 0) *out = CINT_QBE_##K(r);                                                      \
        return (int32_t)e;                                                                       \
    }                                                                                            \
    int64_t cint_qbe_sat_##T(int32_t op, int64_t a, int64_t b);                                  \
    int64_t cint_qbe_sat_##T(int32_t op, int64_t a, int64_t b)                                   \
    {                                                                                            \
        return CINT_QBE_##K(op == 1   ? cint_add_sat_##T((CT)a, (CT)b)                           \
                            : op == 2 ? cint_sub_sat_##T((CT)a, (CT)b)                           \
                                      : cint_mul_sat_##T((CT)a, (CT)b));                         \
    }                                                                                            \
    int32_t cint_qbe_builtin_##T(cint_ctx *ctx, cint_site site, int32_t op, int32_t tag,         \
                                 int32_t mode, int32_t aux, int64_t a, int64_t b, int64_t c,     \
                                 int64_t *out);                                                  \
    int32_t cint_qbe_builtin_##T(cint_ctx *ctx, cint_site site, int32_t op, int32_t tag,         \
                                 int32_t mode, int32_t aux, int64_t a, int64_t b, int64_t c,     \
                                 int64_t *out)                                                   \
    {                                                                                            \
        CT r = 0;                                                                                \
        bool ok = cint_builtin_##T(ctx, site, (uint32_t)op, (uint32_t)tag, (uint32_t)mode,       \
                                   (uint32_t)aux, (uint64_t)a, (uint64_t)b, (uint64_t)c, &r);    \
        if (ok) *out = CINT_QBE_##K(r);                                                          \
        return ok ? 1 : 0;                                                                       \
    }                                                                                            \
    int64_t cint_qbe_minmax_##T(int32_t max, int64_t a, int64_t b);                              \
    int64_t cint_qbe_minmax_##T(int32_t max, int64_t a, int64_t b)                               \
    {                                                                                            \
        return CINT_QBE_##K(max != 0 ? cint_max_##T((CT)a, (CT)b) : cint_min_##T((CT)a, (CT)b)); \
    }
CINT_QBE_ARITH(i8, int8_t, S)
CINT_QBE_ARITH(i16, int16_t, S)
CINT_QBE_ARITH(i32, int32_t, S)
CINT_QBE_ARITH(i64, int64_t, S)
CINT_QBE_ARITH(u8, uint8_t, U)
CINT_QBE_ARITH(u16, uint16_t, U)
CINT_QBE_ARITH(u32, uint32_t, U)
CINT_QBE_ARITH(u64, uint64_t, U)

/* uabs.checked.<t>.<u>, which never faults. */
#define CINT_QBE_UABS(T, CT)                     \
    int64_t cint_qbe_uabs_##T(int64_t a);        \
    int64_t cint_qbe_uabs_##T(int64_t a)         \
    {                                            \
        return CINT_QBE_U(cint_uabs_##T((CT)a)); \
    }
CINT_QBE_UABS(i8, int8_t)
CINT_QBE_UABS(i16, int16_t)
CINT_QBE_UABS(i32, int32_t)
CINT_QBE_UABS(i64, int64_t)

/* as.checked and as| between narrow types, by the target's tag and the source's signedness:
 * the conversion helpers' own body (cint_rt_fits, cint_rt_fault_narrow, cint_rt_sat_bits), so one
 * function covers the 64 pairs of each form. as.wrap is the backend's own extension. */
int32_t cint_qbe_as(cint_ctx *ctx, cint_site site, int32_t from_tag, int32_t to_tag, int64_t x, int64_t *out);
int32_t cint_qbe_as(cint_ctx *ctx, cint_site site, int32_t from_tag, int32_t to_tag, int64_t x, int64_t *out)
{
    uint64_t bits = (uint64_t)x;
    bool fits = cint_rt_fits(bits, cint_rt_tag_signed((uint32_t)from_tag), (uint32_t)to_tag);
    unsigned w = cint_rt_tag_width((uint32_t)to_tag);
    if (cint_rt_faulted(ctx)) return 0;
    if (!fits) return (void)cint_rt_fault_narrow(ctx, site, (uint32_t)from_tag, (uint32_t)to_tag, bits), 0;
    *out = cint_rt_tag_signed((uint32_t)to_tag) ? cint_rt_sext(bits, w) : (int64_t)(bits & cint_rt_mask(w));
    return 1;
}
int64_t cint_qbe_as_sat(int32_t from_tag, int32_t to_tag, int64_t x);
int64_t cint_qbe_as_sat(int32_t from_tag, int32_t to_tag, int64_t x)
{
    uint64_t bits = cint_rt_sat_bits((uint64_t)x, cint_rt_tag_signed((uint32_t)from_tag), (uint32_t)to_tag);
    unsigned w = cint_rt_tag_width((uint32_t)to_tag);
    return cint_rt_tag_signed((uint32_t)to_tag) ? cint_rt_sext(bits, w) : (int64_t)(bits & cint_rt_mask(w));
}

/* The fuel charge and the call protocol (section 6, 6e). */
int32_t cint_qbe_fuel(cint_ctx *ctx, cint_site site, int64_t units);
int32_t cint_qbe_fuel(cint_ctx *ctx, cint_site site, int64_t units)
{
    return cint_fuel_charge(ctx, site, (uint64_t)units) ? 1 : 0;
}
int32_t cint_qbe_call_enter(cint_ctx *ctx, cint_site site);
int32_t cint_qbe_call_enter(cint_ctx *ctx, cint_site site)
{
    return cint_rt_call_enter(ctx, site) ? 1 : 0;
}
void *cint_qbe_state(cint_ctx *ctx, const cint_state *s);
void *cint_qbe_state(cint_ctx *ctx, const cint_state *s)
{
    return cint_rt_state(ctx, s);
}
/* A call of an extern: import k of the site's module's host table with the nargs arguments
 * generated code filled by cint_rt_arg_scalar and cint_rt_arg_view, the result's value to *out
 * and an error union's tag to *etag, 0 for the value alternative. */
int32_t cint_qbe_host_call(cint_ctx *ctx, cint_site site, int32_t k, cint_value *args, int32_t nargs, int32_t tag,
                           int32_t errors, int64_t *out, int64_t *etag);
int32_t cint_qbe_host_call(cint_ctx *ctx, cint_site site, int32_t k, cint_value *args, int32_t nargs, int32_t tag,
                           int32_t errors, int64_t *out, int64_t *etag)
{
    cint_value r;
    cint_rt_result_init(&r, (uint32_t)tag);
    if (!cint_rt_host_call(ctx, site, (uint32_t)k, args, (uint32_t)nargs, &r, (uint32_t)errors)) return 0;
    *out = r.as.i64;
    *etag = r.is_error != 0u ? r.error : 0;
    return 1;
}
void cint_qbe_call_leave(cint_ctx *ctx);
void cint_qbe_call_leave(cint_ctx *ctx)
{
    cint_rt_call_leave(ctx);
}

/* The out-of-line runtime calls whose C result is a bool, as an int32_t. */
int32_t cint_qbe_print_begin(cint_ctx *ctx, cint_site site);
int32_t cint_qbe_print_begin(cint_ctx *ctx, cint_site site)
{
    return cint_rt_print_begin(ctx, site) ? 1 : 0;
}
int32_t cint_qbe_print_bytes(cint_ctx *ctx, const void *bytes, int64_t len);
int32_t cint_qbe_print_bytes(cint_ctx *ctx, const void *bytes, int64_t len)
{
    return cint_rt_print_bytes(ctx, bytes, (size_t)len) ? 1 : 0;
}
int32_t cint_qbe_print_value(cint_ctx *ctx, int32_t tag, int64_t bits, int32_t form, int32_t fill, int64_t width,
                             int32_t zeros);
int32_t cint_qbe_print_value(cint_ctx *ctx, int32_t tag, int64_t bits, int32_t form, int32_t fill, int64_t width,
                             int32_t zeros)
{
    if (form == 0) return cint_rt_print_value(ctx, (uint32_t)tag, (uint64_t)bits) ? 1 : 0;
    return cint_rt_print_format(ctx, (uint32_t)tag, (uint64_t)bits, (uint32_t)form, (uint32_t)fill, (uint64_t)width,
                                (uint64_t)(uint32_t)zeros)
               ? 1
               : 0;
}
int32_t cint_qbe_print_end(cint_ctx *ctx);
int32_t cint_qbe_print_end(cint_ctx *ctx)
{
    return cint_rt_print_end(ctx) ? 1 : 0;
}
int32_t cint_qbe_assert(cint_ctx *ctx, cint_site site, int32_t tag, int64_t a, int64_t b);
int32_t cint_qbe_assert(cint_ctx *ctx, cint_site site, int32_t tag, int64_t a, int64_t b)
{
    return cint_rt_fault_assert(ctx, site, (uint32_t)tag, (uint64_t)a, (uint64_t)b) ? 1 : 0;
}

/* Arrays and views (section 14): the E_BOUNDS record of an index the emitted code found outside
 * its extent, the E_SHAPE entry check, a local array's zeros, and the frame arena (cint_mem.h). */
void cint_qbe_index(cint_ctx *ctx, cint_site site, const char *operation, int64_t index, int64_t extent);
void cint_qbe_index(cint_ctx *ctx, cint_site site, const char *operation, int64_t index, int64_t extent)
{
    (void)cint_index_check(ctx, site, operation, index, extent);
}
void cint_qbe_index_dim(cint_ctx *ctx, cint_site site, const char *operation, int64_t dim, int64_t index,
                        int64_t extent);
void cint_qbe_index_dim(cint_ctx *ctx, cint_site site, const char *operation, int64_t dim, int64_t index,
                        int64_t extent)
{
    (void)cint_index_check_dim(ctx, site, operation, dim, index, extent);
}
int32_t cint_qbe_copy_shape(cint_ctx *ctx, cint_site site, int64_t expected, int64_t actual);
int32_t cint_qbe_copy_shape(cint_ctx *ctx, cint_site site, int64_t expected, int64_t actual)
{
    return cint_copy_shape_check(ctx, site, 0, expected, actual) ? 1 : 0;
}
/* Whole-array assignment (SO_ACOPY): the elements of `size` bytes in index order below both extents,
 * as emitted C's loop. */
void cint_qbe_copy_elems(char *d, int64_t dorg, int64_t dstr, const char *s, int64_t sorg, int64_t sstr, int64_t dn,
                         int64_t sn, int64_t size);
void cint_qbe_copy_elems(char *d, int64_t dorg, int64_t dstr, const char *s, int64_t sorg, int64_t sstr, int64_t dn,
                         int64_t sn, int64_t size)
{
    for (int64_t z = 0; z < dn && z < sn; z++)
        memmove(d + (dorg + z * dstr) * size, s + (sorg + z * sstr) * size, (size_t)size);
}
int32_t cint_qbe_shape_dim(cint_ctx *ctx, cint_site site, int32_t param, int32_t dim, int64_t expected,
                           int64_t actual);
int32_t cint_qbe_shape_dim(cint_ctx *ctx, cint_site site, int32_t param, int32_t dim, int64_t expected,
                           int64_t actual)
{
    return cint_shape_check_dim(ctx, site, (uint32_t)param, (uint32_t)dim, expected, actual) ? 1 : 0;
}
int32_t cint_qbe_slice(cint_ctx *ctx, cint_site site, const char *operation, int64_t dim, int64_t extent,
                       int64_t stride, int64_t lo, int64_t hi, int64_t step, int32_t flags, int64_t *origin,
                       int64_t *length, int64_t *stride_out);
int32_t cint_qbe_slice(cint_ctx *ctx, cint_site site, const char *operation, int64_t dim, int64_t extent,
                       int64_t stride, int64_t lo, int64_t hi, int64_t step, int32_t flags, int64_t *origin,
                       int64_t *length, int64_t *stride_out)
{
    return cint_rt_slice(ctx, site, operation, dim, extent, stride, lo, hi, step, (uint32_t)flags, origin, length,
                         stride_out)
               ? 1
               : 0;
}
int32_t cint_qbe_copy_view(cint_ctx *ctx, cint_site site, const cint_vdesc *dst, const cint_vdesc *src,
                           int64_t size);
int32_t cint_qbe_copy_view(cint_ctx *ctx, cint_site site, const cint_vdesc *dst, const cint_vdesc *src,
                           int64_t size)
{
    return cint_rt_copy_view(ctx, site, dst, src, (size_t)size) ? 1 : 0;
}
int32_t cint_qbe_call_alias(cint_ctx *ctx, cint_site site, int32_t param, int32_t other, const cint_vdesc *p,
                            const cint_vdesc *q);
int32_t cint_qbe_call_alias(cint_ctx *ctx, cint_site site, int32_t param, int32_t other, const cint_vdesc *p,
                            const cint_vdesc *q)
{
    return cint_rt_call_alias(ctx, site, (uint32_t)param, (uint32_t)other, p, q) ? 1 : 0;
}
/* cint_reduce_<r> and equal: the result in the uniform form of its tag. */
int32_t cint_qbe_reduce(cint_ctx *ctx, cint_site site, int32_t op, int32_t elem, int32_t result, const cint_vdesc *xs,
                        const cint_vdesc *ys, int32_t has_init, int64_t init, int64_t *out);
int32_t cint_qbe_reduce(cint_ctx *ctx, cint_site site, int32_t op, int32_t elem, int32_t result, const cint_vdesc *xs,
                        const cint_vdesc *ys, int32_t has_init, int64_t init, int64_t *out)
{
    uint64_t bits = 0;
    unsigned w = cint_rt_tag_width((uint32_t)result);
    if (!cint_rt_reduce(ctx, site, (uint32_t)op, (uint32_t)elem, (uint32_t)result, xs, ys, has_init != 0,
                        (uint64_t)init, &bits))
        return 0;
    *out = cint_rt_tag_signed((uint32_t)result) ? cint_rt_sext(bits, w) : (int64_t)(bits & cint_rt_mask(w));
    return 1;
}
int32_t cint_qbe_equal(cint_ctx *ctx, cint_site site, int32_t elem, const cint_vdesc *xs, const cint_vdesc *ys,
                       int64_t *out);
int32_t cint_qbe_equal(cint_ctx *ctx, cint_site site, int32_t elem, const cint_vdesc *xs, const cint_vdesc *ys,
                       int64_t *out)
{
    bool r = false;
    if (!cint_rt_equal(ctx, site, (uint32_t)elem, xs, ys, &r)) return 0;
    *out = r ? 1 : 0;
    return 1;
}
int32_t cint_qbe_shape(cint_ctx *ctx, cint_site site, int32_t param, int64_t expected, int64_t actual);
int32_t cint_qbe_shape(cint_ctx *ctx, cint_site site, int32_t param, int64_t expected, int64_t actual)
{
    return cint_shape_check(ctx, site, (uint32_t)param, expected, actual) ? 1 : 0;
}
void cint_qbe_zero(void *p, int64_t n);
void cint_qbe_zero(void *p, int64_t n)
{
    memset(p, 0, (size_t)n);
}
int32_t cint_qbe_frame_charge(cint_ctx *ctx, cint_site site, int64_t n);
int32_t cint_qbe_frame_charge(cint_ctx *ctx, cint_site site, int64_t n)
{
    return cint_mem_frame_charge(ctx, site, n) ? 1 : 0;
}
int64_t cint_qbe_frame_mark(cint_ctx *ctx);
int64_t cint_qbe_frame_mark(cint_ctx *ctx)
{
    return cint_mem_frame_mark(ctx);
}
void cint_qbe_frame_release(cint_ctx *ctx, int64_t mark);
void cint_qbe_frame_release(cint_ctx *ctx, int64_t mark)
{
    cint_mem_frame_release(ctx, mark);
}
/* A local array of run-time extents (SO_VALLOC; back_c writes the same steps inline): decl.shape at
 * the first negative extent of v, then row-major strides and the saturated element count, then
 * its storage from the scratch arena. */
int32_t cint_qbe_scratch(cint_ctx *ctx, cint_site site, cint_vdesc *v, int64_t rank, int64_t size);
int32_t cint_qbe_scratch(cint_ctx *ctx, cint_site site, cint_vdesc *v, int64_t rank, int64_t size)
{
    int64_t e = 1;
    for (int64_t g = 0; g < rank; g++) {
        if (v->shape[g] < 0 && !cint_rt_fault_decl_shape(ctx, site, g, v->shape[g])) {
            return 0;
        }
    }
    v->rank = rank;
    for (int64_t g = rank - 1; g >= 0; g--) {
        v->stride[g] = e;
        if (cint_rt_mul_ovf_i64_p(e, v->shape[g], &e)) {
            e = INT64_MAX;
        }
    }
    return cint_rt_scratch(ctx, site, e, (size_t)size, &v->base) ? 1 : 0;
}

#if defined(CINT_RT_LIBRARY) && !defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
#pragma GCC visibility pop
#endif
