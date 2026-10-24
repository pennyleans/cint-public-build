import os, sys; sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # put ref/ on the path
"""Byte strings and `equal` (ruling OQ-307 A; SPEC-04 LS-41 to LS-44, LS-71, LS-145): `b"..."`
is a read-only `U8` view of static storage; `equal(a, b)` compares two arrays or views of one
element type and one rank, is false for other shapes, and charges a reduction's fuel."""
import unittest

from cint_ref.exec import run_program


def run(src, fuel=None):
    return run_program(src.encode("utf-8"), "t.ci", "main", fuel=fuel)


def out(src):
    o = run(src)
    assert o.kind == "value", (o.kind, o.diagnostic, o.record, o.message)
    return o.stdout.decode("utf-8")


def diag(src):
    o = run(src)
    assert o.kind == "compile-error", (o.kind, o.stdout, o.record, o.message)
    p = o.diagnostic.position
    return o.diagnostic.code, p.line, p.column


def main(body, before=""):
    return before + "export void main() {\n" + body + "}\n"


class ByteStrings(unittest.TestCase):
    def test_bytes_and_escapes(self):
        self.assertEqual(out(main('    in U8[_] v = b"a\\x00\\xff\\n\\t\\r\\\\\\"\\\'{{}}";\n'
                                  '    "{len(v)} {v[1]} {v[2]} {v[3]} {v[4]} {v[5]} {v[6]} {v[7]} {v[8]} {v[9]} {v[10]}\\n";\n')),
                         "11 0 255 10 9 13 92 34 39 123 125\n")

    def test_source_text_is_its_utf8(self):
        self.assertEqual(out(main('    in U8[_] v = b"\u00e9";\n    "{len(v)} {v[0]} {v[1]}\\n";\n')), "2 195 169\n")

    def test_empty(self):
        self.assertEqual(out(main('    in U8[_] v = b"";\n    "{len(v)}\\n";\n')), "0\n")

    def test_argument_initializer_and_assignment(self):
        src = main('    I64 m = width(b"xyz!");\n    U8[3] a = b"abc";\n    "{m} {a[2]}\\n";\n'
                   '    a = b"xyz";\n    U8 q = b"q"[0];\n    "{a[0]} {q}\\n";\n',
                   'I64 width(in U8[_] v) { return len(v); }\n\n')
        self.assertEqual(out(src), "4 99\n120 113\n")

    def test_extent_must_agree(self):
        self.assertEqual(diag(main('    U8[3] a = b"abcd";\n')), ("C2012", 2, 15))

    def test_read_only(self):
        self.assertEqual(diag('void put(inout U8[_] v) { v[0] = 1; }\n' + main('    put(b"abc");\n'))[0], "C2067")
        self.assertEqual(diag(main('    inout U8[_] v = b"abc";\n')), ("C2067", 2, 21))

    def test_element_type(self):
        self.assertEqual(diag(main('    I8[3] a = b"abc";\n'))[0], "C2001")

    def test_not_in_a_kernel(self):
        src = ('kernel k[n](in U8[n] a, out I64[n] b) over [i: n] {\n    b[i] = b"xy"[0] as I64;\n}\n\n'
               'export void main() {\n    U8[2] a;\n    I64[2] b;\n    k(a, b);\n}\n')
        self.assertEqual(run(src).kind, "refused")

    def test_lexical_errors(self):
        self.assertEqual(diag(main('    in U8[_] v = b"\\u{41}";\n')), ("C1032", 2, 20))
        self.assertEqual(diag(main('    in U8[_] v = b"\\xzz";\n')), ("C1032", 2, 20))
        self.assertEqual(diag(main('    in U8[_] v = b"\\q";\n')), ("C1032", 2, 20))
        self.assertEqual(diag(main('    in U8[_] v = b"a{b";\n')), ("C1033", 2, 21))
        self.assertEqual(diag(main('    in U8[_] v = b"a}b";\n')), ("C1039", 2, 21))


