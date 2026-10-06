/* parse.c: tokens to postfix expression nodes and a flat statement stream.
 *
 * Expressions (SPEC-04 18.6, SPEC-09 5.5): operator precedence with explicit
 * operator, operand and argument stacks. Children are created before their
 * parent, so nodes come out in postfix order. The parenthesization rules of
 * SPEC-04 LS-151 (C2101) and LS-153 (C2102) are checked when a node is built.
 * Statements (SPEC-04 18.5): a state machine over a stack of open constructs.
 * Each stack holds at most one entry per token, so it is sized from the token
 * count and can never overflow.
 */
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "parse.h"
#include "scan.h"

enum { F_UNARY, F_BIN, F_PAREN, F_CALL, F_INDEX };
enum { C_BLOCK, C_IF, C_LOOP, C_SWITCH, C_CASE };
#define PX_INCDEC 1

typedef struct frame {
    uint8_t kind, op, prec, pad;
    uint32_t tok;    /* operator or bracket token; call: callee name */
    uint32_t stok;   /* call: first token of the callee */
    int32_t qual;    /* call: qualifier token or -1 */
    int32_t base;    /* call: argument stack depth at the call */
    int32_t name;    /* call: name token of the pending named argument, or -1 */
} frame;

typedef struct cframe {
    uint8_t kind, seen_default, pad[2];
    uint32_t tok;
    int32_t blk;     /* case: its S_BLOCK statement */
} cframe;

typedef struct parser {
    int32_t mi;
    seed_module *m;
    uint32_t p;
    frame *fr;
    int32_t nfr;
    int32_t *opnd;
    int32_t nopnd;
    int32_t *astk;
    uint32_t *anames;
    int32_t nastk;
    cframe *cf;
    int32_t ncf;
} parser;

bool op_is_arith(int op)
{
    return op >= OP_MUL && op <= OP_SUBS;
}

bool op_is_bitwise(int op)
{
    return op == OP_BAND || op == OP_BXOR || op == OP_BOR;
}

bool op_is_shift(int op)
{
    return op == OP_SHL || op == OP_SHR || op == OP_SHLW;
}

bool op_is_cmp(int op)
{
    return op >= OP_EQ && op <= OP_GE;
}

int op_of_assign(int op)
{
    static const uint8_t map[][2] = {
        {OP_ADDA, OP_ADD}, {OP_SUBA, OP_SUB}, {OP_MULA, OP_MUL}, {OP_DIVA, OP_DIV}, {OP_REMA, OP_REM},
        {OP_SHLA, OP_SHL}, {OP_SHRA, OP_SHR}, {OP_ANDA, OP_BAND}, {OP_ORA, OP_BOR}, {OP_XORA, OP_BXOR},
        {OP_ADDWA, OP_ADDW}, {OP_SUBWA, OP_SUBW}, {OP_MULWA, OP_MULW}, {OP_SHLWA, OP_SHLW},
        {OP_ADDSA, OP_ADDS}, {OP_SUBSA, OP_SUBS}, {OP_MULSA, OP_MULS}};
    size_t i;
    for (i = 0; i < sizeof map / sizeof map[0]; i++) {
        if (map[i][0] == op) {
            return map[i][1];
        }
    }
    return 0;
}

static int bin_prec(int op)
{
    if (op >= OP_MUL && op <= OP_MULS) {
        return 10;
    }
    if (op >= OP_ADD && op <= OP_SUBS) {
        return 9;
    }
    if (op_is_shift(op)) {
        return 8;
    }
    if (op == OP_BAND) {
        return 7;
    }
    if (op == OP_BXOR) {
        return 6;
    }
    if (op == OP_BOR) {
        return 5;
    }
    if (op_is_cmp(op)) {
        return 4;
    }
    return op == OP_LAND ? 3 : op == OP_LOR ? 2 : 0;
}

/* -- tokens ------------------------------------------------------------------------------ */

bool tok_is(const seed_module *m, uint32_t t, const char *text)
{
    size_t n = strlen(text);
    return m->toks[t].len == n && memcmp(m->src + m->toks[t].off, text, n) == 0;
}

bool tok_eq(const seed_module *m1, uint32_t t1, const seed_module *m2, uint32_t t2)
{
    return m1->toks[t1].len == m2->toks[t2].len &&
           memcmp(m1->src + m1->toks[t1].off, m2->src + m2->toks[t2].off, m1->toks[t1].len) == 0;
}

int32_t int_type_of(const seed_module *m, uint32_t t)
{
    static const char *const names[] = {"I8", "I16", "I32", "I64", "U8", "U16", "U32", "U64", "Bool"};
    int32_t i;
    if (m->toks[t].kind != T_IDENT) {
        return 0;
    }
    for (i = 0; i < 9; i++) {
        if (tok_is(m, t, names[i])) {
            return TY_I8 + i;
        }
    }
    return 0;
}

bool tok_reserved(const seed_module *m, uint32_t t)
{
    const uint8_t *s = m->src + m->toks[t].off;
    uint32_t n = m->toks[t].len, i;
    if (n < 2u || (s[0] != 'I' && s[0] != 'U' && s[0] != 'Q' && s[0] != 'T')) {
        return false;
    }
    for (i = 1; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
    }
    return true;
}

/* Built-in type names that are not cint-boot-1 element types (SPEC-09 BOOT-02, 5.5 elem_type). */
static bool non_boot_type(const seed_module *m, uint32_t t)
{
    static const char *const names[] = {"Str", "T1", "T27", "PT5", "PT4", "Arena", "Pool", "Handle",
                                        "I128", "I256", "I512", "I1024", "Round"};
    size_t i;
    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (tok_is(m, t, names[i])) {
            return true;
        }
    }
    return false;
}

static seed_tok *tk(parser *P, uint32_t k)
{
    uint32_t i = P->p + k;
    return &P->m->toks[i < P->m->ntok ? i : P->m->ntok - 1];
}

static bool is_op(parser *P, int op, uint32_t k)
{
    const seed_tok *t = tk(P, k);
    return t->kind == T_OP && t->sub == op;
}

static bool is_kw(parser *P, int kw, uint32_t k)
{
    const seed_tok *t = tk(P, k);
    return t->kind == T_KW && t->sub == kw;
}

static uint32_t next(parser *P)
{
    uint32_t t = P->p;
    if (P->p + 1 < P->m->ntok) {
        P->p++;
    }
    return t;
}

