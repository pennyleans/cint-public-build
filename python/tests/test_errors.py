"""Exceptions and fault records (SPEC-03 6.1; SPEC-01 IM-104, IM-110, IM-144 to
IM-149; BX10-05, BX10-06, BX10-13).

Records are checked three ways: against the tagged bytes of the IM-148
table, against the encoder of the reference interpreter (`ref/cint_ref`),
and against every frozen `.expect` file of the conformance suite whose
outcome is a fault, field by field."""
import os
from pathlib import Path
import unittest

import cint
from cint import _errors
from cint._errors import error_set_class, exception_for_status, work_index
from cint._record import ArrayValue, Redacted, ViewIdentity, decode_fault_record, display_value

from .support import ROOT, reference_path

I64_MAX = (1 << 63) - 1

# The CONF-01 record (SPEC-09 CONF-01), version 2, as ref/tests/test_fault_record.py derives it by hand.
CONF01_V2 = bytes.fromhex(
    "14000000" "63696e742d636f72652d312f6661756c742f7632" "0100"
    "0f000000" "6164642e636865636b65642e693634" "02000000"
    "14ffffffffffffff7f" "140100000000000000"
    "01" "0f09000000" "000000000000008000" "01" "14ffffffffffffff7f"
    "19000000" "61726974682f6164645f6936345f6f766572666c6f772e6369" "05000000" "0f000000"
    "00" "00" "00" "00000000")
CONF01_V1 = CONF01_V2[:4] + b"cint-core-1/fault/v1" + CONF01_V2[24:-7] + bytes(32) + b"\x00" + bytes(4)

CONF01_TEXT = """E_OVERFLOW at arith/add_i64_overflow.ci:5:15

operation: add.checked.i64
left:      9223372036854775807
right:     1
result:    9223372036854775808
maximum:   9223372036854775807"""


def u32(n: int) -> bytes:
    return n.to_bytes(4, "little")


def i64(n: int) -> bytes:
    return (n & ((1 << 64) - 1)).to_bytes(8, "little")


def position(path: bytes, line: int, column: int) -> bytes:
    return u32(len(path)) + path + u32(line) + u32(column)


def record(operands=(), *, code=6, operation=b"narrow.checked.i8", exact=None, limit=None, revision=None,
           source_map=None, address=None, stack=(), version=2) -> bytes:
    """Fault record bytes around hand-written tagged values (SPEC-01 IM-149)."""
    domain = b"cint-core-1/fault/v%d" % version
    out = [u32(len(domain)), domain, code.to_bytes(2, "little"), u32(len(operation)), operation,
           u32(len(operands)), *operands]
    out += [b"\x00"] if exact is None else [b"\x01", exact]
    out += [b"\x00"] if limit is None else [b"\x01", limit]
    out.append(position(b"a/b.ci", 3, 4))
    if version == 1:
        out.append(revision or bytes(32))
    else:
        for digest in (revision, source_map):
            out += [b"\x00"] if digest is None else [b"\x01", digest]
    if address is None:
        out.append(b"\x00")
    else:
        name, dispatch, phase, item = address
        out += [b"\x01", u32(len(name)), name, i64(dispatch), bytes([phase])]
        out += [b"\x00"] if item is None else [b"\x01", i64(item[0]), i64(item[1])]
    out.append(u32(len(stack)))
    out += [position(*p) for p in stack]
    return b"".join(out)


def operand(tagged: bytes):
    """(type name, value) of one tagged operand."""
    f = decode_fault_record(record([tagged]))
    return f["operand_types"][0], f["operands"][0]


