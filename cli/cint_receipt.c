/* cint_receipt.c: receipt and JSON writing for the `cint` command (decision 26).
 *
 * Canonical JSON strings (SPEC-09 RCPT-02, SPEC-04 LS-289); this tool's own errors as CINT-TOOL-1;
 * the CINT-FAULT-1, CINT-DIAG-1 and CINT-TEST-1 objects (SPEC-06 8.4, 15); the receipt of kind `run`,
 * `build` or `test` (D-16), written only to --receipt PATH; and the retained fault of a faulted
 * compiler (SPEC-09 CINTC-10).
 * cint_main.c reads, builds, and runs; the state both files share is in cint_receipt.h. */
#define _CRT_SECURE_NO_WARNINGS 1
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cint_proc.h"
#include "cint_receipt.h"

#define TOOL_VERSION "0.2.0 (slice 2)"
#define RUNTIME_CONTRACT "cint-rt-3"

/* ------------------------------------------------------------------------- */
/* Byte buffers and canonical JSON (SPEC-09 RCPT-02, SPEC-04 LS-289).        */

static void sb_grow(sbuf *b, size_t n)
{
    if (b->n + n + 1u > b->cap) {
        size_t cap = (b->cap + n + 1u) * 2u;
        char *q = realloc(b->p, cap);
        if (q == NULL) {
            fputs(json_mode ? "{\"category\":\"environment\",\"exit_status\":5,\"message\":\"out of memory\","
                              "\"schema\":\"CINT-TOOL-1\"}\n" : "cint: error: out of memory\n", stderr);
            exit(CINT_EXIT_ENVIRONMENT);
        }
        b->p = q;
        b->cap = cap;
    }
}

void sb_put(sbuf *b, const void *s, size_t n)
{
    sb_grow(b, n);
    if (n > 0u) {
        memcpy(b->p + b->n, s, n);
    }
    b->n += n;
    b->p[b->n] = '\0';
}

void sb_s(sbuf *b, const char *s)
{
    sb_put(b, s, strlen(s));
}

void sb_f(sbuf *b, const char *fmt, ...)
{
    va_list ap, ap2;
    int n;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n > 0) {
        sb_grow(b, (size_t)n);
        (void)vsnprintf(b->p + b->n, (size_t)n + 1u, fmt, ap2);
        b->n += (size_t)n;
    }
    va_end(ap2);
}

/* A JSON string: ASCII only; non-ASCII scalars as \u escapes with lowercase hex
 * (surrogate pairs above U+FFFF), the RFC 8785 short escapes for control characters. */
void sb_jstr(sbuf *b, const char *s, size_t n)
{
    size_t i = 0;
    sb_s(b, "\"");
    while (i < n) {
        unsigned c = (unsigned char)s[i], k = 0, u;
        if (c < 0x80u) {
            i++;
            if (c == '"' || c == '\\') {
                sb_f(b, "\\%c", (char)c);
            } else if (c < 0x20u) {
                const char *esc = c == 8u ? "\\b" : c == 9u ? "\\t" : c == 10u ? "\\n" : c == 12u ? "\\f" : c == 13u ? "\\r" : NULL;
                if (esc != NULL) {
                    sb_s(b, esc);
                } else {
                    sb_f(b, "\\u%04x", c);
                }
            } else {
                sb_put(b, &s[i - 1u], 1u);
            }
            continue;
        }
        k = c >= 0xF8u ? 0u : c >= 0xF0u ? 3u : c >= 0xE0u ? 2u : c >= 0xC2u ? 1u : 0u;
        u = c & (0x3Fu >> k);
        if (i + k >= n) {
            k = 0u;  /* truncated: the lead byte alone is replaced */
        }
        for (unsigned j = 1; j <= k; j++) {
            unsigned d = (unsigned char)s[i + j];
            if ((d & 0xC0u) != 0x80u) {
                k = 0u;
                break;
            }
            u = (u << 6) | (d & 0x3Fu);
        }
        if (k == 0u || u > 0x10FFFFu || (u >= 0xD800u && u <= 0xDFFFu) || (k == 2u && u < 0x800u) ||
            (k == 3u && u < 0x10000u)) {
            u = 0xFFFDu;
            k = 0u;
        }
        i += k + 1u;
        if (u >= 0x10000u) {
            sb_f(b, "\\u%04x\\u%04x", 0xD800u + ((u - 0x10000u) >> 10), 0xDC00u + ((u - 0x10000u) & 0x3FFu));
        } else {
            sb_f(b, "\\u%04x", u);
        }
    }
    sb_s(b, "\"");
}

