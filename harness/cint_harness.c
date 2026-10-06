/* cint_harness.c: cint-harness, the conformance observer (SPEC-09 CONF-10;
 * plan Task 1.7).
 *
 *   cint-harness <program.(dll|so)> --entry <name> --clause <text>
 *       [--args <Type> <value> ...] [--fuel N] [--depth N] [--case <name>]
 *       [--source anchor|reference] [--format 1|2|3|4]
 *   cint-harness <program.(dll|so)> --cases <file> [--fuel N] [--depth N] [--format 1|2|3|4]
 *   either form also takes [--stdout-file PATH]
 *
 * It loads a compiled program, calls one exported function once in a fresh
 * cint-rt-3 context, and prints the outcome as `.expect` text (SPEC-09 9.2,
 * CONF-11): `outcome value` with the typed return value, `outcome fault` with
 * the record fields of SPEC-01 IM-106, or `outcome refused` when the call
 * cannot start (CONF-11 rule 7). Output is ASCII with LF line ends and holds
 * no host path; print output is counted and hashed (CONF-11), and written raw
 * to PATH by --stdout-file (each call replaces it; plan task 2.14, D-19).
 * --cases reads a CONF-12 case list; line n is call n in a fresh context,
 * printed as `case <n>` and the lines from `outcome` on (H-OQ-05). `--format 2`
 * adds the format 2 lines of CONF-11 (D-10), `state.global` from the optional
 * export `cint_observer_state` (rt/cint_rt.h; H-OQ-08); `--format 3` also. A
 * call that leaves with an error result (cint_ctx_error, rt/cint_rt.h 6c') is
 * `outcome error` with `error.set`, `error.value` and `error.tag` (CONF-11 rule
 * 12, RT-OQ-33) in every format; only format 3 files hold it. `--format 4` adds
 * the kernel form of rule 13 (box 09 ruling R9), the only form of a kernel fault.
 *
 * Defaults (SPEC-06 1.4): no fuel allowance, depth limit 256. `--fuel` takes
 * an I64 B >= 0 and `--depth` an I64 D >= 1, as cint_ref does; `--case`
 * defaults to the root module's path without `.ci`, `--source` to
 * `reference`. `--clause` is required (H-OQ-02). Arguments are `<Type>
 * <decimal>` or `Bool true|false` (CONF-12); a wrong count, type, or value is
 * a refusal (SPEC-03 H-12).
 *
 * Exit status: 0 when an outcome was printed; 2 for a malformed command line;
 * 3 when the program cannot be loaded or breaks the export contract; 4 when
 * the program or the runtime breaks the contract during the call, or the
 * output cannot be written. Only status 0 writes to standard output.
 *
 * Export contract: the observer interface cint-observe-1 (SPEC-09 CONF-13).
 * A case-list line reaches the entries of any module of the program; --entry
 * names one of the root module. No binary floating-point type.
 */
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <dlfcn.h>
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cint_rt.h"

#define H_MAX_ARGS 16u
#define H_MAX_NAME 255u  /* CONF-13 X-3, SPEC-04 LS-15 */
#define H_OUT_CAP 65536u
#define H_DEFAULT_DEPTH 256  /* SPEC-06 1.4 */

enum { EXIT_OUTCOME = 0, EXIT_USAGE = 2, EXIT_LOAD = 3, EXIT_BROKEN = 4 };

/* An integer as written on the command line: sign and magnitude; `big` when
 * the magnitude is 2^64 or more. */
typedef struct cli_int {
    uint64_t mag;
    bool negative;
    bool big;
} cli_int;

typedef struct cli_arg {
    uint32_t tag;
    cli_int value;  /* Bool: magnitude 0 or 1 */
} cli_arg;

typedef struct options {
    const char *program;
    const char *entry;
    const char *case_name;
    const char *clause;
    const char *source;
    const char *cases;
    const char *stdout_file;
    bool has_fuel;
    bool has_depth;
    cli_int fuel;
    cli_int depth;
    uint32_t nargs;
    cli_arg args[H_MAX_ARGS];
} options;

typedef struct program_symbols {
    const uint32_t *abi;
    const cint_observer *desc;
    const cint_program *program;
    const cint_observer_entry *entry;  /* NULL when the program has no such entry */
    const cint_state_table *state;     /* NULL when the program exports none */
} program_symbols;

typedef struct call_plan {
    int64_t fuel;
    int64_t depth;
    uint64_t args[H_MAX_ARGS];
} call_plan;

typedef struct out_text {
    size_t len;
    bool full;
    char bytes[H_OUT_CAP];
} out_text;

static out_text g_out;
static bool g_case_mode;
static int g_format = 1;                 /* --format: the CONF-11 format, 1 to 4 */
static cint_fault_record g_record;
static cint_fault_descriptor g_desc[2];  /* the record's view descriptors (format 4) */
static uint32_t g_desc_count;
static char g_reason[512];

typedef struct type_row {
    uint32_t tag;
    const char *name;
} type_row;

