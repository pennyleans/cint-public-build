/* wb_build.c: files, the modules of a program, and B1's `cint emit-c`, for `explain` and the
 * session (box 14 units 1b and 2a).
 *
 * The workbench compiles with B1 itself (box 14 default BX14-08): it finds a program's modules
 * in the order of slice 2 decision D-13, runs `cint emit-c`, and reads the SIR text of the stage
 * that the output set's MANIFEST.ref names (SPEC-09 CINTC-06).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cint_proc.h"
#include "sir.h"
#include "wb.h"

#if defined(_WIN32)
#define SEP "\\"
#else
#define SEP "/"
#endif

char *wb_join(const char *a, const char *b)
{
    size_t n = strlen(a) + strlen(SEP) + strlen(b) + 1u;
    char *p = malloc(n);
    if (p != NULL) (void)snprintf(p, n, "%s%s%s", a, *a ? SEP : "", b);
    return p;
}

char *wb_copy(const char *s)
{
    char *p = malloc(strlen(s) + 1u);
    if (p != NULL) memcpy(p, s, strlen(s) + 1u);
    return p;
}

char *wb_read_file(const char *path, size_t *len)
{
    FILE *f = cli_fopen(path, "rb");
    char *p = NULL;
    size_t n = 0, cap = 0;
    if (f == NULL) return NULL;
    for (;;) {
        if (n + 4096u + 1u > cap) {
            char *q = realloc(p, cap = (n + 4096u + 1u) * 2u);
            if (q == NULL) {
                free(p);
                fclose(f);
                return NULL;
            }
            p = q;
        }
        size_t got = fread(p + n, 1, 4096u, f);
        n += got;
        if (got < 4096u) break;
    }
    if (ferror(f)) {
        free(p);
        p = NULL;
    } else {
        p[n] = '\0';
        *len = n;
    }
    fclose(f);
    return p;
}

int wb_write_file(const char *path, const void *p, size_t n)
{
    FILE *f = cli_fopen(path, "wb");
    if (f == NULL) return -1;
    int bad = n != 0 && fwrite(p, 1, n, f) != n;
    return (fclose(f) != 0) + bad != 0 ? -1 : 0;
}

static int ident_char(int c, int first)
{
    return c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (!first && c >= '0' && c <= '9');
}

static const char *blanks(const char *p)
{
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

/* An import at line start `p` (tools/cint_check.py IMPORT_RE): its dotted module name as
 * [*name, *name + return value), or 0 when the line is not an import. */
static size_t import_at(const char *p, const char **name)
{
    const char *q = blanks(p), *e, *t;
    if (strncmp(q, "import", 6u) != 0 || (q[6] != ' ' && q[6] != '\t')) return 0;
    *name = q = blanks(q + 6);
    for (e = q; ident_char((unsigned char)*e, 1);) {
        while (ident_char((unsigned char)*++e, 0)) {
        }
        if (e[0] != '.' || !ident_char((unsigned char)e[1], 1)) break;
        e++;
    }
    if (e == q) return 0;
    t = blanks(e);
    if (t != e && strncmp(t, "as", 2u) == 0 && (t[2] == ' ' || t[2] == '\t')) {
        const char *a = blanks(t + 2), *z = a;
        while (ident_char((unsigned char)*z, z == a)) z++;
        if (z != a && *blanks(z) == ';') return (size_t)(e - q);
    }
    if (t[0] == '.' && t[1] == '{') {
        const char *z = strchr(t + 2, '}');
        if (z != NULL && *blanks(z + 1) == ';') return (size_t)(e - q);
    }
    return *t == ';' ? (size_t)(e - q) : 0;
}

/* The program's modules in the order of slice 2 decision D-13, as tools/cint_check.py's
 * module_order finds them: each module after every module it imports, ties in byte order of
 * path, the root module last. An import that names no file is left to the compiler. */
typedef struct modlist {
    char *path[WB_MODULES_MAX];
    uint32_t dep[WB_MODULES_MAX][32], ndep[WB_MODULES_MAX];
    uint32_t n;
} modlist;

static int find_module(const modlist *ml, const char *path)
{
    for (uint32_t i = 0; i < ml->n; i++) {
        if (strcmp(ml->path[i], path) == 0) return (int)i;
    }
    return -1;
}

static int discover(modlist *ml, const char *root, const char *rel, wb_buf *err)
{
    ml->n = 1;
    if ((ml->path[0] = wb_copy(rel)) == NULL) return -1;
    for (uint32_t m = 0; m < ml->n; m++) {
        size_t len;
        char *file = wb_join(root, ml->path[m]);
        char *src = file != NULL ? wb_read_file(file, &len) : NULL;
        free(file);
        ml->ndep[m] = 0;
        if (src == NULL) {
            if (m == 0) {
                wb_f(err, "cannot read %s under %s", rel, root);
                return -1;
            }
            continue;
        }
        for (const char *line = src; line != NULL; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : NULL) {
            const char *name;
            size_t n = import_at(line, &name);
            if (n == 0) continue;
            char dep[1024];
            if (n + 4u > sizeof dep) continue;
            for (size_t k = 0; k < n; k++) dep[k] = name[k] == '.' ? '/' : name[k];
            memcpy(dep + n, ".ci", 4u);
            char *dpath = wb_join(root, dep);
            FILE *probe = dpath != NULL ? cli_fopen(dpath, "rb") : NULL;
            free(dpath);
            if (probe == NULL || strcmp(dep, ml->path[m]) == 0) {
                if (probe != NULL) fclose(probe);
                continue;
            }
            fclose(probe);
            int at = find_module(ml, dep);
            if (at < 0) {
                if (ml->n == WB_MODULES_MAX || (ml->path[ml->n] = wb_copy(dep)) == NULL) {
                    free(src);
                    wb_f(err, "the program has more than %u modules", WB_MODULES_MAX);
                    return -1;
                }
                at = (int)ml->n++;
            }
            int seen = 0;
            for (uint32_t k = 0; k < ml->ndep[m]; k++) seen |= ml->dep[m][k] == (uint32_t)at;
            if (!seen && ml->ndep[m] < 32u) ml->dep[m][ml->ndep[m]++] = (uint32_t)at;
        }
        free(src);
    }
    return 0;
}

