/* cint_console.c: the `console/1` service of the runtime's standard service table (SPEC-03 H-7,
 * H-8a, A-10; SPEC-07 SEC-OS-10, SEC-OS-12, catalog rows `console.input` and `console.raw`;
 * ruling OQ-299; I/O services lane U3; rt/OPEN.md RT-OQ-73).
 *
 * rt/cint_rt.c includes this file after rt/cint_fs.c, so the code is part of the cint_rt
 * library, counted in its own SPEC-09 4.2 row, and the standard service table at the end of
 * this file (cint_rt_service_find) joins the rows of fs/1 with the rows here. The rows are `console/1.raw`, `read`, `write`,
 * `window` and `read_line`, each with the I64 Grants handle first (SEC-OS-1a). `write` hands
 * its bytes to the host's output sink, the `output` function of cint_ctx_config that print
 * statements reach, so a host that captures the program's output captures console writes too
 * and stdout stays the host's (SPEC-06 3.4a); it needs no grant. `read`, `read_line` and
 * `window` need a `console.input` line in the grant set and use the process's standard input
 * and its terminal; `raw` needs `console.raw` and switches the terminal of the standard input
 * to raw mode: no echo, no canonical mode, no signals, no flow control, no CR to NL, no output
 * processing. The terminal is a process-wide resource, so raw mode is kept in one static of the
 * process, held by one context at a time, and restored when that context turns it off, when it
 * is destroyed, when it faults (cint_rt_record_begin), at exit, and on a signal that ends the
 * process while the default disposition held (SIGHUP, SIGINT, SIGQUIT, SIGTERM; the console
 * control handler on Windows). Every failure of an operation is a value of lib/console.ci's
 * ConsoleError, in tag order denied 1, closed 2, unavailable 3; a contract violation (a handle
 * other than 0, a view of another type or stride) is CINT_REFUSED, the boundary fault
 * host.error.<name> (A-6). The Windows path is written to the same rules and compiled on the
 * MSVC leg; it has not run here.
 */

#include <signal.h>
#if !defined(_WIN32)
#include <limits.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#endif

#define CON_DENIED 1
#define CON_CLOSED 2
#define CON_UNAVAILABLE 3
#define CON_CHUNK 16384u

/* The one raw terminal of the process (see the header comment). */
typedef struct con_raw_state {
    volatile sig_atomic_t on;    /* raw mode set by this runtime and not yet restored */
    int hooks;                   /* the exit hook is registered */
    cint_ctx *owner;             /* the context that holds raw mode */
#if defined(_WIN32)
    HANDLE in, out;
    DWORD in_mode, out_mode;
    int out_saved;
#else
    struct termios saved;
    struct sigaction old[4];
    int hooked[4];
#endif
} con_raw_state;

static con_raw_state con_raw;

static bool con_handle_ok(const cint_value *v)
{
    return v->kind == CINT_VAL_SCALAR && v->type.code == CINT_TAG_I64 && v->as.i64 == 0;
}
static bool con_bytes_ok(const cint_value *v, uint32_t perm)
{
    return v->kind == CINT_VAL_VIEW && v->type.code == CINT_TAG_U8 && v->as.view.perm == perm &&
           (v->as.view.shape[0] <= 0 || v->as.view.byte_stride[0] == 1);
}
static cint_status con_done(cint_value *r, int st, int64_t value)
{
    if (st != 0) {
        r->is_error = 1u;
        r->error = st;
        return CINT_OK;
    }
    r->as.i64 = value;
    return CINT_OK;
}
static bool con_input_granted(const cint_ctx *ctx)
{
    return (cint_rt_grant_caps(ctx) & CINT_RT_CAP_CONSOLE_INPUT) != 0u;
}

/* ------------------------------------------------------------------------ restoring raw mode */

/* Restores the terminal when raw mode is on. Called from the exit hook and from the signal
 * handler, so it calls only functions that are safe there. */
