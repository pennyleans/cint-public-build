/* cint_wide.c: the out-of-line part of the wide integers I128 to I1024 and
 * U128 to U512 (rt/cint_wide.h; roadmap box 15, unit 3): limb division by Knuth's
 * algorithm D, the integer square root, muldiv, the fault records of wide
 * operations with their exact results (SPEC-01 4.1, 9.2, IM-106, IM-108), a failed
 * assert of a wide comparison, the reductions (RT-OQ-54), the rendering of wide
 * tagged values, and print, with or without a format specification (RT-OQ-55).
 *
 * rt/cint_rt.c includes this file, as it includes rt/cint_mem.c, so the
 * runtime library stays one translation unit; the file has its own size row.
 * It uses the record writers and the view walk declared above its include
 * point in cint_rt.c. No exact result of a wide operation but dot exceeds 2,047
 * magnitude bits (an I1024 product, a shift of an I1024, or a muldiv
 * quotient), and the exact dot of I1024 elements stays within 2,110 bits, so
 * a 34-limb magnitude holds every one, and its Z stays within the run-time
 * bound of IM-108 (264 bytes in version 3, BX15-07 C).
 */

#include "cint_wide.h"

/* ------------------------------------------------------------------------- */
/* Division and square root of magnitudes.                                    */

void cint_rt_wide_udivmod(uint64_t *q, uint64_t *r, const uint64_t *u, unsigned m, const uint64_t *v, unsigned n)
{
    unsigned nv = n, mu = cint_w_len(u, m), i;
    while (nv > 1u && v[nv - 1u] == 0u) nv--;
    if (mu < nv) {
        for (i = 0u; i < m; i++) q[i] = 0u;
        cint_w_copy(r, u, mu);
        for (i = mu; i < n; i++) r[i] = 0u;
        return;
    }
    cint_rt_wide_udivrem(q, r, u, mu, v, nv);
    for (i = mu - nv + 1u; i < m; i++) q[i] = 0u;
    for (i = nv; i < n; i++) r[i] = 0u;
}

void cint_rt_wide_udivrem(uint64_t *q, uint64_t *r, const uint64_t *u, unsigned m, const uint64_t *v, unsigned n)
{
    uint64_t un[33], vn[32];
    unsigned s = cint_rt_clz64(v[n - 1u]), i, j;
    /* Normalize so that the divisor's top limb has its top bit set. */
    if (s != 0u) {
        for (i = n - 1u; i > 0u; i--) vn[i] = (v[i] << s) | (v[i - 1u] >> (64u - s));
        vn[0] = v[0] << s;
        un[m] = u[m - 1u] >> (64u - s);
        for (i = m - 1u; i > 0u; i--) un[i] = (u[i] << s) | (u[i - 1u] >> (64u - s));
        un[0] = u[0] << s;
    } else {
        cint_w_copy(vn, v, n);
        cint_w_copy(un, u, m);
        un[m] = 0u;
    }
    if (n == 1u) {
        uint64_t rem = un[m];
        for (j = m; j > 0u; j--) q[j - 1u] = cint_w_div21(rem, un[j - 1u], vn[0], &rem);
        r[0] = rem >> s;
        return;
    }
    for (j = m - n + 1u; j > 0u;) {
        uint64_t qhat, rhat, carry = 0u, borrow = 0u, t, b1;
        bool rhat_over = false;
        j--;
        /* The digit estimated from the top two limbs, then corrected with the
         * third: at most 1 too large after the loop. */
        if (un[j + n] >= vn[n - 1u]) {
            qhat = UINT64_MAX;
            rhat = un[j + n - 1u] + vn[n - 1u];
            rhat_over = rhat < vn[n - 1u];
        } else {
            qhat = cint_w_div21(un[j + n], un[j + n - 1u], vn[n - 1u], &rhat);
        }
        while (!rhat_over) {
            uint64_t p_hi, p_lo = cint_w_mul64(qhat, vn[n - 2u], &p_hi);
            if (p_hi < rhat || (p_hi == rhat && p_lo <= un[j + n - 2u])) break;
            qhat--;
            rhat += vn[n - 1u];
            rhat_over = rhat < vn[n - 1u];
        }
        /* un[j .. j + n] -= qhat * vn. */
        for (i = 0u; i < n; i++) {
            uint64_t hi, lo = cint_w_mul64(qhat, vn[i], &hi);
            lo += carry;
            hi += lo < carry ? 1u : 0u;
            carry = hi;
            t = un[i + j] - lo;
            b1 = un[i + j] < lo ? 1u : 0u;
            un[i + j] = t - borrow;
            borrow = b1 | (t < borrow ? 1u : 0u);
        }
        t = un[j + n] - carry;
        b1 = un[j + n] < carry ? 1u : 0u;
        un[j + n] = t - borrow;
        borrow = b1 | (t < borrow ? 1u : 0u);
        if (borrow != 0u) {
            /* The estimate was 1 too large: add the divisor back once. */
            qhat--;
            carry = 0u;
            for (i = 0u; i < n; i++) {
                uint64_t sum = un[i + j] + vn[i];
                b1 = sum < vn[i] ? 1u : 0u;
                un[i + j] = sum + carry;
                carry = b1 | (un[i + j] < sum ? 1u : 0u);
            }
            un[j + n] += carry;
        }
        q[j] = qhat;
    }
    if (s != 0u) {
        for (i = 0u; i < n; i++) r[i] = (un[i] >> s) | (un[i + 1u] << (64u - s));
    } else {
        cint_w_copy(r, un, n);
    }
}

/* The kinds of cint_w_divide at 4 limbs from the quotient and remainder
 * magnitudes qm and rm of |a| / |b| (mb = |b|). */
