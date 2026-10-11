/* orbit_1e8's baseline (B-1): figures/legs/orbit_double.c of pennyleans/cint-demo, the
 * double build behind the orbit paper's ratios, unchanged below this comment. Built with the
 * flags of the cint variant's emitted C and, on GCC and Clang, -ffp-contract=off (posture 6.2).
 * tools/cint_bench.py sets STEPS for a smaller run; the output digest is recorded, not compared. */
/* the same integrator in double: meters and meters per second, dt = 1 s,
   semi-implicit euler, same initial state, same step count */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
int main(void) {
    const double GM = 398600441800000.0;
    const long STEPS = 100000000L;
    double x = 6778137.0, y = 0.0, vx = 0.0, vy = 7668.558;
    double rmin = 1e300, rmax = 0.0;
    uint64_t digest = 0;
    for (long i = 0; i < STEPS; i++) {
        double r2 = x * x + y * y;
        double r = sqrt(r2);
        double g = GM / r2;
        double ax = -g * x / r;
        double ay = -g * y / r;
        vx += ax; vy += ay;
        x += vx; y += vy;
        if (r < rmin) rmin = r;
        if (r > rmax) rmax = r;
        uint64_t bx, by, bvx, bvy;
        memcpy(&bx, &x, 8); memcpy(&by, &y, 8); memcpy(&bvx, &vx, 8); memcpy(&bvy, &vy, 8);
        digest = (digest * 1099511628211ULL) ^ bx ^ (by << 21) ^ (bvx << 42) ^ (bvy << 7);
    }
    printf("after %ld s: x %.17g m, y %.17g m, vx %.17g m/s, vy %.17g m/s\n", STEPS, x, y, vx, vy);
    printf("radius stayed between %.17g and %.17g m\n", rmin, rmax);
    printf("state digest %llu\n", (unsigned long long)digest);
    return 0;
}
