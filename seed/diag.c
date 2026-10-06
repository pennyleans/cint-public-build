/* diag.c: diagnostics and bounded allocation for cint-seed. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "tables.h"

seed_state S;

static void put_source_line(const seed_module *m, uint32_t line, uint32_t col)
{
    size_t i = 0, start, end;
    uint32_t l = 1, c;
    char out[512];
    size_t n = 0;
    while (i < m->srclen && l < line) {
        if (m->src[i] == '\n') {
            l++;
        }
        i++;
    }
    start = i;
    end = start;
    while (end < m->srclen && m->src[end] != '\n' && m->src[end] != '\r') {
        end++;
    }
    for (i = start; i < end && n + 2 < sizeof out; i++) {
        uint8_t b = m->src[i];
        out[n++] = (char)((b == '\t' || (b >= 0x20 && b < 0x7f)) ? b : '?');
    }
    out[n] = 0;
    fprintf(stderr, "%s\n", out);
    n = 0;
    for (c = 1; c < col && n + 3 < sizeof out; c++) {
        size_t k = start + c - 1;
        out[n++] = (char)(k < end && m->src[k] == '\t' ? '\t' : ' ');
    }
    out[n++] = '^';
    out[n] = 0;
    fprintf(stderr, "%s\n", out);
}

static void vreport(int32_t module, uint32_t line, uint32_t col, const char *code, const char *fmt, va_list ap)
{
    char text[1024];
    const seed_module *m = &S.mods[module];
    vsnprintf(text, sizeof text, fmt, ap);
    fprintf(stderr, "%s:%u:%u: error %s: %s\n", m->path, line, col, code, text);
    put_source_line(m, line, col);
    fflush(stderr);
}

_Noreturn void diag_at(int32_t module, uint32_t line, uint32_t col, const char *code, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vreport(module, line, col, code, fmt, ap);
    va_end(ap);
    exit(2);
}

_Noreturn void diag_tok(int32_t module, uint32_t tok, const char *code, const char *fmt, ...)
{
    va_list ap;
    const seed_tok *t = &S.mods[module].toks[tok];
    va_start(ap, fmt);
    vreport(module, t->line, t->col, code, fmt, ap);
    va_end(ap);
    exit(2);
}

_Noreturn void diag_fatal(const char *fmt, ...)
{
    va_list ap;
    fputs("cint-seed: error: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(2);
}

const char *fmtbuf(const char *fmt, ...)
{
    static char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    return buf;
}

/* The latest blocks stay reachable from here. A diagnostic ends the process
 * from inside a pass (exit(2)) while that pass's scratch block is live, and a
 * compiler that knows the diagnostic call does not return keeps no pointer to
 * it, so LeakSanitizer would report the block (seen with GCC 13 at -O2). */
static void *recent_blocks[64];
static uint32_t recent_count;

void *seed_alloc(size_t n, const char *what)
{
    void *p = calloc(n > 0u ? n : 1u, 1u);
    if (p == NULL) {
        diag_fatal("%s: out of memory (C9001)", what);
    }
    recent_blocks[recent_count++ % 64u] = p;
    return p;
}

void *seed_grow(void *p, int32_t *cap, int32_t need, size_t elem, const char *what)
{
    int64_t ncap = *cap > 0 ? (int64_t)*cap : 16;
    void *q;
    while (ncap < (int64_t)need) {
        ncap *= 2;
    }
    if (ncap > INT32_MAX / 2 || (uint64_t)ncap > SIZE_MAX / elem) {
        diag_fatal("the %s table is full (C9001)", what);
    }
    q = realloc(p, (size_t)ncap * elem);
    if (q == NULL) {
        diag_fatal("the %s table is full (C9001)", what);
    }
    memset((char *)q + (size_t)*cap * elem, 0, (size_t)(ncap - *cap) * elem);
    *cap = (int32_t)ncap;
    return q;
}
