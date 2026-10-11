/* cint_state_internal.h: declarations shared by rt/cint_state.c and rt/cint_rec.c.
 *
 * Not part of cint-rt-3; hosts include cint_state.h. rt/cint_state.c, which keeps
 * checkpoints, restore and the state hash, defines everything declared here, and
 * rt/cint_rec.c, which keeps recordings and replay, calls it (decision 2026-10-08 on
 * OQ-240). The Python package and the workbench compile cint_rt.c, cint_state.c and
 * cint_rec.c as one translation unit, so every name here carries the cint_state_ prefix
 * and no static name of one file repeats one of another.
 */
#ifndef CINT_STATE_INTERNAL_H
#define CINT_STATE_INTERNAL_H

#include "cint_rt_internal.h"

#define CINT_STATE_PROFILE "cint-core-1"
#define CINT_STATE_FUEL_MODEL "fuel-v1"
#define CINT_STATE_CONTRACT_MAX 64u
#define CINT_STATE_LIT(s) (s), (sizeof (s) - 1u)

/* The context's allocator. */
void *cint_state_mem_alloc(cint_ctx *ctx, size_t bytes, size_t align);
void cint_state_mem_free(cint_ctx *ctx, void *p, size_t bytes, size_t align);

int64_t cint_state_as_i64(uint64_t u);
uint32_t cint_state_width_of(uint32_t tag);
bool cint_state_utf8_ok(const uint8_t *s, size_t n);
int cint_state_name_cmp(const uint8_t *a, size_t an, const uint8_t *b, size_t bn);

/* Little-endian bytes into buf, or counted only when buf is NULL. */
typedef struct cint_state_wr {
    uint8_t *buf;  /* NULL: count only */
    size_t len, cap;
} cint_state_wr;

cint_state_wr cint_state_wr_of(uint8_t *buf, size_t cap);
void cint_state_w_bytes(cint_state_wr *w, const void *p, size_t n);
void cint_state_w_le(cint_state_wr *w, uint64_t v, unsigned n);
void cint_state_w_str(cint_state_wr *w, const void *s, size_t n);

/* A reader of little-endian bytes; bad once a read runs past the end or fails a check. */
typedef struct cint_state_rd {
    const uint8_t *p;
    size_t n, at;
    bool bad;
} cint_state_rd;

/* A fault record of encoding `cint-core-1/fault/v2` or `/v3` (SPEC-01 IM-149), read as the
 * workbench's decoder reads it (workbench/wb_fault.c) with the run-time bounds. */
typedef struct cint_state_fault_view {
    uint16_t code;
    uint8_t operation_len, operand_count, phase, version;
    bool has_exact, has_limit, has_revision, has_map, has_address, has_work_item;
    const uint8_t *operation, *revision, *map, *kernel;
    cint_tvalue operands[CINT_FAULT_MAX_OPERANDS], exact, limit;
    cint_position position;
    uint32_t kernel_len, stack_count;
    int64_t dispatch, work_item, step;
    size_t stack_at;  /* where the call positions begin */
} cint_state_fault_view;

cint_state_rd cint_state_rd_of(const uint8_t *p, size_t n);
uint64_t cint_state_rd_u(cint_state_rd *r, unsigned k);
const uint8_t *cint_state_rd_take(cint_state_rd *r, uint64_t k);
bool cint_state_rd_flag(cint_state_rd *r);
void cint_state_rd_lit(cint_state_rd *r, const char *s, size_t n);
void cint_state_rd_value(cint_state_rd *r, cint_tvalue *v, bool scalar);
void cint_state_rd_fault(cint_state_rd *r, cint_state_fault_view *f);

cint_status cint_state_boundary(cint_ctx *ctx, uint8_t hash[32], uint8_t **keep, size_t *keep_len);

/* The text of `cint-interp show` into out, or counted only when out is NULL. */
typedef struct cint_state_tx {
    char *out;
    size_t cap, len;
} cint_state_tx;

void cint_state_t_put(cint_state_tx *t, const void *s, size_t n);
void cint_state_t_str(cint_state_tx *t, const char *s);
void cint_state_t_u64(cint_state_tx *t, uint64_t v);
void cint_state_t_i64(cint_state_tx *t, int64_t v);
void cint_state_t_hex(cint_state_tx *t, const uint8_t *p, size_t n);
void cint_state_t_value(cint_state_tx *t, const char *label, const cint_tvalue *v);
void cint_state_t_fault(cint_state_tx *t, const uint8_t *fault, size_t len);

#endif
