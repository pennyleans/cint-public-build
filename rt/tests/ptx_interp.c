/* ptx_interp.c: runs the PTX subset compiler/back_ptx.ci writes (ptx_interp.h).
 *
 * A module is parsed once into entries of decoded instructions over 64-bit registers (32-bit
 * and predicate registers live in the same file, as values of their width). Every global
 * access goes through the driver's translate callback, so a work-item that reaches outside
 * the memory the host mapped or allocated fails the launch, as an illegal address does on a
 * device. An instruction or operand outside the subset is a load error naming the line, so a
 * new instruction in back_ptx.ci surfaces here before it reaches a GPU. Division by zero
 * gives all ones (PTX leaves it unspecified; back_ptx.ci tests divisors first). No binary
 * floating-point type and no 128-bit integer type appear. */
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS /* sscanf and strcpy over bounded buffers */
#endif

#include "ptx_interp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE 512
#define MAX_PARAMS 64
#define STEP_LIMIT 100000000LL

enum { O_REG, O_IMM, O_PARAM, O_MEM, O_LABEL, O_CTAID, O_NTID, O_TID };

typedef struct operand {
    int kind;
    int reg;      /* O_REG, O_MEM: the register; O_PARAM: the parameter's byte offset */
    uint64_t imm; /* O_IMM; O_PARAM: the offset inside the parameter; O_LABEL: the target */
} operand;

enum {
    I_MOV, I_MOV32, I_LDP, I_LD, I_ST, I_CVT, I_SETP, I_SELP, I_ADD, I_SUB, I_MULLO, I_MULHI_S, I_MULHI_U,
    I_MULWIDE, I_MAD, I_DIV, I_REM, I_MIN, I_ABS, I_NEG, I_XOR, I_AND, I_SHR, I_PAND, I_POR, I_PNOT, I_BRA,
    I_RET, I_ATOMMIN
};
enum { C_EQ, C_NE, C_LT, C_LE, C_GT, C_GE };

typedef struct insn {
    int op;
    int guard;        /* predicate register, or -1 */
    int negate;
    int bytes;        /* I_LD, I_ST, I_CVT: the width of the type; I_SETP: the comparison */
    int is_signed;
    int n;
    operand a[4];
} insn;

struct ptx_entry {
    struct ptx_entry *next;
    char name[128];
    int nparams;
    int param_off[MAX_PARAMS];
    int param_size[MAX_PARAMS];
    int param_bytes;
    char param_name[MAX_PARAMS][32];
    int nregs;
    char (*reg_name)[16];
    int ninsn;
    insn *code;
};

struct ptx_module {
    ptx_entry *entries;
};

typedef struct parse_state {
    ptx_entry *e;
    char *log;
    size_t cap;
    char labels[1024][24];
    int label_at[1024];
    int nlabels;
} parse_state;

static int fail(parse_state *ps, const char *what, const char *line)
{
    snprintf(ps->log, ps->cap, "ptx_interp: %s: %s", what, line);
    return 0;
}

static int reg_index(ptx_entry *e, const char *name)
{
    int i;
    for (i = 0; i < e->nregs; i++) {
        if (strcmp(e->reg_name[i], name) == 0) {
            return i;
        }
    }
    if (strlen(name) >= sizeof e->reg_name[0]) {
        return -1;
    }
    if ((e->nregs & 255) == 0) {
        char(*grown)[16] = realloc(e->reg_name, (size_t)(e->nregs + 256) * sizeof e->reg_name[0]);
        if (grown == NULL) {
            return -1;
        }
        e->reg_name = grown;
    }
    strcpy(e->reg_name[e->nregs], name);
    return e->nregs++;
}

static int parse_number(const char *s, uint64_t *out)
{
    uint64_t v = 0u;
    int neg = 0, digits = 0;
    if (*s == '-') {
        neg = 1;
        s++;
    }
    if (s[0] == '0' && s[1] == 'x') {
        for (s += 2; *s != '\0'; s++, digits++) {
            int d = *s >= '0' && *s <= '9' ? *s - '0' : *s >= 'a' && *s <= 'f' ? *s - 'a' + 10 : -1;
            if (d < 0) {
                return 0;
            }
            v = (v << 4) | (uint64_t)d;
        }
    } else {
        for (; *s != '\0'; s++, digits++) {
            if (*s < '0' || *s > '9') {
                return 0;
            }
            v = v * 10u + (uint64_t)(*s - '0');
        }
    }
    *out = neg ? 0u - v : v;
    return digits > 0;
}

