/* wb_c17.h: cpu-c17 through libraries (roadmap box 14, unit 5; box 14 default BX14-08).
 *
 * The workbench runs a program on cpu-c17 by loading the library that `cint build --lib` writes
 * (SPEC-06 3.1) with dlopen, or LoadLibraryW on Windows. It reads the reflection table the
 * library exports as cint_library_desc (rt/cint_rt.h 6b''; SPEC-03 A-19, A-20) and calls each
 * export through its cx wrapper (A-12). A library carries its own copy of the runtime, so every
 * context of a library is made, used, and destroyed through that library's host functions (box
 * 10 default BX10-07), never through the workbench's runtime; only bytes cross between them.
 *
 * Checkpoints of a library's context go through the library's cint_ctx_checkpoint and
 * cint_ctx_restore (rt/cint_state.h), with the state table it exports as cint_observer_state
 * bound when the context is made. The toolchain's own library runtime is rt/cint_rt.c alone, so
 * wb_c17_build links rt/cint_state.c and rt/cint_rec.c with it, as the Python bridge does
 * (python/OPEN.md PY-OQ-03). In a library built without them, wb_c17_has_state is 0 and the
 * three state calls refuse, saying so.
 */
#ifndef WB_C17_H
#define WB_C17_H

#include <stddef.h>
#include <stdint.h>

#include "cint_rt.h"
#include "wb.h"

/* The parameters a call carries (wb_c17_call says why there is a limit). */
#define WB_C17_PARAMS_MAX 6u

typedef struct wb_c17_export {
    const char *name;                   /* into the library, not NUL-terminated */
    uint32_t name_len;
    uint32_t module;                    /* its module's index in the program */
    uint32_t kind, effect;              /* CINT_EXPORT_*; CINT_PURE, CINT_OBSERVE, CINT_UNRESTRICTED */
    uint32_t nparam;
    uint32_t param[WB_C17_PARAMS_MAX];  /* CINT_TAG_* of each parameter */
    uint32_t result;                    /* CINT_TAG_* of the result, 0 for none */
    cint_entry_fn call;                 /* the cx wrapper */
    char why[80];                       /* empty when the workbench can call it, else why not */
} wb_c17_export;

typedef struct wb_c17 wb_c17;

/* Runs `cint [--json] build --lib --toolchain T --root ROOT --out OUT REL` with its standard error
 * in the file LOG; REL is the root module's path under ROOT. T is the toolchain file beside cint
 * with a library runtime that also carries rt/cint_state.c and rt/cint_rec.c, made once per
 * toolchain under its cache. Returns the path of the library it writes (malloc'd) with *status 0,
 * NULL with *status its exit status when the build failed, or NULL with *status -1 when the
 * runtime could not be made or cint could not be started; err says why. */
char *wb_c17_build(const char *cint, int json, const char *root, const char *rel, const char *out,
                   const char *log, int *status, wb_buf *err);

/* The configuration of the libraries cint builds, from the toolchain file beside it, for a
 * receipt's observations (SPEC-06 12.2): {"cc_version", "flags", "leg"}, or null. */
void wb_c17_config(const char *cint, wb_buf *out);

/* Loads the library at path (UTF-8) and reads its reflection table. Returns the library, or
 * NULL with the reason in err: a file that does not load, a library without cint_library_desc
 * (the seed writes none), a damaged table, or an ABI other than this workbench's. */
wb_c17 *wb_c17_open(const char *path, wb_buf *err);
void wb_c17_close(wb_c17 *lib);

/* The program as the library describes it: its modules and its revision identity. */
const cint_program *wb_c17_program(const wb_c17 *lib);
/* The exports: the root module's first, then the other modules' in program order (A-19). */
uint32_t wb_c17_count(const wb_c17 *lib);
const wb_c17_export *wb_c17_export_at(const wb_c17 *lib, uint32_t i);
/* Export `name` of the module at path `module`, or with module NULL the first export of that
 * name in wb_c17_export_at order; NULL when there is none. */
const wb_c17_export *wb_c17_find(const wb_c17 *lib, const char *module, const char *name);
/* The signature as a session prints it, "(I64, Bool) -> I64"; "(...)" when it is not callable. */
void wb_c17_signature(wb_buf *b, const wb_c17_export *e);
/* 1 when the library's runtime exports checkpoints, restores, and state hashes. */
int wb_c17_has_state(const wb_c17 *lib);

/* A context of the library's program on its root module's descriptor, with depth limit `depth`
 * (0 for the default of SPEC-06 1.4) and print output to `output` (NULL discards it). Returns
 * the context, or NULL with the reason in err. */
cint_ctx *wb_c17_context(wb_c17 *lib, int64_t depth, cint_output_fn output, void *user, wb_buf *err);
void wb_c17_destroy(wb_c17 *lib, cint_ctx *ctx);

/* Calls export e as one entry with fuel allowance `fuel` (CINT_FUEL_UNBOUNDED for none). args
 * holds each parameter's 64-bit pattern, as the session's entries take them; on CINT_OK
 * *result receives the result's pattern, a signed type's sign-extended. CINT_REFUSED, with
 * nothing run, for an export whose `why` is set.
 *
 * The cx wrapper's prototype follows the export's type signature (A-12, A-13), and C has no
 * portable call through a prototype chosen at run time. Each parameter therefore travels in a
 * 64-bit slot, sign- or zero-extended from its carrier (int8_t to uint64_t, Bool as uint8_t),
 * and the result pointer in a pointer slot. On the x86-64 System V, Windows x64, and AArch64
 * conventions every such argument goes in a register or an 8-byte stack slot, and a callee
 * reads its carrier from the low bytes. Apple's AArch64 convention packs stack arguments
 * narrower than 8 bytes; WB_C17_PARAMS_MAX keeps every parameter of a call in a register. */
cint_status wb_c17_call(wb_c17 *lib, cint_ctx *ctx, const wb_c17_export *e, int64_t fuel, const uint64_t *args,
                        uint64_t *result);

/* Fuel consumed by the last entry, or -1 when it cannot be read. */
int64_t wb_c17_fuel(wb_c17 *lib, cint_ctx *ctx);
/* Appends the canonical bytes of the context's fault record (SPEC-01 IM-149) to out. Returns 1
 * when the context holds one, 0 when it holds none, and -1 when they cannot be read. */
int wb_c17_fault(wb_c17 *lib, cint_ctx *ctx, wb_buf *out);
/* CINT_REFUSAL_* of the last refused call, or CINT_REFUSAL_NONE. */
uint32_t wb_c17_refusal(wb_c17 *lib, cint_ctx *ctx);
cint_status wb_c17_clear_fault(wb_c17 *lib, cint_ctx *ctx);

/* The context's CINT-CKPT-1 appended to out, a restore of one, and the state hash (IM-154),
 * through the library's runtime. Each returns the runtime's status, or CINT_REFUSED with the
 * reason in err when the library has no state functions. */
cint_status wb_c17_checkpoint(wb_c17 *lib, cint_ctx *ctx, wb_buf *out, wb_buf *err);
cint_status wb_c17_restore(wb_c17 *lib, cint_ctx *ctx, const uint8_t *bytes, size_t n, wb_buf *err);
cint_status wb_c17_state_hash(wb_c17 *lib, cint_ctx *ctx, uint8_t out[32], wb_buf *err);

#endif /* WB_C17_H */
