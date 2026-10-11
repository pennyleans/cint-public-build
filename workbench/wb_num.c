/* wb_num.c: exact integers of display (box 14 rulings BX14-02 B; default BX14-11).
 *
 * The workbench prints every value exactly. The engine hands it values; this file converts
 * them to decimal, including values wider than 64 bits (a record's `Z` and the sums that
 * /stats shows), with no floating point and no 128-bit type. It is display, never program
 * arithmetic: nothing computed here is narrowed or written to program state (SPEC-09 ARCH-01).
 */
#include <string.h>

#include "wb.h"

static void normalize(wb_num *x)
{
    while (x->n > 0u && x->limb[x->n - 1u] == 0u) x->n--;
    if (x->n == 0u) x->neg = 0;
}

void wb_num_zero(wb_num *x)
{
    memset(x, 0, sizeof *x);
}

void wb_num_from_u64(wb_num *x, uint64_t v)
{
    wb_num_zero(x);
    x->limb[0] = (uint32_t)(v & 0xffffffffu);
    x->limb[1] = (uint32_t)(v >> 32);
    x->n = 2u;
    normalize(x);
}

void wb_num_from_i64(wb_num *x, int64_t v)
{
    /* The magnitude of INT64_MIN is 2^63, which uint64_t holds. */
    wb_num_from_u64(x, v < 0 ? (uint64_t)0 - (uint64_t)v : (uint64_t)v);
    x->neg = v < 0;
}

int wb_num_from_le(wb_num *x, const uint8_t *p, size_t n, int is_signed)
{
    int neg = is_signed && n > 0u && (p[n - 1u] & 0x80u) != 0u;
    unsigned carry = neg ? 1u : 0u;
    wb_num_zero(x);
    if (n > (size_t)WB_NUM_LIMBS * 4u) return -1;
    /* A negative value's magnitude is its two's complement: every bit inverted, plus one. */
    for (size_t i = 0; i < n; i++) {
        unsigned byte = p[i];
        if (neg) {
            byte = (~byte & 0xffu) + carry;
            carry = byte >> 8;
            byte &= 0xffu;
        }
        x->limb[i / 4u] |= (uint32_t)byte << (8u * (unsigned)(i % 4u));
    }
    x->n = (uint32_t)((n + 3u) / 4u);
    x->neg = neg;
    normalize(x);
    return 0;
}

static int mag_cmp(const wb_num *x, const wb_num *y)
{
    if (x->n != y->n) return x->n < y->n ? -1 : 1;
    for (uint32_t i = x->n; i > 0u; i--) {
        if (x->limb[i - 1u] != y->limb[i - 1u]) return x->limb[i - 1u] < y->limb[i - 1u] ? -1 : 1;
    }
    return 0;
}

/* |x| += |y|. */
static int mag_add(wb_num *x, const wb_num *y)
{
    uint32_t n = x->n > y->n ? x->n : y->n;
    uint64_t carry = 0u;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t s = (uint64_t)x->limb[i] + (i < y->n ? y->limb[i] : 0u) + carry;
        x->limb[i] = (uint32_t)(s & 0xffffffffu);
        carry = s >> 32;
    }
    if (carry != 0u) {
        if (n == WB_NUM_LIMBS) return -1;
        x->limb[n++] = (uint32_t)carry;
    }
    x->n = n;
    return 0;
}

/* |x| -= |y|, where |x| >= |y|. */
static void mag_sub(wb_num *x, const wb_num *y)
{
    uint32_t borrow = 0u;
    for (uint32_t i = 0; i < x->n; i++) {
        uint64_t take = (uint64_t)(i < y->n ? y->limb[i] : 0u) + borrow;
        borrow = (uint64_t)x->limb[i] < take ? 1u : 0u;
        x->limb[i] = (uint32_t)(((uint64_t)x->limb[i] + ((uint64_t)borrow << 32) - take) & 0xffffffffu);
    }
    normalize(x);
}

int wb_num_add(wb_num *x, const wb_num *y)
{
    if (y->n == 0u) return 0;
    if (x->n == 0u || x->neg == y->neg) {
        int neg = x->n == 0u ? y->neg : x->neg;
        if (mag_add(x, y) != 0) return -1;
        x->neg = neg;
        return 0;
    }
    if (mag_cmp(x, y) >= 0) {
        mag_sub(x, y);   /* the sign of x stays, or the sum is zero */
    } else {
        wb_num t = *y;
        mag_sub(&t, x);
        *x = t;          /* the sign of y */
    }
    normalize(x);
    return 0;
}

