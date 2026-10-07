"""cint.copy, cint.empty, and Buffer (SPEC-03 6.1, P-3a, P-17, P-18, A-13a;
case 16a; BX10-16)."""
import array
import ctypes
import gc
import pickle
import unittest

import cint
from cint import _memory

from .support import DLPackOnly, Producer, allocations, numpy

HLA_I64_BE = bytes.fromhex("0000000000003D09")      # HLAinteger64BE 15625


class Case16a(unittest.TestCase):
    """Case 16a: `cint.copy` of a big-endian `>i8` array holding the
    `HLAinteger64BE` value `0x0000000000003D09` makes a bridge-owned `I64`
    buffer with value 15625."""

    def check(self, source):
        before = allocations()
        b = cint.copy(source)
        self.assertEqual(allocations(), before + 1)
        self.assertEqual((b.elem, b.shape, b.tolist()), ("I64", (1,), [15625]))
        self.assertEqual(bytes(b.memoryview()), (15625).to_bytes(8, "little"))
        return b

    def test_ctypes_big_endian(self):
        big = (ctypes.c_int64.__ctype_be__ * 1)()
        ctypes.memmove(big, HLA_I64_BE, 8)
        self.assertEqual(memoryview(big).format, ">q")
        self.check(big)

    @unittest.skipIf(numpy is None, "NumPy is not installed")
    def test_numpy_big_endian(self):
        self.check(numpy.frombuffer(HLA_I64_BE, dtype=">i8"))

    @unittest.skipIf(numpy is None, "NumPy is not installed")
    def test_other_big_endian_types(self):
        cases = [(">i2", [-2, 300], "I16"), (">u4", [1, 4000000000], "U32"), (">i4", [-5], "I32"),
                 ("<i4", [-5], "I32"), (">u2", [65535], "U16"), (">i1", [-1], "I8")]
        for dtype, values, elem in cases:
            b = cint.copy(numpy.array(values, dtype=dtype))
            self.assertEqual((b.elem, b.tolist()), (elem, values), dtype)


