/* wb_qualify.c: the candidate of the debugger's qualification test (box 14 unit 3b-Q; section
 * 7.3 of the box 14 note), for workbench/tests/wbt_diff.py.
 *
 *   wb-qualify record SIR SRC ENTRY N CASE [K]
 *   wb-qualify diff CASE REF CAND OUT CINT RECEIPT [K [once|fault|arena]]
 *   WB_QUALIFY_SESSION=K wb-qualify ARGS...
 *
 * The change: the first global of a context's canonical state, an integer, gains 1, through a
 * checkpoint and a restore, which serve whatever program the context runs.
 *
 * `record` loads the SIR text of a program from directory SIR, calls export ENTRY of its root
 * module, which takes no arguments, N times under a recorder in case mode, with each module's
 * source read from SRC/<path>, and writes the case CASE. With K, the test's candidate makes the
 * recording: after call K, the change, before the recorder takes the state hash, so `cint debug
 * replay` must report DIVERGED at boundary K.
 *
 * `diff` runs `cint debug diff` (wb_diff) on CASE with REF and CAND, cpu-c17 built by CINT ("-":
 * none) in OUT, and writes RECEIPT. With K, a cpu-sir-interp candidate is the test's: the change
 * at boundary K, also on the rerun of SPEC-06 9.3 step 6 unless `once`. With `fault` the change is
 * instead a fault record, E_ASSERT at the first site, as a failed `assert` writes it, so diff reports
 * `fault-difference`; with `arena` it is to element 0 of the first module-level arena that holds
 * one, which gains 1 in its lowest byte, so diff localizes the difference to that element.
 *
 * With WB_QUALIFY_SESSION set, it is `cint workbench` with ARGS, whose `:run --check` makes the
 * change on its cpu-sir-interp side at entry K. Exit statuses are those of wb_diff or the
 * workbench; 2 when this program cannot run.
 */
#define _CRT_SECURE_NO_WARNINGS 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cint_interp.h"
#include "cint_proc.h"
#include "cint_state.h"
#include "wb.h"
#include "wb_debug.h"

static uint64_t g_k, g_calls, g_changed;
static int g_once, g_fault, g_arena;
static cint_observer_fn g_call;

static cint_status to_buf(void *user, const uint8_t *bytes, size_t len)
{
    wb_put(user, bytes, len);
    return ((wb_buf *)user)->failed ? CINT_RESOURCE : CINT_OK;
}

static void bump(cint_ctx *ctx)
{
    wb_buf ck = {0};
    cint_sink sink = {to_buf, &ck};
    cint_checkpoint_info info;
    cint_checkpoint_global g;
    size_t at = 0;
    memset(&info, 0, sizeof info);
    memset(&g, 0, sizeof g);
    cint_checkpoint_arena a;
    memset(&a, 0, sizeof a);
    if (g_arena && cint_ctx_checkpoint(ctx, &sink) == CINT_OK && !ck.failed &&
        cint_checkpoint_decode((const uint8_t *)ck.p, ck.n, &info) == CINT_OK) {
        while (cint_checkpoint_arena_next(&info, &at, &a) == CINT_OK && (a.elements == NULL || a.used == 0)) {
            continue;
        }
        if (a.elements != NULL && a.used > 0) {
            uint8_t *v = (uint8_t *)ck.p + (a.elements - (const uint8_t *)ck.p);
            for (size_t i = 0; i < a.elem_bytes && ++v[i] == 0u; i++) {
                continue;
            }
            if (cint_ctx_restore(ctx, (const uint8_t *)ck.p, ck.n) == CINT_OK) {
                g_changed++;
            }
        }
    } else if (!g_arena && cint_ctx_checkpoint(ctx, &sink) == CINT_OK && !ck.failed &&
        cint_checkpoint_decode((const uint8_t *)ck.p, ck.n, &info) == CINT_OK &&
        cint_checkpoint_global_next(&info, &at, &g) == CINT_OK && g.value.len >= 2u) {
        size_t w = g.value.len - 1u, i = 0;   /* the value's little-endian bytes end where the global does */
        uint8_t *v = (uint8_t *)ck.p + ((info.state + at - w) - (const uint8_t *)ck.p);
        if (memcmp(v, g.value.bytes + 1, w) == 0) {
            while (i < w && ++v[i] == 0u) {
                i++;
            }
            if (cint_ctx_restore(ctx, (const uint8_t *)ck.p, ck.n) == CINT_OK) {
                g_changed++;
            }
        }
    }
    wb_free(&ck);
}

static void alter(uint64_t k, int rerun, cint_ctx *ctx)
{
    if (k == g_k && !(rerun && g_once)) {
        if (g_fault) {
            cint_site first = {0u, 1u};   /* the first site of the first module, which every program has */
            (void)cint_rt_fault_assert(ctx, first, 0u, 0u, 0u);
        } else {
            bump(ctx);
        }
    }
}

static cint_status altered(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args, uint64_t *result)
{
    cint_status st = g_call(ctx, fuel, depth, args, result);
    if (++g_calls == g_k) {
        bump(ctx);
    }
    return st;
}

static int fail(const char *what)
{
    fprintf(stderr, "wb-qualify: %s\n", what);
    return 2;
}

