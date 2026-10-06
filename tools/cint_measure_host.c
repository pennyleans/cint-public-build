/* cint_measure_host.c: the B1 build of compiler/main.ci, observed (slice 2 task 2.17).
 *
 *   cint-measure-host <root> <out> <rel> [<rel> ...]
 *   cint-measure-host --plan < sizes
 *
 * tools/cint_measure.py links this host with the C that cint-seed emits for compiler/main.ci,
 * the runtime, rt/cint_bridge.c, and rt/cint_build.c compiled with CINT_BRIDGE_MEASURE_HOOKS,
 * so a build runs exactly as `cint emit-c` drives it, with the phase budget of the bridge.
 * It writes one JSON object per line, ASCII with LF line ends:
 *
 *   {"e":"phase","phase":P,"module":M,"budget":B,"status":S,"result":R,"fuel":F,"fault":C}
 *       after each plan (P 1), compile (2), measure (3), and emit (4) call, before the bridge
 *       translates its fault. R is the call's result when S is CINT_OK, otherwise null. F is
 *       the fuel the call consumed (A-17) when S is CINT_OK or CINT_FAULT, otherwise null:
 *       a refused call is not an entry. C is the fault code when S is CINT_FAULT.
 *   {"e":"table","table":K,"elem_bytes":E,"capacity":N,"scope":S,"used":U}
 *       for each allocated manifest row, as the build frees it. U is one more than the index
 *       of the last row holding a nonzero byte: a lower bound on the rows the compiler wrote,
 *       over every module of the build, since the bridge allocates zeroed tables.
 *   {"e":"build","result":R,"diag":[[code,module,line,column,d0,d1,d2,d3],...]}
 *       last, with the cint_build result and the retained diagnostics.
 *
 * With --plan it builds nothing: for each decimal size n on standard input, one per line, it
 * calls plan for a program of one module of n bytes, with the bridge's budget, and writes
 *
 *   {"e":"plan","n":N,"status":S,"result":R,"rows":[[elem_bytes,capacity,scope],...]}
 *
 * with the manifest rows in table order when S is CINT_OK and R is 0, otherwise no rows.
 *
 * Exit status 0 when the build or every plan ran (whatever its result), 2 for a usage error,
 * 3 when the context or the root cannot be opened.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cint_bridge.h"

extern const cint_module_info cm_4_main;

static cint_compiler_diag g_diag[CINT_BRIDGE_DIAG_ROWS];
static uint8_t g_faults[CINT_BRIDGE_FAULT_BYTES];

void cint_bridge_observe_phase(cint_ctx *ctx, cint_status status, int64_t result, int64_t phase, int64_t module,
                               int64_t budget)
{
    int64_t fuel = 0;
    cint_fault_record *f = status == CINT_FAULT ? malloc(sizeof *f) : NULL;
    bool entry = (status == CINT_OK || status == CINT_FAULT) && cint_fuel_consumed(ctx, &fuel) == CINT_OK;
    bool fault = f != NULL && cint_ctx_fault(ctx, f) == CINT_OK && f->code != 0;
    unsigned code = fault ? (unsigned)f->code : 0u;
    free(f);
    printf("{\"e\":\"phase\",\"phase\":%lld,\"module\":%lld,\"budget\":%lld,\"status\":%d,\"result\":",
           (long long)phase, (long long)module, (long long)budget, (int)status);
    if (status == CINT_OK) printf("%lld", (long long)result); else printf("null");
    if (entry) printf(",\"fuel\":%lld", (long long)fuel); else printf(",\"fuel\":null");
    if (fault) printf(",\"fault\":%u}\n", code); else printf(",\"fault\":null}\n");
    fflush(stdout);
}

void cint_bridge_observe_table(const cint_table_plan *row, const void *payload)
{
    const uint8_t *p = payload;
    int64_t e = row->elem_bytes, n = row->capacity > 0 ? row->capacity : 0, used = 0;
    for (int64_t i = n * e; i > 0; i--) {
        if (p[i - 1] != 0) {
            used = (i - 1) / e + 1;
            break;
        }
    }
    printf("{\"e\":\"table\",\"table\":%lld,\"elem_bytes\":%lld,\"capacity\":%lld,\"scope\":%lld,\"used\":%lld}\n",
           (long long)row->table, (long long)e, (long long)row->capacity, (long long)row->scope, (long long)used);
    fflush(stdout);
}

static bool plan_view(cint_ctx *ctx, void *p, int64_t n, int64_t elem, uint16_t code, uint32_t record,
                      uint8_t perm, cint_view *v)
{
    memset(v, 0, sizeof *v);
    v->generation = CINT_BUFFER_GENERATION_FIRST;
    v->type.code = code;
    v->type.record_id = record;
    v->rank = 1;
    v->perm = perm;
    v->shape[0] = n;
    v->stride[0] = 1;
    return cint_buffer_register_bytes(ctx, p, n * elem, perm, &v->buffer) == CINT_OK;
}

static int plan_sizes(cint_ctx *ctx)
{
    static cint_table_plan rows[CINT_BRIDGE_TABLES];
    char line[64];
    while (fgets(line, sizeof line, stdin) != NULL) {
        char *end = NULL;
        int64_t n = (int64_t)strtoll(line, &end, 10), r = -1;
        if (end == line || n < 0 || n > (INT64_MAX - CINT_PHASE_FUEL_C) / CINT_PHASE_FUEL_K) return 2;
        cint_view vs, vp, vd;
        cint_status st = CINT_REFUSED;
        memset(rows, 0, sizeof rows);
        memset(g_diag, 0, sizeof g_diag);
        bool ok = plan_view(ctx, &n, 1, 8, CINT_TAG_I64, 0, CINT_VIEW_READ, &vs);
        ok = plan_view(ctx, rows, CINT_BRIDGE_TABLES, 64, CINT_TAG_RECORD, 0, CINT_VIEW_WRITE, &vp) && ok;
        ok = plan_view(ctx, g_diag, CINT_BRIDGE_DIAG_ROWS, 64, CINT_TAG_RECORD, 1, CINT_VIEW_WRITE, &vd) && ok;
        if (ok) st = cint_plan(ctx, CINT_PHASE_FUEL_K * n + CINT_PHASE_FUEL_C, vs, vp, vd, &r);
        (void)cint_buffer_release(ctx, vd.buffer);
        (void)cint_buffer_release(ctx, vp.buffer);
        (void)cint_buffer_release(ctx, vs.buffer);
        if (st != CINT_OK) (void)cint_ctx_clear_fault(ctx);
        printf("{\"e\":\"plan\",\"n\":%lld,\"status\":%d,\"result\":%lld,\"rows\":[", (long long)n, (int)st,
               (long long)(st == CINT_OK ? r : -1));
        for (int k = 0; st == CINT_OK && r == 0 && k < CINT_BRIDGE_TABLES; k++)
            printf("%s[%lld,%lld,%lld]", k ? "," : "", (long long)rows[k].elem_bytes, (long long)rows[k].capacity,
                   (long long)rows[k].scope);
        printf("]}\n");
    }
    fflush(stdout);
    return 0;
}

int main(int argc, char **argv)
{
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL;
    cint_bridge_root *root = NULL;
    bool plan = argc == 2 && strcmp(argv[1], "--plan") == 0;
    if (argc < 4 && !plan) {
        fprintf(stderr, "usage: cint-measure-host <root> <out> <rel> [<rel> ...] | --plan < sizes\n");
        return 2;
    }
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = cm_4_main.program;
    if (cint_ctx_create(&cfg, &ctx) != CINT_OK) return 3;
    if (plan) {
        int code = plan_sizes(ctx);
        cint_ctx_destroy(ctx);
        return code;
    }
    if (!cint_bridge_root_open(argv[1], &root)) {
        cint_ctx_destroy(ctx);
        return 3;
    }
    int result = cint_build(ctx, root, (const char *const *)(argv + 3), argc - 3, argv[2], g_diag, g_faults);
    printf("{\"e\":\"build\",\"result\":%d,\"diag\":[", result);
    for (int64_t i = 1; i <= g_diag[0].detail[0] && i < CINT_BRIDGE_DIAG_ROWS; i++) {
        const cint_compiler_diag *d = &g_diag[i];
        printf("%s[%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld]", i == 1 ? "" : ",", (long long)d->code,
               (long long)d->module, (long long)d->line, (long long)d->column, (long long)d->detail[0],
               (long long)d->detail[1], (long long)d->detail[2], (long long)d->detail[3]);
    }
    printf("]}\n");
    cint_bridge_root_close(root);
    cint_ctx_destroy(ctx);
    return 0;
}
