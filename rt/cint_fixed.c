/* cint_fixed.c: the fault records of fixed-point operations (rt/cint_fixed.h;
 * roadmap box 15; SPEC-01 IM-54 to IM-69, IM-106, IM-146), the operations and
 * reductions with a side held in limbs (section 5 of the header; games wave
 * G2b, rt/OPEN.md RT-OQ-63), and the text of a fixed-point print hole (SPEC-04
 * LS-207, LS-208), laid out by rt/cint_rt.c.
 *
 * rt/cint_rt.c includes this file after rt/cint_wide.c, whose exact integers
 * and record writers it uses; it counts in the size row of rt/cint_fixed.h.
 * The exact result is recomputed here from the operands alone as such an
 * exact integer, apart from the inline helpers that found the fault: none
 * exceeds the 1,151 magnitude bits of I1024 converted to Q1.127.
 */

#include "cint_fixed.h"

#define FQ_LIMBS 4u

/* A type's spelling in an operation identifier (SPEC-01 IM-130): q16_16, i32. */
static const char *fq_ident(uint32_t t, char *buf)
{
    unsigned v[2], n = 0u, k;
    if (!cint_q_is(t)) return cint_rt_type_ident(t);
    v[1] = cint_q_frac(t);
    v[0] = cint_rt_tag_width(t) - v[1];
    buf[n++] = 'q';
    for (k = 0u; k < 2u; k++) {
        char d[3];
        unsigned m = 0u, x = v[k];
        do {
            d[m++] = (char)('0' + x % 10u);
            x /= 10u;
        } while (x != 0u);
        while (m > 0u) buf[n++] = d[--m];
        if (k == 0u) buf[n++] = '_';
    }
    buf[n] = '\0';
    return buf;
}

/* The tagged value of type t at x (IM-145): its limbs when it is held in limbs, else its 64-bit
 * pattern; a fixed-point value is tag 31, its storage tag, f as U16, then its raw storage (IM-146). */
static void fq_tvalue_at(cint_tvalue *v, uint32_t t, const uint64_t *x)
{
    unsigned n = cint_rt_tag_width(t) / 8u, i;
    wide_z z;
    if (!cint_q_is(t) && cint_wide_limbs(t) != 0u) {
        wz_of(&z, t, x);
        wz_tvalue(v, &z, t);
        return;
    }
    if (!cint_q_is(t)) {
        cint_rt_tvalue_scalar(v, t, x[0]);
        return;
    }
    memset(v, 0, sizeof *v);
    v->bytes[0] = CINT_TAG_Q;
    v->bytes[1] = (uint8_t)(t & 0xffu);
    v->bytes[2] = (uint8_t)cint_q_frac(t);
    v->bytes[3] = (uint8_t)(cint_q_frac(t) >> 8);
    for (i = 0u; i < n; i++) v->bytes[4u + i] = (uint8_t)(x[i / 8u] >> (8u * (i % 8u)));
    v->len = (uint16_t)(4u + n);
}

static void fq_tvalue(cint_tvalue *v, uint32_t t, uint64_t bits)
{
    fq_tvalue_at(v, t, &bits);
}

/* r = a / 2^k rounded once under mode, 0 <= k <= 127. */
static void fq_shr(uint64_t *r, const uint64_t *a, unsigned k, uint32_t mode)
{
    uint64_t d[FQ_LIMBS] = {0u, 0u, 0u, 0u};
    if (k == 0u) {
        cint_w_copy(r, a, FQ_LIMBS);
        return;
    }
    d[k / 64u] = (uint64_t)1 << (k % 64u);
    (void)cint_w_div_round(r, a, d, FQ_LIMBS, mode, true);
}

