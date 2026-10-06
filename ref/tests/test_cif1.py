import os, sys; sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # put ref/ on the path
"""CIF-1 fixtures (SPEC-01 13.1): reader, evaluation through arith.op, writer, CLI."""
import os
import subprocess
import tempfile
import unittest

from cint_ref import cif1
from cint_ref.arith import op

REF_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
M = 9223372036854775807

# The four example lines of SPEC-01 13.1, verbatim.
EXAMPLES = [
    '{"id":"add.checked.i64.001","status":"S","op":"add.checked.i64","args":[{"t":"I64","v":"9223372036854775807"},{"t":"I64","v":"1"}],"expect":{"fault":{"code":"E_OVERFLOW","exact":"9223372036854775808","limit":"9223372036854775807"}}}',
    '{"id":"rem.checked.i64.003","status":"S","op":"rem.checked.i64","args":[{"t":"I64","v":"7"},{"t":"I64","v":"-2"}],"expect":{"value":{"t":"I64","v":"-1"}}}',
    '{"id":"mul.checked.q16_16.002","status":"S","op":"mul.checked.q16_16.half_even","args":[{"t":"Q16.16","raw":"1"},{"t":"Q16.16","raw":"98304"}],"expect":{"value":{"t":"Q16.16","raw":"2"}}}',
    '{"id":"fold_checked.add.i8.001","status":"S","op":"fold_checked.add.i8","args":[{"t":"I8","v":"0"},{"t":"I8[3]","v":["120","120","-120"]}],"expect":{"fault":{"code":"E_OVERFLOW","exact":"240","limit":"127","index":"1"}}}',
]

# A decoder fixture (SPEC-01 13.1 `reject`), which the operation layer does not evaluate.
DECODER = '{"id":"d","status":"S","op":"decode.checked.pt5","args":[],"expect":{"reject":true}}'


class Reader(unittest.TestCase):
    def test_examples_parse(self):
        recs = [cif1.parse_line(x) for x in EXAMPLES]
        r = recs[0]
        self.assertEqual((r.id, r.status, r.op, r.name, r.form, r.types, r.mode),
                         ("add.checked.i64.001", "S", "add.checked.i64", "add", "checked", ["I64"], None))
        self.assertEqual(r.args[0], {"t": "I64", "v": "9223372036854775807"})
        self.assertEqual(r.outcome, "fault")
        self.assertEqual(recs[2].mode, "half_even")
        self.assertEqual(recs[3].form, "add")

    def test_each_outcome_status(self):
        base = '{"id":"x","status":"P","op":"add.checked.i8","args":[{"t":"I8","v":"1"},{"t":"I8","v":"2"}],"expect":%s}'
        self.assertEqual(cif1.parse_line(base % '{"value":{"t":"I8","v":"3"}}').outcome, "value")
        self.assertEqual(cif1.parse_line(base % '{"fault":{"code":"E_OVERFLOW"}}').outcome, "fault")
        self.assertEqual(cif1.parse_line(base % '{"compile_error":"TYPE"}').outcome, "compile_error")
        self.assertEqual(cif1.parse_line(base % '{"reject":true}').outcome, "reject")

    def test_rejects_json_numbers_and_bad_records(self):
        bad = [
            '{"id":"x","status":"S","op":"add.checked.i8","args":[{"t":"I8","v":1}],"expect":{"value":{"t":"I8","v":"1"}}}',
            '{"id":"x","status":"S","op":"add.checked.i8","args":[{"t":"I8","v":"1.5"}],"expect":{"value":{"t":"I8","v":"1"}}}',
            '{"id":"x","status":"S","op":"add.checked.i8","args":[],"expect":{"value":{"t":"I8","v":1.0}}}',
            '{"id":"x","status":"Q","op":"add.checked.i8","args":[],"expect":{"value":{"t":"I8","v":"1"}}}',
            '{"status":"S","id":"x","op":"add.checked.i8","args":[],"expect":{"value":{"t":"I8","v":"1"}}}',
            '{"id":"x","status":"S","op":"add.checked.i8","args":[],"expect":{"value":{"t":"I8","v":"1"},"reject":true}}',
        ]
        for line in bad:
            with self.subTest(line=line):
                with self.assertRaises(ValueError):
                    cif1.parse_line(line)