static const char *describe(parser *P, uint32_t t)
{
    static char buf[64];
    const seed_tok *k = &P->m->toks[t];
    uint32_t n = k->len < 40u ? k->len : 40u;
    if (k->kind == T_EOF) {
        return "end of input";
    }
    buf[0] = '`';
    memcpy(buf + 1, P->m->src + k->off, n);
    buf[n + 1] = '`';
    buf[n + 2] = 0;
    return buf;
}

static void expect_op(parser *P, int op, const char *code)
{
    if (!is_op(P, op, 0)) {
        diag_tok(P->mi, P->p, code != NULL ? code : C_SYNTAX, "expected `%s`, found %s", op_text(op),
                 describe(P, P->p));
    }
    next(P);
}

static _Noreturn void refuse(parser *P, uint32_t t, const char *what)
{
    diag_tok(P->mi, t, C_BOOT, "%s is outside cint-boot-1 (SPEC-09 5.5)", what);
}

static uint32_t decl_name(parser *P)
{
    if (tk(P, 0)->kind != T_IDENT) {
        diag_tok(P->mi, P->p, C_SYNTAX, "expected an identifier, found %s", describe(P, P->p));
    }
    if (tok_reserved(P->m, P->p)) {
        diag_tok(P->mi, P->p, "C1010", "`%.*s` matches [IUQT][0-9]+ and is reserved for type names "
                 "(SPEC-04 LS-17)", (int)tk(P, 0)->len, (const char *)P->m->src + tk(P, 0)->off);
    }
    return next(P);
}

/* -- nodes ------------------------------------------------------------------------------- */

static int32_t node_new(parser *P, int kind, int op, uint32_t tok, uint32_t stok)
{
    seed_module *m = P->m;
    seed_node *n;
    if (m->nnode >= m->cap_node) {
        diag_tok(P->mi, tok, C_LIMIT, "the expression node table is full");
    }
    n = &m->nodes[m->nnode];
    memset(n, 0, sizeof *n);
    n->kind = (uint8_t)kind;
    n->op = (uint8_t)op;
    n->tok = tok;
    n->stok = stok;
    n->first = m->nnode;
    n->parent = n->a = n->b = n->aux = n->sym = n->mc = n->tmp = -1;
    return m->nnode++;
}

static void child(parser *P, int32_t n, int32_t a, int32_t b)
{
    seed_node *x = &P->m->nodes[n];
    x->a = a;
    x->b = b;
    if (a >= 0) {
        P->m->nodes[a].parent = n;
        x->first = P->m->nodes[a].first;
    }
    if (b >= 0) {
        P->m->nodes[b].parent = n;
    }
}

static void push(parser *P, int32_t n)
{
    P->opnd[P->nopnd++] = n;
}

static int32_t pop(parser *P)
{
    return P->opnd[--P->nopnd];
}

static bool unparen(parser *P, int32_t n, int kind)
{
    return P->m->nodes[n].kind == kind && (P->m->nodes[n].flags & NF_PAREN) == 0u;
}

static void reduce_top(parser *P)
{
    frame f = P->fr[--P->nfr];
    seed_node *nodes = P->m->nodes;
    int32_t a, b, n, k;
    if (f.kind == F_UNARY) {
        a = pop(P);
        n = node_new(P, N_UNARY, f.op, f.tok, f.tok);
        child(P, n, a, -1);
        push(P, n);
        return;
    }
    b = pop(P);
    a = pop(P);
    k = op_is_cmp(f.op) ? N_CMP : (f.op == OP_LAND || f.op == OP_LOR) ? N_LOGIC : N_BIN;
    n = node_new(P, k, f.op, f.tok, nodes[a].stok);
    child(P, n, a, b);
    if (k == N_LOGIC) {
        int32_t l = nodes[a].a;
        if ((unparen(P, l, N_LOGIC) && nodes[l].op != f.op) || (unparen(P, b, N_LOGIC) && nodes[b].op != f.op)) {
            diag_tok(P->mi, nodes[n].stok, "C2101", "`&&` and `||` in one expression need parentheses");
        }
    } else if (op_is_bitwise(f.op) || op_is_shift(f.op)) {
        int32_t x[2];
        int i;
        x[0] = a, x[1] = b;
        for (i = 0; i < 2; i++) {
            if (!unparen(P, x[i], N_BIN)) {
                continue;
            }
            if (op_is_arith(nodes[x[i]].op)) {
                diag_tok(P->mi, nodes[n].stok, "C2101", "a %s operator with an arithmetic operand needs "
                         "parentheses", op_is_shift(f.op) ? "shift" : "bitwise");
            }
            if (op_is_bitwise(f.op) && op_is_bitwise(nodes[x[i]].op) && nodes[x[i]].op != f.op) {
                diag_tok(P->mi, nodes[n].stok, "C2101", "two different bitwise operators need parentheses");
            }
        }
    }
    push(P, n);
}

/* Reduces operators down to the innermost bracket frame; returns it, or -1. */
static int32_t reduce_to_bracket(parser *P, int32_t fbase)
{
    while (P->nfr > fbase && (P->fr[P->nfr - 1].kind == F_UNARY || P->fr[P->nfr - 1].kind == F_BIN)) {
        reduce_top(P);
    }
    return P->nfr > fbase ? P->nfr - 1 : -1;
}

static void named_arg(parser *P, frame *f)
{
    f->name = -1;
    if (tk(P, 0)->kind == T_IDENT && is_op(P, OP_ASSIGN, 1)) {
        f->name = (int32_t)next(P);
        next(P);
    }
}

static void finish_arg(parser *P, frame *f)
{
    P->astk[P->nastk] = pop(P);
    P->anames[P->nastk] = f->name >= 0 ? (uint32_t)f->name : UINT32_MAX;
    P->nastk++;
}

static void finish_call(parser *P)
{
    frame f = P->fr[--P->nfr];
    seed_module *m = P->m;
    int32_t k = P->nastk - f.base, i, n;
    n = node_new(P, N_CALL, 0, f.tok, f.stok);
    m->nodes[n].sym = f.qual;
    m->nodes[n].a = m->narg;
    m->nodes[n].b = k;
    for (i = 0; i < k; i++) {
        int32_t r = P->astk[f.base + i];
        m->args[m->narg] = r;
        m->argn[m->narg] = P->anames[f.base + i];
        m->narg++;
        m->nodes[r].parent = n;
        if (i == 0) {
            m->nodes[n].first = m->nodes[r].first;
        }
    }
    P->nastk = f.base;
    push(P, n);
}