class Copies(unittest.TestCase):
    def test_any_layout(self):
        a = array.array("i", range(6))
        b = cint.copy(memoryview(a).cast("B").cast("i", (2, 3)))
        self.assertEqual((b.elem, b.shape, b.tolist()), ("I32", (2, 3), [[0, 1, 2], [3, 4, 5]]))
        self.assertEqual(b.strides, (3, 1))

    @unittest.skipIf(numpy is None, "NumPy is not installed")
    def test_strided_numpy(self):
        a = numpy.arange(12, dtype=numpy.int16).reshape(3, 4)
        self.assertEqual(cint.copy(a[::-1, ::2]).tolist(), [[8, 10], [4, 6], [0, 2]])
        self.assertEqual(cint.copy(numpy.asfortranarray(a)).tolist(), a.tolist())

    def test_copies_are_copies(self):
        data = array.array("q", [1, 2, 3])
        view = cint.borrow(data)
        b = cint.copy(view)
        data[0] = 100
        self.assertEqual((b.tolist(), view.tolist()), ([1, 2, 3], [100, 2, 3]))
        self.assertEqual(cint.copy(b).tolist(), [1, 2, 3])
        view.release()

    def test_sequences(self):
        self.assertEqual(cint.copy([[1, 2], [3, 4]], elem="I8").tolist(), [[1, 2], [3, 4]])
        self.assertEqual(cint.copy((5, 6), elem="U16").tolist(), [5, 6])
        self.assertEqual(cint.copy(range(4), elem="I64").tolist(), [0, 1, 2, 3])
        self.assertEqual(cint.copy([], elem="I64").shape, (0,))
        b = cint.copy([[], []], elem="I64")
        self.assertEqual((b.shape, b.tolist()), ((2, 0), [[], []]))
        self.assertEqual(cint.copy([True, False], elem="Bool").tolist(), [True, False])

    def test_sequences_need_an_elem(self):
        with self.assertRaises(TypeError) as caught:
            cint.copy([1, 2])
        self.assertIn("cint.copy(seq, elem=\"I64\")", str(caught.exception))

    def test_shapes(self):
        for seq in ([[1, 2], [3]], [[1], 2], [1, [2]]):
            with self.assertRaises(cint.Refused) as caught:
                cint.copy(seq, elem="I64")
            self.assertEqual(caught.exception.code, "E_SHAPE")
        with self.assertRaises(cint.Refused):
            cint.copy([[[[[1]]]]], elem="I64")
        self.assertEqual(cint.copy([[[[1]]]], elem="I64").shape, (1, 1, 1, 1))

    def test_all_or_nothing(self):
        before = allocations()
        with self.assertRaises(cint.Refused) as caught:
            cint.copy([[1, 300], [-5, 2]], elem="U8")
        e = caught.exception
        self.assertEqual((e.code, e.index, e.count, e.exact, e.limit, e.limit_type),
                         ("E_NARROW", (0, 1), 2, 300, 255, "U8"))
        self.assertIn("2 of 4 elements refused, so no Buffer was made", str(e))
        self.assertEqual(allocations(), before)

    def test_values_follow_the_scalar_rules(self):
        with self.assertRaises(cint.Refused):
            cint.copy([1, True], elem="I64")
        with self.assertRaises(cint.Refused) as caught:
            cint.copy([1, 2.5], elem="I64")
        self.assertEqual((caught.exception.code, caught.exception.index), ("E_UNSUPPORTED", (1,)))
        with self.assertRaises(cint.Refused):
            cint.copy([1, 2], elem="Q16.16")
        b = cint.copy([cint.Fixed("Q16.16", value=1), cint.Fixed("Q16.16", raw=1)], elem="Q16.16")
        self.assertEqual([x.raw for x in b.tolist()], [65536, 1])
        with self.assertRaises(cint.Refused):
            cint.copy([1, 2], elem="Bool")

    @unittest.skipIf(numpy is None, "NumPy is not installed")
    def test_numpy_scalars_in_sequences(self):
        self.assertEqual(cint.copy([numpy.int64(5), numpy.uint8(6)], elem="I32").tolist(), [5, 6])
        with self.assertRaises(cint.Refused):
            cint.copy([numpy.int64(1 << 40)], elem="I32")

    def test_conversions_are_exact(self):
        data = array.array("q", [1, -2, 1 << 40])
        with self.assertRaises(cint.Refused) as caught:
            cint.copy(data, elem="I32")
        self.assertEqual((caught.exception.index, caught.exception.count), ((2,), 1))
        self.assertEqual(cint.copy(array.array("q", [1, -2]), elem="I8").tolist(), [1, -2])
        fixed = cint.copy(data, elem="Q32.32").tolist()
        self.assertEqual([x.raw for x in fixed], [1, -2, 1 << 40])
        with self.assertRaises(cint.Refused) as caught:
            cint.copy(array.array("i", [1]), elem="Q32.32")
        self.assertIn("two readings", str(caught.exception))
        with self.assertRaises(cint.Refused):
            cint.copy(memoryview(bytes([0, 1])).cast("?"), elem="U8")
        with self.assertRaises(cint.Refused):
            cint.copy(bytes([0, 1]), elem="Bool")

    def test_ternary_targets(self):
        self.assertEqual(cint.copy(array.array("b", [1, 0, -1]), elem="T1").tolist(), [1, 0, -1])
        with self.assertRaises(cint.Refused) as caught:
            cint.copy(array.array("b", [1, 3]), elem="T1")
        self.assertEqual((caught.exception.operation, caught.exception.index), ("decode.checked.t1", (1,)))
        t1 = cint.borrow(array.array("b", [1, -1]), elem="T1")
        self.assertEqual(cint.copy(t1).elem, "T1")
        t1.release()

    def test_bytes_copy_as_u8(self):
        b = cint.copy(b"abc")
        self.assertEqual((b.elem, b.tolist()), ("U8", [97, 98, 99]))

    def test_refusals(self):
        for obj in ("text", 5, cint.Fixed("Q16.16", raw=1), object()):
            with self.assertRaises(cint.Refused, msg=repr(obj)):
                cint.copy(obj, elem="I64")
        with self.assertRaises(cint.Refused) as caught:
            cint.copy(array.array("d", [1.5]))
        self.assertIn("cint.from_float(a, \"Q32.32\", rounding=\"half_even\")", str(caught.exception))
        with self.assertRaises(NotImplementedError):
            cint.copy([1], elem="I64", device="cuda:0")
        self.assertEqual(cint.copy([1], elem="I64", device="cpu").tolist(), [1])
        with self.assertRaises(TypeError):
            cint.copy([1.5], elem="Q16.16", from_float="half_even")
        with self.assertRaises(TypeError):
            cint.copy([1.5], from_float=cint.FloatPolicy())

    def test_dlpack_sources(self):
        p = Producer([3, 4, 5], ctype=ctypes.c_int32, dtype=(0, 32, 1))
        b = cint.copy(p)
        self.assertEqual((b.elem, b.tolist(), p.deleted), ("I32", [3, 4, 5], 1))
        with self.assertRaises(cint.Refused):
            cint.copy(Producer([1], device=(2, 0)))

    @unittest.skipIf(numpy is None, "NumPy is not installed")
    def test_numpy_dlpack_source(self):
        self.assertEqual(cint.copy(DLPackOnly(numpy.arange(3, dtype=numpy.uint8))).tolist(), [0, 1, 2])

    def test_wide_elements(self):
        values = [(1 << 1023) - 1, -(1 << 1023), 12345]
        b = cint.copy(values, elem="I1024")
        self.assertEqual(b.tolist(), values)
        self.assertEqual(b.region.extent, 3)
        with self.assertRaises(TypeError):
            b.memoryview()
        self.assertEqual(cint.copy(b).tolist(), values)


