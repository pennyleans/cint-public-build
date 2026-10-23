/* test_record.c: recordings and replay (rt/cint_state.h; roadmap box 13, unit 5), linked into
 * test_rt. A program written by hand as generated code is written, with three exported
 * functions and a program entry, is recorded through a recorder; the recording's header, code,
 * initial checkpoint and first events are built here byte by byte from the header comments of
 * rt/cint_rec.c and rt/cint_state.c, and its digest chain from SPEC-06 10.5. Then replay
 * (SPEC-03 H-16, H-17): every boundary, the stop boundary, the interrupt's allowance, a change
 * of revision, the comparisons that make it diverge, CINT_RESOURCE, the text of `show`, and a
 * deterministic fuzz target over the decoder and replay whose seed and count the output names. */
#include "cint_fixed.h"
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

/* app/ledger.ci: site 1 the entry's charge, 2 a print statement, 3 a charge in a loop. */
static const cint_site_info r_sites[] = {
    {0u, 0u, NULL}, {4u, 1u, "fuel.charge"}, {5u, 5u, "print"}, {9u, 9u, "fuel.charge"}};
static const cint_module r_mod = {"app/ledger.ci", 13u, 4u, r_sites};
static const cint_module *const r_mods[] = {&r_mod};
static const uint8_t r_rev[32] = {0x51, 0x52, 0x53};
static const uint8_t r_rev2[32] = {0x61, 0x62, 0x63};
static const cint_program r_prog = {1u, 0u, r_mods, r_rev};
static const cint_program r_prog2 = {1u, 0u, r_mods, r_rev2};
static const cint_site S_ENTRY = {0u, 1u};
static const cint_site S_PRINT = {0u, 2u};
static const cint_site S_LOOP = {0u, 3u};

/* app.ledger.total I64 at 0, initially 100. */
static const uint8_t r_init[8] = {100, 0, 0, 0, 0, 0, 0, 0};
static const cint_state_var r_vars[] = {{"total", 5u, CINT_TAG_I64, 0u, 0u, NULL, NULL}};
static const cint_state r_state = {0u, 1u, 8u, 0u, r_init, r_vars};
static const cint_state *const r_states[] = {&r_state};
static const cint_state_table r_table = {1u, 0u, r_states, NULL};

static const char *const r_err_values[] = {"Low", "Closed"};
static const cint_error_set r_errs = {"app.ledger.LedgerError", CINT_TAG_U8, 2u, r_err_values};

static int64_t g_step = 1;           /* what deposit adds per unit: 2 makes a different backend */
static const char *g_word = "deposit";
static int64_t g_interrupt_at = -1;  /* burn interrupts itself after this many charges */
static int g_resource;               /* deposit returns CINT_RESOURCE before it runs */

static int64_t *total_of(cint_ctx *ctx)
{
    return (int64_t *)cint_rt_state(ctx, &r_state);
}

/* I64 deposit(I64 n): prints "deposit <n>\n", adds n to total, returns total. */
static cint_status f_deposit(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args, uint64_t *result)
{
    char text[48];
    int64_t n, *total;
    int len;
    cint_status st;
    if (g_resource) {
        return CINT_RESOURCE;
    }
    if ((st = cint_rt_entry_begin(ctx, S_ENTRY, fuel, depth)) != CINT_OK) {
        return st;
    }
    memcpy(&n, &args[0], sizeof n);
    len = snprintf(text, sizeof text, "%s %lld\n", g_word, (long long)n);
    if ((total = total_of(ctx)) != NULL && cint_rt_print_begin(ctx, S_PRINT) &&
        cint_rt_print_bytes(ctx, text, (size_t)len) && cint_rt_print_end(ctx)) {
        *total += n * g_step;
        memcpy(result, total, sizeof *result);
    }
    return cint_rt_entry_end(ctx);
}

/* I64 burn(I64 n): charges 1 unit n times, at most 4096 (the fuzz target's bound), polling the
 * interrupt; returns n. */
static cint_status f_burn(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args, uint64_t *result)
{
    cint_status st = cint_rt_entry_begin(ctx, S_ENTRY, fuel, depth);
    if (st != CINT_OK) {
        return st;
    }
    for (uint64_t i = 0u; i < args[0] && i < 4096u; i++) {
        cint_rt_interrupt_poll(ctx);
        if (!cint_fuel_charge(ctx, S_LOOP, 1u)) {
            break;
        }
        if ((int64_t)i == g_interrupt_at) {
            cint_ctx_interrupt(ctx);
        }
    }
    *result = args[0];
    return cint_rt_entry_end(ctx);
}

/* Bool close(U8 code): an error result LedgerError.Closed when code is 2, else true. */
static cint_status f_close(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args, uint64_t *result)
{
    cint_status st = cint_rt_entry_begin(ctx, S_ENTRY, fuel, depth);
    if (st != CINT_OK) {
        return st;
    }
    if (args[0] == 2u) {
        cint_rt_error_result(ctx, &r_errs, 2u);
    } else {
        *result = 1u;
    }
    return cint_rt_entry_end(ctx);
}

/* The program entry of a `main(in U8[_] args)`: prints the program arguments' bytes (OQ-306). */
static cint_status f_args_main(cint_ctx *ctx, int64_t fuel)
{
    size_t n = 0u;
    const uint8_t *a;
    cint_status st = cint_rt_entry_begin(ctx, S_ENTRY, fuel, CINT_DEPTH_FROM_CONFIG);
    if (st != CINT_OK) {
        return st;
    }
    a = cint_ctx_args(ctx, &n);
    (void)(cint_rt_print_begin(ctx, S_PRINT) && cint_rt_print_bytes(ctx, (const char *)a, n) && cint_rt_print_end(ctx));
    return cint_rt_entry_end(ctx);
}

/* The program entry: prints "main\n". */
static cint_status f_main(cint_ctx *ctx, int64_t fuel)
{
    cint_status st = cint_rt_entry_begin(ctx, S_ENTRY, fuel, CINT_DEPTH_FROM_CONFIG);
    if (st != CINT_OK) {
        return st;
    }
    (void)(cint_rt_print_begin(ctx, S_PRINT) && cint_rt_print_bytes(ctx, "main\n", 5u) && cint_rt_print_end(ctx));
    return cint_rt_entry_end(ctx);
}

/* The program entry that faults: Q80.48.max + 1.0 overflows (E_OVERFLOW add.checked.q80_48), and the
 * record's operands are fixed point of storage I128 (OQ-292), storage tag 15. */
static cint_status f_qfault(cint_ctx *ctx, int64_t fuel)
{
    static const cint_i128 a = {{UINT64_C(0xffffffffffffffff), UINT64_C(0x7fffffffffffffff)}};
    static const cint_i128 b = {{UINT64_C(0x0001000000000000), UINT64_C(0)}};
    cint_i128 out = {{0u, 0u}};
    cint_status st = cint_rt_entry_begin(ctx, S_ENTRY, fuel, CINT_DEPTH_FROM_CONFIG);
    if (st != CINT_OK) {
        return st;
    }
    (void)cint_rt_q_limbs(ctx, S_LOOP, 1u, 0u, 0u, CINT_QT(CINT_TAG_I128, 48u), CINT_QT(CINT_TAG_I128, 48u),
                          CINT_QT(CINT_TAG_I128, 48u), &a, &b, &out);
    return cint_rt_entry_end(ctx);
}

static const uint32_t t_deposit[] = {CINT_TAG_I64, 1u, CINT_TAG_I64};
static const uint32_t t_burn[] = {CINT_TAG_I64, 1u, CINT_TAG_I64};
static const uint32_t t_close[] = {CINT_TAG_BOOL, 1u, CINT_TAG_U8};
static const cint_observer_entry r_entries[] = {
    {0u, 7u, "deposit", t_deposit, f_deposit}, {0u, 4u, "burn", t_burn, f_burn}, {0u, 5u, "close", t_close, f_close}};
static const cint_observer r_obs = {3u, 0u, &r_prog, r_entries};
static const cint_observer r_obs2 = {3u, 0u, &r_prog2, r_entries};
static const cint_observer r_obs_burn_only = {1u, 0u, &r_prog, r_entries + 1};
#define E_DEPOSIT 0u
#define E_BURN 1u
#define E_CLOSE 2u

static const char r_sir[] = "module app.ledger\nsites app/ledger.ci\n";
static const char r_sir2[] = "module app.ledger\nsites app/ledger.ci 1:4:1:fuel.charge\n";
static const char r_source[] = "I64 total = 100;\n";

/* ------------------------------------------------------------------------- */

typedef struct bytes {
    uint8_t *b;
    size_t n, cap;
    int fail;  /* the sink refuses writes after this many bytes, when not 0 */
} bytes;