/* r = the root of x >= 0 rounded once under mode (cint_q_sqrt_raw's rule). */
static void fq_sqrt(uint64_t *r, const uint64_t *x, uint32_t mode)
{
    uint64_t sq[FQ_LIMBS], one[FQ_LIMBS] = {1u, 0u, 0u, 0u};
    cint_rt_wide_isqrt(r, x, FQ_LIMBS);
    cint_w_mul_low(sq, r, r, FQ_LIMBS);
    if (cint_w_ucmp(sq, x, FQ_LIMBS) != 0) {
        bool up = mode == 2u || mode == 4u;
        if (mode >= 5u) {
            (void)cint_w_addc(sq, sq, r, FQ_LIMBS);
            up = cint_w_ucmp(x, sq, FQ_LIMBS) > 0;
        }
        if (up) (void)cint_w_addc(r, r, one, FQ_LIMBS);
    }
}

/* Q<i>.<f> with storage I8 to I128 and i >= 1, or an integer type of any width. */
static bool fq_limbs_ok(uint32_t t)
{
    uint32_t s = t & 0xffu;
    if (!cint_q_is(t)) return is_scalar_tag(t) || cint_wide_limbs(t) != 0u;
    return s >= CINT_TAG_I8 && s <= CINT_TAG_I128 && cint_q_frac(t) < cint_rt_tag_width(s);
}

/* The storage tag of a type word: a fixed-point type's storage, else itself. */
static uint32_t fq_st(uint32_t t)
{
    return cint_q_is(t) ? t & 0xffu : t;
}

/* Q<i>.<f> with storage I8 to I64 and i >= 1, or one of I8 to U64. */
static bool fq_type_ok(uint32_t t)
{
    return fq_limbs_ok(t) && cint_wide_limbs(fq_st(t)) == 0u;
}

/* v[0 .. 16) = z modulo 2^1024 in two's complement. */
static void fz_limbs(uint64_t *v, const wide_z *z)
{
    cint_w_copy(v, z->m, 16u);
    if (z->negative) cint_w_negate(v, v, 16u);
}

/* z = z / 2^k rounded once under mode. */
static void fz_shr(wide_z *z, unsigned k, uint32_t mode)
{
    uint64_t q[WZ_LIMBS], rem[WZ_LIMBS], d[WZ_LIMBS];
    unsigned i;
    if (k == 0u) return;
    memset(d, 0, sizeof d);
    d[k / 64u] = (uint64_t)1 << (k % 64u);
    cint_w_shr_raw(q, z->m, k, WZ_LIMBS, false);
    for (i = 0u; i < WZ_LIMBS; i++) rem[i] = i < k / 64u ? z->m[i] : i == k / 64u ? z->m[i] & (d[i] - 1u) : 0u;
    if (!cint_w_is_zero(rem, k / 64u + 1u) && cint_w_round_up(q, rem, d, k / 64u + 1u, z->negative, mode)) {
        i = 0u;
        while (++q[i] == 0u) i++;
    }
    cint_w_copy(z->m, q, WZ_LIMBS);
    wz_norm(z);
}

/* Whether z lies in the range of type t; lo and hi receive its bounds. */
static bool fz_fits(const wide_z *z, uint32_t t, wide_z *lo, wide_z *hi)
{
    wz_bound(lo, fq_st(t), false);
    wz_bound(hi, fq_st(t), true);
    return wz_cmp(z, lo) >= 0 && wz_cmp(z, hi) <= 0;
}

/* exact, and limit the bound of t on exact's side (IM-106). */
static void fz_record_range(cint_fault_record *rec, const wide_z *exact, uint32_t t)
{
    uint64_t v[16];
    wide_z bound;
    wz_bound(&bound, fq_st(t), !exact->negative);
    fz_limbs(v, &bound);
    rec->has_exact = (uint8_t)wz_tvalue_z(&rec->exact, exact);
    rec->has_limit = 1u;
    fq_tvalue_at(&rec->limit, t, v);
}

/* cint_rt_q_limbs; with refit, a result in range is refused rather than written
 * (cint_rt_fixed_fault, whose inline helper found a fault). */