class Buffers(unittest.TestCase):
    ELEMS = ("I8", "I16", "I32", "I64", "I128", "I1024", "U8", "U16", "U32", "U64", "Q16.16", "Q32.32", "Q4.60",
             "Bool", "T1", "T27")

    def test_not_constructed_directly(self):
        with self.assertRaises(TypeError):
            cint.Buffer()

    def test_alignment(self):
        """A-13a: storage aligned to min(size, 8), here always to 8."""
        for elem in self.ELEMS:
            for n in (0, 1, 3):
                self.assertEqual(cint.empty((n,), elem).region.address % 8, 0, elem)

    def test_empty_is_unpublished(self):
        b = cint.empty((2, 3), "I64")
        self.assertEqual((b.published, b.shape, b.size, b.elem), (False, (2, 3), 6, "I64"))
        for read in (b.tolist, b.memoryview, lambda: cint.copy(b), lambda: cint.to_float(b)):
            with self.assertRaises(cint.Unpublished):
                read()
        self.assertEqual(b.region.extent, 6)
        b._mark_published()
        self.assertEqual(b.tolist(), [[0, 0, 0], [0, 0, 0]])
        self.assertEqual(repr(b), "<cint.Buffer I64[2, 3] published>")

    def test_shapes(self):
        self.assertEqual(cint.empty(4, "U8").shape, (4,))
        with self.assertRaises(cint.Refused):
            cint.empty((1, 1, 1, 1, 1), "U8")
        with self.assertRaises(cint.Refused):
            cint.empty((), "U8")
        with self.assertRaises(ValueError):
            cint.empty((-1,), "U8")
        with self.assertRaises(TypeError):
            cint.empty((True,), "U8")
        with self.assertRaises(ValueError):
            cint.empty((2,), "Str")
        with self.assertRaises(NotImplementedError):
            cint.empty((2,), "U8", device="cuda:0")

    def test_memoryview_formats(self):
        cases = [("I64", "q"), ("U8", "B"), ("Bool", "?"), ("T1", "b"), ("T27", "q"), ("Q16.16", "i"),
                 ("U64", "Q"), ("I16", "h")]
        for elem, code in cases:
            m = cint.empty((2, 2), elem)
            m._mark_published()
            mv = m.memoryview()
            self.assertEqual((mv.format, mv.shape, mv.readonly), (code, (2, 2), True), elem)

    def test_exports_are_counted(self):
        """P-18: exports are read-only and counted; an output with live exports
        is refused with Busy, E_ALIAS."""
        b = cint.copy([1, 2, 3], elem="I64")
        m1 = b.memoryview()
        m2 = b.memoryview()
        self.assertEqual(b.exports, 2)
        with self.assertRaises(cint.Busy) as caught:
            b._claim_output()
        self.assertEqual(caught.exception.code, "E_ALIAS")
        self.assertTrue(str(caught.exception).startswith("E_ALIAS: this Buffer has 2 live exports"))
        derived = m1[1:]
        del m1
        gc.collect()
        self.assertEqual(b.exports, 2)
        del derived, m2
        gc.collect()
        self.assertEqual(b.exports, 0)
        b._claim_output()

    @unittest.skipIf(numpy is None, "NumPy is not installed")
    def test_numpy_exports(self):
        b = cint.copy([1, 2, 3, 4], elem="I32")
        a = numpy.asarray(b)
        self.assertEqual((a.dtype, a.tolist(), a.flags.writeable), (numpy.dtype("int32"), [1, 2, 3, 4], False))
        self.assertEqual(b.exports, 1)
        with self.assertRaises(ValueError):
            a[0] = 5
        del a
        gc.collect()
        self.assertEqual(b.exports, 0)
        c = numpy.array(b)
        gc.collect()
        self.assertEqual((c.tolist(), b.exports), ([1, 2, 3, 4], 0))
        f = numpy.asarray(b, dtype=numpy.int64)
        gc.collect()
        self.assertEqual((f.dtype, f.tolist(), b.exports), (numpy.dtype("int64"), [1, 2, 3, 4], 0))

    def test_no_writable_export(self):
        b = cint.copy([1], elem="I64")
        with self.assertRaises(BufferError):
            _memory.HeldBuffer(b.memoryview(), 0x1d)

    def test_release(self):
        """BX10-16: release drops the storage; a live export stays valid until
        it ends, and every other use raises StaleHandle."""
        b = cint.copy([7, 8], elem="I64")
        m = b.memoryview()
        b.release()
        self.assertTrue(b.released)
        self.assertEqual(m.tolist(), [7, 8])
        for use in (b.tolist, b.memoryview, lambda: b.region, b._claim_output):
            with self.assertRaises(cint.StaleHandle):
                use()
        b.release()
        with cint.copy([1], elem="I64") as c:
            self.assertEqual(c.tolist(), [1])
        self.assertTrue(c.released)
        self.assertEqual(repr(c), "<cint.Buffer I64[1] released>")

    def test_not_pickled(self):
        with self.assertRaises(TypeError):
            pickle.dumps(cint.copy([1], elem="I64"))


if __name__ == "__main__":
    unittest.main()
