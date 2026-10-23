/* cint_discover.c: module discovery for the `cint` command (SPEC-06 3.3, SPEC-09 RCPT-07;
 * OPEN-QUESTIONS.md OQ-184 as amended by the I/O services lane's U4).
 *
 * The root module and, breadth first, every module its imports name are read through the bridge
 * reader (rt/cint_bridge.h; identity from the reading handle, D-3): an import `a.b` names a/b.ci
 * under the source root, and an import of one segment whose path is absent there is probed under
 * the standard library root, --stdlib DIR or `lib` beside the executable (SPEC-07 SEC-CAP-13,
 * ruling OQ-303; rt/OPEN.md RT-OQ-71). A path no file answers is kept as an absent lookup for the
 * receipt; the compiler's own discovery stays the authority on what an import means. The modules
 * then go in dependency order (D-13), and `cint run`, `cint build` and `cint test` build from a
 * snapshot of them. Shared state and helpers: cli/cint_receipt.h (decision 26). */
#define _CRT_SECURE_NO_WARNINGS 1
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cint_bridge.h"
#include "cint_proc.h"
#include "cint_receipt.h"

/* The standard library root (header comment): NULL when there is none. */
cint_bridge_root *lib_root;

/* Opens --stdlib DIR, or `lib` beside this executable when it is a directory. */
int open_stdlib(void)
{
    if (g.stdlib != NULL) {
        return cint_bridge_root_open(g.stdlib, &lib_root) ? 0
             : tool_error(CINT_EXIT_ENVIRONMENT, NULL, 0, NULL, "cannot open the standard library root %s", g.stdlib);
    }
    char *self = cli_self_dir(), *dir = self != NULL ? join(self, "lib") : NULL;
    if (dir != NULL && !cint_bridge_root_open(dir, &lib_root)) lib_root = NULL;
    free(self);
    free(dir);
    return 0;
}

const char *module_key(const cli_module *m, char *buf, size_t n)
{
    if (m->where != 2) return m->rel;
    (void)snprintf(buf, n, "stdlib:%s", m->rel);
    return buf;
}

/* The module at `rel`, read under `root` when first named; an import of one segment that is
 * absent there is probed under the standard library root (RCPT-07). -1 when it cannot be
 * recorded. */
int module_at(cint_bridge_root *root, const char *rel, int import)
{
    cli_module *m;
    for (int k = 0; k < g.nmods; k++) {
        if (strcmp(g.mods[k].rel, rel) == 0) return k;
    }
    if (g.nmods == 2048 || (m = realloc(g.mods, (size_t)(g.nmods + 1) * sizeof *m)) == NULL) return -1;
    g.mods = m;
    m += g.nmods;
    memset(m, 0, sizeof *m);
    if ((m->rel = dup_s(rel)) == NULL) return -1;
    if (!cint_bridge_read_input(root, rel, 4194304u, &m->src, &m->len) && import && lib_root != NULL &&
        strchr(rel, '/') == NULL && cint_bridge_input_absent(root, rel)) {
        m->where = cint_bridge_read_input(lib_root, rel, 4194304u, &m->src, &m->len) ? 2 : 1;
    }
    if (m->src != NULL && digest(m->src, m->len, m->hex) != 0) return -1;
    return g.nmods++;
}

/* Follows the imports of module m: an optional profile line, then the imports, before any
 * other item (SPEC-04 18.3). `import a.b` names a/b.ci. The scan stops at the first other
 * token; an import it does not follow is the compiler's to report (C3009, C3030). */
int follow_imports(cint_bridge_root *root, int m)
{
    const uint8_t *s = g.mods[m].src;
    size_t n = g.mods[m].len, i = blank(s, n, 0u), k;
    if (word(s, n, i, "profile")) {
        i = blank(s, n, i + 7u);
        if (i >= n || s[i] != '"') return 0;
        for (i++; i < n && s[i] != '"'; i++) i += (size_t)(s[i] == '\\');
        i = blank(s, n, i + 1u);
        if (i >= n || s[i] != ';') return 0;
        i = blank(s, n, i + 1u);
    }
    while (word(s, n, i, "import")) {
        sbuf p = {0};
        int t, *deps;
        for (i = blank(s, n, i + 6u); (k = ident(s, n, i)) > 0u;) {
            sb_put(&p, s + i, k);
            i = blank(s, n, i + k);
            size_t j = i < n && s[i] == '.' ? blank(s, n, i + 1u) : n;
            if (ident(s, n, j) == 0u) break;
            sb_s(&p, "/");
            i = j;
        }
        while (i < n && s[i] != ';') i = blank(s, n, i + 1u);
        i = blank(s, n, i + 1u);
        if (p.n == 0u) return 0;
        sb_s(&p, ".ci");
        t = module_at(root, p.p, 1);
        free(p.p);
        if (t < 0 || (deps = realloc(g.mods[m].deps, (size_t)(g.mods[m].ndeps + 1) * sizeof *deps)) == NULL) return -1;
        g.mods[m].deps = deps;
        deps[g.mods[m].ndeps++] = t;
    }
    return 0;
}

