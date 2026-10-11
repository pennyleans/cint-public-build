/* wbt_arrays.c: the driver of workbench/tests/wbt_arrays.py (box 14 unit 4a).
 *
 *   wbt_arrays expect [--json] FILE ROOT   the report of a `.expect` file or a block of
 *                                          cint-harness output, with sources under ROOT
 *   wbt_arrays inspect TYPE EXTENTS LOWER STRIDES ORIGIN VALUES [FORMAT...]
 *                                          `:inspect` of a view over the buffer VALUES; the lists
 *                                          are comma-separated, and `-` is an empty buffer
 *
 * It prints what the library prints and exits 0, or prints the library's reason and exits 1.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cint_proc.h"
#include "wb.h"
#include "wb_array.h"

typedef struct sources {
    const char *root;
    char *path[16], *text[16];
    size_t len[16];
    int n;
} sources;

/* The source of a module path under the root, read once. */
static const uint8_t *source_of(void *user, const char *path, size_t path_len, size_t *len)
{
    sources *s = user;
    char *rel = malloc(path_len + 1u), *full;
    if (rel == NULL) {
        return NULL;
    }
    memcpy(rel, path, path_len);
    rel[path_len] = '\0';
    for (int i = 0; i < s->n; i++) {
        if (strcmp(s->path[i], rel) == 0) {
            free(rel);
            *len = s->len[i];
            return (const uint8_t *)s->text[i];
        }
    }
    full = wb_join(s->root, rel);
    if (s->n == 16 || full == NULL || (s->text[s->n] = wb_read_file(full, &s->len[s->n])) == NULL) {
        free(full);
        free(rel);
        return NULL;
    }
    free(full);
    s->path[s->n] = rel;
    *len = s->len[s->n];
    return (const uint8_t *)s->text[s->n++];
}

static int expect_main(int argc, char **argv, wb_buf *b, wb_buf *err)
{
    int json = argc > 0 && strcmp(argv[0], "--json") == 0;
    size_t n = 0, used = 0;
    char *text = argc == json + 2 ? wb_read_file(argv[json], &n) : NULL;
    sources s = {argv[json + 1], {NULL}, {NULL}, {0}, 0};
    wb_expect *e = malloc(sizeof *e);
    int st = -1;
    if (text == NULL || e == NULL) {
        wb_s(err, "usage: wbt_arrays expect [--json] FILE ROOT");
    } else if ((st = wb_expect_parse(text, n, e, &used, err)) == 0) {
        if (json) {
            wb_fault_json(b, &e->fault, NULL);
            wb_s(b, "\n");
        } else {
            wb_expect_text(b, e, source_of, &s);
        }
        wb_f(b, "-- format %u, %zu of %zu bytes\n", e->format, used, n);
    }
    for (int i = 0; i < s.n; i++) {
        free(s.path[i]);
        free(s.text[i]);
    }
    free(e);
    free(text);
    return st;
}

/* A comma-separated list of integers into v; their number, or -1. */
static int list_of(const char *s, int64_t *v, int cap)
{
    int n = 0;
    char *end;
    if (strcmp(s, "-") == 0) {
        return 0;
    }
    for (;;) {
        if (n == cap) {
            return -1;
        }
        v[n++] = strtoll(s, &end, 10);
        if (end == s || (*end != ',' && *end != '\0')) {
            return -1;
        }
        if (*end == '\0') {
            return n;
        }
        s = end + 1;
    }
}

static int inspect_main(int argc, char **argv, wb_buf *b, wb_buf *err)
{
    static const char *const names[] = {"I8", "I16", "I32", "I64", "U8", "U16", "U32", "U64", "Bool"};
    static int64_t values[4096];
    static uint8_t buffer[4096 * 8];
    int64_t origin[1] = {0};
    unsigned tag = 0, size = 0;
    int nv = 0, ranks[3] = {0, 0, 0};
    wb_view v;
    wb_aformats fmt;
    memset(&v, 0, sizeof v);
    for (unsigned i = 0; argc >= 6 && i < 9u; i++) {
        if (strcmp(argv[0], names[i]) == 0) {
            tag = i == 8u ? CINT_TAG_BOOL : (i < 4u ? CINT_TAG_I8 : CINT_TAG_U8) + i % 4u;
            size = i == 8u ? 1u : 1u << (i % 4u);
        }
    }
    if (tag == 0 || (ranks[0] = list_of(argv[1], v.extent, 4)) < 1 || (ranks[1] = list_of(argv[2], v.lower, 4)) < 1 ||
        (ranks[2] = list_of(argv[3], v.stride, 4)) < 1 || ranks[0] != ranks[1] || ranks[0] != ranks[2] ||
        list_of(argv[4], origin, 1) != 1 || (nv = list_of(argv[5], values, 4096)) < 0) {
        wb_s(err, "usage: wbt_arrays inspect TYPE EXTENTS LOWER STRIDES ORIGIN VALUES [FORMAT...]");
        return -1;
    }
    for (int i = 0; i < nv; i++) {
        for (unsigned k = 0; k < size; k++) {
            buffer[(size_t)i * size + k] = (uint8_t)((uint64_t)values[i] >> (8u * k));
        }
    }
    v.tag = tag;
    v.rank = (uint32_t)ranks[0];
    v.origin = origin[0];
    v.buffer = nv > 0 ? buffer : NULL;
    v.buffer_len = nv;
    v.write = 1;
    if (wb_aformats_parse(argc - 6, (const char *const *)(argv + 6), &fmt, err) != 0) {
        return -1;
    }
    return wb_inspect_array(b, "a", &v, &fmt, err);
}

int main(int argc, char **argv)
{
    wb_buf b = {0}, err = {0};
    int st;
    if (cli_init(&argc, &argv) != 0) {   /* binary stdout, so Windows prints LF as the executables do */
        fputs("cannot read the command line\n", stdout);
        return 1;
    }
    st = argc < 2 ? -1 : strcmp(argv[1], "expect") == 0 ? expect_main(argc - 2, argv + 2, &b, &err)
                       : strcmp(argv[1], "inspect") == 0 ? inspect_main(argc - 2, argv + 2, &b, &err) : -1;
    if (st != 0) {
        printf("%s\n", err.p != NULL ? err.p : "usage: wbt_arrays expect|inspect ...");
    } else if (b.p != NULL) {
        fputs(b.p, stdout);
    }
    wb_free(&b);
    wb_free(&err);
    return st != 0;
}
