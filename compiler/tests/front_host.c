/* front_host.c: runs compiler/tests/front.ci over source files (slice 2 task 2.10a).
 *
 *   front-host <token-record> <node-record> <frame-record> <diag-record> <list-file>
 *
 * The front end's test entry takes its tables as views, which cint-harness cannot pass
 * (it passes scalars only), so compiler/tests/run.py builds this host with the C that
 * cint-seed emits for compiler/tests/front.ci and the runtime, and names the record ids
 * the seed gave Token, Node, Frame and CompilerDiag. Each line of the list file is a
 * source path. For each it allocates the tables with the capacities of compiler/limits.ci
 * (SPEC-09 CINTC-02: tokens n + 1, nodes 2 * tokens + 16, 101 diagnostic rows; the stack
 * 512 + tokens / 32 up to 131,072, compiler/OPEN.md CINTC-OQ-05), registers them, calls
 * front_parse once in a fresh context, and writes
 *
 *   file <path>
 *   status <cint_status> <result>
 *   stats <8 numbers>
 *   diag <code> <line> <column>          for each retained diagnostic
 *   fault <code> <line> <column> <operation>   when the call faulted
 *   tree <text>                           when the parse succeeded
 *
 * Output is ASCII with LF line ends. Exit status 0 when every file was processed, 2 for
 * a usage error, 3 when a file cannot be read or memory runs out.
 */
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cint_rt.h"

extern const cint_module_info cm_14_tests_x2Ffront;
cint_status cx_14_tests_x2Ffront_14_front_x5Fparse(cint_ctx *ctx, int64_t fuel, cint_view p_src,
                                                    cint_view p_toks, cint_view p_nodes, cint_view p_st,
                                                    cint_view p_diag, cint_view p_dst, cint_view p_stats,
                                                    cint_view p_work, int64_t *result);

enum { TOKEN_BYTES = 32, NODE_BYTES = 48, FRAME_BYTES = 48, DIAG_BYTES = 64, DIAG_ROWS = 101, STATS = 8 };

typedef struct table {
    void *base;
    int64_t count;
    int64_t elem;
    uint16_t code;
    uint32_t record;
    uint8_t perm;
    cint_buffer_id id;
} table;

static uint32_t g_records[4];

static int read_file(const char *path, uint8_t **data, int64_t *len)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    size_t cap = 4096, n = 0;
    uint8_t *buf = malloc(cap);
    while (buf != NULL) {
        size_t got = fread(buf + n, 1, cap - n, f);
        n += got;
        if (n < cap) {
            break;
        }
        uint8_t *bigger = realloc(buf, cap * 2);
        if (bigger == NULL) {
            free(buf);
            buf = NULL;
            break;
        }
        buf = bigger;
        cap *= 2;
    }
    fclose(f);
    if (buf == NULL) {
        return 0;
    }
    *data = buf;
    *len = (int64_t)n;
    return 1;
}

static cint_view view_of(const table *t)
{
    cint_view v;
    memset(&v, 0, sizeof v);
    v.buffer = t->id;
    v.generation = CINT_BUFFER_GENERATION_FIRST;
    v.type.code = t->code;
    v.type.record_id = t->record;
    v.rank = 1;
    v.perm = t->perm;
    v.origin = 0;
    v.shape[0] = t->count;
    v.stride[0] = 1;
    return v;
}

static void print_fault(cint_ctx *ctx)
{
    cint_fault_record rec;
    if (cint_ctx_fault(ctx, &rec) != CINT_OK) {
        printf("fault ? 0 0 ?\n");
        return;
    }
    uint32_t line = 0, column = 0;
    const char *operation = "?";
    const cint_program *p = cm_14_tests_x2Ffront.program;
    if (rec.position.module < p->module_count) {
        const cint_module *m = p->modules[rec.position.module];
        if (rec.position.index < m->site_count) {
            line = m->sites[rec.position.index].line;
            column = m->sites[rec.position.index].column;
            operation = m->sites[rec.position.index].operation;
        }
    }
    printf("fault %u %u %u %s\n", (unsigned)rec.code, (unsigned)line, (unsigned)column, operation);
}

