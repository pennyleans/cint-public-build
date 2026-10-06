import os, sys; sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # put ref/ on the path
# ref/tests/test_arith.py
import unittest
from cint_ref.arith import op, Value, Fault

class ReviewExamples(unittest.TestCase):
    M = 9223372036854775807
    def test_checked_add_is_not_associative(self):
        self.assertIsInstance(op("add", "checked", "I64", self.M, 1), Fault)
        self.assertEqual(op("add", "checked", "I64", self.M, op("add", "checked", "I64", 1, -1).value).value, self.M)
    def test_sat_i8_is_not_associative(self):
        s = lambda a, b: op("add", "sat", "I8", a, b).value
        d = lambda a, b: op("sub", "sat", "I8", a, b).value
        self.assertEqual(d(s(120, 120), 120), 7)
        self.assertEqual(s(120, d(120, 120)), 120)
    def test_wrap_i8(self):
        self.assertEqual(op("add", "wrap", "I8", 100, 100).value, -56)   # SPEC-01 fixture 99
    def test_min_div_minus_one(self):
        f = op("div", "checked", "I64", -2**63, -1)
        self.assertIsInstance(f, Fault)
        self.assertEqual(f.code, "E_OVERFLOW"); self.assertEqual(f.exact, 2**63)
    def test_shift_count_range(self):
        f = op("shl", "checked", "I64", 1, 64)
        self.assertEqual(f.code, "E_SHIFT")
    def test_div_round_half_even(self):
        self.assertEqual(op("div_round", "checked", "I64", 5, 2, mode="half_even").value, 2)
        self.assertEqual(op("div_round", "checked", "I64", 7, 2, mode="half_even").value, 4)
        self.assertEqual(op("div_round", "checked", "I64", -5, 2, mode="half_even").value, -2)

# ---------------------------------------------------------------------------
# Further tests, each tied to a SPEC-01 clause or fixture row (section 13.2).
# ---------------------------------------------------------------------------

M = 9223372036854775807
m = -9223372036854775808


def val(r):
    assert isinstance(r, Value), r
    return r.value


class Types(unittest.TestCase):
    def test_ranges_2_1(self):
        from cint_ref.types import int_type
        rows = {
            "I8": (-128, 127), "I16": (-32768, 32767), "I32": (-2147483648, 2147483647),
            "I64": (m, M), "U8": (0, 255), "U16": (0, 65535), "U32": (0, 4294967295),
            "U64": (0, 18446744073709551615),
        }
        for name, (lo, hi) in rows.items():
            t = int_type(name)
            self.assertEqual((t.min, t.max), (lo, hi), name)

    def test_wrap_and_sat_1_2(self):
        from cint_ref.types import int_type
        i8 = int_type("I8")
        self.assertEqual(i8.wrap(128), -128)
        self.assertEqual(i8.wrap(-129), 127)
        self.assertEqual(i8.sat(1000), 127)
        self.assertEqual(i8.sat(-1000), -128)


class AddSubMul(unittest.TestCase):
    """SPEC-01 4.2 table and fixtures 1 to 15."""

    def test_fixture_rows(self):
        cases = [
            ("add", "checked", "I64", (M, 1), ("E_OVERFLOW", 9223372036854775808, M)),
            ("add", "checked", "I64", (9223372036854775800, 12), ("E_OVERFLOW", 9223372036854775812, M)),
            ("add", "wrap", "I64", (M, 1), m),
            ("add", "sat", "I64", (M, 1), M),
            ("sub", "checked", "I64", (m, 1), ("E_OVERFLOW", -9223372036854775809, m)),
            ("sub", "wrap", "I64", (m, 1), M),
            ("sub", "checked", "U8", (3, 5), ("E_OVERFLOW", -2, 0)),
            ("sub", "wrap", "U8", (3, 5), 254),
            ("sub", "sat", "U8", (3, 5), 0),
            ("mul", "checked", "I64", (m, -1), ("E_OVERFLOW", 9223372036854775808, M)),
            ("mul", "wrap", "I64", (m, -1), m),
            ("mul", "checked", "I32", (65536, 32768), ("E_OVERFLOW", 2147483648, 2147483647)),
            ("mul", "checked", "I32", (-65536, 32768), -2147483648),
            ("mul", "wrap", "I8", (100, 3), 44),
            ("mul", "sat", "I8", (-128, -1), 127),
        ]
        for name, form, t, args, expected in cases:
            with self.subTest(name=name, form=form, t=t, args=args):
                r = op(name, form, t, *args)
                if isinstance(expected, tuple):
                    self.assertIsInstance(r, Fault)
                    self.assertEqual((r.code, r.exact, r.limit.value), expected)
                    self.assertEqual(r.limit.type, t)
                else:
                    self.assertEqual(val(r), expected)
                    self.assertEqual(r.type, t)

    def test_operation_identifier_and_operands_9_2(self):
        f = op("add", "checked", "I64", M, 1)
        self.assertEqual(f.operation, "add.checked.i64")
        self.assertEqual([(o.type, o.value) for o in f.operands], [("I64", M), ("I64", 1)])

    def test_operand_out_of_type_is_rejected(self):
        with self.assertRaises(ValueError):
            op("add", "checked", "I8", 128, 0)


