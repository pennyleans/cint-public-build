# Changelog

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), with one section added: **Compiler coverage**, the conformance counts for the release and the cases that moved from unsupported to compared. Versions follow [Semantic Versioning](https://semver.org/); before 1.0, a minor version may break anything.

## [0.2.0] - 2026-10-11

### Added

- `cint debug` replays a run, locates a fault and compares two builds (`replay`, `locate` and `diff`). `cint explain`, `cint workbench` and `cint repl` work beside it. `cint NAME` runs `cint-NAME` from the directory of `cint`. `tools/cint_tools.py --leg gcc` (or `clang`, `msvc` or `apple-clang`) builds `cint-debug`, `cint-explain`, `cint-workbench`, `cint-repl` and `cint-interp` beside `cint`. Every fault report names the operation, its exact operands and the source position.
- `cint interp` is the SIR interpreter. It takes checkpoints, records runs and replays them, and the Python package records and replays as well.
- The reference and `cintc` take integers wider than 64 bits, `I128` to `I1024` and `U128` to `U512`, and `mul_full`.
- The reference and the interpreter run fixed point `Q<i>.<f>`.
- Views can be parameters with array literal arguments. Modules can hold arrays of rank 1 to 4, handles and structs, and arenas and pools carry over a checkpoint and a change of revision.
- The checkpoint and recording encodings are version 1 (SPEC-01P.2). Seeded fuzz runs test their decoders, and each run writes a receipt with its seed, count and input digest.
- The release publishes nine specification editions: SPEC-00P.2, SPEC-01P.2 (canonical encodings and identity), SPEC-02P.1, SPEC-03P.1, SPEC-04P.2 (the cint language), SPEC-06P.1, SPEC-09P.1, GLOSSARY-P.2 and REFERENCES-P.2. Each states what the 0.2.0 reference runs. The earlier editions stay unchanged at their addresses.

### Changed

- The license, Apache-2.0 WITH LLVM-exception, covers the code, the specification text and the receipts.
- Tool messages cite public clause identifiers only.

### Removed

- The tutorials under `docs/tutorials/` are gone. The cint university course on integerc.dev replaces them.

### Compiler coverage

`cintc` refuses several forms with C9102, and SPEC-09P.1 lists each refusal: fixed point; arrays at a script's top level; struct array literals; and, on the wide types, `as?`, the result forms, `switch`, `rotl` and `rotr` by a wide count and `muldiv` of narrow operands into a wide type. The reference refuses `as|`.

On `cpu-c17` and `cpu-sir-interp`, `cintc` runs 823 conformance cases and agrees with the reference on all 1,868,066 records; 11 cases are held, 67 are not applicable and 47 are unsupported.

### Known issues

- Under GCC 16.2.1 and Clang 22.1.8 on Fedora 44, 4 of the 32 configurations crashed in the conformance run: GCC `-O2` with portable helpers on `cpu-c17` and on `cpu-sir-interp` (exit status 1), GCC `-O2` with portable helpers and sanitizers, and Clang `-O0` with portable helpers (each a segmentation fault). The cause is not yet known.
- The CUDA backend, `gpu-cuda`, does not pass conformance in this release. It refuses the 47 fixed-point cases, as `cpu-c17` does, but `conformance/unsupported.txt` lists those refusals for `cpu-c17` only, so each `gpu-cuda` run counts them as disagreements.
- The tests that read the conformance tables need the tables asset unpacked at the repository root first.
- Under Clang, `tools/cint_python.py` reports one required test as not passed. `test_calls.Threads.test_busy` skips because Clang computes the loop of the test program's `spin` in constant time, so the call returns before a second call can be made.
- In Windows PowerShell, `>` writes UTF-16, so write an expectation file with `cmd /c "python -m cint_ref run FILE > FILE.expect"`.
- `cint explain` names a module's `.sites` file instead of the source position, and fault reports print `source-map: none`.

## [0.1.1] - 2026-10-06

### Fixed

- `cint` read at most 8 runtime source files from its toolchain file and left the rest out of a receipt's runtime identity without a word: 6 of the 14 runtime files, `rt/cint_rt.c` among them, were not covered. Every runtime source is now recorded, and a toolchain file with more than the command can hold is refused.
- The Python bridge, `python/`, is now included. `examples/matmul_i8.py`, tutorial 04 and `tools/cint_python.py` need it; v0.1.0 shipped them without it.
- The tutorials no longer open with a broken image.
- Two tests that need a file outside this repository now skip and say why.

### Removed

- Tests and tools that check the author's own build records against receipts, pins and build files that are not published: `tools/cint_accept.py`, `tools/cint_stage.py`, and `tests/test_cint_check_accept.py`, `test_cint_check_exclusions.py`, `test_cint_check_t2_import.py`, `test_cint_compare.py`, `test_cint_measure.py`, `test_cint_manifest.py` and `test_cint_stage.py`. They could not pass in this repository.

### Known issues

- The tests that read the conformance tables need the tables asset unpacked at the repository root first.
- The tutorials cite the receipts of the measuring runs under `results/`. Those receipts are not published; the text quotes what each one records.

## [0.1.0] - 2026-10-06

The first public release.

### Added

- `cintc`, the compiler, written in cint and emitting C17, with the seed compiler that builds it, the runtime, and the `cint` command (`run`, `build`, `test`, `emit-c`).
- `cint_ref`, the reference interpreter.
- The conformance suite: 861 cases with their expected outcomes, and the generators of the integer-machine tables.
- Public specification editions SPEC-00P.1, SPEC-01P.1 and SPEC-04P.1, with GLOSSARY-P.1 and REFERENCES-P.1.
- Tutorials and examples.

### Known issues

- The tutorials cite the receipts of the measuring runs under `results/`. Those receipts are not in this release; the text quotes what each one records.

### Compiler coverage

| Cases | Compared with `cint_ref` | Held | Not applicable |
| ---: | ---: | ---: | ---: |
| 861 | 783 | 11 | 67 |

Compiler source identity: `f30a4745d35ab40208f923d214b27b00ea24773785fec56e93fc6ffaaa78b3ed`.
