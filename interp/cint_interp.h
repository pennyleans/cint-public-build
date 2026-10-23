/* cint_interp.h: the library of cpu-sir-interp (SPEC-09 BACK-01a), spelling Proposed.
 *
 * Units 1 and 2 of roadmap box 13 (docs/design/notes/2026-10-05-box13-interpreter-checkpoints.md,
 * section 9). Unit 1 reads a program's canonical SIR text: cint_sir_load reads the program
 * file cint-program.sites in a directory and the SIR text of each module it lists
 * (output 1 of cintc, <module path without .ci>.sites), checks the forms of SPEC-09
 * SIR-09a and the SIR-10 rules the interpreter relies on (each temporary defined once and
 * before use, operand types, a site on every node that can fault), and recomputes the
 * revision identity (SIR-17). A text that fails is an input error with its position; it
 * is never run (SIR-10). The reader allocates with malloc and reads no environment.
 *
 * Unit 2 runs it: cint_sir_prepare turns a loaded program into the tables a compiled
 * program exports (its cint_program, its cint-observe-1 observer, its module state, its
 * program entry), whose functions run the program's SIR on a cint_ctx through the
 * cint_rt.h helpers, so a host drives the interpreter as it drives compiled C. */
#ifndef CINT_INTERP_H
#define CINT_INTERP_H

#include <stddef.h>
#include <stdint.h>

#include "cint_rt.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    CINT_SIR_OK = 0,
    CINT_SIR_INPUT = 1,     /* the text breaks a rule; the error names the position */
    CINT_SIR_RESOURCE = 2,  /* a file could not be read, or memory ran out */
    CINT_SIR_UNSUPPORTED = 3   /* text cintc writes in a form this unit does not run yet */
};

typedef struct cint_sir_error {
    char path[256];     /* the file, relative to the program directory */
    uint32_t line;      /* from 1; 0 when the error concerns the whole file */
    uint32_t col;       /* from 1; 0 when the error concerns the whole line */
    char text[256];     /* what rule the input breaks */
} cint_sir_error;

typedef struct cint_sir_program cint_sir_program;

/* Reads and checks the program in directory `dir`. On CINT_SIR_OK, *out is the program,
 * freed with cint_sir_free; otherwise *out is NULL and *err says why. */
int cint_sir_load(const char *dir, cint_sir_program **out, cint_sir_error *err);

void cint_sir_free(cint_sir_program *p);

/* One module of a program held in memory: its path, ending in .ci, and its SIR text. */
typedef struct cint_sir_text {
    const char *path;   /* not NUL-terminated */
    size_t path_len;
    const uint8_t *text;
    size_t len;
} cint_sir_text;

/* cint_sir_load of a program given as its modules, in program order (a recording's code,
 * box 13 unit 5); errors name the module's .sites file as cint_sir_load does. */
int cint_sir_load_text(const cint_sir_text *mods, uint32_t count, cint_sir_program **out, cint_sir_error *err);

/* Module k of a loaded program, which keeps the bytes. */
void cint_sir_text_of(const cint_sir_program *p, uint32_t k, cint_sir_text *out);

/* The revision identity of a loaded program (SPEC-01 IM-157, SPEC-09 SIR-17). */
void cint_sir_revision(const cint_sir_program *p, uint8_t out[32]);

/* Counts of a loaded program: modules, functions, and nodes. */
void cint_sir_counts(const cint_sir_program *p, uint32_t *modules, uint32_t *functions, uint32_t *nodes);

typedef struct cint_sir_image cint_sir_image;

/* Prepares loaded program p to run. On CINT_SIR_OK, *out is the image, released with
 * cint_sir_release before p is freed; otherwise *out is NULL and *err names the first
 * form refused. The image's entries run through one process-wide image, the last one
 * prepared, because cint_observer_fn carries no image. */
int cint_sir_prepare(cint_sir_program *p, cint_sir_image **out, cint_sir_error *err);

void cint_sir_release(cint_sir_image *x);

/* What a program compiled for cint-harness exports (SPEC-09 CONF-13): cint_program_abi's
 * program table, cint_observer_desc, and cint_observer_state with the root module's arenas
 * and pools (NULL when no module has state and the root has neither). */
const cint_program *cint_sir_cint_program(const cint_sir_image *x);
const cint_observer *cint_sir_observer(const cint_sir_image *x);
const cint_state_table *cint_sir_state_table(const cint_sir_image *x);

/* cint_imports_desc (CONF-13 X-2; rt/OPEN.md RT-OQ-75): one host import table per module, from
 * its SIR text's extern rows (SPEC-09 SIR-09a), as compiled C writes them, for
 * cint_ctx_config.imports. */
const cint_host_desc *cint_sir_imports(const cint_sir_image *x);

/* The program entry a generated C main runs (the root module's script, else its main(),
 * else its one test), for cint_program_run_fuel; NULL when it has none. */
cint_main_fn cint_sir_main(const cint_sir_image *x);
/* Whether that entry is a `main` that takes the program arguments, `in U8[_] args`
 * (SPEC-04 LS-221; OQ-306), which cint_program_run_argv passes with CINT_PROGRAM_ARGS. */
int cint_sir_main_args(const cint_sir_image *x);

/* Trace events: one per operation that takes a step ordinal (SPEC-01 IM-113), one per
 * call when it starts and one when it returns. The operation is the node's spelling in
 * the text, not NUL-terminated. */
enum { CINT_SIR_EV_OP = 1, CINT_SIR_EV_CALL = 2, CINT_SIR_EV_RETURN = 3 };

typedef struct cint_sir_event {
    uint32_t kind;
    uint64_t step;
    cint_site site;
    const char *operation;
    uint32_t operation_len;
    uint32_t operand_count;   /* the first three are given */
    uint64_t operands[3];
    uint32_t has_result;      /* 0 when the operation faulted or has none, or the call returned an
                                 error */
    uint64_t result;
    uint64_t error;           /* a return, or a result form: the tag of its error, else 0 (box 12) */
} cint_sir_event;

typedef void (*cint_sir_trace_fn)(void *user, const cint_sir_event *e);

/* Calls fn for each event of x's later entries; NULL stops. */
void cint_sir_trace(cint_sir_image *x, cint_sir_trace_fn fn, void *user);

/* The domain of x's last dispatch (box 14 default BX14-13): its rank, 0 to 4, and its extents,
 * outermost first, so a fault record's work-item ordinal w is the index tuple of w in row-major
 * order; extents past the rank are 0. Rank 0 also when no dispatch has charged its N since x was
 * prepared or since the last dispatch began. */
uint32_t cint_sir_dispatch_domain(const cint_sir_image *x, int64_t extents[4]);

#ifdef __cplusplus
}
#endif

#endif
