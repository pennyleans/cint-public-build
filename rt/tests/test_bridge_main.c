/* test_bridge_main.c: command-line driver for rt/tests/test_bridge.py.
 *
 * Linked with rt/cint_bridge.c, rt/cint_build.c, rt/cint_rt.c, and the seed-built stub compiler
 * rt/tests/stub/main.ci. Paths and data arrive hex-encoded, so a UTF-8 path
 * (including non-BMP characters) reaches the bridge byte for byte on every
 * host, whatever the console code page.
 *
 *   test_bridge calls
 *       The four calls refuse a NULL table array, a table count other than
 *       CINT_BRIDGE_TABLES, and a NULL result; exit 0 when each does, else 3.
 *   test_bridge commit PATH DATA LIMIT [-i INPUT]... [-f STEP]
 *       PATH, INPUT: hex of the UTF-8 bytes. DATA: "hex:<bytes>" or
 *       "pat:<decimal length>" (byte i is (i * 31 + 7) mod 256). Each INPUT is
 *       read with cint_bridge_read_input under its directory first. -f STEP
 *       sets the injected failure step (hook builds only). Exit 0: the commit
 *       returned true; 1: false; 2: usage error or a refused input.
 *   test_bridge read ROOT REL LIMIT
 *       ROOT and REL hex. Prints "read <len> <sha256>" and exits 0, or exits 1.
 *   test_bridge build ROOT OUT [-f STEP] [-w K] [-k K] [-p] [-t K] [-l] REL...
 *       ROOT and OUT hex, each REL plain. Runs cint_build; -w fails the K-th
 *       staged file, -k exits 70 after it, -p prints "paused" after the inputs
 *       are read and waits for a line on stdin, -t sets the stage-directory
 *       tries to K. Prints "build <result>" and
 *       each retained diagnostic row; -l (hook builds) then prints "phases P C M E",
 *       the calls cint_build made of plan, compile, measure, and emit. The exit
 *       status is the result.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cint_bridge.h"

extern const cint_module_info cm_4_main;  /* the stub compiler's module descriptor */

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Decodes hex into a new NUL-terminated buffer; *len receives the byte count. */
static uint8_t *unhex(const char *s, size_t *len)
{
    size_t n = strlen(s);
    if (n % 2 != 0) return NULL;
    uint8_t *out = malloc(n / 2 + 1);
    if (out == NULL) return NULL;
    for (size_t i = 0; i < n / 2; i++) {
        int hi = hexval(s[2 * i]), lo = hexval(s[2 * i + 1]);
        if (hi < 0 || lo < 0) { free(out); return NULL; }
        out[i] = (uint8_t)(hi * 16 + lo);
    }
    out[n / 2] = 0;
    *len = n / 2;
    return out;
}

static bool parse_size(const char *s, size_t *out)
{
    size_t v = 0;
    if (*s == 0) return false;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return false;
        size_t d = (size_t)(*s - '0');
        if (v > (SIZE_MAX - d) / 10) return false;
        v = v * 10 + d;
    }
    *out = v;
    return true;
}

static int run_calls(void)
{
    cint_view v, t[CINT_BRIDGE_TABLES];
    int64_t result = 77;
    int bad = 0;
    memset(&v, 0, sizeof v);
    memset(t, 0, sizeof t);
    bad |= cint_plan(NULL, 0, v, v, v, NULL) != CINT_REFUSED;
    bad |= cint_compile(NULL, 0, 0, v, v, NULL, CINT_BRIDGE_TABLES, v, v, &result) != CINT_REFUSED;
    bad |= cint_compile(NULL, 0, 0, v, v, t, CINT_BRIDGE_TABLES - 1, v, v, &result) != CINT_REFUSED;
    bad |= cint_measure(NULL, 0, 0, t, CINT_BRIDGE_TABLES + 1, v, v, v, &result) != CINT_REFUSED;
    bad |= cint_emit(NULL, 0, 0, 0, t, CINT_BRIDGE_TABLES, v, v, v, NULL) != CINT_REFUSED;
    bad |= result != 77;
    printf("calls: %s\n", bad ? "mismatch" : "refused");
    return bad ? 3 : 0;
}

