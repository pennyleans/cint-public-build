/* emit_c.c: the direct C emitter of cint-seed (SPEC-09 section 7, SEED-09, SEED-10).
 *
 * One C statement per operation, operands evaluated left to right into
 * temporaries numbered from 0 per function (EMIT-16, EMIT-17); every checked
 * operation calls its cint_rt.h helper and leaves through the function's single
 * fault exit (EMIT-18). Control flow is emitted as labels and gotos, so an
 * `else if` chain of any length nests no C block. Sites are numbered from 1 per
 * module (EMIT-23); symbols use the injective encoding of EMIT-22. The generated source
 * is ASCII with LF line ends, four-space indentation and no trailing space,
 * and depends only on the program text and its module-relative paths (EMIT-21).
 * It exports the two interfaces of slice 2 patch D-2: the observer descriptor
 * of CONF-13 and the cint-abi-1 wrappers and module descriptor of SPEC-03
 * A-12 to A-14 (seed/OPEN.md SEED-OQ-22).
 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "parse.h"
#include "tables.h"

#define N(i) (M->nodes[(i)])
#define EMIT_LIMIT ((size_t)268435456u)   /* SPEC-09 EMIT-27 */

static int32_t cm;              /* the module being emitted */

typedef struct sbuf {
    char *p;
    size_t n, cap;
} sbuf;