static void divide_signs(uint64_t *q, uint64_t *r, uint64_t *qm, uint64_t *rm, const uint64_t *mb, unsigned kind,
                         bool an, bool bn)
{
    if (!cint_w_is_zero(rm, 4u) && ((kind == 0u && an != bn) || (kind == 2u && an))) {
        /* One step further from zero, the remainder taken from the divisor's side. */
        uint64_t one[4] = {1u, 0u, 0u, 0u};
        (void)cint_w_addc(qm, qm, one, 4u);
        (void)cint_w_subb(rm, mb, rm, 4u);
        cint_w_cneg(rm, rm, 4u, kind == 0u && bn);  /* floor: the divisor's sign */
    } else {
        cint_w_cneg(rm, rm, 4u, an && kind != 2u);  /* truncating, or floor with equal signs: the dividend's */
    }
    cint_w_cneg(q, qm, 4u, an != bn);
    cint_w_copy(r, rm, 4u);
}

bool cint_rt_wide_mul4(uint64_t *r, const uint64_t *a, const uint64_t *b, bool s)
{
    uint64_t ma[4], mb[4], p[8] = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
    bool an = s && (a[3] >> 63) != 0u, bn = s && (b[3] >> 63) != 0u, negative = an != bn;
    unsigned la, lb;
    cint_w4_cneg(ma, a, cint_w_mask(an));
    cint_w4_cneg(mb, b, cint_w_mask(bn));
    la = cint_w_len(ma, 4u);
    lb = cint_w_len(mb, 4u);
    if (la != 0u && lb != 0u) {
        if (la + lb > 5u) return true;
        cint_w_umul_len(p, ma, la, mb, lb);
        if (p[4] != 0u) return true;
    }
    /* p = |a| * |b| < 2^256: up to 2^255 - 1, or 2^255 when negative, if signed */
    if (s && (p[3] >> 63) != 0u && !(negative && p[3] == (uint64_t)1 << 63 && (p[0] | p[1] | p[2]) == 0u)) return true;
    cint_w4_cneg(r, p, cint_w_mask(negative));
    return false;
}

void cint_rt_wide_divide4(uint64_t *q, uint64_t *r, const uint64_t *a, const uint64_t *b, unsigned kind, bool s)
{
    uint64_t ma[4], mb[4], qm[4], rm[4];
    bool an = s && (a[3] >> 63) != 0u, bn = s && (b[3] >> 63) != 0u;
    cint_w4_cneg(ma, a, cint_w_mask(an));
    cint_w4_cneg(mb, b, cint_w_mask(bn));
    cint_rt_wide_udivmod(qm, rm, ma, 4u, mb, 4u);
    divide_signs(q, r, qm, rm, mb, kind, an, bn);
}

void cint_rt_wide_isqrt(uint64_t *root, const uint64_t *x, unsigned n)
{
    uint64_t r[17], next[17], quo[16], rem[16], t, seed;
    unsigned top = cint_w_len(x, n), bits, shift, half, nr, nq, nn, i;
    for (i = 0u; i < n; i++) root[i] = 0u;
    if (top <= 1u) {
        root[0] = cint_rt_isqrt64(top == 0u ? 0u : x[0]);
        return;
    }
    /* A start above the root from the top 63 or 64 bits: with shift even and
     * t = floor(x / 2^shift), x < (t + 1) * 2^shift <= (isqrt(t) + 1)^2 * 2^shift. */
    bits = 64u * top - cint_rt_clz64(x[top - 1u]);
    shift = (bits - 63u) & ~1u;
    t = x[shift / 64u] >> (shift % 64u);
    if (shift % 64u != 0u && shift / 64u + 1u < top) t |= x[shift / 64u + 1u] << (64u - shift % 64u);
    seed = cint_rt_isqrt64(t) + 1u;
    half = shift / 2u;
    nr = half / 64u + 1u;
    for (i = 0u; i + 1u < nr; i++) r[i] = 0u;
    r[nr - 1u] = seed << (half % 64u);
    if (half % 64u != 0u && (seed >> (64u - half % 64u)) != 0u) r[nr++] = seed >> (64u - half % 64u);
    /* Newton's iteration over significant limbs decreases strictly until it
     * reaches the root. */
    for (;;) {
        uint64_t carry = 0u;
        cint_rt_wide_udivrem(quo, rem, x, top, r, nr);
        nq = top - nr + 1u;
        nn = nq > nr ? nq : nr;
        for (i = 0u; i < nn; i++) {
            uint64_t ri = i < nr ? r[i] : 0u, s = ri + carry;
            carry = s < carry ? 1u : 0u;
            next[i] = s + (i < nq ? quo[i] : 0u);
            carry += next[i] < s ? 1u : 0u;
        }
        next[nn] = carry;
        for (i = 0u; i < nn; i++) next[i] = (next[i] >> 1) | (next[i + 1u] << 63);
        nn = cint_w_len(next, nn);
        if (nn > nr || (nn == nr && cint_w_ucmp(next, r, nr) >= 0)) break;
        cint_w_copy(r, next, nn);
        nr = nn;
    }
    cint_w_copy(root, r, nr);
}

/* ------------------------------------------------------------------------- */
/* Exact integers of up to 34 limbs, as sign and magnitude.                   */

#define WZ_LIMBS 34u

typedef struct wide_z {
    uint64_t m[WZ_LIMBS];
    bool negative;  /* never true for zero */
} wide_z;

static void wz_norm(wide_z *z)
{
    if (cint_w_is_zero(z->m, WZ_LIMBS)) z->negative = false;
}

/* The value of the n limbs of wide tag `tag`, or of the one 64-bit pattern
 * of a narrow tag. */
static void wz_of(wide_z *z, uint32_t tag, const uint64_t *limbs)
{
    unsigned n = cint_wide_limbs(tag), i;
    uint64_t v[16];
    memset(z, 0, sizeof *z);
    if (n == 0u) {
        uint64_t bits = cint_rt_tag_signed(tag) ? (uint64_t)cint_rt_sext(limbs[0], cint_rt_tag_width(tag))
                                                : limbs[0] & cint_rt_mask(cint_rt_tag_width(tag));
        z->negative = cint_rt_tag_signed(tag) && (bits >> 63) != 0u;
        z->m[0] = z->negative ? (uint64_t)0 - bits : bits;
        return;
    }
    cint_w_copy(v, limbs, n);
    z->negative = cint_w_magnitude(v, v, n, cint_rt_tag_signed(tag));
    for (i = 0u; i < n; i++) z->m[i] = v[i];
}