/* The C library's stdout buffer, flushed before a wait for input and after a write. A host that
 * runs the program as a child of `cint run` or of an editor reads its stdout through a pipe,
 * which the C library buffers in full, so without this a prompt or a redrawn screen reached the
 * terminal only at exit (RT-OQ-77). Print statements stay buffered between waits. A host whose
 * output sink is not stdout loses nothing: the flush moves no byte of its own. */
static void con_flush_out(void)
{
    (void)fflush(stdout);
}

static void con_restore_now(void)
{
    if (!con_raw.on) return;
#if defined(_WIN32)
    (void)SetConsoleMode(con_raw.in, con_raw.in_mode);
    if (con_raw.out_saved) (void)SetConsoleMode(con_raw.out, con_raw.out_mode);
#else
    (void)tcsetattr(STDIN_FILENO, TCSANOW, &con_raw.saved);
#endif
    con_raw.on = 0;
}

static void con_at_exit(void)
{
    con_restore_now();
}

#if defined(_WIN32)
static BOOL WINAPI con_ctrl(DWORD type)
{
    (void)type;
    con_restore_now();
    return FALSE;   /* the next handler, then the process ends */
}
#else
static const int con_signals[4] = {SIGHUP, SIGINT, SIGQUIT, SIGTERM};

static void con_signal(int sig)
{
    con_restore_now();
    (void)signal(sig, SIG_DFL);
    (void)raise(sig);
}

/* Hooks the signals that end the process while their default disposition holds; a host that
 * handles one itself keeps its handler and restores the terminal through cint_ctx_destroy. */
static void con_hook_signals(void)
{
    for (size_t i = 0u; i < 4u; i++) {
        struct sigaction sa;
        con_raw.hooked[i] = 0;
        if (sigaction(con_signals[i], NULL, &con_raw.old[i]) != 0) continue;
        if ((con_raw.old[i].sa_flags & SA_SIGINFO) != 0 || con_raw.old[i].sa_handler != SIG_DFL) continue;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = con_signal;
        sigemptyset(&sa.sa_mask);
        con_raw.hooked[i] = sigaction(con_signals[i], &sa, NULL) == 0;
    }
}

static void con_unhook_signals(void)
{
    for (size_t i = 0u; i < 4u; i++) {
        if (con_raw.hooked[i]) (void)sigaction(con_signals[i], &con_raw.old[i], NULL);
        con_raw.hooked[i] = 0;
    }
}
#endif

/* Leaves raw mode when `ctx` holds it: the ordinary exit of `raw(false)`, cint_ctx_destroy and
 * a fault. */
static void con_leave(cint_ctx *ctx)
{
    if (!con_raw.on || con_raw.owner != ctx) return;
    con_restore_now();
#if !defined(_WIN32)
    con_unhook_signals();
#endif
    con_raw.owner = NULL;
}

void cint_rt_console_release(cint_ctx *ctx)
{
    con_leave(ctx);
}

void cint_rt_console_fault(cint_ctx *ctx)
{
    con_leave(ctx);
}

/* `raw(true)` for `ctx`: `unavailable` when the standard input is not a terminal or another
 * context holds raw mode; nothing when `ctx` holds it already. */
static int con_enter(cint_ctx *ctx)
{
#if defined(_WIN32)
    DWORD mode = 0u;
#else
    struct termios t;
#endif
    if (con_raw.on) return con_raw.owner == ctx ? 0 : CON_UNAVAILABLE;
#if defined(_WIN32)
    con_raw.in = GetStdHandle(STD_INPUT_HANDLE);
    con_raw.out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (con_raw.in == INVALID_HANDLE_VALUE || !GetConsoleMode(con_raw.in, &con_raw.in_mode)) return CON_UNAVAILABLE;
    con_raw.out_saved = con_raw.out != INVALID_HANDLE_VALUE && GetConsoleMode(con_raw.out, &con_raw.out_mode);
    mode = con_raw.in_mode & ~(DWORD)(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
    mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
    if (!SetConsoleMode(con_raw.in, mode)) return CON_UNAVAILABLE;
    if (con_raw.out_saved) {
        mode = con_raw.out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN;
        if (!SetConsoleMode(con_raw.out, mode)) {
            (void)SetConsoleMode(con_raw.in, con_raw.in_mode);
            return CON_UNAVAILABLE;
        }
    }
    if (!con_raw.hooks) {
        con_raw.hooks = 1;
        (void)atexit(con_at_exit);
        (void)SetConsoleCtrlHandler(con_ctrl, TRUE);
    }
#else
    if (tcgetattr(STDIN_FILENO, &con_raw.saved) != 0) return CON_UNAVAILABLE;
    t = con_raw.saved;
    t.c_iflag &= (tcflag_t) ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    t.c_oflag &= (tcflag_t)~OPOST;
    t.c_lflag &= (tcflag_t) ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    t.c_cflag &= (tcflag_t) ~(CSIZE | PARENB);
    t.c_cflag |= CS8;
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &t) != 0) return CON_UNAVAILABLE;
    if (!con_raw.hooks) {
        con_raw.hooks = 1;
        (void)atexit(con_at_exit);
    }
    con_hook_signals();
