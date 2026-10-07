"""cint.borrow and View (SPEC-03 P-1 to P-14, A-8a, A-13; cases 16, 16b, 17)."""
import array
import ctypes
import gc
import sys
import unittest

import cint
from cint import _dlpack, _memory, _view
from cint._elem import T27_MAX

from .support import DLPackOnly, Producer, address_of, allocations, numpy, torch

FLOAT_REFUSAL = ("E_UNSUPPORTED: element format 'd' (float64) cannot be borrowed.\n"
                 "CINT has no floating-point element type. Convert explicitly:\n"
                 "  cint.from_float(a, \"Q32.32\", rounding=\"half_even\")")


def misaligned(buffer, itemsize: int = 8, count: int = 2):
    """A memoryview of `count` items whose data address is not aligned to `itemsize`."""
    base = address_of(buffer)
    offset = next(k for k in range(1, itemsize) if (base + k) % itemsize)
    return memoryview(buffer)[offset:offset + itemsize * count]


class BorrowCase(unittest.TestCase):
    def refused(self, obj, *fragments, **kwargs) -> cint.BorrowRefused:
        before = allocations()
        with self.assertRaises(cint.BorrowRefused) as caught:
            cint.borrow(obj, **kwargs)
        self.assertEqual(allocations(), before, "a refused borrow made a copy")
        for fragment in fragments:
            self.assertIn(fragment, str(caught.exception))
        return caught.exception

    def borrowed(self, obj, **kwargs) -> cint.View:
        before = allocations()
        view = cint.borrow(obj, **kwargs)
        self.assertEqual(allocations(), before, "a borrow made a copy (SPEC-03 P-9)")
        self.addCleanup(view.release)
        return view


class Case16(BorrowCase):
    """Case 16: `cint.borrow` of float64, bool, big-endian, or misaligned data
    raises `BorrowRefused`, and no copy is made."""

    def test_float64(self):
        e = self.refused(memoryview(array.array("d", [1.5, 2.5])))
        self.assertEqual(str(e), FLOAT_REFUSAL)
        self.assertEqual(e.code, "E_UNSUPPORTED")
        self.refused(array.array("f", [1.5]), "element format 'f' (float32) cannot be borrowed")

    @unittest.skipIf(numpy is None, "NumPy is not installed")
    def test_float64_numpy(self):
        self.assertEqual(str(self.refused(numpy.zeros(4, dtype=numpy.float64))), FLOAT_REFUSAL)
        self.refused(numpy.zeros(4, dtype=numpy.float16), "float16")
        self.refused(numpy.zeros(4, dtype=numpy.complex128), "complex")

    def test_bool_as_an_integer(self):
        flags = memoryview(bytes([0, 1, 1])).cast("?")
        for elem in ("U8", "I8", "I64", "T1"):
            self.refused(flags, "booleans are borrowed only as Bool", elem=elem)

    @unittest.skipIf(numpy is None, "NumPy is not installed")
    def test_bool_as_an_integer_numpy(self):
        self.refused(numpy.array([True, False]), "booleans are borrowed only as Bool", elem="U8")

    def test_big_endian(self):
        big = (ctypes.c_int64.__ctype_be__ * 2)()
        self.refused(big, "big-endian", "cint.copy(a) converts it exactly")

    @unittest.skipIf(numpy is None, "NumPy is not installed")
    def test_big_endian_numpy(self):
        self.refused(numpy.zeros(3, dtype=">i8"), "big-endian")
        self.refused(numpy.zeros(3, dtype=">u2"), "big-endian")

    def test_misaligned(self):
        data = bytearray(24)
        view = misaligned(data).cast("q")
        self.refused(view, "is not aligned to 8 bytes", "cint.copy(a)")
        self.refused(misaligned(data, 4).cast("i"), "is not aligned to 4 bytes")

    @unittest.skipIf(numpy is None, "NumPy is not installed")
    def test_misaligned_numpy(self):
        data = bytearray(24)
        offset = next(k for k in range(1, 8) if (address_of(data) + k) % 8)
        self.refused(numpy.frombuffer(data, dtype=numpy.int64, count=2, offset=offset), "is not aligned")

    def test_accepted_borrows_copy_nothing(self):
        data = array.array("q", [1, 2, 3])
        view = self.borrowed(data)
        self.assertEqual(view.region.address, data.buffer_info()[0])
        data[1] = 20
        self.assertEqual(view.tolist(), [1, 20, 3])


