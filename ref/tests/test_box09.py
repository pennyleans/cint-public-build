import os, sys; sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # put ref/ on the path
"""Tests for box 09 (docs/design/notes/2026-10-05-box09-arrays-views-kernels.md, rulings R1 to
R10): arrays of rank 2 to 4, partial indexing, slices, array literals, local views, the view
functions, the reductions, `dot`, `copy` and `fill`, kernels, and `.expect` format 4 (SPEC-09
CONF-11 rule 13). The held box 09 cases in conformance/ state the outcomes of whole programs;
these tests pin the rules one at a time."""
import unittest

from cint_ref import expect
from cint_ref import parser as P
from cint_ref.check import check_module
from cint_ref.exec import run_program
from cint_ref.faults import CompileError, Descriptor, Refused


def run(src, entry="run", fuel=None):
    return run_program(src.encode("utf-8"), "t.ci", entry, fuel)


def at(src, needle, nth=0):
    """(line, column) of the nth occurrence of `needle` in `src`."""
    i = -1
    for _ in range(nth + 1):
        i = src.index(needle, i + 1)
    return src.count("\n", 0, i) + 1, i - (src.rfind("\n", 0, i) + 1) + 1


def value(src, **kw):
    o = run(src, **kw)
    assert o.kind == "value", (o.kind, o.diagnostic, o.record, o.message)
    return o.value.render()


def fault(src, **kw):
    """(code, operation, operands, exact, limit, (line, column), stack positions) of the fault."""
    o = run(src, **kw)
    assert o.kind == "fault", (o.kind, o.diagnostic, o.value, o.message)
    r = o.record
    return (r.code, r.operation, [v.render() for v in r.operands], r.exact,
            r.limit.render() if r.limit else None, (r.position.line, r.position.column),
            [(p.line, p.column) for p in r.stack])


def diag(src):
    o = run(src)
    assert o.kind == "compile-error", (o.kind, o.value, o.record, o.message)
    p = o.diagnostic.position
    return o.diagnostic.code, (p.line, p.column)


SLICE = """I64 first_last[n](in I64[n] xs) {
    return xs[0] * 10 + xs[n - 1];
}

export I64 run() {
    I64[10] a;
    for i in 0..10 {
        a[i] = i;
    }
    return first_last(a[%s]);
}
"""