int wb_num_mul_small(wb_num *x, uint32_t m)
{
    uint64_t carry = 0u;
    if (m == 0u) {
        wb_num_zero(x);
        return 0;
    }
    for (uint32_t i = 0; i < x->n; i++) {
        uint64_t p = (uint64_t)x->limb[i] * m + carry;
        x->limb[i] = (uint32_t)(p & 0xffffffffu);
        carry = p >> 32;
    }
    if (carry != 0u) {
        if (x->n == WB_NUM_LIMBS) return -1;
        x->limb[x->n++] = (uint32_t)carry;
    }
    return 0;
}

int wb_num_cmp(const wb_num *x, const wb_num *y)
{
    if (x->neg != y->neg) return x->neg ? -1 : 1;
    return x->neg ? mag_cmp(y, x) : mag_cmp(x, y);
}

uint32_t wb_num_bits(const wb_num *x)
{
    uint32_t top, bits;
    if (x->n == 0u) return 0u;
    top = x->limb[x->n - 1u];
    for (bits = 0u; top != 0u; bits++) top >>= 1;
    return (x->n - 1u) * 32u + bits;
}

/* The decimal digits of |x|, most significant first, NUL-terminated; their count. A value of
 * WB_NUM_LIMBS limbs has at most 1,311 digits. */
#define WB_DIGITS_MAX 1320u

static size_t mag_digits(const wb_num *x, char out[WB_DIGITS_MAX])
{
    wb_num t = *x;
    char rev[WB_DIGITS_MAX];
    size_t nd = 0u, k = 0u;
    while (t.n > 0u) {
        uint64_t rem = 0u;
        /* Divide by 10^9; each step's dividend is below 10^9 * 2^32, so it fits in 64 bits. */
        for (uint32_t i = t.n; i > 0u; i--) {
            uint64_t cur = (rem << 32) | t.limb[i - 1u];
            t.limb[i - 1u] = (uint32_t)(cur / 1000000000u);
            rem = cur % 1000000000u;
        }
        normalize(&t);
        for (unsigned j = 0; j < 9u && (t.n > 0u || rem > 0u); j++) {
            rev[nd++] = (char)('0' + (int)(rem % 10u));
            rem /= 10u;
        }
    }
    if (nd == 0u) rev[nd++] = '0';
    while (nd > 0u) out[k++] = rev[--nd];
    out[k] = '\0';
    return k;
}

/* The digits with `_` between groups of three, counted from the right. */
static void put_grouped(wb_buf *b, const char *d, size_t n, int group)
{
    if (!group) {
        wb_put(b, d, n);
        return;
    }
    for (size_t i = 0; i < n; i++) {
        if (i > 0u && (n - i) % 3u == 0u) wb_s(b, "_");
        wb_put(b, &d[i], 1u);
    }
}

void wb_num_dec(wb_buf *b, const wb_num *x, int group)
{
    char d[WB_DIGITS_MAX];
    size_t n = mag_digits(x, d);
    if (x->neg) wb_s(b, "-");
    put_grouped(b, d, n, group);
}

int wb_num_dec_over(wb_buf *b, const wb_num *x, uint64_t d, int group)
{
    unsigned twos = 0u, fives = 0u, k;
    wb_num y = *x;
    char digits[WB_DIGITS_MAX], padded[WB_DIGITS_MAX + 64u];
    size_t n, whole, end;
    if (d == 0u) return -1;
    for (; d % 2u == 0u; d /= 2u) twos++;
    for (; d % 5u == 0u; d /= 5u) fives++;
    if (d != 1u) return -1;
    /* x / (2^a 5^b) = x * 2^(k-a) * 5^(k-b) / 10^k with k = max(a, b). */
    k = twos > fives ? twos : fives;
    for (unsigned i = twos; i < k; i++) {
        if (wb_num_mul_small(&y, 2u) != 0) return -1;
    }
    for (unsigned i = fives; i < k; i++) {
        if (wb_num_mul_small(&y, 5u) != 0) return -1;
    }
    n = mag_digits(&y, digits);
    if (n <= k) {   /* leading zeros, so at least one digit stands before the point */
        size_t z = k + 1u - n;
        memset(padded, '0', z);
        memcpy(padded + z, digits, n + 1u);
        n += z;
    } else {
        memcpy(padded, digits, n + 1u);
    }
    whole = n - k;
    end = n;
    while (end > whole + 1u && padded[end - 1u] == '0') {
        end--;   /* trailing zeros go; one fractional digit stays */
    }
    if (y.neg) wb_s(b, "-");
    put_grouped(b, padded, whole, group);
    if (k > 0u) {
        wb_s(b, ".");
        wb_put(b, padded + whole, end - whole);
    }
    return 0;
}
