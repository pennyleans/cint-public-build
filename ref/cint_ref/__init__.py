"""cint_ref: the Python exact reference for `cint-core-1` (SPEC-09 section 9.1).

Written from the specification text, with the Python standard library only
and no floating point: Python integers, and `fractions.Fraction` for the
value of a fraction literal. It shares no code, grammar file or generated
table with `cint-seed` or `cintc` (SPEC-09 ARCH-02, REF-01, REF-02).

Modules:
  types   numeric types and typed values (SPEC-01 section 2)
  arith   exact operations: checked, wrapping, saturating (SPEC-01 section 4)
  faults  fault codes, canonical fault records, diagnostics (SPEC-01 9.1, 9.2)
  lexer   source text and tokens (SPEC-04 section 3)
  tree    syntax tree
  parser  the scalar surface of SPEC-04 section 18
  check   names, types, context typing, constant expressions (SPEC-01 3.2)
  fmt     exact print formatting (SPEC-04 section 9)
  exec    execution, fuel-v1, call depth (SPEC-01 sections 8 to 10)
  expect  `.expect` reader and writer (SPEC-09 9.2)
  cif1    CIF-1 fixture reader, writer and evaluator (SPEC-01 13.1)

Questions the specification leaves open are listed in ref/OPEN.md.
"""
