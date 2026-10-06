"""Source text and tokens (SPEC-04 section 3; positions per SPEC-09 DIAG-01).

`decode_source` validates the bytes of a `.ci` file (UTF-8 without a BOM,
forbidden characters, line ends) and returns the text with each CR LF replaced
by LF, so that CR LF sources give exactly the positions of LF sources.
`tokenize` produces tokens with 1-based line and column, where the column
counts Unicode scalar values. Strings are returned raw; `string_parts`
decodes their escapes and splits out interpolation holes.
"""
from __future__ import annotations

from dataclasses import dataclass
from fractions import Fraction

from .faults import Position, error

KEYWORDS = frozenset("""
as assert break by case catch const continue default defer do else enum error
expect_fault export fallthrough false for if import in in_place inout kernel out
over profile reduce return schedule static_assert struct switch test true try
type var void where while distinct secret
""".split())
# `errdefer` is reserved with the box 12 surface (SPEC-04 LS-18, LS-189; decision 2026-10-05,
# BX12-08); parser.parse_module(box12=False) lexes with KEYWORDS, as before box 12.
BOX12_KEYWORDS = KEYWORDS | {"errdefer"}

# Longest first, so that the first match is the maximal munch (SPEC-04 3.2).
OPERATORS = sorted("""
<<%= <<= >>= +%= -%= *%= +|= -|= *|= ..= <<% << >> +% -% *% +| -| *| == != <= >=
&& || ++ -- += -= *= /= %= &= |= ^= .. + - * / % < > & | ^ ~ ! ? : = ( ) [ ] { } , ; . @
""".split(), key=len, reverse=True)

# Default_Ignorable_Code_Point (Unicode DerivedCoreProperties), bidirectional
# controls and line/paragraph separators: C1003 (SPEC-04 3.1).
_FORBIDDEN_RANGES = (
    (0x00AD, 0x00AD), (0x034F, 0x034F), (0x061C, 0x061C), (0x115F, 0x1160),
    (0x17B4, 0x17B5), (0x180B, 0x180F), (0x200B, 0x200F), (0x2028, 0x2029),
    (0x202A, 0x202E), (0x2060, 0x206F), (0x3164, 0x3164), (0xFE00, 0xFE0F),
    (0xFEFF, 0xFEFF), (0xFFA0, 0xFFA0), (0xFFF0, 0xFFF8), (0x1BCA0, 0x1BCA3),
    (0x1D173, 0x1D17A), (0xE0000, 0xE0FFF),
)

MAX_LITERAL_BITS = 4096   # SPEC-04 3.5 rule 7


def _forbidden(cp: int) -> bool:
    return any(lo <= cp <= hi for lo, hi in _FORBIDDEN_RANGES)


def _is_control(cp: int) -> bool:
    return cp < 0x20 or 0x7F <= cp <= 0x9F


def decode_source(data: bytes, path: str) -> str:
    """Apply the source-text checks of SPEC-04 3.1 and 3.2; return the text with LF line ends.

    When the file has several source-text errors, the one with the smallest
    (line, column) is reported (SPEC-04 17.1; ref/OPEN.md REF-OQ-11): the valid
    text before an invalid UTF-8 sequence is scanned before C1002 is reported.
    """
    if data.startswith(b"\xef\xbb\xbf"):
        error("C1002", Position(path, 1, 1), "source text begins with a UTF-8 byte-order mark")
    try:
        text = data.decode("utf-8", errors="strict")
    except UnicodeDecodeError as e:
        prefix = data[:e.start].decode("utf-8", errors="strict")
        line, col = _scan_source(prefix, path)
        error("C1002", Position(path, line, col), "invalid UTF-8 sequence")
    _scan_source(text, path)
    return text.replace("\r\n", "\n")


def _scan_source(text: str, path: str):
    """Raise the first C1002, C1003 or C1004 error of `text`; else return the position after it.

    A CR that ends `text` is not followed by a line feed, which is right both at
    the end of the file and before an invalid UTF-8 sequence.
    """
    line, col = 1, 1
    n = len(text)
    i = 0
    while i < n:
        ch = text[i]
        cp = ord(ch)
        if cp == 0:
            error("C1002", Position(path, line, col), "byte 0x00 in source text")
        if ch == "\n":
            line, col = line + 1, 1
            i += 1
            continue
        if ch == "\r":
            if i + 1 < n and text[i + 1] == "\n":
                line, col = line + 1, 1
                i += 2
                continue
            error("C1004", Position(path, line, col), "carriage return not followed by line feed")
        if ch != "\t" and _is_control(cp):
            error("C1004", Position(path, line, col), "control character U+%04X" % cp)
        if _forbidden(cp):
            error("C1003", Position(path, line, col), "forbidden invisible or bidirectional character U+%04X" % cp)
        col += 1
        i += 1
    return line, col


