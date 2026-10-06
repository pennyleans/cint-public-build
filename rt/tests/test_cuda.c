/* test_cuda.c: tests of rt/cint_cuda.c (box 11, unit 2), built with CINT_CUDA_TESTING.
 *
 *   test_cuda fake <library>      the host protocol against rt/tests/cuda_fake.c
 *   test_cuda missing <path>      a driver library that does not exist
 *   test_cuda default             opens the real driver and prints the outcome on one line
 *   test_cuda device              hand-written PTX on a real NVIDIA device
 *
 * Each mode prints one line per failed check and exits 1 when one failed, 0 otherwise.
 * rt/tests/test_cuda.py builds and runs it.
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "cint_cuda.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

static int failures;

static void expect(int ok, int line, const char *text)
{
    if (!ok) {
        printf("FAIL test_cuda.c:%d: %s\n", line, text);
        failures++;
    }
}
#define EXPECT(cond) expect((cond) != 0, __LINE__, #cond)

/* The kernel of the device tests, with the parameter convention of cint_cuda.h: base, end,
 * fault, then a, b, out. out[i] = a[i] * b[i] for I64 elements, with the check of SPEC-02 B-1:
 * the high half of the exact product must be the sign of the low half. */
static const char MUL_PTX[] =
    ".version 6.0\n"
    ".target sm_52\n"
    ".address_size 64\n"
    "\n"
    ".visible .entry cint_test_mul(\n"
    "    .param .u64 p_base, .param .u64 p_end, .param .u64 p_fault,\n"
    "    .param .u64 p_a, .param .u64 p_b, .param .u64 p_out)\n"
    "{\n"
    "    .reg .pred %p<4>;\n"
    "    .reg .b32 %r<4>;\n"
    "    .reg .b64 %rd<18>;\n"
    "    ld.param.u64 %rd5, [p_base];\n"
    "    ld.param.u64 %rd6, [p_end];\n"
    "    ld.param.u64 %rd4, [p_fault];\n"
    "    ld.param.u64 %rd1, [p_a];\n"
    "    ld.param.u64 %rd2, [p_b];\n"
    "    ld.param.u64 %rd3, [p_out];\n"
    "    cvta.to.global.u64 %rd1, %rd1;\n"
    "    cvta.to.global.u64 %rd2, %rd2;\n"
    "    cvta.to.global.u64 %rd3, %rd3;\n"
    "    cvta.to.global.u64 %rd4, %rd4;\n"
    "    mov.u32 %r1, %ctaid.x;\n"
    "    mov.u32 %r2, %ntid.x;\n"
    "    mov.u32 %r3, %tid.x;\n"
    "    mul.wide.u32 %rd7, %r1, %r2;\n"
    "    cvt.u64.u32 %rd8, %r3;\n"
    "    add.u64 %rd7, %rd7, %rd8;\n"
    "    add.u64 %rd7, %rd7, %rd5;\n"
    "    setp.ge.u64 %p1, %rd7, %rd6;\n"
    "    @%p1 bra DONE;\n"
    "    ld.volatile.global.u64 %rd9, [%rd4];\n"
    "    setp.gt.u64 %p2, %rd7, %rd9;\n"
    "    @%p2 bra DONE;\n"
    "    shl.b64 %rd10, %rd7, 3;\n"
    "    add.u64 %rd11, %rd1, %rd10;\n"
    "    ld.global.u64 %rd12, [%rd11];\n"
    "    add.u64 %rd11, %rd2, %rd10;\n"
    "    ld.global.u64 %rd13, [%rd11];\n"
    "    mul.lo.s64 %rd14, %rd12, %rd13;\n"
    "    mul.hi.s64 %rd15, %rd12, %rd13;\n"
    "    shr.s64 %rd16, %rd14, 63;\n"
    "    setp.ne.s64 %p3, %rd15, %rd16;\n"
    "    @%p3 bra FAULT;\n"
    "    add.u64 %rd11, %rd3, %rd10;\n"
    "    st.global.u64 [%rd11], %rd14;\n"
    "    bra DONE;\n"
    "FAULT:\n"
    "    atom.global.min.u64 %rd17, [%rd4], %rd7;\n"
    "DONE:\n"
    "    ret;\n"
    "}\n";

static const char BAD_PTX[] = ".version 6.0\n.target sm_52\n.address_size 64\n.visible .entry cint_test_bad( {\n";

