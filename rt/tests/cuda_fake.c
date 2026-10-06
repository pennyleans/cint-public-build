/* cuda_fake.c: a stand-in CUDA driver for rt/tests/test_cuda.c (box 11, unit 2).
 *
 * Built as a shared library that exports the driver entry points rt/cint_cuda.c resolves,
 * so the host protocol of rt/cint_cuda.c runs on every leg, with no GPU: initialization,
 * the primary context and the caller's current context, the stream and event, memory and
 * copies, module loading with a JIT log, launch geometry, the fault word, and driver errors.
 * Device memory is host memory, and a device address is the host address. One kernel exists,
 * cint_test_mul, run here in C with the semantics of the hand-written PTX kernel of the device
 * tests: out[i] = a[i] * b[i] for I64 elements, an overflowing item lowers the fault word to
 * its index, and an item above the fault word exits at its start (SPEC-02 F-9, F-10).
 *
 * cint_fake_set and cint_fake_get read and change the fake's state by key:
 *   set "attr.can_map", "attr.grid", "func.max_threads"   device and kernel attributes
 *   set "fail.<entry point>" to a CUresult                 that call fails once, after
 *   set "skip" to k                                        k more calls of it succeed
 *   get "live"      streams, events, modules, retains, and allocations not yet released
 *   get "depth"     contexts pushed and not popped; get "top": 1 when this one is current, 2 the
 *                   caller's (pushed with cint_fake_push_caller), 0 none
 *   get "launches", "launch.<k>.base", "launch.<k>.blocks", "launch.<k>.block"
 *
 * Built with CINT_FAKE_PTX (box 11 unit 4; tools/cint_check.py --backend gpu-cuda without a
 * device), it runs the PTX modules a program loads instead (rt/tests/ptx_interp.c): the
 * entries compiler/back_ptx.ci writes, every work-item one at a time, in descending order
 * within a launch unless CINT_FAKE_ORDER is "asc"; CINT_FAKE_FAULT=k lowers the fault word to
 * k after a launch whose range holds item k, as a device that misreports would. Memory is
 * then tracked: device memory and cuMemHostAlloc storage keep the host address as their
 * device address, registered memory gets another (the host address plus 2^56), as on a
 * device whose can_use_host_pointer_for_registered_mem is 0, a registration that shares a
 * page with another fails with CUDA_ERROR_HOST_MEMORY_ALREADY_REGISTERED (712), and a
 * work-item that reaches outside every allocation and mapping fails its launch with
 * CUDA_ERROR_ILLEGAL_ADDRESS (700).
 */
#define _CRT_SECURE_NO_WARNINGS /* getenv, on MSVC */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(CINT_FAKE_PTX)
#include "ptx_interp.h"
#endif

#if defined(_WIN32)
#define FAKE_API __declspec(dllexport) int __stdcall
#define FAKE_EXPORT __declspec(dllexport)
#else
#define FAKE_API int
#define FAKE_EXPORT
#endif

typedef unsigned long long cu_ptr;

#define MAX_STACK 16
#define MAX_LAUNCHES 64
#define MAX_FAILS 8

static int initialized, depth, stack[MAX_STACK], retains, can_map = 1, max_threads = 1024;
static long long grid = 2147483647LL, live, skip;
static long long launches, launch_base[MAX_LAUNCHES], launch_blocks[MAX_LAUNCHES], launch_block[MAX_LAUNCHES];
static struct {
    char name[48];
    int code;
    long long skip;
} fails[MAX_FAILS];
static char our_context;
#if !defined(CINT_FAKE_PTX)
static char mul_function, a_module;
#endif

#if defined(CINT_FAKE_PTX)
#define MAX_RANGES 256
#define REGISTERED_OFFSET ((uint64_t)1 << 56)
#define FAKE_PAGE ((uintptr_t)4096)
/* Device storage and mappings: host [host, host + bytes) at device address dev. */
static struct {
    uintptr_t host;
    size_t bytes;
    uint64_t dev;
    int registered;
} ranges[MAX_RANGES];
static int nranges;

static int range_add(void *host, size_t bytes, uint64_t dev, int registered)
{
    if (nranges == MAX_RANGES) {
        return 2;
    }
    ranges[nranges].host = (uintptr_t)host;
    ranges[nranges].bytes = bytes;
    ranges[nranges].dev = dev;
    ranges[nranges].registered = registered;
    nranges++;
    return 0;
}

