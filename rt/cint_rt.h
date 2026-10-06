/* cint_rt.h: the cint-rt-3 runtime contract (normative).
 *
 * Status: Specified for slice 1 as cint-rt-1 (plan interim decision CC-03,
 * confirmed by decision 23: the contract is published as this header,
 * with static_assert layout checks). Slice 2 task 2.4 makes it cint-rt-2
 * (slice 2 decision patch D-2, D-3, D-16, D-19): view types, a minimal buffer
 * registry and cint_view_bind, the observer interface, status-returning
 * cint_ctx_create and cint_ctx_clear_fault, the depth limit, frame-arena
 * capacity and output callback in cint_ctx_config, staged print output, and
 * cint_program_run. Task 2.7 moved the encoder to fault record version 2
 * (SPEC-01 IM-149, `cint-core-1/fault/v2`, D-10). Box 10 unit 2 makes it
 * cint-rt-3, ABI 3.0 (SPEC-03 A-5, A-26): the registry of SPEC-03 5.3 (6a),
 * the T3 entry checks of a cint-abi-1 wrapper (6a'), struct results through
 * a pointer, the reflection table (6b''), and the host functions a library
 * exports (CINT_RT_API). The prose companion is
 * docs/cint/RT-CONTRACT.md; where the two differ, this header governs. Open
 * items found while writing it are in rt/OPEN.md (RT-OQ-nn).
 *
 * Owning clauses:
 *   SPEC-01 4.1 to 4.9  operator forms, floor division, shifts, conversions
 *   SPEC-01 7 (IM-90)   exact decimal formatting of printed values
 *   SPEC-01 9.1 to 9.4  fault codes, fault record, sticky faults, call depth
 *   SPEC-01 9.8         operation identifiers
 *   SPEC-01 10.2        fuel-v1 charge points
 *   SPEC-01 11.1, 11.2  canonical values and the fault record layout
 *   SPEC-03 A-1 to A-12 C ABI principles, status values, the busy flag, views
 *   SPEC-03 H-12, H-13  refusals, entry faults, overlap
 *   SPEC-04 4.6, 12     error sets and error results (LS-92 to LS-97, LS-315)
 *   SPEC-04 9.1         the print statement (LS-193 to LS-197)
 *   SPEC-06 3.2, 3.4a   exit statuses and channels of a program process
 *   SPEC-09 7.1 to 7.11 the emitted-C contract (EMIT-01 to EMIT-31)
 *   SPEC-09 CONF-13     the observer interface cint-observe-1
 *
 * What this header provides:
 *   - checked helpers `bool cint_<op>_<t>(cint_ctx *, cint_site, ...)` for
 *     add, sub, mul, div, rem, neg, shl, shl_wrap and shr on I8 to I64 and U8
 *     to U64, and checked conversions `cint_as_<to>_from_<from>`; each writes
 *     its result through `out` and returns true, or returns false after the
 *     runtime has written the canonical fault record (once: the record is
 *     sticky, and every checked helper returns false on a faulted context
 *     without writing `out`);
 *   - wrapping helpers `cint_<op>_wrap_<t>` and `cint_as_wrap_<to>_from_<from>`,
 *     which never fault (shl_wrap still checks its count, SPEC-01 4.1), and
 *     saturating helpers `cint_<op>_sat_<t>` for add, sub and mul;
 *   - result-returning helpers `cint_<op>_result_<t>` for add, sub, mul, div,
 *     rem and shl, which return an ArithError tag instead of faulting (10a);
 *   - site tables, the fault record structure, its canonical encoder, and
 *     contexts with entries, fuel and call depth;
 *   - views, host buffers and the entry checks of a cint-abi-1 wrapper;
 *   - print output staged per statement and delivered to a host callback;
 *   - the error result of an entry (6c');
 *   - cint_program_run, the body of a generated program's C main;
 *   - views of rank 1 to 4, slices, copies, reductions, kernel dispatch
 *     addresses and scratch storage for generated code (section 14).
 *
 * Helper modes (SPEC-09 EMIT-04, EMIT-05, CONF-03). Define at most one:
 *   CINT_RT_HELPERS_PORTABLE  range checks in standard C17 only.
 *   CINT_RT_HELPERS_BUILTIN   GCC and Clang __builtin_*_overflow; on MSVC x64
 *                             _mul128 and _umul128 (ARM64 __mulh and __umulh)
 *                             for 64-bit multiply, the portable bodies elsewhere.
 * Neither defined: builtin on GCC and Clang, portable elsewhere (the MSVC
 * intrinsic path is Proposed until admitted, EMIT-05). CINT_RT_PORTABLE=1, the
 * EMIT-04 spelling, selects the portable mode.
 *
 * Rules every helper follows (SPEC-09 EMIT-03, EMIT-07 to EMIT-11):
 *   - no C operation with undefined behavior is evaluated for any input: no
 *     signed overflow, no division by zero, no INT64_MIN / -1 or % -1, no shift
 *     by a count outside 0..63, no left shift of a negative signed value;
 *   - wrapping results are computed in uint64_t and mapped back to the signed
 *     type by cint_rt_sext, never by an out-of-range conversion;
 *   - operands narrower than 64 bits are widened to int64_t or uint64_t first;
 *   - the result is computed (or the operation is found undefined) before the
 *     fault is reported, and a fault is reported before `out` is written.
 *
 * Symbol use (SPEC-09 EMIT-31): every helper here is static inline. Out-of-line
 * functions that generated code calls are named cint_rt_*; host functions are
 * named cint_*. Generated code marks its exports with CINT_RT_EXPORT and its
 * external, non-exported cg_* symbols with CINT_RT_INTERNAL. This header has
 * no binary floating-point types (EMIT-01).
 */
#ifndef CINT_RT_H
#define CINT_RT_H

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* 1. Implementation properties (SPEC-09 EMIT-02, SPEC-03 A-1).               */

#if !defined(INT8_MAX) || !defined(INT16_MAX) || !defined(INT32_MAX) || !defined(INT64_MAX) || \
    !defined(UINT8_MAX) || !defined(UINT16_MAX) || !defined(UINT32_MAX) || !defined(UINT64_MAX)
#error "cint-rt-3 needs int8_t to int64_t and uint8_t to uint64_t (SPEC-09 EMIT-02)"
#endif
_Static_assert(CHAR_BIT == 8, "cint-rt-3: CHAR_BIT must be 8 (SPEC-09 EMIT-02)");
_Static_assert(SIZE_MAX >= 0xFFFFFFFFu, "cint-rt-3: size_t must have at least 32 bits (EMIT-02)");
_Static_assert(sizeof(void *) == 8, "cint-rt-3 supports 64-bit hosts only (SPEC-03 A-1)");

/* The runtime contract version (SPEC-09 RCPT-08; RT-CONTRACT.md). */
#define CINT_RT_CONTRACT "cint-rt-3"

/* Export of a program library's symbols (SPEC-09 SEED-13: the one host
 * compiler extension that generated code uses, gated here). A Windows DLL
 * exports only __declspec(dllexport) symbols, under MSVC, GCC and Clang alike;
 * an ELF or Mach-O shared object exports default-visibility symbols. */
#if defined(_WIN32) && (defined(_MSC_VER) || defined(__GNUC__) || defined(__clang__))
#define CINT_RT_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define CINT_RT_EXPORT __attribute__((visibility("default")))
#else
#define CINT_RT_EXPORT
#endif

/* External linkage without export (SPEC-09 EMIT-31, CONF-13 X-4; CINTC-OQ-49).
 * Generated code writes CINT_RT_INTERNAL on each external cg_ definition and
 * declaration, so the program's C files can name them but the shared object
 * does not export them. ELF and Mach-O need the hidden visibility for that; a
 * Windows DLL exports nothing unmarked, so the macro is empty there. */
#if !defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
#define CINT_RT_INTERNAL __attribute__((visibility("hidden")))
#else
#define CINT_RT_INTERNAL
#endif

/* The runtime object of a library build (`cint build --lib`, SPEC-06 3.1) is
 * compiled with CINT_RT_LIBRARY defined. The library then exports the host
 * functions, which carry CINT_RT_API, and no other runtime symbol (SPEC-09
 * EMIT-31; box 10 default BX10-07): a Windows DLL exports only what
 * CINT_RT_EXPORT marks, and off Windows every other declaration of this header
 * is hidden by the pragma below, popped at its end. */
#if defined(CINT_RT_LIBRARY)
#define CINT_RT_API CINT_RT_EXPORT
#else
#define CINT_RT_API
#endif
#if defined(CINT_RT_LIBRARY) && !defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
#pragma GCC visibility push(hidden)
#endif

/* ------------------------------------------------------------------------- */
/* 2. Helper mode.                                                            */

#if defined(CINT_RT_PORTABLE)
#if CINT_RT_PORTABLE
#ifndef CINT_RT_HELPERS_PORTABLE
#define CINT_RT_HELPERS_PORTABLE 1
#endif
#endif
#endif

#if defined(CINT_RT_HELPERS_PORTABLE) && defined(CINT_RT_HELPERS_BUILTIN)
#error "define at most one of CINT_RT_HELPERS_PORTABLE and CINT_RT_HELPERS_BUILTIN"
#endif

#if defined(__GNUC__) || defined(__clang__)
#define CINT_RT_HAVE_BUILTINS 1
#define CINT_RT_BUILTINS_GNU 1
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
#define CINT_RT_HAVE_BUILTINS 1
#define CINT_RT_BUILTINS_MSVC 1
#include <intrin.h>
#else
#define CINT_RT_HAVE_BUILTINS 0
#endif

#if defined(CINT_RT_HELPERS_PORTABLE)
#define CINT_RT_USE_BUILTINS 0
#elif defined(CINT_RT_HELPERS_BUILTIN)
#if !CINT_RT_HAVE_BUILTINS
#error "CINT_RT_HELPERS_BUILTIN: this compiler has no overflow builtins or intrinsics known to cint-rt-3"
#endif
#define CINT_RT_USE_BUILTINS 1
#elif defined(CINT_RT_BUILTINS_GNU)
#define CINT_RT_USE_BUILTINS 1
#else
#define CINT_RT_USE_BUILTINS 0
#endif

#if CINT_RT_USE_BUILTINS
#define CINT_RT_HELPER_MODE "builtin"
#else
#define CINT_RT_HELPER_MODE "portable"
#endif

/* ------------------------------------------------------------------------- */
/* 3. Status values, fault codes, type tags (SPEC-03 A-6, SPEC-01 9.1, 11.1). */

typedef int32_t cint_status;
#define CINT_OK 0
#define CINT_FAULT 1
#define CINT_REFUSED 2
#define CINT_BUSY 3
#define CINT_FAULTED 4
#define CINT_RESOURCE 5

/* SPEC-03 A-5: (major << 16) | minor. cint-rt-1 was ABI 1.0; cint-rt-2 changed
 * the signature of cint_ctx_create and the layout of cint_ctx_config, so it
 * was ABI 2.0 (rt/OPEN.md RT-OQ-21). cint-rt-3 changes cint_buffer_register,
 * cint_ctx_config and the result of a struct-valued export, and adds the
 * reflection table, so it is ABI 3.0, taken once (A-26; RT-OQ-39). */
#define CINT_ABI_VERSION 0x00030000u

#define CINT_E_OVERFLOW 1u
#define CINT_E_DIV_ZERO 2u
#define CINT_E_BOUNDS 3u
#define CINT_E_SHAPE 4u
#define CINT_E_SHIFT 5u
#define CINT_E_NARROW 6u
#define CINT_E_ALIAS 7u
#define CINT_E_STALE_HANDLE 8u
#define CINT_E_FUEL 9u
#define CINT_E_UNSUPPORTED 10u
#define CINT_E_DOMAIN 11u  /* plan interim decision OQ-01 */
#define CINT_E_DEPTH 12u
#define CINT_E_ASSERT 13u  /* plan interim decision OQ-13 */

#define CINT_TAG_BOOL 0x01
#define CINT_TAG_Z 0x0f
#define CINT_TAG_I8 0x11
#define CINT_TAG_I16 0x12
#define CINT_TAG_I32 0x13
#define CINT_TAG_I64 0x14
#define CINT_TAG_U8 0x21
#define CINT_TAG_U16 0x22
#define CINT_TAG_U32 0x23
#define CINT_TAG_U64 0x24

/* Fuel allowance meaning "no allowance": the entry counts fuel and never
 * faults E_FUEL (SPEC-01 10.1). Any other allowance must be >= 0. */
#define CINT_FUEL_UNBOUNDED ((int64_t)-1)

/* The largest call-depth limit D this runtime provides frames for. An entry
 * with a larger D faults E_UNSUPPORTED before execution (SPEC-01 9.4). */
#define CINT_RT_MAX_DEPTH 1024u

/* Size bounds of a fault record (SPEC-01 9.2). */
#define CINT_FAULT_MAX_OPERANDS 8u
#define CINT_FAULT_MAX_OPERATION 64u
#define CINT_FAULT_MAX_PATH 1024u
#define CINT_Z_MAX_BYTES 257u
/* A tagged value as SPEC-01 11.1 bytes: the largest in cint-rt-1 is a tagged Z
 * (tag, U32 length, 257 bytes). */
#define CINT_TVALUE_MAX 262u

/* ------------------------------------------------------------------------- */
/* 4. Sites, modules, programs (SPEC-09 EMIT-23).                             */

/* A site names one operator, call, or charge point: module number in the
 * program table and site ordinal in that module's table. Ordinals start at 1;
 * ordinal 0 means "no site". */
typedef struct cint_site {
    uint32_t module;
    uint32_t index;
} cint_site;
_Static_assert(sizeof(cint_site) == 8, "cint_site");

/* One row of a module's static site table: the 1-based line and column (in
 * Unicode scalar values, SPEC-09 DIAG-01) of the operator token or call, and
 * its operation identifier (SPEC-01 9.8), a NUL-terminated ASCII string. */
typedef struct cint_site_info {
    uint32_t line;
    uint32_t column;
    const char *operation;
} cint_site_info;
_Static_assert(sizeof(cint_site_info) == 16, "cint_site_info");

/* A module descriptor: module-relative path ('/' separators, at most 1024
 * UTF-8 bytes, SPEC-01 9.2) and its site table (row 0 is the "no site" row). */
typedef struct cint_module {
    const char *path;
    uint32_t path_len;
    uint32_t site_count;
    const cint_site_info *sites;
} cint_module;
_Static_assert(sizeof(cint_module) == 24, "cint_module");

/* A program: its modules in program order (cint_site.module indexes this
 * table) and its revision identity (32 bytes, SPEC-01 11.4), or NULL when the
 * compiler reports it absent (the seed, SPEC-09 SEED-05). */
typedef struct cint_program {
    uint32_t module_count;
    uint32_t reserved;  /* 0 */
    const cint_module *const *modules;
    const uint8_t *revision;
} cint_program;
_Static_assert(sizeof(cint_program) == 24, "cint_program");

/* A resolved site. `path` is not NUL-terminated; `operation` is. */
typedef struct cint_position {
    const char *path;
    uint32_t path_len;
    uint32_t line;
    uint32_t column;
    uint32_t reserved;
    const char *operation;
} cint_position;
_Static_assert(sizeof(cint_position) == 32, "cint_position");

/* ------------------------------------------------------------------------- */
/* 4a. Element types and views (SPEC-03 5.3, A-1, A-13; SPEC-02 V-1, V-2;     */
/* slice 2 decision patch D-3). rt/cint_bridge.h requires CINT_RT_HAVE_VIEW.  */

#define CINT_RT_HAVE_VIEW 1
typedef uint64_t cint_buffer_id;  /* 0 is never a valid id */
#define CINT_MAX_RANK 4           /* SPEC-02 V-2 */

