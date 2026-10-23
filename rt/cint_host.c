/* cint_host.c: the host table and the extern call (SPEC-03 A-10, A-15, A-23, H-5, H-9;
 * I/O services lane U2; rt/OPEN.md RT-OQ-70).
 *
 * rt/cint_rt.c includes this file, as it includes rt/cint_mem.c, so the code is part of the
 * cint_rt library and of its digest in receipts, and it is counted in its own SPEC-09 4.2
 * row. cint_ctx_create resolves every row of the program's host import tables
 * (cint_program.host) against the configuration's host table (cint_rt_host_resolve), and
 * generated code calls each non-service extern through cint_rt_host_call, which stages
 * `out` views, makes the call, or hands it to the recorder's or replay's hook, checks the
 * result against the declaration and writes the boundary faults of A-6. The recorder and
 * replay (rt/cint_rec.c) encode the arguments and the result; nothing here does.
 */

size_t cint_rt_host_width(uint32_t tag)
{
    if (tag == CINT_TAG_BOOL) {
        return 1u;
    }
    if ((tag < CINT_TAG_I8 || tag > CINT_TAG_I64) && (tag < CINT_TAG_U8 || tag > CINT_TAG_U64)) {
        return 0u;
    }
    return (size_t)1u << ((tag & 0x0fu) - 1u);
}

bool cint_rt_host_fits(uint32_t tag, uint64_t bits)
{
    unsigned w;
    if (tag == CINT_TAG_BOOL) {
        return bits <= 1u;
    }
    w = 8u << ((tag & 0x0fu) - 1u);
    if (w == 64u) {
        return true;
    }
    if ((tag & 0xf0u) == 0x10u) {  /* signed: the high bits repeat the sign */
        uint64_t sign = (bits >> (w - 1u)) & 1u;
        uint64_t high = bits >> w;
        return high == (sign != 0u ? (UINT64_C(1) << (64u - w)) - 1u : 0u);
    }
    return (bits >> w) == 0u;
}

/* The import count of a module's table; 0 for a NULL entry. */
static uint32_t host_count_of(const cint_host_table *t)
{
    return t == NULL ? 0u : t->count;
}

bool cint_rt_host_tables_valid(const cint_host_desc *d, const cint_program *p)
{
    if (d->module_count != p->module_count || d->reserved != 0u || (d->module_count > 0u && d->tables == NULL)) {
        return false;
    }
    for (uint32_t m = 0u; m < p->module_count; m++) {
        const cint_host_table *t = d->tables[m];
        if (t == NULL) {
            continue;
        }
        if (t->reserved != 0u || (t->count > 0u && t->imports == NULL)) {
            return false;
        }
        for (uint32_t k = 0u; k < t->count; k++) {
            const cint_host_import *r = &t->imports[k];
            if (r->name.bytes == NULL || r->name.len == 0u || r->name.len > 255u || r->name.reserved != 0u ||
                r->effect < CINT_PURE || r->effect > CINT_UNRESTRICTED || r->param_count > 16u ||
                (r->type_signature_len > 0u && r->type_signature == NULL)) {
                return false;
            }
        }
    }
    return true;
}

/* The function of the configuration's host table that row r names (A-10): the first whose
 * name is the row's, which must declare the row's effect class and the SHA-256 of the row's
 * canonical type signature (A-23); NULL otherwise. */
static const cint_host_fn *host_find(const cint_ctx_config *config, const cint_host_import *r)
{
    uint8_t digest[32];
    for (uint32_t j = 0u; j < config->host_count; j++) {
        const cint_host_fn *f = &config->host[j];
        if (strlen(f->name) != r->name.len || memcmp(f->name, r->name.bytes, r->name.len) != 0) {
            continue;
        }
        if (f->effect != r->effect) {
            return NULL;
        }
        cint_sha256(r->type_signature, (size_t)r->type_signature_len, digest);
        return memcmp(digest, f->signature, 32u) == 0 ? f : NULL;
    }
    return NULL;
}

