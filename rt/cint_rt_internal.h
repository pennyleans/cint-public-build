/* cint_rt_internal.h: declarations shared by the files of the cint_rt library.
 *
 * Not part of cint-rt-3. Generated code and hosts include cint_rt.h only. The
 * context structure is defined here for cint_rt.c and rt/cint_state.c, the two
 * files that take the busy flag (SPEC-03 A-11). Other library files (the
 * bridge, Task 1.6, and the host-boundary conversions of SPEC-03 section 8)
 * build fault records through the functions below.
 */
#ifndef CINT_RT_INTERNAL_H
#define CINT_RT_INTERNAL_H

#include "cint_rt.h"

#include <signal.h>
#if defined(_MSC_VER)
#include <intrin.h>
typedef volatile long cint_rt_busy_flag;
#define CINT_RT_EXCHANGE _InterlockedExchange
#else
#include <stdatomic.h>
typedef atomic_int cint_rt_busy_flag;
#define CINT_RT_EXCHANGE atomic_exchange
#endif

/* One registration of the registry (SPEC-03 M-10, A-8, A-9). */
#define STORAGE_BORROWED 0u /* the host's memory */
#define STORAGE_CREATED 1u  /* cint_buffer_create: the runtime allocated it */
typedef struct buffer_slot {
    cint_buffer_id id;    /* 0: never used */
    uint64_t generation;  /* advanced by release */
    unsigned char *base;
    int64_t bytes;
    cint_type type;
    int64_t elem_bytes;
    int64_t extent;
    uint32_t leases;      /* live export leases (A-9) */
    uint8_t perm;         /* ceiling: CINT_VIEW_READ or CINT_VIEW_WRITE */
    uint8_t live;
    uint8_t typed;
    uint8_t storage;      /* STORAGE_* */
    uint8_t publish;      /* CINT_PUBLISH_*, for the staging of dispatch outputs (M-30a) */
    uint8_t device;       /* placement: 0 host, 1 device(1), CUDA memory (M-28a) */
} buffer_slot;

/* A live export lease: its number (0: free) and its buffer's slot. */
typedef struct lease_slot {
    uint64_t lease;
    uint32_t slot;
} lease_slot;

/* The header of one scratch allocation (cint_rt.h section 14): 32 bytes, so
 * the storage after it keeps the allocation's 16-byte alignment. */
typedef struct scratch_block {
    struct scratch_block *prev;  /* the allocation made before it */
    size_t bytes;                /* the whole allocation, header included */
    int64_t elements;            /* charged to the arena: 0 for a dispatch's staging (RT-OQ-34) */
    int64_t reserved;
} scratch_block;
_Static_assert(sizeof(scratch_block) == 32, "scratch_block");

/* One extern call at the host boundary (rt/cint_host.c; SPEC-03 A-15; RT-OQ-70), as the
 * recorder's or replay's hook sees it: the import row and its resolved function, the
 * arguments as the host receives them (an `out` view already staged) and the result record.
 * The hook makes the live call through cint_rt_host_live when it wants one and returns the
 * status that cint_rt_host_call treats as the host's. */
typedef struct cint_rt_host_frame {
    cint_site site;
    const cint_host_import *import;
    const cint_host_fn *fn;
    cint_value *args;
    uint32_t nargs;
    uint32_t error_count;
    cint_value *result;
} cint_rt_host_frame;

