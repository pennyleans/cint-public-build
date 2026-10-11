/* cint_rt_host.h: the host side of the cint-rt-3 runtime contract (normative).
 *
 * rt/cint_rt.h is split by audience (decision 2026-10-08 on OQ-229). It keeps
 * what generated code calls and reads inside its functions: the helpers, the
 * out-of-line cint_rt_* functions, sites, the fault record, views, the entry
 * checks of a wrapper, print output, error results, module state and the
 * context head. This header holds what a host uses: contexts and their
 * configuration, every host function (CINT_RT_API), the buffer registry, the
 * observer and reflection tables a host reads, and program processes.
 * Generated code still names some of these (it fills the observer and
 * reflection tables, and its C main calls cint_program_run); it reaches them
 * through rt/cint_rt.h, which includes this header at its end, inside its
 * extern "C" block and its library visibility. Either header may be included
 * first. Sections keep the numbers they had in rt/cint_rt.h, so a reference
 * such as "cint_rt.h 6d" finds its text here. No declaration changed when it
 * moved.
 */
#ifndef CINT_RT_H
#include "cint_rt.h"  /* which includes this header at its end */
#elif !defined(CINT_RT_HOST_H)
#define CINT_RT_HOST_H

/* ------------------------------------------------------------------------- */
/* 6. Contexts (SPEC-03 A-2, A-4, A-7, A-7a, A-11): the host side. The entry  */
/* and call functions of generated code are in cint_rt.h, section 6.          */

typedef struct cint_allocator {
    void *(*alloc)(void *user, size_t bytes, size_t align);
    void (*release)(void *user, void *ptr, size_t bytes, size_t align);
    void *user;
} cint_allocator;
_Static_assert(sizeof(cint_allocator) == 24, "cint_allocator");

/* Receives the bytes of one print statement (SPEC-04 LS-193), whole: the
 * runtime stages a statement and calls this once, after its last hole, so a
 * statement whose hole faults writes nothing (LS-197). `bytes` is never NULL.
 * A status other than CINT_OK faults the entry (6c). */
typedef cint_status (*cint_output_fn)(void *user, const uint8_t *bytes, size_t len);

/* The defaults of SPEC-06 1.4 for the C ABI. */
#define CINT_DEFAULT_DEPTH ((int64_t)256)
#define CINT_DEFAULT_FRAME_ARENA_BYTES ((int64_t)16777216)

/* `size` must be sizeof(cint_ctx_config). `max_buffers` is the number of
 * live registrations the context holds (SPEC-03 5.3); 0 selects
 * CINT_RT_MAX_BUFFERS (6a). `program` is required. An allocator with `alloc`
 * NULL means the C library's malloc and free. `depth_limit` is the D of every
 * entry that takes its limit from the context (CINT_DEPTH_FROM_CONFIG, 6;
 * SPEC-01 IM-119); 0 selects CINT_DEFAULT_DEPTH. `frame_arena_bytes` is the
 * frame-arena capacity (SPEC-04 LS-110), recorded with the context; 0 selects
 * the default. With `output` NULL, print statements are staged and their
 * bytes discarded. `module`, when not NULL, is the descriptor of the program's
 * root module, whose record layout table gives the size of a record element
 * type in cint_buffer_register and cint_buffer_create (6a); its `program` must
 * be `program`. */
typedef struct cint_ctx_config {
    uint32_t size;
    uint32_t max_buffers;
    const cint_program *program;
    cint_allocator allocator;
    int64_t depth_limit;
    int64_t frame_arena_bytes;
    cint_output_fn output;
    void *output_user;
    const cint_module_info *module;
    uint32_t device_count, reserved;  /* 0, or 1 built with CINT_GPU_CUDA (SPEC-03 A-16a); 0 */
    const void *const *devices;       /* devices[k-1] backs device(k): a cint_cuda_device (cint_cuda.h) */
} cint_ctx_config;
_Static_assert(sizeof(cint_ctx_config) == 96 && offsetof(cint_ctx_config, depth_limit) == 40 &&
                   offsetof(cint_ctx_config, module) == 72 && offsetof(cint_ctx_config, devices) == 88,
               "cint_ctx_config");

