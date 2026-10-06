/* tables.h: the compiler state of cint-seed (SPEC-09 section 4, plan Task 1.5).
 *
 * Every table is allocated from the size of the input (SEED-11): token, node
 * and statement tables per module from its token count, the global tables
 * (types, structs, functions, symbols) by bounded growth. A table that cannot
 * grow ends compilation with diagnostic C9001, never a crash.
 *
 * Expression nodes of a module are stored in postfix order: the children of a
 * node come before it, and the subtree of node i is the contiguous range
 * nodes[first .. i]. A forward walk over that range visits the subtree in
 * evaluation order (left to right, SPEC-01 IM-92), and a backward walk visits
 * every parent before its children. Statements are a flat stream with explicit
 * end markers. Neither the parser nor the later passes recurse.
 */
#ifndef SEED_TABLES_H
#define SEED_TABLES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Types: fixed ids, then user types (struct, array, view) from TY_USER on. */
enum {
    TY_NONE = 0, /* untyped: a literal-only expression before context typing */
    TY_I8, TY_I16, TY_I32, TY_I64, TY_U8, TY_U16, TY_U32, TY_U64,
    TY_BOOL, TY_VOID, TY_Z, TY_MOD, TY_USER
};
enum { K_SCALAR, K_STRUCT, K_ARRAY, K_VIEW };

typedef struct seed_type {
    uint8_t kind;   /* K_STRUCT, K_ARRAY, K_VIEW */
    uint8_t perm;   /* view: 0 in, 1 inout */
    uint8_t lit;    /* view: a string literal (in U8[_]) */
    uint8_t pad;
    int32_t elem;   /* array and view element type */
    int32_t strct;  /* struct index */
    int32_t sp;     /* view extent: size parameter index, -1 for `_`, -2 for a constant */
    int64_t extent; /* array extent, or view constant extent */
} seed_type;

/* Tokens. */
enum { T_EOF, T_IDENT, T_KW, T_INT, T_STR, T_OP };
enum {
    KW_NONE, KW_AS, KW_ASSERT, KW_BREAK, KW_BY, KW_CASE, KW_CATCH, KW_CONST, KW_CONTINUE,
    KW_DEFAULT, KW_DEFER, KW_DO, KW_ELSE, KW_ENUM, KW_ERROR, KW_EXPECT_FAULT, KW_EXPORT,
    KW_FALLTHROUGH, KW_FALSE, KW_FOR, KW_IF, KW_IMPORT, KW_IN, KW_IN_PLACE, KW_INOUT,
    KW_KERNEL, KW_OUT, KW_OVER, KW_PROFILE, KW_REDUCE, KW_RETURN, KW_SCHEDULE,
    KW_STATIC_ASSERT, KW_STRUCT, KW_SWITCH, KW_TEST, KW_TRUE, KW_TRY, KW_TYPE, KW_VAR,
    KW_VOID, KW_WHERE, KW_WHILE, KW_DISTINCT, KW_SECRET, KW_COUNT
};
enum {
    OP_NONE,
    /* binary operators, in the order of the table in parse.c */
    OP_MUL, OP_DIV, OP_REM, OP_MULW, OP_MULS, OP_ADD, OP_SUB, OP_ADDW, OP_SUBW, OP_ADDS, OP_SUBS,
    OP_SHL, OP_SHR, OP_SHLW, OP_BAND, OP_BXOR, OP_BOR,
    OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE, OP_LAND, OP_LOR,
    /* assignment */
    OP_ASSIGN, OP_ADDA, OP_SUBA, OP_MULA, OP_DIVA, OP_REMA, OP_SHLA, OP_SHRA, OP_ANDA, OP_ORA,
    OP_XORA, OP_ADDWA, OP_SUBWA, OP_MULWA, OP_SHLWA, OP_ADDSA, OP_SUBSA, OP_MULSA,
    /* other punctuation */
    OP_INC, OP_DEC, OP_NOT, OP_TILDE, OP_ASW, OP_ASS, OP_ASQ, OP_DOTDOT, OP_DOTDOTEQ,
    OP_LPAREN, OP_RPAREN, OP_LBRACK, OP_RBRACK, OP_LBRACE, OP_RBRACE, OP_COMMA, OP_SEMI,
    OP_DOT, OP_COLON, OP_QUEST, OP_AT, OP_COUNT
};

typedef struct seed_tok {
    uint8_t kind;
    uint8_t sub;    /* keyword or operator code */
    uint8_t space;  /* whitespace or a comment precedes the token */
    uint8_t big;    /* integer literal: magnitude above UINT64_MAX */
    uint32_t line, col;
    uint32_t off, len;
    uint64_t val;   /* integer literal magnitude; string: offset in the string bytes */
    uint32_t slen;  /* string: decoded length */
    uint32_t pad;
} seed_tok;

