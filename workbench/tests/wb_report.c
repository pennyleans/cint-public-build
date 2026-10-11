/* wb_report.c: renders a fault record as the workbench reports it (box 14 unit 1; a test program).
 *
 * usage: wb-report [--json] [--compile] [--fuel N] [--state TEXT] [--site TEXT]
 *                  [--position TEXT] [--backend TEXT] [--source PATH FILE]... RECORD.hex
 *
 * RECORD.hex holds the record's bytes in lowercase hexadecimal on one line. The report goes to
 * stdout: the human form (wb_fault_text), or under --json the CINT-FAULT-1 line (wb_fault_json).
 * --compile reads a compile-time record (SPEC-01 IM-108 bounds). --source loads FILE as the
 * module PATH for the excerpts, up to eight times. test_workbench.py compares the output with
 * what `cint run` printed for the same record (fixtures/report/). Exit status 0; 1 when the
 * bytes are not a record of that kind; 4 for a malformed command line; 5 for an unreadable file.
 */
#define _CRT_SECURE_NO_WARNINGS 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cint_proc.h"
#include "wb.h"

#define MAX_SOURCES 8

typedef struct loaded {
    const char *path;
    uint8_t *bytes;
    size_t len;
} loaded;

typedef struct sources {
    loaded at[MAX_SOURCES];
    int n;
} sources;

static const uint8_t *find_source(void *user, const char *path, size_t path_len, size_t *len)
{
    const sources *s = user;
    for (int i = 0; i < s->n; i++) {
        if (strlen(s->at[i].path) == path_len && memcmp(s->at[i].path, path, path_len) == 0) {
            *len = s->at[i].len;
            return s->at[i].bytes;
        }
    }
    return NULL;
}

static int read_all(const char *path, uint8_t **out, size_t *len)
{
    FILE *f = cli_fopen(path, "rb");
    uint8_t *p = NULL;
    size_t n = 0u, cap = 0u;
    if (f == NULL) {
        return -1;
    }
    for (;;) {
        size_t got;
        if (cap - n < 4096u) {
            uint8_t *q = realloc(p, cap + 65536u);
            if (q == NULL) {
                free(p);
                fclose(f);
                return -1;
            }
            p = q;
            cap += 65536u;
        }
        got = fread(p + n, 1u, cap - n, f);
        n += got;
        if (got == 0u) {
            break;
        }
    }
    if (ferror(f) || fclose(f) != 0) {
        free(p);
        return -1;
    }
    *out = p;
    *len = n;
    return 0;
}

static int hex_digit(uint8_t c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

/* Hexadecimal text to bytes, in place; a final newline (LF or CR LF) is allowed. */
static int unhex(uint8_t *p, size_t *len)
{
    size_t n = *len;
    if (n > 0u && p[n - 1u] == '\n') {
        n--;
        if (n > 0u && p[n - 1u] == '\r') {
            n--;
        }
    }
    if (n % 2u != 0u) {
        return -1;
    }
    for (size_t i = 0; i < n; i += 2u) {
        int hi = hex_digit(p[i]), lo = hex_digit(p[i + 1u]);
        if (hi < 0 || lo < 0) {
            return -1;
        }
        p[i / 2u] = (uint8_t)(hi * 16 + lo);
    }
    *len = n / 2u;
    return 0;
}

/* A fuel count: decimal digits, no sign, at most 2^63 - 1. */
static int parse_fuel(const char *s, int64_t *out)
{
    int64_t v = 0;
    if (*s == '\0') {
        return -1;
    }
    for (; *s != '\0'; s++) {
        if (*s < '0' || *s > '9' || v > (INT64_MAX - (*s - '0')) / 10) {
            return -1;
        }
        v = v * 10 + (*s - '0');
    }
    *out = v;
    return 0;
}

static int fail(int status, const char *what, const char *detail)
{
    fprintf(stderr, "wb-report: %s%s\n", what, detail);
    return status;
}

int main(int argc, char **argv)
{
    wb_report r = {-1, NULL, NULL, NULL, NULL, 0u, {0, 0, 0, 0}};
    sources src = {0};
    const char *record = NULL;
    int json = 0, kind = WB_RECORD_RUN, status = 0;
    uint8_t *bytes = NULL;
    size_t len = 0u;
    wb_fault *f = NULL;
    wb_buf b = {0};
    if (cli_init(&argc, &argv) != 0) {
        return fail(7, "cannot read the command line", "");
    }
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        int more = i + 1 < argc;
        if (strcmp(a, "--json") == 0) {
            json = 1;
        } else if (strcmp(a, "--compile") == 0) {
            kind = WB_RECORD_COMPILE;
        } else if (strcmp(a, "--fuel") == 0 && more) {
            if (parse_fuel(argv[++i], &r.fuel) != 0) {
                return fail(4, "--fuel takes a count", "");
            }
        } else if (strcmp(a, "--state") == 0 && more) {
            r.state = argv[++i];
        } else if (strcmp(a, "--site") == 0 && more) {
            r.site = argv[++i];
        } else if (strcmp(a, "--position") == 0 && more) {
            r.position = argv[++i];
        } else if (strcmp(a, "--backend") == 0 && more) {
            r.backend = argv[++i];
        } else if (strcmp(a, "--source") == 0 && i + 2 < argc && src.n < MAX_SOURCES) {
            loaded *l = &src.at[src.n++];
            l->path = argv[i + 1];
            if (read_all(argv[i + 2], &l->bytes, &l->len) != 0) {
                return fail(5, "cannot read ", argv[i + 2]);
            }
            i += 2;
        } else if (a[0] != '-' && record == NULL) {
            record = a;
        } else {
            return fail(4, "usage: wb-report [--json] [--compile] [--fuel N] [--state TEXT] [--site TEXT] "
                           "[--position TEXT] [--backend TEXT] [--source PATH FILE]... RECORD.hex", "");
        }
    }
    if (record == NULL) {
        return fail(4, "no record file", "");
    }
    if (read_all(record, &bytes, &len) != 0) {
        return fail(5, "cannot read ", record);
    }
    f = malloc(sizeof *f);
    if (f == NULL) {
        return fail(5, "out of memory", "");
    }
    if (unhex(bytes, &len) != 0 || wb_fault_decode(bytes, len, f, kind) != 0) {
        status = fail(1, "not a fault record: ", record);
    } else {
        if (json) {
            wb_fault_json(&b, f, &r);
            wb_s(&b, "\n");
        } else {
            wb_fault_text(&b, f, &r, find_source, &src);
        }
        if (b.failed || fwrite(b.p, 1u, b.n, stdout) != b.n || fflush(stdout) != 0) {
            status = fail(5, "cannot write the report", "");
        }
    }
    wb_free(&b);
    free(f);
    free(bytes);
    for (int i = 0; i < src.n; i++) {
        free(src.at[i].bytes);
    }
    return status;
}
