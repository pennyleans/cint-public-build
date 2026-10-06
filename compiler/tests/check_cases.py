"""Source files for the resolver and checker tests of the `front` suite (slice 2 task 2.10c).

Every source is compared with `cint_ref`'s outcome (check_suite.py), so this module only
produces inputs, each (name, bytes):

  - written(): hand-written modules of the hello subset of compiler/check.ci, valid and
    invalid, at least one for each diagnostic the checker gives, the D-9 and D-17 rules of
    constant evaluation, scripts and the D-22 note;
  - grid(): every operand form in every operator and every context, so that each path of
    type inference and literal settling (SPEC-01 IM-21) meets each kind of operand;
  - consts(): constant expressions that fault, overflow or narrow, in each place a
    constant expression is evaluated, skipped or walked;
  - widths(): the eight integer widths in every position, pairwise for conversions and
    mixed operands (task 2.13 part a);
  - prelude_names(): modules that declare a prelude name and use it (G-C2 review COR-4);
  - mutants(): seeded single-token mutations of the written modules.
"""
import pathlib
import random

ROOT = pathlib.Path(__file__).resolve().parents[2]

I64_MAX = (1 << 63) - 1

PRELUDE_HEAD = """I64 g() { return 1; }
void v() { }
Bool p(I64 a, Bool c) { return c && a > 0; }
const I64 K = 5;
const Bool KB = true;
"""


def in_function(body: str, result: str = "I64", tail: str = "    return 0;\n") -> bytes:
    """`body` inside a function with I64 i and Bool b parameters, after the shared items."""
    return (PRELUDE_HEAD + "export %s run(I64 i, Bool b) {\n%s\n%s}\n" % (result, body, tail)).encode("utf-8")


def in_script(body: str) -> bytes:
    return ("I64 i = 3;\nBool b = true;\nI64 g() { return 1; }\nvoid v() { }\n%s\n\"done\\n\";\n"
            % body).encode("utf-8")