class Case16b(BorrowCase):
    """Case 16b: a CPU tensor of `int64` is borrowed through DLPack (`kDLCPU`),
    with no copy. Runs on PyTorch where it is installed, and always on a
    DLPack producer over ctypes storage."""

    @unittest.skipIf(torch is None, "PyTorch is not installed")
    def test_torch_cpu_int64(self):
        t = torch.arange(5, dtype=torch.int64)
        view = self.borrowed(t)
        self.assertEqual((view.protocol, view.elem, view.shape), ("DLPack", "I64", (5,)))
        self.assertEqual(view.region.address, t.data_ptr())
        t[0] = 99
        self.assertEqual(view.tolist(), [99, 1, 2, 3, 4])

    @unittest.skipIf(numpy is None, "NumPy is not installed")
    def test_numpy_through_dlpack(self):
        a = numpy.arange(4, dtype=numpy.int64)
        view = self.borrowed(DLPackOnly(a))
        self.assertEqual((view.protocol, view.elem, view.shape), ("DLPack", "I64", (4,)))
        self.assertEqual(view.region.address, a.ctypes.data)
        a[2] = -7
        self.assertEqual(view.tolist(), [0, 1, -7, 3])

    def test_ctypes_producer(self):
        p = Producer([5, 6, 7])
        view = self.borrowed(p)
        self.assertEqual((view.protocol, view.elem, view.notes), ("DLPack", "I64", ()))
        self.assertEqual(view.region.address, p.address)
        self.assertEqual(p.calls, [{"stream": None, "max_version": (1, 0), "dl_device": None, "copy": False}])
        p.storage[0] = 50
        self.assertEqual(view.tolist(), [50, 6, 7])
        self.assertEqual((p.exported, p.deleted), (1, 0))
        view.release()
        self.assertEqual((p.exported, p.deleted, p.unknown_deletes), (1, 1, 0))


class Case17(BorrowCase):
    """Case 17: `cint.borrow(..., writable=True)` of a read-only array raises
    `BorrowRefused` (P-7)."""

    def test_bytes(self):
        self.refused(b"abcd", "writable=True over a read-only export", writable=True)

    def test_read_only_memoryview(self):
        self.refused(memoryview(bytearray(8)).toreadonly().cast("q"), "read-only", writable=True)

    @unittest.skipIf(numpy is None, "NumPy is not installed")
    def test_numpy_read_only(self):
        a = numpy.arange(4, dtype=numpy.int64)
        a.flags.writeable = False
        self.refused(a, "writable=True over a read-only export (SPEC-03 P-7)", writable=True)
        self.refused(DLPackOnly(a), "read-only DLPack tensor", writable=True)
        self.assertEqual(self.borrowed(a).writable, False)

    @unittest.skipIf(numpy is None, "NumPy is not installed")
    def test_broadcast(self):
        b = numpy.broadcast_to(numpy.arange(3, dtype=numpy.int64), (4, 3))
        self.refused(b, "read-only", writable=True)

    def test_dlpack_read_only_flag(self):
        p = Producer([1, 2], flags=_dlpack.FLAG_READ_ONLY)
        self.refused(p, "read-only DLPack tensor", writable=True)
        self.assertEqual(p.deleted, 1)
        self.assertFalse(self.borrowed(p).writable)

    def test_writable_accepted(self):
        data = bytearray(16)
        view = self.borrowed(data, writable=True)
        self.assertTrue(view.writable)
        self.assertTrue(view.region.writable)


