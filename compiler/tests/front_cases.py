"""Source files for the scanner and parser tests of the `front` suite (slice 2 task 2.10a).

Every generated source is compared with `cint_ref`'s outcome (front_ref.py), so this module
only produces inputs: the frozen `.ci` files and compiler sources (`frozen`), hand-written
programs for each diagnostic of the scanner and parser and for the forms of SPEC-04 18
(`written`), `else if` chains and switches of 1 to 5,000 arms for the constant-depth check
(`chains`), and seeded mutations of the frozen files (`mutants`). A source is (name, bytes).
"""
import pathlib
import random

ROOT = pathlib.Path(__file__).resolve().parents[2]

DEC_4096 = str(1 << 4096)


def balanced_ternary(v: int) -> str:
    digits = []
    while v:
        r = v % 3
        if r == 2:
            digits.append("N")
            v = (v + 1) // 3
        elif r == 1:
            digits.append("P")
            v = (v - 1) // 3
        else:
            digits.append("0")
            v //= 3
    return "".join(reversed(digits)) or "0"


def frozen() -> list:
    files = sorted((ROOT / "conformance").rglob("*.ci")) + sorted((ROOT / "compiler").rglob("*.ci"))
    return [(f.relative_to(ROOT).as_posix(), f.read_bytes()) for f in files]


def fn(body: str) -> bytes:
    return ("export I64 run() {\n" + body + "\n    return 0;\n}\n").encode("utf-8")


