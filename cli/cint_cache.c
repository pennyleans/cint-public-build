/* cint_cache.c: the toolchain file, the cache directory, and the executables `cint run` keeps for
 * reuse (OQ-313; SPEC-06 3.4, 4.2; issues #457, #503).
 *
 * The toolchain file (default: cint.toolchain beside this executable) is `cint-toolchain-1`, then
 * `key value` lines, written by tools/cint_bootstrap.py. Its `cache` line is the Python package's
 * (python/cint/_build.py); this tool does not read it.
 *
 * The cache directory of `cint run`, `build` and `test` (cache_root): --cache DIR, else the
 * environment variable CINT_CACHE, else the platform cache directory (SPEC-06 4.2):
 * %LOCALAPPDATA%\cint on Windows, $HOME/Library/Caches/cint on macOS, and elsewhere
 * $XDG_CACHE_HOME/cint when that is an absolute path, else $HOME/.cache/cint. It is never under the
 * toolchain's own tree, which may be read-only or shared by several users (issue #503). With none
 * of these set, a command that needs the cache exits 5 and asks for --cache. `cint emit-c` writes
 * only its output set and takes no --cache.
 *
 * The run cache. Every `cint run` compiles in its own `<cache>/<key>/run-<N>` directory; without
 * reuse a program of ced's size spent 6.5 s in cintc and the host C compiler on every start. A run
 * with no --receipt, no --width-report and no --rebuild first computes a run key: SHA-256 of the
 * text below, one line each, in this order:
 *
 *   cint-run-cache-1
 *   cint <executable_sha256 of the toolchain file, the digest ready() checks against this executable>
 *   toolchain <SHA-256 of the toolchain file's bytes: compiler, flags, include and runtime paths>
 *   runtime <SHA-256 of the runtime object the program links>
 *   module <key> <SHA-256 of its bytes> for each module discovery read, in discovery order, the root
 *     first; an import path no file answered is `absent <where> <path>` (RCPT-07)
 *
 * so a change to a module, an import that starts or stops resolving, a new cint or a changed
 * toolchain gives a new key. `<cache>/exe/<key>/program` (program.exe on MSVC) holds the program
 * of the first run that compiled under that key, moved there by rename, and program.sha256 its
 * SHA-256 in hex. A later run whose program digest matches the record runs it without compiling;
 * a missing, partial or corrupted entry is a miss and is replaced (SPEC-06 4.2). --frame-arena,
 * the --grant lines and the program's arguments reach the program as arguments, not through its
 * bytes, so they are not in the key. The executable is an observation (SPEC-09 FIX-05), never a
 * build identity, and no record names it: a run that writes a receipt always compiles. */
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

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* An environment variable in UTF-8, through the wide API as cli/cint_proc_win.c reads paths; NULL when unset or empty. */
static char *env(const char *name)
{
    wchar_t wname[32], *w;
    char *s = NULL;
    int n;
    if (MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, 32) <= 0 || (w = _wgetenv(wname)) == NULL) return NULL;
    if ((n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL)) > 1 && (s = malloc((size_t)n)) != NULL &&
        WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL) != n) {
        free(s);
        s = NULL;
    }
    return s;
}
#else
static char *env(const char *name)
{
    const char *v = getenv(name);
    return v != NULL && v[0] != '\0' ? dup_s(v) : NULL;
}
#endif

const char *cache_root(void)
{
    static char *root;
    char *v;
    if (g.cache != NULL || root != NULL) return g.cache != NULL ? g.cache : root;
    if ((v = env("CINT_CACHE")) != NULL) return root = v;
#if defined(_WIN32)
    const char *base = "LOCALAPPDATA", *tail = "cint";
#elif defined(__APPLE__)
    const char *base = "HOME", *tail = "Library/Caches/cint";
#else
    const char *base = "HOME", *tail = ".cache/cint";
    if ((v = env("XDG_CACHE_HOME")) != NULL && v[0] == '/') {
        root = join(v, "cint");
        free(v);
        return root;
    }
    free(v);
#endif
    if ((v = env(base)) != NULL) root = join(v, tail);
    free(v);
    return root;
}

