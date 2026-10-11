/* sir_read.c: the SIR text reader and verifier of cpu-sir-interp (SPEC-09 BACK-01a).
 *
 * Reads cint-program.sites and each module's canonical SIR text (SIR-09, SIR-09a),
 * checks the forms, the nesting, the sites, the temporaries and the operand types
 * (SIR-10), and recomputes the revision identity (SIR-17) from the bytes it read. The
 * first broken rule ends the read with its position. A struct is that of its struct line
 * (SIR-09a), in its module or, named `<module>.<Name>`, in a module read before it. The
 * view forms, reductions and frame arena nodes of box 09 are read with the type and
 * rank of each view (compiler/OPEN.md CINTC-OQ-35 item 5, CINTC-OQ-64 item 13), and so
 * are kernels and their dispatch, in the order of the C backend's dispatch (unit 7b), and
 * box 12's error forms with the error sets and conversions they name (unit 8b), and its
 * arena and pool lines, handles and memory nodes (unit 8b-2). The wide types I128 to I1024 and
 * U128 to U512 (box 15) are read with their limbs, and the fixed-point types Q<i>.<f> as their
 * storage integers. Status: Proposed, box 13 units 1, 3, 7a, 7b and 8b. */
#define _CRT_SECURE_NO_WARNINGS 1

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cint_rt.h"
#include "cint_mem.h"
#include "sir.h"

#define TEXT_MAX ((size_t)64 << 20)  /* bytes of one file */
#define COUNT_MAX 0x7FFFFFF0u        /* entries of one table */
#define STACK_MAX 4096u              /* nesting depth of one function */
#define SITE_COLUMN 49u              /* offset of `#` from the start of its line, at least */
#define CELLS_MAX 0x7FFFFFF0u        /* cells of one struct or one parameter list */
#define GLOBAL_MAX 65536u            /* elements of one module-level array, at least cintc's
                                        (compiler/lowering.ci, MODULE_ARRAY_BYTES) */

static const char *const SCALAR_NAMES[SC_COUNT] = {"", "Bool", "I8", "I16", "I32", "I64", "U8", "U16", "U32", "U64",
                                                   "I128", "I256", "I512", "I1024", "U128", "U256", "U512"};
static const char *const SCALAR_SUFFIXES[SC_COUNT] = {"", "bool", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64",
                                                      "i128", "i256", "i512", "i1024", "u128", "u256", "u512"};
static const char *const BIN_NAMES[] = {"add", "sub", "mul", "div", "rem", "shl", "shr", "and", "or", "xor"};
static const char *const CMP_NAMES[] = {"eq", "ne", "lt", "le", "gt", "ge"};
static const char *const FORM_NAMES[] = {"checked", "wrap", "sat", "result", "raw"};
static const char *const BUILTIN_NAMES[] = {"", "abs", "uabs", "min", "max", "clamp", "div_trunc", "rem_trunc",
                                            "div_euclid", "rem_euclid", "div_round", "muldiv", "mul_full",
                                            "isqrt", "isqrt_round", "rotl", "rotr", "sqrt", "qmul", "mul_int"};
static const char *const MODE_NAMES[] = {"", "floor", "ceil", "trunc", "away", "half_even", "half_away",
                                         "half_trunc", "half_up", "half_down"};

/* Built-in numbers (cint_rt.h section 13; compiler/fold.ci). */
enum { BI_UABS = 2, BI_MIN = 3, BI_MAX = 4, BI_CLAMP = 5, BI_DIV_ROUND = 10, BI_MULDIV = 11, BI_MUL_FULL = 12,
       BI_ISQRT = 13, BI_ISQRT_ROUND = 14, BI_ROTL = 15, BI_ROTR = 16 };

typedef struct caps {
    uint32_t site, global, item, func, node, opd, param, strct, field, eset, eval, widen, run, mem;
} caps;

typedef struct frame {
    uint8_t op;       /* OP_IF, OP_ARM, OP_THEN, OP_ELSE, OP_LOOP, OP_SWITCH, OP_CASE, OP_DEFAULT, OP_BLOCK; OP_CLOSE:
                         the function */
    uint8_t state;    /* if: 1 after an arm, 2 after else; loop: 1 after while, 2 after next; switch: 2 after
                         default; case: 1 after fallthrough */
    uint8_t scalar;   /* switch: the scrutinee's scalar */
    uint32_t node;
    int64_t label;
} frame;

typedef struct rd {
    const cint_sir_program *prog;  /* the modules read before this one */
    si_module *m;
    cint_sir_error *err;
    caps cap;
    const uint8_t *t;
    size_t pos;                    /* the cursor */
    size_t eol;                    /* the end of what the line holds, before a site annotation */
    size_t bol;                    /* the start of the line */
    uint32_t line;
    /* the function being read */
    si_func *f;
    si_type *temps;                /* types of %t0 .. %t(ntemp - 1) */
    uint32_t tcap;
    si_type *slots;                /* types of $x.k by k; TK_NONE until its local line */
    uint32_t scap;
    uint32_t *data;                /* the %s numbers defined so far */
    uint32_t ndata, dcap;
    frame *stack;
    uint32_t depth, fcap;
    uint32_t printing;             /* 1 between a print.begin and its print.end */
    uint32_t next_site;            /* the module's next site ordinal */
    uint32_t next_frame;           /* the next site of a frame.charge, after the module's (frame_site0) */
    uint32_t frame_site0;
    uint32_t *marks;               /* the function's frame.mark nodes, by their number in it */
    uint8_t *mparts;               /* and the parts each takes */
    uint32_t mcap, pcap;
    uint8_t charges;               /* a mark of the function takes or gives back charges */
    uint32_t *checks;              /* the function's latest index.checked of each place it checks */
    uint32_t ncheck, kcap;
} rd;

static size_t site_cut(const uint8_t *t, size_t s, size_t e);
static int all_digits(const uint8_t *t, size_t s, size_t e);
static int view_type(rd *r, si_type *ty);

/* ------------------------------------------------------------------ errors and tables */

static void set_error(cint_sir_error *err, const char *path, uint32_t line, uint32_t col, const char *fmt, va_list ap)
{
    snprintf(err->path, sizeof err->path, "%s", path);
    err->line = line;
    err->col = col;
    vsnprintf(err->text, sizeof err->text, fmt, ap);
}

static int fail_at(rd *r, size_t at, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    set_error(r->err, r->m->file, r->line, (uint32_t)(at - r->bol + 1u), fmt, ap);
    va_end(ap);
    return CINT_SIR_INPUT;
}

/* A form this unit reads but does not run (box 15: wide integers where the interpreter keeps a
 * cell), at its position. */
static int refuse_at(rd *r, size_t at, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    set_error(r->err, r->m->file, r->line, (uint32_t)(at - r->bol + 1u), fmt, ap);
    va_end(ap);
    return CINT_SIR_UNSUPPORTED;
}

static int fail_line(rd *r, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    set_error(r->err, r->m->file, r->line, 0, fmt, ap);
    va_end(ap);
    return CINT_SIR_INPUT;
}

static int fail_file(cint_sir_error *err, const char *path, int status, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    set_error(err, path, 0, 0, fmt, ap);
    va_end(ap);
    return status;
}

/* Room for entry n of `size` bytes in *p, which has room for *cap. */
static int grow(void **p, uint32_t *cap, uint32_t n, size_t size)
{
    if (n < *cap) return 1;
    if (*cap >= COUNT_MAX / 2u) return 0;
    uint32_t c = *cap ? *cap * 2u : 64u;
    void *q = realloc(*p, (size_t)c * size);
    if (q == NULL) return 0;
    *p = q;
    *cap = c;
    return 1;
}

static int oom(rd *r)
{
    return fail_file(r->err, r->m->file, CINT_SIR_RESOURCE, "out of memory");
}

/* ------------------------------------------------------------------ the cursor */

static int peek(const rd *r)
{
    return r->pos < r->eol ? r->t[r->pos] : -1;
}

static int peek_at(const rd *r, size_t k)
{
    return r->pos + k < r->eol ? r->t[r->pos + k] : -1;
}

/* Consumes `s` when the line continues with it. */
static int take(rd *r, const char *s)
{
    size_t n = strlen(s);
    if (r->eol - r->pos >= n && memcmp(r->t + r->pos, s, n) == 0) {
        r->pos += n;
        return 1;
    }
    return 0;
}

static int expect(rd *r, const char *s)
{
    return take(r, s) ? CINT_SIR_OK : fail_at(r, r->pos, "expected '%s'", s);
}

static int at_end(rd *r)
{
    return r->pos == r->eol ? CINT_SIR_OK : fail_at(r, r->pos, "unexpected text at the end of the line");
}

static int is_digit(int c)
{
    return c >= '0' && c <= '9';
}

