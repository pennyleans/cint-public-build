# cint conformance suite

This directory holds the cases that every cint implementation must agree on: `cint_ref`, the reference interpreter in Python, and `cintc` on each of its backends. The rules are SPEC-01 section 13 and SPEC-09 section 9. Where this file and the specification differ, the specification governs, and the difference is a defect in this file.

## Layout

| Path | Contents | Expected outcomes from |
| --- | --- | --- |
| `integer-machine/anchors.cif1.jsonl` | Operation cases derived by hand, each with a `derivation` a reader can check | anchor |
| `integer-machine/exh8/*.cif1.jsonl` | Every operand pair of the binary operator forms on `I8` and `U8` (28 files, 1,835,008 records) | generated |
| `integer-machine/bnd64/*.cif1.jsonl` | The `I64` boundary matrix of the 14 binary operator forms (14 files of 256 records) | generated |
| `integer-machine/bnd128/`, `bnd256/`, `bnd512/`, `bnd1024/` | The same matrices for the wide types: signed and unsigned at 128, 256 and 512 bits (28 files each), and signed at 1024 bits (14 files) | generated |
| `integer-machine/MANIFEST.txt` | The SHA-256 of the 8-bit and `I64` files and the commands that regenerate every generated file | |
| `tables/<name>.ci`, `tables/<name>.cases` | Table programs and case lists that carry one CIF-1 file through a compiled implementation | generated |
| `arith/`, `control/`, `switch/`, `struct/`, `diag/`, `determinism/` | Program cases: arithmetic, control flow, `switch`, structs, compile diagnostics, and output that must not depend on the host | reference or compile-error |
| `view/` | Arrays of rank 1 to 4, size parameters, views, slices, indexing, `copy` and `transpose` | reference or compile-error |
| `reduce/` | Loop folds and the reduction built-ins of SPEC-01 IM-77 (`sum`, `fold_checked`, `sum_wrap`, `sum_sat`, `min`, `max`, `count`, `dot`) | reference |
| `kernel/` | Kernels: dispatch, publication, aliasing, reductions inside a kernel, and fault addresses (SPEC-02) | reference |
| `wide/` | Integers wider than 64 bits, `I128` to `I1024` and `U128` to `U1024` | reference or compile-error |
| `fixed/` | Fixed-point types `Q<i>.<f>` | reference or compile-error |
| `errors/`, `cleanup/` | Error sets, error unions, `try`, `catch` and `as?`; `defer` and `errdefer` | reference or compile-error |
| `memory/` | Arenas, pools and handles (SPEC-03) | reference or compile-error |
| `module/` | Programs of several modules; the imported modules are in `module/lib/`, read from the source root `conformance/` | reference or compile-error |
| `boot/` | The productions that the seed compiler accepts, and programs outside its subset that it must refuse (see "Seed outcomes") | reference, or compile-error from the case header |
| `ternary/` | Balanced ternary arithmetic on binary words, written in the language as it is, each also checked by `tools/t27_ref.py` | reference |
| `held.txt` | Cases held out of the frozen set, each with the open item it waits on. A held case has no `.expect` file | |
| `unsupported.txt` | Cases that an implementation refuses under a documented limit of its own | |
| `required/<stage>.txt` | The category every case must have at each stage | |
| `errata/` | Erratum records | |
| `tools/` | The generators, their tests, `oracle3.py` (a third derivation of the generated tables), the expectation writer `gen_expect.py`, and `fuzz_decoders.py` (deterministic fuzzing of the fault record reader and the `.expect` reader) | |

## Formats

### CIF-1 (operation cases)

CIF-1 is defined in SPEC-01 13.1. Each file is JSON Lines [1]: one JSON object per line, UTF-8 [2], LF line endings, keys in the order `id`, `status`, `op`, `args`, `expect`. Every integer is a JSON string of decimal digits, so no JSON parser loses precision. `op` is an operation identifier from SPEC-01 9.8, such as `add.checked.i64` or `div_round.checked.i64.half_even`. `status` is `S` (specified) or `P` (depends on a provisional rule; SPEC-01 1.1).

```json
{"id":"add.checked.i8.33023","status":"S","op":"add.checked.i8","args":[{"t":"I8","v":"0"},{"t":"I8","v":"127"}],"expect":{"value":{"t":"I8","v":"127"}}}
```

`expect` holds exactly one of `value` (a typed value) or `fault`. A `fault` object has `code` and, where the fault record of SPEC-01 9.2 has them, `exact`, `limit` and `index`. `exact` is absent for `E_DIV_ZERO` and `E_SHIFT`. For an `E_SHIFT` with a negative count, the record asserts only the code (`ref/OPEN.md` O-2).

Anchor records add a sixth key, `derivation`, after `expect` (`ref/OPEN.md` O-1).

