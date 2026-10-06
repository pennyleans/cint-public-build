#include <stdio.h>
#include <string.h>
#include "cint_rt.h"

#define CX(name) cx_17_public_x5Frecords_##name
extern const cint_module_info cm_17_public_x5Frecords;
cint_status CX(5_touch)(cint_ctx *, int64_t, cint_view, int64_t *);
cint_status CX(6_copied)(cint_ctx *, int64_t, cint_view, void *);
cint_status CX(4_both)(cint_ctx *, int64_t, cint_view, cint_view, int64_t, void *);
cint_status CX(7_aliases)(cint_ctx *, int64_t, int64_t *);
extern const cint_module_info cm_13_record_x5Flib, cm_13_record_x5Fmid;
extern const cint_library cint_library_desc;
typedef cint_status (*aliases_fn)(cint_ctx *, int64_t, int64_t *);
typedef struct { uint8_t enabled, code; } Flag;
typedef struct { uint8_t marker; int32_t value; uint8_t flag, bits[2]; Flag nested; } Packet;
typedef struct { int64_t value; uint8_t flag; } Simple;

static int passed, total;
static void check(const char *name, int ok)
{
    total++;
    passed += ok != 0;
    if (!ok) { fprintf(stderr, "FAILED %s\n", name); }
}
static cint_view view(cint_buffer_id id, uint32_t record, int64_t n)
{
    cint_view v;
    memset(&v, 0, sizeof v);
    v.buffer = id;
    v.generation = CINT_BUFFER_GENERATION_FIRST;
    v.type.code = CINT_TAG_RECORD;
    v.type.record_id = record;
    v.rank = 1;
    v.perm = CINT_VIEW_WRITE;
    v.shape[0] = n;
    v.stride[0] = 1;
    return v;
}
static int named(const cint_name *name, const char *text)
{
    return name->len == strlen(text) && memcmp(name->bytes, text, name->len) == 0 && name->reserved == 0u;
}
/* The reflection table (SPEC-03 A-19): the modules in the order of the program's module
 * table, the root module last; only the root module has export rows and record names, one
 * qualified name per row of its record layout table. both's canonical type signature is a
 * test vector of SPEC-03 5.6 (116 bytes). The table of the root module, or NULL. */
