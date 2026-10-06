/* cint_build.c: the compiler side of the host bridge: the four calls, the output set,
 * and the build driver (SPEC-09 CINTC-05, CINTC-06, CINTC-09, CINTC-14; slice 2
 * decision patch D-3, D-13; rt/OPEN.md RT-OQ-29).
 *
 * Split from cint_bridge.c to keep each file under its SPEC-09 4.2 ceiling, and so
 * that the file layer does not depend on the compiler. Every call of the host
 * operating system's file API stays in cint_bridge.c (SPEC-09 SEED-03), behind the
 * primitives of cint_bridge_internal.h.
 */
#include "cint_bridge_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* The four calls (CINTC-14): the compiler's exports, one view per table.     */

#define V cint_view
#define TV V, V, V, V, V, V, V, V, V, V, V, V, V, V, V, V, V   /* CINT_BRIDGE_TABLES views */
#define T17(t) t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[7], t[8], t[9], t[10], t[11], t[12], \
               t[13], t[14], t[15], t[16]
_Static_assert(CINT_BRIDGE_TABLES == 17, "TV and T17 list CINT_BRIDGE_TABLES views");
cint_status cx_4_main_4_plan(cint_ctx *ctx, int64_t fuel, V, V, V, int64_t *result);
cint_status cx_4_main_7_compile(cint_ctx *ctx, int64_t fuel, int64_t, V, V, TV, V, V, int64_t *result);
cint_status cx_4_main_7_measure(cint_ctx *ctx, int64_t fuel, int64_t, TV, V, V, V, int64_t *result);
cint_status cx_4_main_4_emit(cint_ctx *ctx, int64_t fuel, int64_t, int64_t, TV, V, V, V, int64_t *result);

cint_status cint_plan(cint_ctx *ctx, int64_t fuel, V module_bytes, V plan, V diag, int64_t *result)
{
    return result == NULL ? CINT_REFUSED : cx_4_main_4_plan(ctx, fuel, module_bytes, plan, diag, result);
}

cint_status cint_compile(cint_ctx *ctx, int64_t fuel, int64_t module_index, V source, V path, const V *t,
                         int64_t table_count, V diag, V faults, int64_t *result)
{
    if (t == NULL || table_count != CINT_BRIDGE_TABLES || result == NULL) return CINT_REFUSED;
    return cx_4_main_7_compile(ctx, fuel, module_index, source, path, T17(t), diag, faults, result);
}

cint_status cint_measure(cint_ctx *ctx, int64_t fuel, int64_t module_index, const V *t, int64_t table_count,
                         V output_bytes, V diag, V faults, int64_t *result)
{
    if (t == NULL || table_count != CINT_BRIDGE_TABLES || result == NULL) return CINT_REFUSED;
    return cx_4_main_7_measure(ctx, fuel, module_index, T17(t), output_bytes, diag, faults, result);
}

cint_status cint_emit(cint_ctx *ctx, int64_t fuel, int64_t module_index, int64_t output_index, const V *t,
                      int64_t table_count, V output, V diag, V faults, int64_t *result)
{
    if (t == NULL || table_count != CINT_BRIDGE_TABLES || result == NULL) return CINT_REFUSED;
    return cx_4_main_4_emit(ctx, fuel, module_index, output_index, T17(t), output, diag, faults, result);
}

/* ------------------------------------------------------------------------- */
/* The output set (CINTC-06).                                                 */

typedef struct stage_entry { char *rel; size_t len; bool dir; char hex[65]; } stage_entry;
struct cint_stage { char *out, *dir; stage_entry *e; size_t n, cap; };
static int g_writes;

static bool stage_add(cint_stage *st, const char *rel, size_t len, bool dir, const uint8_t *bytes)
{
    if (st->n == st->cap) {
        stage_entry *e = realloc(st->e, (st->cap + 16) * sizeof *e);
        if (e == NULL) return false;
        st->e = e;
        st->cap += 16;
    }
    stage_entry *x = &st->e[st->n];
    if ((x->rel = cint_bridge_join(rel, "", "")) == NULL) return false;
    x->len = len;
    x->dir = dir;
    if (!dir) cint_bridge_sha256_hex(bytes, len, x->hex);
    st->n++;
    return true;
}

