/* Runs compiler/tests/ptx.ci over a list of sources: each is a build of one module, whose
 * kernels compiler/back_ptx.ci checks and writes as PTX (box 11 unit 3). The list holds one
 * `<source>\t<output>` line per source; for each, the host prints `file <source>`, then
 * `ptx <bytes>` with the text written to <output>, `unsupported <capability> <node> <site>
 * <function> <line> <column>`, or `stopped <code> <line> <column>`. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cint_rt.h"

typedef cint_view V;
extern const cint_module_info cm_12_tests_x2Fptx;
cint_status cx_12_tests_x2Fptx_3_run(cint_ctx *, int64_t, V, V, V, V, V, V, V, V, V, V, V, V, V, V, V, V, V, V,
                                     V, V, V, int64_t *);

enum { T_SRC, T_PATH, T_PM, T_PBYTES, T_TEXT, T_TOKS, T_NODES, T_SIR, T_FUNCS, T_SLOTS, T_LSTATE, T_SEM,
       T_SYMS, T_NAMES, T_FRAMES, T_LITS, T_DECLS, T_DIAG, T_FAULTS, T_OUT, T_WHY, TABLES };

#define OUT_BYTES (1 << 20)
#define WHY_ROWS 7

/* Element bytes and record index (into ids; -1 for none) of each view, in argument order.
 * T_FRAMES is the frame region, the parser's, checker's and lowering's Frame rows (KR-03). */
static const int64_t elem[TABLES] = {1, 1, 80, 1, 1, 32, 48, 48, 48, 16, 160, 56, 80, 40, 48, 1, 64, 64, 1, 1, 8};
static const int record[TABLES] = {-1, -1, 12, -1, -1, 0, 1, 8, 9, 10, 11, 4, 5, 6, 2, -1, 7, 3, -1, -1, -1};

static uint32_t ids[13];

/* Generous extents for a module of n source bytes (the CINTC-02 capacities, with slack).
 * T_FRAMES has the frame region's extent, the larger of the parser's limit and the
 * checker's. */
static void extents(int64_t n, int64_t *count)
{
    const int64_t nt = 2 * n + 16, nn = 2 * nt + 16, ns = nt / 2 + 64;
    const int64_t region = nn + 16 > 512 + nt / 32 ? nn + 16 : 512 + nt / 32;
    const int64_t c[TABLES] = {n, 7, 2, 1024 + 144, 2 * n + 256, nt, nn, 8 * nt + 16, nt / 4 + 2, ns + 64, 1, nn,
                               ns, 2 * ns + 4096 + 128, region, n + 64, 1024, 101, 237200, OUT_BYTES, WHY_ROWS};
    memcpy(count, c, sizeof c);
}

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *data = NULL;
    long size;
    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0) {
        data = malloc((size_t)size + 1);
        if (data != NULL && fread(data, 1, (size_t)size, f) != (size_t)size) {
            free(data);
            data = NULL;
        }
        *len = (size_t)size;
    }
    fclose(f);
    return data;
}

