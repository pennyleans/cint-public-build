import os, sys; sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
"""Checks constructor array-field copy.shape ordering (SPEC-01 IM-187)."""
import tempfile
import unittest
from pathlib import Path

from cint_ref.exec import run_program
from cint_ref.types import Value


def run(source, fuel=None):
    return run_program(source.encode('ascii'), 'ctor.ci', 'run', fuel=fuel)


class ConstructorShape(unittest.TestCase):
    def assert_shape(self, result, source, destination, line, column, fuel=2, path='ctor.ci'):
        self.assertEqual(result.kind, 'fault', result.message or result.diagnostic or result.value)
        fault = result.record
        self.assertEqual((fault.code, fault.operation), ('E_SHAPE', 'copy.shape'))
        self.assertEqual([(v.type, v.value) for v in fault.operands], [('I64', 0), ('I64', source)])
        self.assertEqual((fault.limit.type, fault.limit.value), ('I64', destination))
        self.assertIsNone(fault.exact)
        self.assertEqual((fault.position.path, fault.position.line, fault.position.column),
                         (path, line, column))
        self.assertEqual(result.fuel, fuel)
        self.assertIsNone(fault.address)
        return fault

    def test_dynamic_source_count_checks_declared_field_extent(self):
        for count in (1, 3):
            with self.subTest(count=count):
                source = ('struct R { I64[2] data; }\n'
                          'I64 take(in I64[_] a) { R r = R(a); return len(r.data); }\n'
                          f'export I64 run() {{ I64[{count}] a; return take(a); }}\n')
                fault = self.assert_shape(run(source), count, 2, 2, 31)
                self.assertEqual(len(fault.stack), 1)
                self.assertEqual(fault.stack[0].line, 3)

    def test_named_fields_check_declaration_order_after_written_order(self):
        source = ('I64 hit = 0;\nstruct R { I64[2] left; I64[4] right; I64 mark; }\n'
                  'Bool choose(I64 digit) { hit = hit * 10 + digit; return true; }\n'
                  'I64 marker() { hit = hit * 10 + 3; return 0; }\n'
                  'void take(in I64[_] a, in I64[_] b) {\n'
                  '    R r = R(right = choose(1) ? b : b, left = choose(2) ? a : a, mark = marker());\n}\n'
                  'export I64 run() { I64[1] a; I64[3] b; take(a, b); return 0; }\n')
        result = run(source)
        self.assert_shape(result, 1, 2, 6, 11, fuel=5)
        self.assertEqual(result.globals['hit'], 123)

    def test_later_field_shape_is_checked_after_equal_earlier_field(self):
        source = ('struct R { I64[2] left; I64[4] right; }\n'
                  'void take(in I64[_] a, in I64[_] b) {\n    R r = R(a, b);\n}\n'
                  'export I64 run() { I64[2] a; I64[3] b; take(a, b); return 0; }\n')
        self.assert_shape(run(source), 3, 4, 3, 11)

    def test_later_argument_fault_precedes_shape_and_keeps_earlier_effects(self):
        source = ('I64 hit = 0;\nstruct R { I64[2] data; I64 mark; }\n'
                  'I64 fail() { hit += 1; I64 zero = 0; return 1 / zero; }\n'
                  'void take(in I64[_] a) { R r = R(a, fail()); }\n'
                  'export I64 run() { I64[1] a; take(a); return 0; }\n')
        result = run(source)
        self.assertEqual(result.kind, 'fault')
        self.assertEqual(result.record.operation, 'div.checked.i64')
        self.assertEqual(result.globals['hit'], 1)
        self.assertEqual(result.fuel, 3)

    def test_source_bounds_fault_precedes_shape(self):
        source = ('struct S { I64[2] data; }\nstruct R { I64[2] data; I64 mark; }\n'
                  'I64 hit = 0;\nI64 later() { hit += 1; return 0; }\n'
                  'void take(in S[_] src, I64 index) { R r = R(src[index].data, later()); }\n'
                  'export I64 run() { S[1] src; take(src, 1); return 0; }\n')
        result = run(source)
        self.assertEqual(result.kind, 'fault')
        self.assertEqual(result.record.operation, 'index.checked.struct')
        self.assertEqual(result.globals['hit'], 0)
        self.assertEqual(result.fuel, 2)

    def test_assignment_destination_is_unchanged_on_constructor_shape_fault(self):
        source = ('struct R { I64[2] data; I64 mark; }\nR target;\nI64 hit = 0;\n'
                  'I64 later() { hit += 1; target.mark += 1; return 99; }\n'
                  'void take(in I64[_] a) {\n    target = R(a, later());\n}\n'
                  'export I64 run() { I64[1] a; target.data[0] = 7; target.data[1] = 8; '
                  'target.mark = 3; take(a); return 0; }\n')
        result = run(source)
        self.assert_shape(result, 1, 2, 6, 14, fuel=3)
        self.assertEqual(result.globals['target'].fields['data'].values(), [7, 8])
        self.assertEqual(result.globals['target'].fields['mark'], 4)
        self.assertEqual(result.globals['hit'], 1)

    def test_assignment_place_bounds_precede_constructor_arguments(self):
        source = ('struct R { I64[2] data; I64 mark; }\nR[1] target;\nI64 hit = 0;\n'
                  'I64 later() { hit += 1; return 0; }\n'
                  'void take(in I64[_] a, I64 index) { target[index] = R(a, later()); }\n'
                  'export I64 run() { I64[1] a; take(a, 1); return 0; }\n')
        result = run(source)
        self.assertEqual(result.kind, 'fault')
        self.assertEqual(result.record.operation, 'index.checked.struct')
        self.assertEqual(result.globals['hit'], 0)
        self.assertEqual(result.fuel, 2)

    def test_shape_failure_charges_no_constructor_copy_fuel(self):
        source = ('struct R { I64[65] data; }\n'
                  'void take(in I64[_] a) {\n    R r = R(a);\n}\n'
                  'export I64 run() { I64[66] a; take(a); return 0; }\n')
        self.assert_shape(run(source, fuel=2), 66, 65, 3, 11)

    def test_zero_byte_elements_use_logical_counts(self):
        source = ('struct Empty {}\nstruct R { Empty[2] data; }\n'
                  'void take(in Empty[_] a) {\n    R r = R(a);\n}\n'
                  'export I64 run() { Empty[3] a; take(a); return 0; }\n')
        self.assert_shape(run(source), 3, 2, 4, 11)

    def test_zero_source_and_destination_counts(self):
        for destination, count in [(0, 1), (1, 0)]:
            with self.subTest(destination=destination, count=count):
                source = (f'struct R {{ U8[{destination}] data; }}\n'
                          'void take(in U8[_] a) {\n    R r = R(a);\n}\n'
                          f'export I64 run() {{ U8[{count}] a; take(a); return 0; }}\n')
                self.assert_shape(run(source), count, destination, 3, 11)

    def test_equal_shapes_copy_and_retain_later_argument_writes(self):
        source = ('struct R { I64[2] data; I64 mark; }\n'
                  'I64 later(inout I64[_] a) { a[0] = 9; return 3; }\n'
                  'export I64 run() { I64[2] a; a[0] = 7; R r = R(a, later(a)); '
                  'a[0] = 4; r.data[1] = 6; return r.data[0] * 100 + a[0] * 10 + a[1] + r.mark; }\n')
        result = run(source)
        self.assertEqual(result.kind, 'value', result.message or result.diagnostic)
        self.assertEqual(result.value.value, 943)
        self.assertEqual(result.fuel, 2)

    def test_equal_record_and_empty_arrays_copy(self):
        sources = [
            ('struct S { I64 value; }\nstruct R { S[1] data; }\n'
             'R make(in S[_] a) { return R(a); }\n'
             'export I64 run() { S[1] a; a[0].value = 7; R r = make(a); '
             'a[0].value = 9; return r.data[0].value; }\n', 7),
            ('struct Empty {}\nstruct R { Empty[0] data; }\n'
             'R make(in Empty[_] a) { return R(a); }\n'
             'export I64 run() { Empty[0] a; R r = make(a); return len(r.data); }\n', 0),
        ]
        for source, expected in sources:
            with self.subTest(expected=expected):
                result = run(source)
                self.assertEqual(result.kind, 'value', result.message or result.diagnostic)
                self.assertEqual(result.value.value, expected)
                self.assertEqual(result.fuel, 2)

    def test_qualified_constructor_uses_member_callee_position(self):
        source = ('import records as alias;\n'
                  'void take(in I64[_] a) {\n    alias.R r = alias.R(a);\n}\n'
                  'export I64 run() { I64[3] a; take(a); return 0; }\n')
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'records.ci').write_text('export struct R { I64[2] data; }\n')
            (root / 'main.ci').write_text(source)
            result = run_program(source.encode('ascii'), 'main.ci', 'run', root=tmp)
        self.assert_shape(result, 3, 2, 3, 23, path='main.ci')

    def test_static_diagnostics_are_unchanged(self):
        for arguments, arrays, code in [
                ('a', 'I64[1] a;', 'C2012'),
                ('a', 'U8[2] a;', 'C2001'),
                ('', '', 'C2020'),
                ('data = a, data = a', 'I64[2] a;', 'C2062')]:
            with self.subTest(arguments=arguments, code=code):
                source = ('struct R { I64[2] data; }\n'
                          f'export I64 run() {{ {arrays} R r = R({arguments}); return 0; }}\n')
                result = run(source)
                self.assertEqual(result.kind, 'compile-error')
                self.assertEqual(result.diagnostic.code, code)

    def test_rank_two_field(self):
        # Box 09 admits fields of rank 2 to 4; the shape check compares each dimension.
        source = ('struct R { I64[2, 3] data; }\n'
                  'export I64 run() { I64[2, 3] m; m[1, 2] = 5; R r = R(m); return r.data[1, 2]; }\n')
        result = run(source)
        self.assertEqual((result.kind, result.value), ('value', Value('I64', 5)))


if __name__ == '__main__':
    unittest.main()
