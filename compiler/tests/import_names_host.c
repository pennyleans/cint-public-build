/* Includes seed-emitted resolve.ci so the test calls its actual checked helpers.
 * Counters are injected only in this test copy. No C implementation of the trie exists. */
#include <stdio.h>
#include <stdlib.h>
#include "import_resolve.c"

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "import names: line %d: %s\n", __LINE__, #x); return 1; } } while (0)
#define DROWS 4096
#define PREFIX 8
#define BASE (PREFIX + 4096)
#define QROWS (BASE + 4096)
static ci_7_resolve_4_Name names[QROWS];
static ci_6_limits_4_Decl decls[DROWS], saved[DROWS];
static ci_6_limits_12_CompilerDiag diag[101], want_diag[101];
static ci_7_resolve_6_RState rs[1];
static ci_4_scan_5_Token toks[3];
static ci_5_parse_4_Node nodes[3];
static ci_7_resolve_3_Sem sem[3];
static ci_7_resolve_3_Sym syms[4];
static char keys[256][256];
static int64_t keyrows[256];
static cint_ctx *ctx;
static int64_t used, entries;

static void header(void)
{
    memset(decls, 0, sizeof decls);
    decls[0].f_kind = 1;
    decls[0].f_a = 2;
    decls[1].f_kind = 2;
    used = 2;
    entries = 0;
}

static void add_key(const char *key, unsigned kind)
{
    size_t n = strlen(key);
    int64_t row = used;
    decls[row].f_kind = (uint8_t)kind;
    decls[row].f_name_len = (uint8_t)n;
    decls[row].f_vis = (uint8_t)(entries % 2);
    for (size_t j = 0; j < n; ++j) {
        decls[row + (int64_t)(j / 32)].f_name[j % 32] = (uint8_t)key[j];
        if (j >= 32) { decls[row + (int64_t)(j / 32)].f_kind = 8; }
    }
    keyrows[entries] = row;
    memcpy(keys[entries], key, n + 1);
    ++entries;
    used += (int64_t)((n + 31) / 32);
    decls[0].f_a = used;
    decls[1].f_a = used - 1;
    decls[1].f_b = entries;
}

static int reset(int64_t q)
{
    memset(diag, 0, sizeof diag);
    memset(toks, 0, sizeof toks);
    for (int j = 0; j < 3; ++j) {
        toks[j].f_kind = 1;
        toks[j].f_start = j;
        toks[j].f_len = 1;
        toks[j].f_line = 7;
        toks[j].f_col = 9 + j;
    }
    return ci_7_resolve_15_resolve_x5Finit(ctx, q, names, q, rs, 1, 3, 4);
}

static int bind(int64_t q, int64_t module, int64_t block)
{
    return ci_7_resolve_14_import_x5Fbind(ctx, q, DROWS, toks, 3, names, q,
                                       decls, DROWS, diag, 101, rs, 1, module, block, 0);
}

static int lookup(int64_t module, const char *key, int64_t expected)
{
    int64_t got = -99;
    int64_t n = (int64_t)strlen(key);
    a5_decisions = a5_bytes = 0;
    REQUIRE(ci_7_resolve_14_import_x5Ffind(ctx, QROWS, DROWS, n, names, QROWS,
                                         decls, DROWS, rs, 1, module, (const uint8_t *)key,
                                         n, 0, n, &got));
    REQUIRE(got == expected);
    REQUIRE(a5_decisions <= (unsigned long)(8 + 8 * n));
    REQUIRE(a5_bytes <= (unsigned long)n);
    return 0;
}

/* The module-level pass as compiler/check.ci runs it: resolve_globals binds the aliases,
 * and resolve_remainder runs only when that returns 0. The field and signature passes
 * check.ci runs between the two have nothing to do in a module of import items. */