def stmt_cases() -> list:
    """Statement and expression texts, each the body of a function."""
    return [
        # literals and the D-21 negative literal (SPEC-04 LS-28, decision patch D-21 option 1)
        "I8 a = -128;", "I8 a = - 128;", "I8 a = -/*c*/128;", "I8 a = -(128);", "I8 a = - -128;",
        "I8 a = -\t128;", "I64 a = x - 1;", "I64 a = x -1;", "I64 a = -x;", "I64 a = -'a';", "I64 a = - 'a';",
        "I8 x = -(128) as I8;", "I8 x = - 128 as I8;", "U64 x = -1 as% U64;", "I64 x = ((((-128)))) as I8;",
        "U8 c = -(1);", "I64 a = -0;", "I64 a = -0x80;", "I64 a = -0b1;", "I64 a = -0o7;", "I64 a = --1;",
        "I64 a = -%1;", "I64 a = ~-1;", "Bool b = !true;", "I64 a = -x as I64;", "I64 a = (-x) as I64;",
        "I64 a = 1_000_000;", "I64 a = 0xFF_FF;", "I64 a = 0b1010_1010;", "I64 a = 0o7_7;",
        "I64 a = 'x';", "I64 a = '\\n';", "I64 a = '\\x41';", "I64 a = '\\u{1F600}';", "I64 a = 'é';",
        "I64 a = '\\'';", "I64 a = '\"';",
        # operators, precedence and the parenthesis rules (SPEC-04 7.1 to 7.3)
        "I64 a = 1 + 2 * 3 - 4 / 5 % 6;", "I64 a = 1 +% 2 *% 3 -% 4;", "I64 a = a << 1 >> 2 <<% 3;",
        "I64 a = a & b & c;", "I64 a = a | b | c;", "I64 a = a ^ b ^ c;", "I64 a = (a & b) | c;",
        "I64 a = a | b & c;", "I64 a = a & b | c;", "I64 a = a ^ b | c;", "I64 a = a & b + c;",
        "I64 a = a + b & c;", "I64 a = a << b + c;", "I64 a = a + b << c;", "I64 a = (a + b) << c;",
        "I64 a = a << (b + c);", "I64 a = a & (b + c);", "I64 a = a * b << c;", "I64 a = a << b << c;",
        "Bool b = a < b;", "Bool b = a < b < c;", "Bool b = a == b == c;", "Bool b = (a < b) == c;",
        "Bool b = a && b || c;", "Bool b = a || b && c;", "Bool b = a && b && c || d;", "Bool b = (a && b) || c;",
        "Bool b = a && (b || c);", "Bool b = a || b || c;", "Bool b = !a && b;", "Bool b = a < b && c > d;",
        "I64 a = c ? 1 : 2;", "I64 a = c ? d ? 1 : 2 : 3;", "I64 a = c ? 1 : d ? 2 : 3;", "I64 a = (c ? 1 : 2) + 3;",
        "I64 a = c ? 1;", "I64 a = x as I64 as I8;", "I64 a = x as% U8 as I64;", "I64 a = x as I64 round nearest;",
        "I64 a = x as I64 round;", "I64 a = x as;", "I64 a = x as 5;", "I64 a = x as m.T;", "I64 a = x as U8[4];",
        "I64 a = x as| I64;", "I64 a = x as? I64;", "I64 a = (x) as I8;", "I64 a = ~x as I8;",
        "I64 a = f(1, 2, 3);", "I64 a = f();", "I64 a = f(a = 1, b = 2);", "I64 a = f(1,);", "I64 a = f(,);",
        "I64 a = f(1 2);", "I64 a = m.f(1);", "I64 a = a.b.c;", "I64 a = a[1][2];", "I64 a = a[i].f[j];",
        "I64 a = f(1)[2];", "I64 a = f(g(h(1)));", "I64 a = I64.max;", "I64 a = U8.min + I64;", "I64 a = Bool;",
        "I64 a = (f)(1);", "I64 a = a[1](2);", "I64 a = a[1, 2];", "I64 a = a[1..2];", "I64 a = a[..];",
        "I64 a = a[..n];", "I64 a = a[..=n];",
        "I64 a = a[1;", "I64 a = (1;", "I64 a = (1, 2);", "I64 a = [1, 2];", "I64 a = .x;", "I64 a = ;",
        "I64 a = 1 +;", "I64 a = * 1;", "I64 a = a.;", "I64 a = a.1;", "I64 a = \"s\";", "I64 a = \"a\" \"b\";",
        "I64 a = true;", "I64 a = x++;", "I64 a = ++x;", "I64 a = (x)++;", "I64 a = a[i++];",
        # statements (SPEC-04 8)
        "x = 1;", "x += 1;", "x -= 1;", "x *= 1;", "x /= 1;", "x %= 1;", "x <<= 1;", "x >>= 1;", "x &= 1;",
        "x |= 1;", "x ^= 1;", "x +%= 1;", "x -%= 1;", "x *%= 1;", "x <<%= 1;", "x +|= 1;", "x -|= 1;",
        "x *|= 1;", "x++;", "x--;", "a[i] = 1;", "a.b = 1;", "a[i].b[j] = 1;", "a.b++;", "f(x);", "m.f(x);",
        "f(x) = 1;", "(x) = 1;", "x + 1;", "x;", "1;", ";", "x = 1", "x == 1;", "x = y = 1;", "_ = f(1);",
        "_ = 1;", "{ x = 1; }", "{ }", "{ { } }", "{", "}", "I64 x;", "I64 x = 1;", "I64[4] a;", "U8[_] s;",
        "vec.V3 v;", "vec.V3[2] vs;", "Tok t = Tok(1, 2);", "const I64 K = 5;", "const I64 K;",
        "const K = 5;", "I64 x = 1 I64 y = 2;", "I64 do = 1;", "I64 if = 1;", "I64 I8 = 1;", "I64 Q1 = 1;",
        "I64 T27x = 1;", "if (x) { }", "if (x) { } else { }", "if (x) { } else if (y) { } else { }",
        "if x { }", "if (x) y = 1;", "if (x) ;", "if (x = 1) { }", "if (x) { } else ;", "if (x) { } else y;",
        "if (x) { } else if y { }", "if (x { }", "if (x) { } elif (y) { }", "while (x) { }", "while x { }",
        "while (x) ;", "while (x = 1) { }", "while (x) y = 1;", "for (I64 i = 0; i < n; i++) { }",
        "for (i = 0; i < n; i += 1) { }", "for (I64 i = 0; i < n; i++) ;", "for (I64 i; i < n; i++) { }",
        "for (I64 i = 0; i = n; i++) { }", "for (I64 i = 0, i < n; i++) { }", "for (I64 i = 0; i < n; i++ { }",
        "for i in 0..n { }", "for i in 0..=n { }", "for i in 0..n by 2 { }", "for i in n { }",
        "for i, x in a { }", "for i 0..n { }", "for I8 in 0..n { }", "for _ in 0..n { }", "for i in 0..n ;",
        "outer: for i in 0..n { break outer; }", "outer: while (x) { continue outer; }", "outer: x = 1;",
        "L: do { } while (x);", "do { } while (x);", "do { } while (x)", "do x; while (x);",
        "switch (x) { case 1: y = 1; }", "switch (x) { case 1, 2: y = 1; default: y = 2; }",
        "switch (x) { case 1..=3: y = 1; }", "switch (x) { case 1..3: y = 1; }", "switch (x) { case 1: }",
        "switch (x) { case 1: case 2: y = 1; }", "switch (x) { default: y = 1; default: y = 2; }",
        "switch (x) { default: y = 1; case 1: y = 2; }", "switch (x) { y = 1; }", "switch (x) { case 1: y = 1;",
        "switch x { case 1: y = 1; }", "switch (x) { case c ? 1 : 2: y = 1; }", "switch (x) { case (c ? 1 : 2): y = 1; }",
        "switch (x) { case 'a'..='z', '_': y = 1; }", "switch (x) { case -1: y = 1; }", "switch (x) { }",
        "switch (x) { case 1: { y = 1; } fallthrough; case 2: break; }", "break;", "continue;", "break x;",
        "return;", "return 1", "fallthrough;", "fallthrough", "assert(x);", "assert(x, \"m\");",
        "assert(x, \"m={x}\");", "assert(x, \"a\" \"b{y}\");", "assert(x, y);", "assert x;",
        "static_assert(1 == 1);", "static_assert(1 == 1, \"m\");", "static_assert(1 == 1, \"a\" \"b\");",
        "static_assert(1, 2);", "expect_fault E_BOUNDS;", "defer x = 1;", "defer { x = 1; }", "defer f(1);",
        "var x = 1;", "var (a, b) = f();", "in U8[_] s = t;", "inout I64[_] v = w;", "(I64 q, I64 r) = f();",
        "(I64 q, _) = f();", "(a, b) = f();", "(a, b) = 1, 2;", "try f(x);", "in_place k(a);",
        "reduce s = sum(x);", "kernel k;", "else { }", "case 1:", "import a;", "struct S { }", "test \"t\" { }",
        "\"hello\\n\";", "\"a\" \"b\";", "\"x={x}\\n\";", "\"{x}{y}\";", "\"{x=}\";", "\"{x = }\";",
        "\"{x:>5}\";", "\"{x:{{}}\";", "\"{x!bits}\";", "\"{x!raw}\";", "\"{x!ratio}\";", "\"{x!lanes(U8)}\";",
        "\"{x!lanes(I16)}\";", "\"{x!lanes(I128)}\";", "\"{x!lanes(U8 )}\";", "\"{x!rawx}\";", "\"{x!bits:>4}\";",
        "\"{x=!bits:x}\";", "\"{}\";", "\"{ }\";", "\"{x y}\";", "\"{x +}\";", "\"{a ? b : c}\";",
        "\"{(a ? b : c)}\";", "\"{f(1, 2)}\";", "\"{a[1]}\";", "\"{1__0}\";", "\"{x $}\";", "\"{$}\";",
        "\"{x:{y}}\";", "\"{x\";", "\"x}\";", "\"{{x}}\";", "\"{\\\"a\\\"}\";", "\"{x}\" \"{y}\";",
        "\"{x}\" \"\\q\";", "\"\\q{x}\";", "\"{x}\\q\";", "\"{-1}\";", "\"{x as U8}\";", "\"{x && y || z}\";",
        "\"{x.f}\";", "\"{I64.max}\";", "\"{'a'}\";", "\"{x == 1}\";", "\"{x + 1 + }\";", "\"{x\\ty}\";",
        "\"{ x }\";", "\"{x:}\";", "\"{x!}\";", "\"{x=:5}\";", "\"{é}\";", "\"é{x}\";", "\"{x}é\";",
        "\"{x} \" 1;", "\"x\" + 1;", "\"x\"", "b\"\\xff\";", "b\"{x}\";",
        # box 09: arrays of rank 2 to 4 (18.4 shape), indexes and slices (18.6 index), array
        # literals (18.6 primary), local views (18.3 var_decl), `reduce` (18.5 reduce_stmt)
        "I64[2, 3] m;", "I64[2, 3, 4, 5] m;", "I64[_, 3] m;", "I64[n, k] a;", "I64[2,] m;", "I64[2, 3 m;",
        "I64[2][3] m;", "I64 a = m[i, j];", "I64 a = m[i, j, k, l];", "m[i, j] = 1;", "m[i, j] += 1;",
        "m[i, j]++;", "f(m[i]);", "x = m[.., j];", "x = m[i, ..];", "x = m[.., ..];", "x = a[lo..];",
        "x = a[lo..hi];", "x = a[lo..=hi];", "x = a[lo..hi by k];", "x = a[hi..lo by -1];", "x = a[.. by 2];",
        "x = a[..n by 2];", "x = a[..=n by 2];", "x = a[lo.. by 2];", "x = m[1..3, 0..=1];", "x = a[1..2][0];",
        "x = a[..][..];", "x = (a[..]);", "x = a[lo..=];", "x = a[lo..=, 1];", "x = a[..=];", "x = a[1..2 by];",
        "x = a[1 by 2];", "x = a[];", "x = a[1,];", "x = a[,];", "x = a[1..2..3];", "x = a[..=1..3];",
        "x = a[.. by];", "x = a[..=n by 2 by 3];", "I64[3] a = [1, 2, 3];", "I64[2, 2] m = [[1, 2], [3, 4]];",
        "x = [1, 2,];", "x = [];", "x = [,];", "x = [1 2];", "x = [[1], [2, 3]];", "x = [-1, -2][0];",
        "x = ([1, 2])[0];", "x = [1, [2, [3]]];", "x = [1, 2", "f([1, 2], [[3]]);", "[1, 2];",
        "in I64[_, _] t = transpose(m);", "inout I64[_] r = a[1..3];", "in I64[_, _] t = m[.., 0..2];",
        "in I64[_] s;", "in I64[_] s = ;", "inout I64 x = y;", "in I8 = 1;", "in I64[_] s = a",
        "in in I64[_] s = a;", "in I64[_] I8 = a;", "in s = a;",
        "reduce s = fold_checked(add, 0, x[i]);", "reduce s = x;", "reduce s = max(x[i]);",
        "reduce s = sum(x) + 1;", "reduce s;", "reduce = sum(x);", "reduce s = ;", "reduce s = sum(x)",
        "reduce s == sum(x);", "reduce I8 = sum(x);", "reduce s.t = sum(x);", "reduce s[0] = sum(x);",
    ]


