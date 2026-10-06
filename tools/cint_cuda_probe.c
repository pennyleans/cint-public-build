/* cint_cuda_probe.c: the CUDA driver probe of box 11, unit 0
 * (docs/design/notes/2026-10-05-box11-cuda-backend.md, section 9).
 *
 *   cint-cuda-probe <out> <label>
 *   cint-cuda-probe --ptx <version> <target>
 *
 * A standalone program outside the receipt identity. It loads the CUDA driver at run time
 * (nvcuda.dll on Windows; libcuda.so.1 on Linux and WSL2), so it builds with no CUDA header
 * or toolkit, as rt/cint_cuda.c will (BX11-08). It records the device attributes box 11
 * relies on, JIT-loads one hand-written PTX kernel under each .version and .target pair of
 * a small matrix (BX11-06), and runs the kernel over three kinds of storage:
 *
 *   mapped_alloc     page-locked host memory from cuMemHostAlloc, mapped into the device's
 *                    address space (BX11-03 A, storage the CUDA context allocates itself)
 *   mapped_register  ordinary host memory, page-locked and mapped by cuMemHostRegister
 *                    (BX11-03 A, storage allocated elsewhere and mapped afterwards)
 *   device_memory    device memory from cuMemAlloc, with explicit copies (BX11-03 B)
 *
 * The kernel multiplies two I64 elements per work-item with the check of B-1: the low and
 * high halves of the exact product (mul.lo.s64, mul.hi.s64) must agree in sign. A work-item
 * that overflows performs atom.global.min.u64 with its index on a U64 fault word in device
 * memory and exits without writing; a work-item above the current word exits at its start
 * (F-9, F-10). A dispatch may be split into launches over ascending index ranges, and a launch
 * whose range lies above a recorded fault is not started (BX11-10).
 *
 * It writes one "key value" line per fact to <out>, ASCII with LF line ends, and echoes each
 * line to standard output. With --ptx it prints the kernel's PTX for one pair and runs nothing.
 *
 * Exit status 0 when every test that ran passed, 1 when one failed, 2 for a usage error,
 * 3 when the driver cannot be loaded or initialized.
 */
#define _CRT_SECURE_NO_WARNINGS
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#define CUAPI __stdcall
#else
#include <dlfcn.h>
#define CUAPI
#endif

typedef int CUresult;
typedef int CUdevice;
typedef void *CUcontext, *CUmodule, *CUfunction, *CUstream;
typedef unsigned long long CUdeviceptr;

static CUresult (CUAPI *cuInit_)(unsigned);
static CUresult (CUAPI *cuDriverGetVersion_)(int *);
static CUresult (CUAPI *cuGetErrorName_)(CUresult, const char **);
static CUresult (CUAPI *cuDeviceGetCount_)(int *);
static CUresult (CUAPI *cuDeviceGet_)(CUdevice *, int);
static CUresult (CUAPI *cuDeviceGetName_)(char *, int, CUdevice);
static CUresult (CUAPI *cuDeviceGetAttribute_)(int *, int, CUdevice);
static CUresult (CUAPI *cuDevicePrimaryCtxSetFlags_)(CUdevice, unsigned);
static CUresult (CUAPI *cuDevicePrimaryCtxRetain_)(CUcontext *, CUdevice);
static CUresult (CUAPI *cuDevicePrimaryCtxRelease_)(CUdevice);
static CUresult (CUAPI *cuCtxSetCurrent_)(CUcontext);
static CUresult (CUAPI *cuCtxSynchronize_)(void);
static CUresult (CUAPI *cuStreamCreate_)(CUstream *, unsigned);
static CUresult (CUAPI *cuStreamSynchronize_)(CUstream);
static CUresult (CUAPI *cuStreamDestroy_)(CUstream);
static CUresult (CUAPI *cuModuleLoadDataEx_)(CUmodule *, const void *, unsigned, int *, void **);
static CUresult (CUAPI *cuModuleGetFunction_)(CUfunction *, CUmodule, const char *);
static CUresult (CUAPI *cuModuleUnload_)(CUmodule);
static CUresult (CUAPI *cuLaunchKernel_)(CUfunction, unsigned, unsigned, unsigned, unsigned, unsigned,
                                         unsigned, unsigned, CUstream, void **, void **);