static int alias_pass(int64_t q, int duplicate)
{
    static const uint8_t aliases[] = "xy";
    memset(nodes, 0, sizeof nodes);
    memset(sem, 0, sizeof sem);
    memset(syms, 0, sizeof syms);
    nodes[0].f_kind = 1;
    nodes[0].f_a = 1;
    for (int j = 1; j < 3; ++j) {
        nodes[j].f_kind = 3;
        nodes[j].f_b = j - 1;
        nodes[j].f_next = j == 1 ? 2 : -1;
        sem[j].f_opty = 0;
        sem[j].f_bits = 1;
    }
    if (duplicate) { toks[1].f_start = 0; }
    int64_t result = -99;
    REQUIRE(ci_7_resolve_18_resolve_x5Fglobals(ctx, 2, 3, 4, q, 101, aliases, 2, toks, 3,
                nodes, 3, sem, 3, syms, 4, names, q, diag, 101, rs, 1, 0, decls, DROWS, &result));
    REQUIRE(result == rs[0].f_err);
    if (result != 0) { return 0; }
    result = -99;
    REQUIRE(ci_7_resolve_20_resolve_x5Fremainder(ctx, aliases, 2, toks, 3,
                nodes, 3, sem, 3, syms, 4, names, q, diag, 101, rs, 1, 0, decls, DROWS, &result));
    REQUIRE(result == rs[0].f_err);
    return 0;
}

