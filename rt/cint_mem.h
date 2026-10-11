/* cint_mem.h: arenas, pools and handles for generated code (SPEC-03 M-12 to M-23; SPEC-04 LS-145; ref/OPEN.md
 * REF-OQ-45; box 12; decision 2026-10-06 on OQ-212).
 *
 * Not a host interface. A module that declares an arena or a pool, or charges a local array to the frame
 * arena, includes this header after cint_rt.h. The functions are in rt/cint_mem.c, which rt/cint_rt.c
 * includes, so every build of the cint_rt library carries them with no other source file; the arenas and
 * pools of a context hang from it and are freed by cint_ctx_destroy.
 *
 * The context creates a program's module-level arenas and pools at the first call that names one, from the
 * table the module passes: identifiers from 1 per kind in declaration order (M-13). A context keeps one
 * table; B1 gives arenas and pools to the root module only (compiler/OPEN.md CINTC-OQ-64). A restore replaces
 * them, and the next call compares its table with what came back; a change of revision carries those that
 * the new table declares unchanged (rt/cint_state.c; rt/OPEN.md RT-OQ-46). An
 * arena is bump storage of one element type with a generation; a child is carved from its parent's used
 * extent and takes the next arena identifier; a reset advances the generation, empties the arena and retires
 * every descendant (M-14, M-16, M-20). A pool has a generation per slot and a last-in, first-out free list
 * (M-15, M-16, M-18).
 *
 * A handle is checked where it is used (M-17): the first reason that applies of null 1, container 6 (another
 * arena or pool, or another kind), retired 2, generation 3, extent 4 and (a pool) vacant 5. A failure records
 * E_STALE_HANDLE with the operation, the handle's five fields (U64 slot, I64 length, U64 generation, U32
 * container, U8 kind), the current generation (U64) and the reason (U8), and no exact or limit. A member of a
 * retired arena records the arena's own fields (slot 0, length its capacity, its generation, its identifier,
 * kind 1) with reason 2, operation arena.reset or arena.alloc. An allocation past the capacity, or of a
 * negative count, records E_BOUNDS arena.alloc (I64 count, used, capacity); an insertion into a full pool
 * E_BOUNDS pool.insert (I64 occupied, retired, capacity). The `_result` forms write the AllocError tag 1
 * (`full`) to *tag instead, and 0 on success. A storage request the allocator refuses records E_UNSUPPORTED
 * host.resource. Every function returns false, with the context faulted, on failure, and false with nothing
 * done on a faulted context.
 */
#ifndef CINT_MEM_H
#define CINT_MEM_H

#include "cint_rt.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CINT_RT_LIBRARY) && !defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
#pragma GCC visibility push(hidden)
#endif

/* A handle (M-12). All fields 0: the null handle. */
#define CINT_HANDLE_ARENA 1u
#define CINT_HANDLE_POOL 2u
typedef struct cint_handle {
    uint64_t slot;        /* the allocation's first element, or the pool's slot */
    int64_t length;       /* elements; 1 for a pool slot */
    uint64_t generation;  /* of the arena, or of the slot, when it was made */
    uint32_t container;   /* the arena's or the pool's identifier */
    uint8_t kind;         /* CINT_HANDLE_* */
    uint8_t reserved[3];  /* 0 */
} cint_handle;
_Static_assert(sizeof(cint_handle) == 32, "cint_handle");

/* A module's arenas and pools in declaration order: the kind, the size of an element as the module stores it
 * (1 or more), the capacity in elements (0 or more), and the element type (decision 2026-10-08 on OQ-244). The
 * type is written as a checkpoint writes it after the element's width (rt/cint_state.c): a count of struct
 * definitions, the definitions (SPEC-03 A-21), then the type. `layout` gives, for each definition in that
 * order, its stored size and then each field's offset, in bytes. A scalar is stored at its own width, or in 8
 * bytes, sign- or zero-extended, when `cell` is 8; a handle as its cint_handle. The name (UTF-8, none when
 * name_len is 0) and the module's index in the program table are what a change of revision matches. */
