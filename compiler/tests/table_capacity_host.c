/* Shortens one module table at a time to its capacity boundary and runs the real passes
 * (compiler/tests/table_capacity.ci). Each refusal must be the table's own diagnostic at a
 * fixed position, with the view's extent as its capacity, and nothing written past it. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cint_rt.h"

typedef cint_view V;
extern const cint_module_info cm_26_tests_x2Ftable_x5Fcapacity;
cint_status cx_26_tests_x2Ftable_x5Fcapacity_3_run(cint_ctx *, int64_t, V, V, int64_t, uint8_t, V, V, V, V, V,
                                                   V, V, V, V, V, V, V, V, V, V, V, V, V, int64_t *);
cint_status cx_26_tests_x2Ftable_x5Fcapacity_5_build(cint_ctx *, int64_t, V, V, V, V, V, V, V, V, V, V, V, V,
                                                     V, V, V, V, V, V, V, int64_t *);

enum { T_SRC, T_PATH, T_TEXT, T_TOKS, T_NODES, T_FRAMES, T_DIAG, T_SEM, T_SYMS, T_NAMES, T_WORK, T_LITS,
       T_FAULTS, T_DECLS, T_SIR, T_FUNCS, T_SLOTS, T_LFRAMES, T_LSTATE, T_STATS, T_PM, T_PBYTES,
       TABLES };

/* Element bytes, type tag and record index (into ids) of each table; -1 for no record. The
 * parser's, checker's and lowering's frames (T_FRAMES, T_WORK, T_LFRAMES) are all Frame
 * rows, the frame region's record (KR-03); a build passes T_WORK as the region. */
static const int64_t elem[TABLES] = {1, 1, 1, 32, 48, 48, 64, 56, 80, 40, 48, 1, 1, 64, 48, 48, 16, 48, 160, 8,
                                       80, 1};
static const int record[TABLES] = {-1, -1, -1, 0, 1, 2, 3, 4, 5, 6, 2, -1, -1, 7, 8, 9, 10, 2, 11, -1, 12, -1};

typedef struct table {
    uint8_t *base;
    int64_t count;
    cint_buffer_id id;
} table;

typedef struct build {
    table t[TABLES];
    int64_t result;
    cint_status status;
} build;

static uint32_t ids[13];

static V view_of(int k, const table *t)
{
    V v;
    memset(&v, 0, sizeof v);
    v.buffer = t->id;
    v.generation = CINT_BUFFER_GENERATION_FIRST;
    v.type.code = record[k] >= 0 ? CINT_TAG_RECORD : k == T_STATS ? CINT_TAG_I64 : CINT_TAG_U8;
    v.type.record_id = record[k] >= 0 ? ids[record[k]] : 0;
    v.rank = 1;
    v.perm = (uint8_t)(k == T_SRC || k == T_PATH ? CINT_VIEW_READ : CINT_VIEW_WRITE);
    v.shape[0] = t->count;
    v.stride[0] = 1;
    return v;
}

/* Generous extents for a module of n source bytes: the planned capacities of SPEC-09
 * CINTC-02, with room for interpolation tokens and imported names. T_WORK has the frame
 * region's extent, the larger of the parser's limit and the checker's. */
static void extents(int64_t n, int64_t *count)
{
    const int64_t nt = 2 * n + 16, nn = 2 * nt + 16, ns = nt / 2 + 64;
    const int64_t region = nn + 16 > 512 + nt / 32 ? nn + 16 : 512 + nt / 32;
    const int64_t c[TABLES] = {n, 7, 2 * n + 256, nt, nn, 512 + nt / 32, 101, nn, ns, 2 * ns + 4096 + 128,
                               region, n + 64, 237200, 64, 4 * nt + 16, nt / 4 + 2, ns + 64, nn, 1, 9,
                               2, 1024 + 144};
    memcpy(count, c, sizeof c);
}

/* Runs one module with the given extents (src and path supplied); decls may be shared.
 * With `whole`, the module is a build of its own through main.compile. */
