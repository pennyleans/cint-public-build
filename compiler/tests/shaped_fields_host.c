#include <stdio.h>
#include <string.h>
#include "cint_rt.h"

/* BX22-06: the record catalog of struct fields of rank 2 to 4 (flat, row-major), and the
   Bool offsets of such a field checked when a public function binds the record. */
#define CX(name) cx_16_shaped_x5Ffields_##name
extern const cint_module_info cm_16_shaped_x5Ffields;
cint_status CX(7_flipped)(cint_ctx *, int64_t, cint_view, void *);

typedef struct { int64_t cells[2][3]; uint8_t on[2][2]; uint8_t tag; } Plate;

static int passed, total;
static void check(const char *name, int ok)
{
    total++; passed += ok != 0;
    if (!ok) { fprintf(stderr, "FAILED %s\n", name); }
}
static int field(const cint_record_field *f, uint32_t offset, uint32_t count, uint32_t code)
{
    return f->offset == offset && f->count == count && f->type.code == code;
}
int main(void)
{
    const cint_module_info *cm = &cm_16_shaped_x5Ffields;
    const cint_record_layout *r = cm->records;
    cint_ctx_config config;
    cint_ctx *ctx = NULL;
    cint_buffer_id pi = 0;
    cint_view v;
    Plate in, out, sentinel;
    int64_t before = -1;
    uint32_t reason = 0;
    memset(&config, 0, sizeof config);
    config.size = (uint32_t)sizeof config; config.program = cm->program;
    if (cint_ctx_create(&config, &ctx) != CINT_OK) { return 1; }
    check("complete catalog", cm->record_count == 6u);
    if (cm->record_count != 6u) { cint_ctx_destroy(ctx); return 1; }
    check("rank 2 and rank 1 fields", r[0].bytes == 136u && r[0].field_count == 3u &&
          field(&r[0].fields[0], 0u, 12u, CINT_TAG_I64) && field(&r[0].fields[1], 96u, 4u, CINT_TAG_I64) &&
          field(&r[0].fields[2], 128u, 1u, CINT_TAG_I64));
    check("rank 2 to 4 fields", r[1].bytes == 40u &&
          field(&r[1].fields[0], 0u, 4u, CINT_TAG_BOOL) && field(&r[1].fields[1], 4u, 12u, CINT_TAG_U8) &&
          field(&r[1].fields[2], 16u, 12u, CINT_TAG_I16));
    check("public record", sizeof(Plate) == 56u && r[3].bytes == 56u && r[3].align == 8u &&
          field(&r[3].fields[0], 0u, 6u, CINT_TAG_I64) && field(&r[3].fields[1], 48u, 4u, CINT_TAG_BOOL) &&
          field(&r[3].fields[2], 52u, 1u, CINT_TAG_U8));
    check("imported record", r[4].bytes == 56u && field(&r[4].fields[0], 0u, 6u, CINT_TAG_I64) &&
          r[5].bytes == 64u && field(&r[5].fields[1], 56u, 8u, CINT_TAG_U8));
    memset(&in, 0, sizeof in);
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 3; j++) { in.cells[i][j] = i * 3 + j + 1; }
    }
    in.on[0][0] = 1; in.on[1][1] = 1; in.tag = 7;
    memset(&out, 0xA5, sizeof out);
    memcpy(&sentinel, &out, sizeof sentinel);
    check("registration", cint_buffer_register_bytes(ctx, &in, sizeof in, CINT_VIEW_READ, &pi) == CINT_OK);
    memset(&v, 0, sizeof v);
    v.buffer = pi; v.generation = CINT_BUFFER_GENERATION_FIRST;
    v.type.code = CINT_TAG_RECORD; v.type.record_id = 3u;
    v.rank = 1; v.perm = CINT_VIEW_READ; v.shape[0] = 1; v.stride[0] = 1;
    check("public call", CX(7_flipped)(ctx, -1, v, &out) == CINT_OK && out.cells[0][0] == 6 &&
          out.cells[0][1] == 2 && out.cells[1][2] == 6 && out.on[0][0] == 1 && out.on[0][1] == 1 &&
          out.on[1][0] == 0 && out.on[1][1] == 1 && out.tag == 8);
    in.on[1][1] = 2;
    memcpy(&out, &sentinel, sizeof out);
    check("last Bool element checked", cint_fuel_consumed(ctx, &before) == CINT_OK &&
          CX(7_flipped)(ctx, -1, v, &out) == CINT_REFUSED && cint_ctx_refusal(ctx, &reason) == CINT_OK &&
          reason == CINT_REFUSAL_BOOL && memcmp(&out, &sentinel, sizeof out) == 0);
    cint_ctx_destroy(ctx);
    printf("shaped fields public: %d of %d pass\n", passed, total);
    return passed == total ? 0 : 1;
}
