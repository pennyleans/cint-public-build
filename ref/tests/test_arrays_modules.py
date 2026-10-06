import os, sys; sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # put ref/ on the path
"""Tests for slice 2 task 2.8 (D-15 rows 6 to 8 and 10): rank-1 arrays, size parameters, views
of scalars and structs, indexing, the entry checks of a call, `import`, module-level state
across modules, `len`, string literals as `in U8` views (BOOT-02), the `profile` line, and
`expect_fault ... at`."""
import shutil
import subprocess
import tempfile
import unittest

from cint_ref import parser as P
from cint_ref.check import check_module
from cint_ref.exec import run_program, test_verdict
from cint_ref.faults import CompileError

REF_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def run(src, entry="run", path="t.ci", root=None):
    return run_program(src.encode("utf-8"), path, entry, root=root)


def value(src, entry="run"):
    o = run(src, entry)
    assert o.kind == "value", (o.kind, o.diagnostic, o.record, o.message)
    return o.value.value


def fault(src, entry="run"):
    o = run(src, entry)
    assert o.kind == "fault", (o.kind, o.diagnostic, o.value, o.message)
    r = o.record
    return (r.code, r.operation, [v.render() for v in r.operands], r.limit.render() if r.limit else None,
            r.position.line, r.position.column, len(r.stack))


def diag(src, entry="run"):
    o = run(src, entry)
    assert o.kind == "compile-error", (o.kind, o.value, o.record, o.message)
    p = o.diagnostic.position
    return o.diagnostic.code, p.line, p.column


