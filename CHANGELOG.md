# Changelog

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), with one section added: **Compiler coverage**, the conformance counts for the release and the cases that moved from unsupported to compared. Versions follow [Semantic Versioning](https://semver.org/); before 1.0, a minor version may break anything.

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