#endif
    con_raw.owner = ctx;
    con_raw.on = 1;
    return 0;
}

/* ------------------------------------------------------------------------------- reading */

/* Whether input is ready within `wait_ms` (negative: no limit): 1 ready, 0 timed out, -1 the
 * input has gone away. */
#if defined(_WIN32)
static int con_wait(int64_t wait_ms)
{
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    ULONGLONG start = GetTickCount64();
    DWORD kind;
    if (in == INVALID_HANDLE_VALUE || in == NULL) return -1;
    kind = GetFileType(in);
    if (kind == FILE_TYPE_PIPE) {
        for (;;) {
            DWORD avail = 0u;
            if (!PeekNamedPipe(in, NULL, 0u, NULL, &avail, NULL)) return -1;
            if (avail > 0u) return 1;
            if (wait_ms >= 0 && (int64_t)(GetTickCount64() - start) >= wait_ms) return 0;
            Sleep(1u);
        }
    }
    if (kind != FILE_TYPE_CHAR) return 1;   /* a file: the read decides */
    for (;;) {
        DWORD span = INFINITE, r;
        if (wait_ms >= 0) {
            int64_t left = wait_ms - (int64_t)(GetTickCount64() - start);
            if (left <= 0) return 0;
            span = left > 0x7fffffff ? 0x7fffffffu : (DWORD)left;
        }
        r = WaitForSingleObject(in, span);
        if (r == WAIT_OBJECT_0) return 1;
        if (r != WAIT_TIMEOUT) return -1;
        if (wait_ms >= 0) return 0;
    }
}

/* Bytes of the standard input, at most n: the count, 0 at its end, -1 when it has gone away. */
static int64_t con_read_some(uint8_t *dst, size_t n)
{
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD got = 0u;
    if (in == INVALID_HANDLE_VALUE || in == NULL) return -1;
    if (!ReadFile(in, dst, n > 0x7fffffffu ? 0x7fffffffu : (DWORD)n, &got, NULL)) {
        return GetLastError() == ERROR_BROKEN_PIPE ? 0 : -1;
    }
    return (int64_t)got;
}
#else
static int64_t con_now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int con_wait(int64_t wait_ms)
{
    int64_t start = con_now_ms();
    for (;;) {
        struct pollfd p;
        int64_t left = wait_ms < 0 ? -1 : wait_ms - (con_now_ms() - start);
        int r;
        p.fd = STDIN_FILENO;
        p.events = POLLIN;
        p.revents = 0;
        if (wait_ms >= 0 && left < 0) left = 0;
        r = poll(&p, 1u, left > INT_MAX ? INT_MAX : (int)left);
        if (r > 0) return (p.revents & POLLNVAL) != 0 ? -1 : 1;
        if (r == 0) {
            if (wait_ms >= 0 && con_now_ms() - start >= wait_ms) return 0;
            continue;   /* a round of INT_MAX ms passed, or poll woke early */
        }
        if (errno != EINTR) return -1;
    }
}

