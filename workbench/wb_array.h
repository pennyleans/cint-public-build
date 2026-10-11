/* wb_array.h: arrays, views, and kernels in the workbench (roadmap box 14 unit 4a).
 *
 * Unit 4a of the box 14 build plan (docs/design/notes/2026-10-05-box14-workbench-debugging.md,
 * sections 5.6, 5.7, and 6.1) adds the inspector formats /grid, /stats, and /view over decoded
 * values (wb_inspect.c), the reading of SIR text that box 13's reader does not run yet, for
 * `explain` of kernels, views, and reductions (wb_explain.c), and fault and error reports from
 * `.expect` text in formats 1 to 4 and from `cint-harness` output (wb_fault.c). Unit 4b shows
 * the session's module-level arrays with these formats (wb_session.c).
 */
#ifndef WB_ARRAY_H
#define WB_ARRAY_H

#include "wb.h"

/* ------------------------------------------------------------------------- */
/* The inspector's arrays and views (wb_inspect.c; SPEC-06 6.2, 6.3).         */

#define WB_RANK_MAX 4u

/* A view as the inspector reads it (SPEC-02 V-1, V-3): the element at logical index
 * (i0, ...) is element origin + sum ik * stride[k] of the buffer, each element `size` bytes
 * little-endian, where size is that of the element type. Indices are shown in declared bounds,
 * lower[k] + ik. A column 02 buffer has no canonical identity (SPEC-01 IM-81). */
typedef struct wb_view {
    unsigned tag;                 /* CINT_TAG_BOOL or an integer tag of 8 to 64 bits */
    uint32_t rank;                /* 1 to WB_RANK_MAX */
    int64_t extent[WB_RANK_MAX], lower[WB_RANK_MAX], stride[WB_RANK_MAX];
    int64_t origin;
    const uint8_t *buffer;
    int64_t buffer_len;           /* in elements */
    int has_id;
    uint64_t id, generation;
    int write;                    /* the view's permission: 1 write, 0 read */
    const char *placement;        /* "host" in column 02 */
} wb_view;

enum { WB_FMT_GRID = 16u, WB_FMT_STATS = 32u, WB_FMT_VIEW = 64u };

/* The formats of `:inspect` for an array or a view: those of a scalar, applied to each element
 * shown, and the array formats. A /grid window is half-open, in declared bounds. */
typedef struct wb_aformats {
    wb_formats f;
    unsigned array;               /* WB_FMT_GRID | WB_FMT_STATS | WB_FMT_VIEW */
    int window;                   /* 1 when /grid gave [r0..r1, c0..c1] (or [c0..c1] at rank 1) */
    int64_t r0, r1, c0, c1;
    uint32_t width;               /* the columns a grid may fill; 0 for 80 */
} wb_aformats;

/* Parses the format words of `:inspect` of an array or a view: the scalar formats, /grid with
 * an optional window, /stats, and /view. Returns 0, or -1 with the reason in err. */
int wb_aformats_parse(int argc, const char *const *argv, wb_aformats *fmt, wb_buf *err);
/* `:inspect` of an array or a view: the summary line and the first elements in canonical order
 * by default, then each format asked for. Returns 0, or -1 with the reason in err. */
int wb_inspect_array(wb_buf *b, const char *name, const wb_view *v, const wb_aformats *fmt, wb_buf *err);

/* The bits SPEC-01 IM-80 bounds for the sum of n elements of w bits, w + ceil(log2 n); w for
 * n at most 1. */
uint32_t wb_sum_bits(uint32_t w, uint64_t n);

/* ------------------------------------------------------------------------- */
/* Reports from `.expect` text (wb_fault.c; SPEC-09 CONF-11, SPEC-06 8).      */

enum { WB_OUT_VALUE, WB_OUT_FAULT, WB_OUT_ERROR, WB_OUT_REFUSED, WB_OUT_COMPILE };

#define WB_DESC_MAX 8u

/* An outcome as a frozen `.expect` file or a block of cint-harness output writes it. Names and
 * positions point into the text it was parsed from, which must outlive it. */
typedef struct wb_expect {
    unsigned format;              /* 1 to 4: the format line, or the first format with each line */
    int outcome;                  /* WB_OUT_* */
    const char *name, *text;      /* the case name or number; refused.reason or diagnostic.code */
    uint32_t name_len, text_len;
    int64_t stdout_bytes, fuel;   /* -1 when the text has no such line */
    int has_return, has_dfault, self_rev, self_map;
    wb_tval ret, tag;             /* the return value; format 3: error.tag */
    const char *set, *value;      /* format 3: error.set and error.value */
    uint32_t set_len, value_len;
    wb_fault fault;               /* a fault, or a diagnostic's position and compile-time fault */
    uint32_t ndesc, nstate;       /* format 4: fault.descriptor lines; state.global lines */
    const char *desc[WB_DESC_MAX];
    uint32_t desc_len[WB_DESC_MAX];
} wb_expect;

/* Parses the lines of SPEC-09 CONF-11 in text: a whole `.expect` file, or the output of
 * cint-harness, whose `--cases` mode writes `case <n>` and the lines from `outcome` on. A second
 * `case` line ends the outcome, and *used (when not NULL) is where it starts. Returns 0, or -1
 * with the line and the reason in err. wb_fault_json of e->fault is its JSON form. */
int wb_expect_parse(const char *text, size_t n, wb_expect *e, size_t *used, wb_buf *err);
/* The report of a parsed outcome: for a fault, the lines of wb_fault_text with `self` for the
 * revision and the source map, the descriptors after the operands, and the phase by name; for an
 * error result, the line of SPEC-06 3.4a and the format 3 fields; for a compile error, the
 * diagnostic and its compile-time fault; otherwise one line. */
void wb_expect_text(wb_buf *b, const wb_expect *e, wb_source_fn source, void *user);

#endif /* WB_ARRAY_H */