static int32_t parse_tref(parser *P, bool shape_ok);

/* One expression; returns its root node. */
static int32_t parse_expr(parser *P, int flags)
{
    seed_module *m = P->m;
    int32_t fbase = P->nfr, obase = P->nopnd, n;
    bool want = true;
    for (;;) {
        seed_tok *t = tk(P, 0);
        int op = t->kind == T_OP ? t->sub : OP_NONE;
        if (want) {
            if (t->kind == T_INT) {
                n = node_new(P, N_LIT, 0, P->p, P->p);
                m->nodes[n].val = t->val;
                m->nodes[n].big = t->big;
                next(P);
            } else if (op == OP_SUB && tk(P, 1)->kind == T_INT) {
                uint32_t mt = next(P), lt = next(P);
                /* One negative literal, whatever separates `-` and the literal (SPEC-04 LS-28, D-21). */
                n = node_new(P, N_LIT, 0, mt, mt);
                m->nodes[n].val = m->toks[lt].val;
                m->nodes[n].big = m->toks[lt].big;
                m->nodes[n].flags |= NF_NEG;
            } else if (op == OP_SUB || op == OP_TILDE || op == OP_NOT || op == OP_LPAREN) {
                frame *f = &P->fr[P->nfr++];
                memset(f, 0, sizeof *f);
                f->kind = (uint8_t)(op == OP_LPAREN ? F_PAREN : F_UNARY);
                f->op = (uint8_t)op;
                f->tok = next(P);
                continue;
            } else if (op == OP_SUBW) {
                refuse(P, P->p, "the unary operator `-%`");
            } else if (op == OP_INC || op == OP_DEC) {
                diag_tok(P->mi, P->p, "C1042", "`%s` is a statement, not an expression", op_text(op));
            } else if (t->kind == T_STR) {
                uint32_t st = P->p, len = 0;
                n = node_new(P, N_STR, 0, st, st);
                m->nodes[n].val = t->val;
                while (tk(P, 0)->kind == T_STR) {   /* adjacent literals concatenate (SPEC-04 LS-45) */
                    if (tk(P, 0)->pad != 0u) {
                        diag_at(P->mi, tk(P, 0)->line, tk(P, 0)->pad, "C1033", "a hole in a string literal: holes "
                                "are permitted only in print statements, format calls and assert messages; a "
                                "literal brace is written `{{` or `}}`");
                    }
                    len += tk(P, 0)->slen;
                    next(P);
                }
                m->nodes[n].aux = (int32_t)len;   /* not b: b is a child (SEED-OQ-23) */
            } else if (t->kind == T_KW && (t->sub == KW_TRUE || t->sub == KW_FALSE)) {
                n = node_new(P, N_BOOL, 0, P->p, P->p);
                m->nodes[n].val = t->sub == KW_TRUE ? 1u : 0u;
                next(P);
            } else if (t->kind == T_IDENT) {
                if (is_op(P, OP_LPAREN, 1) && is_prelude(m, P->p)) {
                    /* before its arguments, which may be type names (`muldiv(I32, ...)`) */
                    refuse(P, P->p, fmtbuf("the built-in `%.*s` (no built-in function)", (int)t->len,
                                           (const char *)m->src + t->off));
                }
                if (int_type_of(m, P->p) != 0 || non_boot_type(m, P->p)) {
                    if (is_op(P, OP_DOT, 1)) {
                        refuse(P, P->p, "a type property such as `I64.max`");
                    }
                    diag_tok(P->mi, P->p, "C3007", "the type name %s is not a value", describe(P, P->p));
                }
                n = node_new(P, N_NAME, 0, P->p, P->p);
                next(P);
            } else if (t->kind == T_KW && t->sub == KW_TRY) {
                refuse(P, P->p, "`try`");
            } else if (op == OP_LBRACK) {
                refuse(P, P->p, "an array literal");
            } else if (op == OP_DOT) {
                refuse(P, P->p, "a context-typed enumerator");
            } else {
                diag_tok(P->mi, P->p, C_SYNTAX, "expected an expression, found %s", describe(P, P->p));
            }
            push(P, n);
            want = false;
            continue;
        }
        /* operator position */
        if (op == OP_LPAREN) {
            int32_t x = P->opnd[P->nopnd - 1];
            seed_node *nx = &m->nodes[x];
            frame *f;
            int32_t qual = -1;
            if (unparen(P, x, N_FIELD) && unparen(P, nx->a, N_NAME) && x == m->nnode - 1) {
                qual = (int32_t)m->nodes[nx->a].tok;
            } else if (!(unparen(P, x, N_NAME) && x == m->nnode - 1)) {
                refuse(P, P->p, "a call of a computed callee or a member call");
            }
            f = &P->fr[P->nfr++];
            memset(f, 0, sizeof *f);
            f->kind = F_CALL;
            f->tok = nx->tok;
            f->stok = nx->stok;
            f->qual = qual;
            f->base = P->nastk;
            m->nnode -= qual >= 0 ? 2 : 1;
            pop(P);
            next(P);
            if (is_op(P, OP_RPAREN, 0)) {
                next(P);
                finish_call(P);
                continue;
            }
            named_arg(P, f);
            want = true;
            continue;
        }
        if (op == OP_LBRACK) {
            frame *f = &P->fr[P->nfr++];
            memset(f, 0, sizeof *f);
            f->kind = F_INDEX;
            f->tok = next(P);
            want = true;
            continue;
        }
        if (op == OP_DOT) {
            int32_t x;
            if (tk(P, 1)->kind != T_IDENT) {
                diag_tok(P->mi, P->p + 1, C_SYNTAX, "expected a field name after `.`");
            }
            next(P);
            x = pop(P);
            n = node_new(P, N_FIELD, 0, next(P), m->nodes[x].stok);
            child(P, n, x, -1);
            push(P, n);
            continue;
        }
        if (op == OP_RPAREN || op == OP_COMMA || op == OP_RBRACK) {
            int32_t fi = reduce_to_bracket(P, fbase);
            int k;
            if (fi < 0) {
                break;   /* the token belongs to the enclosing construct */
            }
            k = P->fr[fi].kind;
            if (op == OP_COMMA) {
                if (k != F_CALL) {
                    refuse(P, P->p, k == F_INDEX ? "a multi-dimensional index" : "a tuple");
                }
                finish_arg(P, &P->fr[fi]);
                next(P);
                named_arg(P, &P->fr[fi]);
                want = true;
                continue;
            }
            if (op == OP_RBRACK) {
                int32_t idx, base;
                if (k != F_INDEX) {
                    diag_tok(P->mi, P->p, C_SYNTAX, "expected `)`, found `]`");
                }
                idx = pop(P);
                base = pop(P);
                n = node_new(P, N_INDEX, 0, P->fr[fi].tok, m->nodes[base].stok);
                child(P, n, base, idx);
                P->nfr--;
                next(P);
                push(P, n);
                continue;
            }
            if (k == F_INDEX) {
                diag_tok(P->mi, P->p, C_SYNTAX, "expected `]`, found `)`");
            }
            if (k == F_CALL) {
                finish_arg(P, &P->fr[fi]);
                next(P);
                finish_call(P);
                continue;
            }
            n = P->opnd[P->nopnd - 1];
            m->nodes[n].flags |= NF_PAREN;
            m->nodes[n].stok = P->fr[fi].tok;
            P->nfr--;
            next(P);
            continue;
        }
        if (op == OP_ASW || op == OP_ASS || op == OP_ASQ || (t->kind == T_KW && t->sub == KW_AS)) {
            uint32_t at;
            int32_t x, tr;
            if (op == OP_ASS || op == OP_ASQ) {
                refuse(P, P->p, op == OP_ASS ? "the conversion `as|`" : "the conversion `as?`");
            }
            while (P->nfr > fbase && P->fr[P->nfr - 1].kind == F_UNARY) {
                reduce_top(P);
            }
            x = P->opnd[P->nopnd - 1];
            if (unparen(P, x, N_UNARY)) {
                diag_tok(P->mi, m->nodes[x].stok, "C2101", "a unary operator followed by a conversion needs "
                         "parentheses");
            }
            at = next(P);
            tr = parse_tref(P, false);
            if (tk(P, 0)->kind == T_IDENT && tok_is(m, P->p, "round")) {
                refuse(P, P->p, "a `round` clause");
            }
            x = pop(P);
            n = node_new(P, N_CONV, op == OP_ASW ? OP_ASW : 0, at, m->nodes[x].stok);
            child(P, n, x, -1);
            m->nodes[n].aux = tr;
            push(P, n);
            continue;
        }
        if (op >= OP_MUL && op <= OP_LOR) {
            int prec = bin_prec(op);
            frame *f;
            if (op == OP_ADDS || op == OP_SUBS || op == OP_MULS) {
                refuse(P, P->p, "a saturating operator");
            }
            while (P->nfr > fbase && (P->fr[P->nfr - 1].kind == F_UNARY ||
                                      (P->fr[P->nfr - 1].kind == F_BIN && P->fr[P->nfr - 1].prec >= prec))) {
                reduce_top(P);
            }
            if (op_is_cmp(op) && unparen(P, P->opnd[P->nopnd - 1], N_CMP)) {
                diag_tok(P->mi, m->nodes[P->opnd[P->nopnd - 1]].stok, "C2102", "comparisons do not chain: "
                         "write `a < b && b < c`");
            }
            if (op == OP_LAND || op == OP_LOR) {
                int32_t x = pop(P);
                n = node_new(P, N_SC, op, P->p, m->nodes[x].stok);
                child(P, n, x, -1);
                push(P, n);
            }
            f = &P->fr[P->nfr++];
            memset(f, 0, sizeof *f);
            f->kind = F_BIN;
            f->op = (uint8_t)op;
            f->prec = (uint8_t)prec;
            f->tok = next(P);
            want = true;
            continue;
        }
        if (op == OP_INC || op == OP_DEC) {
            int32_t i;
            bool open = false;
            for (i = fbase; i < P->nfr; i++) {
                open = open || P->fr[i].kind >= F_PAREN;
            }
            if (open || (flags & PX_INCDEC) == 0) {
                diag_tok(P->mi, P->p, "C1042", "`%s` is a statement, not an expression", op_text(op));
            }
            break;
        }
        if (op == OP_QUEST) {
            refuse(P, P->p, "the conditional expression `?:`");
        }
        if (t->kind == T_KW && t->sub == KW_CATCH) {
            refuse(P, P->p, "`catch`");
        }
        if ((op == OP_DOTDOT || op == OP_DOTDOTEQ) && P->nfr > fbase) {
            int32_t fi = reduce_to_bracket(P, fbase);
            if (fi >= 0 && P->fr[fi].kind == F_INDEX) {
                refuse(P, P->p, "an array slice");
            }
        }
        break;
    }
    while (P->nfr > fbase) {
        int k = P->fr[P->nfr - 1].kind;
        if (k == F_PAREN || k == F_CALL) {
            diag_tok(P->mi, P->p, C_SYNTAX, "expected `)`, found %s", describe(P, P->p));
        }
        if (k == F_INDEX) {
            diag_tok(P->mi, P->p, C_SYNTAX, "expected `]`, found %s", describe(P, P->p));
        }
        reduce_top(P);
    }
    n = pop(P);
    (void)obase;
    return n;
}