class Exceptions(unittest.TestCase):
    """The exceptions of SPEC-03 6.1 and the status mapping."""

    def test_classes(self):
        codes = {"OverflowFault": "E_OVERFLOW", "DivZeroFault": "E_DIV_ZERO", "BoundsFault": "E_BOUNDS",
                 "ShapeFault": "E_SHAPE", "ShiftFault": "E_SHIFT", "NarrowFault": "E_NARROW",
                 "AliasFault": "E_ALIAS", "StaleHandleFault": "E_STALE_HANDLE", "FuelFault": "E_FUEL",
                 "UnsupportedFault": "E_UNSUPPORTED", "DomainFault": "E_DOMAIN", "DepthFault": "E_DEPTH",
                 "AssertFault": "E_ASSERT"}
        for name, code in codes.items():
            cls = getattr(cint, name)
            self.assertTrue(issubclass(cls, cint.Fault))
            self.assertEqual((cls.code, cls.number, cls.__module__), (code, _errors.FAULT_CODES[code], "cint"))
        for name in ("HostError", "HazardError", "ResourceError"):
            self.assertTrue(issubclass(getattr(cint, name), cint.Fault))
        self.assertTrue(issubclass(cint.BorrowRefused, cint.Refused))
        for name in ("Refused", "Busy", "Faulted", "Unpublished", "StaleHandle", "ReplayDivergence",
                     "Unreplayable", "ErrorResult"):
            self.assertFalse(issubclass(getattr(cint, name), cint.Fault), name)

    def test_status_mapping(self):
        self.assertIsInstance(exception_for_status(_errors.CINT_FAULT, record=CONF01_V2), cint.OverflowFault)
        for status, cls in ((_errors.CINT_HOST_ERROR, cint.HostError), (_errors.CINT_HAZARD, cint.HazardError),
                            (_errors.CINT_RESOURCE, cint.ResourceError)):
            e = exception_for_status(status, record=CONF01_V2)
            self.assertIs(type(e), cls)
            self.assertEqual(e.code, "E_OVERFLOW")
        self.assertIsInstance(exception_for_status(_errors.CINT_RESOURCE, mid_entry=False), MemoryError)
        refusal = cint.Refused("unknown buffer")
        self.assertIs(exception_for_status(_errors.CINT_REFUSED, refusal=refusal), refusal)
        self.assertIsInstance(exception_for_status(_errors.CINT_REFUSED), cint.Refused)
        self.assertIsInstance(exception_for_status(_errors.CINT_BUSY), cint.Busy)
        self.assertIsInstance(exception_for_status(_errors.CINT_FAULTED), cint.Faulted)
        self.assertIsInstance(exception_for_status(_errors.CINT_DIVERGED), cint.ReplayDivergence)
        self.assertIsInstance(exception_for_status(_errors.CINT_UNREPLAYABLE), cint.Unreplayable)
        for status in (_errors.CINT_OK, 42):
            with self.assertRaises(ValueError):
                exception_for_status(status)

    def test_refused(self):
        with self.assertRaises(ValueError):
            cint.Refused("x", code="E_BOGUS")
        e = cint.Refused("parameter n: 300 is outside U8 (0 to 255)", code="E_NARROW", operands=(300,),
                         operand_types=("Z",), exact=300, limit=255, limit_type="U8", index=(1,), count=2)
        self.assertEqual(str(e), "E_NARROW: parameter n: 300 is outside U8 (0 to 255)\n"
                                 "  operand:   300\n  result:    300\n  maximum:   255")
        self.assertEqual((e.detail, e.index, e.count), ("parameter n: 300 is outside U8 (0 to 255)", (1,), 2))
        self.assertEqual(str(cint.Refused("nothing to add")), "E_UNSUPPORTED: nothing to add")

    def test_busy(self):
        self.assertEqual(str(cint.Busy("an entry is active")), "an entry is active")
        busy = cint.Busy("live exports", code="E_ALIAS")
        self.assertEqual((str(busy), busy.code), ("E_ALIAS: live exports", "E_ALIAS"))


class ErrorResults(unittest.TestCase):
    """BX10-06: an error result raises cint.ErrorResult, which is not a Fault."""

    def test_not_a_fault(self):
        ParseError = error_set_class("ParseError", ["Empty", "BadDigit"])
        e = ParseError.from_tag(2)
        self.assertEqual((e.set, e.name, e.tag, str(e)), ("ParseError", "BadDigit", 2, "ParseError.BadDigit (tag 2)"))
        self.assertIsInstance(e, cint.ErrorResult)
        self.assertNotIsInstance(e, cint.Fault)
        try:
            try:
                raise e
            except cint.Fault:
                self.fail("except cint.Fault caught an error result")
        except ParseError as caught:
            self.assertIs(caught, e)
        self.assertEqual((ParseError.__name__, ParseError.members), ("ParseError", ("Empty", "BadDigit")))

    def test_tags(self):
        ParseError = error_set_class("ParseError", ["Empty"])
        self.assertEqual(ParseError.from_tag(1).name, "Empty")
        for tag in (0, 2, True, "1"):
            with self.assertRaises(ValueError, msg=repr(tag)):
                ParseError.from_tag(tag)
        with self.assertRaises(TypeError):
            cint.ErrorResult.from_tag(1)
        for name, members in (("ParseError", []), ("ParseError", ["A", "A"]), ("not a name", ["A"]),
                              ("E", ["1x"])):
            with self.assertRaises(ValueError):
                error_set_class(name, members)
        e = cint.ErrorResult("Timeout", 3, "IoError")
        self.assertEqual((e.set, e.name, e.tag), ("IoError", "Timeout", 3))