static CUresult (CUAPI *cuMemAlloc_)(CUdeviceptr *, size_t);
static CUresult (CUAPI *cuMemFree_)(CUdeviceptr);
static CUresult (CUAPI *cuMemcpyHtoD_)(CUdeviceptr, const void *, size_t);
static CUresult (CUAPI *cuMemcpyDtoH_)(void *, CUdeviceptr, size_t);
static CUresult (CUAPI *cuMemsetD32_)(CUdeviceptr, unsigned, size_t);
static CUresult (CUAPI *cuMemHostAlloc_)(void **, size_t, unsigned);
static CUresult (CUAPI *cuMemFreeHost_)(void *);
static CUresult (CUAPI *cuMemHostGetDevicePointer_)(CUdeviceptr *, void *, unsigned);
static CUresult (CUAPI *cuMemHostRegister_)(void *, size_t, unsigned);
static CUresult (CUAPI *cuMemHostUnregister_)(void *);

/* The exported names: cuda.h maps the calls that take sizes or device pointers to their _v2
 * entry points, and the probe resolves those names, as a program built with cuda.h would. */
static const struct { void **slot; const char *name; } SYMS[] = {
    {(void **)&cuInit_, "cuInit"},
    {(void **)&cuDriverGetVersion_, "cuDriverGetVersion"},
    {(void **)&cuGetErrorName_, "cuGetErrorName"},
    {(void **)&cuDeviceGetCount_, "cuDeviceGetCount"},
    {(void **)&cuDeviceGet_, "cuDeviceGet"},
    {(void **)&cuDeviceGetName_, "cuDeviceGetName"},
    {(void **)&cuDeviceGetAttribute_, "cuDeviceGetAttribute"},
    {(void **)&cuDevicePrimaryCtxSetFlags_, "cuDevicePrimaryCtxSetFlags_v2"},
    {(void **)&cuDevicePrimaryCtxRetain_, "cuDevicePrimaryCtxRetain"},
    {(void **)&cuDevicePrimaryCtxRelease_, "cuDevicePrimaryCtxRelease_v2"},
    {(void **)&cuCtxSetCurrent_, "cuCtxSetCurrent"},
    {(void **)&cuCtxSynchronize_, "cuCtxSynchronize"},
    {(void **)&cuStreamCreate_, "cuStreamCreate"},
    {(void **)&cuStreamSynchronize_, "cuStreamSynchronize"},
    {(void **)&cuStreamDestroy_, "cuStreamDestroy_v2"},
    {(void **)&cuModuleLoadDataEx_, "cuModuleLoadDataEx"},
    {(void **)&cuModuleGetFunction_, "cuModuleGetFunction"},
    {(void **)&cuModuleUnload_, "cuModuleUnload"},
    {(void **)&cuLaunchKernel_, "cuLaunchKernel"},
    {(void **)&cuMemAlloc_, "cuMemAlloc_v2"},
    {(void **)&cuMemFree_, "cuMemFree_v2"},
    {(void **)&cuMemcpyHtoD_, "cuMemcpyHtoD_v2"},
    {(void **)&cuMemcpyDtoH_, "cuMemcpyDtoH_v2"},
    {(void **)&cuMemsetD32_, "cuMemsetD32_v2"},
    {(void **)&cuMemHostAlloc_, "cuMemHostAlloc"},
    {(void **)&cuMemFreeHost_, "cuMemFreeHost"},
    {(void **)&cuMemHostGetDevicePointer_, "cuMemHostGetDevicePointer_v2"},
    {(void **)&cuMemHostRegister_, "cuMemHostRegister_v2"},
    {(void **)&cuMemHostUnregister_, "cuMemHostUnregister"},
};