class Negation(unittest.TestCase):
    """SPEC-01 4.3."""

    def test_neg(self):
        f = op("neg", "checked", "I64", m)
        self.assertEqual((f.code, f.exact, f.operation), ("E_OVERFLOW", 9223372036854775808, "neg.checked.i64"))
        f = op("neg", "checked", "U32", 1)
        self.assertEqual((f.code, f.exact, f.limit.value), ("E_OVERFLOW", -1, 0))
        self.assertEqual(val(op("neg", "checked", "U32", 0)), 0)

    def test_wrap_and_sat_negation_via_zero(self):
        self.assertEqual(val(op("sub", "wrap", "I64", 0, m)), m)
        self.assertEqual(val(op("sub", "sat", "I64", 0, m)), M)

    def test_abs_uabs(self):
        f = op("abs", "checked", "I8", -128)
        self.assertEqual((f.code, f.exact, f.limit.value, f.operation), ("E_OVERFLOW", 128, 127, "abs.checked.i8"))
        r = op("uabs", "checked", "I8", -128)
        self.assertEqual((r.type, r.value), ("U8", 128))
        r = op("uabs", "checked", "I64", m)
        self.assertEqual((r.type, r.value), ("U64", 9223372036854775808))


class Division(unittest.TestCase):
    """SPEC-01 4.4 example table (I64) and fixtures 20 to 35."""

    TABLE = [
        (7, 2, 3, 1, 3, 1, 3, 1),
        (-7, 2, -4, 1, -3, -1, -4, 1),
        (7, -2, -4, -1, -3, 1, -3, 1),
        (-7, -2, 3, -1, 3, -1, 4, 1),
        (6, -3, -2, 0, -2, 0, -2, 0),
        (m, 2, -4611686018427387904, 0, -4611686018427387904, 0, -4611686018427387904, 0),
        (m, 3, -3074457345618258603, 1, -3074457345618258602, -2, -3074457345618258603, 1),
    ]
    NAMES = ["div", "rem", "div_trunc", "rem_trunc", "div_euclid", "rem_euclid"]

    def test_table(self):
        for a, b, *expected in self.TABLE:
            for name, e in zip(self.NAMES, expected):
                with self.subTest(a=a, b=b, name=name):
                    self.assertEqual(val(op(name, "checked", "I64", a, b)), e)

    def test_min_by_minus_one(self):
        for name in self.NAMES:
            r = op(name, "checked", "I64", m, -1)
            if name.startswith("div"):
                self.assertEqual((r.code, r.exact, r.limit.value), ("E_OVERFLOW", 9223372036854775808, M))
                self.assertEqual(r.operation, name + ".checked.i64")
            else:
                self.assertEqual(val(r), 0)

    def test_divide_by_zero(self):
        for name in self.NAMES:
            r = op(name, "checked", "I64", 5, 0)
            self.assertEqual(r.code, "E_DIV_ZERO")
            self.assertIsNone(r.exact)
            self.assertIsNone(r.limit)
        self.assertEqual(op("rem_euclid", "checked", "I64", 0, 0).code, "E_DIV_ZERO")
        self.assertEqual(op("div", "checked", "I64", 0, 0).code, "E_DIV_ZERO")

    def test_unsigned(self):
        for name in self.NAMES:
            self.assertEqual(val(op(name, "checked", "U8", 255, 7)), 36 if name.startswith("div") else 3)

    def test_no_wrapping_division(self):
        with self.assertRaises(ValueError):
            op("div", "wrap", "I64", 1, 1)