static const type_row k_types[9] = {
    {CINT_TAG_I8, "I8"}, {CINT_TAG_I16, "I16"}, {CINT_TAG_I32, "I32"}, {CINT_TAG_I64, "I64"},
    {CINT_TAG_U8, "U8"}, {CINT_TAG_U16, "U16"}, {CINT_TAG_U32, "U32"}, {CINT_TAG_U64, "U64"}, {CINT_TAG_BOOL, "Bool"},
};

/* SPEC-01 IM-104; E_DOMAIN and E_ASSERT by the plan's interim decisions. */
static const char *const k_codes[14] = {
    NULL, "E_OVERFLOW", "E_DIV_ZERO", "E_BOUNDS", "E_SHAPE", "E_SHIFT", "E_NARROW", "E_ALIAS",
    "E_STALE_HANDLE", "E_FUEL", "E_UNSUPPORTED", "E_DOMAIN", "E_DEPTH", "E_ASSERT",
};

/* ------------------------------------------------------------------------- */
/* Diagnostics and output.                                                    */

/* One call's print output (LS-193); more than 1 MiB fails and faults the entry. */
static uint8_t g_print[1048576];
static size_t g_print_len;

static cint_status capture(void *user, const uint8_t *bytes, size_t len)
{
    (void)user;
    if (len > sizeof g_print - g_print_len) {
        return CINT_RESOURCE;
    }
    memcpy(g_print + g_print_len, bytes, len);
    g_print_len += len;
    return CINT_OK;
}

static int fail(int status, const char *what, const char *detail)
{
    fprintf(stderr, "cint-harness: %s%s%s\n", what, detail != NULL ? ": " : "", detail != NULL ? detail : "");
    return status;
}

static void put_bytes(const char *s, size_t n)
{
    if (g_out.full || n > H_OUT_CAP - g_out.len) {
        g_out.full = true;
        return;
    }
    memcpy(g_out.bytes + g_out.len, s, n);
    g_out.len += n;
}

static void put_str(const char *s)
{
    put_bytes(s, strlen(s));
}

/* Formatted text; text that does not fit marks the output full. */
#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
static void put_fmt(const char *format, ...)
{
    char text[4096];
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(text, sizeof text, format, ap);
    va_end(ap);
    g_out.full = g_out.full || n < 0 || (size_t)n >= sizeof text;
    put_bytes(text, g_out.full ? 0u : (size_t)n);
}

/* `key value`: every value fits, the longest being an error set name (<= 1,300 bytes). */
static void put_line(const char *key, const char *value)
{
    put_fmt("%s %s\n", key, value);
}

/* A module path as ASCII. Module-relative paths are printable ASCII (SPEC-09
 * CINTC-12); any other byte is written as \xhh, so the output stays ASCII
 * (rt/OPEN.md H-OQ-06). */
static void put_path(const char *path, size_t n)
{
    for (size_t i = 0u; i < n; i++) {
        unsigned char c = (unsigned char)path[i];
        if (c >= 0x20u && c < 0x7fu) {
            put_bytes(path + i, 1u);
        } else {
            put_fmt("\\x%02x", (unsigned)c);
        }
    }
}

static int write_output(void)
{
    if (g_out.full) {
        return fail(EXIT_BROKEN, "the outcome text exceeds the output buffer", NULL);
    }
#if defined(_WIN32)
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) {
        return fail(EXIT_BROKEN, "cannot set standard output to binary mode", NULL);
    }
#endif
    if (fwrite(g_out.bytes, 1u, g_out.len, stdout) != g_out.len || (!g_case_mode && fflush(stdout) != 0)) {
        return fail(EXIT_BROKEN, "cannot write the outcome", NULL);
    }
    g_out.len = 0u;
    return EXIT_OUTCOME;
}

/* ------------------------------------------------------------------------- */
/* Types and values.                                                          */

/* The k_types row with this name, or with this tag when name is NULL. */
static const type_row *type_of(uint32_t tag, const char *name)
{
    for (size_t i = 0u; i < sizeof k_types / sizeof k_types[0]; i++) {
        if (name != NULL ? strcmp(k_types[i].name, name) == 0 : k_types[i].tag == tag) {
            return &k_types[i];
        }
    }
    return NULL;
}

/* [-]digits, in exact decimal. */
static bool parse_int(const char *s, cli_int *out)
{
    const char *p = s;
    *out = (cli_int){0u, false, false};
    if (*p == '-') {
        out->negative = true;
        p++;
    }
    if (*p == '\0') {
        return false;
    }
    for (; *p != '\0'; p++) {
        uint64_t d;
        if (*p < '0' || *p > '9') {
            return false;
        }
        d = (uint64_t)(*p - '0');
        if (out->big || out->mag > (UINT64_MAX - d) / 10u) {
            out->big = true;
        } else {
            out->mag = out->mag * 10u + d;
        }
    }
    out->negative = out->negative && (out->mag != 0u || out->big);
    return true;
}

