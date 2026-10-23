/* cint_state.c: checkpoints of a context (rt/cint_state.h; roadmap box 13, units 4 and 8a).
 *
 * The canonical state of SPEC-01 IM-156 in the byte layout below, which unit 4 writes
 * (SPEC-03 question 10; rt/OPEN.md RT-OQ-42) and unit 8a gives arenas and pools (RT-OQ-46),
 * the CINT-CKPT-1 envelope (SPEC-03 H-19; SPEC-06
 * 15), the validating decoder (H-19a), restore (M-11, M-11a), the interrupt, and the change of
 * revision (H-19b). Recordings and replay are in rt/cint_rec.c, which calls the helpers of
 * rt/cint_state_internal.h defined here (decision 2026-10-08 on OQ-240). Integers are
 * little-endian; a string is a U32 length and its bytes.
 *
 *   checkpoint  "cint-core-1/checkpoint/v1"; the runtime contract version; the presence bytes
 *               of the payload digest, the build record digest and the build identity, each
 *               followed by 32 bytes when 1 (written 0 until build records exist); the U64
 *               length of the state; the state; nothing after it
 *   state       "cint-core-1/state/v1"; the revision identity (32 bytes); "cint-core-1"; a U64
 *               count and the globals, sorted by the bytes of their names; the arena, pool and
 *               registry blocks, each a U64 length (0 when it holds nothing) and its bytes; the
 *               next buffer, arena and pool identifiers and the entry sequence number, U64 each;
 *               "fuel-v1"; the fault record, a presence byte and its IM-149 bytes; and the
 *               effect-log position, an I64
 *   global      the fully qualified name (variable `x` of `a/b.ci` is `a.b.x`, SPEC-04 LS-225)
 *               and the tagged value (SPEC-01 11.1): a scalar, I128 to I1024 or U128 to
 *               U512 (w / 8 little-endian bytes, IM-14), or an array of them, tag 51
 *               (RT-OQ-49); a handle, tag 75 and its 29 bytes (RT-OQ-50); or a struct, tag
 *               71, its type as an element type is written below but without the element's
 *               bytes and with array fields of rank 1 to 4, then its payload (OQ-260,
 *               RT-OQ-51)
 *   arenas      "cint-core-1/arena/v2"; a U64 count, at least 1; each arena in ascending
 *               identifier order, the module-level ones first (1 to n), 41 bytes: identifier
 *               U32, parent identifier U32 (0 for a module-level arena), retired U8 (1 when it
 *               or an ancestor is), offset I64 (a child's element 0 in its parent, 0 for a
 *               module-level arena), capacity and used extent I64 each, generation U64; then
 *               each module-level arena's element type and its elements up to its used
 *               extent, in identifier order. A child's type and elements are its parent's. The
 *               block is empty until the context's first arena or pool operation makes its
 *               arenas and pools (rt/cint_mem.c)
 *   pools       "cint-core-1/pool/v2"; a U64 count, at least 1; each pool in identifier order
 *               (1 to n): identifier U32, element type, capacity I64, the count of slots ever
 *               used U64 (each later slot is vacant at generation 0), the slot the next insert
 *               reuses plus 1 U64 (0 when none is free); for each slot ever used, 17 bytes:
 *               generation U64, mark U8 (0 vacant, 1 occupied, 2 retired), and the slot below
 *               it on the free list plus 1 U64 (0 at the bottom and for a slot not on it); then
 *               each occupied slot's element in slot order. Empty as the arenas are
 *   element     an arena's or a pool's (decision 2026-10-08 on OQ-244): the bytes of one
 *   type        element U32, then the type as SPEC-03 5.6 writes one, with the definitions it
 *               names: their count U32, each struct's definition (A-21: 71, the qualified name,
 *               layout 00, the field count U32, and per field its name, bit width 00 and
 *               type), each after those it names, the element's own last; then the type: an
 *               integer tag of SPEC-01 IM-146, 75 for a handle, or 71 and the definition's
 *               index U32. A field's array is 52, its element, rank 01 and the extent as form
 *               01 with no size symbol (FFFFFFFF) and an I64. Each element is its IM-146
 *               payload: an integer at its width (in 8-byte cells, a wide one takes one per
 *               limb), a handle as its first 29 bytes (slot, length, generation, container,
 *               kind), and a struct's fields in declaration order with no padding. The bytes
 *               of one element let a reader without the program frame the elements
 *   registry    "cint-core-1/registry/v1"; a U64 count, at least 1; each live registration in
 *               ascending identifier order, 37 bytes: identifier U64, generation U64, owner U8
 *               (CINT_OWNER_*), element type code U16, storage U8, frac_bits U16 and record_id
 *               U32, extent I64 (bytes for a registration without an element type, code 0),
 *               ceiling U8, publish U8, state U8 (0, live). No contents: none of these buffers
 *               is context-owned (M-3).
 */
#define _CRT_SECURE_NO_WARNINGS 1  /* fopen (cint_checkpoint_show) */
#include "cint_rt_internal.h"
#include "cint_state.h"
#include "cint_state_internal.h"
#define CINT_MEM_STATE 1  /* a context's arenas and pools (rt/cint_mem.h) */
#include "cint_mem.h"

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char CKPT_DOMAIN[] = "cint-core-1/checkpoint/v1";
static const char STATE_DOMAIN[] = "cint-core-1/state/v1";
static const char REGISTRY_DOMAIN[] = "cint-core-1/registry/v1";
static const char ARENA_DOMAIN[] = "cint-core-1/arena/v2";
static const char POOL_DOMAIN[] = "cint-core-1/pool/v2";
static const char FAULT_V2[] = "cint-core-1/fault/v2", FAULT_V3[] = "cint-core-1/fault/v3";
#define ENTRY_BYTES 37u
#define ARENA_ROW 41u
#define SLOT_BYTES 17u
#define ID_END UINT64_C(4294967296)  /* identifiers fit a handle's U32 container (M-12) */
#define GEN_MAX (UINT64_MAX - 1u)    /* the last usable generation (M-16) */
#define KERNEL_MAX 256u
_Static_assert(sizeof CINT_RT_CONTRACT - 1u <= CINT_STATE_CONTRACT_MAX, "contract");
_Static_assert(sizeof ((cint_fault_address *)0)->name == KERNEL_MAX, "kernel name");

/* The table of a context bound to a program without module state. */
static const cint_state_table NO_STATE = {0u, 0u, NULL, NULL};

void *cint_state_mem_alloc(cint_ctx *ctx, size_t bytes, size_t align)
{
    return bytes == 0u ? NULL : ctx->allocator.alloc(ctx->allocator.user, bytes, align);
}

void cint_state_mem_free(cint_ctx *ctx, void *p, size_t bytes, size_t align)
{
    if (p != NULL) {
        ctx->allocator.release(ctx->allocator.user, p, bytes, align);
    }
}

int64_t cint_state_as_i64(uint64_t u)
{
    int64_t v;
    memcpy(&v, &u, sizeof v);
    return v;
}

static uint64_t le64(const uint8_t *p)
{
    uint64_t v = 0u;
    for (unsigned b = 0u; b < 8u; b++) {
        v |= (uint64_t)p[b] << (8u * b);
    }
    return v;
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8u | (uint32_t)p[2] << 16u | (uint32_t)p[3] << 24u;
}

/* Bool, or I8 to U64: the bytes of the value; 0 for any other tag. */
uint32_t cint_state_width_of(uint32_t tag)
{
    if (tag == CINT_TAG_BOOL) {
        return 1u;
    }
    if ((tag >= CINT_TAG_I8 && tag <= CINT_TAG_I64) || (tag >= CINT_TAG_U8 && tag <= CINT_TAG_U64)) {
        return cint_rt_tag_width(tag) / 8u;
    }
    return 0u;
}

/* A value's payload in canonical state (IM-146): Bool's or I8 to U64's, or the w / 8 bytes of
 * I128 to I1024 and U128 to U512 (tags 15 to 18 and 25 to 27, rt/cint_wide.h), little-endian
 * (IM-14); 0 for any other tag. A recording's arguments and results stay scalars
 * (cint_state_width_of). */
static uint32_t value_width(uint32_t tag)
{
    return (tag >= 0x15u && tag <= 0x18u) || (tag >= 0x25u && tag <= 0x27u) ? cint_rt_tag_width(tag) / 8u
                                                                              : cint_state_width_of(tag);
}

bool cint_state_utf8_ok(const uint8_t *s, size_t n)
{
    size_t i = 0u;
    while (i < n) {
        uint32_t c = s[i], k, least;
        if (c < 0x80u) {
            i++;
            continue;
        }
        if (c >= 0xc2u && c <= 0xdfu) {
            k = 1u, c &= 0x1fu, least = 0x80u;
        } else if (c >= 0xe0u && c <= 0xefu) {
            k = 2u, c &= 0x0fu, least = 0x800u;
        } else if (c >= 0xf0u && c <= 0xf4u) {
            k = 3u, c &= 0x07u, least = 0x10000u;
        } else {
            return false;
        }
        if (n - i - 1u < k) {
            return false;
        }
        for (uint32_t j = 1u; j <= k; j++) {
            uint32_t b = s[i + j];
            if ((b & 0xc0u) != 0x80u) {
                return false;
            }
            c = (c << 6) | (b & 0x3fu);
        }
        if (c < least || c > 0x10ffffu || (c >= 0xd800u && c <= 0xdfffu)) {
            return false;
        }
        i += k + 1u;
    }
    return true;
}

/* Byte order, a prefix before the longer name. */
int cint_state_name_cmp(const uint8_t *a, size_t an, const uint8_t *b, size_t bn)
{
    int c = memcmp(a, b, an < bn ? an : bn);
    return c != 0 ? c : an < bn ? -1 : an > bn ? 1 : 0;
}

/* ------------------------------------------------------------------------- */
/* Writing and reading bytes.                                                 */

cint_state_wr cint_state_wr_of(uint8_t *buf, size_t cap)
{
    cint_state_wr w;
    w.buf = buf;
    w.len = 0u;
    w.cap = cap;
    return w;
}

void cint_state_w_bytes(cint_state_wr *w, const void *p, size_t n)
{
    if (w->buf != NULL && n > 0u) {
        memcpy(w->buf + w->len, p, n);
    }
    w->len += n;
}

void cint_state_w_le(cint_state_wr *w, uint64_t v, unsigned n)
{
    uint8_t b[8];
    for (unsigned i = 0u; i < n; i++) {
        b[i] = (uint8_t)(v >> (8u * i));
    }
    cint_state_w_bytes(w, b, n);
}

void cint_state_w_str(cint_state_wr *w, const void *s, size_t n)
{
    cint_state_w_le(w, n, 4u);
    cint_state_w_bytes(w, s, n);
}

cint_state_rd cint_state_rd_of(const uint8_t *p, size_t n)
{
    cint_state_rd r;
    r.p = p;
    r.n = n;
    r.at = 0u;
    r.bad = false;
    return r;
}

uint64_t cint_state_rd_u(cint_state_rd *r, unsigned k)
{
    uint64_t v = 0u;
    if (r->bad || r->n - r->at < k) {
        r->bad = true;
        return 0u;
    }
    for (unsigned i = 0u; i < k; i++) {
        v |= (uint64_t)r->p[r->at + i] << (8u * i);
    }
    r->at += k;
    return v;
}

const uint8_t *cint_state_rd_take(cint_state_rd *r, uint64_t k)
{
    const uint8_t *p;
    if (r->bad || (uint64_t)(r->n - r->at) < k) {
        r->bad = true;
        return NULL;
    }
    p = r->p + r->at;
    r->at += (size_t)k;
    return p;
}

bool cint_state_rd_flag(cint_state_rd *r)
{
    uint64_t v = cint_state_rd_u(r, 1u);
    r->bad = r->bad || v > 1u;
    return v == 1u;
}

void cint_state_rd_lit(cint_state_rd *r, const char *s, size_t n)
{
    const uint8_t *p = cint_state_rd_u(r, 4u) == n ? cint_state_rd_take(r, n) : NULL;
    if (p == NULL || memcmp(p, s, n) != 0) {
        r->bad = true;
    }
}

/* A tagged value (SPEC-01 11.1) into v when v is not NULL. A Z is minimal and at most 264
 * bytes, the bound of a run-time record of version 3 (IM-108), which cint_state_rd_fault
 * narrows for version 2; `scalar` admits Bool and I8 to U64 only, otherwise I128 to I1024,
 * U128 to U512 and a Z are values too. */
void cint_state_rd_value(cint_state_rd *r, cint_tvalue *v, bool scalar)
{
    size_t start = r->at;
    uint32_t tag = (uint32_t)cint_state_rd_u(r, 1u);
    if (tag == CINT_TAG_Z && !scalar) {
        uint64_t n = cint_state_rd_u(r, 4u);
        const uint8_t *p = n <= CINT_Z_MAX_BYTES ? cint_state_rd_take(r, n) : NULL;
        if (p == NULL || (n == 1u && p[0] == 0u) ||
            (n >= 2u && ((p[n - 1u] == 0u && p[n - 2u] < 0x80u) || (p[n - 1u] == 0xffu && p[n - 2u] >= 0x80u)))) {
            r->bad = true;
        }
    } else if (tag == 0x31u && !scalar) {
        /* Q<i>.<f> (rt/cint_fixed.h): the storage tag I8 to I64 or I128 (OQ-292; SPEC-01 11.1 row 31),
         * f below its width, the raw integer. */
        uint32_t st = (uint32_t)cint_state_rd_u(r, 1u), f = (uint32_t)cint_state_rd_u(r, 2u);
        if (st < CINT_TAG_I8 || (st > CINT_TAG_I64 && st != 0x15u) || f >= cint_rt_tag_width(st) ||
            cint_state_rd_take(r, cint_rt_tag_width(st) / 8u) == NULL) {
            r->bad = true;
        }
    } else if ((scalar ? cint_state_width_of(tag) : value_width(tag)) != 0u) {
        const uint8_t *p = cint_state_rd_take(r, value_width(tag));
        if (p != NULL && tag == CINT_TAG_BOOL && p[0] > 1u) {
            r->bad = true;
        }
    } else {
        r->bad = true;
    }
    if (!r->bad && v != NULL) {
        v->len = (uint16_t)(r->at - start);
        memcpy(v->bytes, r->p + start, r->at - start);
    }
}

#define TAG_ARRAY 0x51u

/* A Handle global's payload (OQ-242; rt/OPEN.md RT-OQ-50): its U64 slot, I64 length, U64
 * generation, U32 container and U8 kind, the first 29 bytes of its cint_handle on a
 * little-endian host (IM-146), without the reserved bytes. */
#define HANDLE_PAYLOAD 29u
_Static_assert(offsetof(cint_handle, kind) + 1u == HANDLE_PAYLOAD, "cint_handle payload");

/* The payload bytes of a global of tag `tag`, 0 for a tag no global takes. */
static uint32_t global_width(uint32_t tag)
{
    return tag == CINT_TAG_HANDLE ? HANDLE_PAYLOAD : value_width(tag);
}

/* ------------------------------------------------------------------------- */
/* Element types (box 13 unit 8b-3; decision 2026-10-08 on OQ-244).          */
/*                                                                           */
/* An arena or pool element enters the blocks as its IM-146 payload: each    */
/* scalar at its width, a handle as its first 29 bytes, a struct's fields in */
/* declaration order with no padding. The module's table gives the type and  */
/* where it stores each part (cint_mem_decl); a ty_plan is the two measured.  */

#define TY_STRUCT 0x71u
#define TY_ARRAY 0x52u

/* A field of a definition, or the element: a scalar's or a handle's tag, or TY_STRUCT and its definition;
 * `count` items `stride` stored bytes apart from `offset` in the stored struct. */
typedef struct ty_part {
    uint32_t tag, def;
    uint64_t count, offset, stride;
} ty_part;

typedef struct ty_def {
    uint32_t first, nfield;  /* its fields among the plan's parts */
    uint64_t width, bytes;   /* its payload's bytes, and its stored size */
} ty_def;

typedef struct ty_plan {
    ty_def *defs;
    ty_part *parts;
    uint32_t ndef, npart;
    ty_part elem;
    uint64_t width;          /* the element's payload bytes */
    uint32_t cell;           /* cint_mem_decl.cell */
} ty_plan;

/* One item of a type at r: a scalar's or a handle's tag, or 71 and a definition below `below`. */
static void rd_item(cint_state_rd *r, ty_part *x, uint32_t below)
{
    x->tag = (uint32_t)cint_state_rd_u(r, 1u);
    x->def = x->tag == TY_STRUCT ? (uint32_t)cint_state_rd_u(r, 4u) : 0u;
    r->bad = r->bad || (x->tag == TY_STRUCT ? x->def >= below : global_width(x->tag) == 0u);
}

/* A name at r: a U32 length of at least 1, then UTF-8. */
static void rd_tyname(cint_state_rd *r)
{
    uint64_t n = cint_state_rd_u(r, 4u);
    const uint8_t *s = cint_state_rd_take(r, n);
    r->bad = r->bad || n == 0u || !cint_state_utf8_ok(s, (size_t)n);
}

/* The type at r after its width (the header comment), checked as far as it can be without the program:
 * names in UTF-8, layout and bit widths 0, a field's array of rank 1 to `maxrank` with literal extents, each
 * definition naming only earlier ones, and the type a scalar, a handle or the last definition. Its counts of
 * definitions and fields into *ndef and *npart, and where the last definition's name is into *name (0 for
 * none); with p, whose arrays have those counts, its definitions and parts too; with `widths`, room for its
 * definitions, each one's payload bytes there, and the type's are returned, at most 2^32 - 1 (0 otherwise). */
