import os, sys; sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # put ref/ on the path
"""Tests for the slice 1 review findings: literal range checks off the evaluated
path, conditional constant faults, unsigned `mul_full`, constant `clamp` bounds,
literal-only shifts under rule 4, deep nesting, source-text error order, and
invalid CIF-1 fixtures; and for the G-C2 review findings COR-3 (nested fallthrough and
C4001), COR-7 (assert message holes), COR-8 (the run-time `clamp` refusal) and COR-10
(format widths and the 1 GiB print limit)."""
import subprocess
import tempfile
import unittest

from cint_ref import arith, cif1, fmt
from cint_ref.exec import run_program
from cint_ref.parser import MAX_NESTING
from cint_ref.types import Value

REF_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def run(src, entry="run"):
    data = src.encode("utf-8") if isinstance(src, str) else src
    return run_program(data, "t.ci", entry)


class LiteralRangeOffThePath(unittest.TestCase):
    """SPEC-01 3.2; SPEC-04 4.2 rules 1 and 7: every literal is range-checked."""

    def code(self, src):
        o = run(src)
        self.assertEqual(o.kind, "compile-error", (o.kind, o.value, o.message))
        return o.diagnostic.code, (o.diagnostic.position.line, o.diagnostic.position.column)

    def test_short_circuit_and(self):
        src = "const I8 K = 5; export Bool run() { Bool b = false && (K < 300); return b; }"
        self.assertEqual(self.code(src), ("C2003", (1, 60)))

    def test_short_circuit_or(self):
        src = "const I8 K = 5; export Bool run() { Bool b = true || (K < 300); return b; }"
        self.assertEqual(self.code(src), ("C2003", (1, 59)))

    def test_conditional_arm_not_chosen(self):
        src = "const I8 K = 5; export I8 run() { I8 y = false ? K + 300 : 1; return y; }"
        self.assertEqual(self.code(src), ("C2003", (1, 54)))

    def test_parenthesized_literal_points_at_the_literal(self):
        # REF-OQ-25: C2003 is reported at the literal, never at an enclosing `(`.
        self.assertEqual(self.code("export I8 run() { I8 m = (128); return m; }"), ("C2003", (1, 27)))
        self.assertEqual(self.code("export I8 run() { I8 m = -(-(128)); return m; }"), ("C2003", (1, 30)))

    def test_evaluation_order_still_decides_on_the_path(self):
        # SEED-08: the overflow at the first `+` comes before the literal 150 (REF-OQ-02).
        o = run("const I8 X = 100 + 100 - 150; export I64 run() { return 0; }")
        self.assertEqual(o.diagnostic.code, "C6001")
        self.assertEqual(o.diagnostic.fault.code, "E_OVERFLOW")


class ConditionalConstantFaults(unittest.TestCase):
    """REF-OQ-20 and REF-OQ-21, decided by D-9 (decision 2026-10-03): a skipped operand
    of a constant expression is not evaluated, and a constant operand that a run-time `&&`,
    `||` or `?:` may skip runs, and faults, only if reached."""

    def test_constant_and_skips_a_fault(self):
        o = run("export Bool run() { Bool b = false && (1 / 0 == 0); return b; }")
        self.assertEqual((o.kind, o.value), ("value", Value("Bool", False)))

    def test_constant_conditional_skips_a_fault(self):
        o = run("export I64 run() { I64 v = true ? 1 : 1 / 0; return v; }")
        self.assertEqual((o.kind, o.value), ("value", Value("I64", 1)))

    def test_runtime_and_guards_a_constant_fault(self):
        src = "export Bool run() { I64 x = %d; Bool b = x == 2 && (1 / 0 == 0); return b; }"
        o = run(src % 1)
        self.assertEqual((o.kind, o.value), ("value", Value("Bool", False)))
        o = run(src % 2)
        self.assertEqual((o.kind, o.record.code, o.record.operation, str(o.record.position)),
                         ("fault", "E_DIV_ZERO", "div.checked.i64", "t.ci:1:54"))

    def test_runtime_conditional_guards_a_constant_fault(self):
        o = run("export I64 run() { I64 x = 1; return x == 1 ? 1 : 1 / 0; }")
        self.assertEqual((o.kind, o.value), ("value", Value("I64", 1)))
        o = run("export I64 run() { I64 x = 0; return x == 1 ? 1 : 1 / 0; }")
        self.assertEqual((o.kind, o.record.code), ("fault", "E_DIV_ZERO"))

    def test_unguarded_constant_fault_is_c6001(self):
        o = run("export I64 run() { I64 x = 1; return x + 1 / 0; }")
        self.assertEqual(o.diagnostic.code, "C6001")


