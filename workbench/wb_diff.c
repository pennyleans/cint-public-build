/* wb_diff.c: `cint debug diff` (box 14 unit 3b-Q; SPEC-06 9; the box 14 note, sections 7.1 and
 * 7.3; wb_debug.h).
 *
 * The case runs on the reference and on the candidate from its initial state, with its events
 * applied to both in order: each entry with its recorded arguments and budget, each restore,
 * clear, and change of revision. The two are compared at each boundary: the status and the fault
 * record, then the result, the prints, the fuel consumed, and the state hash. The recorded
 * outcomes are not compared, since the reference is a backend and not the recording. The
 * candidate then runs again to the first difference, and one that does not come back is
 * `unreproduced` (9.3 step 6); one that does is localized binding by binding, and element by
 * element within an arena or a pool, in the order of each side's CINT-CKPT-1 (step 5).
 * cpu-sir-interp runs the case's SIR text, and cpu-c17 a library that `cint build --lib` makes
 * from the case's sources (unit 5), with the revision identity of that SIR text. A change of
 * revision moves cpu-c17's state through a context of the interpreter's runtime, which holds the
 * change of SPEC-03 H-19b.
 */
#include <stdlib.h>
#include <string.h>

#include "cint_interp.h"
#include "cint_proc.h"
#include "wb_c17.h"
#include "wb_debug.h"

#if defined(_WIN32)
#define SEP '\\'
#define OS "windows"
#elif defined(__APPLE__)
#define SEP '/'
#define OS "macos"
#elif defined(__linux__)
#define SEP '/'
#define OS "linux"
#else
#define SEP '/'
#define OS "other"
#endif

#define STR_(x) #x
#define STR(x) STR_(x)
#if defined(__clang__)
#define BUILT __VERSION__              /* the compiler of this executable, which runs cpu-sir-interp */
#elif defined(__GNUC__)
#define BUILT "gcc " __VERSION__
#else
#define BUILT "msvc " STR(_MSC_FULL_VER)
#endif
#define REVS_MAX 64u    /* revisions of one case, as cint-interp replays them */

enum { CH_FAULT = 1u, CH_RESULT = 2u, CH_PRINTS = 4u, CH_FUEL = 8u, CH_STATE = 16u };

/* One backend running the case. */
typedef struct side {
    const char *id;
    int c17;                         /* 1 on cpu-c17 */
    wb_c17 *lib;                     /* cpu-c17: the library of its context's revision */
    cint_ctx *ctx;
    uint32_t rev;                    /* into diff.img */
    int64_t depth;                   /* cpu-c17: the depth limit its context was made with */
    wb_alter_fn alter;
    int rerun;
    wb_buf out, fault;               /* the entry's prints, and the fault record it left */
    cint_status st;
    uint64_t result;
    int64_t fuel;
    uint8_t hash[32], chain[32];
} side;

typedef struct diff {
    const wb_diff_opts *o;
    uint8_t *bytes;
    size_t len;
    cint_recording_info info;
    int decoded;                     /* info holds the case */
    cint_sir_program *prog[REVS_MAX];
    cint_sir_image *img[REVS_MAX];   /* each revision's code, in the order the case meets them */
    wb_c17 *lib[REVS_MAX];           /* and its library, when a cpu-c17 side needs it */
    uint32_t nrev;
    int c17;
    uint64_t compared;
    cint_recording_event entry, outcome;  /* of the last boundary */
    wb_buf why;                      /* invalid-case or refused: the reason */
} diff;

/* What the report and the receipt show beyond the outcome. */
typedef struct found {
    uint64_t boundary;               /* 0: none */
    unsigned ch;                     /* CH_* */
    const char *rerun;               /* or NULL */
    wb_buf binding, element, va, vz;
} found;

/* Writes the sources of revision event e (NULL: the first revision) under OUT/diff-src-R and
 * builds its library in OUT/diff-lib-R as d->lib[r]. Returns 0, 3 with the reason in d->why, or
 * 5 with it in err. */