/* Host functions. Each that can fail returns a status and writes its out
 * parameters only on CINT_OK (SPEC-03 A-4); each that touches a context takes
 * its busy flag and returns CINT_BUSY when it is held (A-11). */
CINT_RT_API uint32_t cint_abi_version(void);
/* CINT_REFUSED for an invalid configuration (a NULL argument, a wrong size,
 * no program or an invalid one, an allocator with `alloc` but no `release`, a
 * negative depth limit or frame-arena capacity, a module of another ABI or
 * program); CINT_RESOURCE when allocation fails; otherwise CINT_OK and *out. */
CINT_RT_API cint_status cint_ctx_create(const cint_ctx_config *config, cint_ctx **out);
CINT_RT_API void cint_ctx_destroy(cint_ctx *ctx);
/* Copies the context's fault record into *out and returns CINT_OK; out->code
 * is 0 when the context holds none. CINT_BUSY during an entry (A-11). */
CINT_RT_API cint_status cint_ctx_fault(const cint_ctx *ctx, cint_fault_record *out);
/* Clears the fault record and alters no other state: registrations, buffers
 * and counters stay as they are (A-7a). CINT_BUSY during an entry, CINT_REFUSED
 * for a NULL context, otherwise CINT_OK. */
CINT_RT_API cint_status cint_ctx_clear_fault(cint_ctx *ctx);
/* Fuel consumed by the last entry (A-17). CINT_BUSY during an entry. A
 * refused call (6a) is not an entry and leaves the value as it was. */
CINT_RT_API cint_status cint_fuel_consumed(const cint_ctx *ctx, int64_t *out);
/* Why the last call that ended CINT_REFUSED through cint_view_bind was
 * refused: one of CINT_REFUSAL_* (6a), or CINT_REFUSAL_NONE when the last
 * entry was not refused. CINT_BUSY during an entry. SPEC-03 cint_refusal_get,
 * with its canonical detail bytes, arrives with the full registry at T3. */
CINT_RT_API cint_status cint_ctx_refusal(const cint_ctx *ctx, uint32_t *reason);

/* Canonical bytes of a fault record (SPEC-01 11.2 layout of the 9.2 content).
 * Returns the length of the encoding and writes it to buf only when cap is at
 * least that length (buf may be NULL to query). Returns 0 when the record
 * cannot be encoded: code 0, a site that does not resolve, or a field out of
 * its bounds. */
CINT_RT_API size_t cint_fault_encode(const cint_fault_record *record, uint8_t *buf, size_t cap);
/* The canonical bytes of the context's fault record (SPEC-03 5.3, A-17;
 * SPEC-01 IM-149): sets *len to their length, 0 when the context holds no
 * fault, and copies min(cap, *len) bytes to buf (NULL when cap is 0, to
 * query). CINT_REFUSED for a NULL context or len, or a NULL buf with cap
 * above 0; CINT_BUSY during an entry; CINT_RESOURCE when a partial copy needs
 * storage the allocator cannot supply; otherwise CINT_OK. */
CINT_RT_API cint_status cint_fault_get(const cint_ctx *ctx, uint8_t *buf, size_t cap, size_t *len);

/* Resolves a site through a program's tables. False when it does not resolve. */
bool cint_site_resolve(const cint_program *program, cint_site site, cint_position *out);

/* Renders a tagged value as "<Type> <decimal>" (Z as "Z <decimal>", Bool as
 * "Bool true", fixed point as "Q16.16 raw 98304"), or only the decimal ("raw
 * 98304" for fixed point). Returns the length without the NUL, or 0 for an
 * invalid value; writes at most cap bytes including a NUL. */
size_t cint_tvalue_render(const cint_tvalue *value, char *buf, size_t cap);
size_t cint_tvalue_render_decimal(const cint_tvalue *value, char *buf, size_t cap);

/* The SHA-256 digest (FIPS 180-4) of n bytes at p (p may be NULL when n is 0):
 * the digest of the bridge's MANIFEST (SPEC-09 CINTC-06) and of receipts. */
