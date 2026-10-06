/* cint_main.c: the `cint` command: `cint run` (slice 2 task 2.12), `cint build` and `cint test`
 * (task 2.15), and `cint emit-c` (task 2.12a).
 *
 *   cint run [--json] [--receipt PATH] [--root DIR] [--toolchain FILE] [--cache DIR] FILE.ci
 *   cint build [--json] [--receipt PATH] [--root DIR] [--out DIR] [--toolchain FILE] [--cache DIR] [FILE.ci | DIR]
 *   cint test [--json] [--receipt PATH] [--root DIR] [--toolchain FILE] [--cache DIR] [FILE.ci | DIR]
 *
 * Each reads FILE.ci through the bridge reader (rt/cint_bridge.h; the identity comes from the
 * reading handle) and, breadth first, every module its imports name (SPEC-06 3.3): an import
 * `a.b` names a/b.ci under the source root, and a path no file answers is kept as an absent
 * lookup (SPEC-09 RCPT-07), for the compiler to report. The modules go to the compiler linked
 * into this executable (B1, compiler/main.ci built from S1) in dependency order, the root last,
 * through plan, compile, measure and emit per module (cint_build; SPEC-09 CINTC-05, decision
 * patch D-13), which commits the generated source to a cache directory; the host C compiler
 * builds it with the EMIT-26 flags. `cint run` runs it with the SPEC-06 1.4 defaults for
 * `cint run` (unbounded fuel, depth 256, frame arena 16 MiB: the generated main calls
 * cint_program_run) and maps its outcome to an exit status (SPEC-06 3.2, 3.4a; D-19, decision 23):
 *
 *   0 completed; 1 runtime fault; 2 compile error; 4 usage error; 5 environment failure
 *   (no host C compiler, an unreadable or refused input, output not writable); 6 `main` left
 *   with an error result (rt/OPEN.md RT-OQ-33); 7 internal error (a fault inside cintc, the
 *   host C compiler rejecting or warning on generated source, a program that ends without a
 *   CINT status, an unreadable fault, error or fuel record). 3 is not reachable here.
 *
 * `cint build` writes the program to --out DIR (default: build under the project directory) as
 * the root module's name without .ci, and runs nothing. `cint test` runs every test block of the
 * build (SPEC-04 LS-232 to LS-240, SPEC-06 3.5): modules in path byte order, tests in source
 * order, each in its own process with a fresh context and module state and fuel 1,000,000,000
 * (SPEC-06 1.4), through a driver that calls the compiler's cg<P>_test<j> entries; it prints one
 * line per test and `<k> of <n> tests passed` on stdout (under --json, one CINT-TEST-1 object per
 * test) and exits 3 when a test fails, an error that leaves a test included (LS-315). A DIR
 * target is a project directory: its main.ci, under DIR/src when that is a directory (SPEC-06
 * 3.3, Proposed layout).
 *
 * Channels (SPEC-06 3.4a). stdout carries only the program's bytes, in binary mode,
 * flushed before a fault is reported and before exit. stderr carries the diagnostics in
 * SPEC-04 LS-276 form, the fault in SPEC-06 8.4 form, the error result of `main` as one line
 * `error: <set>.<value> (tag <n>)`, and this tool's own errors; with --json every stderr line is
 * one canonical JSON object: CINT-DIAG-1, CINT-FAULT-1, CINT-ERROR-1, or CINT-TOOL-1
 * (SPEC-06 15). The host C compiler's output goes to a log in the cache directory. A receipt
 * (kind `run`, `build` or `test`, D-16) is written only to --receipt PATH, followed by the line
 * `receipt: PATH` on stderr unless --json is given.
 *
 * Without --root, FILE's directory is the project root and its name the module path; with
 * --root DIR, FILE is the module path under DIR (CINTC-12). The toolchain file (default:
 * cint.toolchain beside this executable) is written by tools/cint_bootstrap.py.
 * `cint emit-c [--json] --root DIR --out DIR [--toolchain FILE] MODULE.ci...` (task 2.12a) builds
 * the modules, given in dependency order (D-13; the root last), through cint_build and commits
 * the output set under --out (C per module, the D-2 program file, MANIFEST; <out>/MANIFEST.ref).
 * No stdout; each diagnostic names its module. Exit 0, 2, 4, 5 or 7 as above.
 *
 * Receipt and JSON writing live in cli/cint_receipt.c, the state both share in cli/cint_receipt.h
 * (decision 26). */
#define _CRT_SECURE_NO_WARNINGS 1
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cint_bridge.h"
#include "cint_proc.h"
#include "cint_receipt.h"

extern const cint_module_info cm_4_main;
cint_status cx_4_main_6_digest(cint_ctx *ctx, int64_t fuel, cint_view p_data, cint_view p_hash, int64_t *result);

int json_mode;

/* ------------------------------------------------------------------------- */
/* State.                                                                     */

cli_state g;

cint_compiler_diag g_diag[CINT_BRIDGE_DIAG_ROWS];
static uint8_t g_faults[CINT_BRIDGE_FAULT_BYTES];
static char *g_diag_paths[CINT_BRIDGE_DIAG_ROWS];

static bool keep_diag_path(void *user, int64_t row, const uint8_t *bytes, size_t len)
{
    char **paths = user;
    if (row < 1 || row >= CINT_BRIDGE_DIAG_ROWS || len == SIZE_MAX || memchr(bytes, 0, len) != NULL) return false;
    char *copy = malloc(len + 1);
    if (copy == NULL) return false;
    memcpy(copy, bytes, len);
    copy[len] = '\0';
    free(paths[row]);
    paths[row] = copy;
    return true;
}

static void free_diag_paths(void)
{
    for (int i = 0; i < CINT_BRIDGE_DIAG_ROWS; i++) {
        free(g_diag_paths[i]);
        g_diag_paths[i] = NULL;
    }
}

static const char USAGE[] =
    "usage: cint [--json] run [--receipt PATH] [--root DIR] [--toolchain FILE] [--cache DIR] FILE.ci\n"
    "       cint [--json] build [--receipt PATH] [--root DIR] [--out DIR] [--lib] [--toolchain FILE] [--cache DIR] [FILE.ci | DIR]\n"
    "       cint [--json] test [--receipt PATH] [--root DIR] [--toolchain FILE] [--cache DIR] [FILE.ci | DIR]\n"
    "       cint [--json] emit-c --root DIR --out DIR [--toolchain FILE] MODULE.ci...\n";

static int usage(const char *why)
{
    if (!json_mode) {
        fputs(USAGE, stderr);
    }
    return tool_error(CINT_EXIT_USAGE, NULL, 0, NULL, "%s", why);
}

int read_file(const char *path, uint8_t **bytes, size_t *len)
{
    FILE *f = cli_fopen(path, "rb");
    sbuf b = {0};
    char buf[65536];
    size_t got;
    if (f == NULL) {
        return -1;
    }
    sb_put(&b, "", 0u);
    while ((got = fread(buf, 1, sizeof buf, f)) > 0u) {
        sb_put(&b, buf, got);
    }
    if (ferror(f)) {
        fclose(f);
        free(b.p);
        return -1;
    }
    fclose(f);
    *bytes = (uint8_t *)b.p;
    *len = b.n;
    return 0;
}

static char *dup_s(const char *s)
{
    char *d = malloc(strlen(s) + 1u);
    if (d != NULL) {
        memcpy(d, s, strlen(s) + 1u);
    }
    return d;
}

char *join(const char *a, const char *b)
{
    sbuf s = {0};
    sb_s(&s, a);
    sb_s(&s, SEP);
    sb_s(&s, b);
    return s.p;
}

const char *basename_utf8(const char *path)
{
    const char *name = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') name = p + 1;
    }
    return name;
}

static int by_basename(const void *a, const void *b)
{
    return strcmp(basename_utf8(*(char *const *)a), basename_utf8(*(char *const *)b));
}

/* The toolchain file: `cint-toolchain-1`, then `key value` lines. */
static int load_toolchain(const char *path)
{
    uint8_t *bytes;
    size_t len;
    char *line, *next;
    toolchain *t = &g.tc;
    if (read_file(path, &bytes, &len) != 0 || len < 16u || memcmp(bytes, "cint-toolchain-1\n", 16) != 0) {
        return -1;
    }
    for (line = (char *)bytes + 16; *line != '\0'; line = next) {
        char *sp, *val;
        next = strchr(line, '\n');
        if (next == NULL) {
            next = line + strlen(line);
        } else {
            *next++ = '\0';
        }
        if ((sp = strchr(line, ' ')) == NULL) {
            continue;
        }
        *sp = '\0';
        val = sp + 1;
        if (strcmp(line, "flag") == 0 && t->nflags < 64u) {
            t->flags[t->nflags++] = val;
        } else if (strcmp(line, "runtime_source") == 0 && t->nrt < 8u) {
            t->rt_sources[t->nrt++] = val;
        } else if (strcmp(line, "env") == 0 && strchr(val, '=') != NULL) {
            char *eq = strchr(val, '=');
            *eq = '\0';
            if (cli_setenv(val, eq + 1) != 0) {
                return -1;
            }
        } else {
            static const char *const keys[] = {"leg", "cc", "cc_version", "include", "runtime_object", "cache", "host",
                                               "compiler_sources", "executable_sha256", "runtime_library_object"};
            char **slots[] = {&t->leg, &t->cc, &t->cc_version, &t->include, &t->runtime_object, &t->cache, &t->host,
                              &t->compiler_sources, &t->executable_sha256, &t->runtime_library_object};
            for (size_t k = 0; k < sizeof keys / sizeof keys[0]; k++) {
                if (strcmp(line, keys[k]) == 0) {
                    *slots[k] = val;
                }
            }
        }
    }
    qsort(t->rt_sources, t->nrt, sizeof t->rt_sources[0], by_basename);
    return t->leg != NULL && t->cc != NULL && t->include != NULL && t->runtime_object != NULL && t->cache != NULL &&
                   t->compiler_sources != NULL && t->executable_sha256 != NULL &&
                   strlen(t->executable_sha256) == 64u && t->nrt > 0u && t->nflags > 0u
               ? 0
               : -1;
}

/* ------------------------------------------------------------------------- */
/* Digests: the `digest` export of compiler/main.ci (compiler/sha256.ci).     */

static cint_view u8_view(void *p, int64_t n, uint8_t perm)
{
    cint_view v;
    memset(&v, 0, sizeof v);
    if (cint_buffer_register_bytes(g.ctx, n > 0 ? p : NULL, n, perm, &v.buffer) != CINT_OK) {
        v.buffer = 0u;
    }
    v.generation = CINT_BUFFER_GENERATION_FIRST;
    v.type.code = CINT_TAG_U8;
    v.rank = 1u;
    v.perm = perm;
    v.shape[0] = n;
    v.stride[0] = 1;
    return v;
}