static int is_ident_start(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_ident(int c)
{
    return is_ident_start(c) || is_digit(c);
}

static int ident(rd *r, si_name *out)
{
    size_t s = r->pos;
    if (!is_ident_start(peek(r))) return fail_at(r, s, "expected a name");
    while (is_ident(peek(r))) r->pos++;
    out->at = (uint32_t)s;
    out->len = (uint32_t)(r->pos - s);
    return CINT_SIR_OK;
}

/* Names joined by '.'. */
static int dotted(rd *r, si_name *out)
{
    size_t s = r->pos;
    si_name part = {0u, 0u};
    int st = ident(r, &part);
    while (st == CINT_SIR_OK && peek(r) == '.' && is_ident_start(peek_at(r, 1))) {
        r->pos++;
        st = ident(r, &part);
    }
    out->at = (uint32_t)s;
    out->len = (uint32_t)(r->pos - s);
    return st;
}

/* Decimal digits, no sign, no leading zero, at most max. */
static int number(rd *r, uint64_t max, uint64_t *out)
{
    size_t s = r->pos;
    uint64_t v = 0;
    if (!is_digit(peek(r))) return fail_at(r, s, "expected a number");
    if (peek(r) == '0' && is_digit(peek_at(r, 1)))
        return fail_at(r, s, "a number has no leading zero");
    while (is_digit(peek(r))) {
        uint64_t d = (uint64_t)(r->t[r->pos] - '0');
        if (v > (max - d) / 10u) return fail_at(r, s, "number out of range");
        v = v * 10u + d;
        r->pos++;
    }
    *out = v;
    return CINT_SIR_OK;
}

static int number32(rd *r, uint32_t *out)
{
    uint64_t v = 0;
    int st = number(r, COUNT_MAX, &v);
    *out = (uint32_t)v;
    return st;
}

/* A signed decimal (print.value format fields). */
static int signed_number(rd *r, int64_t *out)
{
    int neg = take(r, "-");
    uint64_t v = 0;
    int st = number(r, neg ? (uint64_t)INT64_MAX + 1u : (uint64_t)INT64_MAX, &v);
    *out = neg ? (int64_t)(0u - v) : (int64_t)v;
    return st;
}

static int name_is(const uint8_t *t, si_name n, const char *s)
{
    return n.len == strlen(s) && memcmp(t + n.at, s, n.len) == 0;
}

static int same_name(const uint8_t *a, si_name x, const uint8_t *b, si_name y)
{
    return x.len == y.len && memcmp(a + x.at, b + y.at, x.len) == 0;
}

static int lookup(const uint8_t *t, si_name n, const char *const *names, int count)
{
    for (int k = 0; k < count; k++)
        if (names[k][0] != '\0' && name_is(t, n, names[k])) return k;
    return -1;
}

/* The scalar a type name (suffix 0) or an operation name's suffix (1) names; a fixed-point type
 * Q<i>.<f>, suffix q<i>_<f>, with i >= 1 and f >= 0 written without a leading zero and i + f a
 * storage of 8 to 64 bits (SPEC-01 IM-54, IM-55), is SC_Q + i + 2f - 8 (sir.h); -1 for another. */
static int scalar_of(const uint8_t *t, si_name n, int suffix)
{
    unsigned v[2] = {0u, 0u};
    uint32_t p = n.at + 1u, e = n.at + n.len;
    int sc = lookup(t, n, suffix ? SCALAR_SUFFIXES : SCALAR_NAMES, SC_COUNT);
    if (sc >= 0 || n.len < 4u || t[n.at] != (suffix ? 'q' : 'Q')) return sc;
    for (int k = 0; k < 2; k++) {
        uint32_t d = p;
        while (p < e && is_digit(t[p]) && p - d < 3u) v[k] = v[k] * 10u + (unsigned)(t[p++] - '0');
        if (p == d || (t[d] == '0' && p - d > 1u) || (k == 0 && (p == e || t[p++] != (suffix ? '_' : '.'))))
            return -1;
    }
    unsigned w = v[0] + v[1];
    return p == e && v[0] >= 1u && (w == 8u || w == 16u || w == 32u || w == 64u) ? SC_Q + (int)(w - 8u + v[1]) : -1;
}

/* Extends name n over the `.<f>` of a fixed-point type name after it. */
static void q_tail(rd *r, si_name *n)
{
    if (peek(r) != '.' || !is_digit(peek_at(r, 1))) return;
    for (r->pos++; is_digit(peek(r));) r->pos++;
    n->len = (uint32_t)(r->pos - n->at);
}

/* ------------------------------------------------------------------ types and values */

static int is_wide(int sc)
{
    return sc >= SC_I128 && sc <= SC_U512;
}

static int is_signed(int sc)
{
    return (sc >= SC_I8 && sc <= SC_I64) || (sc >= SC_I128 && sc <= SC_I1024) || sc_is_q(sc);
}

/* I8 to U64: an integer in one cell. */
static int is_integer(int sc)
{
    return sc >= SC_I8 && sc <= SC_U64;
}

/* An integer of any width, wide ones included (box 15). */
static int is_arith(int sc)
{
    return is_integer(sc) || is_wide(sc);
}

static unsigned width_of(int sc)
{
    static const unsigned w[SC_COUNT] = {0, 1, 8, 16, 32, 64, 8, 16, 32, 64, 128, 256, 512, 1024, 128, 256, 512};
    return sc_is_q(sc) ? sc_qwidth(sc) : w[sc];
}

/* The cells of a scalar: a wide integer's limbs, else one. */
static unsigned limbs_of(int sc)
{
    return is_wide(sc) ? width_of(sc) / 64u : 1u;
}

static si_type scalar_type(int sc)
{
    si_type ty;
    memset(&ty, 0, sizeof ty);
    ty.kind = sc == SC_NONE ? TK_NONE : TK_SCALAR;
    ty.scalar = (uint8_t)sc;
    return ty;
}

static si_type special_type(uint8_t kind, uint8_t scalar)
{
    si_type ty = scalar_type(SC_NONE);
    ty.kind = kind;
    ty.scalar = scalar;
    return ty;
}

#define ANY_INTEGER special_type(TK_NONE, 0xFE)
#define ANY_SCALAR special_type(TK_NONE, 0xFD)
#define ANY_VALUE special_type(TK_NONE, 0xFC)
#define ANY_COUNT special_type(TK_NONE, 0xFB)   /* a shift's count: an integer, a wide one too (RT-OQ-52) */
#define UNKNOWN special_type(TK_UNKNOWN, 0)

/* A value of scalar sc: true or false, or a decimal integer in range; for fixed point `raw` and
 * its storage integer (box 15). */
static int value(rd *r, int sc, int64_t *out)
{
    size_t s = r->pos;
    if (sc_is_q(sc) && !take(r, "raw ")) return fail_at(r, s, "expected raw and the storage integer");
    if (sc == SC_BOOL) {
        if (take(r, "true")) {
            *out = 1;
        } else if (take(r, "false")) {
            *out = 0;
        } else {
            return fail_at(r, s, "expected true or false");
        }
        return CINT_SIR_OK;
    }
    int neg = take(r, "-");
    unsigned w = width_of(sc);
    uint64_t max = is_signed(sc) ? ((uint64_t)1 << (w - 1u)) - (neg ? 0u : 1u)
                                 : (w == 64u ? UINT64_MAX : ((uint64_t)1 << w) - 1u);
    uint64_t v = 0;
    if (neg && !is_signed(sc)) return fail_at(r, s, "an unsigned value has no sign");
    int st = number(r, max, &v);
    if (st == CINT_SIR_OK && neg && v == 0) return fail_at(r, s, "zero has no sign");
    *out = neg ? (int64_t)(0u - v) : (int64_t)v;
    return st;
}

/* A value of wide scalar sc (box 15): a decimal integer in range, written into its limbs, limb 0
 * first, in two's complement for I128 to I1024 and in binary for U128 to U512 (rt/cint_wide.h),
 * under the rules of value. */
static int wide_value(rd *r, int sc, uint64_t *limb)
{
    size_t s = r->pos;
    unsigned n = limbs_of(sc);
    int neg = take(r, "-");
    for (unsigned k = 0; k < n; k++) limb[k] = 0;
    if (neg && !is_signed(sc)) return fail_at(r, s, "an unsigned value has no sign");
    if (!is_digit(peek(r))) return fail_at(r, s, "expected a number");
    if (peek(r) == '0' && is_digit(peek_at(r, 1))) return fail_at(r, s, "a number has no leading zero");
    while (is_digit(peek(r))) {   /* the magnitude times ten plus the digit, below 2^(w - 1) or at it when
                                     negative; below 2^w unsigned */
        uint64_t carry = (uint64_t)(r->t[r->pos++] - '0');
        for (unsigned k = 0; k < n; k++) {
            uint64_t lo = limb[k] & 0xFFFFFFFFu, hi = limb[k] >> 32;
            uint64_t a = lo * 10u + (carry & 0xFFFFFFFFu), b = hi * 10u + (carry >> 32) + (a >> 32);
            limb[k] = (a & 0xFFFFFFFFu) | b << 32;
            carry = b >> 32;
        }
        int top = is_signed(sc) && (limb[n - 1u] >> 63) != 0u, more = top;
        for (unsigned k = 0; top && k + 1u < n; k++) more = more && limb[k] == 0u;
        if (carry != 0u || (top && !(neg && more && limb[n - 1u] == (uint64_t)1 << 63)))
            return fail_at(r, s, "number out of range");
    }
    int zero = 1;
    for (unsigned k = 0; k < n; k++) zero = zero && limb[k] == 0u;
    if (neg && zero) return fail_at(r, s, "zero has no sign");
    uint64_t c = 1;
    for (unsigned k = 0; neg && k < n; k++) {   /* two's complement */
        limb[k] = ~limb[k] + c;
        c = c != 0u && limb[k] == 0u;
    }
    return CINT_SIR_OK;
}

/* A wide value of scalar sc appended to the module's item table, its first limb at *at. */
static int wide_item(rd *r, int sc, uint32_t *at)
{
    uint64_t limb[16];
    int st = wide_value(r, sc, limb);
    *at = r->m->nitem;
    for (unsigned k = 0; st == CINT_SIR_OK && k < limbs_of(sc); k++) {
        if (!grow((void **)&r->m->items, &r->cap.item, r->m->nitem, sizeof *r->m->items)) return oom(r);
        r->m->items[r->m->nitem++] = (int64_t)limb[k];
    }
    return st;
}

/* Module k of the program: one read before this one, or this one. */
static const si_module *mod_at(const rd *r, uint32_t k)
{
    return k < r->prog->nmod ? &r->prog->mods[k] : r->m;
}

/* A scalar type name, a struct line of this module, or `<module>.<Name>` of a module read
 * before it; or box 12's `Handle`, `Arena(<element>)` and `Pool(<element>)`. */
static int elem_type(rd *r, si_type *ty)
{
    si_name n = {0u, 0u};
    int st = dotted(r, &n);
    if (st != CINT_SIR_OK) return st;
    q_tail(r, &n);
    int sc = scalar_of(r->t, n, 0);
    *ty = scalar_type(sc > 0 ? sc : SC_NONE);
    if (sc > 0) return CINT_SIR_OK;
    if (name_is(r->t, n, "Handle")) {
        ty->kind = TK_HANDLE;
        ty->smod = UINT32_MAX;   /* no struct's */
        return CINT_SIR_OK;
    }
    if (name_is(r->t, n, "Arena") || name_is(r->t, n, "Pool")) {
        size_t at = r->pos + 1u;
        uint8_t mem = r->t[n.at] == 'A' ? CINT_HANDLE_ARENA : CINT_HANDLE_POOL;
        if ((st = expect(r, "(")) != CINT_SIR_OK || (st = elem_type(r, ty)) != CINT_SIR_OK) {
            return st;
        }
        if (ty->kind != TK_SCALAR && ty->kind != TK_STRUCT)
            return fail_at(r, at, "an arena or a pool holds scalars or structs");
        ty->kind = TK_MEM;
        ty->mem = mem;
        return expect(r, ")");
    }
    uint32_t cut = n.len;
    while (cut > 0 && r->t[n.at + cut - 1u] != '.') cut--;
    si_name mod = {n.at, cut > 0 ? cut - 1u : 0u}, sn = {n.at + cut, n.len - cut};
    uint32_t k0 = cut > 0 ? 0 : r->prog->nmod, k1 = cut > 0 ? r->prog->nmod : r->prog->nmod + 1u;
    for (uint32_t k = k0; k < k1; k++) {
        const si_module *m = mod_at(r, k);
        for (uint32_t i = 0; (cut == 0 || same_name(m->text, m->dotted, r->t, mod)) && i < m->nstruct; i++) {
            if (same_name(m->text, m->structs[i].name, r->t, sn)) {
                ty->kind = TK_STRUCT;
                ty->smod = k;
                ty->sidx = i;
                return CINT_SIR_OK;
            }
        }
    }
    return fail_at(r, n.at, "no struct %.*s", (int)n.len, (const char *)r->t + n.at);
}

/* elem_type, then `[<extent>]` for an array. */
static int sized_type(rd *r, si_type *ty)
{
    uint64_t e = 0;
    int st = elem_type(r, ty);
    if (st != CINT_SIR_OK || !take(r, "[")) return st;
    if ((st = number(r, (uint64_t)INT64_MAX, &e)) != CINT_SIR_OK) return st;
    ty->kind = TK_ARRAY;
    ty->extent = (int64_t)e;
    return expect(r, "]");
}

/* A struct line's field type: elem_type, then for an array `[<extent>, ...]`, rank 1 to 4 (box 15:
 * rank 2 to 4, row-major), whose extent is their product. */
static int field_type(rd *r, si_field *f)
{
    uint64_t e = 0;
    size_t at = r->pos;
    int st = elem_type(r, &f->ty);
    if (st != CINT_SIR_OK || !take(r, "[")) return st;
    f->ty.kind = TK_ARRAY;
    f->ty.extent = 1;
    do {
        if ((st = number(r, (uint64_t)INT64_MAX, &e)) != CINT_SIR_OK) return st;
        if (e != 0 && (uint64_t)f->ty.extent > (uint64_t)INT64_MAX / e)
            return fail_at(r, at, "an array field of more than 2^63 - 1 elements");
        f->dims[f->rank++] = (int64_t)e;
        f->ty.extent *= (int64_t)e;
    } while (f->rank < 4u && take(r, ", "));
    return expect(r, "]");
}

/* The cells of a value of type ty, or with whole 0 of one element of an array or view;
 * CELLS_MAX + 1 for more than CELLS_MAX. */
static uint64_t cells(const rd *r, si_type ty, int whole)
{
    if (ty.kind == TK_HANDLE || ty.kind == TK_MEM || ty.kind == TK_LEASE)
        return ty.kind == TK_HANDLE ? sizeof(cint_handle) / 8u : ty.kind == TK_MEM ? 1u : sizeof(cint_lease) / 8u;
    uint64_t c = ty.kind == TK_NONE || ty.kind == TK_UNKNOWN ? 1u
                 : ty.scalar != SC_NONE ? limbs_of(ty.scalar) : mod_at(r, ty.smod)->structs[ty.sidx].cells;
    if (whole && ty.kind == TK_ARRAY && c != 0)
        c = (uint64_t)ty.extent > CELLS_MAX / c ? (uint64_t)CELLS_MAX + 1u : c * (uint64_t)ty.extent;
    return c;
}

static int scalar_name(rd *r, int *sc)
{
    size_t s = r->pos;
    si_name n = {0u, 0u};
    int st = ident(r, &n);
    if (st == CINT_SIR_OK) q_tail(r, &n);
    *sc = st == CINT_SIR_OK ? scalar_of(r->t, n, 0) : -1;
    return st != CINT_SIR_OK ? st : *sc > 0 ? CINT_SIR_OK : fail_at(r, s, "expected a scalar type");
}

static void type_text(const rd *r, si_type ty, char *buf, size_t n)
{
    if (ty.kind == TK_SCALAR && sc_is_q(ty.scalar)) {
        snprintf(buf, n, "Q%u.%u", sc_qwidth(ty.scalar) - sc_qfrac(ty.scalar), sc_qfrac(ty.scalar));
    } else if (ty.kind == TK_SCALAR) {
        snprintf(buf, n, "%s", SCALAR_NAMES[ty.scalar]);
    } else if (ty.kind == TK_STRUCT) {
        const si_module *m = mod_at(r, ty.smod);
        si_name s = m->structs[ty.sidx].name;
        snprintf(buf, n, "%.*s", (int)s.len, (const char *)m->text + s.at);
    } else if (ty.kind == TK_ARRAY || ty.kind == TK_VIEW) {
        snprintf(buf, n, "%s", ty.kind == TK_ARRAY ? "an array" : "a view");
    } else if (ty.kind == TK_HANDLE || ty.kind == TK_MEM) {
        snprintf(buf, n, "%s", ty.kind == TK_HANDLE ? "Handle" : ty.mem == CINT_HANDLE_ARENA ? "an arena" : "a pool");
    } else {
        snprintf(buf, n, "no value");
    }
}

/* Whether ty is a view of box 09, whose index is an element offset (si_type.rank). */
static int nview(si_type ty)
{
    return ty.kind == TK_VIEW && ty.rank > 0;
}

/* The rank of an array or a view. */
static unsigned rank_of(si_type ty)
{
    return ty.rank > 0 ? ty.rank : 1u;
}

/* Whether a value of type `have` may stand where `want` is required: equal scalars, or
 * one struct; views and arrays by element, a view of the same rank. */
static int fits(si_type want, si_type have)
{
    if (want.kind == TK_UNKNOWN || have.kind == TK_UNKNOWN) return 1;
    if (want.kind == TK_NONE && want.scalar == 0xFC)
        return have.kind == TK_SCALAR || have.kind == TK_HANDLE;
    if (want.kind == TK_NONE && want.scalar == 0xFB) return have.kind == TK_SCALAR && is_arith(have.scalar);
    if (want.kind == TK_NONE && want.scalar == 0xFD) return have.kind == TK_SCALAR;
    if (want.kind == TK_NONE && want.scalar == 0xFE)
        return have.kind == TK_SCALAR && is_integer(have.scalar);
    if (want.kind == TK_VIEW) {
        if ((have.kind != TK_VIEW && have.kind != TK_ARRAY) || rank_of(want) != rank_of(have))
            return 0;
    } else if (want.kind != have.kind || want.mem != have.mem) {
        return 0;
    }
    if (want.scalar != have.scalar) return 0;
    return want.scalar != 0 || (want.smod == have.smod && want.sidx == have.sidx);
}

/* The element type of an array or view; no type for another. */
static si_type elem_of(si_type ty)
{
    if (ty.kind == TK_ARRAY || ty.kind == TK_VIEW) {
        ty.kind = ty.scalar ? TK_SCALAR : TK_STRUCT;
        ty.extent = 0;
        ty.rank = 0;
        return ty;
    }
    return scalar_type(SC_NONE);
}

/* The scalar of cell c of a value of type ty: SC_NONE where the cell is not that of an integer or
 * a Bool, alone, in a struct, or in an array, which is all a module-level struct holds (box 15,
 * OQ-260). */
static int cell_scalar(const rd *r, si_type ty, uint64_t c)
{
    for (;;) {
        if (ty.kind == TK_SCALAR) return c == 0 ? ty.scalar : SC_NONE;
        if (ty.kind == TK_ARRAY) {
            si_type e = elem_of(ty);
            uint64_t ec = e.kind == TK_STRUCT && ty.smod == UINT32_MAX ? 0u : cells(r, e, 1);
            if (ec == 0) return SC_NONE;
            c %= ec;
            ty = e;
            continue;
        }
        if (ty.kind != TK_STRUCT || ty.smod == UINT32_MAX) return SC_NONE;
        const si_module *m = mod_at(r, ty.smod);
        const si_struct *ss = &m->structs[ty.sidx];
        uint32_t j = 0;
        while (j < ss->nfield && !(m->fields[ss->field + j].off <= c &&
                                   c - m->fields[ss->field + j].off < cells(r, m->fields[ss->field + j].ty, 1)))
            j++;
        if (j == ss->nfield) return SC_NONE;
        c -= m->fields[ss->field + j].off;
        ty = m->fields[ss->field + j].ty;
    }
}

/* ------------------------------------------------------------------ operands */

static int add_opd(rd *r, si_opd o)
{
    si_module *m = r->m;
    if (!grow((void **)&m->opds, &r->cap.opd, m->nopd, sizeof *m->opds)) return oom(r);
    m->opds[m->nopd++] = o;
    return CINT_SIR_OK;
}

static si_opd opd_of(uint8_t kind, uint32_t index)
{
    si_opd o;
    memset(&o, 0, sizeof o);
    o.kind = kind;
    o.index = index;
    return o;
}

static int data_defined(const rd *r, uint32_t k)
{
    for (uint32_t i = 0; i < r->ndata; i++)
        if (r->data[i] == k) return 1;
    return 0;
}

/* Whether name n is t or s and digits, which a parameter spells `%p.<name>`, so that it
 * reads as neither a temporary nor data (compiler/OPEN.md CINTC-OQ-35, item 2). */
static int numbered(const uint8_t *t, si_name n)
{
    return n.len >= 2u && (t[n.at] == 't' || t[n.at] == 's') && all_digits(t, n.at + 1u, n.at + n.len);
}

/* A parameter's name after its `%`, without the `p.` that *pre says it has. */
static int param_name(rd *r, si_name *n, int *pre)
{
    *pre = take(r, "p.");
    return ident(r, n);
}

/* `%t<N>`, `%s<k>` or `%<parameter>`. */
static int percent(rd *r, si_opd *o, size_t *at)
{
    si_name n = {0u, 0u};
    int pre = 0;
    *at = r->pos;
    int st = expect(r, "%");
    if (st == CINT_SIR_OK) st = param_name(r, &n, &pre);
    if (st != CINT_SIR_OK) return st;
    const si_param *ps = r->f->nparam != 0 ? r->m->params + r->f->param : NULL;   /* params is NULL with none */
    for (uint32_t p = 0; pre == numbered(r->t, n) && p < r->f->nparam; p++) {
        if (same_name(r->t, ps[p].name, r->t, n)) {
            *o = opd_of(OD_PARAM, p);
            o->name = n;
            return CINT_SIR_OK;
        }
    }
    int c = r->t[n.at];
    if (!pre && numbered(r->t, n)) {
        rd sub = *r;
        uint32_t k = 0;
        sub.pos = n.at + 1u;
        sub.eol = n.at + n.len;
        if (number32(&sub, &k) == CINT_SIR_OK && sub.pos == sub.eol) {
            *o = opd_of(c == 't' ? OD_TEMP : OD_DATA, k);
            o->name = n;
            if (c == 't' && k >= r->f->ntemp)
                return fail_at(r, *at, "%%t%u is used before it is defined", k);
            if (c == 's' && !data_defined(r, k))
                return fail_at(r, *at, "%%s%u is used before its data line", k);
            return CINT_SIR_OK;
        }
    }
    return fail_at(r, *at, "'%%%s%.*s' is no temporary, data, or parameter", pre ? "p." : "", (int)n.len,
                   (const char *)r->t + n.at);
}

/* `$<name>.<k>`. */
static int slot(rd *r, si_opd *o, int declaring)
{
    size_t s = r->pos;
    si_name n = {0u, 0u};
    uint32_t k = 0;
    int st = expect(r, "$");
    if (st == CINT_SIR_OK) st = ident(r, &n);
    if (st == CINT_SIR_OK) st = expect(r, ".");
    if (st == CINT_SIR_OK) st = number32(r, &k);
    if (st != CINT_SIR_OK) return st;
    if (!declaring && (k >= r->f->nslot || r->slots[k].kind == TK_NONE))
        return fail_at(r, s, "$%.*s.%u is used before its local line", (int)n.len, (const char *)r->t + n.at, k);
    *o = opd_of(OD_SLOT, k);
    o->name = n;
    return CINT_SIR_OK;
}

/* Gives slot k type ty, the slots before it typeless until their own lines. */
static int bind_slot(rd *r, uint32_t k, si_type ty)
{
    while (r->f->nslot <= k) {
        if (!grow((void **)&r->slots, &r->scap, r->f->nslot, sizeof *r->slots)) return oom(r);
        r->slots[r->f->nslot++] = scalar_type(SC_NONE);
    }
    r->slots[k] = ty;
    return CINT_SIR_OK;
}

/* Slot o, written at at, receives a value of type ty: a slot with its local line holds
 * that type; a hidden slot (`$sc.k`, or with any set a reference) takes it here. */
static int receive(rd *r, si_opd o, size_t at, si_type ty, int any)
{
    if (o.index < r->f->nslot && r->slots[o.index].kind != TK_NONE) {
        si_type have = r->slots[o.index];
        return fits(have, ty) && fits(ty, have) ? CINT_SIR_OK : fail_at(r, at, "the slot does not hold that type");
    }
    if (!any && !name_is(r->t, o.name, "sc")) {
        return fail_at(r, at, "$%.*s.%u is used before its local line", (int)o.name.len, (const char *)r->t + o.name.at,
                       o.index);
    }
    return bind_slot(r, o.index, ty);
}

static si_type type_of(const rd *r, si_opd o)
{
    switch (o.kind) {
    case OD_TEMP:
        return r->temps[o.index];
    case OD_PARAM:
        return r->m->params[r->f->param + o.index].ty;
    case OD_SLOT:
        return r->slots[o.index];
    case OD_GLOBAL: {
        const si_global *g = &r->m->globals[o.index];
        si_type ty = scalar_type(g->scalar);
        if (g->extent >= 0) {   /* a module-level array (box 13 MLA) */
            ty.kind = TK_ARRAY;
            ty.extent = g->extent;
        } else if (g->record) {   /* a module-level struct (box 15, OQ-260) */
            ty.kind = TK_STRUCT;
            ty.smod = g->smod;
            ty.sidx = g->sidx;
        }
        return ty;
    }
    case OD_DATA: {
        si_type ty = special_type(TK_VIEW, SC_U8);
        return ty;
    }
    case OD_MEM:
        return r->m->mems[o.index].ty;
    default:
        return UNKNOWN;
    }
}

static int check_type(rd *r, size_t at, si_type want, si_type have)
{
    char a[64], b[64];
    if (fits(want, have)) return CINT_SIR_OK;
    if (want.kind == TK_NONE && want.scalar == 0xFE && have.kind == TK_SCALAR && is_wide(have.scalar))
        return refuse_at(r, at, "a wide index, rotation count or bind.where operand is not run by this unit");
    type_text(r, have, b, sizeof b);
    if (want.kind == TK_NONE) {
        return fail_at(r, at, "operand of type %s where %s is required", b,
                       want.scalar == 0xFE || want.scalar == 0xFB ? "an integer" : "a scalar");
    }
    type_text(r, want, a, sizeof a);
    return fail_at(r, at, "operand of type %s where %s is required", b, a);
}

/* A value: a temporary or a parameter, of type want. */
static int val(rd *r, si_type want)
{
    si_opd o;
    size_t at = r->pos;
    if (peek(r) != '%') return fail_at(r, at, "expected a temporary or a parameter");
    int st = percent(r, &o, &at);
    if (st == CINT_SIR_OK && o.kind == OD_DATA) st = fail_at(r, at, "data is not a value");
    if (st == CINT_SIR_OK) st = check_type(r, at, want, type_of(r, o));
    return st != CINT_SIR_OK ? st : add_opd(r, o);
}

/* `@<name>`, a module-level array (box 13 MLA) or struct (box 15) of this module, as a place. */
static int global_place(rd *r, si_type *ty)
{
    si_opd o = opd_of(OD_GLOBAL, 0);
    size_t at = r->pos;
    int st = expect(r, "@");
    if (st == CINT_SIR_OK) st = ident(r, &o.name);
    for (uint32_t g = 0; st == CINT_SIR_OK && g < r->m->nglobal; g++) {
        if ((r->m->globals[g].extent >= 0 || r->m->globals[g].record) &&
            same_name(r->t, r->m->globals[g].name, r->t, o.name)) {
            o.index = g;
            *ty = type_of(r, o);
            r->f->arrays = 1;
            return add_opd(r, o);
        }
    }
    return st != CINT_SIR_OK ? st : fail_at(r, at, "no module-level array or struct @%.*s", (int)o.name.len,
                                                    (const char *)r->t + o.name.at);
}

/* A slot or a parameter that holds an array, a view or a struct, or a module-level array or struct. */
static int place(rd *r, si_type *ty)
{
    si_opd o;
    size_t at = r->pos;
    if (peek(r) == '@') return global_place(r, ty);
    int st = peek(r) == '$' ? slot(r, &o, 0) : percent(r, &o, &at);
    if (st == CINT_SIR_OK && o.kind != OD_SLOT && o.kind != OD_PARAM)
        st = fail_at(r, at, "expected a slot or a parameter");
    if (st != CINT_SIR_OK) return st;
    *ty = type_of(r, o);
    if (ty->kind == TK_SCALAR) return fail_at(r, at, "a scalar is not a place");
    return add_opd(r, o);
}

/* An arena (mem CINT_HANDLE_ARENA) or a pool (CINT_HANDLE_POOL) of box 12: `@<name>` of its
 * line, or a parameter or a slot that holds one. */
static int container(rd *r, unsigned mem, si_type *ty)
{
    si_opd o = opd_of(OD_MEM, 0);
    size_t at = r->pos;
    int st = CINT_SIR_OK;
    if (take(r, "@")) {
        st = ident(r, &o.name);
        while (st == CINT_SIR_OK && o.index < r->m->nmem && !same_name(r->t, r->m->mems[o.index].name, r->t, o.name))
            o.index++;
        if (st == CINT_SIR_OK && o.index == r->m->nmem)
            return fail_at(r, at, "no arena or pool @%.*s", (int)o.name.len, (const char *)r->t + o.name.at);
    } else {
        st = peek(r) == '$' ? slot(r, &o, 0) : percent(r, &o, &at);
    }
    if (st != CINT_SIR_OK) return st;
    *ty = type_of(r, o);
    if (ty->kind != TK_MEM || ty->mem != mem)
        return fail_at(r, at, mem == CINT_HANDLE_ARENA ? "expected an arena" : "expected a pool");
    return add_opd(r, o);
}

/* A field of struct type sty, of type *fty: the operand gives its first cell and extent. */
static int field(rd *r, si_type sty, si_type *fty)
{
    si_opd o = opd_of(OD_FIELD, 0);
    size_t at = r->pos;
    int st = ident(r, &o.name);
    if (st != CINT_SIR_OK) return st;
    if (sty.kind != TK_STRUCT) return fail_at(r, at, "a field of a value that is not a struct");
    const si_module *m = mod_at(r, sty.smod);
    const si_struct *ss = &m->structs[sty.sidx];
    for (uint32_t i = 0; i < ss->nfield; i++) {
        const si_field *f = &m->fields[ss->field + i];
        if (same_name(m->text, f->name, r->t, o.name)) {
            *fty = f->ty;
            o.index = f->off;
            o.value = f->ty.kind == TK_ARRAY ? f->ty.extent : 1;
            return add_opd(r, o);
        }
    }
    return fail_at(r, at, "no field %.*s", (int)o.name.len, (const char *)r->t + o.name.at);
}

/* field, of a scalar or a handle (array 0) or of an array of scalars (array 1). */
static int scalar_field(rd *r, si_type sty, int array, si_type *fty)
{
    size_t at = r->pos;
    int st = field(r, sty, fty);
    if (st == CINT_SIR_OK && !array && fty->kind == TK_HANDLE) return st;
    if (st == CINT_SIR_OK && (fty->kind != (array ? TK_ARRAY : TK_SCALAR) || fty->scalar == SC_NONE))
        return fail_at(r, at, array ? "the field is not an array of scalars" : "the field is not a scalar");
    *fty = array ? elem_of(*fty) : *fty;
    return st;
}

static int imm(rd *r)
{
    uint64_t v = 0;
    int st = number(r, (uint64_t)INT64_MAX, &v);
    si_opd o = opd_of(OD_IMM, 0);
    o.value = (int64_t)v;
    return st != CINT_SIR_OK ? st : add_opd(r, o);
}

static int sep(rd *r)
{
    return expect(r, ", ");
}

/* ------------------------------------------------------------------ operation names */

typedef struct parts {
    uint32_t n;
    si_name p[6];
} parts;

static int split(const uint8_t *t, si_name w, parts *out)
{
    uint32_t s = w.at;
    out->n = 0;
    for (uint32_t i = w.at; i <= w.at + w.len; i++) {
        if (i == w.at + w.len || t[i] == '.') {
            if (out->n == 6u || i == s) return 0;
            out->p[out->n].at = s;
            out->p[out->n].len = i - s;
            out->n++;
            s = i + 1u;
        }
    }
    return 1;
}

/* Decodes an arithmetic, comparison, conversion or built-in operation word into n. On fixed point
 * (box 15; SPEC-01 IM-60 to IM-69): add and sub checked, wrap or sat; mul in those forms and div
 * checked, with a mode; neg, cmp, abs, min and max; sqrt with a mode; `qmul.checked.<a>.<b>.<r>`,
 * with a mode when fa + fb > fr; `mul_int.checked.<q>.<integer>`; and a conversion with fixed point
 * on one side, its mode when it rounds, or `as.raw` to or from the storage integer. */
static int arith_name(const uint8_t *t, si_name w, si_node *n)
{
    parts p;
    if (!split(t, w, &p) || p.n < 2u) return 0;
    int b = lookup(t, p.p[0], BIN_NAMES, 10);
    if (b >= BO_AND && p.n == 2u) {
        n->op = OP_BIN;
        n->sub = (uint8_t)b;
        n->t1 = (uint8_t)(scalar_of(t, p.p[1], 1) & 0xFF);
        return scalar_of(t, p.p[1], 1) > 0 && !sc_is_q(n->t1);
    }
    if (b >= 0 && b < BO_AND && (p.n == 3u || p.n == 4u)) {   /* the result forms of add to shl (box 12) too */
        int f = lookup(t, p.p[1], FORM_NAMES, 4);
        int sc = scalar_of(t, p.p[2], 1), md = p.n == 4u ? lookup(t, p.p[3], MODE_NAMES, 10) : 0;
        n->op = OP_BIN;
        n->sub = (uint8_t)b;
        n->form = (uint8_t)(f & 0xFF);
        n->t1 = (uint8_t)(sc & 0xFF);
        n->mode = (uint8_t)(md & 0xFF);
        if (sc_is_q(sc))
            return f >= 0 && f != FM_RESULT && b <= BO_DIV && (b == BO_MUL || b == BO_DIV ? md > 0 : p.n == 3u) &&
                   (b != BO_DIV || f == FM_CHECKED);
        return p.n == 3u && f >= 0 && is_arith(sc) && !((b == BO_DIV || b == BO_REM) && f == FM_SAT) &&
               !(b == BO_SHR && f == FM_RESULT);
    }
    if (name_is(t, p.p[0], "neg") && p.n == 3u && name_is(t, p.p[1], "checked")) {
        int sc = scalar_of(t, p.p[2], 1);
        n->op = OP_UN;
        n->sub = BO_NEG;
        n->t1 = (uint8_t)(sc & 0xFF);
        return is_arith(sc) || sc_is_q(sc);
    }
    if (name_is(t, p.p[0], "not") && p.n == 2u) {
        int sc = scalar_of(t, p.p[1], 1);
        n->op = OP_UN;
        n->sub = BO_NOT;
        n->t1 = (uint8_t)(sc & 0xFF);
        return sc > 0 && !sc_is_q(sc);
    }
    if (name_is(t, p.p[0], "cmp") && p.n == 3u) {
        int c = lookup(t, p.p[1], CMP_NAMES, 6);
        int sc = scalar_of(t, p.p[2], 1);
        n->op = OP_CMP;
        n->sub = (uint8_t)(c & 0xFF);
        n->t1 = (uint8_t)(sc & 0xFF);
        return c >= 0 && sc > 0 && (c <= CM_NE || sc != SC_BOOL);
    }
    if (name_is(t, p.p[0], "as") && (p.n == 4u || p.n == 5u)) {   /* as.result: `as?` (box 12) */
        int f = lookup(t, p.p[1], FORM_NAMES, 5);
        int from = scalar_of(t, p.p[2], 1), to = scalar_of(t, p.p[3], 1);
        int md = p.n == 5u ? lookup(t, p.p[4], MODE_NAMES, 10) : 0, q = sc_is_q(from) + sc_is_q(to);
        n->op = OP_AS;
        n->form = (uint8_t)(f & 0xFF);
        n->t1 = (uint8_t)(from & 0xFF);
        n->t2 = (uint8_t)(to & 0xFF);
        n->mode = (uint8_t)(md & 0xFF);
        if (f == FM_RAW)   /* the storage integer of the same width */
            return p.n == 4u && q == 1 && is_signed(from) && is_signed(to) && !is_wide(from) && !is_wide(to) &&
                   width_of(from) == width_of(to);
        if (q > 0)
            return f >= 0 && f != FM_SAT && md >= 0 && (md > 0) == (sc_qfrac(from) > sc_qfrac(to)) &&
                   (sc_is_q(from) || is_integer(from)) && (sc_is_q(to) || is_integer(to));
        return p.n == 4u && f >= 0 && f != FM_SAT && from > 0 && is_arith(to);
    }
    int id = lookup(t, p.p[0], BUILTIN_NAMES, 20);
    if (id < 1 || p.n < 3u || !name_is(t, p.p[1], "checked")) return 0;
    int sc = scalar_of(t, p.p[2], 1);
    uint32_t k = 3;
    n->op = OP_BUILTIN;
    n->sub = (uint8_t)id;
    n->t1 = (uint8_t)(sc & 0xFF);
    n->t2 = n->t1;
    if (id == BI_QMUL || id == BI_MUL_INT) {   /* the second operand's type */
        int y = k < p.n ? scalar_of(t, p.p[k++], 1) : -1;
        if (id == BI_QMUL ? !sc_is_q(y) : !is_integer(y)) return 0;
        n->t3 = (uint8_t)y;
    }
    if (id == BI_UABS || id == BI_MUL_FULL || id == BI_MULDIV || id == BI_QMUL) {
        int r = k < p.n ? scalar_of(t, p.p[k++], 1) : -1;
        if (id == BI_QMUL ? !sc_is_q(r) : !is_arith(r)) return 0;
        n->t2 = (uint8_t)r;
    }
    if (id == BI_DIV_ROUND || id == BI_ISQRT_ROUND || id == BI_MULDIV || id == BI_SQRT ||
        (id == BI_QMUL && sc_qfrac(sc) + sc_qfrac(n->t3) > sc_qfrac(n->t2))) {
        int md = k < p.n ? lookup(t, p.p[k++], MODE_NAMES, 10) : -1;
        if (md < 1) return 0;
        n->mode = (uint8_t)md;
    }
    if (sc_is_q(sc)) return k == p.n && (id == 1 || id == BI_MIN || id == BI_MAX || id >= BI_SQRT);
    return k == p.n && is_arith(sc) && id < BI_SQRT;
}

/* Whether a node of this operation can fault, so carries a site (SIR-04, SIR-10; the
 * nodes compiler/lowering.ci gives a site). */
/* The index.checked an index access relies on: the latest one of its place before it in the
 * function. An access the text did not check first faults at that check's site under its name
 * (sir_exec.c, unchecked), and an access with none before it is refused, since B1 checks an index
 * of each place before it reaches the element; but in a kernel's element form, whose view.offset
 * indices lie in the shapes its entry bound, which compiled C does not check either. */
static int pair_check(rd *r, si_node *n, uint32_t idx, si_name word)
{
    int access = n->op == OP_ILOAD || n->op == OP_ISTORE || n->op == OP_ELOAD || n->op == OP_ESTORE ||
                 n->op == OP_FILOAD || n->op == OP_FISTORE || n->op == OP_EADDR;
    if (n->op != OP_ICHECK && !access) return CINT_SIR_OK;
    const si_opd *p = &r->m->opds[n->opd + (n->op == OP_EADDR)];   /* element.address binds o[0] */
    uint32_t k = 0;
    while (k < r->ncheck) {
        const si_opd *q = &r->m->opds[r->m->nodes[r->checks[k]].opd];
        if (q->kind == p->kind && q->index == p->index) break;
        k++;
    }
    if (n->op == OP_ICHECK) {
        if (k == r->ncheck) {
            if (!grow((void **)&r->checks, &r->kcap, r->ncheck, sizeof *r->checks)) return oom(r);
            r->ncheck++;
        }
        r->checks[k] = idx;
        return CINT_SIR_OK;
    }
    if (k == r->ncheck && r->f->kind == FN_KERNEL && n->view) {
        n->guard = UINT32_MAX;   /* an element of a kernel's element form (view.offset), in its bound shape */
        return CINT_SIR_OK;
    }
    if (k == r->ncheck) {
        return fail_at(r, word.at, "%.*s of a place that no index.checked checks before it", (int)word.len,
                       (const char *)r->t + word.at);
    }
    n->guard = r->checks[k];
    return CINT_SIR_OK;
}

static int can_fault(const si_node *n)
{
    switch (n->op) {
    case OP_BIN:
        return n->sub < BO_AND && n->form != FM_RESULT &&
               (n->form == FM_CHECKED || n->sub == BO_SHL || n->sub == BO_SHR || n->sub == BO_DIV || n->sub == BO_REM);
    case OP_UN:
        return n->sub == BO_NEG;
    case OP_AS:
        return n->form == FM_CHECKED && n->t1 != SC_BOOL;
    case OP_BUILTIN:
        return n->sub != BI_UABS && n->sub != BI_MIN && n->sub != BI_MAX && n->sub != BI_MUL_FULL;
    case OP_ICHECK: case OP_BSHAPE: case OP_CSHAPE: case OP_CALL: case OP_ASSERT: case OP_FUEL:
    case OP_PRINT_BEGIN: case OP_VSLICE: case OP_VCOPY: case OP_REDUCE: case OP_CALIAS: case OP_VALLOC: case OP_VFILL:
    case OP_FCHARGE: case OP_KCALL: case OP_MEMBER: case OP_HVIEW: case OP_HLEASE: case OP_PDEREF:
        return 1;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ calls */

/* The function (kind FN_FUNC) or kernel (FN_KERNEL) `@<dotted module>.<name>` among the modules
 * read so far and this one. */
static const si_func *callee(const rd *r, si_name full, int kind, const si_module **mod)
{
    uint32_t cut = full.len;
    while (cut > 0 && r->t[full.at + cut - 1u] != '.') cut--;
    if (cut < 2u) return NULL;
    si_name modname = {full.at, cut - 1u};
    si_name fname = {full.at + cut, full.len - cut};
    for (uint32_t k = 0; k <= r->prog->nmod; k++) {
        const si_module *m = k < r->prog->nmod ? &r->prog->mods[k] : r->m;
        if (m->text == NULL || !same_name(m->text, m->dotted, r->t, modname)) {
            continue;
        }
        for (uint32_t i = 0; i < m->nfunc; i++) {
            if (m->funcs[i].kind == kind && same_name(m->text, m->funcs[i].name, r->t, fname)) {
                *mod = m;
                return &m->funcs[i];
            }
        }
    }
    return NULL;
}

/* `call @<module>.<name>(<arguments>)[ into <slot>]`, checked against the callee. */
static int call(rd *r, si_node *n, int has_dst)
{
    size_t at = r->pos;
    si_opd target = opd_of(OD_IMPORT, 0);
    const si_module *cm = NULL;
    int st = expect(r, " @");
    if (st == CINT_SIR_OK) st = dotted(r, &target.name);
    if (st != CINT_SIR_OK) return st;
    const si_func *cf = callee(r, target.name, FN_FUNC, &cm);
    if (cf == NULL) {
        return fail_at(r, at + 1u, "no function %.*s in this module or a module read before it",
                       (int)target.name.len, (const char *)r->t + target.name.at);
    }
    if ((st = add_opd(r, target)) != CINT_SIR_OK || (st = expect(r, "(")) != CINT_SIR_OK) {
        return st;
    }
    n->cmod = (uint32_t)(cm - r->prog->mods);   /* r->m is mods[r->prog->nmod] */
    n->cfn = (uint32_t)(cf - cm->funcs);
    for (uint32_t a = 0; a < cf->nparam && st == CINT_SIR_OK; a++) {
        si_type want = cm->params[cf->param + a].ty;
        size_t s = r->pos;
        if (a > 0 && (st = sep(r)) != CINT_SIR_OK) return st;
        s = r->pos;
        if (take(r, "extent ")) {
            si_opd o, e = opd_of(OD_EXTENT, 0);
            si_type have;
            uint64_t d = 0;
            size_t ex = r->m->nopd;
            if (is_digit(peek(r)) && ((st = number(r, 3u, &d)) != CINT_SIR_OK || (st = expect(r, " ")) != CINT_SIR_OK)) {
                return st;   /* the extent of dimension d of a view of rank 2 to 4 */
            }
            e.value = (int64_t)d;
            if ((st = add_opd(r, e)) != CINT_SIR_OK) return st;
            if (!(peek(r) == '%' && peek_at(r, 1) == 's' && is_digit(peek_at(r, 2)))) {
                if ((st = place(r, &have)) == CINT_SIR_OK) {
                    r->m->opds[ex].index = (uint32_t)nview(have);   /* compiled C reads a view's shape[d], else its extent */
                    st = d < rank_of(have) ? st : fail_at(r, s, "the extent of a dimension the place does not have");
                }
            } else if ((st = percent(r, &o, &s)) == CINT_SIR_OK) {
                st = o.kind == OD_DATA ? add_opd(r, o) : fail_at(r, s, "expected an array, a view or data");
            }
            if (st == CINT_SIR_OK && !(want.kind == TK_SCALAR && want.scalar == SC_I64))
                st = fail_at(r, s, "an extent is passed for an I64 parameter only");
        } else if (want.kind == TK_SCALAR || want.kind == TK_HANDLE) {
            st = val(r, want);
        } else if (want.kind == TK_MEM) {   /* an arena or a pool (box 12) */
            si_type have;
            st = container(r, want.mem, &have);
            if (st == CINT_SIR_OK && !fits(want, have))
                st = fail_at(r, s, "argument %u is not an arena or a pool of the parameter's type", a + 1u);
        } else if (peek(r) == '%' && peek_at(r, 1) == 's' && is_digit(peek_at(r, 2))) {
            si_opd o;
            st = percent(r, &o, &s);
            if (st == CINT_SIR_OK && !fits(want, type_of(r, o)))
                st = fail_at(r, s, "data passed for a parameter that is not a U8 view");
            if (st == CINT_SIR_OK) st = add_opd(r, o);
        } else {
            si_type have;
            st = place(r, &have);
            if (st == CINT_SIR_OK && !fits(want, have)) {
                char x[64];
                type_text(r, want, x, sizeof x);
                st = fail_at(r, s, "argument %u does not fit the parameter's type %s", a + 1u, x);
            }
        }
    }
    if (st == CINT_SIR_OK) {
        st = expect(r, ")");
    }
    if (st != CINT_SIR_OK) return st;
    n->nopd = r->m->nopd - n->opd;
    if (cf->result.kind == TK_SCALAR || cf->result.kind == TK_HANDLE) {
        if (!has_dst)
            return fail_at(r, at, "a call of a function with a scalar result defines a temporary");
        r->temps[n->dst] = cf->result;
        r->f->handles = (uint8_t)(r->f->handles || cf->result.kind == TK_HANDLE);
        n->wide = (uint8_t)(cf->result.kind == TK_SCALAR && is_wide(cf->result.scalar) ? limbs_of(cf->result.scalar) : 0u);
    } else if (has_dst) {
        return fail_at(r, at, "a call of a function with no scalar result defines no temporary");
    }
    if (cf->result.kind == TK_STRUCT || cf->result.kind == TK_ARRAY) {
        /* the receiving slot: a declared one of the result's type, or a hidden struct
         * slot `$sc.k` that the call itself introduces (no local line) */
        si_opd o;
        size_t s = r->pos;
        if ((st = expect(r, " into ")) != CINT_SIR_OK || (st = slot(r, &o, 1)) != CINT_SIR_OK)
            return st;
        return (st = receive(r, o, s + 6u, cf->result, 0)) != CINT_SIR_OK ? st : add_opd(r, o);
    }
    return CINT_SIR_OK;
}

/* ------------------------------------------------------------------ nesting */

static int push(rd *r, uint8_t op, uint32_t node, int64_t label)
{
    if (r->depth >= STACK_MAX) return fail_line(r, "nesting deeper than %u", STACK_MAX);
    if (!grow((void **)&r->stack, &r->fcap, r->depth, sizeof *r->stack)) return oom(r);
    frame *fr = &r->stack[r->depth++];
    memset(fr, 0, sizeof *fr);
    fr->op = op;
    fr->node = node;
    fr->label = label;
    return CINT_SIR_OK;
}

static frame *top(rd *r)
{
    return &r->stack[r->depth - 1u];
}

/* The innermost loop (continue) or loop, switch or block (break) with label k. */
static int find_label(rd *r, size_t at, int64_t k, int is_continue)
{
    for (uint32_t i = r->depth; i-- > 0;) {
        const frame *fr = &r->stack[i];
        if ((fr->op == OP_LOOP || fr->op == OP_SWITCH || fr->op == OP_BLOCK) && fr->label == k) {
            if (is_continue && fr->op != OP_LOOP) {
                return fail_at(r, at, "continue names a loop, and L%lld is a %s", (long long)k,
                               fr->op == OP_SWITCH ? "switch" : "block");
            }
            return CINT_SIR_OK;
        }
    }
    return fail_at(r, at, "L%lld is no enclosing %s", (long long)k, is_continue ? "loop" : "loop, switch or block");
}

static int label(rd *r, int64_t *k)
{
    uint64_t v = 0;
    int st = expect(r, "L");
    if (st == CINT_SIR_OK) st = number(r, COUNT_MAX, &v);
    *k = (int64_t)v;
    return st;
}

/* A `case` item list: values and inclusive ranges of the switch's scalar. */
static int items(rd *r, int sc)
{
    int st = CINT_SIR_OK;
    do {
        int64_t a = 0, b = 0;
        size_t s = r->pos;
        /* a scrutinee of unknown type (a struct field): I64 values, or U64 ones without a sign */
        int vs = sc != SC_NONE ? sc : peek(r) == '-' ? SC_I64 : SC_U64;
        if ((st = value(r, vs, &a)) != CINT_SIR_OK) return st;
        b = a;
        if (take(r, "..=")) {
            int ws = sc != SC_NONE ? sc : peek(r) == '-' ? SC_I64 : SC_U64;
            if ((st = value(r, ws, &b)) != CINT_SIR_OK) return st;
            int ordered = is_signed(vs) || is_signed(ws) ? a < b : (uint64_t)a < (uint64_t)b;
            if (!ordered) return fail_at(r, s, "a range's first value is not below its last");
        }
        si_opd lo = opd_of(OD_IMM, 0), hi = opd_of(OD_IMM, 0);
        lo.value = a;
        hi.value = b;
        if ((st = add_opd(r, lo)) != CINT_SIR_OK || (st = add_opd(r, hi)) != CINT_SIR_OK) return st;
    } while (take(r, ", "));
    return st;
}

/* `print.bytes "<text>"`: printable ASCII with \", \\, \n, \t and \x<hh>. */
static int print_text(rd *r, int64_t *count)
{
    int st = expect(r, "\"");
    int64_t n = 0;
    while (st == CINT_SIR_OK && peek(r) != '"') {
        int c = peek(r);
        size_t s = r->pos;
        if (c < 0) return fail_at(r, s, "unterminated text");
        r->pos++;
        if (c == '\\') {
            int e = peek(r);
            r->pos++;
            if (e == 'x') {
                int ok = 1;
                for (int k = 0; k < 2; k++) {
                    int h = peek(r);
                    ok = ok && ((h >= '0' && h <= '9') || (h >= 'a' && h <= 'f'));
                    r->pos++;
                }
                if (!ok) return fail_at(r, s, "expected two lowercase hexadecimal digits");
            } else if (e != '"' && e != '\\' && e != 'n' && e != 't') {
                return fail_at(r, s, "unknown escape");
            }
        } else if (c == '"') {
            return fail_at(r, s, "a quote inside text is escaped");
        }
        n++;
    }
    *count = n;
    return st == CINT_SIR_OK ? expect(r, "\"") : st;
}

/* ------------------------------------------------------------------ one node */

static int define(rd *r, si_node *n, si_type ty)
{
    if (n->dst < 0) {
        size_t at = r->bol;
        while (r->t[at] == ' ') at++;
        return fail_at(r, at, "this node defines a temporary, written %%t%u = first", r->f->ntemp);
    }
    r->temps[n->dst] = ty;
    r->f->handles = (uint8_t)(r->f->handles || ty.kind == TK_HANDLE);   /* four cells of its own (sir_exec.c) */
    n->wide = (uint8_t)(ty.kind == TK_SCALAR && is_wide(ty.scalar) ? limbs_of(ty.scalar) : 0u);   /* its limbs, too */
    return CINT_SIR_OK;
}

/* An immediate in 0 .. max - 1, or with neg -1 too: a dimension, a rank, a mask. */
static int small(rd *r, int64_t max, int neg)
{
    size_t at = r->pos;
    si_opd o = opd_of(OD_IMM, 0);
    int st = value(r, SC_I64, &o.value);
    if (st == CINT_SIR_OK && (o.value >= max || o.value < (neg ? -1 : 0)))
        return fail_at(r, at, "%lld is out of range here", (long long)o.value);
    return st != CINT_SIR_OK ? st : add_opd(r, o);
}

/* A place that is a view of box 09 (box09 1), or an array or a view. */
static int view_place(rd *r, si_type *ty, int box09)
{
    size_t at = r->pos;
    int st = place(r, ty);
    if (st == CINT_SIR_OK && (box09 ? !nview(*ty) : ty->kind != TK_ARRAY && ty->kind != TK_VIEW))
        return fail_at(r, at, box09 ? "expected a view with a rank" : "expected an array or a view");
    return st;
}

/* The view a node writes, `$<name>.<k>: <T>[_, ...]`: the slot takes that type. */
static int view_dest(rd *r, si_type *ty)
{
    si_opd o;
    size_t at = r->pos;
    int st = slot(r, &o, 1);
    if (st != CINT_SIR_OK || (st = view_type(r, ty)) != CINT_SIR_OK || (st = receive(r, o, at, *ty, 1)) != CINT_SIR_OK)
        return st;
    return add_opd(r, o);
}

/* A reduction's word (compiler/back_c.ci, reduce_name): sub the CINT_REDUCE_* code, t1 the
 * element, t2 the result. */
static int reduce_word(const uint8_t *t, si_name w, si_node *n)
{
    static const char *const pre[] = {"", "sum.checked.", "sum_wrap.", "sum_sat.", "fold_checked.add.",
                                      "fold_checked.mul.", "reduce_min.checked.", "reduce_max.checked.", "count.",
                                      "dot.checked."};
    for (int k = 1; k < 10; k++) {
        size_t l = strlen(pre[k]);
        parts p;
        if (w.len <= l || memcmp(t + w.at, pre[k], l) != 0) {
            continue;
        }
        si_name rest = {w.at + (uint32_t)l, w.len - (uint32_t)l};
        if (!split(t, rest, &p) || p.n < (k == 1 || k == 9 ? 2u : 1u) || p.n > (k == 9 ? 3u : k == 1 ? 2u : 1u))
            return 0;
        int e = scalar_of(t, p.p[0], 1), res = p.n >= 2u ? scalar_of(t, p.p[1], 1) : e;
        int md = p.n == 3u ? lookup(t, p.p[2], MODE_NAMES, 10) : 0;
        n->op = OP_REDUCE;
        n->sub = (uint8_t)k;
        n->t1 = (uint8_t)(e & 0xFF);
        n->t2 = (uint8_t)(k == 8 ? SC_I64 : res & 0xFF);
        n->mode = (uint8_t)(md & 0xFF);
        if (sc_is_q(e))   /* fixed point (box 15; RT-OQ-57): the element's format, dot's with a mode when it rounds */
            return md >= 0 && (md > 0) == (k == 9 && 2u * sc_qfrac(e) > sc_qfrac(res)) && sc_is_q(res) &&
                   (k == 9 || res == e) && k != 5 && k != 8;
        return p.n < 3u && (k == 8 ? e == SC_BOOL : is_arith(e) && is_arith(res));
    }
    return 0;
}

#define NOT_VIEW 100   /* view_body: the word names no view form */

/* The view forms of box 09 (compiler/OPEN.md CINTC-OQ-35, item 5) and the frame arena's
 * marks and charges (CINTC-OQ-64), after the word; NOT_VIEW for another word. */
static int view_body(rd *r, si_node *n, si_name word)
{
    const uint8_t *t = r->t;
    si_type a = scalar_type(SC_NONE), b = a;
    size_t s = r->pos + 1u;
    int st = CINT_SIR_OK;
    si_type i64 = scalar_type(SC_I64);
    if (name_is(t, word, "view.begin") || name_is(t, word, "view.transpose") || name_is(t, word, "view.reverse")) {
        int begin = t[word.at + 5] == 'b', rev = t[word.at + 5] == 'r';
        n->op = begin ? OP_VBEGIN : rev ? OP_VREV : OP_VTRANS;
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = view_dest(r, &a)) != CINT_SIR_OK ||
            (st = sep(r)) != CINT_SIR_OK || (st = view_place(r, &b, !begin && !rev)) != CINT_SIR_OK ||
            (begin && ((st = sep(r)) != CINT_SIR_OK || (st = imm(r)) != CINT_SIR_OK || (st = sep(r)) != CINT_SIR_OK ||
                       (st = imm(r)) != CINT_SIR_OK)) ||
            (rev && ((st = sep(r)) != CINT_SIR_OK || (st = small(r, rank_of(b), 0)) != CINT_SIR_OK))) {
            return st;
        }
        n->rank = a.rank;
        n->view = (uint8_t)(begin && b.kind == TK_ARRAY && a.rank > 1u);   /* row-major over a flat array */
        if (!fits(elem_of(a), elem_of(b)) || !fits(elem_of(b), elem_of(a)))
            return fail_at(r, s, "the view and its place differ in element type");
        if (n->view) {   /* the extents, 12 bits each from dimension 0, are those of the array */
            int64_t packed = r->m->opds[r->m->nopd - 1u].value, e = 1;
            for (unsigned q = 0; q < a.rank; q++) e *= packed >> (12u * q) & 4095;
            return e == b.extent ? CINT_SIR_OK : fail_at(r, s, "the extents of the view are not its array's");
        }
        if (begin ? a.rank > rank_of(b) : rev ? a.rank != rank_of(b) : a.rank != 2u || b.rank != 2u)
            return fail_at(r, s, "the view does not have the rank of its place");
        return CINT_SIR_OK;
    }
    if (name_is(t, word, "view.pick") || name_is(t, word, "view.offset") || name_is(t, word, "view.dim")) {
        int dim = t[word.at + 5] == 'd';
        n->op = dim ? OP_VDIM : t[word.at + 5] == 'p' ? OP_VPICK : OP_EOFF;
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = dim ? view_dest(r, &a) : view_place(r, &a, 1)) != CINT_SIR_OK ||
            (st = sep(r)) != CINT_SIR_OK || (st = val(r, i64)) != CINT_SIR_OK || (st = sep(r)) != CINT_SIR_OK ||
            (n->op == OP_EOFF && peek(r) == '%' && ((st = val(r, i64)) != CINT_SIR_OK || (st = sep(r)) != CINT_SIR_OK))) {
            return st;
        }
        /* a pick or an offset may name a dimension its view has while its subscript runs */
        if ((st = small(r, dim ? a.rank : 4, 0)) != CINT_SIR_OK) return st;
        return n->op == OP_EOFF ? define(r, n, i64) : CINT_SIR_OK;
    }
    if (word.len > 14u && memcmp(t + word.at, "slice.checked.", 14) == 0) {
        si_name sfx = {word.at + 14u, word.len - 14u};
        int sc = scalar_of(t, sfx, 1);
        n->op = OP_VSLICE;
        n->t1 = (uint8_t)(sc > 0 ? sc : SC_NONE);
        if (sc <= 0) return fail_at(r, word.at, "unknown operation");
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = view_place(r, &a, 1)) != CINT_SIR_OK)
            return st;
        if (a.scalar != sc) return fail_at(r, word.at, "the slice names another element type");
        unsigned given = 0;
        for (unsigned q = 0; q < 2u && st == CINT_SIR_OK; q++) {   /* the bounds; `_` for one not written */
            if ((st = sep(r)) == CINT_SIR_OK) {
                given |= take(r, "_") ? 0u : 1u << q;
                st = given & 1u << q ? val(r, i64) : add_opd(r, opd_of(OD_IMM, 0));
            }
        }
        size_t f = r->pos + 2u;
        if (st != CINT_SIR_OK || (st = sep(r)) != CINT_SIR_OK || (st = small(r, 512, 0)) != CINT_SIR_OK)
            return st;
        int64_t fl = r->m->opds[r->m->nopd - 1u].value;
        if (fl % 8 >= 4 || (unsigned)(fl / 64) % 4u != given)
            return fail_at(r, f, "the slice's dimension and bounds disagree with what it writes");
        si_opd step = opd_of(OD_IMM, 0);
        size_t at = r->pos + 2u;
        if ((st = sep(r)) != CINT_SIR_OK || (st = value(r, SC_I64, &step.value)) != CINT_SIR_OK)
            return st;
        return step.value == 0 ? fail_at(r, at, "a slice's step is never 0") : add_opd(r, step);
    }
    if (name_is(t, word, "view.end")) {
        n->op = OP_VEND;
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = view_place(r, &a, 1)) != CINT_SIR_OK ||
            (st = sep(r)) != CINT_SIR_OK || (st = view_place(r, &b, 0)) != CINT_SIR_OK || (st = sep(r)) != CINT_SIR_OK ||
            (st = imm(r)) != CINT_SIR_OK || (st = sep(r)) != CINT_SIR_OK || (st = small(r, 16, 0)) != CINT_SIR_OK) {
            return st;
        }
        int64_t mask = r->m->opds[r->m->nopd - 1u].value;
        unsigned q = 0;
        n->rank = (uint8_t)rank_of(b);
        for (unsigned g = 0; g < n->rank; g++) q += (mask >> g & 1) == 0;
        /* the picked dimensions leave the view, whose rank becomes its slot's */
        return mask >> n->rank == 0 && q == a.rank ? CINT_SIR_OK : fail_at(r, s, "the view keeps %u dimensions", q);
    }
    if (name_is(t, word, "view.copy") || name_is(t, word, "bind.alias")) {
        int alias = t[word.at] == 'b';
        n->op = alias ? OP_CALIAS : OP_VCOPY;
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = view_place(r, &a, 0)) != CINT_SIR_OK ||
            (st = sep(r)) != CINT_SIR_OK || (st = view_place(r, &b, 0)) != CINT_SIR_OK ||
            (alias && ((st = sep(r)) != CINT_SIR_OK || (st = imm(r)) != CINT_SIR_OK || (st = sep(r)) != CINT_SIR_OK ||
                       (st = imm(r)) != CINT_SIR_OK))) {
            return st;
        }
        n->value = (int64_t)cells(r, a, 0);
        if (!alias && (rank_of(a) != rank_of(b) || !fits(elem_of(a), elem_of(b))))
            return fail_at(r, s, "view.copy copies between views of one rank and element type");
        return CINT_SIR_OK;
    }
    if (reduce_word(t, word, n)) {
        int dot = n->sub == CINT_REDUCE_DOT, fold = n->sub == CINT_REDUCE_FOLD_ADD || n->sub == CINT_REDUCE_FOLD_MUL;
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = view_place(r, &a, 0)) != CINT_SIR_OK ||
            (dot && ((st = sep(r)) != CINT_SIR_OK || (st = view_place(r, &b, 0)) != CINT_SIR_OK))) {
            return st;
        }
        if (a.scalar != n->t1 || (dot && (b.scalar != n->t1 || rank_of(a) != 1u || rank_of(b) != 1u)))
            return fail_at(r, s, "the reduction names another element type or rank");
        if ((fold || n->sub == CINT_REDUCE_MIN || n->sub == CINT_REDUCE_MAX) && take(r, ", ")) {
            st = val(r, scalar_type(n->t1));   /* the initial value */
            n->form = 1;
        }
        if (st == CINT_SIR_OK && fold && n->form == 0)
            st = fail_at(r, r->pos, "fold_checked takes an initial value");
        return st != CINT_SIR_OK ? st : define(r, n, scalar_type(n->t2));
    }
    if (name_is(t, word, "view.extent")) {
        n->op = OP_VEXT;
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = view_place(r, &a, 0)) != CINT_SIR_OK ||
            (st = sep(r)) != CINT_SIR_OK || (st = small(r, rank_of(a), 1)) != CINT_SIR_OK) {
            return st;
        }
        n->view = (uint8_t)nview(a);
        n->rank = (uint8_t)rank_of(a);
        return define(r, n, i64);
    }
    if (name_is(t, word, "view.alloc")) {
        n->op = OP_VALLOC;
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = view_dest(r, &a)) != CINT_SIR_OK ||
            (st = sep(r)) != CINT_SIR_OK || (st = small(r, a.rank + 1, 0)) != CINT_SIR_OK) {
            return st;
        }
        n->rank = a.rank;
        n->value = (int64_t)cells(r, a, 0);
        r->f->frame |= 2u;
        return r->m->opds[r->m->nopd - 1u].value == a.rank ? CINT_SIR_OK : fail_at(r, s, "view.alloc names its view's rank");
    }
    if (name_is(t, word, "view.fill")) {   /* box 15: the value into every element */
        n->op = OP_VFILL;
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = view_place(r, &a, 0)) != CINT_SIR_OK) return st;
        if (elem_of(a).kind != TK_SCALAR) return fail_at(r, s, "view.fill fills an array or a view of scalars");
        n->t2 = a.scalar;   /* the element's, which a wide one's limbs follow */
        n->value = (int64_t)cells(r, a, 0);
        return (st = sep(r)) != CINT_SIR_OK ? st : val(r, elem_of(a));
    }
    if (name_is(t, word, "view.reshape")) {   /* box 15: row-major from the place's origin */
        int64_t e = 1;
        n->op = OP_VRESHAPE;
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = view_dest(r, &a)) != CINT_SIR_OK ||
            (st = sep(r)) != CINT_SIR_OK || (st = view_place(r, &b, 0)) != CINT_SIR_OK) {
            return st;
        }
        for (unsigned q = 0; q < a.rank; q++) {   /* an extent of rank 2 to 4 is below 4096 (12 bits) */
            if ((st = sep(r)) != CINT_SIR_OK || (st = small(r, a.rank > 1u ? 4096 : 65535, 0)) != CINT_SIR_OK)
                return st;
            e *= r->m->opds[r->m->nopd - 1u].value;
        }
        n->rank = a.rank;
        if (!fits(elem_of(a), elem_of(b)) || !fits(elem_of(b), elem_of(a)))
            return fail_at(r, s, "the view and its place differ in element type");
        return b.kind != TK_ARRAY || e == b.extent ? CINT_SIR_OK
                                                   : fail_at(r, s, "the extents of the view are not its array's");
    }
    if (name_is(t, word, "array.init")) {
        n->op = OP_AINIT;
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = place(r, &a)) != CINT_SIR_OK) return st;
        if (a.kind != TK_ARRAY || elem_of(a).kind != TK_SCALAR)
            return fail_at(r, s, "array.init fills a local array of scalars");
        n->t2 = a.scalar;   /* the element's, which a wide one's limbs follow (box 15) */
        if ((st = sep(r)) != CINT_SIR_OK || (st = val(r, elem_of(a))) != CINT_SIR_OK || (st = sep(r)) != CINT_SIR_OK ||
            (st = imm(r)) != CINT_SIR_OK || (st = sep(r)) != CINT_SIR_OK) {
            return st;
        }
        return small(r, a.extent, 0);
    }
    if (name_is(t, word, "frame.charge")) {
        uint64_t v = 0;
        n->op = OP_FCHARGE;
        r->f->frame |= 1u;
        st = expect(r, " ");
        if (st == CINT_SIR_OK) st = number(r, (uint64_t)INT64_MAX, &v);
        n->value = (int64_t)v;
        return st;
    }
    if (name_is(t, word, "frame.mark") || name_is(t, word, "frame.release")) {
        uint64_t j = 0, pt = 0;
        uint32_t k = 0;
        n->op = t[word.at + 6] == 'm' ? OP_FMARK : OP_FRELEASE;
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = number(r, COUNT_MAX, &j)) != CINT_SIR_OK ||
            (st = sep(r)) != CINT_SIR_OK || (st = number(r, 3u, &pt)) != CINT_SIR_OK) {
            return st;
        }
        while (k < r->f->nmark && r->marks[k] != j) k++;
        if (pt == 0) return fail_at(r, s, "a mark takes or gives back a part");
        if (n->op == OP_FMARK) {   /* j: the mark's node number in its function, as compiled C names it */
            if (k < r->f->nmark)
                return fail_at(r, s, "a second frame.mark %llu in this function", (unsigned long long)j);
            if (!grow((void **)&r->marks, &r->mcap, r->f->nmark, sizeof *r->marks) ||
                !grow((void **)&r->mparts, &r->pcap, r->f->nmark, sizeof *r->mparts)) {
                return oom(r);
            }
            r->marks[k] = (uint32_t)j;
            r->mparts[k] = (uint8_t)pt;
            r->f->nmark++;
        } else if (k == r->f->nmark || (pt & ~(uint64_t)r->mparts[k]) != 0) {
            return fail_at(r, s, "frame.release gives back parts of an earlier frame.mark");
        }
        r->charges |= (uint8_t)(pt & 1u);
        n->value = (int64_t)k;
        n->t1 = (uint8_t)pt;
        return CINT_SIR_OK;
    }
    return NOT_VIEW;
}

