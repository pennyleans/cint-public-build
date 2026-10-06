#include <stdio.h>
#include <string.h>
#include "cint_rt.h"

#define CX(name) cx_15_public_x5Fviews_##name
extern const cint_module_info cm_15_public_x5Fviews;
cint_status CX(6_paired)(cint_ctx *, int64_t, cint_view, int64_t, cint_view, cint_view, cint_view, int64_t *);
cint_status CX(5_fixed)(cint_ctx *, int64_t, cint_view, cint_view, int64_t *);
cint_status CX(4_bits)(cint_ctx *, int64_t, uint8_t, cint_view, uint8_t *);
cint_status CX(8_faulting)(cint_ctx *, int64_t, cint_view, int64_t, int64_t *);
cint_status CX(8_readonly)(cint_ctx *, int64_t, cint_view, cint_view, int64_t *);
extern const cint_library cint_library_desc;
typedef cint_status (*readonly_fn)(cint_ctx *, int64_t, cint_view, cint_view, int64_t *);

static int passed, total;
static void check(const char *name, int ok)
{
    total++;
    passed += ok != 0;
    if (!ok) { fprintf(stderr, "FAILED %s\n", name); }
}

static cint_view view(cint_buffer_id id, uint16_t tag, int64_t origin, int64_t n)
{
    cint_view v;
    memset(&v, 0, sizeof v);
    v.buffer = id;
    v.generation = CINT_BUFFER_GENERATION_FIRST;
    v.type.code = tag;
    v.rank = 1u;
    v.perm = CINT_VIEW_WRITE;
    v.origin = origin;
    v.shape[0] = n;
    v.stride[0] = 1;
    return v;
}

static int named(const cint_name *name, const char *text)
{
    return name->len == strlen(text) && memcmp(name->bytes, text, name->len) == 0 && name->reserved == 0u;
}

/* The reflection table (SPEC-03 A-19): one module, its five exports in declaration order,
 * each row with its size and parameter names, and the canonical type signature of paired,
 * a test vector of SPEC-03 5.6 (116 bytes). The table of the root module, or NULL. */
static const cint_module_table *table_checks(void)
{
    static const char *const names[] = {"paired", "fixed", "bits", "faulting", "readonly"};
    static const uint32_t sizes[] = {2u, 0u, 0u, 0u, 1u};
    static const char *const params[] = {"n", "m", "a", "bias", "b", "bytes", "more"};
    const cint_library *lib = &cint_library_desc;
    const cint_module_table *t = lib->module_count == 1u ? lib->modules[0] : NULL;
    const cint_export *x;
    uint32_t j;
    int ok;
    check("library", lib->abi == CINT_ABI_VERSION && t != NULL && t->info == &cm_15_public_x5Fviews &&
          t->module == 0u && t->record_names == NULL);
    if (t == NULL || t->export_count != 5u) { return NULL; }
    ok = 1;
    for (j = 0; j < 5u; j++) {
        x = &t->exports[j];
        ok = ok && named(&x->name, names[j]) && x->kind == CINT_EXPORT_FUNCTION && x->effect == CINT_PURE &&
             x->size_count == sizes[j] && x->param_count == 2u + 3u * (j == 0u) &&
             (x->size_names == NULL) == (sizes[j] == 0u);
    }
    check("export rows", ok);
    x = &t->exports[0];
    ok = 1;
    for (j = 0; j < 2u; j++) { ok = ok && named(&x->size_names[j], params[j]); }
    for (j = 0; j < 5u; j++) { ok = ok && named(&x->param_names[j], params[2u + j]); }
    check("names", ok && named(&t->exports[4].size_names[0], "n") && named(&t->exports[4].param_names[1], "b"));
    check("paired signature", x->type_signature_len == 116u && x->type_signature[0] == 29u &&
          memcmp(x->type_signature + 4, "cint-core-1/type-signature/v1", 29) == 0 &&
          x->type_signature[115] == CINT_TAG_I64 && x->call == (cint_entry_fn)CX(6_paired));
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
    } else {
        ok = ok && !r.has_limit;
    }
    return ok && cint_ctx_clear_fault(ctx) == CINT_OK;
}

