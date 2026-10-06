import os, sys; sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # put ref/ on the path
"""The canonical fault record, version 2, and its reader (SPEC-01 IM-145 to IM-149;
slice 2 patch D-10 and D-17 with OQ-147, decision 23), and `.expect`
format 2 (SPEC-09 CONF-11).

The reader cases of D-17 are written out as the patch lists them, each passing
the record kind: with the compile-time kind, the 376-byte operand is accepted,
an operand of 514 bytes is rejected, the 520-byte OQ-147 `exact` is accepted and
an `exact` of 521 bytes is rejected; with the run-time kind, 258 bytes in either
field and the same 520-byte `exact` are rejected. No slice 2 program produces a
520-byte value, so its bytes are synthesized."""
import unittest

from cint_ref import expect
from cint_ref.exec import run_program
from cint_ref.faults import (COMPILE_TIME, RUN_TIME, FaultRecord, Position, decode_fault_record,
                             encode_fault_record, z_bytes)
from cint_ref.types import Value

I64MAX = (1 << 63) - 1

# The CONF-01 record (SPEC-09 CONF-01), version 2, derived by hand: domain, code,
# operation, two operands, exact, limit, position, then presence bytes 00 for the
# revision, the source-map digest and the address, and stack count 0. 136 bytes.
CONF01_V2 = bytes.fromhex(
    "14000000" "63696e742d636f72652d312f6661756c742f7632" "0100"
    "0f000000" "6164642e636865636b65642e693634" "02000000"
    "14ffffffffffffff7f" "140100000000000000"
    "01" "0f09000000" "000000000000008000" "01" "14ffffffffffffff7f"
    "19000000" "61726974682f6164645f6936345f6f766572666c6f772e6369" "05000000" "0f000000"
    "00" "00" "00" "00000000")
# Version 1, which cint-rt-1 wrote: domain .../v1 and 32 zero bytes for the revision. 166 bytes.
CONF01_V1 = CONF01_V2[:4] + b"cint-core-1/fault/v1" + CONF01_V2[24:-7] + bytes(32) + b"\x00" + bytes(4)


def conf01() -> FaultRecord:
    return FaultRecord("E_OVERFLOW", "add.checked.i64", (Value("I64", I64MAX), Value("I64", 1)), 1 << 63,
                       Value("I64", I64MAX), Position("arith/add_i64_overflow.ci", 5, 15))


def z_field(n: int, payload: bytes) -> bytes:
    """A tagged Z with an explicit U32 length."""
    return b"\x0f" + n.to_bytes(4, "little") + payload


def narrow_record(operand: bytes, exact: bytes) -> bytes:
    """A C6001 `E_NARROW` record (operation `unassigned`, limit I8 127) with the given tagged
    operand and exact, built by hand so that lengths the encoder refuses can be written."""
    head = b"\x14\x00\x00\x00cint-core-1/fault/v2" + (6).to_bytes(2, "little") + \
        (10).to_bytes(4, "little") + b"unassigned" + (1).to_bytes(4, "little")
    path = b"arith/x.ci"
    tail = b"\x01\x11\x7f" + len(path).to_bytes(4, "little") + path + (8).to_bytes(4, "little") + \
        (762).to_bytes(4, "little") + b"\x00\x00\x00" + bytes(4)
    return head + operand + b"\x01" + exact + tail


# D-17: Z 2^3000 is `0f 78 01 00 00`, 375 bytes 00, and 01 (376 bytes).
Z_2P3000 = z_field(376, bytes(375) + b"\x01")
# D-17: the OQ-147 maximum (2^4096 - 1) * 2^63: `0f 08 02 00 00`, 7 bytes 00, 80, 511 bytes ff, 7f.
Z_EXACT_520 = z_field(520, bytes(7) + b"\x80" + b"\xff" * 511 + b"\x7f")
Z_514 = z_field(514, bytes(513) + b"\x01")        # minimal, 8 * 513 + 1 magnitude bits
Z_521 = z_field(521, bytes(520) + b"\x01")
Z_258 = z_field(258, bytes(257) + b"\x01")
Z_SMALL = z_field(1, b"\x05")


