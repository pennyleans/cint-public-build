/* Exercises real parser writes through actual short Node views. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cint_rt.h"

extern const cint_module_info cm_26_tests_x2Fparse_x5Fcapacity;
cint_status cx_26_tests_x2Fparse_x5Fcapacity_3_run(cint_ctx *, int64_t, cint_view, cint_view, cint_view,
                           cint_view, cint_view, cint_view, int64_t, int64_t *);

typedef struct table {
    void *base;
    int64_t count;
    int64_t elem;
    uint16_t code;
    uint32_t record;
    uint8_t perm;
    cint_buffer_id id;
} table;

typedef struct input_case {
    const char *name;
    const char *source;
    int64_t nodes;
    int64_t columns[16];
} input_case;

static const input_case cases[] = {
    {"empty", "", 1, {1}},
    {"constant", "const I64 a = 1;\n", 4, {1, 1, 7, 15}},
    {"if-arm", "if (true) {}", 5, {1, 1, 1, 5, 11}},
    {"named-call", "f(x= 1);", 6, {1, 1, 1, 1, 3, 6}},
    {"shape", "I64[2] a;", 5, {1, 1, 1, 4, 5}},
    {"range-bindings", "for i, j in 0..1 {}", 7, {1, 1, 5, 8, 13, 16, 18}},
    {"interpolation", "\"x{1}\";", 5, {1, 1, 1, 1, 4}},
    {"conditional", "I64 x = true ? 1 : 2;", 7, {1, 1, 1, 9, 16, 20, 14}},
};

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
    v.shape[0] = t->count;
    v.stride[0] = 1;
    return v;
}

static int sentinel(const uint8_t *bytes, size_t from, size_t to)
{
    for (size_t i = from; i < to; i++) {
        if (bytes[i] != 0xa5) {
            return 0;
        }
    }
    return 1;
}

static int one(const input_case *c, int64_t cap, const uint32_t *ids)
{
    const int64_t n = (int64_t)strlen(c->source);
    table t[6] = {
        {NULL, n, 1, CINT_TAG_U8, 0, CINT_VIEW_READ, 0},
        {NULL, 64, 32, CINT_TAG_RECORD, ids[0], CINT_VIEW_WRITE, 0},
        {NULL, cap, 48, CINT_TAG_RECORD, ids[1], CINT_VIEW_WRITE, 0},
        {NULL, 64, 48, CINT_TAG_RECORD, ids[2], CINT_VIEW_WRITE, 0},
        {NULL, 3, 64, CINT_TAG_RECORD, ids[3], CINT_VIEW_WRITE, 0},
        {NULL, 8, 8, CINT_TAG_I64, 0, CINT_VIEW_WRITE, 0},
    };
    int good = 1;
    for (int i = 0; i < 6; i++) {
        t[i].base = calloc((size_t)(t[i].count * t[i].elem) + 64, 1);
        good = good && t[i].base != NULL;
    }
    if (!good) {
        fprintf(stderr, "allocation failed\n");
        for (int i = 0; i < 6; i++) {
            free(t[i].base);
        }
        return 0;
    }
    memcpy(t[0].base, c->source, (size_t)n);
    memset(t[2].base, 0xa5, (size_t)(cap * 48) + 64);
    cint_ctx_config config;
    memset(&config, 0, sizeof config);
    config.size = (uint32_t)sizeof config;
    config.program = cm_26_tests_x2Fparse_x5Fcapacity.program;
    cint_ctx *ctx = NULL;
    good = cint_ctx_create(&config, &ctx) == CINT_OK;
    for (int i = 0; good && i < 6; i++) {
        good = cint_buffer_register_bytes(ctx, t[i].base, t[i].count * t[i].elem,
                                    t[i].perm, &t[i].id) == CINT_OK;
    }
    int64_t result = -999;
    cint_status status = CINT_REFUSED;
    if (good) {
        status = cx_26_tests_x2Fparse_x5Fcapacity_3_run(ctx, CINT_FUEL_UNBOUNDED, view_of(&t[0]), view_of(&t[1]),
                                 view_of(&t[2]), view_of(&t[3]), view_of(&t[4]), view_of(&t[5]),
                                 7, &result);
        good = status == CINT_OK;
    }
    const int64_t *diag = t[4].base;
    const int64_t *stats = t[5].base;
    const int fits = cap >= c->nodes;
    const int64_t held = fits ? c->nodes : cap;
    good = good && result == (fits ? 0 : -1) && stats[0] == result && stats[1] == held;
    if (!fits) {
        good = good && stats[2] == 9001 && diag[4] == 1 && diag[5] == 0 &&
               diag[8] == 9001 && diag[9] == 7 && diag[10] == 1 &&
               diag[11] == c->columns[cap] && diag[12] == 20 && diag[13] == cap &&
               diag[14] == 0 && diag[15] == 0;
    } else {
        good = good && stats[2] == 0 && diag[4] == 0 && diag[5] == 0;
    }
    good = good && sentinel(t[2].base, (size_t)(held * 48), (size_t)(cap * 48) + 64);
    printf("%s cap=%lld status=%d result=%lld nodes=%lld err=%lld diag=(%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld) %s\n",
           c->name, (long long)cap, (int)status, (long long)result, (long long)stats[1],
           (long long)stats[2], (long long)diag[4], (long long)diag[5], (long long)diag[8],
           (long long)diag[9], (long long)diag[10], (long long)diag[11], (long long)diag[12],
           (long long)diag[13], (long long)diag[14], (long long)diag[15], good ? "pass" : "FAIL");
    if (status == CINT_FAULT) {
        cint_fault_record rec;
        if (cint_ctx_fault(ctx, &rec) == CINT_OK) {
            printf("fault code=%u module=%u index=%u\n", (unsigned)rec.code,
                   (unsigned)rec.position.module, (unsigned)rec.position.index);
        }
    }
    if (ctx != NULL) {
        cint_ctx_destroy(ctx);
    }
    for (int i = 0; i < 6; i++) {
        free(t[i].base);
    }
    return good;
}

int main(int argc, char **argv)
{
    if (argc != 5) {
        return 2;
    }
    uint32_t ids[4];
    for (int i = 0; i < 4; i++) {
        ids[i] = (uint32_t)strtoul(argv[1 + i], NULL, 10);
    }
    int passed = 0, total = 0;
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        for (int64_t cap = 0; cap <= cases[i].nodes + 1; cap++) {
            passed += one(&cases[i], cap, ids);
            total++;
        }
    }
    printf("parse node capacity: %d of %d pass\n", passed, total);
    return passed == total ? 0 : 1;
}
