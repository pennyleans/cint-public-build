# CINT conformance suite

Status: Proposed, 2026-10-02. Slice 1 (stages T0 and T1). Nothing here is released. Updated 2026-10-03 for decision 23 (the slice 2 decision patch): result categories, the numbered diagnostic codes, `.expect` format 2, and the normative case header (sections "Result categories" and "Diagnostic codes" below).

This directory holds the test cases that every CINT implementation must agree on: the Python reference `cint_ref`, the seed compiler, and later `cintc` and its backends. The rules come from SPEC-01 section 13 and SPEC-09 section 9 in `docs/cint/`. Where this file and the specification differ, the specification governs, and the difference is a defect in this file.

The anchors were derived by hand and checked against the generators' own small evaluator (`tools/imeval.py`), which is not an independent oracle: both were written by the same agent from the same text. At the T0 run (2026-10-02) every anchor, every generated record and every table program agreed with `cint_ref`, which was written separately from the same text; the receipt is `results/cint/t0/receipt.json`.

## Layout

| Path | Contents | Expected outcomes from |
| --- | --- | --- |
| `integer-machine/anchors.cif1.jsonl` | Operation-level cases derived by hand, each with a `derivation` a reviewer can check | anchor |
| `integer-machine/exh8/*.cif1.jsonl` | Every operand pair for the 11 non-saturating binary operator forms on `I8` and `U8` (22 files, 1,441,792 records) | generated |
| `integer-machine/bnd64/*.cif1.jsonl` | The `I64` boundary matrix for all 14 binary operator forms (14 files, 256 records each) | generated |
| `integer-machine/MANIFEST.txt` | SHA-256 of every generated file and the commands that regenerate it | |
| `tables/<name>.ci`, `tables/<name>.cases` | Table programs and case lists that carry one CIF-1 file through a compiled implementation | generated |
| `arith/`, `control/`, `switch/`, `struct/`, `diag/`, `determinism/` | Program cases (`.ci` and `.expect`); `tools/gen_expect.py` writes each `.expect` with `cint_ref` | reference or compile-error |
| `view/` | Program cases on rank-1 arrays, size parameters, views and indexing, each observed through a scalar entry that builds its own arrays (slice 2 task 2.8); box 09 adds rank 2 to 4, slices, `copy` and `transpose`, held until `cint_ref` implements them | reference or compile-error |
| `reduce/` | Box 09 reductions: hand-written loop folds compared now, and the SPEC-01 IM-77 built-ins (`sum`, `fold_checked`, `sum_wrap`, `sum_sat`, `min`, `max`, `count`, Proposed `dot`) held with their outcomes in the case header | reference, or held |
| `kernel/` | Box 09 kernels on the C reference: SPEC-02 dispatch, publication, aliasing, in-kernel reductions and canonical fault addresses, held until `cint_ref` runs kernels and CONF-11 has a kernel address form (OQ-156) | held |
| `errors/` | Box 12 error sets, combined sets, error unions, `try`, `catch`, `as?` and the error values of the built-ins, with their compile errors, held until `cint_ref` implements them; a case whose entry returns an error is to be frozen in `.expect` format 3 (SPEC-09 CONF-11, BX12-21) | held |
| `cleanup/` | Box 12 `defer` and `errdefer`: order, block and loop exits, fuel, faults in and before a deferred statement, and the compile errors C4030 and C4031, held until `cint_ref` implements them | held |
| `ternary/` | t27 set 1: balanced ternary on binary words in today's language (bit-plane adders, a 27-trit adder over 64 lanes, PT5 and PT4 over all 256 byte values, bit-plane `tdot`, 16-bit lane bounds, `rescale3` as a plane shift), each also checked by the independent reference `tools/t27_ref.py`; required list `required/t27-1.txt` | reference |
| `module/` | Program cases of several modules; the imported modules are in `module/lib/`, read from the source root `conformance/` | reference or compile-error |
| `boot/` | The cases of the SPEC-09 BOOT-01 table: admitted productions, and `cint-core-1` programs that the seed must refuse, whose `.expect` holds the seed's outcome (see "Seed outcomes of `boot/` cases") | reference, or compile-error from the case header |
| `held.txt` | Program cases held out of the frozen set, each with the `ref/OPEN.md` entries it cites; a held case has no `.expect` file, and a held `<case>.cintc` has no `.cintc.expect` file | |
| `tools/fuzz_decoders.py` | Deterministic fuzz targets for the IM-149 fault-record reader and the `.expect` reader (slice 2 patch D-10); prints the seed, the count, and a digest of the inputs | |
| `errata/` | Erratum records (empty at T1) | |
| `tools/` | Generators and their tests; `oracle3.py` (a third derivation of the generated tables) and `t0_receipt.py` (writes `results/cint/t0/receipt.json`) | |

