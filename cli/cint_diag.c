/* cint_diag.c: the message and note of a compile diagnostic (SPEC-04 17.2, LS-276, LS-313) for
 * the `cint` command. Message text is not compared (SPEC-09 9.2): a diagnostic's code and position
 * are its meaning, and the text here only says it.
 *
 * detail[2] of a row names its note (compiler/parse.ci NOTE_KEYWORD, compiler/resolve.ci
 * NOTE_SCRIPT, NOTE_MODULE and NOTE_BUILTIN): 1 and 2 the LS-312 script note, 3 a keyword at
 * detail[0]:detail[1] where a name is expected (SPEC-04 LS-18), 4 a built-in name declared
 * (SPEC-04 LS-145), at the diagnostic itself. The word is read from the module's source at that
 * position (issues #458, #502). Shared state: cli/cint_receipt.h. */
#define _CRT_SECURE_NO_WARNINGS 1

#include <stdio.h>
#include <string.h>

#include "cint_receipt.h"

enum { NOTE_SCRIPT = 1, NOTE_MODULE = 2, NOTE_KEYWORD = 3, NOTE_BUILTIN = 4 };

/* Messages of the SPEC-04 17.2 codes (LS-313); the first row whose range holds the code. */
static const struct {
    int lo, hi;
    const char *text;
} DIAG_TEXT[] = {
    {1001, 1001, "`.cint` file (legacy profile)"}, {1002, 1005, "invalid source bytes"},
    {1010, 1013, "invalid name"}, {1020, 1024, "invalid integer literal"}, {1036, 1037, "invalid format specification"},
    {1030, 1039, "invalid character or string literal"}, {1040, 1040, "empty statement"},
    {1041, 1041, "assignment in a condition"}, {1042, 1042, "`++` or `--` used as an expression"},
    {1043, 1043, "missing parentheses or braces"}, {1050, 1050, "syntax error"},
    {2001, 2001, "type mismatch"}, {2002, 2002, "condition is not `Bool`"},
    {2003, 2003, "literal does not fit its type"}, {2008, 2008, "no explicit conversion between the two types"},
    {2004, 2009, "fraction, fixed-point, enum literal, or rounding rule"}, {2010, 2014, "array rule"},
    {2020, 2022, "wrong fields or arguments"}, {2050, 2050, "uninitialized variable"},
    {2052, 2052, "loop variable assigned"}, {2056, 2056, "integer used as `Bool`"},
    {2058, 2058, "not an assignable place"}, {2060, 2069, "parameter or argument rule"},
    {2101, 2101, "parentheses required"}, {2102, 2102, "chained comparison"},
    {2103, 2103, "operator not defined for the operand type"}, {2000, 2999, "type rule"},
    {3001, 3001, "duplicate name"}, {3004, 3004, "script local used in a function or test"},
    {3005, 3008, "undefined or unusable name"}, {3009, 3009, "module file unreadable"},
    {3010, 3011, "script rule"}, {3012, 3012, "`import` after other items"},
    {3044, 3044, "`main` parameter list"}, {3000, 3999, "name or module rule"},
    {4001, 4001, "missing return"}, {4012, 4012, "statement form not permitted"},
    {4020, 4026, "`switch` rule"}, {4040, 4040, "recursion in `cint-boot-1`"}, {4000, 4999, "control-flow rule"},
    {5000, 5999, "parameter mode, alias, or ABI rule"}, {6001, 6001, "fault during constant evaluation"},
    {6002, 6002, "compile-time fuel exhausted"}, {6004, 6004, "constant expression required"},
    {6000, 6999, "compile-time evaluation rule"}, {9001, 9001, "capacity or output size exceeded"},
    {9002, 9002, "generated symbol over 247 bytes"}, {9003, 9003, "local array over the frame limit"},
    {9004, 9004, "nesting beyond the implementation's limit"},
    {9102, 9102, "not yet supported by this implementation (SPEC-04 C9102)"},
    {9000, 9999, "implementation limit"},
};

/* The source of the module read at `path`, or NULL. */
const cli_module *module_source(const char *path)
{
    for (int i = 0; i < g.nmods; i++) {
        if (g.mods[i].src != NULL && strcmp(g.mods[i].rel, path) == 0) return &g.mods[i];
    }
    return NULL;
}

/* The identifier at line:column of `path` (columns count scalar values, DIAG-01), into word; 0 when
 * there is none. */
