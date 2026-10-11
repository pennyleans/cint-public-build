import os, sys; sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # put ref/ on the path
"""Fixed point in programs (box 15; SPEC-01 section 5, SPEC-04 sections 3.6, 6.6 and 9): every
expected value is a row or an example of SPEC-01 5.2 to 5.6, SPEC-01 IM-63, IM-64, IM-87 and
IM-106, or SPEC-04 LS-37 and LS-215, run as a program. What box 15 leaves to defaults not yet
ruled (`as|`, the result forms, `!ratio`, a fraction literal as a conversion source) is refused; `sqrt_wrap` and `sqrt_sat`, which SPEC-04 LS-145 does not list,
are undefined names."""
import unittest

from cint_ref import expect, faults
from cint_ref import parser as P
from cint_ref.exec import run_program
from cint_ref.types import Value


def run(src, entry="run"):
    return run_program(src.encode("utf-8"), "t.ci", entry, None)


def body(lines, result="I64"):
    return "export %s run() {\n%s}\n" % (result, "".join("    %s\n" % x for x in lines))


def value(lines, result="I64"):
    o = run(body(lines, result))
    assert o.kind == "value", (o.kind, o.diagnostic, o.record, o.message)
    return o.value


def printed(lines):
    o = run(body(lines + ["return 0;"]))
    assert o.kind == "value", (o.kind, o.diagnostic, o.record, o.message)
    return o.stdout.decode("utf-8")


def fault(lines):
    o = run(body(lines + ["return 0;"]))
    assert o.kind == "fault", (o.kind, o.diagnostic, o.message)
    return o.record


def code(lines):
    o = run(body(lines + ["return 0;"]))
    assert o.kind == "compile-error", (o.kind, o.record, o.message)
    return o.diagnostic


def refused(lines):
    o = run(body(lines + ["return 0;"]))
    return o.kind == "refused"


def raw_of(decl, expr="x"):
    return value([decl, "return %s.raw as I64;" % expr]).value


class Literals(unittest.TestCase):
    def test_grid_and_round_clause(self):                  # SPEC-04 LS-37
        self.assertEqual(raw_of("Q16.16 x = 1.5;"), 98304)
        self.assertEqual(raw_of("Q16.16 x = 3;"), 196608)
        self.assertEqual(raw_of("Q16.16 x = -0.25;"), -16384)
        self.assertEqual(raw_of("Q16.16 x = 0.1 round half_even;"), 6554)
        self.assertEqual(raw_of("Q16.16 x = 0.1 round floor;"), 6553)
        self.assertEqual(raw_of("Q16.16 x = -(0.1 round trunc);"), -6553)
        # A round clause after a negative fraction literal rounds the negative value (OQ-265).
        self.assertEqual(raw_of("Q16.16 x = -0.1 round ceil;"), -6553)
        self.assertEqual(raw_of("Q16.16 x = - /* c */ 0.1 round floor;"), -6554)
        self.assertEqual(raw_of("Q8.0 x = -0.5 round half_up;"), 0)
        self.assertEqual(raw_of("Q8.0 x = -0.5 round half_down;"), -1)
        d = code(["Q8.0 x = -128.5 round floor;"])
        self.assertEqual((d.code, str(d.position)), ("C2003", "t.ci:2:14"))
        d = code(["I64 x = -5 round ceil;"])
        self.assertEqual((d.code, str(d.position)), ("C1050", "t.ci:2:16"))
        d = code(["Q16.16 x = 0.1;"])
        self.assertEqual((d.code, str(d.position)), ("C2006", "t.ci:2:16"))
        self.assertIn("0.1 is not a multiple of 2^-16", d.message)

    def test_range_and_context(self):
        self.assertEqual(code(["Q16.16 x = 40000.0;"]).code, "C2003")   # LS-53
        self.assertEqual(code(["Q8.8 x = 128;"]).code, "C2003")
        self.assertEqual(raw_of("Q8.8 x = -128;"), -32768)
        self.assertEqual(code(["I64 x = 0.5;"]).code, "C2004")          # LS-35
        self.assertEqual(code(["Q17.16 x = 1.0;"]).code, "C2005")        # LS-36
        self.assertEqual(code(["Q16.16 x = 0.5 round nearest;"]).code, "C3007")

    def test_type_properties(self):                        # SPEC-04 LS-51
        self.assertEqual(printed(['"{Q16.16.min} {Q16.16.max} {Q16.16.epsilon}\\n";']),
                         "-32768.0 32767.9999847412109375 0.0000152587890625\n")
        self.assertTrue(refused(["Q16.16 x = Q16.16.bits;"]))

    def test_constants_fold(self):                         # SPEC-01 3.2, 8.4
        o = run("const Q16.16 K = 1.5 * 2.0;\nexport I64 run() {\n    return K.raw as I64;\n}\n")
        self.assertEqual(o.value, Value("I64", 196608))
        d = code(["Q16.16 x = 256.0 * 128.0;"])
        self.assertEqual((d.code, d.fault.operation, d.fault.exact), ("C6001", "mul.checked.q16_16.half_even",
                                                                     2147483648))


