# compiler/OPEN.md: open items

Recorded questions and known limitations of `cintc`, the compiler written in CINT, by entry ID, with each entry's status at this release. Source comments cite these IDs. The full working records are not published; an entry whose status is Open or Proposed may still change.

### CINTC-OQ-01. The limit identifier of a C9001 row

Status: Proposed.

### CINTC-OQ-02. Record ids 0 and 1 depend on the seed's struct order

Status: Proposed.

### CINTC-OQ-03. Fault slots of retained diagnostics

Status: Proposed.

### CINTC-OQ-04. A short emit pass

Status: Proposed.

### CINTC-OQ-05. The parse stack counts frames, two per level of nesting

Status: Proposed.

### CINTC-OQ-06. Where `cint_ref` and `compiler/parse.ci` differ

Status: Proposed.

### CINTC-OQ-07. The parser stops at its first error

Status: Proposed.

### CINTC-OQ-08. Lexing inside an interpolation hole

Status: Proposed.

### CINTC-OQ-09. Codes for syntax errors in constructs `cint_ref` refuses

Status: Proposed.

### CINTC-OQ-10. The bytes of the literal-value table

Status: Proposed.

### CINTC-OQ-11. Revision and source-map digest of a compile-time record

Status: Proposed.

### CINTC-OQ-12. Where the work buffer lives

Status: Proposed.

### CINTC-OQ-13. The checker stops at its first error, in `cint_ref`'s order

Status: Proposed.

### CINTC-OQ-14. Where the script-mode note goes in the diagnostic table

Status: Proposed.

### CINTC-OQ-15. The subset `check.ci` implements, and C9102

Status: Proposed.

### CINTC-OQ-16. Rows of the checker's tables that CINTC-02 does not list

Status: Proposed.

### CINTC-OQ-17. C3030 belongs to the plan stage

Status: Recorded.

### CINTC-OQ-30. What lowering takes from the checker

Status: Proposed.

### CINTC-OQ-31. Name lookup is not O(n log n)

Status: Proposed.

### CINTC-OQ-32. `compiler/lower.ci` cannot be imported

Status: Proposed.

### CINTC-OQ-33. How an internal compiler error names its cause

Status: Proposed.

### CINTC-OQ-34. Unused functions and the fixpoint that finds them

Status: Proposed.

### CINTC-OQ-35. The text forms SIR-09 does not show

Status: Decided, 2026-10-05.

### CINTC-OQ-36. The argument of the generated `main`

Status: Proposed.

### CINTC-OQ-37. Output 1 carries the SIR text

Status: Decided, 2026-10-05.

### CINTC-OQ-38. One observer array across modules

Status: Proposed.

### CINTC-OQ-39. Which module is the root, and the order of the program table

Status: Proposed.

### CINTC-OQ-40. Revision identity and compiler source identity are absent

Status: Closed, 2026-10-03.

### CINTC-OQ-41. Constants and record ids

Status: Proposed.

### CINTC-OQ-42. What lowering covers in task 2.11

Status: Recorded.

### CINTC-OQ-43. The memory constant K

Status: Proposed.

### CINTC-OQ-44. Fuel charge nodes

Status: Proposed.

### CINTC-OQ-45. A module outside the checker's subset is lowered unchecked

Status: Closed.

### CINTC-OQ-46. Fuel consumed of a program process

Status: Proposed.

### CINTC-OQ-47. Nested fallthrough and the reference completion rule

Status: Recorded.

### CINTC-OQ-48. B1 and the seed-only expectations of `boot/` refusal cases

Status: Closed.

### CINTC-OQ-49. Per-module `cg_` definitions are exported from ELF shared libraries

Status: Closed.

### CINTC-OQ-50. Imports that name no module of the build

Status: Proposed.

### CINTC-OQ-51. Imports through the declaration table, task 2.13 part (iii)

Status: Proposed.

### CINTC-OQ-56. Test blocks, `assert`, and the program entry of a module of tests

Status: Proposed.

### CINTC-OQ-57. Table capacity cases under the writer-case rule

Status: Proposed.

### CINTC-OQ-58. The reflection table in `cintc`: effect classes, the declaration table, the brace check, and error unions

Status: Proposed.

### CINTC-OQ-63. Box 12 in `cintc`: error unions, `defer`, `errdefer` and the `*_result` built-ins

Status: Decided, 2026-10-05.

### CINTC-OQ-64. Box 12 memory lifetimes in `cintc`: the checker, the lowering and the C

Status: Proposed.
