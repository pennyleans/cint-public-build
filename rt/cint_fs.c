/* cint_fs.c: the `fs/1` service of the runtime's standard service table (SPEC-03 H-8a, A-10;
 * SPEC-07 SEC-OS-5 to SEC-OS-8, SEC-OS-12, SEC-CAP-6, SEC-CAP-14; I/O services lane U4;
 * rt/OPEN.md RT-OQ-72).
 *
 * rt/cint_rt.c includes this file, as it includes rt/cint_host.c, so the code is part of the
 * cint_rt library and is counted in its own SPEC-09 4.2 row. The grant set of cint_ctx_config
 * is lines separated by '\n', each the canonical capability text of SEC-CAP-6 and, for
 * `fs.read` and `fs.write`, `=` and the host directory its logical root maps to (SEC-OS-6);
 * context creation parses the lines (cint_rt_fs_open), opens each fs root once (SEC-OS-7) and
 * refuses a line it cannot take. A host import row whose name holds '/' names a service extern,
 * `<service>/<version>.<name>`; cint_rt_service_find (rt/cint_console.c, which cint_rt.c
 * includes after this file) resolves it against the table of the rows of fs/1 at the end of
 * this file and of console/1 there, by name, effect class, parameter count and signature bytes,
 * to a function of the context's own (user = ctx). The grant lines that map no root keep their
 * capability bits here too (cint_rt_grant_caps), for the console. The first argument of a service call is the Grants carrier,
 * an I64 handle of which 0 is the context's grant set (SEC-OS-1a, SEC-OS-2); the declared
 * parameters follow it. Every failure of an operation is a value of lib/fs.ci's FsError, in
 * tag order path 1, denied 2, not_found 3, exists 4, not_directory 5, is_directory 6,
 * not_empty 7, bounds 8, quota 9 (SEC-OS-8); a host error that has no status of its own
 * returns CINT_RESOURCE, the boundary fault host.error.<name> (A-6). Logical names are looked
 * up exactly, links are never followed, and a put writes a temporary entry beside its target
 * and renames it over the target (SEC-OS-5a, SEC-OS-7, SEC-OS-8).
 */

#if !defined(_WIN32)
#include <dirent.h>
#include <sys/stat.h>
#endif

#define FS_PATH 1
#define FS_DENIED 2
#define FS_NOT_FOUND 3
#define FS_EXISTS 4
#define FS_NOT_DIRECTORY 5
#define FS_IS_DIRECTORY 6
#define FS_BOUNDS 8
#define FS_QUOTA 9
#define FS_HOST (-1)       /* no FsError value: the call returns CINT_RESOURCE */
#define FS_MAX_PATH 4095u  /* a logical path this runtime takes */
#define FS_MAX_HOST 2047u  /* a full host path on Windows, in UTF-16 units */
#define FS_CHUNK 16384u

typedef struct fs_grant {
    uint32_t write;           /* 1 for fs.write, 0 for fs.read */
    uint32_t root_len;        /* the logical root: "/" or "/a/b", NUL-terminated in the lines */
    const uint8_t *root;
#if defined(_WIN32)
    wchar_t *base;            /* the final path of the opened directory, \\?\ prefixed */
    size_t base_len;
#else
    int fd;
#endif
} fs_grant;

struct cint_rt_fs {
    uint8_t *lines;           /* the grant lines, each NUL-terminated, '=' replaced by NUL */
    size_t lines_length;
    uint32_t count;           /* the fs grants, in line order */
    uint32_t serial;          /* the temporary names of put */
    uint32_t caps;            /* CINT_RT_CAP_* bits: the capabilities of the set that map no root */
    uint32_t reserved;
    fs_grant grants[1];       /* count of them */
};

static size_t fs_struct_size(uint32_t count)
{
    return sizeof(struct cint_rt_fs) + (count > 1u ? (size_t)(count - 1u) * sizeof(fs_grant) : 0u);
}

/* ---------------------------------------------------------------- the grant lines (SEC-CAP-6, SEC-OS-5) */

static bool fs_seg_char(uint8_t c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
           c == '.';
}

/* Whether the part of a segment before its first '.' is a Windows reserved device name. */
static bool fs_reserved(const uint8_t *s, size_t n)
{
    char up[4];
    size_t k = 0u;
    while (k < n && s[k] != '.') k++;
    if (k < 3u || k > 4u) return false;
    for (size_t i = 0u; i < k; i++) up[i] = (char)(s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i]);
    if (k == 3u) {
        return memcmp(up, "CON", 3u) == 0 || memcmp(up, "PRN", 3u) == 0 || memcmp(up, "AUX", 3u) == 0 ||
               memcmp(up, "NUL", 3u) == 0;
    }
    return (memcmp(up, "COM", 3u) == 0 || memcmp(up, "LPT", 3u) == 0) && up[3] >= '1' && up[3] <= '9';
}

/* The logical path syntax of SEC-OS-5: absolute, '/'-separated segments of the segment
 * characters, none empty, `.`, `..` or ending in '.', none a reserved device name, and no
 * trailing '/'; `/` alone has no segment. */
static bool fs_path_ok(const uint8_t *p, size_t n)
{
    size_t i = 1u;
    if (n == 0u || n > FS_MAX_PATH || p[0] != '/') return false;
    if (n == 1u) return true;
    while (i <= n) {
        size_t start = i;
        while (i < n && p[i] != '/') {
            if (!fs_seg_char(p[i])) return false;
            i++;
        }
        if (i == start || p[i - 1u] == '.' || fs_reserved(p + start, i - start)) return false;
        i++;
    }
    return true;
}

