/* wb_c17.c: cpu-c17 through libraries (box 14 unit 5; wb_c17.h).
 *
 * Loads what `cint build --lib` writes, reads its reflection table as the Python bridge does
 * (python/cint/_abi.py read_library), and reaches the library's runtime only through the host
 * functions it exports (SPEC-03 5.3; BX10-07).
 */
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define _CRT_SECURE_NO_WARNINGS 1
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <stdlib.h>
#include <string.h>

#include "cint_proc.h"
#include "cint_state.h"
#include "wb_c17.h"

#define MODULES_MAX 2048u      /* the CLI's limit on a program's modules */
#define EXPORTS_MAX 65536u     /* rows in one module table */
#define NAME_LEN_MAX 1024u
#define SIGNATURE_MAX 1048576u

struct wb_c17 {
    void *handle;
    const cint_program *program;
    const cint_module_info *root;
    const cint_state_table *state;   /* cint_observer_state, or NULL for a program without state */
    uint32_t nexp;
    wb_c17_export *exp;
    /* The host functions of the library's runtime. */
    uint32_t (*abi_version)(void);
    cint_status (*ctx_create)(const cint_ctx_config *, cint_ctx **);
    void (*ctx_destroy)(cint_ctx *);
    cint_status (*fault_get)(const cint_ctx *, uint8_t *, size_t, size_t *);
    cint_status (*fuel_consumed)(const cint_ctx *, int64_t *);
    cint_status (*ctx_refusal)(const cint_ctx *, uint32_t *);
    cint_status (*clear_fault)(cint_ctx *);
    /* rt/cint_state.h, which a library built by wb_c17_build carries; NULL in one built without it. */
    cint_status (*bind_state)(cint_ctx *, const cint_state_table *);
    cint_status (*checkpoint)(cint_ctx *, cint_sink *);
    cint_status (*restore)(cint_ctx *, const uint8_t *, size_t);
    cint_status (*state_hash)(cint_ctx *, uint8_t[32]);
};

/* a, the path separator, and b; malloc'd, or NULL. */
static char *join(const char *a, const char *b)
{
    wb_buf p = {0};
#if defined(_WIN32)
    wb_f(&p, "%s\\%s", a, b);
#else
    wb_f(&p, "%s/%s", a, b);
#endif
    if (p.failed) wb_free(&p);
    return p.p;
}

/* ------------------------------------------------------------------------- */
/* The platform: one file name to one handle, and one symbol to one address.  */

#if defined(_WIN32)
static void *load(const char *path, wb_buf *err)
{
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    wchar_t *w = n > 0 ? malloc((size_t)n * sizeof *w) : NULL, *full = NULL;
    HMODULE h = NULL;
    if (w != NULL && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, w, n) == n) {
        /* A full path, so the loader takes this file and searches nothing for it. */
        DWORD m = GetFullPathNameW(w, 0, NULL, NULL);
        full = m > 0 ? malloc((size_t)m * sizeof *full) : NULL;
        if (full != NULL && GetFullPathNameW(w, m, full, NULL) < m) h = LoadLibraryW(full);
    }
    if (h == NULL) wb_f(err, "cannot load %s (Windows error %lu)", path, (unsigned long)GetLastError());
    free(full);
    free(w);
    return (void *)h;
}

static void *address(void *handle, const char *name)
{
    FARPROC f = GetProcAddress((HMODULE)handle, name);
    void *p = NULL;
    _Static_assert(sizeof f == sizeof p, "a symbol's address fits a pointer");
    memcpy(&p, &f, sizeof p);
    return p;
}

static void unload(void *handle)
{
    (void)FreeLibrary((HMODULE)handle);
}
#else
static void *load(const char *path, wb_buf *err)
{
    /* A name without a slash would send dlopen searching the library path. */
    char *p = strchr(path, '/') != NULL ? NULL : join(".", path);
    void *h = dlopen(p != NULL ? p : path, RTLD_NOW | RTLD_LOCAL);
    if (h == NULL) {
        const char *why = dlerror();
        wb_f(err, "cannot load %s: %s", path, why != NULL ? why : "dlopen failed");
    }
    free(p);
    return h;
}

static void *address(void *handle, const char *name)
{
    return dlsym(handle, name);
}

static void unload(void *handle)
{
    (void)dlclose(handle);
}
#endif