static int wz_cmp(const wide_z *a, const wide_z *b)
{
    if (a->negative != b->negative) return a->negative ? -1 : 1;
    return a->negative ? -cint_w_ucmp(a->m, b->m, WZ_LIMBS) : cint_w_ucmp(a->m, b->m, WZ_LIMBS);
}

static void wz_add(wide_z *r, const wide_z *a, const wide_z *b)
{
    wide_z t;
    memset(&t, 0, sizeof t);
    if (a->negative == b->negative) {
        (void)cint_w_addc(t.m, a->m, b->m, WZ_LIMBS);
        t.negative = a->negative;
    } else if (cint_w_ucmp(a->m, b->m, WZ_LIMBS) >= 0) {
        (void)cint_w_subb(t.m, a->m, b->m, WZ_LIMBS);
        t.negative = a->negative;
    } else {
        (void)cint_w_subb(t.m, b->m, a->m, WZ_LIMBS);
        t.negative = b->negative;
    }
    wz_norm(&t);
    *r = t;
}

/* The product of two values of at most 16 limbs each. */
static void wz_mul(wide_z *r, const wide_z *a, const wide_z *b)
{
    wide_z t;
    memset(&t, 0, sizeof t);
    cint_w_umul(t.m, a->m, b->m, 16u);
    t.negative = a->negative != b->negative;
    wz_norm(&t);
    *r = t;
}

/* The largest value of tag, or its most negative value. */
static void wz_bound(wide_z *z, uint32_t tag, bool largest)
{
    unsigned w = cint_rt_tag_width(tag);
    memset(z, 0, sizeof *z);
    if (!cint_rt_tag_signed(tag) && !largest) return;
    if (!cint_rt_tag_signed(tag)) {
        unsigned i;
        for (i = 0u; i < w / 64u; i++) z->m[i] = UINT64_MAX;
        if (w < 64u) z->m[0] = cint_rt_mask(w);
        return;
    }
    z->m[(w - 1u) / 64u] = (uint64_t)1 << ((w - 1u) % 64u);
    if (largest) {
        wide_z one;
        memset(&one, 0, sizeof one);
        one.m[0] = 1u;
        (void)cint_w_subb(z->m, z->m, one.m, WZ_LIMBS);
    } else {
        z->negative = true;
    }
}

/* The tagged value of z under tag, which holds it. */
static void wz_tvalue(cint_tvalue *v, const wide_z *z, uint32_t tag)
{
    unsigned n = cint_rt_tag_width(tag) / 8u, i;
    uint64_t limbs[16];
    memset(v, 0, sizeof *v);
    for (i = 0u; i < 16u; i++) limbs[i] = z->m[i];
    if (z->negative) cint_w_negate(limbs, limbs, 16u);
    v->bytes[0] = (uint8_t)tag;
    for (i = 0u; i < n; i++) v->bytes[1u + i] = (uint8_t)(limbs[i / 8u] >> (8u * (i % 8u)));
    v->len = (uint16_t)(1u + n);
}

/* A tagged Z: U32 byte count, then the minimal little-endian two's complement
 * bytes (SPEC-01 11.1). False when it would exceed CINT_Z_MAX_BYTES. */
static bool wz_tvalue_z(cint_tvalue *v, const wide_z *z)
{
    uint8_t b[8u * WZ_LIMBS + 1u];
    unsigned n = 8u * WZ_LIMBS + 1u, i;
    unsigned carry = 1u;
    for (i = 0u; i + 1u < n; i++) b[i] = (uint8_t)(z->m[i / 8u] >> (8u * (i % 8u)));
    b[n - 1u] = 0u;
    if (z->negative) {
        for (i = 0u; i < n; i++) {
            unsigned t = (unsigned)(uint8_t)~b[i] + carry;
            b[i] = (uint8_t)t;
            carry = t >> 8;
        }
    }
    while (n > 0u) {
        bool below_negative = n >= 2u && (b[n - 2u] & 0x80u) != 0u;
        if ((b[n - 1u] == 0x00u && !below_negative) || (b[n - 1u] == 0xFFu && n >= 2u && below_negative)) {
            n--;
        } else {
            break;
        }
    }
    if (n > CINT_Z_MAX_BYTES) return false;
    memset(v, 0, sizeof *v);
    v->bytes[0] = CINT_TAG_Z;
    v->bytes[1] = (uint8_t)(n & 0xFFu);
    v->bytes[2] = (uint8_t)(n >> 8);
    memcpy(v->bytes + 5, b, n);
    v->len = (uint16_t)(5u + n);
    return true;
}

/* The tagged value of a wide operand, or of a narrow conversion source. */
static void wide_operand(cint_fault_record *rec, uint32_t tag, const uint64_t *limbs)
{
    wide_z z;
    if (cint_wide_limbs(tag) == 0u) {
        cint_rt_record_operand(rec, tag, limbs[0]);
        return;
    }
    if (rec->operand_count >= CINT_FAULT_MAX_OPERANDS) return;
    wz_of(&z, tag, limbs);
    wz_tvalue(&rec->operands[rec->operand_count], &z, tag);
    rec->operand_count++;
}

/* ------------------------------------------------------------------------- */
/* Fault records.                                                             */

/* exact, and limit the bound of rtag (any integer type) on exact's side, which
 * exact lies beyond (IM-106). */
static void wz_record_range(cint_fault_record *rec, const wide_z *exact, uint32_t rtag)
{
    wide_z bound;
    wz_bound(&bound, rtag, !exact->negative);
    rec->has_exact = (uint8_t)wz_tvalue_z(&rec->exact, exact);
    rec->has_limit = 1u;
    if (cint_wide_limbs(rtag) != 0u) {
        wz_tvalue(&rec->limit, &bound, rtag);
    } else {
        uint64_t v[16];
        unsigned i;
        for (i = 0u; i < 16u; i++) v[i] = bound.m[i];
        if (bound.negative) cint_w_negate(v, v, 16u);
        cint_rt_tvalue_scalar(&rec->limit, rtag, v[0]);
    }
}