/* Pairs at the I64 product boundary (the unit 0 rows); the first overflow is row 3. */
static const struct {
    int64_t a, b;
    int overflow;
} ROWS[] = {
    {0, 0, 0}, {1, -1, 0}, {3037000499LL, 3037000499LL, 0}, {3037000500LL, 3037000500LL, 1},
    {-3037000499LL, 3037000499LL, 0}, {INT64_MIN, 1, 0}, {INT64_MIN, -1, 1}, {-1, INT64_MIN, 1},
    {INT64_MAX, 1, 0}, {INT64_MAX, 2, 1}, {INT64_MAX, -1, 0}, {4294967296LL, 2147483648LL, 1},
    {4294967296LL, -2147483648LL, 0}, {-4294967296LL, -2147483648LL, 1}, {123456789LL, -987654321LL, 0},
    {INT64_MIN, 0, 0},
};
#define NROWS ((int64_t)(sizeof ROWS / sizeof ROWS[0]))
#define SENTINEL INT64_C(0x5A5A5A5A5A5A5A5A)

/* The exact product of a non-overflowing row, by halves, with no signed overflow in C. */
static int64_t product(int64_t a, int64_t b)
{
    uint64_t p = (uint64_t)a * (uint64_t)b;
    return p > (uint64_t)INT64_MAX ? -(int64_t)(~p) - 1 : (int64_t)p;
}

/* ------------------------------------------------------------------------- */
/* The fake driver's controls.                                                */

static void (*fake_reset)(void);
static void (*fake_set)(const char *, long long);
static long long (*fake_get)(const char *);
static void (*fake_push_caller)(void);
static void (*fake_pop_caller)(void);

