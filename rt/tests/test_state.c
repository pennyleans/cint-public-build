/* test_state.c: checkpoints of a context (rt/cint_state.h; roadmap box 13, units 4 and 8a),
 * linked into test_rt. The layout of the canonical state and of CINT-CKPT-1 is built here
 * byte by byte from rt/cint_state.c's header comment and compared with what the runtime
 * writes; then restore (SPEC-03 H-19a, M-11, M-11a; section 9 cases 29, 29a and 29b), the
 * fault record in a checkpoint, the busy flag, the decoder and its text, the counters, the
 * interrupt, the change of revision (H-19b), arenas and pools (rt/OPEN.md RT-OQ-46),
 * module-level arrays (RT-OQ-49) and structs (RT-OQ-51), and a seeded fuzz target over the
 * decoder and restore (OQ-14). */
#include "cint_state.h"
#include "cint_mem.h"
#include "cint_rt_internal.h"

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
static const cint_state_var k_vars0[] = {{"count", 5u, CINT_TAG_I64, 0u, 0u, NULL, NULL}, {"flag", 4u, CINT_TAG_BOOL, 8u, 0u, NULL, NULL}};
static const cint_state_var k_vars1[] = {{"total", 5u, CINT_TAG_U32, 0u, 0u, NULL, NULL}, {"a", 1u, CINT_TAG_I8, 4u, 0u, NULL, NULL}};
static const cint_state k_state0 = {0u, 2u, 16u, 0u, k_init0, k_vars0};
static const cint_state k_state0_other = {0u, 2u, 16u, 0u, k_init0, k_vars0};
static const cint_state k_state1 = {1u, 2u, 8u, 0u, k_init1, k_vars1};
static const cint_state *const k_states[] = {&k_state0, &k_state1};
static const cint_state_table k_table = {2u, 0u, k_states, NULL};

/* The second revision: count kept, flag retyped to U8 (77), total dropped, a moved to offset
 * 0, b I16 new (300). */
