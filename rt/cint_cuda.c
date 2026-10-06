/* cint_cuda.c: the CUDA layer of the gpu-cuda backend (rt/cint_cuda.h).
 *
 * Every CUDA driver call of a gpu-cuda build is in this file (SPEC-03 A-16a; box 11 default
 * BX11-08). The driver library is loaded when a handle is opened and its entry points are
 * resolved by name, so no CUDA header or toolkit takes part in a build; the types and
 * constants below are the driver API's, copied from its documentation [NVIDIA-CUDA-Driver-13.4]
 * with the exported names cuda.h maps the calls to (the _v2 entry points), as the box 11 unit 0
 * probe resolved them on the development machine (tools/cint_cuda_probe.c).
 *
 * Once the driver is loaded and initialized it stays loaded for the life of the process, as
 * the driver expects; closing a handle releases the primary context, the stream, the event
 * and the fault word. Sizes are size_t byte counts the caller computed with checked
 * arithmetic; the launch arithmetic here is on uint64_t values proven in range. No binary
 * floating-point type appears (SPEC-09 EMIT-01).
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L /* dlopen, dlsym */
#endif

#include "cint_cuda.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define CU_API __stdcall
#else
#include <dlfcn.h>
#define CU_API
#endif

/* Driver API types: CUresult, CUdevice, the opaque handles, and CUdeviceptr. */
typedef int cu_result;
typedef int cu_device;
typedef void *cu_handle;
typedef unsigned long long cu_ptr;

_Static_assert(sizeof(cu_ptr) == sizeof(uint64_t), "CUdeviceptr is 64 bits");
_Static_assert(sizeof(void *) == sizeof(void (*)(void)), "entry points are resolved as data pointers");

/* Constants of the driver API. */
#define CU_STREAM_NON_BLOCKING 0x1u
#define CU_EVENT_DISABLE_TIMING 0x2u
#define CU_MEMHOSTALLOC_DEVICEMAP 0x2u
#define CU_MEMHOSTREGISTER_DEVICEMAP 0x2u
#define CU_JIT_ERROR_LOG_BUFFER 5
#define CU_JIT_ERROR_LOG_BUFFER_SIZE_BYTES 6
#define CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK 0
#define CU_ATTR_MAX_THREADS_PER_BLOCK 1
#define CU_ATTR_MAX_GRID_DIM_X 5
#define CU_ATTR_KERNEL_EXEC_TIMEOUT 17
#define CU_ATTR_CAN_MAP_HOST_MEMORY 19
#define CU_ATTR_UNIFIED_ADDRESSING 41
#define CU_ATTR_COMPUTE_CAPABILITY_MAJOR 75
#define CU_ATTR_COMPUTE_CAPABILITY_MINOR 76

/* The entry points this file calls. */
struct cu_api {
    cu_result(CU_API *init)(unsigned);
    cu_result(CU_API *driver_version)(int *);
    cu_result(CU_API *error_name)(cu_result, const char **);
    cu_result(CU_API *device_get)(cu_device *, int);
    cu_result(CU_API *device_name)(char *, int, cu_device);
    cu_result(CU_API *device_attribute)(int *, int, cu_device);
    cu_result(CU_API *primary_retain)(cu_handle *, cu_device);
    cu_result(CU_API *primary_release)(cu_device);
    cu_result(CU_API *ctx_push)(cu_handle);
    cu_result(CU_API *ctx_pop)(cu_handle *);
    cu_result(CU_API *stream_create)(cu_handle *, unsigned);
    cu_result(CU_API *stream_destroy)(cu_handle);
    cu_result(CU_API *stream_sync)(cu_handle);
    cu_result(CU_API *stream_wait_event)(cu_handle, cu_handle, unsigned);
    cu_result(CU_API *event_create)(cu_handle *, unsigned);
    cu_result(CU_API *event_record)(cu_handle, cu_handle);
    cu_result(CU_API *event_destroy)(cu_handle);
    cu_result(CU_API *module_load)(cu_handle *, const void *, unsigned, int *, void **);
    cu_result(CU_API *module_function)(cu_handle *, cu_handle, const char *);
    cu_result(CU_API *module_unload)(cu_handle);
    cu_result(CU_API *function_attribute)(int *, int, cu_handle);
    cu_result(CU_API *launch)(cu_handle, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned,
                              unsigned, cu_handle, void **, void **);
    cu_result(CU_API *mem_alloc)(cu_ptr *, size_t);
    cu_result(CU_API *mem_free)(cu_ptr);
    cu_result(CU_API *copy_htod)(cu_ptr, const void *, size_t, cu_handle);
    cu_result(CU_API *copy_dtoh)(void *, cu_ptr, size_t, cu_handle);
    cu_result(CU_API *copy_dtod)(cu_ptr, cu_ptr, size_t, cu_handle);
    cu_result(CU_API *memset_d32)(cu_ptr, unsigned, size_t, cu_handle);
    cu_result(CU_API *host_alloc)(void **, size_t, unsigned);
    cu_result(CU_API *host_free)(void *);
    cu_result(CU_API *host_device_pointer)(cu_ptr *, void *, unsigned);
    cu_result(CU_API *host_register)(void *, size_t, unsigned);
    cu_result(CU_API *host_unregister)(void *);
};

