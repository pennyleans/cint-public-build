import os, sys; sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # put ref/ on the path
"""Tests for the canonical type signatures of sig.py (SPEC-03 5.6, A-20 to A-22; box 10
defaults BX10-23 and BX10-29): the five test vectors of SPEC-03 5.6 byte for byte, every form
of A-20 through the type model, the definitions of A-21 and their order, the positions and
values A-20 does not admit, and the mapping from cint_ref's checked programs, including the
`sig` command. The vectors' text is copied from SPEC-03 5.6; their bytes are not computed
from cintc."""
import contextlib
import hashlib
import io
import pathlib
import re
import tempfile
import unittest

from cint_ref import sig as S
from cint_ref.__main__ import main as cli
from cint_ref.faults import CompileError

PREFIX = b"\x1d\x00\x00\x00cint-core-1/type-signature/v1"


def listing(text: str) -> bytes:
    """The bytes of a SPEC-03 5.6 listing (hex pairs and quoted ASCII), after the 33-byte prefix."""
    out = bytearray(PREFIX)
    for tok in re.findall(r'"[^"]*"|\b[0-9a-f]{2}\b', text):
        out += tok[1:-1].encode("ascii") if tok.startswith('"') else bytes([int(tok, 16)])
    return bytes(out)


def body(*parts: str) -> bytes:
    """PREFIX followed by the hex of `parts` (spaces ignored)."""
    return PREFIX + bytes.fromhex("".join(parts).replace(" ", ""))


def name(text: str) -> str:
    """A name of A-20 as hex: U32 length, then the ASCII bytes."""
    return len(text).to_bytes(4, "little").hex() + text.encode("ascii").hex()


def exports(src: str, path: str = "t.ci", root=None) -> dict:
    return {e.name: e for e in S.program_exports(src.encode("utf-8"), path, root)}


# SPEC-03 5.6: (export, bytes, SHA-256) of the five test vectors.
VECTORS = {"advance": (99, "f17b75a6e89651dd40ca58ad7fa76bfe39eba562c520e561ce852974e993444e"),
           "both": (116, "e604b42aa501289f82feba1ba82519b757db691ffef9b1aa0ddf737b3d281154"),
           "read_number": (194, "9296f1731a406591620b575fcc8b0f7d92d2a90b4467f53fc1ed114521b03f98"),
           "paired": (116, "a0f444fd666940950bfdab92e6d113a42d45949a5794269f8494e33b6a114993"),
           "parse_u": (118, "1576731f670cdfbfd24094f188a6c49648188aa95b4da168d704d29080bef7b2")}

ADVANCE_LISTING = """
00 00 00 00
01 00 00 00  04 00 00 00
00 52 14 01  01 00 00 00 00  00 00 00 00 00 00 00 00
00 52 14 01  01 00 00 00 00  00 00 00 00 00 00 00 00
00 14
02 52 14 01  01 00 00 00 00  00 00 00 00 00 00 00 00
00
"""
BOTH_LISTING = """
01 00 00 00
71 15 00 00 00 "public_records.Simple"
   00  02 00 00 00
   05 00 00 00 "value" 00 14
   04 00 00 00 "flag" 00 01
00 00 00 00  03 00 00 00
01 71 00 00 00 00
00 71 00 00 00 00
00 14
71 00 00 00 00
"""
READ_NUMBER_LISTING = """
03 00 00 00
73 0d 00 00 00 "parse.IoError" 22 00
   02 00 00 00  06 00 00 00 "closed"  04 00 00 00 "full"
73 10 00 00 00 "parse.ParseError" 22 00
   03 00 00 00  05 00 00 00 "empty"  09 00 00 00 "bad_digit"  09 00 00 00 "too_large"
73 0f 00 00 00 "parse.IoOrParse" 22 01
   02 00 00 00  00 00 00 00  01 00 00 00
00 00 00 00  01 00 00 00
00 52 21 01 00
74 02 00 00 00 14
"""