static bool any_int_tag(uint32_t tag)
{
    return is_scalar_tag(tag) || cint_wide_limbs(tag) != 0u;
}

static bool count_ok(cint_count k)
{
    return is_scalar_tag(k.tag) || (cint_wide_limbs(k.tag) != 0u && k.limbs != NULL);
}

static void count_operand(cint_fault_record *rec, cint_count k)
{
    if (cint_wide_limbs(k.tag) != 0u) {
        wide_operand(rec, k.tag, k.limbs);
    } else {
        cint_rt_record_operand(rec, k.tag, k.bits);
    }
}

/* Whether a shift count of type tag lies in 0 to w - 1: k is its 64-bit pattern,
 * or its limbs for a wide type, whose high limbs are then all zero. */
static bool count_fits(uint32_t tag, const uint64_t *k, unsigned w)
{
    unsigned n = cint_wide_limbs(tag), i;
    for (i = 1u; i < n; i++) {
        if (k[i] != 0u) return false;
    }
    return k[0] <= (uint64_t)(w - 1u);
}

bool cint_rt_wide_fault(cint_ctx *ctx, cint_site site, uint32_t op, uint32_t tag, uint32_t mode, uint32_t aux,
                        const uint64_t *a, const uint64_t *b, const uint64_t *c)
{
    static const char *const names[10] = {"", "add", "sub", "mul", "neg", "div", "rem", "shl", "shl", "shr"};
    static const char *const builtins[17] = {"",          "abs",       "",          "",           "",
                                             "clamp",     "div_trunc", "rem_trunc", "div_euclid", "rem_euclid",
                                             "div_round", "muldiv",    "",          "isqrt",      "isqrt_round",
                                             "rotl",      "rotr"};
    unsigned n = cint_wide_limbs(tag), w = 64u * n, values = 2u;
    uint32_t bi = op >= CINT_W_BUILTIN ? op - CINT_W_BUILTIN : 0u;
    uint32_t rtag = op == CINT_W_AS || bi == 11u ? aux : tag;
    bool rounds = bi == 10u || bi == 11u || bi == 14u, has_exact = false, is_signed = cint_rt_tag_signed(tag);
    /* A shift or rotation: b is the count's 64-bit pattern, or a wide count's limbs. */
    bool shift = (op >= CINT_RT_OP_SHL && op <= CINT_RT_OP_SHR) || bi == 15u || bi == 16u;
    const char *name = op >= CINT_RT_OP_ADD && op <= CINT_RT_OP_SHR ? names[op]
                       : op == CINT_W_AS                             ? "as"
                       : bi >= 1u && bi <= 16u                       ? builtins[bi]
                                                                     : "";
    uint16_t code = 0u;
    bool min_by_minus_one;
    wide_z za, zb, exact;
    cint_fault_record *rec;
    if (cint_rt_faulted(ctx)) return false;
    if ((n == 0u && !(op == CINT_W_AS && is_scalar_tag(tag))) || name[0] == '\0' || a == NULL || !any_int_tag(rtag) ||
        (rounds ? mode < 1u || mode > 9u : mode != 0u) || ((bi == 15u || bi == 16u) && is_signed) ||
        (shift ? !any_int_tag(aux) || b == NULL : op != CINT_W_AS && bi != 11u && aux != 0u)) {
        return fault_refused(ctx, site);
    }
    memset(&exact, 0, sizeof exact);
    memset(&zb, 0, sizeof zb);
    wz_of(&za, tag, a);
    if (op == CINT_RT_OP_NEG || op == CINT_W_AS || bi == 1u || bi == 13u || bi == 14u) values = 1u;
    if (bi == 5u || bi == 11u) values = 3u;
    if ((values > 1u && b == NULL) || (values > 2u && c == NULL)) return fault_refused(ctx, site);
    if (values > 1u && !shift) wz_of(&zb, tag, b);
    /* The signed pair (MIN, -1), whose quotient 2^(w - 1) is the only one out of range. */
    min_by_minus_one = is_signed && values > 1u && !shift && cint_w_is_min(a, n) && cint_w_is_minus_one(b, n);
    if (op == CINT_RT_OP_ADD || op == CINT_RT_OP_SUB) {
        if (op == CINT_RT_OP_SUB) zb.negative = !zb.negative && !cint_w_is_zero(zb.m, WZ_LIMBS);
        wz_add(&exact, &za, &zb);
        code = (uint16_t)CINT_E_OVERFLOW;
    } else if (op == CINT_RT_OP_MUL) {
        wz_mul(&exact, &za, &zb);
        code = (uint16_t)CINT_E_OVERFLOW;
    } else if (op == CINT_RT_OP_NEG || bi == 1u) {
        exact = za;
        exact.negative = op == CINT_RT_OP_NEG && !za.negative;
        wz_norm(&exact);
        code = (uint16_t)CINT_E_OVERFLOW;
    } else if (shift) {
        if (!count_fits(aux, b, w)) {
            code = (uint16_t)CINT_E_SHIFT;
        } else if (op == CINT_RT_OP_SHL) {
            exact = za;
            cint_w_shl_raw(exact.m, za.m, (unsigned)b[0], WZ_LIMBS);
            code = (uint16_t)CINT_E_OVERFLOW;
        }
    } else if (op == CINT_W_AS) {
        exact = za;
        code = (uint16_t)CINT_E_NARROW;
    } else if (op == CINT_RT_OP_DIV || op == CINT_RT_OP_REM || (bi >= 6u && bi <= 10u)) {
        bool quotient = op == CINT_RT_OP_DIV || bi == 6u || bi == 8u || bi == 10u;
        if (cint_w_is_zero(zb.m, WZ_LIMBS)) {
            code = (uint16_t)CINT_E_DIV_ZERO;
        } else if (quotient && min_by_minus_one) {
            exact.m[(w - 1u) / 64u] = (uint64_t)1 << ((w - 1u) % 64u);
            code = (uint16_t)CINT_E_OVERFLOW;
        }
    } else if (bi == 5u) {
        wide_z zc;
        wz_of(&zc, tag, c);
        if (wz_cmp(&zb, &zc) > 0) code = (uint16_t)CINT_E_DOMAIN;
    } else if (bi == 13u || bi == 14u) {
        if (za.negative) code = (uint16_t)CINT_E_DOMAIN;
    } else if (bi == 11u) {
        uint64_t ma[16], mb[16], mc[16], p[32], q[32], rem[16];
        bool negative = cint_w_magnitude(ma, a, n, is_signed) != cint_w_magnitude(mb, b, n, is_signed);
        negative = cint_w_magnitude(mc, c, n, is_signed) != negative;
        if (cint_w_is_zero(mc, n)) {
            code = (uint16_t)CINT_E_DIV_ZERO;
        } else {
            unsigned i;
            cint_w_umul(p, ma, mb, n);
            cint_rt_wide_udivmod(q, rem, p, 2u * n, mc, n);
            if (!cint_w_is_zero(rem, n)) {
                /* Rounded as cint_rt_wide_muldiv rounds, on 2n limbs. */
                uint64_t d2[32], r2[32];
                for (i = 0u; i < 2u * n; i++) {
                    d2[i] = i < n ? mc[i] : 0u;
                    r2[i] = i < n ? rem[i] : 0u;
                }
                if (cint_w_round_up(q, r2, d2, 2u * n, negative, mode)) {
                    i = 0u;
                    while (++q[i] == 0u && i + 1u < 2u * n) i++;
                }
            }
            for (i = 0u; i < 2u * n; i++) exact.m[i] = q[i];
            exact.negative = negative;
            wz_norm(&exact);
            code = (uint16_t)CINT_E_OVERFLOW;
        }
    }
    if (code == (uint16_t)CINT_E_OVERFLOW || code == (uint16_t)CINT_E_NARROW) {
        wide_z lo, hi;
        wz_bound(&lo, rtag, false);
        wz_bound(&hi, rtag, true);
        if (wz_cmp(&exact, &lo) >= 0 && wz_cmp(&exact, &hi) <= 0) code = 0u;  /* in range: not a fault */
        has_exact = true;
    }
    if (code == 0u) return fault_refused(ctx, site);
    rec = cint_rt_record_begin(ctx, site, code);
    if (rec == NULL) return false;
    cint_rt_record_op(rec, name);
    cint_rt_record_op(rec, op == CINT_RT_OP_SHL_WRAP ? "wrap" : "checked");
    cint_rt_record_op(rec, cint_rt_type_ident(tag));
    if (op == CINT_W_AS || bi == 11u) cint_rt_record_op(rec, cint_rt_type_ident(rtag));
    if (rounds) cint_rt_record_op(rec, round_modes[mode]);
    wide_operand(rec, tag, a);
    if (shift) {
        wide_operand(rec, aux, b);
    } else if (values > 1u) {
        wide_operand(rec, tag, b);
        if (values > 2u) wide_operand(rec, tag, c);
    }
    if (code == (uint16_t)CINT_E_SHIFT) {
        record_limit_i64(rec, (int64_t)(w - 1u));  /* REF-OQ-04, as for shifts */
    } else if (has_exact) {
        wz_record_range(rec, &exact, rtag);
    }
    return false;
}

