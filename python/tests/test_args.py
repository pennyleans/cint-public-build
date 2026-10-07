"""Arguments of a call (BX10-04 ruled B, BX10-14; SPEC-03 P-9, P-21 to P-24).

A bare array for a read-only parameter is borrowed for the call, with no
copy; an array the entry writes must be a View or a Buffer; a list is
refused, naming cint.copy."""
import array
import gc
import unittest

import cint
from cint._args import Param, bind_argument, bind_arguments

from .support import allocations, numpy

A_IN = Param("a", "in", "I64", 1)
A_INOUT = Param("a", "inout", "I64", 1)
A_OUT = Param("out", "out", "I64", 1)


class Parameters(unittest.TestCase):
    def test_validation(self):
        for args in (("x", "ref", "I64"), ("x", "inout", "I64"), ("x", "in", "I64", 5), ("x", "in", "F64")):
            with self.assertRaises(ValueError, msg=repr(args)):
                Param(*args)
        self.assertFalse(Param("r", "out", "I64").argument)
        self.assertTrue(A_OUT.argument and A_OUT.written and not A_IN.written)
        self.assertEqual(Param("m", "in", "Q32.32", 2).describe(), "in Q32.32[n0, n1] m")


class BareArguments(unittest.TestCase):
    """BX10-04: borrowed read-only for the call, released when it returns."""

    def test_borrowed_for_the_call(self):
        data = array.array("q", [1, 2, 3])
        before = allocations()
        bound = bind_argument(data, A_IN)
        self.assertEqual(allocations(), before, "a bare argument was copied")
        self.assertTrue(bound.borrowed)
        view = bound.value
        self.assertIsInstance(view, cint.View)
        self.assertEqual((view.writable, view.region.address), (False, data.buffer_info()[0]))
        view.release()

    def test_released_when_the_call_ends(self):
        data = bytearray(8)
        with bind_arguments([Param("a", "in", "U8", 1)], (data,), {}) as call:
            view = call.bounds[0].value
            with self.assertRaises(BufferError):
                data.extend(b"x")
        self.assertTrue(view.released)
        data.extend(b"x")

    @unittest.skipIf(numpy is None, "NumPy is not installed")
    def test_numpy_without_a_copy(self):
        a = numpy.arange(5, dtype=numpy.int64)
        before = allocations()
        with bind_arguments([A_IN], (a,), {}) as call:
            self.assertEqual(call.bounds[0].value.region.address, a.ctypes.data)
        self.assertEqual(allocations(), before)

    def test_sequences_refused(self):
        for seq in ([1, 2, 3], (1, 2), range(3)):
            with self.assertRaises(cint.Refused) as caught:
                bind_argument(seq, A_IN)
            text = str(caught.exception)
            self.assertIn("cint.copy(seq, elem='I64') copies it exactly", text)
            self.assertIn("BX10-04", text)

    def test_text_refused(self):
        with self.assertRaises(cint.Refused) as caught:
            bind_argument("hello", Param("s", "in", "U8", 1))
        self.assertIn('s.encode("utf-8")', str(caught.exception))
        bound = bind_argument("hello".encode("utf-8"), Param("s", "in", "U8", 1))
        self.assertEqual(bound.value.tolist(), list(b"hello"))
        bound.value.release()

    def test_scalars_for_arrays_refused(self):
        for value in (5, cint.Fixed("Q16.16", raw=1)):
            with self.assertRaises(cint.Refused):
                bind_argument(value, A_IN)
        with self.assertRaises(cint.Refused) as caught:
            bind_argument(2.5, A_IN)
        self.assertEqual(caught.exception.code, "E_UNSUPPORTED")

    def test_borrow_refusals_pass_through(self):
        with self.assertRaises(cint.BorrowRefused) as caught:
            bind_argument(array.array("d", [1.0]), A_IN)
        self.assertIn("cannot be borrowed", str(caught.exception))