static const uint8_t k2_init0[16] = {9, 0, 0, 0, 0, 0, 0, 0, 77, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t k2_init1[4] = {0x80, 0, 0x2c, 0x01};
static const cint_state_var k2_vars0[] = {{"count", 5u, CINT_TAG_I64, 0u, 0u, NULL, NULL}, {"flag", 4u, CINT_TAG_U8, 8u, 0u, NULL, NULL}};
static const cint_state_var k2_vars1[] = {{"a", 1u, CINT_TAG_I8, 0u, 0u, NULL, NULL}, {"b", 1u, CINT_TAG_I16, 2u, 0u, NULL, NULL}};
static const cint_state k2_state0 = {0u, 2u, 16u, 0u, k2_init0, k2_vars0};
static const cint_state k2_state1 = {1u, 2u, 4u, 0u, k2_init1, k2_vars1};
static const cint_state *const k2_states[] = {&k2_state0, &k2_state1};
static const cint_state_table k2_table = {2u, 0u, k2_states, NULL};

/* Tables that do not fit k_prog. */
static const cint_state_var k_var_z[] = {{"z", 1u, CINT_TAG_Z, 0u, 0u, NULL, NULL}};
static const cint_state_var k_var_out[] = {{"w", 1u, CINT_TAG_U64, 12u, 0u, NULL, NULL}};
static const cint_state_var k_var_twice[] = {{"count", 5u, CINT_TAG_I64, 0u, 0u, NULL, NULL}, {"count", 5u, CINT_TAG_U8, 8u, 0u, NULL, NULL}};
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
static int g_fail_at;  /* the allocation from now that fails, counted down; 0 for none */
static void *k_alloc(void *user, size_t n, size_t align)
{
    (void)user;
    (void)align;
    return g_fail_alloc || (g_fail_at > 0 && --g_fail_at == 0) ? NULL : malloc(n);
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
    static const cint_state_table bad[] = {{2u, 0u, twice, NULL}, {1u, 0u, out_of_range, NULL}, {1u, 0u, z, NULL},
                                           {1u, 0u, outside, NULL}, {1u, 0u, named_twice, NULL},
                                           {1u, 1u, k_states, NULL}};
    static const cint_state_table lib_table = {1u, 0u, lib, NULL};
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
    static const cint_state_var z[] = {{"z", 1u, CINT_TAG_Z, 0u, 0u, NULL, NULL}};
    static const cint_state zs = {1u, 1u, 4u, 0u, k2_init1, z};
    static const cint_state *const zss[] = {&zs};
    static const cint_state_table ztable = {1u, 0u, zss, NULL};
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

/* ------------------------------------------------------------------------- */
/* Arenas and pools (SPEC-03 M-3, M-11, M-13 to M-20; rt/OPEN.md RT-OQ-46).   */

/* Element types as a checkpoint writes them after the width (rt/cint_state.c; decision
 * 2026-10-08 on OQ-244): k.Pair(a: U64, b: I64), I32 and U16. */
static const uint8_t k_pair[] = {1, 0, 0, 0, 0x71, 6, 0, 0, 0, 'k', '.', 'P', 'a', 'i', 'r', 0, 2, 0, 0, 0,
                                 1, 0, 0, 0, 'a', 0, 0x24, 1, 0, 0, 0, 'b', 0, 0x14, 0x71, 0, 0, 0, 0};
static const uint32_t k_pair_layout[] = {16u, 0u, 8u};
static const uint8_t k_i32[] = {0, 0, 0, 0, 0x13}, k_u16[] = {0, 0, 0, 0, 0x22};

/* Arena 1 of 8 Pair elements of 16 bytes, pool 1 of 4 I32, arena 2 of 3 U16. */
static const cint_mem_decl k_decls[] = {
    {CINT_HANDLE_ARENA, 16u, 8, k_pair, k_pair_layout, (uint32_t)sizeof k_pair, 3u, 0u, 0u, NULL},
    {CINT_HANDLE_POOL, 4u, 4, k_i32, NULL, 5u, 0u, 0u, 0u, NULL},
    {CINT_HANDLE_ARENA, 2u, 3, k_u16, NULL, 5u, 0u, 0u, 0u, NULL}};
static const cint_mem_table k_mem = {3u, 0u, k_decls};
static const cint_mem_decl k_decls_other[] = {
    {CINT_HANDLE_ARENA, 16u, 9, k_pair, k_pair_layout, (uint32_t)sizeof k_pair, 3u, 0u, 0u, NULL},
    {CINT_HANDLE_POOL, 4u, 4, k_i32, NULL, 5u, 0u, 0u, 0u, NULL},
    {CINT_HANDLE_ARENA, 2u, 3, k_u16, NULL, 5u, 0u, 0u, 0u, NULL}};
static const cint_mem_table k_mem_other = {3u, 0u, k_decls_other};

/* The state of k_prog at its initial globals with the arena and pool blocks given (each a
 * U64 length and its bytes) and the counters. */
static void km_state_bytes(bytes *x, const bytes *arenas, const bytes *pools, uint64_t next_arena, uint64_t next_pool)
{
    k_state_bytes(x, 5, 1u, 7u, -3, NULL, 1u, 0u, NULL, 0);
    x->n = find(x, 0u, "lib.total") + 9u + 5u;  /* through the last global */
    b_u(x, arenas->n, 8u);
    b_raw(x, arenas->b, arenas->n);
    b_u(x, pools->n, 8u);
    b_raw(x, pools->b, pools->n);
    b_u(x, 0u, 8u);
    b_u(x, 1u, 8u);
    b_u(x, next_arena, 8u);
    b_u(x, next_pool, 8u);
    b_u(x, 0u, 8u);
    b_s(x, "fuel-v1");
    b_u(x, 0u, 1u);
    b_u(x, 0u, 8u);
}

static void b_arena(bytes *x, uint32_t id, uint32_t parent, uint8_t retired, int64_t offset, int64_t capacity,
                    int64_t used, uint64_t generation)
{
    b_u(x, id, 4u);
    b_u(x, parent, 4u);
    b_u(x, retired, 1u);
    b_u(x, (uint64_t)offset, 8u);
    b_u(x, (uint64_t)capacity, 8u);
    b_u(x, (uint64_t)used, 8u);
    b_u(x, generation, 8u);
}

static void b_slot(bytes *x, uint64_t generation, uint8_t mark, uint64_t below)
{
    b_u(x, generation, 8u);
    b_u(x, mark, 1u);
    b_u(x, below, 8u);
}

/* The address of element 0 of an allocation of arena `arena`, or NULL with the context
 * faulted. */
static uint8_t *km_at(cint_ctx *ctx, uint32_t arena, cint_handle h, uint32_t size)
{
    cint_vdesc v;
    cint_lease lease;
    if (!cint_mem_view(ctx, K_OP, &k_mem, arena, h, &v, &lease)) {
        return NULL;
    }
    return (uint8_t *)v.base + (size_t)v.origin * size;
}

/* An entry whose one operation, op(ctx), faults: the record's code, operation and last
 * operand (the M-17 reason of a stale handle) as text; the fault is then cleared. */
typedef bool (*km_op)(cint_ctx *ctx);
static void km_fault(cint_ctx *ctx, km_op op, char *out, size_t cap)
{
    cint_fault_record rec;
    char reason[32] = "none";
    out[0] = '\0';
    if (cint_rt_entry_open(ctx, K_OP, 10, 8) != CINT_OK) {
        return;
    }
    if (!op(ctx) && cint_rt_entry_end(ctx) == CINT_FAULT && cint_ctx_fault(ctx, &rec) == CINT_OK) {
        if (rec.operand_count > 0u) {
            (void)cint_tvalue_render(&rec.operands[rec.operand_count - 1u], reason, sizeof reason);
        }
        snprintf(out, cap, "%u %.*s %s", (unsigned)rec.code, (int)rec.operation_len, rec.operation, reason);
        (void)cint_ctx_clear_fault(ctx);
    } else if (cint_rt_entry_end(ctx) != CINT_OK) {
        (void)cint_ctx_clear_fault(ctx);
    }
}

static cint_handle g_handle;
static bool km_view_a1(cint_ctx *ctx)
{
    return km_at(ctx, 1u, g_handle, 16u) != NULL;
}
static bool km_alloc_a3(cint_ctx *ctx)
{
    cint_handle h;
    return cint_mem_alloc(ctx, K_OP, &k_mem, 3u, 1, false, &h, NULL);
}
static bool km_alloc_a4(cint_ctx *ctx)
{
    cint_handle h;
    return cint_mem_alloc(ctx, K_OP, &k_mem, 4u, 0, false, &h, NULL);
}
static bool km_alloc_other(cint_ctx *ctx)
{
    cint_handle h;
    return cint_mem_alloc(ctx, K_OP, &k_mem_other, 1u, 1, false, &h, NULL);
}
static bool km_alloc_null(cint_ctx *ctx)
{
    cint_handle h;
    return cint_mem_alloc(ctx, K_OP, NULL, 1u, 1, false, &h, NULL);
}
static bool km_deref_p1(cint_ctx *ctx)
{
    return cint_mem_deref(ctx, K_OP, &k_mem, 1u, g_handle) != NULL;
}

/* Fresh content, the layout, restore, the decoder and its text, a retired child, a retired
 * slot, and the table check. */
static void test_mem(void)
{
    static bytes c1, c2, c3, state, want, arenas, pools, tmp;
    static char text[8192], line[512], fault[128];
    cint_ctx *ctx = k_ctx(&k_prog, NULL), *fresh = k_ctx(&k_prog, NULL), *other = k_ctx(&k_prog, NULL);
    cint_ctx *blank = k_ctx(&k_prog, NULL);
    cint_handle h1, h2, h3, hp[3], late, first;
    cint_checkpoint_info info;
    cint_checkpoint_arena a;
    cint_checkpoint_pool p;
    uint8_t *e = NULL, h[32], hc1[32], d[32];
    uint8_t one[96];
    uint32_t child = 0u, child2 = 0u;
    uint16_t tag = 9u;
    int32_t v;
    size_t cur = 0u, n, accepted = 0u;
    char sh[65];
    int ok = 1;
    memset(&h1, 0, sizeof h1);
    h2 = h3 = late = h1;
    first = h1;
    first.length = 2;
    first.container = 1u;
    first.kind = CINT_HANDLE_ARENA;
    memset(hp, 0, sizeof hp);
    if (ctx == NULL || fresh == NULL || other == NULL || blank == NULL || cint_ctx_bind_state(ctx, &k_table) != CINT_OK ||
        cint_ctx_bind_state(fresh, &k_table) != CINT_OK || cint_ctx_bind_state(other, &k_table) != CINT_OK ||
        cint_ctx_bind_state(blank, &k_table) != CINT_OK) {
        CHECK(0, "bound contexts");
        cint_ctx_destroy(ctx);
        cint_ctx_destroy(fresh);
        cint_ctx_destroy(other);
        cint_ctx_destroy(blank);
        return;
    }

    /* Content: an allocation of arena 1, its child 3 with one, one of arena 2, and a pool
     * whose slots 1 then 0 were removed. */
    CHECK(cint_mem_alloc(ctx, K_OP, &k_mem, 1u, 2, false, &h1, NULL) && (e = km_at(ctx, 1u, h1, 16u)) != NULL,
          "arena 1: two elements");
    for (uint8_t i = 0u; e != NULL && i < 32u; i++) {
        e[i] = (uint8_t)(i + 1u);
    }
    CHECK(cint_mem_child(ctx, K_OP, &k_mem, 1u, 4, &child) && child == 3u, "child 3 of four elements");
    e = NULL;
    CHECK(cint_mem_alloc(ctx, K_OP, &k_mem, 3u, 1, false, &h3, NULL) && (e = km_at(ctx, 3u, h3, 16u)) != NULL,
          "arena 3: one element");
    for (uint8_t i = 0u; e != NULL && i < 16u; i++) {
        e[i] = (uint8_t)(0xc0u + i);
    }
    e = NULL;
    CHECK(cint_mem_alloc(ctx, K_OP, &k_mem, 2u, 1, false, &h2, NULL) && (e = km_at(ctx, 2u, h2, 2u)) != NULL,
          "arena 2: one element");
    if (e != NULL) {
        e[0] = 0x34u;
        e[1] = 0x12u;
    }
    for (int32_t i = 0; i < 3; i++) {
        v = 10 * (i + 1);
        ok &= cint_mem_insert(ctx, K_OP, &k_mem, 1u, &v, false, &hp[i], NULL);
    }
    CHECK(ok && cint_mem_remove(ctx, K_OP, &k_mem, 1u, hp[1]) && cint_mem_remove(ctx, K_OP, &k_mem, 1u, hp[0]),
          "three insertions, two removals");
    CHECK(take(ctx, &c1) == CINT_OK, "checkpoint c1");
    hash_of(ctx, hc1);

    /* The layout (RT-OQ-46). */
    arenas.n = 0u;
    b_s(&arenas, "cint-core-1/arena/v2");
    b_u(&arenas, 3u, 8u);
    b_arena(&arenas, 1u, 0u, 0u, 0, 8, 6, 0u);
    b_arena(&arenas, 2u, 0u, 0u, 0, 3, 1, 0u);
    b_arena(&arenas, 3u, 1u, 0u, 2, 4, 1, 0u);
    memset(one, 0, sizeof one);
    for (uint8_t i = 0u; i < 32u; i++) {
        one[i] = (uint8_t)(i + 1u);
    }
    for (uint8_t i = 0u; i < 16u; i++) {
        one[32u + i] = (uint8_t)(0xc0u + i);
    }
    b_u(&arenas, 16u, 4u);
    b_raw(&arenas, k_pair, sizeof k_pair);
    b_raw(&arenas, one, 96u);
    b_u(&arenas, 2u, 4u);
    b_raw(&arenas, k_u16, 5u);
    b_u(&arenas, 0x1234u, 2u);
    pools.n = 0u;
    b_s(&pools, "cint-core-1/pool/v2");
    b_u(&pools, 1u, 8u);
    b_u(&pools, 1u, 4u);
    b_u(&pools, 4u, 4u);
    b_raw(&pools, k_i32, 5u);
    b_u(&pools, 4u, 8u);
    b_u(&pools, 3u, 8u);
    b_u(&pools, 1u, 8u);  /* slot 0 is the top */
    b_slot(&pools, 1u, CINT_SLOT_VACANT, 2u);
    b_slot(&pools, 1u, CINT_SLOT_VACANT, 0u);
    b_slot(&pools, 0u, CINT_SLOT_OCCUPIED, 0u);
    b_u(&pools, 30u, 4u);
    km_state_bytes(&state, &arenas, &pools, 4u, 2u);
    k_checkpoint_bytes(&want, &state);
    CHECK(c1.n == want.n && memcmp(c1.b, want.b, want.n) == 0, "the bytes of the arena and pool blocks");
    cint_sha256(state.b, state.n, d);
    CHECK(memcmp(hc1, d, 32u) == 0, "the state hash covers them");

    /* The decoder and the text of show (SPEC-06 6.1). */
    CHECK(cint_checkpoint_decode(c1.b, c1.n, &info) == CINT_OK && info.next_arena == 4u && info.next_pool == 2u,
          "decode c1");
    CHECK(cint_checkpoint_arena_next(&info, &cur, &a) == CINT_OK && a.id == 1u && a.parent == 0u &&
              a.elem_bytes == 16u && a.capacity == 8 && a.used == 6 && a.elements != NULL &&
              memcmp(a.elements, one, 96u) == 0 && a.type_len == sizeof k_pair && memcmp(a.type, k_pair, a.type_len) == 0,
          "arena 1, its element type and its elements");
    CHECK(cint_checkpoint_arena_next(&info, &cur, &a) == CINT_OK && a.id == 2u && a.elements != NULL &&
              a.elem_bytes == 2u && a.elements[0] == 0x34u && a.elements[1] == 0x12u && a.type_len == 5u &&
              a.type[4] == 0x22u,
          "arena 2 and its elements");
    CHECK(cint_checkpoint_arena_next(&info, &cur, &a) == CINT_OK && a.id == 3u && a.parent == 1u && a.offset == 2 &&
              a.capacity == 4 && a.used == 1 && a.retired == 0u && a.elements == NULL && a.type == NULL &&
              cint_checkpoint_arena_next(&info, &cur, &a) == CINT_REFUSED,
          "child 3, whose type and elements are its parent's, and no fourth");
    cur = 0u;
    CHECK(cint_checkpoint_pool_next(&info, &cur, &p) == CINT_OK && p.id == 1u && p.used == 3u && p.top == 1u &&
              p.occupied == 1u && p.retired == 0u && p.vacant == 2u && p.elements != NULL && p.elements[0] == 30u &&
              p.elem_bytes == 4u && p.type_len == 5u && p.type[4] == 0x13u &&
              cint_checkpoint_pool_next(&info, &cur, &p) == CINT_REFUSED,
          "pool 1");
    CHECK(cint_checkpoint_render(&info, text, sizeof text) < sizeof text, "the text of show");
    cint_sha256(one, 96u, h);
    hex(sh, h, 32u);
    snprintf(line, sizeof line,
             "arena-block 3\narena 1 parent 0 element-type k.Pair live offset 0 capacity 8 used 6 generation 0 "
             "elements-sha256 %s\n",
             sh);
    CHECK(strstr(text, line) != NULL, "show: arena 1");
    CHECK(strstr(text, "arena 2 parent 0 element-type U16 live offset 0 capacity 3 used 1 generation 0 ") != NULL &&
              strstr(text, "arena 3 parent 1 live offset 2 capacity 4 used 1 generation 0\npool-block 1\n"
                           "pool 1 element-type I32 capacity 4 used 3 occupied 1 retired 0 elements-sha256 ") != NULL &&
              strstr(text, "pool-slot 1 0 generation 1 vacant\npool-slot 1 1 generation 1 vacant\n"
                           "pool-slot 1 2 generation 0 occupied\npool-free 1 0 1\ncounters next-buffer 1 "
                           "next-arena 4 next-pool 2") != NULL,
          "show: child 3, pool 1, its slots and its free list from the top");

    /* Changes after c1: a reset of arena 1 retires child 3; child 4; the pool refilled. */
    CHECK(cint_mem_reset(ctx, K_OP, &k_mem, 1u) && cint_mem_child(ctx, K_OP, &k_mem, 1u, 2, &child2) &&
              child2 == 4u && cint_mem_alloc(ctx, K_OP, &k_mem, 1u, 1, false, &late, NULL),
          "reset, child 4, an allocation at generation 1");
    v = 40;
    CHECK(cint_mem_insert(ctx, K_OP, &k_mem, 1u, &v, false, &hp[0], NULL) && hp[0].slot == 0u &&
              hp[0].generation == 1u,
          "the pool's top slot is reused");
    km_fault(ctx, km_alloc_a3, fault, sizeof fault);
    CHECK(strcmp(fault, "8 arena.alloc U8 2") == 0, "child 3 is retired");

    /* Restore c1 (M-11): contents and generations come back; identifiers stay issued. */
    CHECK(cint_ctx_restore(ctx, c1.b, c1.n) == CINT_OK, "restore c1");
    hash_of(ctx, h);
    CHECK(take(ctx, &c2) == CINT_OK && cint_checkpoint_decode(c2.b, c2.n, &info) == CINT_OK &&
              info.next_arena == 5u && c2.n == c1.n,
          "the arena counter is the greater of the two");
    memcpy(tmp.b, c1.b, c1.n);
    tmp.n = c1.n;
    tmp.b[tmp.n - 8u - 1u - 11u - 8u - 8u - 8u] = 5u;  /* next-arena 5 */
    CHECK(c2.n == tmp.n && memcmp(c2.b, tmp.b, tmp.n) == 0, "and the state is c1's otherwise");
    CHECK((e = km_at(ctx, 1u, h1, 16u)) != NULL && memcmp(e, one, 32u) == 0 &&
              (e = km_at(ctx, 3u, h3, 16u)) != NULL && memcmp(e, one + 32u, 16u) == 0 &&
              (e = km_at(ctx, 2u, h2, 2u)) != NULL && e[0] == 0x34u && e[1] == 0x12u,
          "the handles of c1 are valid with their elements");
    g_handle = late;
    km_fault(ctx, km_view_a1, fault, sizeof fault);
    CHECK(strcmp(fault, "8 handle.check U8 3") == 0, "a handle made after c1 is of another generation");
    km_fault(ctx, km_alloc_a4, fault, sizeof fault);
    CHECK(strcmp(fault, "10 dispatch.admit none") == 0, "identifier 4 names no arena");
    CHECK(cint_mem_child(ctx, K_OP, &k_mem, 3u, 1, &child2) && child2 == 5u, "the next child is 5, not 4");
    CHECK(cint_mem_deref(ctx, K_OP, &k_mem, 1u, hp[2]) != NULL &&
              memcmp(cint_mem_deref(ctx, K_OP, &k_mem, 1u, hp[2]), "\x1e\0\0\0", 4u) == 0,
          "the occupied slot");
    {
        cint_handle q[3];
        v = 50;
        CHECK(cint_mem_insert(ctx, K_OP, &k_mem, 1u, &v, false, &q[0], NULL) && q[0].slot == 0u &&
                  q[0].generation == 1u && cint_mem_insert(ctx, K_OP, &k_mem, 1u, &v, false, &q[1], NULL) &&
                  q[1].slot == 1u && cint_mem_insert(ctx, K_OP, &k_mem, 1u, &v, false, &q[2], NULL) &&
                  q[2].slot == 3u && cint_mem_insert(ctx, K_OP, &k_mem, 1u, &v, true, &q[2], &tag) && tag == 1u,
              "the free list comes back last in, first out, then the fresh slot, then full");
    }

    /* A retired child and a retired slot in a checkpoint. */
    CHECK(cint_ctx_restore(ctx, c1.b, c1.n) == CINT_OK && cint_mem_reset(ctx, K_OP, &k_mem, 1u) &&
              take(ctx, &c3) == CINT_OK && cint_checkpoint_decode(c3.b, c3.n, &info) == CINT_OK,
          "a checkpoint after a reset");
    cur = 0u;
    (void)cint_checkpoint_arena_next(&info, &cur, &a);
    CHECK(a.generation == 1u && a.used == 0, "arena 1 at generation 1, empty");
    (void)cint_checkpoint_arena_next(&info, &cur, &a);
    (void)cint_checkpoint_arena_next(&info, &cur, &a);
    CHECK(a.id == 3u && a.retired == 1u, "child 3 is retired");
    CHECK(cint_ctx_restore(fresh, c3.b, c3.n) == CINT_OK && take(fresh, &c2) == CINT_OK && c2.n == c3.n &&
              memcmp(c2.b, c3.b, c3.n) == 0,
          "it restores into a fresh context and checkpoints to the same bytes");
    km_fault(fresh, km_alloc_a3, fault, sizeof fault);
    CHECK(strcmp(fault, "8 arena.alloc U8 2") == 0, "and child 3 is still retired");
    memcpy(tmp.b, c1.b, c1.n);
    tmp.n = c1.n;
    n = find(&tmp, 0u, "cint-core-1/pool/v2") + 19u + 8u + 4u + 9u + 8u + 8u + 8u;
    tmp.b[n + 9u] = 0u;  /* slot 0 at the bottom of the list */
    for (size_t i = 0u; i < 8u; i++) {
        tmp.b[n + 17u + i] = i == 0u ? 0xfeu : 0xffu;  /* slot 1 at 2^64 - 2, retired */
    }
    tmp.b[n + 17u + 8u] = CINT_SLOT_RETIRED;
    CHECK(cint_ctx_restore(ctx, tmp.b, tmp.n) == CINT_OK, "a retired slot restores");
    {
        cint_handle q[2];
        v = 60;
        CHECK(cint_mem_insert(ctx, K_OP, &k_mem, 1u, &v, false, &q[0], NULL) && q[0].slot == 0u &&
                  cint_mem_insert(ctx, K_OP, &k_mem, 1u, &v, false, &q[1], NULL) && q[1].slot == 3u &&
                  cint_mem_insert(ctx, K_OP, &k_mem, 1u, &v, true, &q[1], &tag) && tag == 1u,
              "and is never reused (M-16)");
        g_handle = hp[1];
        km_fault(ctx, km_deref_p1, fault, sizeof fault);
        CHECK(strcmp(fault, "8 pool.deref U8 2") == 0, "its handles are retired");
    }

    /* The table (RT-OQ-46): compared at restore once the context has named one, else at the
     * first arena or pool operation. */
    CHECK(cint_mem_alloc(other, K_OP, &k_mem_other, 1u, 1, false, &h1, NULL), "another table");
    hash_of(other, h);
    CHECK(cint_ctx_restore(other, c1.b, c1.n) == CINT_REFUSED, "a context with another table refuses c1");
    hash_of(other, d);
    CHECK(memcmp(h, d, 32u) == 0, "unchanged");
    CHECK(cint_ctx_restore(blank, c1.b, c1.n) == CINT_OK, "a context that has named no table restores c1");
    km_fault(blank, km_alloc_other, fault, sizeof fault);
    CHECK(strcmp(fault, "10 dispatch.admit none") == 0, "and refuses another table at the first operation");
    km_fault(blank, km_alloc_null, fault, sizeof fault);
    CHECK(strcmp(fault, "10 dispatch.admit none") == 0, "and a NULL table, which fits none");
    CHECK((e = km_at(blank, 1u, first, 16u)) != NULL && memcmp(e, one, 32u) == 0,
          "its own table takes the restored arenas");

    /* H-19a: malformed blocks are refused with the context unchanged, or restore. */
    hash_of(ctx, h);
    {
        size_t lo = find(&c1, 0u, "cint-core-1/arena/v2") - 12u, hi = find(&c1, 0u, "fuel-v1");
        for (n = lo; n < hi; n++) {
            static const uint8_t masks[3] = {0x01u, 0x80u, 0xffu};
            for (size_t k = 0u; k < 3u; k++) {
                cint_status st;
                memcpy(tmp.b, c1.b, c1.n);
                tmp.b[n] ^= masks[k];
                st = cint_ctx_restore(ctx, tmp.b, c1.n);
                if (st == CINT_OK) {  /* the same blocks come back out (canonical) */
                    accepted++;
                    (void)cint_checkpoint_decode(tmp.b, c1.n, &info);
                    (void)cint_checkpoint_render(&info, text, sizeof text);
                    ok &= take(ctx, &c2) == CINT_OK && c2.n == c1.n && memcmp(c2.b + lo, tmp.b + lo, hi - 44u - lo) == 0;
                    ok &= cint_ctx_restore(ctx, c3.b, c3.n) == CINT_OK;
                    hash_of(ctx, h);
                } else {
                    hash_of(ctx, d);
                    ok &= st == CINT_REFUSED && memcmp(h, d, 32u) == 0;
                }
            }
        }
    }
    CHECK(ok && accepted > 0u, "a changed byte of the blocks is refused with the context unchanged, or restores");
    memcpy(tmp.b, c1.b, c1.n);
    n = find(&tmp, 0u, "cint-core-1/pool/v2") + 19u + 8u + 4u + 9u + 8u + 8u + 8u;
    tmp.b[n + 9u] = 1u;  /* slot 0 below itself */
    CHECK(cint_checkpoint_decode(tmp.b, c1.n, &info) == CINT_REFUSED, "a free list with a cycle");
    memcpy(tmp.b, c1.b, c1.n);
    tmp.b[n + 9u] = 3u;  /* slot 0 above the occupied slot 2 */
    CHECK(cint_checkpoint_decode(tmp.b, c1.n, &info) == CINT_REFUSED, "a free list through an occupied slot");
    memcpy(tmp.b, c1.b, c1.n);
    n = find(&tmp, 0u, "cint-core-1/arena/v2") + 20u + 8u + 2u * 41u;
    tmp.b[n + 9u] = 3u;  /* child 3 at offset 3: past arena 1's used extent */
    CHECK(cint_checkpoint_decode(tmp.b, c1.n, &info) == CINT_REFUSED, "a live child past its parent's used extent");
    memcpy(tmp.b, c1.b, c1.n);
    tmp.b[n + 8u] = 1u;  /* child 3 retired under a live parent: allowed */
    CHECK(cint_checkpoint_decode(tmp.b, c1.n, &info) == CINT_OK, "a retired child");
    memcpy(tmp.b, c1.b, c1.n);
    tmp.b[c1.n - 8u - 1u - 11u - 8u - 8u - 8u] = 3u;  /* next-arena 3 */
    CHECK(cint_checkpoint_decode(tmp.b, c1.n, &info) == CINT_REFUSED, "an arena counter at an issued identifier");

    /* Memory that runs out at each allocation of a restore, and of the growth to the
     * capacities at the first operation after one: CINT_RESOURCE with the context unchanged,
     * or host.resource with nothing lost. */
    {
        static cint_fault_record rec;
        cint_ctx_config cfg;
        cint_ctx *x = NULL, *y = NULL;
        int failed = 0, grown = 0;
        ok = 1;
        memset(&cfg, 0, sizeof cfg);
        cfg.size = (uint32_t)sizeof cfg;
        cfg.program = &k_prog;
        cfg.allocator.alloc = k_alloc;
        cfg.allocator.release = k_release;
        if (cint_ctx_create(&cfg, &x) == CINT_OK && cint_ctx_bind_state(x, &k_table) == CINT_OK &&
            cint_ctx_create(&cfg, &y) == CINT_OK && cint_ctx_bind_state(y, &k_table) == CINT_OK &&
            cint_mem_alloc(x, K_OP, &k_mem, 2u, 1, false, &h2, NULL)) {
            hash_of(x, h);
            for (int i = 1; i < 60; i++) {
                cint_status st;
                g_fail_at = i;
                st = cint_ctx_restore(x, c1.b, c1.n);
                g_fail_at = 0;
                hash_of(x, d);
            failed += st == CINT_RESOURCE ? 1 : 0;
                ok &= st == CINT_OK ? memcmp(d, hc1, 32u) == 0 : st == CINT_RESOURCE && memcmp(d, h, 32u) == 0;
                memcpy(h, d, 32u);
            }
            for (int i = 1; i < 30; i++) {
                ok &= cint_ctx_restore(y, c1.b, c1.n) == CINT_OK && cint_rt_entry_open(y, K_OP, 10, 8) == CINT_OK;
                g_fail_at = i;
                e = km_at(y, 1u, first, 16u);
                g_fail_at = 0;
                if (e != NULL) {
                    grown++;
                    ok &= memcmp(e, one, 32u) == 0 && cint_rt_entry_end(y) == CINT_OK;
                } else {
                    ok &= cint_rt_entry_end(y) == CINT_FAULT && cint_ctx_fault(y, &rec) == CINT_OK &&
                          rec.code == CINT_E_UNSUPPORTED && cint_ctx_clear_fault(y) == CINT_OK;
                }
            }
        } else {
            ok = 0;
        }
        CHECK(ok && failed > 0 && grown > 0, "memory that runs out in a restore or in the growth after one");
        cint_ctx_destroy(x);
        cint_ctx_destroy(y);
    }
    cint_ctx_destroy(ctx);
    cint_ctx_destroy(fresh);
    cint_ctx_destroy(other);
    cint_ctx_destroy(blank);
}

/* ------------------------------------------------------------------------- */
/* Element types (box 13 unit 8b-3; decision 2026-10-08 on OQ-244).          */

/* k.Body(tag: U8, on: Bool, h: Handle, m: I16[2]) as compiled C stores it, with padding, and
 * in 8-byte cells as the interpreter does; a pool of Bool; and the same with `on` named `ok`. */
typedef struct kt_body {
    uint8_t tag;
    bool on;
    cint_handle h;
    int16_t m[2];
} kt_body;
#define KT_BODY(on)                                                                                             \
    {1, 0, 0, 0, 0x71, 6, 0, 0, 0, 'k', '.', 'B', 'o', 'd', 'y', 0, 4, 0, 0, 0, 3, 0, 0, 0, 't', 'a', 'g', 0, 0x21, \
     2, 0, 0, 0, 'o', on, 0, 0x01, 1, 0, 0, 0, 'h', 0, 0x75, 1, 0, 0, 0, 'm', 0, 0x52, 0x12, 1, 1, 0xff, 0xff,    \
     0xff, 0xff, 2, 0, 0, 0, 0, 0, 0, 0, 0x71, 0, 0, 0, 0}
static const uint8_t kt_type[] = KT_BODY('n'), kt_type_other[] = KT_BODY('k'), kt_bool[] = {0, 0, 0, 0, 0x01};
static const uint32_t kt_layout[] = {(uint32_t)sizeof(kt_body), (uint32_t)offsetof(kt_body, tag),
                                     (uint32_t)offsetof(kt_body, on), (uint32_t)offsetof(kt_body, h),
                                     (uint32_t)offsetof(kt_body, m)};
static const uint32_t kt_cells[] = {64u, 0u, 8u, 16u, 48u};
static const cint_mem_decl kt_decls[] = {
    {CINT_HANDLE_ARENA, (uint32_t)sizeof(kt_body), 4, kt_type, kt_layout, (uint32_t)sizeof kt_type, 5u, 0u, 0u, NULL},
    {CINT_HANDLE_POOL, 1u, 2, kt_bool, NULL, 5u, 0u, 0u, 0u, NULL}};
static const cint_mem_decl kt_decls_cell[] = {
    {CINT_HANDLE_ARENA, 64u, 4, kt_type, kt_cells, (uint32_t)sizeof kt_type, 5u, 8u, 0u, NULL},
    {CINT_HANDLE_POOL, 8u, 2, kt_bool, NULL, 5u, 0u, 8u, 0u, NULL}};
static const cint_mem_decl kt_decls_other[] = {
    {CINT_HANDLE_ARENA, (uint32_t)sizeof(kt_body), 4, kt_type_other, kt_layout, (uint32_t)sizeof kt_type, 5u, 0u, 0u, NULL},
    {CINT_HANDLE_POOL, 1u, 2, kt_bool, NULL, 5u, 0u, 0u, 0u, NULL}};
static const cint_mem_table kt_mem = {2u, 0u, kt_decls}, kt_mem_cell = {2u, 0u, kt_decls_cell},
                           kt_mem_other = {2u, 0u, kt_decls_other};
static const cint_handle kt_null = {0u, 0, 0u, 0u, 0u, {0u, 0u, 0u}};

/* Whether checkpoints x and y are the same but for the entry sequence number, which kt_at moves. */
static bool kt_same(const bytes *x, const bytes *y)
{
    size_t at = find(x, 0u, "fuel-v1");
    return x->n == y->n && at != SIZE_MAX && at >= 12u && memcmp(x->b, y->b, at - 12u) == 0 &&
           memcmp(x->b + at - 4u, y->b + at - 4u, x->n - at + 4u) == 0;
}

/* The first element of arena 1 under table t, or NULL with the fault cleared. */
static uint8_t *kt_at(cint_ctx *ctx, const cint_mem_table *t)
{
    cint_vdesc v;
    cint_lease lease;
    cint_handle h = kt_null;
    h.length = 2;
    h.container = 1u;
    h.kind = CINT_HANDLE_ARENA;
    if (cint_rt_entry_open(ctx, K_OP, 10, 8) != CINT_OK) {
        return NULL;
    }
    if (!cint_mem_view(ctx, K_OP, t, 1u, h, &v, &lease)) {
        (void)cint_rt_entry_end(ctx);
        (void)cint_ctx_clear_fault(ctx);
        return NULL;
    }
    (void)cint_rt_entry_end(ctx);
    return (uint8_t *)v.base;
}

/* Two Body elements and a Bool, as compiled C stores them, written as IM-146 values; read
 * into the interpreter's cells and back to the same bytes; another type, a Bool of 2 and a
 * handle of kind 3 refused (H-19a), held until a table that fits is named. */
static void test_mem_types(void)
{
    static bytes c, c2, tmp;
    static char text[8192];
    static const uint8_t want_cells[64] = {7, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0,
                                           3, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0,
                                           2, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0,
                                           0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 5, 0, 0, 0, 0, 0, 0, 0};
    uint8_t want[70], h[32], d[32], *e;
    cint_ctx *ctx = k_ctx(&k_prog, NULL), *held = k_ctx(&k_prog, NULL), *cell = k_ctx(&k_prog, NULL);
    cint_ctx *other = k_ctx(&k_prog, NULL), *blank = k_ctx(&k_prog, NULL);
    cint_checkpoint_info info;
    cint_checkpoint_arena a;
    cint_checkpoint_pool p;
    cint_handle hb = kt_null, hp = kt_null;
    kt_body *body = NULL;
    size_t cur = 0u, at;
    bool yes = true;
    if (ctx == NULL || held == NULL || cell == NULL || other == NULL || blank == NULL ||
        cint_ctx_bind_state(ctx, &k_table) != CINT_OK || cint_ctx_bind_state(held, &k_table) != CINT_OK ||
        cint_ctx_bind_state(cell, &k_table) != CINT_OK || cint_ctx_bind_state(other, &k_table) != CINT_OK ||
        cint_ctx_bind_state(blank, &k_table) != CINT_OK) {
        CHECK(0, "bound contexts");
        cint_ctx_destroy(ctx);
        cint_ctx_destroy(held);
        cint_ctx_destroy(cell);
        cint_ctx_destroy(other);
        cint_ctx_destroy(blank);
        return;
    }
    CHECK(cint_mem_alloc(ctx, K_OP, &kt_mem, 1u, 2, false, &hb, NULL) && (body = (kt_body *)kt_at(ctx, &kt_mem)) != NULL &&
              cint_mem_insert(ctx, K_OP, &kt_mem, 1u, &yes, false, &hp, NULL),
          "two Body elements and a Bool");
    if (body != NULL) {
        memset(body, 0xaa, 2u * sizeof *body);  /* padding that a checkpoint leaves out */
        body[0].tag = 7u;
        body[0].on = true;
        body[0].h = kt_null;
        body[0].h.slot = 3u;
        body[0].h.length = 1;
        body[0].h.generation = 2u;
        body[0].h.container = 1u;
        body[0].h.kind = CINT_HANDLE_POOL;
        body[0].m[0] = -2;
        body[0].m[1] = 5;
        body[1].tag = 0u;
        body[1].on = false;
        body[1].h = kt_null;
        body[1].m[0] = 0;
        body[1].m[1] = -1;
    }
    CHECK(take(ctx, &c) == CINT_OK && cint_checkpoint_decode(c.b, c.n, &info) == CINT_OK, "a checkpoint");
    memset(want, 0, sizeof want);
    want[0] = 7u;
    want[1] = 1u;
    memcpy(want + 2u, want_cells + 16u, 29u);
    want[31] = 0xfeu;
    want[32] = 0xffu;
    want[33] = 5u;
    want[68] = 0xffu;
    want[69] = 0xffu;
    CHECK(cint_checkpoint_arena_next(&info, &cur, &a) == CINT_OK && a.elem_bytes == 35u && a.used == 2 &&
              memcmp(a.elements, want, 70u) == 0 && a.type_len == sizeof kt_type &&
              memcmp(a.type, kt_type, sizeof kt_type) == 0,
          "each Body is its 35 bytes of IM-146 values, its padding left out");
    cur = 0u;
    CHECK(cint_checkpoint_pool_next(&info, &cur, &p) == CINT_OK && p.elem_bytes == 1u && p.elements[0] == 1u,
          "the Bool in 1 byte");
    CHECK(cint_checkpoint_render(&info, text, sizeof text) < sizeof text &&
              strstr(text, "arena 1 parent 0 element-type k.Body live") != NULL &&
              strstr(text, "pool 1 element-type Bool capacity 2") != NULL,
          "show names the element types");

    /* Into the interpreter's cells: held until the table is named, or at once; back the same. */
    CHECK(cint_ctx_restore(held, c.b, c.n) == CINT_OK && take(held, &c2) == CINT_OK && c2.n == c.n &&
              memcmp(c2.b, c.b, c.n) == 0,
          "a context that has named no table holds the elements and writes them back");
    e = kt_at(held, &kt_mem_cell);
    CHECK(e != NULL && memcmp(e, want_cells, 64u) == 0 && e[64u + 48u] == 0u && e[64u + 56u] == 0xffu &&
              e[64u + 63u] == 0xffu,
          "the cell table lays them out: scalars widened, I16 -2 and -1 sign-extended, the handle whole");
    CHECK(take(held, &c2) == CINT_OK && kt_same(&c2, &c), "and writes the same bytes");
    CHECK(kt_at(cell, &kt_mem_cell) == NULL && cint_ctx_restore(cell, c.b, c.n) == CINT_OK &&
              (e = kt_at(cell, &kt_mem_cell)) != NULL && memcmp(e, want_cells, 64u) == 0 && take(cell, &c2) == CINT_OK &&
              kt_same(&c2, &c),
          "a context that named the cell table lays them out at the restore");
    CHECK(cint_ctx_restore(ctx, c2.b, c2.n) == CINT_OK && (body = (kt_body *)kt_at(ctx, &kt_mem)) != NULL &&
              body[0].tag == 7u && body[0].on && body[0].h.slot == 3u && body[0].h.kind == CINT_HANDLE_POOL &&
              body[0].h.reserved[0] == 0u && body[0].m[0] == -2 && body[1].m[1] == -1 && body[1].h.kind == 0u,
          "and compiled C reads them back");

    /* Another type, a Bool of 2, a handle of kind 3. */
    (void)kt_at(other, &kt_mem_other);
    hash_of(other, h);
    CHECK(cint_ctx_restore(other, c.b, c.n) == CINT_REFUSED, "a context of another element type refuses them");
    hash_of(other, d);
    CHECK(memcmp(h, d, 32u) == 0, "unchanged");
    at = find(&c, 0u, "cint-core-1/arena/v2") + 20u + 8u + 41u + 4u + sizeof kt_type;  /* element 0 */
    memcpy(tmp.b, c.b, c.n);
    tmp.n = c.n;
    tmp.b[at + 1u] = 2u;
    CHECK(cint_checkpoint_decode(tmp.b, tmp.n, &info) == CINT_OK && cint_ctx_restore(ctx, tmp.b, tmp.n) == CINT_REFUSED,
          "a Bool of 2 decodes without the program and is refused with it");
    CHECK(cint_ctx_restore(blank, tmp.b, tmp.n) == CINT_OK && take(blank, &c2) == CINT_OK && c2.n == tmp.n &&
              memcmp(c2.b, tmp.b, tmp.n) == 0 && kt_at(blank, &kt_mem) == NULL,
          "held, it is written back as read and refused at the first operation");
    CHECK(cint_ctx_restore(blank, c.b, c.n) == CINT_OK && kt_at(blank, &kt_mem_other) == NULL &&
              kt_at(blank, &kt_mem) != NULL,
          "held elements refuse a table of another type, then take their own");
    tmp.b[at + 1u] = 1u;
    tmp.b[at + 30u] = 3u;
    CHECK(cint_ctx_restore(ctx, tmp.b, tmp.n) == CINT_REFUSED, "a handle of kind 3 is refused");
    tmp.b[at + 30u] = 0u;
    CHECK(cint_ctx_restore(ctx, tmp.b, tmp.n) == CINT_REFUSED, "and of kind 0 with other bytes set");
    cint_ctx_destroy(ctx);
    cint_ctx_destroy(held);
    cint_ctx_destroy(cell);
    cint_ctx_destroy(other);
    cint_ctx_destroy(blank);
}

/* ------------------------------------------------------------------------- */
/* Module-level arrays (rt/OPEN.md RT-OQ-49): a global of tag 51 (SPEC-01 IM-146, IM-148).  */

/* app/main.ci: count I64 at 0 (5), xs I64[3] at 8 ([1, -2, 3]); lib.ci: flags Bool[2] at 0
 * ([true, false]), m I16[2, 3] at 2 ([1, 2, 3, 4, 5, -6]), none U8[0] at 14. */
static const int64_t ka_three[] = {3}, ka_four[] = {4}, ka_two[] = {2}, ka_m[] = {2, 3}, ka_m2[] = {3, 2},
                     ka_zero[] = {0}, ka_neg[] = {-1}, ka_rank5[] = {1, 1, 1, 1, 1};
static const uint8_t ka_init0[32] = {5, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0,
                                     0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 3, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t ka_init1[16] = {1, 0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 0xfa, 0xff, 0, 0};
static const cint_state_var ka_vars0[] = {{"count", 5u, CINT_TAG_I64, 0u, 0u, NULL, NULL},
                                          {"xs", 2u, CINT_TAG_I64, 8u, 1u, ka_three, NULL}};
static const cint_state_var ka_vars1[] = {{"flags", 5u, CINT_TAG_BOOL, 0u, 1u, ka_two, NULL},
                                          {"m", 1u, CINT_TAG_I16, 2u, 2u, ka_m, NULL},
                                          {"none", 4u, CINT_TAG_U8, 14u, 1u, ka_zero, NULL}};
static const cint_state ka_state0 = {0u, 2u, 32u, 0u, ka_init0, ka_vars0};
static const cint_state ka_state1 = {1u, 3u, 16u, 0u, ka_init1, ka_vars1};
static const cint_state *const ka_states[] = {&ka_state0, &ka_state1};
static const cint_state_table ka_table = {2u, 0u, ka_states, NULL};

/* The state of a context of ka_table with xs as given and the rest initial. */
static void ka_state_bytes(bytes *x, const int64_t xs[3])
{
    static const int16_t m[6] = {1, 2, 3, 4, 5, -6};
    x->n = 0u;
    b_s(x, "cint-core-1/state/v1");
    b_raw(x, k_rev, 32u);
    b_s(x, "cint-core-1");
    b_u(x, 5u, 8u);
    b_s(x, "app.main.count");
    b_u(x, CINT_TAG_I64, 1u);
    b_u(x, 5u, 8u);
    b_s(x, "app.main.xs");
    b_u(x, 0x51u, 1u);
    b_u(x, CINT_TAG_I64, 1u);
    b_u(x, 1u, 1u);
    b_u(x, 3u, 8u);
    for (int i = 0; i < 3; i++) {
        b_u(x, (uint64_t)xs[i], 8u);
    }
    b_s(x, "lib.flags");
    b_u(x, 0x51u, 1u);
    b_u(x, CINT_TAG_BOOL, 1u);
    b_u(x, 1u, 1u);
    b_u(x, 2u, 8u);
    b_u(x, 1u, 1u);
    b_u(x, 0u, 1u);
    b_s(x, "lib.m");
    b_u(x, 0x51u, 1u);
    b_u(x, CINT_TAG_I16, 1u);
    b_u(x, 2u, 1u);
    b_u(x, 2u, 8u);
    b_u(x, 3u, 8u);
    for (int i = 0; i < 6; i++) {
        b_u(x, (uint16_t)m[i], 2u);
    }
    b_s(x, "lib.none");
    b_u(x, 0x51u, 1u);
    b_u(x, CINT_TAG_U8, 1u);
    b_u(x, 1u, 1u);
    b_u(x, 0u, 8u);
    for (int i = 0; i < 3; i++) {
        b_u(x, 0u, 8u);  /* no arenas, no pools, no registrations */
    }
    for (int i = 0; i < 3; i++) {
        b_u(x, 1u, 8u);  /* the next buffer, arena and pool */
    }
    b_u(x, 0u, 8u);
    b_s(x, "fuel-v1");
    b_u(x, 0u, 1u);
    b_u(x, 0u, 8u);
}

/* Binding, the bytes, the decoder and its text, restore and the change of revision. */
static void test_arrays(void)
{
    static const cint_state_var five[] = {{"xs", 2u, CINT_TAG_I64, 8u, 5u, ka_rank5, NULL}};
    static const cint_state_var no_extents[] = {{"xs", 2u, CINT_TAG_I64, 8u, 1u, NULL, NULL}};
    static const cint_state_var stray[] = {{"xs", 2u, CINT_TAG_I64, 8u, 0u, ka_three, NULL}};
    static const cint_state_var negative[] = {{"xs", 2u, CINT_TAG_I64, 8u, 1u, ka_neg, NULL}};
    static const cint_state_var past[] = {{"xs", 2u, CINT_TAG_I64, 8u, 1u, ka_four, NULL}};
    static const cint_state bad_states[] = {{0u, 1u, 32u, 0u, ka_init0, five}, {0u, 1u, 32u, 0u, ka_init0, no_extents},
                                            {0u, 1u, 32u, 0u, ka_init0, stray}, {0u, 1u, 32u, 0u, ka_init0, negative},
                                            {0u, 1u, 32u, 0u, ka_init0, past}};
    /* xs moved to offset 0, then retyped: U64[3], I64[4]. */
    static const cint_state_var moved[] = {{"count", 5u, CINT_TAG_I64, 24u, 0u, NULL, NULL}, {"xs", 2u, CINT_TAG_I64, 0u, 1u, ka_three, NULL}};
    static const cint_state_var unsigned_xs[] = {{"count", 5u, CINT_TAG_I64, 0u, 0u, NULL, NULL}, {"xs", 2u, CINT_TAG_U64, 8u, 1u, ka_three, NULL}};
    static const cint_state_var longer[] = {{"count", 5u, CINT_TAG_I64, 0u, 0u, NULL, NULL}, {"xs", 2u, CINT_TAG_I64, 8u, 1u, ka_four, NULL}};
    static const cint_state other0[] = {{0u, 2u, 32u, 0u, ka_init0, moved}, {0u, 2u, 32u, 0u, ka_init0, unsigned_xs},
                                        {0u, 2u, 40u, 0u, ka_init0, longer}};
    /* The second revision: xs kept at offset 16, flags kept, m reshaped to I16[3, 2] (initial
     * value 7 in its first element), none dropped. */
    static const uint8_t kb_init0[40] = {0};
    static const uint8_t kb_init1[14] = {0, 0, 7, 0};
    static const cint_state_var kb_vars0[] = {{"count", 5u, CINT_TAG_I64, 0u, 0u, NULL, NULL}, {"xs", 2u, CINT_TAG_I64, 16u, 1u, ka_three, NULL}};
    static const cint_state_var kb_vars1[] = {{"flags", 5u, CINT_TAG_BOOL, 0u, 1u, ka_two, NULL}, {"m", 1u, CINT_TAG_I16, 2u, 2u, ka_m2, NULL}};
    static const cint_state kb_state0 = {0u, 2u, 40u, 0u, kb_init0, kb_vars0};
    static const cint_state kb_state1 = {1u, 2u, 14u, 0u, kb_init1, kb_vars1};
    static const cint_state *const kb_states[] = {&kb_state0, &kb_state1};
    static const cint_state_table kb_table = {2u, 0u, kb_states, NULL};
    static const int64_t xs0[3] = {1, -2, 3}, xs1[3] = {1, 40, 3};
    static bytes c1, c2, want, tmp;
    static char text[4096], line[256];
    cint_ctx *ctx = k_ctx(&k_prog, NULL);
    cint_checkpoint_info info;
    cint_checkpoint_global g;
    size_t cur = 0u, at, xs_at, flags_at;
    uint8_t h[32], h1[32], d[32];
    char sh[65];
    int ok = 1;
    if (ctx == NULL) {
        CHECK(0, "cint_ctx_create");
        return;
    }
    for (size_t i = 0u; i < sizeof bad_states / sizeof bad_states[0]; i++) {
        const cint_state *one[1];
        cint_state_table t;
        one[0] = &bad_states[i];
        t.count = 1u;
        t.reserved = 0u;
        t.states = one;
        CHECK(cint_ctx_bind_state(ctx, &t) == CINT_REFUSED,
              "a rank above 4, missing or stray extents, a negative extent, an array past its block");
    }
    CHECK(cint_ctx_bind_state(ctx, &ka_table) == CINT_OK && take(ctx, &c1) == CINT_OK, "bind and checkpoint");
    ka_state_bytes(&tmp, xs0);
    k_checkpoint_bytes(&want, &tmp);
    CHECK(c1.n == want.n && memcmp(c1.b, want.b, want.n) == 0, "the bytes of arrays: tag 51, the extents, the elements");
    CHECK(cint_state_render(ctx, &ka_table, text, sizeof text) == 34u &&
              memcmp(text, "state.global app.main count I64 5\n", 34u) == 0,
          "an array has no state.global line");

    /* The decoder and its text. */
    CHECK(cint_checkpoint_decode(c1.b, c1.n, &info) == CINT_OK && info.global_count == 5u &&
              cint_checkpoint_global_next(&info, &cur, &g) == CINT_OK && g.elements == NULL && g.element_count == 0u,
          "a scalar has no elements");
    CHECK(cint_checkpoint_global_next(&info, &cur, &g) == CINT_OK && g.value.len == 11u && g.value.bytes[0] == 0x51u &&
              g.value.bytes[1] == CINT_TAG_I64 && g.value.bytes[2] == 1u && g.element_count == 3u &&
              g.elements != NULL && memcmp(g.elements, ka_init0 + 8, 24u) == 0,
          "xs: its descriptor and its elements");
    CHECK(cint_checkpoint_global_next(&info, &cur, &g) == CINT_OK && g.element_count == 2u &&
              cint_checkpoint_global_next(&info, &cur, &g) == CINT_OK && g.value.len == 19u && g.element_count == 6u &&
              memcmp(g.elements, ka_init1 + 2, 12u) == 0,
          "flags, and m of rank 2");
    CHECK(cint_checkpoint_global_next(&info, &cur, &g) == CINT_OK && g.element_count == 0u && g.elements != NULL &&
              cint_checkpoint_global_next(&info, &cur, &g) == CINT_REFUSED,
          "none: an extent of 0");
    (void)cint_checkpoint_render(&info, text, sizeof text);
    cint_sha256(ka_init0 + 8, 24u, d);
    hex(sh, d, 32u);
    snprintf(line, sizeof line, "\nglobal app.main.xs I64[3] elements-sha256 %s\n", sh);
    ok &= strstr(text, line) != NULL;
    cint_sha256(ka_init1 + 2, 12u, d);
    hex(sh, d, 32u);
    snprintf(line, sizeof line, "\nglobal lib.m I16[2, 3] elements-sha256 %s\n", sh);
    ok &= strstr(text, line) != NULL && strstr(text, "\nglobal app.main.count I64 5\nglobal app.main.xs I64[3] ") != NULL;
    cint_sha256(ka_init1, 0u, d);
    hex(sh, d, 32u);
    snprintf(line, sizeof line, "\nglobal lib.none U8[0] elements-sha256 %s\n", sh);
    ok &= strstr(text, line) != NULL && strstr(text, "\nglobal lib.flags Bool[2] elements-sha256 ") != NULL;
    CHECK(ok, "the text of show: an array's type as written and the SHA-256 of its elements");

    /* Restore. */
    {
        uint8_t *block = (uint8_t *)cint_rt_state(ctx, &ka_state0);
        int64_t v = 40;
        CHECK(block != NULL, "the block of app/main.ci");
        if (block != NULL) {
            memcpy(block + 16, &v, 8u);  /* xs[1] = 40; little-endian hosts */
        }
    }
    ka_state_bytes(&tmp, xs1);
    k_checkpoint_bytes(&want, &tmp);
    CHECK(take(ctx, &c2) == CINT_OK && c2.n == want.n && memcmp(c2.b, want.b, want.n) == 0, "a store to an element");
    hash_of(ctx, h);
    CHECK(cint_ctx_restore(ctx, c1.b, c1.n) == CINT_OK && take(ctx, &tmp) == CINT_OK && tmp.n == c1.n &&
              memcmp(tmp.b, c1.b, c1.n) == 0,
          "restore c1: the elements come back");
    ok = cint_ctx_restore(ctx, c2.b, c2.n) == CINT_OK;
    hash_of(ctx, h1);
    CHECK(ok && memcmp(h, h1, 32u) == 0, "restore c2: the same state");
    for (size_t i = 0u; i < sizeof other0 / sizeof other0[0]; i++) {
        const cint_state *two[2];
        cint_state_table t;
        cint_ctx *o = k_ctx(&k_prog, NULL);
        cint_status st = CINT_RESOURCE;
        two[0] = &other0[i];
        two[1] = &ka_state1;
        t.count = 2u;
        t.reserved = 0u;
        t.states = two;
        if (o != NULL && cint_ctx_bind_state(o, &t) == CINT_OK) {
            st = cint_ctx_restore(o, c2.b, c2.n);
        }
        if (i == 0u) {
            const uint8_t *b = o != NULL ? (const uint8_t *)cint_rt_state(o, &other0[0]) : NULL;
            CHECK(st == CINT_OK && b != NULL && memcmp(b, ka_init0 + 8, 8u) == 0 && b[8] == 40u &&
                      memcmp(b + 16, ka_init0 + 24, 8u) == 0 && b[24] == 5u,
                  "a restore puts each element at the offset of the bound table");
        } else {
            CHECK(st == CINT_REFUSED, "an element type or an extent that differs is refused");
        }
        cint_ctx_destroy(o);
    }

    /* Malformed arrays; every truncation; single-byte changes. */
    xs_at = find(&c2, 0u, "app.main.xs") + 11u;
    flags_at = find(&c2, 0u, "lib.flags") + 9u;
    {
        static const struct {
            size_t at;
            uint8_t v;
        } changes[] = {{2u, 0u}, {2u, 9u}, {1u, CINT_TAG_Z}, {1u, 0x51u}, {10u, 0x80u}, {10u, 0x7fu}};
        ok = 1;
        for (size_t i = 0u; i < sizeof changes / sizeof changes[0]; i++) {
            memcpy(tmp.b, c2.b, c2.n);
            tmp.b[xs_at + changes[i].at] = changes[i].v;
            if (changes[i].v == 0x7fu) {  /* an extent of 2^63 - 1 */
                memset(tmp.b + xs_at + 3u, 0xff, 7u);
            }
            ok &= cint_checkpoint_decode(tmp.b, c2.n, &info) == CINT_REFUSED;
        }
        memcpy(tmp.b, c2.b, c2.n);
        tmp.b[flags_at + 11u] = 2u;
        ok &= cint_checkpoint_decode(tmp.b, c2.n, &info) == CINT_REFUSED;
        CHECK(ok, "a rank of 0 or 9, an element of Z or an array, a negative or a huge extent, a Bool of 2");
    }
    ok = 1;
    for (at = 0u; at < c2.n; at++) {
        ok &= cint_ctx_restore(ctx, c2.b, at) == CINT_REFUSED;
    }
    for (at = find(&c2, 0u, "app.main.xs") - 4u; at < flags_at + 13u; at++) {
        static const uint8_t masks[3] = {0x01u, 0x80u, 0xffu};
        for (size_t k = 0u; k < 3u; k++) {
            cint_status st;
            memcpy(tmp.b, c2.b, c2.n);
            tmp.b[at] ^= masks[k];
            st = cint_ctx_restore(ctx, tmp.b, c2.n);
            if (st == CINT_OK) {  /* an element changed */
                ok &= cint_ctx_restore(ctx, c2.b, c2.n) == CINT_OK;
            } else {
                hash_of(ctx, h1);
                ok &= st == CINT_REFUSED && memcmp(h, h1, 32u) == 0;
            }
        }
    }
    CHECK(ok, "truncations are refused; a changed byte of an array is refused with the context unchanged, or restores");

    /* The change of revision: an array of the same name, element type and extents keeps its
     * elements; m, reshaped, starts at its initial value; none is dropped. */
    CHECK(cint_ctx_change_revision(ctx, &k_prog2, &kb_table) == CINT_OK && take(ctx, &tmp) == CINT_OK &&
              cint_checkpoint_decode(tmp.b, tmp.n, &info) == CINT_OK && info.global_count == 4u,
          "change of revision");
    cur = 0u;
    ok = cint_checkpoint_global_next(&info, &cur, &g) == CINT_OK && g.element_count == 0u;
    ok &= cint_checkpoint_global_next(&info, &cur, &g) == CINT_OK && g.element_count == 3u &&
          memcmp(g.elements, c2.b + xs_at + 11u, 24u) == 0;
    ok &= cint_checkpoint_global_next(&info, &cur, &g) == CINT_OK && g.element_count == 2u && g.elements[0] == 1u;
    ok &= cint_checkpoint_global_next(&info, &cur, &g) == CINT_OK && g.element_count == 6u &&
          memcmp(g.elements, kb_init1 + 2, 12u) == 0 && g.value.bytes[11] == 2u;
    CHECK(ok && cint_checkpoint_global_next(&info, &cur, &g) == CINT_REFUSED,
          "xs and flags keep their elements, m starts at its initial value, none is dropped");
    cint_ctx_destroy(ctx);
}

/* ------------------------------------------------------------------------- */
/* The carry of arenas and pools across a change of revision (decision 2026-10-08 on OQ-243;  */
/* rt/OPEN.md RT-OQ-46).                                                                       */

/* app/main.ci holds four handles: head at 0, tail at 32, mid at 64 and lost at 96. */
static const uint8_t kc_init[128] = {0u};
static const cint_state_var kc_vars[] = {{"head", 4u, CINT_TAG_HANDLE, 0u, 0u, NULL, NULL},
                                         {"tail", 4u, CINT_TAG_HANDLE, 32u, 0u, NULL, NULL},
                                         {"mid", 3u, CINT_TAG_HANDLE, 64u, 0u, NULL, NULL},
                                         {"lost", 4u, CINT_TAG_HANDLE, 96u, 0u, NULL, NULL}};
static const cint_state kc_state = {0u, 4u, 128u, 0u, kc_init, kc_vars};
static const cint_state *const kc_states[] = {&kc_state};

/* k.Node(v: I64, next: Handle(Node)), stored as compiled C stores it. */
typedef struct kc_node {
    int64_t v;
    cint_handle next;
} kc_node;
static const uint8_t kc_node_type[] = {1, 0, 0, 0, 0x71, 6, 0, 0, 0, 'k', '.', 'N', 'o', 'd', 'e', 0, 2, 0, 0, 0,
                                       1, 0, 0, 0, 'v', 0, 0x14, 4, 0, 0, 0, 'n', 'e', 'x', 't', 0, 0x75,
                                       0x71, 0, 0, 0, 0};
static const uint32_t kc_node_layout[] = {(uint32_t)sizeof(kc_node), 0u, (uint32_t)offsetof(kc_node, next)};
#define KC_NODES(cap)                                                                                     \
    {CINT_HANDLE_POOL, (uint32_t)sizeof(kc_node), cap, kc_node_type, kc_node_layout,                      \
     (uint32_t)sizeof kc_node_type, 3u, 0u, 5u, "nodes"}

/* Revision 1: arena 1 nums I32[8], pool 1 nodes Node[4], arena 2 scratch U16[6], pool 2 gone
 * I32[2]. */
static const cint_mem_decl kc_decls[] = {
    {CINT_HANDLE_ARENA, 4u, 8, k_i32, NULL, 5u, 0u, 0u, 4u, "nums"}, KC_NODES(4),
    {CINT_HANDLE_ARENA, 2u, 6, k_u16, NULL, 5u, 0u, 0u, 7u, "scratch"},
    {CINT_HANDLE_POOL, 4u, 2, k_i32, NULL, 5u, 0u, 0u, 4u, "gone"}};
static const cint_mem_table kc_mem = {4u, 0u, kc_decls};
/* Revision 2: pool 1 fresh I32[4] new, arena 1 scratch retyped I32, pool 2 nodes kept, arena 2
 * nums kept in 8-byte cells; gone removed. */
static const cint_mem_decl kc2_decls[] = {
    {CINT_HANDLE_POOL, 4u, 4, k_i32, NULL, 5u, 0u, 0u, 5u, "fresh"},
    {CINT_HANDLE_ARENA, 4u, 6, k_i32, NULL, 5u, 0u, 0u, 7u, "scratch"}, KC_NODES(4),
    {CINT_HANDLE_ARENA, 8u, 8, k_i32, NULL, 5u, 0u, 8u, 4u, "nums"}};
static const cint_mem_table kc2_mem = {4u, 0u, kc2_decls};
/* Revision 3: three new arenas before nums, and nodes of another capacity. */
static const cint_mem_decl kc3_decls[] = {
    {CINT_HANDLE_ARENA, 2u, 1, k_u16, NULL, 5u, 0u, 0u, 2u, "x1"},
    {CINT_HANDLE_ARENA, 2u, 1, k_u16, NULL, 5u, 0u, 0u, 2u, "x2"},
    {CINT_HANDLE_ARENA, 2u, 1, k_u16, NULL, 5u, 0u, 0u, 2u, "x3"},
    {CINT_HANDLE_ARENA, 4u, 8, k_i32, NULL, 5u, 0u, 0u, 4u, "nums"}, KC_NODES(5)};
static const cint_mem_table kc3_mem = {5u, 0u, kc3_decls};
/* Revision 1's table declared by lib.ci: nothing is carried. */
static const cint_mem_table kc_mem_lib = {4u, 1u, kc_decls};
static const cint_state_table kc_table = {1u, 0u, kc_states, &kc_mem}, kc2_table = {1u, 0u, kc_states, &kc2_mem},
                              kc3_table = {1u, 0u, kc_states, &kc3_mem}, kc_table_lib = {1u, 0u, kc_states, &kc_mem_lib},
                              kc_table_none = {1u, 0u, kc_states, NULL};

/* The I32 element `i` of arena `arena` of table t through handle h, stored `size` bytes apart. */
static bool kc_get(cint_ctx *ctx, const cint_mem_table *t, uint32_t arena, cint_handle h, uint32_t size, int64_t i,
                   int64_t *out)
{
    cint_vdesc v;
    cint_lease lease;
    int32_t x = 0;
    int64_t y = 0;
    if (!cint_mem_view(ctx, K_OP, t, arena, h, &v, &lease)) {
        (void)cint_ctx_clear_fault(ctx);
        return false;
    }
    if (size == 8u) {
        memcpy(&y, (const uint8_t *)v.base + (size_t)(v.origin + i) * 8u, 8u);
    } else {
        memcpy(&x, (const uint8_t *)v.base + (size_t)(v.origin + i) * 4u, 4u);
        y = x;
    }
    *out = y;
    return true;
}

static bool kc_set(cint_ctx *ctx, uint32_t arena, cint_handle h, int64_t i, int32_t x)
{
    cint_vdesc v;
    cint_lease lease;
    if (!cint_mem_view(ctx, K_OP, &kc_mem, arena, h, &v, &lease)) {
        return false;
    }
    memcpy((uint8_t *)v.base + (size_t)(v.origin + i) * 4u, &x, 4u);
    return true;
}

static const cint_mem_table *g_table;
static bool kc_deref_p1(cint_ctx *ctx)
{
    return cint_mem_deref(ctx, K_OP, g_table, 1u, g_handle) != NULL;
}
static bool kc_deref_p2(cint_ctx *ctx)
{
    return cint_mem_deref(ctx, K_OP, g_table, 2u, g_handle) != NULL;
}

/* The handle global at `offset` of a context's block. */
static cint_handle kc_global(cint_ctx *ctx, uint32_t offset)
{
    cint_handle h;
    memcpy(&h, (const uint8_t *)cint_rt_state(ctx, &kc_state) + offset, sizeof h);
    return h;
}

/* A context of revision 1 with content: nums [-5, 7, 9] and its child 3 holding [11]; scratch
 * with its child 4; nodes 5 -> 4 at slots 1 and 0, slot 2 removed; one I32 in gone; head the
 * node 5, tail gone's element, mid child 3's allocation, lost child 4's. */
static cint_ctx *kc_ctx(cint_ctx *ctx)
{
    cint_handle hn = kt_null, hc = kt_null, hs = kt_null, hl = kt_null, h1 = kt_null, h2 = kt_null, h3 = kt_null;
    cint_handle hg = kt_null;
    kc_node n1, n2;
    uint32_t c3 = 0u, c4 = 0u;
    int32_t g = 3;
    uint8_t *b = ctx != NULL && cint_ctx_bind_state(ctx, &kc_table) == CINT_OK ? (uint8_t *)cint_rt_state(ctx, &kc_state)
                                                                                : NULL;
    memset(&n1, 0, sizeof n1);
    memset(&n2, 0, sizeof n2);
    n1.v = 4;
    n2.v = 5;
    bool ok = b != NULL && cint_mem_alloc(ctx, K_OP, &kc_mem, 1u, 3, false, &hn, NULL) && kc_set(ctx, 1u, hn, 0, -5) &&
              kc_set(ctx, 1u, hn, 1, 7) && kc_set(ctx, 1u, hn, 2, 9) && cint_mem_child(ctx, K_OP, &kc_mem, 1u, 2, &c3) &&
              cint_mem_alloc(ctx, K_OP, &kc_mem, c3, 1, false, &hc, NULL) && kc_set(ctx, c3, hc, 0, 11) &&
              cint_mem_alloc(ctx, K_OP, &kc_mem, 2u, 1, false, &hs, NULL) &&
              cint_mem_child(ctx, K_OP, &kc_mem, 2u, 2, &c4) && cint_mem_alloc(ctx, K_OP, &kc_mem, c4, 1, false, &hl, NULL) &&
              cint_mem_insert(ctx, K_OP, &kc_mem, 1u, &n1, false, &h1, NULL);
    n2.next = h1;
    ok = ok && cint_mem_insert(ctx, K_OP, &kc_mem, 1u, &n2, false, &h2, NULL) &&
         cint_mem_insert(ctx, K_OP, &kc_mem, 1u, &n1, false, &h3, NULL) && cint_mem_remove(ctx, K_OP, &kc_mem, 1u, h3) &&
         cint_mem_insert(ctx, K_OP, &kc_mem, 2u, &g, false, &hg, NULL) && c3 == 3u && c4 == 4u;
    if (!ok) {
        cint_ctx_destroy(ctx);
        return NULL;
    }
    memcpy(b, &h2, sizeof h2);
    memcpy(b + 32, &hg, sizeof hg);
    memcpy(b + 64, &hc, sizeof hc);
    memcpy(b + 96, &hl, sizeof hl);
    return ctx;
}

/* The container and kind of each handle global of checkpoint c, in name order: head, lost, mid,
 * tail. */
static bool kc_globals(const bytes *c, const uint32_t want[8])
{
    cint_checkpoint_info info;
    cint_checkpoint_global g;
    size_t cur = 0u;
    bool ok = cint_checkpoint_decode(c->b, c->n, &info) == CINT_OK && info.global_count == 4u;
    for (unsigned k = 0u; ok && k < 4u; k++) {
        uint32_t id = 0u;
        ok = cint_checkpoint_global_next(&info, &cur, &g) == CINT_OK && g.value.len == 30u;
        memcpy(&id, g.value.bytes + 1u + offsetof(cint_handle, container), 4u);
        ok = ok && id == want[2u * k] && g.value.bytes[1u + offsetof(cint_handle, kind)] == want[2u * k + 1u];
    }
    return ok;
}

static void test_carry(void)
{
    static bytes c1, c2, c3, tmp;
    static const uint32_t g2[8] = {2u, 2u, 0u, 1u, 3u, 1u, 0u, 2u}, g3[8] = {0u, 2u, 0u, 1u, 5u, 1u, 0u, 2u};
    static const uint32_t g0[8] = {0u, 2u, 0u, 1u, 0u, 1u, 0u, 2u};
    static char fault[128];
    cint_ctx *ctx = kc_ctx(k_ctx(&k_prog, NULL)), *held = k_ctx(&k_prog, NULL), *bare = k_ctx(&k_prog, NULL);
    cint_ctx *back = k_ctx(&k_prog2, NULL), *lib = k_ctx(&k_prog, NULL);
    cint_checkpoint_info info, info1;
    cint_checkpoint_arena a, a1;
    cint_checkpoint_pool p;
    cint_handle hn;
    const kc_node *n;
    size_t cur = 0u, cur1 = 0u;
    uint8_t hb[32], d[32];
    int64_t x = 0, y = 0, z = 0, w = 0;
    int ok = 1;
    if (ctx == NULL || held == NULL || bare == NULL || back == NULL || lib == NULL || take(ctx, &c1) != CINT_OK ||
        cint_ctx_bind_state(held, &kc_table) != CINT_OK || cint_ctx_bind_state(bare, &kc_table_none) != CINT_OK ||
        cint_ctx_bind_state(back, &kc2_table) != CINT_OK || cint_ctx_bind_state(lib, &kc_table) != CINT_OK) {
        CHECK(0, "a context with arenas, pools and handles");
        cint_ctx_destroy(ctx);
        cint_ctx_destroy(held);
        cint_ctx_destroy(bare);
        cint_ctx_destroy(back);
        cint_ctx_destroy(lib);
        return;
    }
    memset(&hn, 0, sizeof hn);
    hn.length = 3;
    hn.container = 2u;
    hn.kind = CINT_HANDLE_ARENA;

    /* Revision 2: nums and nodes kept and renumbered, child 3 keeps its identifier, the rest
     * empty or dropped; the handles follow. */
    CHECK(cint_ctx_change_revision(ctx, &k_prog2, &kc2_table) == CINT_OK && take(ctx, &c2) == CINT_OK &&
              cint_checkpoint_decode(c2.b, c2.n, &info) == CINT_OK && cint_checkpoint_decode(c1.b, c1.n, &info1) == CINT_OK,
          "a context with arenas and pools changes revision");
    ok &= cint_checkpoint_arena_next(&info, &cur, &a) == CINT_OK && a.id == 1u && a.used == 0 && a.capacity == 6 &&
          a.type_len == 5u && memcmp(a.type, k_i32, 5u) == 0;
    ok &= cint_checkpoint_arena_next(&info, &cur, &a) == CINT_OK && a.id == 2u && a.parent == 0u && a.used == 5 &&
          a.capacity == 8 && cint_checkpoint_arena_next(&info1, &cur1, &a1) == CINT_OK && a1.id == 1u &&
          memcmp(a.elements, a1.elements, 20u) == 0;
    ok &= cint_checkpoint_arena_next(&info, &cur, &a) == CINT_OK && a.id == 3u && a.parent == 2u && a.offset == 3 &&
          a.used == 1 && a.retired == 0u && cint_checkpoint_arena_next(&info, &cur, &a) == CINT_REFUSED;
    CHECK(ok, "arena 1 scratch is empty, arena 2 nums keeps its IM-146 elements, child 3 keeps its place");
    cur = 0u;
    ok = cint_checkpoint_pool_next(&info, &cur, &p) == CINT_OK && p.id == 1u && p.used == 0u && p.capacity == 4;
    ok &= cint_checkpoint_pool_next(&info, &cur, &p) == CINT_OK && p.id == 2u && p.used == 3u && p.occupied == 2u &&
          p.top == 3u && cint_checkpoint_pool_next(&info, &cur, &p) == CINT_REFUSED;
    CHECK(ok && info.next_arena == 5u && info.next_pool == 3u, "pool 1 fresh is empty, pool 2 nodes keeps its slots");
    CHECK(kc_globals(&c2, g2), "head names pool 2, mid arena 3; tail and lost name container 0");
    g_table = &kc2_mem;
    g_handle = kc_global(ctx, 0u);
    n = (const kc_node *)cint_mem_deref(ctx, K_OP, &kc2_mem, 2u, g_handle);
    ok = n != NULL && n->v == 5 && n->next.container == 2u && n->next.kind == CINT_HANDLE_POOL;
    n = ok ? (const kc_node *)cint_mem_deref(ctx, K_OP, &kc2_mem, 2u, n->next) : NULL;
    CHECK(ok && n != NULL && n->v == 4 && n->next.kind == 0u, "the node's handle follows its pool");
    CHECK(kc_get(ctx, &kc2_mem, 2u, hn, 8u, 0, &x) && kc_get(ctx, &kc2_mem, 2u, hn, 8u, 1, &y) &&
              kc_get(ctx, &kc2_mem, 2u, hn, 8u, 2, &z) && kc_get(ctx, &kc2_mem, 3u, kc_global(ctx, 64u), 8u, 0, &w) &&
              x == -5 && y == 7 && z == 9 && w == 11,
          "nums in 8-byte cells, -5 sign-extended, and child 3's element through mid");
    g_handle = kc_global(ctx, 32u);
    km_fault(ctx, kc_deref_p1, fault, sizeof fault);
    CHECK(strcmp(fault, "8 pool.deref U8 6") == 0, "a handle into the dropped pool faults as stale, reason container");
    g_handle = kc_global(ctx, 96u);
    km_fault(ctx, kc_deref_p2, fault, sizeof fault);
    CHECK(strcmp(fault, "8 pool.deref U8 6") == 0, "and so does one into a dropped child");

    /* The same from a state held since a restore, and a restore of the result. */
    CHECK(cint_ctx_restore(held, c1.b, c1.n) == CINT_OK && cint_ctx_change_revision(held, &k_prog2, &kc2_table) == CINT_OK &&
              take(held, &tmp) == CINT_OK && tmp.n == c2.n && memcmp(tmp.b, c2.b, c2.n) == 0,
          "a held state is laid out by its own table, then carried alike");
    CHECK(cint_ctx_restore(back, c2.b, c2.n) == CINT_OK && take(back, &tmp) == CINT_OK && tmp.n == c2.n &&
              memcmp(tmp.b, c2.b, c2.n) == 0,
          "and the result restores into the new revision");
    CHECK(cint_ctx_restore(bare, c1.b, c1.n) == CINT_OK, "a context bound without the memory table restores");
    hash_of(bare, hb);
    hash_of(bare, d);
    CHECK(cint_ctx_change_revision(bare, &k_prog2, &kc2_table) == CINT_REFUSED && (hash_of(bare, d), memcmp(hb, d, 32u) == 0),
          "but a state it holds does not change revision, and stays unchanged");

    /* Revision 3: the child takes the next identifier, nodes of another capacity drop. */
    CHECK(cint_ctx_change_revision(ctx, &k_prog, &kc3_table) == CINT_OK && take(ctx, &c3) == CINT_OK &&
              cint_checkpoint_decode(c3.b, c3.n, &info) == CINT_OK && info.next_arena == 6u && kc_globals(&c3, g3),
          "nums becomes arena 4 and its child arena 5");
    hn.container = 4u;
    CHECK(kc_get(ctx, &kc3_mem, 4u, hn, 4u, 0, &x) && x == -5 && kc_get(ctx, &kc3_mem, 5u, kc_global(ctx, 64u), 4u, 0, &w) &&
              w == 11,
          "back in 4-byte elements");
    cur = 0u;
    ok = 1;
    for (uint32_t k = 1u; k <= 5u; k++) {
        ok &= cint_checkpoint_arena_next(&info, &cur, &a) == CINT_OK && a.id == k;
    }
    CHECK(ok && a.parent == 4u, "arenas 1 to 5, child 5 under nums");

    /* No memory table: everything drops, the counters stay. Another module: nothing is carried. */
    CHECK(cint_ctx_change_revision(ctx, &k_prog2, &kc_table_none) == CINT_OK && take(ctx, &tmp) == CINT_OK &&
              cint_checkpoint_decode(tmp.b, tmp.n, &info) == CINT_OK && info.next_arena == 6u && info.next_pool == 3u &&
              kc_globals(&tmp, g0) && (cur = 0u, cint_checkpoint_arena_next(&info, &cur, &a) == CINT_REFUSED),
          "a table without arenas or pools drops them all and keeps the counters");
    CHECK(cint_ctx_restore(lib, c1.b, c1.n) == CINT_OK && cint_ctx_change_revision(lib, &k_prog2, &kc_table_lib) == CINT_OK &&
              take(lib, &tmp) == CINT_OK && kc_globals(&tmp, g0),
          "a table of another module carries nothing");

    /* Memory that runs out: CINT_RESOURCE with the context unchanged, or the same result. */
    {
        cint_ctx_config cfg;
        int failed = 0, done = 0;
        ok = 1;
        memset(&cfg, 0, sizeof cfg);
        cfg.size = (uint32_t)sizeof cfg;
        cfg.program = &k_prog;
        cfg.allocator.alloc = k_alloc;
        cfg.allocator.release = k_release;
        for (int i = 1; i < 120 && done < 3; i++) {
            cint_ctx *y1 = NULL;
            cint_status st;
            ok &= cint_ctx_create(&cfg, &y1) == CINT_OK && cint_ctx_bind_state(y1, &kc_table) == CINT_OK &&
                  cint_ctx_restore(y1, c1.b, c1.n) == CINT_OK;
            hash_of(y1, hb);
            g_fail_at = i;
            st = cint_ctx_change_revision(y1, &k_prog2, &kc2_table);
            g_fail_at = 0;
            hash_of(y1, d);
            failed += st == CINT_RESOURCE ? 1 : 0;
            done += st == CINT_OK ? 1 : 0;
            ok &= st == CINT_OK ? take(y1, &tmp) == CINT_OK && tmp.n == c2.n && memcmp(tmp.b, c2.b, c2.n) == 0
                                : st == CINT_RESOURCE && memcmp(hb, d, 32u) == 0;
            cint_ctx_destroy(y1);
        }
        CHECK(ok && failed > 5 && done > 0, "memory that runs out leaves the context unchanged");
    }
    cint_ctx_destroy(ctx);
    cint_ctx_destroy(held);
    cint_ctx_destroy(bare);
    cint_ctx_destroy(back);
    cint_ctx_destroy(lib);
}

/* ------------------------------------------------------------------------- */
/* Module-level structs (decision 2026-10-09 on OQ-260; rt/OPEN.md RT-OQ-51): tag 71, the type  */
/* as an arena writes an element type, then the payload (SPEC-01 IM-146, IM-148).                */

/* app/main.ci: count I64 at 0 (5), st at 8 of struct St { I16[2, 3] pos; Bool stopped; Inner in;
 * I64 tick; } with Inner { I8 a; U16 b; }: pos [1, 2, 3, 4, 5, -6], stopped true, in (-3, 700),
 * tick 7, laid out as C does (offsets 0, 12, 14, 24; Inner 0, 2) or in cells (cell 8). */
static const uint32_t ks_c_layout[8] = {4u, 0u, 2u, 32u, 0u, 12u, 14u, 24u};
static const uint32_t ks_cell_layout[8] = {16u, 0u, 8u, 80u, 0u, 48u, 56u, 72u};
static const uint32_t ks_handle_layout[8] = {4u, 0u, 2u, 56u, 0u, 12u, 14u, 24u};
static const uint8_t ks_init[40] = {5, 0, 0, 0, 0, 0, 0, 0,
                                    1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 0xfa, 0xff, 1, 0, 0xfd, 0, 0xbc, 0x02, 0, 0,
                                    0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0};
static uint8_t ks_type[256], ks_type_i32[256], ks_type_handle[256], ks_type_rank5[256];
static uint32_t ks_type_len, ks_type_i32_len, ks_type_handle_len, ks_type_rank5_len;

/* The type of St, with tick of type `tick` and pos of rank `rank` (2: [2, 3]; 5: [1, 1, 1, 2, 3]). */
static uint32_t ks_type_of(uint8_t *out, uint8_t tick, uint32_t rank)
{
    static bytes x;
    x.n = 0u;
    b_u(&x, 2u, 4u);
    b_u(&x, 0x71u, 1u);
    b_s(&x, "app.main.Inner");
    b_u(&x, 0u, 1u);
    b_u(&x, 2u, 4u);
    b_s(&x, "a");
    b_u(&x, 0u, 1u);
    b_u(&x, CINT_TAG_I8, 1u);
    b_s(&x, "b");
    b_u(&x, 0u, 1u);
    b_u(&x, CINT_TAG_U16, 1u);
    b_u(&x, 0x71u, 1u);
    b_s(&x, "app.main.St");
    b_u(&x, 0u, 1u);
    b_u(&x, 4u, 4u);
    b_s(&x, "pos");
    b_u(&x, 0u, 1u);
    b_u(&x, 0x52u, 1u);
    b_u(&x, CINT_TAG_I16, 1u);
    b_u(&x, rank, 1u);
    for (uint32_t d = 0u; d < rank; d++) {
        b_u(&x, 1u, 1u);
        b_u(&x, UINT32_MAX, 4u);
        b_u(&x, d + 2u == rank ? 2u : d + 1u == rank ? 3u : 1u, 8u);
    }
    b_s(&x, "stopped");
    b_u(&x, 0u, 1u);
    b_u(&x, CINT_TAG_BOOL, 1u);
    b_s(&x, "in");
    b_u(&x, 0u, 1u);
    b_u(&x, 0x71u, 1u);
    b_u(&x, 0u, 4u);
    b_s(&x, "tick");
    b_u(&x, 0u, 1u);
    b_u(&x, tick, 1u);
    b_u(&x, 0x71u, 1u);
    b_u(&x, 1u, 4u);
    memcpy(out, x.b, x.n);
    return (uint32_t)x.n;
}

/* The payload of st: pos, stopped, in, tick (24 bytes). */
static void ks_payload(bytes *x, int64_t tick)
{
    static const int16_t pos[6] = {1, 2, 3, 4, 5, -6};
    for (int i = 0; i < 6; i++) {
        b_u(x, (uint16_t)pos[i], 2u);
    }
    b_u(x, 1u, 1u);
    b_u(x, 0xfdu, 1u);
    b_u(x, 700u, 2u);
    b_u(x, (uint64_t)tick, 8u);
}

/* The state of a context of one module with count and st as given. */
static void ks_state_bytes(bytes *x, int64_t tick)
{
    x->n = 0u;
    b_s(x, "cint-core-1/state/v1");
    b_raw(x, k_rev, 32u);
    b_s(x, "cint-core-1");
    b_u(x, 2u, 8u);
    b_s(x, "app.main.count");
    b_u(x, CINT_TAG_I64, 1u);
    b_u(x, 5u, 8u);
    b_s(x, "app.main.st");
    b_u(x, 0x71u, 1u);
    b_raw(x, ks_type, ks_type_len);
    ks_payload(x, tick);
    for (int i = 0; i < 3; i++) {
        b_u(x, 0u, 8u);  /* no arenas, no pools, no registrations */
    }
    for (int i = 0; i < 3; i++) {
        b_u(x, 1u, 8u);
    }
    b_u(x, 0u, 8u);
    b_s(x, "fuel-v1");
    b_u(x, 0u, 1u);
    b_u(x, 0u, 8u);
}

/* A table of one state of app/main.ci: count at 0 and st at `offset` of type t, in `size` bytes. */
typedef struct ks_table {
    cint_state_var vars[2];
    cint_state state;
    const cint_state *one[1];
    cint_state_table table;
} ks_table;

static const cint_state_table *ks_make(ks_table *k, const cint_state_type *t, uint32_t offset, uint32_t size,
                                       const uint8_t *init)
{
    static const cint_state_var count = {"count", 5u, CINT_TAG_I64, 0u, 0u, NULL, NULL};
    memset(k, 0, sizeof *k);
    k->vars[0] = count;
    k->vars[1].name = "st";
    k->vars[1].name_len = 2u;
    k->vars[1].tag = 0x71u;
    k->vars[1].offset = offset;
    k->vars[1].type = t;
    k->state.module = 0u;
    k->state.var_count = 2u;
    k->state.bytes = size;
    k->state.init = init;
    k->state.vars = k->vars;
    k->one[0] = &k->state;
    k->table.count = 1u;
    k->table.states = k->one;
    return &k->table;
}

/* Binding, the bytes, the decoder and its text, restore and the change of revision. */
static void test_structs(void)
{
    static ks_table kt, kc, kx;
    static bytes c1, c2, want, tmp;
    static char text[4096], line[256];
    static uint8_t cell_init[96];
    cint_state_type t, cell, i32, bad;
    cint_ctx *ctx = k_ctx(&k_prog, NULL);
    cint_checkpoint_info info;
    cint_checkpoint_global g;
    size_t cur = 0u, at, st_at;
    uint8_t h[32], h1[32], d[32];
    char sh[65];
    int ok = 1;
    ks_type_len = ks_type_of(ks_type, CINT_TAG_I64, 2u);
    ks_type_i32_len = ks_type_of(ks_type_i32, 0x13u, 2u);
    ks_type_rank5_len = ks_type_of(ks_type_rank5, CINT_TAG_I64, 5u);
    ks_type_handle_len = ks_type_of(ks_type_handle, CINT_TAG_HANDLE, 2u);
    t = (cint_state_type){ks_type, ks_c_layout, ks_type_len, 8u, 0u, 0u};
    cell = (cint_state_type){ks_type, ks_cell_layout, ks_type_len, 8u, 8u, 0u};
    i32 = (cint_state_type){ks_type_i32, ks_c_layout, ks_type_i32_len, 8u, 0u, 0u};
    if (ctx == NULL) {
        CHECK(0, "cint_ctx_create");
        return;
    }

    /* What binding refuses. */
    {
        static const int64_t ext[1] = {1};
        const cint_state_table *x;
        int refused = 1;
        bad = t;
        bad.cell = 3u;
        refused &= cint_ctx_bind_state(ctx, ks_make(&kx, &bad, 8u, 40u, ks_init)) == CINT_REFUSED;
        bad = t;
        bad.reserved = 1u;
        refused &= cint_ctx_bind_state(ctx, ks_make(&kx, &bad, 8u, 40u, ks_init)) == CINT_REFUSED;
        bad = (cint_state_type){ks_type_rank5, ks_c_layout, ks_type_rank5_len, 8u, 0u, 0u};
        refused &= cint_ctx_bind_state(ctx, ks_make(&kx, &bad, 8u, 40u, ks_init)) == CINT_REFUSED;
        bad = (cint_state_type){ks_type_handle, ks_handle_layout, ks_type_handle_len, 8u, 0u, 0u};
        refused &= cint_ctx_bind_state(ctx, ks_make(&kx, &bad, 8u, 64u, cell_init)) == CINT_REFUSED;
        bad = (cint_state_type){ks_type, ks_c_layout, ks_type_len, 7u, 0u, 0u};
        refused &= cint_ctx_bind_state(ctx, ks_make(&kx, &bad, 8u, 40u, ks_init)) == CINT_REFUSED;
        bad = (cint_state_type){ks_type, ks_c_layout, ks_type_len - 1u, 8u, 0u, 0u};
        refused &= cint_ctx_bind_state(ctx, ks_make(&kx, &bad, 8u, 40u, ks_init)) == CINT_REFUSED;
        refused &= cint_ctx_bind_state(ctx, ks_make(&kx, &t, 16u, 40u, ks_init)) == CINT_REFUSED;
        x = ks_make(&kx, &t, 8u, 40u, ks_init);
        kx.vars[1].type = NULL;
        refused &= cint_ctx_bind_state(ctx, x) == CINT_REFUSED;
        kx.vars[1].type = &t;
        kx.vars[1].tag = CINT_TAG_I64;
        refused &= cint_ctx_bind_state(ctx, x) == CINT_REFUSED;
        kx.vars[1].tag = 0x71u;
        kx.vars[1].rank = 1u;
        kx.vars[1].extents = ext;
        refused &= cint_ctx_bind_state(ctx, x) == CINT_REFUSED;
        CHECK(refused, "a cell of 3, a reserved word, a rank of 5, a handle field, a layout too short, a type "
                       "cut short, a struct past its block, no type, another tag, an array of structs");
    }
    CHECK(cint_ctx_bind_state(ctx, ks_make(&kt, &t, 8u, 40u, ks_init)) == CINT_OK && take(ctx, &c1) == CINT_OK,
          "bind and checkpoint");
    ks_state_bytes(&tmp, 7);
    k_checkpoint_bytes(&want, &tmp);
    CHECK(c1.n == want.n && memcmp(c1.b, want.b, want.n) == 0,
          "the bytes of a struct: tag 71, its type, its payload with no padding");
    CHECK(cint_state_render(ctx, &kt.table, text, sizeof text) == 34u &&
              memcmp(text, "state.global app.main count I64 5\n", 34u) == 0,
          "a struct has no state.global line");

    /* The decoder and its text. */
    tmp.n = 0u;
    ks_payload(&tmp, 7);
    CHECK(cint_checkpoint_decode(c1.b, c1.n, &info) == CINT_OK && info.global_count == 2u &&
              cint_checkpoint_global_next(&info, &cur, &g) == CINT_OK && g.elements == NULL &&
              cint_checkpoint_global_next(&info, &cur, &g) == CINT_OK && g.value.len == 1u &&
              g.value.bytes[0] == 0x71u && g.element_count == ks_type_len + 24u &&
              memcmp(g.elements, ks_type, ks_type_len) == 0 && memcmp(g.elements + ks_type_len, tmp.b, 24u) == 0 &&
              cint_checkpoint_global_next(&info, &cur, &g) == CINT_REFUSED,
          "st: its tag, then its type and its payload");
    (void)cint_checkpoint_render(&info, text, sizeof text);
    cint_sha256(tmp.b, 24u, d);
    hex(sh, d, 32u);
    snprintf(line, sizeof line, "\nglobal app.main.count I64 5\nglobal app.main.st app.main.St payload-sha256 %s\n", sh);
    CHECK(strstr(text, line) != NULL, "the text of show: the struct's name and the SHA-256 of its payload");

    /* Restore, into the bound layout. */
    {
        uint8_t *block = (uint8_t *)cint_rt_state(ctx, &kt.state);
        int64_t v = 40;
        CHECK(block != NULL, "the block of app/main.ci");
        if (block != NULL) {
            memcpy(block + 32, &v, 8u);  /* st.tick = 40; little-endian hosts */
        }
    }
    ks_state_bytes(&tmp, 40);
    k_checkpoint_bytes(&want, &tmp);
    CHECK(take(ctx, &c2) == CINT_OK && c2.n == want.n && memcmp(c2.b, want.b, want.n) == 0, "a store to a field");
    hash_of(ctx, h);
    CHECK(cint_ctx_restore(ctx, c1.b, c1.n) == CINT_OK && take(ctx, &tmp) == CINT_OK && tmp.n == c1.n &&
              memcmp(tmp.b, c1.b, c1.n) == 0,
          "restore c1: the fields come back");
    ok = cint_ctx_restore(ctx, c2.b, c2.n) == CINT_OK;
    hash_of(ctx, h1);
    CHECK(ok && memcmp(h, h1, 32u) == 0, "restore c2: the same state");
    {
        cint_ctx *o = k_ctx(&k_prog, NULL);
        const uint8_t *b = NULL;
        int64_t pos5 = 0, a = 0, tick = 0;
        ok = o != NULL && cint_ctx_bind_state(o, ks_make(&kc, &cell, 16u, 96u, cell_init)) == CINT_OK &&
             cint_ctx_restore(o, c2.b, c2.n) == CINT_OK && (b = (const uint8_t *)cint_rt_state(o, &kc.state)) != NULL;
        if (ok) {
            memcpy(&pos5, b + 16 + 40, 8u);
            memcpy(&a, b + 16 + 56, 8u);
            memcpy(&tick, b + 16 + 72, 8u);
        }
        CHECK(ok && pos5 == -6 && b[16 + 48] == 1u && a == -3 && b[16 + 64] == 0xbcu && b[16 + 65] == 2u && tick == 40 &&
                  take(o, &tmp) == CINT_OK && tmp.n == c2.n && memcmp(tmp.b, c2.b, c2.n) == 0,
              "a restore into cells: each field in its cell, signed ones extended, and the same checkpoint back");
        cint_ctx_destroy(o);
        o = k_ctx(&k_prog, NULL);
        CHECK(o != NULL && cint_ctx_bind_state(o, ks_make(&kx, &i32, 8u, 40u, ks_init)) == CINT_OK &&
                  cint_ctx_restore(o, c2.b, c2.n) == CINT_REFUSED,
              "a struct whose type differs is refused");
        cint_ctx_destroy(o);
    }

    /* Malformed structs; every truncation; single-byte changes. */
    st_at = find(&c2, 0u, "app.main.st") + 11u;
    {
        size_t pos_rank = find(&c2, st_at, "pos") + 6u, stopped = st_at + 1u + ks_type_len + 12u;
        ok = 1;
        memcpy(tmp.b, c2.b, c2.n);
        tmp.b[pos_rank] = 5u;
        ok &= cint_checkpoint_decode(tmp.b, c2.n, &info) == CINT_REFUSED;
        memcpy(tmp.b, c2.b, c2.n);
        tmp.b[pos_rank] = 0u;
        ok &= cint_checkpoint_decode(tmp.b, c2.n, &info) == CINT_REFUSED;
        memcpy(tmp.b, c2.b, c2.n);
        tmp.b[st_at + 1u + ks_type_len - 4u] = 0u;  /* the type names definition 0 */
        ok &= cint_checkpoint_decode(tmp.b, c2.n, &info) == CINT_REFUSED;
        memcpy(tmp.b, c2.b, c2.n);
        tmp.b[stopped] = 2u;
        ok &= cint_checkpoint_decode(tmp.b, c2.n, &info) == CINT_OK && cint_ctx_restore(ctx, tmp.b, c2.n) == CINT_REFUSED;
        hash_of(ctx, h1);
        CHECK(ok && memcmp(h, h1, 32u) == 0,
              "a rank of 5 or 0 and a type of the wrong definition are malformed; a Bool of 2 is refused at restore");
    }
    ok = 1;
    for (at = 0u; at < c2.n; at++) {
        ok &= cint_ctx_restore(ctx, c2.b, at) == CINT_REFUSED;
    }
    for (at = st_at - 15u; at < st_at + 1u + ks_type_len + 24u; at++) {
        static const uint8_t masks[3] = {0x01u, 0x80u, 0xffu};
        for (size_t k = 0u; k < 3u; k++) {
            cint_status st;
            memcpy(tmp.b, c2.b, c2.n);
            tmp.b[at] ^= masks[k];
            st = cint_ctx_restore(ctx, tmp.b, c2.n);
            if (st == CINT_OK) {  /* a field changed */
                ok &= at >= st_at + 1u + ks_type_len && cint_ctx_restore(ctx, c2.b, c2.n) == CINT_OK;
            } else {
                hash_of(ctx, h1);
                ok &= st == CINT_REFUSED && memcmp(h, h1, 32u) == 0;
            }
        }
    }
    CHECK(ok, "truncations are refused; a changed byte of a struct's type is refused with the context unchanged, "
              "one of its payload refused or restored");

    /* The change of revision: st of the same type keeps its value in a layout of cells; one whose
     * tick is retyped starts at its initial value. */
    {
        ks_table k2;
        int64_t tick = 0;
        const uint8_t *b;
        CHECK(cint_ctx_change_revision(ctx, &k_prog2, ks_make(&k2, &cell, 16u, 96u, cell_init)) == CINT_OK &&
                  (b = (const uint8_t *)cint_rt_state(ctx, &k2.state)) != NULL &&
                  (memcpy(&tick, b + 16 + 72, 8u), tick == 40) && b[16 + 56] == 0xfdu && b[16 + 57] == 0xffu,
              "a change of revision keeps a struct of the same type through its payload");
        CHECK(cint_ctx_change_revision(ctx, &k_prog, ks_make(&kx, &i32, 8u, 40u, ks_init)) == CINT_OK &&
                  (b = (const uint8_t *)cint_rt_state(ctx, &kx.state)) != NULL && memcmp(b, ks_init, 40u) == 0,
              "and one whose type changed starts at its initial value");
    }
    cint_ctx_destroy(ctx);
}

/* A wide field of a module-level struct (rt/OPEN.md RT-OQ-51): st of struct W { I128 lo; Bool on; }
 * at 8, lo -2^64 - 3 and on true. W's layout is the same in C and in cells (lo at 0, on at 16, 24
 * bytes), so one block serves both: both bind and write the same checkpoint, lo in 16 bytes, and a
 * restore into cells puts lo's limbs in two cells. */
static void test_structs_wide(void)
{
    static ks_table k0, k8, kz;
    static bytes c0, c8, type, payload;
    static const uint32_t layout[3] = {24u, 0u, 16u};
    static const uint8_t init[32] = {5, 0, 0, 0, 0, 0, 0, 0, 0xfd, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                     0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 1, 0, 0, 0, 0, 0, 0, 0};
    static const uint8_t zero[32] = {0};
    cint_state_type t0, t8;
    cint_ctx *a = k_ctx(&k_prog, NULL), *b = k_ctx(&k_prog, NULL), *o = k_ctx(&k_prog, NULL);
    const uint8_t *s = NULL;
    size_t at = SIZE_MAX;
    type.n = payload.n = 0u;
    b_u(&type, 1u, 4u);
    b_u(&type, 0x71u, 1u);
    b_s(&type, "app.main.W");
    b_u(&type, 0u, 1u);
    b_u(&type, 2u, 4u);
    b_s(&type, "lo");
    b_u(&type, 0u, 1u);
    b_u(&type, 0x15u, 1u);  /* I128 */
    b_s(&type, "on");
    b_u(&type, 0u, 1u);
    b_u(&type, CINT_TAG_BOOL, 1u);
    b_u(&type, 0x71u, 1u);
    b_u(&type, 0u, 4u);
    b_u(&payload, 0x71u, 1u);
    b_raw(&payload, type.b, type.n);
    b_raw(&payload, init + 8, 17u);
    t0 = (cint_state_type){type.b, layout, (uint32_t)type.n, 3u, 0u, 0u};
    t8 = (cint_state_type){type.b, layout, (uint32_t)type.n, 3u, 8u, 0u};
    if (a == NULL || b == NULL || o == NULL) {
        CHECK(0, "cint_ctx_create");
    } else {
        CHECK(cint_ctx_bind_state(a, ks_make(&k0, &t0, 8u, 32u, init)) == CINT_OK &&
                  cint_ctx_bind_state(b, ks_make(&k8, &t8, 8u, 32u, init)) == CINT_OK,
              "a struct with an I128 field binds in C's layout and in cells");
        CHECK(take(a, &c0) == CINT_OK && take(b, &c8) == CINT_OK && c0.n == c8.n && memcmp(c0.b, c8.b, c0.n) == 0 &&
                  (at = find(&c0, 0u, "app.main.st")) != SIZE_MAX && at + 11u + payload.n <= c0.n &&
                  memcmp(c0.b + at + 11u, payload.b, payload.n) == 0,
              "the same checkpoint from both: tag 71, W, lo in 16 bytes and on in 1");
        CHECK(cint_ctx_bind_state(o, ks_make(&kz, &t8, 8u, 32u, zero)) == CINT_OK &&
                  cint_ctx_restore(o, c0.b, c0.n) == CINT_OK && (s = (const uint8_t *)cint_rt_state(o, &kz.state)) != NULL &&
                  memcmp(s, init, 32u) == 0,
              "a restore into cells: lo's limbs in two cells and on in the third");
    }
    cint_ctx_destroy(a);
    cint_ctx_destroy(b);
    cint_ctx_destroy(o);
}

/* ------------------------------------------------------------------------- */
/* A seeded fuzz target over the checkpoint decoder and restore (OQ-14), modeled on test_record.c's. */

/* rt/tests/test_rt.c: a fuzz target's input digest and receipt line (test_rt.py --fuzz-receipt). */
void test_fuzz_input(uint8_t digest[32], const uint8_t *p, size_t n);
void test_fuzz_line(const char *target, uint64_t seed, unsigned count, unsigned accepted, unsigned rejected,
                    unsigned failures, const uint8_t digest[32]);

typedef struct fz_seed {
    cint_ctx *ctx;     /* the context its mutations restore into */
    bytes c;           /* its checkpoint */
    uint8_t hash[32];  /* the context's state hash */
} fz_seed;

/* The seed checkpoints, each of k_prog: 0 holds the registry, arena and pool blocks; 1 a fault
 * record of version 2 inside the state; 2 one of version 3 (BX15-07 C); 3 a struct of tag 71. */
static bool fz_seeds(fz_seed s[4], ks_table *kt, const cint_state_type *t)
{
    static uint8_t buf16[16];
    static const cint_type i32 = {CINT_TAG_I32, 0u, 0u, 0u, 0u, 0u, 0u};
    cint_buffer_id id = 0u;
    cint_handle h, hp;
    cint_fault_record *r;
    uint64_t gen = 0u;
    uint32_t child = 0u;
    int32_t v = 10;
    bool ok = true;
    for (unsigned i = 0u; i < 4u; i++) {
        s[i].ctx = k_ctx(&k_prog, NULL);
        ok = ok && s[i].ctx != NULL &&
             cint_ctx_bind_state(s[i].ctx, i < 3u ? &k_table : ks_make(kt, t, 8u, 40u, ks_init)) == CINT_OK;
    }
    ok = ok && cint_buffer_register_bytes(s[0].ctx, buf16, 16, CINT_VIEW_READ, &id) == CINT_OK &&
         cint_buffer_create(s[0].ctx, i32, 4, 0u, &id, &gen) == CINT_OK &&
         cint_mem_alloc(s[0].ctx, K_OP, &k_mem, 1u, 2, false, &h, NULL) &&
         cint_mem_child(s[0].ctx, K_OP, &k_mem, 1u, 4, &child) && cint_mem_alloc(s[0].ctx, K_OP, &k_mem, 2u, 1, false, &h, NULL) &&
         cint_mem_insert(s[0].ctx, K_OP, &k_mem, 1u, &v, false, &hp, NULL) &&
         cint_mem_insert(s[0].ctx, K_OP, &k_mem, 1u, &v, false, &h, NULL) && cint_mem_remove(s[0].ctx, K_OP, &k_mem, 1u, hp);
    ok = ok && cint_rt_entry_open(s[1].ctx, K_OP, 10, 8) == CINT_OK && cint_rt_call_enter(s[1].ctx, K_CALL) &&
         !cint_fuel_charge(s[1].ctx, K_FUEL, 11u) && cint_rt_entry_end(s[1].ctx) == CINT_FAULT;
    /* dot over I1024 elements: 2^62 products of MIN(I1024) with itself, a Z of 264 bytes. */
    r = ok ? cint_rt_record_begin(s[2].ctx, K_OP, (uint16_t)CINT_E_OVERFLOW) : NULL;
    if (r != NULL) {
        cint_rt_record_op(r, "dot");
        cint_rt_record_op(r, "checked");
        cint_rt_record_op(r, "i1024");
        cint_rt_record_op(r, "i1024");
        cint_rt_record_operand(r, CINT_TAG_I64, (uint64_t)1u << 62);
        r->has_exact = 1u;
        r->exact.len = 5u + 264u;
        memset(r->exact.bytes, 0, sizeof r->exact.bytes);
        r->exact.bytes[0] = CINT_TAG_Z;
        r->exact.bytes[1] = 8u;
        r->exact.bytes[2] = 1u;
        r->exact.bytes[5u + 263u] = 0x10u;
        r->has_limit = 1u;
        r->limit.len = 129u;
        r->limit.bytes[0] = 0x18u;  /* I1024, rt/cint_wide.h */
        memset(r->limit.bytes + 1, 0xff, 128u);
        r->limit.bytes[128] = 0x7fu;
    }
    ok = ok && r != NULL;
    for (unsigned i = 0u; ok && i < 4u; i++) {
        ok = take(s[i].ctx, &s[i].c) == CINT_OK && cint_ctx_state_hash(s[i].ctx, s[i].hash) == CINT_OK;
    }
    return ok && find(&s[0].c, 0u, "cint-core-1/registry/v1") != SIZE_MAX &&
           find(&s[0].c, 0u, "cint-core-1/arena/v2") != SIZE_MAX && find(&s[0].c, 0u, "cint-core-1/pool/v2") != SIZE_MAX &&
           find(&s[1].c, 0u, "cint-core-1/fault/v2") != SIZE_MAX && find(&s[2].c, 0u, "cint-core-1/fault/v3") != SIZE_MAX &&
           find(&s[3].c, 0u, "app.main.St") != SIZE_MAX;
}

/* Each mutation goes to the decoder, and its text when decoded, and to a restore into its seed's
 * context: a refused restore leaves the state hash unchanged; one that restores is of bytes the
 * decoder accepts, and the seed restores after it. */
static void fuzz(void)
{
    static const uint32_t words[4] = {0u, 0x7fffffffu, 0x80000000u, 0xffffffffu};
    const uint64_t seed = 0x13c1a5edu;
    const unsigned count = 3000u;
    static fz_seed s[4];
    static ks_table kt;
    static bytes copy;
    static char text[8192];
    cint_state_type t;
    cint_checkpoint_info info;
    uint64_t x = seed;
    uint8_t digest[32] = {0}, h[32];
    unsigned accepted = 0u, failures = 0u;
    ks_type_len = ks_type_of(ks_type, CINT_TAG_I64, 2u);
    t = (cint_state_type){ks_type, ks_c_layout, ks_type_len, 8u, 0u, 0u};
    if (!fz_seeds(s, &kt, &t)) {
        CHECK(0, "the fuzz target's checkpoints");
    } else {
        for (unsigned i = 0u; i < count; i++) {
            fz_seed *z;
            unsigned edits;
            bool decoded;
            cint_status st;
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
            z = &s[x % 4u];
            edits = 1u + (unsigned)((x >> 8) % 3u);
            memcpy(copy.b, z->c.b, z->c.n);
            copy.n = z->c.n;
            for (unsigned k = 0u; k < edits; k++) {
                size_t at;
                x ^= x << 13;
                x ^= x >> 7;
                x ^= x << 17;
                at = (size_t)((x >> 8) % copy.n);
                switch (x % 5u) {
                case 0u: copy.b[at] ^= (uint8_t)(1u << ((x >> 40) % 8u)); break;
                case 1u: copy.b[at] = (uint8_t)(x >> 32); break;
                case 2u: copy.b[at] = (uint8_t)(copy.b[at] + 1u); break;
                case 3u:  /* a U32 length, count or half of a U64 */
                    if (at + 4u <= copy.n) {
                        uint32_t w = words[(x >> 40) % 4u];
                        for (unsigned j = 0u; j < 4u; j++) {
                            copy.b[at + j] = (uint8_t)(w >> (8u * j));
                        }
                    }
                    break;
                default: copy.n = at > 0u ? at : 1u; break;
                }
            }
            test_fuzz_input(digest, copy.b, copy.n);
            decoded = cint_checkpoint_decode(copy.b, copy.n, &info) == CINT_OK;
            if (decoded) {
                accepted++;
                (void)cint_checkpoint_render(&info, text, sizeof text);
            }
            st = cint_ctx_restore(z->ctx, copy.b, copy.n);
            if (st == CINT_OK) {
                failures += !decoded || cint_ctx_restore(z->ctx, z->c.b, z->c.n) != CINT_OK ? 1u : 0u;
                hash_of(z->ctx, z->hash);
            } else {
                hash_of(z->ctx, h);
                failures += st != CINT_REFUSED || memcmp(h, z->hash, 32u) != 0 ? 1u : 0u;
            }
        }
        test_fuzz_line("checkpoint", seed, count, accepted, count - accepted, failures, digest);
        CHECK(failures == 0u && accepted > 0u, "every mutated checkpoint is refused with the context unchanged, or restores");
    }
    for (unsigned i = 0u; i < 4u; i++) {
        cint_ctx_destroy(s[i].ctx);
    }
}

void test_state(long *pass, long *fail)
{
    test_layout();
    test_restore();
    test_faulted();
    test_interrupt_revision();
    test_mem();
    test_mem_types();
    test_arrays();
    test_carry();
    test_structs();
    test_structs_wide();
    fuzz();
    *pass += s_pass;
    *fail += s_fail;
}
