/* bench_target.h: the one function bench_call.c times, behind one interface.
 *
 * Two files implement it: target_cint.c for a cint program's exported function
 * (its cx wrapper, the public C ABI of SPEC-03 5.6, found through
 * cint_library_desc), and target_c.c for a baseline function in C or another
 * language with a C ABI, `int f(int64_t a1, ..., int64_t an, int64_t *out)`,
 * named at compile time. Arguments and the result are I64; up to four
 * arguments. C17, integers only.
 */
#ifndef BENCH_TARGET_H
#define BENCH_TARGET_H

#include <stdint.h>

#define BENCH_MAX_ARGS 4u

typedef struct bench_target bench_target;

/* The function `entry` with `nargs` I64 arguments and an I64 result, ready to
 * call; NULL with *why set when the target has no such function. */
bench_target *bench_open(const char *entry, uint32_t nargs, const char **why);

/* One call. 0 with *out set, or the nonzero status the call ended with.
 * `fuel` is an allowance B >= 0 or -1 for none (CINT_FUEL_UNBOUNDED); a
 * baseline ignores it. */
int bench_call(bench_target *t, int64_t fuel, const int64_t *args, int64_t *out);

/* The fuel the last call consumed, or -1 when the target counts none. */
int64_t bench_fuel(const bench_target *t);

void bench_close(bench_target *t);

#endif
