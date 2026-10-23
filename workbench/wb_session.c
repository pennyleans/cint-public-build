/* wb_session.c: the session of `cint workbench` on cpu-sir-interp (SPEC-06 5; box 14 units 2a,
 * 2b, and 3a).
 *
 * Every entry is compiled by B1 (`cint emit-c`, box 14 default BX14-08) in the scope of the
 * session module, `session.ci`, which holds the declarations entered at the prompt and imports
 * every loaded module (ruling BX14-04 A). A declaration entry adds or replaces its declaration
 * and makes a revision of the session module. A statement or expression entry is compiled as
 * the body of an exported function `wb_entry` added to the session module for that compile
 * only, its text on lines of its own so that its columns are those typed; a variable's
 * initializer runs the same way, in its declaring entry. Box 13's interpreter runs the entry
 * from the SIR text B1 wrote, on the session's one context, which changes to the entry's
 * program first (SPEC-03 H-19b): a module variable whose module, name, and type are unchanged
 * keeps its value, a new one starts at its initializer, and a retyped or removed one is dropped
 * (SPEC-04 LS-224).
 *
 * Unit 2b adds box 13's checkpoints (unit 4). A session checkpoint (wb_store.c) holds the
 * context's CINT-CKPT-1 with the source of every module of the program the context is in, so
 * a restore builds that program again, checks its revision identity, and restores into a new
 * context. The session takes one before each entry, which `:undo` restores (BX14-09). Watches
 * are functions of a program of their own, run after each entry in a context restored from the
 * session's, as `:inspect` runs, so neither leaves a boundary (BX14-08). `:save` writes the
 * object store, then the session manifest last (SPEC-06 5.6). An interrupt caps the running
 * entry's allowance at what it has consumed, so it faults E_FUEL (BX14-10).
 *
 * Unit 3a records the context with box 13's recorder (unit 5), in full mode at entry density
 * (BX14-09): every boundary the session reaches can be reached again by replay. `:goto`
 * restores the state a replay reached, `:trace` lists an entry's counted operations from the
 * trace events of a replay, `:replay` re-executes the entries since a checkpoint and evaluates
 * the watches at each boundary, and `:effects` lists the recorded events that led to the
 * current state. `:save` keeps the recording, and `:open` reads it back.
 *
 * `:backend cpu-c17` runs entries on cpu-c17 through unit 5's libraries (BX14-08), and
 * `:run ... --check B` also runs each call on the other backend B and compares the two at each
 * boundary (SPEC-06 5.2, 5.3). The revision set is built as a library once per revision, and a
 * call runs on a context of the library restored from the session's CINT-CKPT-1 (SPEC-03 H-19).
 * The state it leaves comes back the same way. The recorder runs on cpu-sir-interp, so an entry
 * on cpu-c17 alone is not recorded: the live segment ends before it.
 *
 * A value line carries the `inexact` annotation of SPEC-06 5.2, from the interpreter's trace.
 *
 * Unit 4b reads box 13's module-level arrays from the state's CINT-CKPT-1 for a declaration line,
 * an entry that names an array, `:inspect` with the array formats of unit 4a, `:type`, and `:diff`
 * element by element. An array literal is a constant initializer and stays in the declaration.
 *
 * B1 has no `var` (C9102) and no expression statement (C4012), so the type of an expression
 * entry is found by compiling `return (expr);` in a function of each candidate type, in order:
 * Bool when a comparison or logical operator is outside brackets, the types of the session's
 * variables and functions that the expression names, the targets of its `as`, then I64 and
 * Bool. With no implicit conversions (C2001), only a type that the expression has compiles; a
 * literal-only expression is I64, as a `var` initializer would be (SPEC-04 LS-105). An
 * expression with no value, such as a call of a void function, runs as a statement. Each
 * compile takes about 100 ms with B1 (section 11 of the box 14 note).
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cint_interp.h"
#include "cint_proc.h"
#include "cint_state.h"
#include "sir.h"
#include "wb_array.h"
#include "wb_c17.h"
#include "wb_debug.h"
#include "wb_replay.h"

#define ITEMS_MAX 65536u     /* entries kept for :log, :history, and source excerpts */
#define DECLS_MAX 1024u
#define LOADED_MAX 64u
#define IDENT_MAX 256u
#define CANDIDATES 8u
#define WATCHES_MAX 32u
#define UNITS_MAX (1u + 2u * WATCHES_MAX)
#define BRANCHES_MAX 64u
#define SHOWN_MAX 20u        /* watch changes printed per watch and command (SPEC-06 6.4) */
#define LOG_MAX 1000000u     /* changes a watch keeps for :watch log */
#define NO_DEVICE "waits for a gpu-cuda library: `cint build --lib` builds cpu-c17 only\n"

enum { K_NONE, K_EXPR, K_STMT, K_VAR, K_FUNC, K_OTHER, K_IMPORT, K_SCRIPT };   /* K_SCRIPT is 7 in wb_store.c */

wb_alter_fn wb_check_alter;   /* NULL outside the qualification test (wb_debug.h) */

typedef struct decl {
    char name[IDENT_MAX];
    int kind;
    uint32_t entry, rev;   /* the entry that declared it, and the revision it made */
    char *text;            /* its text in the session module */
    uint32_t line, nlines; /* where the module last written holds it */
} decl;

typedef struct value {
    char *module;
    char name[IDENT_MAX];
    uint32_t tag;
    uint64_t bits;
} value;

typedef struct hist {
    char name[IDENT_MAX];
    uint32_t rev, entry;
} hist;

/* A function the session added to the module written last, its body on lines of its own. */
typedef struct unit {
    uint32_t line, lines;           /* the body */
    uint32_t first_line, first_col; /* an expression's first token there; 0 for a statement */
    char path[WB_PATH_MAX + 1u];    /* "entry N", "inspect", "watch N", "watch N when", or a script's */
} unit;

/* A session checkpoint: named (`:checkpoint`), automatic (`auto-N`), or the point an entry
 * undoes to (no name, number 0, outside the graph). */
typedef struct ckpt {
    char name[WB_NAME_MAX + 1u];
    uint32_t number, branch, ordinal;
    int32_t parent;                 /* the checkpoint before it on its branch, or -1 */
    uint32_t seg, at;               /* its state: boundary `at` of segment seg - 1; seg 0 when none */
    uint8_t record[32], sid[32], nid[32];
} ckpt;

/* A recording segment (unit 3a): one recorder's CINT-REC-1 of the session's context, which
 * starts from the state at boundary `at` of segment `parent`, or from a state no segment
 * reached (parent -1). A boundary is an entry that ran, counted from 1 in its segment. */
typedef struct segment {
    wb_buf rec;                     /* while it records; then the object `id` holds it */
    uint8_t id[32];
    uint64_t boundaries;
    int32_t parent;
    uint32_t at;
    int how;                        /* SEG_*: how its context came to that state */
    int ended;                      /* 1 when `id` holds it complete, -1 when it could not end */
} segment;

/* A segment starts on a new context, after a restore, after :goto, or where a segment that a
 * replay or :save ended left off (SEG_CONT, which goes on with the same state). */
enum { SEG_NEW, SEG_RESTORE, SEG_GOTO, SEG_CONT };

/* Where entry n ran: boundary `at` of segment seg - 1 (seg 0: not under a recorder), in the
 * program whose layout (a session checkpoint record without state) is `layout`. */
typedef struct bound {
    uint32_t seg, at;
    uint8_t layout[32];
} bound;

typedef struct branch {
    char name[WB_NAME_MAX + 1u];
    int32_t base;                   /* the checkpoint it starts from, or -1 */
} branch;

typedef struct change {
    uint32_t ordinal;
    uint8_t len, bytes[9];          /* the tagged value; len 0 when the watch faulted */
} change;

typedef struct watch {
    uint32_t number;
    char *expr, *cond;              /* cond NULL without `when` */
    int sc, stop;
    int known;                      /* `last` holds the value at the last boundary */
    change last;
    change *log;
    uint32_t nlog, caplog, shown, more;
} watch;

typedef struct session {
    const wb_session_options *o;
    char *src, *build, *log;
    char *items[ITEMS_MAX];          /* entry n's text at n - 1 */
    const char *outcome[ITEMS_MAX];
    uint32_t next;                   /* the next entry ordinal */
    decl decls[DECLS_MAX];
    uint32_t ndecl;
    hist *hist;
    uint32_t nhist, caphist;
    char *loaded[LOADED_MAX];        /* module paths under src, in load order */
    uint32_t lrev[LOADED_MAX];       /* the revision of each, from 1 */
    uint32_t nloaded;
    value *vals;                     /* the module state after the last entry */
    uint32_t nval;
    uint32_t rev, rev_max;           /* the session module's revision, and the last one made */
    int64_t fuel, depth;
    cint_sir_program *prog;          /* the program compiled last */
    wb_buf fault;                    /* the last fault report */
    wb_buf written;                  /* the session module written last */
    unit units[UNITS_MAX];           /* the functions added to it */
    uint32_t nunit;
    const char *scratch;             /* the text of a unit not kept, while it compiles */
    char scratch_path[WB_PATH_MAX + 1u];
    int at_line_start;               /* the program's output ended with LF */
    /* the context and the program it is in (unit 2b) */
    cint_ctx *ctx;
    cint_sir_program *live_prog;
    cint_sir_image *live_img;
    wb_record live;                  /* the revision set and layout of live_prog; no name */
    wb_store store;
    ckpt *ck;
    uint32_t nck, capck;
    uint32_t *undo, nundo, capundo;  /* indices of undo points, the latest last */
    int32_t head;                    /* the checkpoint the current state follows on its branch */
    branch br[BRANCHES_MAX];
    uint32_t nbr, cur;
    uint32_t numbered, autos;
    watch w[WATCHES_MAX];
    uint32_t nw, wnext;
    cint_sir_program *watch_prog;    /* the session module with the watches, built at epoch */
    uint64_t epoch, wepoch, built_epoch, built_wepoch;
    uint32_t bad_watches;            /* bits of w[] whose units the last compile failed in */
    int other_errors;
    int dirty, warned;               /* changed since :save or :open; :quit said so */
    /* the recording of the context (unit 3a) */
    cint_recorder *rec;              /* records into seg[nseg - 1], calling live_img's entries */
    segment *seg;
    uint32_t nseg, capseg;
    bound bnd[ITEMS_MAX];            /* entry n's at n - 1 */
    uint32_t tip, tip_at;            /* :open: the saved state's position, as in ckpt */
    /* cpu-c17 (BX14-08) */
    char backend[32];                /* where entries run: cpu-sir-interp or cpu-c17 */
    const char *check;               /* the other backend of :run --check, or NULL */
    char call[56];                   /* within a :run of N calls, ", call K of N", else "" */
    wb_c17 *lib;                     /* the library of revision lib_rev, or NULL */
    uint8_t lib_rev[32];
    int quiet;                       /* prints are not shown */
    wb_buf *tee;                     /* or NULL: prints are also kept here */
} session;

static volatile sig_atomic_t g_stop;  /* an interrupt arrived (BX14-10) */
static cint_ctx *volatile g_entry;    /* the context of the running entry */

static void on_interrupt(int sig)
{
    (void)signal(sig, on_interrupt);
    g_stop = 1;
    if (g_entry != NULL) cint_ctx_interrupt(g_entry);
}

static const char *const SCALAR_NAMES[] = {"", "Bool", "I8", "I16", "I32", "I64", "U8", "U16", "U32", "U64"};

static unsigned tag_width(uint32_t tag)
{
    return tag == CINT_TAG_BOOL ? 1u : 1u << ((tag & 15u) - 1u);
}

static void say(session *s, const char *text, size_t n)
{
    if (!s->at_line_start) {
        fputc('\n', stdout);   /* the program's output ended without LF */
        s->at_line_start = 1;
    }
    (void)fwrite(text, 1, n, stdout);
    fflush(stdout);
}

static void sayf(session *s, wb_buf *b)
{
    if (b->failed) say(s, "workbench: out of memory\n", 25u);
    else if (b->n != 0) say(s, b->p, b->n);
    b->n = 0;
}

/* The array p of *cap elements of `size` bytes, with room for element n: p, p grown, or NULL when
 * memory runs out (p then stays as it was). */
static void *room(void *p, uint32_t n, uint32_t *cap, size_t size)
{
    if (n < *cap) return p;
    void *q = realloc(p, (*cap * 2u + 16u) * size);
    *cap = q != NULL ? *cap * 2u + 16u : *cap;
    return q;
}

static void tval_of(wb_tval *v, uint32_t tag, uint64_t bits)
{
    unsigned w = tag_width(tag);
    v->len = (uint16_t)(1u + w);
    v->bytes[0] = (uint8_t)tag;
    for (unsigned i = 0; i < w; i++) v->bytes[1u + i] = (uint8_t)(bits >> (8u * i));
}

/* ---- Tokens, enough to classify an entry (SPEC-04 LS-303) and to name its types. */

enum { T_END, T_ID, T_NUM, T_STR, T_PUNCT };

typedef struct tok {
    const char *p;
    size_t n;
    int kind;
} tok;

static int id_char(int c, int first)
{
    return c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (!first && c >= '0' && c <= '9');
}

/* Blanks and comments (block comments nest, SPEC-04 LS-14). */
static const char *skip(const char *p)
{
    for (;;) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            p++;
        } else if (p[0] == '/' && p[1] == '/') {
            while (*p != '\0' && *p != '\n') p++;
        } else if (p[0] == '/' && p[1] == '*') {
            int depth = 0;
            do {
                if (*p == '\0') return p;
                if (p[0] == '/' && p[1] == '*') {
                    depth++;
                    p += 2;
                } else if (p[0] == '*' && p[1] == '/') {
                    depth--;
                    p += 2;
                } else {
                    p++;
                }
            } while (depth > 0);
        } else {
            return p;
        }
    }
}

/* t from its first character that skip() keeps, with its trailing blanks cut. */
static char *trim(char *t)
{
    t = (char *)skip(t);
    for (char *z = t + strlen(t); z > t && (z[-1] == ' ' || z[-1] == '\t' || z[-1] == '\n' || z[-1] == '\r'); z--)
        z[-1] = '\0';
    return t;
}

static const char *lex(const char *p, tok *t)
{
    p = skip(p);
    t->p = p;
    if (*p == '\0') {
        t->kind = T_END;
        t->n = 0;
        return p;
    }
    const char *q = p + 1;
    if (id_char((unsigned char)*p, 1)) {
        while (id_char((unsigned char)*q, 0)) q++;
        t->kind = T_ID;
    } else if (*p >= '0' && *p <= '9') {
        while (id_char((unsigned char)*q, 0)) q++;
        t->kind = T_NUM;
    } else if (*p == '"' || *p == '\'') {
        while (*q != '\0' && *q != *p) q += q[0] == '\\' && q[1] != '\0' ? 2 : 1;
        q += *q != '\0';
        t->kind = T_STR;
    } else {
        t->kind = T_PUNCT;
    }
    t->n = (size_t)(q - p);
    return q;
}

static int is(const tok *t, const char *s)
{
    return t->n == strlen(s) && memcmp(t->p, s, t->n) == 0;
}

/* 1 when the text needs another line: an open bracket or block comment. A string ends at its
 * line's end; the compiler reports one left open. */
static int incomplete(const char *text)
{
    int depth = 0, comment = 0;
    for (const char *p = text; *p != '\0'; p++) {
        if (comment > 0 || (p[0] == '/' && p[1] == '*')) {   /* block comments nest (LS-14) */
            comment += p[0] == '/' && p[1] == '*';
            comment -= comment > 0 && p[0] == '*' && p[1] == '/';
            p += (p[0] == '/' && p[1] == '*') || (p[0] == '*' && p[1] == '/');
        } else if (p[0] == '/' && p[1] == '/') {
            p += strcspn(p, "\n") - (p[strcspn(p, "\n")] == '\0');
        } else if (*p == '"' || *p == '\'') {
            const char q = *p;
            for (p++; *p != '\0' && *p != q && *p != '\n'; p += p[0] == '\\' && p[1] != '\0' && p[1] != '\n' ? 2 : 1) {
            }
            if (*p == '\0') break;
        } else {
            depth += *p == '(' || *p == '[' || *p == '{';
            depth -= *p == ')' || *p == ']' || *p == '}';
        }
    }
    return depth > 0 || comment > 0;
}

/* The kind of an entry, and for a declaration its name and where the name starts. A
 * declaration begins with a type followed by a name, or with a declaration keyword (LS-303). */
static int classify(const char *text, char *name, const char **name_at, const char **init)
{
    static const char *const KEYWORDS[] = {"struct", "enum", "error", "const", "arena", "pool", "kernel", "schedule",
                                           "type"};
    tok t, u;
    const char *p = lex(text, &t);
    *name_at = *init = NULL;
    name[0] = '\0';
    if (t.kind == T_END) return K_NONE;
    if (is(&t, "import")) return K_IMPORT;
    if (is(&t, "export")) p = lex(p, &t);
    for (size_t k = 0; k < sizeof KEYWORDS / sizeof KEYWORDS[0]; k++) {
        if (is(&t, KEYWORDS[k])) {
            /* the name: the identifier before `=`, `{`, `(`, `;` or `:`, or the last one seen outside
             * brackets, so a kernel's shape variables and a set's or enum's type are not its name */
            const char *at = NULL;
            int depth = 0;
            for (p = lex(p, &u); u.kind != T_END && !(u.kind == T_PUNCT && strchr("={(;:", u.p[0])); p = lex(p, &u)) {
                depth += u.kind == T_PUNCT && u.p[0] == '[';
                depth -= u.kind == T_PUNCT && u.p[0] == ']';
                if (u.kind == T_ID && depth == 0 && u.n < IDENT_MAX) {
                    at = u.p;
                    memcpy(name, u.p, u.n);
                    name[u.n] = '\0';
                }
            }
            *name_at = at;
            return at != NULL ? K_OTHER : K_STMT;
        }
    }
    /* a type: a dotted name, then a (..) group and any [..] groups, then the declared name */
    if (t.kind != T_ID) goto plain;
    p = lex(p, &u);
    for (int bang = 0; bang < 2; bang++) {   /* an error union, Set!T, names two (box 12) */
        while (u.kind == T_PUNCT && u.p[0] == '.') {
            p = lex(p, &u);
            if (u.kind != T_ID) goto plain;
            p = lex(p, &u);
        }
        if (bang == 1 || u.kind != T_PUNCT || u.p[0] != '!') break;
        p = lex(p, &u);
        if (u.kind != T_ID) goto plain;
        p = lex(p, &u);
    }
    if (u.kind == T_PUNCT && u.p[0] == '(') {   /* Handle(T), Arena(T), Pool(T) */
        int depth = 1;
        while (depth > 0) {
            p = lex(p, &u);
            if (u.kind == T_END) goto plain;
            depth += u.kind == T_PUNCT && u.p[0] == '(';
            depth -= u.kind == T_PUNCT && u.p[0] == ')';
        }
        p = lex(p, &u);
    }
    while (u.kind == T_PUNCT && u.p[0] == '[') {
        int depth = 1;
        while (depth > 0) {
            p = lex(p, &u);
            if (u.kind == T_END) goto plain;
            depth += u.kind == T_PUNCT && u.p[0] == '[';
            depth -= u.kind == T_PUNCT && u.p[0] == ']';
        }
        p = lex(p, &u);
    }
    if (u.kind != T_ID || u.n >= IDENT_MAX) goto plain;
    tok v;
    const char *after = lex(p, &v);
    if (v.kind != T_PUNCT || strchr("=;([", v.p[0]) == NULL || (v.p[0] == '=' && after[0] == '=')) goto plain;
    memcpy(name, u.p, u.n);
    name[u.n] = '\0';
    *name_at = u.p;
    if (v.p[0] == '=') *init = v.p;
    return v.p[0] == '(' || v.p[0] == '[' ? K_FUNC : K_VAR;
plain: {
        /* a statement ends with `;` or `}`; anything else is an expression */
        const char *last = NULL;
        for (p = lex(text, &t); t.kind != T_END; p = lex(p, &t)) last = t.p;
        return last != NULL && (*last == ';' || *last == '}') ? K_STMT : K_EXPR;
    }
}

static uint32_t lines_in(const char *s)
{
    uint32_t n = 1;
    for (; *s; s++) n += *s == '\n';
    return n;
}

/* ------------------------------------------ The session module and its builds. */

static char *dotted_of(const char *path)
{
    char *d = wb_copy(path);
    if (d != NULL) {
        d[strlen(d) - 3u] = '\0';   /* ".ci" */
        for (char *c = d; *c; c++) *c = *c == '/' ? '.' : *c;
    }
    return d;
}

/* The text of a script (LS-218) with the top-level items of the other part blanked, so its lines
 * and columns stay: with `run`, its statements and the variables of its implicit entry, which
 * stay locals of that entry; else its declarations, arenas, and pools, which the session module
 * holds (LS-219). Imports are blanked in both: session.ci imports the modules the session loaded.
 * *stmts counts the statements. NULL when memory runs out. */
static char *script_part(const char *text, int run, int *stmts)
{
    char *t = wb_copy(text), *item = malloc(strlen(text) + 1u), name[IDENT_MAX];
    const char *at, *init, *p = text, *q;
    tok k, n, w;
    for (; t != NULL && item != NULL && *skip(p) != '\0'; p = q) {
        int depth = 0;   /* an item ends with `;`, or with a `}` that `else` or `;` does not follow */
        for (q = lex(p, &k); k.kind != T_END; q = lex(q, &k)) {
            depth += k.kind == T_PUNCT && strchr("([{", *k.p) != NULL;
            depth -= k.kind == T_PUNCT && strchr(")]}", *k.p) != NULL;
            if (depth == 0 && k.kind == T_PUNCT &&
                (*k.p == ';' || (*k.p == '}' && (lex(q, &n), !is(&n, "else") && !is(&n, ";")))))
                break;
        }
        memcpy(item, p, (size_t)(q - p));
        item[q - p] = '\0';
        (void)lex(item, &w);
        int c = is(&w, "test") ? K_OTHER : classify(item, name, &at, &init);
        int local = c == K_STMT || c == K_EXPR || (c == K_VAR && !is(&w, "Arena") && !is(&w, "Pool"));
        *stmts += c == K_STMT || c == K_EXPR;
        for (size_t i = (size_t)(p - text); (c == K_IMPORT || local != run) && i < (size_t)(q - text); i++)
            t[i] = t[i] == '\n' ? '\n' : ' ';
    }
    if (item == NULL) {   /* memory ran out */
        free(t);
        t = NULL;
    }
    free(item);
    return t;
}

/* Appends `export RTYPE FN() { ... }` with body on lines of its own (inside `return ( ... );`
 * for a value), and records it as a unit at path. */
static void add_unit(session *s, wb_buf *b, uint32_t *line, const char *fn, const char *rtype, const char *body,
                     const char *path)
{
    int expr = strcmp(rtype, "void") != 0;
    unit *u = &s->units[s->nunit++];
    memset(u, 0, sizeof *u);
    wb_f(b, "export %s %s() {\n%s", rtype, fn, expr ? "    return (\n" : "");
    u->line = *line + 1u + (uint32_t)expr;
    u->lines = lines_in(body);
    if (expr) {   /* the wrapper's `return (` line stands for the expression's first token */
        const char *f = skip(body), *bol = body;
        u->first_line = 1;
        for (const char *c = body; c < f; c++) {
            u->first_line += *c == '\n';
            bol = *c == '\n' ? c + 1 : bol;
        }
        u->first_col = (uint32_t)(f - bol) + 1u;
    }
    (void)snprintf(u->path, sizeof u->path, "%s", path);
    wb_f(b, "%s\n%s}\n", body, expr ? "    );\n" : "");
    *line = u->line + u->lines + (uint32_t)expr + 1u;   /* after `);` and `}` */
}

/* Writes session.ci: the imports, the declarations (decl `at` replaced by `pending`, or
 * pending appended when `at` is ndecl), the entry unit `wb_entry` returning `rtype` with
 * `body` (none when body is NULL), and with `watches` a function per watch and condition.
 * Lines of each part are recorded for positions. */
static int write_module(session *s, const decl *pending, uint32_t at, const char *rtype, const char *body,
                        const char *body_path, int watches)
{
    wb_buf b = {0};
    uint32_t line = 1;
    for (uint32_t i = 0; i < s->nloaded; i++) {
        char *d = dotted_of(s->loaded[i]);
        wb_f(&b, "import %s;\n", d != NULL ? d : "");
        free(d);
        line++;
    }
    for (uint32_t i = 0; i <= s->ndecl; i++) {
        decl *d = i == at && pending != NULL ? (decl *)pending : i < s->ndecl ? &s->decls[i] : NULL;
        if (d == NULL) continue;
        int n = 0;
        char *part = d->kind == K_SCRIPT ? script_part(d->text, 0, &n) : NULL;
        d->line = line;
        d->nlines = lines_in(d->text);
        b.failed |= d->kind == K_SCRIPT && part == NULL;
        wb_f(&b, "%s\n", part != NULL ? part : d->text);
        free(part);
        line += d->nlines;
        if (i == at && pending != NULL && at < s->ndecl) {
            s->decls[i].line = 0;   /* replaced: not in this module */
        }
    }
    s->nunit = 0;
    if (body != NULL) add_unit(s, &b, &line, "wb_entry", rtype, body, body_path);
    for (uint32_t k = 0; watches && k < s->nw; k++) {
        char fn[32], path[32];
        (void)snprintf(fn, sizeof fn, "wb_watch_%u", (unsigned)s->w[k].number);
        (void)snprintf(path, sizeof path, "watch %u", (unsigned)s->w[k].number);
        add_unit(s, &b, &line, fn, SCALAR_NAMES[s->w[k].sc], s->w[k].expr, path);
        if (s->w[k].cond != NULL) {
            (void)snprintf(fn, sizeof fn, "wb_when_%u", (unsigned)s->w[k].number);
            (void)snprintf(path, sizeof path, "watch %u when", (unsigned)s->w[k].number);
            add_unit(s, &b, &line, fn, "Bool", s->w[k].cond, path);
        }
    }
    char *path = wb_join(s->src, "session.ci");
    int r = b.failed || path == NULL ? -1 : wb_write_file(path, b.p, b.n);
    free(path);
    wb_free(&s->written);
    s->written = b;
    return r;
}

/* Where a position of session.ci came from: the entry that typed that line ("entry N" and the
 * line within it), or session.ci itself for the lines the session wrote; the `return (` and `);`
 * lines around an expression stand for its first token. With a layout l, the position is in the
 * program l describes (the one the context is in, or one a recording ran) rather than in the
 * module written last. */
static void place(const session *s, const decl *pending, const wb_record *l, const char *path, size_t plen,
                  uint32_t line, char *out, size_t cap, uint32_t *out_line, uint32_t *col)
{
    *out_line = line;
    (void)snprintf(out, cap, "%.*s", (int)plen, path);
    if (plen != 10u || memcmp(path, "session.ci", 10u) != 0) return;
    if (l != NULL) {
        if (l->first_line != 0 && (line == l->unit_line - 1u || line == l->unit_line + l->unit_lines)) {
            (void)snprintf(out, cap, "%s", l->unit_path);
            *out_line = l->first_line;
            *col = l->first_col;
        } else if (l->unit_lines != 0 && line >= l->unit_line && line < l->unit_line + l->unit_lines) {
            (void)snprintf(out, cap, "%s", l->unit_path);
            *out_line = line - l->unit_line + 1u;
        }
        for (uint32_t i = 0; i < l->ndecl; i++) {
            if (line >= l->decl[i].line && line < l->decl[i].line + l->decl[i].nlines) {
                if (l->decl[i].kind == K_SCRIPT) (void)snprintf(out, cap, "%s", l->decl[i].name);
                else (void)snprintf(out, cap, "entry %u", (unsigned)l->decl[i].entry);
                *out_line = line - l->decl[i].line + 1u;
            }
        }
        return;
    }
    for (uint32_t k = 0; k < s->nunit; k++) {
        const unit *u = &s->units[k];
        if (u->first_line != 0 && (line == u->line - 1u || line == u->line + u->lines)) {
            (void)snprintf(out, cap, "%s", u->path);
            *out_line = u->first_line;
            *col = u->first_col;
            return;
        }
        if (line >= u->line && line < u->line + u->lines) {
            (void)snprintf(out, cap, "%s", u->path);
            *out_line = line - u->line + 1u;
            return;
        }
    }
    for (uint32_t i = 0; i <= s->ndecl; i++) {
        const decl *d = i < s->ndecl ? &s->decls[i] : pending;
        if (d != NULL && d->line != 0 && line >= d->line && line < d->line + d->nlines) {
            if (d->kind == K_SCRIPT) (void)snprintf(out, cap, "%s", d->name);
            else (void)snprintf(out, cap, "entry %u", (unsigned)d->entry);
            *out_line = line - d->line + 1u;
            return;
        }
    }
}

/* The source of a position, for the excerpts: an entry's text, or a loaded module's. */
static const uint8_t *source_of(void *user, const char *path, size_t len, size_t *n)
{
    session *s = user;
    static char *loaded;
    unsigned e = 0;
    if (s->scratch != NULL && len == strlen(s->scratch_path) && memcmp(path, s->scratch_path, len) == 0) {
        *n = strlen(s->scratch);
        return (const uint8_t *)s->scratch;
    }
    for (uint32_t k = 0; k < s->nw; k++) {   /* "watch N" and "watch N when" */
        char w[32], when[32];
        (void)snprintf(w, sizeof w, "watch %u", (unsigned)s->w[k].number);
        (void)snprintf(when, sizeof when, "watch %u when", (unsigned)s->w[k].number);
        const char *t = len == strlen(w) && memcmp(path, w, len) == 0 ? s->w[k].expr :
                        len == strlen(when) && memcmp(path, when, len) == 0 ? s->w[k].cond : NULL;
        if (t != NULL) {
            *n = strlen(t);
            return (const uint8_t *)t;
        }
    }
    for (uint32_t i = 0; i < s->ndecl; i++) {
        const decl *d = &s->decls[i];
        if (d->kind == K_SCRIPT && strlen(d->name) == len && memcmp(path, d->name, len) == 0) {
            *n = strlen(d->text);
            return (const uint8_t *)d->text;
        }
    }
    if (len > 6u && memcmp(path, "entry ", 6u) == 0) {
        for (size_t i = 6; i < len && path[i] >= '0' && path[i] <= '9' && e < ITEMS_MAX; i++)
            e = e * 10u + (unsigned)(path[i] - '0');
        if (e >= 1u && e < s->next && s->items[e - 1u] != NULL) {
            *n = strlen(s->items[e - 1u]);
            return (const uint8_t *)s->items[e - 1u];
        }
        return NULL;
    }
    char rel[1100];
    (void)snprintf(rel, sizeof rel, "%.*s", (int)len, path);
    for (uint32_t i = 0; i < s->nloaded; i++) {
        if (strcmp(rel, s->loaded[i]) == 0) {
            char *file = wb_join(s->src, rel);
            free(loaded);
            loaded = file != NULL ? wb_read_file(file, n) : NULL;
            free(file);
            return (const uint8_t *)loaded;
        }
    }
    return NULL;
}

/* A string or integer value of `"key":` in one JSON line, into out. 0 when found. */
static int json_field(const char *line, const char *key, char *out, size_t cap)
{
    char want[40];
    (void)snprintf(want, sizeof want, "\"%s\":", key);
    const char *p = strstr(line, want);
    size_t n = 0;
    if (p == NULL || cap == 0) return -1;
    p += strlen(want);
    if (*p != '"') {
        while ((*p == '-' || (*p >= '0' && *p <= '9')) && n + 1u < cap) out[n++] = *p++;
        out[n] = '\0';
        return n != 0 ? 0 : -1;
    }
    for (p++; *p != '\0' && *p != '"' && n + 1u < cap; p++) {
        char c = *p;
        if (c == '\\' && p[1] != '\0') {
            c = *++p;
            c = c == 'n' ? '\n' : c == 't' ? '\t' : c;
            if (*p == 'u') {   /* \u00XX, as canonical JSON writes a control byte */
                unsigned v = 0;
                for (int k = 0; k < 4 && p[1] != '\0'; k++) {
                    char h = *++p;
                    v = v * 16u + (unsigned)(h >= 'a' ? h - 'a' + 10 : h >= 'A' ? h - 'A' + 10 : h - '0');
                }
                c = v < 0x80u ? (char)v : '?';
            }
        }
        out[n++] = c;
    }
    out[n] = '\0';
    return 0;
}