static int parse_operand(parse_state *ps, char *s, operand *o, int is_branch)
{
    ptx_entry *e = ps->e;
    size_t n;
    while (*s == ' ') {
        s++;
    }
    n = strlen(s);
    while (n > 0u && s[n - 1u] == ' ') {
        s[--n] = '\0';
    }
    if (is_branch) {
        int i;
        for (i = 0; i < ps->nlabels; i++) {
            if (strcmp(ps->labels[i], s) == 0) {
                o->kind = O_LABEL;
                o->imm = (uint64_t)ps->label_at[i];
                return 1;
            }
        }
        return 0;
    }
    if (strcmp(s, "%ctaid.x") == 0 || strcmp(s, "%ntid.x") == 0 || strcmp(s, "%tid.x") == 0) {
        o->kind = s[1] == 'c' ? O_CTAID : s[1] == 'n' ? O_NTID : O_TID;
        return 1;
    }
    if (s[0] == '%') {
        o->kind = O_REG;
        return (o->reg = reg_index(e, s)) >= 0;
    }
    if (s[0] == '[' && n > 2u && s[n - 1u] == ']') {
        char inner[48];
        char *plus;
        int i;
        if (n - 2u >= sizeof inner) {
            return 0;
        }
        memcpy(inner, s + 1, n - 2u);
        inner[n - 2u] = '\0';
        if (inner[0] == '%') {
            o->kind = O_MEM;
            return (o->reg = reg_index(e, inner)) >= 0;
        }
        o->imm = 0u;
        if ((plus = strchr(inner, '+')) != NULL) {
            *plus = '\0';
            if (!parse_number(plus + 1, &o->imm)) {
                return 0;
            }
        }
        for (i = 0; i < e->nparams; i++) {
            if (strcmp(e->param_name[i], inner) == 0) {
                o->kind = O_PARAM;
                o->reg = e->param_off[i];
                return o->imm + 8u <= (uint64_t)e->param_size[i];
            }
        }
        return 0;
    }
    o->kind = O_IMM;
    return parse_number(s, &o->imm);
}

/* The width in bytes and signedness of a type suffix: s8 to u64, b64. */
static int type_of(const char *t, int *bytes, int *is_signed)
{
    static const char *const names[] = {"s8", "u8", "s16", "u16", "s32", "u32", "s64", "u64", "b64"};
    static const int widths[] = {1, 1, 2, 2, 4, 4, 8, 8, 8};
    size_t i;
    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (strcmp(t, names[i]) == 0) {
            *bytes = widths[i];
            *is_signed = t[0] == 's';
            return 1;
        }
    }
    return 0;
}

