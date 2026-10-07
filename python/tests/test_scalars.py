"""Scalar arguments and cint.Fixed (SPEC-03 P-21 to P-24; cases 23, 23a, 23b, 24
at the conversion layer; BX10-15)."""
import decimal
from fractions import Fraction
import pickle
import unittest

import cint
from cint._args import Param, bind_argument
from cint._elem import T27_MAX
from cint._scalar import from_raw, to_raw

from .support import numpy

I64_MAX = (1 << 63) - 1


class Case23(unittest.TestCase):
    """Case 23: a Python `bool` or a `numpy.int32` for an `I64` parameter is refused."""

    def test_python_bool(self):
        for value in (True, False):
            with self.assertRaises(cint.Refused) as caught:
                to_raw(value, "I64", "parameter n")
            self.assertIn("a bool is not an I64 argument", str(caught.exception))

    @unittest.skipIf(numpy is None, "NumPy is not installed")
    def test_numpy_int32(self):
        with self.assertRaises(cint.Refused) as caught:
            to_raw(numpy.int32(5), "I64", "parameter n")
        self.assertIn("SPEC-03 P-22", str(caught.exception))

    @unittest.skipIf(numpy is None, "NumPy is not installed")
    def test_numpy_bool(self):
        with self.assertRaises(cint.Refused):
            to_raw(numpy.bool_(True), "I64")

    def test_through_the_parameter(self):
        with self.assertRaises(cint.Refused):
            bind_argument(True, Param("n", "in", "I64"))


@unittest.skipIf(numpy is None, "NumPy is not installed")
class Case23a(unittest.TestCase):
    """Case 23a: `numpy.longlong(5)` for an `I64` parameter is accepted on every
    platform: the dtype maps by kind and size, never by its C name."""

    def test_longlong(self):
        self.assertEqual(to_raw(numpy.longlong(5), "I64"), 5)
        self.assertEqual(bind_argument(numpy.longlong(5), Param("n", "in", "I64")).value, 5)

    def test_by_size(self):
        self.assertEqual(to_raw(numpy.int64(-7), "I64"), -7)
        self.assertEqual(to_raw(numpy.intc(3), "I32"), 3)
        self.assertEqual(to_raw(numpy.uint64(2 ** 64 - 1), "U64"), 2 ** 64 - 1)
        self.assertEqual(to_raw(numpy.int8(-128), "I8"), -128)

    def test_other_sizes_refused(self):
        for value, elem in ((numpy.uint32(1), "U64"), (numpy.int64(1), "I32"), (numpy.int64(1), "U64"),
                            (numpy.int8(1), "T1"), (numpy.int64(1), "T27"), (numpy.int64(1), "Q32.32")):
            with self.assertRaises(cint.Refused, msg="%r for %s" % (value, elem)):
                to_raw(value, elem)


class Case23b(unittest.TestCase):
    """Case 23b: a Python `int` `1` for a `Q32.32` parameter is refused with
    `E_UNSUPPORTED`, naming both readings (P-21a)."""

    def test_refused(self):
        with self.assertRaises(cint.Refused) as caught:
            to_raw(1, "Q32.32", "parameter x")
        e = caught.exception
        self.assertEqual(e.code, "E_UNSUPPORTED")
        self.assertIn("cint.Fixed('Q32.32', value=1) is raw 4294967296", str(e))
        self.assertIn("cint.Fixed('Q32.32', raw=1)", str(e))

    def test_fixed_accepted(self):
        self.assertEqual(to_raw(cint.Fixed("Q32.32", value=1), "Q32.32"), 1 << 32)
        self.assertEqual(to_raw(cint.Fixed("Q32.32", raw=1), "Q32.32"), 1)

    def test_other_format_refused(self):
        with self.assertRaises(cint.Refused) as caught:
            to_raw(cint.Fixed("Q16.16", value=1), "Q32.32")
        self.assertIn("never converted implicitly", str(caught.exception))

    def test_fixed_for_an_integer_refused(self):
        with self.assertRaises(cint.Refused):
            to_raw(cint.Fixed("Q32.32", value=1), "I64")