def lex_cases() -> list:
    """(name, bytes, key): whole source texts for the source-text checks and the lexer."""
    body = "export I64 run() {\n    I64 x = %s;\n    return x;\n}\n"
    lits = [
        "0X1F", "0B1", "0O7", "0T0", "0x", "0b", "0o", "0t", "0xG", "0b2", "0o8", "0tX", "1_", "1__2", "0_1",
        "00", "007", "12ab", "1e5", "0x1_", "0x_1", "1.5", "1.x", "1._5", "1.5_", "1.5__5", "0x1.8", "0x1.g",
        "0b1.1", "1..2", "1.5.5", "0", "9", "0x0", "0xDEAD_beef", "0b0", "0o0", "0tP", "0tN0P", "0tP_0",
        "0t_P", "0tPN", "Q16.16", "Q0.5", "Q16.", "Q16.x", "Q1x.5", "0x1.", "1.", "'a'", "''", "'ab'",
        "'\\q'", "'\\x41'", "'\\x80'", "'\\x7F'", "'\\x4'", "'\\xG'", "'\\u{41}'", "'\\u{D800}'",
        "'\\u{110000}'", "'\\u{10FFFF}'", "'\\u{}'", "'\\u{1234567}'", "'\\u{12g}'", "'\\u41'", "'\\'",
        "'a", "'\\0'", "'\\\\'", "'\\x41", "\"abc", "\"\\q\"", "\"\\x80\"", "\"\\u{D800}\"", "\"{x}\"", "\"}\"",
        "\"{{}}\"", "\"{\"", "\"\\u{41}\"", "b\"\\u{41}\"", "b\"\\xff\"", "x as% U8", "x as %U8", "x as | U8",
        "x $ 1", "x # 1", "x ` 1", "x \\ 1", "x é 1", "x·1",
        "0x1" + "0" * 1023, "0x1" + "0" * 1024, "0x" + "F" * 1024, "-0x" + "F" * 1024, "0x0" + "F" * 1024,
        "0x" + "0" * 2000 + "1", "0b1" + "0" * 4095, "0b1" + "0" * 4096, "0o1" + "0" * 1365, "0o2" + "0" * 1365,
        "0o7" + "0" * 1364, "9" * 1233, "1" + "0" * 1233, DEC_4096, str((1 << 4096) - 1), str((1 << 4096) + 1),
        "1" + "0" * 1234, "1_" + "0" * 1233, "0t" + balanced_ternary(1 << 4096),
        "0t" + balanced_ternary((1 << 4096) - 1), "0t" + balanced_ternary(-(1 << 4096)),
        "0t" + balanced_ternary(1 - (1 << 4096)), "0t0" + balanced_ternary(1 << 4096),
        "0t" + "P" + "0" * 2584, "0t" + "P" * 2585,
    ]
    out = [("lex/literal_%03d" % k, (body % lit).encode("utf-8"), lit) for k, lit in enumerate(lits)]
    raw = [
        b"\xef\xbb\xbfexport I64 run() { return 1; }\n", b"export I64 run() { return 1; } // \xff\n",
        b"I64 x = 1;\n\xc3\x28\n", b"I64 x = 1; // \xe2\x82\n", b"// \xc0\xaf\n", b"// \xed\xa0\x80\n",
        b"// \xf4\x90\x80\x80\n", b"// \x80\n", b"I64 x = 1;\x00\n", b"I64 x = 1;\x01\n", b"I64 x = 1;\x7f\n",
        b"// \xc2\x85\n", b"I64 x = 1;\rI64 y = 2;\n", b"I64 x = 1;\r\nI64 y = 2;\r\n", b"I64\tx = 1;\n",
        "// \u200b\n".encode("utf-8"), "I64 x = \"\u202e\";\n".encode("utf-8"), "I64 x = 1;\ufeff\n".encode("utf-8"),
        "// \U000E0001\n".encode("utf-8"), b"I64 x = 1; $ \x01\n", b"$\n\xff\n", b"/* \xff */ $\n",
        b"/* abc", b"/* /* */", b"/* /* */ */ I64 x = 1;", b"I64 x = 1; /* a */ /* b", b"I64 x = 1; //",
        b"// only a comment", b"", b"\n\n\n", b"   ", b"\"abc", b"I64 x = \"a\nb\";", b"I64 x = \"a\\\nb\";",
        b"I64 x = 'a\n';", b"I64 x = 'a", ("I64 " + "a" * 255 + " = 1;").encode(),
        ("I64 " + "a" * 256 + " = 1;").encode(), ("I64 " + "_" * 300 + " = 1;").encode(),
        "I64 x = 1;\n\"\u00e9\" + x;\n".encode("utf-8"), "I64 \u00e9 = 1;\n".encode("utf-8"),
        "I64 x = 1; // \u00e9\u00e9\n I64 y = $;\n".encode("utf-8"),
        "\"\u00e9\u00e9\u00e9\" $\n".encode("utf-8"), "I64 x = '\u00e9' $ 1;\n".encode("utf-8"),
        b"I64 x = 1;\r\n\tI64 y = $;\r\n", b"I64 x = 1 \\\n", b"I64 x = 1;\r", b"x\r\n\r\n$",
    ]
    out += [("lex/raw_%03d" % k, data, None) for k, data in enumerate(raw)]
    return out