typedef struct cint_mem_decl {
    uint32_t kind;  /* CINT_HANDLE_* */
    uint32_t elem_bytes;
    int64_t capacity;
    const uint8_t *type;
    const uint32_t *layout;  /* NULL when layout_len is 0: a scalar or handle element */
    uint32_t type_len;
    uint32_t layout_len;
    uint32_t cell;           /* 0 or 8 */
    uint32_t name_len;
    const char *name;        /* not NUL-terminated */
} cint_mem_decl;
_Static_assert(sizeof(cint_mem_decl) == 56, "cint_mem_decl");

typedef struct cint_mem_table {
    uint32_t count;
    uint32_t module;  /* the declaring module's index in the program table */
    const cint_mem_decl *decls;
} cint_mem_table;
_Static_assert(sizeof(cint_mem_table) == 16, "cint_mem_table");

/* The lease of a view of an arena allocation (M-23): the arena and the handle the view was made from, checked
 * again at each element access. */
typedef struct cint_lease {
    uint32_t arena;
    uint32_t reserved;  /* 0 */
    cint_handle handle;
} cint_lease;
_Static_assert(sizeof(cint_lease) == 40, "cint_lease");

/* `a.alloc(n)`, or `a.alloc_result(n)` when `result`: n zero-filled elements. */
bool cint_mem_alloc(cint_ctx *ctx, cint_site site, const cint_mem_table *t, uint32_t arena, int64_t n, bool result,
                    cint_handle *out, uint16_t *tag);
/* `a.child(n)`: n elements of `arena` as a new arena, whose identifier is written. */
bool cint_mem_child(cint_ctx *ctx, cint_site site, const cint_mem_table *t, uint32_t arena, int64_t n,
                    uint32_t *out);
/* `a.reset()`. */
bool cint_mem_reset(cint_ctx *ctx, cint_site site, const cint_mem_table *t, uint32_t arena);
/* `a[h]`: handle.check, then the rank-1 view of the allocation and its lease. Two views of one top-level
 * arena's storage have one `base`. */
bool cint_mem_view(cint_ctx *ctx, cint_site site, const cint_mem_table *t, uint32_t arena, cint_handle h,
                   cint_vdesc *v, cint_lease *lease);
/* An element access through a leased view: handle.check of its lease. */
bool cint_mem_lease(cint_ctx *ctx, cint_site site, const cint_mem_table *t, const cint_lease *lease);
/* `p.insert(v)`, or `p.insert_result(v)` when `result`: the element's bytes are copied. */
bool cint_mem_insert(cint_ctx *ctx, cint_site site, const cint_mem_table *t, uint32_t pool, const void *value,
                     bool result, cint_handle *out, uint16_t *tag);
/* `p.remove(h)`: checked as pool.deref, then the slot is vacated. */
bool cint_mem_remove(cint_ctx *ctx, cint_site site, const cint_mem_table *t, uint32_t pool, cint_handle h);
/* `p[h]`: pool.deref, then the address of the slot's element; NULL on failure. */
void *cint_mem_deref(cint_ctx *ctx, cint_site site, const cint_mem_table *t, uint32_t pool, cint_handle h);

#if defined(CINT_MEM_STATE)
/* A context's arenas and pools, for rt/cint_mem.c and rt/cint_state.c, which writes them into canonical state
 * and restores them (SPEC-03 M-3; rt/OPEN.md RT-OQ-46). Arenas are in ascending identifier order, the
 * module-level ones 1 to n first; pool k has identifier k + 1. A restored state holds each container's used
 * extent (`room`) until a table that it fits is named, which gives each its capacity. */
#define MEM_GEN_RETIRED UINT64_MAX  /* M-16: no generation reaches 2^64 - 1 */
#define MEM_OCCUPIED 1u
#define MEM_RETIRED 2u

