# SPEC-06P.3: The cint tools

SPEC-06P.3, edition 3 of the specification of the cint tools, cint v0.2.4, 2026-10-24.

This edition specifies the command lines, outputs and exit statuses of the tools of cint 0.2.4. The language they run is specified by SPEC-04P.4, and the byte encodings of the files they read and write by SPEC-01P.3. It replaces SPEC-06P.2: it adds the executable that `cint run` keeps for a later run (5.4), `--rebuild` and `--frame-arena` (5.1, 5.4, 5.5), `cint build --app` (5.5), the grants a run takes from its program (5.4) and the run parameters that a receipt records (12.3).

<a id="foreword"></a>

## Foreword

This document specifies the cint tools as cint 0.2.4 provides them: the `cint` command and the tools it starts, the interpreter `cint interp`, the workbench `cint workbench`, `cint explain` and `cint debug`.

Rules are numbered paragraphs within each section and are cited by section and paragraph, such as 5.4, paragraph 3. A rule that applies a clause of another cint specification ends with that clause's identifier in brackets, after the name of the edition that states the clause, such as [SPEC-04P.4 LS-234]. An identifier names one rule in every edition and document; section numbers belong to this edition only.

<a id="introduction"></a>

## Introduction

cint is a C-family language whose every value is an integer of a declared type, or is built from such integers, and in which every plain operator is checked. Its tools turn a source file into a running program, and they also explain, record, replay and compare runs, so that a reader can find out why a program did what it did.

The first tool is `cint`. It compiles a program to C, builds it with the host C compiler, and runs it:

```console
$ cint run hello.ci
hello, world
answer=42
$ echo $?
0
```

The other tools work beside it. `cint interp` runs the same program from its intermediate representation without a C compiler, and records runs. `cint workbench` is an interactive session: each line typed at its prompt is compiled and run, and the session can show any value exactly, keep checkpoints and go back to an earlier state by replay. `cint explain` prints what the compiler knows about a function without running it. `cint debug` opens a recording, replays it, compares two executors on it and locates the boundary at which a run faulted.

In the source tree, `python3 tools/cint_bootstrap.py --leg LEG` builds `cint` with the C compiler `LEG`, one of `gcc`, `clang`, `apple-clang` and `msvc`. `python3 tools/cint_tools.py --leg LEG` then builds `cint-debug`, `cint-explain`, `cint-workbench`, `cint-repl` and `cint-interp` beside it.

<a id="how-this-document-is-organized"></a>

### How this document is organized

Clauses 1 to 4 state the scope, the documents this one relies on, its terms and what conformance means for a tool. Clause 5 specifies the `cint` command, its four subcommands, and how it starts the other tools. Clause 6 lists the identities the tools print, and clause 7 the text and machine forms of diagnostics, fault reports and other messages. Clause 8 covers the interpreter, clause 9 the workbench and `cint explain`, and clause 10 `cint debug`. Clause 11 describes checkpoints, recordings and cases and clause 12 the receipts the tools write. Clause 13 lists directions beyond this edition. Annex A holds longer transcripts.

Examples of programs are written in `cint` blocks, each followed by the exact output of `cint run`. Transcripts of the tools are written in `console` blocks. A line that starts with `$ ` is a command, and the lines after it are what the command wrote to standard output and standard error, in the order a terminal shows them. In a session, the text after a prompt `ci[1]> ` or `..> ` is what was typed. `$ echo $?` prints the exit status of the command before it. A line `...` stands for lines left out.

Every transcript was produced by the tools of cint 0.2.2 or later; a transcript that shows what this edition adds was produced by the tools of cint 0.2.4. A digest in a transcript, such as a 64-digit revision identity (clause 6), is the value the tools printed for that file with that build; the text says what each digest is. Digests change when the source, the compiler or the runtime changes and timings change from run to run.

<a id="scope"></a>

## 1 Scope

1. This document specifies, for cint 0.2.4:
   - the `cint` command, with its subcommands `run`, `build`, `test` and `emit-c`;
   - the identities that the tools print, and where they print them;
   - the text and machine forms of diagnostics, fault reports, error results, test results and tool errors;
   - the interpreter command `cint interp`;
   - the workbench session `cint workbench`, also started as `cint repl` and the tool `cint explain`;
   - the tool `cint debug` and its forms `diff`, `replay` and `locate`;
   - checkpoints, recordings, cases and saved sessions as the tools make and show them;
   - the receipts that the tools write.
2. This document does not specify:
   - the language, its faults, fuel and limits, which SPEC-04P.4 specifies;
   - the byte encodings of fault records, state, checkpoints, recordings, cases and identities, which SPEC-01P.3 specifies;
   - how an executor carries out a kernel dispatch, which SPEC-02P.2 specifies;
   - the C and Python interfaces of a host program, which SPEC-03P.3 specifies;
   - the intermediate representation, the C that the compiler emits, the toolchain file and the conformance suite, which SPEC-09P.3 specifies;
   - the layout of the build cache beyond what 5.3 states, timings, and the wording of messages other than those this document quotes.

<a id="normative-references"></a>

## 2 Normative references

The following documents are referred to in the text in such a way that some or all of their content constitutes requirements of this document. For dated references, only the edition cited applies.

- SPEC-04P.4, *The cint language*, edition 4, cint v0.2.4.
- SPEC-01P.3, *Canonical encodings and identity*, edition 3, cint v0.2.3.
- SPEC-09P.3, *Toolchain and conformance*, edition 3, cint v0.2.4.
- A. Rundgren, B. Jordan and S. Erdtman. *JSON Canonicalization Scheme (JCS)*. RFC 8785, June 2020. Used for the bytes of a receipt, with the departure in the escaping of strings that 12.1 states.
- T. Bray, editor. *The JavaScript Object Notation (JSON) Data Interchange Format*. RFC 8259 (STD 90), December 2017.
- F. Yergeau. *UTF-8, a transformation format of ISO 10646*. RFC 3629 (STD 63), November 2003.
- G. Klyne and C. Newman. *Date and Time on the Internet: Timestamps*. RFC 3339, July 2002.
- National Institute of Standards and Technology. *Secure Hash Standard (SHS)*. FIPS PUB 180-4, August 2015. Used for SHA-256.

<a id="terms-definitions-and-symbols"></a>

## 3 Terms, definitions and symbols

For the purposes of this document, the terms of SPEC-04P.4 clause 3 apply, among them value, fault, fault record, operation identifier, diagnostic, refusal, the reference, executor, entry, fuel, call depth, frame arena, semantic input, module, script and revision. The following terms also apply.

<a id="term-tool"></a>**3.1 tool**: the `cint` command, or one of the commands that it starts: `cint interp`, `cint workbench`, `cint repl`, `cint explain` and `cint debug` (5.1).

<a id="term-exit-status"></a>**3.2 exit status**: the integer a tool returns to the operating system when it ends, from the table of 5.2.

<a id="term-tool-error"></a>**3.3 tool error**: the refusal by a tool of its command line or its environment, such as an unknown option or an unreadable file. A tool error is not an outcome of a program, and a tool that reports one runs no program.

<a id="term-machine-form"></a><a id="term-text-form"></a>**3.4 text form, machine form**: the two forms of a message. The *text form* is written for a person to read. The *machine form* is one JSON object, named by its `schema` key, written when the tool is given `--json`.

<a id="term-output-set"></a>**3.5 output set**: the files that `cint emit-c` writes for a program: the C of each module and of the program, the intermediate representation of each module as text, and a manifest of their digests (5.7).

<a id="term-project-directory"></a>**3.6 project directory**: a directory that holds a program's modules, either directly or in a subdirectory `src`, with the root module `main.ci` (5.5).

<a id="term-boundary"></a>**3.7 boundary**: a point of a run at which the state of its context is defined and its state hash is taken, such as the end of an entry that ran (SPEC-01P.3, 3.16).

<a id="term-recording"></a>**3.8 recording**: a file that holds a program, a checkpoint (3.10) of a context when recording began and the events that followed, from which the run can be replayed (SPEC-01P.3, 3.14).

<a id="term-case"></a>**3.9 case**: a recording that also holds the source text of every module of its program (SPEC-01P.3, 3.15).

<a id="term-checkpoint"></a>**3.10 checkpoint**: the canonical bytes of a state, taken between entries, from which a context can be restored (SPEC-01P.3, 3.11).

<a id="term-replay"></a>**3.11 replay**: running the entries of a recording again from its initial state with the recorded inputs, and comparing each boundary with the recording.

<a id="term-receipt"></a>**3.12 receipt**: a canonical JSON file that records one run of a tool: its inputs, its outcome and what it observed (SPEC-09P.3, 3.14).

<a id="term-session"></a>**3.13 session**: one run of the workbench: a sequence of entries and commands typed at its prompt, with the state they produce.

<a id="term-identity"></a>**3.14 identity**: a SHA-256 digest, written as 64 lowercase hexadecimal digits, that names a program, a file or a state (clause 6).

<a id="conformance"></a>

## 4 Conformance

1. In this document, "shall" states a requirement on a tool and "may" a permission.
2. A conforming tool accepts the command lines this document specifies, writes the outputs it specifies to the channels it specifies, and ends with the exit status it specifies.
3. The tools that run programs are implementations in the sense of SPEC-04P.4 clause 4. A program has one outcome for each set of semantic inputs: the same output bytes, the same fault record and the same fuel consumed under `cint run`, `cint test`, `cint interp` and the workbench.
4. A text form is written for reading, and its wording may change from one release to the next, except for the layouts of diagnostics and fault reports, which 7.1 and 7.3 fix. A machine form names its format and version in its `schema` key, and a reader locates every field by its key, never by its position.
5. A tool that cannot do what its command line asks reports a tool error, with exit status 4, 5 or 7, and the rules of the other statuses do not apply to it.
6. Transcripts and Notes are informative. Where a transcript and a rule differ, the rule governs.

<a id="the-cint-command"></a>

## 5 The `cint` command

<a id="command-line"></a>

### 5.1 Command line

`cint` translates and runs programs, and it starts the other tools. Each use names one subcommand: `run` runs a program, `build` writes an executable or a library, `test` runs the tests of a module, and `emit-c` writes the C and the intermediate representation of a program without compiling them. The other tools are started as `cint NAME`.

```text
cint [--json] run [--receipt PATH | --width-report PATH] [--rebuild] [--root DIR] [--stdlib DIR] [--toolchain FILE] [--cache DIR] [--grant LINE]... [--frame-arena N] FILE.ci [-- ARG...]
cint [--json] build [--receipt PATH | --width-report] [--root DIR] [--out DIR] [--lib | --app] [--stdlib DIR] [--toolchain FILE] [--cache DIR] [--frame-arena N] [FILE.ci | DIR]
cint [--json] test [--receipt PATH] [--root DIR] [--stdlib DIR] [--toolchain FILE] [--cache DIR] [--grant LINE]... [FILE.ci | DIR]
cint [--json] emit-c --root DIR --out DIR [--stdlib DIR] [--toolchain FILE] MODULE.ci...
cint NAME ARGUMENTS...
```

1. `cint --help` and `cint help` write the usage to standard output and exit with status 0. `cint` with no subcommand writes it to standard error and exits with status 4, and so does an unknown subcommand.
2. `cint NAME` runs the executable `cint-NAME` (`cint-NAME.exe` on Windows) from the directory that holds `cint`, with every other argument in order, a `--json` written before `NAME` included, on the same standard input, standard output and standard error, so that `cint workbench` is interactive in a terminal; `cint` exits with that tool's exit status. A tool finds the `cint` beside it, so `cint` passes it no option of its own. The tools of 0.2.4 are `interp`, `workbench`, `repl`, `explain` and `debug`. A `NAME` is 1 to 64 lowercase letters, digits and `-`, and does not begin with `-`. A `NAME` outside that rule, or one with no executable `cint-NAME` beside `cint`, is an unknown subcommand, exit status 4, and an executable that `cint` cannot start gives exit status 5. Clauses 8 to 10 specify the arguments of each tool, and this document names each tool by its command: `cint debug` is the executable `cint-debug`.
3. For `run`, `build`, `test` and `emit-c`, `--json` may appear anywhere on the command line. It selects the machine forms of clause 7 for everything the tool writes to standard error.
4. The options and the file may appear in any order after the subcommand, up to the `--` of `run` (paragraph 6). Each option other than `--json`, `--lib` and the `--width-report` of `build` takes one value. `--grant` may be given up to 64 times, and every other option at most once; otherwise the exit status is 4, with the message "an option needs one value and is given at most once". An empty `--grant` line is a usage error, exit status 4.
5. An option that the subcommand does not take is a usage error, exit status 4. `--out` belongs to `build` and `emit-c`, `--lib` and `--app` to `build`, `--receipt` and `--cache` to `run`, `build` and `test`, `--stdlib` to all four, `--grant` to `run` and `test`, `--width-report` to `run` and `build`, `--rebuild` to `run`, and `--frame-arena` to `run` and `build`. `--width-report` is not given with `--receipt` or `--lib`.
6. For `run`, `--` ends the options, and every word after it is a program argument, in order (SPEC-04P.4 LS-221). A program whose `main` takes the program arguments receives them; any other program given one exits with status 4 before any of it runs. `cint run` given more than one file exits with status 4.

The options:

| Option | Meaning |
| --- | --- |
| `--json` | Write the machine forms of clause 7 to standard error. |
| `--receipt PATH` | Write a receipt of the run, the build or the tests to `PATH` (clause 12). |
| `--root DIR` | The directory that module paths are relative to (5.3). |
| `--toolchain FILE` | The toolchain file that names the host C compiler and its flags, in place of `cint.toolchain` beside `cint` (SPEC-09P.3). |
| `--cache DIR` | The directory for the files of each build (5.3), in place of the one the toolchain file names. |
| `--out DIR` | Where `build` writes its executable or library, or where `emit-c` writes its output set. |
| `--lib` | `build` writes a shared library in place of an executable. |
| `--app` | `build`: the executable takes every word after its name as a program argument (5.5). |
| `--stdlib DIR` | The standard library root (5.3), in place of the directory `lib` beside `cint`. |
| `--grant LINE` | Grant the program one capability (5.4). |
| `--rebuild` | `run`: compile the program even when the executable of an earlier run is kept (5.4). |
| `--frame-arena N` | The frame-arena capacity of the program, in elements (5.4, 5.5). |
| `-- ARG...` | `run`: end the options; each later word is a program argument (paragraph 6). |
| `--width-report PATH` | Write the width report of the program's wide operations to `PATH` (`run`), or build the program to write it (`build`) (5.8). |

<a id="exit-statuses"></a>

### 5.2 Exit statuses

Every tool ends with one of eight exit statuses. A status says which kind of outcome the tool reached, so that a script can tell a program's fault from a failed check or a tool error.

| Status | Meaning | Written by |
| --- | --- | --- |
| 0 | Success: the program ended normally, the build was written, every test passed, or the check found what it looked for. | every tool |
| 1 | The program stopped with a fault. Standard output holds what it wrote before the fault. | `cint run`, `cint interp run` |
| 2 | Translation rejected the program with a diagnostic, or refused it. Nothing ran. | `cint run`, `build`, `test`, `emit-c` |
| 3 | A check failed: a test failed, a replay diverged or was refused, a comparison found a difference, a locate found no target, or a file is not a complete recording or checkpoint. | `cint test`, `cint interp`, `cint debug` |
| 4 | Usage error: the command line is wrong. | every tool |
| 5 | Environment error: a file cannot be read or written, or a tool or the host C compiler cannot be found. | every tool |
| 6 | `main` returned an error. Standard error names the error set, the value and its tag. | `cint run`, `cint interp run` |
| 7 | Internal error: the compiler faulted, the host C compiler rejected or warned on the emitted C, or the program process ended without a cint status. | every tool |

1. A program that faults ends with status 1 and a program whose `main` returns an error with status 6; neither is a tool error. `main` returning an error is not a fault: the run completed and its result is an error value [SPEC-04P.4 LS-221].
2. A tool error ends with status 4, 5 or 7, and its message is the only thing the tool writes to standard error.

<a id="what-every-subcommand-does"></a>

### 5.3 What every subcommand does

1. *Channels.* Standard output carries only the bytes that the program's print statements write, byte for byte, and `cint` flushes it before it reports a fault. It is also flushed before a `console` read waits for input (`read`, `read_line`) and after each `console` `write`, so that a prompt or a redrawn screen reaches the terminal at once; between those points the C library may hold what print statements write (SPEC-04P.4 6.9.9). Everything else goes to standard error: diagnostics, the fault report, the error result of `main`, the line `built: PATH` of `cint build`, the line `receipt: PATH` when a receipt is written, and tool errors. Under `--json`, the lines `built:` and `receipt:` are not written, and every other line on standard error is one machine-form object (clause 7).
2. *Module paths.* The root of a program is the directory of `FILE.ci`, or `--root DIR` when it is given; then `FILE.ci` is a path under that root. A module path is relative to the root: it is one or more segments separated by `/`, each made of ASCII letters, digits, `_`, `-` and `.`, with no empty segment and no segment `.` or `..`. Each segment, with `.ci` removed from the last one, is an identifier, so `import util.money;` names the file `util/money.ci` under the root. An import of one segment, such as `import fs;`, whose file is absent under the root names the module of that name in the *standard library root*: the directory `--stdlib DIR`, or by default the directory `lib` beside `cint`. A module file of that name under the root is used instead, and without a standard library root each import names one path, under the root [SPEC-04P.4 LS-225]. When the path of `FILE.ci` is outside this grammar, or the file is missing, a symbolic link, unreadable or larger than 4 MiB, `cint` exits with status 5. An imported module that is missing, a symbolic link, unreadable or larger than 4 MiB is the diagnostic C3009, and one whose path breaks the rule of SPEC-09P.3 7.1 is the diagnostic C3030, each with exit status 2 [SPEC-09P.3 CINTC-12].
3. *The program process.* `cint run` and `cint test` build the program into a fresh directory of the cache, `CACHE/DIGEST/run-N`, where `DIGEST` is the SHA-256 digest of the root path and the source, and `N` counts the runs of that source. A build reuses nothing from earlier runs; `cint run` may instead start the executable that an earlier run kept (5.4, paragraph 12). The program runs as a child process whose standard input is the null device, unless the grants of the run include `console.input`: then the program reads the standard input of `cint` (5.4). Before it starts the host C compiler, `cint` removes the environment variables `CL`, `_CL_`, `LINK`, `_LINK_` and `CCC_OVERRIDE_OPTIONS`, which would change the compiler's flags. The compiler's own output is kept in the file `cc.log` of that directory and never reaches standard output; when the compiler rejects or warns on the emitted C, `cint` exits with status 7 and its message names the compiler, its exit status and the log.
4. *Defaults.* Each entry that `cint` starts runs with the following semantic inputs, which a receipt records (clause 12). The frame arena is the capacity of SPEC-04P.4 5.2.6, counted in elements. A receipt records it as `frame_arena_bytes`, 16,777,216, eight bytes per element and a recording as `frame-arena 16777216`.