int digest(const uint8_t *data, size_t n, char hex[65])
{
    uint8_t hash[32];
    int64_t result = -1;
    cint_view vd = u8_view((void *)(uintptr_t)data, (int64_t)n, CINT_VIEW_READ);
    cint_view vh = u8_view(hash, 32, CINT_VIEW_WRITE);
    cint_status st = cx_4_main_6_digest(g.ctx, CINT_FUEL_UNBOUNDED, vd, vh, &result);
    (void)cint_buffer_release(g.ctx, vd.buffer);
    (void)cint_buffer_release(g.ctx, vh.buffer);
    if (st != CINT_OK || result != 0) return -1;
    for (int i = 0; i < 32; i++) {
        (void)snprintf(hex + 2 * i, 3, "%02x", (unsigned)hash[i]);
    }
    return 0;
}

/* A file is hashed with the runtime's SHA-256, as the bridge hashes MANIFEST: through the
 * `digest` export, the check of this executable in ready() took 3.4 s of every command. */
int digest_file(const char *path, char hex[65])
{
    uint8_t *bytes, hash[32];
    size_t len;
    if (read_file(path, &bytes, &len) != 0) return -1;
    cint_sha256(bytes, len, hash);
    free(bytes);
    for (int i = 0; i < 32; i++) (void)snprintf(hex + 2 * i, 3, "%02x", (unsigned)hash[i]);
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Discovery by imports (SPEC-06 3.3, SPEC-09 RCPT-07).                       */

/* Blanks and comments from s[i] (block comments nest, SPEC-04 LS-14). */
static size_t blank(const uint8_t *s, size_t n, size_t i)
{
    for (;;) {
        if (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) {
            i++;
        } else if (i + 1u < n && s[i] == '/' && s[i + 1u] == '/') {
            while (i < n && s[i] != '\n' && s[i] != '\r') i++;
        } else if (i + 1u < n && s[i] == '/' && s[i + 1u] == '*') {
            size_t depth = 0u;
            do {
                if (i + 1u >= n) return n;
                if (s[i] == '/' && s[i + 1u] == '*') { depth++; i += 2u; }
                else if (s[i] == '*' && s[i + 1u] == '/') { depth--; i += 2u; }
                else i++;
            } while (depth > 0u);
        } else {
            return i;
        }
    }
}

/* The length of the ASCII identifier at s[i]; 0 for none, and when a non-ASCII byte continues it. */
static size_t ident(const uint8_t *s, size_t n, size_t i)
{
    size_t k = i;
    while (k < n && (s[k] == '_' || ((s[k] | 32u) >= 'a' && (s[k] | 32u) <= 'z') || (k > i && s[k] >= '0' && s[k] <= '9'))) k++;
    return k < n && s[k] >= 0x80u ? 0u : k - i;
}

static int word(const uint8_t *s, size_t n, size_t i, const char *w)
{
    return ident(s, n, i) == strlen(w) && memcmp(s + i, w, strlen(w)) == 0;
}

/* The module at `rel`, read under `root` when first named; -1 when it cannot be recorded. */
static int module_at(cint_bridge_root *root, const char *rel)
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
    if (cint_bridge_read_input(root, rel, 4194304u, &m->src, &m->len) && digest(m->src, m->len, m->hex) != 0) return -1;
    return g.nmods++;
}

/* Follows the imports of module m: an optional profile line, then the imports, before any
 * other item (SPEC-04 18.3). `import a.b` names a/b.ci. The scan stops at the first other
 * token; an import it does not follow is the compiler's to report (C3009, C3030). */
static int follow_imports(cint_bridge_root *root, int m)
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
        t = module_at(root, p.p);
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
static int discover(cint_bridge_root *root)
{
    if (module_at(root, g.rel) != 0) return -1;
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

/* Whether every module discovery read reads back through `root` with the same bytes. */
static int same_inputs(cint_bridge_root *root)
{
    for (int k = 0; k < g.nmods; k++) {
        uint8_t *again = NULL;
        size_t n = 0u;
        int same = g.mods[k].src == NULL || (cint_bridge_read_input(root, g.mods[k].rel, 4194304u, &again, &n) &&
                                            n == g.mods[k].len && memcmp(again, g.mods[k].src, n) == 0);
        cint_bridge_free_input(again);
        if (!same) return 0;
    }
    return 1;
}

static int by_rel(const void *a, const void *b)
{
    return strcmp((*(const cli_module *const *)a)->rel, (*(const cli_module *const *)b)->rel);
}

/* The modules read (with `all`, also the absent lookups) in path byte order (SPEC-04 LS-234). */
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

/* ------------------------------------------------------------------------- */
/* Canonical fault records (SPEC-01 IM-149 v2, run-time kind).                */

const char *const CODE_NAMES[14] = {"", "E_OVERFLOW", "E_DIV_ZERO", "E_BOUNDS", "E_SHAPE", "E_SHIFT",
                                    "E_NARROW", "E_ALIAS", "E_STALE_HANDLE", "E_FUEL", "E_UNSUPPORTED",
                                    "E_DOMAIN", "E_DEPTH", "E_ASSERT"};
static const char *const CODE_TEXT[14] = {"", "the exact result is outside the type's range", "division by zero",
                                          "index out of bounds", "shapes do not match", "shift count outside 0..w-1",
                                          "the value does not fit the destination type", "views overlap",
                                          "stale handle", "fuel allowance exhausted",
                                          "not supported by this backend", "operand outside the operation's domain",
                                          "call depth limit exceeded", "assertion failed"};

typedef struct rd {
    const uint8_t *p;
    size_t n, at;
    int bad;
} rd;

static uint64_t rd_u(rd *r, unsigned k)
{
    uint64_t v = 0;
    if (r->bad || r->n - r->at < k) {
        r->bad = 1;
        return 0;
    }
    for (unsigned i = 0; i < k; i++) {
        v |= (uint64_t)r->p[r->at + i] << (8u * i);
    }
    r->at += k;
    return v;
}

static int rd_flag(rd *r)
{
    uint64_t v = rd_u(r, 1);
    if (v > 1u) r->bad = 1;
    return (int)v;
}

static const uint8_t *rd_take(rd *r, uint64_t k)
{
    const uint8_t *p = r->p + r->at;
    if (r->bad || r->n - r->at < k) {
        r->bad = 1;
        return NULL;
    }
    r->at += (size_t)k;
    return p;
}

static void rd_value(rd *r, fvalue *v, unsigned zmax)
{
    size_t start = r->at;
    unsigned tag = (unsigned)rd_u(r, 1);
    if (tag == CINT_TAG_Z) {
        uint64_t n = rd_u(r, 4);
        if (n > zmax) r->bad = 1;
        const uint8_t *p = rd_take(r, n);
        if (p != NULL && ((n == 1u && p[0] == 0u) ||
            (n >= 2u && ((p[n - 1u] == 0u && p[n - 2u] < 0x80u) ||
                        (p[n - 1u] == 0xffu && p[n - 2u] >= 0x80u))))) {
            r->bad = 1;
        }
    } else if (tag == CINT_TAG_BOOL || (tag >= 0x11u && tag <= 0x18u) ||
               (tag >= 0x21u && tag <= 0x28u)) {
        const uint8_t *p = rd_take(r, tag == CINT_TAG_BOOL ? 1u : 1u << ((tag & 0x0Fu) - 1u));
        if (p != NULL && tag == CINT_TAG_BOOL && *p > 1u) r->bad = 1;
    } else {
        r->bad = 1;
    }
    if (!r->bad) {
        v->len = (uint16_t)(r->at - start);
        memcpy(v->bytes, r->p + start, r->at - start);
    }
}

static int valid_utf8(const char *s, size_t n)
{
    if (s == NULL) return 0;
    for (size_t i = 0; i < n;) {
        unsigned c = (unsigned char)s[i++], u, k;
        if (c < 0x80u) continue;
        if (c >= 0xc2u && c <= 0xdfu) { k = 1u; u = c & 0x1fu; }
        else if (c >= 0xe0u && c <= 0xefu) { k = 2u; u = c & 0x0fu; }
        else if (c >= 0xf0u && c <= 0xf4u) { k = 3u; u = c & 7u; }
        else return 0;
        if (n - i < k) return 0;
        for (unsigned j = 0; j < k; j++) {
            unsigned d = (unsigned char)s[i++];
            if ((d & 0xc0u) != 0x80u) return 0;
            u = (u << 6) | (d & 0x3fu);
        }
        if ((k == 2u && u < 0x800u) || (k == 3u && u < 0x10000u) ||
            (u >= 0xd800u && u <= 0xdfffu) || u > 0x10ffffu) return 0;
    }
    return 1;
}

static void rd_pos(rd *r, fpos *p)
{
    p->path_len = (uint32_t)rd_u(r, 4);
    if (p->path_len == 0u || p->path_len > CINT_FAULT_MAX_PATH) r->bad = 1;
    p->path = (const char *)rd_take(r, p->path_len);
    if (!valid_utf8(p->path, p->path_len)) r->bad = 1;
    p->line = (uint32_t)rd_u(r, 4);
    p->column = (uint32_t)rd_u(r, 4);
    if (p->line == 0u || p->column == 0u) r->bad = 1;
}

/* Trailing bytes are allowed only in a compile-time slot of the faults table. */
int decode_fault(const uint8_t *data, size_t n, frec *f, int trailing)
{
    static const char domain[] = "cint-core-1/fault/v2";
    rd r = {data, n, 0, 0};
    uint64_t len;
    const uint8_t *s;
    memset(f, 0, sizeof *f);
    len = rd_u(&r, 4);
    s = rd_take(&r, len);
    if (s == NULL || len != sizeof domain - 1u || memcmp(s, domain, sizeof domain - 1u) != 0) {
        return -1;
    }
    f->code = (unsigned)rd_u(&r, 2);
    len = rd_u(&r, 4);
    s = rd_take(&r, len);
    if (f->code == 0u || f->code > 13u || s == NULL || len == 0u || len > 64u) {
        return -1;
    }
    memcpy(f->op, s, (size_t)len);
    for (size_t i = 0; i < len; i++) {
        unsigned c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_')) {
            return -1;
        }
    }
    f->nop = (uint32_t)rd_u(&r, 4);
    if (f->nop > CINT_FAULT_MAX_OPERANDS) {
        return -1;
    }
    for (uint32_t i = 0; i < f->nop; i++) {
        rd_value(&r, &f->opnd[i], trailing ? 513u : 257u);
    }
    if ((f->has_exact = rd_flag(&r)) != 0) {
        rd_value(&r, &f->exact, trailing ? 520u : 257u);
        if (f->exact.bytes[0] != CINT_TAG_Z) r.bad = 1;
    }
    if ((f->has_limit = rd_flag(&r)) != 0) {
        rd_value(&r, &f->limit, 257u);
    }
    rd_pos(&r, &f->pos);
    if ((f->has_rev = rd_flag(&r)) != 0 && (s = rd_take(&r, 32)) != NULL) {
        memcpy(f->rev, s, 32);
    }
    if ((f->has_map = rd_flag(&r)) != 0 && (s = rd_take(&r, 32)) != NULL) {
        memcpy(f->map, s, 32);
    }
    if ((f->has_addr = rd_flag(&r)) != 0) {
        f->kernel_len = (uint32_t)rd_u(&r, 4);
        f->kernel = (const char *)rd_take(&r, f->kernel_len);
        if (!valid_utf8(f->kernel, f->kernel_len)) r.bad = 1;
        uint64_t u = rd_u(&r, 8);
        memcpy(&f->dispatch, &u, sizeof u);
        f->phase = (uint32_t)rd_u(&r, 1);
        if (f->phase > 2u) r.bad = 1;
        if ((f->has_item = rd_flag(&r)) != 0) {
            if (f->phase != 1u) r.bad = 1;
            u = rd_u(&r, 8);
            memcpy(&f->item, &u, sizeof u);
            u = rd_u(&r, 8);
            memcpy(&f->step, &u, sizeof u);
        }
    }
    f->nstack = (uint32_t)rd_u(&r, 4);
    if (f->nstack > CINT_RT_MAX_DEPTH) {
        return -1;
    }
    for (uint32_t i = 0; i < f->nstack; i++) {
        rd_pos(&r, &f->stack[i]);
    }
    return r.bad || (r.at != r.n && !trailing) ? -1 : 0;
}