/* A function's address into a function pointer of any type; ISO C has no cast between them. */
_Static_assert(sizeof(cint_entry_fn) == sizeof(void *), "a function's address fits a pointer");
#define BIND(fp, handle, name) (memcpy(&(fp), &(void *){address((handle), (name))}, sizeof(fp)), (fp) != NULL)

/* ------------------------------------------------------------------------- */
/* Type signatures (SPEC-03 A-20).                                            */

static const char DOMAIN[] = "cint-core-1/type-signature/v1";

static int scalar(uint32_t tag)
{
    return tag == CINT_TAG_BOOL || (tag >= CINT_TAG_I8 && tag <= CINT_TAG_I64) ||
           (tag >= CINT_TAG_U8 && tag <= CINT_TAG_U64);
}

static const char *form(uint32_t tag)
{
    switch (tag) {
    case 0x15: case 0x16: case 0x17: case 0x18: return "a wide integer";
    case 0x31: return "a fixed-point value";
    case 0x41: case 0x42: return "a ternary value";
    case 0x52: return "an array";
    case 0x71: return "a struct";
    case 0x72: return "an enum";
    case 0x73: return "an error-set value";
    case 0x74: return "an error union";
    case 0x76: return "a tuple";
    default: return "a type this workbench does not know";
    }
}