static const cint_module_table *table_checks(void)
{
    static const char *const exports[] = {"touch", "copied", "both", "aliases"};
    static const uint32_t params[] = {1u, 1u, 3u, 0u};
    static const char *const records[] = {"public_records.Packet", "public_records.Simple", "record_mid.Middle",
                                          "record_lib.Flag", "record_lib.Unused"};
    const cint_library *lib = &cint_library_desc;
    const cint_module_table *t;
    const cint_export *x;
    uint32_t j;
    int ok = lib->abi == CINT_ABI_VERSION && lib->module_count == 3u;
    check("library", ok);
    if (!ok) { return NULL; }
    for (j = 0; j < 3u; j++) { ok = ok && lib->modules[j]->module == j; }
    check("module order, root last", ok && lib->modules[0]->info == &cm_13_record_x5Flib &&
          lib->modules[1]->info == &cm_13_record_x5Fmid && lib->modules[2]->info == &cm_17_public_x5Frecords);
    check("imported modules have no rows", lib->modules[0]->export_count == 0u && lib->modules[0]->exports == NULL &&
          lib->modules[0]->record_names == NULL && lib->modules[1]->export_count == 0u &&
          lib->modules[1]->exports == NULL && lib->modules[1]->record_names == NULL);
    t = lib->modules[2];
    if (t->export_count != 4u) { return NULL; }
    for (j = 0; j < 4u; j++) {
        x = &t->exports[j];
        ok = ok && named(&x->name, exports[j]) && x->kind == CINT_EXPORT_FUNCTION && x->effect == CINT_PURE &&
             x->size_count == 0u && x->size_names == NULL && x->param_count == params[j] &&
             (x->param_names == NULL) == (params[j] == 0u);
    }
    check("export rows", ok);
    ok = t->record_names != NULL;
    for (j = 0; ok && j < 5u; j++) { ok = named(&t->record_names[j], records[j]); }
    check("qualified record names", ok);
    x = &t->exports[2];
    check("both row", named(&x->param_names[0], "a") && named(&x->param_names[1], "b") &&
          named(&x->param_names[2], "divisor") && x->type_signature_len == 116u &&
          memcmp(x->type_signature + 4, "cint-core-1/type-signature/v1", 29) == 0 &&
          x->call == (cint_entry_fn)CX(4_both) && t->exports[3].type_signature_len == 46u);
    return t;
}
static int refused(cint_ctx *ctx, cint_status status, uint32_t expected)
{
    uint32_t reason = 0;
    return status == CINT_REFUSED && cint_ctx_refusal(ctx, &reason) == CINT_OK && reason == expected;
}
static int fault(cint_ctx *ctx, uint16_t code, const char *op, const char *operands, const char *limit,
                 int64_t fuel)
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
static int run_checks(void)
{
    const cint_module_info *cm = &cm_17_public_x5Frecords;
    const cint_record_layout *records = cm->records;
    cint_ctx_config config;
    cint_ctx *ctx = NULL;
    _Alignas(8) Packet packets[2];
    Packet expected[2];
    Simple a[2] = {{8, 1}, {9, 0}}, b[2] = {{12, 0}, {13, 1}}, output[2], before[2];
    cint_buffer_id bp = 0, ba = 0, bb = 0;
    cint_view pv, av, bv, v;
    int64_t result = 99;
    const cint_module_table *t = table_checks();
    memset(&config, 0, sizeof config);
    config.size = (uint32_t)sizeof config;
    config.program = cm->program;
    if (cint_ctx_create(&config, &ctx) != CINT_OK) { return 1; }
    check("complete catalog", cm->abi == CINT_ABI_VERSION && cm->record_count == 5u);
    if (cm->record_count != 5u) { cint_ctx_destroy(ctx); return 1; }
    check("packet layout", records[0].bytes == sizeof(Packet) && records[0].align == CINT_ELEM_ALIGN(sizeof(Packet)) &&
          records[0].field_count == 5u && records[0].reserved == 0u &&
          records[0].fields[0].offset == offsetof(Packet, marker) && records[0].fields[0].type.code == CINT_TAG_U8 &&
          records[0].fields[1].offset == offsetof(Packet, value) && records[0].fields[1].type.code == CINT_TAG_I32 &&
          records[0].fields[3].offset == offsetof(Packet, bits) && records[0].fields[3].count == 2u &&
          records[0].fields[3].type.code == CINT_TAG_BOOL);
    check("nested identity", records[0].fields[4].offset == offsetof(Packet, nested) &&
          records[0].fields[4].type.code == CINT_TAG_RECORD && records[0].fields[4].type.record_id == 3u &&
          records[3].bytes == sizeof(Flag) && records[3].fields[0].type.code == CINT_TAG_BOOL);
    check("root before breadth first imports", records[1].bytes == sizeof(Simple) &&
          records[2].fields[0].type.code == CINT_TAG_U32 && records[4].field_count == 2u &&
          records[4].fields[1].count == 3u && records[4].fields[1].type.code == CINT_TAG_BOOL);
    memset(packets, 0xA5, sizeof packets);
    packets[0].value = 10; packets[1].value = 20;
    packets[0].flag = packets[1].flag = 1;
    memset(packets[0].bits, 0, 2); memset(packets[1].bits, 1, 2);
    packets[0].nested.enabled = 0; packets[1].nested.enabled = 1;
    memcpy(expected, packets, sizeof packets); expected[0].value = 11;
    memset(output, 0xA5, sizeof output); memcpy(before, output, sizeof output);
    check("registry", cint_buffer_register_bytes(ctx, packets, sizeof packets, CINT_VIEW_WRITE, &bp) == CINT_OK &&
          cint_buffer_register_bytes(ctx, a, sizeof a, CINT_VIEW_WRITE, &ba) == CINT_OK &&
          cint_buffer_register_bytes(ctx, b, sizeof b, CINT_VIEW_WRITE, &bb) == CINT_OK);
    pv = view(bp, 0, 2); av = view(ba, 1, 1); bv = view(bb, 1, 1);
    check("record mutation and padding", CX(5_touch)(ctx, -1, pv, &result) == CINT_OK && result == 11 &&
          memcmp(packets, expected, sizeof packets) == 0);
    result = 99;
    packets[1].flag = 2; memcpy(expected, packets, sizeof packets);
    check("direct bool", refused(ctx, CX(5_touch)(ctx, -1, pv, &result), CINT_REFUSAL_BOOL) &&
          result == 99 && memcmp(packets, expected, sizeof packets) == 0);
    packets[1].flag = 1; packets[1].bits[1] = 2;
    check("array bool", refused(ctx, CX(5_touch)(ctx, -1, pv, &result), CINT_REFUSAL_BOOL) && result == 99);
    packets[1].bits[1] = 1; packets[1].nested.enabled = 2;
    check("nested bool", refused(ctx, CX(5_touch)(ctx, -1, pv, &result), CINT_REFUSAL_BOOL) && result == 99);
    packets[1].nested.enabled = 1;
    v = pv; v.type.record_id = 1;
    /* T3 (SPEC-03 H-12; box 10 default BX10-13): another record type is an entry fault. */
    check("exact record id", CX(5_touch)(ctx, -1, v, &result) == CINT_FAULT &&
          fault(ctx, CINT_E_UNSUPPORTED, "bind.type", "I64 0", NULL, 0));
    av.perm = CINT_VIEW_READ;
    /* A struct result goes through a pointer to its C layout (box 10 default BX10-26). */
    check("readonly value and arbitrary output bytes", CX(6_copied)(ctx, -1, av, &output[0]) == CINT_OK &&
          a[0].value == 8 && output[0].value == 9 && output[0].flag == 1 &&
          memcmp(&output[1], &before[1], sizeof output[1]) == 0);
    memcpy(output, before, sizeof output);
    check("result pointer", refused(ctx, CX(6_copied)(ctx, -1, av, NULL), CINT_REFUSAL_RESULT));
    a[0].flag = 2;
    check("input bool", refused(ctx, CX(6_copied)(ctx, -1, av, &output[0]), CINT_REFUSAL_BOOL) &&
          memcmp(output, before, sizeof output) == 0);
    a[0].flag = 1;
    v = av; v.shape[0] = 2;
    check("input extent", CX(6_copied)(ctx, -1, v, &output[0]) == CINT_FAULT &&
          fault(ctx, CINT_E_SHAPE, "bind.shape", "I64 0,I64 0,I64 2", "I64 1", 0));
    v.shape[0] = 1; v.type.record_id = 0;
    check("input record id", CX(6_copied)(ctx, -1, v, &output[0]) == CINT_FAULT &&
          fault(ctx, CINT_E_UNSUPPORTED, "bind.type", "I64 0", NULL, 0));
    av.perm = CINT_VIEW_WRITE;
    v = av; v.shape[0] = 0;
    check("inout extent", CX(4_both)(ctx, -1, v, bv, 1, &output[0]) == CINT_FAULT &&
          fault(ctx, CINT_E_SHAPE, "bind.shape", "I64 0,I64 0,I64 0", "I64 1", 0));
    v = av; v.perm = CINT_VIEW_READ;
    check("inout permission", CX(4_both)(ctx, -1, v, bv, 1, &output[0]) == CINT_FAULT &&
          fault(ctx, CINT_E_ALIAS, "bind.permission", "I64 0", NULL, 0));
    a[0].flag = 2;
    check("inout bool", refused(ctx, CX(4_both)(ctx, -1, av, bv, 1, &output[0]), CINT_REFUSAL_BOOL));
    a[0].flag = 1;
    check("argument alias", CX(4_both)(ctx, -1, av, av, 1, &output[0]) == CINT_FAULT &&
          fault(ctx, CINT_E_ALIAS, "bind.alias", "I64 0,I64 1", NULL, 0));
    check("body store retained result preserved", CX(4_both)(ctx, -1, av, bv, 0, &output[0]) == CINT_FAULT &&
          a[0].value == 42 && memcmp(output, before, sizeof output) == 0 &&
          fault(ctx, CINT_E_DIV_ZERO, "div.checked.i64", "I64 10,I64 0", NULL, 1));
    check("successful result publication", CX(4_both)(ctx, -1, av, bv, 2, &output[0]) == CINT_OK &&
          output[0].value == 5 && output[0].flag == 0 && b[0].value == 12);
    check("result written after the body", CX(4_both)(ctx, -1, av, bv, 5, &a[0]) == CINT_OK &&
          a[0].value == 2 && a[0].flag == 0);
    check("two aliases", CX(7_aliases)(ctx, -1, &result) == CINT_OK && result == 14);
    result = 99;
    check("call through the table", t != NULL &&
          ((aliases_fn)t->exports[3].call)(ctx, -1, &result) == CINT_OK && result == 14);
    cint_ctx_destroy(ctx);
    return passed == total ? 0 : 1;
}

#ifdef PUBLIC_RECORDS_ADAPTER
extern const cint_program cg_program;
static cint_status call_checks(cint_ctx *ctx, int64_t fuel, int64_t depth,
                               const uint64_t *args, uint64_t *result)
{
    cint_status status = cint_rt_entry_begin(ctx, (cint_site){0u, 0u}, fuel, depth);
    int failed;
    (void)args;
    if (status != CINT_OK) { return status; }
    passed = total = 0;
    failed = run_checks();
    status = cint_rt_entry_end(ctx);
    if (status == CINT_OK) { *result = failed ? 0u : (uint64_t)passed; }
    return status;
}
static const uint32_t types[] = {CINT_TAG_I64, 0u};
static const cint_observer_entry entries[] = {{2u, 5u, "check", types, call_checks}};
CINT_RT_EXPORT const cint_observer cint_observer_desc = {1u, 0u, &cg_program, entries};
#else
int main(void)
{
    int failed = run_checks();
    printf("public records: %d of %d pass\n", passed, total);
    return failed;
}
#endif