static void sb_put(sbuf *b, const char *s, size_t n)
{
    if (b->n + n + 1u > b->cap) {
        size_t cap = b->cap > 0u ? b->cap : 4096u;
        char *q;
        while (cap < b->n + n + 1u) {
            cap *= 2u;
        }
        if (cap > EMIT_LIMIT * 2u || (q = realloc(b->p, cap)) == NULL) {
            diag_at(cm, 1, 1, C_LIMIT, "the generated C exceeds 268,435,456 bytes (SPEC-09 EMIT-27)");
        }
        b->p = q;
        b->cap = cap;
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = 0;
}

static void sb_vprintf(sbuf *b, const char *fmt, va_list ap)
{
    char small[512];
    va_list aq;
    int n;
    va_copy(aq, ap);
    n = vsnprintf(small, sizeof small, fmt, ap);
    if (n < 0) {
        diag_fatal("internal error: formatting failed");
    }
    if ((size_t)n < sizeof small) {
        sb_put(b, small, (size_t)n);
    } else {
        char *big = seed_alloc((size_t)n + 1u, "output");
        vsnprintf(big, (size_t)n + 1u, fmt, aq);
        sb_put(b, big, (size_t)n);
        free(big);
    }
    va_end(aq);
}

static void sb_printf(sbuf *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    sb_vprintf(b, fmt, ap);
    va_end(ap);
}

/* -- per-program and per-function state ------------------------------------------------------ */

static sbuf out_types, out_data, out_protos, out_funcs, out_entries;
static bool used_s64;
static seed_module *M;
static bool used_cmp[12];       /* cg_<op>_<s|u> helpers */
static bool used_index;          /* cg_index_ok */
static int32_t nstr;            /* string tables of the module */

static sbuf fbody, arena;       /* function body; operand texts */
static int32_t *temps;          /* temporaries: their types */
static int32_t ntemp, cap_temp;
static int32_t nlabel;
static int indent;
static uint32_t cur_tok;        /* the statement being emitted, for C9001 */
static int nfor;                /* open `for (;;)` statements */
static bool fault_used, dead;

/* Nesting of the generated C: its brace depth (the function body is 1) plus
 * the open `for (;;)` statements, which MSVC counts as levels of their own.
 * MSVC stops near 250 such levels (C1061) and Clang at 256 nested brackets,
 * so the seed refuses deeper nesting at the construct that opens the level
 * instead of emitting C that one of them cannot build (SEED-15). */
#define EMIT_MAX_DEPTH 200

static void deeper(void)
{
    if (++indent + nfor > EMIT_MAX_DEPTH) {
        diag_tok(cm, cur_tok, C_NEST, "nesting deeper than %d levels in the generated C (a block takes one "
                 "level, a loop three, a C-form or range `for` four; SPEC-09 SEED-15)", EMIT_MAX_DEPTH);
    }
}

static void line(const char *fmt, ...)
{
    va_list ap;
    int i;
    for (i = 0; i < indent; i++) {
        sb_put(&fbody, "    ", 4u);
    }
    va_start(ap, fmt);
    sb_vprintf(&fbody, fmt, ap);
    va_end(ap);
    sb_put(&fbody, "\n", 1u);
}

static void set_text(int32_t j, const char *fmt, ...)
{
    va_list ap;
    size_t at;
    sbuf tmp = {NULL, 0, 0};
    va_start(ap, fmt);
    sb_vprintf(&tmp, fmt, ap);
    va_end(ap);
    at = arena.n;
    sb_put(&arena, tmp.p, tmp.n + 1u);
    free(tmp.p);
    N(j).tmp = (int32_t)at;
}

#define TXT(j) (arena.p + N(j).tmp)

/* Copies operand text j (the arena may move while formatting). */
static const char *txt(int32_t j, int slot)
{
    static char *buf[4];
    size_t n = strlen(TXT(j)) + 1u;
    free(buf[slot]);
    buf[slot] = seed_alloc(n, "output");
    memcpy(buf[slot], TXT(j), n);
    return buf[slot];
}

/* -- names ------------------------------------------------------------------------------------ */

/* The EMIT-22 component `_<L>_<E(s)>` of the n bytes at s. */
static void component(sbuf *b, const uint8_t *s, size_t n)
{
    sbuf e = {NULL, 0, 0};
    size_t i;
    for (i = 0; i < n; i++) {
        uint8_t c = s[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            sb_put(&e, (const char *)&s[i], 1u);
        } else {
            sb_printf(&e, "_x%02X", (unsigned)c);
        }
    }
    sb_printf(b, "_%u_%s", (unsigned)e.n, e.p != NULL ? e.p : "");
    free(e.p);
}

static const char *ci_name(int32_t mi, uint32_t tok, const char *prefix)
{
    static char buf[2][320];
    static int k;
    const seed_module *m = &S.mods[mi];
    sbuf b = {NULL, 0, 0};
    sb_printf(&b, "%s%s", prefix, m->symc);
    component(&b, m->src + m->toks[tok].off, m->toks[tok].len);
    if (b.n > 247u) {
        diag_tok(mi, tok, "C9002", "the C symbol %s has %u bytes; the limit is 247 (SPEC-09 EMIT-22)", b.p, (unsigned)b.n);
    }
    if (prefix[1] == 'x' && strstr(b.p, "__") != NULL) {
        diag_tok(mi, tok, "C5051", "the public symbol %s contains `__`, which C++ reserves (SPEC-09 EMIT-22)", b.p);
    }
    k ^= 1;
    memcpy(buf[k], b.p, b.n + 1u);
    free(b.p);
    return buf[k];
}

static void sym_limit(int32_t mi, int64_t tok, const char *sym);

static const char *tok_text(const seed_module *m, uint32_t tok)
{
    static char buf[2][260];
    static int k;
    k ^= 1;
    memcpy(buf[k], m->src + m->toks[tok].off, m->toks[tok].len);
    buf[k][m->toks[tok].len] = 0;
    return buf[k];
}

static const char *ctype(int32_t t)
{
    if (t < TY_USER) {
        return ty_ctype(t);
    }
    if (S.types[t].kind == K_STRUCT) {
        const seed_struct *st = &S.structs[S.types[t].strct];
        return ci_name(st->module, st->tok, "ci");
    }
    return ctype(S.types[t].elem);
}

static const char *const_text(int32_t t, uint64_t v)
{
    static char buf[2][64];
    static int k;
    k ^= 1;
    switch (t) {
    case TY_BOOL: return v ? "true" : "false";
    case TY_I64:
        if (v == (uint64_t)1 << 63) {
            return "INT64_MIN";
        }
        snprintf(buf[k], sizeof buf[k], "INT64_C(%" PRId64 ")", (int64_t)v);
        break;
    case TY_I32:
        if ((int64_t)v == INT32_MIN) {
            return "INT32_MIN";
        }
        snprintf(buf[k], sizeof buf[k], "%" PRId64, (int64_t)v);
        break;
    case TY_I8: case TY_I16:
        snprintf(buf[k], sizeof buf[k], "((%s)%" PRId64 ")", ty_ctype(t), (int64_t)v);
        break;
    case TY_U64: snprintf(buf[k], sizeof buf[k], "UINT64_C(%" PRIu64 ")", v); break;
    case TY_U32: snprintf(buf[k], sizeof buf[k], "%" PRIu64 "u", v); break;
    default: snprintf(buf[k], sizeof buf[k], "((%s)%" PRIu64 "u)", ty_ctype(t), v); break;
    }
    return buf[k];
}

static const char *sym_c(int32_t y)
{
    static char buf[2][270];
    static int k;
    const seed_sym *s = &S.syms[y];
    k ^= 1;
    snprintf(buf[k], sizeof buf[k], s->kind == Y_PARAM && s->type >= TY_USER ? "(*v_%s)" : "v_%s",
             tok_text(&S.mods[s->module], s->tok));
    return buf[k];
}

static int32_t new_temp(int32_t t)
{
    GROW(temps, ntemp, cap_temp, "temporary");
    temps[ntemp] = t;
    return ntemp++;
}

static const char *site(int32_t s)
{
    static char buf[2][48];
    static int k;
    k ^= 1;
    snprintf(buf[k], sizeof buf[k], "CG_S(%uu, %uu)", (unsigned)cm, (unsigned)s);
    return buf[k];
}

static void check_call(const char *fmt, ...)
{
    sbuf b = {NULL, 0, 0};
    va_list ap;
    va_start(ap, fmt);
    sb_vprintf(&b, fmt, ap);
    va_end(ap);
    line("if (!%s) {", b.p);
    line("    goto fault;");
    line("}");
    fault_used = true;
    free(b.p);
}

static const char *cmp_helper(int op, int32_t t)
{
    static const char *const names[] = {"eq", "ne", "lt", "le", "gt", "ge"};
    int k = (op - OP_EQ) * 2 + (ty_signed(t) ? 0 : 1);
    used_cmp[k] = true;
    return fmtbuf("cg_%s_%s", names[op - OP_EQ], ty_signed(t) ? "s" : "u");
}

/* -- expressions -------------------------------------------------------------------------------- */

/* The length of the array or view that node j denotes, as C text. */
static const char *extent_text(int32_t j)
{
    const seed_type *t = &S.types[N(j).type];
    if (t->kind == K_ARRAY) {
        return const_text(TY_I64, (uint64_t)t->extent);
    }
    if (t->lit) {
        return const_text(TY_I64, (uint64_t)N(j).aux);
    }
    return fmtbuf("n_%s", tok_text(M, S.syms[N(j).sym].tok));
}

/* Whether node j is used as a place or a reference rather than read as a value. */
static bool is_ref(int32_t j, int32_t root, bool place)
{
    int32_t p = N(j).parent;
    if (j == root) {
        return place || (N(j).type >= TY_USER && S.types[N(j).type].kind != K_STRUCT);
    }
    if ((N(p).kind == N_FIELD || N(p).kind == N_INDEX) && N(p).a == j) {
        return true;
    }
    /* Binds record arguments after evaluating all arguments of the call. */
    if (N(p).kind == N_CALL && N(j).type >= TY_USER && S.types[N(j).type].kind == K_STRUCT) {
        return true;
    }
    return N(j).type >= TY_USER && S.types[N(j).type].kind != K_STRUCT;
}

static void load(int32_t j, int32_t root, bool place)
{
    if (!is_ref(j, root, place)) {
        int32_t t = new_temp(N(j).type);
        line("t%d = %s;", (int)t, txt(j, 0));
        set_text(j, "t%d", (int)t);
    }
}

static void emit_call(int32_t j)
{
    const seed_node *n = &N(j);
    sbuf a = {NULL, 0, 0};
    int32_t i, k, r = -1;
    if (n->op == 1) {
        const seed_struct *st = &S.structs[n->aux];
        r = new_temp(st->type);
        for (i = 0; i < n->b; i++) {
            const seed_field *f = &S.fields[st->field0 + (int32_t)M->argn[n->a + i]];
            line("t%d.f_%s = %s;", (int)r, tok_text(&S.mods[st->module], f->tok), txt(M->args[n->a + i], 0));
        }
        set_text(j, "t%d", (int)r);
        return;
    }
    {
        const seed_func *f = &S.funcs[n->aux];
        sb_printf(&a, "%s(ctx", ci_name(f->module, f->tok, "ci"));
        for (k = 0; k < f->nsize; k++) {
            for (i = 0; i < f->nparam; i++) {
                const seed_param *p = &S.params[f->param0 + i];
                if (p->type >= TY_USER && S.types[p->type].kind == K_VIEW && S.types[p->type].sp == k) {
                    sb_printf(&a, ", %s", extent_text(M->args[n->a + i]));
                    break;
                }
            }
        }
        for (i = 0; i < n->b; i++) {
            const seed_param *p = &S.params[f->param0 + i];
            int32_t x = M->args[n->a + i];
            if (p->type >= TY_USER && S.types[p->type].kind == K_VIEW) {
                sb_printf(&a, ", %s, %s", txt(x, 0), extent_text(x));
            } else if (p->type >= TY_USER) {
                int32_t copy = new_temp(p->type);
                line("t%d = %s;", (int)copy, txt(x, 0));
                sb_printf(&a, ", &t%d", (int)copy);
            } else {
                sb_printf(&a, ", %s", txt(x, 0));
            }
        }
        if (f->result != TY_VOID) {
            r = new_temp(f->result);
            sb_printf(&a, ", &t%d", (int)r);
        }
        check_call("cint_rt_call_enter(ctx, %s)", site(n->site));
        check_call("%s)", a.p);
        line("cint_rt_call_leave(ctx);");
        set_text(j, r >= 0 ? "t%d" : "", (int)r);
    }
    free(a.p);
}

static void emit_node(int32_t j, int32_t root, bool place)
{
    seed_node *n = &N(j);
    int32_t t = n->type, r;
    const char *ty = n->type < TY_USER ? ty_ident(n->type) : "";
    switch (n->kind) {
    case N_STR: {
        int32_t k, len = n->aux;
        sbuf d = {NULL, 0, 0};
        if (len == 0) {
            set_text(j, "NULL");
            break;
        }
        sb_printf(&d, "static const uint8_t cg%s_s%d[%d] = {", M->symc, (int)nstr, (int)len);
        for (k = 0; k < len; k++) {
            sb_printf(&d, "%s%uu", k == 0 ? "" : k % 16 == 0 ? ",\n    " : ", ", (unsigned)M->strs[n->val + (uint64_t)k]);
        }
        sb_printf(&out_data, "%s};\n", d.p);
        free(d.p);
        set_text(j, "cg%s_s%d", M->symc, (int)nstr++);
        sym_limit(cm, n->tok, TXT(j));
        break;
    }
    case N_NAME:
        set_text(j, "%s", sym_c(n->sym));
        load(j, root, place);
        break;
    case N_FIELD:
        set_text(j, "%s.f_%s", txt(n->a, 1), tok_text(&S.mods[S.structs[S.types[N(n->a).type].strct].module],
                                                     S.fields[n->aux].tok));
        load(j, root, place);
        break;
    case N_INDEX:
        /* The range test is visible to the C compiler, so the access below is provably in
         * bounds; the helper then writes the canonical record (RT-OQ-05 shape). */
        used_index = true;
        line("if (!cg_index_ok(%s, %s)) {", txt(n->b, 1), extent_text(n->a));
        line("    (void)cint_index_check(ctx, %s, \"index.checked.%s\", %s, %s);", site(n->site), ty_ident(n->type),
             txt(n->b, 1), extent_text(n->a));
        line("    goto fault;");
        line("}");
        fault_used = true;
        set_text(j, "%s[%s]", txt(n->a, 0), txt(n->b, 1));
        load(j, root, place);
        break;
    case N_UNARY:
        r = new_temp(t);
        if (n->op == OP_NOT) {
            line("t%d = !%s;", (int)r, txt(n->a, 0));
        } else if (n->op == OP_TILDE) {
            line("t%d = (%s)~%s;", (int)r, ty_ctype(t), txt(n->a, 0));
        } else {
            check_call("cint_neg_%s(ctx, %s, %s, &t%d)", ty, site(n->site), txt(n->a, 0), (int)r);
        }
        set_text(j, "t%d", (int)r);
        break;
    case N_BIN: {
        const char *nm = n->op == OP_ADD || n->op == OP_ADDW ? "add" : n->op == OP_SUB || n->op == OP_SUBW ? "sub"
                       : n->op == OP_MUL || n->op == OP_MULW ? "mul" : n->op == OP_DIV ? "div" : "rem";
        r = new_temp(t);
        if (op_is_bitwise(n->op)) {
            line("t%d = (%s)(%s %s %s);", (int)r, ty_ctype(t), txt(n->a, 0), n->op == OP_BAND ? "&" : n->op == OP_BOR ? "|" : "^",
                 txt(n->b, 1));
        } else if (op_is_shift(n->op)) {
            check_call("cint_%s_%s(ctx, %s, %s, cint_count_%s(%s), &t%d)", n->op == OP_SHL ? "shl" : n->op == OP_SHR
                       ? "shr" : "shl_wrap", ty, site(n->site), txt(n->a, 0), ty_ident(N(n->b).type), txt(n->b, 1), (int)r);
        } else if (n->op == OP_ADDW || n->op == OP_SUBW || n->op == OP_MULW) {
            line("t%d = cint_%s_wrap_%s(%s, %s);", (int)r, nm, ty, txt(n->a, 0), txt(n->b, 1));
        } else {
            check_call("cint_%s_%s(ctx, %s, %s, %s, &t%d)", nm, ty, site(n->site), txt(n->a, 0), txt(n->b, 1), (int)r);
        }
        set_text(j, "t%d", (int)r);
        break;
    }
    case N_CMP:
        r = new_temp(TY_BOOL);
        if (N(n->a).type == TY_BOOL) {
            line("t%d = %s %s %s;", (int)r, txt(n->a, 0), n->op == OP_EQ ? "==" : "!=", txt(n->b, 1));
        } else {
            line("t%d = %s(%s, %s);", (int)r, cmp_helper(n->op, N(n->a).type), txt(n->a, 0), txt(n->b, 1));
        }
        set_text(j, "t%d", (int)r);
        break;
    case N_SC:
        r = new_temp(TY_BOOL);
        n->val = (uint64_t)nlabel++;
        line("t%d = %s;", (int)r, txt(n->a, 0));
        line("if (%st%d) {", n->op == OP_LAND ? "!" : "", (int)r);
        line("    goto L%d;", (int)n->val);
        line("}");
        set_text(j, "t%d", (int)r);
        break;
    case N_LOGIC:
        line("%s = %s;", txt(n->a, 0), txt(n->b, 1));
        line("L%d:;", (int)N(n->a).val);
        set_text(j, "%s", txt(n->a, 0));
        break;
    case N_CONV: {
        int32_t from = N(n->a).type;
        r = new_temp(t);
        if (from == TY_BOOL) {
            line("t%d = (%s)(%s ? 1 : 0);", (int)r, ty_ctype(t), txt(n->a, 0));
        } else if (n->op == OP_ASW) {
            line("t%d = cint_as_wrap_%s_from_%s(%s);", (int)r, ty, ty_ident(from), txt(n->a, 0));
        } else {
            check_call("cint_as_%s_from_%s(ctx, %s, %s, &t%d)", ty, ty_ident(from), site(n->site), txt(n->a, 0), (int)r);
        }
        set_text(j, "t%d", (int)r);
        break;
    }
    case N_CALL:
        emit_call(j);
        break;
    default:
        break;
    }
}

/* Evaluates the expression at root in evaluation order; its text is TXT(root).
 * With `place`, the root is left as an lvalue. */
static void emit_expr(int32_t root, bool place)
{
    int32_t j;
    for (j = N(root).first; j <= root; j++) {
        int32_t m = N(j).mc;
        if (m >= 0) {
            if (N(m).kind == N_SC) {   /* a constant left operand of a run-time && or || */
                set_text(N(m).a, "%s", const_text(TY_BOOL, N(N(m).a).val));
                emit_node(m, root, place);
            } else {
                set_text(m, "%s", const_text(N(m).type, N(m).val));
            }
            j = m;
            continue;
        }
        emit_node(j, root, place);
    }
}

/* -- statements --------------------------------------------------------------------------------- */

enum { EF_BLOCK, EF_IF, EF_LOOP, EF_SWITCH, EF_SCOPE };
typedef struct efr {
    uint8_t kind, end_used, all_dead, has_else, done, infinite, brk_used, cont_used;
    int32_t lend, lnext, lbrk, lcont;
    int32_t update;          /* C-form loop: its update statement */
    int32_t forr;            /* range loop statement */
    int32_t tlast;           /* range loop: last value temporary */
    int32_t clause;          /* switch: next clause label */
} efr;

static efr *frs;
static int32_t nfr;

static int32_t skip_block(int32_t i)
{
    int depth = 0;
    for (;; i++) {
        int k = M->stmts[i].kind;
        if (k == S_BLOCK) {
            depth++;
        } else if (k == S_END && --depth <= 0) {
            return i;
        }
    }
}

static int32_t block_end_from(int32_t i)
{
    int depth = 0;
    for (;; i++) {
        int k = M->stmts[i].kind;
        if (k == S_BLOCK) {
            depth++;
        } else if (k == S_END) {
            if (depth == 0) {
                return i;
            }
            depth--;
        }
    }
}

static void emit_simple(int32_t i)
{
    const seed_stmt *s = &M->stmts[i];
    int32_t t, a, r;
    switch (s->kind) {
    case S_VAR: {
        const seed_sym *y = &S.syms[s->sym];
        const char *name = sym_c(s->sym);
        char nm[270];
        snprintf(nm, sizeof nm, "%s", name);
        t = y->type;
        if (t >= TY_USER && S.types[t].kind == K_ARRAY) {
            line("%s %s[%" PRId64 "];", ctype(t), nm, S.types[t].extent > 0 ? S.types[t].extent : 1);
            line("memset(%s, 0, sizeof %s);", nm, nm);
        } else if (s->e1 < 0) {
            line("%s %s;", ctype(t), nm);
            line("memset(&%s, 0, sizeof %s);", nm, nm);
        } else {
            emit_expr(s->e1, false);
            line("%s %s = %s;", ctype(t), nm, txt(s->e1, 0));
        }
        line("(void)%s;", nm);
        break;
    }
    case S_ASSIGN:
    case S_INCDEC: {
        int bop = s->kind == S_INCDEC ? (s->op == OP_INC ? OP_ADD : OP_SUB) : op_of_assign(s->op);
        emit_expr(s->e1, true);
        t = N(s->e1).type;
        if (bop == 0) {
            emit_expr(s->e2, false);
            line("%s = %s;", txt(s->e1, 0), txt(s->e2, 1));
            break;
        }
        a = new_temp(t);
        r = new_temp(t);
        line("t%d = %s;", (int)a, txt(s->e1, 0));
        if (s->e2 >= 0) {
            emit_expr(s->e2, false);
        }
        {
            const char *rhs = s->e2 >= 0 ? txt(s->e2, 1) : const_text(t, 1u);
            const char *ty = ty_ident(t);
            if (op_is_bitwise(bop)) {
                line("t%d = (%s)(t%d %s %s);", (int)r, ty_ctype(t), (int)a, bop == OP_BAND ? "&" : bop == OP_BOR ? "|" : "^",
                     rhs);
            } else if (bop == OP_ADDW || bop == OP_SUBW || bop == OP_MULW) {
                line("t%d = cint_%s_wrap_%s(t%d, %s);", (int)r, bop == OP_ADDW ? "add" : bop == OP_SUBW ? "sub" : "mul",
                     ty, (int)a, rhs);
            } else if (op_is_shift(bop)) {
                check_call("cint_%s_%s(ctx, %s, t%d, cint_count_%s(%s), &t%d)", bop == OP_SHL ? "shl" : bop == OP_SHR ?
                           "shr" : "shl_wrap", ty, site(s->site), (int)a, ty_ident(N(s->e2).type), rhs, (int)r);
            } else {
                check_call("cint_%s_%s(ctx, %s, t%d, %s, &t%d)", bop == OP_ADD ? "add" : bop == OP_SUB ? "sub" :
                           bop == OP_MUL ? "mul" : bop == OP_DIV ? "div" : "rem", ty, site(s->site), (int)a, rhs, (int)r);
            }
        }
        line("%s = t%d;", txt(s->e1, 0), (int)r);
        break;
    }
    case S_CALL:
        emit_expr(s->e1, false);
        break;
    case S_DISCARD:
        emit_expr(s->e1, false);
        line("(void)%s;", txt(s->e1, 0));
        break;
    default:
        break;
    }
}

static void emit_case_test(int32_t i, int32_t scrut, int32_t t, int32_t label)
{
    const seed_stmt *s = &M->stmts[i];
    sbuf c = {NULL, 0, 0};
    int32_t k;
    for (k = 0; k < s->count; k++) {
        const seed_case_item *it = &M->items[s->link + k];
        if (k > 0) {
            sb_put(&c, " || ", 4u);
        }
        if (it->hi < 0) {
            sb_printf(&c, "%s(t%d, %s)", cmp_helper(OP_EQ, t), (int)scrut, const_text(t, N(it->lo).val));
        } else {
            sb_printf(&c, "(%s(t%d, %s)", cmp_helper(OP_GE, t), (int)scrut, const_text(t, N(it->lo).val));
            sb_printf(&c, " && %s(t%d, %s))", cmp_helper(OP_LE, t), (int)scrut, const_text(t, N(it->hi).val));
        }
    }
    line("if (%s) {", c.p);
    line("    goto L%d;", (int)label);
    line("}");
    free(c.p);
}

static efr *push_fr(int kind)
{
    efr *f = &frs[nfr++];
    memset(f, 0, sizeof *f);
    f->kind = (uint8_t)kind;
    f->lend = f->lnext = f->lbrk = f->lcont = f->update = f->forr = f->tlast = f->clause = -1;
    f->all_dead = 1;
    return f;
}

/* A condition: returns 1 constant true, 0 constant false, -1 run time with its text emitted. */
static int cond(int32_t e)
{
    emit_expr(e, false);
    if (N(e).mc == e) {
        return N(e).val != 0u;
    }
    return -1;
}

/* The end of a loop body: the continue label, then the loop's own step. */
static void loop_body_end(efr *f)
{
    if (f->cont_used) {
        line("L%d:;", (int)f->lcont);
        dead = false;
    }
    if (dead) {
        return;
    }
    if (f->update >= 0) {
        emit_simple(f->update);
    } else if (f->forr >= 0) {
        const char *v = sym_c(M->stmts[f->forr].sym);
        line("if (%s(%s, t%d)) {", cmp_helper(OP_EQ, TY_I64), v, (int)f->tlast);
        line("    goto L%d;", (int)f->lbrk);
        line("}");
        line("%s = cint_add_wrap_i64(%s, INT64_C(1));", v, v);
        f->brk_used = 1;
    }
}

static void emit_function(int32_t fi)
{
    const seed_func *f = &S.funcs[fi];
    int32_t i, k;
    sbuf sig = {NULL, 0, 0};
    fbody.n = 0;
    arena.n = 0;
    ntemp = 0;
    nlabel = 0;
    indent = 1;
    nfor = 0;
    fault_used = false;
    dead = false;
    nfr = 0;
    frs = seed_alloc((size_t)(f->body_end - f->body + 2) * sizeof *frs, "statement");
    sb_printf(&sig, "static bool %s(cint_ctx *ctx", ci_name(cm, f->tok, "ci"));
    for (k = 0; k < f->nsize; k++) {
        sb_printf(&sig, ", int64_t v_%s", tok_text(M, S.sizes[f->size0 + k]));
    }
    for (k = 0; k < f->nparam; k++) {
        const seed_param *p = &S.params[f->param0 + k];
        const char *nm = tok_text(M, p->tok);
        if (p->type >= TY_USER && S.types[p->type].kind == K_VIEW) {
            sb_printf(&sig, ", %s%s *v_%s, int64_t n_%s", S.types[p->type].perm ? "" : "const ", ctype(p->type), nm, nm);
        } else if (p->type >= TY_USER) {
            sb_printf(&sig, ", const %s *v_%s", ctype(p->type), nm);
        } else {
            sb_printf(&sig, ", %s v_%s", ty_ctype(p->type), nm);
        }
    }
    if (f->result != TY_VOID) {
        sb_printf(&sig, ", %s *result", ty_ctype(f->result));
    }
    sb_put(&sig, ")", 1u);
    sb_printf(&out_protos, "%s;\n", sig.p);
    for (i = f->body; i <= f->body_end; i++) {
        const seed_stmt *s = &M->stmts[i];
        efr *top = nfr > 0 ? &frs[nfr - 1] : NULL;
        int c;
        cur_tok = s->tok;
        if (dead && (s->kind == S_BLOCK || (s->kind >= S_VAR && s->kind <= S_CONTINUE) || s->kind == S_IF ||
                     s->kind == S_WHILE || s->kind == S_FORC || s->kind == S_FORR || s->kind == S_SWITCH) &&
            !(s->kind == S_BLOCK && top != NULL && top->kind != EF_BLOCK)) {
            i = block_end_from(i) - 1;
            continue;
        }
        if (top != NULL && top->update == i) {
            continue;   /* emitted after the body */
        }
        switch (s->kind) {
        case S_BLOCK:
            if (nfr == 0) {
                push_fr(EF_BLOCK);
                break;
            }
            line("{");
            deeper();
            push_fr(EF_BLOCK);
            break;
        case S_END:
            nfr--;
            if (nfr == 0) {
                break;
            }
            indent--;
            line("}");
            top = &frs[nfr - 1];
            if (top->kind == EF_IF) {
                if (!dead) {
                    if (M->stmts[i + 1].kind != S_ENDIF) {
                        line("goto L%d;", (int)top->lend);
                        top->end_used = 1;
                    }
                    top->all_dead = 0;
                }
                if (top->lnext >= 0) {
                    line("L%d:;", (int)top->lnext);
                    top->lnext = -1;
                }
            } else if (top->kind == EF_LOOP) {
                loop_body_end(top);
            } else if (top->kind == EF_SWITCH) {
                if (!dead) {
                    line("goto L%d;", (int)top->lend);
                    top->end_used = 1;
                    top->all_dead = 0;
                }
            }
            break;
        case S_VAR:
        case S_ASSIGN:
        case S_INCDEC:
        case S_CALL:
        case S_DISCARD:
            emit_simple(i);
            break;
        case S_CONST:
            break;
        case S_RETURN:
            if (s->e1 >= 0) {
                emit_expr(s->e1, false);
                line("*result = %s;", txt(s->e1, 0));
            }
            line("return true;");
            dead = true;
            break;
        case S_BREAK:
        case S_CONTINUE:
            for (k = nfr - 1; k >= 0; k--) {
                if (frs[k].kind == EF_LOOP || (frs[k].kind == EF_SWITCH && s->kind == S_BREAK)) {
                    break;
                }
            }
            if (s->kind == S_CONTINUE) {
                line("goto L%d;", (int)frs[k].lcont);
                frs[k].cont_used = 1;
            } else if (frs[k].kind == EF_SWITCH) {
                line("goto L%d;", (int)frs[k].lend);
                frs[k].end_used = 1;
            } else {
                line("goto L%d;", (int)frs[k].lbrk);
                frs[k].brk_used = 1;
            }
            dead = true;
            break;
        case S_IF:
        case S_ELSEIF:
        case S_ELSE:
            if (s->kind == S_IF) {
                top = push_fr(EF_IF);
                top->lend = nlabel++;
            }
            if (top->done) {
                i = skip_block(i + 1);   /* an arm after a constant-true one */
                break;
            }
            dead = false;
            if (s->kind == S_ELSE) {
                top->has_else = 1;
                break;
            }
            c = cond(s->e1);
            if (c == 0) {
                i = skip_block(i + 1);
                break;
            }
            if (c == 1) {
                top->done = 1;
                top->has_else = 1;
                break;
            }
            top->lnext = nlabel++;
            line("if (!%s) {", txt(s->e1, 0));
            line("    goto L%d;", (int)top->lnext);
            line("}");
            break;
        case S_ENDIF:
            nfr--;
            dead = frs[nfr].has_else && frs[nfr].all_dead;
            if (frs[nfr].end_used) {
                line("L%d:;", (int)frs[nfr].lend);
            }
            break;
        case S_WHILE:
        case S_FORR:
        case S_FORC:
            top = push_fr(EF_LOOP);
            top->lbrk = nlabel++;
            top->lcont = nlabel++;
            if (s->kind == S_FORC) {
                line("{");
                deeper();
                top->update = s->link;
                break;
            }
            if (s->kind == S_FORR) {
                int32_t lo, hi;
                line("{");
                deeper();
                top->forr = i;
                emit_expr(s->e1, false);
                lo = new_temp(TY_I64);
                line("t%d = %s;", (int)lo, txt(s->e1, 0));
                emit_expr(s->e2, false);
                hi = new_temp(TY_I64);
                line("t%d = %s;", (int)hi, txt(s->e2, 0));
                line("int64_t %s = t%d;", sym_c(s->sym), (int)lo);
                line("(void)%s;", sym_c(s->sym));
                line("if (!%s(t%d, t%d)) {", cmp_helper(s->op ? OP_LE : OP_LT, TY_I64), (int)lo, (int)hi);
                line("    goto L%d;", (int)top->lbrk);
                line("}");
                top->brk_used = 1;
                top->tlast = new_temp(TY_I64);
                if (s->op) {
                    line("t%d = t%d;", (int)top->tlast, (int)hi);
                } else {
                    line("t%d = cint_sub_wrap_i64(t%d, INT64_C(1));", (int)top->tlast, (int)hi);
                }
                line("for (;;) {");
                nfor++;
                deeper();
                check_call("cint_fuel_charge(ctx, %s, 1u)", site(s->site));
                break;
            }
            if (N(s->e1).mc == s->e1 && N(s->e1).val == 0u) {
                nfr--;
                i = skip_block(i + 1) + 1;   /* `while (false)`: the body and S_ENDLOOP are never reached */
                break;
            }
            line("for (;;) {");
            nfor++;
            deeper();
            c = cond(s->e1);
            if (c == -1) {
                line("if (!%s) {", txt(s->e1, 0));
                line("    goto L%d;", (int)top->lbrk);
                line("}");
                top->brk_used = 1;
            } else {
                top->infinite = 1;
            }
            check_call("cint_fuel_charge(ctx, %s, 1u)", site(s->site));
            break;
        case S_FORCOND:
            if (N(s->e1).mc == s->e1 && N(s->e1).val == 0u) {
                indent--;
                line("}");
                nfr--;
                i = skip_block(i + 2) + 1;   /* the update, the body and S_ENDLOOP are never reached */
                break;
            }
            line("for (;;) {");
            nfor++;
            deeper();
            c = cond(s->e1);
            if (c == -1) {
                line("if (!%s) {", txt(s->e1, 0));
                line("    goto L%d;", (int)top->lbrk);
                line("}");
                top->brk_used = 1;
            } else {
                top->infinite = 1;
            }
            check_call("cint_fuel_charge(ctx, %s, 1u)", site(s->site));
            break;
        case S_ENDLOOP:
            nfr--;
            nfor--;
            indent--;
            line("}");
            if (frs[nfr].update >= 0 || frs[nfr].forr >= 0) {
                if (frs[nfr].brk_used) {
                    line("L%d:;", (int)frs[nfr].lbrk);
                }
                indent--;
                line("}");
            } else if (frs[nfr].brk_used) {
                line("L%d:;", (int)frs[nfr].lbrk);
            }
            dead = frs[nfr].infinite && !frs[nfr].brk_used;
            break;
        case S_SWITCH: {
            int32_t t = N(s->e1).type, scrut, last = -1, def = -1, z, depth = 0, ncase = 0;
            top = push_fr(EF_SWITCH);
            top->lend = nlabel++;
            emit_expr(s->e1, false);
            scrut = new_temp(t);
            line("t%d = %s;", (int)scrut, txt(s->e1, 0));
            top->clause = nlabel;
            for (z = i + 1; ; z++) {   /* one label per clause, in order */
                int kz = M->stmts[z].kind;
                if (kz == S_BLOCK) {
                    depth++;
                } else if (kz == S_END) {
                    depth--;
                } else if (depth == 0 && kz == S_CASE) {
                    last = z;
                    ncase++;
                    nlabel++;
                } else if (depth == 0 && kz == S_DEFAULT) {
                    def = nlabel++;
                } else if (depth == 0 && kz == S_ENDSWITCH) {
                    break;
                }
            }
            if (ncase == 0 || (ncase == 1 && def < 0)) {
                /* No clause tests the scrutinee (a lone exhaustive case, or a default alone):
                 * read it once, so no variable is set but not used (EMIT-26). */
                line("(void)t%d;", (int)scrut);
            }
            for (z = i + 1, k = top->clause, depth = 0; ; z++) {
                int kz = M->stmts[z].kind;
                if (kz == S_BLOCK) {
                    depth++;
                } else if (kz == S_END) {
                    depth--;
                } else if (depth == 0 && kz == S_CASE) {
                    if (z == last && def < 0) {
                        line("goto L%d;", (int)k);   /* exhaustive: the last clause takes the rest */
                    } else {
                        emit_case_test(z, scrut, t, k);
                    }
                    k++;
                } else if (depth == 0 && kz == S_ENDSWITCH) {
                    break;
                }
            }
            if (def >= 0) {
                line("goto L%d;", (int)def);
            }
            dead = true;
            break;
        }
        case S_CASE:
        case S_DEFAULT:
            line("L%d:;", (int)top->clause++);
            dead = false;
            break;
        case S_ENDSWITCH:
            nfr--;
            dead = frs[nfr].all_dead && !frs[nfr].end_used;
            if (frs[nfr].end_used) {
                line("L%d:;", (int)frs[nfr].lend);
            }
            break;
        default:
            break;
        }
    }
    if (!dead) {
        line(f->result == TY_VOID ? "return true;" : "return false;");
    }
    if (fault_used) {
        line("fault:");
        line("return false;");
    }
    sb_printf(&out_funcs, "\n%s\n{\n", sig.p);
    for (k = 0; k < ntemp; k++) {
        if (temps[k] >= TY_USER) {
            sb_printf(&out_funcs, "    %s t%d;\n", ctype(temps[k]), (int)k);
        } else {
            sb_printf(&out_funcs, "    %s t%d = %s;\n", ty_ctype(temps[k]), (int)k, temps[k] == TY_BOOL ? "false" : "0");
        }
    }
    for (k = 0; k < ntemp; k++) {
        if (temps[k] >= TY_USER) {
            sb_printf(&out_funcs, "    memset(&t%d, 0, sizeof t%d);\n", (int)k, (int)k);
        }
    }
    sb_printf(&out_funcs, "    (void)ctx;\n");
    if (f->result != TY_VOID) {
        sb_printf(&out_funcs, "    (void)result;\n");
    }
    for (k = 0; k < f->nsize; k++) {
        sb_printf(&out_funcs, "    (void)v_%s;\n", tok_text(M, S.sizes[f->size0 + k]));
    }
    for (k = 0; k < f->nparam; k++) {
        const seed_param *p = &S.params[f->param0 + k];
        sb_printf(&out_funcs, "    (void)v_%s;\n", tok_text(M, p->tok));
        if (p->type >= TY_USER && S.types[p->type].kind == K_VIEW) {
            sb_printf(&out_funcs, "    (void)n_%s;\n", tok_text(M, p->tok));
        }
    }
    sb_put(&out_funcs, fbody.p != NULL ? fbody.p : "", fbody.n);
    sb_put(&out_funcs, "}\n", 2u);
    free(sig.p);
    free(frs);
}

/* -- entries ------------------------------------------------------------------------------------ */

/* The CINT_TAG_* of a scalar type (rt/cint_rt.h). */
static const char *tag_of(int32_t t)
{
    static const char *const tags[] = {"0u", "CINT_TAG_I8", "CINT_TAG_I16", "CINT_TAG_I32", "CINT_TAG_I64",
                                       "CINT_TAG_U8", "CINT_TAG_U16", "CINT_TAG_U32", "CINT_TAG_U64", "CINT_TAG_BOOL"};
    return t >= TY_I8 && t <= TY_BOOL ? tags[t] : "0u";
}

/* C9002: a generated identifier over 247 bytes (SPEC-09 EMIT-22), at token tok, or at 1:1. */
static void sym_limit(int32_t mi, int64_t tok, const char *sym)
{
    size_t n = strlen(sym);
    if (n > 247u && tok < 0) {
        diag_at(mi, 1u, 1u, "C9002", "the C symbol %s has %u bytes; the limit is 247 (SPEC-09 EMIT-22)", sym, (unsigned)n);
    } else if (n > 247u) {
        diag_tok(mi, (uint32_t)tok, "C9002", "the C symbol %s has %u bytes; the limit is 247 (SPEC-09 EMIT-22)", sym,
                 (unsigned)n);
    }
}

/* The end every entry shares: begin at the entry site, the checks in `pre`, the body call
 * `call` (without its result argument), the end, and *result from r (SPEC-09 EMIT-18). */
static void entry_tail(const seed_func *f, const char *depth, const char *pre, const char *call, const char *res)
{
    sb_printf(&out_entries, "    st = cint_rt_entry_begin(ctx, %s, fuel, %s);\n    if (st != CINT_OK) {\n"
                            "        return st;\n    }\n%s    ok = %s%s);\n    st = cint_rt_entry_end(ctx);\n",
              site(f->entry_site), depth, pre, call, f->result != TY_VOID ? ", &r" : "");
    if (f->result != TY_VOID) {
        sb_printf(&out_entries, "    if (ok && st == CINT_OK) {\n        *result = %s;\n    }\n", res);
    } else {
        sb_printf(&out_entries, "    (void)ok;\n");
    }
    sb_printf(&out_entries, "    return st;\n}\n");
}

/* The declarations every entry starts with, then the refusal of a Bool argument above 1
 * before entry (SPEC-03 H-12). */
static void entry_head(const seed_func *f, bool observer, const char *decl)
{
    int32_t k;
    sb_printf(&out_entries, "{\n    cint_status st;\n    bool ok;\n");
    if (f->result != TY_VOID) {
        sb_printf(&out_entries, "    %s r = %s;\n", ty_ctype(f->result), f->result == TY_BOOL ? "false" : "0");
    }
    sb_printf(&out_entries, "%s", decl);
    for (k = 0; k < f->nparam; k++) {
        if (S.params[f->param0 + k].type == TY_BOOL && observer) {
            sb_printf(&out_entries, "    if (args[%d] > 1u) {\n        return CINT_REFUSED;\n    }\n", (int)k);
        } else if (S.params[f->param0 + k].type == TY_BOOL) {
            sb_printf(&out_entries, "    if (p_%s > 1u) {\n        return CINT_REFUSED;\n    }\n",
                      tok_text(M, S.params[f->param0 + k].tok));
        }
    }
}

/* The observer entry `cg` + C(P) + `_o<n>` of an exported function with scalar parameters
 * only, its type signature `cg` + C(P) + `_t<n>`, and its row of the observer table (SPEC-09
 * CONF-13 X-3, X-4). Arguments arrive as 64-bit patterns; the depth limit is the caller's. */
static void emit_observer_entry(const seed_func *f, int32_t n, sbuf *rows)
{
    sbuf call = {NULL, 0, 0};
    int32_t k;
    sym_limit(cm, f->tok, fmtbuf("cg%s_o%d", M->symc, (int)n));
    sb_printf(&out_entries, "\nstatic const uint32_t cg%s_t%d[%d] = {%s, %du", M->symc, (int)n, 2 + (int)f->nparam,
              tag_of(f->result), (int)f->nparam);
    for (k = 0; k < f->nparam; k++) {
        sb_printf(&out_entries, ", %s", tag_of(S.params[f->param0 + k].type));
    }
    sb_printf(&out_entries, "};\n\nstatic cint_status cg%s_o%d(cint_ctx *ctx, int64_t fuel, int64_t depth, "
                            "const uint64_t *args, uint64_t *result)\n", M->symc, (int)n);
    entry_head(f, true, "");
    sb_printf(&out_entries, "%s%s", f->nparam == 0 ? "    (void)args;\n" : "", f->result == TY_VOID ? "    (void)result;\n" : "");
    sb_printf(&call, "%s(ctx", ci_name(cm, f->tok, "ci"));
    for (k = 0; k < f->nparam; k++) {
        int32_t t = S.params[f->param0 + k].type;
        used_s64 = used_s64 || ty_signed(t);
        if (t == TY_BOOL) {
            sb_printf(&call, ", args[%d] != 0u", (int)k);
        } else {
            sb_printf(&call, ty_signed(t) ? ", (%s)cg_s64(args[%d])" : ", (%s)args[%d]", ty_ctype(t), (int)k);
        }
    }
    entry_tail(f, "depth", "", call.p, f->result == TY_BOOL ? "r ? 1u : 0u" : ty_signed(f->result) ? "(uint64_t)(int64_t)r"
                                                                                                    : "(uint64_t)r");
    sb_printf(rows, "    {%uu, %uu, \"%s\", cg%s_t%d, cg%s_o%d},\n", (unsigned)cm, (unsigned)M->toks[f->tok].len,
              tok_text(M, f->tok), M->symc, (int)n, M->symc, (int)n);
    free(call.p);
}

/* The cint_type a view of element type t is bound against (SPEC-03 A-13; a struct is
 * CINT_TAG_RECORD with its index in the record layout table of the `cm` descriptor). */
static const char *view_type(int32_t t)
{
    if (t >= TY_USER) {
        return fmtbuf("{CINT_TAG_RECORD, 0u, 0u, 0u, 0u, %uu, 0u}", (unsigned)S.types[t].strct);
    }
    return fmtbuf("{%s, 0u, 0u, 0u, 0u, 0u, 0u}", tag_of(t));
}

/* Appends ", offsetof(T, <at>)" to b for every Bool inside a value of type t at member
 * designator `at` of T (the element type of a view; "" for the element itself). */
static void bool_offsets(sbuf *b, const char *T, int32_t t, const char *at, int32_t *count)
{
    char sub[1024];
    int32_t k, j;
    if (t == TY_BOOL) {
        sb_printf(b, *at != 0 ? ", (uint32_t)offsetof(%s, %s)" : ", 0u", T, at);
        ++*count;
    } else if (t >= TY_USER && S.types[t].kind == K_ARRAY) {
        for (j = 0; j < S.types[t].extent; j++) {
            snprintf(sub, sizeof sub, "%s[%d]", at, (int)j);
            bool_offsets(b, T, S.types[t].elem, sub, count);
        }
    } else if (t >= TY_USER && S.types[t].kind == K_STRUCT) {
        const seed_struct *st = &S.structs[S.types[t].strct];
        for (k = 0; k < st->nfield; k++) {
            const seed_field *fl = &S.fields[st->field0 + k];
            snprintf(sub, sizeof sub, "%s%sf_%s", at, *at != 0 ? "." : "", tok_text(&S.mods[st->module], fl->tok));
            bool_offsets(b, T, fl->type, sub, count);
        }
    }
}

/* The cint-abi-1 wrapper `cx` + C(P) + C(name) of an export of the root module (SPEC-03 A-12;
 * the seed's interim rule, which has no attributes). Every view is bound first, so a refusal
 * comes before any fault (H-12); then shape variables must agree (E_SHAPE) and no inout view
 * may overlap another view argument (E_ALIAS, H-13), both at entry. The depth limit comes
 * from the context configuration (CINT_DEPTH_FROM_CONFIG). */
static void emit_wrapper(const seed_func *f)
{
    sbuf sig = {NULL, 0, 0}, call = {NULL, 0, 0}, pre = {NULL, 0, 0}, decl = {NULL, 0, 0};
    int32_t k, i;
    sb_printf(&sig, "\nCINT_RT_EXPORT cint_status %s(cint_ctx *ctx, int64_t fuel", ci_name(cm, f->tok, "cx"));
    sb_printf(&call, "%s(ctx", ci_name(cm, f->tok, "ci"));
    for (k = 0; k < f->nsize; k++) {
        for (i = 0; i < f->nparam && !(S.params[f->param0 + i].type >= TY_USER &&
                                       S.types[S.params[f->param0 + i].type].sp == k); i++) {
        }
        sb_printf(&call, ", n_%s", tok_text(M, S.params[f->param0 + i].tok));
    }
    for (k = 0; k < f->nparam; k++) {
        const seed_param *p = &S.params[f->param0 + k];
        char nm[260];
        snprintf(nm, sizeof nm, "%s", tok_text(M, p->tok));
        if (p->type < TY_USER) {
            sb_printf(&sig, ", %s p_%s", p->type == TY_BOOL ? "uint8_t" : ty_ctype(p->type), nm);
            sb_printf(&call, p->type == TY_BOOL ? ", p_%s != 0u" : ", p_%s", nm);
            continue;
        }
        sb_printf(&sig, ", cint_view p_%s", nm);
        sb_printf(&call, ", q_%s, n_%s", nm, nm);
        sb_printf(&decl, "    static const cint_type t_%s = %s;\n    void *q_%s = NULL;\n    int64_t n_%s = 0;\n", nm,
                  view_type(S.types[p->type].elem), nm, nm);
        sb_printf(&pre, "    if (!cint_view_bind(ctx, &p_%s, &t_%s, %s, (int64_t)sizeof(%s), &q_%s, &n_%s)) {\n"
                        "        return cint_rt_entry_end(ctx);\n    }\n", nm, nm,
                  S.types[p->type].perm ? "CINT_MODE_INOUT" : "CINT_MODE_IN", ctype(p->type), nm, nm);
        {   /* every Bool of the view's elements is 0 or 1 (A-13), checked as bytes */
            sbuf offs = {NULL, 0, 0};
            char T[320];
            int32_t nb = 0;
            snprintf(T, sizeof T, "%s", ctype(p->type));
            bool_offsets(&offs, T, S.types[p->type].elem, "", &nb);
            if (nb > 0) {
                sb_printf(&decl, "    static const uint32_t b_%s[%d] = {%s};\n", nm, (int)nb, offs.p + 2);
                sb_printf(&pre, "    if (!cint_view_check_bools(ctx, q_%s, n_%s, (int64_t)sizeof(%s), b_%s, %du)) {\n"
                                "        return cint_rt_entry_end(ctx);\n    }\n", nm, nm, T, nm, (int)nb);
            }
            free(offs.p);
        }
    }
    for (k = 0; k < f->nparam; k++) {   /* shape variables and constant extents */
        const seed_type *v = &S.types[S.params[f->param0 + k].type];
        char nm[260], first[300];
        if (S.params[f->param0 + k].type < TY_USER || v->sp == -1) {
            continue;
        }
        snprintf(nm, sizeof nm, "%s", tok_text(M, S.params[f->param0 + k].tok));
        for (i = 0; i < k && !(S.params[f->param0 + i].type >= TY_USER && S.types[S.params[f->param0 + i].type].sp == v->sp &&
                               v->sp >= 0); i++) {
        }
        snprintf(first, sizeof first, "%s%s", v->sp == -2 ? "" : "n_", v->sp == -2 ? const_text(TY_I64, (uint64_t)v->extent)
                                                                                     : tok_text(M, S.params[f->param0 + i].tok));
        if (v->sp == -2 || i < k) {
            sb_printf(&pre, "    if (n_%s != %s) {\n        (void)cint_fault_shape(ctx, %s, %du, 0u, %s, n_%s);\n"
                            "        return cint_rt_entry_end(ctx);\n    }\n", nm, first, site(f->entry_site), (int)k,
                      first, nm);
        }
    }
    for (k = 0; k < f->nparam; k++) {   /* an inout view against every other view */
        const seed_param *p = &S.params[f->param0 + k];
        if (p->type < TY_USER || !S.types[p->type].perm) {
            continue;
        }
        for (i = 0; i < f->nparam; i++) {
            const seed_param *q = &S.params[f->param0 + i];
            if (i == k || q->type < TY_USER || (S.types[q->type].perm && i < k)) {
                continue;
            }
            /* two calls: ctype() returns a static buffer, so each operand is formatted alone */
            sb_printf(&pre, "    if (cint_rt_bytes_overlap(q_%s, n_%s * (int64_t)sizeof(%s), ", tok_text(M, p->tok),
                      tok_text(M, p->tok), ctype(p->type));
            sb_printf(&pre, "q_%s, n_%s * (int64_t)sizeof(%s))) {\n"
                            "        (void)cint_fault_alias(ctx, %s, %du, %du);\n        return cint_rt_entry_end(ctx);\n"
                            "    }\n", tok_text(M, q->tok), tok_text(M, q->tok), ctype(q->type), site(f->entry_site),
                      (int)k, (int)i);
        }
    }
    if (f->result != TY_VOID) {
        sb_printf(&sig, ", %s *result", f->result == TY_BOOL ? "uint8_t" : ty_ctype(f->result));
    }
    sb_printf(&out_entries, "%s)\n", sig.p);
    entry_head(f, false, decl.p != NULL ? decl.p : "");
    entry_tail(f, "CINT_DEPTH_FROM_CONFIG", pre.p != NULL ? pre.p : "", call.p, f->result == TY_BOOL ? "r ? 1u : 0u" : "r");
    free(sig.p);
    free(call.p);
    free(pre.p);
    free(decl.p);
}

/* -- the program -------------------------------------------------------------------------------- */

static void emit_structs(void)
{
    int32_t done = 0, i, k;
    while (done < S.nstruct) {
        for (i = 0; i < S.nstruct; i++) {
            seed_struct *st = &S.structs[i];
            bool ready = !st->emitted;
            for (k = 0; k < st->nfield && ready; k++) {
                int32_t t = S.fields[st->field0 + k].type;
                if (t >= TY_USER && S.types[t].kind == K_ARRAY) {
                    t = S.types[t].elem;
                }
                ready = !(t >= TY_USER && S.types[t].kind == K_STRUCT && !S.structs[S.types[t].strct].emitted);
            }
            if (!ready) {
                continue;
            }
            {
                char nm[320];
                snprintf(nm, sizeof nm, "%s", ci_name(st->module, st->tok, "ci"));
                sb_printf(&out_types, "\ntypedef struct %s {\n", nm);
                for (k = 0; k < st->nfield; k++) {
                    const seed_field *fl = &S.fields[st->field0 + k];
                    if (fl->type >= TY_USER && S.types[fl->type].kind == K_ARRAY) {
                        int64_t n = S.types[fl->type].extent;
                        sb_printf(&out_types, "    %s f_%s[%" PRId64 "];\n", ctype(fl->type),
                                  tok_text(&S.mods[st->module], fl->tok), n > 0 ? n : 1);
                    } else {
                        sb_printf(&out_types, "    %s f_%s;\n", ctype(fl->type), tok_text(&S.mods[st->module], fl->tok));
                    }
                }
                sb_printf(&out_types, "} %s;\n", nm);
            }
            st->emitted = 1;
            done++;
        }
    }
}

static void emit_module_tables(int32_t mi)
{
    const seed_module *m = &S.mods[mi];
    int32_t k;
    sym_limit(mi, -1, fmtbuf("cg%s_module", m->symc));
    sb_printf(&out_data, "\nstatic const cint_site_info cg%s_sites[%d] = {\n", m->symc, (int)m->nsite);
    for (k = 0; k < m->nsite; k++) {
        sb_printf(&out_data, "    {%uu, %uu, \"%s\"},\n", (unsigned)m->sites[k].line, (unsigned)m->sites[k].col,
                  m->sites[k].op);
    }
    sb_printf(&out_data, "};\nstatic const cint_module cg%s_module = {\"%s\", %uu, %uu, cg%s_sites};\n", m->symc,
              m->path, (unsigned)strlen(m->path), (unsigned)m->nsite, m->symc);
}

/* The record layout table of the `cm` descriptor: one row per struct of the program, in
 * struct order, so a view's cint_type.record_id is the struct's index (SPEC-03 A-13, A-14;
 * slice 2 patch D-3), with each field's offset, count and (element) type. */
static void emit_records(sbuf *out)
{
    int32_t i, k, nf = 0;
    sb_printf(out, "\nstatic const cint_record_field cg_fields[] = {\n");
    for (i = 0; i < S.nstruct; i++) {
        const seed_struct *st = &S.structs[i];
        char nm[320];
        snprintf(nm, sizeof nm, "%s", ci_name(st->module, st->tok, "ci"));
        for (k = 0; k < st->nfield; k++) {
            const seed_field *fl = &S.fields[st->field0 + k];
            bool arr = fl->type >= TY_USER && S.types[fl->type].kind == K_ARRAY;
            int32_t t = arr ? S.types[fl->type].elem : fl->type;
            sb_printf(out, "    {(uint32_t)offsetof(%s, f_%s), %uu, ", nm, tok_text(&S.mods[st->module], fl->tok),
                      arr ? (unsigned)S.types[fl->type].extent : 1u);
            sb_printf(out, "%s},\n", view_type(t));
        }
    }
    sb_printf(out, "};\nstatic const cint_record_layout cg_records[%d] = {\n", (int)S.nstruct);
    for (i = 0; i < S.nstruct; i++) {
        const seed_struct *st = &S.structs[i];
        char nm[320];
        snprintf(nm, sizeof nm, "%s", ci_name(st->module, st->tok, "ci"));
        sb_printf(out, "    {(uint32_t)sizeof(%s), CINT_ELEM_ALIGN(sizeof(%s)), %uu, 0u, &cg_fields[%d]},\n", nm, nm,
                  (unsigned)st->nfield, (int)nf);
        nf += st->nfield;
    }
    sb_printf(out, "};\n");
}

char *emit_program(size_t *len, char **sitemap, size_t *sitemap_len)
{
    static const char *const cmp_names[] = {"eq", "ne", "lt", "le", "gt", "ge"};
    static const char *const cmp_ops[] = {"==", "!=", "<", "<=", ">", ">="};
    sbuf out = {NULL, 0, 0}, map = {NULL, 0, 0}, rows = {NULL, 0, 0};
    int32_t o, i, k, nobs = 0;
    for (o = 0; o < S.nmod; o++) {   /* C(P) of every module (EMIT-22) */
        sbuf c = {NULL, 0, 0};
        component(&c, (const uint8_t *)S.mods[o].path, strlen(S.mods[o].path) - 3u);
        S.mods[o].symc = c.p;
    }
    if (strstr(S.mods[0].symc, "__") != NULL) {
        diag_at(0, 1u, 1u, "C5051", "the public symbol cm%s contains `__`, which C++ reserves (SPEC-09 EMIT-22)",
                S.mods[0].symc);
    }
    emit_structs();
    for (o = 0; o < S.nmod; o++) {
        int32_t n = 0;
        cm = o;
        M = &S.mods[o];
        nstr = 0;
        for (i = 0; i < S.nfunc; i++) {
            if (S.funcs[i].module == o && S.funcs[i].reach) {
                emit_function(i);
            }
        }
        for (i = 0; i < S.nfunc; i++) {
            if (S.funcs[i].module == o && fn_observed(&S.funcs[i])) {
                emit_observer_entry(&S.funcs[i], n++, &rows);
            }
            if (S.funcs[i].module == o && o == 0 && S.funcs[i].exported) {
                emit_wrapper(&S.funcs[i]);
            }
        }
        nobs += n;
    }
    sb_printf(&out, "/* %s, %s, emitted by cint-seed (cint-boot-1) */\n", "cint-core-1", "cint-rt-2");
    sb_printf(&out, "#include <stdbool.h>\n#include <stddef.h>\n#include <stdint.h>\n#include <string.h>\n\n"
                    "#include \"cint_rt.h\"\n\n#define CG_S(m, i) ((cint_site){(m), (i)})\n");
    for (k = 0; k < 12; k++) {
        if (used_cmp[k]) {
            sb_printf(&out, "\nstatic inline bool cg_%s_%s(%s a, %s b)\n{\n    return a %s b;\n}\n", cmp_names[k / 2],
                      k % 2 ? "u" : "s", k % 2 ? "uint64_t" : "int64_t", k % 2 ? "uint64_t" : "int64_t", cmp_ops[k / 2]);
        }
    }
    if (used_s64) {
        sb_printf(&out, "\nstatic inline int64_t cg_s64(uint64_t v)\n{\n"
                        "    return v <= (uint64_t)INT64_MAX ? (int64_t)v : -(int64_t)~v - 1;\n}\n");
    }
    if (used_index) {
        sb_printf(&out, "\nstatic inline bool cg_index_ok(int64_t i, int64_t n)\n{\n    return i >= 0 && i < n;\n}\n");
    }
    if (out_types.n > 0u) {
        sb_put(&out, out_types.p, out_types.n);
    }
    for (o = 0; o < S.nmod; o++) {
        emit_module_tables(o);
    }
    if (out_data.n > 0u) {
        sb_put(&out, out_data.p, out_data.n);
    }
    sb_printf(&out, "\nstatic const cint_module *const cg_modules[%d] = {", (int)S.nmod);
    for (o = 0; o < S.nmod; o++) {
        sb_printf(&out, "%s&cg%s_module", o > 0 ? ", " : "", S.mods[o].symc);
    }
    sb_printf(&out, "};\nstatic const cint_program cg_program = {%uu, 0u, cg_modules, NULL};\n", (unsigned)S.nmod);
    if (S.nstruct > 0) {
        emit_records(&out);
    }
    sb_printf(&out, "\nCINT_RT_EXPORT const cint_module_info cm%s = {CINT_ABI_VERSION, %uu, &cg_program, %s};\n",
              S.mods[0].symc, (unsigned)S.nstruct, S.nstruct > 0 ? "cg_records" : "NULL");
    if (out_protos.n > 0u) {
        sb_put(&out, "\n", 1u);
        sb_put(&out, out_protos.p, out_protos.n);
    }
    if (out_funcs.n > 0u) {
        sb_put(&out, out_funcs.p, out_funcs.n);
    }
    if (out_entries.n > 0u) {
        sb_put(&out, out_entries.p, out_entries.n);
    }
    if (nobs > 0) {
        sb_printf(&out, "\nstatic const cint_observer_entry cg_entries[%d] = {\n%s};\n", (int)nobs, rows.p);
    }
    sb_printf(&out, "\nCINT_RT_EXPORT const cint_observer cint_observer_desc = {%uu, 0u, &cg_program, %s};\n"
                    "CINT_RT_EXPORT const uint32_t cint_program_abi = CINT_ABI_VERSION;\n",
              (unsigned)nobs, nobs > 0 ? "cg_entries" : "NULL");
    free(rows.p);
    for (o = 0; o < S.nmod; o++) {
        const seed_module *m = &S.mods[o];
        sb_printf(&map, "module %d %s\n", (int)o, m->path);
        for (k = 1; k < m->nsite; k++) {
            sb_printf(&map, "site %d %d %s:%u:%u %s\n", (int)o, (int)k, m->path, (unsigned)m->sites[k].line,
                      (unsigned)m->sites[k].col, m->sites[k].op);
        }
    }
    *len = out.n;
    *sitemap = map.p != NULL ? map.p : seed_alloc(1u, "output");
    *sitemap_len = map.n;
    return out.p;
}