/* CUdevice_attribute values from cuda.h. */
static const struct { int id; const char *name; } ATTRS[] = {
    {75, "compute_capability_major"}, {76, "compute_capability_minor"},
    {19, "can_map_host_memory"}, {41, "unified_addressing"},
    {91, "can_use_host_pointer_for_registered_mem"}, {83, "managed_memory"},
    {88, "pageable_memory_access"}, {17, "kernel_exec_timeout"}, {35, "tcc_driver"},
    {18, "integrated"}, {20, "compute_mode"}, {16, "multiprocessor_count"},
    {10, "warp_size"}, {1, "max_threads_per_block"}, {2, "max_block_dim_x"},
    {5, "max_grid_dim_x"},
};

#define CU_CTX_MAP_HOST 0x08u
#define CU_MEMHOSTALLOC_DEVICEMAP 0x02u
#define CU_MEMHOSTREGISTER_DEVICEMAP 0x02u
#define CU_JIT_ERROR_LOG_BUFFER 5
#define CU_JIT_ERROR_LOG_BUFFER_SIZE_BYTES 6

static const char *const PTX_VERSIONS[] = {"6.0", "7.0", "7.1", "7.8", "8.0", "8.8", "9.0", "9.4"};
static const char *const PTX_TARGETS[] = {"sm_52", "sm_61", "sm_70", "sm_75", "sm_80", "sm_86", "sm_90"};
#define NVER (sizeof PTX_VERSIONS / sizeof PTX_VERSIONS[0])
#define NTGT (sizeof PTX_TARGETS / sizeof PTX_TARGETS[0])

static const char PTX_BODY[] =
    ".visible .entry probe_mul(\n"
    "    .param .u64 p_a, .param .u64 p_b, .param .u64 p_out,\n"
    "    .param .u64 p_fault, .param .u64 p_base, .param .u64 p_end)\n"
    "{\n"
    "    .reg .pred %p<4>;\n"
    "    .reg .b32 %r<4>;\n"
    "    .reg .b64 %rd<18>;\n"
    "    ld.param.u64 %rd1, [p_a];\n"
    "    ld.param.u64 %rd2, [p_b];\n"
    "    ld.param.u64 %rd3, [p_out];\n"
    "    ld.param.u64 %rd4, [p_fault];\n"
    "    ld.param.u64 %rd5, [p_base];\n"
    "    ld.param.u64 %rd6, [p_end];\n"
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
    "    add.u64 %rd7, %rd7, %rd5;\n"          /* work-item index: base + linear index */
    "    setp.ge.u64 %p1, %rd7, %rd6;\n"
    "    @%p1 bra DONE;\n"
    "    ld.volatile.global.u64 %rd9, [%rd4];\n"
    "    setp.gt.u64 %p2, %rd7, %rd9;\n"       /* F-10: above the fault word, exit */
    "    @%p2 bra DONE;\n"
    "    shl.b64 %rd10, %rd7, 3;\n"
    "    add.u64 %rd11, %rd1, %rd10;\n"
    "    ld.global.u64 %rd12, [%rd11];\n"
    "    add.u64 %rd11, %rd2, %rd10;\n"
    "    ld.global.u64 %rd13, [%rd11];\n"
    "    mul.lo.s64 %rd14, %rd12, %rd13;\n"
    "    mul.hi.s64 %rd15, %rd12, %rd13;\n"
    "    shr.s64 %rd16, %rd14, 63;\n"
    "    setp.ne.s64 %p3, %rd15, %rd16;\n"     /* B-1: the exact product needs more than 64 bits */
    "    @%p3 bra FAULT;\n"
    "    add.u64 %rd11, %rd3, %rd10;\n"
    "    st.global.u64 [%rd11], %rd14;\n"
    "    bra DONE;\n"
    "FAULT:\n"
    "    atom.global.min.u64 %rd17, [%rd4], %rd7;\n"
    "DONE:\n"
    "    ret;\n"
    "}\n";

