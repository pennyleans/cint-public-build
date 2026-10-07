# Changelog

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), with one section added: **Compiler coverage**, the conformance counts for the release and the cases that moved from unsupported to compared. Versions follow [Semantic Versioning](https://semver.org/); before 1.0, a minor version may break anything.

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