/* The 64-bit pattern of v in type `tag` (X-5), or false when v is outside it. */
static bool pattern_of(const cli_int *v, uint32_t tag, uint64_t *bits)
{
    unsigned w;
    if (v->big) {
        return false;
    }
    if (tag == CINT_TAG_BOOL) {
        *bits = v->mag;
        return !v->negative && v->mag <= 1u;
    }
    w = cint_rt_tag_width(tag);
    if (cint_rt_tag_signed(tag)) {
        uint64_t half = (uint64_t)1 << (w - 1u);
        if (v->negative ? v->mag > half : v->mag >= half) {
            return false;
        }
        *bits = v->negative ? (uint64_t)0 - v->mag : v->mag;
        return true;
    }
    if (v->negative || (w < 64u && v->mag > ((uint64_t)1 << w) - 1u)) {
        return false;
    }
    *bits = v->mag;
    return true;
}

/* True when `bits` is a pattern of type `tag` in the encoding of X-5. */
static bool pattern_valid(uint64_t bits, uint32_t tag)
{
    unsigned w = tag == CINT_TAG_BOOL ? 1u : cint_rt_tag_width(tag);
    if (tag == CINT_TAG_BOOL || !cint_rt_tag_signed(tag)) {
        return w == 64u || bits >> w == 0u;
    }
    return (uint64_t)cint_rt_sext(bits, w) == bits;
}

static void make_tvalue(cint_tvalue *v, uint32_t tag, uint64_t bits)
{
    unsigned i;
    unsigned n = tag == CINT_TAG_BOOL ? 1u : cint_rt_tag_width(tag) / 8u;
    memset(v, 0, sizeof *v);
    v->bytes[0] = (uint8_t)tag;
    for (i = 0u; i < n; i++) {
        v->bytes[1u + i] = (uint8_t)(bits >> (8u * i));
    }
    v->len = (uint16_t)(1u + n);
}

/* ------------------------------------------------------------------------- */
/* Command line.                                                              */

/* 1 to max bytes, each a letter, a digit, or a byte of `extra` (when extra is
 * NULL, any printable ASCII byte), with no `edge` byte first or last. */
static bool valid_chars(const char *s, size_t max, const char *extra, char edge)
{
    size_t i, n = strlen(s);
    if (n == 0u || n > max || s[0] == edge || s[n - 1u] == edge) {
        return false;
    }
    for (i = 0u; i < n; i++) {
        char c = s[i];
        if (extra == NULL ? (c < ' ' || c > '~')
                          : !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                              strchr(extra, c) != NULL)) {
            return false;
        }
    }
    return true;
}

static bool valid_identifier(const char *s)
{
    return valid_chars(s, H_MAX_NAME, "_", '\0') && !(s[0] >= '0' && s[0] <= '9');
}

/* case-name of CONF-11: segments of [A-Za-z0-9_.-] separated by '/'. */
static bool valid_case_name(const char *s)
{
    return valid_chars(s, 1024u, "_.-/", '/') && strstr(s, "//") == NULL;
}

/* Appends the argument `<Type> <value>` (CONF-12). */
static int add_arg(options *o, const char *type, const char *value)
{
    const type_row *row = type_of(0u, type);
    if (o->nargs >= H_MAX_ARGS) {
        return fail(EXIT_USAGE, "at most 16 arguments", NULL);
    }
    if (row == NULL) {
        return fail(EXIT_USAGE, "unknown argument type (I8 to I64, U8 to U64, Bool)", type);
    }
    cli_arg *a = &o->args[o->nargs];
    a->tag = row->tag;
    if (a->tag == CINT_TAG_BOOL) {
        bool t = strcmp(value, "true") == 0;
        if (!t && strcmp(value, "false") != 0) {
            return fail(EXIT_USAGE, "a Bool argument is true or false", value);
        }
        a->value = (cli_int){t ? 1u : 0u, false, false};
    } else if (!parse_int(value, &a->value)) {
        return fail(EXIT_USAGE, "an integer argument is [-]digits in decimal", value);
    }
    o->nargs++;
    return 0;
}

