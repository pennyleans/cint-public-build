/* wb_explain.c: `cint explain` from B1's SIR text (SPEC-06 section 7; box 14 default BX14-12).
 *
 * In roadmap column 02, explain reads the SIR text that B1 writes for each module (SPEC-09
 * SIR-09, SIR-09a) and prints for each function, kernel, script, and test of a module: its
 * signature, every fault site with its identifier `name#n` (n the module's site ordinal, SIR-04),
 * position, operation, and fault codes, its print statements, its reductions with the
 * accumulator width of SPEC-01 IM-80, its fuel charge points, its effect class, its lowering on
 * the two CPU backends, and its SIR text byte for byte. For a kernel it adds the domain, the
 * shapes and where clauses checked at entry, aliasing, the views read and written, and
 * publication (SPEC-02). The effect class is `observe` for a function that prints or calls one
 * that does (SPEC-03 H-2, H-7), and `pure` otherwise. A site is possible unless B1 removed it,
 * or unless its operand type rules the fault out; an exclusion that would need range analysis
 * reads `not analyzed`, because no component performs one yet. For a build target, explain adds
 * the compile budgets of SPEC-09 CINTC-09 and the bound of CINTC-02.
 *
 * Explain reads the tables box 13's reader builds (interp/sir.h; SIR-10), which since box 13
 * unit 8b-2 hold every form B1 writes (OQ-231), and the line of each node for its operands as the
 * text spells them.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cint_bridge.h"
#include "sir.h"
#include "wb.h"
#include "wb_array.h"

#define BIT(code) (1u << (code))

/* The fault codes of each operation (SPEC-01 IM-104, IM-39, IM-45, IM-52, IM-49, IM-130,
 * IM-134), by the first word of its identifier. `signed_only` codes need a signed operand: an
 * unsigned quotient never overflows, and an unsigned operand is never negative. A reduction
 * charges fuel for its terms (IM-139), as `equal` does (ruling OQ-307 A), and `arena.alloc`
 * names a charge of the frame arena. */
typedef struct op_codes {
    const char *word;
    unsigned codes, signed_only;
} op_codes;

static const op_codes OPS[] = {
    {"abs", BIT(1), 0}, {"add", BIT(1), 0}, {"arena", BIT(3) | BIT(8), 0}, {"as", BIT(6), 0},
    {"assert", BIT(13), 0}, {"bind", BIT(4), 0}, {"call", BIT(9) | BIT(12), 0}, {"clamp", BIT(11), 0},
    {"copy", BIT(4), 0}, {"count", BIT(9), 0}, {"decl", BIT(3) | BIT(4), 0}, {"dispatch", BIT(9), 0},
    {"div", BIT(1) | BIT(2), BIT(1)}, {"div_euclid", BIT(1) | BIT(2), BIT(1)},
    {"div_round", BIT(1) | BIT(2), BIT(1)}, {"div_trunc", BIT(1) | BIT(2), BIT(1)},
    {"dot", BIT(1) | BIT(4) | BIT(9), 0}, {"equal", BIT(9), 0}, {"fold_checked", BIT(1) | BIT(9), 0},
    {"fuel", BIT(9), 0}, {"index", BIT(3), 0}, {"isqrt", BIT(11), BIT(11)}, {"isqrt_round", BIT(1) | BIT(11), BIT(11)},
    {"mul", BIT(1), 0}, {"muldiv", BIT(1) | BIT(2), 0}, {"neg", BIT(1), 0}, {"reduce_max", BIT(4) | BIT(9), 0},
    {"reduce_min", BIT(4) | BIT(9), 0}, {"rem", BIT(2), 0}, {"rem_euclid", BIT(2), 0}, {"rem_trunc", BIT(2), 0},
    {"rotl", BIT(5), 0}, {"rotr", BIT(5), 0}, {"shl", BIT(1) | BIT(5), 0}, {"shr", BIT(5), 0},
    {"slice", BIT(3), 0}, {"sub", BIT(1), 0}, {"sum", BIT(1) | BIT(9), 0}, {"sum_sat", BIT(9), 0},
    {"sum_wrap", BIT(9), 0},
};

/* ------------------------------------------------------------------------- */
/* The program as explain reads it: box 13's tables, and the line of each node. */

/* A node's line after its indentation and without its site annotation (SIR-09); w is its
 * operation as the line spells it, empty for a closing brace or data. */
typedef struct tx_node {
    const si_node *n;
    const char *s, *w;
    uint32_t len, wlen, site;
} tx_node;

typedef struct tx_module {
    const si_module *m;
    const char *t;             /* its text */
    size_t *bol;               /* where line k starts, from line 1; after the last, the text's end + 1 */
    tx_node *nodes;
} tx_module;

typedef struct tx_prog {
    const cint_sir_program *p;
    tx_module *mods;
    uint32_t *base;            /* function i of module k has index base[k] + i */
    unsigned char *observe;    /* by function index: 1 for `observe` */
    uint32_t funcs, nodes;
} tx_prog;

static int is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static int starts(const char *s, size_t n, const char *prefix)
{
    size_t k = strlen(prefix);
    return n >= k && memcmp(s, prefix, k) == 0;
}

static int word_is(const char *s, size_t len, const char *w)
{
    return strlen(w) == len && memcmp(s, w, len) == 0;
}

/* The decimal at s (at most 18 digits), with the digits it took in *used. */
static int64_t number_at(const char *s, size_t n, size_t *used)
{
    int64_t v = 0;
    size_t k = 0;
    while (k < n && k < 18u && is_digit(s[k])) v = v * 10 + (s[k++] - '0');
    *used = k;
    return v;
}

static const char *text_of(const si_module *m, si_name n)
{
    return (const char *)m->text + n.at;
}

/* Line k of a module, without its LF. */
static const char *line_at(const tx_module *x, uint32_t k, size_t *len)
{
    *len = x->bol[k + 1u] - 1u - x->bol[k];
    return x->t + x->bol[k];
}

/* The lines of module m. B1 writes `#` only to start a site annotation (SIR-09, SIR-17). */
static int tx_read(tx_module *x, const si_module *m)
{
    size_t lines = 1u, k = 1u;
    x->m = m;
    x->t = (const char *)m->text;
    for (size_t i = 0; i < m->len; i++) lines += x->t[i] == '\n';
    x->bol = malloc((lines + 2u) * sizeof *x->bol);
    x->nodes = calloc((size_t)m->nnode + 1u, sizeof *x->nodes);
    if (x->bol == NULL || x->nodes == NULL) return -1;
    x->bol[1] = 0;
    for (size_t i = 0; i < m->len; i++)
        if (x->t[i] == '\n') x->bol[++k] = i + 1u;
    x->bol[k + 1u] = m->len + 1u;
    for (uint32_t i = 0; i < m->nnode; i++) {
        tx_node *d = &x->nodes[i];
        size_t n = 0, ind = 0;
        const char *s = line_at(x, m->nodes[i].line, &n), *hash = m->nodes[i].site != 0 ? memchr(s, '#', n) : NULL;
        n = hash != NULL ? (size_t)(hash - s) : n;
        while (n > 0 && s[n - 1u] == ' ') n--;
        while (ind < n && s[ind] == ' ') ind++;
        d->n = &m->nodes[i];
        d->s = s + ind;
        d->len = (uint32_t)(n - ind);
        d->site = d->n->site;
        d->w = d->n->word.len != 0 ? text_of(m, d->n->word) : d->s;
        d->wlen = d->n->word.len;
    }
    return 0;
}

static void tx_close(tx_prog *P)
{
    for (uint32_t k = 0; P->mods != NULL && k < P->p->nmod; k++) {
        free(P->mods[k].bol);
        free(P->mods[k].nodes);
    }
    free(P->mods);
    free(P->base);
    free(P->observe);
}

/* Reads every module of p and computes the SPEC-03 H-1 class of every function by H-2 and H-7:
 * `observe` when it prints or calls or dispatches one that is `observe`, otherwise `pure`.
 * Column 02 has no `extern`, so no function is `unrestricted`. Returns 0, or -1 when memory ran
 * out; tx_close frees it either way. */
