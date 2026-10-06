import os, sys; sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # put ref/ on the path
"""Lexer tests: SPEC-04 section 3 and SPEC-09 DIAG-01."""
import unittest

from cint_ref.faults import CompileError
from cint_ref.lexer import decode_source, tokenize, string_parts, Hole


def toks(text):
    return [t for t in tokenize(text, "t.ci") if t.kind != "eof"]


def kinds_texts(text):
    return [(t.kind, t.text) for t in toks(text)]


def code_of(fn):
    try:
        fn()
    except CompileError as e:
        return e.diagnostic.code, (e.diagnostic.position.line, e.diagnostic.position.column)
    raise AssertionError("no compile error")


def decode(data):
    return decode_source(data, "t.ci")


class Tokens(unittest.TestCase):
    def test_declaration(self):
        ts = toks("I64 a = 1_000;")
        self.assertEqual([(t.kind, t.text) for t in ts],
                         [("ident", "I64"), ("ident", "a"), ("op", "="), ("int", "1_000"), ("op", ";")])
        self.assertEqual(ts[3].value, 1000)
        self.assertEqual([(t.line, t.col) for t in ts], [(1, 1), (1, 5), (1, 7), (1, 9), (1, 14)])

    def test_maximal_munch_3_2(self):
        self.assertEqual([t for _, t in kinds_texts("a+%b")], ["a", "+%", "b"])
        self.assertEqual([t for _, t in kinds_texts("x<<%2")], ["x", "<<%", "2"])
        self.assertEqual([t for _, t in kinds_texts("x <<%= 1")], ["x", "<<%=", "1"])
        self.assertEqual([t for _, t in kinds_texts("a +| b -| c *| d")], ["a", "+|", "b", "-|", "c", "*|", "d"])
        self.assertEqual([t for _, t in kinds_texts("x as% U8")], ["x", "as%", "U8"])
        self.assertEqual([t for _, t in kinds_texts("i..=n")], ["i", "..=", "n"])

    def test_range_after_integer_3_6(self):
        self.assertEqual(kinds_texts("1..5"), [("int", "1"), ("op", ".."), ("int", "5")])
        self.assertEqual(kinds_texts("0x1..5"), [("int", "0x1"), ("op", ".."), ("int", "5")])
        self.assertEqual(kinds_texts("1.5")[0][0], "frac")

    def test_literal_values_3_5(self):
        cases = {"1_000_000": 1000000, "0xDEAD_beef": 3735928559, "0b1010_0101": 165,
                 "0o755": 493, "0tP0N": 8, "0tN": -1, "0": 0, "0xFFFF_FFFF_FFFF_FFFF": 18446744073709551615}
        for text, value in cases.items():
            t = toks(text)
            self.assertEqual((len(t), t[0].kind, t[0].value), (1, "int", value), text)

    def test_character_literals_3_7(self):
        cases = {"'A'": 65, "'é'": 233, "'\\u{1F600}'": 128512, "'\\n'": 10,
                 "'\\''": 39, "'\\\\'": 92, "'a'": 97}
        for text, value in cases.items():
            t = toks(text)
            self.assertEqual((t[0].kind, t[0].value), ("char", value), text)

    def test_keywords(self):
        self.assertEqual(kinds_texts("while round test")[0], ("keyword", "while"))
        self.assertEqual(kinds_texts("while round test")[1], ("ident", "round"))   # contextual keyword
        self.assertEqual(kinds_texts("while round test")[2], ("keyword", "test"))

    def test_fixed_type_is_one_token_3_6(self):
        self.assertEqual(kinds_texts("Q16.16 x"), [("fixed_type", "Q16.16"), ("ident", "x")])

    def test_comments(self):
        self.assertEqual(kinds_texts("a // c\nb"), [("ident", "a"), ("ident", "b")])
        self.assertEqual(kinds_texts("a /* x /* y */ z */ b"), [("ident", "a"), ("ident", "b")])
        self.assertEqual(code_of(lambda: toks("a /* x")), ("C1005", (1, 3)))

    def test_space_before_flag(self):
        ts = toks("-1 - 1")
        self.assertEqual([t.space_before for t in ts], [False, False, True, True])