static int parse_options(int argc, char **argv, options *o)
{
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (strncmp(a, "--", 2u) != 0) {
            if (o->program != NULL) {
                return fail(EXIT_USAGE, "more than one program library", a);
            }
            o->program = a;
            continue;
        }
        if (strcmp(a, "--args") == 0) {
            while (i + 1 < argc && strncmp(argv[i + 1], "--", 2u) != 0) {
                if (i + 2 >= argc || strncmp(argv[i + 2], "--", 2u) == 0) {
                    return fail(EXIT_USAGE, "--args takes <Type> <value> pairs", argv[i + 1]);
                }
                if (add_arg(o, argv[i + 1], argv[i + 2]) != 0) {
                    return EXIT_USAGE;
                }
                i += 2;
            }
            continue;
        }
        if (v == NULL) {
            return fail(EXIT_USAGE, "option needs a value", a);
        }
        i++;
        if (strcmp(a, "--entry") == 0) {
            o->entry = v;
        } else if (strcmp(a, "--fuel") == 0) {
            o->has_fuel = true;
            if (!parse_int(v, &o->fuel)) {
                return fail(EXIT_USAGE, "--fuel takes a decimal integer", v);
            }
        } else if (strcmp(a, "--depth") == 0) {
            o->has_depth = true;
            if (!parse_int(v, &o->depth)) {
                return fail(EXIT_USAGE, "--depth takes a decimal integer", v);
            }
        } else if (strcmp(a, "--case") == 0) {
            o->case_name = v;
        } else if (strcmp(a, "--clause") == 0) {
            o->clause = v;
        } else if (strcmp(a, "--source") == 0) {
            o->source = v;
        } else if (strcmp(a, "--cases") == 0) {
            o->cases = v;
        } else if (strcmp(a, "--stdout-file") == 0) {
            o->stdout_file = v;
        } else if (strcmp(a, "--format") == 0 && v[0] != '\0' && v[1] == '\0' && strchr("1234", v[0]) != NULL) {
            g_format = v[0] - '0';
        } else {
            return fail(EXIT_USAGE, "unknown option", a);
        }
    }
    if (o->cases != NULL && o->program != NULL && o->entry == NULL && o->nargs == 0u) {
        return 0;
    }
    if (o->program == NULL || o->entry == NULL || o->clause == NULL) {
        return fail(EXIT_USAGE, "usage: cint-harness <program.(dll|so)> --entry <name> --clause <text> "
                                "[--args <Type> <value> ...] [--fuel N] [--depth N] [--case <name>] "
                                "[--source anchor|reference], or <program> --cases <file> [--fuel N] [--depth N]",
                    NULL);
    }
    if (!valid_identifier(o->entry)) {
        return fail(EXIT_USAGE, "--entry takes a CINT identifier", o->entry);
    }
    if (o->case_name != NULL && !valid_case_name(o->case_name)) {
        return fail(EXIT_USAGE, "--case takes a case name of CONF-11", o->case_name);
    }
    if (!valid_chars(o->clause, 400u, NULL, ' ')) {  /* text of CONF-11 */
        return fail(EXIT_USAGE, "--clause takes printable ASCII text without edge spaces", o->clause);
    }
    if (o->source != NULL && strcmp(o->source, "anchor") != 0 && strcmp(o->source, "reference") != 0) {
        return fail(EXIT_USAGE, "--source takes anchor or reference", o->source);
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* The program library.                                                       */

#if defined(_WIN32)
/* A UTF-8 path as UTF-16, or NULL; the caller frees it. */
static wchar_t *wide_path(const char *path)
{
    wchar_t *wide;
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (n <= 0 || (wide = (wchar_t *)malloc((size_t)n * sizeof *wide)) == NULL) {
        return NULL;
    }
    (void)MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, n);
    return wide;
}
#endif

/* fopen(path, "rb" or "wb") for a UTF-8 path, on Windows through _wfopen. */
static FILE *open_file(const char *path, const char *mode)
{
#if defined(_WIN32)
    wchar_t *wide = wide_path(path);
    FILE *f = wide != NULL ? _wfopen(wide, mode[0] == 'w' ? L"wb" : L"rb") : NULL;
    free(wide);
    return f;
#else
    return fopen(path, mode);
#endif
}

/* The only function that touches the platform loader. It reads the two
 * symbols of CONF-13 X-2; entries are found through the descriptor. The
 * library is opened once and never unloaded. */
static int load_program(const char *path, program_symbols *out)
{
    static const char *const names[3] = {"cint_program_abi", "cint_observer_desc", "cint_observer_state"};
    void *found[3];
    int i;
#if defined(_WIN32)
    static HMODULE lib;
    wchar_t *wide, full[32768];
    DWORD n;
    if (lib != NULL) {
        goto opened;
    }
    if ((wide = wide_path(path)) == NULL) {
        return fail(EXIT_LOAD, "the program path is not UTF-8", NULL);
    }
    n = GetFullPathNameW(wide, 32768u, full, NULL);
    free(wide);
    /* Never the DLL search order: only this file, its directory and System32. */
    lib = n > 0u && n < 32768u ? LoadLibraryExW(full, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                                LOAD_LIBRARY_SEARCH_SYSTEM32) : NULL;
    if (lib == NULL) {
        return fail(EXIT_LOAD, "cannot load the program library", path);
    }
#else
    static void *lib;
    char rel[4096];
    if (lib != NULL) {
        goto opened;
    }
    if (strchr(path, '/') == NULL) {   /* dlopen would search the library path, never "." */
        if ((size_t)snprintf(rel, sizeof rel, "./%s", path) >= sizeof rel) {
            return fail(EXIT_LOAD, "the program path is too long", NULL);
        }
        path = rel;
    }
    lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (lib == NULL) {
        return fail(EXIT_LOAD, "cannot load the program library", dlerror());
    }
#endif
opened:
    for (i = 0; i < 3; i++) {
#if defined(_WIN32)
        FARPROC p = GetProcAddress(lib, names[i]);
        _Static_assert(sizeof p == sizeof found[i], "FARPROC is pointer-sized");
        memcpy(&found[i], &p, sizeof p);
#else
        found[i] = dlsym(lib, names[i]);
#endif
    }
    if (found[0] == NULL || found[1] == NULL) {
        return fail(EXIT_LOAD, "the library does not export cint_program_abi and cint_observer_desc (CONF-13 X-2)",
                    NULL);
    }
    out->abi = (const uint32_t *)found[0];
    out->desc = (const cint_observer *)found[1];
    out->state = (const cint_state_table *)found[2];
    return 0;
}

