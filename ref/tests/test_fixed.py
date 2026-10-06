import os, sys; sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # put ref/ on the path
"""Fixed point (SPEC-01 section 5): every expected value is a row of a SPEC-01 table."""
import unittest

from cint_ref import fixed
from cint_ref.arith import OpenCase
from cint_ref.faults import Fault
from cint_ref.types import Value

M = 9223372036854775807


def raw(r):
    assert isinstance(r, Value), r
    return r.value


class Types(unittest.TestCase):
    def test_definition_table(self):                       # SPEC-01 5.1
        for name, w, storage, f in (("Q1.15", 16, "I16", 15), ("Q16.16", 32, "I32", 16),
                                    ("Q32.32", 64, "I64", 32), ("Q4.60", 64, "I64", 60)):
            q = fixed.q_type(name)
            self.assertEqual((q.width, q.storage.name, q.f), (w, storage, f))
        self.assertEqual(fixed.q_type("Q16.16").ident, "q16_16")
        self.assertEqual(fixed.q_from_ident("q1_63").name, "Q1.63")

    def test_illegal_spellings(self):                     # SPEC-01 5.1 constraints
        for bad in ("Q0.16", "Q3.3", "Q16.15", "Q65.0", "Q16", "I32"):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                fixed.q_type(bad)


class Multiply(unittest.TestCase):                         # SPEC-01 5.3 table
    def mul(self, a, b, mode="half_even", form="checked"):
        return fixed.op("mul", form, "Q16.16", a, b, mode=mode)

    def test_table(self):
        rows = [(98304, 98304, "half_even", 147456), (1, 98304, "half_even", 2), (1, 98304, "floor", 1),
                (1, 98304, "trunc", 1), (-1, 98304, "half_even", -2), (-1, 98304, "trunc", -1),
                (-1, 98304, "half_up", -1), (1, 32768, "half_even", 0), (-1, 32768, "half_even", 0),
                (-1, 32768, "half_away", -1), (-16777216, 8388608, "half_even", -2147483648)]
        for a, b, mode, want in rows:
            with self.subTest(a=a, b=b, mode=mode):
                self.assertEqual(raw(self.mul(a, b, mode)), want)

    def test_overflow_and_saturation(self):
        f = self.mul(16777216, 8388608)
        self.assertIsInstance(f, Fault)
        self.assertEqual((f.code, f.operation, f.exact, f.limit), ("E_OVERFLOW", "mul.checked.q16_16.half_even",
                                                                   2147483648, Value("Q16.16", 2147483647)))
        self.assertEqual(f.operands, (Value("Q16.16", 16777216), Value("Q16.16", 8388608)))
        self.assertEqual(raw(self.mul(16777216, 8388608, form="sat")), 2147483647)
        self.assertEqual(raw(self.mul(16777216, 8388608, form="wrap")), -2147483648)   # wrap(I32, 2^31)

    def test_mode_is_required(self):                      # 9.8: the record names the mode actually used
        with self.assertRaises(ValueError):
            fixed.op("mul", "checked", "Q16.16", 1, 1)


class Divide(unittest.TestCase):                           # SPEC-01 5.4 table
    def test_table(self):
        one, two, three = 65536, 131072, 196608
        self.assertEqual(raw(fixed.op("div", "checked", "Q16.16", one, three, mode="half_even")), 21845)
        self.assertEqual(raw(fixed.op("div", "checked", "Q16.16", two, three, mode="half_even")), 43691)
        self.assertEqual(raw(fixed.op("div", "checked", "Q16.16", -one, three, mode="half_even")), -21845)
        self.assertEqual(raw(fixed.op("div", "checked", "Q16.16", -one, three, mode="floor")), -21846)

    def test_faults(self):
        f = fixed.op("div", "checked", "Q16.16", 65536, 0, mode="half_even")
        self.assertEqual((f.code, f.exact, f.limit), ("E_DIV_ZERO", None, None))
        f = fixed.op("div", "checked", "Q16.16", -2147483648, -65536, mode="half_even")
        self.assertEqual((f.code, f.exact, f.limit.value), ("E_OVERFLOW", 2147483648, 2147483647))

    def test_constant_example(self):                      # SPEC-01 3.2: (1.0 / 3.0) * 3.0 is raw 65535
        third = raw(fixed.op("div", "checked", "Q16.16", 65536, 196608, mode="half_even"))
        self.assertEqual(raw(fixed.op("mul", "checked", "Q16.16", third, 196608, mode="half_even")), 65535)