class Formats(BorrowCase):
    """P-3: the element is chosen by the signedness of the format character and
    the reported item size, never by its C type; P-4: `elem` names that
    element, or a fixed-point type over it."""

    def test_by_size(self):
        for code in "bBhHiIlLqQ":
            a = array.array(code, [1])
            view = self.borrowed(a)
            self.assertEqual(view.elem, ("I" if code.islower() else "U") + str(8 * a.itemsize), code)
        for code in "nN":
            view = self.borrowed(memoryview(bytes(8)).cast(code))
            self.assertEqual(view.elem, "I64" if code == "n" else "U64")

    def test_explicit_little_endian(self):
        view = self.borrowed((ctypes.c_int64.__ctype_le__ * 2)(3, 4))
        self.assertEqual((view.elem, view.tolist()), ("I64", [3, 4]))

    def test_refused_formats(self):
        self.refused(memoryview(b"ab").cast("c"), "element format 'c'")

        class Point(ctypes.Structure):
            _fields_ = [("x", ctypes.c_int32), ("y", ctypes.c_int32)]
        self.refused((Point * 2)(), "is not an integer element")

    def test_bool_elements(self):
        view = self.borrowed(memoryview(bytes([0, 1, 1])).cast("?"))
        self.assertEqual((view.elem, view.tolist()), ("Bool", [False, True, True]))
        self.refused(bytes([0, 1]), "only format '?' or kDLBool is borrowed as Bool", elem="Bool")

    def test_bool_bytes_checked_at_entry(self):
        """A-13: a `Bool` byte other than 0 or 1 is refused when an entry binds
        the view (here, its value check) and when it is read; the borrow itself
        does not read the elements."""
        view = self.borrowed(memoryview(bytes([0, 1, 2, 7])).cast("?"))
        with self.assertRaises(cint.BorrowRefused) as caught:
            view._check_values()
        e = caught.exception
        self.assertEqual((e.code, e.index, e.count, e.operands), ("E_NARROW", (2,), 2, (2,)))
        with self.assertRaises(cint.BorrowRefused):
            view.tolist()

    def test_elem_matches(self):
        data = array.array("q", [1 << 32, -(1 << 31)])
        self.assertEqual(self.borrowed(data, elem="I64").elem, "I64")
        view = self.borrowed(data, elem="Q32.32")
        self.assertEqual(view.tolist(), [cint.Fixed("Q32.32", value=1), cint.Fixed("Q32.32", value=cint.Fixed(
            "Q32.32", raw=-(1 << 31)).as_fraction())])
        self.assertEqual(self.borrowed(array.array("i", [65536]), elem="Q16.16").tolist(),
                         [cint.Fixed("Q16.16", value=1)])
        self.refused(data, "storage of its width, I32", elem="Q16.16")
        self.refused(data, "cint.copy(a, elem='I32')", elem="I32")
        self.refused(data, "the bridge never converts", elem="U64")
        with self.assertRaises(ValueError):
            cint.borrow(data, elem="F64")

    def test_ternary_carriers(self):
        """T1 over I8 and T27 over I64 (SPEC-05 TR-HOST-1); the carriers are
        checked at the borrow (TR-VAL-3)."""
        self.assertEqual(self.borrowed(array.array("b", [1, 0, -1]), elem="T1").tolist(), [1, 0, -1])
        e = self.refused(array.array("b", [1, 2, -1, 5]), "not a T1 value", elem="T1")
        self.assertEqual((e.code, e.operation, e.reason), ("E_NARROW", "decode.checked.t1", "unused value"))
        self.assertEqual((e.operands, e.operand_types, e.index, e.count), ((2,), ("U8",), (1,), 2))
        good = array.array("q", [T27_MAX, -T27_MAX])
        self.assertEqual(self.borrowed(good, elem="T27").tolist(), [T27_MAX, -T27_MAX])
        e = self.refused(array.array("q", [0, T27_MAX + 1]), elem="T27")
        self.assertEqual((e.operation, e.operands, e.operand_types, e.index), ("decode.checked.t27",
                                                                                (T27_MAX + 1,), ("I64",), (1,)))
        self.refused(array.array("h", [1]), elem="T1")


