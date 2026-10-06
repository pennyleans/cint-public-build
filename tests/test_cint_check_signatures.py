"""SPEC-09 CONF-16 in tools/cint_check.py (box 10 default BX10-29), on synthetic C text only.

The export rows of a module's reflection table, in the form compiler/back_c.ci writes, are
compared with the type signatures cint_ref writes (ref/cint_ref/sig.py). No compiler runs."""
import hashlib
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import cint_check
from cint_ref import sig  # cint_check puts ref/ on the path

ROOT_REL = "public_records.ci"
SOURCE = b"""struct Simple { I64 value; Bool flag; }

export Simple both(inout Simple a, Simple b, I64 divisor) {
    a.value = 42;
    return Simple(10 / divisor, b.flag);
}

export I64 twice(I64 x) {
    return x * 2;
}
"""
# SPEC-03 5.6: the test vector of `both`.
BOTH_SHA256 = "e604b42aa501289f82feba1ba82519b757db691ffef9b1aa0ddf737b3d281154"


def module_c(prefix, rows, wrappers=None, declared=None, effect="CINT_PURE"):
    """A module's C file as compiler/back_c.ci writes it: the cx wrappers (by default one per
    row), one signature array per row, sixteen bytes to a line, and the export table."""
    out = []
    for name in ([n for n, _ in rows] if wrappers is None else wrappers):
        out += ["CINT_RT_EXPORT cint_status cx_%s_%d_%s(cint_ctx *ctx, int64_t fuel, int64_t *result)"
                % (prefix, len(name), name), "{", "    return CINT_OK;", "}", ""]
    for j, (_, data) in enumerate(rows):
        out.append("static const uint8_t cg_%s_xs%d[] = {" % (prefix, j))
        out += ["    " + ", ".join("0x%02x" % b for b in data[k:k + 16]) + ","
                for k in range(0, len(data), 16)]
        out += ["};", ""]
    out.append("static const cint_export cg_%s_exports[%d] = {"
               % (prefix, len(rows) if declared is None else declared))
    for j, (name, _) in enumerate(rows):
        out.append('    {{"%s", %du, 0u}, CINT_EXPORT_FUNCTION, %s, 0u, 1u, NULL, '
                   '&cg_%s_xn%d[0], cg_%s_xs%d, (uint64_t)sizeof(cg_%s_xs%d), '
                   '(cint_entry_fn)cx_%s_%d_%s},'
                   % (name, len(name), effect, prefix, j, prefix, j, prefix, j, prefix,
                      len(name), name))
    out += ["};", ""]
    return "\n".join(out).encode("ascii")


