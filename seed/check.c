/* check.c: names, types, context typing, constant evaluation, statements.
 *
 * Context typing follows SPEC-01 IM-21, IM-22 and SPEC-04 LS-53 to LS-59: a
 * literal-only expression is untyped until its context gives it a type, which
 * then passes to every operand except a shift count. Constant expressions
 * (SPEC-01 IM-23) are typed first and then evaluated with the run-time rules:
 * the checked and wrapping operators call the helpers of cint_rt.h on a
 * context of their own, so a constant fault has exactly the record a run-time
 * fault has (SEED-08). A literal out of range of its type is C2003 and a fault
 * is C6001 carrying the fault fields, in evaluation order.
 *
 * Passes use postfix node ranges or explicit expression, declaration and
 * statement continuation stacks (no recursion proportional to input depth).
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cint_rt.h"
#include "diag.h"
#include "parse.h"
#include "scan.h"
#include "tables.h"

#define N(i) (M->nodes[(i)])

static int32_t cm;           /* current module */
static seed_module *M;
static int32_t cfn = -1;     /* current function, -1 at module level */
static int32_t *locals;      /* visible local symbols, innermost last */
static int32_t nlocal, cap_local;

/* Suspends expressions at a pending type signature or module constant. */
enum { DEP_SIGNATURE, DEP_CONSTANT };
typedef struct sig_frame {
    int32_t kind, item, root;
    size_t base;
} sig_frame;
static sig_frame *signature_frames;
static int32_t signature_sp;
static int32_t signature_need = -1;
static int32_t signature_kind;
static void ensure_declaration(int32_t kind, int32_t item);

/* Each active expression frame owns a distinct parsed node. Declaration
 * dependencies append above suspended callers and restore their stack base. */
typedef struct expr_frame {
    int32_t node, step, arg, callee;
} expr_frame;
static expr_frame *expression_frames;
static size_t expression_sp;


/* -- types --------------------------------------------------------------------------------- */

static const char *const tnames[] = {"untyped", "I8", "I16", "I32", "I64", "U8", "U16", "U32", "U64",
                                     "Bool", "void", "Z", "module"};
static const char *const tidents[] = {"", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "bool"};
static const char *const tctypes[] = {"", "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t",
                                      "uint32_t", "uint64_t", "bool"};

bool ty_is_int(int32_t t)
{
    return t >= TY_I8 && t <= TY_U64;
}

bool ty_signed(int32_t t)
{
    return t >= TY_I8 && t <= TY_I64;
}

unsigned ty_width(int32_t t)
{
    return 8u << ((unsigned)(t - TY_I8) & 3u);
}

const char *ty_ident(int32_t t)
{
    return t >= TY_I8 && t <= TY_BOOL ? tidents[t] : "struct";
}

const char *ty_ctype(int32_t t)
{
    return t >= TY_I8 && t <= TY_BOOL ? tctypes[t] : "?";
}

const char *ty_name(int32_t t)
{
    static char buf[8][80];
    static unsigned next;
    unsigned k;  /* this call's slot: a nested call takes the next one */
    const seed_type *y;
    if (t < TY_USER) {
        return tnames[t];
    }
    y = &S.types[t];
    k = next++ % 8u;
    if (y->kind == K_STRUCT) {
        const seed_struct *st = &S.structs[y->strct];
        const seed_tok *tk = &S.mods[st->module].toks[st->tok];
        snprintf(buf[k], sizeof buf[k], "%.*s", (int)(tk->len < 60u ? tk->len : 60u),
                 (const char *)S.mods[st->module].src + tk->off);
    } else {
        snprintf(buf[k], sizeof buf[k], "%s%s[%s]", y->kind == K_VIEW ? (y->perm ? "inout " : "in ") : "",
                 ty_name(y->elem), y->kind == K_ARRAY ? fmtbuf("%" PRId64, y->extent) : "_");
    }
    return buf[k];
}

int32_t type_add(seed_type t)
{
    int32_t i;
    for (i = TY_USER; i < S.ntype; i++) {
        const seed_type *y = &S.types[i];
        if (y->kind == t.kind && y->perm == t.perm && y->lit == t.lit && y->elem == t.elem &&
            y->strct == t.strct && y->sp == t.sp && y->extent == t.extent) {
            return i;
        }
    }
    GROW(S.types, S.ntype, S.cap_type, "type");
    S.types[S.ntype] = t;
    return S.ntype++;
}

static bool is_user(int32_t t, int kind)
{
    return t >= TY_USER && S.types[t].kind == kind;
}

static uint64_t ty_max(int32_t t)
{
    unsigned w = ty_width(t);
    return ty_signed(t) ? ((uint64_t)1 << (w - 1u)) - 1u : (w == 64u ? UINT64_MAX : ((uint64_t)1 << w) - 1u);
}

/* The bit pattern of value v (sign-extended for signed types) as decimal text. */
static const char *val_text(int32_t t, uint64_t v)
{
    static char buf[2][32];
    static int k;
    k ^= 1;
    if (ty_signed(t)) {
        snprintf(buf[k], sizeof buf[k], "%" PRId64, (int64_t)v);
    } else {
        snprintf(buf[k], sizeof buf[k], "%" PRIu64, v);
    }
    return buf[k];
}

static const char *z_text(bool neg, uint64_t mag, bool big)
{
    static char buf[48];
    if (big) {
        return "(a literal above 18446744073709551615)";
    }
    snprintf(buf, sizeof buf, "%s%" PRIu64, neg && mag != 0u ? "-" : "", mag);
    return buf;
}

/* Sign-extends or masks bits to type t. */
static uint64_t wrap_to(int32_t t, uint64_t bits)
{
    unsigned w = ty_width(t);
    if (w == 64u) {
        return bits;
    }
    bits &= ((uint64_t)1 << w) - 1u;
    if (ty_signed(t) && (bits >> (w - 1u)) != 0u) {
        bits |= ~(((uint64_t)1 << w) - 1u);
    }
    return bits;
}

/* Whether the integer (neg, mag) or the pattern of type `from` lies in type t. */
static bool z_fits(int32_t t, bool neg, uint64_t mag, bool big)
{
    if (big) {
        return false;
    }
    if (neg && mag != 0u) {
        return ty_signed(t) && mag <= ty_max(t) + 1u;
    }
    return mag <= ty_max(t);
}

/* -- names --------------------------------------------------------------------------------- */

static int32_t lookup(uint32_t tok, bool must)
{
    int32_t i, y;
    for (i = nlocal - 1; i >= 0; i--) {
        if (tok_eq(M, S.syms[locals[i]].tok, M, tok)) {
            return locals[i];
        }
    }
    y = gsym_find(cm, M, tok);
    if (y >= 0 || !must) {
        return y;
    }
    if (is_prelude(M, tok)) {
        diag_tok(cm, tok, C_BOOT, "the built-in `%.*s` is outside cint-boot-1 (SPEC-09 5.5: no built-in "
                 "function)", (int)M->toks[tok].len, (const char *)M->src + M->toks[tok].off);
    }
    diag_tok(cm, tok, "C3005", "undefined name `%.*s`", (int)M->toks[tok].len,
             (const char *)M->src + M->toks[tok].off);
}

static void declare(uint32_t tok, int32_t y)
{
    if (lookup(tok, false) >= 0 || is_prelude(M, tok)) {
        diag_tok(cm, tok, "C3001", "`%.*s` is already declared and visible here", (int)M->toks[tok].len,
                 (const char *)M->src + M->toks[tok].off);
    }
    GROW(locals, nlocal, cap_local, "local");
    locals[nlocal++] = y;
}

/* -- sites --------------------------------------------------------------------------------- */

static const char *intern(const char *s)
{
    size_t n = strlen(s) + 1u;
    char *p = seed_alloc(n, "site");
    memcpy(p, s, n);
    return p;
}

static int32_t site_add(uint32_t tok, const char *op)
{
    seed_site *s;
    GROW(M->sites, M->nsite, M->cap_site, "site");
    s = &M->sites[M->nsite];
    s->line = M->toks[tok].line;
    s->col = M->toks[tok].col;
    s->op = intern(op);
    return M->nsite++;
}

static const char *arith_name(int op)
{
    switch (op) {
    case OP_ADD: case OP_ADDA: case OP_INC: return "add";
    case OP_SUB: case OP_SUBA: case OP_DEC: return "sub";
    case OP_MUL: case OP_MULA: return "mul";
    case OP_DIV: case OP_DIVA: return "div";
    case OP_REM: case OP_REMA: return "rem";
    case OP_SHL: case OP_SHLA: case OP_SHLW: case OP_SHLWA: return "shl";
    case OP_SHR: case OP_SHRA: return "shr";
    default: return NULL;
    }
}

/* The operation identifier of a checked node (SPEC-01 IM-130), or NULL. */
static const char *node_op(int32_t j)
{
    const seed_node *n = &N(j);
    switch (n->kind) {
    case N_BIN:
        if (arith_name(n->op) == NULL) {
            return NULL;
        }
        return fmtbuf("%s.%s.%s", arith_name(n->op), n->op == OP_SHLW ? "wrap" : "checked", ty_ident(n->type));
    case N_UNARY:
        return n->op == OP_SUB ? fmtbuf("neg.checked.%s", ty_ident(n->type)) : NULL;
    case N_CONV:
        return n->op == 0 && ty_is_int(N(n->a).type)
                   ? fmtbuf("as.checked.%s.%s", ty_ident(N(n->a).type), ty_ident(n->type)) : NULL;
    case N_INDEX:
        return fmtbuf("index.checked.%s", ty_ident(n->type));
    case N_CALL:
        return n->op == 0 ? "call.enter" : NULL;
    default:
        return NULL;
    }
}

