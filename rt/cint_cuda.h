/* cint_cuda.h: the CUDA layer of the gpu-cuda backend (box 11, unit 2).
 *
 * Status: Proposed. The spellings below are Proposed with SPEC-03 A-16a; the semantics
 * follow box 11 defaults BX11-08 to BX11-11 and BX11-15, confirmed 2026-10-05, as SPEC-03
 * A-16a and SPEC-02 B-16a, B-16b and B-16e state them. Design note:
 * docs/design/notes/2026-10-05-box11-cuda-backend.md, section 9, unit 2.
 *
 * rt/cint_cuda.c holds every CUDA driver call of a gpu-cuda build (SPEC-09 4.2). It loads
 * the driver library when a handle is opened (nvcuda.dll from the Windows system directory;
 * libcuda.so.1 on Linux and WSL2), so it builds with no CUDA header or toolkit, and on a
 * machine without the driver cint_cuda_open returns CINT_RESOURCE naming the library.
 *
 * One handle backs one CINT context (SPEC-03 A-16a): the device's primary context, with the
 * flags the driver sets, and one stream of the handle's own for every launch and copy. Every
 * function makes the primary context current on entry and restores the caller's current
 * context before it returns, so a host that also uses CUDA (CuPy, PyTorch) keeps its own.
 * A handle is used by one thread at a time, under its CINT context's busy flag (A-11).
 *
 * Status values: CINT_OK; CINT_REFUSED for invalid arguments, with no driver call made;
 * CINT_RESOURCE for a missing driver, a failed driver call (a stopped launch included,
 * B-16e), or a failed allocation. After CINT_RESOURCE, cint_cuda_last_error names the call
 * and the driver's error; the text is a diagnostic, never canonical state.
 *
 * Kernel parameters (Proposed, for compiler/back_ptx.ci, box 11 unit 3). Every kernel that
 * cint_cuda_launch runs takes three .u64 parameters first, then its own:
 *   base    the first work-item index of this launch;
 *   end     one past the last work-item index of the dispatch;
 *   fault   the device address of the U64 fault word (SPEC-02 F-9, B-16b).
 * Work-item i = base + ctaid.x * ntid.x + tid.x (K-9); an item with i >= end exits. An item
 * whose index exceeds the fault word exits at its start (F-10); an item that faults performs
 * atom.global.min.u64 on the word with i and exits without writing. */
#ifndef CINT_CUDA_H
#define CINT_CUDA_H

#include <stddef.h>
#include <stdint.h>

#include "cint_rt.h"

/* Hidden in a library build but for cint_ctx_device_stream (cint_rt.h, CINT_RT_LIBRARY). */
#if defined(CINT_RT_LIBRARY) && !defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
#pragma GCC visibility push(hidden)
#endif

/* The record that cint_ctx_config.devices[0] points at for a CUDA context (A-16a). `size`
 * must be sizeof(cint_cuda_device). */
typedef struct cint_cuda_device {
    uint32_t size;
    int32_t ordinal;
} cint_cuda_device;
_Static_assert(sizeof(cint_cuda_device) == 8, "cint_cuda_device");

typedef struct cint_cuda cint_cuda; /* opaque */

/* One loaded PTX module, and one entry of it. The handles are the driver's; they stay valid
 * until the module is unloaded. */
typedef struct cint_cuda_module {
    void *module;
} cint_cuda_module;
typedef struct cint_cuda_kernel {
    void *function;
    int32_t max_threads;  /* the most work-items per block this entry can run */
    uint32_t reserved;
} cint_cuda_kernel;

/* The last failure of a handle, or of cint_cuda_open. `driver_code` is the driver's CUresult,
 * or 0 when the failure is not a driver call (the driver library missing, an allocation).
 * `call` names the driver call or the step ("load", "allocate"); `detail` holds the driver's
 * error name, or the library name when the library is missing. */
typedef struct cint_cuda_error {
    int32_t driver_code;
    uint32_t reserved;
    char call[48];
    char detail[80];
} cint_cuda_error;

