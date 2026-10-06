/* cint_bridge.c: the host file layer of the compiler's bridge (SPEC-09 CINTC-06, CINTC-15;
 * slice 2 decision patch D-3).
 *
 * The input reader, cint_commit_output, and the primitives that rt/cint_build.c
 * (the four calls, the output set and the build driver) uses are the only code
 * that calls the host operating system's file API (SPEC-09 SEED-03, CINTC-06).
 * Nothing here refers to the compiler, so the seed links this file alone.
 */
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE 1 /* Apple <sys/fcntl.h> hides O_NOFOLLOW under _POSIX_C_SOURCE */
#endif
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L /* openat, fsync, O_NOFOLLOW, O_CLOEXEC, O_DIRECTORY */
#endif

#include "cint_bridge_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

/* ------------------------------------------------------------------------- */
/* Test hooks, file identities, paths, SHA-256.                               */

#ifdef CINT_BRIDGE_TEST_HOOKS
int cint_bridge_test_fail_step = 0, cint_bridge_test_fail_write = 0, cint_bridge_test_kill_after = 0;
unsigned cint_bridge_test_stage_tries = 0;
unsigned cint_bridge_stage_tries(void)
{
    return cint_bridge_test_stage_tries != 0 ? cint_bridge_test_stage_tries : CINT_BRIDGE_STAGE_TRIES;
}
void (*cint_bridge_test_after_read)(void) = NULL;
static bool fail_at(int step) { return cint_bridge_test_fail_step == step; }
#else
static bool fail_at(int step) { (void)step; return false; }
unsigned cint_bridge_stage_tries(void) { return CINT_BRIDGE_STAGE_TRIES; }
#endif

enum { STEP_CREATE = 1, STEP_WRITE = 2, STEP_FLUSH = 3, STEP_CLOSE = 4, STEP_REPLACE = 5 };

/* POSIX: (st_dev, 0, st_ino). Windows: (volume serial, FILE_ID_128 bytes 0-7, 8-15). */
typedef struct file_id { uint64_t volume, high, low; } file_id;

static file_id g_inputs[CINT_BRIDGE_MAX_INPUTS];
static size_t g_input_count;

static bool is_input(file_id id)
{
    for (size_t i = 0; i < g_input_count; i++) {
        if (g_inputs[i].volume == id.volume && g_inputs[i].high == id.high && g_inputs[i].low == id.low) return true;
    }
    return false;
}

static bool remember_input(file_id id)
{
    if (is_input(id)) return true;
    if (g_input_count >= CINT_BRIDGE_MAX_INPUTS) return false;
    g_inputs[g_input_count++] = id;
    return true;
}

void cint_bridge_clear_inputs(void) { g_input_count = 0; }
void cint_bridge_free_input(uint8_t *bytes) { free(bytes); }

/* Strict UTF-8 (RFC 3629): no overlong forms, no surrogates, at most U+10FFFF. */
static bool utf8_valid(const uint8_t *s, size_t n)
{
    size_t i = 0;
    while (i < n) {
        uint32_t c = s[i], min;
        size_t extra;
        if (c < 0x80u) { i++; continue; }
        if ((c & 0xE0u) == 0xC0u) { extra = 1; min = 0x80u; c &= 0x1Fu; }
        else if ((c & 0xF0u) == 0xE0u) { extra = 2; min = 0x800u; c &= 0x0Fu; }
        else if ((c & 0xF8u) == 0xF0u) { extra = 3; min = 0x10000u; c &= 0x07u; }
        else return false;
        if (n - i <= extra) return false;
        for (size_t j = 1; j <= extra; j++) {
            uint32_t b = s[i + j];
            if ((b & 0xC0u) != 0x80u) return false;
            c = (c << 6) | (b & 0x3Fu);
        }
        if (c < min || c > 0x10FFFFu || (c >= 0xD800u && c <= 0xDFFFu)) return false;
        i += extra + 1;
    }
    return true;
}

/* A path the commit accepts: non-empty UTF-8 that does not end in a separator. */
bool cint_bridge_path_ok(const char *path)
{
    if (path == NULL || path[0] == '\0') return false;
    size_t n = strlen(path);
    return !cint_bridge_is_sep(path[n - 1]) && utf8_valid((const uint8_t *)path, n);
}