/* "I64 42" -> {"t":"I64","v":"42"} */
size_t value_text(const fvalue *v, char text[1700], int with_type)
{
    unsigned tag = v->bytes[0];
    size_t n = v->len - 1u, off = 1u, at = 0u, nd = 0u;
    uint8_t mag[520];
    char digits[1600];
    int negative;
    if (tag == CINT_TAG_BOOL) {
        (void)snprintf(text, 1700, "%s%s", with_type ? "Bool " : "", v->bytes[1] ? "true" : "false");
        return strlen(text);
    }
    if (tag == CINT_TAG_Z) {
        n = v->len - 5u;
        off = 5u;
        if (with_type) {
            memcpy(text, "Z ", 2u);
            at = 2u;
        }
    } else if (with_type) {
        int k = snprintf(text, 1700, "%c%u ", tag < 0x20u ? 'I' : 'U', 8u << ((tag & 15u) - 1u));
        at = (size_t)k;
    }
    negative = n > 0u && (tag == CINT_TAG_Z || tag < 0x20u) && (v->bytes[off + n - 1u] & 0x80u) != 0u;
    memcpy(mag, v->bytes + off, n);
    if (negative) {
        unsigned carry = 1u;
        for (size_t i = 0; i < n; i++) {
            unsigned u = (unsigned)(uint8_t)~mag[i] + carry;
            mag[i] = (uint8_t)u;
            carry = u >> 8;
        }
        text[at++] = '-';
    }
    for (int more = 1; more;) {
        unsigned rem = 0u;
        more = 0;
        for (size_t i = n; i > 0u; i--) {
            unsigned u = rem * 256u + mag[i - 1u];
            mag[i - 1u] = (uint8_t)(u / 10u);
            rem = u % 10u;
            more = more || mag[i - 1u] != 0u;
        }
        digits[nd++] = (char)('0' + rem);
    }
    while (nd > 0u) {
        text[at++] = digits[--nd];
    }
    text[at] = '\0';
    return at;
}

/* ------------------------------------------------------------------------- */
/* Human forms (SPEC-04 LS-276, SPEC-06 8.4).                                 */

/* The source excerpt of LS-276 when `path` is a module read: the line, then carets
 * under the column (tabs kept, so they align at any tab width), then `label`. */
static void excerpt(sbuf *b, const char *path, size_t path_len, uint32_t line, uint32_t column, const char *label)
{
    const cli_module *m = NULL;
    for (int i = 0; i < g.nmods; i++) {
        if (g.mods[i].src != NULL && strlen(g.mods[i].rel) == path_len && memcmp(g.mods[i].rel, path, path_len) == 0) m = &g.mods[i];
    }
    if (m == NULL || line == 0u) {
        return;
    }
    const char *s = (const char *)m->src, *end = s + m->len, *e;
    uint32_t k = 1, col = 1;
    int w = line >= 100000u ? 6 : line >= 10000u ? 5 : line >= 1000u ? 4 : line >= 100u ? 3 : 2;
    while (s < end && k < line) {
        if (*s++ == '\n') {
            k++;
        }
    }
    if (k != line || s > end) {
        return;
    }
    for (e = s; e < end && *e != '\n' && *e != '\r'; e++) {
    }
    sb_f(b, "%*s |\n%*u | ", w, "", w, (unsigned)line);
    sb_put(b, s, (size_t)(e - s));
    sb_f(b, "\n%*s | ", w, "");
    for (; s < e && col < column; s++) {
        if (((unsigned char)*s & 0xC0u) == 0x80u) {
            continue;  /* a continuation byte: columns count scalar values (DIAG-01) */
        }
        sb_s(b, *s == '\t' ? "\t" : " ");
        col++;
    }
    sb_f(b, "^%s%s\n%*s |\n", label[0] != '\0' ? " " : "", label, w, "");
}

static void human_fault(sbuf *b, const frec *f, int64_t fuel)
{
    char text[1700];
    sb_f(b, "fault[%s]: %s\n  --> ", CODE_NAMES[f->code], CODE_TEXT[f->code]);
    sb_put(b, f->pos.path, f->pos.path_len);
    sb_f(b, ":%u:%u\n", (unsigned)f->pos.line, (unsigned)f->pos.column);
    excerpt(b, f->pos.path, f->pos.path_len, f->pos.line, f->pos.column, f->op);
    sb_f(b, "   = operation: %s\n", f->op);
    sb_f(b, "   = operand-count: %u\n", (unsigned)f->nop);
    for (uint32_t i = 0; i < f->nop; i++) {
        (void)value_text(&f->opnd[i], text, 1);
        sb_f(b, "   = operand: %s\n", text);
    }
    if (f->has_exact) {
        (void)value_text(&f->exact, text, 0);
        sb_f(b, "   = exact:   %s\n", text);
    } else sb_s(b, "   = exact:   none\n");
    if (f->has_limit) {
        (void)value_text(&f->limit, text, 1);
        sb_f(b, "   = limit:   %s\n", text);
    } else sb_s(b, "   = limit:   none\n");
    sb_s(b, "   = revision: ");
    if (f->has_rev) {
        sb_hex(b, f->rev, 32);
    } else sb_s(b, "none");
    sb_s(b, "\n   = source-map: ");
    if (f->has_map) {
        sb_hex(b, f->map, 32);
    } else sb_s(b, "none");
    sb_s(b, "\n   = address: ");
    if (f->has_addr) {
        sb_put(b, f->kernel, f->kernel_len);
        sb_f(b, ", dispatch %lld, phase %u", (long long)f->dispatch, (unsigned)f->phase);
        if (f->has_item) sb_f(b, ", work-item %lld, step %lld", (long long)f->item, (long long)f->step);
        else sb_s(b, ", work-item none, step none");
    } else sb_s(b, "none");
    sb_f(b, "\n   = stack-depth: %u\n", (unsigned)f->nstack);
    for (uint32_t i = 0; i < f->nstack; i++) {
        sb_s(b, "   = called from: ");
        sb_put(b, f->stack[i].path, f->stack[i].path_len);
        sb_f(b, ":%u:%u\n", (unsigned)f->stack[i].line, (unsigned)f->stack[i].column);
    }
    if (fuel >= 0) {
        sb_f(b, "   = fuel-consumed: %lld (not part of the fault record)\n", (long long)fuel);
    }
    sb_s(b, g.mode == MODE_TEST ? "   = state: the test stopped; its output is not shown\n"
                                : "   = state: the program stopped; stdout holds what it wrote before the fault\n");
}

/* Messages of the SPEC-04 17.2 codes (LS-313); message text is not compared (SPEC-09 9.2). */
static const struct {
    int lo, hi;
    const char *text;
} DIAG_TEXT[] = {
    {1001, 1001, "`.cint` file (legacy profile)"}, {1002, 1005, "invalid source bytes"},
    {1010, 1013, "invalid name"}, {1020, 1024, "invalid integer literal"}, {1036, 1037, "invalid format specification"},
    {1030, 1039, "invalid character or string literal"}, {1040, 1040, "empty statement"},
    {1041, 1041, "assignment in a condition"}, {1042, 1042, "`++` or `--` used as an expression"},
    {1043, 1043, "missing parentheses or braces"}, {1050, 1050, "syntax error"},
    {2001, 2001, "type mismatch"}, {2002, 2002, "condition is not `Bool`"},
    {2003, 2003, "literal does not fit its type"}, {2008, 2008, "no explicit conversion between the two types"},
    {2004, 2009, "fraction, fixed-point, enum literal, or rounding rule"}, {2010, 2014, "array rule"},
    {2020, 2022, "wrong fields or arguments"}, {2050, 2050, "uninitialized variable"},
    {2052, 2052, "loop variable assigned"}, {2056, 2056, "integer used as `Bool`"},
    {2058, 2058, "not an assignable place"}, {2060, 2069, "parameter or argument rule"},
    {2101, 2101, "parentheses required"}, {2102, 2102, "chained comparison"},
    {2103, 2103, "operator not defined for the operand type"}, {2000, 2999, "type rule"},
    {3001, 3001, "duplicate name"}, {3004, 3004, "script local used in a function or test"},
    {3005, 3008, "undefined or unusable name"}, {3009, 3009, "module file unreadable"},
    {3010, 3011, "script rule"}, {3012, 3012, "`import` after other items"}, {3000, 3999, "name or module rule"},
    {4001, 4001, "missing return"}, {4012, 4012, "statement form not permitted"},
    {4020, 4026, "`switch` rule"}, {4040, 4040, "recursion in `cint-boot-1`"}, {4000, 4999, "control-flow rule"},
    {5000, 5999, "parameter mode, alias, or ABI rule"}, {6001, 6001, "fault during constant evaluation"},
    {6002, 6002, "compile-time fuel exhausted"}, {6004, 6004, "constant expression required"},
    {6000, 6999, "compile-time evaluation rule"}, {9001, 9001, "capacity or output size exceeded"},
    {9002, 9002, "generated symbol over 247 bytes"}, {9003, 9003, "local array over the frame limit"},
    {9004, 9004, "nesting beyond the implementation's limit"},
    {9102, 9102, "not yet supported by this implementation (SPEC-04 C9102)"},
    {9000, 9999, "implementation limit"},
};

