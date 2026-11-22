/* khost_rows.c: prints the reflection table of the program it is linked with (cint_library_desc,
 * SPEC-03 A-19 to A-26): per export row its name, kind, effect, size and parameter counts, the
 * size and parameter names, and the type signature bytes. compiler/tests/qbe_kernels.py links it
 * with compiler/tests/qbe/khost.ci once as cpu-c17 writes it and once as cpu-qbe writes it, and
 * the two transcripts must be equal, so a kernel row is compared with cpu-c17's field by field,
 * not only by its signature (SIMD-7; the check of the independent review of PR 884, finding 1). */
#include <inttypes.h>
#include <stdio.h>

#include "cint_rt.h"

extern const cint_library cint_library_desc;

static void name(const cint_name *n)
{
    if (n == NULL) {
        printf(" NULL");
        return;
    }
    printf(" %.*s/%" PRIu32 "/%" PRIu32, (int)n->len, n->bytes, n->len, n->reserved);
}

int main(void)
{
    const cint_library *l = &cint_library_desc;
    uint32_t m, e, i;
    printf("abi %" PRIu32 " modules %" PRIu32 "\n", l->abi, l->module_count);
    for (m = 0; m < l->module_count; m++) {
        const cint_module_table *t = l->modules[m];
        if (t == NULL) {
            printf("module %" PRIu32 " NULL\n", m);
            continue;
        }
        printf("module %" PRIu32 " index %" PRIu32 " exports %" PRIu32 "\n", m, t->module, t->export_count);
        for (e = 0; e < t->export_count; e++) {
            const cint_export *x = &t->exports[e];
            printf("  export");
            name(&x->name);
            printf(" kind %" PRIu32 " effect %" PRIu32 " sizes %" PRIu32 " params %" PRIu32, x->kind, x->effect,
                   x->size_count, x->param_count);
            printf(" size_names");
            if (x->size_names == NULL) printf(" NULL");
            else for (i = 0; i < x->size_count; i++) name(&x->size_names[i]);
            printf(" param_names");
            if (x->param_names == NULL) printf(" NULL");
            else for (i = 0; i < x->param_count; i++) name(&x->param_names[i]);
            printf(" sig");
            for (i = 0; i < x->type_signature_len; i++) printf(" %02x", x->type_signature[i]);
            printf(" call %s\n", x->call != NULL ? "set" : "NULL");
        }
    }
    return 0;
}
