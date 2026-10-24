/* sir_exec.c: the execution of cpu-sir-interp (SPEC-09 BACK-01a).
 *
 * Runs a program that sir_read.c read and checked, node by node, under the execution
 * model of the box 13 note (section 4.4): structured control flow as the text writes it
 * (SIR-05), frames under the depth limit through cint_rt_call_enter (SPEC-01 IM-119),
 * fuel at fuel.charge nodes only (SIR-07; the leading charge of an entry function, an
 * export, main, script or test, is charged by cint_rt_entry_begin when it runs as an entry
 * and not at all when it is called, as compiled C does), step ordinals as IM-113 counts them, print
 * through the runtime, and every arithmetic, conversion, built-in, index, and shape node
 * through the cint_rt.h helper that compiled C calls for that node (BX13-03), so the
 * fault records are the runtime's. A value is a 64-bit pattern: a signed type's
 * sign-extended, an unsigned type's zero-extended, a Bool's 0 or 1.
 *
 * Scalars, structs, arrays and views of either, string data, and module state, module-level
 * arrays included (box 13 MLA), run here,
 * and the box 09 forms: views of rank 1 to 4 (an origin, and an extent and a
 * stride per dimension, as cint_vdesc holds them, over storage of a known element
 * count, which every access is checked against), slices, copies, reductions and
 * run-time-sized arrays, with the frame arena's charges and marks as compiled C makes
 * them; and kernels, whose dispatch runs as the C backend writes it (entry checks, the
 * charge of N, staging views from scratch storage, the work-items in order, the
 * reductions' accumulators, the epilogue and the publication), a fault of one leaving the
 * dispatch's address in its record (CONF-11 format 4); and box 12's arenas, pools and handles
 * through rt/cint_mem.c, each element in the interpreter's cells (unit 8b-2); and calls of
 * externs (host.call) through each module's host import table, which its extern lines give, with
 * an entry's Grants parameter 0, as compiled C passes it (I/O services lane U5c, rt/OPEN.md RT-OQ-75).
 * A struct takes the cells its struct line measures (sir_read.c), its fields in order, and
 * an array its elements one after another; a struct parameter is a copy, as the C
 * backend passes it, and an `inout` one or a view is the caller's place. The observer
 * entries are the exported functions (SIR-09a's export mark) of the last module that has
 * any that take and return scalars, which is where compiled C finds its entries (CONF-13
 * X-3; compiler/OPEN.md CINTC-OQ-38). A form this unit does not run is refused with
 * CINT_SIR_UNSUPPORTED when an entry reaches it. Status: Proposed, box 13 units 2, 3, 7a, 7b
 * and 8b. */
#define _CRT_SECURE_NO_WARNINGS 1

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cint_rt.h"
#include "cint_mem.h"
#include "cint_state.h"
#include "cint_wide.h"
#include "cint_fixed.h"
#include "sir.h"

#define EX_ENTRIES 256u   /* observer entries of one program: one trampoline each */

/* A place as a view (SPEC-02 V-1; cint_vdesc), in elements of its element's cells: an
 * array, a struct or a scalar has rank 1, origin 0 and stride 1. span counts the elements
 * from base its storage holds, so that no access leaves it. held is 1 plus the scratch mark
 * at which run-time storage was allocated for it (view.alloc, dispatch.stage), else 0, so
 * that a frame.release unbinds every place whose storage it gives back. */
typedef struct ex_ref {
    uint64_t *base;
    int64_t origin, rank, shape[4], stride[4];
    int64_t span, held;
} ex_ref;

#define EX_UNSET UINT32_MAX
#define HCELLS (sizeof(cint_handle) / sizeof(uint64_t))   /* a handle's cells (box 12) */

typedef struct ex_fn {
    uint32_t ncell;    /* the cells of its slots */
    uint32_t nwcell;   /* the cells of its wide temporaries (box 15) */
    uint32_t *at;      /* per slot: its first cell, EX_UNSET for a reference */
    uint32_t *wat;     /* per temporary: a wide one's first cell among them */
    int64_t *ext;      /* per slot: its extent, 1 for a scalar */
    cint_sir_error *why;   /* the first form this unit does not run, or NULL */
    int seen;          /* reached from an entry */
    char *kname;       /* a kernel: `<dotted module>.<name>`, as its fault exit names it */
    struct ex_frame *spare;   /* frames of its returned calls, for its next call */
} ex_fn;

/* The fast form of a node (optimization unit O9b): the commonest nodes of a function that is not
 * a kernel, run without decoding the node and its operands while no trace is set, with the node's
 * effects, faults and step ordinals. EXF_NONE runs the node itself. a, b and src are value operands,
 * a temporary or a parameter (EX_PAR), except that a is the first cell of a local slot's own cells
 * for a load, a store, an index check and an element access; value is a constant's bits, a charge's
 * amount, a comparison's CM_*, where a jump goes (for EXF_JZ, when a is 0), or an index check's or
 * element access's extent;
 * t is an index check's index type. */
enum { EXF_NONE, EXF_NOP, EXF_CONST, EXF_LOADL, EXF_STOREL, EXF_RET, EXF_CMP, EXF_JZ, EXF_JUMP, EXF_FUEL,
       EXF_ICHECK, EXF_ILOAD, EXF_ISTORE, EXF_ADD, EXF_SUB, EXF_MUL, EXF_DIV, EXF_REM };
#define EX_PAR 0x80000000u   /* an operand that is a parameter, not a temporary */
#define EXV(k) ((k) & EX_PAR ? *fr->par[(k) & ~EX_PAR].base : fr->t[k])
typedef struct ex_ins {
    uint8_t code;
    uint16_t t;
    uint32_t site;
    int32_t dst;
    uint32_t a, b, src;
    int64_t value;
} ex_ins;

typedef struct ex_mod {
    cint_module mod;
    cint_site_info *sites;
    ex_ins *code;      /* per node, its fast form (prepare_body) */
    char *ops;         /* the sites' operations, NUL-terminated */
    ex_fn *fns;
    ex_ref *data;      /* by data ordinal (si_node.jump of a data node) */
    uint32_t ndata;
    uint8_t *bytes;    /* print.bytes text, decoded; si_node.jump is the offset */
    cint_state state;  /* bytes 0: no module state */
    cint_state_var *vars;
    uint8_t *init;
    int64_t *ext;      /* per global: a module-level array's extents, four places, which its entry
                          points at */
    uint64_t *gcell;   /* the module-level arrays' elements and structs' scalars, a cell each,
                          while an entry runs */
    ex_ref *garr;      /* per global: a module-level array's or struct's place over gcell */
    cint_state_type *gtype; /* per global: a module-level struct's type (box 15, OQ-260), as
                               mem_type gives an element's */
    uint8_t synced;    /* 1 once the running entry has read gcell from the module's block */
    cint_error_set *sets;   /* the module's error sets (box 12), as compiled C describes them */
    cint_mem_decl *memd;    /* its arenas and pools (box 12), each element in its cells */
    cint_mem_table mem;
    const char **vnames;    /* their values' names */
    char *names;            /* the names, NUL-terminated */
    cint_host_import *hosts;   /* its host import table (SIR-09a extern rows; RT-OQ-75), as compiled C's */
    uint8_t *hbytes;           /* the rows' names and signatures */
    cint_host_table host;
} ex_mod;

struct cint_sir_image {
    cint_sir_program *p;
    ex_mod *mods;
    const cint_module **modtab;
    cint_program program;
    cint_observer observer;
    cint_observer_entry entries[EX_ENTRIES];
    uint32_t etype[EX_ENTRIES][18];
    uint32_t efn[EX_ENTRIES];
    uint32_t emod;
    const cint_state **states;
    cint_state_table table;
    const cint_host_table **htabs;
    cint_host_desc imports;   /* cint_imports_desc: the modules' host import tables, in program order */
    int32_t main_fn;   /* the root module's entry, or -1 */
    cint_sir_trace_fn trace;
    void *trace_user;
    uint64_t step;     /* the next step ordinal of the current entry */
    uint32_t drank;    /* the last dispatch's domain (cint_sir_dispatch_domain) */
    int64_t dext[4];
};

/* The accumulator of a kernel's scalar `out` (back_c.ci, kernel_locals): the exact total of
 * sum and sum_wrap, the running value, and the contributions so far. */
typedef struct ex_acc {
    cint_zacc z;
    uint64_t r;
    int64_t n;
    cint_wzacc wz;     /* with a wide side (box 15; back_c.ci, wide_acc): a sum's exact total */
    uint64_t wr[16];   /* a wide target's running value, its limbs */
} ex_acc;

typedef struct ex_frame {
    uint32_t mi;
    const si_func *f;
    uint64_t *t;       /* temporaries */
    ex_ref *slot;
    ex_ref *par;
    uint64_t *cell;    /* the slots' cells, then the parameters' (si_param.cells) */
    int64_t *mark;     /* the frame arena's marks at entry (charges, storage), then two per frame.mark */
    /* a kernel's dispatch: its site and the writable bits of its views (the caller's), its
     * number, phase, work-item and step ordinal (cint_rt_dispatch_fault), the N it charged and the
     * work-items run since, and per parameter the accumulator of a scalar `out` */
    cint_site ks;
    uint32_t kw, kph;
    int64_t kd, kwi, kst, kn, kc;
    ex_acc *acc;
    uint64_t *h;       /* a handle temporary's four cells, by its number (box 12; si_func.handles) */
    uint64_t *w;       /* the wide temporaries' limbs (box 15), temporary k's from wat[k] */
    const uint32_t *wat;
    uint64_t etag;     /* the tag of the last call or result form, which error.tag reads (box 12) */
    uint64_t rtag;     /* the tag it returned with: return.error's, else 0 */
    struct ex_frame *next;   /* a spare frame's next */
} ex_frame;

static cint_sir_image *g_img;  /* the image whose entries the trampolines run */

/* ------------------------------------------------------------------ patterns */

static const uint32_t TAGS[SC_COUNT] = {0, CINT_TAG_BOOL, CINT_TAG_I8, CINT_TAG_I16, CINT_TAG_I32, CINT_TAG_I64,
                                        CINT_TAG_U8, CINT_TAG_U16, CINT_TAG_U32, CINT_TAG_U64,
                                        CINT_TAG_I128, CINT_TAG_I256, CINT_TAG_I512, CINT_TAG_I1024,
                                        CINT_TAG_U128, CINT_TAG_U256, CINT_TAG_U512};
static const char *const ICHECK[SC_COUNT] = {"index.checked.struct", "index.checked.bool", "index.checked.i8",
                                             "index.checked.i16", "index.checked.i32", "index.checked.i64",
                                             "index.checked.u8", "index.checked.u16", "index.checked.u32",
                                             "index.checked.u64", "index.checked.i128", "index.checked.i256",
                                             "index.checked.i512", "index.checked.i1024", "index.checked.u128",
                                             "index.checked.u256", "index.checked.u512"};
static const char *const SLICE[SC_COUNT] = {"", "slice.checked.bool", "slice.checked.i8", "slice.checked.i16",
                                            "slice.checked.i32", "slice.checked.i64", "slice.checked.u8",
                                            "slice.checked.u16", "slice.checked.u32", "slice.checked.u64",
                                            "slice.checked.i128", "slice.checked.i256", "slice.checked.i512",
                                            "slice.checked.i1024", "slice.checked.u128", "slice.checked.u256",
                                            "slice.checked.u512"};

static unsigned width(int sc)
{
    static const unsigned w[SC_COUNT] = {0, 1, 8, 16, 32, 64, 8, 16, 32, 64, 128, 256, 512, 1024, 128, 256, 512};
    return sc_is_q(sc) ? sc_qwidth(sc) : w[sc];
}

/* The storage integer of a fixed-point scalar (box 15), I8 to I64, or I128 (OQ-292); another
 * scalar itself. */
static int storage(int sc)
{
    unsigned w = width(sc);
    return !sc_is_q(sc) ? sc : w == 128u ? SC_I128 : SC_I8 + (w > 8u) + (w > 16u) + (w > 32u);
}

/* The type word of scalar sc: its tag, or for fixed point CINT_QT of its storage's and its fraction
 * bits (rt/cint_fixed.h), as compiled C passes them (back_c.ci, tag_name). */
static uint32_t tag_of(int sc)
{
    return sc_is_q(sc) ? CINT_QT(TAGS[storage(sc)], sc_qfrac(sc)) : TAGS[sc];
}

/* The operation `<pre>.<t>` (index.checked, slice.checked) of element scalar sc, a fixed-point one's
 * in buf (box 15). */
static const char *check_name(char *buf, const char *const *pre, int sc)
{
    if (!sc_is_q(sc)) return pre[sc];
    snprintf(buf, 32, "%.14sq%u_%u", pre[SC_BOOL], width(sc) - sc_qfrac(sc), sc_qfrac(sc));
    return buf;
}

/* I128 to I1024 and U128 to U512 (box 15), and fixed point of 128 bits as its storage I128
 * (OQ-292): a value in limbs(sc) cells, limb 0 first (rt/cint_wide.h). */
static int is_wide(int sc)
{
    return (sc >= SC_I128 && sc <= SC_U512) || (sc_is_q(sc) && width(sc) == 128u);
}

/* I128 to I1024 and fixed point, in two's complement; U128 to U512 are in binary. */
static bool wsigned(int sc)
{
    return (sc >= SC_I128 && sc <= SC_I1024) || sc_is_q(sc);
}

static unsigned limbs(int sc)
{
    return is_wide(sc) ? width(sc) / 64u : 1u;
}

static int is_signed(int sc)
{
    return (sc >= SC_I8 && sc <= SC_I64) || sc_is_q(sc);
}

/* The pattern of type sc whose low bits are those of b. */
static uint64_t canon(int sc, uint64_t b)
{
    if (sc == SC_BOOL) return b != 0u;
    return is_signed(sc) ? (uint64_t)cint_rt_sext(b, width(sc)) : b & cint_rt_mask(width(sc));
}

static cint_count count_of(int sc, uint64_t b)
{
    switch (sc) {
    case SC_I8: return cint_count_i8((int8_t)cint_rt_sext(b, 64u));
    case SC_I16: return cint_count_i16((int16_t)cint_rt_sext(b, 64u));
    case SC_I32: return cint_count_i32((int32_t)cint_rt_sext(b, 64u));
    case SC_I64: return cint_count_i64(cint_rt_sext(b, 64u));
    case SC_U8: return cint_count_u8((uint8_t)b);
    case SC_U16: return cint_count_u16((uint16_t)b);
    case SC_U32: return cint_count_u32((uint32_t)b);
    default: return cint_count_u64(b);
    }
}

#define EX_TYPES(X, D)                                                                         \
    X(D, SC_I8, i8, int8_t, S) X(D, SC_I16, i16, int16_t, S) X(D, SC_I32, i32, int32_t, S)    \
    X(D, SC_I64, i64, int64_t, S) X(D, SC_U8, u8, uint8_t, U) X(D, SC_U16, u16, uint16_t, U)  \
    X(D, SC_U32, u32, uint32_t, U) X(D, SC_U64, u64, uint64_t, U)
/* The same list, for use inside an expansion of EX_TYPES (a macro does not expand itself). */
#define EX_TYPES_IN(X, D)                                                                      \
    X(D, SC_I8, i8, int8_t, S) X(D, SC_I16, i16, int16_t, S) X(D, SC_I32, i32, int32_t, S)    \
    X(D, SC_I64, i64, int64_t, S) X(D, SC_U8, u8, uint8_t, U) X(D, SC_U16, u16, uint16_t, U)  \
    X(D, SC_U32, u32, uint32_t, U) X(D, SC_U64, u64, uint64_t, U)
#define EX_IN_S(CT, b) ((CT)cint_rt_sext((b), 64u))
#define EX_IN_U(CT, b) ((CT)(b))
#define EX_OUT_S(v) ((uint64_t)(int64_t)(v))
#define EX_OUT_U(v) ((uint64_t)(v))

/* A binary or unary operator, or a built-in that can fault, of type T (the operand's for an
 * operator, the result's for a built-in), through its cint_rt.h helper, k a shift's count. 1, or
 * 0 when it faulted; -1 for a form compiled C has no helper for. */
#define EX_ARITH(D, SC, T, CT, K)                                                              \
    static int arith_##T(cint_ctx *ctx, cint_site s, const si_node *n, const uint64_t *v, cint_count k, \
                         uint64_t *out)                                                        \
    {                                                                                          \
        CT x = EX_IN_##K(CT, v[0]), y = EX_IN_##K(CT, v[1]), r = 0;                            \
        bool ok = true;                                                                        \
        if (n->op == OP_BUILTIN) {                                                             \
            uint32_t aux = n->sub == 15 || n->sub == 16 ? tag_of(n->t3) : n->sub == 11 ? tag_of(n->t2) : 0u; \
            ok = cint_builtin_##T(ctx, s, n->sub, tag_of(n->t1), n->mode, aux, v[0], v[1], v[2], &r); \
        } else if (n->op == OP_UN) {                                                           \
            ok = cint_neg_##T(ctx, s, x, &r);                                                  \
        } else {                                                                               \
            switch (n->sub * 3 + n->form) {                                                    \
            case BO_ADD * 3 + FM_CHECKED: ok = cint_add_##T(ctx, s, x, y, &r); break;          \
            case BO_SUB * 3 + FM_CHECKED: ok = cint_sub_##T(ctx, s, x, y, &r); break;          \
            case BO_MUL * 3 + FM_CHECKED: ok = cint_mul_##T(ctx, s, x, y, &r); break;          \
            case BO_DIV * 3 + FM_CHECKED: ok = cint_div_##T(ctx, s, x, y, &r); break;          \
            case BO_REM * 3 + FM_CHECKED: ok = cint_rem_##T(ctx, s, x, y, &r); break;          \
            case BO_ADD * 3 + FM_WRAP: r = cint_add_wrap_##T(x, y); break;                     \
            case BO_SUB * 3 + FM_WRAP: r = cint_sub_wrap_##T(x, y); break;                     \
            case BO_MUL * 3 + FM_WRAP: r = cint_mul_wrap_##T(x, y); break;                     \
            case BO_ADD * 3 + FM_SAT: r = cint_add_sat_##T(x, y); break;                       \
            case BO_SUB * 3 + FM_SAT: r = cint_sub_sat_##T(x, y); break;                       \
            case BO_MUL * 3 + FM_SAT: r = cint_mul_sat_##T(x, y); break;                       \
            case BO_SHL * 3 + FM_CHECKED: ok = cint_shl_##T(ctx, s, x, k, &r); break;          \
            case BO_SHL * 3 + FM_WRAP: ok = cint_shl_wrap_##T(ctx, s, x, k, &r); break;        \
            case BO_SHR * 3 + FM_CHECKED: ok = cint_shr_##T(ctx, s, x, k, &r); break;          \
            default: return -1;                                                                \
            }                                                                                  \
        }                                                                                      \
        *out = EX_OUT_##K(r);                                                                  \
        return ok;                                                                             \
    }
EX_TYPES(EX_ARITH, 0)

/* as.checked, as.wrap and as.sat from type F to type D (cint_as_<D>_from_<F>); wrap is the form,
 * FM_WRAP for as.result. */
#define EX_FROM(D, SC, T, CT, K)                                                               \
    case SC:                                                                                   \
        if (wrap == FM_SAT) {                                                                  \
            r = cint_as_sat_##D##_from_##T(EX_IN_##K(CT, v));                                  \
        } else if (wrap) {                                                                     \
            r = cint_as_wrap_##D##_from_##T(EX_IN_##K(CT, v));                                 \
        } else {                                                                               \
            ok = cint_as_##D##_from_##T(ctx, s, EX_IN_##K(CT, v), &r);                         \
        }                                                                                      \
        break;
#define EX_CONV(D0, SC, D, CD, K)                                                              \
    static int conv_##D(cint_ctx *ctx, cint_site s, int from, int wrap, uint64_t v, uint64_t *out) \
    {                                                                                          \
        CD r = 0;                                                                              \
        bool ok = true;                                                                        \
        switch (from) {                                                                        \
            EX_TYPES_IN(EX_FROM, D)                                                             \
        default:                                                                               \
            r = (CD)(v != 0u ? 1 : 0); /* from Bool, as compiled C writes it */                \
        }                                                                                      \
        *out = EX_OUT_##K(r);                                                                  \
        return ok;                                                                             \
    }
EX_TYPES(EX_CONV, 0)

/* `<op>.result.<t>` of box 12 through cint_<op>_result_<t>: the ArithError tag, and the value
 * when it is 0 (cint_rt.h 10a). */
#define EX_RESULT(D, SC, T, CT, K)                                                             \
    static uint64_t result_##T(const si_node *n, const uint64_t *v, cint_count k, uint64_t *out) \
    {                                                                                          \
        CT x = EX_IN_##K(CT, v[0]), y = EX_IN_##K(CT, v[1]), r = 0;                            \
        uint16_t e = n->sub == BO_ADD   ? cint_add_result_##T(x, y, &r)                        \
                     : n->sub == BO_SUB ? cint_sub_result_##T(x, y, &r)                        \
                     : n->sub == BO_MUL ? cint_mul_result_##T(x, y, &r)                        \
                     : n->sub == BO_DIV ? cint_div_result_##T(x, y, &r)                        \
                     : n->sub == BO_REM ? cint_rem_result_##T(x, y, &r)                        \
                                        : cint_shl_result_##T(x, k, &r);                       \
        *out = EX_OUT_##K(r);                                                                  \
        return e;                                                                              \
    }
EX_TYPES(EX_RESULT, 0)

/* The built-ins that never fault, by operand type (cint_rt.h section 13). */
static uint64_t pure_builtin(int id, int sc, uint64_t a, uint64_t b)
{
    int64_t x = cint_rt_sext(a, 64u), y = cint_rt_sext(b, 64u);
    switch (id * 16 + sc) {
    case 2 * 16 + SC_I8: return cint_uabs_i8((int8_t)x);
    case 2 * 16 + SC_I16: return cint_uabs_i16((int16_t)x);
    case 2 * 16 + SC_I32: return cint_uabs_i32((int32_t)x);
    case 2 * 16 + SC_I64: return cint_uabs_i64(x);
    case 12 * 16 + SC_I8: return EX_OUT_S(cint_mul_full_i8((int8_t)x, (int8_t)y));
    case 12 * 16 + SC_I16: return EX_OUT_S(cint_mul_full_i16((int16_t)x, (int16_t)y));
    case 12 * 16 + SC_I32: return EX_OUT_S(cint_mul_full_i32((int32_t)x, (int32_t)y));
    case 12 * 16 + SC_U8: return cint_mul_full_u8((uint8_t)a, (uint8_t)b);
    case 12 * 16 + SC_U16: return cint_mul_full_u16((uint16_t)a, (uint16_t)b);
    case 12 * 16 + SC_U32: return cint_mul_full_u32((uint32_t)a, (uint32_t)b);
    default: break;
    }
    int max = id == 4;
    switch (sc) {
    case SC_I8: return EX_OUT_S(max ? cint_max_i8((int8_t)x, (int8_t)y) : cint_min_i8((int8_t)x, (int8_t)y));
    case SC_I16: return EX_OUT_S(max ? cint_max_i16((int16_t)x, (int16_t)y) : cint_min_i16((int16_t)x, (int16_t)y));
    case SC_I32: return EX_OUT_S(max ? cint_max_i32((int32_t)x, (int32_t)y) : cint_min_i32((int32_t)x, (int32_t)y));
    case SC_I64: return EX_OUT_S(max ? cint_max_i64(x, y) : cint_min_i64(x, y));
    case SC_U8: return max ? cint_max_u8((uint8_t)a, (uint8_t)b) : cint_min_u8((uint8_t)a, (uint8_t)b);
    case SC_U16: return max ? cint_max_u16((uint16_t)a, (uint16_t)b) : cint_min_u16((uint16_t)a, (uint16_t)b);
    case SC_U32: return max ? cint_max_u32((uint32_t)a, (uint32_t)b) : cint_min_u32((uint32_t)a, (uint32_t)b);
    default: return max ? cint_max_u64(a, b) : cint_min_u64(a, b);
    }
}