static int run(cint_ctx *ctx, build *b, const char *src, int64_t module, int is_root, int whole,
               const int64_t *count, table *decls)
{
    static const char path[] = "case.ci";
    for (int k = 0; k < TABLES; k++) {
        table *t = &b->t[k];
        t->count = count[k];
        if (k == T_DECLS && decls != NULL) {
            *t = *decls;
            continue;
        }
        size_t bytes = (size_t)(t->count * elem[k]);
        t->base = malloc(bytes + 64);
        if (t->base == NULL) {
            return 0;
        }
        memset(t->base, k == T_SRC || k == T_PATH ? 0 : 0xa5, bytes + 64);
        if (k == T_SRC) {
            memcpy(t->base, src, bytes);
        } else if (k == T_PATH) {
            memcpy(t->base, path, bytes);
        } else if (k != T_TEXT && k != T_FRAMES && k != T_WORK && k != T_LFRAMES && k != T_LITS) {
            memset(t->base, 0, bytes);
        }
        const uint8_t perm = (uint8_t)(k == T_SRC || k == T_PATH ? CINT_VIEW_READ : CINT_VIEW_WRITE);
        if (cint_buffer_register_bytes(ctx, t->base, (int64_t)bytes, perm, &t->id) != CINT_OK) {
            return 0;
        }
    }
    V v[TABLES];
    for (int k = 0; k < TABLES; k++) {
        v[k] = view_of(k, &b->t[k]);
    }
    b->result = -99;
    if (whole) {
        b->status = cx_26_tests_x2Ftable_x5Fcapacity_5_build(
            ctx, CINT_FUEL_UNBOUNDED, v[T_SRC], v[T_PATH], v[T_PM], v[T_PBYTES], v[T_TEXT], v[T_TOKS], v[T_NODES],
            v[T_SIR], v[T_FUNCS], v[T_SLOTS], v[T_LSTATE], v[T_SEM], v[T_SYMS], v[T_NAMES], v[T_WORK], v[T_LITS],
            v[T_DECLS], v[T_DIAG], v[T_FAULTS], &b->result);
        return b->status == CINT_OK;
    }
    b->status = cx_26_tests_x2Ftable_x5Fcapacity_3_run(
        ctx, CINT_FUEL_UNBOUNDED, v[T_SRC], v[T_PATH], module, (uint8_t)is_root, v[T_TEXT], v[T_TOKS], v[T_NODES],
        v[T_FRAMES], v[T_DIAG], v[T_SEM], v[T_SYMS], v[T_NAMES], v[T_WORK], v[T_LITS], v[T_FAULTS], v[T_DECLS],
        v[T_SIR], v[T_FUNCS], v[T_SLOTS], v[T_LFRAMES], v[T_LSTATE], v[T_STATS], &b->result);
    return b->status == CINT_OK;
}

static void release(build *b, int keep_decls)
{
    for (int k = 0; k < TABLES; k++) {
        if (k == T_DECLS && keep_decls) {
            continue;
        }
        free(b->t[k].base);
        b->t[k].base = NULL;
    }
}

static const int64_t *stats_of(const build *b)
{
    return (const int64_t *)b->t[T_STATS].base;
}

static const int64_t *diag_of(const build *b)
{
    return (const int64_t *)b->t[T_DIAG].base;
}

/* One module, after an optional library module 0, with table `k` at extent `cap` (-1 for
 * the generous extent). probe prints the stats instead of judging. */
typedef struct expect {
    int64_t pass;      /* 0 for success, else the pass that stops */
    int64_t code;      /* 9001 or 9004 */
    int64_t line;
    int64_t column;
    int64_t limit;     /* detail[0] */
    int64_t capacity;  /* detail[1] */
} expect;