static int cases(void)
{
    _Static_assert(sizeof names[0] == 40 && sizeof decls[0] == 64, "record sizes");
    int64_t planned = -1;
    REQUIRE(ci_6_limits_16_name_x5Fcapacity(ctx, 4, 0, &planned) && planned == 8);
    REQUIRE(ci_6_limits_16_name_x5Fcapacity(ctx, 1048576, 1048576, &planned) && planned == 4198400);
    REQUIRE(ci_6_limits_16_name_x5Fcapacity(ctx, 4, 1048577, &planned) && planned == -1);
    REQUIRE(ci_6_limits_16_name_x5Fcapacity(ctx, INT64_MAX, 1, &planned) && planned == -1);
    REQUIRE(ci_6_limits_16_name_x5Fcapacity(ctx, 4, -1, &planned) && planned == -1);

    header(); add_key("aaaa", 3); add_key("bbbb", 4);
    memcpy(saved, decls, sizeof saved);
    for (int extra = 0; extra <= 3; ++extra) {
        int64_t q = BASE + extra;
        memset(names, 0x55, sizeof names);
        REQUIRE(reset(q));
        a5_rows = a5_inserts = 0;
        REQUIRE(alias_pass(q, 0) == 0);
        if (extra < 3) {
            REQUIRE(rs[0].f_err == 9001 && diag[1].f_code == 9001);
            REQUIRE(diag[1].f_module == 3 && diag[1].f_line == 7 && diag[1].f_column == 9);
            REQUIRE(diag[1].f_detail[0] == 30 && diag[1].f_detail[1] == extra);
            memset(want_diag, 0, sizeof want_diag);
            want_diag[0].f_detail[0] = 1;
            want_diag[1].f_code = 9001; want_diag[1].f_module = 3;
            want_diag[1].f_line = 7; want_diag[1].f_column = 9;
            want_diag[1].f_detail[0] = 30; want_diag[1].f_detail[1] = extra;
            REQUIRE(memcmp(diag, want_diag, sizeof diag) == 0);
            REQUIRE(names[PREFIX].f_tok == -1 && names[PREFIX].f_bind == -1);
            REQUIRE(rs[0].f_import_next == BASE);
            if (extra == 2) { REQUIRE(names[BASE + 1].f_tok == INT64_C(0x5555555555555555)); }
        } else {
            REQUIRE(rs[0].f_err == 0 && rs[0].f_import_next == BASE + 3);
            REQUIRE(a5_rows == 2 && a5_inserts == 2);
            REQUIRE(syms[0].f_value == syms[1].f_value && syms[0].f_ty == syms[1].f_ty);
            REQUIRE(lookup(0, "aaaa", 2) == 0 && lookup(0, "bbbb", 3) == 0);
        }
        REQUIRE(memcmp(saved, decls, sizeof saved) == 0);
    }
    REQUIRE(reset(QROWS)); a5_rows = a5_inserts = 0;
    REQUIRE(alias_pass(QROWS, 1) == 0);
    REQUIRE(rs[0].f_err == 3001 && diag[1].f_line == 7 && diag[1].f_column == 10);
    REQUIRE(a5_rows == 2 && a5_inserts == 2);

    header(); decls[1].f_a = 1;
    REQUIRE(reset(BASE)); REQUIRE(alias_pass(BASE, 0) == 0);
    REQUIRE(rs[0].f_err == 0 && names[PREFIX].f_bind == -1);
    REQUIRE(lookup(0, "absent", -1) == 0);

    header();
    const int lengths[] = {1, 2, 3, 31, 32, 33, 64, 65, 127, 128, 254, 255};
    for (size_t i = 0; i < sizeof lengths / sizeof lengths[0]; ++i) {
        char key[256]; int n = lengths[i];
        memset(key, 'a', (size_t)n); key[n] = 0;
        add_key(key, 3u + (unsigned)(i % 3));
        key[n - 1] = 'z'; add_key(key, 3u + (unsigned)(i % 3));
    }
    for (int j = 0; j < 100; ++j) {
        char key[256]; memset(key, 'k', 255); key[255] = 0;
        key[253] = (char)('a' + j / 26); key[254] = (char)('a' + j % 26);
        add_key(key, 3u);
    }
    /* These distinct legal identifiers have the same FNV-1a-32 hash. */
    add_key("n_3ogbsipc18kw", 3); add_key("n_9blrrun3905z", 3);
    memcpy(saved, decls, sizeof saved);
    REQUIRE(reset(QROWS)); a5_rows = a5_inserts = 0;
    a5_bytes = 0;
    REQUIRE(bind(QROWS, 0, 1));
    unsigned long namebytes = 0;
    for (int64_t j = 0; j < entries; ++j) { namebytes += (unsigned long)strlen(keys[j]) + 1; }
    REQUIRE(a5_bytes <= 20 * namebytes);
    REQUIRE(rs[0].f_err == 0 && rs[0].f_import_next == BASE + 2 * entries - 1);
    REQUIRE(a5_rows == (unsigned long)(used - 2) && a5_inserts == (unsigned long)entries);
    unsigned long scans = a5_rows;
    for (int round = 0; round < 4; ++round) {
        REQUIRE(bind(QROWS, 0, 1));
        for (int64_t j = 0; j < entries; ++j) { REQUIRE(lookup(0, keys[j], keyrows[j]) == 0); }
        REQUIRE(lookup(0, "k", -1) == 0 && lookup(0, "kkkk", -1) == 0);
        char missing[256]; memset(missing, 'k', 255); missing[254] = 'z'; missing[255] = 0;
        REQUIRE(lookup(0, missing, -1) == 0);
    }
    for (int n = 1; n <= 255; ++n) {
        char missing[256]; memset(missing, 'a', (size_t)n); missing[n - 1] = 'b'; missing[n] = 0;
        REQUIRE(lookup(0, missing, -1) == 0);
    }
    REQUIRE(a5_rows == scans && a5_inserts == (unsigned long)entries);
    REQUIRE(memcmp(saved, decls, sizeof saved) == 0);
    /* A second module shares a spelling and returns its own canonical row. */
    int64_t second = used;
    decls[second].f_kind = 2; decls[second].f_module = 1; decls[second].f_a = 2; decls[second].f_b = 1;
    decls[second + 1].f_kind = 3; decls[second + 1].f_module = 1; decls[second + 1].f_name_len = 1;
    decls[second + 1].f_name[0] = 'a'; decls[0].f_a = second + 2;
    REQUIRE(bind(QROWS, 1, second)); REQUIRE(lookup(1, "a", second + 1) == 0);
    REQUIRE(lookup(0, "a", keyrows[0]) == 0);
    REQUIRE(reset(QROWS)); REQUIRE(bind(QROWS, 1, second));
    REQUIRE(lookup(0, "a", -2) == 0 && lookup(1, "a", second + 1) == 0);

    /* Invalid block-row kinds must stop alias binding without publishing a root.
     * Also puts a valid key first to exercise rollback after an allocation. */
    const uint8_t invalid_kinds[] = {0, 1, 2, 9, 255};
    for (size_t j = 0; j < sizeof invalid_kinds / sizeof invalid_kinds[0]; ++j) {
        for (int preceding_key = 0; preceding_key < 2; ++preceding_key) {
            header();
            if (preceding_key) { add_key("valid", 3); }
            decls[used].f_kind = invalid_kinds[j];
            ++used;
            decls[0].f_a = used;
            decls[0].f_b = 1;
            decls[1].f_a = used - 1;
            memcpy(saved, decls, sizeof saved);
            REQUIRE(reset(QROWS));
            int64_t cursor = rs[0].f_import_next;
            REQUIRE(alias_pass(QROWS, 0) == 0);
            REQUIRE(rs[0].f_err == 9102);
            memset(want_diag, 0, sizeof want_diag);
            want_diag[0].f_detail[0] = 1;
            want_diag[1].f_code = 9102; want_diag[1].f_module = 3;
            want_diag[1].f_line = 7; want_diag[1].f_column = 9;
            REQUIRE(memcmp(diag, want_diag, sizeof diag) == 0);
            REQUIRE(names[PREFIX].f_tok == -1 && names[PREFIX].f_bind == -1);
            REQUIRE(rs[0].f_import_next == cursor);
            REQUIRE(rs[0].f_nsym == 1);
            REQUIRE(memcmp(saved, decls, sizeof saved) == 0);
            REQUIRE(lookup(0, "valid", -2) == 0);
            REQUIRE(rs[0].f_err == 9102);
        }
    }

    header(); add_key("same", 3); add_key("same", 4);
    REQUIRE(reset(QROWS)); REQUIRE(bind(QROWS, 0, 1));
    REQUIRE(rs[0].f_err == 9102 && names[PREFIX].f_tok == -1);
    decls[1].f_a = DROWS;
    REQUIRE(reset(QROWS)); REQUIRE(bind(QROWS, 0, 1));
    REQUIRE(rs[0].f_err == 9102 && names[PREFIX].f_tok == -1);

    /* Imported rows do not enlarge the ordinary-name half-full limit. */
    REQUIRE(reset(QROWS)); rs[0].f_name_cap = 2;
    const uint8_t source[] = "xy";
    int64_t id = -1;
    REQUIRE(ci_7_resolve_6_intern(ctx, 2, 3, QROWS, 101, source, 2, toks, 3, names, QROWS, diag, 101, rs, 1, 0, &id));
    REQUIRE(id >= 0 && id < 2);
    REQUIRE(ci_7_resolve_6_intern(ctx, 2, 3, QROWS, 101, source, 2, toks, 3, names, QROWS, diag, 101, rs, 1, 1, &id));
    REQUIRE(id == -1 && rs[0].f_err == 9001 && diag[1].f_detail[0] == 21 && diag[1].f_detail[1] == 1);
    printf("import names: capacity, aliases, privacy keys, continuation, misses, identity, reset, bounds pass (%lld keys)\n", (long long)126);
    return 0;
}

int main(void)
{
    cint_ctx_config config;
    memset(&config, 0, sizeof config);
    config.size = (uint32_t)sizeof config;
    config.program = &cg_program;
    REQUIRE(cint_ctx_create(&config, &ctx) == CINT_OK);
    REQUIRE(cint_rt_entry_begin(ctx, CG_S(0u, 0u), CINT_FUEL_UNBOUNDED, CINT_DEPTH_FROM_CONFIG) == CINT_OK);
    int result = cases();
    REQUIRE(cint_rt_entry_end(ctx) == CINT_OK);
    cint_ctx_destroy(ctx);
    return result;
}
