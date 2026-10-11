/* handoff_tool.c: a stand-in for a tool that `cint <name>` runs (SPEC-06 3.1). It prints each
 * argument on a line, copies stdin to stdout, writes one line on stderr, and exits with the
 * status of an argument `exit=N`, 0 without one; the argument `signal` ends it by SIGTERM. */
#define _CRT_SECURE_NO_WARNINGS 1
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char **argv)
{
    unsigned char bytes[4096];
    size_t count;
    int status = 0;
#if defined(_WIN32)
    if (_setmode(_fileno(stdin), _O_BINARY) == -1 || _setmode(_fileno(stdout), _O_BINARY) == -1 ||
        _setmode(_fileno(stderr), _O_BINARY) == -1) {
        return 90;
    }
#endif
    for (int i = 1; i < argc; i++) {
        printf("arg %s\n", argv[i]);
        if (strncmp(argv[i], "exit=", 5u) == 0) status = atoi(argv[i] + 5);
    }
    while ((count = fread(bytes, 1, sizeof bytes, stdin)) != 0u) {
        if (fwrite(bytes, 1, count, stdout) != count) return 91;
    }
    fputs("handoff tool stderr\n", stderr);
    if (fflush(stdout) != 0) return 92;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "signal") == 0) (void)raise(SIGTERM);
    }
    return status;
}