/* One source: 0 when the host could not run it (an allocation, a fault in the compiler). */
static int one(const char *src_path, const char *out_path)
{
    static const char path[] = "case.ci";
    size_t n = 0;
    uint8_t *src = read_file(src_path, &n);
    uint8_t *base[TABLES];
    int64_t count[TABLES];
    V v[TABLES];
    int good = src != NULL;
    memset(base, 0, sizeof base);
    printf("file %s\n", src_path);
    cint_ctx_config config;
    memset(&config, 0, sizeof config);
    config.size = (uint32_t)sizeof config;
    config.program = cm_12_tests_x2Fptx.program;
    cint_ctx *ctx = NULL;
    if (!good || cint_ctx_create(&config, &ctx) != CINT_OK) {
        free(src);
        return 0;
    }
    extents((int64_t)n, count);
    for (int k = 0; k < TABLES && good; k++) {
        size_t bytes = (size_t)(count[k] * elem[k]);
        cint_buffer_id id;
        base[k] = calloc(bytes + 1, 1);
        if (base[k] == NULL) {
            good = 0;
            break;
        }
        if (k == T_SRC) {
            memcpy(base[k], src, bytes);
        } else if (k == T_PATH) {
            memcpy(base[k], path, bytes);
        }
        const uint8_t perm = (uint8_t)(k == T_SRC || k == T_PATH ? CINT_VIEW_READ : CINT_VIEW_WRITE);
        if (cint_buffer_register_bytes(ctx, base[k], (int64_t)bytes, perm, &id) != CINT_OK) {
            good = 0;
            break;
        }
        memset(&v[k], 0, sizeof v[k]);
        v[k].buffer = id;
        v[k].generation = CINT_BUFFER_GENERATION_FIRST;
        v[k].type.code = record[k] >= 0 ? CINT_TAG_RECORD : k == T_WHY ? CINT_TAG_I64 : CINT_TAG_U8;
        v[k].type.record_id = record[k] >= 0 ? ids[record[k]] : 0;
        v[k].rank = 1;
        v[k].perm = perm;
        v[k].shape[0] = count[k];
        v[k].stride[0] = 1;
    }
    int64_t result = -99;
    cint_status status = CINT_RESOURCE;
    if (good) {
        status = cx_12_tests_x2Fptx_3_run(ctx, CINT_FUEL_UNBOUNDED, v[T_SRC], v[T_PATH], v[T_PM], v[T_PBYTES],
                                          v[T_TEXT], v[T_TOKS], v[T_NODES], v[T_SIR], v[T_FUNCS], v[T_SLOTS],
                                          v[T_LSTATE], v[T_SEM], v[T_SYMS], v[T_NAMES], v[T_FRAMES], v[T_LITS],
                                          v[T_DECLS], v[T_DIAG], v[T_FAULTS], v[T_OUT], v[T_WHY], &result);
    }
    if (good && status == CINT_OK) {
        const int64_t *why = (const int64_t *)base[T_WHY];
        const int64_t *d = (const int64_t *)base[T_DIAG];
        if (result >= 0) {
            FILE *f = fopen(out_path, "wb");
            good = f != NULL && fwrite(base[T_OUT], 1, (size_t)result, f) == (size_t)result;
            good = f != NULL && fclose(f) == 0 && good;
            printf("ptx %lld\n", (long long)result);
        } else if (result == -1) {
            printf("stopped %lld %lld %lld\n", (long long)d[8], (long long)d[10], (long long)d[11]);
        } else if (result == -2) {
            printf("unsupported %.*s %lld %lld %lld %lld %lld\n", (int)why[6], (const char *)base[T_OUT],
                   (long long)why[1], (long long)why[2], (long long)why[3], (long long)why[4],
                   (long long)why[5]);
        } else {
            printf("overflow %lld\n", (long long)result);
        }
    } else if (status == CINT_FAULT) {
        cint_fault_record rec;
        if (cint_ctx_fault(ctx, &rec) == CINT_OK) {
            printf("fault code=%u module=%u index=%u\n", (unsigned)rec.code, (unsigned)rec.position.module,
                   (unsigned)rec.position.index);
        }
        good = 0;
    } else {
        good = 0;
    }
    cint_ctx_destroy(ctx);
    for (int k = 0; k < TABLES; k++) {
        free(base[k]);
    }
    free(src);
    return good;
}

int main(int argc, char **argv)
{
    char line[4096];
    int failed = 0;
    if (argc != 15) {
        return 2;
    }
    for (int i = 0; i < 13; i++) {
        ids[i] = (uint32_t)strtoul(argv[i + 1], NULL, 10);
    }
    FILE *list = fopen(argv[14], "rb");
    if (list == NULL) {
        return 2;
    }
    while (fgets(line, sizeof line, list) != NULL) {
        char *tab = strchr(line, '\t');
        char *end = strchr(line, '\n');
        if (tab == NULL || end == NULL) {
            continue;
        }
        *tab = '\0';
        *end = '\0';
        if (!one(line, tab + 1)) {
            printf("host failed\n");
            failed = 1;
        }
    }
    fclose(list);
    return failed;
}
