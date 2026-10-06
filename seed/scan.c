/* scan.c: source-text checks and tokens (SPEC-04 3.1 to 3.9; SPEC-09 5.3, 5.5).
 *
 * A cint-boot-1 source is ASCII (SPEC-09 5.3), so a column is a byte count
 * and equals the count of Unicode scalar values that DIAG-01 asks for. Before
 * the first non-ASCII byte every byte is ASCII, so its position is exact too:
 * an invalid UTF-8 sequence there is C1002, a forbidden character C1003, a C1
 * control C1004, and any other character the boot-subset refusal.
 */
#include <string.h>

#include "diag.h"
#include "scan.h"

static const char *const keywords[KW_COUNT] = {
    "", "as", "assert", "break", "by", "case", "catch", "const", "continue",
    "default", "defer", "do", "else", "enum", "error", "expect_fault", "export",
    "fallthrough", "false", "for", "if", "import", "in", "in_place", "inout",
    "kernel", "out", "over", "profile", "reduce", "return", "schedule",
    "static_assert", "struct", "switch", "test", "true", "try", "type", "var",
    "void", "where", "while", "distinct", "secret"};

typedef struct op_spell {
    const char *text;
    uint8_t op;
} op_spell;

/* Longest first, so the first match is the maximal munch (SPEC-04 LS-12). */
static const op_spell ops[] = {
    {"<<%=", OP_SHLWA}, {"<<%", OP_SHLW}, {"<<=", OP_SHLA}, {">>=", OP_SHRA}, {"+%=", OP_ADDWA},
    {"-%=", OP_SUBWA}, {"*%=", OP_MULWA}, {"+|=", OP_ADDSA}, {"-|=", OP_SUBSA}, {"*|=", OP_MULSA},
    {"..=", OP_DOTDOTEQ}, {"<<", OP_SHL}, {">>", OP_SHR}, {"+%", OP_ADDW}, {"-%", OP_SUBW},
    {"*%", OP_MULW}, {"+|", OP_ADDS}, {"-|", OP_SUBS}, {"*|", OP_MULS}, {"==", OP_EQ},
    {"!=", OP_NE}, {"<=", OP_LE}, {">=", OP_GE}, {"&&", OP_LAND}, {"||", OP_LOR},
    {"++", OP_INC}, {"--", OP_DEC}, {"+=", OP_ADDA}, {"-=", OP_SUBA}, {"*=", OP_MULA},
    {"/=", OP_DIVA}, {"%=", OP_REMA}, {"&=", OP_ANDA}, {"|=", OP_ORA}, {"^=", OP_XORA},
    {"..", OP_DOTDOT}, {"+", OP_ADD}, {"-", OP_SUB}, {"*", OP_MUL}, {"/", OP_DIV},
    {"%", OP_REM}, {"<", OP_LT}, {">", OP_GT}, {"=", OP_ASSIGN}, {"&", OP_BAND},
    {"|", OP_BOR}, {"^", OP_BXOR}, {"~", OP_TILDE}, {"!", OP_NOT}, {"(", OP_LPAREN},
    {")", OP_RPAREN}, {"[", OP_LBRACK}, {"]", OP_RBRACK}, {"{", OP_LBRACE}, {"}", OP_RBRACE},
    {",", OP_COMMA}, {";", OP_SEMI}, {".", OP_DOT}, {":", OP_COLON}, {"?", OP_QUEST},
    {"@", OP_AT}, {NULL, OP_NONE}};

const char *kw_text(int kw)
{
    return kw > 0 && kw < KW_COUNT ? keywords[kw] : "?";
}

const char *op_text(int op)
{
    int i;
    if (op == OP_ASW) {
        return "as%";
    }
    for (i = 0; ops[i].text != NULL; i++) {
        if (ops[i].op == op) {
            return ops[i].text;
        }
    }
    return "?";
}

/* SPEC-04 LS-6: bidirectional controls, line and paragraph separators, and the
 * Default_Ignorable_Code_Point property of Unicode 15.1. */
static const uint32_t forbidden[][2] = {
    {0x00AD, 0x00AD}, {0x034F, 0x034F}, {0x061C, 0x061C}, {0x115F, 0x1160}, {0x17B4, 0x17B5},
    {0x180B, 0x180F}, {0x200B, 0x200F}, {0x2028, 0x202E}, {0x2060, 0x206F}, {0x3164, 0x3164},
    {0xFE00, 0xFE0F}, {0xFEFF, 0xFEFF}, {0xFFA0, 0xFFA0}, {0xFFF0, 0xFFF8}, {0x1BCA0, 0x1BCA3},
    {0x1D173, 0x1D17A}, {0xE0000, 0xE0FFF}};