static uint64_t rd_type(cint_state_rd *r, ty_plan *p, uint64_t *widths, uint32_t maxrank, uint32_t *ndef,
                        uint32_t *npart, size_t *name)
{
    uint32_t n = (uint32_t)cint_state_rd_u(r, 4u);
    uint64_t parts = 0u, width = 0u;
    ty_part e = {0u, 0u, 1u, 0u, 0u};
    *name = 0u;
    for (uint32_t k = 0u; k < n && !r->bad; k++) {
        uint64_t nf = 0u, dw = 0u;
        r->bad = cint_state_rd_u(r, 1u) != TY_STRUCT;
        *name = r->at;
        rd_tyname(r);
        r->bad = r->bad || cint_state_rd_u(r, 1u) != 0u;  /* layout 00 (SPEC-04 LS-73) */
        nf = cint_state_rd_u(r, 4u);
        if (p != NULL && !r->bad) {
            p->defs[k].first = (uint32_t)parts;
            p->defs[k].nfield = (uint32_t)nf;
        }
        for (uint64_t j = 0u; j < nf && !r->bad; j++, parts++) {
            ty_part x = {0u, 0u, 1u, 0u, 0u};
            rd_tyname(r);
            r->bad = r->bad || cint_state_rd_u(r, 1u) != 0u;  /* no bit field */
            if (!r->bad && r->at < r->n && r->p[r->at] == TY_ARRAY) {
                uint32_t rank = 0u;
                r->at++;
                rd_item(r, &x, k);
                rank = (uint32_t)cint_state_rd_u(r, 1u);
                r->bad = r->bad || rank == 0u || rank > maxrank;
                for (uint32_t d = 0u; d < rank && !r->bad; d++) {
                    int64_t extent = 0;
                    r->bad = cint_state_rd_u(r, 1u) != 1u || cint_state_rd_u(r, 4u) != UINT32_MAX;  /* an extent */
                    extent = cint_state_as_i64(cint_state_rd_u(r, 8u));
                    r->bad = r->bad || extent < 0 || (extent > 0 && x.count > UINT64_MAX / (uint64_t)extent);
                    x.count *= r->bad ? 0u : (uint64_t)extent;
                }
            } else {
                rd_item(r, &x, k);
            }
            if (widths != NULL && !r->bad) {
                uint64_t w = x.tag == TY_STRUCT ? widths[x.def] : global_width(x.tag);
                r->bad = w > 0u && x.count > (UINT32_MAX - dw) / w;
                dw += r->bad ? 0u : x.count * w;
            }
            if (p != NULL && !r->bad && parts < p->npart) {
                p->parts[parts] = x;
            }
        }
        if (widths != NULL && !r->bad) {
            widths[k] = dw;
        }
    }
    rd_item(r, &e, n);
    r->bad = r->bad || (e.tag == TY_STRUCT ? e.def + 1u != n : n != 0u) || parts > UINT32_MAX;
    *ndef = n;
    *npart = r->bad ? 0u : (uint32_t)parts;
    if (p != NULL) {
        p->elem = e;
    }
    if (widths != NULL && !r->bad) {
        width = e.tag == TY_STRUCT ? widths[e.def] : global_width(e.tag);
    }
    return width;
}

/* An element type at r: its width, then its type, a scalar's or a handle's of its own width. */
static void rd_elem_type(cint_state_rd *r, uint32_t *width)
{
    uint32_t ndef = 0u, npart = 0u;
    size_t at = 0u, name = 0u;
    *width = (uint32_t)cint_state_rd_u(r, 4u);
    at = r->at;
    (void)rd_type(r, NULL, NULL, 1u, &ndef, &npart, &name);
    r->bad = r->bad || (ndef == 0u && *width != global_width(r->p[at + 4u]));
}

/* The payload bytes and stored size of one item of x: in 8-byte cells, an integer or a Bool in
 * one cell, and I128 to I1024 or U128 to U512 in width / 8 cells, its limbs (RT-OQ-51). */
static void ty_item(const ty_plan *p, const ty_part *x, uint64_t *width, uint64_t *bytes)
{
    *width = x->tag == TY_STRUCT ? p->defs[x->def].width : global_width(x->tag);
    *bytes = x->tag == TY_STRUCT ? p->defs[x->def].bytes : x->tag == CINT_TAG_HANDLE ? sizeof(cint_handle)
             : p->cell != 0u && *width < 8u ? 8u : *width;
}

/* Measures p against a layout of `len` words: each definition's payload and stored size, and each field's
 * offset and stride, inside its definition's storage where it has a payload. False when the layout does not
 * fit. */
static bool ty_measure(ty_plan *p, const uint32_t *layout, uint32_t len)
{
    uint64_t at = 0u, w, b;
    for (uint32_t k = 0u; k < p->ndef; k++) {
        ty_def *f = &p->defs[k];
        if (at + f->nfield >= len) {
            return false;
        }
        f->bytes = layout[at++];
        for (uint32_t j = 0u; j < f->nfield; j++) {
            ty_part *x = &p->parts[f->first + j];
            x->offset = layout[at++];
            ty_item(p, x, &w, &b);
            x->stride = b;
            if (w > 0u && (x->count > (UINT32_MAX - f->width) / w || x->offset > f->bytes || b == 0u ||
                           x->count > (f->bytes - x->offset) / b)) {
                return false;
            }
            f->width += x->count * w;
        }
    }
    ty_item(p, &p->elem, &p->width, &b);
    p->elem.stride = b;
    return at == len && p->width <= UINT32_MAX;
}

static void ty_free(cint_ctx *ctx, ty_plan *p)
{
    cint_state_mem_free(ctx, p->defs, (size_t)p->ndef * sizeof *p->defs, 8u);
    cint_state_mem_free(ctx, p->parts, (size_t)p->npart * sizeof *p->parts, 8u);
    memset(p, 0, sizeof *p);
}

/* The plan of a type and its layout (rt/cint_mem.h), array fields of rank 1 to `maxrank`, into *p:
 * CINT_REFUSED for a type or layout that is not one, CINT_RESOURCE when memory runs out, with *p empty then. */
static cint_status ty_plan_from(cint_ctx *ctx, const uint8_t *type, uint32_t type_len, const uint32_t *layout,
                                uint32_t layout_len, uint32_t cell, uint32_t maxrank, ty_plan *p)
{
    cint_state_rd r = cint_state_rd_of(type, type_len);
    size_t name = 0u;
    memset(p, 0, sizeof *p);
    (void)rd_type(&r, NULL, NULL, maxrank, &p->ndef, &p->npart, &name);
    if (r.bad || r.at != r.n) {
        return CINT_REFUSED;
    }
    p->defs = (ty_def *)cint_state_mem_alloc(ctx, (size_t)p->ndef * sizeof *p->defs, 8u);
    p->parts = (ty_part *)cint_state_mem_alloc(ctx, (size_t)p->npart * sizeof *p->parts, 8u);
    if ((p->ndef > 0u && p->defs == NULL) || (p->npart > 0u && p->parts == NULL)) {
        ty_free(ctx, p);
        return CINT_RESOURCE;
    }
    if (p->defs != NULL) {
        memset(p->defs, 0, (size_t)p->ndef * sizeof *p->defs);
    }
    p->cell = cell;
    r = cint_state_rd_of(type, type_len);
    (void)rd_type(&r, p, NULL, maxrank, &p->ndef, &p->npart, &name);
    if (!ty_measure(p, layout, layout_len)) {
        ty_free(ctx, p);
        return CINT_REFUSED;
    }
    return CINT_OK;
}

/* The plan of d into *p, as ty_plan_from gives it, refused too when its element is not stored in d's
 * elem_bytes. */
static cint_status ty_plan_of(cint_ctx *ctx, const cint_mem_decl *d, ty_plan *p)
{
    cint_status st = ty_plan_from(ctx, d->type, d->type_len, d->layout, d->layout_len, d->cell, 1u, p);
    if (st == CINT_OK && p->elem.stride != d->elem_bytes) {
        ty_free(ctx, p);
        st = CINT_REFUSED;
    }
    return st;
}

/* Writes the payload of item x stored at `at`. */
static void ty_put(const ty_plan *p, const ty_part *x, const uint8_t *at, cint_state_wr *w)
{
    for (uint64_t i = 0u; i < x->count; i++, at += x->stride) {
        if (x->tag != TY_STRUCT) {
            cint_state_w_bytes(w, at, global_width(x->tag));  /* the low bytes: little-endian hosts */
            continue;
        }
        for (uint32_t j = 0u; j < p->defs[x->def].nfield; j++) {
            const ty_part *y = &p->parts[p->defs[x->def].first + j];
            ty_put(p, y, at + y->offset, w);
        }
    }
}

/* Whether the payload of a handle is one: kind 0 with every byte 0, arena or pool (H-19a). */
static bool handle_ok(const uint8_t *v)
{
    bool zero = true;
    for (uint32_t k = 0u; k + 1u < HANDLE_PAYLOAD; k++) {
        zero = zero && v[k] == 0u;
    }
    return v[HANDLE_PAYLOAD - 1u] <= CINT_HANDLE_POOL && (v[HANDLE_PAYLOAD - 1u] != 0u || zero);
}

/* Reads the payload of item x at *in into zero storage at `at`: a Bool 0 or 1, a handle that handle_ok
 * accepts, and a signed scalar sign-extended through its cell. False at the first that is not one. */
static bool ty_get(const ty_plan *p, const ty_part *x, const uint8_t **in, uint8_t *at)
{
    for (uint64_t i = 0u; i < x->count; i++, at += x->stride) {
        uint32_t n = global_width(x->tag);
        const uint8_t *v = *in;
        if (x->tag == TY_STRUCT) {
            for (uint32_t j = 0u; j < p->defs[x->def].nfield; j++) {
                const ty_part *y = &p->parts[p->defs[x->def].first + j];
                if (!ty_get(p, y, in, at + y->offset)) {
                    return false;
                }
            }
            continue;
        }
        if ((x->tag == CINT_TAG_BOOL && v[0] > 1u) || (x->tag == CINT_TAG_HANDLE && !handle_ok(v))) {
            return false;
        }
        memcpy(at, v, n);
        if (p->cell != 0u && n < 8u && x->tag != CINT_TAG_HANDLE && cint_rt_tag_signed(x->tag) &&
            (v[n - 1u] & 0x80u) != 0u) {
            memset(at + n, 0xff, 8u - n);
        }
        *in += n;
    }
    return true;
}

/* Whether the payload of item x at *in is one that ty_get takes; *in moves past what was read. */
static bool ty_ok(const ty_plan *p, const ty_part *x, const uint8_t **in)
{
    for (uint64_t i = 0u; i < x->count; i++) {
        if (x->tag == TY_STRUCT) {
            for (uint32_t j = 0u; j < p->defs[x->def].nfield; j++) {
                if (!ty_ok(p, &p->parts[p->defs[x->def].first + j], in)) {
                    return false;
                }
            }
            continue;
        }
        if ((x->tag == CINT_TAG_BOOL && (*in)[0] > 1u) || (x->tag == CINT_TAG_HANDLE && !handle_ok(*in))) {
            return false;
        }
        *in += global_width(x->tag);
    }
    return true;
}

/* The type of a struct global at r (OQ-260), checked as an element type is with array fields of rank 1 to 4,
 * a struct's: returns its payload bytes. */
static uint64_t rd_struct_type(cint_state_rd *r)
{
    uint64_t small[16], *widths = small, width = 0u;
    uint32_t ndef = 0u, npart = 0u;
    size_t name = 0u;
    cint_state_rd x = *r;
    (void)rd_type(&x, NULL, NULL, 4u, &ndef, &npart, &name);
    if (x.bad || ndef == 0u || (ndef > 16u && (widths = (uint64_t *)malloc((size_t)ndef * sizeof *widths)) == NULL)) {
        r->bad = true;
        return 0u;
    }
    width = rd_type(r, NULL, widths, 4u, &ndef, &npart, &name);
    if (widths != small) {
        free(widths);
    }
    return width;
}

/* A global's tagged value (IM-156): a scalar or a wide integer into v, or an array of them
 * (tag 51; IM-148), whose descriptor goes into v and whose elements into *elements and *count;
 * or a struct (tag 71; OQ-260), whose tag goes into v and the rest, its type and then its
 * payload, into *elements, *count their bytes. */
static void rd_global(cint_state_rd *r, cint_tvalue *v, const uint8_t **elements, uint64_t *count)
{
    size_t start = r->at;
    uint32_t tag, rank, w;
    uint64_t n = 1u;
    *elements = NULL;
    *count = 0u;
    if (!r->bad && r->at < r->n && r->p[r->at] == TY_STRUCT) {
        uint64_t width;
        r->at++;
        width = rd_struct_type(r);
        r->bad = r->bad || cint_state_rd_take(r, width) == NULL;
        if (!r->bad && v != NULL) {
            v->len = 1u;
            v->bytes[0] = TY_STRUCT;
        }
        *elements = r->bad ? NULL : r->p + start + 1u;
        *count = r->bad ? 0u : r->at - start - 1u;
        return;
    }
    if (!r->bad && r->at < r->n && r->p[r->at] == CINT_TAG_HANDLE) {
        /* a Handle: kind 0 (the null handle, every byte 0), arena or pool */
        const uint8_t *p = cint_state_rd_take(r, 1u + HANDLE_PAYLOAD);
        r->bad = r->bad || p == NULL || !handle_ok(p + 1u);
        if (!r->bad && v != NULL) {
            v->len = (uint16_t)(1u + HANDLE_PAYLOAD);
            memcpy(v->bytes, p, 1u + HANDLE_PAYLOAD);
        }
        return;
    }
    if (!r->bad && r->at < r->n && value_width(r->p[r->at]) > 8u) {  /* I128 to I1024, U128 to U512 */
        uint32_t n_bytes = 1u + value_width(r->p[r->at]);
        const uint8_t *p = cint_state_rd_take(r, n_bytes);
        r->bad = r->bad || p == NULL;
        if (!r->bad && v != NULL) {
            v->len = (uint16_t)n_bytes;
            memcpy(v->bytes, p, n_bytes);
        }
        return;
    }
    if (r->bad || r->at >= r->n || r->p[r->at] != TAG_ARRAY) {
        cint_state_rd_value(r, v, true);
        return;
    }
    r->at++;
    tag = (uint32_t)cint_state_rd_u(r, 1u);
    rank = (uint32_t)cint_state_rd_u(r, 1u);
    w = value_width(tag);
    r->bad = r->bad || w == 0u || rank == 0u || rank > 8u;
    for (uint32_t d = 0u; d < rank && !r->bad; d++) {
        int64_t e = cint_state_as_i64(cint_state_rd_u(r, 8u));
        r->bad = r->bad || e < 0 || (e > 0 && n > UINT64_MAX / (uint64_t)e);
        n *= r->bad ? 0u : (uint64_t)e;
    }
    if (!r->bad && v != NULL) {
        v->len = (uint16_t)(r->at - start);
        memcpy(v->bytes, r->p + start, r->at - start);
    }
    *elements = !r->bad && n <= (uint64_t)(r->n - r->at) / w ? cint_state_rd_take(r, n * w) : NULL;
    for (uint64_t i = 0u; *elements != NULL && tag == CINT_TAG_BOOL && i < n; i++) {
        r->bad = r->bad || (*elements)[i] > 1u;
    }
    r->bad = r->bad || *elements == NULL;
    *count = n;
}

/* A position of a fault record: path, line, column (SPEC-01 11.2). */
static void rd_pos(cint_state_rd *r, cint_position *p)
{
    uint64_t n = cint_state_rd_u(r, 4u);
    const uint8_t *s = n >= 1u && n <= CINT_FAULT_MAX_PATH ? cint_state_rd_take(r, n) : NULL;
    memset(p, 0, sizeof *p);
    if (s == NULL || !cint_state_utf8_ok(s, (size_t)n)) {
        r->bad = true;
        return;
    }
    p->path = (const char *)s;
    p->path_len = (uint32_t)n;
    p->line = (uint32_t)cint_state_rd_u(r, 4u);
    p->column = (uint32_t)cint_state_rd_u(r, 4u);
    if (p->line == 0u || p->column == 0u) {
        r->bad = true;
    }
}

/* True when v is a Z longer than `zmax` bytes. */
static bool z_over(const cint_tvalue *v, uint32_t zmax)
{
    return v->bytes[0] == CINT_TAG_Z && v->len > 5u + zmax;
}

void cint_state_rd_fault(cint_state_rd *r, cint_state_fault_view *f)
{
    uint64_t n;
    const uint8_t *domain;
    uint32_t zmax;
    memset(f, 0, sizeof *f);
    domain = cint_state_rd_u(r, 4u) == sizeof FAULT_V2 - 1u ? cint_state_rd_take(r, sizeof FAULT_V2 - 1u) : NULL;
    f->version = domain == NULL                                          ? 0u
                 : memcmp(domain, FAULT_V2, sizeof FAULT_V2 - 1u) == 0 ? 2u
                 : memcmp(domain, FAULT_V3, sizeof FAULT_V3 - 1u) == 0 ? 3u
                                                                       : 0u;
    if (f->version == 0u) {
        r->bad = true;
        return;
    }
    zmax = f->version == 3u ? CINT_Z_MAX_BYTES : CINT_Z_V2_MAX_BYTES;
    f->code = (uint16_t)cint_state_rd_u(r, 2u);
    n = cint_state_rd_u(r, 4u);
    f->operation = n >= 1u && n <= CINT_FAULT_MAX_OPERATION ? cint_state_rd_take(r, n) : NULL;
    if (f->code == 0u || f->code > CINT_E_ASSERT || f->operation == NULL) {
        r->bad = true;
        return;
    }
    f->operation_len = (uint8_t)n;
    for (uint32_t i = 0u; i < f->operation_len; i++) {
        uint8_t c = f->operation[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_')) {
            r->bad = true;
        }
    }
    /* A run-time record is of version 3 exactly when its operation is dot.checked.i1024.<R>. */
    if (cint_fault_version((const char *)f->operation, f->operation_len) != f->version) {
        r->bad = true;
    }
    n = cint_state_rd_u(r, 4u);
    if (n > CINT_FAULT_MAX_OPERANDS) {
        r->bad = true;
        return;
    }
    f->operand_count = (uint8_t)n;
    for (uint32_t i = 0u; i < f->operand_count; i++) {
        cint_state_rd_value(r, &f->operands[i], false);
        r->bad = r->bad || z_over(&f->operands[i], zmax);
    }
    f->has_exact = cint_state_rd_flag(r);
    if (f->has_exact) {
        cint_state_rd_value(r, &f->exact, false);
        r->bad = r->bad || f->exact.bytes[0] != CINT_TAG_Z || z_over(&f->exact, zmax);
    }
    f->has_limit = cint_state_rd_flag(r);
    if (f->has_limit) {
        cint_state_rd_value(r, &f->limit, false);
        r->bad = r->bad || z_over(&f->limit, zmax);
    }
    rd_pos(r, &f->position);
    f->has_revision = cint_state_rd_flag(r);
    f->revision = f->has_revision ? cint_state_rd_take(r, 32u) : NULL;
    f->has_map = cint_state_rd_flag(r);
    f->map = f->has_map ? cint_state_rd_take(r, 32u) : NULL;
    f->has_address = cint_state_rd_flag(r);
    if (f->has_address) {
        n = cint_state_rd_u(r, 4u);
        f->kernel = n <= KERNEL_MAX ? cint_state_rd_take(r, n) : NULL;
        f->kernel_len = (uint32_t)n;
        r->bad = r->bad || f->kernel == NULL || !cint_state_utf8_ok(f->kernel, (size_t)n);
        f->dispatch = cint_state_as_i64(cint_state_rd_u(r, 8u));
        f->phase = (uint8_t)cint_state_rd_u(r, 1u);
        r->bad = r->bad || f->phase > 2u;
        f->has_work_item = cint_state_rd_flag(r);
        if (f->has_work_item) {
            r->bad = r->bad || f->phase != 1u;  /* a work item only in the work-item phase */
            f->work_item = cint_state_as_i64(cint_state_rd_u(r, 8u));
            f->step = cint_state_as_i64(cint_state_rd_u(r, 8u));
        }
    }
    n = cint_state_rd_u(r, 4u);
    if (n > CINT_RT_MAX_DEPTH) {
        r->bad = true;
        return;
    }
    f->stack_count = (uint32_t)n;
    f->stack_at = r->at;
    for (uint32_t i = 0u; i < f->stack_count && !r->bad; i++) {
        cint_position p;
        rd_pos(r, &p);
    }
}

