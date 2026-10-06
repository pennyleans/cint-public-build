/* Loads PTX modules through rt/cint_cuda.c on the real driver (box 11 unit 3; the device step
 * of compiler/tests/ptx_suite.py, on a machine with an NVIDIA GPU). For each file named on the command line it
 * loads the module (the driver's JIT compiles it, BX11-02) and gets every `.visible .entry`
 * the text declares, then prints `jit <file> <entries>`, or `jit <file> refused <call>
 * <detail>` followed by the JIT log's lines, each prefixed `log `. The first line names the
 * device. Exit status 0 when every module loads, 1 when one does not, 2 when no driver opens. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cint_cuda.h"

static char *read_text(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *data = NULL;
    long size;
    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0) {
        data = malloc((size_t)size + 1);
        if (data != NULL && fread(data, 1, (size_t)size, f) != (size_t)size) {
            free(data);
            data = NULL;
        }
        if (data != NULL) {
            data[size] = '\0';
        }
    }
    fclose(f);
    return data;
}

/* Gets each entry the text declares; returns how many, or -1 at the first the driver lacks. */
static int entries(cint_cuda *cuda, const cint_cuda_module *module, const char *text)
{
    static const char marker[] = ".visible .entry ";
    int count = 0;
    for (const char *p = strstr(text, marker); p != NULL; p = strstr(p, marker)) {
        char name[256];
        size_t n = 0;
        p += sizeof marker - 1;
        while (n + 1 < sizeof name && p[n] != '(' && p[n] != '\0') {
            name[n] = p[n];
            n++;
        }
        name[n] = '\0';
        cint_cuda_kernel kernel;
        if (cint_cuda_kernel_get(cuda, module, name, &kernel) != CINT_OK) {
            return -1;
        }
        count++;
    }
    return count;
}

int main(int argc, char **argv)
{
    cint_cuda *cuda = NULL;
    cint_cuda_error err;
    cint_cuda_info info;
    int failed = 0;
    if (cint_cuda_open(NULL, NULL, &cuda, &err) != CINT_OK) {
        printf("device none: %s %s\n", err.call, err.detail);
        return 2;
    }
    if (cint_cuda_info_get(cuda, &info) == CINT_OK) {
        printf("device %s compute %d.%d driver %d\n", info.name, (int)info.compute_major,
               (int)info.compute_minor, (int)info.driver_version);
    }
    for (int i = 1; i < argc; i++) {
        char *text = read_text(argv[i]);
        cint_cuda_module module;
        int n = -1;
        if (text == NULL) {
            printf("jit %s unreadable\n", argv[i]);
            failed = 1;
            continue;
        }
        if (cint_cuda_module_load(cuda, text, &module) == CINT_OK) {
            n = entries(cuda, &module, text);
            if (n >= 0) {
                printf("jit %s %d\n", argv[i], n);
            }
            if (cint_cuda_module_unload(cuda, &module) != CINT_OK) {
                n = -1;
            }
        }
        if (n < 0) {
            const char *log = cint_cuda_jit_log(cuda);
            (void)cint_cuda_last_error(cuda, &err);
            printf("jit %s refused %s %s\n", argv[i], err.call, err.detail);
            for (const char *p = log; p != NULL && *p != '\0';) {
                const char *end = strchr(p, '\n');
                int len = end != NULL ? (int)(end - p) : (int)strlen(p);
                printf("log %.*s\n", len, p);
                p = end != NULL ? end + 1 : NULL;
            }
            failed = 1;
        }
        free(text);
    }
    cint_cuda_close(cuda);
    return failed;
}