| Subcommand | Fuel allowance | Call-depth limit | Frame arena | Executor |
| --- | --- | --- | --- | --- |
| `cint run` | none | 256 | 2,097,152 elements | `cpu-c17` |
| `cint test` | 1,000,000,000 per test | 256 | 2,097,152 elements | `cpu-c17` |

5. *Receipts.* With `--receipt PATH`, `cint` writes a receipt to `PATH` after the run, the build or the tests, and, without `--json`, writes the line `receipt: PATH` to standard error. Without `--receipt` no receipt is written; there is no default destination. A receipt is written for the exit statuses 0, 1, 2, 3 and 6.

```console
$ cint run --receipt hello.json hello.ci
hello, world
answer=42
receipt: hello.json
$ cint run --json --receipt hello.json hello.ci
hello, world
answer=42
```

<a id="cint-run"></a>

### 5.4 `cint run`

`cint run FILE.ci` translates the program whose root module is `FILE.ci`, with every module it imports, builds it with the host C compiler, and runs it once on the executor `cpu-c17`.

1. The root module shall have a program: top-level statements, which make it a script, or a function `main`. The script's statements, or `main`, run as one entry with the defaults of 5.3.
2. A root module with neither is an importable module, and it has no program to run. `cint run` translates it, reports a usage error that names `cint test`, and exits with status 4; nothing runs.
3. When the program ends normally, standard output holds what it printed and the exit status is 0.
4. When the program faults, standard output holds the bytes it wrote before the fault, with no byte added, standard error holds the fault report of 7.3, and the exit status is 1.
5. When translation rejects the program, standard error holds the diagnostics of 7.1, nothing runs, standard output is empty, and the exit status is 2.
6. When `main` returns an error, standard output holds what the program printed, standard error holds one line `error: SET.VALUE (tag N)` (7.5), and the exit status is 6 [SPEC-04P.4 LS-221].
7. `cint run` takes one file. A project directory is not a program for `cint run`; `cint build` (5.5) builds one.
8. <a id="grants-of-a-run"></a>Each `--grant LINE` adds one grant to the program's grant set (SPEC-04P.4 6.9.9). `LINE` is a capability text, the name of the capability, a colon and its arguments separated by commas, such as `fs.read:/data`, or the name alone for a capability with no argument, such as `console.input`. For a capability whose argument is a path, the text is followed by `=` and the directory of the host that the root maps to, relative to the current directory unless it is absolute: `fs.read:/data=data`. Without `--grant`, the grants come from the program itself (paragraph 11). A service call that no grant covers returns `denied`.
9. The lines are checked when the program starts, before any of its code runs. A line is refused when its capability name, the text before its first colon, is empty, is longer than 63 characters or holds a character other than a letter, a digit, `_`, `-` and `.`; when it names `fs.read` or `fs.write` without a logical path after the colon or without `=` and a directory; when it holds `=` and names another capability; and when its directory cannot be opened. A refused line ends the run with exit status 4 and the message "a --grant line is one the runtime does not take, or names a directory it cannot open", which names the file where the program's own message is kept. Every other line is accepted: a grant that the program does not require, or whose name no standard service checks, covers no call.
10. A line that names `console.input` gives the program the standard input of `cint`, from which `console.read` and `console.read_line` read.
11. <a id="manifest-grants"></a>*Grants from the program.* An executable that `cint run` starts knows the capabilities that its program declares with `requires` (SPEC-04P.4 6.9.9, paragraph 7). When no `--grant` line is given, `cint run` grants from them. A capability that names no path root, such as `console.input`, is granted. A `fs.read` or `fs.write` capability on the root `/argK`, where K is a decimal number from 1 with no leading zero, is granted when program argument K is a path whose last part is a logical segment (SPEC-04P.4 6.9.9, paragraph 12): the directory of that path backs the root, and argument K reaches the program as `/argK/<last part>`. Nothing else is granted, since nothing names any other host directory. The whole directory is granted, not only the file named. A run that writes a receipt or a width report does not take `console.input` this way, so that its standard input is the null device and its receipt can be repeated. One `--grant` line turns the rule off for the run, and the lines given are then the whole grant set.
12. <a id="run-cache"></a>*The kept executable.* Unless `--receipt` or `--width-report` is given, `cint run` keeps the executable it compiled, and a later run of the same program starts it without compiling. The program is the same when the `cint` executable, the toolchain file, the runtime and every module file the run read are the same, an import path that no file answered included. The executable is kept as `CACHE/exe/KEY/program` (`program.exe` on the `msvc` toolchain) beside its SHA-256 digest, where `KEY` is the SHA-256 digest of those inputs. An entry that is missing, incomplete or altered is replaced by compiling. `--rebuild` compiles anyway and replaces the entry. The grants, the program arguments and the frame arena are not inputs: they reach the program on its command line. A run with `--receipt` or `--width-report` always compiles and never reads the entry, so its receipt and its report are as they were. `cint build` and `cint test` do not read or write it.
13. `--frame-arena N` sets the frame-arena capacity of the program to `N` elements (SPEC-04P.4 5.2.6), in place of 2,097,152. `N` is decimal digits with no sign and no leading zero, from 1 to 1,125,899,906,842,623 (2^50 - 1), so that the receipt's `frame_arena_bytes`, eight to an element, is an exact JSON number. Any other text, or the option given twice, is a usage error, exit status 4.

Examples

14. A program that ends normally:

```cint hello.ci
"hello, world\n";
I64 answer = 6 * 7;
"answer={answer}\n";
```

```text
hello, world
answer=42
```

15. A program that faults after it printed one line. Its report names the operation, both operands, the exact result and the bound it crossed:

```cint partial.ci
I64 big = I64.max;
"partial: big={big}\n";
I64 r = big + 1;
"{r}\n";
```

```text
partial: big=9223372036854775807
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> partial.ci:3:13
   |
 3 | I64 r = big + 1;
   |             ^ add.checked.i64
   |
   = operation: add.checked.i64
   = operand-count: 2
   = operand: I64 9223372036854775807
   = operand: I64 1
   = exact:   9223372036854775808
   = limit:   I64 9223372036854775807
   ...
```

16. The same program as a transcript, with its exit status:

```console
$ cint run partial.ci
partial: big=9223372036854775807
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> partial.ci:3:13
   |
 3 | I64 r = big + 1;
   |             ^ add.checked.i64
   |
   = operation: add.checked.i64
   = operand-count: 2
   = operand: I64 9223372036854775807
   = operand: I64 1
   = exact:   9223372036854775808
   = limit:   I64 9223372036854775807
   = revision: 2a5d25c827694f7bef7e0477ec0d760c95a7be976beaf854285a43555aeedef8
   = source-map: none
   = address: none
   = stack-depth: 0
   = fuel-consumed: 1 (not part of the fault record)
   = state: the program stopped; stdout holds what it wrote before the fault
$ echo $?
1
```

17. A `main` that returns an error:

```console
$ cat ledger.ci
error LedgerError { overdrawn, frozen }

LedgerError!I64 withdraw(I64 balance, I64 amount) {
    if (amount > balance) {
        return .overdrawn;
    }
    return balance - amount;
}

LedgerError!void main() {
    I64 b = try withdraw(100, 30);
    "left: {b}\n";
    I64 c = try withdraw(b, 500);
    "left: {c}\n";
}
$ cint run ledger.ci
left: 70
error: ledger.LedgerError.overdrawn (tag 1)
$ echo $?
6
```

18. A module without a program, and a command line without a file:

```console
$ cint run prices.ci
...
cint: error: the root module has no main and no script statements, so there is no program; `cint test` runs its tests
$ echo $?
4
$ cint --json run
{"category":"usage","exit_status":4,"message":"no FILE.ci given","schema":"CINT-TOOL-1"}
$ echo $?
4
```

19. Grants: the program `reads.ci` of SPEC-04P.4 6.9.9 reads `/data/greeting.txt`, which a grant maps to `data/greeting.txt`. With no grant its read returns `denied`, and with a line that names no directory the run does not start:

```console
$ cint run --grant fs.read:/data=data reads.ci
read 12 bytes: 104 101
$ cint run reads.ci
error: fs.FsError.denied (tag 2)
$ echo $?
6
$ cint run --grant fs.read:/data reads.ci
cint: error: a --grant line is one the runtime does not take, or names a directory it cannot open; see .../program.stderr
$ echo $?
4
```

20. Console input: `echo_line.ci` requires `console.input`, which `cint run` grants from the program when it is given no `--grant` line. A `--grant` line for another capability makes the lines given the whole grant set, and the console is then denied:

```console
$ cat echo_line.ci
import console;
requires console.input();

console.ConsoleError!void main(Grants g) {
    U8[80] line;
    I64 n = try console.read_line(g, line);
    "got {n} bytes, first {line[0]}\n";
}
$ echo hello | cint run echo_line.ci
got 5 bytes, first 104
$ echo hello | cint run --grant fs.read:/data=data echo_line.ci
error: console.ConsoleError.denied (tag 1)
```

21. Grants from an argument: `notes.ci` requires `fs.read` and `fs.write` on the root `/arg1`, so `cint run` grants it the directory of its first argument, and the program reads and writes the file there as `/arg1/today.txt`. Run with no argument, the root is not granted. Run with a `--grant` line that grants only `fs.read`, the lines given are the whole grant set, and the write is denied:

```cint notes.ci
import fs;
requires fs.read("/arg1");
requires fs.write("/arg1");

fs.FsError!void main(Grants g, in U8[_] args) {
    U8[64] buf;
    I64 n = try fs.read(g, "/arg1/today.txt", 0, buf);
    "read {n} bytes\n";
    try fs.put(g, "/arg1/today.txt", b"hello\n");
    return;
}
```

```console
$ mkdir notes && echo hi > notes/today.txt
$ cint run notes.ci -- notes/today.txt
read 3 bytes
$ cat notes/today.txt
hello
$ cint run notes.ci
error: fs.FsError.denied (tag 2)
$ cint run --grant fs.read:/arg1=notes notes.ci -- notes/today.txt
read 6 bytes
error: fs.FsError.denied (tag 2)
```

Note

22. The fault report after a run of `cint` is written by `cint` from the fault record that the program process hands back; the program process itself writes no report. An executable written by `cint build` (5.5) that is given no destination for its fault record writes one line in place of the report, as 5.5, paragraph 4 states.

<a id="cint-build"></a>

### 5.5 `cint build`

`cint build` translates a program and writes it as an executable, or with `--lib` as a shared library, that runs without `cint`.

1. `cint build FILE.ci` writes an executable named after the root module, without `.ci`, into `--out DIR`, by default the directory `build` under the root. On the `msvc` toolchain its name ends in `.exe`. The tool writes `built: PATH` to standard error.
2. `cint build DIR` builds the project in the directory `DIR`: its root is `DIR/src` when that directory exists, and `DIR` otherwise, and its root module is `main.ci` there. `--out` defaults to `DIR/build`. A project directory takes no `--root`. `cint build` with no file or directory builds the project in the current directory.
3. With `--lib`, the root module shall be an importable module, with no `main` and no top-level statements, and the tool writes a shared library, `lib<name>.so` on Linux, `lib<name>.dylib` on macOS and `<name>.dll` on Windows. `--lib` on a script or a module with `main` is a usage error, exit status 4. Two builds of one library from the same source with the same toolchain write the same bytes; on macOS the library's install name is `@rpath/lib<name>.dylib`. Without `--lib`, a root module with no program is the usage error of 5.4, paragraph 2.
4. An executable built by `cint build` runs the program as `cint run` does, with the same defaults, and exits with status 0, 1 or 6. Its arguments are the paths to which it writes the fault record and the record of fuel consumed (SPEC-01P.3 6.5), in that order, `-` for none, then, for a program with module variables, the path for their values (8.2, paragraph 2), and any number of `--grant LINE` pairs, in any position, which it takes as `cint run` does (5.4, paragraphs 8 to 11); a line it does not take ends it with exit status 4 before the program starts. Its program arguments are the word after each `--arg`, in any position, and every word after `--`, in order (SPEC-04P.4 LS-221); `cint run` passes the words after its own `--` as `--arg` pairs. An executable whose `main` takes no program arguments, given one, exits with status 4 before the program starts. Given no path for the fault record, it writes, for a fault, one line to standard error that names the fault code, the operation and the position, such as `fault[E_OVERFLOW]: add.checked.i64 at partial.ci:3:13 (pass a file path as argv[1] for the full fault record)`, and for an error result of `main` the line of 7.5. It writes no fault report.
5. `cint build --frame-arena N` (5.4) writes an executable that runs with that capacity: its `main` puts `--frame-arena N` before the arguments it is given, so a `--frame-arena` on the executable's own command line replaces it. With `--lib` the option is a usage error, exit status 4, since the host of a library sets the capacity when it creates a context (SPEC-03P.3 7.2). `cint build --app` writes an executable that takes every word on its command line after its name, in order, as a program argument of `main` (SPEC-04P.4 LS-221), as `cint run` takes the words after `--`; so `ced notes.txt` gives `main` the word `notes.txt`, with no launcher. Its `main` puts `--` before the words it is given, after `--frame-arena N` when that is given too, so a `--grant`, `--arg`, `--frame-arena`, `--` or `-` on the executable's command line is a program argument as well. The executable takes no record destination: its grants are those of its manifest (5.4, paragraph 11), and a fault or an error result writes the one line of paragraph 4 to standard error. An executable whose `main` takes no program arguments, given a word, exits with status 4 before the program starts. `--app` with `--lib` is a usage error, exit status 4, since a library has no command line. A build receipt's `method` does not record `--app`.
6. The exit status of `cint build` is 0 when the file was written, 2 for a diagnostic and 4, 5 or 7 for a tool error.

Example

7. A script, a project with an imported module and a library:

```console
$ cint build hello.ci
built: ./build/hello
$ ./build/hello
hello, world
answer=42
$ cint build partial.ci
built: ./build/partial
$ ./build/partial
partial: big=9223372036854775807
fault[E_OVERFLOW]: add.checked.i64 at partial.ci:3:13 (pass a file path as argv[1] for the full fault record)
$ cint build shop
built: shop/build/main
$ shop/build/main
300 cents
$ cint build --lib --out lib shop/src/util/money.ci
built: lib/libmoney.so
```

The project `shop` holds `src/main.ci`, which imports `util.money` and calls `money.cents(3)`, and `src/util/money.ci`, which exports `cents`.

<a id="cint-test"></a>

### 5.6 `cint test`

`cint test` runs the `test` blocks of a module and reports which passed. Tests are part of the language, and SPEC-04P.4 specifies what passes and what fails; this section specifies how the tool runs them and what it prints.

1. `cint test FILE.ci` runs the tests of `FILE.ci` and of every module it imports. `cint test DIR` runs those of the project in `DIR`, as 5.5 locates it. Modules are taken in the byte order of their paths, and the tests of a module in source order [SPEC-04P.4 LS-234].
2. Each test runs in a process of its own, from a fresh context with fresh module state, with a fuel allowance of 1,000,000,000 and the other defaults of 5.3. The top-level statements of a script do not run under `cint test` [SPEC-04P.4 LS-234, LS-239].
3. A test without `expect_fault` passes when its body completes. It fails on a fault, including a failed `assert`, and when an error leaves it through `try` [SPEC-04P.4 LS-235, LS-315].
4. A test with the header `expect_fault CODE` passes only when its body faults with the fault code `CODE`. With `expect_fault CODE at LINE`, the fault shall also be on line `LINE`. The test fails when the body completes, when an error leaves it, or when it faults with another code or on another line [SPEC-04P.4 LS-236, LS-240].
5. For each test, standard output receives one line, `test FILE:LINE "NAME" ... ok` or `... FAILED`, where `LINE` is the line of the `test` keyword. After the last test, one line gives the count, `K of N tests passed`.
6. The reason for each failure goes to standard error after the test's line: the fault report of 7.3, whose `state` line reads "the test stopped; its output is not shown"; for an expected fault that did not come, `  expected fault CODE; the test completed`; for a fault with another code or on another line, the report and then `  expected fault CODE at line LINE`; and for an error that left the test, the line of 7.5 [SPEC-04P.4 LS-315]. A test's own prints are not shown.
7. The exit status is 0 when every test passed, 3 when any failed and 2 when translation rejected a module.
8. With `--json`, standard output receives one `CINT-TEST-1` object per test (7.6) in place of the text lines, and standard error receives nothing for a failed test.

Example

9. `prices.ci` holds a function and three tests; the third asserts a wrong total:

```console
$ cat prices.ci
export I64 subtotal[n](in I64[n] c) {
    return sum(c);
}

test "subtotal of three prices" {
    I64[3] cents = [1999, 425, 10];
    assert(subtotal(cents) == 2434);
}

test "checked add overflows" expect_fault E_OVERFLOW at 13 {
    I64 big = 9223372036854775800;
    I64 more = 12;
    I64 r = big + more;
}

test "a wrong total" {
    I64[2] cents = [1, 2];
    assert(subtotal(cents) == 4);
}
$ cint test prices.ci
test prices.ci:5 "subtotal of three prices" ... ok
test prices.ci:10 "checked add overflows" ... ok
test prices.ci:16 "a wrong total" ... FAILED
fault[E_ASSERT]: assertion failed
  --> prices.ci:18:12
   |
18 |     assert(subtotal(cents) == 4);
   |            ^ assert.checked.bool
   |
   = operation: assert.checked.bool
   = operand-count: 2
   = operand: I64 3
   = operand: I64 4
   = exact:   none
   = limit:   none
   = revision: cec1064de267a83b4fa100a3396ea2b07101a24b452792c5021254e2d115c791
   = source-map: none
   = address: none
   = stack-depth: 0
   = fuel-consumed: 3 (not part of the fault record)
   = state: the test stopped; its output is not shown
2 of 3 tests passed
$ echo $?
3
$ cint test --json prices.ci
{"expect_fault":null,"expect_line":null,"fault":null,"file":"prices.ci","fuel_consumed":"3","line":5,"name":"subtotal of three prices","passed":true,"schema":"CINT-TEST-1"}
{"expect_fault":"E_OVERFLOW","expect_line":13,"fault":{"address":null,"code":"E_OVERFLOW","column":17,"exact":{"t":"Z","v":"9223372036854775812"},"file":"prices.ci","limit":{"t":"I64","v":"9223372036854775807"},"line":13,"operands":[{"t":"I64","v":"9223372036854775800"},{"t":"I64","v":"12"}],"operation":"add.checked.i64","revision":"cec1064de267a83b4fa100a3396ea2b07101a24b452792c5021254e2d115c791","schema":"CINT-FAULT-1","severity":"fault","source_map":null,"stack":[]},"file":"prices.ci","fuel_consumed":"1","line":10,"name":"checked add overflows","passed":true,"schema":"CINT-TEST-1"}
{"expect_fault":null,"expect_line":null,"fault":{"address":null,"code":"E_ASSERT","column":12,"exact":null,"file":"prices.ci","limit":null,"line":18,"operands":[{"t":"I64","v":"3"},{"t":"I64","v":"4"}],"operation":"assert.checked.bool","revision":"cec1064de267a83b4fa100a3396ea2b07101a24b452792c5021254e2d115c791","schema":"CINT-FAULT-1","severity":"fault","source_map":null,"stack":[]},"file":"prices.ci","fuel_consumed":"3","line":16,"name":"a wrong total","passed":false,"schema":"CINT-TEST-1"}
```