static int build(diff *d, const cint_recording_event *e, uint32_t count, uint32_t r, wb_buf *err)
{
    char name[24], *src, *dir, *log, *lib = NULL;
    int status = 0, st = 0;
    (void)snprintf(name, sizeof name, "diff-src-%u", (unsigned)r);
    src = wb_join(d->o->out, name);
    (void)snprintf(name, sizeof name, "diff-lib-%u", (unsigned)r);
    dir = wb_join(d->o->out, name);
    log = wb_join(d->o->out, "diff.stderr");
    status = src == NULL || dir == NULL || log == NULL ? CINT_EXIT_ENVIRONMENT : 0;
    for (uint32_t k = 0; status == 0 && k < count; k++) {
        cint_code_module m;
        wb_buf p = {0};
        (void)cint_recording_module(&d->info, e, k, &m);
        wb_f(&p, "%.*s", (int)m.path.len, m.path.bytes);
        char *file = p.failed ? NULL : wb_join(src, p.p);
        if (m.source == NULL) {
            wb_s(&d->why, "a recording without sources, so cpu-c17 has nothing to build");
            status = CINT_EXIT_CHECK;
        } else if (file == NULL || !wb_path_ok(p.p)) {
            wb_f(&d->why, "module path %s cannot be written", p.p != NULL ? p.p : "");
            status = CINT_EXIT_CHECK;
        } else {
            char *slash = file;
            for (char *q = file; *q != '\0'; q++) {
                *q = *q == '/' ? SEP : *q;
                slash = *q == SEP ? q : slash;
            }
            *slash = '\0';
            int bad = cli_mkdirs(file) != 0;
            *slash = SEP;
            if (bad || wb_write_file(file, m.source, m.source_len) != 0) {
                wb_f(err, "cannot write the case's sources under %s", src);
                status = CINT_EXIT_ENVIRONMENT;
            } else if (k + 1u == count) {   /* the root module, last in program order */
                lib = wb_c17_build(d->o->cint, 0, src, p.p, dir, log, &st, err);
            }
        }
        free(file);
        wb_free(&p);
    }
    if (status == 0 && lib == NULL && st > 0) {   /* the sources did not build: cint said why in the log */
        size_t n = 0;
        char *said = wb_read_file(log, &n);
        if (said != NULL) (void)fwrite(said, 1, n, stderr);
        free(said);
        wb_f(&d->why, "its sources do not build on cpu-c17 (cint build --lib, exit status %d)", st);
        err->n = 0;
        status = CINT_EXIT_CHECK;
    } else if (status == 0 && (lib == NULL || (d->lib[r] = wb_c17_open(lib, err)) == NULL)) {
        status = CINT_EXIT_ENVIRONMENT;
    } else if (status == 0) {
        const uint8_t *rev = wb_c17_program(d->lib[r])->revision;
        if (rev == NULL || memcmp(rev, cint_sir_cint_program(d->img[r])->revision, 32u) != 0) {
            wb_s(&d->why, "its sources build on cpu-c17 to another revision identity than its SIR text");
            status = CINT_EXIT_CHECK;
        }
    }
    free(src);
    free(dir);
    free(log);
    free(lib);
    return status;
}

/* Loads and prepares the code of revision event e, or of the first revision when e is NULL,
 * with its library when a side runs cpu-c17. Returns 0, 3 with the reason in d->why, or 5 with
 * it in err. */
static int code_of(diff *d, const cint_recording_event *e, uint32_t count, wb_buf *err)
{
    cint_sir_text *t = calloc(count != 0 ? count : 1u, sizeof *t);
    cint_sir_error serr;
    uint8_t rev[32];
    uint32_t r = d->nrev;
    int bad = t == NULL || count == 0 || r == REVS_MAX;
    for (uint32_t k = 0; !bad && k < count; k++) {
        cint_code_module m;
        bad = cint_recording_module(&d->info, e, k, &m) != CINT_OK;
        t[k].path = m.path.bytes;
        t[k].path_len = m.path.len;
        t[k].text = m.sir;
        t[k].len = m.sir_len;
    }
    bad = bad || cint_sir_load_text(t, count, &d->prog[r], &serr) != CINT_SIR_OK;
    free(t);
    if (!bad) {
        d->nrev++;
        bad = cint_sir_prepare(d->prog[r], &d->img[r], &serr) != CINT_SIR_OK;
    }
    if (bad) {
        wb_s(&d->why, "its code does not load on cpu-sir-interp");
        return CINT_EXIT_CHECK;
    }
    cint_sir_revision(d->prog[r], rev);
    if (memcmp(rev, e != NULL ? e->revision : d->info.revision, 32u) != 0) {
        wb_s(&d->why, "its code does not have its recorded revision identity");
        return CINT_EXIT_CHECK;
    }
    return d->c17 ? build(d, e, count, r, err) : 0;
}

/* Gives side s a context of revision r with depth limit `depth` and frame-arena capacity `arena`
 * that holds the state of the CINT-CKPT-1 `from`, in place of its context. Returns 0, or -1. */
static int context(diff *d, side *s, uint32_t r, int64_t depth, int64_t arena, const wb_buf *from, wb_buf *err)
{
    cint_ctx *ctx = NULL;
    if (s->c17) {
        ctx = wb_c17_context(d->lib[r], depth, wb_sink, &s->out, err);
        if (ctx != NULL && wb_c17_restore(d->lib[r], ctx, (const uint8_t *)from->p, from->n, err) != CINT_OK) {
            wb_c17_destroy(d->lib[r], ctx);
            ctx = NULL;
        }
    } else {
        cint_ctx_config cfg;
        memset(&cfg, 0, sizeof cfg);
        cfg.size = (uint32_t)sizeof cfg;
        cfg.program = cint_sir_cint_program(d->img[r]);
        cfg.depth_limit = depth;
        cfg.frame_arena_bytes = arena;
        cfg.output = wb_sink;
        cfg.output_user = &s->out;
        if (cint_ctx_create(&cfg, &ctx) != CINT_OK || cint_ctx_bind_state(ctx, cint_sir_state_table(d->img[r])) != CINT_OK ||
            cint_ctx_restore(ctx, (const uint8_t *)from->p, from->n) != CINT_OK) {
            cint_ctx_destroy(ctx);
            ctx = NULL;
        }
    }
    if (ctx == NULL) return -1;
    if (s->c17 && s->ctx != NULL) wb_c17_destroy(s->lib, s->ctx);
    else if (!s->c17) cint_ctx_destroy(s->ctx);
    s->lib = s->c17 ? d->lib[r] : NULL;
    s->ctx = ctx;
    s->rev = r;
    s->depth = depth;
    return 0;
}

static cint_status checkpoint(side *s, wb_buf *out, wb_buf *err)
{
    cint_sink sink = {wb_sink, out};
    return s->lib != NULL ? wb_c17_checkpoint(s->lib, s->ctx, out, err) : cint_ctx_checkpoint(s->ctx, &sink);
}

/* Moves cpu-c17 side s to a context of library r with depth limit `depth`; its state changes to
 * revision r (SPEC-03 H-19b) in an interpreter context of its revision when r is another one.
 * Returns 0, or -1 with the reason in err. */
