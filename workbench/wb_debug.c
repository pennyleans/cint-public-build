/* wb_debug.c: `cint debug replay`, `cint debug locate`, and `cint debug RECORDING|CASE` (box 14
 * unit 3b-D; SPEC-06 3.1, 9, 10; the box 14 note, sections 6.4 and 7.1).
 *
 * `replay` replays a recording with box 13's replay (wb_replay.h), which compares every print
 * and outcome with the recording and stops with DIVERGED at the first difference. It lists the
 * boundaries it replayed, and `--receipt` writes the receipt of kind `replay` that `cint-interp
 * replay --receipt` writes, with the same identity (BX13-13); under --json that receipt is the
 * output. `locate` runs a program on cpu-sir-interp under a recording, with the workbench
 * defaults, or takes a recording, and finds the first boundary that ends in a fault or an error
 * result. It replays to that boundary and prints the report (BX14-05 A, BX14-13), the boundary
 * bracket, and the last counted operations of the entry from the trace (BX14-14). With no
 * subcommand, a recording opens in the read-only session (BX14-17). Values come from the
 * recording and from replay on the engine; nothing here computes one.
 */
#include <stdlib.h>
#include <string.h>

#include "cint_proc.h"
#include "sir.h"
#include "wb_debug.h"
#include "wb_replay.h"

#if defined(_WIN32)
#define SEP "\\"
#define EXE ".exe"
#else
#define SEP "/"
#define EXE ""
#endif

#define LAST_OPS 20u   /* counted operations `locate` shows (BX14-14) */
#define BOUNDARIES(n) ((n) == 1u ? "boundary" : "boundaries")

/* A recording under examination: its bytes, where a program's sources are, and its replay. */
typedef struct dbg {
    const char *path;            /* the file, or the program's root module */
    uint8_t *bytes;
    size_t len;
    const char *root;            /* a program's sources are under it; NULL: the recording's */
    char *src, *src_path;        /* the source last read from root */
    const char *tool;            /* the interpreter's source identity for a receipt, or NULL */
    wb_replayer rp;
    wb_buf costs;                /* replay's measurements of OQ-72, its start, and their own time */
    long long t0, spent;
} dbg;

/* The outcomes of the entries that ran, in order: next_boundary moves c->e to the next one,
 * with c->entry its entry event and c->k its boundary. Returns 0, or -1 after the last. */
typedef struct cursor {
    cint_recording_event e, entry;
    uint64_t k;
} cursor;

static int next_boundary(const cint_recording_info *info, cursor *c)
{
    while (cint_recording_next(info, &c->e) == CINT_OK) {
        if (c->e.kind == CINT_EVENT_ENTRY) {
            c->entry = c->e;
        } else if (c->e.kind == CINT_EVENT_OUTCOME && c->e.has_hash) {
            c->k++;
            return 0;
        }
    }
    return -1;
}

/* A program's print statements: the recording keeps them, and replay compares them. */
static cint_status discard(void *user, const uint8_t *bytes, size_t len)
{
    (void)user;
    (void)bytes;
    (void)len;
    return CINT_OK;
}

/* A context for prog with its module state bound, the depth limit of its program entry and the
 * frame-arena capacity (0: the defaults). NULL when one cannot be made. */
static cint_ctx *new_ctx(const cint_program *prog, const cint_state_table *t, int64_t depth, int64_t arena)
{
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL;
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = prog;
    cfg.depth_limit = depth;
    cfg.frame_arena_bytes = arena;
    cfg.output = discard;
    if (cint_ctx_create(&cfg, &ctx) != CINT_OK || cint_ctx_bind_state(ctx, t) != CINT_OK) {
        cint_ctx_destroy(ctx);
        return NULL;
    }
    return ctx;
}

/* Loads d's recording for replay, with fn called for each trace event, on a context made as its
 * first entry ran (the program entry's depth limit, the frame-arena capacity). Returns 0, or the
 * exit status with the reason in b. */
static int replay_open(dbg *d, cint_sir_trace_fn fn, void *user, wb_buf *b)
{
    cint_recording_info info;
    cint_recording_event e;
    uint8_t rev[32];
    memset(&e, 0, sizeof e);
    if (cint_recording_decode(d->bytes, d->len, &info) != CINT_OK) {
        wb_f(b, "%s is not a complete recording\n", d->path);
        return CINT_EXIT_CHECK;
    }
    if (wb_replay_load(&d->rp, d->bytes, d->len, fn, user) != 0) {
        wb_f(b, "the code of %s does not load on cpu-sir-interp\n", d->path);
        return CINT_EXIT_CHECK;
    }
    cint_sir_revision(d->rp.prog, rev);
    if (memcmp(rev, d->rp.info.revision, 32u) != 0) {
        wb_f(b, "the code of %s does not have the recording's revision identity\n", d->path);
        return CINT_EXIT_CHECK;
    }
    while (cint_recording_next(&d->rp.info, &e) == CINT_OK && e.kind != CINT_EVENT_ENTRY) continue;
    d->rp.ctx = new_ctx(cint_sir_cint_program(d->rp.img), cint_sir_state_table(d->rp.img),
                        e.kind == CINT_EVENT_ENTRY && e.form == 1u ? e.depth : 0, e.kind == CINT_EVENT_ENTRY ? e.arena : 0);
    if (d->rp.ctx == NULL) {
        wb_s(b, "cannot make a context for the recording's program\n");
        return CINT_EXIT_ENVIRONMENT;
    }
    return 0;
}

static void tval_of(wb_tval *v, const cint_tvalue *t)
{
    v->len = t->len <= sizeof v->bytes ? t->len : 0;
    memcpy(v->bytes, t->bytes, v->len);
}

void wb_entry_text(wb_buf *b, const cint_recording_info *info, const cint_recording_event *e)
{
    static wb_tval v;
    static cint_tvalue t;
    char text[96];
    if (e->form == 1u) {
        wb_s(b, "the program entry");
        return;
    }
    wb_f(b, "%.*s(", (int)e->name.len, e->name.bytes);
    for (uint32_t k = 0; k < e->arg_count && cint_recording_arg(info, e, k, &t) == CINT_OK; k++) {
        tval_of(&v, &t);
        (void)wb_tval_text(&v, text, sizeof text, 1);
        wb_f(b, "%s%s", k != 0 ? ", " : "", text);
    }
    wb_s(b, ")");
}

/* What an outcome records: `faulted E_DEPTH`, `error <set> tag N`, `value I64 6`, or `done`. */
static void outcome_text(wb_buf *b, const cint_recording_event *e)
{
    static wb_fault f;
    static wb_tval v;
    char text[96];
    if (e->status == CINT_FAULT) {
        int ok = e->data != NULL && wb_fault_decode(e->data, e->data_len, &f, WB_RECORD_RUN) == 0 && f.code < 14u;
        wb_f(b, "faulted %s", ok ? WB_CODE_NAMES[f.code] : "(an unreadable record)");
    } else if (e->has_error) {
        wb_f(b, "error %.*s tag %llu", (int)e->error_set.len, e->error_set.bytes, (unsigned long long)e->error_tag);
    } else if (e->has_result) {
        tval_of(&v, &e->result);
        (void)wb_tval_text(&v, text, sizeof text, 1);
        wb_f(b, "value %s", text);
    } else {
        wb_s(b, "done");
    }
}

