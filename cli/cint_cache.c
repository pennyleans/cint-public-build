/* cint_cache.c: the toolchain file, and the executables `cint run` keeps for reuse (OQ-313;
 * SPEC-06 3.4, 4.2; issue #457).
 *
 * The toolchain file (default: cint.toolchain beside this executable) is `cint-toolchain-1`, then
 * `key value` lines, written by tools/cint_bootstrap.py.
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

static int by_basename(const void *a, const void *b)
{
    return strcmp(basename_utf8(*(char *const *)a), basename_utf8(*(char *const *)b));
}

static void hex32(const uint8_t hash[32], char hex[65])
{
    for (int i = 0; i < 32; i++) (void)snprintf(hex + 2 * i, 3, "%02x", (unsigned)hash[i]);
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
        } else {
            static const char *const keys[] = {"leg", "cc", "cc_version", "include", "runtime_object", "cache", "host",
                                               "compiler_sources", "executable_sha256", "runtime_library_object"};
            char **slots[] = {&t->leg, &t->cc, &t->cc_version, &t->include, &t->runtime_object, &t->cache, &t->host,
                              &t->compiler_sources, &t->executable_sha256, &t->runtime_library_object};
            for (size_t k = 0; k < sizeof keys / sizeof keys[0]; k++) {
                if (strcmp(line, keys[k]) == 0) *slots[k] = val;
            }
        }
    }
    qsort(t->rt_sources, t->nrt, sizeof t->rt_sources[0], by_basename);
    return t->leg != NULL && t->cc != NULL && t->include != NULL && t->runtime_object != NULL && t->cache != NULL &&
                   t->compiler_sources != NULL && t->executable_sha256 != NULL &&
                   strlen(t->executable_sha256) == 64u && t->nrt > 0u && t->nrt <= CLI_MAX_RT && t->nflags > 0u
               ? 0
               : -1;
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
    char *root = join(g.cache != NULL ? g.cache : g.tc.cache, "exe"), *dir = join(root, hex);
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