static int move(diff *d, side *s, uint32_t r, int64_t depth, wb_buf *err)
{
    wb_buf ck = {0}, moved = {0};
    side tmp;
    memset(&tmp, 0, sizeof tmp);
    int bad = checkpoint(s, &ck, err) != CINT_OK;
    if (!bad && r != s->rev) {
        bad = context(d, &tmp, s->rev, 0, 0, &ck, err) != 0 ||
              cint_ctx_change_revision(tmp.ctx, cint_sir_cint_program(d->img[r]), cint_sir_state_table(d->img[r])) !=
                  CINT_OK || checkpoint(&tmp, &moved, err) != CINT_OK;
    }
    bad = bad || context(d, s, r, depth, 0, r != s->rev ? &moved : &ck, err) != 0;
    if (bad && err->n == 0)
        wb_f(err, "the state of cpu-c17 cannot be moved to the case's revision %u or depth limit", (unsigned)r + 1u);
    cint_ctx_destroy(tmp.ctx);
    wb_free(&ck);
    wb_free(&moved);
    return bad ? -1 : 0;
}

/* Calls entry e on side s with budget `fuel`: its status, or CINT_REFUSED with the reason in
 * why when the side cannot call it. */
static cint_status call(diff *d, side *s, const cint_recording_event *e, int64_t fuel, const uint64_t *args,
                        wb_buf *why)
{
    char module[1100], name[300];
    s->out.n = 0;
    s->result = 0;
    if (e->form == 1u && s->lib == NULL) {
        cint_main_fn main_fn = cint_sir_main(d->img[s->rev]);
        return main_fn != NULL ? main_fn(s->ctx, fuel) : CINT_REFUSED;
    }
    (void)snprintf(module, sizeof module, "%.*s", (int)e->module.len, e->module.bytes);
    (void)snprintf(name, sizeof name, "%.*s", (int)e->name.len, e->name.bytes);
    if (s->lib != NULL) {
        const wb_c17_export *x = e->form == 0u ? wb_c17_find(s->lib, module, name) : NULL;
        if (x == NULL || x->why[0] != '\0') {
            wb_f(why, "%s does not call %s: %s", s->id, e->form == 0u ? name : "the program entry",
                 x != NULL ? x->why : e->form == 0u ? "the library does not export it"
                                    : "a library built with --lib has no program entry");
            return CINT_REFUSED;
        }
        return wb_c17_call(s->lib, s->ctx, x, fuel, args, &s->result);
    }
    const cint_observer *obs = cint_sir_observer(d->img[s->rev]);
    for (uint32_t i = 0; i < obs->entry_count; i++) {
        const cint_observer_entry *f = &obs->entries[i];
        const cint_module *m = obs->program->modules[f->module];
        if (f->name_len == e->name.len && memcmp(f->name, e->name.bytes, e->name.len) == 0 &&
            m->path_len == e->module.len && memcmp(m->path, e->module.bytes, e->module.len) == 0) {
            return f->call(s->ctx, fuel, e->depth, args, &s->result);
        }
    }
    wb_f(why, "its code has no function %s in %s", name, module);
    return CINT_REFUSED;
}

/* What side s shows at boundary k, after the test's change: its fault record, its fuel, and its
 * state hash, and the chain over all it showed. Returns 0, or -1 with the reason in err. */
static int observe(side *s, uint64_t k, wb_buf *err)
{
    wb_buf all = {0};
    size_t n = 0;
    s->fault.n = 0;
    if (s->alter != NULL) s->alter(k, s->rerun, s->ctx);
    int bad = s->lib != NULL ? wb_c17_fault(s->lib, s->ctx, &s->fault) < 0 || (s->fuel = wb_c17_fuel(s->lib, s->ctx)) < 0 ||
                                   wb_c17_state_hash(s->lib, s->ctx, s->hash, err) != CINT_OK
                             : cint_fault_get(s->ctx, NULL, 0u, &n) != CINT_OK ||
                                   cint_fuel_consumed(s->ctx, &s->fuel) != CINT_OK ||
                                   cint_ctx_state_hash(s->ctx, s->hash) != CINT_OK;
    if (!bad && n != 0) {
        uint8_t *p = malloc(n);
        bad = p == NULL || cint_fault_get(s->ctx, p, n, &n) != CINT_OK;
        if (!bad) wb_put(&s->fault, p, n);
        free(p);
    }
    wb_put(&all, s->chain, 32u);
    wb_f(&all, "%d %llu %lld %zu %zu ", (int)s->st, (unsigned long long)s->result, (long long)s->fuel, s->out.n,
         s->fault.n);
    wb_put(&all, s->out.p, s->out.n);
    wb_put(&all, s->fault.p, s->fault.n);
    wb_put(&all, s->hash, 32u);
    bad = bad || all.failed || s->out.failed || s->fault.failed;
    if (!bad) cint_sha256((const uint8_t *)all.p, all.n, s->chain);
    else if (err->n == 0) wb_f(err, "%s cannot be read at boundary %llu", s->id, (unsigned long long)k);
    wb_free(&all);
    return bad ? -1 : 0;
}

static int same(const wb_buf *a, const wb_buf *z)
{
    return a->n == z->n && (a->n == 0 || memcmp(a->p, z->p, a->n) == 0);
}

/* Applies the case's events to the n sides (1 or 2) from its initial state up to boundary `stop`
 * (0: all). With two, compares them at each boundary and stops at the first difference, its
 * CH_* in *ch. Returns 0; 3 with the reason in d->why, *refused set when the candidate refused;
 * or 5 with the reason in err. */
