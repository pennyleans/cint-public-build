/* rec_host.c: recordings and replay on cpu-sir-interp and cpu-c17 (box 13 unit 5; SPEC-03
 * H-14 to H-18; SPEC-06 10.5). Test driver of interp/tests/test_interp.py.
 *
 *   rec_host DIR OUT [-i] [-c DIR2] [-d]   built with REC_HOST_SIR: records the steps below on
 *                                          the program in DIR on the interpreter, with DIR's
 *                                          SIR text as the recording's code
 *   rec_host - OUT [-d] PATH=FILE...       linked with the program's emitted C and built with
 *                                          REC_HOST_MAIN, its program entry: cpu-c17, with the
 *                                          files given, in program order, as the code
 *   rec_host DIR|- -r REC [STOP]           replays REC on this backend
 *
 * The program is interp/tests/rec/ledger.ci. The steps, each printing one line after the
 * program's own print statements: deposit 5; a checkpoint; deposit 7 with fuel 100; burn
 * 1000 with fuel 50 (E_FUEL); clear; deposit I64.max (E_OVERFLOW); clear; restore; the
 * program entry with fuel 100; with -i, burn 40, which the trace callback interrupts at its
 * 20th event (the interpreter only); with -c, a change to the revision in DIR2 and deposit 2;
 * then the end. Without -i and -c the recording is the same, byte for byte, on both backends.
 * With -d (box 13 unit 7c; SPEC-06 1.5) the recording is at `dispatch` density and its only
 * step is the program entry with fuel 1000, on any program (interp/tests/density/); the
 * interpreter build then writes its last dispatch's domain to stderr (cint_sir_dispatch_domain).
 * Replay prints one line: the report's status, what, event, boundaries and chain head, and
 * the state hash of the context where it stopped. */
#define _CRT_SECURE_NO_WARNINGS 1  /* fopen */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cint_rt.h"
#include "cint_state.h"
#if defined(REC_HOST_SIR)
#include "../cint_interp.h"
#else
extern const cint_observer cint_observer_desc;
extern const cint_state_table cint_observer_state;
cint_status REC_HOST_MAIN(cint_ctx *ctx, int64_t fuel);
#endif

static cint_status to_file(void *user, const uint8_t *bytes, size_t len)
{
    return fwrite(bytes, 1u, len, (FILE *)user) == len ? CINT_OK : CINT_RESOURCE;
}

static cint_status to_stdout(void *user, const uint8_t *bytes, size_t len)
{
    (void)user;
    return fwrite(bytes, 1u, len, stdout) == len ? CINT_OK : CINT_RESOURCE;
}

typedef struct mem {
    uint8_t *b;
    size_t n, cap;
} mem;

static cint_status to_mem(void *user, const uint8_t *bytes, size_t len)
{
    mem *m = (mem *)user;
    if (len > m->cap - m->n) {
        return CINT_RESOURCE;
    }
    memcpy(m->b + m->n, bytes, len);
    m->n += len;
    return CINT_OK;
}

/* The whole file at path into *out, or 0. */
static size_t slurp(const char *path, uint8_t **out)
{
    FILE *f = fopen(path, "rb");
    size_t n = 0u, cap = 1u << 20;
    *out = f != NULL ? (uint8_t *)malloc(cap) : NULL;
    if (*out != NULL) {
        n = fread(*out, 1u, cap, f);
    }
    if (f != NULL) {
        fclose(f);
    }
    return n;
}

static uint32_t find(const cint_observer *o, const char *name)
{
    for (uint32_t i = 0u; i < o->entry_count; i++) {
        if (o->entries[i].name_len == strlen(name) && memcmp(o->entries[i].name, name, strlen(name)) == 0) {
            return i;
        }
    }
    return UINT32_MAX;
}

static cint_ctx *make_ctx(const cint_program *program, const cint_state_table *table)
{
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL;
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = program;
    cfg.output = to_stdout;
    if (cint_ctx_create(&cfg, &ctx) != CINT_OK || cint_ctx_bind_state(ctx, table) != CINT_OK) {
        return NULL;
    }
    return ctx;
}

static void hexline(const char *label, const uint8_t *h)
{
    printf("%s ", label);
    for (int k = 0; k < 32; k++) {
        printf("%02x", (unsigned)h[k]);
    }
    printf("\n");
}

#if defined(REC_HOST_SIR)
static cint_ctx *g_ctx;
static int g_events;