static int decode(parse_state *ps, char *line, insn *in)
{
    char text[MAX_LINE], *op, *rest, *args[4];
    int n = 0, want;
    strcpy(text, line);
    memset(in, 0, sizeof *in);
    in->guard = -1;
    op = text;
    if (op[0] == '@') {
        char *sp = strchr(op, ' ');
        if (sp == NULL) {
            return fail(ps, "guard", line);
        }
        *sp = '\0';
        in->negate = op[1] == '!';
        if ((in->guard = reg_index(ps->e, op + 1 + in->negate)) < 0) {
            return fail(ps, "guard", line);
        }
        op = sp + 1;
    }
    rest = strchr(op, ' ');
    if (rest != NULL) {
        *rest++ = '\0';
        while (rest != NULL && n < 4) {
            char *comma = strchr(rest, ',');
            args[n++] = rest;
            if (comma != NULL) {
                *comma = '\0';
                rest = comma + 1;
            } else {
                rest = NULL;
            }
        }
        if (rest != NULL) {
            return fail(ps, "operands", line);
        }
    }
    want = 2;
    if (strcmp(op, "mov.b64") == 0 || strcmp(op, "mov.u64") == 0 || strcmp(op, "mov.pred") == 0) {
        in->op = I_MOV;
    } else if (strcmp(op, "mov.u32") == 0) {
        in->op = I_MOV32;
    } else if (strcmp(op, "ld.param.u64") == 0) {
        in->op = I_LDP;
    } else if (strcmp(op, "cvta.to.global.u64") == 0) {
        in->op = I_MOV;
    } else if (strcmp(op, "ld.volatile.global.u64") == 0) {
        in->op = I_LD;
        in->bytes = 8;
    } else if (strncmp(op, "ld.global.", 10) == 0) {
        in->op = I_LD;
        if (!type_of(op + 10, &in->bytes, &in->is_signed)) {
            return fail(ps, "type", line);
        }
    } else if (strncmp(op, "st.global.", 10) == 0) {
        in->op = I_ST;
        if (!type_of(op + 10, &in->bytes, &in->is_signed)) {
            return fail(ps, "type", line);
        }
    } else if (strncmp(op, "cvt.", 4) == 0 && strlen(op) > 8u) {
        int b64, s64;
        char d[8];
        const char *dot = strchr(op + 4, '.');
        if (dot == NULL || (size_t)(dot - (op + 4)) >= sizeof d) {
            return fail(ps, "type", line);
        }
        memcpy(d, op + 4, (size_t)(dot - (op + 4)));
        d[dot - (op + 4)] = '\0';
        in->op = I_CVT;
        if (!type_of(d, &b64, &s64) || b64 != 8 || !type_of(dot + 1, &in->bytes, &in->is_signed)) {
            return fail(ps, "type", line);
        }
    } else if (strncmp(op, "setp.", 5) == 0) {
        static const char *const cmps[] = {"eq.", "ne.", "lt.", "le.", "gt.", "ge."};
        int c, b;
        in->op = I_SETP;
        for (c = 0; c < 6 && strncmp(op + 5, cmps[c], 3) != 0; c++) {
        }
        if (c == 6 || !type_of(op + 8, &b, &in->is_signed) || b != 8) {
            return fail(ps, "comparison", line);
        }
        in->bytes = c;
        want = 3;
    } else if (strcmp(op, "selp.u64") == 0 || strcmp(op, "selp.b64") == 0) {
        in->op = I_SELP;
        want = 4;
    } else if (strcmp(op, "mad.lo.s64") == 0) {
        in->op = I_MAD;
        want = 4;
    } else if (strcmp(op, "abs.s64") == 0 || strcmp(op, "neg.s64") == 0 || strcmp(op, "not.pred") == 0) {
        in->op = op[0] == 'a' ? I_ABS : op[0] == 'n' && op[1] == 'e' ? I_NEG : I_PNOT;
    } else if (strcmp(op, "bra") == 0) {
        in->op = I_BRA;
        want = 1;
    } else if (strcmp(op, "ret") == 0) {
        in->op = I_RET;
        want = 0;
    } else {
        static const struct {
            const char *name;
            int op;
        } three[] = {
            {"add.u64", I_ADD}, {"add.s64", I_ADD}, {"sub.u64", I_SUB}, {"sub.s64", I_SUB},
            {"mul.lo.s64", I_MULLO}, {"mul.hi.s64", I_MULHI_S}, {"mul.hi.u64", I_MULHI_U},
            {"mul.wide.u32", I_MULWIDE}, {"div.u64", I_DIV}, {"rem.u64", I_REM}, {"min.u64", I_MIN},
            {"xor.b64", I_XOR}, {"and.b64", I_AND}, {"shr.s64", I_SHR}, {"and.pred", I_PAND},
            {"or.pred", I_POR}, {"atom.global.min.u64", I_ATOMMIN},
        };
        size_t i;
        for (i = 0; i < sizeof three / sizeof three[0] && strcmp(op, three[i].name) != 0; i++) {
        }
        if (i == sizeof three / sizeof three[0]) {
            return fail(ps, "instruction", line);
        }
        in->op = three[i].op;
        want = 3;
    }
    if (n != want) {
        return fail(ps, "operand count", line);
    }
    in->n = n;
    for (n = 0; n < want; n++) {
        if (!parse_operand(ps, args[n], &in->a[n], in->op == I_BRA)) {
            return fail(ps, "operand", line);
        }
    }
    return 1;
}