class Ranks(unittest.TestCase):
    """Arrays of rank 2 to 4 and partial indexing (SPEC-04 LS-64, LS-65; ruling R1)."""

    def test_rank_two_elements(self):
        src = ("export I64 run() {\n    I64[2, 3] m;\n    m[1, 2] = 5;\n    m[0, 1] = 3;\n"
               "    return m[1, 2] * 10 + m[0, 1];\n}\n")
        self.assertEqual(value(src), "I64 53")

    def test_rank_four_extent_and_size(self):
        src = ("export I64 run() {\n    I64[2, 3, 4, 5] t;\n    t[1, 2, 3, 4] = 9;\n"
               "    return t[1, 2, 3, 4] * 1000 + extent(t, 2) * 100 + size(t);\n}\n")
        self.assertEqual(value(src), "I64 9520")

    def test_index_record_names_the_lowest_failing_dimension(self):
        # R1: rank 2 and above record the dimension, then the index; limit the extent.
        src = ("export I64 run() {\n    I64[2, 3] m;\n    I64 i = 2;\n    I64 j = 3;\n"
               "    return m[i, j];\n}\n")
        self.assertEqual(fault(src), ("E_BOUNDS", "index.checked.i64", ["I64 0", "I64 2"], None, "I64 2",
                                      at(src, "[i"), []))
        src = src.replace("m[i, j]", "m[1, j]")
        self.assertEqual(fault(src)[1:5], ("index.checked.i64", ["I64 1", "I64 3"], None, "I64 3"))

    def test_every_index_is_evaluated_before_any_check(self):
        # R1: m[5, ...] is out of bounds, but the division in the second index runs first.
        src = ("export I64 run() {\n    I64[2, 3] m;\n    I64 z = 0;\n    return m[5, 1 / z];\n}\n")
        self.assertEqual(fault(src)[0], "E_DIV_ZERO")

    def test_rank_one_record_is_unchanged(self):
        src = "export I64 run() {\n    I64[3] a;\n    I64 i = 3;\n    return a[i];\n}\n"
        self.assertEqual(fault(src)[:5], ("E_BOUNDS", "index.checked.i64", ["I64 3"], None, "I64 3"))

    def test_partial_index_is_a_view_of_one_row(self):
        src = ("export I64 run() {\n    I64[2, 3] m;\n    m[1, 0] = 4;\n    m[1, 2] = 6;\n"
               "    return sum(m[1]);\n}\n")
        self.assertEqual(value(src), "I64 10")
        src = src.replace("sum(m[1])", "sum(m[2])")
        self.assertEqual(fault(src)[:6], ("E_BOUNDS", "index.checked.i64", ["I64 0", "I64 2"], None, "I64 2",
                                          at(src, "[2]")))

    def test_array_literal_of_rank_two(self):
        src = ("export I64 run() {\n    I64[2, 3] m = [[1, 2, 3], [4, 5, 6]];\n"
               "    return m[1, 0] * 10 + m[0, 2];\n}\n")
        self.assertEqual(value(src), "I64 43")

    def test_negative_extent_record_names_the_dimension(self):
        # R10: every extent is evaluated, then the first negative one faults at the declared name.
        src = ("I64 g[n](in I64[n] xs) {\n    I64[n, n - 3] tmp;\n    return n;\n}\n\n"
               "export I64 run() {\n    I64[2] a;\n    return g(a);\n}\n")
        self.assertEqual(fault(src), ("E_SHAPE", "decl.shape", ["I64 1", "I64 -1"], None, "I64 0",
                                      at(src, "tmp"), [at(src, "g(a)")]))


class Slices(unittest.TestCase):
    """Slices (SPEC-04 LS-161, LS-162; rulings R2 and R3)."""

    def test_slice_forms(self):
        for item, want in (("2..5", 24), ("2..=5", 25), ("..3", 2), ("8..", 89), ("1.. by 3", 17),
                           (".. by -1", 90), ("7..=2 by -1", 72), ("7..2 by -2", 73), ("..", 9)):
            with self.subTest(item=item):
                self.assertEqual(value(SLICE % item), "I64 %d" % want)

    def test_empty_slices_are_valid(self):
        # R3: a[3..=2] is a[3..3]; a[len(a)..] is empty (LS-162).
        src = ("export I64 run() {\n    I64[10] a;\n"
               "    return len(a[3..=2]) + len(a[10..]) + len(a[2..2 by -1]) + len(a[2..=3 by -1]);\n}\n")
        self.assertEqual(value(src), "I64 0")

    def test_slice_record(self):
        # R2: E_BOUNDS slice.checked.<E>, the bounds as written once omitted ones are filled,
        # limit the extent, at the `[`.
        for item, operands in (("2..11", ["I64 2", "I64 11"]), ("5..3", ["I64 5", "I64 3"]),
                               ("-1..3", ["I64 -1", "I64 3"]), ("11..", ["I64 11", "I64 10"]),
                               ("3..=10", ["I64 3", "I64 10"]),
                               ("0..=9223372036854775807", ["I64 0", "I64 9223372036854775807"]),
                               ("10..=2 by -1", ["I64 10", "I64 2"]), ("4..5 by -1", ["I64 4", "I64 5"])):
            with self.subTest(item=item):
                src = SLICE % item
                self.assertEqual(fault(src)[:6], ("E_BOUNDS", "slice.checked.i64", operands, None, "I64 10",
                                                  at(src, "[" + item)))

    def test_slice_record_of_rank_two_names_the_dimension(self):
        src = "export I64 run() {\n    I64[2, 3] m;\n    return sum(m[.., 1..4]);\n}\n"
        self.assertEqual(fault(src)[:5], ("E_BOUNDS", "slice.checked.i64", ["I64 1", "I64 1", "I64 4"],
                                          None, "I64 3"))

    def test_column_is_a_strided_view(self):
        src = ("export I64 run() {\n    I64[3, 4] m = [[0, 0, 1, 9], [0, 0, 2, 9], [0, 0, 3, 9]];\n"
               "    return sum(m[.., 2]);\n}\n")
        self.assertEqual(value(src), "I64 6")


