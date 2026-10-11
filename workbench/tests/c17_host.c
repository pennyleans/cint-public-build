/* c17_host.c: drives workbench/wb_c17.c for workbench/tests/wbt_c17.py (box 14 unit 5).
 *
 * Reads one command per line from standard input and writes each, after "> ", with what it did:
 *
 *   build CINT ROOT REL OUT   wb_c17_build; the library it writes is the one `open lib` opens
 *   open PATH|lib             wb_c17_open: the export count and whether it has checkpoints
 *   exports                   each export with its signature and effect, or why it is not callable
 *   context                   a new context of the open library, which replaces the current one
 *   fuel N|none               the allowance of later calls (none at first)
 *   call NAME [ARG...]        one entry: its value, done, its fault, or its refusal, with its fuel;
 *                             an ARG is an integer, true, or false
 *   checkpoint                keeps the current context's checkpoint, with its state hash
 *   restore                   restores the kept checkpoint into a new context, now the current one
 *
 * It links no runtime of its own, so every context goes through the library's.
 */
#define _CRT_SECURE_NO_WARNINGS 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wb.h"
#include "wb_c17.h"

static cint_status relay(void *user, const uint8_t *bytes, size_t len)
{
    (void)user;
    printf("out: %.*s", (int)len, (const char *)bytes);
    return CINT_OK;
}

static int argument(const char *t, uint64_t *bits)
{
    char *end = NULL;
    if (strcmp(t, "true") == 0 || strcmp(t, "false") == 0) {
        *bits = t[0] == 't' ? 1u : 0u;
        return 0;
    }
    if (t[0] == '-') {
        *bits = (uint64_t)strtoll(t, &end, 10);
    } else {
        *bits = strtoull(t, &end, 10);
    }
    return end != t && *end == '\0' ? 0 : -1;
}

static void hash_line(wb_c17 *lib, cint_ctx *ctx, const char *head)
{
    uint8_t h[32];
    wb_buf err = {0};
    if (wb_c17_state_hash(lib, ctx, h, &err) == CINT_OK) {
        printf("%s   state %02x%02x%02x%02x\n", head, h[0], h[1], h[2], h[3]);
    } else {
        printf("%s   no state hash: %s\n", head, err.n != 0 ? err.p : "refused");
    }
    wb_free(&err);
}

static void call(wb_c17 *lib, cint_ctx *ctx, int64_t fuel, char **w, int n)
{
    const wb_c17_export *e = wb_c17_find(lib, NULL, w[1]);
    uint64_t args[WB_C17_PARAMS_MAX] = {0}, result = 0;
    if (e == NULL || (int)e->nparam != n - 2) {
        printf("no export %s with %d arguments\n", w[1], n - 2);
        return;
    }
    if (e->why[0] != '\0') {
        printf("not callable: %s\n", e->why);
        return;
    }
    for (int i = 2; i < n; i++) {
        if (argument(w[i], &args[i - 2]) != 0) {
            printf("not an argument: %s\n", w[i]);
            return;
        }
    }
    cint_status st = wb_c17_call(lib, ctx, e, fuel, args, &result);
    int64_t used = wb_c17_fuel(lib, ctx);
    if (st == CINT_OK && e->result == 0u) {
        printf("done   fuel %lld\n", (long long)used);
    } else if (st == CINT_OK) {
        wb_buf v = {0}, sig = {0};
        wb_c17_signature(&sig, e);
        if (e->result == CINT_TAG_BOOL) {
            wb_s(&v, result != 0u ? "true" : "false");
        } else if (e->result >= CINT_TAG_U8) {
            wb_f(&v, "%llu", (unsigned long long)result);
        } else {
            wb_f(&v, "%lld", (long long)result);
        }
        printf("= %s : %s   fuel %lld\n", v.p, strstr(sig.p, "-> ") + 3, (long long)used);
        wb_free(&v);
        wb_free(&sig);
    } else if (st == CINT_FAULT) {
        wb_buf rec = {0};
        static wb_fault f;
        if (wb_c17_fault(lib, ctx, &rec) == 1 &&
            wb_fault_decode((const uint8_t *)rec.p, rec.n, &f, WB_RECORD_RUN) == 0) {
            printf("fault[%s] %s at %.*s:%u:%u   fuel %lld\n", WB_CODE_NAMES[f.code], f.op, (int)f.pos.path_len,
                   f.pos.path, (unsigned)f.pos.line, (unsigned)f.pos.column, (long long)used);
        } else {
            printf("fault, and its record cannot be read\n");
        }
        wb_free(&rec);
        (void)wb_c17_clear_fault(lib, ctx);
    } else if (st == CINT_REFUSED) {
        printf("refused   reason %u\n", (unsigned)wb_c17_refusal(lib, ctx));
    } else {
        printf("status %d\n", (int)st);
    }
}

