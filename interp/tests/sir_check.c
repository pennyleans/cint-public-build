/* sir_check.c: reads a program's SIR text with cint_sir_load and prints its revision
 * identity, or the first broken rule. Test driver of interp/tests/test_interp.py.
 *
 *   sir_check DIR   prints `revision <64 hex>` and `counts <modules> <functions> <nodes>`
 *                   and exits 0, or prints `<file>:<line>:<col>: <text>` and exits 1
 *                   (input), 2 (resource) or 3 (a form not run yet). */
#include <stdio.h>

#include "../cint_interp.h"

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: sir_check DIR\n");
        return 2;
    }
    cint_sir_program *p = NULL;
    cint_sir_error err;
    int st = cint_sir_load(argv[1], &p, &err);
    if (st != CINT_SIR_OK) {
        printf("%s:%u:%u: %s\n", err.path, (unsigned)err.line, (unsigned)err.col, err.text);
        return st;
    }
    uint8_t rev[32];
    uint32_t modules = 0, functions = 0, nodes = 0;
    cint_sir_revision(p, rev);
    cint_sir_counts(p, &modules, &functions, &nodes);
    printf("revision ");
    for (int k = 0; k < 32; k++) {
        printf("%02x", (unsigned)rev[k]);
    }
    printf("\ncounts %u %u %u\n", (unsigned)modules, (unsigned)functions, (unsigned)nodes);
    cint_sir_free(p);
    return 0;
}