class Equal(unittest.TestCase):
    def test_values(self):
        src = main('    I32[3] x = [1, 2, 3];\n    I32[3] y = [1, 2, 3];\n    I32[2] z = [1, 2];\n'
                   '    Bool[2] m = [true, false];\n    I64[2, 2] g = [[1, 2], [3, 4]];\n'
                   '    I64[2, 2] h = [[1, 2], [3, 5]];\n'
                   '    Bool p = equal(x, y);\n    Bool q = equal(x[0..2], z);\n    Bool r = equal(x, [1, 2, 4]);\n'
                   '    Bool s = equal(b"ab", b"abc");\n    Bool t = equal(b"abc", b"abc");\n'
                   '    Bool u = equal(m, [true, false]);\n    Bool v = equal(g, h);\n    Bool w = equal(g, g);\n'
                   '    Bool k = equal(x, z);\n'
                   '    "{p} {q} {r} {s} {t} {u} {v} {w} {k}\\n";\n')
        self.assertEqual(out(src), "true true false false true true false true false\n")

    def test_views_and_fixed_point(self):
        src = main('    Q8.8[3] a = [Q8.8.raw(1), Q8.8.raw(2), Q8.8.raw(3)];\n'
                   '    Q8.8[3] b = [Q8.8.raw(1), Q8.8.raw(2), Q8.8.raw(3)];\n'
                   '    U8[4] c = b"abba";\n    Bool p = equal(c[0..2], b"ab");\n    Bool q = equal(c[2..4], b"ab");\n'
                   '    Bool r = same(c, b"abba");\n    "{equal(a, b)} {p} {q} {r}\\n";\n',
                   'Bool same(in U8[_] a, in U8[_] b) { return equal(a, b); }\n\n')
        self.assertEqual(out(src), "true true false true\n")

    def test_type_errors(self):
        self.assertEqual(diag(main('    I32[3] x;\n    I64[3] y;\n    Bool p = equal(x, y);\n')), ("C2001", 4, 23))
        self.assertEqual(diag(main('    I32[3] x;\n    I32[1, 3] y;\n    Bool p = equal(x, y);\n')), ("C2001", 4, 23))
        self.assertEqual(diag(main('    I32 x = 3;\n    Bool p = equal(x, x);\n')), ("C2103", 3, 20))
        self.assertEqual(diag(main('    I32[3] x;\n    Bool p = equal(x);\n'))[0], "C2022")
        self.assertEqual(diag(main('    I32[3] x;\n    I32 p = equal(x, x);\n'))[0], "C2001")

    def test_not_in_a_kernel(self):
        src = ('kernel k[n](in I64[n] a, out I64[n] b) over [i: n] {\n    b[i] = 0;\n    if (equal(a, a)) {\n'
               '        b[i] = 1;\n    }\n}\n\nexport void main() {\n    I64[2] a;\n    I64[2] b;\n    k(a, b);\n}\n')
        self.assertEqual(run(src).kind, "refused")

    def test_struct_elements(self):
        src = 'struct P { I32 a; }\n\n' + main('    P[2] x;\n    Bool p = equal(x, x);\n')
        self.assertEqual(diag(src), ("C2103", 5, 20))

    def test_fuel(self):
        base = run(main('    I32[200] x;\n    I32[200] y;\n    I32[3] z;\n    Bool p = true;\n'))
        same = run(main('    I32[200] x;\n    I32[200] y;\n    I32[3] z;\n    Bool p = equal(x, y);\n'))
        other = run(main('    I32[200] x;\n    I32[200] y;\n    I32[3] z;\n    Bool p = equal(x, z);\n'))
        self.assertEqual(same.fuel - base.fuel, 4)        # ceil(200 / 64)
        self.assertEqual(other.fuel, base.fuel)           # other shapes: no elements read
        o = run(main('    I32[200] x;\n    I32[200] y;\n    Bool p = equal(x, y);\n'), fuel=base.fuel)
        self.assertEqual(o.kind, "fault")
        self.assertEqual(o.record.code, "E_FUEL")


if __name__ == "__main__":
    unittest.main()