class Arrays(unittest.TestCase):
    def test_early_module_variable_extents_have_declared_types(self):
        cases = [
            ("I64 N = 2;\nstruct R { I64[N] a; }\n", "C6004", 2, 16),
            ("struct R { I64[N] a; }\nI64 N = 2;\n", "C6004", 1, 16),
            ("I64 N = 2;\nstruct R { I64[N + 1] a; }\n", "C6004", 2, 16),
            ("U8 N = 2;\nstruct R { I64[N] a; }\n", "C2001", 2, 16),
            ("Bool N = true;\nstruct R { I64[N] a; }\n", "C2001", 2, 16),
            ("I64[2] N;\nstruct R { I64[N] a; }\n", "C2001", 2, 16),
            ("I64 N = 2;\nvoid f(in I64[N] a) {}\n", "C6004", 2, 15),
            ("void f(in I64[N] a) {}\nI64 N = 2;\n", "C6004", 1, 15),
            ("I64 V = 2;\nconst I64 N = V;\nstruct R { I64[N] a; }\n", "C6004", 2, 15),
        ]
        for decls, code, line, column in cases:
            with self.subTest(declarations=decls):
                src = decls + "export I64 run() { return 0; }\n"
                self.assertEqual(diag(src), (code, line, column))

    def test_extent_type_lookup_keeps_later_initializer_errors_later(self):
        src = "I64 N = 1 / 0;\nstruct R { I64[N] a; }\nexport I64 run() { return 0; }\n"
        self.assertEqual(diag(src), ("C6004", 2, 16))

    def test_type_lookup_does_not_move_unrelated_module_errors(self):
        src = "Missing N;\nstruct R { R a; }\nexport I64 run() { return 0; }\n"
        self.assertEqual(diag(src), ("C2055", 2, 14))

    def test_cyclic_module_variable_type_dependency_is_refused(self):
        for decls in ("I64[N] N;\n", "I64[M] N;\nI64[N] M;\n"):
            with self.subTest(declarations=decls):
                outcome = run(decls + "export I64 run() { return 0; }\n")
                self.assertEqual(outcome.kind, "refused")
                self.assertIn("cyclic module-variable type dependency", outcome.message)

    def test_early_type_lookup_uses_module_scope(self):
        src = "I64[n] N;\nvoid f[n](in I64[n] a, in I64[N] b) {}\nexport I64 run() { return 0; }\n"
        self.assertEqual(diag(src), ("C3005", 1, 5))

    def test_early_annotation_calls_resolve_function_signatures(self):
        cases = [
            "I64[f()] N;\nstruct R { I64[len(N)] a; }\nI64 f() { return 2; }\n",
            "I64[f()] N;\nstruct R { I64[N] a; }\nI64 f() { return 2; }\n",
            "I64[f(2)] N;\nstruct R { I64[N[0]] a; }\nI64 f(I64 x) { return x; }\n",
            "I64[f()] N;\nvoid g(in I64[len(N)] a) {}\nI64 f() { return 2; }\n",
        ]
        for declarations in cases:
            with self.subTest(declarations=declarations):
                self.assertEqual(diag(declarations + "export I64 run() { return 0; }\n"), ("C6004", 1, 5))

    def test_cyclic_early_function_signature_is_refused(self):
        src = "I64[f(N)] N;\nI64 f(in I64[len(N)] a) { return 2; }\nexport I64 run() { return 0; }\n"
        outcome = run(src)
        self.assertEqual(outcome.kind, "refused")

    def test_record_array_containment_reports_the_closing_field(self):
        cases = [
            ("struct A {\n    A[1] items;\n}\n", 2, 10),
            ("struct A {\n    B[2] items;\n}\nstruct B {\n    A parent;\n}\n", 5, 7),
            ("struct A {\n    B[2] children;\n}\nstruct B {\n    A[1] parents;\n}\n", 5, 10),
        ]
        for declarations, line, column in cases:
            for local in ("", "A item; "):
                with self.subTest(declarations=declarations, local=local):
                    src = declarations + "export I64 run() { " + local + "return 0; }\n"
                    with self.assertRaises(CompileError) as caught:
                        check_module(P.parse_module(src, "array_cycle.ci"))
                    d = caught.exception.diagnostic
                    self.assertEqual((d.code, d.position.path, d.position.line, d.position.column),
                                     ("C2055", "array_cycle.ci", line, column))

    def test_record_array_cycle_precedes_function_signature_errors(self):
        src = "struct A {\n    A[1] items;\n}\nexport Missing run() { return 0; }\n"
        self.assertEqual(diag(src), ("C2055", 2, 10))

    def test_acyclic_record_array_with_forward_element_type(self):
        src = ("struct A { B[2] items; }\nstruct B { I64 v; }\n"
               "export I64 run() { A item; item.items[1].v = 7; return item.items[1].v; }\n")
        self.assertEqual(value(src), 7)

    def test_zero_filled_local_and_index(self):
        self.assertEqual(value("export I64 run() {\n    I64[4] a;\n    a[2] = 5;\n    return a[2] + a[3];\n}\n"), 5)

    def test_bool_and_struct_elements(self):
        self.assertTrue(value("export Bool run() {\n    Bool[2] f;\n    f[1] = true;\n    return f[1] && !f[0];\n}\n"))
        src = ("struct T { I32 k; I64[2] v; }\nexport I64 run() {\n    T[3] ts;\n    ts[1].v[1] = 4;\n"
               "    ts[2].k = 1;\n    return ts[1].v[1] + (ts[2].k as I64);\n}\n")
        self.assertEqual(value(src), 5)

    def test_bounds_fault_record(self):
        # SPEC-01 IM-186: one I64 operand (the index), the extent as limit, at the `[`.
        src = "export I64 run() {\n    I64[4] a;\n    I64 i = 4;\n    return a[i];\n}\n"
        self.assertEqual(fault(src), ("E_BOUNDS", "index.checked.i64", ["I64 4"], "I64 4", 4, 13, 0))
        src = "export U8 run() {\n    U8[3] a;\n    I64 i = -1;\n    return a[i];\n}\n"
        self.assertEqual(fault(src)[:4], ("E_BOUNDS", "index.checked.u8", ["I64 -1"], "I64 3"))
        src = "struct C { I64 x; }\nexport I64 run() {\n    C[1] c;\n    I64 i = 1;\n    return c[i].x;\n}\n"
        self.assertEqual(fault(src)[1], "index.checked.struct")

    def test_place_checked_before_value(self):
        # SPEC-04 LS-139: `a[-1] = 1 / 0;` faults E_BOUNDS, not E_DIV_ZERO.
        src = "export I64 run() {\n    I64[4] a;\n    I64 z = 0;\n    a[-1] = 1 / z;\n    return 0;\n}\n"
        self.assertEqual(fault(src)[0], "E_BOUNDS")
        src = "export I64 run() {\n    I64[2] a;\n    I64 i = 2;\n    a[i] += 1 / 0;\n    return 0;\n}\n"
        self.assertEqual(diag(src)[0], "C6001")

    def test_index_type_is_i64(self):
        self.assertEqual(diag("export I64 run() {\n    I64[4] a;\n    I32 i = 0;\n    return a[i];\n}\n"),
                         ("C2001", 4, 14))
        self.assertEqual(value("export I64 run() {\n    I64[4] a;\n    I32 i = 1;\n    a[i as I64] = 3;\n"
                               "    return a[1];\n}\n"), 3)

    def test_indexing_a_scalar(self):
        self.assertEqual(diag("export I64 run() {\n    I64 a = 0;\n    return a[0];\n}\n")[0], "C2103")

    def test_whole_array_copy(self):
        src = ("export I64 run() {\n    I64[3] a;\n    a[1] = 4;\n    I64[3] b = a;\n    b[1] = 5;\n"
               "    I64[3] c;\n    c = b;\n    return a[1] * 100 + b[1] * 10 + c[1];\n}\n")
        self.assertEqual(value(src), 455)
        self.assertEqual(diag("export I64 run() {\n    I64[3] a;\n    I64[4] b = a;\n    return 0;\n}\n"),
                         ("C2012", 3, 16))

    def test_struct_copy_copies_array_fields(self):
        src = ("struct R { I64[2] s; }\nexport I64 run() {\n    R a;\n    a.s[0] = 1;\n    R b = a;\n"
               "    b.s[0] = 9;\n    return a.s[0] * 10 + b.s[0];\n}\n")
        self.assertEqual(value(src), 19)

    def test_array_comparison_is_c2013(self):
        self.assertEqual(diag("export Bool run() {\n    I64[2] a;\n    I64[2] b;\n    return a == b;\n}\n")[0],
                         "C2013")

    def test_zero_extent(self):
        src = "export I64 run() {\n    I64[0] none;\n    I64 i = 0;\n    return none[i];\n}\n"
        self.assertEqual(fault(src)[:4], ("E_BOUNDS", "index.checked.i64", ["I64 0"], "I64 0"))

    def test_extent_from_size_parameters(self):
        # SPEC-04 LS-62: in a function body an extent may be an I64 expression of size parameters.
        src = ("I64 f[n](in I64[n] a) {\n    I64[n * 2] t;\n    t[n] = 9;\n    return len(t) * 100 + t[n];\n}\n"
               "export I64 run() {\n    I64[3] a;\n    return f(a);\n}\n")
        self.assertEqual(value(src), 609)
        self.assertEqual(diag("I64[n] g;\nexport I64 run() { return 0; }\n")[0], "C3005")
        self.assertEqual(diag("export I64 run() {\n    I64 k = 2;\n    I64[k] a;\n    return 0;\n}\n"),
                         ("C6004", 3, 9))

    def test_print_an_array(self):
        # SPEC-04 LS-214: `[a, b, c]`, every element, each with the hole's spec applied.
        src = 'export I64 run() {\n    I64[3] a;\n    a[1] = 10;\n    "{a} {a:x} {a=}\\n";\n    return 0;\n}\n'
        self.assertEqual(run(src).stdout, b"[0, 10, 0] [0, a, 0] a=[0, 10, 0]\n")

    def test_len(self):
        self.assertEqual(value("export I64 run() {\n    I64[5] a;\n    return len(a) + len(\"ab\");\n}\n"), 7)


