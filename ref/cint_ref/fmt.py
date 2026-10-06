"""Exact formatting for print holes (SPEC-04 9.2 to 9.5; SPEC-01 section 7).

`parse_spec` reads a format specification, `check` applies the static rules
of SPEC-04 9.3 rule 11 (C1037), including the combinations that SPEC-04
LS-213 names as not defined (OQ-131, also C1037), and `render`
produces the exact text. Integers are rendered by mathematical value; the
storage bits only with `!bits`.
"""
from __future__ import annotations

from dataclasses import dataclass

from .faults import Refused
from .types import BOOL, int_type, is_int_type

# The bytes of one print statement that B1's runtime stages; past them it faults
# E_UNSUPPORTED host.resource (rt/OPEN.md RT-OQ-23).
PRINT_MAX = 1 << 30


class SpecError(Exception):
    """A static formatting error with its C code (SPEC-04 LS-313)."""
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code
        self.message = message


class FormatFault(Exception):
    """A value that `c` cannot render (SPEC-04 9.3 rule 7: E_NARROW)."""
    def __init__(self, value):
        super().__init__(value)
        self.value = value


@dataclass
class Spec:
    fill: str | None = None
    align: str | None = None
    sign: str | None = None
    alt: bool = False
    zero: bool = False
    width: int | None = None
    group: str | None = None
    precision: int | None = None
    kind: str | None = None
    scale: int | None = None


def _digit(c: str) -> bool:
    # Width, precision and scale are written in the ASCII digits 0 to 9;
    # any other character there is C1037 (OQ-192).
    return "0" <= c <= "9"


def parse_spec(text: str) -> Spec:
    s = Spec()
    i = 0
    n = len(text)
    if n >= 2 and text[1] in "<>^":
        s.fill, s.align, i = text[0], text[1], 2
    elif n >= 1 and text[0] in "<>^":
        s.align, i = text[0], 1
    if i < n and text[i] in "+- ":
        s.sign, i = text[i], i + 1
    if i < n and text[i] == "#":
        s.alt, i = True, i + 1
    if i < n and text[i] == "0":
        s.zero, i = True, i + 1
    j = i
    while i < n and _digit(text[i]):
        i += 1
    if i > j:
        s.width = int(text[j:i])
    if i < n and text[i] in "_,":
        s.group, i = text[i], i + 1
    if i < n and text[i] == ".":
        j = i + 1
        i = j
        while i < n and _digit(text[i]):
            i += 1
        if i == j:
            raise SpecError("C1037", "missing precision digits in %r" % text)
        s.precision = int(text[j:i])
    if i < n and text[i] in "dxXobtcs":
        s.kind, i = text[i], i + 1
    if i < n and text[i] == "/":
        j = i + 1
        i = j
        while i < n and _digit(text[i]):
            i += 1
        digits = text[j:i]
        if not (len(digits) >= 2 and digits[0] == "1" and set(digits[1:]) == {"0"}):
            raise SpecError("C1037", "a scale is a power of ten, 10 or greater")
        s.scale = int(digits)
    if i != n:
        raise SpecError("C1037", "malformed format specification %r" % text)
    return s


def _undefined(what):
    raise SpecError("C1037", "%s is not defined (SPEC-04 LS-213)" % what)


def check(spec: Spec | None, ty, conv: str | None, lanes: str | None):
    """Static checks for a hole of type `ty`."""
    if conv in ("raw", "ratio"):
        raise SpecError("C1037", "`!%s` applies to fixed point only" % conv)
    if ty == BOOL:
        if conv is not None:
            _undefined("a conversion applied to Bool")
        if spec and (spec.sign or spec.alt or spec.zero or spec.group or spec.precision is not None
                     or spec.kind or spec.scale):
            raise SpecError("C1037", "only fill, alignment and width apply to Bool")
        _check_layout(spec)
        return
    if not is_int_type(ty):
        _undefined("printing a value of type %s" % getattr(ty, "name", ty))
    if conv == "lanes":
        lt = int_type(lanes)
        if lt.signed or int_type(ty).width % lt.width:
            _undefined("lanes of a signed type, or lanes that do not divide the width")
    if spec is None:
        return
    k = spec.kind or "d"
    if spec.precision is not None and spec.scale is not None:
        raise SpecError("C1036", "precision is not permitted with a scale")
    if spec.precision is not None:
        raise SpecError("C1037", "precision does not apply to an integer")
    if k == "s":
        raise SpecError("C1037", "`s` does not apply to an integer")
    if k == "t" and (spec.alt or spec.sign or spec.group):
        raise SpecError("C1037", "`#`, a sign flag and a group do not apply to `t`")
    if k in "xXob" and spec.group == ",":
        raise SpecError("C1037", "`,` does not apply to `%s`" % k)
    if k == "c" and (spec.alt or spec.zero or spec.group):
        raise SpecError("C1037", "`#`, `0` and a group do not apply to `c`")
    if k == "c" and spec.sign:
        _undefined("a sign flag with `c`")
    if k == "t" and spec.zero:
        _undefined("zero padding with `t`")
    if spec.alt and k == "d":
        _undefined("`#` with decimal")
    if spec.scale is not None and (k != "d" or spec.alt or spec.group or conv):
        _undefined("a scale combined with a kind, `#`, a group or a conversion")
    if spec.zero and spec.group:
        _undefined("zero padding combined with a group")
    if conv == "bits" and k in "tc":
        _undefined("`!bits` with `t` or `c`")
    _check_layout(spec)


