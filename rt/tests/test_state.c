/* test_state.c: checkpoints of a context (rt/cint_state.h; roadmap box 13, unit 4), linked
 * into test_rt. The layout of the canonical state and of CINT-CKPT-1 is built here byte by
 * byte from rt/cint_state.c's header comment and compared with what the runtime writes; then
 * restore (SPEC-03 H-19a, M-11, M-11a; section 9 cases 29, 29a and 29b), the fault record
 * in a checkpoint, the busy flag, the decoder and its text, the counters, the interrupt, and
 * the change of revision (H-19b). */
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

/* Two modules. Sites 1 and 2 of app/main.ci share a position, so a fault at site 2 comes
 * back from a checkpoint at site 1 and encodes to the same bytes. */
static const cint_site_info k_sites0[] = {
    {0u, 0u, NULL}, {3u, 5u, "add.checked.i64"}, {3u, 5u, "fuel.charge"}, {8u, 2u, "call.enter"}};
static const cint_site_info k_sites1[] = {{0u, 0u, NULL}, {4u, 9u, "fuel.charge"}};
static const cint_module k_mod0 = {"app/main.ci", 11u, 4u, k_sites0};
static const cint_module k_mod1 = {"lib.ci", 6u, 2u, k_sites1};
static const cint_module k_badmod = {"lib", 3u, 2u, k_sites1};
static const cint_module *const k_mods[] = {&k_mod0, &k_mod1};
static const cint_module *const k_badmods[] = {&k_badmod};
static const uint8_t k_rev[32] = {0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa,
                                  0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5,
                                  0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf};
static const uint8_t k_rev2[32] = {0x22};
static const cint_program k_prog = {2u, 0u, k_mods, k_rev};
static const cint_program k_prog2 = {2u, 0u, k_mods, k_rev2};
static const cint_program k_prog_seed = {2u, 0u, k_mods, NULL};  /* no revision identity (SEED-05) */
static const cint_program k_prog_bad = {1u, 0u, k_badmods, k_rev};
static const cint_site K_OP = {0u, 1u};
static const cint_site K_FUEL = {0u, 2u};
static const cint_site K_CALL = {0u, 3u};

/* app/main.ci: count I64 at 0 (5), flag Bool at 8 (true); lib.ci: total U32 at 0 (7), a I8
 * at 4 (-3). */