/* The boundaries of the recording up to `upto`, one line each. */
static void boundaries(wb_buf *b, const cint_recording_info *info, uint64_t upto)
{
    cursor c;
    memset(&c, 0, sizeof c);
    wb_f(b, "%8s  %5s  %-30s %-24s %s\n", "boundary", "event", "entry", "outcome", "fuel");
    while (c.k < upto && next_boundary(info, &c) == 0) {
        wb_buf call = {0}, what = {0};
        wb_entry_text(&call, info, &c.entry);
        outcome_text(&what, &c.e);
        wb_f(b, "%8llu  %5llu  %-30s %-24s %lld\n", (unsigned long long)c.k, (unsigned long long)c.e.index,
             call.p != NULL ? call.p : "", what.p != NULL ? what.p : "", (long long)c.e.consumed);
        wb_free(&call);
        wb_free(&what);
    }
}

/* The receipt of kind `replay` as `cint-interp replay --receipt` writes it (SPEC-06 12.2; box 13
 * default BX13-13; interp/cint_interp_main.c), so one replay has one receipt identity whichever
 * tool ran it: canonical JSON (SPEC-09 RCPT-02) ending in LF. */
static void receipt(wb_buf *out, const dbg *d, uint64_t stop, const uint8_t state[32], long long ns)
{
    const cint_recording_info *info = &d->rp.info;
    const cint_replay_report *rep = &d->rp.rep;
    wb_buf id = {0}, oc = {0}, fuel = {0}, all = {0};
    uint8_t h[32], fault[32];
    int faulted = 0;
    cursor c;
    cint_sha256(d->bytes, d->len, h);
    wb_s(&id, "{\"backend\":{\"id\":\"cpu-sir-interp\"},");
    if (info->is_case) {   /* keys in byte order (canonical JSON) */
        wb_f(&id, "\"case\":{\"bytes\":\"%llu\",\"sha256\":\"", (unsigned long long)d->len);
        wb_hex(&id, h, 32u);
        wb_s(&id, "\"},");
    }
    wb_s(&id, "\"identities\":{\"revision\":\"");
    wb_hex(&id, info->revision, 32u);
    wb_s(&id, info->has_map ? "\",\"source_map\":\"" : "\",\"source_map\":null");
    if (info->has_map) {
        wb_hex(&id, info->map, 32u);
        wb_s(&id, "\"");
    }
    wb_f(&id, "},\"kind\":\"replay\",\"method\":{\"density\":\"entry\",\"fuel_model\":\"fuel-v1\",\"mode\":\"%s\","
              "\"state_hash_interval\":\"%llu\",\"stop\":\"%llu\"},\"profile\":\"cint-core-1\",",
         info->mode == CINT_RECORD_DIGEST ? "digest" : "full", (unsigned long long)info->interval,
         (unsigned long long)stop);
    if (!info->is_case) {
        wb_f(&id, "\"recording\":{\"bytes\":\"%llu\",\"sha256\":\"", (unsigned long long)d->len);
        wb_hex(&id, h, 32u);
        wb_s(&id, "\"},");
    }
    wb_f(&id, "\"runtime_contract\":\"" CINT_RT_CONTRACT "\",\"schema\":\"cint-receipt-1/replay\","
              "\"tool\":{\"interpreter_source_identity\":%s%s%s}}", d->tool ? "\"" : "null", d->tool ? d->tool : "", d->tool ? "\"" : "");
    memset(&c, 0, sizeof c);
    while (c.k < rep->boundaries && next_boundary(info, &c) == 0) {
        wb_f(&fuel, "%s\"%llu\"", c.k > 1u ? "," : "", (unsigned long long)c.e.consumed);
        if (c.e.data != NULL) {
            cint_sha256(c.e.data, c.e.data_len, fault);
            faulted = 1;
        }
    }
    wb_f(&oc, "{\"boundaries\":\"%llu\",\"chain_head\":\"", (unsigned long long)rep->boundaries);
    wb_hex(&oc, rep->chain, 32u);
    wb_f(&oc, "\",\"event\":\"%llu\",\"fault_sha256\":%s", (unsigned long long)rep->event, faulted ? "\"" : "null");
    if (faulted) {
        wb_hex(&oc, fault, 32u);
        wb_s(&oc, "\"");
    }
    wb_f(&oc, ",\"fuel_consumed\":[%s],\"state_sha256\":\"", fuel.p != NULL ? fuel.p : "");
    wb_hex(&oc, state, 32u);
    wb_f(&oc, "\",\"status\":\"%s\",\"what\":%s%s%s}",
         rep->status == CINT_DIVERGED ? "diverged" : rep->status == CINT_UNREPLAYABLE ? "unreplayable" : "ok",
         rep->what != 0u ? "\"" : "null", rep->what != 0u ? cint_replay_what(rep->what) : "", rep->what != 0u ? "\"" : "");
    wb_f(&all, "{\"identity\":%s,\"outcome\":%s}", id.p != NULL ? id.p : "", oc.p != NULL ? oc.p : "");
    if (all.p != NULL) cint_sha256((const uint8_t *)all.p, all.n, h);
    wb_f(out, "{\"identity\":%s,\"observations\":{%s%s%s\"limits\":\"The executables' digests are not recorded yet "
              "(RT-OQ-44).\",\"machine\":{\"os\":\"%s\"},\"receipt_identity_sha256\":\"", id.p != NULL ? id.p : "",
         d->costs.n != 0 ? "\"checkpoints\":[" : "", d->costs.n != 0 ? d->costs.p : "", d->costs.n != 0 ? "]," : "",
#if defined(_WIN32)
         "windows"
#elif defined(__APPLE__)
         "macos"
#elif defined(__linux__)
         "linux"
#else
         "other"
#endif
    );
    wb_hex(out, h, 32u);
    wb_f(out, "\",\"timings\":{\"replay_ns\":\"%llu\"}},\"outcome\":%s}\n", (unsigned long long)ns,
         oc.p != NULL ? oc.p : "");
    out->failed |= id.failed | oc.failed | fuel.failed | all.failed;
    wb_free(&id);
    wb_free(&oc);
    wb_free(&fuel);
    wb_free(&all);
}

/* Writes the receipt to path, or to standard output when path is NULL. Returns 0, or -1. */
static int put_receipt(const dbg *d, const char *path, uint64_t stop, const uint8_t state[32], long long ns)
{
    wb_buf r = {0};
    receipt(&r, d, stop, state, ns);
    int ok = !r.failed && (path != NULL ? wb_write_file(path, r.p, r.n) == 0 : fwrite(r.p, 1, r.n, stdout) == r.n);
    wb_free(&r);
    return ok ? 0 : -1;
}

