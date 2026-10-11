/* test_density.c: recordings and replay at `dispatch` density (rt/cint_state.h; SPEC-06 1.5,
 * 10.1, 10.5; roadmap box 13, unit 7c; rt/OPEN.md RT-OQ-48), linked into test_rt. A program
 * written by hand as generated code is written, whose entries dispatch kernels that publish into
 * module state and end each dispatch with cint_rt_dispatch_end, is recorded at both densities.
 * The header's density, the `dispatch` event and the digest chain are built here from
 * rt/cint_rec.c's header comment and SPEC-06 10.5, and a state hash taken at a dispatch
 * boundary is compared with the same state reached at an entry's end. Then replay: every
 * boundary, a stop at each of them, the backends that make it diverge (no boundary, another
 * dispatch number, another state at a dispatch that the entry undoes, which only `dispatch`
 * density sees), the decoder's refusals, the text of `show`, and a deterministic fuzz target
 * whose seed and count the output names. */
#include "cint_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static long s_pass;
static long s_fail;

#define CHECK(cond, what)                                                         \
    do {                                                                          \
        if (cond) {                                                               \
            s_pass++;                                                             \
        } else {                                                                  \
            s_fail++;                                                             \
            if (s_fail <= 40) {                                                   \
                fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));  \
            }                                                                     \
        }                                                                         \
    } while (0)

/* app/field.ci: site 1 the entry's charge, 2 a print statement, 3 an addition. */
static const cint_site_info d_sites[] = {
    {0u, 0u, NULL}, {3u, 1u, "fuel.charge"}, {6u, 5u, "print"}, {9u, 9u, "add.checked.i64"}};
static const cint_module d_mod = {"app/field.ci", 12u, 4u, d_sites};
static const cint_module *const d_mods[] = {&d_mod};
static const uint8_t d_rev[32] = {0x71, 0x72, 0x73};
static const cint_program d_prog = {1u, 0u, d_mods, d_rev};
static const cint_site S_ENTRY = {0u, 1u};
static const cint_site S_PRINT = {0u, 2u};
static const cint_site S_ADD = {0u, 3u};

/* app.field.total I64 at 0, initially 0. */
static const uint8_t d_init[8] = {0, 0, 0, 0, 0, 0, 0, 0};
static const cint_state_var d_vars[] = {{"total", 5u, CINT_TAG_I64, 0u, 0u, NULL, NULL}};
static const cint_state d_state = {0u, 1u, 8u, 0u, d_init, d_vars};
static const cint_state *const d_states[] = {&d_state};
static const cint_state_table d_table = {1u, 0u, d_states, NULL};

/* A backend that differs from the recorded one: no dispatch boundary, the dispatch number plus
 * g_shift, or g_bump more in total at the first dispatch of `step`, which `step` takes back. */
static int g_skip;
static int64_t g_shift;
static int64_t g_bump;

static int64_t *total_of(cint_ctx *ctx)
{
    return (int64_t *)cint_rt_state(ctx, &d_state);
}

/* A dispatch whose kernel publishes v into total, then its boundary, as a kernel's C ends. */
static void publish(cint_ctx *ctx, int64_t v)
{
    int64_t n = cint_rt_dispatch_number(ctx);
    *total_of(ctx) = v;
    if (!g_skip) {
        cint_rt_dispatch_end(ctx, n + g_shift);
    }
}

/* I64 step(I64 n): dispatch 0 publishes total + n, total doubles, and dispatch 1, the entry's
 * last action, publishes total + 1; returns total. */
static cint_status f_step(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args, uint64_t *result)
{
    int64_t n = 0, *t;
    cint_status st = cint_rt_entry_begin(ctx, S_ENTRY, fuel, depth);
    if (st != CINT_OK) {
        return st;
    }
    memcpy(&n, &args[0], sizeof n);
    t = total_of(ctx);
    publish(ctx, *t + n + g_bump);
    *t = (*t - g_bump) * 2;
    publish(ctx, *t + 1);
    memcpy(result, t, sizeof *result);
    return cint_rt_entry_end(ctx);
}

/* I64 quiet(I64 n): dispatch 0 publishes total unchanged, then the entry prints "quiet\n". */
static cint_status f_quiet(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args, uint64_t *result)
{
    cint_status st = cint_rt_entry_begin(ctx, S_ENTRY, fuel, depth);
    if (st != CINT_OK) {
        return st;
    }
    publish(ctx, *total_of(ctx));
    (void)(cint_rt_print_begin(ctx, S_PRINT) && cint_rt_print_bytes(ctx, "quiet\n", 6u) && cint_rt_print_end(ctx));
    *result = args[0];
    return cint_rt_entry_end(ctx);
}