/* Facts about the device of a handle, read once at open. */
typedef struct cint_cuda_info {
    int32_t ordinal;
    int32_t driver_version;      /* cuDriverGetVersion: 1000 * major + 10 * minor */
    int32_t compute_major;
    int32_t compute_minor;
    int32_t can_map_host_memory; /* 1: required, so always 1 on an open handle (M-28a) */
    int32_t unified_addressing;
    int32_t kernel_exec_timeout; /* 1: the display watchdog can stop a long launch (B-16e) */
    int32_t max_threads_per_block;
    int64_t max_grid_blocks;     /* blocks per launch along x: the grid limit of B-16a */
    char name[128];
} cint_cuda_info;

/* What one cint_cuda_launch did. */
typedef struct cint_cuda_launch_info {
    uint32_t block;    /* work-items per block */
    uint32_t reserved;
    int64_t launches;  /* driver launches started */
    uint64_t fault;    /* the fault word after the last launch: UINT64_MAX when no item faulted */
} cint_cuda_launch_info;

#define CINT_CUDA_DEFAULT_BLOCK 256u  /* B-16a */
#define CINT_CUDA_MAX_PARAMS 61u      /* a kernel's own parameters, after base, end, fault */

/* Opens a handle on device `device->ordinal`; `device` NULL means ordinal 0 (A-16a: a
 * gpu-cuda library with device_count 0). `allocator` NULL means malloc and free. Writes
 * `*err` when it is not NULL and the result is not CINT_OK. A device that cannot map host
 * memory is CINT_RESOURCE (M-28a). */
cint_status cint_cuda_open(const cint_cuda_device *device, const cint_allocator *allocator,
                           cint_cuda **out, cint_cuda_error *err);
/* Synchronizes the stream, then releases the stream, the fault word and the primary
 * context. Modules and memory must be released first. NULL is ignored. */
void cint_cuda_close(cint_cuda *cuda);
cint_status cint_cuda_info_get(const cint_cuda *cuda, cint_cuda_info *out);
cint_status cint_cuda_last_error(const cint_cuda *cuda, cint_cuda_error *out);
/* The JIT log of the last failed module load, NUL-terminated (BACK-02: a diagnostic). */
const char *cint_cuda_jit_log(const cint_cuda *cuda);
/* The handle's stream as the integer a DLPack exchange passes (SPEC-03 P-10). */
uintptr_t cint_cuda_stream(const cint_cuda *cuda);

/* Memory. Device addresses are uint64_t. Sizes are in bytes, computed by the caller from
 * element counts with checked arithmetic; 0 is refused. */
/* Page-locked host memory mapped into the device's address space: the storage the context
 * owns (SPEC-03 M-28a). *host is the host address, *device the address device code uses. */
cint_status cint_cuda_host_alloc(cint_cuda *cuda, size_t bytes, void **host, uint64_t *device);
cint_status cint_cuda_host_free(cint_cuda *cuda, void *host);
/* Page-locks and maps memory allocated elsewhere; the device address comes from the driver
 * (unit 0: can_use_host_pointer_for_registered_mem is 0 on the development machine's GPU). */
cint_status cint_cuda_host_register(cint_cuda *cuda, void *host, size_t bytes, uint64_t *device);
cint_status cint_cuda_host_unregister(cint_cuda *cuda, void *host);
/* Device memory: placement device(1). */
cint_status cint_cuda_alloc(cint_cuda *cuda, size_t bytes, uint64_t *device);
cint_status cint_cuda_free(cint_cuda *cuda, uint64_t device);
/* Copies on the handle's stream, ordered after earlier work on it. cint_cuda_copy_to_host
 * returns once the bytes have arrived; the other two return once the copy is queued. */
cint_status cint_cuda_copy_to_device(cint_cuda *cuda, uint64_t dst, const void *src, size_t bytes);
cint_status cint_cuda_copy_to_host(cint_cuda *cuda, void *dst, uint64_t src, size_t bytes);
cint_status cint_cuda_copy_on_device(cint_cuda *cuda, uint64_t dst, uint64_t src, size_t bytes);
/* Queues zeros over `bytes` bytes at `dst`, a multiple of 4. */
cint_status cint_cuda_zero(cint_cuda *cuda, uint64_t dst, size_t bytes);
/* Orders work queued on `producer` (a CUstream of the same device, as a registered buffer's
 * cint_buffer_desc.sync gives it) before later work on the handle's stream, with an event. */
cint_status cint_cuda_wait_stream(cint_cuda *cuda, uintptr_t producer);
/* Waits for all work on the handle's stream: an entry calls it before it returns (K-14). */
cint_status cint_cuda_sync(cint_cuda *cuda);