@dataclass
class Token:
    kind: str          # ident keyword int frac char string bytes fixed_type op eof
    text: str
    value: object
    line: int
    col: int
    offset: int        # index into the (normalized) text
    end: int
    space_before: bool
    path: str = ""

    def pos(self) -> Position:
        return Position(self.path, self.line, self.col)


class Lexer:
    """Tokenizes `text`; `line`, `col` and `offset` locate it in its file."""

    def __init__(self, text: str, path: str, line: int = 1, col: int = 1, keywords=KEYWORDS):
        self.text = text
        self.path = path
        self.i = 0
        self.line = line
        self.col = col
        self.keywords = keywords

    # -- position helpers ------------------------------------------------
    def _pos(self) -> Position:
        return Position(self.path, self.line, self.col)

    def _advance(self, k: int = 1):
        for _ in range(k):
            if self.text[self.i] == "\n":
                self.line, self.col = self.line + 1, 1
            else:
                self.col += 1
            self.i += 1

    def _peek(self, k: int = 0) -> str:
        j = self.i + k
        return self.text[j] if j < len(self.text) else ""

    # -- whitespace and comments -----------------------------------------
    def _skip(self) -> bool:
        skipped = False
        while self.i < len(self.text):
            ch = self._peek()
            if ch in " \t\n":
                self._advance()
                skipped = True
            elif ch == "/" and self._peek(1) == "/":
                while self.i < len(self.text) and self._peek() != "\n":
                    self._advance()
                skipped = True
            elif ch == "/" and self._peek(1) == "*":
                start = self._pos()
                depth = 0
                while True:
                    if self.i >= len(self.text):
                        error("C1005", start, "unterminated block comment")
                    if self._peek() == "/" and self._peek(1) == "*":
                        depth += 1
                        self._advance(2)
                    elif self._peek() == "*" and self._peek(1) == "/":
                        depth -= 1
                        self._advance(2)
                        if depth == 0:
                            break
                    else:
                        self._advance()
                skipped = True
            else:
                break
        return skipped

    # -- tokens ------------------------------------------------------------
    def next_token(self) -> Token:
        space = self._skip()
        line, col, start = self.line, self.col, self.i
        if self.i >= len(self.text):
            return Token("eof", "", None, line, col, start, start, space)
        ch = self._peek()
        pos = self._pos()
        if ch.isascii() and (ch.isalpha() or ch == "_"):
            if ch == "b" and self._peek(1) == '"':
                self._advance()
                raw = self._string_body(pos)
                return Token("bytes", self.text[start:self.i], raw, line, col, start, self.i, space)
            while self.i < len(self.text) and (self._peek().isascii() and (self._peek().isalnum() or self._peek() == "_")):
                self._advance()
            word = self.text[start:self.i]
            if word == "as" and self._peek() in ("%", "|", "?"):
                self._advance()
                word = self.text[start:self.i]
                return Token("op", word, None, line, col, start, self.i, space)
            if (len(word) >= 2 and word[0] == "Q" and word[1:].isdigit() and word[1] != "0"
                    and self._peek() == "." and self._peek(1).isdigit()):
                self._advance()
                while self._peek().isdigit():
                    self._advance()
                return Token("fixed_type", self.text[start:self.i], None, line, col, start, self.i, space)
            if len(word.encode()) > 255:
                error("C1012", pos, "identifier longer than 255 bytes")
            kind = "keyword" if word in self.keywords else "ident"
            return Token(kind, word, None, line, col, start, self.i, space)
        if ch.isdigit():
            return self._number(pos, space)
        if ch == "'":
            return self._char(pos, space)
        if ch == '"':
            raw = self._string_body(pos)
            return Token("string", self.text[start:self.i], raw, line, col, start, self.i, space)
        for op in OPERATORS:
            if self.text.startswith(op, self.i):
                self._advance(len(op))
                return Token("op", op, None, line, col, start, self.i, space)
        if not ch.isascii():
            error("C1013", pos, "non-ASCII character U+%04X outside a literal or comment" % ord(ch))
        error("C1050", pos, "unexpected character %r" % ch)

    def _number(self, pos: Position, space: bool) -> Token:
        start, line, col = self.i, self.line, self.col
        while self.i < len(self.text) and self._peek().isascii() and (self._peek().isalnum() or self._peek() == "_"):
            self._advance()
        word = self.text[start:self.i]
        value = _int_value(word, pos)
        # A fraction needs a digit (hex digit after 0x) right after the point (SPEC-04 3.6).
        is_hex = word.startswith("0x")
        if (self._peek() == "." and self._peek(1) != "" and
                (self._peek(1) in "0123456789abcdefABCDEF" if is_hex else self._peek(1).isdigit())
                and (is_hex or not word.startswith("0") or word == "0")
                and not word.startswith(("0b", "0o", "0t"))):
            self._advance()
            fstart = self.i
            while self.i < len(self.text) and self._peek().isascii() and (self._peek().isalnum() or self._peek() == "_"):
                self._advance()
            frac_text = self.text[fstart:self.i]
            digits = _check_digits(frac_text, "0123456789abcdefABCDEF" if is_hex else "0123456789", pos)
            base = 16 if is_hex else 10
            frac = Fraction(value) + Fraction(int(digits, base), base ** len(digits))
            return Token("frac", self.text[start:self.i], frac, line, col, start, self.i, space)
        return Token("int", word, value, line, col, start, self.i, space)

    def _char(self, pos: Position, space: bool) -> Token:
        start, line, col = self.i, self.line, self.col
        self._advance()
        scalars = []
        while True:
            ch = self._peek()
            if ch == "" or ch == "\n":
                error("C1038", pos, "unterminated character literal")
            if ch == "'":
                self._advance()
                break
            if ch == "\\":
                esc_pos = self._pos()
                body = self._escape_text()
                scalars.append(_escape_value(body, esc_pos, in_str=True))
            else:
                scalars.append(ord(ch))
                self._advance()
        if len(scalars) != 1:
            error("C1030", pos, "a character literal holds exactly one Unicode scalar value")
        return Token("char", self.text[start:self.i], scalars[0], line, col, start, self.i, space)

    def _escape_text(self) -> str:
        """Consume one backslash sequence and return its text (validated later)."""
        start = self.i
        self._advance()                    # the backslash
        ch = self._peek()
        if ch == "x":
            self._advance()
            for _ in range(2):
                if self._peek() and self._peek() in "0123456789abcdefABCDEF":
                    self._advance()
        elif ch == "u" and self._peek(1) == "{":
            self._advance(2)
            while self._peek() and self._peek() not in "}'\"\n":
                self._advance()
            if self._peek() == "}":
                self._advance()
        elif ch and ch != "\n":
            self._advance()
        return self.text[start:self.i]

    def _string_body(self, pos: Position) -> str:
        """Consume a string literal starting at its opening quote; return the raw body."""
        self._advance()                    # opening quote
        body_start = self.i
        while True:
            ch = self._peek()
            if ch == "":
                error("C1038", pos, "unterminated string literal")
            if ch == "\n":
                error("C1034", pos, "unescaped line break in a string literal")
            if ch == '"':
                body = self.text[body_start:self.i]
                self._advance()
                return body
            if ch == "\\" and self._peek(1) not in ("", "\n"):
                self._advance(2)
            else:
                self._advance()