class Rounding(unittest.TestCase):
    """SPEC-01 4.5: the mode table, applied through div_round."""

    # x as a/b, then floor ceil trunc away half_even half_away half_trunc half_up half_down
    MODES = ["floor", "ceil", "trunc", "away", "half_even", "half_away", "half_trunc", "half_up", "half_down"]
    TABLE = {
        (5, 2): [2, 3, 2, 3, 2, 3, 2, 3, 2],          # 2.5
        (-5, 2): [-3, -2, -2, -3, -2, -3, -2, -2, -3],  # -2.5
        (12, 5): [2, 3, 2, 3, 2, 2, 2, 2, 2],          # 2.4
        (-13, 5): [-3, -2, -2, -3, -3, -3, -3, -3, -3],  # -2.6
        (7, 2): [3, 4, 3, 4, 4, 4, 3, 4, 3],           # 3.5
    }

    def test_mode_table(self):
        for (a, b), row in self.TABLE.items():
            for mode, e in zip(self.MODES, row):
                with self.subTest(a=a, b=b, mode=mode):
                    self.assertEqual(val(op("div_round", "checked", "I64", a, b, mode=mode)), e)
                    # the same rational with a negative divisor rounds the same way
                    self.assertEqual(val(op("div_round", "checked", "I64", -a, -b, mode=mode)), e)

    def test_div_round_table(self):
        self.assertEqual(val(op("div_round", "checked", "I64", -5, 2, mode="half_away")), -3)
        f = op("div_round", "checked", "I64", m, -1, mode="floor")
        self.assertEqual((f.code, f.exact, f.operation), ("E_OVERFLOW", 9223372036854775808, "div_round.checked.i64.floor"))
        f = op("div_round", "checked", "I64", 1, 0, mode="half_even")
        self.assertEqual(f.code, "E_DIV_ZERO")

    def test_mode_required_and_known(self):
        with self.assertRaises(ValueError):
            op("div_round", "checked", "I64", 1, 2)
        with self.assertRaises(ValueError):
            op("div_round", "checked", "I64", 1, 2, mode="toward_zero")


class Shifts(unittest.TestCase):
    """SPEC-01 4.6 table and fixtures 38 to 47."""

    def test_table(self):
        self.assertEqual(val(op("shl", "checked", "I64", 1, 62)), 4611686018427387904)
        f = op("shl", "checked", "I64", 1, 63)
        self.assertEqual((f.code, f.exact, f.limit.value), ("E_OVERFLOW", 9223372036854775808, M))
        self.assertEqual(val(op("shl", "checked", "I64", -1, 63)), m)
        self.assertEqual(val(op("shl", "wrap", "I64", 1, 63)), m)
        f = op("shl", "checked", "I64", 1, 64)
        self.assertEqual((f.code, f.limit.value, f.exact), ("E_SHIFT", 63, None))
        self.assertEqual(op("shl", "checked", "I64", 1, -1).code, "E_SHIFT")
        self.assertEqual(op("shl", "wrap", "I64", 1, -1).code, "E_SHIFT")
        self.assertEqual(val(op("shl", "wrap", "U8", 255, 4)), 240)
        self.assertEqual(val(op("shr", "checked", "I64", -7, 1)), -4)
        self.assertEqual(val(op("shr", "checked", "I64", -1, 63)), -1)
        self.assertEqual(val(op("shr", "checked", "U64", 18446744073709551615, 63)), 1)
        self.assertEqual(op("shr", "checked", "I64", 1, 64).code, "E_SHIFT")

    def test_count_keeps_its_own_type(self):
        f = op("shl", "checked", "U8", 1, Value("U8", 8))
        self.assertEqual(f.code, "E_SHIFT")
        self.assertEqual([(o.type, o.value) for o in f.operands], [("U8", 1), ("U8", 8)])
        self.assertEqual(f.operation, "shl.checked.u8")
        self.assertEqual(f.limit.value, 7)

    def test_count_width_minus_one_and_width_plus_one(self):
        for t, w in (("I8", 8), ("U16", 16), ("I32", 32), ("U64", 64)):
            self.assertIsInstance(op("shl", "wrap", t, 1, w - 1), Value)
            self.assertEqual(op("shr", "checked", t, 1, w).code, "E_SHIFT")
            self.assertEqual(op("shr", "checked", t, 1, w + 1).code, "E_SHIFT")

    def test_rotations(self):
        self.assertEqual(val(op("rotl", "checked", "U32", 0x80000001, 1)), 3)
        self.assertEqual(val(op("rotr", "checked", "U32", 3, 1)), 0x80000001)
        with self.assertRaises(ValueError):
            op("rotl", "checked", "I32", 1, 1)