def module_cases() -> list:
    """Module-level texts."""
    return [
        "import a;\nimport a.b;\nimport a.b as c;\nimport a.{X, y};\nimport a.{X,};\nexport I64 run() { return 1; }\n",
        "profile \"cint-core-1\";\nimport a;\n", "profile \"other\";\n", "profile x;\n", "profile \"a\" \"b\";\n",
        "profile \"{x}\";\n", "I64 x = 1;\nimport a;\n", "import a;\nprofile \"cint-core-1\";\n", "import ;\n",
        "import a.;\n", "import a as I8;\n", "import a.{};\n", "import a.{X\n", "import a.b.{X} as c;\n",
        "import a\n", "export test \"t\" { }\n", "export static_assert(true);\n", "export x = 1;\n",
        "export I64 x = 1;\n", "export const I64 K = 1;\n", "export struct S { I64 a; }\n",
        "struct S { I64 a; U8[4] b; vec.V3 c; }\n", "struct S { }\n", "struct S { I64 a }\n", "struct S { a; }\n",
        "struct S { I64 I8; }\n", "struct I8 { }\n", "struct S { I64 a; ", "struct S I64 a; }\n",
        "struct S { I64 a : 3; }\n", "struct S { I64 a = 1; }\n", "struct S { @packed I64 a; }\n",
        "@packed struct S { I64 a; }\n", "@align(8) @x(a = 1, 2) export struct S { I64 a; }\n", "@ struct S { }\n",
        "@packed I64 x = 1;\n", "@packed x = 1;\n",
        "void f() { }\n", "void f(I64 a, U8[_] b, inout I64[n] c) { }\n", "I64 f[n](in I64[n] a) { return 0; }\n",
        "I64 f[n, m](in I64[n] a, inout I64[m] b,) { return 0; }\n", "I64 f(out I64 a) { return 0; }\n",
        "I64 f(I64 a = 1) { return 0; }\n", "I64 f(I64 a) where a > 0 { return 0; }\n", "I64 f() return 0;\n",
        "I64 f( { }\n", "I64 f(I64) { }\n", "I64 f(I64 a I64 b) { }\n", "I64 I8() { }\n", "I64 f[I8]() { }\n",
        "I64 f(I64 Q16) { }\n", "I64 f[]() { }\n", "U8[4] f() { }\n", "in U8[_] f() { }\n", "inout I64 x = y;\n",
        "in U8[_] s = t;\n", "E!I64 f() { }\n", "E!void f() { }\n", "m.E!I64 f() { }\n", "(I64, I64) f() { }\n",
        "(I64, I64) split(I64 a, I64 b) { return (a / b, a % b); }\n", "void f() { (I64 q, I64 r) = split(7, 2); }\n",
        "const I64 K = 1;\n", "const I64 K = 1 + 2 * 3;\n", "const I64 K;\n", "const I64 I8 = 1;\n",
        "test \"t\" { }\n", "test \"t\" expect_fault E_BOUNDS { }\n", "test \"t\" expect_fault E_BOUNDS at 12 { }\n",
        "test \"t\" expect_fault E_NOPE { }\n", "test \"t\" expect_fault E_BOUNDS at 0x5 { }\n",
        "test \"t\" expect_fault E_BOUNDS at x { }\n", "test \"t\" expect_fault { }\n", "test t { }\n",
        "test \"t\"\n", "test \"{x}\" { }\n", "test \"a\" \"b\" { }\n", "test \"t\" expect_fault E_FUEL at 1_0 { }\n",
        "static_assert(true);\n", "static_assert(1 == 1, \"why\");\n", "static_assert(true)\n",
        "enum Mode : U8 { idle, burn = 4, coast }\n", "enum Mode : U8 { idle, }\n", "enum Mode { idle }\n",
        "enum Mode : X { idle }\n", "enum Mode : U8 { }\n", "error E { a, b }\n", "error E : U8 { a, }\n",
        "error E { }\n", "type C = I64;\n", "type C = distinct I64;\n", "type C = I64\n", "type C I64;\n",
        "kernel k[n](in I64[n] u, out I64[n] v) over [i: n] { v[i] = u[i]; }\n",
        "kernel k[n](in I64[n] u) over [i: 0..n, j: _] where n > 0, n < 9 { }\n", "kernel k(in I64 u) { }\n",
        "kernel k[n](I64[n] u) { }\n", "schedule s for k { tile(i = 8); unroll(4); }\n", "schedule s { }\n",
        "schedule s { tile() }\n", "x = 1;\n\"x={x}\\n\";\n", "I64 x = 1;\nI64 y = x + 1;\n", "x\n", "}\n", "{\n",
        "Q16.16 h = r as Q16.16;\n", "I64 x = h as I64 round floor;\n", "I64 x = half(0.5);\n",
        "I64 x = 0.5 round nearest;\n", "Pool(I64) p = q;\n", "Handle(Pool(I64)) h = q;\n", "Str s = \"a\";\n",
        "T27 t = x;\n", "I64 d = digit('7') catch 0;\n", "I64 d = f() catch (e) { return 1; };\n",
        "I64 r = .bad;\n", "Mode m = Mode.coast;\n", "I64 b = Body.bytes;\n",
        "void f() { defer log[0] = 7; defer { x = 1; } }\n",
        "export I64 run() { I8 x = -(128) as I8; return x as I64; }\n",
        # box 09: kernels (18.3 kernel_decl, SPEC-02 K-1, the element form of K-12), arrays and views
        "kernel k[n](in I64[n] u, out I64[n] v) { v = u; }\n",
        "kernel k[n](in I64[n] u, out I64[n] v) over [i: n, j: _] where n > 0, n < 9 { v[i] = u[i]; }\n",
        "export kernel k[n](inout I64[n, n] m) over [i: n, j: n] { m[i, j] = 0; }\n",
        "kernel k[n](in I64[n] u, out I64 s) over [i: n] { reduce s = fold_checked(add, 0, u[i]); }\n",
        "kernel k[n](in I64[n] u) over [i: 0..=n] { }\n", "kernel k[n](in I64[n] u) over [i: n..] { }\n",
        "kernel k[n](in I64[n] u) over [i: ..n] { }\n", "kernel k[n](in I64[n] u) over [I8: n] { }\n",
        "kernel k[n](in I64[n] u) over [i n] { }\n", "kernel k[n](in I64[n] u) over [] { }\n",
        "kernel k[n](in I64[n] u) over [i: n,] { }\n", "kernel k[n](in I64[n] u) over i: n { }\n",
        "kernel k[n](in I64[n] u) over [i: (_)] { }\n", "kernel k[n](in I64[n] u) over [i: _ + 1] { }\n",
        "kernel k[n](in I64[n] u) over [i: _..=n] { }\n", "kernel k[n](in I64[n] u) where { }\n",
        "kernel k[n](in I64[n] u) where n > 0, { }\n", "kernel k[n](in I64[n] u) where n > 0 n < 9 { }\n",
        "kernel k[n](in I64[n] u = 1) { }\n", "kernel k[n](in I64[n] u,) { }\n", "kernel k[n]() { }\n",
        "kernel k[](in I64 u) { }\n", "kernel k[I8](in I64 u) { }\n", "kernel I8[n](in I64 u) { }\n",
        "kernel k[n](in I64[n] u) over [i: n] where n > 0\n", "kernel k[n](in I64[n] u) over [i: n]\n",
        "kernel k[n](in I64 I8) { }\n",
        "I64[2, 3] g;\n", "const I64[2] K = [1, 2];\n", "export in I64[_] v = w;\n",
    ]