typedef struct cint_type {
    uint16_t code;       /* SPEC-01 IM-146 tag */
    uint8_t storage;     /* fixed point: storage tag; otherwise 0 */
    uint8_t reserved0;   /* 0 */
    uint16_t frac_bits;  /* fixed point: f; otherwise 0 */
    uint16_t reserved1;  /* 0 */
    uint32_t record_id;  /* record: layout table index; otherwise 0 */
    uint32_t reserved2;  /* 0 */
} cint_type;
_Static_assert(sizeof(cint_type) == 16 && offsetof(cint_type, record_id) == 8, "cint_type");

/* A view of registered memory. `origin` is the offset of the first logical
 * element from the buffer base, in elements; `shape`, `stride` (elements,
 * signed) and `lower` (declared lower bounds) are used up to `rank`. */
#define CINT_VIEW_READ 0u
#define CINT_VIEW_WRITE 1u
typedef struct cint_view {
    cint_buffer_id buffer;
    uint64_t generation;
    cint_type type;
    uint8_t rank;         /* 1..CINT_MAX_RANK; cint_view_bind binds rank 1 only */
    uint8_t perm;         /* CINT_VIEW_READ or CINT_VIEW_WRITE */
    uint8_t reserved[6];  /* 0 */
    int64_t origin;
    int64_t shape[CINT_MAX_RANK];
    int64_t stride[CINT_MAX_RANK];
    int64_t lower[CINT_MAX_RANK];
} cint_view;
_Static_assert(sizeof(cint_view) == 144 && offsetof(cint_view, origin) == 40 &&
                   offsetof(cint_view, shape) == 48,
               "cint_view");

/* Parameter modes a view is bound for (SPEC-04 6.2). */
#define CINT_MODE_IN 0u
#define CINT_MODE_OUT 1u
#define CINT_MODE_INOUT 2u

/* ------------------------------------------------------------------------- */
/* 5. Typed values, shift counts, the fault record (SPEC-01 9.2, 11.1, 11.2). */

/* A tagged value held as its canonical SPEC-01 11.1 bytes: the type
 * descriptor, then the payload (w / 8 bytes little-endian for I8 to U64; a U32
 * byte count then minimal two's complement bytes for Z). */
typedef struct cint_tvalue {
    uint16_t len;
    uint8_t bytes[CINT_TVALUE_MAX];
} cint_tvalue;
_Static_assert(sizeof(cint_tvalue) == 264, "cint_tvalue");

/* A shift count of any integer type (SPEC-01 4.6): its type tag and its value
 * as a 64-bit pattern, sign-extended for signed types. Build one with the
 * cint_count_<t> functions below. The count is valid for width w exactly when
 * bits <= w - 1, whatever its type. */
typedef struct cint_count {
    uint64_t bits;
    uint32_t tag;
    uint32_t reserved;  /* 0 */
} cint_count;
_Static_assert(sizeof(cint_count) == 16, "cint_count");

/* The kernel-dispatch address of SPEC-01 9.2 and 9.3. Absent for scalar code;
 * present only for kernel faults (slice 3). */
typedef struct cint_fault_address {
    int64_t dispatch;
    int64_t work_item;
    int64_t step;
    uint8_t phase;          /* 0 entry, 1 work-item, 2 epilogue */
    uint8_t has_work_item;  /* work-item index and step are present */
    uint16_t name_len;
    uint32_t reserved;
    char name[256];         /* kernel qualified name, UTF-8, not NUL-terminated */
} cint_fault_address;
_Static_assert(sizeof(cint_fault_address) == 288, "cint_fault_address");

/* The fault record (SPEC-01 9.2) as a host structure. It is a non-canonical
 * view (SPEC-01 1.4, 11.2); cint_fault_encode produces the canonical bytes.
 * Positions are held as sites and resolved through `program` when encoded. A
 * record is zero-filled before it is written, so unused bytes are 0.
 * `code` 0 means "no fault". */
typedef struct cint_fault_record {
    const cint_program *program;
    uint16_t code;
    uint8_t operation_len;
    uint8_t operand_count;
    uint8_t has_exact;
    uint8_t has_limit;
    uint8_t has_revision;
    uint8_t has_address;
    char operation[CINT_FAULT_MAX_OPERATION];  /* ASCII, not NUL-terminated */
    cint_site position;
    uint8_t revision[32];
    cint_tvalue operands[CINT_FAULT_MAX_OPERANDS];
    cint_tvalue exact;  /* tagged Z */
    cint_tvalue limit;  /* tagged value */
    cint_fault_address address;
    uint32_t stack_count;  /* active user calls, outermost first; the entry contributes none */
    uint32_t reserved;
    cint_site stack[CINT_RT_MAX_DEPTH];
} cint_fault_record;
_Static_assert(offsetof(cint_fault_record, operation) == 16, "cint_fault_record.operation");
_Static_assert(offsetof(cint_fault_record, position) == 80, "cint_fault_record.position");
_Static_assert(offsetof(cint_fault_record, operands) == 120, "cint_fault_record.operands");
_Static_assert(offsetof(cint_fault_record, address) == 2760, "cint_fault_record.address");
_Static_assert(offsetof(cint_fault_record, stack) == 3056, "cint_fault_record.stack");
_Static_assert(sizeof(cint_fault_record) == 3056 + 8 * CINT_RT_MAX_DEPTH, "cint_fault_record");

/* ------------------------------------------------------------------------- */
/* 6. Contexts (SPEC-03 A-2, A-4, A-7, A-7a, A-11).                           */

typedef struct cint_ctx cint_ctx;  /* opaque to hosts */

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
 * entry that takes its limit from the context (CINT_DEPTH_FROM_CONFIG, below;
 * SPEC-01 IM-119); 0 selects CINT_DEFAULT_DEPTH. `frame_arena_bytes` is the
 * frame-arena capacity (SPEC-04 LS-110), recorded with the context; 0 selects
 * the default. With `output` NULL, print statements are staged and their
 * bytes discarded. `module`, when not NULL, is the descriptor of the program's
 * root module, whose record layout table gives the size of a record element
 * type in cint_buffer_register and cint_buffer_create (6a); its `program` must
 * be `program`. */
typedef struct cint_module_info cint_module_info;
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
} cint_ctx_config;
_Static_assert(sizeof(cint_ctx_config) == 80 && offsetof(cint_ctx_config, depth_limit) == 40 &&
                   offsetof(cint_ctx_config, output) == 56 && offsetof(cint_ctx_config, module) == 72,
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
 * "Bool true"), or only the decimal. Returns the length without the NUL, or 0
 * for an invalid value; writes at most cap bytes including a NUL. */
size_t cint_tvalue_render(const cint_tvalue *value, char *buf, size_t cap);
size_t cint_tvalue_render_decimal(const cint_tvalue *value, char *buf, size_t cap);

/* The SHA-256 digest (FIPS 180-4) of n bytes at p (p may be NULL when n is 0):
 * the digest of the bridge's MANIFEST (SPEC-09 CINTC-06) and of receipts. */
void cint_sha256(const uint8_t *p, size_t n, uint8_t digest[32]);

/* Functions generated code calls (out of line, in the cint_rt library).
 *
 * cint_rt_entry_begin starts an entry (SPEC-01 9.4) at the entry's site with
 * fuel allowance `fuel` (>= 0, or CINT_FUEL_UNBOUNDED) and depth limit `depth`
 * (>= 1, or CINT_DEPTH_FROM_CONFIG for the context's configured limit, which a
 * cint-abi-1 wrapper passes; an observer entry passes its own, CONF-13):
 * CINT_BUSY if another call is active on the context, CINT_FAULTED if it
 * holds a fault record, CINT_REFUSED for an invalid budget, CINT_FAULT if the
 * depth exceeds CINT_RT_MAX_DEPTH (E_UNSUPPORTED) or the entry's own fuel
 * charge faults (E_FUEL), otherwise CINT_OK with the context held busy until
 * cint_rt_entry_end, which returns CINT_FAULT, CINT_REFUSED when a view was
 * refused at entry (cint_view_bind), or CINT_OK. cint_rt_entry_open does the
 * same without the entry's fuel charge, which a cint-abi-1 wrapper makes with
 * cint_fuel_charge once its entry checks pass (section 6a'; SPEC-02 F-5
 * check 9), so that a refusal or an entry fault charges nothing (H-12).
 *
 * cint_rt_call_enter performs a user-function call's checks at its call site:
 * E_DEPTH when the call would exceed the depth limit (no fuel is charged),
 * then the call's fuel charge, then records the site on the stack.
 * cint_rt_call_leave undoes the depth and stack effect of a returned call. */
#define CINT_DEPTH_FROM_CONFIG ((int64_t)0)
cint_status cint_rt_entry_begin(cint_ctx *ctx, cint_site site, int64_t fuel, int64_t depth);
cint_status cint_rt_entry_open(cint_ctx *ctx, cint_site site, int64_t fuel, int64_t depth);
cint_status cint_rt_entry_end(cint_ctx *ctx);
bool cint_rt_call_enter(cint_ctx *ctx, cint_site site);
void cint_rt_call_leave(cint_ctx *ctx);

/* Fault writers. Each writes the canonical record unless the context already
 * holds one, and returns false. `a` and `b` are operand bit patterns
 * (sign-extended for signed types). */
#define CINT_RT_OP_ADD 1u
#define CINT_RT_OP_SUB 2u
#define CINT_RT_OP_MUL 3u
#define CINT_RT_OP_NEG 4u
#define CINT_RT_OP_DIV 5u
#define CINT_RT_OP_REM 6u
#define CINT_RT_OP_SHL 7u
#define CINT_RT_OP_SHL_WRAP 8u
#define CINT_RT_OP_SHR 9u
bool cint_rt_fault_arith(cint_ctx *ctx, cint_site site, uint32_t op, uint32_t tag, uint64_t a, uint64_t b);
bool cint_rt_fault_shift(cint_ctx *ctx, cint_site site, uint32_t op, uint32_t tag, uint64_t a, cint_count k);
bool cint_rt_fault_narrow(cint_ctx *ctx, cint_site site, uint32_t from_tag, uint32_t to_tag, uint64_t x);
bool cint_rt_fault_index(cint_ctx *ctx, cint_site site, const char *operation, int64_t index, int64_t extent);
bool cint_rt_fault_fuel(cint_ctx *ctx, cint_site site, uint64_t units);
/* E_ASSERT, operation assert.checked.bool (SPEC-04 LS-191, SPEC-01 IM-134): with
 * tag 0 no operands; otherwise a failed comparison's two operands of that tag. */
bool cint_rt_fault_assert(cint_ctx *ctx, cint_site site, uint32_t tag, uint64_t a, uint64_t b);

/* Entry-phase fault writers of a cint-abi-1 wrapper (SPEC-03 A-12, H-12, H-13;
 * slice 2 decision patch D-3), called after cint_rt_entry_begin and before the
 * body runs, with the entry's site. An entry fault charges no fuel, so each
 * returns the entry's own unit (fuel consumed is 0 afterward). Parameters and
 * dimensions are numbered from 0 in declaration order (rt/OPEN.md RT-OQ-22).
 *   cint_fault_shape: E_SHAPE, operation bind.shape, operands I64 param, I64
 *     dim, I64 actual extent; limit I64 expected extent (the extent the shape
 *     variable was first bound to).
 *   cint_fault_alias: E_ALIAS, operation bind.alias, operands I64 param (the
 *     out or inout parameter) and I64 other (the view it overlaps). */
bool cint_fault_shape(cint_ctx *ctx, cint_site site, uint32_t param, uint32_t dim, int64_t expected,
                      int64_t actual);
bool cint_fault_alias(cint_ctx *ctx, cint_site site, uint32_t param, uint32_t other);
/* The same E_SHAPE check at an internal call site (dim 0): true when actual equals
 * expected; otherwise the cint_fault_shape record at that site, fuel kept. */
bool cint_shape_check(cint_ctx *ctx, cint_site site, uint32_t param, int64_t expected, int64_t actual);

/* Compares logical element counts before copying (SPEC-01 IM-187).
 * A mismatch records copy.shape with I64 dimension and source extent, an
 * I64 destination limit and no exact. Preserves fuel and the first fault. */
bool cint_copy_shape_check(cint_ctx *ctx, cint_site site, int64_t dimension,
                           int64_t destination_extent, int64_t source_extent);

/* ------------------------------------------------------------------------- */
/* 6a. The buffer registry and view binding (SPEC-03 5.3, M-10, A-8, A-8a,    */
/* A-9, A-12, A-13a, H-12; rt/OPEN.md RT-OQ-24 and RT-OQ-39). This runtime    */
/* registers host memory (CINT_MEM_HOST) only, has no devices, and reads      */
/* and binds views of any rank, stride and lower bound (6a').                 */
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
/* CINT_PUBLISH_COPY (CALL); the memory is host memory with no device and no  */
/* synchronization (UNSUPPORTED); the type is Bool, I8 to U64, or a record of */
/* config.module's layout table (TYPE); the extent is at least 0 (EXTENT) and */
/* its byte size fits an I64 and the address space (SIZE); the address is not */
/* NULL unless that size is 0, and is aligned to the largest power of two     */
/* dividing the element size, at most 8 (ALIGN). It writes the id and the     */
/* generation. The registration is typed: a view of it must have its type.    */
/* Its memory stays the host's: a borrowed registration lasts until           */
/* cint_buffer_release, and the runtime never frees it. `publish` is recorded */
/* for the staging of dispatch outputs (M-30a).                               */
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
/* binding checks it, and any permission reads. cint_buffer_lease gives a     */
/* read-only address of a live created buffer's storage (NULL for no storage) */
/* and a lease number, from 1, never reused; the address stays valid until    */
/* cint_lease_end of that number. A borrowed registration or an unknown lease */
/* number is refused (LEASE). While a buffer has a live lease, an entry that  */
/* binds it to an out or inout parameter returns CINT_BUSY, nothing recorded  */
/* and fuel as it was (A-9); writing through a leased address is outside      */
/* every guarantee.                                                           */
/*                                                                            */
/* cint_view_bind, the T2 binding of the seed's wrappers, validates one rank  */
/* 1 view for a parameter of mode `mode` (CINT_MODE_*), with element type     */
/* `want` of `elem_bytes` bytes, inside an entry, in this order: the id is    */
/* registered (BUFFER); its registration is live at the view's generation     */
/* (GENERATION); the view's type equals `want` and its reserved bytes are 0,  */
/* and a typed registration also has that type and element size (TYPE); rank  */
/* 1 (RANK); unit stride (STRIDE); a nonnegative extent and origin (EXTENT);  */
/* the byte offsets (origin + extent) * elem_bytes fit an I64 (SIZE) and lie  */
/* inside the registration (EXTENT); the element address is aligned (ALIGN,   */
/* A-13a); a write view on a write ceiling for out and inout (PERMISSION; any */
/* view serves `in`); and no live lease for out and inout (LEASED). For       */
/* zero-size record storage it requires a typed registration (TYPE) and       */
/* checks origin <= capacity and extent <= capacity - origin (EXTENT), then   */
/* permissions, with no byte arithmetic or alignment. On success it writes    */
/* the element pointer (NULL for zero storage) and the extent. On failure it  */
/* writes nothing, marks the entry refused, and returns false: the wrapper    */
/* then returns cint_rt_entry_end, which is CINT_REFUSED (CINT_BUSY for       */
/* LEASED), with the context and the last entry's fuel as they were (H-12).   */
/* Outside an entry, or on a faulted context, it returns false and changes    */
/* nothing.                                                                   */

