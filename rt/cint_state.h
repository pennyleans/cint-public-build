/* cint_state.h: checkpoints, recordings and replay of a context, spelling Proposed (roadmap
 * box 13, units 4, 5 and 8a).
 *
 * The canonical state of SPEC-01 IM-156 and SPEC-03 M-3, the state hash (IM-154, M-5), the
 * checkpoint CINT-CKPT-1 (SPEC-03 H-19; SPEC-06 15), restore under H-19a, M-11 and M-11a, the
 * typed decoder, the interrupt, and the change of revision of H-19b (SPEC-03 5.3), for
 * cpu-c17 and cpu-sir-interp alike (rt/cint_state.c). SPEC-03 5.3 lists these functions in the
 * runtime header; they are declared here, beside it, because rt/cint_rt.h is at its line
 * ceiling (SPEC-09 4.2; rt/OPEN.md RT-OQ-42). Generated code does not use them.
 *
 * Every function that touches a context takes its busy flag and returns CINT_BUSY when it is
 * held, so none runs inside an entry (SPEC-03 A-11; box 13 default BX13-06), except
 * cint_ctx_interrupt, which may be called from a signal handler while an entry runs. */
#ifndef CINT_STATE_H
#define CINT_STATE_H

#include "cint_rt.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Receives a checkpoint's bytes in order, in one or more calls. A status other than CINT_OK
 * ends the checkpoint with that status. */
typedef struct cint_sink {
    cint_status (*write)(void *user, const uint8_t *bytes, size_t len);
    void *user;
} cint_sink;

/* Gives a context its program's module state: one cint_state per module that has
 * module-level variables, as `cint_observer_state` and cint_sir_state_table list them, or
 * NULL for a program that has none. Until the module descriptor carries this table (box 13
 * note 5.4), a host binds it once before its first checkpoint or restore. CINT_REFUSED for a
 * table that does not fit the program: a module out of range or listed twice, a variable
 * that is not an integer or Bool, outside its block, or named twice, a module path that does
 * not end in `.ci`, or a module whose block already exists from another cint_state. */
CINT_RT_API cint_status cint_ctx_bind_state(cint_ctx *ctx, const cint_state_table *t);

/* Writes the context's CINT-CKPT-1 to sink between entries, in a Ready or a Faulted context
 * (H-19): the envelope, whose payload digest, build record digest and build identity are
 * written absent until build records exist (SPEC-07 SEC-REC-11), then the canonical state,
 * with the context's arenas and pools (rt/cint_mem.c; RT-OQ-46). CINT_REFUSED for a NULL
 * argument, a context with no bound state, or a program with no revision identity (one the
 * seed built, SEED-05); CINT_RESOURCE when memory runs out. */
CINT_RT_API cint_status cint_ctx_checkpoint(cint_ctx *ctx, struct cint_sink *sink);

/* Restores a checkpoint (H-19a, M-11, M-11a). Every check runs before any state changes, and
 * a checkpoint that fails one is CINT_REFUSED with the context unchanged: bytes that are not
 * a well-formed CINT-CKPT-1, another runtime contract or revision identity (case 29),
 * globals, a record type or a fault position that the loaded program does not have, or
 * arenas and pools other than those of the table the context's program has named (the
 * table is compared at the program's first arena or pool operation when it has named none;
 * RT-OQ-46). Then the globals, the arenas, pools and generations (M-11), the entry sequence
 * number, the fault record and the effect-log position come back; every registration is
 * removed, since this runtime has no context-owned buffers (borrowed and bridge-owned ones
 * are not context-owned, M-11a), and the storage of a created buffer is freed; and each
 * identifier counter becomes the greater of the checkpoint's and the context's (case 29b),
 * so no arena identifier is issued twice. CINT_BUSY while an export lease is live (A-9). */
CINT_RT_API cint_status cint_ctx_restore(cint_ctx *ctx, const uint8_t *bytes, size_t length);

