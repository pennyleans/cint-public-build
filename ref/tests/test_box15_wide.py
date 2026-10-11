import os, sys; sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # put ref/ on the path
"""Tests for box 15 unit 1, the signed wide half (docs/design/notes/2026-10-05-box15-wide-integers-
fixed-point.md, sections 4.2, 4.3 and 5.1; defaults BX15-06 and BX15-07): `I128` to `I1024` as
elements of local and module-level arrays, views, slices, struct fields, arenas and pools, kernel
parameters and reductions, and in the view descriptors of `.expect` format 4. The cases under
conformance/wide/ state whole programs; these tests pin the rules one at a time."""
import unittest

from cint_ref import expect
from cint_ref.exec import run_program
from cint_ref.faults import Descriptor
from cint_ref.types import INT_TYPES, WIDE_INT_NAMES

WIDTHS = {name: INT_TYPES[name] for name in WIDE_INT_NAMES}


def run(src, entry="run", fuel=None):
    return run_program(src.encode("utf-8"), "t.ci", entry, fuel)


def value(src, **kw):
    o = run(src, **kw)
    assert o.kind == "value", (o.kind, o.diagnostic, o.record, o.message)
    return o.value


def fault(src, **kw):
    o = run(src, **kw)
    assert o.kind == "fault", (o.kind, o.diagnostic, o.value, o.message)
    return o.record


