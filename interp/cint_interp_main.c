/* cint_interp_main.c: cint-interp, the command of cpu-sir-interp (SPEC-09 BACK-01a, CONF-10a).
 *
 *   cint-interp [--trace PATH] observe DIR <the options of cint-harness>
 *   cint-interp [--trace PATH] run DIR [fault_dest [fuel_dest [state_dest]]] [--grant LINE]...
 *   cint-interp [--trace PATH] run --record [--boundary entry|dispatch] PATH DIR
 *   cint-interp replay [--stop K] [--echo] [--checkpoint PATH] [--receipt PATH]
 *                      [--source-identity HEX] FILE
 *   cint-interp show FILE
 *
 * DIR is a program's output set from `cint emit-c`: cint-program.sites and each module's
 * SIR text. `observe` is cint-harness with DIR in place of a program library (CONF-10a):
 * the harness's source is compiled into this file with its two calls into the platform
 * loader pointed at the interpreter, so the .expect text, the refusals, and the exit
 * statuses are the harness's own. `run` does what the C main of a compiled script or
 * main() program does: cint_program_run_args with unbounded fuel, the two record
 * destinations, a third for the state lines of a program with module state, which a program
 * without module state refuses (CINT_EXIT_USAGE), each `--grant` line, and the program's host
 * import tables (cint_sir_imports; I/O services lane U5c, rt/OPEN.md RT-OQ-75). `--trace`
 * writes one line per trace event to PATH. `show` prints a checkpoint through the decoder a
 * restore uses, or a recording; exit 3 for bytes it refuses (BX13-11).
 *
 * Box 13 unit 5 (rt/cint_state.h; SPEC-03 H-14 to H-18, SPEC-06 10.5, 12.2): `run --record`
 * runs the program entry as `run` does, without the record destinations, under a recorder
 * that writes the CINT-REC-1 recording to PATH, with each module's SIR text as its code.
 * `replay` loads the code a recording carries, checks its revision identity, and replays it
 * on a context made as `run` makes one: exit 0 when every boundary compares, 3 when replay
 * stops with DIVERGED or UNREPLAYABLE or the recording is refused; one line on standard error
 * says which. `--stop K` stops after boundary K, `--echo` writes the recorded print bytes to
 * standard output, `--checkpoint` writes a checkpoint of the context where replay stopped,
 * and `--receipt` writes the receipt of kind `replay` (receipt_replay below), whose
 * interpreter source identity is the `--source-identity` given, else null. Box 13 unit 7c:
 * `--boundary dispatch` on `run --record` or `run --case` records at `dispatch` density
 * (SPEC-06 1.5), and `--stop K` then counts dispatch boundaries too.
 *
 * A program whose text breaks SIR-10, or that uses a form this unit does not run yet, is
 * not run: `observe` exits 3, as the harness does for a library it cannot load, and `run`
 * exits 3 (CINT_EXIT_CHECK); standard error says why, with "unsupported:" for a form not
 * run yet. A malformed command line exits 4 (CINT_EXIT_USAGE). Status: Proposed, box 13
 * unit 2 (docs/design/notes/2026-10-05-box13-interpreter-checkpoints.md, section 9). */
#define _CRT_SECURE_NO_WARNINGS 1

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cint_interp.h"
#include "cint_state.h"

static cint_sir_program *g_prog;
static cint_sir_image *g_image;
static FILE *g_trace;
static char g_why[600];
static const uint32_t g_abi = CINT_ABI_VERSION;

static void put_event(void *user, const cint_sir_event *e)
{
    static const char *const kinds[4] = {"", "op", "call", "return"};
    FILE *f = (FILE *)user;
    fprintf(f, "%s %llu %u.%u %.*s", kinds[e->kind & 3u], (unsigned long long)e->step, e->site.module,
            e->site.index, (int)e->operation_len, e->operation != NULL ? e->operation : "");
    for (uint32_t k = 0; k < e->operand_count && k < 3u; k++)
        fprintf(f, " 0x%016llx", (unsigned long long)e->operands[k]);
    if (e->has_result) fprintf(f, " -> 0x%016llx", (unsigned long long)e->result);
    if (e->error != 0u) fprintf(f, " error %llu", (unsigned long long)e->error);
    fputc('\n', f);
}

static int why(int st, const cint_sir_error *err)
{
    static const char *const what[4] = {"", "malformed SIR", "resource", "unsupported"};
    (void)snprintf(g_why, sizeof g_why, "%s: %s:%u:%u: %s", what[st & 3], err->path, err->line, err->col, err->text);
    return st;
}

/* Loads and prepares the program in dir: CINT_SIR_OK, or the status with g_why set. */
static int load_dir(const char *dir)
{
    cint_sir_error err;
    int st = cint_sir_load(dir, &g_prog, &err);
    if (st == CINT_SIR_OK) st = cint_sir_prepare(g_prog, &g_image, &err);
    if (st != CINT_SIR_OK) {
        why(st, &err);
        cint_sir_free(g_prog);
        g_prog = NULL;
        return st;
    }
    if (g_trace != NULL) cint_sir_trace(g_image, put_event, g_trace);
    return st;
}