static int load_fake(const char *path)
{
    static const char *const names[] = {"cint_fake_reset", "cint_fake_set", "cint_fake_get", "cint_fake_push_caller",
                                        "cint_fake_pop_caller"};
    void *slots[5];
    size_t i;
#if defined(_WIN32)
    HMODULE lib = LoadLibraryExA(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
#else
    void *lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
    if (lib == NULL) {
        return 0;
    }
    for (i = 0; i < 5u; i++) {
#if defined(_WIN32)
        FARPROC p = GetProcAddress(lib, names[i]);
#else
        void *p = dlsym(lib, names[i]);
#endif
        if (p == NULL) {
            return 0;
        }
        memcpy(&slots[i], &p, sizeof p);
    }
    memcpy(&fake_reset, &slots[0], sizeof slots[0]);
    memcpy(&fake_set, &slots[1], sizeof slots[1]);
    memcpy(&fake_get, &slots[2], sizeof slots[2]);
    memcpy(&fake_push_caller, &slots[3], sizeof slots[3]);
    memcpy(&fake_pop_caller, &slots[4], sizeof slots[4]);
    return 1;
}

static int counted_allocs, counted_releases;

static void *counting_alloc(void *user, size_t bytes, size_t align)
{
    (void)user;
    (void)align;
    counted_allocs++;
    return malloc(bytes);
}

static void counting_release(void *user, void *ptr, size_t bytes, size_t align)
{
    (void)user;
    (void)bytes;
    (void)align;
    counted_releases++;
    free(ptr);
}

static int error_is(const cint_cuda *c, const char *call, const char *detail)
{
    cint_cuda_error e;
    if (cint_cuda_last_error(c, &e) != CINT_OK) {
        return 0;
    }
    if (strcmp(e.call, call) != 0 || strcmp(e.detail, detail) != 0) {
        printf("  last error: %s %s\n", e.call, e.detail);
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------------- */
/* A dispatch of cint_test_mul over storage the test fills.                    */

struct mul_buffers {
    int64_t *a, *b, *out; /* host views */
    uint64_t da, db, dout; /* device addresses */
};

static void fill_rows(struct mul_buffers *m, int64_t n)
{
    int64_t i;
    for (i = 0; i < n; i++) {
        m->a[i] = ROWS[i % NROWS].a;
        m->b[i] = ROWS[i % NROWS].b;
        m->out[i] = SENTINEL;
    }
}

/* The rows with every overflowing pair made safe (1 * b), so that a test can place its own faults. */
static void fill_safe(struct mul_buffers *m, int64_t n)
{
    int64_t i;
    fill_rows(m, n);
    for (i = 0; i < n; i++) {
        if (ROWS[i % NROWS].overflow) {
            m->a[i] = 1;
        }
    }
}

static cint_status run_mul(cint_cuda *c, const cint_cuda_kernel *k, struct mul_buffers *m, int64_t n, uint32_t block,
                           cint_cuda_launch_info *info)
{
    void *params[3];
    params[0] = &m->da;
    params[1] = &m->db;
    params[2] = &m->dout;
    return cint_cuda_launch(c, k, n, block, params, 3u, info);
}

/* Every output below `fault` is its item's product; a test checks the items above it. */
static int outputs_ok(const struct mul_buffers *m, int64_t n, uint64_t fault)
{
    int64_t i;
    for (i = 0; i < n && (uint64_t)i < fault; i++) {
        if (m->out[i] != product(m->a[i], m->b[i])) {
            printf("  item %lld: %lld\n", (long long)i, (long long)m->out[i]);
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Mode fake.                                                                  */

static void fake_refusals(const char *path)
{
    cint_cuda *c = NULL;
    cint_cuda_device bad = {4u, 0};
    cint_cuda_device negative = {(uint32_t)sizeof(cint_cuda_device), -1};
    cint_allocator half = {counting_alloc, NULL, NULL};
    EXPECT(cint_cuda_open_driver(NULL, NULL, NULL, &c, NULL) == CINT_REFUSED);
    EXPECT(cint_cuda_open_driver(path, NULL, NULL, NULL, NULL) == CINT_REFUSED);
    EXPECT(cint_cuda_open_driver(path, &bad, NULL, &c, NULL) == CINT_REFUSED);
    EXPECT(cint_cuda_open_driver(path, &negative, NULL, &c, NULL) == CINT_REFUSED);
    EXPECT(cint_cuda_open_driver(path, NULL, &half, &c, NULL) == CINT_REFUSED);
    EXPECT(c == NULL && counted_allocs == 0);
    EXPECT(cint_cuda_info_get(NULL, NULL) == CINT_REFUSED);
    EXPECT(cint_cuda_sync(NULL) == CINT_REFUSED);
    EXPECT(cint_cuda_stream(NULL) == 0u);
    EXPECT(strcmp(cint_cuda_jit_log(NULL), "") == 0);
    cint_cuda_close(NULL);
}

/* A failure at open releases everything it made and reports the call. */
static void fake_open_failures(const char *path)
{
    static const struct {
        const char *key;
        long long value;
        const char *call, *detail;
    } cases[] = {
        {"fail.cuInit", 999, "cuInit", "CUDA_ERROR_UNKNOWN"},
        {"fail.cuDeviceGetAttribute", 1, "cuDeviceGetAttribute", "CUDA_ERROR_INVALID_VALUE"},
        {"fail.cuDevicePrimaryCtxRetain", 2, "cuDevicePrimaryCtxRetain", "CUDA_ERROR_OUT_OF_MEMORY"},
        {"fail.cuStreamCreate", 2, "cuStreamCreate", "CUDA_ERROR_OUT_OF_MEMORY"},
        {"fail.cuEventCreate", 2, "cuEventCreate", "CUDA_ERROR_OUT_OF_MEMORY"},
        {"fail.cuMemAlloc_v2", 2, "cuMemAlloc", "CUDA_ERROR_OUT_OF_MEMORY"},
        {"attr.can_map", 0, "cuDeviceGetAttribute", "the device cannot map host memory (M-28a)"},
    };
    cint_cuda_device second = {(uint32_t)sizeof(cint_cuda_device), 1};
    cint_cuda_error e;
    cint_cuda *c = NULL;
    size_t i;
    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        fake_reset();
        fake_set(cases[i].key, cases[i].value);
        memset(&e, 0, sizeof e);
        EXPECT(cint_cuda_open_driver(path, NULL, NULL, &c, &e) == CINT_RESOURCE);
        if (strcmp(e.call, cases[i].call) != 0 || strcmp(e.detail, cases[i].detail) != 0) {
            printf("FAIL open case %s: %s %s\n", cases[i].key, e.call, e.detail);
            failures++;
        }
        EXPECT(fake_get("live") == 0 && fake_get("depth") == 0);
    }
    fake_reset();
    EXPECT(cint_cuda_open_driver(path, &second, NULL, &c, &e) == CINT_RESOURCE);
    EXPECT(strcmp(e.call, "cuDeviceGet") == 0 && strcmp(e.detail, "CUDA_ERROR_INVALID_DEVICE") == 0);
    EXPECT(e.driver_code == 101);
    EXPECT(c == NULL && fake_get("live") == 0);
}

static void fake_memory(cint_cuda *c)
{
    int64_t buf[8], back[8];
    void *host = NULL;
    uint64_t d = 0, mapped = 0, reg = 0;
    int i;
    for (i = 0; i < 8; i++) {
        buf[i] = (int64_t)i * 7 - 20;
        back[i] = 0;
    }
    EXPECT(cint_cuda_host_alloc(c, 0u, &host, &mapped) == CINT_REFUSED);
    EXPECT(cint_cuda_host_alloc(c, sizeof buf, &host, &mapped) == CINT_OK && host != NULL && mapped != 0u);
    EXPECT(cint_cuda_host_register(c, buf, sizeof buf, &reg) == CINT_OK && reg != 0u);
    EXPECT(cint_cuda_alloc(c, sizeof buf, &d) == CINT_OK && d != 0u);
    EXPECT(cint_cuda_copy_to_device(c, d, buf, sizeof buf) == CINT_OK);
    EXPECT(cint_cuda_copy_on_device(c, mapped, d, sizeof buf) == CINT_OK);
    EXPECT(cint_cuda_sync(c) == CINT_OK);
    EXPECT(memcmp(host, buf, sizeof buf) == 0);
    EXPECT(cint_cuda_copy_to_host(c, back, mapped, sizeof back) == CINT_OK && memcmp(back, buf, sizeof buf) == 0);
    EXPECT(cint_cuda_wait_stream(c, 0u) == CINT_OK);
    EXPECT(cint_cuda_free(c, d) == CINT_OK);
    EXPECT(cint_cuda_host_unregister(c, buf) == CINT_OK);
    EXPECT(cint_cuda_host_free(c, host) == CINT_OK);
    EXPECT(cint_cuda_free(c, 0u) == CINT_REFUSED);
    fake_set("fail.cuMemHostAlloc", 2);
    EXPECT(cint_cuda_host_alloc(c, sizeof buf, &host, &mapped) == CINT_RESOURCE);
    EXPECT(error_is(c, "cuMemHostAlloc", "CUDA_ERROR_OUT_OF_MEMORY"));
    fake_set("fail.cuMemHostGetDevicePointer_v2", 1);
    EXPECT(cint_cuda_host_register(c, buf, sizeof buf, &reg) == CINT_RESOURCE);
    EXPECT(error_is(c, "cuMemHostGetDevicePointer", "CUDA_ERROR_INVALID_VALUE"));
}

static void fake_modules(cint_cuda *c, cint_cuda_module *m, cint_cuda_kernel *k)
{
    cint_cuda_kernel none;
    cint_cuda_module bad;
    EXPECT(cint_cuda_module_load(c, BAD_PTX, &bad) == CINT_RESOURCE);
    EXPECT(error_is(c, "cuModuleLoadDataEx", "CUDA_ERROR_INVALID_PTX"));
    EXPECT(strstr(cint_cuda_jit_log(c), "test error") != NULL);
    EXPECT(cint_cuda_module_load(c, MUL_PTX, m) == CINT_OK && m->module != NULL);
    EXPECT(cint_cuda_jit_log(c)[0] == '\0');
    EXPECT(cint_cuda_kernel_get(c, m, "no_such_entry", &none) == CINT_RESOURCE);
    EXPECT(error_is(c, "cuModuleGetFunction", "CUDA_ERROR_NOT_FOUND"));
    EXPECT(cint_cuda_kernel_get(c, m, "cint_test_mul", k) == CINT_OK && k->max_threads == 1024);
}

static void fake_launches(cint_cuda *c, const cint_cuda_kernel *k)
{
    int64_t a[40], b[40], out[40];
    struct mul_buffers m;
    cint_cuda_launch_info info;
    void *params[3];
    m.a = a, m.b = b, m.out = out;
    m.da = (uint64_t)(uintptr_t)a, m.db = (uint64_t)(uintptr_t)b, m.dout = (uint64_t)(uintptr_t)out;

    /* One launch: the first overflowing row is the record (F-11). */
    fill_rows(&m, NROWS);
    EXPECT(run_mul(c, k, &m, NROWS, 0u, &info) == CINT_OK);
    EXPECT(info.block == CINT_CUDA_DEFAULT_BLOCK && info.launches == 1 && info.fault == 3u);
    EXPECT(fake_get("launch.0.base") == 0 && fake_get("launch.0.blocks") == 1 && fake_get("launch.0.block") == 256);
    EXPECT(outputs_ok(&m, NROWS, info.fault));

    /* No overflow: the word stays all ones. */
    fill_rows(&m, 3);
    EXPECT(run_mul(c, k, &m, 3, 32u, &info) == CINT_OK && info.fault == UINT64_MAX && outputs_ok(&m, 3, UINT64_MAX));

    /* A grid limit of 2 blocks of 4 items: 8 items per launch. Only items 19 and 30 overflow;
     * the launch over 24 to 31 lies above 19 and is not started (F-3). */
    EXPECT(cint_cuda_test_grid_limit(c, 2) == CINT_OK);
    fill_safe(&m, 40);
    m.a[19] = INT64_MAX, m.b[19] = 2;
    m.a[30] = INT64_MAX, m.b[30] = 2;
    EXPECT(run_mul(c, k, &m, 40, 4u, &info) == CINT_OK);
    EXPECT(info.launches == 3 && info.fault == 19u);
    EXPECT(fake_get("launches") == 5 && fake_get("launch.2.base") == 0 && fake_get("launch.3.base") == 8 &&
           fake_get("launch.4.base") == 16 && fake_get("launch.4.blocks") == 2);
    EXPECT(outputs_ok(&m, 40, info.fault));
    EXPECT(out[19] == SENTINEL && out[24] == SENTINEL && out[39] == SENTINEL);

    /* 17 items with no overflow: three launches, the last of one block. */
    fill_safe(&m, 17);
    EXPECT(run_mul(c, k, &m, 17, 4u, &info) == CINT_OK && info.launches == 3 && info.fault == UINT64_MAX);
    EXPECT(fake_get("launch.7.base") == 16 && fake_get("launch.7.blocks") == 1 && outputs_ok(&m, 17, UINT64_MAX));
    EXPECT(cint_cuda_test_grid_limit(c, 0) == CINT_REFUSED);
    EXPECT(cint_cuda_test_grid_limit(c, 2147483647LL) == CINT_OK);

    /* No items: no launch. */
    EXPECT(run_mul(c, k, &m, 0, 0u, &info) == CINT_OK && info.launches == 0 && info.fault == UINT64_MAX);
    EXPECT(fake_get("launches") == 8);

    /* Refusals make no driver call. */
    params[0] = &m.da, params[1] = &m.db, params[2] = NULL;
    EXPECT(cint_cuda_launch(c, k, -1, 0u, params, 2u, &info) == CINT_REFUSED);
    EXPECT(cint_cuda_launch(c, k, 4, 0u, params, 3u, &info) == CINT_REFUSED);
    EXPECT(cint_cuda_launch(c, k, 4, 0u, NULL, 1u, &info) == CINT_REFUSED);
    EXPECT(cint_cuda_launch(c, k, 4, 0u, params, CINT_CUDA_MAX_PARAMS + 1u, &info) == CINT_REFUSED);
    EXPECT(cint_cuda_launch(c, k, 4, 2048u, params, 2u, &info) == CINT_REFUSED);
    EXPECT(cint_cuda_launch(c, k, 4, 0u, params, 2u, NULL) == CINT_REFUSED);
    EXPECT(fake_get("launches") == 8);

    /* Driver failures are CINT_RESOURCE with the call named (B-16e). */
    fill_rows(&m, NROWS);
    fake_set("fail.cuLaunchKernel", 700);
    EXPECT(run_mul(c, k, &m, NROWS, 0u, &info) == CINT_RESOURCE);
    EXPECT(error_is(c, "cuLaunchKernel", "CUDA_ERROR_ILLEGAL_ADDRESS"));
    fake_set("fail.cuStreamSynchronize", 702);
    EXPECT(run_mul(c, k, &m, NROWS, 0u, &info) == CINT_RESOURCE);
    EXPECT(error_is(c, "cuStreamSynchronize", "CUDA_ERROR_LAUNCH_TIMEOUT"));
    EXPECT(fake_get("depth") == 1 && fake_get("top") == 2);
}

static void fake_narrow_kernel(cint_cuda *c, const cint_cuda_module *m)
{
    int64_t a[4] = {1, 2, 3, 4}, b[4] = {1, 1, 1, 1}, out[4];
    struct mul_buffers mb;
    cint_cuda_kernel narrow;
    cint_cuda_launch_info info;
    mb.a = a, mb.b = b, mb.out = out;
    mb.da = (uint64_t)(uintptr_t)a, mb.db = (uint64_t)(uintptr_t)b, mb.dout = (uint64_t)(uintptr_t)out;
    fake_set("func.max_threads", 128);
    EXPECT(cint_cuda_kernel_get(c, m, "cint_test_mul", &narrow) == CINT_OK && narrow.max_threads == 128);
    EXPECT(run_mul(c, &narrow, &mb, 4, 0u, &info) == CINT_RESOURCE);
    EXPECT(run_mul(c, &narrow, &mb, 4, 128u, &info) == CINT_OK && info.fault == UINT64_MAX && out[3] == 4);
    fake_set("func.max_threads", 1024);
}

static int mode_fake(const char *path)
{
    cint_allocator counting = {counting_alloc, counting_release, NULL};
    cint_cuda_device zero = {(uint32_t)sizeof(cint_cuda_device), 0};
    cint_cuda_info info;
    cint_cuda_module m;
    cint_cuda_kernel k;
    cint_cuda *c = NULL;
    if (!load_fake(path)) {
        printf("FAIL cannot load the fake driver %s\n", path);
        return 1;
    }
    fake_reset();
    fake_refusals(path);
    fake_open_failures(path);
    fake_reset();
    EXPECT(cint_cuda_open_driver(path, &zero, &counting, &c, NULL) == CINT_OK && c != NULL);
    if (c == NULL) {
        return 1;
    }
    EXPECT(counted_allocs == 1 && fake_get("depth") == 0 && fake_get("live") == 4);
    EXPECT(cint_cuda_info_get(c, &info) == CINT_OK);
    EXPECT(info.ordinal == 0 && info.driver_version == 13040 && info.compute_major == 8 && info.compute_minor == 6);
    EXPECT(info.can_map_host_memory == 1 && info.kernel_exec_timeout == 1 && info.max_threads_per_block == 1024);
    EXPECT(info.max_grid_blocks == 2147483647LL && strcmp(info.name, "cint fake device") == 0);
    EXPECT(cint_cuda_stream(c) != 0u);

    /* Every call leaves the caller's current context current (A-16a). */
    fake_push_caller();
    fake_memory(c);
    fake_modules(c, &m, &k);
    fake_launches(c, &k);
    fake_narrow_kernel(c, &m);
    EXPECT(fake_get("depth") == 1 && fake_get("top") == 2);
    fake_pop_caller();

    EXPECT(cint_cuda_module_unload(c, &m) == CINT_OK && m.module == NULL);
    EXPECT(cint_cuda_module_unload(c, &m) == CINT_REFUSED);
    cint_cuda_close(c);
    EXPECT(counted_releases == 1 && fake_get("live") == 0 && fake_get("depth") == 0);
    return failures != 0;
}

/* ------------------------------------------------------------------------- */
/* Modes missing and default.                                                  */

static int mode_missing(const char *path)
{
    cint_cuda_error e;
    cint_cuda *c = NULL;
    memset(&e, 0, sizeof e);
    EXPECT(cint_cuda_open_driver(path, NULL, NULL, &c, &e) == CINT_RESOURCE);
    EXPECT(c == NULL && e.driver_code == 0 && strcmp(e.call, "load") == 0 && strcmp(e.detail, path) == 0);
    EXPECT(cint_cuda_open_driver(path, NULL, NULL, &c, NULL) == CINT_RESOURCE);
    return failures != 0;
}

static int mode_default(void)
{
    cint_cuda_error e;
    cint_cuda_info info;
    cint_cuda *c = NULL;
    cint_status s = cint_cuda_open(NULL, NULL, &c, &e);
    if (s == CINT_OK) {
        EXPECT(cint_cuda_info_get(c, &info) == CINT_OK);
        printf("default ok %d.%d %s\n", (int)info.compute_major, (int)info.compute_minor, info.name);
        cint_cuda_close(c);
    } else {
        printf("default %s %s %s\n", s == CINT_RESOURCE ? "resource" : "other", e.call, e.detail);
    }
    return failures != 0;
}

/* ------------------------------------------------------------------------- */
/* Mode device.                                                                */

enum { MAPPED, REGISTERED, DEVICE_MEMORY };
static const char *const KINDS[] = {"mapped", "registered", "device"};

struct device_store {
    int kind;
    size_t bytes;
    void *host[3];
    struct mul_buffers m;
};

static int store_open(cint_cuda *c, struct device_store *s, int kind, int64_t n)
{
    uint64_t *dev[3];
    int i;
    memset(s, 0, sizeof *s);
    s->kind = kind;
    s->bytes = (size_t)n * sizeof(int64_t);
    dev[0] = &s->m.da, dev[1] = &s->m.db, dev[2] = &s->m.dout;
    for (i = 0; i < 3; i++) {
        cint_status st;
        if (kind == MAPPED) {
            st = cint_cuda_host_alloc(c, s->bytes, &s->host[i], dev[i]);
        } else {
            s->host[i] = malloc(s->bytes);
            if (s->host[i] == NULL) {
                return 0;
            }
            st = kind == REGISTERED ? cint_cuda_host_register(c, s->host[i], s->bytes, dev[i])
                                    : cint_cuda_alloc(c, s->bytes, dev[i]);
        }
        if (st != CINT_OK) {
            return 0;
        }
    }
    s->m.a = (int64_t *)s->host[0], s->m.b = (int64_t *)s->host[1], s->m.out = (int64_t *)s->host[2];
    return 1;
}

static void store_close(cint_cuda *c, struct device_store *s)
{
    uint64_t dev[3];
    int i;
    dev[0] = s->m.da, dev[1] = s->m.db, dev[2] = s->m.dout;
    for (i = 0; i < 3; i++) {
        if (s->kind == MAPPED) {
            if (s->host[i] != NULL) {
                EXPECT(cint_cuda_host_free(c, s->host[i]) == CINT_OK);
            }
            continue;
        }
        if (s->kind == REGISTERED && dev[i] != 0u) {
            EXPECT(cint_cuda_host_unregister(c, s->host[i]) == CINT_OK);
        } else if (s->kind == DEVICE_MEMORY && dev[i] != 0u) {
            EXPECT(cint_cuda_free(c, dev[i]) == CINT_OK);
        }
        free(s->host[i]);
    }
}

/* One dispatch over a store: device memory is copied in and out around it. */
static cint_status device_dispatch(cint_cuda *c, const cint_cuda_kernel *k, struct device_store *s, int64_t n,
                                   uint32_t block, cint_cuda_launch_info *info)
{
    cint_status st;
    if (s->kind == DEVICE_MEMORY) {
        if ((st = cint_cuda_copy_to_device(c, s->m.da, s->m.a, s->bytes)) != CINT_OK ||
            (st = cint_cuda_copy_to_device(c, s->m.db, s->m.b, s->bytes)) != CINT_OK ||
            (st = cint_cuda_copy_to_device(c, s->m.dout, s->m.out, s->bytes)) != CINT_OK) {
            return st;
        }
    }
    if ((st = run_mul(c, k, &s->m, n, block, info)) != CINT_OK) {
        return st;
    }
    return s->kind == DEVICE_MEMORY ? cint_cuda_copy_to_host(c, s->m.out, s->m.dout, s->bytes) : CINT_OK;
}

#define NBIG INT64_C(1000000)

static int mode_device(void)
{
    static const uint32_t blocks[] = {32u, 256u}; /* the two schedules of SPEC-09 CONF-17 */
    cint_cuda_device beyond = {(uint32_t)sizeof(cint_cuda_device), 64};
    cint_cuda_error e;
    cint_cuda_info info;
    cint_cuda_launch_info li;
    cint_cuda_module m, bad;
    cint_cuda_kernel k;
    cint_cuda *c = NULL;
    struct device_store s;
    int kind;
    size_t b;
    EXPECT(cint_cuda_open(&beyond, NULL, &c, &e) == CINT_RESOURCE && strcmp(e.call, "cuDeviceGet") == 0);
    if (cint_cuda_open(NULL, NULL, &c, &e) != CINT_OK) {
        printf("FAIL open: %s %s\n", e.call, e.detail);
        return 1;
    }
    EXPECT(cint_cuda_info_get(c, &info) == CINT_OK);
    printf("device %s, compute %d.%d, driver %d, kernel_exec_timeout %d\n", info.name, (int)info.compute_major,
           (int)info.compute_minor, (int)info.driver_version, (int)info.kernel_exec_timeout);
    EXPECT(cint_cuda_module_load(c, BAD_PTX, &bad) == CINT_RESOURCE);
    EXPECT(error_is(c, "cuModuleLoadDataEx", "CUDA_ERROR_INVALID_PTX"));
    printf("jit log of the refused module: %.120s\n", cint_cuda_jit_log(c));
    if (cint_cuda_module_load(c, MUL_PTX, &m) != CINT_OK || cint_cuda_kernel_get(c, &m, "cint_test_mul", &k) != CINT_OK) {
        printf("FAIL module: %s\n", cint_cuda_jit_log(c));
        cint_cuda_close(c);
        return 1;
    }
    EXPECT(cint_cuda_wait_stream(c, 0u) == CINT_OK);
    for (kind = MAPPED; kind <= DEVICE_MEMORY; kind++) {
        if (!store_open(c, &s, kind, NBIG)) {
            printf("FAIL store %s\n", KINDS[kind]);
            failures++;
            store_close(c, &s);
            continue;
        }
        for (b = 0; b < sizeof blocks / sizeof blocks[0]; b++) {
            /* The boundary rows: the first overflow, row 3, is the record. */
            fill_rows(&s.m, NROWS);
            EXPECT(device_dispatch(c, &k, &s, NROWS, blocks[b], &li) == CINT_OK);
            EXPECT(li.launches == 1 && li.fault == 3u && outputs_ok(&s.m, NROWS, li.fault));

            /* A million items with overflows at 500,000, 700,000 and 900,000, in launches of
             * 65,536 items: the eighth launch records 500,000 and the ninth is not started. */
            EXPECT(cint_cuda_test_grid_limit(c, (int64_t)(65536u / blocks[b])) == CINT_OK);
            fill_rows(&s.m, NBIG);
            {
                int64_t i;
                for (i = 0; i < NBIG; i++) {
                    if (ROWS[i % NROWS].overflow) {
                        s.m.a[i] = 1; /* 1 * b never overflows */
                    }
                }
                s.m.a[500000] = INT64_MAX, s.m.b[500000] = 2;
                s.m.a[700000] = INT64_MAX, s.m.b[700000] = 2;
                s.m.a[900000] = INT64_MAX, s.m.b[900000] = 2;
            }
            EXPECT(device_dispatch(c, &k, &s, NBIG, blocks[b], &li) == CINT_OK);
            EXPECT(li.launches == 8 && li.fault == 500000u);
            EXPECT(s.m.out[0] == 0 && s.m.out[499999] == product(s.m.a[499999], s.m.b[499999]));
            EXPECT(s.m.out[524288] == SENTINEL && s.m.out[999999] == SENTINEL);
            EXPECT(cint_cuda_test_grid_limit(c, info.max_grid_blocks) == CINT_OK);
            printf("%s block %u: %lld launches, fault %llu\n", KINDS[kind], (unsigned)blocks[b],
                   (long long)li.launches, (unsigned long long)li.fault);
        }
        store_close(c, &s);
    }
    EXPECT(cint_cuda_sync(c) == CINT_OK);
    EXPECT(cint_cuda_module_unload(c, &m) == CINT_OK);
    cint_cuda_close(c);
    return failures != 0;
}

int main(int argc, char **argv)
{
    int r;
    if (argc == 3 && strcmp(argv[1], "fake") == 0) {
        r = mode_fake(argv[2]);
    } else if (argc == 3 && strcmp(argv[1], "missing") == 0) {
        r = mode_missing(argv[2]);
    } else if (argc == 2 && strcmp(argv[1], "default") == 0) {
        r = mode_default();
    } else if (argc == 2 && strcmp(argv[1], "device") == 0) {
        r = mode_device();
    } else {
        fprintf(stderr, "usage: test_cuda fake LIBRARY | missing PATH | default | device\n");
        return 2;
    }
    printf("%s %s: %d failed\n", r ? "FAIL" : "PASS", argv[1], failures);
    return r;
}