# Outcomes for written cases that `cint_ref` refuses (it does not decide them), or where it
# reports an error for text that SPEC-04 18 admits (compiler/OPEN.md CINTC-OQ-06): the
# outcome SPEC-04 18 gives, as ("error", code, line, column) or ("accept",). Keyed by the
# case's text (a statement, a literal, or a module). A refused case not listed must parse.
EXPECT = {
    # SPEC-04 18.6 primary: a fraction with a rounding clause; 18.4 elem_type: Pool(T),
    # Handle(T). A slice with no low bound (`a[..]`, `m[.., j]`) is box 09 text, and an error
    # union as a function's result and an error set are box 12 text, which `cint_ref` parses
    # (front_ref.py), so their cases expect the reference's tree.
    "I64 x = 0.5 round nearest;\n": ("accept",),
    "Pool(I64) p = q;\n": ("accept",),
    "Handle(Pool(I64)) h = q;\n": ("accept",),
    # Refused by cint_ref; the error of SPEC-04 18 (C1043 for a `do` body, D-4).
    "do { } while (x)": ("error", 1050, 3, 5),
    "do x; while (x);": ("error", 1043, 2, 8),
    "(a, b) = 1, 2;": ("error", 1050, 2, 15),
    "kernel k;": ("error", 1050, 2, 5),
    "I64[2][3] m;": ("error", 1050, 2, 11),   # 18.4 type: one shape (cint_ref: an array of arrays)
    'b"\\xff";': ("error", 4012, 2, 5),
    'b"{x}";': ("error", 1033, 2, 7),
    'b"\\u{41}"': ("error", 1032, 2, 15),
    "@ struct S { }\n": ("error", 1050, 1, 3),
    "@packed x = 1;\n": ("error", 1050, 1, 9),
    "enum Mode { idle }\n": ("error", 1050, 1, 11),
    "enum Mode : X { idle }\n": ("error", 1050, 1, 13),
    "enum Mode : U8 { }\n": ("error", 1050, 1, 18),
    "type C = I64\n": ("error", 1050, 2, 1),
    "type C I64;\n": ("error", 1050, 1, 8),
    "schedule s { tile() }\n": ("error", 1050, 1, 21),
}