void sb_jz(sbuf *b, const char *s)
{
    sb_jstr(b, s, strlen(s));
}

/* A key, given as the literal text before the value such as ",\"file\":", then a JSON string. */
static void sb_jkey(sbuf *b, const char *key, const char *s)
{
    sb_s(b, key);
    sb_jz(b, s);
}

void sb_hex(sbuf *b, const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) sb_f(b, "%02x", (unsigned)p[i]);
}

/* ------------------------------------------------------------------------- */
/* Tool errors, faults, and diagnostics (SPEC-06 8.4, 15).                   */

/* One error of this tool (CINT-TOOL-1 under --json). Returns the exit status. */
int tool_error(int status, const char *tool, int tool_status, const char *log, const char *fmt, ...)
{
    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    fflush(stdout);
    if (json_mode) {
        sbuf b = {0};
        sb_jkey(&b, "{\"category\":", status == CINT_EXIT_USAGE         ? "usage"
                                       : status == CINT_EXIT_ENVIRONMENT ? "environment"
                                                                         : "internal");
        sb_f(&b, ",\"exit_status\":%d", status);
        if (log != NULL) {
            sb_jkey(&b, ",\"log_path\":", log);
        }
        sb_jkey(&b, ",\"message\":", msg);
        sb_s(&b, ",\"schema\":\"CINT-TOOL-1\"");
        if (tool != NULL) {
            sb_jkey(&b, ",\"tool\":", tool);
            sb_f(&b, ",\"tool_exit_status\":%d", tool_status);
        }
        sb_s(&b, "}\n");
        fputs(b.p, stderr);
        free(b.p);
    } else {
        fprintf(stderr, "cint: error: %s\n", msg);
    }
    fflush(stderr);
    return status;
}

static void sb_jvalue(sbuf *b, const fvalue *v)
{
    char text[1700];
    size_t n = value_text(v, text, 1);
    char *sp = n > 0u ? strchr(text, ' ') : NULL;
    if (sp == NULL) {
        sb_s(b, "null");
        return;
    }
    *sp = '\0';
    sb_jkey(b, "{\"t\":", text);
    sb_jkey(b, ",\"v\":", sp + 1);
    sb_s(b, "}");
}

static void sb_jpos(sbuf *b, const fpos *p)
{
    sb_f(b, "{\"column\":%u,\"file\":", (unsigned)p->column);
    sb_jstr(b, p->path, p->path_len);
    sb_f(b, ",\"line\":%u}", (unsigned)p->line);
}

/* The CINT-FAULT-1 object of a decoded record (SPEC-06 8.4, SPEC-04 LS-289). With fuel >= 0 it
 * also carries the non-canonical `noncanonical` object (SPEC-06 8.1) with the run's fuel consumed. */