/* The exports of CONF-13 X-2 that a compiled program library has. */
static void *symbol(const char *name)
{
    if (strcmp(name, "cint_program_abi") == 0) return (void *)&g_abi;
    if (strcmp(name, "cint_observer_desc") == 0) return (void *)cint_sir_observer(g_image);
    if (strcmp(name, "cint_observer_state") == 0) return (void *)cint_sir_state_table(g_image);
    if (strcmp(name, "cint_imports_desc") == 0) return (void *)cint_sir_imports(g_image);
    return NULL;
}

#if defined(_WIN32)
static HMODULE sir_load_library(const wchar_t *path, HANDLE file, DWORD flags)
{
    static char dir[32768 * 3];
    (void)file;
    (void)flags;
    if (WideCharToMultiByte(CP_UTF8, 0, path, -1, dir, (int)sizeof dir, NULL, NULL) <= 0)
        return NULL;
    if (load_dir(dir) != CINT_SIR_OK) {
        fprintf(stderr, "cint-interp: %s\n", g_why);
        return NULL;
    }
    return (HMODULE)(void *)&g_image;
}

static FARPROC sir_get_proc(HMODULE lib, const char *name)
{
    void *p = symbol(name);
    FARPROC f;
    _Static_assert(sizeof f == sizeof p, "FARPROC is pointer-sized");
    (void)lib;
    memcpy(&f, &p, sizeof f);
    return f;
}
#define LoadLibraryExW sir_load_library
#define GetProcAddress sir_get_proc
#define wmain harness_wmain
#else
static void *sir_dlopen(const char *path, int flags)
{
    (void)flags;
    return load_dir(path) == CINT_SIR_OK ? (void *)&g_image : NULL;
}

static void *sir_dlsym(void *lib, const char *name)
{
    (void)lib;
    return symbol(name);
}

static char *sir_dlerror(void)
{
    return g_why;
}
#define dlopen sir_dlopen
#define dlsym sir_dlsym
#define dlerror sir_dlerror
#define main harness_main
#endif

#include "../harness/cint_harness.c"

#undef main
#undef wmain

/* The program arguments of `run --record`, `run --case` and `replay` (OQ-306; RT-OQ-76): each
 * `--arg TEXT` pair and every argument after `--`, taken out of argv in place, each its bytes
 * and one NUL in g_args. */
static uint8_t *g_args;
static size_t g_args_len;

static bool take_args(int *argc, char **argv)
{
    int n = 1, tail = *argc;
    size_t at = 0u;
    for (int i = 1; i < *argc; i++) {
        if (i < tail && strcmp(argv[i], "--") == 0) tail = i + 1;
        else if (i < tail && strcmp(argv[i], "--arg") == 0 && i + 1 < *argc) g_args_len += strlen(argv[++i]) + 1u;
        else if (i < tail) argv[n++] = argv[i];
        else g_args_len += strlen(argv[i]) + 1u;
    }
    if (g_args_len > 0u && (g_args = malloc(g_args_len)) == NULL) return false;
    tail = *argc;
    for (int i = 1; i < *argc; i++) {
        if (i < tail && strcmp(argv[i], "--") == 0) {
            tail = i + 1;   /* the separator itself is not an argument; the first loop did not count it */
            continue;
        }
        if (i < tail && strcmp(argv[i], "--arg") == 0 && i + 1 < *argc) i++;
        else if (i < tail) continue;
        memcpy(g_args + at, argv[i], strlen(argv[i]) + 1u);
        at += strlen(argv[i]) + 1u;
    }
    *argc = n;
    return true;
}

/* `run DIR [fault_dest [fuel_dest [state_dest]]]`, with `--grant LINE`, `--arg TEXT`,
 * `--frame-arena N` and a `--` tail anywhere after DIR, as a compiled program's C main
 * (compiler/back_c.ci; CINTC-OQ-63 for the state destination; cint_program_run_argv, RT-OQ-74 to
 * RT-OQ-76, RT-OQ-78). */
static int run_program(int argc, char **argv)
{
    int nd = 0;
    for (int i = 2; i < argc && strcmp(argv[i], "--") != 0; i++)
        nd += strcmp(argv[i], "--grant") == 0 || strcmp(argv[i], "--arg") == 0 || strcmp(argv[i], "--frame-arena") == 0
                  ? (i++, 0)
                  : 1;
    if (argc < 2 || nd > 3) {
        fprintf(stderr, "cint-interp: run takes DIR [fault_dest [fuel_dest [state_dest]]], --grant LINE, --arg TEXT, "
                        "--frame-arena N and -- ARG...\n");
        return CINT_EXIT_USAGE;
    }
    int st = load_dir(argv[1]);
    if (st != CINT_SIR_OK) {
        fprintf(stderr, "cint-interp: %s\n", g_why);
        return st == CINT_SIR_RESOURCE ? CINT_EXIT_ENVIRONMENT : CINT_EXIT_CHECK;
    }
    cint_main_fn entry = cint_sir_main(g_image);
    const cint_state_table *state = cint_sir_state_table(g_image);
    if (entry == NULL) {
        fprintf(stderr, "cint-interp: the root module has no script, main(), or single test to run\n");
        st = CINT_EXIT_USAGE;
    } else if (nd == 3 && state == NULL) {
        fprintf(stderr, "cint-interp: a program without module state takes no state_dest\n");
        st = CINT_EXIT_USAGE;
    } else {
        st = cint_program_run_argv(cint_sir_cint_program(g_image), entry, CINT_FUEL_UNBOUNDED, argc - 1, argv + 1,
                                   cint_sir_imports(g_image), state,
                                   cint_sir_main_args(g_image) ? CINT_PROGRAM_ARGS : 0u);
    }
    cint_sir_release(g_image);
    cint_sir_free(g_prog);
    return st;
}