class SquareRoot(unittest.TestCase):                       # SPEC-01 5.5 table
    def test_table(self):
        rows = [("Q16.16", 131072, "floor", 92681), ("Q16.16", 131072, "half_even", 92682),
                ("Q16.16", 32768, "floor", 46340), ("Q16.16", 32768, "half_even", 46341),
                ("Q32.32", 8589934592, "floor", 6074000999), ("Q32.32", 8589934592, "half_even", 6074001000),
                ("Q1.15", 32767, "floor", 32767)]
        for q, a, mode, want in rows:
            with self.subTest(q=q, a=a, mode=mode):
                self.assertEqual(raw(fixed.op("sqrt", "checked", q, a, mode=mode)), want)

    def test_round_up_out_of_range_for_i_equal_1(self):
        for q, a, mode, exact in (("Q1.15", 32767, "ceil", 32768), ("Q1.15", 32767, "away", 32768),
                                  ("Q1.63", M, "ceil", 2**63)):
            with self.subTest(q=q, mode=mode):
                f = fixed.op("sqrt", "checked", q, a, mode=mode)
                self.assertEqual((f.code, f.exact, f.limit.value), ("E_OVERFLOW", exact, (1 << (fixed.q_type(q).width - 1)) - 1))
        self.assertEqual(raw(fixed.op("sqrt", "sat", "Q1.15", 32767, mode="ceil")), 32767)

    def test_negative_input(self):                        # SPEC-01 4.10, 5.5; interim decision OQ-01
        f = fixed.op("sqrt", "checked", "Q16.16", -65536, mode="half_even")
        self.assertEqual((f.code, f.operation, f.exact, f.limit), ("E_DOMAIN", "sqrt.checked.q16_16.half_even", None, None))


class Conversions(unittest.TestCase):                      # SPEC-01 5.6 table
    def test_q_to_q(self):
        self.assertEqual(fixed.convert("checked", "Q32.32", "Q16.16", 1, mode="half_even"), Value("Q16.16", 0))
        self.assertEqual(fixed.convert("checked", "Q32.32", "Q16.16", 1, mode="ceil"), Value("Q16.16", 1))
        self.assertEqual(fixed.convert("checked", "Q16.16", "Q32.32", 65536), Value("Q32.32", 4294967296))
        f = fixed.convert("checked", "Q32.32", "Q16.16", 32768 << 32, mode="half_even")
        self.assertEqual((f.code, f.operation, f.exact, f.limit), ("E_NARROW", "as.checked.q32_32.q16_16.half_even",
                                                                   2147483648, Value("Q16.16", 2147483647)))
        self.assertEqual(fixed.convert("checked", "Q32.32", "Q16.16", -(32768 << 32), mode="half_even"),
                         Value("Q16.16", -2147483648))

    def test_q_to_int(self):
        x = -163840                                        # Q16.16 -2.5
        self.assertEqual(fixed.convert("checked", "Q16.16", "I32", x, mode="half_even"), Value("I32", -2))
        self.assertEqual(fixed.convert("checked", "Q16.16", "I32", x, mode="floor"), Value("I32", -3))
        self.assertEqual(fixed.convert("checked", "Q16.16", "I32", x, mode="half_away"), Value("I32", -3))

    def test_int_to_q(self):
        self.assertEqual(fixed.convert("checked", "I64", "Q16.16", 2), Value("Q16.16", 131072))
        # n as Q16.16 with n = 40000 is E_NARROW, but whether `exact` is 40000 or the raw
        # 2621440000 is not stated (ref/OPEN.md O-8), so cint_ref decides nothing.
        with self.assertRaises(OpenCase):
            fixed.convert("checked", "I64", "Q16.16", 40000)

    def test_mode_presence(self):                         # SPEC-01 9.8: mode present exactly when it rounds
        with self.assertRaises(ValueError):
            fixed.convert("checked", "Q16.16", "Q32.32", 1, mode="half_even")
        with self.assertRaises(ValueError):
            fixed.convert("checked", "Q32.32", "Q16.16", 1)


if __name__ == "__main__":
    unittest.main()
