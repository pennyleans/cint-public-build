/* exercises the command's process boundary with deterministic stand-ins. */
#define _CRT_SECURE_NO_WARNINGS 1
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif

static int copy_file(const char *source, const char *destination)
{
    unsigned char bytes[4096];
    size_t count;
    FILE *input = fopen(source, "rb"), *output;
    if (input == NULL) {
        return 90;
    }
    output = fopen(destination, "wb");
    if (output == NULL) {
        fclose(input);
        return 91;
    }
    while ((count = fread(bytes, 1, sizeof bytes, input)) != 0u) {
        if (fwrite(bytes, 1, count, output) != count) {
            fclose(input);
            fclose(output);
            return 92;
        }
    }
    fclose(input);
    if (fclose(output) != 0) {
        return 93;
    }
#if !defined(_WIN32)
    if (chmod(destination, 0700) != 0) {
        return 94;
    }
#endif
    return 0;
}

/* The fuel record of cint_program_run_fuel (rt/cint_rt.h) with fuel consumed 0, to the
 * inherited handle of `token`, the program's last argument. */
static int write_fuel(const char *token)
{
    static const char domain[] = "cint-core-1/fuel-consumed/v1";
    unsigned char bytes[40] = {(unsigned char)(sizeof domain - 1u)};
    memcpy(bytes + 4, domain, sizeof domain - 1u);
    if (token == NULL || strncmp(token, "handle:", 7u) != 0) {
        return 0;
    }
#if defined(_WIN32)
    {
        HANDLE handle = (HANDLE)(uintptr_t)_strtoui64(token + 7, NULL, 10);
        DWORD written;
        return WriteFile(handle, bytes, (DWORD)sizeof bytes, &written, NULL) && written == sizeof bytes ? 0 : 99;
    }
#else
    {
        FILE *output = fdopen(atoi(token + 7), "wb");
        int bad = output == NULL || fwrite(bytes, 1, sizeof bytes, output) != sizeof bytes;
        if (output != NULL && fclose(output) != 0) {
            bad = 1;
        }
        return bad ? 99 : 0;
    }
#endif
}

int main(int argc, char **argv)
{
    const char *mode = getenv("CINT_TEST_MODE"), *destination = NULL;
    /* A program gets `handle:<fault> [handle:<fuel>]`; a test driver gets `<index> handle:<fault> handle:<fuel>`. */
    int test = argc == 4 && strncmp(argv[2], "handle:", 7u) == 0;
    int program = test || ((argc == 2 || argc == 3) && strncmp(argv[1], "handle:", 7u) == 0);
    const char *record = test ? argv[2] : argv[1], *fuel = test ? argv[3] : argc == 3 ? argv[2] : NULL;
    if (mode == NULL) {
        return 95;
    }
    if (program) {
        /* The bytes of CINT_TEST_FAULT_FILE as the fault record (exit 1) or the error record (exit 6). */
        if (strcmp(mode, "fault-file") == 0 || strcmp(mode, "error-file") == 0) {
            const char *path = getenv("CINT_TEST_FAULT_FILE");
            unsigned char bytes[4096];
            size_t count;
            FILE *input = path != NULL ? fopen(path, "rb") : NULL;
            if (input == NULL) {
                return 97;
            }
#if defined(_WIN32)
            HANDLE handle = (HANDLE)(uintptr_t)_strtoui64(record + 7, NULL, 10);
            DWORD written;
            while ((count = fread(bytes, 1, sizeof bytes, input)) != 0u) {
                if (!WriteFile(handle, bytes, (DWORD)count, &written, NULL) || written != count) {
                    fclose(input);
                    return 98;
                }
            }
#else
            FILE *output = fdopen(atoi(record + 7), "wb");
            if (output == NULL) {
                fclose(input);
                return 98;
            }
            while ((count = fread(bytes, 1, sizeof bytes, input)) != 0u) {
                if (fwrite(bytes, 1, count, output) != count) {
                    fclose(input);
                    fclose(output);
                    return 98;
                }
            }
            fclose(output);
#endif
            fclose(input);
            return write_fuel(fuel) != 0 ? 99 : strcmp(mode, "error-file") == 0 ? 6 : 1;
        }
#if defined(_WIN32)
        (void)_setmode(_fileno(stdout), _O_BINARY);
#endif
    }
    if (program) {
        if (strcmp(mode, "stdin") == 0 || strcmp(mode, "env-clean") == 0) {
            fputs(fgetc(stdin) == EOF ? "stdin:null\n" : "stdin:inherited\n", stdout);
            return write_fuel(fuel);
        }
        if (strcmp(mode, "no-fuel") == 0) {
            fputs("no fuel record\n", stdout);
            return 0;
        }
        if (strcmp(mode, "stderr") == 0) {
            fputs("program diagnostic\n", stderr);
            return 0;
        }
        if (strcmp(mode, "fault-empty") == 0) {
            fputs("partial", stdout);
            return 1;
        }
        if (strcmp(mode, "fault-bad") == 0) {
            FILE *output;
#if defined(_WIN32)
            /* uses the portable empty-record case on windows. */
            output = NULL;
#else
            output = fdopen(atoi(record + 7), "wb");
#endif
            if (output != NULL) {
                fputs("bad-record", output);
                fclose(output);
            }
            return 1;
        }
        if (strcmp(mode, "signal") == 0) {
            abort();
        }
        fputs("partial", stdout);
        return 23;
    }
    if (strcmp(mode, "reject") == 0) {
        fputs("compiler stdout marker\n", stdout);
        fputs("compiler stderr marker\n", stderr);
        return 19;
    }
    if (strcmp(mode, "warn") == 0) {
        fputs("warning: compiler fixture\n", stderr);
        return 0;
    }
    if (strcmp(mode, "warn-cmdline") == 0) {
        fputs("cl : Command line warning D9002 : ignoring unknown option '/fixture'\n", stdout);
        return 0;
    }
    if (strcmp(mode, "warn-link") == 0) {
        fputs("LINK : warning LNK4044: unrecognized option '/fixture'; ignored\n", stdout);
        return 0;
    }
    if (strcmp(mode, "env-clean") == 0) {
        static const char *const names[] = {"CL", "_CL_", "LINK", "_LINK_", "CCC_OVERRIDE_OPTIONS"};
        for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
            if (getenv(names[i]) != NULL) {
                fprintf(stderr, "compiler environment holds %s\n", names[i]);
                return 31;
            }
        }
    }
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "/Fe", 3u) == 0) {
            destination = argv[i] + 3;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            destination = argv[i + 1];
        }
    }
    if (destination == NULL) {
        return 96;
    }
    fputs("compiler stdout marker\n", stdout);
    fputs("compiler stderr marker\n", stderr);
    return copy_file(argv[0], destination);
}