static int by_basename(const void *a, const void *b)
{
    return strcmp(basename_utf8(*(char *const *)a), basename_utf8(*(char *const *)b));
}

static void hex32(const uint8_t hash[32], char hex[65])
{
    for (int i = 0; i < 32; i++) (void)snprintf(hex + 2 * i, 3, "%02x", (unsigned)hash[i]);
}

int qbe_backend(void) { return g.tc.backend != NULL && strcmp(g.tc.backend, "cpu-qbe") == 0; }
/* The capability names of back_qbe's refusals (CAP_ENTRY .. CAP_MODULE). */
const char *qbe_capability(int64_t capability)
{
    static const char *const names[] = {"none", "entry", "storage", "node", "type", "module", "symbol"};
    return capability >= 1 && capability <= 6 ? names[capability] : "unknown";
}

static int hex_text(const char *s, size_t n)
{
    if (s == NULL || strlen(s) != n) return 0;
    for (size_t i = 0; i < n; i++) if (!(s[i] >= '0' && s[i] <= '9') && !(s[i] >= 'a' && s[i] <= 'f')) return 0;
    return 1;
}

int load_toolchain(const char *path)
{
    uint8_t *bytes, hash[32];
    size_t len;
    char *line, *next;
    toolchain *t = &g.tc;
    if (read_file(path, &bytes, &len) != 0 || len < 16u || memcmp(bytes, "cint-toolchain-1\n", 16) != 0) return -1;
    cint_sha256(bytes, len, hash);
    hex32(hash, t->file_hex);
    for (line = (char *)bytes + 16; *line != '\0'; line = next) {
        char *sp, *val;
        if ((next = strchr(line, '\n')) == NULL) next = line + strlen(line);
        else *next++ = '\0';
        if ((sp = strchr(line, ' ')) == NULL) continue;
        *sp = '\0';
        val = sp + 1;
        if (strcmp(line, "flag") == 0 && t->nflags < 64u) {
            t->flags[t->nflags++] = val;
        } else if (strcmp(line, "runtime_source") == 0 && t->nrt <= CLI_MAX_RT) {
            t->rt_sources[t->nrt++] = val;
        } else if (strcmp(line, "env") == 0 && strchr(val, '=') != NULL) {
            char *eq = strchr(val, '=');
            *eq = '\0';
            if (cli_setenv(val, eq + 1) != 0) return -1;
        } else if (strcmp(line, "as_flag") == 0) {
            if (t->nas_flags == 32u) return -1;
            t->as_flags[t->nas_flags++] = val;
        } else if (strcmp(line, "link_before") == 0) {
            if (t->nlink_before == 32u) return -1;
            t->link_before[t->nlink_before++] = val;
        } else if (strcmp(line, "link_after") == 0) {
            if (t->nlink_after == 32u) return -1;
            t->link_after[t->nlink_after++] = val;
        } else {
            static const char *const keys[] = {"leg", "cc", "cc_version", "include", "runtime_object", "host",
                                               "compiler_sources", "executable_sha256", "runtime_library_object",
                                               "backend", "qbe", "qbe_sha256", "qbe_commit", "target", "assembler", "linker"};
            char **slots[] = {&t->leg, &t->cc, &t->cc_version, &t->include, &t->runtime_object, &t->host,
                              &t->compiler_sources, &t->executable_sha256, &t->runtime_library_object,
                              &t->backend, &t->qbe, &t->qbe_sha256, &t->qbe_commit, &t->target, &t->assembler, &t->linker};
            for (size_t k = 0; k < sizeof keys / sizeof keys[0]; k++) {
                if (strcmp(line, keys[k]) == 0) { if (k >= 9u && *slots[k] != NULL) return -1; *slots[k] = val; }
            }
        }
    }
    qsort(t->rt_sources, t->nrt, sizeof t->rt_sources[0], by_basename);
    if (t->backend != NULL && strcmp(t->backend, "cpu-c17") != 0 && !qbe_backend()) return -1;
    if (qbe_backend() && (!hex_text(t->qbe_sha256, 64) ||
        /* qbe_commit: the upstream git commit (40), or the sha256 of the release source tarball (64) */
        (!hex_text(t->qbe_commit, 40) && !hex_text(t->qbe_commit, 64)) ||
        t->qbe == NULL || t->qbe[0] != '/' || t->assembler == NULL || t->assembler[0] != '/' ||
        t->linker == NULL || t->linker[0] != '/' || t->target == NULL ||
        (strcmp(t->target, "amd64_sysv") != 0 && strcmp(t->target, "arm64_apple") != 0))) return -1;
    if (qbe_backend()) { char actual[65]; if (digest_file(t->qbe, actual) != 0 || strcmp(actual, t->qbe_sha256) != 0) return -1; }
    if (qbe_backend()) { if (cint_build_backend != 0) return -1; cint_build_backend = 2; }   /* cpu-qbe, never with cpu-c17-spec */
    return t->leg != NULL && t->cc != NULL && t->include != NULL && t->runtime_object != NULL &&
                   t->compiler_sources != NULL && t->executable_sha256 != NULL &&
                   strlen(t->executable_sha256) == 64u && t->nrt > 0u && t->nrt <= CLI_MAX_RT && t->nflags > 0u
               ? 0
               : -1;
}