class Shapes(BorrowCase):
    """P-5 strides, P-6 rank, P-8 layout, and the A-8a registration region."""

    def test_rank(self):
        self.refused(ctypes.c_int64(5), "rank 0 is not borrowed")
        five = ((((ctypes.c_int8 * 1) * 1) * 1) * 1) * 1
        self.refused(five(), "rank 5 is above CINT_MAX_RANK, 4")
        four = (((ctypes.c_int8 * 2) * 1) * 1) * 3
        self.assertEqual(self.borrowed(four()).shape, (3, 1, 1, 2))

    @unittest.skipIf(numpy is None, "NumPy is not installed")
    def test_stride_not_a_multiple(self):
        a = numpy.ndarray((3,), dtype=numpy.int64, buffer=bytearray(48), strides=(12,))
        self.refused(a, "byte strides [12] are not multiples of the item size 8")

    @unittest.skipIf(numpy is None, "NumPy is not installed")
    def test_regions(self):
        a = numpy.arange(12, dtype=numpy.int64).reshape(3, 4)
        base = a.ctypes.data
        cases = [(a, (base, 12, 0, (4, 1))), (a[::-1], (base, 12, 8, (-4, 1))),
                 (a[:, ::-1], (base, 12, 3, (4, -1))), (a[::-1, ::-1], (base, 12, 11, (-4, -1))),
                 (a[:, ::2], (base, 11, 0, (4, 2))), (a[1:, 1:], (base + 40, 7, 0, (4, 1))),
                 (a.T, (base, 12, 0, (1, 4)))]
        for arr, (address, extent, origin, strides) in cases:
            r = self.borrowed(arr).region
            self.assertEqual((r.address, r.extent, r.origin, r.strides), (address, extent, origin, strides))
            self.assertEqual(r.shape, arr.shape)
        r = self.borrowed(numpy.broadcast_to(numpy.arange(3, dtype=numpy.int64), (4, 3))).region
        self.assertEqual((r.extent, r.origin, r.strides), (3, 0, (0, 1)))
        r = self.borrowed(numpy.zeros((0, 3), dtype=numpy.int64)).region
        self.assertEqual((r.extent, r.origin), (0, 0))

    @unittest.skipIf(numpy is None, "NumPy is not installed")
    def test_reversed_values(self):
        a = numpy.arange(6, dtype=numpy.int32).reshape(2, 3)
        self.assertEqual(self.borrowed(a[::-1, ::-1]).tolist(), [[5, 4, 3], [2, 1, 0]])

    @unittest.skipIf(numpy is None, "NumPy is not installed")
    def test_layout(self):
        c = numpy.zeros((2, 3), dtype=numpy.int64)
        f = numpy.asfortranarray(c)
        self.borrowed(c, layout="C")
        self.borrowed(f, layout="F")
        self.refused(c, "not contiguous in column-major order", layout="F")
        self.refused(f, "not contiguous in row-major order", layout="C")
        self.refused(c[:, ::2], "row-major", layout="C")
        self.borrowed(c[:, ::2], layout="any")
        row = numpy.zeros((1, 3), dtype=numpy.int64)
        self.borrowed(row, layout="C")
        self.borrowed(row, layout="F")
        self.borrowed(numpy.zeros((0, 3), dtype=numpy.int64), layout="F")

    def test_layout_stdlib(self):
        data = array.array("q", [1, 2, 3])
        self.borrowed(data, layout="C")
        self.borrowed(data, layout="F")
        with self.assertRaises(ValueError):
            cint.borrow(data, layout="K")