static const char *diag_text(const cint_compiler_diag *d)
{
    int64_t code = d->code;
    if (code == 3030) {
        if (d->detail[0] == CINT_PATH_IDENTIFIER)
            return "every segment of a module path, without the final .ci, is an identifier "
                   "(SPEC-04 LS-15) (SPEC-09 CINTC-12)";
        if (d->detail[0] == CINT_PATH_DEVICE)
            return "a segment of a module path is not a Windows device name (SPEC-09 CINTC-12)";
        if (d->detail[0] == CINT_PATH_COLLISION)
            return "two module paths of one program are equal under ASCII case folding (SPEC-09 CINTC-12)";
    }
    if (code == 9001 && d->detail[0] == 31) {
        return "cpu-c17 record layout exceeds 4294967295 bytes (LIM_RECORD_BYTES)";
    }
    for (size_t i = 0; i < sizeof DIAG_TEXT / sizeof DIAG_TEXT[0]; i++) {
        if (code >= DIAG_TEXT[i].lo && code <= DIAG_TEXT[i].hi) {
            return DIAG_TEXT[i].text;
        }
    }
    return "compile error";
}

/* The module a diagnostic belongs to: its reported path, else its module in the build manifest
 * (emit-c's list, or the dependency order), else the root module. */
#define dfile(d) (g_diag_paths[(d) - g_diag] != NULL ? g_diag_paths[(d) - g_diag] : \
    (d)->module >= 0 && (d)->module < (g.emit ? g.nfiles : g.norder) ? \
    (g.emit ? (const char *)g.files[(d)->module] : g.order[(d)->module]) : g.rel)

/* The LS-312 note of C3004, C3010, C3011 and the module-level initializer's C6004: detail[2]
 * is 1 when detail[0..1] is the first top-level statement that makes the module a script, 2
 * when it has none, and 0 for a diagnostic without the note, such as any other C6004
 * (compiler/OPEN.md CINTC-OQ-14). */
static int script_note(const cint_compiler_diag *d, char *out, size_t cap)
{
    if ((d->code != 3004 && d->code != 3010 && d->code != 3011 && d->code != 6004) ||
        (d->detail[2] != 1 && d->detail[2] != 2)) {
        return 0;
    }
    if (d->detail[2] == 1) {
        (void)snprintf(out, cap, "this module is a script because of the top-level statement at %s:%lld:%lld", dfile(d),
                       (long long)d->detail[0], (long long)d->detail[1]);
    } else {
        (void)snprintf(out, cap, "this file is a module, not a script, because every top-level item is a "
                                 "declaration (LS-218)");
    }
    return 1;
}


/* Compile diagnostics in LS-282 order (the compiler retains them sorted). */
void report_diags(sbuf *receipt)
{
    int64_t count = g_diag[0].detail[0], rest = g_diag[0].detail[1];
    sbuf b = {0};
    char note[1400];
    for (int64_t k = 1; k <= count && k < CINT_BRIDGE_DIAG_ROWS; k++) {
        const cint_compiler_diag *d = &g_diag[k];
        /* C6001 and C6002 carry their compile-time fault in a faults-table slot (CINTC-14). */
        frec f;
        int fault = (d->code == 6001 || d->code == 6002) && d->detail[2] >= 0 &&
                    d->detail[2] <= CINT_BRIDGE_FAULT_BYTES - 2372 &&
                    decode_fault(g_faults + d->detail[2], 2372u, &f, 1) == 0;
        sbuf *o = receipt != NULL ? receipt : &b;
        if (receipt != NULL || json_mode) {
            if (receipt != NULL && k > 1) {
                sb_s(o, ",");
            }
            sb_jdiag(o, d, fault ? &f : NULL, dfile(d), receipt != NULL ? NULL : diag_text(d),
                     receipt == NULL && script_note(d, note, sizeof note) ? note : NULL);
            continue;
        }
        sb_f(&b, "error[C%04lld]: %s\n  --> %s:%lld:%lld\n", (long long)d->code, diag_text(d), dfile(d),
             (long long)d->line, (long long)d->column);
        excerpt(&b, dfile(d), strlen(dfile(d)), (uint32_t)d->line, (uint32_t)d->column, "");
        if (fault) {
            human_fault(&b, &f, -1);
        }
        if (script_note(d, note, sizeof note)) {
            sb_f(&b, "   = note: %s\n", note);
        }
    }
    if (receipt == NULL) {
        if (rest > 0 && !json_mode) {
            sb_f(&b, "%lld more errors not shown (SPEC-04 LS-282)\n", (long long)rest);
        }
        if (b.p != NULL) {
            fputs(b.p, stderr);
        }
        fflush(stderr);
    }
    free(b.p);
}

/* ------------------------------------------------------------------------- */
/* The run.                                                                   */

static int relay(void *user, const uint8_t *bytes, size_t len)
{
    run_out *o = user;
    if (fwrite(bytes, 1, len, stdout) != len || fflush(stdout) != 0) {
        o->failed = 1;
        return 1;
    }
    if (o->keep) {
        sb_put(&o->bytes, bytes, len);
    }
    return 0;
}

int64_t now_ns(void)
{
    struct timespec ts;
    if (timespec_get(&ts, TIME_UTC) != TIME_UTC) {
        return 0;
    }
    return (int64_t)ts.tv_sec * 1000000000 + (int64_t)ts.tv_nsec;
}

static int read_outset(const char *out, outset *o)
{
    uint8_t *ref, *man;
    size_t len, mlen;
    char *mpath, *line, *next;
    char *ref_path = join(out, "MANIFEST.ref");
    memset(o, 0, sizeof *o);
    if (read_file(ref_path, &ref, &len) != 0 || len == 0u || len >= 256u || strchr((char *)ref, ' ') == NULL) {
        free(ref_path);
        return -1;
    }
    free(ref_path);
    *strchr((char *)ref, ' ') = '\0';
    (void)snprintf(o->stage, sizeof o->stage, "%s" SEP "..%s%s", out, SEP, (char *)ref);
    free(ref);
    mpath = join(o->stage, "MANIFEST");
    if (read_file(mpath, &man, &mlen) != 0 || digest(man, mlen, o->manifest_hex) != 0) {
        free(mpath);
        return -1;
    }
    free(mpath);
    for (line = strchr((char *)man, '\n'); line != NULL; line = next) {
        char *sp, **rel;
        next = strchr(++line, '\n');
        if (next != NULL) {
            *next = '\0';
        }
        if ((sp = strrchr(line, ' ')) != NULL && sp[1] != '\0' &&
            (rel = realloc(o->rel, (o->n + 1u) * sizeof *rel)) != NULL) {
            o->rel = rel;
            o->rel[o->n++] = dup_s(sp + 1);
        }
    }
    free(man);
    return o->n > 0u ? 0 : -1;
}

/* The program's fuel record (rt/cint_rt.h CINT_FUEL_RECORD_DOMAIN): the domain string,
 * U32 length first, then fuel consumed as a non-negative I64, little-endian. Zero and
 * *fuel on success. */
static int decode_fuel(const uint8_t *data, size_t n, int64_t *fuel)
{
    static const char domain[] = "cint-core-1/fuel-consumed/v1";
    const size_t d = sizeof domain - 1u;
    uint64_t v = 0u;
    if (n != 4u + d + 8u || data[0] != (uint8_t)d || data[1] != 0u || data[2] != 0u || data[3] != 0u ||
        memcmp(data + 4, domain, d) != 0) {
        return -1;
    }
    for (size_t i = 0; i < 8u; i++) {
        v |= (uint64_t)data[4u + d + i] << (8u * i);
    }
    if (v > (uint64_t)INT64_MAX) {
        return -1;
    }
    *fuel = (int64_t)v;
    return 0;
}

/* A U32 length and a name of SPEC-09 CONF-11 rule 12: identifiers joined by at most `dots` dots. */
static const char *rd_name(rd *r, int *len, unsigned dots)
{
    uint64_t k = rd_u(r, 4);
    const char *s = (const char *)rd_take(r, k);
    int start = 1;
    *len = s != NULL && k <= INT32_MAX ? (int)k : 0;
    for (int i = 0; i < *len; i++) {
        int c = (unsigned char)s[i], letter = ((c | 32) >= 'a' && (c | 32) <= 'z') || c == '_';
        if (c == '.' ? start || dots-- == 0u : !letter && (start || c < '0' || c > '9')) r->bad = 1;
        start = c == '.';
    }
    if (start) r->bad = 1;
    return s;
}

/* The program's error record (rt/OPEN.md RT-OQ-33; its domain is spelled out, as pin 1's cint_rt.h has no
 * CINT_ERROR_RECORD_DOMAIN): the domain, the set's and the value's names, the tag code of U8 to U64 and a
 * tag of that type other than 0, nothing after. The CLI builds against pin 1's header (cint-stage floor). */
