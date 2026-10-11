"""Formatted print and failed asserts of the wide integers, formatted print of fixed point, and the
conversion `!bits` against cint_ref (rt/OPEN.md RT-OQ-55, RT-OQ-56; SPEC-04 LS-201 to LS-213; box 15).

rt/tests/test_print_wide.c reads cases written here, one per line. `<id> P <entry> <tag> <form>
<fill> <width> <scale> <value> = <outcome>` prints the value in a statement of its own through
cint_rt_print_format_wide (entry W), cint_rt_print_format (entry N) or cint_rt_print_format_fixed
(entry Q, the tag a CINT_QT type word and the scale its precision); form, fill, width and scale are
the arguments of rt/cint_rt.h section 6c, the form built from a format specification as
compiler/lowering.ci format_form builds it, with no kind for fixed point, CINT_FORMAT_BITS for
`!bits` and CINT_FORMAT_PRECISION for a precision. `<id> A <tag> <a> <b> = <outcome>` calls
cint_rt_fault_assert_wide. A value is the hexadecimal of its little-endian bytes (IM-14). The
outcome is `T <hex>`, the bytes the statement delivered, or `F <hex>`, the canonical bytes of the
run-time fault record (SPEC-01 IM-149) at site 1 of `print.ci` (line 1, column 1) with an empty
stack.

The text is cint_ref's fmt.render, and a specification that fmt.check rejects (C1036, C1037) or
refuses (ref/OPEN.md REF-OQ-47) is a form the runtime refuses with `dispatch.admit`. A rendering
that cint_ref refuses past the statement's 1 GiB (fmt.staged) faults E_UNSUPPORTED host.resource
(rt/OPEN.md RT-OQ-23), and so does a scale of 2^31 or 2^64 - 1 fractional digits, or a precision
of 2^30 - 1 or 2^64 - 1, which cint_ref cannot build. `!raw` and `!bits` of a fixed-point value
print through entry N with the storage's tag, as the compiler lowers them. A failed assert
of a comparison faults E_ASSERT assert.checked.bool with both operands, as ref/cint_ref/exec.py
raises it.

The cases print each wide type and each of I8 to U64 and Bool at the boundaries of its width and
on seeded random values, under every kind (`d`, `x`, `X`, `o`, `b`, `t`), the sign flags, `#`,
zero padding, both groups, fills of one to four bytes in every alignment, widths past the value's
length, and scales from 10 to 10^400; `!bits` of each under every kind it admits; fixed-point
types of every storage width with 0 to 63 fractional bits, at their bounds, on values whose
rounding ties or carries, and on seeded random values, under the sign flags, zero padding, fills
in every alignment, and precisions from 0 to 700, with `!raw` and `!bits`; the specifications the
static rules exclude; a value through an entry of another type family, refused; type words that
name no fixed-point type, refused; and failed asserts of every wide type, with a narrow tag
refused. The driver also checks a statement that mixes the entries, the arguments no emitted code
passes, the order of calls, and a context that already holds a fault.

The driver, with rt/cint_rt.c, rt/cint_state.c and rt/cint_rec.c, is built for each leg of this
host (gcc, and clang when installed, on Linux; apple-clang on macOS; msvc on Windows) at -O0 and
-O2 (MSVC /Od and /O2) in both helper modes, with the flags of tools/cint_check.py and, under
`workflow.py check --sanitize`, its sanitizers on gcc and clang. Build outputs go under
<build>/rt-print-wide, where <build> is CINT_BUILD or cint-build in the system temporary
directory.
"""
import os
import pathlib
import random
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "ref"))
import cint_check as cc  # noqa: E402
from cint_ref import fmt  # noqa: E402
from cint_ref.faults import Fault, FaultRecord, Position, Refused, encode_fault_record  # noqa: E402
from cint_ref.fixed import is_q_type, q_type  # noqa: E402
from cint_ref.types import BOOL, Value, int_type  # noqa: E402

WIDE = (("I128", 0x15), ("I256", 0x16), ("I512", 0x17), ("I1024", 0x18),
        ("U128", 0x25), ("U256", 0x26), ("U512", 0x27))
NARROW = (("I8", 0x11), ("I16", 0x12), ("I32", 0x13), ("I64", 0x14),
          ("U8", 0x21), ("U16", 0x22), ("U32", 0x23), ("U64", 0x24))
