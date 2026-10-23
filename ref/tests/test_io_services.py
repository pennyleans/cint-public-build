import os, sys; sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # put ref/ on the path
"""Tests for the I/O services lane, unit U1 (docs/design/notes/2026-10-21-io-services.md; SPEC-04I.15
section 6.9; ref/OPEN.md REF-OQ-48): `extern` declarations with their effect classes, the
`@service` attribute, `requires`, the `Grants` type, `main(Grants g)`, the standard service
modules under lib/, the program-wide checks C3040 to C3043, the `denied` result of a service
call without a grant, the refusal of a program with a host import, the capability manifest and
the computed effect classes."""
import hashlib
import subprocess
import tempfile
import unittest

from cint_ref import parser as P
from cint_ref.check import check_module
from cint_ref.exec import loader, run_program
from cint_ref.faults import CompileError
from cint_ref.types import Value

REF_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIB = os.path.join(os.path.dirname(REF_DIR), "lib")


def run(src, entry="run", args=(), grants=()):
    return run_program(src.encode("utf-8"), "t.ci", entry, args=args, grants=grants)


def value(src, entry="run", args=()):
    o = run(src, entry, args)
    assert o.kind == "value", (o.kind, o.diagnostic, o.record, o.message)
    return o.value.value if o.value is not None else None


def diag(src, entry="run"):
    o = run(src, entry)
    assert o.kind == "compile-error", (o.kind, o.value, o.record, o.message)
    p = o.diagnostic.position
    return o.diagnostic.code, p.line, p.column


def check(src, path="t.ci"):
    return check_module(P.parse_module(src, path), loader(None))


def at(src, needle, nth=0):
    """(line, column) of the nth occurrence of `needle` in `src`."""
    i = -1
    for _ in range(nth + 1):
        i = src.index(needle, i + 1)
    return src.count("\n", 0, i) + 1, i - (src.rfind("\n", 0, i) + 1) + 1


DENIED = """import fs;
requires fs.read("/data");
export I64 run(Grants g) {
    U8[4] buf;
    I64 n = fs.read(g, "/data/a", 0, buf) catch -1;
    I64 s = fs.stat(g, "/data/a") catch (e) { return e == fs.FsError.denied ? 7 : 8; };
    return n + s;
}
"""

FIELD_JOB = """import fs;
import console;

requires fs.read("/data");
requires fs.write("/out");

export fs.FsError!void main(Grants g) {
    U8[16] buf;
    I64 n = try fs.read(g, "/data/field.bin", 0, buf);
    "read {n} bytes\\n";
    try fs.put(g, "/out/field.bin", buf[0..n]);
    return;
}
"""


class Parsing(unittest.TestCase):
    def test_extern_forms(self):
        src = ('@service("fs/1", fs.read(path))\n'
               'export extern observe FsError!I64 read(Grants g, in U8[] path, I64 offset, out U8[] buf);\n'
               '@service("console/1")\n'
               'extern observe void write(Grants g, in U8[] bytes);\n'
               'extern pure I64 twice(I64 x);\n'
               'extern unrestricted void beep();\n'
               'error FsError { denied }\n')
        m = P.parse_module(src, "t.ci")
        self.assertEqual([x.name for x in m.externs], ["read", "write", "twice", "beep"])
        self.assertEqual([x.effect for x in m.externs], ["observe", "observe", "pure", "unrestricted"])
        self.assertEqual([x.exported for x in m.externs], [True, False, False, False])
        r = m.externs[0]
        self.assertEqual((r.service, r.capability, r.cap_kinds), ("fs/1", ["fs", "read"], ["path"]))
        self.assertEqual([p.mode for p in r.params], ["in", "in", "in", "out"])
        self.assertEqual((m.externs[1].service, m.externs[1].capability), ("console/1", None))
        self.assertIsNone(m.externs[2].service)

    def test_requires_forms(self):
        m = P.parse_module('requires fs.read("/data");\nrequires console.input;\nrequires service("hla.gateway");\n',
                           "t.ci")
        self.assertEqual([r.segments for r in m.requires], [["fs", "read"], ["console", "input"], ["service"]])
        self.assertEqual([len(r.args) for r in m.requires], [1, 0, 1])

    def test_extern_syntax_errors(self):
        for src, code, needle in [
            ("extern I64 f();\n", "C1050", "I64"),
            ("extern observe I64 f() { return 1; }\n", "C1050", "{"),
            ('export requires fs.read("/a");\n', "C1050", "requires"),
            ('@service("x/1")\nI64 f() { return 1; }\n', "C3043", "I64 f"),
            ('@service("x/1", x.cap(bytes))\nextern observe I64 f();\n', "C1050", "bytes"),
            ("I64 extern = 1;\n", "C4012", "I64"),
        ]:
            with self.subTest(src=src):
                with self.assertRaises(CompileError) as cm:
                    P.parse_module(src, "t.ci")
                d = cm.exception.diagnostic
                self.assertEqual((d.code, d.position.line, d.position.column), (code,) + at(src, needle))

    def test_extern_size_params_refused(self):
        from cint_ref.faults import Refused
        with self.assertRaises(Refused):
            P.parse_module("extern observe I64 f[n](in I64[n] a);\n", "t.ci")