#define CU_ENTRY(member, name) {offsetof(struct cu_api, member), name}
static const struct {
    size_t offset;
    const char *name;
} CU_ENTRIES[] = {
    CU_ENTRY(init, "cuInit"),
    CU_ENTRY(driver_version, "cuDriverGetVersion"),
    CU_ENTRY(error_name, "cuGetErrorName"),
    CU_ENTRY(device_get, "cuDeviceGet"),
    CU_ENTRY(device_name, "cuDeviceGetName"),
    CU_ENTRY(device_attribute, "cuDeviceGetAttribute"),
    CU_ENTRY(primary_retain, "cuDevicePrimaryCtxRetain"),
    CU_ENTRY(primary_release, "cuDevicePrimaryCtxRelease_v2"),
    CU_ENTRY(ctx_push, "cuCtxPushCurrent_v2"),
    CU_ENTRY(ctx_pop, "cuCtxPopCurrent_v2"),
    CU_ENTRY(stream_create, "cuStreamCreate"),
    CU_ENTRY(stream_destroy, "cuStreamDestroy_v2"),
    CU_ENTRY(stream_sync, "cuStreamSynchronize"),
    CU_ENTRY(stream_wait_event, "cuStreamWaitEvent"),
    CU_ENTRY(event_create, "cuEventCreate"),
    CU_ENTRY(event_record, "cuEventRecord"),
    CU_ENTRY(event_destroy, "cuEventDestroy_v2"),
    CU_ENTRY(module_load, "cuModuleLoadDataEx"),
    CU_ENTRY(module_function, "cuModuleGetFunction"),
    CU_ENTRY(module_unload, "cuModuleUnload"),
    CU_ENTRY(function_attribute, "cuFuncGetAttribute"),
    CU_ENTRY(launch, "cuLaunchKernel"),
    CU_ENTRY(mem_alloc, "cuMemAlloc_v2"),
    CU_ENTRY(mem_free, "cuMemFree_v2"),
    CU_ENTRY(copy_htod, "cuMemcpyHtoDAsync_v2"),
    CU_ENTRY(copy_dtoh, "cuMemcpyDtoHAsync_v2"),
    CU_ENTRY(copy_dtod, "cuMemcpyDtoDAsync_v2"),
    CU_ENTRY(memset_d32, "cuMemsetD32Async"),
    CU_ENTRY(host_alloc, "cuMemHostAlloc"),
    CU_ENTRY(host_free, "cuMemFreeHost"),
    CU_ENTRY(host_device_pointer, "cuMemHostGetDevicePointer_v2"),
    CU_ENTRY(host_register, "cuMemHostRegister_v2"),
    CU_ENTRY(host_unregister, "cuMemHostUnregister"),
};
#define CU_ENTRY_COUNT (sizeof CU_ENTRIES / sizeof CU_ENTRIES[0])

struct cint_cuda {
    cint_allocator allocator;
    struct cu_api api;
    cu_device device;
    cu_handle context;  /* the device's primary context, retained */
    cu_handle stream;   /* the handle's own stream (A-16a) */
    cu_handle event;    /* orders a producer stream before this one */
    cu_ptr fault_word;  /* one U64 in device memory (B-16b) */
    int64_t grid_limit; /* blocks per launch */
    cint_cuda_info info;
    cint_cuda_error error;
    char jit_log[4096];
};