def written() -> list:
    """(name, bytes, expected outcome or None for `cint_ref`'s)."""
    out = [("stmt/%03d" % k, fn("    " + s), EXPECT.get(s)) for k, s in enumerate(stmt_cases())]
    out += [(name, data, EXPECT.get(text)) for name, data, text in lex_cases()]
    out += [("module/%03d" % k, s.encode("utf-8"), EXPECT.get(s)) for k, s in enumerate(module_cases())]
    nests = [("nest/blocks_%d" % d, fn("{" * d + "x = 1;" + "}" * d)) for d in (255, 256, 257, 300)]
    nests += [("nest/whiles_%d" % d, fn("while (x) {" * d + "}" * d)) for d in (256, 257)]
    nests += [("nest/parens_%d" % d, fn("x = " + "(" * d + "1" + ")" * d + ";")) for d in (200, 256)]
    nests += [("nest/unary_%d" % d, fn("x = " + "-" * d + "x;")) for d in (200, 256)]
    nests += [("nest/calls_%d" % d, fn("x = " + "f(" * d + "1" + ")" * d + ";")) for d in (200, 256)]
    nests += [("nest/sums_%d" % d, fn("x = " + "a + (" * d + "1" + ")" * d + ";")) for d in (200, 256)]
    nests += [("nest/index_%d" % d, fn("x = " + "a[" * d + "1" + "]" * d + ";")) for d in (200, 256)]
    return out + [(name, data, None) for name, data in nests] + multi()