class Raw(unittest.TestCase):                              # SPEC-01 IM-57
    def test_raw_and_constructor(self):
        self.assertEqual(raw_of("Q16.16 x = Q16.16.raw(98304);"), 98304)
        self.assertEqual(value(["Q1.15 x = 0.5;", "I16 r = x.raw;", "return r as I64;"]).value, 16384)
        self.assertEqual(code(["I64 n = 1;", "Q16.16 x = Q16.16.raw(n);"]).code, "C2001")
        d = code(["Q16.16 x = 1.0;", "x.raw = 3;"])
        self.assertEqual(d.code, "C2058")


class AddSubtract(unittest.TestCase):                      # SPEC-01 IM-60
    def test_raw_semantics(self):
        r = fault(["Q16.16 a = Q16.16.raw(2147483647);", "Q16.16 b = Q16.16.raw(1);", "Q16.16 c = a + b;"])
        self.assertEqual((r.code, r.operation, r.exact), ("E_OVERFLOW", "add.checked.q16_16", 2147483648))
        self.assertEqual([v.render() for v in r.operands], ["Q16.16 raw 2147483647", "Q16.16 raw 1"])
        self.assertEqual(r.limit.render(), "Q16.16 raw 2147483647")
        self.assertEqual(printed(["Q8.8 a = 127.0;", "Q8.8 b = 1.0;",
                                  '"{a +% b} {a +| b} {-a} {abs(-a)} {min(a, b)} {max(a, b)} {a > b}\\n";']),
                         "-128.0 127.99609375 -127.0 127.0 1.0 127.0 true\n")
        self.assertEqual(code(["Q8.8 a = 1.0;", "Q8.8 b = a % a;"]).code, "C2103")    # IM-66
        self.assertEqual(code(["Q8.8 a = 1.0;", "Q8.8 b = a & a;"]).code, "C2103")
        self.assertEqual(code(["Q8.8 a = 1.0;", "Q16.16 b = 1.0;", "Q8.8 c = a + b;"]).code, "C2001")  # IM-56
        self.assertEqual(code(["Q16.16 a = 1.0;", "I32 b = a;"]).code, "C2001")


class Multiply(unittest.TestCase):                         # SPEC-01 IM-61, IM-62 table
    def mul(self, a, b, call):
        return value(["Q16.16 a = Q16.16.raw(%d);" % a, "Q16.16 b = Q16.16.raw(%d);" % b,
                      "Q16.16 c = %s;" % call, "return c.raw as I64;"]).value

    def test_table(self):
        rows = [(98304, 98304, "a * b", 147456), (1, 98304, "a * b", 2), (1, 98304, "mul(a, b, floor)", 1),
                (1, 98304, "mul(a, b, trunc)", 1), (-1, 98304, "a * b", -2), (-1, 98304, "mul(a, b, trunc)", -1),
                (-1, 98304, "mul(a, b, half_up)", -1), (1, 32768, "a * b", 0), (-1, 32768, "a * b", 0),
                (-1, 32768, "mul(a, b, half_away)", -1), (16777216, 8388608, "a *| b", 2147483647),
                (16777216, 8388608, "mul_sat(a, b, half_even)", 2147483647),
                (16777216, 8388608, "mul_wrap(a, b, half_even)", -2147483648),
                (-16777216, 8388608, "a * b", -2147483648)]
        for a, b, call, want in rows:
            with self.subTest(a=a, b=b, call=call):
                self.assertEqual(self.mul(a, b, call), want)

    def test_overflow_record(self):
        r = fault(["Q16.16 a = 256.0;", "Q16.16 b = 128.0;", "Q16.16 c = a * b;"])
        self.assertEqual((r.code, r.operation, r.exact, r.limit), ("E_OVERFLOW", "mul.checked.q16_16.half_even",
                                                                   2147483648, Value("Q16.16", 2147483647)))
        r = fault(["Q16.16 a = 256.0;", "Q16.16 b = 128.0;", "Q16.16 c = mul(a, b, floor);"])
        self.assertEqual(r.operation, "mul.checked.q16_16.floor")

    def test_on_integers(self):
        self.assertEqual(code(["I64 a = 2;", "I64 b = mul(a, a, floor);"]).code, "C2103")
        self.assertEqual(code(["I64 a = 2;", "I64 b = mul_int(a, 3);"]).code, "C2103")