class MulFull(unittest.TestCase):
    """SPEC-01 2.4 and 4.10: U8..U32 Specified with the same signedness; U64 Proposed."""

    def test_unsigned_operation(self):
        self.assertEqual(arith.op("mul_full", "checked", "U8", Value("U8", 255), Value("U8", 255)),
                         Value("U16", 65025))
        self.assertEqual(arith.op("mul_full", "checked", "U32", Value("U32", 4294967295), Value("U32", 4294967295)),
                         Value("U64", 18446744065119617025))
        with self.assertRaises(arith.OpenCase):
            arith.op("mul_full", "checked", "U64", Value("U64", 1), Value("U64", 1))

    def test_unsigned_program(self):
        o = run("export U16 run() { U8 a = 255; U8 b = 255; return mul_full(a, b); }")
        self.assertEqual(o.kind, "value")
        self.assertEqual(o.value, Value("U16", 65025))

    def test_u64_is_refused(self):
        o = run("export I64 run() { U64 a = 2; _ = mul_full(a, a); return 0; }")
        self.assertEqual(o.kind, "refused")


class ClampConstantBounds(unittest.TestCase):
    """SPEC-01 4.8: constant bounds lo > hi are a compile error, reached or not."""

    def test_unreached_call(self):
        o = run("export I64 run() { I64 x = 5; if (x == 0) { return clamp(x, 10, 1); } return x; }")
        self.assertEqual(o.kind, "compile-error")
        self.assertEqual(o.diagnostic.code, "C6006")

    def test_all_constant(self):
        self.assertEqual(run("export I64 run() { return clamp(5, 10, 1); }").kind, "compile-error")

    def test_runtime_bounds_stay_refused(self):
        o = run("export I64 run() { I64 x = 5; I64 h = 1; return clamp(x, 10, h); }")
        self.assertEqual(o.kind, "refused")

    def test_runtime_refusal_names_the_decided_fault(self):
        # G-C2 review COR-8: SPEC-01 IM-49 decided E_DOMAIN; cint_ref does not implement it yet.
        o = run("export I64 run() { I64 x = 5; I64 h = 1; return clamp(x, 10, h); }")
        self.assertIn("E_DOMAIN (SPEC-01 IM-49), which cint_ref does not implement yet", o.message)
        self.assertNotIn("Open", o.message)