static uint32_t u32_at(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Fills e's parameters and result from the n bytes of its type signature, or e->why. */
static void parse_signature(wb_c17_export *e, const uint8_t *p, uint64_t n, uint32_t row_params)
{
    uint64_t at = 4u + (sizeof DOMAIN - 1u) + 12u;
    const char *why = "its type signature is malformed", *what = "";
    int ours = n >= at && u32_at(p) == sizeof DOMAIN - 1u && memcmp(p + 4, DOMAIN, sizeof DOMAIN - 1u) == 0;
    if (ours && u32_at(p + at - 12u) != 0u) {   /* a type table moves the counts that follow it */
        why = "it has a struct, enum, or error set";
    } else if (ours && (e->nparam = u32_at(p + at - 4u)) == row_params) {
        if (u32_at(p + at - 8u) != 0u) {
            why = "it has size parameters";
        } else if (e->nparam > WB_C17_PARAMS_MAX) {
            why = "it has more parameters than a call carries";
        } else {
            why = NULL;
            for (uint32_t i = 0; i < e->nparam && why == NULL; i++, at += 2u) {
                if (at + 2u > n) {
                    why = "its type signature is malformed";
                } else if (p[at] != 0u || !scalar(p[at + 1u])) {
                    why = "it takes ";
                    what = p[at] == 1u ? "an inout parameter" : p[at] == 2u ? "an out parameter" : form(p[at + 1u]);
                } else {
                    e->param[i] = p[at + 1u];
                }
            }
            if (why == NULL && at + 1u != n) {
                why = "its type signature is malformed";
            } else if (why == NULL && p[at] != 0u && !scalar(p[at])) {
                why = "it returns ";
                what = form(p[at]);
            } else if (why == NULL) {
                e->result = p[at];
            }
        }
    }
    if (why != NULL) (void)snprintf(e->why, sizeof e->why, "%s%s", why, what);
}

static const char *tag_name(uint32_t tag)
{
    static const char *const signed_names[4] = {"I8", "I16", "I32", "I64"};
    static const char *const unsigned_names[4] = {"U8", "U16", "U32", "U64"};
    return tag == CINT_TAG_BOOL ? "Bool" : tag >= CINT_TAG_U8 ? unsigned_names[tag - CINT_TAG_U8] :
                                                               signed_names[tag - CINT_TAG_I8];
}

void wb_c17_signature(wb_buf *b, const wb_c17_export *e)
{
    if (e->why[0] != '\0') {
        wb_s(b, "(...)");
        return;
    }
    wb_s(b, "(");
    for (uint32_t i = 0; i < e->nparam; i++) wb_f(b, "%s%s", i == 0 ? "" : ", ", tag_name(e->param[i]));
    wb_f(b, ") -> %s", e->result != 0u ? tag_name(e->result) : "void");
}

/* ------------------------------------------------------------------------- */
/* The library runtime with checkpoints.                                      */

/* The bytes of the file at path, NUL-terminated and malloc'd, or NULL. */
static char *slurp(const char *path, size_t *len)
{
    FILE *f = cli_fopen(path, "rb");
    wb_buf b = {0};
    char chunk[4096];
    size_t n;
    if (f == NULL) return NULL;
    while ((n = fread(chunk, 1u, sizeof chunk, f)) > 0u) wb_put(&b, chunk, n);
    if (ferror(f) != 0 || b.failed) wb_free(&b);
    fclose(f);
    *len = b.n;
    return b.p;
}

static int spill(const char *path, const char *text, size_t n)
{
    FILE *f = cli_fopen(path, "wb");
    int ok = f != NULL && fwrite(text, 1u, n, f) == n;
    return f != NULL && fclose(f) == 0 && ok ? 0 : -1;
}

static int exists(const char *path)
{
    FILE *f = path != NULL ? cli_fopen(path, "rb") : NULL;
    return f != NULL && fclose(f) == 0;
}

/* A toolchain file (`cint-toolchain-1`, then `key value` rows; tools/cint_bootstrap.py). */
typedef struct tc_file {
    char *text, *rows;                  /* the file, and a copy split into rows in place */
    const char *cc, *include, *cache, *leg, *version;
    int msvc;
    const char *argv[72];               /* the compiler, then its flags */
    size_t n;
    uint64_t key;                       /* FNV-1a of the file */
} tc_file;

/* Reads the toolchain file beside `cint` and sets the environment its env rows name, as cint
 * does, so the compiler finds its own. 0, or -1 when it cannot be read or lacks a row. */
static int read_toolchain(const char *cint, tc_file *t)
{
    size_t len = 0, cut = strlen(cint);
    while (cut > 0u && cint[cut - 1u] != '/' && cint[cut - 1u] != '\\') cut--;
    wb_buf path = {0};
    wb_f(&path, "%.*scint.toolchain", (int)cut, cint);
    t->text = path.p != NULL ? slurp(path.p, &len) : NULL;
    wb_free(&path);
    if (t->text == NULL || len < 17u || memcmp(t->text, "cint-toolchain-1\n", 17u) != 0 ||
        (t->rows = malloc(len + 1u)) == NULL) {
        return -1;
    }
    memcpy(t->rows, t->text, len + 1u);
    t->key = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < len; i++) t->key = (t->key ^ (uint8_t)t->text[i]) * UINT64_C(1099511628211);
    t->n = 1;
    for (char *line = t->rows + 17, *next, *v; *line != '\0'; line = next) {
        next = strchr(line, '\n');
        next = next != NULL ? (*next = '\0', next + 1) : line + strlen(line);
        if ((v = strchr(line, ' ')) == NULL) continue;
        *v++ = '\0';
        if (strcmp(line, "flag") == 0 && t->n < 64u) {
            t->argv[t->n++] = v;
        } else if (strcmp(line, "env") == 0 && strchr(v, '=') != NULL) {
            char *eq = strchr(v, '=');
            *eq = '\0';
            (void)cli_setenv(v, eq + 1);
        }
        t->cc = strcmp(line, "cc") == 0 ? v : t->cc;
        t->include = strcmp(line, "include") == 0 ? v : t->include;
        t->cache = strcmp(line, "cache") == 0 ? v : t->cache;
        t->msvc = strcmp(line, "leg") == 0 ? strcmp(v, "msvc") == 0 : t->msvc;
        t->leg = strcmp(line, "leg") == 0 ? v : t->leg;
        t->version = strcmp(line, "cc_version") == 0 ? v : t->version;
    }
    return t->cc != NULL && t->include != NULL && t->cache != NULL ? 0 : -1;
}

/* In the new directory work: the runtime unit compiled, and a toolchain file that names the
 * object at obj, where it lands when work becomes its final directory. 0, or -1 with err set. */