/* The source of a module path for the excerpts: under a program's root, or in the recording. */
static const uint8_t *source_of(void *user, const char *path, size_t len, size_t *n)
{
    dbg *d = user;
    cint_code_module m;
    if (d->root == NULL) {
        for (uint32_t k = 0; k < d->rp.info.module_count; k++) {
            if (cint_recording_module(&d->rp.info, NULL, k, &m) == CINT_OK && m.path.len == len &&
                memcmp(m.path.bytes, path, len) == 0) {
                *n = m.source_len;
                return m.source;
            }
        }
        return NULL;
    }
    if (d->src_path == NULL || strlen(d->src_path) != len || memcmp(d->src_path, path, len) != 0) {
        free(d->src);
        free(d->src_path);
        d->src = NULL;
        d->src_path = malloc(len + 1u);
        if (d->src_path == NULL) return NULL;
        memcpy(d->src_path, path, len);
        d->src_path[len] = '\0';
        char *file = wb_join(d->root, d->src_path);
        d->src = file != NULL ? wb_read_file(file, n) : NULL;
        free(file);
    }
    *n = d->src != NULL ? strlen(d->src) : 0;
    return (const uint8_t *)d->src;
}

/* ------------------------------------------------------------------ replay */

/* OQ-72 (box 14 default BX14-09): at each boundary, the elements a checkpoint of the state holds,
 * a scalar or struct global as one, and the time replay took to reach it, less that of these
 * measurements. */
static void cost_at(void *user, uint64_t at, cint_ctx *ctx)
{
    dbg *d = user;
    long long t = wb_now_ns();
    wb_buf ck = {0};
    cint_sink sink = {wb_sink, &ck};
    cint_checkpoint_info info;
    cint_checkpoint_global g;
    cint_checkpoint_arena a;
    cint_checkpoint_pool p;
    uint64_t n = 0;
    size_t i = 0, j = 0, k = 0;
    if (cint_ctx_checkpoint(ctx, &sink) == CINT_OK &&
        cint_checkpoint_decode((const uint8_t *)ck.p, ck.n, &info) == CINT_OK) {
        while (cint_checkpoint_global_next(&info, &i, &g) == CINT_OK)
            n += g.element_count != 0 && g.value.bytes[0] != CINT_TAG_RECORD ? g.element_count : 1u;
        while (cint_checkpoint_arena_next(&info, &j, &a) == CINT_OK) n += a.parent == 0 ? (uint64_t)a.used : 0u;
        while (cint_checkpoint_pool_next(&info, &k, &p) == CINT_OK) n += p.occupied;
        wb_f(&d->costs, "%s{\"boundary\":\"%llu\",\"elements\":\"%llu\",\"replay_ns\":\"%lld\"}",
             d->costs.n != 0 ? "," : "", (unsigned long long)at, (unsigned long long)n,
             wb_elapsed_ns(d->t0 + d->spent, t));
    }
    wb_free(&ck);
    d->spent += wb_elapsed_ns(t, wb_now_ns());
}

/* `cint debug replay RECORDING [--to BOUNDARY] [--receipt PATH]` (note 7.1): exit 0 when every
 * boundary replayed compares, 3 when replay stops with DIVERGED or UNREPLAYABLE or the file is
 * not a recording whose code loads; one line on standard error says why, as for `cint-interp
 * replay`. */
static int replay(dbg *d, int json, uint64_t to, const char *receipt_path, wb_buf *err)
{
    const cint_recording_info *info = &d->rp.info;
    wb_buf b = {0}, why = {0};
    uint8_t state[32];
    long long t0 = wb_now_ns();
    int status = replay_open(d, NULL, NULL, &why), ran = 0;
    memset(state, 0, sizeof state);
    if (status == 0 && to > info->boundaries) {
        wb_f(err, "%s has %llu %s; --to takes one of them", d->path, (unsigned long long)info->boundaries,
             BOUNDARIES(info->boundaries));
        status = CINT_EXIT_USAGE;
    }
    if (status == 0) {
        d->t0 = t0;
        d->rp.at = cost_at;
        d->rp.at_user = d;
        /* 1: every boundary compared; 2: DIVERGED or UNREPLAYABLE; 0: the engine refused it */
        ran = wb_replay_run(&d->rp, to != 0 ? to : UINT64_MAX, &why) == 0 ? 1 : d->rp.rep.status != CINT_OK ? 2 : 0;
        t0 = wb_elapsed_ns(t0 + d->spent, wb_now_ns());
        status = ran == 1 ? CINT_EXIT_OK : CINT_EXIT_CHECK;
        if (ran != 0 && cint_ctx_state_hash(d->rp.ctx, state) != CINT_OK) {
            wb_s(err, "cannot read the state replay reached");
            status = CINT_EXIT_INTERNAL;
        }
    }
    if (ran != 0 && status != CINT_EXIT_INTERNAL) {
        if (json) {
            status = put_receipt(d, NULL, to, state, t0) == 0 ? status : CINT_EXIT_ENVIRONMENT;
        } else {
            wb_f(&b, "%s %s   %llu %s   %llu events   mode %s   density entry\nrevision   ",
                 info->is_case ? "case      " : "recording ", d->path, (unsigned long long)info->boundaries,
                 BOUNDARIES(info->boundaries),
                 (unsigned long long)info->event_count,
                 info->mode == CINT_RECORD_DIGEST ? "digest" : "full");
            wb_hex(&b, info->revision, 32u);
            if (info->has_map) {
                wb_s(&b, "\nsource-map ");
                wb_hex(&b, info->map, 32u);
            }
            wb_s(&b, "\n");
            boundaries(&b, info, d->rp.rep.boundaries);
            if (ran == 2) wb_put(&b, why.p, why.n);
            wb_f(&b, "replay     %s, %llu of %llu %s compared with the recording\nchain      ", ran == 1 ? "ok" : "stopped",
                 (unsigned long long)d->rp.rep.boundaries, (unsigned long long)info->boundaries,
                 BOUNDARIES(info->boundaries));
            wb_hex(&b, d->rp.rep.chain, 32u);
            wb_s(&b, "\nstate      ");
            wb_hex(&b, state, 32u);
            wb_s(&b, "\n");
            (void)fwrite(b.p, 1, b.n, stdout);
        }
        if (receipt_path != NULL && status != CINT_EXIT_ENVIRONMENT &&
            put_receipt(d, receipt_path, to, state, t0) != 0) {
            wb_f(err, "cannot write the receipt %s", receipt_path);
            status = CINT_EXIT_ENVIRONMENT;
        }
    }
    if (status == CINT_EXIT_CHECK && (ran != 2 || json)) {
        fflush(stdout);
        fputs(why.p != NULL ? why.p : "", stderr);
    } else if (status == CINT_EXIT_ENVIRONMENT && err->n == 0) {
        wb_put(err, why.p, why.n != 0 ? why.n - 1u : 0);
    }
    wb_free(&b);
    wb_free(&why);
    return status;
}