class LiteralErrors(unittest.TestCase):
    def test_codes(self):
        self.assertEqual(code_of(lambda: toks("x = 0X1F;")), ("C1020", (1, 5)))
        self.assertEqual(code_of(lambda: toks("1__0")), ("C1021", (1, 1)))
        self.assertEqual(code_of(lambda: toks("1_")), ("C1021", (1, 1)))
        self.assertEqual(code_of(lambda: toks("0x_1F")), ("C1021", (1, 1)))
        self.assertEqual(code_of(lambda: toks("  007")), ("C1022", (1, 3)))
        self.assertEqual(code_of(lambda: toks("''")), ("C1030", (1, 1)))
        self.assertEqual(code_of(lambda: toks("'ab'")), ("C1030", (1, 1)))
        self.assertEqual(code_of(lambda: toks("'\\u{D800}'"))[0], "C1031")
        self.assertEqual(code_of(lambda: toks("'\\q'"))[0], "C1032")
        self.assertEqual(code_of(lambda: toks('"abc\ndef"'))[0], "C1034")

    def test_identifier_after_underscore_prefix(self):
        self.assertEqual(kinds_texts("_1"), [("ident", "_1")])


class SourceText(unittest.TestCase):
    """SPEC-04 3.1 and 3.2; plan Review Focus 1."""

    def test_bom_is_a_diagnostic(self):
        self.assertEqual(code_of(lambda: decode(b"\xef\xbb\xbfI64 a = 1;\n")), ("C1002", (1, 1)))

    def test_invalid_utf8_and_nul(self):
        self.assertEqual(code_of(lambda: decode(b"I64 a = 1; // \xff\n"))[0], "C1002")
        self.assertEqual(code_of(lambda: decode(b"I64 a\x00 = 1;\n")), ("C1002", (1, 6)))

    def test_line_ends(self):
        self.assertEqual(code_of(lambda: decode(b"a\rb")), ("C1004", (1, 2)))
        self.assertEqual(code_of(lambda: decode(b"a\x01b")), ("C1004", (1, 2)))
        self.assertEqual(decode(b"a\tb\r\nc"), "a\tb\nc")

    def test_forbidden_invisible_and_bidi_characters(self):
        self.assertEqual(code_of(lambda: decode("// ‮\n".encode())), ("C1003", (1, 4)))
        self.assertEqual(code_of(lambda: decode('x = "a​b";'.encode())), ("C1003", (1, 7)))
        self.assertEqual(code_of(lambda: decode("a b".encode())), ("C1003", (1, 2)))

    def test_crlf_positions_equal_lf_positions(self):
        lf = b"I64 f() {\n    I64 a = 1;\n\treturn a + 2;\n}\n"
        crlf = lf.replace(b"\n", b"\r\n")
        no_final = lf[:-1]
        def positions(data):
            return [(t.text, t.line, t.col) for t in tokenize(decode(data), "t.ci")]
        self.assertEqual(positions(lf), positions(crlf))
        self.assertEqual(positions(lf)[:-1], positions(no_final)[:-1])
        self.assertIn(("+", 3, 11), positions(crlf))   # a tab counts as one column

    def test_columns_count_scalar_values_diag_01(self):
        text = decode('"é" + x'.encode("utf-8"))
        plus = [t for t in tokenize(text, "t.ci") if t.text == "+"][0]
        self.assertEqual((plus.line, plus.col), (1, 5))


class Strings(unittest.TestCase):
    def test_escapes_and_braces(self):
        (t,) = toks(r'"a\n{{b}}\u{e9}\x41"')
        parts = string_parts(t, allow_holes=False)
        self.assertEqual(parts, [b"a\n{b}\xc3\xa9A"])

    def test_holes(self):
        (t,) = toks('"x={x} y={a + 1 =}:{n:>6}"')
        parts = string_parts(t, allow_holes=True)
        self.assertEqual(parts[0], b"x=")
        self.assertIsInstance(parts[1], Hole)
        self.assertEqual(parts[1].text, "x")
        self.assertEqual((parts[1].line, parts[1].col), (1, 5))
        self.assertEqual(parts[3].text, "a + 1 =")
        self.assertEqual(parts[5].text, "n:>6")

    def test_hole_outside_format_context_c1033(self):
        (t,) = toks('"name {x}"')
        self.assertEqual(code_of(lambda: string_parts(t, allow_holes=False)), ("C1033", (1, 7)))

    def test_bad_escapes(self):
        (t,) = toks(r'"\xff"')
        self.assertEqual(code_of(lambda: string_parts(t, allow_holes=False))[0], "C1032")
        (t,) = toks(r'"\u{DFFF}"')
        self.assertEqual(code_of(lambda: string_parts(t, allow_holes=False))[0], "C1031")


if __name__ == "__main__":
    unittest.main()