static int make_runtime(tc_file *t, const char *work, const char *obj, wb_buf *err)
{
    static const char unit[] = "#define _CRT_SECURE_NO_WARNINGS 1\n#include \"cint_rt.c\"\n#include \"cint_state.c\"\n"
                               "#include \"cint_rec.c\"\n";
    char *src = join(work, "cint_rt_state.c"), *log = join(work, "cc.log"), *file = join(work, "cint.toolchain");
    char *o = join(work, t->msvc ? "cint_rt_state.obj" : "cint_rt_state.o");
    wb_buf inc = {0}, out = {0}, tc = {0};
    int status = -1, r = -1;
    size_t n = t->n;
    wb_f(&inc, "%s%s", t->msvc ? "/I" : "-I", t->include);
    wb_f(&out, "%s%s", t->msvc ? "/Fo" : "", o != NULL ? o : "");
    t->argv[0] = t->cc;
    t->argv[n++] = t->msvc ? "/DCINT_RT_LIBRARY" : "-DCINT_RT_LIBRARY";
    t->argv[n++] = inc.p;
    t->argv[n++] = t->msvc ? "/c" : "-c";
    t->argv[n++] = src;
    if (!t->msvc) t->argv[n++] = "-o";
    t->argv[n++] = out.p;
    t->argv[n] = NULL;
    cli_proc proc = {t->argv, log, NULL, NULL, NULL, NULL, NULL, 0};
    wb_s(&tc, "cint-toolchain-1\n");
    for (const char *line = t->text + 17, *end; *line != '\0'; line = *end != '\0' ? end + 1 : end) {
        end = strchr(line, '\n') != NULL ? strchr(line, '\n') : line + strlen(line);
        if (strncmp(line, "runtime_library_object ", 23u) == 0) wb_f(&tc, "runtime_library_object %s\n", obj);
        else wb_f(&tc, "%.*s\n", (int)(end - line), line);
    }
    if (src == NULL || log == NULL || file == NULL || o == NULL || inc.failed || out.failed || tc.failed ||
        spill(src, unit, sizeof unit - 1u) != 0) {
        wb_f(err, "cannot write the library runtime's files in %s", work);
    } else if (cli_proc_run(&proc, &status) != 0 || status != 0) {
        wb_f(err, "%s cannot compile the library runtime with rt/cint_state.c and rt/cint_rec.c; see %s", t->cc, log);
    } else if (spill(file, tc.p, tc.n) != 0) {
        wb_f(err, "cannot write %s", file);
    } else {
        r = 0;
    }
    free(src);
    free(log);
    free(file);
    free(o);
    wb_free(&inc);
    wb_free(&out);
    wb_free(&tc);
    return r;
}

/* The toolchain file beside `cint` with its library runtime, rt/cint_rt.c alone
 * (tools/cint_bootstrap.py), replaced by rt/cint_rt.c, rt/cint_state.c, and rt/cint_rec.c
 * compiled as one unit with CINT_RT_LIBRARY, as the Python bridge builds its libraries
 * (python/OPEN.md PY-OQ-03): one unit, since a toolchain names one object. A library linked
 * with it also exports the functions of rt/cint_state.h. Both are made once per toolchain file,
 * in a directory of its cache, which a build makes under another name and renames when
 * complete. Returns the new file's path, malloc'd, or NULL with the reason in err. */
static char *state_toolchain(const char *cint, wb_buf *err)
{
    tc_file t;
    memset(&t, 0, sizeof t);
    wb_buf name = {0}, work = {0};
    char *final = NULL, *file = NULL, *obj = NULL;
    int ok = 0, dir = 0;
    if (read_toolchain(cint, &t) != 0) {
        wb_f(err, "cannot read the toolchain file beside %s; give --cint PATH", cint);
    } else {
        wb_f(&name, "wb-rt-%016llx", (unsigned long long)t.key);
        final = name.failed ? NULL : join(t.cache, name.p);
        file = final != NULL ? join(final, "cint.toolchain") : NULL;
        obj = final != NULL ? join(final, t.msvc ? "cint_rt_state.obj" : "cint_rt_state.o") : NULL;
        if (file == NULL || obj == NULL) {
            wb_s(err, "memory ran out");
        } else if (exists(file)) {
            ok = 1;
        } else if (cli_mkdirs(t.cache) != 0) {
            wb_f(err, "cannot create the directory %s", t.cache);
        } else {
            for (unsigned k = 0; dir == 0 && k < 1000u; k++) {
                work.n = 0;
                wb_f(&work, "%s.%u", final, k);
                dir = work.failed ? -1 : cli_mkdir_new(work.p);
            }
            if (dir != 1) {
                wb_f(err, "cannot make a directory for the library runtime under %s", t.cache);
            } else if (make_runtime(&t, work.p, obj, err) == 0) {
                ok = rename(work.p, final) == 0 || exists(file);   /* else another build made it first */
                if (!ok) wb_f(err, "cannot rename %s to %s", work.p, final);
            }
        }
    }
    if (!ok) {
        free(file);
        file = NULL;
    }
    free(t.text);
    free(t.rows);
    free(final);
    free(obj);
    wb_free(&name);
    wb_free(&work);
    return file;
}

