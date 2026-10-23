/* wb.h: the workbench library (roadmap box 14; SPEC-06 sections 5 to 13).
 *
 * One C17 library, linked by an executable for each spelling of SPEC-06 3.1: cint-workbench,
 * cint-repl, cint-explain, and cint-debug (box 14 rulings BX14-02 B and BX14-03 A). It holds
 * no arithmetic of the program (SPEC-09 ARCH-01): values come from the engine, and what the
 * library computes, such as an exact decimal, is display only, exact, never narrowed, and
 * never written to program state.
 *
 * Unit 1 of the box 14 build plan (docs/design/notes/2026-10-05-box14-workbench-debugging.md,
 * section 10) holds text buffers, the multiword integers of display, the fault record decoder
 * and its report, the scalar formats of the inspector, `explain` over box 13's SIR text reader
 * (unit 1b), and the command lines. Unit 2a adds B1's builds and the session on box 13's
 * interpreter for scalar programs, and unit 2b the object store, session checkpoints, watches,
 * and saved sessions on box 13's checkpoints.
 */
#ifndef WB_H
#define WB_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "cint_rt.h"

/* ------------------------------------------------------------------------- */
/* Text (wb_text.c).                                                          */

/* A growing text buffer. One that failed to grow keeps its text and sets `failed`. */
typedef struct wb_buf {
    char *p;
    size_t n, cap;
    int failed;
} wb_buf;

void wb_put(wb_buf *b, const void *s, size_t n);
/* A cint_sink write that appends to the wb_buf `user`. */
cint_status wb_sink(void *user, const uint8_t *bytes, size_t len);
void wb_s(wb_buf *b, const char *s);
void wb_f(wb_buf *b, const char *fmt, ...);
void wb_hex(wb_buf *b, const uint8_t *p, size_t n);
/* A canonical JSON string (SPEC-09 RCPT-02, SPEC-04 LS-289): ASCII only, \u escapes in
 * lowercase hexadecimal, and U+FFFD for bytes that are not UTF-8. */
void wb_jstr(wb_buf *b, const char *s, size_t n);
void wb_free(wb_buf *b);
/* A monotonic clock in nanoseconds, for timings, which stay outside state and identities; 0 without one. */
long long wb_now_ns(void);
/* The nanoseconds from `from` to `to`, two readings of wb_now_ns; 0 when `to` is not later. */
long long wb_elapsed_ns(long long from, long long to);
/* 1 when the n bytes at s are UTF-8 (no surrogates, no overlong forms). */
int wb_utf8(const char *s, size_t n);

/* ------------------------------------------------------------------------- */
/* Builds (wb_build.c): files, a program's modules, and B1's `cint emit-c`.    */

#define WB_MODULES_MAX 1024u

/* a, the path separator, and b; malloc'd, or NULL. */
char *wb_join(const char *a, const char *b);
char *wb_copy(const char *s);
/* A whole file, malloc'd with a NUL after it, or NULL. */
char *wb_read_file(const char *path, size_t *len);
/* Writes n bytes as the whole file. Returns 0, or -1. */
int wb_write_file(const char *path, const void *p, size_t n);

/* The modules of the program of root/rel, malloc'd paths in the order of slice 2 decision D-13
 * (each after the modules it imports, ties in byte order of path, rel last). Returns 0, or -1
 * with the reason in err (err empty when memory ran out). */
typedef struct wb_modlist {
    char *path[WB_MODULES_MAX];
    uint32_t n;
} wb_modlist;
int wb_modules(const char *root, const char *rel, wb_modlist *out, wb_buf *err);
void wb_modules_free(wb_modlist *ml);
/* Runs `cint [--json] emit-c --root ROOT --out OUT` on the modules of ROOT/REL, with its
 * standard error in the file LOG. Returns 0 with *status its exit status (-1 when it ended
 * without one), 1 when it could not be started, or 2 when the root module cannot be read; err
 * says why. */
int wb_emit(const char *cint, int json, const char *root, const char *rel, const char *out, const char *log,
            int *status, wb_buf *err);
/* The program directory of an output set: the stage its MANIFEST.ref names beside it, or the
 * directory itself when it has no MANIFEST.ref (SPEC-09 CINTC-06). malloc'd, or NULL. */
char *wb_stage_of(const char *dir);
/* The source bytes of each module of p under root, -1 for one that cannot be read. */
struct cint_sir_program;
void wb_source_sizes(const struct cint_sir_program *p, const char *root, int64_t *bytes);

/* ------------------------------------------------------------------------- */
/* Exact integers of display (wb_num.c; BX14-02 B, BX14-11).                  */