/* The observer entry `name` of module `module` (CONF-13 X-2, X-3), or NULL. */
static const cint_observer_entry *find_entry(const cint_observer *d, uint32_t module, const char *name)
{
    size_t n = strlen(name);
    for (uint32_t i = 0u; i < d->entry_count; i++) {
        const cint_observer_entry *e = &d->entries[i];
        if (e->module == module && e->name_len == n && e->name != NULL && memcmp(e->name, name, n) == 0) {
            return e;
        }
    }
    return NULL;
}

/* X-3: a result tag (0 or a known tag), n <= 16, n known parameter tags. */
static bool type_signature_valid(const uint32_t *sig)
{
    uint32_t i;
    bool ok = (sig[0] == 0u || type_of(sig[0], NULL) != NULL) && sig[1] <= H_MAX_ARGS;
    for (i = 0u; ok && i < sig[1]; i++) {
        ok = type_of(sig[2u + i], NULL) != NULL;
    }
    return ok;
}

/* ------------------------------------------------------------------------- */
/* The outcome.                                                               */

static void put_header(const options *o, const cint_program *program, bool refused)
{
    if (g_case_mode) {
        return;
    }
    put_str("case ");
    if (o->case_name != NULL) {
        put_str(o->case_name);
    } else {
        const cint_module *m = program->modules[0];
        size_t n = m->path_len;
        put_path(m->path, n > 3u && memcmp(m->path + n - 3u, ".ci", 3u) == 0 ? n - 3u : n);
    }
    put_bytes("\n", 1u);
    put_line("clause", o->clause);
    if (g_format >= 2) {
        put_fmt("format %d\n", g_format);
    }
    put_line("source", refused || o->source == NULL ? "reference" : o->source);
}

static int refuse(const options *o, const cint_program *program, const char *reason)
{
    put_header(o, program, true);
    put_line("outcome", "refused");
    put_line("refused.reason", reason);
    return write_output();
}

/* `key <typed value>`, or `key none` when v is NULL; a decimal value is an
 * exact, which is Z (SPEC-01 IM-106). */
static int put_tvalue(const char *key, const cint_tvalue *v, bool decimal)
{
    char text[720] = "none";
    size_t n = v == NULL ? 4u : decimal ? cint_tvalue_render_decimal(v, text, sizeof text)
                                        : cint_tvalue_render(v, text, sizeof text);
    if (n == 0u || n >= sizeof text || (decimal && v != NULL && v->bytes[0] != CINT_TAG_Z)) {
        return fail(EXIT_BROKEN, "a typed value does not render", key);
    }
    put_line(key, text);
    return 0;
}

/* `key path:line:column` for a site of the record's program (DIAG-01). */
static int put_position(const char *key, const cint_program *program, cint_site site)
{
    cint_position pos;
    if (!cint_site_resolve(program, site, &pos) || pos.path_len == 0u || pos.line == 0u || pos.column == 0u) {
        return fail(EXIT_BROKEN, "a fault position does not resolve through the site tables", key);
    }
    put_fmt("%s ", key);
    put_path(pos.path, pos.path_len);
    put_fmt(":%lu:%lu\n", (unsigned long)pos.line, (unsigned long)pos.column);
    return 0;
}

/* kernel-name of CONF-11: two or more CONF-12 names joined by '.'; s's dots become NUL bytes. */
static bool valid_kernel_name(char *s, unsigned dots)
{
    char *dot = strchr(s, '.');
    if (dot == NULL) {
        return dots > 0u && valid_identifier(s);
    }
    *dot = '\0';
    return valid_identifier(s) && valid_kernel_name(dot + 1, dots + 1u);
}

/* The kernel lines of a fault raised by a kernel dispatch in format 4 (CONF-11
 * rule 13; SPEC-02 F-3, F-8): a work item and step in phase work-item only. */
static int put_kernel(const cint_fault_address *a)
{
    static const char *const phases[3] = {"entry", "work-item", "epilogue"};
    char name[257] = "";
    memcpy(name, a->name, a->name_len <= 256u ? a->name_len : 0u);
    if (a->phase > 2u || (a->has_work_item != 0u) != (a->phase == 1u) || a->dispatch < 0 || a->work_item < 0 ||
        a->step < 0 || strlen(name) != a->name_len || !valid_kernel_name(name, 0u)) {
        return fail(EXIT_BROKEN, "the kernel fault address has no .expect form (CONF-11 rule 13)", NULL);
    }
    put_fmt("fault.phase %s\nfault.kernel %.*s\n", phases[a->phase], (int)a->name_len, a->name);
    if (a->phase == 1u) {
        put_fmt("fault.address %lld %lld %lld\n", (long long)a->dispatch, (long long)a->work_item, (long long)a->step);
    } else {
        put_fmt("fault.address %lld none none\n", (long long)a->dispatch);
    }
    return 0;
}