static int decode_error(const uint8_t *data, size_t n, cli_error *e)
{
    static const char domain[] = "cint-core-1/error-result/v1", *const types[4] = {"U8", "U16", "U32", "U64"};
    rd r = {data, n, 0u, 0};
    const uint8_t *d = rd_u(&r, 4) == sizeof domain - 1u ? rd_take(&r, sizeof domain - 1u) : NULL;
    e->set = rd_name(&r, &e->set_len, ~0u);
    e->value = rd_name(&r, &e->value_len, 1u);
    uint64_t k = rd_u(&r, 4) - (uint64_t)CINT_TAG_U8;
    e->tag = rd_u(&r, 8);
    if (d == NULL || memcmp(d, domain, sizeof domain - 1u) != 0 || r.bad || r.at != n || k > 3u || e->tag == 0u ||
        (k < 3u && e->tag >> (8u << k) != 0u)) return -1;
    e->type = types[k];
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Tests (SPEC-04 LS-232 to LS-240, SPEC-06 3.5).                             */

/* The bytes of a string literal's body; the compiler has checked its escapes (SPEC-04 3.8). */
size_t unescape(const uint8_t *s, size_t n, char *out)
{
    static const unsigned lead[4] = {0x00u, 0xC0u, 0xE0u, 0xF0u};
    size_t o = 0u;
    for (size_t i = 0u; i < n; i++) {
        unsigned c = s[i], v = 0u, k = 0u;
        if (c != '\\' || i + 1u >= n) {
            out[o++] = (char)c;
            continue;
        }
        c = s[++i];
        if (c != 'x' && c != 'u') {
            out[o++] = (char)(c == 'n' ? 10u : c == 'r' ? 13u : c == 't' ? 9u : c == '0' ? 0u : c);
            continue;
        }
        for (i += c == 'u' ? 2u : 1u; i < n && s[i] != '}' && (c == 'u' || k < 2u); i++, k++) {
            v = v * 16u + (s[i] <= '9' ? s[i] - 48u : (s[i] | 32u) - 87u);
        }
        i -= c == 'x' ? 1u : 0u;
        k = v < 0x80u ? 0u : v < 0x800u ? 1u : v < 0x10000u ? 2u : 3u;
        out[o++] = (char)(lead[k] | (v >> (6u * k)));
        while (k-- > 0u) out[o++] = (char)(0x80u | ((v >> (6u * k)) & 0x3Fu));
    }
    return o;
}

/* Reads the header of the test whose `test` token is at s[i]: its name, and expect_fault with
 * an optional `at N` (SPEC-04 LS-236, LS-240). The compiler has accepted it. Zero on success. */
static int test_header(const uint8_t *s, size_t n, size_t i, cli_test *t)
{
    if (!word(s, n, i, "test") || (i = blank(s, n, i + 4u)) >= n || s[i] != '"') return -1;
    t->spelling = s + i;
    for (i++; i < n && s[i] != '"'; i++) i += (size_t)(s[i] == '\\');
    t->spelling_len = (size_t)(s + i + 1 - t->spelling);
    i = blank(s, n, i + 1u);
    t->at = -1;
    if (word(s, n, i, "expect_fault")) {
        i = blank(s, n, i + 12u);
        size_t k = ident(s, n, i);
        for (unsigned c = 1u; c < 14u; c++) {
            if (strlen(CODE_NAMES[c]) == k && memcmp(s + i, CODE_NAMES[c], k) == 0) t->expect = c;
        }
        i = blank(s, n, i + k);
        if (word(s, n, i, "at")) {
            for (i = blank(s, n, i + 2u), t->at = 0; i < n && s[i] >= '0' && s[i] <= '9'; i++) {
                t->at = t->at > INT32_MAX ? t->at : t->at * 10 + (s[i] - '0');
            }
        }
        return t->expect != 0u ? 0 : -1;
    }
    return 0;
}

/* The tests of module m in source order, from its SIR text (output 1, <P>.sites): each
 * `test @<module> <j> {` line is followed by the entry's fuel charge at the `test` token,
 * `... @<path>:<line>:<column>`, where the module's source gives the header. The driver calls
 * test j as <prefix>_test<j>, the prefix of the module descriptor in <P>.c (EMIT-22;
 * compiler/OPEN.md CINTC-OQ-56). Zero on success. */
static int read_tests(const outset *set, cli_module *m, sbuf *decl, sbuf *table)
{
    uint8_t *text[2] = {NULL, NULL};
    size_t len, n = strlen(m->rel) - 3u;
    for (int f = 0; f < 2; f++) {
        sbuf name = {0};
        sb_put(&name, m->rel, n);
        sb_s(&name, f == 0 ? ".sites" : ".c");
        char *path = join(set->stage, name.p);
        int bad = read_file(path, &text[f], &len) != 0;
        free(path);
        free(name.p);
        if (bad) return -1;
    }
    char *prefix = strstr((char *)text[1], "const cint_module "), *end = prefix != NULL ? strstr(prefix, "_module = {") : NULL;
    int j = 0;
    for (char *t = strstr((char *)text[0], "\ntest @"); t != NULL; t = strstr(t + 1, "\ntest @"), j++) {
        char *at = strchr(t + 1, '\n'), *eol = at != NULL ? strchr(at + 1, '\n') : NULL, *c = eol;
        uint32_t pos[2] = {0u, 0u};
        cli_test *tests = realloc(g.tests, (size_t)(g.ntests + 1) * sizeof *tests);
        if (end == NULL || eol == NULL || tests == NULL) return -1;
        g.tests = tests;
        cli_test *x = memset(&tests[g.ntests++], 0, sizeof *tests);
        for (int k = 1; k >= 0; k--) {
            char *d = c;
            while (d > at && d[-1] >= '0' && d[-1] <= '9') d--;
            for (char *q = d; q < c && pos[k] < 100000000u; q++) pos[k] = pos[k] * 10u + (uint32_t)(*q - '0');
            c = d > at && d[-1] == ':' ? d - 1 : at;
        }
        size_t i = 0u;
        for (uint32_t l = 1u; l < pos[0] && i < m->len; i++) l += m->src[i] == '\n';
        for (uint32_t col = 1u; col < pos[1] && i < m->len; col++) {
            for (i++; i < m->len && (m->src[i] & 0xC0u) == 0x80u; i++) {
            }
        }
        x->file = m->rel;
        x->line = pos[0];
        if (pos[0] == 0u || test_header(m->src, m->len, i, x) != 0) return -1;
        sb_s(decl, "CINT_RT_INTERNAL cint_status ");
        sb_put(decl, prefix + 18, (size_t)(end - prefix - 18));
        sb_f(decl, "_test%d(cint_ctx *ctx, int64_t fuel);\n", j);
        sb_s(table, g.ntests > 1 ? ", " : "");
        sb_put(table, prefix + 18, (size_t)(end - prefix - 18));
        sb_f(table, "_test%d", j);
    }
    free(text[0]);
    free(text[1]);
    return 0;
}

/* The test driver (cint-test.c): the program file with its C main renamed, then a main that
 * runs test argv[1] through cint_program_run_fuel with fuel 1,000,000,000 (SPEC-06 1.4) and
 * the fault and fuel destinations that follow. NULL when a test cannot be read. */
static char *write_driver(const outset *set, const char *cache)
{
    sbuf decl = {0}, table = {0}, d = {0};
    int count;
    cli_module **sorted = sorted_modules(0, &count);
    for (int k = 0; sorted != NULL && k < count; k++) {
        if (read_tests(set, sorted[k], &decl, &table) != 0) count = -1;
    }
    free(sorted);
    if (sorted == NULL || count < 0) return NULL;
    sb_s(&d, "/* cint-test.c: the tests of this build, written by `cint test` (SPEC-06 3.5). */\n"
             "#define main cint_program_main\nint cint_program_main(int argc, char **argv);\n"
             "#include \"cint-program.c\"\n#undef main\n");
    sb_put(&d, decl.p, decl.n);
    sb_f(&d, "static const cint_main_fn cint_tests[%d] = {%s};\n\nint main(int argc, char **argv)\n{\n"
             "    unsigned long k = 0u;\n    if (argc != 4) return CINT_EXIT_USAGE;\n"
             "    for (const char *p = argv[1]; *p >= '0' && *p <= '9'; p++) k = k * 10u + (unsigned long)(*p - '0');\n"
             "    return k < %du ? cint_program_run_fuel(&cg_program, cint_tests[k], INT64_C(1000000000), argv[2], argv[3])\n"
             "                  : CINT_EXIT_USAGE;\n}\n", g.ntests, table.p, g.ntests);
    char *path = join(cache, "cint-test.c");
    int ok = cint_commit_output(path, (const uint8_t *)d.p, d.n, (size_t)1 << 30);
    free(decl.p);
    free(table.p);
    free(d.p);
    return ok ? path : NULL;
}

static int discard(void *user, const uint8_t *bytes, size_t len)
{
    (void)user;
    (void)bytes;
    (void)len;
    return 0;
}

/* One test's result: a line on stdout (a CINT-TEST-1 object under --json); for a failure,
 * what was expected on stdout and the fault on stderr in the SPEC-06 8.4 form, or the error
 * that left the test in the 3.4a form (SPEC-06 3.5, BX12-13). */
static void report_test(const cli_test *t)
{
    sbuf b = {0}, e = {0};
    if (json_mode) {
        sb_jtest(&b, t, 1);
    } else {
        sb_f(&b, "test %s:%u ", t->file, (unsigned)t->line);
        sb_put(&b, t->spelling, t->spelling_len);
        sb_s(&b, t->passed ? " ... ok\n" : " ... FAILED\n");
        if (!t->passed && t->expect != 0u) {
            sb_f(&b, "  expected fault %s", CODE_NAMES[t->expect]);
            if (t->at >= 0) sb_f(&b, " at line %lld", (long long)t->at);
            sb_s(&b, t->fault != NULL ? "\n" : t->error != NULL ? "; an error left the test\n" : "; the test completed\n");
        }
        if (!t->passed && t->fault != NULL) human_fault(&e, t->fault, t->fuel);
        if (t->error != NULL) { sb_error(&e, t->error, 0); sb_s(&e, "\n"); }
    }
    fwrite(b.p, 1, b.n, stdout);
    fflush(stdout);
    if (e.p != NULL) fputs(e.p, stderr);
    fflush(stderr);
    free(b.p);
    free(e.p);
}

/* Runs each test in its own process (exit 0: completed; 1: faulted, with a fault record; 6: an
 * error left it, with an error record) and gives the verdict of SPEC-04 LS-235, LS-236, LS-240
 * and LS-315, as cint_ref's test_verdict does. */
static int run_tests(outcome *oc, const char *exe, const char *cache, const char *src_hex)
{
    char *fpath = join(cache, "fault.bin"), *upath = join(cache, "fuel.bin"), *epath = join(cache, "program.stderr");
    int passed = 0;
    for (int k = 0; k < g.ntests; k++) {
        cli_test *t = &g.tests[k];
        char index[24];
        const char *pargv[3] = {exe, index, NULL};
        uint8_t *bytes = NULL;
        size_t len = 0u;
        int st, ok;
        cli_proc pr;
        (void)snprintf(index, sizeof index, "%d", k);
        memset(&pr, 0, sizeof pr);
        pr.argv = pargv;
        pr.err_path = epath;
        pr.fault_path = fpath;
        pr.fuel_path = upath;
        pr.sink = discard;
        if (cli_proc_run(&pr, &st) != 0) {
            return tool_error(CINT_EXIT_ENVIRONMENT, NULL, 0, NULL, "cannot start the test program %s", exe);
        }
        if (read_file(epath, &bytes, &len) != 0 || len != 0u) {
            return tool_error(CINT_EXIT_INTERNAL, exe, st, epath,
                              "the test program wrote to stderr (a sanitizer or runtime report); see %s", epath);
        }
        free(bytes);
        if (st != CINT_EXIT_OK && st != CINT_EXIT_FAULT && st != CINT_EXIT_ERROR_VALUE) {
            return tool_error(st == CINT_EXIT_ENVIRONMENT ? st : CINT_EXIT_INTERNAL, exe, st, NULL,
                              "test %d ended without a CINT status (exit status %d)", k, st);
        }
        ok = read_file(upath, &bytes, &len) == 0 && decode_fuel(bytes, len, &t->fuel) == 0;
        free(bytes);
        /* A decoded fault points into its record's bytes, which stay allocated. */
        if (!ok || (st == CINT_EXIT_FAULT && ((t->fault = malloc(sizeof *t->fault)) == NULL ||
                                             read_file(fpath, &bytes, &len) != 0 || decode_fault(bytes, len, t->fault, 0) != 0)) ||
            (st == CINT_EXIT_ERROR_VALUE && ((t->error = malloc(sizeof *t->error)) == NULL ||
                                             read_file(fpath, &bytes, &len) != 0 || decode_error(bytes, len, t->error) != 0))) {
            return tool_error(CINT_EXIT_INTERNAL, exe, st, ok ? fpath : upath, "the record %s of test %d is unreadable",
                              ok ? fpath : upath, k);
        }
        t->passed = t->expect == 0u ? st == CINT_EXIT_OK
                    : st == CINT_EXIT_FAULT && t->fault->code == t->expect && (t->at < 0 || t->fault->pos.line == t->at);
        passed += t->passed;
        report_test(t);
    }
    if (!json_mode) {
        printf("%d of %d tests passed\n", passed, g.ntests);
        fflush(stdout);
    }
    oc->status = passed == g.ntests ? CINT_EXIT_OK : CINT_EXIT_CHECK;
    oc->kind = passed == g.ntests ? "pass" : "fail";
    return finish(oc, src_hex);
}

/* Variables the host C compiler or linker reads as extra options (MSVC CL, _CL_, LINK, _LINK_;
 * clang CCC_OVERRIDE_OPTIONS). They are removed before the compiler starts, so the flags the
 * receipt records are the flags in effect. */
static const char *const cc_overrides[] = {"CL", "_CL_", "LINK", "_LINK_", "CCC_OVERRIDE_OPTIONS"};

/* After "warning": a colon (GCC, Clang), or a code such as C4100, D9025, or LNK4044 and then a
 * colon, with optional blanks before either colon (MSVC compiler, command line, and linker). */
static int warning_tail(const uint8_t *t, size_t len)
{
    size_t i = 0u, letters = 0u, digits = 0u;
    while (i < len && (t[i] == ' ' || t[i] == '\t')) i++;
    if (i < len && t[i] == ':') return 1;
    while (i < len && t[i] >= 'A' && t[i] <= 'Z') { i++; letters++; }
    while (i < len && t[i] >= '0' && t[i] <= '9') { i++; digits++; }
    while (i < len && (t[i] == ' ' || t[i] == '\t')) i++;
    return letters > 0u && digits > 0u && i < len && t[i] == ':';
}

typedef struct git_capture {
    char text[128];
    size_t n;
    int any, oversized;
} git_capture;

static int capture_git(void *user, const uint8_t *bytes, size_t len)
{
    git_capture *out = user;
    size_t room = sizeof out->text - 1u - out->n, take = len < room ? len : room;
    if (len > 0u) out->any = 1;
    if (take < len) out->oversized = 1;
    memcpy(out->text + out->n, bytes, take);
    out->n += take;
    out->text[out->n] = '\0';
    return 0;
}

static int git_query(const char *gitdir, const char *worktree, int status_query, git_capture *out)
{
    const char **args = malloc((16u + (size_t)g.nmods) * sizeof *args);
    size_t n = 0u;
    if (args == NULL) return -1;
    cli_proc p;
    int status = -1;
    char *err = join(g.cachedir, "git.stderr");
    args[n++] = "git";
    args[n++] = "--no-optional-locks";
    args[n++] = "--literal-pathspecs";
    args[n++] = "-C";
    args[n++] = g.dir;
    if (gitdir != NULL) {
        args[n++] = "--git-dir"; args[n++] = gitdir;
        args[n++] = "--work-tree"; args[n++] = worktree;
    }
    args[n++] = status_query ? "status" : "rev-parse";
    args[n++] = status_query ? "--porcelain=v1" : "--verify";
    args[n++] = status_query ? "--untracked-files=normal" : "HEAD";
    if (status_query) {
        /* dirty covers the compiled inputs only, whose bytes source.files hashes (D-19 as amended
         * by the M1 review): a receipt written into the tree, or any unrelated file, leaves it unchanged. */
        args[n++] = "--";
        for (int k = 0; k < g.nmods; k++) {
            if (g.mods[k].src != NULL) args[n++] = g.mods[k].rel;
        }
    }
    args[n] = NULL;
    memset(&p, 0, sizeof p);
    memset(out, 0, sizeof *out);
    p.argv = args; p.err_path = err; p.sink = capture_git; p.user = out;
    int started = cli_proc_run(&p, &status);
    free(err);
    free(args);
    return started == 0 ? status : -1;
}

static void source_metadata(void)
{
    static const char *const routing[] = {"GIT_DIR", "GIT_WORK_TREE", "GIT_COMMON_DIR", "GIT_INDEX_FILE", "GIT_NAMESPACE"};
    git_capture head, status;
    char *gitdir = NULL, *worktree = NULL;
    g.source_dirty = -1;
    g.source_revision[0] = '\0';
    for (size_t i = 0; i < sizeof routing / sizeof routing[0]; i++) {
        if (cli_unsetenv(routing[i]) != 0) {
            g.git_limit = "Git environment preparation failed; source revision and dirty state are unavailable.";
            return;
        }
    }
    int head_status = git_query(NULL, NULL, 0, &head);
    if (head_status < 0) {
        g.git_limit = "Git could not be started; source revision and dirty state are unavailable.";
        return;
    }
    if (head_status != 0 && cli_git_worktree(g.dir, &gitdir, &worktree)) {
        head_status = git_query(gitdir, worktree, 0, &head);
    }
    while (head.n > 0u && (head.text[head.n - 1u] == '\n' || head.text[head.n - 1u] == '\r')) head.text[--head.n] = '\0';
    int valid_head = head_status == 0 && !head.oversized && (head.n == 40u || head.n == 64u);
    for (size_t i = 0; valid_head && i < head.n; i++) {
        char c = head.text[i];
        valid_head = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    }
    if (valid_head) memcpy(g.source_revision, head.text, head.n + 1u);
    int status_result = git_query(gitdir, worktree, 1, &status);
    if (status_result == 0) g.source_dirty = status.any;
    if (!valid_head && status_result != 0) {
        g.git_limit = "The source project has no readable Git HEAD or status; source revision and dirty state are unavailable.";
    } else if (!valid_head) {
        g.git_limit = "The source project has no readable Git HEAD; source revision is unavailable.";
    } else if (status_result != 0) {
        g.git_limit = "Git status failed for the source project; source dirty state is unavailable.";
    }
    free(gitdir);
    free(worktree);
}

/* Splits FILE into the project root and the module path (CINTC-12). A build or test target
 * that does not end in .ci is a project directory: main.ci under DIR/src when that is a
 * directory, else under DIR (SPEC-06 3.3, Proposed layout). --out defaults to DIR/build. */
static int locate(void)
{
    const char *f = g.file, *slash = NULL;
    size_t n = strlen(f);
    cint_bridge_root *src = NULL;
    if (g.mode != MODE_RUN && (n < 3u || strcmp(f + n - 3u, ".ci") != 0)) {
        char *s = join(f, "src");
        int layout = g.root == NULL && n < sizeof g.dir - 4u && cint_bridge_root_open(s, &src);
        cint_bridge_root_close(src);
        (void)snprintf(g.dir, sizeof g.dir, "%s", layout ? s : f);
        (void)snprintf(g.rel, sizeof g.rel, "main.ci");
        free(s);
        if (g.out == NULL) g.out = join(f, "build");
        return g.root != NULL || n >= sizeof g.dir - 4u ? -1 : 0;
    }
    if (g.root != NULL) {
        if (strlen(f) >= sizeof g.rel) {
            return -1;
        }
        (void)snprintf(g.dir, sizeof g.dir, "%s", g.root);
        (void)snprintf(g.rel, sizeof g.rel, "%s", f);
        for (char *c = g.rel; *c; c++) {
            *c = *c == '\\' ? '/' : *c;
        }
    } else {
        for (const char *c = f; *c; c++) {
            if (*c == '/' || *c == '\\') {
                slash = c;
            }
        }
        if (slash == NULL) {
            (void)snprintf(g.dir, sizeof g.dir, ".");
        } else {
            n = (size_t)(slash - f);
            if (n == 0u || (n == 2u && f[1] == ':')) n++;
            if (n >= sizeof g.dir) return -1;
            (void)snprintf(g.dir, sizeof g.dir, "%.*s", (int)n, f);
        }
        if (strlen(slash != NULL ? slash + 1 : f) >= sizeof g.rel) {
            return -1;
        }
        (void)snprintf(g.rel, sizeof g.rel, "%s", slash != NULL ? slash + 1 : f);
    }
    if (g.out == NULL) g.out = join(g.dir, "build");
    return 0;
}

/* The toolchain file, the compiler's context, and the identity of this executable. */
static int ready(void)
{
    cint_ctx_config cfg;
    char *self = NULL, *tcpath;
    if (g.toolchain != NULL) {
        tcpath = dup_s(g.toolchain);
    } else if ((self = cli_self_dir()) == NULL || (tcpath = join(self, "cint.toolchain")) == NULL) {
        return tool_error(CINT_EXIT_ENVIRONMENT, NULL, 0, NULL, "cannot locate the cint executable's directory");
    }
    if (load_toolchain(tcpath) != 0) {
        return tool_error(CINT_EXIT_ENVIRONMENT, NULL, 0, NULL,
                          "no host C compiler: the toolchain file %s is missing or incomplete (run "
                          "tools/cint_bootstrap.py)",
                          tcpath);
    }
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = cm_4_main.program;
    if (cint_ctx_create(&cfg, &g.ctx) != CINT_OK) {
        return tool_error(CINT_EXIT_ENVIRONMENT, NULL, 0, NULL, "cannot create the compiler's context");
    }
    {
        char compiler_hex[65];
        if (strlen(CINT_COMPILER_SOURCE_IDENTITY) != 64u ||
            digest_file(g.tc.compiler_sources, compiler_hex) != 0 ||
            strcmp(compiler_hex, CINT_COMPILER_SOURCE_IDENTITY) != 0) {
            return tool_error(CINT_EXIT_ENVIRONMENT, NULL, 0, NULL,
                              "the toolchain compiler source identity does not match this cint executable; run tools/cint_bootstrap.py");
        }
        char *executable_path = cli_self_path();
        int matches = executable_path != NULL && digest_file(executable_path, compiler_hex) == 0 &&
                      strcmp(compiler_hex, g.tc.executable_sha256) == 0;
        free(executable_path);
        if (!matches) {
            return tool_error(CINT_EXIT_ENVIRONMENT, NULL, 0, NULL,
                              "the toolchain executable digest does not match this cint executable; run tools/cint_bootstrap.py");
        }
    }
    return 0;
}

/* Copies every module discovery read into `dir`, so the source digests name the bytes the
 * build reads, and opens it as the build's root. Zero on success. */
static int snapshot_inputs(const char *dir, cint_bridge_root **snap)
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
    return ok && cint_bridge_root_open(dir, snap) && same_inputs(*snap) ? 0 : -1;
}