/* Decodes one UTF-8 scalar value at s[0..n); returns its length or 0 if invalid. */
static size_t utf8_decode(const uint8_t *s, size_t n, uint32_t *cp)
{
    size_t len, i;
    uint32_t v, min;
    if (s[0] < 0xC2u || s[0] > 0xF4u) {
        return 0;
    }
    len = s[0] < 0xE0u ? 2u : s[0] < 0xF0u ? 3u : 4u;
    min = len == 2u ? 0x80u : len == 3u ? 0x800u : 0x10000u;
    v = (uint32_t)s[0] & (len == 2u ? 0x1Fu : len == 3u ? 0x0Fu : 0x07u);
    if (len > n) {
        return 0;
    }
    for (i = 1; i < len; i++) {
        if ((s[i] & 0xC0u) != 0x80u) {
            return 0;
        }
        v = (v << 6) | ((uint32_t)s[i] & 0x3Fu);
    }
    if (v < min || v > 0x10FFFFu || (v >= 0xD800u && v <= 0xDFFFu)) {
        return 0;
    }
    *cp = v;
    return len;
}

static void check_source(int32_t mi)
{
    const seed_module *m = &S.mods[mi];
    const uint8_t *s = m->src;
    size_t n = m->srclen, i = 0;
    uint32_t line = 1, col = 1;
    if (n >= 3u && s[0] == 0xEFu && s[1] == 0xBBu && s[2] == 0xBFu) {
        diag_at(mi, 1, 1, "C1002", "the file begins with a UTF-8 byte-order mark (EF BB BF); "
                "source text is UTF-8 without one (SPEC-04 LS-4)");
    }
    while (i < n) {
        uint8_t b = s[i];
        if (b == '\n') {
            line++, col = 1, i++;
            continue;
        }
        if (b == '\r') {
            if (i + 1 < n && s[i + 1] == '\n') {
                line++, col = 1, i += 2;
                continue;
            }
            diag_at(mi, line, col, "C1004", "carriage return not followed by a line feed");
        }
        if (b == 0) {
            diag_at(mi, line, col, "C1002", "byte 0x00 in source text");
        }
        if ((b < 0x20u && b != '\t') || b == 0x7Fu) {
            diag_at(mi, line, col, "C1004", "control character U+%04X", (unsigned)b);
        }
        if (b >= 0x80u) {
            uint32_t cp = 0;
            size_t k;
            if (utf8_decode(s + i, n - i, &cp) == 0) {
                diag_at(mi, line, col, "C1002", "invalid UTF-8 sequence");
            }
            if (cp <= 0x9Fu) {
                diag_at(mi, line, col, "C1004", "control character U+%04X", (unsigned)cp);
            }
            for (k = 0; k < sizeof forbidden / sizeof forbidden[0]; k++) {
                if (cp >= forbidden[k][0] && cp <= forbidden[k][1]) {
                    diag_at(mi, line, col, "C1003", "forbidden invisible or bidirectional character U+%04X",
                            (unsigned)cp);
                }
            }
            diag_at(mi, line, col, C_BOOT, "non-ASCII character U+%04X: cint-boot-1 sources are ASCII "
                    "(SPEC-09 5.3)", (unsigned)cp);
        }
        col++, i++;
    }
}

/* -- the scanner --------------------------------------------------------------------- */

typedef struct scanner {
    int32_t mi;
    seed_module *m;
    size_t i;
    uint32_t line, col;
} scanner;

static int peekc(const scanner *s, size_t k)
{
    return s->i + k < s->m->srclen ? s->m->src[s->i + k] : -1;
}

static void adv(scanner *s, size_t k)
{
    while (k-- > 0u && s->i < s->m->srclen) {
        if (s->m->src[s->i] == '\n') {
            s->line++, s->col = 1;
        } else if (s->m->src[s->i] != '\r') {
            s->col++;
        }
        s->i++;
    }
}