class Divide(unittest.TestCase):                           # SPEC-01 IM-65, IM-66 table
    def test_table(self):
        rows = [("1.0", "3.0", "a / b", 21845), ("2.0", "3.0", "a / b", 43691), ("-1.0", "3.0", "a / b", -21845),
                ("-1.0", "3.0", "div(a, b, floor)", -21846)]
        for a, b, call, want in rows:
            with self.subTest(a=a, b=b, call=call):
                self.assertEqual(value(["Q16.16 a = %s;" % a, "Q16.16 b = %s;" % b, "Q16.16 c = %s;" % call,
                                        "return c.raw as I64;"]).value, want)
        r = fault(["Q16.16 a = 1.0;", "Q16.16 b = 0.0;", "Q16.16 c = a / b;"])
        self.assertEqual((r.code, r.operation, r.exact, r.limit), ("E_DIV_ZERO", "div.checked.q16_16.half_even",
                                                                   None, None))
        r = fault(["Q16.16 a = -32768.0;", "Q16.16 b = -1.0;", "Q16.16 c = a / b;"])
        self.assertEqual((r.code, r.exact), ("E_OVERFLOW", 2147483648))


class SquareRoot(unittest.TestCase):                       # SPEC-01 IM-67 table
    def test_table(self):
        rows = [("Q16.16", 131072, "floor", 92681), ("Q16.16", 131072, "half_even", 92682),
                ("Q16.16", 32768, "floor", 46340), ("Q16.16", 32768, "half_even", 46341),
                ("Q32.32", 8589934592, "floor", 6074000999), ("Q32.32", 8589934592, "half_even", 6074001000),
                ("Q1.15", 32767, "floor", 32767)]
        for t, a, mode, want in rows:
            with self.subTest(t=t, a=a, mode=mode):
                self.assertEqual(value(["%s a = %s.raw(%d);" % (t, t, a), "%s c = sqrt(a, %s);" % (t, mode),
                                        "return c.raw as I64;"]).value, want)
        self.assertEqual(raw_of("Q16.16 x = sqrt(Q16.16.raw(131072));"), 92682)       # half_even by default
        for mode in ("ceil", "away"):
            r = fault(["Q1.15 a = Q1.15.raw(32767);", "Q1.15 c = sqrt(a, %s);" % mode])
            self.assertEqual((r.code, r.operation, r.exact), ("E_OVERFLOW", "sqrt.checked.q1_15.%s" % mode, 32768))
        r = fault(["Q16.16 a = -1.0;", "Q16.16 c = sqrt(a);"])
        self.assertEqual((r.code, r.operation), ("E_DOMAIN", "sqrt.checked.q16_16.half_even"))
        self.assertEqual(code(["I64 a = sqrt(4);"]).code, "C2103")
        # LS-145 lists no `sqrt_wrap` or `sqrt_sat` (box 15 default BX15-08 proposes them).
        self.assertEqual(code(["Q16.16 a = 2.0;", "Q16.16 c = sqrt_sat(a, floor);"]).code, "C3005")