void cint_sha256(const uint8_t *p, size_t n, uint8_t digest[32]);

/* ------------------------------------------------------------------------- */
/* 6a. The buffer registry (SPEC-03 5.3, M-10, A-8, A-8a, A-9, A-12, H-12;    */
/* rt/OPEN.md RT-OQ-24 and RT-OQ-39). It registers host memory and, with      */
/* CINT_GPU_CUDA, device(1) CUDA memory (M-28a; cint_cuda.h). View binding    */
/* (cint_view_bind) and the CINT_REFUSAL_* reasons are in cint_rt.h, 6a.      */
/*                                                                            */
/* A context holds config.max_buffers live registrations (CINT_RT_MAX_BUFFERS */
/* when 0). Ids count from 1 and are never reused within a context, and every */
/* registration starts at generation CINT_BUFFER_GENERATION_FIRST, which      */
/* cint_buffer_release advances. Registrations may overlap; overlap is        */
/* decided when views are bound (H-13). Every registry function returns       */
/* CINT_BUSY during an entry (A-11), CINT_RESOURCE when the registry or the   */
/* allocator has no room, and CINT_REFUSED for arguments it cannot take; all  */
/* but the byte and element forms then leave the reason (CINT_REFUSAL_*) for  */
/* cint_ctx_refusal, which an entry that is not refused resets.               */
/*                                                                            */
/* cint_buffer_register validates a descriptor (A-8), in this order: `size`   */
/* is sizeof(cint_buffer_desc), `writable` is 0 or 1, the reserved fields are */
/* 0, and `publish` is CINT_PUBLISH_NONE or, for a writable buffer,           */
/* CINT_PUBLISH_COPY (CALL); the memory is host memory (device 0) or, on a    */
/* device context, I8 to U64 CUDA memory (device 1), with no synchronization  */
/* (UNSUPPORTED); the type is Bool, I8 to U64, or a record of config.module's */
/* layout table (TYPE); the extent is at least 0 (EXTENT) and its byte size   */
/* fits an I64 and the address space (SIZE); the address is not NULL unless   */
/* that size is 0, and is aligned to the largest power of two dividing the    */
/* element size, at most 8 (ALIGN). It writes the id and the generation. The  */
/* registration is typed: a view of it must have its type. A borrowed         */
/* registration lasts until cint_buffer_release, and the runtime never frees  */
/* its memory. `publish` is recorded for staging dispatch outputs (M-30a).    */
/*                                                                            */
/* cint_buffer_register_bytes records `bytes` bytes at `base` (NULL only when */
/* bytes is 0) with ceiling `perm` (CINT_VIEW_READ or CINT_VIEW_WRITE),       */
/* untyped, and writes a new id: the byte form of T2, which the seed's        */
/* observer path and the test hosts use. cint_buffer_register_elements        */
/* records a Bool, I8..U64 or nominal record type, its logical byte size, and */
/* an extent in 0..INT64_MAX; reserved fields, storage and frac_bits must be  */
/* zero, and record_id is the nominal identity only for records. Scalars must */
/* have their exact byte size; the byte product must fit an I64. Records may  */
/* have size zero: such a registration uses no storage, ignores base          */
/* (recorded as NULL), and has its own fresh logical allocation id even when  */
/* a base repeats. Aliases reuse that id and generation. Byte overlap is kept */
/* across all registrations of positive-size elements.                        */
/*                                                                            */
/* cint_buffer_create allocates a zero-filled runtime-owned buffer of         */
/* `extent` elements of `type` through the context's allocator, writable,     */
/* with no device (A-9: storage of the `bridge` kind, the C form of           */
/* cint.empty), and writes its id and generation. cint_buffer_release ends a  */
/* registration and advances its generation, so a view that names it is       */
/* refused from then on, and frees the storage of a created buffer;           */
/* CINT_REFUSED for an id that is not live, CINT_BUSY while the buffer has a  */
/* live lease. cint_ctx_clear_fault releases nothing (A-7a), and              */
/* cint_ctx_destroy frees every created buffer.                               */
/*                                                                            */
/* cint_buffer_read copies the elements of a view of a live registration, at  */
/* its generation, in row-major order of the view's index tuples, into `dst`, */
/* which holds exactly their bytes (SIZE otherwise); the view is checked as a */
/* binding checks it, and any permission reads host memory (UNSUPPORTED for   */
/* device(1) memory, M-28). cint_buffer_lease gives a read-only address of a  */
/* live created buffer's storage (NULL for no storage) and a lease number,    */
/* from 1, never reused; the address stays valid until cint_lease_end of that */
/* number. A borrowed registration or an unknown lease number is refused      */
/* (LEASE). While a buffer has a live lease, an entry that binds it to an out */
/* or inout parameter returns CINT_BUSY, nothing recorded and fuel as it was  */
/* (A-9); writing through a leased address is outside every guarantee.        */