class Case24(unittest.TestCase):
    """Case 24: a Python `float` argument is refused with `E_UNSUPPORTED`, for
    every parameter type (P-23)."""

    ELEMS = ("I8", "I64", "U64", "I1024", "Q32.32", "Q16.16", "Bool", "T1", "T27")

    def check(self, value):
        for elem in self.ELEMS:
            with self.assertRaises(cint.Refused, msg=elem) as caught:
                to_raw(value, elem, "parameter x")
            e = caught.exception
            self.assertEqual(e.code, "E_UNSUPPORTED")
            self.assertIn("cint.from_float", str(e))
            self.assertIn("cint.Fixed(elem, value=fractions.Fraction(...))", str(e))

    def test_python_float(self):
        self.check(1.5)
        self.check(2.0)

    def test_complex_and_decimal(self):
        self.check(complex(1, 0))
        self.check(decimal.Decimal("1.5"))

    @unittest.skipIf(numpy is None, "NumPy is not installed")
    def test_numpy_floats(self):
        self.check(numpy.float64(1.5))
        self.check(numpy.float32(1.5))
        self.check(numpy.complex128(1))

    def test_through_the_parameter(self):
        with self.assertRaises(cint.Refused) as caught:
            bind_argument(1.5, Param("x", "in", "I64"))
        self.assertEqual(caught.exception.code, "E_UNSUPPORTED")
        with self.assertRaises(cint.Refused):
            bind_argument(1.5, Param("a", "in", "I64", 1))


class Ranges(unittest.TestCase):
    def test_i64_edges(self):
        self.assertEqual(to_raw(I64_MAX, "I64"), I64_MAX)
        self.assertEqual(to_raw(-I64_MAX - 1, "I64"), -I64_MAX - 1)
        with self.assertRaises(cint.Refused) as caught:
            to_raw(I64_MAX + 1, "I64", "parameter n")
        e = caught.exception
        self.assertEqual((e.code, e.operands, e.operand_types), ("E_NARROW", (I64_MAX + 1,), ("Z",)))
        self.assertEqual((e.exact, e.limit, e.limit_type), (I64_MAX + 1, I64_MAX, "I64"))
        self.assertIn("maximum:", str(e))

    def test_unsigned_minimum(self):
        with self.assertRaises(cint.Refused) as caught:
            to_raw(-1, "U8")
        self.assertEqual((caught.exception.limit, caught.exception.limit_type), (0, "U8"))
        self.assertIn("minimum:", str(caught.exception))
        self.assertEqual(to_raw(255, "U8"), 255)

    def test_wide(self):
        top = (1 << 1023) - 1
        self.assertEqual(to_raw(top, "I1024"), top)
        self.assertEqual(to_raw(-top - 1, "I1024"), -top - 1)
        with self.assertRaises(cint.Refused):
            to_raw(top + 1, "I1024")

    def test_other_values(self):
        for value in ("5", b"5", None, [5], Fraction(1, 2)):
            with self.assertRaises(cint.Refused, msg=repr(value)):
                to_raw(value, "I64")

    def test_text_names_encode(self):
        with self.assertRaises(cint.Refused) as caught:
            to_raw("abc", "U8")
        self.assertIn('s.encode("utf-8")', str(caught.exception))