/* ------------------------------------------------------------------ locate */

typedef struct locate_opts {
    const char *root, *cint, *out, *sir, *entry, *when, *changed, *save_case, *receipt;
    char type[16][8];            /* --args: each typed value's type and value */
    const char *value[16];
    uint32_t nargs;
    int64_t depth, fuel;
} locate_opts;

/* The pattern of typed value `TYPE VALUE` (CONF-12) of type `tag`. Returns 0, or -1. */
static int typed(const char *type, const char *value, uint32_t tag, uint64_t *bits)
{
    static const char *const names[] = {"I8", "I16", "I32", "I64", "U8", "U16", "U32", "U64", "Bool"};
    static const uint32_t tags[] = {CINT_TAG_I8, CINT_TAG_I16, CINT_TAG_I32, CINT_TAG_I64, CINT_TAG_U8,
                                    CINT_TAG_U16, CINT_TAG_U32, CINT_TAG_U64, CINT_TAG_BOOL};
    uint32_t t = 0;
    uint64_t mag = 0;
    int neg = value[0] == '-';
    for (uint32_t i = 0; i < 9u; i++) t = strcmp(type, names[i]) == 0 ? tags[i] : t;
    if (t != tag) return -1;
    if (tag == CINT_TAG_BOOL) {
        *bits = strcmp(value, "true") == 0 ? 1u : 0u;
        return strcmp(value, "true") == 0 || strcmp(value, "false") == 0 ? 0 : -1;
    }
    const char *p = value + neg;
    if (*p == '\0' || strspn(p, "0123456789") != strlen(p) || strlen(p) > 20u) return -1;
    for (; *p != '\0'; p++) {
        uint64_t digit = (uint64_t)(*p - '0');
        if (mag > (UINT64_MAX - digit) / 10u) return -1;
        mag = mag * 10u + digit;
    }
    unsigned w = cint_rt_tag_width(tag);
    if (cint_rt_tag_signed(tag)) {
        uint64_t half = (uint64_t)1 << (w - 1u);
        *bits = neg ? (uint64_t)0 - mag : mag;
        return neg ? (mag > half ? -1 : 0) : (mag >= half ? -1 : 0);
    }
    *bits = mag;
    return neg || (w < 64u && mag > ((uint64_t)1 << w) - 1u) ? -1 : 0;
}

/* The directory of path, malloc'd, or NULL: "." for a bare name; *cut is where the name starts. */
static char *dir_of(const char *path, size_t *cut)
{
    size_t k = strlen(path);
    while (k > 0 && path[k - 1u] != '/' && path[k - 1u] != '\\') k--;
    char *dir = k > 1u ? malloc(k) : wb_copy(k != 0 ? SEP : ".");
    if (dir != NULL && k > 1u) (void)snprintf(dir, k, "%s", path);
    *cut = k;
    return dir;
}

/* Builds the program of o (or reads the SIR text of --sir), runs its program entry or --entry on
 * cpu-sir-interp with the workbench defaults (SPEC-06 1.4) under a recorder, and leaves the
 * recording in d, with each module's source. Returns 0, or the exit status with the reason in
 * err; a compile error was reported by `cint` itself. */