static int tx_open(tx_prog *P, const cint_sir_program *p)
{
    memset(P, 0, sizeof *P);
    P->p = p;
    P->mods = calloc((size_t)p->nmod + 1u, sizeof *P->mods);
    P->base = malloc(((size_t)p->nmod + 1u) * sizeof *P->base);
    if (P->mods == NULL || P->base == NULL) return -1;
    for (uint32_t k = 0; k < p->nmod; k++) {
        if (tx_read(&P->mods[k], &p->mods[k]) != 0) return -1;
        P->base[k] = P->funcs;
        P->funcs += p->mods[k].nfunc;
        P->nodes += p->mods[k].nnode;
    }
    if ((P->observe = calloc((size_t)P->funcs + 1u, 1)) == NULL) return -1;
    for (int changed = 1; changed;) {
        changed = 0;
        for (uint32_t k = 0; k < p->nmod; k++) {
            const si_module *m = &p->mods[k];
            for (uint32_t i = 0; i < m->nfunc; i++) {
                const si_func *f = &m->funcs[i];
                for (uint32_t j = f->first; !P->observe[P->base[k] + i] && j < f->first + f->count; j++) {
                    const si_node *n = &m->nodes[j];
                    int calls = n->op == OP_CALL || n->op == OP_KCALL;
                    if (n->op == OP_PRINT_BEGIN || (calls && P->observe[P->base[n->cmod] + n->cfn])) {
                        P->observe[P->base[k] + i] = 1;
                        changed = 1;
                    }
                }
            }
        }
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Lines and operands.                                                        */

/* Operand j (from 0) of a line: the bytes between `, ` after its operation, or NULL. */
static const char *operand(const tx_node *d, unsigned j, size_t *len)
{
    const char *end = d->s + d->len, *s = d->w + d->wlen + 1u, *c = NULL;
    if (s > end) return NULL;
    for (; j > 0; j--) {
        if ((c = memchr(s, ',', (size_t)(end - s))) == NULL) return NULL;
        s = c + 2 <= end ? c + 2 : end;
    }
    c = memchr(s, ',', (size_t)(end - s));
    *len = (size_t)((c != NULL ? c : end) - s);
    return s;
}

/* The last integer of a line (`fuel.charge 3` is 3), or -1. */
static int64_t last_int(const tx_node *d)
{
    size_t k = d->len, used;
    while (k > 0 && is_digit(d->s[k - 1u])) k--;
    return k < d->len ? number_at(d->s + k, d->len - k, &used) : -1;
}

/* The slot number of `$name.k` at s, or -1; the name in *name and *len. */
static int64_t slot_at(const char *s, size_t n, const char **name, size_t *len)
{
    size_t k = 1, used;
    *name = s;
    *len = 0;
    if (n < 2u || s[0] != '$') return -1;
    while (k < n && s[k] != '.' && s[k] != ',' && s[k] != ' ') k++;
    *name = s + 1;
    *len = k - 1u;
    return k + 1u < n && s[k] == '.' && is_digit(s[k + 1u]) ? number_at(s + k + 1u, n - k - 1u, &used) : -1;
}

/* 1 when the operand at s names parameter `name` (`%name` or `%p.name`). */
static int names_param(const char *s, size_t n, const char *name, size_t len)
{
    size_t skip = n > 0 && s[0] == '%' ? (starts(s + 1, n - 1u, "p.") ? 3u : 1u) : n + 1u;
    return skip <= n && n - skip == len && memcmp(s + skip, name, len) == 0;
}

static void put_param(wb_buf *b, const char *s, size_t n)
{
    size_t skip = n > 0 && s[0] == '%' ? (starts(s + 1, n - 1u, "p.") ? 3u : 1u) : 0u;
    wb_put(b, s + (skip <= n ? skip : n), skip <= n ? n - skip : 0u);
}

/* ------------------------------------------------------------------------- */
/* Sites, parameters, and loops.                                              */

/* One fault site of a function. */
typedef struct ex_site {
    const si_site *s;
    const char *op;    /* its operation */
    uint32_t op_len;
    unsigned codes;
    const char *why;   /* NULL: possible, not analyzed; otherwise why it is excluded */
} ex_site;

static size_t part_len(const char *s, size_t n)
{
    size_t k = 0;
    while (k < n && s[k] != '.') k++;
    return k;
}

/* E_SHAPE when a kernel checks shapes or where clauses at entry, E_ALIAS when it checks
 * aliasing (SPEC-02 F-5): the codes of its dispatch beside E_FUEL. */
static unsigned entry_codes(const tx_module *x, const si_func *f)
{
    unsigned codes = 0;
    for (uint32_t i = f->first; i < f->first + f->count; i++) {
        const tx_node *d = &x->nodes[i];
        codes |= word_is(d->w, d->wlen, "bind.shape") || word_is(d->w, d->wlen, "bind.where") ? BIT(4)
               : word_is(d->w, d->wlen, "bind.alias") ? BIT(7) : 0u;
    }
    return codes;
}

/* The site's codes, from its operation: `<word>.<form>.<type>...`, `fuel.charge`, `call.enter`,
 * `copy.shape`. A wrapping or saturating form never overflows or narrows. A dispatch adds the
 * entry checks of its kernel. A reduction's step in a kernel (`reduce.add`) and its epilogue
 * (`reduce.final`) charge no fuel, a step never faults E_SHAPE, and neither does a minimum or
 * maximum with an initial value. */
static ex_site classify(const tx_prog *P, const tx_module *x, const si_func *f, const tx_node *d)
{
    const si_site *s = &x->m->sites[d->site];
    ex_site e = {s, text_of(x->m, s->op), s->op.len, 0, NULL};
    const char *op = e.op;
    size_t n = e.op_len, w = part_len(op, n);
    size_t g = w < n ? part_len(op + w + 1u, n - w - 1u) : 0;
    size_t ty = w + 1u + g + 1u;
    for (size_t i = 0; i < sizeof OPS / sizeof OPS[0]; i++) {
        if (!word_is(op, w, OPS[i].word)) continue;
        e.codes = OPS[i].codes;
        if (w < n && (word_is(op + w + 1u, g, "wrap") || word_is(op + w + 1u, g, "sat")))
            e.codes &= ~(BIT(1) | BIT(6));
        if (w < n && word_is(op + w + 1u, g, "alias")) e.codes = BIT(7);
        if (ty < n && op[ty] == 'u' && (e.codes & OPS[i].signed_only) != 0) {
            e.codes &= ~OPS[i].signed_only;
            if (e.codes == 0) e.why = "an unsigned operand is never negative (SPEC-01 IM-52)";
        }
        break;
    }
    if (d->n->op == OP_KCALL) {
        e.codes |= entry_codes(&P->mods[d->n->cmod], &P->p->mods[d->n->cmod].funcs[d->n->cfn]);
    } else if (word_is(d->w, d->wlen, "dispatch.entry")) {
        e.codes |= entry_codes(x, f);
    } else if (word_is(d->w, d->wlen, "reduce.add") || word_is(d->w, d->wlen, "reduce.final")) {
        e.codes &= ~(BIT(9) | (word_is(d->w, d->wlen, "reduce.add") ? BIT(4) : 0u));
    } else if ((word_is(op, w, "reduce_min") || word_is(op, w, "reduce_max")) && memchr(d->s, ',', d->len) != NULL) {
        e.codes &= ~BIT(4);   /* `reduce_min.checked.E xs, init` */
    }
    return e;
}

static const char *kind_word(const si_func *f)
{
    return f->kind == FN_SCRIPT ? "script" : f->kind == FN_TEST ? "test" : f->kind == FN_KERNEL ? "kernel"
                                                                                                 : "function";
}

static int named(const si_func *f)
{
    return f->kind == FN_FUNC || f->kind == FN_KERNEL;
}

/* The name in a site identifier: the function's or kernel's name, `script`, or `test<N>`. */
static void site_name(const si_module *m, const si_func *f, char *out, size_t cap)
{
    if (named(f)) (void)snprintf(out, cap, "%.*s", (int)f->name.len, text_of(m, f->name));
    else if (f->kind == FN_SCRIPT) (void)snprintf(out, cap, "script");
    else (void)snprintf(out, cap, "test%lld", (long long)f->test);
}

/* 1 for a function or kernel called `name`. */
static int named_as(const si_module *m, const si_func *f, const char *name)
{
    return named(f) && word_is(text_of(m, f->name), f->name.len, name);
}

/* The SIR text of a function: its header line through its closing brace, with the final LF. */
static void sir_text(const tx_module *x, const si_func *f, const char **p, size_t *len)
{
    size_t end = x->bol[(f->count > 0 ? x->m->nodes[f->first + f->count - 1u].line : f->line) + 1u];
    *p = x->t + x->bol[f->line];
    *len = (end <= x->m->len ? end : x->m->len) - x->bol[f->line];
}

/* The parameters as the header line spells them: `%name: TYPE` items between the parentheses.
 * SIR text keeps no direction for a view of a function, through which the caller's storage is
 * reachable; a kernel's view is `in` unless the kernel stages it for writing (`out`, `inout`). */
typedef struct ex_param {
    const char *name, *type;
    int name_len, type_len, scalar, view;
    const char *direction;
} ex_param;

static int scalar_name(const char *s, size_t n)
{
    static const char *const names[] = {"Bool", "I8", "I16", "I32", "I64", "U8", "U16", "U32", "U64"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (word_is(s, n, names[i])) return 1;
    }
    return 0;
}

static uint32_t params_of(const tx_module *x, const si_func *f, ex_param *out, uint32_t cap)
{
    size_t hl = 0;
    const char *head = line_at(x, f->line, &hl), *end = head + hl;
    const char *c = named(f) ? memchr(head, '(', hl) : NULL;
    uint32_t n = 0;
    size_t len = 0;
    c = c != NULL ? c + 1 : end;
    while (c < end && *c == '%' && n < cap) {
        ex_param *q = &out[n];
        const char *name = c + 1, *t;
        int depth = 0;
        name += starts(name, (size_t)(end - name), "p.") ? 2 : 0;
        for (t = name; t < end && *t != ':'; t++) {
        }
        q->name = name;
        q->name_len = (int)(t - name);
        t += t + 2 <= end ? 2 : 0;   /* after ": " */
        int inout = starts(t, (size_t)(end - t), "inout "), o = starts(t, (size_t)(end - t), "out ");
        t += inout ? 6 : o ? 4 : 0;
        for (c = t; c < end && (depth > 0 || (*c != ',' && *c != ')')); c++)
            depth += *c == '[' ? 1 : *c == ']' ? -1 : 0;
        q->type = t;
        q->type_len = (int)(c - t);
        q->view = memchr(t, '[', (size_t)(c - t)) != NULL;
        q->scalar = !q->view && !inout && scalar_name(t, (size_t)(c - t));
        q->direction = q->view ? (f->kind == FN_KERNEL ? "in" : "view") : inout ? "inout" : o ? "out" : "in";
        n++;
        c = c < end && *c == ',' && c + 2 <= end ? c + 2 : end;   /* `, ` before the next */
    }
    for (uint32_t i = f->first; f->kind == FN_KERNEL && i < f->first + f->count; i++) {
        const tx_node *d = &x->nodes[i];   /* `dispatch.stage $sc.k, %p, 1|2` */
        const char *p = word_is(d->w, d->wlen, "dispatch.stage") ? operand(d, 1, &len) : NULL;
        for (uint32_t k = 0; p != NULL && k < n; k++) {
            if (names_param(p, len, out[k].name, (size_t)out[k].name_len))
                out[k].direction = last_int(d) == 2 ? "inout" : "out";
        }
    }
    return n;
}

/* The result type as the header line spells it, or NULL. */
static const char *result_of(const tx_module *x, const si_func *f, int *len)
{
    size_t n = 0;
    const char *h = line_at(x, f->line, &n);
    for (size_t k = 0; f->kind == FN_FUNC && k + 7u <= n; k++) {
        if (memcmp(h + k, ") -> ", 5) == 0) {
            *len = (int)(n - (k + 5u) - 2u);   /* up to " {" */
            return h + k + 5u;
        }
    }
    return NULL;
}

/* 1 when a line opens a block: `if {`, `loop L<k> {`, `} then %t {`, ... */
static int opens(const tx_node *d)
{
    static const char *const words[] = {"if", "arm", "loop", "switch", "case", "default", "block", "else"};
    if (d->len == 0 || d->s[d->len - 1u] != '{') return 0;
    for (size_t i = 0; i < sizeof words / sizeof words[0]; i++) {
        if (word_is(d->w, d->wlen, words[i])) return 1;
    }
    return d->s[0] == '}';
}

/* The label `L<k>` of the innermost loop around node `i` of `f`, as the loop's line spells it
 * (SPEC-09 SIR-09a), or -1. Walking back from the node, blocks closed before it are skipped; the
 * first block still open around it that is a loop is the one. */
static int64_t loop_of(const tx_module *x, const si_func *f, uint32_t i)
{
    uint32_t depth = 0;
    for (uint32_t k = i; k-- > f->first;) {
        const tx_node *d = &x->nodes[k];
        int closes = d->len > 0 && d->s[0] == '}', open = opens(d);
        if (closes && open) {          /* `} then %t {`: one block closes, one opens */
            depth += depth == 0;
        } else if (closes) {
            depth++;
        } else if (open && depth > 0) {
            depth--;
        } else if (open && word_is(d->w, d->wlen, "loop")) {
            const char *l = memchr(d->s, 'L', d->len);
            size_t used;
            return l != NULL ? number_at(l + 1, d->len - (size_t)(l + 1 - d->s), &used) : 0;
        }
    }
    return -1;
}

static void put_codes(wb_buf *b, unsigned codes, int json)
{
    const char *sep = "";
    for (unsigned c = 1; c <= 13u; c++) {
        if (codes & BIT(c)) {
            if (json) wb_f(b, "%s\"%s\"", sep, WB_CODE_NAMES[c]);
            else wb_f(b, "%s%s", sep, WB_CODE_NAMES[c]);
            sep = json ? "," : " ";
        }
    }
}

static void put_text(wb_buf *b, const char *s, size_t n, int json)
{
    if (json) wb_jstr(b, s, n);
    else wb_put(b, s, n);
}

static size_t text_width(const tx_module *x, const si_site *s)
{
    char tmp[64];
    return strlen(x->m->path) + (size_t)snprintf(tmp, sizeof tmp, ":%u:%u", s->line, s->col);
}

/* The callee's qualified name in a call or dispatch line: after `@`, up to `(`. */
static void put_callee(wb_buf *b, const tx_node *d, int json)
{
    const char *at = memchr(d->s, '@', d->len);
    const char *open = at != NULL ? memchr(at, '(', d->len - (size_t)(at - d->s)) : NULL;
    put_text(b, open != NULL ? at + 1 : "", open != NULL ? (size_t)(open - at - 1) : 0u, json);
}

/* ------------------------------------------------------------------------- */
/* Reductions (SPEC-01 IM-77 to IM-87, IM-130, IM-134; SPEC-02 R-2, R-7).      */

/* The type a lowercase suffix names (`i32` I32, `bool` Bool) into out; returns its width. */
static uint32_t type_name(const char *s, size_t n, char *out, size_t cap)
{
    size_t used;
    if (n < 2u || word_is(s, n, "bool")) {
        (void)snprintf(out, cap, "%s", n < 2u ? "?" : "Bool");
        return 1u;
    }
    (void)snprintf(out, cap, "%c%.*s", s[0] == 'i' ? 'I' : 'U', (int)(n - 1u), s + 1);
    return (uint32_t)number_at(s + 1, n - 1u, &used);
}

/* One reduction: `<word>.<form>.<E>[.<R>]`, its site identifier, and in a kernel the output it
 * reduces into. Prints the result type, the accumulator that SPEC-01 IM-80 bounds (IM-87 for a
 * dot product), and how it ends; nothing when the operation is no reduction. */
static void put_reduction(wb_buf *b, const char *id, const char *target, size_t tlen, const char *op, size_t n,
                          int json, int *count)
{
    const char *part[4] = {op, op, op, op};
    size_t len[4] = {0, 0, 0, 0}, k = 0, at = 0, e;
    char result[24], acc[64] = "", end[96];
    while (k < 4u && at <= n) {
        part[k] = op + at;
        len[k] = part_len(op + at, n - at);
        at += len[k++] + 1u;
    }
    int sum = word_is(op, len[0], "sum") || word_is(op, len[0], "dot"), fold = word_is(op, len[0], "fold_checked");
    int sat = word_is(op, len[0], "sum_wrap") || word_is(op, len[0], "sum_sat");
    int minmax = word_is(op, len[0], "reduce_min") || word_is(op, len[0], "reduce_max");
    if (!sum && !fold && !sat && !minmax && !word_is(op, len[0], "count")) return;
    e = sat ? 1u : 2u;   /* the element type: `sum_wrap.E`, `fold_checked.add.E`, `sum.checked.E.R` */
    uint32_t bits = type_name(part[e], len[e], result, sizeof result);
    if (sum && k == 4u) (void)type_name(part[3], len[3], result, sizeof result);
    if (sum) {
        (void)snprintf(acc, sizeof acc, "%u + ceil(log2 n) bits for n terms (SPEC-01 %s)", op[0] == 'd' ? 2u * bits : bits,
                       op[0] == 'd' ? "IM-87" : "IM-80");
        (void)snprintf(end, sizeof end, "exact; one checked narrowing to %s at the end", result);
    } else if (fold) {
        (void)snprintf(end, sizeof end, "checked at each term, in canonical order");
    } else if (sat) {
        (void)snprintf(end, sizeof end, "%s, in canonical order; its value never faults",
                       op[4] == 'w' ? "wrapping" : "saturating");
    } else if (minmax) {
        (void)snprintf(end, sizeof end, "E_SHAPE when there is no term and no initial value");
    } else {
        (void)snprintf(result, sizeof result, "I64");
        (void)snprintf(end, sizeof end, "the number of true elements");
    }
    if (json) {
        wb_s(b, *count ? ",{\"accumulator\":" : "{\"accumulator\":");
        if (acc[0] != '\0') wb_jstr(b, acc, strlen(acc));
        else wb_s(b, "null");
        wb_s(b, ",\"end\":");
        wb_jstr(b, end, strlen(end));
        wb_s(b, ",\"id\":");
        if (id != NULL) wb_jstr(b, id, strlen(id));
        else wb_s(b, "null");
        wb_s(b, ",\"operation\":");
        wb_jstr(b, op, n);
        wb_f(b, ",\"result\":\"%s\"", result);
        if (target != NULL) {
            wb_s(b, ",\"target\":");
            wb_jstr(b, target, tlen);
        }
        wb_s(b, "}");
    } else {
        wb_s(b, *count ? "" : "  reductions\n");
        wb_s(b, "    ");
        if (target != NULL) wb_f(b, "%.*s ", (int)tlen, target);
        wb_f(b, "%s  %.*s  result %s", id != NULL ? id : "(no site)", (int)n, op, result);
        if (acc[0] != '\0') wb_f(b, "; accumulator %s", acc);
        wb_f(b, "; %s\n", end);
    }
    (*count)++;
}

/* The reductions of a body: each sequential reduction site, and in a kernel each output it
 * reduces into (`reduce.add OP %s, %t`), with the site of its epilogue, or else of its step.
 * Returns their number. */
static int reductions(wb_buf *b, const tx_module *x, const si_func *f, const char *sname, int json)
{
    char id[160];
    int count = 0;
    for (uint32_t i = f->first; i < f->first + f->count; i++) {
        const tx_node *d = &x->nodes[i];
        if (d->site != 0 && f->kind != FN_KERNEL) {
            const si_site *s = &x->m->sites[d->site];
            (void)snprintf(id, sizeof id, "%s#%u", sname, d->site);
            put_reduction(b, id, NULL, 0, text_of(x->m, s->op), s->op.len, json, &count);
            continue;
        }
        size_t tn = 0, on = 0;
        const char *op = word_is(d->w, d->wlen, "reduce.add") ? operand(d, 0, &tn) : NULL, *tg = NULL;
        int seen = 0;
        while (op != NULL && on < tn && op[on] != ' ') on++;
        if (op != NULL && on + 1u < tn) {   /* operand 0 is `OP %s` */
            tg = op + on + 1u;
            tn -= on + 1u;
        }
        uint32_t site = d->site;
        for (uint32_t j = f->first; tg != NULL && j < f->first + f->count; j++) {
            const tx_node *q = &x->nodes[j];
            size_t qn = 0;
            const char *qt = word_is(q->w, q->wlen, "reduce.add") || word_is(q->w, q->wlen, "reduce.final")
                                 ? operand(q, 0, &qn) : NULL;
            int same = qt != NULL && qn > tn && memcmp(qt + qn - tn, tg, tn) == 0 && qt[qn - tn - 1u] == ' ';
            seen |= same && j < i && q->w[7] == 'a';
            site = same && q->w[7] == 'f' && q->site != 0 ? q->site : site;
        }
        if (tg != NULL && !seen) {
            (void)snprintf(id, sizeof id, "%s#%u", sname, site);
            put_reduction(b, site != 0 ? id : NULL, tg[0] == '%' ? tg + 1 : tg, tg[0] == '%' ? tn - 1u : tn, op, on,
                          json, &count);
        }
    }
    return count;
}

/* ------------------------------------------------------------------------- */
/* Kernels (SPEC-02 K-8 to K-10a, A-8, F-5, X-8, section 6).                   */

enum { KK_ALIASING, KK_DOMAIN, KK_PUBLISH, KK_READS, KK_SHAPES, KK_WRITES };

/* The node of f that defines temporary `%t<k>` (the n bytes at t), or NULL. */
static const tx_node *def_of(const tx_module *x, const si_func *f, const char *t, size_t n)
{
    for (uint32_t i = f->first; n > 1u && t[0] == '%' && i < f->first + f->count; i++) {
        const tx_node *d = &x->nodes[i];
        if (d->len > n + 3u && memcmp(d->s, t, n) == 0 && d->s[n] == ' ' && d->s[n + 1u] == '=') return d;
    }
    return NULL;
}

static void put_slot(wb_buf *b, const tx_module *x, const si_func *f, int64_t slot, int depth);

/* A shape value as the source names it: a shape variable, an extent of a parameter, or a
 * constant; `?` for anything computed. */
static void put_value(wb_buf *b, const tx_module *x, const si_func *f, const char *t, size_t n, int depth)
{
    const tx_node *d = def_of(x, f, t, n);
    const char *name = "", *o0 = NULL, *o1 = NULL;
    size_t len = 0, l0 = 0, l1 = 0;
    if (d != NULL) {
        o0 = operand(d, 0, &l0);
        o1 = operand(d, 1, &l1);
    }
    int64_t slot = o0 != NULL && word_is(d->w, d->wlen, "load.local") ? slot_at(o0, l0, &name, &len) : -1;
    if (d == NULL && n > 0) {
        put_param(b, t, n);   /* a parameter, or a literal */
    } else if (o0 != NULL && word_is(d->w, d->wlen, "const")) {
        const char *v = memchr(o0, ' ', l0);   /* `const I64 2` */
        wb_put(b, v != NULL ? v + 1 : o0, v != NULL ? l0 - (size_t)(v + 1 - o0) : l0);
    } else if (slot >= 0 && !word_is(name, len, "sc")) {
        wb_put(b, name, len);
    } else if (slot >= 0 && depth < 8) {
        put_slot(b, x, f, slot, depth + 1);
    } else if (o1 != NULL && word_is(d->w, d->wlen, "view.extent")) {
        wb_f(b, "extent %.*s of ", (int)l1, o1);
        put_param(b, o0, l0);
    } else {
        wb_s(b, "?");
    }
}

/* What scratch slot `$sc.<slot>` holds: the value of its first store. */
static void put_slot(wb_buf *b, const tx_module *x, const si_func *f, int64_t slot, int depth)
{
    for (uint32_t i = f->first; i < f->first + f->count; i++) {
        const tx_node *d = &x->nodes[i];
        const char *o0 = NULL, *o1 = NULL, *name = "";
        size_t l0 = 0, l1 = 0, len = 0;
        if (word_is(d->w, d->wlen, "store.local") && (o0 = operand(d, 0, &l0)) != NULL &&
            (o1 = operand(d, 1, &l1)) != NULL && slot_at(o0, l0, &name, &len) == slot && word_is(name, len, "sc")) {
            put_value(b, x, f, o1, l1, depth);
            return;
        }
    }
    wb_s(b, "?");
}

/* The name of slot `slot` in f (`$y.7` is y), or NULL for a scratch slot. */
static const char *slot_name(const tx_module *x, const si_func *f, int64_t slot, size_t *len)
{
    for (uint32_t i = f->first; slot >= 0 && i < f->first + f->count; i++) {
        const tx_node *d = &x->nodes[i];
        for (const char *s = d->s; (s = memchr(s, '$', d->len - (size_t)(s - d->s))) != NULL; s++) {
            const char *name;
            if (slot_at(s, d->len - (size_t)(s - d->s), &name, len) == slot && !word_is(name, *len, "sc")) return name;
        }
    }
    return NULL;
}

static const tx_node *first_word(const tx_module *x, const si_func *f, const char *w)
{
    for (uint32_t i = f->first; i < f->first + f->count; i++) {
        if (word_is(x->nodes[i].w, x->nodes[i].wlen, w)) return &x->nodes[i];
    }
    return NULL;
}

/* The domain: `dispatch.charge $sc.c, rank, <extents>` names the slots of the extents and
 * `dispatch.item %t, rank, <over>, <extents>` those of the over variables. B1 before box 13
 * unit 7b writes no slots: there the extents are slots c - rank to c - 1 and, with %t loaded
 * from slot k, the over variables slots k + 1 to k + rank. An element-form kernel's over
 * variables are scratch, so only its extents show (SPEC-02 K-12). */
static void put_domain(wb_buf *t, const tx_module *x, const si_func *f)
{
    const tx_node *charge = first_word(x, f, "dispatch.charge"), *item = first_word(x, f, "dispatch.item"), *d = NULL;
    const char *o = NULL, *name = "";
    size_t on = 0, len = 0;
    int64_t rank = 0, c = -1, k = -1;
    if (charge != NULL && (o = operand(charge, 1, &on)) != NULL) rank = number_at(o, on, &len);
    if (charge != NULL && (o = operand(charge, 0, &on)) != NULL) c = slot_at(o, on, &name, &len);
    if (item != NULL && (o = operand(item, 0, &on)) != NULL && (d = def_of(x, f, o, on)) != NULL &&
        (o = operand(d, 0, &on)) != NULL) {
        k = slot_at(o, on, &name, &len);
    }
    wb_s(t, "over [");
    for (int64_t j = 0; c >= rank && j < rank && rank <= 8; j++) {
        int64_t e = c - rank + j, v = k >= 0 ? k + 1 + j : -1;
        const char *s = NULL;
        if ((o = operand(charge, 2u + (unsigned)j, &on)) != NULL) e = slot_at(o, on, &name, &len);
        if (item != NULL && (o = operand(item, 2u + (unsigned)j, &on)) != NULL) v = slot_at(o, on, &name, &len);
        s = slot_name(x, f, v, &len);
        wb_s(t, j ? ", " : "");
        if (s != NULL) wb_f(t, "%.*s: ", (int)len, s);
        put_slot(t, x, f, e, 0);
    }
    wb_s(t, "]");
}

/* Item j of a kernel key into t; 0 when there is none. The items of `reads` are the
 * parameters, the domain is one item, and the items of the other keys are lines of the body. */
static int kernel_item(wb_buf *t, const tx_module *x, const si_func *f, const ex_param *ps, int key, uint32_t j)
{
    static const char *const cmp[] = {"?", "==", "!=", "<", "<=", ">", ">="};
    const tx_node *d = key != KK_READS && key != KK_DOMAIN ? &x->nodes[f->first + j] : NULL, *q = NULL;
    const char *o0 = NULL, *o1 = NULL, *o2 = NULL, *o3 = NULL, *name = "";
    size_t l0 = 0, l1 = 0, l2 = 0, l3 = 0, len = 0, used = 0;
    if (d != NULL) {
        o0 = operand(d, 0, &l0);
        o1 = operand(d, 1, &l1);
        o2 = operand(d, 2, &l2);
        o3 = operand(d, 3, &l3);
    }
    if (key == KK_DOMAIN) {
        put_domain(t, x, f);
    } else if (key == KK_ALIASING && word_is(d->w, d->wlen, "bind.alias") && o1 != NULL) {
        put_param(t, o0, l0);
        wb_s(t, " must not overlap ");
        put_param(t, o1, l1);
    } else if (key == KK_PUBLISH && word_is(d->w, d->wlen, "dispatch.publish") && o0 != NULL) {
        put_param(t, o0, l0);
    } else if (key == KK_SHAPES && word_is(d->w, d->wlen, "store.local") && o1 != NULL &&
               slot_at(o0, l0, &name, &len) >= 0 && !word_is(name, len, "sc") && (q = def_of(x, f, o1, l1)) != NULL &&
               word_is(q->w, q->wlen, "view.extent")) {
        wb_f(t, "%.*s = ", (int)len, name);   /* a shape variable bound from an extent */
        put_value(t, x, f, o1, l1, 0);
    } else if (key == KK_SHAPES && word_is(d->w, d->wlen, "bind.shape") && o3 != NULL) {
        put_param(t, o0, l0);   /* `bind.shape %p, j, dim, %t` */
        wb_f(t, " extent %.*s = ", (int)l2, o2);
        put_value(t, x, f, o3, l3, 0);
    } else if (key == KK_SHAPES && word_is(d->w, d->wlen, "bind.where") && o3 != NULL) {
        int64_t c = number_at(o1, l1, &used);   /* `bind.where j, cmp, %a, %b` */
        wb_s(t, "where ");
        put_value(t, x, f, o2, l2, 0);
        wb_f(t, " %s ", cmp[c >= 1 && c <= 6 ? c : 0]);
        put_value(t, x, f, o3, l3, 0);
    } else if (key == KK_WRITES && word_is(d->w, d->wlen, "dispatch.stage") && o1 != NULL) {
        uint32_t stores = 0;   /* `index.store $sc.k, ...` writes the staged copy */
        for (uint32_t i = f->first; i < f->first + f->count; i++) {
            const char *s = word_is(x->nodes[i].w, x->nodes[i].wlen, "index.store") ? operand(&x->nodes[i], 0, &len)
                                                                                      : NULL;
            stores += s != NULL && len == l0 && memcmp(s, o0, l0) == 0;
        }
        put_param(t, o1, l1);
        wb_f(t, ": %u element store%s, in each work-item's own block (SPEC-02 K-10)%s", stores, stores == 1u ? "" : "s",
             last_int(d) == 1 ? ", every element of the block on every path that completes (K-10a, checked by cintc)"
                              : "");
    } else if (key == KK_READS && ps[j].view) {
        uint32_t loads = 0;   /* `index.load %p, ...`, or of its staged copy */
        const char *staged = NULL;
        size_t sl = 0;
        for (uint32_t i = f->first; i < f->first + f->count; i++) {
            const tx_node *s = &x->nodes[i];
            if (word_is(s->w, s->wlen, "dispatch.stage") && (o1 = operand(s, 1, &l1)) != NULL &&
                names_param(o1, l1, ps[j].name, (size_t)ps[j].name_len)) {
                staged = operand(s, 0, &sl);
            }
        }
        for (uint32_t i = f->first; i < f->first + f->count; i++) {
            const char *s = word_is(x->nodes[i].w, x->nodes[i].wlen, "index.load") ? operand(&x->nodes[i], 0, &len)
                                                                                     : NULL;
            loads += s != NULL && (names_param(s, len, ps[j].name, (size_t)ps[j].name_len) ||
                                   (staged != NULL && len == sl && memcmp(s, staged, sl) == 0));
        }
        if (loads == 0) return 0;
        wb_f(t, "%.*s: %u element load%s", ps[j].name_len, ps[j].name, loads, loads == 1u ? "" : "s");
    } else {
        return 0;
    }
    return 1;
}

/* A kernel key: in the text form one line, `  <key>  <items>; ...<tail>`, and in the JSON form
 * `"<key>":[<items>]`, each item a string as the text form words it (the domain one string). */
static void kernel_key(wb_buf *b, const tx_module *x, const si_func *f, const ex_param *ps, uint32_t np, int key,
                       int json)
{
    static const char *const names[] = {"aliasing", "domain", "publish", "reads", "shapes", "writes"};
    static const char *const tails[] = {
        "   checked at entry, E_ALIAS (SPEC-02 A-8, F-5)",
        ("   row-major work-items, one fuel unit each, charged by the dispatch before any runs (SPEC-02 K-8, K-9, "
         "X-8)"),
        ", only when every work-item completes; a fault publishes nothing", "",
        "   else E_SHAPE at entry (SPEC-02 F-5)", ""};
    wb_buf t = {0};
    uint32_t n = 0, count = key == KK_READS ? np : key == KK_DOMAIN ? 1u : f->count;
    wb_f(b, json ? (key == KK_DOMAIN ? "\"%s\":" : "\"%s\":[") : "  %-9s  ", names[key]);
    for (uint32_t j = 0; j < count; j++) {
        t.n = 0;
        if (!kernel_item(&t, x, f, ps, key, j) || t.failed) continue;
        if (json) {
            wb_s(b, n ? "," : "");
            wb_jstr(b, t.p, t.n);
        } else {
            wb_f(b, "%s%.*s", n ? "; " : "", (int)t.n, t.p);
        }
        n++;
    }
    if (json) wb_s(b, key != KK_DOMAIN ? "]" : n == 0 ? "\"\"" : "");
    else wb_f(b, "%s\n", n == 0 ? "none" : tails[key]);
    wb_free(&t);
}

/* ------------------------------------------------------------------------- */
/* The text and JSON forms.                                                    */

typedef struct ex_ctx {
    const tx_prog *P;
    const tx_module *x;
    const si_func *f;
    const char *sname, *effect;
} ex_ctx;

/* One function in the text form. */
static void explain_text(wb_buf *b, const ex_ctx *c)
{
    static const int order[] = {KK_DOMAIN, KK_SHAPES, KK_ALIASING, KK_READS, KK_WRITES, KK_PUBLISH};
    const tx_module *x = c->x;
    const si_func *f = c->f;
    ex_param ps[64];
    uint32_t np = params_of(x, f, ps, 64), sites = 0, excluded = 0, prints = 0;
    size_t idw = 0, posw = 0, opw = 0;
    int rlen = 0;
    const char *result = result_of(x, f, &rlen);
    char id[160];

    wb_s(b, kind_word(f));
    if (named(f)) wb_f(b, " %.*s", (int)f->name.len, text_of(x->m, f->name));
    else if (f->kind == FN_TEST) wb_f(b, " %lld", (long long)f->test);
    wb_f(b, "   %s:%u   %s\n", x->m->file, f->line, c->effect);
    if (named(f)) {
        wb_f(b, "  signature  (");
        for (uint32_t i = 0; i < np; i++) {
            wb_f(b, "%s%s %.*s: %.*s", i ? ", " : "", ps[i].direction, ps[i].name_len, ps[i].name, ps[i].type_len,
                 ps[i].type);
        }
        wb_s(b, ")");
        if (result != NULL) wb_f(b, " -> %.*s", rlen, result);
        wb_s(b, "\n");
    }
    for (size_t k = 0; f->kind == FN_KERNEL && k < sizeof order / sizeof order[0]; k++)
        kernel_key(b, x, f, ps, np, order[k], 0);
    for (uint32_t i = f->first; i < f->first + f->count; i++) {
        const tx_node *d = &x->nodes[i];
        if (d->site == 0) continue;
        if (word_is(d->w, d->wlen, "print.begin")) {
            prints++;
            continue;
        }
        ex_site e = classify(c->P, x, f, d);
        size_t k = (size_t)snprintf(id, sizeof id, "%s#%u", c->sname, d->site);
        idw = k > idw ? k : idw;
        posw = text_width(x, e.s) > posw ? text_width(x, e.s) : posw;
        opw = e.op_len > opw ? e.op_len : opw;
        sites++;
        excluded += e.why != NULL;
    }
    wb_f(b, "  fault sites (%u): %u possible, %u statically excluded\n", sites, sites - excluded, excluded);
    for (uint32_t i = f->first; i < f->first + f->count; i++) {
        const tx_node *d = &x->nodes[i];
        if (d->site == 0 || word_is(d->w, d->wlen, "print.begin")) continue;
        ex_site e = classify(c->P, x, f, d);
        int k = snprintf(id, sizeof id, "%s#%u", c->sname, d->site);
        wb_f(b, "    %s%*s  %s:%u:%u", id, (int)(idw - (size_t)k), "", x->m->path, e.s->line, e.s->col);
        wb_f(b, "%*s  %.*s%*s  ", (int)(posw - text_width(x, e.s)), "", (int)e.op_len, e.op,
             (int)(opw - e.op_len), "");
        if (e.why != NULL) {
            wb_f(b, "excluded: %s\n", e.why);
        } else {
            put_codes(b, e.codes, 0);
            wb_s(b, e.codes != 0 ? " possible\n" : "possible\n");
        }
    }
    if (sites > excluded) {
        wb_s(b, "  hint       not analyzed: excluding a possible site needs range analysis, which no component "
                "performs yet\n");
    }
    if (prints > 0) {
        wb_f(b, "  prints     %u statement%s:", prints, prints == 1u ? "" : "s");
        for (uint32_t i = f->first; i < f->first + f->count; i++) {
            if (word_is(x->nodes[i].w, x->nodes[i].wlen, "print.begin")) wb_f(b, " %s#%u", c->sname, x->nodes[i].site);
        }
        wb_s(b, "; output the host cannot take ends the entry with a host error, not a fault (SPEC-01 "
                "IM-105)\n");
    }
    (void)reductions(b, x, f, c->sname, 0);
    wb_s(b, "  fuel       fuel-v1 charge points (SPEC-01 IM-139, SPEC-09 SIR-07)");
    int any = 0;
    for (uint32_t i = f->first; i < f->first + f->count; i++) {
        const tx_node *d = &x->nodes[i];
        int call = word_is(d->w, d->wlen, "call") && d->site != 0;
        int dispatch = word_is(d->w, d->wlen, "dispatch") && d->site != 0;
        int entry = word_is(d->w, d->wlen, "dispatch.entry") && d->site != 0;
        if (!word_is(d->w, d->wlen, "fuel.charge") && !call && !dispatch && !entry) continue;
        int k = snprintf(id, sizeof id, "%s#%u", c->sname, d->site);
        wb_f(b, "\n    %s%*s  ", id, (int)(idw - (size_t)k), "");
        if (call || dispatch) {
            wb_s(b, call ? "call.enter      1 per call of " : "dispatch        1 per work-item of ");
            put_callee(b, d, 0);
            wb_s(b, call ? "" : ", before any runs (SPEC-02 X-8)");
        } else if (entry) {
            wb_s(b, "dispatch.entry  1 per work-item, when the host dispatches this kernel (SPEC-02 X-8)");
        } else {
            int64_t loop = loop_of(x, f, i);
            wb_f(b, "fuel.charge %-3lld ", (long long)last_int(d));
            if (loop >= 0) wb_f(b, "each iteration of loop L%lld", (long long)loop);
            else wb_s(b, "at the start of the body");
        }
        any = 1;
    }
    wb_s(b, any ? "\n" : ": none in this body\n");
    wb_s(b, "  lowering\n");
    wb_s(b, "    cpu-sir-interp    supported   the workbench evaluator runs this text (SPEC-09 BACK-01a)\n"
            "    cpu-c17 (ref-c)   supported   cintc emits C17 from the same SIR (SPEC-09 BACK-03)\n"
            "  sir\n");
    const char *sir;
    size_t len;
    sir_text(x, f, &sir, &len);
    wb_put(b, sir, len);
}

/* One function as a CINT-EXPLAIN-1 object, keys in byte order. */
static void explain_json(wb_buf *b, const ex_ctx *c)
{
    const tx_module *x = c->x;
    const si_func *f = c->f;
    const char *sname = c->sname;
    ex_param ps[64];
    uint32_t np = params_of(x, f, ps, 64);
    int rlen = 0, first = 1, kernel = f->kind == FN_KERNEL;
    const char *result = result_of(x, f, &rlen);
    const char *sir;
    size_t len;

    wb_s(b, "{");
    if (kernel) {
        kernel_key(b, x, f, ps, np, KK_ALIASING, 1);
        wb_s(b, ",");
        kernel_key(b, x, f, ps, np, KK_DOMAIN, 1);
        wb_s(b, ",");
    }
    wb_f(b, "\"effects\":\"%s\",\"fault_sites\":[", c->effect);
    for (uint32_t i = f->first; i < f->first + f->count; i++) {
        const tx_node *d = &x->nodes[i];
        if (d->site == 0 || word_is(d->w, d->wlen, "print.begin")) continue;
        ex_site e = classify(c->P, x, f, d);
        wb_f(b, "%s{\"codes\":[", first ? "" : ",");
        put_codes(b, e.codes, 1);
        wb_f(b, "],\"id\":\"%s#%u\",\"operation\":", sname, d->site);
        wb_jstr(b, e.op, e.op_len);
        wb_f(b, ",\"position\":{\"column\":%u,\"file\":", e.s->col);
        wb_jstr(b, x->m->path, strlen(x->m->path));
        wb_f(b, ",\"line\":%u},\"reason\":", e.s->line);
        wb_jstr(b, e.why != NULL ? e.why : "not analyzed", strlen(e.why != NULL ? e.why : "not analyzed"));
        wb_f(b, ",\"status\":\"%s\"}", e.why != NULL ? "excluded" : "possible");
        first = 0;
    }
    wb_s(b, "],\"fuel\":[");
    first = 1;
    for (uint32_t i = f->first; i < f->first + f->count; i++) {
        const tx_node *d = &x->nodes[i];
        if ((word_is(d->w, d->wlen, "call") || word_is(d->w, d->wlen, "dispatch")) && d->site != 0) {
            wb_f(b, "%s{%scallee\":", first ? "" : ",", d->wlen == 4u ? "\"amount\":1,\"" : "\"");
            put_callee(b, d, 1);
            wb_f(b, ",\"charge\":\"%s\",\"id\":\"%s#%u\"%s}", d->wlen == 4u ? "call" : "dispatch", sname, d->site,
                 d->wlen == 4u ? "" : ",\"per\":\"work-item\"");
        } else if (word_is(d->w, d->wlen, "dispatch.entry") && d->site != 0) {
            wb_f(b, "%s{\"charge\":\"dispatch.entry\",\"id\":\"%s#%u\",\"per\":\"work-item\"}", first ? "" : ",",
                 sname, d->site);
        } else if (word_is(d->w, d->wlen, "fuel.charge")) {
            int64_t loop = loop_of(x, f, i);
            wb_f(b, "%s{\"amount\":%lld,\"charge\":\"%s\",\"id\":\"%s#%u\"", first ? "" : ",",
                 (long long)last_int(d), loop >= 0 ? "iteration" : "start", sname, d->site);
            if (loop >= 0) wb_f(b, ",\"loop\":\"L%lld\"", (long long)loop);
            wb_s(b, "}");
        } else {
            continue;
        }
        first = 0;
    }
    wb_f(b, "],\"kind\":\"%s\",\"lowering\":[{\"backend\":\"cpu-c17\",\"profile\":\"ref-c\",\"status\":\"supported\"},"
            "{\"backend\":\"cpu-sir-interp\",\"status\":\"supported\"}]", kind_word(f));
    if (named(f)) {
        wb_s(b, ",\"name\":");
        wb_jstr(b, text_of(x->m, f->name), f->name.len);
        wb_s(b, ",\"parameters\":[");
        for (uint32_t i = 0; i < np; i++) {
            wb_f(b, "%s{\"direction\":\"%s\",\"name\":", i ? "," : "", ps[i].direction);
            wb_jstr(b, ps[i].name, (size_t)ps[i].name_len);
            wb_s(b, ",\"type\":");
            wb_jstr(b, ps[i].type, (size_t)ps[i].type_len);
            wb_s(b, "}");
        }
        wb_s(b, "]");
    }
    wb_s(b, ",\"prints\":[");
    first = 1;
    for (uint32_t i = f->first; i < f->first + f->count; i++) {
        if (word_is(x->nodes[i].w, x->nodes[i].wlen, "print.begin")) {
            wb_f(b, "%s\"%s#%u\"", first ? "" : ",", sname, x->nodes[i].site);
            first = 0;
        }
    }
    wb_s(b, "]");
    if (kernel) {
        wb_s(b, ",");
        kernel_key(b, x, f, ps, np, KK_PUBLISH, 1);
        wb_s(b, ",");
        kernel_key(b, x, f, ps, np, KK_READS, 1);
    }
    size_t at = b->n;
    wb_s(b, ",\"reductions\":[");
    if (reductions(b, x, f, sname, 1) == 0) {
        b->n = b->failed ? b->n : at;   /* no reductions: no key */
    } else {
        wb_s(b, "]");
    }
    if (result != NULL) {
        wb_s(b, ",\"result\":");
        wb_jstr(b, result, (size_t)rlen);
    }
    if (kernel) {
        wb_s(b, ",");
        kernel_key(b, x, f, ps, np, KK_SHAPES, 1);
    }
    sir_text(x, f, &sir, &len);
    wb_s(b, ",\"sir\":");
    wb_jstr(b, sir, len);
    wb_s(b, ",\"sir_position\":{\"file\":");
    wb_jstr(b, x->m->file, strlen(x->m->file));
    wb_f(b, ",\"line\":%u}", f->line);
    if (f->kind == FN_TEST) wb_f(b, ",\"test\":%lld", (long long)f->test);
    if (kernel) {
        wb_s(b, ",");
        kernel_key(b, x, f, ps, np, KK_WRITES, 1);
    }
    wb_s(b, "}");
}

/* The compile budgets of a build target (SPEC-09 CINTC-09): k times a phase's input bytes plus c.
 * Source sizes come from the caller; a negative size is one it could not read. */
static int64_t budget(int64_t bytes)
{
    return bytes >= 0 && bytes <= (INT64_MAX - CINT_PHASE_FUEL_C) / CINT_PHASE_FUEL_K
               ? CINT_PHASE_FUEL_K * bytes + CINT_PHASE_FUEL_C : -1;
}

#define MEMORY "K x source bytes + 1 MiB + 80 B x program declarations per module (SPEC-09 CINTC-02); " \
               "K is measured by tools/cint_measure.py"

static void build_text(wb_buf *b, const tx_prog *P, const int64_t *bytes)
{
    const cint_sir_program *p = P->p;
    int64_t total = 0;
    size_t w = strlen("plan");
    wb_f(b, "program   %u module%s, %u function%s, %u nodes\n", p->nmod, p->nmod == 1u ? "" : "s", P->funcs,
         P->funcs == 1u ? "" : "s", P->nodes);
    wb_f(b, "compile fuel   per phase: k x input bytes + c, k = %lld, c = %lld (rt/cint_bridge.h; SPEC-09 "
            "CINTC-09)\n", (long long)CINT_PHASE_FUEL_K, (long long)CINT_PHASE_FUEL_C);
    for (uint32_t k = 0; k < p->nmod; k++) {
        total = total >= 0 && bytes != NULL && bytes[k] >= 0 ? total + bytes[k] : -1;
        w = strlen(p->mods[k].path) > w ? strlen(p->mods[k].path) : w;
    }
    if (budget(total) >= 0) {
        wb_f(b, "  %-*s   %lld bytes in all modules   %lld units\n", (int)w, "plan", (long long)total,
             (long long)budget(total));
    } else {
        wb_f(b, "  %-*s   a source could not be read\n", (int)w, "plan");
    }
    for (uint32_t k = 0; k < p->nmod; k++) {
        int64_t n = bytes != NULL ? bytes[k] : -1;
        if (budget(n) >= 0) {
            wb_f(b, "  %-*s   %lld bytes   %lld units for each of compile, measure, and emit\n", (int)w,
                 p->mods[k].path, (long long)n, (long long)budget(n));
        } else {
            wb_f(b, "  %-*s   the source could not be read\n", (int)w, p->mods[k].path);
        }
    }
    wb_s(b, "memory   " MEMORY "\n");
}

static void build_json(wb_buf *b, const tx_prog *P, const int64_t *bytes)
{
    const cint_sir_program *p = P->p;
    int64_t total = 0;
    wb_f(b, "{\"compile_fuel\":{\"c\":%lld,\"k\":%lld,\"modules\":[", (long long)CINT_PHASE_FUEL_C,
         (long long)CINT_PHASE_FUEL_K);
    for (uint32_t k = 0; k < p->nmod; k++) {
        int64_t n = bytes != NULL ? bytes[k] : -1;
        total = total >= 0 && n >= 0 ? total + n : -1;
        wb_f(b, "%s{", k ? "," : "");
        if (budget(n) >= 0) wb_f(b, "\"bytes\":%lld,", (long long)n);
        wb_s(b, "\"module\":");
        wb_jstr(b, p->mods[k].path, strlen(p->mods[k].path));
        if (budget(n) >= 0) wb_f(b, ",\"units\":%lld", (long long)budget(n));
        wb_s(b, "}");
    }
    wb_s(b, "]");
    if (budget(total) >= 0)
        wb_f(b, ",\"plan\":{\"bytes\":%lld,\"units\":%lld}", (long long)total, (long long)budget(total));
    wb_s(b, "},\"memory\":");
    wb_jstr(b, MEMORY, strlen(MEMORY));
    wb_f(b, ",\"program\":{\"functions\":%u,\"modules\":%u,\"nodes\":%u}}", P->funcs, p->nmod, P->nodes);
}

int wb_explain(wb_buf *b, const cint_sir_program *p, const char *module, const char *name, const int64_t *bytes,
               int json, wb_buf *err)
{
    tx_prog P;
    uint8_t rev[32];
    char sname[160];
    uint32_t shown = 0, at = p->nmod;
    for (uint32_t k = 0; k < p->nmod && at == p->nmod; k++) at = strcmp(p->mods[k].path, module) == 0 ? k : at;
    if (at == p->nmod) {
        wb_f(err, "the program has no module %s", module);
        return -1;
    }
    if (tx_open(&P, p) != 0) {
        tx_close(&P);
        return -1;   /* out of memory: err stays empty */
    }
    const tx_module *x = &P.mods[at];
    for (uint32_t i = 0; name != NULL && i < x->m->nfunc; i++) shown += named_as(x->m, &x->m->funcs[i], name) != 0;
    if (name != NULL && shown == 0) {
        wb_f(err, "%s has no function %s", module, name);
        tx_close(&P);
        return -1;
    }
    ex_ctx c = {&P, x, NULL, sname, NULL};
    cint_sir_revision(p, rev);
    if (json) {
        if (name == NULL) {
            wb_s(b, "{\"build\":");
            build_json(b, &P, bytes);
            wb_s(b, ",");
        } else {
            wb_s(b, "{");
        }
        wb_s(b, "\"functions\":[");
    } else {
        wb_f(b, "module %s   revision ", module);
        wb_hex(b, rev, sizeof rev);
        wb_s(b, "\n");
        if (name == NULL) build_text(b, &P, bytes);
    }
    shown = 0;
    for (uint32_t i = 0; i < x->m->nfunc; i++) {
        c.f = &x->m->funcs[i];
        if (name != NULL && !named_as(x->m, c.f, name)) continue;
        site_name(x->m, c.f, sname, sizeof sname);
        c.effect = P.observe[P.base[at] + i] ? "observe" : "pure";
        if (json) {
            wb_s(b, shown ? "," : "");
            explain_json(b, &c);
        } else {
            wb_s(b, "\n");
            explain_text(b, &c);
        }
        shown++;
    }
    if (json) {
        wb_s(b, "],\"module\":");
        wb_jstr(b, module, strlen(module));
        wb_s(b, ",\"revision\":\"");
        wb_hex(b, rev, sizeof rev);
        wb_s(b, "\",\"schema\":\"CINT-EXPLAIN-1\"}\n");
    }
    tx_close(&P);
    return b->failed ? -1 : 0;
}

/* Function `name` of module `module` (not a kernel), with its module's index in *at. */
static const si_func *function_of(const tx_prog *P, const char *module, const char *name, uint32_t *at)
{
    for (uint32_t k = 0; k < P->p->nmod; k++) {
        const si_module *m = &P->p->mods[k];
        for (uint32_t i = 0; strcmp(m->path, module) == 0 && i < m->nfunc; i++) {
            if (m->funcs[i].kind == FN_FUNC && named_as(m, &m->funcs[i], name)) {
                at[0] = k;
                at[1] = i;
                return &m->funcs[i];
            }
        }
    }
    return NULL;
}

int wb_signature(wb_buf *b, const cint_sir_program *p, const char *module, const char *name)
{
    tx_prog P;
    uint32_t at[2] = {0, 0};
    ex_param ps[64];
    int rlen = 0, st = -1;
    const si_func *f = tx_open(&P, p) == 0 ? function_of(&P, module, name, at) : NULL;
    if (f != NULL) {
        uint32_t np = params_of(&P.mods[at[0]], f, ps, 64);
        const char *result = result_of(&P.mods[at[0]], f, &rlen);
        wb_s(b, "(");
        for (uint32_t i = 0; i < np; i++) {
            wb_f(b, "%s%s%s%.*s", i ? ", " : "", ps[i].scalar ? "" : ps[i].direction, ps[i].scalar ? "" : " ",
                 ps[i].type_len, ps[i].type);
        }
        wb_f(b, ") -> %.*s", result != NULL ? rlen : 4, result != NULL ? result : "void");
        st = 0;
    }
    tx_close(&P);
    return st;
}

int wb_effect(const cint_sir_program *p, const char *module, const char *name)
{
    tx_prog P;
    uint32_t at[2] = {0, 0};
    int effect = tx_open(&P, p) == 0 && function_of(&P, module, name, at) != NULL ? P.observe[P.base[at[0]] + at[1]]
                                                                                 : -1;
    tx_close(&P);
    return effect;
}

void wb_site_id(const cint_sir_program *p, cint_site at, char *out, size_t cap)
{
    out[0] = '\0';
    const si_module *m = p != NULL && at.module < p->nmod && at.index != 0 ? &p->mods[at.module] : NULL;
    for (uint32_t k = 0; m != NULL && k < m->nfunc; k++) {
        const si_func *f = &m->funcs[k];
        for (uint32_t i = f->first; i < f->first + f->count; i++) {
            if (m->nodes[i].site != at.index) continue;
            site_name(m, f, out, cap);
            (void)snprintf(out + strlen(out), cap - strlen(out), "#%u", at.index);
            return;
        }
    }
}
