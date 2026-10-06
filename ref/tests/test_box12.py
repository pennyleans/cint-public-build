import os, sys; sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # put ref/ on the path
"""Tests for box 12 step 2 (docs/design/notes/2026-10-05-box12-errors-cleanup-lifetimes.md,
section 7): error sets and error unions, `try`, `catch`, `as?`, the result built-ins, `defer`
and `errdefer`, and `.expect` format 3 (SPEC-09 CONF-11 rule 12). `HeldCases` runs the 42
box 12 cases of conformance/errors and conformance/cleanup, held until the box 12 freeze
(OQ-206), and compares each outcome with the one its header states; the other classes pin the rules one at a time, and the readings and
refusals of ref/OPEN.md REF-OQ-43 and REF-OQ-44."""
import re
import unittest

from cint_ref import expect
from cint_ref.exec import run_program

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CONFORMANCE = os.path.join(ROOT, "conformance")


def run(src, entry="run", fuel=None):
    return run_program(src.encode("utf-8"), "t.ci", entry, fuel)


def at(src, needle, nth=0):
    """(line, column) of the nth occurrence of `needle` in `src`."""
    i = -1
    for _ in range(nth + 1):
        i = src.index(needle, i + 1)
    return src.count("\n", 0, i) + 1, i - (src.rfind("\n", 0, i) + 1) + 1


def summary(o):
    """(outcome, detail[, fuel]): the return value, the error lines, the fault's code, operation,
    operands, exact, limit, position and stack, or the diagnostic's code and position."""
    if o.kind == "value":
        return o.kind, o.value.render() if o.value is not None else None, o.fuel
    if o.kind == "error":
        return o.kind, o.error, o.fuel
    if o.kind == "fault":
        r = o.record
        return o.kind, (r.code, r.operation, [v.render() for v in r.operands], r.exact,
                        r.limit.render() if r.limit else None, (r.position.line, r.position.column),
                        [(p.line, p.column) for p in r.stack]), o.fuel
    if o.kind == "compile-error":
        p = o.diagnostic.position
        return o.kind, (o.diagnostic.code, (p.line, p.column))
    return o.kind, o.message


def refused(src, entry="run"):
    o = run(src, entry)
    assert o.kind == "refused", summary(o)
    return o.message


MAX = "I64 9223372036854775807"
OVERFLOW = ["I64 1", MAX], 9223372036854775808, MAX