/* ------------------------------------------------------------------------- */
/* Recordings and replay (box 13 unit 5).                                     */

static cint_status to_stdout(void *user, const uint8_t *bytes, size_t len)
{
    (void)user;
    return len == 0u || fwrite(bytes, 1u, len, stdout) == len ? CINT_OK : CINT_RESOURCE;
}

static cint_status to_file(void *user, const uint8_t *bytes, size_t len)
{
    return fwrite(bytes, 1u, len, (FILE *)user) == len ? CINT_OK : CINT_RESOURCE;
}

/* A context for the program as `run` makes one (SPEC-06 1.4), its print statements to
 * standard output, its host import tables and program arguments given and its module state bound. */
static cint_ctx *make_ctx(const cint_program *program, const cint_state_table *state, const cint_host_desc *imports)
{
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL;
#if defined(_WIN32)
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) return NULL;
#endif
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = program;
    cfg.output = to_stdout;
    cfg.imports = imports;
    cfg.args = g_args;
    cfg.args_length = g_args_len;
    if (cint_ctx_create(&cfg, &ctx) != CINT_OK) return NULL;
    if (cint_ctx_bind_state(ctx, state) != CINT_OK) {
        cint_ctx_destroy(ctx);
        return NULL;
    }
    return ctx;
}

static uint8_t *read_all(const char *path, size_t *len);
static void hex(char *out, const uint8_t *b, size_t n);

/* The code of the loaded program, its count in *count; NULL when memory runs out. */
static cint_code_module *code_of(uint32_t *count)
{
    uint32_t functions, nodes;
    cint_code_module *code;
    cint_sir_counts(g_prog, count, &functions, &nodes);
    code = (cint_code_module *)calloc((size_t)*count + 1u, sizeof *code);
    for (uint32_t k = 0u; code != NULL && k < *count; k++) {
        cint_sir_text t;
        cint_sir_text_of(g_prog, k, &t);
        code[k].path.bytes = t.path;
        code[k].path.len = (uint32_t)t.path_len;
        code[k].sir = t.text;
        code[k].sir_len = t.len;
    }
    return code;
}

/* Reads each module's source from src, at its module-relative path: false, with the message
 * written, when one cannot be read. */
static bool read_sources(cint_code_module *code, uint32_t count, const char *src)
{
    for (uint32_t k = 0u; k < count; k++) {
        size_t n = strlen(src) + code[k].path.len + 2u;
        char *p = (char *)malloc(n);
        if (p != NULL) {
            snprintf(p, n, "%s/%.*s", src, (int)code[k].path.len, code[k].path.bytes);
            code[k].source = read_all(p, &code[k].source_len);
        }
        if (p == NULL || code[k].source == NULL) {
            fprintf(stderr, "cint-interp: cannot read the source %s/%.*s\n", src, (int)code[k].path.len,
                    code[k].path.bytes);
            free(p);
            return false;
        }
        free(p);
    }
    return true;
}

/* `identity DIR`: the revision identity of the program's SIR text and its source-map digest
 * (SPEC-09 SIR-17): exit 0; 3 for text that does not load and 5 for text that cannot be
 * read, as `run`. */
static int identity(const char *dir)
{
    uint32_t count = 0u;
    cint_code_module *code;
    uint8_t rev[32], map[32];
    char h[65];
    cint_status st = CINT_RESOURCE;
    int status = load_dir(dir);
    if (status != CINT_SIR_OK) {
        fprintf(stderr, "cint-interp: %s\n", g_why);
        return status == CINT_SIR_RESOURCE ? CINT_EXIT_ENVIRONMENT : CINT_EXIT_CHECK;
    }
    if ((code = code_of(&count)) != NULL && (st = cint_code_source_map(code, count, map)) != CINT_RESOURCE) {
#if defined(_WIN32)
        (void)_setmode(_fileno(stdout), _O_BINARY);  /* no CR */
#endif
        cint_sir_revision(g_prog, rev);
        hex(h, rev, 32u);
        printf("revision %s\n", h);
        hex(h, map, 32u);
        printf("source-map %s\n", st == CINT_OK ? h : "absent");
    }
    status = st == CINT_RESOURCE || fflush(stdout) != 0 || ferror(stdout) != 0 ? CINT_EXIT_ENVIRONMENT : CINT_EXIT_OK;
    free(code);
    cint_sir_release(g_image);
    cint_sir_free(g_prog);
    return status;
}

/* `run --record PATH DIR`, and `run --case PATH --source SRC DIR` with each module's source
 * read from SRC, at the density of `density` (0 or CINT_RECORD_DISPATCH): exit 0, 1 (a fault),
 * 6 (an error result), as `run`; 5 when the recording or standard output cannot be written, or
 * a source cannot be read. */
