/* sir.h: canonical SIR text in memory, for cpu-sir-interp (SPEC-09 BACK-01a).
 *
 * The reader (sir_read.c) builds these tables from the SIR text that cintc writes as
 * output 1 of each module (SPEC-09 SIR-09, SIR-09a) and from the program file
 * cint-program.sites, checks the SIR-10 rules the interpreter relies on, and
 * recomputes the revision identity (SIR-17). Names point into the module's text,
 * which stays loaded. The interpreter (sir_exec.c) runs the nodes. Status: Proposed, box
 * 13 units 1 to 3, 7a and 7b (docs/design/notes/2026-10-05-box13-interpreter-checkpoints.md,
 * section 9); box 12's error forms from unit 8b. */
#ifndef CINT_SIR_H
#define CINT_SIR_H

#include <stddef.h>
#include <stdint.h>

#include "cint_interp.h"

/* Scalar types, as SIR text spells them; I128 to I1024 and U128 to U512 are the wide integers of
 * box 15 (SPEC-01 IM-11; BX15-02), a value in w / 64 cells, limb 0 first (rt/cint_wide.h). The
 * fixed-point types of box 15 follow SC_COUNT (SPEC-01 IM-54, IM-55): Q<w - f>.<f> for storage w
 * of 8 to 64 bits and f below w is SC_Q + w - 8 + f, a value its storage integer in one cell. */
enum { SC_NONE, SC_BOOL, SC_I8, SC_I16, SC_I32, SC_I64, SC_U8, SC_U16, SC_U32, SC_U64,
       SC_I128, SC_I256, SC_I512, SC_I1024, SC_U128, SC_U256, SC_U512 };
#define SC_COUNT 17
#define SC_Q SC_COUNT

static inline int sc_is_q(int sc)
{
    return sc >= SC_Q && sc < SC_Q + 120;
}

static inline unsigned sc_qwidth(int sc)
{
    return sc < SC_Q + 8 ? 8u : sc < SC_Q + 24 ? 16u : sc < SC_Q + 56 ? 32u : 64u;
}

/* The fraction bits of a scalar: 0 for an integer. */
static inline unsigned sc_qfrac(int sc)
{
    return sc_is_q(sc) ? (unsigned)(sc - SC_Q) - (sc_qwidth(sc) - 8u) : 0u;
}

/* Type kinds. TK_UNKNOWN is another module's variable, whose type the text does not
 * carry; it matches any type. Box 12's memory (unit 8b): TK_HANDLE, a handle in four cells
 * (cint_handle); TK_MEM, an arena or a pool (si_type.mem) of an element type, its identifier
 * in one cell; TK_LEASE, the lease of a view of an arena allocation in five (cint_lease). */
enum { TK_NONE, TK_SCALAR, TK_STRUCT, TK_ARRAY, TK_VIEW, TK_UNKNOWN, TK_HANDLE, TK_MEM, TK_LEASE };

typedef struct si_name {
    uint32_t at;  /* offset in the module text */
    uint32_t len;
} si_name;

typedef struct si_type {
    uint8_t kind;
    uint8_t scalar;  /* the scalar, or the element scalar of an array or view (0: struct) */
    uint8_t inout;   /* an `inout` struct parameter */
    uint8_t out;     /* a kernel's scalar `out` parameter, which its caller passes by place */
    uint32_t smod;   /* the struct, or the element struct: its module and its struct line */
    uint32_t sidx;
    int64_t extent;  /* array or view: the extent; view: -1 for `_`, -2 - p for parameter p */
    uint8_t rank;    /* a view of box 09 (SPEC-02 V-1), whose index is an element offset: its rank,
                        1 to 4; 0 for a view parameter of rank 1 and for every other type */
    uint8_t mem;     /* TK_MEM: CINT_HANDLE_ARENA or CINT_HANDLE_POOL; scalar, smod and sidx give
                        its element */
} si_type;

/* A struct line (SIR-09a): its fields in declaration order, each at its first cell. A
 * scalar takes one cell, a wide integer one per limb, an array its extent times its element's. */
typedef struct si_field {
    si_name name;
    si_type ty;        /* an array of rank 2 to 4 (box 15) has the product of its extents */
    uint32_t off;
    uint32_t rank;     /* an array's rank, 1 to 4, else 0 */
    int64_t dims[4];   /* an array's extents, from dimension 0 */
} si_field;

typedef struct si_struct {
    si_name name;
    uint32_t field;    /* first field in the module's field table */
    uint32_t nfield;
    uint32_t cells;
    uint8_t state;     /* the reader's: 1 while its fields are measured, 2 after */
} si_struct;