class Views(unittest.TestCase):
    def test_inout_writes_reach_the_caller(self):
        src = ("void up[n](inout I64[n] xs) {\n    for i in 0..n {\n        xs[i] = i + 1;\n    }\n}\n"
               "export I64 run() {\n    I64[3] a;\n    up(a);\n    return a[2] * 100 + a[1] * 10 + a[0];\n}\n")
        self.assertEqual(value(src), 321)

    def test_inout_struct_parameter(self):
        src = ("struct P { I64 x; I64[2] h; }\nvoid bump(inout P p) {\n    p.x += 5;\n    p.h[1] = 7;\n}\n"
               "export I64 run() {\n    P a;\n    bump(a);\n    return a.x * 10 + a.h[1];\n}\n")
        self.assertEqual(value(src), 57)

    def test_in_struct_parameter_is_a_copy(self):
        src = ("struct P { I64 x; }\nI64 f(P p) {\n    return p.x;\n}\n"
               "export I64 run() {\n    P a = P(3);\n    return f(a);\n}\n")
        self.assertEqual(value(src), 3)

    def test_parameter_modes(self):
        self.assertEqual(diag("void f(in I64[_] a) {\n    a[0] = 1;\n}\nexport I64 run() { return 0; }\n"),
                         ("C2060", 2, 5))
        self.assertEqual(diag("void f(inout I64 a) { }\nexport I64 run() { return 0; }\n"), ("C2064", 1, 8))
        self.assertEqual(diag("void f(out I64[_] a) { }\nexport I64 run() { return 0; }\n"), ("C5001", 1, 8))
        self.assertEqual(diag("I64 f[n, m](in I64[n] a) { return n; }\nexport I64 run() { return 0; }\n"),
                         ("C2065", 1, 10))
        self.assertEqual(diag("void f[n](in I64[n] a) {\n    n = 1;\n}\nexport I64 run() { return 0; }\n"),
                         ("C2060", 2, 5))

    def test_inout_argument_must_be_writable(self):
        src = "void f(inout I64[_] a) { a[0] = 1; }\nvoid g(in I64[_] b) {\n    f(b);\n}\nexport I64 run() { return 0; }\n"
        self.assertEqual(diag(src), ("C2067", 3, 7))

    def test_static_shape_c2012(self):
        src = ("I64 f[n](in I64[n] a, in I64[n] b) { return 0; }\n"
               "export I64 run() {\n    I64[4] a;\n    I64[3] b;\n    return f(a, b);\n}\n")
        self.assertEqual(diag(src), ("C2012", 5, 17))
        src = "I64 f(in I64[4] a) { return 0; }\nexport I64 run() {\n    I64[3] a;\n    return f(a);\n}\n"
        self.assertEqual(diag(src), ("C2012", 4, 14))

    def test_run_time_shape_fault(self):
        # SPEC-04 LS-117, LS-141: E_SHAPE at the call (the callee name), before it is entered.
        src = ("I64 f[n](in I64[n] a, in I64[n] b) { return 0; }\n"
               "I64 g[n, m](in I64[n] a, in I64[m] b) {\n    return f(a, b);\n}\n"
               "export I64 run() {\n    I64[4] a;\n    I64[3] b;\n    return g(a, b);\n}\n")
        self.assertEqual(fault(src), ("E_SHAPE", "bind.shape", ["I64 1", "I64 0", "I64 3"], "I64 4", 3, 12, 1))
        o = run(src)
        self.assertEqual(o.fuel, 2)                     # the entry and g; f is not entered
        src = ("I64 f(in I64[4] a) { return 0; }\nI64 g[n](in I64[n] a) {\n    return f(a);\n}\n"
               "export I64 run() {\n    I64[3] a;\n    return g(a);\n}\n")
        self.assertEqual(fault(src)[:4], ("E_SHAPE", "bind.shape", ["I64 0", "I64 0", "I64 3"], "I64 4"))

    def test_alias_static_and_run_time(self):
        # SPEC-04 LS-121: C5010 when provable; E_ALIAS at entry when it depends on index values.
        src = ("void f(inout I64[_] a, in I64[_] b) { a[0] = b[0]; }\n"
               "export I64 run() {\n    I64[4] a;\n    f(a, a);\n    return 0;\n}\n")
        self.assertEqual(diag(src), ("C5010", 4, 10))
        src = ("struct S { I64[2] v; I64[2] w; }\nvoid f(inout I64[_] a, in I64[_] b) { a[0] = b[0]; }\n"
               "export I64 run() {\n    S[3] s;\n    I64 i = 1;\n    I64 j = 1;\n    f(s[i].v, s[j].v);\n    return 0;\n}\n")
        self.assertEqual(fault(src), ("E_ALIAS", "bind.alias", ["I64 0", "I64 1"], None, 7, 5, 0))
        disjoint = src.replace("I64 j = 1;", "I64 j = 2;")
        self.assertEqual(value(disjoint), 0)
        fields = src.replace("f(s[i].v, s[j].v);", "f(s[1].v, s[1].w);")
        self.assertEqual(value(fields), 0)              # two fields of one struct are disjoint
        whole = ("struct S { I64[2] v; }\nvoid f(inout S s, in I64[_] b) { s.v[0] = b[1]; }\n"
                 "export I64 run() {\n    S s;\n    f(s, s.v);\n    return 0;\n}\n")
        self.assertEqual(diag(whole), ("C5010", 5, 10))

    def test_in_views_may_overlap(self):
        src = ("I64 f[n](in I64[n] a, in I64[n] b) { return a[0] + b[0]; }\n"
               "export I64 run() {\n    I64[1] c;\n    c[0] = 2;\n    return f(c, c);\n}\n")
        self.assertEqual(value(src), 4)

    def test_module_state_argument_c5012(self):
        # SPEC-04 LS-122: the argument views g, which the callee writes through w.
        src = "I64[4] g;\nvoid w() { g[0] = 1; }\nvoid f(in I64[_] a) { w(); }\nvoid main() {\n    f(g);\n}\n"
        self.assertEqual(diag(src, None), ("C5012", 5, 7))
        ok = src.replace("void w() { g[0] = 1; }", "void w() { }")
        self.assertEqual(run(ok, None).kind, "value")

    def test_string_literal_as_u8_view(self):
        # BOOT-02 (ref/OPEN.md REF-OQ-30): a string literal binds an `in U8` view of its bytes.
        src = ("I64 w[n](in U8[n] s) {\n    I64 t = 0;\n    for i in 0..n {\n        t = t + (i + 1) * (s[i] as I64);\n"
               "    }\n    return t;\n}\nexport I64 run() {\n    return w(\"ab\" \"c\");\n}\n")
        self.assertEqual(value(src), 97 + 2 * 98 + 3 * 99)
        self.assertEqual(run("void f(inout U8[_] s) { }\nexport I64 run() {\n    f(\"ab\");\n    return 0;\n}\n").kind,
                         "refused")
        self.assertEqual(diag("I64 f(in U8[_] s) { return 0; }\nexport I64 run() {\n    return f(\"a{x}\");\n}\n")[0],
                         "C1033")