/* -- types ------------------------------------------------------------------------------- */

static int32_t parse_tref(parser *P, bool shape_ok)
{
    seed_module *m = P->m;
    seed_tref r;
    memset(&r, 0, sizeof r);
    r.qual = -1;
    r.ext = -1;
    if (is_op(P, OP_LPAREN, 0)) {
        refuse(P, P->p, "a tuple type");
    }
    if (tk(P, 0)->kind != T_IDENT) {
        diag_tok(P->mi, P->p, C_SYNTAX, "expected a type, found %s", describe(P, P->p));
    }
    if (non_boot_type(m, P->p)) {
        refuse(P, P->p, fmtbuf("the type `%.*s`", (int)tk(P, 0)->len, (const char *)m->src + tk(P, 0)->off));
    }
    r.tok = next(P);
    if (int_type_of(m, r.tok) == 0 && is_op(P, OP_DOT, 0) && tk(P, 1)->kind == T_IDENT) {
        next(P);
        r.qual = (int32_t)r.tok;
        r.tok = next(P);
    }
    if (is_op(P, OP_LBRACK, 0)) {
        if (!shape_ok) {
            refuse(P, P->p, "an array type as a conversion target");
        }
        next(P);
        if (is_op(P, OP_RBRACK, 0) || (tk(P, 0)->kind == T_IDENT && tok_is(m, P->p, "_") && is_op(P, OP_RBRACK, 1))) {
            r.shape = 2;
            if (!is_op(P, OP_RBRACK, 0)) {
                next(P);
            }
        } else {
            r.shape = 1;
            r.ext = parse_expr(P, 0);
            if (is_op(P, OP_COMMA, 0)) {
                refuse(P, P->p, "an array of rank above 1");
            }
            if (is_op(P, OP_DOTDOT, 0) || is_op(P, OP_DOTDOTEQ, 0)) {
                refuse(P, P->p, "a declared lower bound");
            }
        }
        expect_op(P, OP_RBRACK, NULL);
    }
    if (is_op(P, OP_NOT, 0)) {
        refuse(P, P->p, "an error union");
    }
    m->trefs[m->ntref] = r;
    return m->ntref++;
}