# The outcome each box 12 case's header states (`spec outcome`), with its fuel.
HELD = {
    "errors/as_question_narrow_catch": ("value", "I64 -901", 1),
    "errors/as_question_try_narrow": ("error", ("ArithError", "narrow", "U16 4"), 2),
    "errors/c2001_narrow_value": ("compile-error", ("C2001", (13, 20))),
    "errors/c2040_unconsumed_call": ("compile-error", ("C2040", (14, 13))),
    "errors/c2042_return_wider_value": ("compile-error", ("C2042", (13, 12))),
    "errors/c2042_try_from_wider_set": ("compile-error", ("C2042", (16, 13))),
    "errors/c2043_try_not_error_union": ("compile-error", ("C2043", (14, 13))),
    "errors/c2044_shared_name_short_form": ("compile-error", ("C2044", (12, 12))),
    "errors/c2054_zero_filled_error_value": ("compile-error", ("C2054", (10, 5))),
    "errors/c4002_catch_block_completes": ("compile-error", ("C4002", (15, 31))),
    "errors/c4020_switch_missing_error_value": ("compile-error", ("C4020", (15, 9))),
    "errors/catch_bind_switch_exhaustive": ("value", "I64 688", 9),
    "errors/catch_value_and_discard": ("value", "I64 49", 4),
    "errors/combined_set_once_at_first_position":
        ("error", ("errors.combined_set_once_at_first_position.Front", "bad_byte", "U16 4"), 2),
    "errors/combined_try_converts_tag":
        ("error", ("errors.combined_try_converts_tag.IoOrParse", "bad_digit", "U32 4"), 2),
    "errors/div_result_error_values": ("value", "I64 479", 4),
    "errors/error_values_in_array_struct_param": ("value", "I64 19", 7),
    "errors/main_returns_error": ("error", ("errors.main_returns_error.IoError", "full", "U16 2"), 1),
    "errors/shared_name_written_with_set":
        ("error", ("errors.shared_name_written_with_set.IoOrQueue", "QueueError.full", "U16 3"), 1),
    "errors/tags_from_one": ("error", ("errors.tags_from_one.Stage", "third", "U16 3"), 1),
    "errors/test_try_any_set": ("error", ("errors.test_try_any_set.ParseError", "bad_digit", "U16 2"), 3),
    "errors/underlying_type_u8": ("error", ("errors.underlying_type_u8.IoError", "full", "U8 2"), 1),
    "errors/widen_in_assignment_and_compare": ("value", "I64 51", 2),
    "cleanup/c4030_break_in_deferred_block": ("compile-error", ("C4030", (11, 13))),
    "cleanup/c4030_try_in_deferred_statement": ("compile-error", ("C4030", (18, 15))),
    "cleanup/c4031_errdefer_inside_deferred_statement": ("compile-error", ("C4031", (19, 9))),
    "cleanup/c4031_errdefer_script_top_level": ("compile-error", ("C4031", (7, 1))),
    "cleanup/c4031_errdefer_without_error_union": ("compile-error", ("C4031", (10, 5))),
    "cleanup/defer_charges_no_fuel": ("value", "I64 0", 6),
    "cleanup/defer_consumes_error_union": ("value", "I64 5", 3),
    "cleanup/defer_in_catch_block": ("value", "I64 17175", 5),
    "cleanup/defer_in_loop_continue_break": ("value", None, 5),
    "cleanup/defer_reverse_order": ("value", None, 1),
    "cleanup/defer_runs_at_block_exit": ("value", None, 1),
    "cleanup/errdefer_on_returned_error_value": ("value", "I64 19", 3),
    "cleanup/errdefer_skipped_on_success": ("value", "I64 7", 2),
    "cleanup/fault_in_deferred_statement":
        ("fault", ("E_OVERFLOW", "add.checked.i64") + OVERFLOW + ((9, 17), [(14, 12)]), 2),
    "cleanup/no_defer_after_fault":
        ("fault", ("E_DIV_ZERO", "div.checked.i64", ["I64 10", "I64 0"], None, None, (11, 15), [(15, 12)]), 2),
    "cleanup/return_struct_before_defer": ("value", "I64 347855", 4),
    "cleanup/return_value_before_defer": ("value", "I64 340", 2),
    "cleanup/script_defer_runs_at_end": ("value", None, 1),
    "cleanup/test_errdefer_runs_when_error_leaves":
        ("error", ("cleanup.test_errdefer_runs_when_error_leaves.ParseError", "bad_digit", "U16 2"), 2),
    "cleanup/try_runs_defer_and_errdefer":
        ("error", ("cleanup.try_runs_defer_and_errdefer.ParseError", "bad_digit", "U16 2"), 2),
}

# The module variables the headers state, and the bytes the cases print.
STATE = {
    "cleanup/defer_charges_no_fuel": {"calls": "I64 3"},
    "cleanup/defer_consumes_error_union": {"trail": "I64 4"},
    "cleanup/defer_in_catch_block": {"trail": "I64 17175"},
    "cleanup/errdefer_on_returned_error_value": {"trail": "I64 5"},
    "cleanup/errdefer_skipped_on_success": {"trail": "I64 1"},
    "cleanup/no_defer_after_fault": {"cleaned": "I64 0"},
    "cleanup/return_value_before_defer": {"seen": "I64 0"},
    "cleanup/test_errdefer_runs_when_error_leaves": {"trail": "I64 21"},
    "cleanup/try_runs_defer_and_errdefer": {"trail": "I64 132"},
}
STDOUT = {
    "errors/main_returns_error": b"partial\n",
    "cleanup/defer_in_loop_continue_break": b"a\n0\n1\na\n2\n3\n",
    "cleanup/defer_reverse_order": b"0\n3\n2\n1\n",
    "cleanup/defer_runs_at_block_exit": b"0\n1\n2\n",
    "cleanup/script_defer_runs_at_end": b"a\nb\nc\n",
}