/* '/', and on Windows also '\\'; on POSIX a backslash is a name character. */
bool cint_bridge_is_sep(char c)
{
#ifdef _WIN32
    if (c == '\\') return true;
#endif
    return c == '/';
}

/* CINTC-12: `/`-separated segments of [A-Za-z0-9_.-], none empty, ending in `.`
 * (so none is `.` or `..`), or a device name before its first `.`. */
bool cint_bridge_rel_ok(const char *rel)
{
    static const char dev[6][4] = {"CON", "PRN", "AUX", "NUL", "COM", "LPT"};
    size_t n = rel != NULL ? strlen(rel) : 0, seg = 0;
    for (size_t i = 0; i <= n && n > 0; i++) {
        char c = rel[i];
        if (i < n && c != '/') {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'
                  || c == '-' || c == '.')) return false;
            continue;
        }
        const char *g = rel + seg;
        size_t len = i - seg, stem = 0;
        while (stem < len && g[stem] != '.') stem++;
        if (len == 0 || g[len - 1] == '.') return false;
        for (int k = 0; k < 6 && (stem == 3 || stem == 4); k++) {
            if ((g[0] & ~0x20) == dev[k][0] && (g[1] & ~0x20) == dev[k][1] && (g[2] & ~0x20) == dev[k][2]
                && (stem == 3 ? k < 4 : k >= 4 && g[3] >= '1' && g[3] <= '9')) return false;
        }
        seg = i + 1;
    }
    return n > 0;
}

void cint_bridge_sha256_hex(const uint8_t *p, size_t n, char hex[65])
{
    uint8_t d[32];
    cint_sha256(p, n, d);
    for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", (unsigned)d[i]);
}

char *cint_bridge_join(const char *a, const char *sep, const char *b)
{
    size_t n = strlen(a) + strlen(sep) + strlen(b) + 1;
    char *s = malloc(n);
    if (s != NULL) snprintf(s, n, "%s%s%s", a, sep, b);
    return s;
}

/* "<path>.stage-<k>" in a new buffer, or NULL. */
char *cint_bridge_stage_name(const char *path, unsigned k)
{
    char tail[24];
    snprintf(tail, sizeof tail, ".stage-%u", k);
    return cint_bridge_join(path, "", tail);
}

/* ------------------------------------------------------------------------- */
/* Platform layer. write_new: 1 written and flushed, 0 the name exists, -1 a  */
/* failure (the file is then removed). make_dir: 1 made, 0 exists, -1 error.  */

#ifdef _WIN32
struct cint_bridge_root { HANDLE h; wchar_t *path; };

static wchar_t *widen(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    wchar_t *w = n > 0 ? malloc((size_t)n * sizeof *w) : NULL;
    if (w != NULL && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n) != n) { free(w); w = NULL; }
    for (int i = 4; w != NULL && wcsncmp(w, L"\\\\?\\", 4) == 0 && i < n; i++) {
        if (w[i] == L'/') w[i] = L'\\';   /* Windows keeps '/' literal after a \\?\ prefix */
    }
    return w;
}

static bool handle_id(HANDLE h, file_id *id, DWORD *attributes)
{
    FILE_ID_INFO fi;
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandleEx(h, FileIdInfo, &fi, sizeof fi) || !GetFileInformationByHandle(h, &info)) {
        return false;
    }
    id->volume = fi.VolumeSerialNumber;
    memcpy(&id->high, fi.FileId.Identifier, 8);
    memcpy(&id->low, fi.FileId.Identifier + 8, 8);
    *attributes = info.dwFileAttributes;
    return true;
}

static wchar_t *final_path(HANDLE h)
{
    DWORD n = GetFinalPathNameByHandleW(h, NULL, 0, FILE_NAME_NORMALIZED);
    wchar_t *w = n > 0 ? malloc((size_t)n * sizeof *w) : NULL;
    if (w != NULL && GetFinalPathNameByHandleW(h, w, n, FILE_NAME_NORMALIZED) + 1 != n) { free(w); w = NULL; }
    return w;
}

static const DWORD NOT_FILE = FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE;

