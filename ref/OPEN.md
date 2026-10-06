# ref/OPEN.md: open items

Recorded questions and known limitations of `cint_ref`, the reference implementation, and the conformance fixtures, by entry ID, with each entry's status at this release. Source comments cite these IDs. The full working records are not published; an entry whose status is Open or Proposed may still change.

### REF-OQ-01. The `.expect` layout is not yet specified

Status: Provisional.

### REF-OQ-02. Constant-expression typing: when is a literal's range checked?

Status: Closed, 2026-10-03.

### REF-OQ-03. Operation identifiers that SPEC-01 9.8 does not define

Status: Closed, 2026-10-03.

### REF-OQ-04. The `limit` of an `E_SHIFT` fault

Status: Closed, 2026-10-03.

### REF-OQ-05. The `assert` fault: position, operands and message

Status: Closed, 2026-10-03.

### REF-OQ-06. Positions of fuel faults

Status: Closed, 2026-10-03.

### REF-OQ-07. Type of `for` range bounds: SPEC-01 2.2 and SPEC-04 8.4 disagree

Status: Closed, 2026-10-03.

### REF-OQ-08. Compile errors with no assigned code

Status: Closed, 2026-10-03.

### REF-OQ-09. A negative literal with whitespace after the minus sign

Status: Closed, 2026-10-03.

### REF-OQ-10. Format specifications the rules do not cover

Status: Closed, 2026-10-03.

### REF-OQ-11. Which compile error is "the first"

Status: Closed, 2026-10-03.

### REF-OQ-12. How a program-level case names its entry

Status: Confirmed, 2026-10-03.

### REF-OQ-13. The module-relative path of a conformance case

Status: Confirmed, 2026-10-03.

### REF-OQ-14. Reachability for C4001

Status: Closed, 2026-10-03.

### REF-OQ-15. `clamp` with `lo > hi` at run time

Status: Refused.

### REF-OQ-16. `\x` escapes in character literals

Status: Confirmed, 2026-10-03.

### REF-OQ-17. The `stack` field and the entry

Status: Provisional.

### REF-OQ-18. `do ... while` and fuel

Status: Refused.

### REF-OQ-19. Default fuel allowance for `cint_ref run`

Status: Confirmed, 2026-10-03.

### REF-OQ-20. Which unit is evaluated at compile time, and does short-circuiting suppress C6001?

Status: Closed, 2026-10-03.

### REF-OQ-21. A constant subexpression that run time may never evaluate

Status: Closed, 2026-10-03.

### REF-OQ-22. The left operand of a literal-only shift inside a binary operator

Status: Closed, 2026-10-03.

### REF-OQ-23. The `.expect` layout has no line for state after a fault

Status: Closed, 2026-10-03.

### REF-OQ-24. A byte-order mark that is not at the start of the file

Status: Confirmed, 2026-10-03.

### REF-OQ-25. The position of C2003 for a parenthesized literal

Status: Confirmed, 2026-10-03.

### REF-OQ-26. Nested block comments and `cint-boot-1`

Status: Confirmed, 2026-10-03.

### REF-OQ-27. A module variable read in a function declared before it

Status: Closed, 2026-10-03.

### REF-OQ-28. A value-returning `main`

Status: Proposed, 2026-10-03.

### REF-OQ-29. Codes and positions chosen where the table is silent

Status: Proposed, 2026-10-03.

### REF-OQ-30. A string literal where an `in U8` view is expected

Status: Proposed, 2026-10-03.

### REF-OQ-31. Run-time copy shape mismatch (Closed 2026-10-04); negative extents

Status: Closed, 2026-10-05.

### REF-OQ-32. Entry checks of a call between CINT functions

Status: Confirmed, 2026-10-05.

### REF-OQ-33. Which arguments C5012 covers

Status: Proposed, 2026-10-03.

### REF-OQ-34. Programs of several modules

Status: Proposed, 2026-10-03.

### REF-OQ-35. Frame arena capacity

Status: Closed, 2026-10-06.

### REF-OQ-36. The IM-149 reader: tags it refuses and checks beyond IM-148

Status: Proposed, 2026-10-03.

### REF-OQ-37. A slice with no low bound is a syntax error

Status: Closed, 2026-10-05.

### REF-OQ-38. Record containment through arrays and imported names

Status: Recorded, 2026-10-04.

### REF-OQ-39. Module variable types used by earlier type checks

Status: Recorded, 2026-10-04.

### REF-OQ-40. A `where` constraint that is false at dispatch entry

Status: Closed, 2026-10-05.

### REF-OQ-41. Box 09 forms with no specified diagnostic or record

Status: Refused, 2026-10-05.

### REF-OQ-42. A format width past `I64` and a print statement past 1 GiB

Status: Refused, 2026-10-05.

### REF-OQ-43. Readings the box 12 build chose

Status: Provisional, 2026-10-05.

### REF-OQ-44. Box 12 forms with no specified diagnostic, record or meaning

Status: Refused, 2026-10-05.

### REF-OQ-45. Arenas, pools, handles and the frame arena

Status: Resolved in part, 2026-10-06.

### REF-OQ-46. Readings of the type signature encoder

Status: Provisional, 2026-10-05.

### O-1. CIF-1 has no `derivation` key

Status: Recorded.

### O-2. `limit` of an `E_SHIFT` fault for a negative count

Status: Recorded.

### O-3. Boundary-matrix value sets for `U64`, `U32` and `I32`

Status: Recorded.

### O-4. Values CIF-1 cannot write, and the type of a shift count in the 8-bit tables

Status: Recorded.

### O-5. One constant-expression table program per table cannot report each record

Status: Recorded.

### O-6. Case-list syntax and the module name of table programs

Status: Recorded.

### O-7. A CR LF source case conflicts with the planned line-ending lint

Status: Recorded.

### O-8. `exact` of an integer to fixed-point conversion that is out of range

Status: Recorded.

### O-9. Serialization and decoder fixtures have no CIF-1 operation identifier

Status: Recorded.

### O-10. How a program case names its entry, arguments and budgets

Status: Recorded.

### O-11. Position of an `E_FUEL` fault at a loop-iteration charge point

Status: Recorded.

### O-12. Position of a fault raised by `i++` or a compound assignment

Status: Recorded.

### O-13. "First character of the offending construct" for compile errors

Status: Recorded.

### O-14. `fault.stack-depth 0` in the CONF-01 example

Status: Recorded.

### O-15. The `total` example of SPEC-01 10.2 has no size parameter

Status: Recorded.

### I-1. The SPEC-04 5.1 example declares a local named `count`, a built-in name

Status: Recorded.

### I-2. Program cases outside the scalar surface of `cint_ref`

Status: Recorded.

### I-3. A case with several entries

Status: Recorded.

### I-4. Compile-error positions with two readings

Status: Recorded.

### I-5. `fault.stack-depth` and `fuel-consumed` in the generated `.expect` files

Status: Recorded.

### I-6. The seed's aliasing check is stricter than LS-121

Status: Open, 2026-10-03.

### I-7. Box 09 cases that a compiler rejects with another diagnostic

Status: Closed, 2026-10-06.

### I-8. Box 12 cases that a compiler rejects with another diagnostic

Status: Closed, 2026-10-05.
