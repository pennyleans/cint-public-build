/* state_host.c: checkpoints on cpu-sir-interp and cpu-c17 (box 13 unit 4; SPEC-03 H-19,
 * H-19a, H-19b; SPEC-01 IM-154). Test driver of interp/tests/test_interp.py.
 *
 *   state_host DIR OUT [CKPT [DIR2]]   built with STATE_HOST_SIR: the program in DIR on the
 *                                      interpreter (cint_interp.h)
 *   state_host - OUT [CKPT [change]]   linked with the program's emitted C: cpu-c17
 *
 * The program is interp/tests/sir/module__state_two_modules, whose export `run` adds 2 to
 * its module's `seen` and calls `tick` of module/lib/tally.ci, which counts its calls. Both
 * builds bind the program's state table and print one line per step: `run` once, write the
 * checkpoint OUT, `run` again, restore CKPT (OUT when it is not given), and `run` again. The
 * lines and the file OUT are the same on both backends, and each restores the other's
 * checkpoint. On the interpreter, an interrupt from inside an entry then faults it at its
 * next call; with DIR2, another revision of the program, the context changes revision (the
 * values carry over) and a restore of OUT is refused (case 29). On cpu-c17, `change` changes
 * it into the same revision, which carries the arenas and pools (RT-OQ-46), and runs again. */
#define _CRT_SECURE_NO_WARNINGS 1  /* fopen */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cint_rt.h"
#include "cint_state.h"
#if defined(STATE_HOST_SIR)
#include "../cint_interp.h"
#else
extern const cint_observer cint_observer_desc;
extern const cint_state_table cint_observer_state;
#endif

static cint_observer_fn g_run;
static cint_ctx *g_ctx;

static cint_status to_file(void *user, const uint8_t *bytes, size_t len)
{
    return fwrite(bytes, 1u, len, (FILE *)user) == len ? CINT_OK : CINT_RESOURCE;
}

static void call_run(void)
{
    uint64_t r = 0u;
    cint_status st = g_run(g_ctx, CINT_FUEL_UNBOUNDED, 8, NULL, &r);
    printf("run %d %lld\n", (int)st, st == CINT_OK ? (long long)r : 0ll);
}

static void hash(void)
{
    uint8_t h[32];
    cint_status st = cint_ctx_state_hash(g_ctx, h);
    printf("hash %d ", (int)st);
    for (int k = 0; k < 32; k++) {
        printf("%02x", st == CINT_OK ? (unsigned)h[k] : 0u);
    }
    printf("\n");
}

static void restore(const char *label, const char *path)
{
    static uint8_t data[1u << 16];
    FILE *f = fopen(path, "rb");
    size_t n = f != NULL ? fread(data, 1u, sizeof data, f) : 0u;
    if (f != NULL) {
        fclose(f);
    }
    printf("%s %d\n", label, (int)cint_ctx_restore(g_ctx, data, n));
}

static cint_observer_fn find_run(const cint_observer *o)
{
    for (uint32_t i = 0u; o != NULL && i < o->entry_count; i++) {
        if (o->entries[i].name_len == 3u && memcmp(o->entries[i].name, "run", 3u) == 0) {
            return o->entries[i].call;
        }
    }
    return NULL;
}

#if defined(STATE_HOST_SIR)
static int g_events;

static void interrupt_at_first(void *user, const cint_sir_event *e)
{
    (void)user;
    (void)e;
    if (g_events++ == 0) {
        cint_ctx_interrupt(g_ctx);
    }
}

static cint_sir_image *open_dir(const char *dir, cint_sir_program **p)
{
    cint_sir_error err;
    cint_sir_image *x = NULL;
    if (cint_sir_load(dir, p, &err) != CINT_SIR_OK || cint_sir_prepare(*p, &x, &err) != CINT_SIR_OK) {
        fprintf(stderr, "state_host: %s:%u:%u: %s\n", err.path, (unsigned)err.line, (unsigned)err.col, err.text);
        return NULL;
    }
    return x;
}
#endif

int main(int argc, char **argv)
{
    cint_ctx_config cfg;
    const cint_program *program;
    const cint_state_table *table;
    const cint_observer *obs;
    cint_sink sink;
    FILE *out;
#if defined(STATE_HOST_SIR)
    cint_sir_program *p = NULL, *p2 = NULL;
    cint_sir_image *x = argc >= 3 && argc <= 5 ? open_dir(argv[1], &p) : NULL, *x2 = NULL;
    if (x == NULL) {
        return 2;
    }
    program = cint_sir_cint_program(x);
    table = cint_sir_state_table(x);
    obs = cint_sir_observer(x);
#else
    if (argc < 3 || argc > 5 || strcmp(argv[1], "-") != 0 || (argc == 5 && strcmp(argv[4], "change") != 0)) {
        return 2;
    }
    program = cint_observer_desc.program;
    table = &cint_observer_state;
    obs = &cint_observer_desc;
#endif
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = program;
    if ((g_run = find_run(obs)) == NULL || cint_ctx_create(&cfg, &g_ctx) != CINT_OK) {
        return 2;
    }
    printf("bind %d\n", (int)cint_ctx_bind_state(g_ctx, table));
    call_run();
    out = fopen(argv[2], "wb");
    sink.write = to_file;
    sink.user = out;
    printf("checkpoint %d\n", out != NULL ? (int)cint_ctx_checkpoint(g_ctx, &sink) : -1);
    if (out == NULL || fclose(out) != 0) {
        return 2;
    }
    hash();
    call_run();
    restore("restore", argc >= 4 ? argv[3] : argv[2]);
    hash();
    call_run();
#if defined(STATE_HOST_SIR)
    {
        cint_fault_record *rec = (cint_fault_record *)malloc(sizeof *rec);
        cint_sir_trace(x, interrupt_at_first, NULL);
        g_events = 0;
        call_run();
        cint_sir_trace(x, NULL, NULL);
        if (rec != NULL && cint_ctx_fault(g_ctx, rec) == CINT_OK) {
            char limit[64];
            (void)cint_tvalue_render(&rec->limit, limit, sizeof limit);
            printf("interrupt code %u site %u.%u limit %s\n", (unsigned)rec->code, (unsigned)rec->position.module,
                   (unsigned)rec->position.index, rec->has_limit ? limit : "-");
        }
        free(rec);
        printf("clear %d\n", (int)cint_ctx_clear_fault(g_ctx));
        call_run();
    }
    if (argc == 5 && (x2 = open_dir(argv[4], &p2)) != NULL) {
        printf("change %d\n", (int)cint_ctx_change_revision(g_ctx, cint_sir_cint_program(x2), cint_sir_state_table(x2)));
        g_run = find_run(cint_sir_observer(x2));
        call_run();
        restore("restore-old", argv[2]);
        hash();
    }
#else
    if (argc == 5) {
        printf("change %d\n", (int)cint_ctx_change_revision(g_ctx, program, table));
        call_run();
    }
#endif
    cint_ctx_destroy(g_ctx);
#if defined(STATE_HOST_SIR)
    if (x2 != NULL) {
        cint_sir_release(x2);
    }
    if (p2 != NULL) {
        cint_sir_free(p2);
    }
    cint_sir_release(x);
    cint_sir_free(p);
#endif
    return 0;
}
