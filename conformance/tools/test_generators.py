"""Tests for the conformance generators (plan Task 1.3, Step 3).

Every expected value below is taken from the specification text: SPEC-01
sections 4.1 to 4.6, 9.2, 9.8, 13.1 and 13.2, and SPEC-09 section 9.3. None is
taken from the legacy compiler.

Run from the repository root:
    python -m unittest discover -s conformance/tools -p "test_*.py" -v
"""

import hashlib
import json
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
ROOT = os.path.dirname(os.path.dirname(HERE))

import imeval  # noqa: E402
import gen_exhaustive8  # noqa: E402
import gen_boundary64  # noqa: E402
import gen_tables  # noqa: E402

M = 9223372036854775807
m = -9223372036854775808
U64MAX = 18446744073709551615


def parse_lines(data):
    return [json.loads(line) for line in data.decode("utf-8").split("\n") if line]


class EvaluatorAgainstSpecTables(unittest.TestCase):
    """SPEC-01 tables 4.2, 4.3, 4.4 and 4.6, row by row."""

    def val(self, op, form, t, a, b):
        r = imeval.eval_binary(op, form, t, a, b)
        self.assertEqual(r[0], "value", r)
        return r[1]

    def fault(self, op, form, t, a, b):
        r = imeval.eval_binary(op, form, t, a, b)
        self.assertEqual(r[0], "fault", r)
        return r[1]

    def test_section_4_2(self):
        f = self.fault("add", "checked", "I64", M, 1)
        self.assertEqual(f, {"code": "E_OVERFLOW", "exact": 2**63, "limit": M})
        self.assertEqual(self.val("add", "wrap", "I64", M, 1), m)
        self.assertEqual(self.val("add", "sat", "I64", M, 1), M)
        f = self.fault("sub", "checked", "I64", m, 1)
        self.assertEqual(f, {"code": "E_OVERFLOW", "exact": -9223372036854775809, "limit": m})
        self.assertEqual(self.val("sub", "wrap", "I64", m, 1), M)
        self.assertEqual(self.fault("sub", "checked", "U8", 3, 5),
                         {"code": "E_OVERFLOW", "exact": -2, "limit": 0})
        self.assertEqual(self.val("sub", "wrap", "U8", 3, 5), 254)
        self.assertEqual(self.val("sub", "sat", "U8", 3, 5), 0)
        self.assertEqual(self.fault("mul", "checked", "I64", m, -1)["exact"], 2**63)
        self.assertEqual(self.val("mul", "wrap", "I64", m, -1), m)
        self.assertEqual(self.fault("mul", "checked", "I32", 65536, 32768)["exact"], 2147483648)
        self.assertEqual(self.val("mul", "checked", "I32", -65536, 32768), -2147483648)
        self.assertEqual(self.val("mul", "wrap", "I8", 100, 3), 44)
        self.assertEqual(self.val("mul", "sat", "I8", -128, -1), 127)

    def test_section_4_4_floor_division(self):
        rows = [(7, 2, 3, 1), (-7, 2, -4, 1), (7, -2, -4, -1), (-7, -2, 3, -1), (6, -3, -2, 0),
                (m, 2, -4611686018427387904, 0), (m, 3, -3074457345618258603, 1)]
        for a, b, q, r in rows:
            self.assertEqual(self.val("div", "checked", "I64", a, b), q, (a, b))
            self.assertEqual(self.val("rem", "checked", "I64", a, b), r, (a, b))
        self.assertEqual(self.fault("div", "checked", "I64", m, -1),
                         {"code": "E_OVERFLOW", "exact": 2**63, "limit": M})
        self.assertEqual(self.val("rem", "checked", "I64", m, -1), 0)
        self.assertEqual(self.fault("div", "checked", "I64", 5, 0), {"code": "E_DIV_ZERO"})
        self.assertEqual(self.fault("rem", "checked", "I64", 5, 0), {"code": "E_DIV_ZERO"})

    def test_section_4_6_shifts(self):
        self.assertEqual(self.val("shl", "checked", "I64", 1, 62), 4611686018427387904)
        self.assertEqual(self.fault("shl", "checked", "I64", 1, 63),
                         {"code": "E_OVERFLOW", "exact": 2**63, "limit": M})
        self.assertEqual(self.val("shl", "checked", "I64", -1, 63), m)
        self.assertEqual(self.val("shl", "wrap", "I64", 1, 63), m)
        self.assertEqual(self.fault("shl", "checked", "I64", 1, 64), {"code": "E_SHIFT", "limit": 63})
        self.assertEqual(self.val("shl", "wrap", "U8", 255, 4), 240)
        self.assertEqual(self.val("shr", "checked", "I64", -7, 1), -4)
        self.assertEqual(self.val("shr", "checked", "I64", -1, 63), -1)
        self.assertEqual(self.val("shr", "checked", "U64", U64MAX, 63), 1)
        self.assertEqual(self.fault("shr", "checked", "I64", 1, 64)["code"], "E_SHIFT")

    def test_negative_shift_count_asserts_code_only(self):
        # SPEC-01 9.2 does not say which bound `limit` names for a negative count
        # (ref/OPEN.md, entry O-2); fixture 42 asserts only the code.
        self.assertEqual(self.fault("shl", "wrap", "I64", 1, -1), {"code": "E_SHIFT"})
        self.assertEqual(self.fault("shr", "checked", "I8", 1, -1), {"code": "E_SHIFT"})

    def test_unsupported_combination_is_refused(self):
        with self.assertRaises(ValueError):
            imeval.eval_binary("div", "wrap", "I8", 1, 1)  # no wrapping division (SPEC-01 4.4)
        with self.assertRaises(ValueError):
            imeval.eval_binary("shr", "sat", "I8", 1, 1)