/* ------------------------------------------------------------------ kernels */

#define NOT_KERNEL 101   /* kernel_body: the word names no kernel form */

/* `%<name>`, a parameter of the kernel: a view (out 0), or a scalar `out` (out 1). */
static int kernel_param(rd *r, si_type *ty, int out)
{
    si_opd o = opd_of(OD_SLOT, 0);
    size_t at = r->pos;
    int st = peek(r) == '%' ? percent(r, &o, &at) : fail_at(r, at, "expected a parameter of the kernel");
    if (st != CINT_SIR_OK) return st;
    *ty = o.kind == OD_PARAM ? r->m->params[r->f->param + o.index].ty : scalar_type(SC_NONE);
    if (out ? !ty->out : !nview(*ty))
        return fail_at(r, at, out ? "expected an out parameter of the kernel" : "expected a view parameter of the kernel");
    return add_opd(r, o);
}

/* k slots of I64, each after `, `: with declare, ones the node writes (`$sc.<k>`, or an `over`
 * variable, which has no local line); else ones that hold an I64 already. */
static int i64_slots(rd *r, int64_t k, int declare)
{
    int st = CINT_SIR_OK;
    for (int64_t q = 0; q < k && st == CINT_SIR_OK; q++) {
        si_opd o = opd_of(OD_SLOT, 0);
        size_t at = r->pos + 2u;
        if ((st = sep(r)) != CINT_SIR_OK || (st = slot(r, &o, declare)) != CINT_SIR_OK) return st;
        if (declare) {
            st = receive(r, o, at, scalar_type(SC_I64), 1);
        } else if (r->slots[o.index].kind != TK_SCALAR || r->slots[o.index].scalar != SC_I64) {
            st = fail_at(r, at, "the slot does not hold an I64");
        }
        if (st == CINT_SIR_OK) st = add_opd(r, o);
    }
    return st;
}

