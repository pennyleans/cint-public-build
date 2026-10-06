#include <stdint.h>
#include <stdio.h>
#include "cint_rt.h"

static int zpath_total, zpath_passed;
static void zpath_check(const char *name, int ok)
{
    zpath_total++; zpath_passed += ok != 0;
    if (!ok) { fprintf(stderr, "FAILED %s\n", name); }
}

static int zero_paths(void)
{
    uint8_t first = 0, second = 0;
    cint_rt_zero_path ap, bp, cp, dp;
    cint_rt_zero_place a = cint_rt_zero_local(&first);
    cint_rt_zero_place b = cint_rt_zero_local(&second);
    cint_rt_zero_place x, y, child, peer;
    cint_view av = {0}, bv = {0};
    zpath_check("same local allocation", cint_rt_zero_overlap(a, 2, a, 2));
    zpath_check("different local allocation", !cint_rt_zero_overlap(a, 2, b, 2));
    x = cint_rt_zero_index(a, 1); y = cint_rt_zero_index(a, 2);
    zpath_check("same element", cint_rt_zero_overlap(x, 1, x, 1));
    zpath_check("different elements", !cint_rt_zero_overlap(x, 1, y, 1));
    zpath_check("array contains element", cint_rt_zero_overlap(a, 2, x, 1));
    zpath_check("adjacent element", !cint_rt_zero_overlap(a, 2, y, 1));
    child = cint_rt_zero_field(x, &ap, 0);
    peer = cint_rt_zero_field(x, &bp, 0);
    zpath_check("equal paths in separate nodes", cint_rt_zero_overlap(child, 3, peer, 3));
    peer = cint_rt_zero_field(x, &bp, 1);
    zpath_check("sibling fields", !cint_rt_zero_overlap(child, 3, peer, 3));
    peer = cint_rt_zero_field(y, &bp, 0);
    zpath_check("different parent element", !cint_rt_zero_overlap(child, 3, peer, 3));
    zpath_check("ancestor contains field", cint_rt_zero_overlap(a, 2, child, 3));
    zpath_check("reverse ancestor", cint_rt_zero_overlap(child, 3, a, 2));
    zpath_check("ancestor outside field", !cint_rt_zero_overlap(a, 1, child, 3));
    zpath_check("empty field", !cint_rt_zero_overlap(a, 2, child, 0));
    x = cint_rt_zero_index(child, 2);
    y = cint_rt_zero_field(x, &cp, 4);
    peer = cint_rt_zero_field(cint_rt_zero_index(cint_rt_zero_field(
        cint_rt_zero_index(a, 1), &bp, 0), 2), &dp, 4);
    zpath_check("nested equal paths", cint_rt_zero_overlap(y, 5, peer, 5));
    zpath_check("nested ancestor", cint_rt_zero_overlap(child, 3, y, 5));
    zpath_check("nested ancestor excludes element", !cint_rt_zero_overlap(child, 2, y, 5));
    zpath_check("root still identifies nested place", y.local == &first && y.path == &cp &&
          y.path->parent == &ap && y.path->index == 2 && y.path->field == 4 && y.origin == 0);
    av.buffer = 11; av.generation = 7; av.origin = INT64_MAX - 4;
    bv = av; bv.origin = INT64_MAX - 2;
    x = cint_rt_zero_bound(&av); y = cint_rt_zero_bound(&bv);
    zpath_check("public root", x.local == NULL && x.buffer == 11 && x.generation == 7 &&
          x.origin == INT64_MAX - 4 && x.path == NULL);
    zpath_check("high origins overlap", cint_rt_zero_overlap(x, 3, y, 2));
    zpath_check("high origins adjacent", !cint_rt_zero_overlap(x, 2, y, 2));
    y = cint_rt_zero_index(x, 3);
    zpath_check("checked index retains public origin", y.origin == INT64_MAX - 1 && y.buffer == 11);
    bv.buffer = 12; y = cint_rt_zero_bound(&bv);
    zpath_check("different registration", !cint_rt_zero_overlap(x, 4, y, 2));
    bv.buffer = 11; bv.generation = 8; y = cint_rt_zero_bound(&bv);
    zpath_check("different generation", !cint_rt_zero_overlap(x, 4, y, 2));
    zpath_check("local and registration differ", !cint_rt_zero_overlap(x, 4, a, 4));
    printf("zero paths: %d of %d pass\n", zpath_passed, zpath_total);
    return zpath_passed == zpath_total ? 0 : 1;
}