/* Dependency order (D-13): each module after the modules its imports name, in source order.
 * A cycle is left in place for the compiler to report (C3003). */
static void place(int m)
{
    if (g.mods[m].mark != 0 || g.mods[m].src == NULL) return;
    g.mods[m].mark = 1;
    for (int k = 0; k < g.mods[m].ndeps; k++) place(g.mods[m].deps[k]);
    g.mods[m].mark = 2;
    g.order[g.norder++] = g.mods[m].rel;
}

/* The root module and, breadth first, every module its imports name. 1: the root is
 * unreadable; -1: a module cannot be recorded. */
int discover(cint_bridge_root *root)
{
    if (module_at(root, g.rel, 0) != 0) return -1;
    if (g.mods[0].src == NULL) return 1;
    for (int m = 0; m < g.nmods; m++) {
        if (g.mods[m].src != NULL && follow_imports(root, m) != 0) return -1;
    }
    if ((g.order = malloc((size_t)g.nmods * sizeof *g.order)) == NULL) return -1;
    place(0);
    g.src = g.mods[0].src;
    g.src_len = g.mods[0].len;
    return 0;
}

/* Whether every module discovery read reads back with the same bytes: through `root`, or
 * through `lib` (when given) for a standard library module. */
int same_inputs(cint_bridge_root *root, cint_bridge_root *lib)
{
    for (int k = 0; k < g.nmods; k++) {
        uint8_t *again = NULL;
        size_t n = 0u;
        cint_bridge_root *from = g.mods[k].where == 2 && lib != NULL ? lib : root;
        int same = g.mods[k].src == NULL || (cint_bridge_read_input(from, g.mods[k].rel, 4194304u, &again, &n) &&
                                            n == g.mods[k].len && memcmp(again, g.mods[k].src, n) == 0);
        cint_bridge_free_input(again);
        if (!same) return 0;
    }
    return 1;
}

static int by_rel(const void *a, const void *b)
{
    char ka[1200], kb[1200];
    return strcmp(module_key(*(const cli_module *const *)a, ka, sizeof ka),
                  module_key(*(const cli_module *const *)b, kb, sizeof kb));
}

/* The modules read (with `all`, also the absent lookups) in byte order of their receipt keys
 * (path byte order, SPEC-04 LS-234; a standard library module sorts under `stdlib:`). */
cli_module **sorted_modules(int all, int *count)
{
    cli_module **v = malloc((size_t)g.nmods * sizeof *v + 1u);
    *count = 0;
    for (int k = 0; v != NULL && k < g.nmods; k++) {
        if (all || g.mods[k].src != NULL) v[(*count)++] = &g.mods[k];
    }
    if (v != NULL) qsort(v, (size_t)*count, sizeof *v, by_rel);
    return v;
}

int snapshot_inputs(const char *dir, cint_bridge_root **snap)
{
    int ok = 1;
    for (int k = 0; ok && k < g.nmods; k++) {
        if (g.mods[k].src == NULL) continue;
        char *file = join(dir, g.mods[k].rel), *parent = dup_s(file);
        char *slash = strrchr(parent, SEP[0]), *forward = strrchr(parent, '/');
        if (forward != NULL && (slash == NULL || forward > slash)) slash = forward;
        *slash = '\0';
        ok = cli_mkdirs(parent) == 0 && cint_commit_output(file, g.mods[k].src, g.mods[k].len, 4194304u);
        free(file);
        free(parent);
    }
    return ok && cint_bridge_root_open(dir, snap) && same_inputs(*snap, NULL) ? 0 : -1;
}