/* Operand kinds. */
enum {
    OD_TEMP,      /* %t<N>: index N */
    OD_PARAM,     /* %<name>: index the parameter */
    OD_SLOT,      /* $<name>.<k> or $sc.<k>: index k */
    OD_GLOBAL,    /* @<name>: index the module's global */
    OD_IMPORT,    /* @<module>.<name>: name the whole dotted spelling */
    OD_DATA,      /* %s<k>: index k */
    OD_IMM,       /* a decimal number: value */
    OD_FIELD,     /* a field name: index its first cell, value its extent (1: no array) */
    OD_LABEL,     /* L<k>: index k */
    OD_EXTENT,    /* `extent` before a slot, parameter or %s<k>: the next operand */
    OD_MEM        /* @<name> of an arena or a pool line: index the line (si_module.mems) */
};

typedef struct si_opd {
    uint8_t kind;
    uint32_t index;
    int64_t value;
    si_name name;
} si_opd;

/* Node operations. The comments give the SIR-09a form. */
enum {
    OP_CONST, OP_LOAD_LOCAL, OP_STORE_LOCAL, OP_LOAD_GLOBAL, OP_STORE_GLOBAL, OP_LOCAL,
    OP_BIN, OP_UN, OP_CMP, OP_AS, OP_BUILTIN,
    OP_FLOAD, OP_FSTORE, OP_ICHECK, OP_ILOAD, OP_ISTORE, OP_ELOAD, OP_ESTORE, OP_FILOAD, OP_FISTORE,
    OP_SCOPY, OP_ACOPY, OP_FCOPY, OP_AFCOPY, OP_FCOPY_OUT, OP_FADDR, OP_AFADDR, OP_EADDR,
    OP_BSHAPE, OP_CSHAPE, OP_DATA, OP_CALL, OP_DISCARD, OP_ASSERT, OP_FUEL, OP_RETURN,
    OP_PRINT_BEGIN, OP_PRINT_BYTES, OP_PRINT_VALUE, OP_PRINT_END,
    OP_IF, OP_ARM, OP_THEN, OP_ELSE, OP_LOOP, OP_WHILE, OP_NEXT, OP_SWITCH, OP_CASE, OP_DEFAULT,
    OP_FALLTHROUGH, OP_BREAK, OP_CONTINUE, OP_CLOSE,
    OP_BLOCK,  /* block L<k> {: the region of a `defer` or `errdefer`, which break L<k> leaves (box 12) */
    /* box 09's views (compiler/OPEN.md CINTC-OQ-35, item 5) and the frame arena (CINTC-OQ-64) */
    OP_VBEGIN, OP_VPICK, OP_VSLICE, OP_VEND, OP_VTRANS, OP_VREV, OP_EOFF, OP_VCOPY, OP_REDUCE, OP_VEXT,
    OP_CALIAS, OP_VDIM, OP_VALLOC, OP_AINIT, OP_FCHARGE, OP_FMARK, OP_FRELEASE,
    /* box 15's `fill` and `reshape` (compiler/OPEN.md CINTC-OQ-35, item 11): view.fill, view.reshape */
    OP_VFILL, OP_VRESHAPE,
    /* kernels and their dispatch (box 09; box 13 unit 7b), in the order of back_c.ci's SO_KCALL
     * to SO_KPUB: dispatch, dispatch.entry, bind.shape and bind.alias in a kernel, bind.where,
     * dispatch.charge, dispatch.stage, reduce.init, dispatch.push, dispatch.item,
     * dispatch.step, reduce.add, dispatch.phase, reduce.final, dispatch.pop, dispatch.publish */
    OP_KCALL, OP_KENTRY, OP_KSHAPE, OP_KWHERE, OP_KALIAS, OP_KFUEL, OP_KSTAGE, OP_KACC, OP_KPUSH, OP_KITEM,
    OP_KSTEP, OP_KRED, OP_KPHASE, OP_KEPI, OP_KPOP, OP_KPUB,
    /* box 12's error forms (compiler/OPEN.md CINTC-OQ-63, item 1; box 13 unit 8b): error.tag,
     * error.leave, widen.<from>.<to>; return.error is OP_RETURN with form 1, and the result
     * forms are OP_BIN and OP_AS with form FM_RESULT */
    OP_ETAG, OP_ELEAVE, OP_WIDEN,
    /* box 12's memory (compiler/OPEN.md CINTC-OQ-64, item 10; unit 8b): a member, arena.alloc
     * to pool.remove (si_node.sub, MB_*), handle.view, handle.lease and pool.deref */
    OP_MEMBER, OP_HVIEW, OP_HLEASE, OP_PDEREF
};