/* Whether the grant's root is the path or an ancestor of it on a '/' boundary. */
static bool fs_covers(const fs_grant *g, const uint8_t *p, size_t n)
{
    if (g->root_len == 1u) return true;
    return n >= g->root_len && memcmp(p, g->root, g->root_len) == 0 && (n == g->root_len || p[g->root_len] == '/');
}

static void *fs_alloc(cint_ctx *ctx, size_t n, size_t align)
{
    cint_allocator a = ctx->allocator;
    return a.alloc(a.user, n, align);
}

static void fs_release(cint_ctx *ctx, void *p, size_t n, size_t align)
{
    cint_allocator a = ctx->allocator;
    if (p != NULL) a.release(a.user, p, n, align);
}

#if defined(_WIN32)
static void fs_close_root(cint_ctx *ctx, fs_grant *g)
{
    fs_release(ctx, g->base, (g->base_len + 1u) * sizeof(wchar_t), _Alignof(wchar_t));
    g->base = NULL;
}

/* Opens the host directory once and keeps its final path, the anchor of every full path
 * below (SEC-OS-7; the bridge's precedent, rt/cint_bridge.c). */
static bool fs_open_root(cint_ctx *ctx, fs_grant *g, const char *host)
{
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, host, -1, NULL, 0);
    wchar_t *w = n > 0 ? (wchar_t *)fs_alloc(ctx, (size_t)n * sizeof *w, _Alignof(wchar_t)) : NULL;
    HANDLE h = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION info;
    DWORD len = 0;
    bool ok = false;
    if (w != NULL && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, host, -1, w, n) == n) {
        h = CreateFileW(w, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    }
    fs_release(ctx, w, (size_t)(n > 0 ? n : 0) * sizeof *w, _Alignof(wchar_t));
    if (h == INVALID_HANDLE_VALUE) return false;
    if (GetFileInformationByHandle(h, &info) && (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0u) {
        len = GetFinalPathNameByHandleW(h, NULL, 0, FILE_NAME_NORMALIZED);
    }
    if (len > 0 && len <= FS_MAX_HOST) {
        g->base = (wchar_t *)fs_alloc(ctx, (size_t)len * sizeof(wchar_t), _Alignof(wchar_t));
        g->base_len = (size_t)len - 1u;
        ok = g->base != NULL && GetFinalPathNameByHandleW(h, g->base, len, FILE_NAME_NORMALIZED) + 1 == len;
        while (ok && g->base_len > 0u && g->base[g->base_len - 1u] == L'\\') g->base[--g->base_len] = L'\0';
    }
    CloseHandle(h);
    if (!ok) fs_close_root(ctx, g);
    return ok;
}
#else
static void fs_close_root(cint_ctx *ctx, fs_grant *g)
{
    (void)ctx;
    if (g->fd >= 0) (void)close(g->fd);
    g->fd = -1;
}

static bool fs_open_root(cint_ctx *ctx, fs_grant *g, const char *host)
{
    (void)ctx;
    g->fd = open(host, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    return g->fd >= 0;
}
#endif

void cint_rt_fs_close(cint_ctx *ctx)
{
    struct cint_rt_fs *fs = ctx->fs;
    if (fs == NULL) return;
    for (uint32_t i = 0u; i < fs->count; i++) fs_close_root(ctx, &fs->grants[i]);
    fs_release(ctx, fs->lines, fs->lines_length + 1u, 1u);
    fs_release(ctx, fs, fs_struct_size(fs->count), _Alignof(struct cint_rt_fs));
    ctx->fs = NULL;
}

/* The capability bit of a grant line that maps no root: the canonical text `console.input` or
 * `console.raw`, with or without arguments after ':' (rt/cint_console.c; RT-OQ-73). */
static uint32_t fs_cap_bit(const uint8_t *s, size_t len)
{
    const uint8_t *colon = (const uint8_t *)memchr(s, ':', len);
    size_t name_len = colon != NULL ? (size_t)(colon - s) : len;
    if (name_len == 13u && memcmp(s, "console.input", 13u) == 0) return CINT_RT_CAP_CONSOLE_INPUT;
    if (name_len == 11u && memcmp(s, "console.raw", 11u) == 0) return CINT_RT_CAP_CONSOLE_RAW;
    return 0u;
}

uint32_t cint_rt_grant_caps(const cint_ctx *ctx)
{
    return ctx->fs != NULL ? ctx->fs->caps : 0u;
}

/* One grant line, [s, e) with e its NUL: a capability name of 1 to 63 segment characters, its
 * arguments after ':', and the host directory after '='. Writes the fs grant when `g` is given
 * and the line is one; CINT_REFUSED for a line this runtime cannot take. */
static cint_status fs_line(uint8_t *s, uint8_t *e, fs_grant *g, bool *is_fs)
{
    uint8_t *eq = (uint8_t *)memchr(s, '=', (size_t)(e - s)), *colon;
    uint8_t *text_end = eq != NULL ? eq : e;
    size_t name_len;
    colon = (uint8_t *)memchr(s, ':', (size_t)(text_end - s));
    name_len = (size_t)((colon != NULL ? colon : text_end) - s);
    if (name_len == 0u || name_len > 63u) return CINT_REFUSED;
    for (size_t i = 0u; i < name_len; i++) {
        if (!fs_seg_char(s[i])) return CINT_REFUSED;
    }
    *is_fs = (name_len == 7u && memcmp(s, "fs.read", 7u) == 0) || (name_len == 8u && memcmp(s, "fs.write", 8u) == 0);
    if (!*is_fs) return eq == NULL ? CINT_OK : CINT_REFUSED;   /* a host directory maps a path root only */
    if (colon == NULL || eq == NULL || eq + 1 == e || !fs_path_ok(colon + 1, (size_t)(text_end - colon - 1))) {
        return CINT_REFUSED;
    }
    if (g != NULL) {
        g->write = name_len == 8u ? 1u : 0u;
        g->root = colon + 1;
        g->root_len = (uint32_t)(text_end - colon - 1);
        *eq = '\0';
    }
    return CINT_OK;
}

cint_status cint_rt_fs_open(cint_ctx *ctx, const cint_ctx_config *config)
{
    size_t n = config->grants_length, at = 0u;
    uint8_t *lines;
    uint32_t count = 0u, k = 0u, caps = 0u;
    struct cint_rt_fs *fs = NULL;
    if (n == 0u) return CINT_OK;
    if (memchr(config->grants, '\0', n) != NULL) return CINT_REFUSED;
    lines = (uint8_t *)fs_alloc(ctx, n + 1u, 1u);
    if (lines == NULL) return CINT_RESOURCE;
    memcpy(lines, config->grants, n);
    lines[n] = '\0';
    if (lines[n - 1u] == '\n') lines[n - 1u] = '\0';
    for (at = 0u; at < n; at++) {
        if (lines[at] == '\n') lines[at] = '\0';
    }
    /* Two passes: one to validate and count, one to fill and open. */
    for (int pass = 0; pass < 2; pass++) {
        at = 0u;
        for (size_t len = 0u; at < n; at += len + 1u) {
            uint8_t *s = lines + at, *e;
            bool is_fs = false;
            len = strlen((const char *)s);   /* before fs_line shortens an fs line at its '=' */
            e = s + len;
            fs_grant *g = pass == 0 ? NULL : &fs->grants[k];
            if (fs_line(s, e, g, &is_fs) != CINT_OK) {
                if (ctx->fs != NULL) cint_rt_fs_close(ctx);
                else fs_release(ctx, lines, n + 1u, 1u);
                return CINT_REFUSED;
            }
            if (!is_fs) {
                if (pass == 0) caps |= fs_cap_bit(s, len);
                continue;
            }
            if (pass == 0) {
                count++;
                continue;
            }
            k++;
            fs->count = k;
            if (!fs_open_root(ctx, g, (const char *)g->root + g->root_len + 1u)) {   /* after the '=' */
                cint_rt_fs_close(ctx);
                return CINT_REFUSED;
            }
        }
        if (pass == 0) {
            fs = (struct cint_rt_fs *)fs_alloc(ctx, fs_struct_size(count), _Alignof(struct cint_rt_fs));
            if (fs == NULL) {
                fs_release(ctx, lines, n + 1u, 1u);
                return CINT_RESOURCE;
            }
            memset(fs, 0, fs_struct_size(count));
            fs->lines = lines;
            fs->lines_length = n;
            fs->caps = caps;
            ctx->fs = fs;
        }
    }
    return CINT_OK;
}

/* ---------------------------------------------------------------- arguments and results */

static bool fs_handle_ok(const cint_value *v)
{
    return v->kind == CINT_VAL_SCALAR && v->type.code == CINT_TAG_I64 && v->as.i64 == 0;
}

static bool fs_bytes_ok(const cint_value *v, uint32_t perm)
{
    return v->kind == CINT_VAL_VIEW && v->type.code == CINT_TAG_U8 && v->as.view.perm == perm;
}

/* The bytes of an `in` view, through its stride. */
static void fs_gather(const cint_arg_view *v, int64_t from, uint8_t *dst, size_t n)
{
    const uint8_t *p = (const uint8_t *)v->mem.u.host.ptr;
    for (size_t i = 0u; i < n; i++) dst[i] = p[(ptrdiff_t)(from + (int64_t)i) * (ptrdiff_t)v->byte_stride[0]];
}

/* The result: the error alternative for a status, the value for 0, CINT_RESOURCE for a host
 * error (A-6). */
static cint_status fs_done(cint_value *r, int st, int64_t value)
{
    if (st == FS_HOST) return CINT_RESOURCE;
    if (st != 0) {
        r->is_error = 1u;
        r->error = st;
        return CINT_OK;
    }
    r->as.i64 = value;
    return CINT_OK;
}

/* The path argument into `p` (FS_PATH when too long or malformed) and the grant that covers
 * it (FS_DENIED for none): syntax before permission (SEC-OS-5). */
static int fs_target(cint_ctx *ctx, const cint_value *path, bool write, uint8_t p[FS_MAX_PATH + 1u], size_t *n,
                     const fs_grant **grant)
{
    struct cint_rt_fs *fs = ctx->fs;
    int64_t len = path->as.view.shape[0];
    if (len < 0 || len > (int64_t)FS_MAX_PATH) return FS_PATH;
    *n = (size_t)len;
    fs_gather(&path->as.view, 0, p, *n);
    p[*n] = '\0';
    if (!fs_path_ok(p, *n)) return FS_PATH;
    for (uint32_t i = 0u; fs != NULL && i < fs->count; i++) {
        const fs_grant *g = &fs->grants[i];
        if ((g->write != 0u) == write && fs_covers(g, p, *n)) {
            *grant = g;
            return 0;
        }
    }
    return FS_DENIED;
}

/* The path below the grant root: "a/b" for "/x/a/b" under "/x" or "/"; empty for the root. */
static const uint8_t *fs_rel(const fs_grant *g, const uint8_t *p, size_t n, size_t *rel_len)
{
    size_t off = g->root_len == 1u ? 1u : g->root_len + 1u;
    *rel_len = n > off ? n - off : 0u;
    return p + (n > off ? off : n);
}

#if !defined(_WIN32)
/* Whether two names are equal with ASCII letters folded (SEC-OS-5a). */
static bool fs_fold_equal(const uint8_t *a, size_t n, const char *b)
{
    for (size_t i = 0u; i < n; i++) {
        uint8_t x = a[i], y = (uint8_t)b[i];
        if (y == 0u) return false;
        if (x >= 'A' && x <= 'Z') x = (uint8_t)(x + 32u);
        if (y >= 'A' && y <= 'Z') y = (uint8_t)(y + 32u);
        if (x != y) return false;
    }
    return b[n] == '\0';
}

/* ---------------------------------------------------------------- POSIX: handle-relative, one segment at a time */

static int fs_errno(int e)
{
    switch (e) {
    case ENOENT: case ELOOP: case ENXIO: return FS_NOT_FOUND;
    case ENOTDIR: return FS_NOT_DIRECTORY;
    case EISDIR: return FS_IS_DIRECTORY;
    case EACCES: case EPERM: case EROFS: return FS_DENIED;
    case EEXIST: case ENOTEMPTY: return FS_EXISTS;
    case ENOSPC: case EFBIG:
#if defined(EDQUOT)
    case EDQUOT:
#endif
        return FS_QUOTA;
    case ENAMETOOLONG: return FS_PATH;
    default: return FS_HOST;
    }
}

/* Whether the directory holds an entry spelled exactly `name` (SEC-OS-5a: the host's open may
 * have matched another case), or, with `folded`, one that equals it with ASCII letters folded. */
static bool fs_entry(int dir, const uint8_t *name, size_t n, bool folded)
{
    int d = dup(dir);
    DIR *ds = d >= 0 ? fdopendir(d) : NULL;
    struct dirent *e;
    bool found = false;
    if (ds == NULL) {
        if (d >= 0) (void)close(d);
        return false;
    }
    rewinddir(ds);
    while (!found && (e = readdir(ds)) != NULL) {
        found = folded ? fs_fold_equal(name, n, e->d_name)
                       : strlen(e->d_name) == n && memcmp(e->d_name, name, n) == 0;
    }
    (void)closedir(ds);
    return found;
}

/* Opens the directories of `rel` before its last segment, never following a link, and leaves
 * the last segment in `last`. *dir is the grant's root or an opened directory to close. */
static int fs_walk(const fs_grant *g, const uint8_t *rel, size_t n, int *dir, uint8_t last[256], size_t *last_len)
{
    size_t i = 0u;
    *dir = g->fd;
    for (;;) {
        size_t start = i, len;
        int fd, st;
        while (i < n && rel[i] != '/') i++;
        len = i - start;
        if (len > 255u) {
            st = FS_PATH;
        } else if (i == n) {
            memcpy(last, rel + start, len);
            last[len] = '\0';
            *last_len = len;
            return 0;
        } else {
            memcpy(last, rel + start, len);
            last[len] = '\0';
            fd = openat(*dir, (const char *)last, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_DIRECTORY);
            st = fd < 0 ? fs_errno(errno) : fs_entry(*dir, last, len, false) ? 0 : FS_NOT_FOUND;
            if (st == 0) {
                if (*dir != g->fd) (void)close(*dir);
                *dir = fd;
                i++;
                continue;
            }
            if (fd >= 0) (void)close(fd);
        }
        if (*dir != g->fd) (void)close(*dir);
        *dir = g->fd;
        return st;
    }
}

static void fs_leave(const fs_grant *g, int dir)
{
    if (dir != g->fd) (void)close(dir);
}

/* Opens the regular file at the path for reading and reports its size. */
static int fs_open_file(const fs_grant *g, const uint8_t *p, size_t n, int *fd, int64_t *size)
{
    size_t rel_len, last_len = 0u;
    const uint8_t *rel = fs_rel(g, p, n, &rel_len);
    uint8_t last[256];
    int dir, st;
    struct stat sb;
    if (rel_len == 0u) return FS_IS_DIRECTORY;
    st = fs_walk(g, rel, rel_len, &dir, last, &last_len);
    if (st != 0) return st;
    *fd = openat(dir, (const char *)last, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (*fd < 0) {
        st = fs_errno(errno);
    } else if (fstat(*fd, &sb) != 0) {
        st = FS_HOST;
    } else if (S_ISDIR(sb.st_mode)) {
        st = FS_IS_DIRECTORY;
    } else if (!S_ISREG(sb.st_mode) || !fs_entry(dir, last, last_len, false)) {
        st = FS_NOT_FOUND;
    } else {
        *size = (int64_t)sb.st_size;
    }
    if (st != 0 && *fd >= 0) (void)close(*fd);
    fs_leave(g, dir);
    return st;
}

static int fs_read_into(int fd, int64_t offset, uint8_t *dst, int64_t want, int64_t *got)
{
    *got = 0;
    while (*got < want) {
        size_t chunk = (size_t)(want - *got) > FS_CHUNK ? FS_CHUNK : (size_t)(want - *got);
        ssize_t r = pread(fd, dst + *got, chunk, (off_t)(offset + *got));
        if (r < 0) {
            if (errno == EINTR) continue;
            return fs_errno(errno);
        }
        if (r == 0) break;
        *got += (int64_t)r;
    }
    return 0;
}

/* put (SEC-OS-8): the target may be absent or a regular file of that exact name; a
 * directory is `is_directory`, any other entry or a case-fold collision is `exists`. The
 * bytes go to `.cint-put-<serial>` beside it, synced, then renamed over the target. */
static int fs_put(cint_ctx *ctx, const fs_grant *g, const uint8_t *p, size_t n, const cint_arg_view *bytes)
{
    size_t rel_len, last_len = 0u;
    const uint8_t *rel = fs_rel(g, p, n, &rel_len);
    uint8_t last[256], chunk[FS_CHUNK];
    char tmp[32];
    int dir, st, fd = -1;
    struct stat sb;
    int64_t done = 0, total = bytes->shape[0];
    if (rel_len == 0u) return FS_IS_DIRECTORY;
    st = fs_walk(g, rel, rel_len, &dir, last, &last_len);
    if (st != 0) return st;
    if (fstatat(dir, (const char *)last, &sb, AT_SYMLINK_NOFOLLOW) == 0) {
        st = S_ISDIR(sb.st_mode) ? FS_IS_DIRECTORY : !S_ISREG(sb.st_mode) || !fs_entry(dir, last, last_len, false) ? FS_EXISTS : 0;
    } else if (errno != ENOENT) {
        st = fs_errno(errno);
    } else if (fs_entry(dir, last, last_len, true)) {
        st = FS_EXISTS;
    }
    for (int tries = 0; st == 0 && tries < 8; tries++) {
        snprintf(tmp, sizeof tmp, ".cint-put-%u", (unsigned)++ctx->fs->serial);
        fd = openat(dir, tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0666);
        if (fd >= 0 || errno != EEXIST) break;
    }
    if (st == 0 && fd < 0) st = fs_errno(errno);
    while (st == 0 && done < total) {
        size_t want = (size_t)(total - done) > FS_CHUNK ? FS_CHUNK : (size_t)(total - done), at = 0u;
        fs_gather(bytes, done, chunk, want);
        while (st == 0 && at < want) {
            ssize_t w = write(fd, chunk + at, want - at);
            if (w < 0 && errno == EINTR) continue;
            if (w <= 0) st = w < 0 ? fs_errno(errno) : FS_HOST;
            else at += (size_t)w;
        }
        done += (int64_t)want;
    }
    if (st == 0 && fsync(fd) != 0) st = fs_errno(errno);
    if (fd >= 0 && close(fd) != 0 && st == 0) st = fs_errno(errno);
    if (st == 0 && renameat(dir, tmp, dir, (const char *)last) != 0) st = fs_errno(errno);
    if (st != 0 && fd >= 0) (void)unlinkat(dir, tmp, 0);
    if (st == 0) (void)fsync(dir);
    fs_leave(g, dir);
    return st;
}

#else
/* ---------------------------------------------------------------- Windows: full paths under the root's final path */

static bool fs_wfold_equal(const wchar_t *a, const wchar_t *b)
{
    for (;; a++, b++) {
        wchar_t x = *a, y = *b;
        if (x >= L'A' && x <= L'Z') x = (wchar_t)(x + 32);
        if (y >= L'A' && y <= L'Z') y = (wchar_t)(y + 32);
        if (x != y) return false;
        if (x == L'\0') return true;
    }
}

/* `<parent>\.cint-put-<serial>`, the temporary entry of put. */
static void fs_tmp_name(wchar_t *dst, const wchar_t *parent, unsigned serial)
{
    static const wchar_t stem[] = L"\\.cint-put-";
    size_t n = wcslen(parent), m = wcslen(stem), k = 0u;
    wchar_t digits[16];
    memcpy(dst, parent, n * sizeof(wchar_t));
    memcpy(dst + n, stem, m * sizeof(wchar_t));
    do {
        digits[k++] = (wchar_t)(L'0' + serial % 10u);
        serial /= 10u;
    } while (serial != 0u);
    while (k > 0u) dst[n + m++] = digits[--k];
    dst[n + m] = L'\0';
}

static int fs_win_error(DWORD e)
{
    switch (e) {
    case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: case ERROR_INVALID_NAME: case ERROR_BAD_PATHNAME:
    case ERROR_CANT_ACCESS_FILE: case ERROR_CANT_RESOLVE_FILENAME:
        return FS_NOT_FOUND;
    case ERROR_DIRECTORY: return FS_NOT_DIRECTORY;
    case ERROR_ACCESS_DENIED: case ERROR_WRITE_PROTECT: case ERROR_SHARING_VIOLATION: case ERROR_LOCK_VIOLATION:
        return FS_DENIED;
    case ERROR_ALREADY_EXISTS: case ERROR_FILE_EXISTS: case ERROR_DIR_NOT_EMPTY: return FS_EXISTS;
    case ERROR_DISK_FULL: case ERROR_HANDLE_DISK_FULL: case ERROR_FILE_TOO_LARGE: return FS_QUOTA;
    case ERROR_FILENAME_EXCED_RANGE: return FS_PATH;
    default: return FS_HOST;
    }
}

/* The full host path of the first `n` bytes of `rel` (ASCII, '/' to '\'); false when too long. */
static bool fs_full(const fs_grant *g, const uint8_t *rel, size_t n, wchar_t full[FS_MAX_HOST + 1u])
{
    size_t at = g->base_len;
    if (at + 1u + n > FS_MAX_HOST) return false;
    memcpy(full, g->base, at * sizeof(wchar_t));
    if (n > 0u) full[at++] = L'\\';
    for (size_t i = 0u; i < n; i++) full[at + i] = rel[i] == '/' ? L'\\' : (wchar_t)rel[i];
    full[at + n] = L'\0';
    return true;
}

/* Opens the object at `full` without following a final link and checks that its final path is
 * `full` exactly: another case or a link on the way is `not_found` (SEC-OS-5a, SEC-OS-7). When
 * the open fails, the directories on the way past the root (the first `from` units of `full`)
 * are opened in turn and the first that fails gives the status, as in fs_walk on POSIX: a file
 * on the way is `not_directory` there, where Windows reports a missing path. */
static int fs_open_exact(const wchar_t *full, bool want_dir, DWORD access, size_t from, HANDLE *h)
{
    wchar_t fp[FS_MAX_HOST + 1u];
    BY_HANDLE_FILE_INFORMATION info;
    DWORD len;
    int st = 0;
    *h = CreateFileW(full, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                     FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (*h == INVALID_HANDLE_VALUE) {
        st = fs_win_error(GetLastError());
        for (size_t i = from; full[i] != L'\0'; i++) {
            wchar_t on[FS_MAX_HOST + 1u];
            HANDLE d;
            int dst;
            if (i == from || full[i] != L'\\') continue;
            memcpy(on, full, i * sizeof(wchar_t));
            on[i] = L'\0';
            dst = fs_open_exact(on, true, FILE_READ_ATTRIBUTES, i, &d);
            if (dst != 0) return dst;
            CloseHandle(d);
        }
        return st;
    }
    len = GetFinalPathNameByHandleW(*h, fp, FS_MAX_HOST + 1u, FILE_NAME_NORMALIZED);
    if (!GetFileInformationByHandle(*h, &info) || len == 0 || len > FS_MAX_HOST) st = FS_HOST;
    else if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u || wcscmp(fp, full) != 0) st = FS_NOT_FOUND;
    else if (want_dir && (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0u) st = FS_NOT_DIRECTORY;
    else if (!want_dir && (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0u) st = FS_IS_DIRECTORY;
    if (st != 0) {
        CloseHandle(*h);
        *h = INVALID_HANDLE_VALUE;
    }
    return st;
}

static int fs_open_file(const fs_grant *g, const uint8_t *p, size_t n, HANDLE *h, int64_t *size)
{
    size_t rel_len;
    const uint8_t *rel = fs_rel(g, p, n, &rel_len);
    wchar_t full[FS_MAX_HOST + 1u];
    LARGE_INTEGER sz;
    int st;
    if (rel_len == 0u) return FS_IS_DIRECTORY;
    if (!fs_full(g, rel, rel_len, full)) return FS_PATH;
    st = fs_open_exact(full, false, GENERIC_READ, g->base_len, h);
    if (st != 0) return st;
    if (!GetFileSizeEx(*h, &sz)) {
        CloseHandle(*h);
        return FS_HOST;
    }
    *size = (int64_t)sz.QuadPart;
    return 0;
}

static int fs_read_into(HANDLE h, int64_t offset, uint8_t *dst, int64_t want, int64_t *got)
{
    *got = 0;
    while (*got < want) {
        OVERLAPPED ov;
        DWORD chunk = (want - *got) > (int64_t)FS_CHUNK ? FS_CHUNK : (DWORD)(want - *got), r = 0;
        memset(&ov, 0, sizeof ov);
        ov.Offset = (DWORD)((uint64_t)(offset + *got) & 0xffffffffu);
        ov.OffsetHigh = (DWORD)((uint64_t)(offset + *got) >> 32);
        if (!ReadFile(h, dst + *got, chunk, &r, &ov)) {
            if (GetLastError() == ERROR_HANDLE_EOF) break;
            return fs_win_error(GetLastError());
        }
        if (r == 0) break;
        *got += (int64_t)r;
    }
    return 0;
}

/* The entries of the parent directory: 0 when none is spelled `last` in any case, FS_EXISTS
 * for one of another case or one that is not a regular file, FS_IS_DIRECTORY for a directory,
 * and 1 for a regular file of that exact name (SEC-OS-5a). */
static int fs_scan(const wchar_t *parent, const wchar_t *last)
{
    wchar_t pattern[FS_MAX_HOST + 3u];
    WIN32_FIND_DATAW fd;
    HANDLE f;
    int st = 0;
    size_t n = wcslen(parent);
    memcpy(pattern, parent, n * sizeof(wchar_t));
    pattern[n] = L'\\';
    pattern[n + 1u] = L'*';
    pattern[n + 2u] = L'\0';
    f = FindFirstFileW(pattern, &fd);
    if (f == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : fs_win_error(GetLastError());
    do {
        if (wcscmp(fd.cFileName, last) == 0) {
            st = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0u ? FS_IS_DIRECTORY
                 : (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u ? FS_EXISTS : 1;
            break;
        }
        if (fs_wfold_equal(fd.cFileName, last)) st = FS_EXISTS;   /* kept unless the exact name follows */
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    return st;
}

static int fs_put(cint_ctx *ctx, const fs_grant *g, const uint8_t *p, size_t n, const cint_arg_view *bytes)
{
    size_t rel_len, cut;
    const uint8_t *rel = fs_rel(g, p, n, &rel_len);
    wchar_t full[FS_MAX_HOST + 1u], parent[FS_MAX_HOST + 1u], tmp[FS_MAX_HOST + 32u];
    uint8_t chunk[FS_CHUNK];
    HANDLE h = INVALID_HANDLE_VALUE;
    int st;
    int64_t done = 0, total = bytes->shape[0];
    bool renamed = false;
    if (rel_len == 0u) return FS_IS_DIRECTORY;
    if (!fs_full(g, rel, rel_len, full)) return FS_PATH;
    cut = rel_len;
    while (cut > 0u && rel[cut - 1u] != '/') cut--;
    (void)fs_full(g, rel, cut > 0u ? cut - 1u : 0u, parent);
    st = fs_open_exact(parent, true, FILE_READ_ATTRIBUTES, g->base_len, &h);
    if (st != 0) return st;
    CloseHandle(h);
    st = fs_scan(parent, full + wcslen(parent) + 1u);
    if (st == 1) st = 0;
    for (int tries = 0; st == 0 && tries < 8; tries++) {
        fs_tmp_name(tmp, parent, (unsigned)++ctx->fs->serial);
        h = CreateFileW(tmp, GENERIC_WRITE | DELETE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE || GetLastError() != ERROR_FILE_EXISTS) break;
    }
    if (st == 0 && h == INVALID_HANDLE_VALUE) st = fs_win_error(GetLastError());
    while (st == 0 && done < total) {
        size_t want = (size_t)(total - done) > FS_CHUNK ? FS_CHUNK : (size_t)(total - done), at = 0u;
        fs_gather(bytes, done, chunk, want);
        while (st == 0 && at < want) {
            DWORD w = 0;
            if (!WriteFile(h, chunk + at, (DWORD)(want - at), &w, NULL)) st = fs_win_error(GetLastError());
            else if (w == 0) st = FS_HOST;
            else at += w;
        }
        done += (int64_t)want;
    }
    if (st == 0 && !FlushFileBuffers(h)) st = fs_win_error(GetLastError());
#if defined(FILE_RENAME_FLAG_POSIX_SEMANTICS)
    if (st == 0) {   /* the atomic replace of SEC-OS-8; a volume without it falls back below */
        union { FILE_RENAME_INFO info; uint8_t raw[sizeof(FILE_RENAME_INFO) + FS_MAX_HOST * sizeof(wchar_t)]; } u;
        size_t len = wcslen(full);
        memset(&u, 0, sizeof u);
        u.info.Flags = FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS;
        u.info.FileNameLength = (DWORD)(len * sizeof(wchar_t));
        memcpy(u.info.FileName, full, (len + 1u) * sizeof(wchar_t));
        renamed = SetFileInformationByHandle(h, FileRenameInfoEx, &u, (DWORD)sizeof u) != 0;
    }
#endif
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    if (st == 0 && !renamed && !MoveFileExW(tmp, full, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        st = fs_win_error(GetLastError());
    }
    if (st != 0 && h != INVALID_HANDLE_VALUE) DeleteFileW(tmp);
    return st;
}
#endif

/* ---------------------------------------------------------------- the three operations */

#if defined(_WIN32)
typedef HANDLE fs_file;
#define FS_NO_FILE INVALID_HANDLE_VALUE
#define fs_close_file(f) CloseHandle(f)
#else
typedef int fs_file;
#define FS_NO_FILE (-1)
#define fs_close_file(f) ((void)close(f))
#endif

/* `fs/1.read(g, path, offset, buf)`: the bytes of the file from `offset`, at most the extent of
 * `buf`; `bounds` for an offset outside 0 to the size. */
static cint_status fs_call_read(void *user, const cint_value *args, uint32_t nargs, cint_value *result)
{
    cint_ctx *ctx = (cint_ctx *)user;
    uint8_t p[FS_MAX_PATH + 1u];
    size_t n = 0u;
    const fs_grant *g = NULL;
    fs_file f = FS_NO_FILE;
    int64_t size = 0, got = 0, want;
    int st;
    if (nargs != 4u || !fs_handle_ok(&args[0]) || !fs_bytes_ok(&args[1], CINT_VIEW_READ) ||
        args[2].kind != CINT_VAL_SCALAR || args[2].type.code != CINT_TAG_I64 || !fs_bytes_ok(&args[3], CINT_VIEW_WRITE) ||
        (args[3].as.view.shape[0] > 0 && args[3].as.view.byte_stride[0] != 1)) {
        return CINT_REFUSED;
    }
    st = fs_target(ctx, &args[1], false, p, &n, &g);
    if (st == 0) st = fs_open_file(g, p, n, &f, &size);
    if (st == 0) {
        if (args[2].as.i64 < 0 || args[2].as.i64 > size) {
            st = FS_BOUNDS;
        } else {
            want = size - args[2].as.i64 < args[3].as.view.shape[0] ? size - args[2].as.i64 : args[3].as.view.shape[0];
            st = fs_read_into(f, args[2].as.i64, (uint8_t *)args[3].as.view.mem.u.host.ptr, want, &got);
        }
        fs_close_file(f);
    }
    return fs_done(result, st, got);
}

/* `fs/1.stat(g, path)`: the size of the file. */
static cint_status fs_call_stat(void *user, const cint_value *args, uint32_t nargs, cint_value *result)
{
    cint_ctx *ctx = (cint_ctx *)user;
    uint8_t p[FS_MAX_PATH + 1u];
    size_t n = 0u;
    const fs_grant *g = NULL;
    fs_file f = FS_NO_FILE;
    int64_t size = 0;
    int st;
    if (nargs != 2u || !fs_handle_ok(&args[0]) || !fs_bytes_ok(&args[1], CINT_VIEW_READ)) return CINT_REFUSED;
    st = fs_target(ctx, &args[1], false, p, &n, &g);
    if (st == 0) st = fs_open_file(g, p, n, &f, &size);
    if (st == 0) fs_close_file(f);
    return fs_done(result, st, size);
}

/* `fs/1.put(g, path, bytes)`: replaces the file with the bytes, or leaves it as it was. */
static cint_status fs_call_put(void *user, const cint_value *args, uint32_t nargs, cint_value *result)
{
    cint_ctx *ctx = (cint_ctx *)user;
    uint8_t p[FS_MAX_PATH + 1u];
    size_t n = 0u;
    const fs_grant *g = NULL;
    int st;
    if (nargs != 3u || !fs_handle_ok(&args[0]) || !fs_bytes_ok(&args[1], CINT_VIEW_READ) ||
        !fs_bytes_ok(&args[2], CINT_VIEW_READ)) {
        return CINT_REFUSED;
    }
    st = fs_target(ctx, &args[1], true, p, &n, &g);
    if (st == 0) st = fs_put(ctx, g, p, n, &args[2].as.view);
    return fs_done(result, st, 0);
}

/* ---------------------------------------------------------------- the service table (A-10, A-20) */

/* The canonical type signature of each extern of lib/fs.ci without its Grants parameter
 * (SPEC-03 A-20; `ref/cint_ref/sig.py` encodes the same bytes), after the domain string and the
 * definition of `fs.FsError`, which the three share. */
#define FS_SIG_HEAD                                                                                              \
    0x1d, 0x00, 0x00, 0x00, 'c', 'i', 'n', 't', '-', 'c', 'o', 'r', 'e', '-', '1', '/', 't', 'y', 'p', 'e', '-', 's',  \
        'i', 'g', 'n', 'a', 't', 'u', 'r', 'e', '/', 'v', '1', 0x01, 0x00, 0x00, 0x00, 0x73, 0x0a, 0x00, 0x00, 0x00,   \
        'f', 's', '.', 'F', 's', 'E', 'r', 'r', 'o', 'r', 0x22, 0x00, 0x09, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,  \
        'p', 'a', 't', 'h', 0x06, 0x00, 0x00, 0x00, 'd', 'e', 'n', 'i', 'e', 'd', 0x09, 0x00, 0x00, 0x00, 'n', 'o',    \
        't', '_', 'f', 'o', 'u', 'n', 'd', 0x06, 0x00, 0x00, 0x00, 'e', 'x', 'i', 's', 't', 's', 0x0d, 0x00, 0x00,     \
        0x00, 'n', 'o', 't', '_', 'd', 'i', 'r', 'e', 'c', 't', 'o', 'r', 'y', 0x0c, 0x00, 0x00, 0x00, 'i', 's', '_',  \
        'd', 'i', 'r', 'e', 'c', 't', 'o', 'r', 'y', 0x09, 0x00, 0x00, 0x00, 'n', 'o', 't', '_', 'e', 'm', 'p', 't',   \
        'y', 0x06, 0x00, 0x00, 0x00, 'b', 'o', 'u', 'n', 'd', 's', 0x05, 0x00, 0x00, 0x00, 'q', 'u', 'o', 't', 'a',    \
        0x00, 0x00, 0x00, 0x00
#define FS_SIG_U8V 0x52, 0x21, 0x01, 0x00
#define FS_SIG_UNION 0x74, 0x00, 0x00, 0x00, 0x00

static const uint8_t fs_sig_read[] = {FS_SIG_HEAD, 0x03, 0x00, 0x00, 0x00, 0x00, FS_SIG_U8V, 0x00, 0x14, 0x02, FS_SIG_U8V,
                                      FS_SIG_UNION, 0x14};
static const uint8_t fs_sig_stat[] = {FS_SIG_HEAD, 0x01, 0x00, 0x00, 0x00, 0x00, FS_SIG_U8V, FS_SIG_UNION, 0x14};
static const uint8_t fs_sig_put[] = {FS_SIG_HEAD, 0x02, 0x00, 0x00, 0x00, 0x00, FS_SIG_U8V, 0x00, FS_SIG_U8V, FS_SIG_UNION,
                                     0x00};

static const cint_rt_service fs_services[] = {
    {"fs/1.read", 4u, fs_sig_read, sizeof fs_sig_read, fs_call_read},
    {"fs/1.stat", 2u, fs_sig_stat, sizeof fs_sig_stat, fs_call_stat},
    {"fs/1.put", 3u, fs_sig_put, sizeof fs_sig_put, fs_call_put},
};
#define FS_SERVICE_COUNT (sizeof fs_services / sizeof fs_services[0])
