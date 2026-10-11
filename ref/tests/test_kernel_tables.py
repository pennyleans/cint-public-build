import os, sys; sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # put ref/ on the path
"""The kernel tables of the I64 boundary matrices (SPEC-09 CONF-17; SPEC-02 Q-4; box 11 unit 6):
cint_ref's outcome of each line of `conformance/tables/kernel_*.cases` is the frozen record of
the matrix it was generated from, so the format 4 expectations that tools/cint_check.py takes
from cint_ref for these tables are the matrices' own values."""
import json
import unittest

from cint_ref import parser as P
from cint_ref.check import check_module
from cint_ref.exec import DEFAULT_DEPTH, _check_args, _run
from cint_ref.lexer import decode_source
from cint_ref.types import Value

CONF = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                    "conformance")
TABLES = os.path.join(CONF, "tables")


class KernelTables(unittest.TestCase):
    def test_fourteen_tables(self):
        names = sorted(n for n in os.listdir(TABLES) if n.startswith("kernel_") and n.endswith(".ci"))
        self.assertEqual(len(names), 14)
        self.assertEqual(len(os.listdir(os.path.join(CONF, "integer-machine", "bnd64"))), 14)

    def test_each_case_line_gives_its_record(self):
        for name in sorted(n[:-3] for n in os.listdir(TABLES)
                           if n.startswith("kernel_") and n.endswith(".ci")):
            with self.subTest(table=name):
                rel = "tables/%s.ci" % name
                with open(os.path.join(CONF, rel), "rb") as fh:
                    src = fh.read()
                source = [l for l in src.decode("ascii").split("\n") if l.startswith("// source: ")]
                with open(os.path.join(CONF, source[0][len("// source: "):]), "rb") as fh:
                    recs = [json.loads(l) for l in fh.read().decode("utf-8").split("\n") if l]
                with open(os.path.join(TABLES, name + ".cases"), "rb") as fh:
                    lines = fh.read().decode("ascii").split("\n")[:-1]
                self.assertEqual(len(lines), len(recs))
                prog = check_module(P.parse_module(decode_source(src, rel), rel))
                for line, rec in zip(lines, recs):
                    module, entry, t, k = line.split(" ")
                    self.assertEqual(module, "tables." + name)
                    f = prog.functions[entry]
                    o = _run(prog, "function", f, _check_args(f, [Value(t, int(k))]), None,
                             DEFAULT_DEPTH)
                    want = rec["expect"]
                    if "value" in want:
                        self.assertEqual((entry, o.kind, o.value.type, o.value.value),
                                         ("row", "value", want["value"]["t"],
                                          int(want["value"]["v"])), rec["id"])
                        continue
                    fault = want["fault"]
                    self.assertEqual((entry, o.kind), ("fault_row", "fault"), rec["id"])
                    # a record asserts the fields it carries (an E_SHIFT record, its code only)
                    r = o.record
                    got = {"code": r.code, "exact": None if r.exact is None else str(r.exact),
                           "limit": None if r.limit is None else str(r.limit.value)}
                    self.assertEqual({k: got[k] for k in fault}, fault, rec["id"])
                    self.assertIsNotNone(r.address, rec["id"])   # raised by the dispatch


if __name__ == "__main__":
    unittest.main()
