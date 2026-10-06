#include <stdio.h>
#include <string.h>
#include "cint_rt.h"

#define CX(name) cx_14_zero_x5Ffields_##name
extern const cint_module_info cm_14_zero_x5Ffields;
cint_status CX(4_echo)(cint_ctx *, int64_t, cint_view, void *);
cint_status CX(7_cleared)(cint_ctx *, int64_t, void *);
cint_status CX(5_bools)(cint_ctx *, int64_t, cint_view, void *);
cint_status CX(4_last)(cint_ctx *, int64_t, cint_view, void *);
cint_status CX(6_packet)(cint_ctx *, int64_t, cint_view, void *);

typedef struct { _Alignas(8) uint8_t tag; uint8_t gap[7]; uint8_t tail; uint8_t end[7]; } Middle;
typedef struct { _Alignas(8) uint8_t live; uint8_t end[7]; } Aligned;
typedef struct { uint8_t tag, tail, live; } SkipBool;

static int passed, total;
static void check(const char *name, int ok)
{
    total++; passed += ok != 0;
    if (!ok) { fprintf(stderr, "FAILED %s\n", name); }
}
static cint_view view(cint_buffer_id id, uint32_t record)
{
    cint_view v;
    memset(&v, 0, sizeof v);
    v.buffer = id; v.generation = CINT_BUFFER_GENERATION_FIRST;
    v.type.code = CINT_TAG_RECORD; v.type.record_id = record;
    v.rank = 1; v.perm = CINT_VIEW_WRITE; v.shape[0] = 1; v.stride[0] = 1;
    return v;
}
static int refused(cint_ctx *ctx, cint_status status, uint32_t expected, int64_t before)
{
    uint32_t reason = 0;
    int64_t fuel = -1;
    return status == CINT_REFUSED && cint_ctx_refusal(ctx, &reason) == CINT_OK && reason == expected &&
           cint_fuel_consumed(ctx, &fuel) == CINT_OK && fuel == before;
}
static int faulted(cint_ctx *ctx, cint_status status, uint16_t code, const char *op)
{
    static cint_fault_record rec;
    int64_t fuel = -1;
    return status == CINT_FAULT && cint_ctx_fault(ctx, &rec) == CINT_OK && rec.code == code &&
           rec.operation_len == strlen(op) && memcmp(rec.operation, op, rec.operation_len) == 0 &&
           cint_fuel_consumed(ctx, &fuel) == CINT_OK && fuel == 0 && cint_ctx_clear_fault(ctx) == CINT_OK;
}
int main(void)
{
    const cint_module_info *cm = &cm_14_zero_x5Ffields;
    const cint_record_layout *r = cm->records;
    cint_ctx_config config;
    cint_ctx *ctx = NULL;
    cint_buffer_id mi = 0, bi = 0, ei = 0;
    cint_view iv, biv, eiv;
    Middle input, output, sentinel;
    SkipBool bits = {7, 211, 1}, bits_out = {99, 99, 99};
    uint8_t end_in[2] = {17, 2}, end_out = 99;
    int64_t before = -1;
    memset(&config, 0, sizeof config);
    config.size = (uint32_t)sizeof config; config.program = cm->program;
    if (cint_ctx_create(&config, &ctx) != CINT_OK) { return 1; }
    check("complete catalog", cm->record_count == 12u);
    if (cm->record_count != 12u) { cint_ctx_destroy(ctx); return 1; }
    check("middle layout", sizeof(Middle) == 16u && _Alignof(Middle) == 8u &&
          r[0].bytes == 16u && r[0].align == 8u && r[0].field_count == 3u &&
          r[0].fields[0].offset == 0u && r[0].fields[1].offset == 8u &&
          r[0].fields[1].count == 0u && r[0].fields[1].type.code == CINT_TAG_I64 &&
          r[0].fields[2].offset == offsetof(Middle, tail));
    check("leading zero layout", sizeof(Aligned) == 8u && r[1].bytes == 8u && r[1].align == 8u &&
          r[1].fields[0].offset == 0u && r[1].fields[0].count == 0u && r[1].fields[1].offset == 0u);
    check("trailing zero layout", r[2].bytes == 8u && r[2].align == 8u &&
          r[2].fields[0].offset == 0u && r[2].fields[1].offset == 8u && r[2].fields[1].count == 0u);
    check("successive zero alignments", r[3].bytes == 16u && r[3].align == 8u &&
          r[3].fields[1].offset == 2u && r[3].fields[2].offset == 4u && r[3].fields[3].offset == 8u &&
          r[3].fields[1].count == 0u && r[3].fields[2].count == 0u && r[3].fields[3].count == 0u &&
          r[3].fields[4].offset == 8u);
    check("zero record metadata", r[5].bytes == 3u && r[5].fields[1].offset == 1u &&
          r[5].fields[1].count == 0u && r[5].fields[1].type.record_id == 4u &&
          r[5].fields[2].offset == 1u && r[5].fields[3].offset == 2u &&
          r[6].bytes == 1u && r[6].fields[1].offset == 1u && r[6].fields[1].count == 0u);
    check("qualified and nested layout", r[7].bytes == 16u && r[7].fields[1].type.record_id == 9u &&
          r[7].fields[1].count == 0u && r[7].fields[1].offset == 8u && r[7].fields[2].offset == 8u &&
          r[8].bytes == 16u && r[8].fields[0].type.record_id == 2u && r[8].fields[1].offset == 8u);
    check("imported and unused layout", r[9].bytes == 16u && r[10].bytes == 16u &&
          r[10].fields[1].count == 0u && r[10].fields[1].type.record_id == 9u &&
          r[10].fields[1].offset == 8u && r[10].fields[2].offset == 8u &&
          r[11].bytes == 2u && r[11].align == 2u && r[11].fields[0].count == 0u &&
          r[11].fields[0].offset == 0u && r[11].fields[1].offset == 0u);
    memset(&input, 0, sizeof input); memset(&output, 0xA5, sizeof output);
    memcpy(&sentinel, &output, sizeof sentinel); input.tag = 3; input.tail = 29;
    check("registrations", cint_buffer_register_bytes(ctx, &input, sizeof input, CINT_VIEW_WRITE, &mi) == CINT_OK &&
          cint_buffer_register_bytes(ctx, &bits, sizeof bits, CINT_VIEW_WRITE, &bi) == CINT_OK &&
          cint_buffer_register_bytes(ctx, end_in, 1, CINT_VIEW_WRITE, &ei) == CINT_OK);
    iv = view(mi, 0); iv.perm = CINT_VIEW_READ;
    check("public aligned copy", CX(4_echo)(ctx, -1, iv, &output) == CINT_OK &&
          output.tag == 3 && output.tail == 29 && memcmp(&input, &output, sizeof input) == 0);
    memset(&output, 0xA5, sizeof output);
    check("public zero initialization", CX(7_cleared)(ctx, -1, &output) == CINT_OK);
    for (size_t j = 0; j < sizeof output; j++) { check("zero payload and explicit padding", ((uint8_t *)&output)[j] == 0u); }
    biv = view(bi, 5); biv.perm = CINT_VIEW_READ;
    check("zero bool array contributes no offsets", CX(5_bools)(ctx, -1, biv, &bits_out) == CINT_OK &&
          bits_out.tag == 7 && bits_out.tail == 211 && bits_out.live == 1);
    bits.live = 2; bits_out.tag = 99;
    check("following bool still checked", cint_fuel_consumed(ctx, &before) == CINT_OK &&
          refused(ctx, CX(5_bools)(ctx, -1, biv, &bits_out), CINT_REFUSAL_BOOL, before) && bits_out.tag == 99);
    eiv = view(ei, 6);
    check("zero bool array at record end", CX(4_last)(ctx, -1, eiv, &end_out) == CINT_OK && end_out == 17);
    iv.type.record_id = 10;
    check("imported zero-field copy", CX(6_packet)(ctx, -1, iv, &output) == CINT_OK && output.tag == 3 && output.tail == 29);
    iv.type.record_id = 0; memcpy(&output, &sentinel, sizeof output);
    check("nominal type remains checked",
          faulted(ctx, CX(6_packet)(ctx, -1, iv, &output), CINT_E_UNSUPPORTED, "bind.type") &&
          memcmp(&output, &sentinel, sizeof output) == 0);
    cint_ctx_destroy(ctx);
    printf("zero fields public: %d of %d pass\n", passed, total);
    return passed == total ? 0 : 1;
}