static int run_case(diff *d, side **sv, int n, uint64_t stop, unsigned *ch, int *refused, wb_buf *err)
{
    cint_recording_event e, o;
    wb_buf ck = {0};
    memset(&e, 0, sizeof e);
    while (cint_recording_next(&d->info, &e) == CINT_OK && e.kind != CINT_EVENT_ENTRY) continue;
    int64_t depth = e.kind == CINT_EVENT_ENTRY ? e.depth : 0, arena = e.kind == CINT_EVENT_ENTRY ? e.arena : 0;
    wb_put(&ck, d->info.initial, d->info.initial_len);
    for (int i = 0; i < n; i++) {   /* the contexts are made as the first entry ran */
        memset(sv[i]->chain, 0, 32u);
        if (ck.failed || context(d, sv[i], 0, depth, arena, &ck, err) != 0) {
            wb_free(&ck);
            err->n = 0;
            wb_f(&d->why, "its initial state does not restore on %s (SPEC-03 H-19a)", sv[i]->id);
            return CINT_EXIT_CHECK;
        }
    }
    wb_free(&ck);
    d->compared = 0;
    *ch = 0;
    memset(&e, 0, sizeof e);
    while ((stop == 0 || d->compared < stop) && cint_recording_next(&d->info, &e) == CINT_OK && e.kind != CINT_EVENT_END) {
        int bad = 0;
        if (e.kind == CINT_EVENT_ENTRY) {
            uint64_t args[16];
            cint_tvalue v;
            o = e;
            while (cint_recording_next(&d->info, &o) == CINT_OK && o.kind == CINT_EVENT_EFFECT) continue;
            if (o.status == CINT_RESOURCE) {   /* it depends on the machine (SPEC-03 A-6) */
                break;
            }
            bad = e.arg_count > 16u;
            for (uint32_t k = 0; k < e.arg_count && !bad; k++) {   /* each value's pattern */
                bad = cint_recording_arg(&d->info, &e, k, &v) != CINT_OK || v.len < 2u || v.len > 9u;
                uint32_t w = bad ? 0u : (uint32_t)v.len - 1u;
                args[k] = 0;
                for (uint32_t b = w; b > 0; b--) args[k] = args[k] << 8 | v.bytes[b];
                if (w != 0 && w < 8u && cint_rt_tag_signed(v.bytes[0]) && (v.bytes[w] & 0x80u) != 0u)
                    args[k] |= UINT64_MAX << (8u * w);
            }
            int64_t fuel = o.status == CINT_OK || o.status == CINT_FAULT ? o.allowance : e.fuel;
            d->entry = e;
            d->outcome = o;
            for (int i = 0; !bad && i < n; i++) {
                if (sv[i]->lib != NULL && e.depth != sv[i]->depth && move(d, sv[i], sv[i]->rev, e.depth, err) != 0)
                    return CINT_EXIT_ENVIRONMENT;
                sv[i]->st = call(d, sv[i], &e, fuel, args, &d->why);
                if (sv[i]->st != CINT_OK && sv[i]->st != CINT_FAULT) {
                    if (d->why.n == 0) wb_f(&d->why, "%s ends the entry with status %d", sv[i]->id, (int)sv[i]->st);
                    if (i == 0 && n == 2) {   /* the reference cannot run the case */
                        wb_put(err, d->why.p, d->why.n);
                        return CINT_EXIT_ENVIRONMENT;
                    }
                    *refused = sv[i]->st == CINT_REFUSED;
                    return CINT_EXIT_CHECK;
                }
            }
            d->compared += !bad;
            for (int i = 0; !bad && i < n; i++) {
                if (observe(sv[i], d->compared, err) != 0) return CINT_EXIT_ENVIRONMENT;
            }
            e = o;
            if (!bad && n == 2) {
                const side *a = sv[0], *z = sv[1];
                *ch = (a->st != z->st || !same(&a->fault, &z->fault) ? CH_FAULT : 0u) |
                      (o.has_result && a->st == CINT_OK && a->result != z->result ? CH_RESULT : 0u) |
                      (!same(&a->out, &z->out) ? CH_PRINTS : 0u) | (a->fuel != z->fuel ? CH_FUEL : 0u) |
                      (memcmp(a->hash, z->hash, 32u) != 0 ? CH_STATE : 0u);
                if (*ch != 0) return 0;
            }
        } else if (e.kind == CINT_EVENT_RESTORE || e.kind == CINT_EVENT_CLEAR_FAULT) {
            for (int i = 0; !bad && i < n; i++) {
                side *s = sv[i];
                bad = (e.kind == CINT_EVENT_CLEAR_FAULT
                           ? (s->lib != NULL ? wb_c17_clear_fault(s->lib, s->ctx) : cint_ctx_clear_fault(s->ctx))
                           : s->lib != NULL ? wb_c17_restore(s->lib, s->ctx, e.data, e.data_len, err)
                                            : cint_ctx_restore(s->ctx, e.data, e.data_len)) != CINT_OK;
            }
        } else if (e.kind == CINT_EVENT_REVISION && e.module_count != 0) {
            uint32_t r = 0;
            int st = 0;
            while (r < d->nrev && memcmp(cint_sir_cint_program(d->img[r])->revision, e.revision, 32u) != 0) r++;
            if (r == d->nrev && (st = code_of(d, &e, e.module_count, err)) != 0) return st;
            for (int i = 0; !bad && i < n; i++) {
                bad = sv[i]->lib != NULL ? move(d, sv[i], r, sv[i]->depth, err) != 0
                                         : cint_ctx_change_revision(sv[i]->ctx, cint_sir_cint_program(d->img[r]),
                                                                    cint_sir_state_table(d->img[r])) != CINT_OK;
                sv[i]->rev = r;
            }
        }
        if (bad) {
            err->n = 0;
            wb_f(&d->why, "event %llu does not apply on both backends", (unsigned long long)e.index);
            return CINT_EXIT_CHECK;
        }
    }
    return 0;
}

