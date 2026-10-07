"""Tests for t27_ref.py, the independent reference of t27 set 1 (TT-05).

Every expected value below is taken from the SPEC-05 text: section 2 (the
values of R(n) and the trit capacities K), 3.3 (literals and formatting), the
tables of 4.3, 5.2, 5.3 and 11.4, the examples of 4.4, 6 and TR-DOT-7, the
fixtures of 12.2, the bound table of 7.3, and the lane bounds of TR-IMP-2. The
SplitMix64 finalizer is checked against a known value. The last class compares
the regenerated outcomes with the frozen `conformance/ternary/*.expect` files.

Run from the repository root:
    python -m unittest discover -s conformance/tools -p "test_*.py" -v
"""

import os
import re
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
ROOT = os.path.dirname(os.path.dirname(HERE))

import t27_ref as t  # noqa: E402

M = 3812798742493
I64_MAX = 9223372036854775807
I64_MIN = -9223372036854775808
U64_MAX = 18446744073709551615


class DigitDefinitions(unittest.TestCase):
    """SPEC-05 sections 2, 3.3, 4 and 6."""

    def test_r_values_and_capacities(self):
        self.assertEqual(t.M, M)
        self.assertEqual(t.P27, 7625597484987)
        self.assertEqual(t.R(26), 1270932914164)
        for n, r in ((5, 121), (6, 364), (10, 29524), (11, 88573), (20, 1743392200),
                     (21, 5230176601), (40, 6078832729528464400),
                     (41, 18236498188585393201), (42, 54709494565756179604)):
            self.assertEqual(t.R(n), r, n)
        for ty, k in (("I8", 6), ("I16", 11), ("I32", 21), ("I64", 41), ("U64", 42),
                      ("I128", 81), ("T27", 27)):
            self.assertEqual(t.capacity(ty), k, ty)

    def test_two_digit_derivations_agree(self):
        for x in range(-t.R(7), t.R(7) + 1):
            ts = t.trits(x, 7)
            self.assertEqual(ts, t.trits_by_residue(x, 7))
            self.assertEqual(t.value_of(ts), x)
            self.assertTrue(all(d in (-1, 0, 1) for d in ts))
        for x in (I64_MAX, I64_MIN, M, -M, t.R(40), -t.R(40) - 1, 0):
            self.assertEqual(t.trits(x, 41), t.trits_by_residue(x, 41))
        self.assertRaises(ValueError, t.trits, t.R(5) + 1, 5)

    def test_literals_and_format(self):
        # TR-LIT-1, TR-FMT-2, SPEC-04 LS-205, fixtures LIT-01, LIT-02, LIT-04, FMT-01.
        self.assertEqual(t.parse_t("0tP0N"), 8)
        self.assertEqual(t.parse_t("0tN"), -1)
        self.assertEqual(t.parse_t("0tPNN"), 5)
        self.assertEqual(t.parse_t("0t" + "P" * 27), 3812798742493)
        self.assertEqual(t.parse_t("0tPPPPP"), 121)
        self.assertFalse(t.in_range("I8", t.parse_t("0tPPPPPP")))
        self.assertEqual(t.parse_t("0tNPP"), -5)
        self.assertEqual(t.parse_t("0t00P0N"), 8)
        self.assertEqual([t.fmt_t(v) for v in (8, 0, -5, -8)], ["0tP0N", "0t0", "0tNPP", "0tN0P"])
        for v in range(-400, 401):
            self.assertEqual(t.parse_t(t.fmt_t(v)), v)

    def test_trits_of_a_million(self):
        # Section 6: trits(1000000, 27).
        want = [1, 0, 0, -1, 1, -1, 1, 1, -1, 0, -1, 0, -1, 1] + [0] * 13
        self.assertEqual(t.trits(1000000, 27), want)

    def test_rescale3_is_the_unique_quotient(self):
        # TR-DIG-4, 4.2 P4 and P5.
        for k in range(6):
            p = 3 ** k
            for x in range(-300, 301):
                near = x // p
                qs = [q for q in range(near - 3, near + 4) if -t.R(k) <= x - q * p <= t.R(k)]
                self.assertEqual(qs, [t.rescale3(x, k)], (x, k))
                self.assertEqual(t.rescale3(-x, k), -t.rescale3(x, k))
                self.assertEqual(t.rescale3_by_deletion(x, k), t.rescale3(x, k))
                self.assertEqual(t.rescale3_9_4(x, k), t.rescale3(x, k))
                self.assertEqual(t.rescale3(t.rescale3(x, 2), k), t.rescale3(x, k + 2))

    def test_table_4_3(self):
        rows = (
            (5, 1, 2, -1, 1, 1), (-5, 1, -2, 1, -1, -2), (4, 1, 1, 1, 1, 1),
            (-4, 1, -1, -1, -1, -2), (13, 2, 1, 4, 1, 1), (14, 2, 2, -4, 1, 1),
            (-14, 2, -2, 4, -1, -2), (8, 0, 8, 0, 8, 8),
            (M, 26, 1, 1270932914164, 1, 1), (-M, 26, -1, -1270932914164, -1, -2),
            (M, 27, 0, M, 0, 0),
            (I64_MAX, 40, 1, -2934293422202152994, 0, 0),
            (I64_MIN, 40, -1, 2934293422202152993, 0, -1),
            (I64_MIN, 41, 0, I64_MIN, 0, -1),
            (2 ** 63, 40, 1, None, 0, 0), (U64_MAX, 40, 2, None, 1, 1), (U64_MAX, 41, 1, None, 0, 0),
            (127, 5, 1, -116, 0, 0), (121, 5, 0, 121, 0, 0), (122, 5, 1, -121, 0, 0),
        )
        for x, k, q, r, trunc, floor in rows:
            self.assertEqual(t.rescale3(x, k), q, (x, k))
            if r is not None:
                self.assertEqual(t.rescale3_rem(x, k), r, (x, k))
            self.assertEqual(x // 3 ** k, floor, (x, k))
            self.assertEqual((abs(x) // 3 ** k) * (1 if x >= 0 else -1), trunc, (x, k))
        for x, digits in ((5, "PNN"), (-5, "NPP"), (14, "PNNN"), (-14, "NPPP"), (8, "P0N"),
                          (127, "PNNN0P"), (121, "PPPPP"), (122, "PNNNNN")):
            self.assertEqual(t.fmt_t(x), "0t" + digits)
        self.assertEqual(t.width(I64_MAX), 41)
        self.assertEqual(t.width(I64_MIN), 41)
        self.assertEqual(t.width(2 ** 63), 41)
        self.assertEqual(t.width(U64_MAX), 42)      # the 4.3 row for k = 41; its k = 40 row says 41

    def test_section_4_4_examples(self):
        self.assertEqual(t.typed_rescale3("I64", 5, 1), ("value", 2))
        self.assertEqual(t.typed_rescale3("I64", -5, 1), ("value", -2))
        self.assertEqual(5 // 3, 1)
        self.assertEqual(t.typed_rescale3("T27", M, 27), ("value", 0))
        self.assertEqual(t.typed_shl3("T27", 1, 26), ("value", 2541865828329))
        self.assertEqual(t.typed_shl3("T27", 2541865828329, 1), ("fault", "E_OVERFLOW", 7625597484987))
        self.assertEqual(t.typed_rescale3("I64", 1, 42), ("fault", "E_SHIFT"))
        self.assertEqual(t.typed_trit("T27", M, 26), ("value", 1))
        self.assertEqual(t.typed_trit("T27", M, 27), ("fault", "E_SHIFT"))
        self.assertEqual(t.typed_rescale3("I128", 1, 81), ("value", 0))
        self.assertEqual(t.typed_rescale3("I128", 1, 82), ("fault", "E_SHIFT"))
        self.assertEqual(t.typed_shl3("T27", 1, 27), ("fault", "E_OVERFLOW", 7625597484987))
        self.assertEqual(t.typed_shl3("T27", 0, 27), ("value", 0))

    def test_t27_arithmetic(self):
        # TR-T27-2, TR-T27-3, fixtures TR27-01 to TR27-09.
        self.assertEqual(t.t27("+", M, 0), ("value", M))
        self.assertEqual(t.t27("+", M, 1)[:2], ("fault", "E_OVERFLOW"))
        self.assertEqual(t.t27("-", -M, 1)[:2], ("fault", "E_OVERFLOW"))
        self.assertEqual(t.t27("neg", -M), ("value", M))
        self.assertEqual(t.t27("+%", M, 1), ("value", -M))
        self.assertEqual(t.t27("+|", M, 1), ("value", M))
        self.assertEqual(t.t27("*", 1270932914165, 3), ("fault", "E_OVERFLOW", 3812798742495))
        self.assertEqual(t.t27("as", 3812798742494)[:2], ("fault", "E_NARROW"))
        self.assertEqual(t.t27("as%", 3812798742494), ("value", -3812798742493))
        self.assertEqual(t.t27_add_with_carry(M, 1), (-M, 1))
        self.assertEqual(t.t27_add_with_carry(-M, -1), (M, -1))
        self.assertEqual(t.ripple27(M, 1), (-M, 1))
        self.assertEqual(t.ripple27(-M, -M), (1, -1))

    def test_conversions(self):
        # Fixtures CONV-02 and CONV-03.
        self.assertEqual(t.from_trits("I64", [1] * 41), ("fault", "E_NARROW", 18236498188585393201))
        self.assertEqual(t.from_trits("I64", [1] * 40), ("value", 6078832729528464400))


class PackedEncodings(unittest.TestCase):
    """SPEC-05 5.2 to 5.4 and 11.4."""

    def test_table_5_2(self):
        for ts, want in (([1, 0, -1, 1, 1], [0x64]), ([1, 1, 1, 1, 1], [0x79]),
                         ([-1, -1, -1, -1, -1], [0x87]), ([0, 0, 0, 0, 0], [0x00]),
                         ([1, 1, 1, 1, 1, -1, 1], [0x79, 0x02]), ([-1, -1, -1, -1, -1, 1], [0x87, 0x01]),
                         ([1, -1, 0, 1, 1], [0x6a]), ([1, 1, -1, -1, 0], [0xe0])):
            self.assertEqual(t.pack5(ts), want, ts)
        for v, want in ((0, [0, 0, 0, 0, 0, 0]), (8, [8, 0, 0, 0, 0, 0]),
                        (M, [0x79, 0x79, 0x79, 0x79, 0x79, 0x04]),
                        (-M, [0x87, 0x87, 0x87, 0x87, 0x87, 0xfc]),
                        (1000000, [0x37, 0xf0, 0x11, 0x00, 0x00, 0x00])):
            self.assertEqual(t.pack5(t.trits(v, 27)), want, v)

    def test_pt5_validity(self):
        # TR-PT5-1 to TR-PT5-3.
        unused = [u for u in range(256) if t.pt5_decode_byte(u) is None]
        self.assertEqual(unused, [0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86])
        for u in range(256):
            ts = t.pt5_decode_byte(u)
            if ts is not None:
                self.assertEqual(t.pt5_byte(ts), u)
        for r, bound in ((1, 1), (2, 4), (3, 13), (4, 40)):
            self.assertEqual(t.R(r), bound)
            valid = [u for u in range(256) if t.pt5_decode_byte(u) is not None and abs(t.signed8(u)) <= bound]
            self.assertEqual(len(valid), 3 ** r)
        self.assertEqual(sorted(u for u in range(256) if abs(t.signed8(u)) <= 1), [0x00, 0x01, 0xff])

    def test_pt5_fixtures(self):
        self.assertEqual(t.decode_pt5([0x79, 0x03], 6), ("invalid", 1, "nonzero padding"))   # PT5-09
        self.assertEqual(t.decode_pt5([0x79, 0x01], 6), ("valid", [1, 1, 1, 1, 1, 1]))      # PT5-10
        self.assertEqual(t.decode_pt5([0x00, 0x29], 9), ("invalid", 1, "nonzero padding"))   # PT5-11
        self.assertEqual(t.decode_pt5([0, 0, 0], 9), ("shape",))                            # PT5-12
        self.assertEqual(t.decode_pt5([0x00, 0x7a, 0x7a], 15), ("invalid", 1, "unused value"))  # PT5-13
        for u in range(0x7a, 0x87):                                                          # PT5-07
            self.assertEqual(t.decode_pt5([u], 5), ("invalid", 0, "unused value"))
        # TR-VIEW-4: the bytes 79 79 are valid with row length 10 and not with 7.
        self.assertEqual(t.decode_pt5([0x79, 0x79], 10)[0], "valid")
        self.assertEqual(t.decode_pt5([0x79, 0x79], 7), ("invalid", 1, "nonzero padding"))

    def test_table_5_3(self):
        for ts, want in (([1, -1, 0, 1], [0x4d]), ([1, -1, 0, 1, -1], [0x4d, 0x03]),
                         ([1, 0, -1, 1, 1], [0x71, 0x01]), ([1, 1, 1, 1, 1, -1, 1], [0x55, 0x1d]),
                         ([-1, -1, -1, -1, -1, 1], [0xff, 0x07]), ([0, 0, 0, 0, 0], [0x00, 0x00])):
            self.assertEqual(t.pack4(ts), want, ts)

    def test_pt4_fields_and_fixtures(self):
        # TR-PT4-2: the field table read as 2-bit two's complement and as the 9.1 plane path.
        self.assertEqual(t.PT4_TRIT, {0b00: 0, 0b01: 1, 0b11: -1})
        for f, tc in ((0b00, 0), (0b01, 1), (0b11, -1), (0b10, -2)):
            self.assertEqual(t.wrap_signed(f, 2), tc)
        self.assertEqual(t.PT4_PLANE_READ[0b10], 0)
        valid = [u for u in range(256) if 0b10 not in t.pt4_fields(u)]
        self.assertEqual(len(valid), 81)
        self.assertEqual(t.decode_pt4([0x02], 4), ("invalid", 0, "invalid field"))         # PT4-03
        self.assertEqual(t.decode_pt4([0x4d, 0x07], 5), ("invalid", 1, "nonzero padding"))  # PT4-04
        self.assertEqual(t.repack5([0x71, 0x01], 5), [0x64])                                # PT4-05
        for u in valid:                                                                      # PT4-06
            v = t.decode_pt4([u], 4)
            self.assertEqual(v[0], "valid")
            self.assertEqual(t.pack4(v[1]), [u])

    def test_table_11_4(self):
        for v, legacy, core in (
                (0, [0x79, 0x79, 0x79, 0x79, 0x79, 0x04], [0, 0, 0, 0, 0, 0]),
                (M, [0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0x08], [0x79, 0x79, 0x79, 0x79, 0x79, 0x04]),
                (-M, [0, 0, 0, 0, 0, 0], [0x87, 0x87, 0x87, 0x87, 0x87, 0xfc]),
                (1000000, [0xb0, 0x69, 0x8a, 0x79, 0x79, 0x04], [0x37, 0xf0, 0x11, 0x00, 0x00, 0x00])):
            self.assertEqual(t.legacy_word(v), legacy, v)
            self.assertEqual(t.legacy_to_core(legacy), core, v)
            self.assertEqual(t.pack5(t.trits(v, 27)), core, v)


class DotProductsAndLanes(unittest.TestCase):
    """SPEC-05 7 and 9.2."""

    def test_tdot_fixtures(self):
        self.assertEqual(t.tdot("I32", [], []), ("value", 0))                                   # TDOT-01
        self.assertEqual(t.tdot("I32", [1, 0, 1, 0], [1, 2, 3, 4, 5]), ("fault", "E_SHAPE"))    # TDOT-02
        self.assertEqual(t.tdot("I8", [1, 1, -1], [100, 100, 100]), ("value", 100))             # TDOT-03
        self.assertEqual(t.tdot("I8", [-1], [-128]), ("fault", "E_OVERFLOW", 128))              # TDOT-04
        self.assertEqual(t.tdot("I16", [1, -1, 0, 1, 1], [1, 1, -1, -1, 0]), ("value", -1))     # TDOT-05
        self.assertEqual(t.dot_by_planes([1, -1, 0, 1, 1], [1, 1, -1, -1, 0]), -1)              # TR-DOT-7

    def test_bound_table_7_3(self):
        a_max = {"I8": 127, "I16": 32767, "I32": 2147483647, "I64": 9223372036854775807}
        rows = ((1, (127, 32767, 2147483647, 9223372036854775807)),
                (128, (0, 255, 16777215, 72057594037927935)),
                (127, (1, 258, 16909320, 72624976668147841)),
                (32768, (0, 0, 65535, 281474976710655)),
                (2 ** 31, (0, 0, 0, 4294967295)))
        for mx, wants in rows:
            for ty, want in zip(("I8", "I16", "I32", "I64"), wants):
                self.assertEqual(a_max[ty] // mx, want, (mx, ty))
        # TDOT-07 to TDOT-10 by Theorem T1's arithmetic: S = n w x.
        self.assertTrue(t.in_range("I32", 16777215 * 128))
        self.assertFalse(t.in_range("I32", 16777216 * 128))
        self.assertTrue(t.in_range("I32", 16777216 * -128))
        self.assertEqual(16909320 * 127, 2147483640)

    def test_lane_bounds(self):
        # TR-IMP-2: j P <= 2^15 - 1, direct P = mx, offset P = 2 mx.
        self.assertEqual(t.lane_bound(128), 255)
        self.assertEqual(t.lane_bound(127), 258)
        self.assertEqual(t.lane_bound(256), 127)
        self.assertEqual(t.lane_bound(254), 129)
        for p, j in ((128, 255), (127, 258), (256, 127), (254, 129)):
            self.assertLessEqual(j * p, 32767)
            self.assertGreater((j + 1) * p, 32767)

    def test_lane16(self):
        self.assertEqual(t.lane16(32767), 32767)
        self.assertEqual(t.lane16(32768), -32768)
        self.assertEqual(t.lane16(-32769), 32767)
        self.assertEqual(t.lane16(256 * 128), -32768)


class SplitMix64(unittest.TestCase):

    def test_finalizer(self):
        self.assertEqual(t.mix(1), 6238072747940578789)
        # The first output of SplitMix64 from state 0 is the finalizer of the increment.
        self.assertEqual(t.sm(0, 0), 0xE220A8397B1DCDAF)
        self.assertEqual(t.sm(0, 0), t.mix(t.GAMMA))
        self.assertEqual(t.sm(5, 2), t.mix((5 + 3 * 0x9E3779B97F4A7C15) % 2 ** 64))


class FixturesAndOracle(unittest.TestCase):

    def test_spec_values(self):
        self.assertEqual(t.fixture_failures(), [])

    @unittest.skipUnless(os.path.exists(t.ORACLE_PATH), "the legacy oracle is not in this tree")
    def test_legacy_oracle(self):
        self.assertEqual(t.oracle_failures(), [])


def header_claims(name):
    """The return value and line count that a case's `spec outcome` header line states."""
    with open(os.path.join(ROOT, "conformance", "ternary", name + ".ci"), "rb") as fh:
        text = fh.read().decode("utf-8")
    m = re.search(r"^// spec outcome: value I64 (-?[0-9]+) .*; ([0-9]+) output lines$", text, re.M)
    return int(m.group(1)), int(m.group(2))


class CaseRegeneration(unittest.TestCase):

    def test_cases_regenerate(self):
        for name in t.CASES:
            data, ret = t.regenerate(name)
            want_ret, want_lines = header_claims(name)
            self.assertEqual(ret, want_ret, name)
            self.assertEqual(data.count(b"\n"), want_lines, name)
            self.assertTrue(data.endswith(b"mismatches %d\n" % ret), name)
            self.assertNotIn(b"\r", data)
            data.decode("ascii")
            self.assertEqual(t.regenerate(name), (data, ret), name)

    def test_command_line_names(self):
        self.assertEqual(t.case_name("ternary/pt5_table"), "pt5_table")
        self.assertEqual(t.case_name("pt4_planes"), "pt4_planes")
        self.assertEqual(sorted(t.GENERATORS), sorted(t.CASES))


class FrozenOutcomes(unittest.TestCase):
    """The regenerated outcomes against the frozen `.expect` files (the `--check` comparison)."""

    def test_frozen_expect_files(self):
        self.assertEqual(t.check_cases(t.CASES), [])


if __name__ == "__main__":
    unittest.main()