<a id="cint-emit-c"></a>

### 5.7 `cint emit-c`

`cint emit-c` writes the output set of a program: the C that `cint run` would compile, and the intermediate representation that `cint interp` runs. It compiles nothing and runs nothing.

1. `cint emit-c --root DIR --out OUT MODULE.ci...` takes the module paths of the program under `DIR`, each module after every module it imports, with the root module last. It does not follow imports itself: a module named before a module it imports is rejected with a diagnostic, exit status 2. A module of the standard library root (5.3) that the program imports is the exception: it is added to the output set without being named. `--root` and `--out` are required, and `--receipt` and `--cache` are not taken.
2. The output set goes into a new directory `OUT.stage-N`, where `N` is 0 for the first output set and goes up by one with each output set after it; the directory of the last output set is then removed. `OUT` itself receives one file, `MANIFEST.ref`, a line with the name of the current output set's directory and the SHA-256 digest of its manifest.
3. The output set holds, for each module `M.ci`, the C file `M.c` and the text `M.sites`, which is the module's semantic intermediate representation (SIR) as text, with its table of source positions; the program's C, `cint-program.c`, and its list of modules, `cint-program.sites`; and `MANIFEST`. The manifest's first line is `cint-manifest-1`, and each further line gives a file's SHA-256 digest, its size in bytes and its name.
4. `cint emit-c` writes nothing to standard output. Its exit status is 0, 2 for a diagnostic or 4, 5 or 7.

Example

5. Two output sets of the same program:

```console
$ cint emit-c --root . --out partial-out partial.ci
$ ls partial-out partial-out.stage-0
partial-out:
MANIFEST.ref

partial-out.stage-0:
MANIFEST
cint-program.c
cint-program.sites
partial.c
partial.sites
$ cat partial-out/MANIFEST.ref
partial-out.stage-0 11425119e58fec71a9bc56e24d9149748a51404304d39434ea9368b9a7feb048
$ cat partial-out.stage-0/MANIFEST
cint-manifest-1
aaa6ca726178734f0106cb4db3a5b7815815a8905b8191847c0ce3a9e3dc254a 1342 cint-program.c
20e6fd5b36ae765ab532d37eda6e49db797ca2d6991bf986ef97c3dca199f12b 20 cint-program.sites
dfd5099400ef9f0329b507931706de2dbab83373ab661826e00805e59c70b63a 2223 partial.c
b6dd6dc4d37dd2d3ac5af2d065a795cab0db8d03b749c7cd949a1eed8af6e81a 748 partial.sites
$ cint emit-c --root . --out partial-out partial.ci
$ ls -d partial-out*
partial-out
partial-out.stage-1
```

The digests in `MANIFEST` are those of the emitted files; the sizes are in bytes.

<a id="the-width-report"></a>

### 5.8 The width report

Every operation on a wide integer type is checked at its declared width, so a program that holds its values in `I1024` runs correctly whatever bits they use. The width report says how many bits each such operation reached in one run, so that a program can choose narrower types where the run shows they suffice.

1. `cint run --width-report PATH FILE.ci` runs the program as `cint run` does and writes its width report to `PATH`. `cint build --width-report` writes an executable that writes its width report to standard error when the program ends, before any fault line or error line.
2. A report covers each checked operation on the types `I128` to `I1024` and `U128` to `U512`: the arithmetic operators, the shifts, the built-in operations, `muldiv`, the rotations and the conversion `as`. For each, the run notes the operands and the result, and the report gives the largest width that any of them took: for a signed type, the bits of the two's-complement value, its sign bit included, and for an unsigned type its bit length. A conversion takes the width of its source, or of its target when the source is not a wide type. Wrapping, saturating and result forms, comparisons, reductions, fixed point and kernels are not covered, and neither is a run of `cint interp`.
3. The report is text. Its first line is `CINT-WIDTH-1` and its last line `end`. Between them is one line for each covered operation, in the order of the program's modules and then of the operation's site: `site PATH:LINE:COLUMN OPERATION hits N bits B of W fits T` for an operation that ran `N` times and reached `B` of the `W` bits of its type, where `T` is the narrowest of `I8` to `I1024`, or of `U8` to `U512`, that holds `B` bits; or `unreached PATH:LINE:COLUMN OPERATION` for one that did not run.
4. The report describes one run and promises nothing about another: a type chosen from it is checked as every type is, and a value that does not fit faults.

Example

5. A program whose `I128` products reach 41 and 81 bits:

```console
$ cat wide_sum.ci
I128 a = 1000000;
I128 b = a * a;
I128 c = b * b;
"{c}\n";
$ cint run --width-report w.txt wide_sum.ci
1000000000000000000000000
$ cat w.txt
CINT-WIDTH-1
site wide_sum.ci:2:12 mul.checked.i128 hits 1 bits 41 of 128 fits I64
site wide_sum.ci:3:12 mul.checked.i128 hits 1 bits 81 of 128 fits I128
end
```

<a id="identities-as-the-tools-report-them"></a>

## 6 Identities as the tools report them

The tools name programs, files and states by identities: SHA-256 digests of a defined encoding of what they name. An identity lets two runs, two machines or two executors be compared by comparing 64 hexadecimal digits. SPEC-01P.3 specifies the encoding behind each identity; this clause lists what each one names and where the tools print it.

1. Every identity is a SHA-256 digest, written as 64 lowercase hexadecimal digits.
2. The *revision identity* of a program is the digest of its meaning that SPEC-01P.3 9.1 lays out, over the profile, the runtime contract version and the SIR of each module without its source positions (SPEC-09P.3 6.5), so moving code to other lines leaves it unchanged [SPEC-01P.3 IM-157] [SPEC-09P.3 SIR-17].
3. The *runtime contract version*, `cint-rt-3` in cint 0.2.4, names the interface and the meaning of the runtime that emitted code calls, and it is an input of the revision identity, as SPEC-09P.3 7.10 states [SPEC-09P.3 RCPT-08].
4. The *source-map identity* is the digest of the table that maps each site of the program, each operation that can fault, to its module, line and position in the line. Two programs with the same revision identity and different layouts have different source-map identities.
5. The same program has the same revision identity under every executor and every tool. The `revision` line of a fault report from `cint run`, the `revision` printed by `cint interp identity`, and the revision of a recording of that program are equal.
6. The canonical fault record has a field for the source-map identity, and cint 0.2.4 writes none there: the `source-map` line of a fault report reads `none`, and the `source_map` key of `CINT-FAULT-1` is `null`. The source-map identity of a program is printed by `cint interp identity`, and it is held by its recordings and cases and named in their receipts.
7. The *execution identity* of an entry is formed from the profile, the fuel model, the revision identity, the source-map identity, the entry, its arguments, the state it starts from, the effects supplied to it, its fuel allowance, its call-depth limit and its frame-arena capacity, as SPEC-04P.4 8.2 states [SPEC-04P.4 IM-159]. No tool of cint 0.2.4 prints it; a recording holds each of its inputs (11.2).
8. The other identities that the tools print:

| Identity | What it names | Where it is printed |
| --- | --- | --- |
| State hash | The canonical state of a context at a boundary | `state` of `cint debug replay` (10.3); `initial-state`, `state` and `state-hash` of `cint interp show` (11); `outcome.state_sha256` of a replay receipt; its first 8 digits in the workbench's `:effects` |
| Chain head | Every boundary of a recording, through the digest chain of 11.2 | `chain` of `cint debug replay`; the last line of `cint interp replay`; `outcome.chain_head` of a replay receipt |
| File digest | The bytes of one file | each line of an output set's `MANIFEST`, and its digest in `MANIFEST.ref` (5.7); `source.files`, `generated.files` and `runtime.files` of a receipt; `case.sha256` and `recording.sha256` of a replay or diff receipt; `outcome.stdout.sha256` of a run receipt |
| Compiler source identity | The source files of the compiler | `identity.tool.compiler_source_identity` of a run, build or test receipt (12.3) |
| Receipt identity | The `identity` and `outcome` of a receipt | `observations.receipt_identity_sha256` of every receipt (12.1) |

9. The workbench also shows short forms, which are displays and not identities: `session@r3` names the third revision of the session's own module and `tally@r1` the first revision of a loaded module `tally`, counted within one session, and a build on `cpu-c17` names a revision by the first 8 digits of its identity.

Example

10. The output set of `partial.ci` (5.7), and that of the same program with one blank line added at its top, saved as `moved/partial.ci`. The revision identities are equal and the source-map identities differ:

```console
$ cint interp identity partial-out.stage-1
revision 2a5d25c827694f7bef7e0477ec0d760c95a7be976beaf854285a43555aeedef8
source-map be83426b1103002101ac0403acbc26b062cb5b2128a72bd55a423f9911c2a12b
$ cint emit-c --root moved --out moved-out partial.ci
$ cint interp identity moved-out.stage-0
revision 2a5d25c827694f7bef7e0477ec0d760c95a7be976beaf854285a43555aeedef8
source-map 9a20cbc476ac7c7ef8e65149165eca8c74b2ed0287e36b09c732531c0f2b8388
```

The revision is the one `cint run partial.ci` printed in 5.4, paragraph 10.

<a id="diagnostics-fault-reports-and-other-messages"></a>

## 7 Diagnostics, fault reports and other messages

Every tool other than the interpreter reports the same things in the same two forms; the interpreter has no machine form (7.7). The text form is laid out for reading in a terminal. The machine form, selected by `--json`, is one canonical JSON object per line, with the keys sorted, no whitespace and every integer that a program computes written as a typed exact decimal string, `{"t":"I64","v":"12"}`, so that a reader that holds JSON numbers as binary floating point cannot change it [SPEC-04P.4 LS-289]. Each object names its format in its `schema` key.

<a id="diagnostics-text-form"></a>

### 7.1 Diagnostics: text form

A diagnostic reports a program that translation rejects. Its layout is the one fault reports share: a first line with the code and the message, the position and the source line with a caret under the position [SPEC-04P.4 LS-276].

1. The layout is:

```text
error[CODE]: MESSAGE
  --> FILE:LINE:COLUMN
   |
 N | SOURCE LINE
   |         ^
   |
   = note: TEXT
```

`CODE` is the letter `C` and four digits; Annex B of SPEC-04P.4 lists every code [SPEC-04P.4 LS-283]. The severity is always `error`: the language has no warnings [SPEC-04P.4 LS-277]. A `note` line is present only where the diagnostic carries a note.
2. The position is the first character of the offending construct, and the caret marks it. Lines and positions in a line count from 1, and a position in a line counts Unicode scalar values. A tab in the source line is repeated at the same place in the caret line, so the caret stays aligned at any tab width [SPEC-04P.4 LS-278, LS-279].
3. Diagnostics are reported in the order of file, line, position in the line and code. `cint` stops at the first error it finds and reports the least diagnostic, in that order, among the errors of the earliest phase that finds one [SPEC-04P.4 LS-282].
4. In the workbench, `FILE` is `entry N` for the text of entry `N`, and `inspect` for the expression of `:inspect` or `:type`.

Examples

5. A translation error:

```cint widen.ci
I32 a = 1;
I64 b = a;
"{b}\n";
```

```text
error[C2001]: type mismatch
  --> widen.ci:2:9
   |
 2 | I64 b = a;
   |         ^
```

6. A diagnostic with a note, in both forms:

```console
$ cint run note.ci
error[C3004]: script local used in a function or test
  --> note.ci:3:16
   |
 3 |     return x * limit;
   |                ^
   |
   = note: this module is a script because of the top-level statement at note.ci:5:1
$ cint run --json note.ci
{"code":"C3004","column":16,"file":"note.ci","line":3,"message":"script local used in a function or test","notes":[{"column":1,"file":"note.ci","line":5,"text":"this module is a script because of the top-level statement at note.ci:5:1"}],"schema":"CINT-DIAG-1","severity":"error"}
```

<a id="diagnostics-machine-form"></a>

### 7.2 Diagnostics: machine form

1. Under `--json`, each diagnostic is one `CINT-DIAG-1` object on its own line of standard error, in the order of the text form [SPEC-04P.4 LS-289]. Its keys:

| Key | Value |
| --- | --- |
| `schema` | `"CINT-DIAG-1"` |
| `severity` | `"error"` |
| `code` | The code, such as `"C2001"`. |
| `message` | The message of the text form. |
| `file`, `line`, `column` | The position, with `line` and `column` as JSON integers. |
| `notes` | Present only when the diagnostic has notes: a list of objects with the keys `file`, `line`, `column` and `text`. |
| `fault` | Present only for `C6001`, a fault during the evaluation of a constant, and `C6002`, the exhaustion of its fuel: the `CINT-FAULT-1` object of that fault. |

<a id="fault-reports-text-form"></a>

### 7.3 Fault reports: text form

A fault report describes a fault: which operation stopped the program, on which operands, with which exact result, and where. Every tool prints it in the same layout [SPEC-04P.4 LS-276, LS-284].

1. The report begins with `fault[CODE]: TITLE`, then the position `  --> FILE:LINE:COLUMN` and the source line, with a caret under the operator token of the faulting operation and the operation identifier beside it [SPEC-04P.4 LS-278]. The titles:

| Code | Title |
| --- | --- |
| `E_OVERFLOW` | the exact result is outside the type's range |
| `E_DIV_ZERO` | division by zero |
| `E_BOUNDS` | index out of bounds |
| `E_SHAPE` | shapes do not match |
| `E_SHIFT` | shift count outside 0..w-1 |
| `E_NARROW` | the value does not fit the destination type |
| `E_ALIAS` | views overlap |
| `E_STALE_HANDLE` | stale handle |
| `E_FUEL` | fuel allowance exhausted |
| `E_UNSUPPORTED` | not supported by this backend |
| `E_DOMAIN` | operand outside the operation's domain |
| `E_DEPTH` | call depth limit exceeded |
| `E_ASSERT` | assertion failed |

2. Then come the fields of the fault record, one per line, in this order, as SPEC-01P.3 6.1 encodes them [SPEC-01P.3 IM-149]:
   - `operation`: the operation identifier;
   - `operand-count`, then one `operand` line per operand, with its type and its exact value;
   - `exact`: the exact result, or `none` where the fault has none;
   - `limit`: the bound that the result crossed, with its type, or `none` [SPEC-04P.4 LS-280];
   - `revision`: the revision identity of the program (clause 6);
   - `source-map`: the field of the fault record that holds the program's source-map identity; cint 0.2.4 writes none there, so the line reads `none` (6, paragraph 6);
   - `address`: for a fault inside a kernel dispatch, the kernel, the dispatch number counted from 0 within the entry, the phase (0 entry, 1 work-item, 2 epilogue), the work-item index and the step ordinal; `none` otherwise;
   - `stack-depth`, then one `called from: FILE:LINE:COLUMN` line per active call, outermost first.
3. Two lines follow that are not part of the record: `fuel-consumed: N (not part of the fault record)`, the fuel the entry had consumed and `state:`, a fixed sentence that says what the fault left. After `cint run` it reads "the program stopped; stdout holds what it wrote before the fault", and after `cint test` "the test stopped; its output is not shown". In the workbench and `cint debug` it says which state the session holds after the fault.
4. Every value is printed exactly, in decimal, with its type. A value is never abbreviated, rounded or written with an exponent [SPEC-04P.4 LS-280].
5. The workbench and `cint debug` add lines after `= noncanonical: not part of the fault record`: `stack-source`, the source line of each active call; `workbench-position`, the entry or boundary; `site`, the site identifier that `cint explain` prints (9.9); and `backend`, the executor that found the fault and whether it found it by replay.

Examples

6. A fault inside a kernel dispatch, with its address and the two calls that led to it:

```cint heat.ci
kernel advance[n](in I64[n] old, in I64[n] increment, out I64[n] next) {
    next = old + increment;
}

I64 run() {
    I64[3] a = [0, 9223372036854775806, 0];
    I64[3] d = [1, 5, -1];
    I64[3] b;
    advance(a, d, b);
    return b[0];
}

"{run()}\n";
```

```text
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> heat.ci:2:16
   |
 2 |     next = old + increment;
   |                ^ add.checked.i64
   |
   = operation: add.checked.i64
   = operand-count: 2
   = operand: I64 9223372036854775806
   = operand: I64 5
   = exact:   9223372036854775811
   = limit:   I64 9223372036854775807
   ...
   = address: heat.advance, dispatch 0, phase 1, work-item 1, step 3
   = stack-depth: 2
   = called from: heat.ci:13:3
   = called from: heat.ci:9:5
   ...
```

<a id="fault-reports-machine-form"></a>

### 7.4 Fault reports: machine form

1. Under `--json`, `cint run` writes the fault as one `CINT-FAULT-1` object on standard error, after the bytes the program wrote to standard output. Its keys:

| Key | Value |
| --- | --- |
| `schema` | `"CINT-FAULT-1"` |
| `severity` | `"fault"` |
| `code`, `operation` | The fault code and the operation identifier. |
| `file`, `line`, `column` | The position. |
| `operands` | A list of typed values, `{"t":TYPE,"v":DECIMAL}`. |
| `exact` | The exact result as a typed value of type `Z`, the integers or `null`. |
| `limit` | The bound crossed, as a typed value, or `null`. |
| `revision` | The revision identity. |
| `source_map` | `null` in cint 0.2.4. |
| `address` | `null`, or an object with the keys `kernel`, `dispatch`, `phase`, `work_item` and `step`; `phase` is a JSON integer and the others are strings. |
| `stack` | The active calls, outermost first, each an object with `file`, `line` and `column`. |
| `noncanonical` | The values that are not part of the record: `fuel_consumed`, a decimal string; from `cint debug`, also `backend`, `site` and `workbench_position`, the lines of 7.3, paragraph 5. |

2. A fault report gives positions, not the extent of the offending expression or the names of functions. The machine form has no `span` key, and the entries of `stack` have no `function` key. In the text form the caret marks one position, and a `called from` line gives a position.
3. The `fault` of a `CINT-TEST-1` object (7.6) and of a `CINT-DIAG-1` object has the same keys without `noncanonical`.

Example

4. The fault of 5.4, paragraph 9, in machine form. Standard output still holds the line the program printed:

```console
$ cint run --json partial.ci
partial: big=9223372036854775807
{"address":null,"code":"E_OVERFLOW","column":13,"exact":{"t":"Z","v":"9223372036854775808"},"file":"partial.ci","limit":{"t":"I64","v":"9223372036854775807"},"line":3,"noncanonical":{"fuel_consumed":"1"},"operands":[{"t":"I64","v":"9223372036854775807"},{"t":"I64","v":"1"}],"operation":"add.checked.i64","revision":"2a5d25c827694f7bef7e0477ec0d760c95a7be976beaf854285a43555aeedef8","schema":"CINT-FAULT-1","severity":"fault","source_map":null,"stack":[]}
```

<a id="error-results"></a>

### 7.5 Error results

