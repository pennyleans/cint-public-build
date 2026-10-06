/* cint_bridge.h: the compiler's host bridge (cint-rt-3).
 *
 * Status: names and layouts Proposed (SPEC-09 CINTC-05, CINTC-06, CINTC-09,
 * CINTC-14, CINTC-15; slice 2 decision patch D-3, D-13). Open items are in
 * rt/OPEN.md (RT-OQ-15 to RT-OQ-19, RT-OQ-29).
 *
 * Owning clauses:
 *   SPEC-09 CINTC-04, CINTC-05  the four compiler calls and their order
 *   SPEC-09 CINTC-06            committing the output set (MANIFEST.ref)
 *   SPEC-09 CINTC-09            the compiler's phase budget
 *   SPEC-09 CINTC-14            the prototypes below
 *   SPEC-09 CINTC-15            inputs by handle, cint_commit_output
 *
 * The four calls are adapters over the cint-abi-1 wrappers of compiler/main.ci
 * (cx_4_main_4_plan, cx_4_main_7_compile, cx_4_main_7_measure,
 * cx_4_main_4_emit), so a program that links rt/cint_build.c (the four calls,
 * the output set and cint_build) links a compiler: the product compiler, or the
 * stub of rt/tests/stub/main.ci. rt/cint_bridge.c (the reader and
 * cint_commit_output) refers to no compiler, and the seed links it alone.
 *
 * The bridge is single-threaded: the input registry is process state, and no
 * function here may be called from two threads at once.
 */
#ifndef CINT_BRIDGE_H
#define CINT_BRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cint_rt.h"

/* The view types (cint_buffer_id, cint_type, cint_view) are declared once, in
 * cint_rt.h (SPEC-03 5.3; slice 2 decision patch D-3). */
#if !defined(CINT_RT_HAVE_VIEW)
#error "cint_bridge.h needs the view types of cint-rt-2 or later (rt/cint_rt.h, CINT_RT_HAVE_VIEW)"
#endif

/* The hosts also build against the cint-rt-2 header of the staged bootstrap's
 * pin (tools/cint_stage.py, the floor check), where the byte form of the
 * registry has its cint-rt-2 name (rt/OPEN.md RT-OQ-39). */
#if CINT_ABI_VERSION < 0x00030000u
#define cint_buffer_register_bytes cint_buffer_register
#endif

/* ------------------------------------------------------------------------- */
/* The four compiler calls (CINTC-05, CINTC-14).                              */

/* compile, measure and emit take exactly CINT_BRIDGE_TABLES tables, one view
 * argument each (OQ-157): C cannot call a function whose arity it learns at
 * run time. plan writes that many manifest rows; a compiler with fewer tables
 * plans the rest with capacity 0. */
#define CINT_BRIDGE_TABLES 17         /* 16 + the declaration table (CINTC-16) */
#define CINT_BRIDGE_DIAG_ROWS 101        /* the count row and 100 retained (CINTC-02) */
#define CINT_BRIDGE_FAULT_BYTES 237200   /* 100 records of at most 2,372 bytes (CINTC-14) */
/* Output files per module: output 0 is <P>.c, output 1 <P>.sites, for the
 * module path P without .ci; the program-level outputs (module_index equal to
 * the module count) are cint-program.c and cint-program.sites. An output of 0
 * bytes is not written. */
#define CINT_BRIDGE_OUTPUTS 2

typedef struct cint_table_plan {  /* one manifest row of plan's output, in manifest order */
    int64_t table;                /* table index: the row's own index */
    int64_t elem_bytes;           /* size of one element in bytes */
    int64_t elem_align;           /* 1, 2, 4, or 8: CINT_ELEM_ALIGN(elem_bytes) */
    int64_t capacity;             /* elements, from the CINTC-02 formula */
    int64_t scope;                /* 0: program-level; 1: per module, region reused */
    int64_t reserved;             /* 0 */
    cint_type elem_type;          /* a struct's record_id indexes the cm record layout table */
} cint_table_plan;
_Static_assert(sizeof(cint_table_plan) == 64 && offsetof(cint_table_plan, elem_type) == 48,
               "cint_table_plan");

