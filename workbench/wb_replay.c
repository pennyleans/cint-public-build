/* wb_replay.c: replay of a box 13 recording on a context of its own, and the trace events of
 * one entry (box 14 units 3a and 3b; wb_replay.h). */
#include <stdlib.h>
#include <string.h>

#include "sir.h"
#include "wb_replay.h"

void wb_replay_free(wb_replayer *rp)
{
    cint_ctx_destroy(rp->ctx);
    for (uint32_t i = rp->n; i-- > 0;) {
        cint_sir_release(rp->imgs[i]);
        cint_sir_free(rp->progs[i]);
    }
    free(rp->imgs);
    free(rp->progs);
    memset(rp, 0, sizeof *rp);
}

void wb_replay_keep(wb_replayer *rp, cint_ctx **ctx, cint_sir_program **prog, cint_sir_image **img)
{
    *ctx = rp->ctx;
    *prog = rp->prog;
    *img = rp->img;
    for (uint32_t i = 0; i < rp->n; i++) {
        if (rp->progs[i] == rp->prog) {
            rp->progs[i] = NULL;
            rp->imgs[i] = NULL;
        }
    }
    rp->ctx = NULL;
    rp->prog = NULL;
    rp->img = NULL;
}

void wb_scan_at(void *user, uint64_t at, cint_ctx *ctx)
{
    wb_scan *sc = user;
    wb_tval v;
    char shown[320];
    sc->bad |= wb_global_named(ctx, sc->name, &v, shown, sizeof shown, NULL) < 0;
    if (!sc->bad && at > sc->from && (v.len != sc->prev.len || memcmp(v.bytes, sc->prev.bytes, v.len) != 0)) {
        sc->last = at;
        sc->was = sc->prev;
        sc->now = v;
    }
    sc->prev = v;
}

/* Loads and prepares the code of revision event e, or of the first revision when e is NULL, as
 * the program replay is in. Returns 0, or -1. */
static int replay_code(wb_replayer *rp, const cint_recording_event *e, uint32_t count)
{
    cint_sir_text *t = calloc(count != 0 ? count : 1u, sizeof *t);
    cint_sir_program *p = NULL;
    cint_sir_image *img = NULL;
    cint_sir_error err;
    int bad = t == NULL || count == 0;
    for (uint32_t k = 0; !bad && k < count; k++) {
        cint_code_module m;
        bad = cint_recording_module(&rp->info, e, k, &m) != CINT_OK;
        t[k].path = m.path.bytes;
        t[k].path_len = m.path.len;
        t[k].text = m.sir;
        t[k].len = m.sir_len;
    }
    bad = bad || cint_sir_load_text(t, count, &p, &err) != CINT_SIR_OK ||
          cint_sir_prepare(p, &img, &err) != CINT_SIR_OK;
    free(t);
    if (!bad && rp->n == rp->cap) {
        uint32_t cap = rp->cap * 2u + 4u;
        cint_sir_program **gp = realloc(rp->progs, cap * sizeof *gp);
        rp->progs = gp != NULL ? gp : rp->progs;
        cint_sir_image **gi = gp != NULL ? realloc(rp->imgs, cap * sizeof *gi) : NULL;
        rp->imgs = gi != NULL ? gi : rp->imgs;
        rp->cap = gi != NULL ? cap : rp->cap;
        bad = gi == NULL;
    }
    if (bad) {
        cint_sir_release(img);
        cint_sir_free(p);
        return -1;
    }
    rp->progs[rp->n] = rp->prog = p;
    rp->imgs[rp->n++] = rp->img = img;
    if (rp->trace != NULL) cint_sir_trace(img, rp->trace, rp->trace_user);
    return 0;
}

/* The boundary hook: replay calls a copy of the program's observer whose entries run the
 * program's and then rp->at. One replay at a time uses it, as the interpreter runs one image. */