struct cint_ctx {
    cint_rt_head head;  /* first member: read by the inline helpers of cint_rt.h */
    cint_rt_busy_flag busy;
    const cint_program *program;
    cint_allocator allocator;
    int64_t config_depth;       /* cint_ctx_config.depth_limit, defaults applied */
    int64_t frame_arena_bytes;  /* capacity of the scratch arena: frame_arena_bytes / 8 elements */
    int64_t dispatches;         /* dispatch numbers given out in the current entry (SPEC-02 F-1) */
    uint32_t desc_count;        /* descriptors of the fault record (cint_rt_dispatch_alias) */
    cint_fault_descriptor desc[2];
    const cint_bind *host_binds;  /* a host-issued dispatch's binds (cint_rt_dispatch_host), or NULL */
    uint32_t host_count;        /* 0 once cint_rt_dispatch_bound has made checks 6 to 8 */
    scratch_block *scratch;     /* the newest scratch allocation */
    int64_t scratch_count;      /* live scratch allocations */
    int64_t scratch_used;       /* the elements they hold */
    int64_t frame_charged;      /* elements charged by cint_mem_frame_charge (rt/cint_mem.c) */
    cint_output_fn output;
    void *output_user;
    uint32_t refusal;           /* CINT_REFUSAL_* of the current or last entry */
    int64_t saved_fuel_limit;   /* the previous entry's fuel, restored on refusal */
    int64_t saved_fuel_used;
    const cint_module_info *module;  /* cint_ctx_config.module: record element sizes */
    const cint_error_set *error_set;  /* the error result of the current or last entry, or NULL */
    uint64_t error_tag;
    const cint_error_set *saved_error_set;  /* the previous entry's, restored on refusal */
    uint64_t saved_error_tag;
    cint_buffer_id last_id;
    buffer_slot *buffers;  /* buffer_cap slots, of which the first buffer_used were ever used */
    lease_slot *leases;    /* buffer_cap slots */
    uint32_t buffer_cap;
    uint32_t buffer_used;
    uint64_t last_lease;
    uint8_t *print_buf;  /* staging of the open print statement */
    size_t print_cap;
    size_t print_len;
    uint32_t print_open;
    cint_site print_site;
    cint_site stack[CINT_RT_MAX_DEPTH];  /* head.stack points here */
    cint_fault_record fault;
    void *device;  /* the device backend's state, or NULL (cint_rt_device) */
    const void *device_record;  /* config.devices[0] with device_count 1, or NULL (A-16) */
    cint_site bind_site;  /* of the last cint_rt_bind_views: check 4's site (cint_rt_dispatch_host) */
    uint64_t bind_marks;  /* parameters a function's entry takes in device(1) memory (cint_cuda_bind_finish) */
    struct mem_state *mem;  /* arenas and pools (rt/cint_mem.c), made at first use */
    const cint_state_table *states;  /* every module's cint_state, for rt/cint_state.c */
    uint64_t entries, last_arena, last_pool;  /* entries begun; identifiers issued (M-3, M-13) */
    int64_t effects;                  /* print statements written: the effect-log position */
    volatile sig_atomic_t interrupt;  /* cint_ctx_interrupt (rt/cint_state.h) */
    void (*boundary)(cint_ctx *ctx, int64_t number);  /* cint_rt_dispatch_end's: a recorder or replay */
    void *boundary_user;                              /* at dispatch density (rt/cint_state.c), or NULL */
    uint64_t *width;  /* the width report's rows (cint_rt_width_note), or NULL */
    /* The host table (cint_rt_host.h 6f; RT-OQ-70): the program's host import tables
     * (cint_ctx_config.imports), the function resolved for each of their rows, host_base[m]
     * rows before module m's and host_base[module_count] the row count, all NULL without a
     * descriptor; the grants; and the recorder's or replay's hook on each extern call, or NULL. */
    const cint_host_table *const *host_tables;  /* cint_ctx_config.imports->tables; NULL entries allowed */
    const cint_host_fn **host_fns;
    uint32_t *host_base;
    const uint8_t *grants;
    size_t grants_length;
    const uint8_t *args;    /* the program arguments (cint_ctx_config.args; RT-OQ-76), or NULL */
    size_t args_length;
    cint_status (*host_hook)(cint_ctx *ctx, cint_rt_host_frame *call);
    void *host_user;
    /* The standard service table (rt/cint_fs.c; RT-OQ-72): the fs grants opened from the
     * configuration's grant lines, or NULL, and the context's own functions for the service rows
     * it resolved, or NULL. */
    struct cint_rt_fs *fs;
    cint_host_fn *service_fns;
};