Generated files list operand pairs in order: left operand ascending, then right operand ascending. An identifier is the operation identifier and a zero-padded index (`add.checked.i8.00000` to `add.checked.i8.65535`). In the 8-bit files, a shift count has the type of the left operand (`ref/OPEN.md` O-4).

### Program cases (`.ci` and `.expect`)

A program case is a `.ci` source file and an `.expect` file in the canonical text of SPEC-09 9.2 (CONF-01): ASCII, LF, one `<key> <value>` field per line. The fault fields are the record of SPEC-01 9.2. Every `.ci` begins with a header comment:

```text
// case: arith/seed08_runtime_overflow
// clause: SPEC-09 SEED-08 row 4; SPEC-01 4.1, 9.2
// subset: cint-boot-1
// entry: run; args: none
// spec outcome: fault E_OVERFLOW at 9:15, operation add.checked.i64, ...
```

- `clause` names the clauses the case tests.
- `subset` says whether the case is inside `cint-boot-1` (SPEC-09 5.2 and 5.3), which the seed compiler accepts, or needs the full `cint-core-1` language.
- `entry` and `args` name the exported function and its typed arguments; `fuel` and `depth` give the budgets where a case needs them. These keys (`case`, `clause`, `subset`, `entry`, `args`, `fuel`, `depth`) are normative. A case with no named entry runs the script, else `main`, else its only test.
- `spec outcome` is the author's reading of the specification, written for a reader. The `.expect` file, not this comment, is the frozen expectation.

`arith/add_i64_overflow.ci` keeps the listing of SPEC-09 9.2 unchanged. Its header is folded into the first comment line so that the fault stays at line 5, column 15.

### Table programs and case lists

SPEC-09 9.2 runs every CIF-1 file through generated table programs, and SPEC-09 9.8 (CONF-10) observes compiled programs through `cint-harness`, which reads a case list of `<module> <function> <typed arguments>` lines. A fault ends an entry (SPEC-01 9.4), so each record is one call. For one CIF-1 file, `tools/gen_tables.py` writes a program with one exported function that applies the operator once, and a case list whose line n is record n of the file.

With `--kernel`, it writes the kernel table of an `I64` boundary matrix instead, `tables/kernel_<name>.ci` (SPEC-09 CONF-17): a kernel applies the operator with one work-item per record, `row(k)` dispatches every record that does not fault and returns the k-th result, and `fault_row(k)` dispatches the k-th faulting record alone. Line n of its case list is still record n. Its expectations come from `cint_ref`, and `ref/tests/test_kernel_tables.py` checks them against the matrix. A `gpu-cuda` run compares these tables on the device; the eight whose operator the `cuda` capability table refuses are listed in `unsupported.txt` with that capability.

## Where expected outcomes come from

SPEC-09 CONF-02 allows three sources, and every case states which.

| Source | Meaning | Used for |
| --- | --- | --- |
| `anchor` | Derived by hand from the specification text, with the derivation written in the case | Boundary cases, and the check on `cint_ref` itself |
| `reference` | Produced by `cint_ref`, reviewed against the clause the case tests, then frozen | Program cases with a value or fault outcome |
| `compile-error` | The expected diagnostic code and position | `diag/` and the compile-error cases of the other directories |

The generated CIF-1 files come from `tools/imeval.py`, a small exact-integer evaluator written from SPEC-01 sections 1.2, 2.1, 4.1, 4.2, 4.4 and 4.6. It duplicates part of `cint_ref` on purpose, and the two must agree. Both come from the same text, so neither is an independent oracle for the other; `tools/oracle3.py` derives the tables a third way. A disagreement is settled from the specification text, never from either program.

A disagreement between `cint_ref` and an anchor is a defect in `cint_ref` or a question about the specification. It is never a reason to edit the anchor (SPEC-09 CONF-04).

## Result categories

A conformance run reports each case in exactly one category of SPEC-09 CONF-14: compared (an expected compile error included), unsupported by this implementation, held, outside the subset, or not applicable. Any other result is a disagreement.

- A case is unsupported only when `unsupported.txt` lists it, one line per entry: `<case> <compiler> <code> <limit-id> <reason>`. For example, `control/nest_blocks_200 cint-seed C9004 SEED-15 the seed compiles at most 199 nested blocks`. A `gpu-cuda` refusal (SPEC-09 BACK-05) has code `E_UNSUPPORTED`, limit `BACK-05`, and a reason that opens with `capability <name>`.
- A case is held when `held.txt` lists it with the open item it waits on. Once that item is settled, the case's `.expect` is generated with `tools/gen_expect.py`, reviewed, and frozen.
- Each stage has a required list, `required/<stage>.txt`, and `tools/cint_check.py --required` fails a run whose categories differ from it (SPEC-09 CONF-15).

## Seed outcomes of `boot/` cases