static bool is_alpha(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_digit(int c)
{
    return c >= '0' && c <= '9';
}

static bool is_hex(int c)
{
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int hexval(int c)
{
    return is_digit(c) ? c - '0' : (c >= 'a' ? c - 'a' + 10 : c - 'A' + 10);
}

/* The digits of a literal body: separators only between digits (SPEC-04 LS-26).
 * The magnitude is held in 64 bits (`big` above); a radix literal over 4,096
 * bits is C1023, and a decimal literal of more than 1,233 significant digits,
 * which may exceed 4,096 bits, is beyond the seed (SEED-16). */
static void literal_value(scanner *s, seed_tok *t, const uint8_t *w, size_t n, int base)
{
    size_t k, sig = 0;
    uint64_t v = 0;
    unsigned lead = 0, bits = 0;
    if (n == 0u) {
        diag_at(s->mi, t->line, t->col, "C1024", "a radix prefix needs digits after it");
    }
    if (w[0] == '_' || w[n - 1] == '_') {
        diag_at(s->mi, t->line, t->col, "C1021", "misplaced digit separator `_`");
    }
    for (k = 0; k < n; k++) {
        int c = w[k], d;
        if (c == '_') {
            if (w[k + 1] == '_') {
                diag_at(s->mi, t->line, t->col, "C1021", "misplaced digit separator `_`");
            }
            continue;
        }
        d = is_hex(c) ? hexval(c) : 99;
        if (d >= base) {
            diag_at(s->mi, t->line, t->col, "C1024", "invalid digit `%c` in a numeric literal", c);
        }
        if (v > (UINT64_MAX - (uint64_t)d) / (uint64_t)base) {
            t->big = 1;
        }
        v = v * (uint64_t)base + (uint64_t)d;
        if (sig > 0u || d != 0) {
            lead = sig++ == 0u ? (unsigned)d : lead;
        }
    }
    while ((lead >> bits) != 0u) {
        bits++;
    }
    if (base != 10 && sig > 0u && (sig - 1u) * (base == 16 ? 4u : base == 8 ? 3u : 1u) + bits > 4096u) {
        diag_at(s->mi, t->line, t->col, "C1023", "the literal exceeds 4096 bits of magnitude (SPEC-04 LS-31)");
    }
    if (base == 10 && sig > 1233u) {
        diag_at(s->mi, t->line, t->col, C_UNSUP, "a decimal literal of more than 1233 digits (SPEC-09 SEED-16)");
    }
    t->val = v;
}

static void scan_number(scanner *s, seed_tok *t)
{
    const uint8_t *w = s->m->src + s->i;
    size_t n = 0;
    int c1;
    while (s->i + n < s->m->srclen && (is_alpha(w[n]) || is_digit(w[n]))) {
        n++;
    }
    c1 = n >= 2u ? w[1] : 0;
    if (w[0] == '0' && (c1 == 'X' || c1 == 'B' || c1 == 'O' || c1 == 'T')) {
        diag_at(s->mi, t->line, t->col, "C1020", "the radix prefix letter is lowercase: write 0%c", c1 + 32);
    }
    if (w[0] == '0' && c1 == 't') {
        diag_at(s->mi, t->line, t->col, C_BOOT, "a balanced-ternary literal: ternary is outside cint-boot-1 "
                "(SPEC-09 5.5, TERN_INT)");
    }
    if (s->i + n + 1 < s->m->srclen && w[n] == '.' &&
        (w[0] == '0' && c1 == 'x' ? is_hex(w[n + 1]) : is_digit(w[n + 1]))) {
        diag_at(s->mi, t->line, t->col, C_BOOT, "a fraction literal: fixed point is outside cint-boot-1 "
                "(SPEC-09 5.5, DEC_FRAC)");
    }
    if (w[0] == '0' && (c1 == 'x' || c1 == 'b' || c1 == 'o')) {
        literal_value(s, t, w + 2, n - 2, c1 == 'x' ? 16 : c1 == 'b' ? 2 : 8);
    } else {
        if (n > 1u && w[0] == '0' && (is_digit(c1) || c1 == '_')) {
            diag_at(s->mi, t->line, t->col, "C1022", "a decimal literal of more than one digit must not "
                    "begin with 0: write it without the zero, or 0o for octal");
        }
        literal_value(s, t, w, n, 10);
    }
    t->kind = T_INT;
    adv(s, n);
}

/* One escape after a backslash at s->i; returns the byte (SPEC-09 5.5, escape).
 * C1032 points at that backslash, as cint_ref does (seed/OPEN.md SEED-OQ-09). */
static int scan_escape(scanner *s, const seed_tok *t)
{
    int c = peekc(s, 1);
    switch (c) {
    case '\\': case '"': case '\'':
        adv(s, 2);
        return c;
    case 'n': adv(s, 2); return '\n';
    case 'r': adv(s, 2); return '\r';
    case 't': adv(s, 2); return '\t';
    case '0': adv(s, 2); return 0;
    case 'x':
        if (is_hex(peekc(s, 2)) && is_hex(peekc(s, 3))) {
            int v = hexval(peekc(s, 2)) * 16 + hexval(peekc(s, 3));
            if (v > 0x7F) {
                diag_at(s->mi, s->line, s->col, "C1032", "\\x escapes in strings and characters are 00 to 7F");
            }
            adv(s, 4);
            return v;
        }
        break;
    case 'u':
        diag_at(s->mi, t->line, t->col, C_BOOT, "a \\u{...} escape: cint-boot-1 literals are ASCII "
                "(SPEC-09 5.5, escape)");
    default:
        break;
    }
    diag_at(s->mi, s->line, s->col, "C1032", "unknown escape sequence");
}

static void scan_char(scanner *s, seed_tok *t)
{
    int c, v;
    adv(s, 1);
    c = peekc(s, 0);
    if (c == '\'') {
        diag_at(s->mi, t->line, t->col, "C1030", "a character literal holds exactly one character");
    }
    if (c < 0 || c == '\n' || c == '\r') {
        diag_at(s->mi, t->line, t->col, "C1038", "unterminated character literal");
    }
    if (c == '\\') {
        v = scan_escape(s, t);
    } else {
        v = c;
        adv(s, 1);
    }
    if (peekc(s, 0) != '\'') {
        size_t k = 0;
        while (peekc(s, k) >= 0 && peekc(s, k) != '\'' && peekc(s, k) != '\n') {
            k++;
        }
        if (peekc(s, k) == '\'') {
            diag_at(s->mi, t->line, t->col, "C1030", "a character literal holds exactly one character");
        }
        diag_at(s->mi, t->line, t->col, "C1038", "unterminated character literal");
    }
    adv(s, 1);
    t->kind = T_INT;
    t->val = (uint64_t)v;
}

static void scan_string(scanner *s, seed_tok *t)
{
    seed_module *m = s->m;
    t->kind = T_STR;
    t->val = m->nstrs;
    adv(s, 1);
    for (;;) {
        int c = peekc(s, 0), v;
        if (c < 0) {
            diag_at(s->mi, t->line, t->col, "C1038", "unterminated string literal");
        }
        if (c == '\n' || c == '\r') {
            diag_at(s->mi, t->line, t->col, "C1034", "unescaped line break in a string literal");
        }
        if (c == '"') {
            adv(s, 1);
            break;
        }
        if (c == '{' || c == '}') {
            if (peekc(s, 1) != c) {
                /* A hole: an error only where the literal is used as a value (the parser
                 * reports a print statement first), so record where it is and skip it. */
                if (t->pad == 0u) {
                    t->pad = s->col;
                }
                while (peekc(s, 0) >= 0 && peekc(s, 0) != '\n' && peekc(s, 0) != '"' && peekc(s, 0) != '}') {
                    adv(s, 1);
                }
                if (peekc(s, 0) == '}') {
                    adv(s, 1);
                }
                continue;
            }
            adv(s, 2);
            v = c;
        } else if (c == '\\') {
            v = scan_escape(s, t);
        } else {
            v = c;
            adv(s, 1);
        }
        m->strs[m->nstrs++] = (uint8_t)v;
    }
    t->slen = (uint32_t)(m->nstrs - t->val);
}

static bool word_is(const uint8_t *w, size_t n, const char *text)
{
    return strlen(text) == n && memcmp(w, text, n) == 0;
}

static void scan_word(scanner *s, seed_tok *t)
{
    const uint8_t *w = s->m->src + s->i;
    size_t n = 0;
    int k;
    while (s->i + n < s->m->srclen && (is_alpha(w[n]) || is_digit(w[n]))) {
        n++;
    }
    if (n == 1u && w[0] == 'b' && s->i + 1 < s->m->srclen && w[1] == '"') {
        diag_at(s->mi, t->line, t->col, C_BOOT, "a byte-string literal: a string literal already "
                "denotes a U8 view in cint-boot-1 (SPEC-09 5.5, BYTES)");
    }
    if (n > 255u) {
        diag_at(s->mi, t->line, t->col, "C1012", "identifier longer than 255 bytes (SPEC-04 LS-15)");
    }
    if (w[0] == 'Q' && n >= 2u && is_digit(w[1]) && s->i + n + 1 < s->m->srclen && w[n] == '.' &&
        is_digit(w[n + 1])) {
        diag_at(s->mi, t->line, t->col, C_BOOT, "a fixed-point type: fixed point is outside cint-boot-1 "
                "(SPEC-09 5.5, FIXED_TYPE)");
    }
    t->kind = T_IDENT;
    for (k = 1; k < KW_COUNT; k++) {
        if (word_is(w, n, keywords[k])) {
            t->kind = T_KW;
            t->sub = (uint8_t)k;
            break;
        }
    }
    if (t->kind == T_KW && t->sub == KW_AS && s->i + 2 < s->m->srclen) {
        int c = w[2];
        if (c == '%' || c == '|' || c == '?') {
            t->kind = T_OP;
            t->sub = (uint8_t)(c == '%' ? OP_ASW : c == '|' ? OP_ASS : OP_ASQ);
            n = 3;
        }
    }
    adv(s, n);
}

static void scan_op(scanner *s, seed_tok *t)
{
    int i;
    for (i = 0; ops[i].text != NULL; i++) {
        size_t n = strlen(ops[i].text);
        if (s->i + n <= s->m->srclen && memcmp(s->m->src + s->i, ops[i].text, n) == 0) {
            t->kind = T_OP;
            t->sub = ops[i].op;
            adv(s, n);
            return;
        }
    }
    diag_at(s->mi, t->line, t->col, C_SYNTAX, "unexpected character `%c`", s->m->src[s->i]);
}

/* Skips whitespace and comments; returns whether any was skipped. */
static bool skip_space(scanner *s)
{
    bool any = false;
    for (;;) {
        int c = peekc(s, 0);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            adv(s, 1);
        } else if (c == '/' && peekc(s, 1) == '/') {
            while (peekc(s, 0) >= 0 && peekc(s, 0) != '\n') {
                adv(s, 1);
            }
        } else if (c == '/' && peekc(s, 1) == '*') {
            uint32_t line = s->line, col = s->col;
            adv(s, 2);
            for (;;) {
                if (peekc(s, 0) < 0) {
                    diag_at(s->mi, line, col, "C1005", "unterminated block comment");
                }
                if (peekc(s, 0) == '*' && peekc(s, 1) == '/') {
                    adv(s, 2);
                    break;
                }
                if (peekc(s, 0) == '/' && peekc(s, 1) == '*') {
                    /* cint_ref nests (SPEC-04 LS-14, Proposed); boot-1 does not, so
                     * a source that reads differently under the two is refused. */
                    diag_at(s->mi, s->line, s->col, C_BOOT, "a block comment inside a block comment: nesting "
                            "is outside cint-boot-1 (SPEC-09 5.5, BLOCK_COMMENT; SPEC-04 LS-14)");
                }
                adv(s, 1);
            }
        } else {
            return any;
        }
        any = true;
    }
}

void scan_module(int32_t mi)
{
    seed_module *m = &S.mods[mi];
    scanner s;
    check_source(mi);
    m->toks = seed_alloc((m->srclen + 2u) * sizeof *m->toks, "token");
    m->strs = seed_alloc(m->srclen + 1u, "string");
    s.mi = mi, s.m = m, s.i = 0, s.line = 1, s.col = 1;
    for (;;) {
        bool space = skip_space(&s);
        seed_tok *t = &m->toks[m->ntok];
        int c = peekc(&s, 0);
        memset(t, 0, sizeof *t);
        t->line = s.line, t->col = s.col, t->off = (uint32_t)s.i, t->space = space ? 1u : 0u;
        if (c < 0) {
            t->kind = T_EOF;
            m->ntok++;
            return;
        }
        if (is_digit(c)) {
            scan_number(&s, t);
        } else if (is_alpha(c)) {
            scan_word(&s, t);
        } else if (c == '\'') {
            scan_char(&s, t);
        } else if (c == '"') {
            scan_string(&s, t);
        } else {
            scan_op(&s, t);
        }
        t->len = (uint32_t)(s.i - t->off);
        m->ntok++;
    }
}