static size_t word_at(const char *path, int64_t line, int64_t column, char *word, size_t cap)
{
    const cli_module *m = module_source(path);
    if (m == NULL || line < 1 || column < 1) return 0u;
    const uint8_t *s = m->src, *end = s + m->len;
    for (int64_t k = 1; s < end && k < line; s++) k += *s == '\n';
    for (int64_t c = 1; s < end && *s != '\n' && c < column; s++) c += (*s & 0xC0u) != 0x80u;
    size_t n = 0u;
    for (; s < end && n + 1u < cap && ((*s | 32u) - 'a' < 26u || (unsigned)(*s - '0') < 10u || *s == '_'); s++) word[n++] = (char)*s;
    word[n] = '\0';
    return n;
}

/* The message of diagnostic d of the module at `file`, in buf when it names a word. */
const char *diag_text(const cint_compiler_diag *d, const char *file, char *buf, size_t cap)
{
    int64_t code = d->code;
    char w[72];
    if (d->detail[2] == NOTE_KEYWORD && (code == 1050 || code == 4012) && word_at(file, d->detail[0], d->detail[1], w, sizeof w)) {
        (void)snprintf(buf, cap, code == 1050 ? "`%s` is a keyword (SPEC-04 LS-18), and a name is expected here"
                                              : "`%s` is a keyword (SPEC-04 LS-18) and cannot name a variable", w);
        return buf;
    }
    if (d->detail[2] == NOTE_BUILTIN && code == 3001 && word_at(file, d->line, d->column, w, sizeof w)) {
        (void)snprintf(buf, cap, "`%s` is a built-in name (SPEC-04 LS-145) and cannot be declared", w);
        return buf;
    }
    if (code == 3030) {
        if (d->detail[0] == CINT_PATH_IDENTIFIER)
            return "every segment of a module path, without the final .ci, is an identifier "
                   "(SPEC-04 LS-15) (SPEC-09 CINTC-12)";
        if (d->detail[0] == CINT_PATH_DEVICE)
            return "a segment of a module path is not a Windows device name (SPEC-09 CINTC-12)";
        if (d->detail[0] == CINT_PATH_COLLISION)
            return "two module paths of one program are equal under ASCII case folding (SPEC-09 CINTC-12)";
    }
    if (code == 9001 && d->detail[0] == 31) return "cpu-c17 record layout exceeds 4294967295 bytes (LIM_RECORD_BYTES)";
    if (code == 9102 && d->detail[0] == 2) {
        /* The cross-module limits (compiler/resolve.ci U_CROSS, X_ROW to X_COMBINED). */
        if (d->detail[1] == 1)
            return "not yet supported by this implementation: a parameter, result or field of this type "
                   "from another module (SPEC-04 C9102)";
        if (d->detail[1] == 2)
            return "not yet supported by this implementation: relating the error sets of two modules "
                   "by conversion, comparison or `try` (SPEC-04 C9102)";
        if (d->detail[1] == 3)
            return "not yet supported by this implementation: a combined error set of another module "
                   "(SPEC-04 C9102)";
    }
    for (size_t i = 0; i < sizeof DIAG_TEXT / sizeof DIAG_TEXT[0]; i++) {
        if (code >= DIAG_TEXT[i].lo && code <= DIAG_TEXT[i].hi) return DIAG_TEXT[i].text;
    }
    return "compile error";
}

/* The note of diagnostic d, into out; 0 for none. The LS-312 note of C3004, C3010, C3011 and the
 * module-level initializer's C6004 (compiler/OPEN.md CINTC-OQ-14), and the position of the
 * keyword of a C4012 (the statement starts before it). */
int diag_note(const cint_compiler_diag *d, const char *file, char *out, size_t cap)
{
    int script = d->code == 3004 || d->code == 3010 || d->code == 3011 || d->code == 6004;
    if (script && d->detail[2] == NOTE_SCRIPT) {
        (void)snprintf(out, cap, "this module is a script because of the top-level statement at %s:%lld:%lld", file,
                       (long long)d->detail[0], (long long)d->detail[1]);
    } else if (script && d->detail[2] == NOTE_MODULE) {
        (void)snprintf(out, cap, "this file is a module, not a script, because every top-level item is a declaration (LS-218)");
    } else if (d->code == 4012 && d->detail[2] == NOTE_KEYWORD && d->detail[0] > 0) {
        (void)snprintf(out, cap, "the keyword is at %s:%lld:%lld", file, (long long)d->detail[0], (long long)d->detail[1]);
    } else {
        return 0;
    }
    return 1;
}