static bool fq_operate(cint_ctx *ctx, cint_site site, uint32_t op, uint32_t form, uint32_t mode, uint32_t ta, uint32_t tb,
                       uint32_t tr, const uint64_t *a, const uint64_t *b, void *out, bool refit)
{
    static const char *const names[11] = {"",    "add",  "sub",     "mul",  "neg", "div",
                                          "abs", "sqrt", "mul_int", "qmul", "as"};
    unsigned fa = cint_q_frac(ta), fb = cint_q_frac(tb), fr = cint_q_frac(tr);
    bool two = op == CINT_Q_ADD || op == CINT_Q_SUB || op == CINT_Q_MUL || op == CINT_Q_DIV ||
               op == CINT_Q_MUL_INT || op == CINT_Q_QMUL;
    bool same = op != CINT_Q_MUL_INT && op != CINT_Q_QMUL && op != CINT_Q_AS;
    bool rounds = op == CINT_Q_MUL || op == CINT_Q_DIV || op == CINT_Q_SQRT ||
                  (op == CINT_Q_QMUL && fa + fb > fr) || (op == CINT_Q_AS && cint_q_is(ta) && (!cint_q_is(tr) || fr < fa));
    uint64_t t[WZ_LIMBS], u[WZ_LIMBS], v[16];
    uint16_t code = (uint16_t)(op == CINT_Q_AS ? CINT_E_NARROW : CINT_E_OVERFLOW);
    char ia[12], ib[12], ir[12];
    wide_z x, y, e, lo, hi;
    cint_fault_record *rec;
    if (cint_rt_faulted(ctx)) return false;
    if (op < CINT_Q_ADD || op > CINT_Q_AS || form > CINT_Q_SAT || !fq_limbs_ok(ta) || !fq_limbs_ok(tr) ||
        (two ? !fq_limbs_ok(tb) || b == NULL : tb != 0u) || a == NULL || out == NULL ||
        (rounds ? mode < 1u || mode > 9u : mode != 0u) ||
        (same && (!cint_q_is(ta) || tr != ta || (two && tb != ta))) ||
        (op == CINT_Q_MUL_INT && (!cint_q_is(ta) || tr != ta || cint_q_is(tb))) ||
        (op == CINT_Q_QMUL && (!cint_q_is(ta) || !cint_q_is(tb) || !cint_q_is(tr))) ||
        (op == CINT_Q_AS && !cint_q_is(ta) && !cint_q_is(tr))) {
        return fault_refused(ctx, site);
    }
    wz_of(&x, fq_st(ta), a);
    memset(&y, 0, sizeof y);
    if (two) wz_of(&y, fq_st(tb), b);
    e = x;
    switch (op) {
    case CINT_Q_ADD: wz_add(&e, &x, &y); break;
    case CINT_Q_SUB:
        y.negative = !y.negative;
        wz_norm(&y);
        wz_add(&e, &x, &y);
        break;
    case CINT_Q_NEG:
        e.negative = !x.negative;
        wz_norm(&e);
        break;
    case CINT_Q_ABS: e.negative = false; break;
    case CINT_Q_MUL:
        wz_mul(&e, &x, &y);
        fz_shr(&e, fa, mode);
        break;
    case CINT_Q_DIV:
        if (cint_w_is_zero(y.m, WZ_LIMBS)) {
            code = (uint16_t)CINT_E_DIV_ZERO;
            break;
        }
        cint_w_shl_raw(t, x.m, fa, 4u);
        cint_rt_wide_udivmod(e.m, u, t, 4u, y.m, 4u);
        memset(e.m + 4, 0, sizeof e.m - 4u * sizeof e.m[0]);
        e.negative = x.negative != y.negative;
        if (!cint_w_is_zero(u, 4u) && cint_w_round_up(e.m, u, y.m, 4u, e.negative, mode)) {
            unsigned i = 0u;
            while (++e.m[i] == 0u) i++;
        }
        wz_norm(&e);
        break;
    case CINT_Q_SQRT:
        if (x.negative) {
            code = (uint16_t)CINT_E_DOMAIN;
            break;
        }
        cint_w_shl_raw(t, x.m, fa, 4u);
        memset(&e, 0, sizeof e);
        fq_sqrt(e.m, t, mode);
        break;
    case CINT_Q_MUL_INT: wz_mul(&e, &x, &y); break;
    case CINT_Q_QMUL:
        wz_mul(&e, &x, &y);
        if (fa + fb > fr) {
            fz_shr(&e, fa + fb - fr, mode);
        } else {
            cint_w_shl_raw(e.m, e.m, fr - fa - fb, WZ_LIMBS);
        }
        break;
    default:
        if (fr >= fa) {
            cint_w_shl_raw(e.m, e.m, fr - fa, WZ_LIMBS);
        } else {
            fz_shr(&e, fa - fr, mode);
        }
        break;
    }
    if (code != (uint16_t)CINT_E_DIV_ZERO && code != (uint16_t)CINT_E_DOMAIN &&
        (fz_fits(&e, tr, &lo, &hi) || form != CINT_Q_CHECKED)) {
        if (refit) return fault_refused(ctx, site);
        if (form == CINT_Q_SAT && wz_cmp(&e, &lo) < 0) e = lo;
        if (form == CINT_Q_SAT && wz_cmp(&e, &hi) > 0) e = hi;
        fz_limbs(v, &e);
        put_result(fq_st(tr), v, 16u, out);
        return true;
    }
    rec = cint_rt_record_begin(ctx, site, code);
    if (rec == NULL) return false;
    cint_rt_record_op(rec, names[op]);
    cint_rt_record_op(rec, "checked");
    cint_rt_record_op(rec, fq_ident(ta, ia));
    if (op == CINT_Q_MUL_INT || op == CINT_Q_QMUL) cint_rt_record_op(rec, fq_ident(tb, ib));
    if (op == CINT_Q_QMUL || op == CINT_Q_AS) cint_rt_record_op(rec, fq_ident(tr, ir));
    if (rounds) cint_rt_record_op(rec, round_modes[mode]);
    fq_tvalue_at(&rec->operands[rec->operand_count++], ta, a);
    if (two) fq_tvalue_at(&rec->operands[rec->operand_count++], tb, b);
    if (code == (uint16_t)CINT_E_OVERFLOW || code == (uint16_t)CINT_E_NARROW) fz_record_range(rec, &e, tr);
    return false;
}