typedef struct cint_compiler_diag {
    int64_t code;       /* SPEC-04 17.2 number (2001 for C2001); 0: none or the count row */
    int64_t module;     /* module index in build manifest order; -1: none */
    int64_t line;       /* 1-based; 0: no position */
    int64_t column;     /* 1-based, Unicode scalar values (DIAG-01); 0: none */
    int64_t detail[4];  /* code-specific; the count row as CINTC-14 */
} cint_compiler_diag;
_Static_assert(sizeof(cint_compiler_diag) == 64 && offsetof(cint_compiler_diag, detail) == 32,
               "cint_compiler_diag");

/* In compiler/main.ci, records 0 and 1 of the cm record table are the manifest
 * row (64 bytes, 7 fields) and the diagnostic row (64 bytes, 5 fields): the
 * bridge binds `plan` and `diag` with those record ids. Each call returns the
 * wrapper's status and, on CINT_OK, *result (0: completed; 1: a diagnostic is
 * retained). CINT_REFUSED when `tables` is NULL or table_count is not
 * CINT_BRIDGE_TABLES. */
cint_status cint_plan(cint_ctx *ctx, int64_t fuel,
                      cint_view module_bytes,     /* in I64[m]: bytes per module, manifest order */
                      cint_view plan,             /* inout cint_table_plan[t]: the table manifest */
                      cint_view diag,             /* inout cint_compiler_diag[101] */
                      int64_t *result);
cint_status cint_compile(cint_ctx *ctx, int64_t fuel, int64_t module_index,
                         cint_view source,        /* in U8[n]: the module's bytes */
                         cint_view path,          /* in U8[p]: module-relative path (CINTC-12) */
                         const cint_view *tables, int64_t table_count,  /* manifest order */
                         cint_view diag, cint_view faults, int64_t *result);
cint_status cint_measure(cint_ctx *ctx, int64_t fuel, int64_t module_index,
                         const cint_view *tables, int64_t table_count,
                         cint_view output_bytes,  /* inout I64[k]: bytes of each output file */
                         cint_view diag, cint_view faults, int64_t *result);
cint_status cint_emit(cint_ctx *ctx, int64_t fuel, int64_t module_index, int64_t output_index,
                      const cint_view *tables, int64_t table_count,
                      cint_view output,           /* inout U8[b]: exactly the measured bytes */
                      cint_view diag, cint_view faults, int64_t *result);

/* ------------------------------------------------------------------------- */
/* Inputs by handle (CINTC-15 "Inputs"; D-3).                                 */

/* Opens the project root `dir` (UTF-8) and keeps its handle. False when it is
 * not a directory or cannot be opened. */
typedef struct cint_bridge_root cint_bridge_root;  /* opaque */
bool cint_bridge_root_open(const char *dir, cint_bridge_root **out);
void cint_bridge_root_close(cint_bridge_root *root);
/* Opens `rel` under `root` without following a link, junction, or reparse
 * point at any segment, refuses anything but a regular file, records the file
 * identity from the open handle before the first byte is read (device and
 * inode on POSIX; volume serial number and the 128-bit identifier of
 * FILE_ID_INFO on Windows), and reads the whole file, at most `limit` bytes,
 * into a new buffer that cint_bridge_free_input releases. `rel` is `/`-separated
 * segments of [A-Za-z0-9_.-], none empty, `.`, `..`, or a device name, none
 * ending in `.` (CINTC-12). On Windows the file's final path must be the root's
 * final path followed by `rel`, byte for byte, so a junction at any segment and
 * a name that differs from the directory entry only in case are refused; on
 * POSIX each segment must be a directory entry spelled byte for byte, so a
 * file system that folds case refuses such a name too. False
 * on any refusal or error, with *bytes unchanged; an identity recorded before
 * a read that then failed stays recorded (RT-OQ-29). */
#define CINT_BRIDGE_MAX_INPUTS 4096
bool cint_bridge_read_input(cint_bridge_root *root, const char *rel, size_t limit,
                            uint8_t **bytes, size_t *len);
void cint_bridge_free_input(uint8_t *bytes);
/* Forgets every recorded input identity (the start of a build). */
void cint_bridge_clear_inputs(void);

/* ------------------------------------------------------------------------- */
/* The output set (CINTC-06).                                                 */

