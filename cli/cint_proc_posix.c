/* cint_proc_posix.c: cli/cint_proc.h on POSIX systems (slice 2 task 2.12; decision patch D-16, D-19).
 *
 * Children start through posix_spawn. Every descriptor this process opens is close-on-exec,
 * so a child inherits only its stdin (/dev/null, or this process's stdin for a program granted
 * console.input), stdout, stderr, and the fault record file.
 * `cint <name>` instead replaces this process through execv (cli_handoff).
 */
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700
#endif

#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include "cint_proc.h"

extern char **environ;

int cli_init(int *argc, char ***argv)
{
    (void)argc;
    (void)argv;
    return signal(SIGPIPE, SIG_IGN) == SIG_ERR ? -1 : 0;
}

FILE *cli_fopen(const char *path, const char *mode)
{
    FILE *f = fopen(path, mode);
    if (f != NULL && fcntl(fileno(f), F_SETFD, FD_CLOEXEC) != 0) {
        fclose(f);
        return NULL;
    }
    return f;
}

int cli_mkdir_new(const char *path)
{
    return mkdir(path, 0700) == 0 ? 1 : errno == EEXIST ? 0 : -1;
}

int cli_executable(const char *path)
{
    mode_t mask = umask(0);
    (void)umask(mask);
    return chmod(path, 0777 & ~mask);
}

int cli_mkdirs(const char *path)
{
    char *s = malloc(strlen(path) + 1u);
    struct stat st;
    size_t i, n = strlen(path);
    int ok;
    if (s == NULL) return -1;
    memcpy(s, path, n + 1u);
    for (i = 1; i <= n; i++) {
        if (i == n || s[i] == '/') {
            char c = s[i];
            s[i] = '\0';
            (void)mkdir(s, 0777);
            s[i] = c;
        }
    }
    ok = stat(s, &st) == 0 && S_ISDIR(st.st_mode);
    free(s);
    return ok ? 0 : -1;
}

char *cli_self_path(void)
{
    char *buf = malloc(4096);
#if defined(__APPLE__)
    /* _NSGetExecutablePath gives the launch path, a symlink when cint is linked from ~/.local/bin;
     * cint.toolchain and lib/ sit beside the real file, so resolve it (/proc/self/exe already is). */
    uint32_t size = 4096;
    char *real = buf != NULL && _NSGetExecutablePath(buf, &size) == 0 ? realpath(buf, NULL) : NULL;
    free(buf);
    return real;
#else
    ssize_t n = buf != NULL ? readlink("/proc/self/exe", buf, 4095) : -1;
    if (n <= 0) { free(buf); return NULL; }
    buf[n] = '\0';
    return buf;
#endif
}

char *cli_self_dir(void)
{
    char *buf = cli_self_path();
    char *slash = buf != NULL ? strrchr(buf, '/') : NULL;
    if (slash == NULL) { free(buf); return NULL; }
    slash[slash == buf ? 1 : 0] = '\0';
    return buf;
}

int cli_setenv(const char *name, const char *value)
{
    return setenv(name, value, 1);
}

int cli_unsetenv(const char *name)
{
    return unsetenv(name);
}

int cli_git_worktree(const char *dir, char **gitdir, char **worktree)
{
#if defined(__linux__)
    char *root = realpath(dir, NULL);
    char path[8192], line[4096];
    while (root != NULL) {
        (void)snprintf(path, sizeof path, "%s/.git", root);
        FILE *f = cli_fopen(path, "rb");
        if (f != NULL) {
            char *read = fgets(line, sizeof line, f);
            fclose(f);
            if (read != NULL && strncmp(line, "gitdir: ", 8u) == 0) {
                char *p = line + 8u;
                size_t n = strcspn(p, "\r\n");
                p[n] = '\0';
                unsigned drive = (unsigned char)p[0] | 0x20u;
                if (n >= 3u && drive >= 'a' && drive <= 'z' && p[1] == ':' &&
                    (p[2] == '/' || p[2] == '\\')) {
                    char *mapped = malloc(n + 6u);
                    if (mapped != NULL) {
                        (void)snprintf(mapped, n + 6u, "/mnt/%c/%s", (char)drive, p + 3u);
                        for (char *q = mapped; *q; q++) if (*q == '\\') *q = '/';
                        *gitdir = mapped;
                        *worktree = root;
                        return 1;
                    }
                }
                break;
            }
        }
        char *slash = strrchr(root, '/');
        if (slash == NULL || slash == root) break;
        *slash = '\0';
    }
    free(root);
#else
    (void)dir;
    (void)gitdir;
    (void)worktree;
#endif
    return 0;
}