class Bitwise(unittest.TestCase):
    """SPEC-01 4.7."""

    def test_ops(self):
        self.assertEqual(val(op("and", "checked", "I8", -1, 0x0F)), 15)
        self.assertEqual(val(op("or", "checked", "I8", -128, 1)), -127)
        self.assertEqual(val(op("xor", "checked", "U8", 0xFF, 0x0F)), 0xF0)
        self.assertEqual(val(op("not", "checked", "I64", 5)), -6)
        self.assertEqual(val(op("not", "checked", "U8", 5)), 250)


class Conversions(unittest.TestCase):
    """SPEC-01 4.9 table and fixtures 48 to 53."""

    def test_table(self):
        self.assertEqual(val(op("as", "checked", "I64", 2147483647, target="I32")), 2147483647)
        f = op("as", "checked", "I64", 2147483648, target="I32")
        self.assertEqual((f.code, f.exact, f.limit.type, f.limit.value, f.operation),
                         ("E_NARROW", 2147483648, "I32", 2147483647, "as.checked.i64.i32"))
        self.assertEqual(val(op("as", "wrap", "I64", 2147483648, target="I32")), -2147483648)
        f = op("as", "checked", "I64", -1, target="U64")
        self.assertEqual((f.code, f.exact, f.limit.value), ("E_NARROW", -1, 0))
        self.assertEqual(val(op("as", "wrap", "I64", -1, target="U64")), 18446744073709551615)
        self.assertEqual(op("as", "checked", "U64", 9223372036854775808, target="I64").code, "E_NARROW")
        self.assertEqual(val(op("as", "wrap", "U64", 9223372036854775808, target="I64")), m)
        self.assertEqual(op("as", "checked", "I64", -129, target="I8").code, "E_NARROW")
        self.assertEqual(val(op("as", "wrap", "I64", -129, target="I8")), 127)
        r = op("as", "checked", "I64", m, target="I128")
        self.assertEqual((r.type, r.value), ("I128", m))

    def test_bool_source(self):
        r = op("as", "checked", "Bool", True, target="I64")
        self.assertEqual((r.type, r.value), ("I64", 1))


class NamedWide(unittest.TestCase):
    """SPEC-01 4.10 and 4.8."""

    def test_muldiv(self):
        self.assertEqual(val(op("muldiv", "checked", "I64", M, M, M, mode="floor", target="I64")), M)
        f = op("muldiv", "checked", "I64", M, 3, 2, mode="floor", target="I64")
        self.assertEqual((f.code, f.exact, f.operation), ("E_OVERFLOW", 13835058055282163710, "muldiv.checked.i64.i64.floor"))
        f = op("muldiv", "checked", "I64", M, 3, 2, mode="ceil", target="I64")
        self.assertEqual(f.exact, 13835058055282163711)
        self.assertEqual(op("muldiv", "checked", "I64", M, 3, 2, mode="half_even", target="I64").exact, 13835058055282163710)
        self.assertEqual(val(op("muldiv", "checked", "I64", 7, 5, 2, mode="half_even", target="I64")), 18)
        self.assertEqual(val(op("muldiv", "checked", "I64", 9, 5, 2, mode="half_even", target="I64")), 22)

    def test_mul_full(self):
        r = op("mul_full", "checked", "I64", m, m)
        self.assertEqual((r.type, r.value), ("I128", 85070591730234615865843651857942052864))
        r = op("mul_full", "checked", "I64", M, M)
        self.assertEqual(r.value, 85070591730234615847396907784232501249)

    def test_isqrt(self):
        self.assertEqual(val(op("isqrt", "checked", "I128", 2**65)), 6074000999)
        self.assertEqual(val(op("isqrt_round", "checked", "I128", 2**65, mode="half_even")), 6074001000)
        self.assertEqual(val(op("isqrt_round", "checked", "I64", M, mode="ceil")), 3037000500)
        f = op("isqrt", "checked", "I64", -1)
        self.assertEqual((f.code, f.exact, f.limit), ("E_DOMAIN", None, None))   # interim decision OQ-01

    def test_min_max_clamp(self):
        self.assertEqual(val(op("min", "checked", "I64", 3, -4)), -4)
        self.assertEqual(val(op("max", "checked", "U8", 3, 4)), 4)
        self.assertEqual(val(op("clamp", "checked", "I32", 50, 0, 10)), 10)

    def test_clamp_inverted_bounds_is_open(self):
        from cint_ref.arith import OpenCase
        with self.assertRaises(OpenCase):
            op("clamp", "checked", "I32", 5, 10, 0)

    def test_add_wide(self):
        f = op("add", "checked", "I128", 2**127 - 1, 1)
        self.assertEqual(f.exact, 170141183460469231731687303715884105728)


if __name__ == "__main__":
    unittest.main()