cint_status cint_rt_host_resolve(cint_ctx *ctx, const cint_ctx_config *config)
{
    const cint_program *p = ctx->program;
    cint_allocator a = ctx->allocator;
    uint32_t n = p->module_count, total = 0u, at = 0u;
    if (config->reserved2 != 0u || (config->host_count > 0u && config->host == NULL) ||
        (config->grants == NULL && config->grants_length != 0u) ||
        (config->args == NULL && config->args_length != 0u) ||
        (config->args_length != 0u && config->args[config->args_length - 1u] != 0u)) {
        return CINT_REFUSED;   /* each program argument ends with its NUL (RT-OQ-76) */
    }
    for (uint32_t j = 0u; j < config->host_count; j++) {
        const cint_host_fn *f = &config->host[j];
        if (f->name == NULL || f->name[0] == '\0' || strchr(f->name, '/') != NULL || f->effect < CINT_PURE ||
            f->effect > CINT_UNRESTRICTED || f->reserved != 0u || f->call == NULL) {
            return CINT_REFUSED;   /* a name with '/' is a service's (rt/cint_fs.c) */
        }
    }
    ctx->grants = config->grants;
    ctx->grants_length = config->grants_length;
    ctx->args = config->args_length != 0u ? config->args : NULL;
    ctx->args_length = config->args_length;
    {
        cint_status st = cint_rt_fs_open(ctx, config);
        if (st != CINT_OK) return st;
    }
    if (config->imports == NULL) {
        return CINT_OK;
    }
    if (!cint_rt_host_tables_valid(config->imports, p)) {
        return CINT_REFUSED;
    }
    ctx->host_tables = config->imports->tables;
    for (uint32_t m = 0u; m < n; m++) {
        total += host_count_of(ctx->host_tables[m]);
    }
    ctx->host_base = (uint32_t *)a.alloc(a.user, (n + 1u) * sizeof(uint32_t), _Alignof(uint32_t));
    if (ctx->host_base == NULL) {
        return CINT_RESOURCE;
    }
    ctx->host_base[n] = total;
    if (total > 0u) {
        ctx->host_fns = (const cint_host_fn **)a.alloc(a.user, total * sizeof(const cint_host_fn *),
                                                       _Alignof(const cint_host_fn *));
        if (ctx->host_fns == NULL) {
            return CINT_RESOURCE;
        }
    }
    for (uint32_t m = 0u; m < n; m++) {
        ctx->host_base[m] = at;
        for (uint32_t k = 0u; k < host_count_of(ctx->host_tables[m]); k++) {
            const cint_host_import *r = &ctx->host_tables[m]->imports[k];
            const cint_host_fn *f = memchr(r->name.bytes, '/', r->name.len) != NULL ? cint_rt_service_find(ctx, r)
                                                                                  : host_find(config, r);
            if (f == NULL) {
                return CINT_REFUSED;
            }
            ctx->host_fns[at++] = f;
        }
    }
    return CINT_OK;
}

void cint_rt_host_release(cint_ctx *ctx)
{
    cint_allocator a = ctx->allocator;
    uint32_t n = ctx->program->module_count;
    cint_rt_service_release(ctx);
    if (ctx->host_base == NULL) {
        return;
    }
    if (ctx->host_fns != NULL) {
        a.release(a.user, (void *)ctx->host_fns, ctx->host_base[n] * sizeof(const cint_host_fn *),
                  _Alignof(const cint_host_fn *));
    }
    a.release(a.user, ctx->host_base, (n + 1u) * sizeof(uint32_t), _Alignof(uint32_t));
    ctx->host_fns = NULL;
    ctx->host_base = NULL;
}

cint_status cint_rt_host_live(const cint_rt_host_frame *c)
{
    return c->fn->call(c->fn->user, c->args, c->nargs, c->result);
}

void cint_rt_arg_view(cint_value *v, uint32_t tag, const void *elements, int64_t origin, int64_t extent, int64_t stride,
                      uint32_t perm)
{
    size_t w = cint_rt_host_width(tag);
    int64_t span = stride < 0 ? -stride : stride;
    memset(v, 0, sizeof *v);
    v->type.code = (uint16_t)tag;
    v->kind = CINT_VAL_VIEW;
    v->as.view.type.code = (uint16_t)tag;
    v->as.view.rank = 1u;
    v->as.view.perm = (uint8_t)perm;
    v->as.view.mem.kind = CINT_MEM_HOST;
    v->as.view.mem.u.host.ptr = (uint8_t *)(uintptr_t)elements + (ptrdiff_t)origin * (ptrdiff_t)w;
    v->as.view.shape[0] = extent;
    v->as.view.byte_stride[0] = stride * (int64_t)w;
    v->as.view.reachable_bytes = extent > 0 ? (span * (extent - 1) + 1) * (int64_t)w : 0;
}