/* One registry entry, checked as H-19a requires of it alone. */
static void rd_entry(cint_state_rd *r, cint_checkpoint_buffer *b)
{
    uint64_t state;
    uint16_t code;
    memset(b, 0, sizeof *b);
    b->id = cint_state_rd_u(r, 8u);
    b->generation = cint_state_rd_u(r, 8u);
    b->owner = (uint32_t)cint_state_rd_u(r, 1u);
    b->type.code = code = (uint16_t)cint_state_rd_u(r, 2u);
    b->type.storage = (uint8_t)cint_state_rd_u(r, 1u);
    b->type.frac_bits = (uint16_t)cint_state_rd_u(r, 2u);
    b->type.record_id = (uint32_t)cint_state_rd_u(r, 4u);
    b->extent = cint_state_as_i64(cint_state_rd_u(r, 8u));
    b->ceiling = (uint8_t)cint_state_rd_u(r, 1u);
    b->publish = (uint8_t)cint_state_rd_u(r, 1u);
    state = cint_state_rd_u(r, 1u);
    if (b->id == 0u || b->generation == 0u || b->generation > UINT64_MAX - 1u ||
        (b->owner != CINT_OWNER_BORROWED && b->owner != CINT_OWNER_BRIDGE) || b->type.storage != 0u ||
        b->type.frac_bits != 0u || (code != CINT_TAG_RECORD && b->type.record_id != 0u) ||
        (code != 0u && code != CINT_TAG_RECORD && cint_state_width_of(code) == 0u) ||
        (code == 0u && b->owner != CINT_OWNER_BORROWED) || b->extent < 0 || b->ceiling > CINT_VIEW_WRITE ||
        b->publish > CINT_PUBLISH_COPY || (b->publish != CINT_PUBLISH_NONE && b->ceiling != CINT_VIEW_WRITE) ||
        state != 0u) {
        r->bad = true;
    }
}

/* One arena row (41 bytes), checked alone. */
static void rd_arena(cint_state_rd *r, cint_checkpoint_arena *a)
{
    memset(a, 0, sizeof *a);
    a->id = (uint32_t)cint_state_rd_u(r, 4u);
    a->parent = (uint32_t)cint_state_rd_u(r, 4u);
    a->retired = (uint32_t)cint_state_rd_u(r, 1u);
    a->offset = cint_state_as_i64(cint_state_rd_u(r, 8u));
    a->capacity = cint_state_as_i64(cint_state_rd_u(r, 8u));
    a->used = cint_state_as_i64(cint_state_rd_u(r, 8u));
    a->generation = cint_state_rd_u(r, 8u);
    r->bad = r->bad || a->id == 0u || a->retired > 1u || a->capacity < 0 || a->used < 0 ||
             a->used > a->capacity || a->generation > GEN_MAX;
}

/* Row k of the arena rows at `rows`, which rd_arenas has read. */
static void arena_at(const uint8_t *state, size_t rows, uint64_t k, cint_checkpoint_arena *a)
{
    cint_state_rd x = cint_state_rd_of(state + rows + (size_t)k * ARENA_ROW, ARENA_ROW);
    rd_arena(&x, a);
}

/* The arena block at r, whose rows begin at *rows (H-19a): identifiers ascending, the
 * module-level arenas 1 to n first, each with its element type; each child within its parent's
 * capacity, and within its used extent unless retired; a child of a retired arena retired. The
 * highest identifier into *last; nothing when the block is empty. */
static void rd_arenas(cint_state_rd *r, size_t *rows, uint64_t *count, uint64_t *last)
{
    uint64_t len = cint_state_rd_u(r, 8u), mods = 0u;
    size_t end = len <= (uint64_t)(r->n - r->at) ? r->at + (size_t)len : 0u;
    *count = *last = 0u;
    if (len == 0u || r->bad) {
        r->bad = r->bad || (len > 0u && end == 0u);
        return;
    }
    cint_state_rd_lit(r, CINT_STATE_LIT(ARENA_DOMAIN));
    *count = cint_state_rd_u(r, 8u);
    if (r->bad || r->at > end || *count == 0u || *count > (end - r->at) / ARENA_ROW) {
        r->bad = true;
        return;
    }
    *rows = r->at;
    for (uint64_t i = 0u; i < *count && !r->bad; i++) {
        cint_checkpoint_arena a, q;
        uint64_t lo = 0u, hi = i;
        memset(&q, 0, sizeof q);
        rd_arena(r, &a);
        if (r->bad || a.id <= *last ||
            (a.parent == 0u && (a.id != i + 1u || mods != i || a.retired != 0u || a.offset != 0))) {
            r->bad = true;
            break;
        }
        *last = a.id;
        if (a.parent == 0u) {
            mods++;
            continue;
        }
        while (lo < hi) {  /* the parent, by identifier, among the rows before */
            uint64_t mid = lo + (hi - lo) / 2u;
            arena_at(r->p, *rows, mid, &q);
            if (q.id < a.parent) {
                lo = mid + 1u;
            } else {
                hi = mid;
            }
        }
        if (lo < i) {
            arena_at(r->p, *rows, lo, &q);
        }
        r->bad = lo == i || q.id != a.parent || a.offset < 0 || a.offset > q.capacity ||
                 a.capacity > q.capacity - a.offset || (q.retired != 0u && a.retired == 0u) ||
                 (a.retired == 0u && a.offset + a.capacity > q.used);
    }
    for (uint64_t i = 0u; i < mods && !r->bad; i++) {
        cint_checkpoint_arena a;
        uint32_t width = 0u;
        arena_at(r->p, *rows, i, &a);
        rd_elem_type(r, &width);
        if (r->bad || r->at > end || (width > 0u && (uint64_t)a.used > (uint64_t)(end - r->at) / width)) {
            r->bad = true;
        } else {
            r->at += (size_t)a.used * width;
        }
    }
    r->bad = r->bad || r->at != end;
}

/* One pool, whose identifier must be `id` (H-19a): its used slots below its capacity, each
 * generation at most 2^64 - 2, retired at 2^64 - 2, vacant only after a removal; the free list
 * walks from the top through every vacant slot once. */
static void rd_pool(cint_state_rd *r, cint_checkpoint_pool *p, uint32_t id)
{
    size_t at;
    memset(p, 0, sizeof *p);
    p->id = (uint32_t)cint_state_rd_u(r, 4u);
    at = r->at + 4u;
    rd_elem_type(r, &p->elem_bytes);
    p->type = r->bad ? NULL : r->p + at;
    p->type_len = r->bad ? 0u : (uint32_t)(r->at - at);
    p->capacity = cint_state_as_i64(cint_state_rd_u(r, 8u));
    p->used = cint_state_rd_u(r, 8u);
    p->top = cint_state_rd_u(r, 8u);
    if (r->bad || p->id != id || p->capacity < 0 || p->used > (uint64_t)p->capacity ||
        p->used > (uint64_t)(r->n - r->at) / SLOT_BYTES) {
        r->bad = true;
        return;
    }
    p->slots = r->p + r->at;
    for (uint64_t i = 0u; i < p->used && !r->bad; i++) {
        uint64_t gen = cint_state_rd_u(r, 8u), mark = cint_state_rd_u(r, 1u), below = cint_state_rd_u(r, 8u);
        r->bad = mark > CINT_SLOT_RETIRED || gen > GEN_MAX || (mark == CINT_SLOT_RETIRED && gen != GEN_MAX) ||
                 (mark == CINT_SLOT_VACANT && gen == 0u) || (mark != CINT_SLOT_VACANT && below != 0u) ||
                 below > p->used;
        p->occupied += mark == CINT_SLOT_OCCUPIED ? 1u : 0u;
        p->retired += mark == CINT_SLOT_RETIRED ? 1u : 0u;
    }
    p->vacant = p->used - p->occupied - p->retired;
    for (uint64_t cur = p->top, k = 0u; !r->bad; k++) {
        const uint8_t *rec = cur != 0u && cur <= p->used ? p->slots + (size_t)(cur - 1u) * SLOT_BYTES : NULL;
        if (cur == 0u) {
            r->bad = k != p->vacant;
            break;
        }
        r->bad = k == p->vacant || rec == NULL || rec[8] != CINT_SLOT_VACANT;
        cur = rec != NULL ? le64(rec + 9) : 0u;
    }
    if (!r->bad && p->elem_bytes > 0u && p->occupied > (uint64_t)(r->n - r->at) / p->elem_bytes) {
        r->bad = true;
    }
    p->elements = r->bad ? NULL : cint_state_rd_take(r, p->occupied * p->elem_bytes);
}

/* The pool block at r: pools 1 to n. The highest identifier into *last. */
static void rd_pools(cint_state_rd *r, size_t *at, uint64_t *last)
{
    uint64_t len = cint_state_rd_u(r, 8u), count;
    size_t end = len <= (uint64_t)(r->n - r->at) ? r->at + (size_t)len : 0u;
    *last = 0u;
    if (len == 0u || r->bad) {
        r->bad = r->bad || (len > 0u && end == 0u);
        return;
    }
    cint_state_rd_lit(r, CINT_STATE_LIT(POOL_DOMAIN));
    count = cint_state_rd_u(r, 8u);
    *at = r->at;
    if (r->bad || count == 0u || count >= ID_END) {
        r->bad = true;
        return;
    }
    for (uint64_t i = 0u; i < count && !r->bad && r->at <= end; i++) {
        cint_checkpoint_pool p;
        rd_pool(r, &p, (uint32_t)(i + 1u));
    }
    *last = count;
    r->bad = r->bad || r->at != end;
}

/* The canonical state bytes of c->state, checked as H-19a requires of everything that does
 * not need the program, with the offsets of c filled. */
static bool rd_state(cint_checkpoint_info *c)
{
    cint_state_rd r = cint_state_rd_of(c->state, c->state_len);
    const uint8_t *p, *prev = NULL, *elements = NULL;
    uint64_t n, prev_len = 0u, last_id = 0u, last_arena, last_pool, count = 0u;
    size_t block = 0u;
    cint_state_rd_lit(&r, CINT_STATE_LIT(STATE_DOMAIN));
    if ((p = cint_state_rd_take(&r, 32u)) != NULL) {
        memcpy(c->revision, p, 32u);
    }
    cint_state_rd_lit(&r, CINT_STATE_LIT(CINT_STATE_PROFILE));
    c->global_count = cint_state_rd_u(&r, 8u);
    c->globals_at = r.at;
    for (uint64_t i = 0u; i < c->global_count && !r.bad; i++) {
        n = cint_state_rd_u(&r, 4u);
        p = n >= 1u ? cint_state_rd_take(&r, n) : NULL;
        if (p == NULL || !cint_state_utf8_ok(p, (size_t)n) ||
            (prev != NULL && cint_state_name_cmp(prev, (size_t)prev_len, p, (size_t)n) >= 0)) {
            r.bad = true;
        }
        prev = p, prev_len = n;
        rd_global(&r, NULL, &elements, &count);
    }
    c->globals_end = r.at;
    rd_arenas(&r, &block, &n, &last_arena);
    rd_pools(&r, &block, &last_pool);
    n = cint_state_rd_u(&r, 8u);
    c->buffers_at = c->buffers_end = r.at;
    if (n > 0u && !r.bad) {
        size_t end = n <= (uint64_t)(r.n - r.at) ? r.at + (size_t)n : 0u;
        cint_state_rd_lit(&r, CINT_STATE_LIT(REGISTRY_DOMAIN));
        c->buffer_count = cint_state_rd_u(&r, 8u);
        if (r.bad || r.at > end || c->buffer_count == 0u || (end - r.at) % ENTRY_BYTES != 0u ||
            (end - r.at) / ENTRY_BYTES != c->buffer_count) {
            r.bad = true;
        }
        c->buffers_at = r.at;
        for (uint64_t i = 0u; i < c->buffer_count && !r.bad; i++) {
            cint_checkpoint_buffer b;
            rd_entry(&r, &b);
            r.bad = r.bad || b.id <= last_id;
            last_id = b.id;
        }
        c->buffers_end = r.at;
    }
    c->next_buffer = cint_state_rd_u(&r, 8u);
    c->next_arena = cint_state_rd_u(&r, 8u);
    c->next_pool = cint_state_rd_u(&r, 8u);
    c->entries = cint_state_rd_u(&r, 8u);
    if (c->next_buffer <= last_id || c->next_buffer == 0u || c->next_arena <= last_arena || c->next_arena > ID_END ||
        c->next_pool <= last_pool || c->next_pool > ID_END) {
        r.bad = true;  /* identifier counters above every identifier present */
    }
    cint_state_rd_lit(&r, CINT_STATE_LIT(CINT_STATE_FUEL_MODEL));
    if (cint_state_rd_flag(&r) && !r.bad) {
        cint_state_fault_view f;
        size_t at = r.at;
        cint_state_rd_fault(&r, &f);
        c->fault = c->state + at;
        c->fault_len = r.at - at;
    }
    c->effects = cint_state_as_i64(cint_state_rd_u(&r, 8u));
    return !r.bad && c->effects >= 0 && r.at == r.n;
}

cint_status cint_checkpoint_decode(const uint8_t *bytes, size_t len, cint_checkpoint_info *out)
{
    cint_checkpoint_info c;
    cint_state_rd r = cint_state_rd_of(bytes, len);
    uint64_t n;
    if (bytes == NULL || out == NULL) {
        return CINT_REFUSED;
    }
    memset(&c, 0, sizeof c);
    cint_state_rd_lit(&r, CINT_STATE_LIT(CKPT_DOMAIN));
    n = cint_state_rd_u(&r, 4u);
    c.contract.bytes = (const char *)(n >= 1u && n <= CINT_STATE_CONTRACT_MAX ? cint_state_rd_take(&r, n) : NULL);
    c.contract.len = (uint32_t)n;
    for (uint32_t i = 0u; c.contract.bytes != NULL && i < c.contract.len; i++) {
        r.bad = r.bad || c.contract.bytes[i] < 0x21 || c.contract.bytes[i] > 0x7e;
    }
    r.bad = r.bad || c.contract.bytes == NULL;
    c.payload_digest = cint_state_rd_flag(&r) ? cint_state_rd_take(&r, 32u) : NULL;
    c.build_record_digest = cint_state_rd_flag(&r) ? cint_state_rd_take(&r, 32u) : NULL;
    c.build_identity = cint_state_rd_flag(&r) ? cint_state_rd_take(&r, 32u) : NULL;
    n = cint_state_rd_u(&r, 8u);
    if (r.bad || n != (uint64_t)(r.n - r.at)) {
        return CINT_REFUSED;
    }
    c.state = bytes + r.at;
    c.state_len = (size_t)n;
    if (!rd_state(&c)) {
        return CINT_REFUSED;
    }
    *out = c;
    return CINT_OK;
}

cint_status cint_checkpoint_global_next(const cint_checkpoint_info *c, size_t *cursor, cint_checkpoint_global *out)
{
    cint_checkpoint_global g;
    cint_state_rd r;
    uint64_t n;
    if (c == NULL || cursor == NULL || out == NULL || c->state == NULL || c->globals_end > c->state_len) {
        return CINT_REFUSED;
    }
    r = cint_state_rd_of(c->state, c->globals_end);
    r.at = *cursor == 0u ? c->globals_at : *cursor;
    if (r.at < c->globals_at || r.at >= c->globals_end) {
        return CINT_REFUSED;
    }
    n = cint_state_rd_u(&r, 4u);
    g.name.bytes = (const char *)(n >= 1u ? cint_state_rd_take(&r, n) : NULL);
    g.name.len = (uint32_t)n;
    g.name.reserved = 0u;
    rd_global(&r, &g.value, &g.elements, &g.element_count);
    if (r.bad || g.name.bytes == NULL) {
        return CINT_REFUSED;
    }
    *cursor = r.at;
    *out = g;
    return CINT_OK;
}

cint_status cint_checkpoint_buffer_next(const cint_checkpoint_info *c, size_t *cursor, cint_checkpoint_buffer *out)
{
    cint_checkpoint_buffer b;
    cint_state_rd r;
    if (c == NULL || cursor == NULL || out == NULL || c->state == NULL || c->buffers_end > c->state_len) {
        return CINT_REFUSED;
    }
    r = cint_state_rd_of(c->state, c->buffers_end);
    r.at = *cursor == 0u ? c->buffers_at : *cursor;
    if (r.at < c->buffers_at || r.at >= c->buffers_end) {
        return CINT_REFUSED;
    }
    rd_entry(&r, &b);
    if (r.bad) {
        return CINT_REFUSED;
    }
    *cursor = r.at;
    *out = b;
    return CINT_OK;
}

/* Where the arena rows of c lie, their count, and where the elements of its module-level
 * arenas begin; false when the block is empty. */
static bool arena_rows(const cint_checkpoint_info *c, size_t *rows, uint64_t *count, size_t *elements)
{
    cint_state_rd r;
    if (c == NULL || c->state == NULL || c->globals_end > c->state_len) {
        return false;
    }
    r = cint_state_rd_of(c->state, c->state_len);
    r.at = c->globals_end;
    if (cint_state_rd_u(&r, 8u) == 0u) {
        return false;
    }
    cint_state_rd_lit(&r, CINT_STATE_LIT(ARENA_DOMAIN));
    *count = cint_state_rd_u(&r, 8u);
    *rows = r.at;
    if (r.bad || *count == 0u || *count > (uint64_t)(r.n - r.at) / ARENA_ROW) {
        return false;
    }
    *elements = r.at + (size_t)*count * ARENA_ROW;
    return true;
}