static int record_program(dbg *d, const locate_opts *o, int json, wb_buf *err)
{
    char *dir = NULL, *rel = NULL, *self = NULL, *exe = NULL, *outdir = NULL, *stage = NULL;
    const char *cint = o->cint, *out = o->out, *sir = o->sir;
    cint_sir_program *prog = NULL;
    cint_sir_image *img = NULL;
    cint_sir_error serr;
    cint_code_module *code = NULL;
    cint_recorder *r = NULL;
    cint_ctx *ctx = NULL;
    cint_sink sink;
    wb_buf rec = {0};
    uint64_t args[16], result = 0;
    int status = CINT_EXIT_INTERNAL, st = 0;
    size_t cut = 0;
    memset(&serr, 0, sizeof serr);
    memset(args, 0, sizeof args);
    /* With --root, PROGRAM is the module's path under it; else its directory is the root. */
    dir = o->root != NULL ? wb_copy(o->root) : dir_of(d->path, &cut);
    rel = wb_copy(d->path + cut);
    if (dir == NULL || rel == NULL) goto done;
    for (char *q = rel; *q != '\0'; q++) *q = *q == '\\' ? '/' : *q;
    d->root = dir;
    if (sir == NULL) {
        if (cint == NULL && ((self = cli_self_dir()) == NULL || (cint = exe = wb_join(self, "cint" EXE)) == NULL)) {
            wb_s(err, "cannot locate the cint executable; give --cint PATH");
            status = CINT_EXIT_ENVIRONMENT;
            goto done;
        }
        if (out == NULL && (out = outdir = wb_join(dir, "build" SEP "debug")) == NULL) goto done;
        char *log = wb_join(out, "debug.stderr");
        int e = log != NULL ? wb_emit(cint, json, dir, rel, NULL, out, log, &st, err) : 1;
        size_t len = 0;
        char *said = e == 0 ? wb_read_file(log, &len) : NULL;
        if (said != NULL) {   /* cint's diagnostics, after it ends */
            (void)fwrite(said, 1, len, stderr);
            free(said);
        }
        free(log);
        if (e != 0 || st != CINT_EXIT_OK) {
            status = e == 2 ? CINT_EXIT_USAGE : e != 0 ? CINT_EXIT_ENVIRONMENT
                     : st == CINT_EXIT_COMPILE || st == CINT_EXIT_ENVIRONMENT ? st : CINT_EXIT_INTERNAL;
            goto done;
        }
        sir = out;
    }
    if ((stage = wb_stage_of(sir)) == NULL) goto done;
    if ((st = cint_sir_load(stage, &prog, &serr)) != CINT_SIR_OK ||
        (st = cint_sir_prepare(prog, &img, &serr)) != CINT_SIR_OK) {
        wb_f(err, "%s under %s: %s:%u:%u: %s",
             st == CINT_SIR_UNSUPPORTED ? "cpu-sir-interp does not run the program yet" : "the SIR text cannot be read",
             stage, serr.path, serr.line, serr.col, serr.text);
        status = st == CINT_SIR_INPUT ? CINT_EXIT_INTERNAL : CINT_EXIT_ENVIRONMENT;
        goto done;
    }
    const cint_observer *obs = cint_sir_observer(img);
    const cint_observer_entry *e = NULL;
    uint32_t root_mod = prog->nmod - 1u;
    for (uint32_t i = 0; o->entry != NULL && i < obs->entry_count; i++) {
        if (obs->entries[i].module == root_mod && obs->entries[i].name_len == strlen(o->entry) &&
            memcmp(obs->entries[i].name, o->entry, obs->entries[i].name_len) == 0) {
            e = &obs->entries[i];
        }
    }
    status = CINT_EXIT_USAGE;
    if (o->entry != NULL && e == NULL) {
        wb_f(err, "%s exports no function %s that an entry can call", prog->mods[root_mod].path, o->entry);
        goto done;
    }
    if (o->entry == NULL && cint_sir_main(img) == NULL) {
        wb_f(err, "%s has no script, main(), or single test to run; give --entry NAME", prog->mods[root_mod].path);
        goto done;
    }
    if (e != NULL && e->types[1] != o->nargs) {
        wb_f(err, "%s takes %u arguments; --args gives %u", o->entry, (unsigned)e->types[1], (unsigned)o->nargs);
        goto done;
    }
    for (uint32_t i = 0; e != NULL && i < o->nargs; i++) {
        if (typed(o->type[i], o->value[i], e->types[2u + i], &args[i]) != 0) {
            wb_f(err, "argument %u of %s, `%s %s`, is not a value of its type", (unsigned)i + 1u, o->entry, o->type[i],
                 o->value[i]);
            goto done;
        }
    }
    status = CINT_EXIT_INTERNAL;
    if ((code = calloc(prog->nmod, sizeof *code)) == NULL ||
        (ctx = new_ctx(cint_sir_cint_program(img), cint_sir_state_table(img), e == NULL ? o->depth : 0, 0)) == NULL) {
        goto done;
    }
    for (uint32_t k = 0; k < prog->nmod; k++) {
        cint_sir_text t;
        size_t n = 0;
        cint_sir_text_of(prog, k, &t);
        code[k].path.bytes = t.path;
        code[k].path.len = (uint32_t)t.path_len;
        code[k].sir = t.text;
        code[k].sir_len = t.len;
        code[k].source = source_of(d, t.path, t.path_len, &n) != NULL ? malloc(n + 1u) : NULL;
        if (code[k].source != NULL) {
            memcpy((void *)code[k].source, d->src, n);
            code[k].source_len = n;
        } else if (o->save_case != NULL) {   /* a case carries every module's source (SPEC-06 9.2) */
            wb_f(err, "a case needs the source of %.*s, which cannot be read", (int)t.path_len, t.path);
            status = CINT_EXIT_ENVIRONMENT;
            goto done;
        }
    }
    sink.write = wb_sink;
    sink.user = &rec;
    uint32_t mode = o->save_case != NULL ? CINT_RECORD_CASE : CINT_RECORD_FULL;
    cint_status cs = cint_rec_begin(ctx, obs, code, prog->nmod, mode, &sink, &r);
    if (cs == CINT_OK) {
        cs = e != NULL ? cint_rec_entry(r, (uint32_t)(e - obs->entries), args, o->fuel, o->depth, &result)
                       : cint_rec_main(r, cint_sir_main(img), o->fuel);
        cs = cint_rec_end(r) != CINT_OK ? CINT_RESOURCE : cs;
    }
    if (cs != CINT_OK && cs != CINT_FAULT) {
        wb_s(err, cs == CINT_RESOURCE ? "out of memory while recording the program" : "the recorder refused the entry");
        goto done;
    }
    if (o->save_case != NULL && wb_write_file(o->save_case, rec.p, rec.n) != 0) {
        wb_f(err, "cannot write the case %s", o->save_case);
        status = CINT_EXIT_ENVIRONMENT;
        goto done;
    }
    d->bytes = (uint8_t *)rec.p;
    d->len = rec.n;
    rec.p = NULL;
    status = 0;
done:
    if (status == CINT_EXIT_INTERNAL && err->n == 0) wb_s(err, "out of memory");
    cint_ctx_destroy(ctx);
    for (uint32_t k = 0; code != NULL && k < prog->nmod; k++) free((void *)code[k].source);
    free(code);
    cint_sir_release(img);
    cint_sir_free(prog);
    wb_free(&rec);
    free(rel);
    free(self);
    free(exe);
    free(outdir);
    free(stage);
    return status;   /* dir stays as d->root */
}

/* The position of a trace event, `path:line:col`, or empty. */
static void where_of(void *user, const wb_traced *x, char *out, size_t cap)
{
    (void)user;
    const cint_site *s = &x->e.site;
    out[0] = '\0';
    if (s->module < x->p->nmod && s->index >= 1u && s->index <= x->p->mods[s->module].nsite) {
        const si_module *m = &x->p->mods[s->module];
        (void)snprintf(out, cap, "%s:%u:%u", m->path, (unsigned)m->sites[s->index].line, (unsigned)m->sites[s->index].col);
    }
}

/* The bracket and the target of locate: the boundary before the target, or the initial state. */
static void bracket(wb_buf *b, const dbg *d, const cursor *before, const cursor *c, const char *target)
{
    wb_buf call = {0}, what = {0};
    wb_entry_text(&call, &d->rp.info, &c->entry);
    wb_f(b, "target     boundary %llu of %llu, %s: %s\nbracket    ", (unsigned long long)c->k,
         (unsigned long long)d->rp.info.boundaries, call.p != NULL ? call.p : "", target);
    call.n = 0;
    if (before->k == 0) {
        wb_s(b, "the initial state");
    } else {
        wb_entry_text(&call, &d->rp.info, &before->entry);
        outcome_text(&what, &before->e);
        wb_f(b, "boundary %llu, %s: %s", (unsigned long long)before->k, call.p != NULL ? call.p : "",
             what.p != NULL ? what.p : "");
    }
    wb_f(b, "; then boundary %llu\n", (unsigned long long)c->k);
    wb_free(&call);
    wb_free(&what);
}

/* The last LAST_OPS counted operations the tracer kept, with calls and returns among them. */
static void last_ops(wb_buf *b, const wb_tracer *t, uint64_t k, const cint_fault_record *rec)
{
    uint64_t from = t->kept, ops = 0;
    while (from > 0 && from + WB_TRACE_MAX > t->kept && ops < LAST_OPS) {
        ops += t->ring[(from - 1u) % WB_TRACE_MAX].e.kind != CINT_SIR_EV_RETURN;
        from--;
    }
    wb_f(b, "the last %llu counted operations of boundary %llu, of %llu trace events (:trace shows them all)\n",
         (unsigned long long)ops, (unsigned long long)k, (unsigned long long)t->kept);
    wb_trace_head(b);
    for (uint64_t i = from; i < t->kept; i++) {
        const wb_traced *x = &t->ring[i % WB_TRACE_MAX];
        char where[96];
        where_of(NULL, x, where, sizeof where);
        int at = rec != NULL && i + 1u == t->kept && x->e.kind != CINT_SIR_EV_RETURN && rec->code < 14u &&
                 x->e.site.module == rec->position.module && x->e.site.index == rec->position.index;
        wb_trace_line(b, &x->e, where, at ? WB_CODE_NAMES[rec->code] : NULL);
    }
}

/* --changed BINDING: the last boundary at which the global changed, by the backward scan of
 * SPEC-06 10.1 over the recording's one segment, so one replay that reads it at each boundary.
 * Returns 0, with sc->last 0 when it never changed and a divergence in why; else the exit
 * status, with the reason in err. */