class ExhaustiveGenerator(unittest.TestCase):

    def setUp(self):
        self.data = gen_exhaustive8.render("add", "checked", "I8")

    def test_record_count(self):
        self.assertEqual(self.data.count(b"\n"), 65536)
        self.assertEqual(len(parse_lines(self.data)), 65536)

    def test_overflow_record(self):
        recs = parse_lines(self.data)
        hits = [r for r in recs if r["args"] == [{"t": "I8", "v": "127"}, {"t": "I8", "v": "1"}]]
        self.assertEqual(len(hits), 1)
        self.assertEqual(hits[0]["expect"],
                         {"fault": {"code": "E_OVERFLOW", "exact": "128", "limit": "127"}})
        self.assertEqual(hits[0]["op"], "add.checked.i8")
        self.assertEqual(hits[0]["status"], "S")

    def test_no_carriage_return(self):
        self.assertNotIn(b"\r", self.data)  # plan Review Focus 5
        self.assertTrue(self.data.endswith(b"\n"))

    def test_deterministic(self):
        self.assertEqual(self.data, gen_exhaustive8.render("add", "checked", "I8"))

    def test_written_file_is_byte_identical(self):
        with tempfile.TemporaryDirectory() as d:
            p1 = gen_exhaustive8.write("add", "checked", "I8", d)
            with open(p1, "rb") as fh:
                first = fh.read()
            p2 = gen_exhaustive8.write("add", "checked", "I8", d)
            with open(p2, "rb") as fh:
                second = fh.read()
        self.assertEqual(p1, p2)
        self.assertTrue(p1.endswith("add.checked.i8.cif1.jsonl"))
        self.assertEqual(first, self.data)
        self.assertEqual(first, second)

    def test_sorted_operand_order_and_key_order(self):
        recs = parse_lines(self.data)
        pairs = [(int(r["args"][0]["v"]), int(r["args"][1]["v"])) for r in recs]
        self.assertEqual(pairs, sorted(pairs))
        self.assertEqual(pairs[0], (-128, -128))
        self.assertEqual(pairs[-1], (127, 127))
        self.assertEqual(list(recs[0].keys()), ["id", "status", "op", "args", "expect"])
        self.assertEqual(recs[0]["id"], "add.checked.i8.00000")
        self.assertEqual(recs[-1]["id"], "add.checked.i8.65535")
        self.assertEqual(len({r["id"] for r in recs}), 65536)

    def test_fault_key_order(self):
        first = self.data.split(b"\n")[0]
        self.assertIn(b'"expect":{"fault":{"code":"E_OVERFLOW","exact":"-256","limit":"-128"}}', first)

    def test_u8_sub_wrap(self):
        recs = parse_lines(gen_exhaustive8.render("sub", "wrap", "U8"))
        hit = [r for r in recs if r["args"][0]["v"] == "3" and r["args"][1]["v"] == "5"]
        self.assertEqual(hit[0]["expect"], {"value": {"t": "U8", "v": "254"}})

    def test_i8_div_min_by_minus_one(self):
        recs = parse_lines(gen_exhaustive8.render("div", "checked", "I8"))
        hit = [r for r in recs if r["args"][0]["v"] == "-128" and r["args"][1]["v"] == "-1"]
        self.assertEqual(hit[0]["expect"],
                         {"fault": {"code": "E_OVERFLOW", "exact": "128", "limit": "127"}})
        zero = [r for r in recs if r["args"][1]["v"] == "0"]
        self.assertEqual(len(zero), 256)
        self.assertTrue(all(r["expect"] == {"fault": {"code": "E_DIV_ZERO"}} for r in zero))

    def test_t1_batch_is_eleven_forms_per_type(self):
        forms = gen_exhaustive8.T1_FORMS
        self.assertEqual(len(forms), 11)
        self.assertEqual(len(forms) * 2 * 65536, 1441792)  # SPEC-09 9.3
        self.assertEqual(len(gen_exhaustive8.ALL_FORMS), 14)


