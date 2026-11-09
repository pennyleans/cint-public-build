# Changelog

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), with one section added: **Compiler coverage**, the conformance counts for the release and the cases that moved from unsupported to compared. Versions follow [Semantic Versioning](https://semver.org/); before 1.0, a minor version may break anything.

## [0.2.18] - 2026-11-09

### Added

- Source and tests for the x86-64 assembler in `toolchain/as/` and the ELF linker in `link/`. These files were omitted from the 0.2.17 source release.

### Changed

- `cint --version` reports 0.2.18. The language and public specification editions remain those of 0.2.17.

### Fixed

- The Python binding test now distinguishes legacy DLPack producers from versioned producers when checking read-only NumPy arrays. The borrow still refuses the export; runtime behavior is unchanged.

## [0.2.17] - 2026-11-08

0.2.17 is the last release of the 0.2 line. It takes in 0.2.9 to 0.2.16, which were not published on their own and are listed below. 0.3.0 follows as an experimental pre-release. Across the nine versions, the language gains four halving and rounding operations, and calls across modules take error unions and grants. `cint` gains `--version` and a cache outside its own directory. The executor `cpu-c17-spec` runs some calls speculatively without changing any outcome, and the experimental executor `cpu-qbe` builds a program through QBE with no C compiler. No expected output that an earlier release published for a conformance case changes.

### Added

- The experimental executor `cpu-qbe` (SPEC-09P.5 5.3, BACK-18). With a toolchain file that names `backend cpu-qbe`, a QBE binary and its SHA-256, a target (`amd64_sysv` or `arm64_apple`), an assembler and a linker, `cint emit-qbe` writes QBE SSA in place of C, and `cint run` and `cint build` build the program through QBE, the assembler and the linker with no C compiler. Fault records, sites and fuel match those of `cpu-c17`. `cpu-qbe` refuses kernels and some other modules with `E_UNSUPPORTED cpu-qbe capability NAME`; nothing falls back to `cpu-c17`. `tools/cint_check.py --backend cpu-qbe --qbe PATH` compares it with the reference.

### Changed

- The release publishes SPEC-04P.5, SPEC-06P.4 and SPEC-09P.5. Every other edition of 0.2.8 stays in force.

### Fixed

- `cint_build` in `rt/cint_build.c` wrote the flag of `--backend cpu-c17-spec` into the first row of the compiler's program table on every build, past the end of a row narrower than five words. `rt/tests/test_bridge.py`, whose test compilers have such rows, reports the overflow under AddressSanitizer. The flag is now written only for `--backend cpu-c17-spec`, and such a build refuses a row that cannot hold it. `cintc` builds are unchanged.

### Compiler coverage

`cintc` refuses the forms that SPEC-09P.5 clause 12 lists with C9102. The suite holds 966 programs and 1,868,209 records, and the reference agrees with every frozen expectation. With GCC 13.3 at `-O2` on Linux x86-64, `cpu-c17` and `cpu-c17-spec` each agree with the reference on the 1,868,004 records of the 761 programs that `cintc` compiles, and `cpu-c17-spec` does so again with every region forced to rerun. With QBE 1.3 and GCC 16.2.1 at `-O2` on Linux x86-64, `cpu-qbe` agrees on the 1,864,316 records of the 690 programs it compiles. The interpreter was not compared over the whole suite for 0.2.17, and no count is claimed for it.

### Known issues

- The workbench, `cint repl` and `cint debug` refuse a module that declares an extern or a `requires` (C4012).
- On the Windows console, F1, F2 and F4 to F12 reach a program as nothing. Outside raw mode the console's own output code page applies, and in line mode a Ctrl+Z at the start of a read ends the input.
- A receipt records the grants given with `--grant`. It does not record the grants a run takes from its program or the program arguments.
- `cint test` takes `--grant` lines, but a test holds no `Grants` value, so they grant no call.
- `fs` does not list, create, remove or rename; a grant set cannot be narrowed.
- `gpu-cuda` refuses a kernel that calls `hadd`, `rhadd`, `rshr` or `rmulh` (capability `builtin`) or holds a fixed-point value (capability `fixed`).
- `cpu-c17-spec` has run only with GCC and Clang on Linux x86-64. It speculates at calls, not at kernel dispatches.
- `cpu-qbe` is experimental: it refuses kernels, `cint test`, libraries, applications, `--frame-arena`, `--width-report` and `--receipt`, and it has run only on Linux x86-64 and macOS arm64.

## [0.2.16] - 2026-11-07

0.2.16 was not published on its own. It changes no file of the published tree except its version.

## [0.2.15] - 2026-11-06

0.2.15 was not published on its own. Its changes are released in 0.2.17.

### Added

- The executor `cpu-c17-spec`, written by `cint emit-c --backend cpu-c17-spec`. At each call from checked code into the module's *speculation set* (lean functions that make no host call, print, assertion, wide or fixed-point operation and call no built-in that can fault except `isqrt` and `isqrt_round`), the emitted C saves the state the callee can write and runs an unchecked twin of the callee that raises a flag where a checked operation would fault. It keeps the twin's result when the flag is clear and the fuel is within the allowance. Otherwise it restores the saved state and runs the checked call. Every state, result, fault record, fuel count and output equals that of `cpu-c17` (SPEC-09P.5 5.3, BACK-17).
- When compiled with `CINT_SPEC_FORCE_RERUN` defined, every region restores and reruns, so a conformance run checks both paths. `tools/cint_check.py --backend cpu-c17-spec [--spec-rerun]` runs the suite that way, and `compiler/tests/spec_forms.py` runs one program for each operation and type of the twin against the reference at every fuel allowance.
- Without `--backend`, `cint emit-c` writes the same bytes as before.

## [0.2.14] - 2026-11-06

0.2.14 was not published on its own. Its changes are released in 0.2.17.

### Added

- The built-in functions `hadd(a, b)` and `rhadd(a, b)`, the average of two values rounded down and up; `rshr(a, k)`, a right shift by `k` rounded half up; and `rmulh(a, k)`, the product of a 16-bit or 32-bit fixed-point value and a `Q1.15` or `Q1.31` coefficient, rounded half up and saturated. All four take integers and fixed point of 8 to 64 bits and return a value of the operand's type. Only `rshr` faults, with `E_SHIFT`, for a count outside the type's width (SPEC-04P.5 7.2.5, IM-190 to IM-192, LS-329). The reference, `cintc` and the interpreter run all four.
- `cintc` writes each of the four as plain C with no runtime helper. It also writes `+|`, `-|` and `as|` on 8-bit to 32-bit values as a clamp of the exact result instead of a helper call, a form that a C compiler can vectorize. Outcomes do not change.
- Conformance cases `conformance/fixed/lane_q_*` and `conformance/kernel/lane_q_*`: exhaustive 8-bit runs, the corners of 16 to 64 bits, a sweep on `Q5.11`, the diagnostics C2001, C2103 and C3001, and the `E_SHIFT` fault.

### Changed

- The multiply and divide of `I512` and `I1024` take shorter paths when both operands fit in 128 bits or the divisor fits in 64 bits, and the division of one limb no longer makes a normalized copy. Results and fault records are unchanged.

## [0.2.13] - 2026-11-04

0.2.13 was not published on its own. Its changes are released in 0.2.17.

### Changed

- In a kernel `sum` whose `where` bounds, checked at the kernel's entry, show from the element width that the running total cannot overflow, `cintc` writes each addition as a plain add. Otherwise it keeps the checked add of the build's helper mode. The final range check, the total, the fault record and the fuel are unchanged. The case `conformance/kernel/sum_bound_plain_add` holds a bound at the limit and one past it.

## [0.2.12] - 2026-11-04

0.2.12 was not published on its own. Its changes are released in 0.2.17.

### Changed

- `cint_rt_fits`, the runtime's test that a value fits a narrower type, checks a signed value against a signed type below 64 bits with one comparison and no branch on the sign. Every result matches the earlier test, and `rt/tests/test_rt.c` compares the two on the boundary values of every target type.

## [0.2.11] - 2026-11-03

0.2.11 was not published on its own. It changes no file of the published tree except its version.

## [0.2.10] - 2026-11-02

0.2.10 was not published on its own. It changes no file of the published tree except its version.

## [0.2.9] - 2026-11-02

0.2.9 was not published on its own. Its changes are released in 0.2.17.

### Added

- `cint --version` prints the version, the compiler source identity and the runtime contract version, and `cint --json --version` prints them as one JSON object. From this release on, the sources of each release carry its version, and a receipt records it as `identity.tool.version` (SPEC-06P.4 6).
- In an `import`, a segment other than the last may be spelled as a keyword: `import kernel.sha256;` names `kernel/sha256.ci`. The last segment names the module in code and stays an identifier (SPEC-04P.5 LS-225).
- A call to a function of an imported module whose result is an error union of that module's error set takes `catch`, as an expression or as a statement on an `E!void` result, and `try` inside a function whose result has the same error set. A `Grants` value may be passed to a function of an imported module.

### Changed

- The cache of `cint run`, `cint build` and `cint test` is `--cache DIR`, else the directory that `CINT_CACHE` names, else the cache directory of the platform: `%LOCALAPPDATA%\cint` on Windows, `$HOME/Library/Caches/cint` on macOS and elsewhere `$XDG_CACHE_HOME/cint` or `$HOME/.cache/cint`. It is never under the directory of `cint`, which may be read-only or shared (SPEC-06P.4 5.3).
- `cint emit-c` reads any module under `--root` that a named module imports and the command line leaves out, and places it before its first importer (SPEC-06P.4 5.7).
- The diagnostics C4012 for a keyword after a type, C1050 for a keyword where a name is expected and C3001 for a declaration of a built-in name state which word they found. Codes and positions are unchanged.
- When given a program with an extern that no standard service serves, `cint run` and `cint test` exit with status 4 and name that extern (SPEC-06P.4 5.4).
- `python/tests/test_calls.py` holds the entry of `test_busy` on an event instead of a long loop, so the second call meets a running entry on any machine. In `tools/cint_check.py`, a compiler run that exits with status 5 counts as an environment failure, apart from the disagreements, and still fails the run.

### Compiler coverage

`cintc` no longer refuses `catch` on a call to a function of an imported module. It refuses, with C9102 and a message that names the limit, a parameter, result or field of an error-set type from another module, an error union of a combined error set of another module and a combined error set with an operand of another module (SPEC-09P.5 clause 12).

## [0.2.8] - 2026-11-01

0.2.8 takes in 0.2.5, 0.2.6 and 0.2.7, which were not published on their own. Each of the four is listed below. 0.2.5 lets a constant expression size a module-level array. 0.2.6 to 0.2.8 make the emitted C do less work for a call between small functions of one module and for a conversion or shift that cannot fault, without changing what any program computes: every result, fault record, fuel count and digest is the one 0.2.4 gives.

### Changed

- For `I8` to `I64` and `U8` to `U64`, `cintc` writes a widening `as` and an unsigned `>>` by a constant count as plain C, and a signed `>>` by a constant count as a call of `cint_rt_asr`, without a checked helper. A widening `as` is one where every value of the source type fits the target, such as `I32` to `I64`, `U8` to `U32` or `U32` to `I64`. The shift count must be a constant from 0 to one less than the width. `<<`, narrowing `as`, signed-to-unsigned `as` and shifts by a variable count still call their checked helpers (SPEC-09P.4 7.5, EMIT-18).
- Inside a kernel or a function that makes a host call or a dispatch, every conversion and shift keeps its checked helper.
- The release publishes SPEC-09P.4. Every other edition of 0.2.4 stays in force.

### Compiler coverage

`cintc` refuses the forms that SPEC-09P.4 clause 12 lists with C9102. The suite holds 942 programs and 1,868,185 records, and the reference agrees with every frozen expectation. The agreement of `cintc` and the interpreter with the reference over the whole suite was not measured again for 0.2.8, and no count is claimed for it.

### Known issues

- The workbench, `cint repl` and `cint debug` refuse a module that declares an extern or a `requires` (C4012).
- On the Windows console, F1, F2 and F4 to F12 reach a program as nothing. Outside raw mode the console's own output code page applies, and in line mode a Ctrl+Z at the start of a read ends the input.
- A receipt records the grants given with `--grant`. It does not record the grants a run takes from its program or the program arguments.
- `cint test` takes `--grant` lines, but a test holds no `Grants` value, so they grant no call.
- `fs` does not list, create, remove or rename; a grant set cannot be narrowed.

## [0.2.7] - 2026-11-01

0.2.7 was not published on its own. Its changes are released in 0.2.8.

### Changed

- A call between two functions of a module's lean set no longer writes the call site or the depth into the context. A lean function is not an entry, an export or a kernel, and it has no error union, frame arena or scratch arena. It makes no host call, dispatch or fixed-point operation and calls only lean functions of its own module. The depth check, the fuel charge and their order are unchanged. On a fault, each lean caller adds its site to the record as the failure returns, so the fault record's `stack` field is byte for byte what 0.2.4 writes (SPEC-09P.4 7.5, EMIT-16, EMIT-18).
- A lean function that charges fuel, calls or prints receives the fuel count as a parameter and returns the new count instead of passing it through the context, and one that calls also receives the remaining depth.
- In the builtin helper mode, a lean function of bounded size that is not recursive and each per-operation helper are marked `CINT_RT_FOLD`, which asks the C compiler to inline them. The portable helper mode does not force inlining.
- `python/tests/programs/calls.ci`: `spin` multiplies inside its loop, so a C compiler that inlines it cannot reduce the loop to a constant and `test_busy` still runs.

### Known issues

- A call of a function in another module is never lean, and neither is its caller. A program whose hot calls cross modules gains less.
- A program with many small functions builds a larger executable and takes longer to compile with forced inlining.

## [0.2.6] - 2026-11-01

0.2.6 was not published on its own. Its changes are released in 0.2.8.

### Added

- `rt/cint_rt_lean.h` holds lean forms of the checked add, subtract, multiply and negate for `I8` to `I64` and `U8` to `U64`: `cint_add_lean_<t>`, `cint_sub_lean_<t>`, `cint_mul_lean_<t>` and `cint_neg_lean_<t>`. Each makes one overflow test and, on overflow, one unlikely branch to a cold writer out of line, `cint_rt_fault_arith_cold`, which writes the same record as the full form. A lean form does not read the context's fault flag first, so it is correct only where no fault can be pending. `rt/cint_rt.h` includes the header. `cintc` emits calls to the full forms.
- `rt/tests/test_rt.c` compares each lean form with its full form on results, return values and encoded fault record bytes, in both helper modes.

### Changed

- The runtime header digest in a receipt covers `rt/cint_rt_lean.h` as well as `rt/cint_rt.h` and `rt/cint_rt_host.h`. Every receipt identity changes from this release on.

## [0.2.5] - 2026-11-01

0.2.5 was not published on its own. Its changes are released in 0.2.8.

### Added

- The extent of a module-level array may be any `I64` constant expression at every rank: `I64[N] table;`, `I64[SLOTS * WORDS + 1] room;` and `I64[0x10] t;`. A constant may be declared after the array that uses it. `cintc` emits the same bytes for an extent written as a constant as for the same extent written as a literal.
- Seven conformance cases under `conformance/module/extent_*` cover named, product and maximum extents, a named extent in a struct field and the diagnostics C2001, C6004 and C2015.

### Changed

- A module-level array extent that is not valid gets the reference's diagnostic instead of C9102: C2001 for an extent that is not `I64` (`const I32 N`), C6004 for one that is not constant and C2015 for a negative one. C9102 remains for the size limits.
- `I64[_] a;` at module level still fails with C9102, now reported at the type instead of the name.

### Compiler coverage

`cintc` no longer refuses a module-level array whose extent is a name or a constant expression, and clause 12 of SPEC-09P.4 no longer lists that refusal.

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
