"""The package surface: names, the standard library only, and no NumPy
needed (BX10-20; SPEC-03 6.1)."""
import ast
from pathlib import Path
import subprocess
import sys
import unittest

import cint

PACKAGE = Path(cint.__file__).resolve().parent


class Surface(unittest.TestCase):
    def test_names(self):
        for name in cint.__all__:
            self.assertTrue(hasattr(cint, name), name)
        spec_exceptions = ("Fault", "OverflowFault", "DivZeroFault", "BoundsFault", "ShapeFault", "ShiftFault",
                           "NarrowFault", "AliasFault", "StaleHandleFault", "FuelFault", "UnsupportedFault",
                           "DepthFault", "DomainFault", "HostError", "HazardError", "ResourceError", "Refused",
                           "BorrowRefused", "Busy", "Faulted", "Unpublished", "StaleHandle", "ReplayDivergence",
                           "Unreplayable")
        for name in spec_exceptions + ("ErrorResult", "borrow", "copy", "empty", "from_float", "to_float",
                                       "Fixed", "View", "Buffer", "FloatPolicy", "load", "Module", "Context",
                                       "LoadError", "BuildError"):
            self.assertIn(name, cint.__all__)

    def test_no_arena_capacity(self):
        """BX12-29: the host has no arena capacity setting. The frame arena's
        capacity, `frame_arena_elements` (BX10-12), is not an arena of A-10."""
        for path in PACKAGE.glob("*.py"):
            self.assertNotRegex(path.read_text(encoding="utf-8"), r"\barena_elements\b", path.name)

    def test_standard_library_only(self):
        """Every import is from the standard library or the package itself;
        NumPy is imported only inside the functions that serve it."""
        for path in sorted(PACKAGE.glob("*.py")):
            tree = ast.parse(path.read_text(encoding="utf-8"))
            top = {id(node) for node in tree.body}
            for node in ast.walk(tree):
                if isinstance(node, ast.Import):
                    names = [a.name for a in node.names]
                elif isinstance(node, ast.ImportFrom) and node.level == 0:
                    names = [node.module]
                else:
                    continue
                for name in names:
                    root = name.split(".")[0]
                    if root == "numpy":
                        self.assertNotIn(id(node), top, "%s imports numpy at module level" % path.name)
                    else:
                        self.assertIn(root, sys.stdlib_module_names, "%s imports %s" % (path.name, name))

    def test_without_numpy(self):
        script = "\n".join([
            "import sys",
            "sys.modules['numpy'] = None",
            "import array, cint",
            "v = cint.borrow(array.array('q', [1, 2]))",
            "b = cint.copy(v)",
            "f = cint.to_float(b)",
            "assert isinstance(f, memoryview) and f.tolist() == [1.0, 2.0], f",
            "assert cint.to_float(cint.copy([], elem='I64')).tolist() == []",
            "q = cint.from_float([1.5], 'Q16.16')",
            "assert q.tolist()[0].raw == 98304",
            "print('ok')",
        ])
        result = subprocess.run([sys.executable, "-c", script], cwd=PACKAGE.parent, capture_output=True, text=True)
        self.assertEqual((result.returncode, result.stdout.strip()), (0, "ok"), result.stderr)


if __name__ == "__main__":
    unittest.main()
