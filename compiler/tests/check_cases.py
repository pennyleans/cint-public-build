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
  - big_extents(): extents of 2^63 - 1 in a declaration and a `reshape` (box 15);
  - fixed(): fixed-point constant expressions in each place a constant expression is
    evaluated, and modules of the fixed-point forms that `cint_ref` checks or refuses, with
    `as|` and the result forms (box 15 unit 4e, OQ-272, OQ-273, OQ-278);
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


# A service extern of `fs/1` under its covering capability, for the I/O services cases.
FS_SERVICE = ('error FsError { denied }\n@service("fs/1", fs.read(path))\n'
              'extern observe FsError!I64 w(Grants g, in U8[] path, I64 offset, out U8[] buf);\n')

# Whole modules: (name, text).
MODULES = [
    ("hello", 'I64 x = 21 * 2;\n"hello, world\\n";\n"x = {x}\\n";\n'),
    # I/O services lane U1: extern, requires, @service and Grants (SPEC-04 LS-323 to LS-328,
    # SPEC-07 SEC-CAP-1, SEC-CAP-3, SEC-CAP-13). The capability check (C3040, C3041) runs over
    # the module's own declaration block (check.capabilities).
    ("extern_pure", "extern pure I64 f(I64 a);\nexport I64 run() { return f(1); }\n"),
    ("extern_service",
     '@service("console/1") extern observe E!void write(Grants g, in U8[] bytes);\n'
     "error E { denied }\nexport I64 run() { return 0; }\n"),
    ("requires_only", 'requires fs.read("/data");\nexport I64 run() { return 0; }\n'),
    ("extern_observe_call", "extern observe I64 f(I64 a, Bool b);\nvoid main() {\n    _ = f(1, true);\n}\n"),
    ("extern_unrestricted", "extern unrestricted I64 f(I64 a);\nvoid main() {\n    _ = f(1);\n}\n"),
    ("extern_private_call",
     "extern observe I64 f(I64 a);\nI64 g() {\n    return f(2);\n}\nvoid main() {\n    _ = g();\n}\n"),
    ("extern_export", "export extern observe I64 f(I64 a);\nvoid main() {\n    _ = f(1);\n}\n"),
    ("extern_void", "extern observe void f(I64 a);\nvoid main() {\n    f(1);\n}\n"),
    ("extern_q", "extern observe Q32.32 f(Q32.32 a);\nvoid main() {\n    _ = f(1.5);\n}\n"),
    ("extern_array_param", "extern observe I64 f(in I64[] a, out U8[] b);\nvoid main() {\n    U8[4] b;\n"
     "    I64[2] a;\n    _ = f(a, b);\n}\n"),
    ("extern_view_result", "extern observe in U8[] f(I64 a);\nvoid main() {\n}\n"),
    ("extern_named_value", "extern observe I64 f(I64 p);\nvoid main() {\n    I64 a = f;\n}\n"),
    ("extern_dup", "extern observe I64 f(I64 p);\nI64 f(I64 p) {\n    return p;\n}\nvoid main() {\n}\n"),
    ("extern_dup_extern", "extern observe I64 f(I64 p);\nextern observe I64 f(I64 p);\nvoid main() {\n}\n"),
    ("extern_prelude_name", "extern observe I64 len(I64 p);\nvoid main() {\n}\n"),
    ("extern_arg_count", "extern observe I64 f(I64 p);\nvoid main() {\n    _ = f(1, 2);\n}\n"),
    ("extern_arg_type", "extern observe I64 f(I64 p);\nvoid main() {\n    _ = f(true);\n}\n"),
    ("extern_out_scalar", "extern observe I64 f(out I64 p);\nvoid main() {\n}\n"),
    ("extern_inout_scalar", "extern observe I64 f(inout I64 p);\nvoid main() {\n}\n"),
    ("extern_struct_param", "struct P { I64 a; }\nextern observe I64 f(P p);\nvoid main() {\n}\n"),
    ("extern_errset_param", "error E { x }\nextern observe I64 f(E a);\nvoid main() {\n}\n"),
    ("extern_struct_result", "struct P { I64 a; }\nextern observe P f(I64 p);\nvoid main() {\n}\n"),
    ("extern_union_struct_result",
     "struct P { I64 a; }\nerror E { x }\nextern observe E!P f(I64 p);\nvoid main() {\n}\n"),
    ("extern_unknown_type", "extern observe I64 f(Foo a);\nvoid main() {\n}\n"),
    ("extern_unknown_result", "extern observe Foo f(I64 a);\nvoid main() {\n}\n"),
    ("extern_grants_param", "extern observe I64 f(Grants g, I64 a);\nvoid main(Grants g) {\n    _ = f(g, 1);\n}\n"),
    ("grants_main", "void main(Grants g) {\n}\n"),
    ("grants_main_union", "error E { x }\nE!void main(Grants g) {\n}\n"),
    ("grants_pass", "void f(Grants g, I64 a) {\n}\nvoid main(Grants g) {\n    f(g, 1);\n}\n"),
    ("grants_local", "void main(Grants g) {\n    Grants h = g;\n}\n"),
    ("grants_array", "void main(Grants g) {\n    Grants[2] a;\n}\n"),
    ("grants_result", "Grants f(Grants g) {\n    return g;\n}\nvoid main(Grants g) {\n    _ = f(g);\n}\n"),
    ("grants_field", "struct P { Grants g; }\nvoid main() {\n}\n"),
    ("grants_modvar", "Grants x;\nvoid main() {\n}\n"),
    ("grants_to_int", "void main(Grants g) {\n    I64 a = g;\n}\n"),
    ("grants_wrong_arg", "void f(Grants g) {\n}\nvoid main(Grants g) {\n    f(1);\n}\n"),
    ("grants_compare", "void main(Grants g) {\n    if (g == g) {\n    }\n}\n"),
    ("grants_print", 'void main(Grants g) {\n    "{g}\\n";\n}\n'),
    ("grants_arith", "void main(Grants g) {\n    I64 a = g + 1;\n}\n"),
    ("service_call_no_requires", FS_SERVICE +
     'FsError!void main(Grants g) {\n    U8[4] b;\n    _ = try w(g, "/doc/a", 0, b);\n}\n'),
    ("service_call_requires", 'requires fs.read("/doc");\n' + FS_SERVICE +
     'FsError!void main(Grants g) {\n    U8[4] b;\n    _ = try w(g, "/doc/a", 0, b);\n}\n'),
    ("service_call_twice", FS_SERVICE +
     'FsError!void main(Grants g) {\n    U8[4] b;\n    _ = try w(g, "/doc/a", 0, b);\n'
     '    _ = try w(g, "/doc/b", 0, b);\n}\n'),
    ("service_call_in_function", FS_SERVICE +
     'FsError!I64 f(Grants g) {\n    U8[4] b;\n    return try w(g, "/doc/a", 0, b);\n}\n'
     'FsError!void main(Grants g) {\n    _ = try f(g);\n}\n'),
    ("service_no_denied", 'error FsError { other }\n@service("fs/1", fs.read(path))\n'
     'extern observe FsError!I64 w(Grants g, in U8[] path);\nvoid main(Grants g) {\n}\n'),
    ("service_void_result", '@service("console/1")\nextern observe void w(Grants g, in U8[] bytes);\n'
     "void main(Grants g) {\n}\n"),
    ("service_no_capability", 'error E { denied }\n@service("console/1")\n'
     'extern observe E!void w(Grants g, in U8[] bytes);\nE!void main(Grants g) {\n    try w(g, "hi");\n}\n'),
    ("service_pure", 'error E { denied }\n@service("console/1")\nextern pure E!void w(Grants g, in U8[] bytes);\n'
     "void main(Grants g) {\n}\n"),
    ("service_two_kinds", 'error E { denied }\nrequires svc.op("/a", "x");\n@service("svc/1", svc.op(path, name))\n'
     'extern observe E!I64 w(Grants g, in U8[] p);\nE!void main(Grants g) {\n    _ = try w(g, "/a");\n}\n'),
    ("requires_unknown", 'requires fs.nothing("/doc");\nvoid main(Grants g) {\n}\n'),
    ("requires_arg_count", 'requires fs.read("/doc", "/x");\n' + FS_SERVICE + "void main(Grants g) {\n}\n"),
    ("requires_no_arg", "requires fs.read();\n" + FS_SERVICE + "void main(Grants g) {\n}\n"),
    ("requires_non_string", "requires fs.read(1);\n" + FS_SERVICE + "void main(Grants g) {\n}\n"),
    ("requires_expr", "requires fs.read(1 + 2);\n" + FS_SERVICE + "void main(Grants g) {\n}\n"),
    ("requires_root", 'requires fs.read("/");\n' + FS_SERVICE + "void main(Grants g) {\n}\n"),
    ("requires_trailing_slash", 'requires fs.read("/doc/");\n' + FS_SERVICE + "void main(Grants g) {\n}\n"),
    ("requires_relative", 'requires fs.read("doc");\n' + FS_SERVICE + "void main(Grants g) {\n}\n"),
    ("requires_dotdot", 'requires fs.read("/a/../b");\n' + FS_SERVICE + "void main(Grants g) {\n}\n"),
    ("requires_device", 'requires fs.read("/CON.txt/a");\n' + FS_SERVICE + "void main(Grants g) {\n}\n"),
    ("requires_space", 'requires fs.read("/a b");\n' + FS_SERVICE + "void main(Grants g) {\n}\n"),
    ("requires_upper", 'requires fs.read("/Doc");\n' + FS_SERVICE + "void main(Grants g) {\n}\n"),
    ("requires_name", 'error E { denied }\nrequires net.open("example.org");\n@service("net/1", net.open(name))\n'
     "extern observe E!I64 w(Grants g, in U8[] host);\nvoid main(Grants g) {\n}\n"),
    ("requires_name_slash", 'error E { denied }\nrequires net.open("a/b");\n@service("net/1", net.open(name))\n'
     "extern observe E!I64 w(Grants g, in U8[] host);\nvoid main(Grants g) {\n}\n"),
    ("requires_name_empty", 'error E { denied }\nrequires net.open("");\n@service("net/1", net.open(name))\n'
     "extern observe E!I64 w(Grants g, in U8[] host);\nvoid main(Grants g) {\n}\n"),
    ("requires_twice", 'requires fs.read("/doc");\nrequires fs.read("/doc");\n' + FS_SERVICE +
     'FsError!void main(Grants g) {\n    U8[4] b;\n    _ = try w(g, "/doc/a", 0, b);\n}\n'),
    ("requires_long", 'requires fs.read("/%s");\n' % ("a" * 300) + FS_SERVICE + "void main(Grants g) {\n}\n"),
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
] + [("fill_reshape/" + name, "void put(inout I64[_, _] a, in I64[_] p) {\n    fill(a[0, ..], p[0]);\n}\n\n"
      "export I64 run(I64 i) {\n    I64[6] m = [1, 2, 3, 4, 5, 6];\n    I8[2, 3] g;\n    I64[2, 3] a;\n"
      "    Bool[4] f;\n    I256[4] w;\n%s\n    return 0;\n}\n" % body)
     for name, body in [
    # Box 15: `fill` and `reshape`, as `cint_ref` checks them (_copy_fill, _array_builtin) and
    # as check.ci h_reshape refuses the forms that can fault or be refused at run time.
    ("fill", "    fill(a, i);\n    fill(f[1..], true);\n    fill(reverse(w, 0)[0..2], -(1 as I256 << 200));"),
    ("fill_views", "    fill(m, 7);\n    fill(transpose(g)[1, ..], -1);\n    put(a, m);"),
    ("fill_args", "    fill(a);"),
    ("fill_args3", "    fill(a, 1, 2);"),
    ("fill_named", "    fill(a, v = 1);"),
    ("fill_type", "    fill(a, true);"),
    ("fill_narrow", "    fill(g, 300);"),
    ("fill_scalar", "    fill(i, 1);"),
    ("fill_element", "    fill(a[0, 0], 1);"),
    ("fill_value_name", "    fill(a, zz);"),
    ("reshape", "    I64[3, 2] b = reshape(m, 3, 2);\n    I64 s = sum(reshape(reshape(a, 6), 1, 2, 3)) + b[2, 1];"),
    ("reshape_fill", "    fill(reshape(g, 6)[1..3], 4);\n    copy(reshape(a, 3, 2), reshape(m, 3, 2));"),
    ("reshape_value", "    I64 s = reshape(m, 2, 3)[1, 2] + transpose(reshape(m, 3, 2))[1, 0] + extent(reshape(w, 2, 2), 0);"),
    ("reshape_args", "    I64 s = sum(reshape(m));"),
    ("reshape_type", "    I64 s = sum(reshape(m, true));"),
    ("reshape_scalar", "    I64 s = sum(reshape(i, 1));"),
    ("reshape_decl", "    I64[2, 3] b = reshape(m, 3, 2);"),
    ("reshape_alias", "    copy(reshape(m, 6)[0..4], m[2..6]);"),
    ("reshape_disjoint", "    copy(reshape(m, 2, 3)[0, ..], reshape(m, 2, 3)[1, ..]);"),
    ("reshape_rank5", "    I64 s = sum(reshape(m, 1, 1, 1, 2, 3));"),
    ("reshape_negative", "    I64 s = sum(reshape(m, -1, 6));"),
    ("reshape_variable", "    I64 s = sum(reshape(m, i, 3));"),
    ("reshape_count", "    I64 s = sum(reshape(m, 2, 2));"),
    ("reshape_slice", "    I64 s = sum(reshape(m[0..4], 2, 2));"),
    ("reshape_view", "    I64 s = sum(reshape(transpose(a), 6));"),
    ("reshape_argument", "    I64 s = 0;\n    put(a, reshape(a, 6));"),
    ("reshape_extent", "    I64 s = sum(reshape(m, 1, 3071, 0));"),
]] + [
    # A string literal is no writable place, so it cannot fill an `out` view (C2067, LS-119, LS-323),
    # of a module's own extern and of an imported one alike (release tag lane finding at 193eb10).
    # Last, so that the seeded mutants of the earlier modules keep their texts.
    ("service_call_out_literal", 'requires fs.read("/doc");\n' + FS_SERVICE +
     'FsError!void main(Grants g) {\n    _ = try w(g, "/doc/a", 0, "xx");\n}\n'),
    ("service_import_out_literal", 'import fs;\nrequires fs.read("/doc");\n'
     'fs.FsError!void main(Grants g) {\n    _ = try fs.read(g, "/doc/a", 0, "xx");\n}\n'),
    # I/O services lane U6 (OQ-306 A; SPEC-04 LS-221): the parameter lists of `main`, the four
    # forms and C3044 for the rest. Last, so that the seeded mutants of the earlier modules keep
    # their texts.
    ("main_args_both", 'void main(Grants g, in U8[_] args) {\n    "{len(args)}\\n";\n}\n'),
    ("main_args_only", "void main(in U8[_] args) {\n    I64 n = 0;\n    for i in 0..len(args) {\n"
     "        if (args[i] == 0) {\n            n += 1;\n        }\n    }\n}\n"),
    ("main_args_after_scalar", "void main(Grants g, I64 x) {\n}\n"),
    ("main_args_scalar", "void main(I64 x) {\n}\n"),
    ("main_args_order", "void main(in U8[_] args, Grants g) {\n}\n"),
    ("main_args_inout", "void main(inout U8[_] args) {\n}\n"),
    ("main_args_fixed", "void main(in U8[4] args) {\n}\n"),
    ("main_args_two_grants", "void main(Grants g, Grants h) {\n}\n"),
    # Ruling OQ-307 A (SPEC-04 LS-41, LS-71): byte strings as read-only `U8` views of static
    # storage, and `equal`. Last, so that the seeded mutants of the earlier modules keep their texts.
    ("bytes_values", 'I64 width(in U8[_] v) {\n    return len(v);\n}\n\nexport I64 run() {\n'
     '    in U8[_] v = b"a\\x00\\xff\\n{{}}";\n    U8[3] a = b"abc";\n    a = b"xyz";\n'
     '    U8 q = b"pq"[1];\n    I64 n = width(b"") + width(v) + len(b"\u00e9");\n'
     '    Bool e = equal(a, b"xyz") && !equal(v, b"a");\n    return n + q as I64 + v[2] as I64;\n}\n'),
    ("bytes_extent", 'export void run() {\n    U8[3] a = b"abcd";\n}\n'),
    ("bytes_element", 'export void run() {\n    I8[3] a = b"abc";\n}\n'),
    ("bytes_inout_arg", 'void put(inout U8[_] v) {\n    v[0] = 1;\n}\n\nexport void run() {\n    put(b"abc");\n}\n'),
    ("bytes_inout_var", 'export void run() {\n    inout U8[_] v = b"abc";\n}\n'),
    ("bytes_escape", 'export void run() {\n    in U8[_] v = b"\\u{41}";\n}\n'),
    ("bytes_brace", 'export void run() {\n    in U8[_] v = b"a{b";\n}\n'),
    ("bytes_kernel", 'kernel k[n](in U8[n] a, out I64[n] b) over [i: n] {\n    b[i] = b"xy"[0] as I64;\n}\n'),
    ("equal_values", 'export I64 run() {\n    I32[3] x = [1, 2, 3];\n    I64[2, 2] g = [[1, 2], [3, 4]];\n'
     '    Bool[2] m = [true, false];\n    Q8.8[1] f = [Q8.8.raw(3)];\n'
     '    Bool p = equal(x, [1, 2, 3]) && equal([1, 2], x[0..2]) && equal(g, g) && equal(m, m) && equal(f, f);\n'
     '    return 0;\n}\n'),
    ("equal_element_types", 'export void run() {\n    I32[3] x;\n    I64[3] y;\n    Bool p = equal(x, y);\n}\n'),
    ("equal_ranks", 'export void run() {\n    I32[3] x;\n    I32[1, 3] y;\n    Bool p = equal(x, y);\n}\n'),
    ("equal_scalar", 'export void run() {\n    I32 x = 3;\n    Bool p = equal(x, x);\n}\n'),
    ("equal_count", 'export void run() {\n    I32[3] x;\n    Bool p = equal(x);\n}\n'),
    ("equal_result", 'export void run() {\n    I32[3] x;\n    I32 p = equal(x, x);\n}\n'),
    ("equal_structs", 'struct P {\n    I32 a;\n}\n\nexport void run() {\n    P[2] x;\n    Bool p = equal(x, x);\n}\n'),
    ("equal_literals", 'export void run() {\n    Bool p = equal([1, 2], [1, 2]);\n}\n'),
    ("equal_string", 'export void run() {\n    Bool p = equal(b"ab", "ab");\n}\n'),
    ("equal_kernel", 'kernel k[n](in I64[n] a, out I64[n] b) over [i: n] {\n    b[i] = 0;\n'
     '    if (equal(a, a)) {\n        b[i] = 1;\n    }\n}\n'),
    # Ruling OQ-315 A (SPEC-04 LS-131a): `call catch (e) { ... };` for an `E!void` call, whose block
    # may complete; any other error union is C4012 at the call.
    ("catch_statement", 'error E {\n    bad,\n    worse,\n}\n\nE!void f(I64 x) {\n    if (x < 0) {\n        return .bad;\n    }\n}\n\nE!I64 g() {\n    return .worse;\n}\n\nexport I64 run() {\n    I64 n = 0;\n    for i in 0..3 {\n'
     '        f(i - 1) catch (e) {\n            if (e == E.bad) {\n                n += 1;\n                continue;\n'
     '            }\n            n += 2;\n        };\n        n += 10;\n    }\n    f(-1) catch (e) {\n        n += 100;\n    };\n'
     '    return n;\n}\n'),
    ("catch_statement_value", 'error E {\n    bad,\n    worse,\n}\n\nE!void f(I64 x) {\n    if (x < 0) {\n        return .bad;\n    }\n}\n\nE!I64 g() {\n    return .worse;\n}\n\nexport void run() {\n    g() catch (e) {\n        return;\n    };\n}\n'),
    ("catch_statement_value_completes", 'error E {\n    bad,\n    worse,\n}\n\nE!void f(I64 x) {\n    if (x < 0) {\n        return .bad;\n    }\n}\n\nE!I64 g() {\n    return .worse;\n}\n\nexport void run() {\n    g() catch (e) {\n    };\n}\n'),
    ("catch_statement_not_call", 'error E {\n    bad,\n    worse,\n}\n\nE!void f(I64 x) {\n    if (x < 0) {\n        return .bad;\n    }\n}\n\nE!I64 g() {\n    return .worse;\n}\n\nexport void run() {\n    I64 v = 0;\n    v catch (e) {\n    };\n}\n'),
    ("catch_statement_bind_scope", 'error E {\n    bad,\n    worse,\n}\n\nE!void f(I64 x) {\n    if (x < 0) {\n        return .bad;\n    }\n}\n\nE!I64 g() {\n    return .worse;\n}\n\nexport void run() {\n    f(1) catch (e) {\n    };\n    E x = e;\n}\n'),
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


# Box 15 unit 4b: constant expressions of the wide integer types, each folded in the places
# of WIDE_CONST_PLACES ({t} is the expression's type): every operator and built-in on the
# four signed widths, at their bounds, and the faults whose records carry wide operands,
# exact results and limits; since unit 4c, shifts by a wide count as well.
WIDE_CONST_EXPRS = [
    ("I128", "I128.max + 1"), ("I128", "I128.min - 1"), ("I128", "I128.min * -1"), ("I128", "I128.min / -1"),
    ("I128", "I128.min % -1"), ("I128", "-I128.min"), ("I128", "(1 as I128) << 127"), ("I128", "(1 as I128) << 126"),
    ("I128", "(1 as I128) << 128"), ("I128", "I128.max >> 127"), ("I128", "I128.min >> 200"),
    ("I128", "(3 as I128) <<% 127"), ("I128", "I128.max +% 1"), ("I128", "I128.min -| 1"), ("I128", "I128.max *| 2"),
    ("I128", "I128.max *% I128.max"), ("I128", "~I128.max"), ("I128", "1 / (0 as I128)"), ("I128", "7 % (0 as I128)"),
    ("I128", "I64.max as I128 * (I64.max as I128) * 4"), ("I128", "18446744073709551616 * 18446744073709551616"),
    ("I128", "170141183460469231731687303715884105728 as I128"), ("I128", "-170141183460469231731687303715884105729"),
    ("I128", "(I256.max / 2) as I128"), ("I128", "(I256.max / 2) as% I128"), ("I128", "mul_full(I64.max, I64.min)"),
    ("I128", "mul_full(I64.min, I64.min)"), ("I128", "abs(I128.min)"), ("I128", "abs(I128.min + 1)"),
    ("I128", "min(I128.min, 5) + max(I128.max, -3)"), ("I128", "clamp(I128.max, 0, 7) * 3"),
    ("I128", "div_trunc(I128.min, -1)"), ("I128", "rem_trunc(I128.min, -1)"), ("I128", "div_euclid(-7, 2)"),
    ("I128", "rem_euclid(-7, 0)"), ("I128", "div_round(I128.max, 2, half_even)"), ("I128", "div_round(7, 0, floor)"),
    ("I128", "muldiv(I128, I128.max, I128.max, I128.max, floor)"), ("I128", "muldiv(I128, I128.max, 3, 2, ceil)"),
    ("I128", "muldiv(I128, 5, 7, 0, trunc)"), ("I128", "isqrt(I128.max)"), ("I128", "isqrt(-1 as I128)"),
    ("I128", "isqrt_round(I128.max, half_up)"), ("I128", "true ? I128.max : I128.max + 1"),
    ("I128", "I128.max > 0 ? I128.min - 1 : 0"),
    ("I256", "I256.max + 1"), ("I256", "I256.min * I256.min"), ("I256", "-I256.min"), ("I256", "I256.min / -1"),
    ("I256", "(I128.max as I256) * (I128.max as I256) * 4"), ("I256", "(1 as I256) << 255"), ("I256", "I256.max >> 3"),
    ("I256", "mul_full(I128.max, I128.max)"), ("I256", "mul_full(I128.min, I128.min) + 1"),
    ("I256", "abs(I256.min)"), ("I256", "div_round(I256.min, 3, away)"), ("I256", "isqrt(I256.max) + 1"),
    ("I256", "muldiv(I256, I256.max, I256.max, 1, floor)"), ("I256", "(I512.max as% I256) - 1"),
    ("I512", "I512.max + 1"), ("I512", "I512.min - 1"), ("I512", "I512.min * -1"), ("I512", "I512.min % -1"),
    ("I512", "(1 as I512) << 511"), ("I512", "(I256.min as I512) * (I256.min as I512) * 2"),
    ("I512", "mul_full(I256.min, I256.min)"), ("I512", "isqrt_round(I512.max, half_even) * 2"),
    ("I512", "div_euclid(I512.min, -1)"), ("I512", "rem_euclid(I512.min, -1)"), ("I512", "clamp(I512.min, 0, -1)"),
    ("I1024", "I1024.max + 1"), ("I1024", "I1024.min * I1024.min"), ("I1024", "I1024.min / -1"),
    ("I1024", "-I1024.min"), ("I1024", "(1 as I1024) << 1023"), ("I1024", "(1 as I1024) << 1024"),
    ("I1024", "mul_full(I512.min, I512.min)"), ("I1024", "mul_full(I512.min, I512.max) - 1"),
    ("I1024", "I1024.max / 3 * 4"), ("I1024", "I1024.max *| I1024.max"), ("I1024", "I1024.min *% 3"),
    ("I1024", "muldiv(I1024, I1024.max, I1024.max, I1024.max, half_even)"),
    ("I1024", "muldiv(I1024, I1024.max, I1024.max, 2, floor)"), ("I1024", "isqrt(I1024.max)"),
    ("I1024", "div_round(I1024.max, I1024.min, half_down)"), ("I1024", "(I1024.max as I512)"),
    # Box 15 unit 4c: shifts by a count of a wide type (rt/OPEN.md RT-OQ-52).
    ("I64", "1 << (63 as I128)"), ("I64", "1 << (64 as I128)"), ("I64", "-9 >> (2 as I512)"),
    ("I64", "1 << ((1 as I128) << 64)"), ("I8", "(1 as I8) << (7 as I1024)"), ("I128", "(1 as I128) << (-1 as I256)"),
    ("I256", "(1 as I256) << (255 as I1024)"), ("I256", "(3 as I256) <<% (254 as I128)"),
    ("I256", "(1 as I256) <<% (256 as I128)"), ("I1024", "I1024.min >> (1023 as I128)"),
    # Box 15 unit 4d: the unsigned wide types U128 to U512 (ruling BX15-02), their operators,
    # built-ins, rotations and conversions to and from the signed and narrow types.
    ("U128", "U128.max + 1"), ("U128", "U128.min - 1"), ("U128", "0 - (1 as U128)"), ("U128", "U128.max * 2"),
    ("U128", "U128.max / 3"), ("U128", "U128.max % 10"), ("U128", "(1 as U128) << 127"),
    ("U128", "(1 as U128) << 128"), ("U128", "(3 as U128) << 127"), ("U128", "U128.max >> 127"),
    ("U128", "(3 as U128) <<% 127"), ("U128", "U128.max +% 1"), ("U128", "U128.min -% 1"), ("U128", "U128.min -| 1"),
    ("U128", "U128.max +| 1"), ("U128", "U128.max *| 2"), ("U128", "~U128.min"), ("U128", "~(5 as U128)"),
    ("U128", "1 / (0 as U128)"), ("U128", "340282366920938463463374607431768211455"),
    ("U128", "340282366920938463463374607431768211456"), ("U128", "-1 as U128"), ("U128", "(-1 as I128) as U128"),
    ("U128", "(-1 as I128) as% U128"), ("U128", "U256.max as U128"), ("U128", "U256.max as% U128"),
    ("U128", "I128.max as U128 + 1"), ("U128", "mul_full(U64.max, U64.max)"), ("U128", "uabs(I128.min)"),
    ("U128", "uabs(I128.max) + 1"), ("U128", "min(U128.max, 5) + max(U128.min, 3)"), ("U128", "clamp(U128.max, 2, 7)"),
    ("U128", "div_round(U128.max, 2, half_even)"), ("U128", "div_euclid(U128.max, 0)"),
    ("U128", "rem_euclid(U128.max, 7)"), ("U128", "muldiv(U128, U128.max, U128.max, U128.max, floor)"),
    ("U128", "muldiv(U128, U128.max, 3, 2, ceil)"), ("U128", "isqrt(U128.max)"),
    ("U128", "isqrt_round(U128.max, half_up)"), ("U128", "rotl(U128.max - 1, 1)"), ("U128", "rotr(1 as U128, 1)"),
    ("U128", "rotl(5 as U128, 128)"), ("U128", "rotr(U128.max >> 64, 64)"), ("U128", "rotl(5 as U128, -1)"),
    ("U256", "U256.max + 1"), ("U256", "(U128.max as U256) * (U128.max as U256)"),
    ("U256", "mul_full(U128.max, U128.max)"), ("U256", "uabs(I256.min)"), ("U256", "rotl((1 as U256) << 255, 3)"),
    ("U256", "U256.max - U256.max / 2"), ("U256", "I512.min as% U256"), ("U256", "(1 as U256) << (255 as I128)"),
    ("U256", "U256.max >> (200 as U512)"), ("U256", "(1 as U256) << (256 as U128)"), ("U512", "U512.max + 1"),
    ("U512", "mul_full(U256.max, U256.max)"), ("U512", "mul_full(U256.max, U256.max) + 1"), ("U512", "uabs(I512.min)"),
    ("U512", "U512.max *| U512.max"), ("U512", "U512.max *% 3"), ("U512", "I1024.max as U512"),
    ("U512", "I1024.max as% U512"), ("U512", "(U256.max as U512) << 256"), ("U512", "rotr(U512.max - 1, 511)"),
    ("U512", "isqrt(U512.max)"), ("U512", "U512.max / U512.max"), ("I1024", "U512.max as I1024 + 1"),
    ("I64", "(U128.max >> 65) as I64"), ("U64", "(U128.max >> 64) as U64"), ("I128", "U128.max as I128"),
    ("I128", "U128.max as% I128"), ("U8", "(U512.max >> 504) as U8"),
]
WIDE_CONST_PLACES = [
    "const {t} X = {e};", "export I64 run() {{ {t} x = {e}; return 0; }}",
    "export I64 run() {{ const {t} L = {e}; return 0; }}", "{t} g = {e};",
    "const Bool B = false && ({e} == 0);", "const {t} Y = true ? 0 : {e};", "static_assert({e} != 7);",
    "export I64 run() {{ \"{{{e}}}\\n\"; return 0; }}", "const {t} A = {e};\nconst {t} B = A * 2;",
]


def wide_consts() -> list:
    out = []
    for ei, (t, e) in enumerate(WIDE_CONST_EXPRS):
        for pi, place in enumerate(WIDE_CONST_PLACES):
            out.append(("wide_const/%d_%d" % (ei, pi), (place.format(t=t, e=e) + "\n").encode("utf-8")))
    return out


# Box 15 unit 4e and `as|`: constant expressions of fixed point and saturating conversions,
# each folded in the places of WIDE_CONST_PLACES ({t} is the expression's type): literals on
# and off the grid, `round` clauses, the operators and built-ins at the bounds of their types,
# the conversion forms, `Q<i>.<f>.raw(n)`, and the faults whose records carry fixed-point
# operands. A fraction literal as a conversion's source, a type property past `min`, `max` and
# `epsilon`, a `qmul` mode that does not round and a conversion between fixed point and a
# wide type are C9102 in B1 (check_suite.FIXED_REFUSALS).
FIXED_CONST_EXPRS = [
    ("Q16.16", "1.5"), ("Q16.16", "0.1"), ("Q16.16", "0.1 round floor"), ("Q16.16", "-0.1 round ceil"),
    ("Q16.16", "0.1 round nearest"), ("Q8.8", "200.0"), ("Q8.8", "-128.0"), ("Q16.16", "Q16.16.max + 0.5"),
    ("Q16.16", "Q16.16.min - Q16.16.epsilon"), ("Q16.16", "Q16.16.max * 2.0"), ("Q16.16", "-Q16.16.min"),
    ("Q16.16", "1.0 / (Q16.16.epsilon - Q16.16.epsilon)"), ("Q16.16", "Q16.16.max / Q16.16.epsilon"),
    ("Q16.16", "Q16.16.max +| 1.0"), ("Q16.16", "Q16.16.min -% 1.0"), ("Q16.16", "Q16.16.epsilon * 0.5"),
    ("Q16.16", "mul(Q16.16.epsilon, 0.5, ceil)"), ("Q16.16", "div(1.0, 3.0, floor)"), ("Q16.16", "sqrt(2.0)"),
    ("Q16.16", "mul_sat(Q16.16.max, 2.0, floor)"), ("Q16.16", "mul_wrap(Q16.16.max, 2.0, trunc)"),
    ("Q16.16", "qmul(Q16.16, Q8.8.max, Q8.8.max)"), ("Q8.8", "qmul(Q8.8, Q16.16.max, 1.0, half_up)"),
    ("Q16.16", "qmul(Q16.16, Q8.8.max, Q8.8.max, floor)"), ("Q16.16", "mul_int(Q16.16.max, 2)"),
    ("Q16.16", "abs(Q16.16.min)"), ("Q16.16", "min(Q16.16.max, 1.5) + max(-2.0, Q16.16.min)"),
    ("Q16.16", "Q16.16.raw(65536) * 3.0"), ("Q16.16", "Q16.16.raw(-1)"), ("I32", "Q16.16.max as I32"),
    ("I32", "Q16.16.min as I32 round floor"), ("I8", "Q16.16.max as I8"), ("I8", "Q16.16.max as% I8"),
    ("I8", "Q16.16.max as| I8"), ("I8", "Q16.16.min as| I8 round ceil"), ("Q8.8", "Q16.16.max as Q8.8"),
    ("Q8.8", "Q16.16.max as| Q8.8"), ("Q8.8", "Q16.16.min as% Q8.8"), ("Q8.8", "1.75 as Q8.8"),
    ("Q16.16", "Q8.8.max as Q16.16"), ("Q16.16", "Q8.8.min as Q16.16 round floor"), ("Q16.16", "100000 as| Q16.16"),
    ("Q16.16", "-100000 as Q16.16"), ("Q16.16", "I64.max as| Q16.16"), ("Q16.16", "Q16.16.bits"),
    ("Q16.16", "(5 as I128) as Q16.16"), ("I128", "Q16.16.max as| I128"), ("I8", "1000 as| I8"),
    ("U8", "-5 as| U8"), ("I32", "I64.min as| I32"), ("U64", "I64.min as| U64"), ("I128", "U128.max as| I128"),
    ("U8", "U512.max as| U8"), ("I64", "(I64.max as| I32) as I64 + 1"), ("I8", "true as| I8"),
    ("I8", "1000 as| I8 round floor"),
]


def fixed() -> list:
    out = []
    for ei, (t, e) in enumerate(FIXED_CONST_EXPRS):
        for pi, place in enumerate(WIDE_CONST_PLACES):
            out.append(("fixed_const/%d_%d" % (ei, pi), (place.format(t=t, e=e) + "\n").encode("utf-8")))
    for name, text in FIXED_MODULES:
        out.append(("fixed/" + name, text.encode("utf-8")))
    return out


# Box 15: modules of the fixed-point forms whose outcome is not a constant's, the C9102 ones of
# check.ci included.
FIXED_MODULES = [
    ("arith", "export I64 run(Q16.16 a, Q16.16 b) {\n    Q16.16 c = a * b + a / b - 0.25;\n"
              "    Q16.16 d = c *| 2.0 -| a;\n    return (d as I64) + (c.raw as I64);\n}\n"),
    ("rem", "export I64 run(Q16.16 a, Q16.16 b) {\n    Q16.16 c = a % b;\n    return 0;\n}\n"),
    ("mixed", "export I64 run(Q16.16 a, Q8.8 b) {\n    Q16.16 c = a + b;\n    return 0;\n}\n"),
    ("increment", "export I64 run() {\n    Q16.16 x = 1.0;\n    x++;\n    return 0;\n}\n"),
    ("compound", "export I64 run() {\n    Q16.16 x = 1.0;\n    x += 0.5;\n    x *= 3.0;\n"
                 "    return x as I64;\n}\n"),
    ("integer_context", "export I64 run() {\n    I64 x = 1.5;\n    return x;\n}\n"),
    ("frac_source", "export I64 run() {\n    Q16.16 x = 0.5 as Q16.16;\n    return 0;\n}\n"),
    ("round_unknown", "export I64 run() {\n    Q16.16 x = 0.1 round sideways;\n    return 0;\n}\n"),
    ("round_clause_exact", "export I64 run(Q16.16 a) {\n    Q32.32 x = a as Q32.32 round floor;\n    return 0;\n}\n"),
    ("bad_type", "export I64 run() {\n    Q60.60 x = 1.0;\n    return 0;\n}\n"),
    ("bad_property", "export I64 run() {\n    Q16.16 x = Q16.16.bits;\n    return 0;\n}\n"),
    ("epsilon", "export I64 run() {\n    Q1.63 x = Q1.63.epsilon;\n    return x.raw;\n}\n"),
    ("raw_ctor", "export I64 run(I32 n) {\n    Q16.16 x = Q16.16.raw(n);\n    return x.raw as I64;\n}\n"),
    ("raw_ctor_type", "export I64 run(I64 n) {\n    Q16.16 x = Q16.16.raw(n);\n    return 0;\n}\n"),
    ("holes", "export I64 run(Q8.8 a) {\n    \"{a} {a:+} {a:*>9} {a:.2} {a!raw:x} {a!bits:#b} {a=}\\n\";\n"
              "    return 0;\n}\n"),
    ("hole_kind_d", "export I64 run(Q8.8 a) {\n    \"{a:d}\\n\";\n    return 0;\n}\n"),
    ("hole_kind_x", "export I64 run(Q8.8 a) {\n    \"{a:x}\\n\";\n    return 0;\n}\n"),
    ("hole_group", "export I64 run(Q8.8 a) {\n    \"{a:_}\\n\";\n    return 0;\n}\n"),
    ("hole_int_bits", "export I64 run(I64 a) {\n    \"{a!bits:x}\\n\";\n    return 0;\n}\n"),
    ("hole_int_raw", "export I64 run(I64 a) {\n    \"{a!raw}\\n\";\n    return 0;\n}\n"),
    ("hole_array", "export I64 run() {\n    Q8.8[3] v = [0.5, -1.25, 3.0];\n    \"{v} {v:.1} {v!raw}\\n\";\n"
                   "    return 0;\n}\n"),
    ("array_sum", "export I64 run() {\n    Q8.8[3] v = [0.5, -1.25, 3.0];\n    Q8.8 s = sum(v);\n"
                  "    return s.raw as I64;\n}\n"),
    ("array_sum_into_int", "export I64 run() {\n    Q8.8[3] v = [0.5, -1.25, 3.0];\n    I64 s = sum(I64, v);\n"
                           "    return s;\n}\n"),
    ("array_fold_mul", "export I64 run() {\n    Q8.8[3] v = [0.5, -1.25, 3.0];\n"
                       "    Q8.8 s = fold_checked(mul, 1.0, v);\n    return 0;\n}\n"),
    ("dot", "export I64 run() {\n    Q8.8[2] v = [0.5, 1.5];\n    Q16.16 d = dot(Q16.16, v, v);\n"
            "    return d.raw as I64;\n}\n"),
    ("script_array", "Q8.8[2] t = [0.5, 1.0];\n\"{t[1]}\\n\";\n"),
    ("script_array_late", "Q8.8[3] t;\nt[1] = 0.5;\n\"{t[1]}\\n\";\n"),
    ("round_no_mode", "export I64 run() {\n    Q16.16 x = -(0.1 round in);\n    Q16.16 y = 0.1 round ;\n"
                      "    return 0;\n}\n"),
    ("bit_field", "struct S {\n    Q16.16 x;\n    U16 m : 3;\n}\nexport I64 run() {\n    return 0;\n}\n"),
    ("module_state", "Q16.16 g = 2.5;\nQ8.8[2] t = [0.5, -1.0];\nexport I64 run() {\n    g = g * 2.0;\n"
                     "    t[1] = t[0] + t[1];\n    return g.raw as I64;\n}\n"),
    ("struct_field", "struct P {\n    Q16.16 x;\n    Q8.8[2] v;\n}\nexport I64 run() {\n"
                     "    Q8.8[2] v = [0.5, 0.25];\n    P p = P(1.5, v);\n    p.v[1] = p.v[0] - 0.75;\n"
                     "    return (p.x.raw as I64) + (p.v[1].raw as I64);\n}\n"),
    ("sat_bool", "export I64 run(Bool b) {\n    I8 x = b as| I8;\n    return 0;\n}\n"),
    ("sat_round_clause", "export I64 run(I64 a) {\n    I8 x = a as| I8 round floor;\n    return 0;\n}\n"),
    ("sat_forms", "export I64 run(I64 a, Q16.16 q) {\n    I8 x = a as| I8;\n    U16 y = a as| U16;\n"
                  "    Q8.8 z = q as| Q8.8;\n    I32 w = q as| I32 round half_up;\n    Q8.8 v = a as| Q8.8;\n"
                  "    return (x as I64) + (y as I64) + (w as I64) + (z.raw as I64) + (v.raw as I64);\n}\n"),
    ("result_forms",
     "ArithError!Q16.16 f(Q16.16 a, Q16.16 b) {\n    Q16.16 s = try add_result(a, b);\n"
     "    Q16.16 d = try sub_result(s, b);\n    Q16.16 m = try mul_result(d, b, floor);\n"
     "    return try div_result(m, b);\n}\n"
     "export I64 run(Q16.16 a, Q16.16 b) {\n    Q16.16 r = f(a, b) catch 0.0;\n    return r.raw as I64;\n}\n"),
    ("result_rem", "export I64 run(Q16.16 a, Q16.16 b) {\n    Q16.16 r = rem_result(a, b) catch 0.0;\n"
                   "    return 0;\n}\n"),
    ("result_mode_int", "export I64 run(I64 a, I64 b) {\n    I64 r = mul_result(a, b, floor) catch 0;\n"
                        "    return r;\n}\n"),
    ("result_unconsumed", "export I64 run(Q16.16 a, Q16.16 b) {\n    Q16.16 r = add_result(a, b);\n"
                          "    return 0;\n}\n"),
    ("result_mixed", "export I64 run(Q16.16 a, Q8.8 b) {\n    Q16.16 r = add_result(a, b) catch 0.0;\n"
                     "    return 0;\n}\n"),
    ("as_try", "export I64 run(Q16.16 q) {\n    I8 x = q as? I8 catch 0;\n    return x as I64;\n}\n"),
    ("kernel", "kernel scale[n](in Q16.16[n] a, out Q16.16[n] b) over [i: n] {\n    b[i] = a[i] * 2.0;\n}\n"
               "export I64 run() {\n    Q16.16[2] a = [1.5, 200.0];\n    Q16.16[2] b;\n    scale(a, b);\n"
               "    return b[0].raw as I64;\n}\n"),
]


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


def big_extents() -> list:
    """Extents of 2^63 - 1, C9102 in check.ci h_type and h_reshape rather than a fault of
    the extent plus 1. Not mutated: `cint_ref` takes these extents, so the model puts them
    outside only after `cint_ref`'s diagnostics, while B1 refuses them where it checks them."""
    def fn(body):
        return ("export I64 run() {\n    I64[6] m = [1, 2, 3, 4, 5, 6];\n%s\n    return 0;\n}\n" % body).encode("ascii")
    return [
        ("extent/local_i64_max", fn("    I64[9223372036854775807] a;")),
        ("extent/local_rank2_i64_max", fn("    I8[9223372036854775807, 3] b;")),
        ("extent/reshape_i64_max", fn("    I64 s = sum(reshape(m, 9223372036854775807));")),
        ("extent/reshape_rank2_i64_max", fn("    I64 s = sum(reshape(m, 9223372036854775807, 0));")),
    ]


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
