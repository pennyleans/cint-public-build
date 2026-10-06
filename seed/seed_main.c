/* seed_main.c: the command line of cint-seed (plan Task 1.5, SPEC-09 section 4).
 *
 *   cint-seed <module.ci> -o <out.c> [--sitemap <out.sites>] [--root <dir>]
 *
 * <module.ci> is the module-relative path (SPEC-09 CINTC-12): `/`-separated
 * segments, each an identifier (the last without `.ci`) that is not a Windows
 * device name, distinct from every other module path under ASCII case folding;
 * any other path is C3030 at <path>:1:1 (SPEC-04 LS-225, slice 2 patch D-2). It is
 * read from <dir>/<module.ci> with --root, else from the working directory, and
 * it names the module in every diagnostic, site and fault record, so the
 * generated source never depends on where the source lies (EMIT-21, EMIT-25). Imports
 * (`import a.b;`) are read from <dir>/a/b.ci. All imported modules are emitted
 * into the one C file, the root module first.
 *
 * Exit status: 0 with the C file (and the site map) written; 2 with one
 * diagnostic on standard error and nothing written (SEED-07).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cint_bridge.h"
#include "diag.h"
#include "tables.h"

static cint_bridge_root *g_root;   /* the source root, opened once (CINTC-15) */

static const char *usage = "usage: cint-seed <module.ci> -o <out.c> [--sitemap <out.sites>] [--root <dir>]";

