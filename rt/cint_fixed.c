/* cint_fixed.c: the fault records of fixed-point operations (rt/cint_fixed.h;
 * roadmap box 15; SPEC-01 IM-54 to IM-69, IM-106, IM-146), and the text of a
 * fixed-point print hole (SPEC-04 LS-207, LS-208), laid out by rt/cint_rt.c.
 *
 * rt/cint_rt.c includes this file after rt/cint_wide.c, whose exact integers
 * and record writers it uses; it counts in the size row of rt/cint_fixed.h.
 * The exact result is recomputed here from the operands alone, on 256 bits in
 * two's complement, apart from the inline helpers that found the fault: no
 * exact result exceeds 190 magnitude bits (a qmul of two 64-bit storages
 * scaled up by 2^63).
 */

#include "cint_fixed.h"

#define FQ_LIMBS 4u

/* Q<i>.<f> with storage I8 to I64 and i >= 1, or one of I8 to U64. */
static bool fq_type_ok(uint32_t t)
{
    uint32_t s = t & 0xffu;
    if (!cint_q_is(t)) return is_scalar_tag(t);
    return s >= CINT_TAG_I8 && s <= CINT_TAG_I64 && cint_q_frac(t) < cint_rt_tag_width(s);
}

/* A type's spelling in an operation identifier (SPEC-01 IM-130): q16_16, i32. */
static const char *fq_ident(uint32_t t, char *buf)
{
    unsigned v[2], n = 0u, k;
    if (!cint_q_is(t)) return cint_rt_type_ident(t);
    v[1] = cint_q_frac(t);
    v[0] = cint_rt_tag_width(t) - v[1];
    buf[n++] = 'q';
    for (k = 0u; k < 2u; k++) {
        char d[2];
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

/* The tagged value of type t with the 64-bit pattern bits: a fixed-point value
 * is tag 31, its storage tag, f as U16, then its raw storage integer (IM-146). */
static void fq_tvalue(cint_tvalue *v, uint32_t t, uint64_t bits)
{
    unsigned n = cint_rt_tag_width(t) / 8u, i;
    if (!cint_q_is(t)) {
        cint_rt_tvalue_scalar(v, t, bits);
        return;
    }
    memset(v, 0, sizeof *v);
    v->bytes[0] = CINT_TAG_Q;
    v->bytes[1] = (uint8_t)(t & 0xffu);
    v->bytes[2] = (uint8_t)cint_q_frac(t);
    v->bytes[3] = (uint8_t)(cint_q_frac(t) >> 8);
    for (i = 0u; i < n; i++) v->bytes[4u + i] = (uint8_t)(bits >> (8u * i));
    v->len = (uint16_t)(4u + n);
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

bool cint_rt_fixed_fault(cint_ctx *ctx, cint_site site, uint32_t op, uint32_t mode, uint32_t ta, uint32_t tb,
                         uint32_t tr, uint64_t a, uint64_t b)
{
    static const char *const names[11] = {"",    "add",  "sub",     "mul",  "neg", "div",
                                          "abs", "sqrt", "mul_int", "qmul", "as"};
    unsigned fa = cint_q_frac(ta), fb = cint_q_frac(tb), fr = cint_q_frac(tr);
    bool two = op == CINT_Q_ADD || op == CINT_Q_SUB || op == CINT_Q_MUL || op == CINT_Q_DIV ||
               op == CINT_Q_MUL_INT || op == CINT_Q_QMUL;
    bool same = op != CINT_Q_MUL_INT && op != CINT_Q_QMUL && op != CINT_Q_AS;
    bool rounds = op == CINT_Q_MUL || op == CINT_Q_DIV || op == CINT_Q_SQRT ||
                  (op == CINT_Q_QMUL && fa + fb > fr) || (op == CINT_Q_AS && cint_q_is(ta) && (!cint_q_is(tr) || fr < fa));
    uint64_t x[FQ_LIMBS], y[FQ_LIMBS], t[FQ_LIMBS], e[FQ_LIMBS] = {0u, 0u, 0u, 0u}, zero[FQ_LIMBS] = {0u, 0u, 0u, 0u};
    uint16_t code = (uint16_t)(op == CINT_Q_AS ? CINT_E_NARROW : CINT_E_OVERFLOW);
    char ia[12], ib[12], ir[12];
    wide_z exact;
    cint_fault_record *rec;
    if (cint_rt_faulted(ctx)) return false;
    if (op < CINT_Q_ADD || op > CINT_Q_AS || !fq_type_ok(ta) || !fq_type_ok(tr) || (two ? !fq_type_ok(tb) : tb != 0u) ||
        (rounds ? mode < 1u || mode > 9u : mode != 0u) ||
        (same && (!cint_q_is(ta) || tr != ta || (two && tb != ta))) ||
        (op == CINT_Q_MUL_INT && (!cint_q_is(ta) || tr != ta || cint_q_is(tb))) ||
        (op == CINT_Q_QMUL && (!cint_q_is(ta) || !cint_q_is(tb) || !cint_q_is(tr))) ||
        (op == CINT_Q_AS && !cint_q_is(ta) && !cint_q_is(tr))) {
        return fault_refused(ctx, site);
    }
    cint_w_from_narrow(x, FQ_LIMBS, ta, a);
    cint_w_from_narrow(y, FQ_LIMBS, two ? tb : CINT_TAG_I64, b);
    switch (op) {
    case CINT_Q_ADD: (void)cint_w_addc(e, x, y, FQ_LIMBS); break;
    case CINT_Q_SUB: (void)cint_w_subb(e, x, y, FQ_LIMBS); break;
    case CINT_Q_NEG: (void)cint_w_subb(e, zero, x, FQ_LIMBS); break;
    case CINT_Q_ABS: (void)cint_w_magnitude(e, x, FQ_LIMBS, true); break;
    case CINT_Q_MUL:
        cint_w_mul_low(t, x, y, FQ_LIMBS);
        fq_shr(e, t, fa, mode);
        break;
    case CINT_Q_DIV:
        if (cint_w_is_zero(y, FQ_LIMBS)) {
            code = (uint16_t)CINT_E_DIV_ZERO;
            break;
        }
        cint_w_shl_raw(t, x, fa, FQ_LIMBS);
        (void)cint_w_div_round(e, t, y, FQ_LIMBS, mode, true);
        break;
    case CINT_Q_SQRT:
        if (cint_w_negative(x, FQ_LIMBS)) {
            code = (uint16_t)CINT_E_DOMAIN;
            break;
        }
        cint_w_shl_raw(t, x, fa, FQ_LIMBS);
        fq_sqrt(e, t, mode);
        break;
    case CINT_Q_MUL_INT: cint_w_mul_low(e, x, y, FQ_LIMBS); break;
    case CINT_Q_QMUL:
        cint_w_mul_low(t, x, y, FQ_LIMBS);
        if (fa + fb > fr) {
            fq_shr(e, t, fa + fb - fr, mode);
        } else {
            cint_w_shl_raw(e, t, fr - fa - fb, FQ_LIMBS);
        }
        break;
    default:
        if (fr >= fa) {
            cint_w_shl_raw(e, x, fr - fa, FQ_LIMBS);
        } else {
            fq_shr(e, x, fa - fr, mode);
        }
        break;
    }
    wz_of(&exact, CINT_TAG_I256, e);  /* 0 for E_DIV_ZERO and E_DOMAIN, which record none */
    if (code == (uint16_t)CINT_E_OVERFLOW || code == (uint16_t)CINT_E_NARROW) {
        wide_z lo, hi;
        wz_bound(&lo, tr, false);
        wz_bound(&hi, tr, true);
        if (wz_cmp(&exact, &lo) >= 0 && wz_cmp(&exact, &hi) <= 0) return fault_refused(ctx, site);
    }
    rec = cint_rt_record_begin(ctx, site, code);
    if (rec == NULL) return false;
    cint_rt_record_op(rec, names[op]);
    cint_rt_record_op(rec, "checked");
    cint_rt_record_op(rec, fq_ident(ta, ia));
    if (op == CINT_Q_MUL_INT || op == CINT_Q_QMUL) cint_rt_record_op(rec, fq_ident(tb, ib));
    if (op == CINT_Q_QMUL || op == CINT_Q_AS) cint_rt_record_op(rec, fq_ident(tr, ir));
    if (rounds) cint_rt_record_op(rec, round_modes[mode]);
    fq_tvalue(&rec->operands[rec->operand_count++], ta, a);
    if (two) fq_tvalue(&rec->operands[rec->operand_count++], tb, b);
    if (code == (uint16_t)CINT_E_OVERFLOW || code == (uint16_t)CINT_E_NARROW) {
        wide_z bound;
        wz_bound(&bound, tr, !exact.negative);  /* exact lies beyond the bound on its own side */
        rec->has_exact = (uint8_t)wz_tvalue_z(&rec->exact, &exact);
        rec->has_limit = 1u;
        fq_tvalue(&rec->limit, tr, bound.negative ? (uint64_t)0 - bound.m[0] : bound.m[0]);
    }
    return false;
}

/* The text of a fixed-point hole without its fill (SPEC-04 LS-207, LS-208), as cint_ref's
 * fmt._render_fixed: the sign in head, the whole part, the point and the fractional digits in text, and
 * in *zeros the zeros a precision adds past the exact expansion. raw / 2^f has at most f <= 63 fractional
 * digits, the last of them 5, so text takes at most 19 + 1 + 63 bytes. Without a precision it is the
 * shortest exact decimal, with at least one fractional digit; with one, the value is rounded half to
 * even to that many digits, and a value that rounds to zero has no minus sign. */
static void fq_print_text(uint32_t qt, uint64_t raw, uint32_t form, uint64_t precision, char *head,
                          size_t *nhead, char *text, size_t *ntext, uint64_t *zeros)
{
    unsigned w = cint_rt_tag_width(qt), f = cint_q_frac(qt), nd = 0u, keep, i;
    int64_t v = cint_rt_sext(raw, w);
    bool negative = v < 0, given = (form & CINT_FORMAT_PRECISION) != 0u;
    uint64_t mag = negative ? (uint64_t)0 - (uint64_t)v : (uint64_t)v;
    uint64_t whole = mag >> f, rest = mag & cint_rt_mask(f);
    char digit[63], rev[20];
    size_t nw = 0u;
    while (rest != 0u) {  /* the digit is the part of 10 * rest above 2^f */
        uint64_t hi, lo = cint_w_mul64(rest, 10u, &hi);
        digit[nd++] = (char)('0' + ((hi << (64u - f)) | (lo >> f)));
        rest = lo & cint_rt_mask(f);
    }
    keep = nd;
    *zeros = given && precision > (uint64_t)nd ? precision - (uint64_t)nd : 0u;
    if (given && precision < (uint64_t)nd) {
        /* Past digit keep - 1 lie digit[keep] and, unless it is the last, a nonzero tail. */
        bool odd;
        keep = (unsigned)precision;
        odd = keep > 0u ? ((unsigned)(digit[keep - 1u] - '0') & 1u) != 0u : (whole & 1u) != 0u;
        if (digit[keep] > '5' || (digit[keep] == '5' && (keep + 1u < nd || odd))) {
            i = keep;
            while (i > 0u && digit[i - 1u] == '9') digit[--i] = '0';
            if (i > 0u) {
                digit[i - 1u]++;
            } else {
                whole++;
            }
        }
        i = 0u;
        while (whole == 0u && i < keep && digit[i] == '0') i++;
        negative = negative && (whole != 0u || i < keep);
    }
    if (negative) {
        head[(*nhead)++] = '-';
    } else if ((form & CINT_FORMAT_PLUS) != 0u) {
        head[(*nhead)++] = '+';
    } else if ((form & CINT_FORMAT_SPACE) != 0u) {
        head[(*nhead)++] = ' ';
    }
    do {
        rev[nw++] = (char)('0' + whole % 10u);
        whole /= 10u;
    } while (whole != 0u);
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