/* Pairs at the I64 product boundary, one dispatch each, with the expected check outcome. */
static const struct { int64_t a, b; int overflow; } ROWS[] = {
    {0, 0, 0},
    {1, -1, 0},
    {3037000499LL, 3037000499LL, 0},
    {3037000500LL, 3037000500LL, 1},
    {-3037000499LL, 3037000499LL, 0},
    {INT64_MIN, 1, 0},
    {INT64_MIN, -1, 1},
    {-1, INT64_MIN, 1},
    {INT64_MAX, 1, 0},
    {INT64_MAX, 2, 1},
    {INT64_MAX, -1, 0},
    {4294967296LL, 2147483648LL, 1},
    {4294967296LL, -2147483648LL, 0},
    {-4294967296LL, -2147483648LL, 1},
    {123456789LL, -987654321LL, 0},
    {INT64_MIN, 0, 0},
};
#define NROWS (sizeof ROWS / sizeof ROWS[0])
#define FIRST_OVERFLOW_ROW 3u

#define NBIG 1000000u      /* work-items of the large dispatch */
#define FAULT_ITEM 500000u /* the least of the three overflowing items */
#define CHUNK 65536u       /* work-items per launch in the split dispatch */
#define SENTINEL INT64_C(0x5A5A5A5A5A5A5A5A)

static FILE *out;
static void *lib;
static CUstream stream;
static CUdeviceptr fault_word;
static int failures;

static void rec(const char *fmt, ...)
{
    va_list ap, aq;
    va_start(ap, fmt);
    va_copy(aq, ap);
    vprintf(fmt, ap);
    printf("\n");
    if (out) {
        vfprintf(out, fmt, aq);
        fputc('\n', out);
    }
    va_end(aq);
    va_end(ap);
}

static const char *ename(CUresult r)
{
    const char *s = NULL;
    if (cuGetErrorName_ && cuGetErrorName_(r, &s) == 0 && s)
        return s;
    return "unknown";
}

static int ok(const char *what, CUresult r)
{
    if (r != 0)
        rec("error %s %d %s", what, r, ename(r));
    return r == 0;
}

static const char *load_driver(void)
{
#if defined(_WIN32)
    static const char *const names[] = {"nvcuda.dll"};
#else
    static const char *const names[] = {"libcuda.so.1", "/usr/lib/wsl/lib/libcuda.so.1"};
#endif
    const char *found = NULL;
    size_t i;
    for (i = 0; i < sizeof names / sizeof names[0] && !lib; i++) {
#if defined(_WIN32)
        lib = (void *)LoadLibraryA(names[i]);
#else
        lib = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
#endif
        found = names[i];
    }
    if (!lib)
        return NULL;
    for (i = 0; i < sizeof SYMS / sizeof SYMS[0]; i++) {
#if defined(_WIN32)
        void *p = (void *)GetProcAddress((HMODULE)lib, SYMS[i].name);
#else
        void *p = dlsym(lib, SYMS[i].name);
#endif
        if (!p) {
            rec("driver.symbol_missing %s", SYMS[i].name);
            return NULL;
        }
        memcpy(SYMS[i].slot, &p, sizeof p);
    }
    return found;
}

static void ptx_text(char *buf, size_t size, const char *version, const char *target)
{
    snprintf(buf, size, ".version %s\n.target %s\n.address_size 64\n\n%s", version, target, PTX_BODY);
}