/* Expression nodes. */
enum {
    N_LIT, N_BOOL, N_STR, N_NAME, N_UNARY, N_BIN, N_CMP, N_SC, N_LOGIC, N_CONV,
    N_FIELD, N_INDEX, N_CALL
};
#define NF_PAREN 1u   /* written in parentheses */
#define NF_NEG 2u     /* negative literal */
#define NF_CONST 4u   /* a constant expression (SPEC-01 IM-23) */
#define NF_COND 8u    /* below the right operand of a non-constant && or || */
#define NF_HASV 16u   /* the constant value is known */
#define NF_PLACE 32u  /* an assignable place */
#define NF_SKIP 64u   /* skipped by && or || in constant evaluation; checked afterwards (D-9) */

typedef struct seed_node {
    uint8_t kind;
    uint8_t op;
    uint8_t flags;
    uint8_t big;
    uint32_t tok;    /* operator, name or literal token: the position of the operation */
    uint32_t stok;   /* first token of the expression, `(` when parenthesized */
    int32_t first;   /* first node of the subtree */
    int32_t parent;  /* -1 for a root */
    int32_t a, b;    /* children; call: a = first argument slot, b = argument count */
    int32_t type;    /* result type */
    int32_t aux;     /* conversion typeref; field: field index; call: function or struct; string: length */
    int32_t sym;     /* name: symbol; call: qualifier token or -1 */
    int32_t site;    /* site ordinal, 0 for none */
    int32_t mc;      /* the maximal constant subexpression that contains it, or -1 */
    int32_t tmp;     /* emitter: operand text */
    uint64_t val;    /* literal magnitude, or the constant value as a bit pattern */
} seed_node;

/* Statements: a flat stream in source order with explicit ends. */
enum {
    S_BLOCK, S_END, S_VAR, S_CONST, S_ASSIGN, S_INCDEC, S_CALL, S_DISCARD, S_RETURN,
    S_BREAK, S_CONTINUE, S_IF, S_ELSEIF, S_ELSE, S_ENDIF, S_WHILE, S_FORC, S_FORCOND,
    S_FORR, S_ENDLOOP, S_SWITCH, S_CASE, S_DEFAULT, S_ENDSWITCH
};

typedef struct seed_stmt {
    uint8_t kind;
    uint8_t op;      /* assignment operator, ++/--, inclusive range */
    uint8_t dead;    /* emitter: unreachable */
    uint8_t pad;
    uint32_t tok;    /* keyword or operator token */
    int32_t e1, e2;  /* expression roots, -1 for none */
    int32_t tref;    /* declared typeref */
    uint32_t name;   /* declared name token */
    int32_t sym;     /* declared symbol */
    int32_t link;    /* FORC: update statement; CASE: first item; SWITCH, IF, loops: label number */
    int32_t count;   /* CASE: item count */
    int32_t site;    /* loop charge site */
} seed_stmt;

/* A type as written. */
typedef struct seed_tref {
    uint32_t tok;    /* element type name */
    int32_t qual;    /* qualifying module alias token, or -1 */
    uint8_t shape;   /* 0 none, 1 [expr], 2 [_] */
    uint8_t pad[3];
    int32_t ext;     /* extent expression root */
} seed_tref;

typedef struct seed_case_item {
    int32_t lo, hi;  /* expression roots; hi -1 for a single value */
} seed_case_item;

/* Symbols. */
enum { Y_VAR, Y_PARAM, Y_VIEW, Y_SIZE, Y_LOOP, Y_CONST, Y_FUNC, Y_STRUCT, Y_IMPORT };
typedef struct seed_sym {
    uint8_t kind;
    uint8_t state;   /* constants: 0 pending, 1 busy, 2 done */
    uint8_t exported;
    uint8_t pad;
    int32_t module;
    uint32_t tok;    /* name token in its module */
    int32_t type;
    int32_t decl;    /* function, struct, statement or module index */
    uint64_t val;    /* constant value */
} seed_sym;

typedef struct seed_field {
    uint32_t tok;
    int32_t tref;
    int32_t type;
} seed_field;

typedef struct seed_struct {
    int32_t module;
    uint32_t tok;
    int32_t field0, nfield;
    int32_t done;    /* 2 when the field types are resolved */
    int64_t size, align;
    int32_t type;    /* its K_STRUCT type id */
    uint8_t exported;
    uint8_t emitted;
    uint8_t pad[2];
} seed_struct;

typedef struct seed_param {
    uint32_t tok;
    int32_t tref;
    int32_t type;
    uint8_t mode;    /* 0 in, 1 inout */
    uint8_t pad[3];
    int32_t sym;
} seed_param;

