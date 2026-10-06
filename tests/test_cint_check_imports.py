"""tools/cint_check.py IMPORT_RE: the module list given to B1 names every imported module,
whatever the import form (G-C1 finding FX-6)."""

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))

import cint_check  # noqa: E402


class ImportPattern(unittest.TestCase):
    def test_forms(self):
        cases = ((b"import a.b;\n", [b"a.b"]), (b"import a.b as x;\n", [b"a.b"]),
                 (b"import a.b.{X, Y};\n", [b"a.b"]), (b"  import lib.c ;\n", [b"lib.c"]),
                 (b"// import x.y;\n", []), (b"import a.b as;\n", []))
        for text, want in cases:
            with self.subTest(text=text):
                self.assertEqual(cint_check.IMPORT_RE.findall(text), want)


if __name__ == "__main__":
    unittest.main()