/* OP_MEMBER sub-operations, in the order of compiler/resolve.ci's MB_ALLOC to MB_REMOVE. */
enum { MB_ALLOC, MB_ALLOC_RESULT, MB_CHILD, MB_RESET, MB_INSERT, MB_INSERT_RESULT, MB_REMOVE };

/* OP_BIN and OP_UN sub-operations, in the order of SPEC-09 6.2. */
enum { BO_ADD, BO_SUB, BO_MUL, BO_DIV, BO_REM, BO_SHL, BO_SHR, BO_AND, BO_OR, BO_XOR, BO_NEG, BO_NOT };
/* Forms of OP_BIN, OP_UN and OP_AS; FM_RESULT, `<op>.result.<t>` and `as.result.<from>.<to>`,
 * gives an ArithError tag to the error.tag after it (box 12); FM_RAW, `as.raw.<from>.<to>`, is a
 * fixed-point value as its storage integer or back, the same bits (box 15). */
enum { FM_CHECKED, FM_WRAP, FM_SAT, FM_RESULT, FM_RAW };
/* The fixed-point built-ins of box 15, numbered after rotr (16) of cint_rt.h section 13. */
enum { BI_SQRT = 17, BI_QMUL, BI_MUL_INT };
/* OP_CMP sub-operations. */
enum { CM_EQ, CM_NE, CM_LT, CM_LE, CM_GT, CM_GE };

typedef struct si_node {
    uint8_t op;
    uint8_t sub;    /* BO_*, CM_*, or the built-in's number (cint_rt.h section 13) */
    uint8_t form;   /* FM_* */
    uint8_t t1;     /* operand scalar of the operation name; switch: the scrutinee's;
                       struct.copy: 1 where it fills a hidden slot */
    uint8_t t2;     /* result scalar when it differs (as, uabs, mul_full, muldiv); index.load and
                       index.store: the element's */
    uint8_t t3;     /* the count's scalar (shl, shr, rotl, rotr); qmul and mul_int: the second
                       operand's (box 15) */
    uint8_t mode;   /* rounding mode, numbered from 1 as cint_rt.h section 13 numbers it */
    uint8_t view;   /* index.checked, index.load, index.store, bind.shape: the place is a view of
                       box 09; view.begin: 1 over a flat array; bind.where: the comparison (1 eq to
                       6 ge, as compiler/lowering.ci numbers CMP_*) */
    uint8_t rank;   /* view.begin, view.alloc: the rank of the view it writes; view.end, view.extent:
                       that of its place */
    uint8_t wide;   /* the limbs of the wide integer it defines (box 15), else 0 */
    int32_t dst;    /* the temporary or data number the node defines, or -1 */
    uint32_t site;  /* site ordinal, 0 for none */
    uint32_t guard; /* an index.load or .store, element or field.index form, element.address: the
                       index.checked of its place before it, whose site an access the text did
                       not check first faults at (sir_read.c, pair_check); UINT32_MAX for none (a
                       kernel's element form) */
    uint32_t line;  /* line in the text */
    uint32_t opd;   /* first operand in the module's operand table */
    uint32_t nopd;
    int32_t link;   /* opener: its `}`; `}`: its opener; arm: its `} then`; loop: its `next` */
    int32_t jump;   /* where execution goes on, set by the interpreter (sir_exec.c) */
    uint32_t cmod;  /* call: the callee's module and function */
    uint32_t cfn;
    si_name word;   /* the operation as the line spells it */
    si_type ty;     /* const, local, data, print.value */
    int64_t value;  /* const value (a wide one's first limb in the module's item table), fuel
                       amount, data byte count; the cells a struct node
                       copies, or an element's cells where it steps by elements; error.leave:
                       its set, widen: its conversion, each an index in the module's table */
    int64_t fmt[4]; /* print.value format: form, fill, width, scale; fmt[0] 0 for none; reduce.add
                       and reduce.final: the reduction's word, which the site names (at, length) */
} si_node;

enum { FN_FUNC, FN_SCRIPT, FN_TEST, FN_KERNEL };

typedef struct si_param {
    si_name name;
    si_type ty;
    uint32_t cells;    /* the frame's cells: 1 for a scalar, a struct's by value, 0 by place (a view,
                          an `inout` struct, a kernel's `out` scalar) */
} si_param;