def run_case(rel):
    with open(os.path.join(CONFORMANCE, rel + ".ci"), "rb") as f:
        data = f.read()
    m = re.search(r"^// entry: (\w+)", data.decode("utf-8"), re.M)
    return run_program(data, rel + ".ci", m.group(1) if m else None, root=CONFORMANCE)


class HeldCases(unittest.TestCase):
    """Each box 12 case gives the outcome its header states (all 42 frozen, by the box 12
    freeze and after box 12 step 4; ref/OPEN.md I-8), and the case of the LS-316 defect fix."""

    def test_outcomes(self):
        for rel, want in HELD.items():
            with self.subTest(case=rel):
                o = run_case(rel)
                self.assertEqual(summary(o), want)
                self.assertEqual({n: v.render() for _, n, v in o.state} if o.kind != "compile-error" else {},
                                 STATE.get(rel, {}))
                self.assertEqual(o.stdout, STDOUT.get(rel, b""))

    def test_error_outcomes_render_in_format_3(self):
        o = run_case("errors/main_returns_error")
        text = expect.render(expect.from_outcome(o, "errors/main_returns_error", None, fmt=3))
        self.assertIn("format 3\nsource reference\noutcome error\nstdout-bytes 8\n", text)
        self.assertTrue(text.endswith("fuel-consumed 1\nerror.set errors.main_returns_error.IoError\n"
                                      "error.value full\nerror.tag U16 2\n"))


ERR = "error E { a, b }\n\n"
G = "E!I64 g(I64 k) {\n    if (k > 0) {\n        return .a;\n    }\n    return k;\n}\n\n"


class ErrorSets(unittest.TestCase):
    """Declared and combined sets (SPEC-04 LS-92, LS-93, LS-97, LS-314)."""

    def test_tags_count_from_one_in_the_underlying_type(self):
        o = run("error E : U8 { a, b, c }\n\nexport E!I64 run() {\n    return .c;\n}\n")
        self.assertEqual(summary(o), ("error", ("t.E", "c", "U8 3"), 1))

    def test_a_set_that_appears_twice_is_included_once(self):
        # ABA = A | BA, where BA = B | A: a1 1, a2 2, b1 3, and A is not included again.
        src = ("error A { a1, a2 }\nerror B { b1 }\nerror BA = B | A;\nerror ABA = A | BA;\n\n"
               "B!I64 f() {\n    return .b1;\n}\n\nexport ABA!I64 run() {\n    return try f();\n}\n")
        self.assertEqual(summary(run(src)), ("error", ("t.ABA", "b1", "U16 3"), 2))

    def test_c2031_when_the_underlying_type_cannot_hold_every_tag(self):
        # 256 values need tag 256, past U8; the position is the `error` keyword (REF-OQ-43).
        names = ", ".join("v%d" % i for i in range(256))
        src = "export I64 run() {\n    return 0;\n}\n\nerror Big : U8 { %s }\n" % names
        self.assertEqual(summary(run(src)), ("compile-error", ("C2031", at(src, "error Big"))))
        self.assertEqual(run(src.replace(", v255", "")).kind, "value")

    def test_widening_and_narrowing(self):
        src = (ERR + "error F { f }\nerror EF = E | F;\n\nexport I64 run() {\n    EF x = E.b;\n"
               "    F y = F.f;\n    x = y;\n    return x == F.f ? 1 : 0;\n}\n")
        self.assertEqual(summary(run(src)), ("value", "I64 1", 1))
        bad = src.replace("    x = y;\n", "    y = x;\n")
        self.assertEqual(summary(run(bad)), ("compile-error", ("C2001", at(bad, "x;\n    return"))))

    def test_name_without_an_error_set_context_is_c2007(self):
        # REF-OQ-43: an I64 context is no context for `.name` (SPEC-04 LS-60).
        src = ERR + "export I64 run() {\n    I64 x = .b;\n    return x;\n}\n"
        self.assertEqual(summary(run(src)), ("compile-error", ("C2007", at(src, ".b"))))