static int64_t con_read_some(uint8_t *dst, size_t n)
{
    for (;;) {
        ssize_t got = read(STDIN_FILENO, dst, n);
        if (got >= 0) return (int64_t)got;
        if (errno == EINTR) continue;
        return errno == EAGAIN ? 0 : -1;
    }
}
#endif

/* -------------------------------------------------------------------------- the operations */

/* `console/1.raw(g, on)` */
static cint_status con_call_raw(void *user, const cint_value *args, uint32_t nargs, cint_value *result)
{
    cint_ctx *ctx = (cint_ctx *)user;
    int st = 0;
    if (nargs != 2u || !con_handle_ok(&args[0]) || args[1].kind != CINT_VAL_SCALAR || args[1].type.code != CINT_TAG_BOOL) {
        return CINT_REFUSED;
    }
    if ((cint_rt_grant_caps(ctx) & CINT_RT_CAP_CONSOLE_RAW) == 0u) return con_done(result, CON_DENIED, 0);
    if (args[1].as.i64 != 0) st = con_enter(ctx);
    else con_leave(ctx);
    return con_done(result, st, 0);
}

/* `console/1.read(g, buf, wait_ms)`: the bytes that arrived within `wait_ms`, at most the
 * extent of `buf`; 0 on a timeout; `closed` at the end of the input. */
static cint_status con_call_read(void *user, const cint_value *args, uint32_t nargs, cint_value *result)
{
    cint_ctx *ctx = (cint_ctx *)user;
    int64_t n, got = 0;
    int ready;
    if (nargs != 3u || !con_handle_ok(&args[0]) || !con_bytes_ok(&args[1], CINT_VIEW_WRITE) ||
        args[2].kind != CINT_VAL_SCALAR || args[2].type.code != CINT_TAG_I64) {
        return CINT_REFUSED;
    }
    if (!con_input_granted(ctx)) return con_done(result, CON_DENIED, 0);
    n = args[1].as.view.shape[0];
    if (n <= 0) return con_done(result, 0, 0);
    con_flush_out();
    ready = con_wait(args[2].as.i64);
    if (ready < 0) return con_done(result, CON_CLOSED, 0);
    if (ready == 0) return con_done(result, 0, 0);
    got = con_read_some((uint8_t *)args[1].as.view.mem.u.host.ptr, (size_t)n);
    if (got <= 0) return con_done(result, CON_CLOSED, 0);
    return con_done(result, 0, got);
}

/* `console/1.write(g, bytes)`: every byte to the host's output sink, or `closed`;
 * `unavailable` when the host gave none. */
static cint_status con_call_write(void *user, const cint_value *args, uint32_t nargs, cint_value *result)
{
    cint_ctx *ctx = (cint_ctx *)user;
    const cint_arg_view *v;
    int64_t n;
    cint_status st = CINT_OK;
    if (nargs != 2u || !con_handle_ok(&args[0]) || !con_bytes_ok(&args[1], CINT_VIEW_READ)) return CINT_REFUSED;
    v = &args[1].as.view;
    n = v->shape[0];
    if (ctx->output == NULL) return con_done(result, CON_UNAVAILABLE, 0);
    if (n > 0) st = ctx->output(ctx->output_user, (const uint8_t *)v->mem.u.host.ptr, (size_t)n);
    con_flush_out();
    return con_done(result, st == CINT_OK ? 0 : CON_CLOSED, 0);
}

/* `console/1.window(g)`: rows in the high 32 bits, columns in the low 32 bits, read fresh;
 * `unavailable` when no standard stream is a terminal. */
