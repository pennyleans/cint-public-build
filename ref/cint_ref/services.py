"""The standard service table of cint_ref (SPEC-03 H-7, H-8a, A-10; SPEC-07 SEC-CAP-6, SEC-OS-5 to
SEC-OS-8, SEC-OS-10, SEC-OS-12; ref/OPEN.md REF-OQ-48): the grant lines a run is given, the `fs/1`
service, as rt/cint_fs.c builds it (rt/OPEN.md RT-OQ-72), so that the oracle and the runtime
give the same status to the same call over the same host directory, and the `console/1` service,
as rt/cint_console.c builds it (RT-OQ-73).

A grant is the canonical capability text of SEC-CAP-6 and, for `fs.read` and `fs.write`, `=`
and the host directory the logical root maps to (SEC-OS-6; the `--grant` form of ruling
OQ-300). `fs.read` covers `read` and `stat`, `fs.write` covers `put`. Every failure is a
value of lib/fs.ci's `FsError` (`path`, `denied`, `not_found`, `exists`, `not_directory`,
`is_directory`, `not_empty`, `bounds`, `quota`), never a fault (SEC-OS-12): syntax before
permission (SEC-OS-5); the first covering grant of the right kind, in the order given, maps
the path; names are looked up exactly in the host directory's listing and links are never
followed, so another case or a link is `not_found` (SEC-OS-5a, SEC-OS-7); a put over a
directory is `is_directory`, over a link or a name that differs only in ASCII case `exists`,
and the bytes go to a temporary entry beside the target and replace it in one rename
(SEC-OS-8). A host error with no status of its own is `HostError`, which the runtime reports
as the boundary fault `host.error.<name>` and cint_ref as a refusal.

`console/1`: `write` appends its bytes to the run's output, where print statements write (the
runtime hands them to the host's output sink), and needs no grant; `read`, `read_line` and
`window` (under `console.input`) use the process's standard input and its terminal; `raw`
(under `console.raw`) switches that terminal to raw mode and the run restores it when the
program ends, however it ends (ruling OQ-299). Each failure is a value of lib/console.ci's
`ConsoleError`: `denied`, `closed` at the end of the input, `unavailable` where there is no
terminal, no raw mode, or no window.
"""
from __future__ import annotations

import atexit
import errno
import os
import select
import signal
import stat
import sys

__all__ = ["FS_VALUES", "CONSOLE_VALUES", "ServiceStatus", "FsStatus", "ConsoleStatus", "HostError", "parse_grants",
           "fs_call", "console_call", "console_restore"]

FS_VALUES = ("path", "denied", "not_found", "exists", "not_directory", "is_directory", "not_empty", "bounds", "quota")
CONSOLE_VALUES = ("denied", "closed", "unavailable")
SEGMENT = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.")
RESERVED = {"CON", "PRN", "AUX", "NUL"} | {"COM%d" % i for i in range(1, 10)} | {"LPT%d" % i for i in range(1, 10)}
MAX_PATH = 4095       # the logical path this implementation takes (rt/cint_fs.c FS_MAX_PATH)
PATH_CAPS = ("fs.read", "fs.write")


class ServiceStatus(Exception):
    """A failed operation: `name` is the value of the service's error set."""

    def __init__(self, name):
        super().__init__(name)
        self.name = name


class FsStatus(ServiceStatus):
    """A failed fs/1 operation: `name` is the FsError value."""


class ConsoleStatus(ServiceStatus):
    """A failed console/1 operation: `name` is the ConsoleError value."""


class HostError(Exception):
    """A host error that no FsError value names (SPEC-03 A-6)."""


def path_ok(p: str) -> bool:
    """The logical path syntax of SEC-OS-5."""
    if not p or len(p) > MAX_PATH or p[0] != "/":
        return False
    if p == "/":
        return True
    for seg in p[1:].split("/"):
        if seg == "" or seg[-1] == "." or any(c not in SEGMENT for c in seg):
            return False
        if seg.split(".", 1)[0].upper() in RESERVED:
            return False
    return True