bool cint_rt_fixed_fault(cint_ctx *ctx, cint_site site, uint32_t op, uint32_t mode, uint32_t ta, uint32_t tb,
                         uint32_t tr, uint64_t a, uint64_t b)
{
    uint64_t r;
    if (!fq_type_ok(ta) || !fq_type_ok(tr) || (tb != 0u && !fq_type_ok(tb))) return fault_refused(ctx, site);
    return fq_operate(ctx, site, op, CINT_Q_CHECKED, mode, ta, tb, tr, &a, tb != 0u ? &b : NULL, &r, true);
}

bool cint_rt_q_limbs(cint_ctx *ctx, cint_site site, uint32_t op, uint32_t form, uint32_t mode, uint32_t ta, uint32_t tb,
                     uint32_t tr, const void *a, const void *b, void *out)
{
    return fq_operate(ctx, site, op, form, mode, ta, tb, tr, (const uint64_t *)a, (const uint64_t *)b, out, false);
}

/* The text of a fixed-point hole without its fill (SPEC-04 LS-207, LS-208), as cint_ref's
 * fmt._render_fixed: the sign in head, the whole part, the point and the fractional digits in text, and
 * in *zeros the zeros a precision adds past the exact expansion. raw (one 64-bit pattern, or two limbs for
 * storage of 128 bits) / 2^f has at most f <= 127 fractional digits, the last of them 5, so text takes at
 * most 39 + 1 + 127 bytes. Without a precision it is the shortest exact decimal, with at least one
 * fractional digit; with one, the value is rounded half to even to that many digits, and a value that
 * rounds to zero has no minus sign. */