/* `dispatch @<module>.<kernel>(<arguments>), <bits>` (back_c.ci, SO_KCALL), checked against the
 * kernel: an array or a view for a view parameter, a value for a scalar, `out` and a scalar slot
 * of its type for a scalar `out`; then the bit of each argument's view that the kernel may
 * write, by position. */
static int dispatch_call(rd *r, si_node *n)
{
    size_t at = r->pos;
    si_opd target = opd_of(OD_IMPORT, 0);
    const si_module *cm = NULL;
    int st = expect(r, " @");
    if (st == CINT_SIR_OK) st = dotted(r, &target.name);
    if (st != CINT_SIR_OK) return st;
    const si_func *cf = callee(r, target.name, FN_KERNEL, &cm);
    if (cf == NULL) {
        return fail_at(r, at + 1u, "no kernel %.*s in this module or a module read before it", (int)target.name.len,
                       (const char *)r->t + target.name.at);
    }
    if ((st = add_opd(r, target)) != CINT_SIR_OK || (st = expect(r, "(")) != CINT_SIR_OK) {
        return st;
    }
    n->cmod = (uint32_t)(cm - r->prog->mods);   /* r->m is mods[r->prog->nmod] */
    n->cfn = (uint32_t)(cf - cm->funcs);
    for (uint32_t a = 0; a < cf->nparam && st == CINT_SIR_OK; a++) {
        si_type want = cm->params[cf->param + a].ty, have = scalar_type(SC_NONE);
        size_t s = r->pos + (a > 0 ? 2u : 0u);
        si_opd o = opd_of(OD_SLOT, 0);
        if (a > 0 && (st = sep(r)) != CINT_SIR_OK) return st;
        if (want.out) {   /* a local's slot or the module's global (lowering.ci: SK_LOCAL, SK_GLOBAL) */
            if ((st = expect(r, "out ")) == CINT_SIR_OK && r->pos < r->eol && r->t[r->pos] == '@') {
                o = opd_of(OD_GLOBAL, 0);
                r->pos++;
                if ((st = dotted(r, &o.name)) != CINT_SIR_OK) return st;
                for (uint32_t g = 0; g < r->m->nglobal; g++) {
                    if (same_name(r->t, r->m->globals[g].name, r->t, o.name) && r->m->globals[g].extent < 0 &&
                        !r->m->globals[g].record) {
                        o.index = g;
                        have = scalar_type(r->m->globals[g].scalar);
                    }
                }
            } else if (st == CINT_SIR_OK && (st = slot(r, &o, 0)) == CINT_SIR_OK) {
                have = r->slots[o.index];
            }
            if (st == CINT_SIR_OK) {
                st = have.kind == TK_SCALAR && have.scalar == want.scalar
                         ? add_opd(r, o)
                         : fail_at(r, s, "argument %u is not a slot or global of its out parameter's type", a + 1u);
            }
        } else if (want.kind == TK_SCALAR) {
            st = val(r, want);
        } else if ((st = place(r, &have)) == CINT_SIR_OK && !fits(want, have)) {
            char x[64];
            type_text(r, want, x, sizeof x);
            st = fail_at(r, s, "argument %u does not fit the parameter's type %s", a + 1u, x);
        }
    }
    if (st == CINT_SIR_OK && (st = expect(r, "), ")) == CINT_SIR_OK) {
        st = small(r, INT64_C(1) << 32, 0);
    }
    return st;
}

/* The kernel forms of box 09 (compiler/OPEN.md CINTC-OQ-35, item 5) after the word, in the
 * dispatch order of back_c.ci's SO_KCALL to SO_KPUB; NOT_KERNEL for another word. In a kernel,
 * bind.shape and bind.alias take their kernel forms, whose places are its view parameters and
 * whose positions those parameters' (the C backend's c and f). */
static int kernel_body(rd *r, si_node *n, si_name word)
{
    static const char *const words[] = {"dispatch", "dispatch.entry", "bind.shape", "bind.where", "bind.alias",
                                        "dispatch.charge", "dispatch.stage", "reduce.init", "dispatch.push",
                                        "dispatch.item", "dispatch.step", "reduce.add", "dispatch.phase",
                                        "reduce.final", "dispatch.pop", "dispatch.publish"};
    const uint8_t *t = r->t;
    int kernel = r->f->kind == FN_KERNEL, st = CINT_SIR_OK, k = 0;
    si_type a = scalar_type(SC_NONE), b = a, i64 = scalar_type(SC_I64);
    si_opd o = opd_of(OD_SLOT, 0);
    size_t s = r->pos + 1u;
    while (k < 16 && !name_is(t, word, words[k])) k++;
    if (k == 16 || ((k == 2 || k == 4) && !kernel)) {
        return NOT_KERNEL;   /* a function's bind.shape and bind.alias are view_body's */
    }
    if ((k == 0) == kernel) {
        return fail_at(r, word.at, kernel ? "a kernel dispatches no kernel" : "%.*s is a form of a kernel", (int)word.len,
                       (const char *)t + word.at);
    }
    n->op = (uint8_t)(OP_KCALL + k);
    switch (k) {
    case 0:
        return dispatch_call(r, n);
    case 2:   /* bind.shape %v, c, f, %t: dimension f of the view at position c has extent t */
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = kernel_param(r, &a, 0)) != CINT_SIR_OK ||
            (st = sep(r)) != CINT_SIR_OK || (st = small(r, 32, 0)) != CINT_SIR_OK || (st = sep(r)) != CINT_SIR_OK ||
            (st = small(r, rank_of(a), 0)) != CINT_SIR_OK || (st = sep(r)) != CINT_SIR_OK) {
            return st;
        }
        return val(r, i64);
    case 3:   /* bind.where c, f, %a, %b: constraint c, comparison f of a and b */
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = small(r, INT64_MAX, 0)) != CINT_SIR_OK ||
            (st = sep(r)) != CINT_SIR_OK) {
            return st;
        }
        s = r->pos;
        if ((st = small(r, 7, 0)) != CINT_SIR_OK || (st = sep(r)) != CINT_SIR_OK || (st = val(r, ANY_INTEGER)) != CINT_SIR_OK ||
            (st = sep(r)) != CINT_SIR_OK) {
            return st;
        }
        n->view = (uint8_t)r->m->opds[r->m->nopd - 2u].value;
        n->t1 = type_of(r, r->m->opds[r->m->nopd - 1u]).scalar;
        return n->view == 0 ? fail_at(r, s, "a comparison is 1 to 6") : val(r, scalar_type(n->t1));
    case 4:   /* bind.alias %v, %u: the written view against another */
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = kernel_param(r, &a, 0)) != CINT_SIR_OK ||
            (st = sep(r)) != CINT_SIR_OK || (st = kernel_param(r, &b, 0)) != CINT_SIR_OK) {
            return st;
        }
        n->t1 = a.scalar;
        return a.scalar == SC_NONE ? fail_at(r, s, "a kernel's view holds scalars") : CINT_SIR_OK;
    case 5:   /* dispatch.charge $n, d, <d extents>: N, their saturating product */
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = slot(r, &o, 1)) != CINT_SIR_OK ||
            (st = receive(r, o, s, i64, 0)) != CINT_SIR_OK || (st = add_opd(r, o)) != CINT_SIR_OK ||
            (st = sep(r)) != CINT_SIR_OK || (st = small(r, 5, 0)) != CINT_SIR_OK) {
            return st;
        }
        return i64_slots(r, r->m->opds[r->m->nopd - 1u].value, 0);
    case 6:   /* dispatch.stage $s, %v, f: the staging view of written view v; f 2 copies it in */
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = slot(r, &o, 1)) != CINT_SIR_OK || (st = add_opd(r, o)) != CINT_SIR_OK ||
            (st = sep(r)) != CINT_SIR_OK || (st = kernel_param(r, &a, 0)) != CINT_SIR_OK || (st = sep(r)) != CINT_SIR_OK ||
            (st = small(r, 3, 0)) != CINT_SIR_OK) {
            return st;
        }
        r->f->frame |= 2u;
        a.extent = -1;
        return r->m->opds[r->m->nopd - 1u].value == 0 ? fail_at(r, s, "a stage copies in (2) or not (1)")
                                                       : receive(r, o, s, a, 0);
    case 7:   /* reduce.init %out, %v: the accumulator's initial value */
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = kernel_param(r, &a, 1)) != CINT_SIR_OK || (st = sep(r)) != CINT_SIR_OK)
            return st;
        return val(r, scalar_type(a.scalar));
    case 9:   /* dispatch.item %w, d, <d `over` variables>, <d extents> */
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = val(r, i64)) != CINT_SIR_OK || (st = sep(r)) != CINT_SIR_OK ||
            (st = small(r, 5, 0)) != CINT_SIR_OK) {
            return st;
        }
        k = (int)r->m->opds[r->m->nopd - 1u].value;
        return (st = i64_slots(r, k, 1)) != CINT_SIR_OK ? st : i64_slots(r, k, 0);
    case 12:   /* dispatch.phase 2: the epilogue */
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = small(r, 3, 0)) != CINT_SIR_OK) return st;
        return r->m->opds[r->m->nopd - 1u].value == 2 ? CINT_SIR_OK : fail_at(r, s, "the phase after the work-items is 2");
    case 15:   /* dispatch.publish %v, $s: the staging view into v; or dispatch.publish %out */
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = percent(r, &o, &s)) != CINT_SIR_OK)
            return st;
        r->pos = s;
        if (o.kind == OD_PARAM && r->m->params[r->f->param + o.index].ty.out)
            return kernel_param(r, &a, 1);
        if ((st = kernel_param(r, &a, 0)) != CINT_SIR_OK || (st = sep(r)) != CINT_SIR_OK) return st;
        s = r->pos;
        if ((st = view_place(r, &b, 1)) != CINT_SIR_OK) return st;
        return b.scalar == a.scalar && b.rank == a.rank ? CINT_SIR_OK : fail_at(r, s, "the stage is not of its view's type");
    case 11:
    case 13: {   /* reduce.add <reduction> %out, %v and reduce.final <reduction> %out (back_c.ci, c_kred) */
        si_name w = {0u, 0u};
        if ((st = expect(r, " ")) != CINT_SIR_OK) return st;
        w.at = (uint32_t)r->pos;
        while (r->pos < r->eol && (is_ident(peek(r)) || peek(r) == '.')) r->pos++;
        w.len = (uint32_t)(r->pos - w.at);
        if (!reduce_word(t, w, n) || n->sub == CINT_REDUCE_FOLD_MUL || n->sub == CINT_REDUCE_DOT)
            return fail_at(r, w.at, "no reduction of a kernel '%.*s'", (int)w.len, (const char *)t + w.at);
        n->op = k == 11 ? OP_KRED : OP_KEPI;
        n->fmt[0] = w.at;   /* the reduction, which its site names */
        n->fmt[1] = w.len;
        s = r->pos + 1u;
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = kernel_param(r, &a, 1)) != CINT_SIR_OK)
            return st;
        if (a.scalar != n->t2)
            return fail_at(r, s, "the reduction's result is not of its out parameter's type");
        return k == 11 && (st = sep(r)) == CINT_SIR_OK ? val(r, scalar_type(n->t1)) : st;
    }
    default:   /* dispatch.entry, .push, .step, .pop */
        return CINT_SIR_OK;
    }
}

