import os, sys; sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
"""Checks the direct 2026-10-04 copy.shape ruling, SPEC-01 IM-187."""
import unittest

from cint_ref.exec import run_program
from cint_ref.faults import RUN_TIME, decode_fault_record, encode_fault_record
from cint_ref.types import Value


def run(source, fuel=None):
    return run_program(source.encode('ascii'), 'copy.ci', 'run', fuel=fuel)


def assignment_source(elem='I64', dst=2, src=3, prefix=''):
    return (prefix + f'void move(inout {elem}[_] dst, in {elem}[_] src) {{\n'
            '    dst = src;\n}\n'
            f'export I64 run() {{ {elem}[{dst}] dst; {elem}[{src}] src; move(dst, src); return 0; }}\n')


class CopyShape(unittest.TestCase):
    def assert_copy_fault(self, outcome, destination, source, line, column, fuel=2):
        self.assertEqual(outcome.kind, 'fault', outcome.message or outcome.diagnostic)
        fault = outcome.record
        self.assertEqual((fault.code, fault.operation), ('E_SHAPE', 'copy.shape'))
        self.assertEqual([(v.type, v.value) for v in fault.operands], [('I64', 0), ('I64', source)])
        self.assertEqual((fault.limit.type, fault.limit.value), ('I64', destination))
        self.assertIsNone(fault.exact)
        self.assertEqual((fault.position.path, fault.position.line, fault.position.column),
                         ('copy.ci', line, column))
        self.assertEqual(outcome.fuel, fuel)
        self.assertIsNone(fault.revision)
        self.assertIsNone(fault.source_map)
        self.assertIsNone(fault.address)
        return fault

    def test_assignment_runtime_shape_record(self):
        source = assignment_source()
        fault = self.assert_copy_fault(run(source), 2, 3, 2, 9)
        self.assertEqual(len(fault.stack), 1)
        self.assertEqual(fault.stack[0].line, 4)

    def test_assignment_empty_counts_and_bool(self):
        for elem, destination, source in [('I64', 0, 1), ('I64', 2, 0), ('Bool', 1, 2)]:
            with self.subTest(element=elem, destination=destination, source=source):
                self.assert_copy_fault(run(assignment_source(elem, destination, source)),
                                       destination, source, 2, 9)

    def test_zero_byte_record_counts_are_logical_elements(self):
        source = assignment_source('Empty', 2, 3, 'struct Empty {}\n')
        self.assert_copy_fault(run(source), 2, 3, 3, 9)

    def test_assignment_does_not_write_any_element(self):
        source = ('I64[2] dst;\nI64[3] src;\n'
                  'void move(inout I64[_] target, in I64[_] value) {\n'
                  '    target = value;\n}\n'
                  'export I64 run() { dst[0] = 7; dst[1] = 8; src[0] = 91; move(dst, src); return 0; }\n')
        result = run(source)
        self.assert_copy_fault(result, 2, 3, 4, 12)
        self.assertEqual(result.globals['dst'].values(), [7, 8])
        self.assertEqual(result.globals['src'].values(), [91, 0, 0])

    def test_place_and_source_effects_finish_before_shape(self):
        source = ('I64 hit = 0;\nstruct D { I64[2] data; }\nD[1] dst;\n'
                  'I64 index() { hit = hit * 10 + 1; return 0; }\n'
                  'Bool choose() { hit = hit * 10 + 2; return true; }\n'
                  'void move(in I64[_] src) {\n    dst[index()].data = choose() ? src : src;\n}\n'
                  'export I64 run() { I64[3] src; dst[0].data[0] = 7; move(src); return 0; }\n')
        result = run(source)
        self.assert_copy_fault(result, 2, 3, 7, 23, fuel=4)
        self.assertEqual(result.globals['hit'], 12)
        self.assertEqual(result.globals['dst'].values()[0].fields['data'].values(), [7, 0])

    def test_destination_bounds_fault_prevents_source_effects(self):
        source = ('I64 hit = 0;\nstruct D { I64[2] data; }\nD[1] dst;\n'
                  'I64 index() { hit += 1; return 1; }\n'
                  'D source() { hit += 100; D s; return s; }\n'
                  'export I64 run() { dst[index()].data = source().data; return 0; }\n')
        result = run(source)
        self.assertEqual(result.kind, 'fault')
        self.assertEqual(result.record.operation, 'index.checked.struct')
        self.assertEqual(result.globals['hit'], 1)
        self.assertEqual(result.globals['dst'].values()[0].fields['data'].values(), [0, 0])
        self.assertEqual(result.fuel, 2)

    def test_source_fault_precedes_shape_check(self):
        source = ('I64 hit = 0;\nstruct S { I64[3] data; }\n'
                  'S source() { hit += 1; I64 zero = 0; I64 bad = 1 / zero; S s; return s; }\n'
                  'void move(inout I64[_] dst) { dst = source().data; }\n'
                  'export I64 run() { I64[2] dst; move(dst); return 0; }\n')
        result = run(source)
        self.assertEqual(result.kind, 'fault')
        self.assertEqual(result.record.operation, 'div.checked.i64')
        self.assertEqual(result.globals['hit'], 1)
        self.assertEqual(result.fuel, 3)

    def test_mismatch_adds_no_copy_fuel(self):
        self.assert_copy_fault(run(assignment_source(dst=65, src=66), fuel=2), 65, 66, 2, 9)

    def test_fixed_initializer_checks_dynamic_source_shape(self):
        source = ('void move(in I64[_] src) {\n    I64[2] dst = src;\n}\n'
                  'export I64 run() { I64[3] src; move(src); return 0; }\n')
        self.assert_copy_fault(run(source), 2, 3, 2, 16)

    def test_dynamic_initializer_shape(self):
        source = ('void move[n](in I64[n] src) {\n    I64[n + 1] dst = src;\n}\n'
                  'export I64 run() { I64[3] src; move(src); return 0; }\n')
        self.assert_copy_fault(run(source, fuel=2), 4, 3, 2, 20)

    def test_zero_byte_record_initializer_shape(self):
        source = ('struct Empty {}\nvoid move[n](in Empty[n] src) {\n'
                  '    Empty[n + 1] dst = src;\n}\n'
                  'export I64 run() { Empty[2] src; move(src); return 0; }\n')
        self.assert_copy_fault(run(source), 3, 2, 3, 22)

    def test_initializer_source_effects_remain_after_fault(self):
        source = ('I64 hit = 0;\nstruct S { I64[3] data; }\n'
                  'S source() { hit += 1; S s; return s; }\n'
                  'void move[n](in I64[n] shape) {\n    I64[n] dst = source().data;\n}\n'
                  'export I64 run() { I64[2] shape; move(shape); return 0; }\n')
        result = run(source)
        self.assert_copy_fault(result, 2, 3, 5, 16, fuel=3)
        self.assertEqual(result.globals['hit'], 1)

    def test_initializer_extent_fault_precedes_source(self):
        source = ('I64 hit = 0;\nstruct S { I64[3] data; }\n'
                  'S source() { hit += 1; S s; return s; }\n'
                  'void move[n](in I64[n] shape) { I64[1 / n] dst = source().data; }\n'
                  'export I64 run() { I64[0] shape; move(shape); return 0; }\n')
        result = run(source)
        self.assertEqual(result.kind, 'fault', result.message)
        self.assertEqual(result.record.operation, 'div.checked.i64')
        self.assertEqual(result.globals['hit'], 0)
        self.assertEqual(result.fuel, 2)

    def test_for_initializer_retains_equals_position(self):
        source = ('void move[n](in I64[n] src) {\n'
                  '    for (I64[n + 1] dst = src; false; dst[0] = 0) {}\n}\n'
                  'export I64 run() { I64[2] src; move(src); return 0; }\n')
        self.assert_copy_fault(run(source), 3, 2, 2, 25)

    def test_equal_shape_initializer_and_assignment_copy_storage(self):
        source = ('I64 move[n](in I64[n] src) { I64[n] dst = src; dst[0] = 9; return src[0] * 10 + dst[0]; }\n'
                  'export I64 run() { I64[2] src; I64[2] dst; src[0] = 7; dst = src; '
                  'dst[0] = 2; return move(src) * 10 + dst[0]; }\n')
        result = run(source)
        self.assertEqual(result.kind, 'value', result.message or result.diagnostic)
        self.assertEqual(result.value.value, 792)
        self.assertEqual(result.fuel, 2)

    def test_same_array_assignment_remains_no_effect(self):
        source = ('void move(inout I64[_] src) { src = src; }\n'
                  'export I64 run() { I64[1] src; src[0] = 7; move(src); return src[0]; }\n')
        result = run(source)
        self.assertEqual(result.kind, 'value')
        self.assertEqual(result.value.value, 7)
        self.assertEqual(result.fuel, 2)

    def test_equal_empty_initializer_keeps_zero_elements(self):
        source = ('struct Empty {}\n'
                  'I64 move[n](in Empty[n] src) { Empty[n] dst = src; return len(dst); }\n'
                  'export I64 run() { Empty[0] src; return move(src); }\n')
        result = run(source)
        self.assertEqual(result.kind, 'value', result.message or result.diagnostic)
        self.assertEqual(result.value.value, 0)
        self.assertEqual(result.fuel, 2)

    def test_static_mismatch_and_element_mismatch_remain_diagnostics(self):
        for statement, code in [('I64[2] dst = src;', 'C2012'),
                                ('I64[2] dst; dst = src;', 'C2012'),
                                ('U8[3] dst = src;', 'C2001')]:
            with self.subTest(statement=statement):
                result = run('export I64 run() { I64[3] src; ' + statement + ' return 0; }\n')
                self.assertEqual(result.kind, 'compile-error')
                self.assertEqual(result.diagnostic.code, code)

    def test_readonly_copy_target_remains_rejected(self):
        result = run('void move(in I64[_] dst, in I64[_] src) { dst = src; }\n'
                     'export I64 run() { return 0; }\n')
        self.assertEqual(result.kind, 'compile-error')
        self.assertEqual(result.diagnostic.code, 'C2060')

    def test_fault_record_round_trip(self):
        fault = self.assert_copy_fault(run(assignment_source()), 2, 3, 2, 9)
        encoded = encode_fault_record(fault, RUN_TIME)
        self.assertEqual(decode_fault_record(encoded, RUN_TIME), (2, fault))

    def test_negative_extents_builtin_rank_and_slices(self):
        # Box 09: a negative constant extent is C2015 and a negative run-time extent faults
        # `decl.shape` at the declared name (ruling R10); `copy`, rank 2 and slices are admitted.
        result = run('export I64 run() { I64[-1] dst; return 0; }\n')
        self.assertEqual((result.kind, result.diagnostic.code), ('compile-error', 'C2015'))
        result = run('void move[n](in I64[n] src) { I64[n - 2] dst = src; }\n'
                     'export I64 run() { I64[1] src; move(src); return 0; }\n')
        self.assertEqual(result.kind, 'fault')
        r = result.record
        self.assertEqual((r.code, r.operation, r.operands, r.limit, (r.position.line, r.position.column)),
                         ('E_SHAPE', 'decl.shape', (Value('I64', 0), Value('I64', -1)), Value('I64', 0), (1, 42)))
        result = run('export I64 run() { I64[1] dst; I64[2] src; copy(dst, src); return 0; }\n')
        self.assertEqual((result.kind, result.diagnostic.code), ('compile-error', 'C2012'))
        for source in ['export I64 run() { I64[1, 2] dst; return 0; }\n',
                       'export I64 run() { I64[2] src; I64[1] dst; dst = src[0..1]; return 0; }\n']:
            with self.subTest(source=source):
                self.assertEqual(run(source).kind, 'value')


if __name__ == '__main__':
    unittest.main()
