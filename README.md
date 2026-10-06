<a href="https://integerc.dev/"><img src="docs/cint-wordmark.svg" width="320" alt="cint"></a>

# cint

A C family language for deterministic, cross-platform programs.

Every number is an integer and every operator is checked. A program writes the same bytes on every machine, and an operation that cannot give the exact answer stops with a fault record instead of a wrong one. The compiler, `cintc`, is written in cint, emits C17, and compiles itself.

> ⚠️ **Experimental and in development.** This is v0.1. Expect breaking changes before 1.0.

Not to be confused with CINT, the C/C++ interpreter of CERN's ROOT, which Cling replaced in ROOT 6.

## Try it

In a browser, with nothing to install: <https://integerc.dev/examples/>.

To build it, you need Python 3 and one C17 compiler: GCC, Clang, MSVC or Apple Clang.

```
python3 tools/cint_bootstrap.py --leg gcc        # or clang, msvc, apple-clang
```

The bootstrap builds the seed compiler, uses it to build `cintc`, and writes the `cint` command to `$CINT_BUILD/boot/<leg>/cint`. `CINT_BUILD` defaults to `cint-build` in the system temporary directory.

```
$ cint run examples/hello_overflow.ci
counting past the largest I64
big=9223372036854775807
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> hello_overflow.ci:4:16

$ cint test examples/ledger.ci
3 of 3 tests passed
```

`cint build` writes an executable and `cint emit-c` writes the C. `docs/tutorials/` walks through the examples.

## What is here

| Path | What |
| --- | --- |
| `compiler/` | `cintc`, the compiler, written in cint |
| `seed/` | The seed compiler, in C, that builds `cintc` the first time |
| `rt/` | The runtime that the emitted C links against, and its C ABI |
| `cli/` | The `cint` command |
| `ref/` | `cint_ref`, the reference interpreter in Python, which defines what a program means |
| `conformance/` | Conformance cases and their expected outcomes, run through both `cint_ref` and `cintc` |
| `harness/`, `tools/`, `tests/` | The conformance harness, the build and check tools, and their tests |
| `docs/spec/` | The public specification editions |
| `docs/tutorials/`, `examples/` | Tutorials and example programs |

## Specifications

The language is specified in editions that do not change once released: `SPEC-00P.1` (overview), `SPEC-01P.1` (the integer machine: types, operators, faults, the fault record) and `SPEC-04P.1` (the language surface), with `GLOSSARY-P.1` and `REFERENCES-P.1`. A correction that changes no meaning is listed in `docs/spec/ERRATA.md`; a change in meaning is a new edition. Code comments and conformance cases cite clauses by their identifiers (`SPEC-04 LS-17`), which resolve in the current public edition. The index is [docs/spec/README.md](docs/spec/README.md).

## Status

Of the 861 conformance cases, `cintc` is compared with `cint_ref` on 783, holds 11 that wait on a decision, and 67 do not apply to it. The compiler does not yet accept integers wider than 64 bits, and there is no standard library or debugger. Known limitations, with the cases they concern, are listed in each component's `OPEN.md`.

## How the work was done

The author set the requirements, the specifications and the acceptance thresholds, and reviewed the results. Much of the code and documentation was written with AI coding agents working to those specifications. The claims here do not rest on that process: each one is checked by the conformance suite and the receipts, which anyone can rerun from this tree.

## License

Apache License 2.0 with LLVM Exceptions; see [LICENSE](LICENSE) and [NOTICE](NOTICE). Contributions are under the same terms; see [CONTRIBUTING.md](CONTRIBUTING.md).