/* The token index of the declared name when a declaration starts here
 * (type, then name; SPEC-04 LS-303), else 0. */
static uint32_t decl_shape(parser *P)
{
    uint32_t k = 1;
    if (tk(P, 0)->kind != T_IDENT) {
        return 0;
    }
    if (is_op(P, OP_DOT, 1) && tk(P, 2)->kind == T_IDENT) {
        k = 3;
    }
    if (is_op(P, OP_LBRACK, k)) {
        int depth = 1;
        k++;
        while (depth > 0) {
            if (tk(P, k)->kind == T_EOF) {
                return 0;
            }
            depth += is_op(P, OP_LBRACK, k) ? 1 : is_op(P, OP_RBRACK, k) ? -1 : 0;
            k++;
        }
    }
    return k;
}

/* A type followed by its declared name: the index of the name, or 0. */
static uint32_t decl_ahead(parser *P)
{
    uint32_t k = decl_shape(P);
    return k != 0 && tk(P, k)->kind == T_IDENT ? k : 0;
}

/* A type followed by a keyword where the name goes (`I64 do = 1;`) is not a
 * declaration; it is an expression statement, C4012 at its start, as cint_ref
 * reports it (SPEC-04 LS-167). */
static void decl_keyword(parser *P)
{
    uint32_t k = decl_shape(P);
    const seed_tok *t = k != 0 ? tk(P, k) : NULL;
    if (t != NULL && t->kind == T_KW && t->sub != KW_TRUE && t->sub != KW_FALSE) {
        diag_tok(P->mi, P->p, "C4012", "only calls, assignments and increments may stand as statements "
                 "(`%s` is a keyword, not a name)", kw_text(t->sub));
    }
}

/* -- statements ---------------------------------------------------------------------------- */

static int32_t stmt_new(parser *P, int kind, uint32_t tok)
{
    seed_module *m = P->m;
    seed_stmt *s;
    if (m->nstmt >= m->cap_stmt) {
        diag_tok(P->mi, tok, C_LIMIT, "the statement table is full");
    }
    s = &m->stmts[m->nstmt];
    memset(s, 0, sizeof *s);
    s->kind = (uint8_t)kind;
    s->tok = tok;
    s->e1 = s->e2 = s->tref = s->sym = s->link = -1;
    return m->nstmt++;
}

static void push_c(parser *P, int kind, uint32_t tok, int32_t blk)
{
    cframe *c = &P->cf[P->ncf++];
    memset(c, 0, sizeof *c);
    c->kind = (uint8_t)kind;
    c->tok = tok;
    c->blk = blk;
}

static void open_block(parser *P)
{
    int32_t s = stmt_new(P, S_BLOCK, P->p);
    expect_op(P, OP_LBRACE, NULL);
    push_c(P, C_BLOCK, P->m->stmts[s].tok, s);
}

static void check_body_start(parser *P, const char *code)
{
    if (is_op(P, OP_SEMI, 0)) {
        diag_tok(P->mi, P->p, "C1040", "there is no empty statement");
    }
    if (!is_op(P, OP_LBRACE, 0)) {
        diag_tok(P->mi, P->p, code, "braces are required around the body");
    }
}

static int32_t parse_condition(parser *P, const char *code)
{
    int32_t c;
    if (!is_op(P, OP_LPAREN, 0)) {
        diag_tok(P->mi, P->p, code, "parentheses are required around the condition");
    }
    next(P);
    c = parse_expr(P, 0);
    if (is_op(P, OP_ASSIGN, 0)) {
        diag_tok(P->mi, P->m->nodes[c].stok, "C1041", "assignment is a statement, not an expression: "
                 "write `==` to compare");
    }
    expect_op(P, OP_RPAREN, code);
    return c;
}

static int32_t parse_var_decl(parser *P, bool semi)
{
    uint32_t start = P->p;
    int32_t tr = parse_tref(P, true), s, init = -1;
    uint32_t name = decl_name(P);
    if (is_op(P, OP_ASSIGN, 0)) {
        next(P);
        init = parse_expr(P, 0);
    }
    if (semi) {
        expect_op(P, OP_SEMI, NULL);
    }
    s = stmt_new(P, S_VAR, start);
    P->m->stmts[s].tref = tr;
    P->m->stmts[s].name = name;
    P->m->stmts[s].e1 = init;
    return s;
}

static int32_t parse_const(parser *P)
{
    uint32_t kw = next(P);
    int32_t tr = parse_tref(P, true), init, s;
    uint32_t name = decl_name(P);
    expect_op(P, OP_ASSIGN, NULL);
    init = parse_expr(P, 0);
    expect_op(P, OP_SEMI, NULL);
    s = stmt_new(P, S_CONST, kw);
    P->m->stmts[s].tref = tr;
    P->m->stmts[s].name = name;
    P->m->stmts[s].e1 = init;
    return s;
}

static bool is_place(parser *P, int32_t e)
{
    const seed_node *n = P->m->nodes;
    while (n[e].kind == N_FIELD || n[e].kind == N_INDEX) {
        if ((n[e].flags & NF_PAREN) != 0u) {
            return false;
        }
        e = n[e].a;
    }
    return unparen(P, e, N_NAME);
}