class NestedFallthroughCompletion(unittest.TestCase):
    """G-C2 review COR-3: a `fallthrough;` nested in a block, `if` or loop of a clause runs as
    a no-op and completes, so a value-returning function that reaches its closing brace
    through one is C4001 at its name; a clause whose last statement is `fallthrough;`
    continues into the next clause and does not complete."""

    def code(self, clauses, params="I64 x"):
        o = run("I64 f(%s) {\n    switch (x) {\n%s\n    }\n}\n" % (params, clauses), "f")
        if o.kind != "compile-error":
            return o.kind
        return o.diagnostic.code, o.diagnostic.position.line, o.diagnostic.position.column

    def test_block(self):
        self.assertEqual(self.code("case 0: { fallthrough; }\ndefault: return 3;"), ("C4001", 1, 5))

    def test_if_arms(self):
        clauses = "case 1: if (x == 1) { fallthrough; } else { fallthrough; }\ndefault: return 7;"
        self.assertEqual(self.code(clauses), ("C4001", 1, 5))

    def test_last_clause_of_a_full_range_switch(self):
        self.assertEqual(self.code("case -128..=127: { fallthrough; }", "I8 x"), ("C4001", 1, 5))

    def test_direct_final_fallthrough_does_not_complete(self):
        src = ("I64 f(I64 x) {\n    I64 r = 0;\n    switch (x) {\n        case 0: { r += 1; fallthrough; } r += 10; "
               "fallthrough;\n        default: return r + 100;\n    }\n}\n")
        o = run_program(src.encode("utf-8"), "t.ci", "f", args=[Value("I64", 0)])
        self.assertEqual((o.kind, o.value), ("value", Value("I64", 111)))

    def test_nested_no_op_runs(self):
        src = ('void main() {\n    I64 x = 1;\n    switch (x) {\n        case 1:\n            if (x == 1) {\n'
               '                fallthrough;\n            }\n            "after\\n";\n        default:\n'
               '            "d\\n";\n    }\n}\n')
        o = run(src, None)
        self.assertEqual((o.kind, o.stdout, o.fuel), ("value", b"after\n", 1))


class AssertMessageHoles(unittest.TestCase):
    """G-C2 review COR-7: an assert message is never evaluated (SPEC-04 LS-191), so its holes
    fold as skipped operands (SPEC-01 IM-103): no C6001 from a constant hole, while the
    literal range checks, literal conversions (IM-23) and constant `clamp` bounds stay."""

    def message(self, holes, cond="x == 5"):
        return run('export I64 run() {\n    I64 x = 5;\n    assert(%s, "%s");\n    return x;\n}\n' % (cond, holes))

    def test_constant_fault_is_not_folded(self):
        o = self.message("never {1 / 0} {I64.max + 1} {1 << 64}")
        self.assertEqual((o.kind, o.value), ("value", Value("I64", 5)))

    def test_failing_assert_does_not_evaluate_the_message(self):
        r = self.message("never {1 / 0}", "x == 6").record
        self.assertEqual((r.code, str(r.position)), ("E_ASSERT", "t.ci:3:12"))

    def test_literal_range_is_checked(self):
        d = self.message("{9223372036854775808}").diagnostic
        self.assertEqual((d.code, d.position.line, d.position.column), ("C2003", 3, 22))

    def test_literal_conversion_is_decided(self):
        d = self.message("{300 as U8}").diagnostic
        self.assertEqual((d.code, d.position.column, d.fault.code, d.fault.operation), ("C6001", 26, "E_NARROW", "unassigned"))

    def test_constant_clamp_bounds(self):
        self.assertEqual(self.message("{clamp(x, 5, 1)}").diagnostic.code, "C6006")

    def test_print_hole_is_still_folded(self):
        o = run('void main() {\n    "{1 / 0}";\n}\n', None)
        self.assertEqual((o.diagnostic.code, o.diagnostic.fault.code), ("C6001", "E_DIV_ZERO"))