class MixedFormats(unittest.TestCase):                     # SPEC-01 IM-63, IM-64 (BX15-05)
    def test_qmul(self):
        self.assertEqual(value(["Q16.16 a = 1.5;", "Q1.15 b = 0.5;", "Q16.16 c = qmul(Q16.16, a, b);",
                                "return c.raw as I64;"]).value, 49152)
        self.assertEqual(value(["Q16.16 a = 1.5;", "Q32.32 c = qmul(Q32.32, a, a);",
                                "return c.raw;"]).value, 9663676416)
        r = fault(["Q16.16 a = 200.0;", "Q16.16 c = qmul(Q16.16, a, a, floor);"])
        self.assertEqual((r.code, r.operation, r.exact), ("E_OVERFLOW", "qmul.checked.q16_16.q16_16.q16_16.floor",
                                                          40000 << 16))
        self.assertTrue(refused(["Q16.16 a = 1.5;", "Q32.32 c = qmul(Q32.32, a, a, floor);"]))
        self.assertEqual(code(["Q16.16 a = 1.5;", "Q16.16 c = a * (a as Q32.32);"]).code, "C2001")   # IM-63

    def test_mul_int(self):
        self.assertEqual(raw_of("Q16.16 x = mul_int(Q16.16.raw(98304), 3);"), 294912)
        r = fault(["Q16.16 a = 1.5;", "I16 n = 30000;", "Q16.16 c = mul_int(a, n);"])
        self.assertEqual((r.code, r.operation, r.exact), ("E_OVERFLOW", "mul_int.checked.q16_16.i16", 98304 * 30000))
        self.assertEqual([v.render() for v in r.operands], ["Q16.16 raw 98304", "I16 30000"])


class Conversions(unittest.TestCase):                      # SPEC-01 IM-68, IM-69 table, IM-106
    def test_table(self):
        self.assertEqual(value(["Q32.32 x = Q32.32.raw(1);", "Q16.16 y = x as Q16.16;",
                                "return y.raw as I64;"]).value, 0)
        self.assertEqual(value(["Q32.32 x = Q32.32.raw(1);", "Q16.16 y = x as Q16.16 round ceil;",
                                "return y.raw as I64;"]).value, 1)
        self.assertEqual(value(["Q16.16 x = 1.0;", "Q32.32 y = x as Q32.32;", "return y.raw;"]).value, 4294967296)
        r = fault(["Q32.32 x = 32768.0;", "Q16.16 y = x as Q16.16;"])
        self.assertEqual((r.code, r.operation, r.exact), ("E_NARROW", "as.checked.q32_32.q16_16.half_even",
                                                          2147483648))
        self.assertEqual(value(["Q32.32 x = -32768.0;", "Q16.16 y = x as Q16.16;", "return y.raw as I64;"]).value,
                         -2147483648)
        for clause, want in (("", -2), (" round floor", -3), (" round half_away", -3)):
            with self.subTest(clause=clause):
                self.assertEqual(value(["Q16.16 x = -2.5;", "return x as I64%s;" % clause]).value, want)

    def test_integer_to_fixed_record(self):                # IM-106, decision on OQ-147
        r = fault(["I64 n = 40000;", "Q16.16 y = n as Q16.16;"])
        self.assertEqual((r.code, r.operation, r.exact), ("E_NARROW", "as.checked.i64.q16_16", 2621440000))
        self.assertEqual(([v.render() for v in r.operands], r.limit.render()), (["I64 40000"],
                                                                               "Q16.16 raw 2147483647"))
        d = code(["Q16.16 y = 40000 as Q16.16;"])             # IM-23, IM-184: decided at compile time
        self.assertEqual((d.code, d.fault.operation, d.fault.exact, d.fault.limit),
                         ("C6001", "unassigned", 2621440000, Value("Q16.16", 2147483647)))
        self.assertEqual(raw_of("Q16.16 x = 3 as Q16.16;"), 196608)
        self.assertEqual(raw_of("Q8.8 x = 200 as% Q8.8;"), -14336)

    def test_wrap_and_result(self):
        self.assertEqual(value(["Q32.32 x = 32768.5;", "Q16.16 y = x as% Q16.16 round trunc;",
                                "return y.raw as I64;"]).value, -2147483648 + 32768)
        self.assertEqual(printed(["I64 n = 40000;", 'Q16.16 y = n as? Q16.16 catch 1.0;', '"{y}\\n";']), "1.0\n")

    def test_round_clause_rules(self):
        self.assertEqual(code(["Q16.16 x = 1.0;", "Q32.32 y = x as Q32.32 round floor;"]).code, "C2009")
        self.assertEqual(code(["I32 n = 1;", "Q16.16 y = n as Q16.16 round floor;"]).code, "C2009")
        self.assertEqual(code(["Q16.16 x = 1.0;", "Bool b = x as Bool;"]).code, "C2008")
        self.assertTrue(refused(["Q16.16 y = 1.5 as Q16.16;"]))          # OQ-162, BX15-11
        self.assertTrue(refused(["Q16.16 x = 1.5;", "Q8.8 y = x as| Q8.8;"]))   # BX15-09