ADVANCE_SRC = """export kernel advance[n](in I64[n] pos, in I64[n] vel, in I64 dt, out I64[n] next) {
    next = pos + vel * dt;
}
"""
BOTH_SRC = """struct Simple { I64 value; Bool flag; }

export Simple both(inout Simple a, Simple b, I64 divisor) {
    a.value = 42;
    return Simple(10 / divisor, b.flag);
}
"""
PAIRED_SRC = """export I64 paired[n, m](inout I64[n] a, I64 bias, in I64[n] b, in U8[m] bytes, in U8[m] more) {
    if (n == 0) { return m; }
    a[0] = b[0] + bias;
    return a[0] + (bytes[0] as I64) + (more[0] as I64);
}
"""
PARSE_SRC = """error IoError { closed, full }
error ParseError { empty, bad_digit, too_large }
error IoOrParse = IoError | ParseError;

export IoOrParse!I64 read_number(in U8[_] s) {
    return 0;
}

export ParseError!I64 parse_u(in U8[_] s) {
    return .empty;
}

export I64 classify(in IoOrParse e, in ArithError a) {
    return 0;
}

export IoError!void close() {
    return .closed;
}
"""


def parse_sets():
    """The error sets of SPEC-03 5.6 in the module `parse` (SPEC-04 LS-97)."""
    io_error = S.ErrorSet("parse.IoError", ["closed", "full"])
    parse_error = S.ErrorSet("parse.ParseError", ["empty", "bad_digit", "too_large"])
    return io_error, parse_error, S.ErrorSet("parse.IoOrParse", operands=[io_error, parse_error])


def read_number() -> bytes:
    _, _, io_or_parse = parse_sets()
    return S.encode(S.Signature(0, [("in", S.Array("U8", [S.ANY]))], S.ErrorUnion(io_or_parse, "I64")))


def parse_u() -> bytes:
    _, parse_error, _ = parse_sets()
    return S.encode(S.Signature(0, [("in", S.Array("U8", [S.ANY]))], S.ErrorUnion(parse_error, "I64")))


class SpecExamples(unittest.TestCase):
    """The test vectors of SPEC-03 5.6: cint_ref reproduces each byte for byte."""

    def vector(self, export, data):
        size, digest = VECTORS[export]
        self.assertEqual((len(data), hashlib.sha256(data).hexdigest()), (size, digest))

    def test_advance_from_source(self):
        e = exports(ADVANCE_SRC, "advance.ci")["advance"]
        self.assertEqual(e.kind, "kernel")
        self.assertEqual(e.signature, listing(ADVANCE_LISTING))
        self.vector("advance", e.signature)

    def test_both_from_source(self):
        e = exports(BOTH_SRC, "public_records.ci")["both"]
        self.assertEqual(e.signature, listing(BOTH_LISTING))
        self.vector("both", e.signature)

    def test_read_number_from_the_model(self):
        self.assertEqual(read_number(), listing(READ_NUMBER_LISTING))
        self.vector("read_number", read_number())

    def test_paired_from_source(self):
        self.vector("paired", exports(PAIRED_SRC, "public_views.ci")["paired"].signature)

    def test_parse_u_from_the_model(self):
        # The definition of ParseError alone, and read_number's parameter and result with index 0.
        data = parse_u()
        self.vector("parse_u", data)
        self.assertEqual(data[33:37], b"\x01\x00\x00\x00")
        self.assertTrue(data.endswith(bytes.fromhex("00000000" "01000000" "0052210100" "740000000014")))

    def test_read_number_and_parse_u_from_source(self):
        # The checker numbers the sets of SPEC-04 LS-97 (roadmap box 12); the combined set
        # lists its members, not its values (A-21).
        rows = exports(PARSE_SRC, "parse.ci")
        self.assertEqual(rows["read_number"].signature, listing(READ_NUMBER_LISTING))
        self.vector("read_number", rows["read_number"].signature)
        self.assertEqual(rows["parse_u"].signature, parse_u())
        self.vector("parse_u", rows["parse_u"].signature)

    def test_error_values_the_built_in_set_and_void_success_from_source(self):
        rows = exports(PARSE_SRC, "parse.ci")
        io_error, parse_error, io_or_parse = parse_sets()
        arith = S.ErrorSet("ArithError", ["overflow", "div_zero", "shift", "narrow"])
        self.assertEqual(rows["classify"].signature,
                         S.encode(S.Signature(0, [("in", S.ErrorValue(io_or_parse)), ("in", S.ErrorValue(arith))],
                                              "I64")))
        self.assertIn(bytes([0x73]) + bytes.fromhex(name("ArithError")), rows["classify"].signature)
        self.assertEqual(rows["close"].signature, S.encode(S.Signature(0, [], S.ErrorUnion(io_error))))

    def test_the_model_gives_the_source_examples_too(self):
        n = S.symbol(0)
        advance = S.Signature(1, [("in", S.Array("I64", [n])), ("in", S.Array("I64", [n])), ("in", "I64"),
                                  ("out", S.Array("I64", [n]))])
        simple = S.Struct("public_records.Simple", [S.Field("value", "I64"), S.Field("flag", "Bool")])
        both = S.Signature(0, [("inout", simple), ("in", simple), ("in", "I64")], simple)
        paired = S.Signature(2, [("inout", S.Array("I64", [n])), ("in", "I64"), ("in", S.Array("I64", [n])),
                                 ("in", S.Array("U8", [S.symbol(1)])), ("in", S.Array("U8", [S.symbol(1)]))], "I64")
        for export, sig in (("advance", advance), ("both", both), ("paired", paired)):
            with self.subTest(export=export):
                self.vector(export, S.encode(sig))

    def test_every_signature_begins_with_the_domain(self):
        self.assertEqual(PREFIX[:4], (29).to_bytes(4, "little"))
        self.assertEqual(S.DOMAIN, PREFIX[4:])
        self.assertEqual(len(PREFIX), 33)