void wb_c17_config(const char *cint, wb_buf *out)
{
    tc_file t;
    wb_buf flags = {0};
    memset(&t, 0, sizeof t);
    int ok = cint != NULL && read_toolchain(cint, &t) == 0;
    for (size_t i = 1; ok && i < t.n; i++) wb_f(&flags, "%s%s", i > 1u ? " " : "", t.argv[i]);
    if (ok) wb_f(out, "{\"cc_version\":\"%s\",\"flags\":", t.version != NULL ? t.version : "");
    if (ok) wb_jstr(out, flags.p != NULL ? flags.p : "", flags.n);
    wb_f(out, ok ? ",\"leg\":\"%s\"}" : "null", t.leg != NULL ? t.leg : "");
    free(t.text);
    free(t.rows);
    wb_free(&flags);
}

/* ------------------------------------------------------------------------- */
/* Loading.                                                                   */

char *wb_c17_build(const char *cint, int json, const char *root, const char *rel, const char *out,
                   const char *log, int *status, wb_buf *err)
{
    const char *argv[13];
    int n = 0;
    char *lib = NULL, *toolchain = NULL;
    size_t stem = strlen(rel), slash = stem;
    while (slash > 0 && rel[slash - 1u] != '/') slash--;
    *status = -1;
    if (stem < slash + 4u || strcmp(rel + stem - 3u, ".ci") != 0) {
        wb_f(err, "%s is not a module path", rel);
        return NULL;
    }
    if ((toolchain = state_toolchain(cint, err)) == NULL) return NULL;
    argv[n++] = cint;
    if (json) argv[n++] = "--json";
    argv[n++] = "build";
    argv[n++] = "--lib";
    argv[n++] = "--toolchain";
    argv[n++] = toolchain;
    argv[n++] = "--root";
    argv[n++] = root;
    argv[n++] = "--out";
    argv[n++] = out;
    argv[n++] = rel;
    argv[n] = NULL;
    cli_proc proc = {argv, NULL, log, NULL, NULL, NULL, NULL, 0};
    if (cli_mkdirs(out) != 0 || cli_proc_run(&proc, status) != 0) {
        *status = -1;
        wb_f(err, "cannot run %s; give --cint PATH", cint);
    } else if (*status != 0) {
        wb_f(err, "cint build --lib failed with exit status %d; see %s", *status, log);
    } else {
        wb_buf name = {0};
        wb_f(&name, "%s%.*s%s", CLI_LIB_PREFIX, (int)(stem - slash - 3u), rel + slash, CLI_LIB_SUFFIX);
        lib = name.failed ? NULL : join(out, name.p);
        wb_free(&name);
    }
    free(toolchain);
    return lib;
}