class Evaluate(unittest.TestCase):
    def test_examples(self):
        self.assertEqual(cif1.check(cif1.parse_line(EXAMPLES[0])), [])
        self.assertEqual(cif1.check(cif1.parse_line(EXAMPLES[1])), [])
        # Fixed point (SPEC-01 5.3) and fold_checked (6.2) are evaluated too.
        for line in EXAMPLES[2:]:
            self.assertEqual(cif1.check(cif1.parse_line(line)), [])
        with self.assertRaises(cif1.Unsupported):
            cif1.check(cif1.parse_line(DECODER))

    def test_fold_index_mismatch_is_reported(self):
        line = EXAMPLES[3].replace('"index":"1"', '"index":"2"')
        problems = cif1.check(cif1.parse_line(line))
        self.assertEqual(len(problems), 1)
        self.assertIn("index", problems[0])

    def test_fixed_point_and_reductions(self):
        recs = [
            '{"id":"a","status":"S","op":"div.checked.q16_16.floor","args":[{"t":"Q16.16","raw":"-65536"},{"t":"Q16.16","raw":"196608"}],"expect":{"value":{"t":"Q16.16","raw":"-21846"}}}',
            '{"id":"b","status":"S","op":"as.checked.q16_16.i32.floor","args":[{"t":"Q16.16","raw":"-163840"}],"expect":{"value":{"t":"I32","v":"-3"}}}',
            '{"id":"c","status":"S","op":"sqrt.checked.q1_15.ceil","args":[{"t":"Q1.15","raw":"32767"}],"expect":{"fault":{"code":"E_OVERFLOW","exact":"32768","limit":"32767"}}}',
            '{"id":"d","status":"S","op":"sum.checked.i8.i16","args":[{"t":"I8[3]","v":["127","127","127"]}],"expect":{"value":{"t":"I16","v":"381"}}}',
            '{"id":"e","status":"S","op":"reduce_min.checked.i64","args":[{"t":"I64[0]","v":[]}],"expect":{"fault":{"code":"E_SHAPE"}}}',
            '{"id":"f","status":"S","op":"sum_sat.sat.i8","args":[{"t":"I8[3]","v":["120","120","-120"]}],"expect":{"value":{"t":"I8","v":"7"}}}',
        ]
        for line in recs:
            with self.subTest(line=line[:40]):
                self.assertEqual(cif1.check(cif1.parse_line(line)), [])

    def test_array_length_must_match_its_type(self):
        line = '{"id":"x","status":"S","op":"sum.checked.i8.i8","args":[{"t":"I8[2]","v":["1","2","3"]}],"expect":{"value":{"t":"I8","v":"6"}}}'
        with self.assertRaises(ValueError):
            cif1.check(cif1.parse_line(line))

    def test_mismatch_is_reported(self):
        line = EXAMPLES[0].replace('"exact":"9223372036854775808"', '"exact":"9223372036854775809"')
        problems = cif1.check(cif1.parse_line(line))
        self.assertEqual(len(problems), 1)
        self.assertIn("exact", problems[0])
        line = EXAMPLES[1].replace('"v":"-1"}}}', '"v":"1"}}}')
        self.assertEqual(len(cif1.check(cif1.parse_line(line))), 1)

    def test_conversions_and_modes(self):
        recs = [
            '{"id":"a","status":"S","op":"as.checked.i64.i32","args":[{"t":"I64","v":"2147483648"}],"expect":{"fault":{"code":"E_NARROW","exact":"2147483648","limit":"2147483647"}}}',
            '{"id":"b","status":"S","op":"as.wrap.i64.u64","args":[{"t":"I64","v":"-1"}],"expect":{"value":{"t":"U64","v":"18446744073709551615"}}}',
            '{"id":"c","status":"S","op":"div_round.checked.i64.half_away","args":[{"t":"I64","v":"-5"},{"t":"I64","v":"2"}],"expect":{"value":{"t":"I64","v":"-3"}}}',
            '{"id":"d","status":"S","op":"shl.checked.i64","args":[{"t":"I64","v":"1"},{"t":"I64","v":"64"}],"expect":{"fault":{"code":"E_SHIFT","limit":"63"}}}',
            '{"id":"e","status":"S","op":"uabs.checked.i8.u8","args":[{"t":"I8","v":"-128"}],"expect":{"value":{"t":"U8","v":"128"}}}',
            '{"id":"f","status":"S","op":"muldiv.checked.i64.i64.floor","args":[{"t":"I64","v":"9223372036854775807"},{"t":"I64","v":"3"},{"t":"I64","v":"2"}],"expect":{"fault":{"code":"E_OVERFLOW","exact":"13835058055282163710"}}}',
            '{"id":"g","status":"S","op":"mul_full.checked.i64.i128","args":[{"t":"I64","v":"-9223372036854775808"},{"t":"I64","v":"-9223372036854775808"}],"expect":{"value":{"t":"I128","v":"85070591730234615865843651857942052864"}}}',
            '{"id":"h","status":"S","op":"shl.wrap.u8","args":[{"t":"U8","v":"255"},{"t":"U8","v":"4"}],"expect":{"value":{"t":"U8","v":"240"}}}',
            '{"id":"i","status":"S","op":"add.checked.i64","args":[{"t":"I64","v":"9223372036854775807"},{"t":"I64","v":"1"}],"expect":{"fault":{"code":"E_OVERFLOW","operands":[{"t":"I64","v":"9223372036854775807"},{"t":"I64","v":"1"}]}}}',
        ]
        for line in recs:
            with self.subTest(line=line[:40]):
                self.assertEqual(cif1.check(cif1.parse_line(line)), [])


