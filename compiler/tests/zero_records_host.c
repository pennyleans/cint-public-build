#include <stdio.h>
#include <string.h>
#include "cint_rt.h"
#include "zero_paths_test.h"

#define CX(name) cx_15_zero_x5Frecords_##name
extern const cint_module_info cm_15_zero_x5Frecords;
cint_status CX(4_echo)(cint_ctx *, int64_t, cint_view, void *);
cint_status CX(7_cleared)(cint_ctx *, int64_t, void *);
cint_status CX(14_scalar_x5Fcopy)(cint_ctx *, int64_t, cint_view, cint_view);
cint_status CX(13_view_x5Fcount)(cint_ctx *, int64_t, cint_view, int64_t *);
cint_status CX(12_view_x5Fcopy)(cint_ctx *, int64_t, cint_view, cint_view, int64_t *);
cint_status CX(13_view_x5Findex)(cint_ctx *, int64_t, cint_view, int64_t, int64_t *);
cint_status CX(13_view_x5Fwrite)(cint_ctx *, int64_t, cint_view, int64_t);
cint_status CX(21_scalar_x5Fand_x5Fview)(cint_ctx *, int64_t, cint_view, cint_view, int64_t *);
cint_status CX(12_read_x5Fpair)(cint_ctx *, int64_t, cint_view, cint_view, int64_t *);
cint_status CX(13_mixed_x5Fecho)(cint_ctx *, int64_t, cint_view, void *);