class Conf01Bytes(unittest.TestCase):
    def test_v2_encoding_is_the_hand_derivation(self):
        self.assertEqual(len(CONF01_V2), 136)
        self.assertEqual(encode_fault_record(conf01(), RUN_TIME), CONF01_V2)
        self.assertEqual(decode_fault_record(CONF01_V2, RUN_TIME), (2, conf01()))

    def test_v1_stays_readable(self):
        self.assertEqual(len(CONF01_V1), 166)
        version, rec = decode_fault_record(CONF01_V1, RUN_TIME)
        self.assertEqual((version, rec.code, rec.revision), (1, "E_OVERFLOW", bytes(32)))
        self.assertEqual(encode_fault_record(rec, RUN_TIME, 1), CONF01_V1)

    def test_presence_bytes(self):
        r = conf01()
        r.revision, r.source_map = bytes(range(32)), bytes(range(32, 64))
        b = encode_fault_record(r, RUN_TIME)
        self.assertEqual(b[-71:-4], b"\x01" + bytes(range(32)) + b"\x01" + bytes(range(32, 64)) + b"\x00")
        self.assertEqual(decode_fault_record(b, RUN_TIME), (2, r))
        for bad in (b"\x02", b"\xff"):           # a presence byte is 00 or 01
            with self.assertRaises(ValueError):
                decode_fault_record(CONF01_V2[:-7] + bad + CONF01_V2[-6:], RUN_TIME)

    def test_z_table_of_im148(self):
        self.assertEqual(z_bytes(1 << 63), bytes.fromhex("000000000000008000"))
        self.assertEqual(z_bytes(-1), b"\xff")
        self.assertEqual(z_bytes(0), b"")
        self.assertEqual(z_bytes(-128), b"\x80")
        self.assertEqual(z_bytes(128), b"\x80\x00")


class RecordKindBounds(unittest.TestCase):
    """The D-17 reader cases (patch section 5.1, as corrected in 5.12)."""

    def test_compile_time_accepts_the_376_byte_operand(self):
        version, rec = decode_fault_record(narrow_record(Z_2P3000, Z_2P3000), COMPILE_TIME)
        self.assertEqual(rec.operands, (Value("Z", 1 << 3000),))
        self.assertEqual(rec.exact, 1 << 3000)

    def test_compile_time_rejects_an_operand_of_514_bytes(self):
        with self.assertRaisesRegex(ValueError, "514 bytes exceeds the bound 513"):
            decode_fault_record(narrow_record(Z_514, Z_SMALL), COMPILE_TIME)

    def test_compile_time_accepts_the_520_byte_exact(self):
        _, rec = decode_fault_record(narrow_record(Z_SMALL, Z_EXACT_520), COMPILE_TIME)
        self.assertEqual(rec.exact, ((1 << 4096) - 1) << 63)

    def test_the_negative_520_byte_exact_ends_00_80(self):
        self.assertEqual(z_bytes(-(((1 << 4096) - 1) << 63))[-2:], b"\x00\x80")
        self.assertEqual(len(z_bytes(-(((1 << 4096) - 1) << 63))), 520)

    def test_compile_time_rejects_an_exact_of_521_bytes(self):
        with self.assertRaisesRegex(ValueError, "521 bytes exceeds the bound 520"):
            decode_fault_record(narrow_record(Z_SMALL, Z_521), COMPILE_TIME)

    def test_compile_time_operand_bound_is_not_the_exact_bound(self):
        # A single 520-byte bound is not used (IM-108): 514 to 520 bytes is never an operand.
        with self.assertRaises(ValueError):
            decode_fault_record(narrow_record(Z_EXACT_520, Z_SMALL), COMPILE_TIME)

    def test_run_time_rejects_258_bytes_in_either_field(self):
        with self.assertRaisesRegex(ValueError, "258 bytes exceeds the bound 257"):
            decode_fault_record(narrow_record(Z_258, Z_SMALL), RUN_TIME)
        with self.assertRaisesRegex(ValueError, "258 bytes exceeds the bound 257"):
            decode_fault_record(narrow_record(Z_SMALL, Z_258), RUN_TIME)

    def test_one_byte_string_under_both_kinds(self):
        b = narrow_record(Z_SMALL, Z_EXACT_520)
        self.assertEqual(decode_fault_record(b, COMPILE_TIME)[0], 2)
        with self.assertRaisesRegex(ValueError, "520 bytes exceeds the bound 257"):
            decode_fault_record(b, RUN_TIME)

    def test_version_1_bounds_every_z_at_257(self):
        b = narrow_record(Z_2P3000, Z_SMALL).replace(b"fault/v2", b"fault/v1")
        b = b[:-7] + bytes(32) + b"\x00" + bytes(4)        # v1: the revision is 32 bytes
        with self.assertRaisesRegex(ValueError, "376 bytes exceeds the bound 257"):
            decode_fault_record(b, COMPILE_TIME)

    def test_the_kind_is_required(self):
        for kind in (None, "compile", "", "runtime"):
            with self.assertRaises(ValueError):
                decode_fault_record(CONF01_V2, kind)
            with self.assertRaises(ValueError):
                encode_fault_record(conf01(), kind)

    def test_the_encoder_keeps_the_same_bounds(self):
        r = FaultRecord("E_NARROW", "unassigned", (Value("Z", -((1 << 4096) - 1)),), -((1 << 4096) - 1),
                        Value("I8", -128), Position("arith/const_narrow_literal_max.ci", 8, 1036))
        b = encode_fault_record(r, COMPILE_TIME)
        self.assertEqual(decode_fault_record(b, COMPILE_TIME), (2, r))
        with self.assertRaises(ValueError):
            encode_fault_record(r, RUN_TIME)
        with self.assertRaises(ValueError):
            decode_fault_record(b, RUN_TIME)