class Arguments(BorrowCase):
    def test_publish(self):
        data = bytearray(8)
        self.refused(data, "needs writable=True", publish="copy")
        self.assertEqual(self.borrowed(data, writable=True, publish="copy").publish, "copy")
        with self.assertRaises(ValueError):
            cint.borrow(data, writable=True, publish="move")
        with self.assertRaises(TypeError):
            cint.borrow(data, writable="yes")

    def test_context_needs_a_context(self):
        with self.assertRaisesRegex(TypeError, "Module.context"):
            cint.borrow(bytearray(8), context=object())

    def test_objects_without_a_protocol(self):
        for obj in ([1, 2, 3], (1, 2), range(3)):
            e = self.refused(obj, "cint.copy(seq, elem=\"I64\")")
            self.assertEqual(e.reason, "protocol")
        self.assertEqual(self.refused("abc", 's.encode("utf-8")').reason, "protocol")
        self.assertEqual(self.refused(object(), "neither the buffer protocol nor __dlpack__").reason, "protocol")

    def test_view_and_buffer_refused(self):
        view = self.borrowed(bytearray(8))
        self.refused(view, "already a cint.View")
        self.refused(cint.copy([1], elem="I64"), "already a cint.Buffer")

    def test_flags_requested(self):
        """BX10-08: the bridge asks for PyBUF_STRIDES | PyBUF_FORMAT (0x1c), plus
        PyBUF_WRITABLE (0x1) for a writable borrow, through PyObject_GetBuffer."""
        seen = []
        real = _view.HeldBuffer

        def recording(obj, flags):
            seen.append(flags)
            return real(obj, flags)
        _view.HeldBuffer = recording
        try:
            self.borrowed(bytearray(8))
            self.borrowed(bytearray(8), writable=True)
        finally:
            _view.HeldBuffer = real
        self.assertEqual(seen, [0x1c, 0x1d])
        self.assertEqual(_memory.BORROW_FLAGS, 0x1c)

    @unittest.skipIf(sys.version_info < (3, 12), "Python classes export buffers from 3.12")
    def test_flags_seen_by_the_exporter(self):
        class Exporter:
            def __init__(self, needs_indirect=False):
                self.data = bytearray(8)
                self.flags = []
                self.needs_indirect = needs_indirect

            def __buffer__(self, flags):
                self.flags.append(flags)
                if self.needs_indirect and not flags & 0x100:
                    raise BufferError("this export needs PyBUF_INDIRECT")
                return memoryview(self.data)

            def __release_buffer__(self, view):
                view.release()

        x = Exporter()
        self.borrowed(x, writable=True)
        self.assertEqual(x.flags, [0x1d])
        self.refused(Exporter(needs_indirect=True), "PyBUF_INDIRECT is never borrowed")


class Lifetime(BorrowCase):
    """P-2: the View holds the Py_buffer, and with it the exporter, until it
    is released; after that every use raises StaleHandle (SPEC-03 6.1)."""

    def test_buffer_held_until_release(self):
        data = bytearray(b"abcd")
        view = cint.borrow(data)
        with self.assertRaises(BufferError):
            data.extend(b"e")
        view.release()
        data.extend(b"e")
        self.assertTrue(view.released)
        view.release()

    def test_stale_after_release(self):
        view = cint.borrow(bytearray(8))
        view.release()
        for use in (view.tolist, lambda: view.region, view._check_values):
            with self.assertRaises(cint.StaleHandle):
                use()
        with self.assertRaises(cint.StaleHandle):
            with view:
                pass

    def test_context_manager(self):
        data = bytearray(4)
        with cint.borrow(data) as view:
            self.assertEqual(view.tolist(), [0, 0, 0, 0])
        self.assertTrue(view.released)
        data.extend(b"x")

    def test_released_when_collected(self):
        data = bytearray(4)
        view = cint.borrow(data)
        del view
        gc.collect()
        data.extend(b"x")

    def test_attributes(self):
        view = self.borrowed(array.array("i", [1, 2, 3, 4]))
        self.assertEqual((view.elem, view.shape, view.strides, view.ndim, view.size), ("I32", (4,), (1,), 1, 4))
        self.assertEqual((view.writable, view.publish, view.protocol), (False, None, "buffer protocol"))
        self.assertEqual(repr(view), "<cint.View I32[4] read-only, no publish via buffer protocol>")
        with self.assertRaises(TypeError):
            cint.View()