/* JIT-loads the kernel for one pair; records the result and, on failure, the log's start. */
static CUmodule load_pair(const char *version, const char *target)
{
    static char src[sizeof PTX_BODY + 128];
    char log[4096];
    int opts[2] = {CU_JIT_ERROR_LOG_BUFFER, CU_JIT_ERROR_LOG_BUFFER_SIZE_BYTES};
    void *vals[2];
    CUmodule m = NULL;
    CUresult r;
    size_t i;
    ptx_text(src, sizeof src, version, target);
    log[0] = '\0';
    vals[0] = log;
    vals[1] = (void *)(uintptr_t)sizeof log;
    r = cuModuleLoadDataEx_(&m, src, 2, opts, vals);
    log[sizeof log - 1] = '\0';
    for (i = 0; log[i]; i++)
        if (log[i] == '\r' || log[i] == '\n' || log[i] == '\t')
            log[i] = ' ';
    if (strlen(log) > 160)
        log[160] = '\0';
    if (r == 0)
        rec("jit %s %s ok", version, target);
    else
        rec("jit %s %s fail %d %s%s%s", version, target, r, ename(r), log[0] ? " log: " : "", log);
    return r == 0 ? m : NULL;
}

struct store {
    const char *kind;
    int64_t *a, *b, *o;           /* host views (staging for device_memory) */
    CUdeviceptr da, db, dout;     /* device addresses the kernel uses */
    void *raw[3];                 /* malloc blocks behind mapped_register */
    int registered[3];
    int open;
};

static int store_open(struct store *s, const char *kind)
{
    const size_t size = (size_t)NBIG * sizeof(int64_t);
    const size_t page = 4096, rsize = (size + page - 1) / page * page;
    int64_t **host[3];
    CUdeviceptr *dev[3];
    int k;
    memset(s, 0, sizeof *s);
    s->kind = kind;
    host[0] = &s->a, host[1] = &s->b, host[2] = &s->o;
    dev[0] = &s->da, dev[1] = &s->db, dev[2] = &s->dout;
    for (k = 0; k < 3; k++) {
        void *p = NULL;
        if (strcmp(kind, "mapped_alloc") == 0) {
            if (!ok("cuMemHostAlloc", cuMemHostAlloc_(&p, size, CU_MEMHOSTALLOC_DEVICEMAP)))
                return 0;
            *host[k] = p;
            if (!ok("cuMemHostGetDevicePointer", cuMemHostGetDevicePointer_(dev[k], p, 0)))
                return 0;
            if (k == 0)
                rec("store mapped_alloc device_address_equals_host_address %s",
                    *dev[k] == (CUdeviceptr)(uintptr_t)p ? "yes" : "no");
        } else if (strcmp(kind, "mapped_register") == 0) {
            if (!(s->raw[k] = malloc(rsize + page)))
                return 0;
            p = (void *)(((uintptr_t)s->raw[k] + page - 1) / page * page);
            *host[k] = p;
            if (!ok("cuMemHostRegister", cuMemHostRegister_(p, rsize, CU_MEMHOSTREGISTER_DEVICEMAP)))
                return 0;
            s->registered[k] = 1;
            if (!ok("cuMemHostGetDevicePointer", cuMemHostGetDevicePointer_(dev[k], p, 0)))
                return 0;
            if (k == 0)
                rec("store mapped_register device_address_equals_host_address %s",
                    *dev[k] == (CUdeviceptr)(uintptr_t)p ? "yes" : "no");
        } else {
            if (!(*host[k] = malloc(size)))
                return 0;
            if (!ok("cuMemAlloc", cuMemAlloc_(dev[k], size)))
                return 0;
        }
    }
    s->open = 1;
    return 1;
}

static void store_close(struct store *s)
{
    int k;
    int64_t *host[3];
    CUdeviceptr dev[3];
    host[0] = s->a, host[1] = s->b, host[2] = s->o;
    dev[0] = s->da, dev[1] = s->db, dev[2] = s->dout;
    for (k = 0; k < 3; k++) {
        if (strcmp(s->kind, "mapped_alloc") == 0) {
            if (host[k])
                cuMemFreeHost_(host[k]);
        } else if (strcmp(s->kind, "mapped_register") == 0) {
            if (s->registered[k])
                cuMemHostUnregister_(host[k]);
            free(s->raw[k]);
        } else {
            free(host[k]);
            if (dev[k])
                cuMemFree_(dev[k]);
        }
    }
    memset(s, 0, sizeof *s);
}