class BoolAndTernary(unittest.TestCase):
    """BX10-15: `Bool` takes `True` or `False`; `T1` and `T27` take a Python
    `int` in range, refused with `E_NARROW` outside it (SPEC-05 TR-VAL-2)."""

    def test_bool(self):
        self.assertEqual((to_raw(True, "Bool"), to_raw(False, "Bool")), (1, 0))
        for value in (1, 0, "true", None):
            with self.assertRaises(cint.Refused, msg=repr(value)):
                to_raw(value, "Bool")

    @unittest.skipIf(numpy is None, "NumPy is not installed")
    def test_numpy_bool(self):
        self.assertEqual(to_raw(numpy.bool_(True), "Bool"), 1)
        with self.assertRaises(cint.Refused):
            to_raw(numpy.uint8(1), "Bool")

    def test_t1(self):
        self.assertEqual([to_raw(v, "T1") for v in (-1, 0, 1)], [-1, 0, 1])
        with self.assertRaises(cint.Refused) as caught:
            to_raw(2, "T1")
        e = caught.exception
        self.assertEqual((e.code, e.operation, e.limit, e.limit_type), ("E_NARROW", "decode.checked.t1", 1, "T1"))
        with self.assertRaises(cint.Refused):
            to_raw(True, "T1")

    def test_t27(self):
        self.assertEqual(T27_MAX, (3 ** 27 - 1) // 2)
        self.assertEqual(to_raw(T27_MAX, "T27"), T27_MAX)
        self.assertEqual(to_raw(-T27_MAX, "T27"), -T27_MAX)
        with self.assertRaises(cint.Refused) as caught:
            to_raw(T27_MAX + 1, "T27")
        e = caught.exception
        self.assertEqual((e.code, e.operation, e.limit), ("E_NARROW", "decode.checked.t27", T27_MAX))

    def test_from_raw(self):
        self.assertIs(from_raw(1, "Bool"), True)
        self.assertEqual(from_raw(-1, "T1"), -1)
        self.assertEqual(from_raw(T27_MAX, "T27"), T27_MAX)
        self.assertEqual(from_raw(98304, "Q16.16"), cint.Fixed("Q16.16", raw=98304))
        self.assertIs(type(from_raw(5, "I64")), int)


class FixedValues(unittest.TestCase):
    """`cint.Fixed`: exact, immutable, never converted implicitly (P-21a, P-24)."""

    def test_spec_examples(self):
        x = cint.Fixed("Q32.32", raw=429496730)
        self.assertEqual((x.elem, x.raw, x.frac_bits), ("Q32.32", 429496730, 32))
        self.assertEqual(x.as_fraction(), Fraction(429496730, 1 << 32))
        self.assertEqual(str(x), "0.1000000000931322574615478515625")
        self.assertEqual(cint.Fixed("Q32.32", value=1).raw, 1 << 32)

    def test_decimal_text(self):
        cases = [("Q16.16", 98304, "1.5"), ("Q16.16", -81920, "-1.25"), ("Q16.16", 0, "0.0"),
                 ("Q16.16", 65536, "1.0"), ("Q16.16", -1, "-0.0000152587890625"),
                 ("Q16.16", 2147483647, "32767.9999847412109375"), ("Q16.16", -2147483648, "-32768.0"),
                 ("Q4.60", 1, "0.000000000000000000867361737988403547205962240695953369140625")]
        for elem, raw, text in cases:
            x = cint.Fixed(elem, raw=raw)
            self.assertEqual(str(x), text)
            self.assertEqual(format(x), text)
            self.assertEqual(Fraction(text), x.as_fraction())

    def test_value_must_be_exact(self):
        self.assertEqual(cint.Fixed("Q16.16", value=Fraction(3, 2)).raw, 98304)
        self.assertEqual(cint.Fixed("Q16.16", value=-2).raw, -131072)
        with self.assertRaises(cint.Refused) as caught:
            cint.Fixed("Q16.16", value=Fraction(1, 3))
        self.assertEqual(caught.exception.code, "E_NARROW")

    def test_range(self):
        with self.assertRaises(cint.Refused) as caught:
            cint.Fixed("Q16.16", value=32768)
        e = caught.exception
        self.assertEqual((e.code, e.exact, e.limit_type), ("E_NARROW", 32768 << 16, "Q16.16"))
        self.assertEqual(e.limit, cint.Fixed("Q16.16", raw=(1 << 31) - 1))
        with self.assertRaises(cint.Refused):
            cint.Fixed("Q16.16", raw=1 << 31)
        self.assertEqual(cint.Fixed("Q16.16", value=-32768).raw, -(1 << 31))

    def test_floats_and_bools_refused(self):
        with self.assertRaises(cint.Refused) as caught:
            cint.Fixed("Q16.16", value=1.5)
        self.assertEqual(caught.exception.code, "E_UNSUPPORTED")
        for kwargs in ({"value": True}, {"raw": True}, {"raw": 1.0}, {"value": "1"}):
            with self.assertRaises(TypeError, msg=repr(kwargs)):
                cint.Fixed("Q16.16", **kwargs)

    def test_arguments(self):
        with self.assertRaises(TypeError):
            cint.Fixed("Q16.16")
        with self.assertRaises(TypeError):
            cint.Fixed("Q16.16", raw=1, value=1)
        with self.assertRaises(ValueError):
            cint.Fixed("I64", raw=1)
        with self.assertRaises(ValueError):
            cint.Fixed("Q16.15", raw=1)

    def test_no_implicit_conversion(self):
        x = cint.Fixed("Q16.16", raw=98304)
        with self.assertRaises(TypeError):
            int(x)
        with self.assertRaises(TypeError):
            float(x)
        with self.assertRaises(TypeError):
            format(x, ".2f")
        with self.assertRaises(TypeError):
            x + x

    def test_equality_and_order(self):
        a, b = cint.Fixed("Q16.16", raw=1), cint.Fixed("Q16.16", raw=2)
        self.assertEqual(a, cint.Fixed("Q16.16", raw=1))
        self.assertEqual(hash(a), hash(cint.Fixed("Q16.16", raw=1)))
        self.assertTrue(a < b <= b and b > a >= a)
        self.assertNotEqual(a, cint.Fixed("Q32.32", raw=1))
        self.assertNotEqual(a, 1)
        with self.assertRaises(TypeError):
            a < cint.Fixed("Q32.32", raw=2)
        self.assertFalse(cint.Fixed("Q16.16", raw=0))
        self.assertTrue(a)

    def test_immutable_and_pickled(self):
        x = cint.Fixed("Q32.32", raw=-5)
        with self.assertRaises(AttributeError):
            x._raw = 1
        self.assertEqual(repr(x), "cint.Fixed('Q32.32', raw=-5)")
        self.assertEqual(pickle.loads(pickle.dumps(x)), x)


if __name__ == "__main__":
    unittest.main()