static int one(const char *name, const char *lib, const char *src, int whole, int k, int64_t cap,
               const expect *want, int probe, int64_t *stats_out)
{
    cint_ctx_config config;
    memset(&config, 0, sizeof config);
    config.size = (uint32_t)sizeof config;
    config.program = cm_26_tests_x2Ftable_x5Fcapacity.program;
    cint_ctx *ctx = NULL;
    if (cint_ctx_create(&config, &ctx) != CINT_OK) {
        return 0;
    }
    int good = 1;
    table decls;
    memset(&decls, 0, sizeof decls);
    build lb, b;
    memset(&lb, 0, sizeof lb);
    memset(&b, 0, sizeof b);
    int64_t count[TABLES];
    const int64_t module = lib != NULL ? 1 : 0;
    if (lib != NULL) {
        extents((int64_t)strlen(lib), count);
        /* The library allocates the shared declaration view. */
        good = run(ctx, &lb, lib, 0, 0, 0, count, NULL) && lb.result == 0;
        decls = lb.t[T_DECLS];
    }
    extents((int64_t)strlen(src), count);
    if (cap >= 0) {
        count[k] = cap;
    }
    good = good && run(ctx, &b, src, module, 1, whole, count, lib != NULL ? &decls : NULL);
    const int64_t *st = stats_of(&b);
    const int64_t *d = diag_of(&b);
    if (good && probe) {
        printf("%s k=%d cap=%lld result=%lld stats=(%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld) "
               "diag=(%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld)\n",
               name, k, (long long)cap, (long long)b.result, (long long)st[0], (long long)st[1],
               (long long)st[2], (long long)st[3], (long long)st[4], (long long)st[5], (long long)st[6],
               (long long)st[7], (long long)d[4], (long long)d[8], (long long)d[9], (long long)d[10],
               (long long)d[11], (long long)d[12], (long long)d[13], (long long)d[14]);
    }
    if (good && stats_out != NULL) {
        memcpy(stats_out, st, 8 * sizeof *st);
    }
    if (good && want != NULL) {
        good = b.result == want->pass;
        if (want->pass == 0) {
            good = good && d[4] == 0;
        } else {
            good = good && d[4] == 1 && d[5] == 0 && d[8] == want->code && d[9] == module &&
                   d[10] == want->line && d[11] == want->column && d[12] == want->limit &&
                   d[13] == want->capacity && d[14] == 0 && d[15] == 0;
        }
        /* Nothing past the shortened view. */
        if (cap >= 0) {
            const uint8_t *past = b.t[k].base + cap * elem[k];
            for (int i = 0; i < 64; i++) {
                good = good && past[i] == 0xa5;
            }
        }
        if (!good) {
            fprintf(stderr, "%s cap=%lld: result=%lld diag=(%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld)\n",
                    name, (long long)cap, (long long)b.result, (long long)d[4], (long long)d[8], (long long)d[9],
                    (long long)d[10], (long long)d[11], (long long)d[12], (long long)d[13], (long long)d[14],
                    (long long)d[15]);
        }
    }
    if (b.status == CINT_FAULT || lb.status == CINT_FAULT) {
        cint_fault_record rec;
        if (cint_ctx_fault(ctx, &rec) == CINT_OK) {
            fprintf(stderr, "%s cap=%lld: fault code=%u module=%u index=%u\n", name, (long long)cap,
                    (unsigned)rec.code, (unsigned)rec.position.module, (unsigned)rec.position.index);
        }
        good = 0;
    }
    cint_ctx_destroy(ctx);
    release(&b, lib != NULL);
    release(&lb, 0);
    return good;
}

static const char hole_src[] = "\"x{1}\";\n";
static const char nest_src[] =
    "I64 g(I64 a, I64 b) { return a - b; }\n"
    "export I64 f(I64 x) {\n"
    "    return ((x + 1) * (x - 2)) + g(x, (x + 3));\n"
    "}\n";
static const char literal_src[] =
    "const I64 a = 300;\n"
    "const I64 b = 70000;\n"
    "export I64 f() { return a + b; }\n";
static const char print_src[] = "\"abc\";\n";
static const char name_src[] = "I64 x = 1;\n\"{x=}\";\n";
static const char lib_src[] = "export struct P { I64 alpha; I64 beta; }\n";
static const char import_src[] =
    "import lib;\n"
    "export I64 f() {\n"
    "    lib.P p = lib.P(1, 2);\n"
    "    return p.alpha;\n"
    "}\n";

static const char lib_nest_src[] =
    "export struct Q { I64 v; }\nexport struct P { Q q; I64 w; }\nexport P mk() { return P(Q(1), 2); }\n";
static const char nest_call_src[] = "import lib;\nexport I64 f() {\n    lib.P p = lib.mk();\n    return p.w;\n}\n";
static const char test_src[] = "test \"t\" {\n    assert(1 + 1 == 2);\n}\nexport I64 run() {\n    return 2;\n}\n";

typedef struct family {
    const char *name;
    const char *lib;
    const char *src;
    int whole;
    int table;
    int64_t lo;        /* the smallest extent tried */
    int64_t need;      /* the smallest extent that fits */
    const expect *want; /* one row per extent lo to need - 1, or per entry of `extent` */
    const int64_t *extent; /* NULL, or the extents tried below need */
    int64_t extents;
} family;