1. When `main` returns an error, `cint run` writes one line to standard error, `error: SET.VALUE (tag N)`. `SET` is the error set of `main`'s result, qualified by the module that declares it, `VALUE` is the name of the value, and `N` is its tag in that set [SPEC-04P.4 LS-221].
2. Under `--json` the line is one `CINT-ERROR-1` object with the keys `schema` (`"CINT-ERROR-1"`), `set`, `value` and `tag`, a typed value of type `U16`.
3. The same line reports an error that leaves a test (5.6) and the error result of a call made by the workbench's `:run` (9.4).

Example

4. The error result of `ledger.ci` (5.4, paragraph 11) in machine form:

```console
$ cint run --json ledger.ci
left: 70
{"schema":"CINT-ERROR-1","set":"ledger.LedgerError","tag":{"t":"U16","v":"1"},"value":"overdrawn"}
```

<a id="test-results"></a>

### 7.6 Test results

1. Under `--json`, `cint test` writes one `CINT-TEST-1` object per test to standard output, in the order the tests ran. Its keys:

| Key | Value |
| --- | --- |
| `schema` | `"CINT-TEST-1"` |
| `file`, `line`, `name` | The module, the line of the `test` keyword and the test's name. |
| `passed` | `true` or `false`. |
| `expect_fault`, `expect_line` | The code and the line of the test's `expect_fault` header, or `null`. |
| `fault` | The fault the test raised, as in 7.4 without `noncanonical`, or `null`. |
| `error` | Present only when an error left the test: its `CINT-ERROR-1` object. |
| `fuel_consumed` | The fuel the test consumed, a decimal string. |

<a id="tool-errors"></a>

### 7.7 Tool errors

1. The text form of a tool error is one line on standard error: the tool's name, `: error: ` and the message, such as `cint: error: no FILE.ci given` or `cint debug: error: cannot read nofile.rec`. The interpreter writes `cint-interp: ` and the message, and it has no machine form. `cint` writes its usage before the line of a usage error, and for a usage error the interpreter writes its usage alone.
2. Under `--json`, a tool error is one `CINT-TOOL-1` object on standard error, with the keys `schema` (`"CINT-TOOL-1"`), `category` (`"usage"`, `"environment"` or `"internal"`), `exit_status` (4, 5 or 7) and `message`. When the host C compiler failed, the object also has `tool`, the compiler, `tool_exit_status`, its exit status and `log_path`, the log of 5.3, paragraph 3.

Example

3. A tool error from each of three tools:

```console
$ cint run --json --root . --root . hello.ci
{"category":"usage","exit_status":4,"message":"an option needs one value and is given at most once","schema":"CINT-TOOL-1"}
$ cint explain --json money.ci:nothing
{"category":"usage","exit_status":4,"message":"money.ci has no function nothing","schema":"CINT-TOOL-1"}
$ cint debug --json replay nofile.rec
{"category":"environment","exit_status":5,"message":"cannot read nofile.rec","schema":"CINT-TOOL-1"}
$ echo $?
5
```

<a id="the-interpreter"></a>

## 8 The interpreter

The interpreter `cint interp` runs a program on the executor `cpu-sir-interp`: it reads the SIR text of the program's output set (5.7) and runs it, with no C compiler. It gives every program the outcome `cint run` gives it: the same output bytes, the same fault record and the same fuel consumed. It also records runs, replays recordings and shows checkpoints and recordings as text.

<a id="command-line-2"></a>

### 8.1 Command line

```text
cint interp [--trace PATH] run DIR [FAULT [FUEL [STATE]]] [--grant LINE]...
cint interp [--trace PATH] run --record [--boundary entry|dispatch] PATH DIR
cint interp [--trace PATH] run --case [--boundary entry|dispatch] PATH --source SRC DIR
cint interp replay [--stop K] [--echo] [--checkpoint PATH] [--receipt PATH] [--source-identity HEX] FILE
cint interp show FILE
cint interp identity DIR
cint interp [--trace PATH] observe DIR OPTIONS
```

1. `DIR` is the directory of an output set that `cint emit-c` wrote, the one that `MANIFEST.ref` names. Before it runs anything, the interpreter reads the program's SIR text, checks the rules it relies on (each temporary is defined once and before it is used, every operand has the type its operation takes, and every operation that can fault has a site), and computes the program's revision identity. A text that breaks a rule is reported with its position and is not run, with exit status 3. A directory that cannot be read gives exit status 5 [SPEC-09P.3 BACK-01a].
2. A command line that matches none of these forms gives the usage and exit status 4.

<a id="running"></a>

### 8.2 Running

1. `run DIR` runs the program's entry, its script statements or `main`, with no fuel allowance, a call-depth limit of 256 and the frame arena of 5.3. Standard output receives the program's bytes. The exit status is that of `cint run`: 0, 1 for a fault, 6 when `main` returns an error. The interpreter writes no fault report and no error line.
2. With `FAULT`, the interpreter writes the fault record, in the encoding of SPEC-01P.3, to the file `FAULT` when the program faults. With `FUEL`, it writes the fuel consumed, as a record whose domain is `cint-core-1/fuel-consumed/v1`. With `STATE`, it writes one line per module variable at the end of the run, `state.global MODULE NAME TYPE VALUE`; a program without module variables takes no `STATE`, exit status 4.
3. `run --record PATH DIR` runs the program as `run` does and writes a recording of the run to `PATH` (11.2). The recording holds the SIR text of each module but not its source. Its exit status is that of `run`.
4. `run --case PATH --source SRC DIR` writes a case in place of a recording: each module's source is read from the directory `SRC` under the module's path and kept in the case. A source that cannot be read gives exit status 5, and nothing is written.
5. `--boundary` sets the density of a recording or a case: `entry`, the default, puts one boundary at the end of each entry; `dispatch` also puts one after each kernel dispatch publishes its outputs.
6. `run DIR` takes `--grant LINE` as `cint run` does (5.4, paragraphs 8 and 9), and the program's service calls run as they do under `cint run`. `run`, `run --record`, `run --case` and `replay` take program arguments as an executable that `cint build` writes does (5.5, paragraph 4): the word after each `--arg` and every word after `--`. A recording holds them in its program entry, and `replay` of a recording in `full` mode gives the program the recorded ones. `run --record` and `run --case` take no grant: the program runs with an empty grant set, so each service call that needs a capability returns `denied`, and the recording holds each call as an effect (11.2).
7. `--trace PATH` writes one line per counted operation, call and return to the file `PATH`: the kind (`op`, `call` or `return`), the step ordinal, the site as module number and site number, the operation and each operand as 16 hexadecimal digits of its two's-complement bits, followed by `-> ` and the result when there is one.

<a id="replaying-showing-and-identifying"></a>

### 8.3 Replaying, showing and identifying

1. `replay FILE` replays a recording or a case. It loads the code the file carries, checks that the code has the recorded revision identity, and runs each recorded entry again from the recorded initial state with the recorded inputs, comparing the digest of the state at each boundary with the recording's digest chain.
2. It writes one line to standard error. When every boundary agrees, the line is `replay: ok, boundaries N, chain HEX`, where `HEX` is the chain head (6, paragraph 8), and the exit status is 0. When replay diverges, or meets an entry it cannot replay, the line names the status, the event and the boundaries replayed, and the exit status is 3. A file that is not a complete recording, or whose code does not have the recorded revision identity, gives exit status 3 with a line that says so.
3. `--stop K` stops after boundary `K`. `--echo` writes the recorded print bytes to standard output, so that replay shows what the run printed; the interpreter prints nothing of its own while it replays. `--checkpoint PATH` writes a checkpoint of the state where replay stopped. `--receipt PATH` writes a replay receipt (12.4), and `--source-identity HEX`, 64 lowercase hexadecimal digits, gives the interpreter's source identity for it.
4. `show FILE` prints a checkpoint, a recording or a case as text (clause 11). It reads the file through the decoders that a restore uses, so it shows no file that a restore would reject; such a file gives exit status 3.
5. `identity DIR` prints two lines, `revision HEX` and `source-map HEX`, the identities of clause 6, with `absent` for a source-map identity that the text does not give. A text that does not load gives exit status 3.
6. `observe DIR OPTIONS` is the observation interface through which the conformance suite runs a program on the interpreter. `OPTIONS` are the options of the conformance harness `cint-harness`, and SPEC-09P.3 9.6 specifies both.

Example

7. Running, recording and replaying the output set of 5.7:

```console
$ cint interp run partial-out.stage-1
partial: big=9223372036854775807
$ echo $?
1
$ cint interp run --record partial.rec partial-out.stage-1
partial: big=9223372036854775807
$ cint interp replay partial.rec
replay: ok, boundaries 1, chain 49942e33499538df87f7e49b392c1c5aad6c557471959c567107f56b46ac400a
$ cint interp replay --echo --checkpoint partial.ckpt partial.rec
partial: big=9223372036854775807
replay: ok, boundaries 1, chain 49942e33499538df87f7e49b392c1c5aad6c557471959c567107f56b46ac400a
$ cint interp run --case partial.cicase --source . partial-out.stage-1
partial: big=9223372036854775807
$ cint interp --trace partial.trace run partial-out.stage-1
partial: big=9223372036854775807
$ cat partial.trace
op 0 0.3 add.checked.i64 0x7fffffffffffffff 0x0000000000000001
```

The trace has one line: the faulting addition, step 0, at site 3 of module 0, with no result.

<a id="the-workbench"></a>

## 9 The workbench

The workbench is an interactive session on the language. Each line typed at its prompt is compiled by the compiler that `cint run` uses and run on the interpreter's executor, `cpu-sir-interp`, so an entry means what the same text means in a program. Between entries the session can show any value exactly, keep checkpoints of its state, go back to an earlier state, and replay what it ran.

<a id="starting-a-session"></a>

### 9.1 Starting a session

```text
cint workbench [--json] [--root DIR] [--cint PATH] [--out DIR] [--echo] [FILE.ci | DIR]
cint repl [--json] [--root DIR] [--cint PATH] [--out DIR] [--echo] [FILE.ci | DIR]
```

1. `cint repl` is another name for `cint workbench`: it takes the same options and runs the same session.
2. The session's root is `DIR`, the directory of `FILE.ci`, `--root DIR`, or the current directory, in that order of preference; with `--root`, `FILE.ci` is a path under the root. `--root` together with a directory is a usage error, exit status 4.
3. A session started with `FILE.ci` loads that module first, as `:load` does (9.6), and its first prompt is `ci[2]>`.
4. The session needs the `cint` executable, for the compiler and for builds on `cpu-c17`: `--cint PATH`, by default the `cint` in the directory that holds the `cint-workbench` executable. When it cannot be found the session does not start, and the exit status is 5. `--out DIR` is the directory for the session's build files, by default `build/workbench` under the root.
5. The session writes one banner line, `cint workbench   profile cint-core-1   backend cpu-sir-interp   new session`, and then a prompt `ci[N]> `, where `N` is the ordinal the next entry takes. An entry whose braces are not closed continues on the next line after the prompt `..> `.
6. `--echo` writes each line read after its prompt, so that the output of a session that reads from a file reads as a terminal shows it. `--json` selects the machine form of the tool errors that stop a session from starting (7.7).
7. A line that starts with `:` is a command (9.8). Any other line is an entry (9.2).
8. The session ends at `:quit`, or at the end of its input, with exit status 0. An interrupt (Ctrl+C) stops the running entry with the fault `E_FUEL`, or stops a compilation, which then changes nothing.

Example

9. Tool errors at the start:

```console
$ cint workbench --json --root . shop
{"category":"usage","exit_status":4,"message":"--root and a root DIR exclude each other","schema":"CINT-TOOL-1"}
$ echo $?
4
```

<a id="entries"></a>

### 9.2 Entries

An entry is a declaration, a statement or an expression, typed as it would be written in a module. Every entry is one entry in the sense of SPEC-04P.4: it runs from its start to its boundary, with its own fuel and call depth.

1. A declaration adds a declaration to the session module, a module that belongs to the session, or replaces the declaration of the same name, and so makes a new revision of that module. A variable declared at the prompt is a module variable: it keeps its value from entry to entry, it is part of the state that checkpoints hold, and functions declared after it can use it.
2. After a declaration the session prints what it declared: `NAME = VALUE : TYPE` for a variable; `NAME : SIGNATURE   revision session@rN` for a function, with `(replaces rM)` when it replaced revision `M`; `NAME   revision session@rN` for an error set; and for an array its type, its element count and its first elements.
3. A statement runs and prints only what its print statements write. An expression prints `= VALUE : TYPE`.
4. A value computed by `div_round` that discarded a nonzero remainder is followed by `inexact: div_round MODE (exact quotient Q)`, with the exact quotient in decimal.
5. An entry that translation rejects prints its diagnostics (7.1), with the position `entry N:LINE:COLUMN`, and changes nothing but the entry ordinal.
6. An entry that faults prints the fault report of 7.3 with the lines of 7.3, paragraph 5. The state it leaves is the state after a fault that SPEC-04P.4 defines: the stores of the statements that completed before the faulting one remain, and the faulting statement's own store does not happen [SPEC-04P.4 IM-121]. The session then clears the fault, which changes nothing else, so that the next entry can run; the report's `state` line says so with "context reset".
7. Every entry takes the next ordinal, and so do `:run` (one per call), `:load` and `:reload`, whether they succeed or not. Other commands take none. The ordinal names the entry in positions, in `:log`, `:goto` and `:trace`.
8. In a signature, a result of an error union type is shown as `U16!T`, the tag type of the error and the type of the value.

Example

9. Entries of each kind, a fault, a translation error and the log:

```console
$ cint workbench
cint workbench   profile cint-core-1   backend cpu-sir-interp   new session
ci[1]> "hello, world\n";
hello, world
ci[2]> I64 answer = 6 * 7;
answer = 42 : I64
ci[3]> answer * 2
= 84 : I64
ci[4]> I64[3] cents = [1999, 425, 10];
cents : I64[3]   3 elements   host
  [0] = 1999
  [1] = 425
  [2] = 10
ci[5]> I64 subtotal = sum(cents);
subtotal = 2434 : I64
ci[6]> I64 tax = div_round(subtotal * 825, 10000, half_even);
tax = 201 : I64   inexact: div_round half_even (exact quotient 200.805)
ci[7]> I64 big = 9223372036854775800;
big = 9223372036854775800 : I64
ci[8]> big + 12
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> entry 8:1:5
   |
 1 | big + 12
   |     ^ add.checked.i64
   |
   = operation: add.checked.i64
   = operand-count: 2
   = operand: I64 9223372036854775800
   = operand: I64 12
   = exact:   9223372036854775812
   = limit:   I64 9223372036854775807
   = revision: 12ee022b9117fd23e4126c3214f104e3a7f56b3da489030dabcae5f8232cbf47
   = source-map: none
   = address: none
   = stack-depth: 0
   = fuel-consumed: 1 (not part of the fault record)
   = state: no store completed in this entry; context reset
   = noncanonical: not part of the fault record
   = workbench-position: entry 8
   = site: wb_entry#2
   = backend: cpu-sir-interp
ci[9]> big +| 12
= 9223372036854775807 : I64
ci[10]> I64 twice(I64 x) {
..>     return x * 2;
..> }
twice : (I64) -> I64   revision session@r6
ci[11]> twice(answer)
= 84 : I64
ci[12]> I64 twice(I64 x) {
..>     return x + x;
..> }
twice : (I64) -> I64   revision session@r7 (replaces r6)
ci[13]> answer = answer + 1;
ci[14]> answer
= 43 : I64
ci[15]> I64 oops = answer +;
error[C1050]: syntax error
  --> entry 15:1:20
   |
 1 | I64 oops = answer +;
   |                    ^
   |
ci[16]> :log
entry 1     done           "hello, world\n";
entry 2     declared       I64 answer = 6 * 7;
entry 3     value          answer * 2
entry 4     declared       I64[3] cents = [1999, 425, 10];
entry 5     declared       I64 subtotal = sum(cents);
entry 6     declared       I64 tax = div_round(subtotal * 825, 10000, half_even);
entry 7     declared       I64 big = 9223372036854775800;
entry 8     fault          big + 12
entry 9     value          big +| 12
entry 10    declared       I64 twice(I64 x) { ...
entry 11    value          twice(answer)
entry 12    declared       I64 twice(I64 x) { ...
entry 13    done           answer = answer + 1;
entry 14    value          answer
entry 15    compile error  I64 oops = answer +;
ci[16]> :quit --yes
```

The site `wb_entry#2` is the addition in the function that the session made of entry 8.

<a id="the-inspector"></a>

### 9.3 The inspector

`:inspect` shows a value exactly, in the formats a reader asks for, and `:type` shows the type of an expression without evaluating it. Neither is an entry.

1. `:inspect EXPR [/FORMAT ...]` evaluates `EXPR` in a scratch copy of the current state and prints `EXPR : TYPE`, then the value. The expression shall be pure: one that prints, or calls a function that prints, is refused. A fault in the expression prints its report, whose `state` line reads "nothing is kept: the session is as it was; context reset" and whose `workbench-position` is `:inspect, not an entry`; the session's state does not change.
2. `:type EXPR` prints `EXPR : TYPE` and evaluates nothing.
3. An integer is shown in full decimal, with no exponent and no digit left out, whatever its width. A `Bool` is shown as `true` or `false`.
4. The formats:

| Format | Shows |
| --- | --- |
| (none) | `exact`, the value in decimal. |
| `/raw` | `raw`, the stored integer. |
| `/hex` | `hex`, the two's-complement bits in hexadecimal, at the full width of the type. |
| `/bin` | `bin`, the same in binary, in groups of 8 bits joined by `_` when the type is wider than 8 bits. |
| `/bytes` | `bytes`, the storage bytes in memory order, least significant first, after their offset `+0:`. |
| `/group` | `exact` with its digits in groups of three joined by `_`, which is also a valid literal. |
| `/scale N` | `exact`, the value divided by `N` as an exact decimal, and `raw`; for example amounts in cents with `/scale 100`. The type does not change. |
| `/all` | Every format above that applies, under a header that gives the type's kind and width, such as `(signed, 8 bits)`, `(unsigned, 32 bits)` or `(1 byte, 0 or 1)`. |
| `/grid [R0..R1, C0..C1]` | For a rank-2 array, the rows `R0` up to but not including `R1` and the array positions `C0` up to but not including `C1`, with their indices. |
| `/stats` | For an array: the element count; the exact sum, with the width of the accumulator that holds it without overflow, `w + ceil(log2 n)` bits for `n` elements of width `w`; the least and the greatest element with the index of its first occurrence; and the count of nonzero elements [SPEC-04P.4 IM-80]. |
| `/view` | For an array: its element type, rank, shape, lower bounds, origin, strides in elements, permission and placement. |

5. An array is shown with a summary line, `NAME : TYPE   N elements   PLACEMENT`, and its first 8 elements in index order, followed by a count of the elements not shown.
6. `/stats` computes its sum with an accumulator of its own and never faults; it is a display, not an evaluation of the program's `sum`.

Example

7. Formats of three scalars, a fault in an inspected expression and an array:

```console
$ cint workbench
cint workbench   profile cint-core-1   backend cpu-sir-interp   new session
ci[1]> I8 x = -5;
x = -5 : I8
ci[2]> :inspect x
x : I8
  exact   -5
ci[2]> :inspect x /all
x : I8   (signed, 8 bits)
  exact   -5
  raw     -5
  hex     0xfb
  bin     0b11111011
  bytes   +0: fb
ci[2]> U32 n = 4000000000;
n = 4000000000 : U32
ci[3]> :inspect n /group
n : U32
  exact   4_000_000_000
ci[3]> :inspect n /all
n : U32   (unsigned, 32 bits)
  exact   4000000000
  raw     4000000000
  hex     0xee6b2800
  bin     0b11101110_01101011_00101000_00000000
  bytes   +0: 00 28 6b ee
ci[3]> Bool ok = n > 5;
ok = true : Bool
ci[4]> :inspect ok /all
ok : Bool   (1 byte, 0 or 1)
  exact   true
  raw     1
  hex     0x01
  bin     0b00000001
  bytes   +0: 01
ci[4]> I64 cents = 2434;
cents = 2434 : I64
ci[5]> :inspect cents /scale 100
cents : I64   shown with scale 100
  exact   24.34
  raw     2434
ci[5]> :type n + 1
n + 1 : U32
ci[5]> :inspect n + 1000000000
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> inspect:1:3
   |
 1 | n + 1000000000
   |   ^ add.checked.u32
   |
   = operation: add.checked.u32
   = operand-count: 2
   = operand: U32 4000000000
   = operand: U32 1000000000
   = exact:   5000000000
   = limit:   U32 4294967295
   ...
   = state: nothing is kept: the session is as it was; context reset
   = noncanonical: not part of the fault record
   = workbench-position: :inspect, not an entry
   = site: wb_entry#2
   = backend: cpu-sir-interp
ci[5]> I64[2, 3] g;
g : I64[2, 3]   6 elements   host
  [0, 0] = 0
  [0, 1] = 0
  [0, 2] = 0
  [1, 0] = 0
  [1, 1] = 0
  [1, 2] = 0
ci[6]> g[1, 2] = 7;
ci[7]> g[0, 1] = -3;
ci[8]> :inspect g /grid [0..2, 1..3]
g : I64[2, 3]   6 elements   host
  grid   rows 0 to 1 of 0 to 1, columns 1 to 2 of 0 to 2
      1  2
  0  -3  0
  1   0  7
ci[8]> :inspect g /stats
g : I64[2, 3]   6 elements   host
  count    6
...
  min      -3   first at [0, 1]
  max      7    first at [1, 2]
  nonzero  2
ci[8]> :inspect g /view
g : I64[2, 3]   6 elements   host
  view
...
    element     I64
    rank        2
    shape       [2, 3]
    lower       [0, 0]
    origin      0
    strides     [3, 1] elements
    permission  write
    placement   host
ci[8]> I64[4096] big;
big : I64[4096]   4096 elements   host
  [0] = 0
  [1] = 0
  [2] = 0
  [3] = 0
  [4] = 0
  [5] = 0
  [6] = 0
  [7] = 0
  ... 4088 more; /grid and /stats show them
ci[9]> :quit --yes
```

The `sum` line left out of `/stats` gives the sum, 4, and the accumulator, `I67`: 64 bits and 3 more for 6 elements. The line left out of `/view` says that the array's buffer has no identity of its own.

<a id="calls-fuel-and-call-depth"></a>

### 9.4 Calls, fuel and call depth

`:run` calls an exported function of the session as an entry of its own, as a host program would call it. `:fuel` and `:depth` set the limits that each entry runs under, and `:fault` and `:trace` look back at the last fault.

1. `:run F(ARGS) [N]` calls the function `F` `N` times, by default once. `F` shall be an export of the session module, declared at the prompt with `export`; a function that is not exported is refused, and so is an export of a loaded module. Each argument shall be an integer literal or `true` or `false`, a value of its parameter's type.
2. Each call is one entry, with the function as the entry, so the function's calls start the stack of a fault report. A call that returns a value prints it as an expression does; one that returns an error prints the line of 7.5.
3. A call that faults prints its report, with the position `entry E, call K of N`, then `K of N calls made`; the calls after it are not made.
4. `:fault` prints the report of the last fault again, or the line of the last error result. `:fault /path` replays the entry of the last error result and prints the path the error took: the function that returned it, then each function that passed it on with `try`, innermost first, with the position of each call.
5. `:fuel N` gives each entry that follows a fuel allowance of `N`, and `:fuel none` removes the allowance, which is the default. `:depth N` sets the call-depth limit of each entry that follows, 256 by default. An entry that exceeds either faults with `E_FUEL` or `E_DEPTH`, whose `limit` is the allowance or the limit.
6. `:trace [N | fault]` replays entry `N`, by default the entry of the last fault, and prints its counted operations in order: the ordinal from 0, the position, the operation, the operands and the result or the fault code for the operation that faulted.

Example

7. An error result and its path, a function that `:run` refuses, the fuel and depth limits and a trace:

```console
$ cint workbench
cint workbench   profile cint-core-1   backend cpu-sir-interp   new session
ci[1]> error Parse { empty, bad_digit }
Parse   revision session@r1
ci[2]> Parse!I64 digit(I64 c) {
..>     if (c < 48 || c > 57) {
..>         return .bad_digit;
..>     }
..>     return c - 48;
..> }
digit : (I64) -> U16!I64   revision session@r2
ci[3]> export Parse!I64 two(I64 a, I64 b) {
..>     I64 x = try digit(a);
..>     I64 y = try digit(b);
..>     return x * 10 + y;
..> }
two : (I64, I64) -> U16!I64 error E1   revision session@r3
ci[4]> :run two(52, 50)
= 42 : I64
ci[5]> :run two(52, 120)
error: session.Parse.bad_digit (tag 2)
ci[6]> :fault /path
entry 5   error: session.Parse.bad_digit (tag 2)   evaluator cpu-sir-interp, by replay
the path, innermost first, from the calls and returns of the replay:
  digit returned the error (tag 2)
  two passed it on with try (tag 2), from its call of digit at entry 3:3:17
ci[6]> I64 level = 10;
level = 10 : I64
ci[7]> export void step() {
..>     level = level * 3 - 7;
..> }
step : () -> void   revision session@r5
ci[8]> :run step() 3
ci[11]> level
= 179 : I64
ci[12]> :run digit(48)
:run: digit is not an export of the session module (declare it with export)
ci[13]> :fuel 1
fuel: 1 units per entry
ci[13]> export I64 count_to(I64 n) {
..>     I64 t = 0;
..>     for i in 0..n {
..>         t += i;
..>     }
..>     return t;
..> }
count_to : (I64) -> I64   revision session@r6
ci[14]> :run count_to(10)
fault[E_FUEL]: fuel allowance exhausted
  --> entry 13:3:5
   |
 3 |     for i in 0..n {
   |     ^ fuel.charge
   |
   = operation: fuel.charge
   = operand-count: 0
   = exact:   none
   = limit:   I64 1
...
   = workbench-position: entry 14
   = site: count_to#11
   = backend: cpu-sir-interp
ci[15]> :fuel none
fuel: no allowance per entry
ci[15]> :run count_to(10)
= 45 : I64
ci[16]> export I64 down(I64 n) {
..>     if (n == 0) {
..>         return 0;
..>     }
..>     return 1 + down(n - 1);
..> }
down : (I64) -> I64   revision session@r7
ci[17]> :depth 3
depth: 3 per entry
ci[17]> :run down(5)
fault[E_DEPTH]: call depth limit exceeded
  --> entry 16:5:16
   |
 5 |     return 1 + down(n - 1);
   |                ^ call.enter
   |
   = operation: call.enter
   = operand-count: 0
   = exact:   none
   = limit:   I64 3
   = revision: fe8a7fdc3333fa90b074e9720c1f5cdcc51f88513f2be8e19f0cf2451baa05fd
   = source-map: none
   = address: none
   = stack-depth: 2
   = called from: entry 16:5:16
   = called from: entry 16:5:16
   = fuel-consumed: 3 (not part of the fault record)
   = state: no store completed in this entry; context reset
...
ci[18]> :trace
entry 17   :run down(5)   evaluator cpu-sir-interp, by replay
  ord  position           operation              operands                 value
    0  entry 16:5:23      sub.checked.i64        5, 1                     4
    1  entry 16:5:16      call                   4
    2  entry 16:5:23      sub.checked.i64        4, 1                     3
    3  entry 16:5:16      call                   3
    4  entry 16:5:23      sub.checked.i64        3, 1                     2
    5  entry 16:5:16      call                   2
ci[18]> :quit --yes
```

The call of `down` with depth limit 3 faults at the call that would make the fourth active call; the report lists the two calls made inside the entry.

8. A fault in the second of three calls. The first call's store remains, and the third call is not made:

```console
$ cint workbench
cint workbench   profile cint-core-1   backend cpu-sir-interp   new session
ci[1]> I64 cap = 100;
cap = 100 : I64
ci[2]> export void grow() {
..>     cap = cap * 1000000000;
..> }
grow : () -> void   revision session@r2
ci[3]> :run grow() 3
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> entry 2:2:15
   |
 2 |     cap = cap * 1000000000;
   |               ^ mul.checked.i64
   |
   = operation: mul.checked.i64
   = operand-count: 2
   = operand: I64 100000000000
   = operand: I64 1000000000
   = exact:   100000000000000000000
   = limit:   I64 9223372036854775807
...
   = workbench-position: entry 4, call 2 of 3
   = site: grow#2
   = backend: cpu-sir-interp
2 of 3 calls made
ci[5]> cap
= 100000000000 : I64
ci[6]> :trace fault
entry 4   :run grow() 3   evaluator cpu-sir-interp, by replay
  ord  position           operation              operands                 value
    0  entry 2:2:15       mul.checked.i64        100000000000, 1000000000 E_OVERFLOW
ci[6]> :quit --yes
```

<a id="watches-checkpoints-and-history"></a>

### 9.5 Watches, checkpoints and history

A session keeps every state it reached. A *watch* reports when a value changes; a *checkpoint* names a state so that the session can come back to it; and the session reaches any earlier boundary again by replaying the entries that led to it.

1. `:watch EXPR [when COND] [stop]` adds a watch, numbers it and prints `watch K   EXPR = VALUE : TYPE`. After each following entry that changes the value of the pure expression `EXPR`, while the pure condition `COND`, when given, holds, the session prints `watch K   entry E   EXPR = VALUE`. With `stop`, a change that is reported ends the `:run` that made it after the current call, as if fewer calls had been asked for, and the session prints `a watch stops :run: K of N calls made`. A watch observes the state and changes nothing in it.
2. At most 20 changes of one watch are printed for one command, followed by a count of the rest; `:watch log K` prints every change watch `K` recorded. `:watches` lists the watches, and `:unwatch K` removes watch `K`.
3. `:checkpoint NAME` keeps the current state, the revisions of the session's modules and the per-entry settings under `NAME`, and prints the checkpoint's number, its module variables and its revisions. A checkpoint holds the code of its revisions, so a change of a file on disk after the checkpoint does not change what it means.
4. `:diff NAME [BINDING]` compares the current state with checkpoint `NAME`, binding by binding in declaration order, and prints for each binding whether it differs, the count of differing elements and the first difference, with both values exactly.
5. `:restore NAME` makes checkpoint `NAME` current: its state, its revisions and its settings. Before it does, it keeps the current state as an automatic checkpoint `auto-K`, unless an existing checkpoint holds the same state. When a loaded module's file differs from the restored revision, the session says so; `:reload` reads it. A restore does not undo what earlier entries printed.
6. `:undo` goes back to the state before the last entry, after keeping the current state as an automatic checkpoint.
7. `:goto N` makes the state at the end of entry `N` current by replaying the entries up to it, and `:goto fault` the state at the end of the last entry that faulted. When that state is not the end of the current branch, the entries that follow go on a new branch named `entry-N`.
8. `:branch NAME` starts a branch named `NAME` at the current state. `:branches` lists the branches, with the current one marked `*`, and every checkpoint with its branch, the entry after which it was taken, and the checkpoint it follows.
9. `:replay NAME` replays the entries since checkpoint `NAME`, evaluating the watches at each boundary, and compares what they print and how they end with what the session recorded. `:replay NAME --changed BINDING` finds the last entry since `NAME` that changed the module variable `BINDING`, by replaying backward from the current state; it prints the entry and the value before and after. `:replay NAME --when PRED` finds the first entry since `NAME` after which the pure predicate `PRED` holds, by bisection, and so takes `PRED` to stay true once it holds.
10. `:history NAME` lists the revisions of a declaration and the checkpoints that hold each one. `:log` lists the entries of the current state's history with their outcome: `declared`, `value`, `done`, `fault`, `compile error`, `loaded` or `reloaded`. `:effects` lists the recorded entries that led to the current state, with what each printed, its outcome, its fuel and the first 8 digits of the state hash at its end, and a line of totals.

Example

11. A watch that stops a `:run`, two checkpoints, a case of the entries since the second (9.6), and the commands that go back:

```console
$ cint workbench
cint workbench   profile cint-core-1   backend cpu-sir-interp   new session
ci[1]> I64 level = 10;
level = 10 : I64
ci[2]> I64 rate = 3;
rate = 3 : I64
ci[3]> export void step() {
..>     level = level * rate - 7;
..> }
step : () -> void   revision session@r3
ci[4]> :checkpoint start
checkpoint start   #1   bindings 2   revisions: session@r3
ci[4]> :watch level
watch 1   level = 10 : I64
ci[4]> :watch level when level > 100 stop
watch 2   level = 10 : I64   when level > 100   stop
ci[4]> :run step() 6
watch 1   entry 4   level = 23
watch 1   entry 5   level = 62
watch 1   entry 6   level = 179
watch 2   entry 6   level = 179
a watch stops :run: 3 of 6 calls made
ci[7]> :watches
watch 1   level : I64
watch 2   level : I64   when level > 100   stop
ci[7]> :unwatch 2
watch 2 removed
ci[7]> :checkpoint after
checkpoint after   #2   bindings 2   revisions: session@r3
ci[7]> rate = 2;
ci[8]> :run step() 2
watch 1   entry 8   level = 351
watch 1   entry 9   level = 695
ci[10]> :diff after
binding   status      differing   first canonical difference
level     differs     1           695 here, 179 in after
rate      differs     1           2 here, 3 in after
ci[10]> :save-case grow.cicase
note: the case includes every recorded observe result verbatim; this case has none
case grow.cicase   checkpoint after   entries 7..9   effects 0   density entry
ci[10]> :undo
saved current state as auto-1
back to the state before entry 9
ci[10]> :replay start --changed level
session.level last changed at entry 8, from 179 to 351
...
ci[10]> :replay start --when level > 100
level > 100 first holds at entry 6
...
ci[10]> :replay after
replaying 2 entries from after to the current state (entry 7 to entry 8), comparing prints and outcomes with the recording
watch 1   entry 8   level = 351
replay matches the recording at 2 boundaries
ci[10]> :goto 5
saved current state as auto-2
at the end of entry 5, reached by replay
later entries go on branch entry-5, from auto-3
ci[10]> :inspect level
level : I64
  exact   62
ci[10]> :branches
  main   checkpoints 5   head auto-3
* entry-5   from auto-3   checkpoints 0   head auto-3
#1  start   main   after entry 3
#2  after   main   after entry 6   follows start
#3  auto-1   main   after entry 9   follows after
#4  auto-2   main   after entry 9   follows after
#5  auto-3   main   after entry 9   follows after
ci[10]> :restore after
saved current state as auto-4
restored after   #2
ci[10]> :history step
r3  entry 3   in checkpoints: start, after, auto-1, auto-2, auto-3, auto-4
ci[10]> :log
entry 1     declared       I64 level = 10;
entry 2     declared       I64 rate = 3;
entry 3     declared       export void step() { ...
entry 4     value          :run step() 6
entry 5     value          :run step() 6
entry 6     value          :run step() 6
entry 7     done           rate = 2;
entry 8     value          :run step() 2
entry 9     value          :run step() 2
ci[10]> :quit --yes
```

The lines left out after `--changed` and `--when` give the number of boundaries replayed and the number of bisection probes.

<a id="modules-saving-and-opening"></a>

### 9.6 Modules, saving and opening

1. `:load FILE.ci` loads a module under the root. A module with only declarations becomes a loaded module: the session prints `loaded NAME (FILE.ci)`, its exports are in scope as `NAME.X`, and an exported module variable may be assigned. A module with top-level statements is a script: its statements run as one entry, which prints what they print, and its declarations join the session module as one declaration named by the script's path; its variables stay local to that entry.
2. `:reload` reads every loaded module again, makes a new revision of each one that changed, and prints `reloaded NAME (FILE.ci)`.
3. `:save DIR` writes the session to the directory `DIR`: an `objects` directory, which holds the sources, states and records of the session by their digests, and the session manifest `session.manifest`, a text file from the line `CINT-SESSION-1` to the line `end`. The manifest is written last, so a save that stops early leaves no manifest, and such a directory is not opened. The session prints `saved session to DIR   entries N   checkpoints C   objects O`.
4. `:open DIR` opens a saved session: its entries, checkpoints, branches and current state. It prints `opened DIR   entries N   checkpoints C   branch B`, and the next entry takes the ordinal after the saved ones. A saved session is data: opening it runs the code it holds only through replay of what it recorded.
5. `:save-case PATH` writes a case (11.2) of the entries since the last checkpoint, from that checkpoint's state, and prints a note that the case holds every recorded input verbatim, then `case PATH   checkpoint NAME   entries A..B   effects K   density entry`. When no entry ran since the last checkpoint, no case is written and the session says so.
6. `:quit` ends the session. When the session has changes since it was last saved, the first `:quit` says so and the session goes on; a second `:quit`, or `:quit --yes`, ends it.

7. In cint 0.2.4, the workbench, `cint repl` and `cint debug` do not load a module that declares an extern or a `requires`: `:load` reports the declaration with the diagnostic C4012, and the module is not loaded. Such a program runs under `cint run` and `cint interp`.

Example

8. A session started on a module, saved and opened again:

```console
$ cint workbench tally.ci
cint workbench   profile cint-core-1   backend cpu-sir-interp   new session
loaded tally (tally.ci)
ci[2]> tally.total
= 0 : I64
ci[3]> tally.add(250);
ci[4]> tally.total = tally.total + 5;
ci[5]> export void deposit(I64 n) {
..>     tally.add(n);
..> }
deposit : (I64) -> void   revision session@r1
ci[6]> :checkpoint opened
checkpoint opened   #1   revisions: session@r1, tally@r1
ci[6]> :run deposit(100) 3
ci[9]> :reload
reloaded tally (tally.ci)
ci[10]> :save-case deposits.cicase
note: the case includes every recorded observe result verbatim; this case has none
case deposits.cicase   checkpoint opened   entries 6..8   effects 0   density entry
ci[10]> :save saved
saved session to saved   entries 9   checkpoints 1   objects 39
ci[10]> :quit
$ ls saved
objects
session.manifest
$ head -1 saved/session.manifest
CINT-SESSION-1
$ tail -1 saved/session.manifest
end
$ cint workbench
cint workbench   profile cint-core-1   backend cpu-sir-interp   new session
ci[1]> :open saved
opened saved   entries 9   checkpoints 1   branch main
ci[10]> tally.total
= 555 : I64
ci[11]> :quit --yes
```