static void assign_sites(int32_t root)
{
    int32_t j;
    if (root < 0) {
        return;
    }
    for (j = N(root).first; j <= root; j++) {
        const char *op;
        if (N(j).mc >= 0) {
            continue;
        }
        op = node_op(j);
        if (op != NULL) {
            N(j).site = site_add(N(j).tok, op);
        }
    }
}

/* -- constant evaluation through cint_rt.h ------------------------------------------------------ */

static const cint_site_info ce_sites[2] = {{0u, 0u, ""}, {1u, 1u, "const"}};
static const cint_module ce_module = {"const.ci", 8u, 2u, ce_sites};
static const cint_module *const ce_modules[1] = {&ce_module};
static const cint_program ce_program = {1u, 0u, ce_modules, NULL};
static cint_ctx *ce;
static cint_fault_record ce_rec;
#define CE_SITE ((cint_site){0u, 1u})

static const char *const code_names[] = {"none", "E_OVERFLOW", "E_DIV_ZERO", "E_BOUNDS", "E_SHAPE", "E_SHIFT",
                                         "E_NARROW", "E_ALIAS", "E_STALE_HANDLE", "E_FUEL", "E_UNSUPPORTED",
                                         "E_DOMAIN", "E_DEPTH", "E_ASSERT"};

static void ce_begin(void)
{
    if (ce == NULL) {
        cint_ctx_config cfg;
        memset(&cfg, 0, sizeof cfg);
        cfg.size = (uint32_t)sizeof cfg;
        cfg.program = &ce_program;
        (void)cint_ctx_create(&cfg, &ce); /* writes ce only on CINT_OK */
    }
    if (ce == NULL || cint_rt_entry_begin(ce, CE_SITE, CINT_FUEL_UNBOUNDED, 1) != CINT_OK) {
        diag_fatal("internal error: the constant evaluation context cannot start an entry");
    }
}

/* Reports the fault of a constant subexpression: C6001 carrying the fault. */
static _Noreturn void ce_fault(int32_t j, const char *text)
{
    diag_tok(cm, N(j).tok, "C6001", "constant evaluation faults; %s", text);
}

static void ce_end(int32_t j)
{
    char text[900];
    size_t n = 0;
    uint32_t i;
    char v[700];
    if (cint_rt_entry_end(ce) != CINT_FAULT) {
        return;
    }
    if (cint_ctx_fault(ce, &ce_rec) != CINT_OK || ce_rec.code > 13u) {
        diag_fatal("internal error: no constant fault record");
    }
    n += (size_t)snprintf(text + n, sizeof text - n, "fault.code %s; fault.operation %.*s",
                          code_names[ce_rec.code], (int)ce_rec.operation_len, ce_rec.operation);
    for (i = 0; i < ce_rec.operand_count && n < sizeof text; i++) {
        cint_tvalue_render(&ce_rec.operands[i], v, sizeof v);
        n += (size_t)snprintf(text + n, sizeof text - n, "; fault.operand %s", v);
    }
    if (n < sizeof text) {
        if (ce_rec.has_exact) {
            cint_tvalue_render_decimal(&ce_rec.exact, v, sizeof v);
        }
        n += (size_t)snprintf(text + n, sizeof text - n, "; fault.exact %s", ce_rec.has_exact ? v : "none");
    }
    if (n < sizeof text) {
        if (ce_rec.has_limit) {
            cint_tvalue_render(&ce_rec.limit, v, sizeof v);
        }
        snprintf(text + n, sizeof text - n, "; fault.limit %s", ce_rec.has_limit ? v : "none");
    }
    cint_ctx_clear_fault(ce);
    ce_fault(j, text);
}

#define CE_IN_S(v) (int64_t)(v)
#define CE_IN_U(v) (v)
#define CE_OUT_S(z) (uint64_t)(int64_t)(z)
#define CE_OUT_U(z) (uint64_t)(z)
#define CE_CASE(T, TY, CT, SG)                                             \
    case TY: {                                                             \
        CT x = (CT)CE_IN_##SG(a), y = (CT)CE_IN_##SG(b), z = 0;            \
        switch (op) {                                                      \
        case OP_ADD: (void)cint_add_##T(ce, CE_SITE, x, y, &z); break;     \
        case OP_SUB: (void)cint_sub_##T(ce, CE_SITE, x, y, &z); break;     \
        case OP_MUL: (void)cint_mul_##T(ce, CE_SITE, x, y, &z); break;     \
        case OP_DIV: (void)cint_div_##T(ce, CE_SITE, x, y, &z); break;     \
        case OP_REM: (void)cint_rem_##T(ce, CE_SITE, x, y, &z); break;     \
        case OP_ADDW: z = cint_add_wrap_##T(x, y); break;                  \
        case OP_SUBW: z = cint_sub_wrap_##T(x, y); break;                  \
        case OP_MULW: z = cint_mul_wrap_##T(x, y); break;                  \
        case OP_SHL: (void)cint_shl_##T(ce, CE_SITE, x, k, &z); break;     \
        case OP_SHLW: (void)cint_shl_wrap_##T(ce, CE_SITE, x, k, &z); break; \
        case OP_SHR: (void)cint_shr_##T(ce, CE_SITE, x, k, &z); break;     \
        default: (void)cint_neg_##T(ce, CE_SITE, x, &z); break;            \
        }                                                                  \
        r = CE_OUT_##SG(z);                                                \
        break;                                                             \
    }

static uint32_t ty_tag(int32_t t)
{
    static const uint32_t tags[] = {0, CINT_TAG_I8, CINT_TAG_I16, CINT_TAG_I32, CINT_TAG_I64,
                                    CINT_TAG_U8, CINT_TAG_U16, CINT_TAG_U32, CINT_TAG_U64};
    return tags[t];
}

/* op (or OP_NONE for negation) on type t, count type kt for shifts. */
static uint64_t ce_arith(int32_t j, int op, int32_t t, uint64_t a, uint64_t b, int32_t kt)
{
    uint64_t r = 0;
    cint_count k;
    k.bits = b;
    k.tag = kt > 0 ? ty_tag(kt) : CINT_TAG_I64;
    k.reserved = 0u;
    ce_begin();
    switch (t) {
    CE_CASE(i8, TY_I8, int8_t, S)
    CE_CASE(i16, TY_I16, int16_t, S)
    CE_CASE(i32, TY_I32, int32_t, S)
    CE_CASE(i64, TY_I64, int64_t, S)
    CE_CASE(u8, TY_U8, uint8_t, U)
    CE_CASE(u16, TY_U16, uint16_t, U)
    CE_CASE(u32, TY_U32, uint32_t, U)
    CE_CASE(u64, TY_U64, uint64_t, U)
    default:
        break;
    }
    ce_end(j);
    return r;
}

/* The operand is the source type's name (or Z) and the value; it is formatted
 * here, never passed in from fmtbuf, whose one buffer the record is built in. */
static void ce_narrow(int32_t j, const char *op, const char *from, const char *value, int32_t to, bool above)
{
    char operand[128];
    snprintf(operand, sizeof operand, "%s %s", from, value);
    ce_fault(j, fmtbuf("fault.code E_NARROW; fault.operation %s; fault.operand %s; fault.exact %s; "
                       "fault.limit %s %s", op, operand, value, ty_name(to),
                       above ? val_text(to, ty_max(to)) : val_text(to, wrap_to(to, ty_max(to) + 1u))));
}