int main(void)
{
    static char line[4096];
    wb_c17 *lib = NULL;
    cint_ctx *ctx = NULL;
    wb_buf kept = {0};
    char *built = NULL;
    int64_t fuel = CINT_FUEL_UNBOUNDED;
    setvbuf(stdout, NULL, _IONBF, 0);
    while (fgets(line, sizeof line, stdin) != NULL) {
        char *w[16];
        int n = 0;
        line[strcspn(line, "\r\n")] = '\0';
        printf("> %s\n", line);
        for (char *t = strtok(line, " "); t != NULL && n < 16; t = strtok(NULL, " ")) {
            w[n++] = t;
        }
        wb_buf err = {0};
        if (n == 0) {
            continue;
        } else if (strcmp(w[0], "build") == 0 && n == 5) {
            int status = 0;
            free(built);
            built = wb_c17_build(w[1], 0, w[2], w[3], w[4], "build.log", &status, &err);
            if (built != NULL) {
                printf("built\n");
            } else {
                printf("error: %s\n", err.p);
            }
        } else if (strcmp(w[0], "open") == 0 && n == 2) {
            wb_c17_destroy(lib, ctx);
            wb_c17_close(lib);
            ctx = NULL;
            lib = wb_c17_open(strcmp(w[1], "lib") == 0 && built != NULL ? built : w[1], &err);
            if (lib != NULL) {
                printf("open: %u exports, checkpoints %s\n", (unsigned)wb_c17_count(lib),
                       wb_c17_has_state(lib) ? "yes" : "no");
            } else {
                printf("error: %s\n", err.p);
            }
        } else if (lib == NULL) {
            printf("no library is open\n");
        } else if (strcmp(w[0], "exports") == 0) {
            static const char *const effects[4] = {"", "pure", "observe", "unrestricted"};
            for (uint32_t i = 0; i < wb_c17_count(lib); i++) {
                const wb_c17_export *e = wb_c17_export_at(lib, i);
                wb_buf sig = {0};
                wb_c17_signature(&sig, e);
                printf("  %.*s %s   %s%s\n", (int)e->name_len, e->name, sig.p, e->why[0] ? "not callable: " : "",
                       e->why[0] ? e->why : effects[e->effect <= 3u ? e->effect : 0u]);
                wb_free(&sig);
            }
        } else if (strcmp(w[0], "context") == 0) {
            wb_c17_destroy(lib, ctx);
            ctx = wb_c17_context(lib, 0, relay, NULL, &err);
            if (ctx != NULL) {
                printf("context\n");
            } else {
                printf("error: %s\n", err.p);
            }
        } else if (strcmp(w[0], "fuel") == 0 && n == 2) {
            fuel = strcmp(w[1], "none") == 0 ? CINT_FUEL_UNBOUNDED : strtoll(w[1], NULL, 10);
        } else if (ctx == NULL) {
            printf("no context\n");
        } else if (strcmp(w[0], "call") == 0 && n >= 2) {
            call(lib, ctx, fuel, w, n);
        } else if (strcmp(w[0], "checkpoint") == 0) {
            kept.n = 0;
            cint_status st = wb_c17_checkpoint(lib, ctx, &kept, &err);
            if (st == CINT_OK) {
                hash_line(lib, ctx, "checkpoint");
            } else {
                printf("refused: %s\n", err.n != 0 ? err.p : "by the runtime");
            }
        } else if (strcmp(w[0], "restore") == 0) {
            cint_ctx *x = wb_c17_context(lib, 0, relay, NULL, &err);
            cint_status st = x != NULL ? wb_c17_restore(lib, x, (const uint8_t *)kept.p, kept.n, &err) : CINT_REFUSED;
            if (st == CINT_OK) {
                wb_c17_destroy(lib, ctx);
                ctx = x;
                hash_line(lib, ctx, "restored");
            } else {
                wb_c17_destroy(lib, x);
                printf("refused: %s\n", err.n != 0 ? err.p : "by the runtime");
            }
        } else {
            printf("unknown command\n");
        }
        wb_free(&err);
    }
    if (lib != NULL) {
        wb_c17_destroy(lib, ctx);
    }
    wb_c17_close(lib);
    wb_free(&kept);
    free(built);
    return 0;
}