/* The fault lines of CONF-11 for the record r (SPEC-01 IM-106, IM-107). */
static int put_fault(const cint_fault_record *r, const cint_program *program)
{
    uint32_t i;
    if (r->code >= sizeof k_codes / sizeof k_codes[0] || k_codes[r->code] == NULL) {
        return fail(EXIT_BROKEN, "the fault record has an unknown code", NULL);
    }
    put_line("fault.code", k_codes[r->code]);
    if (r->operation_len == 0u || r->operation_len > CINT_FAULT_MAX_OPERATION ||
        r->operand_count > CINT_FAULT_MAX_OPERANDS || r->stack_count > CINT_RT_MAX_DEPTH) {
        return fail(EXIT_BROKEN, "the fault record has an invalid operation, operand or stack count", NULL);
    }
    for (i = 0u; i < r->operation_len; i++) {
        char c = r->operation[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.')) {
            return fail(EXIT_BROKEN, "the fault record's operation is not an identifier of SPEC-01 IM-130", NULL);
        }
    }
    put_fmt("fault.operation %.*s\n", (int)r->operation_len, r->operation);
    for (i = 0u; i < r->operand_count; i++) {
        if (put_tvalue("fault.operand", &r->operands[i], false) != 0) {
            return EXIT_BROKEN;
        }
    }
    for (i = 0u; g_format == 4 && i < g_desc_count; i++) {  /* rule 13: element type to permission */
        const cint_fault_descriptor *d = &g_desc[i];
        const type_row *t = type_of(d->elem, NULL);
        if (t == NULL || d->elem == CINT_TAG_BOOL || d->rank < 1 || d->rank > 4 || d->write > 1u) {
            return fail(EXIT_BROKEN, "a fault descriptor has no .expect form (CONF-11 rule 13)", NULL);
        }
        put_fmt("fault.descriptor %s none %lld %lld", t->name, (long long)d->rank, (long long)d->origin);
        for (int64_t k = 0; k < d->rank; k++) {
            put_fmt(" %lld 0 %lld", (long long)d->shape[k], (long long)d->stride[k]);
        }
        put_str(d->write != 0u ? " write\n" : " read\n");
    }
    if (put_tvalue("fault.exact", r->has_exact ? &r->exact : NULL, true) != 0 ||
        put_tvalue("fault.limit", r->has_limit ? &r->limit : NULL, false) != 0) {
        return EXIT_BROKEN;
    }
    program = r->program != NULL ? r->program : program;
    if (put_position("fault.position", program, r->position) != 0) {
        return EXIT_BROKEN;
    }
    put_line("fault.revision", "self");
    if (g_format >= 2) {
        put_line("fault.source-map", "self");
    }
    if (r->has_address && g_format < 4) {  /* as cint_ref: the kernel form exists in format 4 only */
        return fail(EXIT_BROKEN, "a fault raised by a kernel dispatch is written in format 4 (CONF-11 rule 13)", NULL);
    }
    if (!r->has_address) {
        put_line("fault.address", "none");
    } else if (put_kernel(&r->address) != 0) {
        return EXIT_BROKEN;
    }
    put_fmt("fault.stack-depth %lu\n", (unsigned long)r->stack_count);
    for (i = 0u; g_format >= 2 && i < r->stack_count; i++) {
        if (put_position("fault.stack", program, r->stack[i]) != 0) {
            return EXIT_BROKEN;
        }
    }
    return 0;
}

/* Checks the call's budgets and arguments in the order cint_ref does (fuel,
 * depth, entry, argument count, then each argument's type and range). Returns
 * NULL with the call planned, or the reason for refusing it. */
static const char *plan_call(const options *o, const program_symbols *s, call_plan *c)
{
    uint64_t bits;
    c->fuel = CINT_FUEL_UNBOUNDED;
    c->depth = H_DEFAULT_DEPTH;
    if (o->has_fuel) {
        if (!pattern_of(&o->fuel, CINT_TAG_I64, &bits) || o->fuel.negative) {
            return "the fuel allowance must be an I64 with B >= 0 (SPEC-01 10.1)";
        }
        c->fuel = (int64_t)bits;
    }
    if (o->has_depth) {
        if (!pattern_of(&o->depth, CINT_TAG_I64, &bits) || o->depth.negative || bits == 0u) {
            return "the depth limit must be an I64 with D >= 1 (SPEC-01 9.4)";
        }
        c->depth = (int64_t)bits;
    }
    if (s->entry == NULL) {
        (void)snprintf(g_reason, sizeof g_reason, "the program exports no function `%s`", o->entry);
        return g_reason;
    }
    if (s->entry->types[1] != o->nargs) {
        (void)snprintf(g_reason, sizeof g_reason, "`%s` takes %u arguments, %u given", o->entry,
                       (unsigned)s->entry->types[1], (unsigned)o->nargs);
        return g_reason;
    }
    for (uint32_t i = 0u; i < o->nargs; i++) {
        uint32_t want = s->entry->types[2u + i];
        if (o->args[i].tag != want || !pattern_of(&o->args[i].value, want, &c->args[i])) {
            (void)snprintf(g_reason, sizeof g_reason, o->args[i].tag != want ? "argument %u must be a %s value"
                           : "argument %u is outside %s", (unsigned)(i + 1u), type_of(want, NULL)->name);
            return g_reason;
        }
    }
    return NULL;
}