/* Sign and magnitude, in little-endian 32-bit limbs. 136 limbs (4,352 bits) hold the widest
 * tagged value of a record, a 528-byte compile-time `exact` (SPEC-01 IM-108), and every sum
 * that /stats shows: at most w + 63 bits for elements of w bits (SPEC-01 IM-80). */
#define WB_NUM_LIMBS 136u

typedef struct wb_num {
    int neg;                        /* 1 for a negative value; a zero is never negative */
    uint32_t n;                     /* limbs in use; the value is zero when n is 0 */
    uint32_t limb[WB_NUM_LIMBS];
} wb_num;

void wb_num_zero(wb_num *x);
void wb_num_from_u64(wb_num *x, uint64_t v);
void wb_num_from_i64(wb_num *x, int64_t v);
/* n bytes of two's complement (is_signed) or of an unsigned value, least significant first.
 * Returns -1 when the value needs more than WB_NUM_LIMBS limbs. */
int wb_num_from_le(wb_num *x, const uint8_t *p, size_t n, int is_signed);
/* x += y, and x *= m. Each returns -1, leaving x unspecified, when the result needs more
 * than WB_NUM_LIMBS limbs. */
int wb_num_add(wb_num *x, const wb_num *y);
int wb_num_mul_small(wb_num *x, uint32_t m);
int wb_num_cmp(const wb_num *x, const wb_num *y);
/* The bit length of the magnitude: 0 for zero. */
uint32_t wb_num_bits(const wb_num *x);
/* The exact decimal; with group, `_` between groups of three digits (SPEC-06 6.1, /group). */
void wb_num_dec(wb_buf *b, const wb_num *x, int group);
/* The exact decimal of x / d, for d = 2^a * 5^b >= 1, which always terminates: trailing zeros
 * removed with at least one fractional digit kept (SPEC-06 6.1, rule 2), and no fractional
 * part when d is 1. Returns -1, writing nothing, when d has another prime factor. */
int wb_num_dec_over(wb_buf *b, const wb_num *x, uint64_t d, int group);

/* ------------------------------------------------------------------------- */
/* Fault records and reports (wb_fault.c; SPEC-01 IM-106 and IM-149, SPEC-06 8). */

/* A tagged value (SPEC-01 IM-149): the tag byte, then the little-endian two's-complement bytes,
 * with a U32 length before them for `Z`. A compile-time record allows a 513-byte operand and a
 * 528-byte `exact` (IM-108; 520 before OQ-292 admitted `Q1.127`). */
typedef struct wb_tval {
    uint16_t len;
    uint8_t bytes[533];
} wb_tval;

typedef struct wb_pos {
    const char *path;   /* into the record's bytes, not NUL-terminated */
    uint32_t path_len, line, column;
} wb_pos;

/* A decoded `cint-core-1/fault/v2` or `/v3` record. Paths and the kernel name point into the bytes it
 * was decoded from, which must outlive it. */
typedef struct wb_fault {
    unsigned code;                  /* 1 to 13, SPEC-01 IM-104 in rt/cint_rt.h order */
    char op[CINT_FAULT_MAX_OPERATION + 1u];
    uint32_t nop, nstack;
    wb_tval opnd[CINT_FAULT_MAX_OPERANDS], exact, limit;
    int has_exact, has_limit, has_rev, has_map, has_addr, has_item;
    uint8_t rev[32], map[32];
    const char *kernel;
    uint32_t kernel_len, phase;
    int64_t dispatch, item, step;
    wb_pos pos, stack[CINT_RT_MAX_DEPTH];
} wb_fault;

/* The record kind (SPEC-01 IM-148): the bytes do not show it, so the reader passes it. */
enum { WB_RECORD_RUN = 0, WB_RECORD_COMPILE = 1 };

extern const char *const WB_CODE_NAMES[14];

/* Decodes exactly n bytes. Returns 0, or -1 when they are not a valid v2 record of that kind. */
int wb_fault_decode(const uint8_t *data, size_t n, wb_fault *f, int kind);
/* The text of a tagged value, "I64 -5" or with_type 0 "-5", NUL-terminated; its length. */
size_t wb_tval_text(const wb_tval *v, char *text, size_t cap, int with_type);
/* Its JSON, a typed exact decimal {"t":"I64","v":"-5"} (SPEC-04 LS-289), or null when it has none. */
void wb_tval_json(wb_buf *b, const wb_tval *v);

/* What a report adds to the record (SPEC-06 8.1). NULL, or a negative fuel, leaves a line out. */
typedef struct wb_report {
    int64_t fuel;              /* fuel consumed by the entry (not part of the record) */
    const char *state;         /* what the fault left: the post-fault state of SPEC-01 IM-121 */
    const char *site;          /* the `explain` site, `name#n` */
    const char *position;      /* the workbench position: branch, entry ordinal, call ordinal */
    const char *backend;       /* the backend and how it found the canonical fault (SPEC-06 8.3) */
    uint32_t rank;             /* the faulting dispatch's domain, for the work-item's index tuple; */
    int64_t extents[4];        /* rank 0 when unknown (cint_sir_dispatch_domain) */
} wb_report;