/* SHA-256 of the canonical state bytes (IM-154): the bytes cint_ctx_checkpoint writes after
 * the envelope. Refused as cint_ctx_checkpoint is. */
CINT_RT_API cint_status cint_ctx_state_hash(cint_ctx *ctx, uint8_t out[32]);

/* Sets the context's interrupt flag. At its next fuel charge the running entry's allowance
 * becomes its consumption so far, so it faults E_FUEL at its first charge of one unit or
 * more. An entry clears the flag when it begins. cpu-sir-interp reads the flag at each
 * charge (cint_rt_interrupt_poll); compiled code reads it once the context head carries it,
 * with the next runtime contract (SPEC-03 5.3; RT-OQ-42). */
CINT_RT_API void cint_ctx_interrupt(cint_ctx *ctx);

/* For an executor, before a fuel charge: applies a pending interrupt. */
void cint_rt_interrupt_poll(cint_ctx *ctx);

/* Moves the context to another revision of its program between entries (H-19b), with that
 * revision's state table (NULL for none). A global whose qualified name and type are
 * unchanged keeps its value; a new one starts at its initial value; the others are dropped.
 * Arenas and pools carry over as RT-OQ-46 writes (OQ-243), through the memory tables that
 * the state tables name. The registry, the identifier counters, the entry sequence number
 * and the effect-log position are kept. CINT_REFUSED for a Faulted context, a context
 * created with a module descriptor or with no bound state, a program without a revision
 * identity, a context with arenas or pools whose state table names no memory table, or a
 * state or memory table that does not fit the program (RT-OQ-42); CINT_RESOURCE when
 * memory runs out. */
CINT_RT_API cint_status cint_ctx_change_revision(cint_ctx *ctx, const cint_program *program,
                                                 const cint_state_table *t);

/* A typed view of a CINT-CKPT-1 (SPEC-03 5.3; box 13 default BX13-11), which points into the
 * bytes it was decoded from. */
#define CINT_OWNER_BORROWED 1u  /* registered host memory */
#define CINT_OWNER_BRIDGE 2u    /* cint_buffer_create (A-9) */
typedef struct cint_checkpoint_info {
    cint_name contract;          /* the runtime contract version */
    const uint8_t *payload_digest;       /* 32 bytes each, NULL when absent */
    const uint8_t *build_record_digest;
    const uint8_t *build_identity;
    uint8_t revision[32];
    uint64_t global_count;       /* read them with cint_checkpoint_global_next */
    uint64_t buffer_count;       /* registry entries, read with cint_checkpoint_buffer_next */
    uint64_t next_buffer, next_arena, next_pool, entries;
    int64_t effects;             /* the effect-log position */
    const uint8_t *fault;        /* the fault record (SPEC-01 IM-149), NULL when absent */
    size_t fault_len;
    const uint8_t *state;        /* the canonical state bytes */
    size_t state_len;
    size_t globals_at, globals_end;  /* where the globals and registry entries lie in `state` */
    size_t buffers_at, buffers_end;
} cint_checkpoint_info;

typedef struct cint_checkpoint_global {
    cint_name name;              /* fully qualified: `<module>.<name>` (SPEC-04 LS-225) */
    cint_tvalue value;           /* a scalar or a wide integer; for an array (RT-OQ-49), its
                                    descriptor only: tag 51, the element's tag, the rank and each
                                    extent (SPEC-01 IM-146); for a struct (RT-OQ-51), tag 71 only */
    const uint8_t *elements;     /* an array's elements, untagged, row-major; a struct's type and
                                    payload, the bytes after its tag; NULL for a scalar */
    uint64_t element_count;      /* their count, a struct's in bytes; 0 for a scalar */
} cint_checkpoint_global;

typedef struct cint_checkpoint_buffer {
    cint_buffer_id id;
    uint64_t generation;
    cint_type type;              /* code 0: a registration of bytes without an element type */
    int64_t extent;              /* elements, or bytes when code is 0 */
    uint32_t owner;              /* CINT_OWNER_* */
    uint8_t ceiling;             /* CINT_VIEW_READ or CINT_VIEW_WRITE */
    uint8_t publish;             /* CINT_PUBLISH_* */
    uint16_t reserved;           /* 0 */
} cint_checkpoint_buffer;