class WideArrays(unittest.TestCase):
    def test_every_width_holds_its_extremes(self):
        for name, t in WIDTHS.items():
            with self.subTest(type=name):
                v = value("export %s run() {\n    %s[3] a = [%s.max, %s.min, 0];\n    a[2] = a[0] / 2;\n"
                          "    return a[1] + a[2];\n}\n" % (name, name, name, name))
                self.assertEqual((v.type, v.value), (name, t.min + t.max // 2))

    def test_index_fault_names_the_element_type(self):
        for name in WIDTHS:
            with self.subTest(type=name):
                r = fault("export %s run() {\n    %s[2] a;\n    I64 k = 2;\n    return a[k];\n}\n" % (name, name))
                self.assertEqual((r.code, r.operation), ("E_BOUNDS", "index.checked.%s" % name.lower()))
                self.assertEqual([v.render() for v in r.operands], ["I64 2"])
                self.assertEqual(r.limit.render(), "I64 2")

    def test_slice_fault_names_the_element_type(self):
        r = fault("export I512 run() {\n    I512[6] a;\n    I64 k = 7;\n    return sum(a[1..k]);\n}\n")
        self.assertEqual((r.code, r.operation), ("E_BOUNDS", "slice.checked.i512"))
        self.assertEqual([v.render() for v in r.operands], ["I64 1", "I64 7"])

    def test_rank_two_partial_index_and_reversed_slice(self):
        v = value("export I1024 run() {\n    I1024[2, 3] g;\n    for i in 0..2 {\n        for j in 0..3 {\n"
                  "            g[i, j] = ((i * 3 + j) as I1024) << 1000;\n        }\n    }\n"
                  "    return sum(g[1, 2..=0 by -2]) >> 1000;\n}\n")
        self.assertEqual((v.type, v.value), ("I1024", 8))

    def test_views_as_parameters(self):
        v = value("I256 total[n](in I256[n] xs) {\n    I256 s = 0;\n    for i in 0..n {\n        s = s + xs[i];\n"
                  "    }\n    return s;\n}\n\nvoid neg(inout I256[_] v) {\n    for i in 0..extent(v, 0) {\n"
                  "        v[i] = -v[i];\n    }\n}\n\nexport I256 run() {\n    I256[3] a = [I256.max, 1, 2];\n"
                  "    neg(a);\n    return total(a[1..]);\n}\n")
        self.assertEqual((v.type, v.value), ("I256", -3))

    def test_copy_fill_reshape_transpose(self):
        v = value("export I128 run() {\n    I128[4] a = [1, 2, 3, I128.max];\n    I128[4] b;\n    copy(b, a);\n"
                  "    fill(a, I128.min);\n    I128[2, 2] m = reshape(b, 2, 2);\n"
                  "    return transpose(m)[1, 1] + a[0];\n}\n")
        self.assertEqual((v.type, v.value), ("I128", -1))

    def test_struct_array_field_and_array_of_structs(self):
        v = value("struct W {\n    I128[2] v;\n    I512 k;\n}\n\nexport I512 run() {\n    W[2] ws;\n"
                  "    ws[1].v[1] = I128.min;\n    ws[0].k = 5;\n    W c = ws[1];\n    ws[1].v[1] = 0;\n"
                  "    return (c.v[1] as I512) + ws[0].k;\n}\n")
        self.assertEqual((v.type, v.value), ("I512", -(1 << 127) + 5))

    def test_module_array_has_no_state_line(self):
        o = run("I1024[2] g;\nI1024 s = 1;\n\nexport I1024 run() {\n    g[0] = I1024.max;\n    s = g[0];\n"
                "    return s;\n}\n")
        self.assertEqual(o.kind, "value")
        text = expect.render(expect.from_outcome(o, "t", "c", fmt=2))
        lines = [line for line in text.split("\n") if line.startswith("state.global ")]
        self.assertEqual(lines, ["state.global t s I1024 %d" % WIDTHS["I1024"].max])


class WideReductions(unittest.TestCase):
    def test_sum_is_exact_past_the_element_range(self):
        v = value("export I1024 run() {\n    I1024[3] a = [I1024.max, I1024.max, I1024.min];\n    return sum(a);\n}\n")
        self.assertEqual(v.value, WIDTHS["I1024"].max - 1)

    def test_sum_into_a_narrower_result_records_the_exact_total(self):
        r = fault("export I128 run() {\n    I1024[2] a = [I1024.max, 1];\n    return sum(I128, a);\n}\n")
        self.assertEqual((r.code, r.operation), ("E_OVERFLOW", "sum.checked.i1024.i128"))
        self.assertEqual([v.render() for v in r.operands], ["I64 2"])
        self.assertEqual(r.exact, 1 << 1023)
        self.assertEqual(r.limit.render(), "I128 %d" % WIDTHS["I128"].max)

    def test_fold_checked_record_carries_wide_operands(self):
        r = fault("export I256 run() {\n    I256[3] a = [I256.min, -1, 0];\n    return fold_checked(add, 0, a);\n}\n")
        self.assertEqual((r.code, r.operation), ("E_OVERFLOW", "fold_checked.add.i256"))
        self.assertEqual([v.render() for v in r.operands],
                         ["I64 1", "I256 %d" % WIDTHS["I256"].min, "I256 -1"])
        self.assertEqual(r.exact, WIDTHS["I256"].min - 1)

    def test_sum_wrap_sum_sat_min_max(self):
        o = run("export I64 run() {\n    I128[3] a = [I128.max, I128.max, -5];\n"
                "    \"{sum_wrap(a)} {sum_sat(a)} {min(a)} {max(I128.min, a[2..])}\\n\";\n    return 0;\n}\n")
        self.assertEqual(o.kind, "value")
        m = WIDTHS["I128"].max
        self.assertEqual(o.stdout, ("%d %d %d %d\n" % (WIDTHS["I128"].wrap(2 * m - 5), m - 5, -5, -5)).encode())

    def test_min_of_an_empty_view_faults(self):
        r = fault("export I512 run() {\n    I512[2] a;\n    return min(a[1..1]);\n}\n")
        self.assertEqual((r.code, r.operation), ("E_SHAPE", "reduce_min.checked.i512"))

    def test_dot_up_to_512_bits(self):
        v = value("export I1024 run() {\n    I512[2] a = [I512.max, I512.min];\n    I512[2] b = [I512.max, I512.min];\n"
                  "    return dot(I1024, a, b);\n}\n")
        t = WIDTHS["I512"]
        self.assertEqual(v.value, t.max * t.max + t.min * t.min)

    def test_dot_over_i1024_is_refused(self):
        # BX15-07: an I1024 dot can exceed the run-time record bound of IM-108.
        o = run("export I1024 run() {\n    I1024[2] a = [1, 2];\n    return dot(I1024, a, a);\n}\n")
        self.assertEqual(o.kind, "refused")
        self.assertIn("IM-108", o.message)

    def test_kernel_reduce_and_work_item_fault(self):
        v = value("kernel sq[n](in I512[n] a, out I512 t) over [i: n] {\n    reduce t = sum(a[i] * a[i]);\n}\n\n"
                  "export I512 run() {\n    I512[3] x = [(1 as I512) << 255, 1, -1];\n    I512 t = 0;\n    sq(x, t);\n"
                  "    return t;\n}\n")
        self.assertEqual(v.value, (1 << 510) + 2)
        r = fault("kernel sq[n](in I128[n] a, out I128[n] b) over [i: n] {\n    b[i] = a[i] * a[i];\n}\n\n"
                  "export I128 run() {\n    I128[2] x = [2, I128.min];\n    I128[2] y;\n    sq(x, y);\n"
                  "    return y[0];\n}\n")
        self.assertEqual((r.operation, r.address.phase, r.address.work_item), ("mul.checked.i128", 1, 1))


class WideMemory(unittest.TestCase):
    def test_arena_and_pool_of_wide_elements(self):
        v = value("Arena(I256, 4) scratch;\nPool(I1024, 2) cells;\n\nexport I1024 run() {\n"
                  "    Handle(I256[]) h = scratch.alloc(3);\n    scratch[h][2] = I256.max;\n"
                  "    Handle(I1024) c = cells.insert(I1024.min);\n"
                  "    return cells[c] + (sum(scratch[h]) as I1024);\n}\n")
        self.assertEqual(v.value, WIDTHS["I1024"].min + WIDTHS["I256"].max)


class WideDescriptors(unittest.TestCase):
    def test_entry_alias_record_has_wide_descriptors(self):
        r = fault("kernel bump[h, w](in I1024[h, w] u, out I1024[h, w] v) over [y: h, x: w] {\n"
                  "    v[y, x] = u[y, x];\n}\n\nexport I64 run() {\n    I1024[2, 4] m;\n"
                  "    bump(m[.., 0..2], m[.., 2..4]);\n    return 0;\n}\n")
        self.assertEqual((r.code, r.address.phase), ("E_ALIAS", 0))
        self.assertEqual([d.render() for d in r.descriptors],
                         ["I1024 none 2 2 2 0 4 2 0 1 write", "I1024 none 2 0 2 0 4 2 0 1 write"])

    def test_render_of_every_wide_element_type(self):
        for name in WIDTHS:
            with self.subTest(type=name):
                d = Descriptor(name, 3, (2,), (-1,), False)
                self.assertEqual(d.render(), "%s none 1 3 2 0 -1 read" % name)


if __name__ == "__main__":
    unittest.main()