class Forms(unittest.TestCase):
    """Every form of the A-20 table, with the bytes written out by hand."""

    def param(self, t, size_count=0, mode="in"):
        return S.encode(S.Signature(size_count, [(mode, t)]))

    def test_scalar_tags(self):
        tags = {"Bool": "01", "I8": "11", "I16": "12", "I32": "13", "I64": "14", "I128": "15", "I256": "16",
                "I512": "17", "I1024": "18", "U8": "21", "U16": "22", "U32": "23", "U64": "24", "T1": "41",
                "T27": "42"}
        for t, tag in tags.items():
            with self.subTest(t=t):
                self.assertEqual(self.param(t), body("00000000 00000000 01000000", "00", tag, "00"))
                self.assertEqual(S.encode(S.Signature(0, [], t)), body("00000000 00000000 00000000", tag))

    def test_modes(self):
        self.assertEqual(self.param("I64", mode="out"), body("00000000 00000000 01000000 02 14 00"))
        self.assertEqual(self.param(S.Array("I64", [S.ANY]), mode="inout"),
                         body("00000000 00000000 01000000 01 52 14 01 00 00"))
        with self.assertRaises(ValueError):
            self.param("I64", mode="ref")

    def test_fixed_point(self):
        # Q4.60: storage I64, 60 fractional bits; Q1.7 is the widest fraction of I8.
        self.assertEqual(self.param(S.Fixed("I64", 60)), body("00000000 00000000 01000000 00 31 14 3c00 00"))
        self.assertEqual(self.param(S.Fixed("I8", 7)), body("00000000 00000000 01000000 00 31 11 0700 00"))
        for bad in (S.Fixed("I8", 8), S.Fixed("U8", 4), S.Fixed("I128", 4), S.Fixed("I32", -1)):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                self.param(bad)

    def test_shapes(self):
        lit8 = "01 ffffffff 0800000000000000"
        self.assertEqual(self.param(S.Array("I32", [S.ANY])), body("00000000 00000000 01000000 00 52 13 01 00 00"))
        self.assertEqual(self.param(S.Array("I32", [S.literal(8)])),
                         body("00000000 00000000 01000000 00 52 13 01", lit8, "00"))
        # n + 1 and m - 1 (SPEC-02 K-1): (symbol, offset).
        self.assertEqual(self.param(S.Array("I32", [S.symbol(0, 1), S.symbol(1, -1)]), size_count=2),
                         body("00000000 02000000 01000000 00 52 13 02",
                              "01 00000000 0100000000000000", "01 01000000 ffffffffffffffff", "00"))
        # 1 ..= n and 1 .. 101 (SPEC-04 LS-72): the lower bound as I64, then the bound.
        inclusive = S.Shape("inclusive", S.Bound(0, 0), lower=1)
        exclusive = S.Shape("exclusive", S.Bound(None, 101), lower=1)
        self.assertEqual(self.param(S.Array("U8", [inclusive, exclusive]), size_count=1),
                         body("00000000 01000000 01000000 00 52 21 02",
                              "02 0100000000000000 00000000 0000000000000000",
                              "03 0100000000000000 ffffffff 6500000000000000", "00"))
        self.assertEqual(self.param(S.Array("Bool", [S.ANY] * 4)),
                         body("00000000 00000000 01000000 00 52 01 04 00 00 00 00 00"))

    def test_array_rules(self):
        bad = [S.Array("I64", []), S.Array("I64", [S.ANY] * 5),               # rank 1 to 4
               S.Array(S.Array("I64", [S.ANY]), [S.ANY]),                     # an element is never an array
               S.Array("I64", [S.symbol(1)]),                                 # a symbol at or above size_count
               S.Array("I64", [S.Shape("diagonal")])]
        for t in bad:
            with self.subTest(t=t), self.assertRaises(ValueError):
                self.param(t, size_count=1)
        for result in (S.Array("I64", [S.ANY]), S.Tuple(["I64", S.Array("I64", [S.literal(2)])])):
            with self.subTest(result=result), self.assertRaises(ValueError):
                S.encode(S.Signature(0, [], result))      # an array result is A-25

    def test_struct_with_layout_bit_fields_nesting_and_arrays(self):
        inner = S.Struct("geo.vec.Inner", [S.Field("v", "I16")])
        status = S.Struct("geo.vec.Status", [S.Field("mode", "U16", 3), S.Field("armed", "Bool", 1),
                                             S.Field("trim", "I16", 12), S.Field("inner", inner),
                                             S.Field("pts", S.Array(inner, [S.literal(2)]))], packed=True)
        data = S.encode(S.Signature(0, [("inout", status), ("in", inner)], inner))
        self.assertEqual(data, body(
            "02000000",
            "71", name("geo.vec.Inner"), "00 01000000", name("v"), "00 12",              # 0: completes first
            "71", name("geo.vec.Status"), "01 05000000",                                 # 1: @packed
            name("mode"), "03 22", name("armed"), "01 01", name("trim"), "0c 12",
            name("inner"), "00 71 00000000",
            name("pts"), "00 52 71 00000000 01 01 ffffffff 0200000000000000",
            "00000000 02000000", "01 71 01000000", "00 71 00000000", "71 00000000"))

    def test_struct_rules(self):
        a = S.Struct("m.A", [S.Field("x", "I64")])
        cases = [S.Struct("m.B", [S.Field("x", "Bool", 2)]),                   # a Bool bit width is 1
                 S.Struct("m.B", [S.Field("x", "U16", 17)]),                   # above the type's width
                 S.Struct("m.B", [S.Field("x", a, 1)]),                        # a struct field
                 S.Struct("m.B", [S.Field("x", S.Array("U8", [S.literal(2)]), 1)]),
                 S.Struct("m.B", [S.Field("x", "I64"), S.Field("x", "I32")]),  # one name twice
                 S.Struct("m.B", [S.Field("x", S.Array("U8", [S.ANY]))]),      # an open extent
                 S.Struct("m.B", [S.Field("x", S.Array("U8", [S.symbol(0)]))]),
                 S.Struct("m.B", [S.Field("1x", "I64")]),                      # not an identifier
                 S.Struct("m..B", [S.Field("x", "I64")])]
        for st in cases:
            with self.subTest(st=st), self.assertRaises(ValueError):
                S.encode(S.Signature(1, [("in", st)]))
        loop = S.Struct("m.Loop")
        loop.fields = [S.Field("next", loop)]
        other = S.Struct("m.A", [S.Field("y", "I64")])
        for sig in (S.Signature(0, [("in", loop)]), S.Signature(0, [("in", a), ("in", other)])):
            with self.subTest(sig=sig), self.assertRaises(ValueError):
                S.encode(sig)

    def test_a_definition_is_written_once(self):
        # A struct used by two parameters, by a field of another, and as the result.
        point = S.Struct("m.Point", [S.Field("x", "I32"), S.Field("y", "I32")])
        line = S.Struct("m.Line", [S.Field("a", point), S.Field("b", point)])
        data = S.encode(S.Signature(0, [("in", point), ("in", line), ("in", point)], line))
        self.assertEqual(data, body(
            "02000000",
            "71", name("m.Point"), "00 02000000", name("x"), "00 13", name("y"), "00 13",
            "71", name("m.Line"), "00 02000000", name("a"), "00 71 00000000", name("b"), "00 71 00000000",
            "00000000 03000000", "00 71 00000000", "00 71 01000000", "00 71 00000000", "71 01000000"))

    def test_enum(self):
        mode = S.Enum("m.Mode", "I8", [("idle", 0), ("burn", 1), ("back", -2)])
        wide = S.Enum("m.Wide", "U16", [("low", 1), ("high", 65535)])
        data = S.encode(S.Signature(0, [("in", mode), ("in", S.Array(wide, [S.ANY]))], wide))
        self.assertEqual(data, body(
            "02000000",
            "72", name("m.Mode"), "11 03000000", name("idle"), "00", name("burn"), "01", name("back"), "fe",
            "72", name("m.Wide"), "22 02000000", name("low"), "0100", name("high"), "ffff",
            "00000000 02000000", "00 72 00000000", "00 52 72 01000000 01 00", "72 01000000"))
        for bad in (S.Enum("m.E", "Bool", [("a", 0)]), S.Enum("m.E", "I128", [("a", 0)]),
                    S.Enum("m.E", "U8", [("a", 0), ("b", 0)]), S.Enum("m.E", "U8", [("a", 0), ("a", 1)]),
                    S.Enum("m.E", "U8", [("a", 256)]), S.Enum("m.E", "I8", [("a", -129)])):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                S.encode(S.Signature(0, [("in", bad)]))

    def test_error_sets_values_and_unions(self):
        # A combined set lists its members, not its operands: AB is an operand of ABC and is not
        # one of its members, B is listed once, and A keeps its first position (SPEC-04 LS-97).
        a = S.ErrorSet("m.A", ["x", "y"])
        b = S.ErrorSet("m.B", ["z"])
        c = S.ErrorSet("m.C", ["w"], underlying="U8")
        ab = S.ErrorSet("m.AB", operands=[a, b])
        abc = S.ErrorSet("m.ABC", operands=[ab, b, c, a], underlying="U32")
        self.assertEqual([m.name for m in abc.members()], ["m.A", "m.B", "m.C"])
        data = S.encode(S.Signature(0, [("in", S.ErrorValue(ab))], S.ErrorUnion(abc, S.Tuple(["I64", "Bool"]))))
        self.assertEqual(data, body(
            "05000000",
            "73", name("m.A"), "22 00 02000000", name("x"), name("y"),
            "73", name("m.B"), "22 00 01000000", name("z"),
            "73", name("m.AB"), "22 01 02000000 00000000 01000000",
            "73", name("m.C"), "21 00 01000000", name("w"),
            "73", name("m.ABC"), "23 01 03000000 00000000 01000000 03000000",
            "00000000 01000000", "00 73 02000000",
            "74 04000000 76 02000000 14 01"))
        # E!void, and a built-in set named alone (SPEC-09 CONF-11 rule 12).
        arith = S.ErrorSet("ArithError", ["overflow"])
        self.assertEqual(S.encode(S.Signature(0, [], S.ErrorUnion(arith))),
                         body("01000000", "73", name("ArithError"), "22 00 01000000", name("overflow"),
                              "00000000 00000000", "74 00000000 00"))

    def test_error_set_rules(self):
        big = S.ErrorSet("m.Big", ["v%d" % i for i in range(255)], underlying="U8")
        S.encode(S.Signature(0, [("in", S.ErrorValue(big))]))         # 255 values fit U8
        half = [S.ErrorSet("m.H%d" % k, ["v%d" % i for i in range(200)], underlying="U8") for k in (0, 1)]
        cases = [S.ErrorSet("m.Big", ["v%d" % i for i in range(256)], underlying="U8"),
                 S.ErrorSet("m.Both", operands=half, underlying="U8"),   # 400 values, members together
                 S.ErrorSet("m.E", ["a"], underlying="I16"), S.ErrorSet("m.E", ["a", "a"]),
                 S.ErrorSet("m.E"), S.ErrorSet("m.E", ["a"], operands=[big]), S.ErrorSet("m.E", operands=[])]
        for bad in cases:
            with self.subTest(bad=bad.name), self.assertRaises(ValueError):
                S.encode(S.Signature(0, [("in", S.ErrorValue(bad))]))

    def test_positions(self):
        e = S.ErrorSet("m.E", ["a"])
        cases = [S.Signature(0, [("in", S.Tuple(["I64", "I64"]))]),               # a tuple parameter
                 S.Signature(0, [], S.Tuple(["I64"])),                             # one element
                 S.Signature(0, [], S.Tuple([S.Tuple(["I8", "I8"]), "I64"])),      # nested
                 S.Signature(0, [("in", S.ErrorUnion(e, "I64"))]),                 # a union parameter
                 S.Signature(0, [], S.ErrorUnion(e, S.ErrorUnion(e, "I64"))),
                 S.Signature(0, [], S.Tuple([S.ErrorUnion(e), "I64"])),
                 S.Signature(0, [("in", S.Struct("m.S", [S.Field("t", S.Tuple(["I8", "I8"]))]))]),
                 S.Signature(0, [("in", "Str")]), S.Signature(0, [("in", "PT5")])]  # forms of A-25
        for sig in cases:
            with self.subTest(sig=sig), self.assertRaises(ValueError):
                S.encode(sig)
        # A tuple of structs as the success type of E!T: every position admits a struct.
        st = S.Struct("m.S", [S.Field("v", S.Fixed("I32", 16))])
        data = S.encode(S.Signature(0, [], S.ErrorUnion(e, S.Tuple([st, "T27"]))))
        self.assertEqual(data, body("02000000", "73", name("m.E"), "22 00 01000000", name("a"),
                                    "71", name("m.S"), "00 01000000", name("v"), "00 31 13 1000",
                                    "00000000 00000000", "74 00000000 76 02000000 71 01000000 42"))


