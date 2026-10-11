<a href="https://integerc.dev/"><img src="docs/cint-wordmark.svg" width="320" alt="cint"></a>

# cint

cint is a compiled programming language that looks like C and checks every integer operation. When a result does not fit, the program stops and reports it instead of carrying on with a wrong number.

> ⚠️ **Experimental and in development.** This is v0.2. Expect breaking changes before 0.3.0.

## What it is, in plain terms

A computer stores each whole number in a fixed amount of memory, so each number type has a largest value. In many languages, Java and C among them, a sum that passes that value can quietly become a wrong number, and the program carries on with it. In cint every operation is checked. When the exact answer does not fit, the program stops and reports which operation failed, on which numbers and on which line.

Here is a program that counts past the largest number an `I64`, a 64-bit integer, can hold:

```
"counting past the largest I64\n";
I64 big = 9223372036854775807;
"big={big}\n";
I64 next = big + 1;
"next={next}\n";
```

It prints its first two lines, then stops at line 4 instead of printing a wrong `next`:

```
$ cint run examples/hello_overflow.ci
counting past the largest I64
big=9223372036854775807
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> hello_overflow.ci:4:16
   |
 4 | I64 next = big + 1;
   |                ^ add.checked.i64
   |
   = operation: add.checked.i64
   = operand: I64 9223372036854775807
   = operand: I64 1
   = exact:   9223372036854775808
   = limit:   I64 9223372036854775807
```

This report is shortened; the full one has a few more lines. The same program gives the same output, byte for byte, on Windows, Linux and macOS.

## For C developers

- **C syntax, integers only.** There is no `float` or `double`. The types run from `I8` to `I1024` and from `U8` to `U512`.
- **Every operator is checked.** Where C leaves `INT64_MAX + 1` undefined, cint stops with a fault record that names the operation, its exact operands and the source position. Wrapping and saturating arithmetic are separate operators: `+%`, `-%` and `*%` wrap, and `+|`, `-|` and `*|` saturate.
- **Self-hosted, emits C17.** The compiler, `cintc`, is written in cint, emits C17 and compiles itself. It builds with GCC, Clang, MSVC or Apple Clang.
- **A reference that defines the language.** `cint_ref`, an interpreter in Python, runs the rules of the specification, and the conformance suite compares `cintc` with it case by case.
- **Debugging by comparison.** `cint debug` replays a run, finds the operation that faulted and compares two builds of one program. The same build gives `cint explain`, `cint workbench` and `cint repl`.

## Try it

The examples at <https://integerc.dev/examples/> run in a browser, with nothing to install.

To build it, you need Python 3 and one C17 compiler: GCC, Clang, MSVC or Apple Clang.

```
python3 tools/cint_bootstrap.py --leg gcc        # or clang, msvc, apple-clang
python3 tools/cint_tools.py --leg gcc            # the debugging tools
```

The bootstrap first builds a small seed compiler written in C, uses it to build `cintc` and writes the `cint` command to `$CINT_BUILD/boot/gcc/cint`, or the directory of the leg you chose. `CINT_BUILD` defaults to `cint-build` in the system temporary directory. `tools/cint_tools.py` builds `cint debug`, `cint explain`, `cint workbench`, `cint repl` and `cint interp` beside it.

```
$ cint test examples/ledger.ci
test ledger.ci:83 "interest ties round to even" ... ok
test ledger.ci:89 "an overdraft is rejected" ... ok
test ledger.ci:94 "overflow is a fault" ... ok
3 of 3 tests passed
```

A fault ends the program with exit status 1, after the output it had already written. `cint build` writes an executable and `cint emit-c` writes the C17 source. The cint university course on [integerc.dev](https://integerc.dev) teaches the language from the first program.

cint is not CINT, the C/C++ interpreter of CERN's ROOT, which Cling replaced in ROOT 6.

## What is here

| Path | What |
| --- | --- |
| `compiler/` | `cintc`, the compiler, written in cint |
| `seed/` | The seed compiler, in C, that builds `cintc` the first time |
| `rt/` | The runtime that the emitted C links against, and its C ABI |
| `cli/` | The `cint` command |
| `interp/` | `cint interp`, the interpreter, with checkpoints, recordings and replay |
| `workbench/` | `cint debug`, `cint explain`, `cint workbench` and `cint repl` |
| `python/` | The Python package that loads and calls cint code |
| `ref/` | `cint_ref`, the reference interpreter in Python |
| `conformance/` | Conformance cases and their expected outcomes |
| `harness/`, `tools/`, `tests/` | The conformance harness and the build and check tools, with their tests |
| `docs/spec/` | The specification editions |
| `examples/` | Example programs |

## Specifications

SPEC-04P.2, the cint language, holds the whole language in one document. SPEC-01P.2 states the byte encodings and identities, and the index in [docs/spec/README.md](docs/spec/README.md) lists the other editions. An edition does not change once released. A correction that changes no meaning is listed in `docs/spec/ERRATA.md`, and a change of meaning is a new edition. Code comments and conformance cases cite clauses by their identifiers, such as `SPEC-04 LS-17`.

## Status

`cintc` refuses some forms the reference runs, among them fixed point and struct array literals, with error C9102. SPEC-09P.1 lists each one. The language has no standard library yet. A program can print text but cannot store it in a variable, and it cannot read input. Known limitations, with the cases they concern, are listed in each component's `OPEN.md`.

## How the work was done

The author set the requirements, the specifications and the acceptance thresholds, and reviewed the results. Much of the code and documentation was written with AI coding agents working to those specifications. The claims here do not rest on that process: each one is checked by the conformance suite and the receipts, which anyone can rerun from this tree.

## License

The code, the specification text and the receipts are under the Apache License 2.0 with LLVM Exceptions (`Apache-2.0 WITH LLVM-exception`); see [LICENSE](LICENSE) and [NOTICE](NOTICE). Contributions are under the same terms; see [CONTRIBUTING.md](CONTRIBUTING.md).