static int scan_changed(dbg *d, const char *binding, wb_scan *sc, wb_buf *why, wb_buf *err)
{
    wb_buf all = {0};
    int status = replay_open(d, NULL, NULL, why), found = -1;
    if (status == 0 && wb_replay_run(&d->rp, 0, why) == 0) {   /* the first state */
        found = wb_global_named(d->rp.ctx, binding, &sc->prev, sc->name, sizeof sc->name, &all);
    }
    if (status == 0 && (found != 1 || all.failed)) {
        wb_f(err, found == 0 ? "no global %s in the recording's state" : found > 1 ? "%s names globals %s; give one "
             "with its module" : "the recording's first state cannot be read", binding, all.p != NULL ? all.p : "");
        status = found >= 0 && !all.failed ? CINT_EXIT_USAGE : CINT_EXIT_INTERNAL;
    }
    wb_free(&all);
    if (status == 0) {
        d->rp.at = wb_scan_at;
        d->rp.at_user = sc;
        (void)wb_replay_run(&d->rp, UINT64_MAX, why);
        d->rp.at = NULL;
        if (sc->bad) {
            wb_s(err, "a state on the way cannot be read");
            status = CINT_EXIT_INTERNAL;
        }
    }
    if (status == CINT_EXIT_CHECK) {   /* the recording does not load */
        fputs(why->p, stderr);
    }
    wb_replay_free(&d->rp);
    return status;
}

/* `cint debug locate` (note 6.4; SPEC-06 10.1): finds the first boundary that ends in a fault or
 * an error result, replays to it, and reports it. Exit 0 when it located one; 3 when there is
 * none, or replay stops before it with DIVERGED or UNREPLAYABLE. */
static int locate(dbg *d, int json, const locate_opts *o, int program, wb_buf *err)
{
    static cint_fault_record rec;
    static wb_fault f;
    cint_recording_info info;
    wb_tracer t;
    wb_buf b = {0}, why = {0};
    cursor c, before;
    uint8_t state[32];
    long long t0 = wb_now_ns();
    int status = CINT_EXIT_CHECK;
    static wb_scan sc;
    memset(&sc, 0, sizeof sc);
    memset(&t, 0, sizeof t);
    memset(&c, 0, sizeof c);
    memset(&before, 0, sizeof before);
    memset(state, 0, sizeof state);
    if (cint_recording_decode(d->bytes, d->len, &info) != CINT_OK) {
        fprintf(stderr, "%s is not a complete recording\n", d->path);
        return CINT_EXIT_CHECK;
    }
    if (o->changed != NULL && (status = scan_changed(d, o->changed, &sc, &why, err)) != 0) return status;
    while (next_boundary(&info, &c) == 0 && (o->changed != NULL ? c.k < sc.last
                                                                : c.e.status != CINT_FAULT && !c.e.has_error)) {
        before = c;
    }
    uint64_t k = c.e.kind == CINT_EVENT_OUTCOME && why.n == 0 && (o->changed == NULL || sc.last != 0) ? c.k : 0;
    /* As :trace does, one replay counts the trace events before the target's entry, and the next
     * keeps the entry's own. */
    t.rp = &d->rp;
    status = replay_open(d, wb_on_trace, &t, &why);
    if (status == 0 && k > 1u && wb_replay_run(&d->rp, k - 1u, &why) == 0) {
        t.skip = t.seen;
        t.seen = 0;
        wb_replay_free(&d->rp);
        status = replay_open(d, wb_on_trace, &t, &why);
    }
    if (status == 0 && why.n == 0 && k != 0 && (t.ring = malloc(WB_TRACE_MAX * sizeof *t.ring)) == NULL) {
        wb_s(&why, "out of memory\n");
        status = CINT_EXIT_INTERNAL;
    }
    if (status == 0 && why.n == 0) (void)wb_replay_run(&d->rp, k != 0 ? k : UINT64_MAX, &why);
    t0 = wb_elapsed_ns(t0, wb_now_ns());
    if (status != 0) {
        if (status == CINT_EXIT_CHECK) fputs(why.p, stderr);
        else wb_put(err, why.p, why.n != 0 ? why.n - 1u : 0);
        goto done;
    }
    (void)cint_ctx_state_hash(d->rp.ctx, state);
    status = why.n != 0 || k == 0 ? CINT_EXIT_CHECK : CINT_EXIT_OK;
    if (program) {
        wb_f(&b, "program    %s   evaluator cpu-sir-interp, %s%s\n", d->path,
             o->save_case != NULL ? "recorded as the case " : "under a recording",
             o->save_case != NULL ? o->save_case : "");
    } else {
        wb_f(&b, "%s %s   %llu %s\n", d->rp.info.is_case ? "case      " : "recording ", d->path,
             (unsigned long long)d->rp.info.boundaries,
             BOUNDARIES(d->rp.info.boundaries));
    }
    if (status == CINT_EXIT_CHECK) {
        if (why.n == 0 && o->changed != NULL) {
            wb_f(&b, "target     none: %s did not change in the %llu %s replayed\n", sc.name,
                 (unsigned long long)d->rp.info.boundaries, BOUNDARIES(d->rp.info.boundaries));
        } else if (why.n == 0) {
            wb_f(&b, "target     none: no boundary ends in a fault or an error result; %llu replayed and compared "
                     "with the recording\n", (unsigned long long)d->rp.info.boundaries);
        }
        wb_put(&b, why.p, why.n);
    } else if (o->changed != NULL) {   /* the backward scan (SPEC-06 10.1) */
        char was[300] = "(none)", is[300] = "(none)";
        wb_buf target = {0};
        int found = cint_ctx_fault(d->rp.ctx, &rec) == CINT_OK && rec.code != 0;
        if (sc.was.len != 0) (void)wb_tval_text(&sc.was, was, sizeof was, 0);
        if (sc.now.len != 0) (void)wb_tval_text(&sc.now, is, sizeof is, 0);
        wb_f(&target, "the last change of %s, from %s to %s", sc.name, was, is);
        bracket(&b, d, &before, &c, target.p != NULL ? target.p : "");
        wb_free(&target);
        last_ops(&b, &t, k, found ? &rec : NULL);
        if (json) {   /* CINT-CHANGE-1 (SPEC-06 15) */
            b.n = 0;
            wb_s(&b, "{\"binding\":");
            wb_jstr(&b, sc.name, strlen(sc.name));
            wb_f(&b, ",\"boundary\":\"%llu\",\"from\":", (unsigned long long)k);
            wb_tval_json(&b, &sc.was);
            wb_s(&b, ",\"schema\":\"CINT-CHANGE-1\",\"to\":");
            wb_tval_json(&b, &sc.now);
            wb_s(&b, "}\n");
        }
    } else if (c.e.status == CINT_FAULT) {
        char position[48];
        (void)snprintf(position, sizeof position, "boundary %llu", (unsigned long long)k);
        int found = cint_ctx_fault(d->rp.ctx, &rec) == CINT_OK && rec.code != 0;
        char site[96];
        wb_site_id(found ? d->rp.prog : NULL, found ? rec.position : (cint_site){0, 0}, site, sizeof site);
        wb_report r = {c.e.consumed, "the post-fault state of SPEC-01 IM-121, reached by replay",
                       site[0] != '\0' ? site : NULL, position, "cpu-sir-interp, by replay", 0u, {0, 0, 0, 0}};
        if (found && d->rp.img != NULL) r.rank = cint_sir_dispatch_domain(d->rp.img, r.extents);
        if (c.e.data == NULL || wb_fault_decode(c.e.data, c.e.data_len, &f, WB_RECORD_RUN) != 0) {
            wb_s(err, "the fault record of the target cannot be decoded");
            status = CINT_EXIT_INTERNAL;
            goto done;
        }
        if (json) {
            b.n = 0;
            wb_fault_json(&b, &f, &r);
            wb_s(&b, "\n");
        } else {
            bracket(&b, d, &before, &c, "the first fault, reproduced by replay");
            wb_fault_text(&b, &f, &r, source_of, d);
            last_ops(&b, &t, k, found ? &rec : NULL);
        }
    } else {   /* an error result (BX14-18) */
        const cint_error_set *set = NULL;
        uint64_t tag = 0;
        const char *value = cint_ctx_error(d->rp.ctx, &set, &tag) == CINT_OK && set != NULL && tag >= 1u &&
                            tag <= set->count ? set->values[tag - 1u] : NULL;
        if (value == NULL) {
            wb_s(err, "the error result of the target cannot be read from the context");
            status = CINT_EXIT_INTERNAL;
            goto done;
        }
        if (json) {
            b.n = 0;
            wb_s(&b, "{\"schema\":\"CINT-ERROR-1\",\"set\":");
            wb_jstr(&b, set->name, strlen(set->name));
            wb_f(&b, ",\"tag\":{\"t\":\"U%u\",\"v\":\"%llu\"},\"value\":", 8u << (set->tag - CINT_TAG_U8),
                 (unsigned long long)tag);
            wb_jstr(&b, value, strlen(value));
            wb_s(&b, "}\n");
        } else {
            bracket(&b, d, &before, &c, "the first error result, reproduced by replay");
            wb_f(&b, "error: %s.%s (tag %llu)\n", set->name, value, (unsigned long long)tag);
            wb_error_path(&b, &t, tag, where_of, NULL);
        }
    }
    fflush(stdout);
    (void)fwrite(b.p, 1, b.n, json && status == CINT_EXIT_CHECK ? stderr : stdout);
    if (o->receipt != NULL && put_receipt(d, o->receipt, k, state, t0) != 0) {
        wb_f(err, "cannot write the receipt %s", o->receipt);
        status = CINT_EXIT_ENVIRONMENT;
    }
done:
    free(t.ring);
    wb_free(&b);
    wb_free(&why);
    return status;
}