static int qbe_tool(const char *const *argv, const char *log)
{
    int status = 0;
    FILE *f = cli_fopen(log, "ab");
    if (f == NULL) return tool_error(CINT_EXIT_ENVIRONMENT, NULL, 0, log, "cannot write native tool log");
    for (size_t i = 0; argv[i] != NULL; i++) (void)fprintf(f, "%s%s", i == 0 ? "" : " ", argv[i]);
    (void)fputc('\n', f); if (fclose(f) != 0) return CINT_EXIT_ENVIRONMENT;
    cli_proc child = {.argv = argv, .log_path = log};
    if (cli_proc_run(&child, &status) != 0) return tool_error(CINT_EXIT_ENVIRONMENT, argv[0], -1, log, "cannot start native tool %s", argv[0]);
    return status == 0 ? 0 : tool_error(CINT_EXIT_INTERNAL, argv[0], status, log, "native tool rejected backend output; see %s", log);
}

int compile_qbe(const outset *set, const char *bin, const char *exe, const char *log)
{
    const char **link = calloc(set->n + g.tc.nlink_before + g.tc.nlink_after + 5u, sizeof *link);
    char **objects = calloc(set->n, sizeof *objects);
    size_t n = 0, no = 0;
    int status = 0;
    if (link == NULL || objects == NULL) { free(link); free(objects); return CINT_EXIT_ENVIRONMENT; }
    link[n++] = g.tc.linker;
    for (size_t i = 0; i < g.tc.nlink_before; i++) link[n++] = g.tc.link_before[i];
    for (size_t i = 0; status == 0 && i < set->n; i++) {
        size_t len = strlen(set->rel[i]);
        if (len < 4 || strcmp(set->rel[i] + len - 4, ".ssa") != 0) continue;
        char *src = join(set->stage, set->rel[i]), *stem = join(bin, basename_utf8(set->rel[i]));
        sbuf assembly = {0}, object = {0};
        sb_f(&assembly, "%s.s", stem); sb_f(&object, "%s.o", stem); free(stem);
        if (src == NULL || assembly.p == NULL || object.p == NULL) status = CINT_EXIT_ENVIRONMENT;
        if (status == 0) { const char *args[] = {g.tc.qbe, "-t", g.tc.target, src, "-o", assembly.p, NULL}; status = qbe_tool(args, log); }
        if (status == 0) {
            const char *args[38]; size_t a = 0;
            args[a++] = g.tc.assembler;
            for (size_t j = 0; j < g.tc.nas_flags; j++) args[a++] = g.tc.as_flags[j];
            args[a++] = "-o"; args[a++] = object.p; args[a++] = assembly.p; args[a] = NULL;
            status = qbe_tool(args, log);
        }
        free(src); free(assembly.p); objects[no++] = object.p; link[n++] = object.p;
    }
    link[n++] = g.tc.runtime_object;
    for (size_t i = 0; i < g.tc.nlink_after; i++) link[n++] = g.tc.link_after[i];
    link[n++] = "-o"; link[n++] = exe; link[n] = NULL;
    if (status == 0) status = no == 0 ? CINT_EXIT_INTERNAL : qbe_tool(link, log);
    for (size_t i = 0; i < no; i++) free(objects[i]);
    free(objects); free(link); return status;
}