#define CINT_RT_MAX_BUFFERS 256u
#define CINT_BUFFER_GENERATION_FIRST ((uint64_t)1)

/* Backend memory references (SPEC-03 A-8) and producer synchronization. */
#define CINT_MEM_HOST 1u
#define CINT_MEM_CUDA 2u  /* also HIP */
#define CINT_MEM_VULKAN 3u
#define CINT_MEM_METAL 4u
#define CINT_MEM_WEBGPU 5u
typedef struct cint_mem {
    uint32_t kind;      /* CINT_MEM_* */
    uint32_t reserved;  /* 0 */
    union {
        struct { void *ptr; } host;              /* the lowest reachable byte */
        struct { uint64_t device_ptr; } cuda;    /* CUdeviceptr or hipDeviceptr_t */
        struct { uint64_t buffer, buffer_offset, memory, memory_offset; } vulkan;
        struct { void *buffer; uint64_t offset; void *heap; } metal;  /* heap NULL: not from a heap */
        struct { void *buffer; uint64_t offset; } webgpu;
    } u;
} cint_mem;
_Static_assert(sizeof(cint_mem) == 40 && offsetof(cint_mem, u) == 8, "cint_mem");

typedef struct cint_sync {
    uint32_t kind;      /* CINT_MEM_* of the device, or 0: none */
    uint32_t reserved;  /* 0 */
    union {
        struct { void *stream; } cuda;
        struct { uint64_t semaphore, wait_value, signal_value; } vulkan;  /* a timeline VkSemaphore */
        struct { void *shared_event; uint64_t wait_value, signal_value; } metal;
        struct { void *queue; } webgpu;
    } u;
} cint_sync;
_Static_assert(sizeof(cint_sync) == 32 && offsetof(cint_sync, u) == 8, "cint_sync");

#define CINT_PUBLISH_NONE 0u
#define CINT_PUBLISH_COPY 1u
typedef struct cint_buffer_desc {
    uint32_t size;      /* sizeof(cint_buffer_desc) */
    uint32_t writable;  /* the ceiling: 0 read, 1 write */
    cint_type type;
    cint_mem mem;       /* the lowest reachable element (A-8a) */
    int64_t extent;     /* elements */
    uint32_t device;    /* 0: host; k >= 1: placement device(k) */
    uint32_t publish;   /* CINT_PUBLISH_*; NONE unless writable */
    cint_sync sync;     /* producer synchronization of device memory, or kind 0 */
} cint_buffer_desc;
_Static_assert(sizeof(cint_buffer_desc) == 112 && offsetof(cint_buffer_desc, mem) == 24 &&
                   offsetof(cint_buffer_desc, extent) == 64 && offsetof(cint_buffer_desc, sync) == 80,
               "cint_buffer_desc");

CINT_RT_API cint_status cint_buffer_register(cint_ctx *ctx, const cint_buffer_desc *desc, cint_buffer_id *out_id,
                                             uint64_t *out_generation);
CINT_RT_API cint_status cint_buffer_register_bytes(cint_ctx *ctx, void *base, int64_t bytes, uint8_t perm,
                                                   cint_buffer_id *out);