# Whole modules: (name, text).
MODULES = [
    ("hello", 'I64 x = 21 * 2;\n"hello, world\\n";\n"x = {x}\\n";\n'),
    ("hello_main", 'void main() {\n    I64 x = 6 * 7;\n    "x = {x}, again {x=}\\n";\n}\n'),
    ("hello_bool", 'Bool ok = 1 < 2;\n"ok = {ok}\\n";\n"{ok=} {!ok}\\n";\n'),
    ("hello_holes", 'I64 a = 1;\nI64 b = 2;\n"{a + b} {a * b - 3} {(a << 3) >> 1} {a == b}\\n";\n'),
    ("hello_hole_literal", '"{1} {-5} {9223372036854775807} {I64.min} {true}\\n";\n'),
    ("hello_hole_void", 'void f() { }\n"{f()}\\n";\n'),
    ("hello_hole_overflow", '"{9223372036854775807 + 1}\\n";\n'),
    ("hello_hole_range", '"{9223372036854775808}\\n";\n'),
    ("hello_hole_undefined", '"{y}\\n";\n'),
    ("empty", ""),
    ("only_comment", "// nothing\n"),
    ("profile", 'profile "cint-core-1";\nexport I64 run() { return 1; }\n'),
    ("functions",
     "I64 add(I64 a, I64 b) { return a + b; }\nBool pos(I64 a) { return a > 0; }\n"
     "export I64 run() {\n    I64 s = add(1, 2);\n    if (pos(s)) { return s; }\n    return 0;\n}\n"),
    ("forward_call", "export I64 run() { return later(2); }\nI64 later(I64 a) { return a * 2; }\n"),
    ("loops",
     "export I64 run(I64 n) {\n    I64 s = 0;\n    for k in 0..n { s += k; }\n    for k in 0..=n by 2 { s -= k; }\n"
     "    for (I64 j = 0; j < n; j += 1) { s = s +% j; }\n    while (s > 100) { s /= 2; }\n"
     "    outer: while (true) {\n        inner: for k in 0..3 {\n            if (k == 1) { continue inner; }\n"
     "            if (k == 2) { break outer; }\n        }\n    }\n    return s;\n}\n"),
    ("else_if",
     "export I64 run(I64 x) {\n    if (x == 0) { return 1; } else if (x == 1) { return 2; } else { return 3; }\n}\n"),
    ("else_if_no_else",
     "export I64 run(I64 x) {\n    if (x == 0) { return 1; } else if (x == 1) { return 2; }\n}\n"),
    ("if_completes", "export I64 run(I64 x) {\n    if (x == 0) { return 1; } else { x = 2; }\n}\n"),
    ("while_true_return", "export I64 run() {\n    while (true) { return 1; }\n}\n"),
    ("return_in_block", "export I64 run() {\n    { return 1; }\n}\n"),
    ("break_then_more", "export I64 run() {\n    while (true) { break; }\n    return 2;\n}\n"),
    ("nested_return", "export I64 run(Bool c) {\n    if (c) { { return 1; } } else { if (c) { return 2; } else { return 3; } }\n}\n"),
    ("consts",
     "const I64 A = B * 2;\nconst I64 B = 3;\nconst Bool C = A > B;\nexport I64 run() {\n"
     "    const I64 D = A + B;\n    static_assert(C, \"c\");\n    return D;\n}\n"),
    ("const_cycle", "const I64 A = B + 1;\nconst I64 B = A + 1;\n"),
    ("const_self", "const I64 A = A;\n"),
    ("const_cycle_use", "export I64 run() { return A; }\nconst I64 A = B;\nconst I64 B = A;\n"),
    ("const_call", "I64 f() { return 1; }\nconst I64 A = f();\n"),
    ("const_local_name", "export I64 run() {\n    I64 x = 1;\n    const I64 K = x;\n    return K;\n}\n"),
    ("const_fault_order", "const I64 A = 1 / 0;\nconst I64 B = 9223372036854775807 + 1;\n"),
    ("const_fault_via_use", "export I64 run() { return B; }\nconst I64 B = 9223372036854775807 + 1;\n"),
    ("const_in_function_call", "export I64 run() { return K(1); }\nconst I64 K = 1 / 0;\n"),
    ("const_as_call", "const I64 K = 1;\nexport I64 run() { return K(1); }\n"),
    ("modvar", "I64 counter = 5;\nexport I64 run() { counter += 1; return counter; }\n"),
    ("modvar_forward", "export I64 show() {\n    return balance;\n}\n\nI64 balance = 5;\n"),
    ("modvar_nonconst", "I64 f() { return 1; }\nI64 x = f();\n"),
    ("modvar_nonconst_var", "I64 a = 1;\nI64 b = a + 1;\n"),
    ("modvar_no_init", "I64 a;\nexport I64 run() { return 1; }\n"),
    ("modvar_type_error", "Bool a = 1;\n"),
    ("modvar_fault", "I64 a = 1 / 0;\n"),
    # C5012 (SPEC-04 LS-122; G-C2 review COR-6): a module-level variable passed to a function
    # that writes it, directly or through the functions it calls, at the first such call.
    ("state_arg_direct",
     "I64 total = 0;\nvoid add(I64 v) { total += v; }\nexport I64 run() {\n    add(5);\n    add(total);\n"
     "    return total;\n}\n"),
    ("state_arg_transitive",
     "I64 total = 0;\nvoid add(I64 v) { total += v; }\nvoid twice(I64 v) { add(v); add(v); }\n"
     "export I64 run() {\n    twice(total);\n    return total;\n}\n"),
    ("state_arg_reader", "I64 total = 7;\nI64 peek(I64 v) { return v + total; }\nexport I64 run() { return peek(total); }\n"),
    ("state_arg_expression",
     "I64 total = 1;\nvoid add(I64 v) { total += v; }\nexport I64 run() {\n    add(total + 1);\n    add(-total);\n"
     "    return total;\n}\n"),
    ("state_arg_paren", "I64 total = 1;\nvoid add(I64 v) { total++; }\nexport I64 run() {\n    add((total));\n    return 0;\n}\n"),
    ("state_arg_second",
     "I64 a = 1;\nI64 b = 2;\nvoid f(I64 x, I64 y) { b = x + y; }\nexport I64 run() {\n    f(a, b);\n    return 0;\n}\n"),
    ("state_arg_nested",
     "I64 total = 1;\nI64 bump(I64 v) { total += v; return total; }\nexport I64 run() { return bump(bump(total)); }\n"),
    ("state_arg_builtin",
     "I64 total = 1;\nI64 bump(I64 v) { total += v; return total; }\nexport I64 run() { return max(bump(total), 1); }\n"),
    ("state_arg_recursive",
     "I64 total = 1;\nvoid a(I64 v) { if (v > 0) { b(v - 1); } }\nvoid b(I64 v) { a(v); total = v; }\n"
     "export I64 run() {\n    a(total);\n    return 0;\n}\n"),
    ("state_arg_test", "I64 total = 1;\nvoid add(I64 v) { total += v; }\ntest \"t\" {\n    add(total);\n}\n"),
    ("state_arg_later_site",
     "I64 total = 1;\nI64 peek(I64 v) { return v; }\nvoid add(I64 v) { total += v; }\nexport I64 run() {\n"
     "    I64 r = peek(total);\n    add(r);\n    if (r > 0) { add(total); }\n    return r;\n}\n"),
    ("state_arg_other_var",
     "I64 a = 1;\nI64 b = 2;\nvoid w() { b += 1; }\nvoid f(I64 v) { w(); }\nexport I64 run() {\n    f(a);\n"
     "    f(b);\n    return a;\n}\n"),
    ("main_value", "I64 main() { return 0; }\n"),
    ("main_bool", "Bool main() { return true; }\n"),
    ("main_void", "void main() { }\n"),
    ("script_main", '"x\\n";\nvoid main() { }\n'),
    ("script_main_late", 'I64 a = 1;\nvoid main() { }\na = 2;\n'),
    ("script_local_in_fn", 'I64 balance = 5;\nexport I64 read_balance() { return balance; }\n"ready\\n";\n'),
    ("script_local_in_const", 'I64 a = 1;\nconst I64 K = a;\n"x\\n";\n'),
    ("script_local_assign_stmt", 'I64 a = 1;\nexport I64 f() { return a; }\na = 2;\n'),
    ("script_local_if_stmt", 'I64 a = 1;\nexport I64 f() { return a; }\nif (true) { }\n'),
    ("script_local_call_stmt", 'I64 a = 1;\nvoid h() { a = 1; }\nh();\n'),
    ("script_use_before", 'a = 1;\nI64 a = 2;\n'),
    ("script_dup", 'I64 a = 1;\nI64 a = 2;\n"x\\n";\n'),
    ("script_shadow_fn", 'I64 g() { return 1; }\nI64 g = 2;\n"x\\n";\n'),
    ("script_return", 'return;\n'),
    ("script_return_value", 'return 1;\n'),
    ("script_break", 'break;\n'),
    ("script_static_assert", 'static_assert(1 < 2, "ok");\n"x\\n";\n'),
    ("dup_fn", "I64 f() { return 1; }\nI64 f() { return 2; }\n"),
    ("dup_const_fn", "const I64 f = 1;\nI64 f() { return 2; }\n"),
    ("dup_modvar", "I64 a = 1;\nI64 a = 2;\n"),
    ("dup_prelude_fn", "I64 count() { return 1; }\n"),
    ("dup_prelude_const", "const I64 max = 1;\n"),
    ("dup_builtin_type", "const I64 Str = 1;\n"),
    ("dup_param", "I64 f(I64 a, I64 a) { return a; }\n"),
    ("dup_param_global", "const I64 a = 1;\nI64 f(I64 a) { return a; }\n"),
    ("dup_param_fn", "I64 f(I64 f) { return 1; }\n"),
    ("dup_param_prelude", "I64 f(I64 count) { return 1; }\n"),
    ("dup_local_param", "I64 f(I64 a) { I64 a = 1; return a; }\n"),
    ("dup_local_nested", "I64 f() { I64 a = 1; { I64 a = 2; } return a; }\n"),
    ("local_sibling_ok", "I64 f() { { I64 a = 1; } { I64 a = 2; } return 0; }\n"),
    ("local_out_of_scope", "I64 f() { { I64 a = 1; } return a; }\n"),
    ("dup_loop_var", "I64 f() { I64 k = 0; for k in 0..3 { } return k; }\n"),
    ("loop_var_scope", "I64 f() { for k in 0..3 { } for k in 0..3 { } return 0; }\n"),
    ("loop_var_after", "I64 f() { for k in 0..3 { } return k; }\n"),
    ("for_c_scope", "I64 f() { for (I64 j = 0; j < 3; j += 1) { } return j; }\n"),
    ("for_c_dup", "I64 f() { I64 j = 0; for (I64 j = 0; j < 3; j += 1) { } return 0; }\n"),
    ("unknown_type_local", "I64 f() { Foo x = 1; return 0; }\n"),
    ("unknown_type_param", "I64 f(Foo x) { return 0; }\n"),
    ("unknown_type_result", "Foo f() { return 0; }\n"),
    ("unknown_type_const", "const Foo X = 1;\n"),
    ("unknown_type_as", "I64 f() { return 1 as Foo; }\n"),
    ("unknown_type_modvar", "Foo x = 1;\n"),
    ("type_is_fn", "I64 g() { return 1; }\nI64 f() { g x = 1; return 0; }\n"),
    ("inout_scalar", "I64 f(inout I64 a) { return a; }\n"),
    ("in_scalar", "I64 f(in I64 a) { return a; }\n"),
    ("inout_bool", "I64 f(I64 x, inout Bool a) { return x; }\n"),
    ("struct_out", "struct S { I64 a; }\n"),
    ("i32_out", "I32 f() { return 1; }\n"),
    ("switch_default", "export I64 run(I64 x) { switch (x) { default: return 1; } }\n"),
    ("assert_in", "export I64 run() { assert(true); return 1; }\n"),
    ("test_in", 'test "t" { }\n'),
    ("assert_not_bool", "export I64 run() { assert(1); return 1; }\n"),
    ("assert_message", 'export I64 run(I64 x) { assert(x > 0, "x={x}"); return x; }\n'),
    ("assert_message_void", 'void f() { }\nexport I64 run() { assert(true, "{f()}"); return 1; }\n'),
    ("test_dup", 'test "a" { }\ntest "a" { }\n'),
    ("test_dup_escape", 'test "a" { }\ntest "\\x61" { }\n'),
    ("test_script_local", 'I64 n = 1;\n"x";\ntest "t" { _ = n; }\n'),
    ("test_return_value", 'test "t" { return 1; }\n'),
    ("test_body_error", 'test "t" { I64 a = true; }\n'),
    ("array_out", "export I64 run() { I64[4] a; return 0; }\n"),
    ("builtin_out", "export I64 run() { return abs(-1); }\n"),
    # Format specifications (SPEC-04 LS-202 to LS-213): checked at the hole, as `cint_ref` does.
    ("spec_ok", 'I64 x = 1;\nU8 u = 2;\nBool c = true;\n"{x:>4}{x=:*^9_}{u:#010b}{x:+/100}{x:t}{c:<6}{x:}{u:c}\\n";\n'),
    ("spec_no_align", 'I64 x = 1;\n"{x:4}\\n";\n'),
    ("spec_named_bad", 'I64 x = 1;\n"a {x=:5} b\\n";\n'),
    ("spec_bool_sign", 'Bool c = true;\n"{c:+}\\n";\n'),
    ("spec_precision_scale", 'I64 x = 1;\n"{x:.2/100}\\n";\n'),
    ("spec_precision", 'I64 x = 1;\n"{x:.2}\\n";\n'),
    ("spec_malformed", 'I64 x = 1;\n"{x:>4q}\\n";\n'),
    ("spec_scale_bad", 'I64 x = 1;\n"{x:/20}\\n";\n'),
    ("spec_hex_comma", 'I64 x = 1;\n"{x:,x}\\n";\n'),
    ("spec_wide", 'I64 x = 1;\n"{x:>99999999999999999999}\\n";\n'),
    ("spec_wide_bad", 'I64 x = 1;\n"{x:99999999999999999999}\\n";\n'),
    ("spec_in_function", 'export I64 run(I64 v) { "{v:>8/100}"; "{v:_x}"; return 0; }\n'),
    ("spec_assert_message", 'export I64 run(I64 v) { assert(v > 0, "v={v:#x}"); return v; }\n'),
    ("spec_assert_message_bad", 'export I64 run(I64 v) { assert(v > 0, "v={v:#d}"); return v; }\n'),
    ("assert_message_clamp_bounds", 'export I64 run(I64 v) { assert(v > 0, "{clamp(v, 5, 1)}"); return v; }\n'),
    ("conv_out", 'I64 x = 1;\n"{x!bits}\\n";\n'),
    ("u8_prop_out", "export I64 run() { return U8.max as I64; }\n"),
    ("import_out", "import a.b;\n"),
    ("string_value_out", 'export I64 run() { I64 x = "s"; return 0; }\n'),
    ("named_arg_out", "I64 f(I64 a) { return a; }\nexport I64 run() { return f(a = 1); }\n"),
    ("void_hole_main", 'void f() { }\nvoid main() { "{f()}\\n"; }\n'),
    ("labels_bad", "export I64 run() { a: while (true) { break b; } return 0; }\n"),
    ("labels_continue_bad", "export I64 run() { a: while (true) { continue b; } return 0; }\n"),
    ("label_reuse", "export I64 run() { a: while (true) { a: while (true) { break a; } } return 0; }\n"),
    ("label_out_of_loop", "export I64 run() { a: while (true) { } break a; return 0; }\n"),
    ("continue_out", "export I64 run() { continue; return 0; }\n"),
    ("break_out", "export I64 run() { break; return 0; }\n"),
    ("static_assert_false", 'static_assert(1 > 2, "no");\n'),
    ("static_assert_nonconst", "I64 a = 1;\nstatic_assert(a > 0);\n"),
    ("static_assert_int", "static_assert(1);\n"),
    ("static_assert_fault", "static_assert(1 / 0 == 0);\n"),
    ("static_assert_skip", "static_assert(true || 1 / 0 == 0);\nstatic_assert(!(false && 1 / 0 == 0));\n"),
    ("static_assert_in_fn", "export I64 run() { static_assert(K > 1); return 0; }\nconst I64 K = 2;\n"),
    ("d9_false_and", "const Bool B = false && (1 / 0 == 0);\n"),
    ("d9_cond_skip", "const I64 C = true ? 1 : 1 / 0;\n"),
    ("d9_runtime", "export Bool run(I64 x) {\n    Bool b = x == 2 && (1 / 0 == 0);\n    return b;\n}\n"),
    ("d9_runtime_cond", "export I64 run(Bool c) {\n    return c ? 1 / 0 : 2;\n}\n"),
    ("d9_seed08", "const I64 X = 9223372036854775807 + 1 - 9223372036854775808;\n"),
    ("d9_skipped_range", "export Bool run(I64 x) { return false && (x < 9223372036854775808); }\n"),
    ("d9_skipped_range_runtime", "export Bool run(Bool c) { return c && (1 < 9223372036854775808); }\n"),
    ("d9_shift_typing", "export I64 run(I64 x) { return x + (1 << 6); }\n"),
    ("d9_shift_bool", "export Bool run(Bool c) { return c == (1 << 2); }\n"),
    ("d9_shift_bool_nested", "export Bool run(Bool c) { return c == -(1 + (2 << 1)); }\n"),
    ("d17_skipped_conversion", "const Bool B = false && (9223372036854775808 as I64 == 0);\n"),
    ("d17_runtime_skipped_conversion", "export Bool run(Bool c) { return c && (9223372036854775808 as I64 == 0); }\n"),
    ("d17_wrap_conversion", "const I64 W = 18446744073709551615 as% I64;\n"),
    ("d17_neg_conversion", "const I64 N = -9223372036854775809 as I64;\n"),
    ("d17_huge_conversion", "const I64 N = 0x" + "f" * 128 + " as I64;\n"),
    ("d17_huge_wrap", "const I64 N = -0x" + "f" * 1024 + " as% I64;\n"),
    ("d17_huge_char", "const I64 C = '\\u{10FFFF}' as I64;\n"),
    # Balanced-ternary literals (SPEC-04 LS-30): values, signs, and the range of each width.
    ("ternary_literal", "export I64 run() { return 0tP0N; }\n"),
    ("ternary_negative", "export I64 run() { return 0tN0P; }\n"),
    ("ternary_negated", "export I64 run() { return -0tN; }\n"),
    ("ternary_negated_comment", "export I64 run() { return - /* c */ 0tP0N; }\n"),
    ("ternary_separators", "const I64 T = 0tP_0_N + 0tN_N;\n"),
    ("ternary_zero", "const I64 Z = 0t000 + -0t0;\n"),
    ("ternary_static_assert", "static_assert(0tP0N == 8 && -0tN == 1 && 0tN0P == -8);\n"),
    ("ternary_i8_max", "const I8 A = 0tPNNN0P;\n"),
    ("ternary_i8_over", "const I8 A = 0tPNNNPN;\n"),
    ("ternary_i8_min", "const I8 A = 0tNPPPNP;\n"),
    ("ternary_i8_under", "const I8 A = 0tNPPPN0;\n"),
    ("ternary_negated_i8_over", "const I8 A = -0tNPPPN0;\n"),
    ("ternary_u8_negative", "const U8 A = 0tN;\n"),
    ("ternary_u8_negated", "const U8 A = -0tN;\n"),
    ("ternary_i64_max", "const I64 A = 0tPNPNPPP00PPP00NP0PNNPPN0P0P0N0N0PPN0NP0NP;\n"),
    ("ternary_i64_over", "const I64 A = 0tPNPNPPP00PPP00NP0PNNPPN0P0P0N0N0PPN0NP00N;\n"),
    ("ternary_i64_min", "const I64 A = 0tNPNPNNN00NNN00PN0NPPNNP0N0N0P0P0NNP0PN00P;\n"),
    ("ternary_i64_under", "const I64 A = 0tNPNPNNN00NNN00PN0NPPNNP0N0N0P0P0NNP0PN000;\n"),
    ("ternary_u64_max", "const U64 A = 0tPNNNN00N0P00N00NN0PPNNPPPNPNNPNPPNPP0NN0N0;\n"),
    ("ternary_u64_over", "const U64 A = 0tPNNNN00N0P00N00NN0PPNNPPPNPNNPNPPNPP0NN0NP;\n"),
    ("ternary_conversion", "const I8 A = 0tPPPPPP as I8;\n"),
    ("ternary_wrap_conversion", "const I8 A = 0tPPPPPP as% I8;\n"),
    ("ternary_huge_wrap", "const I64 A = 0tN" + "P" * 2000 + " as% I64;\n"),
    ("ternary_huge_conversion", "const I64 A = 0t" + "N0" * 1000 + " as I64;\n"),
    ("ternary_runtime", "export I64 run(I64 x) { return x + 0tPN; }\n"),
    ("ternary_switch", "export I64 run(I64 x) {\n    switch (x) {\n        case 0tP: return 1;\n"
     "        case 0tPN..=0tP0: return 2;\n        default: return 0;\n    }\n}\n"),
    ("shift_count", "const I64 S = 1 << 64;\n"),
    ("shift_count_neg", "const I64 S = 1 >> -1;\n"),
    ("shift_wrap_count", "const I64 S = 1 <<% 70;\n"),
    ("neg_min", "const I64 N = -I64.min;\n"),
    ("neg_min_lit", "const I64 N = -(-9223372036854775808);\n"),
    ("div_min", "const I64 N = I64.min / -1;\n"),
    ("rem_zero", "const I64 N = 5 % 0;\n"),
    ("sat_ok", "const I64 N = I64.max +| 1;\nconst I64 M = I64.min -| 1;\nconst I64 P = I64.max *| 2;\n"),
    ("wrap_ok", "const I64 N = I64.max +% 1;\nconst I64 M = -%I64.min;\nconst I64 P = ~0;\n"),
    ("mul_overflow", "const I64 N = 4611686018427387904 * 2;\n"),
    ("sub_overflow", "const I64 N = -9223372036854775807 - 2;\n"),
    ("bool_as", "const I64 T = true as I64;\nconst I64 F = false as I64;\n"),
    ("bool_as_wrap", "const I64 T = true as% I64;\n"),
    ("int_as_bool", "const Bool T = 1 as Bool;\n"),
    ("int_var_as_bool", "export Bool run(I64 x) { return x as Bool; }\n"),
    ("bool_as_bool", "export Bool run(Bool x) { return x as Bool; }\n"),
    ("void_as", "void f() { }\nexport I64 run() { return f() as I64; }\n"),
    ("round_clause", "export I64 run(I64 x) { return x as I64 round nearest; }\n"),
    ("arg_count_many", "I64 f(I64 a) { return a; }\nexport I64 run() { return f(1, 2); }\n"),
    ("arg_count_few", "I64 f(I64 a, I64 b) { return a; }\nexport I64 run() { return f(1); }\n"),
    ("arg_typearg", "I64 f(I64 a) { return a; }\nexport I64 run() { return f(I64); }\n"),
    ("arg_typearg_many", "I64 f(I64 a) { return a; }\nexport I64 run() { return f(1, I64); }\n"),
    ("arg_type", "I64 f(I64 a) { return a; }\nexport I64 run() { return f(true); }\n"),
    ("arg_type_second", "I64 f(I64 a, Bool c) { return a; }\nexport I64 run() { return f(1, 2); }\n"),
    ("arg_void", "void h() { }\nI64 f(I64 a) { return a; }\nexport I64 run() { return f(h()); }\n"),
    ("call_local", "export I64 run() { I64 x = 1; return x(); }\n"),
    ("call_undefined", "export I64 run() { return nothing(1); }\n"),
    ("call_script_local", 'I64 a = 1;\nexport I64 run() { return a(); }\n"x\\n";\n'),
    ("fn_value", "I64 f() { return 1; }\nexport I64 run() { return f; }\n"),
    ("fn_value_paren", "I64 f() { return 1; }\nexport I64 run() { return (f) + 1; }\n"),
    ("result_unused", "I64 f() { return 1; }\nexport I64 run() { f(); return 0; }\n"),
    ("discard_ok", "I64 f() { return 1; }\nexport I64 run() { _ = f(); _ = 1 + 2; return 0; }\n"),
    ("discard_void", "void f() { }\nexport I64 run() { _ = f(); return 0; }\n"),
    ("void_call_stmt", "void f() { }\nexport I64 run() { f(); return 0; }\n"),
    ("assign_const", "const I64 K = 1;\nexport I64 run() { K = 2; return 0; }\n"),
    ("assign_local_const", "export I64 run() { const I64 L = 1; L += 2; return 0; }\n"),
    ("assign_param", "export I64 run(I64 a) { a = 2; return 0; }\n"),
    ("incdec_param", "export I64 run(I64 a) { a++; return 0; }\n"),
    ("assign_loop", "export I64 run() { for k in 0..3 { k = 1; } return 0; }\n"),
    ("incdec_loop", "export I64 run() { for k in 0..3 { k--; } return 0; }\n"),
    ("assign_fn", "I64 f() { return 1; }\nexport I64 run() { f = 2; return 0; }\n"),
    ("assign_typearg", "export I64 run() { I64 = 2; return 0; }\n"),
    ("assign_bool_compound", "export I64 run(Bool c) { Bool d = c; d += true; return 0; }\n"),
    ("incdec_bool", "export I64 run() { Bool d = true; d++; return 0; }\n"),
    ("assign_shift_bool", "export I64 run() { I64 a = 1; a <<= true; return a; }\n"),
    ("assign_shift_ok", "export I64 run() { I64 a = 1; a <<= 3; a >>= 1; a <<%= 70 - 66; return a; }\n"),
    ("assign_shift_fault", "export I64 run() { I64 a = 1; a <<= 64; return a; }\n"),
    ("assign_mismatch", "export I64 run() { I64 a = 1; a = true; return a; }\n"),
    ("assign_undefined", "export I64 run() { zz = 1; return 0; }\n"),
    ("assign_paren", "export I64 run() { I64 a = 1; (a) = 2; return a; }\n"),
    ("uninit", "export I64 run() { I64 a; return 0; }\n"),
    ("uninit_bool", "export I64 run() { Bool a; return 0; }\n"),
    ("step_zero", "export I64 run() { for k in 0..3 by 0 { } return 0; }\n"),
    ("step_nonconst", "export I64 run(I64 s) { for k in 0..3 by s { } return 0; }\n"),
    ("step_bool", "export I64 run() { for k in 0..3 by true { } return 0; }\n"),
    ("step_fault", "export I64 run() { for k in 0..3 by 1 / 0 { } return 0; }\n"),
    ("step_neg", "export I64 run() { I64 s = 0; for k in 10..0 by -2 { s += k; } return s; }\n"),
    ("range_bool", "export I64 run() { for k in true..3 { } return 0; }\n"),
    ("range_bool_hi", "export I64 run() { for k in 0..false { } return 0; }\n"),
    ("range_both_bad", "export I64 run() { for k in true..zz { } return 0; }\n"),
    ("range_fault", "export I64 run() { for k in 0..(1 / 0) { } return 0; }\n"),
    ("cond_int", "export I64 run() { if (1) { } return 0; }\n"),
    ("cond_int_var", "export I64 run(I64 x) { while (x) { } return 0; }\n"),
    ("cond_for", "export I64 run() { for (I64 j = 0; j; j += 1) { } return 0; }\n"),
    ("not_int", "export Bool run(I64 x) { return !x; }\n"),
    ("and_int", "export Bool run(I64 x) { return x && true; }\n"),
    ("or_int_right", "export Bool run(Bool c) { return c || 1; }\n"),
    ("bool_arith", "export Bool run(Bool c) { return c + c; }\n"),
    ("bool_order", "export Bool run(Bool c) { return c < true; }\n"),
    ("bool_eq", "export Bool run(Bool c) { return c == true && c != false; }\n"),
    ("bool_neg", "export Bool run(Bool c) { return -c; }\n"),
    ("bool_neg_lit", "export Bool run() { return -1; }\n"),
    ("bool_shift", "export I64 run(Bool c) { return c << 1; }\n"),
    ("shift_by_bool", "export I64 run(I64 x, Bool c) { return x << c; }\n"),
    ("mismatch_add", "export I64 run(I64 x, Bool c) { return x + c; }\n"),
    ("mismatch_cmp", "export Bool run(I64 x, Bool c) { return x == c; }\n"),
    ("void_cmp", "void f() { }\nexport Bool run() { return f() == f(); }\n"),
    ("void_cmp_lit", "void f() { }\nexport Bool run() { return f() == 1; }\n"),
    ("void_add", "void f() { }\nexport I64 run() { return f() + 1; }\n"),
    ("void_add_both", "void f() { }\nexport I64 run() { return f() + f(); }\n"),
    ("void_init", "void f() { }\nexport I64 run() { I64 x = f(); return x; }\n"),
    ("void_return", "void f() { }\nexport I64 run() { return f(); }\n"),
    ("void_cond", "void f() { }\nexport I64 run(Bool c) { I64 x = c ? f() : 1; return x; }\n"),
    ("cond_mismatch", "export I64 run(Bool c, I64 x) { return c ? x : c; }\n"),
    ("cond_bool_lit", "export Bool run(Bool c) { return c ? true : 1; }\n"),
    ("cond_lit_bool", "export Bool run(Bool c) { return c ? 1 : true; }\n"),
    ("cond_untyped_bool", "export Bool run(Bool c) { return c ? 1 : 2; }\n"),
    ("cond_nested_bool", "export Bool run(Bool c) { return c ? (c ? -1 : 2) : 3; }\n"),
    ("cond_op_bool", "export Bool run(Bool c) { return c ? 1 + 2 : 3; }\n"),
    ("cond_not_bool", "export I64 run(I64 x) { return x ? 1 : 2; }\n"),
    ("return_void_value", "export void run() { return 1; }\n"),
    ("return_missing", "export I64 run() { return; }\n"),
    ("missing_return", "export I64 run(I64 x) { if (x > 0) { return 1; } }\n"),
    ("missing_return_loop", "export I64 run() { while (true) { } }\n"),
    ("missing_return_empty", "export Bool run() { }\n"),
    ("literal_range", "export I64 run() { return 9223372036854775808; }\n"),
    ("literal_range_neg_ok", "export I64 run() { return -9223372036854775808; }\n"),
    ("literal_range_neg", "export I64 run() { return -9223372036854775809; }\n"),
    ("literal_range_paren", "export I64 run() { return (((9223372036854775808))); }\n"),
    ("literal_range_unary", "export I64 run() { return -(9223372036854775808); }\n"),
    ("literal_range_hex", "export I64 run() { return 0x8000000000000000; }\n"),
    ("literal_range_char_ok", "export I64 run() { return '\\u{10FFFF}'; }\n"),
    ("literal_range_runtime", "export I64 run(I64 x) { return x + 9223372036854775808; }\n"),
    ("literal_range_huge", "export I64 run() { return " + str(1 << 4095) + "; }\n"),
    ("literal_after_fault", "export I64 run() { return 1 / 0 + 9223372036854775808; }\n"),
    ("literal_before_fault", "export I64 run() { return 9223372036854775808 + 1 / 0; }\n"),
    ("typearg_value", "export I64 run() { I64 x = I64; return x; }\n"),
    ("typearg_bool_value", "export I64 run() { Bool x = Bool; return 0; }\n"),
    ("typeprop_min", "export I64 run() { return I64.min + I64.max; }\n"),
    ("typeprop_fold", "const I64 N = I64.max + 1;\n"),
    ("undefined_in_hole", 'void main() { "{zz}\\n"; }\n'),
    ("hole_bool_expr", 'void main() { I64 x = 1; "{x > 0 && x < 5} {x as I64}\\n"; }\n'),
    ("hole_fault", 'void main() { "{1 / 0}\\n"; }\n'),
    ("hole_skip", 'void main() { Bool c = true; "{c || 1 / 0 == 0}\\n"; }\n'),
    ("print_many", 'void main() { I64 a = 1; "{a}"; "{a}{a}\\n"; "plain\\n"; "{{literal}}\\n"; }\n'),
    ("expr_stmt_literal", "export I64 run() { 1; return 0; }\n"),
    ("discard_bool", "export I64 run() { _ = true; return 0; }\n"),
    ("discard_fault", "export I64 run() { _ = 1 / 0; return 0; }\n"),
    ("many_errors", "export I64 run() { I64 a = true; Bool b = 1; return zz; }\n"),
    ("signature_before_body", "export I64 run() { return zz; }\nI64 f(Foo x) { return 0; }\n"),
    ("body_order", "export I64 run() { return zz; }\nconst I64 A = 1 / 0;\n"),
    ("item_order", "const I64 A = 1 / 0;\nexport I64 run() { return zz; }\n"),
    ("decl_after_init", "export I64 run() { I64 a = a; return 0; }\n"),
    ("decl_dup_after_init_error", "export I64 run() { I64 a = 1; I64 a = zz; return 0; }\n"),
    ("decl_dup_after_fold_error", "export I64 run() { I64 a = 1; I64 a = 1 / 0; return 0; }\n"),
    ("nest_blocks_256",
     "export I64 run() {\n    I64 a = 1;\n    " + "{" * 255 + " a = 2; " + "}" * 255 + "\n    return a;\n}\n"),
    ("nest_parens_200", "export I64 run() { return " + "(" * 200 + "1" + ")" * 200 + "; }\n"),
    ("nest_unary_200", "export I64 run() { return " + "-" * 199 + "(1); }\n"),
    ("nest_bool_200", "export Bool run() { return " + "!" * 200 + "true; }\n"),
    ("long_sum", "export I64 run() { return " + " + ".join(["1"] * 3000) + "; }\n"),
    ("long_sum_overflow", "const I64 S = " + " + ".join(["4611686018427387904"] * 3) + ";\n"),
    ("long_and", "const Bool B = " + " && ".join(["true"] * 2000) + " && false;\n"),
    ("const_self_call", "const I64 A = A(1);\n"),
    ("const_in_script_body", 'I64 a = 1;\n"{K}\\n";\nconst I64 K = 2;\n'),
    ("const_cycle_via_fn", "export I64 run() { return A; }\nconst I64 A = B + 1;\nconst I64 B = A * 2;\n"),
    ("for_c_assign_init", "export I64 run(I64 n) { I64 j = 0; for (j = 1; j < n; j += 1) { } return j; }\n"),
    ("for_c_bad_update", "export I64 run(I64 n) { for (I64 j = 0; j < n; j = true) { } return 0; }\n"),
    ("for_c_bad_init", "export I64 run(I64 n) { for (I64 j = true; j < n; j += 1) { } return 0; }\n"),
    ("export_modvar", "export I64 x = 1;\nexport const I64 Y = 2;\nexport I64 run() { return x + Y; }\n"),
    ("script_note_assign", 'I64 a = 1;\na = 2;\nI64 f() { return a; }\n'),
    ("script_note_block", 'I64 a = 1;\n{ }\nI64 f() { return a; }\n'),
    ("script_note_while", 'I64 a = 1;\nwhile (false) { }\nI64 f() { return a; }\n'),
    ("script_note_for", 'I64 a = 1;\nfor k in 0..1 { }\nI64 f() { return a; }\n'),
    ("script_note_incdec", 'I64 a = 1;\na++;\nI64 f() { return a; }\n'),
    ("script_note_discard", 'I64 a = 1;\n_ = 5;\nI64 f() { return a; }\n'),
    ("script_note_return", 'I64 a = 1;\nreturn;\nI64 f() { return a; }\n'),
    ("script_note_print_tab", 'I64 a = 1;\n\t\t"x\\n";\nI64 f() { return a; }\n'),
    ("script_note_main_assign", 'I64 a = 1;\n  a += 3;\nvoid main() { }\n'),
    ("script_note_unicode", '// éé\nI64 a = 1; "é{a}\\n";\nI64 f() { return a; }\n'),
    ("modvar_note_after_fn", 'I64 f() { return 1; }\nI64 x = f();\nconst I64 K = 1;\n'),
    ("script_uninit", 'I64 a;\n"x\\n";\n'),
    ("script_const_ok", 'const I64 K = 2;\n"{K}\\n";\n'),
    ("script_for_scope", 'for k in 0..2 { "{k}\\n"; }\nfor k in 0..2 { }\n'),
    ("script_labels", 'a: while (true) { break a; }\nb: for k in 0..1 { continue b; }\n'),
    ("main_with_params", "void main(I64 a) { }\n"),
    ("void_return_in_main", "void main() { return; }\n"),
    ("local_main", "export I64 run() { I64 main = 1; return main; }\n"),
    ("call_main", "void main() { }\nexport I64 run() { main(); return 0; }\n"),
    ("unicode_positions", "export I64 run() {\n    I64 été = 1;\n    return 0;\n}\n"),
    ("tab_positions", "export I64 run() {\n\tBool b = 1;\n\treturn 0;\n}\n"),
    ("crlf_positions", "export I64 run() {\r\n    Bool b = 1;\r\n    return 0;\r\n}\r\n"),
    ("long_else_if",
     "export I64 run(I64 x) {\n" + "".join("    %sif (x == %d) { return %d; }\n" % ("} else " if k else "", k, k)
                                           for k in range(1)) + "    return -1;\n}\n"),
]