## Formats

### CIF-1 (operation level)

CIF-1 is defined in SPEC-01 13.1. Each file is JSON Lines [1]: one JSON object per line, UTF-8 [2], LF line endings, keys in the order `id`, `status`, `op`, `args`, `expect`. All integers are JSON strings of decimal digits so no JSON parser loses precision. `op` is an operation identifier from SPEC-01 9.8, for example `add.checked.i64` or `div_round.checked.i64.half_even`. `status` is `S` (Specified) or `P` (depends on a Proposed rule or an Open question; SPEC-01 1.1).

```json
{"id":"add.checked.i8.33023","status":"S","op":"add.checked.i8","args":[{"t":"I8","v":"0"},{"t":"I8","v":"127"}],"expect":{"value":{"t":"I8","v":"127"}}}
```

`expect` holds exactly one of `value` (a typed value) or `fault`. A `fault` object has `code` and, where the SPEC-01 9.2 record has them, `exact`, `limit` and `index`. `exact` is absent for `E_DIV_ZERO` and `E_SHIFT` (SPEC-01 9.2). For an `E_SHIFT` with a negative count, the record asserts only the code, because the text leaves `limit` undecided (`ref/OPEN.md`, O-2).

Anchor records add a sixth key, `derivation`, after `expect`. SPEC-01 13.1 does not list it yet (`ref/OPEN.md`, O-1).

Generated files list operand pairs in sorted order: left operand ascending, then right operand ascending. Identifiers are the operation identifier plus a zero-padded index (`add.checked.i8.00000` to `add.checked.i8.65535`). In the exhaustive 8-bit files, a shift count has the same type as the left operand (`ref/OPEN.md`, O-4).

### Program cases (`.ci` and `.expect`)

A program case is a `.ci` source file and an `.expect` file in canonical text, defined in SPEC-09 9.2 (CONF-01): ASCII, LF, one `<key> <value>` field per line. The fault fields are the SPEC-01 9.2 record. Every `.ci` here begins with a header comment:

```text
// case: arith/seed08_runtime_overflow
// clause: SPEC-09 SEED-08 row 4; SPEC-01 4.1, 9.2
// subset: cint-boot-1
// entry: run; args: none
// spec outcome: fault E_OVERFLOW at 9:15, operation add.checked.i64, ...
```

- `clause` names the owning sections. Clause identifiers (`IM-<n>` for SPEC-01, `LS-<n>` for SPEC-04) are assigned by plan Task 1.0; until then cases cite section numbers.
- `subset` says whether the case is inside `cint-boot-1` (SPEC-09 5.2 and 5.3), so the seed compiler can run it, or needs the full `cint-core-1` surface.
- `entry` and `args` name the exported function and its typed arguments; `fuel` and `depth` give the budgets where a case needs them. These header keys (`case`, `clause`, `subset`, `entry`, `args`, `fuel`, `depth`) are normative (decision 2026-10-03 on OPEN-QUESTIONS.md OQ-133); a case-list form for array arguments waits for T3. A case with no named entry runs the script, else `main`, else its only test (`ref/OPEN.md` REF-OQ-12, confirmed).
- `spec outcome` is the case author's reading of the specification, written for the reviewer. The `.expect` file, not this comment, is the frozen expectation.

The CONF-01 example, `arith/add_i64_overflow.ci`, keeps the listing of SPEC-09 9.2 unchanged. Its header is folded into the first comment line so that the fault stays at line 5, column 15.

### Table programs and case lists

SPEC-09 9.2 runs every CIF-1 file through generated `.ci` table programs, and SPEC-09 9.8 (CONF-10) observes compiled programs through `cint-harness`, which reads a case list of `<module> <function> <typed arguments>` lines. A fault ends an entry (SPEC-01 9.4), so each record is one call. `tools/gen_tables.py` writes, for one CIF-1 file, a program with one exported function that applies the operator once, and a case list whose line n is record n of the CIF-1 file. The constant-expression twin that CONF-08 asks for is not generated yet (`ref/OPEN.md`, O-5).

## Where expected outcomes come from

SPEC-09 CONF-02 allows three sources, and every case states which.