/* The source bytes of a module path, for the excerpts; NULL when the module is not loaded. */
typedef const uint8_t *(*wb_source_fn)(void *user, const char *path, size_t path_len, size_t *len);

/* The source excerpt of SPEC-04 LS-276 at p, with the label after the caret. */
void wb_excerpt(wb_buf *b, const wb_pos *p, const char *label, wb_source_fn source, void *user);

/* The human form: the field lines of `cint run` (decision 26; box 14 ruling BX14-05 A), the
 * `fuel-consumed` and `state` lines, then the lines that are not canonical (BX14-13). */
void wb_fault_text(wb_buf *b, const wb_fault *f, const wb_report *r, wb_source_fn source, void *user);
/* The CINT-FAULT-1 object (SPEC-06 8.4, 15), with the additions under `noncanonical`; no newline. */
void wb_fault_json(wb_buf *b, const wb_fault *f, const wb_report *r);

/* ------------------------------------------------------------------------- */
/* The inspector (wb_inspect.c; SPEC-06 6).                                   */

enum {
    WB_FMT_RAW = 1u, WB_FMT_HEX = 2u, WB_FMT_BIN = 4u, WB_FMT_BYTES = 8u,
    WB_FMT_ALL = WB_FMT_RAW | WB_FMT_HEX | WB_FMT_BIN | WB_FMT_BYTES
};

typedef struct wb_formats {
    unsigned show;     /* WB_FMT_* beside the exact value */
    int group;         /* /group: `_` separators in decimals */
    uint64_t scale;    /* /scale n, or 0 */
} wb_formats;

/* Parses the format words of `:inspect` ("/hex", "/scale 100", ...). Returns 0, or -1 with the
 * reason in err when a word is unknown, waits for a later box, or is malformed. */
int wb_formats_parse(int argc, const char *const *argv, wb_formats *fmt, wb_buf *err);
/* The value line of an entry: "answer = 42 : I64", or "= 42 : I64" with no name. */
int wb_value_line(wb_buf *b, const char *name, const wb_tval *v);
/* `:inspect` of a scalar binding. Returns 0, or -1 with the reason in err. */
int wb_inspect_scalar(wb_buf *b, const char *name, const wb_tval *v, const wb_formats *fmt, wb_buf *err);

/* A field of a struct value, in declaration order. */
typedef struct wb_field {
    const char *name;
    wb_tval value;
} wb_field;

/* `:inspect` of a struct binding: its type, then each field in the formats asked for. */
int wb_inspect_struct(wb_buf *b, const char *name, const char *type_name, const wb_field *fields,
                      size_t count, const wb_formats *fmt, wb_buf *err);

/* ------------------------------------------------------------------------- */
/* explain (wb_explain.c; SPEC-06 7; box 14 default BX14-12).                  */

struct cint_sir_program;

/* `cint explain` of module `module` of a program box 13's reader loaded: every function,
 * script, and test of the module with the build target's budgets when `name` is NULL, or
 * function `name`, as text or as one CINT-EXPLAIN-1 line. `bytes` holds each module's source
 * size in program order, -1 for one not read, or is NULL. Returns 0, or -1 with the reason in
 * err (no such module or function), or with err empty when memory ran out. */
int wb_explain(wb_buf *b, const struct cint_sir_program *p, const char *module, const char *name,
               const int64_t *bytes, int json, wb_buf *err);

/* The signature of function `name` of module `module` as a session prints it,
 * "(I64, in I64[3]) -> I64". Returns 0, or -1 when the module has no such function. */
int wb_signature(wb_buf *b, const struct cint_sir_program *p, const char *module, const char *name);
/* The SPEC-03 H-1 class of function `name` of module `module`: 1 for `observe`, 0 for `pure`,
 * -1 when there is no such function or memory ran out. */
int wb_effect(const struct cint_sir_program *p, const char *module, const char *name);
/* The `explain` identifier `name#n` of site `at` into out (SPEC-06 8.1; BX14-13), "" when p has
 * no such site. */
void wb_site_id(const struct cint_sir_program *p, cint_site at, char *out, size_t cap);

/* ------------------------------------------------------------------------- */
/* The object store and session checkpoints (wb_store.c; SPEC-06 5.4, 5.6; unit 2b). */

#define WB_PATH_MAX 255u   /* a module path of a revision set */
#define WB_NAME_MAX 64u    /* a checkpoint or branch name */