def else_if_chain(arms: int, bad_arm: int = -1) -> bytes:
    lines = ["export I64 run(I64 x) {", "    I64 y = 0;"]
    for k in range(arms):
        head = "    if" if k == 0 else "    } else if"
        cond = "x" if k == bad_arm else "x == %d" % k
        lines.append("%s (%s) {" % (head, cond))
        lines.append("        y = %d;" % k)
    lines += ["    }", "    return y;", "}", ""]
    return "\n".join(lines).encode("ascii")


# Statement bodies for in_function and in_script.
BODIES = [
    "I64 x = 1;", "I64 x = -1;", "I64 x = K;", "I64 x = g();", "Bool x = KB;", "Bool x = p(1, true);",
    "Bool x = p(true, 1);", "I64 x = p(1, true);", "i = i + 1;", "b = !b;", "i += K;", "i <<= K;",
    "b = i;", "i = b;", "v();", "g();", "_ = g();", "_ = v();", "p(1, b);", "_ = p(1, b);",
    "if (b) { i = 1; } else if (!b) { i = 2; } else { i = 3; }", "if (i) { }", "while (b && i > 0) { i -= 1; }",
    "for k in 0..i { i += k; }", "for k in 0..b { }", "for (I64 j = 0; j < i; j += 1) { }",
    "\"{i} {b} {g()} {K}\\n\";", "\"{v()}\\n\";", "\"{i + b}\\n\";", "static_assert(K == 5, \"k\");",
    "static_assert(K == 6);", "static_assert(i == 1);", "const I64 L = K + 1;", "const I64 L = i;",
    "const Bool L = KB && false;", "I64 x = 1 / 0;", "I64 x = i / 0;", "I64 x = K / (K - 5);",
    "Bool x = b && (1 / 0 == 0);", "Bool x = false && (1 / 0 == 0);", "I64 x = b ? 1 / 0 : 0;",
    "I64 x = true ? 0 : 1 / 0;", "I64 x = false ? 0 : 1 / 0;", "I64 x = 9223372036854775807 + 1;",
    "I64 x = i + 9223372036854775807 + 1;", "I64 x = K + 9223372036854775807;",
    "I64 x = (K + 9223372036854775807) as% I64;", "I64 x = 9223372036854775808 as% I64;",
    "I64 x = b ? 9223372036854775808 as I64 : 0;", "I64 x = 1 << 63;", "I64 x = 1 << 64;", "I64 x = i << 64;",
    "I64 x = (2 << 62) * 2;", "I64 x = -I64.min;", "I64 x = ~I64.max;", "I64 x = I64.min % -1;",
    "Bool x = I64.max > I64.min;", "Bool x = true != false;", "Bool x = !true == !false;",
    "zz = 1;", "I64 zz = yy;", "I64 x = x;", "I64 i = 1;", "I64 g = 1;", "I64 count = 1;", "Bool sum = true;",
    "break;", "continue;", "return 1;", "return;", "return true;",
]