static int range_drop(void *host, int registered)
{
    int i;
    for (i = 0; i < nranges; i++) {
        if (ranges[i].host == (uintptr_t)host && ranges[i].registered == registered) {
            ranges[i] = ranges[--nranges];
            return 0;
        }
    }
    return 1;
}

static void *translate(uint64_t addr, size_t bytes)
{
    int i;
    for (i = 0; i < nranges; i++) {
        if (addr >= ranges[i].dev && bytes <= ranges[i].bytes && addr - ranges[i].dev <= ranges[i].bytes - bytes) {
            return (void *)(ranges[i].host + (uintptr_t)(addr - ranges[i].dev));
        }
    }
    return NULL;
}
#endif

FAKE_EXPORT void cint_fake_reset(void)
{
    initialized = depth = retains = 0;
    can_map = 1;
    max_threads = 1024;
    grid = 2147483647LL;
    live = skip = launches = 0;
    memset(fails, 0, sizeof fails);
}

FAKE_EXPORT void cint_fake_set(const char *key, long long value)
{
    int i;
    if (strcmp(key, "attr.can_map") == 0) {
        can_map = (int)value;
    } else if (strcmp(key, "attr.grid") == 0) {
        grid = value;
    } else if (strcmp(key, "func.max_threads") == 0) {
        max_threads = (int)value;
    } else if (strcmp(key, "skip") == 0) {
        skip = value;
    } else if (strncmp(key, "fail.", 5) == 0) {
        for (i = 0; i < MAX_FAILS; i++) {
            if (fails[i].name[0] == '\0') {
                size_t len = strlen(key + 5);
                len = len < sizeof fails[i].name ? len : sizeof fails[i].name - 1u;
                memcpy(fails[i].name, key + 5, len);
                fails[i].name[len] = '\0';
                fails[i].code = (int)value;
                fails[i].skip = skip;
                skip = 0;
                return;
            }
        }
    }
}

FAKE_EXPORT long long cint_fake_get(const char *key)
{
    long long k;
    if (strcmp(key, "live") == 0) {
        return live;
    }
    if (strcmp(key, "depth") == 0) {
        return depth;
    }
    if (strcmp(key, "top") == 0) {
        return depth > 0 ? stack[depth - 1] : 0;
    }
    if (strcmp(key, "launches") == 0) {
        return launches;
    }
    if (strncmp(key, "launch.", 7) == 0) {
        const char *p = key + 7;
        for (k = 0; *p >= '0' && *p <= '9'; p++) {
            k = k * 10 + (*p - '0');
        }
        if (k < MAX_LAUNCHES && strcmp(p, ".base") == 0) {
            return launch_base[k];
        }
        if (k < MAX_LAUNCHES && strcmp(p, ".blocks") == 0) {
            return launch_blocks[k];
        }
        if (k < MAX_LAUNCHES && strcmp(p, ".block") == 0) {
            return launch_block[k];
        }
    }
    return -1;
}

/* Pushes the caller's own context, as a host that uses CUDA itself would have one current. */
FAKE_EXPORT void cint_fake_push_caller(void)
{
    stack[depth++] = 2;
}

FAKE_EXPORT void cint_fake_pop_caller(void)
{
    if (depth > 0) {
        depth--;
    }
}

/* The injected failure of `name`, or 0; then 3 before cuInit and 201 with no current context. */
static int check(const char *name, int needs_context)
{
    int i;
    for (i = 0; i < MAX_FAILS; i++) {
        if (fails[i].name[0] != '\0' && strcmp(fails[i].name, name) == 0) {
            if (fails[i].skip > 0) {
                fails[i].skip--;
                break;
            }
            fails[i].name[0] = '\0';
            return fails[i].code;
        }
    }
    if (!initialized && strcmp(name, "cuInit") != 0) {
        return 3;
    }
    if (needs_context && (depth == 0 || stack[depth - 1] != 1)) {
        return 201;
    }
    return 0;
}

static int failed_with;
#define CHECK(name, needs_context)                                                                   \
    if ((failed_with = check((name), (needs_context))) != 0) {                                      \
        return failed_with;                                                                          \
    }

FAKE_API cuInit(unsigned flags)
{
    CHECK("cuInit", 0);
    initialized = flags == 0u;
    return initialized ? 0 : 1;
}