class Views(unittest.TestCase):
    """Local views and the view functions (SPEC-04 LS-106, LS-145; ruling R4)."""

    def test_inout_view_writes_through(self):
        src = ("export I64 run() {\n    I64[4] a;\n    inout I64[_] w = a[1..3];\n    w[1] = 7;\n"
               "    return a[2];\n}\n")
        self.assertEqual(value(src), "I64 7")

    def test_transpose_reverse_reshape(self):
        src = ("export I64 run() {\n    I64[2, 3] m = [[1, 2, 3], [4, 5, 6]];\n"
               "    in I64[_, _] t = transpose(m);\n    in I64[_, _] r = reverse(m, 1);\n"
               "    I64[6] a = [1, 2, 3, 4, 5, 6];\n    in I64[_, _] s = reshape(a, 3, 2);\n"
               "    return t[2, 1] * 1000 + r[0, 0] * 100 + s[2, 0] * 10 + extent(t, 0);\n}\n")
        self.assertEqual(value(src), "I64 6353")

    def test_reshape_without_a_layout_is_refused(self):
        # LS-145 gives E_UNSUPPORTED (SPEC-02 V-11) but no record, so cint_ref refuses it.
        src = ("export I64 run() {\n    I64[2, 3] m;\n    in I64[_] s = reshape(transpose(m), 6);\n"
               "    return s[0];\n}\n")
        self.assertEqual(run(src).kind, "refused")

    def test_box09_names_are_built_ins(self):
        # R4 and R6: the four names join the prelude, so declaring one is C3001; parsed
        # without the box 09 surface they are ordinary names.
        src = "I64 dot(I64 x) {\n    return x;\n}\n\nexport I64 run() {\n    return dot(2);\n}\n"
        self.assertEqual(diag(src), ("C3001", at(src, "dot")))
        m = P.parse_module(src, "t.ci", box09=False)
        check_module(m)


