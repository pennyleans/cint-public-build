/* bench_call.c: times each call of one function (tools/cint_bench.py, call form).
 *
 *   bench-call --entry NAME [--arg N]... [--index K] [--fuel B] [--warmup W] [--samples S]
 *
 * Opens the function through bench_target.h (target_cint.c or target_c.c),
 * makes W calls that are not timed, then S calls, each timed alone with a
 * monotonic clock: clock_gettime(CLOCK_MONOTONIC) on POSIX systems,
 * mach_absolute_time on macOS, QueryPerformanceCounter on Windows. Samples are
 * kept in memory and printed after the last call, so no output falls inside a
 * timed interval. `--fuel` is the allowance of every call (default: none,
 * SPEC-01 IM-137). With `--index K`, argument K (from 0) of each call is the
 * call's ordinal, warm-up calls included, set before the clock starts: the
 * per-iteration form of SPEC-06 11, whose results form a digest stream.
 * Output, ASCII lines:
 *
 *   bench-call 1
 *   timer <kind> <num> <den>            ns = ticks * num / den
 *   sample <ns> <status> <result> <fuel> one per timed call, in call order
 *   end <samples>
 *
 * Exit status: 0 when every call returned status 0; 1 when a call did not (its
 * sample is the last line before `end`); 2 for a malformed command line; 3
 * when the function cannot be opened. C17, integers only.
 */
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach_time.h>
#else
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench_target.h"

#define MAX_SAMPLES 10000000LL

typedef struct sample {
    uint64_t ns;
    int64_t result;
    int64_t fuel;
    int status;
} sample;

static uint64_t g_num = 1u, g_den = 1u;

/* The timer's kind, and g_num / g_den, the nanoseconds of one tick. */
static const char *timer_init(void)
{
#if defined(_WIN32)
    LARGE_INTEGER f = {0};
    QueryPerformanceFrequency(&f);
    g_num = 1000000000u;
    g_den = (uint64_t)f.QuadPart;
    return "QueryPerformanceCounter";
#elif defined(__APPLE__)
    mach_timebase_info_data_t tb = {1u, 1u};
    (void)mach_timebase_info(&tb);
    g_num = tb.numer;
    g_den = tb.denom;
    return "mach_absolute_time";
#else
    return "CLOCK_MONOTONIC";
#endif
}

static uint64_t ticks(void)
{
#if defined(_WIN32)
    LARGE_INTEGER c = {0};
    QueryPerformanceCounter(&c);
    return (uint64_t)c.QuadPart;
#elif defined(__APPLE__)
    return mach_absolute_time();
#else
    struct timespec ts = {0, 0};
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
#endif
}

/* Nanoseconds in `dt` ticks, without overflow for any dt below 2^64 / g_num ticks. */
static uint64_t ns_of(uint64_t dt)
{
    return dt / g_den * g_num + dt % g_den * g_num / g_den;
}

/* A decimal I64 with an optional sign; false when malformed or out of range. */
static int parse_i64(const char *s, int64_t *out)
{
    uint64_t mag = 0u, limit = (uint64_t)INT64_MAX;
    int negative = 0;
    if (*s == '-' || *s == '+') {
        negative = *s == '-';
        limit += (uint64_t)negative;
        s++;
    }
    if (*s == '\0') return 0;
    for (; *s != '\0'; s++) {
        if (*s < '0' || *s > '9') return 0;
        uint64_t d = (uint64_t)(*s - '0');
        if (mag > (limit - d) / 10u) return 0;
        mag = mag * 10u + d;
    }
    if (negative) *out = mag == 0u ? 0 : -(int64_t)(mag - 1u) - 1;
    else *out = (int64_t)mag;
    return 1;
}

static int usage(const char *why)
{
    fprintf(stderr, "bench-call: %s\nusage: bench-call --entry NAME [--arg N]... [--index K] [--fuel B] "
                    "[--warmup W] [--samples S]\n", why);
    return 2;
}

int main(int argc, char **argv)
{
    const char *entry = NULL, *why = "", *kind = NULL;
    int64_t args[BENCH_MAX_ARGS] = {0, 0, 0, 0};
    int64_t fuel = -1, warmup = 1, count = 11, result = 0;
    uint32_t nargs = 0u;
    int64_t index = -1;
    bench_target *t = NULL;
    sample *s = NULL;
    int64_t done = 0;
    int status = 0;
    for (int i = 1; i < argc; i += 2) {
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        int64_t n = 0;
        if (v == NULL) return usage("an option has no value");
        if (strcmp(argv[i], "--entry") == 0) {
            entry = v;
            continue;
        }
        if (!parse_i64(v, &n)) return usage("a value is not a decimal I64");
        if (strcmp(argv[i], "--arg") == 0) {
            if (nargs == BENCH_MAX_ARGS) return usage("more than four arguments");
            args[nargs++] = n;
        } else if (strcmp(argv[i], "--index") == 0 && n >= 0 && n < (int64_t)BENCH_MAX_ARGS) {
            index = n;
        } else if (strcmp(argv[i], "--fuel") == 0 && n >= -1) {
            fuel = n;
        } else if (strcmp(argv[i], "--warmup") == 0 && n >= 0) {
            warmup = n;
        } else if (strcmp(argv[i], "--samples") == 0 && n >= 1 && n <= MAX_SAMPLES) {
            count = n;
        } else {
            return usage("an unknown option or a value out of range");
        }
    }
    if (entry == NULL) return usage("--entry is required");
    if (index >= (int64_t)nargs) return usage("--index names no argument");
    t = bench_open(entry, nargs, &why);
    if (t == NULL) {
        fprintf(stderr, "bench-call: %s: %s\n", entry, why);
        return 3;
    }
    s = (sample *)calloc((size_t)count, sizeof *s);
    if (s == NULL) {
        bench_close(t);
        fprintf(stderr, "bench-call: out of memory\n");
        return 3;
    }
    kind = timer_init();
    for (int64_t i = 0; i < warmup && status == 0; i++) {
        if (index >= 0) args[index] = i;
        status = bench_call(t, fuel, args, &result);
    }
    for (int64_t i = 0; i < count && status == 0; i++) {
        if (index >= 0) args[index] = warmup + i;
        uint64_t t0 = ticks();
        status = bench_call(t, fuel, args, &s[i].result);
        uint64_t t1 = ticks();
        s[i].ns = ns_of(t1 - t0);
        s[i].status = status;
        s[i].fuel = bench_fuel(t);
        done = i + 1;
    }
    printf("bench-call 1\ntimer %s %llu %llu\n", kind, (unsigned long long)g_num, (unsigned long long)g_den);
    for (int64_t i = 0; i < done; i++) {
        printf("sample %llu %d %lld %lld\n", (unsigned long long)s[i].ns, s[i].status, (long long)s[i].result,
               (long long)s[i].fuel);
    }
    printf("end %lld\n", (long long)done);
    free(s);
    bench_close(t);
    return status == 0 ? 0 : 1;
}