| Source | Meaning | Used for |
| --- | --- | --- |
| `anchor` | Derived by hand from the specification text and reviewed. The derivation is written in the case. | Boundary cases, and the check on `cint_ref` itself |
| `reference` | Produced by `cint_ref`, reviewed line by line against the owning clause, then frozen | Program cases with a value or fault outcome |
| `compile-error` | The expected diagnostic code and position | `diag/` and the compile-error rows of the other categories |

The generated CIF-1 tables come from `tools/imeval.py`, a small exact-integer evaluator written from SPEC-01 sections 1.2, 2.1, 4.1, 4.2, 4.4 and 4.6. It deliberately duplicates part of `cint_ref` (plan Task 1.3). The two are compared at integration; a disagreement is resolved from the specification text, never from either program and never from the legacy compiler in `cint/` or `src/`.

A disagreement between `cint_ref` and an anchor is a reference bug or an Open question against the specification. It is never a reason to edit the anchor (plan Task 1.3 Step 2; SPEC-09 CONF-04).

## Errata

Frozen expectations are never edited in place (SPEC-09 CONF-02). There are three ways to change one.

1. A semantic change ships under a new profile name, with its own frozen expectations. An encoding change that carries the same content ships under a new encoding version: a `.expect` file with the line `format 2` after `clause` is format 2, and a file without it stays a valid format 1 file, compared in format 1 (SPEC-09 CONF-02, CONF-11; decision 2026-10-03, D-20).
2. A `reference` expectation that is wrong because of a `cint_ref` bug, with the specification text unchanged, is superseded by a recorded erratum, `errata/<id>.txt`. The erratum names the case, the old and the new expectation, the clause text that decides it, and the reviewer. It is part of the suite digest. An `anchor` expectation is corrected only by the same process with a second reviewer.
3. A change to the `clause` line alone, which is free text (SPEC-09 CONF-11 rule 8) that no comparison reads, regenerates the file with `tools/gen_expect.py` from the case header, in one dedicated change in which every other line of each regenerated file stays identical and after which `gen_expect.py --check` reports 0 differ (SPEC-09 CONF-02; decision 2026-10-05 on OQ-200).

## Bytes and determinism

Every file the generators write is UTF-8 with LF line endings and no timestamps or absolute paths (SPEC-09 7.6). The generators open files in binary mode, so a Windows text stream cannot add a CR (plan Review Focus 5). Running a generator twice gives byte-identical output; `tools/test_generators.py` checks this. `integer-machine/MANIFEST.txt` records the SHA-256 [3] of every generated file, so a regeneration on another machine can be compared byte for byte.

To regenerate, from the repository root:

```text
python conformance/tools/gen_exhaustive8.py --batch t1 --out conformance/integer-machine/exh8
python conformance/tools/gen_boundary64.py --batch all --out conformance/integer-machine/bnd64
python conformance/tools/gen_tables.py conformance/integer-machine/exh8/*.cif1.jsonl conformance/integer-machine/bnd64/*.cif1.jsonl --out conformance/tables
python -m unittest discover -s conformance/tools -p "test_*.py" -v
```

The generators use the Python standard library only and no floating point.

## Result categories (decision 2026-10-03)