static const uint8_t k_init0[16] = {5, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t k_init1[8] = {7, 0, 0, 0, 0xfd, 0, 0, 0};
static const cint_state_var k_vars0[] = {{"count", 5u, CINT_TAG_I64, 0u, 0u}, {"flag", 4u, CINT_TAG_BOOL, 8u, 0u}};
static const cint_state_var k_vars1[] = {{"total", 5u, CINT_TAG_U32, 0u, 0u}, {"a", 1u, CINT_TAG_I8, 4u, 0u}};
static const cint_state k_state0 = {0u, 2u, 16u, 0u, k_init0, k_vars0};
static const cint_state k_state0_other = {0u, 2u, 16u, 0u, k_init0, k_vars0};
static const cint_state k_state1 = {1u, 2u, 8u, 0u, k_init1, k_vars1};
static const cint_state *const k_states[] = {&k_state0, &k_state1};
static const cint_state_table k_table = {2u, 0u, k_states};

/* The second revision: count kept, flag retyped to U8 (77), total dropped, a moved to offset
 * 0, b I16 new (300). */
static const uint8_t k2_init0[16] = {9, 0, 0, 0, 0, 0, 0, 0, 77, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t k2_init1[4] = {0x80, 0, 0x2c, 0x01};
static const cint_state_var k2_vars0[] = {{"count", 5u, CINT_TAG_I64, 0u, 0u}, {"flag", 4u, CINT_TAG_U8, 8u, 0u}};
static const cint_state_var k2_vars1[] = {{"a", 1u, CINT_TAG_I8, 0u, 0u}, {"b", 1u, CINT_TAG_I16, 2u, 0u}};
static const cint_state k2_state0 = {0u, 2u, 16u, 0u, k2_init0, k2_vars0};
static const cint_state k2_state1 = {1u, 2u, 4u, 0u, k2_init1, k2_vars1};
static const cint_state *const k2_states[] = {&k2_state0, &k2_state1};
static const cint_state_table k2_table = {2u, 0u, k2_states};

/* Tables that do not fit k_prog. */
static const cint_state_var k_var_z[] = {{"z", 1u, CINT_TAG_Z, 0u, 0u}};
static const cint_state_var k_var_out[] = {{"w", 1u, CINT_TAG_U64, 12u, 0u}};
static const cint_state_var k_var_twice[] = {{"count", 5u, CINT_TAG_I64, 0u, 0u}, {"count", 5u, CINT_TAG_U8, 8u, 0u}};
static const cint_state k_bad_module = {2u, 2u, 16u, 0u, k_init0, k_vars0};
static const cint_state k_bad_z = {1u, 1u, 8u, 0u, k_init1, k_var_z};
static const cint_state k_bad_out = {1u, 1u, 16u, 0u, k_init0, k_var_out};
static const cint_state k_bad_twice = {0u, 2u, 16u, 0u, k_init0, k_var_twice};
static const cint_state k_bad_lib = {0u, 2u, 8u, 0u, k_init1, k_vars1};

static const cint_record_layout k_records[1] = {{16u, 8u, 0u, 0u, NULL}};
static const cint_module_info k_info = {CINT_ABI_VERSION, 1u, &k_prog, k_records};

/* ------------------------------------------------------------------------- */

typedef struct bytes {
    uint8_t b[8192];
    size_t n;
    int fail;  /* the sink refuses writes */
} bytes;

static void b_u(bytes *x, uint64_t v, unsigned k)
{
    for (unsigned i = 0u; i < k; i++) {
        x->b[x->n++] = (uint8_t)(v >> (8u * i));
    }
}

static void b_raw(bytes *x, const void *p, size_t n)
{
    memcpy(x->b + x->n, p, n);
    x->n += n;
}

static void b_s(bytes *x, const char *s)
{
    b_u(x, strlen(s), 4u);
    b_raw(x, s, strlen(s));
}

static cint_status b_write(void *user, const uint8_t *p, size_t n)
{
    bytes *x = (bytes *)user;
    if (x->fail || n > sizeof x->b - x->n) {
        return CINT_RESOURCE;
    }
    b_raw(x, p, n);
    return CINT_OK;
}

static cint_status take(cint_ctx *ctx, bytes *x)
{
    cint_sink sink;
    x->n = 0u;
    sink.write = b_write;
    sink.user = x;
    return cint_ctx_checkpoint(ctx, &sink);
}

/* The state hash, or 32 zero bytes when it is refused. */
static void hash_of(cint_ctx *ctx, uint8_t out[32])
{
    if (cint_ctx_state_hash(ctx, out) != CINT_OK) {
        memset(out, 0, 32u);
    }
}

static cint_ctx *k_ctx(const cint_program *p, const cint_module_info *info)
{
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL;
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = p;
    cfg.module = info;
    return cint_ctx_create(&cfg, &ctx) == CINT_OK ? ctx : NULL;
}

/* The state of a fresh context of k_prog with globals as given, in the documented layout. */
static void k_state_bytes(bytes *x, int64_t count, uint8_t flag, uint32_t total, int8_t a, const bytes *registry,
                          uint64_t next_buffer, uint64_t entries, const bytes *fault, int64_t effects)
{
    x->n = 0u;
    b_s(x, "cint-core-1/state/v1");
    b_raw(x, k_rev, 32u);
    b_s(x, "cint-core-1");
    b_u(x, 4u, 8u);
    b_s(x, "app.main.count");
    b_u(x, CINT_TAG_I64, 1u);
    b_u(x, (uint64_t)count, 8u);
    b_s(x, "app.main.flag");
    b_u(x, CINT_TAG_BOOL, 1u);
    b_u(x, flag, 1u);
    b_s(x, "lib.a");
    b_u(x, CINT_TAG_I8, 1u);
    b_u(x, (uint8_t)a, 1u);
    b_s(x, "lib.total");
    b_u(x, CINT_TAG_U32, 1u);
    b_u(x, total, 4u);
    b_u(x, 0u, 8u);
    b_u(x, 0u, 8u);
    b_u(x, registry != NULL ? registry->n : 0u, 8u);
    if (registry != NULL) {
        b_raw(x, registry->b, registry->n);
    }
    b_u(x, next_buffer, 8u);
    b_u(x, 1u, 8u);
    b_u(x, 1u, 8u);
    b_u(x, entries, 8u);
    b_s(x, "fuel-v1");
    b_u(x, fault != NULL ? 1u : 0u, 1u);
    if (fault != NULL) {
        b_raw(x, fault->b, fault->n);
    }
    b_u(x, (uint64_t)effects, 8u);
}

static void k_checkpoint_bytes(bytes *x, const bytes *state)
{
    x->n = 0u;
    b_s(x, "cint-core-1/checkpoint/v1");
    b_s(x, CINT_RT_CONTRACT);
    b_u(x, 0u, 3u);
    b_u(x, state->n, 8u);
    b_raw(x, state->b, state->n);
}

static void b_entry(bytes *x, uint64_t id, uint64_t generation, uint32_t owner, uint16_t code, int64_t extent,
                    uint8_t ceiling, uint8_t publish)
{
    b_u(x, id, 8u);
    b_u(x, generation, 8u);
    b_u(x, owner, 1u);
    b_u(x, code, 2u);
    b_u(x, 0u, 1u);
    b_u(x, 0u, 2u);
    b_u(x, 0u, 4u);
    b_u(x, (uint64_t)extent, 8u);
    b_u(x, ceiling, 1u);
    b_u(x, publish, 1u);
    b_u(x, 0u, 1u);
}

static size_t find(const bytes *x, size_t from, const char *s)
{
    size_t n = strlen(s);
    for (size_t i = from; i + n <= x->n; i++) {
        if (memcmp(x->b + i, s, n) == 0) {
            return i;
        }
    }
    return SIZE_MAX;
}

static void hex(char *out, const uint8_t *p, size_t n)
{
    for (size_t i = 0u; i < n; i++) {
        snprintf(out + 2u * i, 3u, "%02x", p[i]);
    }
}

static int g_fail_alloc;
static void *k_alloc(void *user, size_t n, size_t align)
{
    (void)user;
    (void)align;
    return g_fail_alloc ? NULL : malloc(n);
}

static void k_release(void *user, void *p, size_t n, size_t align)
{
    (void)user;
    (void)n;
    (void)align;
    free(p);
}

/* ------------------------------------------------------------------------- */

/* Binding, the layout of a fresh context, and what refuses a checkpoint. */
static void test_layout(void)
{
    static const cint_state *const twice[] = {&k_state0, &k_state0};
    static const cint_state *const out_of_range[] = {&k_bad_module};
    static const cint_state *const z[] = {&k_bad_z};
    static const cint_state *const outside[] = {&k_bad_out};
    static const cint_state *const named_twice[] = {&k_bad_twice};
    static const cint_state *const lib[] = {&k_bad_lib};
    static const cint_state_table bad[] = {{2u, 0u, twice}, {1u, 0u, out_of_range}, {1u, 0u, z},
                                           {1u, 0u, outside}, {1u, 0u, named_twice}, {1u, 1u, k_states}};
    static const cint_state_table lib_table = {1u, 0u, lib};
    static bytes got, state, want;
    cint_ctx *ctx = k_ctx(&k_prog, NULL), *seed = k_ctx(&k_prog_seed, NULL), *badpath = k_ctx(&k_prog_bad, NULL);
    uint8_t h[32], d[32];
    if (ctx == NULL || seed == NULL || badpath == NULL) {
        CHECK(0, "cint_ctx_create");
        return;
    }
    CHECK(take(ctx, &got) == CINT_REFUSED && cint_ctx_state_hash(ctx, h) == CINT_REFUSED,
          "a context with no bound state has no checkpoint");
    for (size_t i = 0u; i < sizeof bad / sizeof bad[0]; i++) {
        CHECK(cint_ctx_bind_state(ctx, &bad[i]) == CINT_REFUSED, "a table that does not fit is refused");
    }
    CHECK(cint_ctx_bind_state(badpath, &lib_table) == CINT_REFUSED, "a module path without .ci is refused");
    CHECK(cint_ctx_bind_state(NULL, &k_table) == CINT_REFUSED && cint_ctx_checkpoint(ctx, NULL) == CINT_REFUSED,
          "NULL arguments are refused");
    CHECK(take(ctx, &got) == CINT_REFUSED, "a refused table binds nothing");
    CHECK(cint_ctx_bind_state(ctx, &k_table) == CINT_OK && take(ctx, &got) == CINT_OK, "bind and checkpoint");
    k_state_bytes(&state, 5, 1u, 7u, -3, NULL, 1u, 0u, NULL, 0);
    k_checkpoint_bytes(&want, &state);
    CHECK(got.n == want.n && memcmp(got.b, want.b, want.n) == 0, "the bytes of a fresh context");
    cint_sha256(state.b, state.n, d);
    CHECK(cint_ctx_state_hash(ctx, h) == CINT_OK && memcmp(h, d, 32u) == 0, "the state hash is SHA-256 of the state");
    CHECK(cint_ctx_bind_state(seed, &k_table) == CINT_OK && take(seed, &got) == CINT_REFUSED,
          "a program without a revision identity has no checkpoint");
    CHECK(cint_ctx_bind_state(seed, NULL) == CINT_OK, "a NULL table binds no state");

    /* A block made from another cint_state for the module refuses the table. */
    CHECK(cint_rt_state(seed, &k_state0_other) != NULL && cint_ctx_bind_state(seed, &k_table) == CINT_REFUSED,
          "a block of another cint_state is refused");
    CHECK(cint_rt_state(ctx, &k_state0) != NULL && take(ctx, &got) == CINT_OK && got.n == want.n &&
              memcmp(got.b, want.b, want.n) == 0,
          "a block made from the initial image checkpoints as before");
    CHECK(cint_rt_state(ctx, &k_state0_other) != NULL && cint_ctx_bind_state(ctx, &k_table) == CINT_OK,
          "a later access with another cint_state keeps the block");

    /* Allocation failure. */
    {
        cint_ctx_config cfg;
        cint_ctx *a = NULL;
        memset(&cfg, 0, sizeof cfg);
        cfg.size = (uint32_t)sizeof cfg;
        cfg.program = &k_prog;
        cfg.allocator.alloc = k_alloc;
        cfg.allocator.release = k_release;
        if (cint_ctx_create(&cfg, &a) == CINT_OK && cint_ctx_bind_state(a, &k_table) == CINT_OK) {
            g_fail_alloc = 1;
            CHECK(take(a, &got) == CINT_RESOURCE && cint_ctx_state_hash(a, h) == CINT_RESOURCE,
                  "CINT_RESOURCE when memory runs out");
            g_fail_alloc = 0;
            CHECK(take(a, &got) == CINT_OK, "and a checkpoint once it does not");
            got.fail = 1;
            CHECK(take(a, &got) == CINT_RESOURCE, "the sink's status ends the checkpoint");
            got.fail = 0;
        } else {
            CHECK(0, "a context with a counting allocator");
        }
        cint_ctx_destroy(a);
    }
    cint_ctx_destroy(ctx);
    cint_ctx_destroy(seed);
    cint_ctx_destroy(badpath);
}

/* Registrations, restore (cases 29, 29a, 29b, M-11a), the decoder and its text. */
static void test_restore(void)
{
    static bytes c1, c2, state, want, reg, tmp;
    static uint8_t buf16[16], buf8[8];
    static int64_t words[2];
    static const cint_type i64 = {CINT_TAG_I64, 0u, 0u, 0u, 0u, 0u, 0u};
    static const cint_type i32 = {CINT_TAG_I32, 0u, 0u, 0u, 0u, 0u, 0u};
    static char text[4096], expect[4096];
    cint_ctx *ctx = k_ctx(&k_prog, NULL);
    cint_buffer_id id = 0u, created = 0u;
    uint64_t gen = 0u, lease = 0u;
    uint8_t h[32], h2[32];
    const void *ptr = NULL;
    cint_checkpoint_info info;
    cint_checkpoint_global g;
    cint_checkpoint_buffer b;
    size_t cur = 0u, n, accepted = 0u;
    char rev[65], sh[65];
    uint32_t reason = 0u;
    int ok = 1;
    if (ctx == NULL || cint_ctx_bind_state(ctx, &k_table) != CINT_OK) {
        CHECK(0, "a bound context");
        cint_ctx_destroy(ctx);
        return;
    }
    {
        uint8_t *block = (uint8_t *)cint_rt_state(ctx, &k_state0);
        int64_t minus2 = -2;
        CHECK(block != NULL, "the block of app/main.ci");
        if (block != NULL) {
            memcpy(block, &minus2, 8u);  /* little-endian hosts */
        }
    }
    CHECK(cint_buffer_register_bytes(ctx, buf16, 16, CINT_VIEW_READ, &id) == CINT_OK && id == 1u, "id 1");
    CHECK(cint_buffer_create(ctx, i32, 4, 0u, &created, &gen) == CINT_OK && created == 2u, "id 2, created");
    CHECK(cint_buffer_register_elements(ctx, words, &i64, 8, 2, CINT_VIEW_WRITE, &id) == CINT_OK && id == 3u, "id 3");
    CHECK(cint_buffer_register_bytes(ctx, buf8, 8, CINT_VIEW_WRITE, &id) == CINT_OK && id == 4u &&
              cint_buffer_release(ctx, 4u) == CINT_OK,
          "id 4, released");
    CHECK(cint_buffer_register_bytes(ctx, buf8, 8, CINT_VIEW_WRITE, &id) == CINT_OK && id == 5u, "id 5");
    CHECK(take(ctx, &c1) == CINT_OK, "checkpoint c1");
    reg.n = 0u;
    b_s(&reg, "cint-core-1/registry/v1");
    b_u(&reg, 4u, 8u);
    b_entry(&reg, 1u, 1u, CINT_OWNER_BORROWED, 0u, 16, CINT_VIEW_READ, CINT_PUBLISH_NONE);
    b_entry(&reg, 2u, 1u, CINT_OWNER_BRIDGE, CINT_TAG_I32, 4, CINT_VIEW_WRITE, CINT_PUBLISH_NONE);
    b_entry(&reg, 3u, 1u, CINT_OWNER_BORROWED, CINT_TAG_I64, 2, CINT_VIEW_WRITE, CINT_PUBLISH_NONE);
    b_entry(&reg, 5u, 1u, CINT_OWNER_BORROWED, 0u, 8, CINT_VIEW_WRITE, CINT_PUBLISH_NONE);
    k_state_bytes(&state, -2, 1u, 7u, -3, &reg, 6u, 0u, NULL, 0);
    k_checkpoint_bytes(&want, &state);
    CHECK(c1.n == want.n && memcmp(c1.b, want.b, want.n) == 0, "the bytes with registrations and a block");

    /* The decoder and the text of `cint-interp show`. */
    CHECK(cint_checkpoint_decode(c1.b, c1.n, &info) == CINT_OK && info.global_count == 4u &&
              info.buffer_count == 4u && info.next_buffer == 6u && info.next_arena == 1u && info.next_pool == 1u &&
              info.entries == 0u && info.effects == 0 && info.fault == NULL && info.payload_digest == NULL &&
              info.contract.len == 9u && memcmp(info.revision, k_rev, 32u) == 0,
          "cint_checkpoint_decode");
    CHECK(cint_checkpoint_global_next(&info, &cur, &g) == CINT_OK && g.name.len == 14u &&
              memcmp(g.name.bytes, "app.main.count", 14u) == 0 && g.value.len == 9u && g.value.bytes[1] == 0xfe,
          "the first global");
    for (n = 1u; cint_checkpoint_global_next(&info, &cur, &g) == CINT_OK; n++) {
    }
    CHECK(n == 4u, "four globals");
    cur = 0u;
    CHECK(cint_checkpoint_buffer_next(&info, &cur, &b) == CINT_OK && b.id == 1u && b.type.code == 0u &&
              b.extent == 16 && b.owner == CINT_OWNER_BORROWED,
          "the first registry entry");
    CHECK(cint_checkpoint_buffer_next(&info, &cur, &b) == CINT_OK && b.id == 2u && b.owner == CINT_OWNER_BRIDGE &&
              b.type.code == CINT_TAG_I32 && b.ceiling == CINT_VIEW_WRITE,
          "the created buffer");
    CHECK(cint_checkpoint_decode(NULL, 0u, &info) == CINT_REFUSED && cint_checkpoint_global_next(NULL, &cur, &g) ==
              CINT_REFUSED, "NULL arguments");
    (void)cint_checkpoint_decode(c1.b, c1.n, &info);
    hex(rev, k_rev, 32u);
    cint_sha256(info.state, info.state_len, h);
    hex(sh, h, 32u);
    snprintf(expect, sizeof expect,
             "checkpoint cint-core-1/checkpoint/v1\ncontract cint-rt-3\npayload-digest absent\n"
             "build-record-digest absent\nbuild-identity absent\nstate cint-core-1/state/v1\nrevision %s\n"
             "profile cint-core-1\nfuel-model fuel-v1\nglobal app.main.count I64 -2\nglobal app.main.flag Bool true\n"
             "global lib.a I8 -3\nglobal lib.total U32 7\narena-block 0\npool-block 0\n"
             "buffer 1 generation 1 type bytes extent 16 owner borrowed ceiling read publish none state live\n"
             "buffer 2 generation 1 type I32 extent 4 owner bridge ceiling write publish none state live\n"
             "buffer 3 generation 1 type I64 extent 2 owner borrowed ceiling write publish none state live\n"
             "buffer 5 generation 1 type bytes extent 8 owner borrowed ceiling write publish none state live\n"
             "counters next-buffer 6 next-arena 1 next-pool 1 entries 0\nfault absent\neffects 0\nstate-hash %s\n",
             rev, sh);
    n = cint_checkpoint_render(&info, NULL, 0u);
    CHECK(n == strlen(expect) && cint_checkpoint_render(&info, text, sizeof text) == n &&
              memcmp(text, expect, n) == 0,
          "the text of show");
    CHECK(cint_checkpoint_render(&info, text, 10u) == n && memcmp(text, expect, 10u) == 0, "a short buffer");

    /* Case 29b: identifiers are never reissued across a restore (M-11). */
    CHECK(cint_buffer_register_bytes(ctx, buf8, 8, CINT_VIEW_READ, &id) == CINT_OK && id == 6u, "id 6");
    hash_of(ctx, h);
    CHECK(cint_ctx_restore(ctx, c1.b, c1.n) == CINT_OK, "restore c1");
    CHECK(cint_buffer_release(ctx, 1u) == CINT_REFUSED && cint_ctx_refusal(ctx, &reason) == CINT_OK &&
              reason == CINT_REFUSAL_GENERATION && cint_buffer_release(ctx, 6u) == CINT_REFUSED,
          "M-11a: every registration is removed");
    CHECK(cint_buffer_register_bytes(ctx, buf8, 8, CINT_VIEW_READ, &id) == CINT_OK && id == 7u,
          "case 29b: the next identifier is 7");
    CHECK(cint_ctx_restore(ctx, c1.b, c1.n) == CINT_OK && take(ctx, &c2) == CINT_OK &&
              cint_checkpoint_decode(c2.b, c2.n, &info) == CINT_OK && info.buffer_count == 0u &&
              info.next_buffer == 8u,
          "the counter is the greater of the checkpoint's and the context's");
    hash_of(ctx, h2);
    CHECK(memcmp(h, h2, 32u) != 0, "different registration histories are different states (M-1)");

    /* Case 29: another runtime contract or another revision is refused, the context unchanged. */
    memcpy(tmp.b, c1.b, c1.n);
    tmp.n = c1.n;
    tmp.b[find(&tmp, 0u, CINT_RT_CONTRACT) + 8u] = '9';
    CHECK(cint_checkpoint_decode(tmp.b, tmp.n, &info) == CINT_OK && info.contract.bytes[8] == '9',
          "the decoder shows another contract");
    CHECK(cint_ctx_restore(ctx, tmp.b, tmp.n) == CINT_REFUSED, "case 29: another runtime contract");
    memcpy(tmp.b, c1.b, c1.n);
    tmp.b[find(&tmp, 0u, "cint-core-1/state/v1") + 20u] ^= 1u;
    CHECK(cint_ctx_restore(ctx, tmp.b, tmp.n) == CINT_REFUSED, "case 29: another revision identity");
    hash_of(ctx, h);
    CHECK(memcmp(h, h2, 32u) == 0, "a refused restore leaves the context unchanged");

    /* Case 29a: malformed bytes; every truncation, a trailing byte, single-byte changes. */
    for (n = 0u; n < c1.n; n++) {
        ok &= cint_ctx_restore(ctx, c1.b, n) == CINT_REFUSED;
    }
    memcpy(tmp.b, c1.b, c1.n);
    tmp.b[c1.n] = 0u;
    ok &= cint_ctx_restore(ctx, tmp.b, c1.n + 1u) == CINT_REFUSED;
    hash_of(ctx, h);
    CHECK(ok && memcmp(h, h2, 32u) == 0, "case 29a: truncations and a trailing byte are refused, unchanged");
    ok = 1;
    for (n = 0u; n < c1.n; n++) {
        static const uint8_t masks[3] = {0x01u, 0x80u, 0xffu};
        for (size_t k = 0u; k < 3u; k++) {
            cint_status st;
            memcpy(tmp.b, c1.b, c1.n);
            tmp.b[n] ^= masks[k];
            st = cint_ctx_restore(ctx, tmp.b, c1.n);
            if (st == CINT_OK) {  /* a value or a counter changed: back to c2, from a new baseline */
                accepted++;
                ok &= cint_ctx_restore(ctx, c2.b, c2.n) == CINT_OK;
                hash_of(ctx, h2);
            } else {
                hash_of(ctx, h);
                ok &= st == CINT_REFUSED && memcmp(h, h2, 32u) == 0;
            }
            (void)cint_checkpoint_decode(tmp.b, c1.n, &info);
        }
    }
    CHECK(ok && accepted > 0u, "case 29a: a changed byte is refused with the context unchanged, or restores");
    memcpy(tmp.b, c1.b, c1.n);
    tmp.b[find(&tmp, 0u, "app.main.count") + 14u] = CINT_TAG_Z;
    CHECK(cint_checkpoint_decode(tmp.b, c1.n, &info) == CINT_REFUSED, "a Z global is refused");
    memcpy(tmp.b, c1.b, c1.n);
    tmp.b[find(&tmp, 0u, "app.main.flag") + 14u] = 2u;
    CHECK(cint_checkpoint_decode(tmp.b, c1.n, &info) == CINT_REFUSED, "a Bool of 2 is refused");

    /* Leases (A-9), and created storage freed by a restore. */
    CHECK(cint_buffer_create(ctx, i32, 4, 0u, &created, &gen) == CINT_OK &&
              cint_buffer_lease(ctx, created, &ptr, &lease) == CINT_OK,
          "a lease");
    CHECK(cint_ctx_restore(ctx, c1.b, c1.n) == CINT_BUSY, "CINT_BUSY while a lease is live");
    CHECK(cint_lease_end(ctx, lease) == CINT_OK && cint_ctx_restore(ctx, c1.b, c1.n) == CINT_OK,
          "restore once the lease ends");
    cint_ctx_destroy(ctx);
}

/* A Faulted context, the entry and effect counters, the busy flag, a record type. */
static void test_faulted(void)
{
    static bytes c3, c4, f1, f2;
    static cint_fault_record rec;
    static uint8_t buf16[16];
    static const cint_type record = {CINT_TAG_RECORD, 0u, 0u, 0u, 0u, 0u, 0u};
    static char text[8192];
    cint_ctx *ctx = k_ctx(&k_prog, NULL), *mod = k_ctx(&k_prog, &k_info);
    cint_checkpoint_info info;
    cint_buffer_id id = 0u;
    size_t len = 0u, at;
    uint8_t h[32];
    if (ctx == NULL || mod == NULL || cint_ctx_bind_state(ctx, &k_table) != CINT_OK ||
        cint_ctx_bind_state(mod, &k_table) != CINT_OK) {
        CHECK(0, "bound contexts");
        cint_ctx_destroy(ctx);
        cint_ctx_destroy(mod);
        return;
    }
    /* Entries and effects: a refused call is not an entry (H-12). */
    CHECK(cint_rt_entry_open(ctx, K_OP, 10, 8) == CINT_OK && cint_rt_print_begin(ctx, K_OP) &&
              cint_rt_print_bytes(ctx, "hi\n", 3u) && cint_rt_print_end(ctx) && cint_rt_entry_end(ctx) == CINT_OK,
          "an entry with one print statement");
    CHECK(cint_rt_entry_open(ctx, K_OP, 10, 8) == CINT_OK && !cint_rt_refuse(ctx, CINT_REFUSAL_BUFFER) &&
              cint_rt_entry_end(ctx) == CINT_REFUSED,
          "a refused call");
    CHECK(take(ctx, &c3) == CINT_OK && cint_checkpoint_decode(c3.b, c3.n, &info) == CINT_OK && info.entries == 1u &&
              info.effects == 1,
          "the entry sequence number and the effect-log position");

    /* The busy flag (A-11). */
    CHECK(cint_rt_entry_open(ctx, K_OP, 10, 8) == CINT_OK, "an open entry");
    CHECK(take(ctx, &c4) == CINT_BUSY && cint_ctx_restore(ctx, c3.b, c3.n) == CINT_BUSY &&
              cint_ctx_state_hash(ctx, h) == CINT_BUSY && cint_ctx_bind_state(ctx, &k_table) == CINT_BUSY &&
              cint_ctx_change_revision(ctx, &k_prog2, &k2_table) == CINT_BUSY,
          "CINT_BUSY inside an entry");

    /* A fault at site 2, under a call at site 3. */
    CHECK(cint_rt_call_enter(ctx, K_CALL) && !cint_fuel_charge(ctx, K_FUEL, 11u) &&
              cint_rt_entry_end(ctx) == CINT_FAULT,
          "an entry that faults E_FUEL");
    CHECK(cint_fault_get(ctx, f1.b, sizeof f1.b, &len) == CINT_OK && len > 0u, "its record");
    f1.n = len;
    CHECK(take(ctx, &c4) == CINT_OK && cint_checkpoint_decode(c4.b, c4.n, &info) == CINT_OK &&
              info.fault_len == f1.n && memcmp(info.fault, f1.b, f1.n) == 0 && info.entries == 2u,
          "a Faulted context checkpoints its fault record");
    CHECK(cint_ctx_clear_fault(ctx) == CINT_OK && cint_ctx_restore(ctx, c4.b, c4.n) == CINT_OK, "restore it");
    CHECK(cint_fault_get(ctx, f2.b, sizeof f2.b, &len) == CINT_OK && len == f1.n && memcmp(f2.b, f1.b, len) == 0,
          "the same record");
    CHECK(cint_ctx_fault(ctx, &rec) == CINT_OK && rec.position.index == 1u && rec.stack_count == 1u &&
              rec.stack[0].index == 3u,
          "the position comes back as the lowest site with its line and column");
    CHECK(cint_rt_entry_open(ctx, K_OP, 10, 8) == CINT_FAULTED, "the restored context is Faulted");
    CHECK(take(ctx, &c3) == CINT_OK && c3.n == c4.n && memcmp(c3.b, c4.b, c4.n) == 0,
          "the same checkpoint after the restore");
    CHECK(cint_checkpoint_decode(c4.b, c4.n, &info) == CINT_OK &&
              cint_checkpoint_render(&info, text, sizeof text) < sizeof text &&
              strstr(text, "fault E_FUEL fuel.charge\nfault-limit I64 10\nfault-position app/main.ci:3:5\n") != NULL &&
              strstr(text, "fault-source-map absent\nfault-call app/main.ci:8:2\neffects 1\n") != NULL,
          "the fault record field by field");
    memcpy(c3.b, c4.b, c4.n);
    c3.n = c4.n;
    at = find(&c3, find(&c3, 0u, "cint-core-1/fault/v2"), "app/main.ci");
    c3.b[at + 11u] = 99u;  /* the line: no site at 99:5 */
    CHECK(cint_checkpoint_decode(c3.b, c4.n, &info) == CINT_OK && cint_ctx_restore(ctx, c3.b, c4.n) == CINT_REFUSED,
          "a fault position the program does not have is refused");
    CHECK(cint_ctx_change_revision(ctx, &k_prog2, &k2_table) == CINT_REFUSED, "no change of revision when Faulted");

    /* A registration of a record type needs the module's layout table. */
    CHECK(cint_buffer_register_elements(mod, buf16, &record, 16, 1, CINT_VIEW_READ, &id) == CINT_OK &&
              take(mod, &c3) == CINT_OK,
          "a checkpoint with a record registration");
    CHECK(cint_ctx_restore(ctx, c3.b, c3.n) == CINT_REFUSED && cint_ctx_restore(mod, c3.b, c3.n) == CINT_OK,
          "a record type the program does not have is refused");
    CHECK(cint_ctx_change_revision(mod, &k_prog2, &k2_table) == CINT_REFUSED,
          "no change of revision with a module descriptor");
    cint_ctx_destroy(ctx);
    cint_ctx_destroy(mod);
}

/* The interrupt, and the change of revision (H-19b). */
static void test_interrupt_revision(void)
{
    static bytes c, c_old;
    static cint_fault_record rec;
    static uint8_t buf8[8];
    static const cint_state_var z[] = {{"z", 1u, CINT_TAG_Z, 0u, 0u}};
    static const cint_state zs = {1u, 1u, 4u, 0u, k2_init1, z};
    static const cint_state *const zss[] = {&zs};
    static const cint_state_table ztable = {1u, 0u, zss};
    cint_ctx *ctx = k_ctx(&k_prog, NULL);
    cint_checkpoint_info info;
    cint_checkpoint_global g;
    cint_buffer_id id = 0u;
    size_t cur = 0u;
    uint8_t h[32], h2[32];
    char text[64];
    if (ctx == NULL || cint_ctx_bind_state(ctx, &k_table) != CINT_OK) {
        CHECK(0, "a bound context");
        cint_ctx_destroy(ctx);
        return;
    }
    cint_ctx_interrupt(ctx);
    CHECK(cint_rt_entry_open(ctx, K_OP, CINT_FUEL_UNBOUNDED, 8) == CINT_OK, "an entry");
    cint_rt_interrupt_poll(ctx);
    CHECK(cint_fuel_charge(ctx, K_FUEL, 1u), "an interrupt before the entry began is cleared");
    cint_ctx_interrupt(ctx);
    cint_rt_interrupt_poll(ctx);
    CHECK(cint_fuel_charge(ctx, K_FUEL, 0u) && !cint_fuel_charge(ctx, K_FUEL, 1u) &&
              cint_rt_entry_end(ctx) == CINT_FAULT,
          "an interrupt faults E_FUEL at the next charge of one unit");
    CHECK(cint_ctx_fault(ctx, &rec) == CINT_OK && rec.code == CINT_E_FUEL &&
              cint_tvalue_render(&rec.limit, text, sizeof text) > 0u && strcmp(text, "I64 1") == 0,
          "the record holds the allowance in force");
    cint_ctx_interrupt(NULL);
    CHECK(cint_ctx_clear_fault(ctx) == CINT_OK, "clear");

    {
        uint8_t *block = (uint8_t *)cint_rt_state(ctx, &k_state0);
        int64_t v = 42;
        if (block != NULL) {
            memcpy(block, &v, 8u);
        }
    }
    CHECK(cint_buffer_register_bytes(ctx, buf8, 8, CINT_VIEW_READ, &id) == CINT_OK, "a registration");
    CHECK(take(ctx, &c_old) == CINT_OK, "a checkpoint of the first revision");
    hash_of(ctx, h);
    CHECK(cint_ctx_change_revision(ctx, &k_prog_seed, &k2_table) == CINT_REFUSED &&
              cint_ctx_change_revision(ctx, &k_prog2, &ztable) == CINT_REFUSED &&
              cint_ctx_change_revision(ctx, NULL, &k2_table) == CINT_REFUSED,
          "a program without a revision identity or a table that does not fit is refused");
    hash_of(ctx, h2);
    CHECK(memcmp(h, h2, 32u) == 0, "a refused change leaves the context unchanged");
    CHECK(cint_ctx_change_revision(ctx, &k_prog2, &k2_table) == CINT_OK, "change of revision");
    CHECK(take(ctx, &c) == CINT_OK && cint_checkpoint_decode(c.b, c.n, &info) == CINT_OK &&
              memcmp(info.revision, k_rev2, 32u) == 0 && info.global_count == 4u && info.buffer_count == 1u &&
              info.next_buffer == 2u && info.entries == 1u,
          "the new revision keeps the registry and the counters");
    CHECK(cint_checkpoint_global_next(&info, &cur, &g) == CINT_OK && memcmp(g.name.bytes, "app.main.count", 14u) == 0 &&
              cint_tvalue_render(&g.value, text, sizeof text) > 0u && strcmp(text, "I64 42") == 0,
          "count keeps its value");
    CHECK(cint_checkpoint_global_next(&info, &cur, &g) == CINT_OK && cint_tvalue_render(&g.value, text, sizeof text) > 0u &&
              strcmp(text, "U8 77") == 0,
          "flag, retyped, starts at its initial value");
    CHECK(cint_checkpoint_global_next(&info, &cur, &g) == CINT_OK && cint_tvalue_render(&g.value, text, sizeof text) > 0u &&
              strcmp(text, "I8 -3") == 0,
          "a keeps its value at its new offset");
    CHECK(cint_checkpoint_global_next(&info, &cur, &g) == CINT_OK && g.name.len == 5u &&
              memcmp(g.name.bytes, "lib.b", 5u) == 0 && cint_tvalue_render(&g.value, text, sizeof text) > 0u &&
              strcmp(text, "I16 300") == 0 && cint_checkpoint_global_next(&info, &cur, &g) == CINT_REFUSED,
          "b starts at its initial value and total is dropped");
    CHECK(cint_buffer_release(ctx, id) == CINT_OK, "the registration is still live");
    CHECK(cint_ctx_restore(ctx, c_old.b, c_old.n) == CINT_REFUSED,
          "case 29: a checkpoint of the first revision does not restore into the second");
    CHECK(cint_ctx_restore(ctx, c.b, c.n) == CINT_OK, "a checkpoint of the second does");
    cint_ctx_destroy(ctx);
}

void test_state(long *pass, long *fail)
{
    test_layout();
    test_restore();
    test_faulted();
    test_interrupt_revision();
    *pass += s_pass;
    *fail += s_fail;
}
