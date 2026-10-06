/* resolve.c: modules, imports, module-level names, recursion and reachability.
 *
 * - Imports (SPEC-04 LS-225, LS-226, LS-230): every module is checked after the
 *   modules it imports; an import cycle is C3003. Imports are not transitive.
 * - One definition per name (LS-228): a module-level name is declared once and
 *   must not reuse a built-in name (C3001).
 * - No recursion in cint-boot-1 (SPEC-09 5.3, EMIT-30): a cycle in the call
 *   graph is C4040, the seed's number for C-RECURSION (seed/OPEN.md).
 * - Only functions reachable from an exported entry of the root module or from
 *   an observer entry are emitted, so emitted C has no unused static function
 *   (EMIT-26).
 * Every graph walk uses an explicit stack sized from the table it walks.
 */
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "tables.h"

/* Built-in names of SPEC-04 LS-21 and LS-145 that a program must not redefine. */
static const char *const prelude[] = {
    "len", "extent", "size", "lower", "copy", "fill", "equal", "swap", "abs", "uabs", "min", "max",
    "clamp", "div_trunc", "rem_trunc", "div_euclid", "rem_euclid", "divmod", "div_round", "muldiv",
    "mul_full", "isqrt", "isqrt_round", "add_result", "sub_result", "mul_result", "div_result",
    "rem_result", "shl_result", "rotl", "rotr", "mul", "div", "mul_wrap", "mul_sat", "sqrt", "sum",
    "fold_checked", "sum_wrap", "sum_sat", "count", "wrap_bits", "rescale3", "rescale3_rem", "shl3",
    "sign", "tdot", "random", "format", "utf8", "embed", "to_device", "to_host",
    "Bool", "Str", "Round", "T1", "T27", "PT5", "PT4", "Arena", "Pool", "Handle", NULL};

bool is_prelude(const seed_module *m, uint32_t tok)
{
    int i;
    for (i = 0; prelude[i] != NULL; i++) {
        if (tok_is(m, tok, prelude[i])) {
            return true;
        }
    }
    return false;
}

static uint32_t name_hash(const seed_module *m, uint32_t tok)
{
    uint32_t h = 2166136261u, i;
    const uint8_t *s = m->src + m->toks[tok].off;
    for (i = 0; i < m->toks[tok].len; i++) {
        h = (h ^ s[i]) * 16777619u;
    }
    return h;
}

int32_t gsym_find(int32_t mi, const seed_module *nm, uint32_t tok)
{
    const seed_module *m = &S.mods[mi];
    uint32_t h;
    if (m->ghcap == 0) {
        return -1;
    }
    h = name_hash(nm, tok) % (uint32_t)m->ghcap;
    while (m->ghash[h] >= 0) {
        const seed_sym *y = &S.syms[m->ghash[h]];
        if (tok_eq(m, y->tok, nm, tok)) {
            return m->ghash[h];
        }
        h = (h + 1u) % (uint32_t)m->ghcap;
    }
    return -1;
}

int32_t sym_add(int kind, int32_t mi, uint32_t tok, int32_t type, int32_t decl)
{
    seed_sym *y;
    GROW(S.syms, S.nsym, S.cap_sym, "symbol");
    y = &S.syms[S.nsym];
    memset(y, 0, sizeof *y);
    y->kind = (uint8_t)kind;
    y->module = mi;
    y->tok = tok;
    y->type = type;
    y->decl = decl;
    return S.nsym++;
}

static void global_add(int32_t mi, int kind, uint32_t tok, int32_t decl, bool exported)
{
    seed_module *m = &S.mods[mi];
    int32_t y;
    uint32_t h;
    if (gsym_find(mi, m, tok) >= 0 || is_prelude(m, tok)) {
        diag_tok(mi, tok, "C3001", "`%.*s` is already declared", (int)m->toks[tok].len,
                 (const char *)m->src + m->toks[tok].off);
    }
    y = sym_add(kind, mi, tok, 0, decl);
    S.syms[y].exported = exported ? 1u : 0u;
    h = name_hash(m, tok) % (uint32_t)m->ghcap;
    while (m->ghash[h] >= 0) {
        h = (h + 1u) % (uint32_t)m->ghcap;
    }
    m->ghash[h] = y;
}

typedef struct item {
    uint32_t tok;
    int kind;
    int32_t decl;
    bool exported;
} item;

static int item_cmp(const void *a, const void *b)
{
    const item *x = a, *y = b;
    return x->tok < y->tok ? -1 : x->tok > y->tok ? 1 : 0;
}

