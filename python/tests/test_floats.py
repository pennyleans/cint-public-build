"""The float boundary (SPEC-03 section 8, X-1 to X-11; case 33; BX10-18).

Every row of the boundary table of SPEC-03 8.1 is a test below, in table
order. Inputs that are not plain decimals are built from their bit patterns."""
import array
from fractions import Fraction
import random
import struct
import unittest

import cint
from cint import _floats
from cint._floats import ROUND_MODES, exact_to_bits, round_div

from .support import allocations, bits64, f64, numpy

Q16_MAX, Q16_MIN = (1 << 31) - 1, -(1 << 31)
I64_MAX, I64_MIN = (1 << 63) - 1, -(1 << 63)
NAN, POS_INF, NEG_INF = 0x7FF8000000000000, 0x7FF0000000000000, 0xFFF0000000000000


def raw(x, elem, rounding="half_even", overflow="refuse"):
    """The raw value of one converted element."""
    v = cint.from_float([x], elem, rounding=rounding, overflow=overflow).tolist()[0]
    return v.raw if isinstance(v, cint.Fixed) else v


def doubles(*bits):
    """A binary64 array of exact bit patterns, never passed through a Python float."""
    return memoryview(array.array("Q", bits)).cast("B").cast("d")


class Case33(unittest.TestCase):
    """Case 33: every row of the table in SPEC-03 8.1."""

    def refused(self, x, elem, rounding="half_even", overflow="refuse") -> cint.Refused:
        before = allocations()
        with self.assertRaises(cint.Refused) as caught:
            cint.from_float(x if isinstance(x, memoryview) else [x], elem, rounding=rounding, overflow=overflow)
        self.assertEqual(allocations(), before, "a refused conversion left a Buffer")
        return caught.exception

    def test_row_01_one_and_a_half(self):
        self.assertEqual(raw(1.5, "Q16.16"), 98304)

    def test_row_02_two_and_a_half(self):
        self.assertEqual(raw(2.5, "Q16.16"), 163840)

    def test_row_03_minus_one_and_a_quarter(self):
        self.assertEqual(raw(-1.25, "Q16.16"), -81920)

    def test_row_04_half_lsb_ties_to_even(self):
        self.assertEqual(raw(2.0 ** -17, "Q16.16"), 0)

    def test_row_05_three_half_lsb(self):
        self.assertEqual(raw(3 * 2.0 ** -17, "Q16.16"), 2)

    def test_row_06_five_half_lsb(self):
        self.assertEqual(raw(5 * 2.0 ** -17, "Q16.16"), 2)

    def test_rows_07_to_11_negative_half_lsb(self):
        x = -(2.0 ** -17)
        for mode, want in (("half_even", 0), ("floor", -1), ("away", -1), ("half_down", -1), ("half_up", 0)):
            self.assertEqual(raw(x, "Q16.16", mode), want, mode)

    def test_row_12_one_tenth(self):
        x = 0.1
        self.assertEqual(bits64(x), 0x3FB999999999999A)
        self.assertEqual(Fraction(x), Fraction(3602879701896397, 2 ** 55))
        self.assertEqual(Fraction(3602879701896397, 2 ** 23), Fraction(x) * 2 ** 32)
        self.assertEqual(raw(x, "Q32.32"), 429496730)

    def test_row_13_largest_q16(self):
        x = 32767.99998474121
        self.assertEqual(Fraction(x), Fraction(2 ** 31 - 1, 2 ** 16))
        self.assertEqual(raw(x, "Q16.16"), Q16_MAX)

    def test_row_14_overflow_refused(self):
        for mode in ROUND_MODES:
            e = self.refused(32768.0, "Q16.16", mode)
            self.assertEqual((e.code, e.operation), ("E_NARROW", "from_f64.checked.q16_16." + mode))
            self.assertEqual(e.operands, (bits64(32768.0), 1 << 52, -37))
            self.assertEqual(e.operand_types, ("U64", "Z", "Z"))
            self.assertEqual((e.exact, e.limit, e.limit_type), (1 << 31, cint.Fixed("Q16.16", raw=Q16_MAX), "Q16.16"))
            self.assertEqual((e.index, e.count, e.reason), ((0,), 1, None))
            self.assertIn("maximum:", str(e))

    def test_row_15_saturate(self):
        b = cint.from_float([32768.0], "Q16.16", rounding="half_even", overflow="saturate")
        self.assertEqual(b.tolist()[0].raw, Q16_MAX)
        self.assertEqual(b.conversion, cint.FloatConversion("binary64", "half_even", "saturate", 1, 1))

    def test_row_16_minimum_q16(self):
        for mode in ROUND_MODES:
            self.assertEqual(raw(-32768.0, "Q16.16", mode), Q16_MIN, mode)

    def test_row_17_two_to_the_63_refused(self):
        for mode in ROUND_MODES:
            e = self.refused(2.0 ** 63, "I64", mode)
            self.assertEqual((e.code, e.operation, e.exact, e.limit, e.limit_type),
                             ("E_NARROW", "from_f64.checked.i64." + mode, 1 << 63, I64_MAX, "I64"))

    def test_row_18_minus_two_to_the_63(self):
        for mode in ROUND_MODES:
            self.assertEqual(raw(-(2.0 ** 63), "I64", mode), I64_MIN, mode)

    def test_row_19_negative_zero(self):
        x = f64(1 << 63)
        self.assertEqual(bits64(x), 0x8000000000000000)
        for mode in ROUND_MODES:
            self.assertEqual(raw(x, "I64", mode), 0, mode)
            self.assertEqual(raw(x, "Q16.16", mode), 0, mode)

    def test_row_20_non_finite(self):
        for bits in (NAN, POS_INF, NEG_INF):
            for elem in ("I8", "I64", "U64", "I1024", "Q16.16", "Q32.32"):
                for mode in ROUND_MODES:
                    for overflow in ("refuse", "saturate"):
                        e = self.refused(doubles(bits), elem, mode, overflow)
                        self.assertEqual((e.code, e.reason, e.operands, e.operand_types),
                                         ("E_NARROW", "non_finite", (bits,), ("U64",)))
                        self.assertEqual((e.exact, e.limit), (None, None))

    def test_row_21_tiny(self):
        self.assertEqual(raw(1e-300, "Q32.32"), 0)

    def test_row_22_unknown_mode(self):
        before = allocations()
        with self.assertRaises(cint.Refused) as caught:
            cint.from_float([1.5], "Q16.16", rounding="toward_zero")
        e = caught.exception
        self.assertEqual(e.code, "E_UNSUPPORTED")
        self.assertIn("use 'trunc'", str(e))
        self.assertEqual(allocations(), before)