/* True when `path` may be replaced: absent, or a regular file that is no input. */
static bool target_ok(const char *path)
{
    wchar_t *w = widen(path);
    HANDLE h = w == NULL ? INVALID_HANDLE_VALUE
                         : CreateFileW(w, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                       NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    file_id id;
    DWORD attributes = 0;
    bool ok = w != NULL && h == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND;
    if (h != INVALID_HANDLE_VALUE) {
        ok = handle_id(h, &id, &attributes) && (attributes & NOT_FILE) == 0 && !is_input(id);
        CloseHandle(h);
    }
    free(w);
    return ok;
}

int cint_bridge_write_new(const char *path, const uint8_t *p, size_t len)
{
    wchar_t *w = fail_at(STEP_CREATE) ? NULL : widen(path);
    HANDLE h = w == NULL ? INVALID_HANDLE_VALUE
                         : CreateFileW(w, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        int r = w != NULL && GetLastError() == ERROR_FILE_EXISTS ? 0 : -1;
        free(w);
        return r;
    }
    bool ok = true;
    for (DWORD done = 0; ok && len > 0; p += done, len -= done) {
        ok = WriteFile(h, p, len > ((size_t)1 << 20) ? (DWORD)1 << 20 : (DWORD)len, &done, NULL) && done > 0;
    }
    ok = ok && !fail_at(STEP_WRITE) && FlushFileBuffers(h) && !fail_at(STEP_FLUSH);
    if (!CloseHandle(h) || fail_at(STEP_CLOSE)) ok = false;
    if (!ok) DeleteFileW(w);
    free(w);
    return ok ? 1 : -1;
}

int cint_bridge_make_dir(const char *path)
{
    wchar_t *w = widen(path);
    int r = w == NULL ? -1 : CreateDirectoryW(w, NULL) ? 1 : GetLastError() == ERROR_ALREADY_EXISTS ? 0 : -1;
    free(w);
    return r;
}

void cint_bridge_remove(const char *path, bool dir)
{
    wchar_t *w = widen(path);
    if (w != NULL) (void)(dir ? RemoveDirectoryW(w) : DeleteFileW(w));
    free(w);
}

static bool replace_file(const char *from, const char *to)
{
    wchar_t *a = widen(from), *b = widen(to);
    bool ok = a != NULL && b != NULL && MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    free(a);
    free(b);
    return ok;
}

bool cint_bridge_read_file(const char *path, size_t limit, uint8_t **bytes, size_t *len)
{
    wchar_t *w = widen(path);
    HANDLE h = w == NULL ? INVALID_HANDLE_VALUE
                         : CreateFileW(w, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    LARGE_INTEGER size = {.QuadPart = 0};
    uint8_t *buf = NULL;
    DWORD got = 0;
    bool ok = h != INVALID_HANDLE_VALUE && GetFileSizeEx(h, &size) && (uint64_t)size.QuadPart <= limit
              && limit < MAXDWORD && (buf = malloc((size_t)size.QuadPart + 1)) != NULL
              && ReadFile(h, buf, (DWORD)size.QuadPart, &got, NULL) && got == (DWORD)size.QuadPart;
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    free(w);
    if (!ok) free(buf);
    else { *bytes = buf; *len = got; }
    return ok;
}

bool cint_bridge_root_open(const char *dir, cint_bridge_root **out)
{
    wchar_t *w = dir != NULL && out != NULL ? widen(dir) : NULL;
    HANDLE h = w == NULL ? INVALID_HANDLE_VALUE
                         : CreateFileW(w, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                       NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    cint_bridge_root *r = h != INVALID_HANDLE_VALUE ? calloc(1, sizeof *r) : NULL;
    BY_HANDLE_FILE_INFORMATION info;
    free(w);
    if (r != NULL && GetFileInformationByHandle(h, &info) && (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        r->h = h;
        r->path = final_path(h);
    }
    if (r == NULL || r->path == NULL) {
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        free(r);
        return false;
    }
    *out = r;
    return true;
}

void cint_bridge_root_close(cint_bridge_root *root)
{
    if (root == NULL) return;
    CloseHandle(root->h);
    free(root->path);
    free(root);
}

static bool read_handle(cint_bridge_root *root, const char *rel, size_t limit, uint8_t **bytes, size_t *len)
{
    wchar_t *r = widen(rel), *full = NULL, *fp = NULL;
    size_t n = wcslen(root->path), m = r != NULL ? wcslen(r) : 0;
    bool ok = r != NULL && (full = malloc((n + m + 2) * sizeof *full)) != NULL;
    HANDLE h = INVALID_HANDLE_VALUE;
    file_id id;
    DWORD attributes = 0;
    LARGE_INTEGER size = {.QuadPart = 0};
    uint8_t *buf = NULL;
    if (ok) {
        memcpy(full, root->path, n * sizeof *full);
        if (n > 0 && full[n - 1] != L'\\') full[n++] = L'\\';
        for (size_t i = 0; i <= m; i++) full[n + i] = r[i] == L'/' ? L'\\' : r[i];
        h = CreateFileW(full, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    }
    ok = h != INVALID_HANDLE_VALUE && GetFileType(h) == FILE_TYPE_DISK && handle_id(h, &id, &attributes)
         && (attributes & NOT_FILE) == 0 && (fp = final_path(h)) != NULL && wcscmp(fp, full) == 0
         && GetFileSizeEx(h, &size) && (uint64_t)size.QuadPart <= limit && remember_input(id)
         && (buf = malloc((size_t)size.QuadPart + 1)) != NULL;
    size_t got = 0;
    for (DWORD done = 1; ok && done > 0; got += done) {
        DWORD want = (DWORD)((size_t)size.QuadPart + 1 - got > ((size_t)1 << 20) ? (size_t)1 << 20
                                                                                 : (size_t)size.QuadPart + 1 - got);
        ok = want > 0 && ReadFile(h, buf + got, want, &done, NULL);
    }
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    if (got == (size_t)size.QuadPart && buf != NULL) { *bytes = buf; *len = got; }
    else { free(buf); buf = NULL; }
    free(r);
    free(full);
    free(fp);
    return buf != NULL;
}

#else
struct cint_bridge_root { int fd; };

static file_id identity_of(const struct stat *st)
{
    file_id id = {(uint64_t)st->st_dev, 0, (uint64_t)st->st_ino};
    return id;
}

static bool target_ok(const char *path)
{
    struct stat st;
    if (lstat(path, &st) == 0) return S_ISREG(st.st_mode) && !is_input(identity_of(&st));
    return errno == ENOENT;
}

int cint_bridge_write_new(const char *path, const uint8_t *p, size_t len)
{
    int fd = fail_at(STEP_CREATE) ? -1 : open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, (mode_t)0666);
    if (fd < 0) return !fail_at(STEP_CREATE) && errno == EEXIST ? 0 : -1;
    bool ok = true;
    while (ok && len > 0) {
        ssize_t w = write(fd, p, len > ((size_t)1 << 20) ? (size_t)1 << 20 : len);
        if (w < 0 && errno == EINTR) continue;
        ok = w > 0;
        p += ok ? (size_t)w : 0;
        len -= ok ? (size_t)w : 0;
    }
    ok = ok && !fail_at(STEP_WRITE) && fsync(fd) == 0 && !fail_at(STEP_FLUSH);
    if (close(fd) != 0 || fail_at(STEP_CLOSE)) ok = false;
    if (!ok) (void)unlink(path);
    return ok ? 1 : -1;
}

int cint_bridge_make_dir(const char *path) { return mkdir(path, 0777) == 0 ? 1 : errno == EEXIST ? 0 : -1; }

void cint_bridge_remove(const char *path, bool dir) { (void)(dir ? rmdir(path) : unlink(path)); }

/* rename(2), then a best-effort flush of the directory, so the new entry is
 * durable; its failure does not undo the rename (crash durability is Open). */
static bool replace_file(const char *from, const char *to)
{
    if (rename(from, to) != 0) return false;
    const char *slash = strrchr(to, '/');
    char *dir = slash == NULL ? cint_bridge_join(".", "", "") : cint_bridge_join(to, "", "");
    if (dir != NULL && slash != NULL) dir[slash == to ? 1 : (size_t)(slash - to)] = '\0';
    int fd = dir != NULL ? open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1;
    if (fd >= 0) {
        (void)fsync(fd);
        (void)close(fd);
    }
    free(dir);
    return true;
}

bool cint_bridge_read_file(const char *path, size_t limit, uint8_t **bytes, size_t *len)
{
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    struct stat st;
    uint8_t *buf = NULL;
    size_t got = 0;
    bool ok = fd >= 0 && fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && (uint64_t)st.st_size <= limit
              && (buf = malloc((size_t)st.st_size + 1)) != NULL;
    for (ssize_t r = 1; ok && got < (size_t)st.st_size && r != 0;) {
        r = read(fd, buf + got, (size_t)st.st_size - got);
        ok = r >= 0 ? (got += (size_t)r, true) : errno == EINTR;
    }
    if (fd >= 0) (void)close(fd);
    if (!ok || got != (size_t)st.st_size) { free(buf); return false; }
    *bytes = buf;
    *len = got;
    return true;
}

bool cint_bridge_root_open(const char *dir, cint_bridge_root **out)
{
    int fd = dir != NULL && out != NULL ? open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1;
    cint_bridge_root *r = fd >= 0 ? malloc(sizeof *r) : NULL;
    if (r == NULL) {
        if (fd >= 0) (void)close(fd);
        return false;
    }
    r->fd = fd;
    *out = r;
    return true;
}

void cint_bridge_root_close(cint_bridge_root *root)
{
    if (root == NULL) return;
    (void)close(root->fd);
    free(root);
}

/* True when `dir` has an entry spelled `name` byte for byte. A file system that
 * folds case (the WSL mount of F:, macOS by default) also opens a name that
 * differs only in case; this refuses it, as the final-path check does on
 * Windows, so a module path resolves the same way on every host (CINTC-12). */
static bool entry_exact(int dir, const char *name)
{
    int d = dup(dir);
    DIR *ds = d >= 0 ? fdopendir(d) : NULL;
    struct dirent *e;
    bool found = false;
    if (ds == NULL) {
        if (d >= 0) (void)close(d);
        return false;
    }
    rewinddir(ds);   /* the duplicate shares the offset of earlier scans */
    while (!found && (e = readdir(ds)) != NULL) found = strcmp(e->d_name, name) == 0;
    (void)closedir(ds);
    return found;
}

static bool read_handle(cint_bridge_root *root, const char *rel, size_t limit, uint8_t **bytes, size_t *len)
{
    char *p = cint_bridge_join(rel, "", ""), *s = p;
    int dir = root->fd, fd = -1;
    while (s != NULL) {   /* one segment at a time, never following a link */
        char *slash = strchr(s, '/');
        if (slash != NULL) *slash = '\0';
        fd = openat(dir, s, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | (slash != NULL ? O_DIRECTORY : O_NONBLOCK));
        if (fd >= 0 && !entry_exact(dir, s)) {
            (void)close(fd);
            fd = -1;
        }
        if (dir != root->fd) (void)close(dir);
        dir = fd;
        s = fd >= 0 && slash != NULL ? slash + 1 : NULL;
    }
    free(p);
    struct stat st;
    uint8_t *buf = NULL;
    bool ok = fd >= 0 && fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && (uint64_t)st.st_size <= limit
              && remember_input(identity_of(&st)) && (buf = malloc((size_t)st.st_size + 1)) != NULL;
    size_t got = 0;
    for (ssize_t r = 1; ok && r != 0;) {   /* to end of file: a file that grew is refused */
        r = read(fd, buf + got, (size_t)st.st_size + 1 - got);
        ok = r >= 0 ? (got += (size_t)r) <= (size_t)st.st_size : errno == EINTR;
    }
    if (fd >= 0) (void)close(fd);
    if (ok && got == (size_t)st.st_size) {
        *bytes = buf;
        *len = got;
        return true;
    }
    free(buf);
    return false;
}
#endif

bool cint_bridge_read_input(cint_bridge_root *root, const char *rel, size_t limit, uint8_t **bytes, size_t *len)
{
    if (root == NULL || bytes == NULL || len == NULL || !cint_bridge_rel_ok(rel) || limit >= SIZE_MAX) return false;
    return read_handle(root, rel, limit, bytes, len);
}

static bool commit(const char *path, const uint8_t *bytes, size_t len)
{
    char *stage = NULL;
    int r = target_ok(path) ? 0 : -1;
    for (unsigned k = 0; r == 0 && k < CINT_BRIDGE_STAGE_TRIES; k++) {
        free(stage);
        stage = cint_bridge_stage_name(path, k);
        r = stage != NULL ? cint_bridge_write_new(stage, bytes, len) : -1;
    }
    bool ok = r == 1 && !fail_at(STEP_REPLACE) && replace_file(stage, path);
    if (r == 1 && !ok) cint_bridge_remove(stage, false);
    free(stage);
    return ok;
}

bool cint_commit_output(const char *path, const uint8_t *bytes, size_t len, size_t limit)
{
    if (!cint_bridge_path_ok(path) || (bytes == NULL && len != 0) || len > limit) return false;
    return commit(path, bytes == NULL ? (const uint8_t *)"" : bytes, len);
}