/* A simple statement without its `;` (SPEC-04 18.5 simple_stmt). */
static int32_t parse_simple(parser *P)
{
    seed_module *m = P->m;
    int32_t e, s;
    int op;
    if (tk(P, 0)->kind == T_IDENT && tok_is(m, P->p, "_") && is_op(P, OP_ASSIGN, 1)) {
        uint32_t t = next(P);
        next(P);
        s = stmt_new(P, S_DISCARD, t);
        m->stmts[s].e1 = parse_expr(P, 0);
        return s;
    }
    e = parse_expr(P, PX_INCDEC);
    op = tk(P, 0)->kind == T_OP ? tk(P, 0)->sub : OP_NONE;
    if (op == OP_ASSIGN || op_of_assign(op) != 0 || op == OP_INC || op == OP_DEC) {
        if (op == OP_ADDSA || op == OP_SUBSA || op == OP_MULSA) {
            refuse(P, P->p, "a saturating assignment");
        }
        if (!is_place(P, e)) {
            diag_tok(P->mi, m->nodes[e].stok, "C2058", "the left side of an assignment must be a variable, "
                     "a field or an element");
        }
        s = stmt_new(P, op == OP_INC || op == OP_DEC ? S_INCDEC : S_ASSIGN, next(P));
        m->stmts[s].op = (uint8_t)op;
        m->stmts[s].e1 = e;
        if (op != OP_INC && op != OP_DEC) {
            m->stmts[s].e2 = parse_expr(P, 0);
        }
        return s;
    }
    if (!unparen(P, e, N_CALL)) {
        diag_tok(P->mi, m->nodes[e].stok, "C4012", "only calls, assignments and increments may stand as "
                 "statements");
    }
    s = stmt_new(P, S_CALL, m->nodes[e].stok);
    m->stmts[s].e1 = e;
    return s;
}

static void parse_for(parser *P)
{
    seed_module *m = P->m;
    uint32_t kw = next(P);
    int32_t s;
    if (is_op(P, OP_LPAREN, 0)) {
        int32_t fs, c;
        next(P);
        fs = stmt_new(P, S_FORC, kw);
        if (decl_ahead(P) != 0) {
            int32_t v = parse_var_decl(P, false);
            if (m->stmts[v].e1 < 0) {
                diag_tok(P->mi, P->p, C_SYNTAX, "expected `=`: the loop variable needs an initializer");
            }
        } else {
            parse_simple(P);
        }
        expect_op(P, OP_SEMI, NULL);
        c = parse_expr(P, 0);
        if (is_op(P, OP_ASSIGN, 0)) {
            diag_tok(P->mi, m->nodes[c].stok, "C1041", "assignment is a statement, not an expression: "
                     "write `==` to compare");
        }
        expect_op(P, OP_SEMI, NULL);
        s = stmt_new(P, S_FORCOND, kw);
        m->stmts[s].e1 = c;
        m->stmts[fs].link = parse_simple(P);
        expect_op(P, OP_RPAREN, NULL);
    } else {
        uint32_t name = decl_name(P);
        int32_t lo, hi;
        uint32_t rt;
        if (is_op(P, OP_COMMA, 0)) {
            refuse(P, P->p, "a two-variable `for`");
        }
        if (!is_kw(P, KW_IN, 0)) {
            diag_tok(P->mi, P->p, C_SYNTAX, "expected `in`, found %s", describe(P, P->p));
        }
        next(P);
        lo = parse_expr(P, 0);
        if (!is_op(P, OP_DOTDOT, 0) && !is_op(P, OP_DOTDOTEQ, 0)) {
            refuse(P, P->p, "iteration over an array or view");
        }
        rt = next(P);
        hi = parse_expr(P, 0);
        if (is_kw(P, KW_BY, 0)) {
            refuse(P, P->p, "a range step `by`");
        }
        s = stmt_new(P, S_FORR, kw);
        m->stmts[s].e1 = lo;
        m->stmts[s].e2 = hi;
        m->stmts[s].name = name;
        m->stmts[s].op = (uint8_t)(m->toks[rt].sub == OP_DOTDOTEQ ? 1 : 0);
    }
    check_body_start(P, "C1043");
    push_c(P, C_LOOP, kw, -1);
    open_block(P);
}

static void parse_statement(parser *P)
{
    seed_module *m = P->m;
    seed_tok *t = tk(P, 0);
    int32_t s;
    if (is_op(P, OP_LBRACE, 0)) {
        open_block(P);
        return;
    }
    if (is_op(P, OP_SEMI, 0)) {
        diag_tok(P->mi, P->p, "C1040", "there is no empty statement");
    }
    if (t->kind == T_KW) {
        uint32_t kw;
        switch (t->sub) {
        case KW_CONST:
            parse_const(P);
            return;
        case KW_IF:
            kw = next(P);
            s = parse_condition(P, "C1043");
            check_body_start(P, "C1043");
            m->stmts[stmt_new(P, S_IF, kw)].e1 = s;
            push_c(P, C_IF, kw, -1);
            open_block(P);
            return;
        case KW_WHILE:
            kw = next(P);
            s = parse_condition(P, "C1043");
            check_body_start(P, "C1043");
            m->stmts[stmt_new(P, S_WHILE, kw)].e1 = s;
            push_c(P, C_LOOP, kw, -1);
            open_block(P);
            return;
        case KW_FOR:
            parse_for(P);
            return;
        case KW_SWITCH:
            kw = next(P);
            expect_op(P, OP_LPAREN, NULL);
            s = parse_expr(P, 0);
            expect_op(P, OP_RPAREN, NULL);
            expect_op(P, OP_LBRACE, NULL);
            m->stmts[stmt_new(P, S_SWITCH, kw)].e1 = s;
            push_c(P, C_SWITCH, kw, -1);
            return;
        case KW_BREAK:
        case KW_CONTINUE:
            kw = next(P);
            if (tk(P, 0)->kind == T_IDENT) {
                refuse(P, P->p, "a labeled `break` or `continue`");
            }
            expect_op(P, OP_SEMI, NULL);
            stmt_new(P, t->sub == KW_BREAK ? S_BREAK : S_CONTINUE, kw);
            return;
        case KW_RETURN:
            kw = next(P);
            s = is_op(P, OP_SEMI, 0) ? -1 : parse_expr(P, 0);
            expect_op(P, OP_SEMI, NULL);
            m->stmts[stmt_new(P, S_RETURN, kw)].e1 = s;
            return;
        case KW_TRUE:
        case KW_FALSE:
        case KW_TRY:
            break;
        case KW_CASE:
        case KW_DEFAULT:
        case KW_ELSE:
            diag_tok(P->mi, P->p, C_SYNTAX, "unexpected `%s`", kw_text(t->sub));
        default:
            refuse(P, P->p, fmtbuf("`%s`", kw_text(t->sub)));
        }
    }
    if (t->kind == T_STR) {
        refuse(P, P->p, "a print statement");
    }
    if (t->kind == T_IDENT && is_op(P, OP_COLON, 1)) {
        refuse(P, P->p, "a labeled loop");
    }
    if (is_op(P, OP_LPAREN, 0) && tk(P, 1)->kind == T_IDENT && tk(P, 2)->kind == T_IDENT) {
        refuse(P, P->p, "destructuring");
    }
    if (decl_ahead(P) != 0) {
        parse_var_decl(P, true);
        return;
    }
    decl_keyword(P);
    parse_simple(P);
    expect_op(P, OP_SEMI, NULL);
}