/* The reflection table into lib->exp: NULL, or what is wrong with it. */
static const char *read_table(wb_c17 *lib, const cint_library *desc)
{
    if (desc->abi != CINT_ABI_VERSION || desc->module_count == 0u || desc->module_count > MODULES_MAX ||
        desc->modules == NULL) {
        return "its library descriptor has another ABI version, no modules, or too many";
    }
    uint64_t total = 0;
    for (uint32_t i = 0; i < desc->module_count; i++) {
        const cint_module_table *t = desc->modules[i];
        if (t == NULL || t->module != i || t->info == NULL || t->export_count > EXPORTS_MAX ||
            (t->export_count != 0u && t->exports == NULL)) {
            return "a module table is damaged";
        }
        total += t->export_count;
    }
    /* cintc writes the root module last (SPEC-03 A-19; compiler/OPEN.md CINTC-OQ-39). */
    lib->root = desc->modules[desc->module_count - 1u]->info;
    lib->program = lib->root->program;
    if (lib->root->abi != desc->abi || lib->program == NULL || lib->program->module_count != desc->module_count ||
        lib->program->modules == NULL) {
        return "the root module's descriptor does not match the library descriptor";
    }
    for (uint32_t i = 0; i < desc->module_count; i++) {
        const cint_module *m = lib->program->modules[i];
        if (m == NULL || m->path == NULL || m->path_len == 0u || m->path_len > NAME_LEN_MAX)
            return "a module of the program has no path";
    }
    lib->exp = calloc(total != 0u ? (size_t)total : 1u, sizeof *lib->exp);
    if (lib->exp == NULL) return "memory ran out";
    for (uint32_t k = 0; k < desc->module_count; k++) {
        uint32_t order = k == 0 ? desc->module_count - 1u : k - 1u;   /* the root module's exports first */
        const cint_module_table *t = desc->modules[order];
        for (uint32_t j = 0; j < t->export_count; j++) {
            const cint_export *x = &t->exports[j];
            wb_c17_export *e = &lib->exp[lib->nexp++];
            if (x->name.bytes == NULL || x->name.len == 0u || x->name.len > NAME_LEN_MAX ||
                x->type_signature_len > SIGNATURE_MAX || (x->type_signature_len != 0u && x->type_signature == NULL)) {
                return "an export row is damaged";
            }
            e->name = x->name.bytes;
            e->name_len = x->name.len;
            e->module = order;
            e->kind = x->kind;
            e->effect = x->effect;
            e->call = x->call;
            if (x->kind != CINT_EXPORT_FUNCTION || x->call == NULL)
                (void)snprintf(e->why, sizeof e->why, x->call == NULL ? "it has no cx wrapper" : "it is a kernel");
            else parse_signature(e, x->type_signature, x->type_signature_len, x->param_count);
        }
    }
    return NULL;
}

wb_c17 *wb_c17_open(const char *path, wb_buf *err)
{
    wb_c17 *lib = calloc(1, sizeof *lib);
    const cint_library *desc = NULL;
    const char *why = NULL;
    if (lib == NULL) {
        wb_s(err, "memory ran out");
        return NULL;
    }
    if ((lib->handle = load(path, err)) == NULL) {
        free(lib);
        return NULL;
    }
    desc = address(lib->handle, "cint_library_desc");
    if (desc == NULL) {
        wb_f(err, "%s exports no cint_library_desc, so it has no reflection table (SPEC-03 A-19); build it with "
                  "cint build --lib from cintc, not the seed", path);
        wb_c17_close(lib);
        return NULL;
    }
#define NEED(fp, name) (BIND(fp, lib->handle, name) || (why = name, 0))
    if (!NEED(lib->abi_version, "cint_abi_version") || !NEED(lib->ctx_create, "cint_ctx_create") ||
        !NEED(lib->ctx_destroy, "cint_ctx_destroy") || !NEED(lib->fault_get, "cint_fault_get") ||
        !NEED(lib->fuel_consumed, "cint_fuel_consumed") || !NEED(lib->ctx_refusal, "cint_ctx_refusal") ||
        !NEED(lib->clear_fault, "cint_ctx_clear_fault")) {
        wb_f(err, "%s does not export the runtime's host function %s (SPEC-03 A-3)", path, why);
    }
    if (why == NULL && lib->abi_version() != CINT_ABI_VERSION) {
        uint32_t v = lib->abi_version();
        wb_f(err, "%s has runtime ABI %u.%u and this workbench %u.%u; build it with this tree's cint", path,
             (unsigned)(v >> 16), (unsigned)(v & 0xFFFFu), (unsigned)(CINT_ABI_VERSION >> 16),
             (unsigned)(CINT_ABI_VERSION & 0xFFFFu));
        why = "";
    }
    if (why == NULL && (why = read_table(lib, desc)) != NULL) wb_f(err, "%s cannot be used: %s", path, why);
    if (why != NULL) {
        wb_c17_close(lib);
        return NULL;
    }
    lib->state = address(lib->handle, "cint_observer_state");
    if (!BIND(lib->bind_state, lib->handle, "cint_ctx_bind_state") ||
        !BIND(lib->checkpoint, lib->handle, "cint_ctx_checkpoint") ||
        !BIND(lib->restore, lib->handle, "cint_ctx_restore") ||
        !BIND(lib->state_hash, lib->handle, "cint_ctx_state_hash")) {
        lib->bind_state = NULL;   /* all four, or none */
    }
    return lib;
}

