/* golden_host.c: builds a program with compiler/main.ci through the bridge (slice 2 task 2.11).
 *
 *   golden-host <root> <out> <rel> [<rel> ...]
 *   golden-host --digest <file> [<file> ...]
 *
 * compiler/tests/run.py builds this host with the C that cint-seed emits for
 * compiler/main.ci, rt/cint_build.c, rt/cint_bridge.c and the runtime, so a build goes
 * through the bridge's four calls exactly as `cint run` will drive them (SPEC-09 CINTC-05,
 * CINTC-14; slice 2 decision patch D-13): cint_build reads the modules `rel` under `root`
 * in the order given (dependency order), and commits the output set under `out`
 * (CINTC-06). It writes, ASCII with LF line ends:
 *
 *   build <cint_build result>
 *   diag <code> <module> <line> <column> <detail 0> <detail 1>   for each retained diagnostic
 *   fault <code> <operation> <path>:<line>:<column> <operand 0>  when the compiler faulted
 *
 * With --digest it calls the `digest` export of compiler/main.ci (cx_4_main_6_digest,
 * compiler/sha256.ci) on each file's bytes and writes `digest <64 hex digits>` or
 * `digest error <status>`, for the M1 receipt's digests (plan section 5).
 *
 * Exit status 0 when the build ran (whatever its result), 2 for a usage error, 3 when
 * the context or the root cannot be opened.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cint_bridge.h"

extern const cint_module_info cm_4_main;
cint_status cx_4_main_6_digest(cint_ctx *ctx, int64_t fuel, cint_view p_data, cint_view p_hash, int64_t *result);

static cint_compiler_diag g_diag[CINT_BRIDGE_DIAG_ROWS];
static uint8_t g_faults[CINT_BRIDGE_FAULT_BYTES];

static void print_fault(cint_ctx *ctx)
{
    cint_fault_record *f = malloc(sizeof *f);
    cint_position pos;
    char operand[600];
    if (f == NULL || cint_ctx_fault(ctx, f) != CINT_OK) {
        printf("fault unreadable\n");
        free(f);
        return;
    }
    operand[0] = '\0';
    if (f->operand_count > 0u) {
        (void)cint_tvalue_render(&f->operands[0], operand, sizeof operand);
    }
    if (cint_site_resolve(f->program, f->position, &pos)) {
        printf("fault %u %.*s %.*s:%u:%u %s\n", (unsigned)f->code, (int)f->operation_len, f->operation,
               (int)pos.path_len, pos.path, (unsigned)pos.line, (unsigned)pos.column, operand);
    } else {
        printf("fault %u %.*s ? %s\n", (unsigned)f->code, (int)f->operation_len, f->operation, operand);
    }
    free(f);
}

/* A rank-1 U8 view of n bytes at p, registered with ceiling `perm`. */
static cint_view u8_view(cint_ctx *ctx, void *p, int64_t n, uint8_t perm)
{
    cint_view v;
    memset(&v, 0, sizeof v);
    if (cint_buffer_register_bytes(ctx, n > 0 ? p : NULL, n, perm, &v.buffer) != CINT_OK) {
        v.buffer = 0u;
    }
    v.generation = CINT_BUFFER_GENERATION_FIRST;
    v.type.code = CINT_TAG_U8;
    v.rank = 1u;
    v.perm = perm;
    v.shape[0] = n;
    v.stride[0] = 1;
    return v;
}

static int digest_files(cint_ctx *ctx, int count, char **paths)
{
    for (int k = 0; k < count; k++) {
        FILE *f = fopen(paths[k], "rb");
        uint8_t *data = NULL, hash[32];
        size_t n = 0, cap = 0;
        int64_t result = -1;
        if (f == NULL) {
            printf("digest error unreadable\n");
            continue;
        }
        for (;;) {
            if (n == cap) {
                uint8_t *bigger = realloc(data, cap + 65536u);
                if (bigger == NULL) {
                    break;
                }
                data = bigger;
                cap += 65536u;
            }
            size_t got = fread(data + n, 1, cap - n, f);
            n += got;
            if (got == 0) {
                break;
            }
        }
        fclose(f);
        cint_view vd = u8_view(ctx, data, (int64_t)n, CINT_VIEW_READ);
        cint_view vh = u8_view(ctx, hash, 32, CINT_VIEW_WRITE);
        cint_status st = cx_4_main_6_digest(ctx, CINT_FUEL_UNBOUNDED, vd, vh, &result);
        (void)cint_buffer_release(ctx, vd.buffer);
        (void)cint_buffer_release(ctx, vh.buffer);
        if (st != CINT_OK || result != 0) {
            printf("digest error %d\n", (int)st);
        } else {
            printf("digest ");
            for (int i = 0; i < 32; i++) {
                printf("%02x", (unsigned)hash[i]);
            }
            printf("\n");
        }
        free(data);
    }
    return 0;
}

int main(int argc, char **argv)
{
    cint_ctx_config cfg;
    cint_ctx *ctx = NULL;
    cint_bridge_root *root = NULL;
    int result;
    if (argc >= 2 && strcmp(argv[1], "--digest") == 0) {
        memset(&cfg, 0, sizeof cfg);
        cfg.size = (uint32_t)sizeof cfg;
        cfg.program = cm_4_main.program;
        if (cint_ctx_create(&cfg, &ctx) != CINT_OK) {
            return 3;
        }
        result = digest_files(ctx, argc - 2, argv + 2);
        cint_ctx_destroy(ctx);
        return result;
    }
    if (argc < 4) {
        fprintf(stderr, "usage: golden-host <root> <out> <rel> [<rel> ...]\n");
        return 2;
    }
    memset(&cfg, 0, sizeof cfg);
    cfg.size = (uint32_t)sizeof cfg;
    cfg.program = cm_4_main.program;
    if (cint_ctx_create(&cfg, &ctx) != CINT_OK) {
        fprintf(stderr, "golden-host: no context\n");
        return 3;
    }
    if (!cint_bridge_root_open(argv[1], &root)) {
        fprintf(stderr, "golden-host: cannot open the root %s\n", argv[1]);
        cint_ctx_destroy(ctx);
        return 3;
    }
    result = cint_build(ctx, root, (const char *const *)(argv + 3), (int64_t)(argc - 3), argv[2], g_diag, g_faults);
    printf("build %d\n", result);
    if (result == CINT_BUILD_DIAG) {
        int64_t k;
        for (k = 1; k <= g_diag[0].detail[0] && k < CINT_BRIDGE_DIAG_ROWS; k++) {
            const cint_compiler_diag *d = &g_diag[k];
            printf("diag %lld %lld %lld %lld %lld %lld\n", (long long)d->code, (long long)d->module,
                   (long long)d->line, (long long)d->column, (long long)d->detail[0], (long long)d->detail[1]);
        }
    } else if (result == CINT_BUILD_FAULT) {
        print_fault(ctx);
    }
    cint_bridge_root_close(root);
    cint_ctx_destroy(ctx);
    return 0;
}