/* The canonical text of a tagged value, `I64 9`. */
static void value_text(wb_buf *b, const cint_tvalue *t)
{
    wb_tval v;
    char text[300];
    v.len = t->len <= sizeof v.bytes ? t->len : 0;
    memcpy(v.bytes, t->bytes, v.len);
    (void)wb_tval_text(&v, text, sizeof text, 1);
    wb_s(b, text);
}

/* The result pattern `bits` as a value of the type the case recorded for it. */
static void result_text(wb_buf *b, const cint_tvalue *recorded, uint64_t bits)
{
    cint_tvalue v = *recorded;
    for (uint32_t i = 1; i < v.len && i <= 8u; i++, bits >>= 8) v.bytes[i] = (uint8_t)(bits & 0xffu);
    value_text(b, &v);
}

/* Localizes a state difference (9.3 step 5) in the CINT-CKPT-1 of each side: the first global
 * in the checkpoints' order whose value differs, else the first arena or pool, and in it the
 * first element, with both values; an element is shown as its bytes, since the checkpoint holds
 * no type for it. */
static void localize(const wb_buf *a, const wb_buf *z, found *f)
{
    cint_checkpoint_info ia, iz;
    cint_checkpoint_global ga, gz;
    size_t ca = 0, cz = 0;
    if (cint_checkpoint_decode((const uint8_t *)a->p, a->n, &ia) != CINT_OK ||
        cint_checkpoint_decode((const uint8_t *)z->p, z->n, &iz) != CINT_OK) {
        wb_s(&f->binding, "(a checkpoint cannot be decoded)");
        return;
    }
    for (;;) {   /* the globals, in canonical order */
        int ha = cint_checkpoint_global_next(&ia, &ca, &ga) == CINT_OK, hz = cint_checkpoint_global_next(&iz, &cz, &gz) == CINT_OK;
        int named = ha && hz && ga.name.len == gz.name.len && memcmp(ga.name.bytes, gz.name.bytes, ga.name.len) == 0;
        if (!ha && !hz) break;
        if (named && ga.value.len == gz.value.len && memcmp(ga.value.bytes, gz.value.bytes, ga.value.len) == 0)
            continue;
        const cint_checkpoint_global *g = ha ? &ga : &gz;
        wb_f(&f->binding, "%.*s", (int)g->name.len, g->name.bytes);
        if (ha) value_text(&f->va, &ga.value);
        else wb_s(&f->va, "absent");
        if (named || !ha) value_text(&f->vz, &gz.value);
        else wb_s(&f->vz, "absent");
        return;
    }
    for (int pools = 0; pools < 2; pools++) {   /* the arenas, then the pools, in identifier order */
        ca = cz = 0;
        for (;;) {
            cint_checkpoint_arena aa = {0}, az = {0};
            cint_checkpoint_pool pa = {0}, pz = {0};
            int ha = pools ? cint_checkpoint_pool_next(&ia, &ca, &pa) == CINT_OK : cint_checkpoint_arena_next(&ia, &ca, &aa) == CINT_OK;
            int hz = pools ? cint_checkpoint_pool_next(&iz, &cz, &pz) == CINT_OK : cint_checkpoint_arena_next(&iz, &cz, &az) == CINT_OK;
            if (!ha && !hz) break;
            const uint8_t *ea = NULL, *ez = NULL;
            uint64_t eb = 0, ne = 0;
            int shape = !ha || !hz;
            if (!shape && pools) {
                shape = pa.id != pz.id || pa.elem_bytes != pz.elem_bytes || pa.capacity != pz.capacity || pa.used != pz.used ||
                        pa.top != pz.top || pa.occupied != pz.occupied || memcmp(pa.slots, pz.slots, (size_t)pa.used * 17u) != 0;
                ea = pa.elements, ez = pz.elements, eb = pa.elem_bytes, ne = pa.occupied;
            } else if (!shape) {
                shape = aa.id != az.id || aa.parent != az.parent || aa.elem_bytes != az.elem_bytes || aa.retired != az.retired ||
                        aa.offset != az.offset || aa.capacity != az.capacity || aa.used != az.used ||
                        aa.generation != az.generation;
                ea = aa.elements, ez = az.elements, eb = aa.elem_bytes, ne = aa.elements != NULL ? (uint64_t)aa.used : 0u;
            }
            uint64_t i = 0;
            while (!shape && i < ne && memcmp(ea + i * eb, ez + i * eb, (size_t)eb) == 0) i++;
            if (!shape && i == ne) continue;
            wb_f(&f->binding, "%s %u", pools ? "pool" : "arena", (unsigned)(ha ? (pools ? pa.id : aa.id) : (pools ? pz.id : az.id)));
            if (shape) {
                wb_s(&f->element, pools ? "its slots or counts" : "its extent, place, or generation");
                wb_s(&f->va, ha ? "present" : "absent");
                wb_s(&f->vz, hz ? "present" : "absent");
            } else {
                wb_f(&f->element, "%llu", (unsigned long long)i);
                wb_s(&f->va, "bytes ");
                wb_hex(&f->va, ea + i * eb, (size_t)eb);
                wb_s(&f->vz, "bytes ");
                wb_hex(&f->vz, ez + i * eb, (size_t)eb);
            }
            return;
        }
    }
    wb_s(&f->binding, "(none: the registry, the counters, or the effect position)");
}