typedef struct { _Alignas(8) uint8_t tag; uint8_t pad[7]; uint8_t tail; uint8_t end[7]; } Mixed;
static int passed, total;
static void check(const char *name, int ok)
{
    total++; passed += ok != 0;
    if (!ok) { fprintf(stderr, "FAILED %s\n", name); }
}
static cint_view view(cint_buffer_id id, uint32_t record, int64_t origin, int64_t n)
{
    cint_view v = {0};
    v.buffer = id; v.generation = CINT_BUFFER_GENERATION_FIRST;
    v.type.code = CINT_TAG_RECORD; v.type.record_id = record;
    v.rank = 1; v.perm = CINT_VIEW_WRITE; v.origin = origin;
    v.shape[0] = n; v.stride[0] = 1;
    return v;
}
static int refused(cint_ctx *ctx, cint_status status, uint32_t expected, int64_t before)
{
    uint32_t reason = 0;
    int64_t fuel = -1;
    return status == CINT_REFUSED && cint_ctx_refusal(ctx, &reason) == CINT_OK && reason == expected &&
           cint_fuel_consumed(ctx, &fuel) == CINT_OK && fuel == before;
}
static int fault(cint_ctx *ctx, uint16_t code, const char *op, int64_t fuel)
{
    static cint_fault_record r;
    int64_t used = -1;
    int ok = cint_ctx_fault(ctx, &r) == CINT_OK && r.code == code && r.operation_len == strlen(op) &&
             memcmp(r.operation, op, r.operation_len) == 0 && r.position.index != 0u &&
             cint_fuel_consumed(ctx, &used) == CINT_OK && used == fuel;
    return ok && cint_ctx_clear_fault(ctx) == CINT_OK;
}
int main(void)
{
    int path_status = zero_paths();
    const cint_module_info *cm = &cm_15_zero_x5Frecords;
    const cint_record_layout *r = cm->records;
    cint_ctx_config config = {0};
    cint_ctx *ctx = NULL;
    cint_buffer_id ai = 0, bi = 0, ei = 0, hi = 0, mi = 0, old = 0;
    cint_view a, b, v, empty;
    uint8_t sentinel = 0xA5;
    int64_t result = 99, before = -1;
    Mixed input = {0}, output;
    config.size = (uint32_t)sizeof config; config.program = cm->program;
    if (cint_ctx_create(&config, &ctx) != CINT_OK) { return 1; }
    check("complete catalog", cm->record_count == 6u);
    if (cm->record_count != 6u) { cint_ctx_destroy(ctx); return 1; }
    check("zero scalar field layout", r[0].bytes == 0u && r[0].align == 8u && r[0].field_count == 1u &&
          r[0].fields[0].offset == 0u && r[0].fields[0].count == 0u && r[0].fields[0].type.code == CINT_TAG_I64);
    check("nominal zero type", r[1].bytes == 0u && r[1].align == 8u && r[1].field_count == 1u);
    check("nested zero fields", r[2].bytes == 0u && r[2].align == 8u && r[2].field_count == 2u &&
          r[2].fields[0].count == 1u && r[2].fields[1].count == 3u &&
          r[2].fields[0].offset == 0u && r[2].fields[1].offset == 0u &&
          r[2].fields[0].type.record_id == 0u && r[2].fields[1].type.record_id == 0u);
    check("positive parent layout", sizeof(Mixed) == 16u && r[3].bytes == 16u && r[3].align == 8u &&
          r[3].fields[0].offset == 0u && r[3].fields[1].offset == 8u && r[3].fields[2].offset == 8u &&
          r[3].fields[2].count == 2u && r[3].fields[3].offset == offsetof(Mixed, tail));
    check("unused and imported zero types", r[4].bytes == 0u && r[4].align == 8u &&
          r[4].fields[0].count == 4u && r[4].fields[0].type.record_id == 2u &&
          r[5].bytes == 0u && r[5].align == 1u && r[5].fields[0].type.code == CINT_TAG_BOOL &&
          r[5].fields[0].count == 0u);
    a = view(0, 0, 0, 4);
    check("finite logical registrations", cint_buffer_register_elements(ctx, &sentinel, &a.type, 0, 4, CINT_VIEW_WRITE, &ai) == CINT_OK &&
          cint_buffer_register_elements(ctx, &sentinel, &a.type, 0, 4, CINT_VIEW_WRITE, &bi) == CINT_OK &&
          cint_buffer_register_elements(ctx, NULL, &a.type, 0, 0, CINT_VIEW_WRITE, &ei) == CINT_OK &&
          cint_buffer_register_elements(ctx, NULL, &a.type, 0, INT64_MAX, CINT_VIEW_WRITE, &hi) == CINT_OK &&
          cint_buffer_register_bytes(ctx, NULL, 0, CINT_VIEW_WRITE, &old) == CINT_OK);
    a = view(ai, 0, 1, 2); b = view(bi, 0, 0, 2); empty = view(ei, 0, 0, 0);
    check("logical count forwarded", CX(13_view_x5Fcount)(ctx, -1, a, &result) == CINT_OK && result == 2);
    check("full I64 logical capacity", CX(13_view_x5Fcount)(ctx, -1, view(hi, 0, 0, INT64_MAX), &result) == CINT_OK && result == INT64_MAX);
    check("empty logical allocation", CX(13_view_x5Fcount)(ctx, -1, empty, &result) == CINT_OK && result == 0);
    check("last logical element", CX(13_view_x5Findex)(ctx, -1, view(ai, 0, 3, 1), 0, &result) == CINT_OK && result == 7);
    check("last high-origin element", CX(13_view_x5Findex)(ctx, -1, view(hi, 0, INT64_MAX - 1, 1), 0, &result) == CINT_OK && result == 7);
    check("zero payload write", CX(13_view_x5Fwrite)(ctx, -1, view(ai, 0, 0, 4), 3) == CINT_OK && sentinel == 0xA5);
    check("distinct registrations at same address", CX(12_view_x5Fcopy)(ctx, -1, a, b, &result) == CINT_OK && result == 2 && sentinel == 0xA5);
    check("same registration disjoint intervals", CX(12_view_x5Fcopy)(ctx, -1, view(ai, 0, 0, 2), view(ai, 0, 2, 2), &result) == CINT_OK && result == 2);
    result = 99;
    check("partial logical overlap", CX(12_view_x5Fcopy)(ctx, -1, a, view(ai, 0, 2, 2), &result) == CINT_FAULT &&
          fault(ctx, CINT_E_ALIAS, "bind.alias", 0) && result == 99);
    check("shape before alias", CX(12_view_x5Fcopy)(ctx, -1, a, view(ai, 0, 1, 1), &result) == CINT_FAULT &&
          fault(ctx, CINT_E_SHAPE, "bind.shape", 0) && result == 99);
    check("readonly overlap permitted", CX(12_read_x5Fpair)(ctx, -1, a, a, &result) == CINT_OK && result == 4);
    check("empty interval has no alias", CX(21_scalar_x5Fand_x5Fview)(ctx, -1, view(ai, 0, 0, 1), view(ai, 0, 0, 0), &result) == CINT_OK && result == 0);
    check("scalar inout copy", CX(14_scalar_x5Fcopy)(ctx, -1, view(ai, 0, 0, 1), view(ai, 0, 1, 1)) == CINT_OK && sentinel == 0xA5);
    check("zero public result", CX(4_echo)(ctx, -1, view(ai, 0, 0, 1), &sentinel) == CINT_OK && sentinel == 0xA5);
    check("result pointer", cint_fuel_consumed(ctx, &before) == CINT_OK &&
          refused(ctx, CX(4_echo)(ctx, -1, view(ai, 0, 0, 1), NULL), CINT_REFUSAL_RESULT, before));
    check("zero result cleared", CX(7_cleared)(ctx, -1, &sentinel) == CINT_OK && sentinel == 0xA5);
    check("zero result pointer", cint_fuel_consumed(ctx, &before) == CINT_OK &&
          refused(ctx, CX(7_cleared)(ctx, -1, NULL), CINT_REFUSAL_RESULT, before));
    result = 99;
    check("logical bounds", CX(13_view_x5Findex)(ctx, -1, a, 2, &result) == CINT_FAULT &&
          fault(ctx, CINT_E_BOUNDS, "index.checked.struct", 1) && result == 99);
    check("write bounds", CX(13_view_x5Fwrite)(ctx, -1, empty, 0) == CINT_FAULT &&
          fault(ctx, CINT_E_BOUNDS, "index.checked.struct", 1) && sentinel == 0xA5);
    check("fuel baseline", CX(13_view_x5Fcount)(ctx, -1, a, &result) == CINT_OK &&
          cint_fuel_consumed(ctx, &before) == CINT_OK && before == 3);
    v = a; v.type.record_id = 1;
    check("nominal refusal", refused(ctx, CX(13_view_x5Fcount)(ctx, -1, v, &result), CINT_REFUSAL_TYPE, before));
    v = a; v.origin = 3;
    check("finite capacity refusal", refused(ctx, CX(13_view_x5Fcount)(ctx, -1, v, &result), CINT_REFUSAL_EXTENT, before));
    v = a; v.generation++;
    check("stale generation", CX(13_view_x5Fcount)(ctx, -1, v, &result) == CINT_FAULT &&
          fault(ctx, CINT_E_STALE_HANDLE, "bind.stale", 0));
    check("fuel baseline again", CX(13_view_x5Fcount)(ctx, -1, a, &result) == CINT_OK &&
          cint_fuel_consumed(ctx, &before) == CINT_OK && before == 3);
    v = a; v.perm = CINT_VIEW_READ;
    check("readonly inout view", CX(13_view_x5Fwrite)(ctx, -1, v, 0) == CINT_FAULT &&
          fault(ctx, CINT_E_ALIAS, "bind.permission", 0));
    check("fuel baseline restored", CX(13_view_x5Fcount)(ctx, -1, a, &result) == CINT_OK &&
          cint_fuel_consumed(ctx, &before) == CINT_OK && before == 3);
    check("byte registry lacks logical capacity", refused(ctx, CX(13_view_x5Fcount)(ctx, -1, view(old, 0, 0, 0), &result), CINT_REFUSAL_TYPE, before));
    check("release", cint_buffer_release(ctx, bi) == CINT_OK);
    check("released registration", refused(ctx, CX(13_view_x5Fcount)(ctx, -1, b, &result), CINT_REFUSAL_GENERATION, before));
    input.tag = 7; input.tail = 31; memset(&output, 0xA5, sizeof output);
    check("positive registration", cint_buffer_register_bytes(ctx, &input, sizeof input, CINT_VIEW_WRITE, &mi) == CINT_OK);
    check("positive copy omits nested zero payload", CX(13_mixed_x5Fecho)(ctx, -1, view(mi, 3, 0, 1), &output) == CINT_OK &&
          memcmp(&input, &output, sizeof input) == 0);
    cint_ctx_destroy(ctx);
    printf("zero records public: %d of %d pass\n", passed, total);
    return passed == total && path_status == 0 ? 0 : 1;
}