/* ------------------------------------------------------------------------- */
/* The run cache.                                                             */

static char entry[4096];   /* <cache>/exe/<key>, once run_cache_lookup has computed the key */

int run_cacheable(void)
{
    return g.mode == MODE_RUN && g.receipt == NULL && g.width == NULL && !g.lib;
}

static const char *exe_name(void)
{
    return strcmp(g.tc.leg, "msvc") == 0 ? "program.exe" : "program";
}

char *run_cache_lookup(void)
{
    sbuf k = {0};
    char hex[65], rt_hex[65], buf[1200], *exe, *rec;
    uint8_t hash[32], *seen = NULL;
    size_t n = 0u;
    if (digest_file(g.tc.runtime_object, rt_hex) != 0) return NULL;
    sb_f(&k, "cint-run-cache-1\ncint %s\ntoolchain %s\nruntime %s\n", g.tc.executable_sha256, g.tc.file_hex, rt_hex);
    for (int i = 0; i < g.nmods; i++) {
        const cli_module *m = &g.mods[i];
        if (m->src != NULL) sb_f(&k, "module %s %s\n", module_key(m, buf, sizeof buf), m->hex);
        else sb_f(&k, "absent %d %s\n", m->where, m->rel);
    }
    if (k.p == NULL) return NULL;
    cint_sha256((const uint8_t *)k.p, k.n, hash);
    free(k.p);
    hex32(hash, hex);
    char *root = join(cache_root(), "exe"), *dir = join(root, hex);
    (void)snprintf(entry, sizeof entry, "%s", dir);
    free(root);
    free(dir);
    if (g.rebuild) return NULL;
    exe = join(entry, exe_name());
    rec = join(entry, "program.sha256");
    int hit = digest_file(exe, hex) == 0 && read_file(rec, &seen, &n) == 0 && n == 64u && memcmp(seen, hex, 64u) == 0;
    free(seen);
    free(rec);
    if (hit) return exe;
    free(exe);
    return NULL;
}

char *run_cache_store(const char *exe)
{
    char hex[65], *path, *rec;
    if (entry[0] == '\0' || cli_mkdirs(entry) != 0) return NULL;
    path = join(entry, exe_name());
    rec = join(entry, "program.sha256");
    /* rename replaces an older entry on POSIX; where it cannot (Windows), the old entry is removed and the
     * move tried again. If that also fails (an entry in use), the run keeps its own executable, and without
     * its record the entry is a miss next time, and is replaced. */
    if (rename(exe, path) != 0 && (remove(path) != 0 || rename(exe, path) != 0)) {
        free(path);
        path = NULL;
    } else if (digest_file(path, hex) == 0) {
        (void)cint_commit_output(rec, (const uint8_t *)hex, 64u, 64u);
    }
    free(rec);
    return path;
}