/* ------------------------------------------------------------------------- */
/* Errors.                                                                    */

static void copy_text(char *dst, size_t cap, const char *src)
{
    size_t i = 0;
    if (src != NULL) {
        for (; i + 1u < cap && src[i] != '\0'; i++) {
            dst[i] = src[i];
        }
    }
    dst[i] = '\0';
}

static void set_error(cint_cuda_error *e, int32_t code, const char *call, const char *detail)
{
    memset(e, 0, sizeof *e);
    e->driver_code = code;
    copy_text(e->call, sizeof e->call, call);
    copy_text(e->detail, sizeof e->detail, detail);
}

/* Records a failed driver call and returns CINT_RESOURCE (SPEC-03 A-6; B-16e). */
static cint_status driver_failed(cint_cuda *c, const char *call, cu_result r)
{
    const char *name = NULL;
    if (c->api.error_name == NULL || c->api.error_name(r, &name) != 0 || name == NULL) {
        name = "unknown driver error";
    }
    set_error(&c->error, (int32_t)r, call, name);
    return CINT_RESOURCE;
}

#define CU_CALL(c, call, expr)                                                                       \
    {                                                                                                \
        cu_result cu_call_result_ = (expr);                                                          \
        if (cu_call_result_ != 0) {                                                                  \
            return driver_failed((c), (call), cu_call_result_);                                      \
        }                                                                                            \
    }

static cint_status checked(cint_cuda *c, const char *call, cu_result r)
{
    return r == 0 ? CINT_OK : driver_failed(c, call, r);
}

/* Makes the primary context current; leave() restores the caller's, keeping the first failure. */
static cint_status enter(cint_cuda *c)
{
    return checked(c, "cuCtxPushCurrent", c->api.ctx_push(c->context));
}

static cint_status leave(cint_cuda *c, cint_status s)
{
    cu_handle popped = NULL;
    cu_result r = c->api.ctx_pop(&popped);
    if (r != 0 && s == CINT_OK) {
        return driver_failed(c, "cuCtxPopCurrent", r);
    }
    return s;
}

/* One driver call with the primary context current: the expression is evaluated after enter(). */
#define CU_ONE(c, call, expr) (enter(c) != CINT_OK ? CINT_RESOURCE : leave((c), checked((c), (call), (expr))))

/* ------------------------------------------------------------------------- */
/* The driver library.                                                        */

static void *load_library(const char *name, int system_name)
{
#if defined(_WIN32)
    /* The driver comes from the system directory only, never from the current directory. */
    return (void *)LoadLibraryExA(name, NULL,
                                  system_name ? LOAD_LIBRARY_SEARCH_SYSTEM32 : LOAD_WITH_ALTERED_SEARCH_PATH);
#else
    (void)system_name;
    return dlopen(name, RTLD_NOW | RTLD_LOCAL);
#endif
}

static void unload_library(void *library)
{
#if defined(_WIN32)
    FreeLibrary((HMODULE)library);
#else
    dlclose(library);
#endif
}