`tally.ci` exports a variable `total` and a function `add` that adds its argument to it.

<a id="executors"></a>

### 9.7 Executors

1. Entries run on `cpu-sir-interp` unless `:backend cpu-c17` selects the executor that `cint run` uses. On `cpu-c17`, each revision of the session's code is built as a library with `cint build --lib` the first time an entry needs it, and the session prints the time the build took; entries on `cpu-c17` are not recorded, and `:inspect` and the watches still run on `cpu-sir-interp`. `:backend` alone prints the executor in force.
2. `:run F(ARGS) [N] --check B` runs each call on the executor in force and on `B` too, compares the two at each boundary, and stops at the first boundary where they differ; when they agree it prints `check: A and B agree at N boundaries`.
3. `:backend gpu-cuda` and the commands `:place` and `:faults`, which place data on a device and list the work-items of a device fault, are refused: cint 0.2.4 builds no `gpu-cuda` library for a session.

Example

4. Two calls on `cpu-c17`, and two more checked against it:

```console
$ cint workbench
cint workbench   profile cint-core-1   backend cpu-sir-interp   new session
ci[1]> I64 level = 10;
level = 10 : I64
ci[2]> export void step() {
..>     level = level * 3 - 7;
..> }
step : () -> void   revision session@r2
ci[3]> :backend cpu-c17
backend: cpu-c17; each revision is built as a library, entries on it are not recorded, and :inspect and the watches stay on cpu-sir-interp
ci[3]> :run step() 2
cpu-c17: revision 876902c8 built as a library in 6844 ms
ci[5]> level
cpu-c17: revision b6bc2b52 built as a library in 504 ms
= 62 : I64
ci[6]> :backend cpu-sir-interp
backend: cpu-sir-interp
ci[6]> :run step() 2 --check cpu-c17
check: cpu-sir-interp and cpu-c17 agree at 2 boundaries
ci[8]> :quit --yes
```

The expression entry `level` is compiled as code of its own, so it builds a second library.

<a id="commands"></a>

### 9.8 Commands

1. Every command of a session:

| Command | Does | Section |
| --- | --- | --- |
| `:load FILE.ci` | Load a module or run a script. | 9.6 |
| `:reload` | Read the loaded modules again. | 9.6 |
| `:run F(ARGS) [N] [--check B]` | Call an export of the session `N` times, each call an entry. | 9.4, 9.7 |
| `:inspect EXPR [/FORMAT ...]` | Show a value exactly. | 9.3 |
| `:type EXPR` | Show a type without evaluating. | 9.3 |
| `:explain [NAME]` | What the compiler knows about the session's functions, as `cint explain` prints it. | 9.9 |
| `:fault [/path]` | The last fault report or error result; the path of the error. | 9.4 |
| `:fuel N`, `:fuel none` | The fuel allowance of each entry. | 9.4 |
| `:depth N` | The call-depth limit of each entry. | 9.4 |
| `:backend [ID]` | The executor of entries. | 9.7 |
| `:trace [N \| fault]` | The counted operations of an entry. | 9.4 |
| `:watch EXPR [when COND] [stop]`, `:watch log K`, `:watches`, `:unwatch K` | Watches. | 9.5 |
| `:checkpoint NAME`, `:restore NAME`, `:undo` | Keep and restore states. | 9.5 |
| `:goto N`, `:goto fault` | Reach an earlier boundary by replay. | 9.5 |
| `:branch NAME`, `:branches` | Branches and checkpoints. | 9.5 |
| `:diff NAME [BINDING]` | Compare with a checkpoint. | 9.5 |
| `:replay NAME [--changed BINDING \| --when PRED]` | Replay since a checkpoint, or search it. | 9.5 |
| `:history NAME`, `:log`, `:effects` | The history of a declaration, the entries, the recorded effects. | 9.5 |
| `:save DIR`, `:open DIR`, `:save-case PATH` | Save, open and write a case. | 9.6 |
| `:help` | A summary of the commands. | |
| `:quit [--yes]`, `:q` | End the session. | 9.6 |

2. An unknown command, or a known one with arguments it does not take, prints a line that says what it takes and changes nothing.

<a id="cint-explain"></a>

### 9.9 `cint explain`

`cint explain` prints what the compiler knows about a function without running it: its signature, every operation that can fault, its reductions, where it is charged fuel, which executors run it, and its SIR text.

```text
cint explain [--json] [--root DIR] [--stdlib DIR] [--cint PATH] [--out DIR | --sir DIR] FILE.ci[:NAME]
```

1. `cint explain` translates `FILE.ci` with `cint emit-c`, into `--out DIR`, by default `build/explain` under the root, or reads an output set that is already written from `--sir DIR`. `--out` and `--sir` exclude each other. `--stdlib DIR` names the standard library root (5.3); by default it is the directory `lib` beside the `cint` that `--cint` names, or else beside `cint-explain`. An import of one segment that is absent under the root, such as `fs` or `console`, is read from there.
2. The first line is `module FILE.ci   revision HEX`. With `:NAME`, one section follows for the function `NAME`; without it, a summary of the program and of the compiler's budget, then a section for each function. A `NAME` that the module does not define is a usage error, exit status 4.
3. A function's section has these parts:
   - `function NAME   MODULE.sites:LINE   EFFECTS`: the line at which the function begins in the module's SIR text, the file `MODULE.sites` of the output set (5.7), which is not a position in the source; and whether the function is `pure`;
   - `signature`: each parameter with its direction, `in` or `view`, its name and its type and the result;
   - `fault sites`: the count of sites that can fault and of those excluded, then one line per site with its identifier `NAME#N`, its position, its operation and the fault codes it can raise. A site identifier is the site number of the program in node order; fault reports of the workbench name it in their `site` line. In cint 0.2.4 no site is excluded, and every site is reported as `possible`;
   - `reductions`: for each reduction, its operation, its result type and the accumulator width, `w + ceil(log2 n)` bits for `n` terms of width `w`, which holds every partial sum exactly [SPEC-04P.4 IM-80];
   - `fuel`: each charge point, a `fuel.charge` node of the SIR, with its amount and where it is charged; fuel is charged at these nodes and at no other point, so every executor charges it alike [SPEC-09P.3 SIR-07];
   - `lowering`: each executor, `cpu-sir-interp` and `cpu-c17`, and whether it runs the function;
   - `sir`: the function's SIR text, byte for byte as the compiler wrote it [SPEC-09P.3 SIR-09].
4. <a id="CINTC-09"></a>With `--json`, the output is one `CINT-EXPLAIN-1` object with the keys `schema`, `module`, `revision` and `functions`, and, without `:NAME`, `build`. Each function is an object with the keys `name`, `kind`, `effects`, `parameters` (each with `direction`, `name` and `type`), `result`, `fault_sites` (each with `id`, `position`, `operation`, `codes`, `status` and `reason`), `fuel` (each with `id`, `charge` and `amount`), `lowering` (each with `backend`, `status` and, for `cpu-c17`, `profile`), `prints`, `sir` and `sir_position`. `build` holds the program's counts of modules, functions and nodes and `compile_fuel`, the budget of each compiler phase: `k` times the input bytes plus `c` units, with `k` = 21,524 and `c` = 19,876, and the units of each module [CINTC-09].

Example

5. The section of one function, and the same in machine form:

```console
$ cint explain money.ci:tax
module money.ci   revision 602173cd38defe996814e3298bd2a45c715a1cea53e3b5f8f84f6d073777ebde

function tax   money.sites:16   pure
  signature  (in subtotal: I64, in rate_bp: I64) -> I64
  fault sites (3): 3 possible, 0 statically excluded
    tax#6  money.ci:8:12  fuel.charge                      E_FUEL possible
    tax#7  money.ci:9:31  mul.checked.i64                  E_OVERFLOW possible
    tax#8  money.ci:9:12  div_round.checked.i64.half_even  E_OVERFLOW E_DIV_ZERO possible
...
    tax#6  fuel.charge 1   at the start of the body
  lowering
...
  sir
export func @money.tax(%subtotal: I64, %rate_bp: I64) -> I64 {
  fuel.charge 1                                  #6 @money.ci:8:12
  %t0 = mul.checked.i64 %subtotal, %rate_bp      #7 @money.ci:9:31
  %t1 = const I64 10000
  %t2 = div_round.checked.i64.half_even %t0, %t1 #8 @money.ci:9:12
  return %t2
}
$ cint explain --json money.ci:tax
{"functions":[{"effects":"pure","fault_sites":[{"codes":["E_FUEL"],"id":"tax#6","operation":"fuel.charge","position":{"column":12,"file":"money.ci","line":8},"reason":"not analyzed","status":"possible"},{"codes":["E_OVERFLOW"],"id":"tax#7","operation":"mul.checked.i64","position":{"column":31,"file":"money.ci","line":9},"reason":"not analyzed","status":"possible"},{"codes":["E_OVERFLOW","E_DIV_ZERO"],"id":"tax#8","operation":"div_round.checked.i64.half_even","position":{"column":12,"file":"money.ci","line":9},"reason":"not analyzed","status":"possible"}],"fuel":[{"amount":1,"charge":"start","id":"tax#6"}],"kind":"function","lowering":[{"backend":"cpu-c17","profile":"ref-c","status":"supported"},{"backend":"cpu-sir-interp","status":"supported"}],"name":"tax","parameters":[{"direction":"in","name":"subtotal","type":"I64"},{"direction":"in","name":"rate_bp","type":"I64"}],"prints":[],"result":"I64","sir":"export func @money.tax(%subtotal: I64, %rate_bp: I64) -> I64 {\n  fuel.charge 1                                  #6 @money.ci:8:12\n  %t0 = mul.checked.i64 %subtotal, %rate_bp      #7 @money.ci:9:31\n  %t1 = const I64 10000\n  %t2 = div_round.checked.i64.half_even %t0, %t1 #8 @money.ci:9:12\n  return %t2\n}\n","sir_position":{"file":"money.sites","line":16}}],"module":"money.ci","revision":"602173cd38defe996814e3298bd2a45c715a1cea53e3b5f8f84f6d073777ebde","schema":"CINT-EXPLAIN-1"}
```

The lines left out of the text form are the reason no site is excluded, the heading of the fuel part and the two lines of the lowering part, which say that `cpu-sir-interp` runs this text and that `cpu-c17` compiles C from it. `money.ci` is listed in Annex A.1.

<a id="debugging-with-cint-debug"></a>

## 10 Debugging with `cint debug`

`cint debug` answers questions about a run after it happened. It works on a recording or a case (clause 11), never on the live program: it replays the run to show any boundary of it, compares two executors on it, and locates the boundary where it went wrong. Because replay supplies every input from the file, the answers are the same on every machine that has the file.

<a id="command-line-3"></a>

### 10.1 Command line

```text
cint debug [--json] [--out DIR] [--echo] RECORDING | CASE
cint debug replay [--json] RECORDING | CASE [--to N] [--receipt PATH] [--source-identity HEX]
cint debug diff [--json] CASE [--ref EXECUTOR] [--cand EXECUTOR] [--receipt PATH] [--cint PATH] [--out DIR]
                [--compiler-identity HEX] [--interpreter-identity HEX] [--workbench-identity HEX]
cint debug locate [--json] PROGRAM.ci | RECORDING | CASE [--entry NAME [--args TYPED...]]
                  [--depth N] [--fuel N] [--changed BINDING | --when PREDICATE] [--save-case PATH]
                  [--receipt PATH] [--source-identity HEX] [--root DIR] [--cint PATH] [--out DIR | --sir DIR]
```

1. With a recording or a case and no other word, `cint debug` opens it in a read-only session (10.2). `replay` (10.3), `diff` (10.4) and `locate` (10.5) print their answer and end.
2. Every form replays on `cpu-sir-interp`, and `diff` also runs the case on the executor named by `--ref`. Replay takes every input of an entry from the file, and it performs no print: what the run printed is compared with the recording, not written again.
3. A command line that matches no form is a usage error, exit status 4. A file that cannot be read gives exit status 5, and a file that is not a complete recording or case gives exit status 3.
4. Under `--json`, tool errors are `CINT-TOOL-1` objects (7.7). `replay` and `diff` then print their receipt (12.4, 12.5) on standard output in place of the text, and `locate` prints the target in place of its report: a fault as a `CINT-FAULT-1` object, an error result as a `CINT-ERROR-1` object, or the change that `--changed` found as a `CINT-CHANGE-1` object; with no target it writes its text (10.5, paragraph 7).
5. `--source-identity HEX`, `--compiler-identity HEX`, `--interpreter-identity HEX` and `--workbench-identity HEX` give the source identities of the tools, 64 lowercase hexadecimal digits each, for a receipt to record. They change nothing else.

<a id="opening-a-recording-or-a-case"></a>

### 10.2 Opening a recording or a case

1. `cint debug FILE` starts a workbench session (clause 9) on the run that `FILE` holds and replays it to its end. It prints the banner with `debug FILE (read-only)` in place of `new session`, then `case:` or `recording:` with `entries N   boundaries B`, then `at the end of entry N, reached by replay`. The entries of the session are the recorded ones, numbered from 1, and the prompt shows the ordinal after the last.
2. The session is read-only. The commands that look at the run work: `:inspect`, `:type`, `:goto`, `:trace`, `:fault`, `:log`, `:effects`, `:replay NAME`, where the name `start` stands for the recording's start, and `:save-case`, which writes a case of the run from its start. An entry typed at the prompt, and every command that would change the session (`:checkpoint`, `:restore`, `:undo`, `:branch`, `:branches`, `:diff`, `:history`, `:watch`, `:run`, `:load`, `:open`, `:save`, `:fuel`, `:depth`, `:explain`, and `:backend` with an executor other than `cpu-sir-interp`), is refused with one line that says so, and the session goes on.
3. The session names each module variable with its module: a variable typed in a workbench session is `session.NAME`. An expression may use the plain name.
4. `--out DIR` is the directory the session works in, by default `build/debug` beside `FILE`. `--echo` writes each entry after its prompt, as the workbench does (9.1).
5. A fault report in a debug session has no source excerpt, and each of its `stack-source` lines ends with `(source not loaded)`, for a recording, which holds no source, and for a case alike.

Examples

6. The case `grow.cicase` that the workbench wrote in 9.5. It holds three entries: `rate = 2;` and two calls of `step()`:

```console
$ cint debug grow.cicase
cint workbench   profile cint-core-1   backend cpu-sir-interp   debug grow.cicase (read-only)
case: entries 3   boundaries 3
at the end of entry 3, reached by replay
ci[4]> :inspect level
session.level : I64
  exact   695
ci[4]> :goto 2
at the end of entry 2, reached by replay
ci[4]> :inspect level
session.level : I64
  exact   351
ci[4]> :trace 3
entry 3   session.ci: step()   evaluator cpu-sir-interp, by replay
  ord  position           operation              operands                 value
    0  session.ci:4:19    mul.checked.i64        351, 2                   702
    1  session.ci:4:26    sub.checked.i64        702, 7                   695
ci[4]> :replay start
replaying 2 entries from the recording's start to the current state (entry 1 to entry 2), comparing prints and outcomes with the recording
replay matches the recording at 2 boundaries
ci[4]> :backend cpu-c17
:backend cpu-c17 is not available in a debug session, which replays on cpu-sir-interp
ci[4]> :quit
```

7. The recording `partial.rec` of 8.3, which holds no source:

```console
$ cint debug partial.rec
cint workbench   profile cint-core-1   backend cpu-sir-interp   debug partial.rec (read-only)
recording: entries 1   boundaries 1
at the end of entry 1, reached by replay
ci[2]> :fault
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> partial.ci:3:13
   = operation: add.checked.i64
   = operand-count: 2
   = operand: I64 9223372036854775807
   = operand: I64 1
   = exact:   9223372036854775808
   = limit:   I64 9223372036854775807
   = revision: 2a5d25c827694f7bef7e0477ec0d760c95a7be976beaf854285a43555aeedef8
   = source-map: none
   = address: none
   = stack-depth: 0
   = state: the state the fault left, reached by replay (SPEC-01 IM-121)
   = noncanonical: not part of the fault record
   = workbench-position: entry 1
   = site: script#3
   = backend: cpu-sir-interp
ci[2]> :effects
new context
entry 1   the program entry
  print "partial: big=9223372036854775807\n"
  fault E_OVERFLOW   fuel 1   state cff0bda8
entries 1   prints 1   context resets 0   changes of revision set 0
ci[2]> :quit
```

<a id="cint-debug-replay"></a>

### 10.3 `cint debug replay`

1. `cint debug replay FILE` replays a recording or a case on `cpu-sir-interp` from its initial state, compares the state at each boundary with the recording's digest chain (11.2), and lists the boundaries.
2. It prints the file's header line (`case` or `recording`, the file, its boundaries and events, its mode and its density), the `revision` and `source-map` identities, then one line per boundary with its number, the index of its event, the entry, written as its function and arguments, the outcome and the fuel consumed. A statement typed in a workbench session is recorded as a call of a function `wb_entry`, and the program entry of a script or of `main` is shown as `the program entry`.
3. When every boundary agrees, it prints `replay     ok, K of B boundaries compared with the recording`, then `chain`, the head of the digest chain up to the last boundary replayed, and `state`, the state hash there; the exit status is 0. When a boundary differs, replay stops there, prints the boundary and what differed, and the exit status is 3.
4. `--to K` stops after boundary `K`, which shall be one of the file's boundaries; otherwise the exit status is 4. `--receipt PATH` writes a replay receipt (12.4).

Example

5. Replaying the case of 10.2 to its end and to its second boundary, and the recording of 8.3, whose one entry faulted:

```console
$ cint debug replay grow.cicase
case       grow.cicase   3 boundaries   9 events   mode full   density entry
revision   8d9f23f0901330065b452c169e537a3a180f567a17297b8616ca45a7f1e9f14e
source-map 8acd6e8377e85eac2e71a120316073bd03d172a3e8a022a2e89c1ce69cb1acd4
boundary  event  entry                          outcome                  fuel
       1      2  wb_entry()                     done                     1
       2      5  step()                         done                     1
       3      8  step()                         done                     1
replay     ok, 3 of 3 boundaries compared with the recording
chain      f0b2f70fff960186500385ef87eed8d3906da27071125e5476abc25211915c29
state      42592ed8fb48eb6500af5d9e59e08e9f4b50685614b26db21f97eb00daa2e0e0
$ cint debug replay grow.cicase --to 2
case       grow.cicase   3 boundaries   9 events   mode full   density entry
revision   8d9f23f0901330065b452c169e537a3a180f567a17297b8616ca45a7f1e9f14e
source-map 8acd6e8377e85eac2e71a120316073bd03d172a3e8a022a2e89c1ce69cb1acd4
boundary  event  entry                          outcome                  fuel
       1      2  wb_entry()                     done                     1
       2      5  step()                         done                     1
replay     ok, 2 of 3 boundaries compared with the recording
chain      9ff9df65ed047bbf27222afeff00b77b39cf0f927ffc98c22a9269595541a057
state      fe6985626d8d2e1fee13bb500442c2c9b97ecd0dfc9cdeecb1dd17536f6b0f7f
$ cint debug replay partial.rec
recording  partial.rec   1 boundary   3 events   mode full   density entry
revision   2a5d25c827694f7bef7e0477ec0d760c95a7be976beaf854285a43555aeedef8
source-map be83426b1103002101ac0403acbc26b062cb5b2128a72bd55a423f9911c2a12b
boundary  event  entry                          outcome                  fuel
       1      2  the program entry              faulted E_OVERFLOW       1
replay     ok, 1 of 1 boundary compared with the recording
chain      49942e33499538df87f7e49b392c1c5aad6c557471959c567107f56b46ac400a
state      cff0bda8706611d5a578aadb44ae46f13e6047e63020326774cb32e69f8b1dfa
$ cint debug replay grow.cicase --to 5
cint debug: error: grow.cicase has 3 boundaries; --to takes one of them
$ echo $?
4
```