/* Evaluates node j of a constant subexpression; its children are evaluated. */
static void eval_node(int32_t j)
{
    seed_node *n = &N(j);
    uint64_t a = n->a >= 0 && n->kind != N_CALL ? N(n->a).val : 0u;
    uint64_t b = n->b >= 0 && n->kind != N_CALL ? N(n->b).val : 0u;
    switch (n->kind) {
    case N_LIT:
        if (n->type != TY_Z && !z_fits(n->type, (n->flags & NF_NEG) != 0u, n->val, n->big != 0u)) {
            diag_tok(cm, n->tok, "C2003", "constant %s does not fit %s (%s ..= %s)",
                     z_text((n->flags & NF_NEG) != 0u, n->val, n->big != 0u), ty_name(n->type),
                     val_text(n->type, ty_signed(n->type) ? wrap_to(n->type, ty_max(n->type) + 1u) : 0u),
                     val_text(n->type, ty_max(n->type)));
        }
        n->val = (n->flags & NF_NEG) != 0u ? (uint64_t)0 - n->val : n->val;
        if (n->type != TY_Z) {
            n->val = wrap_to(n->type, n->val);
        }
        break;
    case N_BOOL:
        break;
    case N_NAME:
        n->val = S.syms[n->sym].val;
        break;
    case N_FIELD:
        n->val = S.syms[n->sym].val;
        break;
    case N_UNARY:
        if (n->op == OP_NOT) {
            n->val = a == 0u ? 1u : 0u;
        } else if (n->op == OP_TILDE) {
            n->val = wrap_to(n->type, ~a);
        } else {
            n->val = ce_arith(j, OP_NONE, n->type, a, 0u, 0);
        }
        break;
    case N_BIN:
        if (n->op == OP_BAND || n->op == OP_BOR || n->op == OP_BXOR) {
            n->val = n->op == OP_BAND ? a & b : n->op == OP_BOR ? a | b : a ^ b;
        } else {
            n->val = ce_arith(j, n->op, n->type, a, b, N(n->b).type);
        }
        break;
    case N_CMP: {
        int32_t t = N(n->a).type;
        int c = ty_signed(t) ? ((int64_t)a < (int64_t)b ? -1 : (int64_t)a > (int64_t)b)
                             : (a < b ? -1 : a > b);
        bool r = n->op == OP_EQ ? c == 0 : n->op == OP_NE ? c != 0 : n->op == OP_LT ? c < 0
               : n->op == OP_LE ? c <= 0 : n->op == OP_GT ? c > 0 : c >= 0;
        n->val = r ? 1u : 0u;
        break;
    }
    case N_SC:
        n->val = a;
        break;
    case N_LOGIC:
        n->val = (n->op == OP_LAND) ? (a != 0u && b != 0u) : (a != 0u || b != 0u);
        break;
    case N_CONV: {
        int32_t from = N(n->a).type, to = n->type;
        const seed_node *c = &N(n->a);
        if (from == TY_Z) {
            bool neg = (c->flags & NF_NEG) != 0u;
            uint64_t mag = neg ? (uint64_t)0 - a : a;
            if (n->op == OP_ASW) {
                n->val = wrap_to(to, a);
            } else if (z_fits(to, neg, mag, c->big != 0u)) {
                n->val = wrap_to(to, a);
            } else {
                char z[64];
                if (c->big != 0u) {
                    diag_tok(cm, n->tok, C_UNSUP, "the fault record of a conversion of a literal above "
                             "18446744073709551615 (SPEC-09 SEED-16)");
                }
                snprintf(z, sizeof z, "%s", z_text(neg, mag, false));
                ce_narrow(j, "unassigned", "Z", z, to, !neg);
            }
        } else if (from == TY_BOOL) {
            n->val = a;
        } else if (n->op == OP_ASW) {
            n->val = wrap_to(to, a);
        } else {
            bool neg = ty_signed(from) && (int64_t)a < 0;
            uint64_t mag = neg ? (uint64_t)0 - a : a;
            if (!z_fits(to, neg, mag, false)) {
                char v[32], op[48];
                snprintf(v, sizeof v, "%s", val_text(from, a));
                snprintf(op, sizeof op, "as.checked.%s.%s", ty_ident(from), ty_ident(to));
                ce_narrow(j, op, ty_name(from), v, to, !neg);
            }
            n->val = wrap_to(to, a);
        }
        break;
    }
    default:
        break;
    }
    n->flags |= NF_HASV;
}

/* Whether node j is checked at compile time where nothing around it is evaluated: a
 * literal, a constant name, or a checked conversion of a literal (SPEC-01 IM-26, D-17). */
static bool ce_leaf(int32_t j)
{
    int k = N(j).kind;
    return k == N_LIT || k == N_BOOL || k == N_NAME || k == N_FIELD || (k == N_CONV && N(N(j).a).type == TY_Z);
}

/* Marks maximal constant subexpressions and evaluates them in evaluation order
 * (SPEC-01 IM-23, slice 2 patch D-9). Below the right operand of a run-time && or
 * || (NF_COND) a constant operand is not evaluated: only its literals and literal
 * conversions are checked, and the rest runs if reached. Inside a constant
 * expression an operand that && or || skips is not evaluated; its literals and
 * literal conversions are checked after the expression, in source order. */
static void fold(int32_t root)
{
    int32_t first, j, k, skip_end = -1;
    if (root < 0) {
        return;
    }
    first = N(root).first;
    for (j = root; j >= first; j--) {
        seed_node *n = &N(j);
        int32_t p = j == root ? -1 : n->parent;
        n->mc = -1;
        n->flags = (uint8_t)(n->flags & (0xFFu ^ NF_COND ^ NF_SKIP));
        if (p >= 0 && ((N(p).flags & NF_COND) != 0u ||
                       (N(p).kind == N_LOGIC && (N(p).flags & NF_CONST) == 0u && N(p).b == j))) {
            n->flags |= NF_COND;
        }
        if ((n->flags & NF_CONST) != 0u) {
            n->mc = p >= 0 && N(p).mc >= 0 ? N(p).mc : (n->flags & NF_COND) == 0u || ce_leaf(j) ? j : -1;
        }
    }
    for (j = first; j <= root; j++) {
        int32_t m = N(j).mc;
        if (m < 0) {
            continue;
        }
        if (j <= skip_end) {
            N(j).flags |= NF_SKIP;
        } else {
            eval_node(j);
            if (N(j).kind == N_SC && ((N(j).op == OP_LAND) == (N(j).val == 0u)) && N(j).parent - 1 > skip_end) {
                skip_end = N(j).parent - 1;
            }
        }
        for (k = j == m ? N(m).first : m + 1; k <= m; k++) {
            if ((N(k).flags & NF_SKIP) != 0u && ce_leaf(k)) {
                eval_node(k);
            }
        }
    }
}


/* -- typing -------------------------------------------------------------------------------- */

static _Noreturn void type_error(int32_t j, const char *code, const char *msg)
{
    diag_tok(cm, N(j).stok, code, "%s", msg);
}

/* An operator defined on integers only (C2103, SPEC-05 X-10). */
static void require_int(int32_t t, int32_t j)
{
    if (!ty_is_int(t)) {
        type_error(j, "C2103", fmtbuf("the operator is defined on integer types, not on %s", ty_name(t)));
    }
}

/* Gives untyped nodes of the subtree at e the type t, parents first. */
static void settle(int32_t e, int32_t t)
{
    int32_t j;
    for (j = e; j >= N(e).first; j--) {
        seed_node *n = &N(j);
        int32_t pt;
        if (n->type != 0) {
            continue;
        }
        pt = j == e ? t : N(n->parent).type;
        if (!ty_is_int(pt)) {
            if (n->kind == N_LIT) {
                diag_tok(cm, n->tok, "C2001", "expected %s, found an integer literal", ty_name(pt));
            }
            type_error(j, "C2001", fmtbuf("expected %s, found an integer expression", ty_name(pt)));
        }
        n->type = pt;
    }
}

/* SPEC-01 IM-21 rule 4 into an untyped operand. The left operand of a literal-only
 * shift is never typed by rule 4 (SPEC-04 LS-57, D-9): the operand settles to I64
 * (rule 6), and another type on the other side is C2001. */
static void settle_rule4(int32_t e, int32_t t)
{
    int32_t j;
    for (j = N(e).first; j <= e; j++) {
        if (N(j).kind == N_BIN && op_is_shift(N(j).op) && N(j).type == 0 && t != TY_I64) {
            settle(e, TY_I64);
            type_error(e, "C2001", fmtbuf("expected %s, found I64: the left operand of a literal-only shift is "
                                          "typed I64 here, never by the other operand (SPEC-04 LS-57)", ty_name(t)));
        }
    }
    settle(e, t);
}

static void expect(int32_t e, int32_t t)
{
    int32_t got = N(e).type;
    if (got == 0) {
        settle(e, t);
        return;
    }
    if (got == TY_VOID) {
        type_error(e, "C2001", "a call of a void function has no value");
    }
    if (got != t) {
        type_error(e, "C2001", fmtbuf("expected %s, found %s", ty_name(t), ty_name(got)));
    }
}

static int32_t root_sym(int32_t e)
{
    while (N(e).kind == N_FIELD || N(e).kind == N_INDEX) {
        e = N(e).a;
    }
    return N(e).kind == N_NAME ? N(e).sym : -1;
}

static int64_t const_extent(int32_t e);

/* Resolves a written type. ctx 0: variable, field or result; 1: parameter of
 * the function f (views); a scalar or struct otherwise. */
static int32_t resolve_tref(int32_t tr, int ctx, const seed_func *f)
{
    const seed_tref *r = &M->trefs[tr];
    int32_t elem = r->qual < 0 ? int_type_of(M, r->tok) : 0, y;
    seed_type t;
    if (elem == 0) {
        if (r->qual >= 0) {
            int32_t q = gsym_find(cm, M, (uint32_t)r->qual);
            if (q < 0 || S.syms[q].kind != Y_IMPORT) {
                diag_tok(cm, (uint32_t)r->qual, "C3006", "`%.*s` is not an imported module",
                         (int)M->toks[r->qual].len, (const char *)M->src + M->toks[r->qual].off);
            }
            y = gsym_find(S.syms[q].decl, M, r->tok);
            if (y < 0 || S.syms[y].kind == Y_IMPORT) {
                diag_tok(cm, r->tok, "C3005", "unknown type `%.*s`", (int)M->toks[r->tok].len,
                         (const char *)M->src + M->toks[r->tok].off);
            }
            if (!S.syms[y].exported) {
                diag_tok(cm, r->tok, "C3008", "the struct is not exported by its module");
            }
            if (S.syms[y].kind != Y_STRUCT) {
                diag_tok(cm, r->tok, "C3007", "`%.*s` is not a type", (int)M->toks[r->tok].len,
                         (const char *)M->src + M->toks[r->tok].off);
            }
        } else {
            y = gsym_find(cm, M, r->tok);
            if (y < 0 || S.syms[y].kind != Y_STRUCT) {
                diag_tok(cm, r->tok, "C3006", "unknown type `%.*s`", (int)M->toks[r->tok].len,
                         (const char *)M->src + M->toks[r->tok].off);
            }
        }
        elem = S.structs[S.syms[y].decl].type;
    }
    if (r->shape == 0) {
        return elem;
    }
    memset(&t, 0, sizeof t);
    t.elem = elem;
    t.strct = -1;
    t.sp = -1;
    if (ctx == 1) {
        t.kind = K_VIEW;
        if (r->shape == 1) {
            const seed_node *x = &N(r->ext);
            int32_t k;
            t.sp = -2;
            for (k = 0; f != NULL && x->kind == N_NAME && x->first == r->ext && k < f->nsize; k++) {
                if (tok_eq(M, S.sizes[f->size0 + k], M, x->tok)) {
                    t.sp = k;
                }
            }
            if (t.sp == -2) {
                t.extent = const_extent(r->ext);
                if (signature_need >= 0) { return 0; }
            }
        }
        return t.kind == K_VIEW ? type_add(t) : elem;
    }
    if (r->shape == 2) {
        diag_tok(cm, r->tok, "C2001", "the extent `_` is permitted only in a view parameter");
    }
    t.kind = K_ARRAY;
    t.extent = const_extent(r->ext);
    if (signature_need >= 0) { return 0; }
    return type_add(t);
}