/* Modules. `ptx` is NUL-terminated PTX text; the driver compiles it (BX11-02). A module the
 * driver refuses is CINT_RESOURCE, with the JIT log in cint_cuda_jit_log. */
cint_status cint_cuda_module_load(cint_cuda *cuda, const char *ptx, cint_cuda_module *out);
cint_status cint_cuda_module_unload(cint_cuda *cuda, cint_cuda_module *module);
cint_status cint_cuda_kernel_get(cint_cuda *cuda, const cint_cuda_module *module, const char *name,
                                 cint_cuda_kernel *out);

/* Runs work-items [0, n) of `kernel` (B-16a). `block` 0 means CINT_CUDA_DEFAULT_BLOCK.
 * `params` points at `nparams` pointers to the kernel's own parameter values, as
 * cuLaunchKernel takes them; base, end and fault come first and are supplied here. The fault
 * word is set to all ones, then the items run in launches over ascending index ranges, each
 * of at most the grid limit's blocks; a launch whose first item lies above the fault word
 * after the previous launch is not started (F-2, F-3). Returns after the stream has drained,
 * with the final fault word in *info. n == 0 starts no launch. */
cint_status cint_cuda_launch(cint_cuda *cuda, const cint_cuda_kernel *kernel, int64_t n,
                             uint32_t block, void *const *params, uint32_t nparams,
                             cint_cuda_launch_info *info);

/* The dispatch path of a gpu-cuda build (box 11 unit 4; rt/cint_cuda_dispatch.c; SPEC-02
 * B-16a to B-16c, F-11). Generated code calls these from a kernel's function, compiled with
 * CINT_GPU_CUDA: the entry checks, the dispatch charge, staging and the push have run on the
 * host, as on cpu-c17.
 *
 * cint_cuda_dispatch runs the work-items [0, n) of entry `entry` of the module whose PTX is
 * `ptx` (one line per string, ending with an empty string), opening the context's CUDA
 * handle and loading the module at the context's first use of them. `args` are the entry's
 * a<j> and l<j> parameters in order (compiler/back_ptx.ci): a view's storage is page-locked
 * and mapped for the launch (SPEC-03 M-28a), and its descriptor passed with the device address
 * of its base. With `fuel`, each item's charge C(i) is kept and `run->limit` is the first item
 * whose prefix of charges exceeds the allowance left (k_f), or k_b, the lowest faulting item,
 * when that is lower, or n. `run->contrib[j]` and `run->mark[j]` hold the contributions and
 * presence marks of reduction j, n of each, readable until cint_cuda_dispatch_end. False with
 * the context holding a boundary record (host.resource, CINT_RESOURCE) when the driver fails.
 *
 * cint_cuda_dispatch_end charges the charges of the items below `limit` (at most run->limit,
 * lowered by the host's fold for k_r) and releases the dispatch's device storage. When item
 * `limit` is left to the host (limit < n), it first reads each view argument in device memory
 * back into a host copy and points the caller's descriptor of it there, so that the host's
 * re-execution reads the pre-dispatch inputs (B-16b, fault-readback); the copies live until
 * the context's next dispatch.
 * cint_cuda_dispatch_diverged records host.divergence: the item the host re-executed
 * completed (F-11). */
typedef struct cint_cuda_arg {
    const cint_vdesc *view; /* a view, or NULL for a scalar */
    size_t elem;            /* a view's element size in bytes */
    uint64_t value;         /* a scalar, sign- or zero-extended to 64 bits */
} cint_cuda_arg;

typedef struct cint_cuda_run {
    int64_t n;
    int64_t limit;
    int64_t reductions;
    const int64_t *const *contrib;
    const uint32_t *const *mark;
    void *state; /* the dispatch's device storage and registrations */
} cint_cuda_run;

bool cint_cuda_dispatch(cint_ctx *ctx, cint_site site, const char *const *ptx, const char *entry, int64_t n,
                        bool fuel, int64_t reductions, const cint_cuda_arg *args, uint32_t nargs,
                        cint_cuda_run *run);
bool cint_cuda_dispatch_end(cint_ctx *ctx, cint_site site, cint_cuda_run *run, int64_t limit);
bool cint_cuda_dispatch_diverged(cint_ctx *ctx, cint_site site);