MULTI = {
    # One lexical error per line: the scanner reports each (SPEC-04 LS-282, decision patch D-11).
    "multi/lex": ["I64 a = 0X1;", "I64 b = 1__2;", "I64 c = 'ab';", "x = $ 1;", "I64 e = '\\q';", "I64 f = 0x;",
                  "I64 g = é;", "I64 h = 007;", "I64 i = 0b102;", "I64 j = 1.5_;", "/* unterminated"],
    # Source-text errors (C1002 to C1004) on several lines.
    "multi/text": ["I64 a = 1;\x01", "// ​", "I64 b = 2;", "x\x7f", "I64 c = 3;\x00", "y\rz"],
    # Source-text errors come first: the lexical errors of the other lines are not reported.
    "multi/mixed": ["I64 a = 0X1;", "// ‮", "x = $;", "I64 b = 1;\x02"],
}


def multi() -> list:
    """(name, bytes, ("errors", [[code, line, column], ...])): files with an error on several
    lines, whose expected diagnostics are `cint_ref`'s for each line alone, those of the
    source-text check if any line has one (D-11: the earliest phase)."""
    import front_ref
    out = []
    for name, lines in MULTI.items():
        rows = []
        for k, line in enumerate(lines):
            want = front_ref.outcome((line + "\n").encode("utf-8"), "case.ci")
            if want[0] == "error":
                rows.append([want[1], k + 1, want[3]])
        text_rows = [r for r in rows if r[0] in (1002, 1003, 1004)]
        out.append((name, ("\n".join(lines) + "\n").encode("utf-8"), ("errors", text_rows or rows)))
    return out


