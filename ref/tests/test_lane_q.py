import os, sys; sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # put ref/ on the path
"""The lane Q operations (SPEC-01 IM-190 to IM-192, SPEC-04 LS-329; OQ-338): `hadd`, `rhadd`,
`rshr` and `rmulh`. Every expected value is a row of the IM-190 to IM-192 tables, or a closed
form of their definitions on the raw values, run through `arith.op`, `fixed.op` and programs."""
import random
import unittest

from cint_ref import arith, fixed
from cint_ref import parser as P
from cint_ref.check import check_module
from cint_ref.exec import run_program
from cint_ref.faults import CompileError
from cint_ref.types import Value, int_type


def run(src, entry="run"):
    return run_program(src.encode("utf-8"), "t.ci", entry, None)


def body(lines, result="I64"):
    return "export %s run() {\n%s}\n" % (result, "".join("    %s\n" % x for x in lines))


def printed(lines):
    o = run(body(lines + ["return 0;"]))
    assert o.kind == "value", (o.kind, o.diagnostic, o.record, o.message)
    return o.stdout.decode("utf-8")


def code(src):
    o = run(src)
    assert o.kind == "compile-error", (o.kind, o.record, o.message)
    return o.diagnostic.code


def hadd_closed(a, b):
    return (a + b) // 2


def rhadd_closed(a, b):
    return (a + b + 1) // 2


def rshr_closed(a, k):
    # round(A / 2^k, half_up) as a value in Z (IM-192).
    return arith.round_div(a, 1 << k, "half_up")


