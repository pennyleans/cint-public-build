/* cint_proc_win.c: cli/cint_proc.h on Windows (slice 2 task 2.12; decision patch D-16, D-19).
 *
 * Every path goes through the wide API, so a UTF-8 path with characters outside the ANSI
 * code page works. A child inherits exactly the handles it is given (a handle list on
 * STARTUPINFOEX): its stdin (NUL), stdout and stderr, and the fault record file.
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define _CRT_SECURE_NO_WARNINGS 1

#include <fcntl.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <shellapi.h>

#include "cint_proc.h"

#pragma comment(lib, "shell32.lib")

static wchar_t *widen(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    wchar_t *w = n > 0 ? malloc((size_t)n * sizeof *w) : NULL;
    if (w != NULL && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n) != n) {
        free(w);
        w = NULL;
    }
    return w;
}

static char *narrow(const wchar_t *w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *s = n > 0 ? malloc((size_t)n) : NULL;
    if (s != NULL && WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL) != n) {
        free(s);
        s = NULL;
    }
    return s;
}

int cli_init(int *argc, char ***argv)
{
    int n = 0, i;
    wchar_t **w = CommandLineToArgvW(GetCommandLineW(), &n);
    char **a = w != NULL ? calloc((size_t)n + 1u, sizeof *a) : NULL;
    if (a == NULL) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        if ((a[i] = narrow(w[i])) == NULL) {
            return -1;
        }
    }
    LocalFree(w);
    *argc = n;
    *argv = a;
    if (_setmode(_fileno(stdout), _O_BINARY) == -1 || _setmode(_fileno(stderr), _O_BINARY) == -1) {
        return -1;
    }
    return 0;
}

FILE *cli_fopen(const char *path, const char *mode)
{
    wchar_t *wp = widen(path), *wm = widen(mode);
    FILE *f = wp != NULL && wm != NULL ? _wfopen(wp, wm) : NULL;
    free(wp);
    free(wm);
    return f;
}

int cli_mkdirs(const char *path)
{
    wchar_t *w = widen(path);
    size_t i, n;
    int ok;
    if (w == NULL) {
        return -1;
    }
    n = wcslen(w);
    for (i = 1; i <= n; i++) {
        if (i == n || w[i] == L'\\' || w[i] == L'/') {
            wchar_t c = w[i];
            if (i == 2 && w[1] == L':') {
                continue;  /* the drive */
            }
            w[i] = L'\0';
            (void)CreateDirectoryW(w, NULL);
            w[i] = c;
        }
    }
    {
        DWORD a = GetFileAttributesW(w);
        ok = a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
    free(w);
    return ok ? 0 : -1;
}

int cli_executable(const char *path)
{
    (void)path;
    return 0;
}

int cli_mkdir_new(const char *path)
{
    wchar_t *w = widen(path);
    int r = w != NULL && CreateDirectoryW(w, NULL) ? 1 :
            GetLastError() == ERROR_ALREADY_EXISTS ? 0 : -1;
    free(w);
    return r;
}

char *cli_self_path(void)
{
    wchar_t buf[32768];
    DWORD n = GetModuleFileNameW(NULL, buf, 32768u);
    char *s;
    if (n == 0u || n >= 32768u) {
        return NULL;
    }
    s = narrow(buf);
    return s;
}

char *cli_self_dir(void)
{
    char *buf = cli_self_path();
    char *slash = buf != NULL ? strrchr(buf, '\\') : NULL;
    if (slash == NULL) { free(buf); return NULL; }
    slash[slash == buf + 2 && buf[1] == ':' ? 1 : 0] = '\0';
    return buf;
}

int cli_setenv(const char *name, const char *value)
{
    wchar_t *n = widen(name), *v = widen(value);
    int r = n != NULL && v != NULL && SetEnvironmentVariableW(n, v) ? 0 : -1;
    free(n);
    free(v);
    return r;
}

int cli_unsetenv(const char *name)
{
    wchar_t *n = widen(name);
    int r = n != NULL && SetEnvironmentVariableW(n, NULL) ? 0 : -1;
    free(n);
    return r;
}