/* A blob of the store, named by the SHA-256 of its bytes. */
typedef struct wb_obj {
    uint8_t id[32];
    uint8_t *p;
    size_t n;
} wb_obj;

typedef struct wb_store {
    wb_obj *obj;
    uint32_t n, cap;
    uint32_t *slot, nslot;   /* open addressing over the first bytes of the names */
} wb_store;

/* Adds a copy of n bytes unless the store holds them, and writes their name to id (when not
 * NULL). Returns 0, or -1 when memory runs out. */
int wb_store_put(wb_store *st, const void *p, size_t n, uint8_t id[32]);
const wb_obj *wb_store_get(const wb_store *st, const uint8_t id[32]);
void wb_store_free(wb_store *st);

/* 1 when s is a checkpoint or branch name of at most max bytes (letters, digits, `_`, `-`,
 * `.`), or a module path that a revision set may hold (relative, `/` between segments, `.ci`). */
int wb_name_ok(const char *s, size_t max);
int wb_path_ok(const char *s);

typedef struct wb_rmod {
    char path[WB_PATH_MAX + 1u];
    uint8_t text[32];
} wb_rmod;

typedef struct wb_rdecl {
    char name[256];
    int kind;
    uint32_t entry, rev, line, nlines;
    uint8_t text[32];
} wb_rdecl;

typedef struct wb_rloaded {
    char path[WB_PATH_MAX + 1u];
    uint32_t rev;
} wb_rloaded;

/* A session checkpoint (layout in wb_store.c). The modules end with session.ci. */
typedef struct wb_record {
    uint8_t revision[32];
    uint32_t nmod;
    wb_rmod *mod;
    int has_state;
    uint8_t state[32];            /* the CINT-CKPT-1 object */
    int64_t effects, fuel, depth;
    char backend[32];
    int has_parent;
    uint8_t parent[32];
    uint64_t ordinal;
    uint32_t rev, ndecl, nloaded;
    wb_rdecl *decl;
    wb_rloaded *loaded;
    uint32_t unit_line, unit_lines, first_line, first_col;
    char unit_path[WB_PATH_MAX + 1u];
    char name[WB_NAME_MAX + 1u];
} wb_record;

/* Returns 0, or -1 when memory runs out. */
int wb_record_encode(wb_buf *b, const wb_record *r);
/* Decodes exactly n bytes into *r, allocating its lists. Returns 0, or -1 for bytes that are not
 * a well-formed record; *r is then empty. */
int wb_record_decode(const uint8_t *p, size_t n, wb_record *r);
void wb_record_free(wb_record *r);
/* The state identity and the node identity of SPEC-06 5.4. Returns 0, or -1 when memory runs out. */
int wb_record_ids(const wb_record *r, const uint8_t *state, size_t state_len, uint8_t sid[32], uint8_t nid[32]);

/* ------------------------------------------------------------------------- */
/* The session (wb_session.c; SPEC-06 5; box 14 units 2a and 2b).             */

typedef struct wb_session_options {
    const char *cint;    /* B1's cint */
    const char *root;    /* the project root that :load reads */
    const char *out;     /* the session's own directory: its sources and builds */
    const char *load;    /* a module to load first, or NULL */
    int echo;            /* print each entry after its prompt, as a transcript shows it */
    const uint8_t *debug;    /* a recording that cint_recording_decode accepts, opened read-only
                                (cint debug, SPEC-06 10.4), or NULL */
    size_t debug_len;
    const char *debug_path;  /* its file, as the session names it */
} wb_session_options;

/* Runs a session on the lines of `in` until :quit or the end of input. Returns the exit status:
 * 0, CINT_EXIT_CHECK when the recording of a debug session does not decode, or
 * CINT_EXIT_ENVIRONMENT when the session's directory cannot be made. A debug session needs only
 * `out`, `echo`, and the recording; its commands compile nothing. */
int wb_session(const wb_session_options *o, FILE *in);

/* Global NAME of ctx's state, as `module.name` or by its last part: its value into *tv and its
 * qualified name into shown (cap bytes), and every global it names into all when given. Returns
 * the count of globals it names, or -1 when the state cannot be read. The session's :inspect of
 * a debug session and the backward scan read globals with it. */
int wb_global_named(cint_ctx *ctx, const char *name, wb_tval *tv, char *shown, size_t cap, wb_buf *all);

/* ------------------------------------------------------------------------- */
/* Command lines (wb_cmd.c).                                                  */

/* The executables' entry points: argv[0] is the executable. Each returns its exit status
 * (SPEC-06 3.2; CINT_EXIT_* in rt/cint_rt.h). */
int wb_main_workbench(int argc, char **argv);
int wb_main_repl(int argc, char **argv);
int wb_main_explain(int argc, char **argv);
int wb_main_debug(int argc, char **argv);

#endif /* WB_H */
