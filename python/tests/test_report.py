"""The copy report of an entry, in elements (BX10-17; SPEC-03 P-15, P-16, M-31)."""
import unittest

import cint


class CopyReport(unittest.TestCase):
    def test_record(self):
        r = cint.CopyRecord("out", "publish-copy", "I64", 1000)
        self.assertEqual(str(r), "publish-copy out: I64 x 1,000")
        cint.CopyRecord("a", "snapshot", "Q32.32", 0)
        cint.CopyRecord("b", "staging", "Bool", 3)
        for args in (("out", "bytes", "I64", 1), ("out", "staging", "F64", 1), ("out", "staging", "I64", -1),
                     ("out", "staging", "I64", True), ("out", "staging", "I64", "8")):
            with self.assertRaises(ValueError, msg=repr(args)):
                cint.CopyRecord(*args)

    def test_entry_report(self):
        copies = [cint.CopyRecord("next", "publish-copy", "I64", 1000), cint.CopyRecord("pos", "snapshot", "I64", 24)]
        report = cint.EntryReport("advance", copies, ["legacy DLPack producer"])
        self.assertEqual(report.copied_elements, 1024)
        self.assertEqual(str(report), "advance: 2 copies\n  publish-copy next: I64 x 1,000\n"
                                      "  snapshot pos: I64 x 24\n  note: legacy DLPack producer")
        self.assertEqual(str(cint.EntryReport("total")), "total: no copies")
        self.assertEqual(cint.EntryReport("total").copied_elements, 0)
        self.assertEqual(str(cint.EntryReport("f", copies[:1])).splitlines()[0], "f: 1 copy")
        with self.assertRaises(TypeError):
            cint.EntryReport("f", ["next"])


if __name__ == "__main__":
    unittest.main()