static void stage_free(cint_stage *st)
{
    for (size_t i = 0; i < st->n; i++) free(st->e[i].rel);
    free(st->e);
    free(st->out);
    free(st->dir);
    free(st);
}

bool cint_stage_begin(const char *out, cint_stage **stage)
{
    cint_stage *st = cint_bridge_path_ok(out) && stage != NULL ? calloc(1, sizeof *st) : NULL;
    int r = st != NULL && (st->out = cint_bridge_join(out, "", "")) != NULL ? 0 : -1;
    for (unsigned k = 0; r == 0 && k < cint_bridge_stage_tries(); k++) {
        free(st->dir);
        st->dir = cint_bridge_stage_name(out, k);
        r = st->dir != NULL ? cint_bridge_make_dir(st->dir) : -1;
    }
    if (r != 1) {
        if (st != NULL) stage_free(st);
        return false;
    }
    g_writes = 0;
    *stage = st;
    return true;
}

void cint_stage_abort(cint_stage *st)
{
    if (st == NULL) return;
    for (size_t i = st->n; i-- > 0;) {
        char *p = cint_bridge_join(st->dir, "/", st->e[i].rel);
        if (p != NULL) cint_bridge_remove(p, st->e[i].dir);
        free(p);
    }
    cint_bridge_remove(st->dir, true);
    stage_free(st);
}

static bool stage_file(cint_stage *st, const char *rel, const uint8_t *bytes, size_t len)
{
    char *p = cint_bridge_join(st->dir, "/", rel), *s = p != NULL ? p + strlen(st->dir) + 1 : NULL;
    bool ok = p != NULL;
    while (ok && (s = strchr(s, '/')) != NULL) {   /* intermediate directories */
        *s = '\0';
        int r = cint_bridge_make_dir(p);
        ok = r == 0 || (r == 1 && stage_add(st, p + strlen(st->dir) + 1, 0, true, NULL));
        *s++ = '/';
    }
    ok = ok && cint_bridge_write_new(p, bytes, len) == 1 && stage_add(st, rel, len, false, bytes);
    free(p);
    return ok;
}

bool cint_stage_write(cint_stage *st, const char *rel, const uint8_t *bytes, size_t len)
{
    if (st == NULL) return false;
    bool ok = cint_bridge_rel_ok(rel) && (bytes != NULL || len == 0) && strcmp(rel, "MANIFEST") != 0;
    ++g_writes;
#ifdef CINT_BRIDGE_TEST_HOOKS
    ok = ok && cint_bridge_test_fail_write != g_writes;
#endif
    ok = ok && stage_file(st, rel, bytes == NULL ? (const uint8_t *)"" : bytes, len);
    if (!ok) cint_stage_abort(st);
#ifdef CINT_BRIDGE_TEST_HOOKS
    if (ok && cint_bridge_test_kill_after == g_writes) _Exit(70);
#endif
    return ok;
}

static int entry_order(const void *a, const void *b)
{
    return strcmp(((const stage_entry *)a)->rel, ((const stage_entry *)b)->rel);
}

/* Removes the files that `list` (a MANIFEST, NUL-terminated) names under `dir`,
 * then their directories, deepest first; best effort. */
static void remove_listed(const char *dir, char *list)
{
    for (int pass = 0; pass < 2; pass++) {
        for (char *line = strchr(list, '\n'); line != NULL && line[1] != '\0'; line = strchr(line + 1, '\n')) {
            char *rel = strchr(line + 1, ' '), *end = strchr(line + 1, '\n'), *p;
            rel = rel != NULL ? strchr(rel + 1, ' ') : NULL;
            if (rel == NULL || end == NULL || rel > end) return;
            *end = '\0';
            if (cint_bridge_rel_ok(++rel) && (p = cint_bridge_join(dir, "/", rel)) != NULL) {
                if (pass == 0) cint_bridge_remove(p, false);
                for (char *s = p + strlen(p); pass == 1 && s > p + strlen(dir); s--) {
                    if (*s == '/') {
                        *s = '\0';
                        cint_bridge_remove(p, true);
                    }
                }
                free(p);
            }
            *end = '\n';
        }
    }
}

/* CINTC-06 step 4, best effort: once MANIFEST.ref is replaced, removes the stage
 * that the old one (`old`, its text) named: only a sibling `<out name>.stage-<N>`
 * other than the new stage, whose MANIFEST has the digest the old reference gave,
 * and only the files that MANIFEST lists, their directories, and the stage. */
