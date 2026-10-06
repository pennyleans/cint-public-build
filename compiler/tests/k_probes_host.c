/* k_probes_host.c: runs the K reduction probes (compiler/tests/k_probes_suite.py; KR-11 of
 * docs/design/notes/2026-10-05-k-reduction.md) through the real passes of
 * compiler/tests/table_capacity.ci, each module with the tables that main.plan plans for a
 * build of that one module (SPEC-09 CINTC-02), and prints the counts that the bounds of
 * CINTC-16 rows 4, 6 and 14 speak of:
 *
 *   k-probes-host <12 record ids> <list-file>
 *
 * The record ids are those the seed gave Token, Node, Frame, CompilerDiag, Sem, Sym, Name,
 * Decl, Sir, SirFunc, SirSlot and LState. Each line of the list file is a source path. For
 * each source it writes
 *
 *   probe <path> <bytes> <result> <nodes> <tokens> <parse frames> <check frames>
 *         <lowering frames> <SIR rows> <diagnostics> <first code>
 *
 * on one line, where result is run's (0 when every pass succeeded), the frames are the most
 * in use at once (the checker's above its interval rows), and the last two are the retained
 * diagnostic count and the first diagnostic's code (0 for none). Output is ASCII with LF
 * line ends. Exit status 0 when every source ran, 2 for a usage error, 3 when a source
 * cannot be read, memory runs out, or a call does not return CINT_OK. */
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cint_rt.h"

typedef cint_view V;
extern const cint_module_info cm_26_tests_x2Ftable_x5Fcapacity;
cint_status cx_26_tests_x2Ftable_x5Fcapacity_3_run(cint_ctx *, int64_t, V, V, int64_t, uint8_t, V, V, V, V, V,
                                                   V, V, V, V, V, V, V, V, V, V, V, V, V, int64_t *);

enum { T_SRC, T_PATH, T_TEXT, T_TOKS, T_NODES, T_FRAMES, T_DIAG, T_SEM, T_SYMS, T_NAMES, T_WORK, T_LITS,
       T_FAULTS, T_DECLS, T_SIR, T_FUNCS, T_SLOTS, T_LFRAMES, T_LSTATE, T_STATS, TABLES };

/* Element bytes and record index (into ids) of each table; -1 for no record. The three
 * frame views are Frame rows of the frame region's extent (KR-03). */
static const int64_t elem[TABLES] = {1, 1, 1, 32, 48, 48, 64, 56, 80, 40, 48, 1, 1, 64, 48, 48, 16, 48, 160, 8};
static const int record[TABLES] = {-1, -1, -1, 0, 1, 2, 3, 4, 5, 6, 2, -1, -1, 7, 8, 9, 10, 2, 11, -1};

static uint32_t ids[12];

static int64_t least(int64_t a, int64_t b)
{
    return a < b ? a : b;
}

/* The planned extents of a build of one module of n bytes (compiler/main.ci plan and
 * compiler/limits.ci capacity, SPEC-09 CINTC-02). */
static void extents(int64_t n, int64_t path_bytes, int64_t *count)
{
    const int64_t t = n + 1, nodes = 2 * t + 16, syms = least(t / 2 + 64, 1048576);
    const int64_t decls = least(1 + n / 4 + n / 32 + 1, 1048576);
    const int64_t stack = least(512 + t / 32, 131072);
    const int64_t region = nodes + 16 > stack ? nodes + 16 : stack;
    const int64_t c[TABLES] = {n, path_bytes, 2 * n + 64, t, nodes, region, 101, nodes, syms,
                               2 * syms + 4096 + 2 * decls, region, (n + 1) / 2 + 64, 237200, decls,
                               4 * t + 16, least(t / 4 + 1, 65536) + 1, syms, region, 1, 9};
    memcpy(count, c, sizeof c);
}

static V view_of(int k, cint_buffer_id id, int64_t count)
{
    V v;
    memset(&v, 0, sizeof v);
    v.buffer = id;
    v.generation = CINT_BUFFER_GENERATION_FIRST;
    v.type.code = record[k] >= 0 ? CINT_TAG_RECORD : k == T_STATS ? CINT_TAG_I64 : CINT_TAG_U8;
    v.type.record_id = record[k] >= 0 ? ids[record[k]] : 0;
    v.rank = 1;
    v.perm = (uint8_t)(k == T_SRC || k == T_PATH ? CINT_VIEW_READ : CINT_VIEW_WRITE);
    v.shape[0] = count;
    v.stride[0] = 1;
    return v;
}