/* B1's diagnostics (CINT-DIAG-1 lines) as `cint build` prints them, with positions in the
 * entries. Returns the count; *only_c2001 is 1 when every one is C2001 in the entry unit. */
static int diagnostics(session *s, const decl *pending, const char *json, wb_buf *b, int *only_c2001)
{
    int count = 0;
    *only_c2001 = 1;
    for (const char *line = json; line != NULL && *line != '\0';) {
        const char *end = strchr(line, '\n');
        size_t n = end != NULL ? (size_t)(end - line) : strlen(line);
        char *one = malloc(n + 1u), code[16], file[1100], text[1400], num[24], where[1100];
        if (one == NULL) {
            b->failed = 1;
            return count;
        }
        memcpy(one, line, n);
        one[n] = '\0';
        if (strstr(one, "\"CINT-DIAG-1\"") != NULL && json_field(one, "code", code, sizeof code) == 0) {
            uint32_t at = 0, col = 0;
            if (json_field(one, "file", file, sizeof file) != 0) file[0] = '\0';
            at = json_field(one, "line", num, sizeof num) == 0 ? (uint32_t)strtoul(num, NULL, 10) : 0u;
            col = json_field(one, "column", num, sizeof num) == 0 ? (uint32_t)strtoul(num, NULL, 10) : 0u;
            if (json_field(one, "message", text, sizeof text) != 0) (void)snprintf(text, sizeof text, "compile error");
            uint32_t mapped = at;
            place(s, pending, NULL, file, strlen(file), at, where, sizeof where, &mapped, &col);
            *only_c2001 &= strcmp(code, "C2001") == 0 && s->nunit != 0 && strcmp(where, s->units[0].path) == 0;
            int in_watch = 0;
            for (uint32_t k = 0; k < s->nw; k++) {   /* which watches no longer compile */
                char w[32];
                int wl = snprintf(w, sizeof w, "watch %u", (unsigned)s->w[k].number);
                if (strncmp(where, w, (size_t)wl) == 0 && (where[wl] == '\0' || where[wl] == ' ')) {
                    s->bad_watches |= 1u << k;
                    in_watch = 1;
                }
            }
            s->other_errors |= !in_watch;
            wb_f(b, "error[%s]: %s\n  --> %s:%u:%u\n", code, text, where, (unsigned)mapped, (unsigned)col);
            wb_pos pos = {where, (uint32_t)strlen(where), mapped, col};
            wb_excerpt(b, &pos, "", source_of, s);
            if (json_field(one, "note", text, sizeof text) == 0) wb_f(b, "   = note: %s\n", text);
            count++;
        } else if (n != 0) {
            wb_put(b, one, n);   /* any other line cint wrote, as it wrote it */
            wb_s(b, "\n");
        }
        free(one);
        line = end != NULL ? end + 1 : NULL;
    }
    return count;
}

enum { C_OK, C_ERROR, C_BROKEN, C_STOPPED };

/* Frees p unless the session holds it. */
static void retire(session *s, cint_sir_program *p)
{
    if (p != NULL && p != s->prog && p != s->live_prog && p != s->watch_prog) cint_sir_free(p);
}

/* Compiles session.ci as written. C_OK with s->prog the program; C_ERROR with the diagnostics
 * in b; C_BROKEN when B1 could not run or wrote SIR text that cannot be read (said in b);
 * C_STOPPED when an interrupt arrived, which stops the compile and changes nothing (BX14-10). */
static int compile(session *s, const decl *pending, wb_buf *b, int *only_c2001)
{
    wb_buf err = {0};
    int status = -1, r = wb_emit(s->o->cint, 1, s->src, "session.ci", s->build, s->log, &status, &err);
    size_t len = 0;
    char *said = r == 0 && !g_stop ? wb_read_file(s->log, &len) : NULL;
    *only_c2001 = 0;
    s->bad_watches = 0;
    s->other_errors = 0;
    if (g_stop) {
        wb_free(&err);
        wb_s(b, "interrupted: the compile stopped; nothing changed\n");
        return C_STOPPED;
    }
    if (r != 0) {
        wb_f(b, "workbench: %s\n", err.n != 0 ? err.p : "out of memory");
        wb_free(&err);
        return C_BROKEN;
    }
    wb_free(&err);
    if (status == CINT_EXIT_COMPILE) {
        int n = diagnostics(s, pending, said != NULL ? said : "", b, only_c2001);
        free(said);
        return n > 0 ? C_ERROR : C_BROKEN;
    }
    if (status != CINT_EXIT_OK) {
        wb_f(b, "workbench: cint emit-c exited %d\n", status);
        if (said != NULL) wb_put(b, said, len);
        free(said);
        return C_BROKEN;
    }
    free(said);
    char *stage = wb_stage_of(s->build);
    cint_sir_program *p = NULL;
    cint_sir_error serr;
    int st = stage != NULL ? cint_sir_load(stage, &p, &serr) : CINT_SIR_RESOURCE;
    free(stage);
    if (st != CINT_SIR_OK) {
        wb_f(b, "workbench: the SIR text cintc wrote cannot be read: %s:%u:%u: %s\n", serr.path, serr.line, serr.col,
             serr.text);
        return C_BROKEN;
    }
    cint_sir_program *old = s->prog;
    s->prog = p;
    retire(s, old);
    return C_OK;
}

/* ----------------------------------------------------------- Running an entry. */

static cint_status relay(void *user, const uint8_t *bytes, size_t len)
{
    session *s = user;
    if (s->tee != NULL) wb_put(s->tee, bytes, len);
    if (len != 0 && !s->quiet) {
        (void)fwrite(bytes, 1, len, stdout);
        fflush(stdout);
        s->at_line_start = bytes[len - 1u] == '\n';
    }
    return CINT_OK;
}

static value *find_value(const session *s, const char *module, size_t mlen, const char *name, size_t nlen)
{
    for (uint32_t i = 0; i < s->nval; i++) {
        value *v = &s->vals[i];
        if (strlen(v->module) == mlen && memcmp(v->module, module, mlen) == 0 && strlen(v->name) == nlen &&
            memcmp(v->name, name, nlen) == 0) {
            return v;
        }
    }
    return NULL;
}

static void free_values(value *v, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) free(v[i].module);
    free(v);
}

/* The byte of a native integer that holds bits 8b to 8b + 7 of a value of w bytes. */
static unsigned native_at(unsigned b, unsigned w)
{
    uint64_t probe = 1;
    return *(const uint8_t *)&probe ? b : w - 1u - b;
}

/* The values of the context's module state, as the state tables list them. */
static int state_out(cint_ctx *ctx, const cint_program *prog, const cint_state_table *t, value **out, uint32_t *nout)
{
    value *next = NULL;
    uint32_t n = 0, cap = 0;
    for (uint32_t i = 0; t != NULL && i < t->count; i++) {
        const cint_state *st = t->states[i];
        const cint_module *m = prog->modules[st->module];
        const uint8_t *block = cint_rt_state(ctx, st);
        for (uint32_t k = 0; k < st->var_count; k++) {
            const cint_state_var *var = &st->vars[k];
            uint64_t bits = 0;
            /* an array has no state.global line until OQ-156 (rt/OPEN.md RT-OQ-49), a struct none
             * (OQ-260; RT-OQ-51), and a handle (OQ-242) is no integer */
            if (var->rank != 0u || var->tag == CINT_TAG_HANDLE || var->tag == CINT_TAG_RECORD) continue;
            unsigned w = tag_width(var->tag);
            if (block == NULL) {
                free_values(next, n);
                return -1;
            }
            if (n == cap) {
                value *grown = realloc(next, (cap = cap * 2u + 16u) * sizeof *grown);
                if (grown == NULL) {
                    free_values(next, n);
                    return -1;
                }
                next = grown;
            }
            for (unsigned b = 0; b < w; b++) bits |= (uint64_t)block[var->offset + native_at(b, w)] << (8u * b);
            value *v = &next[n];
            v->module = malloc(m->path_len + 1u);
            if (v->module == NULL || var->name_len >= IDENT_MAX) {
                free(v->module);
                free_values(next, n);
                return -1;
            }
            memcpy(v->module, m->path, m->path_len);
            v->module[m->path_len] = '\0';
            memcpy(v->name, var->name, var->name_len);
            v->name[var->name_len] = '\0';
            v->tag = var->tag;
            v->bits = bits;
            n++;
        }
    }
    *out = next;
    *nout = n;
    return 0;
}

typedef struct outcome {
    int faulted, has_result, differs, errored;
    uint32_t rtag;
    uint64_t result;
    int64_t fuel;
    cint_sir_event last;   /* the entry's last counted operation on cpu-sir-interp, when traced */
} outcome;

static int same_values(const value *a, uint32_t na, const value *b, uint32_t nb)
{
    if (na != nb) return 0;
    for (uint32_t i = 0; i < na; i++) {
        int found = 0;
        for (uint32_t k = 0; k < nb && !found; k++) {
            found = strcmp(a[i].module, b[k].module) == 0 && strcmp(a[i].name, b[k].name) == 0 &&
                    a[i].tag == b[k].tag && a[i].bits == b[k].bits;
        }
        if (!found) return 0;
    }
    return 1;
}

/* The report of the fault in ctx, found on backend `on`, its positions in the entries (SPEC-06 5.2);
 * img, when not NULL, ran the entry, and its last dispatch's domain gives a work-item's index tuple. */
static void report(session *s, const decl *pending, int live, cint_ctx *ctx, const cint_sir_image *img, int64_t fuel,
                   const char *state, const char *where, const char *on, wb_buf *b)
{
    static wb_fault f;
    static char paths[1u + CINT_RT_MAX_DEPTH][WB_PATH_MAX + 1u];
    static cint_fault_record rec;
    uint8_t *bytes = NULL;
    size_t n = 0;
    int bad = cint_ctx_fault(ctx, &rec) != CINT_OK || (n = cint_fault_encode(&rec, NULL, 0)) == 0 ||
              (bytes = malloc(n)) == NULL || cint_fault_encode(&rec, bytes, n) != n ||
              wb_fault_decode(bytes, n, &f, WB_RECORD_RUN) != 0;
    if (bad) {   /* the decoded record points into bytes, which stay until the report is written */
        free(bytes);
        wb_s(b, "workbench: the entry faulted with a record that cannot be decoded\n");
        return;
    }
    wb_pos *all[1u + CINT_RT_MAX_DEPTH];
    all[0] = &f.pos;
    for (uint32_t i = 0; i < f.nstack; i++) all[1u + i] = &f.stack[i];
    for (uint32_t i = 0; i <= f.nstack; i++) {
        uint32_t line = all[i]->line;
        place(s, pending, live ? &s->live : NULL, all[i]->path, all[i]->path_len, line, paths[i], sizeof paths[i],
              &line, &all[i]->column);
        all[i]->path = paths[i];
        all[i]->path_len = (uint32_t)strlen(paths[i]);
        all[i]->line = line;
    }
    const cint_sir_program *ps[3] = {s->prog, s->live_prog, s->watch_prog}, *p = NULL;
    for (unsigned i = 0; i < 3u && p == NULL; i++)
        if (ps[i] != NULL && f.has_rev && memcmp(ps[i]->revision, f.rev, 32u) == 0) p = ps[i];
    char site[96];
    wb_site_id(p, rec.position, site, sizeof site);
    wb_report r = {fuel, state, site[0] != '\0' ? site : NULL, where, on, 0u, {0, 0, 0, 0}};
    if (img != NULL) r.rank = cint_sir_dispatch_domain(img, r.extents);
    wb_fault_text(b, &f, &r, source_of, s);
    free(bytes);
}

/* --------------------------------- Session checkpoints (SPEC-06 5.4; BX14-19). */

/* The CINT-CKPT-1 of ctx into *out. */
static cint_status ckpt_of(cint_ctx *ctx, wb_buf *out)
{
    cint_sink sink = {wb_sink, out};
    out->n = 0;
    return cint_ctx_checkpoint(ctx, &sink);
}

/* The revision set and layout of the program the context goes into (BX14-19): the source of
 * each module (session.ci as written last), the declarations, the loaded modules, and the
 * entry unit. Returns 0, or -1 when memory runs out or a module cannot be read. */
static int go_live(session *s)
{
    const cint_sir_program *p = s->live_prog;
    wb_record x;
    memset(&x, 0, sizeof x);
    x.nmod = p->nmod;
    x.mod = calloc(p->nmod, sizeof *x.mod);
    x.decl = s->ndecl != 0 ? calloc(s->ndecl, sizeof *x.decl) : NULL;
    x.loaded = s->nloaded != 0 ? calloc(s->nloaded, sizeof *x.loaded) : NULL;
    int bad = x.mod == NULL || (s->ndecl != 0 && x.decl == NULL) || (s->nloaded != 0 && x.loaded == NULL);
    for (uint32_t k = 0; !bad && k < p->nmod; k++) {
        const char *path = p->mods[k].path;
        size_t n = 0;
        int own = strcmp(path, "session.ci") == 0;
        char *file = own ? NULL : wb_join(s->src, path);
        char *text = file != NULL ? wb_read_file(file, &n) : NULL;
        bad = strlen(path) > WB_PATH_MAX || (!own && text == NULL) ||
              wb_store_put(&s->store, own ? s->written.p : text, own ? s->written.n : n, x.mod[k].text) != 0;
        (void)snprintf(x.mod[k].path, sizeof x.mod[k].path, "%s", path);
        free(file);
        free(text);
    }
    for (uint32_t i = 0; !bad && i < s->ndecl; i++) {
        const decl *d = &s->decls[i];
        wb_rdecl *r = &x.decl[i];
        (void)snprintf(r->name, sizeof r->name, "%s", d->name);
        r->kind = d->kind;
        r->entry = d->entry;
        r->rev = d->rev;
        r->line = d->line;
        r->nlines = d->nlines;
        bad = wb_store_put(&s->store, d->text, strlen(d->text), r->text) != 0;
    }
    for (uint32_t i = 0; !bad && i < s->nloaded; i++) {
        (void)snprintf(x.loaded[i].path, sizeof x.loaded[i].path, "%s", s->loaded[i]);
        x.loaded[i].rev = s->lrev[i];
    }
    x.ndecl = s->ndecl;
    x.nloaded = s->nloaded;
    if (s->nunit != 0 && (strncmp(s->units[0].path, "entry ", 6u) == 0 || wb_path_ok(s->units[0].path))) {
        x.unit_line = s->units[0].line;
        x.unit_lines = s->units[0].lines;
        x.first_line = s->units[0].first_line;
        x.first_col = s->units[0].first_col;
        (void)snprintf(x.unit_path, sizeof x.unit_path, "%s", s->units[0].path);
    }
    x.rev = s->rev;
    (void)snprintf(x.backend, sizeof x.backend, "%s", s->backend);
    cint_sir_revision(p, x.revision);
    if (bad) {
        wb_record_free(&x);
        return -1;
    }
    wb_record_free(&s->live);
    s->live = x;
    return 0;
}

/* A session checkpoint of the current state before entry `ordinal`, kept in the store as id,
 * with its identities. Returns 0, or -1 with the reason in b. */
static int take(session *s, const char *name, int32_t parent, uint32_t ordinal, uint8_t id[32], uint8_t sid[32],
                uint8_t nid[32], wb_buf *b)
{
    wb_buf ck = {0}, enc = {0};
    wb_record r = s->live;   /* its lists stay the live record's */
    wb_rmod empty;
    cint_checkpoint_info info;
    memset(&info, 0, sizeof info);
    if (s->ctx != NULL && s->live.nmod == 0) {
        wb_s(b, "workbench: the sources of this revision were not kept, so no checkpoint holds it\n");
        return -1;
    }
    if (s->ctx != NULL) {
        cint_status st = ckpt_of(s->ctx, &ck);
        if (st != CINT_OK || cint_checkpoint_decode((const uint8_t *)ck.p, ck.n, &info) != CINT_OK ||
            wb_store_put(&s->store, ck.p, ck.n, r.state) != 0) {
            wb_s(b, st == CINT_REFUSED ? "workbench: the runtime refuses a checkpoint of this state (SPEC-03 H-19)\n"
                                       : "workbench: out of memory\n");
            wb_free(&ck);
            return -1;
        }
        r.has_state = 1;
        r.effects = info.effects;
    } else {   /* before the first entry: an empty session module */
        memset(&r, 0, sizeof r);
        memset(&empty, 0, sizeof empty);
        (void)snprintf(empty.path, sizeof empty.path, "session.ci");
        r.nmod = 1;
        r.mod = &empty;
        if (wb_store_put(&s->store, "", 0, empty.text) != 0) {
            wb_s(b, "workbench: out of memory\n");
            return -1;
        }
    }
    r.fuel = s->fuel;
    r.depth = s->depth;
    (void)snprintf(r.backend, sizeof r.backend, "%s", s->backend);
    r.has_parent = parent >= 0;
    if (parent >= 0) memcpy(r.parent, s->ck[parent].nid, 32u);
    r.ordinal = ordinal;
    (void)snprintf(r.name, sizeof r.name, "%s", name);
    int bad = wb_record_encode(&enc, &r) != 0 || wb_store_put(&s->store, enc.p, enc.n, id) != 0 ||
              wb_record_ids(&r, info.state, info.state_len, sid, nid) != 0;
    wb_free(&enc);
    wb_free(&ck);
    if (bad) wb_s(b, "workbench: out of memory\n");
    return bad ? -1 : 0;
}

/* Adds a checkpoint to the session's list. Returns its index, or -1 when memory runs out. */
static int32_t add_ckpt(session *s, const char *name, uint32_t number, int32_t parent, uint32_t ordinal,
                        const uint8_t id[32], const uint8_t sid[32], const uint8_t nid[32])
{
    ckpt *grown = room(s->ck, s->nck, &s->capck, sizeof *grown);
    if (grown == NULL) return -1;
    s->ck = grown;
    ckpt *c = &s->ck[s->nck];
    memset(c, 0, sizeof *c);
    (void)snprintf(c->name, sizeof c->name, "%s", name);
    c->number = number;
    c->branch = s->cur;
    c->parent = parent;
    c->ordinal = ordinal;
    if (s->rec != NULL) {   /* the current state is the last boundary of the live segment */
        c->seg = s->nseg;
        c->at = (uint32_t)s->seg[s->nseg - 1u].boundaries;
    }
    memcpy(c->record, id, 32u);
    memcpy(c->sid, sid, 32u);
    memcpy(c->nid, nid, 32u);
    return (int32_t)s->nck++;
}

/* Keeps the point entry `ordinal` undoes to (BX14-09); without it, :undo stops there. */
static void undo_point(session *s, uint32_t ordinal, wb_buf *b)
{
    uint8_t id[32], sid[32], nid[32];
    int32_t k = -1;
    if (take(s, "", -1, ordinal, id, sid, nid, b) != 0 || (k = add_ckpt(s, "", 0, -1, ordinal, id, sid, nid)) < 0) {
        s->nundo = 0;   /* :undo cannot pass an entry it cannot undo */
        return;
    }
    uint32_t *grown = room(s->undo, s->nundo, &s->capundo, sizeof *grown);
    if (grown == NULL) {
        s->nundo = 0;
        return;
    }
    s->undo = grown;
    s->undo[s->nundo++] = (uint32_t)k;
}

/* ----------------------------- The recording (SPEC-06 10.4; BX14-09; unit 3a). */
/*
 * The session records its context with box 13's recorder, in full mode at entry density: each
 * entry with its prints and outcome, each cleared fault, and each change of revision, with the
 * SIR text of a new revision (a change that keeps the revision carries no code). A recorder
 * lasts as long as its context, so a restore starts a new segment from the state restored, and
 * a replay of the live segment ends it first, since only a complete recording replays (SPEC-03
 * H-17), and starts the next from the same state. The segments and their parents form the tree
 * of the states the session reached; one is reached again by replaying its segment up to it.
 */

static cint_status rec_sink(void *user, const uint8_t *bytes, size_t len)
{
    session *s = user;
    wb_buf *r = &s->seg[s->nseg - 1u].rec;
    wb_put(r, bytes, len);
    return r->failed ? CINT_RESOURCE : CINT_OK;
}

/* The code of program p for a recording: each module's path and SIR text. NULL when memory runs
 * out. */
static cint_code_module *code_of(const cint_sir_program *p)
{
    cint_code_module *c = calloc(p->nmod != 0 ? p->nmod : 1u, sizeof *c);
    for (uint32_t k = 0; c != NULL && k < p->nmod; k++) {
        cint_sir_text t;
        cint_sir_text_of(p, k, &t);
        c[k].path.bytes = t.path;
        c[k].path.len = (uint32_t)t.path_len;
        c[k].sir = t.text;
        c[k].sir_len = t.len;
    }
    return c;
}

/* Starts a segment that records the context from its current state, boundary `at` of segment
 * `parent` (-1: none), reached as `how` says. When box 13's recorder refuses, the state goes on
 * unrecorded. */
static void rec_open(session *s, int32_t parent, uint32_t at, int how)
{
    cint_sink sink = {rec_sink, s};
    if (s->rec != NULL || s->ctx == NULL || s->live_img == NULL) return;
    segment *grown = room(s->seg, s->nseg, &s->capseg, sizeof *grown);
    if (grown == NULL) return;
    s->seg = grown;
    segment *g = &s->seg[s->nseg++];
    memset(g, 0, sizeof *g);
    g->parent = parent;
    g->at = at;
    g->how = how;
    cint_code_module *code = code_of(s->live_prog);
    if (code == NULL || cint_rec_begin(s->ctx, cint_sir_observer(s->live_img), code, s->live_prog->nmod,
                                       CINT_RECORD_FULL, &sink, &s->rec) != CINT_OK) {
        s->rec = NULL;
        wb_free(&g->rec);
        s->nseg--;
    }
    free(code);
}

/* Ends the live segment; the store holds it. */
static void rec_close(session *s)
{
    if (s->rec != NULL) {
        segment *g = &s->seg[s->nseg - 1u];
        int ok = cint_rec_end(s->rec) == CINT_OK && !g->rec.failed &&
                 wb_store_put(&s->store, g->rec.p, g->rec.n, g->id) == 0;
        g->ended = ok ? 1 : -1;
        wb_free(&g->rec);
        s->rec = NULL;
    }
}

/* The context changes to the program of img, recorded. */
static cint_status rec_revision(session *s, cint_sir_image *img)
{
    uint8_t now[32];
    const cint_program *prog = cint_sir_cint_program(img);
    cint_sir_revision(s->live_prog, now);
    int same = memcmp(now, prog->revision, 32u) == 0;
    cint_code_module *code = same ? NULL : code_of(s->prog);
    cint_status st = !same && code == NULL ? CINT_RESOURCE :
                     cint_rec_change_revision(s->rec, cint_sir_observer(img), cint_sir_state_table(img), code,
                                              same ? 0u : s->prog->nmod);
    free(code);
    return st;
}

/* The layout of the program the context is in: a session checkpoint record without state, in
 * the store as id. Returns 0, or -1. */
static int layout_of(session *s, uint8_t id[32])
{
    wb_record x = s->live;   /* its lists stay the live record's */
    wb_buf enc = {0};
    x.has_state = 0;
    x.effects = 0;
    x.fuel = s->fuel;
    x.depth = s->depth;
    x.has_parent = 0;
    x.name[0] = '\0';
    x.ordinal = 1;
    for (uint32_t i = 0; i < x.ndecl; i++) {   /* the first ordinal its declarations allow */
        x.ordinal = x.decl[i].entry >= x.ordinal ? x.decl[i].entry + 1u : x.ordinal;
    }
    int bad = x.nmod == 0 || wb_record_encode(&enc, &x) != 0 || wb_store_put(&s->store, enc.p, enc.n, id) != 0;
    wb_free(&enc);
    return bad ? -1 : 0;
}

/* ---- Running an entry on the session's context, or an expression in a scratch one. */

static cint_ctx *new_ctx(session *s, const cint_program *prog, const cint_state_table *t)
{
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL;
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = prog;
    cfg.output = relay;
    cfg.output_user = s;
    if (cint_ctx_create(&cfg, &ctx) != CINT_OK || cint_ctx_bind_state(ctx, t) != CINT_OK) {
        cint_ctx_destroy(ctx);
        return NULL;
    }
    return ctx;
}

/* Moves the session's context into the program of img for entry `ordinal`, after keeping the
 * point the entry undoes to; the program becomes the live one. Without `record` (an entry on
 * cpu-c17 alone) the live segment ends first and none starts. Returns the context, or NULL with
 * the reason in b. */
static cint_ctx *enter(session *s, cint_sir_image *img, uint32_t ordinal, int record, wb_buf *b)
{
    const cint_program *prog = cint_sir_cint_program(img);
    const cint_state_table *t = cint_sir_state_table(img);
    undo_point(s, ordinal, b);
    if (!record) rec_close(s);
    if (s->ctx != NULL && s->rec != NULL &&
        (cint_rec_clear_fault(s->rec) != CINT_OK || rec_revision(s, img) != CINT_OK)) {
        rec_close(s);   /* the recorder failed: the state goes on unrecorded, and a new segment starts */
    }
    if (s->ctx != NULL && s->rec == NULL) {
        /* the context reset the last report announced (A-7a) */
        (void)cint_ctx_clear_fault(s->ctx);
        if (cint_ctx_change_revision(s->ctx, prog, t) != CINT_OK) {
            wb_s(b, "workbench: the session's state cannot move to this revision (SPEC-03 H-19b); it starts again "
                    "from the declarations\n");
            cint_ctx_destroy(s->ctx);
            s->ctx = NULL;
        }
    }
    if (s->ctx == NULL && (s->ctx = new_ctx(s, prog, t)) == NULL) {
        wb_s(b, "workbench: cannot make a context for the entry\n");
        return NULL;
    }
    cint_sir_image *old_img = s->live_img;
    cint_sir_program *old = s->live_prog;
    s->live_img = img;
    s->live_prog = s->prog;
    cint_sir_release(old_img);
    if (old != s->live_prog) {
        retire(s, old);
        if (go_live(s) != 0)
            wb_s(b, "workbench: cannot keep the sources of this revision; checkpoints will be refused\n");
    }
    if (record) {
        rec_open(s, -1, 0, SEG_NEW);   /* a new context, or a state that was not recorded */
    }
    return s->ctx;
}

/* A context in the program of img whose state is that of `from`, a context in the program of
 * from_img (the session's, or one a replay left), for an expression whose effects are not kept
 * (BX14-08). NULL with the reason in b. */
static cint_ctx *scratch_of(session *s, cint_ctx *from, cint_sir_image *from_img, cint_sir_image *img, wb_buf *b)
{
    const cint_program *prog = cint_sir_cint_program(img);
    const cint_state_table *t = cint_sir_state_table(img);
    cint_ctx *x = NULL;
    wb_buf ck = {0};
    if (from == NULL) {
        x = new_ctx(s, prog, t);
    } else if ((x = new_ctx(s, cint_sir_cint_program(from_img), cint_sir_state_table(from_img))) != NULL &&
               (ckpt_of(from, &ck) != CINT_OK || cint_ctx_restore(x, (const uint8_t *)ck.p, ck.n) != CINT_OK ||
                cint_ctx_clear_fault(x) != CINT_OK || cint_ctx_change_revision(x, prog, t) != CINT_OK)) {
        cint_ctx_destroy(x);
        x = NULL;
    }
    wb_free(&ck);
    if (x == NULL) wb_s(b, "workbench: cannot make a context with the session's state (SPEC-03 H-19a)\n");
    return x;
}

static void hex_of(const uint8_t id[32], char out[65])
{
    static const char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < 32u; i++) {
        out[2u * i] = digits[id[i] >> 4];
        out[2u * i + 1u] = digits[id[i] & 15u];
    }
    out[64] = '\0';
}

/* The library of the program compiled last, built with `cint build --lib` once per revision set
 * (BX14-08), or loaded again when this session built it before; a build says what it cost in b.
 * NULL with the reason in b. */
static wb_c17 *c17_lib(session *s, wb_buf *b)
{
    uint8_t rev[32];
    char hex[65], name[24];
    cint_sir_revision(s->prog, rev);
    if (s->lib != NULL && memcmp(s->lib_rev, rev, 32u) == 0) return s->lib;
    hex_of(rev, hex);
    (void)snprintf(name, sizeof name, "c17-%.16s", hex);
    wb_buf err = {0};
    int status = 0;
    long long t0 = wb_now_ns();
    char *out = wb_join(s->build, name), *log = wb_join(s->build, "c17.stderr");
    char *made = out != NULL ? wb_join(out, CLI_LIB_PREFIX "session" CLI_LIB_SUFFIX) : NULL, *path = NULL;
    wb_c17 *lib = made != NULL ? wb_c17_open(made, &err) : NULL;
    err.n = 0;
    if (lib == NULL && out != NULL && log != NULL &&
        (path = wb_c17_build(s->o->cint, 0, s->src, "session.ci", out, log, &status, &err)) != NULL) {
        lib = wb_c17_open(path, &err);
        if (lib != NULL) wb_f(b, "cpu-c17: revision %.8s built as a library in %lld ms\n", hex,
                                 wb_elapsed_ns(t0, wb_now_ns()) / 1000000);
    }
    if (lib != NULL && memcmp(wb_c17_program(lib)->revision, rev, 32u) != 0) {
        wb_f(&err, "%s has another revision identity than the SIR text of this revision", path != NULL ? path : made);
        wb_c17_close(lib);
        lib = NULL;
    }
    if (lib == NULL) {
        wb_f(b, "workbench: cpu-c17: %s\n", err.n != 0 ? err.p : "out of memory");
    } else {
        wb_c17_close(s->lib);
        s->lib = lib;
        memcpy(s->lib_rev, rev, 32u);
    }
    free(out);
    free(log);
    free(made);
    free(path);
    wb_free(&err);
    return lib;
}

/* A call on cpu-c17: its status, result, fuel, and state hash, and the CINT-CKPT-1 it left. */
typedef struct c17_run {
    cint_status st;
    uint64_t result;
    int64_t fuel;
    uint8_t hash[32];
    wb_buf after;
} c17_run;

/* Calls export x of s->lib as one entry, on a context restored from `from`, a CINT-CKPT-1 of the
 * session's context in the same revision (SPEC-03 H-19). Returns 0, or -1 with the reason in b. */
static int c17_call(session *s, const wb_c17_export *x, const uint64_t *args, const wb_buf *from, c17_run *r,
                    wb_buf *b)
{
    wb_buf err = {0};
    cint_ctx *ctx = wb_c17_context(s->lib, s->depth, relay, s, &err);
    cint_status st = ctx != NULL ? wb_c17_restore(s->lib, ctx, (const uint8_t *)from->p, from->n, &err) : CINT_REFUSED;
    int ok = st == CINT_OK;
    if (ok) {   /* compiled code does not read the interrupt flag yet (RT-OQ-42) */
        r->st = wb_c17_call(s->lib, ctx, x, g_stop ? 0 : s->fuel, args, &r->result);
        r->fuel = wb_c17_fuel(s->lib, ctx);
        ok = (r->st == CINT_OK || r->st == CINT_FAULT) && wb_c17_state_hash(s->lib, ctx, r->hash, &err) == CINT_OK &&
             wb_c17_checkpoint(s->lib, ctx, &r->after, &err) == CINT_OK;
        if (!ok && err.n == 0) wb_f(&err, "the call ended with status %d", (int)r->st);
    } else if (ctx != NULL && err.n == 0) {
        wb_f(&err, "the library refused the session's state (status %d)", (int)st);
    }
    if (!ok) wb_f(b, "workbench: cpu-c17: %s\n", err.n != 0 ? err.p : "out of memory");
    wb_c17_destroy(s->lib, ctx);
    wb_free(&err);
    return ok ? 0 : -1;
}

/* ---------------------------------------- The inexact annotation (SPEC-06 5.2) */

/* The trace events of an entry on cpu-sir-interp keep its last counted operation. A call
 * clears it, so a value that a later call returned is not taken for the operation's. */
static void last_op(void *user, const cint_sir_event *e)
{
    cint_sir_event *last = user;
    if (e->kind == CINT_SIR_EV_OP) *last = *e;
    else if (e->kind == CINT_SIR_EV_CALL) last->kind = 0;
}

/* Whether p has such a built-in (sir_read.c's numbers 10, 11, and 14); only then does an entry
 * pay for trace events. */
