"""The box 10 demonstration (BX10-22): `examples/matmul_i8.py` runs and writes
`examples/matmul_i8.stdout`, and its fault is the record the reference
interpreter gives the same call."""
import os
import subprocess
import sys
import unittest

import cint
from tests.support import ROOT, numpy, reference_path
from tests.test_calls import load, setUpModule  # noqa: F401 (setUpModule runs here too)

EXAMPLES = str(ROOT / "examples")


@unittest.skipIf(numpy is None, "NumPy is not installed; examples/matmul_i8.py needs it")
class MatmulI8(unittest.TestCase):
    def test_script(self):
        env = dict(os.environ)
        env["PYTHONPATH"] = os.pathsep.join(p for p in (str(ROOT / "python"), env.get("PYTHONPATH")) if p)
        run = subprocess.run([sys.executable, os.path.join(EXAMPLES, "matmul_i8.py")], env=env, cwd=str(ROOT),
                             capture_output=True, text=True, timeout=900)
        self.assertEqual(run.returncode, 0, run.stderr)
        with open(os.path.join(EXAMPLES, "matmul_i8.stdout"), encoding="utf-8") as f:
            self.assertEqual(run.stdout, f.read())

    def test_fault_matches_the_reference(self):
        """The overflow of matmul_narrow has the code, operation, operands, exact,
        limit and position that cint_ref gives the same call from `.ci`; only the
        stack differs, since there the call has a caller."""
        sys.path.insert(0, EXAMPLES)
        try:
            import matmul_i8
        finally:
            sys.path.remove(EXAMPLES)
        x, w = matmul_i8.inputs()
        mod = load("matmul_i8.ci", root=EXAMPLES)
        y8 = numpy.zeros((4, 3), dtype=numpy.int8)
        with self.assertRaises(cint.OverflowFault) as caught:
            mod.context().matmul_narrow(x, w, y=cint.borrow(y8, writable=True))
        got = caught.exception
        lines = ["export I64 run() {", "    I8[4, 16] x;", "    I8[16, 3] w;", "    I8[4, 3] y;"]
        lines += ["    x[%d, %d] = %d;" % (i, p, x[i, p]) for i in range(4) for p in range(16)]
        lines += ["    w[%d, %d] = %d;" % (p, j, w[p, j]) for p in range(16) for j in range(3)]
        lines += ["    matmul_narrow(x, w, y);", "    return 0;", "}", ""]
        with open(os.path.join(EXAMPLES, "matmul_i8.ci"), "rb") as f:
            source = f.read() + "\n".join(lines).encode()
        reference_path()
        from cint_ref.exec import DEFAULT_DEPTH, run_program
        from cint_ref.faults import RUN_TIME, encode_fault_record
        o = run_program(source, "matmul_i8.ci", "run", None, DEFAULT_DEPTH)
        self.assertEqual(o.kind, "fault")
        want = cint.Fault.from_record(encode_fault_record(o.record, RUN_TIME, 2))
        same = ("code", "operation", "operands", "operand_types", "exact", "limit", "limit_type", "position",
                "kernel")
        self.assertEqual({k: getattr(got, k) for k in same}, {k: getattr(want, k) for k in same})
        self.assertEqual((len(got.stack), len(want.stack)), (0, 1))
        self.assertEqual(y8.tolist(), [[27, -12, -15], [-12, 27, -15], [0, -19, 19], [-75, 0, 0]])


@unittest.skipIf(numpy is None, "NumPy is not installed; docs/tutorials/newcomer/p1_copies.py needs it")
@unittest.skipUnless((ROOT / "docs" / "tutorials" / "newcomer" / "p1_copies.py").exists(),
                     "the newcomer check's script is not in this tree")
class NewcomerTask3P(unittest.TestCase):
    """The measured answers of G-N task 3P (docs/tutorials/newcomer-check-box10.md,
    key results/cint/box10/newcomer/key-box10.json): the script runs line by line,
    and each question's values are read where it asks for them."""

    def test_answers(self):
        with open(str(ROOT / "docs" / "tutorials" / "newcomer" / "p1_copies.py"), encoding="utf-8") as f:
            lines = f.read().replace('"examples/matmul_i8.ci"', repr(os.path.join(EXAMPLES, "matmul_i8.ci")))
        lines = lines.split("\n")
        self.assertEqual(len(lines), 18)  # 17 lines and the final line feed
        ns = {}

        def upto(first, last):
            exec(compile("\n" * (first - 1) + "\n".join(lines[first - 1:last]), "p1_copies.py", "exec"), ns)

        def copies():
            return [(c.buffer, c.reason, c.elem, c.elements) for c in ns["ctx"].last_entry().copies]

        sixteen = [[16, 16, 16], [16, 16, 16]]
        upto(1, 9)
        self.assertEqual((ns["y"].tolist(), copies()), (sixteen, []))
        upto(10, 12)
        self.assertEqual((ns["z"].tolist(), copies()), (sixteen, [("z", "staging", "I32", 6)]))
        upto(13, 14)
        self.assertEqual((ns["z2"].tolist(), copies()), (sixteen, [("z", "publish-copy", "I32", 6)]))
        upto(15, 16)
        with self.assertRaises(cint.OverflowFault) as caught:
            upto(17, 17)
        f = caught.exception
        self.assertEqual((f.code, f.operation, f.operands, f.exact, f.limit, f.position, f.stack),
                         ("E_OVERFLOW", "dot.checked.i8.i8", (16,), 144, 127, "matmul_i8.ci:22:23", ()))
        self.assertEqual(ns["y8"].tolist(), [[16, 16, 16], [0, 0, 0]])
        self.assertEqual(ns["x"].tolist(), [[1] * 16, [9] * 16])
        self.assertEqual(ns["w"].tolist(), [[1, 1, 1]] * 16)
        for name in ("y", "b", "z", "z2"):
            self.assertEqual(ns[name].tolist(), sixteen, name)
        # Question 9.
        with self.assertRaises(cint.Faulted):
            ns["ctx"].relu(ns["y"], z=ns["z"])
        ns["ctx"].clear_fault()
        ns["ctx"].relu(ns["y"], z=ns["z"])
        self.assertEqual(ns["z"].tolist(), sixteen)
        # Question 8, on a new context.
        ctx = ns["mod"].context()
        z2 = numpy.zeros((2, 3), dtype=numpy.int32)
        with self.assertRaises(cint.UnsupportedFault) as caught:
            ctx.relu(ns["y"], z=cint.borrow(z2, writable=True))
        f = caught.exception
        self.assertEqual((f.operation, f.operands, f.position, f.phase),
                         ("bind.limit", (1,), "matmul_i8.ci:29:15", "entry"))
        self.assertIn('publish="copy"', str(f))
        self.assertEqual(z2.tolist(), [[0, 0, 0], [0, 0, 0]])
        self.assertIsNotNone(ctx.fault())


if __name__ == "__main__":
    unittest.main()
