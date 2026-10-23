/* wb_text.c: text buffers and canonical JSON strings of the workbench (box 14).
 */
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wb.h"

/* A monotonic clock: TIME_UTC is the time of day, which steps back when the system clock is set
 * or a virtual machine resumes, so a difference of two readings can be negative. */
#if defined(_WIN32)
long long wb_now_ns(void)
{
    LARGE_INTEGER c, f;
    if (!QueryPerformanceCounter(&c) || !QueryPerformanceFrequency(&f) || f.QuadPart <= 0) return 0ll;
    return c.QuadPart / f.QuadPart * 1000000000ll + c.QuadPart % f.QuadPart * 1000000000ll / f.QuadPart;
}
#else
long long wb_now_ns(void)
{
    struct timespec t;
    return clock_gettime(CLOCK_MONOTONIC, &t) == 0 ? (long long)t.tv_sec * 1000000000ll + t.tv_nsec : 0ll;
}
#endif

long long wb_elapsed_ns(long long from, long long to)
{
    return to > from ? to - from : 0ll;
}

static int grow(wb_buf *b, size_t n)
{
    if (b->failed) return -1;
    if (n > SIZE_MAX / 4u - b->n - 1u) {
        b->failed = 1;
        return -1;
    }
    if (b->n + n + 1u > b->cap) {
        size_t cap = (b->n + n + 1u) * 2u;
        char *q = realloc(b->p, cap);
        if (q == NULL) {
            b->failed = 1;
            return -1;
        }
        b->p = q;
        b->cap = cap;
    }
    return 0;
}

void wb_put(wb_buf *b, const void *s, size_t n)
{
    if (grow(b, n) != 0) return;
    if (n > 0u) memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = '\0';
}

cint_status wb_sink(void *user, const uint8_t *bytes, size_t len)
{
    wb_buf *b = user;
    wb_put(b, bytes, len);
    return b->failed ? CINT_RESOURCE : CINT_OK;
}

void wb_s(wb_buf *b, const char *s)
{
    wb_put(b, s, strlen(s));
}

void wb_f(wb_buf *b, const char *fmt, ...)
{
    va_list ap, ap2;
    int n;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n > 0 && grow(b, (size_t)n) == 0) {
        (void)vsnprintf(b->p + b->n, (size_t)n + 1u, fmt, ap2);
        b->n += (size_t)n;
    }
    va_end(ap2);
}

void wb_hex(wb_buf *b, const uint8_t *p, size_t n)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        char two[2] = {digits[p[i] >> 4], digits[p[i] & 15u]};
        wb_put(b, two, 2u);
    }
}

void wb_free(wb_buf *b)
{
    free(b->p);
    b->p = NULL;
    b->n = b->cap = 0u;
    b->failed = 0;
}

/* The scalar value of the UTF-8 sequence at s[i], or -1 when it is not one; *k is its length. */
static long scalar_at(const char *s, size_t n, size_t i, size_t *k)
{
    unsigned c = (unsigned char)s[i], u;
    size_t len;
    if (c < 0x80u) {
        *k = 1u;
        return (long)c;
    }
    if (c >= 0xc2u && c <= 0xdfu) {
        len = 2u;
        u = c & 0x1fu;
    } else if (c >= 0xe0u && c <= 0xefu) {
        len = 3u;
        u = c & 0x0fu;
    } else if (c >= 0xf0u && c <= 0xf4u) {
        len = 4u;
        u = c & 7u;
    } else {
        return -1;
    }
    if (n - i < len) return -1;
    for (size_t j = 1u; j < len; j++) {
        unsigned d = (unsigned char)s[i + j];
        if ((d & 0xc0u) != 0x80u) return -1;
        u = (u << 6) | (d & 0x3fu);
    }
    if ((len == 3u && u < 0x800u) || (len == 4u && u < 0x10000u) || (u >= 0xd800u && u <= 0xdfffu) ||
        u > 0x10ffffu) {
        return -1;
    }
    *k = len;
    return (long)u;
}

int wb_utf8(const char *s, size_t n)
{
    size_t i = 0u, k = 0u;
    if (s == NULL && n > 0u) return 0;
    while (i < n) {
        if (scalar_at(s, n, i, &k) < 0) return 0;
        i += k;
    }
    return 1;
}

void wb_jstr(wb_buf *b, const char *s, size_t n)
{
    size_t i = 0u, k = 0u;
    wb_s(b, "\"");
    while (i < n) {
        long u = scalar_at(s, n, i, &k);
        if (u < 0) {
            u = 0xfffd;   /* one byte that is not UTF-8 */
            k = 1u;
        }
        i += k;
        if (u == '"' || u == '\\') {
            wb_f(b, "\\%c", (char)u);
        } else if (u == 8 || u == 9 || u == 10 || u == 12 || u == 13) {
            wb_s(b, u == 8 ? "\\b" : u == 9 ? "\\t" : u == 10 ? "\\n" : u == 12 ? "\\f" : "\\r");
        } else if (u < 0x20 || u >= 0x80) {
            if (u >= 0x10000) {
                unsigned long v = (unsigned long)u - 0x10000ul;
                wb_f(b, "\\u%04lx\\u%04lx", 0xd800ul + (v >> 10), 0xdc00ul + (v & 0x3fful));
            } else {
                wb_f(b, "\\u%04lx", (unsigned long)u);
            }
        } else {
            char c = (char)u;
            wb_put(b, &c, 1u);
        }
    }
    wb_s(b, "\"");
}