bool cint_rt_fault_assert_wide(cint_ctx *ctx, cint_site site, uint32_t tag, const uint64_t *a, const uint64_t *b)
{
    cint_fault_record *rec;
    if (cint_wide_limbs(tag) == 0u || a == NULL || b == NULL) return fault_refused(ctx, site);
    rec = cint_rt_record_begin(ctx, site, (uint16_t)CINT_E_ASSERT);
    if (rec == NULL) return false;
    cint_rt_record_op(rec, "assert.checked.bool");
    wide_operand(rec, tag, a);
    wide_operand(rec, tag, b);
    return false;
}

/* ------------------------------------------------------------------------- */
/* muldiv.                                                                    */

/* Writes v, a value of len limbs in two's complement that fits rtag, to the C
 * object of rtag (I8 to U64, or a wide type) at out. */
static void put_result(uint32_t rtag, const uint64_t *v, unsigned len, void *out)
{
    unsigned nr = cint_wide_limbs(rtag);
    if (nr != 0u) {
        cint_w_resize((uint64_t *)out, nr, v, len, true);
        return;
    }
    switch (rtag) {
    case CINT_TAG_I8: *(int8_t *)out = (int8_t)cint_rt_sext(v[0], 8u); break;
    case CINT_TAG_I16: *(int16_t *)out = (int16_t)cint_rt_sext(v[0], 16u); break;
    case CINT_TAG_I32: *(int32_t *)out = (int32_t)cint_rt_sext(v[0], 32u); break;
    case CINT_TAG_I64: *(int64_t *)out = cint_rt_sext(v[0], 64u); break;
    case CINT_TAG_U8: *(uint8_t *)out = (uint8_t)v[0]; break;
    case CINT_TAG_U16: *(uint16_t *)out = (uint16_t)v[0]; break;
    case CINT_TAG_U32: *(uint32_t *)out = (uint32_t)v[0]; break;
    default: *(uint64_t *)out = v[0]; break;
    }
}

#if CINT_W_FAST
/* The magnitude of the n-limb value a (n = 2 or 4) in 2 limbs and its sign
 * mask: whether a lies in 128 bits. */
static bool mag2(uint64_t *m, uint64_t *sign, const uint64_t *a, unsigned n, bool s)
{
    if (n == 4u) return cint_w4_small(m, a, s, sign);
    *sign = s ? (uint64_t)0 - (a[1] >> 63) : 0u;
    cint_w2_cneg(m, a, *sign);
    return true;
}

/* muldiv at 2 and 4 limbs for operands in 128 bits, a nonzero divisor and a
 * result of at most 256 bits (rt/OPEN.md RT-OQ-62): the 4-limb product of the
 * magnitudes divided limb by limb by a 1-limb divisor, and by cint_w2_udivmod
 * or algorithm D by a 2-limb one.
 * Whether it wrote the result; otherwise the general path below decides. */