static int arith(cint_ctx *ctx, cint_site s, const si_node *n, const uint64_t *v, cint_count k, uint64_t *out)
{
    int sc = n->op == OP_BUILTIN ? n->t2 : n->t1;
    switch (sc) {
    case SC_I8: return arith_i8(ctx, s, n, v, k, out);
    case SC_I16: return arith_i16(ctx, s, n, v, k, out);
    case SC_I32: return arith_i32(ctx, s, n, v, k, out);
    case SC_I64: return arith_i64(ctx, s, n, v, k, out);
    case SC_U8: return arith_u8(ctx, s, n, v, k, out);
    case SC_U16: return arith_u16(ctx, s, n, v, k, out);
    case SC_U32: return arith_u32(ctx, s, n, v, k, out);
    default: return arith_u64(ctx, s, n, v, k, out);
    }
}

static int conv(cint_ctx *ctx, cint_site s, const si_node *n, uint64_t v, uint64_t *out)
{
    int from = n->t1 == SC_BOOL ? 0 : n->t1, wrap = n->form == FM_RESULT ? FM_WRAP : n->form;   /* as.result wraps */
    switch (n->t2) {
    case SC_I8: return conv_i8(ctx, s, from, wrap, v, out);
    case SC_I16: return conv_i16(ctx, s, from, wrap, v, out);
    case SC_I32: return conv_i32(ctx, s, from, wrap, v, out);
    case SC_I64: return conv_i64(ctx, s, from, wrap, v, out);
    case SC_U8: return conv_u8(ctx, s, from, wrap, v, out);
    case SC_U16: return conv_u16(ctx, s, from, wrap, v, out);
    case SC_U32: return conv_u32(ctx, s, from, wrap, v, out);
    default: return conv_u64(ctx, s, from, wrap, v, out);
    }
}

/* A result form of box 12: `as.result` (`as?`), which wraps, with ArithError.narrow (4) when the
 * value does not fit, as compiled C tests it (cint_rt_fits); or `<op>.result.<t>`. The tag. */
static uint64_t result_form(cint_ctx *ctx, cint_site s, const si_node *n, const uint64_t *v, cint_count k,
                            uint64_t *out)
{
    if (n->op == OP_AS) {
        (void)conv(ctx, s, n, v[0], out);
        return cint_rt_fits(v[0], is_signed(n->t1), tag_of(n->t2)) ? 0u : CINT_ARITH_NARROW;
    }
    switch (n->t1) {
    case SC_I8: return result_i8(n, v, k, out);
    case SC_I16: return result_i16(n, v, k, out);
    case SC_I32: return result_i32(n, v, k, out);
    case SC_I64: return result_i64(n, v, k, out);
    case SC_U8: return result_u8(n, v, k, out);
    case SC_U16: return result_u16(n, v, k, out);
    case SC_U32: return result_u32(n, v, k, out);
    default: return result_u64(n, v, k, out);
    }
}

static int compare(const si_node *n, uint64_t a, uint64_t b)
{
    int lt = is_signed(n->t1) ? cint_rt_sext(a, 64u) < cint_rt_sext(b, 64u) : a < b;
    int gt = is_signed(n->t1) ? cint_rt_sext(a, 64u) > cint_rt_sext(b, 64u) : a > b;
    switch (n->sub) {
    case CM_EQ: return a == b;
    case CM_NE: return a != b;
    case CM_LT: return lt;
    case CM_LE: return !gt;
    case CM_GT: return gt;
    default: return !lt;
    }
}

/* ------------------------------------------------------------------ fixed point */

/* A fixed-point operator or built-in (box 15; SPEC-01 IM-60 to IM-67) whose result has storage T:
 * through its rt/cint_fixed.h helper, and `+%`, `+|`, `-%`, `-|`, `min` and `max` through the
 * storage integer's (IM-60), as compiled C calls them (back_c.ci, c_fixed); a result form's tag
 * into *etag (OQ-273, OQ-278). 1, or 0 when it faulted. */
#define EX_QARITH(T, CT)                                                                       \
    static int qarith_##T(cint_ctx *ctx, cint_site s, const si_node *n, const uint64_t *v, uint64_t *out, \
                          uint64_t *etag)                                                      \
    {                                                                                          \
        CT x = (CT)cint_rt_sext(v[0], 64u), y = (CT)cint_rt_sext(v[1], 64u), r = 0;            \
        unsigned f = sc_qfrac(n->op == OP_BUILTIN ? n->t2 : n->t1);                            \
        bool ok = true;                                                                        \
        if (n->op == OP_BIN && n->form == FM_RESULT) {                                         \
            *etag = n->sub == BO_ADD   ? cint_add_result_##T(x, y, &r)                         \
                    : n->sub == BO_SUB ? cint_sub_result_##T(x, y, &r)                         \
                    : n->sub == BO_MUL ? cint_q_mul_result_##T(f, n->mode, x, y, &r)           \
                                       : cint_q_div_result_##T(f, n->mode, x, y, &r);          \
            *out = EX_OUT_S(r);                                                                \
            return 1;                                                                          \
        }                                                                                      \
        switch (n->op == OP_BUILTIN ? 64 + n->sub : n->op == OP_UN ? 63 : n->sub * 3 + n->form) { \
        case 63: ok = cint_q_neg_##T(ctx, s, f, x, &r); break;                                 \
        case 64 + 1: ok = cint_q_abs_##T(ctx, s, f, x, &r); break;                             \
        case 64 + 3: r = cint_min_##T(x, y); break;                                            \
        case 64 + 4: r = cint_max_##T(x, y); break;                                            \
        case 64 + BI_SQRT: ok = cint_q_sqrt_##T(ctx, s, f, n->mode, x, &r); break;             \
        case 64 + BI_QMUL:                                                                     \
            ok = cint_qmul_##T(ctx, s, tag_of(n->t1), tag_of(n->t3), f, n->mode, cint_rt_sext(v[0], 64u), \
                               cint_rt_sext(v[1], 64u), &r);                                   \
            break;                                                                             \
        case 64 + BI_MUL_INT: ok = cint_q_mul_int_##T(ctx, s, f, tag_of(n->t3), x, v[1], &r); break; \
        case BO_ADD * 3 + FM_CHECKED: ok = cint_q_add_##T(ctx, s, f, x, y, &r); break;         \
        case BO_SUB * 3 + FM_CHECKED: ok = cint_q_sub_##T(ctx, s, f, x, y, &r); break;         \
        case BO_MUL * 3 + FM_CHECKED: ok = cint_q_mul_##T(ctx, s, f, n->mode, x, y, &r); break; \
        case BO_DIV * 3 + FM_CHECKED: ok = cint_q_div_##T(ctx, s, f, n->mode, x, y, &r); break; \
        case BO_ADD * 3 + FM_WRAP: r = cint_add_wrap_##T(x, y); break;                         \
        case BO_SUB * 3 + FM_WRAP: r = cint_sub_wrap_##T(x, y); break;                         \
        case BO_MUL * 3 + FM_WRAP: r = cint_q_mul_wrap_##T(f, n->mode, x, y); break;           \
        case BO_ADD * 3 + FM_SAT: r = cint_add_sat_##T(x, y); break;                           \
        case BO_SUB * 3 + FM_SAT: r = cint_sub_sat_##T(x, y); break;                           \
        default: r = cint_q_mul_sat_##T(f, n->mode, x, y); break;                              \
        }                                                                                      \
        *out = EX_OUT_S(r);                                                                    \
        return ok;                                                                             \
    }
EX_QARITH(i8, int8_t)
EX_QARITH(i16, int16_t)
EX_QARITH(i32, int32_t)
EX_QARITH(i64, int64_t)

/* A conversion with fixed point on one side (SPEC-01 5.6) into R, the target or its storage:
 * as.checked, as.wrap, as.sat and as.result through cint_q_as_<R>, cint_q_as_wrap_<R>,
 * cint_q_as_sat_<R> and cint_q_as_result_<R> on the source's 64-bit pattern. */
#define EX_QCONV(D, SC, T, CT, K)                                                              \
    case SC: {                                                                                 \
        CT r = 0;                                                                              \
        if (n->form == FM_CHECKED) {                                                           \
            ok = cint_q_as_##T(ctx, s, tag_of(n->t1), tag_of(n->t2), n->mode, v[0], &r);       \
        } else if (n->form == FM_WRAP) {                                                       \
            r = cint_q_as_wrap_##T(tag_of(n->t1), tag_of(n->t2), n->mode, v[0]);               \
        } else if (n->form == FM_SAT) {                                                        \
            r = cint_q_as_sat_##T(tag_of(n->t1), tag_of(n->t2), n->mode, v[0]);                \
        } else {                                                                               \
            *etag = cint_q_as_result_##T(tag_of(n->t1), tag_of(n->t2), n->mode, v[0], &r);    \
        }                                                                                      \
        *out = EX_OUT_##K(r);                                                                  \
        break;                                                                                 \
    }

/* A node on fixed point; as.raw is the same bits. 1, or 0 when it faulted; a result form's tag
 * into *etag. */
static int qnode(cint_ctx *ctx, cint_site s, const si_node *n, const uint64_t *v, uint64_t *out, uint64_t *etag)
{
    bool ok = true;
    if (n->op != OP_AS) {
        switch (width(n->op == OP_BUILTIN ? n->t2 : n->t1)) {
        case 8: return qarith_i8(ctx, s, n, v, out, etag);
        case 16: return qarith_i16(ctx, s, n, v, out, etag);
        case 32: return qarith_i32(ctx, s, n, v, out, etag);
        default: return qarith_i64(ctx, s, n, v, out, etag);
        }
    }
    *out = v[0];
    switch (n->form == FM_RAW ? SC_NONE : storage(n->t2)) {
        EX_TYPES(EX_QCONV, 0)
    default:
        break;
    }
    return ok;
}

/* ------------------------------------------------------------------ wide integers */

/* I128 to I1024 and U128 to U512 (box 15): a wide value is its limbs in cells, which the
 * rt/cint_wide.h helper of each node reads and writes as the C type cint_<t> (a struct of those
 * limbs), so that a fault record is the one compiled C writes (BX13-03, BX15-23). */
#define EX_WIDES(X, D)                                                                         \
    X(D, SC_I128, i128) X(D, SC_I256, i256) X(D, SC_I512, i512) X(D, SC_I1024, i1024)          \
    X(D, SC_U128, u128) X(D, SC_U256, u256) X(D, SC_U512, u512)
#define EX_WIDES_IN(X, D)                                                                      \
    X(D, SC_I128, i128) X(D, SC_I256, i256) X(D, SC_I512, i512) X(D, SC_I1024, i1024)          \
    X(D, SC_U128, u128) X(D, SC_U256, u256) X(D, SC_U512, u512)
#define EX_WP(T, p) ((cint_##T *)(void *)(p))
#define EX_WC(T, p) ((const cint_##T *)(const void *)(p))

/* An operator, or a built-in other than mul_full, of wide operand type T (v its operands' limbs,
 * k a shift's count): its result into out, a cint_<T> or, for a muldiv of narrow result, the C
 * object of that type; a result form's tag into *etag. 1, or 0 when it faulted. */
#define EX_WARITH(D, SC, T)                                                                    \
    static int warith_##T(cint_ctx *ctx, cint_site s, const si_node *n, const uint64_t *const *v, cint_count k, \
                          void *out, uint64_t *etag)                                           \
    {                                                                                          \
        const cint_##T *a = EX_WC(T, v[0]), *b = EX_WC(T, v[1]);                               \
        cint_##T *r = EX_WP(T, out);                                                           \
        bool ok = true;                                                                        \
        if (n->op == OP_BUILTIN && (n->sub == 3 || n->sub == 4)) {                             \
            if (n->sub == 3) {                                                                 \
                cint_min_##T(a, b, r);                                                         \
            } else {                                                                           \
                cint_max_##T(a, b, r);                                                         \
            }                                                                                  \
        } else if (n->op == OP_BUILTIN && n->sub == 11) {                                      \
            ok = cint_muldiv_##T(ctx, s, tag_of(n->t2), n->mode, a, b, EX_WC(T, v[2]), out);      \
        } else if (n->op == OP_BUILTIN) {                                                      \
            ok = cint_builtin_##T(ctx, s, n->sub, n->mode, a, b, EX_WC(T, v[2]), r);           \
        } else if (n->op == OP_UN) {                                                           \
            ok = cint_neg_##T(ctx, s, a, r);                                                   \
        } else if (n->form == FM_RESULT) {                                                     \
            *etag = n->sub == BO_ADD   ? cint_add_result_##T(a, b, r)                          \
                    : n->sub == BO_SUB ? cint_sub_result_##T(a, b, r)                          \
                    : n->sub == BO_MUL ? cint_mul_result_##T(a, b, r)                          \
                    : n->sub == BO_DIV ? cint_div_result_##T(a, b, r)                          \
                    : n->sub == BO_REM ? cint_rem_result_##T(a, b, r)                          \
                                       : cint_shl_result_##T(a, k, r);                         \
        } else {                                                                               \
            switch (n->sub * 3 + n->form) {                                                    \
            case BO_ADD * 3 + FM_CHECKED: ok = cint_add_##T(ctx, s, a, b, r); break;           \
            case BO_SUB * 3 + FM_CHECKED: ok = cint_sub_##T(ctx, s, a, b, r); break;           \
            case BO_MUL * 3 + FM_CHECKED: ok = cint_mul_##T(ctx, s, a, b, r); break;           \
            case BO_DIV * 3 + FM_CHECKED: ok = cint_div_##T(ctx, s, a, b, r); break;           \
            case BO_REM * 3 + FM_CHECKED: ok = cint_rem_##T(ctx, s, a, b, r); break;           \
            case BO_ADD * 3 + FM_WRAP: cint_add_wrap_##T(a, b, r); break;                      \
            case BO_SUB * 3 + FM_WRAP: cint_sub_wrap_##T(a, b, r); break;                      \
            case BO_MUL * 3 + FM_WRAP: cint_mul_wrap_##T(a, b, r); break;                      \
            case BO_ADD * 3 + FM_SAT: cint_add_sat_##T(a, b, r); break;                        \
            case BO_SUB * 3 + FM_SAT: cint_sub_sat_##T(a, b, r); break;                        \
            case BO_MUL * 3 + FM_SAT: cint_mul_sat_##T(a, b, r); break;                        \
            case BO_SHL * 3 + FM_CHECKED: ok = cint_shl_##T(ctx, s, a, k, r); break;           \
            case BO_SHL * 3 + FM_WRAP: ok = cint_shl_wrap_##T(ctx, s, a, k, r); break;         \
            default: ok = cint_shr_##T(ctx, s, a, k, r); break;                                \
            }                                                                                  \
        }                                                                                      \
        return ok;                                                                             \
    }
EX_WIDES(EX_WARITH, 0)

/* as.checked, as.wrap and as.sat into wide type D: from narrow type T (its pattern b), from wide
 * type T (its limbs x), or from Bool. */
#define EX_WFROM_N(D, SC, T, CT, K)                                                            \
    case SC:                                                                                   \
        if (wrap == FM_SAT) {                                                                  \
            cint_as_sat_##D##_from_##T(EX_IN_##K(CT, b), EX_WP(D, out));                       \
        } else if (wrap) {                                                                     \
            cint_as_wrap_##D##_from_##T(EX_IN_##K(CT, b), EX_WP(D, out));                      \
        } else {                                                                               \
            ok = cint_as_##D##_from_##T(ctx, s, EX_IN_##K(CT, b), EX_WP(D, out));              \
        }                                                                                      \
        break;
#define EX_WFROM_W(D, SC, T)                                                                   \
    case SC:                                                                                   \
        if (wrap == FM_SAT) {                                                                  \
            cint_as_sat_##D##_from_##T(EX_WC(T, x), EX_WP(D, out));                            \
        } else if (wrap) {                                                                     \
            cint_as_wrap_##D##_from_##T(EX_WC(T, x), EX_WP(D, out));                           \
        } else {                                                                               \
            ok = cint_as_##D##_from_##T(ctx, s, EX_WC(T, x), EX_WP(D, out));                   \
        }                                                                                      \
        break;