class Rounding(unittest.TestCase):
    """X-3, X-4: the exact rational, rounded once by the named mode."""

    def test_both_halves_every_mode(self):
        want = {"floor": (0, -1), "ceil": (1, 0), "trunc": (0, 0), "away": (1, -1), "half_even": (0, 0),
                "half_away": (1, -1), "half_trunc": (0, 0), "half_up": (1, 0), "half_down": (0, -1)}
        for mode, (up, down) in want.items():
            self.assertEqual((raw(2.0 ** -17, "Q16.16", mode), raw(-(2.0 ** -17), "Q16.16", mode)), (up, down), mode)

    def test_round_div_matches_fractions(self):
        rng = random.Random(20261005)
        for _ in range(3000):
            n, d = rng.randint(-10 ** 6, 10 ** 6), rng.randint(1, 2000)
            q = Fraction(n, d)
            for mode in ROUND_MODES:
                self.assertEqual(round_div(n, d, mode), oracle(q, mode), (n, d, mode))

    def test_policies(self):
        self.assertEqual(cint.FloatPolicy(), cint.FloatPolicy("half_even", "refuse"))
        self.assertEqual(cint.FloatPolicy(overflow="saturate").form, "sat")
        for rounding, hint in (("toward_zero", "'trunc'"), ("nearest", "'half_even'"), ("bogus", None)):
            with self.assertRaises(cint.Refused) as caught:
                cint.FloatPolicy(rounding)
            self.assertEqual(caught.exception.code, "E_UNSUPPORTED")
            if hint:
                self.assertIn(hint, str(caught.exception))
        with self.assertRaises(cint.Refused) as caught:
            cint.FloatPolicy(overflow="wrap")
        self.assertIn("there is no wrapping policy", str(caught.exception))
        with self.assertRaises(AttributeError):
            cint.FloatPolicy().rounding = "floor"

    def test_policy_checked_before_reading(self):
        with self.assertRaises(cint.Refused):
            cint.from_float(object(), "I64", rounding="bogus")

    def test_saturated_count(self):
        b = cint.from_float([1.0, 1e300, -1e300, 2.0], "I8", overflow="saturate")
        self.assertEqual(b.tolist(), [1, 127, -128, 2])
        self.assertEqual(b.conversion.saturated, 2)
        self.assertEqual(b.conversion.elements, 4)


