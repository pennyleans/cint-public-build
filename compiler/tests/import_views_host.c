#include <stdio.h>
#include <string.h>
#include "cint_rt.h"

extern const cint_module_info cm_4_main;
cint_status cx_4_main_4_read(cint_ctx *, int64_t, cint_view, int64_t *);
cint_status cx_4_main_5_fixed(cint_ctx *, int64_t, cint_view, int64_t *);
cint_status cx_4_main_4_same(cint_ctx *, int64_t, cint_view, cint_view, int64_t *);
cint_status cx_4_main_6_copied(cint_ctx *, int64_t, cint_view, void *);
cint_status cx_4_main_4_fail(cint_ctx *, int64_t, cint_view, int64_t, void *);
typedef struct { uint8_t active; int64_t value; } R;

static int passed, total;
static void check(const char *name, int ok)
{
    total++;
    passed += ok != 0;
    if (!ok) { fprintf(stderr, "FAILED %s\n", name); }
}
static cint_view view(cint_buffer_id buffer, int64_t count)
{
    cint_view v;
    memset(&v, 0, sizeof v);
    v.buffer = buffer;
    v.generation = CINT_BUFFER_GENERATION_FIRST;
    v.type.code = CINT_TAG_RECORD;
    v.type.record_id = 0;
    v.rank = 1;
    v.perm = CINT_VIEW_WRITE;
    v.shape[0] = count;
    v.stride[0] = 1;
    return v;
}
static int refused(cint_ctx *ctx, cint_status status, uint32_t expected)
{
    uint32_t reason = 0;
    return status == CINT_REFUSED && cint_ctx_refusal(ctx, &reason) == CINT_OK && reason == expected;
}
static int fault(cint_ctx *ctx, uint16_t code, const char *op, const char *operands,
                 const char *limit, int64_t fuel)
{
    static cint_fault_record r;
    char buf[256], text[256] = "";
    uint32_t i;
    int64_t used = -1;
    int ok = cint_ctx_fault(ctx, &r) == CINT_OK && r.code == code && r.operation_len == strlen(op) &&
             memcmp(r.operation, op, r.operation_len) == 0 && cint_fuel_consumed(ctx, &used) == CINT_OK &&
             used == fuel && r.position.index != 0u;
    for (i = 0; i < r.operand_count; i++) {
        size_t n = cint_tvalue_render(&r.operands[i], buf, sizeof buf), len = strlen(text);
        snprintf(text + len, sizeof text - len, "%s%.*s", i ? "," : "", (int)n, buf);
    }
    ok = ok && strcmp(text, operands) == 0;
    if (limit) {
        ok = ok && r.has_limit && cint_tvalue_render(&r.limit, buf, sizeof buf) > 0u && strcmp(buf, limit) == 0;
    } else { ok = ok && !r.has_limit; }
    return ok && cint_ctx_clear_fault(ctx) == CINT_OK;
}