int cli_git_worktree(const char *dir, char **gitdir, char **worktree)
{
    (void)dir;
    (void)gitdir;
    (void)worktree;
    return 0;
}

static wchar_t *find_executable(const char *name)
{
    wchar_t *w = widen(name), *path = NULL, *found = NULL;
    if (w == NULL || wcschr(w, L'\\') != NULL || wcschr(w, L'/') != NULL || wcschr(w, L':') != NULL) return w;
    DWORD n = GetEnvironmentVariableW(L"PATH", NULL, 0);
    if (n > 0u && (path = malloc((size_t)n * sizeof *path)) != NULL) {
        DWORD got = GetEnvironmentVariableW(L"PATH", path, n);
        if (got > 0u && got < n) {
            DWORD need = SearchPathW(path, w, L".exe", 0, NULL, NULL);
            if (need > 0u && (found = malloc((size_t)need * sizeof *found)) != NULL &&
                SearchPathW(path, w, L".exe", need, found, NULL) == 0u) {
                free(found);
                found = NULL;
            }
        }
    }
    free(path);
    free(w);
    return found;
}

/* Appends one argument to a command line under the rules the C runtime parses. */
static void quote_arg(wchar_t *out, size_t *at, const wchar_t *a)
{
    size_t k = *at, slashes = 0;
    int plain = a[0] != L'\0' && wcspbrk(a, L" \t\n\v\"") == NULL;
    if (plain) {
        while (*a) {
            out[k++] = *a++;
        }
        *at = k;
        return;
    }
    out[k++] = L'"';
    for (; *a; a++) {
        if (*a == L'\\') {
            slashes++;
            out[k++] = L'\\';
            continue;
        }
        if (*a == L'"') {
            for (size_t s = 0; s < slashes + 1u; s++) {
                out[k++] = L'\\';
            }
        }
        slashes = 0;
        out[k++] = *a;
    }
    for (size_t s = 0; s < slashes; s++) {
        out[k++] = L'\\';
    }
    out[k++] = L'"';
    *at = k;
}

static HANDLE open_file(const char *path, DWORD disposition, BOOL append)
{
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    wchar_t *w = widen(path);
    HANDLE h = INVALID_HANDLE_VALUE;
    if (w != NULL) {
        h = CreateFileW(w, append ? FILE_APPEND_DATA : GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                        disposition, FILE_ATTRIBUTE_NORMAL, NULL);
    }
    free(w);
    return h;
}