static int run_checks(void)
{
    cint_ctx_config config;
    cint_ctx *ctx = NULL;
    int64_t a[4] = {1, 2, 3, 4}, b[4] = {10, 20, 30, 40}, result = 99;
    int32_t four[4] = {9, 8, 7, 6};
    uint8_t bytes[4] = {1, 0, 1, 0}, bit = 99;
    cint_buffer_id ba = 0, bb = 0, bc = 0, bf = 0, duplicate = 0, stale = 0, unaligned = 0;
    cint_view av, bv, cv, fv, v;
    const cint_module_table *t = table_checks();
    memset(&config, 0, sizeof config);
    config.size = (uint32_t)sizeof config;
    config.program = cm_15_public_x5Fviews.program;
    if (cint_ctx_create(&config, &ctx) != CINT_OK) { return 1; }
    check("registry", cint_buffer_register_bytes(ctx, a, sizeof a, CINT_VIEW_WRITE, &ba) == CINT_OK &&
          cint_buffer_register_bytes(ctx, b, sizeof b, CINT_VIEW_WRITE, &bb) == CINT_OK &&
          cint_buffer_register_bytes(ctx, bytes, sizeof bytes, CINT_VIEW_WRITE, &bc) == CINT_OK &&
          cint_buffer_register_bytes(ctx, four, sizeof four, CINT_VIEW_WRITE, &bf) == CINT_OK &&
          cint_buffer_register_bytes(ctx, a, sizeof a, CINT_VIEW_WRITE, &duplicate) == CINT_OK &&
          cint_buffer_register_bytes(ctx, a, sizeof a, CINT_VIEW_WRITE, &stale) == CINT_OK &&
          cint_buffer_release(ctx, stale) == CINT_OK &&
          cint_buffer_register_bytes(ctx, (uint8_t *)a + 1, 24, CINT_VIEW_WRITE, &unaligned) == CINT_OK);
    av = view(ba, CINT_TAG_I64, 0, 4);
    bv = view(bb, CINT_TAG_I64, 0, 4);
    cv = view(bc, CINT_TAG_U8, 0, 4);
    fv = view(bf, CINT_TAG_I32, 0, 4);
    check("multiple sizes", CX(6_paired)(ctx, -1, av, 2, bv, cv, cv, &result) == CINT_OK &&
          result == 14 && a[0] == 12);
    check("fixed", CX(5_fixed)(ctx, -1, fv, cv, &result) == CINT_OK && result == 1 && four[0] == 1);
    result = 99;
    v = bv; v.shape[0] = 3;
    check("shape source index", CX(6_paired)(ctx, -1, av, 2, v, cv, cv, &result) == CINT_FAULT &&
          fault(ctx, CINT_E_SHAPE, "bind.shape", "I64 2,I64 0,I64 3", "I64 4", 0) && result == 99 && a[0] == 12);
    v = cv; v.shape[0] = 2;
    check("independent shape", CX(6_paired)(ctx, -1, av, 2, bv, cv, v, &result) == CINT_FAULT &&
          fault(ctx, CINT_E_SHAPE, "bind.shape", "I64 4,I64 0,I64 2", "I64 4", 0) && result == 99);
    v = fv; v.shape[0] = 3;
    check("fixed shape", CX(5_fixed)(ctx, -1, v, cv, &result) == CINT_FAULT &&
          fault(ctx, CINT_E_SHAPE, "bind.shape", "I64 0,I64 0,I64 3", "I64 4", 0) && four[0] == 1);
    cv.type.code = CINT_TAG_BOOL;
    /* T3 (SPEC-03 H-12, SPEC-02 F-5; box 10 default BX10-13): an element type other than
     * the declared one is an entry fault, and check 3 comes before the shapes of check 5. */
    check("later type before shape", CX(5_fixed)(ctx, -1, v, cv, &result) == CINT_FAULT &&
          fault(ctx, CINT_E_UNSUPPORTED, "bind.type", "I64 1", NULL, 0));
    check("bool admitted", CX(4_bits)(ctx, -1, 1, cv, &bit) == CINT_OK && bit == 1);
    bit = 99;
    check("scalar bool range", refused(ctx, CX(4_bits)(ctx, -1, 2, cv, &bit), CINT_REFUSAL_BOOL) && bit == 99);
    bytes[3] = 2;
    check("bool bytes", refused(ctx, CX(4_bits)(ctx, -1, 1, cv, &bit), CINT_REFUSAL_BOOL) && bit == 99);
    check("result pointer", refused(ctx, CX(4_bits)(ctx, -1, 1, cv, NULL), CINT_REFUSAL_RESULT));
    bytes[3] = 0; cv.type.code = CINT_TAG_U8;
    check("alias", CX(6_paired)(ctx, -1, av, 0, av, cv, cv, &result) == CINT_FAULT &&
          fault(ctx, CINT_E_ALIAS, "bind.alias", "I64 0,I64 2", NULL, 0));
    v = av; v.buffer = duplicate;
    check("distinct buffer alias", CX(6_paired)(ctx, -1, av, 0, v, cv, cv, &result) == CINT_FAULT &&
          fault(ctx, CINT_E_ALIAS, "bind.alias", "I64 0,I64 2", NULL, 0));
    check("readonly overlap", CX(8_readonly)(ctx, -1, av, av, &result) == CINT_OK && result == 24);
    result = 99;
    check("call through the table", t != NULL &&
          ((readonly_fn)t->exports[4].call)(ctx, -1, av, av, &result) == CINT_OK && result == 24);
    av.shape[0] = 2; v = av; v.origin = 2;
    check("adjacent", CX(6_paired)(ctx, -1, av, 0, v, cv, cv, &result) == CINT_OK && result == 5);
    av.shape[0] = 0; v = av;
    check("empty overlap", CX(6_paired)(ctx, -1, av, 0, v, cv, cv, &result) == CINT_OK && result == 4);
    av.shape[0] = 4;
#define BAD(label, reason, change) do { v = av; change; result = 99; \
    check(label, refused(ctx, CX(6_paired)(ctx, 0, v, 0, bv, cv, cv, &result), reason) && result == 99); } while (0)
    BAD("record id", CINT_REFUSAL_TYPE, v.type.record_id = 1);
    BAD("released", CINT_REFUSAL_GENERATION, v.buffer = stale);
    BAD("origin", CINT_REFUSAL_EXTENT, v.origin = -1);
    BAD("extent", CINT_REFUSAL_EXTENT, v.shape[0] = -1);
    BAD("range", CINT_REFUSAL_EXTENT, v.origin = 1);
    BAD("size", CINT_REFUSAL_SIZE, v.origin = 2; v.shape[0] = INT64_MAX);
    BAD("alignment", CINT_REFUSAL_ALIGN, v.buffer = unaligned; v.shape[0] = 2);
