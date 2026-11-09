<p align="center">
  <a href="https://integerc.dev/"><img src="https://integerc.dev/cint-wordmark.svg" width="320" alt="cint"></a>
  <br>
  A compiled language for checked integer arithmetic.
  <br><br>
  <a href="https://github.com/pennyleans/cint-public-build/releases/latest"><img src="https://img.shields.io/github/v/release/pennyleans/cint-public-build" alt="Latest release"></a>
  <img src="https://img.shields.io/badge/platforms-Windows%20%7C%20Linux%20%7C%20macOS-blue" alt="Windows, Linux and macOS">
</p>

# cint

cint is an integer-only programming language with C-family syntax. Every plain arithmetic operator is checked. An overflow stops the program with a fault record containing the operation, operands and source position. Wrapping and saturating arithmetic use explicit operators.

The compiler is written in cint and emits C17. A seed compiler written in C builds it from source with GCC, Clang, MSVC or Apple Clang.

```text
void main() {
    I64 big = 9223372036854775807;
    I64 next = big + 1;
}
```

The addition faults with `E_OVERFLOW`, reporting the exact result `9223372036854775808` and the `I64` limit. Integer and fixed-point arithmetic need no floating-point types.

## Get started

[Download the latest source release](https://github.com/pennyleans/cint-public-build/releases/latest) or clone the repository. Python 3 and a C17 compiler are required to bootstrap.

```sh
git clone https://github.com/pennyleans/cint-public-build.git
cd cint-public-build
python3 tools/cint_bootstrap.py --leg gcc
```

Use `clang`, `msvc` or `apple-clang` for the other compiler configurations. The command is written to `$CINT_BUILD/boot/gcc/cint` for GCC, with the selected configuration replacing `gcc` on other builds. `CINT_BUILD` defaults to `cint-build` in the system temporary directory. Add that directory to `PATH`, then run:

```sh
cint run examples/hello_overflow.ci
cint test examples/ledger.ci
cint build --out ledger-build examples/ledger.ci
```

The overflow example ends with a fault. `cint build` creates an executable; `cint emit-c` writes C17 source. Build the debugging tools with `python3 tools/cint_tools.py --leg gcc`.

## Documentation

- [Try the examples](https://integerc.dev/examples/) in a browser.
- [Language and tools](docs/spec/README.md): published specification editions, including the language in SPEC-04P.5.
- [Example programs](examples/): arithmetic, tests and complete programs.
- [Release notes](https://github.com/pennyleans/cint-public-build/releases): changes and measured results for each version.
- [Contributing](CONTRIBUTING.md) and [reporting security issues](SECURITY.md).

## Source

| Directory | Contents |
| --- | --- |
| `compiler/`, `seed/` | Self-hosted compiler and C bootstrap seed |
| `cli/`, `rt/` | Command-line tools, runtime and C ABI |
| `toolchain/as/`, `link/` | x86-64 assembler and ELF linker |
| `ref/`, `conformance/` | Reference interpreter, test programs and expected outcomes |
| `interp/`, `workbench/` | Interpreter, replay and debugging tools |
| `python/` | Python bindings |

The reference defines the executable language rules. The conformance suite compares compiler outcomes with the reference. Published specification editions remain available at fixed paths; code and tests cite their clause identifiers.

## Status

cint is in development. Versions before 1.0 may introduce breaking changes. The compiler implements part of the reference language and refuses unsupported forms with C9102. [SPEC-09P.5, section 12](docs/spec/SPEC-09P.5-TOOLCHAIN.md) lists those forms; component `OPEN.md` files record further limitations.

cint is separate from CINT, the former C/C++ interpreter in CERN ROOT.

## Development

The author sets the requirements, specifications and acceptance thresholds and reviews the results. Much of the code and documentation was written with AI coding agents. The test tools and receipts are included so results can be checked from source.

## License

[Apache-2.0 with LLVM Exceptions](LICENSE). See [NOTICE](NOTICE) for acknowledgments.