#define CINT_RT_MAX_BUFFERS 256u
#define CINT_BUFFER_GENERATION_FIRST ((uint64_t)1)

#define CINT_REFUSAL_NONE 0u
#define CINT_REFUSAL_BUFFER 1u      /* no registration has this id */
#define CINT_REFUSAL_GENERATION 2u  /* released, or another generation */
#define CINT_REFUSAL_TYPE 3u
#define CINT_REFUSAL_RANK 4u
#define CINT_REFUSAL_STRIDE 5u
#define CINT_REFUSAL_SIZE 6u        /* byte size or offset overflows an I64 */
#define CINT_REFUSAL_EXTENT 7u      /* negative, or outside the registration */
#define CINT_REFUSAL_ALIGN 8u
#define CINT_REFUSAL_PERMISSION 9u
#define CINT_REFUSAL_CALL 10u       /* an invalid argument: a wrapper's `want`, mode or size, a descriptor field */
#define CINT_REFUSAL_BOOL 11u       /* a Bool element or field holds a byte other than 0 or 1 */
#define CINT_REFUSAL_RESULT 12u     /* a NULL result pointer */
#define CINT_REFUSAL_LEASED 13u     /* a leased buffer for out or inout: the entry returns CINT_BUSY (A-9) */
#define CINT_REFUSAL_UNSUPPORTED 14u /* device memory, a device, or a synchronization object */
#define CINT_REFUSAL_LEASE 15u      /* a lease of memory the runtime does not own, or an unknown lease */

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
bool cint_view_bind(cint_ctx *ctx, const cint_view *v, const cint_type *want, uint8_t mode,
                    int64_t elem_bytes, void **ptr, int64_t *n);
/* After cint_view_bind of a view whose element type holds a Bool (the element
 * itself, or a field or array-field element of a record at any depth): each
 * of the `n` elements of `elem_bytes` bytes at `p` has a Bool at each of the
 * `count` byte offsets `offsets`. A byte other than 0 or 1 refuses the entry
 * as cint_view_bind does (CINT_REFUSAL_BOOL; SPEC-03 A-13 "other values
 * refused", H-12), before the body reads it as a C bool. */
bool cint_view_check_bools(cint_ctx *ctx, const void *p, int64_t n, int64_t elem_bytes,
                           const uint32_t *offsets, uint32_t count);

/* ------------------------------------------------------------------------- */
/* 6a'. The entry checks of a cint-abi-1 wrapper (SPEC-03 A-12, H-12, H-13;   */
/* SPEC-02 F-5, A-5, A-7; box 10 default BX10-13; rt/OPEN.md RT-OQ-38).       */
/*                                                                            */
/* A wrapper opens its entry with cint_rt_entry_open, fills one cint_bind per */
/* array parameter (a struct parameter is a record view of extent 1) in       */
/* declaration order, and calls cint_rt_bind_views. That first refuses what   */
/* cannot be formed, for each parameter in order: a wrapper error (CALL); a   */
/* view whose type is not one this runtime knows, or whose reserved bytes are */
/* not 0 (TYPE); a rank outside 1..CINT_MAX_RANK (RANK); an unknown id        */
/* (BUFFER) or a released registration (GENERATION); a write view of a read   */
/* ceiling (PERMISSION); a typed registration whose type or element size the  */
/* view does not have (TYPE); a negative origin or extent, or an element      */
/* outside the registration (EXTENT); an element offset beyond the I64 range  */
/* (SIZE); a misaligned origin element (ALIGN, A-13a); and, for a view of the */
/* declared element type, a Bool byte other than 0 or 1 at one of `bools`     */
/* (BOOL). The element size of a view of another type is its scalar width, or */
/* the typed registration's; a record view of another type over untyped       */
/* memory is not range-checked, since check 3 faults it. It then makes F-5    */
/* checks 1 to 3, each over every parameter in turn, and faults the first     */
/* failure at `site`, with I64 parameter indices and no fuel charged:         */
/*   1  E_STALE_HANDLE bind.stale: the view's generation is not the live      */
/*      registration's (operands I64 param, U64 view generation; limit U64    */
/*      the registration's);                                                  */
/*   2  E_ALIAS bind.permission: a read view for out or inout (I64 param);    */
/*   3  E_UNSUPPORTED bind.type: element type other than `want` (I64 param);  */
/*      E_SHAPE bind.type: rank other than `rank` (I64 param, I64 the view's  */
/*      rank; limit I64 `rank`).                                              */
/* On success it writes each `ptr` (the origin element; NULL for no storage). */
/* The wrapper then checks shapes (check 5, cint_fault_shape) and calls       */
/* cint_rt_bind_finish, which makes checks 6 and 7:                           */
/*   6  E_ALIAS bind.injective: an out or inout view that fails A-7 (I64      */
/*      param);                                                               */
/*   7  E_ALIAS bind.alias: for each out or inout p, each other q in order    */
/*      that A-5 does not prove disjoint by T0, T1 (byte intervals; element   */
/*      intervals within one registration for records with no storage) or T2  */
/*      (same registration and element size, gcd of strides) (I64 p, I64 q,   */
/*      as cint_fault_alias).                                                 */
/* A function's check 8 has nothing to refuse: a body takes a view of any     */
/* rank and stride from `ptr` with the view's extents and strides (rt/OPEN.md */
/* RT-OQ-38). Then the wrapper charges the entry's fuel unit (check 9) and    */
/* runs the body. A kernel's wrapper makes checks 1 to 3 the same way and     */
/* hands its binds to the dispatch (cint_rt_dispatch_host, section 14), which */
/* makes checks 5 to 9 in their order. Both return false, with the context    */
/* refused or faulted, when a check fails, and false with nothing changed     */
/* outside an entry, on a faulted or refused context. cint_rt_refuse marks    */
/* the entry refused for a scalar or result that cannot be formed             */
/* (CINT_REFUSAL_*) and returns false.                                        */
typedef struct cint_bind {
    const cint_view *view;   /* the argument */
    const cint_type *want;   /* the declared element type */
    int64_t elem_bytes;      /* its C size; 0 for a record with no storage */
    const uint32_t *bools;   /* byte offsets of an element's Bools, or NULL */
    uint32_t bool_count;
    uint32_t param;          /* the parameter's index in declaration order */
    uint32_t mode;           /* CINT_MODE_* */
    uint32_t rank;           /* the declared rank */
    void *ptr;               /* written: the origin element */
} cint_bind;
_Static_assert(sizeof(cint_bind) == 56 && offsetof(cint_bind, ptr) == 48, "cint_bind");

bool cint_rt_bind_views(cint_ctx *ctx, cint_site site, cint_bind *b, uint32_t count);
bool cint_rt_bind_finish(cint_ctx *ctx, cint_site site, const cint_bind *b, uint32_t count);
bool cint_rt_refuse(cint_ctx *ctx, uint32_t reason);

/* True when the byte ranges [a, a + an) and [b, b + bn) intersect: test T1 of
 * SPEC-03 H-13 for host memory, which a wrapper applies to each out or inout
 * view against every other view argument before cint_fault_alias. An empty
 * range intersects nothing. Addresses are compared as uintptr_t, so pointers
 * into different objects may be compared. */
static inline bool cint_rt_bytes_overlap(const void *a, int64_t an, const void *b, int64_t bn)
{
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    if (an <= 0 || bn <= 0) {
        return false;
    }
    return x <= y ? y - x < (uintptr_t)an : x - y < (uintptr_t)bn;
}

/* Called only after both views bind successfully in the same context.
 * Zero-size elements overlap by logical intervals within one registration.
 * Different zero-storage registrations are distinct allocations. Positive
 * elements keep byte overlap across registrations; an empty view or a
 * zero/positive pair has no overlap. No endpoint sum is needed. */
static inline bool cint_rt_view_overlap(const cint_view *a, const void *ap, int64_t a_bytes,
                                        const cint_view *b, const void *bp, int64_t b_bytes)
{
    if (a->shape[0] == 0 || b->shape[0] == 0) {
        return false;
    }
    if (a_bytes == 0 || b_bytes == 0) {
        if (a_bytes != b_bytes || a->buffer != b->buffer || a->generation != b->generation) {
            return false;
        }
        return a->origin <= b->origin ? b->origin - a->origin < a->shape[0] :
                                       a->origin - b->origin < b->shape[0];
    }
    return cint_rt_bytes_overlap(ap, a->shape[0] * a_bytes, bp, b->shape[0] * b_bytes);
}

/* Carries zero-storage identity separately from private C token addresses.
 * A field path selects one parent element and field; its node lives in the
 * borrowing frame until its synchronous calls return. Never flattens nested
 * extents or allocate one token per element. These are internal carriers. */
typedef struct cint_rt_zero_path {
    const struct cint_rt_zero_path *parent;
    int64_t index, field;
} cint_rt_zero_path;
typedef struct cint_rt_zero_place {
    const void *local;
    cint_buffer_id buffer;
    uint64_t generation;
    const cint_rt_zero_path *path;
    int64_t origin;
} cint_rt_zero_place;

static inline cint_rt_zero_place cint_rt_zero_local(const void *allocation)
{
    cint_rt_zero_place p = {0};
    p.local = allocation;
    return p;
}

/* Called after successful zero-size binding in the current context. */
static inline cint_rt_zero_place cint_rt_zero_bound(const cint_view *v)
{
    cint_rt_zero_place p = {0};
    p.buffer = v->buffer; p.generation = v->generation; p.origin = v->origin;
    return p;
}

/* Adds an index only after its bounds check. The bound origin plus extent
 * fits I64, so this addition cannot overflow. */
static inline cint_rt_zero_place cint_rt_zero_index(cint_rt_zero_place p, int64_t index)
{
    p.origin += index;
    return p;
}

static inline cint_rt_zero_place cint_rt_zero_field(cint_rt_zero_place p,
                                                    cint_rt_zero_path *node, int64_t field)
{
    node->parent = p.path; node->index = p.origin; node->field = field;
    p.path = node; p.origin = 0;
    return p;
}

/* Compares valid logical places and extents in one context. Equal paths
 * need not share node addresses. Ancestors contain the selected child index;
 * different sibling fields or parent indices are disjoint. */
static inline bool cint_rt_zero_overlap(cint_rt_zero_place a, int64_t an,
                                        cint_rt_zero_place b, int64_t bn)
{
    const cint_rt_zero_path *ap = a.path, *bp = b.path, *ac = NULL, *bc = NULL;
    size_t ad = 0, bd = 0;
    if (an <= 0 || bn <= 0 || a.local != b.local ||
        (a.local == NULL && (a.buffer != b.buffer || a.generation != b.generation))) {
        return false;
    }
    for (const cint_rt_zero_path *p = ap; p != NULL; p = p->parent) { ad++; }
    for (const cint_rt_zero_path *p = bp; p != NULL; p = p->parent) { bd++; }
    while (ad > bd) { ac = ap; ap = ap->parent; ad--; }
    while (bd > ad) { bc = bp; bp = bp->parent; bd--; }
    while (ap != NULL && bp != NULL) {
        if (ap->index != bp->index || ap->field != bp->field) { return false; }
        ap = ap->parent; bp = bp->parent;
    }
    if (ac != NULL) { return ac->index >= b.origin && ac->index - b.origin < bn; }
    if (bc != NULL) { return bc->index >= a.origin && bc->index - a.origin < an; }
    return a.origin <= b.origin ? b.origin - a.origin < an : a.origin - b.origin < bn;
}

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

/* Module state (SPEC-04 LS-113; SPEC-03 1.3 "Context", A-7; slice 2 plan    */
/* task 2.14). A module with module-level variables defines one cint_state:  */
/* its index in the program table, its variables (name, scalar tag, offset  */
/* in the block), the block's size and its initial image. The context owns  */
/* one block per module, created from the image at the module's first      */
/* access and kept until cint_ctx_destroy, so a store is not rolled back by */
/* a fault or a clear (A-7, IM-121). cint_rt_state returns the block, or    */
/* NULL with the entry faulted E_UNSUPPORTED host.resource when it cannot   */
/* be allocated. Proposed (rt/OPEN.md H-OQ-08): a program for cint-harness  */
/* may also export `const cint_state_table cint_observer_state`, its        */
/* tables in CONF-11 rule 10 order. cint_state_render writes the lines     */
/* `state.global <module> <name> <typed value>` of every variable of `t`    */
/* (CONF-11 rule 10) into `out` and returns their length, or SIZE_MAX when */
/* they do not fit in `cap` bytes or a table is malformed. */
typedef struct cint_state_var {
    const char *name;  /* not NUL-terminated */
    uint32_t name_len;
    uint32_t tag;      /* CINT_TAG_* of an integer type or Bool */
    uint32_t offset;   /* in the block */
    uint32_t reserved; /* 0 */
} cint_state_var;
_Static_assert(sizeof(cint_state_var) == 24, "cint_state_var");

typedef struct cint_state {
    uint32_t module;     /* index into program->modules */
    uint32_t var_count;
    uint32_t bytes;      /* block size, >= 1 */
    uint32_t reserved;   /* 0 */
    const void *init;    /* `bytes` bytes */
    const cint_state_var *vars;
} cint_state;
_Static_assert(sizeof(cint_state) == 32, "cint_state");

typedef struct cint_state_table {
    uint32_t count;
    uint32_t reserved;  /* 0 */
    const cint_state *const *states;
} cint_state_table;
_Static_assert(sizeof(cint_state_table) == 16, "cint_state_table");

void *cint_rt_state(cint_ctx *ctx, const cint_state *s);
size_t cint_state_render(const cint_ctx *ctx, const cint_state_table *t, char *out, size_t cap);

/* ------------------------------------------------------------------------- */
/* 6b'. Module descriptor `cm` + C(P) of cint-abi-1 (SPEC-03 A-14; slice 2    */
/* decision patch D-2, D-3). Proposed for T2 (rt/OPEN.md RT-OQ-27): the       */
/* program a host passes to cint_ctx_create, and the record layout table that */
/* cint_type.record_id indexes for a view of struct elements (code            */
/* CINT_TAG_RECORD, SPEC-01 IM-146 tag 71). Exports, effect classes and the   */
/* canonical type signatures are in the reflection table of section 6b''.     */

#define CINT_TAG_RECORD 0x71
typedef struct cint_record_field {
    uint32_t offset;  /* bytes from the start of the record */
    uint32_t count;   /* 1, or the extent of a fixed-length array field */
    cint_type type;   /* the field's (element) type */
} cint_record_field;
_Static_assert(sizeof(cint_record_field) == 24 && offsetof(cint_record_field, type) == 8, "cint_record_field");

typedef struct cint_record_layout {
    uint32_t bytes;        /* logical record bytes; private zero carriers add none */
    uint32_t align;        /* zero: LS-73 alignment; positive: CINT_ELEM_ALIGN(bytes) */
    uint32_t field_count;  /* declaration order */
    uint32_t reserved;     /* 0 */
    const cint_record_field *fields;
} cint_record_layout;
_Static_assert(sizeof(cint_record_layout) == 24 && offsetof(cint_record_layout, fields) == 16, "cint_record_layout");
/* The alignment of an element of `b` bytes that cint_view_bind requires: the
 * largest power of two dividing b, at most 8. A-13a gives 8 for every record,
 * which a record of 12 bytes cannot meet; RT-OQ-27 records the difference. */
#define CINT_ELEM_ALIGN(b) ((uint32_t)((b) % 8u == 0u ? 8u : (b) % 4u == 0u ? 4u : (b) % 2u == 0u ? 2u : 1u))