int cli_proc_run(const cli_proc *p, int *status)
{
    posix_spawn_file_actions_t fa;
    pid_t pid;
    int pipefd[2] = {-1, -1}, fault = -1, fuel = -1, r = -1, w = 0;
    size_t n = 0;
    const char **args;
    char token[32], fuel_token[32];
    while (p->argv[n] != NULL) n++;
    if ((args = calloc(n + 3u, sizeof *args)) == NULL) return -1;
    memcpy((void *)args, (const void *)p->argv, n * sizeof *args);
    if (posix_spawn_file_actions_init(&fa) != 0) { free((void *)args); return -1; }
    if (!p->inherit_stdin && posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0) != 0) goto done;
    if (p->log_path != NULL) {
        if (posix_spawn_file_actions_addopen(&fa, 1, p->log_path, O_WRONLY | O_CREAT | O_APPEND, 0666) != 0 ||
            posix_spawn_file_actions_adddup2(&fa, 1, 2) != 0) {
            goto done;
        }
    } else {
        if (pipe(pipefd) != 0 || fcntl(pipefd[0], F_SETFD, FD_CLOEXEC) != 0 ||
            fcntl(pipefd[1], F_SETFD, FD_CLOEXEC) != 0 || posix_spawn_file_actions_adddup2(&fa, pipefd[1], 1) != 0 ||
            posix_spawn_file_actions_addopen(&fa, 2, p->err_path, O_WRONLY | O_CREAT | O_TRUNC, 0666) != 0) {
            goto done;
        }
        if (p->fault_path != NULL) {
            /* Not close-on-exec: the child writes the record through it (cint_program_run). */
            if ((fault = open(p->fault_path, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0) goto done;
            (void)snprintf(token, sizeof token, "handle:%d", fault);
            args[n++] = token;
            if (p->fuel_path != NULL) {
                if ((fuel = open(p->fuel_path, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0) goto done;
                (void)snprintf(fuel_token, sizeof fuel_token, "handle:%d", fuel);
                args[n++] = fuel_token;
            }
        }
    }
    if (posix_spawnp(&pid, args[0], &fa, NULL, (char *const *)args, environ) != 0) goto done;
    if (pipefd[1] >= 0) {
        uint8_t buf[65536];
        ssize_t got;
        int relay = 1;
        close(pipefd[1]);
        pipefd[1] = -1;
        for (;;) {
            got = read(pipefd[0], buf, sizeof buf);
            if (got < 0 && errno == EINTR) continue;
            if (got <= 0) break;
            if (relay && p->sink != NULL && p->sink(p->user, buf, (size_t)got) != 0) {
                relay = 0;  /* keep draining, so the child never blocks */
            }
        }
    }
    while (waitpid(pid, &w, 0) < 0) {
        if (errno != EINTR) goto done;
    }
    *status = WIFEXITED(w) ? WEXITSTATUS(w) : -1;
    r = 0;
done:
    posix_spawn_file_actions_destroy(&fa);
    int open_fds[4] = {pipefd[0], pipefd[1], fault, fuel};
    for (int i = 0; i < 4; i++) {
        if (open_fds[i] >= 0) close(open_fds[i]);
    }
    free((void *)args);
    return r;
}

int cli_handoff(const char *path, const char *const *argv, int *status)
{
    void (*was)(int) = signal(SIGPIPE, SIG_DFL);   /* cli_init ignored it, and exec keeps an ignored signal */
    (void)status;
    fflush(NULL);
    execv(path, (char *const *)argv);
    int missing = errno == ENOENT || errno == ENOTDIR;
    if (was != SIG_ERR) (void)signal(SIGPIPE, was);
    return missing ? 1 : -1;
}
