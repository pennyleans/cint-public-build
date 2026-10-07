"""The canonical type signature (SPEC-03 A-20 to A-24; BX10-23, BX10-27).

The five examples of SPEC-03 A-26 are reproduced from their listings or,
for the two given by length and hash only, from their declarations through
the model and the encoder. Each obligation of A-24 has a case that breaks it.
"""
import hashlib
import struct
import unittest

from cint import _sig
from cint._sig import (Array, Bound, Dim, EnumDef, EnumRef, ErrorSetDef, ErrorUnion, ErrorValue, Field, Parameter,
                       Scalar, Signature, SignatureError, StructDef, StructRef, Tuple)

I64, U8, BOOL = Scalar("I64"), Scalar("U8"), Scalar("Bool")


def u32(n: int) -> bytes:
    return struct.pack("<I", n)


def i64(n: int) -> bytes:
    return struct.pack("<q", n)


def name(text: str) -> bytes:
    return u32(len(text)) + text.encode("ascii")


def h(*parts) -> bytes:
    """Bytes from hex strings and raw bytes, after the 33-byte prefix."""
    return _sig.PREFIX + b"".join(bytes.fromhex(p) if isinstance(p, str) else p for p in parts)


def n_dim(symbol: int = 0, offset: int = 0) -> Dim:
    return Dim(1, bound=Bound(symbol, offset))


DIM_N = "01 00000000 0000000000000000"
ADVANCE = h("00000000", "01000000 04000000",
            "00 52 14 01", DIM_N, "00 52 14 01", DIM_N, "00 14", "02 52 14 01", DIM_N, "00")
BOTH = h("01000000", "71", name("public_records.Simple"), "00 02000000",
         name("value"), "00 14", name("flag"), "00 01",
         "00000000 03000000", "01 71 00000000", "00 71 00000000", "00 14", "71 00000000")
READ_NUMBER = h("03000000",
                "73", name("parse.IoError"), "22 00 02000000", name("closed"), name("full"),
                "73", name("parse.ParseError"), "22 00 03000000", name("empty"), name("bad_digit"), name("too_large"),
                "73", name("parse.IoOrParse"), "22 01 02000000 00000000 01000000",
                "00000000 01000000", "00 52 21 01 00", "74 02000000 14")
PARSE_ERROR = ErrorSetDef("parse.ParseError", "U16", ("empty", "bad_digit", "too_large"))
PAIRED = Signature((), 2, (Parameter("inout", Array(I64, (n_dim(0),))), Parameter("in", I64),
                           Parameter("in", Array(I64, (n_dim(0),))), Parameter("in", Array(U8, (n_dim(1),))),
                           Parameter("in", Array(U8, (n_dim(1),)))), I64)
PARSE_U = Signature((PARSE_ERROR,), 0, (Parameter("in", Array(U8, (Dim(0),))),), ErrorUnion(0, I64))