static void entry_free(ptx_entry *e)
{
    free(e->reg_name);
    free(e->code);
    free(e);
}

void ptx_free(ptx_module *m)
{
    if (m == NULL) {
        return;
    }
    while (m->entries != NULL) {
        ptx_entry *e = m->entries;
        m->entries = e->next;
        entry_free(e);
    }
    free(m);
}

/* The next line of text into buf without leading spaces; NULL at the end. */
static const char *next_line(const char *p, char *buf, size_t cap)
{
    size_t n = 0u;
    if (*p == '\0') {
        return NULL;
    }
    while (*p == ' ') {
        p++;
    }
    while (*p != '\0' && *p != '\n') {
        if (n + 1u < cap) {
            buf[n++] = *p;
        }
        p++;
    }
    buf[n] = '\0';
    return *p == '\n' ? p + 1 : p;
}

/* One entry, from the line after `.visible .entry NAME(` through its closing `}`. */
static const char *parse_entry(parse_state *ps, const char *p)
{
    ptx_entry *e = ps->e;
    char line[MAX_LINE];
    const char *body;
    int pass;
    /* Parameters: `.param .u64 NAME,` or `.param .align 8 .b8 NAME[88],`, the last with `)`. */
    for (;;) {
        char name[32];
        int size = 8, last;
        size_t n;
        if ((p = next_line(p, line, sizeof line)) == NULL) {
            fail(ps, "end of text in parameters", e->name);
            return NULL;
        }
        n = strlen(line);
        last = n > 0u && line[n - 1u] == ')';
        if (n > 0u) {
            line[n - 1u] = '\0';
        }
        if (sscanf(line, ".param .u64 %31s", name) == 1 && strchr(line, '[') == NULL) {
            size = 8;
        } else if (sscanf(line, ".param .align 8 .b8 %31[^[][88]", name) == 1) {
            size = 88;
        } else {
            fail(ps, "parameter", line);
            return NULL;
        }
        if (e->nparams == MAX_PARAMS) {
            fail(ps, "too many parameters", line);
            return NULL;
        }
        strcpy(e->param_name[e->nparams], name);
        e->param_off[e->nparams] = e->param_bytes;
        e->param_size[e->nparams] = size;
        e->param_bytes += size;
        e->nparams++;
        if (last) {
            break;
        }
    }
    if ((p = next_line(p, line, sizeof line)) == NULL || strcmp(line, "{") != 0) {
        fail(ps, "entry body", e->name);
        return NULL;
    }
    body = p;
    /* Pass 0 counts instructions and records labels; pass 1 decodes. */
    for (pass = 0; pass < 2; pass++) {
        int k = 0;
        p = body;
        for (;;) {
            size_t n;
            if ((p = next_line(p, line, sizeof line)) == NULL) {
                fail(ps, "end of text in body", e->name);
                return NULL;
            }
            n = strlen(line);
            if (strcmp(line, "}") == 0) {
                break;
            }
            if (n == 0u || strncmp(line, ".reg ", 5) == 0) {
                continue;
            }
            if (line[n - 1u] == ':') {
                if (pass == 0) {
                    if (ps->nlabels == 1024 || n - 1u >= sizeof ps->labels[0]) {
                        fail(ps, "label", line);
                        return NULL;
                    }
                    line[n - 1u] = '\0';
                    strcpy(ps->labels[ps->nlabels], line);
                    ps->label_at[ps->nlabels++] = k;
                }
                continue;
            }
            if (line[n - 1u] != ';') {
                fail(ps, "statement", line);
                return NULL;
            }
            line[n - 1u] = '\0';
            if (pass == 1 && !decode(ps, line, &e->code[k])) {
                return NULL;
            }
            k++;
        }
        if (pass == 0) {
            e->ninsn = k;
            if ((e->code = (insn *)calloc((size_t)k + 1u, sizeof *e->code)) == NULL) {
                fail(ps, "memory", e->name);
                return NULL;
            }
        }
    }
    return p;
}