static void remove_old_stage(const cint_stage *st, size_t base, const uint8_t *old, size_t len)
{
    size_t name = strlen(st->out) - base, sp = len >= 66 ? len - 66 : 0, d = name + 7;
    bool ok = sp > d && sp - d <= 5 && old[len - 1] == '\n' && old[sp] == ' '
              && memcmp(old, st->out + base, name) == 0 && memcmp(old + name, ".stage-", 7) == 0
              && !(strlen(st->dir + base) == sp && memcmp(old, st->dir + base, sp) == 0);
    for (size_t i = d; ok && i < sp; i++) ok = old[i] >= '0' && old[i] <= '9';
    char *stage = ok ? malloc(base + sp + 1) : NULL, *man = NULL, hex[65];
    uint8_t *list = NULL;
    size_t n = 0;
    if (stage == NULL) return;
    memcpy(stage, st->out, base);
    memcpy(stage + base, old, sp);
    stage[base + sp] = '\0';
    if ((man = cint_bridge_join(stage, "/", "MANIFEST")) != NULL
        && cint_bridge_read_file(man, (size_t)1 << 24, &list, &n)) {
        cint_bridge_sha256_hex(list, n, hex);
        list[n] = '\0';
        if (memcmp(hex, old + sp + 1, 64) == 0 && strlen((char *)list) == n) {
            remove_listed(stage, (char *)list);
            cint_bridge_remove(man, false);
            cint_bridge_remove(stage, true);
        }
    }
    free(list);
    free(man);
    free(stage);
}

bool cint_stage_commit(cint_stage *st)
{
    if (st == NULL) return false;
    size_t cap = 17, n = 0, files = 0, k = strlen(st->out), refcap = strlen(st->dir) + 68, oldlen = 0;
    for (size_t i = 0; i < st->n; i++) cap += st->e[i].dir ? 0 : strlen(st->e[i].rel) + 88;
    while (k > 0 && !cint_bridge_is_sep(st->out[k - 1])) k--;   /* the stage's own name */
    char *text = malloc(cap), *ref = malloc(refcap), hex[65];
    char *path = cint_bridge_join(st->out, "/", "MANIFEST.ref");
    uint8_t *old = NULL;
    stage_entry *sorted = malloc((st->n + 1) * sizeof *sorted);
    bool ok = text != NULL && ref != NULL && sorted != NULL && path != NULL;
    for (size_t i = 0; ok && i < st->n; i++) {
        if (!st->e[i].dir) sorted[files++] = st->e[i];
    }
    if (ok) {
        qsort(sorted, files, sizeof *sorted, entry_order);
        n = (size_t)snprintf(text, cap, "cint-manifest-1\n");
        for (size_t i = 0; i < files; i++) {
            n += (size_t)snprintf(text + n, cap - n, "%s %zu %s\n", sorted[i].hex, sorted[i].len, sorted[i].rel);
        }
        cint_bridge_sha256_hex((const uint8_t *)text, n, hex);
        snprintf(ref, refcap, "%s %s\n", st->dir + k, hex);
        if (!cint_bridge_read_file(path, 4096, &old, &oldlen)) old = NULL;
    }
    free(sorted);
    ok = ok && stage_file(st, "MANIFEST", (const uint8_t *)text, n) && cint_bridge_make_dir(st->out) >= 0
         && cint_commit_output(path, (const uint8_t *)ref, strlen(ref), refcap);
    if (ok && old != NULL) remove_old_stage(st, k, old, oldlen);
    free(text);
    free(ref);
    free(path);
    free(old);
    if (ok) stage_free(st);
    else cint_stage_abort(st);
    return ok;
}

/* ------------------------------------------------------------------------- */
/* A build (CINTC-05, D-13), with the phase budget of CINTC-09.               */

typedef struct build { cint_ctx *ctx; cint_buffer_id ids[CINT_BRIDGE_TABLES + 8]; int n; } build;

/* A rank-1 view of n elements at p, registered with ceiling `perm`. A failed
 * registration leaves the id 0, so the wrapper refuses the call. */