static int diff(char **argv)
{
    wb_diff_opts o;
    wb_buf err = {0};
    memset(&o, 0, sizeof o);
    o.path = argv[2];
    o.ref = argv[3];
    o.cand = argv[4];
    o.out = argv[5];
    o.cint = strcmp(argv[6], "-") != 0 ? argv[6] : NULL;
    o.receipt = argv[7];
    o.alter = g_k != 0 ? alter : NULL;
    int status = wb_diff(&o, &err);
    if (err.n != 0) {
        fprintf(stderr, "wb-qualify: %.*s\n", (int)err.n, err.p);
    }
    wb_free(&err);
    return status;
}

static int record(char **argv)
{
    cint_sir_program *prog = NULL;
    cint_sir_image *img = NULL;
    cint_sir_error serr;
    if (cint_sir_load(argv[2], &prog, &serr) != CINT_SIR_OK || cint_sir_prepare(prog, &img, &serr) != CINT_SIR_OK) {
        fprintf(stderr, "wb-qualify: %s:%u:%u: %s\n", serr.path, (unsigned)serr.line, (unsigned)serr.col, serr.text);
        return 2;
    }
    const cint_observer *obs = cint_sir_observer(img);
    static cint_observer_entry entries[256];
    cint_observer copy = *obs;
    uint32_t n = cint_sir_cint_program(img)->module_count, entry = UINT32_MAX;
    cint_code_module *code = calloc(n, sizeof *code);
    for (uint32_t i = 0; i < obs->entry_count && i < 256u; i++) {
        entries[i] = obs->entries[i];
        if (obs->entries[i].module == n - 1u && obs->entries[i].name_len == strlen(argv[4]) &&
            memcmp(obs->entries[i].name, argv[4], obs->entries[i].name_len) == 0) {
            entry = i;
            g_call = entries[i].call;
            entries[i].call = g_k != 0 ? altered : entries[i].call;
        }
    }
    copy.entries = entries;
    if (code == NULL || entry == UINT32_MAX) {
        return fail("the root module exports no such entry");
    }
    for (uint32_t k = 0; k < n; k++) {
        cint_sir_text st;
        wb_buf path = {0};
        cint_sir_text_of(prog, k, &st);
        wb_f(&path, "%s/%.*s", argv[3], (int)st.path_len, st.path);
        code[k].path.bytes = st.path;
        code[k].path.len = (uint32_t)st.path_len;
        code[k].sir = st.text;
        code[k].sir_len = st.len;
        code[k].source = (const uint8_t *)wb_read_file(path.p, &code[k].source_len);
        wb_free(&path);
        if (code[k].source == NULL) {
            return fail("a module's source cannot be read");
        }
    }
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL;
    cint_recorder *r = NULL;
    wb_buf out = {0};
    cint_sink sink = {to_buf, &out};
    uint64_t result = 0;
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = cint_sir_cint_program(img);
    if (cint_ctx_create(&cfg, &ctx) != CINT_OK || cint_ctx_bind_state(ctx, cint_sir_state_table(img)) != CINT_OK ||
        cint_rec_begin(ctx, &copy, code, n, CINT_RECORD_CASE, &sink, &r) != CINT_OK) {
        return fail("the recorder does not start");
    }
    for (unsigned long long i = strtoull(argv[5], NULL, 10); i > 0; i--) {
        cint_status st = cint_rec_entry(r, entry, NULL, CINT_FUEL_UNBOUNDED, 256, &result);
        if (st != CINT_OK && st != CINT_FAULT) {
            return fail("an entry does not run");
        }
    }
    if (cint_rec_end(r) != CINT_OK || out.failed || (g_k != 0 && g_changed != 1u) ||
        wb_write_file(argv[6], out.p, out.n) != 0) {
        return fail("the case cannot be written");
    }
    for (uint32_t k = 0; k < n; k++) {
        free((void *)code[k].source);
    }
    free(code);
    wb_free(&out);
    cint_ctx_destroy(ctx);
    cint_sir_release(img);
    cint_sir_free(prog);
    return 0;
}

int main(int argc, char **argv)
{
    const char *session = getenv("WB_QUALIFY_SESSION");
    if (session != NULL) {   /* the workbench reads its own command line */
        g_k = strtoull(session, NULL, 10);
        wb_check_alter = alter;
        return wb_main_workbench(argc, argv);
    }
    if (cli_init(&argc, &argv) != 0) {
        return fail("cannot read the command line");
    }
    int rec = argc >= 7 && strcmp(argv[1], "record") == 0, dif = argc >= 8 && strcmp(argv[1], "diff") == 0;
    int k_at = rec ? 7 : 8;
    g_k = argc > k_at ? strtoull(argv[k_at], NULL, 10) : 0;
    g_once = argc > k_at + 1 && strcmp(argv[k_at + 1], "once") == 0;
    g_fault = argc > k_at + 1 && strcmp(argv[k_at + 1], "fault") == 0;
    g_arena = argc > k_at + 1 && strcmp(argv[k_at + 1], "arena") == 0;
    return rec ? record(argv) : dif ? diff(argv) :
           fail("usage: wb-qualify record SIR SRC ENTRY N CASE [K] | diff CASE REF CAND OUT CINT RECEIPT "
                "[K [once|fault|arena]]");
}