TAG_BOOL = 0x01
POSITION = Position("print.ci", 1, 1)
EDGE_LIMBS = (0, 1, (1 << 63) - 1, 1 << 63, (1 << 64) - 1)
KIND = {None: 1, "d": 1, "x": 2, "X": 3, "o": 4, "b": 5, "t": 6}
ALIGN = {None: 0, "<": 16, ">": 32, "^": 48}
SIGN = {None: 0, "-": 0, "+": 64, " ": 128}
GROUP = {None: 0, "_": 1024, ",": 2048}

# Specifications the runtime admits, then ones whose width passes most values' length.
SPECS = ("", "d", "x", "X", "o", "b", "t", "#x", "#X", "#o", "#b", "+", " ", "-", "+x", " b", "+#o", "0",
         "_", ",", "_x", "_X", "_o", "_b", "+_", " ,", ">12", "<12", "^13", "*>40", "\u00e9^41",
         "-0320", "/10", "/100", "/1" + "0" * 20, "/1" + "0" * 155, "/1" + "0" * 308, "/1" + "0" * 400,
         "+/1000", ">40/100", "^20/10", " /1000",
         # excluded by the static rules (LS-213, SPEC-04 9.3 rule 11)
         "#", "#d", "_/100", ",/10", ",x", ",b", "+t", " t", "#t", "_t", "0t", "0>8", "8", "0_", "x/100",
         "#/100", "0^9", "b/10", ".3", ".2x", "_.1")
LARGE = ("*>400", "\U0001f600<330", ".>700t", "->1300b", "#>1100b", "0400", "+0400x", "#01100b", "#0300X",
         " 0350o", "0350/10000", "\u20ac^1031", "\u00e9>1029,")
# Past the statement's 1 GiB: fmt.staged refuses before it builds the text.
HUGE = (">1073741825", "01073741825", "\U0001f600^269484033", "\u00e9<536875008", "+#01073741825x")
# `!bits` (LS-201), then the specifications the static rules exclude with it (LS-213).
BITS_SPECS = ("", "d", "x", "X", "o", "b", "#x", "#X", "#o", "#b", "+", " x", "+#b", "_x", "_X", "_b", "_o", "_",
              ",", ">40x", "<70b", "\u00e9^41o", "0350d", "#0300X",
              "t", "#t", "/100", "x/100", ",x", "#", "#d")
BITS_LARGE = ("->1300b", "#01100b", "_b", "*>400o")
# Fixed point (LS-207, LS-208; ref/OPEN.md REF-OQ-47), then the specifications excluded or refused.
QTYPES = ("Q8.0", "Q1.7", "Q4.4", "Q7.1", "Q16.0", "Q1.15", "Q8.8", "Q13.3", "Q1.31", "Q16.16", "Q24.8", "Q2.30",
          "Q64.0", "Q1.63", "Q3.61", "Q32.32", "Q48.16", "Q63.1")
FIXED_SPECS = ("", "+", " ", "-", ".0", ".1", ".2", ".3", ".5", ".7", ".10", ".20", ".63", ".64", ".70", "+.2", " .3", "-.0",
               ">12", "<12", "^13", "*>40.3", "\u00e9^41", "\U0001f600<30.1", "012", "+015.4", " 030.1", "-0320",
               ">30.0", "08.0",
               "d", "x", "X", "o", "b", "t", "#", "#.2", "_", ",", "_.3", "/100", "0>8", "8", "0_", "+t", ".2x")
FIXED_LARGE = (".400", ".700", "*>400.3", "0400.10", "\u20ac^1031.5")
FIXED_HUGE = (">1073741825", "01073741825", ".1073741825", "\u00e9<536875008")
RAW_SPECS = ("", "x", "#b", "+", ">24")
FIXED_BITS = ("", "x", "#b", "_X", "o")
FORMAT_BITS = 4096
FORMAT_PRECISION = 8192
TAG_Q = 0x31


