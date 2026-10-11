# Benchmarks

`tools/cint_bench.py` times cint programs against baselines written in C and Rust and writes `perf`
receipts (SPEC-06 11 and 12.2). It stands in for a `cint bench` command.

## Layout

| Path | What it holds |
| --- | --- |
| `workloads/<name>.json` | A workload manifest: the cint source, its form, parameters, method, variants and bars |
| `workloads/orbit_1e8/` | The program of the orbit paper (<https://integerc.dev/papers/orbit/>), run for 10^8 steps |
| `baselines/<workload>/` | Baseline programs: the same algorithm in C17, built by the same compiler with the same flags |
| `cpu_trials/` | `ledger.ci`, the program that `ledger` and `transactions` time, and its Rust baseline `rust/ledger.rs` |
| `driver/` | `bench_call.c`, which times each call of one function in process, and its two targets: `target_cint.c` (a cint export through its C wrapper) and `target_c.c` (a baseline with a C ABI) |
| `tests/` | A per-iteration stand-in (`tick.ci`, `tick_c.c`, `tick_stream.json`) for `tests/test_cint_bench.py` |

| Workload | What is compared | Form | Variants |
| --- | --- | --- | --- |
| `orbit_1e8` | cint's time over the time of the same integrator in `double` | program | cint with portable and with built-in helpers; C `double` |
| `ledger`, `transactions` | cint's time over the faster checked C build, and over Rust with `checked_*`; the cost of fuel is reported beside it | call | cint with portable helpers, built-in helpers, and built-in helpers with fuel on; checked C with built-in checks (not on MSVC) and with portable checks; Rust |

A bar is a limit on a ratio of median times that the receipt judges, such as cint with built-in
helpers at most 8 times the `double` build on `orbit_1e8`.

## Commands

Bootstrap a `cint` from the same checkout, then run a workload with one C compiler (`gcc`,
`clang`, `apple-clang` or `msvc`):

```sh
python tools/cint_bootstrap.py --leg gcc --out BOOT
python tools/cint_bench.py run orbit_1e8 --leg gcc --cint BOOT/cint
python tools/cint_bench.py run ledger --leg gcc --cint BOOT/cint
python tools/cint_bench.py run transactions --leg gcc --cint BOOT/cint
python tools/cint_bench.py list
```

`BOOT` is a directory outside the checkout; with `msvc` the executable is `BOOT/cint.exe`.

The default is the full size at `--opt 2`, with the manifest's method. `--size agreement` runs
the agreement parameters instead, `--only` selects variants, `--rustc` names the Rust compiler
(otherwise `rustc` on `PATH`, then `~/.cargo/bin`; without one the Rust variant is not run and
the receipt says so), and `--cpu N` pins each timed process to one CPU on Linux. The `cint` must
come from `tools/cint_bootstrap.py` at the same compiler sources: the tool reads the
`bootstrap.json` beside it and refuses any other. Never run with `python -O`.

## Receipts

Each run writes into `<out>/receipts`, where `<out>` is `--out` or, by default,
`bench/<workload>-<leg>-O<opt>` under the build directory (`CINT_BUILD`, or `cint-build` in the
system temporary directory). It refuses a directory that already holds receipts.

- `agree-<workload>-<leg>-O<opt>-<variant>.json`, kind `diff`: each cint variant against
  `cint_ref` at the agreement parameters, through `cint-harness` and `bench-call` for a call, and
  through stdout, fuel and the fault record for a program. Nothing is timed unless every cint
  variant agrees.
- `perf-<workload>-<leg>-O<opt>.json`, kind `perf`: the identity (workload, method, variants,
  sources and the agreement receipts), the results, and the observations (samples, statistics,
  bar verdicts, toolchain and machine).

Every timed sample must give the same result: an `I64` baseline must return the cint result, and
a program must print output with the recorded digest. Otherwise no `perf` receipt is written.
Fuel off is a call with no allowance, and fuel on is a call with the allowance 2^63 - 1; both
count fuel (SPEC-01 IM-137). `python tools/cint_receipts.py identity DIR --pattern "*.json"`
checks the receipts' form and identities.