/* Fills order[] with module indices: each after its imports, ties in byte order, root last. */
static void module_order(const modlist *ml, uint32_t *order)
{
    unsigned char done[WB_MODULES_MAX] = {0};
    for (uint32_t placed = 0; placed < ml->n; placed++) {
        int pick = -1;
        for (int pass = 0; pass < 3 && pick < 0; pass++) {
            for (uint32_t m = 0; m < ml->n; m++) {
                int ready = !done[m] && (pass == 2 || m != 0);
                for (uint32_t k = 0; pass == 0 && ready && k < ml->ndep[m]; k++) ready = done[ml->dep[m][k]];
                if (ready && (pick < 0 || strcmp(ml->path[m], ml->path[pick]) < 0)) pick = (int)m;
            }
        }
        done[pick] = 1;
        order[placed] = (uint32_t)pick;
    }
}

int wb_modules(const char *root, const char *rel, wb_modlist *out, wb_buf *err)
{
    static modlist ml;
    uint32_t order[WB_MODULES_MAX];
    int r = discover(&ml, root, rel, err);
    out->n = 0;
    if (r == 0) {
        module_order(&ml, order);
        for (uint32_t i = 0; i < ml.n; i++) out->path[i] = ml.path[order[i]];
        out->n = ml.n;
    } else {
        for (uint32_t i = 0; i < ml.n; i++) free(ml.path[i]);
    }
    return r;
}

void wb_modules_free(wb_modlist *ml)
{
    for (uint32_t i = 0; i < ml->n; i++) free(ml->path[i]);
    ml->n = 0;
}

int wb_emit(const char *cint, int json, const char *root, const char *rel, const char *stdlib, const char *out,
            const char *log, int *status, wb_buf *err)
{
    static wb_modlist ml;
    const char *argv[WB_MODULES_MAX + 10u];
    int n = 0, r = 1;
    if (wb_modules(root, rel, &ml, err) != 0) return 2;
    argv[n++] = cint;
    if (json) argv[n++] = "--json";
    argv[n++] = "emit-c";
    argv[n++] = "--root";
    argv[n++] = root;
    argv[n++] = "--out";
    argv[n++] = out;
    if (stdlib != NULL) {
        argv[n++] = "--stdlib";
        argv[n++] = stdlib;
    }
    for (uint32_t i = 0; i < ml.n; i++) argv[n++] = ml.path[i];
    argv[n] = NULL;
    cli_proc proc = {argv, NULL, log, NULL, NULL, NULL, NULL, 0};
    if (cli_mkdirs(out) == 0 && cli_proc_run(&proc, status) == 0) r = 0;
    else wb_f(err, "cannot run %s; give --cint PATH", cint);
    wb_modules_free(&ml);
    return r;
}

/* The source bytes of each module under root, or under stdlib for a module of one path segment
 * absent under root, as `cint` reads an import (SPEC-06 3.4 --stdlib; SPEC-07 SEC-CAP-13); -1
 * for one that cannot be read. */
void wb_source_sizes(const cint_sir_program *p, const char *root, const char *stdlib, int64_t *bytes)
{
    for (uint32_t k = 0; k < p->nmod && k < WB_MODULES_MAX; k++) {
        size_t len = 0;
        char *path = wb_join(root, p->mods[k].path);
        char *text = path != NULL ? wb_read_file(path, &len) : NULL;
        if (text == NULL && path != NULL && stdlib != NULL && strchr(p->mods[k].path, '/') == NULL) {
            free(path);
            path = wb_join(stdlib, p->mods[k].path);
            text = path != NULL ? wb_read_file(path, &len) : NULL;
        }
        bytes[k] = text != NULL && len <= (size_t)INT64_MAX ? (int64_t)len : -1;
        free(text);
        free(path);
    }
}

char *wb_stage_of(const char *dir)
{
    size_t len = 0, cut = strlen(dir);
    char *ref = wb_join(dir, "MANIFEST.ref");
    char *text = ref != NULL ? wb_read_file(ref, &len) : NULL;
    char *stage;
    free(ref);
    if (text == NULL) return wb_copy(dir);
    text[strcspn(text, " \n")] = '\0';
    while (cut > 0 && dir[cut - 1u] != '/' && dir[cut - 1u] != '\\') cut--;
    stage = malloc(cut + strlen(text) + 1u);
    if (stage != NULL) (void)snprintf(stage, cut + strlen(text) + 1u, "%.*s%s", (int)cut, dir, text);
    free(text);
    return stage;
}