class Unions(unittest.TestCase):
    """`try`, `catch` and `_ =` (SPEC-04 LS-95, LS-129 to LS-132)."""

    def test_try_returns_the_error_and_catch_gives_the_fallback(self):
        src = ERR + G + "E!I64 h(I64 k) {\n    I64 v = try g(k);\n    return v + 1;\n}\n\n" \
            "export I64 run() {\n    return (h(0) catch 50) + (h(1) catch 70);\n}\n"
        self.assertEqual(summary(run(src)), ("value", "I64 71", 5))

    def test_a_catch_fallback_runs_only_on_an_error(self):
        # REF-OQ-43: a constant fallback is an operand that run time may skip (D-9, REF-OQ-21).
        src = ERR + G + "export I64 run(I64 k) {\n    return g(k) catch 9223372036854775807 + 1;\n}\n"
        o = run_program(src.encode(), "t.ci", "run", args=[_i64(0)])
        self.assertEqual(summary(o), ("value", "I64 0", 2))
        o = run_program(src.encode(), "t.ci", "run", args=[_i64(1)])
        self.assertEqual(summary(o)[1][:6], ("E_OVERFLOW", "add.checked.i64", [MAX, "I64 1"], 9223372036854775808,
                                             MAX, at(src, "+ 1")))

    def test_a_catch_statement_is_c4012(self):
        # SPEC-04 LS-167: a `catch` expression cannot stand as a statement; `_ =` discards.
        src = ERR + G + "export I64 run() {\n    g(1) catch (e) {\n        return 1;\n    };\n    return 0;\n}\n"
        self.assertEqual(summary(run(src)), ("compile-error", ("C4012", at(src, "g(1)"))))
        ok = ERR + G + "export I64 run() {\n    _ = g(1);\n    return 0;\n}\n"
        self.assertEqual(summary(run(ok)), ("value", "I64 0", 2))


class Results(unittest.TestCase):
    """The result built-ins and `as?` return `ArithError` values (SPEC-01 IM-28, IM-30; LS-133)."""

    def test_each_fault_code_has_its_value(self):
        for expr, value in [("add_result(x, 1)", ("overflow", "U16 1")), ("div_result(x, z)", ("div_zero", "U16 2")),
                            ("shl_result(1, s)", ("shift", "U16 3"))]:
            with self.subTest(expr=expr):
                src = ("export ArithError!I64 run() {\n    I64 x = I64.max;\n    I64 z = 0;\n    I64 s = 64;\n"
                       "    return try %s;\n}\n" % expr)
                self.assertEqual(summary(run(src)), ("error", ("ArithError",) + value, 1))

    def test_as_question_narrows_or_returns_narrow(self):
        src = ("export ArithError!I64 run() {\n    I64 x = %d;\n    I8 y = try (x as? I8);\n"
               "    return y as I64;\n}\n")
        self.assertEqual(summary(run(src % 99)), ("value", "I64 99", 1))
        self.assertEqual(summary(run(src % 300)), ("error", ("ArithError", "narrow", "U16 4"), 1))