/* The host C compiler, with the EMIT-26 flags (SPEC-09 7.7); its output goes to the log. A
 * test build compiles the driver, which includes the program file, in its place. */
static int compile_c(outcome *oc, const outset *set, const char *driver, const char *bin, const char *log)
{
    const char **argv = malloc((g.tc.nflags + set->n + 12u) * sizeof *argv);
    sbuf inc = {0}, stage = {0}, fe = {0}, fo = {0};
    int msvc = strcmp(g.tc.leg, "msvc") == 0, st = 0;
    size_t na = 0u;
    FILE *lf = cli_fopen(log, "wb");
    if (argv == NULL || lf == NULL) {
        return tool_error(CINT_EXIT_ENVIRONMENT, NULL, 0, NULL, "cannot write the log %s", log);
    }
    fclose(lf);
    argv[na++] = g.tc.cc;
    if (g.lib) argv[na++] = msvc ? "/LD" : "-shared";   /* one shared library (SPEC-06 3.1) */
    for (size_t i = 0; i < g.tc.nflags; i++) argv[na++] = g.tc.flags[i];
    sb_s(&inc, msvc ? "/I" : "-I");
    sb_s(&inc, g.tc.include);
    argv[na++] = inc.p;
    for (size_t i = 0; i < set->n; i++) {
        size_t n = strlen(set->rel[i]);
        if (n > 2u && strcmp(set->rel[i] + n - 2u, ".c") == 0 && (driver == NULL || strcmp(set->rel[i], "cint-program.c") != 0)) {
            argv[na++] = join(set->stage, set->rel[i]);
        }
    }
    if (driver != NULL) {
        sb_s(&stage, msvc ? "/I" : "-I");
        sb_s(&stage, set->stage);
        argv[na++] = stage.p;
        argv[na++] = driver;
    }
    argv[na++] = g.tc.runtime_object;
    if (msvc) {
        sb_s(&fe, "/Fe");
        sb_s(&fe, oc->exe);
        sb_s(&fo, "/Fo");
        sb_s(&fo, bin);
        sb_s(&fo, SEP);
        argv[na++] = fe.p;
        argv[na++] = fo.p;
    } else {
        argv[na++] = "-o";
        argv[na++] = oc->exe;
    }
    argv[na] = NULL;
    for (size_t i = 0; i < sizeof cc_overrides / sizeof cc_overrides[0]; i++) {
        if (getenv(cc_overrides[i]) != NULL && cli_unsetenv(cc_overrides[i]) != 0) {
            return tool_error(CINT_EXIT_ENVIRONMENT, NULL, 0, NULL, "cannot remove %s from the host C "
                              "compiler environment", cc_overrides[i]);
        }
    }
    cli_proc cc;
    uint8_t *text;
    size_t len;
    memset(&cc, 0, sizeof cc);
    cc.argv = argv;
    cc.log_path = log;
    oc->t_cc = now_ns();
    if (cli_proc_run(&cc, &st) != 0) {
        return tool_error(CINT_EXIT_ENVIRONMENT, g.tc.cc, -1, log, "no host C compiler: cannot start %s", g.tc.cc);
    }
    oc->t_cc = now_ns() - oc->t_cc;
    if (st != 0) {
        return tool_error(CINT_EXIT_INTERNAL, g.tc.cc, st, log,
                          "the host C compiler %s rejected the generated source with exit status %d; its "
                          "output is in %s (SPEC-09 EMIT-26)",
                          g.tc.cc, st, log);
    }
    if (read_file(log, &text, &len) == 0) {
        for (size_t i = 0; i + 7u <= len; i++) {
            if (memcmp(text + i, "warning", 7u) == 0 && warning_tail(text + i + 7u, len - i - 7u)) {
                return tool_error(CINT_EXIT_INTERNAL, g.tc.cc, st, log,
                                  "the host C compiler %s warned on the generated source; its output is in %s "
                                  "(SPEC-09 EMIT-26)",
                                  g.tc.cc, log);
            }
        }
        free(text);
    }
    return 0;
}