#define WB_HOOKED 256u   /* entries of one observer, as many as the interpreter has (EX_ENTRIES) */
static wb_replayer *g_rp;
static const cint_observer *g_orig;
static cint_observer g_obs;
static cint_observer_entry g_entries[WB_HOOKED];
static cint_main_fn g_main;

static cint_status passed(cint_ctx *ctx, cint_status st)
{
    if (st == CINT_OK || st == CINT_FAULT) g_rp->at(g_rp->at_user, ++g_rp->passed, ctx);
    return st;
}

static cint_status hooked(uint32_t k, cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args,
                          uint64_t *result)
{
    return passed(ctx, g_orig->entries[k].call(ctx, fuel, depth, args, result));
}

static cint_status hooked_main(cint_ctx *ctx, int64_t fuel)
{
    return passed(ctx, g_main(ctx, fuel));
}

#define WB_HOOK(h, l)                                                                                    \
    static cint_status hook_##h##l(cint_ctx *c, int64_t f, int64_t d, const uint64_t *a, uint64_t *r) \
    {                                                                                                  \
        return hooked(0x##h##l##u, c, f, d, a, r);                                                     \
    }
#define WB_HOOK16(h)                                                                                   \
    WB_HOOK(h, 0) WB_HOOK(h, 1) WB_HOOK(h, 2) WB_HOOK(h, 3) WB_HOOK(h, 4) WB_HOOK(h, 5) WB_HOOK(h, 6)   \
    WB_HOOK(h, 7) WB_HOOK(h, 8) WB_HOOK(h, 9) WB_HOOK(h, a) WB_HOOK(h, b) WB_HOOK(h, c) WB_HOOK(h, d)   \
    WB_HOOK(h, e) WB_HOOK(h, f)
#define WB_NAME16(h)                                                                                   \
    hook_##h##0, hook_##h##1, hook_##h##2, hook_##h##3, hook_##h##4, hook_##h##5, hook_##h##6,          \
    hook_##h##7, hook_##h##8, hook_##h##9, hook_##h##a, hook_##h##b, hook_##h##c, hook_##h##d,          \
    hook_##h##e, hook_##h##f
WB_HOOK16(0) WB_HOOK16(1) WB_HOOK16(2) WB_HOOK16(3) WB_HOOK16(4) WB_HOOK16(5) WB_HOOK16(6) WB_HOOK16(7)
WB_HOOK16(8) WB_HOOK16(9) WB_HOOK16(a) WB_HOOK16(b) WB_HOOK16(c) WB_HOOK16(d) WB_HOOK16(e) WB_HOOK16(f)
static const cint_observer_fn HOOKS[WB_HOOKED] = {
    WB_NAME16(0), WB_NAME16(1), WB_NAME16(2), WB_NAME16(3), WB_NAME16(4), WB_NAME16(5), WB_NAME16(6), WB_NAME16(7),
    WB_NAME16(8), WB_NAME16(9), WB_NAME16(a), WB_NAME16(b), WB_NAME16(c), WB_NAME16(d), WB_NAME16(e), WB_NAME16(f)};

/* The observer and program entry replay calls for o and *main_fn: themselves without a hook,
 * else the hooked copies. */
static const cint_observer *hook(wb_replayer *rp, const cint_observer *o, cint_main_fn *main_fn)
{
    if (rp->at == NULL || o == NULL || o->entry_count > WB_HOOKED) return o;
    g_rp = rp;
    g_orig = o;
    g_obs = *o;
    for (uint32_t k = 0; k < o->entry_count; k++) {
        g_entries[k] = o->entries[k];
        g_entries[k].call = HOOKS[k];
    }
    g_obs.entries = g_entries;
    g_main = *main_fn;
    *main_fn = g_main != NULL ? hooked_main : NULL;
    return &g_obs;
}

/* The program of a later revision: from the event's code, or the one replay is in when the
 * change kept the revision and so carries none. */
static const cint_observer *replay_revision(void *user, const cint_recording_event *e, cint_main_fn *main_fn,
                                            const cint_state_table **table)
{
    wb_replayer *rp = user;
    uint8_t now[32];
    cint_sir_revision(rp->prog, now);
    if (e->module_count != 0 ? replay_code(rp, e, e->module_count) != 0 : memcmp(now, e->revision, 32u) != 0)
        return NULL;
    *main_fn = cint_sir_main(rp->img);
    *table = cint_sir_state_table(rp->img);
    return hook(rp, cint_sir_observer(rp->img), main_fn);
}

int wb_replay_load(wb_replayer *rp, const void *rec, size_t n, cint_sir_trace_fn fn, void *user)
{
    memset(rp, 0, sizeof *rp);
    rp->trace = fn;
    rp->trace_user = user;
    return cint_recording_decode(rec, n, &rp->info) == CINT_OK && replay_code(rp, NULL, rp->info.module_count) == 0
               ? 0 : -1;
}

int wb_replay_run(wb_replayer *rp, uint64_t stop, wb_buf *b)
{
    cint_replay_config cfg;
    if (stop == 0) {
        if (cint_ctx_restore(rp->ctx, rp->info.initial, rp->info.initial_len) != CINT_OK) {
            wb_s(b, "workbench: the recording's initial state does not restore (SPEC-03 H-19a)\n");
            return -1;
        }
        return 0;
    }
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.stop = stop == UINT64_MAX ? 0 : stop;
    cfg.main_fn = cint_sir_main(rp->img);   /* a recording of the program entry (form 1) */
    cfg.entries = hook(rp, cint_sir_observer(rp->img), &cfg.main_fn);
    cfg.revision = replay_revision;
    rp->passed = 0;
    cfg.user = rp;
    cint_status st = cint_replay(rp->ctx, &rp->info, &cfg, &rp->rep);
    if (st != CINT_OK) {
        wb_s(b, "workbench: the recording does not replay on a context made for it\n");
    } else if (rp->rep.status == CINT_DIVERGED) {
        wb_f(b, "replay DIVERGED at event %llu: its %s differs from the recording (SPEC-03 H-17)\n",
             (unsigned long long)rp->rep.event, cint_replay_what(rp->rep.what) != NULL ?
             cint_replay_what(rp->rep.what) : "outcome");
    } else if (rp->rep.status != CINT_OK) {
        wb_f(b, "replay stops before event %llu: UNREPLAYABLE\n", (unsigned long long)rp->rep.event);
    }
    return st != CINT_OK || rp->rep.status != CINT_OK ? -1 : 0;
}

void wb_on_trace(void *user, const cint_sir_event *e)
{
    wb_tracer *t = user;
    if (t->seen++ >= t->skip && t->ring != NULL) {
        wb_traced *x = &t->ring[t->kept++ % WB_TRACE_MAX];
        x->e = *e;
        x->p = t->rp->prog;
    }
}

void wb_bits_text(const char *op, uint32_t len, uint64_t v, char *out, size_t cap)
{
    uint32_t k = len;
    int64_t sv;
    while (k > 0 && op[k - 1u] != '.') k--;
    memcpy(&sv, &v, sizeof sv);
    if (len - k >= 2u && op[k] == 'u' && op[k + 1u] >= '1' && op[k + 1u] <= '9')
        (void)snprintf(out, cap, "%llu", (unsigned long long)v);
    else if (len - k == 4u && memcmp(op + k, "bool", 4u) == 0)
        (void)snprintf(out, cap, "%s", v != 0 ? "true" : "false");
    else (void)snprintf(out, cap, "%lld", (long long)sv);
}

void wb_trace_head(wb_buf *b)
{
    wb_f(b, "%5s  %-18s %-22s %-24s %s\n", "ord", "position", "operation", "operands", "value");
}

void wb_trace_line(wb_buf *b, const cint_sir_event *e, const char *where, const char *fault)
{
    const char *op = e->kind == CINT_SIR_EV_RETURN ? "return" : e->operation != NULL ? e->operation : "";
    uint32_t oplen = e->kind == CINT_SIR_EV_RETURN ? 6u : e->operation != NULL ? e->operation_len : 0u;
    char ord[24], operands[96], shown[48];
    (void)snprintf(ord, sizeof ord, e->kind == CINT_SIR_EV_RETURN ? "-" : "%llu", (unsigned long long)e->step);
    operands[0] = '\0';
    for (uint32_t k = 0; k < e->operand_count && k < 3u; k++) {
        char one[32];
        wb_bits_text(op, oplen, e->operands[k], one, sizeof one);
        (void)snprintf(operands + strlen(operands), sizeof operands - strlen(operands), "%s%s", k != 0 ? ", " : "",
                       one);
    }
    shown[0] = '\0';
    if (e->error != 0u) {   /* a return or a result form with an error (box 12) */
        (void)snprintf(shown, sizeof shown, "error %llu", (unsigned long long)e->error);
    } else if (e->has_result) {
        wb_bits_text(op, oplen, e->result, shown, sizeof shown);
    } else if (fault != NULL) {
        (void)snprintf(shown, sizeof shown, "%s", fault);
    }
    size_t line_at = b->n;
    wb_f(b, "%5s  %-18s %-22.*s %-24s %s", ord, where, (int)oplen, op, operands, shown);
    while (!b->failed && b->n > line_at && b->p[b->n - 1u] == ' ') {   /* no blanks at the end of a line */
        b->n--;
    }
    wb_s(b, "\n");
}

/* The node that event x ran, found by its operation's spelling, which points into its module's
 * text: its module, function, and index. 0, or -1 when no node has it. */
static int node_of(const wb_traced *x, uint32_t *mi, uint32_t *fi, uint32_t *ni)
{
    for (uint32_t k = 0; x->e.operation != NULL && k < x->p->nmod; k++) {
        const si_module *m = &x->p->mods[k];
        uintptr_t at = (uintptr_t)x->e.operation - (uintptr_t)m->text;
        for (uint32_t f = 0; at < m->len && f < m->nfunc; f++) {
            for (uint32_t n = m->funcs[f].first; n < m->funcs[f].first + m->funcs[f].count; n++) {
                if (m->nodes[n].word.at == at) {
                    *mi = k;
                    *fi = f;
                    *ni = n;
                    return 0;
                }
            }
        }
    }
    return -1;
}

/* 1 when temporary v, defined by a node of m from `from` to `to` - 1, is tag or is computed from
 * it within depth steps: a widening to a wider set (SPEC-04 LS-95). */
static int from_tag(const si_module *m, uint32_t from, uint32_t to, uint32_t v, int32_t tag, int depth)
{
    if ((int32_t)v == tag) return 1;
    for (uint32_t w = to; depth > 0 && w-- > from;) {   /* the node that defines v */
        const si_node *n = &m->nodes[w];
        if (n->op != OP_DATA && n->dst == (int32_t)v) {
            for (uint32_t q = 0; q < n->nopd; q++) {
                const si_opd *o = &m->opds[n->opd + q];
                if (o->kind == OD_TEMP && from_tag(m, from, w, o->index, tag, depth - 1)) return 1;
            }
            return 0;
        }
    }
    return 0;
}

/* The name of the function event x ran in, or with callee 1 of the function its call node
 * calls, in out (unchanged when there is no node). Returns 1 when a `try` passed on the error
 * of x's node, a call or a result form: its error.tag is the next node, and the first
 * return.error or error.leave after that gives that tag or its widening (SPEC-04 LS-95). */
static int tried(const wb_traced *x, int callee, char *out, size_t cap)
{
    uint32_t mi = 0, fi = 0, k = 0;
    if (node_of(x, &mi, &fi, &k) != 0) return 0;
    const si_module *m = &x->p->mods[mi], *cm = callee ? &x->p->mods[m->nodes[k].cmod] : m;
    const si_func *f = &m->funcs[fi], *fn = callee ? &cm->funcs[m->nodes[k].cfn] : f;
    int32_t tag = k + 1u < f->first + f->count && m->nodes[k + 1u].op == OP_ETAG ? m->nodes[k + 1u].dst : -1;
    (void)snprintf(out, cap, "%.*s", (int)fn->name.len, (const char *)cm->text + fn->name.at);
    for (uint32_t i = k + 2u; tag >= 0 && i < f->first + f->count; i++) {
        const si_node *n = &m->nodes[i];
        if ((n->op == OP_RETURN && n->form == 1) || n->op == OP_ELEAVE) {
            const si_opd *o = &m->opds[n->opd];
            return o->kind == OD_TEMP && from_tag(m, k + 2u, i, o->index, tag, 4);
        }
    }
    return 0;
}

void wb_error_path(wb_buf *b, const wb_tracer *t, uint64_t tag, wb_where_fn where, void *user)
{
    uint64_t n = t->kept, d = 0, k = 0, j = 0, top = 0;
    uint64_t *last = n <= WB_TRACE_MAX ? calloc(3u * (n + 1u), sizeof *last) : NULL;
    char name[128] = "the entry's function", callee[128] = "", at[96] = "";
    if (last == NULL) {
        wb_f(b, n > WB_TRACE_MAX ? "the path needs every call and return of the entry: %llu trace events, of which "
                                   "%u are kept\n" : "workbench: out of memory for %llu trace events (%u kept)\n",
             (unsigned long long)n, WB_TRACE_MAX);
        return;
    }
    /* last[d]: 1 + the index of the last call made at depth d that returned an error; link[i]: that
     * of the callee whose return at i carried one */
    uint64_t *link = last + (n + 1u), *chain = link + (n + 1u);
    for (uint64_t i = 0; i < n; i++) {
        const cint_sir_event *e = &t->ring[i].e;
        top = top == 0 && d == 0 && e->kind != CINT_SIR_EV_RETURN ? i + 1u : top;
        if (e->kind == CINT_SIR_EV_CALL) {
            last[++d] = 0;
        } else if (e->kind == CINT_SIR_EV_RETURN && d > 0u) {
            link[i] = last[d--];
            last[d] = e->error != 0u ? i + 1u : last[d];
        }
    }
    for (uint64_t c = last[0]; c != 0; c = link[c - 1u]) chain[k++] = c - 1u;
    while (j < k && tried(&t->ring[chain[j]], 0, callee, sizeof callee)) {   /* passed on from the entry down */
        j++;
    }
    /* the function that made the error: the caller of the first call not tried, else the last callee */
    const wb_traced *made = j < k ? &t->ring[chain[j]] : k != 0 ? &t->ring[chain[k - 1u]] :
                            top != 0 ? &t->ring[top - 1u] : NULL;
    if (made != NULL) (void)tried(made, j == k && k != 0, name, sizeof name);
    wb_f(b, "the path, innermost first, from the calls and returns of the replay:\n  %s returned the error "
            "(tag %llu)\n", name, (unsigned long long)(j != 0 ? t->ring[chain[j - 1u]].e.error : tag));
    while (j-- > 0) {
        const wb_traced *x = &t->ring[chain[j]];
        (void)tried(x, 0, name, sizeof name);
        (void)tried(x, 1, callee, sizeof callee);
        where(user, x, at, sizeof at);
        wb_f(b, "  %s passed it on with try (tag %llu), from its call of %s at %s\n", name,
             (unsigned long long)(j != 0 ? t->ring[chain[j - 1u]].e.error : tag), callee, at);
    }
    free(last);
}

/* ------------------------------------------------------------------------- */
/* A case of a way through recordings (box 14 unit 3b; SPEC-06 9.2).         */

typedef struct case_run {
    cint_ctx *ctx;
    cint_recorder *r;
    cint_sir_program *prog;          /* the program the context is in */
    cint_sir_image *img;
    wb_case_source_fn src;
    void *user;
    cint_sink *sink;                 /* where the recorder writes the case */
    const char *why;                 /* the reason it failed */
} case_run;

static cint_status dropped(void *user, const uint8_t *bytes, size_t len)
{
    (void)user;
    (void)bytes;
    (void)len;
    return CINT_OK;
}

/* A context for the program of img whose prints go nowhere: the session showed them when they
 * were recorded. */
static cint_ctx *quiet_ctx(const cint_sir_image *img)
{
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL;
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = cint_sir_cint_program(img);
    cfg.output = dropped;
    if (cint_ctx_create(&cfg, &ctx) != CINT_OK || cint_ctx_bind_state(ctx, cint_sir_state_table(img)) != CINT_OK) {
        cint_ctx_destroy(ctx);
        return NULL;
    }
    return ctx;
}

/* Moves the context to program p of img, recorded with its code and each module's source; a
 * case records a change that keeps the revision with its code too. Returns 0, or -1. */
static int case_revision(case_run *x, cint_sir_program *p, cint_sir_image *img)
{
    uint8_t rev[32];
    uint32_t nmod, nfunc, nnode;
    cint_sir_counts(p, &nmod, &nfunc, &nnode);
    cint_code_module *c = calloc(nmod != 0 ? nmod : 1u, sizeof *c);
    int bad = c == NULL;
    cint_sir_revision(p, rev);
    for (uint32_t k = 0; !bad && k < nmod; k++) {
        cint_sir_text t;
        cint_sir_text_of(p, k, &t);
        c[k].path.bytes = t.path;
        c[k].path.len = (uint32_t)t.path_len;
        c[k].sir = t.text;
        c[k].sir_len = t.len;
        c[k].source = x->src(x->user, rev, t.path, t.path_len, &c[k].source_len);
        bad = c[k].source == NULL;
        x->why = bad ? "the sources of a revision on the way are not kept" : x->why;
    }
    bad = bad || (x->r == NULL ? cint_rec_begin(x->ctx, cint_sir_observer(img), c, nmod, CINT_RECORD_CASE, x->sink,
                                                &x->r) :
                                 cint_rec_change_revision(x->r, cint_sir_observer(img), cint_sir_state_table(img), c,
                                                          nmod)) != CINT_OK;
    free(c);
    x->prog = bad ? x->prog : p;
    x->img = bad ? x->img : img;
    return bad ? -1 : 0;
}

/* Makes entry event *e of info again through the recorder, as replay makes it, and compares its
 * status and state hash with the recorded outcome, where *e is left. Returns 1 for an entry
 * that ran (a boundary), 0 for one that did not, or -1. */
static int case_entry(case_run *x, const cint_recording_info *info, cint_recording_event *e)
{
    const cint_observer *obs = cint_sir_observer(x->img);
    const cint_program *prog = cint_sir_cint_program(x->img);
    cint_recording_event o = *e;
    uint64_t args[16], res = 0;
    uint32_t k = 0;
    uint8_t hash[32];
    int ok = e->arg_count <= 16u && (e->form == 0u || cint_sir_main(x->img) != NULL);
    for (; e->form == 0u && k < obs->entry_count; k++) {
        const cint_observer_entry *f = &obs->entries[k];
        const cint_module *m = f->module < prog->module_count ? prog->modules[f->module] : NULL;
        if (m != NULL && f->name_len == e->name.len && memcmp(f->name, e->name.bytes, e->name.len) == 0 &&
            m->path_len == e->module.len && memcmp(m->path, e->module.bytes, e->module.len) == 0) {
            break;
        }
    }
    for (uint32_t i = 0; ok && i < e->arg_count; i++) {   /* the pattern, sign-extended when signed */
        cint_tvalue v;
        uint32_t n = cint_recording_arg(info, e, i, &v) == CINT_OK && v.len >= 2u && v.len <= 9u ? v.len - 1u : 0u;
        args[i] = n < 8u && n != 0 && cint_rt_tag_signed(v.bytes[0]) && (v.bytes[n] & 0x80u) != 0 ?
                  UINT64_MAX << (8u * n) : 0;
        for (uint32_t j = 0; j < n; j++) args[i] |= (uint64_t)v.bytes[1u + j] << (8u * j);
        ok = n != 0;
    }
    while (cint_recording_next(info, &o) == CINT_OK && o.kind == CINT_EVENT_EFFECT) {
    }
    int ran = o.status == CINT_OK || o.status == CINT_FAULT;
    if (!ok || (e->form == 0u && k == obs->entry_count) || o.kind != CINT_EVENT_OUTCOME || o.status == CINT_RESOURCE) {
        x->why = o.status == CINT_RESOURCE ? "an entry on the way ran out of memory (UNREPLAYABLE)" :
                                             "an entry on the way is not in its program";
        return -1;
    }
    int64_t fuel = ran ? o.allowance : e->fuel;   /* an interrupt lowered it (box 13 default BX13-09) */
    cint_status st = e->form == 0u ? cint_rec_entry(x->r, k, args, fuel, e->depth, &res) :
                                     cint_rec_main(x->r, cint_sir_main(x->img), fuel);
    *e = o;
    if (st != o.status || (ran && (cint_ctx_state_hash(x->ctx, hash) != CINT_OK || memcmp(hash, o.hash, 32u) != 0))) {
        x->why = "an entry made again differs from the recording (SPEC-03 H-17)";
        return -1;
    }
    return ran;
}

/* The events of part p, decoded in rp, after *e: up to boundary p->to, then, when `trail`, the
 * rest. Returns 0, or -1. */
static int case_events(case_run *x, wb_replayer *rp, const wb_part *p, int trail, cint_recording_event *e)
{
    cint_sink none = {dropped, NULL};
    uint64_t at = p->from;
    int r = 0;
    while (r >= 0 && (at < p->to || trail) && cint_recording_next(&rp->info, e) == CINT_OK &&
           e->kind != CINT_EVENT_END) {
        if (e->kind == CINT_EVENT_ENTRY) {
            r = case_entry(x, &rp->info, e);
            at += r > 0 ? 1u : 0u;
        } else if (e->kind == CINT_EVENT_REVISION) {
            r = e->module_count != 0 && replay_code(rp, e, e->module_count) != 0 ? -1 :
                case_revision(x, e->module_count != 0 ? rp->prog : x->prog, e->module_count != 0 ? rp->img : x->img);
        } else {
            r = (e->kind == CINT_EVENT_CLEAR_FAULT ? cint_rec_clear_fault(x->r) :
                 e->kind == CINT_EVENT_CHECKPOINT ? cint_rec_checkpoint(x->r, &none) :
                 e->kind == CINT_EVENT_RESTORE    ? cint_rec_restore(x->r, e->data, e->data_len) : CINT_OK) == CINT_OK
                    ? 0 : -1;
        }
    }
    return r < 0 ? -1 : 0;
}

/* Joins the part decoded in rp to the way: the context moves to the part's first revision, and
 * a part that does not go on from the last state restores its initial state, unless the
 * context is in that state already. Returns 0, or -1. */
static int case_join(case_run *x, wb_replayer *rp, int cont)
{
    uint8_t was[32], to[32], now[32];
    cint_checkpoint_info k;
    memset(&k, 0, sizeof k);
    cint_sir_revision(x->prog, was);
    cint_sir_revision(rp->prog, to);
    if (memcmp(was, to, 32u) != 0 &&
        ((!cont && cint_rec_clear_fault(x->r) != CINT_OK) || case_revision(x, rp->prog, rp->img) != 0)) {
        return -1;
    }
    if (cont) return 0;
    if (cint_checkpoint_decode(rp->info.initial, rp->info.initial_len, &k) != CINT_OK ||
        cint_ctx_state_hash(x->ctx, now) != CINT_OK) {
        return -1;
    }
    cint_sha256(k.state, k.state_len, to);
    return memcmp(now, to, 32u) == 0 || cint_rec_restore(x->r, rp->info.initial, rp->info.initial_len) == CINT_OK ? 0
                                                                                                                  : -1;
}

int wb_case_write(const wb_part *parts, uint32_t n, wb_case_source_fn src, void *user, wb_buf *out, uint8_t state[32],
                  uint64_t *effects, wb_buf *b)
{
    wb_replayer *rp = calloc(n != 0 ? n : 1u, sizeof *rp);
    wb_replayer check;
    case_run x;
    cint_sink sink = {wb_sink, out};
    cint_recording_event e;
    uint8_t now[32];
    size_t had = b->n;
    int bad = rp == NULL || n == 0;
    memset(&x, 0, sizeof x);
    memset(&check, 0, sizeof check);
    memset(&e, 0, sizeof e);
    x.src = src;
    x.user = user;
    x.sink = &sink;
    out->n = 0;
    *effects = 0;
    bad = bad || wb_replay_load(&rp[0], parts[0].rec, parts[0].n, NULL, NULL) != 0 ||
          (rp[0].ctx = x.ctx = quiet_ctx(rp[0].img)) == NULL || wb_replay_run(&rp[0], parts[0].from, b) != 0 ||
          case_revision(&x, rp[0].prog, rp[0].img) != 0;   /* the recorder begins in that program */
    while (!bad && (e.next == 0 ? rp[0].rep.event != 0 : e.index + 1u < rp[0].rep.event)) {
        bad = cint_recording_next(&rp[0].info, &e) != CINT_OK;   /* to the boundary replay reached */
    }
    for (uint32_t p = 0; !bad && p < n; p++) {
        if (p != 0) {
            memset(&e, 0, sizeof e);
            bad = wb_replay_load(&rp[p], parts[p].rec, parts[p].n, NULL, NULL) != 0 ||
                  case_join(&x, &rp[p], parts[p].cont) != 0;
        }
        int trail = (p + 1u == n || parts[p + 1u].cont) && parts[p].to == rp[p].info.boundaries;
        bad = bad || case_events(&x, &rp[p], &parts[p], trail, &e) != 0;
    }
    bad = (x.r != NULL && cint_rec_end(x.r) != CINT_OK) || bad || out->failed ||
          cint_ctx_state_hash(x.ctx, state) != CINT_OK;
    if (!bad) {   /* the case replays, to the same state */
        bad = wb_replay_load(&check, out->p, out->n, NULL, NULL) != 0 || (check.ctx = quiet_ctx(check.img)) == NULL ||
              wb_replay_run(&check, UINT64_MAX, b) != 0 || cint_ctx_state_hash(check.ctx, now) != CINT_OK ||
              memcmp(now, state, 32u) != 0;
        x.why = "the case does not replay to the state it was made from";
        for (memset(&e, 0, sizeof e); !bad && cint_recording_next(&check.info, &e) == CINT_OK;)
            *effects += e.kind == CINT_EVENT_EFFECT;
    }
    if (bad && b->n == had) {
        wb_f(b, "workbench: %s; no case is written\n", x.why != NULL ? x.why : "the way to the current state cannot "
                                                                             "be read or made again");
    }
    wb_replay_free(&check);
    for (uint32_t p = 0; rp != NULL && p < n; p++) {   /* the context first, then the programs it was in */
        wb_replay_free(&rp[p]);
    }
    free(rp);
    return bad ? -1 : 0;
}