class Programs(unittest.TestCase):
    """cint_ref's checked programs as signatures: each export of the root module (A-12, A-19)."""

    def test_imported_struct_has_its_module_name(self):
        with tempfile.TemporaryDirectory() as root:
            (pathlib.Path(root) / "geo").mkdir()
            (pathlib.Path(root) / "geo" / "vec.ci").write_bytes(
                b"export struct Body { I64 id; I32[3] pos; }\nexport I64 unit() { return 1; }\n")
            src = ("import geo.vec as v;\n\nexport I64 mass(in v.Body[_] bodies, v.Body b) {\n"
                   "    return bodies[0].id + b.id + v.unit();\n}\n")
            got = exports(src, "main.ci", root)
        body_def = S.Struct("geo.vec.Body", [S.Field("id", "I64"), S.Field("pos", S.Array("I32", [S.literal(3)]))])
        want = S.encode(S.Signature(0, [("in", S.Array(body_def, [S.ANY])), ("in", body_def)], "I64"))
        self.assertEqual(list(got), ["mass"])       # only the root module's exports
        self.assertEqual(got["mass"].signature, want)
        self.assertIn(b"\x0c\x00\x00\x00geo.vec.Body", want)

    def test_kernel_shapes_and_constant_extents(self):
        src = """const I64 N = 3;
export kernel diff[n](in I64[n + 1] a, in I64[n - 1] e, in U8[4] c, out I64[n] d, out I64 total) over [i: n] {
    I64 v = a[i + 1] - a[i];
    d[i] = v;
    reduce total = sum(v);
}
I64 hidden(I64 x) { return x; }
export I64 first(in I64[2 * 4] a, inout U8[N] b) { return a[0]; }
export kernel grid[h, w](in I32[h, w] u, out I32[h, w] v) {
    v = u;
}
"""
        got = exports(src)
        self.assertEqual([(e.name, e.kind) for e in got.values()],
                         [("diff", "kernel"), ("first", "function"), ("grid", "kernel")])
        n = S.symbol(0)
        self.assertEqual(got["diff"].signature, S.encode(S.Signature(1, [
            ("in", S.Array("I64", [S.symbol(0, 1)])), ("in", S.Array("I64", [S.symbol(0, -1)])),
            ("in", S.Array("U8", [S.literal(4)])), ("out", S.Array("I64", [n])), ("out", "I64")])))
        # A constant extent, a constant expression or a named constant, is (none, its value).
        self.assertEqual(got["first"].signature, S.encode(S.Signature(0, [
            ("in", S.Array("I64", [S.literal(8)])), ("inout", S.Array("U8", [S.literal(3)]))], "I64")))
        self.assertEqual(got["grid"].signature, S.encode(S.Signature(2, [
            ("in", S.Array("I32", [n, S.symbol(1)])), ("out", S.Array("I32", [n, S.symbol(1)]))])))

    def test_a_renamed_parameter_keeps_the_signature_and_a_renamed_field_does_not(self):
        # A-22: parameter names are fields of the table row, field names part of the type.
        base = exports(BOTH_SRC, "public_records.ci")["both"].signature
        renamed = BOTH_SRC.replace("inout Simple a", "inout Simple first").replace("a.value", "first.value")
        self.assertEqual(exports(renamed, "public_records.ci")["both"].signature, base)
        field = BOTH_SRC.replace("I64 value;", "I64 amount;").replace(".value", ".amount")
        self.assertNotEqual(exports(field, "public_records.ci")["both"].signature, base)

    def test_a_compile_error_raises(self):
        with self.assertRaises(CompileError):
            exports("export I64 f(I64 x) { return y; }\n")

    def test_sig_command(self):
        with tempfile.TemporaryDirectory() as root:
            path = pathlib.Path(root) / "public_records.ci"
            path.write_bytes((BOTH_SRC + "\nexport I64 one() { return 1; }\n").encode("ascii"))
            with contextlib.redirect_stdout(io.TextIOWrapper(io.BytesIO(), write_through=True)) as w:
                code = cli(["sig", str(path), "--path", "public_records.ci"])
                text = w.buffer.getvalue().decode("ascii")
            self.assertEqual(code, 0, text)
            self.assertEqual(text, "both %s\none %s\n" % (listing(BOTH_LISTING).hex(),
                                                           body("00000000 00000000 00000000 14").hex()))
            path.write_bytes(b"export I64 f(I64 x) { return y; }\n")
            with contextlib.redirect_stdout(io.TextIOWrapper(io.BytesIO(), write_through=True)) as w:
                code = cli(["sig", str(path), "--path", "public_records.ci"])
                text = w.buffer.getvalue().decode("ascii")
            self.assertEqual((code, text), (1, "compile-error C3005 public_records.ci:1:30\n"))


if __name__ == "__main__":
    unittest.main()