CINT_RT_API cint_status cint_buffer_register_elements(cint_ctx *ctx, void *base, const cint_type *type,
                                                      int64_t elem_bytes, int64_t extent, uint8_t perm,
                                                      cint_buffer_id *out);
CINT_RT_API cint_status cint_buffer_create(cint_ctx *ctx, cint_type type, int64_t extent, uint32_t device,
                                           cint_buffer_id *out_id, uint64_t *out_generation);
CINT_RT_API cint_status cint_buffer_release(cint_ctx *ctx, cint_buffer_id id);
CINT_RT_API cint_status cint_buffer_read(cint_ctx *ctx, cint_view view, void *dst, size_t dst_bytes);
CINT_RT_API cint_status cint_buffer_lease(cint_ctx *ctx, cint_buffer_id id, const void **out_ptr,
                                          uint64_t *out_lease);
CINT_RT_API cint_status cint_lease_end(cint_ctx *ctx, uint64_t lease);

/* ------------------------------------------------------------------------- */
/* 6b. Observer interface cint-observe-1 (SPEC-09 CONF-13, X-2 to X-4); not   */
/* the public C ABI. A program built for cint-harness exports                 */
/* `const uint32_t cint_program_abi` and `const cint_observer cint_observer_desc`. */

typedef cint_status (*cint_observer_fn)(cint_ctx *ctx, int64_t fuel, int64_t depth,
                                        const uint64_t *args, uint64_t *result);
typedef struct cint_observer_entry {
    uint32_t module;         /* index into program->modules */
    uint32_t name_len;       /* 1 to 255 */
    const char *name;        /* CINT identifier, not NUL-terminated */
    const uint32_t *types;   /* type signature {R, n, T1..Tn}, n <= 16; R 0: none */
    cint_observer_fn call;
} cint_observer_entry;
_Static_assert(sizeof(cint_observer_entry) == 32, "cint_observer_entry");

typedef struct cint_observer {
    uint32_t entry_count;
    uint32_t reserved;  /* 0 */
    const cint_program *program;
    const cint_observer_entry *entries;  /* module order, then declaration order */
} cint_observer;
_Static_assert(sizeof(cint_observer) == 24, "cint_observer");

/* ------------------------------------------------------------------------- */
/* 6b''. The reflection table of cint-abi-1 (SPEC-03 5.6, A-19 to A-26; box   */
/* 10 defaults BX10-23 to BX10-28, and the ruling on OQ-202), a side table:   */
/* cint_module_info and cint_record_layout keep their layouts. The            */
/* program-level file that cintc writes exports `const cint_library           */
/* cint_library_desc`, which holds a pointer to each module's table in the    */
/* order of cint_program.modules. A module's table holds one cint_export row  */
/* per cx wrapper of the module, in declaration order, and the qualified name */
/* of each row of its record layout table. A row's type_signature is the      */
/* canonical type signature of A-20, which also gives the prototype a host    */
/* casts `call` to (A-12, A-13). The seed writes no side table.               */

#define CINT_PURE 1u
#define CINT_OBSERVE 2u
#define CINT_UNRESTRICTED 3u
#define CINT_EXPORT_FUNCTION 1u
#define CINT_EXPORT_KERNEL 2u

typedef struct cint_name {
    const char *bytes;  /* UTF-8, not NUL-terminated */
    uint32_t len;       /* at least 1 */
    uint32_t reserved;  /* 0 */
} cint_name;
_Static_assert(sizeof(cint_name) == 16 && offsetof(cint_name, len) == 8, "cint_name");

typedef void (*cint_entry_fn)(void);

typedef struct cint_export {
    cint_name name;
    uint32_t kind;                    /* CINT_EXPORT_* */
    uint32_t effect;                  /* CINT_PURE, CINT_OBSERVE, or CINT_UNRESTRICTED */
    uint32_t size_count;              /* size parameters or shape symbols */
    uint32_t param_count;
    const cint_name *size_names;      /* declaration order; NULL when size_count is 0 */
    const cint_name *param_names;     /* declaration order; NULL when param_count is 0 */
    const uint8_t *type_signature;    /* SPEC-03 A-20 */
    uint64_t type_signature_len;
    cint_entry_fn call;               /* the cx wrapper */
} cint_export;
_Static_assert(sizeof(cint_export) == 72 && offsetof(cint_export, size_names) == 32 &&
               offsetof(cint_export, type_signature_len) == 56 && offsetof(cint_export, call) == 64, "cint_export");