class DLPack(BorrowCase):
    """P-10 to P-14 against a producer over ctypes storage, which counts its
    exports and deleter calls."""

    def test_refused_tensors_are_deleted(self):
        cases = [
            (Producer([1], flags=_dlpack.FLAG_IS_COPIED), "IS_COPIED"),
            (Producer([1], version=(2, 0)), "DLPack version 2.0"),
            (Producer([1.5], ctype=ctypes.c_double, dtype=(_dlpack.kDLFloat, 64, 1)), "cannot be borrowed"),
            (Producer([1], dtype=(_dlpack.kDLInt, 64, 2)), "lanes"),
            (Producer([1], dtype=(_dlpack.kDLOpaqueHandle, 64, 1)), "maps to no element"),
            (Producer([1, 2], byte_offset=4), "byte_offset 4 is not a multiple"),
            (Producer([1], shape=()), "rank 0 is not borrowed"),
        ]
        for producer, fragment in cases:
            self.refused(producer, fragment)
            self.assertEqual((producer.exported, producer.deleted, producer.unknown_deletes), (1, 1, 0), fragment)

    def test_device_refused_before_export(self):
        p = Producer([1], device=(2, 0))
        self.refused(p, "kDLCUDA:0", "CPU memory only")
        self.assertEqual(p.exported, 0)

    def test_device_in_the_tensor(self):
        class Lying(Producer):
            def __dlpack_device__(self):
                return (_dlpack.kDLCPU, 0)
        p = Lying([1], device=(2, 1))
        self.refused(p, "kDLCUDA:1")
        self.assertEqual(p.deleted, 1)

    def test_copy_false_refused_by_the_producer(self):
        self.refused(Producer([1], refuse_copy_false=True), "cannot export without a copy")

    def test_legacy_producer(self):
        p = Producer([1, 2], legacy_signature=True)
        view = self.borrowed(p)
        self.assertEqual(p.calls, [{"stream": None}])
        self.assertIn("legacy DLPack producer: copy=False could not be requested (SPEC-03 P-10a)", view.notes)
        self.refused(Producer([1, 2], legacy_signature=True), "unversioned DLPack capsule", writable=True)

    def test_unversioned_capsule(self):
        view = self.borrowed(Producer([1, 2], versioned=False))
        self.assertEqual(view.notes, ("unversioned DLPack capsule: read-only status cannot be expressed "
                                      "(SPEC-03 P-10a)",))

    def test_bool_and_offsets(self):
        view = self.borrowed(Producer([0, 1], ctype=ctypes.c_uint8, dtype=(_dlpack.kDLBool, 8, 1)))
        self.assertEqual((view.elem, view.tolist()), ("Bool", [False, True]))
        p = Producer([1, 2, 3], shape=(2,), byte_offset=8)
        view = self.borrowed(p)
        self.assertEqual((view.region.address, view.tolist()), (p.address + 8, [2, 3]))
        p = Producer([1, 2, 3], strides=(-1,), byte_offset=16)
        r = self.borrowed(p).region
        self.assertEqual((r.address, r.extent, r.origin), (p.address, 3, 2))
        self.assertEqual(self.borrowed(Producer([1, 2, 3], strides=(-1,), byte_offset=16)).tolist(), [3, 2, 1])

    def test_unsigned_and_rank(self):
        p = Producer([1, 2, 3, 4, 5, 6], ctype=ctypes.c_uint16, dtype=(_dlpack.kDLUInt, 16, 1), shape=(2, 3))
        view = self.borrowed(p)
        self.assertEqual((view.elem, view.tolist()), ("U16", [[1, 2, 3], [4, 5, 6]]))
        self.refused(Producer([0] * 32, shape=(2, 2, 2, 2, 2)), "rank 5")

    def test_exchange_per_entry(self):
        """P-10: each entry asks the producer again and compares; P-14: the
        capsule of that entry is deleted when the entry ends."""
        p = Producer([1, 2, 3])
        view = cint.borrow(p)
        with view._entry_exchange() as t:
            self.assertEqual((t.data, p.exported, p.deleted), (p.address, 2, 0))
        self.assertEqual(p.deleted, 1)
        p.shift = 8
        with self.assertRaises(cint.Refused) as caught:
            with view._entry_exchange():
                pass
        self.assertIn("SPEC-03 P-10", str(caught.exception))
        self.assertEqual((p.exported, p.deleted), (3, 2))
        view.release()
        self.assertEqual((p.deleted, p.unknown_deletes, p.live), (3, 0, {}))
        with self.assertRaises(cint.StaleHandle):
            with view._entry_exchange():
                pass

    def test_buffer_protocol_needs_no_exchange(self):
        with self.borrowed(bytearray(8))._entry_exchange() as t:
            self.assertIsNone(t)

    @unittest.skipIf(numpy is None, "NumPy is not installed")
    def test_numpy_dtypes_through_dlpack(self):
        view = self.borrowed(DLPackOnly(numpy.array([True, False])))
        self.assertEqual((view.elem, view.protocol), ("Bool", "DLPack"))
        e = self.refused(DLPackOnly(numpy.zeros(2)))
        self.assertIn("DLPack data type kDLFloat, 64 bits, cannot be borrowed", str(e))


if __name__ == "__main__":
    unittest.main()