typedef struct seed_func {
    int32_t module;
    uint32_t tok;
    int32_t rtref;   /* result typeref, -1 for void */
    int32_t result;  /* result type */
    int32_t param0, nparam;
    int32_t size0, nsize;   /* size parameter name tokens in sizes[] */
    int32_t body;    /* first statement (S_BLOCK) */
    int32_t body_end;
    int32_t entry_site;
    uint8_t exported;
    uint8_t reach;   /* reachable from an entry */
    uint8_t mark;
    uint8_t signature_state; /* 0 pending, 1 active, 2 complete */
    int32_t call0, ncall;   /* callee list in calls[] */
} seed_func;

typedef struct seed_import {
    uint32_t tok, ntok;     /* path tokens */
    int32_t module;
    uint32_t alias;         /* last path component token */
} seed_import;

typedef struct seed_site {
    uint32_t line, col;
    const char *op;         /* operation identifier (static storage or the name arena) */
} seed_site;

typedef struct seed_module {
    char *path;             /* module-relative path with `.ci` (CINTC-12) */
    char *symc;             /* EMIT-22 component `_<L>_<E(path without .ci)>` */
    uint8_t *src;
    size_t srclen;
    seed_tok *toks;
    uint32_t ntok;
    uint8_t *strs;          /* decoded string literal bytes */
    size_t nstrs;
    seed_node *nodes;
    int32_t nnode, cap_node;
    seed_stmt *stmts;
    int32_t nstmt, cap_stmt;
    seed_tref *trefs;
    int32_t ntref, cap_tref;
    seed_case_item *items;
    int32_t nitem, cap_item;
    int32_t *args;          /* call argument roots */
    uint32_t *argn;         /* named argument name token, or UINT32_MAX */
    int32_t narg, cap_arg;
    seed_import *imports;
    int32_t nimport;
    int32_t *consts;        /* module-level S_CONST statements */
    int32_t nconst;
    seed_site *sites;       /* row 0 is "no site" */
    int32_t nsite, cap_site;
    int32_t order;          /* position in check order */
    int32_t *ghash;         /* module-level names: open addressing over syms */
    int32_t ghcap;
    uint32_t import_tok;    /* where it was first imported (for diagnostics) */
    int32_t import_from;
} seed_module;

/* The whole compilation. */
typedef struct seed_state {
    const char *root;       /* source root, or NULL for the working directory */
    seed_module *mods;
    int32_t nmod, cap_mod;
    seed_type *types;
    int32_t ntype, cap_type;
    seed_struct *structs;
    int32_t nstruct, cap_struct;
    seed_field *fields;
    int32_t nfield, cap_field;
    seed_func *funcs;
    int32_t nfunc, cap_func;
    seed_param *params;
    int32_t nparam, cap_param;
    uint32_t *sizes;
    int32_t nsize, cap_size;
    seed_sym *syms;
    int32_t nsym, cap_sym;
    int32_t *calls;
    int32_t ncall, cap_call;
    int32_t *check_order;   /* modules, imports first */
} seed_state;

extern seed_state S;

/* Bounded growth of a global or per-module table (C9001 when it cannot grow). */
void *seed_grow(void *p, int32_t *cap, int32_t need, size_t elem, const char *what);
#define GROW(arr, n, cap, what) \
    ((n) >= (cap) ? (void)((arr) = seed_grow((arr), &(cap), (n) + 1, sizeof *(arr), (what))) : (void)0)
void *seed_alloc(size_t n, const char *what);

/* Type helpers (check.c). */
bool ty_is_int(int32_t t);
bool ty_signed(int32_t t);
unsigned ty_width(int32_t t);
const char *ty_name(int32_t t);    /* CINT spelling of a scalar type */
const char *ty_ident(int32_t t);   /* lowercase spelling of SPEC-01 IM-130 */
const char *ty_ctype(int32_t t);   /* C spelling of a scalar type */
int32_t type_add(seed_type t);

/* Token text helpers. */
bool tok_is(const seed_module *m, uint32_t t, const char *text);
bool tok_eq(const seed_module *m1, uint32_t t1, const seed_module *m2, uint32_t t2);
int32_t int_type_of(const seed_module *m, uint32_t t);   /* TY_I8.. or 0 */

/* Module-level names (resolve.c). */
bool is_prelude(const seed_module *m, uint32_t tok);
int32_t gsym_find(int32_t mi, const seed_module *nm, uint32_t tok);
int32_t sym_add(int kind, int32_t mi, uint32_t tok, int32_t type, int32_t decl);

/* Passes. */
void scan_module(int32_t mi);
void parse_module(int32_t mi);
void resolve_program(void);
void check_program(void);
void reach_program(void);
/* An exported function of any module whose parameters are all scalars, at most 16: it has an
 * observer entry (SPEC-09 CONF-13 X-2). */
bool fn_observed(const seed_func *f);
char *emit_program(size_t *len, char **sitemap, size_t *sitemap_len);

#endif /* SEED_TABLES_H */