class WrittenArguments(unittest.TestCase):
    """BX10-04 and BX10-14: an array the entry may write is a View or a
    Buffer, passed by name; none is allocated implicitly."""

    def test_bare_refused(self):
        data = array.array("q", [1])
        with self.assertRaises(cint.Refused) as caught:
            bind_argument(data, A_INOUT)
        self.assertIn("cint.borrow(a, writable=True), or a Buffer from cint.copy(a)", str(caught.exception))
        with self.assertRaises(cint.Refused) as caught:
            bind_argument(data, A_OUT)
        self.assertIn("cint.empty(shape, 'I64')", str(caught.exception))
        with self.assertRaises(cint.Refused):
            bind_argument([1], A_OUT)

    def test_views_and_buffers(self):
        data = array.array("q", [1, 2])
        view = cint.borrow(data, writable=True)
        bound = bind_argument(view, A_INOUT)
        self.assertIs(bound.value, view)
        self.assertFalse(bound.borrowed)
        view.release()
        with self.assertRaises(cint.StaleHandle):
            bind_argument(view, A_INOUT)
        out = cint.empty((2,), "I64")
        self.assertIs(bind_argument(out, A_OUT).value, out)
        with self.assertRaises(cint.Unpublished):
            bind_argument(out, A_INOUT)
        with self.assertRaises(cint.Unpublished):
            bind_argument(out, A_IN)
        out.release()
        with self.assertRaises(cint.StaleHandle):
            bind_argument(out, A_OUT)

    def test_output_with_live_exports(self):
        """P-18 (case 19 at the argument layer): Busy, E_ALIAS."""
        b = cint.copy([1, 2], elem="I64")
        m = b.memoryview()
        with self.assertRaises(cint.Busy) as caught:
            bind_argument(b, A_OUT)
        self.assertEqual(caught.exception.code, "E_ALIAS")
        bind_argument(b, A_IN)
        del m
        gc.collect()
        self.assertIs(bind_argument(b, A_OUT).value, b)

    def test_scalars_take_values(self):
        with self.assertRaises(cint.Refused):
            bind_argument(cint.copy([1], elem="I64"), Param("n", "in", "I64"))
        self.assertEqual(bind_argument(7, Param("n", "in", "I64")).value, 7)


class Placement(unittest.TestCase):
    """BX10-14: positional arguments fill the `in` parameters in order; a
    written argument is passed by name; a scalar `out` is a result."""

    PARAMS = [Param("a", "in", "I64", 1), Param("n", "in", "I64"), Param("out", "out", "I64", 1),
              Param("count", "out", "I64")]

    def test_placed(self):
        out = cint.empty((3,), "I64")
        with bind_arguments(self.PARAMS, (array.array("q", [1, 2, 3]), 3), {"out": out}) as call:
            self.assertEqual([b.param.name for b in call.bounds], ["a", "n", "out"])
            self.assertEqual(call.bounds[1].value, 3)
            self.assertIs(call.bounds[2].value, out)

    def test_by_name(self):
        out = cint.empty((1,), "I64")
        with bind_arguments(self.PARAMS, (), {"a": array.array("q", [1]), "n": 1, "out": out}) as call:
            self.assertEqual(len(call.bounds), 3)

    def test_errors(self):
        data, out = array.array("q", [1]), cint.empty((1,), "I64")
        with self.assertRaises(cint.Refused) as caught:
            bind_arguments(self.PARAMS, (data, 1), {})
        self.assertIn("out=cint.empty(shape, 'I64')", str(caught.exception))
        with self.assertRaises(TypeError):
            bind_arguments(self.PARAMS, (data,), {"out": out})
        with self.assertRaises(TypeError):
            bind_arguments(self.PARAMS, (data, 1, out), {})
        with self.assertRaises(TypeError):
            bind_arguments(self.PARAMS, (data, 1), {"out": out, "count": 0})
        with self.assertRaises(TypeError):
            bind_arguments(self.PARAMS, (data, 1), {"out": out, "a": data})

    def test_borrows_released_on_refusal(self):
        data = bytearray(8)
        with self.assertRaises(cint.Refused):
            bind_arguments([Param("a", "in", "U8", 1), Param("b", "in", "U8", 1)], (data, [1, 2]), {})
        data.extend(b"x")


if __name__ == "__main__":
    unittest.main()