static char *dup_str(const char *s, size_t n)
{
    char *p = seed_alloc(n + 1u, "path");
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

/* CINTC-12: a relative path of clean segments ending in `.ci`. */
static char *check_path(const char *in)
{
    size_t n = strlen(in), i, seg = 0;
    char *p = dup_str(in, n);
    for (i = 0; i < n; i++) {
        char c = p[i] == '\\' ? '/' : p[i];
        p[i] = c;
        if (c == ':') {
            diag_fatal("%s: a module path is relative to the source root (SPEC-09 CINTC-12); use --root", in);
        }
    }
    for (i = 0; i <= n; i++) {
        if (i == n || p[i] == '/') {
            size_t len = i - seg;
            if (len == 0u || (len == 1u && p[seg] == '.') || (len == 2u && p[seg] == '.' && p[seg + 1] == '.')) {
                diag_fatal("%s: a module path is relative to the source root, with no empty, `.` or `..` segment "
                           "(SPEC-09 CINTC-12); use --root for the directory", in);
            }
            seg = i + 1;
        }
    }
    if (n >= 5u && strcmp(p + n - 5, ".cint") == 0) {
        diag_fatal("%s: error C1001: a .cint file belongs to the legacy profile cint-bt27-legacy (SPEC-04 LS-3)", in);
    }
    if (n < 4u || strcmp(p + n - 3, ".ci") != 0) {
        diag_fatal("%s: a module file has the extension .ci (SPEC-04 LS-3)", in);
    }
    return p;
}

/* C3030 (SPEC-09 CINTC-12, SPEC-04 LS-225): every segment, the last without
 * `.ci`, is an identifier and not a device name, and no two module paths are
 * equal under ASCII case folding. Bytes outside printable ASCII print as `?`. */
static bool device_name(const char *g, size_t len)
{
    static const char dev[6][4] = {"CON", "PRN", "AUX", "NUL", "COM", "LPT"};
    size_t k;
    for (k = 0; k < 6u; k++) {
        if (((len == 3u && k < 4u) || (len == 4u && k >= 4u && g[3] >= '1' && g[3] <= '9')) &&
            (g[0] & ~0x20) == dev[k][0] && (g[1] & ~0x20) == dev[k][1] && (g[2] & ~0x20) == dev[k][2]) {
            return true;
        }
    }
    return false;
}

static void check_module_path(const char *rel)
{
    const char *rule = NULL, *ident = "every segment of a module path, without the final .ci, is an identifier "
                                      "(SPEC-04 LS-15)";
    char shown[1100];
    size_t n = strlen(rel) - 3u, seg = 0, i;
    int32_t j;
    for (i = 0; i <= n && rule == NULL; i++) {
        if (i < n && rel[i] != '/') {
            bool digit = rel[i] >= '0' && rel[i] <= '9';
            if (!((rel[i] >= 'a' && rel[i] <= 'z') || (rel[i] >= 'A' && rel[i] <= 'Z') || digit || rel[i] == '_') ||
                (digit && i == seg)) {
                rule = ident;
            }
            continue;
        }
        if (i == seg || i - seg > 255u) {
            rule = ident;
        } else if (device_name(rel + seg, i - seg)) {
            rule = "a segment of a module path is not a Windows device name";
        }
        seg = i + 1u;
    }
    for (j = 0; j < S.nmod && rule == NULL; j++) {
        for (i = 0; rel[i] != 0 && (rel[i] | 0x20) == (S.mods[j].path[i] | 0x20); i++) {
        }
        if (rel[i] == 0 && S.mods[j].path[i] == 0) {
            rule = "two module paths of one program are equal under ASCII case folding";
        }
    }
    if (rule != NULL) {
        for (i = 0; rel[i] != 0 && i + 1u < sizeof shown; i++) {
            shown[i] = (char)(rel[i] >= 0x20 && rel[i] < 0x7f ? rel[i] : '?');
        }
        shown[i] = 0;
        fprintf(stderr, "%s:1:1: error C3030: %s (SPEC-09 CINTC-12)\n", shown, rule);
        exit(2);
    }
}

/* Loads, scans and parses one module; returns its index. The bridge reads it under the
 * source root by handle, and refuses a name that differs from the directory entry only in
 * case on every host (SPEC-09 CINTC-12, CINTC-15), so resolution never depends on whether
 * the host file system folds case. */
static int32_t load_module(char *rel, int32_t from, uint32_t tok)
{
    seed_module *m;
    uint8_t *src = NULL;
    size_t len = 0;
    int32_t mi;
    check_module_path(rel);
    GROW(S.mods, S.nmod, S.cap_mod, "module");
    mi = S.nmod++;
    m = &S.mods[mi];
    memset(m, 0, sizeof *m);
    m->path = rel;
    m->import_from = from;
    m->import_tok = tok;
    if (cint_bridge_read_input(g_root, rel, (size_t)INT32_MAX, &src, &len)) {
        m->src = src;
        m->srclen = len;
    }
    if (m->src == NULL) {
        if (from < 0) {
            diag_fatal("%s: cannot read the module file", rel);
        }
        diag_tok(from, tok, "C3009", "cannot read the module file %s", rel);
    }
    if (m->srclen > (size_t)INT32_MAX / 4u) {
        diag_fatal("%s: the module is larger than the seed's tables (C9001)", rel);
    }
    scan_module(mi);
    parse_module(mi);
    return mi;
}

static void write_file(const char *file, const char *data, size_t len)
{
    FILE *f = fopen(file, "wb");
    if (f == NULL || fwrite(data, 1u, len, f) != len || fclose(f) != 0) {
        diag_fatal("%s: cannot write the file", file);
    }
}

int main(int argc, char **argv)
{
    const char *in = NULL, *out = NULL, *map = NULL;
    char *text, *sites;
    size_t len, map_len;
    int32_t i, k;
    for (i = 1; i < argc; i++) {
        bool opt = i + 1 < argc;
        if (strcmp(argv[i], "-o") == 0 && opt) {
            out = argv[++i];
        } else if (strcmp(argv[i], "--sitemap") == 0 && opt) {
            map = argv[++i];
        } else if (strcmp(argv[i], "--root") == 0 && opt) {
            S.root = argv[++i];
        } else if (argv[i][0] != '-' && in == NULL) {
            in = argv[i];
        } else {
            diag_fatal("%s", usage);
        }
    }
    if (in == NULL || out == NULL) {
        diag_fatal("%s", usage);
    }
    if (!cint_bridge_root_open(S.root != NULL ? S.root : ".", &g_root)) {
        diag_fatal("%s: cannot open the source root", S.root != NULL ? S.root : ".");
    }
    load_module(check_path(in), -1, 0);
    for (i = 0; i < S.nmod; i++) {
        for (k = 0; k < S.mods[i].nimport; k++) {
            seed_module *m = &S.mods[i];
            seed_import *im = &m->imports[k];
            size_t n = 0;
            uint32_t t;
            int32_t j;
            char *rel = seed_alloc((size_t)m->toks[im->tok + im->ntok - 1].off + m->toks[im->tok + im->ntok - 1].len + 8u, "path");
            for (t = im->tok; t < im->tok + im->ntok; t++) {
                const seed_tok *tk = &m->toks[t];
                if (tk->kind == T_OP) {
                    rel[n++] = '/';
                } else {
                    memcpy(rel + n, m->src + tk->off, tk->len);
                    n += tk->len;
                }
            }
            memcpy(rel + n, ".ci", 4u);
            im->module = -1;
            for (j = 0; j < S.nmod; j++) {
                if (strcmp(S.mods[j].path, rel) == 0) {
                    im->module = j;
                }
            }
            if (im->module < 0) {
                im->module = load_module(rel, i, im->tok);
            } else {
                free(rel);
            }
        }
    }
    resolve_program();
    check_program();
    reach_program();
    text = emit_program(&len, &sites, &map_len);
    write_file(out, text, len);
    if (map != NULL) {
        write_file(map, sites, map_len);
    }
    free(text);
    free(sites);
    return 0;
}