/* I64 crash(I64 n): dispatch 0 publishes total + n, then I64.max + n faults E_OVERFLOW. */
static cint_status f_crash(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args, uint64_t *result)
{
    int64_t n = 0, r = 0;
    cint_status st = cint_rt_entry_begin(ctx, S_ENTRY, fuel, depth);
    if (st != CINT_OK) {
        return st;
    }
    memcpy(&n, &args[0], sizeof n);
    publish(ctx, *total_of(ctx) + n);
    if (cint_add_i64(ctx, S_ADD, INT64_MAX, n, &r)) {
        memcpy(result, &r, sizeof *result);
    }
    return cint_rt_entry_end(ctx);
}

/* I64 set(I64 v): total = v, with no dispatch. */
static cint_status f_set(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args, uint64_t *result)
{
    cint_status st = cint_rt_entry_begin(ctx, S_ENTRY, fuel, depth);
    if (st != CINT_OK) {
        return st;
    }
    memcpy(total_of(ctx), &args[0], sizeof args[0]);
    *result = args[0];
    return cint_rt_entry_end(ctx);
}

static const uint32_t t_i64[] = {CINT_TAG_I64, 1u, CINT_TAG_I64};
static const cint_observer_entry d_entries[] = {{0u, 4u, "step", t_i64, f_step},
                                                {0u, 5u, "quiet", t_i64, f_quiet},
                                                {0u, 5u, "crash", t_i64, f_crash},
                                                {0u, 3u, "set", t_i64, f_set}};
static const cint_observer d_obs = {4u, 0u, &d_prog, d_entries};
#define E_STEP 0u
#define E_QUIET 1u
#define E_CRASH 2u
#define E_SET 3u

/* ------------------------------------------------------------------------- */

typedef struct bytes {
    uint8_t *b;
    size_t n, cap;
} bytes;

static cint_status b_write(void *user, const uint8_t *p, size_t n)
{
    bytes *x = (bytes *)user;
    if (n > x->cap - x->n) {
        return CINT_RESOURCE;
    }
    memcpy(x->b + x->n, p, n);
    x->n += n;
    return CINT_OK;
}

static void b_init(bytes *x, size_t cap)
{
    x->b = (uint8_t *)malloc(cap);
    x->n = 0u;
    x->cap = x->b != NULL ? cap : 0u;
}

static void b_u(bytes *x, uint64_t v, unsigned k)
{
    for (unsigned i = 0u; i < k && x->n < x->cap; i++, v >>= 8u) {
        x->b[x->n++] = (uint8_t)v;
    }
}

static void b_raw(bytes *x, const void *p, size_t n)
{
    if (n <= x->cap - x->n) {
        memcpy(x->b + x->n, p, n);
        x->n += n;
    }
}

static void b_s(bytes *x, const char *s)
{
    b_u(x, strlen(s), 4u);
    b_raw(x, s, strlen(s));
}

static cint_ctx *d_ctx(bytes *out)
{
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL;
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = &d_prog;
    if (out != NULL) {
        cfg.output = b_write;
        cfg.output_user = out;
    }
    if (cint_ctx_create(&cfg, &ctx) != CINT_OK) {
        return NULL;
    }
    if (cint_ctx_bind_state(ctx, &d_table) != CINT_OK) {
        cint_ctx_destroy(ctx);
        return NULL;
    }
    return ctx;
}

/* D_k (SPEC-06 10.5), built here from the specification's text: kind 0 at an entry's end, 1 at a
 * dispatch publication, and the dispatch number when `number` is 0 or more. */
static void chain_step(uint8_t chain[32], uint64_t index, unsigned kind, int64_t number, const uint8_t hash[32],
                       const uint8_t *fault, size_t len)
{
    bytes x;
    b_init(&x, 160u + len);
    b_s(&x, "cint-core-1/recording-chain/v1");
    b_raw(&x, chain, 32u);
    b_u(&x, index, 8u);
    b_u(&x, kind, 1u);
    b_u(&x, number >= 0 ? 1u : 0u, 1u);
    if (number >= 0) {
        b_u(&x, (uint64_t)number, 8u);
    }
    b_raw(&x, hash, 32u);
    b_u(&x, fault != NULL ? 1u : 0u, 1u);
    if (fault != NULL) {
        b_u(&x, len, 4u);
        b_raw(&x, fault, len);
    }
    cint_sha256(x.b, x.n, chain);
    free(x.b);
}

