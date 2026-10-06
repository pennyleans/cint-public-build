/* host.c: drives rt/tests/roundtrip/touch.ci through its cint-abi-1 wrapper
 * (slice 2 task 2.6, tests a to c; SPEC-03 A-8, A-12, H-12, H-13; slice 2
 * decision patch D-3).
 *
 *   (a) a table of Tok rows changes in place, and every byte is checked;
 *   (b) each descriptor refusal of cint_view_bind, with its reason, and the
 *       rows left unchanged;
 *   (c) E_SHAPE and E_ALIAS at entry.
 *
 * Prints one "<name>: ok" or "<name>: FAIL ..." line per check and a summary;
 * exit 0 when every check passed.
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "cint_rt.h"

extern const cint_module_info cm_5_touch;
cint_status cx_5_touch_5_touch(cint_ctx *ctx, int64_t fuel, cint_view t, cint_view w);

#define N 5
#define ROW 12

static int passed, total;

static void check(const char *name, int ok, const char *detail)
{
    total++;
    passed += ok != 0;
    printf("%s: %s%s\n", name, ok ? "ok" : "FAIL ", ok ? "" : detail);
}

static cint_view view(cint_buffer_id id, uint16_t code, uint32_t record, int64_t n, uint8_t perm)
{
    cint_view v;
    memset(&v, 0, sizeof v);
    v.buffer = id;
    v.generation = CINT_BUFFER_GENERATION_FIRST;
    v.type.code = code;
    v.type.record_id = record;
    v.rank = 1;
    v.perm = perm;
    v.shape[0] = n;
    v.stride[0] = 1;
    return v;
}

static void put32(uint8_t *p, int32_t v)
{
    memcpy(p, &v, sizeof v);
}

static int32_t get32(const uint8_t *p)
{
    int32_t v;
    memcpy(&v, p, sizeof v);
    return v;
}

int main(void)
{
    static uint64_t store[N * ROW / 8 + 1];   /* 8-aligned rows */
    uint8_t *rows = (uint8_t *)store, want[N * ROW], before[N * ROW];
    int64_t w[N] = {10, -3, 0, 2147483646, 7};
    const cint_record_layout *rec = &cm_5_touch.records[0];
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL;
    cint_buffer_id t_id = 0, w_id = 0, gone = 0, alias_id = 0;
    cint_fault_record f;
    uint32_t reason = 0;
    char d[160];
    int i;
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = cm_5_touch.program;
    if (cint_ctx_create(&cfg, &ctx) != CINT_OK) return 3;
    check("record table", rec->bytes == ROW && rec->align == 4u && rec->field_count == 3u &&
                              rec->fields[0].offset == 0u && rec->fields[1].offset == 4u && rec->fields[2].offset == 8u &&
                              rec->fields[2].count == 3u && rec->fields[2].type.code == CINT_TAG_U8,
          "layout");
    for (i = 0; i < N * ROW; i++) rows[i] = (uint8_t)(i * 7 + 3);
    for (i = 0; i < N; i++) {
        put32(rows + i * ROW, -100 - i);
        put32(rows + i * ROW + 4, 1000 * i - 2);
        rows[i * ROW + 8] = (uint8_t)(i + 1);
        rows[i * ROW + 10] = (uint8_t)(2 * i);
    }
    memcpy(want, rows, sizeof want);
    for (i = 0; i < N; i++) {
        put32(want + i * ROW, (int32_t)w[i] + 1);
        put32(want + i * ROW + 4, (1000 * i - 2) * 2);
        want[i * ROW + 9] = (uint8_t)(i + 1 + 2 * i);
    }
    if (cint_buffer_register_bytes(ctx, rows, N * ROW, CINT_VIEW_WRITE, &t_id) != CINT_OK ||
        cint_buffer_register_bytes(ctx, w, (int64_t)sizeof w, CINT_VIEW_READ, &w_id) != CINT_OK ||
        cint_buffer_register_bytes(ctx, w, (int64_t)sizeof w, CINT_VIEW_READ, &gone) != CINT_OK ||
        cint_buffer_release(ctx, gone) != CINT_OK ||
        cint_buffer_register_bytes(ctx, rows, N * ROW, CINT_VIEW_READ, &alias_id) != CINT_OK) {
        return 3;
    }
    cint_view t = view(t_id, CINT_TAG_RECORD, 0, N, CINT_VIEW_WRITE), wv = view(w_id, CINT_TAG_I64, 0, N, CINT_VIEW_READ);

    /* (b) refusals first: each leaves the rows as they were. */
    struct { const char *name; cint_view t, w; uint32_t reason; } bad[9];
    memcpy(before, rows, sizeof before);
    for (i = 0; i < 9; i++) { bad[i].t = t; bad[i].w = wv; }
    bad[0].name = "unknown id"; bad[0].t.buffer = 999; bad[0].reason = CINT_REFUSAL_BUFFER;
    bad[1].name = "stale generation"; bad[1].w.buffer = gone; bad[1].reason = CINT_REFUSAL_GENERATION;
    bad[2].name = "wrong type tag"; bad[2].t.type.code = CINT_TAG_I64; bad[2].reason = CINT_REFUSAL_TYPE;
    bad[3].name = "wrong record id"; bad[3].t.type.record_id = 1; bad[3].reason = CINT_REFUSAL_TYPE;
    bad[4].name = "rank 2"; bad[4].t.rank = 2; bad[4].t.shape[1] = 1; bad[4].t.stride[1] = 1; bad[4].reason = CINT_REFUSAL_RANK;
    bad[5].name = "stride 2"; bad[5].t.stride[0] = 2; bad[5].reason = CINT_REFUSAL_STRIDE;
    bad[6].name = "extent past the buffer"; bad[6].t.shape[0] = N + 1; bad[6].w.shape[0] = N + 1;
    bad[6].reason = CINT_REFUSAL_EXTENT;
    bad[7].name = "read view for inout"; bad[7].t.perm = CINT_VIEW_READ; bad[7].reason = CINT_REFUSAL_PERMISSION;
    bad[8].name = "byte size overflow"; bad[8].t.origin = INT64_MAX / 4; bad[8].reason = CINT_REFUSAL_SIZE;
    for (i = 0; i < 9; i++) {
        cint_status st = cx_5_touch_5_touch(ctx, CINT_FUEL_UNBOUNDED, bad[i].t, bad[i].w);
        int got = cint_ctx_refusal(ctx, &reason) == CINT_OK ? (int)reason : -1;
        snprintf(d, sizeof d, "status %d reason %d, want %d %u", (int)st, got, (int)CINT_REFUSED, bad[i].reason);
        check(bad[i].name, st == CINT_REFUSED && got == (int)bad[i].reason && memcmp(rows, before, sizeof before) == 0, d);
    }

    /* (c) E_SHAPE (w is one shorter than t) and E_ALIAS (w lies over the rows). */
    cint_view shorter = wv, over = view(alias_id, CINT_TAG_I64, 0, N, CINT_VIEW_READ);
    shorter.shape[0] = N - 1;
    cint_status st = cx_5_touch_5_touch(ctx, CINT_FUEL_UNBOUNDED, t, shorter);
    snprintf(d, sizeof d, "status %d", (int)st);
    check("E_SHAPE", st == CINT_FAULT && cint_ctx_fault(ctx, &f) == CINT_OK && f.code == CINT_E_SHAPE &&
                         memcmp(rows, before, sizeof before) == 0 && cint_ctx_clear_fault(ctx) == CINT_OK, d);
    st = cx_5_touch_5_touch(ctx, CINT_FUEL_UNBOUNDED, t, over);
    snprintf(d, sizeof d, "status %d", (int)st);
    check("E_ALIAS", st == CINT_FAULT && cint_ctx_fault(ctx, &f) == CINT_OK && f.code == CINT_E_ALIAS &&
                         memcmp(rows, before, sizeof before) == 0 && cint_ctx_clear_fault(ctx) == CINT_OK, d);

    /* (a) the round trip, after the refusals and faults. */
    st = cx_5_touch_5_touch(ctx, CINT_FUEL_UNBOUNDED, t, wv);
    for (i = 0; i < N * ROW && rows[i] == want[i]; i++) {
    }
    snprintf(d, sizeof d, "status %d, first differing byte %d (row %d), kind %" PRId32, (int)st, i, i / ROW,
             i < N * ROW ? get32(rows + (i / ROW) * ROW) : 0);
    check("round trip, every byte", st == CINT_OK && i == N * ROW, d);
    cint_ctx_destroy(ctx);
    printf("roundtrip: %d of %d checks passed\n", passed, total);
    return passed == total ? 0 : 1;
}