static int record_program(const char *path, const char *dir, const char *src, uint32_t density)
{
    uint32_t count = 0u;
    cint_code_module *code;
    cint_recorder *r = NULL;
    cint_ctx *ctx = NULL;
    cint_main_fn entry;
    cint_sink sink;
    cint_status st = CINT_RESOURCE, end = CINT_RESOURCE;
    FILE *f = NULL;
    bool unread = false;
    int status = load_dir(dir);
    if (status != CINT_SIR_OK) {
        fprintf(stderr, "cint-interp: %s\n", g_why);
        return status == CINT_SIR_RESOURCE ? CINT_EXIT_ENVIRONMENT : CINT_EXIT_CHECK;
    }
    if ((entry = cint_sir_main(g_image)) == NULL) {
        fprintf(stderr, "cint-interp: the root module has no script, main(), or single test to run\n");
        cint_sir_release(g_image);
        cint_sir_free(g_prog);
        return CINT_EXIT_USAGE;
    }
    code = code_of(&count);
    if (code != NULL && src != NULL && !read_sources(code, count, src)) {
        unread = true;
    } else if (code != NULL &&
               (ctx = make_ctx(cint_sir_cint_program(g_image), cint_sir_state_table(g_image),
                               cint_sir_imports(g_image))) != NULL &&
               (f = fopen(path, "wb")) != NULL) {
        sink.write = to_file;
        sink.user = f;
        if (cint_rec_begin(ctx, cint_sir_observer(g_image), code, count,
                           (src != NULL ? CINT_RECORD_CASE : CINT_RECORD_FULL) | density, &sink, &r) == CINT_OK) {
            st = cint_rec_main(r, entry, CINT_FUEL_UNBOUNDED);
            end = cint_rec_end(r);
        }
    }
    status = unread || fflush(stdout) != 0 || ferror(stdout) != 0 || end != CINT_OK ? CINT_EXIT_ENVIRONMENT
                                                                                 : CINT_EXIT_OK;
    if (f != NULL && fclose(f) != 0) status = CINT_EXIT_ENVIRONMENT;
    if (status == CINT_EXIT_ENVIRONMENT) {
        if (!unread)
            fprintf(stderr, "cint-interp: cannot write the recording %s or standard output\n", path);
    } else if (st == CINT_OK) {
        const cint_error_set *set = NULL;
        uint64_t tag = 0u;
        status = cint_ctx_error(ctx, &set, &tag) != CINT_OK ? CINT_EXIT_INTERNAL
                 : set != NULL                              ? CINT_EXIT_ERROR_VALUE
                                                            : CINT_EXIT_OK;
    } else {
        status = st == CINT_FAULT ? CINT_EXIT_FAULT : CINT_EXIT_INTERNAL;
    }
    cint_ctx_destroy(ctx);
    for (uint32_t k = 0u; code != NULL && k < count; k++) free((void *)code[k].source);
    free(code);
    cint_sir_release(g_image);
    cint_sir_free(g_prog);
    return status;
}

#define REVISIONS_MAX 64u

/* The programs a replay loaded: the first revision's, then one per `revision` event. */
typedef struct replay_host {
    const cint_recording_info *info;
    cint_sir_program *prog[REVISIONS_MAX];
    cint_sir_image *image[REVISIONS_MAX];
    uint32_t count;
} replay_host;

/* Loads and prepares the code of revision event e (NULL: the first revision): CINT_SIR_OK,
 * or the status with g_why set. */
static int load_code(replay_host *h, const cint_recording_event *e, uint32_t n)
{
    cint_sir_text *mods = (cint_sir_text *)calloc((size_t)n + 1u, sizeof *mods);
    cint_sir_error err;
    int st = mods != NULL && h->count < REVISIONS_MAX ? CINT_SIR_OK : CINT_SIR_RESOURCE;
    memset(&err, 0, sizeof err);
    for (uint32_t k = 0u; st == CINT_SIR_OK && k < n; k++) {
        cint_code_module m;
        memset(&m, 0, sizeof m);
        st = cint_recording_module(h->info, e, k, &m) == CINT_OK ? CINT_SIR_OK : CINT_SIR_INPUT;
        mods[k].path = m.path.bytes;
        mods[k].path_len = m.path.len;
        mods[k].text = m.sir;
        mods[k].len = m.sir_len;
    }
    if (st == CINT_SIR_OK && (st = cint_sir_load_text(mods, n, &h->prog[h->count], &err)) == CINT_SIR_OK) {
        if ((st = cint_sir_prepare(h->prog[h->count], &h->image[h->count], &err)) != CINT_SIR_OK) {
            cint_sir_free(h->prog[h->count]);
        } else {
            h->count++;
        }
    }
    free(mods);
    if (st != CINT_SIR_OK && err.text[0] != '\0') return why(st, &err);
    if (st != CINT_SIR_OK)
        (void)snprintf(g_why, sizeof g_why, "the recording's code cannot be loaded");
    return st;
}