#define NOT_ERROR 102   /* error_body: the word names no error form */

/* The set `E<k>` or the conversion `W<k>` of this module (error_line): its index, or -1. */
static int64_t named(const rd *r, int set, uint64_t k)
{
    for (uint32_t i = 0; set && i < r->m->neset; i++)
        if (r->m->esets[i].k == (int64_t)k) return i;
    for (uint32_t i = 0; !set && i < r->m->nwiden; i++)
        if (r->m->widens[i].k == (int64_t)k) return i;
    return -1;
}

/* `E<k>` or `W<k>` after its letter, as n->value. */
static int set_ref(rd *r, si_node *n, int set)
{
    size_t at = r->pos - 1u;
    uint64_t k = 0;
    int st = number(r, (uint64_t)INT64_MAX, &k);
    if (st == CINT_SIR_OK && (n->value = named(r, set, k)) < 0) {
        st = fail_at(r, at, "no %s %c%llu before the functions", set ? "error set" : "conversion", set ? 'E' : 'W',
                     (unsigned long long)k);
    }
    return st;
}

/* The error forms of box 12 (compiler/OPEN.md CINTC-OQ-63, item 1; box 13 unit 8b):
 * `%t = error.tag` right after the node that gives a tag (a call of a function with an error
 * union result, or a result form), `return.error %t`, `error.leave %t, E<k>` in a test, and
 * `%t = widen.<from>.<to> %t, W<k>`. NOT_ERROR for another word. */
static int error_body(rd *r, si_node *n, si_name word)
{
    const uint8_t *t = r->t;
    const si_func *f = r->f;
    int st = CINT_SIR_OK;
    parts p;
    if (name_is(t, word, "error.tag")) {
        const si_node *g = r->m->nnode >= 2u && r->m->nnode - 2u >= f->first ? &r->m->nodes[r->m->nnode - 2u] : NULL;
        int sc = g == NULL ? 0 : g->op == OP_CALL ? r->prog->mods[g->cmod].funcs[g->cfn].etag
                 : (g->op == OP_BIN || g->op == OP_AS) && g->form == FM_RESULT ? SC_U16
                 : g->op == OP_MEMBER && (g->sub == MB_ALLOC_RESULT || g->sub == MB_INSERT_RESULT) ? SC_U16 : 0;
        n->op = OP_ETAG;
        if (sc == 0)
            return fail_at(r, word.at, "error.tag follows a call of a function with an error union result, or a result form");
        return define(r, n, scalar_type(sc));
    }
    if (name_is(t, word, "return.error")) {
        n->op = OP_RETURN;
        n->form = 1;
        if (f->etag == 0)
            return fail_at(r, word.at, "return.error is in a function with an error union result");
        return (st = expect(r, " ")) != CINT_SIR_OK ? st : val(r, scalar_type(f->etag));
    }
    if (name_is(t, word, "error.leave")) {
        size_t at = r->pos + 1u;
        n->op = OP_ELEAVE;
        if (f->kind != FN_TEST) return fail_at(r, word.at, "error.leave is in a test");
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = val(r, ANY_INTEGER)) != CINT_SIR_OK ||
            (st = expect(r, ", E")) != CINT_SIR_OK || (st = set_ref(r, n, 1)) != CINT_SIR_OK) {
            return st;
        }
        return check_type(r, at, scalar_type(r->m->esets[n->value].scalar), type_of(r, r->m->opds[r->m->nopd - 1u]));
    }
    if (!split(t, word, &p) || p.n != 3u || !name_is(t, p.p[0], "widen")) return NOT_ERROR;
    int from = scalar_of(t, p.p[1], 1), to = scalar_of(t, p.p[2], 1);
    n->op = OP_WIDEN;
    n->t1 = (uint8_t)(from & 0xFF);
    n->t2 = (uint8_t)(to & 0xFF);
    if (!is_integer(from) || is_signed(from) || !is_integer(to) || is_signed(to))
        return fail_at(r, word.at, "widen converts between unsigned tag types");
    if ((st = expect(r, " ")) != CINT_SIR_OK || (st = val(r, scalar_type(from))) != CINT_SIR_OK ||
        (st = expect(r, ", W")) != CINT_SIR_OK || (st = set_ref(r, n, 0)) != CINT_SIR_OK) {
        return st;
    }
    return define(r, n, scalar_type(to));
}

#define NOT_MEM 103   /* mem_body: the word names no memory form */

/* Box 12's memory forms (compiler/OPEN.md CINTC-OQ-64, item 10; box 13 unit 8b): the members
 * `%t = arena.alloc <arena>, %n` (and .alloc_result), `arena.child <arena>, %n into $c.k`,
 * `arena.reset <arena>`, `%t = pool.insert <pool>, <element>` (and .insert_result; a struct's
 * slot or a scalar) and `pool.remove <pool>, %h`; `handle.view $v.k, <arena>, %h`, which binds
 * slot k to a view of rank 1 of the allocation and slot k + 1 to its lease; `handle.lease
 * $l.k`; and `pool.deref $r.k, <pool>, %h`, which binds slot k to the element. NOT_MEM for
 * another word. */
static int mem_body(rd *r, si_node *n, si_name word)
{
    static const char *const MEMBERS[] = {"arena.alloc", "arena.alloc_result", "arena.child", "arena.reset",
                                          "pool.insert", "pool.insert_result", "pool.remove"};
    const uint8_t *t = r->t;
    si_type c = scalar_type(SC_NONE), e = c, h = special_type(TK_HANDLE, 0);
    si_opd o = opd_of(OD_IMM, 0);
    size_t at = r->pos + 1u;
    int st = CINT_SIR_OK, k = 0, v = name_is(t, word, "handle.view");
    h.smod = UINT32_MAX;
    while (k < 7 && !name_is(t, word, MEMBERS[k])) k++;
    if (name_is(t, word, "handle.lease")) {
        n->op = OP_HLEASE;
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = slot(r, &o, 0)) != CINT_SIR_OK) return st;
        return r->slots[o.index].kind == TK_LEASE ? add_opd(r, o) : fail_at(r, at, "handle.lease takes a view's lease");
    }
    if (k == 7 && !v && !name_is(t, word, "pool.deref")) return NOT_MEM;
    n->op = k < 7 ? OP_MEMBER : v ? OP_HVIEW : OP_PDEREF;
    n->sub = (uint8_t)k;
    if ((st = expect(r, " ")) != CINT_SIR_OK) return st;
    if (k == 7 && ((st = slot(r, &o, 1)) != CINT_SIR_OK || (st = add_opd(r, o)) != CINT_SIR_OK ||
                   (st = sep(r)) != CINT_SIR_OK)) {
        return st;
    }
    if ((st = container(r, k < MB_INSERT || v ? CINT_HANDLE_ARENA : CINT_HANDLE_POOL, &c)) != CINT_SIR_OK ||
        k == MB_RESET || (st = sep(r)) != CINT_SIR_OK) {
        return st;
    }
    e = c;   /* the element */
    e.kind = c.scalar != SC_NONE ? TK_SCALAR : TK_STRUCT;
    e.mem = 0;
    n->value = (int64_t)cells(r, e, 1);
    n->t1 = e.scalar;
    if (k == MB_INSERT || k == MB_INSERT_RESULT) {
        si_type have = scalar_type(SC_NONE);
        at = r->pos;
        st = e.kind == TK_SCALAR ? val(r, e) : place(r, &have);
        if (st == CINT_SIR_OK && e.kind == TK_STRUCT && !fits(e, have))
            st = fail_at(r, at, "the element is not of the pool's type");
    } else {
        st = val(r, k == MB_ALLOC || k == MB_ALLOC_RESULT || k == MB_CHILD ? scalar_type(SC_I64) : h);
    }
    if (st != CINT_SIR_OK || k == MB_REMOVE) return st;
    if (k == MB_CHILD) {
        at = r->pos + 6u;
        if ((st = expect(r, " into ")) != CINT_SIR_OK || (st = slot(r, &o, 0)) != CINT_SIR_OK)
            return st;
        return (st = check_type(r, at, c, type_of(r, o))) != CINT_SIR_OK ? st : add_opd(r, o);
    }
    if (k < 7) return define(r, n, h);
    if (v) {   /* a view of rank 1 of the allocation, its index an element offset, and its lease */
        e.kind = TK_VIEW;
        e.extent = -1;
        e.rank = 1;
        if (o.index + 1u < r->f->nslot && r->slots[o.index + 1u].kind != TK_NONE)
            return fail_at(r, at, "slot %u holds the view's lease", o.index + 1u);
    }
    if ((st = receive(r, o, at, e, 1)) != CINT_SIR_OK) return st;
    return v ? bind_slot(r, o.index + 1u, special_type(TK_LEASE, 0)) : CINT_SIR_OK;
}

/* The operation of a node line after its `%t<N> = `, with its operands. */
static int node_body(rd *r, si_node *n, si_name word)
{
    const uint8_t *t = r->t;
    int st = CINT_SIR_OK;
    si_type ty = scalar_type(SC_NONE);
    size_t s = r->pos;
    int ks = kernel_body(r, n, word);
    if (ks != NOT_KERNEL) return ks;
    if (arith_name(t, word, n)) {
        si_type a = scalar_type(n->t1), res = scalar_type(n->op == OP_CMP ? SC_BOOL : n->t1);
        if (n->op == OP_AS) res = scalar_type(n->t2);
        int arity = n->op == OP_UN || n->op == OP_AS ? 1 : 2;
        if (n->op == OP_BUILTIN) {
            arity = n->sub == 1 || n->sub == BI_UABS || n->sub == BI_ISQRT || n->sub == BI_ISQRT_ROUND ||
                            n->sub == BI_SQRT ? 1
                    : n->sub == BI_CLAMP || n->sub == BI_MULDIV ? 3 : 2;
            res = scalar_type(n->t2);
        }
        for (int k = 0; k < arity && st == CINT_SIR_OK; k++) {
            st = expect(r, k == 0 ? " " : ", ");
            int shift = n->op == OP_BIN && (n->sub == BO_SHL || n->sub == BO_SHR);
            int count = k == 1 && (shift || (n->op == OP_BUILTIN && (n->sub == BI_ROTL || n->sub == BI_ROTR)));
            /* a shift's count may be wide (RT-OQ-52); a rotation's, which B1 refuses, may not */
            if (st == CINT_SIR_OK)   /* qmul's and mul_int's second operand: its own type (box 15) */
                st = val(r, count ? (shift ? ANY_COUNT : ANY_INTEGER)
                            : k == 1 && n->op == OP_BUILTIN && n->sub >= BI_QMUL ? scalar_type(n->t3) : a);
            if (st == CINT_SIR_OK && count) n->t3 = type_of(r, r->m->opds[r->m->nopd - 1u]).scalar;
        }
        return st != CINT_SIR_OK ? st : define(r, n, res);
    }
    if (name_is(t, word, "const")) {
        int sc = 0;
        n->op = OP_CONST;
        if (take(r, " Handle 0")) {   /* the null handle (box 12) */
            n->ty = special_type(TK_HANDLE, 0);
            n->ty.smod = UINT32_MAX;
            return define(r, n, n->ty);
        }
        if ((st = expect(r, " ")) == CINT_SIR_OK && (st = scalar_name(r, &sc)) == CINT_SIR_OK &&
            (st = expect(r, " ")) == CINT_SIR_OK) {
            uint32_t at = 0;   /* a wide constant's limbs are items (box 15) */
            st = is_wide(sc) ? wide_item(r, sc, &at) : value(r, sc, &n->value);
            n->value = is_wide(sc) ? (int64_t)at : n->value;
        }
        n->ty = scalar_type(sc);
        return st != CINT_SIR_OK ? st : define(r, n, n->ty);
    }
    if (name_is(t, word, "load.local") || name_is(t, word, "store.local")) {
        si_opd o;
        n->op = name_is(t, word, "load.local") ? OP_LOAD_LOCAL : OP_STORE_LOCAL;
        if ((st = expect(r, " ")) != CINT_SIR_OK ||
            (st = slot(r, &o, r->f->kind == FN_KERNEL && n->op == OP_STORE_LOCAL)) != CINT_SIR_OK ||
            (st = add_opd(r, o)) != CINT_SIR_OK) {
            return st;
        }
        if (o.index >= r->f->nslot || r->slots[o.index].kind == TK_NONE) {
            /* a kernel's hidden scalar `$sc.<k>` (its extents, its work-item counter) takes the
             * type of its first store, as it has no local line */
            if ((st = sep(r)) != CINT_SIR_OK || (st = val(r, ANY_SCALAR)) != CINT_SIR_OK) return st;
            return receive(r, o, s + 1u, type_of(r, r->m->opds[r->m->nopd - 1u]), 0);
        }
        ty = r->slots[o.index];
        n->ty = ty;
        if (ty.kind != TK_SCALAR && ty.kind != TK_HANDLE)
            return fail_at(r, s + 1u, "load.local and store.local take a scalar or handle slot");
        if (n->op == OP_LOAD_LOCAL) return define(r, n, ty);
        return (st = sep(r)) != CINT_SIR_OK ? st : val(r, ty);
    }
    if (name_is(t, word, "load.global") || name_is(t, word, "store.global")) {
        si_opd o = opd_of(OD_GLOBAL, 0);
        n->op = name_is(t, word, "load.global") ? OP_LOAD_GLOBAL : OP_STORE_GLOBAL;
        if ((st = expect(r, " @")) != CINT_SIR_OK || (st = dotted(r, &o.name)) != CINT_SIR_OK)
            return st;
        ty = UNKNOWN;
        int found = 0;
        for (uint32_t g = 0; g < r->m->nglobal && !found; g++) {
            if (same_name(t, r->m->globals[g].name, t, o.name)) {
                o.index = g;
                ty = scalar_type(r->m->globals[g].scalar);
                if (r->m->globals[g].handle) {   /* a Handle (OQ-242), as sized_type types it */
                    ty.kind = TK_HANDLE;
                    ty.smod = UINT32_MAX;
                }
                found = 1;
                if (r->m->globals[g].extent >= 0 || r->m->globals[g].record) {
                    return fail_at(r, s + 2u, "@%.*s is %s", (int)o.name.len, (const char *)t + o.name.at,
                                   r->m->globals[g].record ? "a struct" : "an array");
                }
            }
        }
        if (!found) {
            uint32_t dots = 0;
            for (uint32_t i = 0; i < o.name.len; i++) dots += t[o.name.at + i] == '.';
            if (dots == 0)
                return fail_at(r, s + 2u, "no global @%.*s", (int)o.name.len, (const char *)t + o.name.at);
            o.kind = OD_IMPORT;   /* another module's variable: its type is not in this text */
        }
        if ((st = add_opd(r, o)) != CINT_SIR_OK) return st;
        if (n->op == OP_LOAD_GLOBAL) return define(r, n, ty);
        return (st = sep(r)) != CINT_SIR_OK ? st : val(r, found ? ty : ANY_SCALAR);
    }
    if (name_is(t, word, "local")) {
        si_opd o;
        n->op = OP_LOCAL;
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = slot(r, &o, 1)) != CINT_SIR_OK ||
            (st = expect(r, ": ")) != CINT_SIR_OK || (st = sized_type(r, &ty)) != CINT_SIR_OK) {
            return st;
        }
        if (o.index < r->f->nslot && r->slots[o.index].kind != TK_NONE)
            return fail_at(r, s + 1u, "slot %u has a local line already", o.index);
        if ((st = bind_slot(r, o.index, ty)) != CINT_SIR_OK) return st;
        n->ty = ty;
        n->value = (int64_t)cells(r, ty, 1);
        return add_opd(r, o);
    }
    if (name_is(t, word, "field.load") || name_is(t, word, "field.store") || name_is(t, word, "field.index.load") ||
        name_is(t, word, "field.index.store")) {
        int ix = word.len > 12u, load = t[word.at + word.len - 1u] == 'd';
        si_type fty;
        n->op = ix ? (load ? OP_FILOAD : OP_FISTORE) : load ? OP_FLOAD : OP_FSTORE;
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = place(r, &ty)) != CINT_SIR_OK ||
            (st = sep(r)) != CINT_SIR_OK || (st = scalar_field(r, ty, ix, &fty)) != CINT_SIR_OK ||
            (ix && ((st = sep(r)) != CINT_SIR_OK || (st = val(r, ANY_INTEGER)) != CINT_SIR_OK))) {
            return st;
        }
        n->ty = fty;
        return load ? define(r, n, fty) : (st = sep(r)) != CINT_SIR_OK ? st : val(r, fty);
    }
    if (name_is(t, word, "element.load") || name_is(t, word, "element.store")) {
        si_type fty;
        n->op = name_is(t, word, "element.load") ? OP_ELOAD : OP_ESTORE;
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = place(r, &ty)) != CINT_SIR_OK ||
            (st = sep(r)) != CINT_SIR_OK || (st = val(r, ANY_INTEGER)) != CINT_SIR_OK ||
            (st = sep(r)) != CINT_SIR_OK || (st = scalar_field(r, elem_of(ty), 0, &fty)) != CINT_SIR_OK) {
            return st;
        }
        if (nview(ty))
            return fail_at(r, s + 1u, "%.*s takes an array or a view parameter", (int)word.len, (const char *)t + word.at);
        n->value = (int64_t)cells(r, ty, 0);
        n->ty = fty;
        return n->op == OP_ELOAD ? define(r, n, fty) : (st = sep(r)) != CINT_SIR_OK ? st : val(r, fty);
    }
    if (name_is(t, word, "field.copy") || name_is(t, word, "array.field.copy") || name_is(t, word, "field.copy.out")) {
        si_type fty, other;
        si_opd o;
        size_t at = r->pos + 1u;
        n->op = name_is(t, word, "field.copy.out") ? OP_FCOPY_OUT : name_is(t, word, "field.copy") ? OP_FCOPY : OP_AFCOPY;
        if (n->op == OP_FCOPY_OUT) {
            /* the destination may be a hidden struct slot `$sc.k` the node introduces */
            if ((st = expect(r, " ")) != CINT_SIR_OK || (st = slot(r, &o, 1)) != CINT_SIR_OK ||
                (st = add_opd(r, o)) != CINT_SIR_OK || (st = sep(r)) != CINT_SIR_OK ||
                (st = place(r, &ty)) != CINT_SIR_OK || (st = sep(r)) != CINT_SIR_OK ||
                (st = field(r, ty, &fty)) != CINT_SIR_OK) {
                return st;
            }
            n->value = (int64_t)cells(r, fty, 1);
            return fty.kind != TK_STRUCT ? fail_at(r, at, "field.copy.out copies a struct field")
                                         : receive(r, o, at, fty, 0);
        }
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = place(r, &ty)) != CINT_SIR_OK ||
            (st = sep(r)) != CINT_SIR_OK || (st = field(r, ty, &fty)) != CINT_SIR_OK || (st = sep(r)) != CINT_SIR_OK) {
            return st;
        }
        at = r->pos;
        if ((st = place(r, &other)) != CINT_SIR_OK) return st;
        int arr = n->op == OP_AFCOPY;
        int same = arr ? fty.kind == TK_ARRAY && (other.kind == TK_ARRAY || other.kind == TK_VIEW) &&
                             fits(elem_of(fty), elem_of(other))
                       : fty.kind == TK_STRUCT && fits(fty, other);
        n->value = (int64_t)cells(r, fty, !arr);
        return same ? CINT_SIR_OK : fail_at(r, at, "the source does not have the field's type");
    }
    if (name_is(t, word, "field.address") || name_is(t, word, "array.field.address") ||
        name_is(t, word, "element.address")) {
        si_opd o;
        si_type ref = scalar_type(SC_NONE);
        size_t at = r->pos + 1u;
        int el = name_is(t, word, "element.address");
        n->op = el ? OP_EADDR : name_is(t, word, "field.address") ? OP_FADDR : OP_AFADDR;
        /* the first operand is a reference slot the node binds; it has no local line */
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = slot(r, &o, 1)) != CINT_SIR_OK ||
            (st = add_opd(r, o)) != CINT_SIR_OK || (st = sep(r)) != CINT_SIR_OK ||
            (st = place(r, &ty)) != CINT_SIR_OK || (st = sep(r)) != CINT_SIR_OK) {
            return st;
        }
        size_t f = r->pos;
        if (!el) {
            st = field(r, ty, &ref);
        } else if ((st = val(r, ANY_INTEGER)) == CINT_SIR_OK && take(r, ", ")) {
            f = r->pos;   /* an element of an array field */
            st = field(r, ty, &ty);
        }
        if (st == CINT_SIR_OK && el && ty.kind != TK_ARRAY && ty.kind != TK_VIEW)
            st = fail_at(r, f, "element.address takes an array or a view");
        if (st != CINT_SIR_OK) return st;
        ref = el ? elem_of(ty) : ref;
        n->value = (int64_t)cells(r, ty, 0);
        n->view = (uint8_t)(el && nview(ty));   /* an element offset (box 12's handle.view) */
        return receive(r, o, at, ref, 1);
    }
    if (name_is(t, word, "bind.shape") || name_is(t, word, "copy.shape")) {
        si_type c = scalar_type(SC_NONE);
        n->op = name_is(t, word, "bind.shape") ? OP_BSHAPE : OP_CSHAPE;
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = place(r, &ty)) != CINT_SIR_OK ||
            (st = sep(r)) != CINT_SIR_OK || (st = imm(r)) != CINT_SIR_OK || (st = sep(r)) != CINT_SIR_OK ||
            (st = is_digit(peek(r)) ? imm(r) : place(r, &c)) != CINT_SIR_OK) {
            return st;
        }
        /* compiled C reads a view's shape[d], else the extent (back_c.ci, c_dext) */
        n->view = (uint8_t)(nview(ty) | nview(c) << 1);
        if (n->op == OP_BSHAPE && take(r, ", ")) {
            /* per dimension: the argument's dimension, plus 16 with the dimension operand, and the
             * dimension of the expected view (CINTC-OQ-35, item 5) */
            if ((st = small(r, 20, 0)) != CINT_SIR_OK || (st = sep(r)) != CINT_SIR_OK || (st = small(r, 4, 0)) != CINT_SIR_OK)
                return st;
            if (r->m->opds[r->m->nopd - 2u].value % 16 >= 4)
                return fail_at(r, s, "bind.shape names a dimension a view does not have");
        }
        return st;
    }
    if (name_is(t, word, "struct.copy") || name_is(t, word, "array.copy")) {
        n->op = name_is(t, word, "struct.copy") ? OP_SCOPY : OP_ACOPY;
        si_type from;
        si_opd o = opd_of(OD_SLOT, 0);
        size_t d = r->pos + 1u;
        if ((st = expect(r, " ")) != CINT_SIR_OK) return st;
        /* the destination may be a hidden struct slot `$sc.k` the node introduces, where cintc
         * keeps a struct result while a defer runs */
        if (n->op == OP_SCOPY && peek(r) == '$') {
            if ((st = slot(r, &o, 1)) != CINT_SIR_OK) return st;
            n->t1 = (uint8_t)(name_is(t, o.name, "sc") && (o.index >= r->f->nslot || r->slots[o.index].kind == TK_NONE));
            r->pos = n->t1 ? r->pos : d;   /* a slot with its local line reads as a place */
        }
        if ((st = n->t1 ? add_opd(r, o) : place(r, &ty)) != CINT_SIR_OK || (st = sep(r)) != CINT_SIR_OK)
            return st;
        size_t a = r->pos;
        if ((st = place(r, &from)) != CINT_SIR_OK) return st;
        if (n->t1) {
            if (from.kind != TK_STRUCT) return fail_at(r, a, "struct.copy copies a struct");
            n->value = (int64_t)cells(r, from, 1);
            return receive(r, o, d, from, 0);
        }
        if ((ty.kind == TK_STRUCT) != (n->op == OP_SCOPY)) {
            return fail_at(r, s + 1u, "%s copies %s", n->op == OP_SCOPY ? "struct.copy" : "array.copy",
                           n->op == OP_SCOPY ? "a struct" : "an array");
        }
        /* arrays and views by element; their extents are checked when the copy runs */
        int same = n->op == OP_SCOPY ? fits(ty, from) : from.kind != TK_STRUCT && fits(elem_of(ty), elem_of(from));
        n->value = (int64_t)cells(r, ty, n->op == OP_SCOPY);
        return same ? CINT_SIR_OK : fail_at(r, a, "the source does not have the destination's type");
    }
    if (word.len > 14 && memcmp(t + word.at, "index.checked.", 14) == 0) {
        si_name sfx = {word.at + 14u, word.len - 14u};
        int sc = scalar_of(t, sfx, 1);
        n->op = OP_ICHECK;
        n->t1 = (uint8_t)(sc > 0 ? sc : SC_NONE);
        if (sc <= 0 && !name_is(t, sfx, "struct")) return fail_at(r, word.at, "unknown operation");
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = place(r, &ty)) != CINT_SIR_OK ||
            (st = sep(r)) != CINT_SIR_OK || (st = val(r, ANY_INTEGER)) != CINT_SIR_OK) {
            return st;
        }
        n->view = (uint8_t)nview(ty);
        n->rank = (uint8_t)rank_of(ty);
        if (n->rank > 1u) {   /* the dimension of a view of rank 2 to 4 (ruling R1) */
            return (st = sep(r)) != CINT_SIR_OK ? st : small(r, n->rank, 0);
        }
        size_t f = r->pos + 2u;
        if (!n->view && take(r, ", ") && (st = field(r, ty, &ty)) == CINT_SIR_OK && ty.kind != TK_ARRAY) {
            return fail_at(r, f, "the field is not an array");   /* an element of an array field */
        }
        return st;
    }
    if (name_is(t, word, "index.load") || name_is(t, word, "index.store")) {
        n->op = name_is(t, word, "index.load") ? OP_ILOAD : OP_ISTORE;
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = place(r, &ty)) != CINT_SIR_OK ||
            (st = sep(r)) != CINT_SIR_OK || (st = val(r, ANY_INTEGER)) != CINT_SIR_OK) {
            return st;
        }
        if (elem_of(ty).kind != TK_SCALAR)
            return fail_at(r, s + 1u, "%.*s takes an array or a view of scalars", (int)word.len, (const char *)t + word.at);
        n->view = (uint8_t)nview(ty);   /* the index is an element offset (view.offset) */
        n->t2 = ty.scalar;
        if (n->op == OP_ILOAD) return define(r, n, elem_of(ty));
        return (st = sep(r)) != CINT_SIR_OK ? st : val(r, elem_of(ty));
    }
    if (name_is(t, word, "call")) {
        n->op = OP_CALL;
        return call(r, n, n->dst >= 0);
    }
    if (name_is(t, word, "discard") || name_is(t, word, "assert.checked.bool")) {
        int d = name_is(t, word, "discard");
        n->op = d ? OP_DISCARD : OP_ASSERT;
        return (st = expect(r, " ")) != CINT_SIR_OK ? st : val(r, d ? ANY_VALUE : scalar_type(SC_BOOL));
    }
    if (name_is(t, word, "fuel.charge")) {
        uint64_t v = 0;
        n->op = OP_FUEL;
        st = expect(r, " ");
        if (st == CINT_SIR_OK) st = number(r, (uint64_t)INT64_MAX, &v);
        n->value = (int64_t)v;
        return st;
    }
    if (name_is(t, word, "return")) {
        n->op = OP_RETURN;
        si_type res = r->f->result;
        if (res.kind == TK_NONE) return CINT_SIR_OK;
        if ((st = expect(r, " ")) != CINT_SIR_OK) return st;
        if (res.kind == TK_SCALAR || res.kind == TK_HANDLE) return val(r, res);
        size_t a = r->pos;
        if ((st = place(r, &ty)) != CINT_SIR_OK) return st;
        n->value = (int64_t)cells(r, res, 1);
        return fits(res, ty) ? CINT_SIR_OK : fail_at(r, a, "the place does not hold the result type");
    }
    if (name_is(t, word, "print.begin") || name_is(t, word, "print.end")) {
        n->op = name_is(t, word, "print.begin") ? OP_PRINT_BEGIN : OP_PRINT_END;
        return CINT_SIR_OK;
    }
    if (name_is(t, word, "print.bytes")) {
        n->op = OP_PRINT_BYTES;
        return (st = expect(r, " ")) != CINT_SIR_OK ? st : print_text(r, &n->value);
    }
    if (name_is(t, word, "print.value")) {
        int sc = 0;
        n->op = OP_PRINT_VALUE;
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = scalar_name(r, &sc)) != CINT_SIR_OK ||
            (st = expect(r, " ")) != CINT_SIR_OK || (st = val(r, scalar_type(sc))) != CINT_SIR_OK) {
            return st;
        }
        n->ty = scalar_type(sc);
        if (take(r, " format ")) {
            for (int k = 0; k < 4 && st == CINT_SIR_OK; k++) {
                st = k ? expect(r, " ") : CINT_SIR_OK;
                if (st == CINT_SIR_OK) st = signed_number(r, &n->fmt[k]);
            }
            if (st == CINT_SIR_OK && n->fmt[0] == 0) st = fail_at(r, s, "a format's form is not 0");
        }
        return st;
    }
    if (name_is(t, word, "while")) {
        n->op = OP_WHILE;
        return (st = expect(r, " ")) != CINT_SIR_OK ? st : val(r, scalar_type(SC_BOOL));
    }
    if ((st = view_body(r, n, word)) != NOT_VIEW) return st;
    if ((st = error_body(r, n, word)) != NOT_ERROR) return st;
    if ((st = mem_body(r, n, word)) != NOT_MEM) return st;
    return fail_at(r, word.at, "unknown operation '%.*s'", (int)word.len, (const char *)t + word.at);
}