class Writer(unittest.TestCase):
    def test_render_from_result_matches_spec_examples(self):
        args = [{"t": "I64", "v": str(M)}, {"t": "I64", "v": "1"}]
        line = cif1.render("add.checked.i64.001", "S", "add.checked.i64", args, op("add", "checked", "I64", M, 1))
        self.assertEqual(line, EXAMPLES[0])
        args = [{"t": "I64", "v": "7"}, {"t": "I64", "v": "-2"}]
        line = cif1.render("rem.checked.i64.003", "S", "rem.checked.i64", args, op("rem", "checked", "I64", 7, -2))
        self.assertEqual(line, EXAMPLES[1])

    def test_render_division_by_zero_has_no_exact_or_limit(self):
        args = [{"t": "I64", "v": "5"}, {"t": "I64", "v": "0"}]
        line = cif1.render("div.0", "S", "div.checked.i64", args, op("div", "checked", "I64", 5, 0))
        self.assertTrue(line.endswith('"expect":{"fault":{"code":"E_DIV_ZERO"}}}'))

    def test_render_parse_roundtrip_with_derivation(self):
        args = [{"t": "U8", "v": "3"}, {"t": "U8", "v": "5"}]
        line = cif1.render("sub.checked.u8.1", "S", "sub.checked.u8", args, op("sub", "checked", "U8", 3, 5),
                           derivation="3 - 5 = -2 < 0 = MIN(U8)")
        rec = cif1.parse_line(line)
        self.assertEqual(rec.derivation, "3 - 5 = -2 < 0 = MIN(U8)")
        self.assertEqual(cif1.check(rec), [])
        self.assertEqual(cif1.render_record(rec), line)


class Cli(unittest.TestCase):
    def test_cif1_subcommand(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "x.cif1.jsonl")
            bad = EXAMPLES[1].replace('"v":"-1"}}}', '"v":"1"}}}').replace("rem.checked.i64.003", "rem.bad")
            with open(p, "wb") as f:
                f.write(("\n".join([EXAMPLES[0], EXAMPLES[1], DECODER, bad]) + "\n").encode())
            env = dict(os.environ, PYTHONPATH=REF_DIR)
            r = subprocess.run([sys.executable, "-m", "cint_ref", "cif1", p], capture_output=True, env=env)
            out = r.stdout.decode("ascii")
            self.assertEqual(r.returncode, 1, out)
            self.assertIn("rem.bad", out)
            self.assertIn("4 records, 2 agree, 1 disagree, 1 unsupported, 0 invalid", out)
            with open(p, "wb") as f:
                f.write(("\n".join(EXAMPLES[:2]) + "\n").encode())
            r = subprocess.run([sys.executable, "-m", "cint_ref", "cif1", p], capture_output=True, env=env)
            self.assertEqual(r.returncode, 0, r.stdout)


if __name__ == "__main__":
    unittest.main()