static void infer(int32_t root);

/* An array or view extent: a constant I64 expression of value >= 0. */
static int64_t const_extent(int32_t e)
{
    infer(e);
    if (signature_need >= 0) { return 0; }
    expect(e, TY_I64);
    if ((N(e).flags & NF_CONST) == 0u) {
        type_error(e, "C6004", "an extent must be a constant expression or a size parameter");
    }
    fold(e);
    if ((int64_t)N(e).val < 0) {
        type_error(e, "C2001", "an extent must not be negative");
    }
    return (int64_t)N(e).val;
}

/* Extent of a view or array argument: 1 constant, 2 the caller's size
 * parameter, 0 unknown. */
static int arg_extent(int32_t a, int64_t *v)
{
    const seed_type *t = &S.types[N(a).type];
    if (t->kind == K_ARRAY) {
        *v = t->extent;
        return 1;
    }
    if (t->lit) {
        *v = N(a).aux;
        return 1;
    }
    if (t->sp == -2) {
        *v = t->extent;
        return 1;
    }
    *v = t->sp;
    return t->sp >= 0 ? 2 : 0;
}

static void bind_user_call(int32_t j, int32_t fi)
{
    seed_node *n = &N(j);
    const seed_func *f = &S.funcs[fi];
    int32_t i;
    for (i = 0; i < n->b; i++) {
        if (M->argn[n->a + i] != UINT32_MAX) {
            diag_tok(cm, M->argn[n->a + i], C_BOOT, "a named argument of a function call is outside cint-boot-1 "
                     "(SPEC-09 5.5, args)");
        }
    }
    if (n->b != f->nparam) {
        type_error(j, "C2022", fmtbuf("the function takes %d arguments, not %d", (int)f->nparam, (int)n->b));
    }
    if (f->signature_state != 2u) {
        if (f->signature_state == 1u) {
            type_error(j, C_UNSUP, "a cyclic function-signature dependency is outside the reference surface");
        }
        signature_need = fi;
        signature_kind = DEP_SIGNATURE;
        return;
    }
}

static void user_argument(int32_t j, int32_t fi, int32_t i)
{
    const seed_node *n = &N(j);
    const seed_func *f = &S.funcs[fi];
    const seed_param *p = &S.params[f->param0 + i];
    int32_t a = M->args[n->a + i];
    if (N(a).type == TY_MOD) {
        type_error(a, "C3007", "a module name is not a value");
    }
    if (is_user(p->type, K_VIEW)) {
        const seed_type *pv = &S.types[p->type];
        int32_t at = N(a).type, r;
        if (!(is_user(at, K_ARRAY) || is_user(at, K_VIEW)) || S.types[at].elem != pv->elem) {
            type_error(a, "C2001", fmtbuf("expected a view of %s, found %s", ty_name(pv->elem), ty_name(at)));
        }
        r = root_sym(a);
        if (pv->perm && (r < 0 || !(S.syms[r].kind == Y_VAR || (S.syms[r].kind == Y_VIEW &&
                                                                 S.types[S.syms[r].type].perm)))) {
            type_error(a, "C2067", "an `inout` argument must be writable storage");
        }
    } else {
        expect(a, p->type);
    }
}

static void user_call(int32_t j, int32_t fi)
{
    seed_node *n = &N(j);
    const seed_func *f = &S.funcs[fi];
    int32_t i, k;
    /* Shapes: every view bound to one size parameter has one extent (SPEC-04 LS-117). */
    for (k = -2; k < f->nsize; k++) {
        int kind0 = -1;
        if (k == -1) {
            continue;   /* `_` views bind nothing */
        }
        int64_t v0 = 0;
        int32_t count = 0;
        for (i = 0; i < n->b; i++) {
            const seed_param *p = &S.params[f->param0 + i];
            const seed_type *pv = &S.types[p->type];
            int32_t a = M->args[n->a + i];
            int64_t v;
            int kind;
            if (!is_user(p->type, K_VIEW) || pv->sp != k) {
                continue;
            }
            kind = arg_extent(a, &v);
            if (k == -2) {
                kind0 = 1, v0 = pv->extent, count = 1;
            }
            if (count++ == 0) {
                kind0 = kind, v0 = v;
                continue;
            }
            if (kind0 == 1 && kind == 1 && v0 != v) {
                type_error(a, "C2012", fmtbuf("extent %" PRId64 " where %" PRId64 " is required", v, v0));
            }
            if (kind0 == 0 || kind != kind0 || v != v0) {
                type_error(a, C_UNSUP, "the extents of these views are known only at run time; the seed has no "
                           "E_SHAPE writer for the check (seed/OPEN.md SEED-OQ-05)");
            }
        }
    }
    /* Aliasing: an inout view must not overlap another argument (SPEC-04 LS-121). */
    for (i = 0; i < n->b; i++) {
        int32_t ai = M->args[n->a + i], ri = root_sym(ai), x;
        const seed_param *pi = &S.params[f->param0 + i];
        if (ri < 0 || N(ai).type < TY_USER) {
            continue;
        }
        for (x = 0; x < n->b; x++) {
            int32_t ax = M->args[n->a + x];
            const seed_param *px = &S.params[f->param0 + x];
            if (x != i && N(ax).type >= TY_USER && root_sym(ax) == ri &&
                ((is_user(pi->type, K_VIEW) && S.types[pi->type].perm) ||
                 (is_user(px->type, K_VIEW) && S.types[px->type].perm))) {
                type_error(x > i ? ax : ai, "C5010", "an `inout` argument overlaps another argument of the call");
            }
        }
    }
    n->type = f->result;
    n->aux = fi;
    n->op = 0;
    if (cfn >= 0) {
        GROW(S.calls, S.ncall + 1, S.cap_call, "call");
        S.calls[S.ncall++] = fi;
        S.calls[S.ncall++] = j;
    }
}

static void constructor(int32_t j, int32_t si)
{
    seed_node *n = &N(j);
    const seed_struct *st = &S.structs[si];
    const seed_module *sm = &S.mods[st->module];
    uint8_t *filled = seed_alloc((size_t)st->nfield + 1u, "argument");
    int32_t i, k, filled_n = 0;
    bool positional = true;
    for (i = 0; i < n->b; i++) {
        uint32_t name = M->argn[n->a + i];
        int32_t idx = -1;
        if (name == UINT32_MAX) {
            if (!positional) {
                diag_tok(cm, N(M->args[n->a + i]).stok, "C2069", "positional arguments come before named arguments");
            }
            idx = filled_n;
            if (idx >= st->nfield) {
                type_error(j, "C2022", fmtbuf("the struct has %d fields", (int)st->nfield));
            }
        } else {
            positional = false;
            for (k = 0; k < st->nfield; k++) {
                if (tok_eq(sm, S.fields[st->field0 + k].tok, M, name)) {
                    idx = k;
                }
            }
            if (idx < 0) {
                diag_tok(cm, name, "C2063", "the struct has no field `%.*s`", (int)M->toks[name].len,
                         (const char *)M->src + M->toks[name].off);
            }
            if (filled[idx]) {
                diag_tok(cm, name, "C2062", "the field receives more than one argument");
            }
        }
        filled[idx] = 1;
        filled_n++;
        M->argn[n->a + i] = (uint32_t)idx;
    }
    for (k = 0; k < st->nfield; k++) {
        if (!filled[k]) {
            type_error(j, "C2020", fmtbuf("the constructor does not supply every field: %.*s is missing",
                                          (int)sm->toks[S.fields[st->field0 + k].tok].len,
                                          (const char *)sm->src + sm->toks[S.fields[st->field0 + k].tok].off));
        }
    }
    free(filled);
    for (i = 0; i < n->b; i++) {
        int32_t ft = S.fields[st->field0 + (int32_t)M->argn[n->a + i]].type;
        if (is_user(ft, K_ARRAY)) {
            type_error(M->args[n->a + i], C_UNSUP, "an array field in a constructor (seed/OPEN.md SEED-OQ-06)");
        }
        expect(M->args[n->a + i], ft);
    }
    n->type = st->type;
    n->aux = si;
    n->op = 1;
}