class Printing(unittest.TestCase):                         # SPEC-04 LS-201, LS-207, LS-208, LS-215
    def test_ls215_rows(self):
        out = printed(["Q32.32 a = 1.5;", "Q32.32 b = 3;", "Q32.32 c = Q32.32.raw(1);", "Q32.32 d = 0.125;",
                       "Q32.32 e = 0.375;", "Q32.32 f = -0.03125;", "Q16.16 g = Q16.16.raw(1);",
                       '"{a}|{b}|{c}|{d:.2}|{e:.2}|{f:.1}|{g:.3}|{a!raw}\\n";'])
        self.assertEqual(out, "1.5|3.0|0.00000000023283064365386962890625|0.12|0.38|0.0|0.000|6442450944\n")

    def test_layout_and_storage(self):
        out = printed(["Q8.8 a = -1.5;", "Q8.8[3] v = [0.5, -1.25, 3.0];",
                       '"{a:+}|{-a:+}|{a:>7}|{a:08}|{a!bits:#x}|{a!raw:x}|{v}|{v:.1}|{a:.0}\\n";'])
        self.assertEqual(out, "-1.5|+1.5|   -1.5|-00001.5|0xfe80|-180|[0.5, -1.25, 3.0]|[0.5, -1.2, 3.0]|-2\n")

    def test_spec_errors(self):
        for spec in ("{a:x}", "{a:/100}", "{a:#}", "{a!lanes(U8)}", "{a:c}", "{a:.2/100}"):
            with self.subTest(spec=spec):
                self.assertIn(code(["Q8.8 a = 1.0;", '"%s\\n";' % spec]).code, ("C1036", "C1037"))
        self.assertEqual(code(["I64 a = 1;", '"{a!raw}\\n";']).code, "C1037")
        for spec in ("{a!ratio}", "{a:_}", "{a:d}"):
            with self.subTest(spec=spec):
                self.assertTrue(refused(["Q8.8 a = 1.0;", '"%s\\n";' % spec]))


class Reductions(unittest.TestCase):                       # SPEC-02 R-4, SPEC-01 IM-77 to IM-87
    def test_same_format(self):
        out = printed(["Q16.16[4] a = [1.5, 2.25, -0.5, 0.125];",
                       '"{sum(a)} {min(a)} {max(a)} {fold_checked(add, 1.0, a)} {sum_wrap(a)} {sum_sat(a)}\\n";'])
        self.assertEqual(out, "3.375 -0.5 2.25 4.375 3.375 3.375\n")
        r = fault(["Q8.8[2] a = [100.0, 100.0];", "Q8.8 s = sum(a);"])
        self.assertEqual((r.code, r.operation, r.exact, r.limit), ("E_OVERFLOW", "sum.checked.q8_8.q8_8", 51200,
                                                                   Value("Q8.8", 32767)))
        r = fault(["Q8.8[2] a = [100.0, 100.0];", "Q8.8 s = fold_checked(add, 0.0, a);"])
        self.assertEqual(r.operation, "fold_checked.add.q8_8")
        self.assertTrue(refused(["Q8.8[2] a = [1.0, 1.0];", "Q16.16 s = sum(Q16.16, a);"]))
        self.assertTrue(refused(["Q8.8[2] a = [1.0, 1.0];", "Q8.8 s = fold_checked(mul, 1.0, a);"]))

    def test_dot(self):                                    # SPEC-01 IM-87 (BX15-04)
        self.assertEqual(value(["Q16.16[2] a = [Q16.16.raw(1), Q16.16.raw(1)];", "Q16.16[2] b = [0.5, 0.5];",
                                "Q16.16 d = dot(Q16.16, a, b);", "return d.raw as I64;"]).value, 1)
        self.assertEqual(value(["Q16.16[2] a = [1.5, 0.25];", "Q16.16[2] b = [0.5, 3.0];",
                                "Q32.32 d = dot(Q32.32, a, b);", "return d.raw;"]).value, 6442450944)
        self.assertEqual(value(["Q16.16[1] a = [Q16.16.raw(1)];", "Q16.16[1] b = [0.5];",
                                "Q16.16 d = dot(Q16.16, a, b, ceil);", "return d.raw as I64;"]).value, 1)
        r = fault(["Q8.8[2] a = [100.0, 100.0];", "Q8.8 d = dot(Q8.8, a, a, floor);"])
        self.assertEqual((r.code, r.operation, r.exact), ("E_OVERFLOW", "dot.checked.q8_8.q8_8.floor", 20000 << 8))
        r = fault(["Q8.8[2] a = [1.0, 1.0];", "Q8.8[3] b = [1.0, 1.0, 1.0];", "Q8.8 d = dot(Q8.8, a, b);"])
        self.assertEqual((r.code, r.operation), ("E_SHAPE", "dot.checked.q8_8.q8_8.half_even"))
        r = fault(["Q8.8[2] a = [100.0, 100.0];", "Q32.32 d = dot(Q32.32, a, a);", "Q8.8 e = d as Q8.8;"])
        self.assertEqual(r.operation, "as.checked.q32_32.q8_8.half_even")
        self.assertTrue(refused(["Q8.8[1] a = [1.0];", "Q32.32 d = dot(Q32.32, a, a, floor);"]))
        self.assertTrue(refused(["Q8.8[1] a = [1.0];", "I64 d = dot(I64, a, a);"]))

    def test_kernel(self):                                 # SPEC-04 LS-246; SPEC-02 R-4
        src = ("kernel twice[n](in Q16.16[n] a, out Q16.16[n] b, out Q16.16 s) over [i: n] {\n"
               "    b[i] = a[i] * 2.0;\n    reduce s = sum(a[i]);\n}\n"
               "export I64 run() {\n    Q16.16[3] a = [1.5, -0.25, 2.0];\n    Q16.16[3] b;\n    Q16.16 s = 0.0;\n"
               "    twice(a, b, s);\n    \"{b} {s}\\n\";\n    return 0;\n}\n")
        o = run(src)
        self.assertEqual((o.kind, o.stdout), ("value", b"[3.0, -0.5, 4.0] 3.25\n"))