class Examples(unittest.TestCase):
    """SPEC-03 A-26: the bytes, lengths, and SHA-256 of the five examples."""

    def check(self, data: bytes, length: int, digest: str):
        self.assertEqual(len(data), length)
        self.assertEqual(hashlib.sha256(data).hexdigest(), digest)
        sig = _sig.decode(data)
        self.assertEqual(_sig.encode(sig), data)
        return sig

    def test_prefix(self):
        self.assertEqual(_sig.PREFIX, bytes.fromhex("1d000000") + b"cint-core-1/type-signature/v1")
        self.assertEqual(len(_sig.PREFIX), 33)

    def test_advance(self):
        sig = self.check(ADVANCE, 99, "f17b75a6e89651dd40ca58ad7fa76bfe39eba562c520e561ce852974e993444e")
        self.assertEqual(sig.size_count, 1)
        self.assertEqual([p.mode for p in sig.params], ["in", "in", "in", "out"])
        self.assertIsNone(sig.result)
        self.assertEqual(_sig.render(sig, "advance", ("n",), ("pos", "vel", "dt", "next"), kernel=True),
                         "kernel advance[n](in I64[n] pos, in I64[n] vel, in I64 dt, out I64[n] next)")
        params, result = _sig.python_forms(sig, ("pos", "vel", "dt", "next"))
        self.assertEqual([(p.name, p.mode, p.elem, p.rank) for p in params],
                         [("pos", "in", "I64", 1), ("vel", "in", "I64", 1), ("dt", "in", "I64", 0),
                          ("next", "out", "I64", 1)])
        self.assertIsNone(result)

    def test_both(self):
        sig = self.check(BOTH, 116, "e604b42aa501289f82feba1ba82519b757db691ffef9b1aa0ddf737b3d281154")
        self.assertEqual(sig.definitions, (StructDef("public_records.Simple", False,
                                                     (Field("value", 0, I64), Field("flag", 0, BOOL))),))
        self.assertEqual(_sig.render(sig, "both", (), ("a", "b", "divisor")),
                         "public_records.Simple both(inout public_records.Simple a, public_records.Simple b, "
                         "I64 divisor)")
        with self.assertRaisesRegex(_sig.NoPythonForm, r"parameter a is struct public_records\.Simple.*BX10-27"):
            _sig.python_forms(sig, ("a", "b", "divisor"))

    def test_read_number(self):
        sig = self.check(READ_NUMBER, 194, "9296f1731a406591620b575fcc8b0f7d92d2a90b4467f53fc1ed114521b03f98")
        self.assertTrue(sig.definitions[2].combined)
        self.assertEqual(sig.definitions[2].members, (0, 1))
        self.assertEqual(_sig.render(sig, "read_number", (), ("s",)), "parse.IoOrParse!I64 read_number(in U8[_] s)")
        self.assertEqual(_sig.result_form(sig), ("error", 2, "I64"))
        self.assertEqual(_sig.error_sets(sig), {
            "parse.IoError": ("declared", ("closed", "full")),
            "parse.ParseError": ("declared", ("empty", "bad_digit", "too_large")),
            "parse.IoOrParse": ("combined", ("parse.IoError", "parse.ParseError"))})

    def test_paired(self):
        data = _sig.encode(PAIRED)
        sig = self.check(data, 116, "a0f444fd666940950bfdab92e6d113a42d45949a5794269f8494e33b6a114993")
        self.assertEqual(_sig.render(sig, "paired", ("n", "m"), ("a", "bias", "b", "bytes", "more")),
                         "I64 paired[n, m](inout I64[n] a, I64 bias, in I64[n] b, in U8[m] bytes, in U8[m] more)")

    def test_parse_u(self):
        data = _sig.encode(PARSE_U)
        self.check(data, 118, "1576731f670cdfbfd24094f188a6c49648188aa95b4da168d704d29080bef7b2")
        # The definition of ParseError alone, and read_number's parameter and result with index 0.
        self.assertEqual(data[37:37 + 62], READ_NUMBER[33 + 4 + 42:33 + 4 + 42 + 62])
        self.assertEqual(data[37 + 62 + 8:], READ_NUMBER[-11:-6] + bytes.fromhex("74 00000000 14"))


def sig_bytes(defs=(), size_count=0, params=(), result=None) -> bytes:
    return _sig.encode(Signature(tuple(defs), size_count, tuple(params), result))