static int32_t call_symbol(int32_t j)
{
    seed_node *n = &N(j);
    int32_t y;
    if (n->sym >= 0) {
        uint32_t q = (uint32_t)n->sym;
        int32_t ys = lookup(q, true);
        if (S.syms[ys].kind != Y_IMPORT) {
            diag_tok(cm, n->tok, C_BOOT, "a member call");
        }
        y = gsym_find(S.syms[ys].decl, M, n->tok);
        if (y < 0) {
            diag_tok(cm, n->tok, "C3005", "the module has no `%.*s`", (int)M->toks[n->tok].len,
                     (const char *)M->src + M->toks[n->tok].off);
        }
        if (!S.syms[y].exported) {
            diag_tok(cm, n->tok, "C3008", "`%.*s` is not exported by its module", (int)M->toks[n->tok].len,
                     (const char *)M->src + M->toks[n->tok].off);
        }
    } else {
        y = lookup(n->tok, true);
    }
    if (S.syms[y].kind != Y_FUNC && S.syms[y].kind != Y_STRUCT) {
        diag_tok(cm, n->tok, "C3007", "`%.*s` is not a function", (int)M->toks[n->tok].len,
                 (const char *)M->src + M->toks[n->tok].off);
    }
    return y;
}

static void condition_type(int32_t e)
{
    if (N(e).type != TY_BOOL) {
        type_error(e, "C2002", "a condition must have type Bool; write `x != 0`");
    }
}

static void infer_node(int32_t j)
{
    seed_node *n = &N(j);
    int32_t a = n->kind != N_CALL ? n->a : -1, b = n->kind != N_CALL ? n->b : -1;
    int32_t ta = a >= 0 ? N(a).type : 0, tb = b >= 0 ? N(b).type : 0, t, y;
    bool kc = (a < 0 || (N(a).flags & NF_CONST)) && (b < 0 || (N(b).flags & NF_CONST));
    if ((ta == TY_MOD && n->kind != N_FIELD) || tb == TY_MOD) {
        type_error(ta == TY_MOD ? a : b, "C3007", "a module name is not a value");
    }
    switch (n->kind) {
    case N_LIT:
        n->flags |= NF_CONST;
        break;
    case N_BOOL:
        n->type = TY_BOOL;
        n->flags |= NF_CONST;
        break;
    case N_STR: {
        seed_type st;
        memset(&st, 0, sizeof st);
        st.kind = K_VIEW, st.lit = 1, st.elem = TY_U8, st.strct = -1, st.sp = -1;
        n->type = type_add(st);
        break;
    }
    case N_NAME:
        y = lookup(n->tok, true);
        n->sym = y;
        switch (S.syms[y].kind) {
        case Y_FUNC:
            diag_tok(cm, n->tok, "C4010", "a function name without a call");
        case Y_STRUCT:
            diag_tok(cm, n->tok, "C3007", "a struct name is not a value");
        case Y_IMPORT:
            n->type = TY_MOD;
            break;
        case Y_CONST:
            if (S.syms[y].state != 2u) {
                if (S.syms[y].state == 1u) {
                    type_error(j, "C6005", "the constant depends on itself");
                }
                signature_need = y;
                signature_kind = DEP_CONSTANT;
                return;
            }
            n->type = S.syms[y].type;
            n->flags |= NF_CONST;
            break;
        default:
            n->type = S.syms[y].type;
            n->flags |= NF_PLACE;
        }
        break;
    case N_UNARY:
        if (n->op == OP_NOT) {
            if (ta != TY_BOOL) {
                type_error(a, "C2002", "`!` needs a Bool operand; write `x == 0`");
            }
            n->type = TY_BOOL;
        } else if (ta != 0) {
            require_int(ta, a);
            n->type = ta;
        }
        if (kc) {
            n->flags |= NF_CONST;
        }
        break;
    case N_BIN:
        if (op_is_shift(n->op)) {
            if (tb == 0) {
                settle(b, TY_I64);
            } else {
                require_int(tb, b);
            }
            if (ta != 0) {
                require_int(ta, a);
            }
            t = ta;
        } else if (ta == 0 && tb == 0) {
            t = 0;
        } else if (ta == 0) {
            require_int(tb, b);
            settle_rule4(a, tb);
            t = tb;
        } else if (tb == 0) {
            require_int(ta, a);
            settle_rule4(b, ta);
            t = ta;
        } else {
            if (ta != tb) {
                type_error(b, "C2001", fmtbuf("operands of `%s` have types %s and %s", op_text(n->op),
                                              ty_name(ta), ty_name(tb)));
            }
            t = ta;
        }
        if (t != 0) {
            require_int(t, j);
        }
        n->type = t;
        if (kc) {
            n->flags |= NF_CONST;
        }
        break;
    case N_CMP:
        if (ta == 0 && tb == 0) {
            settle(a, TY_I64);
            settle(b, TY_I64);
            t = TY_I64;
        } else if (ta == 0) {
            settle_rule4(a, tb);
            t = tb;
        } else if (tb == 0) {
            settle_rule4(b, ta);
            t = ta;
        } else {
            if (ta != tb) {
                type_error(b, "C2001", fmtbuf("operands of `%s` have types %s and %s", op_text(n->op),
                                              ty_name(ta), ty_name(tb)));
            }
            t = ta;
        }
        if (!ty_is_int(t) && t != TY_BOOL) {
            type_error(j, "C2103", fmtbuf("values of type %s cannot be compared", ty_name(t)));
        }
        if (t == TY_BOOL && n->op != OP_EQ && n->op != OP_NE) {
            type_error(j, "C2103", "Bool supports only `==` and `!=`");
        }
        n->type = TY_BOOL;
        if (kc) {
            n->flags |= NF_CONST;
        }
        break;
    case N_SC:
        n->type = ta;
        if (kc) {
            n->flags |= NF_CONST;
        }
        break;
    case N_LOGIC:
        condition_type(N(a).a);
        condition_type(b);
        n->type = TY_BOOL;
        if (kc) {
            n->flags |= NF_CONST;
        }
        break;
    case N_CONV: {
        int32_t to = resolve_tref(n->aux, 0, NULL), from;
        if (!ty_is_int(to) && to != TY_BOOL) {
            type_error(j, "C2008", fmtbuf("there is no conversion to %s", ty_name(to)));
        }
        if (N(a).kind == N_LIT) {
            N(a).type = TY_Z;   /* a literal source, parenthesized or not, is a value in Z (SPEC-01 IM-26, D-21) */
            from = TY_Z;
        } else {
            from = ta;
            if (from == 0) {
                settle(a, TY_I64);
                from = TY_I64;
            }
        }
        if (to == TY_BOOL) {
            type_error(j, (ty_is_int(from) || from == TY_Z) ? "C2056" : "C2008",
                       "there is no conversion from integers to Bool; write `x != 0`");
        }
        if (!(ty_is_int(from) || from == TY_Z || from == TY_BOOL)) {
            type_error(j, "C2008", fmtbuf("there is no conversion from %s", ty_name(from)));
        }
        if (from == TY_BOOL && n->op == OP_ASW) {
            type_error(j, "C2008", "Bool converts to integers only with `as`");
        }
        n->type = to;
        if (kc) {
            n->flags |= NF_CONST;
        }
        break;
    }
    case N_FIELD:
        if (ta == TY_MOD) {
            int32_t mm = S.syms[N(a).sym].decl;
            y = gsym_find(mm, M, n->tok);
            if (y < 0) {
                diag_tok(cm, n->tok, "C3005", "the module has no `%.*s`", (int)M->toks[n->tok].len,
                         (const char *)M->src + M->toks[n->tok].off);
            }
            if (!S.syms[y].exported) {
                diag_tok(cm, n->tok, "C3008", "`%.*s` is not exported by its module", (int)M->toks[n->tok].len,
                         (const char *)M->src + M->toks[n->tok].off);
            }
            if (S.syms[y].kind != Y_CONST) {
                diag_tok(cm, n->tok, S.syms[y].kind == Y_FUNC ? "C4010" : "C3007", "only a constant of "
                         "another module is a value");
            }
            n->sym = y;
            n->op = 1;
            n->type = S.syms[y].type;
            n->flags |= NF_CONST;
        } else {
            const seed_struct *st;
            const seed_module *sm;
            int32_t k;
            if (!is_user(ta, K_STRUCT)) {
                diag_tok(cm, n->tok, "C2103", "a field access needs a struct value, found %s", ty_name(ta));

            }
            st = &S.structs[S.types[ta].strct];
            sm = &S.mods[st->module];
            for (k = 0; k < st->nfield; k++) {
                if (tok_eq(sm, S.fields[st->field0 + k].tok, M, n->tok)) {
                    n->aux = st->field0 + k;
                }
            }
            if (n->aux < 0) {
                diag_tok(cm, n->tok, "C3005", "struct %s has no field `%.*s`", ty_name(ta), (int)M->toks[n->tok].len,
                         (const char *)M->src + M->toks[n->tok].off);
            }
            n->type = S.fields[n->aux].type;
            n->flags |= (uint8_t)(N(a).flags & NF_PLACE);
        }
        break;
    case N_INDEX:
        if (!is_user(ta, K_ARRAY) && !is_user(ta, K_VIEW)) {
            diag_tok(cm, n->tok, "C2103", "indexing needs an array or a view, found %s", ty_name(ta));
        }
        if (tb == 0) {
            settle(b, TY_I64);
        } else if (tb != TY_I64) {
            type_error(b, "C2001", fmtbuf("an index has type I64, found %s", ty_name(tb)));
        }
        n->type = S.types[ta].elem;
        n->flags |= (uint8_t)(N(a).flags & NF_PLACE);
        break;
    default:
        break;
    }
}