FAKE_API cuDriverGetVersion(int *v)
{
    *v = 13040;
    return 0;
}

FAKE_API cuGetErrorName(int r, const char **name)
{
    static const struct {
        int code;
        const char *name;
    } names[] = {
        {0, "CUDA_SUCCESS"}, {1, "CUDA_ERROR_INVALID_VALUE"}, {2, "CUDA_ERROR_OUT_OF_MEMORY"},
        {3, "CUDA_ERROR_NOT_INITIALIZED"}, {101, "CUDA_ERROR_INVALID_DEVICE"},
        {201, "CUDA_ERROR_INVALID_CONTEXT"}, {218, "CUDA_ERROR_INVALID_PTX"},
        {400, "CUDA_ERROR_INVALID_HANDLE"}, {500, "CUDA_ERROR_NOT_FOUND"},
        {700, "CUDA_ERROR_ILLEGAL_ADDRESS"}, {702, "CUDA_ERROR_LAUNCH_TIMEOUT"}, {999, "CUDA_ERROR_UNKNOWN"},
    };
    size_t i;
    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (names[i].code == r) {
            *name = names[i].name;
            return 0;
        }
    }
    *name = NULL;
    return 1;
}

FAKE_API cuDeviceGet(int *device, int ordinal)
{
    CHECK("cuDeviceGet", 0);
    if (ordinal != 0) {
        return 101;
    }
    *device = 0;
    return 0;
}

FAKE_API cuDeviceGetName(char *name, int len, int device)
{
    static const char text[] = "cint fake device";
    CHECK("cuDeviceGetName", 0);
    if (device != 0 || len < (int)sizeof text) {
        return 1;
    }
    memcpy(name, text, sizeof text);
    return 0;
}

FAKE_API cuDeviceGetAttribute(int *v, int attribute, int device)
{
    CHECK("cuDeviceGetAttribute", 0);
    if (device != 0) {
        return 101;
    }
    switch (attribute) {
    case 1: *v = 1024; break;
    case 5: *v = (int)grid; break;
    case 17: *v = 1; break;
    case 19: *v = can_map; break;
    case 41: *v = 1; break;
    case 75: *v = 8; break;
    case 76: *v = 6; break;
    default: return 1;
    }
    return 0;
}

FAKE_API cuDevicePrimaryCtxRetain(void **context, int device)
{
    CHECK("cuDevicePrimaryCtxRetain", 0);
    if (device != 0) {
        return 101;
    }
    retains++;
    live++;
    *context = &our_context;
    return 0;
}

FAKE_API cuDevicePrimaryCtxRelease_v2(int device)
{
    CHECK("cuDevicePrimaryCtxRelease_v2", 0);
    if (device != 0 || retains == 0) {
        return 201;
    }
    retains--;
    live--;
    return 0;
}

FAKE_API cuCtxPushCurrent_v2(void *context)
{
    CHECK("cuCtxPushCurrent_v2", 0);
    if (context != &our_context || retains == 0 || depth == MAX_STACK) {
        return 201;
    }
    stack[depth++] = 1;
    return 0;
}

FAKE_API cuCtxPopCurrent_v2(void **context)
{
    CHECK("cuCtxPopCurrent_v2", 0);
    if (depth == 0 || stack[depth - 1] != 1) {
        return 201;
    }
    depth--;
    *context = &our_context;
    return 0;
}

static void *object(void)
{
    void *p = malloc(1);
    if (p != NULL) {
        live++;
    }
    return p;
}

static int release(void *p)
{
    if (p == NULL) {
        return 400;
    }
    free(p);
    live--;
    return 0;
}

FAKE_API cuStreamCreate(void **stream, unsigned flags)
{
    CHECK("cuStreamCreate", 1);
    return flags != 1u ? 1 : (*stream = object()) != NULL ? 0 : 2;
}

FAKE_API cuStreamDestroy_v2(void *stream)
{
    CHECK("cuStreamDestroy_v2", 1);
    return release(stream);
}

FAKE_API cuStreamSynchronize(void *stream)
{
    CHECK("cuStreamSynchronize", 1);
    return stream == NULL ? 400 : 0;
}

FAKE_API cuStreamWaitEvent(void *stream, void *event, unsigned flags)
{
    CHECK("cuStreamWaitEvent", 1);
    return stream == NULL || event == NULL || flags != 0u ? 400 : 0;
}