/* Module-level names in source order, so a duplicate is reported at its second declaration. */
static void module_names(int32_t mi)
{
    seed_module *m = &S.mods[mi];
    int32_t n = m->nimport + m->nconst, i, k = 0;
    item *it;
    for (i = 0; i < S.nstruct; i++) {
        n += S.structs[i].module == mi;
    }
    for (i = 0; i < S.nfunc; i++) {
        n += S.funcs[i].module == mi;
    }
    it = seed_alloc((size_t)(n + 1) * sizeof *it, "name");
    for (i = 0; i < m->nimport; i++) {
        it[k].tok = m->imports[i].alias, it[k].kind = Y_IMPORT, it[k].decl = m->imports[i].module;
        it[k++].exported = false;
    }
    for (i = 0; i < m->nconst; i++) {
        const seed_stmt *s = &m->stmts[m->consts[i]];
        it[k].tok = s->name, it[k].kind = Y_CONST, it[k].decl = m->consts[i], it[k++].exported = s->op != 0u;
    }
    for (i = 0; i < S.nstruct; i++) {
        if (S.structs[i].module == mi) {
            it[k].tok = S.structs[i].tok, it[k].kind = Y_STRUCT, it[k].decl = i;
            it[k++].exported = S.structs[i].exported != 0u;
        }
    }
    for (i = 0; i < S.nfunc; i++) {
        if (S.funcs[i].module == mi) {
            it[k].tok = S.funcs[i].tok, it[k].kind = Y_FUNC, it[k].decl = i;
            it[k++].exported = S.funcs[i].exported != 0u;
        }
    }
    qsort(it, (size_t)k, sizeof *it, item_cmp);
    m->ghcap = 2 * k + 1;
    m->ghash = seed_alloc((size_t)m->ghcap * sizeof *m->ghash, "name");
    for (i = 0; i < m->ghcap; i++) {
        m->ghash[i] = -1;
    }
    for (i = 0; i < k; i++) {
        global_add(mi, it[i].kind, it[i].tok, it[i].decl, it[i].exported);
    }
    free(it);
}

/* Depth-first over imports from the root: post-order is the check order; a
 * back edge is an import cycle. */
void resolve_program(void)
{
    int32_t *stack = seed_alloc((size_t)(S.nmod + 1) * sizeof *stack, "module");
    int32_t *next = seed_alloc((size_t)(S.nmod + 1) * sizeof *next, "module");
    uint8_t *color = seed_alloc((size_t)(S.nmod + 1), "module");
    int32_t sp = 0, norder = 0, i;
    S.check_order = seed_alloc((size_t)(S.nmod + 1) * sizeof *S.check_order, "module");
    stack[sp++] = 0;
    color[0] = 1;
    while (sp > 0) {
        int32_t mi = stack[sp - 1];
        seed_module *m = &S.mods[mi];
        if (next[mi] < m->nimport) {
            const seed_import *im = &m->imports[next[mi]++];
            if (color[im->module] == 1) {
                diag_tok(mi, im->tok, "C3003", "import cycle: this module is imported, directly or "
                         "through other modules, by the module it imports");
            }
            if (color[im->module] == 0) {
                color[im->module] = 1;
                stack[sp++] = im->module;
            }
            continue;
        }
        color[mi] = 2;
        m->order = norder;
        S.check_order[norder++] = mi;
        sp--;
    }
    for (i = 0; i < norder; i++) {
        module_names(S.check_order[i]);
    }
    free(stack);
    free(next);
    free(color);
}

/* The call graph has no cycle (SPEC-09 5.3). Calls are recorded by the checker
 * as pairs (callee, call node) in S.calls. */
static void recursion_check(void)
{
    int32_t *stack = seed_alloc((size_t)(S.nfunc + 1) * sizeof *stack, "function");
    int32_t *next = seed_alloc((size_t)(S.nfunc + 1) * sizeof *next, "function");
    int32_t r;
    for (r = 0; r < S.nfunc; r++) {
        int32_t sp = 0;
        if (S.funcs[r].mark != 0u) {
            continue;
        }
        stack[sp++] = r;
        S.funcs[r].mark = 1;
        while (sp > 0) {
            int32_t f = stack[sp - 1];
            seed_func *fn = &S.funcs[f];
            if (next[f] < fn->ncall) {
                int32_t k = fn->call0 + 2 * next[f]++;
                int32_t g = S.calls[k];
                if (S.funcs[g].mark == 1u) {
                    const seed_module *m = &S.mods[fn->module];
                    diag_tok(fn->module, m->nodes[S.calls[k + 1]].tok, "C4040", "recursion is outside "
                             "cint-boot-1: this call closes a cycle in the call graph (SPEC-09 5.3)");
                }
                if (S.funcs[g].mark == 0u) {
                    S.funcs[g].mark = 1;
                    stack[sp++] = g;
                }
                continue;
            }
            fn->mark = 2;
            sp--;
        }
    }
    free(stack);
    free(next);
}

bool fn_observed(const seed_func *f)
{
    int32_t k;
    for (k = 0; k < f->nparam; k++) {
        if (S.params[f->param0 + k].type >= TY_USER) {
            return false;
        }
    }
    return f->exported && f->nparam <= 16;
}

void reach_program(void)
{
    int32_t *work = seed_alloc((size_t)(S.nfunc + 1) * sizeof *work, "function");
    int32_t n = 0, i;
    recursion_check();
    for (i = 0; i < S.nfunc; i++) {
        if ((S.funcs[i].module == 0 && S.funcs[i].exported) || fn_observed(&S.funcs[i])) {
            S.funcs[i].reach = 1;
            work[n++] = i;
        }
    }
    while (n > 0) {
        const seed_func *f = &S.funcs[work[--n]];
        int32_t k;
        for (k = 0; k < f->ncall; k++) {
            int32_t g = S.calls[f->call0 + 2 * k];
            if (!S.funcs[g].reach) {
                S.funcs[g].reach = 1;
                work[n++] = g;
            }
        }
    }
    free(work);
}
