import os, sys; sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # put ref/ on the path
"""Tests for the unsigned wide types of box 15 (ruling BX15-02, A; SPEC-01 IM-6, IM-16, IM-34,
IM-46, IM-146): `U128`, `U256` and `U512` as values, elements and fault operands; `uabs` of
`I128` to `I512`; `rotl`, `rotr` and logical `>>`; `mul_full` of `U64`, `U128` and `U256`; and
their values in `.expect` format 5 (BX15-03 A; SPEC-09 CONF-11 rule 14)."""
import unittest

from cint_ref import arith, expect, faults
from cint_ref.exec import run_program
from cint_ref.types import INT_TYPES, UNSIGNED_WIDE_INT_NAMES, Value

WIDTHS = {name: INT_TYPES[name] for name in UNSIGNED_WIDE_INT_NAMES}


def run(src, entry="run"):
    return run_program(src.encode("utf-8"), "t.ci", entry, None)


def printed(src):
    o = run(src)
    assert o.kind == "value", (o.kind, o.diagnostic, o.record, o.message)
    return o.stdout.decode("ascii").split()


class Types(unittest.TestCase):
    def test_ranges_and_u1024_stays_out(self):
        for name, t in WIDTHS.items():
            with self.subTest(type=name):
                self.assertEqual((t.min, t.max), (0, (1 << t.width) - 1))
        self.assertNotIn("U1024", INT_TYPES)
        o = run("export I64 run() {\n    U1024 x = 1;\n    return 0;\n}\n")
        self.assertEqual(o.kind, "compile-error")

    def test_checked_wrap_sat(self):
        for name, t in WIDTHS.items():
            with self.subTest(type=name):
                out = printed("export I64 run() {\n    %s a = %s.max;\n    %s b = 3;\n"
                              "    \"{a +%% b} {a +| b} {b -| a} {b - 3}\\n\";\n    return 0;\n}\n"
                              % (name, name, name))
                self.assertEqual(out, ["2", str(t.max), "0", "0"])

    def test_fault_record_has_unsigned_wide_operands(self):
        o = run("export I64 run() {\n    U256 a = 1;\n    U256 b = a - 2;\n    return 0;\n}\n")
        r = o.record
        self.assertEqual((r.code, r.operation), ("E_OVERFLOW", "sub.checked.u256"))
        self.assertEqual([v.render() for v in r.operands], ["U256 1", "U256 2"])
        self.assertEqual((r.exact, r.limit.render()), (-1, "U256 0"))

    def test_tags_round_trip_through_a_record(self):
        # IM-146: U128 to U512 are tags 25 to 27.
        for tag, name in ((0x25, "U128"), (0x26, "U256"), (0x27, "U512")):
            with self.subTest(type=name):
                data = faults.encode_value(Value(name, WIDTHS[name].max))
                self.assertEqual((data[0], len(data)), (tag, 1 + WIDTHS[name].width // 8))
        r = run("export I64 run() {\n    U512 a = 0;\n    U512 b = a - 1;\n    return 0;\n}\n").record
        back = faults.decode_fault_record(faults.encode_fault_record(r, faults.RUN_TIME), faults.RUN_TIME)[1]
        self.assertEqual(back.operands, r.operands)


class Operations(unittest.TestCase):
    def test_uabs_of_signed_wide(self):
        out = printed("export I64 run() {\n    \"{uabs(I128.min)} {uabs(I256.min)} {uabs(I512.min + 1)}\\n\";\n"
                      "    return 0;\n}\n")
        self.assertEqual(out, [str(1 << 127), str(1 << 255), str((1 << 511) - 1)])
        o = run("export I64 run() {\n    I1024 a = -1;\n    _ = uabs(a);\n    return 0;\n}\n")
        self.assertEqual((o.kind, o.diagnostic.code), ("compile-error", "C2103"))

    def test_rotations_and_logical_shift(self):
        out = printed("export I64 run() {\n    U128 one = 1;\n    I256 m = -1;\n"
                      "    \"{rotl(one, 127)} {rotr(one, 1)} {rotl(U512.max - 1, 1)} {(m as% U256) >> 255}\\n\";\n"
                      "    return 0;\n}\n")
        self.assertEqual(out, [str(1 << 127), str(1 << 127), str((1 << 512) - 3), "1"])
        r = run("export I64 run() {\n    U256 a = 1;\n    I64 k = 256;\n    _ = rotl(a, k);\n    return 0;\n}\n").record
        self.assertEqual((r.code, r.operation, r.limit.render()), ("E_SHIFT", "rotl.checked.u256", "I64 255"))

    def test_mul_full(self):
        m64, m128, m256 = (1 << 64) - 1, (1 << 128) - 1, (1 << 256) - 1
        self.assertEqual(arith.op("mul_full", "checked", "U64", Value("U64", m64), Value("U64", m64)),
                         Value("U128", m64 * m64))
        self.assertEqual(arith.op("mul_full", "checked", "U128", Value("U128", m128), Value("U128", m128)),
                         Value("U256", m128 * m128))
        self.assertEqual(arith.op("mul_full", "checked", "U256", Value("U256", m256), Value("U256", m256)),
                         Value("U512", m256 * m256))
        o = run("export I64 run() {\n    U512 a = 2;\n    _ = mul_full(a, a);\n    return 0;\n}\n")
        self.assertEqual((o.kind, o.diagnostic.code), ("compile-error", "C2103"))

    def test_conversions(self):
        out = printed("export I64 run() {\n    U512 a = U512.max;\n    I64 n = -1;\n"
                      "    \"{a as% U128} {(n as% U128) == U128.max} {(U128.max as U256) + 1}\\n\";\n"
                      "    return 0;\n}\n")
        self.assertEqual(out, [str((1 << 128) - 1), "true", str(1 << 128)])

    def test_arrays_and_sum(self):
        out = printed("export I64 run() {\n    U256[3] a = [U256.max, U256.max, 1];\n"
                      "    \"{sum(U512, a)} {sum_wrap(a)} {max(a)}\\n\";\n    return 0;\n}\n")
        m = (1 << 256) - 1
        self.assertEqual(out, [str(2 * m + 1), str((2 * m + 1) % (1 << 256)), str(m)])


class ExpectFormats(unittest.TestCase):
    SOURCES = ("export U128 run() {\n    return 5;\n}\n",
               "export I64 run() {\n    U512 a = 0;\n    U512 b = a - 1;\n    return 0;\n}\n",
               "U256 s = 1;\n\nexport I64 run() {\n    s = 2;\n    return 0;\n}\n",
               "const U256 K = U256.max + 1;\n\nexport I64 run() {\n    return 0;\n}\n")

    def test_unsigned_wide_values_are_format_5(self):
        # SPEC-09 CONF-11 rule 14 (BX15-03 A): U128 to U512 are .expect types of format 5 only.
        for fmt in (1, 2, 3, 4):
            for src in self.SOURCES[:2] + self.SOURCES[3:] if fmt == 1 else self.SOURCES:  # no state in format 1
                with self.subTest(fmt=fmt, src=src):
                    with self.assertRaisesRegex(ValueError, "format 5"):
                        expect.from_outcome(run(src), "t", fmt=fmt)

    def test_format_5_text_and_reader(self):
        lines = [expect.render(expect.from_outcome(run(src), "t", "x", fmt=5)) for src in self.SOURCES]
        self.assertIn("format 5\n", lines[0])
        self.assertIn("return U128 5\n", lines[0])
        self.assertIn("fault.operand U512 0\nfault.operand U512 1\nfault.exact -1\nfault.limit U512 0\n", lines[1])
        self.assertIn("state.global t s U256 2\n", lines[2])
        self.assertIn("diagnostic.fault.limit U256 %d\n" % ((1 << 256) - 1), lines[3])
        for text in lines:
            self.assertEqual(expect.render(expect.parse(text)), text)
            with self.assertRaisesRegex(ValueError, "rule 14"):
                expect.parse(text.replace("format 5\n", "format 4\n"))
        fixed = lines[0].replace("U128 5", "Q16.16 raw 98304")
        self.assertEqual(expect.parse(fixed).format, 5)
        with self.assertRaisesRegex(ValueError, "rule 14"):
            expect.parse(fixed.replace("format 5\n", "format 2\n"))

    def test_printed_values_need_no_new_format(self):
        o = run("export I64 run() {\n    U512 a = U512.max;\n    \"{a}\\n\";\n    return 1;\n}\n")
        text = expect.render(expect.from_outcome(o, "t", fmt=2))
        self.assertIn("return I64 1\n", text)


if __name__ == "__main__":
    unittest.main()