static int is_device(const struct store *s)
{
    return strcmp(s->kind, "device_memory") == 0;
}

/* Runs items [0, n) in launches of at most `chunk` items (0: one launch) with blocks of `block`
 * work-items. Sets *fault to the fault word after the last launch run, *launches to their count. */
static int dispatch(CUfunction f, const struct store *s, uint64_t n, uint64_t chunk, unsigned block,
                    uint64_t *fault, unsigned *launches)
{
    const size_t size = (size_t)n * sizeof(int64_t);
    uint64_t base, end;
    if (is_device(s)) {
        if (!ok("cuMemcpyHtoD", cuMemcpyHtoD_(s->da, s->a, size)) ||
            !ok("cuMemcpyHtoD", cuMemcpyHtoD_(s->db, s->b, size)) ||
            !ok("cuMemcpyHtoD", cuMemcpyHtoD_(s->dout, s->o, size)))
            return 0;
    }
    if (!ok("cuMemsetD32", cuMemsetD32_(fault_word, 0xFFFFFFFFu, 2)) || !ok("cuCtxSynchronize", cuCtxSynchronize_()))
        return 0;
    *fault = UINT64_MAX;
    *launches = 0;
    for (base = 0; base < n; base = end) {
        CUdeviceptr da = s->da, db = s->db, dout = s->dout, dfault = fault_word;
        void *args[6];
        unsigned grid;
        end = (chunk != 0 && n - base > chunk) ? base + chunk : n;
        if (*fault < base)
            break; /* BX11-10: no launch above a recorded fault */
        grid = (unsigned)((end - base + block - 1) / block);
        args[0] = &da, args[1] = &db, args[2] = &dout, args[3] = &dfault, args[4] = &base, args[5] = &end;
        if (!ok("cuLaunchKernel", cuLaunchKernel_(f, grid, 1, 1, block, 1, 1, 0, stream, args, NULL)) ||
            !ok("cuStreamSynchronize", cuStreamSynchronize_(stream)) ||
            !ok("cuMemcpyDtoH", cuMemcpyDtoH_(fault, fault_word, sizeof *fault)))
            return 0;
        ++*launches;
    }
    if (is_device(s) && !ok("cuMemcpyDtoH", cuMemcpyDtoH_(s->o, s->dout, size)))
        return 0;
    return 1;
}

static int verdict(const char *kind, const char *test, int pass, const char *detail)
{
    rec("test %s %s %s %s", kind, test, pass ? "pass" : "FAIL", detail);
    if (!pass)
        failures++;
    return pass;
}

/* Each boundary pair in a dispatch of its own, then all pairs in one dispatch. */
static int test_rows(CUfunction f, const struct store *s, const char *tag)
{
    char detail[160];
    uint64_t fault;
    unsigned launches, passed = 0;
    size_t r;
    int all = 1;
    for (r = 0; r < NROWS; r++) {
        int pass;
        s->a[0] = ROWS[r].a, s->b[0] = ROWS[r].b, s->o[0] = SENTINEL;
        if (!dispatch(f, s, 1, 0, 256, &fault, &launches))
            return verdict(s->kind, tag, 0, "driver error");
        if (ROWS[r].overflow)
            pass = fault == 0 && s->o[0] == SENTINEL;
        else
            pass = fault == UINT64_MAX && s->o[0] == ROWS[r].a * ROWS[r].b;
        if (pass)
            passed++;
        else
            rec("row %u a=%lld b=%lld fault=%llu out=%lld", (unsigned)r, (long long)ROWS[r].a,
                (long long)ROWS[r].b, (unsigned long long)fault, (long long)s->o[0]);
    }
    snprintf(detail, sizeof detail, "rows %u/%u", passed, (unsigned)NROWS);
    all &= verdict(s->kind, tag, passed == NROWS, detail);
    for (r = 0; r < NROWS; r++)
        s->a[r] = ROWS[r].a, s->b[r] = ROWS[r].b, s->o[r] = SENTINEL;
    if (!dispatch(f, s, NROWS, 0, 32, &fault, &launches))
        return verdict(s->kind, "rows_one_dispatch", 0, "driver error");
    {
        int pass = fault == FIRST_OVERFLOW_ROW && s->o[FIRST_OVERFLOW_ROW] == SENTINEL;
        for (r = 0; r < FIRST_OVERFLOW_ROW; r++)
            pass &= s->o[r] == ROWS[r].a * ROWS[r].b;
        snprintf(detail, sizeof detail, "fault=%llu expected=%u", (unsigned long long)fault, FIRST_OVERFLOW_ROW);
        all &= verdict(s->kind, "rows_one_dispatch", pass, detail);
    }
    return all;
}