static void fq_print_text(uint32_t qt, const uint64_t *raw, uint32_t form, uint64_t precision, char *head,
                          size_t *nhead, char *text, size_t *ntext, uint64_t *zeros)
{
    unsigned w = cint_rt_tag_width(qt), f = cint_q_frac(qt), nd = 0u, keep, i;
    bool negative, given = (form & CINT_FORMAT_PRECISION) != 0u;
    uint64_t mag[3] = {0u, 0u, 0u}, whole[3], rest[3], cut[3], one[2] = {1u, 0u};
    char digit[127], rev[40];
    size_t nw = 0u;
    if (w <= 64u) {
        int64_t v = cint_rt_sext(raw[0], w);
        negative = v < 0;
        mag[0] = negative ? (uint64_t)0 - (uint64_t)v : (uint64_t)v;
    } else {
        negative = cint_w_magnitude(mag, raw, 2u, true);
    }
    cint_w_shr_raw(whole, mag, f, 3u, false);
    cint_w_shl_raw(cut, whole, f, 3u);
    (void)cint_w_subb(rest, mag, cut, 3u);
    while (!cint_w_is_zero(rest, 3u)) {  /* the digit is the part of 10 * rest above 2^f */
        uint64_t carry = 0u, hi;
        for (i = 0u; i < 3u; i++) {
            rest[i] = cint_w_mul64(rest[i], 10u, &hi) + carry;
            carry = hi + (rest[i] < carry ? 1u : 0u);
        }
        cint_w_shr_raw(cut, rest, f, 3u, false);
        digit[nd++] = (char)('0' + cut[0]);
        cint_w_shl_raw(cut, cut, f, 3u);
        (void)cint_w_subb(rest, rest, cut, 3u);
    }
    keep = nd;
    *zeros = given && precision > (uint64_t)nd ? precision - (uint64_t)nd : 0u;
    if (given && precision < (uint64_t)nd) {
        /* Past digit keep - 1 lie digit[keep] and, unless it is the last, a nonzero tail. */
        bool odd;
        keep = (unsigned)precision;
        odd = keep > 0u ? ((unsigned)(digit[keep - 1u] - '0') & 1u) != 0u : (whole[0] & 1u) != 0u;
        if (digit[keep] > '5' || (digit[keep] == '5' && (keep + 1u < nd || odd))) {
            i = keep;
            while (i > 0u && digit[i - 1u] == '9') digit[--i] = '0';
            if (i > 0u) {
                digit[i - 1u]++;
            } else {
                (void)cint_w_addc(whole, whole, one, 2u);
            }
        }
        i = 0u;
        while (cint_w_is_zero(whole, 2u) && i < keep && digit[i] == '0') i++;
        negative = negative && (!cint_w_is_zero(whole, 2u) || i < keep);
    }
    if (negative) {
        head[(*nhead)++] = '-';
    } else if ((form & CINT_FORMAT_PLUS) != 0u) {
        head[(*nhead)++] = '+';
    } else if ((form & CINT_FORMAT_SPACE) != 0u) {
        head[(*nhead)++] = ' ';
    }
    do {
        uint64_t r = whole[1] % 10u;
        whole[1] /= 10u;
        whole[0] = cint_w_div21_any(r, whole[0], 10u, &r);
        rev[nw++] = (char)('0' + r);
    } while (!cint_w_is_zero(whole, 2u));
    while (nw > 0u) text[(*ntext)++] = rev[--nw];
    if (!given || precision > 0u) text[(*ntext)++] = '.';
    for (i = 0u; i < keep; i++) text[(*ntext)++] = digit[i];
    if (!given && keep == 0u) text[(*ntext)++] = '0';
}