static const cint_observer *next_revision(void *user, const cint_recording_event *e, cint_main_fn *main_fn,
                                          const cint_state_table **table)
{
    replay_host *h = (replay_host *)user;
    if (load_code(h, e, e->module_count) != CINT_SIR_OK) {
        fprintf(stderr, "cint-interp: the revision of event %llu: %s\n", (unsigned long long)e->index, g_why);
        return NULL;
    }
    *main_fn = cint_sir_main(h->image[h->count - 1u]);
    *table = cint_sir_state_table(h->image[h->count - 1u]);
    return cint_sir_observer(h->image[h->count - 1u]);
}

static void hex(char *out, const uint8_t *b, size_t n)
{
    for (size_t i = 0u; i < n; i++) (void)snprintf(out + 2u * i, 3u, "%02x", b[i]);
}

typedef struct jb {
    char *p;
    size_t n, cap;
    int bad;
} jb;

static void jb_s(jb *b, const char *s)
{
    size_t n = strlen(s);
    if (b->n + n + 1u > b->cap) {
        size_t cap = (b->cap + n + 1u) * 2u;
        char *q = (char *)realloc(b->p, cap);
        if (q == NULL) {
            b->bad = 1;
            return;
        }
        b->p = q;
        b->cap = cap;
    }
    memcpy(b->p + b->n, s, n + 1u);
    b->n += n;
}

static void jb_hex(jb *b, const uint8_t h[32])
{
    char x[65];
    hex(x, h, 32u);
    jb_s(b, "\"");
    jb_s(b, x);
    jb_s(b, "\"");
}

static void jb_u(jb *b, unsigned long long v)
{
    char x[32];
    (void)snprintf(x, sizeof x, "\"%llu\"", v);
    jb_s(b, x);
}

/* The receipt of kind `replay` (SPEC-06 12.2; box 13 default BX13-13), canonical JSON
 * (SPEC-09 RCPT-02) ending in LF. `identity` and `outcome` hold nothing specific to a
 * machine (RCPT-03): the recording, its revision, the method and the tool; the status, the
 * event, the boundaries, the chain head, the final state hash, the SHA-256 of the last fault
 * record of a boundary replayed, and the fuel consumed by each. */
static int receipt_replay(const char *path, const char *tool, const uint8_t *bytes, size_t len,
                          const cint_recording_info *info, const cint_replay_report *rep, uint64_t stop,
                          const uint8_t state[32], long long ns)
{
    static const char *const status[8] = {"ok", "", "", "", "", "", "diverged", "unreplayable"};
    jb id = {0}, out = {0}, all = {0}, rec = {0}, fuel = {0};
    cint_recording_event e;
    uint8_t h[32], fault[32];
    uint64_t seen = 0u;
    bool faulted = false;
    FILE *f = NULL;
    int ok;
    cint_sha256(bytes, len, h);
    jb_s(&id, "{\"backend\":{\"id\":\"cpu-sir-interp\"},");
    if (info->is_case) {   /* keys in byte order (canonical JSON) */
        jb_s(&id, "\"case\":{\"bytes\":");
        jb_u(&id, len);
        jb_s(&id, ",\"sha256\":");
        jb_hex(&id, h);
        jb_s(&id, "},");
    }
    jb_s(&id, "\"identities\":{\"revision\":");
    jb_hex(&id, info->revision);
    jb_s(&id, ",\"source_map\":");
    if (info->has_map) {
        jb_hex(&id, info->map);
    } else {
        jb_s(&id, "null");
    }
    jb_s(&id, "},\"kind\":\"replay\",\"method\":{\"density\":");
    jb_s(&id, info->density == 1u ? "\"dispatch\"" : "\"entry\"");
    jb_s(&id, ",\"fuel_model\":\"fuel-v1\",\"mode\":");
    jb_s(&id, info->mode == CINT_RECORD_DIGEST ? "\"digest\"" : "\"full\"");
    jb_s(&id, ",\"state_hash_interval\":");
    jb_u(&id, info->interval);
    jb_s(&id, ",\"stop\":");
    jb_u(&id, stop);
    jb_s(&id, "},\"profile\":\"cint-core-1\",");
    if (!info->is_case) {
        jb_s(&id, "\"recording\":{\"bytes\":");
        jb_u(&id, len);
        jb_s(&id, ",\"sha256\":");
        jb_hex(&id, h);
        jb_s(&id, "},");
    }
    jb_s(&id, "\"runtime_contract\":\"");
    jb_s(&id, CINT_RT_CONTRACT);
    jb_s(&id, "\",\"schema\":\"cint-receipt-1/replay\",\"tool\":{\"interpreter_source_identity\":");
    jb_s(&id, tool != NULL ? "\"" : "null");
    jb_s(&id, tool != NULL ? tool : "");
    jb_s(&id, tool != NULL ? "\"}}" : "}}");
    jb_s(&fuel, "[");
    memset(&e, 0, sizeof e);
    while (seen < rep->boundaries && cint_recording_next(info, &e) == CINT_OK) {
        if (e.kind == CINT_EVENT_OUTCOME && e.has_hash) {
            jb_s(&fuel, seen++ > 0u ? "," : "");
            jb_u(&fuel, (unsigned long long)e.consumed);
            if (e.data != NULL) {
                cint_sha256(e.data, e.data_len, fault);
                faulted = true;
            }
        }
    }
    jb_s(&fuel, "]");
    jb_s(&out, "{\"boundaries\":");
    jb_u(&out, rep->boundaries);
    jb_s(&out, ",\"chain_head\":");
    jb_hex(&out, rep->chain);
    jb_s(&out, ",\"event\":");
    jb_u(&out, rep->event);
    jb_s(&out, ",\"fault_sha256\":");
    if (faulted) {
        jb_hex(&out, fault);
    } else {
        jb_s(&out, "null");
    }
    jb_s(&out, ",\"fuel_consumed\":");
    jb_s(&out, fuel.p != NULL ? fuel.p : "");
    jb_s(&out, ",\"state_sha256\":");
    jb_hex(&out, state);
    jb_s(&out, ",\"status\":\"");
    jb_s(&out, status[rep->status & 7]);
    jb_s(&out, "\",\"what\":");
    jb_s(&out, rep->what != 0u ? "\"" : "null");
    jb_s(&out, rep->what != 0u ? cint_replay_what(rep->what) : "");
    jb_s(&out, rep->what != 0u ? "\"}" : "}");
    jb_s(&all, "{\"identity\":");
    jb_s(&all, id.p != NULL ? id.p : "");
    jb_s(&all, ",\"outcome\":");
    jb_s(&all, out.p != NULL ? out.p : "");
    jb_s(&all, "}");
    if (all.p != NULL) cint_sha256((const uint8_t *)all.p, all.n, h);
    jb_s(&rec, "{\"identity\":");
    jb_s(&rec, id.p != NULL ? id.p : "");
    jb_s(&rec, ",\"observations\":{\"limits\":\"The executables' digests are not recorded yet (RT-OQ-44).\","
               "\"machine\":{\"os\":\"");
#if defined(_WIN32)
    jb_s(&rec, "windows");
#elif defined(__APPLE__)
    jb_s(&rec, "macos");
#elif defined(__linux__)
    jb_s(&rec, "linux");
#else
    jb_s(&rec, "other");
#endif
    jb_s(&rec, "\"},\"receipt_identity_sha256\":");
    jb_hex(&rec, h);
    jb_s(&rec, ",\"timings\":{\"replay_ns\":");
    jb_u(&rec, (unsigned long long)ns);
    jb_s(&rec, "}},\"outcome\":");
    jb_s(&rec, out.p != NULL ? out.p : "");
    jb_s(&rec, "}\n");
    ok = !(id.bad | out.bad | all.bad | rec.bad | fuel.bad) && (f = fopen(path, "wb")) != NULL;
    if (ok) {
        ok = fwrite(rec.p, 1u, rec.n, f) == rec.n;
        ok = fclose(f) == 0 && ok;
    }
    free(id.p);
    free(out.p);
    free(all.p);
    free(rec.p);
    free(fuel.p);
    return ok ? CINT_EXIT_OK : CINT_EXIT_ENVIRONMENT;
}