static void expression_push(int32_t node)
{
    expr_frame *f = &expression_frames[expression_sp++];
    f->node = node;
    f->step = f->arg = 0;
    f->callee = -1;
}

static void infer(int32_t root)
{
    size_t base = expression_sp;
    if (signature_sp > 0 && signature_frames[signature_sp - 1].root == root) {
        base = signature_frames[signature_sp - 1].base;
    } else {
        expression_push(root);
    }
    while (expression_sp > base) {
        expr_frame *f = &expression_frames[expression_sp - 1];
        int32_t j = f->node;
        seed_node *n = &N(j);
        if (n->kind == N_CALL) {
            if (f->step == 0) {
                if (f->callee < 0) { f->callee = call_symbol(j); }
                if (S.syms[f->callee].kind == Y_FUNC) {
                    bind_user_call(j, S.syms[f->callee].decl);
                }
                if (signature_need < 0) { f->step = 1; }
            }
            if (signature_need < 0) {
                int32_t decl = S.syms[f->callee].decl;
                if (f->step == 2) {
                    if (S.syms[f->callee].kind == Y_FUNC) { user_argument(j, decl, f->arg); }
                    f->arg++;
                    f->step = 1;
                }
                if (f->arg < n->b) {
                    f->step = 2;
                    expression_push(M->args[n->a + f->arg]);
                    continue;
                }
                if (S.syms[f->callee].kind == Y_FUNC) { user_call(j, decl); }
                else { constructor(j, decl); }
                expression_sp--;
            }
        } else {
            if (f->step == 0) {
                f->step = 1;
                if (n->a >= 0) { expression_push(n->a); continue; }
            }
            if (f->step == 1) {
                f->step = 2;
                if (n->b >= 0) { expression_push(n->b); continue; }
            }
            infer_node(j);
            if (signature_need < 0) { expression_sp--; }
        }
        if (signature_need >= 0) {
            if (signature_sp > 0) {
                signature_frames[signature_sp - 1].root = root;
                signature_frames[signature_sp - 1].base = base;
                return;
            }
            ensure_declaration(signature_kind, signature_need);
        }
    }
    if (signature_sp > 0) { signature_frames[signature_sp - 1].root = -1; }
    if (N(root).type == TY_MOD) {
        type_error(root, "C3007", "a module name is not a value");
    }
}

/* -- declarations ------------------------------------------------------------------------- */

static void module_const(int32_t s)
{
    seed_stmt *st = &M->stmts[s];
    int32_t t = resolve_tref(st->tref, 0, NULL), y = gsym_find(cm, M, st->name);
    if (signature_need >= 0) { return; }
    if (!ty_is_int(t)) {
        diag_tok(cm, st->tok, C_BOOT, "a constant of type %s (cint-boot-1 constants have an integer type, "
                 "SPEC-09 5.5)", ty_name(t));
    }
    infer(st->e1);
    if (signature_need >= 0) { return; }
    expect(st->e1, t);
    if ((N(st->e1).flags & NF_CONST) == 0u) {
        type_error(st->e1, "C6004", "the initializer of a constant must be a constant expression");
    }
    fold(st->e1);
    if (y < 0) {
        y = sym_add(Y_CONST, cm, st->name, t, s);
        declare(st->name, y);
    }
    S.syms[y].type = t;
    S.syms[y].val = N(st->e1).val;
    S.syms[y].state = 2;
    st->sym = y;
}

/* Demands each remaining constant in source order. */
static void module_consts(void)
{
    int32_t i;
    for (i = 0; i < M->nconst; i++) {
        int32_t y0 = gsym_find(cm, M, M->stmts[M->consts[i]].name);
        ensure_declaration(DEP_CONSTANT, y0);
    }
}

static int64_t type_size(int32_t t, int64_t *align)
{
    if (t == TY_BOOL) {
        *align = 1;
        return 1;
    }
    if (ty_is_int(t)) {
        *align = (int64_t)(ty_width(t) / 8u);
        return *align;
    }
    if (S.types[t].kind == K_STRUCT) {
        *align = S.structs[S.types[t].strct].align;
        return S.structs[S.types[t].strct].size;
    }
    return S.types[t].extent * type_size(S.types[t].elem, align);
}

static int32_t field_struct(int32_t t)
{
    if (is_user(t, K_ARRAY)) {
        t = S.types[t].elem;
    }
    return is_user(t, K_STRUCT) ? S.types[t].strct : -1;
}

/* Field types, then layouts in containment order (SPEC-04 LS-73). */
static void module_structs(void)
{
    int32_t i, k, *stack = seed_alloc((size_t)(S.nstruct + 1) * sizeof *stack, "struct");
    for (i = 0; i < S.nstruct; i++) {
        seed_struct *st = &S.structs[i];
        if (st->module != cm) {
            continue;
        }
        if (st->nfield == 0) {
            diag_tok(cm, st->tok, C_UNSUP, "a struct without fields (C17 has no empty struct; seed/OPEN.md)");
        }
        for (k = 0; k < st->nfield; k++) {
            seed_field *f = &S.fields[st->field0 + k];
            int32_t j;
            for (j = 0; j < k; j++) {
                if (tok_eq(M, S.fields[st->field0 + j].tok, M, f->tok)) {
                    diag_tok(cm, f->tok, "C3001", "a field of this name is already declared");
                }
            }
            f->type = resolve_tref(f->tref, 0, NULL);
        }
    }
    for (i = 0; i < S.nstruct; i++) {
        int32_t sp = 0;
        if (S.structs[i].module != cm || S.structs[i].done == 2) {
            continue;
        }
        stack[sp++] = i;
        S.structs[i].done = 1;
        while (sp > 0) {
            seed_struct *st = &S.structs[stack[sp - 1]];
            int32_t dep = -1;
            int64_t off = 0, al = 1;
            for (k = 0; k < st->nfield && dep < 0; k++) {
                int32_t fs = field_struct(S.fields[st->field0 + k].type);
                if (fs >= 0 && S.structs[fs].done != 2) {
                    if (S.structs[fs].done == 1) {
                        diag_tok(cm, S.fields[st->field0 + k].tok, "C2055", "the struct contains itself");
                    }
                    dep = fs;
                }
            }
            if (dep >= 0) {
                S.structs[dep].done = 1;
                stack[sp++] = dep;
                continue;
            }
            for (k = 0; k < st->nfield; k++) {
                int64_t fa = 1, fsz = type_size(S.fields[st->field0 + k].type, &fa);
                off = (off + fa - 1) / fa * fa + fsz;
                al = fa > al ? fa : al;
            }
            st->size = (off + al - 1) / al * al;
            st->align = al;
            st->done = 2;
            sp--;
        }
    }
    free(stack);
}

static void signature(int32_t fi)
{
    seed_func *f = &S.funcs[fi];
    int32_t i, k;
    /* Checks size names before parameter types, without publishing body symbols. */
    for (k = 0; k < f->nsize; k++) {
        uint32_t tok = S.sizes[f->size0 + k];
        bool duplicate = gsym_find(cm, M, tok) >= 0 || is_prelude(M, tok);
        for (i = 0; i < k; i++) {
            duplicate = duplicate || tok_eq(M, S.sizes[f->size0 + i], M, tok);
        }
        if (duplicate) {
            diag_tok(cm, tok, "C3001", "`%.*s` is already declared and visible here", (int)M->toks[tok].len,
                     (const char *)M->src + M->toks[tok].off);
        }
    }
    for (i = 0; i < f->nparam; i++) {
        seed_param *p = &S.params[f->param0 + i];
        int32_t t;
        if (p->type != 0) { continue; }
        t = resolve_tref(p->tref, 1, f);
        if (signature_need >= 0) { return; }
        if (is_user(t, K_VIEW)) {
            seed_type v = S.types[t];
            if (!(v.elem == TY_U8 || v.elem == TY_I32 || v.elem == TY_I64 || is_user(v.elem, K_STRUCT))) {
                diag_tok(cm, p->tok, C_BOOT, "a view of %s (cint-boot-1 views have element type U8, I32, I64 or a "
                         "struct, SPEC-09 5.5)", ty_name(v.elem));
            }
            v.perm = p->mode;
            t = type_add(v);
        } else if (p->mode) {
            diag_tok(cm, p->tok, is_user(t, K_STRUCT) ? C_BOOT : "C2064", "an `inout` %s parameter (`inout` "
                     "applies to arrays%s)", ty_name(t), is_user(t, K_STRUCT) ? "; cint-boot-1 passes structs `in`" : "");
        }
        p->type = t;
    }
    for (k = 0; k < f->nsize; k++) {
        bool bound = false;
        for (i = 0; i < f->nparam; i++) {
            const seed_type *v = &S.types[S.params[f->param0 + i].type];
            bound = bound || (S.params[f->param0 + i].type >= TY_USER && v->kind == K_VIEW && v->sp == k);
        }
        if (!bound) {
            diag_tok(cm, S.sizes[f->size0 + k], "C2065", "the size parameter is not the extent of any view "
                     "parameter");
        }
    }
    f->result = TY_VOID;
    if (f->rtref >= 0) {
        f->result = resolve_tref(f->rtref, 0, f);
        if (signature_need >= 0) { return; }
        if (!ty_is_int(f->result) && f->result != TY_BOOL) {
            diag_tok(cm, M->trefs[f->rtref].tok, C_BOOT, "a result of type %s (cint-boot-1 results are void, an "
                     "integer type or Bool, SPEC-09 5.5)", ty_name(f->result));
        }
    }
    if (cm == 0 && f->exported) {
        for (i = 0; i < f->nparam; i++) {
            const seed_param *p = &S.params[f->param0 + i];
            if (is_user(p->type, K_STRUCT)) {
                diag_tok(cm, p->tok, C_UNSUP, "a struct parameter of an exported entry (SPEC-03 A-12 has no struct "
                         "mapping in the seed, seed/OPEN.md SEED-OQ-03)");
            }
        }
    }
}

