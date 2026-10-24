# Changelog

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), with one section added: **Compiler coverage**, the conformance counts for the release and the cases that moved from unsupported to compared. Versions follow [Semantic Versioning](https://semver.org/); before 1.0, a minor version may break anything.

## [0.2.4] - 2026-10-24

### Added

- `cint run` keeps the executable it built, and a later run of the same program starts it without building it. `--rebuild` builds anyway. A run with `--receipt` or `--width-report` always builds (SPEC-06P.3 5.4).
- A run takes its grants from the program when no `--grant` line is given. Those grants are a capability with no path root and a file capability on `/argK`, which backs the directory of program argument K. An executable that `cint build` writes does the same (SPEC-06P.3 5.4, SPEC-04P.4 6.9.9).
- The language has byte-string literals, `b"abc"`, and the built-in function `equal(a, b)` (SPEC-04P.4 6.4.8, 7.4).
- A `catch` statement handles a call whose result is `E!void`, as in `f() catch (e) { ... };`, and its block may complete (SPEC-04P.4 6.11.4).
- `cint build --app`: the executable takes every word after its name as a program argument, so it needs no launcher (SPEC-06P.3 5.5).
- `--frame-arena N` sets the capacity of the frame arena for `cint run` and `cint build`.
- A run receipt names the `--grant` lines and says whether standard input was the null device. `cint explain --stdlib DIR` names the standard library root.
- The release publishes five specification editions: SPEC-03P.3, SPEC-04P.4, SPEC-06P.3, SPEC-09P.3 and GLOSSARY-P.4. SPEC-01P.3 and SPEC-02P.2 stay in force.

### Fixed

- `cintc` accepts an element of a module-level array of rank 2 to 4 that a function reads only inside a hole of a print string.
- `cint` finds `cint.toolchain` and `lib/` beside the real executable when it is started through a symbolic link.
- On the Windows console in raw mode, a window resize no longer holds up a timed `read`, and Ctrl+Z, Ctrl+Y and Ctrl+C reach the program as 0x1A, 0x19 and 0x03. The arrows, Home, End, Page Up, Page Down, Insert, Delete and F3 arrive as xterm sequences, and input and output are UTF-8.

### Compiler coverage

`cintc` refuses the forms that SPEC-09P.3 clause 12 lists with C9102; the `catch` statement and the `catch` expression on a call of a function of an imported module are among them. The suite holds 935 programs and 1,868,178 records. The agreement of `cintc` and the interpreter with the reference was not measured again for 0.2.4; the counts of 0.2.3 hold for the 0.2.2 sources and are not claimed here.

### Known issues

- The workbench, `cint repl` and `cint debug` refuse a module that declares an extern or a `requires` (C4012).
- On the Windows console, F1, F2 and F4 to F12 reach a program as nothing. Outside raw mode the console's own output code page applies, and in line mode a Ctrl+Z at the start of a read ends the input.
- A receipt records the grants given with `--grant`. It does not record the grants a run takes from its program or the program arguments.
- `cint test` takes `--grant` lines, but a test holds no `Grants` value, so they grant no call.
- `fs` does not list, create, remove or rename; a grant set cannot be narrowed.

## [0.2.3] - 2026-10-23

0.2.3 takes in 0.2.1 and 0.2.2, which were not published on their own; 0.2.3 is 0.2.2 with one fix.

### Added

- The standard library has its first services, `fs` and `console`. `import fs;` reads a file, writes a file in one step and reports its size; `import console;` writes to the terminal, reads lines or bytes, reports the window size and switches raw mode. The option `--stdlib DIR` names the standard library root, by default the directory `lib` beside `cint`.
- A service call works only under a grant given with `cint run --grant LINE`, such as `--grant fs.read:/data=data`; a call that no grant covers returns `denied`. `main` receives the grant set as a parameter of type `Grants`.
- `main` can take the program arguments, the words after `--` in `cint run FILE.ci -- ARG...`, as `in U8[_] args`, each word's bytes followed by a NUL byte. A built executable takes them the same way, and a recording holds them.
- `extern` declares a function that the host supplies, with its effect class. A host passes its functions when it creates a context; the C interface is version 3.4 (SPEC-03P.2 7.2, 7.6). A recording keeps the results of `observe` calls, and replay can give them back without calling the host.
- `cint build --lib` writes a header that declares the library's exports, which compiles as C17, C++17 and C++20, and builds the same library bytes every time. `cint_site_resolve` gives the source position of a fault's site.
- `cintc` compiles fixed point, and fixed-point storage may be 128 bits in the reference, `cintc` and the interpreter.
- The reference, `cintc` and the interpreter implement the saturating conversion `as|` and the result forms of fixed-point operations.
- A module-level array may hold up to 65,536 elements of any type.
- `cint run --width-report PATH` and `cint build --width-report` report the bits each wide operation used.
- The release publishes seven specification editions: SPEC-01P.3, SPEC-02P.2, SPEC-03P.2, SPEC-04P.3, SPEC-06P.2, SPEC-09P.2 and GLOSSARY-P.3. SPEC-00P.2 and REFERENCES-P.2 stay in force.

### Fixed

- The C that `cintc` writes for a function that uses a field or an element of a struct as a place no longer reads through a null pointer when the function starts. Clang's UndefinedBehaviorSanitizer reported the read; GCC and Clang without the sanitizer did not.

### Changed

- `cint` itself is built with the release compiler flags.
- The runtime's wide arithmetic takes shorter paths for small operands, and on x86-64 under GCC and Clang divides with the hardware instruction that takes a 128-bit dividend.
- A fault with no destination prints one line on standard error.
- The workbench times builds, replays and comparisons on a monotonic clock.

### Compiler coverage

`cintc` refuses the forms that SPEC-09P.2 clause 12 lists with C9102, among them several forms on the wide types and an export or extern with a wide or fixed-point parameter or result. `gpu-cuda` refuses fixed point in a kernel and `as|` in a kernel. The records of the 47 fixed-point cases that 0.2.0 left unsupported are now compared with the reference.

`cpu-c17` compiles 732 programs, compares 1,867,975 of their records with the reference with no disagreement and refuses the other 191, with GCC 16.2.1, Clang 23.1.3, MSVC 19.44 and Apple Clang 21; `cpu-sir-interp` compares 1,868,166 records with no disagreement. The counts were measured on the 0.2.2 sources and hold for 0.2.3.

### Known issues

- The workbench, `cint repl` and `cint debug` refuse a module that declares an extern or a `requires` (C4012).
- A receipt records neither the grants of a run nor its program arguments.
- `cint test` takes `--grant` lines, but a test holds no `Grants` value, so they grant no call.
- `cintc` refuses with C9102 an element of a module-level array of rank 2 to 4 that a function reads only inside a hole of a print string.
- `fs` does not list, create, remove or rename; a grant set cannot be narrowed.

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