static bool muldiv_small(uint32_t tag, uint32_t rtag, uint32_t mode, const uint64_t *a, const uint64_t *b,
                         const uint64_t *c, void *out)
{
    unsigned n = cint_wide_limbs(tag), wr = cint_rt_tag_width(rtag), bits = 0u, i;
    uint64_t ma[2] = {0u, 0u}, mb[2] = {0u, 0u}, mc[2] = {0u, 0u}, p[4], q[4] = {0u, 0u, 0u, 0u}, rem[2] = {0u, 0u};
    uint64_t sa = 0u, sb = 0u, sc = 0u, neg;
    bool s = cint_rt_tag_signed(tag), fits;
    if ((n != 2u && n != 4u) || wr > 256u) return false;
    if (!(mag2(ma, &sa, a, n, s) && mag2(mb, &sb, b, n, s) && mag2(mc, &sc, c, n, s))) return false;
    if ((mc[0] | mc[1]) == 0u) return false;
    cint_w2_umul(p, ma, mb);
    if (mc[1] == 0u) {
        for (i = cint_w_len(p, 4u); i > 0u; i--) q[i - 1u] = cint_w_div21_any(rem[0], p[i - 1u], mc[0], &rem[0]);
    } else if ((p[2] | p[3]) == 0u) {
        cint_w2_udivmod(q, rem, p, mc);
    } else {
        cint_rt_wide_udivmod(q, rem, p, 4u, mc, 2u);
    }
    neg = sa ^ sb ^ sc;
    if ((rem[0] | rem[1]) != 0u && cint_w_round_up(q, rem, mc, 2u, neg != 0u, mode)) {
        uint64_t one[4] = {1u, 0u, 0u, 0u};
        (void)cint_w_addc(q, q, one, 4u);  /* q < 2^256 - 1: a remainder means a divisor of 2 or more */
    }
    for (i = 4u; i > 0u && bits == 0u; i--) {
        if (q[i - 1u] != 0u) bits = 64u * i - cint_rt_clz64(q[i - 1u]);
    }
    if (bits == 0u) neg = 0u;
    if (!cint_rt_tag_signed(rtag)) {
        fits = neg != 0u ? bits == 0u : bits <= wr;
    } else {  /* as below */
        fits = bits <= wr - 1u || (neg != 0u && bits == wr && q[(wr - 1u) / 64u] == (uint64_t)1 << ((wr - 1u) % 64u) &&
                                   cint_w_is_zero(q, (wr - 1u) / 64u));
    }
    if (!fits) return false;
    cint_w4_cneg(q, q, neg);
    put_result(rtag, q, 4u, out);
    return true;
}
#endif

bool cint_rt_wide_muldiv(cint_ctx *ctx, cint_site site, uint32_t tag, uint32_t rtag, uint32_t mode,
                         const uint64_t *a, const uint64_t *b, const uint64_t *c, void *out)
{
    unsigned n = cint_wide_limbs(tag), wr = cint_rt_tag_width(rtag), i, bits = 0u;
    uint64_t ma[16], mb[16], mc[16], p[32], q[32], rem[16], res[16];
    bool negative, fits, s = cint_rt_tag_signed(tag);
    if (cint_rt_faulted(ctx)) return false;
    if (n == 0u || !any_int_tag(rtag) || mode < 1u || mode > 9u || out == NULL) return fault_refused(ctx, site);
#if CINT_W_FAST
    if (muldiv_small(tag, rtag, mode, a, b, c, out)) return true;
#endif
    negative = cint_w_magnitude(ma, a, n, s) != cint_w_magnitude(mb, b, n, s);
    negative = cint_w_magnitude(mc, c, n, s) != negative;
    if (cint_w_is_zero(mc, n)) return cint_rt_wide_fault(ctx, site, CINT_W_BUILTIN + 11u, tag, mode, rtag, a, b, c);
    cint_w_umul(p, ma, mb, n);
    cint_rt_wide_udivmod(q, rem, p, 2u * n, mc, n);
    if (!cint_w_is_zero(rem, n)) {
        uint64_t d2[32], r2[32];
        for (i = 0u; i < 2u * n; i++) {
            d2[i] = i < n ? mc[i] : 0u;
            r2[i] = i < n ? rem[i] : 0u;
        }
        if (cint_w_round_up(q, r2, d2, 2u * n, negative, mode)) {
            i = 0u;
            while (++q[i] == 0u && i + 1u < 2u * n) i++;
        }
    }
    for (i = 2u * n; i > 0u; i--) {
        if (q[i - 1u] != 0u) {
            bits = 64u * i - cint_rt_clz64(q[i - 1u]);
            break;
        }
    }
    if (bits == 0u) negative = false;
    if (!cint_rt_tag_signed(rtag)) {
        fits = negative ? bits == 0u : bits <= wr;
    } else {
        /* Up to 2^(wr - 1) - 1, or 2^(wr - 1) itself when negative. */
        fits = bits <= wr - 1u ||
               (negative && bits == wr && q[(wr - 1u) / 64u] == (uint64_t)1 << ((wr - 1u) % 64u) &&
                cint_w_is_zero(q, (wr - 1u) / 64u));
    }
    if (!fits) return cint_rt_wide_fault(ctx, site, CINT_W_BUILTIN + 11u, tag, mode, rtag, a, b, c);
    for (i = 0u; i < 16u; i++) res[i] = i < 2u * n ? q[i] : 0u;
    if (negative) cint_w_negate(res, res, 16u);
    put_result(rtag, res, 16u, out);
    return true;
}

/* ------------------------------------------------------------------------- */
/* Reductions (RT-OQ-54).                                                     */

/* The limb count of an element: 1 for I8 to U64. */
static unsigned elem_limbs(uint32_t tag)
{
    unsigned n = cint_wide_limbs(tag);
    return n != 0u ? n : 1u;
}

/* x of type tag as elem_limbs(tag) limbs; a narrow pattern made canonical. */
static void elem_value(uint64_t *v, uint32_t tag, const uint64_t *x)
{
    if (cint_wide_limbs(tag) == 0u) {
        v[0] = canon(tag, x[0]);
    } else {
        cint_w_copy(v, x, cint_wide_limbs(tag));
    }
}