void wb_c17_close(wb_c17 *lib)
{
    if (lib != NULL) {
        if (lib->handle != NULL) unload(lib->handle);
        free(lib->exp);
        free(lib);
    }
}

const cint_program *wb_c17_program(const wb_c17 *lib)
{
    return lib->program;
}

uint32_t wb_c17_count(const wb_c17 *lib)
{
    return lib->nexp;
}

const wb_c17_export *wb_c17_export_at(const wb_c17 *lib, uint32_t i)
{
    return i < lib->nexp ? &lib->exp[i] : NULL;
}

const wb_c17_export *wb_c17_find(const wb_c17 *lib, const char *module, const char *name)
{
    for (uint32_t i = 0; i < lib->nexp; i++) {
        const wb_c17_export *e = &lib->exp[i];
        const cint_module *m = lib->program->modules[e->module];
        if (e->name_len == strlen(name) && memcmp(e->name, name, e->name_len) == 0 &&
            (module == NULL || (m->path_len == strlen(module) && memcmp(m->path, module, m->path_len) == 0))) {
            return e;
        }
    }
    return NULL;
}

int wb_c17_has_state(const wb_c17 *lib)
{
    return lib->bind_state != NULL;
}

/* ------------------------------------------------------------------------- */
/* Contexts and calls.                                                        */

cint_ctx *wb_c17_context(wb_c17 *lib, int64_t depth, cint_output_fn output, void *user, wb_buf *err)
{
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL;
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = lib->program;
    cfg.depth_limit = depth;
    cfg.output = output;
    cfg.output_user = user;
    cfg.module = lib->root;
    cint_status st = lib->ctx_create(&cfg, &ctx);
    if (st != CINT_OK) {
        wb_f(err, "the library refused a context (status %d)", (int)st);
        return NULL;
    }
    if (lib->bind_state != NULL && (st = lib->bind_state(ctx, lib->state)) != CINT_OK) {
        wb_f(err, "the library refused its own state table (status %d)", (int)st);
        lib->ctx_destroy(ctx);
        return NULL;
    }
    return ctx;
}

void wb_c17_destroy(wb_c17 *lib, cint_ctx *ctx)
{
    if (ctx != NULL) lib->ctx_destroy(ctx);
}

static unsigned width_of(uint32_t tag)
{
    return tag == CINT_TAG_BOOL ? 8u : 8u << ((tag & 15u) - 1u);
}

/* The pattern of a value of `tag` in the low bits of v, extended to 64 bits. */
static uint64_t extend(uint64_t v, uint32_t tag)
{
    unsigned w = width_of(tag);
    if (w == 64u) return v;
    v &= (UINT64_C(1) << w) - 1u;
    if (tag >= CINT_TAG_I8 && tag <= CINT_TAG_I64 && (v >> (w - 1u)) != 0u) v |= ~((UINT64_C(1) << w) - 1u);
    return v;
}

typedef uint64_t slot;
#define CX(...) ((cint_status (*)(cint_ctx *, int64_t, __VA_ARGS__))f)
#define CX0 ((cint_status (*)(cint_ctx *, int64_t))f)

/* clang's function check (-fsanitize=function) reports these calls, which pass the cx wrapper's
 * arguments in slots as wb_c17.h describes, as calls through another function type. */