class Modules(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.root)

    def write(self, rel, text):
        path = os.path.join(self.root, *rel.split("/"))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(text.encode("utf-8"))

    def main(self, text, entry="run"):
        self.write("m/main.ci", text)
        return run(text, entry, "m/main.ci", self.root)

    def test_containment_distinguishes_same_named_imported_records(self):
        self.write("m/lib/records.ci", "export struct S { I64 v; }\n")
        for field_type, access in (("records.S", "item.part.v"),
                                   ("records.S[2]", "item.part[1].v")):
            with self.subTest(field_type=field_type):
                o = self.main("import m.lib.records;\nstruct S { " + field_type + " part; }\n"
                              "export I64 run() { S item; " + access + " = 7; return " + access + "; }\n")
                self.assertEqual(o.kind, "value", o.diagnostic)
                self.assertEqual(o.value.value, 7)

    GEO = ("export const I64 SCALE = 10;\nexport struct V2 { I64 x; I64 y; }\n"
           "export I64 dot2(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }\nI64 hidden() { return 1; }\n"
           "I64 calls = 0;\nexport I64 tick() {\n    calls += 1;\n    return calls;\n}\n")

    def test_qualified_names_and_state(self):
        self.write("m/lib/geo.ci", self.GEO)
        o = self.main("import m.lib.geo;\nexport I64 run() {\n    geo.V2 a = geo.V2(1, 2);\n"
                      "    geo.V2 b = geo.V2(x = 3, y = 4);\n    _ = geo.tick();\n"
                      "    return geo.dot2(a, b) * geo.SCALE + geo.tick();\n}\n")
        self.assertEqual((o.kind, o.value.value), ("value", 112))
        self.assertEqual([(mod, n, v.value) for mod, n, v in o.state], [("m.lib.geo", "calls", 2)])

    def test_alias_and_selected_names(self):
        self.write("m/lib/geo.ci", self.GEO)
        o = self.main("import m.lib.geo.{V2, dot2};\nimport m.lib.geo as g;\n"
                      "export I64 run() {\n    V2 a = V2(2, 3);\n    return dot2(a, a) + g.SCALE;\n}\n")
        self.assertEqual((o.kind, o.value.value), ("value", 23))
        o = self.main("import m.lib.geo.{V2};\nstruct V2 { I64 z; }\nexport I64 run() { return 0; }\n")
        self.assertEqual((o.diagnostic.code, o.diagnostic.position.line), ("C3002", 2))

    def test_private_and_missing_names(self):
        self.write("m/lib/geo.ci", self.GEO)
        o = self.main("import m.lib.geo;\nexport I64 run() {\n    return geo.hidden();\n}\n")
        self.assertEqual((o.diagnostic.code, str(o.diagnostic.position)), ("C3008", "m/main.ci:3:16"))
        o = self.main("import m.lib.geo;\nexport I64 run() {\n    return geo.nothing();\n}\n")
        self.assertEqual(o.diagnostic.code, "C3005")

    def test_load_errors_and_cycles(self):
        o = self.main("import m.nothere;\nexport I64 run() { return 0; }\n")
        self.assertEqual((o.diagnostic.code, str(o.diagnostic.position)), ("C3009", "m/main.ci:1:8"))
        self.write("m/a.ci", "import m.main;\nexport I64 f() { return 0; }\n")
        o = self.main("import m.a;\nexport I64 run() { return 0; }\n")
        self.assertEqual((o.diagnostic.code, str(o.diagnostic.position)), ("C3003", "m/a.ci:1:8"))
        o = self.main("export I64 run() { return 0; }\nimport m.a;\n")
        self.assertEqual(o.diagnostic.code, "C3012")

    def test_module_paths_c3030_and_case(self):
        """SPEC-09 CINTC-12: path rules are C3030 at <path>:1:1 before any read, and a name is
        resolved byte for byte against the directory entry on every host."""
        self.write("m/lib/Util.ci", "export I64 one() { return 1; }\n")
        o = self.main("import m.lib.Util;\nimport m.lib.util;\nexport I64 run() { return 0; }\n")
        self.assertEqual((o.diagnostic.code, str(o.diagnostic.position)), ("C3030", "m/lib/util.ci:1:1"))
        o = self.main("import m.lib.util;\nexport I64 run() { return 0; }\n")
        self.assertEqual((o.diagnostic.code, str(o.diagnostic.position)), ("C3009", "m/main.ci:1:8"))
        o = self.main("import m.lib.Util;\nexport I64 run() { return Util.one(); }\n")
        self.assertEqual((o.kind, o.value.value), ("value", 1))
        for name in ("con", "AUX", "lpt9", "Com1"):
            o = self.main("import m.%s;\nexport I64 run() { return 0; }\n" % name)
            self.assertEqual((o.diagnostic.code, str(o.diagnostic.position)), ("C3030", "m/%s.ci:1:1" % name))
        o = run("export I64 run() { return 0; }\n", "run", "m/1bad.ci", self.root)
        self.assertEqual((o.diagnostic.code, str(o.diagnostic.position)), ("C3030", "m/1bad.ci:1:1"))
        o = run("export I64 run() { return 0; }\n", "run", "m/a-b.ci", self.root)
        self.assertEqual(o.diagnostic.code, "C3030")

    def test_state_in_module_order(self):
        """SPEC-09 CONF-11 rule 10: state lines in module order, the root module first."""
        self.write("m/lib/geo.ci", self.GEO)
        o = self.main("import m.lib.geo;\nI64 seen = 5;\nexport I64 run() {\n    seen += 2;\n"
                      "    return geo.tick();\n}\n")
        self.assertEqual([(mod, n, v.value) for mod, n, v in o.state], [("m.main", "seen", 7), ("m.lib.geo", "calls", 1)])

    def test_importing_a_script(self):
        self.write("m/s.ci", "I64 x = 1;\n\"hi\\n\";\n")
        o = self.main("import m.s;\nexport I64 run() { return 0; }\n")
        self.assertEqual((o.diagnostic.code, str(o.diagnostic.position)), ("C3010", "m/main.ci:1:8"))
        self.assertTrue(any("is a script because" in n for n in o.diagnostic.notes))

    def test_imported_module_is_checked_first(self):
        self.write("m/bad.ci", "export I64 f() {\n    return y;\n}\n")
        o = self.main("import m.bad;\nexport I64 run() { return z; }\n")
        self.assertEqual(str(o.diagnostic.position), "m/bad.ci:2:12")

    def test_cli_reads_imports_from_the_source_root(self):
        self.write("conformance/m/lib/geo.ci", self.GEO)
        self.write("conformance/m/main.ci", "import m.lib.geo;\nexport I64 run() { return geo.SCALE; }\n")
        out = subprocess.run([sys.executable, "-m", "cint_ref", "run", os.path.join(self.root, "conformance", "m", "main.ci"),
                              "--entry", "run"], capture_output=True, cwd=REF_DIR).stdout.decode("ascii")
        self.assertIn("return I64 10\n", out)