class PrintLimits(unittest.TestCase):
    """G-C2 review COR-10: a format width of 2^63 or more is refused when its hole is checked,
    and a print statement whose text would pass 1 GiB (rt/OPEN.md RT-OQ-23) is refused before
    the text is built, where Python stopped with OverflowError or MemoryError."""

    def script(self, statement):
        return run('I64 x = 1;\n"before\\n";\n%s\n' % statement, None)

    def test_width_past_i64(self):
        for w in ("99999999999999999999", "9223372036854775808"):
            o = self.script('"{x:>%s}";' % w)
            self.assertEqual((o.kind, o.stdout), ("refused", b""), w)
            self.assertIn("RT-OQ-23", o.message)

    def test_width_past_i64_unreached(self):
        o = run('export I64 run() {\n    if (false) {\n        "{1:>9223372036854775808}";\n    }\n    return 1;\n}\n')
        self.assertEqual(o.kind, "refused")

    def test_spec_rules_come_first(self):
        self.assertEqual(self.script('"{x:99999999999999999999}";').diagnostic.code, "C1037")

    def test_padding_past_1_gib(self):
        holes = [("I64", h) for h in ("{x:>9223372036854775807}", "{x:>1073741825}", "{x:01073741825}",
                                      "{x=:<1073741823}")] + [("U64", "{x!lanes(U8):>1073741823}")]
        for ty, hole in holes:
            o = run('%s x = 1;\n"before\\n";\n"%s";\n' % (ty, hole), None)
            self.assertEqual((o.kind, o.stdout, o.fuel), ("refused", b"before\n", 1), hole)
            self.assertIn("RT-OQ-23", o.message)

    def test_statement_limit(self):
        # The limit is on the whole statement, as the runtime stages it: 12 bytes pass, 13 do not.
        old = fmt.PRINT_MAX
        fmt.PRINT_MAX = 12
        try:
            o = self.script('"{x:>4}{x:*<8}";')
            self.assertEqual((o.kind, o.stdout), ("value", b"before\n   11*******"))
            for statement in ('"{x:>4}{x:*<9}";', '"{x:>4}{x:*<8}!";', '"ab{x:>4}{x:07}";', '"{x=:>11}";'):
                o = self.script(statement)
                self.assertEqual((o.kind, o.stdout), ("refused", b"before\n"), statement)
            src = 'export I64 run() {\n    I64[2] a;\n    "{a:>4}";\n    return 0;\n}\n'
            self.assertEqual(run(src).stdout, b"[   0,    0]")
            o = run(src.replace(">4", ">5"))
            self.assertEqual((o.kind, o.stdout), ("refused", b""))
        finally:
            fmt.PRINT_MAX = old


class ShiftUnderRule4(unittest.TestCase):
    """REF-OQ-22, decided by D-9 (OQ-139): the left operand of a literal-only shift is never
    typed by rule 4, so it is I64 (rule 6) and `I8 + I64` is C2001 at that operand."""

    def test_binary_operand(self):
        o = run("export I8 run() { I8 x = 1; I8 y = x + (1 << 6); return y; }")
        p = o.diagnostic.position
        self.assertEqual((o.kind, o.diagnostic.code, p.line, p.column), ("compile-error", "C2001", 1, 40))

    def test_comparison_operand(self):
        o = run("export Bool run() { I8 x = 0; return x < (1 << 10); }")
        self.assertEqual((o.kind, o.diagnostic.code), ("compile-error", "C2001"))

    def test_i64_operand_is_valid(self):
        o = run("export I64 run() { I64 x = 1; return x + (1 << 6); }")
        self.assertEqual(o.value, Value("I64", 65))

    def test_whole_expression_context_is_kept(self):
        o = run("export I8 run() { I8 y = (1 << 6) + 1; return y; }")
        self.assertEqual(o.value, Value("I8", 65))