class Cleanup(unittest.TestCase):
    """`defer` and `errdefer` (SPEC-04 LS-185 to LS-189, LS-316 to LS-322)."""

    def test_an_errdefer_belongs_to_its_block(self):
        # REF-OQ-43: the inner block exits normally, so its errdefer is dropped; the error that
        # leaves the function later runs only the errdefer still pending.
        src = ("error E { bad }\n\nI64 trail = 0;\n\nE!I64 g(I64 x) {\n    {\n        errdefer trail = trail + 1;\n"
               "        trail = trail + 10;\n    }\n    errdefer trail = trail + 100;\n    if (x > 0) {\n"
               "        return .bad;\n    }\n    return x;\n}\n\nexport I64 run(I64 x) {\n"
               "    I64 v = g(x) catch -1;\n    return trail * 1000 + v;\n}\n")
        self.assertEqual(summary(run_program(src.encode(), "t.ci", "run", args=[_i64(1)])),
                         ("value", "I64 109999", 2))
        self.assertEqual(summary(run_program(src.encode(), "t.ci", "run", args=[_i64(0)])),
                         ("value", "I64 10000", 2))

    def test_defer_runs_when_a_try_leaves(self):
        src = (ERR + G + "I64 trail = 0;\n\nE!I64 h() {\n    defer trail = trail * 10 + 1;\n"
               "    errdefer trail = trail * 10 + 2;\n    I64 v = try g(1);\n    return v;\n}\n\n"
               "export I64 run() {\n    _ = h();\n    return trail;\n}\n")
        self.assertEqual(summary(run(src)), ("value", "I64 21", 3))

    def test_a_deferred_statement_cannot_change_a_returned_struct(self):
        # LS-316: the struct returned through an error union is taken before the deferred
        # statements run; they still write the variable, and a later one reads the write.
        src = ("struct P {\n    I64 a;\n    I64 b;\n}\n\nerror E { bad }\n\nI64 seen = 0;\n\n"
               "E!P take(I64 v) {\n    P p = P(v, v);\n    defer seen = p.a;\n    defer p.a = 50;\n"
               "    return p;\n}\n\nexport I64 run() {\n    P r = take(1) catch P(0, 0);\n"
               "    return r.a * 1000 + seen;\n}\n")
        self.assertEqual(summary(run(src)), ("value", "I64 1050", 2))

    def test_no_deferred_statement_runs_after_a_fault(self):
        src = ("I64 t = 0;\n\nI64 f(I64 d) {\n    defer t = 1;\n    return 10 / d;\n}\n\n"
               "export I64 run() {\n    return f(0);\n}\n")
        o = run(src)
        self.assertEqual(o.kind, "fault")
        self.assertEqual({n: v.render() for _, n, v in o.state}, {"t": "I64 0"})