typedef struct si_func {
    uint8_t kind;
    uint8_t exported;  /* the export mark (SIR-09a) */
    si_name name;      /* FN_FUNC: the function's name */
    int64_t test;      /* FN_TEST: the test's number */
    uint32_t param;    /* first parameter in the module's parameter table */
    uint32_t nparam;
    uint32_t pcells;   /* the cells of its parameters */
    si_type result;    /* TK_NONE when it has none; of an error union, the value's */
    uint8_t etag;      /* an error union result (box 12): its tag's scalar, else 0 */
    int32_t eset;      /* an entry's error set (` error E<k>`): its index in the module's table,
                          else -1 */
    uint32_t first;    /* first node */
    uint32_t count;    /* node count, the closing `}` included */
    uint32_t ntemp;
    uint32_t nslot;    /* one more than the largest slot number */
    uint32_t nmark;    /* its frame.mark nodes */
    uint8_t frame;     /* bit 0: it charges the frame arena (frame.charge); bit 1: it has run-time
                          storage (view.alloc, a kernel's dispatch.stage); both are given back when
                          it returns */
    uint8_t arrays;    /* 1 when it names a module-level array (box 13 MLA) or struct (box 15) of its
                          module */
    uint8_t handles;   /* 1 when a temporary holds a handle (box 12), in four cells of its own */
    uint32_t line;
} si_func;

typedef struct si_global {
    si_name name;
    uint8_t scalar;    /* the scalar, or a module-level array's element (box 13 MLA) */
    uint8_t handle;    /* 1 for a Handle, the null handle at start (OQ-242) */
    int64_t value;     /* the bits, sign-extended for signed types; a wide integer's limbs are
                          nitem items from item */
    int64_t extent;    /* an array's element count; -1 for a scalar */
    int64_t dims[4];   /* an array's extents, from dimension 0 (box 13 MLA: rank 1 to 4) */
    uint32_t rank;
    uint32_t item;     /* an array's or a struct's initial values: nitem of them from item in the
                          module's item table, each as value is, a wide integer's in its limbs;
                          none when it is zero-filled */
    uint32_t nitem;
    uint8_t record;    /* 1 for a module-level struct (box 15, OQ-260): struct sidx of module smod,
                          in cells cells, a scalar a cell; extent -1 */
    uint32_t smod;
    uint32_t sidx;
    uint32_t cells;
    uint32_t line;
} si_global;

/* An arena or a pool line (box 12; `arena @<name>: <element>, <capacity>`): its identifier
 * counts the lines of its kind from 1 (SPEC-03 M-13). */
typedef struct si_mem {
    si_name name;
    si_type ty;        /* TK_MEM, with its kind and element */
    int64_t capacity;  /* in elements */
    uint32_t id;
    uint32_t line;
} si_mem;

/* An error set (box 12; `error E<k>: <name> <type> <values>`): what an entry or a test leaves
 * with names, its values nvalue names from value in the module's value table, in tag order. */
typedef struct si_eset {
    int64_t k;         /* the number E<k> */
    si_name name;
    uint8_t scalar;    /* the tag's */
    uint32_t value;
    uint32_t nvalue;
} si_eset;

/* A conversion between error sets (box 12; `widen W<k>: <last> <adds>, ...`): per run of the
 * source's tags in tag order, from run in the module's run table, the run's last tag and what
 * it adds. */
typedef struct si_widen {
    int64_t k;
    uint32_t run;
    uint32_t nrun;
} si_widen;

typedef struct si_site {
    uint32_t line;
    uint32_t col;
    si_name op;
} si_site;

typedef struct si_module {
    char *path;        /* module path, e.g. arith/add.ci */
    char *file;        /* its SIR text, e.g. arith/add.sites */
    uint8_t *text;
    size_t len;
    si_name dotted;
    si_site *sites;    /* sites[0] unused; ordinals from 1 */
    uint32_t nsite;
    si_struct *structs;
    uint32_t nstruct;
    si_field *fields;
    uint32_t nfield;
    si_global *globals;
    uint32_t nglobal;
    int64_t *items;    /* the module-level arrays' initial values (si_global.item), and the limbs
                          of the wide constants (si_node.value) */
    uint32_t nitem;
    si_eset *esets;
    uint32_t neset;
    si_name *evals;    /* the error sets' values (si_eset.value) */
    uint32_t neval;
    si_widen *widens;
    uint32_t nwiden;
    si_mem *mems;      /* the arena and pool lines (box 12) */
    uint32_t nmem;
    int64_t *runs;     /* the conversions' runs, two numbers each (si_widen.run) */
    uint32_t nrun;
    si_func *funcs;
    uint32_t nfunc;
    si_node *nodes;
    uint32_t nnode;
    si_opd *opds;
    uint32_t nopd;
    si_param *params;
    uint32_t nparam;
} si_module;

struct cint_sir_program {
    si_module *mods;   /* in the order of cint-program.sites */
    uint32_t nmod;
    uint8_t revision[32];
};

#endif