typedef struct cint_module_table {
    const cint_module_info *info;     /* the module's cm descriptor */
    uint32_t module;                  /* its index in info->program->modules */
    uint32_t export_count;
    const cint_export *exports;       /* NULL when export_count is 0 */
    const cint_name *record_names;    /* row i of info->records; NULL when it has none */
} cint_module_table;
_Static_assert(sizeof(cint_module_table) == 32 && offsetof(cint_module_table, exports) == 16 &&
               offsetof(cint_module_table, record_names) == 24, "cint_module_table");

typedef struct cint_library {
    uint32_t abi;                     /* CINT_ABI_VERSION */
    uint32_t module_count;            /* the program's module count */
    const cint_module_table *const *modules;
} cint_library;
_Static_assert(sizeof(cint_library) == 16 && offsetof(cint_library, modules) == 8, "cint_library");

/* ------------------------------------------------------------------------- */
/* 6c'. Error results: the host side (SPEC-03 A-18). The error sets and       */
/* cint_rt_error_result are in cint_rt.h, 6c'.                                */
/*                                                                            */
/* cint_ctx_error reads the last entry's error result: CINT_REFUSED for a     */
/* NULL argument, CINT_BUSY during an entry, otherwise CINT_OK with the set   */
/* and the tag, or with *set NULL and *tag 0 when the last entry left no      */
/* error or faulted. cint_ctx_clear_fault does not change it.                 */
CINT_RT_API cint_status cint_ctx_error(const cint_ctx *ctx, const cint_error_set **set, uint64_t *tag);

/* ------------------------------------------------------------------------- */
/* 6d. Program processes (SPEC-06 3.2, 3.4a; slice 2 decision patch D-19).    */
/*                                                                            */
/* cint_program_run is the body of a generated program's C main. It puts      */
/* stdout in binary mode on Windows (LF is never written as CR LF, LS-194),   */
/* creates a context with the SPEC-06 1.4 defaults for `cint run` and an      */
/* output callback that writes each print statement to stdout, runs `main_fn` */
/* once with allowance `fuel`, flushes stdout before a fault is reported and  */
/* before it returns, and returns the exit status:                            */
/*   CINT_EXIT_OK           main_fn returned CINT_OK and stdout was written;  */
/*   CINT_EXIT_FAULT        CINT_FAULT: the canonical fault record bytes      */
/*                          (SPEC-01 IM-149) were written to `fault_dest`;    */
/*   CINT_EXIT_ERROR_VALUE  CINT_OK with an error result (6c'): the error     */
/*                          record (below) was written to `fault_dest`;       */
/*   CINT_EXIT_USAGE        a destination is malformed;                       */
/*   CINT_EXIT_ENVIRONMENT  stdout or a destination could not be written, or  */
/*                          the context could not be allocated;               */
/*   CINT_EXIT_INTERNAL     any other status, a record that does not encode,  */
/*                          or a malformed state table.                       */
/* `fault_dest` is NULL (the record is not written), "handle:<N>" for an      */
/* inherited handle (a file descriptor on POSIX, a HANDLE value on Windows,   */
/* in decimal; it is written and not closed), or a file path in UTF-8, which  */
/* is created or truncated. It is touched only when the program faults or     */
/* leaves with an error result. The caller (the `cint` command) gives the     */
/* program the null device as stdin and renders the record on stderr; this    */
/* function writes nothing to stderr.                                         */
/*                                                                            */
/* The error record (Proposed, rt/OPEN.md RT-OQ-33; SPEC-06 3.4a, BX12-12,    */
/* BX12-22), all little-endian: a U32 length and the ASCII domain string      */
/* CINT_ERROR_RECORD_DOMAIN (SPEC-01 IM-152); a U32 length and the bytes of   */
/* the set's `name`; a U32 length and the bytes of the value's name,          */
/* values[tag - 1]; the set's `tag` code (CINT_TAG_U8 to CINT_TAG_U64) as a   */
/* U32; and the tag as a U64. Names are written without their NUL. A set      */
/* with an empty name or value name, another `tag` code, or a tag above its   */
/* type's range does not encode.                                              */