ptx_module *ptx_load(const char *text, char *log, size_t cap)
{
    parse_state *ps = (parse_state *)calloc(1u, sizeof *ps);
    ptx_module *m = (ptx_module *)calloc(1u, sizeof *m);
    char line[MAX_LINE];
    const char *p = text, *q;
    if (ps == NULL || m == NULL) {
        free(ps);
        free(m);
        return NULL;
    }
    ps->log = log;
    ps->cap = cap;
    while ((q = next_line(p, line, sizeof line)) != NULL) {
        char name[128];
        p = q;
        if (strncmp(line, ".version ", 9) == 0 || strncmp(line, ".target ", 8) == 0 ||
            strncmp(line, ".address_size ", 14) == 0 || line[0] == '\0') {
            continue;
        }
        if (sscanf(line, ".visible .entry %127[^(](", name) != 1) {
            fail(ps, "directive", line);
            ptx_free(m);
            free(ps);
            return NULL;
        }
        ps->e = (ptx_entry *)calloc(1u, sizeof *ps->e);
        if (ps->e == NULL) {
            ptx_free(m);
            free(ps);
            return NULL;
        }
        strcpy(ps->e->name, name);
        ps->e->next = m->entries;
        m->entries = ps->e;
        ps->nlabels = 0;
        if ((p = parse_entry(ps, p)) == NULL) {
            ptx_free(m);
            free(ps);
            return NULL;
        }
    }
    free(ps);
    return m;
}

ptx_entry *ptx_find(ptx_module *m, const char *name)
{
    ptx_entry *e;
    for (e = m->entries; e != NULL && strcmp(e->name, name) != 0; e = e->next) {
    }
    return e;
}

