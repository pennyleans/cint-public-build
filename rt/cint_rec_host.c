/* cint_rec_host.c: extern calls in recordings and replay (SPEC-03 A-15, H-14, H-17; SPEC-07
 * SEC-OS-11; rt/OPEN.md RT-OQ-70), included by rt/cint_rec.c and counted in a 4.2 row of its own.
 *
 * The request and response blocks of an effect event in the host form, as the header comment of
 * rt/cint_rec.c lays them out, from the call as the hook of rt/cint_host.c sees it; the hook a
 * recorder installs; and the hook replay installs. */

/* The elements of a rank-1 view argument: the bytes in full mode, their SHA-256 in digest
 * mode. False when memory runs out. */
static bool host_elements(cint_state_wr *w, const cint_arg_view *a, bool digest)
{
    static const uint8_t none[1] = {0};
    size_t width = cint_rt_host_width(a->type.code), n = (size_t)a->shape[0] * width;
    uint8_t d[32], *gather = n > 0u ? (uint8_t *)malloc(n) : NULL;
    if (n > 0u && gather == NULL) {
        return false;
    }
    for (int64_t i = 0; i < a->shape[0]; i++) {
        memcpy(gather + (size_t)i * width, (const uint8_t *)a->mem.u.host.ptr + (ptrdiff_t)i * (ptrdiff_t)a->byte_stride[0],
               width);
    }
    if (digest) {
        cint_sha256(gather != NULL ? gather : none, n, d);
        cint_state_w_bytes(w, d, 32u);
    } else {
        cint_state_w_bytes(w, gather, n);
    }
    free(gather);
    return true;
}

/* The request block (response false) or the response block of status st (response true) of
 * call c into w, which counts when its buf is NULL. */
static bool host_block(cint_state_wr *w, const cint_rt_host_frame *c, cint_status st, bool digest, bool response)
{
    cint_tvalue v;
    if (!response) {
        cint_state_w_le(w, c->nargs, 4u);
        for (uint32_t i = 0u; i < c->nargs; i++) {
            const cint_value *x = &c->args[i];
            if (x->kind == CINT_VAL_SCALAR) {
                cint_rt_tvalue_scalar(&v, x->type.code, x->as.u64);
                cint_state_w_bytes(w, v.bytes, v.len);
                continue;
            }
            cint_state_w_le(w, 0x52u, 1u);
            cint_state_w_le(w, x->as.view.type.code, 1u);
            cint_state_w_le(w, x->as.view.perm, 1u);
            cint_state_w_le(w, (uint64_t)x->as.view.shape[0], 8u);
            if (x->as.view.perm == CINT_VIEW_READ && !host_elements(w, &x->as.view, digest)) {
                return false;
            }
        }
        return true;
    }
    if (st != CINT_OK) {
        return true;
    }
    cint_state_w_le(w, c->result->is_error != 0u ? 1u : 0u, 1u);
    if (c->result->is_error != 0u) {
        cint_state_w_le(w, (uint64_t)c->result->error, 8u);
        return true;
    }
    if (c->result->kind == CINT_VAL_SCALAR) {
        cint_rt_tvalue_scalar(&v, c->result->type.code, c->result->as.u64);
        cint_state_w_bytes(w, v.bytes, v.len);
    }
    for (uint32_t i = 0u; i < c->nargs; i++) {
        const cint_value *x = &c->args[i];
        if (x->kind == CINT_VAL_VIEW && x->as.view.perm == CINT_VIEW_WRITE && !host_elements(w, &x->as.view, digest)) {
            return false;
        }
    }
    return true;
}

/* A block into memory of its own: *out (NULL when empty) and *len. False when memory runs out. */
static bool host_encode(const cint_rt_host_frame *c, cint_status st, bool digest, bool response, uint8_t **out,
                        size_t *len)
{
    cint_state_wr w = cint_state_wr_of(NULL, 0u);
    *out = NULL;
    *len = 0u;
    if (!host_block(&w, c, st, digest, response)) {
        return false;
    }
    if (w.len == 0u) {
        return true;
    }
    if ((*out = (uint8_t *)malloc(w.len)) == NULL) {
        return false;
    }
    w = cint_state_wr_of(*out, w.len);
    (void)host_block(&w, c, st, digest, response);
    *len = w.len;
    return true;
}

/* The recorded response of a call fed back into it (full mode): the result record, or the
 * staged `out` views. False for a block that does not fit the call. */
static bool host_apply(cint_rt_host_frame *c, cint_status st, const uint8_t *p, size_t n)
{
    cint_state_rd r = cint_state_rd_of(p, n);
    cint_tvalue v;
    if (st != CINT_OK) {
        return n == 0u;
    }
    if (cint_state_rd_flag(&r)) {
        c->result->is_error = 1u;
        c->result->error = cint_state_as_i64(cint_state_rd_u(&r, 8u));
        return !r.bad && r.at == r.n;
    }
    if (c->result->kind == CINT_VAL_SCALAR) {
        cint_state_rd_value(&r, &v, true);
        if (r.bad || v.bytes[0] != c->result->type.code) {
            return false;
        }
        c->result->as.u64 = bits_of(&v);
    }
    for (uint32_t i = 0u; i < c->nargs; i++) {
        cint_arg_view *a = &c->args[i].as.view;
        size_t width, bytes;
        const uint8_t *src;
        if (c->args[i].kind != CINT_VAL_VIEW || a->perm != CINT_VIEW_WRITE) {
            continue;
        }
        width = cint_rt_host_width(a->type.code);
        bytes = (size_t)a->shape[0] * width;
        src = cint_state_rd_take(&r, bytes);
        if (r.bad) {
            return false;
        }
        for (int64_t j = 0; j < a->shape[0]; j++) {
            memcpy((uint8_t *)a->mem.u.host.ptr + (ptrdiff_t)j * (ptrdiff_t)a->byte_stride[0], src + (size_t)j * width,
                   width);
        }
    }
    return !r.bad && r.at == r.n;
}