/* The site annotation of the line, if any: `#<ordinal> @<path>:<line>:<column>` at the
 * end of the line (the SIR-17 cut of compiler/back_c.ci, site_cut). Sets r->eol to the
 * end of the node's text and *ord to the ordinal, 0 for none. */
static int annotation(rd *r, uint32_t *ord)
{
    const uint8_t *t = r->t;
    size_t e = r->eol;
    *ord = 0;
    /* a line is annotated exactly when SIR-17 cuts it (site_cut) */
    size_t sp = site_cut(t, r->bol, e), k = sp + 1u, h = sp;
    if (sp == e) return CINT_SIR_OK;
    while (is_digit(t[h - 1u])) h--;
    size_t hash = h - 1u;
    if (hash == r->bol || t[hash - 1u] != ' ')
        return fail_at(r, hash, "a site annotation follows a space");
    size_t end = hash;
    while (end > r->bol && t[end - 1u] == ' ') end--;
    size_t want = end - r->bol + 1u > SITE_COLUMN ? end - r->bol + 1u : SITE_COLUMN;
    if (hash - r->bol != want)
        return fail_at(r, hash, "a site annotation starts at column %u of this line", (unsigned)(want + 1u));
    /* `#<ordinal> @<path>:<line>:<column>` */
    rd a = *r;
    a.pos = h;
    a.eol = sp;
    uint32_t o = 0, line = 0, col = 0;
    int st = number32(&a, &o);
    if (st != CINT_SIR_OK) return st;
    a.pos = k + 1u;
    a.eol = e;
    size_t plen = strlen(r->m->path);
    if (e - a.pos < plen || memcmp(t + a.pos, r->m->path, plen) != 0)
        return fail_at(r, k, "a site annotation names the module's path, %s", r->m->path);
    a.pos += plen;
    if ((st = expect(&a, ":")) != CINT_SIR_OK || (st = number32(&a, &line)) != CINT_SIR_OK ||
        (st = expect(&a, ":")) != CINT_SIR_OK || (st = number32(&a, &col)) != CINT_SIR_OK) {
        return st;
    }
    if (a.pos != e) return fail_at(r, a.pos, "unexpected text at the end of the line");
    /* a frame arena charge takes the sites after the module's, in node order (back_c.ci,
     * write_sites) */
    size_t w = r->bol;
    while (t[w] == ' ') w++;
    int charge = w + 13u < end && memcmp(t + w, "frame.charge ", 13) == 0;
    uint32_t *next = charge ? &r->next_frame : &r->next_site;
    if (o > r->m->nsite) return fail_at(r, hash, "site #%u is not in the sites line", o);
    if (o != *next || (!charge && o >= r->frame_site0)) {
        return fail_at(r, hash, "site #%u where #%u comes next (sites are numbered in node order%s)", o,
                       *next, charge ? ", a frame.charge's after the module's" : "");
    }
    if (r->m->sites[o].line != line || r->m->sites[o].col != col)
        return fail_at(r, k, "site #%u is at %u:%u in the sites line", o, r->m->sites[o].line, r->m->sites[o].col);
    (*next)++;
    *ord = o;
    r->eol = end;
    return CINT_SIR_OK;
}

/* One line of a function body at r->bol. Returns CINT_SIR_OK with *done set after the
 * function's closing brace. */
static int body_line(rd *r, int *done)
{
    si_module *m = r->m;
    const uint8_t *t = r->t;
    uint32_t ord = 0;
    int st = annotation(r, &ord);
    if (st != CINT_SIR_OK) return st;
    size_t ind = 0;
    while (r->bol + ind < r->eol && t[r->bol + ind] == ' ') ind++;
    r->pos = r->bol + ind;
    int closing = peek(r) == '}';
    uint32_t depth = closing ? r->depth - 1u : r->depth;
    uint32_t cols = depth > 16u ? 32u : 2u * depth;  /* two per level, 32 past 16 levels (SIR-09, OQ-205) */
    if (ind != cols)
        return fail_at(r, r->pos, "indented %u spaces where %u are expected", (unsigned)ind, cols);
    if (!grow((void **)&m->nodes, &r->cap.node, m->nnode, sizeof *m->nodes)) return oom(r);
    uint32_t idx = m->nnode;
    si_node *n = &m->nodes[idx];
    memset(n, 0, sizeof *n);
    n->dst = -1;
    n->line = r->line;
    n->opd = m->nopd;
    n->link = -1;
    n->site = ord;
    m->nnode++;
    si_name word = {0, 0};
    frame *fr = top(r);
    if (fr->op == OP_CASE && fr->state == 1 && !closing)
        return fail_at(r, r->pos, "fallthrough ends its case");
    if (closing) {
        r->pos++;
        if (take(r, " then ")) {
            if (fr->op != OP_ARM)
                return fail_at(r, r->bol + ind, "'} then' closes an arm's condition");
            n->op = OP_THEN;
            if ((st = val(r, scalar_type(SC_BOOL))) != CINT_SIR_OK || (st = expect(r, " {")) != CINT_SIR_OK)
                return st;
            m->nodes[fr->node].link = (int32_t)idx;
            fr->op = OP_THEN;
            fr->node = idx;
        } else {
            n->op = OP_CLOSE;
            if (fr->op == OP_ARM)
                return fail_at(r, r->bol + ind, "an arm's condition closes with '} then'");
            if (fr->op == OP_IF && fr->state == 0)
                return fail_at(r, r->bol + ind, "an if has an arm");
            if (fr->op == OP_LOOP && fr->state != 2)
                return fail_at(r, r->bol + ind, "a loop has while and next");
            if (r->printing)
                return fail_at(r, r->bol + ind, "a print statement ends with print.end");
            n->link = (int32_t)fr->node;
            if (fr->op != OP_CLOSE) m->nodes[fr->node].link = (int32_t)idx;
            *done = fr->op == OP_CLOSE;
            r->depth--;
        }
        if (n->site != 0) return fail_at(r, r->bol + ind, "this node has no site");
        return at_end(r);
    }
    if (take(r, "%")) {
        size_t s = r->pos - 1u;
        int is_data = peek(r) == 's';
        if (!take(r, is_data ? "s" : "t")) return fail_at(r, s, "expected %%t<N> or %%s<k>");
        uint32_t k = 0;
        if ((st = number32(r, &k)) != CINT_SIR_OK || (st = expect(r, " = ")) != CINT_SIR_OK)
            return st;
        if (is_data) {
            if (data_defined(r, k)) return fail_at(r, s, "%%s%u is defined twice", k);
            if (!grow((void **)&r->data, &r->dcap, r->ndata, sizeof *r->data)) return oom(r);
            uint64_t len = 0;
            n->op = OP_DATA;
            n->dst = (int32_t)k;
            if ((st = expect(r, "data U8[")) != CINT_SIR_OK || (st = number(r, (uint64_t)INT64_MAX, &len)) != CINT_SIR_OK ||
                (st = expect(r, "]")) != CINT_SIR_OK) {
                return st;
            }
            for (uint64_t b = 0; b < len && st == CINT_SIR_OK; b++) {
                int64_t v = 0;
                if ((st = expect(r, b ? ", " : " ")) == CINT_SIR_OK) st = value(r, SC_U8, &v);
                si_opd o = opd_of(OD_IMM, 0);
                o.value = v;
                if (st == CINT_SIR_OK) st = add_opd(r, o);
            }
            n->value = (int64_t)len;
            n->nopd = m->nopd - n->opd;
            r->data[r->ndata++] = k;
            return st != CINT_SIR_OK ? st : n->site ? fail_at(r, s, "data has no site") : at_end(r);
        }
        if (k != r->f->ntemp)
            return fail_at(r, s, "%%t%u is defined where %%t%u comes next", k, r->f->ntemp);
        if (!grow((void **)&r->temps, &r->tcap, r->f->ntemp, sizeof *r->temps)) return oom(r);
        r->temps[k] = UNKNOWN;
        n->dst = (int32_t)k;   /* counted once the node is read, so it cannot use itself */
    }
    word.at = (uint32_t)r->pos;
    while (r->pos < r->eol && (is_ident(peek(r)) || peek(r) == '.')) r->pos++;
    word.len = (uint32_t)(r->pos - word.at);
    n->word = word;
    int64_t lab = 0;
    if (name_is(t, word, "if") || name_is(t, word, "arm") || name_is(t, word, "else") || name_is(t, word, "default")) {
        uint8_t op = name_is(t, word, "if") ? OP_IF : name_is(t, word, "arm") ? OP_ARM
                     : name_is(t, word, "else") ? OP_ELSE : OP_DEFAULT;
        n->op = op;
        if ((op == OP_ARM || op == OP_ELSE) && (fr->op != OP_IF || fr->state == 2))
            return fail_at(r, word.at, "%s is inside an if, before its else", op == OP_ARM ? "arm" : "else");
        if (op == OP_ELSE && fr->state == 0) return fail_at(r, word.at, "else follows an arm");
        if (op == OP_DEFAULT && (fr->op != OP_SWITCH || fr->state == 2))
            return fail_at(r, word.at, "default is inside a switch, once");
        if (op != OP_IF && op != OP_ARM && op != OP_ELSE) {
            fr->state = 2;
        } else if (op != OP_IF) {
            fr->state = op == OP_ELSE ? 2 : 1;
        }
        st = expect(r, " {");
        if (st == CINT_SIR_OK) st = push(r, op, idx, 0);
    } else if (name_is(t, word, "loop") || name_is(t, word, "switch") || name_is(t, word, "block")) {
        n->op = name_is(t, word, "loop") ? OP_LOOP : name_is(t, word, "switch") ? OP_SWITCH : OP_BLOCK;
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = label(r, &lab)) != CINT_SIR_OK) return st;
        for (uint32_t i = 0; i < r->depth; i++) {
            if ((r->stack[i].op == OP_LOOP || r->stack[i].op == OP_SWITCH || r->stack[i].op == OP_BLOCK) &&
                r->stack[i].label == lab) {
                return fail_at(r, word.at, "L%lld names an enclosing structure already", (long long)lab);
            }
        }
        si_opd o = opd_of(OD_LABEL, 0);
        o.value = lab;
        if ((st = add_opd(r, o)) != CINT_SIR_OK) return st;
        uint8_t sc = 0;
        if (n->op == OP_SWITCH) {
            si_opd v;
            size_t a = r->pos + 1u;
            if ((st = expect(r, " ")) != CINT_SIR_OK || (st = percent(r, &v, &a)) != CINT_SIR_OK)
                return st;
            si_type vt = type_of(r, v);
            if (v.kind == OD_DATA || (vt.kind != TK_SCALAR && vt.kind != TK_UNKNOWN))
                return fail_at(r, a, "a switch takes a scalar value");
            if (vt.kind == TK_SCALAR && is_wide(vt.scalar))
                return refuse_at(r, a, "a switch over a wide integer is not run by this unit");
            sc = vt.kind == TK_SCALAR ? vt.scalar : SC_NONE;
            if ((st = add_opd(r, v)) != CINT_SIR_OK) return st;
        }
        if ((st = expect(r, " {")) == CINT_SIR_OK && (st = push(r, n->op, idx, lab)) == CINT_SIR_OK) {
            top(r)->scalar = sc;
            n->t1 = sc;
        }
    } else if (name_is(t, word, "case")) {
        n->op = OP_CASE;
        if (fr->op != OP_SWITCH || fr->state == 2)
            return fail_at(r, word.at, "case is inside a switch, before its default");
        if ((st = expect(r, " ")) == CINT_SIR_OK && (st = items(r, fr->scalar)) == CINT_SIR_OK &&
            (st = expect(r, " {")) == CINT_SIR_OK) {
            st = push(r, OP_CASE, idx, 0);
        }
    } else if (name_is(t, word, "next")) {
        n->op = OP_NEXT;
        if (fr->op != OP_LOOP || fr->state != 1)
            return fail_at(r, word.at, "next is inside a loop, after its while");
        fr->state = 2;
        m->nodes[fr->node].link = (int32_t)idx;   /* replaced by the loop's `}` at its close */
        n->link = (int32_t)fr->node;
    } else if (name_is(t, word, "while")) {
        if (fr->op != OP_LOOP || fr->state != 0)
            return fail_at(r, word.at, "while is inside a loop, once");
        fr->state = 1;
        st = node_body(r, n, word);
    } else if (name_is(t, word, "fallthrough")) {
        n->op = OP_FALLTHROUGH;
        if (fr->op != OP_CASE) return fail_at(r, word.at, "fallthrough ends a case");
        fr->state = 1;
    } else if (name_is(t, word, "break") || name_is(t, word, "continue")) {
        int cont = name_is(t, word, "continue");
        n->op = cont ? OP_CONTINUE : OP_BREAK;
        si_opd o = opd_of(OD_LABEL, 0);
        if ((st = expect(r, " ")) == CINT_SIR_OK && (st = label(r, &lab)) == CINT_SIR_OK &&
            (st = find_label(r, word.at, lab, cont)) == CINT_SIR_OK) {
            o.value = lab;
            st = add_opd(r, o);
        }
    } else {
        st = node_body(r, n, word);
    }
    if (st != CINT_SIR_OK) return st;
    /* a print statement is print.begin, its print.bytes and print.value nodes, and print.end */
    int piece = n->op == OP_PRINT_BYTES || n->op == OP_PRINT_VALUE || n->op == OP_PRINT_END;
    if (r->printing && !piece)
        return fail_at(r, word.at, "a print statement holds print.bytes and print.value only");
    if (!r->printing && piece)
        return fail_at(r, word.at, "%.*s is inside a print statement", (int)word.len, (const char *)t + word.at);
    r->printing = n->op == OP_PRINT_BEGIN || (r->printing && n->op != OP_PRINT_END);
    if (n->op != OP_CALL) n->nopd = m->nopd - n->opd;
    if ((st = pair_check(r, n, idx, word)) != CINT_SIR_OK) return st;
    if (n->dst >= 0 && n->op != OP_DATA) r->f->ntemp++;
    if (n->dst >= 0 && r->temps[n->dst].kind == TK_UNKNOWN && n->op != OP_FLOAD && n->op != OP_FILOAD &&
        n->op != OP_ELOAD && n->op != OP_LOAD_GLOBAL && n->op != OP_ILOAD) {
        return fail_at(r, r->bol + ind, "this node defines no temporary");
    }
    if (can_fault(n) && n->site == 0 && r->f->kind != FN_KERNEL) {   /* a kernel's node faults at its dispatch's */
        return fail_at(r, word.at, "a node that can fault has a site");
    }
    if (n->site != 0) {
        si_name want = m->sites[n->site].op;
        /* the operation compiled C names at the site (compiler/back_c.ci, op_name) */
        static const char *const MEMBER_SITES[] = {"arena.alloc", "arena.alloc", "arena.alloc", "arena.reset",
                                                   "pool.insert", "pool.insert", "pool.deref"};
        const char *op = n->op == OP_CALL ? "call.enter" : n->op == OP_PRINT_BEGIN ? "print"
                         : n->op == OP_VCOPY ? "copy.shape" : n->op == OP_VFILL ? "fill" : n->op == OP_VALLOC ? "decl.shape"
                         : n->op == OP_FCHARGE ? "arena.alloc" : n->op == OP_MEMBER ? MEMBER_SITES[n->sub]
                         : n->op == OP_HVIEW || n->op == OP_HLEASE ? "handle.check" : NULL;
        si_name red = {(uint32_t)n->fmt[0], (uint32_t)n->fmt[1]};   /* a kernel's reduction (kernel_body) */
        int ok = op != NULL ? name_is(t, want, op)
                 : same_name(t, want, t, n->op == OP_KRED || n->op == OP_KEPI ? red : word);
        if (!ok) {
            return fail_at(r, word.at, "site #%u names %.*s in the sites line", n->site, (int)want.len,
                           (const char *)t + want.at);
        }
    }
    return at_end(r);
}