class DecoderObligations(unittest.TestCase):
    """IM-148: each kind of malformed input is rejected with ValueError."""

    def reject(self, data, kind=RUN_TIME):
        with self.assertRaises(ValueError):
            decode_fault_record(data, kind)

    def test_truncation_and_trailing_bytes(self):
        for n in range(len(CONF01_V2)):
            self.reject(CONF01_V2[:n])
        self.reject(CONF01_V2 + b"\x00")

    def test_non_minimal_z(self):
        exact_at = CONF01_V2.index(bytes.fromhex("010f09000000"))
        nonminimal = bytes.fromhex("010f0a000000") + bytes.fromhex("00000000000000800000")
        self.reject(CONF01_V2[:exact_at] + nonminimal + CONF01_V2[exact_at + 15:])
        self.reject(narrow_record(z_field(1, b"\x00"), Z_SMALL), COMPILE_TIME)    # zero has n = 0
        self.reject(narrow_record(z_field(2, b"\xff\xff"), Z_SMALL), COMPILE_TIME)

    def test_unknown_tags_and_misplaced_tags(self):
        limit_at = CONF01_V2.index(bytes.fromhex("0114ffffffffffffff7f19"))
        for tag in (0x00, 0x02, 0x19, 0x25, 0x41, 0x43, 0x71, 0x61, 0x7F, 0xFF):
            self.reject(CONF01_V2[:limit_at + 1] + bytes([tag]) + CONF01_V2[limit_at + 2:])

    def test_invalid_bool_and_q_descriptors(self):
        self.reject(narrow_record(b"\x01\x02", Z_SMALL), COMPILE_TIME)
        self.reject(narrow_record(b"\x31\x15\x00\x00" + bytes(16), Z_SMALL), COMPILE_TIME)  # storage I128
        self.reject(narrow_record(b"\x31\x11\x08\x00\x00", Z_SMALL), COMPILE_TIME)         # f 8 in I8
        decode_fault_record(narrow_record(b"\x31\x11\x07\x00\x00", Z_SMALL), COMPILE_TIME)  # f 7 is fine

    def test_array_rank_extent_and_size(self):
        ok = b"\x51\x11\x01" + (3).to_bytes(8, "little") + b"\x78\x78\x88"     # I8[3], IM-148 table
        decode_fault_record(narrow_record(ok, Z_SMALL), COMPILE_TIME)
        self.reject(narrow_record(b"\x51\x11\x00", Z_SMALL), COMPILE_TIME)
        self.reject(narrow_record(b"\x51\x11\x09" + bytes(72), Z_SMALL), COMPILE_TIME)
        self.reject(narrow_record(b"\x51\x11\x01" + (-1).to_bytes(8, "little", signed=True), Z_SMALL),
                    COMPILE_TIME)
        huge = b"\x51\x14\x02" + ((1 << 62)).to_bytes(8, "little") * 2
        self.reject(narrow_record(huge, Z_SMALL), COMPILE_TIME)
        self.reject(narrow_record(b"\x51\x01\x01" + (1).to_bytes(8, "little") + b"\x02", Z_SMALL), COMPILE_TIME)

    def test_counts_larger_than_the_bytes_remaining(self):
        count_at = 4 + 20 + 2 + 4 + 15
        self.reject(CONF01_V2[:count_at] + (9).to_bytes(4, "little") + CONF01_V2[count_at + 4:])
        self.reject(CONF01_V2[:count_at] + (0xFFFFFFFF).to_bytes(4, "little") + CONF01_V2[count_at + 4:])
        self.reject(CONF01_V2[:-4] + (1).to_bytes(4, "little"))
        self.reject(CONF01_V2[:-4] + (0xFFFFFFFF).to_bytes(4, "little"))

    def test_content_checks(self):
        self.reject(CONF01_V2[:24] + (0).to_bytes(2, "little") + CONF01_V2[26:])     # code 0
        self.reject(CONF01_V2[:24] + (14).to_bytes(2, "little") + CONF01_V2[26:])    # no code 14
        self.reject(CONF01_V2.replace(b"add.checked.i64", b"Add.checked.i64"))
        self.reject(CONF01_V2.replace(b"cint-core-1/fault/v2", b"cint-core-1/fault/v3"))