static cint_status con_call_window(void *user, const cint_value *args, uint32_t nargs, cint_value *result)
{
    cint_ctx *ctx = (cint_ctx *)user;
    int64_t rows = 0, cols = 0;
    if (nargs != 1u || !con_handle_ok(&args[0])) return CINT_REFUSED;
    if (!con_input_granted(ctx)) return con_done(result, CON_DENIED, 0);
#if defined(_WIN32)
    {
        static const DWORD streams[2] = {STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
        for (size_t i = 0u; i < 2u && rows == 0; i++) {
            CONSOLE_SCREEN_BUFFER_INFO info;
            HANDLE h = GetStdHandle(streams[i]);
            if (h == INVALID_HANDLE_VALUE || h == NULL || !GetConsoleScreenBufferInfo(h, &info)) continue;
            rows = (int64_t)info.srWindow.Bottom - info.srWindow.Top + 1;
            cols = (int64_t)info.srWindow.Right - info.srWindow.Left + 1;
        }
    }
#else
    {
        static const int fds[3] = {STDOUT_FILENO, STDIN_FILENO, STDERR_FILENO};
        for (size_t i = 0u; i < 3u && rows == 0; i++) {
            struct winsize ws;
            memset(&ws, 0, sizeof ws);
            if (ioctl(fds[i], TIOCGWINSZ, &ws) != 0) continue;
            rows = ws.ws_row;
            cols = ws.ws_col;
        }
    }
#endif
    if (rows <= 0 || cols <= 0) return con_done(result, CON_UNAVAILABLE, 0);
    return con_done(result, 0, (rows << 32) | cols);
}

/* `console/1.read_line(g, buf)`: one line without its line end ('\n', and a '\r' before it),
 * at most the extent of `buf`, the rest of a longer line left for the next call; the bytes
 * before the end of the input, then `closed`. */
static cint_status con_call_read_line(void *user, const cint_value *args, uint32_t nargs, cint_value *result)
{
    cint_ctx *ctx = (cint_ctx *)user;
    uint8_t *dst;
    int64_t n, count = 0;
    if (nargs != 2u || !con_handle_ok(&args[0]) || !con_bytes_ok(&args[1], CINT_VIEW_WRITE)) return CINT_REFUSED;
    if (!con_input_granted(ctx)) return con_done(result, CON_DENIED, 0);
    n = args[1].as.view.shape[0];
    dst = (uint8_t *)args[1].as.view.mem.u.host.ptr;
    con_flush_out();
    while (count < n) {
        uint8_t c;
        int64_t got = con_read_some(&c, 1u);
        if (got < 0) return con_done(result, CON_CLOSED, 0);
        if (got == 0) return count > 0 ? con_done(result, 0, count) : con_done(result, CON_CLOSED, 0);
        if (c == '\n') {
            if (count > 0 && dst[count - 1] == '\r') count--;
            break;
        }
        dst[count++] = c;
    }
    return con_done(result, 0, count);
}

/* ------------------------------------------------------------------- the rows (A-10, A-20) */

/* The canonical type signature of each extern of lib/console.ci without its Grants parameter
 * (SPEC-03 A-20; `ref/cint_ref/sig.py` encodes the same bytes), after the domain string and
 * the definition of `console.ConsoleError`, which the five share. */
#define CON_SIG_HEAD                                                                                             \
    0x1d, 0x00, 0x00, 0x00, 'c', 'i', 'n', 't', '-', 'c', 'o', 'r', 'e', '-', '1', '/', 't', 'y', 'p', 'e', '-', 's',  \
        'i', 'g', 'n', 'a', 't', 'u', 'r', 'e', '/', 'v', '1', 0x01, 0x00, 0x00, 0x00, 0x73, 0x14, 0x00, 0x00, 0x00,   \
        'c', 'o', 'n', 's', 'o', 'l', 'e', '.', 'C', 'o', 'n', 's', 'o', 'l', 'e', 'E', 'r', 'r', 'o', 'r', 0x22,      \
        0x00, 0x03, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 'd', 'e', 'n', 'i', 'e', 'd', 0x06, 0x00, 0x00, 0x00,    \
        'c', 'l', 'o', 's', 'e', 'd', 0x0b, 0x00, 0x00, 0x00, 'u', 'n', 'a', 'v', 'a', 'i', 'l', 'a', 'b', 'l', 'e',   \
        0x00, 0x00, 0x00, 0x00
#define CON_SIG_U8V 0x52, 0x21, 0x01, 0x00
#define CON_SIG_UNION 0x74, 0x00, 0x00, 0x00, 0x00

static const uint8_t con_sig_raw[] = {CON_SIG_HEAD, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, CON_SIG_UNION, 0x00};
static const uint8_t con_sig_read[] = {CON_SIG_HEAD, 0x02, 0x00, 0x00, 0x00, 0x02, CON_SIG_U8V, 0x00, 0x14,
                                       CON_SIG_UNION, 0x14};
static const uint8_t con_sig_write[] = {CON_SIG_HEAD, 0x01, 0x00, 0x00, 0x00, 0x00, CON_SIG_U8V, CON_SIG_UNION, 0x00};
static const uint8_t con_sig_window[] = {CON_SIG_HEAD, 0x00, 0x00, 0x00, 0x00, CON_SIG_UNION, 0x14};
static const uint8_t con_sig_read_line[] = {CON_SIG_HEAD, 0x01, 0x00, 0x00, 0x00, 0x02, CON_SIG_U8V, CON_SIG_UNION, 0x14};

static const cint_rt_service con_services[] = {
    {"console/1.raw", 2u, con_sig_raw, sizeof con_sig_raw, con_call_raw},
    {"console/1.read", 3u, con_sig_read, sizeof con_sig_read, con_call_read},
    {"console/1.write", 2u, con_sig_write, sizeof con_sig_write, con_call_write},
    {"console/1.window", 1u, con_sig_window, sizeof con_sig_window, con_call_window},
    {"console/1.read_line", 2u, con_sig_read_line, sizeof con_sig_read_line, con_call_read_line},
};
#define CON_SERVICE_COUNT (sizeof con_services / sizeof con_services[0])

/* The standard service table: the rows of fs/1 (rt/cint_fs.c) and of console/1. */
static const struct {
    const cint_rt_service *rows;
    size_t count;
} rt_services[] = {{fs_services, FS_SERVICE_COUNT}, {con_services, CON_SERVICE_COUNT}};
#define RT_SERVICE_COUNT (FS_SERVICE_COUNT + CON_SERVICE_COUNT)

/* The row's function of the context's service table: a row whose name, class (`observe`),
 * parameter count and signature bytes are a service's; NULL for any other, which refuses
 * creation as a missing host function does (A-10). The functions are the context's own, so
 * that each call reaches its grant set. */
const cint_host_fn *cint_rt_service_find(cint_ctx *ctx, const cint_host_import *r)
{
    size_t at = 0u;
    for (size_t t = 0u; t < sizeof rt_services / sizeof rt_services[0]; at += rt_services[t].count, t++) {
        for (size_t i = 0u; i < rt_services[t].count; i++) {
            const cint_rt_service *s = &rt_services[t].rows[i];
            cint_host_fn *f;
            if (strlen(s->name) != r->name.len || memcmp(s->name, r->name.bytes, r->name.len) != 0) continue;
            if (r->effect != CINT_OBSERVE || r->param_count != s->param_count || r->type_signature_len != s->signature_len ||
                memcmp(r->type_signature, s->signature, s->signature_len) != 0) {
                return NULL;
            }
            if (ctx->service_fns == NULL) {
                ctx->service_fns =
                    (cint_host_fn *)fs_alloc(ctx, RT_SERVICE_COUNT * sizeof(cint_host_fn), _Alignof(cint_host_fn));
                if (ctx->service_fns == NULL) return NULL;
                memset(ctx->service_fns, 0, RT_SERVICE_COUNT * sizeof(cint_host_fn));
            }
            f = &ctx->service_fns[at + i];
            if (f->call == NULL) {
                f->name = s->name;
                f->effect = CINT_OBSERVE;
                cint_sha256(s->signature, s->signature_len, f->signature);
                f->call = s->call;
                f->user = ctx;
            }
            return f;
        }
    }
    return NULL;
}

void cint_rt_service_release(cint_ctx *ctx)
{
    cint_rt_console_release(ctx);
    fs_release(ctx, ctx->service_fns, RT_SERVICE_COUNT * sizeof(cint_host_fn), _Alignof(cint_host_fn));
    ctx->service_fns = NULL;
    cint_rt_fs_close(ctx);
}