#define CINT_EXIT_OK 0
#define CINT_EXIT_FAULT 1
#define CINT_EXIT_COMPILE 2
#define CINT_EXIT_CHECK 3
#define CINT_EXIT_USAGE 4
#define CINT_EXIT_ENVIRONMENT 5
#define CINT_EXIT_ERROR_VALUE 6  /* `main` or a test left with an error result (6c') */
#define CINT_EXIT_INTERNAL 7

#define CINT_ERROR_RECORD_DOMAIN "cint-core-1/error-result/v1"

typedef cint_status (*cint_main_fn)(cint_ctx *ctx, int64_t fuel);
int cint_program_run(const cint_program *program, cint_main_fn main_fn, int64_t fuel,
                     const char *fault_dest);

/* cint_program_run with a second destination, `fuel_dest`, of the same forms  */
/* (NULL: none). When main_fn returned CINT_OK (with or without an error        */
/* result), or CINT_FAULT with a fault record, the fuel record is written there */
/* before the fault or error record is reported: the domain string              */
/* CINT_FUEL_RECORD_DOMAIN (U32 length and ASCII, SPEC-01 IM-152), then the     */
/* entry's fuel consumed as an I64, little-endian (IM-144),                     */
/* CINT_FUEL_RECORD_BYTES in all (SPEC-06 3.4a; compiler/OPEN.md CINTC-OQ-46,   */
/* Proposed). If it cannot be written the status is CINT_EXIT_ENVIRONMENT.      */
/* Generated mains of programs without module state call this one.              */
#define CINT_FUEL_RECORD_DOMAIN "cint-core-1/fuel-consumed/v1"
#define CINT_FUEL_RECORD_BYTES 40
int cint_program_run_fuel(const cint_program *program, cint_main_fn main_fn, int64_t fuel,
                          const char *fault_dest, const char *fuel_dest);

/* cint_program_run_fuel with a third destination, `state_dest`, of the same    */
/* forms, and the program's state tables (6b). When the entry ran (main_fn      */
/* returned CINT_OK, or CINT_FAULT with a fault record) and neither             */
/* `state_dest` nor `state` is NULL, the text cint_state_render writes for      */
/* `state` (the `state.global` lines of SPEC-09 CONF-11 rule 10, LF after each, */
/* none when no table has a variable) is written there after the fuel record    */
/* and before the fault or error record is reported. If it cannot be written    */
/* the status is CINT_EXIT_ENVIRONMENT; a malformed table is                    */
/* CINT_EXIT_INTERNAL. Proposed (rt/OPEN.md RT-OQ-33). Generated mains of       */
/* programs with module state call this one, with argv[3] as `state_dest`.      */
int cint_program_run_state(const cint_program *program, cint_main_fn main_fn, int64_t fuel,
                           const char *fault_dest, const char *fuel_dest, const char *state_dest,
                           const cint_state_table *state);

/* ------------------------------------------------------------------------- */
/* 14. The view descriptors of a dispatch's E_ALIAS record (rt/OPEN.md        */
/* RT-OQ-35). Views and kernels are in cint_rt.h, section 14.                 */

typedef struct cint_fault_descriptor {
    uint32_t elem;   /* element type tag */
    uint32_t write;  /* 1 write, 0 read */
    int64_t rank;
    int64_t origin;
    int64_t shape[4];
    int64_t stride[4];
} cint_fault_descriptor;
_Static_assert(sizeof(cint_fault_descriptor) == 88, "cint_fault_descriptor");
CINT_RT_API cint_status cint_ctx_fault_descriptors(const cint_ctx *ctx, cint_fault_descriptor out[2], uint32_t *count);

#endif /* CINT_RT_HOST_H */