/* ------------------------------------------------------------------ the command line */

/* A count of at least min, in decimal. Returns 0, or -1. */
/* `cint debug RECORDING`: the recording opened read-only in a session (SPEC-06 10.4; unit 3b-S),
 * whose directory is --out, by default build/debug beside the recording. The session reports
 * its own errors. */
static int session(const dbg *d, const char *out, int echo, wb_buf *err)
{
    wb_session_options so;
    size_t cut = 0;
    char *dir = out == NULL ? dir_of(d->path, &cut) : NULL;
    char *own = dir != NULL ? wb_join(dir, "build" SEP "debug") : NULL;
    free(dir);
    if (out == NULL && own == NULL) {
        wb_s(err, "out of memory");
        return CINT_EXIT_INTERNAL;
    }
    memset(&so, 0, sizeof so);
    so.out = out != NULL ? out : own;
    so.echo = echo;
    so.debug = d->bytes;
    so.debug_len = d->len;
    so.debug_path = d->path;
    int status = wb_session(&so, stdin);
    free(own);
    return status;
}

/* `cint debug diff CASE`: cpu-c17 against cpu-sir-interp unless --ref and --cand say otherwise;
 * cpu-c17's libraries are built in --out DIR, by default build/debug beside the case, by the cint
 * beside the workbench or --cint PATH. */
static int diff_case(wb_diff_opts *df, const char *file, int json, const char *cint, const char *out, wb_buf *err)
{
    char *self = NULL, *exe = NULL, *dir = NULL, *own = NULL;
    size_t cut = 0;
    int status = CINT_EXIT_INTERNAL;
    df->ref = df->ref != NULL ? df->ref : "cpu-c17";
    df->cand = df->cand != NULL ? df->cand : "cpu-sir-interp";
    for (int i = 0; i < 2; i++) {
        const char *b = i == 0 ? df->ref : df->cand;
        if (strcmp(b, "gpu-cuda") == 0) {
            wb_s(err, "gpu-cuda is compared once there is a gpu-cuda library: `cint build --lib` builds cpu-c17 "
                      "only");
            return CINT_EXIT_ENVIRONMENT;
        }
        if (strcmp(b, "cpu-c17") != 0 && strcmp(b, "cpu-sir-interp") != 0) {
            wb_f(err, "--ref and --cand take cpu-c17 or cpu-sir-interp, not %s", b);
            return CINT_EXIT_USAGE;
        }
    }
    if (cint == NULL && (strcmp(df->ref, "cpu-c17") == 0 || strcmp(df->cand, "cpu-c17") == 0) &&
        ((self = cli_self_dir()) == NULL || (cint = exe = wb_join(self, "cint" EXE)) == NULL)) {
        wb_s(err, "cannot locate the cint executable; give --cint PATH");
        status = CINT_EXIT_ENVIRONMENT;
    } else if (out == NULL && ((dir = dir_of(file, &cut)) == NULL || (out = own = wb_join(dir, "build" SEP "debug")) == NULL)) {
        wb_s(err, "out of memory");
    } else {
        df->path = file;
        df->json = json;
        df->cint = cint;
        df->out = out;
        status = wb_diff(df, err);
    }
    free(self);
    free(exe);
    free(dir);
    free(own);
    return status;
}

static int count_of(const char *s, int64_t min, int64_t *out)
{
    int64_t v = 0;
    if (*s == '\0' || strspn(s, "0123456789") != strlen(s) || strlen(s) > 18u) return -1;
    for (; *s != '\0'; s++) v = v * 10 + (*s - '0');
    *out = v;
    return v >= min ? 0 : -1;
}