static cint_status call(cint_recorder *r, uint32_t entry, int64_t n, uint64_t *res)
{
    uint64_t a = 0u;
    memcpy(&a, &n, sizeof a);
    return cint_rec_entry(r, entry, &a, CINT_FUEL_UNBOUNDED, 8, res);
}

/* The scenario, recorded at the density of `mode`: step 5, quiet 0, crash 1 (E_OVERFLOW after its
 * dispatch), clear, step 2, end. */
static cint_status scenario(uint32_t mode, bytes *rec, bytes *out)
{
    cint_recorder *r = NULL;
    cint_sink sink = {b_write, rec};
    uint64_t res = 0u;
    cint_ctx *ctx = d_ctx(out);
    cint_status st;
    if (ctx == NULL || cint_rec_begin(ctx, &d_obs, NULL, 0u, mode, &sink, &r) != CINT_OK) {
        cint_ctx_destroy(ctx);
        return CINT_REFUSED;
    }
    CHECK(call(r, E_STEP, 5, &res) == CINT_OK && res == 11u, "step 5");
    CHECK(call(r, E_QUIET, 0, &res) == CINT_OK, "quiet");
    CHECK(call(r, E_CRASH, 1, &res) == CINT_FAULT, "crash faults after its dispatch");
    CHECK(cint_rec_clear_fault(r) == CINT_OK, "clear");
    CHECK(call(r, E_STEP, 2, &res) == CINT_OK && res == 29u, "step 2");
    st = cint_rec_end(r);
    cint_ctx_destroy(ctx);
    return st;
}

/* A boundary of a recording, as SPEC-06 1.5 and 10.5 make them. */
typedef struct bound {
    uint64_t event;   /* its event's index */
    unsigned kind;
    int64_t number;   /* -1: no dispatch */
    uint8_t hash[32];
} bound;

/* The boundaries of a decoded recording, from its events, and the chain they make. */
static unsigned boundaries_of(const cint_recording_info *info, bound out[16], uint8_t chain[32])
{
    cint_recording_event e, pend, entry;
    unsigned n = 0u;
    bool pending = false;
    memset(&e, 0, sizeof e);
    memset(&entry, 0, sizeof entry);
    memset(&pend, 0, sizeof pend);
    while (cint_recording_next(info, &e) == CINT_OK && n < 16u) {
        bool merged = pending && e.kind == CINT_EVENT_OUTCOME && e.has_hash && memcmp(e.hash, pend.hash, 32u) == 0;
        if (pending && !merged) {
            out[n] = (bound){pend.index, 1u, pend.ordinal, {0}};
            memcpy(out[n].hash, pend.hash, 32u);
            chain_step(chain, entry.index, 1u, pend.ordinal, pend.hash, NULL, 0u);
            n++;
        }
        pending = e.kind == CINT_EVENT_DISPATCH;
        if (pending) {
            pend = e;
        } else if (e.kind == CINT_EVENT_ENTRY) {
            entry = e;
        } else if (e.kind == CINT_EVENT_OUTCOME && e.has_hash) {
            out[n] = (bound){e.index, 0u, merged ? pend.ordinal : -1, {0}};
            memcpy(out[n].hash, e.hash, 32u);
            chain_step(chain, entry.index, 0u, out[n].number, e.hash, e.data, e.data_len);
            n++;
        }
    }
    return n;
}

/* Replay to `stop` (0: the end) on a fresh context, which writes the print bytes to echo if given. */
static void replay_on(const cint_recording_info *info, uint64_t stop, cint_replay_report *rep, uint8_t state[32],
                      cint_status *st, bytes *echo)
{
    cint_replay_config cfg;
    cint_ctx *ctx = d_ctx(echo);
    memset(&cfg, 0, sizeof cfg);
    memset(rep, 0, sizeof *rep);
    memset(state, 0, 32u);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.echo = echo != NULL ? 1u : 0u;
    cfg.stop = stop;
    cfg.entries = &d_obs;
    *st = ctx != NULL ? cint_replay(ctx, info, &cfg, rep) : CINT_RESOURCE;
    if (ctx != NULL && cint_ctx_state_hash(ctx, state) != CINT_OK) {
        memset(state, 0, 32u);
    }
    cint_ctx_destroy(ctx);
}