def covers(root: str, path: str) -> bool:
    """Whether `path` is the grant root or a descendant on a `/` boundary (SEC-OS-5)."""
    return root == "/" or path == root or path.startswith(root + "/")


def parse_grants(texts):
    """The grant lines of a run: the canonical texts, for `covers`, and the fs grants
    [(write, root, host directory)] in order. ValueError for a line the runtime refuses: a
    capability name outside the segment character set or its length, a path capability
    without a host directory or with a root outside SEC-OS-5, or a host directory on another
    capability (rt/cint_fs.c `fs_line`)."""
    canonical, fs = [], []
    for line in texts:
        if "\n" in line or "\0" in line:
            raise ValueError("a grant is one line without a zero byte: %r" % line)
        text, _, host = line.partition("=")
        name, _, args = text.partition(":")
        if not 1 <= len(name) <= 63 or any(c not in SEGMENT for c in name):
            raise ValueError("a capability name is 1 to 63 segment characters: %r" % line)
        if name in PATH_CAPS:
            if ":" not in text or not path_ok(args) or host == "":
                raise ValueError("a path capability names a logical root and the host directory it maps to, "
                                 "`%s:/root=dir`: %r" % (name, line))
            fs.append((name == "fs.write", args, host))
        elif "=" in line:
            raise ValueError("a host directory maps a path root only: %r" % line)
        canonical.append(text)
    return canonical, fs


def _host(e: OSError) -> FsStatus | HostError:
    n = e.errno
    if n in (errno.ENOENT, errno.ELOOP, errno.ENXIO):
        return FsStatus("not_found")
    if n == errno.ENOTDIR:
        return FsStatus("not_directory")
    if n == errno.EISDIR:
        return FsStatus("is_directory")
    if n in (errno.EACCES, errno.EPERM, errno.EROFS):
        return FsStatus("denied")
    if n in (errno.EEXIST, errno.ENOTEMPTY):
        return FsStatus("exists")
    if n in (errno.ENOSPC, errno.EFBIG, getattr(errno, "EDQUOT", -1)):
        return FsStatus("quota")
    if n == errno.ENAMETOOLONG:
        return FsStatus("path")
    return HostError(str(e))


def _is_link(st) -> bool:
    return stat.S_ISLNK(st.st_mode) or (getattr(st, "st_file_attributes", 0) & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)) != 0


def _listing(d: str) -> list:
    try:
        return os.listdir(d)
    except OSError as e:
        raise _host(e) from None


def _lstat(p: str):
    try:
        return os.lstat(p)
    except OSError as e:
        raise _host(e) from None


def _walk(host: str, segments: list) -> str:
    """The host directory of the last segment: each directory before it must be an entry of
    that exact spelling, not a link, and a directory."""
    d = host
    for seg in segments[:-1]:
        if seg not in _listing(d):
            raise FsStatus("not_found")
        p = os.path.join(d, seg)
        st = _lstat(p)
        if _is_link(st):
            raise FsStatus("not_found")
        if not stat.S_ISDIR(st.st_mode):
            raise FsStatus("not_directory")
        d = p
    return d


def _segments(root: str, path: str) -> list:
    rel = path[1:] if root == "/" else path[len(root) + 1:]
    return rel.split("/") if rel else []


def _target(grants, write: bool, path: str):
    """The grant and the segments below its root: `path` for bad syntax, `denied` when no
    grant of the kind covers the path (syntax before permission)."""
    if not path_ok(path):
        raise FsStatus("path")
    for w, root, host in grants.fs:
        if w == write and covers(root, path):
            return host, _segments(root, path)
    raise FsStatus("denied")


def _open_file(host: str, segments: list):
    """The host path and size of the regular file the segments name."""
    if not segments:
        raise FsStatus("is_directory")
    d = _walk(host, segments)
    name = segments[-1]
    if name not in _listing(d):
        raise FsStatus("not_found")
    p = os.path.join(d, name)
    st = _lstat(p)
    if _is_link(st):
        raise FsStatus("not_found")
    if stat.S_ISDIR(st.st_mode):
        raise FsStatus("is_directory")
    if not stat.S_ISREG(st.st_mode):
        raise FsStatus("not_found")
    return p, st.st_size