class FrozenRecordsRoundTrip(unittest.TestCase):
    """The D-17 cases that task 2.7 freezes: their compile-time records fit the
    compile-time bounds and not the run-time ones."""

    CASES = {"arith/const_narrow_literal_2p3000": 376, "arith/const_narrow_literal_max": 513,
             "arith/literal_conversion_skipped_operand": 376}

    def test_compile_time_records_of_the_frozen_cases(self):
        root = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                            "conformance")
        for case, size in self.CASES.items():
            with open(os.path.join(root, case + ".ci"), "rb") as fh:
                o = run_program(fh.read(), case + ".ci", "main")
            self.assertEqual((o.kind, o.diagnostic.code), ("compile-error", "C6001"), case)
            b = encode_fault_record(o.diagnostic.record(), COMPILE_TIME)
            # the operand: tag 0f at offset 44 (domain 24, code 2, operation 14, count 4), then its length
            self.assertEqual((b[44], int.from_bytes(b[45:49], "little")), (0x0F, size), case)
            self.assertEqual(decode_fault_record(b, COMPILE_TIME)[1], o.diagnostic.record(), case)
            with self.assertRaises(ValueError):
                decode_fault_record(b, RUN_TIME)


STORES = """I64 counter = 0;
Bool seen = false;

I64 g() {
    counter = counter + 1;
    seen = true;
    return 5;
}

export void step(I64 z) {
    I64 y = 7;
    y = g() + 1 / z;
}
"""

DEEP = """export I64 f(I64 n) {
    return g(n);
}

I64 g(I64 n) {
    return f(n);
}
"""


class ExpectFormat2(unittest.TestCase):
    def test_state_lines_after_a_fault(self):
        o = run_program(STORES.encode(), "a/stores.ci", "step", None, 256, args=[Value("I64", 0)])
        text = expect.render(expect.from_outcome(o, "a/stores", "SPEC-01 9.5", fmt=2))
        self.assertEqual(text.split("\n")[2], "format 2")
        self.assertTrue(text.endswith("fault.stack-depth 0\nstate.global a.stores counter I64 1\n"
                                      "state.global a.stores seen Bool true\n"), text)
        self.assertIn("fault.revision self\nfault.source-map self\n", text)
        self.assertEqual(expect.render(expect.parse(text)), text)
        v1 = expect.render(expect.from_outcome(o, "a/stores", "SPEC-01 9.5"))
        self.assertNotIn("state.global", v1)
        self.assertNotIn("format", v1)

    def test_state_lines_after_a_value(self):
        o = run_program(STORES.encode(), "stores.ci", "step", None, 256, args=[Value("I64", 1)])
        text = expect.render(expect.from_outcome(o, "stores", "x", fmt=2))
        self.assertTrue(text.endswith("fuel-consumed 2\nstate.global stores counter I64 1\n"
                                      "state.global stores seen Bool true\n"), text)

    def test_stack_lines(self):
        o = run_program(DEEP.encode(), "deep.ci", "f", None, 3, args=[Value("I64", 0)])
        text = expect.render(expect.from_outcome(o, "deep", "x", fmt=2))
        self.assertIn("fault.stack-depth 2\nfault.stack deep.ci:2:12\nfault.stack deep.ci:6:12\n", text)

    def test_a_script_has_no_state_lines(self):
        o = run_program(b'I64 x = 1;\n"x={x}\\n";\n', "s.ci")
        self.assertEqual(o.kind, "value")
        self.assertNotIn("state.global", expect.render(expect.from_outcome(o, "s", "x", fmt=2)))

    def test_reader_rules(self):
        o = run_program(DEEP.encode(), "deep.ci", "f", None, 3, args=[Value("I64", 0)])
        good = expect.render(expect.from_outcome(o, "deep", "x", fmt=2))
        bad = [good.replace("format 2\n", ""),                                   # v2 lines in a v1 file
               good.replace("fault.source-map self\n", ""),                      # rule 5
               good.replace("fault.stack deep.ci:6:12\n", ""),                   # rule 5: count
               good.replace("format 2\n", "format 5\n"),
               good.replace("clause x\nformat 2\n", "format 2\nclause x\n")]
        for text in bad:
            with self.assertRaises(ValueError):
                expect.parse(text)
        self.assertEqual(expect.parse(good).format, 2)

    def test_format_1_is_unchanged(self):
        o = run_program(DEEP.encode(), "deep.ci", "f", None, 3, args=[Value("I64", 0)])
        text = expect.render(expect.from_outcome(o, "deep", "x"))
        self.assertTrue(text.endswith("fault.revision self\nfault.address none\nfault.stack-depth 2\n"), text)
        self.assertEqual(expect.parse(text).format, 1)


if __name__ == "__main__":
    unittest.main()