/* The element of type tag at p: a wide one is its struct of m limbs. */
static void elem_load(uint64_t *v, const unsigned char *p, uint32_t tag, unsigned m)
{
    if (cint_wide_limbs(tag) == 0u) {
        v[0] = load_elem(p, tag);
    } else {
        memcpy(v, p, 8u * m);
    }
}

/* acc[0 .. L) plus the magnitude mag[0 .. len), or minus it when subtract,
 * modulo 2^(64L); the limbs above mag's are touched only while a carry or
 * borrow runs. */
static void wzacc_step(uint64_t *acc, unsigned L, const uint64_t *mag, unsigned len, bool subtract)
{
    uint64_t carry = 0u;
    unsigned i;
    for (i = 0u; i < L && (i < len || carry != 0u); i++) {
        uint64_t b = i < len ? mag[i] : 0u;
        if (subtract) {
            uint64_t d = acc[i] - b, t = d - carry;
            carry = (acc[i] < b ? 1u : 0u) | (d < carry ? 1u : 0u);
            acc[i] = t;
        } else {
            uint64_t sum = acc[i] + b, t = sum + carry;
            carry = (sum < b ? 1u : 0u) | (t < sum ? 1u : 0u);
            acc[i] = t;
        }
    }
}

void cint_wzacc_add(cint_wzacc *a, uint32_t tag, const uint64_t *x)
{
    unsigned m = elem_limbs(tag), len;
    uint64_t v[16], mag[16];
    bool negative;
    elem_value(v, tag, x);
    len = cint_w_mag(mag, v, m, cint_rt_tag_signed(tag), &negative);
    wzacc_step(a->limb, m + 1u, mag, len, negative);
}

void cint_wzacc_add_product(cint_wzacc *a, uint32_t tag, const uint64_t *x, const uint64_t *y)
{
    unsigned m = elem_limbs(tag), lx, ly;
    uint64_t vx[16], vy[16], mx[16], my[16], p[32];
    bool nx, ny, s = cint_rt_tag_signed(tag);
    elem_value(vx, tag, x);
    elem_value(vy, tag, y);
    lx = cint_w_mag(mx, vx, m, s, &nx);
    ly = cint_w_mag(my, vy, m, s, &ny);
    if (lx == 0u || ly == 0u) return;
    cint_w_umul_len(p, mx, lx, my, ly);
    wzacc_step(a->limb, 2u * m + 1u, p, lx + ly, nx != ny);
}

bool cint_rt_wzacc_fit(cint_ctx *ctx, cint_site site, const char *name, uint32_t elem, uint32_t result, int64_t n,
                       const cint_wzacc *a, void *out)
{
    bool dot = name != NULL && strcmp(name, "dot") == 0;
    unsigned len = (dot ? 2u : 1u) * elem_limbs(elem) + 1u, nr = cint_wide_limbs(result);
    uint64_t v[33];
    wide_z exact;
    cint_fault_record *rec;
    if (cint_rt_faulted(ctx)) return false;
    if ((!dot && (name == NULL || strcmp(name, "sum") != 0)) || !any_int_tag(elem) || !any_int_tag(result) ||
        a == NULL || out == NULL) {
        return fault_refused(ctx, site);
    }
    if (nr != 0u ? cint_w_fits_into(a->limb, len, true, nr, cint_rt_tag_signed(result))
                 : cint_w_fits_narrow(a->limb, len, true, result)) {
        put_result(result, a->limb, len, out);
        return true;
    }
    rec = record_checked(ctx, site, (uint16_t)CINT_E_OVERFLOW, name, elem);
    if (rec != NULL) {
        cint_rt_record_op(rec, cint_rt_type_ident(result));
        cint_rt_record_operand(rec, CINT_TAG_I64, (uint64_t)n);
        memset(&exact, 0, sizeof exact);
        exact.negative = cint_w_magnitude(v, a->limb, len, true);
        cint_w_copy(exact.m, v, len);
        wz_record_range(rec, &exact, result);  /* a Z of version 3 only for dot over I1024 */
    }
    return false;
}

bool cint_rt_wide_fold_step(cint_ctx *ctx, cint_site site, uint32_t op, uint32_t tag, int64_t j, uint64_t *acc,
                            const uint64_t *x)
{
    unsigned n = cint_wide_limbs(tag);
    bool s = cint_rt_tag_signed(tag), over;
    uint64_t r[16];
    wide_z za, zx, exact;
    cint_fault_record *rec;
    if (cint_rt_faulted(ctx)) return false;
    if ((op != CINT_RT_OP_ADD && op != CINT_RT_OP_MUL) || n == 0u || acc == NULL || x == NULL)
        return fault_refused(ctx, site);
    over = op == CINT_RT_OP_ADD ? cint_w_add_ovf(r, acc, x, n, s) : cint_w_mul_ovf(r, acc, x, n, s);
    if (!over) {
        cint_w_copy(acc, r, n);
        return true;
    }
    wz_of(&za, tag, acc);
    wz_of(&zx, tag, x);
    if (op == CINT_RT_OP_ADD) {
        wz_add(&exact, &za, &zx);
    } else {
        wz_mul(&exact, &za, &zx);
    }
    rec = cint_rt_record_begin(ctx, site, (uint16_t)CINT_E_OVERFLOW);
    if (rec != NULL) {
        cint_rt_record_op(rec, "fold_checked");
        cint_rt_record_op(rec, op == CINT_RT_OP_ADD ? "add" : "mul");
        cint_rt_record_op(rec, cint_rt_type_ident(tag));
        cint_rt_record_operand(rec, CINT_TAG_I64, (uint64_t)j);
        wide_operand(rec, tag, acc);
        wide_operand(rec, tag, x);
        wz_record_range(rec, &exact, tag);
    }
    return false;
}