class Recovery(unittest.TestCase):
    """BX10-05: recovery is explicit, and the Faulted message names both ways
    back and the phase of the held fault."""

    def test_faulted_message(self):
        held = cint.Fault.from_record(CONF01_V2)
        f = cint.Faulted(held)
        text = str(f)
        self.assertEqual((f.held, f.nothing_ran), (held, False))
        self.assertIn("holds E_OVERFLOW, add.checked.i64 at arith/add_i64_overflow.ci:5:15", text)
        self.assertIn("raised while the entry ran", text)
        self.assertIn("call ctx.clear_fault() and then an entry that reinitializes that state (A-7b)", text)
        self.assertIn("make a new context with Module.context()", text)
        self.assertIn("holds a fault", str(cint.Faulted()))

    def test_entry_phase_fault(self):
        held = cint.Fault.from_record(record(code=7, operation=b"bind.alias", address=(b"step", 0, 0, None)))
        f = cint.Faulted(held)
        self.assertTrue(f.nothing_ran)
        self.assertIn("a fault of the entry checks, so nothing ran: ctx.clear_fault() alone makes the context "
                      "usable again, or make a new context with Module.context()", str(f))
        self.assertTrue(cint.Faulted(cint.AliasFault(operation="bind.alias")).nothing_ran)
        self.assertFalse(cint.Faulted(held, nothing_ran=False).nothing_ran)


