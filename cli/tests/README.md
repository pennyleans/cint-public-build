# cli checks

build each executable with `tools/cint_bootstrap.py` before running these checks. `<build>` below is the build directory, `$CINT_BUILD`, and `<checkout>` is this repository's path inside wsl.

```text
python -m unittest discover -s cli/tests -p test_compare_bytes.py -v
python cli/tests/compare_bytes.py --expect examples/hello.stdout -- <build>\boot\msvc\cint.exe run examples/hello.ci
python cli/tests/run.py --leg msvc
```

for gcc and clang, run the runner with linux python inside wsl:

```text
wsl.exe -d Ubuntu-24.04 --cd <checkout> -- python3 cli/tests/run.py --leg gcc
wsl.exe -d Ubuntu-24.04 --cd <checkout> -- python3 cli/tests/run.py --leg clang
```

`--exe` selects an existing executable. `--out` selects the evidence directory; its default is `<build>/cli`, where `CINT_BUILD` defaults to `cint-build` in the system temporary directory. `--case` selects one test method.

the default suite includes the cli and compiler contract checks. `--suite cli` checks the cli boundary. `--suite compiler` checks inherited compiler blockers: a narrowing literal must produce `C2003`, a type mismatch after an `I8` declaration must produce `C2001`, and a runtime fault must carry the revision required by `CONF-01`, equal to the SIR-17 manifest these tests assemble from the generated SIR text and unchanged by blank lines. failures remain failures.

each invocation creates a unique directory under `<out>/<leg>/<suite>-<unique>` containing raw outputs, fixtures, commands, receipts, and `test-summary.json`. targeted `--case` runs use the prefix `cases`. earlier runs remain intact. a latest summary is also copied to `<out>/<leg>/test-summary-<suite>.json`. summaries record the selected cases, successful methods, skips, failure boundary, and executable hashes before and after; an executable change fails the run.

the cli suite checks stdout bytes, human and json channels, reachable statuses 0, 1, 2, 4, 5, 6, and 7, explicit receipts, canonical serialization, independent hashes, compiler source manifest entries, source and runtime snapshots, generated files, concurrent cache isolation, toolchain binding, and process failures. status 3 is outside the run path. status 6 (an error result of `main`, rt/OPEN.md RT-OQ-33) is checked with error records the fake host injects: the stderr line, the `CINT-ERROR-1` object, the receipt, malformed records (status 7), and `cint test` naming an error that leaves a test. frozen fault comparisons check every canonical field, the revision against the independently assembled SIR-17 manifest, and fuel consumed against the frozen `fuel-consumed` line; hello and the overflow example compare fuel consumed with `cint_ref`. the program's fuel record (compiler/OPEN.md CINTC-OQ-46) is required after statuses 0, 1, and 6.

source metadata checks create a local git repository and independently compare its head and clean, dirty, and untracked status with the receipt. sources outside git and runs with git unavailable must continue with null repository metadata. receipts explicitly mark unavailable meaning, source-map, build, execution, and revision identities as null and carry fuel consumed as an exact decimal string; source file hashes remain checked independently.

the runner builds a small fake compiler executable with the configured host compiler. it uses that fixture to check captured compiler output, warnings, rejected source, null stdin, unknown process status, program stderr, and malformed runtime records. injected record checks cover `Bool`, minimal `Z`, zero, the runtime byte bound, trailing bytes, utf8 fields, and the required `Z` exact field.

a private fixture includes the cli source and links the bootstrap's compiler and runtime objects. it directly checks the compile-time `Z` bounds of 513 operand bytes and 520 exact bytes, then creates a real runtime fault to verify internal compiler fault retention, canonical bytes, and input and compiler identities. it adds no production injection option.

the comparator captures stdout directly as bytes. it defaults to expected exit status 0; use `--exit-status 1` for partial output before a fault. success ends with `identical: <n> bytes`.

## scoped boot and boundary parity

```text
python -m unittest discover -s cli/tests -p test_parity.py -v
python tools/cint_bootstrap.py --leg msvc
python cli/tests/run.py --leg msvc --suite parity --out <build>/review/parity
```

`--suite parity` selects three admitted boot programs and eighteen boundary outcomes from fifteen sources. combine it with `--case test_parity_control_else_if_126_middle` to select one outcome. entries, values, fuel, and diagnostics come from the frozen headers and expectations. the existing `all`, `cli`, and `compiler` selections keep their scope.

each runtime case stages its exact source as a byte prefix under the same module-relative path, then appends a `main` that calls the entry and prints a unique result line. the reference checks the original export against its frozen expectation and independently runs the staged wrapper. the cli must match the wrapper's stdout bytes, exit status, and measured fuel. original export and wrapper fuel are recorded separately. the compile error runs the unmodified source.

`parity.json` records source and expectation hashes, staged hashes, reference commands and channels, cli receipts and their identity digests, and generated module C and canonical SIR (`.sites`) hashes. generated files are read through this invocation's manifest and receipt. the designated long-branch and nested-block cases also assert generated C brace depth at most two, ignoring comments and quoted tokens.

the bootstrap receipt, compiler source archive, and executable identities are checked before parity runs. the executable hash is checked again after the suite. this suite builds no fake host or private cli unit fixture.

`boot/string_literal_view` and `module/import_plain` remain named waiting cases for task 2.14. the sixteen seed-only refusal cases are recorded as excluded from this B1 run. these inventory records are outside the test count. scoped parity does not establish CONF-13 conformance or full T2 acceptance.