cint_status cint_checkpoint_arena_next(const cint_checkpoint_info *c, size_t *cursor, cint_checkpoint_arena *out)
{
    cint_checkpoint_arena a, b;
    size_t rows = 0u, at = 0u;
    uint64_t count = 0u, k;
    if (cursor == NULL || out == NULL || !arena_rows(c, &rows, &count, &at) ||
        (*cursor != 0u && (*cursor < rows || (*cursor - rows) % ARENA_ROW != 0u))) {
        return CINT_REFUSED;
    }
    k = *cursor == 0u ? 0u : (*cursor - rows) / ARENA_ROW;
    if (k >= count) {
        return CINT_REFUSED;
    }
    arena_at(c->state, rows, k, &a);
    for (uint64_t i = 0u; a.parent == 0u && i <= k; i++) {  /* each module-level arena's type and elements */
        cint_state_rd r = cint_state_rd_of(c->state, c->state_len);
        r.at = at;
        arena_at(c->state, rows, i, &b);
        rd_elem_type(&r, &a.elem_bytes);
        if (r.bad || (a.elem_bytes > 0u && (uint64_t)b.used > (uint64_t)(r.n - r.at) / a.elem_bytes)) {
            return CINT_REFUSED;
        }
        a.type = c->state + at + 4u;
        a.type_len = (uint32_t)(r.at - at - 4u);
        a.elements = c->state + r.at;
        at = r.at + (size_t)b.used * a.elem_bytes;
    }
    *cursor = rows + (size_t)(k + 1u) * ARENA_ROW;
    *out = a;
    return CINT_OK;
}

cint_status cint_checkpoint_pool_next(const cint_checkpoint_info *c, size_t *cursor, cint_checkpoint_pool *out)
{
    cint_checkpoint_pool p;
    cint_state_rd r, x;
    uint64_t len;
    size_t first;
    if (c == NULL || cursor == NULL || out == NULL || c->state == NULL || c->globals_end > c->state_len) {
        return CINT_REFUSED;
    }
    r = cint_state_rd_of(c->state, c->state_len);
    r.at = c->globals_end;
    (void)cint_state_rd_take(&r, cint_state_rd_u(&r, 8u));  /* the arena block */
    len = cint_state_rd_u(&r, 8u);
    if (r.bad || len == 0u || len > (uint64_t)(r.n - r.at)) {
        return CINT_REFUSED;
    }
    r.n = r.at + (size_t)len;
    cint_state_rd_lit(&r, CINT_STATE_LIT(POOL_DOMAIN));
    (void)cint_state_rd_u(&r, 8u);
    first = r.at;
    r.at = *cursor == 0u ? first : *cursor;
    if (r.bad || r.at < first || r.at >= r.n) {
        return CINT_REFUSED;
    }
    x = r;
    rd_pool(&r, &p, (uint32_t)cint_state_rd_u(&x, 4u));
    if (r.bad) {
        return CINT_REFUSED;
    }
    *cursor = r.at;
    *out = p;
    return CINT_OK;
}

/* ------------------------------------------------------------------------- */
/* The globals of a state table.                                              */

typedef struct gref {
    const uint8_t *name;  /* the qualified name, in the list's name bytes */
    uint32_t len, tag, width, offset;  /* tag and width: an array's element's; a struct's payload bytes */
    uint32_t rank;        /* 0 for a scalar */
    const int64_t *extents;
    uint64_t count;       /* elements: 1 for a scalar */
    const cint_state *state;
    const cint_state_type *type;  /* a struct's (tag 71, OQ-260), and its plan; NULL otherwise */
    const ty_plan *plan;
} gref;

typedef struct glist {
    gref *g;
    uint8_t *names;
    size_t count, name_bytes;
    ty_plan *plans;       /* one per struct, zero until made */
    size_t nplan;
} glist;

static int gref_cmp(const void *a, const void *b)
{
    const gref *x = (const gref *)a, *y = (const gref *)b;
    return cint_state_name_cmp(x->name, x->len, y->name, y->len);
}

static void glist_free(cint_ctx *ctx, glist *l)
{
    for (size_t k = 0u; l->plans != NULL && k < l->nplan; k++) {
        ty_free(ctx, &l->plans[k]);
    }
    cint_state_mem_free(ctx, l->plans, l->nplan * sizeof *l->plans, 8u);
    cint_state_mem_free(ctx, l->g, l->count * sizeof(gref), _Alignof(gref));
    cint_state_mem_free(ctx, l->names, l->name_bytes, 1u);
    memset(l, 0, sizeof *l);
}

/* The plan of struct variable v of a block of `bytes` bytes into *p (OQ-260): a struct type of array fields of
 * rank 1 to 4 and no handle, stored inside the block. CINT_REFUSED otherwise, CINT_RESOURCE when memory runs
 * out. */
static cint_status struct_plan(cint_ctx *ctx, const cint_state_var *v, uint32_t bytes, ty_plan *p)
{
    const cint_state_type *t = v->type;
    cint_status st;
    if (t->type == NULL || (t->layout == NULL && t->layout_len > 0u) || t->reserved != 0u ||
        (t->cell != 0u && t->cell != 8u)) {
        return CINT_REFUSED;
    }
    if ((st = ty_plan_from(ctx, t->type, t->type_len, t->layout, t->layout_len, t->cell, 4u, p)) != CINT_OK) {
        return st;
    }
    for (uint32_t k = 0u; k < p->npart; k++) {
        st = p->parts[k].tag == CINT_TAG_HANDLE ? CINT_REFUSED : st;
    }
    if (st != CINT_OK || p->elem.tag != TY_STRUCT || p->elem.stride > bytes - v->offset) {
        ty_free(ctx, p);
        return CINT_REFUSED;
    }
    return CINT_OK;
}

/* Every variable of table t over program p, sorted by qualified name. CINT_REFUSED for a
 * table that does not fit p (cint_ctx_bind_state lists the cases). */
static cint_status globals(cint_ctx *ctx, const cint_program *p, const cint_state_table *t, glist *out)
{
    size_t count = 0u, bytes = 0u, at = 0u, k = 0u, structs = 0u, built = 0u;
    cint_status st = CINT_OK;
    memset(out, 0, sizeof *out);
    if (t->reserved != 0u || (t->count > 0u && t->states == NULL)) {
        return CINT_REFUSED;
    }
    for (uint32_t i = 0u; i < t->count; i++) {
        const cint_state *s = t->states[i];
        const cint_module *m;
        if (s == NULL || s->module >= p->module_count || s->bytes == 0u || s->init == NULL || s->reserved != 0u ||
            (s->var_count > 0u && s->vars == NULL)) {
            return CINT_REFUSED;
        }
        m = p->modules[s->module];
        if (m->path_len < 4u || memcmp(m->path + m->path_len - 3u, ".ci", 3u) != 0) {
            return CINT_REFUSED;
        }
        for (uint32_t j = 0u; j < i; j++) {
            if (t->states[j]->module == s->module) {
                return CINT_REFUSED;
            }
        }
        for (uint32_t j = 0u; j < s->var_count; j++) {
            const cint_state_var *v = &s->vars[j];
            bool record = v->tag == CINT_TAG_RECORD;
            uint32_t w = record ? 1u : global_width(v->tag);
            uint64_t n = 1u;
            bool ok = v->name != NULL && v->name_len != 0u && w != 0u && v->rank <= 4u &&
                      ((v->tag != CINT_TAG_HANDLE && !record) || v->rank == 0u) && record == (v->type != NULL) &&
                      (v->rank == 0u) == (v->extents == NULL) && v->offset <= s->bytes;
            for (uint32_t d = 0u; ok && d < v->rank; d++) {
                int64_t e = v->extents[d];
                ok = e >= 0 && (e == 0 || n <= UINT64_MAX / (uint64_t)e);
                n *= ok ? (uint64_t)e : 0u;
            }
            if (!ok || n > (s->bytes - v->offset) / w || !cint_state_utf8_ok((const uint8_t *)v->name, v->name_len)) {
                return CINT_REFUSED;
            }
            bytes += (size_t)m->path_len - 2u + v->name_len;
            structs += record ? 1u : 0u;
        }
        count += s->var_count;
    }
    if (count == 0u) {
        return CINT_OK;
    }
    out->g = (gref *)cint_state_mem_alloc(ctx, count * sizeof(gref), _Alignof(gref));
    out->names = (uint8_t *)cint_state_mem_alloc(ctx, bytes, 1u);
    out->plans = (ty_plan *)cint_state_mem_alloc(ctx, structs * sizeof *out->plans, 8u);
    out->count = count;
    out->name_bytes = bytes;
    out->nplan = out->plans != NULL ? structs : 0u;
    if (out->plans != NULL) {
        memset(out->plans, 0, structs * sizeof *out->plans);
    }
    if (out->g == NULL || out->names == NULL || (structs > 0u && out->plans == NULL)) {
        glist_free(ctx, out);
        return CINT_RESOURCE;
    }
    for (uint32_t i = 0u; i < t->count; i++) {
        const cint_state *s = t->states[i];
        const cint_module *m = p->modules[s->module];
        for (uint32_t j = 0u; j < s->var_count; j++, k++) {
            const cint_state_var *v = &s->vars[j];
            gref *g = &out->g[k];
            g->name = out->names + at;
            g->len = m->path_len - 2u + v->name_len;
            for (uint32_t q = 0u; q + 3u < m->path_len; q++) {
                out->names[at++] = (uint8_t)(m->path[q] == '/' ? '.' : m->path[q]);
            }
            out->names[at++] = '.';
            memcpy(out->names + at, v->name, v->name_len);
            at += v->name_len;
            g->tag = v->tag;
            g->width = global_width(v->tag);
            g->offset = v->offset;
            g->rank = v->rank;
            g->extents = v->extents;
            g->count = 1u;
            for (uint32_t d = 0u; d < v->rank; d++) {
                g->count *= (uint64_t)v->extents[d];
            }
            g->state = s;
            g->type = v->type;
            g->plan = NULL;
            if (v->type != NULL && st == CINT_OK) {
                ty_plan *pl = &out->plans[built++];
                if ((st = struct_plan(ctx, v, s->bytes, pl)) == CINT_OK) {
                    g->plan = pl;
                    g->width = (uint32_t)pl->width;
                }
            }
        }
    }
    if (st != CINT_OK) {
        glist_free(ctx, out);
        return st;
    }
    qsort(out->g, count, sizeof(gref), gref_cmp);
    for (k = 1u; k < count; k++) {
        if (gref_cmp(&out->g[k - 1u], &out->g[k]) == 0) {
            glist_free(ctx, out);
            return CINT_REFUSED;
        }
    }
    return CINT_OK;
}

/* The bytes of a module's state: its block once the module has been accessed, before
 * that its initial image. */
static const uint8_t *block_of(const cint_ctx *ctx, const cint_state *s)
{
    return ctx->head.state != NULL && ctx->head.state[s->module] != NULL ? (const uint8_t *)ctx->head.state[s->module]
                                                                : (const uint8_t *)s->init;
}

/* The type descriptor of global g (IM-146): its tag, or tag 51, the element's tag, the rank
 * and each extent for an array. Returns its length, at most 35 bytes. */
static size_t desc_of(const gref *g, uint8_t *out)
{
    cint_state_wr w = cint_state_wr_of(out, 35u);
    if (g->rank != 0u) {
        cint_state_w_le(&w, TAG_ARRAY, 1u);
    }
    cint_state_w_le(&w, g->tag, 1u);
    if (g->rank != 0u) {
        cint_state_w_le(&w, g->rank, 1u);
    }
    for (uint32_t d = 0u; d < g->rank; d++) {
        cint_state_w_le(&w, (uint64_t)g->extents[d], 8u);
    }
    return w.len;
}

/* Whether the checkpoint's global cg has the name and type of g; a struct's type bytes in full (OQ-260). */
static bool same_global(const cint_checkpoint_global *cg, const gref *g)
{
    uint8_t d[35];
    size_t n = g->type != NULL ? 1u : desc_of(g, d);
    if (cg->name.len != g->len || memcmp(cg->name.bytes, g->name, g->len) != 0) {
        return false;
    }
    if (g->type != NULL) {
        return cg->value.len == 1u && cg->value.bytes[0] == TY_STRUCT &&
               cg->element_count == (uint64_t)g->type->type_len + g->width &&
               memcmp(cg->elements, g->type->type, g->type->type_len) == 0;
    }
    return cg->value.len >= n && memcmp(cg->value.bytes, d, n) == 0 && (g->rank == 0u || cg->value.len == n);
}

/* Whether globals x and y have one type: a struct's type bytes in full, or one descriptor. */
static bool same_type(const gref *x, const gref *y)
{
    uint8_t dx[35], dy[35];
    size_t nx;
    if (x->type != NULL || y->type != NULL) {
        return x->type != NULL && y->type != NULL && x->type->type_len == y->type->type_len &&
               memcmp(x->type->type, y->type->type, x->type->type_len) == 0;
    }
    nx = desc_of(x, dx);
    return nx == desc_of(y, dy) && memcmp(dx, dy, nx) == 0;
}

