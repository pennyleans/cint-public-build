#include <stdio.h>
#include <string.h>
#include "cint_rt.h"

#define CX(name) cx_16_nested_x5Fpublic_##name
extern const cint_module_info cm_16_nested_x5Fpublic;
cint_status CX(4_echo)(cint_ctx *, int64_t, cint_view, void *);
cint_status CX(5_touch)(cint_ctx *, int64_t, cint_view, int64_t *);
cint_status CX(6_change)(cint_ctx *, int64_t, cint_view, int64_t, void *);

typedef struct { uint8_t active; int64_t value; } Leaf;
typedef struct { Leaf item; uint8_t gate; } Middle;
typedef struct { uint8_t marker; Middle item; } Outer;

static int passed, total;
static void check(const char *name, int ok)
{
    total++;
    passed += ok != 0;
    if (!ok) { fprintf(stderr, "FAILED %s\n", name); }
}
static cint_view view(cint_buffer_id id)
{
    cint_view v;
    memset(&v, 0, sizeof v);
    v.buffer = id;
    v.generation = CINT_BUFFER_GENERATION_FIRST;
    v.type.code = CINT_TAG_RECORD;
    v.type.record_id = 0;
    v.rank = 1;
    v.perm = CINT_VIEW_WRITE;
    v.shape[0] = 1;
    v.stride[0] = 1;
    return v;
}
static int refused(cint_ctx *ctx, cint_status status, uint32_t expected)
{
    uint32_t reason = 0;
    return status == CINT_REFUSED && cint_ctx_refusal(ctx, &reason) == CINT_OK && reason == expected;
}
int main(void)
{
    const cint_module_info *cm = &cm_16_nested_x5Fpublic;
    const cint_record_layout *r = cm->records;
    cint_ctx_config config;
    cint_ctx *ctx = NULL;
    cint_buffer_id bi = 0;
    cint_view iv;
    Outer input, output, before;
    int64_t result = 99, fuel = -1;
    memset(&config, 0, sizeof config);
    config.size = (uint32_t)sizeof config;
    config.program = cm->program;
    if (cint_ctx_create(&config, &ctx) != CINT_OK) { return 1; }
    check("complete source-order catalog", cm->record_count == 4u);
    if (cm->record_count != 4u) { cint_ctx_destroy(ctx); return 1; }
    check("outer layout", r[0].bytes == sizeof(Outer) && r[0].align == _Alignof(Outer) &&
          r[0].field_count == 2u && r[0].fields[1].offset == offsetof(Outer, item) &&
          r[0].fields[1].type.record_id == 1u);
    check("middle layout", r[1].bytes == sizeof(Middle) && r[1].align == _Alignof(Middle) &&
          r[1].fields[0].type.record_id == 2u && r[1].fields[1].offset == offsetof(Middle, gate));
    check("leaf and private layout", r[2].bytes == sizeof(Leaf) &&
          r[2].fields[1].offset == offsetof(Leaf, value) && r[3].bytes == sizeof(uint16_t));
    memset(&input, 0, sizeof input);
    input.marker = 7; input.item.gate = 1; input.item.item.active = 1; input.item.item.value = 8;
    memset(&output, 0xA5, sizeof output); memcpy(&before, &output, sizeof before);
    check("registry", cint_buffer_register_bytes(ctx, &input, sizeof input, CINT_VIEW_WRITE, &bi) == CINT_OK);
    iv = view(bi);
    input.item.item.active = 2;
    check("deep incoming bool", refused(ctx, CX(4_echo)(ctx, -1, iv, &output), CINT_REFUSAL_BOOL) &&
          memcmp(&output, &before, sizeof output) == 0 && cint_fuel_consumed(ctx, &fuel) == CINT_OK && fuel == 0);
    input.item.item.active = 1; input.item.gate = 2;
    check("later incoming bool", refused(ctx, CX(4_echo)(ctx, -1, iv, &output), CINT_REFUSAL_BOOL));
    input.item.gate = 1;
    iv.perm = CINT_VIEW_READ;
    check("readonly copy and output bool bytes", CX(4_echo)(ctx, -1, iv, &output) == CINT_OK &&
          output.marker == 7 && output.item.gate == 1 && output.item.item.active == 1 && output.item.item.value == 8);
    iv.perm = CINT_VIEW_WRITE;
    check("nested view write", CX(5_touch)(ctx, -1, iv, &result) == CINT_OK && result == 9 && input.item.item.value == 9);
    input.item.item.active = 2;
    check("view deep bool", refused(ctx, CX(5_touch)(ctx, -1, iv, &result), CINT_REFUSAL_BOOL) && result == 9);
    input.item.item.active = 1;
    memcpy(&output, &before, sizeof output);
    check("fault keeps nested store and result", CX(6_change)(ctx, -1, iv, 0, &output) == CINT_FAULT &&
          input.item.item.value == 33 && memcmp(&output, &before, sizeof output) == 0);
    check("clear fault", cint_ctx_clear_fault(ctx) == CINT_OK);
    check("nested result publication", CX(6_change)(ctx, -1, iv, 2, &output) == CINT_OK &&
          input.item.item.value == 33 && output.item.item.value == 5 && output.item.item.active == 1);
    cint_ctx_destroy(ctx);
    printf("nested public: %d of %d pass\n", passed, total);
    return passed == total ? 0 : 1;
}