/* Resolves requested declarations in demand order using input-sized frames.
 * infer returns a pending dependency while this driver is active, so a chain
 * never nests this driver on the native stack. */
static void ensure_declaration(int32_t kind, int32_t item)
{
    int32_t saved_module = cm, saved_local = nlocal, saved_function = cfn;
    if ((kind == DEP_SIGNATURE ? S.funcs[item].signature_state : S.syms[item].state) == 2u) { return; }
    signature_frames[0].kind = kind;
    signature_frames[0].item = item;
    signature_frames[0].root = -1;
    signature_sp = 1;
    nlocal = 0;
    cfn = -1;
    while (signature_sp > 0) {
        int32_t current = signature_frames[signature_sp - 1].item;
        int32_t current_kind = signature_frames[signature_sp - 1].kind;
        if (current_kind == DEP_SIGNATURE) {
            S.funcs[current].signature_state = 1;
            cm = S.funcs[current].module;
        } else {
            S.syms[current].state = 1;
            cm = S.syms[current].module;
        }
        M = &S.mods[cm];
        signature_need = -1;
        if (current_kind == DEP_SIGNATURE) {
            signature(current);
        } else {
            module_const(S.syms[current].decl);
        }
        if (signature_need >= 0) {
            signature_frames[signature_sp].kind = signature_kind;
            signature_frames[signature_sp].item = signature_need;
            signature_frames[signature_sp].root = -1;
            signature_sp++;
        } else {
            if (current_kind == DEP_SIGNATURE) { S.funcs[current].signature_state = 2; }
            signature_sp--;
        }
    }
    cm = saved_module;
    M = &S.mods[cm];
    nlocal = saved_local;
    cfn = saved_function;
}

/* -- statements --------------------------------------------------------------------------- */

enum { FR_BLOCK, FR_IF, FR_LOOP, FR_SWITCH };
typedef struct cfr {
    uint8_t kind, cc, any, has_else, broken, has_default, pad[2];
    int32_t mark;     /* nlocal at entry */
    int32_t ibase;    /* switch: first interval */
    int32_t type;     /* switch: scrutinee type */
    uint32_t tok;
} cfr;

typedef struct ival {
    uint64_t lo, hi;  /* order keys */
} ival;

static ival *ivals;
static int32_t nival, cap_ival;

static uint64_t key(int32_t t, uint64_t v)
{
    return ty_signed(t) ? v ^ ((uint64_t)1 << 63) : v;
}

static void check_place(int32_t e)
{
    int32_t r = e, y;
    const seed_sym *s;
    while (N(r).kind == N_FIELD || N(r).kind == N_INDEX) {
        r = N(r).a;
    }
    y = N(r).sym;
    s = &S.syms[y];
    if (N(r).kind != N_NAME || N(r).type == TY_MOD) {
        type_error(e, "C2058", "the left side of an assignment must be a variable, a field or an element");
    }
    if (is_user(N(e).type, K_ARRAY) || is_user(N(e).type, K_VIEW)) {
        type_error(e, C_UNSUP, "array assignment (seed/OPEN.md SEED-OQ-06)");
    }
    switch (s->kind) {
    case Y_LOOP:
        type_error(e, "C2052", "the loop variable is read-only");
    case Y_CONST:
        type_error(e, "C2058", "a constant cannot be assigned");
    case Y_PARAM:
    case Y_SIZE:
        type_error(e, "C2060", "a parameter cannot be assigned unless it is `inout`");
    case Y_VIEW:
        if (!S.types[s->type].perm) {
            type_error(e, "C2060", "an `in` view is read-only");
        }
        break;
    default:
        break;
    }
}

static void stmt_expr(int32_t e)
{
    fold(e);
    assign_sites(e);
}