def _check_layout(spec):
    if spec is None:
        return
    if spec.zero and spec.align:
        _undefined("zero padding combined with an explicit alignment")
    if spec.width is not None and not spec.align and not spec.zero:
        _undefined("the default alignment when a width is given without `<`, `>` or `^`")


def staged(size: int) -> int:
    """`size`, the bytes of a print statement so far: past PRINT_MAX, cint_ref refuses
    rather than fault (G-C2 review COR-10). Padding is checked before it is built."""
    if size > PRINT_MAX:
        raise Refused("a print statement of more than 1 GiB (rt/OPEN.md RT-OQ-23): cint_ref does not "
                      "implement the host.resource fault")
    return size


def _pad(text: str, spec: Spec | None, used: int) -> str:
    if spec is None or spec.width is None or len(text) >= spec.width:
        return text
    fill = spec.fill or " "
    gap = spec.width - len(text)
    staged(used + len(text.encode("utf-8")) + gap * len(fill.encode("utf-8")))
    if spec.align == "<":
        return text + fill * gap
    if spec.align == ">":
        return fill * gap + text
    left = gap // 2
    return fill * left + text + fill * (gap - left)


def _group(digits: str, every: int, sep: str) -> str:
    out = []
    while len(digits) > every:
        out.append(digits[-every:])
        digits = digits[:-every]
    out.append(digits)
    return sep.join(reversed(out))


def ternary(v: int) -> str:
    """The minimal balanced-ternary literal of `v` (SPEC-04 9.3 rule 3)."""
    if v == 0:
        return "0t0"
    digits = []
    while v != 0:
        r = v % 3
        if r == 2:
            digits.append("N")
            v = (v + 1) // 3
        else:
            digits.append("0" if r == 0 else "P")
            v = (v - r) // 3
    return "0t" + "".join(reversed(digits))


def render(value, ty, conv: str | None, lanes: str | None, spec: Spec | None, used: int = 0) -> str:
    """The text of `value`; `used` is the bytes of the print statement before it."""
    if ty == BOOL:
        return _pad("true" if value else "false", spec, used)
    t = int_type(ty)
    if conv == "lanes":
        lt = int_type(lanes)
        bits = t.bits(value)
        items, used = [], used + 2              # the brackets
        for k in range(t.width // lt.width):
            used += 2 if k else 0               # the separator before lane k
            items.append(render((bits >> (k * lt.width)) & ((1 << lt.width) - 1), lt.name, None, None, spec, used))
            used += len(items[-1].encode("utf-8"))
        return "[" + ", ".join(items) + "]"
    width_bits = t.width
    if conv == "bits":
        value = t.bits(value)
    s = spec or Spec()
    kind = s.kind or "d"
    if kind == "c":
        if not (0 <= value <= 0x10FFFF) or 0xD800 <= value <= 0xDFFF:
            raise FormatFault(value)
        return _pad(chr(value), spec, used)
    if kind == "t":
        return _pad(ternary(value), spec, used)
    neg = value < 0
    a = -value if neg else value
    prefix = ""
    if s.scale is not None:
        k = len(str(s.scale)) - 1
        digits = "%d.%0*d" % (a // s.scale, k, a % s.scale)
    elif kind == "d":
        digits = str(a)
    else:
        base = {"x": 16, "X": 16, "o": 8, "b": 2}[kind]
        digits = format(a, kind)          # integer formatting only: x, X, o, b
        if conv == "bits":
            full = {16: (width_bits + 3) // 4, 8: (width_bits + 2) // 3, 2: width_bits}[base]
            digits = digits.rjust(full, "0")
        if s.alt:
            prefix = {16: "0x", 8: "0o", 2: "0b"}[base]
    if s.group:
        digits = _group(digits, 3 if kind == "d" else 4, s.group)
    sign = "-" if neg else ("+" if s.sign == "+" else " " if s.sign == " " else "")
    if s.zero and s.width is not None:
        body = len(sign) + len(prefix) + len(digits)
        if body < s.width:
            staged(used + s.width)
            digits = "0" * (s.width - body) + digits
        return sign + prefix + digits
    return _pad(sign + prefix + digits, spec, used)
