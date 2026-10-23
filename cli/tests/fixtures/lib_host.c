/* A host of the library that cli/tests/run.py builds from shapes.ci with `cint build --lib`. It
 * includes the header written beside the library, as C or as C++ (rt/OPEN.md RT-OQ-65), enters
 * shapes.twice twice, and prints where the faulting entry stopped through cint_site_resolve:
 * the position, then the active calls, outermost first. */
#include <stdio.h>
#include <string.h>

#include "shapes.h"

static void where(const char *what, const cint_program *program, cint_site site)
{
    cint_position at;
    if (!cint_site_resolve(program, site, &at)) {
        printf("%s: unresolved\n", what);
        return;
    }
    printf("%s: %.*s:%u:%u %s\n", what, (int)at.path_len, at.path, (unsigned)at.line, (unsigned)at.column,
           at.operation);
}

int main(void)
{
    static cint_fault_record record;
    cint_ctx_config config;
    cint_ctx *ctx = NULL;
    int64_t result = 0;
    cint_position none;
    memset(&config, 0, sizeof config);
    config.size = (uint32_t)sizeof config;
    config.program = cm_6_shapes.program;
    config.module = &cm_6_shapes;
    if (cint_ctx_create(&config, &ctx) != CINT_OK) return 2;
    cint_status st = cx_6_shapes_5_twice(ctx, CINT_FUEL_UNBOUNDED, 21, &result);
    printf("twice 21: %d %lld\n", (int)st, (long long)result);
    st = cx_6_shapes_5_twice(ctx, CINT_FUEL_UNBOUNDED, INT64_MAX, &result);
    printf("twice max: %d\n", (int)st);
    if (cint_ctx_fault(ctx, &record) != CINT_OK || record.code == 0u) return 3;
    printf("code %u\n", (unsigned)record.code);
    where("at", record.program, record.position);
    for (uint32_t i = 0; i < record.stack_count; i++) where("call", record.program, record.stack[i]);
    cint_site past = {record.position.module, 0u};
    printf("no site: %d %d\n", (int)cint_site_resolve(record.program, past, &none),
           (int)cint_site_resolve(NULL, record.position, &none));
    cint_ctx_destroy(ctx);
    return 0;
}