A `boot/` case whose header names a subset other than `cint-boot-1` is a valid `cint-core-1` program that uses one production outside `cint-boot-1`, which the seed must refuse with a positioned diagnostic (SPEC-09 BOOT-01, SEED-06). Its `.expect` holds the seed's outcome and is compared on code and position. The outcome comes from the specification, not from the seed: the header line `// seed outcome: C9100 at 9:13` names the code and the first character of the first construct outside the subset, and `tools/gen_expect.py` writes the `.expect` from it with `source compile-error`. `cint_ref` still runs the case, which must not be a compile error under `cint-core-1`.

The seed's file is not an expectation for `cintc`, which accepts the whole language. Such a case has a second file, `<case>.cintc.expect`, with the same `case` line, frozen from `cint_ref` and compared for `cintc` only. Where `cint_ref` does not implement the construct, no file can be frozen, and `held.txt` lists `<case>.cintc`. `gen_expect.py --check` checks both files of each case.

## Diagnostic codes

The compile diagnostic codes are the table of SPEC-04 17.2 (LS-313). Three `.ci` header comments still name codes from before that table: C9001 in `control/nest_blocks_200` and `control/nest_whiles_67` (the seed now reports its nesting limit as C9004), and C9100 in `diag/block_comment_nested`. Comments are not compared, so they stay as written.

## Errata

A frozen expectation is never edited in place (SPEC-09 CONF-02). It changes in one of three ways.

1. A change of meaning comes under a new profile name, with its own frozen expectations. A change of encoding that carries the same content comes under a new encoding version: an `.expect` file with the line `format 2` after `clause` is in format 2, and a file without it stays a valid format 1 file, compared in format 1 (SPEC-09 CONF-02, CONF-11).
2. A `reference` expectation that is wrong because of a defect in `cint_ref`, with the specification text unchanged, is superseded by an erratum, `errata/<id>.txt`. The erratum names the case, the old and the new expectation, the clause text that decides it, and the reviewer, and it is part of the suite digest. An `anchor` expectation is corrected the same way, with a second reviewer.
3. A change to the `clause` line alone, which is free text that no comparison reads (SPEC-09 CONF-11 rule 8), regenerates the file with `tools/gen_expect.py` from the case header. Every other line of each regenerated file stays identical, and `gen_expect.py --check` then reports 0 differ.

## Bytes and determinism

Every file the generators write is UTF-8 with LF line endings, with no timestamps or absolute paths (SPEC-09 7.6). The generators open files in binary mode, so a Windows text stream cannot add a CR. Running a generator twice gives byte-identical output, which `tools/test_generators.py` checks. `integer-machine/MANIFEST.txt` records the SHA-256 [3] of every generated file, so a regeneration on another machine can be compared byte for byte.

To regenerate, from the repository root:

```text
python conformance/tools/gen_exhaustive8.py --batch all --out conformance/integer-machine/exh8
python conformance/tools/gen_boundary64.py --batch all --out conformance/integer-machine/bnd64
python conformance/tools/gen_tables.py conformance/integer-machine/exh8/*.cif1.jsonl conformance/integer-machine/bnd64/*.cif1.jsonl --out conformance/tables
python conformance/tools/gen_tables.py --kernel conformance/integer-machine/bnd64/*.cif1.jsonl --out conformance/tables
python conformance/tools/gen_boundary64.py --type I128 --batch all --out conformance/integer-machine/bnd128
python conformance/tools/gen_tables.py conformance/integer-machine/bnd128/*.cif1.jsonl --out conformance/tables
python -m unittest discover -s conformance/tools -p "test_*.py" -v
```

The wide matrices are written the same way with `--type` `I128`, `U128`, `I256`, `U256`, `I512`, `U512` and `I1024`, into `bnd128` to `bnd1024`. `tools/test_generators.py` compares them with a fresh generation.

To check the frozen expectations and their grammar:

```text
python conformance/tools/gen_expect.py --check
python conformance/tools/check_expect_abnf.py
```

The generators use the Python standard library only, and no floating point.

## Not here yet

- Boundary matrices for `U64`, `U32` and `I32` (`ref/OPEN.md` O-3).
- Constant-expression table programs (O-5), a CR LF source case (O-7), and serialization and decoder fixtures (O-9).

## References

1. JSON Lines, "JSON Lines text format," https://jsonlines.org/ (accessed 2026-10-02). Each line is a valid JSON value [4].
2. F. Yergeau, "UTF-8, a transformation format of ISO 10646," RFC 3629, IETF, 2003. https://www.rfc-editor.org/rfc/rfc3629
3. National Institute of Standards and Technology, "Secure Hash Standard (SHS)," FIPS PUB 180-4, 2015. https://doi.org/10.6028/NIST.FIPS.180-4
4. T. Bray, Ed., "The JavaScript Object Notation (JSON) Data Interchange Format," RFC 8259, IETF, 2017. https://www.rfc-editor.org/rfc/rfc8259