void cint_signature_hash(const uint8_t *signature, size_t length, uint8_t digest[32])
{
    cint_sha256(signature, length, digest);
}

void cint_rt_arg_scalar(cint_value *v, uint32_t tag, uint64_t bits)
{
    memset(v, 0, sizeof *v);
    v->type.code = (uint16_t)tag;
    v->kind = CINT_VAL_SCALAR;
    if (tag == CINT_TAG_BOOL) {
        v->as.i64 = bits != 0u ? 1 : 0;
    } else if (tag == CINT_TAG_I8 || tag == CINT_TAG_I16 || tag == CINT_TAG_I32 || tag == CINT_TAG_I64) {
        v->as.i64 = (int64_t)bits;
    } else {
        v->as.u64 = bits;
    }
}

void cint_rt_result_init(cint_value *r, uint32_t tag)
{
    memset(r, 0, sizeof *r);
    r->type.code = (uint16_t)tag;
    r->kind = tag == 0u ? 0u : CINT_VAL_SCALAR;
}

/* A boundary fault of the call (A-6): E_UNSUPPORTED with the operation host.<part>.<name>,
 * or host.<part> when the name would carry it past CINT_FAULT_MAX_OPERATION, and the
 * host's status as an I64 operand when has_status. Returns false. */
static bool host_fault(cint_ctx *ctx, cint_site site, const char *part, const cint_host_import *r, bool has_status,
                       cint_status status)
{
    char name[256];
    cint_fault_record *rec = cint_rt_record_begin(ctx, site, (uint16_t)CINT_E_UNSUPPORTED);
    if (rec != NULL) {
        cint_rt_record_op(rec, "host");
        cint_rt_record_op(rec, part);
        if (r != NULL && 4u + 1u + strlen(part) + 1u + r->name.len <= CINT_FAULT_MAX_OPERATION) {
            memcpy(name, r->name.bytes, r->name.len);
            name[r->name.len] = '\0';
            cint_rt_record_op(rec, name);
        }
        if (has_status) cint_rt_record_operand(rec, CINT_TAG_I64, (uint64_t)(int64_t)status);
    }
    return false;
}

/* Whether v is an argument this runtime passes: a scalar of its tag, or a rank-1 view of
 * scalars in host memory with a permission of the two. */
static bool host_arg_ok(const cint_value *v)
{
    const cint_arg_view *a = &v->as.view;
    if (v->kind == CINT_VAL_SCALAR) {
        return cint_rt_host_width(v->type.code) != 0u && cint_rt_host_fits(v->type.code, v->as.u64);
    }
    return v->kind == CINT_VAL_VIEW && a->rank == 1u && cint_rt_host_width(a->type.code) != 0u &&
           a->type.code == v->type.code && (a->perm == CINT_VIEW_READ || a->perm == CINT_VIEW_WRITE) &&
           a->mem.kind == CINT_MEM_HOST && a->shape[0] >= 0 && (a->shape[0] == 0 || a->mem.u.host.ptr != NULL);
}

/* Whether the host produced one alternative of the declared result (A-15): the error
 * alternative with a tag of the set, or the value alternative, a value of the scalar type. */
static bool host_result_ok(const cint_value *r, uint32_t error_count)
{
    if (r->is_error != 0u) {
        return r->is_error == 1u && error_count > 0u && r->error >= 1 && (uint64_t)r->error <= error_count;
    }
    return r->kind == 0u || (r->kind == CINT_VAL_SCALAR && cint_rt_host_fits(r->type.code, r->as.u64));
}