# Operand forms (I64-, Bool-, void-typed and untyped) and operator templates for the grid.
OPERANDS = ["1", "-2", "(3)", "'a'", "i", "b", "true", "g()", "v()", "K", "KB", "I64.max", "(1 << 2)",
            "(b ? 1 : 2)", "(b ? i : 2)", "(b ? true : false)", "-(4)", "~5", "(1 + 2)", "(i as I64)",
            "(b as I64)", "(1 as I64)", "9223372036854775808", "(1 / 0)", "zz", "I64", "p(i, b)"]
UNARY = ["-{a}", "~{a}", "!{a}", "-%{a}", "({a})", "{a} as I64", "{a} as Bool", "{a} as% I64"]
BINARY = ["{a} + {c}", "{a} << {c}", "{a} == {c}", "{a} < {c}", "{a} && {c}", "{a} || {c}", "{a} & {c}",
          "b ? {a} : {c}", "{a} ? 1 : {c}", "{a} *| {c}", "{a} != {c}", "{a} >> {c}"]
CONTEXTS = [
    ("I64 x = {e};", None), ("Bool x = {e};", None), ("if ({e}) {{ }}", None), ("\"{{{e}}}\\n\";", None),
    ("_ = {e};", None), ("return {e};", "I64"), ("return {e};", "Bool"), ("{e};", None), ("i = {e};", None),
    ("const I64 L = {e};", None), ("for k in 0..{e} {{ }}", None), ("static_assert({e});", None),
]