/* `cint build`: the program goes to --out as the root module's name without .ci (with --lib, lib<name>.so or <name>.dll). */
static int publish(outcome *oc, const char *src_hex)
{
    const char *stem = basename_utf8(g.rel);
    sbuf name = {0};
    uint8_t *bytes = NULL;
    size_t len = 0u;
    sb_s(&name, g.lib ? CLI_LIB_PREFIX : "");
    sb_put(&name, stem, strlen(stem) - 3u);
    sb_s(&name, g.lib ? CLI_LIB_SUFFIX : strcmp(g.tc.leg, "msvc") == 0 ? ".exe" : "");
    char *path = join(g.out, name.p);
    free(name.p);
    if (cli_mkdirs(g.out) != 0 || read_file(oc->exe, &bytes, &len) != 0 ||
        !cint_commit_output(path, bytes, len, (size_t)1 << 31) || cli_executable(path) != 0) {
        return tool_error(CINT_EXIT_ENVIRONMENT, NULL, 0, NULL, "cannot write the program %s", path);
    }
    free(bytes);
    oc->exe = path;
    oc->kind = "built";
    oc->status = CINT_EXIT_OK;
    if (!json_mode) {
        fprintf(stderr, "built: %s\n", path);
    }
    return finish(oc, src_hex);
}

