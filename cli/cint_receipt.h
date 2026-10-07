/* cint_receipt.h: what cli/cint_main.c and cli/cint_receipt.c share (decision 26).
 *
 * Internal to the `cint` command. cint_receipt.c writes canonical JSON strings, this tool's
 * errors, the fault, error, diagnostic and test objects, the receipts, and the retained fault of a
 * faulted compiler; cint_main.c reads the toolchain, discovers modules, decodes records, builds,
 * and runs.
 */
#ifndef CINT_RECEIPT_H
#define CINT_RECEIPT_H

#include <stddef.h>
#include <stdint.h>

#include "cint_bridge.h"

#if defined(_WIN32)
#define SEP "\\"
#else
#define SEP "/"
#endif

#ifndef CINT_COMPILER_SOURCE_IDENTITY
#define CINT_COMPILER_SOURCE_IDENTITY ""
#endif

/* Byte buffers and canonical JSON (SPEC-09 RCPT-02, SPEC-04 LS-289). */
typedef struct sbuf {
    char *p;
    size_t n, cap;
} sbuf;

void sb_put(sbuf *b, const void *s, size_t n);
void sb_s(sbuf *b, const char *s);
void sb_f(sbuf *b, const char *fmt, ...);
void sb_hex(sbuf *b, const uint8_t *p, size_t n);
void sb_jstr(sbuf *b, const char *s, size_t n);
void sb_jz(sbuf *b, const char *s);

/* State. */
enum { MODE_RUN, MODE_BUILD, MODE_TEST };

/* A module of the build, or an import path that no file answered (src NULL; RCPT-07). */
typedef struct cli_module {
    char *rel;          /* path under the source root (CINTC-12) */
    uint8_t *src;
    size_t len;
    char hex[65];
    int *deps, ndeps;   /* the modules its imports name, in source order */
    int mark;           /* the dependency order walk: 1 entered, 2 placed */
} cli_module;

typedef struct toolchain {
    char *leg, *cc, *cc_version, *include, *runtime_object, *cache, *host, *compiler_sources, *executable_sha256;
    char *runtime_library_object;   /* the runtime compiled with CINT_RT_LIBRARY, for `build --lib` */
    char *flags[64], *rt_sources[64];
    size_t nflags, nrt;
} toolchain;

typedef struct cli_state {
    const char *receipt, *root, *toolchain, *cache, *file, *out;
    char *files[64];
    int nfiles, emit, mode, lib;   /* lib: `build --lib` (SPEC-06 3.1) */
    cli_module *mods;   /* mods[0] is the root module; the rest in discovery order */
    int nmods;
    const char **order; /* the present modules in dependency order, the root last (D-13) */
    int norder;
    struct cli_test *tests;
    int ntests;
    toolchain tc;
    cint_ctx *ctx;
    char rel[1100], dir[4096], cachedir[4096];
    uint8_t *src;
    size_t src_len;
    char source_revision[65];
    int source_dirty;
    const char *git_limit;
} cli_state;

extern cli_state g;
extern int json_mode;
extern cint_compiler_diag g_diag[CINT_BRIDGE_DIAG_ROWS];

/* Canonical fault records (SPEC-01 IM-149 v2, run-time kind). */
extern const char *const CODE_NAMES[14];

typedef struct fpos {
    const char *path;
    uint32_t path_len, line, column;
} fpos;

/* Compile records allow 513-byte operands and a 520-byte exact value (IM-108). */
typedef struct fvalue {
    uint16_t len;
    uint8_t bytes[525];
} fvalue;

typedef struct frec {
    unsigned code;
    char op[65];
    uint32_t nop, nstack;
    fvalue opnd[CINT_FAULT_MAX_OPERANDS], exact, limit;
    int has_exact, has_limit, has_rev, has_map, has_addr, has_item;
    uint8_t rev[32], map[32];
    const char *kernel;
    uint32_t kernel_len, phase;
    int64_t dispatch, item, step;
    fpos pos, stack[CINT_RT_MAX_DEPTH];
} frec;

/* An error result (SPEC-06 3.4a): the error record of rt/cint_rt.h 6d, decoded. The names are
 * names of SPEC-09 CONF-11 rule 12 and point into the record's bytes, which stay allocated. */
typedef struct cli_error {
    const char *set, *value;
    int set_len, value_len;
    const char *type;  /* the set's underlying type, "U8" to "U64" */
    uint64_t tag;
} cli_error;

/* A test of `cint test` (SPEC-04 LS-232 to LS-240, SPEC-06 3.5). */
typedef struct cli_test {
    const char *file;
    const uint8_t *spelling;  /* the name's string literal as written, quotes included */
    size_t spelling_len;
    unsigned expect;          /* the code that expect_fault names, or 0 */
    uint32_t line;            /* the line of `test` */
    int64_t at;               /* the line of `at N` (LS-240), or -1 */
    int passed;
    int64_t fuel;
    frec *fault;
    cli_error *error;         /* the error that left the test (LS-315), or NULL */
} cli_test;

/* The run. */
typedef struct run_out {
    sbuf bytes;  /* kept for the receipt's stdout digest */
    int keep, failed;
} run_out;

/* The generated files of the committed output set: MANIFEST lines `<sha256> <bytes> <rel>`. */
typedef struct outset {
    char stage[4096], manifest_hex[65];
    char **rel;
    size_t n;
} outset;

typedef struct outcome {
    int status;      /* the exit status of `cint run` */
    const char *kind;
    const char *exe;
    frec *fault;
    cli_error *error;  /* the error result of `main`, or NULL */
    run_out *out;
    outset *set;
    int64_t fuel;    /* fuel consumed, from the program's fuel record; -1 when nothing ran */
    int64_t t0, t_build, t_cc, t_run;
} outcome;

/* cint_main.c */
int read_file(const char *path, uint8_t **bytes, size_t *len);
char *join(const char *a, const char *b);
const char *basename_utf8(const char *path);
int digest(const uint8_t *data, size_t n, char hex[65]);
int digest_file(const char *path, char hex[65]);
int64_t now_ns(void);
int decode_fault(const uint8_t *data, size_t n, frec *f, int trailing);
size_t value_text(const fvalue *v, char text[1700], int with_type);
void report_diags(sbuf *receipt);
cli_module **sorted_modules(int all, int *count);
size_t unescape(const uint8_t *s, size_t n, char *out);

/* cint_receipt.c */
int tool_error(int status, const char *tool, int tool_status, const char *log, const char *fmt, ...);
void sb_jfault(sbuf *b, const frec *f, int64_t fuel);
void sb_jdiag(sbuf *o, const cint_compiler_diag *d, const frec *fault, const char *file, const char *message,
              const char *note);
void sb_jtest(sbuf *b, const cli_test *t, int line);
void sb_error(sbuf *b, const cli_error *e, int json);
int finish(outcome *oc, const char *source_hex);
int internal_compiler_fault(const char *source_hex);

#endif /* CINT_RECEIPT_H */