/* Where the first dispatch event lies in bytes, or 0. */
static size_t find_event(const bytes *rec, const uint8_t ev[41])
{
    for (size_t at = 1u; at + 41u <= rec->n; at++) {
        if (memcmp(rec->b + at, ev, 41u) == 0) {
            return at;
        }
    }
    return 0u;
}

/* The decoder of a recording with one byte changed at `at`. */
static cint_status decode_edit(const bytes *rec, size_t at, uint8_t v)
{
    cint_recording_info info;
    uint8_t *b = (uint8_t *)malloc(rec->n);
    cint_status st = CINT_RESOURCE;
    if (b != NULL) {
        memcpy(b, rec->b, rec->n);
        b[at] = v;
        st = cint_recording_decode(b, rec->n, &info);
        free(b);
    }
    return st;
}

/* The two densities, the layout, the boundaries and their chain, and replay. */
static void density(void)
{
    static const unsigned kinds[8] = {1u, 0u, 1u, 0u, 1u, 0u, 1u, 0u};
    static const int64_t numbers[8] = {0, 1, 0, -1, 0, -1, 0, 1};
    bytes rec, flat, out, head;
    cint_recording_info info, finfo;
    cint_recording_event e;
    cint_checkpoint_info k;
    cint_replay_report rep;
    bound b[16];
    uint8_t chain[32], state[32], set5[32], ev[41];
    unsigned n, dispatches = 0u;
    uint64_t res = 0u, five = 5u;
    size_t at, density_at;
    cint_status st = CINT_OK;
    cint_ctx *ctx;
    char text[4096];
    memset(&info, 0, sizeof info);  /* each filled through a pointer (MSVC C4701) */
    memset(&finfo, 0, sizeof finfo);
    memset(&k, 0, sizeof k);
    memset(&rep, 0, sizeof rep);
    b_init(&rec, 1u << 16);
    b_init(&flat, 1u << 16);
    b_init(&out, 4096u);
    b_init(&head, 256u);
    CHECK(scenario(CINT_RECORD_FULL | CINT_RECORD_DISPATCH, &rec, &out) == CINT_OK, "recorded at dispatch density");
    CHECK(out.n == 6u && memcmp(out.b, "quiet\n", 6u) == 0, "the print reached the host");
    CHECK(scenario(CINT_RECORD_FULL, &flat, NULL) == CINT_OK, "recorded at entry density");
    CHECK(cint_recording_decode(rec.b, rec.n, &info) == CINT_OK && info.density == 1u, "decoded, density 1");
    CHECK(cint_recording_decode(flat.b, flat.n, &finfo) == CINT_OK && finfo.density == 0u && finfo.boundaries == 4u,
          "entry density: one boundary per entry that ran");
    for (memset(&e, 0, sizeof e); cint_recording_next(&finfo, &e) == CINT_OK;) {
        dispatches += e.kind == CINT_EVENT_DISPATCH ? 1u : 0u;
    }
    CHECK(dispatches == 0u, "no dispatch event at entry density");

    /* The header up to its density byte, with no code and so no source-map digest. */
    b_s(&head, "cint-core-1/recording/v1");
    b_s(&head, "cint-core-1");
    b_s(&head, "fuel-v1");
    b_s(&head, CINT_RT_CONTRACT);
    b_raw(&head, d_rev, 32u);
    b_u(&head, 0u, 1u);  /* no source-map digest */
    b_u(&head, 0u, 1u);  /* full */
    b_u(&head, 1u, 8u);  /* the state hash at every entry */
    b_u(&head, 1u, 1u);  /* dispatch density */
    density_at = head.n - 1u;
    CHECK(rec.n > head.n && memcmp(rec.b, head.b, head.n) == 0, "the header, density 1");
    head.b[density_at] = 0u;
    CHECK(flat.n > head.n && memcmp(flat.b, head.b, head.n) == 0, "the header, density 0");

    /* Boundaries: step 5 has dispatch 0, then its end, into which dispatch 1, its last action,
     * merges; quiet's dispatch is followed by a print; crash ends in its fault; step 2 as step 5. */
    CHECK(cint_checkpoint_decode(info.initial, info.initial_len, &k) == CINT_OK, "the initial state");
    cint_sha256(k.state, k.state_len, state);
    head.n = 0u;
    b_s(&head, "cint-core-1/recording-chain/v1");
    b_raw(&head, state, 32u);
    cint_sha256(head.b, head.n, chain);
    n = boundaries_of(&info, b, chain);
    CHECK(n == 8u && info.boundaries == 8u, "eight boundaries");
    for (unsigned i = 0u; i < n && i < 8u; i++) {
        CHECK(b[i].kind == kinds[i] && b[i].number == numbers[i], "each boundary's kind and dispatch");
    }
    CHECK(memcmp(chain, info.chain, 32u) == 0, "the digest chain of SPEC-06 10.5");

    /* The dispatch event's bytes, and the state at step 5's dispatch 0: that of `set 5` run as an
     * entry on a fresh context (total 5, one entry begun, no effect). */
    ev[0] = CINT_EVENT_DISPATCH;
    memset(ev + 1, 0, 8u);
    memcpy(ev + 9, b[0].hash, 32u);
    at = find_event(&rec, ev);
    CHECK(at > 0u, "a dispatch event: kind 9, the number I64, the state hash");
    ctx = d_ctx(NULL);
    CHECK(ctx != NULL && d_entries[E_SET].call(ctx, CINT_FUEL_UNBOUNDED, 8, &five, &res) == CINT_OK &&
              cint_ctx_state_hash(ctx, set5) == CINT_OK && memcmp(set5, b[0].hash, 32u) == 0,
          "the state hash at a dispatch boundary is the canonical state's");
    cint_ctx_destroy(ctx);

    /* Replay: every boundary, then a stop at each one, which leaves its state. */
    replay_on(&info, 0u, &rep, state, &st, NULL);
    CHECK(st == CINT_OK && rep.status == CINT_OK && rep.boundaries == 8u && memcmp(rep.chain, info.chain, 32u) == 0,
          "replay compares every boundary");
    for (unsigned i = 0u; i < n && i < 8u; i++) {
        replay_on(&info, i + 1u, &rep, state, &st, NULL);
        CHECK(st == CINT_OK && rep.status == CINT_OK && rep.boundaries == i + 1u && rep.event == b[i].event + 1u,
              "a stop at each boundary");
        CHECK(memcmp(state, b[i].hash, 32u) == 0, "the context holds the state of the stop boundary");
    }
    out.n = 0u;
    replay_on(&info, 3u, &rep, state, &st, &out);
    CHECK(st == CINT_OK && rep.boundaries == 3u && out.n == 0u, "echo: nothing after a stop at quiet's dispatch");
    replay_on(&info, 4u, &rep, state, &st, &out);
    CHECK(st == CINT_OK && rep.boundaries == 4u && out.n == 6u && memcmp(out.b, "quiet\n", 6u) == 0,
          "echo: quiet's print by its end");

    /* A backend with another state at step's dispatch 0, which the entry takes back: the entries'
     * boundaries are the same, so only dispatch density sees it (SPEC-06 9.5). */
    g_bump = 3;
    replay_on(&finfo, 0u, &rep, state, &st, NULL);
    CHECK(st == CINT_OK && rep.status == CINT_OK, "entry density: the difference reconverges unseen");
    replay_on(&info, 0u, &rep, state, &st, NULL);
    CHECK(st == CINT_OK && rep.status == CINT_DIVERGED && rep.what == CINT_REPLAY_STATE && rep.event == b[0].event &&
              rep.boundaries == 0u,
          "dispatch density: diverged at the dispatch");
    g_bump = 0;
    g_skip = 1;
    replay_on(&info, 0u, &rep, state, &st, NULL);
    CHECK(st == CINT_OK && rep.status == CINT_DIVERGED && rep.what == CINT_REPLAY_DISPATCH && rep.event == b[0].event,
          "a backend without dispatch boundaries diverges");
    g_skip = 0;
    g_shift = 4;
    replay_on(&info, 0u, &rep, state, &st, NULL);
    CHECK(st == CINT_OK && rep.status == CINT_DIVERGED && rep.what == CINT_REPLAY_DISPATCH && rep.event == b[0].event,
          "another dispatch number diverges");
    g_shift = 0;
    CHECK(strcmp(cint_replay_what(CINT_REPLAY_DISPATCH), "dispatch") == 0, "what: dispatch");

    /* `show`. */
    n = (unsigned)cint_recording_render(&info, text, sizeof text - 1u);
    text[n < sizeof text ? n : sizeof text - 1u] = '\0';
    CHECK(strstr(text, "\ndensity dispatch\n") != NULL && strstr(text, " dispatch 0 state ") != NULL &&
              strstr(text, " dispatch 1 state ") != NULL,
          "show: the density and each dispatch event");
    n = (unsigned)cint_recording_render(&finfo, text, sizeof text - 1u);
    text[n < sizeof text ? n : sizeof text - 1u] = '\0';
    CHECK(strstr(text, "\ndensity entry\n") != NULL, "show: entry density");

    /* Refusals: dispatch events at entry density, a density above 1, a negative dispatch number,
     * and a dispatch event's state hash changed. */
    CHECK(decode_edit(&rec, density_at, 0u) == CINT_REFUSED, "a dispatch event at entry density");
    CHECK(decode_edit(&flat, density_at, 2u) == CINT_REFUSED, "density 2");
    CHECK(at > 0u && decode_edit(&rec, at + 8u, 0x80u) == CINT_REFUSED, "a negative dispatch number");
    CHECK(at > 0u && decode_edit(&rec, at + 9u, (uint8_t)(rec.b[at + 9u] ^ 1u)) == CINT_REFUSED,
          "a dispatch event's state hash changed");
    CHECK(decode_edit(&flat, density_at, 1u) == CINT_OK, "entry events at dispatch density: no dispatch ran");
    free(rec.b);
    free(flat.b);
    free(out.b);
    free(head.b);
}