/* Splits a hex-encoded path at its last separator and reads it as an input. */
static bool read_path(const char *hex)
{
    size_t n = 0, len = 0;
    uint8_t *p = unhex(hex, &n), *bytes = NULL;
    cint_bridge_root *root = NULL;
    char *cut = p != NULL ? strrchr((char *)p, '/') : NULL;
#ifdef _WIN32
    char *back = p != NULL ? strrchr((char *)p, '\\') : NULL;
    if (back != NULL && (cut == NULL || back > cut)) cut = back;
#endif
    bool ok = cut != NULL;
    if (ok) *cut = '\0';
    ok = ok && cint_bridge_root_open((const char *)p, &root)
         && cint_bridge_read_input(root, cut + 1, (size_t)1 << 30, &bytes, &len);
    cint_bridge_free_input(bytes);
    cint_bridge_root_close(root);
    free(p);
    return ok;
}

static int run_commit(int argc, char **argv)
{
    size_t plen = 0, dlen = 0, limit = 0;
    uint8_t *path = unhex(argv[2], &plen);
    uint8_t *data = NULL;
    int status = 2;
    if (path == NULL || !parse_size(argv[4], &limit)) goto done;
    if (strncmp(argv[3], "hex:", 4) == 0) {
        data = unhex(argv[3] + 4, &dlen);
    } else if (strncmp(argv[3], "pat:", 4) == 0 && parse_size(argv[3] + 4, &dlen)) {
        data = malloc(dlen + 1);
        for (size_t i = 0; data != NULL && i < dlen; i++) data[i] = (uint8_t)((i * 31 + 7) % 256);
    }
    if (data == NULL) goto done;
    cint_bridge_clear_inputs();
    for (int i = 5; i < argc; i += 2) {
        if (i + 1 >= argc) goto done;
        if (strcmp(argv[i], "-i") == 0) {
            if (!read_path(argv[i + 1])) {
                fprintf(stderr, "read_input failed\n");
                goto done;
            }
        } else if (strcmp(argv[i], "-f") == 0) {
#ifdef CINT_BRIDGE_TEST_HOOKS
            cint_bridge_test_fail_step = atoi(argv[i + 1]);
#else
            fprintf(stderr, "-f needs a CINT_BRIDGE_TEST_HOOKS build\n");
            goto done;
#endif
        } else {
            goto done;
        }
    }
    bool ok = cint_commit_output((const char *)path, data, dlen, limit);
    printf("commit: %s\n", ok ? "true" : "false");
    status = ok ? 0 : 1;
done:
    free(path);
    free(data);
    return status;
}

static int run_read(char **argv)
{
    size_t n = 0, m = 0, limit = 0, len = 0;
    uint8_t *dir = unhex(argv[2], &n), *rel = unhex(argv[3], &m), *bytes = NULL, d[32];
    cint_bridge_root *root = NULL;
    bool ok = dir != NULL && rel != NULL && parse_size(argv[4], &limit) && cint_bridge_root_open((const char *)dir, &root)
              && cint_bridge_read_input(root, (const char *)rel, limit, &bytes, &len);
    if (ok) {
        cint_sha256(bytes, len, d);
        printf("read %zu ", len);
        for (int i = 0; i < 32; i++) printf("%02x", (unsigned)d[i]);
        printf("\n");
    }
    cint_bridge_free_input(bytes);
    cint_bridge_root_close(root);
    free(dir);
    free(rel);
    return ok ? 0 : 1;
}

#ifdef CINT_BRIDGE_TEST_HOOKS
static void pause_after_read(void)
{
    char line[16];
    printf("paused\n");
    fflush(stdout);
    if (fgets(line, sizeof line, stdin) == NULL) exit(4);
}
#endif

typedef struct diagnostic_paths {
    int fail, calls;
    uint8_t *bytes[CINT_BRIDGE_DIAG_ROWS];
    size_t sizes[CINT_BRIDGE_DIAG_ROWS];
} diagnostic_paths;

static bool retain_path(void *user, int64_t row, const uint8_t *bytes, size_t len)
{
    diagnostic_paths *paths = user;
    paths->calls++;
    if (paths->fail || row < 1 || row >= CINT_BRIDGE_DIAG_ROWS) return false;
    uint8_t *copy = malloc(len);
    if (copy == NULL) return false;
    memcpy(copy, bytes, len);
    free(paths->bytes[row]);
    paths->bytes[row] = copy;
    paths->sizes[row] = len;
    return true;
}