/* Placement device(1) (box 11 unit 5; SPEC-03 A-16a, M-28, M-28a; decision 2026-10-06 on
 * OQ-216). A context created with device_count 1 registers CUDA memory of I8 to U64 with
 * device 1 (cint_buffer_register), and a dispatch issued from the host binds such views only
 * (SPEC-02 F-5 check 4). A dispatch passes them to its kernel as they are, and stages its
 * outputs in device memory of the entry, zero-filled, published by copies on the device
 * (B-16d). cint_rt.c, built with CINT_GPU_CUDA, calls the first three:
 *   cint_cuda_placed: true when v's base lies in a live device(1) registration of ctx or in
 *     the device staging of its current dispatch;
 *   cint_cuda_stage: cint_rt_scratch's staging of `count` elements of `size` bytes in device
 *     memory, freed when a later dispatch stages; false with host.resource;
 *   cint_cuda_copy_view: cint_rt_copy_view of views either of which is device placed, one
 *     driver copy per run of elements adjacent in both; false with host.resource.
 * cint_ctx_device_stream writes the stream of placement `device` (1) of ctx as a cint_sync of
 * kind CINT_MEM_CUDA, opening the context's handle: the stream a DLPack producer makes its
 * data ready on (P-10). CINT_REFUSED for another placement or a context without a device,
 * CINT_BUSY during an entry, CINT_RESOURCE when the handle cannot be opened. */
bool cint_cuda_placed(cint_ctx *ctx, const cint_vdesc *v);
bool cint_cuda_stage(cint_ctx *ctx, cint_site site, int64_t count, size_t size, void **out);
bool cint_cuda_copy_view(cint_ctx *ctx, cint_site site, const cint_vdesc *dst, const cint_vdesc *src, size_t size);
CINT_RT_API cint_status cint_ctx_device_stream(cint_ctx *ctx, uint32_t device, cint_sync *out);

/* Device arrays in exported functions (box 11 unit 5b; SPEC-03 M-28, 5.3; SPEC-02 F-5 check 4;
 * decision 2026-10-06 on OQ-217), both inside ABI 3.1. Generated code calls them only when
 * compiled with CINT_GPU_CUDA, from a module whose kernels all have a device path.
 *   cint_rt_bind_finish refuses a view of device(1) memory at every parameter of a function's
 *     entry. cint_cuda_bind_finish is cint_rt_bind_finish with `marks`, bit j for parameter j
 *     (from 0, in declaration order, j below 64): those parameters the body passes only,
 *     whole, to dispatches (compiler/back_c.ci, passed_only). On a context with a device, a
 *     marked parameter takes a device(1) view as well as a host one.
 *   cint_cuda_dispatch_placed makes check 4 of a dispatch issued from `.ci`, at entry phase
 *     before check 5, over `views`: count of them, one per parameter of the kernel in order,
 *     NULL for a scalar. On a context with a device, the views must all be device(1) memory
 *     (a registration or the entry's device staging), or all storage the context owns (module
 *     variables, frame-arena arrays, arenas and pools; not a registration); otherwise it
 *     records E_UNSUPPORTED bind.placement with the I64 index of the first parameter that
 *     differs (a host registration differs from either) and returns false. A dispatch issued
 *     from the host made check 4 in cint_rt_dispatch_host, and a context without a device
 *     has host memory only, which the dispatch maps (M-28a): both pass. False on a faulted
 *     context. */
bool cint_cuda_bind_finish(cint_ctx *ctx, cint_site site, const cint_bind *b, uint32_t count, uint64_t marks);
bool cint_cuda_dispatch_placed(cint_ctx *ctx, cint_site site, const cint_vdesc *const *views, uint32_t count);

#if defined(CINT_CUDA_TESTING)
/* Test builds only: open with the driver library at `path`, and lower the grid limit so that
 * a test dispatch is split into several launches. */
cint_status cint_cuda_open_driver(const char *path, const cint_cuda_device *device,
                                  const cint_allocator *allocator, cint_cuda **out,
                                  cint_cuda_error *err);
cint_status cint_cuda_test_grid_limit(cint_cuda *cuda, int64_t max_blocks);
#endif

#if defined(CINT_RT_LIBRARY) && !defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
#pragma GCC visibility pop
#endif

#endif