/* One call in a fresh context, and its outcome (CONF-10). */
static int call_entry(const options *o, const program_symbols *s, const call_plan *c)
{
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL;
    cint_status st;
    uint64_t result = 0u, etag = 0u;
    int64_t fuel_used = 0;
    uint32_t rtag = s->entry->types[0];
    int status = 0;
    const cint_error_set *e = NULL;
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = s->program;
    cfg.output = capture;
    g_print_len = 0u;
    if (cint_ctx_create(&cfg, &ctx) != CINT_OK) {
        return fail(EXIT_BROKEN, "cint_ctx_create refused the program descriptor", NULL);
    }
    st = s->entry->call(ctx, c->fuel, c->depth, c->args, &result);
    if (st == CINT_REFUSED) {
        cint_ctx_destroy(ctx);
        return refuse(o, s->program, "the entry refused the call (SPEC-03 H-12)");
    }
    if ((st != CINT_OK && st != CINT_FAULT) || cint_ctx_fault(ctx, &g_record) != CINT_OK ||
        cint_ctx_fault_descriptors(ctx, g_desc, &g_desc_count) != CINT_OK ||
        cint_fuel_consumed(ctx, &fuel_used) != CINT_OK || fuel_used < 0 || cint_ctx_error(ctx, &e, &etag) != CINT_OK) {
        status = fail(EXIT_BROKEN, "the entry returned an unexpected status, or the context is busy", NULL);
    } else if ((st == CINT_FAULT) != (g_record.code != 0u)) {
        status = fail(EXIT_BROKEN, "the entry's status and its context's fault record disagree", NULL);
    } else if (e != NULL && (e->name == NULL || e->values == NULL || etag == 0u || etag > e->count ||
                             e->values[etag - 1u] == NULL || !valid_chars(e->name, 1300u, "_.", '.') ||
                             !valid_chars(e->values[etag - 1u], 1300u, "_.", '.') || e->tag < CINT_TAG_U8 ||
                             e->tag > CINT_TAG_U64 || !pattern_valid(etag, e->tag))) {
        status = fail(EXIT_BROKEN, "the entry's error result has no valid error set (rt/cint_rt.h 6c')", NULL);
    } else if (st == CINT_OK && rtag != 0u && !pattern_valid(result, rtag)) {
        status = fail(EXIT_BROKEN, "the entry returned a pattern outside its result type",
                      type_of(rtag, NULL)->name);
    } else {
        put_header(o, s->program, false);
        put_line("outcome", e != NULL ? "error" : st == CINT_OK ? "value" : "fault");
        put_fmt("stdout-bytes %llu\n", (unsigned long long)g_print_len);
        if (g_print_len != 0u) {
            uint8_t digest[32];
            cint_sha256(g_print, g_print_len, digest);
            put_str("stdout-sha256 ");
            for (unsigned i = 0u; i < 32u; i++) {
                put_fmt("%02x", (unsigned)digest[i]);
            }
            put_bytes("\n", 1u);
        }
        cint_tvalue v;
        if (st == CINT_OK && rtag != 0u && e == NULL) {
            make_tvalue(&v, rtag, result);
            status = put_tvalue("return", &v, false);
        }
        put_fmt("fuel-consumed %lld\n", (long long)fuel_used);
        if (e != NULL) {   /* CONF-11 rule 12 */
            put_fmt("error.set %s\nerror.value %s\n", e->name, e->values[etag - 1u]);
            make_tvalue(&v, e->tag, etag);
            status = put_tvalue("error.tag", &v, false);
        }
        if (status == 0 && st == CINT_FAULT) {
            status = put_fault(&g_record, s->program);
        }
        size_t n = g_format >= 2 && s->state != NULL && !g_out.full
                       ? cint_state_render(ctx, s->state, g_out.bytes + g_out.len, H_OUT_CAP - g_out.len) : 0u;
        g_out.full = g_out.full || n == SIZE_MAX;
        g_out.len += n == SIZE_MAX ? 0u : n;
    }
    if (status == 0 && o->stdout_file != NULL) {
        FILE *f = open_file(o->stdout_file, "wb");
        size_t n = f != NULL ? fwrite(g_print, 1u, g_print_len, f) : 0u;
        if (f == NULL || (fclose(f) != 0) + (n != g_print_len) != 0) {
            status = fail(EXIT_BROKEN, "cannot write the --stdout-file", o->stdout_file);
        }
    }
    cint_ctx_destroy(ctx);
    return status != 0 ? status : write_output();
}

/* In case mode, the line's module against a module of the program: `a/b.ci`
 * is `a.b` (SPEC-04 LS-225). */
static bool module_matches(const char *name, const cint_module *m)
{
    size_t i, n = m->path_len > 3u && memcmp(m->path + m->path_len - 3u, ".ci", 3u) == 0 ? m->path_len - 3u
                                                                                       : m->path_len;
    for (i = 0u; i < n && name[i] == (m->path[i] == '/' ? '.' : m->path[i]); i++) {
    }
    return i == n && name[n] == '\0';
}

