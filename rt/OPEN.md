# rt/OPEN.md: open items

Recorded questions and known limitations of the C runtime (`cint-rt`), the bridge and the harness, by entry ID, with each entry's status at this release. Source comments cite these IDs. The full working records are not published; an entry whose status is Open or Proposed may still change.

### RT-OQ-01. Type and value of the `limit` of an `E_SHIFT` fault

Status: Closed, 2026-10-03.

### RT-OQ-02. An absent `revision` in the canonical fault record bytes

Status: Closed, 2026-10-03.

### RT-OQ-03. Refusing an entry whose depth limit exceeds the runtime's frames

Status: Confirmed as interim, 2026-10-03.

### RT-OQ-04. Fuel counting past `INT64_MAX` with no allowance

Status: Confirmed, 2026-10-03.

### RT-OQ-05. The `E_BOUNDS` record of an index check

Status: Confirmed, 2026-10-03.

### RT-OQ-06. Two context functions whose plan signatures differ from SPEC-03 5.3

Status: Closed, 2026-10-03.

### RT-OQ-07. Reading a context that holds no fault record

Status: Confirmed, 2026-10-03.

### RT-OQ-08. Checked helpers on a faulted context

Status: Confirmed, 2026-10-03.

### RT-OQ-09. Spellings that SPEC-01 9.8 does not cover

Status: Confirmed, 2026-10-03.

### RT-OQ-10. Admission of the MSVC intrinsic path

Status: Confirmed, 2026-10-03.

### RT-OQ-11. Header includes beyond the EMIT-01 list

Status: Open.

### RT-OQ-12. Library contents the plan lists for `rt/cint_rt.c` but Task 1.4 does not specify

Status: Partly closed, 2026-10-03.

### RT-OQ-13. Warning flags

Status: Recorded.

### RT-OQ-14. `cint_view` and `cint_type` are not in `cint_rt.h`

Status: Closed, 2026-10-03.

### RT-OQ-15. What the four compiler calls return at T1

Status: Confirmed, 2026-10-03.

### RT-OQ-16. How the bridge knows its inputs

Status: Closed, 2026-10-03.

### RT-OQ-17. Path forms `cint_commit_output` refuses beyond CINTC-15

Status: Confirmed, 2026-10-03.

### RT-OQ-18. Staging file name, permissions, and a busy target

Status: Confirmed, 2026-10-03.

### RT-OQ-19. Durability and the window between check and replace

Status: Confirmed, 2026-10-03.

### RT-OQ-20. Fault writers given arguments no emitted code passes

Status: Confirmed, 2026-10-03.

### RT-OQ-21. The ABI version and the contract name of `cint-rt-2`

Status: Open.

### RT-OQ-22. The records of `E_SHAPE` and `E_ALIAS` at a wrapper's entry

Status: Proposed.

### RT-OQ-23. Print output: staging, the callback, and boundary faults

Status: Proposed.

### RT-OQ-24. The minimal registry and `cint_view_bind`: choices D-3 left open

Status: Proposed.

### RT-OQ-25. `cint_program_run`: destination, exit statuses, and Windows paths

Status: Proposed.

### RT-OQ-26. The D-25 recovery source differs from the patch text in one name

Status: Recorded.

### RT-OQ-27. The `cm` module descriptor and the record layout table

Status: Proposed.

### RT-OQ-28. The source-map digest has no carrier in `cint-rt-2`

Status: Open.

### H-OQ-01. The observer export contract

Status: Closed, 2026-10-03.

### H-OQ-02. The `clause` line of a harness outcome

Status: Confirmed, 2026-10-03.

### H-OQ-03. Fuel `-1` on the command line

Status: Confirmed, 2026-10-03.

### H-OQ-04. Depth limits above the runtime's frames

Status: Confirmed, 2026-10-03.

### H-OQ-05. Calls per process and the case list

Status: Deferred.

### H-OQ-06. Text the `.expect` grammar cannot carry

Status: Confirmed, 2026-10-03.

### H-OQ-07. A note for generated code

Status: Recorded.

### H-OQ-08. `state.global` lines from a compiled program

Status: Proposed.

### RT-OQ-29. The Python bridge: choices, and the size ceiling

Status: Closed, 2026-10-03.

### RT-OQ-30. The bridge does not compile on macOS: `O_NOFOLLOW` is hidden by `_POSIX_C_SOURCE`

Status: Closed, 2026-10-03.

### RT-OQ-31. Apple Clang emits `__chkstk_darwin`, which the EMIT-31 list does not name

Status: Closed, 2026-10-03.