class Reductions(unittest.TestCase):
    """The reductions and `dot` (SPEC-01 IM-77, IM-83, IM-87; rulings R5 to R7)."""

    def test_sum_is_exact_then_checked(self):
        src = "export I8 run() {\n    I8[3] a = [100, 100, -100];\n    return sum(a);\n}\n"
        self.assertEqual(value(src), "I8 100")
        src = "export I8 run() {\n    I8[3] a = [100, 100, -50];\n    return sum(a);\n}\n"
        self.assertEqual(fault(src)[:6], ("E_OVERFLOW", "sum.checked.i8.i8", ["I64 3"], 150, "I8 127",
                                          at(src, "sum(")))

    def test_fold_checked_record(self):
        src = "export I8 run() {\n    I8[3] a = [100, 100, -100];\n    return fold_checked(add, 0, a);\n}\n"
        self.assertEqual(fault(src)[:6], ("E_OVERFLOW", "fold_checked.add.i8", ["I64 1", "I8 100", "I8 100"],
                                          200, "I8 127", at(src, "fold_checked")))

    def test_empty_min_and_max(self):
        for op in ("min", "max"):
            with self.subTest(op=op):
                src = "export I64 run() {\n    I64[0] none;\n    return %s(none);\n}\n" % op
                self.assertEqual(fault(src)[:6], ("E_SHAPE", "reduce_%s.checked.i64" % op, ["I64 0"], None, None,
                                                  at(src, op + "(")))
                self.assertEqual(value(src.replace("%s(none)" % op, "%s(7, none)" % op)), "I64 7")

    def test_dot_is_exact(self):
        src = ("export I64 run() {\n    I64[2] a = [3037000500, -3037000500];\n"
               "    I64[2] b = [3037000500, 3037000500];\n    return dot(I64, a, b);\n}\n")
        self.assertEqual(value(src), "I64 0")

    def test_dot_shape_fault_precedes_the_charge(self):
        src = "export I64 run() {\n    I64[2] a;\n    I64[3] b;\n    return dot(I64, a, b);\n}\n"
        o = run(src)
        self.assertEqual(fault(src)[:6], ("E_SHAPE", "dot.checked.i64.i64", ["I64 0", "I64 3"], None, "I64 2",
                                          at(src, "dot")))
        self.assertEqual(o.fuel, 1)

    def test_reduction_fuel_is_one_unit_per_64_elements(self):
        # R7: ceil(n / 64), charged before the elements are read.
        for n, fuel in ((0, 1), (1, 2), (64, 2), (65, 3), (128, 3), (129, 4)):
            with self.subTest(n=n):
                o = run("export I64 run() {\n    I64[%d] a;\n    return sum(a);\n}\n" % n)
                self.assertEqual((o.kind, o.fuel), ("value", fuel))

    def test_copy_fill_and_assignment_charge_no_fuel(self):
        src = ("export I64 run() {\n    I64[100] a;\n    I64[100] b;\n    fill(a, 3);\n    copy(b, a);\n"
               "    a = b;\n    I64[100] c = a;\n    return c[99];\n}\n")
        o = run(src)
        self.assertEqual((o.value.render(), o.fuel), ("I64 3", 1))


class Copies(unittest.TestCase):
    """`copy` and array assignment between views (SPEC-04 LS-70; ruling R8)."""

    SHIFT = ("void shift[n](inout I64[n] x, I64 j, I64 k) {\n    %s\n}\n\n"
             "export I64 run() {\n    I64[8] x;\n    for i in 0..8 {\n        x[i] = i;\n    }\n"
             "    shift(x, %d, 4);\n    return x[0] * 10 + x[3];\n}\n")

    def test_overlap_faults_copy_alias(self):
        for stmt, pos in (("copy(x[0..k], x[j..j + k]);", "copy"), ("x[0..k] = x[j..j + k];", "= x")):
            with self.subTest(stmt=stmt):
                src = self.SHIFT % (stmt, 2)
                self.assertEqual(fault(src), ("E_ALIAS", "copy.alias", [], None, None, at(src, pos),
                                              [at(src, "shift(x")]))
                self.assertEqual(value(self.SHIFT % (stmt, 4)), "I64 47")

    def test_shape_is_checked_before_overlap(self):
        src = self.SHIFT % ("copy(x[0..k], x[j..j + k + 1]);", 2)
        self.assertEqual(fault(src)[:6], ("E_SHAPE", "copy.shape", ["I64 0", "I64 5"], None, "I64 4",
                                          at(src, "copy")))

    def test_copy_to_the_same_view_is_allowed(self):
        src = self.SHIFT % ("copy(x[j..j + k], x[j..j + k]);", 2)
        self.assertEqual(value(src), "I64 3")


KERNEL = """kernel inv[n](in I64[n] a, out I64[n] b, out I64 total) over [i: n] {
    I64 d = a[i];
    I64 r = 100 / d;
    b[i] = r;
    reduce total = sum(r);
}

export I64 run() {
    I64[4] a = [%s];
    I64[4] b;
    I64 t = 0;
    inv(a, b, t);
    return t * 1000 + b[3];
}
"""