struct cint_module_info {
    uint32_t abi;           /* CINT_ABI_VERSION */
    uint32_t record_count;
    const cint_program *program;
    const cint_record_layout *records;  /* NULL when record_count is 0 */
};
_Static_assert(sizeof(cint_module_info) == 24 && offsetof(cint_module_info, program) == 8 &&
               offsetof(cint_module_info, records) == 16, "cint_module_info");

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
/* 6c. Print output (SPEC-04 LS-193 to LS-197; SPEC-01 IM-90).                */
/*                                                                            */
/* A print statement is cint_rt_print_begin, then one cint_rt_print_bytes per */
/* literal piece and one cint_rt_print_value per hole, in source order, then  */
/* cint_rt_print_end, which hands the statement's bytes to the output         */
/* callback in one call. Nothing reaches the callback before print_end, so a  */
/* hole that faults leaves the statement unwritten (LS-197); the next         */
/* print_begin or entry discards what was staged. Each returns false on a     */
/* faulted context. Staging grows through the context's allocator; when it    */
/* cannot, the entry faults E_UNSUPPORTED host.resource (SPEC-03 A-6), and a  */
/* callback status other than CINT_OK faults it E_UNSUPPORTED                 */
/* host.error.print with that status as an I64 operand (rt/OPEN.md RT-OQ-23). */
/* A call out of this order faults E_UNSUPPORTED dispatch.admit (IM-185).     */
/*                                                                            */
/* cint_format_value writes the exact decimal of a value of type `tag` (I8 to */
/* U64, given as its 64-bit pattern, whose low w bits are used, sign-extended */
/* for a signed type), or `true` or `false` for Bool (bits 0 or 1): no `+`,   */
/* no grouping, no leading zeros, ASCII `-` (IM-90, LS-195). It returns the   */
/* length (at most CINT_FORMAT_MAX), or 0 for another tag or a Bool other     */
/* than 0 and 1. No terminating NUL is written.                               */
/*                                                                            */
/* cint_rt_print_format prints a hole with a format specification (SPEC-04    */
/* LS-202 to LS-212) as cint_rt_print_value prints one without. `form` is the */
/* kind (CINT_FORMAT_KIND bits: 1 d, 2 x, 3 X, 4 o, 5 b, 6 t) plus the flags  */
/* below; `fill` is the fill's Unicode scalar value; `width` the minimum      */
/* width in scalar values (0 for none); `scale` the number of fractional      */
/* digits of a `/` scale (0 for none). The compiler has applied the static    */
/* rules (LS-213): a form they exclude, a Bool with more than fill, alignment */
/* and width, a fill that is not a scalar value, or another tag faults        */
/* E_UNSUPPORTED dispatch.admit. A rendering whose bytes would pass the       */
/* statement's 1 GiB faults E_UNSUPPORTED host.resource, as staging does.     */

#define CINT_FORMAT_MAX 20u
#define CINT_FORMAT_KIND 15u
#define CINT_FORMAT_LEFT 16u
#define CINT_FORMAT_RIGHT 32u
#define CINT_FORMAT_CENTER 48u
#define CINT_FORMAT_PLUS 64u
#define CINT_FORMAT_SPACE 128u
#define CINT_FORMAT_ALT 256u
#define CINT_FORMAT_ZERO 512u
#define CINT_FORMAT_UNDERSCORE 1024u
#define CINT_FORMAT_COMMA 2048u
size_t cint_format_value(uint32_t tag, uint64_t bits, char out[CINT_FORMAT_MAX]);
bool cint_rt_print_begin(cint_ctx *ctx, cint_site site);
bool cint_rt_print_bytes(cint_ctx *ctx, const void *bytes, size_t len);
bool cint_rt_print_value(cint_ctx *ctx, uint32_t tag, uint64_t bits);
bool cint_rt_print_format(cint_ctx *ctx, uint32_t tag, uint64_t bits, uint32_t form, uint32_t fill,
                          uint64_t width, uint64_t scale);
bool cint_rt_print_end(cint_ctx *ctx);

/* ------------------------------------------------------------------------- */
/* 6c'. Error results (SPEC-04 LS-92 to LS-97, LS-315; SPEC-03 A-18; SPEC-06  */
/* 3.4a, 3.5; SPEC-09 CONF-11 rule 12; decision 2026-10-05, BX12-12, BX12-13, */
/* BX12-21, BX12-22, BX12-30). Proposed (rt/OPEN.md RT-OQ-33).                */
/*                                                                            */
/* An entry whose function returns an error union and returns an error        */
/* completes: cint_rt_entry_end returns CINT_OK and the context is not        */
/* faulted (A-18). Generated code describes each error set an entry can leave */
/* with in a cint_error_set and, inside the entry, calls cint_rt_error_result */
/* once with the error's tag in that set: the set of the entry's result type, */
/* or for a test block the set of the error that leaves it (BX12-30). The     */
/* call is ignored outside an entry, on a faulted context, for a NULL set, a  */
/* tag outside 1..count, or a second call in the same entry. The start of an  */
/* entry clears what the last entry recorded, a refused call (6a) leaves it   */
/* as it was, and an entry that faults leaves none.                           */
/*                                                                            */
/* cint_ctx_error reads the last entry's error result: CINT_REFUSED for a     */
/* NULL argument, CINT_BUSY during an entry, otherwise CINT_OK with the set   */
/* and the tag, or with *set NULL and *tag 0 when the last entry left no      */
/* error or faulted. cint_ctx_clear_fault does not change it.                 */

typedef struct cint_error_set {
    const char *name;           /* "<module>.<Set>" (SPEC-04 LS-225), or a built-in set such as
                                   "ArithError"; NUL-terminated ASCII */
    uint32_t tag;               /* CINT_TAG_* of the underlying type: U8, U16, U32 or U64 (LS-93) */
    uint32_t count;             /* values; their tags are 1 to count */
    const char *const *values;  /* `count` NUL-terminated names in tag order, each "<value>", or
                                   "<Set>.<value>" where another member set declares the same
                                   name (LS-97) */
} cint_error_set;
_Static_assert(sizeof(cint_error_set) == 24 && offsetof(cint_error_set, tag) == 8 &&
                   offsetof(cint_error_set, count) == 12 && offsetof(cint_error_set, values) == 16,
               "cint_error_set");

void cint_rt_error_result(cint_ctx *ctx, const cint_error_set *set, uint64_t tag);
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

/* The leading member of every context. Generated code reads it through the
 * helpers below; hosts never touch it. A context object begins with this
 * structure, so a pointer to the context, converted, points to it (C17
 * 6.7.2.1, paragraph 15). */
typedef struct cint_rt_head {
    uint32_t faulted;    /* nonzero while the context holds a fault record; 2: a boundary record */
    uint32_t in_entry;   /* nonzero between entry begin and end */
    int64_t fuel_limit;  /* allowance of the current entry, or CINT_FUEL_UNBOUNDED */
    int64_t fuel_used;   /* consumed by the current (or last) entry, <= fuel_limit */
} cint_rt_head;
_Static_assert(sizeof(cint_rt_head) == 24, "cint_rt_head");

static inline cint_rt_head *cint_rt_head_of(cint_ctx *ctx)
{
    return (cint_rt_head *)(void *)ctx;
}

static inline bool cint_rt_faulted(cint_ctx *ctx)
{
    return cint_rt_head_of(ctx)->faulted != 0u;
}

/* A fuel-v1 charge of `units` at a charge point (SPEC-01 10.2): loop
 * iterations charge 1. A charge that would exceed the allowance faults E_FUEL
 * and is not counted. With no allowance, a charge that would carry the count
 * past INT64_MAX faults E_FUEL (rt/OPEN.md RT-OQ-04). */
static inline bool cint_fuel_charge(cint_ctx *ctx, cint_site site, uint64_t units)
{
    cint_rt_head *h = cint_rt_head_of(ctx);
    uint64_t room;
    if (h->faulted != 0u) {
        return false;
    }
    room = h->fuel_limit >= 0 ? (uint64_t)(h->fuel_limit - h->fuel_used)
                              : (uint64_t)(INT64_MAX - h->fuel_used);
    if (units > room) {
        return cint_rt_fault_fuel(ctx, site, units);
    }
    h->fuel_used += (int64_t)units;
    return true;
}

/* ------------------------------------------------------------------------- */
/* 7. Bit patterns.                                                           */

/* The value of the low w bits of u read as two's complement (w is 8, 16, 32
 * or 64). This is EMIT-07's defined mapping from unsigned to signed. */
static inline int64_t cint_rt_sext(uint64_t u, unsigned w)
{
    uint64_t mask = w >= 64u ? UINT64_MAX : (((uint64_t)1 << (w & 63u)) - 1u);
    uint64_t sign = (uint64_t)1 << ((w - 1u) & 63u);
    u &= mask;
    if ((u & sign) == 0u) {
        return (int64_t)u;
    }
    return -(int64_t)(mask - u) - 1;
}

static inline uint64_t cint_rt_mask(unsigned w)
{
    return w >= 64u ? UINT64_MAX : (((uint64_t)1 << (w & 63u)) - 1u);
}

/* Arithmetic right shift by k in 0..63, defined for negative x (EMIT-10). */
static inline int64_t cint_rt_asr(int64_t x, unsigned k)
{
    return x >= 0 ? x >> k : ~(~x >> k);
}

static inline unsigned cint_rt_tag_width(uint32_t tag)
{
    return 8u << ((tag & 0x0fu) - 1u);
}

static inline bool cint_rt_tag_signed(uint32_t tag)
{
    return (tag & 0xf0u) == 0x10u;
}

/* True when the integer with pattern `bits` (signed when src_signed) lies in
 * the range of the type `to_tag` (SPEC-09 EMIT-11). */
static inline bool cint_rt_fits(uint64_t bits, bool src_signed, uint32_t to_tag)
{
    unsigned w = cint_rt_tag_width(to_tag);
    bool to_signed = cint_rt_tag_signed(to_tag);
    uint64_t max = to_signed ? ((uint64_t)1 << ((w - 1u) & 63u)) - 1u : cint_rt_mask(w);
    if (!src_signed || (bits >> 63) == 0u) {
        return bits <= max;
    }
    return to_signed && (uint64_t)0 - bits <= (uint64_t)1 << ((w - 1u) & 63u);
}

#define CINT_RT_BITS_S(x) ((uint64_t)(int64_t)(x))
#define CINT_RT_BITS_U(x) ((uint64_t)(x))
#define CINT_RT_SIGNED_S true
#define CINT_RT_SIGNED_U false
#define CINT_RT_NARROW_S(bits, w) cint_rt_sext((bits), (w))
#define CINT_RT_NARROW_U(bits, w) ((bits) & cint_rt_mask(w))

/* Shift counts of every integer type. */
#define CINT_RT_COUNT(T, CT, TAG, K)                         \
    static inline cint_count cint_count_##T(CT k)            \
    {                                                        \
        cint_count c;                                        \
        c.bits = CINT_RT_BITS_##K(k);                        \
        c.tag = (uint32_t)(TAG);                             \
        c.reserved = 0u;                                     \
        return c;                                            \
    }
CINT_RT_COUNT(i8, int8_t, CINT_TAG_I8, S)
CINT_RT_COUNT(i16, int16_t, CINT_TAG_I16, S)
CINT_RT_COUNT(i32, int32_t, CINT_TAG_I32, S)
CINT_RT_COUNT(i64, int64_t, CINT_TAG_I64, S)
CINT_RT_COUNT(u8, uint8_t, CINT_TAG_U8, U)
CINT_RT_COUNT(u16, uint16_t, CINT_TAG_U16, U)
CINT_RT_COUNT(u32, uint32_t, CINT_TAG_U32, U)
CINT_RT_COUNT(u64, uint64_t, CINT_TAG_U64, U)

/* ------------------------------------------------------------------------- */
/* 8. Overflow primitives.                                                    */
/*                                                                            */
/* bool cint_rt_<op>_ovf_<t>_p(CT a, CT b, CT *r)   portable body              */
/* bool cint_rt_<op>_ovf_<t>_b(CT a, CT b, CT *r)   builtin body (when present) */
/* for op in add, sub, mul. Both always write *r = wrap(T, a op b) and return  */
/* true exactly when a op b is outside T. The portable bodies follow SPEC-09   */
/* EMIT-04; the two are compared on the boundary matrix and on seeded random   */
/* pairs by rt/tests/test_rt.c (EMIT-05).                                     */

#define CINT_RT_PRIMS_NARROW_SIGNED(T, CT, W, TMIN, TMAX)                           \
    static inline bool cint_rt_add_ovf_##T##_p(CT a, CT b, CT *r)                   \
    {                                                                               \
        int64_t v = (int64_t)a + (int64_t)b;                                        \
        *r = (CT)cint_rt_sext((uint64_t)v, (W));                                    \
        return v < (TMIN) || v > (TMAX);                                            \
    }                                                                               \
    static inline bool cint_rt_sub_ovf_##T##_p(CT a, CT b, CT *r)                   \
    {                                                                               \
        int64_t v = (int64_t)a - (int64_t)b;                                        \
        *r = (CT)cint_rt_sext((uint64_t)v, (W));                                    \
        return v < (TMIN) || v > (TMAX);                                            \
    }                                                                               \
    static inline bool cint_rt_mul_ovf_##T##_p(CT a, CT b, CT *r)                   \
    {                                                                               \
        int64_t v = (int64_t)a * (int64_t)b; /* |v| <= 2^62 for W <= 32 */         \
        *r = (CT)cint_rt_sext((uint64_t)v, (W));                                    \
        return v < (TMIN) || v > (TMAX);                                            \
    }

#define CINT_RT_PRIMS_NARROW_UNSIGNED(T, CT, W, TMAX)                               \
    static inline bool cint_rt_add_ovf_##T##_p(CT a, CT b, CT *r)                   \
    {                                                                               \
        uint64_t v = (uint64_t)a + (uint64_t)b;                                     \
        *r = (CT)(v & cint_rt_mask(W));                                             \
        return v > (uint64_t)(TMAX);                                                \
    }                                                                               \
    static inline bool cint_rt_sub_ovf_##T##_p(CT a, CT b, CT *r)                   \
    {                                                                               \
        uint64_t v = (uint64_t)a - (uint64_t)b;                                     \
        *r = (CT)(v & cint_rt_mask(W));                                             \
        return a < b;                                                               \
    }                                                                               \
    static inline bool cint_rt_mul_ovf_##T##_p(CT a, CT b, CT *r)                   \
    {                                                                               \
        uint64_t v = (uint64_t)a * (uint64_t)b; /* < 2^64 for W <= 32 */            \
        *r = (CT)(v & cint_rt_mask(W));                                             \
        return v > (uint64_t)(TMAX);                                                \
    }

CINT_RT_PRIMS_NARROW_SIGNED(i8, int8_t, 8u, INT8_MIN, INT8_MAX)
CINT_RT_PRIMS_NARROW_SIGNED(i16, int16_t, 16u, INT16_MIN, INT16_MAX)
CINT_RT_PRIMS_NARROW_SIGNED(i32, int32_t, 32u, INT32_MIN, INT32_MAX)
CINT_RT_PRIMS_NARROW_UNSIGNED(u8, uint8_t, 8u, UINT8_MAX)
CINT_RT_PRIMS_NARROW_UNSIGNED(u16, uint16_t, 16u, UINT16_MAX)
CINT_RT_PRIMS_NARROW_UNSIGNED(u32, uint32_t, 32u, UINT32_MAX)

static inline bool cint_rt_add_ovf_i64_p(int64_t a, int64_t b, int64_t *r)
{
    *r = cint_rt_sext((uint64_t)a + (uint64_t)b, 64u);
    return (b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b);
}

static inline bool cint_rt_sub_ovf_i64_p(int64_t a, int64_t b, int64_t *r)
{
    *r = cint_rt_sext((uint64_t)a - (uint64_t)b, 64u);
    return (b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b);
}