bool cint_rt_print_format_fixed(cint_ctx *ctx, uint32_t qt, uint64_t raw, uint32_t form, uint32_t fill,
                                uint64_t width, uint64_t precision)
{
    if (!cint_q_is(qt) || !fq_type_ok(qt)) return fault_refused(ctx, ctx->print_site);
    return print_format_limbs(ctx, qt, &raw, form, fill, width, precision);
}

bool cint_rt_print_q_limbs(cint_ctx *ctx, uint32_t qt, const void *raw, uint32_t form, uint32_t fill, uint64_t width,
                           uint64_t precision)
{
    if (!cint_q_is(qt) || !fq_limbs_ok(qt) || raw == NULL) return fault_refused(ctx, ctx->print_site);
    return print_format_limbs(ctx, qt, (const uint64_t *)raw, form, fill, width, precision);
}

/* ------------------------------------------------------------------------- */
/* Reductions with a side held in limbs.                                      */

static cint_fault_record *record_reduce(cint_ctx *ctx, cint_site site, uint16_t code, const char *name,
                                        const char *form, uint32_t e, uint32_t r, uint32_t mode);

/* The total acc of sum or dot (n elements) written to out, or with check E_OVERFLOW when it leaves result. */
static bool fq_total(cint_ctx *ctx, cint_site site, bool check, bool dot, uint32_t elem, uint32_t result,
                     uint32_t mode, int64_t n, const wide_z *acc, void *out)
{
    uint64_t v[16];
    wide_z lo, hi;
    cint_fault_record *rec;
    if (check && !fz_fits(acc, result, &lo, &hi)) {
        rec = record_reduce(ctx, site, (uint16_t)CINT_E_OVERFLOW, dot ? "dot" : "sum", "checked", elem, result, mode);
        if (rec != NULL) {
            cint_rt_record_operand(rec, CINT_TAG_I64, (uint64_t)n);
            fz_record_range(rec, acc, result);
        }
        return false;
    }
    fz_limbs(v, acc);
    put_result(fq_st(result), v, 16u, out);
    return true;
}