def _check_digits(text: str, allowed: str, pos: Position) -> str:
    """Validate digit separators (SPEC-04 3.5 rule 2) and return the digits."""
    if text == "":
        error("C1024", pos, "a radix prefix or point needs digits after it")
    if text.startswith("_") or text.endswith("_") or "__" in text:
        error("C1021", pos, "misplaced digit separator `_`")
    digits = text.replace("_", "")
    if any(c not in allowed for c in digits):
        error("C1024", pos, "invalid digit in numeric literal")
    return digits


def _int_value(word: str, pos: Position) -> int:
    if len(word) >= 2 and word[0] == "0" and word[1] in "XBOT":
        error("C1020", pos, "the radix prefix letter is lowercase: write 0%s" % word[1].lower())
    if len(word) >= 2 and word[0] == "0" and word[1] in "xbot":
        prefix, body = word[1], word[2:]
        allowed = {"x": "0123456789abcdefABCDEF", "b": "01", "o": "01234567", "t": "N0P"}[prefix]
        digits = _check_digits(body, allowed, pos)
        if prefix == "t":
            value = 0
            for d in digits:
                value = value * 3 + {"N": -1, "0": 0, "P": 1}[d]
        else:
            value = int(digits, {"x": 16, "b": 2, "o": 8}[prefix])
    else:
        if len(word) > 1 and word[0] == "0" and (word[1].isdigit() or word[1] == "_"):
            error("C1022", pos, "a decimal literal of more than one digit must not begin with 0")
        digits = _check_digits(word, "0123456789", pos)
        value = int(digits)
    if abs(value).bit_length() > MAX_LITERAL_BITS:
        error("C1023", pos, "literal exceeds %d bits of magnitude" % MAX_LITERAL_BITS)
    return value