#define EX_WCONV(D0, SC, D)                                                                    \
    static int wconv_##D(cint_ctx *ctx, cint_site s, int from, int wrap, uint64_t b, const uint64_t *x, \
                         uint64_t *out)                                                        \
    {                                                                                          \
        bool ok = true;                                                                        \
        switch (from) {                                                                        \
            EX_TYPES_IN(EX_WFROM_N, D)                                                          \
            EX_WIDES_IN(EX_WFROM_W, D)                                                          \
        default:   /* from Bool, as compiled C writes it */                                    \
            memset(out, 0, sizeof(cint_##D));                                                  \
            out[0] = b != 0u;                                                                  \
        }                                                                                      \
        return ok;                                                                             \
    }
EX_WIDES(EX_WCONV, 0)

/* as.checked, as.wrap and as.sat from wide type F into narrow type T, its pattern into *out. */
#define EX_WTO_N(F, SC, T, CT, K)                                                              \
    case SC: {                                                                                 \
        CT r = 0;                                                                              \
        if (wrap == FM_SAT) {                                                                  \
            r = cint_as_sat_##T##_from_##F(EX_WC(F, x));                                       \
        } else if (wrap) {                                                                     \
            r = cint_as_wrap_##T##_from_##F(EX_WC(F, x));                                      \
        } else {                                                                               \
            ok = cint_as_##T##_from_##F(ctx, s, EX_WC(F, x), &r);                              \
        }                                                                                      \
        *out = EX_OUT_##K(r);                                                                  \
        break;                                                                                 \
    }
#define EX_WNARROW(D0, SC, F)                                                                  \
    static int wnarrow_##F(cint_ctx *ctx, cint_site s, int to, int wrap, const uint64_t *x, uint64_t *out) \
    {                                                                                          \
        bool ok = true;                                                                        \
        switch (to) {                                                                          \
            EX_TYPES_IN(EX_WTO_N, F)                                                            \
        default:                                                                               \
            break;                                                                             \
        }                                                                                      \
        return ok;                                                                             \
    }
EX_WIDES(EX_WNARROW, 0)

static int warith(cint_ctx *ctx, cint_site s, const si_node *n, const uint64_t *const *v, cint_count k, void *out,
                  uint64_t *etag)
{
    switch (n->t1) {
    case SC_I128: return warith_i128(ctx, s, n, v, k, out, etag);
    case SC_I256: return warith_i256(ctx, s, n, v, k, out, etag);
    case SC_I512: return warith_i512(ctx, s, n, v, k, out, etag);
    case SC_I1024: return warith_i1024(ctx, s, n, v, k, out, etag);
    case SC_U128: return warith_u128(ctx, s, n, v, k, out, etag);
    case SC_U256: return warith_u256(ctx, s, n, v, k, out, etag);
    default: return warith_u512(ctx, s, n, v, k, out, etag);
    }
}

/* A conversion with a wide side: into wide out, or a narrow pattern into *out. as.result wraps,
 * its tag ArithError.narrow (4) when the value does not fit (cint_w_fits_into, cint_w_fits_narrow;
 * a narrow value into U128 to U512 when it is negative). */
static int wconv(cint_ctx *ctx, cint_site s, const si_node *n, uint64_t b, const uint64_t *x, uint64_t *out,
                 uint64_t *etag)
{
    int from = n->t1 == SC_BOOL ? 0 : n->t1, wrap = n->form == FM_RESULT ? FM_WRAP : n->form, ok;
    switch (is_wide(n->t2) ? n->t2 : SC_NONE) {
    case SC_I128: ok = wconv_i128(ctx, s, from, wrap, b, x, out); break;
    case SC_I256: ok = wconv_i256(ctx, s, from, wrap, b, x, out); break;
    case SC_I512: ok = wconv_i512(ctx, s, from, wrap, b, x, out); break;
    case SC_I1024: ok = wconv_i1024(ctx, s, from, wrap, b, x, out); break;
    case SC_U128: ok = wconv_u128(ctx, s, from, wrap, b, x, out); break;
    case SC_U256: ok = wconv_u256(ctx, s, from, wrap, b, x, out); break;
    case SC_U512: ok = wconv_u512(ctx, s, from, wrap, b, x, out); break;
    default:
        switch (n->t1) {
        case SC_I128: ok = wnarrow_i128(ctx, s, n->t2, wrap, x, out); break;
        case SC_I256: ok = wnarrow_i256(ctx, s, n->t2, wrap, x, out); break;
        case SC_I512: ok = wnarrow_i512(ctx, s, n->t2, wrap, x, out); break;
        case SC_I1024: ok = wnarrow_i1024(ctx, s, n->t2, wrap, x, out); break;
        case SC_U128: ok = wnarrow_u128(ctx, s, n->t2, wrap, x, out); break;
        case SC_U256: ok = wnarrow_u256(ctx, s, n->t2, wrap, x, out); break;
        default: ok = wnarrow_u512(ctx, s, n->t2, wrap, x, out); break;
        }
    }
    if (n->form == FM_RESULT) {
        int fits = !is_wide(n->t1)  ? wsigned(n->t2) || !is_signed(n->t1) || (int64_t)b >= 0
                   : is_wide(n->t2) ? cint_w_fits_into(x, limbs(n->t1), wsigned(n->t1), limbs(n->t2), wsigned(n->t2))
                                    : cint_w_fits_narrow(x, limbs(n->t1), wsigned(n->t1), tag_of(n->t2));
        *etag = fits ? 0u : CINT_ARITH_NARROW;
    }
    return ok;
}

/* mul_full into the wide type of twice the width: from I64, I128, I256, I512, U64, U128 or U256. */
static void wmul_full(const si_node *n, const uint64_t *const *v, const uint64_t *low, uint64_t *out)
{
    switch (n->t1) {
    case SC_I64: cint_mul_full_i64(cint_rt_sext(low[0], 64u), cint_rt_sext(low[1], 64u), EX_WP(i128, out)); break;
    case SC_I128: cint_mul_full_i128(EX_WC(i128, v[0]), EX_WC(i128, v[1]), EX_WP(i256, out)); break;
    case SC_I256: cint_mul_full_i256(EX_WC(i256, v[0]), EX_WC(i256, v[1]), EX_WP(i512, out)); break;
    case SC_I512: cint_mul_full_i512(EX_WC(i512, v[0]), EX_WC(i512, v[1]), EX_WP(i1024, out)); break;
    case SC_U64: cint_mul_full_u64(low[0], low[1], EX_WP(u128, out)); break;
    case SC_U128: cint_mul_full_u128(EX_WC(u128, v[0]), EX_WC(u128, v[1]), EX_WP(u256, out)); break;
    default: cint_mul_full_u256(EX_WC(u256, v[0]), EX_WC(u256, v[1]), EX_WP(u512, out)); break;
    }
}

/* uabs.checked of I128 to I512 into the unsigned type of the width, which never faults. */
static void wuabs(const si_node *n, const uint64_t *a, uint64_t *out)
{
    switch (n->t1) {
    case SC_I128: cint_uabs_i128(EX_WC(i128, a), EX_WP(u128, out)); break;
    case SC_I256: cint_uabs_i256(EX_WC(i256, a), EX_WP(u256, out)); break;
    default: cint_uabs_i512(EX_WC(i512, a), EX_WP(u512, out)); break;
    }
}

/* rotl.checked and rotr.checked of U128 to U512 by count k, a narrow one (built-ins 15 and 16). 1,
 * or 0 when it faulted. */
static int wrotate(cint_ctx *ctx, cint_site s, const si_node *n, const uint64_t *a, cint_count k, uint64_t *out)
{
    int left = n->sub == 15;
    switch (n->t1) {
    case SC_U128:
        return left ? cint_rotl_u128(ctx, s, EX_WC(u128, a), k, EX_WP(u128, out))
                    : cint_rotr_u128(ctx, s, EX_WC(u128, a), k, EX_WP(u128, out));
    case SC_U256:
        return left ? cint_rotl_u256(ctx, s, EX_WC(u256, a), k, EX_WP(u256, out))
                    : cint_rotr_u256(ctx, s, EX_WC(u256, a), k, EX_WP(u256, out));
    default:
        return left ? cint_rotl_u512(ctx, s, EX_WC(u512, a), k, EX_WP(u512, out))
                    : cint_rotr_u512(ctx, s, EX_WC(u512, a), k, EX_WP(u512, out));
    }
}

/* The C object of a narrow result that a wide helper writes: a muldiv's, or a reduction's of wide
 * elements (box 15). */
typedef union ex_obj {
    int8_t i8;
    int16_t i16;
    int32_t i32;
    int64_t i64;
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
    uint64_t u64;
} ex_obj;

/* The pattern of the C object of narrow type sc at p (an ex_obj). */
static uint64_t narrow_obj(int sc, const void *p)
{
    switch (sc) {
    case SC_I8: return EX_OUT_S(*(const int8_t *)p);
    case SC_I16: return EX_OUT_S(*(const int16_t *)p);
    case SC_I32: return EX_OUT_S(*(const int32_t *)p);
    case SC_I64: return EX_OUT_S(*(const int64_t *)p);
    case SC_U8: return *(const uint8_t *)p;
    case SC_U16: return *(const uint16_t *)p;
    case SC_U32: return *(const uint32_t *)p;
    default: return *(const uint64_t *)p;
    }
}

/* ------------------------------------------------------------------ module state */

static uint64_t state_load(const uint8_t *b, int sc)
{
    unsigned n = sc == SC_BOOL ? 1u : width(sc) / 8u;
    uint64_t v = 0;
    for (unsigned k = n; k-- > 0;) v = v << 8 | b[k];
    return canon(sc, v);
}

static void state_store(uint8_t *b, int sc, uint64_t v)
{
    unsigned n = sc == SC_BOOL ? 1u : width(sc) / 8u;
    for (unsigned k = 0; k < n; k++) b[k] = (uint8_t)(v >> (8u * k));
}

/* The bytes of one element of a module-level array of scalar sc in the block. */
static unsigned elem_bytes(int sc)
{
    return sc == SC_BOOL ? 1u : width(sc) / 8u;
}

/* Before a function of module mi that names a module-level array or struct runs: their cells
 * from the module's block, once per entry, as compiled C takes the block at the start of a
 * function that touches state (back_c.ci, uses_state); a struct's are its block's, 8 bytes each
 * (prepare_mod). 0 when the runtime faulted the entry. */
static int arrays_in(cint_sir_image *x, cint_ctx *ctx, uint32_t mi)
{
    ex_mod *em = &x->mods[mi];
    const si_module *m = &x->p->mods[mi];
    const uint8_t *b;
    if (em->synced) return 1;
    if ((b = cint_rt_state(ctx, &em->state)) == NULL) return 0;
    for (uint32_t g = 0; g < m->nglobal; g++) {
        unsigned w = elem_bytes(m->globals[g].scalar), q = limbs(m->globals[g].scalar);
        for (int64_t i = 0; q > 1u && i < m->globals[g].extent; i++)   /* a wide element's limbs (box 15) */
            cint_wide_load(em->garr[g].base + (size_t)i * q, b + em->vars[g].offset + (size_t)i * w, q);
        for (int64_t i = 0; q == 1u && i < m->globals[g].extent; i++)
            em->garr[g].base[i] = state_load(b + em->vars[g].offset + (size_t)i * w, m->globals[g].scalar);
        for (uint32_t i = 0; i < m->globals[g].cells; i++)
            em->garr[g].base[i] = state_load(b + em->vars[g].offset + (size_t)i * 8u, SC_U64);
    }
    em->synced = 1;
    return 1;
}

/* The cells of each module's arrays and structs the entry read back into the module's block: when the entry
 * ends (end 1), and at a dispatch boundary (end 0), whose state hash compiled C, which stores
 * into the block, also takes with every element written so far (SPEC-06 1.5). */
static void arrays_out(cint_sir_image *x, cint_ctx *ctx, int end)
{
    for (uint32_t mi = 0; mi < x->p->nmod; mi++) {
        ex_mod *em = &x->mods[mi];
        const si_module *m = &x->p->mods[mi];
        uint8_t *b = em->synced ? (uint8_t *)cint_rt_state(ctx, &em->state) : NULL;
        for (uint32_t g = 0; b != NULL && g < m->nglobal; g++) {
            unsigned w = elem_bytes(m->globals[g].scalar), q = limbs(m->globals[g].scalar);
            for (int64_t i = 0; q > 1u && i < m->globals[g].extent; i++)
                cint_wide_store(b + em->vars[g].offset + (size_t)i * w, em->garr[g].base + (size_t)i * q, q);
            for (int64_t i = 0; q == 1u && i < m->globals[g].extent; i++)
                state_store(b + em->vars[g].offset + (size_t)i * w, m->globals[g].scalar, em->garr[g].base[i]);
            for (uint32_t i = 0; i < m->globals[g].cells; i++)
                state_store(b + em->vars[g].offset + (size_t)i * 8u, SC_U64, em->garr[g].base[i]);
        }
        em->synced = end ? 0u : em->synced;
    }
}

/* ------------------------------------------------------------------ execution */

static uint64_t vget(const ex_frame *fr, const si_opd *o)
{
    return o->kind == OD_TEMP ? fr->t[o->index] : *fr->par[o->index].base;
}

/* The limbs of wide value operand o (box 15): a temporary's, or a parameter's cells. */
static uint64_t *wget(const ex_frame *fr, const si_opd *o)
{
    return o->kind == OD_TEMP ? fr->w + fr->wat[o->index] : fr->par[o->index].base;
}

/* The count of a shift, operand o of type n->t3: a wide one points at its limbs, which outlive
 * the helper that takes it (rt/cint_wide.h, cint_count_<t>; RT-OQ-52). */
static cint_count count_at(const ex_frame *fr, const si_node *n, const si_opd *o)
{
    switch (n->t3) {
    case SC_I128: return cint_count_i128(EX_WC(i128, wget(fr, o)));
    case SC_I256: return cint_count_i256(EX_WC(i256, wget(fr, o)));
    case SC_I512: return cint_count_i512(EX_WC(i512, wget(fr, o)));
    case SC_I1024: return cint_count_i1024(EX_WC(i1024, wget(fr, o)));
    case SC_U128: return cint_count_u128(EX_WC(u128, wget(fr, o)));
    case SC_U256: return cint_count_u256(EX_WC(u256, wget(fr, o)));
    case SC_U512: return cint_count_u512(EX_WC(u512, wget(fr, o)));
    default: return count_of(n->t3, vget(fr, o));
    }
}

static ex_ref place(const cint_sir_image *x, const ex_frame *fr, const si_opd *o)
{
    switch (o->kind) {
    case OD_SLOT: return fr->slot[o->index];
    case OD_PARAM: return fr->par[o->index];
    case OD_GLOBAL: return x->mods[fr->mi].garr[o->index];
    default: return x->mods[fr->mi].data[o->index];
    }
}

/* The view a node changes in place: a slot's, or a parameter's; or the place a node only
 * reads, which may also be a module-level array's (the reader lets no node change that). */
static ex_ref *view_of(cint_sir_image *x, ex_frame *fr, const si_opd *o)
{
    return o->kind == OD_SLOT ? &fr->slot[o->index] : o->kind == OD_GLOBAL ? &x->mods[fr->mi].garr[o->index]
                                                                           : &fr->par[o->index];
}

/* The cells of handle operand o (box 12): a temporary's own, or a parameter's. */
static uint64_t *hcells(const ex_frame *fr, const si_opd *o)
{
    return o->kind == OD_TEMP ? fr->h + HCELLS * o->index : fr->par[o->index].base;
}

static ex_ref ref1(uint64_t *base, int64_t extent)
{
    ex_ref a;
    memset(&a, 0, sizeof a);
    a.base = base;
    a.rank = 1;
    a.shape[0] = extent;
    a.stride[0] = 1;
    a.span = extent;
    return a;
}

/* The extent of dimension d of place a, as compiled C reads it (back_c.ci, c_dext): a view's
 * (nv), else the place's extent. */
static int64_t dext(const ex_ref *a, int64_t d, int nv)
{
    return a->shape[nv ? d : 0];
}

/* The element offset of index i of a rank-1 place: its origin plus i strides, modulo 2^64. */
static int64_t at1(const ex_ref *a, int64_t i)
{
    return cint_rt_sext((uint64_t)a->origin + (uint64_t)i * (uint64_t)a->stride[0], 64u);
}

/* Whether every element of view a lies in its storage; an empty view reaches none. */
static int inside(const ex_ref *a)
{
    int64_t lo = a->origin, hi = a->origin, w = 0;
    for (int64_t k = 0; k < a->rank && k < 4; k++)
        if (a->shape[k] <= 0) return 1;
    for (int64_t k = 0; k < a->rank && k < 4; k++) {
        if (a->shape[k] > 1 && (cint_rt_mul_ovf_i64_p(a->stride[k], a->shape[k] - 1, &w) ||
                                (w < 0 ? cint_rt_add_ovf_i64_p(lo, w, &lo) : cint_rt_add_ovf_i64_p(hi, w, &hi)))) {
            return 0;
        }
    }
    return lo >= 0 && hi < a->span;
}

/* The descriptor of view a for the runtime, its offsets in units of q per element. */
static cint_vdesc vdesc(const ex_ref *a, uint64_t q)
{
    cint_vdesc v;
    v.base = a->base;
    v.origin = cint_rt_sext((uint64_t)a->origin * q, 64u);
    v.rank = a->rank;
    for (int k = 0; k < 4; k++) {
        v.shape[k] = a->shape[k];
        v.stride[k] = cint_rt_sext((uint64_t)a->stride[k] * q, 64u);
    }
    return v;
}

static void trace(cint_sir_image *x, uint32_t kind, uint64_t step, cint_site s, const si_module *m,
                  const si_node *n, const uint64_t *v, uint32_t nv, const uint64_t *result, uint64_t error)
{
    cint_sir_event e;
    memset(&e, 0, sizeof e);
    e.kind = kind;
    e.step = step;
    e.site = s;
    if (n != NULL) {
        e.operation = (const char *)m->text + n->word.at;
        e.operation_len = n->word.len;
    }
    e.operand_count = nv;
    for (uint32_t k = 0; k < nv && k < 3u; k++) e.operands[k] = v[k];
    e.has_result = result != NULL;
    e.result = result != NULL ? *result : 0u;
    e.error = error;   /* the error tag of a return or a result form (box 12) */
    x->trace(x->trace_user, &e);
}

static ex_frame *frame_new(const cint_sir_image *x, uint32_t mi, uint32_t fi)
{
    const si_module *m = &x->p->mods[mi];
    const si_func *f = &m->funcs[fi];
    ex_fn *e = &x->mods[mi].fns[fi];
    size_t refs = (size_t)f->nslot + f->nparam, cells = (size_t)f->ntemp + e->ncell + f->pcells + 1u;
    size_t marks = 2u + 2u * (size_t)f->nmark, accs = f->kind == FN_KERNEL ? f->nparam : 0u;
    cells += (f->handles ? HCELLS * (size_t)f->ntemp : 0u) + e->nwcell;
    size_t bytes = sizeof(ex_frame) + refs * sizeof(ex_ref) + (cells + marks) * sizeof(uint64_t) + accs * sizeof(ex_acc);
    ex_frame *fr = e->spare;
    if (fr != NULL) {
        e->spare = fr->next;
        memset(fr, 0, bytes);
    } else {
        fr = calloc(1, bytes);
    }
    if (fr == NULL) return NULL;
    fr->mi = mi;
    fr->f = f;
    fr->slot = (ex_ref *)(void *)(fr + 1);
    fr->par = fr->slot + f->nslot;
    fr->t = (uint64_t *)(void *)(fr->par + f->nparam);
    fr->cell = fr->t + f->ntemp;
    fr->h = fr->cell + e->ncell + f->pcells + 1u;
    fr->w = fr->h + (f->handles ? HCELLS * (size_t)f->ntemp : 0u);
    fr->wat = e->wat;
    fr->mark = (int64_t *)(void *)(fr->w + e->nwcell);
    fr->acc = (ex_acc *)(void *)(fr->mark + marks);
    for (uint32_t k = 0; k < f->nslot; k++) {   /* a reference or a view reads as nothing until bound */
        fr->slot[k] = ref1(fr->cell + (e->at[k] == EX_UNSET ? 0u : e->at[k]), e->at[k] == EX_UNSET ? 0 : e->ext[k]);
        fr->slot[k].rank = e->at[k] == EX_UNSET ? 0 : 1;
    }
    for (uint32_t p = 0, c = e->ncell; p < f->nparam; c += m->params[f->param + p++].cells)
        fr->par[p] = ref1(fr->cell + c, 1);
    return fr;
}

static int run(cint_sir_image *x, cint_ctx *ctx, ex_frame *fr, uint64_t *result);

/* The identifier of arena or pool operand o (box 12): its line's, or the one a parameter or a
 * slot holds. */
static uint32_t mem_id(const cint_sir_image *x, const ex_frame *fr, const si_opd *o)
{
    return o->kind == OD_MEM ? x->p->mods[fr->mi].mems[o->index].id : (uint32_t)*place(x, fr, o).base;
}

/* A call node: the callee's frame from the arguments, call.enter at the call's site, the
 * callee, then the call's leave, as compiled C calls (back_c.ci, SO_CALL). */
static int call(cint_sir_image *x, cint_ctx *ctx, ex_frame *fr, const si_node *n, cint_site s)
{
    const si_module *m = &x->p->mods[fr->mi];
    const si_module *cm = &x->p->mods[n->cmod];
    const si_func *cf = &cm->funcs[n->cfn];
    const si_opd *o = m->opds + n->opd + 1u;
    uint64_t r = 0, args[3] = {0, 0, 0};
    ex_frame *cfr = frame_new(x, n->cmod, n->cfn);
    if (cfr == NULL) return -1;
    for (uint32_t p = 0; p < cf->nparam; p++) {
        const si_param *cp = &cm->params[cf->param + p];
        if (o->kind == OD_EXTENT) {
            ex_ref a = place(x, fr, o + 1);
            *cfr->par[p].base = (uint64_t)dext(&a, o->value, (int)o->index);
            o += 2;
        } else if (cp->ty.kind == TK_SCALAR && is_wide(cp->ty.scalar)) {   /* its limbs (box 15) */
            memcpy(cfr->par[p].base, wget(fr, o++), (size_t)cp->cells * sizeof(uint64_t));
        } else if (cp->ty.kind == TK_SCALAR) {
            *cfr->par[p].base = vget(fr, o++);
        } else if (cp->ty.kind == TK_MEM) {   /* an arena or a pool: its identifier (box 12) */
            *cfr->par[p].base = mem_id(x, fr, o++);
        } else if (cp->ty.kind == TK_HANDLE) {
            memcpy(cfr->par[p].base, hcells(fr, o++), sizeof(cint_handle));
        } else if (cp->cells > 0) {   /* a struct by value: a copy */
            memmove(cfr->par[p].base, place(x, fr, o++).base, (size_t)cp->cells * sizeof(uint64_t));
        } else {
            ex_ref *a = &cfr->par[p];
            *a = place(x, fr, o++);
            for (int64_t k = cp->ty.rank > 1u ? 4 : 1; cp->ty.kind == TK_VIEW && k < 4; k++) {
                a->shape[k] = a->stride[k] = 0;   /* a view of rank 1: its base, origin, extent and stride */
                a->rank = 1;
            }
        }
        if (p < 3u && cm->params[cf->param + p].ty.kind == TK_SCALAR) args[p] = *cfr->par[p].base;
    }
    uint64_t wr[16];   /* a wide result the call discards */
    uint64_t step = x->step++, *res = cf->result.kind == TK_STRUCT ? place(x, fr, o).base   /* `into` */
                                     : cf->result.kind == TK_HANDLE ? fr->h + HCELLS * (uint32_t)n->dst
                                     : !is_wide(cf->result.scalar) ? &r
                                     : n->dst >= 0 ? fr->w + fr->wat[n->dst] : wr;
    if (x->trace != NULL)
        trace(x, CINT_SIR_EV_CALL, step, s, m, n, args, cf->nparam < 3u ? cf->nparam : 3u, NULL, 0u);
    int st = (cint_rt_interrupt_poll(ctx), cint_rt_call_enter(ctx, s)) ? run(x, ctx, cfr, res) : 0;
    uint64_t tag = cfr->rtag;
    cfr->next = x->mods[n->cmod].fns[n->cfn].spare;
    x->mods[n->cmod].fns[n->cfn].spare = cfr;
    if (st == 1) {
        cint_rt_call_leave(ctx);
        if (n->dst >= 0 && !n->wide) fr->t[n->dst] = r;
        fr->etag = tag;   /* for the error.tag after a call of a function with an error union result */
        if (x->trace != NULL) {
            trace(x, CINT_SIR_EV_RETURN, step, s, m, n, NULL, 0,
                  n->dst >= 0 && tag == 0u && cf->result.kind == TK_SCALAR ? res : NULL, tag);
        }
    }
    return st;
}

/* A host.call node (SPEC-09 SIR-09a; rt/OPEN.md RT-OQ-75): row cfn of the module's host import
 * table, its arguments as compiled C passes them (back_c.ci, host_call): a scalar or a Grants by
 * its bits, and a view by its elements at their width, staged in a buffer that an `out` view's
 * cells take back when the call returns. The fuel unit, the staging of `out` and the boundary
 * faults are cint_rt_host_call's; the call's error tag is the one error.tag reads. */
static int host_call(cint_sir_image *x, cint_ctx *ctx, ex_frame *fr, const si_node *n, cint_site s)
{
    const si_module *m = &x->p->mods[fr->mi];
    const si_extern *e = &m->externs[n->cfn];
    const si_opd *o = m->opds + n->opd + 1u;
    const si_param *ps = m->params + e->param;
    cint_value hv[16], hr;
    uint8_t *buf[16] = {NULL};
    ex_ref ref[16];
    uint64_t args[3] = {0, 0, 0};
    int st = 1;
    for (uint32_t p = 0; p < e->nparam && st == 1; p++) {
        int sc = ps[p].ty.scalar;
        unsigned w = elem_bytes(sc);
        if (ps[p].ty.kind == TK_SCALAR) {
            cint_rt_arg_scalar(&hv[p], TAGS[sc], vget(fr, o + p));
            if (p < 3u) args[p] = vget(fr, o + p);
            continue;
        }
        ref[p] = place(x, fr, o + p);
        if ((buf[p] = malloc((size_t)ref[p].shape[0] * w + 1u)) == NULL) st = -1;
        for (int64_t k = 0; st == 1 && k < ref[p].shape[0]; k++)
            state_store(buf[p] + (size_t)k * w, sc, ref[p].base[at1(&ref[p], k)]);
        cint_rt_arg_view(&hv[p], TAGS[sc], buf[p], 0, ref[p].shape[0], 1, ps[p].ty.out ? CINT_VIEW_WRITE : CINT_VIEW_READ);
    }
    uint64_t step = x->step++, r = 0;
    if (st == 1 && x->trace != NULL)
        trace(x, CINT_SIR_EV_CALL, step, s, m, n, args, e->nparam < 3u ? e->nparam : 3u, NULL, 0u);
    cint_rt_result_init(&hr, e->result.kind == TK_SCALAR ? TAGS[e->result.scalar] : 0u);
    if (st == 1 && !cint_rt_host_call(ctx, s, n->cfn, hv, e->nparam, &hr, e->errors)) st = 0;
    for (uint32_t p = 0; st == 1 && p < e->nparam; p++) {
        unsigned w = elem_bytes(ps[p].ty.scalar);
        for (int64_t k = 0; ps[p].ty.out && k < ref[p].shape[0]; k++)
            ref[p].base[at1(&ref[p], k)] = state_load(buf[p] + (size_t)k * w, ps[p].ty.scalar);
    }
    if (st == 1) {
        r = e->result.kind == TK_SCALAR ? canon(e->result.scalar, hr.as.u64) : 0u;
        if (n->dst >= 0) fr->t[n->dst] = r;
        fr->etag = hr.is_error != 0u ? canon(e->etag, (uint64_t)hr.error) : 0u;
        if (x->trace != NULL) {
            trace(x, CINT_SIR_EV_RETURN, step, s, m, n, NULL, 0, n->dst >= 0 && fr->etag == 0u ? &r : NULL,
                  fr->etag);
        }
    }
    for (uint32_t p = 0; p < e->nparam; p++) free(buf[p]);
    return st;
}

/* Before fr's function returns: its run-time storage and its charges are given back. */
static void frame_return(cint_ctx *ctx, const ex_frame *fr)
{
    if (fr->f->frame & 2u) cint_rt_scratch_release(ctx, fr->mark[1]);
    if (fr->f->frame & 1u) cint_mem_frame_release(ctx, fr->mark[0]);
}

/* A step for node n, a view-forming operation or a reduction (IM-113), traced with v. */
static void view_step(cint_sir_image *x, const ex_frame *fr, const si_node *n, cint_site s, const uint64_t *v,
                      uint32_t nv, const uint64_t *result)
{
    uint64_t step = x->step++;
    if (x->trace != NULL)
        trace(x, CINT_SIR_EV_OP, step, s, &x->p->mods[fr->mi], n, v, nv, result, 0u);
}

/* The view and frame arena nodes (box 09, compiler/OPEN.md CINTC-OQ-35 item 5; CINTC-OQ-64), as
 * compiled C runs them (back_c.ci, c_view_node and frame_node): 1, or 0 when it faulted. */
static int view_op(cint_sir_image *x, cint_ctx *ctx, ex_frame *fr, const si_node *n, cint_site s)
{
    const si_opd *o = x->p->mods[fr->mi].opds + n->opd;
    ex_ref *a = n->op == OP_FCHARGE || n->op == OP_FMARK || n->op == OP_FRELEASE ? NULL : view_of(x, fr, o), b;
    int64_t *mk = fr->mark + 2 + 2 * n->value, f = 0;
    uint64_t v[3] = {0, 0, 0}, r = 0;
    char qb[32];
    int st = 1;
    switch (n->op) {
    case OP_VBEGIN:   /* the place's view, or a flat array's in row-major order (extents in val) */
        b = place(x, fr, o + 1);
        if (n->view) {
            int64_t e = 1;
            b.rank = n->rank;
            b.origin = 0;
            for (int64_t g = 4; g-- > 0;) {
                b.shape[g] = g < n->rank ? o[3].value >> (12 * g) & 4095 : 0;
                b.stride[g] = g < n->rank ? e : 0;
                e *= g < n->rank ? b.shape[g] : 1;
            }
        }
        *a = b;
        break;
    case OP_VPICK:
        f = o[2].value;
        a->origin = cint_rt_sext((uint64_t)a->origin + vget(fr, o + 1) * (uint64_t)a->stride[f], 64u);
        break;
    case OP_VSLICE: {
        /* f: the dimension, its slice's position times 8, and the bounds written times 64 */
        int64_t fl = o[3].value, d = fl % 8;
        v[0] = fl & 64 ? vget(fr, o + 1) : 0u;
        v[1] = fl & 128 ? vget(fr, o + 2) : 0u;
        v[2] = (uint64_t)o[4].value;
        if (d == 0) view_step(x, fr, n, s, v, 3, NULL);
        st = cint_rt_slice(ctx, s, check_name(qb, SLICE, n->t1), a->rank > 1 ? d : -1, a->shape[d], a->stride[d], cint_rt_sext(v[0], 64u),
                           cint_rt_sext(v[1], 64u), o[4].value, (uint32_t)(fl / 64), &a->origin, &a->shape[d],
                           &a->stride[d]);
        break;
    }
    case OP_VEND: {   /* the picked dimensions leave the view; the rest move down in order */
        int64_t q = 0;
        for (int64_t g = 0; g < n->rank; g++) {
            if ((o[3].value >> g & 1) == 0) {
                a->shape[q] = a->shape[g];
                a->stride[q++] = a->stride[g];
            }
        }
        for (int64_t g = q; q != n->rank && g < n->rank; g++) a->shape[g] = a->stride[g] = 0;
        a->rank = q != n->rank ? q : a->rank;
        break;
    }
    case OP_VTRANS:
    case OP_VREV:
        b = place(x, fr, o + 1);
        view_step(x, fr, n, s, v, 0, NULL);
        if (n->op == OP_VTRANS) {
            ex_ref t = b;
            t.shape[0] = b.shape[1];
            t.shape[1] = b.shape[0];
            t.stride[0] = b.stride[1];
            t.stride[1] = b.stride[0];
            *a = t;
            break;
        }
        f = o[2].value;
        if (b.shape[f] > 0)
            b.origin = cint_rt_sext((uint64_t)b.origin + (uint64_t)b.stride[f] * (uint64_t)(b.shape[f] - 1), 64u);
        b.stride[f] = b.stride[f] == INT64_MIN ? INT64_MAX : -b.stride[f];
        *a = b;
        break;
    case OP_EOFF:   /* c (an earlier offset) or the origin, plus index b strides of dimension f */
        f = o[n->nopd - 1u].value;
        r = n->nopd > 3u ? vget(fr, o + 2) : (uint64_t)a->origin;
        fr->t[n->dst] = r + vget(fr, o + 1) * (uint64_t)a->stride[f];
        break;
    case OP_VCOPY: {
        cint_vdesc dv = vdesc(a, 1u), sv;
        b = place(x, fr, o + 1);
        sv = vdesc(&b, 1u);
        view_step(x, fr, n, s, v, 0, NULL);
        if (!inside(a) || !inside(&b))
            return cint_index_check(ctx, s, ICHECK[0], a->origin, a->span);
        st = cint_rt_copy_view(ctx, s, &dv, &sv, (size_t)n->value * sizeof(uint64_t));
        break;
    }
    case OP_VFILL: {   /* a copy from the value through a view of it with every stride 0 (box 15) */
        uint64_t one = is_wide(n->t2) ? 0u : vget(fr, o + 1);
        cint_vdesc dv = vdesc(a, 1u), sv = dv;
        sv.base = is_wide(n->t2) ? (void *)wget(fr, o + 1) : (void *)&one;
        sv.origin = 0;
        for (int k = 0; k < 4; k++) sv.stride[k] = 0;
        if (!inside(a)) return cint_index_check(ctx, s, ICHECK[0], a->origin, a->span);
        st = cint_rt_copy_view(ctx, s, &dv, &sv, (size_t)n->value * sizeof(uint64_t));
        break;
    }
    case OP_VRESHAPE: {   /* the place's base and origin, row-major with the extents (box 15) */
        int64_t e = 1;
        b = place(x, fr, o + 1);
        view_step(x, fr, n, s, v, 0, NULL);
        b.rank = n->rank;
        for (int64_t g = 4; g-- > 0;) {
            b.shape[g] = g < n->rank ? o[2 + g].value : 0;
            b.stride[g] = g < n->rank ? e : 0;
            e *= g < n->rank ? b.shape[g] : 1;
        }
        *a = b;
        break;
    }
    case OP_REDUCE: {
        /* each element is the low bytes of its cell (IM-146), so offsets count in cells; a wide one
         * is its limbs, so they count in elements (box 15) */
        int eq = n->sub == REDUCE_EQUAL, dot = n->sub == CINT_REDUCE_DOT || eq, wide = is_wide(n->t1) || is_wide(n->t2);
        uint64_t q = is_wide(n->t1) ? 1u : n->t1 == SC_BOOL ? 8u : 64u / width(n->t1);
        uint64_t init = n->form && !is_wide(n->t1) ? vget(fr, o + 1 + dot) : 0u;
        cint_vdesc xv = vdesc(a, q), yv;
        b = dot ? place(x, fr, o + 1) : *a;
        yv = vdesc(&b, q);
        if (!inside(a) || !inside(&b))
            return cint_index_check(ctx, s, ICHECK[0], a->origin, a->span);
        if (eq) {   /* equal (ruling OQ-307 A): a Bool */
            bool same = false;
            st = cint_rt_equal(ctx, s, tag_of(n->t1), &xv, &yv, &same);
            r = same;
        } else if (wide) {   /* cint_rt_reduce_wide (rt/OPEN.md RT-OQ-54); a narrow result is its C object */
            ex_obj u;
            uint64_t *w = n->wide ? fr->w + fr->wat[n->dst] : &u.u64;
            const void *iv = !n->form ? NULL : is_wide(n->t1) ? (const void *)wget(fr, o + 1 + dot) : &init;
            u.u64 = 0;
            st = sc_is_q(n->t2)   /* fixed point of 128 bits (RT-OQ-63), with dot's mode */
                     ? cint_rt_reduce_q_limbs(ctx, s, n->sub, tag_of(n->t1), tag_of(n->t2), n->mode, &xv, dot ? &yv : NULL, iv, w)
                     : cint_rt_reduce_wide(ctx, s, n->sub, tag_of(n->t1), tag_of(n->t2), &xv, dot ? &yv : NULL, iv, w);
            r = n->wide ? w[0] : narrow_obj(storage(n->t2), &u);   /* a wide result's low limb, as a trace shows it */
        } else if (dot && sc_is_q(n->t1)) {   /* rounded once under its mode (box 15; RT-OQ-57) */
            st = cint_rt_dot_fixed(ctx, s, tag_of(n->t1), tag_of(n->t2), n->mode, &xv, &yv, &r);
            r = canon(n->t2, r);
        } else {   /* fixed point as cint_reduce_q_<S> calls it, with the type's word */
            st = cint_rt_reduce(ctx, s, n->sub, tag_of(n->t1), tag_of(n->t2), &xv, dot ? &yv : NULL, n->form != 0, init, &r);
            r = canon(n->t2, r);
        }
        view_step(x, fr, n, s, v, 0, st ? &r : NULL);
        if (!n->wide) fr->t[n->dst] = r;
        break;
    }
    case OP_VEXT:   /* dimension f's extent, or with f -1 the element count */
        f = o[1].value;
        r = (uint64_t)a->shape[n->view && f > 0 ? f : 0];
        for (int64_t g = 1; n->view && f < 0 && g < n->rank; g++) r *= (uint64_t)a->shape[g];
        fr->t[n->dst] = r;
        break;
    case OP_CALIAS: {
        cint_vdesc p, q;
        b = place(x, fr, o + 1);
        p = vdesc(a, 1u);
        q = vdesc(&b, 1u);
        st = cint_rt_call_alias(ctx, s, (uint32_t)o[3].value, (uint32_t)o[2].value, &p, &q);
        break;
    }
    case OP_VDIM:
        a->shape[o[2].value] = cint_rt_sext(vget(fr, o + 1), 64u);
        break;
    case OP_VALLOC: {
        /* decl.shape at the first negative extent, row-major strides and the element count,
         * saturated, then the storage from the frame arena */
        int64_t e = 1, at = cint_rt_scratch_mark(ctx);
        void *p = NULL;
        for (int64_t g = 0; g < n->rank; g++)
            if (a->shape[g] < 0) return cint_rt_fault_decl_shape(ctx, s, g, a->shape[g]);
        a->rank = n->rank;
        for (int64_t g = n->rank; g-- > 0;) {
            a->stride[g] = e;
            e = cint_rt_mul_ovf_i64_p(e, a->shape[g], &e) ? INT64_MAX : e;
        }
        st = cint_rt_scratch(ctx, s, e, (size_t)n->value * sizeof(uint64_t), &p);
        a->base = st ? p : a->base;
        a->span = st ? e : 0;
        a->held = st ? at + 1 : a->held;
        break;
    }
    case OP_AINIT:
        if (o[3].value >= a->span) return cint_index_check(ctx, s, ICHECK[0], o[3].value, a->span);
        if (is_wide(n->t2)) {   /* a wide element's limbs (box 15) */
            memcpy(a->base + o[3].value * (int64_t)limbs(n->t2), wget(fr, o + 1), limbs(n->t2) * sizeof(uint64_t));
        } else {
            a->base[o[3].value] = vget(fr, o + 1);
        }
        break;
    case OP_FCHARGE:
        st = cint_mem_frame_charge(ctx, s, n->value);
        break;
    case OP_FMARK:
    case OP_FRELEASE:   /* the charges' part, then the run-time storage's */
        if (n->t1 & 1u) {
            if (n->op == OP_FMARK) {
                mk[0] = cint_mem_frame_mark(ctx);
            } else {
                cint_mem_frame_release(ctx, mk[0]);
            }
        }
        if (n->t1 & 2u) {
            if (n->op == OP_FMARK) {
                mk[1] = cint_rt_scratch_mark(ctx);
            } else {   /* at the frame's own mark at least: a mark the text never took reads as 0 */
                int64_t at = mk[1] > fr->mark[1] ? mk[1] : fr->mark[1];
                cint_rt_scratch_release(ctx, at);
                for (uint32_t k = 0; k < fr->f->nslot + fr->f->nparam; k++) {
                    if (fr->slot[k].held > at) {   /* its storage given back: it reads as nothing, as unbound */
                        fr->slot[k] = ref1(fr->cell, 0);
                        fr->slot[k].rank = 0;
                    }
                }
            }
        }
        break;
    default:
        break;
    }
    return st;
}

/* A dispatch node (back_c.ci, SO_KCALL): the kernel's frame from the arguments (a view
 * parameter the argument's view, a scalar its value, a scalar `out` the caller's slot), then the
 * kernel with the dispatch site and the writable bits, as compiled C calls it: no call.enter
 * and no step of its own. */
static int dispatch(cint_sir_image *x, cint_ctx *ctx, ex_frame *fr, const si_node *n, cint_site s)
{
    const si_module *m = &x->p->mods[fr->mi], *km = &x->p->mods[n->cmod];
    const si_func *kf = &km->funcs[n->cfn];
    const si_opd *o = m->opds + n->opd + 1u;
    uint8_t *gb = NULL;   /* an `out` global's bytes: the kernel publishes into cells, then into them */
    uint64_t cell[16] = {0};   /* a wide one's limbs (box 15) */
    int sc = SC_NONE;
    ex_frame *kfr = frame_new(x, n->cmod, n->cfn);
    if (kfr == NULL) return -1;
    for (uint32_t p = 0; p < kf->nparam; p++, o++) {
        si_type ty = km->params[kf->param + p].ty;
        if (ty.kind == TK_SCALAR && !ty.out && is_wide(ty.scalar)) {   /* its limbs, in its cells */
            memcpy(kfr->par[p].base, wget(fr, o), limbs(ty.scalar) * sizeof(uint64_t));
        } else if (ty.kind == TK_SCALAR && !ty.out) {
            *kfr->par[p].base = vget(fr, o);
        } else if (o->kind == OD_GLOBAL && ty.kind == TK_SCALAR) {
            if ((gb = cint_rt_state(ctx, &x->mods[fr->mi].state)) == NULL) {
                free(kfr);
                return 0;
            }
            gb += x->mods[fr->mi].vars[o->index].offset;
            sc = ty.scalar;
            if (is_wide(sc)) {
                cint_wide_load(cell, gb, limbs(sc));
            } else {
                cell[0] = state_load(gb, sc);
            }
            kfr->par[p].base = cell;
        } else {
            kfr->par[p] = place(x, fr, o);
        }
    }
    kfr->ks = s;
    kfr->kw = (uint32_t)o->value;
    int st = run(x, ctx, kfr, NULL);
    if (st == 1 && gb != NULL && is_wide(sc)) {
        cint_wide_store(gb, cell, limbs(sc));
    } else if (st == 1 && gb != NULL) {
        state_store(gb, sc, cell[0]);
    }
    if (st == 1) {
        /* every written parameter is published: the boundary at `dispatch` density (SPEC-06 1.5) */
        arrays_out(x, ctx, 0);
        cint_rt_dispatch_end(ctx, kfr->kd);
    }
    free(kfr);
    return st;
}

/* The epilogue or contribution op of the accumulator of type sc (cint_rt.h section 14,
 * CINT_RT_KERNEL_ACC): sum_sat's addition, fold_checked's step, sum's epilogue of contributions
 * of type elem, or sum_wrap's total. */
#define EX_KACC(D, SC, T, CT, K)                                                               \
    case SC: {                                                                                 \
        CT a = EX_IN_##K(CT, ac->r), y = EX_IN_##K(CT, v);                                     \
        switch (op) {                                                                          \
        case CINT_REDUCE_SUM_SAT: a = cint_add_sat_##T(a, y); break;                           \
        case CINT_REDUCE_FOLD_ADD: ok = cint_kfold_##T(ctx, s, ac->n, &a, y); break;           \
        case CINT_REDUCE_SUM: ok = cint_ksum_##T(ctx, s, elem, ac->n, &ac->z, &a); break;      \
        default: a = cint_kwrap_##T(&ac->z); break;                                            \
        }                                                                                      \
        ac->r = EX_OUT_##K(a);                                                                 \
        break;                                                                                 \
    }
/* sum and fold_checked of fixed point (box 15; RT-OQ-57) through cint_ksum_q_<S> and
 * cint_kfold_q_<S>, which name the type; sum_wrap and sum_sat are the storage integer's. */
#define EX_KACC_Q(T, CT, W)                                                                    \
    case W: {                                                                                  \
        CT a = (CT)cint_rt_sext(ac->r, 64u);                                                   \
        ok = op == CINT_REDUCE_SUM ? cint_ksum_q_##T(ctx, s, sc_qfrac(sc), ac->n, &ac->z, &a)  \
                                   : cint_kfold_q_##T(ctx, s, sc_qfrac(sc), ac->n, &a, (CT)cint_rt_sext(v, 64u)); \
        ac->r = EX_OUT_S(a);                                                                   \
        break;                                                                                 \
    }
static bool kacc(cint_ctx *ctx, cint_site s, int sc, unsigned op, uint32_t elem, ex_acc *ac, uint64_t v)
{
    bool ok = true;
    if (sc_is_q(sc) && (op == CINT_REDUCE_SUM || op == CINT_REDUCE_FOLD_ADD)) {
        switch (width(sc)) {
            EX_KACC_Q(i8, int8_t, 8u) EX_KACC_Q(i16, int16_t, 16u) EX_KACC_Q(i32, int32_t, 32u)
            EX_KACC_Q(i64, int64_t, 64u)
        default:
            break;
        }
        return ok;
    }
    switch (storage(sc)) {
        EX_TYPES(EX_KACC, 0)
    default:
        break;
    }
    return ok;
}

/* Contribution o of a reduction with a wide side, into the accumulator of a target of type sc, as
 * compiled C adds it (back_c.ci, kred_wide; box 15): a sum into the exact total by its limbs, a
 * narrow one by its pattern; fold_checked through cint_rt_wide_fold_step; sum_wrap and sum_sat on
 * the running value at the element's width (BX15-06); min and max as cint_lt_<t> compares. 1, or
 * 0 when it faulted. */
#define EX_KSTEP(D, SC, T)                                                                     \
    case SC:                                                                                   \
        if (wrap) {                                                                            \
            cint_add_wrap_##T(EX_WC(T, ac->wr), EX_WC(T, v), EX_WP(T, ac->wr));                \
        } else {                                                                               \
            cint_add_sat_##T(EX_WC(T, ac->wr), EX_WC(T, v), EX_WP(T, ac->wr));                 \
        }                                                                                      \
        break;
static int kred_wide(cint_ctx *ctx, cint_site s, const ex_frame *fr, const si_node *n, int sc, ex_acc *ac,
                     const si_opd *o)
{
    uint64_t kv = is_wide(n->t1) ? 0u : vget(fr, o);
    const uint64_t *v = is_wide(n->t1) ? wget(fr, o) : &kv;
    unsigned q = limbs(sc);
    int c = 0;
    switch (n->sub) {
    case CINT_REDUCE_SUM:   /* fixed point by its storage's (RT-OQ-63) */
        cint_wzacc_add(&ac->wz, tag_of(storage(n->t1)), v);
        return 1;
    case CINT_REDUCE_FOLD_ADD:
        return cint_rt_wide_fold_step(ctx, s, CINT_RT_OP_ADD, tag_of(sc), ac->n, ac->wr, v);
    case CINT_REDUCE_MIN:
    case CINT_REDUCE_MAX:
        c = cint_w_cmp(v, ac->wr, q, wsigned(sc));
        if (ac->n == 0 || (n->sub == CINT_REDUCE_MIN ? c < 0 : c > 0)) memcpy(ac->wr, v, q * sizeof *v);
        return 1;
    default:   /* sum_wrap and sum_sat, whose element is the target's type */
        break;
    }
    int wrap = n->sub == CINT_REDUCE_SUM_WRAP;
    switch (storage(sc)) {
        EX_WIDES(EX_KSTEP, 0)
    default:
        break;
    }
    return 1;
}

/* The nodes of a kernel's dispatch (box 09; compiler/OPEN.md CINTC-OQ-35 item 5), as compiled C
 * runs them (back_c.ci, c_kernel_node and c_kred): 1, or 0 when it faulted. A view's elements
 * are cells, a wide one's its limbs (box 15), so the runtime stages, copies and compares them in
 * units of an element's cells. */
static int kernel_op(cint_sir_image *x, cint_ctx *ctx, ex_frame *fr, const si_node *n, cint_site s)
{
    const si_module *m = &x->p->mods[fr->mi];
    const si_opd *o = m->opds + n->opd;
    const si_param *ps = m->params + fr->f->param;
    int out = n->op == OP_KACC || n->op == OP_KRED || n->op == OP_KEPI || (n->op == OP_KPUB && n->nopd == 1u);
    ex_acc *ac = out ? &fr->acc[o->index] : NULL;
    int sc = out ? ps[o->index].ty.scalar : SC_NONE, st = 1;
    uint64_t v = 0;
    switch (n->op) {
    case OP_KENTRY:
        fr->kd = cint_rt_dispatch_number(ctx);
        x->drank = 0;
        break;
    case OP_KSHAPE:   /* dimension f of the view at position c has extent t (F-5 checks 1 to 4) */
        st = cint_shape_check_dim(ctx, s, (uint32_t)o[1].value, (uint32_t)o[2].value, cint_rt_sext(vget(fr, o + 3), 64u),
                                  fr->par[o->index].shape[o[2].value]);
        break;
    case OP_KWHERE: {   /* constraint c, comparison f of a and b (check 5) */
        si_node cmp;
        uint64_t a = vget(fr, o + 2), b = vget(fr, o + 3);
        memset(&cmp, 0, sizeof cmp);
        cmp.sub = (uint8_t)(n->view - 1u);   /* CMP_EQ 1 to CMP_GE 6, as CM_EQ 0 to CM_GE 5 */
        cmp.t1 = n->t1;
        if (!compare(&cmp, a, b))
            st = cint_rt_fault_where(ctx, s, o[0].value, cint_rt_sext(a, 64u), cint_rt_sext(b, 64u));
        break;
    }
    case OP_KALIAS: {   /* written view c against view f (check 7) */
        uint32_t c = o[0].index, f = o[1].index;
        cint_vdesc p = vdesc(&fr->par[c], 1u), q = vdesc(&fr->par[f], 1u);
        st = cint_rt_dispatch_alias(ctx, s, c, f, &p, &q, tag_of(n->t1), ((fr->kw >> c) & 1u) | (((fr->kw >> f) & 1u) << 1));
        break;
    }
    case OP_KFUEL: {   /* checks 6 to 8 of a host-issued dispatch, then N, the saturating product */
        int64_t nn = 1;
        if (!cint_rt_dispatch_bound(ctx, s)) return 0;
        for (int64_t q = 0; q < o[1].value; q++) {
            int64_t e = cint_rt_sext(*fr->slot[o[2 + q].index].base, 64u);
            nn = e <= 0 ? 0 : nn > INT64_MAX / e ? INT64_MAX : nn * e;
            x->dext[q] = e;
        }
        x->drank = (uint32_t)o[1].value;
        *fr->slot[o->index].base = (uint64_t)nn;
        fr->kn = nn;
        fr->kc = 0;
        st = nn > 0 ? cint_fuel_charge(ctx, s, (uint64_t)nn) : 1;
        break;
    }
    case OP_KSTAGE: {   /* the staging view of written view v, its elements copied in with f 2 */
        cint_vdesc g, a = vdesc(&fr->par[o[1].index], 1u);
        ex_ref *d = &fr->slot[o->index];
        int64_t at = cint_rt_scratch_mark(ctx);
        if (!cint_rt_dispatch_stage(ctx, s, &g, &a, limbs(ps[o[1].index].ty.scalar) * sizeof(uint64_t), o[2].value == 2))
            return 0;
        memset(d, 0, sizeof *d);
        d->base = g.base;
        d->origin = g.origin;
        d->rank = g.rank;
        d->span = 1;
        d->held = at + 1;
        for (int k = 0; k < 4; k++) {
            d->shape[k] = g.shape[k];
            d->stride[k] = g.stride[k];
            d->span *= k < g.rank ? g.shape[k] : 1;   /* the elements cint_rt_scratch counted */
        }
        break;
    }
    case OP_KACC:
        if (is_wide(sc)) {
            memcpy(ac->wr, wget(fr, o + 1), limbs(sc) * sizeof(uint64_t));
        } else {
            ac->r = vget(fr, o + 1);
        }
        break;
    case OP_KPUSH:
        cint_rt_dispatch_push(ctx, s);
        st = !cint_rt_faulted(ctx);
        fr->kph = st ? CINT_PHASE_WORK_ITEM : fr->kph;
        break;
    case OP_KITEM: {   /* work-item w: the `over` variables at its indices, row-major */
        int64_t d = o[1].value, kr = cint_rt_sext(vget(fr, o), 64u), far = kr < 0 || kr >= fr->kn;
        fr->kwi = fr->kc;   /* the item's ordinal, which a fault of an item not run reports */
        fr->kst = 0;
        if (far || fr->kc >= fr->kn) {   /* an item outside 0 to N - 1, or past the N charged: only an edited text */
            st = cint_index_check(ctx, s, ICHECK[SC_I64], far ? kr : fr->kc, fr->kn) ? 1 : 0;
            break;
        }
        fr->kwi = kr;
        fr->kc += 1;
        for (int64_t q = d; q-- > 0;) {
            int64_t e = cint_rt_sext(*fr->slot[o[2 + d + q].index].base, 64u);
            if (e <= 0) {   /* a domain of no work-item, which only an edited text runs an item of */
                st = cint_index_check(ctx, s, ICHECK[SC_I64], kr, e) ? 1 : 0;
                break;
            }
            *fr->slot[o[2 + q].index].base = (uint64_t)(kr % e);
            kr /= q > 0 ? e : 1;
        }
        break;
    }
    case OP_KSTEP:
        fr->kst += 1;
        break;
    case OP_KRED:   /* contribution v (back_c.ci, c_kred) */
        v = is_wide(n->t1) ? 0u : vget(fr, o + 1);
        if (is_wide(sc) || is_wide(n->t1)) {
            st = kred_wide(ctx, s, fr, n, sc, ac, o + 1);
        } else if (n->sub == CINT_REDUCE_SUM || n->sub == CINT_REDUCE_SUM_WRAP) {
            cint_zacc_add(&ac->z, tag_of(n->t1), v);
        } else if (n->sub == CINT_REDUCE_SUM_SAT || n->sub == CINT_REDUCE_FOLD_ADD) {
            st = kacc(ctx, s, sc, n->sub, 0u, ac, v);
        } else if (n->sub == CINT_REDUCE_MIN || n->sub == CINT_REDUCE_MAX) {
            si_node lt;
            memset(&lt, 0, sizeof lt);
            lt.sub = CM_LT;
            lt.t1 = (uint16_t)sc;
            if (ac->n == 0 || (n->sub == CINT_REDUCE_MIN ? compare(&lt, v, ac->r) : compare(&lt, ac->r, v)))
                ac->r = v;
        } else if (v != 0u) {   /* count */
            ac->r = canon(sc, ac->r + 1u);
        }
        ac->n += st;
        break;
    case OP_KPHASE:
        fr->kph = CINT_PHASE_EPILOGUE;
        break;
    case OP_KEPI:   /* sum's fit and sum_wrap's total; min and max of no contribution fault */
        if ((n->sub == CINT_REDUCE_SUM || n->sub == CINT_REDUCE_SUM_WRAP) && (is_wide(sc) || is_wide(n->t1))) {
            /* a wide side: the exact total fits through cint_rt_wzacc_fit, a narrow target into its C
             * object; sum_wrap ran on the running value */
            ex_obj u;
            u.u64 = 0;
            if (n->sub == CINT_REDUCE_SUM && sc_is_q(sc)) {   /* fixed point of 128 bits (RT-OQ-63) */
                st = cint_rt_wzacc_fit_q(ctx, s, tag_of(sc), ac->n, &ac->wz, is_wide(sc) ? (void *)ac->wr : &u);
                if (st && !is_wide(sc)) ac->r = narrow_obj(storage(sc), &u);
            } else if (n->sub == CINT_REDUCE_SUM) {
                st = cint_rt_wzacc_fit(ctx, s, "sum", tag_of(n->t1), tag_of(sc), ac->n, &ac->wz, is_wide(sc) ? (void *)ac->wr : &u);
                if (st && !is_wide(sc)) ac->r = narrow_obj(sc, &u);
            }
        } else if (n->sub == CINT_REDUCE_SUM || n->sub == CINT_REDUCE_SUM_WRAP) {
            st = kacc(ctx, s, sc, n->sub, tag_of(n->t1), ac, 0u);
        } else if ((n->sub == CINT_REDUCE_MIN || n->sub == CINT_REDUCE_MAX) && ac->n == 0) {
            st = cint_rt_fault_reduce_empty(ctx, s, n->sub == CINT_REDUCE_MAX, tag_of(sc));
        }
        break;
    case OP_KPOP:
        cint_rt_dispatch_pop(ctx);
        break;
    case OP_KPUB:   /* the staging view into view v, or the accumulator into the caller's slot */
        if (ac != NULL && is_wide(sc)) {
            memcpy(fr->par[o->index].base, ac->wr, limbs(sc) * sizeof(uint64_t));
        } else if (ac != NULL) {
            *fr->par[o->index].base = canon(sc, ac->r);
        } else {
            cint_vdesc dv = vdesc(&fr->par[o->index], 1u), sv = vdesc(&fr->slot[o[1].index], 1u);
            st = cint_rt_copy_view(ctx, s, &dv, &sv, limbs(ps[o->index].ty.scalar) * sizeof(uint64_t));
        }
        break;
    default:
        break;
    }
    return st;
}

/* An access the text did not check first: E_BOUNDS at the site of the index.checked of its place
 * that the reader paired with it (sir_read.c, pair_check), under that check's name, since the
 * access itself has no site. */
static int unchecked(cint_ctx *ctx, const si_module *m, uint32_t mi, const si_node *n, int64_t i, int64_t ext)
{
    if (n->guard == UINT32_MAX) {   /* a kernel's element form, whose shapes its entry bound */
        cint_site g = {mi, 0u};
        return cint_index_check(ctx, g, ICHECK[0], i, ext);
    }
    const si_node *c = &m->nodes[n->guard];
    cint_site g = {mi, c->site};
    char qb[32];
    return cint_index_check(ctx, g, check_name(qb, ICHECK, c->t1), i, ext);
}

/* A memory node of box 12 through rt/cint_mem.c, as compiled C calls it (back_c.ci, SO_MEMBER
 * to SO_PREF): a member; `handle.view`, the view of rank 1 of an allocation over its arena's
 * storage and its lease; `handle.lease`, the lease's check before an access through it; or
 * `pool.deref`, the place of a pool's element. An element takes its cells there (prepare_mod),
 * so the storage is read as the interpreter's own. Each is one step (IM-113). */
static int mem_op(cint_sir_image *x, cint_ctx *ctx, ex_frame *fr, const si_node *n, cint_site s)
{
    const si_module *m = &x->p->mods[fr->mi];
    const si_opd *o = m->opds + n->opd;
    const cint_mem_table *t = &x->mods[fr->mi].mem;
    uint64_t step = x->step++, v = 0;
    uint16_t tag = 0;
    int st = 1, at = n->op == OP_MEMBER ? 0 : 1;   /* the container's operand */
    cint_handle h;
    memset(&h, 0, sizeof h);
    if (n->op == OP_HLEASE) {
        cint_lease l;
        memcpy(&l, fr->slot[o->index].base, sizeof l);
        st = cint_mem_lease(ctx, s, t, &l);
    } else if (n->op == OP_MEMBER && n->sub != MB_INSERT && n->sub != MB_INSERT_RESULT && n->sub != MB_REMOVE) {
        uint32_t c = mem_id(x, fr, o), id = 0;
        int64_t k = n->sub == MB_RESET ? 0 : cint_rt_sext(vget(fr, o + 1), 64u);
        st = n->sub == MB_RESET ? cint_mem_reset(ctx, s, t, c)
             : n->sub == MB_CHILD ? cint_mem_child(ctx, s, t, c, k, &id)
                                  : cint_mem_alloc(ctx, s, t, c, k, n->sub == MB_ALLOC_RESULT, &h, &tag);
        if (st && n->sub == MB_CHILD) *place(x, fr, o + 2).base = id;
    } else if (n->op == OP_MEMBER && n->sub != MB_REMOVE) {   /* a scalar, a wide one's limbs, or a struct's cells */
        v = n->t1 != SC_NONE && !is_wide(n->t1) ? vget(fr, o + 1) : 0u;
        st = cint_mem_insert(ctx, s, t, mem_id(x, fr, o),
                             is_wide(n->t1) ? wget(fr, o + 1) : n->t1 != SC_NONE ? &v : place(x, fr, o + 1).base,
                             n->sub == MB_INSERT_RESULT, &h, &tag);
    } else {
        memcpy(&h, hcells(fr, o + at + 1), sizeof h);
    }
    if (n->op == OP_MEMBER && n->sub == MB_REMOVE) {
        st = cint_mem_remove(ctx, s, t, mem_id(x, fr, o), h);
    } else if (n->op == OP_HVIEW) {
        cint_vdesc d;
        cint_lease l;
        if ((st = cint_mem_view(ctx, s, t, mem_id(x, fr, o + 1), h, &d, &l)) != 0) {
            ex_ref *a = &fr->slot[o->index];
            *a = ref1((uint64_t *)d.base, d.origin + d.shape[0]);   /* the arena's storage up to its end */
            a->origin = d.origin;
            a->shape[0] = d.shape[0];
            memcpy(fr->slot[o->index + 1u].base, &l, sizeof l);
        }
    } else if (n->op == OP_PDEREF) {
        uint64_t *e = cint_mem_deref(ctx, s, t, mem_id(x, fr, o + 1), h);
        st = e != NULL;
        if (st) fr->slot[o->index] = ref1(e, 1);
    } else if (st && n->dst >= 0) {   /* a handle, and the AllocError tag of an _result form */
        memcpy(fr->h + HCELLS * (uint32_t)n->dst, &h, sizeof h);
        fr->etag = tag;
    }
    if (x->trace != NULL)
        trace(x, CINT_SIR_EV_OP, step, s, m, n, NULL, 0, NULL, n->dst >= 0 ? fr->etag : 0u);
    return st;
}

/* Whether node n is fixed point with a side in limbs: of 128 bits, or with a wide integer (games
 * wave G2b); a comparison is wide_node's. */
static int q_limbs(const si_node *n)
{
    int q = sc_is_q(n->t1) || ((n->op == OP_AS || n->op == OP_BUILTIN) && sc_is_q(n->t2));
    return q && n->op != OP_CMP && (is_wide(n->t1) || is_wide(n->t2) || (n->op == OP_BUILTIN && is_wide(n->t3)));
}

/* A q_limbs node, through cint_rt_q_limbs as compiled C calls it (back_c.ci, c_q_limbs; rt/OPEN.md
 * RT-OQ-63): a narrow operand by its pattern, a narrow result into its C object, the `%` and `|`
 * forms with no site; min, max and as.raw are the storage integer's (IM-60). One step. 1, or 0
 * when it faulted. */
static int qlimbs(cint_sir_image *x, cint_ctx *ctx, ex_frame *fr, const si_node *n, const si_opd *o, cint_site s)
{
    int bi = n->op == OP_BUILTIN, two = n->op == OP_BIN || (bi && n->sub != 1 && n->sub != BI_SQRT), st = 1;
    int tb = !two ? SC_NONE : bi && n->sub >= BI_QMUL ? n->t3 : n->t1, tr = n->op == OP_AS || bi ? n->t2 : n->t1;
    uint64_t pat[2] = {0u, 0u}, low[2] = {0u, 0u}, r = 0, step = x->step++;
    const uint64_t *v[2] = {pat, pat + 1};
    ex_obj u;
    uint64_t *out = n->wide ? fr->w + fr->wat[n->dst] : &u.u64;
    u.u64 = 0;
    for (uint32_t k = 0; k < 1u + (uint32_t)two; k++) {
        int t = k == 0 ? n->t1 : tb;
        pat[k] = is_wide(t) ? 0u : vget(fr, o + k);
        v[k] = is_wide(t) ? wget(fr, o + k) : &pat[k];
        low[k] = v[k][0];
    }
    if (n->op == OP_AS && n->form == FM_RAW) {
        memcpy(out, v[0], limbs(tr) * sizeof(uint64_t));
    } else if (bi && (n->sub == 3 || n->sub == 4)) {   /* min and max */
        int c = cint_w_cmp(v[0], v[1], limbs(tr), true);
        memcpy(out, (n->sub == 3) == (c <= 0) ? v[0] : v[1], limbs(tr) * sizeof(uint64_t));
    } else {
        uint32_t op = n->op == OP_AS   ? CINT_Q_AS
                      : n->op == OP_UN ? CINT_Q_NEG
                      : !bi            ? (n->sub == BO_DIV ? CINT_Q_DIV : (uint32_t)n->sub + 1u)
                      : n->sub == 1    ? CINT_Q_ABS
                      : n->sub == BI_SQRT ? CINT_Q_SQRT
                      : n->sub == BI_QMUL ? CINT_Q_QMUL
                                          : CINT_Q_MUL_INT;
        uint32_t form = bi ? CINT_Q_CHECKED : n->form;
        bool ok = cint_rt_q_limbs(ctx, form == CINT_Q_CHECKED ? s : (cint_site){0u, 0u}, op, form, n->mode,
                                  tag_of(n->t1), tb == SC_NONE ? 0u : tag_of(tb), tag_of(tr), v[0], two ? v[1] : NULL, out);
        st = ok || form != CINT_Q_CHECKED;
    }
    r = n->wide ? out[0] : narrow_obj(storage(tr), &u);   /* a wide result's low limb, as a trace shows it */
    if (!n->wide) fr->t[n->dst] = r;
    if (x->trace != NULL)
        trace(x, CINT_SIR_EV_OP, step, s, &x->p->mods[fr->mi], n, low, 1u + (uint32_t)two, st == 1 ? &r : NULL, 0u);
    return st;
}

/* An operator, comparison, conversion or built-in node with a wide operand or result (box 15), as
 * run_body runs a narrow one: a comparison, a bitwise operator and `not` take no step, any other
 * node one, through its rt/cint_wide.h helper. 1, or 0 when it faulted. */
static int wide_node(cint_sir_image *x, cint_ctx *ctx, ex_frame *fr, const si_node *n, const si_opd *o, cint_site s)
{
    const uint64_t *v[3] = {NULL, NULL, NULL};
    uint64_t low[3] = {0, 0, 0}, k = 0, r = 0;   /* low: each operand's low limb, as a trace shows it */
    uint32_t nv = n->nopd < 3u ? n->nopd : 3u, q = limbs(n->t1);
    int count = (n->op == OP_BIN && (n->sub == BO_SHL || n->sub == BO_SHR)) ||
                (n->op == OP_BUILTIN && (n->sub == 15 || n->sub == 16)), st = 1;
    cint_count kc = count ? count_at(fr, n, o + 1) : count_of(SC_U64, 0u);
    for (uint32_t i = 0; i < nv; i++) {
        if (count && i == 1u) {   /* a shift's count, narrow or wide, or a rotation's */
            low[i] = is_wide(n->t3) ? wget(fr, o + i)[0] : vget(fr, o + i);
        } else if (!is_wide(n->t1)) {   /* a narrow source */
            low[i] = k = vget(fr, o + i);
        } else {
            v[i] = wget(fr, o + i);
            low[i] = v[i][0];
        }
    }
    uint64_t *out = n->wide ? fr->w + fr->wat[n->dst] : &r;
    if (n->op == OP_CMP) {
        int c = cint_w_cmp(v[0], v[1], q, wsigned(n->t1));
        fr->t[n->dst] = (uint64_t)(n->sub == CM_EQ   ? c == 0
                                   : n->sub == CM_NE ? c != 0
                                   : n->sub == CM_LT ? c < 0
                                   : n->sub == CM_LE ? c <= 0
                                   : n->sub == CM_GT ? c > 0
                                                     : c >= 0);
        return 1;
    }
    if ((n->op == OP_BIN && n->sub >= BO_AND) || (n->op == OP_UN && n->sub == BO_NOT)) {
        for (uint32_t i = 0; i < q; i++) {
            out[i] = n->op == OP_UN ? ~v[0][i] : n->sub == BO_AND ? v[0][i] & v[1][i]
                     : n->sub == BO_OR ? v[0][i] | v[1][i] : v[0][i] ^ v[1][i];
        }
        return 1;
    }
    uint64_t step = x->step++;
    if (n->op == OP_AS) {
        st = wconv(ctx, s, n, k, v[0], out, &fr->etag);
    } else if (n->op == OP_BUILTIN && n->sub == 12) {   /* mul_full, which never faults */
        wmul_full(n, v, low, out);
    } else if (n->op == OP_BUILTIN && n->sub == 2) {   /* uabs, which never faults */
        wuabs(n, v[0], out);
    } else if (n->op == OP_BUILTIN && (n->sub == 15 || n->sub == 16)) {
        st = wrotate(ctx, s, n, v[0], kc, out);
    } else if (n->op == OP_BUILTIN && n->sub == 11 && !n->wide) {   /* muldiv into a narrow type */
        ex_obj u;
        u.u64 = 0;
        st = warith(ctx, s, n, v, kc, &u, &fr->etag);
        r = narrow_obj(n->t2, &u);
    } else {
        st = warith(ctx, s, n, v, kc, out, &fr->etag);
    }
    if (!n->wide) fr->t[n->dst] = r;
    if (x->trace != NULL) {
        trace(x, CINT_SIR_EV_OP, step, s, &x->p->mods[fr->mi], n, low, nv, st == 1 ? out : NULL,
              n->form == FM_RESULT ? fr->etag : 0u);
    }
    return st;
}

/* The body of fr's function, after an entry function's leading charge. 1 when it
 * returned, 0 when it faulted (the context holds the record), -1 when memory ran out. */
static int run_body(cint_sir_image *x, cint_ctx *ctx, ex_frame *fr, uint64_t *result)
{
    const si_module *m = &x->p->mods[fr->mi];
    const si_node *nodes = m->nodes;
    const ex_ins *code = x->mods[fr->mi].code;
    const int fast = x->trace == NULL;
    const uint32_t mi = fr->mi;
    uint32_t pc = fr->f->first + (nodes[fr->f->first].op == OP_FUEL), end = fr->f->first + fr->f->count - 1u;
    char qb[32];
    if (fr->f->arrays && !arrays_in(x, ctx, fr->mi)) return 0;
    /* the frame arena's marks at entry, given back when the function returns (back_c.ci,
     * frame_return); the scratch mark is also the floor of its frame.release */
    fr->mark[0] = fr->f->frame & 1u ? cint_mem_frame_mark(ctx) : 0;
    fr->mark[1] = cint_rt_scratch_mark(ctx);
    for (;;) {
        if (fast) {   /* the node's fast form, if it has one */
            const ex_ins *c = &code[pc];
            uint64_t u = 2;   /* 2: the node runs itself; else a checked operation's result, 0 when it faulted */
            int64_t w = 0;
            switch (c->code) {
            case EXF_NOP: pc++; continue;
            case EXF_CONST: fr->t[c->dst] = (uint64_t)c->value; pc++; continue;
            case EXF_LOADL: fr->t[c->dst] = fr->cell[c->a]; pc++; continue;
            case EXF_STOREL: fr->cell[c->a] = EXV(c->b); pc++; continue;
            case EXF_RET:
                fr->rtag = 0u;
                *result = EXV(c->a);
                frame_return(ctx, fr);
                return 1;
            case EXF_CMP: {
                int64_t a = (int64_t)EXV(c->a), b = (int64_t)EXV(c->b);
                fr->t[c->dst] = c->value == CM_EQ ? a == b : c->value == CM_NE ? a != b : c->value == CM_LT ? a < b
                                : c->value == CM_LE ? a <= b : c->value == CM_GT ? a > b : a >= b;
                pc++;
                continue;
            }
            case EXF_JZ: pc = EXV(c->a) == 0u ? (uint32_t)c->value : pc + 1u; continue;
            case EXF_JUMP: pc = (uint32_t)c->value; continue;
            case EXF_FUEL:
                cint_rt_interrupt_poll(ctx);
                if (!cint_fuel_charge(ctx, (cint_site){mi, c->site}, (uint64_t)c->value)) return 0;
                pc++;
                continue;
            case EXF_ICHECK:   /* a step (IM-113) */
                x->step++;
                if (!cint_index_check(ctx, (cint_site){mi, c->site}, check_name(qb, ICHECK, c->t), (int64_t)EXV(c->b), c->value)) return 0;
                pc++;
                continue;
            case EXF_ILOAD:
            case EXF_ISTORE:
                if (EXV(c->b) >= (uint64_t)c->value) break;   /* not checked first: the node faults */
                if (c->code == EXF_ILOAD) {
                    fr->t[c->dst] = fr->cell[c->a + EXV(c->b)];
                } else {
                    fr->cell[c->a + EXV(c->b)] = EXV(c->src);
                }
                pc++;
                continue;
            case EXF_ADD: u = cint_add_i64(ctx, (cint_site){mi, c->site}, (int64_t)EXV(c->a), (int64_t)EXV(c->b), &w); break;
            case EXF_SUB: u = cint_sub_i64(ctx, (cint_site){mi, c->site}, (int64_t)EXV(c->a), (int64_t)EXV(c->b), &w); break;
            case EXF_MUL: u = cint_mul_i64(ctx, (cint_site){mi, c->site}, (int64_t)EXV(c->a), (int64_t)EXV(c->b), &w); break;
            case EXF_DIV: u = cint_div_i64(ctx, (cint_site){mi, c->site}, (int64_t)EXV(c->a), (int64_t)EXV(c->b), &w); break;
            case EXF_REM: u = cint_rem_i64(ctx, (cint_site){mi, c->site}, (int64_t)EXV(c->a), (int64_t)EXV(c->b), &w); break;
            default: break;
            }
            if (u != 2) {   /* a checked I64 operation's result, or its fault; a step (IM-113) */
                x->step++;
                fr->t[c->dst] = (uint64_t)w;
                if (u == 0) return 0;
                pc++;
                continue;
            }
        }
        const si_node *n = &nodes[pc];
        const si_opd *o = m->opds != NULL ? m->opds + n->opd : NULL;   /* NULL: no operands in the module */
        cint_site s = {fr->mi, n->site};
        uint64_t v[3] = {0, 0, 0}, r = 0;
        int st = 1;
        pc++;
        if (n->site == 0 && fr->f->kind == FN_KERNEL) {
            s = fr->ks;   /* a kernel's node without a site faults at the dispatch site */
        }
        switch (n->op) {
        case OP_CONST:   /* or the null handle (box 12), or a wide constant's limbs (box 15) */
            if (n->ty.kind == TK_HANDLE) {
                memset(fr->h + HCELLS * (uint32_t)n->dst, 0, sizeof(cint_handle));
            } else if (n->wide) {
                memcpy(fr->w + fr->wat[n->dst], m->items + n->value, (size_t)n->wide * sizeof(uint64_t));
            } else {
                fr->t[n->dst] = (uint64_t)n->value;
            }
            break;
        case OP_LOAD_LOCAL:
        case OP_STORE_LOCAL:
            if (n->ty.kind == TK_HANDLE) {   /* a handle's cells (box 12) */
                uint64_t *h = n->op == OP_LOAD_LOCAL ? fr->h + HCELLS * (uint32_t)n->dst : hcells(fr, o + 1);
                memmove(n->op == OP_LOAD_LOCAL ? h : fr->slot[o->index].base,
                        n->op == OP_LOAD_LOCAL ? fr->slot[o->index].base : h, sizeof(cint_handle));
            } else if (is_wide(n->ty.scalar)) {   /* a wide integer's limbs (box 15) */
                uint64_t *w = n->op == OP_LOAD_LOCAL ? fr->w + fr->wat[n->dst] : wget(fr, o + 1);
                memmove(n->op == OP_LOAD_LOCAL ? w : fr->slot[o->index].base,
                        n->op == OP_LOAD_LOCAL ? fr->slot[o->index].base : w, limbs(n->ty.scalar) * sizeof(uint64_t));
            } else if (n->op == OP_LOAD_LOCAL) {
                fr->t[n->dst] = *fr->slot[o->index].base;
            } else {
                *fr->slot[o->index].base = vget(fr, o + 1);
            }
            break;
        case OP_LOAD_GLOBAL:
        case OP_STORE_GLOBAL: {
            uint32_t gm = o->kind == OD_IMPORT ? (uint32_t)o->value : fr->mi;
            const si_global *g = &x->p->mods[gm].globals[o->index];
            uint8_t *b = cint_rt_state(ctx, &x->mods[gm].state);
            if (b == NULL) return 0;
            b += x->mods[gm].vars[o->index].offset;
            if (g->handle) {   /* a Handle's cint_handle (OQ-242) */
                memcpy(n->op == OP_LOAD_GLOBAL ? (void *)(fr->h + HCELLS * (uint32_t)n->dst) : (void *)b,
                       n->op == OP_LOAD_GLOBAL ? (const void *)b : (const void *)hcells(fr, o + 1), sizeof(cint_handle));
            } else if (is_wide(g->scalar)) {   /* its w / 8 bytes, limb 0 first (box 15) */
                if (n->op == OP_LOAD_GLOBAL) {
                    cint_wide_load(fr->w + fr->wat[n->dst], b, limbs(g->scalar));
                } else {
                    cint_wide_store(b, wget(fr, o + 1), limbs(g->scalar));
                }
            } else if (n->op == OP_LOAD_GLOBAL) {
                fr->t[n->dst] = state_load(b, g->scalar);
            } else {
                state_store(b, g->scalar, vget(fr, o + 1));
            }
            break;
        }
        case OP_BIN:
        case OP_UN:
        case OP_CMP:
        case OP_AS:
        case OP_BUILTIN: {
            uint32_t nv = n->nopd < 3u ? n->nopd : 3u;
            if (q_limbs(n)) {
                st = qlimbs(x, ctx, fr, n, o, s);
                break;
            }
            if (n->wide || is_wide(n->t1)) {
                st = wide_node(x, ctx, fr, n, o, s);
                break;
            }
            for (uint32_t k = 0; k < nv; k++) v[k] = vget(fr, o + k);
            int shift = n->op == OP_BIN && (n->sub == BO_SHL || n->sub == BO_SHR);
            cint_count kc = shift ? count_at(fr, n, o + 1) : count_of(SC_U64, 0u);
            if (shift && is_wide(n->t3)) v[1] = wget(fr, o + 1)[0];   /* a wide count's low limb, as a trace shows it */
            if (n->op == OP_CMP) {
                r = (uint64_t)compare(n, v[0], v[1]);
            } else if (n->op == OP_BIN && n->sub >= BO_AND) {
                r = n->sub == BO_AND ? v[0] & v[1] : n->sub == BO_OR ? v[0] | v[1] : v[0] ^ v[1];
            } else if (n->op == OP_UN && n->sub == BO_NOT) {
                r = n->t1 == SC_BOOL ? !v[0] : canon(n->t1, ~v[0]);
            } else {
                uint64_t step = x->step++;
                if (sc_is_q(n->t1) || (n->op == OP_AS && sc_is_q(n->t2))) {   /* fixed point (box 15) */
                    st = qnode(ctx, s, n, v, &r, &fr->etag);
                } else if (n->form == FM_RESULT) {   /* `<op>.result.<t>` or `as.result` (box 12) */
                    fr->etag = result_form(ctx, s, n, v, kc, &r);
                } else if (n->op == OP_AS) {
                    st = conv(ctx, s, n, v[0], &r);
                } else if (n->op == OP_BUILTIN && (n->sub == 2 || n->sub == 3 || n->sub == 4 || n->sub == 12)) {
                    r = pure_builtin(n->sub, n->t1, v[0], v[1]);
                } else {
                    st = arith(ctx, s, n, v, kc, &r);
                }
                if (x->trace != NULL) {
                    trace(x, CINT_SIR_EV_OP, step, s, m, n, v, nv, st == 1 ? &r : NULL,
                          n->form == FM_RESULT ? fr->etag : 0u);
                }
            }
            fr->t[n->dst] = r;
            break;
        }
        case OP_LOCAL:   /* an array or a struct starts at zero where it is declared, each time */
            memset(fr->slot[o->index].base, 0, (size_t)n->value * sizeof(uint64_t));
            break;
        case OP_ICHECK: {
            /* an array field's extent, or a dimension's; a subscript is one step (IM-113) */
            ex_ref a = place(x, fr, o);
            int64_t d = n->rank > 1u ? o[2].value : 0, ext = n->rank <= 1u && n->nopd > 2u ? o[2].value : a.shape[d];
            uint64_t step = d == 0 ? x->step++ : 0;
            v[0] = vget(fr, o + 1);
            st = n->rank > 1u ? cint_index_check_dim(ctx, s, check_name(qb, ICHECK, n->t1), d, cint_rt_sext(v[0], 64u), ext)
                              : cint_index_check(ctx, s, check_name(qb, ICHECK, n->t1), cint_rt_sext(v[0], 64u), ext);
            if (x->trace != NULL && d == 0) trace(x, CINT_SIR_EV_OP, step, s, m, n, v, 1, NULL, 0u);
            break;
        }
        case OP_ILOAD:
        case OP_ISTORE: {
            /* an index, or a view's element offset (view.offset) */
            ex_ref a = place(x, fr, o);
            int64_t i = cint_rt_sext(vget(fr, o + 1), 64u), k = n->view ? i : at1(&a, i);
            if ((!n->view && (i < 0 || i >= a.shape[0])) || k < 0 || k >= a.span) {   /* not checked first */
                return unchecked(ctx, m, fr->mi, n, n->view ? k : i, n->view ? a.span : a.shape[0]);
            }
            if (is_wide(n->t2)) {   /* a wide element's limbs (box 15) */
                uint32_t q = limbs(n->t2);
                uint64_t *e = a.base + k * q, *w = n->op == OP_ILOAD ? fr->w + fr->wat[n->dst] : wget(fr, o + 2);
                memmove(n->op == OP_ILOAD ? w : e, n->op == OP_ILOAD ? e : w, q * sizeof(uint64_t));
            } else if (n->op == OP_ILOAD) {
                fr->t[n->dst] = a.base[k];
            } else {
                a.base[k] = vget(fr, o + 2);
            }
            break;
        }
        case OP_FLOAD:
        case OP_FSTORE:
        case OP_FILOAD:
        case OP_FISTORE:
        case OP_ELOAD:
        case OP_ESTORE: {
            /* field f of the struct, element i of array field f, or field f of element i */
            int ix = n->op == OP_FILOAD || n->op == OP_FISTORE, el = n->op == OP_ELOAD || n->op == OP_ESTORE;
            ex_ref a = place(x, fr, o);
            const si_opd *f = o + 1 + el;
            int64_t i = ix || el ? cint_rt_sext(vget(fr, o + 1 + ix), 64u) : 0, ext = ix ? f->value : el ? a.shape[0] : 1;
            int64_t k = el ? at1(&a, i) : 0;
            if (i < 0 || i >= ext || k < 0 || (el && k >= a.span)) {   /* an access the text did not check first */
                return unchecked(ctx, m, fr->mi, n, i, ext);
            }
            uint64_t *c = a.base + (el ? k * n->value : ix ? i * (int64_t)limbs(n->ty.scalar) : 0) + f->index;
            int load = n->op == OP_FLOAD || n->op == OP_FILOAD || n->op == OP_ELOAD;
            if (n->ty.kind == TK_HANDLE) {   /* a handle field's cells (box 12) */
                uint64_t *h = load ? fr->h + HCELLS * (uint32_t)n->dst : hcells(fr, o + n->nopd - 1u);
                memmove(load ? h : c, load ? c : h, sizeof(cint_handle));
            } else if (is_wide(n->ty.scalar)) {   /* a wide field's limbs (box 15) */
                uint64_t *w = load ? fr->w + fr->wat[n->dst] : wget(fr, o + n->nopd - 1u);
                memmove(load ? w : c, load ? c : w, limbs(n->ty.scalar) * sizeof(uint64_t));
            } else if (load) {
                fr->t[n->dst] = *c;
            } else {
                *c = vget(fr, o + n->nopd - 1u);
            }
            break;
        }
        case OP_FADDR:
        case OP_AFADDR:
        case OP_EADDR: {
            /* slot o binds to field f, or to element i of the array, view or array field */
            ex_ref a = place(x, fr, o + 1);
            if (n->op != OP_EADDR || n->nopd > 3u) {
                const si_opd *f = o + (n->op == OP_EADDR ? 3 : 2);
                a = ref1(a.base + f->index, f->value);
            }
            if (n->op == OP_EADDR) {
                int64_t i = cint_rt_sext(vget(fr, o + 2), 64u), k = n->view ? i : at1(&a, i);
                if ((!n->view && (i < 0 || i >= a.shape[0])) || k < 0 || k >= a.span)
                    return unchecked(ctx, m, fr->mi, n, i, n->view ? a.span : a.shape[0]);
                a = ref1(a.base + k * n->value, 1);
            }
            fr->slot[o->index] = a;
            break;
        }
        case OP_SCOPY:
        case OP_ACOPY:
        case OP_FCOPY:
        case OP_AFCOPY:
        case OP_FCOPY_OUT: {
            /* the cells of a struct, or of the elements both arrays hold */
            int out = n->op == OP_FCOPY_OUT, fld = n->op == OP_FCOPY || n->op == OP_AFCOPY;
            ex_ref d = place(x, fr, o), f = place(x, fr, o + 1 + fld);
            const si_opd *fo = out ? o + 2 : o + 1;
            if (out) {
                f = ref1(f.base + fo->index, 1);
            } else if (fld) {
                d = ref1(d.base + fo->index, fo->value);
            }
            /* elements in index order, a view's through its origin and stride */
            int64_t k = n->op == OP_ACOPY || n->op == OP_AFCOPY ? (d.shape[0] < f.shape[0] ? d.shape[0] : f.shape[0]) : 1;
            for (int64_t z = 0; z < k; z++) {
                int64_t dz = at1(&d, z), fz = at1(&f, z);
                if (dz < 0 || dz >= d.span || fz < 0 || fz >= f.span)
                    return cint_index_check(ctx, s, ICHECK[0], z, 0);
                memmove(d.base + dz * n->value, f.base + fz * n->value, (size_t)n->value * sizeof *d.base);
            }
            break;
        }
        case OP_CSHAPE:
        case OP_BSHAPE: {
            /* per dimension: the argument's dimension (16 more with the dimension operand) and
             * the expected view's */
            ex_ref a = place(x, fr, o), c = o[2].kind == OD_IMM ? a : place(x, fr, o + 2);
            int64_t fd = n->nopd > 3u ? o[3].value : 0, e1 = n->nopd > 3u ? o[4].value : 0;
            int64_t want = o[2].kind == OD_IMM ? o[2].value : dext(&c, e1, n->view >> 1), have = dext(&a, fd % 16, n->view & 1);
            st = n->op == OP_CSHAPE ? cint_copy_shape_check(ctx, s, 0, want, have)
                 : fd >= 16 ? cint_shape_check_dim(ctx, s, (uint32_t)o[1].value, (uint32_t)(fd % 16), want, have)
                            : cint_shape_check(ctx, s, (uint32_t)o[1].value, want, have);
            break;
        }
        case OP_CALL:
            st = call(x, ctx, fr, n, s);
            break;
        case OP_HCALL:
            st = host_call(x, ctx, fr, n, s);
            break;
        case OP_KCALL:
            st = dispatch(x, ctx, fr, n, s);
            break;
        case OP_ASSERT:
            if (vget(fr, o) == 0u) {
                const si_node *c = o->kind == OD_TEMP ? &nodes[fr->f->first + (uint32_t)n->jump] : NULL;
                if (c != NULL && n->jump >= 0 && is_wide(c->t1)) {   /* by their limbs (box 15; RT-OQ-55) */
                    const si_opd *co = m->opds + c->opd;
                    st = cint_rt_fault_assert_wide(ctx, s, tag_of(c->t1), wget(fr, co), wget(fr, co + 1));
                } else if (c != NULL && n->jump >= 0) {
                    const si_opd *co = m->opds + c->opd;
                    st = cint_rt_fault_assert(ctx, s, tag_of(c->t1), vget(fr, co), vget(fr, co + 1));
                } else {
                    st = cint_rt_fault_assert(ctx, s, 0u, 0u, 0u);
                }
            }
            break;
        case OP_FUEL:
            cint_rt_interrupt_poll(ctx);   /* cint_ctx_interrupt (rt/cint_state.h) */
            st = cint_fuel_charge(ctx, s, (uint64_t)n->value);
            break;
        case OP_RETURN:
            fr->rtag = n->form == 1 ? vget(fr, o) : 0u;   /* return.error: the tag alone (box 12) */
            if (n->form == 0 && n->nopd > 0 && fr->f->result.kind == TK_SCALAR && is_wide(fr->f->result.scalar)) {
                memcpy(result, wget(fr, o), limbs(fr->f->result.scalar) * sizeof(uint64_t));
            } else if (n->form == 0 && n->nopd > 0 && fr->f->result.kind == TK_SCALAR) {
                *result = vget(fr, o);
            } else if (n->form == 0 && n->nopd > 0 && fr->f->result.kind == TK_HANDLE) {
                memcpy(result, hcells(fr, o), sizeof(cint_handle));
            } else if (n->form == 0 && n->nopd > 0) {   /* a struct, into the caller's slot */
                memmove(result, place(x, fr, o).base, (size_t)n->value * sizeof *result);
            }
            frame_return(ctx, fr);
            return 1;
        case OP_ETAG:
            fr->t[n->dst] = fr->etag;
            break;
        case OP_MEMBER:
        case OP_HVIEW:
        case OP_HLEASE:
        case OP_PDEREF:
            st = mem_op(x, ctx, fr, n, s);
            break;
        case OP_ELEAVE:   /* an error leaving a test (LS-315): the test fails with it */
            cint_rt_error_result(ctx, &x->mods[fr->mi].sets[n->value], vget(fr, o));
            break;
        case OP_WIDEN: {   /* the first run of the source's tags that holds the tag, else the last */
            const si_widen *w = &m->widens[n->value];
            const int64_t *run = m->runs + w->run;
            uint64_t tag = vget(fr, o);
            uint32_t k = 0;
            while (k + 1u < w->nrun && tag > (uint64_t)run[2u * k]) k++;
            fr->t[n->dst] = canon(n->t2, tag + (uint64_t)run[2u * k + 1u]);
            break;
        }
        case OP_PRINT_BEGIN:
            st = cint_rt_print_begin(ctx, s);
            break;
        case OP_PRINT_BYTES:
            st = cint_rt_print_bytes(ctx, x->mods[fr->mi].bytes + n->jump, (size_t)n->value);
            break;
        case OP_PRINT_VALUE:
            if (sc_is_q(n->ty.scalar) && is_wide(n->ty.scalar)) {   /* fixed point of 128 bits (RT-OQ-63) */
                st = cint_rt_print_q_limbs(ctx, tag_of(n->ty.scalar), wget(fr, o), (uint32_t)n->fmt[0],
                                           n->fmt[0] == 0 ? 32u : (uint32_t)n->fmt[1], (uint64_t)n->fmt[2],
                                           (uint64_t)n->fmt[3]);
                break;
            }
            if (is_wide(n->ty.scalar)) {   /* its decimal or its formatted text, from its limbs (box 15; RT-OQ-55) */
                st = n->fmt[0] == 0 ? cint_rt_print_wide(ctx, tag_of(n->ty.scalar), wget(fr, o))
                                    : cint_rt_print_format_wide(ctx, tag_of(n->ty.scalar), wget(fr, o), (uint32_t)n->fmt[0],
                                                                (uint32_t)n->fmt[1], (uint64_t)n->fmt[2],
                                                                (uint64_t)n->fmt[3]);
                break;
            }
            v[0] = vget(fr, o);
            if (sc_is_q(n->ty.scalar)) {   /* its decimal, or as a hole's form gives it (box 15) */
                st = cint_rt_print_format_fixed(ctx, tag_of(n->ty.scalar), v[0], (uint32_t)n->fmt[0],
                                                n->fmt[0] == 0 ? 32u : (uint32_t)n->fmt[1], (uint64_t)n->fmt[2],
                                                (uint64_t)n->fmt[3]);
                break;
            }
            st = n->fmt[0] == 0 ? cint_rt_print_value(ctx, tag_of(n->ty.scalar), v[0])
                                : cint_rt_print_format(ctx, tag_of(n->ty.scalar), v[0], (uint32_t)n->fmt[0],
                                                       (uint32_t)n->fmt[1], (uint64_t)n->fmt[2], (uint64_t)n->fmt[3]);
            break;
        case OP_PRINT_END:
            st = cint_rt_print_end(ctx);
            break;
        case OP_THEN:
            if (vget(fr, o) == 0u) pc = (uint32_t)n->link + 1u;
            break;
        case OP_WHILE:
            if (vget(fr, o) == 0u) pc = (uint32_t)n->jump;
            break;
        case OP_SWITCH: {
            uint64_t sv = vget(fr, o + 1);
            uint32_t c = pc, dflt = 0;
            pc = (uint32_t)n->link + 1u;
            for (; c < (uint32_t)n->link; c = (uint32_t)nodes[c].link + 1u) {
                const si_opd *it = m->opds + nodes[c].opd;
                if (nodes[c].op == OP_DEFAULT) dflt = c + 1u;
                for (uint32_t k = 0; nodes[c].op == OP_CASE && k + 1u < nodes[c].nopd; k += 2u) {
                    uint64_t lo = (uint64_t)it[k].value, hi = (uint64_t)it[k + 1u].value;
                    int in = is_signed(n->t1) ? cint_rt_sext(lo, 64u) <= cint_rt_sext(sv, 64u) &&
                                                    cint_rt_sext(sv, 64u) <= cint_rt_sext(hi, 64u)
                                              : lo <= sv && sv <= hi;
                    if (in) {
                        dflt = c + 1u;
                        c = (uint32_t)n->link;
                        break;
                    }
                }
                if (c == (uint32_t)n->link) break;
            }
            if (dflt != 0) pc = dflt;
            break;
        }
        case OP_BREAK:
        case OP_CONTINUE:
            pc = (uint32_t)n->jump;
            break;
        case OP_CLOSE:
            if (pc - 1u == end) {
                frame_return(ctx, fr);
                return 1;
            }
            if (n->jump >= 0)
                pc = nodes[pc - 2u].op == OP_FALLTHROUGH ? pc + 1u : (uint32_t)n->jump;
            break;
        default:   /* local, data, discard; the openers, next and fallthrough go on */
            st = n->op > OP_KCALL ? kernel_op(x, ctx, fr, n, s) : n->op >= OP_VBEGIN ? view_op(x, ctx, fr, n, s) : 1;
            break;
        }
        if (st != 1) return st;
    }
}

/* run_body; after a fault of a kernel, its fault exit: the dispatch's address in the record
 * (back_c.ci, c_func; SPEC-02 F-8, ruling R9). */
static int run(cint_sir_image *x, cint_ctx *ctx, ex_frame *fr, uint64_t *result)
{
    int st = run_body(x, ctx, fr, result);
    if (st == 0 && fr->f->kind == FN_KERNEL) {
        cint_rt_dispatch_fault(ctx, x->mods[fr->mi].fns[fr->f - x->p->mods[fr->mi].funcs].kname, fr->kd, fr->kph, fr->kwi,
                               fr->kst);
    }
    return st;
}

/* ------------------------------------------------------------------ entries */

static cint_status entry(cint_sir_image *x, uint32_t mi, uint32_t fi, cint_ctx *ctx, int64_t fuel, int64_t depth,
                         const uint64_t *args, uint64_t *result)
{
    const si_module *m = &x->p->mods[mi];
    const si_func *f = &m->funcs[fi];
    cint_site s = {mi, m->nodes[f->first].site};
    uint64_t r = 0;
    cint_status st = cint_rt_entry_begin(ctx, s, fuel, depth);
    if (st != CINT_OK) return st;
    x->step = 0;
    ex_frame *fr = frame_new(x, mi, fi);
    uint64_t *acells = NULL;   /* the program arguments of `main(in U8[_] args)`, one cell per byte */
    int ok = fr != NULL ? 0 : -1;
    for (uint32_t p = 0, a = 0; ok == 0 && p < f->nparam; p++) {   /* a Grants: the context's set, 0 */
        const si_type *t = &m->params[f->param + p].ty;
        if (t->kind == TK_VIEW) {   /* the program entry's arguments (OQ-306; RT-OQ-76) */
            size_t n = 0;
            const uint8_t *b = cint_ctx_args(ctx, &n);
            if ((acells = malloc((n > 0 ? n : 1) * sizeof *acells)) == NULL) { ok = -1; break; }
            for (size_t i = 0; i < n; i++) acells[i] = b[i];
            fr->par[p] = ref1(acells, (int64_t)n);
        } else {
            *fr->par[p].base = t->grants ? 0u : canon(t->scalar, args[a++]);
        }
    }
    ok = ok == 0 ? run(x, ctx, fr, &r) : -1;
    uint64_t tag = fr != NULL ? fr->rtag : 0u;
    free(acells);
    free(fr);
    if (ok == 1 && tag != 0u && f->eset >= 0) {   /* the error the entry leaves with (box 12) */
        cint_rt_error_result(ctx, &x->mods[mi].sets[f->eset], tag);
    }
    arrays_out(x, ctx, 1);
    st = cint_rt_entry_end(ctx);
    if (ok == 1 && st == CINT_OK && result != NULL && tag == 0u) *result = r;
    return ok < 0 && st != CINT_FAULT ? CINT_RESOURCE : st;
}

static cint_status entry_k(uint32_t k, cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args,
                           uint64_t *result)
{
    return entry(g_img, g_img->emod, g_img->efn[k], ctx, fuel, depth, args, result);
}

#define EX_TRAMP(h, l)                                                                         \
    static cint_status tramp_##h##l(cint_ctx *c, int64_t f, int64_t d, const uint64_t *a, uint64_t *r) \
    {                                                                                          \
        return entry_k(0x##h##l##u, c, f, d, a, r);                                            \
    }
#define EX_TRAMP16(h)                                                                          \
    EX_TRAMP(h, 0) EX_TRAMP(h, 1) EX_TRAMP(h, 2) EX_TRAMP(h, 3) EX_TRAMP(h, 4) EX_TRAMP(h, 5)  \
    EX_TRAMP(h, 6) EX_TRAMP(h, 7) EX_TRAMP(h, 8) EX_TRAMP(h, 9) EX_TRAMP(h, a) EX_TRAMP(h, b)  \
    EX_TRAMP(h, c) EX_TRAMP(h, d) EX_TRAMP(h, e) EX_TRAMP(h, f)
#define EX_NAME16(h)                                                                           \
    tramp_##h##0, tramp_##h##1, tramp_##h##2, tramp_##h##3, tramp_##h##4, tramp_##h##5,        \
    tramp_##h##6, tramp_##h##7, tramp_##h##8, tramp_##h##9, tramp_##h##a, tramp_##h##b,        \
    tramp_##h##c, tramp_##h##d, tramp_##h##e, tramp_##h##f
EX_TRAMP16(0) EX_TRAMP16(1) EX_TRAMP16(2) EX_TRAMP16(3) EX_TRAMP16(4) EX_TRAMP16(5) EX_TRAMP16(6)
EX_TRAMP16(7) EX_TRAMP16(8) EX_TRAMP16(9) EX_TRAMP16(a) EX_TRAMP16(b) EX_TRAMP16(c) EX_TRAMP16(d)
EX_TRAMP16(e) EX_TRAMP16(f)
static const cint_observer_fn TRAMPS[EX_ENTRIES] = {
    EX_NAME16(0), EX_NAME16(1), EX_NAME16(2), EX_NAME16(3), EX_NAME16(4), EX_NAME16(5), EX_NAME16(6),
    EX_NAME16(7), EX_NAME16(8), EX_NAME16(9), EX_NAME16(a), EX_NAME16(b), EX_NAME16(c), EX_NAME16(d),
    EX_NAME16(e), EX_NAME16(f)};

/* The program entry of the root module, as a generated C main runs it. */
static cint_status main_entry(cint_ctx *ctx, int64_t fuel)
{
    return entry(g_img, g_img->p->nmod - 1u, (uint32_t)g_img->main_fn, ctx, fuel, CINT_DEPTH_FROM_CONFIG, NULL, NULL);
}

/* ------------------------------------------------------------------ preparing */

static int refuse(cint_sir_error *err, const si_module *m, uint32_t line, int st, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    snprintf(err->path, sizeof err->path, "%s", m->file);
    err->line = line;
    err->col = 0;
    vsnprintf(err->text, sizeof err->text, fmt, ap);
    va_end(ap);
    return st;
}

/* Whether node n is a form this unit runs: not a switch over another module's variable,
 * whose type the text does not carry, nor a form compiled C has no helper for. Of the wide
 * built-ins (box 15, rt/cint_wide.h), a muldiv has none from a narrow type into a wide one,
 * mul_full one into twice the width, from I64 to I512 and from U64 to U256, uabs one from I128
 * to I512 into the unsigned type of the width, and rotl and rotr one of U128 to U512. */
static int runs(const si_node *n)
{
    if (n->op == OP_BUILTIN && (is_wide(n->t1) || is_wide(n->t2)) && !sc_is_q(n->t1) && !sc_is_q(n->t2)) {
        switch (n->sub) {
        case 12:
            return (n->t1 == SC_I64 && n->t2 == SC_I128) || (n->t1 == SC_U64 && n->t2 == SC_U128) ||
                   (is_wide(n->t1) && n->t1 != SC_I1024 && n->t1 != SC_U512 && n->t2 == n->t1 + 1);
        case 2: return wsigned(n->t1) && n->t1 != SC_I1024 && n->t2 == n->t1 + (SC_U128 - SC_I128);
        case 15: case 16: return is_wide(n->t1) && !wsigned(n->t1);
        default: return is_wide(n->t1);
        }
    }
    return n->op == OP_SWITCH ? n->t1 != SC_NONE
           : n->op != OP_BIN || n->sub >= BO_AND || n->form == FM_CHECKED || n->form == FM_RESULT ||
                 n->sub <= BO_MUL || (n->sub == BO_SHL && n->form == FM_WRAP);
}

/* Slot k of e takes c cells, unless it has them already. */
static int cells_for(ex_fn *e, uint32_t k, int64_t c)
{
    if (e->at[k] != EX_UNSET) return 1;
    if ((uint64_t)c > (uint64_t)(UINT32_MAX - e->ncell) / 2u) return 0;
    e->at[k] = e->ncell;
    e->ncell += (uint32_t)c;
    return 1;
}

/* `print.bytes "<text>"` decoded into b; returns the byte count. */
static size_t decode(const uint8_t *t, uint8_t *b)
{
    size_t n = 0;
    for (t++; *t != '"'; t++) {
        if (*t != '\\') {
            b[n++] = *t;
        } else if (t[1] == 'x') {
            unsigned h = 0;
            for (int k = 2; k < 4; k++)
                h = h * 16u + (unsigned)(t[k] <= '9' ? t[k] - '0' : t[k] - 'a' + 10);
            b[n++] = (uint8_t)h;
            t += 3;
        } else {
            t++;
            b[n++] = *t == 'n' ? '\n' : *t == 't' ? '\t' : *t;
        }
    }
    return n;
}

static int prepare_body(cint_sir_image *x, uint32_t mi, uint32_t fi, size_t *bytes_at, cint_sir_error *err);

/* The jumps of one function's structure (si_node.jump), its slots' cells, its data, and its
 * print text; or the first form this unit does not run, kept in its why. */
static int prepare_fn(cint_sir_image *x, uint32_t mi, uint32_t fi, size_t *bytes_at, cint_sir_error *err)
{
    ex_fn *e = &x->mods[mi].fns[fi];
    const si_module *m = &x->p->mods[mi];
    const si_func *f = &m->funcs[fi];
    int st = prepare_body(x, mi, fi, bytes_at, err);
    if (st == CINT_SIR_OK && f->kind == FN_KERNEL) {
        e->kname = malloc((size_t)m->dotted.len + f->name.len + 2u);
        if (e->kname == NULL) return refuse(err, m, 0, CINT_SIR_RESOURCE, "out of memory");
        snprintf(e->kname, (size_t)m->dotted.len + f->name.len + 2u, "%.*s.%.*s", (int)m->dotted.len,
                 (const char *)m->text + m->dotted.at, (int)f->name.len, (const char *)m->text + f->name.at);
    }
    if (st == CINT_SIR_UNSUPPORTED) {
        e->why = malloc(sizeof *e->why);
        if (e->why == NULL)
            return refuse(err, &x->p->mods[mi], 0, CINT_SIR_RESOURCE, "out of memory");
        *e->why = *err;
        st = CINT_SIR_OK;
    }
    return st;
}

/* Every function reached from entry function (mi, fi) by calls and dispatches runs in this unit, or *err
 * names the first form one of them uses that it does not run. */
static int reach(cint_sir_image *x, uint32_t mi, uint32_t fi, uint32_t *stack, cint_sir_error *err)
{
    uint32_t depth = 0;
    if (x->mods[mi].fns[fi].seen) return CINT_SIR_OK;
    x->mods[mi].fns[fi].seen = 1;
    stack[depth++] = mi;
    stack[depth++] = fi;
    while (depth > 0) {
        uint32_t f = stack[--depth], m = stack[--depth];
        const si_module *sm = &x->p->mods[m];
        const si_func *sf = &sm->funcs[f];
        if (x->mods[m].fns[f].why != NULL) {
            *err = *x->mods[m].fns[f].why;
            return CINT_SIR_UNSUPPORTED;
        }
        for (uint32_t i = sf->first; i < sf->first + sf->count; i++) {
            const si_node *n = &sm->nodes[i];
            if ((n->op == OP_CALL || n->op == OP_KCALL) && !x->mods[n->cmod].fns[n->cfn].seen) {
                x->mods[n->cmod].fns[n->cfn].seen = 1;
                stack[depth++] = n->cmod;
                stack[depth++] = n->cfn;
            }
        }
    }
    return CINT_SIR_OK;
}

/* Value operand o as a fast form reads it (EXV): a temporary, or a scalar parameter; else EX_UNSET. */
static uint32_t fast_val(const si_module *m, const si_func *f, const si_opd *o)
{
    return o->kind == OD_TEMP ? o->index
           : o->kind == OD_PARAM && m->params[f->param + o->index].ty.kind == TK_SCALAR ? o->index | EX_PAR
                                                                                        : EX_UNSET;
}

/* Node i's fast form, or EXF_NONE; bound marks the slots read through their place. */
static void fast_form(const si_module *m, const si_func *f, const ex_fn *e, const uint8_t *bound, uint32_t i, ex_ins *c)
{
    const si_node *n = &m->nodes[i];
    const si_opd *o = n->nopd > 0 ? m->opds + n->opd : NULL;
    uint32_t v0 = o != NULL ? fast_val(m, f, o) : EX_UNSET, v1 = n->nopd > 1 ? fast_val(m, f, o + 1) : EX_UNSET;
    uint32_t own = o != NULL && o->kind == OD_SLOT && !bound[o->index] ? e->at[o->index] : EX_UNSET;
    int scalar = n->ty.kind == TK_SCALAR, i64 = n->t1 == SC_I64 && v0 != EX_UNSET && v1 != EX_UNSET;
    *c = (ex_ins){EXF_NONE, n->t1, n->site, n->dst, v0, v1, n->nopd > 2 ? fast_val(m, f, o + 2) : EX_UNSET, n->value};
    switch (n->op) {
    case OP_IF: case OP_ARM: case OP_ELSE: case OP_LOOP: case OP_NEXT: case OP_BLOCK:
        c->code = EXF_NOP;
        break;
    case OP_CONST:
        c->code = scalar && !n->wide ? EXF_CONST : EXF_NONE;
        break;
    case OP_LOAD_LOCAL:
    case OP_STORE_LOCAL:
        c->code = !scalar || is_wide(n->ty.scalar) || own == EX_UNSET ? EXF_NONE : n->op == OP_LOAD_LOCAL ? EXF_LOADL : v1 != EX_UNSET ? EXF_STOREL
                                                                                                         : EXF_NONE;
        c->a = own;
        break;
    case OP_RETURN:
        c->code = n->form == 0 && f->result.kind == TK_SCALAR && !is_wide(f->result.scalar) && v0 != EX_UNSET ? EXF_RET
                                                                                                : EXF_NONE;
        break;
    case OP_CMP:
        c->code = i64 ? EXF_CMP : EXF_NONE;
        c->value = n->sub;
        break;
    case OP_WHILE:
    case OP_THEN:   /* while false, past the loop; then false, past the arm's `} then` */
        c->code = v0 != EX_UNSET ? EXF_JZ : EXF_NONE;
        c->value = n->op == OP_WHILE ? n->jump : n->link + 1;
        break;
    case OP_BREAK:
    case OP_CONTINUE:
        c->code = EXF_JUMP;
        c->value = n->jump;
        break;
    case OP_FUEL:
        c->code = EXF_FUEL;
        break;
    case OP_CLOSE:   /* a jump; the function's last close returns, and a fallthrough goes on */
        c->code = i != f->first + f->count - 1u && n->jump >= 0 && m->nodes[i - 1u].op != OP_FALLTHROUGH ? EXF_JUMP
                                                                                                       : EXF_NONE;
        c->value = n->jump;
        break;
    case OP_BIN:
        c->code = (uint8_t)(n->form == FM_CHECKED && i64 && n->sub <= BO_REM ? EXF_ADD + n->sub : EXF_NONE);
        break;
    case OP_ICHECK:   /* of a local array, or an array field of a local struct, by a scalar */
        if (own == EX_UNSET || n->rank > 1u || v1 == EX_UNSET) break;
        *c = (ex_ins){EXF_ICHECK, n->t1, n->site, -1, own, v1, EX_UNSET, n->nopd > 2u ? o[2].value : e->ext[o->index]};
        break;
    case OP_ILOAD:
    case OP_ISTORE:   /* an element of a local array */
        if (own == EX_UNSET || n->view || is_wide(n->t2) || v1 == EX_UNSET || (n->op == OP_ISTORE && c->src == EX_UNSET))
            break;
        c->code = n->op == OP_ILOAD ? EXF_ILOAD : EXF_ISTORE;
        c->a = own;
        c->value = e->ext[o->index];
        break;
    default:
        break;
    }
}

static int prepare_body(cint_sir_image *x, uint32_t mi, uint32_t fi, size_t *bytes_at, cint_sir_error *err)
{
    si_module *m = &x->p->mods[mi];
    si_func *f = &m->funcs[fi];
    ex_mod *em = &x->mods[mi];
    ex_fn *e = &em->fns[fi];
    uint32_t *stack = malloc(((size_t)f->count + 1u) * sizeof *stack), depth = 0;
    e->at = calloc((size_t)f->nslot + 1u, sizeof *e->at);
    e->ext = calloc((size_t)f->nslot + 1u, sizeof *e->ext);
    if (stack == NULL || e->at == NULL || e->ext == NULL) {
        free(stack);
        return refuse(err, m, f->line, CINT_SIR_RESOURCE, "out of memory");
    }
    for (uint32_t k = 0; k < f->nslot; k++) {
        e->at[k] = EX_UNSET;
        e->ext[k] = 1;
    }
    for (uint32_t i = f->first; i < f->first + f->count; i++) {
        si_node *n = &m->nodes[i];
        si_opd *o = m->opds != NULL ? m->opds + n->opd : NULL;
        const si_func *cf = n->op == OP_CALL ? &x->p->mods[n->cmod].funcs[n->cfn] : NULL;
        n->jump = -1;
        if (!runs(n)) {
            free(stack);
            return refuse(err, m, n->line, CINT_SIR_UNSUPPORTED, "%.*s is not run by this unit", (int)n->word.len,
                          (const char *)m->text + n->word.at);
        }
        /* a local's cells, and a hidden struct slot's where a call or a copy fills it */
        int fills = n->op == OP_LOCAL || n->op == OP_FCOPY_OUT || (n->op == OP_SCOPY && n->t1 != 0);
        int64_t need = fills ? n->value : 0;
        uint32_t sk = fills ? o->index : 0;
        if (cf != NULL && cf->result.kind == TK_STRUCT) {
            sk = o[n->nopd].index;   /* the `into` slot, after the operands a call counts */
            need = x->p->mods[cf->result.smod].structs[cf->result.sidx].cells;
        }
        if (n->op == OP_LOCAL) e->ext[sk] = n->ty.kind == TK_ARRAY ? n->ty.extent : 1;
        /* a kernel's scalars without a local line (sir_read.c, kernel_body): N, its extents, its
         * counter and its `over` variables, a cell each */
        int hidden = f->kind == FN_KERNEL && (n->op == OP_LOAD_LOCAL || n->op == OP_STORE_LOCAL || n->op == OP_KFUEL ||
                                              n->op == OP_KITEM);
        for (int64_t q = 0; hidden && q < (n->op == OP_KITEM ? o[1].value : 1); q++) {
            if (!cells_for(e, n->op == OP_KITEM ? o[2 + q].index : o->index, 1)) {
                free(stack);
                return refuse(err, m, n->line, CINT_SIR_RESOURCE, "the function's locals exceed the interpreter's frame");
            }
        }
        if (n->op == OP_HVIEW && !cells_for(e, o->index + 1u, (int64_t)(sizeof(cint_lease) / sizeof(uint64_t)))) {
            free(stack);   /* the lease of a view of an arena allocation (box 12) */
            return refuse(err, m, n->line, CINT_SIR_RESOURCE, "the function's locals exceed the interpreter's frame");
        }
        if ((fills || need > 0) && !cells_for(e, sk, need)) {
            free(stack);
            return refuse(err, m, n->line, CINT_SIR_RESOURCE, "the function's locals exceed the interpreter's frame");
        }
        switch (n->op) {
        case OP_DATA: {
            ex_ref *d = &em->data[em->ndata];
            uint64_t *base = calloc((size_t)n->value + 1u, sizeof *base);
            if (base == NULL) {
                free(stack);
                return refuse(err, m, n->line, CINT_SIR_RESOURCE, "out of memory");
            }
            *d = ref1(base, n->value);
            for (int64_t k = 0; k < n->value; k++) d->base[k] = (uint64_t)o[k].value;
            n->jump = (int32_t)em->ndata++;
            break;
        }
        case OP_PRINT_BYTES:
            n->jump = (int32_t)*bytes_at;
            *bytes_at += decode(m->text + n->word.at + n->word.len + 1u, em->bytes + *bytes_at);
            break;
        case OP_ASSERT:
            for (uint32_t c = f->first; o->kind == OD_TEMP && c < i; c++) {
                if (m->nodes[c].dst == (int32_t)o->index && m->nodes[c].op != OP_DATA)
                    n->jump = m->nodes[c].op == OP_CMP ? (int32_t)(c - f->first) : -1;
            }
            break;
        case OP_IF: case OP_ARM: case OP_ELSE: case OP_LOOP: case OP_SWITCH: case OP_CASE: case OP_DEFAULT: case OP_BLOCK:
            stack[depth++] = i;
            break;
        case OP_THEN:
            stack[depth - 1u] = i;
            n->jump = m->nodes[stack[depth - 2u]].link + 1;   /* past the if */
            break;
        case OP_WHILE:
            n->jump = m->nodes[stack[depth - 1u]].link + 1;
            break;
        case OP_NEXT:
            m->nodes[stack[depth - 1u]].jump = (int32_t)i;   /* the loop's next */
            break;
        case OP_BREAK:
        case OP_CONTINUE:
            for (uint32_t k = depth; k-- > 0;) {
                const si_node *t = &m->nodes[stack[k]];
                if ((t->op == OP_LOOP || t->op == OP_SWITCH || t->op == OP_BLOCK) && m->opds[t->opd].value == o->value) {
                    n->jump = (int32_t)stack[k];   /* resolved below */
                    break;
                }
            }
            break;
        case OP_CLOSE:
            if (depth > 0) {
                const si_node *t = &m->nodes[stack[--depth]];
                n->jump = t->op == OP_THEN ? t->jump : t->op == OP_LOOP ? (int32_t)stack[depth] + 1
                          : t->op == OP_CASE ? m->nodes[stack[depth - 1u]].link + 1 : -1;
            }
            break;
        default:
            break;
        }
    }
    for (uint32_t i = f->first; i < f->first + f->count; i++) {
        si_node *n = &m->nodes[i];
        if (n->op == OP_BREAK || n->op == OP_CONTINUE) {
            const si_node *t = &m->nodes[n->jump];
            n->jump = n->op == OP_CONTINUE ? t->jump + 1 : t->link + 1;
        }
    }
    free(stack);
    /* the wide temporaries' limbs (box 15), each defined once; a load of another module's wide
     * variable, whose type the text does not carry, takes its limbs here */
    e->wat = calloc((size_t)f->ntemp + 1u, sizeof *e->wat);
    if (e->wat == NULL) return refuse(err, m, f->line, CINT_SIR_RESOURCE, "out of memory");
    for (uint32_t i = f->first; i < f->first + f->count; i++) {
        si_node *n = &m->nodes[i];
        const si_opd *o = n->nopd > 0 ? m->opds + n->opd : NULL;
        if (n->op == OP_LOAD_GLOBAL && o->kind == OD_IMPORT) {
            int sc = x->p->mods[o->value].globals[o->index].scalar;
            n->wide = (uint8_t)(is_wide(sc) ? limbs(sc) : 0u);
        }
        if (n->wide == 0 || n->dst < 0) continue;
        if (e->nwcell > UINT32_MAX / 2u)
            return refuse(err, m, n->line, CINT_SIR_RESOURCE, "the function's locals exceed the interpreter's frame");
        e->wat[n->dst] = e->nwcell;
        e->nwcell += n->wide;
    }
    /* the fast forms. A slot that a node binds to other storage (a reference, a view, a
     * dereference, a lease) is read through its place; any other local slot through its own cells */
    uint8_t *bound = calloc((size_t)f->nslot + 1u, 1u);
    if (bound == NULL) return refuse(err, m, f->line, CINT_SIR_RESOURCE, "out of memory");
    for (uint32_t i = f->first; i < f->first + f->count; i++) {
        const si_node *n = &m->nodes[i];
        const si_opd *o = n->nopd > 0 ? m->opds + n->opd : NULL;
        if (o != NULL && o->kind == OD_SLOT &&
            (n->op == OP_FADDR || n->op == OP_AFADDR || n->op == OP_EADDR || n->op >= OP_VBEGIN))
            bound[o->index] = 1;
    }
    for (uint32_t i = f->first; f->kind != FN_KERNEL && i < f->first + f->count; i++)
        fast_form(m, f, e, bound, i, &em->code[i]);
    free(bound);
    /* data operands by the data ordinal of their function's data node */
    for (uint32_t i = f->first; i < f->first + f->count; i++) {
        const si_node *n = &m->nodes[i];
        for (uint32_t k = 0; n->op != OP_DATA && k < n->nopd; k++) {
            si_opd *o = &m->opds[n->opd + k];
            for (uint32_t d = f->first; o->kind == OD_DATA && d < i; d++) {
                if (m->nodes[d].op == OP_DATA && m->nodes[d].dst == (int32_t)o->index) {
                    o->index = (uint32_t)m->nodes[d].jump;
                    break;
                }
            }
        }
    }
    return CINT_SIR_OK;
}

static int same(const uint8_t *a, si_name x, const uint8_t *b, si_name y)
{
    return x.len == y.len && memcmp(a + x.at, b + y.at, x.len) == 0;
}

/* `@<module>.<name>` of another module: operand value the module, index the global. */
static int resolve_global(cint_sir_image *x, si_module *m, si_opd *o, uint32_t line, cint_sir_error *err)
{
    si_name mod = o->name, var;
    while (mod.len > 0 && m->text[mod.at + mod.len - 1u] != '.') mod.len--;
    var.at = mod.at + mod.len;
    var.len = o->name.len - mod.len;
    mod.len = mod.len > 0 ? mod.len - 1u : 0u;
    for (uint32_t k = 0; k < x->p->nmod; k++) {
        const si_module *g = &x->p->mods[k];
        for (uint32_t i = 0; same(g->text, g->dotted, m->text, mod) && i < g->nglobal; i++) {
            if (same(g->text, g->globals[i].name, m->text, var)) {
                o->value = k;
                o->index = i;
                return g->globals[i].handle ? refuse(err, m, line, CINT_SIR_INPUT, "@%.*s is another module's Handle",
                                                     (int)o->name.len, (const char *)m->text + o->name.at)
                                            : CINT_SIR_OK;
            }
        }
    }
    return refuse(err, m, line, CINT_SIR_INPUT, "no module variable @%.*s", (int)o->name.len,
                  (const char *)m->text + o->name.at);
}

/* Writes n bytes, or counts them when b->p is NULL. */
typedef struct ty_out {
    uint8_t *p;
    size_t n;
} ty_out;

static void ty_w(ty_out *b, const void *v, size_t n)
{
    if (b->p != NULL) memcpy(b->p + b->n, v, n);
    b->n += n;
}

static void ty_u(ty_out *b, uint64_t v, unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        uint8_t c = (uint8_t)(v >> (8u * i));
        ty_w(b, &c, 1u);
    }
}

/* A field's or an element's type: a scalar's tag, 75 for a Handle, or 71 and the definition
 * index of a struct (num, by the program's struct number base[smod] + sidx). */
static void ty_item(ty_out *b, const si_type *t, const uint32_t *base, const uint32_t *num)
{
    if (t->kind == TK_HANDLE) {
        ty_u(b, CINT_TAG_HANDLE, 1u);
    } else if (t->scalar != SC_NONE) {
        ty_u(b, tag_of(t->scalar), 1u);
    } else {
        ty_u(b, 0x71u, 1u);
        ty_u(b, num[base[t->smod] + t->sidx], 4u);
    }
}

/* The element type of arena or pool type e, or the type of module-level struct e (box 15), into
 * d->type, and its layout in cells into d->layout (rt/cint_mem.h; decision 2026-10-08 on
 * OQ-244): the struct definitions the element reaches, each after those its fields hold, fields
 * in declaration order (SPEC-03 A-21), then the type, as compiled C gives them from the same
 * struct lines (compiler/back_c.ci, mem_def); per definition its cells and each field's first
 * cell, in bytes; every scalar in a cell (cell 8). False when memory runs out. */
static bool mem_type(const cint_sir_program *p, const si_type *e, cint_mem_decl *d)
{
    uint32_t total = 0, ndef = 0, depth = 0;
    for (uint32_t k = 0; k < p->nmod; k++) total += p->mods[k].nstruct;
    uint32_t *base = calloc((size_t)p->nmod + 1u, sizeof *base), *num = malloc(((size_t)total + 1u) * sizeof *num);
    uint32_t *stack = malloc(((size_t)total + 1u) * 3u * sizeof *stack), *order = stack + total + 1u;
    uint32_t *cur = order + total + 1u;
    bool ok = base != NULL && num != NULL && stack != NULL;
    for (uint32_t k = 1; ok && k < p->nmod; k++) base[k] = base[k - 1u] + p->mods[k - 1u].nstruct;
    for (uint32_t g = 0; ok && g < total; g++) num[g] = UINT32_MAX;
    if (ok && e->scalar == SC_NONE) {
        stack[depth] = base[e->smod] + e->sidx;
        cur[depth++] = 0;
        num[stack[0]] = UINT32_MAX - 1u;
    }
    while (ok && depth > 0) {   /* post-order: a struct after the structs its fields hold */
        uint32_t g = stack[depth - 1u], mi = 0;
        while (mi + 1u < p->nmod && base[mi + 1u] <= g) mi++;
        const si_module *m = &p->mods[mi];
        const si_struct *ss = &m->structs[g - base[mi]];
        if (cur[depth - 1u] < ss->nfield) {
            const si_type *t = &m->fields[ss->field + cur[depth - 1u]++].ty;
            uint32_t h = t->kind == TK_STRUCT || (t->kind == TK_ARRAY && t->scalar == SC_NONE) ? base[t->smod] + t->sidx
                                                                                               : UINT32_MAX;
            if (h != UINT32_MAX && num[h] == UINT32_MAX) {
                num[h] = UINT32_MAX - 1u;
                stack[depth] = h;
                cur[depth++] = 0;
            }
            continue;
        }
        order[ndef] = g;
        num[g] = ndef++;
        depth--;
    }
    for (int pass = 0; ok && pass < 2; pass++) {
        ty_out b = {pass == 0 ? NULL : (uint8_t *)malloc(d->type_len), 0u};
        size_t nl = 0;
        uint32_t *lay = pass == 0 ? NULL : malloc(((size_t)d->layout_len + 1u) * sizeof *lay);
        ok = pass == 0 || (b.p != NULL && lay != NULL);
        ty_u(&b, ndef, 4u);
        for (uint32_t i = 0; ok && i < ndef; i++) {
            uint32_t g = order[i], mi = 0;
            while (mi + 1u < p->nmod && base[mi + 1u] <= g) mi++;
            const si_module *m = &p->mods[mi];
            const si_struct *ss = &m->structs[g - base[mi]];
            ty_u(&b, 0x71u, 1u);
            ty_u(&b, (uint64_t)m->dotted.len + 1u + ss->name.len, 4u);
            ty_w(&b, m->text + m->dotted.at, m->dotted.len);
            ty_w(&b, ".", 1u);
            ty_w(&b, m->text + ss->name.at, ss->name.len);
            ty_u(&b, 0u, 1u);
            ty_u(&b, ss->nfield, 4u);
            if (lay != NULL) lay[nl] = ss->cells * 8u;
            nl++;
            for (uint32_t j = 0; j < ss->nfield; j++) {
                const si_field *f = &m->fields[ss->field + j];
                ty_u(&b, f->name.len, 4u);
                ty_w(&b, m->text + f->name.at, f->name.len);
                ty_u(&b, 0u, 1u);
                if (f->ty.kind == TK_ARRAY) ty_u(&b, 0x52u, 1u);
                ty_item(&b, &f->ty, base, num);
                if (f->ty.kind == TK_ARRAY) {   /* its rank, then per dimension form 01, no size symbol */
                    ty_u(&b, f->rank, 1u);
                    for (uint32_t q = 0; q < f->rank; q++) {
                        ty_u(&b, 1u, 1u);
                        ty_u(&b, UINT32_MAX, 4u);
                        ty_u(&b, (uint64_t)f->dims[q], 8u);
                    }
                }
                if (lay != NULL) lay[nl] = f->off * 8u;
                nl++;
            }
        }
        if (e->scalar == SC_NONE) {
            ty_u(&b, 0x71u, 1u);
            ty_u(&b, ndef - 1u, 4u);
        } else {
            ty_item(&b, e, base, num);
        }
        ok = ok && b.n <= UINT32_MAX && nl <= UINT32_MAX;
        if (pass == 0) {
            d->type_len = (uint32_t)b.n;
            d->layout_len = (uint32_t)nl;
        } else if (ok) {
            d->type = b.p;
            d->layout = nl > 0u ? lay : NULL;
            if (nl == 0u) free(lay);
        } else {
            free(b.p);
            free(lay);
        }
    }
    d->cell = 8u;
    free(base);
    free(num);
    free(stack);
    return ok;
}

static int prepare_mod(cint_sir_image *x, uint32_t mi, cint_sir_error *err)
{
    si_module *m = &x->p->mods[mi];
    ex_mod *em = &x->mods[mi];
    size_t ops = 1, ndata = 0, bytes = 0, at = 0;
    for (uint32_t s = 1; s <= m->nsite; s++) ops += m->sites[s].op.len + 1u;
    for (uint32_t i = 0; i < m->nnode; i++) {
        ndata += m->nodes[i].op == OP_DATA;
        bytes += m->nodes[i].op == OP_PRINT_BYTES ? (size_t)m->nodes[i].value : 0u;
    }
    em->sites = calloc((size_t)m->nsite + 1u, sizeof *em->sites);
    em->ops = malloc(ops);
    em->fns = calloc((size_t)m->nfunc + 1u, sizeof *em->fns);
    em->code = calloc((size_t)m->nnode + 1u, sizeof *em->code);
    em->data = calloc(ndata + 1u, sizeof *em->data);
    em->bytes = malloc(bytes + 1u);
    /* module state, in declaration order, little-endian: a scalar in 8 bytes, a wide one in its
     * w / 8 (box 15), a Handle's cint_handle, a module-level array's elements at their width,
     * rounded up to 8 bytes, a module-level struct's scalars in 8 bytes each and a wide one's
     * limbs in as many (box 15), and the arrays' and structs' cells while an entry runs, a wide
     * element's limbs in as many */
    size_t sbytes = 0, ncell = 0;
    for (uint32_t g = 0; g < m->nglobal; g++) {
        int64_t e = m->globals[g].extent;
        int sc = m->globals[g].scalar;
        sbytes += m->globals[g].handle ? sizeof(cint_handle)
                  : m->globals[g].record ? (size_t)m->globals[g].cells * 8u
                  : e < 0 && is_wide(sc) ? elem_bytes(sc)
                  : e <= 0             ? 8u
                                       : ((size_t)e * elem_bytes(sc) + 7u) / 8u * 8u;
        ncell += e > 0 ? (size_t)e * limbs(sc) : (size_t)m->globals[g].cells;
    }
    if (sbytes > UINT32_MAX)
        return refuse(err, m, 0, CINT_SIR_RESOURCE, "the module state is larger than a cint_state block");
    em->vars = calloc((size_t)m->nglobal + 1u, sizeof *em->vars);
    em->init = calloc(sbytes + 1u, 1u);
    em->ext = calloc(4u * (size_t)m->nglobal + 1u, sizeof *em->ext);
    em->garr = calloc((size_t)m->nglobal + 1u, sizeof *em->garr);
    em->gtype = calloc((size_t)m->nglobal + 1u, sizeof *em->gtype);
    em->gcell = calloc(ncell + 1u, sizeof *em->gcell);
    if (em->sites == NULL || em->ops == NULL || em->fns == NULL || em->code == NULL || em->data == NULL ||
        em->bytes == NULL || em->vars == NULL || em->init == NULL || em->ext == NULL || em->garr == NULL ||
        em->gtype == NULL || em->gcell == NULL) {
        return refuse(err, m, 0, CINT_SIR_RESOURCE, "out of memory");
    }
    /* the site table of the runtime (cint_site_resolve), row 0 "no site" */
    em->ops[0] = '\0';
    em->sites[0].operation = em->ops;
    for (uint32_t s = 1, o = 1; s <= m->nsite; s++) {
        memcpy(em->ops + o, m->text + m->sites[s].op.at, m->sites[s].op.len);
        em->ops[o + m->sites[s].op.len] = '\0';
        em->sites[s].line = m->sites[s].line;
        em->sites[s].column = m->sites[s].col;
        em->sites[s].operation = em->ops + o;
        o += m->sites[s].op.len + 1u;
    }
    em->mod.path = m->path;
    em->mod.path_len = (uint32_t)strlen(m->path);
    em->mod.site_count = m->nsite + 1u;
    em->mod.sites = em->sites;
    sbytes = ncell = 0;
    for (uint32_t g = 0; g < m->nglobal; g++) {
        const si_global *sg = &m->globals[g];
        unsigned w = elem_bytes(sg->scalar);
        em->vars[g].name = (const char *)m->text + sg->name.at;
        em->vars[g].name_len = sg->name.len;
        em->vars[g].tag = tag_of(sg->scalar);
        em->vars[g].offset = (uint32_t)sbytes;
        if (sg->handle) {   /* the null handle: zero bytes (OQ-242) */
            em->vars[g].tag = CINT_TAG_HANDLE;
            sbytes += sizeof(cint_handle);
            continue;
        }
        if (sg->record) {   /* tag 71 and its type, its cells as an arena element's (OQ-260) */
            cint_mem_decl md;
            si_type ty;
            memset(&md, 0, sizeof md);
            memset(&ty, 0, sizeof ty);
            ty.kind = TK_STRUCT;
            ty.smod = sg->smod;
            ty.sidx = sg->sidx;
            if (!mem_type(x->p, &ty, &md)) return refuse(err, m, 0, CINT_SIR_RESOURCE, "out of memory");
            em->gtype[g] = (cint_state_type){md.type, md.layout, md.type_len, md.layout_len, 8u, 0u};
            em->vars[g].tag = CINT_TAG_RECORD;
            em->vars[g].type = &em->gtype[g];
            em->garr[g] = ref1(em->gcell + ncell, 1);
            for (uint32_t i = 0; i < sg->nitem; i++)
                state_store(em->init + sbytes + (size_t)i * 8u, SC_U64, (uint64_t)m->items[sg->item + i]);
            sbytes += (size_t)sg->cells * 8u;
            ncell += sg->cells;
            continue;
        }
        if (sg->extent < 0 && is_wide(sg->scalar)) {   /* its limbs are items (box 15) */
            if (sg->nitem > 0) cint_wide_store(em->init + sbytes, (const uint64_t *)(m->items + sg->item), sg->nitem);
            sbytes += w;
            continue;
        }
        if (sg->extent < 0) {
            state_store(em->init + sbytes, sg->scalar, (uint64_t)sg->value);
            sbytes += 8u;
            continue;
        }
        memcpy(&em->ext[4u * g], sg->dims, sizeof sg->dims);
        em->vars[g].rank = sg->rank;
        em->vars[g].extents = &em->ext[4u * g];
        em->garr[g] = ref1(em->gcell + ncell, sg->extent);
        if (is_wide(sg->scalar)) {   /* each element's limbs are items (box 15) */
            if (sg->nitem > 0) cint_wide_store(em->init + sbytes, (const uint64_t *)(m->items + sg->item), sg->nitem);
        } else {
            for (uint32_t i = 0; i < sg->nitem; i++)
                state_store(em->init + sbytes + (size_t)i * w, sg->scalar, (uint64_t)m->items[sg->item + i]);
        }
        sbytes += sg->extent == 0 ? 8u : ((size_t)sg->extent * w + 7u) / 8u * 8u;
        ncell += (size_t)sg->extent * limbs(sg->scalar);
    }
    /* the error sets (box 12): names into one block */
    size_t nb = 1;
    for (uint32_t k = 0; k < m->neset; k++) nb += m->esets[k].name.len + 1u;
    for (uint32_t v = 0; v < m->neval; v++) nb += m->evals[v].len + 1u;
    em->sets = calloc((size_t)m->neset + 1u, sizeof *em->sets);
    em->vnames = calloc((size_t)m->neval + 1u, sizeof *em->vnames);
    em->names = malloc(nb);
    if (em->sets == NULL || em->vnames == NULL || em->names == NULL)
        return refuse(err, m, 0, CINT_SIR_RESOURCE, "out of memory");
    char *q = em->names;
    for (uint32_t v = 0; v < m->neval; v++) {
        em->vnames[v] = q;
        memcpy(q, m->text + m->evals[v].at, m->evals[v].len);
        q += m->evals[v].len;
        *q++ = '\0';
    }
    for (uint32_t k = 0; k < m->neset; k++) {
        const si_eset *es = &m->esets[k];
        em->sets[k].name = q;
        em->sets[k].tag = tag_of(es->scalar);
        em->sets[k].count = es->nvalue;
        em->sets[k].values = em->vnames + es->value;
        memcpy(q, m->text + es->name.at, es->name.len);
        q += es->name.len;
        *q++ = '\0';
    }
    /* the arenas and pools (box 12) in declaration order, each element in its cells, a wide one in
     * a cell per limb (box 15) */
    em->memd = calloc((size_t)m->nmem + 1u, sizeof *em->memd);
    if (em->memd == NULL) return refuse(err, m, 0, CINT_SIR_RESOURCE, "out of memory");
    for (uint32_t k = 0; k < m->nmem; k++) {
        const si_type *e = &m->mems[k].ty;
        uint64_t c = e->scalar != SC_NONE ? limbs(e->scalar) : x->p->mods[e->smod].structs[e->sidx].cells;
        if (c > UINT32_MAX / sizeof(uint64_t))
            return refuse(err, m, m->mems[k].line, CINT_SIR_RESOURCE, "an element of more cells than the interpreter's limit");
        em->memd[k].kind = e->mem;
        em->memd[k].elem_bytes = (uint32_t)(c * sizeof(uint64_t));
        em->memd[k].capacity = m->mems[k].capacity;
        em->memd[k].name = (const char *)m->text + m->mems[k].name.at;  /* what a change of revision matches */
        em->memd[k].name_len = m->mems[k].name.len;
        if (!mem_type(x->p, e, &em->memd[k])) return refuse(err, m, 0, CINT_SIR_RESOURCE, "out of memory");
    }
    em->mem.count = m->nmem;
    em->mem.module = mi;
    em->mem.decls = em->memd;
    em->state.module = mi;
    em->state.var_count = m->nglobal;
    em->state.bytes = (uint32_t)sbytes;
    em->state.init = em->init;
    em->state.vars = em->vars;
    for (uint32_t i = 0; i < m->nnode; i++) {
        si_opd *o = m->opds != NULL ? m->opds + m->nodes[i].opd : NULL;
        int st = (m->nodes[i].op == OP_LOAD_GLOBAL || m->nodes[i].op == OP_STORE_GLOBAL) && o->kind == OD_IMPORT
                     ? resolve_global(x, m, o, m->nodes[i].line, err) : CINT_SIR_OK;
        if (st != CINT_SIR_OK) return st;
    }
    for (uint32_t f = 0; f < m->nfunc; f++) {
        int st = prepare_fn(x, mi, f, &at, err);
        if (st != CINT_SIR_OK) return st;
    }
    return CINT_SIR_OK;
}

/* Module mi's host import table from its extern rows, in their order, as compiled C writes it
 * (back_c.ci, c_host_table): each row's name, effect, parameter count, Grants included, and
 * signature bytes, by which context creation resolves it (rt/cint_rt_host.h 6f). */
static int prepare_hosts(cint_sir_image *x, uint32_t mi, cint_sir_error *err)
{
    const si_module *m = &x->p->mods[mi];
    ex_mod *em = &x->mods[mi];
    size_t bytes = 1, at = 0;
    for (uint32_t k = 0; k < m->nextern; k++) bytes += m->externs[k].row.len + m->externs[k].sig.len / 2u;
    em->hosts = calloc((size_t)m->nextern + 1u, sizeof *em->hosts);
    em->hbytes = malloc(bytes);
    if (em->hosts == NULL || em->hbytes == NULL) return refuse(err, m, 0, CINT_SIR_RESOURCE, "out of memory");
    for (uint32_t k = 0; k < m->nextern; k++) {
        const si_extern *e = &m->externs[k];
        cint_host_import *h = &em->hosts[k];
        h->name.bytes = (const char *)em->hbytes + at;
        h->name.len = (uint32_t)decode(m->text + e->row.at, em->hbytes + at);
        at += h->name.len;
        h->effect = e->effect;
        h->param_count = e->nparam;
        h->type_signature = em->hbytes + at;
        h->type_signature_len = e->sig.len / 2u;
        for (uint32_t i = 0; i < e->sig.len; i += 2u) {
            const uint8_t *c = m->text + e->sig.at + i;
            em->hbytes[at++] = (uint8_t)((c[0] <= '9' ? c[0] - '0' : c[0] - 'a' + 10) * 16 +
                                         (c[1] <= '9' ? c[1] - '0' : c[1] - 'a' + 10));
        }
    }
    em->host.count = m->nextern;
    em->host.imports = m->nextern > 0 ? em->hosts : NULL;
    x->htabs[mi] = &em->host;
    return CINT_SIR_OK;
}

/* Whether f takes scalars only, at most 16, and returns a scalar or nothing (CONF-13 X-3); a wide
 * one is not an entry here (box 15). */
static int observable(const si_module *m, const si_func *f)
{
    for (uint32_t p = 0; p < f->nparam; p++)
        if (m->params[f->param + p].ty.kind != TK_SCALAR || is_wide(m->params[f->param + p].ty.scalar)) return 0;
    return f->kind == FN_FUNC && f->exported && f->nparam <= 16u &&
           (f->result.kind == TK_NONE || (f->result.kind == TK_SCALAR && !is_wide(f->result.scalar)));
}

int cint_sir_prepare(cint_sir_program *p, cint_sir_image **out, cint_sir_error *err)
{
    cint_sir_image *x = calloc(1, sizeof *x);
    uint32_t nstate = 0;
    memset(err, 0, sizeof *err);
    *out = NULL;
    if (x == NULL) return refuse(err, &p->mods[0], 0, CINT_SIR_RESOURCE, "out of memory");
    x->p = p;
    x->mods = calloc(p->nmod, sizeof *x->mods);
    x->modtab = calloc(p->nmod, sizeof *x->modtab);
    x->states = calloc(p->nmod, sizeof *x->states);
    x->htabs = calloc(p->nmod, sizeof *x->htabs);
    if (x->mods == NULL || x->modtab == NULL || x->states == NULL || x->htabs == NULL) {
        cint_sir_release(x);
        return refuse(err, &p->mods[0], 0, CINT_SIR_RESOURCE, "out of memory");
    }
    for (uint32_t i = 0; i < p->nmod; i++) {
        int st = prepare_mod(x, i, err);
        if (st == CINT_SIR_OK) st = prepare_hosts(x, i, err);
        if (st != CINT_SIR_OK) {
            cint_sir_release(x);
            return st;
        }
        x->modtab[i] = &x->mods[i].mod;
    }
    x->program.module_count = p->nmod;
    x->program.modules = x->modtab;
    x->program.revision = p->revision;
    x->imports.module_count = p->nmod;
    x->imports.tables = x->htabs;
    /* state tables: the root module first, then the others in program order (CONF-11 rule
     * 10 as compiled C orders it, compiler/back_c.ci program_state) */
    for (uint32_t q = 0; q < p->nmod; q++) {
        uint32_t k = q == 0 ? p->nmod - 1u : q - 1u;
        if (p->mods[k].nglobal > 0) x->states[nstate++] = &x->mods[k].state;
    }
    x->table.count = nstate;
    x->table.states = x->states;
    x->table.memory = p->mods[p->nmod - 1u].nmem > 0 ? &x->mods[p->nmod - 1u].mem : NULL;  /* the root's */
    /* observer entries: the last module with observable functions */
    for (uint32_t k = p->nmod; k-- > 0 && x->observer.entry_count == 0;) {
        const si_module *m = &p->mods[k];
        for (uint32_t f = 0; f < m->nfunc; f++) {
            uint32_t e = x->observer.entry_count;
            if (!observable(m, &m->funcs[f])) {
                continue;
            }
            if (e == EX_ENTRIES) {
                cint_sir_release(x);
                return refuse(err, m, m->funcs[f].line, CINT_SIR_UNSUPPORTED, "more than %u observer entries",
                              EX_ENTRIES);
            }
            x->emod = k;
            x->efn[e] = f;
            x->etype[e][0] = tag_of(m->funcs[f].result.kind == TK_SCALAR ? m->funcs[f].result.scalar : 0);
            for (uint32_t a = 0; a < m->funcs[f].nparam; a++) {   /* a Grants is not an argument (back_c.ci) */
                if (!m->params[m->funcs[f].param + a].ty.grants)
                    x->etype[e][2u + x->etype[e][1]++] = tag_of(m->params[m->funcs[f].param + a].ty.scalar);
            }
            x->entries[e].module = k;
            x->entries[e].name_len = m->funcs[f].name.len;
            x->entries[e].name = (const char *)m->text + m->funcs[f].name.at;
            x->entries[e].types = x->etype[e];
            x->entries[e].call = TRAMPS[e];
            x->observer.entry_count++;
        }
    }
    x->observer.program = &x->program;
    x->observer.entries = x->entries;
    /* the root's program entry: its script, else `main()`, else its one test */
    const si_module *root = &p->mods[p->nmod - 1u];
    int32_t tests = 0, test = -1;
    x->main_fn = -1;
    for (uint32_t f = 0; f < root->nfunc; f++) {
        const si_func *g = &root->funcs[f];
        tests += g->kind == FN_TEST;
        test = g->kind == FN_TEST ? (int32_t)f : test;
        const si_type *p0 = g->nparam > 0 ? &root->params[g->param].ty : NULL;
        int bare = g->nparam == 0 || (g->nparam == 1 && (p0->grants || si_args_param(p0))) ||
                   (g->nparam == 2 && p0->grants && si_args_param(&root->params[g->param + 1].ty));
        if (g->kind == FN_SCRIPT || (x->main_fn < 0 && g->kind == FN_FUNC && bare && g->result.kind == TK_NONE &&
                                     same(root->text, g->name, (const uint8_t *)"main", (si_name){0, 4}))) {
            x->main_fn = (int32_t)f;
        }
    }
    if (x->main_fn < 0 && tests == 1) x->main_fn = test;
    /* what the entries reach runs here; a function nothing reaches may use any form */
    size_t nfn = 0;
    for (uint32_t k = 0; k < p->nmod; k++) nfn += p->mods[k].nfunc;
    uint32_t *stack = malloc((2u * nfn + 2u) * sizeof *stack);
    int st = stack != NULL ? CINT_SIR_OK : refuse(err, root, 0, CINT_SIR_RESOURCE, "out of memory");
    for (uint32_t e = 0; st == CINT_SIR_OK && e < x->observer.entry_count; e++)
        st = reach(x, x->emod, x->efn[e], stack, err);
    if (st == CINT_SIR_OK && x->main_fn >= 0)
        st = reach(x, p->nmod - 1u, (uint32_t)x->main_fn, stack, err);
    free(stack);
    if (st != CINT_SIR_OK) {
        cint_sir_release(x);
        return st;
    }
    g_img = x;
    *out = x;
    return CINT_SIR_OK;
}

void cint_sir_release(cint_sir_image *x)
{
    if (x == NULL) return;
    for (uint32_t i = 0; x->mods != NULL && i < x->p->nmod; i++) {
        ex_mod *em = &x->mods[i];
        for (uint32_t f = 0; em->fns != NULL && f < x->p->mods[i].nfunc; f++) {
            free(em->fns[f].at);
            free(em->fns[f].wat);
            free(em->fns[f].ext);
            free(em->fns[f].why);
            free(em->fns[f].kname);
            for (ex_frame *k = em->fns[f].spare, *nx; k != NULL; k = nx) {
                nx = k->next;
                free(k);
            }
        }
        for (uint32_t d = 0; d < em->ndata; d++) free(em->data[d].base);
        free(em->sites);
        free(em->ops);
        free(em->fns);
        free(em->code);
        free(em->data);
        free(em->bytes);
        free(em->vars);
        free(em->init);
        free(em->ext);
        free(em->garr);
        for (uint32_t g = 0; em->gtype != NULL && g < x->p->mods[i].nglobal; g++) {
            free((void *)em->gtype[g].type);
            free((void *)em->gtype[g].layout);
        }
        free(em->gtype);
        free(em->gcell);
        free(em->sets);
        for (uint32_t k = 0; em->memd != NULL && k < x->p->mods[i].nmem; k++) {
            free((void *)em->memd[k].type);
            free((void *)em->memd[k].layout);
        }
        free(em->memd);
        free(em->vnames);
        free(em->names);
        free(em->hosts);
        free(em->hbytes);
    }
    free(x->mods);
    free(x->modtab);
    free(x->states);
    free(x->htabs);
    if (g_img == x) g_img = NULL;
    free(x);
}

const cint_program *cint_sir_cint_program(const cint_sir_image *x)
{
    return &x->program;
}

const cint_observer *cint_sir_observer(const cint_sir_image *x)
{
    return &x->observer;
}

const cint_host_desc *cint_sir_imports(const cint_sir_image *x)
{
    return &x->imports;
}

const cint_state_table *cint_sir_state_table(const cint_sir_image *x)
{
    return x->table.count > 0 || x->table.memory != NULL ? &x->table : NULL;
}

cint_main_fn cint_sir_main(const cint_sir_image *x)
{
    return x->main_fn >= 0 ? main_entry : NULL;
}

int cint_sir_main_args(const cint_sir_image *x)
{
    const si_module *root = &x->p->mods[x->p->nmod - 1u];
    const si_func *f = x->main_fn >= 0 ? &root->funcs[x->main_fn] : NULL;
    return f != NULL && f->nparam > 0 && si_args_param(&root->params[f->param + f->nparam - 1].ty);
}

void cint_sir_trace(cint_sir_image *x, cint_sir_trace_fn fn, void *user)
{
    x->trace = fn;
    x->trace_user = user;
}

uint32_t cint_sir_dispatch_domain(const cint_sir_image *x, int64_t extents[4])
{
    for (uint32_t k = 0; k < 4u; k++) extents[k] = k < x->drank ? x->dext[k] : 0;
    return x->drank;
}