static long long now_ns(void)
{
    struct timespec t;
    return timespec_get(&t, TIME_UTC) == TIME_UTC ? (long long)t.tv_sec * 1000000000ll + t.tv_nsec : 0ll;
}

/* The whole file at path, or NULL. */
static uint8_t *read_all(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *data = NULL;
    size_t cap = 0u, n = 0u, got = 1u;
    while (f != NULL && got > 0u) {
        if (n == cap) {
            uint8_t *q = cap < ((size_t)1 << 30) ? (uint8_t *)realloc(data, cap = cap * 2u + 65536u) : NULL;
            if (q == NULL) break;
            data = q;
        }
        n += got = fread(data + n, 1u, cap - n, f);
    }
    if (f == NULL || got > 0u || ferror(f) != 0) {  /* got > 0: larger than 1 GiB, or out of memory */
        free(data);
        data = NULL;
    }
    if (f != NULL) fclose(f);
    *len = n;
    return data;
}

/* `replay [--stop K] [--echo] [--checkpoint PATH] [--receipt PATH] [--source-identity HEX] FILE` */
static int replay_recording(int argc, char **argv)
{
    static const char digits[] = "0123456789abcdef";
    const char *ckpt = NULL, *receipt = NULL, *tool = NULL;
    cint_replay_config cfg;
    cint_replay_report rep;
    cint_recording_info info;
    replay_host host;
    cint_ctx *ctx = NULL;
    uint64_t stop = 0u;
    uint8_t *bytes, revision[32], state[32];
    size_t len = 0u;
    long long t0;
    int k = 1, status;
    char chain[65];
    memset(&cfg, 0, sizeof cfg);
    memset(&rep, 0, sizeof rep);
    memset(state, 0, sizeof state);
    for (; k < argc - 1; k++) {   /* the last argument is FILE */
        const char *v = k + 2 < argc ? argv[k + 1] : NULL;
        if (strcmp(argv[k], "--echo") == 0) {
            cfg.echo = 1u;
            continue;
        }
        if (v != NULL && strcmp(argv[k], "--stop") == 0 && strspn(v, "0123456789") == strlen(v) && v[0] != '0' &&
            strlen(v) < 20u) {
            stop = strtoull(v, NULL, 10);
        } else if (v != NULL && strcmp(argv[k], "--checkpoint") == 0) {
            ckpt = v;
        } else if (v != NULL && strcmp(argv[k], "--receipt") == 0) {
            receipt = v;
        } else if (v != NULL && strcmp(argv[k], "--source-identity") == 0 && strlen(v) == 64u &&
                   strspn(v, digits) == 64u) {
            tool = v;
        } else {
            break;
        }
        k++;
    }
    if (k != argc - 1) {
        fprintf(stderr, "cint-interp: replay takes [--stop K] [--echo] [--checkpoint PATH] [--receipt PATH] "
                        "[--source-identity HEX] FILE\n");
        return CINT_EXIT_USAGE;
    }
    if ((bytes = read_all(argv[k], &len)) == NULL) {
        fprintf(stderr, "cint-interp: %s cannot be read\n", argv[k]);
        return CINT_EXIT_ENVIRONMENT;
    }
    memset(&host, 0, sizeof host);
    host.info = &info;
    if (cint_recording_decode(bytes, len, &info) != CINT_OK) {
        fprintf(stderr, "cint-interp: %s is not a complete recording\n", argv[k]);
        free(bytes);
        return CINT_EXIT_CHECK;
    }
    if ((status = load_code(&host, NULL, info.module_count)) != CINT_SIR_OK) {
        fprintf(stderr, "cint-interp: the recording's code: %s\n", g_why);
        free(bytes);
        return status == CINT_SIR_RESOURCE ? CINT_EXIT_ENVIRONMENT : CINT_EXIT_CHECK;
    }
    cint_sir_revision(host.prog[0], revision);
    status = CINT_EXIT_CHECK;
    if (memcmp(revision, info.revision, 32u) != 0) {
        fprintf(stderr, "cint-interp: the recording's code does not have the recording's revision identity\n");
    } else if ((ctx = make_ctx(cint_sir_cint_program(host.image[0]), cint_sir_state_table(host.image[0]),
                               cint_sir_imports(host.image[0]))) == NULL) {
        status = CINT_EXIT_ENVIRONMENT;
    } else {
        cint_status st;
        cfg.size = (uint32_t)sizeof cfg;
        cfg.stop = stop;
        cfg.entries = cint_sir_observer(host.image[0]);
        cfg.main_fn = cint_sir_main(host.image[0]);
        cfg.revision = next_revision;
        cfg.user = &host;
        t0 = now_ns();
        st = cint_replay(ctx, &info, &cfg, &rep);
        t0 = now_ns() - t0;
        if (fflush(stdout) != 0 || ferror(stdout) != 0 || (st == CINT_OK && cint_ctx_state_hash(ctx, state) != CINT_OK))
            st = CINT_RESOURCE;
        if (st != CINT_OK) {
            fprintf(stderr, "cint-interp: %s\n", st == CINT_REFUSED ? "the context refused the recording's initial state"
                                                                    : "out of memory, or standard output failed");
            status = st == CINT_REFUSED ? CINT_EXIT_CHECK : CINT_EXIT_ENVIRONMENT;
        } else {
            hex(chain, rep.chain, 32u);
            if (rep.status == CINT_OK) {
                fprintf(stderr, "replay: ok, boundaries %llu, chain %s\n", (unsigned long long)rep.boundaries, chain);
            } else {
                fprintf(stderr, "replay: %s at event %llu%s%s%s, boundaries %llu\n",
                        rep.status == CINT_DIVERGED ? "diverged" : "unreplayable", (unsigned long long)rep.event,
                        rep.what != 0u ? " (" : "", rep.what != 0u ? cint_replay_what(rep.what) : "",
                        rep.what != 0u ? ")" : "", (unsigned long long)rep.boundaries);
            }
            status = rep.status == CINT_OK ? CINT_EXIT_OK : CINT_EXIT_CHECK;
            if (ckpt != NULL) {
                FILE *f = fopen(ckpt, "wb");
                cint_sink sink;
                sink.write = to_file;
                sink.user = f;
                bool written = f != NULL && cint_ctx_checkpoint(ctx, &sink) == CINT_OK;
                if (f != NULL && fclose(f) != 0) written = false;
                if (!written) {
                    fprintf(stderr, "cint-interp: cannot write the checkpoint %s\n", ckpt);
                    status = CINT_EXIT_ENVIRONMENT;
                }
            }
            if (receipt != NULL && status != CINT_EXIT_ENVIRONMENT &&
                receipt_replay(receipt, tool, bytes, len, &info, &rep, stop, state, t0) != CINT_EXIT_OK) {
                fprintf(stderr, "cint-interp: cannot write the receipt %s\n", receipt);
                status = CINT_EXIT_ENVIRONMENT;
            }
        }
    }
    cint_ctx_destroy(ctx);
    while (host.count > 0u) {
        host.count--;
        cint_sir_release(host.image[host.count]);
        cint_sir_free(host.prog[host.count]);
    }
    free(bytes);
    return status;
}