class Inputs(unittest.TestCase):
    """X-2, X-5, X-7, X-8: accepted formats, non-finite inputs, zeros and
    subnormals, and all-or-nothing bulk conversion."""

    def test_binary32(self):
        b = cint.from_float(array.array("f", [1.5, -1.25]), "Q16.16")
        self.assertEqual([x.raw for x in b.tolist()], [98304, -81920])
        self.assertEqual(b.conversion.source, "binary32")
        with self.assertRaises(cint.Refused) as caught:
            cint.from_float(array.array("f", [1.5, 65536.0]), "Q16.16")
        e = caught.exception
        self.assertEqual((e.operation, e.operand_types, e.index), ("from_f32.checked.q16_16.half_even",
                                                                   ("U32", "Z", "Z"), (1,)))

    def test_binary32_non_finite_payload(self):
        bits = 0x7FC00001
        with self.assertRaises(cint.Refused) as caught:
            cint.from_float(memoryview(array.array("I", [bits])).cast("B").cast("f"), "I32")
        self.assertEqual((caught.exception.operands, caught.exception.operand_types), ((bits,), ("U32",)))

    def test_nan_payloads(self):
        payloads = (0x7FF0000000000001, 0xFFF8000000000000, 0x7FF8DEADBEEF0000)
        with self.assertRaises(cint.Refused) as caught:
            cint.from_float(doubles(1 << 62, *payloads), "I64")
        e = caught.exception
        self.assertEqual((e.operands, e.index, e.count, e.reason), ((payloads[0],), (1,), 3, "non_finite"))
        self.assertIn("3 of 4 elements refused", str(e))

    def test_two_dimensional_index(self):
        with self.assertRaises(cint.Refused) as caught:
            cint.from_float([[1.0, float("nan")], [float("inf"), 2.0]], "I64")
        self.assertEqual((caught.exception.index, caught.exception.count), ((0, 1), 2))

    def test_subnormals(self):
        smallest, largest_sub = f64(1), f64(0x000FFFFFFFFFFFFF)
        self.assertEqual(raw(smallest, "Q32.32"), 0)
        self.assertEqual(raw(smallest, "Q32.32", "ceil"), 1)
        self.assertEqual(raw(-smallest, "Q32.32", "floor"), -1)
        self.assertEqual(raw(largest_sub, "I64", "away"), 1)
        self.assertEqual(raw(largest_sub, "Q4.60"), 0)

    def test_wide_targets(self):
        self.assertEqual(raw(2.0 ** 1022, "I1024"), 1 << 1022)
        self.assertEqual(raw(-(2.0 ** 1023), "I1024"), -(1 << 1023))
        with self.assertRaises(cint.Refused):
            cint.from_float([2.0 ** 1023], "I1024")
        self.assertEqual(raw(f64(0x7FEFFFFFFFFFFFFF), "I1024", overflow="saturate"), (1 << 1023) - 1)

    def test_mixed_widths(self):
        if numpy is None:
            self.skipTest("NumPy is not installed")
        b = cint.from_float([1.5, numpy.float32(2.5)], "Q16.16")
        self.assertEqual(b.conversion.source, "mixed")

    @unittest.skipIf(numpy is None, "NumPy is not installed")
    def test_numpy_sources(self):
        self.assertEqual(cint.from_float(numpy.array([1.5, -0.5]), "Q16.16").tolist(),
                         [cint.Fixed("Q16.16", raw=98304), cint.Fixed("Q16.16", raw=-32768)])
        self.assertEqual(cint.from_float(numpy.array([1.5], dtype=">f8"), "Q16.16").tolist()[0].raw, 98304)
        self.assertEqual(cint.from_float(numpy.array([[1.0, 2.0]], dtype=numpy.float32), "I8").tolist(), [[1, 2]])
        b = cint.copy(numpy.array([0.25]), elem="Q8.8", from_float=cint.FloatPolicy("floor"))
        self.assertEqual(b.tolist()[0].raw, 64)
        for dtype in (numpy.float16, numpy.longdouble, numpy.complex128):
            if dtype is numpy.longdouble and numpy.dtype(dtype).itemsize == 8:
                continue
            with self.assertRaises(cint.Refused, msg=str(dtype)) as caught:
                cint.from_float(numpy.zeros(2, dtype=dtype), "I64")
            self.assertIn("SPEC-03 X-2", str(caught.exception))

    def test_refusals(self):
        with self.assertRaises(cint.Refused) as caught:
            cint.from_float(array.array("q", [1]), "I64")
        self.assertIn("from_float converts floating-point data", str(caught.exception))
        with self.assertRaises(cint.Refused) as caught:
            cint.from_float([1.5, 2], "I64")
        self.assertEqual((caught.exception.index, caught.exception.count), ((1,), 1))
        for elem in ("Bool", "T1", "T27"):
            with self.assertRaises(cint.Refused, msg=elem):
                cint.from_float([1.0], elem)
        with self.assertRaises(cint.Refused):
            cint.from_float(1.5, "I64")
        with self.assertRaises(NotImplementedError):
            cint.from_float([1.0], "I64", recorder=object())