void sb_jfault(sbuf *b, const frec *f, int64_t fuel)
{
    sb_s(b, "{\"address\":");
    if (f->has_addr) {
        sb_f(b, "{\"dispatch\":\"%lld\",\"kernel\":", (long long)f->dispatch);
        sb_jstr(b, f->kernel, f->kernel_len);
        sb_f(b, ",\"phase\":%u,\"step\":", (unsigned)f->phase);
        if (f->has_item) sb_f(b, "\"%lld\"", (long long)f->step);
        else sb_s(b, "null");
        sb_s(b, ",\"work_item\":");
        if (f->has_item) sb_f(b, "\"%lld\"", (long long)f->item);
        else sb_s(b, "null");
        sb_s(b, "}");
    } else {
        sb_s(b, "null");
    }
    sb_jkey(b, ",\"code\":", CODE_NAMES[f->code]);
    sb_f(b, ",\"column\":%u,\"exact\":", (unsigned)f->pos.column);
    if (f->has_exact) {
        sb_jvalue(b, &f->exact);
    } else {
        sb_s(b, "null");
    }
    sb_s(b, ",\"file\":");
    sb_jstr(b, f->pos.path, f->pos.path_len);
    sb_s(b, ",\"limit\":");
    if (f->has_limit) {
        sb_jvalue(b, &f->limit);
    } else {
        sb_s(b, "null");
    }
    sb_f(b, ",\"line\":%u", (unsigned)f->pos.line);
    if (fuel >= 0) {
        sb_f(b, ",\"noncanonical\":{\"fuel_consumed\":\"%lld\"}", (long long)fuel);
    }
    sb_s(b, ",\"operands\":[");
    for (uint32_t i = 0; i < f->nop; i++) {
        sb_s(b, i > 0u ? "," : "");
        sb_jvalue(b, &f->opnd[i]);
    }
    sb_jkey(b, "],\"operation\":", f->op);
    sb_s(b, ",\"revision\":");
    if (f->has_rev) {
        sb_s(b, "\"");
        sb_hex(b, f->rev, 32);
        sb_s(b, "\"");
    } else {
        sb_s(b, "null");
    }
    sb_s(b, ",\"schema\":\"CINT-FAULT-1\",\"severity\":\"fault\",\"source_map\":");
    if (f->has_map) {
        sb_s(b, "\"");
        sb_hex(b, f->map, 32);
        sb_s(b, "\"");
    } else {
        sb_s(b, "null");
    }
    sb_s(b, ",\"stack\":[");
    for (uint32_t i = 0; i < f->nstack; i++) {
        sb_s(b, i > 0u ? "," : "");
        sb_jpos(b, &f->stack[i]);
    }
    sb_s(b, "]}");
}

/* One compile diagnostic as a CINT-DIAG-1 line (SPEC-06 15), or, with message NULL, as a receipt
 * row, which carries no message or note text (SPEC-09 9.2). fault is its compile-time fault or NULL. */
void sb_jdiag(sbuf *o, const cint_compiler_diag *d, const frec *fault, const char *file, const char *message,
              const char *note)
{
    sb_f(o, "{\"code\":\"C%04lld\",\"column\":%lld", (long long)d->code, (long long)d->column);
    if (fault != NULL) {
        sb_s(o, ",\"fault\":");
        sb_jfault(o, fault, -1);
    }
    sb_jkey(o, ",\"file\":", file);
    sb_f(o, ",\"line\":%lld", (long long)d->line);
    if (message == NULL) {
        sb_s(o, "}");
        return;
    }
    sb_jkey(o, ",\"message\":", message);
    if (note != NULL) {
        sb_s(o, ",\"notes\":[{");
        if (d->detail[0] > 0) {
            sb_f(o, "\"column\":%lld,\"file\":", (long long)d->detail[1]);
            sb_jz(o, file);
            sb_f(o, ",\"line\":%lld,", (long long)d->detail[0]);
        }
        sb_jkey(o, "\"text\":", note);
        sb_s(o, "}]");
    }
    sb_s(o, ",\"schema\":\"CINT-DIAG-1\",\"severity\":\"error\"}\n");
}

/* An error result: its SPEC-06 3.4a stderr line, or its CINT-ERROR-1 object; rule 12 names need no escapes. */
void sb_error(sbuf *b, const cli_error *e, int json)
{
    if (json) sb_f(b, "{\"schema\":\"CINT-ERROR-1\",\"set\":\"%.*s\",\"tag\":{\"t\":\"%s\",\"v\":\"%llu\"},\"value\":\"%.*s\"}",
                   e->set_len, e->set, e->type, (unsigned long long)e->tag, e->value_len, e->value);
    else sb_f(b, "error: %.*s.%.*s (tag %llu)", e->set_len, e->set, e->value_len, e->value, (unsigned long long)e->tag);
}