/* cint_stage_begin creates the sibling directory `<out>.stage-<N>`
 * exclusively, N the first unused decimal integer below
 * CINT_BRIDGE_STAGE_TRIES. cint_stage_write writes one file `rel` (CINTC-12
 * segments; intermediate directories are created) exclusively into it and
 * flushes it. cint_stage_commit writes `MANIFEST` into the stage directory
 * (one line `<sha256> <bytes> <rel>` per file, in byte order of rel, after the
 * line `cint-manifest-1`), flushes it, creates `<out>` if needed, and commits
 * `<out>/MANIFEST.ref` (`<stage directory name> <sha256 of MANIFEST>`) with
 * cint_commit_output. After that commit it removes, best effort (CINTC-06
 * step 4), the stage the replaced MANIFEST.ref named: a sibling
 * `<out name>.stage-<N>` other than the new stage whose MANIFEST has the named
 * digest, and only the files it lists, their directories, and the stage, so
 * consecutive builds leave one stage. cint_stage_abort removes what the stage
 * wrote. Commit and abort free the stage; a failed write or commit aborts it,
 * so the old MANIFEST.ref and the old set stay byte for byte as they were.
 * The output set and cint_build are in rt/cint_build.c. */
typedef struct cint_stage cint_stage;  /* opaque */
bool cint_stage_begin(const char *out, cint_stage **stage);
bool cint_stage_write(cint_stage *stage, const char *rel, const uint8_t *bytes, size_t len);
bool cint_stage_commit(cint_stage *stage);
void cint_stage_abort(cint_stage *stage);

/* Replaces the file at `path` (UTF-8) with `len` bytes, atomically.
 *
 * Refuses, returning false and leaving the file at `path` unchanged, when:
 *   - `path` is NULL or empty, ends in a separator, or is not valid UTF-8
 *     (RT-OQ-17), or `bytes` is NULL while `len` is not 0;
 *   - `len` exceeds `limit`;
 *   - `path` names a symbolic link, junction, or other reparse point, or any
 *     object other than a regular file;
 *   - `path` is the same file as a recorded input, by file identity, so a
 *     hard link or any other name for the input is refused too.
 * Otherwise it creates the sibling staging file `<path>.stage-<N>` exclusively
 * (N the first unused decimal integer below CINT_BRIDGE_STAGE_TRIES), writes
 * `bytes`, flushes it to stable storage, closes it, and replaces `path` with
 * it: rename(2) on POSIX, then a best-effort flush of the directory;
 * MoveFileExW with MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH on
 * Windows. A failure at any step removes the staging file and returns false;
 * the file at `path` is then unchanged. */
#define CINT_BRIDGE_STAGE_TRIES 10000u
bool cint_commit_output(const char *path, const uint8_t *bytes, size_t len, size_t limit);

/* ------------------------------------------------------------------------- */
/* A build (CINTC-05 order, D-13).                                            */

/* The phase budget of CINTC-09: K times the phase's input bytes plus C, in
 * fuel units; frozen at 4 times the plan task 2.17 measurement (raw k 5,381,
 * c 4,969; results/cint/t2/measure-*.json) by decision 2026-10-05 on OQ-175.
 * A phase's input is every module for plan and the program-level outputs, and
 * the module itself for its compile, measure, and emit. */
#define CINT_PHASE_FUEL_K ((int64_t)21524)
#define CINT_PHASE_FUEL_C ((int64_t)19876)

#define CINT_BUILD_OK 0        /* the set is committed */
#define CINT_BUILD_DIAG 1      /* diag holds the compiler's diagnostics, or a C9001 phase limit */
#define CINT_BUILD_FAULT 2     /* a fault inside the compiler: an internal error (CINTC-10); ctx holds it */
#define CINT_BUILD_REFUSED 3   /* a call was refused, or the manifest is invalid */
#define CINT_BUILD_IO 4        /* an input, a table allocation, or the output set failed */