class Records(unittest.TestCase):
    def test_tag_31_round_trip(self):                      # SPEC-01 11.1, IM-149
        r = fault(["Q16.16 a = 256.0;", "Q16.16 b = 128.0;", "Q16.16 c = a * b;"])
        self.assertEqual(faults.encode_value(Value("Q16.16", 98304)).hex(), "3113100000800100")
        back = faults.decode_fault_record(faults.encode_fault_record(r, faults.RUN_TIME), faults.RUN_TIME)[1]
        self.assertEqual((back.operands, back.limit), (r.operands, r.limit))

    def test_expect_format_5(self):                        # SPEC-09 CONF-11 rule 14 (BX15-03)
        o = run("Q16.16 g = 2.5;\nexport Q16.16 run() {\n    g = g * 2.0;\n    return 1.5;\n}\n")
        e = expect.from_outcome(o, "fixed/t", fmt=5)
        lines = ["%s %s" % kv for kv in e.lines]
        self.assertIn("return Q16.16 raw 98304", lines)
        self.assertIn("state.global t g Q16.16 raw 327680", lines)
        with self.assertRaises(ValueError):
            expect.from_outcome(o, "fixed/t", fmt=4)


class Surface(unittest.TestCase):
    def test_structs_and_module_state(self):
        o = run("struct P { Q16.16 x; Q1.15 k; }\nQ8.8[2] t = [0.5, -1.0];\n"
                "export I64 run() {\n    P p = P(x = 1.5, k = -0.25);\n    \"{p.x} {p.k} {t}\\n\";\n    return 0;\n}\n")
        self.assertEqual(o.stdout, b"1.5 -0.25 [0.5, -1.0]\n")

    def test_left_to_defaults_not_yet_ruled(self):
        self.assertTrue(refused(["Q16.16 a = 1.5;", "a++;"]))
        self.assertTrue(refused(["Q16.16 a = 1.5;", "Q16.16 c = mul_result(a, a) catch 0.0;"]))

    def test_boot_refusal_cases_run(self):
        # SPEC-09 BOOT-01: the spec outcome of each boot/ refusal case under cint-core-1.
        root = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                            "conformance", "boot")
        for case, want in (("refuse_fixed_point", 3), ("refuse_fraction_literal", 32768)):
            with self.subTest(case=case):
                with open(os.path.join(root, case + ".ci"), "rb") as f:
                    o = run_program(f.read(), "boot/%s.ci" % case, "run", None)
                self.assertEqual(o.value, Value("I64", want))

    def test_without_the_fixed_surface(self):
        # compiler/tests/check_suite.py parses cintc's surface with `fixed=False`: fixed point
        # is refused.
        with self.assertRaises(faults.Refused):
            P.parse_module("Q16.16 q = 0.5;", "t.ci", fixed=False)


if __name__ == "__main__":
    unittest.main()