class ToFloat(unittest.TestCase):
    """X-10, X-11: exact values to the nearest binary64 or binary32, ties to
    even, computed from the integers."""

    def test_spec_example(self):
        out = cint.to_float(cint.copy([2 ** 53 + 1], elem="I64"))
        self.assertEqual(bits64(out[0]), bits64(9007199254740992.0))

    def test_fixed_point(self):
        b = cint.copy([cint.Fixed("Q16.16", raw=98304), cint.Fixed("Q16.16", raw=-81920)], elem="Q16.16")
        self.assertEqual([bits64(x) for x in cint.to_float(b)], [bits64(1.5), bits64(-1.25)])
        tenth = cint.to_float(cint.copy([cint.Fixed("Q32.32", raw=429496730)], elem="Q32.32"))[0]
        self.assertEqual(Fraction(tenth), Fraction(429496730, 2 ** 32))

    def test_binary64_against_int_division(self):
        """Python divides integers with correct rounding, ties to even, so it
        is an independent oracle for X-10 in binary64."""
        rng = random.Random(10)
        for _ in range(4000):
            f = rng.choice((0, 1, 16, 32, 60, 63))
            n = rng.choice((rng.randint(-2 ** 63, 2 ** 63 - 1), rng.randint(-2 ** 70, 2 ** 70),
                            rng.randint(-1000, 1000), (rng.randint(1, 2 ** 20) << 53 | 1 << 52) >> rng.randint(0, 9)))
            self.assertEqual(exact_to_bits(n, f, 64), bits64(n / 2 ** f), (n, f))

    def test_binary64_ties(self):
        for n in (2 ** 53 + 1, 2 ** 53 + 3, -(2 ** 53 + 1), 2 ** 54 + 2, 2 ** 54 + 6, (2 ** 53 + 1) << 10):
            self.assertEqual(exact_to_bits(n, 0, 64), bits64(n / 1), n)
        self.assertEqual(exact_to_bits(1, 1074, 64), 1)
        self.assertEqual(exact_to_bits(1, 1075, 64), 0)
        self.assertEqual(exact_to_bits(3, 1076, 64), 1)
        self.assertEqual(exact_to_bits((1 << 1024) - 1, 0, 64), POS_INF)

    def test_binary32(self):
        rng = random.Random(32)
        for _ in range(2000):
            f = rng.choice((0, 8, 16, 32))
            n = rng.choice((rng.randint(-2 ** 40, 2 ** 40), rng.randint(-2 ** 26, 2 ** 26), rng.randint(-50, 50)))
            self.assertEqual(exact_to_bits(n, f, 32), nearest32(Fraction(n, 2 ** f)), (n, f))
        self.assertEqual(exact_to_bits(2 ** 24 + 1, 0, 32), struct.unpack("<I", struct.pack("<f", 2.0 ** 24))[0])
        self.assertEqual(exact_to_bits(1 << 200, 0, 32), 0x7F800000)

    def test_results(self):
        b = cint.copy([[1, 2], [3, 4]], elem="I32")
        out = cint.to_float(b, dtype="float32")
        if numpy is not None:
            self.assertEqual((out.dtype, out.shape, out.flags.writeable), (numpy.dtype("float32"), (2, 2), False))
        else:
            self.assertEqual((out.format, out.shape, out.readonly), ("f", (2, 2), True))
        self.assertEqual(out.tolist(), [[1.0, 2.0], [3.0, 4.0]])
        view = cint.borrow(array.array("q", [-3]))
        self.assertEqual(cint.to_float(view).tolist(), [-3.0])
        view.release()
        self.assertEqual(cint.to_float(cint.copy([], elem="I64")).tolist(), [])
        with self.assertRaises(TypeError):
            cint.to_float([1, 2])
        with self.assertRaises(ValueError):
            cint.to_float(b, dtype="float16")
        with self.assertRaises(TypeError):
            cint.to_float(b, "float32")

    def test_wide_and_bool(self):
        self.assertEqual(cint.to_float(cint.copy([(1 << 1023) - 1], elem="I1024")).tolist(), [2.0 ** 1023])
        self.assertEqual(cint.to_float(cint.copy([True, False], elem="Bool")).tolist(), [1.0, 0.0])