/* Magnitudes in uint64_t, no signed overflow (SPEC-09 EMIT-04). */
static inline bool cint_rt_mul_ovf_i64_p(int64_t a, int64_t b, int64_t *r)
{
    uint64_t ua = a < 0 ? (uint64_t)0 - (uint64_t)a : (uint64_t)a;
    uint64_t ub = b < 0 ? (uint64_t)0 - (uint64_t)b : (uint64_t)b;
    bool negative = (a < 0) != (b < 0);
    uint64_t limit = negative ? (uint64_t)INT64_MAX + 1u : (uint64_t)INT64_MAX;
    *r = cint_rt_sext((uint64_t)a * (uint64_t)b, 64u);
    return ub != 0u && ua > limit / ub;
}

static inline bool cint_rt_add_ovf_u64_p(uint64_t a, uint64_t b, uint64_t *r)
{
    *r = a + b;
    return *r < a;
}

static inline bool cint_rt_sub_ovf_u64_p(uint64_t a, uint64_t b, uint64_t *r)
{
    *r = a - b;
    return a < b;
}

static inline bool cint_rt_mul_ovf_u64_p(uint64_t a, uint64_t b, uint64_t *r)
{
    *r = a * b;
    return a != 0u && b > UINT64_MAX / a;
}

#if defined(CINT_RT_BUILTINS_GNU)
/* GCC and Clang: the builtins compute the infinitely precise result, store it
 * wrapped to the result type, and report whether it fits [GCC manual, "Integer
 * Overflow Builtins"; Clang language extensions, "Checked Arithmetic"]. */
#define CINT_RT_PRIMS_GNU(T, CT)                                                    \
    static inline bool cint_rt_add_ovf_##T##_b(CT a, CT b, CT *r)                   \
    {                                                                               \
        return __builtin_add_overflow(a, b, r);                                     \
    }                                                                               \
    static inline bool cint_rt_sub_ovf_##T##_b(CT a, CT b, CT *r)                   \
    {                                                                               \
        return __builtin_sub_overflow(a, b, r);                                     \
    }                                                                               \
    static inline bool cint_rt_mul_ovf_##T##_b(CT a, CT b, CT *r)                   \
    {                                                                               \
        return __builtin_mul_overflow(a, b, r);                                     \
    }
CINT_RT_PRIMS_GNU(i8, int8_t)
CINT_RT_PRIMS_GNU(i16, int16_t)
CINT_RT_PRIMS_GNU(i32, int32_t)
CINT_RT_PRIMS_GNU(i64, int64_t)
CINT_RT_PRIMS_GNU(u8, uint8_t)
CINT_RT_PRIMS_GNU(u16, uint16_t)
CINT_RT_PRIMS_GNU(u32, uint32_t)
CINT_RT_PRIMS_GNU(u64, uint64_t)
#elif defined(CINT_RT_BUILTINS_MSVC)
/* MSVC: 64-bit multiply through the 128-bit product intrinsics (EMIT-05);
 * every other primitive uses the portable body. */
#define CINT_RT_PRIMS_ALIAS(OP, T, CT)                                              \
    static inline bool cint_rt_##OP##_ovf_##T##_b(CT a, CT b, CT *r)                \
    {                                                                               \
        return cint_rt_##OP##_ovf_##T##_p(a, b, r);                                 \
    }
#define CINT_RT_PRIMS_ALIAS3(T, CT) \
    CINT_RT_PRIMS_ALIAS(add, T, CT) CINT_RT_PRIMS_ALIAS(sub, T, CT) CINT_RT_PRIMS_ALIAS(mul, T, CT)
CINT_RT_PRIMS_ALIAS3(i8, int8_t)
CINT_RT_PRIMS_ALIAS3(i16, int16_t)
CINT_RT_PRIMS_ALIAS3(i32, int32_t)
CINT_RT_PRIMS_ALIAS3(u8, uint8_t)
CINT_RT_PRIMS_ALIAS3(u16, uint16_t)
CINT_RT_PRIMS_ALIAS3(u32, uint32_t)
CINT_RT_PRIMS_ALIAS(add, i64, int64_t)
CINT_RT_PRIMS_ALIAS(sub, i64, int64_t)
CINT_RT_PRIMS_ALIAS(add, u64, uint64_t)
CINT_RT_PRIMS_ALIAS(sub, u64, uint64_t)

static inline bool cint_rt_mul_ovf_i64_b(int64_t a, int64_t b, int64_t *r)
{
#if defined(_M_X64)
    int64_t hi = 0;
    int64_t lo = _mul128(a, b, &hi);
#else
    int64_t hi = __mulh(a, b);
    int64_t lo = cint_rt_sext((uint64_t)a * (uint64_t)b, 64u);
#endif
    *r = lo;
    return hi != (lo < 0 ? -1 : 0);
}

static inline bool cint_rt_mul_ovf_u64_b(uint64_t a, uint64_t b, uint64_t *r)
{
#if defined(_M_X64)
    uint64_t hi = 0;
    *r = _umul128(a, b, &hi);
#else
    uint64_t hi = __umulh(a, b);
    *r = a * b;
#endif
    return hi != 0u;
}
#endif

#if CINT_RT_USE_BUILTINS
#define CINT_RT_OVF(OP, T) cint_rt_##OP##_ovf_##T##_b
#else
#define CINT_RT_OVF(OP, T) cint_rt_##OP##_ovf_##T##_p
#endif

/* ------------------------------------------------------------------------- */
/* 9. Shift primitives: 0 <= k <= w - 1 already checked.                      */
/*                                                                            */
/* bool cint_rt_shl_ovf_<t>(CT a, unsigned k, CT *r): *r = wrap(T, a * 2^k);  */
/* returns true exactly when a * 2^k is outside T (EMIT-10).                  */

#define CINT_RT_SHL_NARROW_SIGNED(T, CT, W, TMIN, TMAX)                             \
    static inline bool cint_rt_shl_ovf_##T(CT a, unsigned k, CT *r)                 \
    {                                                                               \
        int64_t v = (int64_t)a * ((int64_t)1 << k); /* |v| <= 2^62 */              \
        *r = (CT)cint_rt_sext((uint64_t)v, (W));                                    \
        return v < (TMIN) || v > (TMAX);                                            \
    }
#define CINT_RT_SHL_NARROW_UNSIGNED(T, CT, W, TMAX)                                 \
    static inline bool cint_rt_shl_ovf_##T(CT a, unsigned k, CT *r)                 \
    {                                                                               \
        uint64_t v = (uint64_t)a << k; /* < 2^63 */                                 \
        *r = (CT)(v & cint_rt_mask(W));                                             \
        return v > (uint64_t)(TMAX);                                                \
    }
CINT_RT_SHL_NARROW_SIGNED(i8, int8_t, 8u, INT8_MIN, INT8_MAX)
CINT_RT_SHL_NARROW_SIGNED(i16, int16_t, 16u, INT16_MIN, INT16_MAX)
CINT_RT_SHL_NARROW_SIGNED(i32, int32_t, 32u, INT32_MIN, INT32_MAX)
CINT_RT_SHL_NARROW_UNSIGNED(u8, uint8_t, 8u, UINT8_MAX)
CINT_RT_SHL_NARROW_UNSIGNED(u16, uint16_t, 16u, UINT16_MAX)
CINT_RT_SHL_NARROW_UNSIGNED(u32, uint32_t, 32u, UINT32_MAX)

/* I64: shift the unsigned pattern, then shift back arithmetically (EMIT-10). */
static inline bool cint_rt_shl_ovf_i64(int64_t a, unsigned k, int64_t *r)
{
    int64_t v = cint_rt_sext((uint64_t)a << k, 64u);
    *r = v;
    return cint_rt_asr(v, k) != a;
}

static inline bool cint_rt_shl_ovf_u64(uint64_t a, unsigned k, uint64_t *r)
{
    *r = a << k;
    return (*r >> k) != a;
}

/* ------------------------------------------------------------------------- */
/* 10. Arithmetic helpers.                                                    */
/*                                                                            */
/* For each t in i8 i16 i32 i64 u8 u16 u32 u64 with C type CT:                */
/*   bool cint_add_<t>(cint_ctx *, cint_site, CT a, CT b, CT *out)   add.checked.<t>      */
/*   bool cint_sub_<t>(...)  bool cint_mul_<t>(...)                   sub, mul.checked.<t> */
/*   bool cint_div_<t>(...)  bool cint_rem_<t>(...)   floor division (SPEC-01 4.4)       */
/*   bool cint_neg_<t>(cint_ctx *, cint_site, CT a, CT *out)         neg.checked.<t>      */
/*   bool cint_shl_<t>(cint_ctx *, cint_site, CT a, cint_count k, CT *out)  shl.checked  */
/*   bool cint_shl_wrap_<t>(...)   shl.wrap.<t>: E_SHIFT only                             */
/*   bool cint_shr_<t>(...)        shr.checked.<t>: E_SHIFT only                          */
/*   CT cint_add_wrap_<t>(CT a, CT b), cint_sub_wrap_<t>, cint_mul_wrap_<t>               */
/*   CT cint_neg_wrap_<t>(CT a)    0 -% a                                                 */
/*   CT cint_add_sat_<t>(CT a, CT b), cint_sub_sat_<t>, cint_mul_sat_<t>   <op>.sat.<t>:  */
/*       sat(T, exact): MIN(T) or MAX(T) by the sign of the exact result (SPEC-01 4.1)    */
/* There is no wrapping or saturating division (SPEC-01 4.4).                            */

