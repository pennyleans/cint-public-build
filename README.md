<p align="center">
  <a href="https://integerc.dev/"><img src="https://integerc.dev/cint-wordmark.svg" width="320" alt="cint"></a>
  <br>
  <a href="https://github.com/pennyleans/cint-public-build/releases/tag/v0.2.20"><img src="https://img.shields.io/badge/release-v0.2.20-blue" alt="Release v0.2.20"></a>
  <a href="https://github.com/pennyleans/cint-public-build/blob/v0.2.20/docs/spec/SPEC-09P.7-TOOLCHAIN.md#measured-agreement"><img src="https://img.shields.io/badge/release%20CI-47%2F47%20passing-brightgreen" alt="Release CI: 47 of 47 checks passing"></a>
  <a href="https://github.com/pennyleans/cint-public-build/blob/v0.2.20/docs/spec/SPEC-09P.7-TOOLCHAIN.md#measured-agreement"><img src="https://img.shields.io/badge/conformance-0%20disagreements-brightgreen" alt="Release conformance: 0 disagreements in the C17 and QBE comparisons"></a>
  <a href="#supported-platforms"><img src="https://img.shields.io/badge/CPU-x86--64-blue" alt="Supported processor architecture: x86-64"></a>
  <a href="#supported-platforms"><img src="https://img.shields.io/badge/CPU-Arm64-blue" alt="Supported processor architecture: Arm64"></a>
</p>

# cint

cint is a compiled programming language for checked integer arithmetic. It uses C-family syntax and integer and fixed-point types.

An `I64` holds signed 64-bit integers. Adding one to its largest value stops the program instead of wrapping to a negative number:

```
void main() {
    I64 big = 9223372036854775807;
    I64 next = big + 1;
}
```

The `E_OVERFLOW` fault record identifies the operation, its operands, and the source position. Separate operators request wrapping or saturation explicitly.

- The compiler is written in cint and emits C17. A small C seed compiler builds it from source.
- The runtime has a C ABI. An experimental QBE backend provides a separate native build path.
- The reference implementation and conformance suite check the compiler's results, including its fault records.

## Get started

[Download the latest source release](https://github.com/pennyleans/cint-public-build/releases/latest) or clone the repository. Bootstrap requires Python 3 and a C17 compiler. On Linux with GCC:

```sh
git clone https://github.com/pennyleans/cint-public-build.git
cd cint-public-build
python3 tools/cint_bootstrap.py --leg gcc --out ../cint-build
```

The bootstrap writes `cint` to `../cint-build`. Run a program that faults, run the ledger tests, or build the ledger as an executable:

```sh
../cint-build/cint run examples/hello_overflow.ci
../cint-build/cint test examples/ledger.ci
../cint-build/cint build --out ../ledger-build examples/ledger.ci
```

Use `cint emit-c` to inspect the generated C17. Build the debugging tools with `python3 tools/cint_tools.py --leg gcc`.

## Supported platforms

These are the C17 toolchain configurations in the [conformance matrix](docs/spec/SPEC-09P.7-TOOLCHAIN.md#the-run-matrix). Pass the corresponding `--leg` to the bootstrap command.

| Processor | Operating system | Compiler leg |
| --- | --- | --- |
| x86-64 | Linux | `gcc` or `clang` |
| x86-64 | Windows | `msvc` |
| Arm64 (AArch64) | macOS | `apple-clang` |

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

cint is in development. Versions before 1.0 may introduce breaking changes. The compiler implements part of the reference language and reports unsupported forms. [SPEC-09P.7, section 12](docs/spec/SPEC-09P.7-TOOLCHAIN.md#refusals-of-the-cint-tools-in-0-2-0) lists those forms.

cint is separate from CINT, the former C/C++ interpreter in CERN ROOT.

## Development

The author sets the requirements, specifications and acceptance thresholds and reviews the results. Much of the code and documentation was written with AI coding agents. The test tools and receipts are included so results can be checked from source.

## License

[Apache-2.0 with LLVM Exceptions](LICENSE). See [NOTICE](NOTICE) for acknowledgments.