class Kernels(unittest.TestCase):
    """Kernel dispatch (SPEC-02 K-1 to K-12, F-5, F-8, R-1; rulings R7 and R9)."""

    def test_dispatch_value_and_fuel(self):
        # R7: the entry charges 1 and the dispatch N, at the call; the body has no loop.
        o = run(KERNEL % "1, 2, 4, 5")
        self.assertEqual((o.kind, o.value.render(), o.fuel), ("value", "I64 195020", 5))

    def test_work_item_fault_address(self):
        # The least faulting work-item is 1; one counted operation (a[i]) precedes the division.
        src = KERNEL % "5, 0, 0, 5"
        o = run(src)
        self.assertEqual(fault(src), ("E_DIV_ZERO", "div.checked.i64", ["I64 100", "I64 0"], None, None,
                                      at(src, "/ d"), [at(src, "inv(a")]))
        a = o.record.address
        self.assertEqual((a.name, a.dispatch, a.phase, a.work_item, a.step), ("t.inv", 0, 1, 1, 1))
        self.assertEqual(o.fuel, 5)

    def test_outputs_are_not_published_after_a_fault(self):
        # Outputs are staged and published only when the whole dispatch succeeds (SPEC-02
        # F-6): work-item 3 faults, so the module-level `g` keeps its value.
        src = "I64 g = 7;\n\n" + (KERNEL % "5, 5, 5, 0").replace("inv(a, b, t);", "inv(a, b, g);")
        o = run(src)
        self.assertEqual((o.kind, o.record.address.work_item), ("fault", 3))
        self.assertEqual([(m, n, v.render()) for m, n, v in o.state], [("t", "g", "I64 7")])
        o = run(src.replace("5, 5, 5, 0", "5, 5, 5, 5"))
        self.assertEqual((o.kind, [v.render() for _, _, v in o.state]), ("value", ["I64 80"]))

    def test_epilogue_fault(self):
        # The sum of the outputs is checked once, after every work-item (R-1).
        src = KERNEL.replace("100 / d", "d")
        src = src % "9223372036854775807, 1, 0, 0"
        o = run(src)
        self.assertEqual(fault(src)[:6], ("E_OVERFLOW", "sum.checked.i64.i64", ["I64 4"],
                                          9223372036854775808, "I64 9223372036854775807",
                                          at(src, "sum(r")))
        a = o.record.address
        self.assertEqual((a.phase, a.work_item, a.step), (2, None, None))

    def test_dispatch_numbers_count_from_zero(self):
        src = (KERNEL % "1, 2, 4, 5").replace("    inv(a, b, t);\n", "    inv(a, b, t);\n    a[2] = 0;\n"
                                                    "    inv(a, b, t);\n")
        o = run(src)
        self.assertEqual((o.record.address.dispatch, o.record.address.work_item), (1, 2))

    def test_entry_alias_record_carries_the_descriptors(self):
        src = ("kernel step1[n](in I64[n] a, out I64[n] b) over [i: n] {\n    b[i] = a[i];\n}\n\n"
               "export I64 run() {\n    I64[8] x;\n    I64 j = 1;\n    step1(x[0..4], x[j..j + 4]);\n"
               "    return x[0];\n}\n")
        o = run(src)
        self.assertEqual(fault(src), ("E_ALIAS", "bind.alias", ["I64 1", "I64 0"], None, None,
                                      at(src, "step1(x"), []))
        self.assertEqual([d.render() for d in o.record.descriptors],
                         ["I64 none 1 1 4 0 1 write", "I64 none 1 0 4 0 1 write"])
        self.assertEqual((o.record.address.phase, o.fuel), (0, 1))
        lines = expect.render(expect.from_outcome(o, "k/alias", "test", fmt=4)).split("\n")
        self.assertEqual(lines[lines.index("fault.code E_ALIAS"):], [
            "fault.code E_ALIAS", "fault.operation bind.alias", "fault.operand I64 1", "fault.operand I64 0",
            "fault.descriptor I64 none 1 1 4 0 1 write", "fault.descriptor I64 none 1 0 4 0 1 write",
            "fault.exact none", "fault.limit none", "fault.position t.ci:%d:%d" % at(src, "step1(x"),
            "fault.revision self", "fault.source-map self", "fault.phase entry", "fault.kernel t.step1",
            "fault.address 0 none none", "fault.stack-depth 0", ""])

    def test_shape_binding_record(self):
        src = ("kernel add1[n](in I64[n] a, out I64[n] b) over [i: n] {\n    b[i] = a[i] + 1;\n}\n\n"
               "export I64 run() {\n    I64[3] a;\n    I64[4] b;\n    I64 k = 4;\n"
               "    add1(a, b[0..k]);\n    return b[0];\n}\n")
        self.assertEqual(fault(src)[:6], ("E_SHAPE", "bind.shape", ["I64 1", "I64 0", "I64 4"], None, "I64 3",
                                          at(src, "add1(a")))

    def test_where_record(self):
        # OQ-203: the first false constraint in written order, after the shape binding: its
        # index from 0 and the values of its two sides, with no exact and no limit, at the
        # dispatch site with the caller's stack, before the dispatch charge.
        src = ("kernel two[n, m](in I64[n] a, out I64[m] b) over [i: m] where n >= 1, m >= n + 1 {\n"
               "    b[i] = a[0];\n}\n\nexport I64 run() {\n    I64[3] a;\n    I64[3] b;\n    two(a, b);\n"
               "    return b[0];\n}\n")
        o = run(src)
        self.assertEqual(fault(src), ("E_SHAPE", "bind.where", ["I64 1", "I64 3", "I64 4"], None, None,
                                      at(src, "two(a"), []))
        self.assertEqual((o.record.address.phase, o.record.address.work_item, o.fuel), (0, None, 1))
        self.assertEqual(value(src.replace("I64[3] b", "I64[4] b")), "I64 0")