bool cint_rt_host_call(cint_ctx *ctx, cint_site site, uint32_t k, cint_value *args, uint32_t nargs, cint_value *result,
                       uint32_t error_count)
{
    const cint_program *p = ctx->program;
    cint_rt_host_frame c;
    uint8_t *saved[16];
    int64_t saved_stride[16], mark;
    cint_status st;
    bool ok;
    if (ctx->head.faulted != 0u) {
        return false;
    }
    if (ctx->host_tables == NULL || ctx->host_fns == NULL || site.module >= p->module_count ||
        k >= host_count_of(ctx->host_tables[site.module]) || nargs > 16u || (nargs > 0u && args == NULL) ||
        result == NULL) {
        return host_fault(ctx, site, "call", NULL, false, CINT_OK);
    }
    c.site = site;
    c.import = &ctx->host_tables[site.module]->imports[k];
    c.fn = ctx->host_fns[ctx->host_base[site.module] + k];
    c.args = args;
    c.nargs = nargs;
    c.error_count = error_count;
    c.result = result;
    if (nargs != c.import->param_count || (result->kind != 0u && result->kind != CINT_VAL_SCALAR) ||
        (result->kind == CINT_VAL_SCALAR && cint_rt_host_width(result->type.code) == 0u)) {
        return host_fault(ctx, site, "call", c.import, false, CINT_OK);
    }
    for (uint32_t i = 0u; i < nargs; i++) {
        if (!host_arg_ok(&args[i])) {
            return host_fault(ctx, site, "call", c.import, false, CINT_OK);
        }
    }
    if (!cint_fuel_charge(ctx, site, 1u)) {
        return false;
    }
    /* An `out` view is written into staging, which reaches the argument only on CINT_OK with
     * the value alternative (H-5, A-15); the hook sees the staging, so a recording holds what
     * the host wrote. */
    mark = cint_rt_scratch_mark(ctx);
    for (uint32_t i = 0u; i < nargs; i++) {
        cint_arg_view *a = &args[i].as.view;
        void *stage = NULL;
        saved[i] = NULL;
        if (args[i].kind != CINT_VAL_VIEW || a->perm != CINT_VIEW_WRITE) {
            continue;
        }
        if (!cint_rt_scratch(ctx, site, a->shape[0], cint_rt_host_width(a->type.code), &stage)) {
            cint_rt_scratch_release(ctx, mark);
            return false;
        }
        saved[i] = (uint8_t *)a->mem.u.host.ptr;
        saved_stride[i] = a->byte_stride[0];
        a->mem.u.host.ptr = stage;
        a->byte_stride[0] = (int64_t)cint_rt_host_width(a->type.code);
        a->reachable_bytes = a->shape[0] * a->byte_stride[0];
    }
    result->is_error = 0u;
    result->error = 0;
    result->as.u64 = 0u;
    st = ctx->host_hook != NULL ? ctx->host_hook(ctx, &c) : cint_rt_host_live(&c);
    ok = st == CINT_OK && host_result_ok(result, error_count);
    for (uint32_t i = 0u; i < nargs; i++) {
        cint_arg_view *a = &args[i].as.view;
        size_t w;
        if (saved[i] == NULL) {
            continue;
        }
        w = cint_rt_host_width(a->type.code);
        if (ok && result->is_error == 0u) {
            for (int64_t j = 0; j < a->shape[0]; j++) {
                memcpy(saved[i] + (ptrdiff_t)j * (ptrdiff_t)saved_stride[i], (const uint8_t *)a->mem.u.host.ptr + (size_t)j * w,
                       w);
            }
        }
        a->mem.u.host.ptr = saved[i];
        a->byte_stride[0] = saved_stride[i];
        a->reachable_bytes = a->shape[0] > 0 ? ((saved_stride[i] < 0 ? -saved_stride[i] : saved_stride[i]) * (a->shape[0] - 1) +
                                                (int64_t)w)
                                             : 0;
    }
    cint_rt_scratch_release(ctx, mark);
    if (st != CINT_OK) {
        return host_fault(ctx, site, "error", c.import, true, st);
    }
    if (!ok) {
        return host_fault(ctx, site, "result", c.import, false, CINT_OK);
    }
    if (c.import->effect != CINT_PURE) ctx->effects++;
    return true;
}

const uint8_t *cint_ctx_args(const cint_ctx *ctx, size_t *length)
{
    /* The program arguments of the configuration (RT-OQ-76), or those replay gave the context. */
    if (length != NULL) *length = ctx != NULL ? ctx->args_length : 0u;
    return ctx != NULL ? ctx->args : NULL;
}