static void block_closed(parser *P)
{
    seed_module *m = P->m;
    cframe *c;
    if (P->ncf == 0) {
        return;
    }
    c = &P->cf[P->ncf - 1];
    if (c->kind == C_IF) {
        if (is_kw(P, KW_ELSE, 0)) {
            next(P);
            if (is_kw(P, KW_IF, 0)) {
                uint32_t kw = next(P);
                int32_t cond = parse_condition(P, "C1043");
                check_body_start(P, "C1043");
                m->stmts[stmt_new(P, S_ELSEIF, kw)].e1 = cond;
            } else {
                check_body_start(P, "C1043");
                stmt_new(P, S_ELSE, P->p);
            }
            open_block(P);
            return;
        }
        stmt_new(P, S_ENDIF, P->p);
        P->ncf--;
    } else if (c->kind == C_LOOP) {
        stmt_new(P, S_ENDLOOP, P->p);
        P->ncf--;
    }
}

static void parse_switch_clause(parser *P)
{
    seed_module *m = P->m;
    cframe *c = &P->cf[P->ncf - 1];
    uint32_t ct;
    int32_t s;
    if (is_kw(P, KW_CASE, 0)) {
        int32_t first = m->nitem;
        if (c->seen_default) {
            diag_tok(P->mi, P->p, "C4025", "`default` must be the last clause of a switch");
        }
        ct = next(P);
        for (;;) {
            int32_t lo = parse_expr(P, 0), hi = -1;
            if (is_op(P, OP_DOTDOT, 0)) {
                diag_tok(P->mi, m->nodes[lo].stok, "C4022", "case ranges are inclusive: write `..=`");
            }
            if (is_op(P, OP_DOTDOTEQ, 0)) {
                next(P);
                hi = parse_expr(P, 0);
            }
            m->items[m->nitem].lo = lo;
            m->items[m->nitem].hi = hi;
            m->nitem++;
            if (!is_op(P, OP_COMMA, 0)) {
                break;
            }
            next(P);
        }
        expect_op(P, OP_COLON, NULL);
        s = stmt_new(P, S_CASE, ct);
        m->stmts[s].link = first;
        m->stmts[s].count = m->nitem - first;
    } else if (is_kw(P, KW_DEFAULT, 0)) {
        if (c->seen_default) {
            diag_tok(P->mi, P->p, "C4025", "a switch has at most one `default`");
        }
        c->seen_default = 1;
        ct = next(P);
        expect_op(P, OP_COLON, NULL);
        stmt_new(P, S_DEFAULT, ct);
    } else if (is_op(P, OP_RBRACE, 0)) {
        stmt_new(P, S_ENDSWITCH, next(P));
        P->ncf--;
        return;
    } else {
        diag_tok(P->mi, P->p, C_SYNTAX, "expected `case`, `default` or `}`, found %s", describe(P, P->p));
    }
    s = stmt_new(P, S_BLOCK, ct);
    push_c(P, C_CASE, ct, s);
}

static void parse_body(parser *P)
{
    seed_module *m = P->m;
    open_block(P);
    while (P->ncf > 0) {
        cframe *c = &P->cf[P->ncf - 1];
        if (c->kind == C_CASE) {
            if (is_kw(P, KW_CASE, 0) || is_kw(P, KW_DEFAULT, 0) || is_op(P, OP_RBRACE, 0)) {
                if (m->nstmt == c->blk + 1) {
                    diag_tok(P->mi, c->tok, "C4023", "a case clause has an empty body: merge its items into "
                             "the next clause");
                }
                stmt_new(P, S_END, P->p);
                P->ncf--;
                continue;
            }
        } else if (c->kind == C_SWITCH) {
            parse_switch_clause(P);
            continue;
        } else if (is_op(P, OP_RBRACE, 0)) {
            stmt_new(P, S_END, next(P));
            P->ncf--;
            block_closed(P);
            continue;
        }
        if (tk(P, 0)->kind == T_EOF) {
            diag_tok(P->mi, P->p, C_SYNTAX, "expected `}`, found end of input");
        }
        parse_statement(P);
    }
}

/* -- module level ------------------------------------------------------------------------------- */

static void parse_struct(parser *P, bool exported)
{
    seed_struct st;
    memset(&st, 0, sizeof st);
    next(P);
    st.module = P->mi;
    st.tok = decl_name(P);
    st.exported = exported ? 1u : 0u;
    st.field0 = S.nfield;
    expect_op(P, OP_LBRACE, NULL);
    while (!is_op(P, OP_RBRACE, 0)) {
        seed_field f;
        if (is_op(P, OP_AT, 0)) {
            refuse(P, P->p, "an attribute");
        }
        f.tref = parse_tref(P, true);
        f.tok = decl_name(P);
        f.type = 0;
        if (is_op(P, OP_COLON, 0)) {
            refuse(P, P->p, "a bit field");
        }
        if (is_op(P, OP_ASSIGN, 0)) {
            refuse(P, P->p, "a field default");
        }
        expect_op(P, OP_SEMI, NULL);
        GROW(S.fields, S.nfield, S.cap_field, "field");
        S.fields[S.nfield++] = f;
    }
    next(P);
    st.nfield = S.nfield - st.field0;
    GROW(S.structs, S.nstruct, S.cap_struct, "struct");
    S.structs[S.nstruct++] = st;
}