/* The busy flag of SPEC-03 A-11. */
static inline bool cint_rt_busy_take(cint_ctx *ctx) { return CINT_RT_EXCHANGE(&ctx->busy, 1) == 0; }
static inline void cint_rt_busy_give(cint_ctx *ctx) { (void)CINT_RT_EXCHANGE(&ctx->busy, 0); }
/* A host function's start: CINT_REFUSED for no context or `bad` arguments, else the flag taken. */
static inline cint_status cint_rt_host_take(cint_ctx *ctx, bool bad)
{
    return ctx == NULL || bad ? CINT_REFUSED : cint_rt_busy_take(ctx) ? CINT_OK : CINT_BUSY;
}

/* Hidden in a library build, as every declaration of cint_rt.h but its host
 * functions is (cint_rt.h, CINT_RT_LIBRARY). */
#if defined(CINT_RT_LIBRARY) && !defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
#pragma GCC visibility push(hidden)
#endif

/* A mathematical integer of magnitude below 2^128, as sign and magnitude. It
 * holds every exact result of the scalar operations of cint-rt-3: the largest
 * is (2^64 - 1)^2, the product of two U64 values. */
typedef struct cint_rt_z128 {
    uint64_t lo;
    uint64_t hi;
    bool negative;  /* never true for zero */
} cint_rt_z128;

cint_rt_z128 cint_rt_z_from_bits(uint32_t tag, uint64_t bits);
cint_rt_z128 cint_rt_z_add(cint_rt_z128 x, cint_rt_z128 y);
cint_rt_z128 cint_rt_z_neg(cint_rt_z128 x);
/* x * y for operands whose magnitudes are below 2^64. */
cint_rt_z128 cint_rt_z_mul(cint_rt_z128 x, cint_rt_z128 y);
/* x * 2^k for a magnitude below 2^64 and k in 0..63. */
cint_rt_z128 cint_rt_z_shl(cint_rt_z128 x, unsigned k);
int cint_rt_z_cmp(cint_rt_z128 x, cint_rt_z128 y);
/* MIN(T) and MAX(T) of a scalar type tag. */
cint_rt_z128 cint_rt_z_min(uint32_t tag);
cint_rt_z128 cint_rt_z_max(uint32_t tag);

/* Canonical tagged values (SPEC-01 11.1). */
void cint_rt_tvalue_scalar(cint_tvalue *v, uint32_t tag, uint64_t bits);
void cint_rt_tvalue_z(cint_tvalue *v, cint_rt_z128 z);

/* Starts the fault record at `site` with `code`: zero-fills it, records the
 * program, the revision, and the active-call stack, and marks the context
 * faulted. Returns NULL (writing nothing) when the context already holds a
 * fault record (SPEC-01 9.4). */
cint_fault_record *cint_rt_record_begin(cint_ctx *ctx, cint_site site, uint16_t code);
/* Appends dot-separated parts to the operation identifier (SPEC-01 9.8). */
void cint_rt_record_op(cint_fault_record *rec, const char *part);
void cint_rt_record_operand(cint_fault_record *rec, uint32_t tag, uint64_t bits);
/* Sets exact (when has_exact) and the limit of an out-of-range value: MAX(T)
 * when exact is above MAX(T), otherwise MIN(T) (SPEC-01 9.2). */
void cint_rt_record_range(cint_fault_record *rec, cint_rt_z128 exact, uint32_t tag);
/* A context's device backend slot (rt/cint_cuda_dispatch.c): NULL, or state whose first member
 * is the release function cint_ctx_destroy calls on it. */