/* A JSON member `,"key":"text"`, or null for an empty text. */
static void member(wb_buf *j, const char *key, const wb_buf *text)
{
    wb_f(j, ",\"%s\":", key);
    if (text->n == 0) wb_s(j, "null");
    else wb_jstr(j, text->p, text->n);
}

/* The receipt of kind `diff` (SPEC-06 12.2; box 14 default BX14-15): canonical JSON (SPEC-09
 * RCPT-02) ending in LF, whose identity names the case and both backends, and whose outcome is
 * that of 9.4 with its boundary, binding, and element. */
static void receipt(wb_buf *out, const diff *d, const char *outcome, const found *f, long long ns)
{
    static const char *const names[] = {"fault", "result", "prints", "fuel", "state"};
    wb_buf id = {0}, oc = {0}, all = {0}, file = {0};
    uint8_t h[32];
    cint_sha256(d->bytes, d->len, h);
    wb_f(&file, "{\"bytes\":\"%llu\",\"sha256\":\"", (unsigned long long)d->len);
    wb_hex(&file, h, 32u);
    wb_s(&file, "\"}");
    wb_f(&id, "{\"backend\":{\"candidate\":{\"id\":\"%s\"},\"reference\":{\"id\":\"%s\"}},", d->o->cand, d->o->ref);
    if (d->info.is_case) {   /* keys in byte order (canonical JSON) */
        wb_f(&id, "\"case\":%s,", file.p);
    }
    if (!d->decoded) {
        wb_s(&id, "\"identities\":{\"revision\":null,\"source_map\":null");
    } else {
        wb_s(&id, "\"identities\":{\"revision\":\"");
        wb_hex(&id, d->info.revision, 32u);
        wb_s(&id, d->info.has_map ? "\",\"source_map\":\"" : "\",\"source_map\":null");
    }
    if (d->decoded && d->info.has_map) {
        wb_hex(&id, d->info.map, 32u);
        wb_s(&id, "\"");
    }
    wb_s(&id, "},\"kind\":\"diff\",\"method\":{\"density\":\"entry\",\"fuel_model\":\"fuel-v1\"},\"profile\":\"cint-core-1\",");
    if (!d->info.is_case) wb_f(&id, "\"recording\":%s,", file.p);
    wb_s(&id, "\"runtime_contract\":\"" CINT_RT_CONTRACT "\",\"schema\":\"cint-receipt-1/diff\",\"tool\":{");
    for (int k = 0; k < 3; k++) {   /* SPEC-06 12.2: null when not given */
        const char *t = d->o->tool[k];
        wb_f(&id, "%s\"%s_source_identity\":%s%s%s", k ? "," : "", k == 0 ? "compiler" : k == 1 ? "interpreter" : "workbench",
             t ? "\"" : "null", t ? t : "", t ? "\"" : "");
    }
    wb_s(&id, "}}");
    wb_s(&oc, "{");
    {   /* the members in byte order, each after a comma, the first one's dropped */
        wb_buf m = {0};
        member(&m, "binding", &f->binding);
        wb_f(&m, ",\"boundaries\":\"%llu\"", (unsigned long long)d->info.boundaries);
        if (f->boundary != 0) wb_f(&m, ",\"boundary\":\"%llu\"", (unsigned long long)f->boundary);
        else wb_s(&m, ",\"boundary\":null");
        member(&m, "candidate", &f->vz);
        wb_f(&m, ",\"compared\":\"%llu\",\"differs\":[", (unsigned long long)d->compared);
        for (unsigned i = 0, first = 1; i < 5u; i++) {
            if ((f->ch & (1u << i)) != 0) {
                wb_f(&m, "%s\"%s\"", first ? "" : ",", names[i]);
                first = 0;
            }
        }
        wb_s(&m, "]");
        member(&m, "element", &f->element);
        wb_f(&m, ",\"outcome\":\"%s\"", outcome);
        member(&m, "reference", &f->va);
        if (f->rerun != NULL) wb_f(&m, ",\"rerun\":\"%s\"", f->rerun);
        else wb_s(&m, ",\"rerun\":null");
        member(&m, "why", &d->why);
        wb_put(&oc, m.p != NULL ? m.p + 1 : "", m.n != 0 ? m.n - 1u : 0u);
        oc.failed |= m.failed;
        wb_free(&m);
    }
    wb_s(&oc, "}");
    wb_f(&all, "{\"identity\":%s,\"outcome\":%s}", id.p != NULL ? id.p : "", oc.p != NULL ? oc.p : "");
    if (all.p != NULL) cint_sha256((const uint8_t *)all.p, all.n, h);
    wb_f(out, "{\"identity\":%s,\"observations\":{\"backend\":{", id.p != NULL ? id.p : "");
    for (int k = 0; k < 2; k++) {   /* each side's build configuration, outside the identity (SPEC-06 12.2) */
        const char *b = k == 0 ? d->o->cand : d->o->ref;
        wb_f(out, "\"%s\":", k == 0 ? "candidate" : "reference");
        if (strcmp(b, "cpu-c17") == 0) wb_c17_config(d->o->cint, out);
        else wb_s(out, "{\"compiler\":\"" BUILT "\"}");
        wb_s(out, k == 0 ? "," : "},");
    }
    wb_s(out, "\"limits\":\"The executables' digests are not recorded yet (RT-OQ-44); a tool's source identity is "
              "null when not given.\",\"machine\":{\"os\":\"" OS "\"},\"receipt_identity_sha256\":\"");
    wb_hex(out, h, 32u);
    wb_f(out, "\",\"timings\":{\"diff_ns\":\"%llu\"}},\"outcome\":%s}\n", (unsigned long long)ns, oc.p != NULL ? oc.p : "");
    out->failed |= id.failed | oc.failed | all.failed | file.failed;
    wb_free(&id);
    wb_free(&oc);
    wb_free(&all);
    wb_free(&file);
}