static int resolve(void *library, struct cu_api *api, const char **missing)
{
    size_t i;
    for (i = 0; i < CU_ENTRY_COUNT; i++) {
#if defined(_WIN32)
        FARPROC p = GetProcAddress((HMODULE)library, CU_ENTRIES[i].name);
#else
        void *p = dlsym(library, CU_ENTRIES[i].name);
#endif
        if (p == NULL) {
            *missing = CU_ENTRIES[i].name;
            return 0;
        }
        memcpy((char *)api + CU_ENTRIES[i].offset, &p, sizeof p);
    }
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Handles.                                                                   */

static void *default_alloc(void *user, size_t bytes, size_t align)
{
    (void)user;
    (void)align; /* malloc's alignment suffices for struct cint_cuda */
    return malloc(bytes);
}

static void default_release(void *user, void *ptr, size_t bytes, size_t align)
{
    (void)user;
    (void)bytes;
    (void)align;
    free(ptr);
}

static cint_status read_attribute(cint_cuda *c, int attribute, int32_t *out)
{
    int v = 0;
    CU_CALL(c, "cuDeviceGetAttribute", c->api.device_attribute(&v, attribute, c->device));
    *out = (int32_t)v;
    return CINT_OK;
}

/* Everything of open after the library is loaded: initialize, read the device, retain its
 * primary context, and make the stream, the event and the fault word. */
static cint_status open_device(cint_cuda *c, int32_t ordinal)
{
    int version = 0;
    int32_t grid = 0;
    cu_result r;
    cint_status s;
    CU_CALL(c, "cuInit", c->api.init(0u));
    CU_CALL(c, "cuDriverGetVersion", c->api.driver_version(&version));
    CU_CALL(c, "cuDeviceGet", c->api.device_get(&c->device, (int)ordinal));
    c->info.ordinal = ordinal;
    c->info.driver_version = (int32_t)version;
    if ((s = read_attribute(c, CU_ATTR_COMPUTE_CAPABILITY_MAJOR, &c->info.compute_major)) != CINT_OK ||
        (s = read_attribute(c, CU_ATTR_COMPUTE_CAPABILITY_MINOR, &c->info.compute_minor)) != CINT_OK ||
        (s = read_attribute(c, CU_ATTR_CAN_MAP_HOST_MEMORY, &c->info.can_map_host_memory)) != CINT_OK ||
        (s = read_attribute(c, CU_ATTR_UNIFIED_ADDRESSING, &c->info.unified_addressing)) != CINT_OK ||
        (s = read_attribute(c, CU_ATTR_KERNEL_EXEC_TIMEOUT, &c->info.kernel_exec_timeout)) != CINT_OK ||
        (s = read_attribute(c, CU_ATTR_MAX_THREADS_PER_BLOCK, &c->info.max_threads_per_block)) != CINT_OK ||
        (s = read_attribute(c, CU_ATTR_MAX_GRID_DIM_X, &grid)) != CINT_OK) {
        return s;
    }
    if (c->info.can_map_host_memory != 1 || c->info.max_threads_per_block < 1 || grid < 1) {
        set_error(&c->error, 0, "cuDeviceGetAttribute",
                  c->info.can_map_host_memory != 1 ? "the device cannot map host memory (M-28a)"
                                                   : "the device reports no launch geometry");
        return CINT_RESOURCE;
    }
    c->info.max_grid_blocks = grid;
    c->grid_limit = grid;
    CU_CALL(c, "cuDeviceGetName", c->api.device_name(c->info.name, (int)sizeof c->info.name, c->device));
    c->info.name[sizeof c->info.name - 1u] = '\0';
    CU_CALL(c, "cuDevicePrimaryCtxRetain", c->api.primary_retain(&c->context, c->device));
    if ((s = enter(c)) != CINT_OK) {
        return s;
    }
    if ((r = c->api.stream_create(&c->stream, CU_STREAM_NON_BLOCKING)) != 0) {
        c->stream = NULL;
        s = driver_failed(c, "cuStreamCreate", r);
    } else if ((r = c->api.event_create(&c->event, CU_EVENT_DISABLE_TIMING)) != 0) {
        c->event = NULL;
        s = driver_failed(c, "cuEventCreate", r);
    } else if ((r = c->api.mem_alloc(&c->fault_word, sizeof(uint64_t))) != 0) {
        c->fault_word = 0;
        s = driver_failed(c, "cuMemAlloc", r);
    }
    return leave(c, s);
}

/* Releases what open_device made, in reverse order; the library stays loaded once initialized. */
static void release_device(cint_cuda *c)
{
    if (c->context == NULL) {
        return;
    }
    if (c->api.ctx_push(c->context) == 0) {
        cu_handle popped = NULL;
        if (c->stream != NULL) {
            (void)c->api.stream_sync(c->stream);
        }
        if (c->fault_word != 0) {
            (void)c->api.mem_free(c->fault_word);
        }
        if (c->event != NULL) {
            (void)c->api.event_destroy(c->event);
        }
        if (c->stream != NULL) {
            (void)c->api.stream_destroy(c->stream);
        }
        (void)c->api.ctx_pop(&popped);
    }
    (void)c->api.primary_release(c->device);
    c->context = NULL;
}

static cint_status open_from(const char *const *names, size_t count, int system_names, const cint_cuda_device *device,
                             const cint_allocator *allocator, cint_cuda **out, cint_cuda_error *err)
{
    cint_allocator a;
    cint_cuda *c;
    void *library = NULL;
    const char *missing = NULL;
    cint_status s;
    size_t i;
    if (out == NULL || (device != NULL && (device->size != (uint32_t)sizeof *device || device->ordinal < 0)) ||
        (allocator != NULL && allocator->alloc != NULL && allocator->release == NULL)) {
        return CINT_REFUSED;
    }
    a.alloc = default_alloc;
    a.release = default_release;
    a.user = NULL;
    if (allocator != NULL && allocator->alloc != NULL) {
        a = *allocator;
    }
    c = (cint_cuda *)a.alloc(a.user, sizeof *c, _Alignof(struct cint_cuda));
    if (c == NULL) {
        if (err != NULL) {
            set_error(err, 0, "allocate", "the cint_cuda handle");
        }
        return CINT_RESOURCE;
    }
    memset(c, 0, sizeof *c);
    c->allocator = a;
    for (i = 0; i < count && library == NULL; i++) {
        library = load_library(names[i], system_names);
    }
    if (library == NULL) {
        set_error(&c->error, 0, "load", names[0]);
        s = CINT_RESOURCE;
    } else if (!resolve(library, &c->api, &missing)) {
        set_error(&c->error, 0, missing, "entry point not found in the driver library");
        memset(&c->api, 0, sizeof c->api);
        unload_library(library);
        s = CINT_RESOURCE;
    } else {
        s = open_device(c, device != NULL ? device->ordinal : 0);
    }
    if (s != CINT_OK) {
        if (err != NULL) {
            *err = c->error;
        }
        release_device(c);
        a.release(a.user, c, sizeof *c, _Alignof(struct cint_cuda));
        return s;
    }
    *out = c;
    return CINT_OK;
}

cint_status cint_cuda_open(const cint_cuda_device *device, const cint_allocator *allocator, cint_cuda **out,
                           cint_cuda_error *err)
{
#if defined(_WIN32)
    static const char *const names[] = {"nvcuda.dll"};
#else
    /* WSL2 keeps the driver in /usr/lib/wsl/lib, which its loader configuration normally lists. */
    static const char *const names[] = {"libcuda.so.1", "/usr/lib/wsl/lib/libcuda.so.1"};
#endif
    return open_from(names, sizeof names / sizeof names[0], 1, device, allocator, out, err);
}

#if defined(CINT_CUDA_TESTING)
cint_status cint_cuda_open_driver(const char *path, const cint_cuda_device *device, const cint_allocator *allocator,
                                  cint_cuda **out, cint_cuda_error *err)
{
    const char *names[1];
    if (path == NULL || path[0] == '\0') {
        return CINT_REFUSED;
    }
    names[0] = path;
    return open_from(names, 1u, 0, device, allocator, out, err);
}

cint_status cint_cuda_test_grid_limit(cint_cuda *cuda, int64_t max_blocks)
{
    if (cuda == NULL || max_blocks < 1 || max_blocks > cuda->info.max_grid_blocks) {
        return CINT_REFUSED;
    }
    cuda->grid_limit = max_blocks;
    return CINT_OK;
}
#endif

void cint_cuda_close(cint_cuda *cuda)
{
    cint_allocator a;
    if (cuda == NULL) {
        return;
    }
    release_device(cuda);
    a = cuda->allocator;
    a.release(a.user, cuda, sizeof *cuda, _Alignof(struct cint_cuda));
}

cint_status cint_cuda_info_get(const cint_cuda *cuda, cint_cuda_info *out)
{
    if (cuda == NULL || out == NULL) {
        return CINT_REFUSED;
    }
    *out = cuda->info;
    return CINT_OK;
}

cint_status cint_cuda_last_error(const cint_cuda *cuda, cint_cuda_error *out)
{
    if (cuda == NULL || out == NULL) {
        return CINT_REFUSED;
    }
    *out = cuda->error;
    return CINT_OK;
}

const char *cint_cuda_jit_log(const cint_cuda *cuda)
{
    return cuda != NULL ? cuda->jit_log : "";
}

uintptr_t cint_cuda_stream(const cint_cuda *cuda)
{
    return cuda != NULL ? (uintptr_t)cuda->stream : 0u;
}

/* ------------------------------------------------------------------------- */
/* Memory and copies.                                                         */

cint_status cint_cuda_host_alloc(cint_cuda *cuda, size_t bytes, void **host, uint64_t *device)
{
    void *p = NULL;
    cu_ptr d = 0;
    cu_result r;
    cint_status s;
    if (cuda == NULL || bytes == 0u || host == NULL || device == NULL) {
        return CINT_REFUSED;
    }
    if ((s = enter(cuda)) != CINT_OK) {
        return s;
    }
    if ((r = cuda->api.host_alloc(&p, bytes, CU_MEMHOSTALLOC_DEVICEMAP)) != 0) {
        s = driver_failed(cuda, "cuMemHostAlloc", r);
    } else if ((r = cuda->api.host_device_pointer(&d, p, 0u)) != 0) {
        s = driver_failed(cuda, "cuMemHostGetDevicePointer", r);
        (void)cuda->api.host_free(p);
    } else {
        *host = p;
        *device = (uint64_t)d;
    }
    return leave(cuda, s);
}

cint_status cint_cuda_host_free(cint_cuda *cuda, void *host)
{
    return cuda == NULL || host == NULL ? CINT_REFUSED : CU_ONE(cuda, "cuMemFreeHost", cuda->api.host_free(host));
}

cint_status cint_cuda_host_register(cint_cuda *cuda, void *host, size_t bytes, uint64_t *device)
{
    cu_ptr d = 0;
    cu_result r;
    cint_status s;
    if (cuda == NULL || host == NULL || bytes == 0u || device == NULL) {
        return CINT_REFUSED;
    }
    if ((s = enter(cuda)) != CINT_OK) {
        return s;
    }
    if ((r = cuda->api.host_register(host, bytes, CU_MEMHOSTREGISTER_DEVICEMAP)) != 0) {
        s = driver_failed(cuda, "cuMemHostRegister", r);
    } else if ((r = cuda->api.host_device_pointer(&d, host, 0u)) != 0) {
        s = driver_failed(cuda, "cuMemHostGetDevicePointer", r);
        (void)cuda->api.host_unregister(host);
    } else {
        *device = (uint64_t)d;
    }
    return leave(cuda, s);
}

cint_status cint_cuda_host_unregister(cint_cuda *cuda, void *host)
{
    return cuda == NULL || host == NULL ? CINT_REFUSED
                                        : CU_ONE(cuda, "cuMemHostUnregister", cuda->api.host_unregister(host));
}

cint_status cint_cuda_alloc(cint_cuda *cuda, size_t bytes, uint64_t *device)
{
    cu_ptr d = 0;
    cint_status s;
    if (cuda == NULL || bytes == 0u || device == NULL) {
        return CINT_REFUSED;
    }
    if ((s = CU_ONE(cuda, "cuMemAlloc", cuda->api.mem_alloc(&d, bytes))) == CINT_OK) {
        *device = (uint64_t)d;
    }
    return s;
}

cint_status cint_cuda_free(cint_cuda *cuda, uint64_t device)
{
    return cuda == NULL || device == 0u ? CINT_REFUSED
                                        : CU_ONE(cuda, "cuMemFree", cuda->api.mem_free((cu_ptr)device));
}

cint_status cint_cuda_copy_to_device(cint_cuda *cuda, uint64_t dst, const void *src, size_t bytes)
{
    if (cuda == NULL || dst == 0u || src == NULL || bytes == 0u) {
        return CINT_REFUSED;
    }
    return CU_ONE(cuda, "cuMemcpyHtoDAsync", cuda->api.copy_htod((cu_ptr)dst, src, bytes, cuda->stream));
}

cint_status cint_cuda_copy_to_host(cint_cuda *cuda, void *dst, uint64_t src, size_t bytes)
{
    cu_result r;
    cint_status s;
    if (cuda == NULL || dst == NULL || src == 0u || bytes == 0u) {
        return CINT_REFUSED;
    }
    if ((s = enter(cuda)) != CINT_OK) {
        return s;
    }
    if ((r = cuda->api.copy_dtoh(dst, (cu_ptr)src, bytes, cuda->stream)) != 0) {
        s = driver_failed(cuda, "cuMemcpyDtoHAsync", r);
    } else if ((r = cuda->api.stream_sync(cuda->stream)) != 0) {
        s = driver_failed(cuda, "cuStreamSynchronize", r);
    }
    return leave(cuda, s);
}

cint_status cint_cuda_copy_on_device(cint_cuda *cuda, uint64_t dst, uint64_t src, size_t bytes)
{
    if (cuda == NULL || dst == 0u || src == 0u || bytes == 0u) {
        return CINT_REFUSED;
    }
    return CU_ONE(cuda, "cuMemcpyDtoDAsync", cuda->api.copy_dtod((cu_ptr)dst, (cu_ptr)src, bytes, cuda->stream));
}

cint_status cint_cuda_wait_stream(cint_cuda *cuda, uintptr_t producer)
{
    cu_result r;
    cint_status s;
    if (cuda == NULL) {
        return CINT_REFUSED;
    }
    if ((s = enter(cuda)) != CINT_OK) {
        return s;
    }
    if ((r = cuda->api.event_record(cuda->event, (cu_handle)producer)) != 0) {
        s = driver_failed(cuda, "cuEventRecord", r);
    } else if ((r = cuda->api.stream_wait_event(cuda->stream, cuda->event, 0u)) != 0) {
        s = driver_failed(cuda, "cuStreamWaitEvent", r);
    }
    return leave(cuda, s);
}

cint_status cint_cuda_sync(cint_cuda *cuda)
{
    return cuda == NULL ? CINT_REFUSED : CU_ONE(cuda, "cuStreamSynchronize", cuda->api.stream_sync(cuda->stream));
}

/* ------------------------------------------------------------------------- */
/* Modules and kernels.                                                       */

cint_status cint_cuda_module_load(cint_cuda *cuda, const char *ptx, cint_cuda_module *out)
{
    int options[2] = {CU_JIT_ERROR_LOG_BUFFER, CU_JIT_ERROR_LOG_BUFFER_SIZE_BYTES};
    void *values[2];
    cu_handle m = NULL;
    cu_result r;
    cint_status s;
    if (cuda == NULL || ptx == NULL || out == NULL) {
        return CINT_REFUSED;
    }
    if ((s = enter(cuda)) != CINT_OK) {
        return s;
    }
    memset(cuda->jit_log, 0, sizeof cuda->jit_log);
    values[0] = cuda->jit_log;
    values[1] = (void *)(uintptr_t)(sizeof cuda->jit_log - 1u);
    if ((r = cuda->api.module_load(&m, ptx, 2u, options, values)) != 0) {
        s = driver_failed(cuda, "cuModuleLoadDataEx", r);
    } else {
        cuda->jit_log[0] = '\0';
        out->module = m;
    }
    cuda->jit_log[sizeof cuda->jit_log - 1u] = '\0';
    return leave(cuda, s);
}

cint_status cint_cuda_module_unload(cint_cuda *cuda, cint_cuda_module *module)
{
    cu_result r;
    cint_status s;
    if (cuda == NULL || module == NULL || module->module == NULL) {
        return CINT_REFUSED;
    }
    if ((s = enter(cuda)) != CINT_OK) {
        return s;
    }
    if ((r = cuda->api.module_unload(module->module)) != 0) {
        s = driver_failed(cuda, "cuModuleUnload", r);
    } else {
        module->module = NULL;
    }
    return leave(cuda, s);
}

cint_status cint_cuda_kernel_get(cint_cuda *cuda, const cint_cuda_module *module, const char *name,
                                 cint_cuda_kernel *out)
{
    cu_handle f = NULL;
    int threads = 0;
    cu_result r;
    cint_status s;
    if (cuda == NULL || module == NULL || module->module == NULL || name == NULL || out == NULL) {
        return CINT_REFUSED;
    }
    if ((s = enter(cuda)) != CINT_OK) {
        return s;
    }
    if ((r = cuda->api.module_function(&f, module->module, name)) != 0) {
        s = driver_failed(cuda, "cuModuleGetFunction", r);
    } else if ((r = cuda->api.function_attribute(&threads, CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK, f)) != 0) {
        s = driver_failed(cuda, "cuFuncGetAttribute", r);
    } else {
        memset(out, 0, sizeof *out);
        out->function = f;
        out->max_threads = (int32_t)threads;
    }
    return leave(cuda, s);
}

/* ------------------------------------------------------------------------- */
/* Launches (SPEC-02 B-16a, B-16b; F-2, F-3, F-9, F-10).                      */

/* Reads the fault word once the stream has drained. */
static cint_status read_fault(cint_cuda *c, uint64_t *word)
{
    cu_ptr w = 0;
    CU_CALL(c, "cuMemcpyDtoHAsync", c->api.copy_dtoh(&w, c->fault_word, sizeof w, c->stream));
    CU_CALL(c, "cuStreamSynchronize", c->api.stream_sync(c->stream));
    *word = (uint64_t)w;
    return CINT_OK;
}

static cint_status run_launches(cint_cuda *c, const cint_cuda_kernel *k, uint64_t n, unsigned block, void **args,
                                cu_ptr *base, cint_cuda_launch_info *info)
{
    /* At most 2^31 - 1 blocks of at most 2^31 - 1 work-items: the product fits in 62 bits. */
    const uint64_t per = (uint64_t)c->grid_limit * (uint64_t)block;
    uint64_t first, word = UINT64_MAX;
    cint_status s;
    CU_CALL(c, "cuMemsetD32Async", c->api.memset_d32(c->fault_word, 0xFFFFFFFFu, 2u, c->stream));
    for (first = 0; first < n; first += per) {
        const uint64_t count = n - first < per ? n - first : per;
        const uint64_t blocks = count / block + (count % block != 0u ? 1u : 0u);
        if (info->launches > 0) {
            if ((s = read_fault(c, &word)) != CINT_OK) {
                return s;
            }
            if (first > word) {
                break; /* F-3: the whole range lies above a recorded fault */
            }
        }
        *base = (cu_ptr)first;
        CU_CALL(c, "cuLaunchKernel",
                c->api.launch(k->function, (unsigned)blocks, 1u, 1u, block, 1u, 1u, 0u, c->stream, args, NULL));
        info->launches++;
    }
    if (info->launches > 0) {
        if ((s = read_fault(c, &word)) != CINT_OK) {
            return s;
        }
    }
    info->fault = word;
    return CINT_OK;
}

cint_status cint_cuda_launch(cint_cuda *cuda, const cint_cuda_kernel *kernel, int64_t n, uint32_t block,
                             void *const *params, uint32_t nparams, cint_cuda_launch_info *info)
{
    void *args[3u + CINT_CUDA_MAX_PARAMS];
    cu_ptr base = 0, end, fault;
    cint_cuda_launch_info result;
    cint_status s;
    uint32_t i;
    if (cuda == NULL || kernel == NULL || kernel->function == NULL || n < 0 || info == NULL ||
        nparams > CINT_CUDA_MAX_PARAMS || (nparams > 0u && params == NULL)) {
        return CINT_REFUSED;
    }
    if (block == 0u) {
        block = CINT_CUDA_DEFAULT_BLOCK;
    }
    if (block > (uint32_t)cuda->info.max_threads_per_block) {
        return CINT_REFUSED;
    }
    for (i = 0; i < nparams; i++) {
        if (params[i] == NULL) {
            return CINT_REFUSED;
        }
        args[3u + i] = params[i];
    }
    if (kernel->max_threads < 1 || block > (uint32_t)kernel->max_threads) {
        set_error(&cuda->error, 0, "cuFuncGetAttribute", "the block size exceeds what the kernel can run");
        return CINT_RESOURCE;
    }
    end = (cu_ptr)n;
    fault = cuda->fault_word;
    args[0] = &base;
    args[1] = &end;
    args[2] = &fault;
    memset(&result, 0, sizeof result);
    result.block = block;
    result.fault = UINT64_MAX;
    if (n == 0) {
        *info = result;
        return CINT_OK;
    }
    if ((s = enter(cuda)) != CINT_OK) {
        return s;
    }
    s = run_launches(cuda, kernel, (uint64_t)n, block, args, &base, &result);
    s = leave(cuda, s);
    if (s == CINT_OK) {
        *info = result;
    }
    return s;
}