/* NBIG items, a[i] = i and b[i] = 3, with overflows at items 500000, 777777, and 999999. */
static int test_large(CUfunction f, const struct store *s, uint64_t chunk, unsigned block, unsigned want_launches)
{
    char test[64], detail[200];
    uint64_t fault, i, below = 0, above = 0;
    unsigned launches;
    int pass;
    for (i = 0; i < NBIG; i++)
        s->a[i] = (int64_t)i, s->b[i] = 3, s->o[i] = SENTINEL;
    s->b[FAULT_ITEM] = s->b[777777] = s->b[999999] = INT64_MAX;
    snprintf(test, sizeof test, "large_block%u%s", block, chunk ? "_split" : "");
    if (!dispatch(f, s, NBIG, chunk, block, &fault, &launches))
        return verdict(s->kind, test, 0, "driver error");
    for (i = 0; i < FAULT_ITEM; i++)
        below += s->o[i] == (int64_t)(3 * i);
    for (i = FAULT_ITEM + 1; i < NBIG; i++)
        above += s->o[i] != SENTINEL;
    pass = fault == FAULT_ITEM && below == FAULT_ITEM && s->o[FAULT_ITEM] == SENTINEL &&
           (want_launches == 0 || launches == want_launches);
    snprintf(detail, sizeof detail, "fault=%llu expected=%u below_written=%llu/%u launches=%u above_written=%llu",
             (unsigned long long)fault, FAULT_ITEM, (unsigned long long)below, FAULT_ITEM, launches,
             (unsigned long long)above);
    return verdict(s->kind, test, pass, detail);
}

static const char *run_store(const char *kind, CUfunction lo, CUfunction hi)
{
    struct store s;
    int pass = 1;
    if (!store_open(&s, kind)) {
        store_close(&s);
        verdict(kind, "open", 0, "storage could not be opened");
        return "fail";
    }
    pass &= test_rows(lo, &s, "rows_lowest_pair");
    if (hi)
        pass &= test_rows(hi, &s, "rows_highest_pair");
    pass &= test_large(lo, &s, 0, 256, 1);
    pass &= test_large(lo, &s, 0, 32, 1);
    pass &= test_large(lo, &s, CHUNK, 256, (FAULT_ITEM / CHUNK) + 1);
    store_close(&s);
    return pass ? "pass" : "fail";
}