int main(void)
{
    const cint_module_info *cm = &cm_4_main;
    const cint_record_layout *records = cm->records;
    cint_ctx_config config;
    cint_ctx *ctx = NULL;
    R a[2], b[2], output, before, expected[2];
    _Alignas(8) uint8_t unaligned[sizeof(R) + 1];
    cint_buffer_id ba = 0, bb = 0, bu = 0;
    cint_view av, bv, v;
    int64_t result = 99, used = -1;
    memset(&config, 0, sizeof config);
    config.size = (uint32_t)sizeof config;
    config.program = cm->program;
    if (cint_ctx_create(&config, &ctx) != CINT_OK) { return 1; }
    check("complete nominal catalog", cm->abi == CINT_ABI_VERSION && cm->record_count == 3u);
    if (cm->record_count != 3u) { cint_ctx_destroy(ctx); return 1; }
    check("imported layout and field order", records[0].bytes == sizeof(R) && records[0].align == 8u &&
          records[0].field_count == 2u && records[0].fields[0].type.code == CINT_TAG_BOOL &&
          records[0].fields[0].offset == offsetof(R, active) &&
          records[0].fields[1].type.code == CINT_TAG_I64 && records[0].fields[1].offset == offsetof(R, value));
    check("private and distinct same-layout records", records[1].bytes == sizeof(int64_t) &&
          records[2].bytes == records[0].bytes && records[2].field_count == records[0].field_count);
    memset(a, 0xA5, sizeof a); memset(b, 0xA5, sizeof b); memset(&output, 0xA5, sizeof output);
    memcpy(&before, &output, sizeof output);
    memset(unaligned, 0, sizeof unaligned);
    a[0].active = 1; a[0].value = 7; a[1].active = 0; a[1].value = 8;
    b[0].active = 0; b[0].value = 3; b[1].active = 1; b[1].value = 4;
    check("registry", cint_buffer_register_bytes(ctx, a, sizeof a, CINT_VIEW_WRITE, &ba) == CINT_OK &&
          cint_buffer_register_bytes(ctx, b, sizeof b, CINT_VIEW_WRITE, &bb) == CINT_OK &&
          cint_buffer_register_bytes(ctx, unaligned + 1, sizeof(R), CINT_VIEW_WRITE, &bu) == CINT_OK);
    av = view(ba, 2); bv = view(bb, 2);
    av.perm = CINT_VIEW_READ;
    check("readonly wildcard", cx_4_main_4_read(ctx, -1, av, &result) == CINT_OK && result == 7 &&
          cint_fuel_consumed(ctx, &used) == CINT_OK && used == 1);
    result = 99; a[1].active = 2;
    check("last element bool validation", refused(ctx, cx_4_main_4_read(ctx, -1, av, &result), CINT_REFUSAL_BOOL) &&
          result == 99);
    a[1].active = 0;
    v = av; v.type.record_id = 2;
    check("nominal record identity", cx_4_main_4_read(ctx, -1, v, &result) == CINT_FAULT &&
          fault(ctx, CINT_E_UNSUPPORTED, "bind.type", "I64 0", NULL, 0));
    v = av; v.type.code = CINT_TAG_I64;
    check("element tag", cx_4_main_4_read(ctx, -1, v, &result) == CINT_FAULT &&
          fault(ctx, CINT_E_UNSUPPORTED, "bind.type", "I64 0", NULL, 0));
    v = av; v.rank = 2;
    check("rank", cx_4_main_4_read(ctx, -1, v, &result) == CINT_FAULT &&
          fault(ctx, CINT_E_SHAPE, "bind.type", "I64 0,I64 2", "I64 1", 0));
    v = av; v.generation++;
    check("generation", cx_4_main_4_read(ctx, -1, v, &result) == CINT_FAULT &&
          fault(ctx, CINT_E_STALE_HANDLE, "bind.stale", "I64 0,U64 2", "U64 1", 0));
    v = av; v.shape[0] = 3;
    check("registry extent", refused(ctx, cx_4_main_4_read(ctx, -1, v, &result), CINT_REFUSAL_EXTENT));
    v = view(bu, 1);
    check("alignment", refused(ctx, cx_4_main_4_read(ctx, -1, v, &result), CINT_REFUSAL_ALIGN));
    check("inout permission", cx_4_main_5_fixed(ctx, -1, av, &result) == CINT_FAULT &&
          fault(ctx, CINT_E_ALIAS, "bind.permission", "I64 0", NULL, 0));
    av.perm = CINT_VIEW_WRITE;
    memcpy(expected, a, sizeof a); expected[1].value++;
    check("fixed mutation and padding", cx_4_main_5_fixed(ctx, -1, av, &result) == CINT_OK && result == 9 &&
          memcmp(a, expected, sizeof a) == 0);
    v = av; v.shape[0] = 1;
    check("fixed shape before body", cx_4_main_5_fixed(ctx, -1, v, &result) == CINT_FAULT &&
          fault(ctx, CINT_E_SHAPE, "bind.shape", "I64 0,I64 0,I64 1", "I64 2", 0));
    v = bv; v.shape[0] = 1;
    check("shared size", cx_4_main_4_same(ctx, -1, av, v, &result) == CINT_FAULT &&
          fault(ctx, CINT_E_SHAPE, "bind.shape", "I64 1,I64 0,I64 1", "I64 2", 0));
    check("inout alias", cx_4_main_4_same(ctx, -1, av, av, &result) == CINT_FAULT &&
          fault(ctx, CINT_E_ALIAS, "bind.alias", "I64 0,I64 1", NULL, 0));
    check("two module aliases share type", cx_4_main_4_same(ctx, -1, av, bv, &result) == CINT_OK &&
          result == 2 && a[0].value == 10 && b[0].value == 3);
    av.shape[0] = 1; av.perm = CINT_VIEW_READ;
    check("by-value copy and result", cx_4_main_6_copied(ctx, -1, av, &output) == CINT_OK &&
          output.value == 11 && output.active == 1 && a[0].value == 10);
    memcpy(&output, &before, sizeof output);
    check("result pointer", refused(ctx, cx_4_main_6_copied(ctx, -1, av, NULL), CINT_REFUSAL_RESULT));
    check("result written after the body", cx_4_main_6_copied(ctx, -1, av, &a[0]) == CINT_OK &&
          a[0].value == 11 && a[0].active == 1);
    a[0].value = 10;
    av.perm = CINT_VIEW_WRITE;
    check("stores survive fault and result is preserved", cx_4_main_4_fail(ctx, -1, av, 0, &output) == CINT_FAULT &&
          a[0].value == 42 && memcmp(&output, &before, sizeof output) == 0 &&
          fault(ctx, CINT_E_DIV_ZERO, "div.checked.i64", "I64 10,I64 0", NULL, 1));
    check("success publishes result", cx_4_main_4_fail(ctx, -1, av, 2, &output) == CINT_OK &&
          output.value == 5 && output.active == 0);
    cint_ctx_destroy(ctx);
    printf("import view host: %d of %d pass\n", passed, total);
    return passed == total ? 0 : 1;
}