/* The source of a module path for a fault report's excerpt, from the case's first revision. */
static const uint8_t *source_of(void *user, const char *path, size_t len, size_t *n)
{
    const diff *d = user;
    cint_code_module m;
    for (uint32_t k = 0; k < d->info.module_count; k++) {
        if (cint_recording_module(&d->info, NULL, k, &m) == CINT_OK && m.path.len == len &&
            memcmp(m.path.bytes, path, len) == 0) {
            *n = m.source_len;
            return m.source;
        }
    }
    return NULL;
}

/* A side's name in the report: its backend, or its role when both sides run one backend. */
static const char *label(const diff *d, int i)
{
    if (strcmp(d->o->ref, d->o->cand) == 0) return i == 0 ? "reference" : "candidate";
    return i == 0 ? d->o->ref : d->o->cand;
}

/* Two lines, one per side: `    cpu-c17          I64 9`. */
static void sides(wb_buf *b, const char *title, const diff *d, const wb_buf *va, const wb_buf *vz)
{
    wb_f(b, "  %s\n    %-16s %.*s\n    %-16s %.*s\n", title, label(d, 0), (int)va->n, va->p, label(d, 1), (int)vz->n,
         vz->p);
}

/* The text report of SPEC-06 9.6. */
static void report(wb_buf *b, diff *d, const char *outcome, const found *f, side *const sv[2])
{
    const char *slash = strrchr(d->o->path, SEP);
    wb_buf x = {0}, y = {0};
    wb_f(b, "case       %s   %llu %s   density entry\nreference  %s\ncandidate  %s\n",
         slash != NULL ? slash + 1 : d->o->path, (unsigned long long)d->info.boundaries,
         d->info.boundaries == 1u ? "boundary" : "boundaries", d->o->ref, d->o->cand);
    wb_f(b, "boundaries %llu of %llu compared\noutcome    %s%s%.*s\n", (unsigned long long)d->compared,
         (unsigned long long)d->info.boundaries, outcome, d->why.n != 0 ? ": " : "", (int)d->why.n, d->why.p);
    if (f->boundary == 0) return;
    wb_entry_text(&x, &d->info, &d->entry);
    wb_f(b, "  first differing boundary   %llu, %.*s\n", (unsigned long long)f->boundary, (int)x.n, x.p);
    if (strcmp(outcome, "fault-difference") == 0) {
        for (int i = 0; i < 2; i++) {
            wb_fault flt;
            const side *s = sv[i];
            wb_f(b, "  %s\n", label(d, i));
            if (s->fault.n == 0 || wb_fault_decode((const uint8_t *)s->fault.p, s->fault.n, &flt, WB_RECORD_RUN) != 0) {
                wb_s(b, s->st == CINT_FAULT ? "    a fault whose record cannot be decoded\n" : "    no fault\n");
            } else {
                char position[48];
                (void)snprintf(position, sizeof position, "boundary %llu", (unsigned long long)f->boundary);
                wb_report r = {s->fuel, "the post-fault state of SPEC-01 IM-121", NULL, position, s->id, 0u,
                               {0, 0, 0, 0}};
                wb_fault_text(b, &flt, &r, source_of, d);
            }
        }
    } else if (strcmp(outcome, "value-difference") == 0) {
        if ((f->ch & CH_STATE) != 0) {
            wb_f(b, "  binding                    %.*s\n", (int)f->binding.n, f->binding.p);
            if (f->element.n != 0) wb_f(b, "  first element              %.*s\n", (int)f->element.n, f->element.p);
            if (f->va.n != 0) sides(b, "values", d, &f->va, &f->vz);
        }
        if ((f->ch & CH_RESULT) != 0) {
            x.n = y.n = 0;
            result_text(&x, &d->outcome.result, sv[0]->result);
            result_text(&y, &d->outcome.result, sv[1]->result);
            sides(b, "result", d, &x, &y);
        }
        if ((f->ch & CH_PRINTS) != 0) {
            x.n = y.n = 0;
            wb_jstr(&x, sv[0]->out.p != NULL ? sv[0]->out.p : "", sv[0]->out.n);
            wb_jstr(&y, sv[1]->out.p != NULL ? sv[1]->out.p : "", sv[1]->out.n);
            sides(b, "prints", d, &x, &y);
        }
        if ((f->ch & CH_FUEL) != 0) {
            x.n = y.n = 0;
            wb_f(&x, "%lld", (long long)sv[0]->fuel);
            wb_f(&y, "%lld", (long long)sv[1]->fuel);
            sides(b, "fuel consumed", d, &x, &y);
        }
    }
    wb_f(b, "  candidate rerun            %s\n", f->rerun != NULL && strcmp(f->rerun, "reproduced") == 0
                                                     ? "difference reproduced" : "the difference did not come back");
    wb_free(&x);
    wb_free(&y);
}