int main(int argc, char **argv)
{
    static char ptx[sizeof PTX_BODY + 128];
    const char *libname, *result[3] = {"skip", "skip", "skip"};
    static const char *const kinds[3] = {"mapped_alloc", "mapped_register", "device_memory"};
    CUmodule mods[NVER * NTGT];
    CUmodule lo_mod = NULL, hi_mod = NULL;
    CUfunction lo = NULL, hi = NULL;
    const char *lo_pair[2] = {"none", "none"}, *hi_pair[2] = {"none", "none"};
    CUcontext ctx = NULL;
    CUdevice dev = 0;
    int count = 0, version = 0, value, k;
    char name[256];
    size_t i;

    if (argc == 4 && strcmp(argv[1], "--ptx") == 0) {
        ptx_text(ptx, sizeof ptx, argv[2], argv[3]);
        fputs(ptx, stdout);
        return 0;
    }
    if (argc != 3) {
        fprintf(stderr, "usage: cint-cuda-probe <out> <label> | --ptx <version> <target>\n");
        return 2;
    }
    if (!(out = fopen(argv[1], "wb"))) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    rec("probe cint-cuda-probe 1");
    rec("label %s", argv[2]);
    if (!(libname = load_driver())) {
        rec("driver.library missing");
        rec("summary driver-missing");
        fclose(out);
        return 3;
    }
    rec("driver.library %s", libname);
    if (!ok("cuInit", cuInit_(0)) || !ok("cuDriverGetVersion", cuDriverGetVersion_(&version))) {
        rec("summary driver-init-failed");
        fclose(out);
        return 3;
    }
    rec("driver.api_version %d", version);
    if (!ok("cuDeviceGetCount", cuDeviceGetCount_(&count)) || count < 1 || !ok("cuDeviceGet", cuDeviceGet_(&dev, 0))) {
        rec("device.count %d", count);
        rec("summary no-device");
        fclose(out);
        return 3;
    }
    rec("device.count %d", count);
    if (ok("cuDeviceGetName", cuDeviceGetName_(name, (int)sizeof name, dev)))
        rec("device.0.name %s", name);
    for (i = 0; i < sizeof ATTRS / sizeof ATTRS[0]; i++)
        if (ok("cuDeviceGetAttribute", cuDeviceGetAttribute_(&value, ATTRS[i].id, dev)))
            rec("device.0.%s %d", ATTRS[i].name, value);
    value = cuDevicePrimaryCtxSetFlags_(dev, CU_CTX_MAP_HOST);
    rec("context.primary_set_flags_map_host %d %s", value, value ? ename(value) : "CUDA_SUCCESS");
    if (!ok("cuDevicePrimaryCtxRetain", cuDevicePrimaryCtxRetain_(&ctx, dev)) ||
        !ok("cuCtxSetCurrent", cuCtxSetCurrent_(ctx)) ||
        !ok("cuStreamCreate", cuStreamCreate_(&stream, 0)) ||
        !ok("cuMemAlloc", cuMemAlloc_(&fault_word, sizeof(uint64_t)))) {
        rec("summary context-failed");
        fclose(out);
        return 3;
    }

    for (i = 0; i < NVER * NTGT; i++) {
        const char *v = PTX_VERSIONS[i / NTGT], *t = PTX_TARGETS[i % NTGT];
        mods[i] = load_pair(v, t);
        if (mods[i] && !lo_mod)
            lo_mod = mods[i], lo_pair[0] = v, lo_pair[1] = t;
        if (mods[i])
            hi_mod = mods[i], hi_pair[0] = v, hi_pair[1] = t;
    }
    rec("jit.lowest %s %s", lo_pair[0], lo_pair[1]);
    rec("jit.highest %s %s", hi_pair[0], hi_pair[1]);
    if (lo_mod && !ok("cuModuleGetFunction", cuModuleGetFunction_(&lo, lo_mod, "probe_mul")))
        lo = NULL;
    if (lo && hi_mod != lo_mod && !ok("cuModuleGetFunction", cuModuleGetFunction_(&hi, hi_mod, "probe_mul")))
        hi = NULL;
    if (lo) {
        for (k = 0; k < 3; k++)
            result[k] = run_store(kinds[k], lo, hi);
    } else {
        failures++;
    }
    rec("summary mapped_alloc=%s mapped_register=%s device_memory=%s jit_lowest=%s/%s failures=%d", result[0],
        result[1], result[2], lo_pair[0], lo_pair[1], failures);

    for (i = 0; i < NVER * NTGT; i++)
        if (mods[i])
            cuModuleUnload_(mods[i]);
    cuMemFree_(fault_word);
    cuStreamDestroy_(stream);
    cuDevicePrimaryCtxRelease_(dev);
    fclose(out);
    return failures ? 1 : 0;
}