static cint_view view_of(build *b, void *p, int64_t n, int64_t elem, cint_type type, uint8_t perm)
{
    cint_view v;
    memset(&v, 0, sizeof v);
    if (cint_buffer_register_bytes(b->ctx, p, n * elem, perm, &v.buffer) == CINT_OK) b->ids[b->n++] = v.buffer;
    v.generation = CINT_BUFFER_GENERATION_FIRST;
    v.type = type;
    v.rank = 1;
    v.perm = perm;
    v.shape[0] = n;
    v.stride[0] = 1;
    return v;
}

static void release_to(build *b, int n)
{
    while (b->n > n) (void)cint_buffer_release(b->ctx, b->ids[--b->n]);
}

static cint_type tag(uint16_t code, uint32_t record)
{
    cint_type t;
    memset(&t, 0, sizeof t);
    t.code = code;
    t.record_id = record;
    return t;
}

#ifdef CINT_BRIDGE_TEST_HOOKS
int64_t cint_bridge_test_phase_calls[5];
#endif

/* The build result of one call, or -1 to go on. E_FUEL is the phase limit. Each caller
 * stores the call's status in a variable first, so the call that writes `r` is complete
 * before this reads it (C17 6.5.2.2p10, SEI CERT EXP10-C; decision 24). */
static int phase_end(cint_ctx *ctx, cint_status st, const int64_t *r, int64_t phase, int64_t module, int64_t budget,
                     cint_compiler_diag *diag)
{
#ifdef CINT_BRIDGE_TEST_HOOKS
    cint_bridge_test_phase_calls[phase]++;
#endif
#ifdef CINT_BRIDGE_MEASURE_HOOKS
    cint_bridge_observe_phase(ctx, st, st == CINT_OK ? *r : 0, phase, module, budget);
#endif
    cint_fault_record *f = st == CINT_FAULT ? malloc(sizeof *f) : NULL;
    bool fuel = f != NULL && cint_ctx_fault(ctx, f) == CINT_OK && f->code == CINT_E_FUEL;
    free(f);
    if (st == CINT_OK) return *r == 0 ? -1 : CINT_BUILD_DIAG;
    if (!fuel) return st == CINT_FAULT ? CINT_BUILD_FAULT : CINT_BUILD_REFUSED;
    (void)cint_ctx_clear_fault(ctx);
    memset(diag, 0, 2 * sizeof *diag);
    diag[0].detail[0] = 1;
    diag[1].code = 9001;
    diag[1].module = module;
    diag[1].detail[0] = phase;
    diag[1].detail[1] = budget;
    return CINT_BUILD_DIAG;
}

/* The element size a manifest row's type implies: its record's bytes, or the
 * width of an integer or Bool tag; 0 for anything else. */
static int64_t type_bytes(const cint_type *t)
{
    unsigned w = t->code & 0xfu, kind = t->code & 0xf0u;
    if (t->code == CINT_TAG_RECORD) return t->record_id < cm_4_main.record_count ? cm_4_main.records[t->record_id].bytes : 0;
    if (t->code == CINT_TAG_BOOL) return 1;
    return (kind == 0x10u || kind == 0x20u) && w >= 1 && w <= 4 ? (int64_t)1 << (w - 1) : 0;
}

/* Output `i` of module path `rel` (or of the program): <rel without .ci>.c or .sites. */
static char *out_name(const char *rel, int i)
{
    size_t n = strlen(rel);
    char *s = cint_bridge_join(rel, "", i == 0 ? ".c" : ".sites");
    if (s != NULL && n > 3 && strcmp(rel + n - 3, ".ci") == 0) memmove(s + n - 3, s + n, strlen(s + n) + 1);
    return s;
}

#define BUDGET(bytes) (CINT_PHASE_FUEL_K * (bytes) + CINT_PHASE_FUEL_C)

/* compile of module m (CINTC-05): the build result, or -1 to go on. */
static int compile_one(build *b, const char *rel, uint8_t *src, int64_t n, int64_t m, const cint_view *tv,
                       cint_view vd, cint_view vf, cint_compiler_diag *diag)
{
    int mark = b->n;
    int64_t r = 0, bud = BUDGET(n);
    cint_view vsrc = view_of(b, src, n, 1, tag(CINT_TAG_U8, 0), CINT_VIEW_READ);
    cint_view vrel = view_of(b, (void *)rel, (int64_t)strlen(rel), 1, tag(CINT_TAG_U8, 0), CINT_VIEW_READ);
    cint_status status = cint_compile(b->ctx, bud, m, vsrc, vrel, tv, CINT_BRIDGE_TABLES, vd, vf, &r);
    int res = phase_end(b->ctx, status, &r, 2, m, bud, diag);
    release_to(b, mark);
    return res;
}

