/* khost_host.c: dispatches the exported kernels of compiler/tests/qbe/khost.ci from a C host
 * through their cx wrappers (SPEC-02 K-15; SPEC-03 A-12, A-13; SIMD-7), the way
 * rt/tests/roundtrip/host.c drives touch.ci. compiler/tests/qbe_kernels.py links it once with
 * the program cpu-c17 writes and once with the program cpu-qbe writes, against one runtime
 * object, and requires the two transcripts to be equal byte for byte.
 *
 * Each call prints one line: its name, the status, the refusal reason, the fuel the entry
 * consumed, the canonical bytes of the fault record in hex (none when the context holds no
 * fault), then the words of the written views and the scalar `out` cells with the guard words
 * around them, so a publication on a fault, a write of a refused call, and an `out` written
 * wider than its C type all show.
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "cint_rt.h"

extern const cint_module_info cm_5_khost;
cint_status cx_5_khost_6_kscale(cint_ctx *ctx, int64_t fuel, cint_view a, int32_t k, cint_view b, int32_t *t32,
                                int64_t *t64);
cint_status cx_5_khost_5_kbump(cint_ctx *ctx, int64_t fuel, cint_view a, int8_t d, int64_t *c);
cint_status cx_5_khost_5_khuge(cint_ctx *ctx, int64_t fuel, cint_view x, int64_t *s);
cint_status cx_5_khost_5_kramp(cint_ctx *ctx, int64_t fuel, int64_t base, cint_view y);

#define GUARD32 INT32_C(0x5a5a5a5a)
#define GUARD64 INT64_C(0x5a5a5a5a5a5a5a5a)

static cint_view view(cint_buffer_id id, uint16_t code, int64_t h, int64_t w, uint8_t perm)
{
    cint_view v;
    memset(&v, 0, sizeof v);
    v.buffer = id;
    v.generation = CINT_BUFFER_GENERATION_FIRST;
    v.type.code = code;
    v.rank = w > 0 ? 2 : 1;
    v.perm = perm;
    v.shape[0] = h;
    v.stride[0] = w > 0 ? w : 1;
    if (w > 0) {
        v.shape[1] = w;
        v.stride[1] = 1;
    }
    return v;
}

/* A writable host buffer that a dispatch publishes into by copy (SPEC-03 P-15): a borrowed
 * registration without it refuses an out or inout view (F-5 check 8, bind.limit). */
static cint_status reg(cint_ctx *ctx, void *base, uint16_t code, int64_t n, cint_buffer_id *id)
{
    cint_buffer_desc d;
    uint64_t gen = 0;
    memset(&d, 0, sizeof d);
    d.size = (uint32_t)sizeof d;
    d.writable = 1u;
    d.type.code = code;
    d.mem.kind = CINT_MEM_HOST;
    d.mem.u.host.ptr = base;
    d.extent = n;
    d.publish = CINT_PUBLISH_COPY;
    return cint_buffer_register(ctx, &d, id, &gen);
}

static void line(cint_ctx *ctx, const char *name, cint_status st)
{
    uint8_t buf[2048];
    size_t len = 0, i;
    int64_t fuel = -1;
    uint32_t reason = 0;
    (void)cint_fuel_consumed(ctx, &fuel);
    (void)cint_ctx_refusal(ctx, &reason);
    printf("%s status %d refusal %" PRIu32 " fuel %" PRId64 " fault ", name, (int)st, reason, fuel);
    if (cint_fault_get(ctx, buf, sizeof buf, &len) == CINT_OK && len > 0 && len <= sizeof buf) {
        for (i = 0; i < len; i++) printf("%02x", buf[i]);
        (void)cint_ctx_clear_fault(ctx);
    } else {
        printf("none");
    }
}

static void words32(const char *what, const int32_t *p, int n)
{
    int i;
    printf(" %s", what);
    for (i = 0; i < n; i++) printf(" %" PRId32, p[i]);
}

static void words64(const char *what, const int64_t *p, int n)
{
    int i;
    printf(" %s", what);
    for (i = 0; i < n; i++) printf(" %" PRId64, p[i]);
}