/* One source file; 0 when memory ran out. */
static int run_file(const char *path, const uint8_t *src, int64_t n)
{
    int64_t tokens = n + 1;
    int64_t stack = 512 + tokens / 32;
    if (stack > 131072) {
        stack = 131072;
    }
    int64_t out_bytes = 64 * tokens + 65536;
    table t[8] = {
        {NULL, n, 1, CINT_TAG_U8, 0, CINT_VIEW_READ, 0},
        {NULL, tokens, TOKEN_BYTES, CINT_TAG_RECORD, g_records[0], CINT_VIEW_WRITE, 0},
        {NULL, 2 * tokens + 16, NODE_BYTES, CINT_TAG_RECORD, g_records[1], CINT_VIEW_WRITE, 0},
        {NULL, stack, FRAME_BYTES, CINT_TAG_RECORD, g_records[2], CINT_VIEW_WRITE, 0},
        {NULL, DIAG_ROWS, DIAG_BYTES, CINT_TAG_RECORD, g_records[3], CINT_VIEW_WRITE, 0},
        {NULL, out_bytes, 1, CINT_TAG_U8, 0, CINT_VIEW_WRITE, 0},
        {NULL, STATS, 8, CINT_TAG_I64, 0, CINT_VIEW_WRITE, 0},
        {NULL, 2 * (2 * tokens + 16) + 16, FRAME_BYTES, CINT_TAG_RECORD, g_records[2], CINT_VIEW_WRITE, 0},
    };
    int ok = 1;
    for (int i = 0; i < 8; i++) {
        size_t bytes = (size_t)(t[i].count * t[i].elem);
        t[i].base = calloc(bytes > 0 ? bytes : 1, 1);
        if (t[i].base == NULL) {
            ok = 0;
        }
    }
    if (ok && n > 0) {
        memcpy(t[0].base, src, (size_t)n);
    }
    cint_ctx *ctx = NULL;
    cint_ctx_config config;
    memset(&config, 0, sizeof config);
    config.size = (uint32_t)sizeof config;
    config.program = cm_14_tests_x2Ffront.program;
    if (ok && cint_ctx_create(&config, &ctx) != CINT_OK) {
        ok = 0;
    }
    for (int i = 0; ok && i < 8; i++) {
        if (cint_buffer_register_bytes(ctx, t[i].base, t[i].count * t[i].elem, t[i].perm, &t[i].id) != CINT_OK) {
            ok = 0;
        }
    }
    if (ok) {
        int64_t result = -1;
        cint_status st = cx_14_tests_x2Ffront_14_front_x5Fparse(
            ctx, CINT_FUEL_UNBOUNDED, view_of(&t[0]), view_of(&t[1]), view_of(&t[2]), view_of(&t[3]),
            view_of(&t[4]), view_of(&t[5]), view_of(&t[6]), view_of(&t[7]), &result);
        printf("file %s\nstatus %d %lld\n", path, (int)st, (long long)result);
        const int64_t *stats = t[6].base;
        printf("stats");
        for (int i = 0; i < STATS; i++) {
            printf(" %lld", (long long)stats[i]);
        }
        printf("\n");
        if (st == CINT_OK) {
            const int64_t *diag = t[4].base;
            int64_t retained = diag[4];
            for (int64_t k = 1; k <= retained && k < DIAG_ROWS; k++) {
                const int64_t *row = diag + 8 * k;
                printf("diag %lld %lld %lld\n", (long long)row[0], (long long)row[2], (long long)row[3]);
            }
            if (result == 0) {
                printf("tree ");
                fwrite(t[5].base, 1, (size_t)stats[5], stdout);
            }
        } else if (st == CINT_FAULT) {
            print_fault(ctx);
        }
    }
    if (ctx != NULL) {
        cint_ctx_destroy(ctx);
    }
    for (int i = 0; i < 8; i++) {
        free(t[i].base);
    }
    return ok;
}

int main(int argc, char **argv)
{
#if defined(_WIN32)
    (void)_setmode(_fileno(stdout), _O_BINARY);
#endif
    if (argc != 6) {
        fprintf(stderr, "usage: front-host <token> <node> <frame> <diag> <list-file>\n");
        return 2;
    }
    for (int i = 0; i < 4; i++) {
        g_records[i] = (uint32_t)strtoul(argv[1 + i], NULL, 10);
    }
    FILE *list = fopen(argv[5], "rb");
    if (list == NULL) {
        fprintf(stderr, "front-host: cannot read %s\n", argv[5]);
        return 3;
    }
    char line[4096];
    int status = 0;
    while (fgets(line, sizeof line, list) != NULL) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = 0;
        }
        if (len == 0) {
            continue;
        }
        uint8_t *src = NULL;
        int64_t n = 0;
        if (!read_file(line, &src, &n)) {
            fprintf(stderr, "front-host: cannot read %s\n", line);
            status = 3;
            break;
        }
        int ok = run_file(line, src, n);
        free(src);
        if (!ok) {
            fprintf(stderr, "front-host: out of memory for %s\n", line);
            status = 3;
            break;
        }
        fflush(stdout);
    }
    fclose(list);
    return status;
}