class Signatures(unittest.TestCase):
    def setUp(self):
        self.exports = sig.program_exports(SOURCE, ROOT_REL)
        self.ref = {e.name: e.signature for e in self.exports}
        self.rows = [(e.name, e.signature) for e in self.exports]

    def compare(self, rows, **kwargs):
        files = {"public_records.c": module_c("17_public_x5Frecords", rows, **kwargs)}
        return cint_check.signature_problems(files, [ROOT_REL], ROOT_REL, self.exports)

    def test_cint_ref_writes_the_spec_vector(self):
        self.assertEqual([e.name for e in self.exports], ["both", "twice"])
        self.assertEqual(hashlib.sha256(self.ref["both"]).hexdigest(), BOTH_SHA256)

    def test_rows_are_read_in_the_emitted_form(self):
        text = module_c("17_public_x5Frecords", self.rows).decode("ascii")
        self.assertEqual(cint_check.table_rows(text), (self.rows, []))
        # The effect class is not part of the type signature (SPEC-03 H-2, H-7).
        text = module_c("17_public_x5Frecords", self.rows, effect="CINT_OBSERVE").decode("ascii")
        self.assertEqual(cint_check.table_rows(text), (self.rows, []))

    def test_equal_bytes_agree(self):
        self.assertEqual(self.compare(self.rows), (2, []))

    def test_a_corrupted_byte_is_caught(self):
        for at in (0, 33, 50, len(self.ref["both"]) - 1):
            with self.subTest(at=at):
                data = bytearray(self.ref["both"])
                data[at] ^= 0x01
                compared, bad = self.compare([("both", bytes(data)), self.rows[1]])
                self.assertEqual(compared, 2)
                self.assertEqual(bad, [{
                    "module": ROOT_REL, "export": "both",
                    "reason": "the type signature differs from cint_ref's at byte %d" % at,
                    "actual": bytes(data).hex(), "expected": self.ref["both"].hex()}])
        # A shorter or longer signature differs where the shorter one ends.
        for data in (self.ref["both"][:-1], self.ref["both"] + b"\x00"):
            with self.subTest(length=len(data)):
                _, bad = self.compare([("both", data), self.rows[1]])
                self.assertEqual([d["reason"] for d in bad],
                                 ["the type signature differs from cint_ref's at byte %d"
                                  % (len(self.ref["both"]) - 1 + (len(data) > len(self.ref["both"])))])

    def test_a_row_without_a_cint_ref_signature_disagrees(self):
        compared, bad = self.compare(self.rows + [("ghost", self.ref["twice"])])
        self.assertEqual(compared, 3)
        self.assertEqual(bad, [{"module": ROOT_REL, "export": "ghost",
                                "reason": "a row and a cx wrapper, and no cint_ref type signature: "
                                          "not an export of the module"}])
        # An export cint_ref gives no signature (A-25), and a program cint_ref does not accept.
        exports = [self.exports[0], sig.Export("twice", "function", None, "no carrier")]
        files = {"public_records.c": module_c("17_public_x5Frecords", self.rows)}
        _, bad = cint_check.signature_problems(files, [ROOT_REL], ROOT_REL, exports)
        self.assertEqual([d["reason"] for d in bad], [
            "a row and a cx wrapper, and no cint_ref type signature: no carrier"])
        compared, bad = cint_check.signature_problems(files, [ROOT_REL], ROOT_REL,
                                                      "cint_ref refuses the program: x")
        self.assertEqual((compared, [d["export"] for d in bad]), (2, ["both", "twice"]))
        self.assertTrue(all(d["reason"].endswith(": cint_ref refuses the program: x") for d in bad))

    def test_a_missing_row_disagrees(self):
        compared, bad = self.compare(self.rows[:1])
        self.assertEqual(compared, 2)
        self.assertEqual(bad, [{"module": ROOT_REL, "export": "twice",
                                "reason": "cint_ref writes a type signature, and the table has "
                                          "no row"}])
        # An exported kernel has a cx wrapper and a row since box 10 unit 3, so its missing row
        # disagrees too.
        exports = [self.exports[0], self.exports[1]._replace(kind="kernel")]
        files = {"public_records.c": module_c("17_public_x5Frecords", self.rows[:1])}
        compared, bad = cint_check.signature_problems(files, [ROOT_REL], ROOT_REL, exports)
        self.assertEqual((compared, [d["export"] for d in bad]), (2, ["twice"]))

    def test_rows_follow_declaration_order(self):
        _, bad = self.compare(self.rows[::-1])
        self.assertEqual(bad, [{"module": ROOT_REL,
                                "reason": "rows not in declaration order: twice, both"}])
        _, bad = self.compare(self.rows + self.rows[:1])
        self.assertIn({"module": ROOT_REL, "export": "both", "reason": "two rows"}, bad)

    def test_only_the_root_module_has_rows(self):
        # SPEC-03 A-12, interim rule: an imported module's exports have no wrapper and no row.
        files = {"public_records.c": module_c("17_public_x5Frecords", self.rows),
                 "lib/geo.c": module_c("7_lib_x2Egeo", [])}
        rels = ["lib/geo.ci", ROOT_REL]
        self.assertEqual(cint_check.signature_problems(files, rels, ROOT_REL, self.exports), (2, []))
        files["lib/geo.c"] = module_c("7_lib_x2Egeo", [("helper", self.ref["twice"])])
        compared, bad = cint_check.signature_problems(files, rels, ROOT_REL, self.exports)
        self.assertEqual((compared, bad), (3, [{
            "module": "lib/geo.ci", "export": "helper",
            "reason": "a row and a cx wrapper, and no cint_ref type signature: not the root "
                      "module (SPEC-03 A-12, interim rule)"}]))
        del files["lib/geo.c"]
        _, bad = cint_check.signature_problems(files, rels, ROOT_REL, self.exports)
        self.assertEqual(bad, [{"module": "lib/geo.ci", "reason": "no C file in the output set"}])

    def test_wrappers_and_rows_correspond(self):
        _, bad = self.compare(self.rows, wrappers=["both", "twice", "extra"])
        self.assertEqual([d["reason"] for d in bad],
                         ["cx wrapper cx_17_public_x5Frecords_5_extra has no row"])
        _, bad = self.compare(self.rows, wrappers=["both"])
        self.assertEqual([d["reason"] for d in bad],
                         ["a row names cx_17_public_x5Frecords_5_twice, which the file does not "
                          "define"])

    def test_an_unreadable_table_disagrees(self):
        good = module_c("17_public_x5Frecords", self.rows).decode("ascii")
        for text, reason in (
                (good.replace('"twice", 5u', '"twice", 4u'),
                 "the row of twice names its length or its signature array inconsistently"),
                (good.replace("sizeof(cg_17_public_x5Frecords_xs1)", "sizeof(cg_17_public_x5Frecords_xs0)"),
                 "the row of twice names its length or its signature array inconsistently"),
                (good.replace("CINT_EXPORT_FUNCTION, CINT_PURE, 0u, 1u, NULL, &cg_17_public_x5Frecords_xn1",
                              "CINT_EXPORT_FUNCTION CINT_PURE, 0u, 1u, NULL, &cg_17_public_x5Frecords_xn1"),
                 "a row not in the form compiler/back_c.ci writes: "),
                (good.replace("_exports[2]", "_exports[3]"),
                 "cg_17_public_x5Frecords_exports declares 3 rows and holds 2"),
                (good.replace("    0x1d, 0x00", "    0x1d, 0x0", 1),
                 "cg_17_public_x5Frecords_xs0 is not a list of bytes")):
            with self.subTest(reason=reason):
                rows, problems = cint_check.table_rows(text)
                self.assertTrue(any(p.startswith(reason) for p in problems), problems)
                files = {"public_records.c": text.encode("ascii")}
                _, bad = cint_check.signature_problems(files, [ROOT_REL], ROOT_REL, self.exports)
                self.assertTrue(any(d["reason"].startswith(reason) for d in bad), bad)

    def test_cint_ref_exports_of_a_program(self):
        with tempfile.TemporaryDirectory(prefix="synthetic-sig-") as directory:
            root = pathlib.Path(directory)
            (root / ROOT_REL).write_bytes(SOURCE)
            p = cint_check.Program("public_records", root, ROOT_REL, "program")
            self.assertEqual([(e.name, e.signature) for e in cint_check.ref_exports(p)], self.rows)
            (root / ROOT_REL).write_bytes(b"export I64 f() { return y; }\n")
            self.assertRegex(cint_check.ref_exports(p),
                             r"^cint_ref reports C\d{4} at public_records\.ci:1:\d+$")


if __name__ == "__main__":
    unittest.main()