class DeepNesting(unittest.TestCase):
    """SPEC-04 20 (Proposed): at least 256 levels; beyond the limit a diagnostic, never a crash."""

    def value(self, src):
        o = run(src)
        self.assertEqual(o.kind, "value", (o.kind, o.diagnostic, o.message))
        return o.value.value

    def test_parentheses_256(self):
        self.assertEqual(self.value("export I64 run() { I64 x = 1; return %sx%s; }" % ("(" * 256, ")" * 256)), 1)

    def test_operator_chain_2000(self):
        self.assertEqual(self.value("export I64 run() { I64 x = 1; return %s; }" % " + ".join(["x"] * 2000)), 2000)

    def test_blocks_256(self):
        src = "export I64 run() { I64 x = 1; %s x = x + 1; %s return x; }" % ("if (x == 1) { " * 256, "} " * 256)
        self.assertEqual(self.value(src), 2)

    def test_block_257_is_c9004(self):
        # SPEC-09 CINTC-02 and D-5: the function body is not counted; the 257th `{` is C9004.
        src = "export I64 run() {\n%s\n%s\n    return 1;\n}\n" % ("{" * 257, "}" * 257)
        o = run(src)
        p = o.diagnostic.position
        self.assertEqual((o.diagnostic.code, p.line, p.column), ("C9004", 2, 257))

    def test_unary_256(self):
        self.assertEqual(self.value("export I64 run() { I64 x = 1; return %sx; }" % ("~" * 256)), 1)

    def test_beyond_the_limit_is_a_diagnostic(self):
        n = MAX_NESTING + 1
        o = run("export I64 run() { return %s1%s; }" % ("(" * n, ")" * n))
        self.assertEqual((o.kind, o.diagnostic.code), ("compile-error", "C9004"))


class SourceTextOrder(unittest.TestCase):
    """SPEC-04 17.1: the source-text error with the smallest (line, column) is reported."""

    def first(self, data):
        o = run(data)
        p = o.diagnostic.position
        return o.diagnostic.code, (p.line, p.column)

    def test_lone_cr_before_invalid_utf8(self):
        self.assertEqual(self.first(b"// a\rb\nexport I64 run() {\n    // \xff\n    return 1;\n}\n"), ("C1004", (1, 5)))

    def test_bidi_before_invalid_utf8(self):
        data = "// a‮b\n".encode() + b"export I64 run() {\n    // \xff\n    return 1;\n}\n"
        self.assertEqual(self.first(data), ("C1003", (1, 5)))

    def test_invalid_utf8_alone(self):
        self.assertEqual(self.first(b"export I64 run() {\n    // \xff\n    return 1;\n}\n"), ("C1002", (2, 8)))


class InvalidFixtures(unittest.TestCase):
    BAD = [
        '{"id":"a","status":"S","op":"div.wrap.i8","args":[{"t":"I8","v":"1"},{"t":"I8","v":"1"}],"expect":{"value":{"t":"I8","v":"1"}}}',
        '{"id":"b","status":"S","op":"add.checked.i8","args":[{"t":"I8","v":"200"},{"t":"I8","v":"1"}],"expect":{"value":{"t":"I8","v":"1"}}}',
        '{"id":"c","status":"S","op":"add.checked.i8","args":[{"t":"I16","v":"1"},{"t":"I8","v":"1"}],"expect":{"value":{"t":"I8","v":"2"}}}',
        '{"id":"d","status":"S","op":"add.checked.i8","args":[{"t":"I8","v":"1"}],"expect":{"value":{"t":"I8","v":"1"}}}',
        '{"id":"e","status":"S","op":"shl.sat.i8","args":[{"t":"I8","v":"1"},{"t":"I8","v":"1"}],"expect":{"value":{"t":"I8","v":"2"}}}',
    ]

    def test_each_is_invalid(self):
        for line in self.BAD:
            with self.assertRaises(cif1.Invalid, msg=line):
                cif1.check(cif1.parse_line(line))

    def test_cli_exits_1(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "bad.cif1.jsonl")
            with open(p, "wb") as f:
                f.write(("\n".join(self.BAD) + "\n").encode())
            env = dict(os.environ, PYTHONPATH=REF_DIR)
            r = subprocess.run([sys.executable, "-m", "cint_ref", "cif1", p], capture_output=True, env=env)
            out = r.stdout.decode("ascii")
            self.assertEqual(r.returncode, 1, out)
            self.assertIn("5 records, 0 agree, 0 disagree, 0 unsupported, 5 invalid", out)


if __name__ == "__main__":
    unittest.main()