static int run(void)
{
    cint_bridge_root *root = NULL, *snap = NULL;
    outcome oc;
    run_out out;
    outset set;
    frec *fault = NULL;
    cli_error error;
    char key_hex[65], *src_hex, *cache, *outdir, *bin, *log, *exe, *fpath, *epath, *upath, *driver = NULL;
    int st = 0, built, found;
    memset(&oc, 0, sizeof oc);
    oc.fuel = -1;
    memset(&out, 0, sizeof out);
    oc.out = &out;
    oc.t0 = now_ns();
    out.keep = g.receipt != NULL;
    if ((st = ready()) != 0) return st;
    if (g.lib && (g.tc.runtime_object = g.tc.runtime_library_object) == NULL) return tool_error(CINT_EXIT_ENVIRONMENT, NULL, 0, NULL, "the toolchain has no runtime_library_object for --lib");
    if (locate() != 0) {
        return usage(g.root != NULL ? "a project directory takes no --root; give FILE.ci" : "the file path is too long");
    }
    /* The sources, through the bridge reader: identity from the reading handle (D-3). */
    if (!cint_bridge_root_open(g.dir, &root)) {
        return tool_error(CINT_EXIT_ENVIRONMENT, NULL, 0, NULL, "cannot open the directory %s", g.dir);
    }
    if ((found = discover(root)) > 0) {
        return tool_error(CINT_EXIT_ENVIRONMENT, NULL, 0, NULL,
                          "cannot read %s under %s: it is missing, unreadable, a link, larger than 4 MiB, or a "
                          "path the bridge reader refuses (SPEC-09 CINTC-12)",
                          g.rel, g.dir);
    }
    if (found < 0) {
        return tool_error(CINT_EXIT_ENVIRONMENT, NULL, 0, NULL, "cannot record the imports of %s (at most 2048 modules)", g.rel);
    }
    src_hex = g.mods[0].hex;
    {
        sbuf k = {0};
        sb_s(&k, g.rel);
        sb_put(&k, "\n", 1u);
        sb_put(&k, g.src, g.src_len);
        if (digest((const uint8_t *)k.p, k.n, key_hex) != 0) {
            return tool_error(CINT_EXIT_INTERNAL, NULL, 0, NULL, "the digest export of the compiler failed");
        }
        free(k.p);
    }
    char *base = join(g.cache != NULL ? g.cache : g.tc.cache, key_hex);
    if (cli_mkdirs(base) != 0) {
        return tool_error(CINT_EXIT_ENVIRONMENT, NULL, 0, NULL, "cannot create the cache directory %s", base);
    }
    cache = NULL;
    for (unsigned k = 0; k < 10000u; k++) {
        char name[32];
        (void)snprintf(name, sizeof name, "run-%u", k);
        free(cache);
        cache = join(base, name);
        int created = cli_mkdir_new(cache);
        if (created == 1) break;
        if (created < 0 || k == 9999u) {
            return tool_error(CINT_EXIT_ENVIRONMENT, NULL, 0, NULL, "cannot create a private run directory under %s", base);
        }
    }
    outdir = join(cache, "out");
    bin = join(cache, "bin");
    log = join(cache, "cc.log");
    fpath = join(cache, "fault.bin");
    epath = join(cache, "program.stderr");
    upath = join(cache, "fuel.bin");
    (void)snprintf(g.cachedir, sizeof g.cachedir, "%s", cache);
    if (cli_mkdirs(bin) != 0) {
        return tool_error(CINT_EXIT_ENVIRONMENT, NULL, 0, NULL, "cannot create the cache directory %s", cache);
    }
    char *snapshot = join(cache, "input");
    if (snapshot_inputs(snapshot, &snap) != 0) {
        return tool_error(CINT_EXIT_ENVIRONMENT, NULL, 0, NULL, "cannot write the source snapshot %s", snapshot);
    }
    if (g.receipt != NULL) source_metadata();
    /* plan, compile, measure, emit per module, then commit the output set (D-13). */
    built = cint_build_with_paths(g.ctx, snap, g.order, g.norder, outdir, g_diag, g_faults, keep_diag_path, g_diag_paths);
    oc.t_build = now_ns() - oc.t0;
    cint_bridge_root_close(snap);
    if (built == CINT_BUILD_FAULT) {
        cint_bridge_root_close(root);
        return internal_compiler_fault(src_hex);
    }
    found = same_inputs(root);
    cint_bridge_root_close(root);
    if (!found) {
        return tool_error(CINT_EXIT_ENVIRONMENT, NULL, 0, NULL, "a module under %s changed while it was compiled", g.dir);
    }
    if (built == CINT_BUILD_DIAG) {
        oc.status = CINT_EXIT_COMPILE;
        oc.kind = "compile-error";
        report_diags(NULL);
        return finish(&oc, src_hex);
    }
    if (built == CINT_BUILD_REFUSED) {
        return tool_error(CINT_EXIT_INTERNAL, NULL, 0, NULL, "internal compiler error: build result %d", built);
    }
    if (built != CINT_BUILD_OK) {
        return tool_error(CINT_EXIT_ENVIRONMENT, NULL, 0, NULL, "cannot write the generated source under %s",
                          outdir);
    }
    if (read_outset(outdir, &set) != 0) {
        return tool_error(CINT_EXIT_INTERNAL, NULL, 0, NULL, "the output set under %s is unreadable", outdir);
    }
    oc.set = &set;
    if (g.mode != MODE_TEST) {
        uint8_t *text = NULL;
        size_t len;
        char *program = join(set.stage, "cint-program.c");
        int entry = read_file(program, &text, &len) == 0 && strstr((char *)text, "\nint main(") != NULL;
        free(program);
        free(text);
        if (entry == g.lib) return usage(g.lib ? "--lib builds a library, whose root module has no main and no script statements" : "the root module has no main and no script statements, so there is no program; `cint test` runs its tests");
    }
    if (g.mode == MODE_TEST && (driver = write_driver(&set, cache)) == NULL) {
        return tool_error(CINT_EXIT_INTERNAL, NULL, 0, NULL, "the tests of the output set under %s are unreadable", set.stage);
    }
    if (g.mode == MODE_TEST && g.ntests == 0) {
        return run_tests(&oc, NULL, cache, src_hex);
    }
    exe = join(bin, strcmp(g.tc.leg, "msvc") != 0 ? "program" : g.lib ? "program.dll" : "program.exe");
    oc.exe = exe;
    if ((st = compile_c(&oc, &set, driver, bin, log)) != 0) {
        return st;
    }
    if (g.mode == MODE_BUILD) {
        return publish(&oc, src_hex);
    }
    if (g.mode == MODE_TEST) {
        return run_tests(&oc, exe, cache, src_hex);
    }
    /* The program: stdin is the null device, stdout relayed byte for byte, the fault or error
     * record through an inherited handle (SPEC-06 3.4a). */
    {
        cli_proc pr;
        const char *pargv[2] = {exe, NULL};
        uint8_t *bytes = NULL;
        size_t len = 0;
        memset(&pr, 0, sizeof pr);
        pr.argv = pargv;
        pr.err_path = epath;
        pr.fault_path = fpath;
        pr.fuel_path = upath;
        pr.sink = relay;
        pr.user = &out;
        oc.t_run = now_ns();
        if (cli_proc_run(&pr, &st) != 0) {
            return tool_error(CINT_EXIT_ENVIRONMENT, NULL, 0, NULL, "cannot start the program %s", exe);
        }
        oc.t_run = now_ns() - oc.t_run;
        if (fflush(stdout) != 0 || out.failed) {
            return tool_error(CINT_EXIT_ENVIRONMENT, NULL, 0, NULL, "cannot write to stdout");
        }
        if (read_file(epath, &bytes, &len) != 0 || len != 0u) {
            return tool_error(CINT_EXIT_INTERNAL, exe, st, epath,
                              "the program wrote to stderr (a sanitizer or runtime report); see %s", epath);
        }
        free(bytes);
        bytes = NULL;
        if (st == CINT_EXIT_OK || st == CINT_EXIT_FAULT || st == CINT_EXIT_ERROR_VALUE) {
            /* The fuel record, written before exit by cint_program_run_fuel (SPEC-06 3.4a). */
            int ok = read_file(upath, &bytes, &len) == 0 && decode_fuel(bytes, len, &oc.fuel) == 0;
            free(bytes);
            bytes = NULL;
            if (!ok && st == CINT_EXIT_OK) {
                return tool_error(CINT_EXIT_INTERNAL, exe, st, upath, "the program's fuel record %s is unreadable",
                                  upath);
            }
            if (!ok) {
                oc.fuel = -2;   /* reported after the fault or error record, which is checked first */
            }
        }
        if (st == CINT_EXIT_OK) {
            oc.kind = "value";
            oc.status = CINT_EXIT_OK;
            return finish(&oc, src_hex);
        }
        if (st == CINT_EXIT_ENVIRONMENT) {
            return tool_error(CINT_EXIT_ENVIRONMENT, exe, st, NULL, "the program could not write its output");
        }
        if (st != CINT_EXIT_FAULT && st != CINT_EXIT_ERROR_VALUE) {
            return tool_error(CINT_EXIT_INTERNAL, exe, st, NULL,
                              "the program ended without a CINT status (exit status %d)", st);
        }
        if (read_file(fpath, &bytes, &len) != 0 || (st == CINT_EXIT_FAULT ? (fault = malloc(sizeof *fault)) == NULL ||
            decode_fault(bytes, len, fault, 0) != 0 : decode_error(bytes, len, oc.error = &error) != 0)) {
            return tool_error(CINT_EXIT_INTERNAL, exe, st, fpath, "the program's %s record %s is unreadable",
                              st == CINT_EXIT_FAULT ? "fault" : "error", fpath);
        }
        if (oc.fuel < 0) {
            return tool_error(CINT_EXIT_INTERNAL, exe, st, upath, "the program's fuel record %s is unreadable",
                              upath);
        }
        {
            sbuf b = {0};
            if (oc.error != NULL) sb_error(&b, oc.error, json_mode);   /* one line (SPEC-06 3.4a) */
            else if (json_mode) sb_jfault(&b, fault, oc.fuel);
            else human_fault(&b, fault, oc.fuel);
            if (json_mode || oc.error != NULL) sb_s(&b, "\n");
            fputs(b.p, stderr);
            fflush(stderr);
            free(b.p);
        }
        oc.kind = oc.error != NULL ? "error" : "fault";
        oc.status = st;
        oc.fault = fault;
        return finish(&oc, src_hex);
    }
}

static int emit_c(void)
{
    cint_bridge_root *root = NULL;
    char src_hex[65] = "";
    int st = ready(), built;
    if (st != 0) return st;
    if (g.root == NULL || g.out == NULL) return usage("emit-c needs --root DIR and --out DIR");
    for (int i = 0; i < g.nfiles; i++)
        for (char *c = g.files[i]; *c; c++) *c = *c == '\\' ? '/' : *c;
    (void)snprintf(g.rel, sizeof g.rel, "%s", g.files[g.nfiles - 1]);
    (void)snprintf(g.cachedir, sizeof g.cachedir, "%s", g.out);
    if (!cint_bridge_root_open(g.root, &root) || cli_mkdirs(g.out) != 0) {
        return tool_error(CINT_EXIT_ENVIRONMENT, NULL, 0, NULL, "cannot open %s or create %s", g.root, g.out);
    }
    for (int i = 0; i < g.nfiles; i++) {
        int m = module_at(root, g.files[i]);
        if (m < 0) return tool_error(CINT_EXIT_ENVIRONMENT, NULL, 0, NULL, "cannot record %s", g.files[i]);
        if (i == g.nfiles - 1) memcpy(src_hex, g.mods[m].hex, sizeof src_hex);
    }
    built = cint_build_with_paths(g.ctx, root, (const char *const *)g.files, g.nfiles, g.out, g_diag, g_faults,
                                 keep_diag_path, g_diag_paths);
    cint_bridge_root_close(root);
    if (built == CINT_BUILD_FAULT) return internal_compiler_fault(src_hex);
    if (built == CINT_BUILD_DIAG) report_diags(NULL);
    return built == CINT_BUILD_OK ? CINT_EXIT_OK : built == CINT_BUILD_DIAG ? CINT_EXIT_COMPILE
         : tool_error(built == CINT_BUILD_IO ? CINT_EXIT_ENVIRONMENT : CINT_EXIT_INTERNAL, NULL, 0, NULL,
                      "cannot build the output set under %s (cint_build result %d)", g.out, built);
}

int main(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) json_mode = 1;
    }
    if (cli_init(&argc, &argv) != 0) {
        return tool_error(CINT_EXIT_ENVIRONMENT, NULL, 0, NULL, "cannot read the command line");
    }
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) {
            json_mode = 1;
        }
    }
    /* The subcommand is the first argument other than --json, which may come before it (SPEC-06 3.1). */
    int sub = 1;
    while (sub < argc && strcmp(argv[sub], "--json") == 0) sub++;
    if (sub < argc && (strcmp(argv[sub], "--help") == 0 || strcmp(argv[sub], "help") == 0)) {
        fputs(USAGE, stdout);
        return 0;
    }
    static const char *const subs[4] = {"run", "build", "test", "emit-c"};
    for (g.mode = 0; sub < argc && g.mode < 4 && strcmp(argv[sub], subs[g.mode]) != 0; g.mode++) {
    }
    if (sub >= argc || g.mode == 4) {
        return usage(sub >= argc ? "no subcommand" : "unknown subcommand; there are run, build, test and emit-c");
    }
    g.emit = g.mode == 3;
    for (i = sub + 1; i < argc; i++) {
        const char *a = argv[i];
        const char **slot = strcmp(a, "--receipt") == 0     ? &g.receipt
                            : strcmp(a, "--root") == 0      ? &g.root
                            : strcmp(a, "--toolchain") == 0 ? &g.toolchain
                            : strcmp(a, "--cache") == 0     ? &g.cache
                            : strcmp(a, "--out") == 0 && (g.emit || g.mode == MODE_BUILD) ? &g.out
                                                            : NULL;
        if (strcmp(a, "--json") == 0 || (strcmp(a, "--lib") == 0 && g.mode == MODE_BUILD && (g.lib = 1) != 0)) {
            continue;
        }
        if (slot != NULL) {
            if (i + 1 >= argc || *slot != NULL) {
                return usage("an option needs one value and is given at most once");
            }
            *slot = argv[++i];
        } else if (a[0] == '-' && a[1] != '\0') {
            return usage("unknown option (SPEC-06 3.4 options other than --receipt are not in slice 2)");
        } else if ((g.file != NULL && !g.emit) || g.nfiles == 64) {
            return usage(g.mode == MODE_RUN ? "program arguments are not supported; give one FILE.ci" : "give one FILE.ci or project directory");
        } else {
            g.file = a;
            g.files[g.nfiles++] = argv[i];
        }
    }
    if (g.file == NULL && (g.mode == MODE_BUILD || g.mode == MODE_TEST)) {
        g.file = ".";
    }
    if (g.file == NULL) {
        return usage("no FILE.ci given");
    }
    if (g.emit) {
        int status = g.receipt != NULL || g.cache != NULL ? usage("emit-c takes no --receipt or --cache") : emit_c();
        free_diag_paths();
        return status;
    }
    if (g.receipt != NULL && g.receipt[0] == '\0') {
        return usage("--receipt needs a path");
    }
    int status = run();
    free_diag_paths();
    return status;
}