class BoundaryGenerator(unittest.TestCase):

    def test_values_are_the_specified_set(self):
        self.assertEqual(gen_boundary64.VALUES["I64"], [
            m, m + 1, -2**32, -2**31, -2, -1, 0, 1, 2, 2**31 - 1, 2**31, 2**32,
            3037000499, 3037000500, M - 1, M])

    def test_matrix(self):
        data = gen_boundary64.render("add", "checked", "I64")
        recs = parse_lines(data)
        self.assertEqual(len(recs), 256)
        self.assertNotIn(b"\r", data)
        self.assertEqual(data, gen_boundary64.render("add", "checked", "I64"))
        hit = [r for r in recs if r["args"] == [{"t": "I64", "v": str(M)}, {"t": "I64", "v": "1"}]]
        self.assertEqual(hit[0]["expect"]["fault"],
                         {"code": "E_OVERFLOW", "exact": "9223372036854775808", "limit": str(M)})
        self.assertEqual(recs[0]["id"], "add.checked.i64.000")

    def test_unspecified_types_are_refused(self):
        for t in ("I32", "U32", "U64"):
            with self.assertRaises(gen_boundary64.Unspecified):
                gen_boundary64.render("add", "checked", t)


class TableGenerator(unittest.TestCase):

    def test_table_program_and_cases(self):
        cif = gen_exhaustive8.render("add", "checked", "I8")
        src, cases = gen_tables.render(cif, "integer-machine/exh8/add.checked.i8.cif1.jsonl")
        self.assertNotIn(b"\r", src)
        self.assertNotIn(b"\r", cases)
        text = src.decode("ascii")
        self.assertIn("export I8 add_checked_i8(I8 a, I8 b) {\n    return a + b;\n}\n", text)
        lines = cases.decode("ascii").split("\n")
        self.assertEqual(lines[-1], "")
        self.assertEqual(len(lines) - 1, 65536)
        self.assertEqual(lines[0], "tables.add_checked_i8 add_checked_i8 I8 -128 I8 -128")
        self.assertEqual(gen_tables.render(cif, "integer-machine/exh8/add.checked.i8.cif1.jsonl"),
                         (src, cases))
        self.assertIn(hashlib.sha256(cif).hexdigest(), text)

    def test_operator_spelling(self):
        spell = gen_tables.OPERATOR
        self.assertEqual(spell[("add", "wrap")], "+%")
        self.assertEqual(spell[("shl", "wrap")], "<<%")
        self.assertEqual(spell[("mul", "sat")], "*|")
        self.assertEqual(spell[("rem", "checked")], "%")