int cli_proc_run(const cli_proc *p, int *status)
{
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    STARTUPINFOEXW si;
    PROCESS_INFORMATION pi;
    HANDLE in = INVALID_HANDLE_VALUE, out = INVALID_HANDLE_VALUE, err = INVALID_HANDLE_VALUE;
    HANDLE rd = INVALID_HANDLE_VALUE, fault = INVALID_HANDLE_VALUE, fuel = INVALID_HANDLE_VALUE, list[5];
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = NULL;
    SIZE_T attr_bytes = 0;
    wchar_t *cmd = NULL, *exe = NULL;
    char token[48], fuel_token[48];
    const char **args = NULL;
    size_t len = 64, at = 0, i, n, nh = 0;
    int r = -1;
    DWORD code = 0;
    memset(&si, 0, sizeof si);
    memset(&pi, 0, sizeof pi);
    in = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    if (p->log_path != NULL) {
        out = open_file(p->log_path, OPEN_ALWAYS, TRUE);
        if (out == INVALID_HANDLE_VALUE || !DuplicateHandle(GetCurrentProcess(), out, GetCurrentProcess(), &err, 0,
                                                            TRUE, DUPLICATE_SAME_ACCESS)) {
            goto done;
        }
    } else {
        err = open_file(p->err_path, CREATE_ALWAYS, FALSE);
        if (err == INVALID_HANDLE_VALUE || !CreatePipe(&rd, &out, &sa, 0) ||
            !SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0)) {
            goto done;
        }
        if (p->fault_path != NULL) {
            fault = open_file(p->fault_path, CREATE_ALWAYS, FALSE);
            if (fault == INVALID_HANDLE_VALUE) {
                goto done;
            }
            (void)snprintf(token, sizeof token, "handle:%llu", (unsigned long long)(uintptr_t)fault);
            if (p->fuel_path != NULL) {
                fuel = open_file(p->fuel_path, CREATE_ALWAYS, FALSE);
                if (fuel == INVALID_HANDLE_VALUE) {
                    goto done;
                }
                (void)snprintf(fuel_token, sizeof fuel_token, "handle:%llu", (unsigned long long)(uintptr_t)fuel);
            }
        }
    }
    if (in == INVALID_HANDLE_VALUE) {
        goto done;
    }
    for (n = 0; p->argv[n] != NULL; n++) {
        len += 2u * strlen(p->argv[n]) + 3u;
    }
    if ((args = calloc(n + 3u, sizeof *args)) == NULL) {
        goto done;
    }
    memcpy((void *)args, (const void *)p->argv, n * sizeof *args);
    if (fault != INVALID_HANDLE_VALUE) {
        args[n++] = token;
    }
    if (fuel != INVALID_HANDLE_VALUE) {
        args[n++] = fuel_token;
    }
    if ((cmd = malloc((len + sizeof token + sizeof fuel_token) * 2u * sizeof *cmd)) == NULL || (exe = find_executable(p->argv[0])) == NULL) {
        goto done;
    }
    for (i = 0; i < n; i++) {
        wchar_t *w = widen(args[i]);
        if (w == NULL) {
            goto done;
        }
        if (at > 0) {
            cmd[at++] = L' ';
        }
        quote_arg(cmd, &at, w);
        free(w);
    }
    cmd[at] = L'\0';
    list[nh++] = in;
    list[nh++] = out;
    list[nh++] = err;
    if (fault != INVALID_HANDLE_VALUE) {
        list[nh++] = fault;
    }
    if (fuel != INVALID_HANDLE_VALUE) {
        list[nh++] = fuel;
    }
    (void)InitializeProcThreadAttributeList(NULL, 1, 0, &attr_bytes);
    if ((attrs = malloc(attr_bytes)) == NULL || !InitializeProcThreadAttributeList(attrs, 1, 0, &attr_bytes) ||
        !UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, list, nh * sizeof list[0], NULL,
                                   NULL)) {
        free(attrs);
        attrs = NULL;
        goto done;
    }
    si.StartupInfo.cb = sizeof si;
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = in;
    si.StartupInfo.hStdOutput = out;
    si.StartupInfo.hStdError = err;
    si.lpAttributeList = attrs;
    if (!CreateProcessW(exe, cmd, NULL, NULL, TRUE, EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, NULL, NULL,
                        &si.StartupInfo, &pi)) {
        goto done;
    }
    CloseHandle(out);
    out = INVALID_HANDLE_VALUE;
    if (rd != INVALID_HANDLE_VALUE) {
        uint8_t buf[65536];
        DWORD got = 0;
        int relay = 1;
        while (ReadFile(rd, buf, sizeof buf, &got, NULL) && got > 0u) {
            if (relay && p->sink != NULL && p->sink(p->user, buf, got) != 0) {
                relay = 0;  /* keep draining, so the child never blocks */
            }
        }
    }
    (void)WaitForSingleObject(pi.hProcess, INFINITE);
    if (GetExitCodeProcess(pi.hProcess, &code)) {
        *status = code <= 255u ? (int)code : -1;
        r = 0;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
done:
    if (attrs != NULL) {
        DeleteProcThreadAttributeList(attrs);
        free(attrs);
    }
    HANDLE all[6] = {in, out, err, rd, fault, fuel};
    for (i = 0; i < 6u; i++) {
        if (all[i] != INVALID_HANDLE_VALUE && all[i] != NULL) {
            CloseHandle(all[i]);
        }
    }
    free((void *)args);
    free(cmd);
    free(exe);
    return r;
}