def grid() -> list:
    out = []
    contexts = CONTEXTS
    for ui, u in enumerate(UNARY):
        for ai, a in enumerate(OPERANDS):
            e = u.format(a=a)
            for ci, (ctx, result) in enumerate(contexts[:3] + contexts[5:7]):
                body = "    " + ctx.format(e=e)
                out.append(("grid/u%d_%d_%d" % (ui, ai, ci), in_function(body, result or "I64")))
    for bi, t in enumerate(BINARY):
        for ai, a in enumerate(OPERANDS):
            for ci, c in enumerate(OPERANDS):
                e = t.format(a=a, c=c)
                ctx, result = contexts[(ai * 7 + ci * 3 + bi) % len(contexts)]
                body = "    " + ctx.format(e=e)
                out.append(("grid/b%d_%d_%d" % (bi, ai, ci), in_function(body, result or "I64")))
    for k, body in enumerate(BODIES):
        out.append(("body/fn_%d" % k, in_function("    " + body)))
        out.append(("body/script_%d" % k, in_script(body)))
        out.append(("body/void_%d" % k, in_function("    " + body, "void", "")))
    return out


# Constant expressions that fault or narrow, and the places a constant expression is met.
CONST_EXPRS = [
    "9223372036854775807 + 1", "-9223372036854775807 - 2", "4611686018427387904 * 2", "1 / 0", "1 % 0",
    "I64.min / -1", "I64.min % -1", "-I64.min", "1 << 64", "1 << -1", "1 >> 64", "1 <<% 64", "1 <<% 63",
    "9223372036854775808 as I64", "-9223372036854775809 as I64", "18446744073709551616 as% I64",
    "(9223372036854775808) as I64", "-(9223372036854775808) as I64", "9223372036854775808",
    "-9223372036854775809", "(0 - 1) * I64.min", "I64.max +| 1", "I64.min *| 2", "I64.max *% 3",
    "'\\u{10FFFF}' * 4398046511104", "0x7fff_ffff_ffff_ffff + 0b1", "0o777777777777777777777 * 2",
    "-0x8000_0000_0000_0000 - 1", "(I64.max - 1) + 2", "true as I64 + I64.max", "-(-I64.max - 1)",
    "~I64.min - 1", "(1 << 62) + (1 << 62)",
]
CONST_PLACES = [
    "const I64 X = {e};", "export I64 run() {{ return {e}; }}", "export I64 run() {{ I64 x = {e}; return x; }}",
    "export Bool run(Bool c) {{ return c && ({e} == 0); }}", "const Bool B = false && ({e} == 0);",
    "const Bool B = true || ({e} == 0);", "const I64 Y = true ? 0 : {e};", "const I64 Y = false ? 0 : {e};",
    "export I64 run(Bool c) {{ return c ? {e} : 0; }}", "static_assert({e} != 7);",
    "I64 m = {e};", "\"{{{e}}}\\n\";", "export I64 run() {{ for k in 0..{e} {{ }} return 0; }}",
    "export I64 run() {{ _ = {e}; return 0; }}", "export I64 run() {{ const I64 L = {e}; return L; }}",
    # An assert message is never evaluated: its holes fold as skipped operands (G-C2 review COR-7).
    "export I64 run(Bool c) {{ assert(c, \"{{{e}}}\"); return 0; }}",
]