int wb_diff(const wb_diff_opts *o, wb_buf *err)
{
    diff d;
    found f;
    side ref, cand, again;
    side *both[2] = {&ref, &cand}, *one[1] = {&again}, *all[3] = {&ref, &cand, &again};
    wb_buf b = {0}, ca = {0}, cz = {0};
    const char *outcome = "agree";
    int status = 0, refused = 0;
    long long t0 = wb_now_ns();
    memset(&d, 0, sizeof d);
    memset(&f, 0, sizeof f);
    memset(&ref, 0, sizeof ref);
    memset(&cand, 0, sizeof cand);
    memset(&again, 0, sizeof again);
    d.o = o;
    ref.id = o->ref;
    cand.id = again.id = o->cand;
    cand.alter = again.alter = o->alter;
    again.rerun = 1;
    ref.c17 = strcmp(o->ref, "cpu-c17") == 0;
    cand.c17 = again.c17 = strcmp(o->cand, "cpu-c17") == 0;
    d.c17 = ref.c17 || cand.c17;
    if ((d.bytes = (uint8_t *)wb_read_file(o->path, &d.len)) == NULL) {
        wb_f(err, "cannot read %s", o->path);
        return CINT_EXIT_ENVIRONMENT;
    }
    /* 9.3 steps 1 and 2: the case, its code, and the library of its sources for cpu-c17 */
    if (cint_recording_decode(d.bytes, d.len, &d.info) != CINT_OK) {
        memset(&d.info, 0, sizeof d.info);
        wb_s(&d.why, "not a complete case or recording");
        status = CINT_EXIT_CHECK;
    } else {
        d.decoded = 1;
    }
    if (d.decoded && d.c17) {   /* a library has no program entry, so cpu-c17 calls none (SPEC-06 3.1) */
        cint_recording_event e;
        memset(&e, 0, sizeof e);
        while (status == 0 && cint_recording_next(&d.info, &e) == CINT_OK) {
            if (e.kind == CINT_EVENT_ENTRY && e.form == 1u) {
                wb_s(err, "the case calls the program entry, which a cpu-c17 library does not have; compare it "
                          "with --ref cpu-sir-interp");
                status = CINT_EXIT_ENVIRONMENT;
            }
        }
    }
    status = status != 0 ? status : code_of(&d, NULL, d.info.module_count, err);
    /* steps 3 and 4: both sides from the initial state, compared at each boundary */
    status = status != 0 ? status : run_case(&d, both, 2, 0, &f.ch, &refused, err);
    if (status == CINT_EXIT_CHECK) {
        outcome = refused ? "refused" : "invalid-case";
    } else if (status == 0 && f.ch != 0) {
        /* step 6: the candidate again to that boundary; step 5 when the difference came back */
        uint64_t k = d.compared;
        uint8_t chain[32];
        unsigned ch = 0;
        memcpy(chain, cand.chain, 32u);
        int st = run_case(&d, one, 1, k, &ch, &refused, err), back = st == 0 && d.compared == k &&
                                                                    memcmp(again.chain, chain, 32u) == 0;
        if (st == CINT_EXIT_ENVIRONMENT) status = st;
        d.why.n = 0;
        d.compared = k;
        f.boundary = k;
        f.rerun = back ? "reproduced" : "not reproduced";
        outcome = !back ? "unreproduced" : (f.ch & CH_FAULT) != 0 ? "fault-difference" : "value-difference";
        if (back && (f.ch & (CH_FAULT | CH_STATE)) == CH_STATE && checkpoint(&ref, &ca, err) == CINT_OK &&
            checkpoint(&cand, &cz, err) == CINT_OK) {
            localize(&ca, &cz, &f);
        }
        if (!back) f.ch = 0;
        status = status != 0 ? status : CINT_EXIT_CHECK;
    }
    if (status == 0 || status == CINT_EXIT_CHECK) {   /* the report: the receipt under --json */
        if (o->json) receipt(&b, &d, outcome, &f, wb_elapsed_ns(t0, wb_now_ns()));
        else report(&b, &d, outcome, &f, both);
        if (b.failed || fwrite(b.p, 1, b.n, stdout) != b.n) {
            wb_s(err, "cannot write the report");
            status = CINT_EXIT_ENVIRONMENT;
        }
        b.n = 0;
        if (o->receipt != NULL && status != CINT_EXIT_ENVIRONMENT) {
            receipt(&b, &d, outcome, &f, wb_elapsed_ns(t0, wb_now_ns()));
            if (b.failed || wb_write_file(o->receipt, b.p, b.n) != 0) {
                wb_f(err, "cannot write the receipt %s", o->receipt);
                status = CINT_EXIT_ENVIRONMENT;
            }
        }
    }
    for (int i = 0; i < 3; i++) {
        if (all[i]->c17 && all[i]->ctx != NULL) wb_c17_destroy(all[i]->lib, all[i]->ctx);
        else cint_ctx_destroy(all[i]->ctx);
        wb_free(&all[i]->out);
        wb_free(&all[i]->fault);
    }
    for (uint32_t r = 0; r < d.nrev; r++) {
        if (d.lib[r] != NULL) wb_c17_close(d.lib[r]);
        cint_sir_release(d.img[r]);
        cint_sir_free(d.prog[r]);
    }
    wb_buf *bufs[] = {&b, &ca, &cz, &d.why, &f.binding, &f.element, &f.va, &f.vz};
    for (size_t i = 0; i < sizeof bufs / sizeof bufs[0]; i++) wb_free(bufs[i]);
    free(d.bytes);
    return status;
}