class Refusals(unittest.TestCase):
    """Box 12 forms with no specified diagnostic, record or meaning (ref/OPEN.md REF-OQ-44)."""

    def test_refused(self):
        cases = {
            "signed underlying type": "error E : I8 { a }\n\nexport I64 run() {\n    return 0;\n}\n",
            "set combined from itself": "error A = B | A;\nerror B { b }\n\nexport I64 run() {\n    return 0;\n}\n",
            "union of an error set": ERR + "error F { f }\n\nE!F g() {\n    return .a;\n}\n\n"
                                     "export I64 run() {\n    return 0;\n}\n",
            "catch of a non-union": "I64 g() {\n    return 1;\n}\n\nexport I64 run() {\n    return g() catch 0;\n}\n",
            "catch of E!void": ERR + "E!void g() {\n    return;\n}\n\nexport I64 run() {\n    I64 v = g() catch 0;\n"
                               "    return v;\n}\n",
            "try in a function without a union": ERR + G + "export I64 run() {\n    return try g(1);\n}\n",
            "nested defer": "I64 t = 0;\n\nexport I64 run() {\n    defer {\n        defer t = 1;\n    }\n    return 0;\n}\n",
            "defer in a switch clause": "I64 t = 0;\n\nexport I64 run() {\n    I64 k = 1;\n    switch (k) {\n"
                                        "        case 1: defer t = 1;\n        default: t = 2;\n    }\n    return t;\n}\n",
            "conversion of an error value": ERR + "export I64 run() {\n    E e = E.b;\n    return e as I64;\n}\n",
            "rendering an error value": ERR + "export I64 run() {\n    E e = E.b;\n    \"{e}\\n\";\n    return 0;\n}\n",
            "module variable of a set": ERR + "E last = .a;\n\nexport I64 run() {\n    return 0;\n}\n",
            "context-typed returned value": ERR + "export E!I64 run() {\n    Bool c = true;\n    return c ? .a : .b;\n}\n",
            "range of error values": ERR + "export I64 run() {\n    E e = E.b;\n    switch (e) {\n"
                                     "        case .a..=.b: return 1;\n    }\n}\n",
            "assignment to the catch binding": ERR + G + "export I64 run() {\n    I64 v = g(1) catch (e) {\n"
                                               "        e = .b;\n        return 0;\n    };\n    return v;\n}\n",
            "failed assert of error values": ERR + "export I64 run() {\n    E e = E.a;\n    assert(e == E.b);\n"
                                             "    return 0;\n}\n",
            "index fault on error values": ERR + "export I64 run() {\n    E[2] es = [E.a, E.b];\n    I64 i = 2;\n"
                                           "    E x = es[i];\n    return 0;\n}\n",
            "error-set entry value": ERR + "export E run() {\n    return .a;\n}\n",
            "break in a catch block in a header": ERR + G + "export I64 run() {\n    I64 n = 0;\n"
                                                  "    while (n < (g(1) catch (e) { break; })) {\n        n = n + 1;\n"
                                                  "    }\n    return n;\n}\n",
        }
        for what, src in cases.items():
            with self.subTest(what=what):
                self.assertIn("REF-OQ-44", refused(src))
        script = ERR + G + "try g(0);\n"
        self.assertIn("REF-OQ-44", refused(script, entry=None))
        kernel = ("kernel k[n](in I64[n] a, out I64[n] b) over [i: n] {\n    errdefer b[i] = 0;\n    b[i] = a[i];\n}\n\n"
                  "export I64 run() {\n    return 0;\n}\n")
        self.assertIn("REF-OQ-44", refused(kernel))


class Format3(unittest.TestCase):
    """`.expect` format 3 (SPEC-09 CONF-11 rule 12)."""

    SRC = ERR + "export E!I64 run() {\n    return .b;\n}\n"

    def test_an_error_needs_format_3(self):
        o = run(self.SRC)
        for fmt in (1, 2):
            with self.assertRaises(ValueError):
                expect.from_outcome(o, "k/x", "test", fmt=fmt)
        text = expect.render(expect.from_outcome(o, "k/x", "test", fmt=3))
        self.assertEqual(text, "case k/x\nclause test\nformat 3\nsource reference\noutcome error\nstdout-bytes 0\n"
                               "fuel-consumed 1\nerror.set t.E\nerror.value b\nerror.tag U16 2\n")
        self.assertEqual(expect.parse(text).format, 3)
        four = expect.render(expect.from_outcome(o, "k/x", "test", fmt=4))
        self.assertEqual(four, text.replace("format 3\n", "format 4\n"))

    def test_parse_rejects(self):
        text = expect.render(expect.from_outcome(run(self.SRC), "k/x", "test", fmt=3))
        for bad in [text.replace("format 3\n", "format 2\n"),
                    text.replace("error.value b\n", ""),
                    text.replace("error.tag U16 2\n", "error.tag U16 2\nerror.tag U16 2\n"),
                    text.replace("fuel-consumed 1\n", "return I64 1\nfuel-consumed 1\n"),
                    text.replace("outcome error\n", "outcome value\n")]:
            with self.subTest(bad=bad):
                with self.assertRaises(ValueError):
                    expect.parse(bad)


def _i64(n):
    from cint_ref.types import Value
    return Value("I64", n)


if __name__ == "__main__":
    unittest.main()