class Records(unittest.TestCase):
    def test_conf01(self):
        f = cint.Fault.from_record(CONF01_V2)
        self.assertIs(type(f), cint.OverflowFault)
        self.assertEqual((f.code, f.operation, f.operands, f.operand_types),
                         ("E_OVERFLOW", "add.checked.i64", (I64_MAX, 1), ("I64", "I64")))
        self.assertEqual((f.exact, f.limit, f.limit_type), (1 << 63, I64_MAX, "I64"))
        self.assertEqual((f.file, f.line, f.column, f.position), ("arith/add_i64_overflow.ci", 5, 15,
                                                                  "arith/add_i64_overflow.ci:5:15"))
        self.assertEqual((f.revision, f.source_map, f.kernel, f.phase, f.stack), (None, None, None, None, ()))
        self.assertEqual((f.record, f.version), (CONF01_V2, 2))
        self.assertEqual(str(f), CONF01_TEXT)
        self.assertFalse(f.entry_phase())

    def test_version_1(self):
        f = cint.Fault.from_record(CONF01_V1)
        self.assertEqual((f.version, f.revision, f.exact), (1, None, 1 << 63))
        g = cint.Fault.from_record(record(version=1, revision=b"\x07" * 32))
        self.assertEqual(g.revision, b"\x07" * 32)

    def test_im148_table(self):
        rows = [("14 fe ff ff ff ff ff ff ff", "I64", -2), ("14 e8 03 00 00 00 00 00 00", "I64", 1000),
                ("21 ff", "U8", 255), ("31 13 10 00 00 80 01 00", "Q16.16", cint.Fixed("Q16.16", value=1) and
                                       cint.Fixed("Q16.16", raw=98304)),
                ("15 00 00 00 00 00 00 00 00 01 00 00 00 00 00 00 00", "I128", 1 << 64),
                ("0f 09 00 00 00 00 00 00 00 00 00 00 80 00", "Z", 1 << 63), ("0f 01 00 00 00 ff", "Z", -1),
                ("51 11 01 03 00 00 00 00 00 00 00 78 78 88", "I8[3]", ArrayValue("I8", (3,), (120, 120, -120))),
                ("7f 14", "redacted I64", Redacted("I64"))]
        for hexed, type_name, value in rows:
            self.assertEqual(operand(bytes.fromhex(hexed)), (type_name, value), hexed)

    def test_other_values(self):
        self.assertEqual(operand(bytes.fromhex("01 01")), ("Bool", True))
        self.assertEqual(operand(bytes.fromhex("41 ff")), ("T1", -1))
        self.assertEqual(operand(bytes([0x42]) + i64(-(3 ** 27 - 1) // 2)), ("T27", -(3 ** 27 - 1) // 2))
        self.assertEqual(operand(bytes.fromhex("0f 00 00 00 00")), ("Z", 0))
        self.assertEqual(operand(bytes.fromhex("31 14 3c 00") + i64(1)), ("Q4.60", cint.Fixed("Q4.60", raw=1)))
        view = bytes([0x61, 0x14]) + i64(5) + i64(2) + bytes([2]) + i64(3) + i64(4) + i64(1) + i64(-1) + \
            i64(2) + i64(0) + i64(4) + bytes([1])
        t, v = operand(view)
        self.assertEqual((t, v), ("view of I64", ViewIdentity("I64", 5, 2, 3, ((4, 1, -1), (2, 0, 4)), True)))
        self.assertEqual(display_value(t, v), "view of I64: buffer 5 generation 2 origin 3, extent 4 lower 1 "
                                              "stride -1, extent 2 lower 0 stride 4, write")
        t, v = operand(bytes.fromhex("51 31 12 08 00 02 01 00 00 00 00 00 00 00 02 00 00 00 00 00 00 00 80 01 ff ff"))
        self.assertEqual((t, v.type, v.values), ("Q8.8[1, 2]", "Q8.8[1, 2]", (cint.Fixed("Q8.8", raw=384),
                                                                             cint.Fixed("Q8.8", raw=-1))))
        self.assertEqual(display_value(t, v), "Q8.8[1, 2] {1.5 (Q8.8 raw 384), -0.00390625 (Q8.8 raw -1)}")
        self.assertEqual(operand(bytes.fromhex("7f 51 21 01 02 00 00 00 00 00 00 00")),
                         ("redacted U8[2]", Redacted("U8[2]")))

    def test_rejected(self):
        """IM-148: what a decoder must reject."""
        bad_operands = ["01 02", "41 02", "42" + i64(3 ** 27).hex(), "43 01 01 00 00 00 00 00 00 00",
                        "44 01 01 00 00 00 00 00 00 00", "71 00 00 00 00", "99", "0f 01 00 00 00 00",
                        "0f 02 00 00 00 01 00", "0f 02 00 00 00 ff ff", "31 15 10 00", "31 13 20 00",
                        "51 11 00", "51 11 09", "51 11 01 ff ff ff ff ff ff ff ff", "51 0f 01 01 00 00 00 00 00 00 00",
                        "51 11 01 05 00 00 00 00 00 00 00 01 02", "7f 7f 14", "61 51 11 01",
                        "0f " + u32(258).hex() + "01" * 258]
        for hexed in bad_operands:
            with self.assertRaises(ValueError, msg=hexed):
                decode_fault_record(record([bytes.fromhex(hexed)]))
        good = record([bytes.fromhex("21 05")])
        for broken in (good[:-1], good + b"\x00", good.replace(b"narrow", b"Narrow"),
                       good[:24] + (99).to_bytes(2, "little") + good[26:]):
            with self.assertRaises(ValueError):
                decode_fault_record(broken)
        for limit in ("61 14 " + "00" * 16 + "01" + "00" * 32 + "00", "7f 14"):
            with self.assertRaises(ValueError, msg=limit):
                decode_fault_record(record(limit=bytes.fromhex(limit)))
        with self.assertRaises(ValueError):
            decode_fault_record(record(exact=bytes.fromhex("14 01 00 00 00 00 00 00 00")))
        with self.assertRaises(ValueError):
            decode_fault_record(record(address=(b"k", 0, 0, (1, 1))))
        with self.assertRaises(ValueError):
            decode_fault_record(record(address=(b"k", 0, 3, None)))
        with self.assertRaises(ValueError):
            decode_fault_record(record(), kind="other")

    def test_redacted_exact(self):
        f = decode_fault_record(record(exact=bytes.fromhex("7f 0f")))
        self.assertEqual(f["exact"], Redacted("Z"))

    def test_kernel_address(self):
        data = record([bytes.fromhex("14 01 00 00 00 00 00 00 00")], code=1, operation=b"add.checked.i64",
                      address=(b"advance", 3, 1, (13, 2)), stack=[(b"k.ci", 9, 1), (b"k.ci", 20, 5)],
                      revision=b"\x01" * 32, source_map=b"\x02" * 32)
        f = cint.Fault.from_record(data, index_space=(4, 5))
        self.assertEqual((f.kernel, f.dispatch, f.phase, f.work_item, f.step, f.work_index),
                         ("advance", 3, "work-item", 13, 2, (2, 3)))
        self.assertEqual(f.stack, (("k.ci", 9, 1), ("k.ci", 20, 5)))
        self.assertEqual((f.revision, f.source_map), (b"\x01" * 32, b"\x02" * 32))
        self.assertIn("kernel:    advance, dispatch 3, phase work-item, work item 13, step 2", str(f))
        self.assertIsNone(cint.Fault.from_record(data).work_index)
        entry = cint.Fault.from_record(record(address=(b"advance", 0, 0, None)))
        self.assertEqual((entry.phase, entry.work_item), ("entry", None))
        self.assertTrue(entry.entry_phase())
        with self.assertRaises(ValueError):
            entry.expect_lines()

    def test_work_index(self):
        self.assertEqual(work_index(0, (3,)), (0,))
        self.assertEqual(work_index(23, (2, 3, 4)), (1, 2, 3))
        for item, space in ((24, (2, 3, 4)), (0, (0, 3))):
            with self.assertRaises(ValueError):
                work_index(item, space)

    def test_rounding_label(self):
        f = cint.Fault(code="E_OVERFLOW", operation="muldiv.checked.i64.half_even", operands=(1, 2, 3), exact=5,
                       limit=4, limit_type="I64", file="m.ci", line=1, column=1)
        self.assertIn("rounded:   5", str(f))
        self.assertIn("operand:   1", str(f))

    def test_host_error_class(self):
        self.assertIs(type(cint.HostError.from_record(CONF01_V2)), cint.HostError)


class AgainstTheReference(unittest.TestCase):
    """Records written by the encoder of `cint_ref` decode to the same fields,
    and `expect_lines` writes the lines the reference writes."""

    @classmethod
    def setUpClass(cls):
        reference_path()

    def test_synthesized_records(self):
        from cint_ref.faults import RUN_TIME, Address, FaultRecord, Position, encode_fault_record
        from cint_ref.types import Value
        records = [
            FaultRecord("E_OVERFLOW", "add.checked.i64", (Value("I64", I64_MAX), Value("I64", 1)), 1 << 63,
                        Value("I64", I64_MAX), Position("x.ci", 1, 2)),
            FaultRecord("E_NARROW", "narrow.checked.u8", (Value("Z", -(1 << 2000)),), -(1 << 2000), Value("U8", 0),
                        Position("dir/y.ci", 30, 4), (Position("dir/y.ci", 2, 2),)),
            FaultRecord("E_DIV_ZERO", "div.checked.i32", (Value("I32", 5), Value("I32", 0)), None, None,
                        Position("z.ci", 7, 9), (), b"\x09" * 32, None, b"\x0a" * 32),
            FaultRecord("E_ASSERT", "assert", (Value("Bool", False),), None, None, Position("t.ci", 1, 1)),
            FaultRecord("E_BOUNDS", "index.checked", (Value("I64", 9), Value("U16", 4)), None, None,
                        Position("k.ci", 4, 4), (), None, Address("step", 2, 1, 6, 11)),
        ]
        for r in records:
            for version in (1, 2):
                if version == 1 and r.source_map is not None:
                    continue
                f = cint.Fault.from_record(encode_fault_record(r, RUN_TIME, version))
                self.assertEqual(type(f), getattr(cint, {"E_OVERFLOW": "OverflowFault", "E_NARROW": "NarrowFault",
                                                         "E_DIV_ZERO": "DivZeroFault", "E_ASSERT": "AssertFault",
                                                         "E_BOUNDS": "BoundsFault"}[r.code]))
                self.assertEqual((f.code, f.operation, f.exact), (r.code, r.operation, r.exact))
                self.assertEqual(f.operands, tuple(v.value for v in r.operands))
                self.assertEqual(f.operand_types, tuple(v.type for v in r.operands))
                self.assertEqual((f.limit, f.limit_type), (None, None) if r.limit is None else
                                 (r.limit.value, r.limit.type))
                self.assertEqual((f.file, f.line, f.column), (r.position.path, r.position.line, r.position.column))
                self.assertEqual(f.stack, tuple((p.path, p.line, p.column) for p in r.stack))
                self.assertEqual((f.revision, f.source_map), (r.revision, r.source_map))
                a = r.address
                self.assertEqual((f.kernel, f.dispatch, f.work_item, f.step), (None,) * 4 if a is None else
                                 (a.name, a.dispatch, a.work_item, a.step))
                if r.revision is None:
                    for fmt in (1, 2, 4) if a is None else (4,):
                        self.assertEqual(f.expect_lines(fmt), r.to_expect_lines(fmt))

    def test_revision_lines(self):
        from cint_ref.faults import RUN_TIME, FaultRecord, Position, encode_fault_record
        r = FaultRecord("E_DIV_ZERO", "div.checked.i64", (), None, None, Position("z.ci", 1, 1), (), b"\x09" * 32)
        f = cint.Fault.from_record(encode_fault_record(r, RUN_TIME))
        self.assertIn("fault.revision " + "09" * 32, f.expect_lines(2))
        self.assertIn("fault.revision self", f.expect_lines(2, revision=b"\x09" * 32))

    def test_frozen_fault_expectations(self):
        """Every frozen `.expect` whose outcome is a fault: the reference runs the
        case, its record is encoded (versions 1 and 2), decoded here, and its
        `fault.*` lines must equal the frozen ones, in the file's format."""
        from cint_ref.__main__ import parse_arg, source_root
        from cint_ref.exec import DEFAULT_DEPTH, run_program
        from cint_ref.faults import RUN_TIME, encode_fault_record
        import gen_expect
        conformance = ROOT / "conformance"
        checked = 0
        for path in sorted(conformance.rglob("*.expect")):
            text = path.read_text(encoding="ascii")
            if "\noutcome fault\n" not in text:
                continue
            lines = text.split("\n")
            fmt = int(lines[2][len("format "):]) if lines[2] in ("format 2", "format 4") else 1
            stem = path.relative_to(conformance).as_posix()[:-len(".expect")]
            entry = None
            if not (conformance / (stem + ".ci")).exists():
                stem, entry = stem.rsplit(".", 1)
            source = conformance / (stem + ".ci")
            data = source.read_bytes()
            h = gen_expect.header(data.decode("utf-8", "replace"))
            fuel = None if h["fuel"] is None else int(h["fuel"])
            depth = DEFAULT_DEPTH if h["depth"] is None else int(h["depth"])
            o = run_program(data, stem + ".ci", entry or h["entries"][0], fuel, depth,
                            args=[parse_arg(a) for a in h["args"]], root=source_root(str(source), stem + ".ci"))
            self.assertEqual(o.kind, "fault", stem)
            want = [line for line in lines if line.startswith("fault.")]
            if o.record.descriptors:
                # A view descriptor operand has no canonical bytes yet (SPEC-01 IM-81, IM-146),
                # so the record cannot be encoded for the bridge; its lines are the reference's.
                with self.assertRaises(ValueError):
                    encode_fault_record(o.record, RUN_TIME)
                self.assertEqual(o.record.to_expect_lines(fmt), want, stem)
                checked += 1
                continue
            for version in (1, 2):
                f = cint.Fault.from_record(encode_fault_record(o.record, RUN_TIME, version))
                self.assertIsInstance(f, getattr(cint, {c.code: c.__name__ for c in _errors.FAULT_CLASSES.values()}
                                                 [f.code]))
                self.assertEqual(f.expect_lines(fmt), want, "%s (record version %d)" % (stem, version))
            checked += 1
        self.assertGreaterEqual(checked, 60)


if __name__ == "__main__":
    unittest.main()