/* An arena of a checkpoint (SPEC-03 M-3; rt/OPEN.md RT-OQ-46). Extents count elements. */
typedef struct cint_checkpoint_arena {
    uint32_t id;
    uint32_t parent;          /* 0 for a module-level arena */
    uint32_t elem_bytes;      /* the bytes of one element as the block writes it (rt/cint_state.c) */
    uint32_t retired;         /* 1 when it or an ancestor is retired (M-20) */
    int64_t offset;           /* a child's element 0 in its parent; 0 for a module-level arena */
    int64_t capacity;
    int64_t used;
    uint64_t generation;
    const uint8_t *elements;  /* a module-level arena's `used` elements; NULL for a child, whose
                                 type and elements are its parent's */
    const uint8_t *type;      /* its element type after the width, type_len bytes; NULL for a child */
    uint32_t type_len;
} cint_checkpoint_arena;

/* A pool of a checkpoint. Its `used` slots each have a record of 17 bytes at `slots`: the
 * generation U64, the mark U8 (CINT_SLOT_*), and the slot below it on the free list plus 1, a
 * U64 (0 at the bottom and for a slot not on the list). Every later slot is vacant at
 * generation 0 and has never held an element. */
#define CINT_SLOT_VACANT 0u
#define CINT_SLOT_OCCUPIED 1u
#define CINT_SLOT_RETIRED 2u
typedef struct cint_checkpoint_pool {
    uint32_t id;
    uint32_t elem_bytes;        /* the bytes of one element as the block writes it */
    int64_t capacity;
    uint64_t used;              /* slots ever used */
    uint64_t top;               /* the slot the next insert takes, plus 1; 0 when none is free */
    uint64_t occupied, retired, vacant;  /* of the used slots; the vacant ones are the free list */
    const uint8_t *slots;
    const uint8_t *elements;    /* the occupied slots' elements in slot order */
    const uint8_t *type;        /* its element type after the width, type_len bytes */
    uint32_t type_len;
} cint_checkpoint_pool;

/* Checks the bytes as H-19a requires of everything that does not need the program, and
 * fills *out. CINT_REFUSED for bytes that are not a well-formed CINT-CKPT-1. */
CINT_RT_API cint_status cint_checkpoint_decode(const uint8_t *bytes, size_t len, cint_checkpoint_info *out);

/* The global or registry entry at *cursor (0 for the first), in the checkpoint's order; each
 * call advances *cursor. CINT_OK while one remains, then CINT_REFUSED. */
CINT_RT_API cint_status cint_checkpoint_global_next(const cint_checkpoint_info *info, size_t *cursor,
                                                    cint_checkpoint_global *out);
CINT_RT_API cint_status cint_checkpoint_buffer_next(const cint_checkpoint_info *info, size_t *cursor,
                                                    cint_checkpoint_buffer *out);
/* The arena, or the pool, at *cursor (0 for the first), in identifier order, likewise. */
CINT_RT_API cint_status cint_checkpoint_arena_next(const cint_checkpoint_info *info, size_t *cursor,
                                                   cint_checkpoint_arena *out);
CINT_RT_API cint_status cint_checkpoint_pool_next(const cint_checkpoint_info *info, size_t *cursor,
                                                  cint_checkpoint_pool *out);

/* The plain text of `cint-interp show` (BX13-11): one line per field of the envelope, each
 * global with its type and exact value, each registry entry, the counters, the fault record
 * field by field, the effect-log position, and the state hash. Returns the text's length
 * and writes as much of it as fits in cap bytes, with no NUL (out may be NULL to measure);
 * 0 for an info that cint_checkpoint_decode did not fill. */
CINT_RT_API size_t cint_checkpoint_render(const cint_checkpoint_info *info, char *out, size_t cap);