A conformance run reports each case in exactly one category of SPEC-09 CONF-14: compared (an expected compile error included), unsupported by this implementation, held, outside the subset, or not applicable. Any other result is a disagreement. A case is unsupported only when `unsupported.txt` lists it, one line per entry, `<case> <compiler> <code> <limit-id> <reason>`; the first entries name the seed's nesting limit, for example `control/nest_blocks_200 cint-seed C9004 SEED-15 the seed compiles at most 199 nested blocks`. Each roadmap stage has a required case list, `required/<stage>.txt`, and `tools/cint_check.py --required` fails a run whose categories differ from it (SPEC-09 CONF-15). Slice 2 tasks 2.1 and 2.2 write these files. Task 2.1 (2026-10-03) released the 22 held cases that decision 23 decides (D-11 and D-12), the six `E_SHIFT` program cases of plan Review Focus 3 among them, each with `limit I64 63` (SPEC-01 IM-45), and froze the nesting and long-branch cases of D-5, the constant-expression cases of D-9, the literal cases of D-21, and the script-mode cases of D-22; `held.txt` keeps six cases, and `unsupported.txt` lists the seed's SEED-15 refusals. Task 2.7 (2026-10-03) froze the first format 2 cases, `control/depth_limit_stack` (three `fault.stack` lines) and `arith/stores_before_fault` (released, with its `state.global` line), the four literal cases of D-17 (three listed in `unsupported.txt` under the seed's SEED-16), and the two print-then-fault cases of D-19 in format 1; `held.txt` keeps five cases. `tools/gen_expect.py` regenerates a frozen file in its own format; `--format 2` applies only to a case that has no file yet.

## Seed outcomes of `boot/` cases (decision 2026-10-03)

A `boot/` case whose header names a subset other than `cint-boot-1` is a valid `cint-core-1` program that uses one production outside `cint-boot-1`, which the seed must refuse with a positioned diagnostic (SPEC-09 BOOT-01, SEED-06). Its `.expect` holds the seed's outcome and is compared on code and position (SPEC-09 CONF-14 category 4; OQ-154, decided by D-8). The outcome is read from the specification, not from the seed: the header line `// seed outcome: C9100 at 9:13` names the code and the first character of the first construct outside the subset, and `tools/gen_expect.py` writes the `.expect` from it with `source compile-error`. `cint_ref` still runs the case, which must not be a compile error under `cint-core-1`; it may refuse a construct it does not implement. Slice 2 task 2.8 (2026-10-03) froze the 21 cases that BOOT-01 listed as planned (20 in `boot/` and `module/import_plain`), with 17 cases in `view/` and `module/`, and released `diag/c1042_increment_in_expression` and `struct/array_of_structs`; the seed agrees with every one on MSVC, GCC and Clang, except `view/shape_mismatch_runtime`, which `unsupported.txt` lists under the seed's limit SEED-OQ-05.

The seed's file is not an expectation for `cintc`, which has no outside-the-subset category. Under decision 26 item 2 (SPEC-09 CONF-11, Proposed; `compiler/OPEN.md` CINTC-OQ-48), such a case has a second file, `<case>.cintc.expect`, in the CONF-11 grammar with the same `case` line, frozen from `cint_ref` by `tools/gen_expect.py` and compared for `cintc` only. Where `cint_ref` refuses the construct, no file can be frozen (CONF-11 rule 7), and `held.txt` lists `<case>.cintc`, so the case is held for `cintc`. Slice 2 task 2.13 (2026-10-03) froze five files (`boot/refuse_print`, `refuse_profile_line`, `refuse_static_assert`, `refuse_ternary_literal`, `refuse_test_block`) and held the other 11 under `ref/OPEN.md` I-2. `gen_expect.py --check` checks both files of each case. `cint-harness` now counts and hashes a call's print output, so `boot/refuse_print` is observed with its 7 bytes.

## Diagnostic codes (decision 2026-10-03)

The compile-diagnostic codes are the numbered table of SPEC-04 17.2 (LS-313). The renumbering renamed no code that a frozen `.expect` file cites. Three `.ci` header comments still name the old codes: C9001 in `control/nest_blocks_200` and `control/nest_whiles_67` (the seed's nesting limit is now reported as C9004), and C9100 in `diag/block_comment_nested`. Comments are not compared, so they stay as written.

## What is not here yet

- Boundary matrices for `U64`, `U32` and `I32` (`ref/OPEN.md`, O-3).
- The 3 saturating 8-bit forms (393,216 records); SPEC-09 9.3 schedules them for T2. `gen_exhaustive8.py --batch all` writes them.
- Constant-expression table programs (O-5), a CR LF source case (O-7), and serialization and decoder fixtures (O-9).
- The category `const`, and view cases beyond rank 1 (slices, strides, lower bounds), which are T3.
- The `.expect` files of the cases listed in `held.txt`. Each is generated with `tools/gen_expect.py` and reviewed once the `ref/OPEN.md` entry it cites is closed.
- A frozen program case with a `fault` outcome. Every fault case was held for `ref/OPEN.md` O-14 and I-5 at the T0 run; plan Task 1.0 released the 12 cases held only for them, and that decision was confirmed on 2026-10-03.

## References

1. JSON Lines, "JSON Lines text format," https://jsonlines.org/ (accessed 2026-10-02). Each line is a valid JSON value [4].
2. F. Yergeau, "UTF-8, a transformation format of ISO 10646," RFC 3629, IETF, 2003. https://www.rfc-editor.org/rfc/rfc3629
3. National Institute of Standards and Technology, "Secure Hash Standard (SHS)," FIPS PUB 180-4, 2015. https://doi.org/10.6028/NIST.FIPS.180-4
4. T. Bray, Ed., "The JavaScript Object Notation (JSON) Data Interchange Format," RFC 8259, IETF, 2017. https://www.rfc-editor.org/rfc/rfc8259