class Obligations(unittest.TestCase):
    """SPEC-03 A-24: one case for each obligation, each rejected with its offset."""

    def rejects(self, data: bytes, pattern: str):
        with self.assertRaisesRegex(SignatureError, pattern) as caught:
            _sig.decode(data)
        self.assertIsInstance(caught.exception, ValueError)
        self.assertTrue(0 <= caught.exception.offset <= len(data))
        self.assertIn("A-24", str(caught.exception))
        return caught.exception

    def test_valid_baseline(self):
        _sig.decode(sig_bytes(params=[Parameter("in", I64)], result=I64))

    def test_domain(self):
        good = sig_bytes(result=I64)
        self.rejects(b"\x1d\x00\x00\x00cint-core-1/type-signature/v2" + good[33:], "domain")
        self.rejects(b"\x1c\x00\x00\x00cint-core-1/type-signature/v" + good[33:], "domain")
        self.rejects(b"", "domain|truncated")

    def test_unknown_tag(self):
        self.rejects(h("00000000 00000000 01000000", "00 99", "00"), "tag")
        self.rejects(h("00000000 00000000 00000000", "51"), "tag")

    def test_unknown_mode(self):
        self.rejects(h("00000000 00000000 01000000", "03 14", "00"), "mode")

    def test_unknown_shape_form(self):
        self.rejects(h("00000000 01000000 01000000", "00 52 14 01 04", "00"), "form")

    def test_unknown_layout(self):
        self.rejects(h("01000000", "71", name("m.S"), "02 01000000", name("a"), "00 14",
                       "00000000 01000000 00 71 00000000 00"), "layout")

    def test_unknown_set_form(self):
        self.rejects(h("01000000", "73", name("m.E"), "21 02 01000000", name("a"),
                       "00000000 00000000 74 00000000 00"), "form")

    def test_position(self):
        tup = Tuple((I64, I64))
        self.rejects(sig_bytes(params=[Parameter("in", tup)]), "tuple.*parameter|position|not allowed")
        self.rejects(sig_bytes(params=[Parameter("in", ErrorUnion(0, I64))],
                               defs=[ErrorSetDef("m.E", "U8", ("a",))]), "not allowed|position")
        self.rejects(sig_bytes(result=Tuple((I64, tup))), "not allowed|position")
        self.rejects(sig_bytes(result=Array(I64, (Dim(0),))), "not allowed|position")

    def test_array_of_array(self):
        self.rejects(sig_bytes(params=[Parameter("in", Array(Array(I64, (Dim(0),)), (Dim(0),)))]),
                     "array")

    def test_field_array_extents(self):
        for dim in (Dim(0), n_dim(0)):
            d = StructDef("m.S", False, (Field("a", 0, Array(I64, (dim,))),))
            self.rejects(sig_bytes([d], 1, [Parameter("in", StructRef(0))]), "literal|extent|field")
        d = StructDef("m.S", False, (Field("a", 0, Array(I64, (Dim(1, bound=Bound(None, 4)),))),))
        _sig.decode(sig_bytes([d], 0, [Parameter("in", StructRef(0))]))

    def test_short_tuple(self):
        self.rejects(sig_bytes(result=Tuple((I64,))), "tuple")
        self.rejects(sig_bytes(result=Tuple(())), "tuple")

    def test_fixed_point(self):
        self.rejects(h("00000000 00000000 00000000", "31 21 0100"), "storage")
        self.rejects(h("00000000 00000000 00000000", "31 11 0800"), "f |frac|width")
        _sig.decode(h("00000000 00000000 00000000", "31 11 0700"))

    def test_rank(self):
        self.rejects(h("00000000 00000000 01000000", "00 52 14 00", "00"), "rank")
        self.rejects(h("00000000 00000000 01000000", "00 52 14 05", "00 00 00 00 00", "00"), "rank")

    def test_size_symbol(self):
        self.rejects(sig_bytes(size_count=1, params=[Parameter("in", Array(I64, (n_dim(1),)))]), "symbol")
        _sig.decode(sig_bytes(size_count=2, params=[Parameter("in", Array(I64, (n_dim(1),)))]))

    def test_forward_reference(self):
        self.rejects(sig_bytes([StructDef("m.S", False, (Field("a", 0, StructRef(0)),))], 0,
                               [Parameter("in", StructRef(0))]), "index|definition")
        self.rejects(sig_bytes([], 0, [Parameter("in", StructRef(0))]), "index|definition")

    def test_reference_kind(self):
        e = EnumDef("m.E", "U8", (("a", 0),))
        self.rejects(sig_bytes([e], 0, [Parameter("in", StructRef(0))]), "kind|struct")

    def test_combined_members(self):
        a = ErrorSetDef("m.A", "U8", ("x",))
        b = ErrorSetDef("m.B", "U8", members=(0,))
        c = ErrorSetDef("m.C", "U8", members=(0, 1))
        self.rejects(sig_bytes([a, b, c], 0, [], ErrorUnion(2, None)), "declared")
        self.rejects(sig_bytes([a, ErrorSetDef("m.D", "U8", members=(0, 0))], 0, [], ErrorUnion(1, None)), "twice")

    def test_unreferenced_definition(self):
        self.rejects(sig_bytes([EnumDef("m.E", "U8", (("a", 0),))], 0, [], I64), "refer")

    def test_duplicate_definition(self):
        e = EnumDef("m.E", "U8", (("a", 0),))
        self.rejects(sig_bytes([e, e], 0, [Parameter("in", EnumRef(0)), Parameter("in", EnumRef(1))]), "twice|two")
        # One name in two kinds is allowed.
        s = StructDef("m.E", False, (Field("a", 0, I64),))
        _sig.decode(sig_bytes([e, s], 0, [Parameter("in", EnumRef(0)), Parameter("in", StructRef(1))]))

    def test_underlying_tags(self):
        self.rejects(h("01000000", "72", name("m.E"), "01 01000000", name("a"), "00",
                       "00000000 01000000 00 72 00000000 00"), "underlying")
        self.rejects(h("01000000", "73", name("m.E"), "11 00 01000000", name("a"),
                       "00000000 00000000 74 00000000 00"), "underlying")

    def test_set_capacity(self):
        values = tuple("v%d" % i for i in range(256))
        self.rejects(sig_bytes([ErrorSetDef("m.E", "U8", values)], 0, [], ErrorUnion(0, None)), "more values|number")
        _sig.decode(sig_bytes([ErrorSetDef("m.E", "U8", values[:255])], 0, [], ErrorUnion(0, None)))
        a = ErrorSetDef("m.A", "U8", values[:200])
        b = ErrorSetDef("m.B", "U8", tuple("w%d" % i for i in range(100)))
        self.rejects(sig_bytes([a, b, ErrorSetDef("m.C", "U8", members=(0, 1))], 0, [], ErrorUnion(2, None)),
                     "more values|number")
        _sig.decode(sig_bytes([a, b, ErrorSetDef("m.C", "U16", members=(0, 1))], 0, [], ErrorUnion(2, None)))

    def test_duplicate_names_and_values(self):
        s = StructDef("m.S", False, (Field("a", 0, I64), Field("a", 0, BOOL)))
        self.rejects(sig_bytes([s], 0, [Parameter("in", StructRef(0))]), "twice|two")
        e = EnumDef("m.E", "U8", (("a", 0), ("a", 1)))
        self.rejects(sig_bytes([e], 0, [Parameter("in", EnumRef(0))]), "twice|two")
        e = EnumDef("m.E", "U8", (("a", 0), ("b", 0)))
        self.rejects(sig_bytes([e], 0, [Parameter("in", EnumRef(0))]), "value")
        x = ErrorSetDef("m.X", "U8", ("a", "a"))
        self.rejects(sig_bytes([x], 0, [], ErrorUnion(0, None)), "twice|two")

    def test_bit_widths(self):
        def struct_with(field):
            return sig_bytes([StructDef("m.S", True, (field,))], 0, [Parameter("in", StructRef(0))])
        self.rejects(struct_with(Field("a", 9, Scalar("I8"))), "bit width")
        self.rejects(struct_with(Field("a", 2, BOOL)), "Bool|bit width")
        self.rejects(struct_with(Field("a", 1, Array(Scalar("I8"), (Dim(1, bound=Bound(None, 2)),)))),
                     "bit width|array")
        inner = StructDef("m.T", False, (Field("x", 0, I64),))
        self.rejects(sig_bytes([inner, StructDef("m.S", True, (Field("a", 3, StructRef(0)),))], 0,
                               [Parameter("in", StructRef(1))]), "bit width|struct")
        _sig.decode(struct_with(Field("a", 8, Scalar("I8"))))
        _sig.decode(struct_with(Field("a", 1, BOOL)))

    def test_identifiers(self):
        self.rejects(sig_bytes([StructDef("m.S", False, (Field("1a", 0, I64),))], 0,
                               [Parameter("in", StructRef(0))]), "identifier")
        self.rejects(sig_bytes([StructDef("m..S", False, (Field("a", 0, I64),))], 0,
                               [Parameter("in", StructRef(0))]), "qualified")
        self.rejects(sig_bytes([StructDef("m.S", False, (Field("a" * 256, 0, I64),))], 0,
                               [Parameter("in", StructRef(0))]), "identifier")
        _sig.decode(sig_bytes([StructDef("ArithError", False, (Field("a" * 255, 0, I64),))], 0,
                              [Parameter("in", StructRef(0))]))

    def test_counts_truncation_and_trailing(self):
        good = sig_bytes(params=[Parameter("in", I64)], result=I64)
        self.rejects(h("ffffffff"), "count|remaining")
        self.rejects(h("00000000 00000000 ffffff7f"), "count|remaining")
        self.rejects(good[:-1], "truncated|remaining")
        self.rejects(good + b"\x00", "trailing")
        for cut in range(len(good)):
            with self.assertRaises(SignatureError):
                _sig.decode(good[:cut])