<a id="cint-debug-diff"></a>

### 10.4 `cint debug diff`

`cint debug diff` shows whether two executors give a case the same meaning. It runs the case on a reference executor and on a candidate from the same initial state with the same inputs, and reports agreement or the first difference.

1. `--ref` names the reference and `--cand` the candidate, each `cpu-c17` or `cpu-sir-interp`; the defaults are `cpu-c17` and `cpu-sir-interp`. Any other name is a usage error, exit status 4.
2. On `cpu-c17`, the case's sources are built into a library with `cint build --lib` (5.5), in `--out DIR`, by default `build/debug` beside the case; `--cint PATH` names the `cint` to use, by default the one in the directory that holds the `cint-debug` executable. A library has no program entry, so a case whose entries call the program entry of a script or of `main` cannot run on `cpu-c17`: the exit status is 5, and the message names `--ref cpu-sir-interp`. A case that the workbench wrote calls exported functions and runs on both.
3. The procedure:
   - The case is checked first: its digests, the order and the count of its events. A case that fails is the outcome `invalid-case`.
   - Both executors run each entry from the case's initial state with the case's inputs, and the digest of the state at each boundary is compared.
   - At the first boundary whose digests differ, or at which one run faulted and the other did not, or both faulted with different fault records, the state is compared binding by binding in declaration order, and within an array element by element, and the first difference is reported with both values exactly.
   - The candidate runs that boundary once more. A difference that does not happen again is reported as `unreproduced`, never as a difference.
4. The outcome is the first of these that applies:

| Outcome | Meaning |
| --- | --- |
| `invalid-case` | The case failed the check of paragraph 3. |
| `refused` | The candidate refused the case with `E_UNSUPPORTED`. |
| `unreproduced` | The candidate's difference did not happen again when it ran the boundary once more. |
| `fault-difference` | One run faulted and the other did not, or the two fault records differ. |
| `value-difference` | The state differs: the boundary, the binding, the element and both values. |
| `agree` | Every boundary's state is equal, and so are the results and the fault records. |

5. The text form gives the case, its boundaries and density, the `reference` and the `candidate`, the boundaries compared and the `outcome`, followed by the details of a difference. The exit status is 0 for `agree` and 3 for every other outcome. `--receipt PATH` writes a diff receipt (12.5).
6. Agreement is checked at boundaries only. Two runs that differ inside an entry and agree again at its end are not told apart.

Example

7. The workbench's case compared on the two executors, and the case that the interpreter wrote in 8.3, which calls the program entry:

```console
$ cint debug diff grow.cicase
case       grow.cicase   3 boundaries   density entry
reference  cpu-c17
candidate  cpu-sir-interp
boundaries 3 of 3 compared
outcome    agree
$ echo $?
0
$ cint debug diff partial.cicase
cint debug: error: the case calls the program entry, which a cpu-c17 library does not have; compare it with --ref cpu-sir-interp
$ echo $?
5
$ cint debug diff --ref cpu-sir-interp partial.cicase
case       partial.cicase   1 boundary   density entry
reference  cpu-sir-interp
candidate  cpu-sir-interp
boundaries 1 of 1 compared
outcome    agree
```

<a id="cint-debug-locate"></a>

### 10.5 `cint debug locate`

`cint debug locate` finds the boundary where a run went wrong and shows how the run got there: the fault report, the boundary before it and the last operations counted before the fault.

1. Given a program, `locate` translates it with `cint emit-c` into `--out DIR`, by default `build/debug` under the root, or reads an output set already written from `--sir DIR`, and runs it on `cpu-sir-interp` under a recording. It runs the program entry, or, with `--entry NAME`, the exported function `NAME` of the root module, a usage error when the module exports none, called with the typed values of `--args`, written `I64 9` or `Bool true`. The entry runs with no fuel allowance and a call-depth limit of 256, as the workbench's entries do; `--fuel N` and `--depth N` set them. `--cint PATH` names the `cint` to translate with, by default the one in the directory that holds the `cint-debug` executable.
2. Given a recording or a case, `locate` replays it.
3. The target is the first boundary that ends in a fault or with an error result. `locate` prints `program`, `recording` or `case` with what it ran, `target` with the boundary and how it was found, and `bracket`, the boundary before the target and the target. For a fault, the fault report of 7.3 follows, with the lines of 7.3, paragraph 5, and then the last 20 counted operations of the entry, with each one's position, operation, operands and value. For an error result, the error line of 7.5 follows, with the path the error took: where it was returned and each call that passed it on with `try`, innermost first.
4. `--changed BINDING` makes the target the last boundary at which the module variable `BINDING` changed, found by replaying backward from the end. `target` then gives the binding's value before and after. Under `--json` the answer is one `CINT-CHANGE-1` object with the keys `schema`, `binding` (qualified by its module), `boundary` (a decimal string) and `from` and `to` (typed values, as in `CINT-FAULT-1`). A `BINDING` that the recording's state does not have is a usage error, exit status 4.
5. `cint debug locate` does not search by predicate, and `--when PREDICATE` is refused with exit status 5. The workbench searches by predicate with `:replay NAME --when PRED` (9.5).
6. `--save-case PATH` also writes the run of a program as a case, with each module's source, and `--receipt PATH` writes the replay receipt of the replay (12.4).
7. The exit status is 0 when a target was found, and 3 when there is none or when replay differs from the recording. When there is no target, `locate` writes two lines of text, the `program`, `recording` or `case` line and `target     none: ...`, with or without `--json`.

Examples

8. A fault inside a call, an error result and a run with no target, in the modules `money.ci` (Annex A.1) and `ledger.ci` (5.4):

```console
$ cint debug locate money.ci --entry overflow
program    money.ci   evaluator cpu-sir-interp, under a recording
target     boundary 1 of 1, overflow(): the first fault, reproduced by replay
bracket    the initial state; then boundary 1
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> money.ci:13:14
   |
13 |     return a + b;
   |              ^ add.checked.i64
   |
   = operation: add.checked.i64
   = operand-count: 2
   = operand: I64 9223372036854775800
   = operand: I64 12
   = exact:   9223372036854775812
   = limit:   I64 9223372036854775807
   = revision: 602173cd38defe996814e3298bd2a45c715a1cea53e3b5f8f84f6d073777ebde
   = source-map: none
   = address: none
   = stack-depth: 1
   = called from: money.ci:23:12
   = fuel-consumed: 2 (not part of the fault record)
   = state: the post-fault state of SPEC-01 IM-121, reached by replay
   = noncanonical: not part of the fault record
   = stack-source: money.ci:23:12
   |
23 |     return add_cents(big, 12);
   |            ^
   |
   = workbench-position: boundary 1
   = site: add_cents#10
   = backend: cpu-sir-interp, by replay
the last 2 counted operations of boundary 1, of 2 trace events (:trace shows them all)
  ord  position           operation              operands                 value
    0  money.ci:23:12     call                   9223372036854775800, 12
    1  money.ci:13:14     add.checked.i64        9223372036854775800, 12  E_OVERFLOW
$ cint debug locate ledger.ci
program    ledger.ci   evaluator cpu-sir-interp, under a recording
target     boundary 1 of 1, the program entry: the first error result, reproduced by replay
bracket    the initial state; then boundary 1
error: ledger.LedgerError.overdrawn (tag 1)
the path, innermost first, from the calls and returns of the replay:
  withdraw returned the error (tag 1)
  main passed it on with try (tag 1), from its call of withdraw at ledger.ci:13:17
$ cint debug locate money.ci --entry priced
program    money.ci   evaluator cpu-sir-interp, under a recording
target     none: no boundary ends in a fault or an error result; 1 replayed and compared with the recording
$ echo $?
3
$ cint debug locate --json money.ci --entry priced
program    money.ci   evaluator cpu-sir-interp, under a recording
target     none: no boundary ends in a fault or an error result; 1 replayed and compared with the recording
```

9. A call-depth fault, located with arguments and a depth limit, and saved as a case with a receipt:

```console
$ cint debug locate deep.ci --entry down --args 'I64 10' --depth 4 --save-case down.cicase --receipt down.json
program    deep.ci   evaluator cpu-sir-interp, recorded as the case down.cicase
target     boundary 1 of 1, down(I64 10): the first fault, reproduced by replay
bracket    the initial state; then boundary 1
fault[E_DEPTH]: call depth limit exceeded
  --> deep.ci:5:16
   |
 5 |     return 1 + down(n - 1);
   |                ^ call.enter
   |
   = operation: call.enter
   = operand-count: 0
   = exact:   none
   = limit:   I64 4
...
the last 8 counted operations of boundary 1, of 8 trace events (:trace shows them all)
  ord  position           operation              operands                 value
    0  deep.ci:5:23       sub.checked.i64        10, 1                    9
    1  deep.ci:5:16       call                   9
    2  deep.ci:5:23       sub.checked.i64        9, 1                     8
    3  deep.ci:5:16       call                   8
    4  deep.ci:5:23       sub.checked.i64        8, 1                     7
    5  deep.ci:5:16       call                   7
    6  deep.ci:5:23       sub.checked.i64        7, 1                     6
    7  deep.ci:5:16       call                   6                        E_DEPTH
```

The lines left out are the rest of the fault report: its three `called from` lines, each with its `stack-source` excerpt.

10. The last change of a module variable in the case of 10.2, in both forms:

```console
$ cint debug locate grow.cicase --changed level
case       grow.cicase   3 boundaries
target     boundary 3 of 3, step(): the last change of session.level, from 351 to 695
bracket    boundary 2, step(): done; then boundary 3
the last 2 counted operations of boundary 3, of 2 trace events (:trace shows them all)
  ord  position           operation              operands                 value
    0  session.ci:4:19    mul.checked.i64        351, 2                   702
    1  session.ci:4:26    sub.checked.i64        702, 7                   695
$ cint debug locate --json grow.cicase --changed rate
{"binding":"session.rate","boundary":"1","from":{"t":"I64","v":"3"},"schema":"CINT-CHANGE-1","to":{"t":"I64","v":"2"}}
$ cint debug locate grow.cicase --changed nosuch
cint debug: error: no global nosuch in the recording's state
$ echo $?
4
```

<a id="checkpoints-recordings-and-cases"></a>

## 11 Checkpoints, recordings and cases

The tools keep runs in three kinds of file. A *checkpoint* holds one state. A *recording* holds a whole run: the code, the state it started from and every entry with its inputs, its effects and its outcome, so that the run can be replayed. A *case* is a recording that also holds the source, so that it can be compared and shown on another machine. SPEC-01P.3 specifies the bytes of each; this clause says what they hold and how the tools use them.

<a id="checkpoints"></a>

### 11.1 Checkpoints

1. A checkpoint holds the canonical state of a context at one boundary: its module variables, its buffers and arenas and the fault record when the context faulted. It names the revision identity of the program whose state it is, and it can be restored only into a program with that revision identity. The locals of a running function are not canonical state, so a checkpoint is taken only at a boundary, never inside an entry.
2. `cint interp replay --checkpoint PATH` writes the state where the replay stopped as a checkpoint file. A workbench checkpoint (9.5) is kept in the session; it also holds the code of the session's revisions and the per-entry settings.
3. A checkpoint file begins with its format, `cint-core-1/checkpoint/v1`. `cint interp show` prints it as text: the format, the runtime contract, the canonical state with its revision, profile and fuel model, the identifier counters, the fault record when there is one, the count of effects and `state-hash`, the state hash of clause 6.

<a id="recordings-and-cases"></a>

### 11.2 Recordings and cases

1. A recording holds:
   - a header: its format, `cint-core-1/recording/v1`, the runtime contract, the revision identity and the source-map identity of the code, the mode (`full`), how often the state is hashed (`every entry`) and the density (11.3);
   - the code: for each module, its path, its SIR text with its table of sites, and, in a case, its source;
   - the state the run began from, as a checkpoint, with its state hash as `initial-state`;
   - the events of the run, in order: for each entry an `entry` event, with the function or the program entry, its arguments, its fuel allowance, its call-depth limit and its frame-arena capacity; an `effect` event for each print, with its site and its bytes, and for each call of an `observe` extern, with its site, the name of the extern or the service operation, its arguments and what it returned; and an `outcome` event, with the outcome, the fault record when the entry faulted, the fuel consumed and the state hash at the boundary;
   - last, an `end` event with the head of the digest chain and the count of events.
2. The *digest chain* links the boundaries. Its first digest covers the initial state, and each boundary's digest covers the digest before it, the boundary's event, the state hash there and the fault record when the entry faulted. The head of the chain thus depends on every boundary of the run, and replay recomputes the chain as it goes and stops at the first boundary whose digest differs.
3. A case is a recording in the format `cint-core-1/case/v1` that holds the source of every module. Its source is used for displays and for building it on `cpu-c17`; what replay runs is its SIR text, whose revision identity is checked first.
4. The interpreter writes a recording with `cint interp run --record` and a case with `cint interp run --case` (8.2). The workbench records every entry of a session and writes a case with `:save-case` (9.6), and `cint debug locate --save-case` writes the run it located (10.5).
5. A recording is written in order, and its `end` event is written last. A file without it, or whose events, digests or counts do not agree, is not a complete recording: every tool rejects it as a whole, with exit status 3, and nothing in it is used.
6. A recording or a case is data. Opening one runs no code except by replay of the code it holds, and replay performs no effect: every input comes from the file, every print is compared with the recorded bytes, and each extern call is compared with the recorded call and given the recorded result, without calling the host.

Example

7. The recording, the checkpoint and the case that 8.3 wrote, shown as text:

```console
$ cint interp show partial.rec
recording cint-core-1/recording/v1
contract cint-rt-3
revision 2a5d25c827694f7bef7e0477ec0d760c95a7be976beaf854285a43555aeedef8
source-map be83426b1103002101ac0403acbc26b062cb5b2128a72bd55a423f9911c2a12b
mode full
state-hash every entry
density entry
module partial.ci sir 748 sha256 b6dd6dc4d37dd2d3ac5af2d065a795cab0db8d03b749c7cd949a1eed8af6e81a source absent
initial-state 55c236bfbae5a4346437db7a1c7cb6095cb0ab7d3ee2b480924b9ceb500f48df
event 0 entry 1 program fuel -1 depth 256 frame-arena 16777216
event 1 effect 0 site 0.2 print status 0 "partial: big=9223372036854775807\x0a"
event 2 outcome fault fuel 1 allowance -1 state cff0bda8706611d5a578aadb44ae46f13e6047e63020326774cb32e69f8b1dfa
fault E_OVERFLOW add.checked.i64
fault-operand I64 9223372036854775807
fault-operand I64 1
fault-exact Z 9223372036854775808
fault-limit I64 9223372036854775807
fault-position partial.ci:3:13
fault-revision 2a5d25c827694f7bef7e0477ec0d760c95a7be976beaf854285a43555aeedef8
fault-source-map absent
event 3 end host chain 49942e33499538df87f7e49b392c1c5aad6c557471959c567107f56b46ac400a events 3
$ cint interp show partial.ckpt
checkpoint cint-core-1/checkpoint/v1
contract cint-rt-3
payload-digest absent
build-record-digest absent
build-identity absent
state cint-core-1/state/v1
revision 2a5d25c827694f7bef7e0477ec0d760c95a7be976beaf854285a43555aeedef8
profile cint-core-1
fuel-model fuel-v1
arena-block 0
pool-block 0
counters next-buffer 1 next-arena 1 next-pool 1 entries 1
fault E_OVERFLOW add.checked.i64
fault-operand I64 9223372036854775807
fault-operand I64 1
fault-exact Z 9223372036854775808
fault-limit I64 9223372036854775807
fault-position partial.ci:3:13
fault-revision 2a5d25c827694f7bef7e0477ec0d760c95a7be976beaf854285a43555aeedef8
fault-source-map absent
effects 1
state-hash cff0bda8706611d5a578aadb44ae46f13e6047e63020326774cb32e69f8b1dfa
$ cint interp show partial.cicase | head -8
case cint-core-1/case/v1
contract cint-rt-3
revision 2a5d25c827694f7bef7e0477ec0d760c95a7be976beaf854285a43555aeedef8
source-map be83426b1103002101ac0403acbc26b062cb5b2128a72bd55a423f9911c2a12b
mode full
state-hash every entry
density entry
module partial.ci sir 748 sha256 b6dd6dc4d37dd2d3ac5af2d065a795cab0db8d03b749c7cd949a1eed8af6e81a source 69
$ head -c 1000 partial.rec > cut.rec
$ cint interp show cut.rec
cint-interp: cut.rec is not a checkpoint that a restore accepts, nor a complete recording
$ echo $?
3
```

The `fuel -1` and `allowance -1` of the recording stand for no fuel allowance, and `frame-arena 16777216` is the frame arena of 2,097,152 elements, in bytes. The state hash of the `outcome` event equals the checkpoint's `state-hash`, since the replay that wrote the checkpoint stopped at that boundary, and the case differs from the recording only in its format and in holding the 69 bytes of `partial.ci`.

<a id="boundaries-and-density"></a>

### 11.3 Boundaries and density

1. A boundary is where a recording digests the state. At the density `entry`, the default, there is one boundary at the end of each entry, including an entry that faulted, whose boundary holds the state the fault left and the fault record.
2. At the density `dispatch`, chosen with `cint interp run --boundary dispatch`, each kernel dispatch inside an entry also has a boundary after it publishes its outputs. The end of an entry whose last action is such a publication is one boundary, not two.
3. Replay can reach every boundary, and the interpreter can trace every counted operation of an entry by replaying it. A point inside an entry is reached by replaying that entry, never by restoring a checkpoint taken there. A program that runs all its work in one entry, such as one `main`, has one boundary; a program meant to be inspected step by step runs one entry per step, as the workbench's `:run step() N` does.

<a id="what-replay-does-not-do"></a>

### 11.4 What replay does not do

1. Replay reaches only what was recorded. `cint run` makes no recording, and a run under it cannot be replayed; `cint interp run --record` and the workbench make recordings.
2. Replay does not rewind the outside world. A restore or a replay does not take back what the run printed or wrote.
3. Replay runs the recorded code. A recording replays under the revisions it holds, never under code edited since.
4. Reaching a boundary costs the replay of every entry before it, from the start of the recording or from the checkpoint the replay starts at.