#define CINT_RT_ARITH(T, CT, TAG, K, W)                                                       \
    static inline bool cint_add_##T(cint_ctx *ctx, cint_site site, CT a, CT b, CT *out)        \
    {                                                                                         \
        CT r;                                                                                 \
        bool overflow = CINT_RT_OVF(add, T)(a, b, &r);                                        \
        if (cint_rt_faulted(ctx)) {                                                           \
            return false;                                                                     \
        }                                                                                     \
        if (overflow) {                                                                       \
            return cint_rt_fault_arith(ctx, site, CINT_RT_OP_ADD, (TAG), CINT_RT_BITS_##K(a), \
                                       CINT_RT_BITS_##K(b));                                  \
        }                                                                                     \
        *out = r;                                                                             \
        return true;                                                                          \
    }                                                                                         \
    static inline bool cint_sub_##T(cint_ctx *ctx, cint_site site, CT a, CT b, CT *out)        \
    {                                                                                         \
        CT r;                                                                                 \
        bool overflow = CINT_RT_OVF(sub, T)(a, b, &r);                                        \
        if (cint_rt_faulted(ctx)) {                                                           \
            return false;                                                                     \
        }                                                                                     \
        if (overflow) {                                                                       \
            return cint_rt_fault_arith(ctx, site, CINT_RT_OP_SUB, (TAG), CINT_RT_BITS_##K(a), \
                                       CINT_RT_BITS_##K(b));                                  \
        }                                                                                     \
        *out = r;                                                                             \
        return true;                                                                          \
    }                                                                                         \
    static inline bool cint_mul_##T(cint_ctx *ctx, cint_site site, CT a, CT b, CT *out)        \
    {                                                                                         \
        CT r;                                                                                 \
        bool overflow = CINT_RT_OVF(mul, T)(a, b, &r);                                        \
        if (cint_rt_faulted(ctx)) {                                                           \
            return false;                                                                     \
        }                                                                                     \
        if (overflow) {                                                                       \
            return cint_rt_fault_arith(ctx, site, CINT_RT_OP_MUL, (TAG), CINT_RT_BITS_##K(a), \
                                       CINT_RT_BITS_##K(b));                                  \
        }                                                                                     \
        *out = r;                                                                             \
        return true;                                                                          \
    }                                                                                         \
    static inline bool cint_neg_##T(cint_ctx *ctx, cint_site site, CT a, CT *out)              \
    {                                                                                         \
        CT r;                                                                                 \
        bool overflow = CINT_RT_OVF(sub, T)((CT)0, a, &r);                                    \
        if (cint_rt_faulted(ctx)) {                                                           \
            return false;                                                                     \
        }                                                                                     \
        if (overflow) {                                                                       \
            return cint_rt_fault_arith(ctx, site, CINT_RT_OP_NEG, (TAG), CINT_RT_BITS_##K(a), 0u); \
        }                                                                                     \
        *out = r;                                                                             \
        return true;                                                                          \
    }                                                                                         \
    static inline CT cint_add_wrap_##T(CT a, CT b)                                            \
    {                                                                                         \
        CT r;                                                                                 \
        (void)CINT_RT_OVF(add, T)(a, b, &r);                                                  \
        return r;                                                                             \
    }                                                                                         \
    static inline CT cint_sub_wrap_##T(CT a, CT b)                                            \
    {                                                                                         \
        CT r;                                                                                 \
        (void)CINT_RT_OVF(sub, T)(a, b, &r);                                                  \
        return r;                                                                             \
    }                                                                                         \
    static inline CT cint_mul_wrap_##T(CT a, CT b)                                            \
    {                                                                                         \
        CT r;                                                                                 \
        (void)CINT_RT_OVF(mul, T)(a, b, &r);                                                  \
        return r;                                                                             \
    }                                                                                         \
    static inline CT cint_neg_wrap_##T(CT a)                                                  \
    {                                                                                         \
        return cint_sub_wrap_##T((CT)0, a);                                                   \
    }                                                                                         \
    static inline bool cint_shl_##T(cint_ctx *ctx, cint_site site, CT a, cint_count k, CT *out) \
    {                                                                                         \
        CT r = 0;                                                                             \
        bool bad = k.bits > (uint64_t)((W) - 1u);                                             \
        bool overflow = !bad && cint_rt_shl_ovf_##T(a, (unsigned)k.bits, &r);                 \
        if (cint_rt_faulted(ctx)) {                                                           \
            return false;                                                                     \
        }                                                                                     \
        if (bad || overflow) {                                                                \
            return cint_rt_fault_shift(ctx, site, CINT_RT_OP_SHL, (TAG), CINT_RT_BITS_##K(a), k); \
        }                                                                                     \
        *out = r;                                                                             \
        return true;                                                                          \
    }                                                                                         \
    static inline bool cint_shl_wrap_##T(cint_ctx *ctx, cint_site site, CT a, cint_count k, CT *out) \
    {                                                                                         \
        CT r = 0;                                                                             \
        bool bad = k.bits > (uint64_t)((W) - 1u);                                             \
        if (!bad) {                                                                           \
            (void)cint_rt_shl_ovf_##T(a, (unsigned)k.bits, &r);                               \
        }                                                                                     \
        if (cint_rt_faulted(ctx)) {                                                           \
            return false;                                                                     \
        }                                                                                     \
        if (bad) {                                                                            \
            return cint_rt_fault_shift(ctx, site, CINT_RT_OP_SHL_WRAP, (TAG), CINT_RT_BITS_##K(a), k); \
        }                                                                                     \
        *out = r;                                                                             \
        return true;                                                                          \
    }

/* Signed division: floor quotient and remainder from C's truncating ones, with
 * the divisor checked for 0 and the pair (MIN, -1) handled before any C `/` or
 * `%` (SPEC-01 4.4, SPEC-09 EMIT-09). */
#define CINT_RT_DIV_SIGNED(T, CT, TAG, TMIN)                                                  \
    static inline bool cint_div_##T(cint_ctx *ctx, cint_site site, CT a, CT b, CT *out)        \
    {                                                                                         \
        int64_t x = (int64_t)a, y = (int64_t)b, q = 0;                                        \
        bool bad = y == 0 || (x == (int64_t)(TMIN) && y == -1);                               \
        if (!bad) {                                                                           \
            int64_t m = x % y;                                                                \
            q = x / y;                                                                        \
            if (m != 0 && ((m < 0) != (y < 0))) {                                             \
                q -= 1;                                                                       \
            }                                                                                 \
        }                                                                                     \
        if (cint_rt_faulted(ctx)) {                                                           \
            return false;                                                                     \
        }                                                                                     \
        if (bad) {                                                                            \
            return cint_rt_fault_arith(ctx, site, CINT_RT_OP_DIV, (TAG), (uint64_t)x, (uint64_t)y); \
        }                                                                                     \
        *out = (CT)q;                                                                         \
        return true;                                                                          \
    }                                                                                         \
    static inline bool cint_rem_##T(cint_ctx *ctx, cint_site site, CT a, CT b, CT *out)        \
    {                                                                                         \
        int64_t x = (int64_t)a, y = (int64_t)b, m = 0;                                        \
        bool bad = y == 0;                                                                    \
        if (!bad && y != -1) {                                                                \
            m = x % y;                                                                        \
            if (m != 0 && ((m < 0) != (y < 0))) {                                             \
                m += y;                                                                       \
            }                                                                                 \
        }                                                                                     \
        if (cint_rt_faulted(ctx)) {                                                           \
            return false;                                                                     \
        }                                                                                     \
        if (bad) {                                                                            \
            return cint_rt_fault_arith(ctx, site, CINT_RT_OP_REM, (TAG), (uint64_t)x, (uint64_t)y); \
        }                                                                                     \
        *out = (CT)m;                                                                         \
        return true;                                                                          \
    }                                                                                         \
    static inline bool cint_shr_##T(cint_ctx *ctx, cint_site site, CT a, cint_count k, CT *out) \
    {                                                                                         \
        bool bad = k.bits > (uint64_t)(sizeof(CT) * 8u - 1u);                                 \
        int64_t r = bad ? 0 : cint_rt_asr((int64_t)a, (unsigned)k.bits);                      \
        if (cint_rt_faulted(ctx)) {                                                           \
            return false;                                                                     \
        }                                                                                     \
        if (bad) {                                                                            \
            return cint_rt_fault_shift(ctx, site, CINT_RT_OP_SHR, (TAG), CINT_RT_BITS_S(a), k); \
        }                                                                                     \
        *out = (CT)r;                                                                         \
        return true;                                                                          \
    }

#define CINT_RT_DIV_UNSIGNED(T, CT, TAG)                                                      \
    static inline bool cint_div_##T(cint_ctx *ctx, cint_site site, CT a, CT b, CT *out)        \
    {                                                                                         \
        uint64_t x = (uint64_t)a, y = (uint64_t)b;                                            \
        uint64_t q = y == 0u ? 0u : x / y;                                                    \
        if (cint_rt_faulted(ctx)) {                                                           \
            return false;                                                                     \
        }                                                                                     \
        if (y == 0u) {                                                                        \
            return cint_rt_fault_arith(ctx, site, CINT_RT_OP_DIV, (TAG), x, y);               \
        }                                                                                     \
        *out = (CT)q;                                                                         \
        return true;                                                                          \
    }                                                                                         \
    static inline bool cint_rem_##T(cint_ctx *ctx, cint_site site, CT a, CT b, CT *out)        \
    {                                                                                         \
        uint64_t x = (uint64_t)a, y = (uint64_t)b;                                            \
        uint64_t m = y == 0u ? 0u : x % y;                                                    \
        if (cint_rt_faulted(ctx)) {                                                           \
            return false;                                                                     \
        }                                                                                     \
        if (y == 0u) {                                                                        \
            return cint_rt_fault_arith(ctx, site, CINT_RT_OP_REM, (TAG), x, y);               \
        }                                                                                     \
        *out = (CT)m;                                                                         \
        return true;                                                                          \
    }                                                                                         \
    static inline bool cint_shr_##T(cint_ctx *ctx, cint_site site, CT a, cint_count k, CT *out) \
    {                                                                                         \
        bool bad = k.bits > (uint64_t)(sizeof(CT) * 8u - 1u);                                 \
        uint64_t r = bad ? 0u : (uint64_t)a >> k.bits;                                        \
        if (cint_rt_faulted(ctx)) {                                                           \
            return false;                                                                     \
        }                                                                                     \
        if (bad) {                                                                            \
            return cint_rt_fault_shift(ctx, site, CINT_RT_OP_SHR, (TAG), (uint64_t)a, k);     \
        }                                                                                     \
        *out = (CT)r;                                                                         \
        return true;                                                                          \
    }

/* Saturating forms: on overflow the exact result lies beyond MIN or MAX on the side
 * its sign gives: a's for add and sub (signed), the operands' signs for mul. */
#define CINT_RT_SAT_BODY(OP, T, CT, ON_OVERFLOW)                                   \
    static inline CT cint_##OP##_sat_##T(CT a, CT b)                               \
    {                                                                              \
        CT r = 0;                                                                  \
        if (CINT_RT_OVF(OP, T)(a, b, &r)) {                                        \
            ON_OVERFLOW                                                            \
        }                                                                          \
        return r;                                                                  \
    }
#define CINT_RT_SAT_SIGNED(T, CT, TMIN, TMAX)                                      \
    CINT_RT_SAT_BODY(add, T, CT, r = (CT)(TMAX); if (a < 0) { r = (CT)(TMIN); })   \
    CINT_RT_SAT_BODY(sub, T, CT, r = (CT)(TMAX); if (a < 0) { r = (CT)(TMIN); })   \
    CINT_RT_SAT_BODY(mul, T, CT, r = (CT)(TMAX); if ((a < 0) != (b < 0)) { r = (CT)(TMIN); })
#define CINT_RT_SAT_UNSIGNED(T, CT, TMAX)                                          \
    CINT_RT_SAT_BODY(add, T, CT, r = (CT)(TMAX);)                                  \
    CINT_RT_SAT_BODY(sub, T, CT, r = 0;)                                           \
    CINT_RT_SAT_BODY(mul, T, CT, r = (CT)(TMAX);)

CINT_RT_ARITH(i8, int8_t, CINT_TAG_I8, S, 8u)
CINT_RT_ARITH(i16, int16_t, CINT_TAG_I16, S, 16u)
CINT_RT_ARITH(i32, int32_t, CINT_TAG_I32, S, 32u)
CINT_RT_ARITH(i64, int64_t, CINT_TAG_I64, S, 64u)
CINT_RT_ARITH(u8, uint8_t, CINT_TAG_U8, U, 8u)
CINT_RT_ARITH(u16, uint16_t, CINT_TAG_U16, U, 16u)
CINT_RT_ARITH(u32, uint32_t, CINT_TAG_U32, U, 32u)
CINT_RT_ARITH(u64, uint64_t, CINT_TAG_U64, U, 64u)
CINT_RT_SAT_SIGNED(i8, int8_t, INT8_MIN, INT8_MAX)
CINT_RT_SAT_SIGNED(i16, int16_t, INT16_MIN, INT16_MAX)
CINT_RT_SAT_SIGNED(i32, int32_t, INT32_MIN, INT32_MAX)
CINT_RT_SAT_SIGNED(i64, int64_t, INT64_MIN, INT64_MAX)
CINT_RT_SAT_UNSIGNED(u8, uint8_t, UINT8_MAX)
CINT_RT_SAT_UNSIGNED(u16, uint16_t, UINT16_MAX)
CINT_RT_SAT_UNSIGNED(u32, uint32_t, UINT32_MAX)
CINT_RT_SAT_UNSIGNED(u64, uint64_t, UINT64_MAX)
CINT_RT_DIV_SIGNED(i8, int8_t, CINT_TAG_I8, INT8_MIN)
CINT_RT_DIV_SIGNED(i16, int16_t, CINT_TAG_I16, INT16_MIN)
CINT_RT_DIV_SIGNED(i32, int32_t, CINT_TAG_I32, INT32_MIN)
CINT_RT_DIV_SIGNED(i64, int64_t, CINT_TAG_I64, INT64_MIN)
CINT_RT_DIV_UNSIGNED(u8, uint8_t, CINT_TAG_U8)
CINT_RT_DIV_UNSIGNED(u16, uint16_t, CINT_TAG_U16)
CINT_RT_DIV_UNSIGNED(u32, uint32_t, CINT_TAG_U32)
CINT_RT_DIV_UNSIGNED(u64, uint64_t, CINT_TAG_U64)

/* ------------------------------------------------------------------------- */
/* 10a. Result-returning forms (SPEC-01 IM-30, IM-188; SPEC-04 LS-96).        */
/*                                                                            */
/* For each t in i8 i16 i32 i64 u8 u16 u32 u64 with C type CT:                */
/*   uint16_t cint_add_result_<t>(CT a, CT b, CT *out)   add_result: ArithError!T          */
/*   uint16_t cint_sub_result_<t>(...), cint_mul_result_<t>(...)   sub_result, mul_result  */
/*   uint16_t cint_div_result_<t>(...), cint_rem_result_<t>(...)   floor division (4.4)    */
/*   uint16_t cint_shl_result_<t>(CT a, cint_count k, CT *out)     shl_result              */
/* Each takes no context and never faults. Where the checked form cint_<op>_<t>          */
/* succeeds, it writes the same value to *out and returns 0; where that form faults, it   */
/* leaves *out unwritten and returns the ArithError tag that mirrors the fault code       */
/* (IM-188): CINT_ARITH_OVERFLOW for E_OVERFLOW (MIN / -1 included), CINT_ARITH_DIV_ZERO  */
/* for E_DIV_ZERO, and CINT_ARITH_SHIFT for E_SHIFT, a count of any integer type outside  */
/* 0..w-1. MIN % -1 is 0, as for cint_rem_<t>. CINT_ARITH_NARROW is the tag of `as?`.     */

#define CINT_ARITH_OVERFLOW ((uint16_t)1)
#define CINT_ARITH_DIV_ZERO ((uint16_t)2)
#define CINT_ARITH_SHIFT ((uint16_t)3)
#define CINT_ARITH_NARROW ((uint16_t)4)

#define CINT_RT_RESULT_BODY(OP, T, CT)                                                        \
    static inline uint16_t cint_##OP##_result_##T(CT a, CT b, CT *out)                        \
    {                                                                                         \
        CT r = 0;                                                                             \
        if (CINT_RT_OVF(OP, T)(a, b, &r)) {                                                   \
            return CINT_ARITH_OVERFLOW;                                                       \
        }                                                                                     \
        *out = r;                                                                             \
        return 0u;                                                                            \
    }
#define CINT_RT_RESULT(T, CT, W)                                                              \
    CINT_RT_RESULT_BODY(add, T, CT)                                                           \
    CINT_RT_RESULT_BODY(sub, T, CT)                                                           \
    CINT_RT_RESULT_BODY(mul, T, CT)                                                           \
    static inline uint16_t cint_shl_result_##T(CT a, cint_count k, CT *out)                   \
    {                                                                                         \
        CT r = 0;                                                                             \
        if (k.bits > (uint64_t)((W) - 1u)) {                                                  \
            return CINT_ARITH_SHIFT;                                                          \
        }                                                                                     \
        if (cint_rt_shl_ovf_##T(a, (unsigned)k.bits, &r)) {                                   \
            return CINT_ARITH_OVERFLOW;                                                       \
        }                                                                                     \
        *out = r;                                                                             \
        return 0u;                                                                            \
    }

/* The checked forms' floor division (CINT_RT_DIV_SIGNED): 0 and (MIN, -1) are
 * decided before any C `/` or `%` (SPEC-09 EMIT-09). */
#define CINT_RT_DIV_RESULT_SIGNED(T, CT, TMIN)                                                \
    static inline uint16_t cint_div_result_##T(CT a, CT b, CT *out)                           \
    {                                                                                         \
        int64_t x = (int64_t)a, y = (int64_t)b, q, m;                                         \
        if (y == 0) {                                                                         \
            return CINT_ARITH_DIV_ZERO;                                                       \
        }                                                                                     \
        if (x == (int64_t)(TMIN) && y == -1) {                                                \
            return CINT_ARITH_OVERFLOW;                                                       \
        }                                                                                     \
        m = x % y;                                                                            \
        q = x / y;                                                                            \
        if (m != 0 && ((m < 0) != (y < 0))) {                                                 \
            q -= 1;                                                                           \
        }                                                                                     \
        *out = (CT)q;                                                                         \
        return 0u;                                                                            \
    }                                                                                         \
    static inline uint16_t cint_rem_result_##T(CT a, CT b, CT *out)                           \
    {                                                                                         \
        int64_t x = (int64_t)a, y = (int64_t)b, m = 0;                                        \
        if (y == 0) {                                                                         \
            return CINT_ARITH_DIV_ZERO;                                                       \
        }                                                                                     \
        if (y != -1) {                                                                        \
            m = x % y;                                                                        \
            if (m != 0 && ((m < 0) != (y < 0))) {                                             \
                m += y;                                                                       \
            }                                                                                 \
        }                                                                                     \
        *out = (CT)m;                                                                         \
        return 0u;                                                                            \
    }

#define CINT_RT_DIV_RESULT_UNSIGNED(T, CT)                                                    \
    static inline uint16_t cint_div_result_##T(CT a, CT b, CT *out)                           \
    {                                                                                         \
        if ((uint64_t)b == 0u) {                                                              \
            return CINT_ARITH_DIV_ZERO;                                                       \
        }                                                                                     \
        *out = (CT)((uint64_t)a / (uint64_t)b);                                               \
        return 0u;                                                                            \
    }                                                                                         \
    static inline uint16_t cint_rem_result_##T(CT a, CT b, CT *out)                           \
    {                                                                                         \
        if ((uint64_t)b == 0u) {                                                              \
            return CINT_ARITH_DIV_ZERO;                                                       \
        }                                                                                     \
        *out = (CT)((uint64_t)a % (uint64_t)b);                                               \
        return 0u;                                                                            \
    }

CINT_RT_RESULT(i8, int8_t, 8u)
CINT_RT_RESULT(i16, int16_t, 16u)
CINT_RT_RESULT(i32, int32_t, 32u)
CINT_RT_RESULT(i64, int64_t, 64u)
CINT_RT_RESULT(u8, uint8_t, 8u)
CINT_RT_RESULT(u16, uint16_t, 16u)
CINT_RT_RESULT(u32, uint32_t, 32u)
CINT_RT_RESULT(u64, uint64_t, 64u)
CINT_RT_DIV_RESULT_SIGNED(i8, int8_t, INT8_MIN)
CINT_RT_DIV_RESULT_SIGNED(i16, int16_t, INT16_MIN)
CINT_RT_DIV_RESULT_SIGNED(i32, int32_t, INT32_MIN)
CINT_RT_DIV_RESULT_SIGNED(i64, int64_t, INT64_MIN)
CINT_RT_DIV_RESULT_UNSIGNED(u8, uint8_t)
CINT_RT_DIV_RESULT_UNSIGNED(u16, uint16_t)
CINT_RT_DIV_RESULT_UNSIGNED(u32, uint32_t)
CINT_RT_DIV_RESULT_UNSIGNED(u64, uint64_t)

/* ------------------------------------------------------------------------- */
/* 11. Conversions (SPEC-01 4.9, SPEC-09 EMIT-11).                            */
/*                                                                            */
/* For every pair of types (to, from), including to == from:                 */
/*   bool cint_as_<to>_from_<from>(cint_ctx *, cint_site, CF x, CT *out)      */
/*       as.checked.<from>.<to>: E_NARROW when x is outside <to>              */
/*   CT cint_as_wrap_<to>_from_<from>(CF x)    as.wrap.<from>.<to>: wrap      */
/* Widening conversions never fault (the range test is then always true).    */

#define CINT_RT_CONV(D, CD, DTAG, DK, DW, S, CS, STAG, SK)                                   \
    static inline bool cint_as_##D##_from_##S(cint_ctx *ctx, cint_site site, CS x, CD *out)  \
    {                                                                                       \
        uint64_t bits = CINT_RT_BITS_##SK(x);                                               \
        bool fits = cint_rt_fits(bits, CINT_RT_SIGNED_##SK, (DTAG));                        \
        if (cint_rt_faulted(ctx)) {                                                         \
            return false;                                                                   \
        }                                                                                   \
        if (!fits) {                                                                        \
            return cint_rt_fault_narrow(ctx, site, (STAG), (DTAG), bits);                   \
        }                                                                                   \
        *out = (CD)CINT_RT_NARROW_##DK(bits, (DW));                                         \
        return true;                                                                        \
    }                                                                                       \
    static inline CD cint_as_wrap_##D##_from_##S(CS x)                                      \
    {                                                                                       \
        return (CD)CINT_RT_NARROW_##DK(CINT_RT_BITS_##SK(x), (DW));                         \
    }

#define CINT_RT_CONV_TO(D, CD, DTAG, DK, DW)                                \
    CINT_RT_CONV(D, CD, DTAG, DK, DW, i8, int8_t, CINT_TAG_I8, S)           \
    CINT_RT_CONV(D, CD, DTAG, DK, DW, i16, int16_t, CINT_TAG_I16, S)        \
    CINT_RT_CONV(D, CD, DTAG, DK, DW, i32, int32_t, CINT_TAG_I32, S)        \
    CINT_RT_CONV(D, CD, DTAG, DK, DW, i64, int64_t, CINT_TAG_I64, S)        \
    CINT_RT_CONV(D, CD, DTAG, DK, DW, u8, uint8_t, CINT_TAG_U8, U)          \
    CINT_RT_CONV(D, CD, DTAG, DK, DW, u16, uint16_t, CINT_TAG_U16, U)       \
    CINT_RT_CONV(D, CD, DTAG, DK, DW, u32, uint32_t, CINT_TAG_U32, U)       \
    CINT_RT_CONV(D, CD, DTAG, DK, DW, u64, uint64_t, CINT_TAG_U64, U)

CINT_RT_CONV_TO(i8, int8_t, CINT_TAG_I8, S, 8u)
CINT_RT_CONV_TO(i16, int16_t, CINT_TAG_I16, S, 16u)
CINT_RT_CONV_TO(i32, int32_t, CINT_TAG_I32, S, 32u)
CINT_RT_CONV_TO(i64, int64_t, CINT_TAG_I64, S, 64u)
CINT_RT_CONV_TO(u8, uint8_t, CINT_TAG_U8, U, 8u)
CINT_RT_CONV_TO(u16, uint16_t, CINT_TAG_U16, U, 16u)
CINT_RT_CONV_TO(u32, uint32_t, CINT_TAG_U32, U, 32u)
CINT_RT_CONV_TO(u64, uint64_t, CINT_TAG_U64, U, 64u)

/* ------------------------------------------------------------------------- */
/* 12. Index checks (E_BOUNDS). Provisional record shape: operand I64 index,  */
/* limit I64 extent, operation supplied by the caller as index.checked.<E>    */
/* (rt/OPEN.md RT-OQ-05).                                                     */

static inline bool cint_index_check(cint_ctx *ctx, cint_site site, const char *operation,
                                    int64_t index, int64_t extent)
{
    bool inside = index >= 0 && index < extent;
    if (cint_rt_faulted(ctx)) {
        return false;
    }
    if (!inside) {
        /* Always false; returning the literal lets a C compiler see that no access
         * follows a failed check (GCC -O2 -Warray-bounds on a constant index). */
        (void)cint_rt_fault_index(ctx, site, operation, index, extent);
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------------- */
/* 13. Integer built-ins (SPEC-01 2.4, 4.3 to 4.10; operation names IM-134).  */
/*                                                                            */
/* uabs, min, max and mul_full never fault: cint_<name>_<t>(a[, b]) with t    */
/* the operand type. The others go through cint_rt_builtin: op the built-in's */
/* number (1 abs, 5 clamp, 6 div_trunc, 7 rem_trunc, 8 div_euclid,            */
/* 9 rem_euclid, 10 div_round, 11 muldiv, 13 isqrt, 14 isqrt_round, 15 rotl,  */
/* 16 rotr), tag the operand type, mode the rounding mode of SPEC-01 4.5      */
/* numbered from 1 in its order (floor ... half_down; 0 for none), aux the    */
/* count's tag (rotl, rotr) or the result's (muldiv), and a, b, c the operand */
/* patterns. On success *out holds the result's pattern; otherwise the record */
/* is written: E_OVERFLOW with exact and limit, E_DIV_ZERO, E_SHIFT with      */
/* limit I64 w - 1, E_DOMAIN (isqrt of a negative, clamp with lo > hi).       */
/* cint_builtin_<r>(..., CT *out) narrows *out to the result type r.         */

bool cint_rt_builtin(cint_ctx *ctx, cint_site site, uint32_t op, uint32_t tag, uint32_t mode, uint32_t aux,
                     uint64_t a, uint64_t b, uint64_t c, uint64_t *out);

#define CINT_RT_BUILTIN(T, CT, K, W)                                                                \
    static inline bool cint_builtin_##T(cint_ctx *ctx, cint_site site, uint32_t op, uint32_t tag,    \
                                        uint32_t mode, uint32_t aux, uint64_t a, uint64_t b,         \
                                        uint64_t c, CT *out)                                         \
    {                                                                                               \
        uint64_t bits;                                                                              \
        if (!cint_rt_builtin(ctx, site, op, tag, mode, aux, a, b, c, &bits)) {                      \
            return false;                                                                           \
        }                                                                                           \
        *out = (CT)CINT_RT_NARROW_##K(bits, (W));                                                   \
        return true;                                                                                \
    }                                                                                               \
    static inline CT cint_min_##T(CT a, CT b)                                                       \
    {                                                                                               \
        return (CT)(a < b ? a : b);                                                                 \
    }                                                                                               \
    static inline CT cint_max_##T(CT a, CT b)                                                       \
    {                                                                                               \
        return (CT)(a < b ? b : a);                                                                 \
    }
CINT_RT_BUILTIN(i8, int8_t, S, 8u)
CINT_RT_BUILTIN(i16, int16_t, S, 16u)
CINT_RT_BUILTIN(i32, int32_t, S, 32u)
CINT_RT_BUILTIN(i64, int64_t, S, 64u)
CINT_RT_BUILTIN(u8, uint8_t, U, 8u)
CINT_RT_BUILTIN(u16, uint16_t, U, 16u)
CINT_RT_BUILTIN(u32, uint32_t, U, 32u)
CINT_RT_BUILTIN(u64, uint64_t, U, 64u)

/* uabs.checked.<t>.<u>: |a| in the unsigned type of the same width. */
#define CINT_RT_UABS(T, CT, UT)                                          \
    static inline UT cint_uabs_##T(CT a)                                 \
    {                                                                    \
        return (UT)(a < 0 ? (UT)((UT)0 - (UT)a) : (UT)a);                \
    }
CINT_RT_UABS(i8, int8_t, uint8_t)
CINT_RT_UABS(i16, int16_t, uint16_t)
CINT_RT_UABS(i32, int32_t, uint32_t)
CINT_RT_UABS(i64, int64_t, uint64_t)

/* mul_full.checked.<t>.<t2>: the product in the type of twice the width (I8 to
 * I32, U8 to U32; the 64-bit operands are refused by the compilers). The
 * operands are widened first, so no product overflows or promotes to int. */
#define CINT_RT_MUL_FULL(T, CT, WT, RT)                                  \
    static inline RT cint_mul_full_##T(CT a, CT b)                       \
    {                                                                    \
        return (RT)((WT)a * (WT)b);                                      \
    }
CINT_RT_MUL_FULL(i8, int8_t, int32_t, int16_t)
CINT_RT_MUL_FULL(i16, int16_t, int32_t, int32_t)
CINT_RT_MUL_FULL(i32, int32_t, int64_t, int64_t)
CINT_RT_MUL_FULL(u8, uint8_t, uint32_t, uint16_t)
CINT_RT_MUL_FULL(u16, uint16_t, uint32_t, uint32_t)
CINT_RT_MUL_FULL(u32, uint32_t, uint64_t, uint64_t)

/* ------------------------------------------------------------------------- */
/* 14. Views of rank 1 to 4, slices, copies, reductions and kernels (roadmap  */
/* box 09; SPEC-02 V-1 to V-13, A-5 to A-8, F-1 to F-8; SPEC-01 IM-77 to      */
/* IM-87, IM-186, IM-187; rulings R1 to R10 of the box 09 design note). The   */
/* records are those of ref/cint_ref (exec.py, views.py, reduce.py).          */
/*                                                                            */
/* Every function of this section that checks something returns false on a  */
/* faulted context and writes nothing (RT-10), and a false return always     */
/* leaves a record: arguments that no emitted code passes (a NULL view, a    */
/* rank outside 1 to 4, an unknown tag or operator, an operation identifier  */
/* that is NULL, empty or longer than 64 bytes) record E_UNSUPPORTED         */
/* dispatch.admit (RT-OQ-20). Operands named below are I64 unless stated.    */
/* Views, descriptors, the dispatch address and scratch storage are          */
/* provisional (rt/OPEN.md RT-OQ-34).                                         */

/* A view as emitted code hands it to these helpers (SPEC-02 V-1, V-3): the
 * buffer's first element, the element offset of logical index 0, the rank (1
 * to 4), and per dimension the extent and the signed stride, in elements.
 * Entries at and above `rank` are 0. Two views are on one buffer exactly when
 * `base` is equal. The element at logical index (i0, ...) is at
 * (char *)base + size * (origin + sum ik * stride[k]). No pointer is formed
 * for an empty view (an extent 0), whose origin may lie outside the buffer. */
typedef struct cint_vdesc {
    void *base;
    int64_t origin;
    int64_t rank;
    int64_t shape[4];
    int64_t stride[4];
} cint_vdesc;
_Static_assert(sizeof(cint_vdesc) == 88 && offsetof(cint_vdesc, shape) == 24, "cint_vdesc");

/* Index checks of rank 2 and above (ruling R1): as cint_rt_fault_index and
 * cint_index_check, with operands dim (from 0) then index; limit the extent
 * of that dimension. Rank 1 keeps the one-operand form of section 12. */
bool cint_rt_fault_index_dim(cint_ctx *ctx, cint_site site, const char *operation, int64_t dim, int64_t index,
                             int64_t extent);

static inline bool cint_index_check_dim(cint_ctx *ctx, cint_site site, const char *operation, int64_t dim,
                                        int64_t index, int64_t extent)
{
    bool inside = index >= 0 && index < extent;
    if (cint_rt_faulted(ctx)) {
        return false;
    }
    if (!inside) {
        (void)cint_rt_fault_index_dim(ctx, site, operation, dim, index, extent);
        return false;
    }
    return true;
}

/* One slice item over a dimension of `extent` elements and stride `stride`,
 * with a constant step other than 0 (rulings R2, R3; SPEC-04 LS-161, LS-162):
 * views.py slice_dim in exact arithmetic. `lo` and `hi` count only when their
 * flag is set. Step s > 0: lo := 0 and hi := extent when omitted, end := hi + 1
 * for `..=`, else hi; valid when 0 <= lo <= end <= extent; first = lo, length
 * ceil((end - lo) / s); the record's bounds are (lo, hi). Step s < 0, k = -s:
 * first := extent - 1 when lo is omitted, else lo; with `..=`, last := hi (0
 * when omitted), valid when 0 <= last <= first + 1 <= extent, length
 * ceil((first - last + 1) / k) when first >= last, else 0; without, last := hi
 * (-1 when omitted), valid when -1 <= last <= first <= extent - 1, length
 * ceil((first - last) / k); the bounds are (first, last). Valid: *origin +=
 * first * stride (left unchanged when that overflows, which only an empty
 * result allows), *length, and *stride_out = stride * step, saturated to
 * INT64_MIN or INT64_MAX when it overflows (only a length of at most 1
 * allows that); true. Otherwise E_BOUNDS `operation` (slice.checked.<E>),
 * operands [dim when dim >= 0] and the two bounds, no exact, limit extent;
 * false. Emitted code passes dim -1 for an object of rank 1. */
#define CINT_SLICE_LO 1u         /* the low bound is written */
#define CINT_SLICE_HI 2u         /* the high bound is written */
#define CINT_SLICE_INCLUSIVE 4u  /* `..=` */
bool cint_rt_slice(cint_ctx *ctx, cint_site site, const char *operation, int64_t dim, int64_t extent,
                   int64_t stride, int64_t lo, int64_t hi, int64_t step, uint32_t flags, int64_t *origin,
                   int64_t *length, int64_t *stride_out);

/* The pairwise decision of SPEC-02 A-5 (views.py classify, the buffer being
 * `base`): 0 disjoint (T0 an empty view, different buffers, T1 bounding
 * intervals apart, T2 origins apart modulo the gcd of the strides of
 * dimensions longer than 1), 1 identical (T3: origin, rank, extents and
 * strides equal), 2 overlapping or uncertain (T4 and the rest). Dimensions of
 * extent 1 take no part in T1 and T2, so a saturated stride there is harmless;
 * a bounding interval that does not fit an I64 (no valid view has one) is 2. */
int cint_rt_view_relation(const cint_vdesc *p, const cint_vdesc *q);

/* copy(dst, src) and array assignment between views of equal rank (SPEC-01
 * IM-187; rulings R7, R8), with elements of `size` bytes: at the first
 * dimension d whose extents differ, E_SHAPE copy.shape as
 * cint_copy_shape_check (operands d and the source extent, limit the
 * destination extent); then relation 1 is no copy (true, nothing written) and
 * relation 2 is E_ALIAS copy.alias (no operands, exact or limit); otherwise
 * every element is copied with memcpy in logical (row-major) order. No fuel;
 * the fuel consumed is kept. */
bool cint_rt_copy_view(cint_ctx *ctx, cint_site site, const cint_vdesc *dst, const cint_vdesc *src, size_t size);

/* The entry alias check of a call (SPEC-04 LS-121, LS-123; REF-OQ-32): the
 * view of `inout` parameter `param` against parameter `other`. Relation 1 or 2
 * records E_ALIAS bind.alias, operands param and other, no exact or limit,
 * with the fuel consumed kept (cint_fault_alias, for a cint-abi-1 wrapper,
 * sets it to 0); false. */
bool cint_rt_call_alias(cint_ctx *ctx, cint_site site, uint32_t param, uint32_t other, const cint_vdesc *p,
                        const cint_vdesc *q);

/* cint_shape_check with the dimension: E_SHAPE bind.shape, operands param,
 * dim and actual, limit expected, fuel kept. Unlike cint_shape_check, false on
 * a faulted context even when the extents agree. */
bool cint_shape_check_dim(cint_ctx *ctx, cint_site site, uint32_t param, uint32_t dim, int64_t expected,
                          int64_t actual);

/* A declared extent below 0 (ruling R10): E_SHAPE decl.shape, operands dim
 * and the extent, no exact, limit I64 0; false. */
bool cint_rt_fault_decl_shape(cint_ctx *ctx, cint_site site, int64_t dim, int64_t extent);

/* Exact accumulation (SPEC-01 IM-77 to IM-87; rulings R5, R6). A cint_zacc is
 * a 256-bit two's complement integer, little-endian limbs, zero-initialized
 * by the caller; it holds every sum and dot product over a view (fewer than
 * 2^63 terms, each below 2^128 in magnitude). The add functions read `bits`
 * as a value of `tag` (I8 to U64; for any other tag the width is taken from
 * its low two bits, so no input is undefined, and cint_rt_zacc_fit refuses
 * it). */
typedef struct cint_zacc {
    uint64_t limb[4];
} cint_zacc;
_Static_assert(sizeof(cint_zacc) == 32, "cint_zacc");

static inline uint64_t cint_rt_zcanon(uint32_t tag, uint64_t bits)
{
    unsigned w = 8u << ((tag - 1u) & 3u);
    return cint_rt_tag_signed(tag) ? (uint64_t)cint_rt_sext(bits, w) : bits & cint_rt_mask(w);
}

static inline void cint_zacc_add(cint_zacc *a, uint32_t tag, uint64_t bits)
{
    uint64_t v = cint_rt_zcanon(tag, bits);
    uint64_t ext = (v >> 63) != 0u && cint_rt_tag_signed(tag) ? UINT64_MAX : 0u;
    uint64_t carry;
    unsigned k;
    a->limb[0] += v;
    carry = a->limb[0] < v ? 1u : 0u;
    for (k = 1u; k < 4u; k++) {
        uint64_t t = a->limb[k] + ext;
        uint64_t over = t < ext ? 1u : 0u;
        a->limb[k] = t + carry;
        carry = over + (a->limb[k] < carry ? 1u : 0u);
    }
}

/* Adds the exact product of x and y, both of type `tag`. */
void cint_zacc_add_product(cint_zacc *a, uint32_t tag, uint64_t x, uint64_t y);
/* The final check of sum and dot: when the total fits `result`, writes its
 * pattern (sign-extended for a signed type) and returns true; otherwise
 * E_OVERFLOW <name>.checked.<elem>.<result> (both types always written:
 * sum.checked.i8.i8, dot.checked.i64.i64), operand n, exact the total, limit
 * MAX(result) when above, else MIN(result); false. `name` is sum or dot. */
bool cint_rt_zacc_fit(cint_ctx *ctx, cint_site site, const char *name, uint32_t elem, uint32_t result, int64_t n,
                      const cint_zacc *a, uint64_t *out);
/* One step j of fold_checked: *acc + x (op CINT_RT_OP_ADD) or *acc * x
 * (CINT_RT_OP_MUL) in type `tag`. In range: writes *acc, true. Otherwise
 * E_OVERFLOW fold_checked.add.<t> or fold_checked.mul.<t>, operands j, <T>
 * *acc (before the step) and <T> x, exact, limit the bound crossed; false. */
bool cint_rt_fold_step(cint_ctx *ctx, cint_site site, uint32_t op, uint32_t tag, int64_t j, uint64_t *acc,
                       uint64_t x);
/* min or max with no element and no init: E_SHAPE reduce_min.checked.<t>
 * (max false) or reduce_max.checked.<t>, operand I64 0, no exact or limit. */
bool cint_rt_fault_reduce_empty(cint_ctx *ctx, cint_site site, bool max, uint32_t tag);

/* A sequential reduction built-in at its callee name `site` (exec.py
 * Machine.reduction). xs holds elements of type `elem` (Bool, as a C bool, for
 * count); ys is dot's b, of the same type and extent, else NULL; xs and ys
 * have rank 1 for dot, any rank for the others. `result` is R for sum(R, xs)
 * and dot(R, a, b), I64 for count, otherwise elem. has_init and init (bits of
 * elem): fold_checked's init (required) and the optional init of min and max.
 * In order: (1) dot only: unequal extents are E_SHAPE dot.checked.<E>.<R>,
 * operands 0 and the extent of b, no exact, limit the extent of a, no fuel;
 * (2) for N elements of xs, ceil(N / 64) fuel units at `site` (none for 0);
 * (3) the elements in logical order: sum and dot the exact total through
 * cint_rt_zacc_fit (n = N); sum_wrap the total wrapped to elem; sum_sat a
 * saturating left fold from 0; fold_checked cint_rt_fold_step from init, j
 * from 0; min and max over the elements and init (an empty pool is
 * cint_rt_fault_reduce_empty); count the true elements; (4) writes the result
 * (sign-extended for a signed type) and returns true. */
#define CINT_REDUCE_SUM 1u
#define CINT_REDUCE_SUM_WRAP 2u
#define CINT_REDUCE_SUM_SAT 3u
#define CINT_REDUCE_FOLD_ADD 4u
#define CINT_REDUCE_FOLD_MUL 5u
#define CINT_REDUCE_MIN 6u
#define CINT_REDUCE_MAX 7u
#define CINT_REDUCE_COUNT 8u
#define CINT_REDUCE_DOT 9u
bool cint_rt_reduce(cint_ctx *ctx, cint_site site, uint32_t op, uint32_t elem, uint32_t result,
                    const cint_vdesc *xs, const cint_vdesc *ys, bool has_init, uint64_t init, uint64_t *out);

/* cint_reduce_<r>: cint_rt_reduce with result type r, narrowed to r's C type
 * as cint_builtin_<r> does. count is cint_reduce_i64 with elem Bool. */
#define CINT_RT_REDUCE(T, CT, TAG, K, W)                                                             \
    static inline bool cint_reduce_##T(cint_ctx *ctx, cint_site site, uint32_t op, uint32_t elem,     \
                                       const cint_vdesc *xs, const cint_vdesc *ys, bool has_init,     \
                                       uint64_t init, CT *out)                                        \
    {                                                                                                 \
        uint64_t bits;                                                                                \
        if (!cint_rt_reduce(ctx, site, op, elem, (TAG), xs, ys, has_init, init, &bits)) {             \
            return false;                                                                             \
        }                                                                                             \
        *out = (CT)CINT_RT_NARROW_##K(bits, (W));                                                     \
        return true;                                                                                  \
    }
CINT_RT_REDUCE(i8, int8_t, CINT_TAG_I8, S, 8u)
CINT_RT_REDUCE(i16, int16_t, CINT_TAG_I16, S, 16u)
CINT_RT_REDUCE(i32, int32_t, CINT_TAG_I32, S, 32u)
CINT_RT_REDUCE(i64, int64_t, CINT_TAG_I64, S, 64u)
CINT_RT_REDUCE(u8, uint8_t, CINT_TAG_U8, U, 8u)
CINT_RT_REDUCE(u16, uint16_t, CINT_TAG_U16, U, 16u)
CINT_RT_REDUCE(u32, uint32_t, CINT_TAG_U32, U, 32u)
CINT_RT_REDUCE(u64, uint64_t, CINT_TAG_U64, U, 64u)

/* Kernel dispatch (SPEC-02 F-1 to F-8; ruling R9; CONF-11 format 4).
 *
 * cint_rt_dispatch_number returns the current entry's next dispatch number
 * and advances it: 0 for an entry's first dispatch (cint_rt_entry_begin
 * resets it). Every dispatch takes one, even one that faults at entry.
 *
 * cint_rt_dispatch_push records the dispatch site as the innermost position of
 * the active-call stack for the work-items and the epilogue, and
 * cint_rt_dispatch_pop removes it after the epilogue; neither charges fuel or
 * changes the depth. A push that could let a later call overrun the stack
 * (only a dispatch inside a dispatch at the largest depth limit) records
 * dispatch.admit instead; a pop removes nothing but a pushed position.
 *
 * cint_rt_dispatch_fault, called by a kernel's fault exit after any fault of
 * its dispatch: when the context holds a record without an address, it sets
 * the address (cint_fault_address): the dispatch number, the phase, the
 * kernel's qualified name `kernel` (kernel.heat_e1_overflow.diffuse, 1 to 256
 * bytes, NUL-terminated) and, in the work-item phase only, the work-item and
 * its step ordinal. Otherwise, or for a phase above 2, it does nothing.
 *
 * cint_rt_dispatch_alias is F-5 check 7 for written parameter i against array
 * parameter x: relation 1 or 2 records E_ALIAS bind.alias, operands i and x,
 * no exact or limit, fuel kept, and the two descriptors of SPEC-02 A-8 (p's
 * view, then q's: element type `elem`, rank, origin, extents and strides, and
 * write when bit 0 of `writable` is set for p, bit 1 for q, else read);
 * false. The descriptors are held beside the record, outside its canonical
 * bytes (cint_ref's encoder has no form for them either), and cleared when a
 * record begins or the fault is cleared. cint_ctx_fault_descriptors copies
 * them (count 0 when the record has none) and returns CINT_OK; CINT_BUSY during
 * an entry, as cint_ctx_fault; CINT_REFUSED for a NULL argument. */
#define CINT_PHASE_ENTRY 0u
#define CINT_PHASE_WORK_ITEM 1u
#define CINT_PHASE_EPILOGUE 2u
int64_t cint_rt_dispatch_number(cint_ctx *ctx);
void cint_rt_dispatch_push(cint_ctx *ctx, cint_site site);
void cint_rt_dispatch_pop(cint_ctx *ctx);
void cint_rt_dispatch_fault(cint_ctx *ctx, const char *kernel, int64_t number, uint32_t phase, int64_t work_item,
                            int64_t step);
bool cint_rt_dispatch_alias(cint_ctx *ctx, cint_site site, uint32_t i, uint32_t x, const cint_vdesc *p,
                            const cint_vdesc *q, uint32_t elem, uint32_t writable);

/* A host-issued dispatch (SPEC-02 K-15; rt/OPEN.md RT-OQ-40). The wrapper of
 * an exported kernel binds its views with cint_rt_bind_views (checks 1 to 3),
 * then hands the binds to the dispatch with cint_rt_dispatch_host; they stay
 * valid until the entry ends, and cint_rt_entry_open and cint_rt_entry_end
 * forget them. cint_rt_dispatch_bound makes, once per entry, F-5 checks 6 and
 * 7 of those binds as cint_rt_bind_finish makes them (a bind.alias record gets
 * the two views as its descriptors), then check 8: an out or inout view of a
 * borrowed registration whose `publish` is CINT_PUBLISH_NONE records
 * E_UNSUPPORTED bind.limit, operand I64 param (SPEC-03 M-30a, P-15). A kernel
 * calls it right before its fuel charge, and cint_rt_dispatch_alias of a
 * host-issued dispatch calls it in place of its own test, so checks 6 to 8
 * come after check 5 and before check 9 (F-5). With no binds handed over (a
 * dispatch from `.ci`), or after the first call, it returns true; on a
 * faulted context, false. */
void cint_rt_dispatch_host(cint_ctx *ctx, const cint_bind *b, uint32_t count);
bool cint_rt_dispatch_bound(cint_ctx *ctx, cint_site site);

/* F-5 check 5 for a `where` constraint that is false at dispatch entry (SPEC-02
 * K-1, F-8; OQ-203): E_SHAPE bind.where, operands the constraint's index (from 0,
 * in written order), its left value and its right value, no exact or limit, fuel
 * kept; false. */
bool cint_rt_fault_where(cint_ctx *ctx, cint_site site, int64_t index, int64_t left, int64_t right);

/* The staging buffer of a written array parameter (SPEC-02 P-4): the product of
 * arg's extents in elements of `size` bytes from cint_rt_scratch, zero-filled, and
 * *stage a row-major view of them with arg's rank and extents; with copy_in (an
 * `inout` parameter) arg's elements copied in, in logical order. False with the
 * record of cint_rt_scratch. */
bool cint_rt_dispatch_stage(cint_ctx *ctx, cint_site site, cint_vdesc *stage, const cint_vdesc *arg, size_t size,
                            bool copy_in);

/* The accumulator of a kernel's scalar `out` (SPEC-02 R-2, R-7) of type t:
 * cint_kfold_<t> adds contribution x of `fold_checked(add, init, e)` to *acc
 * (cint_rt_fold_step, j the n contributions before it); cint_ksum_<t> is the
 * epilogue of `sum` over n contributions of type `elem` (cint_rt_zacc_fit, into
 * *out); cint_kwrap_<t> is the total of `sum_wrap`, wrapped to t. */
#define CINT_RT_KERNEL_ACC(T, CT, TAG, K, W)                                                       \
    static inline bool cint_kfold_##T(cint_ctx *ctx, cint_site site, int64_t n, CT *acc, CT x)     \
    {                                                                                              \
        uint64_t bits = (uint64_t)*acc;                                                            \
        if (!cint_rt_fold_step(ctx, site, CINT_RT_OP_ADD, (TAG), n, &bits, (uint64_t)x)) {          \
            return false;                                                                          \
        }                                                                                          \
        *acc = (CT)CINT_RT_NARROW_##K(bits, (W));                                                  \
        return true;                                                                               \
    }                                                                                              \
    static inline bool cint_ksum_##T(cint_ctx *ctx, cint_site site, uint32_t elem, int64_t n,     \
                                     const cint_zacc *a, CT *out)                                  \
    {                                                                                              \
        uint64_t bits;                                                                             \
        if (!cint_rt_zacc_fit(ctx, site, "sum", elem, (TAG), n, a, &bits)) {                       \
            return false;                                                                          \
        }                                                                                          \
        *out = (CT)CINT_RT_NARROW_##K(bits, (W));                                                  \
        return true;                                                                               \
    }                                                                                              \
    static inline CT cint_kwrap_##T(const cint_zacc *a)                                            \
    {                                                                                              \
        return (CT)CINT_RT_NARROW_##K(a->limb[0], (W));                                            \
    }
CINT_RT_KERNEL_ACC(i8, int8_t, CINT_TAG_I8, S, 8u)
CINT_RT_KERNEL_ACC(i16, int16_t, CINT_TAG_I16, S, 16u)
CINT_RT_KERNEL_ACC(i32, int32_t, CINT_TAG_I32, S, 32u)
CINT_RT_KERNEL_ACC(i64, int64_t, CINT_TAG_I64, S, 64u)
CINT_RT_KERNEL_ACC(u8, uint8_t, CINT_TAG_U8, U, 8u)
CINT_RT_KERNEL_ACC(u16, uint16_t, CINT_TAG_U16, U, 16u)
CINT_RT_KERNEL_ACC(u32, uint32_t, CINT_TAG_U32, U, 32u)
CINT_RT_KERNEL_ACC(u64, uint64_t, CINT_TAG_U64, U, 64u)

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

/* Scratch storage, a minimal frame arena (SPEC-04 LS-110; SPEC-03 M-19): kernel
 * copy-in and run-time-sized local arrays. cint_rt_scratch writes a pointer to
 * `count` elements of `size` bytes, zero-filled and aligned for any scalar,
 * from the context's allocator, and returns true. The arena counts elements:
 * its capacity is frame_arena_bytes / 8 (2,097,152 at the default) until
 * roadmap box 12's runtime step. A request beyond it records E_BOUNDS
 * arena.alloc at `site`, operands count, the elements in use and the capacity;
 * a failure of the allocator, or a byte size beyond size_t, records E_UNSUPPORTED
 * with the same operation and operands. A count of 0 gets a pointer that is not
 * NULL and is never read. cint_rt_scratch_release frees every allocation made
 * after cint_rt_scratch_mark returned `mark`; cint_rt_entry_begin and
 * cint_ctx_destroy free the rest (a faulted entry leaves its allocations). */
bool cint_rt_scratch(cint_ctx *ctx, cint_site site, int64_t count, size_t size, void **out);
int64_t cint_rt_scratch_mark(const cint_ctx *ctx);
void cint_rt_scratch_release(cint_ctx *ctx, int64_t mark);

#if defined(CINT_RT_LIBRARY) && !defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
#pragma GCC visibility pop
#endif

#ifdef __cplusplus
}
#endif

#endif /* CINT_RT_H */