FAKE_API cuEventCreate(void **event, unsigned flags)
{
    CHECK("cuEventCreate", 1);
    return flags != 2u ? 1 : (*event = object()) != NULL ? 0 : 2;
}

FAKE_API cuEventRecord(void *event, void *stream)
{
    CHECK("cuEventRecord", 1);
    (void)stream; /* NULL is the default stream */
    return event == NULL ? 400 : 0;
}

FAKE_API cuEventDestroy_v2(void *event)
{
    CHECK("cuEventDestroy_v2", 1);
    return release(event);
}

#if defined(CINT_FAKE_PTX)
FAKE_API cuModuleLoadDataEx(void **module, const void *image, unsigned n, int *options, void **values)
{
    char log[512] = "";
    ptx_module *m;
    unsigned i;
    CHECK("cuModuleLoadDataEx", 1);
    if ((m = ptx_load((const char *)image, log, sizeof log)) == NULL) {
        for (i = 0; i + 1u < n; i++) {
            if (options[i] == 5 && options[i + 1u] == 6 && (uintptr_t)values[i + 1u] > strlen(log)) {
                memcpy(values[i], log, strlen(log) + 1u);
            }
        }
        return 218;
    }
    live++;
    *module = m;
    return 0;
}

FAKE_API cuModuleGetFunction(void **function, void *module, const char *name)
{
    ptx_entry *e;
    CHECK("cuModuleGetFunction", 1);
    if (module == NULL) {
        return 400;
    }
    if ((e = ptx_find((ptx_module *)module, name)) == NULL) {
        return 500;
    }
    *function = e;
    return 0;
}

FAKE_API cuModuleUnload(void *module)
{
    CHECK("cuModuleUnload", 1);
    if (module == NULL) {
        return 400;
    }
    ptx_free((ptx_module *)module);
    live--;
    return 0;
}

FAKE_API cuFuncGetAttribute(int *v, int attribute, void *function)
{
    CHECK("cuFuncGetAttribute", 1);
    if (function == NULL || attribute != 0) {
        return 1;
    }
    *v = max_threads;
    return 0;
}

FAKE_API cuLaunchKernel(void *function, unsigned gx, unsigned gy, unsigned gz, unsigned bx, unsigned by,
                        unsigned bz, unsigned shared, void *stream, void **params, void **extra)
{
    const char *order = getenv("CINT_FAKE_ORDER");
    cu_ptr base;
    CHECK("cuLaunchKernel", 1);
    if (function == NULL || gx == 0u || gy != 1u || gz != 1u || bx == 0u || bx > 1024u || by != 1u ||
        bz != 1u || shared != 0u || stream == NULL || params == NULL || extra != NULL ||
        (long long)gx > grid || (int)bx > max_threads) {
        return 1;
    }
    memcpy(&base, params[0], sizeof base);
    if (launches < MAX_LAUNCHES) {
        launch_base[launches] = (long long)base;
        launch_blocks[launches] = (long long)gx;
        launch_block[launches] = (long long)bx;
    }
    launches++;
    int rc = ptx_launch((const ptx_entry *)function, gx, bx, params, translate,
                        order != NULL && strcmp(order, "asc") == 0);
    /* CINT_FAKE_FAULT=k: a device that reports item k faulted although it did not, for the
     * host's re-execution to find the divergence of F-11. */
    const char *inject = getenv("CINT_FAKE_FAULT");
    if (rc == 0 && inject != NULL) {
        cu_ptr k = (cu_ptr)strtoull(inject, NULL, 10), word = 0u;
        uint64_t *fault;
        memcpy(&word, params[2], sizeof word);
        fault = (uint64_t *)translate(word, sizeof *fault);
        if (fault != NULL && k >= base && k - base < (cu_ptr)gx * bx && k < *fault) {
            *fault = k;
        }
    }
    return rc;
}

#else
FAKE_API cuModuleLoadDataEx(void **module, const void *image, unsigned n, int *options, void **values)
{
    static const char log[] = "ptxas fatal   : cint_test_bad is a test error";
    unsigned i;
    CHECK("cuModuleLoadDataEx", 1);
    if (strstr((const char *)image, "cint_test_bad") != NULL) {
        for (i = 0; i < n; i++) {
            if (options[i] == 5 && n == 2u && options[1] == 6 && (uintptr_t)values[1] > sizeof log) {
                memcpy(values[i], log, sizeof log);
            }
        }
        return 218;
    }
    live++;
    *module = &a_module;
    return 0;
}

