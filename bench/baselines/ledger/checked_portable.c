/* The B-6 checked C baseline in portable C17 (posture 3.4, H-4): the same two
 * functions as checked_builtin.c, each overflow found by a comparison before
 * the operation instead of a compiler builtin, so MSVC builds it too. Each
 * returns 0 with *out set, or 1 on overflow with *out unchanged.
 */
#include <stdint.h>

/* a + b without overflow: 1 with *r set, or 0. */
static int add_i64(int64_t a, int64_t b, int64_t *r)
{
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) return 0;
    *r = a + b;
    return 1;
}

static int sub_i64(int64_t a, int64_t b, int64_t *r)
{
    if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b)) return 0;
    *r = a - b;
    return 1;
}

int portable_ledger(int64_t n, int64_t initial, int64_t *out)
{
    int64_t b = initial;
    for (int64_t i = 0; i < n;) {
        if (!add_i64(b, i % 97, &b)) return 1;
        if (!sub_i64(b, 48, &b)) return 1;
        if (!add_i64(i, 1, &i)) return 1;
    }
    *out = b;
    return 0;
}

int portable_transactions(int64_t n, int64_t initial, int64_t seed, int64_t *out)
{
    int64_t amounts[256], b = initial;
    for (int64_t j = 0; j < 256; ++j) {
        int64_t x = 0;
        if (j > INT64_MAX / 17) return 1; /* j >= 0: the product's only bound */
        if (!add_i64(seed, j * 17, &x)) return 1;
        x %= 201;
        if (x < 0) x += 201; /* cint's floor remainder, including negative seeds */
        if (!sub_i64(x, 100, &amounts[j])) return 1;
    }
    for (int64_t i = 0; i < n;) {
        if (!add_i64(b, amounts[i % 256], &b)) return 1;
        if (!add_i64(i, 1, &i)) return 1;
    }
    *out = b;
    return 0;
}