/* One test's result: a CINT-TEST-1 line, or with line 0 a receipt row without the schema key; the
 * key `error`, a CINT-ERROR-1 object, appears only when an error left the test. */
void sb_jtest(sbuf *b, const cli_test *t, int line)
{
    char *name = malloc(t->spelling_len + 1u);
    size_t n = name != NULL ? unescape(t->spelling + 1, t->spelling_len - 2u, name) : 0u;
    sb_s(b, "{");
    if (t->error != NULL) { sb_s(b, "\"error\":"); sb_error(b, t->error, 1); sb_s(b, ","); }
    sb_s(b, "\"expect_fault\":");
    if (t->expect != 0u) sb_f(b, "\"%s\"", CODE_NAMES[t->expect]);
    else sb_s(b, "null");
    if (t->at >= 0) sb_f(b, ",\"expect_line\":%lld,\"fault\":", (long long)t->at);
    else sb_s(b, ",\"expect_line\":null,\"fault\":");
    if (t->fault != NULL) sb_jfault(b, t->fault, -1);
    else sb_s(b, "null");
    sb_jkey(b, ",\"file\":", t->file);
    sb_f(b, ",\"fuel_consumed\":\"%lld\",\"line\":%u,\"name\":", (long long)t->fuel, (unsigned)t->line);
    sb_jstr(b, name != NULL ? name : "", n);
    sb_s(b, t->passed ? ",\"passed\":true" : ",\"passed\":false");
    sb_s(b, line ? ",\"schema\":\"CINT-TEST-1\"}\n" : "}");
    free(name);
}

/* ------------------------------------------------------------------------- */
/* The run receipt (D-16, SPEC-09 RCPT-02) and the retained compiler fault.  */

static void utc(char out[24], int64_t ns)
{
    time_t t = (time_t)(ns / 1000000000);
    struct tm *tm = gmtime(&t);
    if (tm == NULL || strftime(out, 24, "%Y-%m-%dT%H:%M:%SZ", tm) == 0u) {
        (void)snprintf(out, 24, "unknown");
    }
}

static const char *const KINDS[3] = {"run", "build", "test"};