/* `cint-interp show FILE`: the text of the checkpoint or recording in the file at path on
 * standard output, in binary mode. CINT_EXIT_OK; CINT_EXIT_CHECK, with nothing written, for
 * bytes that neither cint_checkpoint_decode nor cint_recording_decode accepts;
 * CINT_EXIT_ENVIRONMENT when the file cannot be read or the text written. Nothing goes to
 * standard error. */
CINT_RT_API int cint_checkpoint_show(const char *path);

/* ------------------------------------------------------------------------- */
/* Recordings and replay, spelling Proposed (roadmap box 13, unit 5).        */
/*                                                                           */
/* A recording, CINT-REC-1 (SPEC-03 H-14 to H-18; SPEC-06 10.5), is written  */
/* by a recorder that makes the host's calls on a context: its entries, and  */
/* the checkpoints, restores, clears and changes of revision between them.   */
/* Its layout is in rt/cint_state.c's header comment. Replay re-executes it  */
/* on either CPU backend and compares every boundary (H-16, H-17).           */

#ifndef CINT_DIVERGED
#define CINT_DIVERGED 6      /* a replay mismatch (SPEC-03 A-6, H-17) */
#define CINT_UNREPLAYABLE 7  /* replay cannot go past this event */
#endif

#define CINT_RECORD_FULL 0u    /* inputs recorded in full (H-18) */
#define CINT_RECORD_DIGEST 1u  /* inputs recorded as digests */
#define CINT_RECORD_CASE 2u    /* in full, written as a case, CINT-CASE-1 (SPEC-06 9.2), whose
                                  code carries each module's source */
#define CINT_RECORD_DISPATCH 4u  /* OR'd into the mode: `dispatch` density, a boundary at each
                                    dispatch publication too (SPEC-06 1.5; RT-OQ-48) */

/* One module of a program's code: its path, its canonical SIR text with the sites line
 * (SPEC-09 SIR-17), and its source bytes, NULL when not given. */
typedef struct cint_code_module {
    cint_name path;
    const uint8_t *sir;
    size_t sir_len;
    const uint8_t *source;
    size_t source_len;
} cint_code_module;

/* The source-map digest of SPEC-09 SIR-17 over a program's code, from each module's `sites`
 * line, in the encoding of rt/cint_state.c's header comment (rt/OPEN.md RT-OQ-45): CINT_OK;
 * CINT_REFUSED when n is 0, a module has no well-formed `sites` line naming its path, or two
 * modules have one path; CINT_RESOURCE when memory runs out. */
CINT_RT_API cint_status cint_code_source_map(const cint_code_module *code, uint32_t n, uint8_t out[32]);

typedef struct cint_recorder cint_recorder;

/* Starts a recording of ctx, which has bound state, to sink: the header, the code (code_count
 * modules, 0 when the host gives none), and a checkpoint of the context as it is. With
 * CINT_RECORD_DISPATCH in mode, each dispatch that publishes during an entry is also a
 * `dispatch` event with the state hash there (cint_rt_dispatch_end). `entries`
 * lists the functions cint_rec_entry calls; the header holds the code's source-map digest when
 * cint_code_source_map gives one. Until cint_rec_end the recorder delivers the
 * context's print statements to the context's output and records each as an effect, so the
 * host makes every call on the context through the recorder. CINT_REFUSED as
 * cint_ctx_checkpoint refuses, or for a NULL argument, a mode other than CINT_RECORD_*, a code
 * module whose path does not end in `.ci`, a case without code or without a module's source,
 * or a context that a recorder already records;
 * CINT_RESOURCE when memory runs out; the sink's status when it fails. */
CINT_RT_API cint_status cint_rec_begin(cint_ctx *ctx, const cint_observer *entries, const cint_code_module *code,
                                       uint32_t code_count, uint32_t mode, cint_sink *sink, cint_recorder **out);