/* An optional `--boundary entry|dispatch` at argv[*j], stepped over: false for another density. */
static bool boundary_arg(int argc, char **argv, int *j, uint32_t *density)
{
    *density = 0u;
    if (argc - *j < 2 || strcmp(argv[*j], "--boundary") != 0) return true;
    *j += 2;
    *density = strcmp(argv[*j - 1], "dispatch") == 0 ? CINT_RECORD_DISPATCH : 0u;
    return *density != 0u || strcmp(argv[*j - 1], "entry") == 0;
}

static int interp(int argc, char **argv)
{
    int k = 1;
    if (argc > 2 && strcmp(argv[1], "--trace") == 0) {
        g_trace = fopen(argv[2], "wb");
        if (g_trace == NULL) {
            fprintf(stderr, "cint-interp: cannot write the trace file\n");
            return CINT_EXIT_ENVIRONMENT;
        }
        k = 3;
    }
    int st = CINT_EXIT_USAGE;
    if (argc - k >= 2 && ((strcmp(argv[k], "run") == 0 && (strcmp(argv[k + 1], "--record") == 0 ||
                                                           strcmp(argv[k + 1], "--case") == 0)) ||
                          (k == 1 && strcmp(argv[1], "replay") == 0)) &&
        !take_args(&argc, argv)) {   /* the program arguments of a recording or a digest replay */
        fprintf(stderr, "cint-interp: out of memory\n");
        return CINT_EXIT_ENVIRONMENT;
    }
    if (k < argc && strcmp(argv[k], "observe") == 0) {
        st = run(argc - k, argv + k);   /* the harness; argv[k] stands for its own name */
    } else if (argc - k >= 2 && strcmp(argv[k], "run") == 0 && strcmp(argv[k + 1], "--record") == 0) {
        int j = k + 2;
        uint32_t density = 0u;
        if (boundary_arg(argc, argv, &j, &density) && argc - j == 2) {
            st = record_program(argv[j], argv[j + 1], NULL, density);
        } else {
            fprintf(stderr, "cint-interp: run --record takes [--boundary entry|dispatch] PATH DIR\n");
        }
    } else if (argc - k >= 2 && strcmp(argv[k], "run") == 0 && strcmp(argv[k + 1], "--case") == 0) {
        int j = k + 2;
        uint32_t density = 0u;
        if (boundary_arg(argc, argv, &j, &density) && argc - j == 4 && strcmp(argv[j + 1], "--source") == 0) {
            st = record_program(argv[j], argv[j + 3], argv[j + 2], density);
        } else {
            fprintf(stderr, "cint-interp: run --case takes [--boundary entry|dispatch] PATH --source SRC DIR\n");
        }
    } else if (k < argc && strcmp(argv[k], "run") == 0) {
        st = run_program(argc - k, argv + k);
    } else if (k == 1 && argc >= 3 && strcmp(argv[1], "replay") == 0) {
        st = replay_recording(argc - 1, argv + 1);
    } else if (k == 1 && argc == 3 && strcmp(argv[1], "identity") == 0) {
        st = identity(argv[2]);
    } else if (k == 1 && argc == 3 && strcmp(argv[1], "show") == 0) {
        if ((st = cint_checkpoint_show(argv[2])) != CINT_EXIT_OK) {   /* BX13-11 */
            fprintf(stderr, "cint-interp: %s %s\n", argv[2],
                    st == CINT_EXIT_CHECK ? "is not a checkpoint that a restore accepts, nor a complete recording"
                                          : "cannot be read");
        }
    } else {
        fprintf(stderr, "usage: cint-interp [--trace PATH] observe DIR <cint-harness options>\n"
                        "       cint-interp [--trace PATH] run DIR [fault_dest [fuel_dest [state_dest]]]\n"
                        "                                  [--grant LINE]... [--frame-arena N] [-- ARG...]\n"
                        "       cint-interp [--trace PATH] run --record [--boundary entry|dispatch] PATH DIR\n"
                        "       cint-interp [--trace PATH] run --case [--boundary entry|dispatch] PATH\n"
                        "                                  --source SRC DIR\n"
                        "       cint-interp replay [--stop K] [--echo] [--checkpoint PATH] [--receipt PATH]\n"
                        "                          [--source-identity HEX] FILE\n"
                        "       cint-interp show FILE\n"
                        "       cint-interp identity DIR\n");
    }
    if (g_trace != NULL && fclose(g_trace) != 0) {
        fprintf(stderr, "cint-interp: cannot write the trace file\n");
        st = st == 0 ? CINT_EXIT_ENVIRONMENT : st;
    }
    return st;
}

#if defined(_WIN32)
/* Windows passes the command line as UTF-16; the interpreter works in UTF-8. */
int wmain(int argc, wchar_t **wargv)
{
    char **argv = (char **)calloc((size_t)argc + 1u, sizeof *argv);
    int i, status;
    if (argv == NULL) return CINT_EXIT_ENVIRONMENT;
    for (i = 0; i < argc; i++) {
        int n = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, NULL, 0, NULL, NULL);
        if (n <= 0 || (argv[i] = (char *)malloc((size_t)n)) == NULL) break;
        (void)WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, argv[i], n, NULL, NULL);
    }
    status = i == argc ? interp(argc, argv) : CINT_EXIT_USAGE;
    for (i = 0; i < argc; i++) free(argv[i]);
    free(argv);
    return status;
}
#else
int main(int argc, char **argv)
{
    return interp(argc, argv);
}
#endif
