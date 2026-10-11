/* wb_replay.h: replay of a box 13 recording on a context of its own, and the trace events of
 * one entry (box 14 units 3a and 3b; SPEC-06 10).
 *
 * The session's `:goto`, `:trace`, `:replay`, and `:effects` and the debugger share these. A
 * replay compares every print and outcome with the recording (SPEC-03 H-17), in programs
 * loaded from the SIR text the recording holds, and stops only after a boundary.
 */
#ifndef WB_REPLAY_H
#define WB_REPLAY_H

#include "cint_interp.h"
#include "cint_state.h"
#include "wb.h"

#define WB_TRACE_MAX 10000u   /* trace events kept, the last ones */

typedef struct wb_replayer {
    cint_recording_info info;
    cint_sir_program **progs;        /* each revision's, kept until the replay is freed */
    cint_sir_image **imgs;
    uint32_t n, cap;
    cint_sir_program *prog;          /* the revision replay is in */
    cint_sir_image *img;
    cint_ctx *ctx;                   /* made by the caller for the first revision's program */
    cint_sir_trace_fn trace;
    void *trace_user;
    cint_replay_report rep;
    /* set after wb_replay_load, or NULL: called after each entry that ends at a boundary, with
     * the boundary's number and the context in its state, before replay compares the outcome */
    void (*at)(void *user, uint64_t boundary, cint_ctx *ctx);
    void *at_user;
    uint64_t passed;                 /* boundaries `at` was called with */
} wb_replayer;

/* Decodes the recording rec[0..n) into rp, which it clears first, and loads the code of its
 * first revision, with fn (or NULL) called for each trace event. The caller then makes rp->ctx
 * for cint_sir_cint_program(rp->img) with cint_sir_state_table(rp->img). Returns 0, or -1. */
int wb_replay_load(wb_replayer *rp, const void *rec, size_t n, cint_sir_trace_fn fn, void *user);

/* Replays on rp->ctx up to boundary `stop` (0: restores the initial state only; UINT64_MAX:
 * all of it); the context stays at the state reached. Returns 0, or -1 with the reason in b. */
int wb_replay_run(wb_replayer *rp, uint64_t stop, wb_buf *b);

/* Frees what rp holds, its context included, and clears it. */
void wb_replay_free(wb_replayer *rp);

/* Hands over the context and the program it is in, which the caller then owns (cint_ctx_destroy,
 * cint_sir_release, cint_sir_free); wb_replay_free frees the rest. */
void wb_replay_keep(wb_replayer *rp, cint_ctx **ctx, cint_sir_program **prog, cint_sir_image **img);

/* The backward scan of SPEC-06 10.1 over global `name`, qualified: its value at the last boundary
 * read, and its last change after boundary `from`. wb_scan_at is a replayer's `at`, the scan its
 * user. */
typedef struct wb_scan {
    char name[320];
    uint64_t from, last;
    wb_tval prev, was, now;
    int bad;                         /* a state could not be read */
} wb_scan;
void wb_scan_at(void *user, uint64_t at, cint_ctx *ctx);

/* One recording on the way a case covers: rec[0..n) from boundary `from` (0: its initial
 * state) to boundary `to`; `cont` when it goes on from the state the one before it left, as a
 * segment does after a replay, else from its own initial state. */
typedef struct wb_part {
    const void *rec;
    size_t n;
    uint64_t from, to;
    int cont;
} wb_part;

/* The source bytes of module path[0..len) of the program whose revision identity is rev, or
 * NULL when none is kept. */
typedef const uint8_t *(*wb_case_source_fn)(void *user, const uint8_t rev[32], const char *path, size_t len,
                                           size_t *n);

/* Writes to out the case (CINT-CASE-1, SPEC-06 9.2) of the way parts[0..n): replay reaches
 * boundary `from` of the first on a context of its own, then a recorder in case mode makes
 * each later call again, with the code of each revision and the sources src gives, and each
 * outcome is compared with the recorded one. The case is then replayed on a new context.
 * state is the state hash where both end, effects the prints the case holds. Returns 0, or -1
 * with the reason in b. */
int wb_case_write(const wb_part *parts, uint32_t n, wb_case_source_fn src, void *user, wb_buf *out, uint8_t state[32],
                  uint64_t *effects, wb_buf *b);

/* A trace event and the program it ran in. */
typedef struct wb_traced {
    cint_sir_event e;
    const cint_sir_program *p;
} wb_traced;

/* Keeps the last WB_TRACE_MAX events after the first `skip`: wb_on_trace is the trace function,
 * with the tracer as its user. */
typedef struct wb_tracer {
    const wb_replayer *rp;
    uint64_t skip, seen, kept;       /* events before the entry's; events so far; the entry's */
    wb_traced *ring;                 /* the entry's last WB_TRACE_MAX, or NULL to count only */
} wb_tracer;

void wb_on_trace(void *user, const cint_sir_event *e);

/* An operand or result of operation op[0..len): unsigned when its last word is a uN type, a
 * Bool for `bool`, and signed otherwise, since signed values are kept sign-extended. */
void wb_bits_text(const char *op, uint32_t len, uint64_t v, char *out, size_t cap);

/* A trace listing: the header line, then one line per event with its step ordinal ("-" for a
 * return), the position text `where`, the operation, its first three operands, and its result,
 * or `fault` (a fault code's name, or NULL) on an operation that has none; no line ends in a
 * blank. */
void wb_trace_head(wb_buf *b);
void wb_trace_line(wb_buf *b, const cint_sir_event *e, const char *where, const char *fault);

/* The position text of an event's site, or "" for none. */
typedef void (*wb_where_fn)(void *user, const wb_traced *x, char *out, size_t cap);

/* The path of an entry's error result, tag `tag`, from its trace (SPEC-06 5.3; BX12-19,
 * BX14-18): from the entry's function down, the last call of each function that returned an
 * error before the function returned. Written innermost first: the function that returned the
 * error, then each function that passed it on, with the position of its call. */
void wb_error_path(wb_buf *b, const wb_tracer *t, uint64_t tag, wb_where_fn where, void *user);

#endif