static int write_identity(sbuf *b, const char *source_hex, outset *set)
{
    char hex[65];
    sb_s(b, "{\"backend\":{\"id\":\"cpu-c17\"},\"encodings\":{\"fault\":\"cint-core-1/fault/v2\"},\"generated\":");
    if (set == NULL) {
        sb_s(b, "null");
    } else {
        sb_s(b, "{\"files\":{");
        for (size_t i = 0; i < set->n; i++) {
            char *p = join(set->stage, set->rel[i]);
            if (digest_file(p, hex) != 0) {
                free(p);
                return -1;
            }
            free(p);
            sb_s(b, i > 0u ? "," : "");
            sb_jz(b, set->rel[i]);
            sb_f(b, ":\"%s\"", hex);
        }
        sb_f(b, "},\"manifest\":\"%s\"}", set->manifest_hex);
    }
    sb_f(b, ",\"identities\":{\"build\":null,\"execution\":null,\"meaning\":null,\"revision\":null,\"source_map\":null},"
            "\"kind\":\"%s\"", KINDS[g.mode]);
    if (g.mode != MODE_RUN) {
        /* Every import lookup, present or absent (SPEC-09 RCPT-07); one source root, so one probe each. */
        int n;
        cli_module **v = sorted_modules(1, &n);
        if (v == NULL) return -1;
        sb_s(b, ",\"lookups\":{");
        for (int k = 0, first = 1; k < n; k++) {
            if (v[k] == &g.mods[0]) continue;
            sb_s(b, first ? "" : ",");
            sb_jz(b, v[k]->rel);
            sb_s(b, v[k]->src != NULL ? ":true" : ":false");
            first = 0;
        }
        sb_s(b, "}");
        free(v);
    }
    if (g.mode == MODE_BUILD) {
        sb_s(b, ",\"method\":null");
    } else {
        sb_f(b, ",\"method\":{\"depth_limit\":256,\"frame_arena_bytes\":16777216,\"fuel_budget\":\"%s\","
                "\"fuel_model\":\"fuel-v1\",\"stdin\":\"null\"}", g.mode == MODE_TEST ? "1000000000" : "none");
    }
    sb_s(b, ",\"profile\":\"cint-core-1\",\"runtime\":{\"contract\":\"" RUNTIME_CONTRACT "\",\"files\":{");
    for (size_t i = 0; i < g.tc.nrt; i++) {
        const char *name = basename_utf8(g.tc.rt_sources[i]);
        if (digest_file(g.tc.rt_sources[i], hex) != 0) {
            return -1;
        }
        sb_s(b, i > 0u ? "," : "");
        sb_jz(b, name);
        sb_f(b, ":\"%s\"", hex);
    }
    sb_f(b, "}},\"schema\":\"cint-receipt-1/%s\",\"source\":{\"dirty\":", KINDS[g.mode]);
    sb_s(b, g.source_dirty < 0 ? "null" : g.source_dirty ? "true" : "false");
    sb_s(b, ",\"files\":{");
    {
        int n;
        cli_module **v = sorted_modules(0, &n);
        if (v == NULL) return -1;
        for (int k = 0; k < n; k++) {
            sb_s(b, k > 0 ? "," : "");
            sb_jz(b, v[k]->rel);
            sb_f(b, ":\"%s\"", v[k] == &g.mods[0] ? source_hex : v[k]->hex);
        }
        free(v);
    }
    sb_s(b, "},\"revision\":");
    if (g.source_revision[0] != '\0') sb_jz(b, g.source_revision);
    else sb_s(b, "null");
    sb_jkey(b, ",\"root_module\":", g.rel);
    if (digest_file(g.tc.compiler_sources, hex) != 0) {
        return -1;
    }
    sb_f(b, "},\"tool\":{\"compiler_source_identity\":\"%s\",\"name\":\"cint\",\"version\":\"" TOOL_VERSION "\"}}",
         hex);
    return 0;
}