/* An extern call while a recorder records (SPEC-03 H-14): a `pure` call is made and not
 * recorded (H-6, without audit_pure); an `unrestricted` call writes its event and ends the
 * recording with reason 2, then is made; an `observe` call is made and written as an effect
 * with its request and response blocks and the host's status. */
static cint_status rec_host(cint_ctx *ctx, cint_rt_host_frame *c)
{
    cint_recorder *r = (cint_recorder *)ctx->host_user;
    uint8_t *req = NULL, *resp = NULL;
    size_t nreq = 0u, nresp = 0u;
    cint_status st;
    if (r->ended || c->import->effect == CINT_PURE) {
        return cint_rt_host_live(c);
    }
    r_settle(r);
    if (c->import->effect == CINT_UNRESTRICTED) {
        r_le(r, CINT_EVENT_UNRESTRICTED, 1u);
        r_le(r, (uint64_t)ctx->effects, 8u);
        r_le(r, c->site.module, 4u);
        r_le(r, c->site.index, 4u);
        r_str(r, c->import->name.bytes, c->import->name.len);
        r_flush(r);
        r->events++;
        r_end(r, 2u);
        return cint_rt_host_live(c);
    }
    st = cint_rt_host_live(c);
    if (!host_encode(c, CINT_OK, r->digest, false, &req, &nreq) || !host_encode(c, st, r->digest, true, &resp, &nresp)) {
        r_fail(r);
    }
    r_le(r, CINT_EVENT_EFFECT, 1u);
    r_le(r, (uint64_t)ctx->effects, 8u);
    r_le(r, c->site.module, 4u);
    r_le(r, c->site.index, 4u);
    r_str(r, c->import->name.bytes, c->import->name.len);
    r_le(r, (uint32_t)st, 4u);
    r_le(r, nreq, 8u);
    r_put(r, req, nreq);
    r_le(r, nresp, 8u);
    r_put(r, resp, nresp);
    r_flush(r);
    r->events++;
    free(req);
    free(resp);
    return st;
}

/* An extern call of a replayed entry (SPEC-03 H-17; SPEC-07 SEC-OS-11): a `pure` call is made;
 * another is matched with the next recorded event by ordinal, site, name and request block,
 * then in full mode the recorded response is fed back and no call is made, and in digest mode
 * the call is made and its status and response digests must be the recorded ones. A mismatch
 * diverges, and the call is refused, which faults the entry; an `unrestricted` event stops
 * replay before the call, with CINT_UNREPLAYABLE. */
static cint_status replay_host(cint_ctx *ctx, cint_rt_host_frame *c)
{
    replay_run *x = (replay_run *)ctx->host_user;
    cint_recording_event e = x->last;
    uint8_t *block = NULL;
    size_t n = 0u;
    bool digest = x->rec->mode == CINT_RECORD_DIGEST, same;
    cint_status st;
    if (c->import->effect == CINT_PURE) {
        return cint_rt_host_live(c);
    }
    if (x->diverged || x->stopped || x->unreplayable || x->resource) {
        return CINT_REFUSED;  /* the entry is not compared past here, and makes no call */
    }
    if (x->left == 0u || cint_recording_next(x->rec, &e) != CINT_OK) {
        x->diverged = true;
        x->what = CINT_REPLAY_EFFECT;
        x->at = x->last.index + 1u;  /* the outcome: one effect too many */
        return CINT_REFUSED;
    }
    if (e.kind == CINT_EVENT_UNRESTRICTED) {
        x->unreplayable = true;
        x->at = e.index;
        return CINT_REFUSED;
    }
    x->what = CINT_REPLAY_EFFECT;
    x->at = e.index;
    if (!host_encode(c, CINT_OK, digest, false, &block, &n)) {
        x->resource = true;
        return CINT_REFUSED;
    }
    same = e.kind == CINT_EVENT_EFFECT && e.ordinal == ctx->effects && e.site.module == c->site.module &&
           e.site.index == c->site.index && e.name.len == c->import->name.len &&
           memcmp(e.name.bytes, c->import->name.bytes, e.name.len) == 0 && e.data_len == n &&
           (n == 0u || memcmp(e.data, block, n) == 0);
    free(block);
    if (!same) {
        x->diverged = true;
        return CINT_REFUSED;
    }
    if (digest) {
        st = cint_rt_host_live(c);
        if (!host_encode(c, st, true, true, &block, &n)) {
            x->resource = true;
            return CINT_REFUSED;
        }
        same = st == e.status && e.response_len == n && (n == 0u || memcmp(e.response, block, n) == 0);
        free(block);
    } else {
        st = e.status;
        same = host_apply(c, st, e.response, e.response_len);
    }
    if (!same) {
        x->diverged = true;
        return CINT_REFUSED;
    }
    x->last = e;
    x->left--;
    return st;
}