/* The high 64 bits of the 128-bit product of a and b, unsigned, from 32-bit limbs. */
static uint64_t mul_hi_u(uint64_t a, uint64_t b)
{
    uint64_t a0 = a & 0xffffffffu, a1 = a >> 32, b0 = b & 0xffffffffu, b1 = b >> 32;
    uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    uint64_t mid = (p00 >> 32) + (p01 & 0xffffffffu) + (p10 & 0xffffffffu);
    return p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

/* Signed: the unsigned high half, less b when a is negative and a when b is. */
static uint64_t mul_hi_s(uint64_t a, uint64_t b)
{
    uint64_t hi = mul_hi_u(a, b);
    if (a >> 63) {
        hi -= b;
    }
    if (b >> 63) {
        hi -= a;
    }
    return hi;
}

static uint64_t extend(uint64_t v, int bytes, int is_signed)
{
    unsigned bits = 8u * (unsigned)bytes;
    uint64_t mask, sign;
    if (bits >= 64u) {
        return v;
    }
    mask = ((uint64_t)1 << bits) - 1u;
    sign = (uint64_t)1 << (bits - 1u);
    v &= mask;
    return is_signed && (v & sign) != 0u ? v | ~mask : v;
}

typedef struct item_state {
    uint64_t *regs;
    const unsigned char *params;
    uint64_t ctaid, ntid, tid;
} item_state;

static uint64_t value(const item_state *s, const operand *o)
{
    uint64_t v;
    switch (o->kind) {
    case O_REG:
    case O_MEM: return s->regs[o->reg];
    case O_IMM: return o->imm;
    case O_CTAID: return s->ctaid;
    case O_NTID: return s->ntid;
    case O_TID: return s->tid;
    case O_PARAM:
        memcpy(&v, s->params + o->reg + (size_t)o->imm, sizeof v);
        return v;
    default: return 0u;
    }
}

static int compare(int c, int is_signed, uint64_t a, uint64_t b)
{
    int lt, eq = a == b;
    if (is_signed) {
        lt = (a ^ 0x8000000000000000u) < (b ^ 0x8000000000000000u);
    } else {
        lt = a < b;
    }
    switch (c) {
    case C_EQ: return eq;
    case C_NE: return !eq;
    case C_LT: return lt;
    case C_LE: return lt || eq;
    case C_GT: return !lt && !eq;
    default: return !lt;
    }
}

/* One work-item: 0, 700 or 702. */
static int run_item(const ptx_entry *e, item_state *s, ptx_translate translate)
{
    long long steps = 0;
    int pc = 0;
    memset(s->regs, 0, (size_t)e->nregs * sizeof s->regs[0]);
    while (pc < e->ninsn) {
        const insn *in = &e->code[pc++];
        uint64_t a = 0u, b = 0u, *d;
        void *host;
        if (++steps > STEP_LIMIT) {
            return 702;
        }
        if (in->guard >= 0 && (s->regs[in->guard] != 0u) == (in->negate != 0)) {
            continue;
        }
        if (in->n > 1) {
            a = value(s, &in->a[1]);
        }
        if (in->n > 2) {
            b = value(s, &in->a[2]);
        }
        d = in->n > 0 && in->a[0].kind == O_REG ? &s->regs[in->a[0].reg] : NULL;
        switch (in->op) {
        case I_MOV: *d = a; break;
        case I_MOV32: *d = a & 0xffffffffu; break;
        case I_LDP: *d = a; break;
        case I_LD:
            if ((host = translate(a, (size_t)in->bytes)) == NULL) {
                return 700;
            }
            {
                uint64_t v = 0u;
                memcpy(&v, host, (size_t)in->bytes); /* little-endian hosts only (SPEC-03 5.3) */
                *d = extend(v, in->bytes, in->is_signed);
            }
            break;
        case I_ST: {
            uint64_t v = value(s, &in->a[1]);
            if ((host = translate(value(s, &in->a[0]), (size_t)in->bytes)) == NULL) {
                return 700;
            }
            memcpy(host, &v, (size_t)in->bytes);
            break;
        }
        case I_ATOMMIN: {
            uint64_t old;
            if ((host = translate(a, 8u)) == NULL) {
                return 700;
            }
            memcpy(&old, host, sizeof old);
            if (b < old) {
                memcpy(host, &b, sizeof b);
            }
            *d = old;
            break;
        }
        case I_CVT: *d = extend(a, in->bytes, in->is_signed); break;
        case I_SETP: *d = (uint64_t)compare(in->bytes, in->is_signed, a, b); break;
        case I_SELP: *d = s->regs[in->a[3].reg] != 0u ? a : b; break;
        case I_ADD: *d = a + b; break;
        case I_SUB: *d = a - b; break;
        case I_MULLO: *d = a * b; break;
        case I_MULHI_S: *d = mul_hi_s(a, b); break;
        case I_MULHI_U: *d = mul_hi_u(a, b); break;
        case I_MULWIDE: *d = (a & 0xffffffffu) * (b & 0xffffffffu); break;
        case I_MAD: *d = a * b + value(s, &in->a[3]); break;
        case I_DIV: *d = b == 0u ? UINT64_MAX : a / b; break;
        case I_REM: *d = b == 0u ? a : a % b; break;
        case I_MIN: *d = a < b ? a : b; break;
        case I_ABS: *d = (a >> 63) != 0u ? 0u - a : a; break;
        case I_NEG: *d = 0u - a; break;
        case I_XOR: *d = a ^ b; break;
        case I_AND: *d = a & b; break;
        case I_SHR: {
            unsigned k = b > 63u ? 63u : (unsigned)b;
            *d = (a >> 63) != 0u ? ~(~a >> k) : a >> k;
            break;
        }
        case I_PAND: *d = (uint64_t)(a != 0u && b != 0u); break;
        case I_POR: *d = (uint64_t)(a != 0u || b != 0u); break;
        case I_PNOT: *d = (uint64_t)(a == 0u); break;
        case I_BRA: pc = (int)in->a[0].imm; break;
        default: return 0; /* I_RET */
        }
    }
    return 0;
}

int ptx_launch(const ptx_entry *e, unsigned grid, unsigned block, void **params, ptx_translate translate,
               int ascending)
{
    unsigned char buf[MAX_PARAMS * 88];
    item_state s;
    uint64_t items = (uint64_t)grid * block, k;
    int i, r = 0;
    for (i = 0; i < e->nparams; i++) {
        memcpy(buf + e->param_off[i], params[i], (size_t)e->param_size[i]);
    }
    s.regs = (uint64_t *)calloc((size_t)e->nregs + 1u, sizeof *s.regs);
    if (s.regs == NULL) {
        return 2;
    }
    s.params = buf;
    s.ntid = block;
    for (k = 0u; k < items && r == 0; k++) {
        uint64_t linear = ascending ? k : items - 1u - k;
        s.ctaid = linear / block;
        s.tid = linear % block;
        r = run_item(e, &s, translate);
    }
    free(s.regs);
    return r;
}