#if defined(__clang__)
__attribute__((no_sanitize("function")))
#endif
cint_status wb_c17_call(wb_c17 *lib, cint_ctx *ctx, const wb_c17_export *e, int64_t fuel, const uint64_t *args,
                        uint64_t *result)
{
    slot a[WB_C17_PARAMS_MAX] = {0};
    union {
        uint8_t u8;
        uint16_t u16;
        uint32_t u32;
        uint64_t u64;
    } r;
    void *rp = e->result != 0u ? (void *)&r : NULL;
    cint_entry_fn f = e->call;
    cint_status st = CINT_REFUSED;
    (void)lib;
    if (e->why[0] != '\0' || ctx == NULL) return CINT_REFUSED;
    memset(&r, 0, sizeof r);
    for (uint32_t i = 0; i < e->nparam; i++) a[i] = extend(args[i], e->param[i]);
    switch (e->nparam * 2u + (rp != NULL)) {
    case 0: st = CX0(ctx, fuel); break;
    case 1: st = CX(void *)(ctx, fuel, rp); break;
    case 2: st = CX(slot)(ctx, fuel, a[0]); break;
    case 3: st = CX(slot, void *)(ctx, fuel, a[0], rp); break;
    case 4: st = CX(slot, slot)(ctx, fuel, a[0], a[1]); break;
    case 5: st = CX(slot, slot, void *)(ctx, fuel, a[0], a[1], rp); break;
    case 6: st = CX(slot, slot, slot)(ctx, fuel, a[0], a[1], a[2]); break;
    case 7: st = CX(slot, slot, slot, void *)(ctx, fuel, a[0], a[1], a[2], rp); break;
    case 8: st = CX(slot, slot, slot, slot)(ctx, fuel, a[0], a[1], a[2], a[3]); break;
    case 9: st = CX(slot, slot, slot, slot, void *)(ctx, fuel, a[0], a[1], a[2], a[3], rp); break;
    case 10: st = CX(slot, slot, slot, slot, slot)(ctx, fuel, a[0], a[1], a[2], a[3], a[4]); break;
    case 11: st = CX(slot, slot, slot, slot, slot, void *)(ctx, fuel, a[0], a[1], a[2], a[3], a[4], rp); break;
    case 12: st = CX(slot, slot, slot, slot, slot, slot)(ctx, fuel, a[0], a[1], a[2], a[3], a[4], a[5]); break;
    case 13:
        st = CX(slot, slot, slot, slot, slot, slot, void *)(ctx, fuel, a[0], a[1], a[2], a[3], a[4], a[5], rp);
        break;
    default: return CINT_REFUSED;
    }
    if (st == CINT_OK && rp != NULL) {
        unsigned w = width_of(e->result);
        *result = extend(w == 8u ? (uint64_t)r.u8 : w == 16u ? (uint64_t)r.u16 : w == 32u ? (uint64_t)r.u32 : r.u64,
                         e->result);
    }
    return st;
}

int64_t wb_c17_fuel(wb_c17 *lib, cint_ctx *ctx)
{
    int64_t n = -1;
    return lib->fuel_consumed(ctx, &n) == CINT_OK ? n : -1;
}

int wb_c17_fault(wb_c17 *lib, cint_ctx *ctx, wb_buf *out)
{
    size_t n = 0;
    if (lib->fault_get(ctx, NULL, 0u, &n) != CINT_OK) return -1;
    if (n == 0u) return 0;
    uint8_t *p = malloc(n);
    size_t got = 0;
    int r = p != NULL && lib->fault_get(ctx, p, n, &got) == CINT_OK && got == n ? 1 : -1;
    if (r == 1) wb_put(out, p, n);
    free(p);
    return r;
}

uint32_t wb_c17_refusal(wb_c17 *lib, cint_ctx *ctx)
{
    uint32_t reason = CINT_REFUSAL_NONE;
    return lib->ctx_refusal(ctx, &reason) == CINT_OK ? reason : CINT_REFUSAL_NONE;
}

cint_status wb_c17_clear_fault(wb_c17 *lib, cint_ctx *ctx)
{
    return lib->clear_fault(ctx);
}

/* ------------------------------------------------------------------------- */
/* Checkpoints, through the library's runtime.                                */

static int no_state(const wb_c17 *lib, wb_buf *err)
{
    if (lib->bind_state == NULL) {
        wb_s(err, "this library's runtime has no checkpoints: it was built without rt/cint_state.c, which "
                  "wb_c17_build links");
        return 1;
    }
    return 0;
}

cint_status wb_c17_checkpoint(wb_c17 *lib, cint_ctx *ctx, wb_buf *out, wb_buf *err)
{
    cint_sink sink = {wb_sink, out};
    return no_state(lib, err) ? CINT_REFUSED : lib->checkpoint(ctx, &sink);
}

cint_status wb_c17_restore(wb_c17 *lib, cint_ctx *ctx, const uint8_t *bytes, size_t n, wb_buf *err)
{
    return no_state(lib, err) ? CINT_REFUSED : lib->restore(ctx, bytes, n);
}

cint_status wb_c17_state_hash(wb_c17 *lib, cint_ctx *ctx, uint8_t out[32], wb_buf *err)
{
    return no_state(lib, err) ? CINT_REFUSED : lib->state_hash(ctx, out);
}
