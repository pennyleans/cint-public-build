/* wb_debug_session.c: a debug session on a recording (box 14 unit 3b; a test program).
 *
 * usage: wb-debug-session RECORDING OUT
 *
 * Opens RECORDING read-only as `cint debug RECORDING` does (SPEC-06 10.4), with its directory
 * OUT, reads the session's lines from stdin, and echoes each after its prompt, as a transcript
 * shows it. test_workbench.py runs it on a recording that a session saved, until `cint debug`
 * opens the session itself. Exit status: the session's; 4 for a malformed command line; 5 for
 * an unreadable file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cint_proc.h"
#include "wb.h"

int main(int argc, char **argv)
{
    wb_session_options o;
    size_t n = 0;
    if (cli_init(&argc, &argv) != 0) {   /* binary stdout, so Windows prints LF as the executables do */
        fputs("cannot read the command line\n", stdout);
        return 1;
    }
    char *rec = argc == 3 ? wb_read_file(argv[1], &n) : NULL;
    if (rec == NULL) {
        fprintf(stderr, "wb-debug-session: %s\n", argc == 3 ? "cannot read the recording" : "takes RECORDING OUT");
        return argc == 3 ? CINT_EXIT_ENVIRONMENT : CINT_EXIT_USAGE;
    }
    memset(&o, 0, sizeof o);
    o.out = argv[2];
    o.echo = 1;
    o.debug = (const uint8_t *)rec;
    o.debug_len = n;
    o.debug_path = argv[1];
    int status = wb_session(&o, stdin);
    free(rec);
    return status;
}
