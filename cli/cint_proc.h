/* cint_proc.h: what the `cint` command needs from the host system (slice 2 task 2.12).
 *
 * One platform file implements it: cli/cint_proc_win.c on Windows, cli/cint_proc_posix.c
 * elsewhere (slice 2 decision patch D-16: process spawning lives in one platform file).
 * Paths and arguments are UTF-8 on every platform.
 */
#ifndef CINT_PROC_H
#define CINT_PROC_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Makes argv UTF-8 (Windows reads the wide command line) and puts stdout and stderr in
 * binary mode, so LF is never written as CR LF (SPEC-04 LS-194). Zero on success. */
int cli_init(int *argc, char ***argv);

FILE *cli_fopen(const char *path, const char *mode);
/* Creates the directory and its missing parents. Zero when it exists afterwards. */
int cli_mkdirs(const char *path);
/* Creates one private directory exclusively. 1: created; 0: exists; -1: error. */
int cli_mkdir_new(const char *path);
/* Makes a written program executable (POSIX: 0777 less the umask; Windows: nothing). Zero on success. */
int cli_executable(const char *path);
/* The directory of the running executable, malloc'd, or NULL. */
char *cli_self_dir(void);
/* The running executable's path, malloc'd, or NULL. */
char *cli_self_path(void);
/* Sets an environment variable of this process, so every child inherits it. */
int cli_setenv(const char *name, const char *value);
int cli_unsetenv(const char *name);
/* Maps a drive-absolute gitdir in a Windows worktree file under WSL. Returns 1
 * with malloc'd paths, or 0 when this source directory needs no such mapping. */
int cli_git_worktree(const char *dir, char **gitdir, char **worktree);

/* The name a shared library takes on this host (`cint build --lib`, SPEC-06 3.1). */
#if defined(_WIN32)
#define CLI_LIB_PREFIX ""
#define CLI_LIB_SUFFIX ".dll"
#elif defined(__APPLE__)
#define CLI_LIB_PREFIX "lib"
#define CLI_LIB_SUFFIX ".dylib"
#else
#define CLI_LIB_PREFIX "lib"
#define CLI_LIB_SUFFIX ".so"
#endif

/* Receives the bytes the child writes to its stdout. Nonzero stops relaying. */
typedef int (*cli_sink)(void *user, const uint8_t *bytes, size_t len);

typedef struct cli_proc {
    const char *const *argv;  /* argv[0] is the executable's path; NULL-terminated */
    const char *log_path;     /* stdout and stderr both appended to this file, or NULL */
    const char *err_path;     /* without log_path: stderr to this file (truncated) */
    const char *fault_path;   /* without log_path: created and passed as an argument
                                 "handle:<N>", an inherited handle (cint_program_run) */
    const char *fuel_path;    /* with fault_path: created and passed after it the same way,
                                 the fuel record's destination (cint_program_run_fuel) */
    cli_sink sink;            /* without log_path: the child's stdout */
    void *user;
} cli_proc;

/* Runs the child with the null device as stdin and waits for it. Returns 0 and sets
 * *status to its exit status, or to -1 when it ended without one (a signal or a crash on
 * POSIX); returns -1 when it could not be started (the executable is missing). */
int cli_proc_run(const cli_proc *p, int *status);

#endif /* CINT_PROC_H */