def hexle(name: str, v: int) -> str:
    if name == BOOL:
        return "%02x" % int(v)
    w = q_type(name).width if is_q_type(name) else int_type(name).width
    return (v & ((1 << w) - 1)).to_bytes(w // 8, "little").hex()


def storage_tag(width: int) -> int:
    return dict(NARROW)["I%d" % width]


def qword(name: str) -> int:
    """CINT_QT(storage tag, f) of rt/cint_fixed.h."""
    q = q_type(name)
    return (q.f << 16) | (TAG_Q << 8) | storage_tag(q.width)


def boundaries(name: str) -> list:
    t = int_type(name)
    w = t.width
    vals = {0, 1, -1, 2, -2, 7, 10, -5, 999, 1000, -1000, 123456789, t.max, t.min, t.max - 1, t.min + 1,
            1 << 63, -(1 << 63), (1 << 64) - 1, 1 << 64, -(1 << 64), 1 << (w - 2), -(1 << (w - 2)),
            (1 << (w - 1)) - 1, 10 ** ((w - 1) * 3 // 10), -(10 ** ((w - 1) * 3 // 10))}
    return sorted(v for v in vals if t.contains(v))


def patterned(rng: random.Random, name: str) -> int:
    t = int_type(name)
    n = max(1, t.width // 64)
    used = rng.randint(1, n)
    limbs = [rng.choice(EDGE_LIMBS) if rng.random() < 0.4 else rng.getrandbits(64) for _ in range(used)]
    if rng.random() < 0.3:
        limbs[-1] >>= rng.randint(0, 63)
    v = sum(limb << (64 * i) for i, limb in enumerate(limbs))
    if rng.random() < 0.5:
        v = -v
    return v if t.contains(v) else t.wrap(v)


def fixed_values(rng: random.Random, name: str) -> list:
    """Raw values of a fixed-point type: its bounds, values whose rounding ties or carries at some
    precision (0.5, 0.75, 1.5, 2.5, 0.125, 0.625, 9.99609375, 6.25, and 1 - 2^-f), and random ones."""
    q = q_type(name)
    t, f = q.storage, q.f
    vals = {0, 1, -1, 2, -2, 5, -5, t.max, t.min, t.max - 1, t.min + 1, (1 << f) - 1, 1 - (1 << f)}
    for num, shift in ((1, 0), (1, 1), (3, 2), (3, 1), (5, 1), (1, 3), (5, 3), (2559, 8), (25, 2)):
        if shift <= f:
            vals |= {num << (f - shift), -(num << (f - shift))}
    out = sorted(v for v in vals if t.contains(v))
    return out + [rng.randint(t.min, t.max) for _ in range(3)] + [t.wrap(rng.randint(-(1 << (f + 5)), 1 << (f + 5)))]


def arguments(spec: fmt.Spec, conv=None):
    """form, fill, width and scale of rt/cint_rt.h section 6c, as compiler/lowering.ci passes them."""
    form = KIND[spec.kind] + ALIGN[spec.align] + SIGN[spec.sign] + 256 * spec.alt + 512 * spec.zero
    form += GROUP[spec.group] + FORMAT_BITS * (conv == "bits")
    scale = len(str(spec.scale)) - 1 if spec.scale is not None else 0
    if spec.precision is not None:  # C1037 on an integer: refused whatever else the form holds
        form, scale = form + FORMAT_PRECISION, spec.precision
    return form, ord(spec.fill) if spec.fill else 32, spec.width or 0, scale


def fixed_arguments(spec: fmt.Spec):
    """The arguments of cint_rt_print_format_fixed: no kind unless the specification names one,
    and the precision in place of a scale, under CINT_FORMAT_PRECISION."""
    form, fill, width, scale = arguments(spec)
    if spec.kind is None:
        form -= KIND[None]
    return form, fill, width, scale


def encode(fault: Fault) -> str:
    return "F " + encode_fault_record(FaultRecord.from_fault(fault, POSITION), "run-time").hex()


REFUSED = encode(Fault("E_UNSUPPORTED", "dispatch.admit", ()))
RESOURCE = encode(Fault("E_UNSUPPORTED", "host.resource", ()))


def printed(value: int, name: str, spec: fmt.Spec, conv=None) -> str:
    try:
        fmt.check(spec, name, conv, None)
    except (fmt.SpecError, Refused):
        return REFUSED
    try:
        text = fmt.render(value, name, conv, None, spec).encode("utf-8")
    except Refused:
        return RESOURCE
    return RESOURCE if len(text) > fmt.PRINT_MAX else "T " + text.hex()


def generate(seed: int = 55):
    """The case lines."""
    rng = random.Random(seed)
    out = []

    def add(body, outcome):
        out.append("%d %s = %s" % (len(out), body, outcome))

    def hole(entry, tag, args, name, value, outcome):
        add("P %s %d %d %d %d %d %s" % ((entry, tag) + args + (hexle(name, value),)), outcome)

    specs = [fmt.parse_spec(s) for s in SPECS]
    large = [fmt.parse_spec(s) for s in LARGE]
    huge = [fmt.parse_spec(s) for s in HUGE]
    for t, tag in WIDE + NARROW:
        vals = boundaries(t)
        vals += [patterned(rng, t) for _ in range(8 if (t, tag) in WIDE else 3)]
        entry, other = ("W", "N") if (t, tag) in WIDE else ("N", "W")
        for i, v in enumerate(vals):
            for spec in specs + (large if i % 3 == 0 else []):
                hole(entry, tag, arguments(spec), t, v, printed(v, t, spec))
        for spec in huge:
            hole(entry, tag, arguments(spec), t, vals[-1], printed(vals[-1], t, spec))
        for scale in (1 << 31, (1 << 64) - 1):     # no bytes before a fault of host.resource
            hole(entry, tag, (1, 32, 0, scale), t, vals[0], RESOURCE)
        for v in (vals[0], vals[-1]):                # the other entry refuses the type
            hole(other, tag, (1, 32, 0, 0), t, v, REFUSED)
    bits = [fmt.parse_spec(s) for s in BITS_SPECS]
    bits_large = [fmt.parse_spec(s) for s in BITS_LARGE]
    for t, tag in WIDE + NARROW:
        vals = boundaries(t)[::2] + [patterned(rng, t) for _ in range(3)]
        entry = "W" if (t, tag) in WIDE else "N"
        for i, v in enumerate(vals):
            for spec in bits + (bits_large if i % 4 == 0 else []):
                hole(entry, tag, arguments(spec, "bits"), t, v, printed(v, t, spec, "bits"))
    fixed = [fmt.parse_spec(s) for s in FIXED_SPECS]
    fixed_large = [fmt.parse_spec(s) for s in FIXED_LARGE]
    raw = [fmt.parse_spec(s) for s in RAW_SPECS]
    fixed_bits = [fmt.parse_spec(s) for s in FIXED_BITS]
    for qn in QTYPES:
        qt, stag = qword(qn), storage_tag(q_type(qn).width)
        vals = fixed_values(rng, qn)
        for i, v in enumerate(vals):
            for spec in fixed + (fixed_large if i % 4 == 0 else []):
                hole("Q", qt, fixed_arguments(spec), qn, v, printed(v, qn, spec))
            for spec in raw:  # `!raw`: the storage integer through the narrow entry
                hole("N", stag, arguments(spec), qn, v, printed(v, qn, spec, "raw"))
            for spec in fixed_bits:
                hole("N", stag, arguments(spec, "bits"), qn, v, printed(v, qn, spec, "bits"))
        for s in FIXED_HUGE:
            spec = fmt.parse_spec(s)
            hole("Q", qt, fixed_arguments(spec), qn, vals[-1], printed(vals[-1], qn, spec))
        for precision in ((1 << 30) - 1, (1 << 64) - 1):  # the whole part and the point pass 1 GiB
            hole("Q", qt, (FORMAT_PRECISION, 32, 0, precision), qn, vals[0], RESOURCE)
        for entry in ("N", "W"):  # a fixed-point type word through another entry
            hole(entry, qt, (1, 32, 0, 0), qn, vals[0], REFUSED)
        hole("Q", qt, (FORMAT_BITS, 32, 0, 0), qn, vals[0], REFUSED)  # `!bits` takes the storage's tag
    for word, name in (((8 << 16) | (TAG_Q << 8) | 0x11, "I8"), ((4 << 16) | (TAG_Q << 8) | 0x22, "U16"),
                       (0x13, "I32"), (0x14, "I64"), (0x15, "I128"), (TAG_BOOL, BOOL)):
        for form in (0, 1):
            hole("Q", word, (form, 32, 0, 0), name, 1, REFUSED)  # no fixed-point type
    for entry, tag, name in (("N", 0x13, "I32"), ("W", 0x15, "I128"), ("Q", qword("Q16.16"), "Q16.16")):
        hole(entry, tag, ((1 if entry != "Q" else 0) + 16384, 32, 0, 0), name, 1, REFUSED)  # a bit no form has
    for v in (False, True):
        for s in SPECS[:30] + LARGE[:2] + HUGE[:1]:
            spec = fmt.parse_spec(s)
            if spec.kind != "d" and spec.sign != "-":  # their form is that of no kind, no sign
                hole("N", TAG_BOOL, arguments(spec), BOOL, v, printed(v, BOOL, spec))
        hole("W", TAG_BOOL, (1, 32, 0, 0), BOOL, v, REFUSED)
        hole("N", TAG_BOOL, (1 + FORMAT_BITS, 32, 0, 0), BOOL, v, REFUSED)  # no conversion of Bool
    add("P N %d 1 32 0 0 02" % TAG_BOOL, REFUSED)     # a Bool other than 0 and 1
    for t, tag in WIDE:
        vals = boundaries(t) + [patterned(rng, t) for _ in range(6)]
        for i, a in enumerate(vals):
            b = vals[(7 * i + 3) % len(vals)]
            fault = Fault("E_ASSERT", "assert.checked.bool", (Value(t, a), Value(t, b)))
            add("A %d %s %s" % (tag, hexle(t, a), hexle(t, b)), encode(fault))
    for t, tag in NARROW:
        add("A %d %s %s" % (tag, hexle(t, 1), hexle(t, 2)), REFUSED)
    return out


def legs():
    if os.name == "nt":
        return ["msvc"]
    if sys.platform == "darwin":
        return ["apple-clang"]
    return [leg for leg in ("gcc", "clang") if shutil.which(leg)]


class PrintWide(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lines = generate()

    def test_cases_reach_every_outcome(self):
        printed_kinds, bits_kinds, fixed_printed, refused, resource, asserts = set(), set(), set(), set(), set(), set()
        for line in self.lines:
            f = line.split(" ")
            outcome = line.split(" = ", 1)[1]
            if f[1] == "P" and outcome.startswith("T "):
                tag, form = int(f[3]), int(f[4])
                if form & FORMAT_BITS:
                    bits_kinds.add((tag, form & 15))
                elif f[2] == "W":
                    printed_kinds.add((tag, form & 15))
                elif f[2] == "Q":
                    fixed_printed.add((tag, form & FORMAT_PRECISION != 0))
            elif f[1] == "P" and outcome == REFUSED:
                refused.add((f[2], int(f[3])))
            elif f[1] == "P" and outcome == RESOURCE:
                resource.add((f[2], int(f[3])))
            elif f[1] == "A" and outcome != REFUSED:
                asserts.add(int(f[2]))
        wide, narrow = {tag for _, tag in WIDE}, {tag for _, tag in NARROW}
        words = {qword(qn) for qn in QTYPES}
        self.assertEqual(printed_kinds, {(tag, k) for tag in wide for k in range(1, 7)})
        self.assertEqual(bits_kinds, {(tag, k) for tag in wide | narrow for k in range(1, 6)})
        self.assertEqual(fixed_printed, {(word, p) for word in words for p in (False, True)})
        self.assertEqual({tag for entry, tag in refused if entry == "W"}, wide | narrow | words | {1})
        self.assertEqual({tag for entry, tag in refused if entry == "Q"} & words, words)
        self.assertEqual({tag for entry, tag in resource if entry == "W"}, wide)
        self.assertEqual({tag for entry, tag in resource if entry == "Q"}, words)
        self.assertEqual(asserts, wide)

    def test_every_build_agrees(self):
        found = legs()
        if not found:
            self.skipTest("no gcc or clang on this host")
        root = cc.build_root() / "rt-print-wide"
        root.mkdir(parents=True, exist_ok=True)
        sanitize = os.environ.get("INTOS_SANITIZE") == "1"
        with tempfile.TemporaryDirectory(prefix="print-", dir=root) as tmp:
            work = pathlib.Path(tmp)
            cases = work / "cases.txt"
            cases.write_bytes("".join(line + "\n" for line in self.lines).encode("ascii"))
            for leg in found:
                for opt in ("0", "2"):
                    for helpers in ("portable", "builtin"):
                        name = "%s-O%s-%s" % (leg, opt, helpers)
                        with self.subTest(build=name):
                            out = work / name
                            out.mkdir()
                            tc = cc.Toolchain(leg, opt, helpers, sanitize and leg in ("gcc", "clang"))
                            objs = tc.compile([ROOT / "rt" / f for f in ("cint_rt.c", "cint_state.c", "cint_rec.c",
                                                                          "tests/test_print_wide.c")], out, name)
                            exe = tc.link_exe(objs, out / "test_print_wide", name)
                            run = subprocess.run([str(exe), str(cases)], capture_output=True, timeout=600)
                            self.assertEqual(run.returncode, 0, run.stderr[-3000:])
                            self.assertEqual(run.stderr, b"")
                            self.assertIn(b"helper mode: " + helpers.encode("ascii"), run.stdout)
                            self.assertIn(b"print cases: %d, passed %d, failed 0" % (len(self.lines),
                                                                                     len(self.lines)), run.stdout)
                            self.assertIn(b"direct checks: failed 0", run.stdout)


if __name__ == "__main__":
    unittest.main()