int main(void)
{
    int32_t a[6] = {1, 2, 3, 4, 5, 6}, big[6] = {1, 2, 2147483647, 4, 5, 6};
    int32_t b[8], small[6];
    int32_t o32[3];
    int64_t o64[3], k64[6], kc[3];
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL;
    cint_buffer_id a_id = 0, big_id = 0, b_id = 0, small_id = 0, gone = 0, ro_id = 0, k_id = 0;
    cint_status st;
    int i;
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = cm_5_khost.program;
    if (cint_ctx_create(&cfg, &ctx) != CINT_OK) return 3;
    if (cint_buffer_register_bytes(ctx, a, (int64_t)sizeof a, CINT_VIEW_READ, &a_id) != CINT_OK ||
        cint_buffer_register_bytes(ctx, big, (int64_t)sizeof big, CINT_VIEW_READ, &big_id) != CINT_OK ||
        reg(ctx, b, CINT_TAG_I32, 8, &b_id) != CINT_OK || reg(ctx, small, CINT_TAG_I32, 6, &small_id) != CINT_OK ||
        cint_buffer_register_bytes(ctx, a, (int64_t)sizeof a, CINT_VIEW_READ, &gone) != CINT_OK ||
        cint_buffer_release(ctx, gone) != CINT_OK ||
        cint_buffer_register_bytes(ctx, b, (int64_t)sizeof b, CINT_VIEW_READ, &ro_id) != CINT_OK ||
        reg(ctx, k64, CINT_TAG_I64, 6, &k_id) != CINT_OK) {
        return 3;
    }
    cint_view av = view(a_id, CINT_TAG_I32, 2, 3, CINT_VIEW_READ), bv = view(b_id, CINT_TAG_I32, 2, 3, CINT_VIEW_WRITE);
    struct { const char *name; cint_view a, b; int32_t k; int64_t fuel; int null64; } calls[10];
    for (i = 0; i < 10; i++) {
        calls[i].a = av;
        calls[i].b = bv;
        calls[i].k = 3;
        calls[i].fuel = CINT_FUEL_UNBOUNDED;
        calls[i].null64 = 0;
    }
    calls[0].name = "kscale";
    calls[1].name = "kscale_overflow";
    calls[1].a = view(big_id, CINT_TAG_I32, 2, 3, CINT_VIEW_READ);
    calls[2].name = "kscale_shape";
    calls[2].b = view(small_id, CINT_TAG_I32, 3, 2, CINT_VIEW_WRITE);
    calls[3].name = "kscale_null_out";
    calls[3].null64 = 1;
    calls[4].name = "kscale_alias";
    calls[4].a = view(ro_id, CINT_TAG_I32, 2, 3, CINT_VIEW_READ);
    calls[5].name = "kscale_stale";
    calls[5].a = view(gone, CINT_TAG_I32, 2, 3, CINT_VIEW_READ);
    calls[6].name = "kscale_read_view_for_out";
    calls[6].b = view(ro_id, CINT_TAG_I32, 2, 3, CINT_VIEW_READ);
    calls[7].name = "kscale_type";
    calls[7].a = view(a_id, CINT_TAG_I64, 1, 3, CINT_VIEW_READ);
    calls[8].name = "kscale_fuel";
    calls[8].fuel = 3;
    calls[9].name = "kscale_negative";
    calls[9].k = -7;
    for (i = 0; i < 10; i++) {
        int j;
        for (j = 0; j < 8; j++) b[j] = GUARD32;
        for (j = 0; j < 6; j++) small[j] = GUARD32;
        o32[0] = o32[2] = GUARD32;
        o32[1] = -1;
        o64[0] = o64[2] = GUARD64;
        o64[1] = -1;
        st = cx_5_khost_6_kscale(ctx, calls[i].fuel, calls[i].a, calls[i].k, calls[i].b, &o32[1],
                                 calls[i].null64 ? NULL : &o64[1]);
        line(ctx, calls[i].name, st);
        words32("b", b, 8);
        words32("small", small, 6);
        words32("t32", o32, 3);
        words64("t64", o64, 3);
        printf("\n");
    }
    for (i = 0; i < 3; i++) {
        static const int8_t ds[3] = {-1, 5, -128};
        int j;
        for (j = 0; j < 6; j++) k64[j] = j + 1;
        kc[0] = kc[2] = GUARD64;
        kc[1] = -1;
        st = cx_5_khost_5_kbump(ctx, CINT_FUEL_UNBOUNDED, view(k_id, CINT_TAG_I64, 6, 0, CINT_VIEW_WRITE), ds[i],
                                &kc[1]);
        line(ctx, i == 0 ? "kbump" : i == 1 ? "kbump_up" : "kbump_min", st);
        words64("a", k64, 6);
        words64("c", kc, 3);
        printf("\n");
    }
    {
        /* N = 8192^5 saturates at the largest I64; an allowance of 10 faults at its charge. */
        static int64_t huge[8192];
        cint_buffer_id h_id = 0;
        if (cint_buffer_register_bytes(ctx, huge, (int64_t)sizeof huge, CINT_VIEW_READ, &h_id) != CINT_OK) return 3;
        kc[0] = kc[2] = GUARD64;
        kc[1] = -1;
        st = cx_5_khost_5_khuge(ctx, 10, view(h_id, CINT_TAG_I64, 8192, 0, CINT_VIEW_READ), &kc[1]);
        line(ctx, "khuge_fuel", st);
        words64("s", kc, 3);
        printf("\n");
    }
    for (i = 0; i < 2; i++) {
        /* No scalar `out`; the second base overflows at work-item 1, so nothing is published. */
        int j;
        for (j = 0; j < 6; j++) k64[j] = GUARD64;
        st = cx_5_khost_5_kramp(ctx, CINT_FUEL_UNBOUNDED, i == 0 ? 100 : INT64_MAX,
                                view(k_id, CINT_TAG_I64, 2, 3, CINT_VIEW_WRITE));
        line(ctx, i == 0 ? "kramp" : "kramp_overflow", st);
        words64("y", k64, 6);
        printf("\n");
    }
    cint_ctx_destroy(ctx);
    return 0;
}