int wb_debug(int json, int argc, char **argv, wb_buf *err)
{
    const char *cmd = NULL, *file = NULL, *receipt_path = NULL, *bad = NULL;
    locate_opts o;
    wb_diff_opts df;
    dbg d;
    int64_t to = 0;
    int status = CINT_EXIT_USAGE, echo = 0;
    memset(&o, 0, sizeof o);
    memset(&df, 0, sizeof df);
    memset(&d, 0, sizeof d);
    o.depth = CINT_DEFAULT_DEPTH;
    o.fuel = CINT_FUEL_UNBOUNDED;
    for (int i = 1; i < argc && bad == NULL; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        int is_locate = cmd != NULL && strcmp(cmd, "locate") == 0, is_session = cmd == NULL || file == cmd;
        int is_diff = cmd != NULL && strcmp(cmd, "diff") == 0;
        const char **slot = is_diff ? (strcmp(a, "--ref") == 0 ? &df.ref : strcmp(a, "--cand") == 0 ? &df.cand
                                       : strcmp(a, "--receipt") == 0 ? &df.receipt : strcmp(a, "--cint") == 0 ? &o.cint
                                       : strcmp(a, "--out") == 0 ? &o.out : strcmp(a, "--compiler-identity") == 0 ? &df.tool[0]
                                       : strcmp(a, "--interpreter-identity") == 0 ? &df.tool[1]
                                       : strcmp(a, "--workbench-identity") == 0 ? &df.tool[2] : NULL)
                          : strcmp(a, "--source-identity") == 0 && !is_session ? &df.tool[1]
                          : !is_locate ? (cmd != NULL && strcmp(cmd, "replay") == 0 && strcmp(a, "--receipt") == 0
                                              ? &receipt_path : is_session && strcmp(a, "--out") == 0 ? &o.out : NULL)
                          : strcmp(a, "--root") == 0 ? &o.root : strcmp(a, "--cint") == 0 ? &o.cint
                          : strcmp(a, "--out") == 0 ? &o.out : strcmp(a, "--sir") == 0 ? &o.sir
                          : strcmp(a, "--entry") == 0 ? &o.entry : strcmp(a, "--when") == 0 ? &o.when
                          : strcmp(a, "--changed") == 0 ? &o.changed : strcmp(a, "--save-case") == 0 ? &o.save_case
                          : strcmp(a, "--receipt") == 0 ? &o.receipt : NULL;
        if (strcmp(a, "--json") == 0) {
            continue;
        } else if (is_session && strcmp(a, "--echo") == 0) {
            echo = 1;
        } else if (slot != NULL) {
            bad = *slot != NULL || v == NULL ? "an option needs one value and is given at most once" : NULL;
            *slot = v;
            i++;
        } else if (cmd != NULL && strcmp(a, "--to") == 0 && strcmp(cmd, "replay") == 0) {
            bad = to != 0 || v == NULL || count_of(v, 1, &to) != 0 ? "--to takes a boundary, 1 or more" : NULL;
            i++;
        } else if (is_locate && (strcmp(a, "--depth") == 0 || strcmp(a, "--fuel") == 0)) {
            int depth = a[2] == 'd';
            bad = v == NULL || count_of(v, depth, depth ? &o.depth : &o.fuel) != 0
                      ? (depth ? "--depth takes a limit of 1 or more" : "--fuel takes an allowance of 0 or more") : NULL;
            i++;
        } else if (is_locate && strcmp(a, "--args") == 0) {   /* `I64 0` as one word or two */
            while (bad == NULL && i + 1 < argc && strncmp(argv[i + 1], "--", 2u) != 0) {
                const char *w = argv[++i], *sp = strchr(w, ' ');
                size_t tl = sp != NULL ? (size_t)(sp - w) : strlen(w);
                bad = o.nargs == 16u || tl >= sizeof o.type[0] || (sp == NULL && i + 1 == argc)
                          ? "--args takes at most 16 typed values, each `I64 0` or `Bool true`" : NULL;
                if (bad == NULL) {
                    memcpy(o.type[o.nargs], w, tl);
                    o.type[o.nargs][tl] = '\0';
                    o.value[o.nargs++] = sp != NULL ? sp + 1 : argv[++i];
                }
            }
        } else if (a[0] == '-' || (cmd != NULL && file != NULL)) {
            bad = "debug takes the options of --help";
        } else if (cmd == NULL) {
            cmd = a;
            file = strcmp(a, "replay") != 0 && strcmp(a, "locate") != 0 && strcmp(a, "diff") != 0 ? a : NULL;
        } else {
            file = a;
        }
    }
    if (bad == NULL && (cmd == NULL || file == NULL)) {
        bad = cmd == NULL ? "debug takes a recording, a case, or one of diff, replay, and locate"
                          : "diff, replay, and locate take a case or a recording, or for locate a PROGRAM.ci";
    }
    if (bad == NULL && echo && file != cmd) bad = "debug takes the options of --help";
    if (bad == NULL && o.nargs != 0 && o.entry == NULL) bad = "--args needs --entry NAME";
    for (int k = 0; k < 3 && bad == NULL; k++)   /* as cint-interp's --source-identity */
        if (df.tool[k] != NULL && (strlen(df.tool[k]) != 64u || strspn(df.tool[k], "0123456789abcdef") != 64u))
            bad = "a source identity is 64 lowercase hexadecimal digits";
    if (bad != NULL) {
        wb_s(err, bad);
        return CINT_EXIT_USAGE;
    }
    if (strcmp(cmd, "diff") == 0) return diff_case(&df, file, json, o.cint, o.out, err);
    if (o.when != NULL) {   /* SPEC-06 10.1 */
        wb_s(err, o.changed != NULL ? "--when and --changed exclude each other" : "--when is not provided yet");
        return o.changed != NULL ? CINT_EXIT_USAGE : CINT_EXIT_ENVIRONMENT;
    }
    size_t flen = strlen(file);
    int program = strcmp(cmd, "locate") == 0 && flen > 3u && strcmp(file + flen - 3u, ".ci") == 0;
    if (!program && (o.save_case != NULL || o.root != NULL || o.cint != NULL || (o.out != NULL && file != cmd) ||
                     o.sir != NULL || o.entry != NULL)) {
        wb_s(err, "--root, --cint, --out, --sir, --entry, --args, and --save-case take a PROGRAM.ci");
        return CINT_EXIT_USAGE;
    }
    d.path = file;
    d.tool = df.tool[1];
    if (program) {
        status = record_program(&d, &o, json, err);
    } else if ((d.bytes = (uint8_t *)wb_read_file(file, &d.len)) == NULL) {
        wb_f(err, "cannot read %s", file);
        status = CINT_EXIT_ENVIRONMENT;
    } else {
        status = 0;
    }
    if (status == 0 && strcmp(cmd, "replay") == 0) status = replay(&d, json, (uint64_t)to, receipt_path, err);
    else if (status == 0 && strcmp(cmd, "locate") == 0) status = locate(&d, json, &o, program, err);
    else if (status == 0) status = session(&d, o.out, echo, err);
    fflush(stdout);
    wb_replay_free(&d.rp);
    wb_free(&d.costs);
    free(d.bytes);
    free((void *)d.root);
    free(d.src);
    free(d.src_path);
    return status;
}