static void interrupt_at(void *user, const cint_sir_event *e)
{
    (void)user;
    (void)e;
    if (++g_events == 20) {
        cint_ctx_interrupt(g_ctx);
    }
}

static cint_sir_image *open_dir(const char *dir, cint_sir_program **p)
{
    cint_sir_error err;
    cint_sir_image *x = NULL;
    if (cint_sir_load(dir, p, &err) != CINT_SIR_OK || cint_sir_prepare(*p, &x, &err) != CINT_SIR_OK) {
        fprintf(stderr, "rec_host: %s:%u:%u: %s\n", err.path, (unsigned)err.line, (unsigned)err.col, err.text);
        return NULL;
    }
    return x;
}

static void code_of(const cint_sir_program *p, cint_code_module *code, uint32_t *count)
{
    uint32_t functions, nodes;
    cint_sir_counts(p, count, &functions, &nodes);
    for (uint32_t k = 0u; k < *count && k < 8u; k++) {
        cint_sir_text t;
        cint_sir_text_of(p, k, &t);
        code[k].path.bytes = t.path;
        code[k].path.len = (uint32_t)t.path_len;
        code[k].path.reserved = 0u;
        code[k].sir = t.text;
        code[k].sir_len = t.len;
        code[k].source = NULL;
        code[k].source_len = 0u;
    }
}
#endif

static void entry(cint_recorder *r, const char *label, uint32_t index, int64_t n, int64_t fuel)
{
    uint64_t arg, res = 0u;
    cint_status st;
    memcpy(&arg, &n, sizeof arg);
    st = cint_rec_entry(r, index, &arg, fuel, CINT_DEPTH_FROM_CONFIG, &res);
    printf("%s %d %lld\n", label, (int)st, st == CINT_OK ? (long long)res : 0ll);
}

static int replay(cint_ctx *ctx, const cint_observer *obs, cint_main_fn main_fn, const char *path, uint64_t stop)
{
    cint_recording_info info;
    cint_replay_config cfg;
    cint_replay_report rep;
    uint8_t *bytes = NULL, h[32];
    size_t n = slurp(path, &bytes);
    cint_status st = cint_recording_decode(bytes, n, &info);
    if (st != CINT_OK) {
        printf("decode %d\n", (int)st);
        free(bytes);
        return 1;
    }
    memset(&cfg, 0, sizeof cfg);
    memset(&rep, 0, sizeof rep);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.stop = stop;
    cfg.entries = obs;
    cfg.main_fn = main_fn;
    st = cint_replay(ctx, &info, &cfg, &rep);
    if (st == CINT_OK) {
        printf("replay %d %s %llu %llu ", (int)rep.status, rep.what != 0u ? cint_replay_what(rep.what) : "-",
               (unsigned long long)rep.event, (unsigned long long)rep.boundaries);
        hexline("chain", rep.chain);
        if (cint_ctx_state_hash(ctx, h) == CINT_OK) {
            hexline("state", h);
        }
    } else {
        printf("replay refused %d\n", (int)st);
    }
    free(bytes);
    return 0;
}