class Format4(unittest.TestCase):
    """`.expect` format 4 (SPEC-09 CONF-11 rule 13; ruling R9)."""

    def test_descriptor_rendering(self):
        d = Descriptor("I32", 8, (8, 8), (16, 1), True)
        self.assertEqual(d.render(), "I32 none 2 8 8 0 16 8 0 1 write")
        self.assertEqual(Descriptor("U8", 0, (3,), (-1,), False).render(), "U8 none 1 0 3 0 -1 read")

    def test_a_kernel_fault_needs_format_4(self):
        o = run(KERNEL % "5, 0, 0, 5")
        with self.assertRaises(ValueError):
            expect.from_outcome(o, "k/x", "test", fmt=2)
        text = expect.render(expect.from_outcome(o, "k/x", "test", fmt=4))
        parsed = expect.parse(text)
        self.assertEqual(parsed.format, 4)
        self.assertIn("fault.address 0 1 1\n", text)
        self.assertIn("fault.phase work-item\nfault.kernel t.inv\n", text)

    def test_format_4_lines_only_in_format_4(self):
        o = run(KERNEL % "5, 0, 0, 5")
        text = expect.render(expect.from_outcome(o, "k/x", "test", fmt=4))
        with self.assertRaises(ValueError):
            expect.parse(text.replace("format 4\n", "format 2\n"))
        with self.assertRaises(ValueError):
            expect.parse(text.replace("fault.phase work-item\n", "fault.phase entry\n"))

    def test_a_sequential_fault_in_format_4_keeps_the_format_2_form(self):
        o = run("export I64 run() {\n    I64[3] a;\n    I64 i = 3;\n    return a[i];\n}\n")
        two = expect.render(expect.from_outcome(o, "k/x", "test", fmt=2))
        four = expect.render(expect.from_outcome(o, "k/x", "test", fmt=4))
        self.assertEqual(four, two.replace("format 2\n", "format 4\n"))
        self.assertEqual(expect.parse(four).format, 4)


if __name__ == "__main__":
    unittest.main()
