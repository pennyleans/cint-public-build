/* target_c.c: bench_target.h for a baseline function with a C ABI.
 *
 * Compile with BENCH_FN, the function's name, and BENCH_ARITY, its number of
 * I64 arguments (0 to 4); the function is
 *     int BENCH_FN(int64_t a1, ..., int64_t an, int64_t *out)
 * returning 0 with *out set, or nonzero (a detected overflow). Written in C,
 * or in Rust as `extern "C" fn(..., out: &mut i64) -> i32`.
 */
#include <stdlib.h>
#include <string.h>

#include "bench_target.h"

#if !defined(BENCH_FN) || !defined(BENCH_ARITY)
#error "target_c.c needs BENCH_FN and BENCH_ARITY"
#endif

#define BENCH_TEXT(x) #x
#define BENCH_NAME(x) BENCH_TEXT(x)

#if BENCH_ARITY == 0
int BENCH_FN(int64_t *out);
#define BENCH_INVOKE(a, out) BENCH_FN(out)
#elif BENCH_ARITY == 1
int BENCH_FN(int64_t a1, int64_t *out);
#define BENCH_INVOKE(a, out) BENCH_FN((a)[0], out)
#elif BENCH_ARITY == 2
int BENCH_FN(int64_t a1, int64_t a2, int64_t *out);
#define BENCH_INVOKE(a, out) BENCH_FN((a)[0], (a)[1], out)
#elif BENCH_ARITY == 3
int BENCH_FN(int64_t a1, int64_t a2, int64_t a3, int64_t *out);
#define BENCH_INVOKE(a, out) BENCH_FN((a)[0], (a)[1], (a)[2], out)
#elif BENCH_ARITY == 4
int BENCH_FN(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t *out);
#define BENCH_INVOKE(a, out) BENCH_FN((a)[0], (a)[1], (a)[2], (a)[3], out)
#else
#error "BENCH_ARITY is 0 to 4"
#endif

struct bench_target {
    uint32_t nargs;
};

bench_target *bench_open(const char *entry, uint32_t nargs, const char **why)
{
    bench_target *t = NULL;
    if (strcmp(entry, BENCH_NAME(BENCH_FN)) != 0) {
        *why = "this baseline was built for another function";
        return NULL;
    }
    if (nargs != (uint32_t)BENCH_ARITY) {
        *why = "the baseline takes another number of arguments";
        return NULL;
    }
    t = (bench_target *)malloc(sizeof *t);
    if (t == NULL) {
        *why = "out of memory";
        return NULL;
    }
    t->nargs = nargs;
    return t;
}

int bench_call(bench_target *t, int64_t fuel, const int64_t *a, int64_t *out)
{
    (void)t;
    (void)fuel;
    (void)a;
    return BENCH_INVOKE(a, out);
}

int64_t bench_fuel(const bench_target *t)
{
    (void)t;
    return -1;
}

void bench_close(bench_target *t)
{
    free(t);
}