def consts() -> list:
    out = []
    for ei, e in enumerate(CONST_EXPRS):
        for pi, place in enumerate(CONST_PLACES):
            out.append(("const/%d_%d" % (ei, pi), (place.format(e=e) + "\n").encode("utf-8")))
    return out


def switches() -> list:
    """Switch diagnostics, constant bounds, control flow and domain boundaries."""
    out = []

    def add(name, body, params="I64 x", head="", tail=""):
        text = head + "export I64 run(%s) { %s %s }\n" % (params, body, tail)
        out.append(("switch/" + name, text.encode("ascii")))

    cases = [
        ("incomplete", "case 0: return 1;"),
        ("overlap", "case -2..=2: return 1; case 2..=3: return 2; default: return 3;"),
        ("duplicate", "case 4: return 1; case 4: return 2; default: return 3;"),
        ("reversed", "case 2..=-2: return 1; default: return 3;"),
        ("half_open", "case 0..2: return 1; default: return 3;"),
        ("empty", "case 0: default: return 3;"),
        ("default_order", "default: return 3; case 0: return 1;"),
        ("default_duplicate", "default: return 3; default: return 1;"),
        ("type_mismatch", "case true: return 1; default: return 3;"),
        ("fault", "case 1 / 0: return 1; default: return 3;"),
        ("nonconstant", "case x: return 1; default: return 3;"),
        ("overlap_first", "case 1: return 1; case 1: return 2; case true: return 3; default: return 4;"),
        ("coverage_first", "case 1: return zz;"),
        ("labels_first", "case 1: return zz; case true: return 2; default: return 3;"),
        ("fallthrough_next", "case 0: fallthrough; case 1: return 2; default: return 3;"),
        ("fallthrough_middle", "case 0: fallthrough; return 1; default: return 3;"),
        ("fallthrough_last", "case 0: return 1; default: fallthrough;"),
        ("fallthrough_nested", "case 0: { fallthrough; } return 1; default: return 3;"),
        ("case_scope", "case 0: I64 a = 1; return a; default: I64 a = 2; return a;"),
        ("case_scope_leak", "case 0: I64 a = 1; return a; default: return a;"),
        ("continue_no_loop", "case 0: continue; default: return 3;"),
        ("break_missing_return", "case 0: break; default: return 3;"),
        ("break_nested_missing_return", "case 0: { if (x == 0) { break; } } return 1; default: return 3;"),
        ("loop_break_completes", "case 0: while (true) { break; } return 1; default: return 3;"),
        ("inner_switch_break", "case 0: switch (x) { default: break; } return 1; default: return 3;"),
        # A nested fallthrough runs as a no-op and completes, so C4001; a direct final one
        # continues into the next clause (CINTC-OQ-47, G-C2 review COR-3).
        ("nested_fallthrough_completion", "case 0: { fallthrough; } default: return 3;"),
        ("nested_fallthrough_if_completion",
         "case 0: if (x == 0) { fallthrough; } else { fallthrough; } default: return 3;"),
        ("nested_then_direct_fallthrough", "case 0: { fallthrough; } fallthrough; default: return 3;"),
    ]
    for name, clauses in cases:
        add(name, "switch (x) { " + clauses + " }")
    add("void_scrutinee", "switch (v()) { default: return 1; }", head="void v() { }\n")
    add("fallthrough_outside", "fallthrough; return 0;")
    add("const_names", "const I64 L = K + 2; switch (x) { case K, L..=L + 1: return 1; default: return 2; }",
        head="const I64 K = 1 + 2;\n")
    add("loop_continue", "while (true) { switch (x) { case 0: continue; default: break; } break; } return 0;")
    add("loop_label", "outer: while (true) { switch (x) { case 0: break outer; default: continue outer; } } return 0;")
    add("switch_label_invalid", "switch (x) { default: break nowhere; } return 0;")
    add("bool_complete", "switch (x) { case true: return 1; case false: return 0; }", "Bool x")
    add("bool_incomplete", "switch (x) { case true: return 1; }", "Bool x")
    add("bool_range", "switch (x) { case zz..=false: return 1; default: return 0; }", "Bool x")
    add("bool_literal", "switch (x) { case 1: return 1; default: return 0; }", "Bool x")
    for ty in ("I8", "I16", "I32", "I64", "U8", "U16", "U32", "U64"):
        add(ty + "_complete", "switch (x) { case %s.min..=%s.max: return 1; }" % (ty, ty), ty + " x")
        add(ty + "_extrema", "switch (x) { case %s.min, %s.max: return 1; default: return 2; }" % (ty, ty), ty + " x")
        add(ty + "_hole", "switch (x) { case %s.min..=%s.max - 1: return 1; }" % (ty, ty), ty + " x")
        if ty.startswith("I"):
            add(ty + "_zero", "switch (x) { case %s.min..=-1: return 1; case 0..=%s.max: return 2; }" % (ty, ty), ty + " x")
            add(ty + "_cross_zero", "switch (x) { case -5..=5: return 1; default: return 2; }", ty + " x")
        else:
            add(ty + "_split", "switch (x) { case 0..=1: return 1; case 2..=%s.max: return 2; }" % ty, ty + " x")
    add("U64_high", "switch (x) { case 9223372036854775808..=U64.max: return 1; default: return 2; }", "U64 x")
    add("U64_range_error", "switch (x) { case 18446744073709551616: return 1; default: return 2; }", "U64 x")
    add("I8_range_error", "switch (x) { case 128: return 1; default: return 2; }", "I8 x")
    for name, values in (("left_right", (3, 1, 2)), ("right_left", (1, 3, 2))):
        clauses = " ".join("case %d: return %d;" % (k, k) for k in values)
        add(name, "switch (x) { " + clauses + " default: return 0; }")
    values = list(range(256))
    random.Random(213).shuffle(values)
    clauses = " ".join("case %d: return %d;" % (k, k) for k in values)
    add("shuffled_u8_complete", "switch (x) { " + clauses + " }", "U8 x")
    add("literal_scrutinee", "switch (1 + 2) { case 3: return 1; default: return 0; }")
    for arms in (1, 1000):
        for reverse in (False, True):
            values = list(range(arms))[::(-1 if reverse else 1)]
            clauses = " ".join("case %d: return %d;" % (k, k) for k in values)
            add("arms_%d_%s" % (arms, "reverse" if reverse else "ordered"),
                "switch (x) { " + clauses + " default: return -1; }")
    clauses = " ".join("case %d: return %d;" % (k, k) for k in range(1000))
    add("arms_1000_overlap", "switch (x) { " + clauses + " case 999: return 1; default: return -1; }")
    return out