### RT-OQ-32. Apple Clang at `-O0` calls `bzero`, which the EMIT-31 list does not name

Status: Closed, 2026-10-05.

### RT-OQ-33. Error results in `cint-rt-2`: the error set descriptor, the error record, and the state destination

Status: Decided, 2026-10-05.

### RT-OQ-34. Section 14 is provisional; the scratch capacity in elements

Status: Proposed.

### RT-OQ-35. Fault descriptors are held outside the canonical record

Status: Proposed.

### RT-OQ-36. Choices in section 14 and the harness where the texts are silent

Status: Proposed.

### RT-OQ-37. The runtime of a library build, and the fault record as bytes

Status: Proposed.

### RT-OQ-38. The T3 entry checks of a `cint-abi-1` wrapper

Status: Proposed.

### RT-OQ-39. The registry of SPEC-03 5.3, and ABI 3 with `cint-rt-3`

Status: Proposed.

### RT-OQ-40. A host-issued dispatch: the wrapper of an exported kernel

Status: Proposed.

### RT-OQ-41. Arenas, pools and handles: `rt/cint_mem.c`

Status: Proposed.

### RT-OQ-42. Checkpoints, restore, the interrupt and the change of revision

Status: Proposed.

### RT-OQ-43. Device contexts, `device(1)` memory and ABI 3.1

Status: Proposed.

### RT-OQ-44. Recordings and replay

Status: Proposed.

### RT-OQ-45. Cases and the source-map digest

Status: Proposed.

### RT-OQ-46. Arenas and pools in canonical state

Status: Decided, 2026-10-08.

### RT-OQ-47. The host header and the inline call protocol

Status: Proposed.

### RT-OQ-48. Recordings at `dispatch` density

Status: Proposed.

### RT-OQ-49. Module-level arrays in the module state

Status: Decided, 2026-10-08.

### RT-OQ-50. Module-level handles in the module state

Status: Proposed.

### RT-OQ-51. Module-level structs in the module state

Status: Decided, 2026-10-09.

### RT-OQ-52. Shift counts of a wide type

Status: Proposed.

### RT-OQ-53. The fixed-point helpers

Status: Proposed.

### RT-OQ-54. Reductions with a wide side

Status: Proposed.

### RT-OQ-55. Formatted print and failed asserts of a wide type

Status: Proposed.

### RT-OQ-56. Formatted print of fixed point, and `!bits`

Status: Proposed.

### RT-OQ-57. Reductions over fixed point, and `dot` with a rounding mode

Status: Proposed.

### RT-OQ-58. Saturating conversions `as|`

Status: Proposed.

### RT-OQ-59. The fixed-point result forms

Status: Proposed.

### RT-OQ-60. gcc refuses a clone of `cint_builtin_<t>` with a NULL operand

Status: Closed, 2026-10-15.

### RT-OQ-61. Module-level arrays of 8-bit or `Bool` elements stop at 65,534 elements

Status: Closed, 2026-10-19.

### RT-OQ-62. Two- and four-limb paths of the wide helpers

Status: Proposed.

### RT-OQ-63. Fixed point with a side held in limbs

Status: Proposed.

### RT-OQ-64. The width report

Status: Proposed.

### RT-OQ-65. The header of a library build, C++ hosts and `cint_site_resolve`

Status: Proposed.

### RT-OQ-66. Wide products of small operands

Status: Proposed.

### RT-OQ-67. Rotations on the inline path of the built-ins

Status: Proposed.

### RT-OQ-68. Module state on the inline path

Status: Proposed.

### RT-OQ-69. Wide quotients of small operands

Status: Proposed.

### RT-OQ-70. Extern calls: the host import table, the host call and its events

Status: Proposed.

### RT-OQ-71. The standard library root in the bridge: `cint_bridge_input_absent` and `cint_bridge_root_chain`

Status: Proposed.

### RT-OQ-72. The `fs/1` service of the standard service table: grant lines, service rows and `rt/cint_fs.c`

Status: Proposed.

### RT-OQ-73. The `console/1` service of the standard service table: `rt/cint_console.c`

Status: Proposed.

### RT-OQ-74. The entry of a generated `main`: `cint_program_run_args` and `rt/cint_program.c`

Status: Proposed.

### RT-OQ-75. `cpu-sir-interp` runs `Grants` parameters and service calls

Status: Proposed.

### RT-OQ-76. The program arguments of `main`: `config.args`, `cint_program_run_argv` and the entry's input record

Status: Proposed.

### RT-OQ-77. stdout flushed before a `console/1` wait and after a `console/1` write

Status: Decided, 2026-10-22.
