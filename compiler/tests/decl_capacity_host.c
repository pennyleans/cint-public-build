/* A checked module drives decl.write_block through a physical short view. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cint_rt.h"

extern const cint_module_info cm_25_tests_x2Fdecl_x5Fcapacity;
cint_status cx_25_tests_x2Fdecl_x5Fcapacity_16_checked_x5Fblock(
    cint_ctx *, int64_t, cint_view, cint_view, int64_t, uint8_t, cint_view, cint_view,
    cint_view, cint_view, cint_view, cint_view, cint_view, cint_view, cint_view, cint_view,
    cint_view, int64_t *);

typedef struct table {
    void *base;
    int64_t count;
    int64_t elem;
    uint16_t code;
    uint32_t record;
    uint8_t perm;
    cint_buffer_id id;
} table;

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

static int64_t word(const void *row, size_t at)
{
    int64_t result;
    memcpy(&result, (const uint8_t *)row + at, sizeof result);
    return result;
}

static uint32_t dword(const void *row, size_t at)
{
    uint32_t result;
    memcpy(&result, (const uint8_t *)row + at, sizeof result);
    return result;
}

static int one(int64_t k, int variant, const uint32_t *ids)
{
    static const uint8_t basic[] = "const I64 a = 1;\nI64 f(I64 p) { return p; }\n";
    static const uint8_t long_name[] =
        "const I64 a = 1;\nconst I64 abcdefghijklmnopqrstuvwxyzabcdefghijklmn = 2;\n";
    static const uint8_t structure[] = "struct A { I64 x; }\n";
    static const uint8_t forward[] = "A f() { return A(1); }\nstruct A { I64 x; }\n";
    const uint8_t *src = variant == 2 ? long_name : variant == 3 ? structure :
                         variant == 4 ? forward : basic;
    static const uint8_t path[] = "case.ci";
    const int64_t n = variant == 2 ? (int64_t)(sizeof long_name - 1) :
                      variant == 3 ? (int64_t)(sizeof structure - 1) :
                      variant == 4 ? (int64_t)(sizeof forward - 1) : (int64_t)(sizeof basic - 1);
    const int64_t nt = n + 1, nn = 2 * nt + 16, ns = nt / 2 + 64;
    const int64_t stack = 512 + nt / 32;
    table t[] = {
        {(void *)src, n, 1, CINT_TAG_U8, 0, CINT_VIEW_READ, 0},
        {(void *)path, (int64_t)(sizeof path - 1), 1, CINT_TAG_U8, 0, CINT_VIEW_READ, 0},
        {NULL, nt, 32, CINT_TAG_RECORD, ids[0], CINT_VIEW_WRITE, 0},
        {NULL, nn, 48, CINT_TAG_RECORD, ids[1], CINT_VIEW_WRITE, 0},
        {NULL, stack, 48, CINT_TAG_RECORD, ids[2], CINT_VIEW_WRITE, 0},
        {NULL, 101, 64, CINT_TAG_RECORD, ids[3], CINT_VIEW_WRITE, 0},
        {NULL, nn, 56, CINT_TAG_RECORD, ids[4], CINT_VIEW_WRITE, 0},
        {NULL, ns, 80, CINT_TAG_RECORD, ids[5], CINT_VIEW_WRITE, 0},
        {NULL, 2 * ns, 40, CINT_TAG_RECORD, ids[6], CINT_VIEW_WRITE, 0},
        {NULL, nn + 16, 48, CINT_TAG_RECORD, ids[2], CINT_VIEW_WRITE, 0},
        {NULL, (n + 1) / 2 + 64, 1, CINT_TAG_U8, 0, CINT_VIEW_WRITE, 0},
        {NULL, 237200, 1, CINT_TAG_U8, 0, CINT_VIEW_WRITE, 0},
        {NULL, k, 64, CINT_TAG_RECORD, ids[7], CINT_VIEW_WRITE, 0},
    };
    const size_t tables = sizeof t / sizeof t[0];
    cint_ctx *ctx = NULL;
    cint_ctx_config config;
    int good = 1;
    int64_t result = -2;
    uint8_t *before = NULL;
    for (size_t i = 2; i < tables; i++) {
        t[i].base = calloc((size_t)(t[i].count * t[i].elem), 1);
        if (t[i].base == NULL) {
            good = 0;
        }
    }
    if (good) {
        memset(t[12].base, 0xa5, (size_t)k * 64);
        memset(&config, 0, sizeof config);
        config.size = (uint32_t)sizeof config;
        config.program = cm_25_tests_x2Fdecl_x5Fcapacity.program;
        good = cint_ctx_create(&config, &ctx) == CINT_OK;
    }
    for (size_t i = 0; good && i < tables; i++) {
        good = cint_buffer_register_bytes(ctx, t[i].base, t[i].count * t[i].elem,
                                    t[i].perm, &t[i].id) == CINT_OK;
    }
    if (good) {
        cint_status st = cx_25_tests_x2Fdecl_x5Fcapacity_16_checked_x5Fblock(
            ctx, CINT_FUEL_UNBOUNDED, view_of(&t[0]), view_of(&t[1]), 0, 1,
            view_of(&t[2]), view_of(&t[3]), view_of(&t[4]), view_of(&t[5]),
            view_of(&t[6]), view_of(&t[7]), view_of(&t[8]), view_of(&t[9]),
            view_of(&t[10]), view_of(&t[11]), view_of(&t[12]), &result);
        good = st == CINT_OK;
        if (!good) {
            fprintf(stderr, "variant=%d k=%lld status=%d\n", variant, (long long)k, (int)st);
        }
    }
    if (good && variant == 1) {
        good = result == 1;
        before = malloc((size_t)k * 64);
        good = good && before != NULL;
        if (good) {
            memcpy(before, t[12].base, (size_t)k * 64);
            cint_status st = cx_25_tests_x2Fdecl_x5Fcapacity_16_checked_x5Fblock(
                ctx, CINT_FUEL_UNBOUNDED, view_of(&t[0]), view_of(&t[1]), 1, 0,
                view_of(&t[2]), view_of(&t[3]), view_of(&t[4]), view_of(&t[5]),
                view_of(&t[6]), view_of(&t[7]), view_of(&t[8]), view_of(&t[9]),
                view_of(&t[10]), view_of(&t[11]), view_of(&t[12]), &result);
            good = st == CINT_OK;
        }
    }
    if (good) {
        const uint8_t *d = t[12].base;
        const int64_t *diag = t[5].base;
        const int64_t want_line[] = {1, 1, 2, 2, 0};
        const int64_t want_col[] = {1, 11, 5, 11, 0};
        const int64_t at = variant == 1 ? k - 5 : k - 1;
        const int64_t needed = variant == 1 ? 9 : variant == 3 ? 4 : 5;
        const int64_t kept = variant == 1 ? 5 : 1;
        const int64_t blocks = variant == 1 ? 1 : 0;
        const int64_t line = variant == 2 ? 2 : variant == 3 ? 1 :
                             variant == 4 ? (k == 2 ? 1 : 2) : want_line[at];
        const int64_t column = variant == 2 ? 11 : variant == 3 ? 16 :
                               variant == 4 ? (k == 2 ? 3 : k == 3 ? 8 : 16) : want_col[at];
        good = result == (k == needed ? 1 : 0) && d[0] == 1 &&
               word(d, 16) == (k == needed ? needed : kept) &&
               word(d, 24) == (k == needed ? blocks + 1 : blocks);
        if (k < needed) {
            good = good && diag[4] == 1 && diag[5] == 0 &&
                   diag[8] == 9001 && diag[9] == (variant == 1 ? 1 : 0) &&
                   diag[10] == line && diag[11] == column &&
                   diag[12] == 29 && diag[13] == k && diag[14] == 0 && diag[15] == 0;
            if (variant == 1) {
                good = good && memcmp(d, before, (size_t)k * 64) == 0;
            } else {
                for (int64_t i = 64; i < k * 64; i++) {
                    good = good && d[i] == 0xa5;
                }
            }
        } else {
            good = good && diag[4] == 0;
            if (variant == 0) {
                good = good && d[64] == 2 && word(d + 64, 16) == 4 &&
                       word(d + 64, 24) == 2 && d[128] == 3 &&
                       d[192] == 4 && d[256] == 7;
            } else if (variant == 1) {
                good = good && memcmp(d + 64, before + 64, 4 * 64) == 0 &&
                       d[5 * 64] == 2 && d[6 * 64] == 3 &&
                       d[7 * 64] == 4 && d[8 * 64] == 7;
            } else if (variant == 2) {
                good = good && d[3 * 64] == 3 && d[4 * 64] == 8;
            } else if (variant == 3) {
                good = good && d[2 * 64] == 5 && d[3 * 64] == 6;
            } else {
                good = good && d[2 * 64] == 4 && d[3 * 64] == 5 &&
                       d[4 * 64] == 6 && dword(d + 2 * 64, 12) == 3;
            }
        }
        if (!good) {
            fprintf(stderr, "variant=%d k=%lld result=%lld header=(%lld,%lld) diag=(%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld) row2=%u\n",
                    variant, (long long)k, (long long)result, (long long)word(d, 16),
                    (long long)word(d, 24), (long long)diag[4], (long long)diag[5],
                    (long long)diag[8], (long long)diag[10], (long long)diag[11],
                    (long long)diag[12], (long long)diag[13], (long long)diag[14],
                    k > 2 ? (unsigned)d[128] : 0u);
        }
    }
    if (ctx != NULL) {
        cint_ctx_destroy(ctx);
    }
    free(before);
    for (size_t i = 2; i < tables; i++) {
        free(t[i].base);
    }
    return good;
}

int main(int argc, char **argv)
{
    uint32_t ids[8];
    if (argc != 9) {
        return 2;
    }
    for (int i = 0; i < 8; i++) {
        ids[i] = (uint32_t)strtoul(argv[i + 1], NULL, 10);
    }
    for (int64_t k = 1; k <= 5; k++) {
        if (!one(k, 0, ids)) {
            return 1;
        }
    }
    for (int64_t k = 5; k <= 9; k++) {
        if (!one(k, 1, ids)) {
            return 1;
        }
    }
    for (int64_t k = 4; k <= 5; k++) {
        if (!one(k, 2, ids)) {
            return 1;
        }
    }
    for (int64_t k = 3; k <= 4; k++) {
        if (!one(k, 3, ids)) {
            return 1;
        }
    }
    for (int64_t k = 2; k <= 5; k++) {
        if (!one(k, 4, ids)) {
            return 1;
        }
    }
    puts("decl capacity: 18 cases pass");
    return 0;
}