static int run_one(options *op)
{
    const options o = *op;
    program_symbols s;
    call_plan c;
    const char *reason;
    uint32_t module = 0u, i;
    int status;
    memset(&s, 0, sizeof s);
    status = load_program(o.program, &s);
    if (status != 0) {
        return status;
    }
    if (*s.abi != CINT_ABI_VERSION) {
        return fail(EXIT_LOAD, "the program was built for another cint-rt ABI version (X-2)", NULL);
    }
    if (s.desc->reserved != 0u || s.desc->program == NULL || (s.desc->entry_count > 0u && s.desc->entries == NULL)) {
        return fail(EXIT_LOAD, "the observer descriptor is malformed (CONF-13 X-2)", NULL);
    }
    s.program = s.desc->program;
    if ((o.case_name == NULL || g_case_mode) &&
        (s.program->module_count == 0u || s.program->modules == NULL || s.program->modules[0] == NULL ||
         s.program->modules[0]->path == NULL || s.program->modules[0]->path_len == 0u)) {
        return fail(EXIT_LOAD, "the program has no root module path; give --case", NULL);
    }
    while (g_case_mode && module < s.program->module_count &&
           !(s.program->modules[module] != NULL && s.program->modules[module]->path != NULL &&
             module_matches(o.case_name, s.program->modules[module]))) {
        module++;
    }
    if (g_case_mode && module == s.program->module_count) {
        return refuse(&o, s.program, "the case names a module that the program does not contain");
    }
    for (i = module + 1u; g_case_mode && i < s.program->module_count; i++) {
        if (s.program->modules[i] != NULL && s.program->modules[i]->path != NULL &&
            module_matches(o.case_name, s.program->modules[i])) {   /* LS-225: one name, one module */
            return refuse(&o, s.program, "the case names a module that matches more than one module of the program");
        }
    }
    s.entry = find_entry(s.desc, module, o.entry);
    if (s.entry != NULL && (s.entry->types == NULL || s.entry->call == NULL || !type_signature_valid(s.entry->types))) {
        return fail(EXIT_LOAD, "the entry has no valid type signature or function (CONF-13 X-3)", o.entry);
    }
    memset(&c, 0, sizeof c);
    reason = plan_call(&o, &s, &c);
    if (reason != NULL) {
        return refuse(&o, s.program, reason);
    }
    return call_entry(&o, &s, &c);
}

/* The case list of CONF-12: each line is one call in a fresh context. */
static int run_cases(options *o)
{
    static char line[4096];
    FILE *f = open_file(o->cases, "rb");
    uint64_t n = 0u;
    int status = 0;
    if (f == NULL) {
        return fail(EXIT_USAGE, "cannot read the case list", o->cases);
    }
    g_case_mode = true;
    while (status == 0 && fgets(line, (int)sizeof line, f) != NULL) {
        char *word[2 + 2 * H_MAX_ARGS + 1];
        char *p = line;
        size_t w = 0u, k, len = strlen(line);
        if (len == 0u || line[len - 1u] != '\n') {
            status = fail(EXIT_USAGE, feof(f) ? "the case list's last line has no LF (CONF-12)"
                                              : "a case-list line is longer than 4 KiB or holds a NUL byte", NULL);
            break;
        }
        line[len - 1u] = '\0';
        do {
            word[w++] = p;
            if ((p = strchr(p, ' ')) != NULL) {
                *p++ = '\0';
            }
        } while (p != NULL && w < sizeof word / sizeof word[0]);
        n++;
        if (p != NULL || w < 2u || w % 2u != 0u || !valid_identifier(word[1])) {
            status = fail(EXIT_USAGE, "a case-list line is not `<module> <function> <typed values>`", word[0]);
            break;
        }
        o->entry = word[1];
        o->nargs = 0u;
        for (k = 2u; k < w && status == 0; k += 2u) {
            status = add_arg(o, word[k], word[k + 1u]);
        }
        o->case_name = word[0];
        put_fmt("case %llu\n", (unsigned long long)n);
        if (status == 0) {
            status = run_one(o);
        }
    }
    (void)fclose(f);
    if (status == 0 && fflush(stdout) != 0) {
        status = fail(EXIT_BROKEN, "cannot write the outcome", NULL);
    }
    return status;
}

static int run(int argc, char **argv)
{
    static options o;
    int status = parse_options(argc, argv, &o);
    return status != 0 ? status : o.cases != NULL ? run_cases(&o) : run_one(&o);
}

#if defined(_WIN32)
/* Windows passes the command line as UTF-16; the harness works in UTF-8. */
int wmain(int argc, wchar_t **wargv)
{
    char **argv = (char **)calloc((size_t)argc + 1u, sizeof *argv);
    int i, status = EXIT_USAGE;
    if (argv == NULL) {
        return fail(EXIT_BROKEN, "out of memory", NULL);
    }
    for (i = 0; i < argc; i++) {
        int n = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, NULL, 0, NULL, NULL);
        if (n <= 0 || (argv[i] = (char *)malloc((size_t)n)) == NULL) {
            break;
        }
        (void)WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, argv[i], n, NULL, NULL);
    }
    status = i == argc ? run(argc, argv) : fail(EXIT_USAGE, "cannot convert the command line to UTF-8", NULL);
    for (i = 0; i < argc; i++) {
        free(argv[i]);
    }
    free(argv);
    return status;
}
#else
int main(int argc, char **argv)
{
    return run(argc, argv);
}
#endif