typedef struct by_path { const char *rel; int64_t m; } by_path;

static int path_order(const void *a, const void *b)
{
    return strcmp(((const by_path *)a)->rel, ((const by_path *)b)->rel);
}

/* The revision identity (SIR-17, CINTC-05): each module's output 1, read
 * back from the stage, passed to emit at program level as output 2 + m, in byte order of
 * path. The build result, or -1 to go on. */
static int revision_inputs(build *b, cint_stage *st, const char *const *rels, int64_t count, const cint_view *tv,
                           int64_t bud, cint_view vd, cint_view vf, cint_compiler_diag *diag)
{
    by_path *o = malloc((size_t)count * sizeof *o);
    int res = o == NULL ? CINT_BUILD_IO : -1, mark = b->n;
    for (int64_t k = 0; o != NULL && k < count; k++) o[k] = (by_path){rels[k], k};
    if (o != NULL) qsort(o, (size_t)count, sizeof *o, path_order);
    for (int64_t k = 0; res < 0 && k < count; k++) {
        char *name = out_name(o[k].rel, 1), *path = name != NULL ? cint_bridge_join(st->dir, "/", name) : NULL;
        uint8_t *buf = NULL;
        size_t n = 0;
        int64_t r = 0;
        if (path == NULL || !cint_bridge_read_file(path, 268435456u, &buf, &n)) res = CINT_BUILD_IO;
        else {
            cint_status status = cint_emit(b->ctx, bud, count, 2 + o[k].m, tv, CINT_BRIDGE_TABLES,
                                           view_of(b, buf, (int64_t)n, 1, tag(CINT_TAG_U8, 0), CINT_VIEW_WRITE), vd, vf,
                                           &r);
            res = phase_end(b->ctx, status, &r, 4, -1, bud, diag);
            release_to(b, mark);
        }
        free(buf);
        free(path);
        free(name);
    }
    free(o);
    return res;
}

/* The CINTC-12 rule broken by module path rels[m], or zero (SPEC-04 LS-225, LS-15; cint_ref's
 * module_path_rule): every segment, the last without `.ci`, is an ASCII identifier of at most
 * 255 bytes and not a Windows device name, and no earlier path equals it under ASCII case
 * folding. Checked before any module is read; a broken rule is C3030 at <path>:1:1. */
static int path_rule(const char *const *rels, int64_t m)
{
    static const char dev[6][4] = {"con", "prn", "aux", "nul", "com", "lpt"};
    const char *s = rels[m];
    size_t n = strlen(s), end = n > 3 && strcmp(s + n - 3, ".ci") == 0 ? n - 3 : n, a, b;
    for (a = 0; a <= end; a = b + 1) {
        for (b = a; b < end && s[b] != '/'; b++) {
            int c = s[b] | 32;
            if (s[b] != '_' && !(s[b] >= '0' && s[b] <= '9' && b > a) && !(c >= 'a' && c <= 'z')) return CINT_PATH_IDENTIFIER;
        }
        if (b == a || b - a > 255) return CINT_PATH_IDENTIFIER;
        for (int d = 0; d < 6 && b - a >= 3; d++) {
            bool same = (s[a] | 32) == dev[d][0] && (s[a + 1] | 32) == dev[d][1] && (s[a + 2] | 32) == dev[d][2];
            if (same && (b - a == 3 ? d < 4 : b - a == 4 && d >= 4 && s[a + 3] >= '1' && s[a + 3] <= '9')) return CINT_PATH_DEVICE;
        }
    }
    for (int64_t o = 0; o < m; o++) {
        size_t i = 0;
        while (i < n && rels[o][i] != '\0' && (rels[o][i] | 32) == (s[i] | 32)
               && (((rels[o][i] | 32) >= 'a' && (rels[o][i] | 32) <= 'z') || rels[o][i] == s[i])) i++;
        if (i == n && rels[o][n] == '\0' && strcmp(rels[o], s) != 0) return CINT_PATH_COLLISION;
    }
    return 0;
}