class Tables(unittest.TestCase):
    def test_im190_rows(self):
        rows = [("I16", 3, 4, 3, 4), ("I16", -3, -4, -4, -3), ("I16", -1, 0, -1, 0),
                ("I16", 32767, 32767, 32767, 32767), ("I16", -32768, -32768, -32768, -32768),
                ("I16", 32767, -32768, -1, 0), ("U8", 255, 254, 254, 255),
                ("I64", 2 ** 63 - 1, 2 ** 63 - 1, 2 ** 63 - 1, 2 ** 63 - 1)]
        for t, a, b, h, r in rows:
            self.assertEqual(arith.op("hadd", "checked", t, a, b), Value(t, h))
            self.assertEqual(arith.op("rhadd", "checked", t, a, b), Value(t, r))
        q = "Q5.11"
        self.assertEqual(fixed.op("hadd", "checked", q, Value(q, 2048), Value(q, 1)), Value(q, 1024))
        self.assertEqual(fixed.op("rhadd", "checked", q, Value(q, 2048), Value(q, 1)), Value(q, 1025))

    def test_im192_rows(self):
        rows = [(6, 2, 2), (-6, 2, -1), (-7, 2, -2), (5, 1, 3), (32767, 1, 16384), (-32768, 15, -1), (1, 0, 1)]
        for a, k, r in rows:
            self.assertEqual(arith.op("rshr", "checked", "I16", a, Value("I64", k), mode="half_up"), Value("I16", r))
        f = arith.op("rshr", "checked", "I16", 1, Value("I64", 16), mode="half_up")
        self.assertEqual((f.code, f.operation, f.limit), ("E_SHIFT", "rshr.checked.i16.half_up", Value("I64", 15)))
        f = arith.op("rshr", "checked", "I16", 1, Value("I64", -1), mode="half_up")
        self.assertEqual(f.code, "E_SHIFT")

    def test_im191_rows(self):
        rows = [("Q1.15", 16384, "Q1.15", 16384, 8192), ("Q1.15", 1, "Q1.15", 16384, 1),
                ("Q1.15", -1, "Q1.15", 16384, 0), ("Q1.15", -32768, "Q1.15", -32768, 32767),
                ("Q5.11", 2048, "Q1.15", 5734, 358), ("Q5.11", -32768, "Q1.15", 32767, -32767),
                ("Q1.31", -2 ** 31, "Q1.31", -2 ** 31, 2 ** 31 - 1)]
        for qa, a, qk, k, r in rows:
            self.assertEqual(fixed.op("rmulh", "sat", qa, Value(qa, a), Value(qk, k), mode="half_up"), Value(qa, r))

    def test_rmulh_is_qmul_half_up_saturated(self):
        # IM-191: the value of qmul(Q, a, k, half_up) with the saturating form.
        rnd = random.Random(338)
        for _ in range(2000):
            a, k = rnd.randrange(-32768, 32768), rnd.randrange(-32768, 32768)
            q = fixed.op("qmul", "checked", "Q5.11", Value("Q5.11", a), Value("Q1.15", k), mode="half_up",
                         target="Q5.11")
            r = fixed.op("rmulh", "sat", "Q5.11", Value("Q5.11", a), Value("Q1.15", k), mode="half_up")
            if isinstance(q, Value):
                self.assertEqual(r, q)
            else:
                self.assertEqual((a, k), (-32768, -32768))

    def test_closed_forms_on_every_i8_pair(self):
        for a in range(-128, 128):
            for b in range(-128, 128):
                self.assertEqual(arith.op("hadd", "checked", "I8", a, b).value, hadd_closed(a, b))
                self.assertEqual(arith.op("rhadd", "checked", "I8", a, b).value, rhadd_closed(a, b))
            for k in range(8):
                r = arith.op("rshr", "checked", "I8", a, Value("I64", k), mode="half_up").value
                self.assertEqual(r, rshr_closed(a, k))
                self.assertTrue(-128 <= r <= 127)

    def test_unsigned_and_64_bit_corners(self):
        for t in ("U8", "U16", "U32", "U64", "I64"):
            it = int_type(t)
            corners = sorted({it.min, it.min + 1, 0, 1, 2, it.max // 2, it.max // 2 + 1, it.max - 1, it.max})
            for a in corners:
                for b in corners:
                    self.assertEqual(arith.op("hadd", "checked", t, a, b).value, hadd_closed(a, b))
                    self.assertEqual(arith.op("rhadd", "checked", t, a, b).value, rhadd_closed(a, b))
                for k in range(it.width):
                    self.assertEqual(arith.op("rshr", "checked", t, a, Value("I64", k), mode="half_up").value,
                                     rshr_closed(a, k))


class Programs(unittest.TestCase):
    def test_values_in_a_program(self):
        out = printed(["I16 a = 3;", "I16 b = 4;", "Q5.11 x = 1.0;", "Q5.11 y = Q5.11.raw(1);",
                       "Q1.15 c = Q1.15.raw(5734);", "Q5.11 m = rmulh(x, c);", "Q5.11 n = rmulh(x, 0.5);",
                       "I16 h = hadd(a, b);", "I16 r = rhadd(-3, -4);", "Q5.11 z = rhadd(x, y);",
                       "I16 s = rshr(a, 1);", "U64 u = hadd(U64.max, 0);",
                       "\"{h} {r} {z!raw} {m!raw} {n!raw} {s} {u}\\n\";"])
        self.assertEqual(out, "3 -3 1025 358 1024 2 9223372036854775807\n")

    def test_untyped_call_takes_its_context(self):
        # LS-329: a call whose arguments are all untyped takes its type from its context.
        self.assertEqual(printed(["U8 v = rhadd(255, 254);", "I8 w = rshr(-7, 2);", "\"{v} {w}\\n\";"]),
                         "255 -2\n")

    def test_runtime_count_fault(self):
        o = run(body(["I64 k = 16;", "I16 a = 1;", "I16 b = rshr(a, k);", "return 0;"]))
        self.assertEqual(o.kind, "fault")
        self.assertEqual(o.record.code, "E_SHIFT")
        self.assertEqual(o.record.operation, "rshr.checked.i16.half_up")

    def test_diagnostics(self):
        self.assertEqual(code(body(["Q5.11 a = 1.0;", "Q8.8 b = 1.0;", "Q5.11 c = hadd(a, b);", "return 0;"])),
                         "C2001")
        self.assertEqual(code(body(["Q4.4 a = 1.0;", "Q1.7 k = 0.5;", "Q4.4 c = rmulh(a, k);", "return 0;"])),
                         "C2103")
        self.assertEqual(code(body(["Q5.11 a = 1.0;", "Q2.14 k = 0.5;", "Q5.11 c = rmulh(a, k);", "return 0;"])),
                         "C2001")
        self.assertEqual(code(body(["Q64.64 a = 1.0;", "Q64.64 c = hadd(a, a);", "return 0;"])), "C2103")
        self.assertEqual(code(body(["I128 a = 1;", "I128 c = rhadd(a, a);", "return 0;"])), "C2103")
        self.assertEqual(code(body(["I16 a = 1;", "I16 c = rmulh(a, a);", "return 0;"])), "C2103")
        self.assertEqual(code(body(["Bool a = true;", "Bool c = hadd(a, a);", "return 0;"])), "C2103")
        self.assertEqual(code("I16 rhadd(I16 a) {\n    return a;\n}\n" + body(["return 0;"])), "C3001")

    def test_names_free_without_fixed_point(self):
        # A module parsed without fixed point may declare the names (LANEQ_PRELUDE, as FIXED_PRELUDE).
        m = P.parse_module("I64 hadd(I64 a) {\n    return a;\n}\n", "t.ci", fixed=False)
        check_module(m)
        m = P.parse_module("I64 hadd(I64 a) {\n    return a;\n}\n", "t.ci")
        with self.assertRaises(CompileError):
            check_module(m)


if __name__ == "__main__":
    unittest.main()