void **cint_rt_device(cint_ctx *ctx);
/* The host table (rt/cint_host.c; RT-OQ-70): whether a host import descriptor is well formed
 * and the program's; the resolution of cint_ctx_create (SPEC-03 A-10), CINT_REFUSED for a
 * descriptor that is not, or a function that is missing or disagrees, CINT_RESOURCE when the
 * allocator fails; the release of what it made; and the live call of a hook. */
bool cint_rt_host_tables_valid(const cint_host_desc *d, const cint_program *p);
/* The standard service table (rt/cint_fs.c and rt/cint_console.c; RT-OQ-72, RT-OQ-73): one row
 * of a service, with its name `<service>/<version>.<name>`, its parameter count with the Grants
 * handle, the canonical type signature of its extern without the Grants parameter, and its
 * function; the grant lines parsed and the fs roots opened at creation; the capability bits
 * of the grant set that have no roots (`console.input`, `console.raw`); the function of a
 * service row; the console's release at destruction and at a fault; and their release. */
typedef struct cint_rt_service {
    const char *name;
    uint32_t param_count;
    const uint8_t *signature;
    size_t signature_len;
    cint_status (*call)(void *user, const cint_value *args, uint32_t nargs, cint_value *result);
} cint_rt_service;
#define CINT_RT_CAP_CONSOLE_INPUT 1u
#define CINT_RT_CAP_CONSOLE_RAW 2u
cint_status cint_rt_fs_open(cint_ctx *ctx, const cint_ctx_config *config);
void cint_rt_fs_close(cint_ctx *ctx);
uint32_t cint_rt_grant_caps(const cint_ctx *ctx);
const cint_host_fn *cint_rt_service_find(cint_ctx *ctx, const cint_host_import *r);
void cint_rt_service_release(cint_ctx *ctx);
void cint_rt_console_release(cint_ctx *ctx);
void cint_rt_console_fault(cint_ctx *ctx);
cint_status cint_rt_host_resolve(cint_ctx *ctx, const cint_ctx_config *config);
void cint_rt_host_release(cint_ctx *ctx);
cint_status cint_rt_host_live(const cint_rt_host_frame *c);
/* The width in bytes of a scalar tag, Bool included, or 0; whether `bits` is a value of it. */
size_t cint_rt_host_width(uint32_t tag);
bool cint_rt_host_fits(uint32_t tag, uint64_t bits);
/* Lowercase type name for operation identifiers ("i64"), uppercase for
 * rendering ("I64"), of I8 to U64, I128 to I1024 and U128 to U512; NULL for any other tag. */
const char *cint_rt_type_ident(uint32_t tag);
const char *cint_rt_type_name(uint32_t tag);
/* The element size of a registry type (A-13), or -1 (cint_rt.c). */
int64_t cint_rt_type_elem_bytes(const cint_ctx *ctx, const cint_type *t);
/* cint_program_run_fuel and cint_program_run_state with the width report (RT-OQ-64): a program
 * built with it names them through two macros (cint_wide.h section 2); hidden, so a library does not
 * export them. */
int cint_program_run_width(const cint_program *program, cint_main_fn main_fn, int64_t fuel, const char *fault_dest,
                           const char *fuel_dest);
int cint_program_run_width_state(const cint_program *program, cint_main_fn main_fn, int64_t fuel,
                                 const char *fault_dest, const char *fuel_dest, const char *state_dest,
                                 const cint_state_table *state);
/* cint_program_run_args with the width report (RT-OQ-74): the macro of a generated main. */
int cint_program_run_width_argv(const cint_program *program, cint_main_fn main_fn, int64_t fuel, int argc,
                                char **argv, const cint_host_desc *imports, const cint_state_table *state,
                                uint32_t flags);
int cint_program_run_width_args(const cint_program *program, cint_main_fn main_fn, int64_t fuel, int argc,
                                char **argv, const cint_host_desc *imports, const cint_state_table *state);

#if defined(CINT_RT_LIBRARY) && !defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
#pragma GCC visibility pop
#endif

#endif /* CINT_RT_INTERNAL_H */