static int rounds(const cint_sir_program *p)
{
    for (uint32_t k = 0; k < p->nmod; k++) {
        for (uint32_t i = 0; i < p->mods[k].nnode; i++) {
            const si_node *n = &p->mods[k].nodes[i];
            if (n->op == OP_BUILTIN && (n->sub == 10 || n->sub == 11 || n->sub == 14)) return 1;
        }
    }
    return 0;
}

static void mul128(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo)
{
    uint64_t a0 = a & 0xFFFFFFFFu, a1 = a >> 32, b0 = b & 0xFFFFFFFFu, b1 = b >> 32;
    uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0;
    uint64_t mid = (p00 >> 32) + (p01 & 0xFFFFFFFFu) + (p10 & 0xFFFFFFFFu);
    *lo = mid << 32 | (p00 & 0xFFFFFFFFu);
    *hi = a1 * b1 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

/* (hi:lo) / d into hi and lo, bit by bit, and the remainder; d is not 0. */
static uint64_t divmod128(uint64_t *hi, uint64_t *lo, uint64_t d)
{
    uint64_t n[2] = {*hi, *lo}, q[2] = {0, 0}, r = 0;
    for (int i = 127; i >= 0; i--) {
        uint64_t top = r >> 63;
        r = r << 1 | (n[i < 64] >> (i & 63) & 1u);
        if (top != 0 || r >= d) {   /* the true value is below 2d, so the wrapped difference is exact */
            r -= d;
            q[i < 64] |= (uint64_t)1 << (i & 63);
        }
    }
    *hi = q[0];
    *lo = q[1];
    return r;
}

/* The magnitude of operand bits, flipping *neg when a signed operand is negative. */
static uint64_t magnitude(uint64_t bits, int sig, int *neg)
{
    if (sig && (bits >> 63) != 0) {
        *neg = !*neg;
        return 0u - bits;
    }
    return bits;
}

/* The annotation of entry value `bits` of type `tag` into out ("" for none), from the entry's
 * last counted operation: `inexact: <built-in> <mode>`, and for a quotient its exact value,
 * in decimal when it terminates and as a reduced fraction otherwise. */
static void inexact_of(const cint_sir_event *e, uint32_t tag, uint64_t bits, char *out, size_t cap)
{
    const char *part[6];
    uint32_t len[6], np = 0;
    out[0] = '\0';
    if (e->kind != CINT_SIR_EV_OP || !e->has_result || tag == CINT_TAG_BOOL) return;
    for (uint32_t at = 0, k = 0; k <= e->operation_len && np < 6u; k++) {   /* name.form.type[.type].mode */
        if (k == e->operation_len || e->operation[k] == '.') {
            part[np] = e->operation + at;
            len[np++] = k - at;
            at = k + 1u;
        }
    }
    int dr = np == 4u && len[0] == 9u && memcmp(part[0], "div_round", 9u) == 0;
    int md = np == 5u && len[0] == 6u && memcmp(part[0], "muldiv", 6u) == 0;
    int sq = np == 4u && len[0] == 11u && memcmp(part[0], "isqrt_round", 11u) == 0;
    unsigned wbits = (dr || md || sq) && len[2] >= 2u ? (unsigned)strtoul(part[2] + 1, NULL, 10) : 0u;
    if (wbits != 8u && wbits != 16u && wbits != 32u && wbits != 64u) {
        return;   /* not such a built-in, or a type of more than 64 bits, which the event does not hold */
    }
    const char *ty = part[md ? 3 : 2];   /* the operands' type */
    uint32_t rtag = (part[2][0] == 'u' ? 0x20u : 0x10u) | (wbits == 8u ? 1u : wbits == 16u ? 2u : wbits == 32u ? 3u : 4u);
    uint64_t mask = wbits == 64u ? UINT64_MAX : ((uint64_t)1 << wbits) - 1u;
    if (rtag != tag || ((e->result ^ bits) & mask) != 0) {
        return;   /* the entry's value is not this operation's result */
    }
    int sig = ty[0] == 'i', neg = 0;
    wb_buf t = {0};
    if (sq) {   /* inexact unless the operand is a square */
        uint64_t x = magnitude(e->operands[0], sig, &neg), rt = e->result;
        if (rt > 0xFFFFFFFFu || rt * rt != x) wb_f(&t, "inexact: isqrt_round %.*s", (int)len[3], part[3]);
    } else {
        uint64_t hi = 0, lo = magnitude(e->operands[0], sig, &neg), d = magnitude(e->operands[dr ? 1 : 2], sig, &neg);
        if (md) mul128(lo, magnitude(e->operands[1], sig, &neg), &hi, &lo);
        uint64_t r[2] = {hi, lo}, rem = d != 0 ? divmod128(&r[0], &r[1], d) : 0;
        if (rem != 0) {
            uint64_t g = d, h = rem;   /* gcd(n, d) = gcd(d, n mod d) */
            while (h != 0) {
                uint64_t m = g % h;
                g = h;
                h = m;
            }
            (void)divmod128(&hi, &lo, g);
            d /= g;
            uint8_t le[16];
            for (unsigned i = 0; i < 8u; i++) {
                le[i] = (uint8_t)(lo >> (8u * i));
                le[8u + i] = (uint8_t)(hi >> (8u * i));
            }
            wb_num q;
            (void)wb_num_from_le(&q, le, sizeof le, 0);
            q.neg = neg;
            wb_f(&t, "inexact: %.*s %.*s (exact quotient ", (int)len[0], part[0], (int)len[np - 1u], part[np - 1u]);
            if (wb_num_dec_over(&t, &q, d, 0) != 0) {
                wb_num_dec(&t, &q, 0);
                wb_f(&t, "/%llu", (unsigned long long)d);
            }
            wb_s(&t, ")");
        }
    }
    if (!t.failed && t.n != 0) (void)snprintf(out, cap, "%.*s", (int)t.n, t.p);
    wb_free(&t);
}

/* The value line of an entry, with its inexact annotation. */
static void entry_value(wb_buf *b, const char *name, uint32_t tag, uint64_t bits, const outcome *oc)
{
    wb_tval tv;
    char note[160];
    size_t at = b->n;
    tval_of(&tv, tag, bits);
    if (wb_value_line(b, name, &tv) != 0) return;
    inexact_of(&oc->last, tag, bits, note, sizeof note);
    if (note[0] != '\0' && !b->failed && b->n > at && b->p[b->n - 1u] == '\n') {
        b->n--;
        wb_f(b, "   %s\n", note);
    }
}

/* ------- Arrays of the state (box 14 unit 4b): CINT-CKPT-1 holds a module-level array as its
 * descriptor (tag 51, the element's tag, the rank and each extent; SPEC-01 IM-146) and its
 * elements, row-major and little-endian, which the inspector reads as a view from index 0. */

static unsigned elem_size(unsigned tag)
{
    return tag == CINT_TAG_BOOL ? 1u : 1u << (((tag & 15u) - 1u) & 3u);
}

static int array_view(const cint_checkpoint_global *g, wb_view *v)
{
    const uint8_t *d = g->value.bytes;
    int64_t stride = 1;
    if (g->elements == NULL || g->value.len < 4u || d[0] != 0x51u || d[2] < 1u || d[2] > WB_RANK_MAX ||
        g->value.len != 3u + 8u * d[2]) {
        return 0;
    }
    memset(v, 0, sizeof *v);
    v->tag = d[1];
    v->rank = d[2];
    for (uint32_t k = v->rank; k-- > 0;) {
        uint64_t e = 0;
        for (unsigned i = 8; i-- > 0;) e = e << 8 | d[3u + 8u * k + i];
        v->extent[k] = (int64_t)e;
        v->stride[k] = stride;
        stride *= v->extent[k];   /* at most the element count, which the decoder checked */
    }
    v->buffer = g->elements;
    v->buffer_len = (int64_t)g->element_count;
    v->write = 1;
    v->placement = "host";
    return 1;
}

/* The array that NAME (`name` or `module.name`, blanks around it) names in the current state: 1,
 * with *v over its elements in ck, which the caller frees, and in shown the name as typed (in a
 * debug session, the global's); 0 when NAME names no one array; -1 when the state cannot be
 * read. */
static int array_named(session *s, const char *name, wb_buf *ck, wb_view *v, char shown[2u * IDENT_MAX])
{
    char full[2u * IDENT_MAX];
    const char *p = skip(name);
    size_t n = 0, cursor = 0;
    wb_tval tv = {0};
    cint_checkpoint_info info;
    cint_checkpoint_global g;
    memset(&g, 0, sizeof g);
    while (id_char((unsigned char)p[n], n == 0) || (n > 0 && p[n] == '.')) n++;
    if (n == 0 || n >= sizeof full || *skip(p + n) != '\0') return 0;
    (void)snprintf(shown, 2u * IDENT_MAX, "%.*s", (int)n, p);
    int found = wb_global_named(s->ctx, shown, &tv, full, sizeof full, NULL);
    if (found != 1 || tv.len == 0 || tv.bytes[0] != 0x51u) {
        return 0;   /* the expression's compile says why, when it does not compile */
    }
    memset(&info, 0, sizeof info);
    if (ckpt_of(s->ctx, ck) != CINT_OK || cint_checkpoint_decode((const uint8_t *)ck->p, ck->n, &info) != CINT_OK)
        return -1;
    while (cint_checkpoint_global_next(&info, &cursor, &g) == CINT_OK) {
        if (g.name.len == strlen(full) && memcmp(g.name.bytes, full, g.name.len) == 0) {
            if (s->o->debug != NULL) memcpy(shown, full, sizeof full);
            return array_view(&g, v) ? 1 : -1;
        }
    }
    return -1;
}

/* An array's type, `I64[4]`. */
static void array_type(wb_buf *b, const wb_view *v)
{
    char text[64];
    wb_tval t;
    memset(&t, 0, sizeof t);
    t.bytes[0] = (uint8_t)v->tag;
    t.len = (uint16_t)(1u + elem_size(v->tag));
    (void)wb_tval_text(&t, text, sizeof text, 1);
    wb_f(b, "%.*s[", (int)strcspn(text, " "), text);
    for (uint32_t k = 0; k < v->rank; k++) wb_f(b, "%s%lld", k != 0 ? ", " : "", (long long)v->extent[k]);
    wb_s(b, "]");
}

static void tvalue_text(const cint_tvalue *v, int with_type, char *out, size_t cap)
{
    wb_tval tv;
    tv.len = v->len;
    memcpy(tv.bytes, v->bytes, v->len);
    (void)wb_tval_text(&tv, out, cap, with_type);
}

/* The bytes of struct global g's type (tag 71; OQ-260, rt/OPEN.md RT-OQ-51), which the decoder
 * checked: a U32 count of definitions, the definitions (SPEC-03 A-21) with its struct's last,
 * then 71 and that one's index; before them, its payload. Their count, with *name and *len its
 * struct's qualified name; 0 when g is no struct. */
static size_t struct_type(const cint_checkpoint_global *g, const uint8_t **name, uint32_t *len)
{
    const uint8_t *p = g->elements;
    uint64_t n = g->element_count, at = 4u;
#define U32_AT(k) ((uint32_t)p[k] | (uint32_t)p[(k) + 1u] << 8 | (uint32_t)p[(k) + 2u] << 16 | (uint32_t)p[(k) + 3u] << 24)
    if (g->value.len != 1u || g->value.bytes[0] != CINT_TAG_RECORD || p == NULL || n < 4u) return 0u;
    for (uint32_t d = 0, nd = U32_AT(0u); d < nd; d++) {
        uint32_t nf;
        if (at + 5u > n || p[at] != CINT_TAG_RECORD || (*len = U32_AT(at + 1u)) > n - at - 5u) return 0u;
        *name = p + at + 5u;
        at += 5u + *len;
        if (at + 5u > n) return 0u;
        nf = U32_AT(at + 1u);
        at += 5u;
        for (uint32_t f = 0; f < nf; f++) {   /* its name, bit width 0, and type */
            if (at + 4u > n || U32_AT(at) > n - at - 4u) return 0u;
            at += 4u + U32_AT(at) + 1u;
            if (at + 1u > n) return 0u;
            unsigned shaped = p[at] == 0x52u, item;
            at += shaped;
            if (at + 1u > n) return 0u;
            item = p[at++];
            at += item == CINT_TAG_RECORD ? 4u : 0u;
            if (shaped) {   /* the rank, then per dimension form 01, no size symbol, the extent */
                if (at + 1u > n) return 0u;
                at += 1u + 13u * (uint64_t)p[at];
            }
        }
    }
#undef U32_AT
    return at + 5u <= n ? (size_t)(at + 5u) : 0u;
}

/* The text of global g: its value, an array's type, or a struct's type's name. */
static void global_text(const cint_checkpoint_global *g, int with_type, char *out, size_t cap)
{
    wb_buf t = {0};
    wb_view v = {0};
    const uint8_t *name = NULL;
    uint32_t len = 0;
    if (struct_type(g, &name, &len) != 0u) {
        (void)snprintf(out, cap, "%.*s", (int)len, (const char *)name);
        return;
    }
    if (!array_view(g, &v)) {
        tvalue_text(&g->value, with_type, out, cap);
        return;
    }
    array_type(&t, &v);
    (void)snprintf(out, cap, "%.*s", t.failed ? 0 : (int)t.n, t.p != NULL ? t.p : "");
    wb_free(&t);
}

/* The text of element p of v, in canonical order. */
static void element_text(const wb_view *v, uint64_t p, char *out, size_t cap)
{
    wb_tval t;
    unsigned w = elem_size(v->tag);
    t.bytes[0] = (uint8_t)v->tag;
    memcpy(t.bytes + 1, v->buffer + p * w, w);
    t.len = (uint16_t)(w + 1u);
    (void)wb_tval_text(&t, out, cap, 0);
}

/* `:inspect` (words: the format words) or `:type` of the array NAME names: 1 when it names
 * one, 0 when not. */
static int inspect_array(session *s, const char *name, int type_only, int nw, char **words, wb_buf *b)
{
    wb_buf ck = {0}, err = {0};
    wb_aformats af;
    wb_view v = {0};
    char shown[2u * IDENT_MAX] = "";
    int st = array_named(s, name, &ck, &v, shown);
    memset(&af, 0, sizeof af);
    if (st < 0) {
        wb_s(b, "workbench: the current state cannot be read\n");
    } else if (st > 0 && type_only) {
        wb_f(b, "%s : ", shown);
        array_type(b, &v);
        wb_s(b, "\n");
    } else if (st > 0 && (wb_aformats_parse(nw, (const char *const *)words, &af, &err) != 0 ||
                          wb_inspect_array(b, shown, &v, &af, &err) != 0)) {
        wb_f(b, ":inspect: %s\n", err.n != 0 ? err.p : "out of memory");
    }
    wb_free(&ck);
    wb_free(&err);
    return st != 0;
}

/* Runs export `fn` of module `module` with args: as entry `ordinal` on the session's context and
 * backend when `keep` is set, and then also on s->check when set, or on the interpreter in a
 * scratch context. With fn NULL it only moves the context into the program, which drops what
 * the revision no longer declares. Returns 0, or -1 with the reason in b. */
static int run(session *s, const decl *pending, const char *module, const char *fn, const uint64_t *args,
               uint32_t ordinal, int keep, outcome *oc, wb_buf *b)
{
    cint_sir_image *img = NULL;
    cint_sir_error err;
    const cint_error_set *set = NULL;
    uint64_t etag = 0;
    memset(oc, 0, sizeof *oc);
    int st = cint_sir_prepare(s->prog, &img, &err);
    if (st != CINT_SIR_OK) {
        wb_f(b, "workbench: cpu-sir-interp does not run this yet: %s:%u:%u: %s\n", err.path, err.line, err.col,
             err.text);
        return -1;
    }
    const cint_program *prog = cint_sir_cint_program(img);
    const cint_observer *obs = cint_sir_observer(img);
    const cint_observer_entry *e = NULL;
    for (uint32_t i = 0; fn != NULL && i < obs->entry_count; i++) {
        const cint_module *m = prog->modules[obs->entries[i].module];
        if (obs->entries[i].name_len == strlen(fn) && memcmp(obs->entries[i].name, fn, strlen(fn)) == 0 &&
            m->path_len == strlen(module) && memcmp(m->path, module, m->path_len) == 0) {
            e = &obs->entries[i];
        }
    }
    if (fn != NULL && e == NULL) {
        wb_f(b, "workbench: %s exports no function %s that an entry can call\n", module, fn);
        cint_sir_release(img);
        return -1;
    }
    /* c17: the entry runs on cpu-c17; other: on the backend --check names too */
    int c17 = keep && e != NULL && strcmp(s->backend, "cpu-c17") == 0, other = keep && e != NULL && s->check != NULL;
    const wb_c17_export *x = NULL;
    if ((c17 || other) && c17_lib(s, b) != NULL &&
        ((x = wb_c17_find(s->lib, module, fn)) == NULL || x->why[0] != '\0')) {
        wb_f(b, "workbench: cpu-c17 cannot call %s: %s\n", fn, x != NULL ? x->why : "the library does not export it");
        x = NULL;
    }
    if ((c17 || other) && x == NULL) {
        cint_sir_release(img);
        return -1;
    }
    sayf(s, b);   /* before the entry's prints */
    cint_ctx *ctx = keep ? enter(s, img, ordinal, !c17 || other, b) : scratch_of(s, s->ctx, s->live_img, img, b);
    if (ctx == NULL) {
        if (!keep || s->live_img != img) cint_sir_release(img);
        return -1;
    }
    const cint_state_table *table = cint_sir_state_table(img);
    value *before = NULL, *after = NULL;
    uint32_t nbefore = 0, nafter = 0;
    int read = state_out(ctx, prog, table, &before, &nbefore);
    if (e != NULL) {
        uint64_t result = 0;
        cint_status cs = CINT_OK;
        int recorded = keep && s->rec != NULL, bad = 0;
        wb_buf from = {0}, shown = {0}, kept = {0};
        c17_run cr;
        memset(&cr, 0, sizeof cr);
        if (c17 || other) {   /* the state both backends start from */
            bad = ckpt_of(ctx, &from) != CINT_OK;
            if (bad) wb_s(b, "workbench: cpu-c17: the session's state cannot be checkpointed\n");
        }
        if (!bad && c17) {   /* the backend in force: its prints are shown */
            s->tee = other ? &shown : NULL;
            bad = c17_call(s, x, args, &from, &cr, b);
            s->tee = NULL;
        }
        if (!bad && (!c17 || other)) {
            s->quiet = c17;
            s->tee = other ? c17 ? &kept : &shown : NULL;
            int traced = keep && rounds(s->prog);
            if (traced) cint_sir_trace(img, last_op, &oc->last);
            g_entry = ctx;   /* an interrupt that came before the call caps its allowance at nothing */
            cs = recorded ? cint_rec_entry(s->rec, (uint32_t)(e - obs->entries), args, g_stop ? 0 : s->fuel, s->depth,
                                           &result)
                          : e->call(ctx, g_stop ? 0 : s->fuel, s->depth, args, &result);
            g_entry = NULL;
            if (traced) cint_sir_trace(img, NULL, NULL);
            s->quiet = 0;
            s->tee = NULL;
            if (other && wb_check_alter != NULL) {   /* the test's change, before the two are compared */
                wb_check_alter(ordinal, 0, ctx);
            }
            if (recorded && cs == CINT_RESOURCE) {
                rec_close(s);   /* the recording ended at this entry (SPEC-03 A-6); the next entry starts another */
            }
            if (recorded && (cs == CINT_OK || cs == CINT_FAULT) && ordinal >= 1u && ordinal <= ITEMS_MAX) {
                bound *d = &s->bnd[ordinal - 1u];   /* a boundary that replay reaches again */
                d->seg = layout_of(s, d->layout) == 0 ? s->nseg : 0u;
                d->at = (uint32_t)++s->seg[s->nseg - 1u].boundaries;
            }
        }
        if (!bad && other && !c17) {
            s->quiet = 1;
            s->tee = &kept;
            bad = c17_call(s, x, args, &from, &cr, b);
            s->quiet = 0;
            s->tee = NULL;
        }
        if (!bad && other) {   /* compared at the boundary (SPEC-06 5.2, --check) */
            uint8_t h[32];
            const char *what = cs != cr.st ? "outcome" :
                               cs == CINT_OK && e->types[0] != 0 && result != cr.result ? "result" :
                               shown.n != kept.n || (shown.n != 0 && memcmp(shown.p, kept.p, shown.n) != 0) ? "prints" :
                               cint_ctx_state_hash(ctx, h) != CINT_OK || memcmp(h, cr.hash, 32u) != 0 ? "state" : NULL;
            oc->differs = what != NULL;
            if (what != NULL) {
                wb_f(b, "check: entry %u differs: its %s on %s is not its %s on %s\n", (unsigned)ordinal, what,
                     s->backend, what, s->check);
            }
        }
        if (!bad && c17 && (!other || oc->differs)) {   /* the session goes on from cpu-c17's state */
            rec_close(s);
            if (ordinal >= 1u && ordinal <= ITEMS_MAX) s->bnd[ordinal - 1u].seg = 0;
            bad = cint_ctx_restore(ctx, (const uint8_t *)cr.after.p, cr.after.n) != CINT_OK;
            if (bad) wb_s(b, "workbench: the state cpu-c17 left cannot be restored on cpu-sir-interp (SPEC-03 H-19)\n");
        }
        if (c17) {
            cs = cr.st;
            result = cr.result;
        }
        wb_free(&from);
        wb_free(&shown);
        wb_free(&kept);
        wb_free(&cr.after);
        if (bad) {
            free_values(before, nbefore);
            return -1;
        }
        oc->faulted = cs == CINT_FAULT;
        oc->errored = cs == CINT_OK && !c17 && cint_ctx_error(ctx, &set, &etag) == CINT_OK && set != NULL &&
                      etag >= 1u && etag <= set->count;
        oc->rtag = e->types[0];
        oc->has_result = cs == CINT_OK && e->types[0] != 0 && !oc->errored;
        oc->result = result;
        oc->fuel = cr.fuel;
        if (!c17 && cint_fuel_consumed(ctx, &oc->fuel) != CINT_OK) oc->fuel = -1;
        if (cs == CINT_REFUSED) wb_s(b, "workbench: the entry refused the call (SPEC-03 H-12)\n");
    }
    if (read == 0) read = state_out(ctx, prog, table, &after, &nafter);
    if (read != 0) wb_s(b, "workbench: cannot read the entry's module state\n");
    if (oc->errored) {   /* the entry completed with an error result (SPEC-04 LS-221; SPEC-06 3.4a) */
        wb_free(&s->fault);
        wb_f(&s->fault, "error: %s.%s (tag %llu)\n", set->name, set->values[etag - 1u], (unsigned long long)etag);
        wb_put(b, s->fault.p, s->fault.n);
    }
    if (oc->faulted) {
        char state[160], where[WB_NAME_MAX + 96u];
        static cint_fault_record rec;
        if (g_stop && cint_ctx_fault(ctx, &rec) == CINT_OK && rec.code == CINT_E_FUEL) {
            wb_f(b, "interrupted: the allowance of this %s was capped at the %lld units it had consumed\n",
                 keep ? "entry" : "expression", (long long)oc->fuel);
        }
        (void)snprintf(state, sizeof state, "%s; context reset",
                       !keep ? "nothing is kept: the session is as it was" :
                       read == 0 && same_values(before, nbefore, after, nafter) ? "no store completed in this entry" :
                       "stores that completed before the fault remain (SPEC-01 IM-121)");
        if (ordinal == 0) (void)snprintf(where, sizeof where, ":inspect, not an entry");
        else (void)snprintf(where, sizeof where, "%s%s%sentry %u%s", s->cur != 0 ? "branch " : "",   /* BX14-13 */
                            s->cur != 0 ? s->br[s->cur].name : "", s->cur != 0 ? ", " : "", (unsigned)ordinal, s->call);
        wb_buf r = {0};
        report(s, pending, keep, ctx, c17 && oc->differs ? NULL : img, oc->fuel, state, where,
               keep ? s->backend : "cpu-sir-interp", &r);
        wb_free(&s->fault);
        s->fault = r;
        if (!r.failed) wb_put(b, r.p, r.n);
    }
    if (read == 0 && keep) {   /* the values the entry left, for its value line */
        free_values(s->vals, s->nval);
        s->vals = after;
        s->nval = nafter;
    } else if (read == 0) {
        free_values(after, nafter);
    }
    free_values(before, nbefore);
    if (!keep) {
        cint_ctx_destroy(ctx);
        cint_sir_release(img);
    }
    return 0;
}

/* Writes n bytes as the session's source rel, making its directories. Returns 0, or -1. */
static int put_source(session *s, const char *rel, const void *p, size_t n)
{
    char *to = wb_join(s->src, rel), *dir = to != NULL ? wb_copy(to) : NULL;
    char *cut = dir != NULL ? strrchr(dir, '/') : NULL, *cut2 = dir != NULL ? strrchr(dir, '\\') : NULL;
    if (cut2 != NULL && (cut == NULL || cut2 > cut)) cut = cut2;
    if (cut != NULL) *cut = '\0';
    int r = to == NULL || dir == NULL || cli_mkdirs(dir) != 0 || wb_write_file(to, p, n) != 0 ? -1 : 0;
    free(to);
    free(dir);
    return r;
}

/* Writes the revision set of record r, objects of st, into the session's sources. */
static int write_sources(session *s, const wb_store *st, const wb_record *r)
{
    for (uint32_t k = 0; k < r->nmod; k++) {
        const wb_obj *o = wb_store_get(st, r->mod[k].text);
        if (o == NULL || put_source(s, r->mod[k].path, o->p, o->n) != 0) return -1;
    }
    return 0;
}

/* Writes the sources of the live revision again, after a step that wrote others and failed. */
static void rollback_sources(session *s)
{
    if (s->live.nmod != 0) (void)write_sources(s, &s->store, &s->live);
}

/* The text on one line, blanks folded, cut at cap with "...", as a name in value lines. */
static void one_line(const char *text, char *out, size_t cap)
{
    size_t n = 0;
    for (const char *c = skip(text); *c != '\0' && n + 4u < cap; c++) {
        int blank = *c == ' ' || *c == '\t' || *c == '\n' || *c == '\r';
        if (!blank || (n != 0 && out[n - 1u] != ' ')) out[n++] = blank ? ' ' : *c;
    }
    while (n != 0 && out[n - 1u] == ' ') n--;
    if (n + 4u >= cap) {
        memcpy(out + n, "...", 3u);
        n += 3u;
    }
    out[n] = '\0';
}

/* ------------------------------------------------------ Watches (SPEC-06 6.4). */

enum { W_REPORT, W_QUIET, W_FRESH, W_REPLAY };

static void free_watch(watch *w)
{
    free(w->expr);
    free(w->cond);
    free(w->log);
    memset(w, 0, sizeof *w);
}

static void remove_watch(session *s, uint32_t k)
{
    free_watch(&s->w[k]);
    memmove(&s->w[k], &s->w[k + 1u], (s->nw - k - 1u) * sizeof s->w[0]);
    s->nw--;
    s->wepoch++;
}

/* Builds the program of the watches: the session module with a function per watch and per
 * condition. A watch that no longer compiles, after a declaration changed what it names, is
 * removed with its diagnostics. Returns 0, or -1 with the reason in b. */
static int watch_program(session *s, wb_buf *b)
{
    while (s->watch_prog == NULL || s->built_epoch != s->epoch || s->built_wepoch != s->wepoch) {
        int only = 0;
        wb_buf cb = {0};
        cint_sir_program *had = s->prog, *built;
        if (s->nw == 0) return -1;
        s->prog = NULL;   /* the watch program does not become the one compiled last */
        int c = write_module(s, NULL, s->ndecl, "void", NULL, "", 1) == 0 ? compile(s, NULL, &cb, &only) : C_BROKEN;
        built = s->prog;
        s->prog = had;
        if (c == C_OK) {
            cint_sir_program *old = s->watch_prog;
            s->watch_prog = built;
            retire(s, old);
            s->built_epoch = s->epoch;
            s->built_wepoch = s->wepoch;
            wb_free(&cb);
            return 0;
        }
        if (!cb.failed) wb_put(b, cb.p, cb.n);
        wb_free(&cb);
        if (c != C_ERROR || s->other_errors || s->bad_watches == 0) {
            wb_s(b, "workbench: the watches cannot be evaluated\n");
            return -1;
        }
        for (uint32_t k = s->nw; k-- > 0;) {
            if (s->bad_watches & (1u << k)) {
                wb_f(b, "watch %u is removed: it no longer compiles\n", (unsigned)s->w[k].number);
                remove_watch(s, k);
            }
        }
    }
    return 0;
}

/* Calls export `fn` of the session module in x. Returns 1 with the value in *v, or 0 when the
 * call faulted (the fault cleared, its code in *code). */
static int call_unit(const session *s, cint_ctx *x, const cint_observer *obs, const char *fn, change *v,
                     unsigned *code)
{
    static cint_fault_record rec;
    for (uint32_t i = 0; i < obs->entry_count; i++) {
        const cint_observer_entry *e = &obs->entries[i];
        if (e->name_len == strlen(fn) && memcmp(e->name, fn, e->name_len) == 0) {
            uint64_t result = 0;
            g_entry = x;
            cint_status st = e->call(x, s->fuel, s->depth, NULL, &result);
            g_entry = NULL;
            if (st == CINT_OK && e->types[0] != 0) {
                wb_tval tv = {0};
                tval_of(&tv, e->types[0], result);
                v->len = (uint8_t)tv.len;
                memcpy(v->bytes, tv.bytes, tv.len);
                return 1;
            }
            *code = cint_ctx_fault(x, &rec) == CINT_OK && rec.code < 14u ? (unsigned)rec.code : 0u;
            (void)cint_ctx_clear_fault(x);
            v->len = 0;
            return 0;
        }
    }
    *code = 0;
    v->len = 0;
    return 0;
}

static void value_text(const change *v, unsigned code, char *out, size_t cap)
{
    wb_tval tv;
    if (v->len == 0) {
        (void)snprintf(out, cap, "fault[%s]", WB_CODE_NAMES[code]);
        return;
    }
    tv.len = v->len;
    memcpy(tv.bytes, v->bytes, v->len);
    (void)wb_tval_text(&tv, out, cap, 0);
}

/* Evaluates the watches on the state of `from`, a context in the program of from_img, in a
 * context restored from it (BX14-08), at the boundary after entry `ordinal`. W_REPORT reports
 * each watch whose value changed and whose condition holds, at most SHOWN_MAX per watch and
 * command, and logs it; W_REPLAY reports it without logging it again (:replay); W_QUIET only
 * takes the values in, after a restore; W_FRESH also prints the newest watch's first value.
 * Returns 1 when a watch with `stop` reported. */
static int watches_of(session *s, cint_ctx *from, cint_sir_image *from_img, uint32_t ordinal, int mode, wb_buf *b)
{
    cint_sir_image *img = NULL;
    cint_sir_error err;
    cint_ctx *x = NULL;
    int stopped = 0;
    if (s->nw == 0 || g_stop || watch_program(s, b) != 0) return 0;
    if (cint_sir_prepare(s->watch_prog, &img, &err) != CINT_SIR_OK) {
        wb_f(b, "workbench: cpu-sir-interp does not run the watches yet: %s:%u:%u: %s\n", err.path, err.line, err.col,
             err.text);
        return 0;
    }
    if ((x = scratch_of(s, from, from_img, img, b)) == NULL) {
        cint_sir_release(img);
        return 0;
    }
    const cint_observer *obs = cint_sir_observer(img);
    for (uint32_t k = 0; k < s->nw && !g_stop; k++) {
        watch *w = &s->w[k];
        change v;
        char fn[32], text[300], shown[256];
        unsigned code = 0, ccode = 0;
        int holds = 1;
        memset(&v, 0, sizeof v);
        v.ordinal = ordinal;
        (void)snprintf(fn, sizeof fn, "wb_watch_%u", (unsigned)w->number);
        (void)call_unit(s, x, obs, fn, &v, &code);
        if (w->cond != NULL) {
            change c;
            memset(&c, 0, sizeof c);
            (void)snprintf(fn, sizeof fn, "wb_when_%u", (unsigned)w->number);
            holds = call_unit(s, x, obs, fn, &c, &ccode) && c.bytes[1] != 0;
        }
        if (g_stop) {   /* an interrupt leaves this watch and the rest as they were */
            wb_s(b, "interrupted: the watches were not evaluated at this boundary\n");
            break;
        }
        value_text(&v, code, text, sizeof text);
        one_line(w->expr, shown, sizeof shown);
        int changed = !w->known || w->last.len != v.len || memcmp(w->last.bytes, v.bytes, v.len) != 0;
        if (mode == W_FRESH && k + 1u == s->nw) {
            wb_f(b, "watch %u   %s = %s%s%s", (unsigned)w->number, shown, text, v.len != 0 ? " : " : "",
                 v.len != 0 ? SCALAR_NAMES[w->sc] : "");
            if (w->cond != NULL) {
                one_line(w->cond, shown, sizeof shown);
                wb_f(b, "   when %s", shown);
            }
            wb_s(b, w->stop ? "   stop\n" : "\n");
        } else if ((mode == W_REPORT || mode == W_REPLAY) && w->known && changed && holds) {
            if (w->shown < SHOWN_MAX) {
                wb_f(b, "watch %u   entry %u   %s = %s\n", (unsigned)w->number, (unsigned)ordinal, shown, text);
                w->shown++;
            } else {
                w->more++;
            }
            if (mode == W_REPORT && w->nlog == w->caplog && w->caplog < LOG_MAX) {
                change *grown = realloc(w->log, (w->caplog = w->caplog * 2u + 64u) * sizeof *grown);
                if (grown == NULL) w->caplog = w->nlog;
                else w->log = grown;
            }
            if (mode == W_REPORT && w->nlog < w->caplog) w->log[w->nlog++] = v;
            stopped |= w->stop;
        }
        w->last = v;
        w->known = 1;
    }
    cint_ctx_destroy(x);
    cint_sir_release(img);
    return stopped;
}

static int watches(session *s, uint32_t ordinal, int mode, wb_buf *b)
{
    return watches_of(s, s->ctx, s->live_img, ordinal, mode, b);
}

/* After a command: the count of changes each watch did not print (SPEC-06 6.4). */
static void watch_summary(session *s, wb_buf *b)
{
    for (uint32_t k = 0; k < s->nw; k++) {
        if (s->w[k].more != 0) {
            wb_f(b, "  ... %u more changes of watch %u recorded (:watch log %u)\n", (unsigned)s->w[k].more,
                 (unsigned)s->w[k].number, (unsigned)s->w[k].number);
        }
        s->w[k].shown = s->w[k].more = 0;
    }
}

/* -------------------------------------------------------------------- Entries. */

static int find_decl(const session *s, const char *name)
{
    for (uint32_t i = 0; i < s->ndecl; i++) {
        if (strcmp(s->decls[i].name, name) == 0) return (int)i;
    }
    return -1;
}

/* The scalar type of a name the expression uses: a global or, called, a function of the
 * session module, or `alias.name` of a loaded module. 0 when it has none. */
static int type_of_name(const session *s, const char *name, size_t n, int called)
{
    const cint_sir_program *p = s->prog;
    const char *dot = memchr(name, '.', n);
    for (uint32_t k = 0; p != NULL && k < p->nmod; k++) {
        const si_module *m = &p->mods[k];
        const char *want = name;
        size_t wn = n;
        if (dot != NULL) {   /* alias.name: the alias is the last segment of the module's name */
            size_t an = (size_t)(dot - name), dl = m->dotted.len;
            const uint8_t *d = m->text + m->dotted.at;
            if (dl < an || memcmp(d + dl - an, name, an) != 0 || (dl > an && d[dl - an - 1u] != '.')) continue;
            want = dot + 1;
            wn = n - an - 1u;
        } else if (strcmp(m->path, "session.ci") != 0) {
            continue;
        }
        for (uint32_t g = 0; !called && g < m->nglobal; g++) {
            if (m->globals[g].name.len == wn && memcmp(m->text + m->globals[g].name.at, want, wn) == 0)
                return m->globals[g].scalar;
        }
        for (uint32_t f = 0; called && f < m->nfunc; f++) {
            const si_func *fn = &m->funcs[f];
            if (fn->kind == FN_FUNC && fn->name.len == wn && memcmp(m->text + fn->name.at, want, wn) == 0 &&
                fn->result.kind == TK_SCALAR) {
                return fn->result.scalar;
            }
        }
    }
    return 0;
}

/* 1 when an operator outside brackets makes the expression a Bool: a comparison, `&&`, `||`,
 * or `!` (the shifts `<<` and `>>` are not comparisons). */
static int boolish(const char *expr)
{
    int depth = 0;
    tok t;
    for (const char *p = lex(expr, &t); t.kind != T_END; p = lex(p, &t)) {
        char c = t.kind == T_PUNCT ? t.p[0] : '\0', next = c != '\0' ? t.p[1] : '\0';
        char before = t.p > expr ? t.p[-1] : '\0';
        depth += c == '(' || c == '[' || c == '{';
        depth -= c == ')' || c == ']' || c == '}';
        if (depth == 0 && ((c == '=' && next == '=' && before != '=') || c == '!' ||
                           ((c == '<' || c == '>') && next != c && before != c) ||
                           ((c == '&' || c == '|') && next == c))) {
            return 1;
        }
    }
    return 0;
}

/* The candidate types of an expression, in the order the session tries them (above). */
static uint32_t candidates(const session *s, const char *expr, int *out)
{
    uint32_t n = 0;
    tok t, u;
    int after_as = 0;
    if (boolish(expr)) out[n++] = SC_BOOL;
    for (const char *p = lex(expr, &t); t.kind != T_END && n < CANDIDATES - 2u; p = lex(p, &t)) {
        int sc = 0;
        if (t.kind != T_ID) {
            after_as = 0;
            continue;
        }
        if (after_as) {
            for (int k = 1; k < 10; k++) sc = is(&t, SCALAR_NAMES[k]) ? k : sc;
        } else {
            char name[IDENT_MAX];
            size_t len = t.n;
            const char *q = p;
            if (len >= IDENT_MAX) continue;
            memcpy(name, t.p, len);
            while (q = lex(q, &u), u.kind == T_PUNCT && u.p[0] == '.') {   /* alias.name */
                q = lex(q, &u);
                if (u.kind != T_ID || len + 1u + u.n >= IDENT_MAX) break;
                name[len++] = '.';
                memcpy(name + len, u.p, u.n);
                len += u.n;
                p = q;
            }
            sc = type_of_name(s, name, len, u.kind == T_PUNCT && u.p[0] == '(');
        }
        after_as = is(&t, "as");
        for (uint32_t k = 0; sc != 0 && k < n; k++) sc = out[k] == sc ? 0 : sc;
        if (sc != 0) out[n++] = sc;
    }
    for (int k = SC_I64; k != 0; k = k == SC_I64 ? SC_BOOL : 0) {
        int seen = 0;
        for (uint32_t i = 0; i < n; i++) seen |= out[i] == k;
        if (!seen) out[n++] = k;
    }
    return n;
}

/* Compiles an expression in the entry unit with each candidate type until one compiles.
 * Returns C_OK with *sc the type, or C_ERROR or C_BROKEN with the first attempt's report. */
static int typed(session *s, const char *expr, const char *path, int *sc, wb_buf *b)
{
    int cand[CANDIDATES], only = 0, first = C_ERROR;
    uint32_t n = candidates(s, expr, cand);
    wb_buf firstb = {0};
    for (uint32_t i = 0; i < n; i++) {
        wb_buf tb = {0};
        if (write_module(s, NULL, s->ndecl, SCALAR_NAMES[cand[i]], expr, path, 0) != 0) {
            wb_s(b, "workbench: cannot write the session module\n");
            wb_free(&firstb);
            return C_BROKEN;
        }
        int r = compile(s, NULL, &tb, &only);
        if (r == C_OK) {
            *sc = cand[i];
            wb_free(&tb);
            wb_free(&firstb);
            return C_OK;
        }
        if (i == 0) {
            firstb = tb;
            first = r;
        } else {
            wb_free(&tb);
        }
        if (r == C_BROKEN || !only) {
            break;   /* not a type mismatch of the whole expression */
        }
    }
    if (!firstb.failed && firstb.n != 0) wb_put(b, firstb.p, firstb.n);
    wb_free(&firstb);
    return first;
}

static void note_outcome(session *s, uint32_t ordinal, const char *what)
{
    if (ordinal >= 1u && ordinal <= ITEMS_MAX) s->outcome[ordinal - 1u] = what;
}

/* A copy of text with every byte before `upto` that is not a line break made a space, so
 * that what follows keeps its line and column. */
static char *blanked(const char *text, const char *upto)
{
    char *c = wb_copy(text);
    for (size_t i = 0; c != NULL && text + i < upto; i++) c[i] = c[i] == '\n' || c[i] == '\r' ? c[i] : ' ';
    return c;
}

static void add_hist(session *s, const char *name, uint32_t rev, uint32_t entry)
{
    hist *h = room(s->hist, s->nhist, &s->caphist, sizeof *h);
    if (h == NULL) return;
    s->hist = h;
    (void)snprintf(s->hist[s->nhist].name, IDENT_MAX, "%s", name);
    s->hist[s->nhist].rev = rev;
    s->hist[s->nhist++].entry = entry;
}

static void declaration(session *s, uint32_t ordinal, const char *text, int kind, const char *name,
                        const char *name_at, const char *init, wb_buf *b)
{
    decl d;
    char *body = NULL;
    int at = find_decl(s, name), only = 0;
    uint32_t where = at >= 0 ? (uint32_t)at : s->ndecl;
    memset(&d, 0, sizeof d);
    (void)snprintf(d.name, sizeof d.name, "%s", name);
    d.kind = kind;
    d.entry = ordinal;
    d.rev = s->rev_max + 1u;
    if (strcmp(name, "wb_entry") == 0) {
        wb_s(b, "workbench: the name wb_entry is the session's own; choose another\n");
        note_outcome(s, ordinal, "refused");
        return;
    }
    if (at < 0 && s->ndecl == DECLS_MAX) {
        wb_f(b, "workbench: the session module holds at most %u declarations\n", DECLS_MAX);
        note_outcome(s, ordinal, "refused");
        return;
    }
    int literal = kind == K_VAR && init != NULL && *skip(init + 1) == '[';
    if (literal) {
        init = NULL;   /* an array literal is a constant initializer, which stays in the module (MLA) */
    }
    if (kind == K_VAR && init != NULL) {
        /* the module declares the variable; its initializer runs in this entry (BX14-08) */
        size_t n = (size_t)(init - text);
        while (n > 0 && (text[n - 1u] == ' ' || text[n - 1u] == '\t')) n--;
        d.text = malloc(n + 2u);
        body = blanked(text, name_at);
        if (d.text != NULL) {
            memcpy(d.text, text, n);
            memcpy(d.text + n, ";", 2u);
        }
    } else {
        int stmts = 0;
        d.text = wb_copy(text);
        body = kind == K_SCRIPT ? script_part(text, 1, &stmts) : NULL;   /* its statements run as one entry */
    }
    char path[WB_PATH_MAX + 1u];
    if (kind == K_SCRIPT) (void)snprintf(path, sizeof path, "%s", name);
    else (void)snprintf(path, sizeof path, "entry %u", (unsigned)ordinal);
    if (d.text == NULL || ((kind == K_SCRIPT || (kind == K_VAR && init != NULL)) && body == NULL) ||
        write_module(s, &d, where, "void", body, path, 0) != 0) {
        wb_s(b, "workbench: cannot write the session module\n");
        free(d.text);
        free(body);
        note_outcome(s, ordinal, "refused");
        return;
    }
    size_t mark = b->n, len = 0;
    int r = compile(s, &d, b, &only);
    char *said = r == C_ERROR && kind == K_VAR && body != NULL ? wb_read_file(s->log, &len) : NULL;
    if (said != NULL && strstr(said, "\"C2054\"") != NULL) {   /* LS-91, LS-314: no zero fill (BX14-08) */
        b->n = mark;
        free(d.text);
        init = NULL;
        r = (d.text = wb_copy(text)) == NULL || write_module(s, &d, where, "void", NULL, path, 0) != 0
                ? C_BROKEN : compile(s, &d, b, &only);
    }
    free(said);
    free(body);
    if (r != C_OK) {
        free(d.text);
        note_outcome(s, ordinal, r == C_ERROR ? "compile error" : r == C_STOPPED ? "interrupted" : "not run");
        return;
    }
    uint32_t replaced = at >= 0 ? s->decls[at].rev : 0;
    if (at >= 0) free(s->decls[at].text);
    s->decls[where] = d;
    s->ndecl += at < 0;
    s->rev = ++s->rev_max;
    s->epoch++;
    add_hist(s, name, s->rev, ordinal);
    outcome oc;
    if (run(s, NULL, "session.ci", kind == K_SCRIPT || (kind == K_VAR && init != NULL) ? "wb_entry" : NULL, NULL,
            ordinal, 1, &oc, b) != 0) {
        note_outcome(s, ordinal, "not run");
        return;
    }
    note_outcome(s, ordinal, oc.faulted ? "fault" : kind == K_SCRIPT ? "loaded" : "declared");
    if (kind == K_VAR && !oc.faulted) {
        value *v = find_value(s, "session.ci", 10u, name, strlen(name));
        if (v != NULL) entry_value(b, name, v->tag, v->bits, &oc);
        else if (!inspect_array(s, name, 0, 0, NULL, b))
            wb_f(b, "%s : not shown by the session (it shows integers, Bool, and arrays of them)\n", name);
        else if (literal && at >= 0)
            wb_s(b, "  (a redeclaration of the same type keeps the elements (I6); an assignment changes them)\n");
    } else if (kind == K_FUNC) {
        wb_f(b, "%s : ", name);
        if (wb_signature(b, s->prog, "session.ci", name) != 0) wb_s(b, "function");
        wb_f(b, "   revision session@r%u", (unsigned)s->rev);
        if (replaced != 0) wb_f(b, " (replaces r%u)", (unsigned)replaced);
        wb_s(b, "\n");
    } else if (kind == K_OTHER || kind == K_SCRIPT) {
        wb_f(b, "%s   revision session@r%u\n", name, (unsigned)s->rev);
    }
    (void)watches(s, ordinal, W_REPORT, b);
}

static void statement(session *s, uint32_t ordinal, const char *text, int kind, wb_buf *b)
{
    char path[32], shown[2u * IDENT_MAX] = "";
    int sc = 0, only = 0, r;
    wb_buf ck = {0};
    wb_view av = {0};
    int array = kind == K_EXPR && array_named(s, text, &ck, &av, shown) == 1;
    wb_free(&ck);
    if (array) {   /* an array's name: the entry runs nothing, and its value is the array */
        kind = K_STMT;
        text = "";
    }
    (void)snprintf(path, sizeof path, "entry %u", (unsigned)ordinal);
    if (kind == K_EXPR) {
        /* an expression with no value, such as a call of a void function, runs as a statement */
        wb_buf tb = {0}, sb = {0};
        char *stmt = NULL;
        r = typed(s, text, path, &sc, &tb);
        if (r == C_ERROR && (stmt = malloc(strlen(text) + 2u)) != NULL) {
            memcpy(stmt, text, strlen(text));
            memcpy(stmt + strlen(text), ";", 2u);
            if (write_module(s, NULL, s->ndecl, "void", stmt, path, 0) == 0 && compile(s, NULL, &sb, &only) == C_OK) {
                r = C_OK;
                kind = K_STMT;
            }
        }
        if (r != C_OK && !tb.failed) wb_put(b, tb.p, tb.n);
        free(stmt);
        wb_free(&tb);
        wb_free(&sb);
    } else if (write_module(s, NULL, s->ndecl, "void", text, path, 0) != 0) {
        wb_s(b, "workbench: cannot write the session module\n");
        r = C_BROKEN;
    } else {
        r = compile(s, NULL, b, &only);
    }
    if (r != C_OK) {
        note_outcome(s, ordinal, r == C_ERROR ? "compile error" : r == C_STOPPED ? "interrupted" : "not run");
        return;
    }
    outcome oc;
    if (run(s, NULL, "session.ci", "wb_entry", NULL, ordinal, 1, &oc, b) != 0) {
        note_outcome(s, ordinal, "not run");
        return;
    }
    note_outcome(s, ordinal, oc.faulted ? "fault" : oc.errored ? "error" : kind == K_EXPR || array ? "value" : "done");
    if (oc.has_result) entry_value(b, NULL, oc.rtag, oc.result, &oc);
    else if (array && !oc.faulted) (void)inspect_array(s, shown, 0, 0, NULL, b);
    (void)watches(s, ordinal, W_REPORT, b);
}

/* ----------------------------------------------------- Commands (SPEC-06 5.3). */

static const char HELP[] =
    "Entries: a declaration (I64 x = 1;), a statement (x = x + 1;), or an expression (x * 2).\n"
    "Commands:\n"
    "  :load FILE.ci        load a module under the root and import it into the session\n"
    "  :reload              read the loaded modules again\n"
    "  :run F(ARGS) [N] [--check B]   call export F of the session N times, each call an entry; --check\n"
    "                       runs each call on backend B too and stops where the two differ\n"
    "  :inspect EXPR [/F]   the exact value of a pure expression (/raw /hex /bin /bytes /group /scale N /all)\n"
    "                       or of an array (/grid [r0..r1, c0..c1] /stats /view)\n"
    "  :type EXPR           its type without running it\n"
    "  :explain [NAME]      what the compiler knows about the session's functions (cint explain)\n"
    "  :fault [/path]       the last fault report or error result; /path, the path the error took\n"
    "  :fuel N | none       the per-entry fuel allowance (default none)\n"
    "  :depth N             the per-entry depth limit (default 256)\n"
    "  :backend ID          where entries run: cpu-sir-interp or cpu-c17\n"
    "  :history NAME        the revisions of a declaration, and the checkpoints that hold them\n"
    "  :log                 the entries of this session\n"
    "  :checkpoint NAME     keep the current state as checkpoint NAME\n"
    "  :restore NAME        make checkpoint NAME current; the current state is kept first, as auto-N\n"
    "  :undo                go back to the state before the last entry\n"
    "  :branch NAME         start branch NAME at the current state\n"
    "  :branches            the branches and their checkpoints\n"
    "  :diff NAME [BINDING] the current state against checkpoint NAME, binding by binding\n"
    "  :watch EXPR [when COND] [stop]   print EXPR after each entry that changes it\n"
    "  :watch log N         the changes watch N recorded\n"
    "  :watches             the watches; :unwatch N removes one\n"
    "  :goto N | fault      make the state at the end of entry N, or of the last fault, current, by replay\n"
    "  :trace [N | fault]   the counted operations of entry N, by replay (default: the last fault)\n"
    "  :replay NAME         replay the entries since checkpoint NAME, with the watches at each boundary\n"
    "  :replay NAME --changed BINDING   the last entry since NAME that changed global BINDING (backward scan)\n"
    "  :replay NAME --when PRED   the first entry since NAME after which PRED holds; PRED monotone (bisection)\n"
    "  :effects             the recorded entries, prints, and outcomes that led to the current state\n"
    "  :save PATH           write the session to the directory PATH\n"
    "  :save-case PATH      write a case of the entries since the last checkpoint (CINT-CASE-1)\n"
    "  :open PATH           open the session saved in PATH\n"
    "  :help, :quit [--yes]\n"
    "An interrupt (Ctrl+C) stops the running entry with E_FUEL, or stops a compile, which changes nothing.\n";

static int parse_count(const char *t, int64_t lo, int64_t *out)
{
    char *end;
    if (*t < '0' || *t > '9') return -1;
    long long v = strtoll(t, &end, 10);
    if (*end != '\0' || v < lo) return -1;
    *out = (int64_t)v;
    return 0;
}

/* A constant argument of `:run`: an integer literal, or true or false, in the parameter's type. */
static int argument(const char *t, uint32_t tag, uint64_t *bits)
{
    if (tag == CINT_TAG_BOOL) {
        *bits = strcmp(t, "true") == 0;
        return strcmp(t, "true") == 0 || strcmp(t, "false") == 0 ? 0 : -1;
    }
    int neg = *t == '-';
    const char *d = t + neg;
    uint64_t v = 0;
    unsigned w = tag_width(tag);
    if (*d < '0' || *d > '9') return -1;
    for (; *d >= '0' && *d <= '9'; d++) {
        unsigned digit = (unsigned)(*d - '0');
        if (v > (UINT64_MAX - digit) / 10u) return -1;
        v = v * 10u + digit;
    }
    if (*d != '\0') return -1;
    uint64_t span = w == 8u ? UINT64_MAX : (1ull << (8u * w)) - 1u;
    if (tag >= CINT_TAG_U8) {
        if (neg || v > span) return -1;
        *bits = v;
        return 0;
    }
    uint64_t max = span >> 1;   /* the signed maximum; its minimum is -(max + 1) */
    if ((!neg && v > max) || (neg && v > max + 1u)) return -1;
    *bits = neg ? (uint64_t)0 - v : v;
    *bits &= span;
    return 0;
}

static void cmd_run(session *s, uint32_t first, const char *args, wb_buf *b)
{
    char call[1024], *open, *close;
    int64_t count = 1;
    (void)snprintf(call, sizeof call, "%s", args);
    open = strchr(call, '(');
    close = strrchr(call, ')');
    if (open == NULL || close == NULL || close < open) {
        wb_s(b, ":run takes F(ARGS) [N], with constant arguments\n");
        return;
    }
    char *w[4], *p = close + 1;
    const char *check = NULL;
    int nw = 0, counted = 0, usage = 0;
    while (!usage && *(p += strspn(p, " \t")) != '\0') {
        usage = nw == 4;
        w[nw < 4 ? nw++ : 0] = p;
        p += strcspn(p, " \t");
        if (*p != '\0') *p++ = '\0';
    }
    for (int i = 0; !usage && i < nw; i++) {
        if (strcmp(w[i], "--device") == 0) {
            wb_s(b, ":run --device " NO_DEVICE);
            return;
        } else if (strcmp(w[i], "--check") == 0 && i + 1 < nw) {
            check = w[++i];
        } else {
            usage = counted++ != 0 || parse_count(w[i], 1, &count) != 0;
        }
    }
    if (usage) {
        wb_s(b, ":run takes F(ARGS) [N] [--check BACKEND]; N is a whole number of calls, at least 1\n");
        return;
    }
    if (check != NULL && strcmp(check, "cpu-sir-interp") != 0 && strcmp(check, "cpu-c17") != 0) {
        wb_f(b, ":run --check: no backend %s (cpu-sir-interp, cpu-c17)\n", check);
        return;
    }
    if (check != NULL && strcmp(check, s->backend) == 0) {
        wb_f(b, ":run --check names a second backend; %s is the one in force\n", check);
        return;
    }
    *open = *close = '\0';
    char *fn = trim(call);
    if (strchr(fn, '.') != NULL) {
        wb_f(b, ":run calls an export of the session module; call %s from an entry, as %s(...), until the "
                "interpreter lists the exports of every module\n", fn, fn);
        return;
    }
    int only = 0;
    if (write_module(s, NULL, s->ndecl, "void", NULL, "", 0) != 0 || compile(s, NULL, b, &only) != C_OK) return;
    /* the arguments, against the function's signature in the observer */
    cint_sir_image *img = NULL;
    cint_sir_error err;
    uint64_t argv[16];
    uint32_t nargs = 0;
    if (cint_sir_prepare(s->prog, &img, &err) != CINT_SIR_OK) {
        wb_f(b, "workbench: cpu-sir-interp does not run this yet: %s:%u:%u: %s\n", err.path, err.line, err.col,
             err.text);
        return;
    }
    const cint_observer *obs = cint_sir_observer(img);
    const cint_observer_entry *e = NULL;
    for (uint32_t i = 0; i < obs->entry_count; i++) {
        const cint_module *m = obs->program->modules[obs->entries[i].module];
        if (obs->entries[i].name_len == strlen(fn) && memcmp(obs->entries[i].name, fn, strlen(fn)) == 0 &&
            m->path_len == 10u && memcmp(m->path, "session.ci", 10u) == 0) {
            e = &obs->entries[i];
        }
    }
    const char *why = e == NULL ? "is not an export of the session module (declare it with export)" : NULL;
    for (char *a = open + 1, *next; why == NULL && *skip(a) != '\0'; a = next) {
        next = strchr(a, ',');
        next = next != NULL ? (*next = '\0', next + 1) : a + strlen(a);
        a = trim(a);
        if (nargs == 16u || nargs >= e->types[1] || argument(a, e->types[2u + nargs], &argv[nargs]) != 0)
            why = nargs >= e->types[1] ? "takes fewer arguments" : "takes literal arguments of its parameter types";
        nargs++;
    }
    if (why == NULL && nargs != e->types[1]) why = "takes more arguments";
    cint_sir_release(img);
    if (why != NULL) {
        wb_f(b, ":run: %s %s\n", fn, why);
        return;
    }
    for (int64_t k = 0; k < count; k++) {
        if (g_stop) {   /* an interrupt between calls ends the :run there */
            wb_f(b, "interrupted: %lld of %lld calls made\n", (long long)k, (long long)count);
            return;
        }
        if (k != 0 && s->next > ITEMS_MAX) {
            wb_f(b, "workbench: a session holds at most %u entries; %lld of %lld calls made\n", ITEMS_MAX,
                 (long long)k, (long long)count);
            return;
        }
        uint32_t ordinal = k == 0 ? first : s->next++;
        outcome oc;
        if (ordinal != first) s->items[ordinal - 1u] = wb_copy(s->items[first - 1u]);
        s->check = check;
        if (count > 1) (void)snprintf(s->call, sizeof s->call, ", call %lld of %lld", (long long)k + 1, (long long)count);
        int r = run(s, NULL, "session.ci", fn, argv, ordinal, 1, &oc, b);
        s->check = NULL;
        s->call[0] = '\0';
        if (r != 0) {
            note_outcome(s, ordinal, "not run");
            return;
        }
        if (oc.differs) {   /* --check stops at the first difference */
            note_outcome(s, ordinal, oc.faulted ? "fault" : oc.errored ? "error" : "value");
            wb_f(b, "%lld of %lld calls made\n", (long long)k + 1, (long long)count);
            return;
        }
        note_outcome(s, ordinal, oc.faulted ? "fault" : oc.errored ? "error" : "value");
        if (oc.has_result) entry_value(b, NULL, oc.rtag, oc.result, &oc);
        int stop = watches(s, ordinal, W_REPORT, b);
        if (check != NULL && (oc.faulted || stop || k + 1 == count)) {
            wb_f(b, "check: %s and %s agree at %lld boundar%s\n", s->backend, check, (long long)k + 1,
                 k == 0 ? "y" : "ies");
        }
        if (oc.faulted) {
            if (count > 1) wb_f(b, "%lld of %lld calls made\n", (long long)k + 1, (long long)count);
            return;
        }
        if (stop && k + 1 < count) {   /* SPEC-06 6.4: the remaining calls are not made */
            wb_f(b, "a watch stops :run: %lld of %lld calls made\n", (long long)k + 1, (long long)count);
            return;
        }
        sayf(s, b);
    }
}

/* Copies the modules of root/rel into the session's sources. */
static int copy_in(session *s, const char *rel, wb_buf *b)
{
    wb_modlist ml;
    wb_buf err = {0};
    if (wb_modules(s->o->root, rel, &ml, &err) != 0) {
        wb_f(b, ":load: %s\n", err.n != 0 ? err.p : "out of memory");
        wb_free(&err);
        return -1;
    }
    int r = 0;
    for (uint32_t i = 0; i < ml.n && r == 0; i++) {
        size_t len = 0;
        char *from = wb_join(s->o->root, ml.path[i]);
        char *text = from != NULL ? wb_read_file(from, &len) : NULL;
        if (strcmp(ml.path[i], "session.ci") == 0) {
            wb_s(b, ":load: a module of the root named session.ci collides with the session module\n");
            r = -1;
        } else if (text == NULL || put_source(s, ml.path[i], text, len) != 0) {
            wb_f(b, ":load: cannot copy %s into the session\n", ml.path[i]);
            r = -1;
        }
        free(from);
        free(text);
    }
    wb_modules_free(&ml);
    return r;
}

static void cmd_load(session *s, uint32_t ordinal, const char *arg, int reload, wb_buf *b)
{
    char rel[1024];
    int only = 0;
    uint32_t had = s->nloaded;
    (void)snprintf(rel, sizeof rel, "%s", arg);
    for (char *c = rel; *c; c++) *c = *c == '\\' ? '/' : *c;
    if (!reload) {
        size_t n = strlen(rel);
        if (!wb_path_ok(rel)) {
            wb_s(b, ":load takes FILE.ci, a module or script path under the root\n");
            note_outcome(s, ordinal, "refused");
            return;
        }
        char *file = wb_join(s->o->root, rel), *text = file != NULL ? wb_read_file(file, &n) : NULL;
        int stmts = 0;
        free(file);
        free(text != NULL ? script_part(text, 1, &stmts) : NULL);
        if (stmts > 0) {   /* a script (LS-218, BX14-04) */
            (void)snprintf(s->scratch_path, sizeof s->scratch_path, "%.*s", (int)WB_PATH_MAX, rel);   /* its positions */
            s->scratch = text;
            declaration(s, ordinal, text, K_SCRIPT, rel, NULL, NULL, b);
            s->scratch = NULL;
        }
        free(text);
        if (stmts > 0) return;
        int known = 0;
        for (uint32_t i = 0; i < s->nloaded; i++) known |= strcmp(s->loaded[i], rel) == 0;
        if (!known && (s->nloaded == LOADED_MAX || (s->loaded[s->nloaded] = wb_copy(rel)) == NULL)) {
            wb_f(b, ":load: the session loads at most %u modules\n", LOADED_MAX);
            note_outcome(s, ordinal, "refused");
            return;
        }
        s->nloaded += !known;
    }
    int r = 0;
    for (uint32_t i = 0; i < s->nloaded && r == 0; i++) r = copy_in(s, s->loaded[i], b);
    if (r == 0 && (write_module(s, NULL, s->ndecl, "void", NULL, "", 0) != 0 || compile(s, NULL, b, &only) != C_OK))
        r = -1;
    uint8_t was[LOADED_MAX][32];   /* each loaded module's source in the live revision */
    for (uint32_t i = 0; i < s->nloaded; i++) {
        memset(was[i], 0, 32u);
        for (uint32_t k = 0; k < s->live.nmod; k++) {
            if (strcmp(s->live.mod[k].path, s->loaded[i]) == 0) memcpy(was[i], s->live.mod[k].text, 32u);
        }
    }
    outcome oc;
    if (r == 0 && run(s, NULL, "session.ci", NULL, NULL, ordinal, 1, &oc, b) != 0) r = -1;
    if (r != 0) {
        while (s->nloaded > had) {   /* a load that fails changes nothing */
            free(s->loaded[--s->nloaded]);
        }
        rollback_sources(s);
        note_outcome(s, ordinal, g_stop ? "interrupted" : "not loaded");
        return;
    }
    for (uint32_t i = 0; i < s->nloaded; i++) {   /* a module whose source changed makes a revision */
        if (i >= had) s->lrev[i] = 1;
        for (uint32_t k = 0; i < had && k < s->live.nmod; k++) {
            if (strcmp(s->live.mod[k].path, s->loaded[i]) == 0 && memcmp(s->live.mod[k].text, was[i], 32u) != 0)
                s->lrev[i]++;
        }
        if (i < s->live.nloaded) s->live.loaded[i].rev = s->lrev[i];
    }
    s->epoch++;
    note_outcome(s, ordinal, reload ? "reloaded" : "loaded");
    for (uint32_t k = 0; s->prog != NULL && k + 1u < s->prog->nmod; k++) {
        const si_module *m = &s->prog->mods[k];
        wb_f(b, "%s %.*s (%s)\n", reload ? "reloaded" : "loaded", (int)m->dotted.len, (const char *)m->text + m->dotted.at,
             m->path);
    }
    (void)watches(s, ordinal, W_REPORT, b);
}

/* Global NAME of ctx's state (wb.h). */
int wb_global_named(cint_ctx *ctx, const char *name, wb_tval *tv, char *shown, size_t cap, wb_buf *all)
{
    wb_buf ck = {0};
    cint_checkpoint_info info;
    cint_checkpoint_global g;
    size_t cursor = 0, n = strlen(name);
    int found = 0;
    memset(&info, 0, sizeof info);
    tv->len = 0;
    if (ctx == NULL || ckpt_of(ctx, &ck) != CINT_OK ||
        cint_checkpoint_decode((const uint8_t *)ck.p, ck.n, &info) != CINT_OK) {
        wb_free(&ck);
        return -1;
    }
    while (cint_checkpoint_global_next(&info, &cursor, &g) == CINT_OK) {
        size_t len = g.name.len;
        if (len == n ? memcmp(g.name.bytes, name, n) == 0 :
            len > n && g.name.bytes[len - n - 1u] == '.' && memcmp(g.name.bytes + len - n, name, n) == 0) {
            if (all != NULL) wb_f(all, "%s%.*s", found != 0 ? ", " : "", (int)len, g.name.bytes);
            if (found++ == 0) {
                (void)snprintf(shown, cap, "%.*s", (int)len, g.name.bytes);
                tv->len = g.value.len;
                memcpy(tv->bytes, g.value.bytes, g.value.len);
            }
        }
    }
    wb_free(&ck);
    return found;
}

/* Whether NAME names one global of the current state; when not, why in b. */
static int one_global(session *s, const char *name, wb_tval *tv, char *shown, size_t cap, wb_buf *b)
{
    wb_buf all = {0};
    int found = wb_global_named(s->ctx, name, tv, shown, cap, &all), ok = found == 1 && !all.failed;
    if (found < 0 || all.failed) {
        wb_s(b, "workbench: the current state cannot be read\n");
    } else if (found != 1) {
        wb_f(b, found == 0 ? "no global %s in the current state\n" : "%s names globals %s; give one with its module\n",
             name, all.p);
    }
    wb_free(&all);
    return ok;
}

/* `:inspect NAME [/F]` and `:type NAME` in a debug session, which has no source to compile an
 * expression in: global NAME of the current state. */
static void debug_global(session *s, const char *name, int type_only, const wb_formats *fmt, wb_buf *b)
{
    wb_buf err = {0};
    wb_tval tv;
    char shown[320], text[300];
    if (!one_global(s, name, &tv, shown, sizeof shown, b)) {
        wb_s(b, "  (a debug session reads globals: it has no source to compile an expression in)\n");
    } else if (type_only) {
        (void)wb_tval_text(&tv, text, sizeof text, 1);
        wb_f(b, "%s : %.*s\n", shown, (int)strcspn(text, " "), text);
    } else if (wb_inspect_scalar(b, shown, &tv, fmt, &err) != 0) {
        wb_f(b, ":inspect: %s\n", err.n != 0 ? err.p : "out of memory");
    }
    wb_free(&err);
}

static void inspect(session *s, char *arg, int type_only, wb_buf *b)
{
    /* the expression, then the format words: the first `/` after a blank that starts a format
     * name begins them, so `a / b` stays an expression */
    static const char *const FORMATS[] = {"raw", "hex", "bin", "bytes", "group", "scale", "all", "grid", "stats",
                                          "view"};
    char *words[16];
    int nw = 0;
    char *slash = NULL;
    for (char *c = arg; *c && slash == NULL; c++) {
        size_t n = 0;
        while (c[0] == '/' && id_char((unsigned char)c[1 + n], 0)) n++;
        for (size_t k = 0; n != 0 && c > arg && (c[-1] == ' ' || c[-1] == '\t') && k < 10u; k++)
            slash = strlen(FORMATS[k]) == n && memcmp(c + 1, FORMATS[k], n) == 0 ? c : slash;
    }
    if (slash != NULL) {
        slash[-1] = '\0';
        for (char *w = slash; *w != '\0' && nw < 16;) {   /* blank-separated words */
            words[nw++] = w;
            w += strcspn(w, " \t");
            while (*w == ' ' || *w == '\t') *w++ = '\0';
        }
    }
    wb_formats fmt;
    wb_buf err = {0};
    const char *expr = skip(arg);
    if (*expr == '\0') {
        wb_f(b, ":%s takes an expression\n", type_only ? "type" : "inspect");
        return;
    }
    if (inspect_array(s, expr, type_only, nw, words, b)) return;
    if (wb_formats_parse(nw, (const char *const *)words, &fmt, &err) != 0) {
        wb_f(b, ":inspect: %s\n", err.n != 0 ? err.p : "out of memory");
        wb_free(&err);
        return;
    }
    if (s->o->debug != NULL) {
        debug_global(s, expr, type_only, &fmt, b);
        return;
    }
    int sc = 0;
    if (typed(s, expr, "inspect", &sc, b) != C_OK) return;
    char shown[256];   /* the expression on one line, as its name in the value lines */
    one_line(expr, shown, sizeof shown);
    if (type_only) {
        wb_f(b, "%s : %s\n", shown, SCALAR_NAMES[sc]);
        return;
    }
    if (wb_effect(s->prog, "session.ci", "wb_entry") != 0) {
        wb_s(b, ":inspect takes a pure expression: this one prints or calls a function that does\n");
        return;
    }
    outcome oc;
    if (run(s, NULL, "session.ci", "wb_entry", NULL, 0, 0, &oc, b) != 0 || !oc.has_result) return;
    wb_tval tv;
    tval_of(&tv, oc.rtag, oc.result);
    if (wb_inspect_scalar(b, shown, &tv, &fmt, &err) != 0)
        wb_f(b, ":inspect: %s\n", err.n != 0 ? err.p : "out of memory");
    wb_free(&err);
}

static void cmd_explain(session *s, char *arg, wb_buf *b)
{
    int only = 0;
    char module[1100] = "session.ci", name[IDENT_MAX];
    const char *dot = strrchr(arg, '.');
    (void)snprintf(name, sizeof name, "%s", dot != NULL ? dot + 1 : arg);
    if (write_module(s, NULL, s->ndecl, "void", NULL, "", 0) != 0 || compile(s, NULL, b, &only) != C_OK) return;
    if (dot != NULL) {   /* alias.name: a loaded module's function */
        size_t an = (size_t)(dot - arg);
        module[0] = '\0';
        for (uint32_t k = 0; k + 1u < s->prog->nmod; k++) {
            const si_module *m = &s->prog->mods[k];
            const char *d = (const char *)m->text + m->dotted.at;
            size_t dl = m->dotted.len;
            if (dl >= an && memcmp(d + dl - an, arg, an) == 0 && (dl == an || d[dl - an - 1u] == '.'))
                (void)snprintf(module, sizeof module, "%s", m->path);
        }
    }
    wb_buf err = {0};
    static int64_t bytes[WB_MODULES_MAX];
    wb_source_sizes(s->prog, s->src, bytes);
    if (wb_explain(b, s->prog, module, *name != '\0' ? name : NULL, s->prog->nmod <= WB_MODULES_MAX ? bytes : NULL, 0,
                   &err) != 0) {
        wb_f(b, ":explain: %s\n", err.n != 0 ? err.p : "out of memory");
    }
    wb_free(&err);
}

/* ------- History: checkpoints, restore, undo, and branches (SPEC-06 5.4, 5.5). */

static int32_t find_ckpt(const session *s, const char *name)
{
    for (uint32_t i = 0; *name != '\0' && i < s->nck; i++) {
        if (s->ck[i].number != 0 && strcmp(s->ck[i].name, name) == 0) return (int32_t)i;
    }
    return -1;
}

static const char *ckpt_name(const session *s, int32_t k)
{
    return k >= 0 ? s->ck[k].name : "the start";
}

/* The name an entry uses for a global of a checkpoint: `x` for the session module's, and
 * `acc.total` for one of module lib/acc.ci. */
static const char *shown_name(const char *q, size_t n, size_t *len)
{
    size_t at = 0, dots = 0;
    if (n > 8u && memcmp(q, "session.", 8u) == 0) {
        *len = n - 8u;
        return q + 8u;
    }
    for (size_t i = n; i-- > 0;) {
        if (q[i] == '.' && ++dots == 2u) {
            at = i + 1u;
            break;
        }
    }
    *len = n - at;
    return q + at;
}

/* The globals of the state a record holds, decoded from the store (their bytes stay there);
 * none for the state before the first entry. Returns 0, or -1 with the reason in b. */
static int globals_of(session *s, const uint8_t record[32], cint_checkpoint_global **out, size_t *n,
                      cint_checkpoint_info *info, wb_buf *b)
{
    wb_record r;
    const wb_obj *o = wb_store_get(&s->store, record), *ck;
    *out = NULL;
    *n = 0;
    memset(info, 0, sizeof *info);
    if (o == NULL || wb_record_decode(o->p, o->n, &r) != 0) {
        wb_s(b, "workbench: a checkpoint's record cannot be read\n");
        return -1;
    }
    ck = r.has_state ? wb_store_get(&s->store, r.state) : NULL;
    int has = r.has_state;
    wb_record_free(&r);
    if (!has) return 0;
    if (ck == NULL || cint_checkpoint_decode(ck->p, ck->n, info) != CINT_OK ||
        (info->global_count != 0 && (*out = calloc((size_t)info->global_count, sizeof **out)) == NULL)) {
        wb_s(b, "workbench: a checkpoint's state cannot be read\n");
        return -1;
    }
    size_t cursor = 0;
    while (*n < info->global_count && cint_checkpoint_global_next(info, &cursor, &(*out)[*n]) == CINT_OK) (*n)++;
    return 0;
}

/* " session@r4, acc@r1" for the revision set of record l (the live one, or a layout), or
 * " none". */
static void revisions_text(const wb_record *l, wb_buf *b)
{
    int any = 0;
    if (l->rev != 0) {
        wb_f(b, " session@r%u", (unsigned)l->rev);
        any = 1;
    }
    for (uint32_t i = 0; i < l->nloaded; i++) {
        const char *p = l->loaded[i].path, *slash = strrchr(p, '/');
        const char *base = slash != NULL ? slash + 1 : p;
        wb_f(b, "%s %.*s@r%u", any ? "," : "", (int)(strlen(base) - 3u), base, (unsigned)l->loaded[i].rev);
        any = 1;
    }
    wb_s(b, any ? "" : " none");
}

static void cmd_checkpoint(session *s, char *name, wb_buf *b)
{
    uint8_t id[32], sid[32], nid[32];
    cint_checkpoint_global *g = NULL;
    cint_checkpoint_info info;
    size_t n = 0, bindings = 0;
    int32_t k = -1;
    if (!wb_name_ok(name, WB_NAME_MAX)) {
        wb_f(b, ":checkpoint takes a NAME of at most %u letters, digits, _, - and .\n", WB_NAME_MAX);
        return;
    }
    if (strncmp(name, "auto-", 5u) == 0) {
        wb_s(b, ":checkpoint: names auto-N are the session's own; choose another\n");
        return;
    }
    if (find_ckpt(s, name) >= 0) {
        wb_f(b, ":checkpoint: %s exists; choose another name\n", name);
        return;
    }
    if (take(s, name, s->head, s->next, id, sid, nid, b) != 0 ||
        (k = add_ckpt(s, name, s->numbered + 1u, s->head, s->next, id, sid, nid)) < 0) {
        return;
    }
    s->numbered++;
    s->head = k;
    s->dirty = 1;
    if (globals_of(s, id, &g, &n, &info, b) == 0) {
        for (size_t i = 0; i < n; i++) bindings += g[i].name.len > 8u && memcmp(g[i].name.bytes, "session.", 8u) == 0;
    }
    free(g);
    wb_f(b, "checkpoint %s   #%u", name, (unsigned)s->ck[k].number);
    if (bindings != 0) wb_f(b, "   bindings %zu", bindings);
    wb_s(b, "   revisions:");
    revisions_text(&s->live, b);
    wb_s(b, "\n");
}

/* Keeps the current state before a restore replaces it (SPEC-06 5.5): as auto-N, unless a
 * checkpoint of the branch has its state identity. Returns that checkpoint or the new one, or
 * -1 with the reason in b. */
static int32_t keep_current(session *s, wb_buf *b)
{
    uint8_t id[32], sid[32], nid[32];
    char name[WB_NAME_MAX + 1u];
    (void)snprintf(name, sizeof name, "auto-%u", (unsigned)s->autos + 1u);
    if (take(s, name, s->head, s->next, id, sid, nid, b) != 0) return -1;
    for (uint32_t i = 0; i < s->nck; i++) {
        if (s->ck[i].number != 0 && s->ck[i].branch == s->cur && memcmp(s->ck[i].sid, sid, 32u) == 0) {
            wb_f(b, "current state equals checkpoint %s; no automatic checkpoint\n", s->ck[i].name);
            return (int32_t)i;
        }
    }
    int32_t k = add_ckpt(s, name, s->numbered + 1u, s->head, s->next, id, sid, nid);
    if (k < 0) {
        wb_s(b, "workbench: out of memory\n");
        return -1;
    }
    s->numbered++;
    s->autos++;
    wb_f(b, "saved current state as %s\n", name);
    return k;
}

/* s->fault: the report of the fault the current state holds, if any (SPEC-01 IM-121), for a
 * restored checkpoint (entry 0) or the end of an entry reached by replay. */
static void state_fault(session *s, int live, uint32_t entry)
{
    static cint_fault_record rec;
    char where[32] = "a restored checkpoint";
    if (entry != 0) (void)snprintf(where, sizeof where, "entry %u", (unsigned)entry);
    wb_free(&s->fault);
    if (s->ctx != NULL && cint_ctx_fault(s->ctx, &rec) == CINT_OK && rec.code != 0) {
        report(s, NULL, live, s->ctx, entry != 0 ? s->live_img : NULL, -1,
               entry == 0 ? "restored with the checkpoint (SPEC-01 IM-121)" :
               "the state the fault left, reached by replay (SPEC-01 IM-121)", where, "cpu-sir-interp", &s->fault);
    }
}

/* Makes record `id` of store st the current state (SPEC-06 5.5): its revision set is written
 * into the session's sources and built, the build's revision identity must be the record's, and
 * its CINT-CKPT-1 is restored into a new context (SPEC-03 H-19a). Then its declarations, loaded
 * modules, and settings become the session's. Nothing changes when a step fails. Returns 0, or
 * -1 with the reason in b. */
static int restore_record(session *s, const wb_store *st, const uint8_t id[32], wb_buf *b)
{
    const wb_obj *o = wb_store_get(st, id), *ck = NULL;
    wb_record r;
    cint_ctx *ctx = NULL;
    cint_sir_image *img = NULL;
    cint_sir_program *had = s->prog;
    decl *nd = NULL;
    char **nl = NULL;
    int ok;
    if (o == NULL || wb_record_decode(o->p, o->n, &r) != 0) {
        wb_s(b, "workbench: the checkpoint's record cannot be read\n");
        return -1;
    }
    nd = r.ndecl != 0 ? calloc(r.ndecl, sizeof *nd) : NULL;
    nl = r.nloaded != 0 ? calloc(r.nloaded, sizeof *nl) : NULL;
    ok = (r.ndecl == 0 || nd != NULL) && (r.nloaded == 0 || nl != NULL);
    for (uint32_t i = 0; ok && i < r.ndecl; i++) {
        const wb_obj *t = wb_store_get(st, r.decl[i].text);
        ok = t != NULL && memchr(t->p, '\0', t->n) == NULL && (nd[i].text = malloc(t->n + 1u)) != NULL;
        if (ok) {
            memcpy(nd[i].text, t->p, t->n);
            nd[i].text[t->n] = '\0';
            (void)snprintf(nd[i].name, sizeof nd[i].name, "%s", r.decl[i].name);
            nd[i].kind = r.decl[i].kind;
            nd[i].entry = r.decl[i].entry;
            nd[i].rev = r.decl[i].rev;
            nd[i].line = r.decl[i].line;
            nd[i].nlines = r.decl[i].nlines;
        }
    }
    for (uint32_t i = 0; ok && i < r.nloaded; i++) ok = (nl[i] = wb_copy(r.loaded[i].path)) != NULL;
    if (ok && r.has_state) {
        wb_buf cb = {0};
        uint8_t rev[32];
        cint_sir_error err;
        int only = 0;
        ck = wb_store_get(st, r.state);
        s->nunit = 0;
        s->prog = NULL;
        ok = ck != NULL && write_sources(s, st, &r) == 0 && compile(s, NULL, &cb, &only) == C_OK;
        if (ok) {
            cint_sir_revision(s->prog, rev);
            ok = memcmp(rev, r.revision, 32u) == 0 && cint_sir_prepare(s->prog, &img, &err) == CINT_SIR_OK &&
                 (ctx = new_ctx(s, cint_sir_cint_program(img), cint_sir_state_table(img))) != NULL &&
                 cint_ctx_restore(ctx, ck->p, ck->n) == CINT_OK;
        }
        if (!ok) {
            cint_sir_program *built = s->prog;
            wb_s(b, g_stop ? "interrupted: the restore stopped; nothing changed\n" :
                             "workbench: the checkpoint's program does not build again to its revision identity, "
                             "or its state does not restore (SPEC-03 H-19); nothing changed\n");
            cint_ctx_destroy(ctx);
            cint_sir_release(img);
            s->prog = had;
            retire(s, built);
            rollback_sources(s);
        }
        wb_free(&cb);
    }
    if (!ok) {
        for (uint32_t i = 0; nd != NULL && i < r.ndecl; i++) free(nd[i].text);
        for (uint32_t i = 0; nl != NULL && i < r.nloaded; i++) free(nl[i]);
        free(nd);
        free(nl);
        wb_record_free(&r);
        if (o != NULL && b->n == 0) wb_s(b, "workbench: the checkpoint cannot be restored; nothing changed\n");
        return -1;
    }
    /* the restore is made: the session takes the record's state, sources, and settings; the
     * caller starts the next segment */
    cint_sir_program *old = s->live_prog;
    rec_close(s);
    cint_ctx_destroy(s->ctx);
    cint_sir_release(s->live_img);
    s->ctx = ctx;
    s->live_img = img;
    s->live_prog = r.has_state ? s->prog : NULL;
    if (!r.has_state) {
        s->prog = NULL;   /* the start: no program yet */
    }
    retire(s, old);
    if (had != old) retire(s, had);
    for (uint32_t i = 0; i < s->ndecl; i++) free(s->decls[i].text);
    for (uint32_t i = 0; i < r.ndecl; i++) s->decls[i] = nd[i];
    s->ndecl = r.ndecl;
    for (uint32_t i = 0; i < s->nloaded; i++) free(s->loaded[i]);
    for (uint32_t i = 0; i < r.nloaded; i++) {
        s->loaded[i] = nl[i];
        s->lrev[i] = r.loaded[i].rev;
    }
    s->nloaded = r.nloaded;
    free(nd);
    free(nl);
    s->rev = r.rev;
    s->fuel = r.fuel;
    s->depth = r.depth;
    (void)snprintf(s->backend, sizeof s->backend, "%s", r.backend);
    s->nunit = 0;
    s->epoch++;
    wb_record_free(&s->live);
    if (r.has_state) {
        r.name[0] = '\0';
        s->live = r;
    } else {
        wb_record_free(&r);
    }
    state_fault(s, 1, 0);   /* the fault record is restored with the state (SPEC-06 5.5) */
    return 0;
}

/* After a restore: a module of the restored revision set whose source under the root differs. */
static void disk_differs(session *s, wb_buf *b)
{
    for (uint32_t k = 0; k + 1u < s->live.nmod; k++) {
        size_t n = 0;
        uint8_t id[32];
        char *file = wb_join(s->o->root, s->live.mod[k].path);
        char *text = file != NULL ? wb_read_file(file, &n) : NULL;
        if (text != NULL) cint_sha256((const uint8_t *)text, n, id);
        if (text == NULL || memcmp(id, s->live.mod[k].text, 32u) != 0)
            wb_f(b, "%s differs on disk from the restored revision; :reload reads it\n", s->live.mod[k].path);
        free(file);
        free(text);
    }
}

/* What changed in the declarations: "twice is revision session@r2", "y is not declared". */
static void decls_changed(const session *s, const wb_rdecl *was, uint32_t nwas, wb_buf *b)
{
    for (uint32_t i = 0; i < s->ndecl; i++) {
        int same = 0;
        for (uint32_t k = 0; k < nwas; k++)
            same |= strcmp(was[k].name, s->decls[i].name) == 0 && was[k].rev == s->decls[i].rev;
        if (!same) wb_f(b, "   %s is revision session@r%u", s->decls[i].name, (unsigned)s->decls[i].rev);
    }
    for (uint32_t k = 0; k < nwas; k++) {
        int found = 0;
        for (uint32_t i = 0; i < s->ndecl; i++) found |= strcmp(was[k].name, s->decls[i].name) == 0;
        if (!found) wb_f(b, "   %s is not declared", was[k].name);
    }
}

/* Restores checkpoint k, or undo point k for :undo, after keeping the current state. */
static int restore_ckpt(session *s, int32_t k, wb_buf *b)
{
    wb_rdecl *was = s->ndecl != 0 ? calloc(s->ndecl, sizeof *was) : NULL;
    uint32_t nwas = s->ndecl;
    if (s->ndecl != 0 && was == NULL) {
        wb_s(b, "workbench: out of memory\n");
        return -1;
    }
    for (uint32_t i = 0; i < nwas; i++) {
        (void)snprintf(was[i].name, sizeof was[i].name, "%s", s->decls[i].name);
        was[i].rev = s->decls[i].rev;
    }
    uint8_t record[32];
    memcpy(record, s->ck[k].record, 32u);   /* keep_current may move s->ck */
    if (keep_current(s, b) < 0 || restore_record(s, &s->store, record, b) != 0) {
        free(was);
        return -1;
    }
    s->dirty = 1;
    rec_open(s, (int32_t)s->ck[k].seg - 1, s->ck[k].at, SEG_RESTORE);
    if (s->ck[k].number != 0) {
        wb_f(b, "restored %s   #%u", s->ck[k].name, (unsigned)s->ck[k].number);
        s->head = k;
        s->nundo = 0;
    } else {
        wb_f(b, "back to the state before entry %u", (unsigned)s->ck[k].ordinal);
    }
    decls_changed(s, was, nwas, b);
    wb_s(b, "\n");
    free(was);
    disk_differs(s, b);
    (void)watches(s, 0, W_QUIET, b);
    return 0;
}

static void cmd_restore(session *s, char *name, wb_buf *b)
{
    int32_t k = find_ckpt(s, name);
    if (*name == '\0') wb_s(b, ":restore takes a checkpoint NAME\n");
    else if (k < 0) wb_f(b, "no checkpoint %s (:branches lists them)\n", name);
    else (void)restore_ckpt(s, k, b);
}

static void cmd_undo(session *s, char *arg, wb_buf *b)
{
    (void)arg;
    if (s->nundo == 0) wb_s(b, "nothing to undo\n");
    else if (restore_ckpt(s, (int32_t)s->undo[s->nundo - 1u], b) == 0) s->nundo--;
}

static void cmd_branch(session *s, char *name, wb_buf *b)
{
    int32_t base = -1;
    if (!wb_name_ok(name, WB_NAME_MAX)) {
        wb_f(b, ":branch takes a NAME of at most %u letters, digits, _, - and .\n", WB_NAME_MAX);
        return;
    }
    for (uint32_t i = 0; i < s->nbr; i++) {
        if (strcmp(s->br[i].name, name) == 0) {
            wb_f(b, ":branch: %s exists; choose another name\n", name);
            return;
        }
    }
    if (s->nbr == BRANCHES_MAX) {
        wb_f(b, ":branch: a session holds at most %u branches\n", BRANCHES_MAX);
        return;
    }
    if ((base = keep_current(s, b)) < 0) return;
    (void)snprintf(s->br[s->nbr].name, sizeof s->br[s->nbr].name, "%s", name);
    s->br[s->nbr].base = base;
    s->cur = s->nbr++;
    s->head = base;
    s->dirty = 1;
    wb_f(b, "branch %s from %s\n", name, s->ck[base].name);
}

static void cmd_branches(session *s, char *arg, wb_buf *b)
{
    (void)arg;
    for (uint32_t i = 0; i < s->nbr; i++) {
        uint32_t count = 0;
        int32_t head = i == s->cur ? s->head : s->br[i].base;
        for (uint32_t k = 0; k < s->nck; k++) {
            if (s->ck[k].number != 0 && s->ck[k].branch == i) {
                count++;
                head = i == s->cur ? head : (int32_t)k;
            }
        }
        wb_f(b, "%c %s", i == s->cur ? '*' : ' ', s->br[i].name);
        if (i != 0) wb_f(b, "   from %s", ckpt_name(s, s->br[i].base));
        wb_f(b, "   checkpoints %u   head %s\n", (unsigned)count, ckpt_name(s, head));
    }
    for (uint32_t k = 0; k < s->nck; k++) {
        const ckpt *c = &s->ck[k];
        if (c->number == 0) continue;
        wb_f(b, "#%u  %s   %s   ", (unsigned)c->number, c->name, s->br[c->branch].name);
        if (c->ordinal > 1u) wb_f(b, "after entry %u", (unsigned)c->ordinal - 1u);
        else wb_s(b, "before any entry");
        if (c->parent >= 0) wb_f(b, "   follows %s", s->ck[c->parent].name);
        wb_s(b, "\n");
    }
}

/* `:diff NAME [binding]`: the current state against a checkpoint, binding by binding, in
 * canonical order (SPEC-06 5.3, 7.1). */
static void cmd_diff(session *s, char *arg, wb_buf *b)
{
    char *only = arg + strcspn(arg, " \t");
    if (*only != '\0') {
        *only++ = '\0';
        only = (char *)skip(only);
    }
    int32_t k = find_ckpt(s, arg);
    if (*arg == '\0') {
        wb_s(b, ":diff takes a checkpoint NAME and an optional BINDING\n");
        return;
    }
    if (k < 0) {
        wb_f(b, "no checkpoint %s (:branches lists them)\n", arg);
        return;
    }
    uint8_t id[32], sid[32], nid[32];
    cint_checkpoint_global *here = NULL, *there = NULL;
    cint_checkpoint_info ih, it;
    size_t nh = 0, nt = 0, width = 7, rows = 0, total = 0;
    if (take(s, "", s->head, s->next, id, sid, nid, b) != 0 || globals_of(s, id, &here, &nh, &ih, b) != 0 ||
        globals_of(s, s->ck[k].record, &there, &nt, &it, b) != 0) {
        free(here);
        free(there);
        return;
    }
    wb_buf t = {0};
    for (int pass = 0; pass < 2; pass++) {   /* the widths, then the rows */
        size_t i = 0, j = 0;
        while (i < nh || j < nt) {
            const cint_checkpoint_global *a = i < nh ? &here[i] : NULL, *z = j < nt ? &there[j] : NULL;
            int c = a == NULL ? 1 : z == NULL ? -1 : 0;
            if (a != NULL && z != NULL) {
                size_t m = a->name.len < z->name.len ? a->name.len : z->name.len;
                c = memcmp(a->name.bytes, z->name.bytes, m);
                c = c != 0 ? c : a->name.len < z->name.len ? -1 : a->name.len > z->name.len ? 1 : 0;
            }
            const cint_checkpoint_global *g = c <= 0 ? a : z;
            size_t len;
            const char *shown = shown_name(g->name.bytes, g->name.len, &len);
            int same = c == 0 && a->value.len == z->value.len && memcmp(a->value.bytes, z->value.bytes, a->value.len) == 0;
            int retyped = c == 0 && (a->value.bytes[0] != z->value.bytes[0] || (a->value.bytes[0] == 0x51u && !same));
            const uint8_t *sn = NULL;
            uint32_t snl = 0;
            size_t ta = c == 0 ? struct_type(a, &sn, &snl) : 0u, tz = c == 0 ? struct_type(z, &sn, &snl) : 0u;
            wb_view av = {0}, zv = {0};
            uint64_t differing = 1, first = 0;
            if (same && ta != 0u) {   /* a struct (OQ-260): its type, then its payload byte by byte */
                retyped = ta != tz || memcmp(a->elements, z->elements, ta) != 0 || a->element_count != z->element_count;
                same = !retyped;
                differing = 0;
                for (uint64_t e = a->element_count; !retyped && e-- > ta;) {
                    if (a->elements[e] != z->elements[e]) {
                        differing++;
                        first = e - ta;
                    }
                }
                same = same && differing == 0;
                differing = retyped ? 1u : differing;
            }
            if (same && array_view(a, &av) && array_view(z, &zv)) {   /* an array: element by element */
                unsigned w = elem_size(av.tag);
                differing = 0;
                for (uint64_t e = a->element_count; e-- > 0;) {
                    if (memcmp(a->elements + e * w, z->elements + e * w, w) != 0) {
                        differing++;
                        first = e;
                    }
                }
                same = differing == 0;
            }
            int wanted = *only == '\0' || (strlen(only) == len && memcmp(only, shown, len) == 0);
            i += c <= 0;
            j += c >= 0;
            if (!wanted) continue;
            total += pass == 0;
            if (same) {
                if (pass == 1 && *only != '\0') {
                    char v[300];
                    global_text(a, 1, v, sizeof v);
                    wb_f(&t, "%s is equal here and in %s: %s\n", only, arg, v);
                }
                continue;
            }
            if (pass == 0) {
                width = len > width ? len : width;
                rows++;
                continue;
            }
            char va[300], vz[300];
            if (a != NULL && c <= 0) global_text(a, retyped, va, sizeof va);
            if (z != NULL && c >= 0) global_text(z, retyped, vz, sizeof vz);
            wb_f(&t, "%-*.*s   %-11s %-11llu ", (int)width, (int)len, shown,
                 c < 0 ? "only here" : c > 0 ? "only there" : retyped ? "retyped" : "differs",
                 (unsigned long long)differing);
            if (c == 0 && !retyped && ta != 0u) {   /* a struct's first payload byte that differs */
                wb_f(&t, "payload byte %llu: ", (unsigned long long)first);
                (void)snprintf(va, sizeof va, "%02x", (unsigned)a->elements[ta + first]);
                (void)snprintf(vz, sizeof vz, "%02x", (unsigned)z->elements[ta + first]);
            } else if (c == 0 && !retyped && a->elements != NULL) {   /* the first element that differs */
                element_text(&av, first, va, sizeof va);
                element_text(&zv, first, vz, sizeof vz);
                for (uint32_t d = 0; d < av.rank; d++)
                    wb_f(&t, "%s%lld", d != 0 ? ", " : "[", (long long)((int64_t)first / av.stride[d] % av.extent[d]));
                wb_s(&t, "]: ");
            }
            if (c < 0) wb_f(&t, "%s here, not in %s\n", va, arg);
            else if (c > 0) wb_f(&t, "not here, %s in %s\n", vz, arg);
            else wb_f(&t, "%s here, %s in %s\n", va, vz, arg);
        }
        if (pass == 0 && rows != 0) {
            wb_f(&t, "%-*s   %-11s %-11s %s\n", (int)width, "binding", "status", "differing",
                 "first canonical difference");
        }
    }
    if (rows == 0 && *only == '\0') wb_f(&t, "no binding differs from %s (%zu compared)\n", arg, total);
    else if (total == 0) wb_f(&t, "no binding %s here or in %s\n", only, arg);
    if (*only == '\0' && (ih.fault == NULL) != (it.fault == NULL)) {
        wb_f(&t, "fault record: %s here, %s in %s\n", ih.fault != NULL ? "held" : "none",
             it.fault != NULL ? "held" : "none", arg);
    } else if (*only == '\0' && ih.fault != NULL &&
               (ih.fault_len != it.fault_len || memcmp(ih.fault, it.fault, ih.fault_len) != 0)) {
        wb_f(&t, "fault record: differs from %s\n", arg);
    }
    wb_put(b, t.p, t.n);
    wb_free(&t);
    free(here);
    free(there);
}

/* `:history NAME` with the checkpoints that hold each revision (SPEC-06 16.1). */
static void cmd_history(session *s, char *name, wb_buf *b)
{
    int any = 0;
    if (*name == '\0') {
        wb_s(b, ":history takes a NAME\n");
        return;
    }
    for (uint32_t i = 0; i < s->nhist; i++) {
        if (strcmp(s->hist[i].name, name) != 0) continue;
        int listed = 0;
        any = 1;
        wb_f(b, "r%u  entry %u", (unsigned)s->hist[i].rev, (unsigned)s->hist[i].entry);
        for (uint32_t k = 0; k < s->nck; k++) {
            const wb_obj *o = s->ck[k].number != 0 ? wb_store_get(&s->store, s->ck[k].record) : NULL;
            wb_record r;
            if (o == NULL || wb_record_decode(o->p, o->n, &r) != 0) continue;
            for (uint32_t d = 0; d < r.ndecl; d++) {
                if (strcmp(r.decl[d].name, name) == 0 && r.decl[d].rev == s->hist[i].rev)
                    wb_f(b, "%s%s", listed++ ? ", " : "   in checkpoints: ", s->ck[k].name);
            }
            wb_record_free(&r);
        }
        wb_s(b, "\n");
    }
    if (!any) wb_f(b, "%s has no revision in this session\n", name);
}

/* ------------------------------------------ Watch commands (SPEC-06 5.3, 6.4). */

/* Splits `EXPR [when COND] [stop]` at its words outside brackets. Returns 0, or -1 when the
 * expression or the condition is empty. */
static int watch_words(char *arg, char **cond, int *stop)
{
    tok t;
    int depth = 0;
    char *last = NULL, *when = NULL;
    *cond = NULL;
    *stop = 0;
    for (const char *p = lex(arg, &t); t.kind != T_END; p = lex(p, &t)) {
        char c = t.kind == T_PUNCT ? t.p[0] : '\0';
        depth += c == '(' || c == '[' || c == '{';
        depth -= c == ')' || c == ']' || c == '}';
        if (depth == 0 && when == NULL && t.kind == T_ID && is(&t, "when")) when = (char *)t.p;
        last = (char *)t.p;
    }
    if (last != NULL && strcmp(last, "stop") == 0 && last > arg) {
        *stop = 1;
        *last = '\0';
    }
    if (when != NULL && (*stop == 0 || when < last)) {
        *when = '\0';
        *cond = (char *)skip(when + 4);
    }
    (void)trim(arg);
    *cond = *cond != NULL ? trim(*cond) : NULL;
    return *skip(arg) == '\0' || (*cond != NULL && **cond == '\0') ? -1 : 0;
}

static void cmd_watch(session *s, char *arg, wb_buf *b)
{
    char *cond;
    int stop, sc = 0, only = 0;
    if (strncmp(arg, "log", 3u) == 0 && (arg[3] == ' ' || arg[3] == '\0')) {
        unsigned n = (unsigned)strtoul(arg + 3, NULL, 10);
        for (uint32_t k = 0; k < s->nw; k++) {
            if (s->w[k].number == n) {
                for (uint32_t i = 0; i < s->w[k].nlog; i++) {
                    char v[300];
                    value_text(&s->w[k].log[i], 0, v, sizeof v);
                    wb_f(b, "entry %u   %s\n", (unsigned)s->w[k].log[i].ordinal, v);
                }
                if (s->w[k].nlog == 0) wb_f(b, "watch %u has no change recorded\n", n);
                return;
            }
        }
        wb_f(b, "no watch %u\n", n);
        return;
    }
    if (watch_words(arg, &cond, &stop) != 0) {
        wb_s(b, ":watch takes a pure EXPR, then when COND, then stop, as you need them\n");
        return;
    }
    if (s->nw == WATCHES_MAX) {
        wb_f(b, ":watch: a session holds at most %u watches\n", WATCHES_MAX);
        return;
    }
    char path[32];
    const char *expr = skip(arg);
    uint32_t number = s->wnext + 1u;
    (void)snprintf(path, sizeof path, "watch %u", (unsigned)number);
    (void)snprintf(s->scratch_path, sizeof s->scratch_path, "%s", path);
    s->scratch = expr;
    int r = typed(s, expr, path, &sc, b);
    if (r == C_OK && wb_effect(s->prog, "session.ci", "wb_entry") != 0) {
        wb_s(b, ":watch takes a pure expression: this one prints or calls a function that does\n");
        r = C_ERROR;
    }
    if (r == C_OK && cond != NULL) {
        (void)snprintf(path, sizeof path, "watch %u when", (unsigned)number);
        (void)snprintf(s->scratch_path, sizeof s->scratch_path, "%s", path);
        s->scratch = cond;
        r = write_module(s, NULL, s->ndecl, "Bool", cond, path, 0) == 0 ? compile(s, NULL, b, &only) : C_BROKEN;
        if (r == C_OK && wb_effect(s->prog, "session.ci", "wb_entry") != 0) {
            wb_s(b, ":watch takes a pure condition: this one prints or calls a function that does\n");
            r = C_ERROR;
        }
    }
    s->scratch = NULL;
    if (r != C_OK) return;
    watch *w = &s->w[s->nw];
    memset(w, 0, sizeof *w);
    w->number = number;
    w->expr = wb_copy(expr);
    w->cond = cond != NULL ? wb_copy(cond) : NULL;
    w->sc = sc;
    w->stop = stop;
    if (w->expr == NULL || (cond != NULL && w->cond == NULL)) {
        free_watch(w);
        wb_s(b, "workbench: out of memory\n");
        return;
    }
    s->nw++;
    s->wnext = number;
    s->wepoch++;
    s->dirty = 1;
    (void)watches(s, 0, W_FRESH, b);
}

static void cmd_watches(session *s, char *arg, wb_buf *b)
{
    (void)arg;
    for (uint32_t k = 0; k < s->nw; k++) {
        char shown[256];
        one_line(s->w[k].expr, shown, sizeof shown);
        wb_f(b, "watch %u   %s : %s", (unsigned)s->w[k].number, shown, SCALAR_NAMES[s->w[k].sc]);
        if (s->w[k].cond != NULL) {
            one_line(s->w[k].cond, shown, sizeof shown);
            wb_f(b, "   when %s", shown);
        }
        wb_s(b, s->w[k].stop ? "   stop\n" : "\n");
    }
    if (s->nw == 0) wb_s(b, "no watches\n");
}

static void cmd_unwatch(session *s, char *arg, wb_buf *b)
{
    int64_t n = 0;
    if (parse_count(arg, 1, &n) != 0) {
        wb_s(b, ":unwatch takes the number of a watch (:watches lists them)\n");
        return;
    }
    for (uint32_t k = 0; k < s->nw; k++) {
        if ((int64_t)s->w[k].number == n) {
            remove_watch(s, k);
            s->dirty = 1;
            wb_f(b, "watch %lld removed\n", (long long)n);
            return;
        }
    }
    wb_f(b, "no watch %lld\n", (long long)n);
}

/* ---- Replay and debugging: :goto, :trace, :replay, :effects (SPEC-06 5.3, 10). */
/* The replay and the trace ring are wb_replay.c's. The session's context is not touched until
 * :goto restores the state a replay reached. A replay stops only after a boundary, so :replay
 * with watches replays a segment up to each boundary it reports, and its cost grows with the
 * square of the boundaries it passes (SPEC-06 10.4). */

/* Replays segment g on a new context up to boundary `stop` (0: its initial state only;
 * UINT64_MAX: all of it), with fn called for each trace event; the context stays in rp at the
 * state reached. The live segment ends first, since only a complete recording replays, and the
 * next goes on from the same state. Returns 0, or -1 with the reason in b; the caller frees rp
 * either way. */
static int replay_seg(session *s, uint32_t g, uint64_t stop, cint_sir_trace_fn fn, void *user, wb_replayer *rp,
                      wb_buf *b)
{
    wb_buf why = {0};
    memset(rp, 0, sizeof *rp);
    if (g + 1u == s->nseg && s->rec != NULL) {
        uint32_t at = (uint32_t)s->seg[g].boundaries;
        rec_close(s);
        rec_open(s, (int32_t)g, at, SEG_CONT);
    }
    const wb_obj *o = s->seg[g].ended == 1 ? wb_store_get(&s->store, s->seg[g].id) : NULL;
    if (o == NULL || wb_replay_load(rp, o->p, o->n, fn, user) != 0 ||
        (rp->ctx = new_ctx(s, cint_sir_cint_program(rp->img), cint_sir_state_table(rp->img))) == NULL) {
        wb_s(b, "workbench: the recording of this state cannot be read or replayed\n");
        return -1;
    }
    g_entry = stop != 0 ? rp->ctx : NULL;   /* an interrupt stops the entry replay is in */
    int r = wb_replay_run(rp, stop, &why);
    g_entry = NULL;
    if (g_stop && stop != 0) {
        wb_s(b, "interrupted: the replay stopped; nothing changed\n");
        r = -1;
    } else if (why.n != 0) {
        wb_put(b, why.p, why.n);
    }
    wb_free(&why);
    return r;
}

/* The entries that made the boundaries of segment g: ords[at] for boundary at, 0 when unknown.
 * NULL when memory runs out. */
static uint32_t *ords_of(const session *s, uint32_t g)
{
    uint32_t *ords = calloc((size_t)s->seg[g].boundaries + 1u, sizeof *ords);
    for (uint32_t n = 1; ords != NULL && n < s->next && n <= ITEMS_MAX; n++) {
        const bound *d = &s->bnd[n - 1u];
        if (d->seg == g + 1u && d->at >= 1u && d->at <= s->seg[g].boundaries) ords[d->at] = n;
    }
    return ords;
}

/* The segments that lead to the current state, the live one first: path[i], and the boundaries
 * of path[i] on the way, upto[i]. Returns their count, 0 when the current state is not
 * recorded; the caller frees *path and *upto. */
static uint32_t path_of(const session *s, uint32_t **path, uint64_t **upto)
{
    uint32_t n = 0;
    *path = s->rec != NULL ? malloc(s->nseg * sizeof **path) : NULL;
    *upto = *path != NULL ? malloc(s->nseg * sizeof **upto) : NULL;
    if (*upto == NULL) {
        free(*path);
        *path = NULL;
        return 0;
    }
    int32_t g = (int32_t)s->nseg - 1;
    uint64_t at = s->seg[g].boundaries;
    while (g >= 0 && n < s->nseg) {   /* a parent comes before its segment */
        (*path)[n] = (uint32_t)g;
        (*upto)[n++] = at;
        at = s->seg[g].at;
        g = s->seg[g].parent;
    }
    return n;
}

/* The state identity of the session's context, as a checkpoint of it would have. */
static int current_sid(session *s, uint8_t sid[32])
{
    wb_buf ck = {0};
    cint_checkpoint_info info;
    uint8_t nid[32];
    wb_record r = s->live;
    memset(&info, 0, sizeof info);
    int bad = s->ctx == NULL || ckpt_of(s->ctx, &ck) != CINT_OK ||
              cint_checkpoint_decode((const uint8_t *)ck.p, ck.n, &info) != CINT_OK;
    if (!bad) {
        r.effects = info.effects;
        bad = wb_record_ids(&r, info.state, info.state_len, sid, nid) != 0;
    }
    wb_free(&ck);
    return bad ? -1 : 0;
}

/* The entry `:goto` and `:trace` name: N, `fault` (the latest that faulted), or with no
 * argument the latest that faulted, else the latest that ran. Returns it, or 0 with the reason
 * in b. */
static uint32_t entry_named(const session *s, const char *cmd, const char *arg, wb_buf *b)
{
    int64_t n = 0;
    int fault = strcmp(arg, "fault") == 0 || strcmp(arg, "error") == 0 || *arg == '\0';
    for (uint32_t e = s->next - 1u; fault && n == 0 && e >= 1u; e--) {
        n = s->bnd[e - 1u].seg != 0 && s->outcome[e - 1u] != NULL &&
            strcmp(s->outcome[e - 1u], *arg == 'e' ? "error" : "fault") == 0 ? e : 0;
    }
    for (uint32_t e = s->next - 1u; *arg == '\0' && n == 0 && e >= 1u; e--) n = s->bnd[e - 1u].seg != 0 ? e : 0;
    if (n == 0 && fault) {
        wb_s(b, *arg == 'e' ? "no recorded entry of this session left an error result\n" :
                *arg != '\0' ? "no entry of this session faulted\n" : "no entry of this session ran code\n");
        return 0;
    }
    if (!fault && (parse_count(arg, 1, &n) != 0 || n >= (int64_t)s->next)) {
        wb_f(b, ":%s takes N, an entry of this session, or fault\n", cmd);
        return 0;
    }
    if (s->bnd[n - 1].seg == 0) {
        wb_f(b, "entry %u has no boundary in the recording: it ran no code, ran on cpu-c17 alone, or ran while "
                "the state was not recorded\n", (unsigned)n);
        return 0;
    }
    return (uint32_t)n;
}

/* ---------------------------------- Debug mode (SPEC-06 10.4; box 14 unit 3b). */
/*
 * `cint debug RECORDING` opens a recording read-only. It becomes segment 0 and its entries
 * entries 1 to N; the session replays it to the end, or to the last boundary that matched when
 * replay stops before, and goes on from the state reached, which no entry changes. Its code is
 * the SIR text the recording holds, with no source to compile an expression in, so :inspect and
 * :type read a global of the state, and positions are those of the SIR text.
 */

/* The entry whose boundary is boundary `at` of the recording, or 0. */
static uint32_t debug_entry_at(const session *s, uint64_t at)
{
    for (uint32_t n = 1; at != 0 && n < s->next && n <= ITEMS_MAX; n++) {
        if (s->bnd[n - 1u].seg == 1u && s->bnd[n - 1u].at == at) return n;
    }
    return 0;
}

/* Makes the state at boundary `at` of the recording current by replay, as `how` reached it; at
 * UINT64_MAX, its end, or the last boundary that matched when replay stops before it. Returns
 * the boundary, or -1 with the reason in b and the state unchanged. */
static int64_t debug_reach(session *s, uint64_t at, int how, wb_buf *b)
{
    wb_replayer rp;
    int r = replay_seg(s, 0, at, NULL, NULL, &rp, b);
    if (r != 0 && at == UINT64_MAX && rp.ctx != NULL && !g_stop) {
        at = rp.rep.boundaries;
        wb_replay_free(&rp);
        r = replay_seg(s, 0, at, NULL, NULL, &rp, b);
    }
    if (r != 0) {
        wb_replay_free(&rp);
        return -1;
    }
    at = at == UINT64_MAX ? rp.rep.boundaries : at;
    rec_close(s);
    cint_ctx_destroy(s->ctx);
    cint_sir_release(s->live_img);
    cint_sir_free(s->live_prog);
    wb_replay_keep(&rp, &s->ctx, &s->live_prog, &s->live_img);
    wb_replay_free(&rp);
    rec_open(s, 0, (uint32_t)at, how);
    state_fault(s, 0, debug_entry_at(s, at));
    return (int64_t)at;
}

/* Entry e of a recording as text: `module: name(I64 1, ...)`, or the program entry. NULL when
 * memory runs out. */
static char *debug_entry_text(const cint_recording_info *info, const cint_recording_event *e)
{
    wb_buf t = {0};
    if (e->form == 1u) {
        wb_s(&t, "the program entry");
    } else {
        wb_f(&t, "%.*s: %.*s(", (int)e->module.len, e->module.bytes, (int)e->name.len, e->name.bytes);
        for (uint32_t k = 0; k < e->arg_count; k++) {
            cint_tvalue v;
            char one[300] = "?";
            if (cint_recording_arg(info, e, k, &v) == CINT_OK) tvalue_text(&v, 1, one, sizeof one);
            wb_f(&t, "%s%s", k != 0 ? ", " : "", one);
        }
        wb_s(&t, ")");
    }
    if (t.failed) wb_free(&t);
    return t.p;
}

/* Opens the recording the options give: segment 0 and its entries, then the state replay
 * reaches. Returns 0, or -1 with the reason in b. */
static int debug_open(session *s, wb_buf *b)
{
    const wb_session_options *o = s->o;
    cint_recording_info info;
    cint_recording_event e;
    uint64_t at = 0;
    segment *g = calloc(8u, sizeof *g);
    memset(&e, 0, sizeof e);
    if (g == NULL || cint_recording_decode(o->debug, o->debug_len, &info) != CINT_OK ||
        wb_store_put(&s->store, o->debug, o->debug_len, g->id) != 0) {
        wb_f(b, "workbench: %s is not a complete recording (CINT-REC-1)\n", o->debug_path);
        free(g);
        return -1;
    }
    s->seg = g;
    s->capseg = 8u;
    s->nseg = 1u;
    g->boundaries = info.boundaries;
    g->parent = -1;
    g->how = SEG_NEW;
    g->ended = 1;
    while (s->next <= ITEMS_MAX && cint_recording_next(&info, &e) == CINT_OK && e.kind != CINT_EVENT_END) {
        if (e.kind == CINT_EVENT_ENTRY) {
            s->outcome[s->next - 1u] = "not run";
            if ((s->items[s->next++ - 1u] = debug_entry_text(&info, &e)) == NULL) {
                wb_s(b, "workbench: out of memory\n");
                return -1;
            }
        } else if (e.kind == CINT_EVENT_OUTCOME && s->next > 1u) {
            uint32_t n = s->next - 1u;
            s->outcome[n - 1u] = e.status == CINT_FAULT ? "fault" : e.status != CINT_OK ? "not run" :
                                 e.has_error ? "error" : e.has_result ? "value" : "done";
            if (e.status == CINT_OK || e.status == CINT_FAULT) {
                s->bnd[n - 1u].seg = 1u;
                s->bnd[n - 1u].at = (uint32_t)++at;
            }
        }
    }
    wb_f(b, "%s: entries %u   boundaries %llu%s\n", info.is_case ? "case" : "recording",
         (unsigned)(s->next - 1u), (unsigned long long)info.boundaries,
         s->next > ITEMS_MAX ? " (the session lists the first entries)" : "");
    int64_t r = debug_reach(s, UINT64_MAX, SEG_CONT, b);
    uint32_t n = r > 0 ? debug_entry_at(s, (uint64_t)r) : 0;
    if (n != 0) wb_f(b, "at the end of entry %u, reached by replay\n", (unsigned)n);
    else if (r == 0) wb_s(b, "at the recording's initial state\n");
    return r < 0 ? -1 : 0;
}

/* `:goto N|fault`: makes the state at the end of entry N current, by replaying its segment up
 * to it and restoring what the replay reached (SPEC-06 5.3). Later entries start a branch named
 * after it. */
static void cmd_goto(session *s, char *arg, wb_buf *b)
{
    uint32_t n = entry_named(s, "goto", arg, b);
    wb_replayer rp;
    wb_record r;
    wb_buf ck = {0}, enc = {0};
    cint_checkpoint_info info;
    uint8_t id[32], sid[32], nid[32], now[32];
    char name[WB_NAME_MAX + 1u];
    memset(&info, 0, sizeof info);
    if (n == 0) return;
    if (s->o->debug != NULL) {   /* no layouts and no branches: the state replay reaches becomes current */
        if (debug_reach(s, s->bnd[n - 1u].at, SEG_GOTO, b) >= 0)
            wb_f(b, "at the end of entry %u, reached by replay\n", (unsigned)n);
        return;
    }
    uint32_t g = s->bnd[n - 1u].seg - 1u, at = s->bnd[n - 1u].at;
    const wb_obj *o = wb_store_get(&s->store, s->bnd[n - 1u].layout);
    if (o == NULL || wb_record_decode(o->p, o->n, &r) != 0) {
        wb_s(b, "workbench: the layout of that entry's program cannot be read\n");
        return;
    }
    int ok = replay_seg(s, g, at, NULL, NULL, &rp, b) == 0 && ckpt_of(rp.ctx, &ck) == CINT_OK &&
             cint_checkpoint_decode((const uint8_t *)ck.p, ck.n, &info) == CINT_OK &&
             wb_store_put(&s->store, ck.p, ck.n, r.state) == 0;
    if (ok) {   /* the layout with the state reached, and the session's settings */
        r.has_state = 1;
        r.effects = info.effects;
        r.fuel = s->fuel;
        r.depth = s->depth;
        (void)snprintf(r.backend, sizeof r.backend, "%s", s->backend);
        r.ordinal = s->next;
        ok = wb_record_encode(&enc, &r) == 0 && wb_store_put(&s->store, enc.p, enc.n, id) == 0 &&
             wb_record_ids(&r, info.state, info.state_len, sid, nid) == 0;
    } else if (b->n == 0) {
        wb_s(b, "workbench: out of memory\n");
    }
    wb_free(&ck);
    wb_free(&enc);
    wb_replay_free(&rp);
    if (ok && current_sid(s, now) == 0 && memcmp(now, sid, 32u) == 0) {
        wb_f(b, "the current state is the state at the end of entry %u\n", (unsigned)n);
        ok = 0;
    }
    wb_rdecl *was = ok && s->ndecl != 0 ? calloc(s->ndecl, sizeof *was) : NULL;
    uint32_t nwas = was != NULL ? s->ndecl : 0;
    for (uint32_t i = 0; i < nwas; i++) {
        (void)snprintf(was[i].name, sizeof was[i].name, "%s", s->decls[i].name);
        was[i].rev = s->decls[i].rev;
    }
    if (ok && (keep_current(s, b) < 0 || restore_record(s, &s->store, id, b) != 0)) ok = 0;
    wb_record_free(&r);
    if (!ok) {
        free(was);
        return;
    }
    rec_open(s, (int32_t)g, at, SEG_GOTO);
    s->nundo = 0;
    s->dirty = 1;
    state_fault(s, 1, n);
    wb_f(b, "at the end of entry %u, reached by replay", (unsigned)n);
    decls_changed(s, was, nwas, b);
    free(was);
    (void)snprintf(name, sizeof name, "entry-%u", (unsigned)n);
    for (uint32_t k = 2, i = 0; i < s->nbr; i++) {   /* a name no branch has */
        if (strcmp(s->br[i].name, name) == 0) {
            (void)snprintf(name, sizeof name, "entry-%u.%u", (unsigned)n, (unsigned)k++);
            i = (uint32_t)-1;
        }
    }
    wb_buf quiet = {0};   /* the branch starts from a checkpoint of this state (:branch) */
    int32_t base = s->nbr < BRANCHES_MAX ? keep_current(s, &quiet) : -1;
    if (base >= 0) {
        (void)snprintf(s->br[s->nbr].name, sizeof s->br[s->nbr].name, "%s", name);
        s->br[s->nbr].base = base;
        s->cur = s->nbr++;
        s->head = base;
        wb_f(b, "\nlater entries go on branch %s, from %s\n", name, s->ck[base].name);
    } else if (s->nbr == BRANCHES_MAX) {
        wb_f(b, "\nlater entries stay on branch %s (a session holds at most %u branches)\n", s->br[s->cur].name,
             BRANCHES_MAX);
    } else {
        wb_s(b, "\n");
        wb_put(b, quiet.p, quiet.n);
    }
    wb_free(&quiet);
    disk_differs(s, b);
    (void)watches(s, 0, W_QUIET, b);
}

/* An event's position as the session shows it (wb_where_fn). */
typedef struct where_at {
    const session *s;
    const wb_record *layout;
} where_at;

static void session_where(void *user, const wb_traced *x, char *out, size_t cap)
{
    const where_at *w = user;
    const cint_site *e = &x->e.site;
    char path[WB_PATH_MAX + 1u];
    uint32_t line = 0, col = 0;
    out[0] = '\0';
    if (e->module < x->p->nmod && e->index >= 1u && e->index <= x->p->mods[e->module].nsite) {
        const si_module *m = &x->p->mods[e->module];
        col = m->sites[e->index].col;
        place(w->s, NULL, w->layout, m->path, strlen(m->path), m->sites[e->index].line, path, sizeof path, &line, &col);
        (void)snprintf(out, cap, "%s:%u:%u", path, (unsigned)line, (unsigned)col);
    }
}

/* `:trace [N|fault]`: the counted operations of entry N (SPEC-01 IM-113), each with its step
 * ordinal, position, operation, operands, and value, from a replay on cpu-sir-interp (SPEC-06
 * 10.3); calls and returns are listed, a return without an ordinal. The replay runs twice, to
 * the boundary before the entry and to the entry's, so that the events before it are counted
 * and skipped. */
/* With arg "error", `:fault /path`: the path the last recorded error result took (SPEC-06 5.3;
 * BX12-19, BX14-18), from the calls and returns of the same replay. */
static void cmd_trace(session *s, char *arg, wb_buf *b)
{
    uint32_t n = entry_named(s, "trace", arg, b);
    wb_replayer rp;
    wb_tracer t;
    wb_record layout;
    static cint_fault_record rec;
    memset(&rp, 0, sizeof rp);
    memset(&layout, 0, sizeof layout);
    if (n == 0) return;
    uint32_t g = s->bnd[n - 1u].seg - 1u, at = s->bnd[n - 1u].at;
    const wb_obj *o = s->o->debug == NULL ? wb_store_get(&s->store, s->bnd[n - 1u].layout) : NULL;
    int laid = o != NULL && wb_record_decode(o->p, o->n, &layout) == 0;   /* a debug session has none */
    if (!laid && s->o->debug == NULL) {
        wb_s(b, "workbench: the layout of that entry's program cannot be read\n");
        return;
    }
    memset(&t, 0, sizeof t);
    t.rp = &rp;
    int ok = at == 1u || replay_seg(s, g, at - 1u, wb_on_trace, &t, &rp, b) == 0;
    if (at != 1u) wb_replay_free(&rp);
    t.skip = t.seen;
    t.seen = 0;
    ok = ok && (t.ring = malloc(WB_TRACE_MAX * sizeof *t.ring)) != NULL;
    ok = ok && replay_seg(s, g, at, wb_on_trace, &t, &rp, b) == 0;
    if (!ok && b->n == 0) wb_s(b, "workbench: out of memory\n");
    int faulted = ok && cint_ctx_fault(rp.ctx, &rec) == CINT_OK && rec.code != 0 && rec.code < 14u;
    where_at w = {s, laid ? &layout : NULL};
    const cint_error_set *set = NULL;
    uint64_t tag = 0;
    if (ok && *arg == 'e' && (cint_ctx_error(rp.ctx, &set, &tag) != CINT_OK || set == NULL || tag == 0u ||
                              tag > set->count)) {
        wb_f(b, "workbench: the replay of entry %u left no error result\n", (unsigned)n);
    } else if (ok && *arg == 'e') {
        wb_f(b, "entry %u   error: %s.%s (tag %llu)   evaluator cpu-sir-interp, by replay\n", (unsigned)n, set->name,
             set->values[tag - 1u], (unsigned long long)tag);
        wb_error_path(b, &t, tag, session_where, &w);
    }
    if (ok && *arg != 'e') {
        const char *text = s->items[n - 1u] != NULL ? s->items[n - 1u] : "";
        size_t len = strcspn(text, "\n");
        wb_f(b, "entry %u   %.*s%s   evaluator cpu-sir-interp, by replay\n", (unsigned)n, (int)(len > 60u ? 60u : len),
             text, len > 60u || text[len] != '\0' ? " ..." : "");
        if (t.kept == 0) wb_s(b, "no counted operation\n");
        else wb_trace_head(b);
        if (t.kept > WB_TRACE_MAX)
            wb_f(b, "  ... the first %llu events are not shown\n", (unsigned long long)(t.kept - WB_TRACE_MAX));
    }
    for (uint64_t i = t.kept > WB_TRACE_MAX ? t.kept - WB_TRACE_MAX : 0; ok && *arg != 'e' && i < t.kept; i++) {
        const wb_traced *x = &t.ring[i % WB_TRACE_MAX];
        const cint_sir_event *e = &x->e;
        char where[WB_PATH_MAX + 24u];
        session_where(&w, x, where, sizeof where);
        int at_fault = faulted && i + 1u == t.kept && e->kind == CINT_SIR_EV_OP &&
                       e->site.module == rec.position.module && e->site.index == rec.position.index;
        wb_trace_line(b, e, where, at_fault ? WB_CODE_NAMES[rec.code] : NULL);
    }
    wb_replay_free(&rp);
    free(t.ring);
    if (laid) wb_record_free(&layout);
}

/* The faults of segment g's boundaries from + 1 to `to`, which a replay has reproduced: it
 * compares each fault record with the recorded one, byte for byte (SPEC-03 H-17). */
static void faults_of(const session *s, uint32_t g, uint64_t from, uint64_t to, const uint32_t *ords, wb_buf *b)
{
    cint_recording_info info;
    cint_recording_event e;
    static wb_fault f;
    const wb_obj *o = s->seg[g].ended == 1 ? wb_store_get(&s->store, s->seg[g].id) : NULL;
    uint64_t at = 0;
    if (o == NULL || cint_recording_decode(o->p, o->n, &info) != CINT_OK) return;
    memset(&e, 0, sizeof e);
    while (at < to && cint_recording_next(&info, &e) == CINT_OK) {
        if (e.kind == CINT_EVENT_OUTCOME && (e.status == CINT_OK || e.status == CINT_FAULT) && ++at > from &&
            e.status == CINT_FAULT && e.data != NULL && wb_fault_decode(e.data, e.data_len, &f, WB_RECORD_RUN) == 0 &&
            f.code < 14u) {
            wb_f(b, "entry %u: %s reproduced, its fault record equal to the recorded one\n", (unsigned)ords[at],
                 WB_CODE_NAMES[f.code]);
        }
    }
}

/* `:replay NAME --changed BINDING`: the last boundary on the way to the current state at which
 * global BINDING changed, by the backward scan of SPEC-06 10.1. Each segment on the way, the
 * latest first, is replayed once with BINDING read at every boundary, and the scan stops at the
 * first segment with a change; the junction of two segments is one state. */
static void replay_changed(session *s, const char *binding, const char *from, const uint32_t *path,
                           const uint64_t *upto, uint32_t i, uint32_t cat, wb_buf *b)
{
    wb_scan sc;
    wb_tval now;
    char shown[320], was[300], is[300];
    uint64_t replayed = 0;
    uint32_t j = 0;
    int ok = one_global(s, binding, &now, shown, sizeof shown, b);
    memset(&sc, 0, sizeof sc);
    (void)snprintf(sc.name, sizeof sc.name, "%s", shown);
    for (; ok && j <= i; j++) {
        wb_replayer rp;
        wb_buf why = {0};
        memset(&rp, 0, sizeof rp);   /* a segment the way does not pass is not replayed */
        sc.from = j == i ? cat : 0;
        ok = upto[j] == 0 || replay_seg(s, path[j], 0, NULL, NULL, &rp, b) == 0;   /* its first state */
        if (ok && upto[j] != 0) {
            wb_scan_at(&sc, 0, rp.ctx);
            rp.at = wb_scan_at;
            rp.at_user = &sc;
            g_entry = rp.ctx;
            ok = wb_replay_run(&rp, sc.from + upto[j], &why) == 0 && !g_stop && !sc.bad;
            g_entry = NULL;
            replayed += rp.passed;
            wb_s(b, g_stop ? "interrupted: the scan stopped; nothing changed\n" :
                    sc.bad ? "workbench: a state on the way cannot be read\n" : "");
            wb_put(b, why.p, g_stop ? 0u : why.n);
        }
        wb_free(&why);
        wb_replay_free(&rp);
        if (sc.last != 0) break;
    }
    if (ok && sc.last != 0) {
        uint32_t *ords = ords_of(s, path[j]);
        (void)snprintf(was, sizeof was, "(none)");
        (void)snprintf(is, sizeof is, "(none)");
        if (sc.was.len != 0) (void)wb_tval_text(&sc.was, was, sizeof was, 0);
        if (sc.now.len != 0) (void)wb_tval_text(&sc.now, is, sizeof is, 0);
        wb_f(b, "%s last changed at entry %u, from %s to %s\n", shown, ords != NULL ? (unsigned)ords[sc.last] : 0u, was,
             is);
        free(ords);
    } else if (ok) {
        wb_f(b, "%s did not change after %s\n", shown, from);
    }
    if (ok) {
        wb_f(b, "backward scan: %llu %s replayed\n", (unsigned long long)replayed,
             replayed == 1u ? "boundary" : "boundaries");
    }
}

/* Whether the predicate s->prog holds as its entry wb_entry on the state of `from`, a context
 * in the program of from_img: 1 or 0, or -1 with the reason in b. */
static int holds_at(session *s, cint_ctx *from, cint_sir_image *from_img, wb_buf *b)
{
    cint_sir_image *img = NULL;
    cint_sir_error err;
    cint_ctx *x = NULL;
    change v;
    unsigned code = 0;
    int r = -1;
    memset(&v, 0, sizeof v);
    if (cint_sir_prepare(s->prog, &img, &err) != CINT_SIR_OK) {
        wb_f(b, "workbench: cpu-sir-interp does not run this yet: %s:%u:%u: %s\n", err.path, err.line, err.col,
             err.text);
        return -1;
    }
    if ((x = scratch_of(s, from, from_img, img, b)) != NULL) {
        r = call_unit(s, x, cint_sir_observer(img), "wb_entry", &v, &code) ? v.bytes[1] != 0 : -1;
        wb_s(b, r >= 0 ? "" : g_stop ? "interrupted: the bisection stopped; nothing changed\n" : "");
        if (r < 0 && !g_stop) wb_f(b, "--when: PRED faults with %s on a state on the way\n", WB_CODE_NAMES[code]);
    }
    cint_ctx_destroy(x);
    cint_sir_release(img);
    return r;
}

/* Position p of the way from a checkpoint (0: its state) to the current state, as boundary *at
 * of segment path[*j]; upto[j] counts the boundaries of path[j] on the way. */
static void way_at(const uint64_t *upto, uint32_t i, uint32_t cat, uint64_t p, uint32_t *j, uint64_t *at)
{
    for (*j = i, *at = cat + p; *j > 0 && p > upto[*j]; *at = p) p -= upto[(*j)--];
}

/* Whether PRED (holds_at) holds at position p of the way, by replay: 1 or 0, or -1. */
static int probe(session *s, const uint32_t *path, const uint64_t *upto, uint32_t i, uint32_t cat, uint64_t p,
                 wb_buf *b)
{
    wb_replayer rp;
    uint32_t j;
    uint64_t at;
    way_at(upto, i, cat, p, &j, &at);
    int r = replay_seg(s, path[j], at, NULL, NULL, &rp, b) == 0 ? holds_at(s, rp.ctx, rp.img, b) : -1;
    wb_replay_free(&rp);
    return r;
}

/* `:replay NAME --when PRED`: the first boundary on the way to the current state at which PRED
 * holds, a pure Bool expression that this use declares monotone over boundaries, found by
 * bisection (SPEC-06 10.1). Each probe replays to its boundary and evaluates PRED there. */
static void replay_when(session *s, const char *pred, const char *from, const uint32_t *path,
                        const uint64_t *upto, uint32_t i, uint32_t cat, wb_buf *b)
{
    uint64_t total = 0, at;
    uint32_t j;
    char shown[256];
    int only = 0;
    for (uint32_t k = 0; k <= i; k++) total += upto[k];
    if (s->o->debug != NULL) {
        wb_s(b, "--when compiles PRED in the session's sources, which a debug session does not have\n");
        return;
    }
    (void)snprintf(s->scratch_path, sizeof s->scratch_path, "when");
    s->scratch = pred;
    int r = write_module(s, NULL, s->ndecl, "Bool", pred, "when", 0) == 0 ? compile(s, NULL, b, &only) : C_BROKEN;
    if (r == C_OK && wb_effect(s->prog, "session.ci", "wb_entry") != 0) {
        wb_s(b, "--when takes a pure PRED: this one prints or calls a function that does\n");
        r = C_ERROR;
    }
    s->scratch = NULL;
    one_line(pred, shown, sizeof shown);
    r = r != C_OK ? -1 : total == 0 ? 2 : holds_at(s, s->ctx, s->live_img, b);   /* at the current state */
    uint64_t lo = 0, hi = total, probes = r == 0 || r == 1 ? 1u : 0u;
    if (r == 1 && (r = probe(s, path, upto, i, cat, 0, b)) >= 0) {   /* at the checkpoint */
        probes++;
        r = r == 1 ? 3 : 1;
    }
    while (r == 1 && hi - lo > 1u) {   /* PRED holds at hi and not at lo */
        uint64_t mid = lo + (hi - lo) / 2u;
        int h = probe(s, path, upto, i, cat, mid, b);
        probes++;
        r = h < 0 ? -1 : 1;
        hi = h == 1 ? mid : hi;
        lo = h == 0 ? mid : lo;
    }
    if (r == 0 || r == 2 || r == 3) {
        wb_f(b, r == 0 ? "%s does not hold at the current state\n" : r == 2 ? "no entry ran after %s\n" :
                                                                         "%s holds already at %s\n",
             r == 2 ? from : shown, from);
    } else if (r == 1) {
        way_at(upto, i, cat, hi, &j, &at);
        uint32_t *ords = ords_of(s, path[j]);
        wb_f(b, "%s first holds at entry %u\nbisection: %llu probes over %llu boundaries; PRED is taken as monotone\n", shown, ords != NULL ? (unsigned)ords[at] : 0u, (unsigned long long)probes,
             (unsigned long long)total);
        free(ords);
    }
}

/* `:replay NAME`: re-executes the recorded entries from checkpoint NAME to the current state,
 * comparing each with the recording, and evaluates the watches at each boundary, which is how
 * a watch added later finds when a value changed (SPEC-06 6.4, 10). The session's state does not
 * change. */
/* Where checkpoint k (-1: the recording's start, segment 0) is on the way path[0..np): the
 * index of its segment there, with its boundary in *cat; np when it is not on the way, and
 * np + 1 when it was not recorded. */
static uint32_t way_index(const session *s, int32_t k, const uint32_t *path, const uint64_t *upto, uint32_t np,
                          uint32_t *cat)
{
    uint32_t cg = k < 0 ? 1u : s->ck[k].seg, i = 0;
    *cat = k < 0 ? 0u : s->ck[k].at;
    if (cg == 0 && np != 0 && s->seg[path[np - 1u]].how == SEG_NEW) {   /* a checkpoint before any entry */
        wb_record r;
        const wb_obj *o = wb_store_get(&s->store, s->ck[k].record);
        if (o != NULL && wb_record_decode(o->p, o->n, &r) == 0) {
            cg = r.has_state ? 0 : path[np - 1u] + 1u;
            wb_record_free(&r);
        }
    }
    while (i < np && (path[i] + 1u != cg || *cat > upto[i])) i++;
    return cg == 0 ? np + 1u : i;
}

/* The entries that made the boundaries on the way from boundary cat of path[i] to the current
 * state, where upto[j] counts those of path[j]: their count, and the first and the last (0
 * when none). */
static uint64_t way_entries(const session *s, const uint32_t *path, const uint64_t *upto, uint32_t i, uint32_t cat,
                            uint32_t *first, uint32_t *last)
{
    uint64_t total = 0;
    *first = *last = 0;
    for (uint32_t j = i + 1u; j-- > 0;) {
        uint32_t *o = upto[j] != 0 ? ords_of(s, path[j]) : NULL;
        uint64_t from = j == i ? cat : 0;
        *first = *first == 0 && o != NULL ? o[from + 1u] : *first;
        *last = o != NULL ? o[from + upto[j]] : *last;
        total += upto[j];
        free(o);
    }
    return total;
}

static void cmd_replay(session *s, char *arg, wb_buf *b)
{
    char *opt = strncmp(arg, "--", 2u) == 0 ? arg : strstr(arg, " --"), *what = NULL, since[WB_NAME_MAX + 16u];
    int by = 0;   /* 1: --changed BINDING, 2: --when PRED */
    if (opt != NULL) {
        by = strncmp(skip(opt), "--changed ", 10u) == 0 ? 1 : strncmp(skip(opt), "--when ", 7u) == 0 ? 2 : -1;
        what = by > 0 ? (char *)skip(skip(opt) + (by == 1 ? 10u : 7u)) : NULL;
        *opt = '\0';
        (void)trim(arg);
    }
    const char *name = arg;
    int32_t k = find_ckpt(s, name);
    uint32_t *path = NULL, np = 0, i = 0, cat = 0, first = 0, last = 0, *ords = NULL;
    uint64_t *upto = NULL, passed = 0, total = 0;
    wb_replayer rp;
    int start = s->o->debug != NULL && (*name == '\0' || strcmp(name, "start") == 0);   /* the recording's */
    if (by < 0 || (by > 0 && *what == '\0')) {
        wb_s(b, ":replay takes NAME, then --changed BINDING or --when PRED, as you need them\n");
        return;
    }
    if (!start && (*name == '\0' || k < 0)) {
        wb_f(b, s->o->debug != NULL ? ":replay in a debug session takes start, or nothing: the recording's start\n" :
                *name == '\0' ? ":replay takes a checkpoint NAME\n" : "no checkpoint %s (:branches lists them)\n",
             name);
        return;
    }
    name = start ? "the recording's start" : name;
    (void)snprintf(since, sizeof since, "%s%s", start ? "" : "checkpoint ", name);
    np = path_of(s, &path, &upto);
    i = way_index(s, start ? -1 : k, path, upto, np, &cat);
    if (i >= np) {
        wb_f(b, i > np || np == 0 ? "checkpoint %s, or the current state, was not recorded, so no replay "
                                    "reaches it\n" :
                                    "checkpoint %s is not on the way to the current state; :restore it first\n",
             name);
        free(path);
        free(upto);
        return;
    }
    upto[i] -= cat;   /* from here on, upto[j] counts the boundaries of path[j] the replay passes */
    if (by != 0) {
        (by == 1 ? replay_changed : replay_when)(s, what, since, path, upto, i, cat, b);
        free(path);
        free(upto);
        return;
    }
    total = way_entries(s, path, upto, i, cat, &first, &last);
    if (first == 0) {
        wb_f(b, "no entry ran after %s\n", since);
    } else {
        wb_f(b, "replaying %llu %s from %s to the current state (entry %u", (unsigned long long)total,
             total == 1u ? "entry" : "entries", name, (unsigned)first);
        if (last != first) wb_f(b, " to entry %u", (unsigned)last);
        wb_s(b, "), comparing prints and outcomes with the recording\n");
    }
    int ok = 1, stopped = 0;
    if (s->nw != 0 && first != 0) {   /* the watches' values where the replay starts */
        ok = replay_seg(s, path[i], cat, NULL, NULL, &rp, b) == 0;
        if (ok) (void)watches_of(s, rp.ctx, rp.img, 0, W_QUIET, b);
        wb_replay_free(&rp);
    }
    for (uint32_t j = i + 1u; ok && !stopped && j-- > 0;) {
        uint64_t from = j == i ? cat : 0;
        ords = upto[j] != 0 ? ords_of(s, path[j]) : NULL;
        for (uint64_t at = from + 1u; ok && !stopped && ords != NULL && at <= from + upto[j]; at++) {
            if (s->nw == 0) {   /* without watches, one replay of the segment checks it */
                at = from + upto[j];
            }
            ok = replay_seg(s, path[j], at, NULL, NULL, &rp, b) == 0;
            stopped = ok && s->nw != 0 && watches_of(s, rp.ctx, rp.img, ords[at], W_REPLAY, b);
            if (ok) faults_of(s, path[j], s->nw != 0 ? at - 1u : from, at, ords, b);
            if (stopped) wb_f(b, "a watch stops the replay after entry %u\n", (unsigned)ords[at]);
            passed += ok && s->nw != 0 ? 1u : ok ? upto[j] : 0u;
            wb_replay_free(&rp);
        }
        free(ords);
        ords = NULL;
    }
    if (ok && first != 0) {
        wb_f(b, "replay matches the recording at %llu %s\n", (unsigned long long)passed,
             passed == 1u ? "boundary" : "boundaries");
    }
    free(path);
    free(upto);
    (void)watches(s, 0, W_QUIET, b);   /* the watches' values are the current state's again */
}

/* The bytes of a print, quoted, at most 120 of them. */
static void quoted(wb_buf *b, const uint8_t *p, size_t n)
{
    wb_s(b, "\"");
    for (size_t i = 0; i < n && i < 120u; i++) {
        unsigned c = p[i];
        if (c == '\n' || c == '\t' || c == '"' || c == '\\')
            wb_f(b, "\\%c", c == '\n' ? 'n' : c == '\t' ? 't' : (int)c);
        else if (c < 0x20u || c >= 0x7fu) wb_f(b, "\\x%02x", c);
        else wb_f(b, "%c", (int)c);
    }
    wb_s(b, n > 120u ? "\" ..." : "\"");
}

/* `:effects`: the events of the recording on the way to the current state (SPEC-06 5.3,
 * SPEC-03 H-14): each entry and its prints and outcome, each context reset, and the revision
 * set before each entry that changed it. The revision events themselves are not shown: every
 * expression and statement entry is a unit of the session module, so each makes one. A segment
 * the way leaves at a boundary is shown up to that boundary. */
static void cmd_effects(session *s, char *arg, wb_buf *b)
{
    (void)arg;
    uint32_t *path = NULL, entries = 0, prints = 0, resets = 0, revisions = 0;
    uint64_t *upto = NULL;
    wb_buf last = {0}, revs = {0};   /* the revision set of the last entry shown, and of this one */
    uint32_t np = path_of(s, &path, &upto);
    if (np == 0) {
        wb_s(b, "the current state is not recorded: no entry has run on it\n");
        return;
    }
    rec_close(s);   /* the live segment, path[0], complete */
    rec_open(s, (int32_t)path[0], (uint32_t)s->seg[path[0]].boundaries, SEG_CONT);
    for (uint32_t i = np; i-- > 0;) {
        const segment *x = &s->seg[path[i]];
        int whole = i == 0 || s->seg[path[i - 1u]].how == SEG_CONT;
        uint32_t *ords = ords_of(s, path[i]), from = 0, first_of = 0;
        if (x->parent >= 0 && x->how != SEG_CONT) {   /* the entry its state follows, or precedes */
            uint32_t *po = ords_of(s, (uint32_t)x->parent);
            from = po != NULL ? po[x->at] : 0;
            first_of = po != NULL && x->at == 0 && s->seg[x->parent].boundaries != 0 ? po[1] : 0;
            free(po);
        }
        if (x->how == SEG_NEW) {
            wb_s(b, "new context\n");
        } else if (x->how != SEG_CONT) {
            wb_s(b, x->how == SEG_GOTO ? ":goto" : "restored");
            if (from != 0) wb_f(b, ": the state at the end of entry %u\n", (unsigned)from);
            else if (first_of != 0) wb_f(b, ": the state before entry %u\n", (unsigned)first_of);
            else wb_s(b, "\n");
        }
        cint_recording_info info;
        cint_recording_event e;
        const wb_obj *o = x->ended == 1 ? wb_store_get(&s->store, x->id) : NULL;
        if (o == NULL || cint_recording_decode(o->p, o->n, &info) != CINT_OK) {
            wb_s(b, "  (this part of the recording cannot be read)\n");
            free(ords);
            continue;
        }
        memset(&e, 0, sizeof e);
        uint64_t at = 0;
        while ((whole || at < upto[i]) && cint_recording_next(&info, &e) == CINT_OK && e.kind != CINT_EVENT_END) {
            if (e.kind == CINT_EVENT_ENTRY) {
                uint32_t n = ords != NULL && at + 1u <= x->boundaries ? ords[at + 1u] : 0;
                const char *text = n != 0 && s->items[n - 1u] != NULL ? s->items[n - 1u] : "";
                size_t len = strcspn(text, "\n");
                const wb_obj *lo = n != 0 ? wb_store_get(&s->store, s->bnd[n - 1u].layout) : NULL;
                wb_record l;
                entries++;
                if (lo != NULL && wb_record_decode(lo->p, lo->n, &l) == 0) {   /* a change of revision set */
                    revs.n = 0;
                    revisions_text(&l, &revs);
                    wb_record_free(&l);
                    if (!revs.failed && (last.n != revs.n || memcmp(last.p, revs.p, revs.n) != 0)) {
                        revisions += last.n != 0;
                        wb_s(b, "revisions:");
                        wb_put(b, revs.p, revs.n);
                        wb_s(b, "\n");
                        last.n = 0;
                        wb_put(&last, revs.p, revs.n);
                    }
                }
                if (n != 0) {
                    wb_f(b, "entry %u   %.*s%s\n", (unsigned)n, (int)(len > 60u ? 60u : len), text,
                         len > 60u || text[len] != '\0' ? " ..." : "");
                } else {
                    wb_f(b, "entry   %.*s\n", (int)e.name.len, e.name.bytes);
                }
            } else if (e.kind == CINT_EVENT_EFFECT) {
                prints++;
                wb_f(b, "  %.*s ", (int)e.name.len, e.name.bytes);
                quoted(b, e.data, e.data_len);
                wb_s(b, "\n");
            } else if (e.kind == CINT_EVENT_OUTCOME) {
                char text[300];
                static wb_fault f;
                at += e.status == CINT_OK || e.status == CINT_FAULT;
                if (e.status == CINT_FAULT && e.data != NULL &&
                    wb_fault_decode(e.data, e.data_len, &f, WB_RECORD_RUN) == 0 && f.code < 14u) {
                    wb_f(b, "  fault %s", WB_CODE_NAMES[f.code]);
                } else if (e.has_result) {
                    tvalue_text(&e.result, 1, text, sizeof text);
                    wb_f(b, "  value %s", text);
                } else {
                    wb_s(b, e.status == CINT_OK ? "  done" : "  not run");
                }
                wb_f(b, "   fuel %lld   state ", (long long)e.consumed);
                wb_hex(b, e.hash, 4u);
                wb_s(b, "\n");
            } else if (e.kind == CINT_EVENT_CLEAR_FAULT) {
                resets++;
                wb_s(b, "context reset\n");
            } else if (e.kind == CINT_EVENT_CHECKPOINT || e.kind == CINT_EVENT_RESTORE) {
                wb_s(b, e.kind == CINT_EVENT_CHECKPOINT ? "checkpoint\n" : "restore\n");
            }
        }
        free(ords);
    }
    wb_f(b, "entries %u   prints %u   context resets %u   changes of revision set %u\n", (unsigned)entries,
         (unsigned)prints, (unsigned)resets, (unsigned)revisions);
    wb_free(&last);
    wb_free(&revs);
    free(path);
    free(upto);
}

/* -------------------------------------- Saved sessions (SPEC-06 5.6; BX14-19). */
/* The directory and its manifest, CINT-SESSION-1, are given in section 5.4 of the box 14 note. */

static const char *const OUTCOMES[] = {"refused", "compile error", "interrupted", "not run", "declared", "fault",
                                       "value", "done", "loaded", "reloaded", "not loaded", "error"};
#define NOUTCOMES (sizeof OUTCOMES / sizeof OUTCOMES[0])

static void id_end(wb_buf *m, const uint8_t *id)   /* " ID", or " -" for none, and the line's end */
{
    if (id != NULL) {
        wb_s(m, " ");
        wb_hex(m, id, 32u);
    }
    wb_s(m, id != NULL ? "\n" : " -\n");
}

static void index_or_none(wb_buf *m, int64_t k)   /* an index, or "-" for none */
{
    char n[24] = "";
    (void)snprintf(n, sizeof n, "%lld", (long long)k);
    wb_s(m, k >= 0 ? n : "-");
}

static void cmd_save(session *s, char *path, wb_buf *b)
{
    uint8_t current[32], sid[32], nid[32], id[32];
    wb_buf m = {0}, tail = {0};
    uint32_t named = 0;
    if (*path == '\0') {
        wb_s(b, ":save takes a PATH, the directory of the saved session\n");
        return;
    }
    uint32_t tip = s->rec != NULL ? s->nseg : 0;   /* the live segment ends, so the store holds it */
    rec_close(s);
    if (take(s, "", s->head, s->next, current, sid, nid, b) != 0) {
        if (tip != 0) rec_open(s, (int32_t)tip - 1, (uint32_t)s->seg[tip - 1u].boundaries, SEG_CONT);
        return;
    }
    for (uint32_t e = 1; e < s->next; e++) {   /* the texts go into the store first */
        const char *t = s->items[e - 1u] != NULL ? s->items[e - 1u] : "";
        int k = -1;
        for (size_t i = 0; s->outcome[e - 1u] != NULL && i < NOUTCOMES; i++)
            k = strcmp(s->outcome[e - 1u], OUTCOMES[i]) == 0 ? (int)i : k;
        tail.failed |= wb_store_put(&s->store, t, strlen(t), id) != 0;
        wb_f(&tail, "entry %u ", (unsigned)e);
        index_or_none(&tail, k);
        id_end(&tail, id);
    }
    for (uint32_t i = 0; i < s->nhist; i++)
        wb_f(&tail, "hist %u %u %s\n", (unsigned)s->hist[i].rev, (unsigned)s->hist[i].entry, s->hist[i].name);
    for (uint32_t i = 0; i < s->nbr; i++) {
        wb_s(&tail, "branch ");
        index_or_none(&tail, s->br[i].base);
        wb_f(&tail, " %s\n", s->br[i].name);
    }
    for (uint32_t k = 0; k < s->nck; k++) {
        const ckpt *c = &s->ck[k];
        named += c->number != 0;
        wb_f(&tail, "checkpoint %u %u ", (unsigned)c->number, (unsigned)c->branch);
        index_or_none(&tail, c->parent);
        id_end(&tail, c->record);
    }
    for (uint32_t i = 0; i < s->nundo; i++) wb_f(&tail, "undo %u\n", (unsigned)s->undo[i]);
    for (uint32_t k = 0; k < s->nw; k++) {
        const watch *w = &s->w[k];
        wb_f(&tail, "watch %u %d %d ", (unsigned)w->number, w->sc, w->stop);
        tail.failed |= wb_store_put(&s->store, w->expr, strlen(w->expr), id) != 0;
        wb_hex(&tail, id, 32u);
        if (w->cond != NULL) tail.failed |= wb_store_put(&s->store, w->cond, strlen(w->cond), id) != 0;
        id_end(&tail, w->cond != NULL ? id : NULL);
    }
    for (uint32_t g = 0; g < s->nseg; g++) {
        const segment *x = &s->seg[g];
        wb_s(&tail, "segment ");
        index_or_none(&tail, x->parent);
        wb_f(&tail, " %u %d %llu", (unsigned)x->at, x->how, (unsigned long long)x->boundaries);
        id_end(&tail, x->ended == 1 ? x->id : NULL);
    }
    for (uint32_t k = 0; k < s->nck; k++) {
        if (s->ck[k].seg != 0)
            wb_f(&tail, "position %u %u %u\n", (unsigned)k, (unsigned)s->ck[k].seg - 1u, (unsigned)s->ck[k].at);
    }
    for (uint32_t e = 1; e < s->next && e <= ITEMS_MAX; e++) {
        const bound *d = &s->bnd[e - 1u];
        if (d->seg != 0) {
            wb_f(&tail, "bound %u %u %u", (unsigned)e, (unsigned)d->seg - 1u, (unsigned)d->at);
            id_end(&tail, d->layout);
        }
    }
    if (tip != 0) wb_f(&tail, "tip %u %llu\n", (unsigned)tip - 1u, (unsigned long long)s->seg[tip - 1u].boundaries);
    wb_s(&tail, "end\n");
    wb_f(&m, "CINT-SESSION-1\nprofile cint-core-1\ncounters %u %u %u %u %u %u ", (unsigned)s->next,
         (unsigned)s->rev_max, (unsigned)s->numbered, (unsigned)s->autos, (unsigned)s->wnext, (unsigned)s->cur);
    index_or_none(&m, s->head);
    wb_s(&m, "\n");
    wb_s(&m, "current");
    id_end(&m, current);
    for (uint32_t i = 0; i < s->store.n; i++) {
        wb_s(&m, "object");
        id_end(&m, s->store.obj[i].id);
    }
    wb_put(&m, tail.p, tail.n);
    char *objects = wb_join(path, "objects"), *manifest = wb_join(path, "session.manifest");
    int bad = m.failed || tail.failed || objects == NULL || manifest == NULL || cli_mkdirs(objects) != 0;
    if (!bad) {
        (void)remove(manifest);   /* a save that does not finish leaves no manifest (SPEC-06 5.6) */
    }
    for (uint32_t i = 0; !bad && !g_stop && i < s->store.n; i++) {
        char hex[65], *file;
        hex_of(s->store.obj[i].id, hex);
        file = wb_join(objects, hex);
        bad = file == NULL || wb_write_file(file, s->store.obj[i].p, s->store.obj[i].n) != 0;
        free(file);
    }
    if (bad || g_stop) {
        wb_f(b, bad ? "workbench: cannot write the session under %s; it holds no saved session\n" :
                      "interrupted: the save stopped before its manifest; %s holds no saved session\n", path);
    } else if (wb_write_file(manifest, m.p, m.n) != 0) {
        wb_f(b, "workbench: cannot write %s; %s holds no saved session\n", manifest, path);
    } else {
        s->dirty = 0;
        wb_f(b, "saved session to %s   entries %u   checkpoints %u   objects %u\n", path, (unsigned)s->next - 1u,
             (unsigned)named, (unsigned)s->store.n);
    }
    free(objects);
    free(manifest);
    wb_free(&m);
    wb_free(&tail);
    if (tip != 0) {   /* the next segment goes on from the same state */
        rec_open(s, (int32_t)tip - 1, (uint32_t)s->seg[tip - 1u].boundaries, SEG_CONT);
    }
}

/* Where :save-case finds the sources of a revision: the revision sets of the layouts of the
 * entries and of the checkpoints, which name each module's text in the store, the live one, or
 * the case a debug session opened. */
typedef struct sources {
    const session *s;
    uint8_t (*rev)[32], (*id)[32];   /* a record of each revision, in entry order */
    uint32_t n, at;                  /* at: where the last one was found */
    cint_recording_info info;        /* the case; is_case 0 when none */
} sources;

static void source_add(sources *x, const uint8_t id[32])
{
    wb_record r;
    const wb_obj *o = wb_store_get(&x->s->store, id);
    if (o != NULL && wb_record_decode(o->p, o->n, &r) == 0) {
        memcpy(x->rev[x->n], r.revision, 32u);
        memcpy(x->id[x->n++], id, 32u);
        wb_record_free(&r);
    }
}

/* The text of module path[0..len) in the revision set r, or NULL. */
static const wb_obj *source_of_mod(const session *s, const wb_record *r, const char *path, size_t len)
{
    for (uint32_t k = 0; k < r->nmod; k++) {
        if (strlen(r->mod[k].path) == len && memcmp(r->mod[k].path, path, len) == 0)
            return wb_store_get(&s->store, r->mod[k].text);
    }
    return NULL;
}

static const uint8_t *source_in(void *user, const uint8_t rev[32], const char *path, size_t len, size_t *n)
{
    sources *x = user;
    cint_recording_event e;
    const wb_obj *t = memcmp(x->s->live.revision, rev, 32u) == 0 ? source_of_mod(x->s, &x->s->live, path, len) : NULL;
    for (uint32_t c = 0; t == NULL && c < x->n; c++) {   /* from the last found, as the way asks in order */
        uint32_t i = (x->at + c) % x->n;
        const wb_obj *o = memcmp(x->rev[i], rev, 32u) == 0 ? wb_store_get(&x->s->store, x->id[i]) : NULL;
        wb_record r;
        if (o != NULL && wb_record_decode(o->p, o->n, &r) == 0) {
            t = source_of_mod(x->s, &r, path, len);
            x->at = t != NULL ? i : x->at;
            wb_record_free(&r);
        }
    }
    if (t != NULL) {
        *n = t->n;
        return t->p != NULL ? t->p : (const uint8_t *)"";
    }
    memset(&e, 0, sizeof e);
    for (int first = 1; x->info.is_case && (first || cint_recording_next(&x->info, &e) == CINT_OK); first = 0) {
        uint32_t count = first ? x->info.module_count : e.kind == CINT_EVENT_REVISION ? e.module_count : 0u;
        for (uint32_t k = 0; memcmp(first ? x->info.revision : e.revision, rev, 32u) == 0 && k < count; k++) {
            cint_code_module m;
            if (cint_recording_module(&x->info, first ? NULL : &e, k, &m) == CINT_OK && m.path.len == len &&
                memcmp(m.path.bytes, path, len) == 0) {
                *n = m.source_len;
                return m.source;
            }
        }
    }
    return NULL;
}

/* `:save-case PATH`: the case (CINT-CASE-1, SPEC-06 9.2) of the way to the current state from
 * the checkpoint it follows, or from the recording's start. Each segment on the way is made
 * again under a recorder in case mode, with each revision's sources, and the case replays to
 * the current state before it is written. */
static void cmd_save_case(session *s, char *file, wb_buf *b)
{
    uint32_t *path = NULL, np, i, cat = 0, first = 0, last = 0;
    uint64_t *upto = NULL, effects = 0;
    uint8_t state[32], now[32];
    int32_t from = s->head;
    int unread = 0;
    const char *what;
    wb_part *parts = NULL;
    wb_buf out = {0};
    sources x;
    memset(&x, 0, sizeof x);
    x.s = s;
    if (*file == '\0' || (s->o->debug != NULL && (cint_recording_decode(s->o->debug, s->o->debug_len, &x.info) !=
                                                  CINT_OK || !x.info.is_case))) {
        wb_s(b, *file == '\0' ? ":save-case takes a PATH, the file of the case\n" :
                                ":save-case needs each module's source, which a case holds and a recording does not\n");
        return;
    }
    if (s->rec != NULL) {   /* the live segment ends, so the store holds it, and the next goes on */
        uint32_t g = s->nseg - 1u, at = (uint32_t)s->seg[g].boundaries;
        rec_close(s);
        rec_open(s, (int32_t)g, at, SEG_CONT);
    }
    np = path_of(s, &path, &upto);
    i = from >= 0 && np != 0 ? way_index(s, from, path, upto, np, &cat) : np;
    if (np != 0 && i >= np) {   /* from the start of the way */
        from = -1;
        i = np - 1u;
        cat = 0;
    }
    what = from >= 0 ? s->ck[from].name : "the recording's start";
    if (np != 0) {
        upto[i] -= cat;
        (void)way_entries(s, path, upto, i, cat, &first, &last);
    }
    uint32_t lo = s->rec != NULL && path[0] + 1u == s->nseg;   /* the live segment, just begun, holds nothing */
    parts = first != 0 && i >= lo ? calloc(i + 1u - lo, sizeof *parts) : NULL;
    x.rev = parts != NULL ? malloc(((size_t)s->next + s->nck) * sizeof *x.rev) : NULL;
    x.id = x.rev != NULL ? malloc(((size_t)s->next + s->nck) * sizeof *x.id) : NULL;
    for (uint32_t p = 0; x.id != NULL && p + lo <= i; p++) {   /* the earliest first */
        uint32_t g = path[i - p];
        const wb_obj *o = s->seg[g].ended == 1 ? wb_store_get(&s->store, s->seg[g].id) : NULL;
        unread |= o == NULL;
        parts[p].rec = o != NULL ? o->p : NULL;
        parts[p].n = o != NULL ? o->n : 0;
        parts[p].from = p == 0 ? cat : 0;
        parts[p].to = parts[p].from + upto[i - p];
        parts[p].cont = s->seg[g].how == SEG_CONT;
    }
    for (uint32_t n = 1; x.id != NULL && n < s->next && n <= ITEMS_MAX; n++) {
        if (s->bnd[n - 1u].seg != 0) source_add(&x, s->bnd[n - 1u].layout);
    }
    for (uint32_t k = 0; x.id != NULL && k < s->nck; k++) source_add(&x, s->ck[k].record);
    if (np == 0) {
        wb_s(b, "the current state was not recorded, so no case reaches it\n");
    } else if (first == 0) {
        wb_f(b, "no entry ran after %s%s, so a case would hold none\n", from >= 0 ? "checkpoint " : "", what);
    } else if (x.id == NULL || unread) {
        wb_s(b, unread ? "workbench: the recording of this state cannot be read\n" : "workbench: out of memory\n");
    } else if (wb_case_write(parts, i + 1u - lo, source_in, &x, &out, state, &effects, b) == 0) {
        if (cint_ctx_state_hash(s->ctx, now) != CINT_OK || memcmp(now, state, 32u) != 0) {
            wb_s(b, "workbench: the case does not end in the current state; no case is written\n");
        } else if (wb_write_file(file, out.p, out.n) != 0) {
            wb_f(b, "workbench: cannot write %s\n", file);
        } else {
            wb_s(b, "note: the case includes every recorded observe result verbatim; this case has none\n");
            wb_f(b, "case %s   %s%s   entr%s %u", file, from >= 0 ? "checkpoint " : "from ", what,
                 first == last ? "y" : "ies", (unsigned)first);
            if (first != last) wb_f(b, "..%u", (unsigned)last);
            wb_f(b, "   effects %llu%s   density entry\n", (unsigned long long)effects, effects != 0 ? " (print)" : "");
        }
    }
    wb_free(&out);
    free(x.rev);
    free(x.id);
    free(parts);
    free(path);
    free(upto);
}

/* The words of a manifest line, split at single spaces, at most max. Returns the count, or -1. */
static int words_of(char *line, char **w, int max)
{
    int n = 0;
    for (char *p = line;; ) {
        char *sp = strchr(p, ' ');
        if (n == max || *p == '\0' || p == sp) return -1;
        w[n++] = p;
        if (sp == NULL) return n;
        *sp = '\0';
        p = sp + 1;
    }
}

/* A decimal of at most max, with no sign and no leading zero; "-" is -1 when dash is set. */
static int num_of(const char *t, uint64_t max, int dash, int64_t *v)
{
    uint64_t x = 0;
    if (dash && strcmp(t, "-") == 0) {
        *v = -1;
        return 0;
    }
    if (*t == '\0' || (t[0] == '0' && t[1] != '\0')) return -1;
    for (; *t != '\0'; t++) {
        uint64_t d = (uint64_t)(unsigned char)*t - '0';
        if (d > 9u || d > max || x > (max - d) / 10u) return -1;
        x = x * 10u + d;
    }
    *v = (int64_t)x;
    return 0;
}

/* A name of 64 lowercase hexadecimal digits into id. */
static int id_of(const char *t, uint8_t id[32])
{
    if (strlen(t) != 64u) return -1;
    for (unsigned i = 0; i < 64u; i++) {
        unsigned c = (unsigned char)t[i], v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10u : 16u;
        if (v == 16u) return -1;
        id[i / 2u] = (uint8_t)(i % 2u != 0 ? (id[i / 2u] | v) : v << 4);
    }
    return 0;
}

/* The object a name designates, which st must hold. */
static const wb_obj *obj_of(const wb_store *st, const char *t, uint8_t id[32])
{
    return id_of(t, id) == 0 ? wb_store_get(st, id) : NULL;
}

/* A text object as a string: no NUL inside, and not empty when `full`. */
static char *text_of(const wb_obj *o, int full)
{
    char *t = o != NULL && memchr(o->p, '\0', o->n) == NULL && (!full || o->n != 0) ? malloc(o->n + 1u) : NULL;
    if (t != NULL) {
        memcpy(t, o->p, o->n);
        t[o->n] = '\0';
    }
    return t;
}

/* Record `id` of st, checked as :restore needs it: its objects are in st, its CINT-CKPT-1
 * decodes, and its revision numbers are at most rev_max. Its identities go to sid and nid. */
static int record_ok(const wb_store *st, const uint8_t id[32], uint32_t rev_max, wb_record *r, uint8_t sid[32],
                     uint8_t nid[32])
{
    const wb_obj *o = wb_store_get(st, id), *ck;
    cint_checkpoint_info info;
    memset(&info, 0, sizeof info);
    if (o == NULL || wb_record_decode(o->p, o->n, r) != 0) return -1;
    int bad = r->rev > rev_max ||
              (r->has_state && ((ck = wb_store_get(st, r->state)) == NULL ||
                                cint_checkpoint_decode(ck->p, ck->n, &info) != CINT_OK));
    for (uint32_t k = 0; !bad && k < r->nmod; k++) bad = wb_store_get(st, r->mod[k].text) == NULL;
    for (uint32_t i = 0; !bad && i < r->ndecl; i++) {
        const wb_obj *t = wb_store_get(st, r->decl[i].text);
        bad = t == NULL || memchr(t->p, '\0', t->n) != NULL;
    }
    if (bad || wb_record_ids(r, info.state, info.state_len, sid, nid) != 0) {
        wb_record_free(r);
        return -1;
    }
    return 0;
}

/* Frees the entries, history, checkpoints, undo points, watches, recording, and objects of x. */
static void free_history(session *x)
{
    for (uint32_t i = 0; i < ITEMS_MAX && i + 1u < x->next; i++) free(x->items[i]);
    for (uint32_t k = 0; k < x->nw; k++) free_watch(&x->w[k]);
    for (uint32_t g = 0; g < x->nseg; g++) wb_free(&x->seg[g].rec);
    free(x->hist);
    free(x->ck);
    free(x->undo);
    free(x->seg);
    wb_store_free(&x->store);
}

enum { M_COUNTERS, M_CURRENT, M_OBJECT, M_ENTRY, M_HIST, M_BRANCH, M_CHECKPOINT, M_UNDO, M_WATCH, M_SEGMENT,
       M_POSITION, M_BOUND, M_TIP, M_END };

/* One manifest line into t; *count is the entries read. Returns 0, or -1 when it is not well
 * formed. */
static int manifest_line(session *t, const char *dir, int kind, char **w, int n, uint8_t current[32], uint32_t *count)
{
    int64_t v[7];
    uint8_t id[32], sid[32], nid[32], want[32];
    wb_record r;
    switch (kind) {
    case M_COUNTERS:
        if (n != 8 || num_of(w[1], ITEMS_MAX + 1u, 0, &v[0]) != 0 || v[0] < 1 || num_of(w[2], 1u << 30, 0, &v[1]) != 0 ||
            num_of(w[3], 1u << 30, 0, &v[2]) != 0 || num_of(w[4], 1u << 30, 0, &v[3]) != 0 ||
            num_of(w[5], 1u << 30, 0, &v[4]) != 0 || num_of(w[6], BRANCHES_MAX - 1u, 0, &v[5]) != 0 ||
            num_of(w[7], 1u << 30, 1, &v[6]) != 0 || v[3] > v[2]) {
            return -1;
        }
        t->next = (uint32_t)v[0];
        t->rev_max = (uint32_t)v[1];
        t->numbered = (uint32_t)v[2];
        t->autos = (uint32_t)v[3];
        t->wnext = (uint32_t)v[4];
        t->cur = (uint32_t)v[5];
        t->head = (int32_t)v[6];
        return 0;
    case M_CURRENT:
        return n == 2 && id_of(w[1], current) == 0 ? 0 : -1;
    case M_OBJECT: {   /* the file's bytes must have its name */
        char *file = n == 2 && id_of(w[1], want) == 0 ? wb_join(dir, w[1]) : NULL;
        size_t len = 0;
        char *bytes = file != NULL ? wb_read_file(file, &len) : NULL;
        int bad = bytes == NULL || wb_store_put(&t->store, bytes, len, id) != 0 || memcmp(id, want, 32u) != 0;
        free(file);
        free(bytes);
        return bad ? -1 : 0;
    }
    case M_ENTRY: {
        uint32_t e = *count + 1u;
        if (n != 4 || num_of(w[1], ITEMS_MAX, 0, &v[0]) != 0 || (uint32_t)v[0] != e || e >= t->next ||
            num_of(w[2], NOUTCOMES - 1u, 1, &v[1]) != 0 ||
            (t->items[e - 1u] = text_of(obj_of(&t->store, w[3], id), 0)) == NULL) {
            return -1;
        }
        t->outcome[e - 1u] = v[1] >= 0 ? OUTCOMES[v[1]] : NULL;
        *count = e;
        return 0;
    }
    case M_HIST:
        if (n != 4 || num_of(w[1], t->rev_max, 0, &v[0]) != 0 || v[0] < 1 || num_of(w[2], ITEMS_MAX, 0, &v[1]) != 0 ||
            v[1] < 1 || (uint64_t)v[1] >= t->next || !(wb_name_ok(w[3], IDENT_MAX - 1u) || wb_path_ok(w[3]))) {
            return -1;
        }
        n = (int)t->nhist;
        add_hist(t, w[3], (uint32_t)v[0], (uint32_t)v[1]);
        return t->nhist == (uint32_t)n + 1u ? 0 : -1;
    case M_BRANCH:
        if (n != 3 || t->nbr == BRANCHES_MAX || num_of(w[1], 1u << 30, 1, &v[0]) != 0 || !wb_name_ok(w[2], WB_NAME_MAX))
            return -1;
        for (uint32_t i = 0; i < t->nbr; i++) {
            if (strcmp(t->br[i].name, w[2]) == 0) return -1;
        }
        (void)snprintf(t->br[t->nbr].name, sizeof t->br[t->nbr].name, "%s", w[2]);
        t->br[t->nbr++].base = (int32_t)v[0];
        return 0;
    case M_CHECKPOINT: {
        if (n != 5 || num_of(w[1], t->numbered, 0, &v[0]) != 0 || num_of(w[2], BRANCHES_MAX - 1u, 0, &v[1]) != 0 ||
            (uint32_t)v[1] >= t->nbr || num_of(w[3], 1u << 30, 1, &v[2]) != 0 || v[2] >= (int64_t)t->nck ||
            obj_of(&t->store, w[4], id) == NULL || record_ok(&t->store, id, t->rev_max, &r, sid, nid) != 0) {
            return -1;
        }
        int bad = r.ordinal > t->next || (v[0] == 0) != (r.name[0] == '\0') || (v[0] == 0 && v[2] >= 0) ||
                  r.has_parent != (v[2] >= 0) || (v[2] >= 0 && (t->ck[v[2]].number == 0 ||
                                                               memcmp(r.parent, t->ck[v[2]].nid, 32u) != 0));
        for (uint32_t k = 0; !bad && v[0] != 0 && k < t->nck; k++)
            bad = t->ck[k].number == (uint32_t)v[0] || (t->ck[k].number != 0 && strcmp(t->ck[k].name, r.name) == 0);
        uint32_t cur = t->cur;
        t->cur = (uint32_t)v[1];   /* add_ckpt puts a checkpoint on the current branch */
        bad = bad || add_ckpt(t, r.name, (uint32_t)v[0], (int32_t)v[2], (uint32_t)r.ordinal, id, sid, nid) < 0;
        t->cur = cur;
        wb_record_free(&r);
        return bad ? -1 : 0;
    }
    case M_UNDO: {
        if (n != 2 || num_of(w[1], 1u << 30, 0, &v[0]) != 0 || v[0] >= (int64_t)t->nck || t->ck[v[0]].number != 0)
            return -1;
        uint32_t *grown = room(t->undo, t->nundo, &t->capundo, sizeof *grown);
        if (grown == NULL) return -1;
        t->undo = grown;
        t->undo[t->nundo++] = (uint32_t)v[0];
        return 0;
    }
    case M_WATCH: {
        watch *x = &t->w[t->nw];
        if (n != 6 || t->nw == WATCHES_MAX || num_of(w[1], t->wnext, 0, &v[0]) != 0 || v[0] < 1 ||
            (t->nw != 0 && (uint32_t)v[0] <= t->w[t->nw - 1u].number) || num_of(w[2], 9u, 0, &v[1]) != 0 || v[1] < 1 ||
            num_of(w[3], 1u, 0, &v[2]) != 0) {
            return -1;
        }
        memset(x, 0, sizeof *x);
        x->number = (uint32_t)v[0];
        x->sc = (int)v[1];
        x->stop = (int)v[2];
        x->expr = text_of(obj_of(&t->store, w[4], id), 1);
        x->cond = strcmp(w[5], "-") != 0 ? text_of(obj_of(&t->store, w[5], id), 1) : NULL;
        if (x->expr == NULL || (strcmp(w[5], "-") != 0 && x->cond == NULL)) {
            free_watch(x);
            return -1;
        }
        t->nw++;
        return 0;
    }
    case M_SEGMENT: {   /* its parent comes before it, and its recording decodes */
        cint_recording_info info;
        const wb_obj *o = n == 6 && strcmp(w[5], "-") != 0 ? obj_of(&t->store, w[5], id) : NULL;
        if (n != 6 || num_of(w[1], 1u << 30, 1, &v[0]) != 0 || v[0] >= (int64_t)t->nseg ||
            num_of(w[2], ITEMS_MAX, 0, &v[1]) != 0 ||
            (v[0] < 0 ? v[1] != 0 : (uint64_t)v[1] > t->seg[v[0]].boundaries) ||
            num_of(w[3], SEG_CONT, 0, &v[2]) != 0 || num_of(w[4], ITEMS_MAX, 0, &v[3]) != 0 ||
            (strcmp(w[5], "-") != 0 && (o == NULL || cint_recording_decode(o->p, o->n, &info) != CINT_OK ||
                                         info.boundaries != (uint64_t)v[3]))) {
            return -1;
        }
        segment *grown = room(t->seg, t->nseg, &t->capseg, sizeof *grown);
        if (grown == NULL) return -1;
        t->seg = grown;
        segment *g = &t->seg[t->nseg++];
        memset(g, 0, sizeof *g);
        g->parent = (int32_t)v[0];
        g->at = (uint32_t)v[1];
        g->how = (int)v[2];
        g->boundaries = (uint64_t)v[3];
        g->ended = o != NULL ? 1 : -1;
        if (o != NULL) memcpy(g->id, id, 32u);
        return 0;
    }
    case M_POSITION:
    case M_TIP:
        if (n != (kind == M_TIP ? 3 : 4) ||
            (kind == M_POSITION && (num_of(w[1], 1u << 30, 0, &v[0]) != 0 || v[0] >= (int64_t)t->nck)) ||
            num_of(w[n - 2], 1u << 30, 0, &v[1]) != 0 || v[1] >= (int64_t)t->nseg ||
            num_of(w[n - 1], ITEMS_MAX, 0, &v[2]) != 0 || (uint64_t)v[2] > t->seg[v[1]].boundaries ||
            (kind == M_TIP && t->tip != 0)) {
            return -1;
        }
        if (kind == M_TIP) {
            t->tip = (uint32_t)v[1] + 1u;
            t->tip_at = (uint32_t)v[2];
        } else {
            t->ck[v[0]].seg = (uint32_t)v[1] + 1u;
            t->ck[v[0]].at = (uint32_t)v[2];
        }
        return 0;
    case M_BOUND: {   /* a boundary of its segment, with a layout that :goto restores */
        if (n != 5 || num_of(w[1], ITEMS_MAX, 0, &v[0]) != 0 || v[0] < 1 || (uint64_t)v[0] >= t->next ||
            t->bnd[v[0] - 1].seg != 0 || num_of(w[2], 1u << 30, 0, &v[1]) != 0 || v[1] >= (int64_t)t->nseg ||
            num_of(w[3], ITEMS_MAX, 0, &v[2]) != 0 || v[2] < 1 || (uint64_t)v[2] > t->seg[v[1]].boundaries ||
            obj_of(&t->store, w[4], id) == NULL || record_ok(&t->store, id, t->rev_max, &r, sid, nid) != 0) {
            return -1;
        }
        wb_record_free(&r);
        t->bnd[v[0] - 1].seg = (uint32_t)v[1] + 1u;
        t->bnd[v[0] - 1].at = (uint32_t)v[2];
        memcpy(t->bnd[v[0] - 1].layout, id, 32u);
        return 0;
    }
    default:
        return n == 1 ? 0 : -1;
    }
}

/* Reads DIR/session.manifest and the objects it names into t. Returns 0, or -1 with the
 * reason in b. */
static int read_manifest(session *t, const char *dir, uint8_t current[32], wb_buf *b)
{
    static const char *const KINDS[] = {"counters", "current", "object", "entry", "hist", "branch", "checkpoint",
                                        "undo", "watch", "segment", "position", "bound", "tip", "end"};
    size_t len = 0;
    char *file = wb_join(dir, "session.manifest"), *objects = wb_join(dir, "objects");
    char *text = file != NULL ? wb_read_file(file, &len) : NULL, *w[8];
    uint32_t line = 0, count = 0;
    uint8_t sid[32], nid[32];
    wb_record r;
    int last = -1, bad = 0;
    if (text == NULL || objects == NULL) {
        wb_f(b, "no session manifest at %s/session.manifest: not a saved session, or a save that did not finish; "
                "nothing changed\n", dir);
        free(file);
        free(objects);
        free(text);
        return -1;
    }
    bad = memchr(text, '\0', len) != NULL || memchr(text, '\r', len) != NULL;
    for (char *p = text; !bad && last != M_END && *p != '\0'; line++) {
        char *eol = strchr(p, '\n');
        if (eol == NULL) {
            break;   /* the last line has no end: the save did not finish */
        }
        *eol = '\0';
        if (line < 2u) {
            bad = strcmp(p, line == 0 ? "CINT-SESSION-1" : "profile cint-core-1") != 0;
        } else {
            int n = words_of(p, w, 8), kind = 0;
            while (n > 0 && kind <= M_END && strcmp(w[0], KINDS[kind]) != 0) kind++;
            bad = n <= 0 || kind > M_END || kind < last || (kind == last && kind <= M_CURRENT) ||
                  (last < M_CURRENT && kind > M_COUNTERS && kind != last + 1) ||
                  manifest_line(t, objects, kind, w, n, current, &count) != 0;
            last = kind;
        }
        p = eol + 1;
        bad |= last == M_END && *p != '\0';
    }
    if (!bad && last != M_END) {
        wb_f(b, "%s/session.manifest is incomplete: a save that did not finish; nothing changed\n", dir);
    } else if (bad || count + 1u != t->next || t->nbr == 0 || t->cur >= t->nbr || t->head >= (int32_t)t->nck ||
               (t->head >= 0 && t->ck[t->head].number == 0) ||
               record_ok(&t->store, current, t->rev_max, &r, sid, nid) != 0) {
        bad = 1;
    } else {
        wb_record_free(&r);
        for (uint32_t i = 0; !bad && i < t->nbr; i++)
            bad = t->br[i].base >= (int32_t)t->nck || (t->br[i].base >= 0 && t->ck[t->br[i].base].number == 0);
    }
    if (bad) {
        wb_f(b, ":open: %s/session.manifest is not a CINT-SESSION-1 session this workbench reads (line %u); "
                "nothing changed\n", dir, (unsigned)line);
    }
    free(file);
    free(objects);
    free(text);
    return bad || last != M_END ? -1 : 0;
}

static void cmd_open(session *s, char *path, wb_buf *b)
{
    uint8_t current[32];
    session *t = *path != '\0' ? calloc(1, sizeof *t) : NULL;
    if (*path == '\0') {
        wb_s(b, ":open takes a PATH, the directory of a saved session\n");
        return;
    }
    if (t == NULL) {
        wb_s(b, "workbench: out of memory\n");
        return;
    }
    t->o = s->o;
    if (read_manifest(t, path, current, b) != 0 || restore_record(s, &t->store, current, b) != 0) {
        free_history(t);
        free(t);
        return;
    }
    /* the session is now the saved one: its entries, history, checkpoints, watches, and
     * recording (the session's ended with restore_record) */
    free_history(s);
    memcpy(s->items, t->items, sizeof s->items);
    for (uint32_t i = 0; i < ITEMS_MAX; i++) {
        s->outcome[i] = t->outcome[i];   /* by element: memcpy takes void *, and MSVC warns (C4090) */
    }
    s->next = t->next;
    s->hist = t->hist;
    s->nhist = t->nhist;
    s->caphist = t->caphist;
    s->store = t->store;
    s->ck = t->ck;
    s->nck = t->nck;
    s->capck = t->capck;
    s->undo = t->undo;
    s->nundo = t->nundo;
    s->capundo = t->capundo;
    memcpy(s->br, t->br, sizeof s->br);
    s->nbr = t->nbr;
    s->cur = t->cur;
    s->head = t->head;
    s->numbered = t->numbered;
    s->autos = t->autos;
    s->rev_max = t->rev_max;
    memcpy(s->w, t->w, sizeof s->w);
    s->nw = t->nw;
    s->wnext = t->wnext;
    s->wepoch++;
    s->seg = t->seg;
    s->nseg = t->nseg;
    s->capseg = t->capseg;
    memcpy(s->bnd, t->bnd, sizeof s->bnd);
    rec_open(s, (int32_t)t->tip - 1, t->tip_at, t->tip != 0 ? SEG_CONT : SEG_RESTORE);
    s->dirty = 0;
    free(t);
    uint32_t named = 0;
    for (uint32_t k = 0; k < s->nck; k++) named += s->ck[k].number != 0;
    wb_f(b, "opened %s   entries %u   checkpoints %u   branch %s\n", path, (unsigned)s->next - 1u, (unsigned)named,
         s->br[s->cur].name);
    disk_differs(s, b);
    (void)watches(s, 0, W_QUIET, b);
}

/* Keeps a command that is an entry (:load, :reload, :run) as entry `ordinal`. */
static int keep(session *s, uint32_t ordinal, const char *name, const char *arg, wb_buf *b)
{
    size_t n = strlen(name) + strlen(arg) + 3u;
    char *text = ordinal <= ITEMS_MAX ? malloc(n) : NULL;
    if (text == NULL) {
        wb_s(b, ordinal <= ITEMS_MAX ? "workbench: out of memory\n" : "workbench: the session holds no more entries\n");
        return -1;
    }
    (void)snprintf(text, n, ":%s%s%s", name, *arg != '\0' ? " " : "", arg);
    s->items[ordinal - 1u] = text;
    s->next++;
    s->dirty = 1;
    return 0;
}

/* The commands, and the handlers of those command() does not handle itself. */
typedef void command_fn(session *s, char *arg, wb_buf *b);
static const struct {
    const char *name;
    command_fn *run;
    int debug;   /* available in a debug session, which is read-only (SPEC-06 10.4) */
} COMMANDS[] = {
    {"load", NULL, 0}, {"reload", NULL, 0}, {"run", NULL, 0}, {"inspect", NULL, 1}, {"type", NULL, 1},
    {"explain", cmd_explain, 0}, {"fault", NULL, 1}, {"fuel", NULL, 0}, {"depth", NULL, 0}, {"backend", NULL, 1},
    {"history", cmd_history, 0}, {"log", NULL, 1}, {"checkpoint", cmd_checkpoint, 0}, {"restore", cmd_restore, 0},
    {"undo", cmd_undo, 0}, {"branch", cmd_branch, 0}, {"branches", cmd_branches, 0}, {"diff", cmd_diff, 0},
    {"watch", cmd_watch, 0}, {"unwatch", cmd_unwatch, 0}, {"watches", cmd_watches, 0}, {"save", cmd_save, 0},
    {"open", cmd_open, 0}, {"help", NULL, 1}, {"quit", NULL, 1}, {"goto", cmd_goto, 1}, {"trace", cmd_trace, 1},
    {"replay", cmd_replay, 1}, {"effects", cmd_effects, 1}, {"save-case", cmd_save_case, 1}, {"place", NULL, 1},
    {"faults", NULL, 1},
};

/* One command line. Returns 1 for :quit. */
static int command(session *s, uint32_t ordinal, char *line, wb_buf *b)
{
    char *name = line + 1, *arg = name;
    while (*arg != '\0' && *arg != ' ' && *arg != '\t') arg++;
    if (*arg != '\0') *arg++ = '\0';
    arg = trim(arg);
    int64_t n = 0;
    int asked = s->warned;   /* :quit and :open ask once when the session has unsaved changes */
    s->warned = 0;
    size_t k = 0, nk = sizeof COMMANDS / sizeof COMMANDS[0];
    while (k < nk && strcmp(name, COMMANDS[k].name) != 0) k++;
    if (k < nk && s->o->debug != NULL && !COMMANDS[k].debug) {
        wb_f(b, ":%s is not available in a debug session, which is read-only\n", name);
        return 0;
    }
    int quit = strcmp(name, "quit") == 0 || strcmp(name, "q") == 0;
    if ((quit || strcmp(name, "open") == 0) && s->dirty && asked != (quit ? 'q' : 'o') && strcmp(arg, "--yes") != 0) {
        wb_f(b, "the session has changes since it was last saved; :save PATH keeps them, and :%s again %s\n",
             quit ? "quit" : "open", quit ? "ends it" : "replaces them");
        s->warned = quit ? 'q' : 'o';
        return 0;
    }
    if (quit) {
        return 1;
    } else if (k < nk && COMMANDS[k].run != NULL) {
        COMMANDS[k].run(s, arg, b);
    } else if (strcmp(name, "place") == 0 || strcmp(name, "faults") == 0) {
        wb_f(b, ":%s " NO_DEVICE, name);
    } else if (strcmp(name, "help") == 0) {
        wb_s(b, HELP);
    } else if (strcmp(name, "fuel") == 0) {
        if (strcmp(arg, "none") == 0) {
            s->fuel = CINT_FUEL_UNBOUNDED;
            wb_s(b, "fuel: no allowance per entry\n");
        } else if (parse_count(arg, 0, &n) == 0) {
            s->fuel = n;
            wb_f(b, "fuel: %lld units per entry\n", (long long)n);
        } else {
            wb_s(b, ":fuel takes N >= 0, or none\n");
        }
    } else if (strcmp(name, "depth") == 0) {
        if (parse_count(arg, 1, &n) == 0) {
            s->depth = n;
            wb_f(b, "depth: %lld per entry\n", (long long)n);
        } else {
            wb_s(b, ":depth takes N >= 1\n");
        }
    } else if (strcmp(name, "backend") == 0) {
        if (*arg == '\0' || strcmp(arg, s->backend) == 0) {
            wb_f(b, "backend: %s\n", s->backend);
        } else if (strcmp(arg, "cpu-c17") == 0 && s->o->debug != NULL) {
            wb_s(b, ":backend cpu-c17 is not available in a debug session, which replays on cpu-sir-interp\n");
        } else if (strcmp(arg, "cpu-sir-interp") == 0 || strcmp(arg, "cpu-c17") == 0) {
            (void)snprintf(s->backend, sizeof s->backend, "%s", arg);
            wb_f(b, "backend: %s%s\n", arg, strcmp(arg, "cpu-c17") != 0 ? "" :
                 "; each revision is built as a library, entries on it are not recorded, and :inspect and the "
                 "watches stay on cpu-sir-interp");
        } else if (strcmp(arg, "gpu-cuda") == 0) {
            wb_s(b, ":backend gpu-cuda " NO_DEVICE);
        } else {
            wb_f(b, ":backend: no backend %s (cpu-sir-interp, cpu-c17, gpu-cuda)\n", arg);
        }
    } else if (strcmp(name, "load") == 0 || strcmp(name, "reload") == 0) {
        if (strcmp(name, "load") == 0 && *arg == '\0') wb_s(b, ":load takes FILE.ci\n");
        else if (keep(s, ordinal, name, arg, b) == 0) cmd_load(s, ordinal, arg, strcmp(name, "reload") == 0, b);
    } else if (strcmp(name, "run") == 0) {
        if (keep(s, ordinal, name, arg, b) == 0) cmd_run(s, ordinal, arg, b);
    } else if (strcmp(name, "inspect") == 0 || strcmp(name, "type") == 0) {
        (void)snprintf(s->scratch_path, sizeof s->scratch_path, "inspect");   /* the source of its positions */
        s->scratch = arg;
        inspect(s, arg, strcmp(name, "type") == 0, b);
        s->scratch = NULL;
    } else if (strcmp(name, "fault") == 0) {
        char error[] = "error";
        if (*arg != '\0' && strcmp(arg, "/path") != 0) {
            wb_s(b, ":fault takes /path, or nothing\n");
        } else if (*arg != '\0') {
            cmd_trace(s, error, b);
        } else if (s->fault.n == 0) {   /* an opened or debug session keeps no error result's report */
            wb_buf quiet = {0};
            uint32_t e = entry_named(s, "fault", error, &quiet);
            wb_free(&quiet);
            wb_s(b, e != 0 ? "no fault in this session; entry " : "no fault in this session\n");
            if (e != 0) wb_f(b, "%u left an error result, which :fault /path shows\n", (unsigned)e);
        } else {
            wb_put(b, s->fault.p, s->fault.n);
        }
    } else if (strcmp(name, "log") == 0) {
        for (uint32_t e = 1; e < s->next && e <= ITEMS_MAX; e++) {
            const char *t = s->items[e - 1u] != NULL ? s->items[e - 1u] : "";
            size_t len = strcspn(t, "\n");
            char label[24];
            (void)snprintf(label, sizeof label, "entry %u", (unsigned)e);
            wb_f(b, "%-11s %-14s %.*s%s\n", label, s->outcome[e - 1u] != NULL ? s->outcome[e - 1u] : "",
                 (int)(len > 60u ? 60u : len), t, len > 60u || t[len] != '\0' ? " ..." : "");
        }
    } else {
        wb_f(b, "no command :%s; :help lists them\n", name);
    }
    return 0;
}

/* ------------------------------------------------------------------- The loop. */

static int one_entry(session *s, char *text, wb_buf *b)
{
    uint32_t ordinal = s->next;
    char name[IDENT_MAX];
    const char *name_at, *init;
    if (*skip(text) == ':') return command(s, ordinal, (char *)skip(text), b);
    int kind = classify(text, name, &name_at, &init);
    if (kind == K_NONE) return 0;
    if (s->o->debug != NULL) {
        wb_s(b, "a debug session is read-only: it takes commands, not entries\n");
        return 0;
    }
    if (ordinal > ITEMS_MAX) {
        wb_f(b, "workbench: a session holds at most %u entries\n", ITEMS_MAX);
        return 1;
    }
    s->items[ordinal - 1u] = text;
    s->next++;
    s->dirty = 1;
    s->warned = 0;
    if (kind == K_IMPORT) {
        wb_s(b, "workbench: load a module with :load FILE.ci, which imports it into the session\n");
        note_outcome(s, ordinal, "refused");
    } else if (kind == K_VAR || kind == K_FUNC || kind == K_OTHER) {
        declaration(s, ordinal, text, kind, name, name_at, init, b);
    } else {
        statement(s, ordinal, text, kind, b);
    }
    return -1;   /* the text is kept */
}

/* An entry or a command; then what the watches did not print. */
static int entry(session *s, char *text, wb_buf *b)
{
    g_stop = 0;
    int r = one_entry(s, text, b);
    watch_summary(s, b);
    return r;
}

int wb_session(const wb_session_options *o, FILE *in)
{
    static session s;
    wb_buf b = {0}, text = {0};
    char line[65536];
    int status = CINT_EXIT_OK;
    memset(&s, 0, sizeof s);
    s.o = o;
    s.next = 1;
    s.fuel = CINT_FUEL_UNBOUNDED;
    s.depth = CINT_DEFAULT_DEPTH;
    (void)snprintf(s.backend, sizeof s.backend, "cpu-sir-interp");
    s.at_line_start = 1;
    s.nbr = 1;
    (void)snprintf(s.br[0].name, sizeof s.br[0].name, "main");
    s.br[0].base = -1;
    s.head = -1;
    g_stop = 0;
    g_entry = NULL;
    void (*was)(int) = signal(SIGINT, on_interrupt);
    s.src = wb_join(o->out, "src");
    s.build = wb_join(o->out, "build");
    s.log = wb_join(o->out, "cint.stderr");
    if (s.src == NULL || s.build == NULL || s.log == NULL || cli_mkdirs(s.src) != 0 || cli_mkdirs(s.build) != 0) {
        fprintf(stderr, "cint workbench: error: cannot make the session's directory %s\n", o->out);
        status = CINT_EXIT_ENVIRONMENT;
        goto done;
    }
    if (o->debug != NULL) {
        printf("cint workbench   profile cint-core-1   backend cpu-sir-interp   debug %s (read-only)\n", o->debug_path);
        int r = debug_open(&s, &b);
        sayf(&s, &b);
        if (r != 0) {
            status = CINT_EXIT_CHECK;
            goto done;
        }
    } else {
        printf("cint workbench   profile cint-core-1   backend cpu-sir-interp   new session\n");
    }
    fflush(stdout);
    if (o->load != NULL) {
        char load[1100];
        (void)snprintf(load, sizeof load, ":load %s", o->load);
        if (o->echo) printf("ci[%u]> %s\n", (unsigned)s.next, load);
        (void)command(&s, s.next, load, &b);
        sayf(&s, &b);
        s.dirty = 0;   /* the session as it starts */
    }
    for (;;) {
        if (text.n == 0) printf("ci[%u]> ", (unsigned)s.next);
        else printf("..> ");
        fflush(stdout);
        if (fgets(line, sizeof line, in) == NULL) {
            if (text.n == 0 && o->echo) printf("\n");
            break;
        }
        if (o->echo) printf("%s%s", line, strchr(line, '\n') != NULL ? "" : "\n");
        size_t n = strcspn(line, "\r\n");
        line[n] = '\0';
        if (text.n != 0) wb_s(&text, "\n");
        wb_s(&text, line);
        if (text.failed) {
            fprintf(stderr, "cint workbench: error: out of memory\n");
            status = CINT_EXIT_INTERNAL;
            break;
        }
        if (incomplete(text.p)) continue;
        s.at_line_start = 1;
        int r = entry(&s, text.p, &b);
        sayf(&s, &b);
        if (!s.at_line_start) {   /* the prompt starts a line of its own */
            fputc('\n', stdout);
        }
        if (r == -1) {
            text.p = NULL;   /* kept as the entry's text */
            text.cap = 0;
        }
        text.n = 0;
        if (r == 1) break;
    }
done:
    fflush(stdout);
    for (uint32_t i = 0; i < s.ndecl; i++) free(s.decls[i].text);
    for (uint32_t i = 0; i < s.nloaded; i++) free(s.loaded[i]);
    free_values(s.vals, s.nval);
    rec_close(&s);
    free_history(&s);
    cint_ctx_destroy(s.ctx);
    wb_c17_close(s.lib);
    cint_sir_release(s.live_img);
    if (s.watch_prog != s.prog && s.watch_prog != s.live_prog) cint_sir_free(s.watch_prog);
    if (s.live_prog != s.prog) cint_sir_free(s.live_prog);
    cint_sir_free(s.prog);
    wb_record_free(&s.live);
    wb_free(&s.written);
    wb_free(&s.fault);
    wb_free(&b);
    wb_free(&text);
    free(s.src);
    free(s.build);
    free(s.log);
    (void)signal(SIGINT, was != SIG_ERR ? was : SIG_DFL);
    return status;
}