/* Row 3, tokens: the scanner's own refusal at the end token, then the interpolation hole's
 * appended tokens (the literal and the hole's end token) at the hole's first byte. */
static const expect hole_want[] = {
    {1, 9001, 1, 8, 19, 2}, {2, 9001, 1, 4, 19, 3}, {2, 9001, 1, 4, 19, 4},
};
/* Parse frames (the frame region, row 14, KR-03): C9004 at the token that needs the next
 * frame. */
static const expect parse_want[] = {
    {2, 9004, 1, 1, 25, 0}, {2, 9004, 1, 1, 25, 1}, {2, 9004, 1, 30, 25, 2}, {2, 9004, 1, 34, 25, 3},
    {2, 9004, 3, 14, 25, 4}, {2, 9004, 3, 18, 25, 5}, {2, 9004, 3, 28, 25, 6}, {2, 9004, 3, 44, 25, 7},
};
/* Checker frames (the frame region): the first nn rows hold switch intervals (32 nodes
 * here), so an extent below nn is C9004 at the module before any task, with no limit
 * detail (compiler/OPEN.md CINTC-OQ-57); above it, C9004 LIM_STACK at the node of the next
 * task. */
static const expect check_want[] = {
    {3, 9004, 1, 1, 0, 0}, {3, 9004, 1, 1, 0, 0}, {3, 9004, 1, 5, 25, 32}, {3, 9004, 1, 7, 25, 33},
    {3, 9004, 1, 23, 25, 34}, {3, 9004, 1, 32, 25, 35}, {3, 9004, 1, 32, 25, 36}, {3, 9004, 1, 30, 25, 37},
    {3, 9004, 3, 16, 25, 38}, {3, 9004, 3, 14, 25, 39}, {3, 9004, 3, 40, 25, 40},
};
static const int64_t check_extent[] = {0, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40};
/* Row 13, interned names: a view shorter than twice the symbol view (182 rows for this
 * module) cannot hold the ordinary pool; C9001 LIM_SYMBOLS at the first name interned. */
static const expect names_want[] = {{3, 9001, 1, 30, 21, 0}, {3, 9001, 1, 30, 21, 181}};
static const int64_t names_extent[] = {0, 363};
/* Row 15, literal bytes: 300 takes 2 bytes and 70000 takes 3 (SPEC-01 IM-145). */
static const expect literal_want[] = {
    {3, 9001, 1, 15, 27, 0}, {3, 9001, 1, 15, 27, 1}, {3, 9001, 2, 15, 27, 2}, {3, 9001, 2, 15, 27, 3},
    {3, 9001, 2, 15, 27, 4},
};
/* Lowering frames (the frame region): C9004 LIM_STACK at the node of the next frame. */
static const expect lower_want[] = {
    {5, 9004, 1, 1, 25, 0}, {5, 9004, 1, 21, 25, 1}, {5, 9004, 1, 32, 25, 2}, {5, 9004, 1, 32, 25, 3},
    {5, 9004, 3, 21, 25, 4}, {5, 9004, 3, 16, 25, 5}, {5, 9004, 3, 42, 25, 6},
};
/* Row 2, module text, three writers: decoded print text (7 source bytes, then "abc"); the
 * name of a `{x=}` hole (19 source bytes, then "x" by the decoder and "=" by the printer);
 * the field names of an imported struct (78 source bytes, then "alpha" and "beta"). */
static const expect print_want[] = {{5, 9001, 1, 1, 16, 7}, {5, 9001, 1, 1, 16, 8}, {5, 9001, 1, 1, 16, 9}};
static const expect name_want[] = {{5, 9001, 2, 3, 16, 19}, {5, 9001, 2, 3, 16, 20}};
static const expect import_want[] = {
    {5, 9001, 3, 9, 16, 78}, {5, 9001, 3, 9, 16, 79}, {5, 9001, 3, 9, 16, 80}, {5, 9001, 3, 9, 16, 81},
    {5, 9001, 3, 9, 16, 82}, {5, 9001, 3, 9, 16, 83}, {5, 9001, 3, 9, 16, 84}, {5, 9001, 3, 9, 16, 85},
    {5, 9001, 3, 9, 16, 86},
};

/* Row 7, functions and import rows, four writers: the module's own function f, the
 * imported struct P named through its alias, the struct Q that P's field names (a row with
 * no alias token, at 1:1), and the imported function mk. Then a script's entry (at 1:1)
 * and a test (at its `test` token). */