static void parse_func(parser *P, bool exported)
{
    seed_func f;
    memset(&f, 0, sizeof f);
    f.module = P->mi;
    f.rtref = -1;
    f.exported = exported ? 1u : 0u;
    if (is_kw(P, KW_VOID, 0)) {
        next(P);
    } else {
        f.rtref = parse_tref(P, true);
    }
    f.tok = decl_name(P);
    f.size0 = S.nsize;
    if (is_op(P, OP_LBRACK, 0)) {
        next(P);
        for (;;) {
            GROW(S.sizes, S.nsize, S.cap_size, "size parameter");
            S.sizes[S.nsize++] = decl_name(P);
            if (!is_op(P, OP_COMMA, 0)) {
                break;
            }
            next(P);
        }
        expect_op(P, OP_RBRACK, NULL);
    }
    f.nsize = S.nsize - f.size0;
    expect_op(P, OP_LPAREN, NULL);
    f.param0 = S.nparam;
    while (!is_op(P, OP_RPAREN, 0)) {
        seed_param p;
        memset(&p, 0, sizeof p);
        if (is_kw(P, KW_OUT, 0)) {
            diag_tok(P->mi, P->p, "C5001", "`out` is for kernels and extern declarations; a function "
                     "parameter is `in` or `inout`");
        }
        if (is_kw(P, KW_IN, 0)) {
            next(P);
        } else if (is_kw(P, KW_INOUT, 0)) {
            p.mode = 1;
            next(P);
        }
        p.tref = parse_tref(P, true);
        p.tok = decl_name(P);
        p.sym = -1;
        if (is_op(P, OP_ASSIGN, 0)) {
            refuse(P, P->p, "a default argument");
        }
        GROW(S.params, S.nparam, S.cap_param, "parameter");
        S.params[S.nparam++] = p;
        if (!is_op(P, OP_RPAREN, 0)) {
            expect_op(P, OP_COMMA, NULL);
        }
    }
    next(P);
    f.nparam = S.nparam - f.param0;
    if (is_kw(P, KW_WHERE, 0)) {
        refuse(P, P->p, "a `where` clause");
    }
    if (!is_op(P, OP_LBRACE, 0)) {
        diag_tok(P->mi, P->p, C_SYNTAX, "expected `{`, found %s", describe(P, P->p));
    }
    f.body = P->m->nstmt;
    parse_body(P);
    f.body_end = P->m->nstmt - 1;
    f.entry_site = 0;
    GROW(S.funcs, S.nfunc, S.cap_func, "function");
    S.funcs[S.nfunc++] = f;
}

void parse_module(int32_t mi)
{
    seed_module *m = &S.mods[mi];
    parser P;
    int32_t cap = (int32_t)m->ntok + 16;
    memset(&P, 0, sizeof P);
    P.mi = mi;
    P.m = m;
    m->cap_node = 2 * cap;
    m->nodes = seed_alloc((size_t)m->cap_node * sizeof *m->nodes, "node");
    m->cap_stmt = 2 * cap;
    m->stmts = seed_alloc((size_t)m->cap_stmt * sizeof *m->stmts, "statement");
    m->trefs = seed_alloc((size_t)cap * sizeof *m->trefs, "type");
    m->items = seed_alloc((size_t)cap * sizeof *m->items, "case item");
    m->args = seed_alloc((size_t)cap * sizeof *m->args, "argument");
    m->argn = seed_alloc((size_t)cap * sizeof *m->argn, "argument");
    m->imports = seed_alloc((size_t)cap * sizeof *m->imports, "import");
    m->consts = seed_alloc((size_t)cap * sizeof *m->consts, "constant");
    P.fr = seed_alloc((size_t)cap * sizeof *P.fr, "parser");
    P.opnd = seed_alloc((size_t)cap * sizeof *P.opnd, "parser");
    P.astk = seed_alloc((size_t)cap * sizeof *P.astk, "parser");
    P.anames = seed_alloc((size_t)cap * sizeof *P.anames, "parser");
    P.cf = seed_alloc((size_t)cap * sizeof *P.cf, "parser");
    if (is_kw(&P, KW_PROFILE, 0)) {
        refuse(&P, P.p, "a `profile` line");
    }
    while (is_kw(&P, KW_IMPORT, 0)) {
        seed_import *im = &m->imports[m->nimport];
        next(&P);
        im->tok = P.p;
        im->alias = decl_name(&P);
        while (is_op(&P, OP_DOT, 0) && tk(&P, 1)->kind == T_IDENT) {
            next(&P);
            im->alias = next(&P);
        }
        im->ntok = P.p - im->tok;
        im->module = -1;
        if (is_kw(&P, KW_AS, 0) || is_op(&P, OP_DOT, 0)) {
            refuse(&P, P.p, "an import alias or selected names");
        }
        expect_op(&P, OP_SEMI, NULL);
        m->nimport++;
    }
    while (tk(&P, 0)->kind != T_EOF) {
        bool exported = false;
        uint32_t start = P.p, k;
        seed_tok *t;
        if (is_kw(&P, KW_IMPORT, 0)) {
            diag_tok(mi, P.p, "C3012", "import declarations come before every other item (SPEC-04 LS-226)");
        }
        if (is_op(&P, OP_AT, 0)) {
            refuse(&P, P.p, "an attribute");
        }
        if (is_kw(&P, KW_EXPORT, 0)) {
            exported = true;
            next(&P);
        }
        t = tk(&P, 0);
        if (is_kw(&P, KW_STRUCT, 0)) {
            parse_struct(&P, exported);
        } else if (is_kw(&P, KW_CONST, 0)) {
            int32_t s = parse_const(&P);
            m->stmts[s].op = exported ? 1u : 0u;
            m->consts[m->nconst++] = s;
        } else if (is_kw(&P, KW_VOID, 0)) {
            parse_func(&P, exported);
        } else if (t->kind == T_KW && t->sub != KW_TRUE && t->sub != KW_FALSE) {
            refuse(&P, P.p, fmtbuf("`%s` at module level", kw_text(t->sub)));
        } else if ((k = decl_ahead(&P)) != 0 && (is_op(&P, OP_LPAREN, k + 1) || is_op(&P, OP_LBRACK, k + 1))) {
            parse_func(&P, exported);
        } else if (k != 0) {
            refuse(&P, start, "a module-level variable");
        } else if (exported) {
            diag_tok(mi, P.p, C_SYNTAX, "`export` must precede a declaration");
        } else if (is_op(&P, OP_LPAREN, 0) && tk(&P, 1)->kind == T_IDENT && is_op(&P, OP_COMMA, 2)) {
            refuse(&P, P.p, "a tuple type");
        } else {
            decl_keyword(&P);
            refuse(&P, P.p, "a top-level statement (a cint-boot-1 module is an importable module)");
        }
    }
    free(P.fr);
    free(P.opnd);
    free(P.astk);
    free(P.anames);
    free(P.cf);
}