FAKE_API cuModuleGetFunction(void **function, void *module, const char *name)
{
    CHECK("cuModuleGetFunction", 1);
    if (module != &a_module) {
        return 400;
    }
    if (strcmp(name, "cint_test_mul") != 0) {
        return 500;
    }
    *function = &mul_function;
    return 0;
}

FAKE_API cuModuleUnload(void *module)
{
    CHECK("cuModuleUnload", 1);
    if (module != &a_module) {
        return 400;
    }
    live--;
    return 0;
}

FAKE_API cuFuncGetAttribute(int *v, int attribute, void *function)
{
    CHECK("cuFuncGetAttribute", 1);
    if (function != &mul_function || attribute != 0) {
        return 1;
    }
    *v = max_threads;
    return 0;
}

/* cint_test_mul over one launch: items base + linear index, below end and not above the fault word. */
static void run_mul(uint64_t base, uint64_t end, uint64_t *fault, const int64_t *a, const int64_t *b, int64_t *out,
                    uint64_t items)
{
    uint64_t k;
    for (k = 0; k < items; k++) {
        const uint64_t i = base + k;
        int64_t product;
        if (i >= end || i > *fault) {
            continue;
        }
        if ((a[i] == -1 && b[i] == INT64_MIN) || (b[i] == -1 && a[i] == INT64_MIN) ||
            (a[i] != 0 && b[i] != 0 && a[i] != -1 && b[i] != -1 &&
             ((a[i] > 0) == (b[i] > 0) ? (a[i] > 0 ? a[i] > INT64_MAX / b[i] : a[i] < INT64_MAX / b[i])
                                         : (a[i] > 0 ? b[i] < INT64_MIN / a[i] : a[i] < INT64_MIN / b[i])))) {
            if (i < *fault) {
                *fault = i;
            }
            continue;
        }
        product = a[i] * b[i];
        out[i] = product;
    }
}

FAKE_API cuLaunchKernel(void *function, unsigned gx, unsigned gy, unsigned gz, unsigned bx, unsigned by,
                        unsigned bz, unsigned shared, void *stream, void **params, void **extra)
{
    cu_ptr base, end, fault, a, b, out;
    CHECK("cuLaunchKernel", 1);
    if (function != &mul_function || gx == 0u || gy != 1u || gz != 1u || bx == 0u || bx > 1024u || by != 1u ||
        bz != 1u || shared != 0u || stream == NULL || params == NULL || extra != NULL ||
        (long long)gx > grid || (int)bx > max_threads) {
        return 1;
    }
    memcpy(&base, params[0], sizeof base);
    memcpy(&end, params[1], sizeof end);
    memcpy(&fault, params[2], sizeof fault);
    memcpy(&a, params[3], sizeof a);
    memcpy(&b, params[4], sizeof b);
    memcpy(&out, params[5], sizeof out);
    if (launches < MAX_LAUNCHES) {
        launch_base[launches] = (long long)base;
        launch_blocks[launches] = (long long)gx;
        launch_block[launches] = (long long)bx;
    }
    launches++;
    run_mul(base, end, (uint64_t *)(uintptr_t)fault, (const int64_t *)(uintptr_t)a, (const int64_t *)(uintptr_t)b,
            (int64_t *)(uintptr_t)out, (uint64_t)gx * bx);
    return 0;
}

#endif

FAKE_API cuMemAlloc_v2(cu_ptr *device, size_t bytes)
{
    void *p;
    CHECK("cuMemAlloc_v2", 1);
    if (bytes == 0u || (p = malloc(bytes)) == NULL) {
        return 2;
    }
#if defined(CINT_FAKE_PTX)
    if (range_add(p, bytes, (uint64_t)(uintptr_t)p, 0) != 0) {
        free(p);
        return 2;
    }
#endif
    live++;
    *device = (cu_ptr)(uintptr_t)p;
    return 0;
}

FAKE_API cuMemFree_v2(cu_ptr device)
{
    CHECK("cuMemFree_v2", 1);
#if defined(CINT_FAKE_PTX)
    (void)range_drop((void *)(uintptr_t)device, 0);
#endif
    return release((void *)(uintptr_t)device);
}