static const expect nested_want[] = {
    {5, 9001, 2, 12, 22, 0}, {5, 9001, 3, 9, 22, 1}, {5, 9001, 1, 1, 22, 2}, {5, 9001, 3, 19, 22, 3},
};
static const expect script_want[] = {{5, 9001, 1, 1, 22, 0}};
static const expect test_want[] = {{5, 9001, 4, 12, 22, 0}, {5, 9001, 1, 1, 22, 1}};

/* The writers in main.ci, through the bridge's two calls of main.compile for a build of
 * one 29-byte module at path case.ci: row 1, the discovery pass copies the 7-byte path
 * below the 144 revision bytes; row 2, the source and 64 bytes of slack must fit the text
 * (main.front); row 16, the full pass writes the header row, then the module's block (its
 * module row and the function f). */
static const char fn_src[] = "export I64 f() { return 1; }\n";
static const expect path_want[] = {{1, 9001, 1, 1, 16, 149}, {1, 9001, 1, 1, 16, 150}};
static const expect front_want[] = {{1, 9001, 1, 1, 16, 91}, {1, 9001, 1, 1, 16, 92}};
static const expect header_want[] = {{2, 9001, 1, 1, 29, 0}, {2, 9001, 1, 1, 29, 1}, {2, 9001, 1, 12, 29, 2}};

static const family families[] = {
    {"build-paths", NULL, fn_src, 1, T_PBYTES, 149, 151, path_want, NULL, 0},
    {"build-text", NULL, fn_src, 1, T_TEXT, 91, 93, front_want, NULL, 0},
    {"build-decls", NULL, fn_src, 1, T_DECLS, 0, 3, header_want, NULL, 0},
    {"hole-tokens", NULL, hole_src, 0, T_TOKS, 2, 5, hole_want, NULL, 0},
    {"parse-frames", NULL, nest_src, 0, T_FRAMES, 0, 8, parse_want, NULL, 0},
    {"check-frames", NULL, nest_src, 0, T_WORK, 0, 41, check_want, check_extent, 11},
    {"names-view", NULL, nest_src, 0, T_NAMES, 0, 364, names_want, names_extent, 2},
    {"literal-bytes", NULL, literal_src, 0, T_LITS, 0, 5, literal_want, NULL, 0},
    {"lowering-frames", NULL, nest_src, 0, T_LFRAMES, 0, 7, lower_want, NULL, 0},
    {"print-text", NULL, print_src, 0, T_TEXT, 7, 10, print_want, NULL, 0},
    {"print-name", NULL, name_src, 0, T_TEXT, 19, 21, name_want, NULL, 0},
    {"import-names", lib_src, import_src, 0, T_TEXT, 78, 87, import_want, NULL, 0},
    {"import-functions", lib_nest_src, nest_call_src, 0, T_FUNCS, 0, 4, nested_want, NULL, 0},
    {"script-entry", NULL, print_src, 0, T_FUNCS, 0, 1, script_want, NULL, 0},
    {"test-entry", NULL, test_src, 0, T_FUNCS, 0, 2, test_want, NULL, 0},
};

int main(int argc, char **argv)
{
    if (argc != 14 && argc != 15) {
        return 2;
    }
    for (int i = 0; i < 13; i++) {
        ids[i] = (uint32_t)strtoul(argv[i + 1], NULL, 10);
    }
    const int probe = argc == 15 && strcmp(argv[14], "probe") == 0;
    int passed = 0, total = 0;
    for (size_t f = 0; f < sizeof families / sizeof families[0]; f++) {
        const family *y = &families[f];
        const expect fits = {0, 0, 0, 0, 0, 0};
        const int sparse = y->extent != NULL;
        const int64_t rows = sparse ? y->extents : y->need - y->lo;
        for (int64_t i = 0; i <= rows; i++) {
            const int64_t cap = i == rows ? y->need : sparse ? y->extent[i] : y->lo + i;
            const expect *want = i == rows ? &fits : &y->want[i];
            int good = one(y->name, y->lib, y->src, y->whole, y->table, cap, probe ? NULL : want, probe, NULL);
            passed += good;
            total++;
        }
    }
    printf("table capacity: %d of %d pass\n", passed, total);
    return passed == total ? 0 : 1;
}
