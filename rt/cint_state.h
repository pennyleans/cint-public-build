/* cint_state.h: checkpoints of a context, spelling Proposed (roadmap box 13, unit 4).
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
 * written absent until build records exist (SPEC-07 SEC-REC-11), then the canonical state.
 * CINT_REFUSED for a NULL argument, a context with no bound state or with arenas or pools
 * (rt/cint_mem.c; RT-OQ-42), or a program with no revision identity (one the seed built,
 * SEED-05); CINT_RESOURCE when memory runs out. */
CINT_RT_API cint_status cint_ctx_checkpoint(cint_ctx *ctx, struct cint_sink *sink);

/* Restores a checkpoint (H-19a, M-11, M-11a). Every check runs before any state changes, and
 * a checkpoint that fails one is CINT_REFUSED with the context unchanged: bytes that are not
 * a well-formed CINT-CKPT-1, another runtime contract or revision identity (case 29),
 * globals, a record type or a fault position that the loaded program does not have, or a
 * context with arenas or pools. Then the globals, the entry sequence number, the fault
 * record and the effect-log position come back; every registration is removed, since this
 * runtime has no context-owned buffers (borrowed and bridge-owned ones are not
 * context-owned, M-11a), and the storage of a created buffer is freed; and each identifier
 * counter becomes the greater of the checkpoint's and the context's (case 29b). CINT_BUSY
 * while an export lease is live (A-9). */
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
 * The registry, the identifier counters, the entry sequence number and the effect-log
 * position are kept. CINT_REFUSED for a Faulted context, a context created with a module
 * descriptor, a context with no bound state or with arenas or pools, a program without a
 * revision identity, or a table that does not fit the program (RT-OQ-42); CINT_RESOURCE
 * when memory runs out. */
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
    cint_tvalue value;
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

/* Checks the bytes as H-19a requires of everything that does not need the program, and
 * fills *out. CINT_REFUSED for bytes that are not a well-formed CINT-CKPT-1. */
CINT_RT_API cint_status cint_checkpoint_decode(const uint8_t *bytes, size_t len, cint_checkpoint_info *out);

/* The global or registry entry at *cursor (0 for the first), in the checkpoint's order; each
 * call advances *cursor. CINT_OK while one remains, then CINT_REFUSED. */
CINT_RT_API cint_status cint_checkpoint_global_next(const cint_checkpoint_info *info, size_t *cursor,
                                                    cint_checkpoint_global *out);
CINT_RT_API cint_status cint_checkpoint_buffer_next(const cint_checkpoint_info *info, size_t *cursor,
                                                    cint_checkpoint_buffer *out);

/* The plain text of `cint-interp show` (BX13-11): one line per field of the envelope, each
 * global with its type and exact value, each registry entry, the counters, the fault record
 * field by field, the effect-log position, and the state hash. Returns the text's length
 * and writes as much of it as fits in cap bytes, with no NUL (out may be NULL to measure);
 * 0 for an info that cint_checkpoint_decode did not fill. */
CINT_RT_API size_t cint_checkpoint_render(const cint_checkpoint_info *info, char *out, size_t cap);

/* `cint-interp show FILE`: the text of the checkpoint in the file at path on standard
 * output, in binary mode. CINT_EXIT_OK; CINT_EXIT_CHECK, with nothing written, for bytes
 * that cint_checkpoint_decode refuses; CINT_EXIT_ENVIRONMENT when the file cannot be read
 * or the text written. Nothing goes to standard error. */
CINT_RT_API int cint_checkpoint_show(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* CINT_STATE_H */