/* ------------------------------------------------------------------ headers */

/* The type of a parameter: a scalar, a struct, `inout <S>`, or a view. */
static int param_type(rd *r, si_func *f, si_type *ty)
{
    int st;
    int io = take(r, "inout ");
    if ((st = elem_type(r, ty)) != CINT_SIR_OK) return st;
    if (io) {
        if (ty->kind != TK_STRUCT) return fail_at(r, r->pos, "inout takes a struct");
        ty->inout = 1;
        return CINT_SIR_OK;
    }
    if (!take(r, "[")) return CINT_SIR_OK;
    ty->kind = TK_VIEW;
    if (take(r, "_")) {
        ty->extent = -1;
    } else if (peek(r) == '%') {
        si_name n = {0u, 0u};
        int pre = 0;
        size_t s = r->pos++;
        if ((st = param_name(r, &n, &pre)) != CINT_SIR_OK) return st;
        const si_param *ps = f->nparam != 0 ? r->m->params + f->param : NULL;
        int found = -1;
        for (uint32_t p = 0; pre == numbered(r->t, n) && p < f->nparam; p++) {
            if (same_name(r->t, ps[p].name, r->t, n) && ps[p].ty.kind == TK_SCALAR && ps[p].ty.scalar == SC_I64)
                found = (int)p;
        }
        if (found < 0) return fail_at(r, s, "a view's extent names an earlier I64 parameter");
        ty->extent = -2 - (int64_t)found;
    } else {
        uint64_t e = 0;
        if ((st = number(r, (uint64_t)INT64_MAX, &e)) != CINT_SIR_OK) return st;
        ty->extent = (int64_t)e;
    }
    if (peek(r) == ',') {   /* a view of rank 2 to 4, `T[_, _]` (CINTC-OQ-35, item 5) */
        if (ty->extent != -1) return fail_at(r, r->pos, "a view of rank 2 to 4 is written T[_, _]");
        for (ty->rank = 1; take(r, ", "); ty->rank++) {
            if (ty->rank == 4 || (st = expect(r, "_")) != CINT_SIR_OK)
                return ty->rank == 4 ? fail_at(r, r->pos, "a view has rank 1 to 4") : st;
        }
    }
    return expect(r, "]");
}

/* `: <T>[_(, _)*]`, the type of the view of box 09 that a node writes (CINTC-OQ-35, item 5). */
static int view_type(rd *r, si_type *ty)
{
    int st = expect(r, ": ");
    if (st != CINT_SIR_OK || (st = elem_type(r, ty)) != CINT_SIR_OK || (st = expect(r, "[_")) != CINT_SIR_OK)
        return st;
    ty->kind = TK_VIEW;
    ty->extent = -1;
    for (ty->rank = 1; take(r, ", "); ty->rank++) {
        if (ty->rank == 4 || (st = expect(r, "_")) != CINT_SIR_OK)
            return ty->rank == 4 ? fail_at(r, r->pos, "a view has rank 1 to 4") : st;
    }
    return expect(r, "]");
}

/* `[export ]func @<module>.<name>(<parameters>)[ -> <T>] {`, `[export ]kernel
 * @<module>.<name>(<parameters>) {`, `script @<module> {` or `test @<module> <k> {`, at r->bol. */
static int header(rd *r, si_func *f)
{
    si_module *m = r->m;
    si_name mod = {0u, 0u};
    int st;
    memset(f, 0, sizeof *f);
    f->line = r->line;
    f->param = m->nparam;
    f->result = scalar_type(SC_NONE);
    f->eset = -1;
    r->pos = r->bol;
    f->exported = (uint8_t)take(r, "export ");
    if (take(r, "func @")) {
        f->kind = FN_FUNC;
    } else if (take(r, "kernel @")) {
        f->kind = FN_KERNEL;
    } else if (f->exported) {
        return fail_at(r, r->pos, "expected func or kernel");
    } else if (take(r, "script @")) {
        f->kind = FN_SCRIPT;
    } else if (take(r, "test @")) {
        f->kind = FN_TEST;
    } else {
        return fail_at(r, r->pos, "expected func, script or test");
    }
    size_t s = r->pos;
    if ((st = dotted(r, &mod)) != CINT_SIR_OK) return st;
    if (f->kind == FN_FUNC || f->kind == FN_KERNEL) {
        uint32_t cut = mod.len;
        while (cut > 0 && r->t[mod.at + cut - 1u] != '.') cut--;
        f->name.at = mod.at + cut;
        f->name.len = mod.len - cut;
        mod.len = cut ? cut - 1u : 0;
    }
    if (!same_name(r->t, mod, r->t, m->dotted))
        return fail_at(r, s, "a function of module %.*s", (int)m->dotted.len, (const char *)r->t + m->dotted.at);
    if (f->kind == FN_TEST) {
        uint64_t k = 0;
        if ((st = expect(r, " ")) != CINT_SIR_OK || (st = number(r, (uint64_t)INT64_MAX, &k)) != CINT_SIR_OK)
            return st;
        f->test = (int64_t)k;
    }
    if (f->kind == FN_FUNC || f->kind == FN_KERNEL) {
        if ((st = expect(r, "(")) != CINT_SIR_OK) {
            return st;
        }
        while (st == CINT_SIR_OK && peek(r) != ')') {
            si_param p;
            if (f->nparam > 0 && (st = sep(r)) != CINT_SIR_OK) return st;
            size_t a = r->pos;
            int pre = 0;
            if ((st = expect(r, "%")) != CINT_SIR_OK || (st = param_name(r, &p.name, &pre)) != CINT_SIR_OK)
                return st;
            if (pre != numbered(r->t, p.name)) {
                return pre ? fail_at(r, a, "only a parameter named t or s and digits is written %%p.<name>")
                           : fail_at(r, a, "a parameter named %.*s is written %%p.%.*s (CINTC-OQ-35, item 2)",
                                     (int)p.name.len, (const char *)r->t + p.name.at, (int)p.name.len,
                                     (const char *)r->t + p.name.at);
            }
            if ((st = expect(r, ": ")) != CINT_SIR_OK) return st;
            /* a kernel's parameter: a view of box 09 `T[_, ...]`, a scalar, or a scalar `out` */
            int out = f->kind == FN_KERNEL && take(r, "out ");
            size_t ts = r->pos;
            if ((st = param_type(r, f, &p.ty)) != CINT_SIR_OK) return st;
            if (f->kind == FN_KERNEL) {   /* a wide or fixed-point scalar or element too (box 15) */
                p.ty.out = (uint8_t)out;
                p.ty.rank = p.ty.kind == TK_VIEW && p.ty.rank == 0 ? 1u : p.ty.rank;
                if (out ? p.ty.kind != TK_SCALAR || !(is_arith(p.ty.scalar) || sc_is_q(p.ty.scalar))
                        : p.ty.kind == TK_VIEW ? p.ty.extent != -1 || p.ty.scalar == SC_NONE : p.ty.kind != TK_SCALAR) {
                    return fail_at(r, ts, "a kernel takes views T[_, ...] of scalars, scalars and scalar outs");
                }
            }
            p.cells = p.ty.kind == TK_VIEW || p.ty.inout || p.ty.out ? 0u : (uint32_t)cells(r, p.ty, 1);
            if (f->pcells + (uint64_t)p.cells > CELLS_MAX) {
                (void)fail_at(r, a, "parameters of more cells than the reader's limit");
                return CINT_SIR_RESOURCE;
            }
            f->pcells += p.cells;
            for (uint32_t i = 0; i < f->nparam; i++) {
                if (same_name(r->t, m->params[f->param + i].name, r->t, p.name))
                    return fail_at(r, a, "two parameters named %.*s", (int)p.name.len, (const char *)r->t + p.name.at);
            }
            if (!grow((void **)&m->params, &r->cap.param, m->nparam, sizeof *m->params))
                return oom(r);
            m->params[m->nparam++] = p;
            f->nparam++;
        }
        if ((st = expect(r, ")")) != CINT_SIR_OK) {
            return st;
        }
        if (f->kind == FN_KERNEL && r->pos + 4u <= r->eol && memcmp(r->t + r->pos, " -> ", 4) == 0)
            return fail_at(r, r->pos + 1u, "a kernel has no result; it publishes through its out parameters");
        if (f->kind == FN_FUNC && take(r, " -> ") && (st = elem_type(r, &f->result)) != CINT_SIR_OK)
            return st;
        if (f->result.kind == TK_SCALAR && take(r, "!")) {   /* `<tag type>!<value type>` (box 12) */
            f->etag = f->result.scalar;
            f->result = scalar_type(SC_NONE);
            if (!is_integer(f->etag) || is_signed(f->etag))
                return fail_at(r, r->pos - 1u, "an error union's tag has an unsigned type");
            if (!take(r, "void") && (st = elem_type(r, &f->result)) != CINT_SIR_OK) return st;
            if (take(r, " error E")) {   /* an entry's error set */
                si_node n;
                memset(&n, 0, sizeof n);
                if ((st = set_ref(r, &n, 1)) != CINT_SIR_OK) return st;
                f->eset = (int32_t)n.value;
                if (m->esets[n.value].scalar != f->etag)
                    return fail_at(r, r->pos, "the error set's tag type is not the result's");
            }
            /* an entry, an export or `main()`, names the set its error leaves with, and only an entry */
            int entry = f->exported || (f->nparam == 0 && f->result.kind == TK_NONE && name_is(r->t, f->name, "main"));
            if (entry != (f->eset >= 0)) {
                return fail_at(r, r->pos, entry ? "an entry's error union result names its error set, error E<k>"
                                                : "only an entry's error union result names its error set");
            }
        }
    }
    if ((st = expect(r, " {")) != CINT_SIR_OK) return st;
    return at_end(r);
}

/* ------------------------------------------------------------------ one module */

/* Moves to line `line` (from 1) that starts at `bol`. */
static void at_line(rd *r, size_t bol, uint32_t line)
{
    r->bol = r->pos = bol;
    r->line = line;
    r->eol = bol;
    while (r->eol < r->m->len && r->t[r->eol] != '\n') r->eol++;
}

static int sites_line(rd *r)
{
    si_module *m = r->m;
    int st = expect(r, "sites ");
    size_t plen = strlen(m->path);
    if (st != CINT_SIR_OK) return st;
    if (r->eol - r->pos < plen || memcmp(r->t + r->pos, m->path, plen) != 0)
        return fail_at(r, r->pos, "the sites line names the module's path, %s", m->path);
    r->pos += plen;
    if (!grow((void **)&m->sites, &r->cap.site, 0, sizeof *m->sites)) return oom(r);
    memset(&m->sites[0], 0, sizeof m->sites[0]);
    while (st == CINT_SIR_OK && take(r, " ")) {
        uint32_t o = 0;
        si_site s;
        size_t a = r->pos;
        if ((st = number32(r, &o)) != CINT_SIR_OK || (st = expect(r, ":")) != CINT_SIR_OK ||
            (st = number32(r, &s.line)) != CINT_SIR_OK || (st = expect(r, ":")) != CINT_SIR_OK ||
            (st = number32(r, &s.col)) != CINT_SIR_OK || (st = expect(r, ":")) != CINT_SIR_OK) {
            return st;
        }
        s.op.at = (uint32_t)r->pos;
        while (r->pos < r->eol && (is_ident(peek(r)) || peek(r) == '.')) r->pos++;
        s.op.len = (uint32_t)(r->pos - s.op.at);
        if (o != m->nsite + 1u)
            return fail_at(r, a, "site %u where %u comes next", o, m->nsite + 1u);
        if (s.line == 0 || s.col == 0 || s.op.len == 0)
            return fail_at(r, a, "a site has a line, a column and an operation");
        if (!grow((void **)&m->sites, &r->cap.site, o, sizeof *m->sites)) return oom(r);
        m->sites[o] = s;
        m->nsite = o;
    }
    return st == CINT_SIR_OK ? at_end(r) : st;
}

/* The cells of struct k and the first cell of each of its fields, the structs its fields
 * hold measured first; a struct that holds itself is refused. stack has a place per struct. */
static int measure(rd *r, uint32_t k, uint32_t *stack)
{
    si_module *m = r->m;
    uint32_t depth = 0;
    if (m->structs[k].state == 0) {
        m->structs[k].state = 1;
        stack[depth++] = k;
    }
    while (depth > 0) {
        uint32_t top = stack[depth - 1u];
        si_struct *ss = &m->structs[top];
        uint64_t c = 0;
        uint32_t i = 0;
        for (; i < ss->nfield; i++) {
            si_field *f = &m->fields[ss->field + i];
            si_struct *u = f->ty.scalar == SC_NONE && f->ty.smod == r->prog->nmod ? &m->structs[f->ty.sidx] : NULL;
            r->bol = ss->name.at - 7u;   /* `struct ` */
            r->line = 3u + top;
            if (u != NULL && u->state == 1)
                return fail_at(r, f->name.at, "struct %.*s holds itself", (int)u->name.len, (const char *)r->t + u->name.at);
            if (u != NULL && u->state == 0) {
                u->state = 1;
                stack[depth++] = f->ty.sidx;
                break;
            }
            f->off = (uint32_t)c;
            c += cells(r, f->ty, 1);
            if (c > CELLS_MAX) {
                (void)fail_at(r, ss->name.at, "a struct of more cells than the reader's limit");
                return CINT_SIR_RESOURCE;
            }
        }
        if (i == ss->nfield) {
            ss->cells = (uint32_t)c;
            ss->state = 2;
            depth--;
        }
    }
    return CINT_SIR_OK;
}

/* The `struct <Name>(<field>: <T>, ...)` lines from line 3, at *bol: their names first, so
 * that a field may hold the struct of a later line, then their fields and cells. */
static int struct_lines(rd *r, size_t *bol)
{
    si_module *m = r->m;
    int st = CINT_SIR_OK;
    for (size_t b = *bol; b < m->len; b = r->eol + 1u) {
        si_struct ss;
        memset(&ss, 0, sizeof ss);
        at_line(r, b, 3u + m->nstruct);
        if (!take(r, "struct ")) break;
        if ((st = ident(r, &ss.name)) != CINT_SIR_OK || (st = expect(r, "(")) != CINT_SIR_OK) {
            return st;
        }
        for (uint32_t i = 0; i < m->nstruct; i++) {
            if (same_name(r->t, m->structs[i].name, r->t, ss.name))
                return fail_at(r, ss.name.at, "a second struct of this name");
        }
        if (!grow((void **)&m->structs, &r->cap.strct, m->nstruct, sizeof *m->structs))
            return oom(r);
        m->structs[m->nstruct++] = ss;
    }
    for (uint32_t k = 0; k < m->nstruct; k++) {
        si_struct *ss = &m->structs[k];
        at_line(r, *bol, 3u + k);
        *bol = r->eol + 1u;
        r->pos = ss->name.at + ss->name.len + 1u;
        ss->field = m->nfield;
        while (st == CINT_SIR_OK && peek(r) != ')') {
            si_field f;
            memset(&f, 0, sizeof f);
            if ((ss->nfield > 0 && (st = sep(r)) != CINT_SIR_OK) || (st = ident(r, &f.name)) != CINT_SIR_OK ||
                (st = expect(r, ": ")) != CINT_SIR_OK || (st = field_type(r, &f)) != CINT_SIR_OK) {
                return st;
            }
            for (uint32_t i = 0; i < ss->nfield; i++) {
                if (same_name(r->t, m->fields[ss->field + i].name, r->t, f.name))
                    return fail_at(r, f.name.at, "a second field of this name");
            }
            if (!grow((void **)&m->fields, &r->cap.field, m->nfield, sizeof *m->fields))
                return oom(r);
            m->fields[m->nfield++] = f;
            ss->nfield++;
        }
        if ((st = expect(r, ")")) != CINT_SIR_OK || (st = at_end(r)) != CINT_SIR_OK) {
            return st;
        }
    }
    uint32_t *stack = malloc(((size_t)m->nstruct + 1u) * sizeof *stack);
    if (stack == NULL) return oom(r);
    for (uint32_t k = 0; st == CINT_SIR_OK && k < m->nstruct; k++) st = measure(r, k, stack);
    free(stack);
    return st;
}

/* After `arena @` or `pool @` (box 12; compiler/OPEN.md CINTC-OQ-64, item 10): `<name>:
 * <element>, <capacity>`, an element a scalar or a struct and a capacity of at most 2^31 - 1
 * elements (item 12). */
static int mem_line(rd *r, unsigned mem)
{
    si_module *m = r->m;
    si_mem d;
    uint64_t cap = 0;
    memset(&d, 0, sizeof d);
    d.line = r->line;
    int st = ident(r, &d.name);
    if (st != CINT_SIR_OK || (st = expect(r, ": ")) != CINT_SIR_OK || (st = elem_type(r, &d.ty)) != CINT_SIR_OK)
        return st;
    if (d.ty.kind != TK_SCALAR && d.ty.kind != TK_STRUCT)
        return fail_at(r, r->pos, "an arena or a pool holds scalars or structs");
    if ((st = expect(r, ", ")) != CINT_SIR_OK || (st = number(r, INT32_MAX, &cap)) != CINT_SIR_OK)
        return st;
    d.ty.kind = TK_MEM;
    d.ty.mem = (uint8_t)mem;
    d.capacity = (int64_t)cap;
    d.id = 1;   /* M-13: from 1 per kind, in declaration order */
    for (uint32_t i = 0; i < m->nmem; i++) {
        if (same_name(r->t, m->mems[i].name, r->t, d.name))
            return fail_at(r, d.name.at, "a second arena or pool of this name");
        d.id += m->mems[i].ty.mem == mem;
    }
    if (!grow((void **)&m->mems, &r->cap.mem, m->nmem, sizeof *m->mems)) return oom(r);
    m->mems[m->nmem++] = d;
    return at_end(r);
}

/* After `widen W` or `error E` (box 12; compiler/OPEN.md CINTC-OQ-63, item 1): a conversion
 * between error sets, `<k>: <last> <adds>, ...`, each run's last tag and what it adds; or with
 * set an error set, `<k>: <name> <type> <value>, ...`, its values' names in tag order. */
static int error_line(rd *r, int set)
{
    si_module *m = r->m;
    uint64_t k = 0, n = 0;
    int st = number(r, (uint64_t)INT64_MAX, &k);
    if (st != CINT_SIR_OK || (st = expect(r, set ? ": " : ":")) != CINT_SIR_OK) return st;
    if (named(r, set, k) >= 0)
        return fail_at(r, r->bol, "a second %c%llu", set ? 'E' : 'W', (unsigned long long)k);
    if (!set) {
        si_widen w;
        w.k = (int64_t)k;
        w.run = m->nrun;
        w.nrun = 0;
        do {
            uint64_t last = 0;
            int64_t adds = 0;
            if ((st = expect(r, " ")) != CINT_SIR_OK || (st = number(r, UINT64_MAX, &last)) != CINT_SIR_OK ||
                (st = expect(r, " ")) != CINT_SIR_OK || (st = signed_number(r, &adds)) != CINT_SIR_OK) {
                return st;
            }
            if (!grow((void **)&m->runs, &r->cap.run, m->nrun + 1u, sizeof *m->runs)) return oom(r);
            m->runs[m->nrun++] = (int64_t)last;
            m->runs[m->nrun++] = adds;
            w.nrun++;
        } while (take(r, ","));
        if (!grow((void **)&m->widens, &r->cap.widen, m->nwiden, sizeof *m->widens)) return oom(r);
        m->widens[m->nwiden++] = w;
        return at_end(r);
    }
    si_eset e;
    int sc = 0;
    memset(&e, 0, sizeof e);
    e.k = (int64_t)k;
    e.value = m->neval;
    if ((st = dotted(r, &e.name)) != CINT_SIR_OK || (st = expect(r, " ")) != CINT_SIR_OK ||
        (st = scalar_name(r, &sc)) != CINT_SIR_OK) {
        return st;
    }
    if (!is_integer(sc) || is_signed(sc))
        return fail_at(r, r->pos, "an error set's tag has an unsigned type");
    e.scalar = (uint8_t)sc;
    do {
        if ((st = expect(r, " ")) != CINT_SIR_OK) return st;
        if (!grow((void **)&m->evals, &r->cap.eval, m->neval, sizeof *m->evals)) return oom(r);
        if ((st = dotted(r, &m->evals[m->neval])) != CINT_SIR_OK) return st;
        m->neval++;
        n++;
    } while (take(r, ","));
    if (width_of(sc) < 64u && n > (UINT64_MAX >> (64u - width_of(sc))))
        return fail_at(r, r->bol, "more values than tags of %s", SCALAR_NAMES[sc]);
    e.nvalue = (uint32_t)n;
    if (!grow((void **)&m->esets, &r->cap.eset, m->neset, sizeof *m->esets)) return oom(r);
    m->esets[m->neset++] = e;
    return at_end(r);
}