class Forms(unittest.TestCase):
    """The Python forms of BX10-27 and the rendering of a declaration."""

    def test_scalars_and_arrays(self):
        sig = Signature((), 1, (Parameter("in", Scalar("Q32.32")), Parameter("inout", Array(Scalar("I8"), (n_dim(),))),
                                Parameter("in", Scalar("T27"))), Scalar("U16"))
        params, result = _sig.python_forms(_sig.decode(_sig.encode(sig)), ("q", "a", "t"))
        self.assertEqual([(p.mode, p.elem, p.rank) for p in params],
                         [("in", "Q32.32", 0), ("inout", "I8", 1), ("in", "T27", 0)])
        self.assertEqual(result, "U16")

    def test_results(self):
        e = ErrorSetDef("m.E", "U8", ("bad",))
        self.assertEqual(_sig.result_form(Signature((), 0, (), Tuple((I64, BOOL)))), ("I64", "Bool"))
        self.assertEqual(_sig.result_form(Signature((e,), 0, (), ErrorUnion(0, None))), ("error", 0, None))
        self.assertEqual(_sig.result_form(Signature((e,), 0, (), ErrorUnion(0, Tuple((I64, I64))))),
                         ("error", 0, ("I64", "I64")))
        self.assertIsNone(_sig.result_form(Signature((), 0, (), None)))

    def test_no_form(self):
        e = ErrorSetDef("m.E", "U8", ("bad",))
        s = StructDef("m.S", False, (Field("a", 0, I64),))
        en = EnumDef("m.K", "I32", (("a", -1),))
        cases = [
            (Signature((s,), 0, (), StructRef(0)), r"result is struct m\.S"),
            (Signature((en,), 0, (Parameter("in", EnumRef(0)),), None), r"parameter p is enum m\.K"),
            (Signature((e,), 0, (Parameter("in", ErrorValue(0)),), None), r"parameter p is a value of error set m\.E"),
            (Signature((s,), 0, (Parameter("in", Array(StructRef(0), (Dim(0),))),), None), r"parameter p"),
            (Signature((e,), 0, (), ErrorUnion(0, ErrorValue(0))), r"success type"),
            (Signature((s,), 0, (), Tuple((I64, StructRef(0)))), r"result"),
        ]
        for sig, pattern in cases:
            sig = _sig.decode(_sig.encode(sig))
            with self.subTest(pattern):
                with self.assertRaisesRegex(_sig.NoPythonForm, pattern + ".*BX10-27"):
                    _sig.python_forms(sig, ("p",) * len(sig.params))

    def test_render_shapes(self):
        sig = Signature((), 2, (Parameter("in", Array(I64, (Dim(2, 1, Bound(0, 0)), Dim(3, 0, Bound(1, -1)),
                                                             Dim(1, bound=Bound(0, 1)), Dim(1, bound=Bound(None, 8))))),),
                        None)
        self.assertEqual(_sig.render(_sig.decode(_sig.encode(sig)), "f", ("n", "m"), ("a",)),
                         "void f[n, m](in I64[1..=n, 0..m - 1, n + 1, 8] a)")


if __name__ == "__main__":
    unittest.main()