int main(int argc, char **argv)
{
    static uint8_t ckpt_bytes[1u << 16];
    cint_code_module code[8];
    const cint_program *program;
    const cint_state_table *table;
    const cint_observer *obs;
    cint_main_fn main_fn;
    cint_recorder *r = NULL;
    cint_sink sink, cs;
    mem ckpt = {ckpt_bytes, 0u, sizeof ckpt_bytes};
    uint32_t count = 0u;
    cint_ctx *ctx;
    FILE *out;
    int a = 3, interrupt = 0, density = 0, status = 0;
    const char *dir2 = NULL;
#if defined(REC_HOST_SIR)
    cint_sir_program *p = NULL, *p2 = NULL;
    cint_sir_image *x = argc >= 3 ? open_dir(argv[1], &p) : NULL, *x2 = NULL;
    if (x == NULL) {
        return 2;
    }
    program = cint_sir_cint_program(x);
    table = cint_sir_state_table(x);
    obs = cint_sir_observer(x);
    main_fn = cint_sir_main(x);
    code_of(p, code, &count);
    for (; strcmp(argv[2], "-r") != 0 && a < argc; a++) {
        if (strcmp(argv[a], "-i") == 0) {
            interrupt = 1;
        } else if (strcmp(argv[a], "-c") == 0 && a + 1 < argc) {
            dir2 = argv[++a];
        } else if (strcmp(argv[a], "-d") == 0) {
            density = 1;
        } else if (strcmp(argv[a], "-r") != 0) {
            return 2;
        }
    }
#else
    uint8_t *texts[8];
    if (argc < 3 || strcmp(argv[1], "-") != 0) {
        return 2;
    }
    program = cint_observer_desc.program;
    table = &cint_observer_state;
    obs = &cint_observer_desc;
    main_fn = REC_HOST_MAIN;
    if (strcmp(argv[2], "-r") != 0 && a < argc && strcmp(argv[a], "-d") == 0) {
        density = 1;
        a++;
    }
    for (; strcmp(argv[2], "-r") != 0 && a < argc && count < 8u; a++, count++) {
        const char *eq = strchr(argv[a], '=');
        if (eq == NULL) {
            return 2;
        }
        code[count].path.bytes = argv[a];
        code[count].path.len = (uint32_t)(eq - argv[a]);
        code[count].path.reserved = 0u;
        code[count].sir_len = slurp(eq + 1, &texts[count]);
        code[count].sir = texts[count];
        code[count].source = NULL;
        code[count].source_len = 0u;
    }
#endif
    if ((ctx = make_ctx(program, table)) == NULL) {
        return 2;
    }
    if (strcmp(argv[2], "-r") == 0) {
        status = argc >= 4 ? replay(ctx, obs, main_fn, argv[3], argc >= 5 ? strtoull(argv[4], NULL, 10) : 0u) : 2;
    } else {
        uint32_t deposit = find(obs, "deposit"), burn = find(obs, "burn");
        out = fopen(argv[2], "wb");
        sink.write = to_file;
        sink.user = out;
        cs.write = to_mem;
        cs.user = &ckpt;
        if (out == NULL || cint_rec_begin(ctx, obs, code, count, CINT_RECORD_FULL | (density ? CINT_RECORD_DISPATCH : 0u),
                                          &sink, &r) != CINT_OK) {
            return 2;
        }
        if (density) {
            printf("main %d\n", (int)cint_rec_main(r, main_fn, 1000));
#if defined(REC_HOST_SIR)
            int64_t e[4] = {0, 0, 0, 0};
            uint32_t rank = cint_sir_dispatch_domain(x, e);
            fprintf(stderr, "domain %u %lld %lld %lld %lld\n", (unsigned)rank, (long long)e[0], (long long)e[1],
                    (long long)e[2], (long long)e[3]);
#endif
        } else {
            entry(r, "deposit", deposit, 5, CINT_FUEL_UNBOUNDED);
            printf("checkpoint %d\n", (int)cint_rec_checkpoint(r, &cs));
            entry(r, "deposit", deposit, 7, 100);
            entry(r, "burn", burn, 1000, 50);
            printf("clear %d\n", (int)cint_rec_clear_fault(r));
            entry(r, "deposit", deposit, INT64_MAX, CINT_FUEL_UNBOUNDED);
            printf("clear %d\n", (int)cint_rec_clear_fault(r));
            printf("restore %d\n", (int)cint_rec_restore(r, ckpt.b, ckpt.n));
            printf("main %d\n", (int)cint_rec_main(r, main_fn, 100));
        }
#if defined(REC_HOST_SIR)
        if (interrupt && !density) {
            g_ctx = ctx;
            cint_sir_trace(x, interrupt_at, NULL);
            entry(r, "burn", burn, 40, CINT_FUEL_UNBOUNDED);
            cint_sir_trace(x, NULL, NULL);
        }
        if (dir2 != NULL && !density && (x2 = open_dir(dir2, &p2)) != NULL) {
            cint_code_module code2[8];
            uint32_t count2 = 0u;
            code_of(p2, code2, &count2);
            printf("change %d\n", (int)cint_rec_change_revision(r, cint_sir_observer(x2), cint_sir_state_table(x2), code2,
                                                                 count2));
            entry(r, "deposit", find(cint_sir_observer(x2), "deposit"), 2, CINT_FUEL_UNBOUNDED);
        }
#else
        (void)interrupt;
        (void)dir2;
#endif
        printf("end %d\n", (int)cint_rec_end(r));
        if (fclose(out) != 0) {
            status = 2;
        }
    }
    cint_ctx_destroy(ctx);
#if defined(REC_HOST_SIR)
    if (x2 != NULL) {
        cint_sir_release(x2);
        cint_sir_free(p2);
    }
    cint_sir_release(x);
    cint_sir_free(p);
#else
    for (uint32_t k = 0u; k < count; k++) {
        free(texts[k]);
    }
#endif
    return status;
}