# Task 2.13 (a): the eight integer widths of cint-boot-1 in every position. Each body is the
# body of `export U run(T t, U u)`, with `U f(U a)` and `Bool h(T a)` declared first; {T} and
# {U} are the two widths, {B} the bit width of T, {S} its last shift amount.
WIDTHS = ("I8", "I16", "I32", "I64", "U8", "U16", "U32", "U64")
PAIR_BODIES = [
    "return t;", "u = t; return u;", "{U} x = t; return x;", "return t as {U};", "return t as% {U};",
    "return {T}.max as {U};", "return {T}.min as {U};", "return {T}.max as% {U};", "return {T}.min as% {U};",
    "return (t & u) as {U};", "return (t + u) as {U};", "return (t << u) as {U};", "return (u >> t) as {U};",
    "if (t == u) {{ return u; }} return u;", "if (t < u || u > 0) {{ return u; }} return u;",
    "t = {U}.max; return u;", "t = u; return u;", "t += u; return u;", "t <<= u; return u;",
    "switch (t) {{ case {U}.max: return u; default: return u; }}",
    "switch (t) {{ case {U}.min..={U}.max: return u; default: return u; }}",
    "switch (t) {{ case 0 as {U}: return u; default: return u; }}",
    "for k in 0..u {{ t = k; }} return u;", "for k in t..u {{ }} return u;", "return f(t);",
    "return f(t as {U});", "const {T} C = {T}.max; return C as {U};", "const {U} C = {T}.max; return C;",
    "const {U} C = {T}.max as {U}; return C;", "const {U} C = {T}.min as% {U}; return C;",
    "return f({T}.max);", "_ = f(u); return t as% {U};", "return (t == {U}.max) as {U};",
    "return ({T}.max as {U}) +% u;",
]
WIDTH_BODIES = [
    "t = {T}.max + 1; return u;", "t = {T}.min - 1; return u;", "t = 128; return u;", "t = 256; return u;",
    "t = -1; return u;", "t = 65536; return u;", "t = 4294967296; return u;", "t = -129; return u;",
    "t = -2147483649; return u;", "t = 18446744073709551616; return u;", "t = 9223372036854775808; return u;",
    "t += 1; return u;", "t -= 1; return u;", "t *= 2; return u;", "t /= 0; return u;", "t %= 0; return u;",
    "t +%= {T}.max; return u;", "t -%= 1; return u;", "t *%= 3; return u;", "t <<%= {B}; return u;",
    "t <<= {B}; return u;", "t <<= {S}; return u;", "t >>= {B}; return u;", "t &= 255; return u;",
    "t |= 256; return u;", "t ^= -1; return u;", "t++; t--; return u;",
    "return (t + 1) as {U};", "return (t +% {T}.max) as {U};", "return (t *% 3 -% 1) as {U};",
    "return (-t) as {U};", "return (~t) as {U};", "return (-%t) as {U};", "return (t << 1) as {U};",
    "return (t << {B}) as {U};", "return (t << {S}) as {U};", "return (t <<% {B}) as {U};",
    "return (t >> {S}) as {U};", "return (t >> {B}) as {U};", "return (t / 0) as {U};", "return (t % 0) as {U};",
    "return (t / -1) as {U};", "return ({T}.min / -1) as {U};", "return ({T}.min % -1) as {U};",
    "return (-{T}.min) as {U};", "return (-{T}.max) as {U};", "return ({T}.max * 2) as {U};",
    "return ({T}.max *% 2) as {U};", "return ({T}.max <<% 1) as {U};", "return ({T}.max << 1) as {U};",
    "return (1 << {S}) as {U};", "return (1 << {B}) as {U};", "return ({T}.max + 1 - 1) as {U};",
    "return (300 as {T}) as {U};", "return (300 as% {T}) as {U};", "return (-1 as {T}) as {U};",
    "return (-1 as% {T}) as {U};", "return ('a' as {T}) as {U};", "return (true as {T}) as {U};",
    "return (t as Bool) as {U};", "return (h(t) as {T}) as {U};", "Bool z = t; return u;",
    "Bool z = h(t) && t > 0 || !h(t); return u;", "Bool z = false && {T}.max + 1 > 0; return u;",
    "Bool z = true || ({T}.min - 1 > 0); return u;", "Bool z = true && ({T}.min - 1 > 0); return u;",
    "Bool z = h(t) && ({T}.max + 1 > 0); return u;", "Bool z = t > 0 && t; return u;",
    "const {T} C = {T}.max + 1; return u;", "const {T} C = {T}.min - 1; return u;",
    "const {T} C = {T}.max *% 2; return C as {U};", "const {T} C = {T}.max <<% 1; return C as {U};",
    "const {T} C = 300 as% {T}; return C as {U};", "const {T} C = 300 as {T}; return u;",
    "const {T} C = -1 as {T}; return u;", "const {T} C = {T}.min / -1; return u;",
    "const {T} C = {T}.min % -1; return u;", "const {T} C = 1 << {B}; return u;",
    "const {T} C = 1 << {S}; return C as {U};", "const {T} C = {T}.max >> {B}; return u;",
    "const {T} C = -{T}.min; return u;", "const {T} C = ~{T}.min; return C as {U};",
    "const {T} C = {T}.min -% 1; return C as {U};", "const I64 C = {T}.max as I64 + 1; return u;",
    "\"{{t}} {{u}} {{t as I64}} {{{T}.max}} {{t=}}\n\"; return u;", "\"{{{T}.max + 1}}\n\"; return u;",
    "switch (t) {{ case {T}.min..={T}.max: return u; }}", "switch (t) {{ case {T}.min..={T}.max - 1: return u; }}",
    "switch (t) {{ case 128: return u; default: return u; }}", "switch (t) {{ case -1: return u; default: return u; }}",
    "switch (t) {{ case 0..=127, 100..=200: return u; default: return u; }}",
    "switch (t) {{ case {T}.max..={T}.min: return u; default: return u; }}",
    "switch (t) {{ case {T}.max + 1: return u; default: return u; }}",
    "switch (t) {{ case 1 as {T}: return u; case 1: return u; default: return u; }}",
    "for k in {T}.min..={T}.max {{ t = k; }} return u;", "for k in t..={T}.max + 1 {{ }} return u;",
    "for k in 0..300 {{ t = k; }} return u;", "for (I64 j = t; j < 10; j += 1) {{ }} return u;",
    "while (t) {{ }} return u;", "if (t as Bool) {{ }} return u;", "return h(t + 1) as {U};",
]


def widths() -> list:
    out = []

    def module(name, T, U, body):
        bits = int(T[1:])
        b = body.format(T=T, U=U, B=bits, S=bits - 1)
        text = ("%s f(%s a) { return a; }\nBool h(%s a) { return a > 0; }\n"
                "export %s run(%s t, %s u) {\n    %s\n}\n") % (U, U, T, U, T, U, b)
        out.append((name, text.encode("ascii")))

    for ti, T in enumerate(WIDTHS):
        for ui, U in enumerate(WIDTHS):
            for k, body in enumerate(PAIR_BODIES):
                module("width/pair_%s_%s_%d" % (T, U, k), T, U, body)
        for k, body in enumerate(WIDTH_BODIES):
            module("width/one_%s_%d" % (T, k), T, "I64" if ti != 3 else "U8", body)
    return out



def beyond_ls311(name: str) -> bool:
    """Whether nesting() case `name` nests deeper than the SPEC-04 LS-311 minimum of 256
    levels. Beyond it, cintc may refuse at its own limit, the explicit parse stack of
    SPEC-09 CINTC-02 (CINTC-OQ-05), with C9004 at the construct that needs the frame; it
    accepts or refuses, never miscompiles, so either cint_ref's outcome or that C9004 passes."""
    parts = name.split("_")
    if name.startswith("nest/blocks_"):
        return int(parts[1]) + int(parts[3]) > 256
    return name.split("/")[0] == "nest" and parts[0] in ("nest/paren", "nest/tilde", "nest/neg", "nest/call",
                                                           "nest/rassoc") and int(parts[1]) > 256


def nesting() -> list:
    """Task 2.13 (a): expression and block nesting about cint_ref's limits (C9004 at 256
    blocks, SPEC-09 CINTC-02, and 4,096 levels of blocks and expressions together, SPEC-04
    LS-311), and tables the checker sizes by tokens and nodes (C9001, CINTC-02)."""
    def fn(body, params="", result="I64"):
        return ("export %s run(%s) {\n%s\n}\n" % (result, params, body)).encode("ascii")
    out = []
    for n in (256, 257, 1000, 2047, 2048, 4095, 4096, 4097):
        out.append(("nest/paren_%d" % n, fn("    return " + "(" * n + "1" + ")" * n + ";")))
        out.append(("nest/tilde_%d" % n, fn("    return " + "~ " * n + "1;")))
        out.append(("nest/neg_%d" % n, fn("    return " + "-(" * n + "1" + ")" * n + ";")))
        out.append(("nest/call_%d" % n, b"I64 g(I64 a) { return a; }\n" +
                    fn("    return " + "g(" * n + "1" + ")" * n + ";")))
        out.append(("nest/rassoc_%d" % n, fn("    return " + "1 + (" * n + "1" + ")" * n + ";")))
    for n in (100, 256):
        for k in sorted({3840, 4096 - n, 4097 - n}):
            body = "{" * n + "    return " + "(" * k + "1" + ")" * k + ";" + "}" * n + "    return 0;"
            out.append(("nest/blocks_%d_paren_%d" % (n, k), fn(body)))
    # LS-311: 256 levels of blocks and expressions together, in each shape.
    shapes = {"paren": ("(", ")"), "rassoc": ("1 + (", ")"), "neg": ("-(", ")"), "call": ("g(", ")"),
              "and": ("b && (", ")"), "conv": ("(", " as I64)")}
    for n in (1, 128, 255):
        for shape, (left, right) in sorted(shapes.items()):
            k = 256 - n
            ret = left * k + ("b" if shape == "and" else "1") + right * k
            result, tail = ("Bool", "false") if shape == "and" else ("I64", "0")
            body = "{" * (n - 1) + "    return " + ret + ";" + "}" * (n - 1) + "    return " + tail + ";"
            out.append(("nest/ls311_%d_%s" % (n, shape), b"I64 g(I64 a) { return a; }\n" + fn(body, "Bool b", result)))
    for kind, head in (("if", "if (x > 0) {"), ("while", "while (x > 0) {"), ("for", "for k%d in 0..x {"),
                       ("forc", "for (I64 j%d = 0; j%d < x; j%d += 1) {"), ("switch", "switch (x) { default:")):
        heads = "".join(head.replace("%d", str(k)) for k in range(255))
        out.append(("nest/stmt_255_%s" % kind, fn("    " + heads + " return (1); " + "}" * 255 + " return 0;", "I64 x")))
    clauses = " ".join("case %d: return %d;" % (k, k & 1) for k in range(4096))
    out.append(("table/switch_4096", fn("    switch (x) { " + clauses + " default: return 2; }", "U16 x")))
    out.append(("nest/chain_20000", fn("    return " + " + ".join(["1"] * 20000) + ";")))
    out.append(("nest/chain_and_20000", fn("    return " + " && ".join(["b"] * 20000) + ";", "Bool b", "Bool")))
    out.append(("table/params_2000", fn("    return a1999;", ", ".join("I64 a%d" % k for k in range(2000)))))
    out.append(("table/consts_5000", fn("".join("    const I64 c%d = %d;\n" % (k, k) for k in range(5000)) +
                                        "    return c4999;")))
    out.append(("table/locals_5000", fn("".join("    I64 c%d = %d;\n" % (k, k) for k in range(5000)) +
                                        "    return c4999;")))
    out.append(("table/labels_3000", fn("".join("    l%d: while (false) { break l%d; }\n" % (k, k)
                                                for k in range(3000)) + "    return 0;")))
    out.append(("table/funcs_2000", "".join("void f%d() { }\n" % k for k in range(2000)).encode("ascii") +
                fn("    f1999();\n    return 0;")))
    out.append(("table/for_vars_3000", fn("".join("    for k%d in 0..1 { }\n" % k for k in range(3000)) +
                                          "    return 0;")))
    out.append(("table/undefined_3000", fn("    return " + " + ".join("z%d" % k for k in range(3000)) + ";")))
    out.append(("table/script_5000", "".join("I64 v%d = %d;\n" % (k, k) for k in range(5000)).encode("ascii")))
    out.append(("nest/whiles_256_label", fn("".join("    l%d: while (true) {\n" % k for k in range(256)) +
                                            "    break l0;\n" + "    }\n" * 256 + "    return 0;")))
    out.append(("nest/switch_200", fn("".join("    switch (x) { default:\n" for k in range(200)) + "    return 1;\n" +
                                      "    }\n" * 200 + "    return 0;", "I64 x")))
    for n in (256, 257):
        out.append(("nest/if_%d" % n, fn("".join("    if (x > %d) {\n" % k for k in range(n)) + "    return 1;\n" +
                                         "    }\n" * n + "    return 0;", "I64 x")))
    return out