/* Calls entries->entries[entry] as its observer function does and records the call: an
 * `entry` event, an `effect` per print statement, and an `outcome`, which after an entry that
 * ran (CINT_OK or CINT_FAULT) holds the state hash and extends the digest chain. Returns the
 * entry's status; CINT_REFUSED, recording nothing, for an entry out of range, a signature
 * other than integers and Bool, or an argument that is not its type's pattern (CONF-13 X-5).
 * After CINT_RESOURCE the recording ends (`end`, reason 1) and later calls are refused. */
CINT_RT_API cint_status cint_rec_entry(cint_recorder *r, uint32_t entry, const uint64_t *args, int64_t fuel,
                                       int64_t depth, uint64_t *result);

/* The same for the program entry of a script or main() program (cint_rt.h 6d), with the
 * context's depth limit. */
CINT_RT_API cint_status cint_rec_main(cint_recorder *r, cint_main_fn main_fn, int64_t fuel);

/* cint_ctx_checkpoint, cint_ctx_restore, cint_ctx_clear_fault and cint_ctx_change_revision,
 * each recorded when it succeeds: `checkpoint` with the state hash; `restore` with the state
 * hash of the checkpoint, the identifier counters chosen and the checkpoint's bytes, which
 * replay restores; `clear_fault` with the SHA-256 of the record cleared; `revision` with the
 * new revision identity, its source-map digest and its code. CINT_REFUSED after the recording
 * ended, and in a case for code without each module's source. */
CINT_RT_API cint_status cint_rec_checkpoint(cint_recorder *r, cint_sink *sink);
CINT_RT_API cint_status cint_rec_restore(cint_recorder *r, const uint8_t *bytes, size_t length);
CINT_RT_API cint_status cint_rec_clear_fault(cint_recorder *r);
CINT_RT_API cint_status cint_rec_change_revision(cint_recorder *r, const cint_observer *entries,
                                                 const cint_state_table *t, const cint_code_module *code,
                                                 uint32_t code_count);

/* Writes the `end` event, unless CINT_RESOURCE ended the recording, gives the context its
 * output back, and frees r. CINT_OK, or the first status other than CINT_OK that the sink
 * returned, after which the recording is incomplete. */
CINT_RT_API cint_status cint_rec_end(cint_recorder *r);

/* A recording, checked by cint_recording_decode, which points into its bytes. */
typedef struct cint_recording_info {
    cint_name contract;          /* the runtime contract version */
    uint8_t revision[32];        /* of the first revision */
    uint32_t mode;               /* CINT_RECORD_FULL or CINT_RECORD_DIGEST */
    uint32_t is_case;            /* 1: a case, CINT-CASE-1 (SPEC-06 9.2) */
    uint32_t has_map;            /* 1: map is the first revision's source-map digest */
    uint8_t map[32];
    uint32_t density;            /* 0: entry; 1: dispatch (CINT_RECORD_DISPATCH) */
    uint64_t interval;           /* the state-hash interval: 1, every entry (H-14a) */
    uint32_t module_count;       /* modules of the first revision's code */
    uint32_t end_reason;         /* 0 closed by the host, 1 CINT_RESOURCE, 2 an unrestricted occurrence */
    const uint8_t *initial;      /* the CINT-CKPT-1 of the initial state */
    size_t initial_len;
    uint64_t event_count;        /* events before the end event */
    uint64_t boundaries;         /* entries that ran, and dispatch events of their own */
    uint8_t chain[32];           /* the head of the digest chain */
    const uint8_t *bytes;
    size_t len, code_at, events_at;
} cint_recording_info;

#define CINT_EVENT_ENTRY 1u
#define CINT_EVENT_EFFECT 2u
#define CINT_EVENT_OUTCOME 3u
#define CINT_EVENT_CLEAR_FAULT 4u
#define CINT_EVENT_CHECKPOINT 5u
#define CINT_EVENT_RESTORE 6u
#define CINT_EVENT_REVISION 7u
#define CINT_EVENT_END 8u
#define CINT_EVENT_DISPATCH 9u
#define CINT_EVENT_UNRESTRICTED 10u  /* an `unrestricted` extern called: the recording ends (H-14; RT-OQ-70) */