static int write_receipt(const outcome *oc, const char *source_hex)
{
    sbuf id = {0}, res = {0}, all = {0}, rec = {0};
    char hex[65], when[2][24];
    run_out *o = oc->out;
    if (write_identity(&id, source_hex, oc->set) != 0) {
        free(id.p);
        return tool_error(CINT_EXIT_ENVIRONMENT, NULL, 0, NULL, "cannot read a receipt identity input");
    }
    sb_s(&res, "{\"diagnostics\":");
    if (strcmp(oc->kind, "compile-error") == 0) {
        sb_s(&res, "[");
        report_diags(&res);
        sb_f(&res, "],\"diagnostics_not_retained\":%lld", (long long)g_diag[0].detail[1]);
    } else {
        sb_s(&res, "[],\"diagnostics_not_retained\":0");
    }
    if (oc->error != NULL) { sb_s(&res, ",\"error\":"); sb_error(&res, oc->error, 1); }
    sb_f(&res, ",\"exit_status\":%d", oc->status);
    if (g.mode == MODE_RUN) {
        sb_s(&res, ",\"fault\":");
        if (oc->fault != NULL) {
            sb_jfault(&res, oc->fault, -1);
        } else {
            sb_s(&res, "null");
        }
        if (oc->fuel >= 0) {
            sb_f(&res, ",\"fuel_consumed\":\"%lld\"", (long long)oc->fuel);
        } else {
            sb_s(&res, ",\"fuel_consumed\":null");
        }
    }
    sb_jkey(&res, ",\"kind\":", oc->kind);
    if (g.mode == MODE_RUN) {
        if (digest((const uint8_t *)o->bytes.p, o->bytes.n, hex) != 0) {
            return tool_error(CINT_EXIT_INTERNAL, NULL, 0, NULL, "the digest export of the compiler failed");
        }
        sb_f(&res, ",\"stdout\":{\"bytes\":%llu,\"sha256\":\"%s\"}", (unsigned long long)o->bytes.n, hex);
    }
    if (g.mode == MODE_TEST && oc->status != CINT_EXIT_COMPILE) {
        sb_s(&res, ",\"tests\":[");
        for (int k = 0; k < g.ntests; k++) {
            sb_s(&res, k > 0 ? "," : "");
            sb_jtest(&res, &g.tests[k], 0);
        }
        sb_s(&res, "]");
    }
    sb_s(&res, "}");
    sb_s(&all, "{\"identity\":");
    sb_put(&all, id.p, id.n);
    sb_s(&all, ",\"outcome\":");
    sb_put(&all, res.p, res.n);
    sb_s(&all, "}");
    if (digest((const uint8_t *)all.p, all.n, hex) != 0) {
        return tool_error(CINT_EXIT_INTERNAL, NULL, 0, NULL, "the digest export of the compiler failed");
    }
    utc(when[0], oc->t0);
    utc(when[1], now_ns());
    sb_s(&rec, "{\"identity\":");
    sb_put(&rec, id.p, id.n);
    sb_jkey(&rec, ",\"observations\":{\"cache\":", g.cachedir);
    sb_s(&rec, ",\"cc\":{\"flags\":[");
    for (size_t i = 0; i < g.tc.nflags; i++) {
        sb_s(&rec, i > 0u ? "," : "");
        sb_jz(&rec, g.tc.flags[i]);
    }
    sb_jkey(&rec, "],\"log\":\"cc.log\",\"name\":", g.tc.leg);
    sb_jkey(&rec, ",\"version\":", g.tc.cc_version != NULL ? g.tc.cc_version : "unknown");
    sb_s(&rec, "},\"executables\":{");
    {
        char *cli_path = cli_self_path();
        char executable_hex[65];
        if (cli_path == NULL || digest_file(cli_path, executable_hex) != 0) {
            free(cli_path);
            return tool_error(CINT_EXIT_ENVIRONMENT, NULL, 0, NULL, "cannot read the cint executable for the receipt");
        }
        free(cli_path);
        sb_f(&rec, "\"cint\":\"%s\"", executable_hex);
        if (oc->exe != NULL) {
            if (digest_file(oc->exe, executable_hex) != 0) {
                return tool_error(CINT_EXIT_ENVIRONMENT, NULL, 0, NULL, "cannot read the program executable for the receipt");
            }
            sb_f(&rec, ",\"program\":\"%s\"", executable_hex);
        }
        if (digest_file(g.tc.runtime_object, executable_hex) != 0) {
            return tool_error(CINT_EXIT_ENVIRONMENT, NULL, 0, NULL, "cannot read the runtime object for the receipt");
        }
        sb_f(&rec, ",\"runtime\":\"%s\"", executable_hex);
    }
    sb_f(&rec, "},\"finished_utc\":\"%s\",\"host\":", when[1]);
    sb_jz(&rec, g.tc.host != NULL ? g.tc.host : "unknown");
    {
        sbuf limits = {0};
        sb_s(&limits, "compile runs check.ci; a module outside the checker's subset is refused with C9102 before lowering. "
                      "Meaning, source-map, and execution identities are unavailable from the compiler; the revision identity (SIR-17) "
                      "is in the program and its fault records but not yet in this identity (compiler/OPEN.md CINTC-OQ-40). "
                      "The generated manifest hashes emitted output bytes; the specified build identity encoding is unavailable. "
                      "Git revision and dirty state are sampled for the source project; "
                      "file hashes identify the compiled snapshot.");
        if (g.mode != MODE_BUILD) sb_s(&limits, " Fuel consumed comes from the program's fuel record (compiler/OPEN.md CINTC-OQ-46).");
        if (g.mode == MODE_TEST) sb_s(&limits, " Tests are read from the SIR text of the output set (cli/OPEN.md).");
        if (g.git_limit != NULL) { sb_s(&limits, " "); sb_s(&limits, g.git_limit); }
        sb_jkey(&rec, ",\"limits\":", limits.p);
        free(limits.p);
    }
    sb_f(&rec, ",\"receipt_identity_sha256\":\"%s\",\"started_utc\":\"%s\",\"timings\":{\"build_ns\":\"%lld\","
               "\"cc_ns\":\"%lld\",\"run_ns\":\"%lld\"}},\"outcome\":",
         hex, when[0], (long long)oc->t_build, (long long)oc->t_cc, (long long)oc->t_run);
    sb_put(&rec, res.p, res.n);
    sb_s(&rec, "}\n");
    if (!cint_commit_output(g.receipt, (const uint8_t *)rec.p, rec.n, (size_t)1 << 30)) {
        return tool_error(CINT_EXIT_ENVIRONMENT, NULL, 0, NULL, "cannot write the receipt %s", g.receipt);
    }
    if (!json_mode) {
        fprintf(stderr, "receipt: %s\n", g.receipt);
        fflush(stderr);
    }
    free(id.p);
    free(res.p);
    free(all.p);
    free(rec.p);
    return oc->status;
}