/* rt/tests/test_rt.c: a fuzz target's input digest and receipt line (test_rt.py --fuzz-receipt). */
void test_fuzz_input(uint8_t digest[32], const uint8_t *p, size_t n);
void test_fuzz_line(const char *target, uint64_t seed, unsigned count, unsigned accepted, unsigned rejected,
                    unsigned failures, const uint8_t digest[32]);

/* Seeded mutations of the dispatch-density recording, each refused by the decoder or replayed
 * without a crash or a sanitizer report. */
static void fuzz(void)
{
    const uint64_t seed = 0x7cd15a7cu;
    const unsigned count = 2000u;
    bytes rec, copy;
    cint_recording_info info;
    cint_replay_report rep;
    uint8_t state[32];
    uint64_t x = seed;
    uint8_t digest[32] = {0};
    unsigned accepted = 0u, other = 0u;
    cint_status st = CINT_OK;
    memset(&info, 0, sizeof info);
    memset(&rep, 0, sizeof rep);
    b_init(&rec, 1u << 16);
    b_init(&copy, 1u << 16);
    if (copy.b == NULL || scenario(CINT_RECORD_DIGEST | CINT_RECORD_DISPATCH, &rec, NULL) != CINT_OK) {
        CHECK(0, "the fuzz target's recording");
        return;
    }
    for (unsigned i = 0u; i < count; i++) {
        unsigned edits;
        memcpy(copy.b, rec.b, rec.n);
        copy.n = rec.n;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        edits = 1u + (unsigned)(x % 3u);
        for (unsigned j = 0u; j < edits; j++) {
            size_t pos;
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
            pos = (size_t)((x >> 8) % copy.n);
            switch (x % 4u) {
            case 0u: copy.b[pos] ^= (uint8_t)(1u << ((x >> 40) % 8u)); break;
            case 1u: copy.b[pos] = (uint8_t)(x >> 32); break;
            case 2u: copy.b[pos] = (uint8_t)(copy.b[pos] + 1u); break;
            default: copy.n = pos > 0u ? pos : 1u; break;
            }
        }
        test_fuzz_input(digest, copy.b, copy.n);
        if (cint_recording_decode(copy.b, copy.n, &info) != CINT_OK) {
            continue;
        }
        accepted++;
        replay_on(&info, (x >> 20) % 10u, &rep, state, &st, NULL);
        other += st == CINT_OK || st == CINT_REFUSED ? 0u : 1u;
    }
    test_fuzz_line("density", seed, count, accepted, count - accepted, other, digest);
    CHECK(other == 0u, "every decoded mutation replays or is refused");
    free(rec.b);
    free(copy.b);
}

void test_density(long *pass, long *fail)
{
    s_pass = 0;
    s_fail = 0;
    density();
    fuzz();
    *pass += s_pass;
    *fail += s_fail;
}