static int run_build(int argc, char **argv)
{
    size_t n = 0, m = 0;
    uint8_t *dir = unhex(argv[2], &n), *out = unhex(argv[3], &m);
    const char **rels = calloc((size_t)argc, sizeof *rels);
    int64_t count = 0;
    cint_bridge_root *root = NULL;
    cint_compiler_diag *diag = calloc(CINT_BRIDGE_DIAG_ROWS, sizeof *diag);
    uint8_t *faults = calloc(CINT_BRIDGE_FAULT_BYTES, 1);
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL;
    int res = 9, calls = 0, path_mode = 0;
    diagnostic_paths paths = {0};
    for (int i = 4; rels != NULL && i < argc; i++) {
        int k = i + 1 < argc ? atoi(argv[i + 1]) : 0;
        if (strcmp(argv[i], "-d") == 0) { path_mode = k; paths.fail = k == 2; i++; continue; }
#ifdef CINT_BRIDGE_TEST_HOOKS
        if (strcmp(argv[i], "-f") == 0) { cint_bridge_test_fail_step = k; i++; continue; }
        if (strcmp(argv[i], "-w") == 0) { cint_bridge_test_fail_write = k; i++; continue; }
        if (strcmp(argv[i], "-k") == 0) { cint_bridge_test_kill_after = k; i++; continue; }
        if (strcmp(argv[i], "-p") == 0) { cint_bridge_test_after_read = pause_after_read; continue; }
        if (strcmp(argv[i], "-t") == 0) { cint_bridge_test_stage_tries = (unsigned)k; i++; continue; }
        if (strcmp(argv[i], "-l") == 0) { calls = 1; continue; }
#else
        (void)k;
        (void)calls;
#endif
        rels[count++] = argv[i];
    }
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = cm_4_main.program;
    if (dir != NULL && out != NULL && rels != NULL && diag != NULL && faults != NULL
        && cint_ctx_create(&cfg, &ctx) == CINT_OK && cint_bridge_root_open((const char *)dir, &root)) {
        res = path_mode == 0 ? cint_build(ctx, root, rels, count, (const char *)out, diag, faults)
            : cint_build_with_paths(ctx, root, rels, count, (const char *)out, diag, faults, retain_path, &paths);
        printf("build %d\n", res);
        for (int64_t i = 1; res == CINT_BUILD_DIAG && i <= diag[0].detail[0] && i < CINT_BRIDGE_DIAG_ROWS; i++) {
            const cint_compiler_diag *d = &diag[i];
            printf("diag %lld %lld %lld %lld %lld %lld\n", (long long)d->code, (long long)d->module, (long long)d->line,
                   (long long)d->column, (long long)d->detail[0], (long long)d->detail[1]);
        }
        if (path_mode != 0) printf("paths %d\n", paths.calls);
        for (int i = 1; i < CINT_BRIDGE_DIAG_ROWS; i++) {
            if (paths.bytes[i] == NULL) continue;
            printf("path %d ", i);
            for (size_t j = 0; j < paths.sizes[i]; j++) printf("%02x", (unsigned)paths.bytes[i][j]);
            printf("\n");
        }
#ifdef CINT_BRIDGE_TEST_HOOKS
        if (calls)
            printf("phases %lld %lld %lld %lld\n", (long long)cint_bridge_test_phase_calls[1],
                   (long long)cint_bridge_test_phase_calls[2], (long long)cint_bridge_test_phase_calls[3],
                   (long long)cint_bridge_test_phase_calls[4]);
#endif
        if (res == CINT_BUILD_FAULT) {
            cint_fault_record f;
            printf("fault %u\n", cint_ctx_fault(ctx, &f) == CINT_OK ? (unsigned)f.code : 0u);
        }
    }
    cint_bridge_root_close(root);
    cint_ctx_destroy(ctx);
    free(dir);
    free(out);
    free((void *)rels);
    free(diag);
    free(faults);
    for (int i = 0; i < CINT_BRIDGE_DIAG_ROWS; i++) free(paths.bytes[i]);
    return res;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "calls") == 0) return run_calls();
    if (argc >= 5 && strcmp(argv[1], "commit") == 0) return run_commit(argc, argv);
    if (argc == 5 && strcmp(argv[1], "read") == 0) return run_read(argv);
    if (argc >= 5 && strcmp(argv[1], "build") == 0) return run_build(argc, argv);
    fprintf(stderr, "usage: test_bridge calls | commit PATH DATA LIMIT [-i INPUT]... [-f STEP] | read ROOT REL LIMIT"
                    " | build ROOT OUT [-f STEP] [-w K] [-k K] [-p] [-t K] [-l] REL...\n");
    return 2;
}