def fs_read(grants, path: str, offset: int, extent: int) -> bytes:
    """`fs/1.read`: at most `extent` bytes from `offset`; `bounds` outside 0 to the size."""
    host, segments = _target(grants, False, path)
    p, size = _open_file(host, segments)
    if not 0 <= offset <= size:
        raise FsStatus("bounds")
    try:
        with open(p, "rb") as f:
            f.seek(offset)
            return f.read(min(extent, size - offset))
    except OSError as e:
        raise _host(e) from None


def fs_stat(grants, path: str) -> int:
    """`fs/1.stat`: the size of the file."""
    host, segments = _target(grants, False, path)
    return _open_file(host, segments)[1]


def fs_put(grants, path: str, data: bytes) -> None:
    """`fs/1.put`: replaces the file with the bytes in one rename, or leaves it as it was."""
    host, segments = _target(grants, True, path)
    if not segments:
        raise FsStatus("is_directory")
    d = _walk(host, segments)
    name = segments[-1]
    entries = _listing(d)
    if name in entries:
        st = _lstat(os.path.join(d, name))
        if stat.S_ISDIR(st.st_mode):
            raise FsStatus("is_directory")
        if _is_link(st) or not stat.S_ISREG(st.st_mode):
            raise FsStatus("exists")
    elif any(e.lower() == name.lower() and e.isascii() for e in entries):
        raise FsStatus("exists")
    fd = -1
    tmp = None
    try:
        for serial in range(1, 9):
            tmp = os.path.join(d, ".cint-put-%d" % serial)
            try:
                fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_BINARY", 0), 0o666)
                break
            except FileExistsError:
                continue
        if fd < 0:
            raise FsStatus("exists")
        view = memoryview(data)
        while len(view) > 0:
            n = os.write(fd, view)
            view = view[n:]
        os.fsync(fd)
        os.close(fd)
        fd = -1
        os.replace(tmp, os.path.join(d, name))
        tmp = None
    except OSError as e:
        raise _host(e) from None
    finally:
        if fd >= 0:
            os.close(fd)
        if tmp is not None:
            try:
                os.unlink(tmp)
            except OSError:
                pass


def fs_call(name: str, grants, args: list):
    """`fs/1.<name>` over the evaluated arguments after the grant set: the value, or FsStatus."""
    def text(v) -> str:
        return bytes(v.values()).decode("ascii", "replace")
    if name == "read":
        path, offset, buf = args
        data = fs_read(grants, text(path), offset, len(buf.offsets()))
        for off, b in zip(buf.offsets(), data):
            buf.buf[off] = b
        return len(data)
    if name == "stat":
        return fs_stat(grants, text(args[0]))
    if name == "put":
        fs_put(grants, text(args[0]), bytes(args[1].values()))
        return None
    raise HostError("no operation `%s` of fs/1" % name)


# -- console/1 (rt/cint_console.c; RT-OQ-73) -------------------------------------------------------

_RAW = {"saved": None, "hooked": False}   # the terminal attributes before raw mode, while it is on


def _stdin_fd() -> int:
    try:
        return sys.stdin.fileno()
    except (AttributeError, ValueError, OSError):
        raise ConsoleStatus("closed") from None


def console_restore() -> None:
    """Leaves raw mode when it is on: the end of the run, `raw(false)`, exit, a signal."""
    saved = _RAW["saved"]
    if saved is None:
        return
    _RAW["saved"] = None
    try:
        import termios
        termios.tcsetattr(sys.stdin.fileno(), termios.TCSANOW, saved)
    except Exception:
        pass


def _on_signal(sig, frame):
    console_restore()
    signal.signal(sig, signal.SIG_DFL)
    os.kill(os.getpid(), sig)


