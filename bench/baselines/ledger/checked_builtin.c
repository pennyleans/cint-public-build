/* The B-6 checked C baseline with the GCC and Clang overflow builtins (posture
 * 3.4, H-4): ledger.ci's `ledger` and `transactions` (bench/cpu_trials/ledger.ci)
 * with every addition, subtraction and multiplication the cint program checks
 * checked here too. Each returns 0 with *out set, or 1 on overflow with *out
 * unchanged. The bodies are those of the first checked C trials. GCC and Clang
 * only; checked_portable.c is the form every toolchain builds.
 */
#include <stdint.h>

int checked_ledger(int64_t n, int64_t initial, int64_t *out)
{
    int64_t b = initial;
    for (int64_t i = 0; i < n;) {
        if (__builtin_add_overflow(b, i % 97, &b)) return 1;
        if (__builtin_sub_overflow(b, INT64_C(48), &b)) return 1;
        if (__builtin_add_overflow(i, INT64_C(1), &i)) return 1;
    }
    *out = b;
    return 0;
}

int checked_transactions(int64_t n, int64_t initial, int64_t seed, int64_t *out)
{
    int64_t amounts[256], b = initial;
    for (int64_t j = 0; j < 256; ++j) {
        int64_t x;
        if (__builtin_mul_overflow(j, INT64_C(17), &x)) return 1;
        if (__builtin_add_overflow(seed, x, &x)) return 1;
        x %= 201;
        if (x < 0) x += 201; /* cint's floor remainder, including negative seeds */
        if (__builtin_sub_overflow(x, INT64_C(100), &amounts[j])) return 1;
    }
    for (int64_t i = 0; i < n;) {
        if (__builtin_add_overflow(b, amounts[i % 256], &b)) return 1;
        if (__builtin_add_overflow(i, INT64_C(1), &i)) return 1;
    }
    *out = b;
    return 0;
}