class Anchors(unittest.TestCase):
    PATH = os.path.join(ROOT, "conformance", "integer-machine", "anchors.cif1.jsonl")

    def setUp(self):
        with open(self.PATH, "rb") as fh:
            self.data = fh.read()
        self.recs = parse_lines(self.data)

    def test_shape(self):
        self.assertNotIn(b"\r", self.data)
        self.assertTrue(self.data.endswith(b"\n"))
        self.assertGreaterEqual(len(self.recs), 120)
        self.assertEqual(len({r["id"] for r in self.recs}), len(self.recs))
        for r in self.recs:
            self.assertEqual(list(r.keys()), ["id", "status", "op", "args", "expect", "derivation"], r["id"])
            self.assertIn(r["status"], ("S", "P"))
            self.assertEqual(len(r["expect"]), 1)
            self.assertTrue(r["derivation"].strip(), r["id"])
            self.assertTrue(r["id"].startswith(r["op"].split(".")[0]), r["id"])

    def test_integer_records_agree_with_generator_evaluator(self):
        """A cross-check of two hand derivations, not an oracle: a mismatch means one is wrong."""
        checked = 0
        for r in self.recs:
            parts = r["op"].split(".")
            if len(parts) != 3 or parts[0] not in imeval.BINARY_OPS:
                continue
            t = parts[2].upper()
            if t not in imeval.TYPES or len(r["args"]) != 2:
                continue
            a, b = (int(x["v"]) for x in r["args"])
            got = imeval.render_expect(imeval.eval_binary(parts[0], parts[1], t, a, b), t)
            self.assertEqual(got, r["expect"], r["id"])
            checked += 1
        self.assertGreaterEqual(checked, 60)

    def test_required_coverage(self):
        ops = {r["op"] for r in self.recs}
        modes = {"floor", "ceil", "trunc", "away", "half_even", "half_away", "half_trunc", "half_up", "half_down"}
        for mode in modes:
            neg = [r for r in self.recs if r["op"] == "div_round.checked.i64." + mode
                   and any(int(a["v"]) < 0 for a in r["args"])]
            self.assertGreaterEqual(len(neg), 2, mode)
        for op in ("sqrt.checked.q1_15.ceil", "sqrt.checked.q1_15.away", "sqrt.checked.q1_15.floor",
                   "sqrt.checked.q1_63.ceil", "sum.checked.i64.i64", "fold_checked.add.i64",
                   "sum_sat.sat.i8", "sum_wrap.wrap.i8"):
            self.assertIn(op, ops)
        for name in ("add", "sub", "mul"):
            for form in ("checked", "wrap", "sat"):
                self.assertTrue(any(o.startswith("%s.%s." % (name, form)) for o in ops), (name, form))


class ExpectHeader(unittest.TestCase):
    """gen_expect.header reads the header after a leading byte-order mark."""

    def test_header_after_byte_order_mark(self):
        import gen_expect
        with open(os.path.join(ROOT, "conformance", "diag", "c1002_byte_order_mark.ci"), "rb") as fh:
            text = fh.read().decode("utf-8", "replace")
        h = gen_expect.header(text)
        self.assertEqual(h["case"], "diag/c1002_byte_order_mark")
        self.assertEqual(h["clause"], "SPEC-04 3.1; SPEC-04 17.1, 17.2")
        self.assertEqual(h["entries"], ["run"])


if __name__ == "__main__":
    unittest.main()