def chain(kind: str, arms: int, minimal: bool) -> bytes:
    """An `else if` chain or a switch of `arms` arms (SPEC-09 CINTC-03)."""
    lines = ["export I64 run(I64 x) {", "    I64 y = 0;"]
    if kind == "if":
        for k in range(arms):
            head = "    if" if k == 0 else "    } else if"
            lines.append("%s (x == %d) {" % (head, k))
            lines.append("        y = %d;" % k if not minimal else "        y = 1;")
        lines.append("    }")
    else:
        lines.append("    switch (x) {")
        for k in range(arms):
            lines.append("        case %d: y = %d;" % (k, k) if not minimal else "        case %d: y = 1;" % k)
        lines.append("        default: y = -1;")
        lines.append("    }")
    lines += ["    return y;", "}", ""]
    return "\n".join(lines).encode("ascii")


CHAIN_ARMS = (1, 2, 10, 100, 1000, 5000)


def chains() -> list:
    return [("chain/%s_%s_%d" % (kind, "minimal" if minimal else "realistic", arms), chain(kind, arms, minimal))
            for kind in ("if", "switch") for minimal in (False, True) for arms in CHAIN_ARMS]


VOCAB = [";", "(", ")", "{", "}", "[", "]", ",", "=", "==", "-", "+", "*", "/", "as", "as%", "if", "else",
         "x", "1", "'a'", "\"s\"", "..", "..=", ":", "?", "&&", "||", "&", "|", "^", "<<", "<", "++", "case",
         "default", "return", "for", "while", "in", "I64", "U8", ".", "_", "const", "struct", "export",
         "import", "@", "!", "~", "-1", "0x", "1__0", "0X1", "01", "'ab'", "\"{x}\"", "/*", "$", "\u00e9",
         "break", "switch", "fallthrough", "assert", "test", "void", "inout", "out", "try", "do", "var",
         "\"a{\"", "}}", "{{", "->", "::", "#", "\\", "'", "\""]


def tokens(text: str) -> list:
    """(start, end) character spans of the tokens of `text` by cint_ref's lexer, or [] when it
    does not lex."""
    import front_ref  # noqa: F401  (puts ref/ on the path)
    from cint_ref import lexer
    from cint_ref.faults import CompileError
    try:
        toks = lexer.tokenize(text.replace("\r\n", "\n"), "m.ci")
    except CompileError:
        return []
    return [(t.offset, t.end) for t in toks if t.kind != "eof"]


def mutants(per_file: int, seed: int) -> list:
    """Seeded single-token and single-character mutations of the frozen sources."""
    rng = random.Random(seed)
    out = []
    for name, data in frozen():
        try:
            text = data.decode("utf-8").replace("\r\n", "\n")
        except UnicodeDecodeError:
            continue
        spans = tokens(text)
        if not spans:
            continue
        for k in range(per_file):
            how = rng.randrange(6)
            a, b = spans[rng.randrange(len(spans))]
            if how == 0:
                new = text[:a] + text[b:]
            elif how == 1:
                new = text[:b] + " " + text[a:b] + text[b:]
            elif how == 2:
                new = text[:a] + rng.choice(VOCAB) + text[b:]
            elif how == 3:
                new = text[:a] + rng.choice(VOCAB) + " " + text[a:]
            elif how == 4:
                c = rng.randrange(len(text))
                new = text[:c] + text[c + 1:]
            else:
                c = rng.randrange(len(text) + 1)
                new = text[:c] + rng.choice(VOCAB) + text[c:]
            out.append(("mutant/%s/%d" % (name, k), new.encode("utf-8")))
    return out