static cint_status b_write(void *user, const uint8_t *p, size_t n)
{
    bytes *x = (bytes *)user;
    if ((x->fail != 0 && x->n + n > (size_t)x->fail) || n > x->cap - x->n) {
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
    x->fail = 0;
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

/* One module's part of the source map of rt/cint_rec.c's header comment, written here from
 * that text: the path, the count of sites, and each site's ordinal, line, column and operation. */
static void map_one(bytes *x, const char *path, uint32_t n, const uint32_t (*site)[3], const char *const *op)
{
    b_s(x, path);
    b_u(x, n, 4u);
    for (uint32_t i = 0u; i < n; i++) {
        b_u(x, site[i][0], 4u);
        b_u(x, site[i][1], 4u);
        b_u(x, site[i][2], 4u);
        b_s(x, op[i]);
    }
}

/* The source-map digest of app/ledger.ci with r_sir (no sites) or r_sir2 (one). */
static void ledger_map(int second, uint8_t out[32])
{
    static const uint32_t site[1][3] = {{1u, 4u, 1u}};
    static const char *const op[1] = {"fuel.charge"};
    uint8_t b[128];
    bytes x = {b, 0u, sizeof b, 0};
    b_s(&x, "cint-core-1/source-map/v1");
    b_u(&x, 1u, 4u);
    map_one(&x, "app/ledger.ci", second ? 1u : 0u, site, op);
    cint_sha256(x.b, x.n, out);
}

static cint_status out_write(void *user, const uint8_t *p, size_t n)
{
    return b_write(user, p, n);
}

static cint_ctx *r_ctx_args(bytes *out, int64_t arena, const uint8_t *args, size_t args_length)
{
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL;
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = &r_prog;
    cfg.frame_arena_bytes = arena;
    cfg.args = args;
    cfg.args_length = args_length;
    if (out != NULL) {
        cfg.output = out_write;
        cfg.output_user = out;
    }
    if (cint_ctx_create(&cfg, &ctx) != CINT_OK) {
        return NULL;
    }
    if (cint_ctx_bind_state(ctx, &r_table) != CINT_OK) {
        cint_ctx_destroy(ctx);
        return NULL;
    }
    return ctx;
}

static cint_ctx *r_ctx(bytes *out, int64_t arena)
{
    return r_ctx_args(out, arena, NULL, 0u);
}

static void hash_of(cint_ctx *ctx, uint8_t out[32])
{
    if (cint_ctx_state_hash(ctx, out) != CINT_OK) {
        memset(out, 0, 32u);
    }
}

static cint_status sink_of(bytes *x, cint_sink *s)
{
    s->write = b_write;
    s->user = x;
    return CINT_OK;
}

/* D_k (SPEC-06 10.5), built here from the specification's text. */
static void chain_step(uint8_t chain[32], uint64_t index, const uint8_t hash[32], const uint8_t *fault, size_t len)
{
    bytes x;
    b_init(&x, 128u + len);
    b_s(&x, "cint-core-1/recording-chain/v1");
    b_raw(&x, chain, 32u);
    b_u(&x, index, 8u);
    b_u(&x, 0u, 2u);
    b_raw(&x, hash, 32u);
    b_u(&x, fault != NULL ? 1u : 0u, 1u);
    if (fault != NULL) {
        b_u(&x, len, 4u);
        b_raw(&x, fault, len);
    }
    cint_sha256(x.b, x.n, chain);
    free(x.b);
}

static uint64_t u64_of(int64_t v)
{
    uint64_t u;
    memcpy(&u, &v, sizeof u);
    return u;
}

/* The scenario. On a context with output `out`: begin, deposit 5, checkpoint, deposit 7,
 * burn 3 with fuel 2 (E_FUEL), clear, restore, close 2 (an error result), close 1, the program
 * entry, burn 10 interrupted after 3 charges, end. Fills hashes[k] with the state hash after
 * boundary k (from 1) and ckpt with the checkpoint. */
static cint_status scenario(cint_ctx *ctx, bytes *rec, bytes *ckpt, uint8_t hashes[9][32], uint8_t fault[512],
                            size_t *fault_len)
{
    static const cint_code_module code = {{"app/ledger.ci", 13u, 0u}, (const uint8_t *)r_sir, sizeof r_sir - 1u,
                                          (const uint8_t *)r_source, sizeof r_source - 1u};
    cint_recorder *r = NULL;
    cint_sink sink, cs;
    uint64_t res = 0u, a;
    int k = 1;
    sink_of(rec, &sink);
    sink_of(ckpt, &cs);
    if (cint_rec_begin(ctx, &r_obs, &code, 1u, CINT_RECORD_FULL, &sink, &r) != CINT_OK) {
        return CINT_REFUSED;
    }
    a = 5u;
    CHECK(cint_rec_entry(r, E_DEPOSIT, &a, CINT_FUEL_UNBOUNDED, 8, &res) == CINT_OK && res == 105u, "deposit 5");
    hash_of(ctx, hashes[k++]);
    CHECK(cint_rec_checkpoint(r, &cs) == CINT_OK, "checkpoint");
    a = 7u;
    CHECK(cint_rec_entry(r, E_DEPOSIT, &a, 50, CINT_DEPTH_FROM_CONFIG, &res) == CINT_OK && res == 112u, "deposit 7");
    hash_of(ctx, hashes[k++]);
    a = 3u;
    CHECK(cint_rec_entry(r, E_BURN, &a, 2, 8, &res) == CINT_FAULT, "burn 3 with fuel 2 faults");
    hash_of(ctx, hashes[k++]);
    CHECK(cint_fault_get(ctx, fault, 512u, fault_len) == CINT_OK, "its record");
    CHECK(cint_rec_entry(r, E_DEPOSIT, &a, 50, 8, &res) == CINT_FAULTED, "a Faulted context runs no entry");
    CHECK(cint_rec_clear_fault(r) == CINT_OK && cint_rec_clear_fault(r) == CINT_OK, "clear, twice");
    CHECK(cint_rec_restore(r, ckpt->b, ckpt->n) == CINT_OK, "restore");
    a = 2u;
    CHECK(cint_rec_entry(r, E_CLOSE, &a, 50, 8, &res) == CINT_OK, "close 2: an error result");
    hash_of(ctx, hashes[k++]);
    a = 1u;
    CHECK(cint_rec_entry(r, E_CLOSE, &a, 50, 8, &res) == CINT_OK && res == 1u, "close 1");
    hash_of(ctx, hashes[k++]);
    CHECK(cint_rec_main(r, f_main, 50) == CINT_OK, "the program entry");
    hash_of(ctx, hashes[k++]);
    a = 10u;
    g_interrupt_at = 2;
    CHECK(cint_rec_entry(r, E_BURN, &a, CINT_FUEL_UNBOUNDED, 8, &res) == CINT_FAULT, "burn 10, interrupted");
    g_interrupt_at = -1;
    hash_of(ctx, hashes[k++]);
    return cint_rec_end(r);
}

/* The layout, the decoder, and replay of every boundary. */
static void record_and_replay(void)
{
    static uint8_t hashes[9][32], fault[512];
    static const char want_text[] =
        "event 0 entry 1 app/ledger.ci deposit (I64 5) fuel -1 depth 8 frame-arena 4096\n"
        "event 1 effect 0 site 0.2 print status 0 \"deposit 5\\x0a\"\n";
    bytes rec, ckpt, out, want, init, out2;
    cint_recording_info info;
    cint_recording_event e;
    cint_replay_config cfg;
    cint_replay_report rep;
    cint_code_module m;
    cint_tvalue v;
    cint_ctx *ctx, *fresh, *other;
    uint8_t h[32], chain[32];
    size_t fault_len = 0u, n;
    char *text;
    b_init(&rec, 1u << 16);
    b_init(&ckpt, 4096u);
    b_init(&out, 4096u);
    b_init(&out2, 4096u);
    b_init(&want, 4096u);
    b_init(&init, 4096u);
    ctx = r_ctx(&out, 4096);
    if (ctx == NULL || rec.b == NULL || want.b == NULL || init.b == NULL) {
        CHECK(0, "a context and buffers");
        return;
    }
    {
        cint_sink s;
        sink_of(&init, &s);
        CHECK(cint_ctx_checkpoint(ctx, &s) == CINT_OK, "the initial checkpoint");
    }
    hash_of(ctx, hashes[0]);
    CHECK(scenario(ctx, &rec, &ckpt, hashes, fault, &fault_len) == CINT_OK, "the scenario is recorded");
    CHECK(out.n == 25u && memcmp(out.b, "deposit 5\ndeposit 7\nmain\n", 25u) == 0, "print statements reach the host's output");

    /* The bytes up to the first effect. */
    b_s(&want, "cint-core-1/recording/v1");
    b_s(&want, "cint-core-1");
    b_s(&want, "fuel-v1");
    b_s(&want, CINT_RT_CONTRACT);
    b_raw(&want, r_rev, 32u);
    b_u(&want, 1u, 1u);
    ledger_map(0, h);
    b_raw(&want, h, 32u);
    b_u(&want, 0u, 1u);
    b_u(&want, 1u, 8u);
    b_u(&want, 0u, 1u);
    b_u(&want, 1u, 4u);
    b_s(&want, "app/ledger.ci");
    b_u(&want, sizeof r_sir - 1u, 8u);
    b_raw(&want, r_sir, sizeof r_sir - 1u);
    b_u(&want, 1u, 1u);
    b_u(&want, sizeof r_source - 1u, 8u);
    b_raw(&want, r_source, sizeof r_source - 1u);
    b_u(&want, init.n, 8u);
    b_raw(&want, init.b, init.n);
    b_u(&want, CINT_EVENT_ENTRY, 1u);
    b_u(&want, 1u, 8u);
    b_u(&want, 0u, 1u);
    b_s(&want, "app/ledger.ci");
    b_s(&want, "deposit");
    b_u(&want, 1u, 4u);
    b_u(&want, CINT_TAG_I64, 1u);
    b_u(&want, 5u, 8u);
    b_u(&want, u64_of(CINT_FUEL_UNBOUNDED), 8u);
    b_u(&want, 8u, 8u);
    b_u(&want, 4096u, 8u);
    b_u(&want, 0u, 4u);
    b_u(&want, CINT_EVENT_EFFECT, 1u);
    b_u(&want, 0u, 8u);
    b_u(&want, 0u, 4u);
    b_u(&want, 2u, 4u);
    b_s(&want, "print");
    b_u(&want, 0u, 4u);
    b_u(&want, 10u, 8u);
    b_raw(&want, "deposit 5\n", 10u);
    b_u(&want, CINT_EVENT_OUTCOME, 1u);
    CHECK(rec.n > want.n && memcmp(rec.b, want.b, want.n) == 0, "the header, the code, the initial state, an entry and its effect");

    /* The decoder and its chain. */
    CHECK(cint_recording_decode(rec.b, rec.n, &info) == CINT_OK, "the recording decodes");
    CHECK(info.boundaries == 7u && info.end_reason == 0u && info.module_count == 1u && info.mode == CINT_RECORD_FULL,
          "seven boundaries");
    memset(chain, 0, sizeof chain);
    {
        bytes c0;
        b_init(&c0, 128u);
        b_s(&c0, "cint-core-1/recording-chain/v1");
        b_raw(&c0, hashes[0], 32u);
        cint_sha256(c0.b, c0.n, chain);
        free(c0.b);
    }
    memset(&e, 0, sizeof e);
    n = 0u;
    while (cint_recording_next(&info, &e) == CINT_OK) {
        static uint64_t entry_at;
        if (e.kind == CINT_EVENT_ENTRY) {
            entry_at = e.index;
        } else if (e.kind == CINT_EVENT_OUTCOME && e.has_hash) {
            n++;
            CHECK(memcmp(e.hash, hashes[n], 32u) == 0, "each outcome holds the state hash after its entry");
            chain_step(chain, entry_at, e.hash, e.status == CINT_FAULT ? e.data : NULL, e.data_len);
        }
    }
    CHECK(n == 7u && e.kind == CINT_EVENT_END && e.sequence == info.event_count && memcmp(chain, info.chain, 32u) == 0 &&
              memcmp(e.hash, chain, 32u) == 0,
          "the end event holds the head of the chain of SPEC-06 10.5 and the event count");
    CHECK(cint_recording_module(&info, NULL, 0u, &m) == CINT_OK && m.sir_len == sizeof r_sir - 1u &&
              m.source_len == sizeof r_source - 1u && cint_recording_module(&info, NULL, 1u, &m) == CINT_REFUSED,
          "the code");
    memset(&e, 0, sizeof e);
    CHECK(cint_recording_next(&info, &e) == CINT_OK && cint_recording_arg(&info, &e, 0u, &v) == CINT_OK &&
              v.len == 9u && v.bytes[1] == 5u && cint_recording_arg(&info, &e, 1u, &v) == CINT_REFUSED,
          "an entry's argument");

    /* Every event of the scenario, through the text of `show`. */
    n = cint_recording_render(&info, NULL, 0u);
    text = (char *)malloc(n + 1u);
    if (text != NULL) {
        CHECK(cint_recording_render(&info, text, n) == n, "the text");
        text[n] = '\0';
        CHECK(strstr(text, want_text) != NULL, "an entry and an effect");
        CHECK(strstr(text, "outcome fault fuel 2 allowance 2 state ") != NULL &&
                  strstr(text, "fault E_FUEL fuel.charge\nfault-limit I64 2\nfault-position app/ledger.ci:9:9\n") != NULL,
              "an outcome with its fault record");
        CHECK(strstr(text, "event 10 outcome faulted fuel 0 allowance 50\n") != NULL, "an entry a Faulted context refused");
        CHECK(strstr(text, " clear-fault record ") != NULL && strstr(text, " restore state ") != NULL &&
                  strstr(text, " checkpoint state ") != NULL,
              "a checkpoint, a clear and a restore");
        CHECK(strstr(text, "outcome ok fuel 1 allowance 50 error app.ledger.LedgerError 2 state ") != NULL &&
                  strstr(text, "outcome ok fuel 1 allowance 50 result Bool true state ") != NULL,
              "an error result and a result");
        CHECK(strstr(text, "event 17 entry 4 program fuel 50 depth 256 frame-arena 4096\n") != NULL,
              "the program entry with the context's depth limit");
        CHECK(strstr(text, "outcome fault fuel 4 allowance 4 state ") != NULL,
              "the interrupt lowered the allowance to the consumption");
        CHECK(strstr(text, " end host chain ") != NULL, "the end");
        free(text);
    }

    /* Replay on a fresh context: every boundary compares, and the context ends as recorded. */
    fresh = r_ctx(&out2, 4096);
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.entries = &r_obs;
    cfg.main_fn = f_main;
    CHECK(fresh != NULL && cint_replay(fresh, &info, &cfg, &rep) == CINT_OK && rep.status == CINT_OK &&
              rep.boundaries == 7u && rep.event == info.event_count && memcmp(rep.chain, info.chain, 32u) == 0,
          "replay compares every boundary");
    hash_of(fresh, h);
    CHECK(memcmp(h, hashes[7], 32u) == 0 && out2.n == 0u, "the same state, and no print without echo");
    cint_ctx_destroy(fresh);

    /* The stop boundary, and echo. */
    fresh = r_ctx(&out2, 4096);
    cfg.stop = 2u;
    cfg.echo = 1u;
    CHECK(fresh != NULL && cint_replay(fresh, &info, &cfg, &rep) == CINT_OK && rep.status == CINT_OK &&
              rep.boundaries == 2u,
          "replay stops after the second boundary");
    hash_of(fresh, h);
    CHECK(memcmp(h, hashes[2], 32u) == 0 && out2.n == 20u && memcmp(out2.b, "deposit 5\ndeposit 7\n", 20u) == 0,
          "the context as it was there, with the recorded print bytes echoed");
    cfg.stop = 0u;
    cfg.echo = 0u;
    CHECK(cint_replay(fresh, &info, &cfg, &rep) == CINT_OK && rep.status == CINT_OK && rep.boundaries == 7u,
          "replay restores the initial state into a context it used");
    cint_ctx_destroy(fresh);
    fresh = r_ctx(NULL, 4096);
    {
        uint8_t buf[8];
        cint_buffer_id id = 0u;
        CHECK(fresh != NULL && cint_buffer_register_bytes(fresh, buf, 8, CINT_VIEW_READ, &id) == CINT_OK &&
                  cint_buffer_release(fresh, id) == CINT_OK && cint_replay(fresh, &info, &cfg, &rep) == CINT_REFUSED,
              "a context whose identifier counters passed the recording's is refused");
    }
    cint_ctx_destroy(fresh);

    /* Comparisons that diverge. */
    other = r_ctx(NULL, 8192);
    CHECK(other != NULL && cint_replay(other, &info, &cfg, &rep) == CINT_OK && rep.status == CINT_DIVERGED &&
              rep.what == CINT_REPLAY_ENTRY && rep.event == 0u,
          "another frame-arena capacity");
    cint_ctx_destroy(other);
    fresh = r_ctx(NULL, 4096);
    g_step = 2;
    CHECK(fresh != NULL && cint_replay(fresh, &info, &cfg, &rep) == CINT_OK && rep.status == CINT_DIVERGED &&
              rep.what == CINT_REPLAY_RESULT && rep.event == 2u && rep.boundaries == 0u,
          "a backend that adds twice diverges at the first outcome");
    g_step = 1;
    cint_ctx_destroy(fresh);
    fresh = r_ctx(NULL, 4096);
    g_word = "Deposit";
    CHECK(fresh != NULL && cint_replay(fresh, &info, &cfg, &rep) == CINT_OK && rep.status == CINT_DIVERGED &&
              rep.what == CINT_REPLAY_EFFECT && rep.event == 1u,
          "other print bytes diverge at the effect");
    g_word = "deposit";
    cint_ctx_destroy(fresh);
    fresh = r_ctx(NULL, 4096);
    cfg.entries = &r_obs_burn_only;
    CHECK(fresh != NULL && cint_replay(fresh, &info, &cfg, &rep) == CINT_OK && rep.status == CINT_DIVERGED &&
              rep.what == CINT_REPLAY_ENTRY && rep.event == 0u,
          "a program without the function");
    cfg.entries = &r_obs;
    cint_ctx_destroy(fresh);
    fresh = r_ctx(NULL, 4096);
    cfg.main_fn = NULL;
    CHECK(fresh != NULL && cint_replay(fresh, &info, &cfg, &rep) == CINT_OK && rep.status == CINT_DIVERGED &&
              rep.what == CINT_REPLAY_ENTRY && rep.boundaries == 5u,
          "no program entry");
    cfg.main_fn = f_main;
    cint_ctx_destroy(fresh);

    /* Refusals: a truncated recording, and every prefix of it. */
    for (size_t cut = 0u; cut < rec.n; cut += cut < 400u ? 1u : 37u) {
        CHECK(cint_recording_decode(rec.b, cut, &info) == CINT_REFUSED, "a prefix is incomplete");
    }
    CHECK(cint_recording_decode(rec.b, rec.n, &info) == CINT_OK, "the whole");
    cint_ctx_destroy(ctx);
    free(rec.b);
    free(ckpt.b);
    free(out.b);
    free(out2.b);
    free(want.b);
    free(init.b);
}

/* The program of revision 2 for replay. */
static const cint_observer *next_revision(void *user, const cint_recording_event *e, cint_main_fn *main_fn,
                                          const cint_state_table **table)
{
    (void)user;
    if (memcmp(e->revision, r_rev2, 32u) != 0) {
        return NULL;
    }
    *main_fn = f_main;
    *table = &r_table;
    return &r_obs2;
}

/* A change of revision: recorded with its code, and replayed through the host's callback. */
static void record_revision(void)
{
    static const cint_code_module code = {{"app/ledger.ci", 13u, 0u}, (const uint8_t *)r_sir, sizeof r_sir - 1u, NULL, 0u};
    static const cint_code_module code2 = {{"app/ledger.ci", 13u, 0u}, (const uint8_t *)r_sir2, sizeof r_sir2 - 1u,
                                           NULL, 0u};
    static const cint_code_module bad = {{"app/ledger.cx", 13u, 0u}, (const uint8_t *)r_sir, sizeof r_sir - 1u, NULL, 0u};
    bytes rec;
    cint_sink sink;
    cint_recorder *r = NULL, *r2 = NULL;
    cint_recording_info info;
    cint_recording_event e;
    cint_replay_config cfg;
    cint_replay_report rep;
    cint_code_module m;
    cint_ctx *ctx = r_ctx(NULL, 4096), *fresh;
    uint64_t a = 5u, res = 0u, at = 0u;
    char *text;
    size_t n;
    b_init(&rec, 1u << 14);
    sink_of(&rec, &sink);
    CHECK(ctx != NULL && cint_rec_begin(ctx, &r_obs, &bad, 1u, CINT_RECORD_FULL, &sink, &r) == CINT_REFUSED &&
              cint_rec_begin(ctx, &r_obs, &code, 1u, 3u, &sink, &r) == CINT_REFUSED &&
              cint_rec_begin(NULL, &r_obs, &code, 1u, CINT_RECORD_FULL, &sink, &r) == CINT_REFUSED && rec.n == 0u,
          "a code path not ending in .ci, another mode, no context");
    CHECK(cint_rec_begin(ctx, &r_obs, &code, 1u, CINT_RECORD_DIGEST, &sink, &r) == CINT_OK, "begin");
    CHECK(cint_rec_begin(ctx, &r_obs, &code, 1u, CINT_RECORD_FULL, &sink, &r2) == CINT_REFUSED && r2 == NULL,
          "a context a recorder records is not recorded twice");
    CHECK(cint_rec_entry(r, E_DEPOSIT, &a, CINT_FUEL_UNBOUNDED, 8, &res) == CINT_OK && res == 105u, "deposit 5");
    CHECK(cint_rec_entry(r, 3u, &a, CINT_FUEL_UNBOUNDED, 8, &res) == CINT_REFUSED, "no entry 3");
    a = (uint64_t)1u << 8;
    CHECK(cint_rec_entry(r, E_CLOSE, &a, CINT_FUEL_UNBOUNDED, 8, &res) == CINT_REFUSED, "a U8 argument of 256");
    CHECK(cint_rec_change_revision(r, &r_obs2, &r_table, &bad, 1u) == CINT_REFUSED &&
              cint_rec_change_revision(r, &r_obs2, &r_table, &code2, 1u) == CINT_OK,
          "a change of revision");
    a = 1u;
    CHECK(cint_rec_entry(r, E_DEPOSIT, &a, CINT_FUEL_UNBOUNDED, 8, &res) == CINT_OK && res == 106u, "deposit 1");
    CHECK(cint_rec_end(r) == CINT_OK, "end");
    CHECK(cint_recording_decode(rec.b, rec.n, &info) == CINT_OK && info.mode == CINT_RECORD_DIGEST &&
              info.boundaries == 2u && memcmp(info.revision, r_rev, 32u) == 0,
          "it decodes");
    memset(&e, 0, sizeof e);
    while (cint_recording_next(&info, &e) == CINT_OK && e.kind != CINT_EVENT_REVISION) {
    }
    at = e.index;
    CHECK(e.kind == CINT_EVENT_REVISION && at == 3u && memcmp(e.revision, r_rev2, 32u) == 0 && e.module_count == 1u &&
              cint_recording_module(&info, &e, 0u, &m) == CINT_OK && m.sir_len == sizeof r_sir2 - 1u &&
              memcmp(m.sir, r_sir2, m.sir_len) == 0 && m.source == NULL,
          "the revision event holds the new identity and its code");
    n = cint_recording_render(&info, NULL, 0u);
    text = (char *)malloc(n + 1u);
    if (text != NULL) {
        (void)cint_recording_render(&info, text, n);
        text[n] = '\0';
        CHECK(strstr(text, "mode digest\n") != NULL && strstr(text, " revision 616263") != NULL &&
                  strstr(text, "sir 56 sha256 ") != NULL && strstr(text, "source absent\n") != NULL,
              "the text of a revision");
        free(text);
    }
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.entries = &r_obs;
    cfg.main_fn = f_main;
    cfg.revision = next_revision;
    fresh = r_ctx(NULL, 4096);
    CHECK(fresh != NULL && cint_replay(fresh, &info, &cfg, &rep) == CINT_OK && rep.status == CINT_OK &&
              rep.boundaries == 2u && memcmp(rep.chain, info.chain, 32u) == 0,
          "replay changes revision as recorded");
    cint_ctx_destroy(fresh);
    cfg.revision = NULL;
    fresh = r_ctx(NULL, 4096);
    CHECK(fresh != NULL && cint_replay(fresh, &info, &cfg, &rep) == CINT_OK && rep.status == CINT_DIVERGED &&
              rep.what == CINT_REPLAY_REVISION && rep.event == at && rep.boundaries == 1u &&
              strcmp(cint_replay_what(rep.what), "revision") == 0,
          "a host without the new revision diverges at it");
    cint_ctx_destroy(fresh);
    cint_ctx_destroy(ctx);
    free(rec.b);
}

/* The source-map digest of SPEC-09 SIR-17 (box 13 unit 6; rt/OPEN.md RT-OQ-45). */
static void source_maps(void)
{
    static const char lib[] = "module lib\nsites lib.ci 1:2:3:print 2:2:9:add.checked.i64\n  return\n";
    static const char moved[] = "module lib\nsites lib.ci 1:3:3:print 2:3:9:add.checked.i64\n  return\n";
    static const char *const bad[] = {
        "module lib\n", "module lib\nsites lib.ci 1:2:3:print", "module lib\nsites lib.cx 1:2:3:print\n",
        "module lib\nsites lib.ci 1:2:print\n", "module lib\nsites lib.ci 1:2:3:\n", "module lib\nsites lib.ci  1:2:3:print\n",
        "module lib\nsites lib.ci 1:2:3:print \n", "module lib\nsites lib.ci 4294967296:2:3:print\n",
        "module lib\nsites lib.ci 1:2:3:pr:int\n", "sites lib.ci 1:2:3:print\nmodule lib\n"};
    static const uint32_t site2[1][3] = {{1u, 4u, 1u}}, site3[2][3] = {{1u, 2u, 3u}, {2u, 2u, 9u}};
    static const char *const op2[1] = {"fuel.charge"}, *const op3[2] = {"print", "add.checked.i64"};
    cint_code_module two[2], swapped[2], m;
    uint8_t b[512], want[32], got[32], other[32];
    bytes x = {b, 0u, sizeof b, 0};
    size_t refused = 0u;
    memset(two, 0, sizeof two);
    two[0].path.bytes = "app/ledger.ci";
    two[0].path.len = 13u;
    two[0].sir = (const uint8_t *)r_sir2;
    two[0].sir_len = sizeof r_sir2 - 1u;
    two[1].path.bytes = "lib.ci";
    two[1].path.len = 6u;
    two[1].sir = (const uint8_t *)lib;
    two[1].sir_len = sizeof lib - 1u;
    swapped[0] = two[1];
    swapped[1] = two[0];
    ledger_map(1, want);
    CHECK(cint_code_source_map(two, 1u, got) == CINT_OK && memcmp(got, want, 32u) == 0, "one module, one site");
    b_s(&x, "cint-core-1/source-map/v1");
    b_u(&x, 2u, 4u);
    map_one(&x, "app/ledger.ci", 1u, site2, op2);
    map_one(&x, "lib.ci", 2u, site3, op3);
    cint_sha256(x.b, x.n, want);
    CHECK(cint_code_source_map(two, 2u, got) == CINT_OK && memcmp(got, want, 32u) == 0 &&
              cint_code_source_map(swapped, 2u, other) == CINT_OK && memcmp(other, want, 32u) == 0,
          "two modules, in byte order of path whatever their order");
    swapped[0].sir = (const uint8_t *)moved;
    swapped[0].sir_len = sizeof moved - 1u;
    CHECK(cint_code_source_map(swapped, 2u, other) == CINT_OK && memcmp(other, want, 32u) != 0,
          "sites a line lower, as a blank line above them leaves them, change it");
    m = two[1];
    for (size_t k = 0u; k < sizeof bad / sizeof bad[0]; k++) {
        m.sir = (const uint8_t *)bad[k];
        m.sir_len = strlen(bad[k]);
        refused += cint_code_source_map(&m, 1u, got) == CINT_REFUSED;
    }
    swapped[0] = two[0];
    m.sir = NULL;
    m.sir_len = 0u;
    CHECK(refused == sizeof bad / sizeof bad[0] && cint_code_source_map(&m, 1u, got) == CINT_REFUSED &&
              cint_code_source_map(two, 0u, got) == CINT_REFUSED && cint_code_source_map(NULL, 1u, got) == CINT_REFUSED &&
              cint_code_source_map(swapped, 2u, got) == CINT_REFUSED,
          "no well-formed sites line naming the path, no code, or one path twice");
}

/* A case, CINT-CASE-1 (SPEC-06 9.2): a recording that carries each module's source. */
static void record_case(void)
{
    static const cint_code_module bare = {{"app/ledger.ci", 13u, 0u}, (const uint8_t *)r_sir2, sizeof r_sir2 - 1u, NULL, 0u};
    static const cint_code_module code = {{"app/ledger.ci", 13u, 0u}, (const uint8_t *)r_sir2, sizeof r_sir2 - 1u,
                                          (const uint8_t *)r_source, sizeof r_source - 1u};
    bytes rec, want, copy;
    cint_sink sink;
    cint_recorder *r = NULL;
    cint_recording_info info;
    cint_replay_config cfg;
    cint_replay_report rep;
    cint_ctx *ctx = r_ctx(NULL, 4096), *fresh;
    uint64_t a = 5u, res = 0u;
    uint8_t map[32];
    char *text;
    size_t n, at;
    b_init(&rec, 1u << 14);
    b_init(&want, 1024u);
    b_init(&copy, 1u << 14);
    sink_of(&rec, &sink);
    CHECK(ctx != NULL && cint_rec_begin(ctx, &r_obs, &bare, 1u, CINT_RECORD_CASE, &sink, &r) == CINT_REFUSED &&
              cint_rec_begin(ctx, &r_obs, NULL, 0u, CINT_RECORD_CASE, &sink, &r) == CINT_REFUSED && rec.n == 0u,
          "a case needs code, with each module's source");
    CHECK(cint_rec_begin(ctx, &r_obs, &code, 1u, CINT_RECORD_CASE, &sink, &r) == CINT_OK, "begin a case");
    CHECK(cint_rec_entry(r, E_DEPOSIT, &a, CINT_FUEL_UNBOUNDED, 8, &res) == CINT_OK && res == 105u, "deposit 5");
    CHECK(cint_rec_change_revision(r, &r_obs2, &r_table, &bare, 1u) == CINT_REFUSED, "a revision without its source");
    CHECK(cint_rec_end(r) == CINT_OK, "end");
    b_s(&want, "cint-core-1/case/v1");
    b_s(&want, "cint-core-1");
    b_s(&want, "fuel-v1");
    b_s(&want, CINT_RT_CONTRACT);
    b_raw(&want, r_rev, 32u);
    b_u(&want, 1u, 1u);
    ledger_map(1, map);
    b_raw(&want, map, 32u);
    b_u(&want, 0u, 1u);
    CHECK(rec.n > want.n && memcmp(rec.b, want.b, want.n) == 0, "the domain of a case, its source-map digest, mode full");
    CHECK(cint_recording_decode(rec.b, rec.n, &info) == CINT_OK && info.is_case == 1u && info.mode == CINT_RECORD_FULL &&
              info.has_map == 1u && memcmp(info.map, map, 32u) == 0 && info.boundaries == 1u,
          "it decodes as a case");
    n = cint_recording_render(&info, NULL, 0u);
    text = (char *)malloc(n + 1u);
    if (text != NULL) {
        (void)cint_recording_render(&info, text, n);
        text[n] = '\0';
        CHECK(strncmp(text, "case cint-core-1/case/v1\n", 25u) == 0 && strstr(text, "\nsource-map ") != NULL &&
                  strstr(text, " source 17\n") != NULL,
              "the text of a case");
        free(text);
    }
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.entries = &r_obs;
    cfg.main_fn = f_main;
    fresh = r_ctx(NULL, 4096);
    CHECK(fresh != NULL && cint_replay(fresh, &info, &cfg, &rep) == CINT_OK && rep.status == CINT_OK &&
              rep.boundaries == 1u,
          "a case replays");
    cint_ctx_destroy(fresh);
    memcpy(copy.b, rec.b, copy.n = rec.n);
    copy.b[want.n - 1u] = 1u;
    CHECK(cint_recording_decode(copy.b, copy.n, &info) == CINT_REFUSED, "a case in digest mode");
    memcpy(copy.b, rec.b, copy.n);
    copy.b[want.n - 2u] ^= 1u;
    CHECK(cint_recording_decode(copy.b, copy.n, &info) == CINT_REFUSED, "a source-map digest that is not its code's");
    memcpy(copy.b, rec.b, copy.n);
    for (at = want.n; at + 5u <= copy.n && memcmp(copy.b + at, "1:4:1", 5u) != 0; at++) {
    }
    if (at + 5u <= copy.n) {
        copy.b[at + 2u] = '5';
    }
    CHECK(at + 5u <= copy.n && cint_recording_decode(copy.b, copy.n, &info) == CINT_REFUSED,
          "code whose sites are not the header's digest");
    at = want.n - 34u;  /* the presence byte */
    memcpy(copy.b, rec.b, at);
    copy.b[at] = 0u;
    memcpy(copy.b + at + 1u, rec.b + at + 33u, rec.n - at - 33u);
    copy.n = rec.n - 32u;
    CHECK(cint_recording_decode(copy.b, copy.n, &info) == CINT_REFUSED, "no digest for code that gives one");
    cint_ctx_destroy(ctx);
    free(rec.b);
    free(want.b);
    free(copy.b);
}

/* CINT_RESOURCE ends the recording; replay cannot go past it. A failing sink. */
static void record_resource(void)
{
    static const cint_code_module code = {{"app/ledger.ci", 13u, 0u}, (const uint8_t *)r_sir, sizeof r_sir - 1u, NULL, 0u};
    bytes rec, ck;
    cint_sink sink, cs;
    cint_recorder *r = NULL;
    cint_recording_info info;
    cint_recording_event e;
    cint_replay_config cfg;
    cint_replay_report rep;
    cint_ctx *ctx = r_ctx(NULL, 4096), *fresh;
    uint64_t a = 5u, res = 0u;
    b_init(&rec, 1u << 14);
    b_init(&ck, 4096u);
    sink_of(&rec, &sink);
    sink_of(&ck, &cs);
    CHECK(ctx != NULL && cint_rec_begin(ctx, &r_obs, &code, 1u, CINT_RECORD_FULL, &sink, &r) == CINT_OK, "begin");
    CHECK(cint_rec_entry(r, E_DEPOSIT, &a, 40, 8, &res) == CINT_OK, "deposit 5");
    g_resource = 1;
    CHECK(cint_rec_entry(r, E_DEPOSIT, &a, 40, 8, &res) == CINT_RESOURCE, "the machine ran out");
    g_resource = 0;
    CHECK(cint_rec_entry(r, E_DEPOSIT, &a, 40, 8, &res) == CINT_REFUSED && cint_rec_checkpoint(r, &cs) == CINT_REFUSED &&
              cint_rec_clear_fault(r) == CINT_REFUSED && cint_rec_main(r, f_main, 40) == CINT_REFUSED,
          "the recording ended");
    CHECK(cint_rec_end(r) == CINT_OK, "end");
    CHECK(cint_recording_decode(rec.b, rec.n, &info) == CINT_OK && info.end_reason == 1u && info.boundaries == 1u &&
              info.event_count == 5u,
          "an end of reason 1 after the outcome");
    memset(&e, 0, sizeof e);
    while (cint_recording_next(&info, &e) == CINT_OK && e.kind != CINT_EVENT_END) {
    }
    CHECK(e.kind == CINT_EVENT_END && e.reason == 1u && e.index == 5u, "the end event");
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.entries = &r_obs;
    fresh = r_ctx(NULL, 4096);
    CHECK(fresh != NULL && cint_replay(fresh, &info, &cfg, &rep) == CINT_OK && rep.status == CINT_UNREPLAYABLE &&
              rep.event == 3u && rep.boundaries == 1u,
          "replay stops before the entry that ran out");
    cfg.size = 4u;
    CHECK(cint_replay(fresh, &info, &cfg, &rep) == CINT_REFUSED && cint_replay(NULL, &info, &cfg, &rep) == CINT_REFUSED,
          "a configuration of another size, no context");
    cint_ctx_destroy(fresh);

    /* A sink that fails: in the header, the recorder is not made; later, the end reports it and
     * the bytes do not decode. */
    rec.n = 0u;
    rec.fail = 40;
    a = 5u;
    CHECK(cint_rec_begin(ctx, &r_obs, &code, 1u, CINT_RECORD_FULL, &sink, &r) == CINT_RESOURCE, "a sink that fails");
    rec.fail = 0;
    rec.n = 0u;
    CHECK(cint_rec_begin(ctx, &r_obs, &code, 1u, CINT_RECORD_FULL, &sink, &r) == CINT_OK, "begin again");
    rec.fail = (int)rec.n + 60;
    CHECK(cint_rec_entry(r, E_DEPOSIT, &a, 40, 8, &res) == CINT_OK && cint_rec_entry(r, E_DEPOSIT, &a, 40, 8, &res) == CINT_OK &&
              cint_rec_end(r) == CINT_RESOURCE && cint_recording_decode(rec.b, rec.n, &info) == CINT_REFUSED,
          "a sink that fails later");
    cint_ctx_destroy(ctx);
    free(rec.b);
    free(ck.b);
}

/* ------------------------------------------------------------------------- */
/* Extern calls (SPEC-03 A-15, H-14, H-17; SPEC-07 SEC-OS-11; rt/cint_rec_host.c; RT-OQ-70).
 * app/probe.ci imports `observe I64 get(I64 key, in I64[] keys, out I64[] buf)`, the pure
 * `I64 sq(I64 x)` and `unrestricted void sync()`; site 1 is an entry's charge, 2 the call. */
static const cint_site_info p_sites[] = {{0u, 0u, NULL}, {3u, 1u, "fuel.charge"}, {4u, 5u, "host.call"}};
static const cint_module p_mod = {"app/probe.ci", 12u, 3u, p_sites};
static const cint_module *const p_mods[] = {&p_mod};
static const uint8_t p_rev[32] = {0x71, 0x72, 0x73};
static const cint_program p_prog = {1u, 0u, p_mods, p_rev};
static const cint_site P_ENTRY = {0u, 1u};
static const cint_site P_CALL = {0u, 2u};
static const uint8_t p_sig[] = {1u, 2u, 3u};
static const cint_host_import p_imports[] = {
    {{"get", 3u, 0u}, CINT_OBSERVE, 3u, p_sig, 3u}, {{"sq", 2u, 0u}, CINT_PURE, 1u, p_sig, 2u},
    {{"sync", 4u, 0u}, CINT_UNRESTRICTED, 0u, NULL, 0u}};
static const cint_host_table p_table = {3u, 0u, p_imports};
static const cint_host_table *const p_tables[1] = {&p_table};
static const cint_host_desc p_desc = {1u, 0u, p_tables};
static const char p_sir[] = "module app.probe\nsites app/probe.ci\n";

static int g_host_live;       /* live calls of get and sync: replay in full mode makes none */
static int64_t g_host_shift;  /* what get adds to each element: another host, for digest replay */
static int64_t g_key_shift;   /* what fetch adds to its key: another request, for replay */

/* get: buf[i] = keys[i] + key + g_host_shift; returns the sum of buf. */
static cint_status p_get(void *user, const cint_value *args, uint32_t nargs, cint_value *result)
{
    const cint_arg_view *keys = &args[1].as.view, *buf = &args[2].as.view;
    int64_t sum = 0;
    (void)user;
    g_host_live++;
    if (nargs != 3u || keys->shape[0] != buf->shape[0]) {
        return CINT_REFUSED;
    }
    for (int64_t i = 0; i < buf->shape[0]; i++) {
        int64_t k, v;
        memcpy(&k, (const uint8_t *)keys->mem.u.host.ptr + i * keys->byte_stride[0], 8u);
        v = k + args[0].as.i64 + g_host_shift;
        memcpy((uint8_t *)buf->mem.u.host.ptr + i * buf->byte_stride[0], &v, 8u);
        sum += v;
    }
    result->as.i64 = sum;
    return CINT_OK;
}

static cint_status p_sq(void *user, const cint_value *args, uint32_t nargs, cint_value *result)
{
    (void)user;
    result->as.i64 = nargs == 1u ? args[0].as.i64 * args[0].as.i64 : 0;
    return CINT_OK;
}

static cint_status p_sync(void *user, const cint_value *args, uint32_t nargs, cint_value *result)
{
    (void)user;
    (void)args;
    (void)nargs;
    (void)result;
    g_host_live++;
    return CINT_OK;
}

static cint_host_fn p_fns[3];

static void p_fns_init(void)
{
    static const char *const names[3] = {"get", "sq", "sync"};
    static cint_status (*const calls[3])(void *, const cint_value *, uint32_t, cint_value *) = {p_get, p_sq, p_sync};
    memset(p_fns, 0, sizeof p_fns);
    for (unsigned i = 0u; i < 3u; i++) {
        p_fns[i].name = names[i];
        p_fns[i].effect = p_imports[i].effect;
        cint_sha256(p_imports[i].type_signature, (size_t)p_imports[i].type_signature_len, p_fns[i].signature);
        p_fns[i].call = calls[i];
    }
}

static void p_scalar(cint_value *v, int64_t x)
{
    memset(v, 0, sizeof *v);
    v->type.code = CINT_TAG_I64;
    v->kind = CINT_VAL_SCALAR;
    v->as.i64 = x;
}

/* I64 fetch(I64 key): get(key + g_key_shift, {1, 2}, buf), adds buf's elements to total, returns get's result. */
static cint_status f_fetch(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args, uint64_t *result)
{
    int64_t keys[2] = {1, 2}, buf[2] = {0, 0}, key, *total;
    cint_value hv[3], hr;
    cint_status st = cint_rt_entry_begin(ctx, P_ENTRY, fuel, depth);
    if (st != CINT_OK) {
        return st;
    }
    memcpy(&key, &args[0], sizeof key);
    p_scalar(&hv[0], key + g_key_shift);
    cint_rt_arg_view(&hv[1], CINT_TAG_I64, keys, 0, 2, 1, CINT_VIEW_READ);
    cint_rt_arg_view(&hv[2], CINT_TAG_I64, buf, 0, 2, 1, CINT_VIEW_WRITE);
    p_scalar(&hr, 0);
    if (cint_rt_host_call(ctx, P_CALL, 0u, hv, 3u, &hr, 0u) && (total = total_of(ctx)) != NULL) {
        *total += buf[0] + buf[1];
        memcpy(result, &hr.as.i64, sizeof *result);
    }
    return cint_rt_entry_end(ctx);
}

/* I64 twice(I64 x): sq(x). */
static cint_status f_twice(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args, uint64_t *result)
{
    cint_value hv[1], hr;
    int64_t x;
    cint_status st = cint_rt_entry_begin(ctx, P_ENTRY, fuel, depth);
    if (st != CINT_OK) {
        return st;
    }
    memcpy(&x, &args[0], sizeof x);
    p_scalar(&hv[0], x);
    p_scalar(&hr, 0);
    if (cint_rt_host_call(ctx, P_CALL, 1u, hv, 1u, &hr, 0u)) {
        memcpy(result, &hr.as.i64, sizeof *result);
    }
    return cint_rt_entry_end(ctx);
}

/* Bool flush(): sync(), then prints "flushed\n"; true. */
static cint_status f_flush(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args, uint64_t *result)
{
    cint_value hr;
    cint_status st = cint_rt_entry_begin(ctx, P_ENTRY, fuel, depth);
    (void)args;
    if (st != CINT_OK) {
        return st;
    }
    memset(&hr, 0, sizeof hr);
    if (cint_rt_host_call(ctx, P_CALL, 2u, NULL, 0u, &hr, 0u) && cint_rt_print_begin(ctx, P_CALL) &&
        cint_rt_print_bytes(ctx, "flushed\n", 8u) && cint_rt_print_end(ctx)) {
        *result = 1u;
    }
    return cint_rt_entry_end(ctx);
}

static const uint32_t t_flush[] = {CINT_TAG_BOOL, 0u};
static const cint_observer_entry p_entries[] = {
    {0u, 5u, "fetch", t_deposit, f_fetch}, {0u, 5u, "twice", t_deposit, f_twice}, {0u, 5u, "flush", t_flush, f_flush}};
static const cint_observer p_obs = {3u, 0u, &p_prog, p_entries};
#define E_FETCH 0u
#define E_TWICE 1u
#define E_FLUSH 2u

static cint_ctx *p_ctx(bytes *out)
{
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL;
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = &p_prog;
    cfg.frame_arena_bytes = 4096;
    cfg.host_count = 3u;
    cfg.host = p_fns;
    cfg.imports = &p_desc;
    if (out != NULL) {
        cfg.output = out_write;
        cfg.output_user = out;
    }
    if (cint_ctx_create(&cfg, &ctx) != CINT_OK) {
        return NULL;
    }
    if (cint_ctx_bind_state(ctx, &r_table) != CINT_OK) {
        cint_ctx_destroy(ctx);
        return NULL;
    }
    return ctx;
}

/* Records twice 6 and fetch 10 in `mode`, then flush when `flush` is set, into rec. */
static void p_record(bytes *rec, uint32_t mode, int flush)
{
    static const cint_code_module code = {{"app/probe.ci", 12u, 0u}, (const uint8_t *)p_sir, sizeof p_sir - 1u, NULL, 0u};
    cint_recorder *r = NULL;
    cint_sink sink;
    cint_ctx *ctx = p_ctx(NULL);
    uint64_t a = 6u, res = 0u;
    sink_of(rec, &sink);
    rec->n = 0u;
    CHECK(ctx != NULL && cint_rec_begin(ctx, &p_obs, &code, 1u, mode, &sink, &r) == CINT_OK, "begin");
    if (ctx == NULL) {
        return;
    }
    CHECK(cint_rec_entry(r, E_TWICE, &a, 50, 8, &res) == CINT_OK && res == 36u, "twice 6 is 36, through a pure extern");
    a = 10u;
    CHECK(cint_rec_entry(r, E_FETCH, &a, 50, 8, &res) == CINT_OK && res == 23u && *total_of(ctx) == 123,
          "fetch 10: get writes 11 and 12, and returns 23");
    if (flush) {
        g_host_live = 0;
        CHECK(cint_rec_entry(r, E_FLUSH, NULL, 50, 8, &res) == CINT_OK && res == 1u && g_host_live == 1,
              "flush: the unrestricted call is made");
        CHECK(cint_rec_entry(r, E_TWICE, &a, 50, 8, &res) == CINT_REFUSED, "the recording ended");
    }
    CHECK(cint_rec_end(r) == CINT_OK, "end");
    cint_ctx_destroy(ctx);
}

static void record_host(void)
{
    bytes rec, want, out;
    cint_recording_info info;
    cint_recording_event e;
    cint_replay_config cfg;
    cint_replay_report rep;
    cint_ctx *fresh;
    uint8_t d[32];
    size_t n;
    char *text;
    p_fns_init();
    b_init(&rec, 1u << 14);
    b_init(&want, 256u);
    b_init(&out, 256u);
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.entries = &p_obs;

    /* Full mode: the effect event of get, byte by byte from the header comment of rt/cint_rec.c. */
    p_record(&rec, CINT_RECORD_FULL, 0);
    CHECK(cint_recording_decode(rec.b, rec.n, &info) == CINT_OK && info.event_count == 5u && info.boundaries == 2u &&
              info.end_reason == 0u,
          "entry, outcome, entry, effect, outcome, end: a pure call leaves no event");
    memset(&e, 0, sizeof e);
    while (cint_recording_next(&info, &e) == CINT_OK && e.kind != CINT_EVENT_EFFECT) {
    }
    b_u(&want, 3u, 4u);
    b_u(&want, CINT_TAG_I64, 1u);
    b_u(&want, 10u, 8u);
    b_u(&want, 0x52u, 1u);
    b_u(&want, CINT_TAG_I64, 1u);
    b_u(&want, CINT_VIEW_READ, 1u);
    b_u(&want, 2u, 8u);
    b_u(&want, 1u, 8u);
    b_u(&want, 2u, 8u);
    b_u(&want, 0x52u, 1u);
    b_u(&want, CINT_TAG_I64, 1u);
    b_u(&want, CINT_VIEW_WRITE, 1u);
    b_u(&want, 2u, 8u);
    CHECK(e.kind == CINT_EVENT_EFFECT && e.index == 3u && e.ordinal == 0 && e.site.module == 0u && e.site.index == 2u &&
              e.name.len == 3u && memcmp(e.name.bytes, "get", 3u) == 0 && e.status == CINT_OK && e.data_len == want.n &&
              memcmp(e.data, want.b, want.n) == 0,
          "the request block: the count, the key, the in view with its elements, the out view");
    want.n = 0u;
    b_u(&want, 0u, 1u);
    b_u(&want, CINT_TAG_I64, 1u);
    b_u(&want, 23u, 8u);
    b_u(&want, 11u, 8u);
    b_u(&want, 12u, 8u);
    CHECK(e.response_len == want.n && memcmp(e.response, want.b, want.n) == 0,
          "the response block: the value alternative, the result, the out view's elements");
    n = cint_recording_render(&info, NULL, 0u);
    text = (char *)malloc(n + 1u);
    if (text != NULL) {
        CHECK(cint_recording_render(&info, text, n) == n, "the text");
        text[n] = '\0';
        CHECK(strstr(text, "event 3 effect 0 site 0.2 get status 0 request 03000000") != NULL &&
                  strstr(text, " response 00") != NULL && strstr(text, "0b000000000000000c00000000000000\n") != NULL,
              "the text of an extern call's effect");
        free(text);
    }

    /* Replay in full mode: the recorded response, and no live call. */
    g_host_live = 0;
    fresh = p_ctx(NULL);
    CHECK(fresh != NULL && cint_replay(fresh, &info, &cfg, &rep) == CINT_OK && rep.status == CINT_OK && rep.boundaries == 2u &&
              g_host_live == 0 && *total_of(fresh) == 123,
          "replay feeds the recorded response back and makes no live call");
    cint_ctx_destroy(fresh);
    fresh = p_ctx(NULL);
    g_key_shift = 1;
    CHECK(fresh != NULL && cint_replay(fresh, &info, &cfg, &rep) == CINT_OK && rep.status == CINT_DIVERGED &&
              rep.what == CINT_REPLAY_EFFECT && rep.event == 3u && rep.boundaries == 1u,
          "another request diverges at the effect");
    g_key_shift = 0;
    cint_ctx_destroy(fresh);

    /* Digest mode: the in view's elements and the response as their SHA-256; replay calls live. */
    p_record(&rec, CINT_RECORD_DIGEST, 0);
    CHECK(cint_recording_decode(rec.b, rec.n, &info) == CINT_OK && info.mode == CINT_RECORD_DIGEST, "a digest recording");
    memset(&e, 0, sizeof e);
    while (cint_recording_next(&info, &e) == CINT_OK && e.kind != CINT_EVENT_EFFECT) {
    }
    want.n = 0u;
    b_u(&want, 1u, 8u);
    b_u(&want, 2u, 8u);
    cint_sha256(want.b, 16u, d);
    CHECK(e.kind == CINT_EVENT_EFFECT && e.data_len == 4u + 9u + 11u + 32u + 11u && memcmp(e.data + 24u, d, 32u) == 0,
          "the in view's elements as their SHA-256");
    want.n = 0u;
    b_u(&want, 11u, 8u);
    b_u(&want, 12u, 8u);
    cint_sha256(want.b, 16u, d);
    CHECK(e.response_len == 1u + 9u + 32u && memcmp(e.response + 10u, d, 32u) == 0, "the out view's elements as their SHA-256");
    g_host_live = 0;
    fresh = p_ctx(NULL);
    CHECK(fresh != NULL && cint_replay(fresh, &info, &cfg, &rep) == CINT_OK && rep.status == CINT_OK && rep.boundaries == 2u &&
              g_host_live == 1,
          "replay in digest mode calls live and compares the digests");
    cint_ctx_destroy(fresh);
    fresh = p_ctx(NULL);
    g_host_shift = 1;
    CHECK(fresh != NULL && cint_replay(fresh, &info, &cfg, &rep) == CINT_OK && rep.status == CINT_DIVERGED &&
              rep.what == CINT_REPLAY_EFFECT && rep.event == 3u,
          "another host's response diverges");
    g_host_shift = 0;
    cint_ctx_destroy(fresh);

    /* An unrestricted call: its event, the end with reason 2, nothing after; replay stops before it. */
    p_record(&rec, CINT_RECORD_FULL, 1);
    CHECK(cint_recording_decode(rec.b, rec.n, &info) == CINT_OK && info.event_count == 7u && info.end_reason == 2u &&
              info.boundaries == 2u,
          "entry, unrestricted, end: no outcome, and the print after the call is not recorded");
    memset(&e, 0, sizeof e);
    while (cint_recording_next(&info, &e) == CINT_OK && e.kind != CINT_EVENT_UNRESTRICTED) {
    }
    CHECK(e.kind == CINT_EVENT_UNRESTRICTED && e.index == 6u && e.ordinal == 1 && e.site.index == 2u && e.name.len == 4u &&
              memcmp(e.name.bytes, "sync", 4u) == 0 && cint_recording_next(&info, &e) == CINT_OK && e.kind == CINT_EVENT_END &&
              e.reason == 2u && e.sequence == 7u,
          "the unrestricted event and the end");
    n = cint_recording_render(&info, NULL, 0u);
    text = (char *)malloc(n + 1u);
    if (text != NULL) {
        CHECK(cint_recording_render(&info, text, n) == n, "the text");
        text[n] = '\0';
        CHECK(strstr(text, "event 6 unrestricted 1 site 0.2 sync\n") != NULL && strstr(text, " end unrestricted chain ") != NULL,
              "the text of an unrestricted call");
        free(text);
    }
    g_host_live = 0;
    fresh = p_ctx(&out);
    CHECK(fresh != NULL && cint_replay(fresh, &info, &cfg, &rep) == CINT_OK && rep.status == CINT_UNREPLAYABLE && rep.event == 6u &&
              rep.boundaries == 2u && g_host_live == 0 && out.n == 0u && *total_of(fresh) == 123,
          "replay stops before the unrestricted call, which is not made");
    cint_ctx_destroy(fresh);
    free(rec.b);
    free(want.b);
    free(out.b);
}

/* rt/tests/test_rt.c: a fuzz target's input digest and receipt line (test_rt.py --fuzz-receipt). */
void test_fuzz_input(uint8_t digest[32], const uint8_t *p, size_t n);
void test_fuzz_line(const char *target, uint64_t seed, unsigned count, unsigned accepted, unsigned rejected,
                    unsigned failures, const uint8_t digest[32]);

/* A deterministic fuzz target: mutations of the scenario's recording through the decoder,
 * and replay of each that decodes. Named by its seed and count. */
static void fuzz(void)
{
    const uint64_t seed = 0x13c1a5edu;
    const unsigned count = 3000u;
    static uint8_t hashes[9][32], fault[512];
    bytes rec, ckpt, out, copy;
    cint_recording_info info;
    cint_replay_config cfg;
    cint_replay_report rep;
    uint64_t x = seed;
    size_t fault_len = 0u;
    uint8_t digest[32] = {0};
    unsigned accepted = 0u, crashes = 0u;
    cint_ctx *ctx;
    b_init(&rec, 1u << 16);
    b_init(&ckpt, 4096u);
    b_init(&out, 4096u);
    b_init(&copy, 1u << 16);
    ctx = r_ctx(&out, 4096);
    if (ctx == NULL || copy.b == NULL || scenario(ctx, &rec, &ckpt, hashes, fault, &fault_len) != CINT_OK) {
        CHECK(0, "the fuzz target's recording");
        return;
    }
    cint_ctx_destroy(ctx);
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.entries = &r_obs;
    cfg.main_fn = f_main;
    for (unsigned i = 0u; i < count; i++) {
        unsigned edits;
        memcpy(copy.b, rec.b, rec.n);
        copy.n = rec.n;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        edits = 1u + (unsigned)(x % 3u);
        for (unsigned k = 0u; k < edits; k++) {
            size_t at;
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
            at = (size_t)((x >> 8) % copy.n);
            switch (x % 4u) {
            case 0u: copy.b[at] ^= (uint8_t)(1u << ((x >> 40) % 8u)); break;
            case 1u: copy.b[at] = (uint8_t)(x >> 32); break;
            case 2u: copy.b[at] = (uint8_t)(copy.b[at] + 1u); break;
            default: copy.n = at > 0u ? at : 1u; break;
            }
        }
        test_fuzz_input(digest, copy.b, copy.n);
        if (cint_recording_decode(copy.b, copy.n, &info) != CINT_OK) {
            continue;
        }
        accepted++;
        ctx = r_ctx(NULL, 4096);
        if (ctx != NULL) {
            cint_status st = cint_replay(ctx, &info, &cfg, &rep);
            crashes += st == CINT_OK || st == CINT_REFUSED ? 0u : 1u;
            cint_ctx_destroy(ctx);
        }
    }
    test_fuzz_line("recording", seed, count, accepted, count - accepted, crashes, digest);
    CHECK(crashes == 0u, "replay of every decoded mutation returns CINT_OK or CINT_REFUSED");
    free(rec.b);
    free(ckpt.b);
    free(out.b);
    free(copy.b);
}

/* A fault record whose operands are fixed point of storage I128 (OQ-292; SPEC-01 11.1 row 31 admits
 * storage tags 11 to 15) decodes and replays: the decoder once stopped at I64, so a program that
 * faulted on a Q80.48 operation was recorded but "not a complete recording" (RT-OQ-44). */
static void record_fixed_fault(void)
{
    bytes rec;
    cint_sink sink;
    cint_recorder *r = NULL;
    cint_recording_info info;
    cint_recording_event e;
    cint_replay_config cfg;
    cint_replay_report rep;
    cint_ctx *ctx = r_ctx(NULL, 4096), *fresh;
    bool seen = false;
    b_init(&rec, 1u << 14);
    sink_of(&rec, &sink);
    CHECK(ctx != NULL && cint_rec_begin(ctx, &r_obs, NULL, 0u, CINT_RECORD_FULL, &sink, &r) == CINT_OK, "begin");
    CHECK(cint_rec_main(r, f_qfault, 50) == CINT_FAULT, "Q80.48.max + 1.0 faults");
    CHECK(cint_rec_end(r) == CINT_OK, "end");
    CHECK(cint_recording_decode(rec.b, rec.n, &info) == CINT_OK && info.boundaries == 1u,
          "a recording with a Q80.48 fault record is complete");
    memset(&e, 0, sizeof e);
    while (cint_recording_next(&info, &e) == CINT_OK) {
        seen = seen || (e.kind == CINT_EVENT_OUTCOME && e.status == CINT_FAULT);
    }
    CHECK(seen, "the outcome is the fault");
    fresh = r_ctx(NULL, 4096);
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.entries = &r_obs;
    cfg.main_fn = f_qfault;
    CHECK(fresh != NULL && cint_replay(fresh, &info, &cfg, &rep) == CINT_OK && rep.status == CINT_OK &&
              rep.boundaries == 1u && memcmp(rep.chain, info.chain, 32u) == 0,
          "replay reproduces the fault");
    cint_ctx_destroy(fresh);
    cint_ctx_destroy(ctx);
    free(rec.b);
}

/* The program arguments in the program entry's input record (RT-OQ-76; OQ-306): in full mode
 * replay gives them to the entry, in digest mode it compares them with the context's. */
static void record_args(void)
{
    static const uint8_t args[] = "a\0bc\0", other[] = "a\0bd\0";
    bytes rec, out;
    cint_recording_info info;
    cint_recording_event e;
    cint_replay_config cfg;
    cint_replay_report rep;
    cint_recorder *r = NULL;
    cint_sink sink;
    cint_ctx *ctx, *fresh;
    size_t n;
    char *text;
    uint32_t mode;
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.entries = &r_obs;
    cfg.main_fn = f_args_main;
    for (mode = CINT_RECORD_FULL; mode <= CINT_RECORD_DIGEST; mode++) {
        b_init(&rec, 1u << 14);
        b_init(&out, 256u);
        sink_of(&rec, &sink);
        ctx = r_ctx_args(&out, 4096, args, sizeof args);
        CHECK(ctx != NULL && cint_rec_begin(ctx, &r_obs, NULL, 0u, mode, &sink, &r) == CINT_OK &&
                  cint_rec_main(r, f_args_main, 50) == CINT_OK && cint_rec_end(r) == CINT_OK,
              "a program entry with arguments is recorded");
        CHECK(out.n == sizeof args && memcmp(out.b, args, sizeof args) == 0, "the entry read the arguments");
        cint_ctx_destroy(ctx);
        CHECK(cint_recording_decode(rec.b, rec.n, &info) == CINT_OK && info.boundaries == 1u, "it decodes");
        memset(&e, 0, sizeof e);
        CHECK(cint_recording_next(&info, &e) == CINT_OK && e.kind == CINT_EVENT_ENTRY && e.index == 0u,
              "the entry event");
        n = cint_recording_render(&info, NULL, 0u);
        text = (char *)malloc(n + 1u);
        if (text != NULL) {
            CHECK(cint_recording_render(&info, text, n) == n, "the text");
            text[n] = '\0';
            CHECK(strstr(text, mode == CINT_RECORD_FULL ? " args 6 bytes fuel " : " args 6 bytes digest fuel ") != NULL,
                  "the text names the arguments' extent, and the digest mode");
            free(text);
        }
        /* Replay: full mode gives the recorded bytes to the entry; digest mode wants the same
         * bytes in the context, and another extent or content diverges at the entry. The
         * entry's print is compared with the recorded effect, so other bytes would diverge there. */
        fresh = r_ctx_args(NULL, 4096, mode == CINT_RECORD_FULL ? NULL : args, mode == CINT_RECORD_FULL ? 0u : sizeof args);
        CHECK(fresh != NULL && cint_replay(fresh, &info, &cfg, &rep) == CINT_OK && rep.status == CINT_OK && rep.boundaries == 1u,
              "replay runs the entry over the recorded arguments");
        CHECK(cint_ctx_args(fresh, &n) == (mode == CINT_RECORD_FULL ? NULL : args), "the context's own arguments after replay");
        cint_ctx_destroy(fresh);
        if (mode == CINT_RECORD_DIGEST) {
            fresh = r_ctx_args(NULL, 4096, other, sizeof other);
            CHECK(fresh != NULL && cint_replay(fresh, &info, &cfg, &rep) == CINT_OK && rep.status == CINT_DIVERGED &&
                      rep.what == CINT_REPLAY_ENTRY && rep.boundaries == 0u,
                  "other arguments diverge at the entry");
            cint_ctx_destroy(fresh);
            fresh = r_ctx(NULL, 4096);
            CHECK(fresh != NULL && cint_replay(fresh, &info, &cfg, &rep) == CINT_OK && rep.status == CINT_DIVERGED &&
                      rep.what == CINT_REPLAY_ENTRY,
                  "no arguments diverge at the entry");
            cint_ctx_destroy(fresh);
        }
        free(rec.b);
        free(out.b);
    }
    /* A recording of a main without arguments replays on a context with them: the entry sees
     * none, as it was recorded, and its empty print matches the recorded effect. */
    b_init(&rec, 1u << 14);
    b_init(&out, 256u);
    sink_of(&rec, &sink);
    ctx = r_ctx(&out, 4096);
    CHECK(ctx != NULL && cint_rec_begin(ctx, &r_obs, NULL, 0u, CINT_RECORD_FULL, &sink, &r) == CINT_OK &&
              cint_rec_main(r, f_args_main, 50) == CINT_OK && cint_rec_end(r) == CINT_OK && out.n == 0u,
          "a program entry without arguments");
    cint_ctx_destroy(ctx);
    CHECK(cint_recording_decode(rec.b, rec.n, &info) == CINT_OK, "it decodes");
    fresh = r_ctx_args(NULL, 4096, args, sizeof args);
    CHECK(fresh != NULL && cint_replay(fresh, &info, &cfg, &rep) == CINT_OK && rep.status == CINT_OK && rep.boundaries == 1u,
          "replay runs the entry over no arguments");
    cint_ctx_destroy(fresh);
    fresh = r_ctx(NULL, 4096);
    CHECK(fresh != NULL && cint_replay(fresh, &info, &cfg, &rep) == CINT_OK && rep.status == CINT_OK && rep.boundaries == 1u,
          "and on a context without them");
    cint_ctx_destroy(fresh);
    free(rec.b);
    free(out.b);
}

void test_record(long *pass, long *fail)
{
    s_pass = 0;
    s_fail = 0;
    record_and_replay();
    record_args();
    record_revision();
    record_fixed_fault();
    source_maps();
    record_case();
    record_resource();
    record_host();
    fuzz();
    *pass += s_pass;
    *fail += s_fail;
}