class Surface(unittest.TestCase):
    def test_profile_line(self):
        self.assertEqual(value('profile "cint-core-1";\nexport I64 run() { return 1; }\n'), 1)
        self.assertEqual(run('profile "cint-boot-1";\nexport I64 run() { return 1; }\n').kind, "refused")
        self.assertEqual(diag('export I64 run() { return 1; }\nprofile "cint-core-1";\n')[0], "C1050")

    def test_expect_fault_at(self):
        # SPEC-04 LS-236, LS-240 (Proposed): the code, and with `at N` the line of the fault position.
        src = ('test "t" expect_fault E_OVERFLOW at 4 {\n    I64 a = 9223372036854775807;\n    I64 b = 1;\n'
               '    I64 c = a + b;\n}\n')
        m = P.parse_module(src, "t.ci")
        self.assertEqual((m.tests[0].expect_fault, m.tests[0].expect_line), ("E_OVERFLOW", 4))
        o = run_program(src.encode(), "t.ci", None, test="t")
        self.assertTrue(test_verdict(m.tests[0], o))
        m.tests[0].expect_line = 3
        self.assertFalse(test_verdict(m.tests[0], o))
        m.tests[0].expect_line, m.tests[0].expect_fault = None, "E_NARROW"
        self.assertFalse(test_verdict(m.tests[0], o))
        bad = src.replace("at 4", "at 0x4")
        self.assertEqual(diag(bad, None)[0], "C1050")


if __name__ == "__main__":
    unittest.main()
