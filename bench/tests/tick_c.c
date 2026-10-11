/* tick.ci in C17, the stand-in's baseline for the tests of tools/cint_bench.py. */
#include <stdint.h>

int tick_c(int64_t t, int64_t steps, int64_t *out)
{
    int64_t d = t;
    for (int64_t i = 0; i < steps; i++) {
        d = (d * 31 + i + t) % 1000003;
    }
    *out = d;
    return 0;
}