def _escape_value(body: str, pos: Position, in_str: bool) -> int:
    """The scalar value of one escape (SPEC-04 3.8). `in_str` admits `\\u{...}`."""
    simple = {"\\\\": 0x5C, '\\"': 0x22, "\\'": 0x27, "\\n": 0x0A, "\\r": 0x0D, "\\t": 0x09, "\\0": 0x00}
    if body in simple:
        return simple[body]
    if body.startswith("\\x") and len(body) == 4:
        v = int(body[2:], 16)
        if v <= 0x7F:
            return v
        error("C1032", pos, "\\x escapes in strings and characters are 00 to 7F")
    if in_str and body.startswith("\\u{") and body.endswith("}") and 1 <= len(body) - 4 <= 6:
        hexd = body[3:-1]
        if all(c in "0123456789abcdefABCDEF" for c in hexd):
            v = int(hexd, 16)
            if 0xD800 <= v <= 0xDFFF:
                error("C1031", pos, "surrogate code point in \\u escape")
            if v > 0x10FFFF:
                error("C1031", pos, "\\u escape above U+10FFFF")
            return v
    error("C1032", pos, "unknown escape sequence %r" % body)


def tokenize(text: str, path: str, line: int = 1, col: int = 1, keywords=KEYWORDS) -> list[Token]:
    lx = Lexer(text, path, line, col, keywords)
    out = []
    while True:
        t = lx.next_token()
        t.path = path
        out.append(t)
        if t.kind == "eof":
            return out


# ---------------------------------------------------------------------------
# String literals and holes (SPEC-04 3.8, 9.2)
# ---------------------------------------------------------------------------

@dataclass
class Hole:
    """An interpolation hole: `text` is the source between the braces."""
    text: str
    line: int
    col: int        # column of the first character after `{`


def string_parts(tok: Token, allow_holes: bool) -> list:
    """Decode a string token into a list of `bytes` and `Hole` items.

    Adjacent text is merged. A hole where holes are not permitted is C1033.
    """
    raw = tok.value
    line = tok.line
    path_pos = lambda k: Position(tok.path, line, tok.col + 1 + k)
    parts: list = []
    buf = bytearray()
    k = 0
    while k < len(raw):
        ch = raw[k]
        if ch == "\\":
            lx = Lexer(raw[k:], "", line, tok.col + 1 + k)
            body = lx._escape_text()
            v = _escape_value(body, path_pos(k), in_str=True)
            buf += chr(v).encode("utf-8")
            k += len(body)
        elif ch == "{" and raw[k + 1:k + 2] == "{":
            buf += b"{"
            k += 2
        elif ch == "}" and raw[k + 1:k + 2] == "}":
            buf += b"}"
            k += 2
        elif ch == "{":
            if not allow_holes:
                error("C1033", path_pos(k), "interpolation hole outside a print statement, format call or assert message")
            end = raw.find("}", k + 1)
            if end < 0:
                error("C1035", path_pos(k), "unterminated interpolation hole")
            if buf:
                parts.append(bytes(buf))
                buf = bytearray()
            parts.append(Hole(raw[k + 1:end], line, tok.col + 2 + k))
            k = end + 1
        elif ch == "}":
            error("C1039", path_pos(k), "a literal `}` is written `}}`")

        else:
            buf += ch.encode("utf-8")
            k += 1
    if buf or not parts:
        parts.append(bytes(buf))
    return parts
