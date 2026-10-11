/* wb_debug.h: the debugger of `cint debug` (box 14 units 3b-D and 3b-Q; SPEC-06 3.1, 9, 10; the
 * box 14 note, sections 6.4, 7.1, and 7.3). */
#ifndef WB_DEBUG_H
#define WB_DEBUG_H

#include "cint_state.h"
#include "wb.h"

/* `cint debug` with its command line, argv[0] the executable; `--json` may come anywhere, and
 * the caller has handled --help. The first other argument is `replay`, `locate`, `diff`, or a
 * recording or case to open in the read-only session. Writes what it found on standard output
 * and returns the exit status (SPEC-06 3.2). For a usage, environment, or internal error (4, 5,
 * 7) the message is in err, which the caller writes, as a CINT-TOOL-1 line under --json; any
 * other status, and any with err empty (the read-only session's), has said why itself. */
int wb_debug(int json, int argc, char **argv, wb_buf *err);

/* An entry event as a call, `deposit(I64 5)`, or `the program entry`. */
void wb_entry_text(wb_buf *b, const cint_recording_info *info, const cint_recording_event *e);

/* A change a test makes to the candidate's state at boundary k, before it is compared (the
 * qualification test of note section 7.3): `rerun` is 1 when the candidate runs again to the
 * boundary of a difference (SPEC-06 9.3 step 6). */
typedef void (*wb_alter_fn)(uint64_t k, int rerun, cint_ctx *ctx);

/* The same change, made by the test to the cpu-sir-interp side of the session's `:run --check`
 * before the two backends are compared (wb_session.c), with k the entry's number, so that the
 * test reaches the session's difference path. NULL outside the test. */
extern wb_alter_fn wb_check_alter;

/* `cint debug diff` (unit 3b-Q; wb_diff.c). */
typedef struct wb_diff_opts {
    const char *path;            /* the case */
    const char *ref, *cand;      /* cpu-c17 or cpu-sir-interp */
    const char *cint;            /* the cint that builds cpu-c17's libraries */
    const char *out;             /* where the case's sources and libraries go */
    const char *receipt;         /* the receipt's path, or NULL */
    const char *tool[3];         /* the compiler's, interpreter's and workbench's source identities, or NULL */
    int json;                    /* the receipt is the report */
    wb_alter_fn alter;           /* NULL; a test sets it for a cpu-sir-interp candidate */
} wb_diff_opts;

/* Compares the case on the two backends (SPEC-06 9.3) and writes the report of 9.4 on standard
 * output. Returns 0 for `agree`, 3 for any other outcome, or 5 or 7 with the reason in err. */
int wb_diff(const wb_diff_opts *o, wb_buf *err);

#endif /* WB_DEBUG_H */