typedef struct mem_arena {
    uint32_t id;
    uint32_t root;               /* the index of the top-level arena whose storage it shares */
    uint32_t parent;             /* the index of its parent plus 1; 0 for a top-level arena */
    int64_t offset;              /* its element 0 in the root's storage */
    int64_t capacity;
    int64_t used;
    uint64_t generation;
    uint64_t parent_generation;  /* the parent's generation when it was carved */
    unsigned char *storage;      /* a top-level arena's elements; NULL for a child or capacity 0 */
    int64_t room;                /* the elements of storage */
    uint32_t elem_bytes;
    uint32_t type_len;
    const uint8_t *type;         /* a restored module-level arena's width and type until laid out */
} mem_arena;

typedef struct mem_pool {
    unsigned char *storage;
    uint64_t *gens;
    uint8_t *marks;              /* MEM_OCCUPIED, MEM_RETIRED */
    int64_t *free_list;          /* last in, first out */
    int64_t free_count;
    int64_t fresh;               /* the next slot never used */
    int64_t occupied;
    int64_t retired;
    int64_t capacity;
    int64_t room;                /* the slots of each array */
    uint32_t elem_bytes;
    uint32_t type_len;
    const uint8_t *type;         /* a restored pool's width and type until laid out */
} mem_pool;

struct mem_state {
    const cint_mem_table *table;  /* NULL after a restore, until a call names one that fits */
    mem_arena *arenas;
    uint32_t arena_count;
    uint32_t arena_cap;
    mem_pool *pools;
    uint32_t pool_count;
    uint32_t pool_cap;
    /* A restored state holds each element as the checkpoint wrote it, and each type in `held`, until
     * lay_out (rt/cint_state.c) gives them the layout of a table; NULL once it has. */
    cint_status (*lay_out)(cint_ctx *ctx, struct mem_state *s, const cint_mem_table *t);
    uint8_t *held;
    size_t held_len;
    const cint_mem_table *laid;   /* the table whose layout the elements have; NULL while held */
};

/* Zero-filled storage from the context's allocator, as each container's is (16-byte aligned), and the release
 * of a whole state, its arrays and its containers' storage. */
void *cint_mem_take(cint_ctx *ctx, size_t bytes);
void cint_mem_state_free(cint_ctx *ctx, struct mem_state *s);
/* Whether table t is well formed, with its arena and pool counts. */
bool cint_mem_table_ok(const cint_mem_table *t, uint32_t *arenas, uint32_t *pools);
/* The index of arena `id` in s, or UINT32_MAX when it has none. */
uint32_t cint_mem_row(const struct mem_state *s, uint32_t id);
/* Gives restored state s table t: CINT_REFUSED unless s holds exactly the module-level arenas and pools that
 * t declares, in order, with their element types and capacities, then its children, and every element is
 * one of its type; CINT_RESOURCE, with s unchanged but for containers grown, when storage runs out. */
cint_status cint_mem_adopt(cint_ctx *ctx, struct mem_state *s, const cint_mem_table *t);
#endif

/* The frame arena (SPEC-04 LS-110; decision 2026-10-06 on OQ-213). A local array whose storage is the
 * generated code's own is charged its element count n; run-time storage from cint_rt_scratch counts toward
 * the same capacity, frame_arena_bytes / 8 elements. A charge past it, or of n below 0, records E_BOUNDS
 * arena.alloc (I64 n, used, capacity). cint_mem_frame_release gives back every charge made after
 * cint_mem_frame_mark returned `mark`; each entry starts with none. */
bool cint_mem_frame_charge(cint_ctx *ctx, cint_site site, int64_t n);
int64_t cint_mem_frame_mark(const cint_ctx *ctx);
void cint_mem_frame_release(cint_ctx *ctx, int64_t mark);

#if defined(CINT_RT_LIBRARY) && !defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
#pragma GCC visibility pop
#endif

#ifdef __cplusplus
}
#endif

#endif /* CINT_MEM_H */