static void check_function(int32_t fi)
{
    seed_func *f = &S.funcs[fi];
    cfr *fr = seed_alloc((size_t)(f->body_end - f->body + 2) * sizeof *fr, "statement");
    int32_t nfr = 0, i, k;
    bool body_cc = false;
    cfn = fi;
    nlocal = 0;
    f->call0 = S.ncall;
    for (k = 0; k < f->nsize; k++) {
        declare(S.sizes[f->size0 + k], sym_add(Y_SIZE, cm, S.sizes[f->size0 + k], TY_I64, fi));
    }
    for (k = 0; k < f->nparam; k++) {
        seed_param *p = &S.params[f->param0 + k];
        p->sym = sym_add(is_user(p->type, K_VIEW) ? Y_VIEW : Y_PARAM, cm, p->tok, p->type, fi);
        declare(p->tok, p->sym);
    }
    if (f->exported) {
        f->entry_site = site_add(f->tok, "fuel.charge");
    }
    for (i = f->body; i <= f->body_end; i++) {
        seed_stmt *s = &M->stmts[i];
        int complete = -1;   /* the completion of a finished statement, or -1 */
        cfr *top = nfr > 0 ? &fr[nfr - 1] : NULL;
        int32_t t;
        switch (s->kind) {
        case S_BLOCK:
            memset(&fr[nfr], 0, sizeof fr[nfr]);
            fr[nfr].kind = FR_BLOCK, fr[nfr].cc = 1, fr[nfr].mark = nlocal;
            nfr++;
            break;
        case S_END:
            nfr--;
            nlocal = fr[nfr].mark;
            if (nfr == 0) {
                body_cc = fr[0].cc != 0u;
            } else {
                complete = fr[nfr].cc;
            }
            break;
        case S_VAR:
            if (M->trefs[s->tref].shape != 0 && s->e1 >= 0) {
                type_error(s->e1, C_BOOT, "an array initializer (an array is zero-filled, SPEC-09 5.5)");
            }
            t = resolve_tref(s->tref, 0, NULL);
            if (is_user(t, K_ARRAY)) {
                int64_t al, size = type_size(t, &al);
                if (size > 4096) {
                    diag_tok(cm, s->name, "C9003", "a local array of %" PRId64 " bytes; the limit is 4,096 "
                             "(SPEC-09 EMIT-30)", size);
                }
            }
            if (s->e1 < 0) {
                if (t < TY_USER) {
                    diag_tok(cm, s->name, "C2050", "a scalar variable needs an initializer");
                }
            } else {
                infer(s->e1);
                expect(s->e1, t);
                stmt_expr(s->e1);
            }
            s->sym = sym_add(Y_VAR, cm, s->name, t, i);
            declare(s->name, s->sym);
            complete = 1;
            break;
        case S_CONST:
            t = resolve_tref(s->tref, 0, NULL);
            if (!ty_is_int(t)) {
                diag_tok(cm, s->tok, C_BOOT, "a constant of type %s", ty_name(t));
            }
            infer(s->e1);
            expect(s->e1, t);
            if ((N(s->e1).flags & NF_CONST) == 0u) {
                type_error(s->e1, "C6004", "the initializer of a constant must be a constant expression");
            }
            fold(s->e1);
            s->sym = sym_add(Y_CONST, cm, s->name, t, i);
            S.syms[s->sym].val = N(s->e1).val;
            S.syms[s->sym].state = 2;
            declare(s->name, s->sym);
            complete = 1;
            break;
        case S_ASSIGN:
        case S_INCDEC: {
            int bop = s->kind == S_INCDEC ? (s->op == OP_INC ? OP_ADD : OP_SUB) : op_of_assign(s->op);
            infer(s->e1);
            check_place(s->e1);
            t = N(s->e1).type;
            if (s->kind == S_ASSIGN) {
                infer(s->e2);
                if (bop == 0) {
                    expect(s->e2, t);
                } else {
                    require_int(t, s->e1);
                    if (op_is_shift(bop)) {
                        if (N(s->e2).type == 0) {
                            settle(s->e2, TY_I64);
                        } else {
                            require_int(N(s->e2).type, s->e2);
                        }
                    } else {
                        expect(s->e2, t);
                    }
                }
            } else {
                require_int(t, s->e1);
            }
            stmt_expr(s->e1);
            stmt_expr(s->e2);
            if (bop != 0 && arith_name(bop) != NULL) {
                s->site = site_add(s->tok, fmtbuf("%s.%s.%s", arith_name(bop), bop == OP_SHLW ? "wrap" : "checked",
                                                  ty_ident(t)));
            }
            complete = 1;
            break;
        }
        case S_CALL:
            infer(s->e1);
            if (N(s->e1).type != TY_VOID) {
                type_error(s->e1, "C4011", "the result of the call must be used or discarded with `_ =`");
            }
            stmt_expr(s->e1);
            complete = 1;
            break;
        case S_DISCARD:
            if (N(s->e1).kind != N_CALL || (N(s->e1).flags & NF_PAREN) != 0u) {
                type_error(s->e1, C_BOOT, "`_ =` of an expression that is not a call (SPEC-09 5.5, simple_stmt)");
            }
            infer(s->e1);
            if (N(s->e1).type == TY_VOID) {
                type_error(s->e1, "C2001", "a call of a void function has no value");
            }
            stmt_expr(s->e1);
            complete = 1;
            break;
        case S_RETURN:
            if (f->result == TY_VOID && s->e1 >= 0) {
                type_error(s->e1, "C2066", "a void function returns no value");
            }
            if (f->result != TY_VOID && s->e1 < 0) {
                diag_tok(cm, s->tok, "C2066", "`return` needs a value of type %s", ty_name(f->result));
            }
            if (s->e1 >= 0) {
                infer(s->e1);
                expect(s->e1, f->result);
                stmt_expr(s->e1);
            }
            complete = 0;
            break;
        case S_BREAK:
        case S_CONTINUE:
            for (k = nfr - 1; k >= 0; k--) {
                if (fr[k].kind == FR_LOOP || (fr[k].kind == FR_SWITCH && s->kind == S_BREAK)) {
                    break;
                }
            }
            if (k < 0) {
                diag_tok(cm, s->tok, "C4013", "`%s` outside a loop%s", s->kind == S_BREAK ? "break" : "continue",
                         s->kind == S_BREAK ? " or switch" : "");
            }
            if (fr[k].kind == FR_SWITCH) {
                fr[k].broken = 1;
            }
            complete = 0;
            break;
        case S_IF:
        case S_ELSEIF:
        case S_WHILE:
        case S_FORCOND:
            infer(s->e1);
            condition_type(s->e1);
            stmt_expr(s->e1);
            if (s->kind == S_IF) {
                memset(&fr[nfr], 0, sizeof fr[nfr]);
                fr[nfr++].kind = FR_IF;
            } else if (s->kind == S_WHILE) {
                memset(&fr[nfr], 0, sizeof fr[nfr]);
                fr[nfr].kind = FR_LOOP, fr[nfr].mark = nlocal;
                nfr++;
            }
            if (s->kind == S_WHILE || s->kind == S_FORCOND) {
                s->site = site_add(s->tok, "fuel.charge");
            }
            break;
        case S_ELSE:
            top->has_else = 1;
            break;
        case S_ENDIF:
            nfr--;
            complete = !fr[nfr].has_else || fr[nfr].any;
            break;
        case S_FORC:
            memset(&fr[nfr], 0, sizeof fr[nfr]);
            fr[nfr].kind = FR_LOOP, fr[nfr].mark = nlocal;
            nfr++;
            break;
        case S_FORR:
            infer(s->e1);
            infer(s->e2);
            for (k = 0; k < 2; k++) {
                int32_t e = k == 0 ? s->e1 : s->e2;
                if (N(e).type == 0) {
                    settle(e, TY_I64);
                } else if (N(e).type != TY_I64) {
                    type_error(e, "C2001", fmtbuf("a range bound has type %s, not I64 (SPEC-01 2.2; "
                                                  "ref/OPEN.md REF-OQ-07)", ty_name(N(e).type)));
                }
            }
            stmt_expr(s->e1);
            stmt_expr(s->e2);
            s->site = site_add(s->tok, "fuel.charge");
            memset(&fr[nfr], 0, sizeof fr[nfr]);
            fr[nfr].kind = FR_LOOP, fr[nfr].mark = nlocal;
            nfr++;
            s->sym = sym_add(Y_LOOP, cm, s->name, TY_I64, i);
            declare(s->name, s->sym);
            break;
        case S_ENDLOOP:
            nfr--;
            nlocal = fr[nfr].mark;
            complete = 1;
            break;
        case S_SWITCH:
            infer(s->e1);
            t = N(s->e1).type;
            if (t == 0) {
                settle(s->e1, TY_I64);
                t = TY_I64;
            }
            if (!ty_is_int(t)) {
                type_error(s->e1, t == TY_BOOL ? C_BOOT : "C4026", fmtbuf("a switch over %s (cint-boot-1 "
                                                                         "switches over integers)", ty_name(t)));
            }
            stmt_expr(s->e1);
            memset(&fr[nfr], 0, sizeof fr[nfr]);
            fr[nfr].kind = FR_SWITCH, fr[nfr].ibase = nival, fr[nfr].type = t, fr[nfr].tok = s->tok;
            nfr++;
            break;
        case S_CASE:
            t = top->type;
            for (k = 0; k < s->count; k++) {
                seed_case_item *it = &M->items[s->link + k];
                uint64_t v[2];
                int x, z;
                for (x = 0; x < 2; x++) {
                    int32_t e = x == 0 ? it->lo : (it->hi >= 0 ? it->hi : it->lo);
                    if (x == 0 || it->hi >= 0) {
                        infer(e);
                        expect(e, t);
                        if ((N(e).flags & NF_CONST) == 0u) {
                            type_error(e, "C6004", "a case item must be a constant expression");
                        }
                        fold(e);
                    }
                    v[x] = key(t, N(e).val);
                }
                if (v[0] > v[1]) {
                    type_error(it->lo, "C4024", "an empty case range");
                }
                for (z = top->ibase; z < nival; z++) {
                    if (v[0] <= ivals[z].hi && ivals[z].lo <= v[1]) {
                        type_error(it->lo, "C4021", "overlapping or duplicate case item");
                    }
                }
                GROW(ivals, nival, cap_ival, "case");
                ivals[nival].lo = v[0], ivals[nival].hi = v[1];
                nival++;
            }
            break;
        case S_DEFAULT:
            top->has_default = 1;
            break;
        case S_ENDSWITCH:
            nfr--;
            if (!fr[nfr].has_default) {
                uint64_t lo = key(fr[nfr].type, ty_signed(fr[nfr].type) ? wrap_to(fr[nfr].type, ty_max(fr[nfr].type) + 1u) : 0u);
                uint64_t hi = key(fr[nfr].type, ty_max(fr[nfr].type));
                bool done = false;
                while (!done) {
                    bool moved = false;
                    for (k = fr[nfr].ibase; k < nival; k++) {
                        if (ivals[k].lo <= lo && ivals[k].hi >= lo) {
                            if (ivals[k].hi >= hi) {
                                done = true;
                            } else {
                                lo = ivals[k].hi + 1u;
                            }
                            moved = true;
                            break;
                        }
                    }
                    if (!moved && !done) {
                        diag_tok(cm, fr[nfr].tok, "C4020", "the switch is not exhaustive: add `default` or cover "
                                 "every value of %s", ty_name(fr[nfr].type));
                    }
                }
            }
            nival = fr[nfr].ibase;
            complete = fr[nfr].broken || fr[nfr].any;
            break;
        default:
            break;
        }
        if (complete >= 0 && nfr > 0) {
            cfr *p = &fr[nfr - 1];
            if (p->kind == FR_BLOCK) {
                p->cc = (uint8_t)(p->cc && complete);
            } else if (p->kind == FR_IF || p->kind == FR_SWITCH) {
                p->any = (uint8_t)(p->any || complete);
            }
        }
    }
    if (f->result != TY_VOID && body_cc) {
        diag_tok(cm, f->tok, "C4001", "the function can reach its closing brace without returning a value");
    }
    f->ncall = (S.ncall - f->call0) / 2;
    free(fr);
    cfn = -1;
}

void check_program(void)
{
    int32_t o, i;
    size_t nodes = 1;
    /* Active declaration expressions cannot share nodes; the checker caches completed
     * declarations and reject active dependency cycles before pushing them. */
    for (i = 0; i < S.nmod; i++) {
        if ((size_t)S.mods[i].nnode > SIZE_MAX / sizeof *expression_frames - nodes) {
            diag_fatal("the expression continuation table exceeds host SIZE_MAX (C9001)");
        }
        nodes += (size_t)S.mods[i].nnode;
    }
    expression_frames = seed_alloc(nodes * sizeof *expression_frames, "expression continuation");
    /* Global symbols bound the constant frames; body symbols are not added by this driver. */
    signature_frames = seed_alloc(((size_t)S.nfunc + (size_t)S.nsym + 1u) * sizeof *signature_frames,
                                  "declaration dependency");
    for (i = 0; i < TY_USER; i++) {
        seed_type t;
        memset(&t, 0, sizeof t);
        t.kind = K_SCALAR;
        t.elem = i;
        GROW(S.types, S.ntype, S.cap_type, "type");
        S.types[S.ntype++] = t;
    }
    for (i = 0; i < S.nstruct; i++) {
        seed_type t;
        memset(&t, 0, sizeof t);
        t.kind = K_STRUCT;
        t.strct = i;
        t.sp = -1;
        S.structs[i].type = type_add(t);
    }
    for (o = 0; o < S.nmod; o++) {
        cm = S.check_order[o];
        M = &S.mods[cm];
        nlocal = 0;
        GROW(M->sites, 0, M->cap_site, "site");
        memset(&M->sites[0], 0, sizeof M->sites[0]);
        M->sites[0].op = "";
        M->nsite = 1;
        module_structs();
        for (i = 0; i < S.nfunc; i++) {
            if (S.funcs[i].module == cm) {
                ensure_declaration(DEP_SIGNATURE, i);
            }
        }
        module_consts();
        for (i = 0; i < S.nfunc; i++) {
            if (S.funcs[i].module == cm) {
                check_function(i);
            }
        }
    }
    free(expression_frames);
    expression_frames = NULL;
    free(signature_frames);
    signature_frames = NULL;
}
