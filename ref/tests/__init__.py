"""Tests for the cint_ref exact reference.

Both `python -m unittest ref.tests.test_arith` (from the repository root) and
`python -m unittest discover -s ref/tests` work: this package file and each
test module put `ref/` on `sys.path` so that `cint_ref` imports.
"""
import os
import sys

_REF = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _REF not in sys.path:
    sys.path.insert(0, _REF)