int finish(outcome *oc, const char *source_hex)
{
    fflush(stdout);
    if (g.receipt != NULL && (oc->status == CINT_EXIT_OK || oc->status == CINT_EXIT_FAULT || oc->status == CINT_EXIT_COMPILE ||
                              oc->status == CINT_EXIT_CHECK || oc->status == CINT_EXIT_ERROR_VALUE)) {
        return write_receipt(oc, source_hex);
    }
    return oc->status;
}

/* Retains the compiler's fault without calling its digest export while it is faulted. */
int internal_compiler_fault(const char *source_hex)
{
    cint_fault_record *record = calloc(1, sizeof *record);
    frec *f = calloc(1, sizeof *f);
    uint8_t *bytes = NULL;
    char *binary = join(g.cachedir, "compiler-fault.bin"), *log = join(g.cachedir, "compiler-fault.json");
    size_t n = record != NULL && cint_ctx_fault(g.ctx, record) == CINT_OK ?
               cint_fault_encode(record, NULL, 0u) : 0u;
    sbuf detail = {0};
    int decoded = 0, saved = 0;
    if (n > 0u && (bytes = malloc(n)) != NULL && cint_fault_encode(record, bytes, n) == n) {
        decoded = f != NULL && decode_fault(bytes, n, f, 0) == 0;
        sb_f(&detail, "{\"compiler_source_identity\":\"%s\",\"fault\":", CINT_COMPILER_SOURCE_IDENTITY);
        if (decoded) sb_jfault(&detail, f, -1);
        else sb_s(&detail, "null");
        sb_s(&detail, ",\"fault_record_hex\":\"");
        sb_hex(&detail, bytes, n);
        sb_jkey(&detail, "\",\"inputs\":{", g.rel);
        sb_f(&detail, ":\"%s\"},\"schema\":\"CINT-COMPILER-FAULT-1\"}\n", source_hex);
        saved = cint_commit_output(binary, bytes, n, (size_t)1 << 30) &&
                cint_commit_output(log, (const uint8_t *)detail.p, detail.n, (size_t)1 << 30);
    }
    free(record);
    free(bytes);
    free(binary);
    free(detail.p);
    int status;
    if (saved && decoded) {
        status = tool_error(CINT_EXIT_INTERNAL, NULL, 0, log,
                            "internal compiler error: cintc faulted %s in %s; canonical fault and input identities are in %s (SPEC-09 CINTC-10)",
                            CODE_NAMES[f->code], f->op, log);
    } else {
        status = tool_error(CINT_EXIT_INTERNAL, NULL, 0, saved ? log : NULL,
                            "internal compiler error: cintc faulted; %s (SPEC-09 CINTC-10)",
                            saved ? "the retained fault cannot be decoded" : "cannot retain the canonical fault and input identities");
    }
    free(f);
    free(log);
    return status;
}