#undef BAD
#define FAULT(label, change, code, op, operands, limit) do { v = av; change; result = 99; \
    check(label, CX(6_paired)(ctx, -1, v, 0, bv, cv, cv, &result) == CINT_FAULT && \
          fault(ctx, code, op, operands, limit, 0) && result == 99); } while (0)
    FAULT("generation", v.generation = 2, CINT_E_STALE_HANDLE, "bind.stale", "I64 0,U64 2", "U64 1");
    FAULT("permission", v.perm = CINT_VIEW_READ, CINT_E_ALIAS, "bind.permission", "I64 0", NULL);
    FAULT("tag", v.type.code = CINT_TAG_U64, CINT_E_UNSUPPORTED, "bind.type", "I64 0", NULL);
    FAULT("rank", v.rank = 2; v.shape[1] = 1, CINT_E_SHAPE, "bind.type", "I64 0,I64 2", "I64 1");
#undef FAULT
    /* A view of rank 1 takes any stride (rt/OPEN.md RT-OQ-38): a[0] is b[3] here. */
    v = av; v.shape[0] = 2; v.stride[0] = 2;
    fv = bv; fv.origin = 3; fv.shape[0] = 2; fv.stride[0] = -1;
    check("stride", CX(6_paired)(ctx, -1, v, 0, fv, cv, cv, &result) == CINT_OK && result == 42 && a[0] == 40);
    result = 99;
    v.stride[0] = 0;
    check("injective", CX(6_paired)(ctx, -1, v, 0, fv, cv, cv, &result) == CINT_FAULT &&
          fault(ctx, CINT_E_ALIAS, "bind.injective", "I64 0", NULL, 0));
    check("body fault", CX(8_faulting)(ctx, -1, av, 0, &result) == CINT_FAULT && a[0] == 42 && result == 99 &&
          fault(ctx, CINT_E_DIV_ZERO, "div.checked.i64", "I64 10,I64 0", NULL, 1));
    cint_ctx_destroy(ctx);
    return passed == total ? 0 : 1;
}

#ifdef PUBLIC_VIEWS_ADAPTER
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
static const cint_observer_entry entries[] = {{0u, 5u, "check", types, call_checks}};
CINT_RT_EXPORT const cint_observer cint_observer_desc = {1u, 0u, &cg_program, entries};
#else
int main(void)
{
    int failed = run_checks();
    printf("public views: %d of %d pass\n", passed, total);
    return failed;
}
#endif