def console_raw(on: bool) -> None:
    """`console/1.raw`: no echo, no canonical mode, no signals, no flow control, no CR to NL, no
    output processing, on the terminal of the standard input; `unavailable` without one."""
    if not on:
        console_restore()
        return
    if _RAW["saved"] is not None:
        return
    try:
        import termios
    except ImportError:
        raise ConsoleStatus("unavailable") from None
    fd = _stdin_fd()
    try:
        saved = termios.tcgetattr(fd)
    except (termios.error, OSError):
        raise ConsoleStatus("unavailable") from None
    t = list(saved)
    t[0] &= ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK | termios.ISTRIP | termios.INLCR | termios.IGNCR
              | termios.ICRNL | termios.IXON)
    t[1] &= ~termios.OPOST
    t[2] &= ~(termios.CSIZE | termios.PARENB)
    t[2] |= termios.CS8
    t[3] &= ~(termios.ECHO | termios.ECHONL | termios.ICANON | termios.ISIG | termios.IEXTEN)
    t[6] = list(t[6])
    t[6][termios.VMIN] = 1
    t[6][termios.VTIME] = 0
    try:
        termios.tcsetattr(fd, termios.TCSANOW, t)
    except (termios.error, OSError):
        raise ConsoleStatus("unavailable") from None
    _RAW["saved"] = saved
    if not _RAW["hooked"]:
        _RAW["hooked"] = True
        atexit.register(console_restore)
        for name in ("SIGHUP", "SIGTERM", "SIGQUIT"):
            sig = getattr(signal, name, None)
            if sig is not None and signal.getsignal(sig) == signal.SIG_DFL:
                signal.signal(sig, _on_signal)


def _read_some(fd: int, n: int) -> bytes:
    """At most n bytes of the standard input; `closed` at its end or when it has gone away."""
    try:
        data = os.read(fd, n)
    except OSError:
        raise ConsoleStatus("closed") from None
    if data == b"":
        raise ConsoleStatus("closed")
    return data


def console_read(extent: int, wait_ms: int) -> bytes:
    """`console/1.read`: the bytes that arrived within `wait_ms` (negative: no limit), at most
    `extent`; empty on a timeout."""
    if extent <= 0:
        return b""
    fd = _stdin_fd()
    if wait_ms >= 0:
        try:
            poller = select.poll()
            poller.register(fd, select.POLLIN)
            ready = poller.poll(min(wait_ms, 0x7fffffff))
        except (AttributeError, OSError, ValueError):
            ready = [fd]   # a platform without poll (Windows): the read decides
        if not ready:
            return b""
    return _read_some(fd, extent)


def console_read_line(extent: int) -> bytes:
    """`console/1.read_line`: one line without its end ('\n', and a '\r' before it), at most
    `extent` bytes, the rest of a longer line left for the next call."""
    fd = _stdin_fd()
    line = bytearray()
    while len(line) < extent:
        try:
            c = _read_some(fd, 1)
        except ConsoleStatus:
            if line:
                break
            raise
        if c == b"\n":
            if line[-1:] == b"\r":
                del line[-1]
            break
        line += c
    return bytes(line)


def console_window() -> int:
    """`console/1.window`: rows in the high 32 bits, columns in the low 32 bits, of the first
    standard stream that is a terminal; `unavailable` when none is."""
    for fd in (1, 0, 2):
        try:
            size = os.get_terminal_size(fd)
        except (OSError, ValueError):
            continue
        if size.lines > 0 and size.columns > 0:
            return (size.lines << 32) | size.columns
    raise ConsoleStatus("unavailable")


def console_call(name: str, args: list, out: bytearray):
    """`console/1.<name>` over the evaluated arguments after the grant set, `out` the run's
    output: the value, or ConsoleStatus."""
    if name == "raw":
        console_raw(bool(args[0]))
        return None
    if name == "read":
        buf, wait_ms = args
        data = console_read(len(buf.offsets()), wait_ms)
        for off, b in zip(buf.offsets(), data):
            buf.buf[off] = b
        return len(data)
    if name == "write":
        out += bytes(args[0].values())
        return None
    if name == "window":
        return console_window()
    if name == "read_line":
        buf = args[0]
        data = console_read_line(len(buf.offsets()))
        for off, b in zip(buf.offsets(), data):
            buf.buf[off] = b
        return len(data)
    raise HostError("no operation `%s` of console/1" % name)
