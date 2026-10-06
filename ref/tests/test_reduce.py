import os, sys; sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # put ref/ on the path
"""Reductions (SPEC-01 section 6): expected values from the 6.1 and 6.2 examples and fixtures 80 to 91."""
import unittest

from cint_ref import reduce
from cint_ref.faults import Fault
from cint_ref.types import Value

M = 9223372036854775807
m = -9223372036854775808


class Sum(unittest.TestCase):
    def test_examples(self):
        self.assertEqual(reduce.op("sum", "checked", "I8", [120, 120, -120]), Value("I8", 120))
        self.assertEqual(reduce.op("sum", "checked", "I8", [127, 127, 127], target="I16"), Value("I16", 381))
        self.assertEqual(reduce.op("sum", "checked", "I64", [M, 1, -1]), Value("I64", M))
        self.assertEqual(reduce.op("sum", "checked", "I64", []), Value("I64", 0))

    def test_final_range_check(self):                       # 6.2: E_OVERFLOW, not E_NARROW
        f = reduce.op("sum", "checked", "I8", [127, 127, 127])
        self.assertIsInstance(f, Fault)
        self.assertEqual((f.code, f.operation, f.exact, f.limit), ("E_OVERFLOW", "sum.checked.i8.i8", 381, Value("I8", 127)))

    def test_wrap_and_sat(self):
        self.assertEqual(reduce.op("sum_wrap", "wrap", "I8", [127, 1]), Value("I8", -128))
        self.assertEqual(reduce.op("sum_wrap", "wrap", "I8", [120, 120, -120]), Value("I8", 120))
        self.assertEqual(reduce.op("sum_sat", "sat", "I8", [120, 120, -120]), Value("I8", 7))
        self.assertEqual(reduce.op("sum_sat", "sat", "I8", [120, -120, 120]), Value("I8", 120))
        self.assertEqual(reduce.op("sum_sat", "sat", "I8", [100, 100, -100, -100]), Value("I8", -73))   # 6.1 table
        self.assertEqual(reduce.op("sum_wrap", "wrap", "I8", []), Value("I8", 0))
        self.assertEqual(reduce.op("sum_sat", "sat", "I8", []), Value("I8", 0))


class Fold(unittest.TestCase):
    def test_left_fold_fault_index(self):
        f = reduce.op("fold_checked", "add", "I8", [120, 120, -120], init=0)
        self.assertEqual((f.code, f.operation, f.exact, f.limit, f.index),
                         ("E_OVERFLOW", "fold_checked.add.i8", 240, Value("I8", 127), 1))
        f = reduce.op("fold_checked", "add", "I64", [M, 1, -1], init=0)
        self.assertEqual((f.exact, f.index), (2**63, 1))
        f = reduce.op("fold_checked", "add", "I8", [100, 100, -100, -100], init=0)   # 6.1 column-major row
        self.assertEqual((f.exact, f.index), (200, 1))

    def test_fault_operands(self):                          # 9.2: index, accumulator, element
        f = reduce.op("fold_checked", "add", "I8", [120, 120, -120], init=0)
        self.assertEqual(f.operands[-3:], (Value("I64", 1), Value("I8", 120), Value("I8", 120)))

    def test_values(self):
        self.assertEqual(reduce.op("fold_checked", "add", "I8", [100, -100, 100, -100], init=0), Value("I8", 0))
        self.assertEqual(reduce.op("fold_checked", "add", "I8", [], init=5), Value("I8", 5))
        f = reduce.op("fold_checked", "mul", "I8", [16, 8], init=1)
        self.assertEqual((f.code, f.exact, f.index), ("E_OVERFLOW", 128, 1))


class MinMax(unittest.TestCase):
    def test_examples(self):
        self.assertEqual(reduce.op("reduce_max", "checked", "I64", [-1, m]), Value("I64", -1))
        self.assertEqual(reduce.op("reduce_min", "checked", "I64", [-1, m]), Value("I64", m))
        f = reduce.op("reduce_min", "checked", "I64", [])
        self.assertEqual((f.code, f.exact, f.limit), ("E_SHAPE", None, None))
        self.assertEqual(reduce.op("reduce_min", "checked", "I64", [], init=3), Value("I64", 3))   # min(init, xs)


class Arguments(unittest.TestCase):
    def test_elements_must_fit(self):
        with self.assertRaises(ValueError):
            reduce.op("sum", "checked", "I8", [128])

    def test_forms(self):
        with self.assertRaises(ValueError):
            reduce.op("sum_sat", "checked", "I8", [1])
        with self.assertRaises(ValueError):
            reduce.op("fold_checked", "sub", "I8", [1], init=0)


if __name__ == "__main__":
    unittest.main()