<a id="receipts"></a>

## 12 Receipts

A receipt is a canonical JSON file that records a run of a tool: what went in, what came out, and what the machine was. Two receipts that agree on the first two parts show the same result on different machines, whatever the machines were. SPEC-09P.3 11.2 specifies the format and the receipt identity that every receipt shares; this clause specifies the receipts that the tools of this document write.

<a id="format"></a>

### 12.1 Format

1. A receipt is one JSON object written as the bytes of the JSON Canonicalization Scheme (RFC 8785) with one exception, followed by one LF: keys sorted, no space outside strings and integers only, with a value outside plus or minus 2^53 - 1 written as a decimal string. It has three parts. `identity` records the inputs, `outcome` the result and `observations` the machine, the timings and the paths, which can differ between two runs with the same result. `observations.receipt_identity_sha256` is the receipt identity, the SHA-256 digest of the canonical bytes of the object `{"identity": ..., "outcome": ...}`.
2. The exception is the escaping of strings. A receipt holds only ASCII characters: every character outside ASCII is written `\u` with four lowercase hexadecimal digits, a character above U+FFFF as its UTF-16 surrogate pair, where RFC 8785 writes the character itself. The other escapes are those of RFC 8785: `\"`, `\\`, `\b`, `\t`, `\n`, `\f` and `\r` and `\u00XX`, in lowercase, for the other control characters; `/` and U+007F are written as themselves. The receipts of SPEC-09P.3 11.2, which the Python tools of the source tree write, differ in one more character: they write U+007F as `\u007f`.
3. A receipt written this way is not the output of RFC 8785 when a string holds a character outside ASCII. When no string holds U+007F, the bytes are those that Python's `json.dumps(receipt, sort_keys=True, separators=(",", ":"))` writes, which escapes the other characters the same way; `json.dumps` writes U+007F as `\u007f`.
4. Some integers of a receipt are JSON numbers, such as an exit status, a line, a call-depth limit and the size of standard output. Others are decimal strings, such as fuel, boundary numbers, timings and the size of a recording or a case. The form of each key is fixed and does not depend on its value.

<a id="what-every-receipt-records"></a>

### 12.2 What every receipt records

1. `identity.schema` is `cint-receipt-1/KIND`, and `identity.kind` is `KIND`: `run`, `build` and `test` (12.3) for the `cint` command, `replay` (12.4) and `diff` (12.5) for the interpreter and `cint debug`.
2. `identity.profile` is `cint-core-1`. `observations.machine` or `observations.host` names the operating system, and `observations.timings` holds durations in nanoseconds, under keys that end in `_ns`.
3. `observations.limits` is a sentence that says what the receipt does not show.

<a id="run-build-and-test-receipts"></a>

### 12.3 Run, build and test receipts

1. `cint run`, `cint build` and `cint test` write these keys:

| Key | Content |
| --- | --- |
| `identity.tool` | `name` (`cint`), `version`, and `compiler_source_identity`, the digest of the source manifest of the compiler (SPEC-09P.3 11.2). |
| `identity.source` | `root_module`; `files`, the SHA-256 of each module read, by module path, with a module of the standard library root named `stdlib:PATH`; `revision` and `dirty`, the version-control revision of the source and whether it had changes, or `null` when there is no repository. |
| `identity.generated` | `files`, the SHA-256 of each file of the output set, and `manifest`, the digest of its `MANIFEST` (5.7). |
| `identity.runtime` | `contract` (`cint-rt-3`) and `files`, the SHA-256 of each runtime source compiled with the program. |
| `identity.backend` | `id`: `cpu-c17`. |
| `identity.encodings` | `fault`: the version of the fault record, `cint-core-1/fault/v2`. |
| `identity.method` | The semantic inputs of 5.3, in a run or test receipt: `depth_limit`; `frame_arena_bytes`, the frame arena in force in bytes, eight per element (16,777,216 by default, and eight times the count of `--frame-arena`); `fuel_budget` (`none`, or the allowance as a decimal string); `fuel_model` (`fuel-v1`); `grants`, the capability text of each `--grant` line, the part before its `=`, in byte order without repeats, `[]` for none; and `stdin`, `"inherited"` when the grants include `console.input` given by a `--grant` line and `"null"` otherwise. A grant's host directory is not recorded, since it is a path of the machine. The grants that a run takes from its program (5.4, paragraph 11) and the program arguments are not recorded. In a build receipt it is `null`, or `{"frame_arena_bytes": N}` when `--frame-arena` was given. |
| `identity.identities` | `build`, `execution`, `meaning`, `revision` and `source_map`, each `null` in cint 0.2.4. |
| `identity.lookups` | In build and test receipts: each module path that an import looked up under the root, with `true` when the file was there, and each it then looked up in the standard library root, named `stdlib:PATH` [RCPT-07]. |
| `outcome.kind` | `run`: `value` (the program ended normally), `fault`, `error` or `compile-error`. `build`: `built`. `test`: `pass` or `fail`. |
| `outcome.exit_status` | The exit status of 5.2. |
| `outcome.diagnostics` | The diagnostics, each with `code`, `file`, `line` and `column`; `diagnostics_not_retained` counts those left out. |
| `outcome.fault`, `outcome.error` | `run`: the `CINT-FAULT-1` object without `noncanonical`, or `null`; the `CINT-ERROR-1` object when `main` returned an error. |
| `outcome.fuel_consumed`, `outcome.stdout` | `run`: the fuel the entry consumed, and `bytes` and `sha256` of what the program wrote to standard output. |
| `outcome.tests` | `test`: one object per test, with the keys of `CINT-TEST-1` (7.6) other than `schema`. |
| `observations` | `cache`, the directory of the build (5.3); `cc`, the host C compiler's `name`, `version`, `flags` and `log`; `executables`, the SHA-256 of `cint`, of the runtime library and of the program; `host`; `started_utc` and `finished_utc`, timestamps in the form of RFC 3339; `timings`, `build_ns`, `cc_ns` and `run_ns`. |

2. Nothing that depends on the machine, the C compiler or the path of the cache is in `identity` or `outcome`, so one program run with the same inputs on two machines gives one receipt identity.
3. `observations.cache` records the cache directory as it was given, by `--cache` or by the toolchain file, so it can hold a path of the machine, such as one under a home directory.

Note

4. A receipt of a run with a relative `--cache` path holds no path of the machine there, and can be shared as it is.

<a id="replay-receipts"></a>

### 12.4 Replay receipts

1. `cint interp replay --receipt`, `cint debug replay --receipt` and `cint debug locate --receipt` write a replay receipt:

| Key | Content |
| --- | --- |
| `identity.backend` | `id`: `cpu-sir-interp`. |
| `identity.recording`, `identity.case` | For a recording or a case, its size in bytes and its SHA-256. |
| `identity.identities` | `revision` and `source_map`, the identities of the code replayed. |
| `identity.method` | `density`, `fuel_model`, `mode`, `state_hash_interval` and `stop`, the boundary replay stopped after or `0` for the end. |
| `identity.runtime_contract` | `cint-rt-3`. |
| `identity.tool` | `interpreter_source_identity`, from `--source-identity`, or `null`. |
| `outcome.status`, `outcome.event`, `outcome.what` | `ok` when replay agreed with the recording to its end, or the status that stopped it; the index of the event at which replay ended; and what differed, or `null`. |
| `outcome.boundaries`, `outcome.chain_head`, `outcome.state_sha256` | The boundaries replayed, the head of the digest chain and the state hash at the last of them. |
| `outcome.fault_sha256` | The SHA-256 of the last fault record, or `null`. |
| `outcome.fuel_consumed` | The fuel consumed at each boundary. |
| `observations.checkpoints` | `cint debug` only: for each boundary, the elements a checkpoint of the state there holds (a variable counts one, an array its elements) and the time replay took to reach it. |

2. A file replayed on two machines gives one receipt identity.

<a id="diff-receipts"></a>

### 12.5 Diff receipts

1. `cint debug diff --receipt` writes a diff receipt:

| Key | Content |
| --- | --- |
| `identity.backend` | `reference` and `candidate`, each with its `id`. |
| `identity.case`, `identity.identities`, `identity.runtime_contract` | As in a replay receipt. |
| `identity.method` | `density` and `fuel_model`. |
| `identity.tool` | `compiler_source_identity`, `interpreter_source_identity` and `workbench_source_identity`, from the options of 10.1, paragraph 5, or `null`. |
| `outcome.outcome` | The outcome of 10.4. |
| `outcome.boundaries`, `outcome.compared` | The case's boundaries and those compared. |
| `outcome.boundary`, `binding`, `element`, `reference`, `candidate`, `differs`, `rerun`, `why` | The details of a difference, `null` or empty for `agree`. |
| `observations.backend` | The build configuration of each executor: for `cpu-c17`, the C compiler, its version and its flags. |

2. The configuration of the C compiler is an observation, so one comparison has one receipt identity with every toolchain.

<a id="reading-a-receipt"></a>

### 12.6 Reading a receipt

Example

1. The parts of the receipt of 5.3, its method and outcome, its identity computed again from its bytes, and a cache path outside ASCII as a receipt writes it:

```console
$ python3 -c 'import json; r = json.load(open("hello.json")); [print(p + ":", " ".join(sorted(r[p]))) for p in ("identity", "outcome", "observations")]'
identity: backend encodings generated identities kind method profile runtime schema source tool
outcome: diagnostics diagnostics_not_retained exit_status fault fuel_consumed kind stdout
observations: cache cc executables finished_utc host limits receipt_identity_sha256 started_utc timings
$ python3 -c 'import json; r = json.load(open("hello.json")); print(json.dumps(r["identity"]["method"])); print(json.dumps(r["outcome"]))'
{"depth_limit": 256, "frame_arena_bytes": 16777216, "fuel_budget": "none", "fuel_model": "fuel-v1", "stdin": "null"}
{"diagnostics": [], "diagnostics_not_retained": 0, "exit_status": 0, "fault": null, "fuel_consumed": "1", "kind": "value", "stdout": {"bytes": 23, "sha256": "0d3f756a0f6c68f4cee819726e6ddf3f969bb5b63c827eb66a1166eba9104b57"}}
$ python3 -c 'import hashlib, json; r = json.load(open("hello.json")); b = json.dumps({"identity": r["identity"], "outcome": r["outcome"]}, sort_keys=True, separators=(",", ":")).encode(); print(hashlib.sha256(b).hexdigest()); print(r["observations"]["receipt_identity_sha256"])'
bfba045301d788cc9bd64ff0055520ce74fe031bcb5c93a07b02f109ae7eb275
bfba045301d788cc9bd64ff0055520ce74fe031bcb5c93a07b02f109ae7eb275
$ python3 -c 'import json; r = json.load(open("hello.json")); print(json.dumps(r, sort_keys=True, separators=(",", ":")) + "\n" == open("hello.json").read())'
True
$ cint run --cache cachée --receipt r2.json hello.ci
hello, world
answer=42
receipt: r2.json
$ grep -o '"cache":"[^"]*"' r2.json
"cache":"cach\u00e9e/6d8cb55c098d7a0d2ef3dc1a852e416b04a06fc851142c6a608cbbf1f50b9806/run-0"
```

The two digests are equal: the receipt identity is the digest of the canonical bytes of `identity` and `outcome`. `stdin` is the string `"null"`, which names the null device of 5.3.

<a id="future-directions"></a>

## 13 Future directions

This clause is informative. It names tools and features that cint 0.2.4 does not include.

1. *A project manifest.* A declarative file, with no conditionals or loops, that gives a project its name, version, targets, embedded data and foreign libraries, each value an input of the build.
2. *`cint fmt`.* A formatter with one style and no options that rewrites source without changing its revision identity, so that checkpoints still restore after formatting, and refuses to write a file whose meaning changes.
3. *`cint doc`.* A documentation generator that describes every export with its signature and the faults it can raise, from what `cint explain` knows, and runs the examples in its comments as tests.
4. *A build cache.* A cache that keys each build action by the digests of its inputs, so that an unchanged module is not translated again. A file taken from the cache is byte-identical to a fresh one. The kept executable of `cint run` (5.4, paragraph 12) is the one use of it so far.
5. *`cint bench`.* A benchmark runner that times blocks of code by a declared method, with warm-up runs, samples and nearest-rank percentiles in integer nanoseconds, and rejects a run whose samples do not end in the same state.
6. *A language server.* A server for the Language Server Protocol that gives an editor the compiler's diagnostics, the exact value of a constant and the operation, rounding and faults of each operator.
7. *Grants in receipts and sessions.* A receipt that records the grants a run takes from its program and its program arguments, and workbench sessions that load programs with externs and grants.
8. *Device steps in the workbench.* Workbench commands that run kernels on a `gpu-cuda` library, place a module's buffers on a device and back with an explicit command, and compare such a run with `cpu-c17` in `cint debug diff`.

<a id="transcripts"></a>

## Annex A (informative) Transcripts

The transcripts of this annex were produced by the tools of cint 0.2.2 or later, as the others in this document were. They continue the transcripts before them in the same directory.

<a id="exact-money-in-the-workbench"></a>

### A.1 Exact money in the workbench

A session that prints, sums prices held in whole cents, shows a sum as dollars, compares two rounding modes on a tie, replaces a function and goes back to the earlier revision:

```console
$ cint workbench
cint workbench   profile cint-core-1   backend cpu-sir-interp   new session
ci[1]> "hello, world\n";
hello, world
ci[2]> I64[3] cents = [1999, 425, 10];
cents : I64[3]   3 elements   host
  [0] = 1999
  [1] = 425
  [2] = 10
ci[3]> I64 subtotal = sum(cents);
subtotal = 2434 : I64
ci[4]> :inspect subtotal /scale 100
subtotal : I64   shown with scale 100
  exact   24.34
  raw     2434
ci[4]> div_round(200 * 825, 10000, half_even)
= 16 : I64   inexact: div_round half_even (exact quotient 16.5)
ci[5]> div_round(200 * 825, 10000, half_up)
= 17 : I64   inexact: div_round half_up (exact quotient 16.5)
ci[6]> I64 total_with_tax[n](in I64[n] c, I64 rate_bp) {
..>     I64 s = sum(c);
..>     return s + div_round(s * rate_bp, 10000, half_even);
..> }
total_with_tax : (I64, view I64[%n], I64) -> I64   revision session@r3
ci[7]> total_with_tax(cents, 825)
= 2635 : I64
ci[8]> :checkpoint priced
checkpoint priced   #1   bindings 2   revisions: session@r3
ci[8]> I64 total_with_tax[n](in I64[n] c, I64 rate_bp) {
..>     I64 s = sum(c);
..>     return s + div_round(s * rate_bp, 10000, half_up);
..> }
total_with_tax : (I64, view I64[%n], I64) -> I64   revision session@r4 (replaces r3)
ci[9]> total_with_tax([200], 825)
= 217 : I64
ci[10]> :restore priced
saved current state as auto-1
restored priced   #1   total_with_tax is revision session@r3
ci[10]> total_with_tax([200], 825)
= 216 : I64
ci[11]> :history total_with_tax
r3  entry 6   in checkpoints: priced
r4  entry 8   in checkpoints: auto-1
ci[11]> :save-case priced.cicase
note: the case includes every recorded observe result verbatim; this case has none
case priced.cicase   checkpoint priced   entry 10   effects 0   density entry
ci[11]> :save session-a
saved session to session-a   entries 10   checkpoints 2   objects 54
ci[11]> :quit
```

`half_even` rounds the exact quotient 16.5 to its even neighbor, 16, and `half_up` rounds it up, to 17; the session says that each result is inexact and gives the exact quotient. The module `money.ci` holds the same function with others, and `cint explain` (9.9) and `cint debug locate` (10.5) use it:

```console
$ cat money.ci
// money.ci: amounts in whole cents, summed exactly and rounded once.

export I64 total_with_tax[n](in I64[n] c, I64 rate_bp) {
    I64 s = sum(c);
    return s + div_round(s * rate_bp, 10000, half_even);
}

export I64 tax(I64 subtotal, I64 rate_bp) {
    return div_round(subtotal * rate_bp, 10000, half_even);
}

export I64 add_cents(I64 a, I64 b) {
    return a + b;
}

export I64 priced() {
    I64[3] cents = [1999, 425, 10];
    return total_with_tax(cents, 825);
}

export I64 overflow() {
    I64 big = 9223372036854775800;
    return add_cents(big, 12);
}

test "the tie at 16.5 cents rounds to even" {
    assert(tax(200, 825) == 16);
}
```

<a id="from-a-test-to-a-located-fault"></a>

### A.2 From a test to a located fault

The tests of `money.ci` pass, and its function `overflow` faults. `cint debug locate` finds the fault and saves the run as a case, `cint debug diff` shows that both executors give the case the same fault, and a session on the case shows the fault, the operations that led to it and what the run recorded:

```console
$ cint test money.ci
test money.ci:26 "the tie at 16.5 cents rounds to even" ... ok
1 of 1 tests passed
$ cint debug locate money.ci --entry overflow --save-case overflow.cicase
program    money.ci   evaluator cpu-sir-interp, recorded as the case overflow.cicase
target     boundary 1 of 1, overflow(): the first fault, reproduced by replay
bracket    the initial state; then boundary 1
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> money.ci:13:14
...
$ cint debug diff overflow.cicase
case       overflow.cicase   1 boundary   density entry
reference  cpu-c17
candidate  cpu-sir-interp
boundaries 1 of 1 compared
outcome    agree
$ cint debug overflow.cicase
cint workbench   profile cint-core-1   backend cpu-sir-interp   debug overflow.cicase (read-only)
case: entries 1   boundaries 1
at the end of entry 1, reached by replay
ci[2]> :fault
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> money.ci:13:14
   = operation: add.checked.i64
   = operand-count: 2
   = operand: I64 9223372036854775800
   = operand: I64 12
   = exact:   9223372036854775812
   = limit:   I64 9223372036854775807
   = revision: 602173cd38defe996814e3298bd2a45c715a1cea53e3b5f8f84f6d073777ebde
   = source-map: none
   = address: none
   = stack-depth: 1
   = called from: money.ci:23:12
   = state: the state the fault left, reached by replay (SPEC-01 IM-121)
   = noncanonical: not part of the fault record
   = stack-source: money.ci:23:12 (source not loaded)
   = workbench-position: entry 1
   = site: add_cents#10
   = backend: cpu-sir-interp
ci[2]> :trace fault
entry 1   money.ci: overflow()   evaluator cpu-sir-interp, by replay
  ord  position           operation              operands                 value
    0  money.ci:23:12     call                   9223372036854775800, 12
    1  money.ci:13:14     add.checked.i64        9223372036854775800, 12  E_OVERFLOW
ci[2]> :effects
new context
entry 1   money.ci: overflow()
  fault E_OVERFLOW   fuel 2   state 9bde50f2
entries 1   prints 0   context resets 0   changes of revision set 0
ci[2]> :quit
```

The rest of the `locate` report is that of 10.5, paragraph 8. The diff agrees because both executors raise the same fault, with the same operands, at the same operation. The session's fault report has no source excerpt even though the case holds the source.