bool cint_rt_reduce_q_limbs(cint_ctx *ctx, cint_site site, uint32_t op, uint32_t elem, uint32_t result, uint32_t mode,
                            const cint_vdesc *xs, const cint_vdesc *ys, const void *init, void *out)
{
    bool dot = op == CINT_REDUCE_DOT, minmax = op == CINT_REDUCE_MIN || op == CINT_REDUCE_MAX;
    bool any = minmax && init != NULL, rounds = dot && 2u * cint_q_frac(elem) > cint_q_frac(result);
    uint32_t es = fq_st(elem);
    unsigned m = elem_limbs(es), fe = cint_q_frac(elem), fr = cint_q_frac(result);
    size_t size = cint_rt_tag_width(es) / 8u;
    uint64_t x[16], v[16], n, j = 0u;
    wide_z acc, zx, t, lo, hi;
    cint_fault_record *rec;
    view_walk wx, wy;
    if (cint_rt_faulted(ctx)) return false;
    if (op < CINT_REDUCE_SUM || op > CINT_REDUCE_DOT || op == CINT_REDUCE_COUNT || op == CINT_REDUCE_FOLD_MUL ||
        xs == NULL || out == NULL || xs->rank < 1 || xs->rank > CINT_MAX_RANK || !cint_q_is(elem) ||
        !cint_q_is(result) || !fq_limbs_ok(elem) || !fq_limbs_ok(result) || (!dot && result != elem) ||
        (op == CINT_REDUCE_FOLD_ADD && init == NULL) || (rounds ? mode < 1u || mode > 9u : mode != 0u) ||
        (dot && (ys == NULL || xs->rank != 1 || ys->rank != 1))) {
        return fault_refused(ctx, site);
    }
    if (dot && xs->shape[0] != ys->shape[0]) {
        /* dot's shape check, before the fuel charge (ruling R6) */
        rec = record_reduce(ctx, site, (uint16_t)CINT_E_SHAPE, "dot", "checked", elem, result, mode);
        if (rec != NULL) {
            cint_rt_record_operand(rec, CINT_TAG_I64, 0u);
            cint_rt_record_operand(rec, CINT_TAG_I64, (uint64_t)ys->shape[0]);
            record_limit_i64(rec, xs->shape[0]);
        }
        return false;
    }
    n = view_count(xs);
    if (n > 0u && !cint_fuel_charge(ctx, site, n / REDUCE_FUEL_BLOCK + (n % REDUCE_FUEL_BLOCK != 0u ? 1u : 0u)))
        return false;
    memset(&acc, 0, sizeof acc);
    if (init != NULL) wz_of(&acc, es, (const uint64_t *)init);
    (void)fz_fits(&acc, elem, &lo, &hi);
    if (n > 0u) {
        walk_start(&wx, xs);
        walk_start(&wy, dot ? ys : xs);
        do {
            elem_load(x, walk_at(&wx, size), es, m);
            wz_of(&zx, es, x);
            switch (op) {
            case CINT_REDUCE_FOLD_ADD:
                wz_add(&t, &acc, &zx);
                if (!fz_fits(&t, elem, &lo, &hi)) {
                    rec = record_reduce(ctx, site, (uint16_t)CINT_E_OVERFLOW, "fold_checked", "add", elem, 0u, 0u);
                    if (rec != NULL) {
                        cint_rt_record_operand(rec, CINT_TAG_I64, j);
                        fz_limbs(v, &acc);
                        fq_tvalue_at(&rec->operands[rec->operand_count++], elem, v);
                        fq_tvalue_at(&rec->operands[rec->operand_count++], elem, x);
                        fz_record_range(rec, &t, elem);
                    }
                    return false;
                }
                acc = t;
                break;
            case CINT_REDUCE_MIN:
            case CINT_REDUCE_MAX: {
                int c = wz_cmp(&zx, &acc);
                if (!any || (op == CINT_REDUCE_MIN ? c < 0 : c > 0)) acc = zx;
                any = true;
                break;
            }
            case CINT_REDUCE_DOT:
                elem_load(v, walk_at(&wy, size), es, m);
                wz_of(&t, es, v);
                wz_mul(&t, &zx, &t);
                wz_add(&acc, &acc, &t);
                (void)walk_next(&wy);
                break;
            default:  /* sum, sum_wrap and sum_sat */
                wz_add(&acc, &acc, &zx);
                if (op == CINT_REDUCE_SUM_SAT && wz_cmp(&acc, &lo) < 0) acc = lo;
                if (op == CINT_REDUCE_SUM_SAT && wz_cmp(&acc, &hi) > 0) acc = hi;
                break;
            }
            j++;
        } while (walk_next(&wx));
    }
    if (minmax && !any) return cint_rt_fault_reduce_empty(ctx, site, op == CINT_REDUCE_MAX, elem);
    if (rounds) {
        /* the exact sum at scale 2^(2f), rounded once to R's 2^g (SPEC-01 IM-87, BX15-04) */
        fz_shr(&acc, 2u * fe - fr, mode);
    } else if (dot) {
        cint_w_shl_raw(acc.m, acc.m, fr - 2u * fe, WZ_LIMBS);
    }
    return fq_total(ctx, site, op == CINT_REDUCE_SUM || dot, dot, elem, result, mode, (int64_t)n, &acc, out);
}

bool cint_rt_wzacc_fit_q(cint_ctx *ctx, cint_site site, uint32_t qt, int64_t n, const cint_wzacc *a, void *out)
{
    wide_z e;
    if (cint_rt_faulted(ctx)) return false;
    if (!cint_q_is(qt) || !fq_limbs_ok(qt) || a == NULL || out == NULL) return fault_refused(ctx, site);
    memset(&e, 0, sizeof e);
    e.negative = cint_w_magnitude(e.m, a->limb, elem_limbs(fq_st(qt)) + 1u, true);
    return fq_total(ctx, site, true, false, qt, qt, 0u, n, &e, out);
}