/* The module whose text is in r->m: its lines, functions and nodes. */
static int read_module(rd *r)
{
    si_module *m = r->m;
    const uint8_t *t = m->text;
    int st;
    for (size_t i = 0, bol = 0, line = 1; i < m->len; i++) {
        if (t[i] != '\n' && (t[i] < 0x20 || t[i] > 0x7E)) {
            r->bol = bol;
            r->line = (uint32_t)line;
            return fail_at(r, i, "byte 0x%02x is not printable ASCII or LF", (unsigned)t[i]);
        }
        if (t[i] == '\n' && i == bol) {
            r->bol = bol;
            r->line = (uint32_t)line;
            return fail_line(r, "an empty line");
        }
        if (t[i] == '\n') {
            bol = i + 1u;
            line++;
        }
    }
    if (m->len == 0 || t[m->len - 1u] != '\n')
        return fail_file(r->err, m->file, CINT_SIR_INPUT, "the text ends with LF");
    r->t = t;
    at_line(r, 0, 1);
    if ((st = expect(r, "module ")) != CINT_SIR_OK || (st = dotted(r, &m->dotted)) != CINT_SIR_OK ||
        (st = at_end(r)) != CINT_SIR_OK) {
        return st;
    }
    /* the dotted name is the path without .ci, '/' read as '.' */
    size_t plen = strlen(m->path) - 3u;
    int same = m->dotted.len == plen;
    for (size_t i = 0; same && i < plen; i++)
        same = t[m->dotted.at + i] == (m->path[i] == '/' ? '.' : (uint8_t)m->path[i]);
    if (!same) return fail_at(r, m->dotted.at, "module of path %s", m->path);
    if (r->eol + 1u >= m->len)
        return fail_file(r->err, m->file, CINT_SIR_INPUT, "the text has a sites line");
    at_line(r, r->eol + 1u, 2);
    if ((st = sites_line(r)) != CINT_SIR_OK) return st;
    r->next_site = 1;
    size_t first = r->eol + 1u;
    if ((st = struct_lines(r, &first)) != CINT_SIR_OK) return st;
    /* the headers first, so that a call may name a function later in the text */
    uint32_t charges = 0;
    for (size_t b = first, line = 3u + m->nstruct; b < m->len; line++) {
        at_line(r, b, (uint32_t)line);
        b = r->eol + 1u;
        if (t[r->bol] == ' ') {
            while (peek(r) == ' ') r->pos++;
            charges += (uint32_t)take(r, "frame.charge ");
            continue;
        }
        if (t[r->bol] == '}' || take(r, "global ") || take(r, "struct ")) {
            continue;
        }
        if (take(r, "widen W") || take(r, "error E")) {   /* box 12's conversions and error sets */
            if (m->nfunc > 0)
                return fail_line(r, "widen and error lines come before the functions");
            if ((st = error_line(r, t[r->bol] == 'e')) != CINT_SIR_OK) return st;
            continue;
        }
        if (take(r, "arena @") || take(r, "pool @")) {   /* box 12's arenas and pools */
            if (m->nfunc > 0) return fail_line(r, "arena and pool lines come before the functions");
            if ((st = mem_line(r, t[r->bol] == 'a' ? CINT_HANDLE_ARENA : CINT_HANDLE_POOL)) != CINT_SIR_OK)
                return st;
            continue;
        }
        if (!grow((void **)&m->funcs, &r->cap.func, m->nfunc, sizeof *m->funcs)) return oom(r);
        if ((st = header(r, &m->funcs[m->nfunc])) != CINT_SIR_OK) return st;
        for (uint32_t i = 0; i < m->nfunc; i++) {
            const si_func *g = &m->funcs[i], *h = &m->funcs[m->nfunc];
            int named = (g->kind == FN_FUNC || g->kind == FN_KERNEL) && (h->kind == FN_FUNC || h->kind == FN_KERNEL);
            if ((g->kind == h->kind && (g->kind == FN_SCRIPT || (g->kind == FN_TEST && g->test == h->test))) ||
                (named && same_name(t, g->name, t, h->name))) {
                return fail_line(r, "a second function of this name");
            }
        }
        m->nfunc++;
    }
    if (charges > m->nsite) {
        return fail_file(r->err, m->file, CINT_SIR_INPUT, "the sites line lists %u sites; the frame.charge nodes take %u",
                         m->nsite, charges);
    }
    r->frame_site0 = r->next_frame = m->nsite + 1u - charges;
    uint32_t fn = 0;
    int in_globals = 1;
    size_t b = first;
    for (uint32_t line = 3u + m->nstruct; b < m->len; line++) {
        at_line(r, b, line);
        b = r->eol + 1u;
        if (in_globals && take(r, "global @")) {
            si_global g;
            si_type ty = scalar_type(SC_NONE);
            uint64_t e = 0;
            memset(&g, 0, sizeof g);
            g.line = line;
            g.extent = -1;
            g.item = m->nitem;
            if ((st = ident(r, &g.name)) != CINT_SIR_OK || (st = expect(r, ": ")) != CINT_SIR_OK)
                return st;
            size_t ta = r->pos;
            if (take(r, "Handle")) {   /* a Handle (OQ-242): the null handle */
                g.handle = 1;
                st = expect(r, " = 0");
            } else if ((st = elem_type(r, &ty)) != CINT_SIR_OK) {
                return st;
            } else if (ty.kind == TK_STRUCT) {
                /* a module-level struct (box 15, OQ-260): zero-filled, or a value per scalar, in
                 * payload order, GLOBAL_MAX cells at most; a wide scalar takes a cell per limb */
                uint64_t c = cells(r, ty, 1);
                if (c == 0 || c > GLOBAL_MAX)
                    return fail_at(r, ta, "a module-level struct of no cells or more than %u", GLOBAL_MAX);
                for (uint64_t i = 0; i < c; i += limbs_of(cell_scalar(r, ty, i))) {
                    if (cell_scalar(r, ty, i) == SC_NONE)
                        return fail_at(r, ta, "a module-level struct holds integers and Bools, alone, in structs or in arrays");
                }
                g.record = 1;
                g.smod = ty.smod;
                g.sidx = ty.sidx;
                g.cells = (uint32_t)c;
                if (take(r, " = [")) {   /* a wide scalar's limbs, each an item (box 15) */
                    for (uint64_t i = 0; i < c; i += limbs_of(cell_scalar(r, ty, i))) {
                        int sc = cell_scalar(r, ty, i);
                        int64_t v = 0;
                        uint32_t lead = 0;
                        if ((i > 0 && (st = sep(r)) != CINT_SIR_OK) ||
                            (st = is_wide(sc) ? wide_item(r, sc, &lead) : value(r, sc, &v)) != CINT_SIR_OK)
                            return st;
                        if (is_wide(sc)) continue;
                        if (!grow((void **)&m->items, &r->cap.item, m->nitem, sizeof *m->items))
                            return oom(r);
                        m->items[m->nitem++] = v;
                    }
                    g.nitem = (uint32_t)c;
                    st = expect(r, "]");
                }
            } else if (ty.kind != TK_SCALAR) {
                return fail_at(r, ta, "a module variable holds a scalar, a Handle, an array of scalars or a struct");
            } else if (take(r, "[")) {   /* a module-level array (box 13 MLA): zero-filled, or its items */
                size_t at = r->pos;
                g.extent = 1;
                do {   /* rank 1 to 4, row-major: the items are its elements, GLOBAL_MAX at most */
                    if ((st = number(r, GLOBAL_MAX, &e)) != CINT_SIR_OK) return st;
                    g.dims[g.rank++] = (int64_t)e;
                    g.extent *= (int64_t)e;
                    if (g.extent > (int64_t)GLOBAL_MAX)
                        return fail_at(r, at, "a module-level array of more than %u elements", GLOBAL_MAX);
                } while (g.rank < 4u && take(r, ", "));
                if ((st = expect(r, "]")) != CINT_SIR_OK) return st;
                e = (uint64_t)g.extent;
                if (take(r, " = [")) {   /* a wide element's limbs, each an item (box 15) */
                    for (uint64_t i = 0; i < e; i++) {
                        int64_t v = 0;
                        uint32_t lead = 0;
                        if ((i > 0 && (st = sep(r)) != CINT_SIR_OK) ||
                            (st = is_wide(ty.scalar) ? wide_item(r, ty.scalar, &lead) : value(r, ty.scalar, &v)) != CINT_SIR_OK)
                            return st;
                        if (is_wide(ty.scalar)) continue;
                        if (!grow((void **)&m->items, &r->cap.item, m->nitem, sizeof *m->items))
                            return oom(r);
                        m->items[m->nitem++] = v;
                    }
                    g.nitem = (uint32_t)(e * limbs_of(ty.scalar));
                    st = expect(r, "]");
                }
            } else if ((st = expect(r, " = ")) == CINT_SIR_OK && is_wide(ty.scalar)) {
                st = wide_item(r, ty.scalar, &g.item);   /* its limbs */
                g.nitem = limbs_of(ty.scalar);
            } else if (st == CINT_SIR_OK) {
                st = value(r, ty.scalar, &g.value);
            }
            if (st != CINT_SIR_OK || (st = at_end(r)) != CINT_SIR_OK) return st;
            for (uint32_t i = 0; i < m->nglobal; i++) {
                if (same_name(t, m->globals[i].name, t, g.name))
                    return fail_at(r, g.name.at, "a second global of this name");
            }
            g.scalar = ty.scalar;
            if (!grow((void **)&m->globals, &r->cap.global, m->nglobal, sizeof *m->globals))
                return oom(r);
            m->globals[m->nglobal++] = g;
            continue;
        }
        in_globals = 0;
        if (t[r->bol] == ' ' || t[r->bol] == '}')
            return fail_at(r, r->bol, "a node outside a function");
        if (take(r, "global ") || take(r, "struct "))
            return fail_line(r, "struct and global lines come before the functions");
        if (take(r, "widen W") || take(r, "error E") || take(r, "arena @") || take(r, "pool @")) {   /* read above */
            continue;
        }
        /* a function: its header was read above */
        if (fn >= m->nfunc || m->funcs[fn].line != line)
            return fail_line(r, "a function header the first pass did not read");
        si_func *f = &m->funcs[fn++];
        r->f = f;
        f->first = m->nnode;
        f->ntemp = 0;
        f->nslot = 0;
        r->ncheck = 0;
        r->ndata = 0;
        r->depth = 0;
        r->printing = 0;
        r->charges = 0;
        if ((st = push(r, OP_CLOSE, m->nnode, -1)) != CINT_SIR_OK) return st;
        int done = 0;
        while (!done) {
            if (b >= m->len) {
                return fail_file(r->err, m->file, CINT_SIR_INPUT, "the function of line %u has no closing brace",
                                 f->line);
            }
            at_line(r, b, ++line);
            b = r->eol + 1u;
            if ((st = body_line(r, &done)) != CINT_SIR_OK) return st;
        }
        f->count = m->nnode - f->first;
        if (r->charges && !(f->frame & 1u)) {
            return fail_file(r->err, m->file, CINT_SIR_INPUT,
                             "the function of line %u gives back charges and has no frame.charge", f->line);
        }
    }
    if (r->next_site != r->frame_site0 || r->next_frame != m->nsite + 1u) {
        return fail_file(r->err, m->file, CINT_SIR_INPUT, "the sites line lists %u sites; the nodes carry %u",
                         m->nsite, r->next_site - 1u + r->next_frame - r->frame_site0);
    }
    return CINT_SIR_OK;
}

/* ------------------------------------------------------------------ files and the program */

static void free_module(si_module *m)
{
    free(m->path);
    free(m->file);
    free(m->text);
    free(m->sites);
    free(m->globals);
    free(m->items);
    free(m->funcs);
    free(m->nodes);
    free(m->opds);
    free(m->params);
    free(m->structs);
    free(m->fields);
    free(m->esets);
    free(m->evals);
    free(m->widens);
    free(m->runs);
    free(m->mems);
}

void cint_sir_free(cint_sir_program *p)
{
    if (p == NULL) return;
    for (uint32_t i = 0; i < p->nmod; i++) free_module(&p->mods[i]);
    free(p->mods);
    free(p);
}

static char *join(const char *dir, const char *name)
{
    size_t a = strlen(dir), b = strlen(name);
    char *s = malloc(a + b + 2u);
    if (s != NULL) {
        memcpy(s, dir, a);
        s[a] = '/';
        memcpy(s + a + 1u, name, b + 1u);
    }
    return s;
}

static char *copy_text(const char *s, size_t n)
{
    char *c = malloc(n + 1u);
    if (c != NULL) {
        memcpy(c, s, n);
        c[n] = '\0';
    }
    return c;
}

/* The whole of file `name` in `dir`. */
static int read_file(const char *dir, const char *name, uint8_t **out, size_t *len, cint_sir_error *err)
{
    char *path = join(dir, name);
    if (path == NULL) return fail_file(err, name, CINT_SIR_RESOURCE, "out of memory");
    FILE *fp = fopen(path, "rb");
    free(path);
    if (fp == NULL) return fail_file(err, name, CINT_SIR_RESOURCE, "cannot be opened");
    size_t cap = 4096, n = 0;
    uint8_t *buf = malloc(cap);
    int st = buf == NULL ? CINT_SIR_RESOURCE : CINT_SIR_OK;
    while (st == CINT_SIR_OK) {
        if (n == cap) {
            uint8_t *q = cap >= TEXT_MAX ? NULL : realloc(buf, cap * 2u);
            if (q == NULL) {
                st = fail_file(err, name, cap >= TEXT_MAX ? CINT_SIR_INPUT : CINT_SIR_RESOURCE,
                               cap >= TEXT_MAX ? "larger than the reader's limit" : "out of memory");
                break;
            }
            buf = q;
            cap *= 2u;
        }
        size_t got = fread(buf + n, 1, cap - n, fp);
        n += got;
        if (got == 0) {
            if (ferror(fp)) st = fail_file(err, name, CINT_SIR_RESOURCE, "cannot be read");
            break;
        }
    }
    fclose(fp);
    if (st != CINT_SIR_OK) {
        free(buf);
        return st == CINT_SIR_RESOURCE && err->text[0] == '\0' ? fail_file(err, name, st, "out of memory") : st;
    }
    *out = buf;
    *len = n;
    return CINT_SIR_OK;
}

/* A module path: identifier segments joined by '/', ending .ci (SPEC-09 DIAG-01). */
static int valid_path(const char *p, size_t n)
{
    if (n < 4u || memcmp(p + n - 3u, ".ci", 3) != 0) return 0;
    size_t seg = 0;
    for (size_t i = 0; i < n - 3u; i++) {
        if (p[i] == '/') {
            if (seg == 0) return 0;
            seg = 0;
        } else if (!(is_ident(p[i]) && (seg > 0 || is_ident_start(p[i])))) {
            return 0;
        } else {
            seg++;
        }
    }
    return seg > 0;
}

static void le(uint8_t *b, uint64_t v, unsigned n)
{
    for (unsigned k = 0; k < n; k++) b[k] = (uint8_t)(v >> (8u * k));
}

static int all_digits(const uint8_t *t, size_t s, size_t e)
{
    if (e <= s) return 0;
    for (size_t i = s; i < e; i++)
        if (!is_digit(t[i])) return 0;
    return 1;
}

/* The end of line [s, e) without its site annotation: k when the line ends
 * `#<digits> @<path>:<digits>:<digits>`, the annotation starting at the space k, else e
 * (compiler/back_c.ci, site_cut, which matches the text, not the node). */
static size_t site_cut(const uint8_t *t, size_t s, size_t e)
{
    size_t k = e;
    while (k > s && t[k - 1u] != ' ') k--;
    if (k == s) return e;
    k--;
    if (k < s + 2u || k + 1u >= e || t[k + 1u] != '@') return e;
    size_t c2 = e - 1u;
    while (c2 > k + 1u && t[c2] != ':') c2--;
    size_t c1 = c2 - 1u;
    while (c1 > k + 1u && t[c1] != ':') c1--;
    if (c1 <= k + 2u || !all_digits(t, c2 + 1u, e) || !all_digits(t, c1 + 1u, c2)) return e;
    size_t h = k - 1u;
    while (h > s && is_digit(t[h])) h--;
    if (h == k - 1u || t[h] != '#') return e;
    return k;
}

/* SIR-17 (compiler/back_c.ci, sir17_encode and revision_add): SHA-256 over the domain,
 * the profile, the runtime contract and the module count, then each module in byte order
 * of path: its path and its text without the sites line and without ` @<path>:<line>:<col>`
 * at the end of a node line. */
static int revision(cint_sir_program *p, cint_sir_error *err)
{
    static const char *const head[] = {"cint-core-1/revision/v1", "cint-core-1", CINT_RT_CONTRACT};
    size_t total = 4u;
    for (int k = 0; k < 3; k++) total += 4u + strlen(head[k]);
    for (uint32_t i = 0; i < p->nmod; i++) total += 12u + strlen(p->mods[i].path) + p->mods[i].len;
    uint8_t *buf = malloc(total);
    uint32_t *order = malloc((size_t)p->nmod * sizeof *order);
    if (buf == NULL || order == NULL) {
        free(buf);
        free(order);
        return fail_file(err, "cint-program.sites", CINT_SIR_RESOURCE, "out of memory");
    }
    size_t n = 0;
    for (int k = 0; k < 3; k++) {
        size_t l = strlen(head[k]);
        le(buf + n, l, 4u);
        memcpy(buf + n + 4u, head[k], l);
        n += 4u + l;
    }
    le(buf + n, p->nmod, 4u);
    n += 4u;
    for (uint32_t i = 0; i < p->nmod; i++) {
        uint32_t j = i;
        order[i] = i;
        while (j > 0 && strcmp(p->mods[order[j - 1u]].path, p->mods[i].path) > 0) {
            order[j] = order[j - 1u];
            j--;
        }
        order[j] = i;
    }
    for (uint32_t i = 0; i < p->nmod; i++) {
        const si_module *m = &p->mods[order[i]];
        size_t l = strlen(m->path), at = n + 12u + l, b = 0;
        uint32_t line = 0;
        le(buf + n, l, 4u);
        memcpy(buf + n + 4u, m->path, l);
        while (b < m->len) {
            size_t e = b;
            while (e < m->len && m->text[e] != '\n') e++;
            if (line != 1) {
                size_t cut = site_cut(m->text, b, e);
                memcpy(buf + at, m->text + b, cut - b);
                at += cut - b;
                if (e < m->len) buf[at++] = '\n';
            }
            line++;
            b = e + 1u;
        }
        le(buf + n + 4u + l, (uint64_t)(at - (n + 12u + l)), 8u);
        n = at;
    }
    cint_sha256(buf, n, p->revision);
    free(buf);
    free(order);
    return CINT_SIR_OK;
}

/* Module k's text from mods (copied, as read_file would read it), or file `name` in dir. */
static int fetch(const char *dir, const cint_sir_text *mods, uint32_t k, const char *name, uint8_t **out, size_t *len,
                 cint_sir_error *err)
{
    if (dir != NULL) return read_file(dir, name, out, len, err);
    if (mods[k].len > TEXT_MAX)
        return fail_file(err, name, CINT_SIR_INPUT, "larger than the reader's limit");
    if ((*out = malloc(mods[k].len + 1u)) == NULL)
        return fail_file(err, name, CINT_SIR_RESOURCE, "out of memory");
    if (mods[k].len > 0u) memcpy(*out, mods[k].text, mods[k].len);
    *len = mods[k].len;
    return CINT_SIR_OK;
}

/* The program in dir, or in mods (count modules) when dir is NULL: its program file is
 * then `module <k> <path>` for each, in order. */
static int load(const char *dir, const cint_sir_text *mods, uint32_t count, cint_sir_program **out,
                cint_sir_error *err)
{
    uint8_t *pf = NULL;
    size_t plen = 0;
    int st = CINT_SIR_OK;
    memset(err, 0, sizeof *err);
    *out = NULL;
    if (dir != NULL) {
        st = read_file(dir, "cint-program.sites", &pf, &plen, err);
    } else {
        size_t cap = 1u;
        for (uint32_t k = 0; k < count; k++) cap += mods[k].path_len + 24u;
        if ((pf = malloc(cap)) == NULL)
            return fail_file(err, "cint-program.sites", CINT_SIR_RESOURCE, "out of memory");
        for (uint32_t k = 0; k < count; k++) {
            plen += (size_t)snprintf((char *)pf + plen, cap - plen, "module %u %.*s\n", k,
                                     (int)(mods[k].path_len < 4096u ? mods[k].path_len : 4096u), mods[k].path);
        }
    }
    if (st != CINT_SIR_OK) return st;
    cint_sir_program *p = calloc(1, sizeof *p);
    uint32_t mcap = 0;
    if (p == NULL) {
        free(pf);
        return fail_file(err, "cint-program.sites", CINT_SIR_RESOURCE, "out of memory");
    }
    /* `module <k> <path>` per module, k from 0 */
    for (size_t b = 0, line = 1; st == CINT_SIR_OK && b < plen; line++) {
        size_t e = b;
        while (e < plen && pf[e] != '\n') e++;
        char want[32];
        snprintf(want, sizeof want, "module %u ", p->nmod);
        size_t w = strlen(want);
        if (e == plen || e - b <= w || memcmp(pf + b, want, w) != 0 || !valid_path((const char *)pf + b + w, e - b - w)) {
            snprintf(err->path, sizeof err->path, "cint-program.sites");
            err->line = (uint32_t)line;
            err->col = 1;
            snprintf(err->text, sizeof err->text, "expected 'module %u <path>' ending with LF", p->nmod);
            st = CINT_SIR_INPUT;
            break;
        }
        if (!grow((void **)&p->mods, &mcap, p->nmod, sizeof *p->mods)) {
            st = fail_file(err, "cint-program.sites", CINT_SIR_RESOURCE, "out of memory");
            break;
        }
        si_module *m = &p->mods[p->nmod];
        memset(m, 0, sizeof *m);
        p->nmod++;
        size_t n = e - b - w;
        m->path = copy_text((const char *)pf + b + w, n);
        m->file = malloc(n + 4u);
        if (m->path == NULL || m->file == NULL) {
            st = fail_file(err, "cint-program.sites", CINT_SIR_RESOURCE, "out of memory");
            break;
        }
        memcpy(m->file, m->path, n - 3u);
        memcpy(m->file + n - 3u, ".sites", 7);
        for (uint32_t i = 0; i + 1u < p->nmod; i++) {
            if (strcmp(p->mods[i].path, m->path) == 0)
                st = fail_file(err, "cint-program.sites", CINT_SIR_INPUT, "module %s is listed twice", m->path);
        }
        b = e + 1u;
    }
    free(pf);
    if (st == CINT_SIR_OK && p->nmod == 0)
        st = fail_file(err, "cint-program.sites", CINT_SIR_INPUT, "the program has no module");
    for (uint32_t i = 0; st == CINT_SIR_OK && i < p->nmod; i++) {
        si_module *m = &p->mods[i];
        rd r;
        memset(&r, 0, sizeof r);
        if ((st = fetch(dir, mods, i, m->file, &m->text, &m->len, err)) != CINT_SIR_OK) break;
        cint_sir_program before = *p;
        before.nmod = i;
        r.prog = &before;
        r.m = m;
        r.err = err;
        st = read_module(&r);
        free(r.temps);
        free(r.slots);
        free(r.data);
        free(r.stack);
        free(r.marks);
        free(r.mparts);
        free(r.checks);
    }
    if (st == CINT_SIR_OK) st = revision(p, err);
    if (st != CINT_SIR_OK) {
        cint_sir_free(p);
        return st;
    }
    *out = p;
    return CINT_SIR_OK;
}

int cint_sir_load(const char *dir, cint_sir_program **out, cint_sir_error *err)
{
    return load(dir, NULL, 0u, out, err);
}

int cint_sir_load_text(const cint_sir_text *mods, uint32_t count, cint_sir_program **out, cint_sir_error *err)
{
    return load(NULL, mods, count, out, err);
}

void cint_sir_text_of(const cint_sir_program *p, uint32_t k, cint_sir_text *out)
{
    out->path = p->mods[k].path;
    out->path_len = strlen(p->mods[k].path);
    out->text = p->mods[k].text;
    out->len = p->mods[k].len;
}

void cint_sir_revision(const cint_sir_program *p, uint8_t out[32])
{
    memcpy(out, p->revision, 32);
}

void cint_sir_counts(const cint_sir_program *p, uint32_t *modules, uint32_t *functions, uint32_t *nodes)
{
    uint32_t f = 0, n = 0;
    for (uint32_t i = 0; i < p->nmod; i++) {
        f += p->mods[i].nfunc;
        n += p->mods[i].nnode;
    }
    *modules = p->nmod;
    *functions = f;
    *nodes = n;
}