/* Reads the `count` modules `rels` under `root` in build manifest order, then
 * calls plan, and for each module in the given order compile, measure, and
 * emit, writing its outputs into a new stage of `out` before the next module
 * reuses the per-module tables; after the last module, the program-level
 * outputs; then commits the set. The caller gives the modules in dependency
 * order. `diag` has CINT_BRIDGE_DIAG_ROWS rows and `faults`
 * CINT_BRIDGE_FAULT_BYTES bytes. A phase that exhausts its budget (E_FUEL) is
 * CINT_BUILD_DIAG with one C9001 row: module (-1 for plan and the program-level
 * outputs), detail[0] the phase (1 plan, 2 compile, 3 measure, 4 emit),
 * detail[1] the budget; its fault is cleared. Any result but CINT_BUILD_OK
 * leaves MANIFEST.ref and the old set as they were. A module path that breaks CINTC-12
 * (an identifier per segment, no Windows device name, no two paths equal under ASCII case
 * folding) is CINT_BUILD_DIAG with one C3030 row at 1:1 of that module, before any read. */
int cint_build(cint_ctx *ctx, cint_bridge_root *root, const char *const *rels, int64_t count,
               const char *out, cint_compiler_diag *diag, uint8_t *faults);

/* C3030 detail[0] identifies the broken CINTC-12 rule, for listed and imported paths. */
enum { CINT_PATH_IDENTIFIER = 1, CINT_PATH_DEVICE = 2, CINT_PATH_COLLISION = 3 };

/* A compiler C3030 can name an absent import target. Its module is the importer and
 * detail[2..3] hold the candidate's offset and length in compiler table 1. The legacy
 * entry above has no filename channel. This entry calls path_sink before releasing
 * that table, once for each such retained row. Listed-input C3030 rows use their
 * module path and do not call the sink. The bytes are borrowed for the call only;
 * the sink must copy any bytes it retains and must not reenter the bridge or ctx.
 * Return true on success, false for a reporting failure (CINT_BUILD_IO). Malformed
 * path ranges give CINT_BUILD_REFUSED. Neither failure publishes the output set.
 * A null sink is allowed and has the same behavior as the legacy entry. */
typedef bool (*cint_diagnostic_path_sink)(void *user, int64_t row, const uint8_t *bytes, size_t len);
int cint_build_with_paths(cint_ctx *ctx, cint_bridge_root *root, const char *const *rels, int64_t count,
                          const char *out, cint_compiler_diag *diag, uint8_t *faults,
                          cint_diagnostic_path_sink path_sink, void *user);

#ifdef CINT_BRIDGE_MEASURE_HOOKS
/* Measurement builds only (tools/cint_measure.py). cint_bridge_observe_phase runs after
 * each plan, compile, measure, and emit call, before its fault is translated; result is
 * the call's result when status is CINT_OK. cint_bridge_observe_table runs once for each
 * allocated manifest row before the build frees it, with its payload as the compiler
 * left it. Both observe only; neither may change the context or the build's inputs. */
void cint_bridge_observe_phase(cint_ctx *ctx, cint_status status, int64_t result, int64_t phase, int64_t module,
                               int64_t budget);
void cint_bridge_observe_table(const cint_table_plan *row, const void *payload);
#endif

#ifdef CINT_BRIDGE_TEST_HOOKS
/* Test builds only. cint_bridge_test_fail_step: cint_commit_output and the
 * stage's file writes fail at that step (1 create, 2 write, 3 flush, 4 close,
 * 5 replace) as if the host call had failed. cint_bridge_test_fail_write: the
 * k-th cint_stage_write fails (an injected emit failure). cint_bridge_test_kill_after:
 * the process exits with status 70 right after the k-th file is written.
 * cint_bridge_test_after_read: called once all inputs of cint_build are read.
 * cint_bridge_test_stage_tries: when not 0, replaces CINT_BRIDGE_STAGE_TRIES
 * for the stage directories of cint_stage_begin. cint_bridge_test_phase_calls:
 * entry p (1 plan, 2 compile, 3 measure, 4 emit) counts the calls of that phase
 * cint_build has made (the call log of decision 24). */
extern int cint_bridge_test_fail_step;
extern int cint_bridge_test_fail_write;
extern int cint_bridge_test_kill_after;
extern void (*cint_bridge_test_after_read)(void);
extern unsigned cint_bridge_test_stage_tries;
extern int64_t cint_bridge_test_phase_calls[5];
#endif

#endif /* CINT_BRIDGE_H */