/* One event. Zeroed, it stands before the first; cint_recording_next moves it on. An effect
 * is a print statement, whose name is "print", or an extern call (SPEC-03 A-15; RT-OQ-70),
 * whose name is the extern's: `data` holds its request block and `response` its response
 * block, in the layouts of rt/cint_rec.c's header comment. */
typedef struct cint_recording_event {
    uint32_t kind;               /* CINT_EVENT_* */
    uint32_t form;               /* entry: 0 an exported function, 1 the program entry */
    uint64_t index;              /* from 0 */
    size_t next;                 /* where the next event begins; 0 before the first */
    uint64_t sequence;           /* entry: its sequence number */
    cint_name module, name;      /* entry: the function (empty for form 1); effect: "print" or
                                    the extern's name; unrestricted: the extern's name */
    uint32_t arg_count;          /* entry: read them with cint_recording_arg */
    uint32_t module_count;       /* revision: modules of its code */
    int64_t fuel, depth, arena;  /* entry: budget, depth limit, frame-arena capacity */
    int64_t ordinal;             /* effect, unrestricted: the effect-log position; dispatch: its number */
    cint_site site;              /* effect, unrestricted: the print statement or the call */
    cint_status status;          /* effect: the host's; outcome: the entry's */
    const uint8_t *data;         /* effect: the request bytes; outcome: the fault record, NULL
                                    when none; restore: the checkpoint; revision: its code */
    size_t data_len;
    int64_t consumed, allowance; /* outcome: fuel consumed, and the allowance in force at the
                                    end, which an interrupt lowers */
    uint32_t has_result, has_error, has_hash, reason;  /* reason: end (0 the host, 1 CINT_RESOURCE,
                                    2 an unrestricted occurrence) */
    cint_tvalue result;          /* outcome */
    cint_name error_set;         /* outcome: an error result's set and tag */
    uint64_t error_tag;
    uint8_t hash[32];            /* outcome, dispatch: the state hash; clear_fault: the record's
                                    SHA-256; checkpoint, restore: the state hash; revision:
                                    the source-map digest, when has_hash; end: the head of
                                    the digest chain */
    uint64_t next_buffer, next_arena, next_pool;  /* restore: the identifier counters */
    uint8_t revision[32];        /* revision */
    size_t args_at;
    const uint8_t *response;     /* effect of an extern call: the response block (ABI 3.3) */
    size_t response_len;
} cint_recording_event;

/* Checks a CINT-REC-1 or a CINT-CASE-1: its form, its initial checkpoint and each checkpoint it
 * carries (H-19a), each source-map digest, present exactly when cint_code_source_map gives
 * one for its code, a case's sources, the order of its events (an `entry`, its effects and, at
 * `dispatch` density, its dispatch events, then its `outcome`; the `end` last, with the event
 * count), and the digest chain recomputed from its boundaries. Fills *out.
 * CINT_REFUSED for bytes that are not such a recording, an incomplete one among them. */
CINT_RT_API cint_status cint_recording_decode(const uint8_t *bytes, size_t len, cint_recording_info *out);

/* The event after *e (the first when *e is zeroed): CINT_OK, then CINT_REFUSED after the end
 * event. */
CINT_RT_API cint_status cint_recording_next(const cint_recording_info *info, cint_recording_event *e);

/* Argument k of an entry event. */
CINT_RT_API cint_status cint_recording_arg(const cint_recording_info *info, const cint_recording_event *e,
                                           uint32_t k, cint_tvalue *out);

/* Module k of the code of a revision event, or of the first revision when e is NULL. */
CINT_RT_API cint_status cint_recording_module(const cint_recording_info *info, const cint_recording_event *e,
                                              uint32_t k, cint_code_module *out);