static int read_file(const char *path, uint8_t **data, int64_t *len)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    size_t cap = 4096, n = 0;
    uint8_t *buf = malloc(cap);
    int ok = buf != NULL;
    while (ok) {
        if (n == cap) {
            uint8_t *more = realloc(buf, cap * 2);
            if (more == NULL) {
                ok = 0;
                break;
            }
            buf = more;
            cap *= 2;
        }
        size_t got = fread(buf + n, 1, cap - n, f);
        n += got;
        if (got == 0) {
            ok = !ferror(f);
            break;
        }
    }
    fclose(f);
    if (!ok) {
        free(buf);
        return 0;
    }
    *data = buf;
    *len = (int64_t)n;
    return 1;
}

/* One probe; 0 when memory ran out or the call did not return CINT_OK. */
static int one(const char *path, const uint8_t *src, int64_t n)
{
    static const uint8_t module_path[] = "case.ci";
    const int64_t path_bytes = (int64_t)(sizeof module_path - 1);
    int64_t count[TABLES];
    uint8_t *base[TABLES];
    cint_buffer_id id[TABLES];
    extents(n, path_bytes, count);
    memset(base, 0, sizeof base);
    int ok = 1;
    for (int k = 0; k < TABLES; k++) {
        size_t bytes = (size_t)(count[k] * elem[k]);
        base[k] = calloc(bytes > 0 ? bytes : 1, 1);
        ok = ok && base[k] != NULL;
    }
    if (ok) {
        memcpy(base[T_SRC], src, (size_t)n);
        memcpy(base[T_PATH], module_path, (size_t)path_bytes);
    }
    cint_ctx *ctx = NULL;
    cint_ctx_config config;
    memset(&config, 0, sizeof config);
    config.size = (uint32_t)sizeof config;
    config.program = cm_26_tests_x2Ftable_x5Fcapacity.program;
    ok = ok && cint_ctx_create(&config, &ctx) == CINT_OK;
    for (int k = 0; ok && k < TABLES; k++) {
        const uint8_t perm = (uint8_t)(k == T_SRC || k == T_PATH ? CINT_VIEW_READ : CINT_VIEW_WRITE);
        ok = cint_buffer_register_bytes(ctx, base[k], count[k] * elem[k], perm, &id[k]) == CINT_OK;
    }
    if (ok) {
        V v[TABLES];
        for (int k = 0; k < TABLES; k++) {
            v[k] = view_of(k, id[k], count[k]);
        }
        int64_t result = -99;
        cint_status st = cx_26_tests_x2Ftable_x5Fcapacity_3_run(
            ctx, CINT_FUEL_UNBOUNDED, v[T_SRC], v[T_PATH], 0, 1, v[T_TEXT], v[T_TOKS], v[T_NODES], v[T_FRAMES],
            v[T_DIAG], v[T_SEM], v[T_SYMS], v[T_NAMES], v[T_WORK], v[T_LITS], v[T_FAULTS], v[T_DECLS], v[T_SIR],
            v[T_FUNCS], v[T_SLOTS], v[T_LFRAMES], v[T_LSTATE], v[T_STATS], &result);
        if (st != CINT_OK) {
            fprintf(stderr, "k-probes-host: %s: status %d\n", path, (int)st);
            ok = 0;
        } else {
            const int64_t *s = (const int64_t *)base[T_STATS];
            const int64_t *d = (const int64_t *)base[T_DIAG];
            printf("probe %s %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld\n", path, (long long)n,
                   (long long)result, (long long)s[6], (long long)s[0], (long long)s[1], (long long)s[2],
                   (long long)s[4], (long long)s[8], (long long)d[4], (long long)d[8]);
        }
    }
    if (ctx != NULL) {
        cint_ctx_destroy(ctx);
    }
    for (int k = 0; k < TABLES; k++) {
        free(base[k]);
    }
    return ok;
}

int main(int argc, char **argv)
{
#if defined(_WIN32)
    (void)_setmode(_fileno(stdout), _O_BINARY);
#endif
    if (argc != 14) {
        fprintf(stderr, "usage: k-probes-host <12 record ids> <list-file>\n");
        return 2;
    }
    for (int i = 0; i < 12; i++) {
        ids[i] = (uint32_t)strtoul(argv[1 + i], NULL, 10);
    }
    FILE *list = fopen(argv[13], "rb");
    if (list == NULL) {
        fprintf(stderr, "k-probes-host: cannot read %s\n", argv[13]);
        return 3;
    }
    char line[8192];
    int status = 0;
    while (status == 0 && fgets(line, sizeof line, list) != NULL) {
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
            fprintf(stderr, "k-probes-host: cannot read %s\n", line);
            status = 3;
        } else if (!one(line, src, n)) {
            status = 3;
        }
        free(src);
    }
    fclose(list);
    return status;
}