static int report_paths(const cint_compiler_diag *diag, const cint_table_plan *rows, void *const *mem,
                        cint_diagnostic_path_sink sink, void *user)
{
    int64_t count = diag[0].detail[0];
    if (count < 0 || count >= CINT_BRIDGE_DIAG_ROWS) return CINT_BUILD_REFUSED;
    for (int64_t i = 1; i <= count; i++) {
        int64_t at = diag[i].detail[2], len = diag[i].detail[3];
        if (diag[i].code != 3030 || (at == 0 && len == 0)) continue;
        if (at < 0 || len <= 0 || mem[1] == NULL || rows[1].elem_bytes != 1 ||
            at > rows[1].capacity || len > rows[1].capacity - at) return CINT_BUILD_REFUSED;
        if (sink != NULL && !sink(user, i, (const uint8_t *)mem[1] + (size_t)at, (size_t)len))
            return CINT_BUILD_IO;
    }
    return CINT_BUILD_DIAG;
}

int cint_build_with_paths(cint_ctx *ctx, cint_bridge_root *root, const char *const *rels, int64_t count,
                          const char *out, cint_compiler_diag *diag, uint8_t *faults,
                          cint_diagnostic_path_sink path_sink, void *user)
{
    const cint_record_layout *rec = cm_4_main.records;
    if (ctx == NULL || root == NULL || rels == NULL || count < 1 || count > 4096 || out == NULL || diag == NULL
        || faults == NULL || cm_4_main.record_count < 2 || rec[0].bytes != 64 || rec[0].field_count != 7
        || rec[1].bytes != 64 || rec[1].field_count != 5) return CINT_BUILD_REFUSED;
    for (int64_t p = 0; p < count; p++) {
        int rule = path_rule(rels, p);
        if (rule != 0) {
            memset(diag, 0, CINT_BRIDGE_DIAG_ROWS * sizeof *diag);
            diag[0].detail[0] = 1;
            diag[1] = (cint_compiler_diag){3030, p, 1, 1, {rule, 0, 0, 0}};
            return CINT_BUILD_DIAG;
        }
    }
    build b = {ctx, {0}, 0};
    cint_table_plan rows[CINT_BRIDGE_TABLES];
    void *mem[CINT_BRIDGE_TABLES] = {0};
    cint_view tv[CINT_BRIDGE_TABLES], vd, vf, vo;
    uint8_t **src = calloc((size_t)count, sizeof *src);
    int64_t *size = calloc((size_t)count, sizeof *size), outb[CINT_BRIDGE_OUTPUTS], total = 0, r = 0;
    cint_stage *st = NULL;
    cint_status status;
    int res = CINT_BUILD_IO, m, i, k;
    cint_bridge_clear_inputs();
    for (m = 0; src != NULL && size != NULL && m < count; m++) {
        size_t n = 0;
        if (!cint_bridge_read_input(root, rels[m], 4194304u, &src[m], &n) || (total += (int64_t)n) > 67108864) break;
        size[m] = (int64_t)n;
    }
    if (m < count) goto done;
#ifdef CINT_BRIDGE_TEST_HOOKS
    if (cint_bridge_test_after_read != NULL) cint_bridge_test_after_read();
#endif
    memset(diag, 0, CINT_BRIDGE_DIAG_ROWS * sizeof *diag);
    memset(rows, 0, sizeof rows);
    vd = view_of(&b, diag, CINT_BRIDGE_DIAG_ROWS, 64, tag(CINT_TAG_RECORD, 1), CINT_VIEW_WRITE);
    vf = view_of(&b, faults, CINT_BRIDGE_FAULT_BYTES, 1, tag(CINT_TAG_U8, 0), CINT_VIEW_WRITE);
    vo = view_of(&b, outb, CINT_BRIDGE_OUTPUTS, 8, tag(CINT_TAG_I64, 0), CINT_VIEW_WRITE);
    cint_view vs = view_of(&b, size, count, 8, tag(CINT_TAG_I64, 0), CINT_VIEW_READ);
    cint_view vp = view_of(&b, rows, CINT_BRIDGE_TABLES, 64, tag(CINT_TAG_RECORD, 0), CINT_VIEW_WRITE);
    status = cint_plan(ctx, BUDGET(total), vs, vp, vd, &r);
    if ((res = phase_end(ctx, status, &r, 1, -1, BUDGET(total), diag)) >= 0) goto done;
    for (k = 0, res = CINT_BUILD_REFUSED; k < CINT_BRIDGE_TABLES; k++) {   /* the manifest (CINTC-14) */
        const cint_table_plan *p = &rows[k];
        int64_t e = p->elem_bytes, c = p->capacity;
        if (p->table != k || e < 1 || e != type_bytes(&p->elem_type) || p->elem_align != CINT_ELEM_ALIGN((uint64_t)e)
            || c < 0 || c > INT64_MAX / 2 / e || (uint64_t)(c * e) > SIZE_MAX / 2 || (p->scope | 1) != 1 || p->reserved != 0)
            goto done;
        if ((mem[k] = calloc((size_t)(c > 0 ? c : 1), (size_t)e)) == NULL) { res = CINT_BUILD_IO; goto done; }
        tv[k] = view_of(&b, mem[k], c, e, p->elem_type, CINT_VIEW_WRITE);
    }
    for (m = 0; m < count; m++) {   /* the discovery pass: every import resolved before any check */
        if ((res = compile_one(&b, rels[m], src[m], size[m], m, tv, vd, vf, diag)) >= 0) goto done;
    }
    res = CINT_BUILD_IO;
    if (!cint_stage_begin(out, &st)) goto done;
    for (m = 0; m <= count; m++) {   /* compile, measure, emit per module; m == count: the program */
        int64_t n = m < count ? size[m] : total, mi = m < count ? m : -1, bud = BUDGET(n);
        int mark = b.n;
        if (m < count && (res = compile_one(&b, rels[m], src[m], n, m, tv, vd, vf, diag)) >= 0) goto done;
        if (m == count && (res = revision_inputs(&b, st, rels, count, tv, bud, vd, vf, diag)) >= 0) goto done;
        memset(outb, 0, sizeof outb);
        status = cint_measure(ctx, bud, m, tv, CINT_BRIDGE_TABLES, vo, vd, vf, &r);
        res = phase_end(ctx, status, &r, 3, mi, bud, diag);
        for (i = 0; res < 0 && i < CINT_BRIDGE_OUTPUTS; i++) {
            bool present = outb[i] != 0 || (m < count && i == 1);
            uint8_t empty = 0;
            uint8_t *allocated = outb[i] > 0 && outb[i] <= 268435456 ? malloc((size_t)outb[i]) : NULL;
            uint8_t *buf = outb[i] == 0 ? &empty : allocated;
            char *name = present ? out_name(m < count ? rels[m] : "cint-program.ci", i) : NULL;
            if (present && (buf == NULL || name == NULL)) res = CINT_BUILD_REFUSED;
            else if (present) {
                status = cint_emit(ctx, bud, m, i, tv, CINT_BRIDGE_TABLES,
                                   view_of(&b, buf, outb[i], 1, tag(CINT_TAG_U8, 0), CINT_VIEW_WRITE), vd, vf, &r);
                res = phase_end(ctx, status, &r, 4, mi, bud, diag);
                release_to(&b, mark);
                if (res < 0 && !cint_stage_write(st, name, buf, (size_t)outb[i])) {
                    st = NULL;   /* a failed write aborted the stage */
                    res = CINT_BUILD_IO;
                }
            }
            free(allocated);
            free(name);
        }
        if (res >= 0) goto done;
    }
    res = cint_stage_commit(st) ? CINT_BUILD_OK : CINT_BUILD_IO;
    st = NULL;
done:
    if (res == CINT_BUILD_DIAG) res = report_paths(diag, rows, mem, path_sink, user);
    cint_stage_abort(st);
    release_to(&b, 0);
    for (k = 0; k < CINT_BRIDGE_TABLES; k++) {
#ifdef CINT_BRIDGE_MEASURE_HOOKS
        if (mem[k] != NULL) cint_bridge_observe_table(&rows[k], mem[k]);
#endif
        free(mem[k]);
    }
    for (m = 0; src != NULL && m < count; m++) cint_bridge_free_input(src[m]);
    free(src);
    free(size);
    return res;
}

int cint_build(cint_ctx *ctx, cint_bridge_root *root, const char *const *rels, int64_t count, const char *out,
               cint_compiler_diag *diag, uint8_t *faults)
{
    return cint_build_with_paths(ctx, root, rels, count, out, diag, faults, NULL, NULL);
}