/* The text of `cint-interp show` for a recording: the header, the code, the initial state
 * hash, and one line per event. As cint_checkpoint_render. */
CINT_RT_API size_t cint_recording_render(const cint_recording_info *info, char *out, size_t cap);

/* What a replay compared when it diverged. */
#define CINT_REPLAY_ENTRY 1u      /* no such function, or another depth limit or frame-arena capacity */
#define CINT_REPLAY_EFFECT 2u     /* a print statement's position, site or bytes, an extern call's position,
                                     site, name, arguments or, in digest mode, status or response, or one
                                     too many or few */
#define CINT_REPLAY_STATUS 3u
#define CINT_REPLAY_FAULT 4u      /* the fault record's bytes */
#define CINT_REPLAY_FUEL 5u
#define CINT_REPLAY_RESULT 6u
#define CINT_REPLAY_ERROR 7u
#define CINT_REPLAY_STATE 8u      /* the state hash */
#define CINT_REPLAY_ALLOWANCE 9u
#define CINT_REPLAY_CLEAR 10u
#define CINT_REPLAY_RESTORE 11u
#define CINT_REPLAY_REVISION 12u
#define CINT_REPLAY_CHAIN 13u
#define CINT_REPLAY_DISPATCH 14u  /* a dispatch boundary's number, or one too many or few */

/* The program of a later revision, from a `revision` event: its entries, its program entry
 * (or NULL), and its state table; NULL when the host has none. */
typedef const cint_observer *(*cint_revision_fn)(void *user, const cint_recording_event *revision,
                                                 cint_main_fn *main_fn, const cint_state_table **table);

typedef struct cint_replay_config {
    uint32_t size;               /* sizeof(cint_replay_config) */
    uint32_t echo;               /* 1: deliver the recorded print bytes to the context's output */
    uint64_t stop;               /* 0, or the boundary after whose comparisons replay stops */
    const cint_observer *entries;  /* the first revision's */
    cint_main_fn main_fn;        /* its program entry, or NULL */
    cint_revision_fn revision;   /* or NULL */
    void *user;
} cint_replay_config;

typedef struct cint_replay_report {
    cint_status status;          /* CINT_OK, CINT_DIVERGED or CINT_UNREPLAYABLE */
    uint32_t what;               /* CINT_REPLAY_* when diverged, else 0 */
    uint64_t event;              /* the event that diverged or that replay stopped before; else
                                    the event after the last one replayed */
    uint64_t boundaries;         /* boundaries replayed */
    uint8_t chain[32];           /* the digest chain's head after them */
} cint_replay_report;

/* Replays a decoded recording on ctx, a context of the recording's first revision with that
 * revision's state bound and nothing registered (H-16, H-17): restores the initial state, then
 * applies each event in order, calling each entry with its recorded arguments and budget (the
 * allowance its outcome records, when an interrupt lowered it) and comparing every print
 * statement with its effect, every dispatch boundary with its event, and every outcome with the
 * recorded one. The first difference stops replay with CINT_DIVERGED; an outcome of CINT_RESOURCE
 * stops it with CINT_UNREPLAYABLE before that entry; with cfg->stop, replay stops after that
 * boundary, and the context stays as replay left it. A dispatch boundary lies inside an entry:
 * replay keeps the state there, runs the rest of the entry without comparing it, and restores
 * the state kept, whose identifier counters may then be above the boundary's (M-11). Returns
 * CINT_OK with *out filled; CINT_REFUSED for a NULL argument, a context that the initial state
 * does not restore into as it was recorded, or a recorder on ctx; CINT_RESOURCE when memory runs
 * out. */
CINT_RT_API cint_status cint_replay(cint_ctx *ctx, const cint_recording_info *rec, const cint_replay_config *cfg,
                                    cint_replay_report *out);

/* The name of a CINT_REPLAY_* value ("entry", "effect", ...), or NULL. */
CINT_RT_API const char *cint_replay_what(uint32_t what);

#ifdef __cplusplus
}
#endif

#endif /* CINT_STATE_H */
