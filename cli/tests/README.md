# Checks of the `cint` command

Build `cint` with `tools/cint_bootstrap.py` first. `<build>` below is the build directory, `$CINT_BUILD`, which defaults to `cint-build` in the system temporary directory. `--leg` is `gcc`, `clang` or `msvc`.

```text
python -m unittest discover -s cli/tests -p test_compare_bytes.py -v
python cli/tests/compare_bytes.py --expect examples/hello.stdout -- <build>/boot/gcc/cint run examples/hello.ci
python cli/tests/run.py --leg gcc
```

`--exe` names an existing executable, `--out` the evidence directory (by default `<build>/cli`), and `--case` one test method. `--suite` is `all` (the default), `cli`, `compiler` or `parity`.

## Suites

- `cli` checks the command's boundary: stdout bytes, the human and JSON channels, the exit statuses 0, 1, 2 and 4 to 7, receipts and their canonical serialization, hashes computed independently, the compiler source manifest, source and runtime snapshots, generated files, cache isolation under concurrent runs, toolchain binding, a launch through a symlink (the toolchain and `lib/` beside the real executable), and process failures. Status 3 is outside the run path. Status 6, an error result of `main` (`rt/OPEN.md` RT-OQ-33), is checked with error records that a stand-in host injects: the stderr line, the `CINT-ERROR-1` object, the receipt, malformed records (status 7), and `cint test` naming an error that leaves a test. Frozen fault comparisons check every canonical field, the revision against the SIR-17 manifest assembled independently, and the fuel consumed against the frozen `fuel-consumed` line; the hello and overflow examples compare their fuel with `cint_ref`. The program's fuel record (`compiler/OPEN.md` CINTC-OQ-46) is required after statuses 0, 1 and 6.
- `compiler` checks contracts of the compiler that the command relies on: a narrowing literal gives `C2003`, a type mismatch after an `I8` declaration gives `C2001`, and a runtime fault carries the revision that SPEC-09 CONF-01 requires, equal to the SIR-17 manifest assembled from the generated SIR text and unchanged by blank lines.
- `parity` runs three programs of the seed's subset and eighteen boundary outcomes from fifteen sources, with entries, values, fuel and diagnostics from their frozen headers and expectations. Add `--case test_parity_control_else_if_126_middle` to run one outcome.

Each run writes a new directory, `<out>/<leg>/<suite>-<unique>` (`cases-<unique>` for a `--case` run), with raw outputs, fixtures, commands, receipts and `test-summary.json`, and leaves earlier runs in place. The latest summary is also copied to `<out>/<leg>/test-summary-<suite>.json`. A summary records the selected cases, the methods that passed, skips, where the run stopped, and the executable's hash before and after; a change of executable fails the run.

## How the checks work

Source metadata checks create a local git repository and compare its head, and its clean, modified and untracked state, with the receipt. A source outside git, or a run without git, continues with null repository metadata. A receipt marks each identity it cannot give (meaning, source map, build, execution and revision) as null, and carries the fuel consumed as an exact decimal string; source file hashes are still checked.

The runner builds a small stand-in compiler with the configured C compiler, and uses it to check captured compiler output, warnings, rejected sources, null stdin, an unknown process status, program stderr and malformed runtime records. The injected records cover `Bool`, minimal `Z`, zero, the runtime's byte bound, trailing bytes, UTF-8 fields, and the required `Z` field `exact`.

A test-only unit includes the command's source and links the bootstrap's compiler and runtime objects. It checks the compile-time `Z` bounds of 513 operand bytes and 520 exact bytes, then raises a real runtime fault to check that the compiler's fault is kept, its canonical bytes, and the input and compiler identities. It adds no option to the command itself.

`compare_bytes.py` captures stdout as bytes. It expects exit status 0 unless given `--exit-status`, such as `--exit-status 1` for output written before a fault, and on success ends with `identical: <n> bytes`.

## Parity

```text
python -m unittest discover -s cli/tests -p test_parity.py -v
python tools/cint_bootstrap.py --leg gcc
python cli/tests/run.py --leg gcc --suite parity --out <build>/review/parity
```

Each runtime case stages its exact source as a byte prefix under the same module-relative path, then appends a `main` that calls the entry and prints one result line. The reference checks the original export against its frozen expectation and runs the staged program on its own. The command must match the staged program's stdout bytes, exit status and fuel. The fuel of the original export and of the staged program are recorded separately. The compile-error case runs the source unchanged.

`parity.json` records the source and expectation hashes, the staged hashes, the reference commands and channels, the command's receipts and their identity digests, and the hashes of each module's generated C and canonical SIR text (`.sites`), read through this run's manifest and receipt. The long-branch and nested-block cases also check that the generated C nests braces at most two deep, outside comments and quoted tokens.

The bootstrap receipt, the compiler source archive and the executable's identity are checked before the suite runs, and the executable's hash again after it. This suite builds no stand-in host and no test-only unit.

`boot/string_literal_view` and `module/import_plain` are listed as waiting cases, and the sixteen cases that only the seed refuses are listed as excluded; neither list counts as a test. Parity alone does not establish conformance.