FAKE_API cuMemcpyHtoDAsync_v2(cu_ptr dst, const void *src, size_t bytes, void *stream)
{
    CHECK("cuMemcpyHtoDAsync_v2", 1);
    if (stream == NULL) {
        return 400;
    }
    memcpy((void *)(uintptr_t)dst, src, bytes);
    return 0;
}

FAKE_API cuMemcpyDtoHAsync_v2(void *dst, cu_ptr src, size_t bytes, void *stream)
{
    CHECK("cuMemcpyDtoHAsync_v2", 1);
    if (stream == NULL) {
        return 400;
    }
    memcpy(dst, (const void *)(uintptr_t)src, bytes);
    return 0;
}

FAKE_API cuMemcpyDtoDAsync_v2(cu_ptr dst, cu_ptr src, size_t bytes, void *stream)
{
    CHECK("cuMemcpyDtoDAsync_v2", 1);
    if (stream == NULL) {
        return 400;
    }
    memmove((void *)(uintptr_t)dst, (const void *)(uintptr_t)src, bytes);
    return 0;
}

FAKE_API cuMemsetD32Async(cu_ptr dst, unsigned value, size_t n, void *stream)
{
    uint32_t v = value;
    size_t i;
    CHECK("cuMemsetD32Async", 1);
    if (stream == NULL) {
        return 400;
    }
    for (i = 0; i < n; i++) {
        memcpy((unsigned char *)(uintptr_t)dst + 4u * i, &v, sizeof v);
    }
    return 0;
}

FAKE_API cuMemHostAlloc(void **host, size_t bytes, unsigned flags)
{
    CHECK("cuMemHostAlloc", 1);
    if (flags != 2u || bytes == 0u || (*host = malloc(bytes)) == NULL) {
        return flags != 2u ? 1 : 2;
    }
#if defined(CINT_FAKE_PTX)
    if (range_add(*host, bytes, (uint64_t)(uintptr_t)*host, 0) != 0) {
        free(*host);
        return 2;
    }
#endif
    live++;
    return 0;
}

FAKE_API cuMemFreeHost(void *host)
{
    CHECK("cuMemFreeHost", 1);
#if defined(CINT_FAKE_PTX)
    (void)range_drop(host, 0);
#endif
    return release(host);
}

FAKE_API cuMemHostGetDevicePointer_v2(cu_ptr *device, void *host, unsigned flags)
{
    CHECK("cuMemHostGetDevicePointer_v2", 1);
    if (host == NULL || flags != 0u) {
        return 1;
    }
#if defined(CINT_FAKE_PTX)
    {
        int i;
        for (i = 0; i < nranges; i++) {
            if (ranges[i].host == (uintptr_t)host) {
                *device = (cu_ptr)ranges[i].dev;
                return 0;
            }
        }
        return 1;
    }
#else
    *device = (cu_ptr)(uintptr_t)host;
    return 0;
#endif
}

FAKE_API cuMemHostRegister_v2(void *host, size_t bytes, unsigned flags)
{
    CHECK("cuMemHostRegister_v2", 1);
    if (host == NULL || bytes == 0u || flags != 2u) {
        return 1;
    }
#if defined(CINT_FAKE_PTX)
    {
        const uintptr_t lo = (uintptr_t)host / FAKE_PAGE, hi = ((uintptr_t)host + bytes - 1u) / FAKE_PAGE;
        int i;
        for (i = 0; i < nranges; i++) {
            if (ranges[i].registered && ranges[i].host / FAKE_PAGE <= hi &&
                lo <= (ranges[i].host + ranges[i].bytes - 1u) / FAKE_PAGE) {
                return 712;
            }
        }
        if (range_add(host, bytes, (uint64_t)(uintptr_t)host + REGISTERED_OFFSET, 1) != 0) {
            return 2;
        }
    }
#endif
    live++;
    return 0;
}

FAKE_API cuMemHostUnregister(void *host)
{
    CHECK("cuMemHostUnregister", 1);
    if (host == NULL) {
        return 1;
    }
#if defined(CINT_FAKE_PTX)
    if (range_drop(host, 1) != 0) {
        return 713; /* CUDA_ERROR_HOST_MEMORY_NOT_REGISTERED */
    }
#endif
    live--;
    return 0;
}