# Modules that declare a prelude name, which is C3001 at the declaration (SPEC-04 LS-112),
# and use it, or use a prelude name that only another scope declares (G-C2 review COR-4).
PRELUDE_NAMES = [
    ("local_unused", 'void main() {\n    I64 clamp = 3;\n    "ok\\n";\n}\n'),
    ("local_hole", 'void main() {\n    I64 clamp = 3;\n    "{clamp}\\n";\n}\n'),
    ("local_expr", 'void main() {\n    I64 max = 3;\n    I64 y = max + 1;\n    "{y}\\n";\n}\n'),
    ("local_assign", "void main() {\n    I64 rotl = 3;\n    rotl = 4;\n}\n"),
    ("local_uses", 'void main() {\n    I64 max = 3;\n    I64 y = max + max;\n    "{y} {max}\\n";\n}\n'),
    ("local_and_other", 'void main() {\n    I64 max = 3;\n    I64 y = max + min;\n    "{y}\\n";\n}\n'),
    ("local_mode_name",
     'void main() {\n    I64 half_even = 3;\n    I64 y = div_round(7, 2, half_even);\n    "{y} {half_even}\\n";\n}\n'),
    ("local_after_call", 'void main() {\n    I64 a = max(1, 2);\n    I64 max = 3;\n    "{a} {max}\\n";\n}\n'),
    ("local_after_error", "void main() {\n    Bool b = 1;\n    I64 max = 3;\n    I64 y = max + 1;\n}\n"),
    ("param", 'I64 f(I64 min) {\n    return min;\n}\nvoid main() {\n    "{f(2)}\\n";\n}\n'),
    ("param_unused", 'I64 f(I64 abs) {\n    return 1;\n}\nvoid main() {\n    "{f(2)}\\n";\n}\n'),
    ("size_param", 'I64 f[len](in I64[len] v) {\n    return len;\n}\nvoid main() {\n    I64[3] a;\n'
                   '    "{f(a)}\\n";\n}\n'),
    ("const", 'const I64 count = 3;\nvoid main() {\n    "{count}\\n";\n}\n'),
    ("const_unused", 'const I64 max = 3;\nvoid main() {\n    "ok\\n";\n}\n'),
    ("global", 'I64 sum = 3;\nvoid main() {\n    "{sum}\\n";\n}\n'),
    ("global_later", 'void main() {\n    "{sum}\\n";\n}\nI64 sum = 3;\n'),
    ("loop_var", 'void main() {\n    I64 t = 0;\n    for len in 0..3 {\n        t += len;\n    }\n    "{t}\\n";\n}\n'),
    ("for_init", 'void main() {\n    I64 t = 0;\n    for (I64 count = 0; count < 3; count += 1) {\n'
                 '        t += count;\n    }\n    "{t}\\n";\n}\n'),
    ("script_local", 'I64 abs = 4;\n"{abs}\\n";\n'),
    ("script_local_in_function", 'I64 abs = 4;\nI64 f() {\n    return abs;\n}\n"{f()}\\n";\n'),
    ("function", 'I64 min(I64 a, I64 b) {\n    return a;\n}\nvoid main() {\n    "{min(1, 2)}\\n";\n}\n'),
    ("function_one", 'I64 count(I64 a) {\n    return a;\n}\nvoid main() {\n    "{count(1)}\\n";\n}\n'),
    ("function_switch", "I64 sign(I8 x) {\n    switch (x) {\n        case -128..=-1:\n            return -1;\n"
                        "        case 0:\n            return 0;\n        case 1..=127:\n            return 1;\n"
                        '    }\n}\nvoid main() {\n    "{sign(-5)} {sign(0)} {sign(9)}\\n";\n}\n'),
    ("struct", 'struct count {\n    I64 a;\n}\nvoid main() {\n    count c = count(1);\n    "{c.a}\\n";\n}\n'),
    ("other_function", 'I64 f() {\n    I64 y = max + 1;\n    return y;\n}\nI64 g() {\n    I64 max = 2;\n'
                       '    return max;\n}\nvoid main() {\n    "{f()} {g()}\\n";\n}\n'),
    ("other_param", 'I64 f(I64 a) {\n    return min;\n}\nI64 g(I64 min) {\n    return min;\n}\nvoid main() {\n'
                    '    "{f(1)} {g(2)}\\n";\n}\n'),
    ("field", 'struct S {\n    I64 max;\n}\nvoid main() {\n    S s = S(1);\n    I64 y = max;\n    "{s.max} {y}\\n";\n}\n'),
    ("undeclared", 'void main() {\n    I64 y = count + 1;\n    "{y}\\n";\n}\n'),
]


def prelude_names() -> list:
    return [("prelude/" + name, text.encode("ascii")) for name, text in PRELUDE_NAMES]


def written() -> list:
    out = [("module/" + name, text.encode("utf-8")) for name, text in MODULES]
    for arms in (1, 2, 100, 1000):
        out.append(("chain/else_if_%d" % arms, else_if_chain(arms)))
    out.append(("chain/else_if_bad_999", else_if_chain(1000, 999)))
    out += switches()
    from contextual_names_cases import CASES
    out += [("contextual/" + name, files["main.ci"].encode("utf-8"))
            for name, files in CASES.items() if len(files) == 1]
    return out


# Mutation vocabulary by token class: each replacement keeps the module likely to parse, so
# that most mutants reach the checker.
IDENTS = ["i", "b", "x", "zz", "g", "v", "p", "K", "KB", "count", "main", "I64", "Bool", "Foo", "run", "a"]
LITERALS = ["0", "1", "-1", "2", "63", "64", "'a'", "9223372036854775807", "9223372036854775808",
            "-9223372036854775808", "0x8000000000000000", "true", "false", "I64.max", "I64.min", "(1 / 0)",
            "(1 << 2)", "g()", "v()", "i", "b"]
BINOPS = ["+", "-", "*", "/", "%", "<<", ">>", "<<%", "+%", "-%", "*%", "+|", "&", "|", "^", "==", "!=", "<",
          "<=", ">", ">=", "&&", "||"]
ASSIGNOPS = ["=", "+=", "-=", "<<=", ">>=", "&=", "+|=", "<<%="]
KEYWORDS = {"if": ["while"], "while": ["if"], "break": ["continue", "return"], "continue": ["break"],
            "return": ["break", "_ ="], "const": [""], "void": ["I64", "Bool"], "true": ["false", "1"],
            "false": ["true", "0"], "as": ["as%"]}


def mutate(text: str, rng) -> str:
    import front_cases  # noqa: F401  (puts ref/ on the path)
    from cint_ref import lexer
    from cint_ref.faults import CompileError
    try:
        toks = [t for t in lexer.tokenize(text, "m.ci") if t.kind != "eof"]
    except CompileError:
        return text
    if not toks:
        return text
    how = rng.randrange(6)
    t = toks[rng.randrange(len(toks))]
    a, b = t.offset, t.end
    if how == 5:
        lines = text.split("\n")
        k = rng.randrange(len(lines))
        if rng.randrange(2):
            del lines[k]
        else:
            j = rng.randrange(len(lines))
            lines[k], lines[j] = lines[j], lines[k]
        return "\n".join(lines)
    if t.kind == "ident":
        new = rng.choice(IDENTS)
    elif t.kind in ("int", "char"):
        new = rng.choice(LITERALS)
    elif t.kind == "keyword" and t.text in KEYWORDS:
        new = rng.choice(KEYWORDS[t.text])
    elif t.kind == "op" and t.text in BINOPS:
        new = rng.choice(BINOPS)
    elif t.kind == "op" and t.text in ASSIGNOPS:
        new = rng.choice(ASSIGNOPS)
    elif t.kind == "string" and "{" not in t.text:
        new = '"{%s}\\n"' % rng.choice(LITERALS)
    else:
        new = rng.choice(LITERALS + IDENTS)
    return text[:a] + new + text[b:]


def mutants(per_file: int, seed: int, sources: list) -> list:
    """Seeded mutations of `sources` ((name, bytes) pairs): one or two tokens replaced by
    tokens of the same class, or a line deleted or swapped."""
    rng = random.Random(seed)
    out = []
    for name, data in sources:
        text = data.decode("utf-8")
        for k in range(per_file):
            new = mutate(text, rng)
            if rng.randrange(3) == 0:
                new = mutate(new, rng)
            out.append(("mutant/%s/%d" % (name, k), new.encode("utf-8")))
    return out