class Checking(unittest.TestCase):
    def test_lib_modules_check(self):
        for name in ("fs.ci", "console.ci"):
            with open(os.path.join(LIB, name), "rb") as f:
                src = f.read().decode("utf-8")
            prog = check(src, name)
            caps = [tuple(x.capability) if x.capability else None for _, x in prog.externs]
            if name == "fs.ci":
                self.assertEqual(caps, [("fs", "read"), ("fs", "read"), ("fs", "write")])
            else:
                self.assertEqual(caps, [("console", "raw"), ("console", "input"), None, ("console", "input"),
                                        ("console", "input")])
            self.assertEqual(prog.host_imports, [])

    def test_c3040_call_without_requires(self):
        src = 'import fs;\nexport I64 run(Grants g) {\n    return fs.stat(g, "/data/a") catch -1;\n}\n'
        self.assertEqual(diag(src), ("C3040",) + at(src, "fs.stat"))
        # The `requires` may sit in any module of the program: here the same one, after the use.
        self.assertEqual(value(src + 'requires fs.read("/data");\n'), -1)

    def test_c3041_requires(self):
        head = 'import fs;\n'
        tail = 'export I64 run(Grants g) {\n    return fs.stat(g, "/data/a") catch -1;\n}\n'
        for line, needle in [
            ('requires fs.list("/data");', "fs.list"),
            ('requires fs.read();', "fs.read"),
            ('requires fs.read("/a", "/b");', "fs.read"),
            ('requires fs.read(1);', "1"),
            ('requires fs.read("data");', '"data"'),
            ('requires fs.read("/data/");', '"/data/"'),
            ('requires fs.read("/data/../x");', '"/data/../x"'),
            ('requires fs.read("/con.txt");', '"/con.txt"'),
            ('requires fs.read("/data./x");', '"/data./x"'),
            ('requires fs.read("/da ta");', '"/da ta"'),
        ]:
            src = head + line + "\n" + tail
            with self.subTest(line=line):
                self.assertEqual(diag(src), ("C3041",) + at(src, needle))
        self.assertEqual(value(head + 'requires fs.read("/");\n' + tail), -1)

    def test_c3041_name_kind(self):
        decl = ('error GwError { denied }\n'
                '@service("hla.gateway/1", service(name))\n'
                'extern observe GwError!I64 connect(Grants g);\n')
        good = decl + 'requires service("hla.gateway");\nexport I64 run(Grants g) {\n    return connect(g) catch -2;\n}\n'
        self.assertEqual(check(good).manifest, ["service:hla.gateway"])
        self.assertEqual(run(good).kind, "refused")  # no table supplies hla.gateway/1 (A-10)
        bad = decl + 'requires service("%s");\nexport I64 run(Grants g) {\n    return connect(g) catch -2;\n}\n' % ("a" * 64)
        self.assertEqual(diag(bad), ("C3041",) + at(bad, '"aaaa'))

    def test_c3042_grants_outside_a_parameter(self):
        for src, needle in [
            ("import fs;\nexport I64 run(Grants g) {\n    Grants h = g;\n    return 1;\n}\n", "Grants h"),
            ("struct S { Grants g; }\nexport I64 run() { return 1; }\n", "Grants g"),
            ("Grants f(Grants g) { return g; }\nexport I64 run() { return 1; }\n", "Grants f"),
            ("export I64 run(Grants[2] g) { return 1; }\n", "Grants["),
            ("const Grants G = 1;\nexport I64 run() { return 1; }\n", "Grants G"),
        ]:
            with self.subTest(src=src):
                self.assertEqual(diag(src), ("C3042",) + at(src, needle))

    def test_grants_not_compared_or_mixed(self):
        src = "export Bool run(Grants g, Grants h) { return g == h; }\n"
        self.assertEqual(diag(src), ("C2103",) + at(src, "g == h"))
        src = "I64 f(I64 x) { return x; }\nexport I64 run(Grants g) { return f(g); }\n"
        self.assertEqual(diag(src), ("C2001",) + at(src, "g);"))

    def test_c3043_service_extern_result(self):
        for src, needle in [
            ('@service("x/1", x.cap)\nextern observe I64 f(Grants g);\nexport I64 run() { return 1; }\n', "f(Grants"),
            ('error E { full }\n@service("x/1", x.cap)\nextern observe E!I64 f(Grants g);\nexport I64 run() { return 1; }\n',
             "f(Grants"),
        ]:
            with self.subTest(src=src):
                self.assertEqual(diag(src), ("C3043",) + at(src, needle))

    def test_c5050_and_c2064_extern_signatures(self):
        for src, code, needle in [
            ("struct S { I64 a; }\nextern observe void f(S s);\nexport I64 run() { return 1; }\n", "C5050", "S s"),
            ("struct S { I64 a; }\nextern observe S f();\nexport I64 run() { return 1; }\n", "C5050", "S f"),
            ("extern observe void f(out I64 x);\nexport I64 run() { return 1; }\n", "C2064", "out I64"),
        ]:
            with self.subTest(src=src):
                self.assertEqual(diag(src), (code,) + at(src, needle))

    def test_out_argument_is_a_writable_place(self):
        src = ('import fs;\nrequires fs.read("/a");\n'
               'export I64 run(Grants g, in U8[] v) {\n    return fs.read(g, "/a/b", 0, v) catch -1;\n}\n')
        self.assertEqual(diag(src), ("C2067",) + at(src, "v) catch"))

    def test_string_literal_is_no_out_argument(self):
        # A string literal is a read-only `in U8[_]` view (LS-41), not a writable place: C2067 at the
        # literal, as cintc reports it, rather than a refusal.
        src = ('import fs;\nrequires fs.read("/a");\n'
               'export I64 run(Grants g) {\n    return fs.read(g, "/a/b", 0, "xx") catch -1;\n}\n')
        self.assertEqual(diag(src), ("C2067",) + at(src, '"xx"'))

    def test_extern_named_without_a_call(self):
        src = 'import fs;\nexport I64 run(Grants g) {\n    I64 x = fs.read;\n    return x;\n}\n'
        self.assertEqual(diag(src), ("C4010",) + at(src, "read;"))
        src = 'extern observe I64 clock();\nexport I64 run() {\n    I64 x = clock;\n    return x;\n}\n'
        self.assertEqual(diag(src), ("C4010",) + at(src, "clock;"))

    def test_duplicate_name_with_an_extern(self):
        src = "extern observe I64 f();\nI64 f() { return 1; }\nexport I64 run() { return 1; }\n"
        self.assertEqual(diag(src), ("C3001",) + at(src, "f()", 1))

    def test_main_parameter_forms(self):
        # C3044 at the first parameter outside LS-221's forms (OQ-306).
        src = "void main(Grants g, I64 x) {}\n"
        self.assertEqual(diag(src, None), ("C3044",) + at(src, "I64 x"))
        src = "void main(I64 x) {}\n"
        self.assertEqual(diag(src, None), ("C3044",) + at(src, "I64 x"))
        src = "void main(in U8[_] args, Grants g) {}\n"
        self.assertEqual(diag(src, None), ("C3044",) + at(src, "Grants g"))
        src = "void main(inout U8[_] args) {}\n"
        self.assertEqual(diag(src, None), ("C3044",) + at(src, "inout"))
        src = "void main(in U8[4] args) {}\n"
        self.assertEqual(diag(src, None), ("C3044",) + at(src, "in U8[4]"))
        src = "void main(Grants g, Grants h) {}\n"
        self.assertEqual(diag(src, None), ("C3044",) + at(src, "Grants h"))
        o = run("void main(Grants g) {}\n", None)
        self.assertEqual((o.kind, o.value), ("value", None))
        o = run("void main(in U8[_] args) {}\n", None)
        self.assertEqual((o.kind, o.value), ("value", None))
        o = run("void main(Grants g, in U8[_] args) {}\n", None)
        self.assertEqual((o.kind, o.value), ("value", None))

    def test_main_program_arguments(self):
        # Each argument is its bytes and one NUL, in order; none gives the empty view (OQ-306).
        src = ('void main(in U8[_] args) {\n'
               '    "{len(args)}:";\n'
               '    for i in 0..len(args) { if (args[i] == 0) { "|"; } else { "{args[i]}"; } }\n'
               '    "\\n";\n'
               '}\n')
        o = run_program(src.encode("utf-8"), "t.ci", None, program_args=("a", "bc", ""))
        self.assertEqual((o.kind, o.stdout), ("value", b"6:97|9899||\n"))
        o = run_program(src.encode("utf-8"), "t.ci", None)
        self.assertEqual((o.kind, o.stdout), ("value", b"0:\n"))
        src = src.replace("main(in U8[_] args)", "main(Grants g, in U8[_] args)")
        o = run_program(src.encode("utf-8"), "t.ci", None, program_args=("x",), grants=("console.input",))
        self.assertEqual((o.kind, o.stdout), ("value", b"2:120|\n"))
        # The view is read-only (LS-119): a write through it is C2060.
        src = "void main(in U8[_] args) { args[0] = 1; }\n"
        self.assertEqual(diag(src, None)[0], "C2060")
        # The words after `--` on the command line are the program arguments.
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "t.ci")
            with open(path, "w", encoding="utf-8") as f:
                f.write('void main(in U8[_] args) { "{len(args)}\\n"; }\n')
            r = subprocess.run([sys.executable, "-m", "cint_ref", "run", path, "--", "ab", "--grant"],
                               cwd=REF_DIR, capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertIn("stdout-bytes 3\n", r.stdout)
            self.assertIn("stdout-sha256 " + hashlib.sha256(b"11\n").hexdigest() + "\n", r.stdout)

    def test_manifest_host_imports_and_effects(self):
        src = ('import fs;\nimport console;\n'
               'requires fs.write("/out");\nrequires fs.read("/data");\nrequires fs.read("/data");\n'
               'extern observe I64 clock_monotonic_ns();\nextern unrestricted void beep();\n'
               'I64 pure_f(I64 x) { return x + 1; }\n'
               'I64 printer(I64 x) { "x={x}\\n"; return x; }\n'
               'I64 caller(I64 x) { return pure_f(x) + clock_monotonic_ns(); }\n'
               'I64 deep(I64 x) { return caller(x); }\n'
               'void noisy() { beep(); }\n'
               'export I64 run(Grants g) { return fs.stat(g, "/data/a") catch -1; }\n')
        prog = check(src)
        self.assertEqual(prog.manifest, ["fs.read:/data", "fs.write:/out"])
        self.assertEqual(prog.host_imports, [("t.clock_monotonic_ns", "observe"), ("t.beep", "unrestricted")])
        eff = {f.name: prog.effects[f] for f in prog.functions.values()}
        self.assertEqual(eff, {"pure_f": "pure", "printer": "observe", "caller": "observe", "deep": "observe",
                               "noisy": "unrestricted", "run": "observe"})

    def test_requires_inside_a_function(self):
        src = 'import fs;\nexport I64 run(Grants g) {\n    requires fs.read("/a");\n    return 1;\n}\n'
        self.assertEqual(diag(src), ("C4012",) + at(src, "requires"))


class Running(unittest.TestCase):
    def test_denied_through_catch(self):
        o = run(DENIED)
        self.assertEqual((o.kind, o.value.render(), o.fuel), ("value", "I64 7", 3))

    def test_denied_through_try_in_main(self):
        o = run(FIELD_JOB, None)
        self.assertEqual((o.kind, o.error, o.stdout), ("error", ("fs.FsError", "denied", "U16 2"), b""))

    def test_main_with_grants_prints(self):
        o = run('import console;\nexport void main(Grants g) {\n    "hi\\n";\n}\n', None)
        self.assertEqual((o.kind, o.value, o.stdout), ("value", None, b"hi\n"))

    def test_entry_with_grants_takes_no_host_value(self):
        src = "export I64 run(Grants g, I64 k) { return k * 2; }\n"
        self.assertEqual(value(src, "run", [Value("I64", 5)]), 10)
        o = run(src, "run", [Value("I64", 5), Value("I64", 6)])
        self.assertEqual(o.kind, "refused")

    def test_host_import_refused_before_the_run(self):
        src = "extern observe I64 clock_monotonic_ns();\nexport I64 run() { return 1; }\n"
        o = run(src)
        self.assertEqual(o.kind, "refused")
        self.assertIn("clock_monotonic_ns", o.message)
        self.assertIn("A-10", o.message)

    def test_unknown_service_refused_before_the_run(self):
        # A service row no table supplies refuses context creation (SPEC-03 A-10), so the oracle refuses
        # the program rather than running the call to `denied`.
        src = ('error E { denied }\n@service("x/1", x.cap)\nextern observe E!I64 f(Grants g);\n'
               'requires x.cap();\nexport I64 run(Grants g) { return f(g) catch -1; }\n')
        o = run(src)
        self.assertEqual(o.kind, "refused")
        self.assertIn("x/1", o.message)
        self.assertIn("A-10", o.message)

    def test_fs_runs_with_a_grant_and_console_writes(self):
        with tempfile.TemporaryDirectory() as d:
            with open(os.path.join(d, "a"), "wb") as f:
                f.write(b"0123456789")
            o = run(DENIED, grants=["fs.read:/data=" + d])
            self.assertEqual((o.kind, o.value.render()), ("value", "I64 14"))
        o = run(DENIED, grants=["fs.read:/data"])
        self.assertEqual(o.kind, "refused")
        self.assertIn("RT-OQ-72", o.message)
        o = run('import console;\nconsole.ConsoleError!I64 w(Grants g) {\n    try console.write(g, "x");\n    return 1;\n}\n'
                'export I64 run(Grants g) {\n    return w(g) catch 0;\n}\n')
        self.assertEqual((o.kind, o.stdout, o.value.render()), ("value", b"x", "I64 1"))

    def test_console_denied(self):
        src = ('import console;\nrequires console.input;\nrequires console.raw;\n'
               'console.ConsoleError!I64 go(Grants g) {\n    U8[8] b;\n'
               '    I64 n = console.read(g, b, 0) catch -1;\n'
               '    try console.raw(g, true);\n    return n;\n}\n'
               'export I64 run(Grants g) {\n'
               '    I64 r = go(g) catch (e) { return e == console.ConsoleError.denied ? 39 : 0; };\n'
               '    return r + (console.window(g) catch -9);\n}\n')
        self.assertEqual(value(src), 39)

    def test_caps_command(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "field_job.ci")
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(FIELD_JOB + "extern observe I64 clock_monotonic_ns();\n")
            r = subprocess.run([sys.executable, "-m", "cint_ref", "caps", path], cwd=REF_DIR,
                               capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(r.stdout.splitlines(), ["capability fs.read:/data", "capability fs.write:/out",
                                                     "extern field_job.clock_monotonic_ns observe",
                                                     "effect field_job.main observe"])


if __name__ == "__main__":
    unittest.main()


class Services(unittest.TestCase):
    """The `fs/1` service over a temporary directory (ref/cint_ref/services.py, as rt/cint_fs.c
    builds it; SPEC-07 SEC-OS-5 to SEC-OS-8; ref/OPEN.md REF-OQ-48)."""
    HEAD = ('import fs;\nrequires fs.read("/data");\nrequires fs.read("/out");\nrequires fs.write("/out");\n'
            'export fs.FsError!I64 run(Grants g) {\n')

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.data, self.out = os.path.join(self.tmp.name, "data"), os.path.join(self.tmp.name, "out")
        os.mkdir(self.data)
        os.mkdir(self.out)
        os.mkdir(os.path.join(self.data, "sub"))
        with open(os.path.join(self.data, "a"), "wb") as f:
            f.write(b"0123456789")
        self.grants = ["fs.read:/data=" + self.data, "fs.read:/out=" + self.out, "fs.write:/out=" + self.out,
                       "console.input"]

    def tearDown(self):
        self.tmp.cleanup()

    def fs(self, body, grants=None):
        return run(self.HEAD + body + "\n}\n", "run", grants=self.grants if grants is None else grants)

    def value(self, body, grants=None):
        o = self.fs(body, grants)
        self.assertEqual(o.kind, "value", (o.kind, o.error, o.message))
        return o.value.value

    def error(self, body, grants=None):
        o = self.fs(body, grants)
        self.assertEqual(o.kind, "error", (o.kind, o.value, o.message))
        self.assertEqual(o.error[0], "fs.FsError")
        return o.error[1]

    def test_read_and_stat(self):
        self.assertEqual(self.value('U8[4] b;\n    I64 n = try fs.read(g, "/data/a", 3, b);\n'
                                    '    return n * 1000 + (b[0] as I64) * 10 + (b[3] as I64);'), 4 * 1000 + 51 * 10 + 54)
        self.assertEqual(self.value('U8[16] b;\n    return try fs.read(g, "/data/a", 4, b);'), 6)
        self.assertEqual(self.value('U8[4] b;\n    return try fs.read(g, "/data/a", 10, b);'), 0)
        self.assertEqual(self.value('return try fs.stat(g, "/data/a");'), 10)
        self.assertEqual(self.error('U8[4] b;\n    return try fs.read(g, "/data/a", 11, b);'), "bounds")
        self.assertEqual(self.error('U8[4] b;\n    return try fs.read(g, "/data/a", -1, b);'), "bounds")

    def test_statuses_of_lookup(self):
        for path, want in (("/data", "is_directory"), ("/data/sub", "is_directory"), ("/data/nope", "not_found"),
                           ("/data/a/x", "not_directory"), ("/data/A", "not_found"), ("/Data/a", "denied"),
                           ("/database/a", "denied"), ("/out/a", "not_found"), ("/data/../a", "path"),
                           ("/data//a", "path"), ("/data/a.", "path"), ("/data/nul.txt", "path"), ("data/a", "path"),
                           ("", "path")):
            self.assertEqual(self.error('return try fs.stat(g, "%s");' % path), want, path)

    @unittest.skipIf(os.name == "nt", "symbolic links need a privilege on Windows")
    def test_links_are_not_entries(self):
        os.symlink("a", os.path.join(self.data, "l"))
        os.symlink("a", os.path.join(self.out, "l2"))
        self.assertEqual(self.error('return try fs.stat(g, "/data/l");'), "not_found")
        self.assertEqual(self.error('try fs.put(g, "/out/l2", "x");\n    return 0;'), "exists")

    def test_put(self):
        self.assertEqual(self.value('try fs.put(g, "/out/n.txt", "hi");\n    return try fs.stat(g, "/out/n.txt");'), 2)
        with open(os.path.join(self.out, "n.txt"), "rb") as f:
            self.assertEqual(f.read(), b"hi")
        self.assertEqual(self.value('try fs.put(g, "/out/n.txt", "bye");\n    return try fs.stat(g, "/out/n.txt");'), 3)
        self.assertEqual(self.error('try fs.put(g, "/out/N.TXT", "x");\n    return 0;'), "exists")
        self.assertEqual(self.value('U8[0] e;\n    try fs.put(g, "/out/empty", e);\n    return try fs.stat(g, "/out/empty");'), 0)
        for path, want in (("/out", "is_directory"), ("/out/d2/x", "not_found"), ("/out/n.txt/y", "not_directory"),
                           ("/data/z", "denied"), ("/elsewhere/z", "denied"), ("/out/..", "path")):
            self.assertEqual(self.error('try fs.put(g, "%s", "x");\n    return 0;' % path), want, path)
        self.assertEqual(sorted(os.listdir(self.out)), ["empty", "n.txt"])

    def test_no_grant_is_denied(self):
        self.assertEqual(self.error('return try fs.stat(g, "/data/a");', []), "denied")
        self.assertEqual(self.error('try fs.put(g, "/out/q", "x");\n    return 0;', ["fs.read:/out=" + self.out]), "denied")

    def test_grant_lines_the_runtime_refuses(self):
        for line in ("fs.read:/data", "fs.read:/data/=" + self.data, "console.input=" + self.data, "fs.read:data=x",
                     "bad name:x", "fs.write:/d=", "", "fs.read:/aux/x=" + self.data, "a" * 64):
            o = self.fs("return 1;", [line])
            self.assertEqual(o.kind, "refused", line)
            self.assertIn("RT-OQ-72", o.message)

    CONSOLE = ('import console;\nrequires console.input;\nrequires console.raw;\n'
               'export console.ConsoleError!I64 run(Grants g) {\n    U8[16] b;\n')

    def console(self, body, grants=("console.input", "console.raw"), stdin=b""):
        """The program over a pipe in place of the standard input: (return code, stdout)."""
        path = os.path.join(self.tmp.name, "c.ci")
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(self.CONSOLE + body + "}\n")
        argv = [sys.executable, "-m", "cint_ref", "run", path, "--entry", "run", "--format", "3"]
        for g in grants:
            argv += ["--grant", g]
        out = subprocess.run(argv, cwd=REF_DIR, capture_output=True, input=stdin)
        return out.returncode, out.stdout.decode("utf-8", "replace")

    def test_console_reads_the_standard_input(self):
        rc, text = self.console('    I64 n = try console.read_line(g, b);\n'
                                '    try console.write(g, b[0 .. n]);\n'
                                '    n = try console.read_line(g, b);\n'
                                '    try console.write(g, b[0 .. n]);\n'
                                '    n = try console.read(g, b, 0);\n'
                                '    try console.write(g, b[0 .. n]);\n'
                                '    n = console.read(g, b, 10) catch (e) { return e == console.ConsoleError.closed ? 100 : 0; };\n'
                                '    return n;\n', stdin=b"ab\ncd\r\nxyz")
        self.assertEqual(rc, 0, text)
        self.assertIn("stdout-sha256 bc5c610196f15a7e0d21a3af77b0e1740a4415a44d8a7ad4905d877825574bf9", text)   # abcdxyz
        self.assertIn("I64 100", text)

    def test_console_without_a_terminal(self):
        rc, text = self.console('    I64 w = console.window(g) catch (e) { return e == console.ConsoleError.unavailable ? 7 : 0; };\n'
                                '    return w;\n')
        self.assertEqual(rc, 0, text)
        self.assertIn("I64 7", text)
        rc, text = self.console('    I64 r = raw_on(g) catch (e) { return e == console.ConsoleError.unavailable ? 8 : 0; };\n'
                                '    return r;\n}\nconsole.ConsoleError!I64 raw_on(Grants g) {\n    try console.raw(g, true);\n    return 0;\n')
        self.assertEqual(rc, 0, text)
        self.assertIn("I64 8", text)
        rc, text = self.console('    I64 n = console.read_line(g, b) catch (e) { return e == console.ConsoleError.denied ? 9 : 0; };\n'
                                '    return n;\n', grants=("console.raw",))
        self.assertEqual(rc, 0, text)
        self.assertIn("I64 9", text)

    def test_run_command_takes_grant(self):
        path = os.path.join(self.tmp.name, "t.ci")
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(self.HEAD + '    return try fs.stat(g, "/data/a");\n}\n')
        out = subprocess.run([sys.executable, "-m", "cint_ref", "run", path, "--entry", "run", "--grant",
                              "fs.read:/data=" + self.data], cwd=REF_DIR, capture_output=True, text=True)
        self.assertEqual(out.returncode, 0, out.stderr)
        self.assertIn("return I64 10", out.stdout)