def oracle(q: Fraction, mode: str) -> int:
    """SPEC-01 4.5 rounding of a rational, written from the definitions."""
    lo = q.numerator // q.denominator
    if lo == q:
        return lo
    hi = lo + 1
    toward_zero, away = (lo, hi) if q > 0 else (hi, lo)
    direct = {"floor": lo, "ceil": hi, "trunc": toward_zero, "away": away}
    if mode in direct:
        return direct[mode]
    if q - lo != Fraction(1, 2):
        return lo if q - lo < Fraction(1, 2) else hi
    ties = {"half_even": lo if lo % 2 == 0 else hi, "half_away": away, "half_trunc": toward_zero,
            "half_up": hi, "half_down": lo}
    return ties[mode]


def nearest32(q: Fraction) -> int:
    """The binary32 bits nearest to q, ties to even: the candidates around the
    binary32 rounding of the nearest binary64, compared exactly."""
    guess = struct.unpack("<I", struct.pack("<f", float(q)))[0] if q else 0
    candidates = []
    for b in (guess - 1, guess, guess + 1):
        if 0 <= b < 0x7F800000 or 0x80000000 <= b < 0xFF800000:
            candidates.append(b)
    if q < 0 and guess == 0:
        candidates = [0x80000001, 0]

    def value(b):
        return Fraction(struct.unpack("<f", struct.pack("<I", b))[0])
    best = min(candidates, key=lambda b: (abs(value(b) - q), b & 1))
    return 0 if value(best) == 0 else best


if __name__ == "__main__":
    unittest.main()
