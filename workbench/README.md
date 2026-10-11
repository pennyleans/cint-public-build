# cint workbench

This directory builds the tools for running cint programs interactively and for finding where they fault: `cint workbench`, `cint repl`, `cint explain` and `cint debug`. They run programs on two backends: `cpu-sir-interp`, the interpreter in `interp/`, and `cpu-c17`, the C that `cintc` emits. The rules they follow are in SPEC-06. Where this file and the specification differ, the specification governs, and the difference is a defect in this file.

## Building

```
python3 tools/cint_bootstrap.py --leg gcc
python3 tools/cint_tools.py --leg gcc
```

The first command builds `cint`. The second builds the tools beside it, `cint-workbench`, `cint-repl`, `cint-explain`, `cint-debug` and `cint-interp`, at `-O2` (`/O2` on `msvc`). The legs are `gcc`, `clang`, `apple-clang` and `msvc`. Both write to `<build>/boot/<leg>`, where `<build>` is the `CINT_BUILD` environment variable or `cint-build` in the system temporary directory. Each tool is its own executable, `cint-<name>`, and `cint <name>` runs it from the directory of `cint` with the remaining arguments, so `cint debug locate prog.ci` runs `cint-debug locate prog.ci`. A tool compiles through the `cint` in its own directory unless given `--cint PATH`.

## The tools

| Command | What it does |
| --- | --- |
| `cint workbench` | An interactive session. Type a declaration, a statement or an expression, and the session compiles it, runs it and prints the exact value with its type. Checkpoints, branches, `:undo`, watches, replay and `:save` keep the session's history. `:help` lists the commands. |
| `cint repl` | The same session under a shorter name. |
| `cint explain FILE.ci[:NAME]` | What the compiler knows about each function of a module: its signature, every site that can fault and the operation there, its prints, its fuel charge points and its SIR text. |
| `cint debug` | Opens a recording or a case read-only in a session. `cint debug diff` runs a case on two backends and finds the first boundary where they differ, binding by binding. `cint debug replay` replays a recording and compares each boundary with it. `cint debug locate` finds the first boundary that ends in a fault or an error result. |
| `cint interp` | Built from `interp/`. Runs a program's SIR text, writes a recording (`run --record`) or a case (`run --case`), replays either one (`replay`), and prints a checkpoint, recording or case (`show`). |

Each tool prints its full usage with `--help`. Under `--json`, a tool reports an error that stops it as one `CINT-TOOL-1` line on standard error.

## A session

```
$ cint repl
cint workbench   profile cint-core-1   backend cpu-sir-interp   new session
ci[1]> I64 big = 9223372036854775807;
big = 9223372036854775807 : I64
ci[2]> big + 1
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> entry 2:1:5
   |
 1 | big + 1
   |     ^ add.checked.i64
   |
   = operation: add.checked.i64
   = operand-count: 2
   = operand: I64 9223372036854775807
   = operand: I64 1
   = exact:   9223372036854775808
   = limit:   I64 9223372036854775807
   = revision: 6d65c1a6275af8b5f6f0db1b99da50149da67bef4764a708a8f625db192c0324
   = source-map: none
   = address: none
   = stack-depth: 0
   = fuel-consumed: 1 (not part of the fault record)
   = state: no store completed in this entry; context reset
   = noncanonical: not part of the fault record
   = workbench-position: entry 2
   = site: wb_entry#2
   = backend: cpu-sir-interp
ci[3]>
```

Every tool prints a fault in the layout that `cint run` prints. The lines from `operation` to `stack-depth` are the canonical fault record: every backend that runs the program reports the same values. The lines after `= noncanonical` say where the workbench found the fault and are not part of the record.

## Tests

```
python3 workbench/tests/test_workbench.py --leg gcc --cint <build>/boot/gcc/cint
python3 interp/tests/test_interp.py --leg gcc
```

`test_workbench.py` builds the tools at `-O0` and `-O2`, and on Linux also with the address and undefined-behavior sanitizers, then checks their usage, exit statuses and `--json` errors, `cint explain` against the SIR text it reads, and session transcripts. With `--cint`, it also checks sessions in which that `cint` compiles each entry. `workbench/tests/golden/` holds what the tools print for fixed inputs: explain reports, locate reports and session transcripts. Like a conformance `.expect` file, each golden file is written once by `python3 workbench/tests/wbt_golden.py --write` and changes only in a change of its own.