/* True when t lists the cint_state of every block the context holds. */
static bool covered(const cint_ctx *ctx, const cint_state_table *t)
{
    uint32_t n = ctx->program->module_count;
    for (uint32_t m = 0u; ctx->head.state != NULL && m < n; m++) {
        bool found = ctx->head.state[m] == NULL;
        for (uint32_t i = 0u; !found && i < t->count; i++) {
            found = t->states[i] == (const cint_state *)ctx->head.state[n + m];
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

static void blocks_free(cint_ctx *ctx, void **state, uint32_t n)
{
    for (uint32_t m = 0u; state != NULL && m < n; m++) {
        if (state[m] != NULL) {
            cint_state_mem_free(ctx, state[m], ((const cint_state *)state[n + m])->bytes, 8u);
        }
    }
    cint_state_mem_free(ctx, (void *)state, (size_t)n * 2u * sizeof(void *), _Alignof(void *));
}

/* Gives every module of t its block, as its first access would (cint_rt_state). A block
 * made here holds the module's initial image, which is the state the module already had. */
static cint_status blocks_make(cint_ctx *ctx, void ***state, uint32_t n, const cint_state_table *t)
{
    if (t->count > 0u && *state == NULL) {
        *state = (void **)cint_state_mem_alloc(ctx, (size_t)n * 2u * sizeof(void *), _Alignof(void *));
        if (*state == NULL) {
            return CINT_RESOURCE;
        }
        for (size_t k = 0u; k < (size_t)n * 2u; k++) {
            (*state)[k] = NULL;
        }
    }
    for (uint32_t i = 0u; i < t->count; i++) {
        const cint_state *s = t->states[i];
        if ((*state)[s->module] == NULL) {
            void *b = cint_state_mem_alloc(ctx, s->bytes, 8u);
            if (b == NULL) {
                return CINT_RESOURCE;
            }
            memcpy(b, s->init, s->bytes);
            (*state)[s->module] = b;
            (*state)[n + s->module] = (void *)(uintptr_t)s;
        }
    }
    return CINT_OK;
}

cint_status cint_ctx_bind_state(cint_ctx *ctx, const cint_state_table *t)
{
    glist g;
    cint_status st;
    if (ctx == NULL) {
        return CINT_REFUSED;
    }
    if (!cint_rt_busy_take(ctx)) {
        return CINT_BUSY;
    }
    t = t != NULL ? t : &NO_STATE;
    st = globals(ctx, ctx->program, t, &g);
    if (st == CINT_OK && !covered(ctx, t)) {
        st = CINT_REFUSED;
    }
    if (st == CINT_OK) {
        ctx->states = t;
    }
    glist_free(ctx, &g);
    cint_rt_busy_give(ctx);
    return st;
}

/* ------------------------------------------------------------------------- */
/* The canonical state, the state hash, and the checkpoint.                   */

typedef struct snap {
    glist g;
    const buffer_slot **live;  /* the live registrations, by identifier */
    size_t live_count;
    uint64_t *below;           /* per used pool slot, the slot below it on its free list plus 1 */
    size_t below_count;
    ty_plan *plans;            /* per declaration of the table the elements are laid out by */
    uint32_t plan_count;
    uint8_t *bytes;
    size_t len;
} snap;

static int slot_cmp(const void *a, const void *b)
{
    cint_buffer_id x = (*(const buffer_slot *const *)a)->id, y = (*(const buffer_slot *const *)b)->id;
    return x < y ? -1 : x > y ? 1 : 0;
}

static void snap_free(cint_ctx *ctx, snap *s)
{
    glist_free(ctx, &s->g);
    cint_state_mem_free(ctx, (void *)s->live, s->live_count * sizeof *s->live, _Alignof(const buffer_slot *));
    cint_state_mem_free(ctx, s->below, s->below_count * sizeof *s->below, 8u);
    for (uint32_t k = 0u; k < s->plan_count; k++) {
        ty_free(ctx, &s->plans[k]);
    }
    cint_state_mem_free(ctx, s->plans, (size_t)s->plan_count * sizeof *s->plans, 8u);
    cint_state_mem_free(ctx, s->bytes, s->len, 8u);
    memset(s, 0, sizeof *s);
}

/* Whether arena k of m or an ancestor is retired (rt/cint_mem.c; M-20). */
static bool arena_retired(const struct mem_state *m, uint32_t k)
{
    for (const mem_arena *a = &m->arenas[k]; a->parent != 0u; a = &m->arenas[a->parent - 1u]) {
        if (m->arenas[a->parent - 1u].generation != a->parent_generation) {
            return true;
        }
    }
    return false;
}

/* A container's element type: through plan p, the plan of declaration d, or as a restore read it
 * (p NULL). */
static void put_type(const ty_plan *p, const cint_mem_decl *d, const uint8_t *held, uint32_t held_len,
                     cint_state_wr *w)
{
    if (p == NULL) {
        cint_state_w_bytes(w, held, held_len);
        return;
    }
    cint_state_w_le(w, p->width, 4u);
    cint_state_w_bytes(w, d->type, d->type_len);
}

/* n elements stored from `at`, `stride` bytes apart: their payloads through plan p, or as a restore
 * read them (p NULL). */
static void put_elements(const ty_plan *p, const uint8_t *at, uint32_t stride, int64_t n, cint_state_wr *w)
{
    if (p == NULL) {
        cint_state_w_bytes(w, at, (size_t)n * stride);
    } else if (w->buf == NULL) {
        w->len += (size_t)n * (size_t)p->width;
    } else {
        for (int64_t i = 0; i < n; i++, at += stride) {
            ty_put(p, &p->elem, at, w);
        }
    }
}

/* The declaration of kind `kind` at or after *k in m's laid-out table, and its plan; NULLs for a state a
 * restore holds. */
static const ty_plan *plan_next(const struct mem_state *m, const ty_plan *plans, uint32_t kind, uint32_t *k,
                                const cint_mem_decl **d)
{
    *d = NULL;
    if (m->laid == NULL) {
        return NULL;
    }
    while (m->laid->decls[*k].kind != kind) {
        ++*k;
    }
    *d = &m->laid->decls[*k];
    return &plans[(*k)++];
}

/* The arena block's bytes after its length: none for no arenas. */
static void put_arenas(const struct mem_state *m, const ty_plan *plans, cint_state_wr *w)
{
    uint32_t j = 0u;
    if (m == NULL || m->arena_count == 0u) {
        return;
    }
    cint_state_w_str(w, CINT_STATE_LIT(ARENA_DOMAIN));
    cint_state_w_le(w, m->arena_count, 8u);
    for (uint32_t k = 0u; k < m->arena_count; k++) {
        const mem_arena *a = &m->arenas[k];
        const mem_arena *up = a->parent != 0u ? &m->arenas[a->parent - 1u] : NULL;
        cint_state_w_le(w, a->id, 4u);
        cint_state_w_le(w, up != NULL ? up->id : 0u, 4u);
        cint_state_w_le(w, arena_retired(m, k) ? 1u : 0u, 1u);
        cint_state_w_le(w, (uint64_t)(up != NULL ? a->offset - up->offset : 0), 8u);
        cint_state_w_le(w, (uint64_t)a->capacity, 8u);
        cint_state_w_le(w, (uint64_t)a->used, 8u);
        cint_state_w_le(w, a->generation, 8u);
    }
    for (uint32_t i = 0u; i < m->arena_count; i++) {
        const mem_arena *a = &m->arenas[i];
        const cint_mem_decl *d = NULL;
        const ty_plan *p = a->parent == 0u ? plan_next(m, plans, CINT_HANDLE_ARENA, &j, &d) : NULL;
        if (a->parent == 0u) {
            put_type(p, d, a->type, a->type_len, w);
            put_elements(p, a->storage, a->elem_bytes, a->used, w);
        }
    }
}

/* The pool block's bytes after its length, with each used slot's `below` from s. */
static void put_pools(const struct mem_state *m, const ty_plan *plans, const uint64_t *below, cint_state_wr *w)
{
    uint32_t j = 0u;
    if (m == NULL || m->pool_count == 0u) {
        return;
    }
    cint_state_w_str(w, CINT_STATE_LIT(POOL_DOMAIN));
    cint_state_w_le(w, m->pool_count, 8u);
    for (uint32_t k = 0u; k < m->pool_count; k++) {
        const mem_pool *p = &m->pools[k];
        const cint_mem_decl *d = NULL;
        const ty_plan *t = plan_next(m, plans, CINT_HANDLE_POOL, &j, &d);
        cint_state_w_le(w, k + 1u, 4u);
        put_type(t, d, p->type, p->type_len, w);
        cint_state_w_le(w, (uint64_t)p->capacity, 8u);
        cint_state_w_le(w, (uint64_t)p->fresh, 8u);
        cint_state_w_le(w, p->free_count > 0 ? (uint64_t)p->free_list[p->free_count - 1] + 1u : 0u, 8u);
        for (int64_t i = 0; i < p->fresh; i++) {
            cint_state_w_le(w, p->gens[i], 8u);
            cint_state_w_le(w, (p->marks[i] & MEM_OCCUPIED) != 0u ? CINT_SLOT_OCCUPIED
                    : (p->marks[i] & MEM_RETIRED) != 0u ? CINT_SLOT_RETIRED : CINT_SLOT_VACANT, 1u);
            cint_state_w_le(w, *below++, 8u);
        }
        for (int64_t i = 0; i < p->fresh; i++) {
            if ((p->marks[i] & MEM_OCCUPIED) != 0u) {
                put_elements(t, p->storage + (size_t)i * p->elem_bytes, p->elem_bytes, 1, w);
            }
        }
    }
}

static void put_state(const cint_ctx *ctx, const snap *s, cint_state_wr *w)
{
    cint_state_wr n = cint_state_wr_of(NULL, 0u);
    cint_state_w_str(w, CINT_STATE_LIT(STATE_DOMAIN));
    cint_state_w_bytes(w, ctx->program->revision, 32u);
    cint_state_w_str(w, CINT_STATE_LIT(CINT_STATE_PROFILE));
    cint_state_w_le(w, s->g.count, 8u);
    for (size_t i = 0u; i < s->g.count; i++) {
        const gref *g = &s->g.g[i];
        uint8_t d[35];
        cint_state_w_str(w, g->name, g->len);
        if (g->plan != NULL) {  /* a struct: tag 71, its type, its payload (OQ-260) */
            cint_state_w_le(w, TY_STRUCT, 1u);
            cint_state_w_bytes(w, g->type->type, g->type->type_len);
            ty_put(g->plan, &g->plan->elem, block_of(ctx, g->state) + g->offset, w);
            continue;
        }
        cint_state_w_bytes(w, d, desc_of(g, d));
        /* little-endian hosts (IM-146) */
        cint_state_w_bytes(w, block_of(ctx, g->state) + g->offset, (size_t)(g->width * g->count));
    }
    put_arenas(ctx->mem, s->plans, &n);
    cint_state_w_le(w, n.len, 8u);
    put_arenas(ctx->mem, s->plans, w);
    n = cint_state_wr_of(NULL, 0u);
    put_pools(ctx->mem, s->plans, s->below, &n);
    cint_state_w_le(w, n.len, 8u);
    put_pools(ctx->mem, s->plans, s->below, w);
    cint_state_w_le(w, s->live_count == 0u ? 0u : 4u + sizeof REGISTRY_DOMAIN - 1u + 8u + ENTRY_BYTES * s->live_count,
                    8u);
    if (s->live_count > 0u) {
        cint_state_w_str(w, CINT_STATE_LIT(REGISTRY_DOMAIN));
        cint_state_w_le(w, s->live_count, 8u);
    }
    for (size_t i = 0u; i < s->live_count; i++) {
        const buffer_slot *b = s->live[i];
        cint_state_w_le(w, b->id, 8u);
        cint_state_w_le(w, b->generation, 8u);
        cint_state_w_le(w, b->storage == STORAGE_CREATED ? CINT_OWNER_BRIDGE : CINT_OWNER_BORROWED, 1u);
        cint_state_w_le(w, b->type.code, 2u);  /* 0 for a registration without an element type */
        cint_state_w_le(w, b->type.storage, 1u);
        cint_state_w_le(w, b->type.frac_bits, 2u);
        cint_state_w_le(w, b->type.record_id, 4u);
        cint_state_w_le(w, (uint64_t)(b->typed != 0u ? b->extent : b->bytes), 8u);
        cint_state_w_le(w, b->perm, 1u);
        cint_state_w_le(w, b->publish, 1u);
        cint_state_w_le(w, 0u, 1u);
    }
    cint_state_w_le(w, ctx->last_id + 1u, 8u);
    cint_state_w_le(w, ctx->last_arena + 1u, 8u);
    cint_state_w_le(w, ctx->last_pool + 1u, 8u);
    cint_state_w_le(w, ctx->entries, 8u);
    cint_state_w_str(w, CINT_STATE_LIT(CINT_STATE_FUEL_MODEL));
    cint_state_w_le(w, ctx->head.faulted != 0u ? 1u : 0u, 1u);
    if (ctx->head.faulted != 0u) {
        w->len += cint_fault_encode(&ctx->fault, w->buf != NULL ? w->buf + w->len : NULL,
                                    w->buf != NULL ? w->cap - w->len : 0u);
    }
    cint_state_w_le(w, (uint64_t)ctx->effects, 8u);
}

/* The context's canonical state bytes into s. CINT_REFUSED for a context with no bound
 * state, a program without a revision identity, or a fault record that does not encode. */
static cint_status state_bytes(cint_ctx *ctx, snap *s)
{
    cint_state_wr w = cint_state_wr_of(NULL, 0u);
    cint_status st;
    size_t k = 0u, base = 0u;
    memset(s, 0, sizeof *s);
    /* A counter at UINT64_MAX has no next identifier to write; 2^64 registrations do not occur. */
    if (ctx->states == NULL || ctx->program->revision == NULL || !covered(ctx, ctx->states) ||
        ctx->last_id == UINT64_MAX || ctx->last_arena == UINT64_MAX || ctx->last_pool == UINT64_MAX ||
        (ctx->head.faulted != 0u && cint_fault_encode(&ctx->fault, NULL, 0u) == 0u)) {
        return CINT_REFUSED;
    }
    if ((st = globals(ctx, ctx->program, ctx->states, &s->g)) != CINT_OK) {
        return st;
    }
    for (uint32_t i = 0u; i < ctx->buffer_used; i++) {
        if (ctx->buffers[i].live != 0u) {
            s->live_count++;
        }
    }
    s->live = (const buffer_slot **)cint_state_mem_alloc(ctx, s->live_count * sizeof *s->live,
                                                         _Alignof(const buffer_slot *));
    if (s->live_count > 0u && s->live == NULL) {
        return CINT_RESOURCE;
    }
    for (uint32_t i = 0u; i < ctx->buffer_used; i++) {
        if (ctx->buffers[i].live != 0u) {
            s->live[k++] = &ctx->buffers[i];
        }
    }
    if (s->live_count > 1u) {
        qsort((void *)s->live, s->live_count, sizeof *s->live, slot_cmp);
    }
    for (uint32_t i = 0u; ctx->mem != NULL && i < ctx->mem->pool_count; i++) {
        s->below_count += (size_t)ctx->mem->pools[i].fresh;
    }
    s->below = (uint64_t *)cint_state_mem_alloc(ctx, s->below_count * sizeof *s->below, 8u);
    if (s->below_count > 0u && s->below == NULL) {
        return CINT_RESOURCE;
    }
    if (s->below != NULL) {
        memset(s->below, 0, s->below_count * sizeof *s->below);
    }
    for (uint32_t i = 0u; ctx->mem != NULL && i < ctx->mem->pool_count; i++) {
        const mem_pool *p = &ctx->mem->pools[i];
        for (int64_t j = 1; j < p->free_count; j++) {
            s->below[base + (size_t)p->free_list[j]] = (uint64_t)p->free_list[j - 1] + 1u;
        }
        base += (size_t)p->fresh;
    }
    if (ctx->mem != NULL && ctx->mem->laid != NULL) {  /* the elements' payloads (OQ-244) */
        const cint_mem_table *t = ctx->mem->laid;
        if ((s->plans = (ty_plan *)cint_state_mem_alloc(ctx, (size_t)t->count * sizeof *s->plans, 8u)) == NULL &&
            t->count > 0u) {
            return CINT_RESOURCE;
        }
        for (s->plan_count = 0u; s->plan_count < t->count; s->plan_count++) {
            if ((st = ty_plan_of(ctx, &t->decls[s->plan_count], &s->plans[s->plan_count])) != CINT_OK) {
                return st;  /* a table whose types and layouts do not agree: CINT_REFUSED */
            }
        }
    }
    put_state(ctx, s, &w);
    if ((s->bytes = (uint8_t *)cint_state_mem_alloc(ctx, w.len, 8u)) == NULL) {
        return CINT_RESOURCE;
    }
    s->len = w.len;
    w = cint_state_wr_of(s->bytes, s->len);
    put_state(ctx, s, &w);
    return CINT_OK;
}

/* The CINT-CKPT-1 envelope before state bytes of length n. */
static void put_envelope(cint_state_wr *w, size_t n)
{
    cint_state_w_str(w, CINT_STATE_LIT(CKPT_DOMAIN));
    cint_state_w_str(w, CINT_STATE_LIT(CINT_RT_CONTRACT));
    cint_state_w_le(w, 0u, 1u);  /* the payload digest, */
    cint_state_w_le(w, 0u, 1u);  /* the build record digest */
    cint_state_w_le(w, 0u, 1u);  /* and the build identity: absent */
    cint_state_w_le(w, n, 8u);
}

cint_status cint_ctx_checkpoint(cint_ctx *ctx, struct cint_sink *sink)
{
    uint8_t head[4u + sizeof CKPT_DOMAIN - 1u + 4u + CINT_STATE_CONTRACT_MAX + 3u + 8u];
    cint_state_wr w = cint_state_wr_of(head, sizeof head);
    snap s;
    cint_status st;
    if (ctx == NULL || sink == NULL || sink->write == NULL) {
        return CINT_REFUSED;
    }
    if (!cint_rt_busy_take(ctx)) {
        return CINT_BUSY;
    }
    st = state_bytes(ctx, &s);
    if (st == CINT_OK) {
        put_envelope(&w, s.len);
        st = sink->write(sink->user, head, w.len);
        if (st == CINT_OK) {
            st = sink->write(sink->user, s.bytes, s.len);
        }
    }
    snap_free(ctx, &s);
    cint_rt_busy_give(ctx);
    return st;
}

cint_status cint_ctx_state_hash(cint_ctx *ctx, uint8_t out[32])
{
    snap s;
    cint_status st;
    if (ctx == NULL || out == NULL) {
        return CINT_REFUSED;
    }
    if (!cint_rt_busy_take(ctx)) {
        return CINT_BUSY;
    }
    st = state_bytes(ctx, &s);
    if (st == CINT_OK) {
        cint_sha256(s.bytes, s.len, out);
    }
    snap_free(ctx, &s);
    cint_rt_busy_give(ctx);
    return st;
}

/* The state hash as a recording begins, inside an entry, or at a dispatch boundary
 * (cint_rt_dispatch_end), and with keep the CINT-CKPT-1 of that state in *keep, from malloc.
 * The status of state_bytes, or CINT_RESOURCE when memory runs out. */
cint_status cint_state_boundary(cint_ctx *ctx, uint8_t hash[32], uint8_t **keep, size_t *keep_len)
{
    uint8_t head[4u + sizeof CKPT_DOMAIN - 1u + 4u + CINT_STATE_CONTRACT_MAX + 3u + 8u];
    cint_state_wr w = cint_state_wr_of(head, sizeof head);
    snap s;
    cint_status st = state_bytes(ctx, &s);
    if (st == CINT_OK) {
        cint_sha256(s.bytes, s.len, hash);
    }
    if (st == CINT_OK && keep != NULL) {
        put_envelope(&w, s.len);
        if ((*keep = (uint8_t *)malloc(w.len + s.len)) == NULL) {
            st = CINT_RESOURCE;
        } else {
            memcpy(*keep, head, w.len);
            memcpy(*keep + w.len, s.bytes, s.len);
            *keep_len = w.len + s.len;
        }
    }
    snap_free(ctx, &s);
    return st;
}

/* ------------------------------------------------------------------------- */
/* Restore (H-19a, M-11, M-11a).                                              */

/* The site of program p at a position: the lowest site of the module with that path whose
 * line and column match. Every such site encodes to the same position (SPEC-01 11.2). */
static bool site_back(const cint_program *p, const cint_position *pos, cint_site *out)
{
    for (uint32_t m = 0u; m < p->module_count; m++) {
        const cint_module *mod = p->modules[m];
        if (mod->path_len != pos->path_len || memcmp(mod->path, pos->path, pos->path_len) != 0) {
            continue;
        }
        for (uint32_t k = 1u; mod->sites != NULL && k < mod->site_count; k++) {
            if (mod->sites[k].line == pos->line && mod->sites[k].column == pos->column) {
                out->module = m;
                out->index = k;
                return true;
            }
        }
    }
    return false;
}

/* The fault record of a checkpoint as the context holds it, with its positions as sites of
 * the context's program. CINT_REFUSED unless the record encodes back to the same bytes. */
static cint_status fault_back(cint_ctx *ctx, const uint8_t *bytes, size_t len, cint_fault_record *rec)
{
    cint_state_rd r = cint_state_rd_of(bytes, len);
    cint_state_fault_view f;
    uint8_t *again;
    bool same;
    cint_state_rd_fault(&r, &f);
    memset(rec, 0, sizeof *rec);
    if (r.bad || f.has_map || !site_back(ctx->program, &f.position, &rec->position)) {
        return CINT_REFUSED;  /* cint-rt-3 has no carrier for a source-map digest (RT-OQ-28) */
    }
    rec->program = ctx->program;
    rec->code = f.code;
    rec->operation_len = f.operation_len;
    memcpy(rec->operation, f.operation, f.operation_len);
    rec->operand_count = f.operand_count;
    memcpy(rec->operands, f.operands, f.operand_count * sizeof f.operands[0]);
    rec->has_exact = (uint8_t)f.has_exact;
    rec->exact = f.exact;
    rec->has_limit = (uint8_t)f.has_limit;
    rec->limit = f.limit;
    rec->has_revision = (uint8_t)f.has_revision;
    if (f.has_revision) {
        memcpy(rec->revision, f.revision, 32u);
    }
    rec->has_address = (uint8_t)f.has_address;
    if (f.has_address) {
        rec->address.dispatch = f.dispatch;
        rec->address.work_item = f.work_item;
        rec->address.step = f.step;
        rec->address.phase = f.phase;
        rec->address.has_work_item = (uint8_t)f.has_work_item;
        rec->address.name_len = (uint16_t)f.kernel_len;
        memcpy(rec->address.name, f.kernel, f.kernel_len);
    }
    rec->stack_count = f.stack_count;
    r.at = f.stack_at;
    for (uint32_t i = 0u; i < f.stack_count; i++) {
        cint_position pos;
        rd_pos(&r, &pos);
        if (r.bad || !site_back(ctx->program, &pos, &rec->stack[i])) {
            return CINT_REFUSED;
        }
    }
    if (cint_fault_encode(rec, NULL, 0u) != len) {
        return CINT_REFUSED;
    }
    if ((again = (uint8_t *)cint_state_mem_alloc(ctx, len, 1u)) == NULL) {
        return CINT_RESOURCE;
    }
    same = cint_fault_encode(rec, again, len) == len && memcmp(again, bytes, len) == 0;
    cint_state_mem_free(ctx, again, len, 1u);
    return same ? CINT_OK : CINT_REFUSED;
}

/* Every check of a restore that needs the context. */
static cint_status restore_check(cint_ctx *ctx, const uint8_t *bytes, size_t length, cint_checkpoint_info *c,
                                 glist *g, cint_fault_record **rec)
{
    cint_checkpoint_global cg;
    cint_checkpoint_buffer cb;
    size_t cur = 0u;
    cint_status st;
    for (uint32_t i = 0u; i < ctx->buffer_cap; i++) {
        if (ctx->leases[i].lease != 0u) {
            return CINT_BUSY;  /* a leased address stays valid until cint_lease_end (A-9) */
        }
    }
    if (cint_checkpoint_decode(bytes, length, c) != CINT_OK || ctx->states == NULL ||
        ctx->program->revision == NULL || c->contract.len != sizeof CINT_RT_CONTRACT - 1u ||
        memcmp(c->contract.bytes, CINT_RT_CONTRACT, c->contract.len) != 0 ||
        memcmp(c->revision, ctx->program->revision, 32u) != 0 || !covered(ctx, ctx->states)) {
        return CINT_REFUSED;
    }
    if ((st = globals(ctx, ctx->program, ctx->states, g)) != CINT_OK) {
        return st;
    }
    if (c->global_count != g->count) {
        return CINT_REFUSED;
    }
    for (size_t i = 0u; i < g->count; i++) {
        if (cint_checkpoint_global_next(c, &cur, &cg) != CINT_OK || !same_global(&cg, &g->g[i])) {
            return CINT_REFUSED;
        }
        if (g->g[i].plan != NULL) {  /* a struct's Bools 0 or 1 (H-19a) */
            const uint8_t *in = cg.elements + g->g[i].type->type_len;
            if (!ty_ok(g->g[i].plan, &g->g[i].plan->elem, &in)) {
                return CINT_REFUSED;
            }
        }
    }
    cur = 0u;
    while (cint_checkpoint_buffer_next(c, &cur, &cb) == CINT_OK) {
        if (cb.type.code == CINT_TAG_RECORD && (ctx->module == NULL || cb.type.record_id >= ctx->module->record_count)) {
            return CINT_REFUSED;
        }
    }
    if (c->fault == NULL) {
        return CINT_OK;
    }
    if ((*rec = (cint_fault_record *)cint_state_mem_alloc(ctx, sizeof **rec, _Alignof(cint_fault_record))) == NULL) {
        return CINT_RESOURCE;
    }
    return fault_back(ctx, c->fault, c->fault_len, *rec);
}

/* Zero-filled storage of `count` elements of `size` bytes into *out (rt/cint_mem.c's
 * alignment), none for 0; false when it cannot be had. */
static bool take_zero(cint_ctx *ctx, uint64_t count, size_t size, void **out)
{
    bool fits = size == 0u || count <= (SIZE_MAX - 1u) / size;
    *out = fits && count > 0u && size > 0u ? cint_mem_take(ctx, (size_t)count * size) : NULL;
    return fits && (*out != NULL || count == 0u || size == 0u);
}

/* New storage for a container's elements, and its size. */
typedef struct laid_storage {
    void *p;
    size_t bytes;
} laid_storage;

/* cint_mem_adopt's first step for a state that mem_build holds (rt/cint_mem.h): table t declares exactly
 * the module-level arenas and the pools held, each with the held element type byte for byte, and each
 * element read as one of that type (ty_get) goes into storage of t's layout. All or nothing: CINT_REFUSED,
 * with s unchanged, when a container, a type or an element is not t's; CINT_RESOURCE when memory runs
 * out. */
static cint_status lay_out(cint_ctx *ctx, struct mem_state *s, const cint_mem_table *t)
{
    size_t n = (size_t)t->count;
    ty_plan *plans = (ty_plan *)cint_state_mem_alloc(ctx, n * sizeof *plans, 8u);
    laid_storage *fresh = (laid_storage *)cint_state_mem_alloc(ctx, n * sizeof *fresh, 8u);
    cint_status st = n > 0u && (plans == NULL || fresh == NULL) ? CINT_RESOURCE : CINT_OK;
    uint32_t a = 0u, q = 0u, k;
    if (st == CINT_OK && n > 0u) {
        memset(plans, 0, n * sizeof *plans);
        memset(fresh, 0, n * sizeof *fresh);
    }
    for (k = 0u; st == CINT_OK && k < t->count; k++) {
        const cint_mem_decl *d = &t->decls[k];
        mem_arena *x = d->kind == CINT_HANDLE_ARENA && a < s->arena_count ? &s->arenas[a] : NULL;
        mem_pool *p = d->kind == CINT_HANDLE_POOL && q < s->pool_count ? &s->pools[q] : NULL;
        const uint8_t *held = x != NULL ? x->type : p != NULL ? p->type : NULL;
        const uint8_t *in = x != NULL ? x->storage : p != NULL ? p->storage : NULL;
        uint32_t held_len = x != NULL ? x->type_len : p != NULL ? p->type_len : 0u;
        int64_t room = x != NULL ? x->room : p != NULL ? p->room : 0;
        ty_plan *plan = &plans[k];
        a += d->kind == CINT_HANDLE_ARENA ? 1u : 0u;
        q += d->kind == CINT_HANDLE_POOL ? 1u : 0u;
        if (held == NULL || (x != NULL && x->parent != 0u) || (st = ty_plan_of(ctx, d, plan)) != CINT_OK) {
            st = st != CINT_OK ? st : CINT_REFUSED;
            break;
        }
        if (held_len != 4u + d->type_len || le32(held) != plan->width ||
            memcmp(held + 4u, d->type, d->type_len) != 0) {
            st = CINT_REFUSED;
            break;
        }
        if (!take_zero(ctx, (uint64_t)room, d->elem_bytes, &fresh[k].p)) {
            st = CINT_RESOURCE;
            break;
        }
        fresh[k].bytes = (size_t)room * d->elem_bytes;
        for (int64_t i = 0; i < room && st == CINT_OK; i++) {  /* an arena's used extent, a pool's used slots */
            const uint8_t *at = plan->width > 0u ? in + (size_t)i * plan->width : in;
            if (p == NULL || (p->marks[i] & MEM_OCCUPIED) != 0u) {
                st = ty_get(plan, &plan->elem, &at, (uint8_t *)fresh[k].p + (size_t)i * d->elem_bytes) ? CINT_OK
                                                                                                    : CINT_REFUSED;
            }
        }
    }
    if (st == CINT_OK && a < s->arena_count && s->arenas[a].parent == 0u) {
        st = CINT_REFUSED;  /* a module-level arena that t does not declare */
    }
    for (k = 0u, a = 0u, q = 0u; st == CINT_OK && k < t->count; k++) {
        mem_arena *x = t->decls[k].kind == CINT_HANDLE_ARENA ? &s->arenas[a++] : NULL;
        mem_pool *p = x == NULL ? &s->pools[q++] : NULL;
        unsigned char **storage = x != NULL ? &x->storage : &p->storage;
        cint_state_mem_free(ctx, *storage, (size_t)(x != NULL ? x->room : p->room) *
                                               (x != NULL ? x->elem_bytes : p->elem_bytes), 16u);
        *storage = (unsigned char *)fresh[k].p;
        fresh[k].p = NULL;
        if (x != NULL) {
            x->elem_bytes = t->decls[k].elem_bytes;
            x->type = NULL;
            x->type_len = 0u;
        } else {
            p->elem_bytes = t->decls[k].elem_bytes;
            p->type = NULL;
            p->type_len = 0u;
        }
    }
    if (st == CINT_OK) {
        cint_state_mem_free(ctx, s->held, s->held_len, 16u);
        s->held = NULL;
        s->held_len = 0u;
        s->lay_out = NULL;
        s->laid = t;
    }
    for (k = 0u; k < t->count && plans != NULL && fresh != NULL; k++) {
        cint_state_mem_free(ctx, fresh[k].p, fresh[k].bytes, 16u);
        ty_free(ctx, &plans[k]);
    }
    cint_state_mem_free(ctx, plans, n * sizeof *plans, 8u);
    cint_state_mem_free(ctx, fresh, n * sizeof *fresh, 8u);
    return st;
}

/* The arenas and pools of checkpoint c as a new state into *out, NULL when c has none
 * (RT-OQ-46). Each container holds its used extent, its elements as the checkpoint wrote them
 * and its element type (`held`); the table the context has named, if any, then lays them out
 * (lay_out) and gives each its capacity (cint_mem_adopt), and is CINT_REFUSED when it does not
 * fit; CINT_RESOURCE when memory runs out. The context does not change. */
static cint_status mem_build(cint_ctx *ctx, const cint_checkpoint_info *c, struct mem_state **out)
{
    cint_checkpoint_arena a;
    cint_checkpoint_pool p;
    struct mem_state *m;
    size_t cur = 0u, held = 0u, h = 0u;
    uint32_t arenas = 0u, pools = 0u;
    cint_status st = CINT_OK;
    bool ok;
    *out = NULL;
    while (cint_checkpoint_arena_next(c, &cur, &a) == CINT_OK) {
        arenas++;
        held += a.parent == 0u ? 4u + (size_t)a.type_len : 0u;  /* sections of the checkpoint: no overflow */
    }
    for (cur = 0u; cint_checkpoint_pool_next(c, &cur, &p) == CINT_OK;) {
        pools++;
        held += 4u + (size_t)p.type_len;
    }
    if (arenas == 0u && pools == 0u) {
        return CINT_OK;  /* made at the first arena or pool operation, as in a fresh context */
    }
    if ((m = (struct mem_state *)cint_mem_take(ctx, sizeof *m)) == NULL) {
        return CINT_RESOURCE;
    }
    m->arena_cap = arenas < 4u ? 4u : arenas;
    m->arenas = (mem_arena *)cint_mem_take(ctx, (size_t)m->arena_cap * sizeof(mem_arena));
    m->arena_cap = m->arenas != NULL ? m->arena_cap : 0u;
    m->pools = pools == 0u ? NULL : (mem_pool *)cint_mem_take(ctx, (size_t)pools * sizeof(mem_pool));
    m->pool_cap = m->pools != NULL ? pools : 0u;
    m->held = (uint8_t *)cint_mem_take(ctx, held);
    m->held_len = m->held != NULL ? held : 0u;
    m->lay_out = lay_out;
    ok = m->arenas != NULL && (pools == 0u || m->pools != NULL) && m->held != NULL;
    for (cur = 0u; ok && cint_checkpoint_arena_next(c, &cur, &a) == CINT_OK;) {
        mem_arena *x = &m->arenas[m->arena_count];
        uint32_t up = a.parent != 0u ? cint_mem_row(m, a.parent) : 0u;  /* among the rows before */
        x->id = a.id;
        x->capacity = a.capacity;
        x->used = a.used;
        x->generation = a.generation;
        x->elem_bytes = a.elem_bytes;
        if (a.parent == 0u) {
            void *v;
            x->root = m->arena_count;
            ok = take_zero(ctx, (uint64_t)a.used, a.elem_bytes, &v);
            x->storage = (unsigned char *)v;
            x->room = ok ? a.used : 0;
            if (ok && a.used > 0 && a.elem_bytes > 0u) {
                memcpy(x->storage, a.elements, (size_t)a.used * a.elem_bytes);
            }
            memcpy(m->held + h, a.type - 4u, 4u + (size_t)a.type_len);  /* the width, then the type */
            x->type = m->held + h;
            x->type_len = 4u + a.type_len;
            h += x->type_len;
        } else {
            x->root = m->arenas[up].root;
            x->parent = up + 1u;
            x->offset = m->arenas[up].offset + a.offset;
            x->parent_generation = a.retired != 0u ? MEM_GEN_RETIRED : m->arenas[up].generation;  /* M-20 */
        }
        m->arena_count++;
    }
    cur = 0u;
    for (uint32_t k = 0u; ok && k < pools && cint_checkpoint_pool_next(c, &cur, &p) == CINT_OK; k++) {
        mem_pool *q = &m->pools[k];
        void *v[4] = {NULL, NULL, NULL, NULL};
        m->pool_count = k + 1u;
        q->capacity = p.capacity;
        q->elem_bytes = p.elem_bytes;
        q->fresh = (int64_t)p.used;
        q->occupied = (int64_t)p.occupied;
        q->retired = (int64_t)p.retired;
        q->free_count = (int64_t)p.vacant;
        q->room = (int64_t)p.used;  /* the arrays not taken stay NULL */
        memcpy(m->held + h, p.type - 4u, 4u + (size_t)p.type_len);
        q->type = m->held + h;
        q->type_len = 4u + p.type_len;
        h += q->type_len;
        ok = take_zero(ctx, p.used, p.elem_bytes, &v[0]) && take_zero(ctx, p.used, sizeof(uint64_t), &v[1]) &&
             take_zero(ctx, p.used, 1u, &v[2]) && take_zero(ctx, p.used, sizeof(int64_t), &v[3]);
        q->storage = (unsigned char *)v[0];
        q->gens = (uint64_t *)v[1];
        q->marks = (uint8_t *)v[2];
        q->free_list = (int64_t *)v[3];
        for (uint64_t i = 0u, e = 0u; ok && i < p.used; i++) {
            const uint8_t *rec = p.slots + (size_t)i * SLOT_BYTES;
            q->gens[i] = le64(rec);
            q->marks[i] = (uint8_t)(rec[8] == CINT_SLOT_OCCUPIED  ? MEM_OCCUPIED
                                    : rec[8] == CINT_SLOT_RETIRED ? MEM_RETIRED
                                                                  : 0u);
            if (rec[8] == CINT_SLOT_OCCUPIED && p.elem_bytes > 0u) {
                memcpy(q->storage + (size_t)i * p.elem_bytes, p.elements + (size_t)(e++) * p.elem_bytes, p.elem_bytes);
            }
        }
        for (uint64_t top = p.top, i = p.vacant; ok && top != 0u && i > 0u;) {  /* M-18: top last */
            q->free_list[--i] = (int64_t)(top - 1u);
            top = le64(p.slots + (size_t)(top - 1u) * SLOT_BYTES + 9u);
        }
    }
    if (ok && ctx->mem != NULL && ctx->mem->table != NULL) {
        st = cint_mem_adopt(ctx, m, ctx->mem->table);
    }
    if (!ok || st != CINT_OK) {
        cint_mem_state_free(ctx, m);
        return ok ? st : CINT_RESOURCE;
    }
    *out = m;
    return CINT_OK;
}

cint_status cint_ctx_restore(cint_ctx *ctx, const uint8_t *bytes, size_t length)
{
    cint_checkpoint_info c;
    cint_checkpoint_global cg;
    cint_fault_record *rec = NULL;
    struct mem_state *mem = NULL;
    glist g;
    size_t cur = 0u;
    cint_status st;
    if (ctx == NULL || bytes == NULL) {
        return CINT_REFUSED;
    }
    if (!cint_rt_busy_take(ctx)) {
        return CINT_BUSY;
    }
    memset(&g, 0, sizeof g);
    st = restore_check(ctx, bytes, length, &c, &g, &rec);
    if (st == CINT_OK) {
        st = blocks_make(ctx, &ctx->head.state, ctx->program->module_count, ctx->states);
    }
    if (st == CINT_OK) {
        st = mem_build(ctx, &c, &mem);
    }
    if (st == CINT_OK) {
        if (ctx->mem != NULL) {
            cint_mem_state_free(ctx, ctx->mem);
        }
        ctx->mem = mem;
        for (size_t i = 0u; i < g.count; i++) {
            uint8_t *at = (uint8_t *)ctx->head.state[g.g[i].state->module] + g.g[i].offset;
            (void)cint_checkpoint_global_next(&c, &cur, &cg);
            if (g.g[i].plan != NULL) {  /* a struct, through its payload (OQ-260) */
                const uint8_t *in = cg.elements + g.g[i].type->type_len;
                memset(at, 0, (size_t)g.g[i].plan->elem.stride);
                (void)ty_get(g.g[i].plan, &g.g[i].plan->elem, &in, at);
                continue;
            }
            memcpy(at, cg.elements != NULL ? cg.elements : cg.value.bytes + 1, (size_t)(g.g[i].width * g.g[i].count));
        }
        for (uint32_t i = 0u; i < ctx->buffer_used; i++) {
            buffer_slot *b = &ctx->buffers[i];
            if (b->live != 0u) {  /* none is context-owned (M-11a) */
                if (b->storage == STORAGE_CREATED) {
                    cint_state_mem_free(ctx, b->base, (size_t)b->bytes, 8u);
                    b->base = NULL;
                }
                b->live = 0u;
                b->generation++;
            }
        }
        ctx->last_id = c.next_buffer - 1u > ctx->last_id ? c.next_buffer - 1u : ctx->last_id;
        ctx->last_arena = c.next_arena - 1u > ctx->last_arena ? c.next_arena - 1u : ctx->last_arena;
        ctx->last_pool = c.next_pool - 1u > ctx->last_pool ? c.next_pool - 1u : ctx->last_pool;
        ctx->entries = c.entries;
        ctx->effects = c.effects;
        if (rec != NULL) {
            memcpy(&ctx->fault, rec, sizeof *rec);
        } else {
            memset(&ctx->fault, 0, sizeof ctx->fault);
        }
        ctx->head.faulted = rec != NULL ? 1u : 0u;
        ctx->desc_count = 0u;
    }
    glist_free(ctx, &g);
    cint_state_mem_free(ctx, rec, sizeof *rec, _Alignof(cint_fault_record));
    cint_rt_busy_give(ctx);
    return st;
}

/* ------------------------------------------------------------------------- */
/* The carry of arenas and pools (decision 2026-10-08 on OQ-243).             */
/*                                                                           */
/* A change of revision keeps each module-level arena and pool that the new  */
/* table declares in a module of the same path with the same name, kind,     */
/* capacity and element type: its contents, through their IM-146 payloads,   */
/* and an arena's children, retired or not. Every other container of the new */
/* table starts empty, and every other old one is dropped (rt/OPEN.md        */
/* RT-OQ-46). The module-level containers take identifiers 1 to n in the new */
/* declaration order. The children keep theirs when each lies above the new  */
/* module-level arenas, and otherwise take the next ones the context issues, */
/* in their order. A handle, in a global or an element, names its           */
/* container's new identifier, or container 0 when it was dropped, so that   */
/* its next use faults as stale with reason container (M-17).                */

typedef struct carry {
    struct mem_state *from;  /* the old state; NULL when there is none, and nothing is rewritten */
    struct mem_state *to;    /* the new one; NULL when the new table declares no container */
    uint32_t *ids;           /* per old arena row, then per old pool, its new identifier; 0 when dropped */
    uint32_t *rows;          /* per old arena row, its new row */
    uint32_t *match;         /* per new declaration, the old one it keeps plus 1; 0 for none */
    size_t words;            /* of ids, rows and match, one allocation */
    ty_plan *plans;          /* per new declaration, the old one's plan and its own */
    uint32_t nplan;
    uint8_t *payload;        /* one element's payload */
    size_t payload_len;
    uint64_t last_arena, last_pool;  /* the identifiers issued once the change is made */
} carry;

static void carry_free(cint_ctx *ctx, carry *c)
{
    if (c->to != NULL) {
        cint_mem_state_free(ctx, c->to);
    }
    cint_state_mem_free(ctx, c->ids, c->words * sizeof *c->ids, 8u);
    for (uint32_t k = 0u; k < c->nplan; k++) {
        ty_free(ctx, &c->plans[k]);
    }
    cint_state_mem_free(ctx, c->plans, (size_t)c->nplan * sizeof *c->plans, 8u);
    cint_state_mem_free(ctx, c->payload, c->payload_len, 8u);
    memset(c, 0, sizeof *c);
}

/* Rewrites the container of the handle at h, a cint_handle or its payload, through c. */
static void carry_handle(const carry *c, uint8_t *h)
{
    uint32_t id = 0u, k;
    memcpy(&id, h + offsetof(cint_handle, container), sizeof id);
    if (h[offsetof(cint_handle, kind)] == CINT_HANDLE_ARENA) {
        k = cint_mem_row(c->from, id);
        id = k != UINT32_MAX ? c->ids[k] : 0u;
    } else if (h[offsetof(cint_handle, kind)] == CINT_HANDLE_POOL) {
        id = id >= 1u && id <= c->from->pool_count ? c->ids[c->from->arena_count + id - 1u] : 0u;
    }
    memcpy(h + offsetof(cint_handle, container), &id, sizeof id);
}

/* Rewrites each handle of item x of plan p stored at `at`. */
static void carry_item(const carry *c, const ty_plan *p, const ty_part *x, uint8_t *at)
{
    for (uint64_t i = 0u; i < x->count; i++, at += x->stride) {
        if (x->tag == CINT_TAG_HANDLE) {
            carry_handle(c, at);
        }
        for (uint32_t j = 0u; x->tag == TY_STRUCT && j < p->defs[x->def].nfield; j++) {
            const ty_part *y = &p->parts[p->defs[x->def].first + j];
            carry_item(c, p, y, at + y->offset);
        }
    }
}

/* n elements stored from `in`, `from` bytes apart, through plan a, into `out`, `to` bytes apart, through
 * plan b, with their handles rewritten; a pool's slots that `marks` does not mark occupied stay zero. False
 * for an element that b does not read back. */
static bool carry_elements(const carry *c, const ty_plan *a, const ty_plan *b, const uint8_t *in, uint32_t from,
                           uint8_t *out, uint32_t to, int64_t n, const uint8_t *marks)
{
    for (int64_t i = 0; i < n; i++) {
        cint_state_wr w = cint_state_wr_of(c->payload, c->payload_len);
        const uint8_t *at = c->payload;
        if (marks == NULL || (marks[i] & MEM_OCCUPIED) != 0u) {
            ty_put(a, &a->elem, in + (size_t)i * from, &w);
            if (!ty_get(b, &b->elem, &at, out + (size_t)i * to)) {
                return false;
            }
            carry_item(c, b, &b->elem, out + (size_t)i * to);
        }
    }
    return true;
}

/* Whether old declaration e of module x and new declaration d of module y declare one container. */
static bool carry_same(const cint_module *x, const cint_mem_decl *e, const cint_module *y, const cint_mem_decl *d)
{
    return d->name_len != 0u && e->kind == d->kind && e->capacity == d->capacity && e->name_len == d->name_len &&
           e->type_len == d->type_len && x->path_len == y->path_len && memcmp(e->name, d->name, d->name_len) == 0 &&
           memcmp(e->type, d->type, d->type_len) == 0 && memcmp(x->path, y->path, x->path_len) == 0;
}

/* The old row of an arena, or the old index of a pool, that old declaration j declares. */
static uint32_t carry_ordinal(const cint_mem_table *o, uint32_t j)
{
    uint32_t r = 0u;
    for (uint32_t i = 0u; i < j; i++) {
        r += o->decls[i].kind == o->decls[j].kind ? 1u : 0u;
    }
    return r;
}

/* Fills the new state's containers, module-level ones in t's order and then the children, from the old
 * state m laid out by table o. */
static cint_status carry_fill(cint_ctx *ctx, carry *c, const cint_mem_table *o, const cint_mem_table *t,
                              uint32_t old_arenas)
{
    struct mem_state *m = c->from, *n = c->to;
    for (uint32_t k = 0u; k < t->count; k++) {
        const cint_mem_decl *d = &t->decls[k];
        const ty_plan *a = &c->plans[2u * k], *b = &c->plans[2u * k + 1u];
        uint32_t r = c->match[k] != 0u ? carry_ordinal(o, c->match[k] - 1u) : UINT32_MAX;
        void *v[4] = {NULL, NULL, NULL, NULL};
        bool ok = true;
        if (d->kind == CINT_HANDLE_ARENA) {
            mem_arena *x = &n->arenas[n->arena_count];
            const mem_arena *y = r != UINT32_MAX ? &m->arenas[r] : NULL;
            x->id = n->arena_count + 1u;
            x->root = n->arena_count++;
            x->capacity = d->capacity;
            x->elem_bytes = d->elem_bytes;
            if (y != NULL) {
                if (!take_zero(ctx, (uint64_t)y->used, d->elem_bytes, &v[0])) {
                    return CINT_RESOURCE;
                }
                x->storage = (unsigned char *)v[0];
                x->room = x->used = y->used;
                x->generation = y->generation;
                ok = carry_elements(c, a, b, y->storage, y->elem_bytes, x->storage, x->elem_bytes, y->used, NULL);
            }
        } else {
            mem_pool *x = &n->pools[n->pool_count++];
            const mem_pool *y = r != UINT32_MAX ? &m->pools[r] : NULL;
            x->capacity = d->capacity;
            x->elem_bytes = d->elem_bytes;
            if (y != NULL) {
                uint64_t used = (uint64_t)y->fresh;
                ok = take_zero(ctx, used, d->elem_bytes, &v[0]) && take_zero(ctx, used, sizeof(uint64_t), &v[1]) &&
                     take_zero(ctx, used, 1u, &v[2]) && take_zero(ctx, used, sizeof(int64_t), &v[3]);
                x->storage = (unsigned char *)v[0];
                x->gens = (uint64_t *)v[1];
                x->marks = (uint8_t *)v[2];
                x->free_list = (int64_t *)v[3];
                x->room = y->fresh;  /* the arrays not taken stay NULL */
                if (!ok) {
                    return CINT_RESOURCE;
                }
                if (used > 0u) {
                    memcpy(x->gens, y->gens, (size_t)used * sizeof(uint64_t));
                    memcpy(x->marks, y->marks, (size_t)used);
                    memcpy(x->free_list, y->free_list, (size_t)y->free_count * sizeof(int64_t));
                }
                x->free_count = y->free_count;
                x->fresh = y->fresh;
                x->occupied = y->occupied;
                x->retired = y->retired;
                ok = carry_elements(c, a, b, y->storage, y->elem_bytes, x->storage, x->elem_bytes, y->fresh, y->marks);
            }
        }
        if (!ok) {
            return CINT_REFUSED;  /* an element that the new layout does not read back */
        }
    }
    for (uint32_t r = old_arenas; r < m->arena_count; r++) {  /* the children of the arenas kept */
        const mem_arena *y = &m->arenas[r];
        mem_arena *x = &n->arenas[n->arena_count];
        if (c->ids[y->root] == 0u) {
            continue;
        }
        *x = *y;
        x->id = c->ids[r];
        x->root = c->rows[y->root];
        x->parent = c->rows[y->parent - 1u] + 1u;
        x->elem_bytes = n->arenas[x->root].elem_bytes;
        c->rows[r] = n->arena_count++;
    }
    return CINT_OK;
}

/* The change of revision's new state for table t of program p (NULL: none) into c, from ctx->mem. A state
 * held since a restore is first given the old revision's table, which ctx->states names (cint_mem_adopt).
 * CINT_REFUSED for a table that is not one, or identifiers run out; CINT_RESOURCE when memory runs out. The
 * context does not change, but for that adoption. */
static cint_status carry_make(cint_ctx *ctx, const cint_program *p, const cint_mem_table *t, carry *c)
{
    struct mem_state *m = ctx->mem;
    const cint_mem_table *o;
    uint32_t arenas = 0u, pools = 0u, old_arenas = 0u, old_pools = 0u, kids = 0u, low = UINT32_MAX, count, cap;
    uint64_t next;
    size_t width = 0u;
    cint_status st = CINT_OK;
    memset(c, 0, sizeof *c);
    c->last_arena = ctx->last_arena;
    c->last_pool = ctx->last_pool;
    if (m == NULL) {
        return CINT_OK;  /* no arena or pool operation has run: the new program's first makes them */
    }
    count = t != NULL ? t->count : 0u;
    if ((t != NULL && (!cint_mem_table_ok(t, &arenas, &pools) || t->module >= p->module_count)) ||
        (m->table == NULL && ctx->states->memory == NULL)) {
        return CINT_REFUSED;
    }
    if (m->table == NULL && (st = cint_mem_adopt(ctx, m, ctx->states->memory)) != CINT_OK) {
        return st;
    }
    o = m->table;
    if (!cint_mem_table_ok(o, &old_arenas, &old_pools) || o->module >= ctx->program->module_count) {
        return CINT_REFUSED;
    }
    c->from = m;
    c->words = (size_t)m->arena_count * 2u + m->pool_count + count;
    if ((c->ids = (uint32_t *)cint_state_mem_alloc(ctx, c->words * sizeof *c->ids, 8u)) == NULL && c->words > 0u) {
        return CINT_RESOURCE;
    }
    if (c->ids != NULL) {
        memset(c->ids, 0, c->words * sizeof *c->ids);
    }
    c->rows = c->ids + m->arena_count + m->pool_count;
    c->match = c->rows + m->arena_count;
    for (uint32_t k = 0u, na = 0u, np = 0u; k < count; k++) {  /* the module-level containers kept */
        const cint_mem_decl *d = &t->decls[k];
        uint32_t id = d->kind == CINT_HANDLE_ARENA ? ++na : ++np;
        for (uint32_t j = 0u, oa = 0u, op = 0u; j < o->count && c->match[k] == 0u; j++) {
            const cint_mem_decl *e = &o->decls[j];
            uint32_t *at = e->kind == CINT_HANDLE_ARENA ? &c->ids[oa++] : &c->ids[m->arena_count + op++];
            if (*at == 0u && carry_same(ctx->program->modules[o->module], e, p->modules[t->module], d)) {
                *at = id;
                if (e->kind == CINT_HANDLE_ARENA) {
                    c->rows[oa - 1u] = id - 1u;
                }
                c->match[k] = j + 1u;
            }
        }
    }
    for (uint32_t r = old_arenas; r < m->arena_count; r++) {  /* the children of the arenas kept */
        if (c->ids[m->arenas[r].root] != 0u) {
            kids++;
            low = m->arenas[r].id < low ? m->arenas[r].id : low;
        }
    }
    next = ctx->last_arena > arenas ? ctx->last_arena : arenas;
    if (kids > 0u && low <= arenas && next + kids >= ID_END) {
        return CINT_REFUSED;  /* no identifiers left for the children */
    }
    for (uint32_t r = old_arenas; r < m->arena_count; r++) {
        if (c->ids[m->arenas[r].root] != 0u) {
            c->ids[r] = low > arenas ? m->arenas[r].id : (uint32_t)++next;
        }
    }
    c->last_arena = count == 0u ? ctx->last_arena : next;
    c->last_pool = count == 0u || ctx->last_pool > pools ? ctx->last_pool : pools;
    if (count == 0u) {
        return CINT_OK;  /* every container is dropped */
    }
    if ((c->plans = (ty_plan *)cint_state_mem_alloc(ctx, (size_t)count * 2u * sizeof *c->plans, 8u)) == NULL) {
        return CINT_RESOURCE;
    }
    memset(c->plans, 0, (size_t)count * 2u * sizeof *c->plans);
    c->nplan = count * 2u;
    for (uint32_t k = 0u; k < count && st == CINT_OK; k++) {
        if (c->match[k] != 0u && (st = ty_plan_of(ctx, &o->decls[c->match[k] - 1u], &c->plans[2u * k])) == CINT_OK &&
            (st = ty_plan_of(ctx, &t->decls[k], &c->plans[2u * k + 1u])) == CINT_OK) {
            width = c->plans[2u * k].width > width ? (size_t)c->plans[2u * k].width : width;
        }
    }
    if (st != CINT_OK) {
        return st;
    }
    c->payload_len = width;
    if ((c->payload = (uint8_t *)cint_state_mem_alloc(ctx, width, 8u)) == NULL && width > 0u) {
        return CINT_RESOURCE;
    }
    if ((c->to = (struct mem_state *)cint_mem_take(ctx, sizeof *c->to)) == NULL) {
        return CINT_RESOURCE;
    }
    cap = arenas + kids < 4u ? 4u : arenas + kids;  /* distinct identifiers below 2^32: no overflow */
    c->to->arenas = (mem_arena *)cint_mem_take(ctx, (size_t)cap * sizeof(mem_arena));
    c->to->arena_cap = c->to->arenas != NULL ? cap : 0u;
    c->to->pools = pools == 0u ? NULL : (mem_pool *)cint_mem_take(ctx, (size_t)pools * sizeof(mem_pool));
    c->to->pool_cap = c->to->pools != NULL ? pools : 0u;
    if (c->to->arenas == NULL || (pools > 0u && c->to->pools == NULL)) {
        return CINT_RESOURCE;
    }
    if ((st = carry_fill(ctx, c, o, t, old_arenas)) != CINT_OK) {
        return st;
    }
    c->to->laid = t;  /* the program's first arena or pool operation adopts it (cint_mem_adopt) */
    return CINT_OK;
}

/* ------------------------------------------------------------------------- */
/* The interrupt and the change of revision.                                  */

void cint_ctx_interrupt(cint_ctx *ctx)
{
    if (ctx != NULL) {
        ctx->interrupt = 1;
    }
}

void cint_rt_interrupt_poll(cint_ctx *ctx)
{
    if (ctx->interrupt != 0) {
        ctx->interrupt = 0;
        if (ctx->head.fuel_limit < 0 || ctx->head.fuel_limit > ctx->head.fuel_used) {
            ctx->head.fuel_limit = ctx->head.fuel_used;
        }
    }
}

/* What cint_ctx_create requires of a program. */
static bool program_ok(const cint_program *p)
{
    if (p->module_count > 0u && p->modules == NULL) {
        return false;
    }
    for (uint32_t i = 0u; i < p->module_count; i++) {
        const cint_module *m = p->modules[i];
        if (m == NULL || m->path == NULL || m->path_len > CINT_FAULT_MAX_PATH || (m->site_count > 0u && m->sites == NULL)) {
            return false;
        }
    }
    return true;
}

cint_status cint_ctx_change_revision(cint_ctx *ctx, const cint_program *program, const cint_state_table *t)
{
    glist from, to;
    carry cy;
    void **state = NULL;
    uint8_t *payload = NULL;  /* a struct's payload on its way to the new block */
    size_t payload_len = 0u;
    cint_status st;
    if (ctx == NULL || program == NULL) {
        return CINT_REFUSED;
    }
    if (!cint_rt_busy_take(ctx)) {
        return CINT_BUSY;
    }
    t = t != NULL ? t : &NO_STATE;
    memset(&from, 0, sizeof from);
    memset(&to, 0, sizeof to);
    memset(&cy, 0, sizeof cy);
    st = ctx->head.faulted != 0u || ctx->module != NULL || ctx->states == NULL || program->revision == NULL ||
                 !program_ok(program) || !covered(ctx, ctx->states)
             ? CINT_REFUSED
             : CINT_OK;
    if (st == CINT_OK) {
        st = globals(ctx, ctx->program, ctx->states, &from);
    }
    if (st == CINT_OK) {
        st = globals(ctx, program, t, &to);
    }
    for (size_t k = 0u; k < to.count; k++) {
        payload_len = to.g[k].plan != NULL && to.g[k].width > payload_len ? to.g[k].width : payload_len;
    }
    if (st == CINT_OK && payload_len > 0u &&
        (payload = (uint8_t *)cint_state_mem_alloc(ctx, payload_len, 8u)) == NULL) {
        st = CINT_RESOURCE;
    }
    if (st == CINT_OK && (st = blocks_make(ctx, &state, program->module_count, t)) != CINT_OK) {
        blocks_free(ctx, state, program->module_count);
    }
    if (st == CINT_OK && (st = carry_make(ctx, program, t->memory, &cy)) != CINT_OK) {
        blocks_free(ctx, state, program->module_count);
    }
    if (st == CINT_OK) {
        /* Both lists are sorted by name: a global of the same name and type, an array's element
         * type and extents included, a struct's whole type (OQ-260), keeps its value, and a
         * handle names its container's new identifier (OQ-243). */
        size_t i = 0u, j = 0u;
        while (i < from.count && j < to.count) {
            const gref *x = &from.g[i], *y = &to.g[j];
            int c = cint_state_name_cmp(x->name, x->len, y->name, y->len);
            if (c == 0 && same_type(x, y)) {
                uint8_t *at = (uint8_t *)state[y->state->module] + y->offset;
                if (y->plan != NULL) {  /* through its payload, which both layouts give */
                    cint_state_wr w = cint_state_wr_of(payload, payload_len);
                    const uint8_t *in = payload;
                    ty_put(x->plan, &x->plan->elem, block_of(ctx, x->state) + x->offset, &w);
                    memset(at, 0, (size_t)y->plan->elem.stride);
                    (void)ty_get(y->plan, &y->plan->elem, &in, at);
                } else {
                    memcpy(at, block_of(ctx, x->state) + x->offset, (size_t)(y->width * y->count));
                }
                if (y->tag == CINT_TAG_HANDLE && cy.from != NULL) {
                    carry_handle(&cy, at);
                }
            }
            if (c <= 0) {
                i++;
            }
            if (c >= 0) {
                j++;
            }
        }
        blocks_free(ctx, ctx->head.state, ctx->program->module_count);
        if (cy.from != NULL) {
            cint_mem_state_free(ctx, ctx->mem);
            ctx->mem = cy.to;
            cy.to = NULL;
        }
        ctx->last_arena = cy.last_arena;
        ctx->last_pool = cy.last_pool;
        ctx->head.state = state;
        ctx->program = program;
        ctx->head.module_count = program->module_count;
        ctx->states = t;
        ctx->error_set = ctx->saved_error_set = NULL;  /* the last entry's, of the old revision */
        ctx->error_tag = ctx->saved_error_tag = 0u;
    }
    carry_free(ctx, &cy);
    cint_state_mem_free(ctx, payload, payload_len, 8u);
    glist_free(ctx, &from);
    glist_free(ctx, &to);
    cint_rt_busy_give(ctx);
    return st;
}

/* ------------------------------------------------------------------------- */
/* The text of `cint-interp show` (SPEC-06 6.1).                              */

void cint_state_t_put(cint_state_tx *t, const void *s, size_t n)
{
    if (t->out != NULL && t->len < t->cap && n > 0u) {
        memcpy(t->out + t->len, s, n < t->cap - t->len ? n : t->cap - t->len);
    }
    t->len += n;
}

void cint_state_t_str(cint_state_tx *t, const char *s)
{
    cint_state_t_put(t, s, strlen(s));
}

void cint_state_t_u64(cint_state_tx *t, uint64_t v)
{
    char b[20];
    size_t n = 0u;
    do {
        b[sizeof b - 1u - n++] = (char)('0' + (int)(v % 10u));
        v /= 10u;
    } while (v != 0u);
    cint_state_t_put(t, b + sizeof b - n, n);
}

void cint_state_t_i64(cint_state_tx *t, int64_t v)
{
    if (v < 0) {
        cint_state_t_str(t, "-");
    }
    cint_state_t_u64(t, v < 0 ? 0u - (uint64_t)v : (uint64_t)v);
}

void cint_state_t_hex(cint_state_tx *t, const uint8_t *p, size_t n)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0u; i < n; i++) {
        char c[2];
        c[0] = hex[p[i] >> 4];
        c[1] = hex[p[i] & 15u];
        cint_state_t_put(t, c, 2u);
    }
}

static void t_digest(cint_state_tx *t, const char *label, const uint8_t *p)
{
    cint_state_t_str(t, label);
    if (p != NULL) {
        cint_state_t_hex(t, p, 32u);
    } else {
        cint_state_t_str(t, "absent");
    }
    cint_state_t_str(t, "\n");
}

void cint_state_t_value(cint_state_tx *t, const char *label, const cint_tvalue *v)
{
    char b[700];  /* the longest is a Z of 264 bytes: 636 digits */
    cint_state_t_str(t, label);
    cint_state_t_put(t, b, cint_tvalue_render(v, b, sizeof b));
    cint_state_t_str(t, "\n");
}

static void t_pos(cint_state_tx *t, const char *label, const cint_position *p)
{
    cint_state_t_str(t, label);
    cint_state_t_put(t, p->path, p->path_len);
    cint_state_t_str(t, ":");
    cint_state_t_u64(t, p->line);
    cint_state_t_str(t, ":");
    cint_state_t_u64(t, p->column);
    cint_state_t_str(t, "\n");
}

void cint_state_t_fault(cint_state_tx *t, const uint8_t *fault, size_t len)
{
    static const char *const codes[CINT_E_ASSERT + 1u] = {
        "", "E_OVERFLOW", "E_DIV_ZERO", "E_BOUNDS", "E_SHAPE", "E_SHIFT", "E_NARROW", "E_ALIAS",
        "E_STALE_HANDLE", "E_FUEL", "E_UNSUPPORTED", "E_DOMAIN", "E_DEPTH", "E_ASSERT"};
    cint_state_rd r = cint_state_rd_of(fault, len);
    cint_state_fault_view f;
    if (fault == NULL) {
        cint_state_t_str(t, "fault absent\n");
        return;
    }
    cint_state_rd_fault(&r, &f);
    if (r.bad) {
        return;  /* not from cint_checkpoint_decode */
    }
    cint_state_t_str(t, "fault ");
    cint_state_t_str(t, codes[f.code]);
    cint_state_t_str(t, " ");
    cint_state_t_put(t, f.operation, f.operation_len);
    cint_state_t_str(t, "\n");
    for (uint32_t i = 0u; i < f.operand_count; i++) {
        cint_state_t_value(t, "fault-operand ", &f.operands[i]);
    }
    if (f.has_exact) {
        cint_state_t_value(t, "fault-exact ", &f.exact);
    }
    if (f.has_limit) {
        cint_state_t_value(t, "fault-limit ", &f.limit);
    }
    t_pos(t, "fault-position ", &f.position);
    t_digest(t, "fault-revision ", f.revision);
    t_digest(t, "fault-source-map ", f.map);
    if (f.has_address) {
        cint_state_t_str(t, "fault-address ");
        cint_state_t_put(t, f.kernel, f.kernel_len);
        cint_state_t_str(t, " dispatch ");
        cint_state_t_i64(t, f.dispatch);
        cint_state_t_str(t, " phase ");
        cint_state_t_u64(t, f.phase);
        if (f.has_work_item) {
            cint_state_t_str(t, " work-item ");
            cint_state_t_i64(t, f.work_item);
            cint_state_t_str(t, " step ");
            cint_state_t_i64(t, f.step);
        }
        cint_state_t_str(t, "\n");
    }
    r.at = f.stack_at;
    for (uint32_t i = 0u; i < f.stack_count; i++) {
        cint_position p;
        rd_pos(&r, &p);
        t_pos(t, "fault-call ", &p);
    }
}

/* An element type (type after its width), or a struct global's (maxrank 4): Bool, Handle, I8 to U64, or its
 * struct's qualified name. */
static void t_type(cint_state_tx *t, const uint8_t *type, uint32_t len, uint32_t maxrank)
{
    cint_state_rd r = cint_state_rd_of(type, len);
    uint32_t ndef = 0u, npart = 0u;
    size_t name = 0u;
    (void)rd_type(&r, NULL, NULL, maxrank, &ndef, &npart, &name);
    if (r.bad) {
        cint_state_t_str(t, "?");
    } else if (ndef > 0u) {
        cint_state_t_put(t, type + name + 4u, le32(type + name));
    } else {
        cint_state_t_str(t, type[4] == CINT_TAG_BOOL     ? "Bool"
                            : type[4] == CINT_TAG_HANDLE ? "Handle"
                                                         : cint_rt_type_name(type[4]));
    }
}

/* The arena and pool tables (RT-OQ-46): a line per arena and per pool, with the element type of
 * each module-level arena and pool and the SHA-256 of the elements written for it, a line per
 * used slot, and the free list from its top. */
static void t_mem(cint_state_tx *t, const cint_checkpoint_info *c)
{
    static const char *const marks[3] = {" vacant\n", " occupied\n", " retired\n"};
    cint_checkpoint_arena a;
    cint_checkpoint_pool p;
    uint8_t h[32];
    size_t cur = 0u;
    uint64_t n = 0u;
    while (cint_checkpoint_arena_next(c, &cur, &a) == CINT_OK) {
        n++;
    }
    cint_state_t_str(t, "arena-block ");
    cint_state_t_u64(t, n);
    cint_state_t_str(t, "\n");
    for (cur = 0u; cint_checkpoint_arena_next(c, &cur, &a) == CINT_OK;) {
        cint_state_t_str(t, "arena ");
        cint_state_t_u64(t, a.id);
        cint_state_t_str(t, " parent ");
        cint_state_t_u64(t, a.parent);
        if (a.type != NULL) {
            cint_state_t_str(t, " element-type ");
            t_type(t, a.type, a.type_len, 1u);
        }
        cint_state_t_str(t, a.retired != 0u ? " retired offset " : " live offset ");
        cint_state_t_i64(t, a.offset);
        cint_state_t_str(t, " capacity ");
        cint_state_t_i64(t, a.capacity);
        cint_state_t_str(t, " used ");
        cint_state_t_i64(t, a.used);
        cint_state_t_str(t, " generation ");
        cint_state_t_u64(t, a.generation);
        if (a.elements != NULL) {
            cint_sha256(a.elements, (size_t)a.used * a.elem_bytes, h);
            t_digest(t, " elements-sha256 ", h);
        } else {
            cint_state_t_str(t, "\n");
        }
    }
    for (cur = 0u, n = 0u; cint_checkpoint_pool_next(c, &cur, &p) == CINT_OK;) {
        n++;
    }
    cint_state_t_str(t, "pool-block ");
    cint_state_t_u64(t, n);
    cint_state_t_str(t, "\n");
    for (cur = 0u; cint_checkpoint_pool_next(c, &cur, &p) == CINT_OK;) {
        cint_state_t_str(t, "pool ");
        cint_state_t_u64(t, p.id);
        cint_state_t_str(t, " element-type ");
        t_type(t, p.type, p.type_len, 1u);
        cint_state_t_str(t, " capacity ");
        cint_state_t_i64(t, p.capacity);
        cint_state_t_str(t, " used ");
        cint_state_t_u64(t, p.used);
        cint_state_t_str(t, " occupied ");
        cint_state_t_u64(t, p.occupied);
        cint_state_t_str(t, " retired ");
        cint_state_t_u64(t, p.retired);
        cint_sha256(p.elements, (size_t)(p.occupied * p.elem_bytes), h);
        t_digest(t, " elements-sha256 ", h);
        for (uint64_t i = 0u; i < p.used; i++) {
            const uint8_t *rec = p.slots + (size_t)i * SLOT_BYTES;
            cint_state_t_str(t, "pool-slot ");
            cint_state_t_u64(t, p.id);
            cint_state_t_str(t, " ");
            cint_state_t_u64(t, i);
            cint_state_t_str(t, " generation ");
            cint_state_t_u64(t, le64(rec));
            cint_state_t_str(t, marks[rec[8]]);
        }
        if (p.top != 0u) {
            cint_state_t_str(t, "pool-free ");
            cint_state_t_u64(t, p.id);
            for (uint64_t top = p.top; top != 0u; top = le64(p.slots + (size_t)(top - 1u) * SLOT_BYTES + 9u)) {
                cint_state_t_str(t, " ");
                cint_state_t_u64(t, top - 1u);
            }
            cint_state_t_str(t, "\n");
        }
    }
}

size_t cint_checkpoint_render(const cint_checkpoint_info *c, char *out, size_t cap)
{
    cint_state_tx t;
    cint_checkpoint_global g;
    cint_checkpoint_buffer b;
    uint8_t hash[32];
    size_t cur = 0u;
    t.out = out;
    t.cap = out != NULL ? cap : 0u;
    t.len = 0u;
    if (c == NULL || c->state == NULL || c->contract.bytes == NULL) {
        return 0u;
    }
    cint_state_t_str(&t, "checkpoint cint-core-1/checkpoint/v1\ncontract ");
    cint_state_t_put(&t, c->contract.bytes, c->contract.len);
    cint_state_t_str(&t, "\n");
    t_digest(&t, "payload-digest ", c->payload_digest);
    t_digest(&t, "build-record-digest ", c->build_record_digest);
    t_digest(&t, "build-identity ", c->build_identity);
    cint_state_t_str(&t, "state cint-core-1/state/v1\nrevision ");
    cint_state_t_hex(&t, c->revision, 32u);
    cint_state_t_str(&t, "\nprofile cint-core-1\nfuel-model fuel-v1\n");
    while (cint_checkpoint_global_next(c, &cur, &g) == CINT_OK) {
        cint_state_t_str(&t, "global ");
        cint_state_t_put(&t, g.name.bytes, g.name.len);
        if (g.value.bytes[0] == TY_STRUCT) {  /* a struct: its type's name and the SHA-256 of its payload */
            cint_state_rd x = cint_state_rd_of(g.elements, (size_t)g.element_count);
            uint32_t ndef = 0u, npart = 0u;
            size_t name = 0u;
            (void)rd_type(&x, NULL, NULL, 4u, &ndef, &npart, &name);
            cint_state_t_str(&t, " ");
            t_type(&t, g.elements, (uint32_t)x.at, 4u);
            cint_sha256(g.elements + x.at, (size_t)g.element_count - x.at, hash);
            cint_state_t_str(&t, " payload-sha256 ");
            cint_state_t_hex(&t, hash, 32u);
            cint_state_t_str(&t, "\n");
        } else if (g.elements != NULL) {  /* an array: its type and the SHA-256 of its elements */
            cint_state_t_str(&t, " ");
            cint_state_t_str(&t, g.value.bytes[1] == CINT_TAG_BOOL ? "Bool" : cint_rt_type_name(g.value.bytes[1]));
            for (uint32_t d = 0u; d < g.value.bytes[2]; d++) {  /* the type as written: I64[2, 3] */
                cint_state_t_str(&t, d == 0u ? "[" : ", ");
                cint_state_t_u64(&t, le64(g.value.bytes + 3u + 8u * d));
            }
            cint_state_t_str(&t, "]");
            cint_sha256(g.elements, (size_t)(g.element_count * value_width(g.value.bytes[1])), hash);
            cint_state_t_str(&t, " elements-sha256 ");
            cint_state_t_hex(&t, hash, 32u);
            cint_state_t_str(&t, "\n");
        } else if (g.value.bytes[0] == CINT_TAG_HANDLE) {  /* Handle null, or its container and fields */
            const uint8_t *h = g.value.bytes + 1;
            cint_state_t_str(&t, h[28] == 0u ? " Handle null\n" : h[28] == CINT_HANDLE_ARENA ? " Handle arena " : " Handle pool ");
            if (h[28] != 0u) {
                cint_state_t_u64(&t, (uint64_t)h[24] | (uint64_t)h[25] << 8 | (uint64_t)h[26] << 16 | (uint64_t)h[27] << 24);
                cint_state_t_str(&t, " slot ");
                cint_state_t_u64(&t, le64(h));
                cint_state_t_str(&t, " length ");
                cint_state_t_i64(&t, cint_state_as_i64(le64(h + 8)));
                cint_state_t_str(&t, " generation ");
                cint_state_t_u64(&t, le64(h + 16));
                cint_state_t_str(&t, "\n");
            }
        } else {
            cint_state_t_value(&t, " ", &g.value);
        }
    }
    t_mem(&t, c);
    cur = 0u;
    while (cint_checkpoint_buffer_next(c, &cur, &b) == CINT_OK) {
        cint_state_t_str(&t, "buffer ");
        cint_state_t_u64(&t, b.id);
        cint_state_t_str(&t, " generation ");
        cint_state_t_u64(&t, b.generation);
        cint_state_t_str(&t, " type ");
        if (b.type.code == CINT_TAG_RECORD) {
            cint_state_t_str(&t, "record ");
            cint_state_t_u64(&t, b.type.record_id);
        } else {
            cint_state_t_str(&t, b.type.code == 0u              ? "bytes"
                                 : b.type.code == CINT_TAG_BOOL ? "Bool"
                                                                : cint_rt_type_name(b.type.code));
        }
        cint_state_t_str(&t, " extent ");
        cint_state_t_i64(&t, b.extent);
        cint_state_t_str(&t, b.owner == CINT_OWNER_BRIDGE ? " owner bridge" : " owner borrowed");
        cint_state_t_str(&t, b.ceiling == CINT_VIEW_WRITE ? " ceiling write" : " ceiling read");
        cint_state_t_str(&t, b.publish == CINT_PUBLISH_COPY ? " publish copy state live\n"
                                                            : " publish none state live\n");
    }
    cint_state_t_str(&t, "counters next-buffer ");
    cint_state_t_u64(&t, c->next_buffer);
    cint_state_t_str(&t, " next-arena ");
    cint_state_t_u64(&t, c->next_arena);
    cint_state_t_str(&t, " next-pool ");
    cint_state_t_u64(&t, c->next_pool);
    cint_state_t_str(&t, " entries ");
    cint_state_t_u64(&t, c->entries);
    cint_state_t_str(&t, "\n");
    cint_state_t_fault(&t, c->fault, c->fault_len);
    cint_state_t_str(&t, "effects ");
    cint_state_t_i64(&t, c->effects);
    cint_sha256(c->state, c->state_len, hash);
    cint_state_t_str(&t, "\nstate-hash ");
    cint_state_t_hex(&t, hash, 32u);
    cint_state_t_str(&t, "\n");
    return t.len;
}

int cint_checkpoint_show(const char *path)
{
    FILE *f = path != NULL ? fopen(path, "rb") : NULL;
    long size = f != NULL && fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1;
    uint8_t *data = size >= 0 && fseek(f, 0, SEEK_SET) == 0 ? (uint8_t *)malloc((size_t)size + 1u) : NULL;
    char *text = NULL;
    cint_checkpoint_info info;
    cint_recording_info rec;
    int st = data != NULL && fread(data, 1u, (size_t)size, f) == (size_t)size ? CINT_EXIT_OK : CINT_EXIT_ENVIRONMENT;
    bool ckpt = st == CINT_EXIT_OK && cint_checkpoint_decode(data, (size_t)size, &info) == CINT_OK;
    if (st == CINT_EXIT_OK && !ckpt && cint_recording_decode(data, (size_t)size, &rec) != CINT_OK) {
        st = CINT_EXIT_CHECK;
    } else if (st == CINT_EXIT_OK) {
        size_t n = ckpt ? cint_checkpoint_render(&info, NULL, 0u) : cint_recording_render(&rec, NULL, 0u);
#if defined(_WIN32)
        (void)_setmode(_fileno(stdout), _O_BINARY);  /* no CR */
#endif
        st = (text = (char *)malloc(n + 1u)) != NULL &&
                     (ckpt ? cint_checkpoint_render(&info, text, n) : cint_recording_render(&rec, text, n)) == n &&
                     fwrite(text, 1u, n, stdout) == n && fflush(stdout) == 0
                 ? CINT_EXIT_OK
                 : CINT_EXIT_ENVIRONMENT;
    }
    if (f != NULL) {
        fclose(f);
    }
    free(data);
    free(text);
    return st;
}