bool cint_rt_reduce_wide(cint_ctx *ctx, cint_site site, uint32_t op, uint32_t elem, uint32_t result,
                         const cint_vdesc *xs, const cint_vdesc *ys, const uint64_t *init, void *out)
{
    bool fold = op == CINT_REDUCE_FOLD_ADD || op == CINT_REDUCE_FOLD_MUL;
    bool minmax = op == CINT_REDUCE_MIN || op == CINT_REDUCE_MAX, any = minmax && init != NULL;
    bool s = cint_rt_tag_signed(elem);
    unsigned m = elem_limbs(elem);
    size_t size = cint_rt_tag_width(elem) / 8u;
    uint64_t x[16], y[16], t[16], acc[16], n, j = 0u;
    cint_wzacc z;
    cint_fault_record *rec;
    view_walk wx, wy;
    if (cint_rt_faulted(ctx)) return false;
    if (op < CINT_REDUCE_SUM || op > CINT_REDUCE_DOT || op == CINT_REDUCE_COUNT || xs == NULL || out == NULL ||
        xs->rank < 1 || xs->rank > CINT_MAX_RANK || !any_int_tag(elem) || !any_int_tag(result) ||
        (cint_wide_limbs(elem) == 0u && cint_wide_limbs(result) == 0u) ||
        (op != CINT_REDUCE_SUM && op != CINT_REDUCE_DOT && result != elem) || (fold && init == NULL) ||
        (op == CINT_REDUCE_DOT && (ys == NULL || xs->rank != 1 || ys->rank != 1))) {
        return fault_refused(ctx, site);
    }
    if (op == CINT_REDUCE_DOT && xs->shape[0] != ys->shape[0]) {
        /* dot's shape check, before the fuel charge (ruling R6) */
        rec = record_checked(ctx, site, (uint16_t)CINT_E_SHAPE, "dot", elem);
        if (rec != NULL) {
            cint_rt_record_op(rec, cint_rt_type_ident(result));
            cint_rt_record_operand(rec, CINT_TAG_I64, 0u);
            cint_rt_record_operand(rec, CINT_TAG_I64, (uint64_t)ys->shape[0]);
            record_limit_i64(rec, xs->shape[0]);
        }
        return false;
    }
    n = view_count(xs);
    if (n > 0u && !cint_fuel_charge(ctx, site, n / REDUCE_FUEL_BLOCK + (n % REDUCE_FUEL_BLOCK != 0u ? 1u : 0u)))
        return false;
    memset(&z, 0, sizeof z);
    memset(acc, 0, sizeof acc);
    if (fold || any) elem_value(acc, elem, init);
    if (n > 0u) {
        walk_start(&wx, xs);
        walk_start(&wy, op == CINT_REDUCE_DOT ? ys : xs);
        do {
            elem_load(x, walk_at(&wx, size), elem, m);
            switch (op) {
            case CINT_REDUCE_SUM:
                cint_wzacc_add(&z, elem, x);
                break;
            case CINT_REDUCE_SUM_WRAP:
                (void)cint_w_addc(acc, acc, x, m);
                break;
            case CINT_REDUCE_SUM_SAT:
                if (cint_w_add_ovf(t, acc, x, m, s)) {
                    cint_w_bound(acc, m, s && cint_w_negative(x, m), s);  /* past the bound on x's side */
                } else {
                    cint_w_copy(acc, t, m);
                }
                break;
            case CINT_REDUCE_FOLD_ADD:
            case CINT_REDUCE_FOLD_MUL:
                if (!cint_rt_wide_fold_step(ctx, site, op == CINT_REDUCE_FOLD_ADD ? CINT_RT_OP_ADD : CINT_RT_OP_MUL,
                                            elem, cint_rt_sext(j, 64u), acc, x)) {
                    return false;
                }
                break;
            case CINT_REDUCE_MIN:
            case CINT_REDUCE_MAX: {
                int c = cint_w_cmp(x, acc, m, s);
                if (!any || (op == CINT_REDUCE_MIN ? c < 0 : c > 0)) cint_w_copy(acc, x, m);
                any = true;
                break;
            }
            default:  /* dot */
                elem_load(y, walk_at(&wy, size), elem, m);
                cint_wzacc_add_product(&z, elem, x, y);
                (void)walk_next(&wy);
                break;
            }
            j++;
        } while (walk_next(&wx));
    }
    if (op == CINT_REDUCE_SUM || op == CINT_REDUCE_DOT) {
        return cint_rt_wzacc_fit(ctx, site, op == CINT_REDUCE_SUM ? "sum" : "dot", elem, result,
                                 cint_rt_sext(n, 64u), &z, out);
    }
    if (minmax && !any) return cint_rt_fault_reduce_empty(ctx, site, op == CINT_REDUCE_MAX, elem);
    cint_w_copy((uint64_t *)out, acc, m);
    return true;
}

/* ------------------------------------------------------------------------- */
/* Print (SPEC-04 LS-193; SPEC-01 IM-90).                                     */

static size_t decimal(const uint8_t *le, size_t n, bool is_signed, char *out);
static bool print_format_limbs(cint_ctx *ctx, uint32_t tag, const uint64_t *limb, uint32_t form, uint32_t fill,
                               uint64_t width, uint64_t scale);

bool cint_rt_print_wide(cint_ctx *ctx, uint32_t tag, const uint64_t *limb)
{
    unsigned n = cint_wide_limbs(tag);
    uint8_t le[128];
    char text[704];
    size_t len;
    if (cint_rt_faulted(ctx)) return false;
    if (n == 0u || limb == NULL) return fault_refused(ctx, ctx->print_site);
    cint_wide_store(le, limb, n);
    len = decimal(le, 8u * n, cint_rt_tag_signed(tag), text);
    return cint_rt_print_bytes(ctx, text, len);
}

bool cint_rt_print_format_wide(cint_ctx *ctx, uint32_t tag, const uint64_t *limb, uint32_t form, uint32_t fill,
                               uint64_t width, uint64_t scale)
{
    if (cint_wide_limbs(tag) == 0u || limb == NULL) return fault_refused(ctx, ctx->print_site);
    return print_format_limbs(ctx, tag, limb, form, fill, width, scale);
}
