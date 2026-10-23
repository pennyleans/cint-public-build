# SPEC-09P.2: Toolchain and conformance

SPEC-09P.2, edition 2 of the toolchain and conformance specification, cint v0.2.3, 2026-10-23.

<a id="foreword"></a>

## Foreword

This document specifies how the cint toolchain of cint 0.2.2 is built and checked: the intermediate representation that its compiler produces, the C that the compiler emits and the runtime that C calls, what two implementations of cint must agree on, the conformance suite that measures that agreement, the bootstrap of the compiler from a hand-written C program, and the receipts from which a reader reruns a published figure. It replaces SPEC-09P.1: it adds the SIR forms of extern declarations and their calls (6.2), the host import descriptor in the symbol audit and in the observer interface (7.9, 9.6), fixed-point values of 128 bits in `.expect` files (9.3), the standard library root of a bootstrap (10.3), and the refusals of the tools of 0.2.2 (clause 12).

Each rule ends with one or more clause identifiers in brackets, such as [SIR-09] or [EMIT-03]. An identifier names one rule across editions: a rule keeps its identifier when its text moves to another section or another document, and an identifier is not reused. Code comments, test files and receipts cite rules by these identifiers. Section numbers belong to this edition only.

<a id="introduction"></a>

## Introduction

cint has one specification of meaning, SPEC-04P.3, and several implementations of it. A program compiled to C, the same program run by the interpreter, and a program whose kernels run on a GPU must give the same output bytes, the same fault record and the same count of fuel. A separate implementation written in Python, the reference, runs each program from its source text and gives the expected results. This document says how these implementations are built, what they must agree on, and how that agreement is measured and recorded.

A small program shows what agreement means. It prints a sum of squares and then narrows the sum to a type too small for it.

```cint agree.ci
I64 total = 0;
for i in 1..=10 {
    total += i * i;
}
"{total}\n";
I8 small = total as I8;
"{small}\n";
```

```text
385
fault[E_NARROW]: the value does not fit the destination type
  --> agree.ci:6:18
   |
 6 | I8 small = total as I8;
   |                  ^ as.checked.i64.i8
   |
   = operation: as.checked.i64.i8
   = operand-count: 1
   = operand: I64 385
   = exact:   385
   = limit:   I8 127
...
```

`cint run` compiled the program to C, built it and ran it. The reference shares no code with the compiler (5.4). It reads the same source and prints the outcome as the text that the conformance suite stores (9.3):

```console
$ python3 -m cint_ref run agree.ci
case agree
source reference
outcome fault
stdout-bytes 4
stdout-sha256 5476dba1f4bd59f1444ca1fd0770c1db808311b772f7685b3ff60ad7956418a8
fuel-consumed 11
fault.code E_NARROW
fault.operation as.checked.i64.i8
fault.operand I64 385
fault.exact 385
fault.limit I8 127
fault.position agree.ci:6:18
fault.revision self
fault.address none
fault.stack-depth 0
```

The two agree. Both wrote the same 4 bytes, `385` and a line feed, whose SHA-256 digest is the `stdout-sha256` line. Both stopped at the conversion at `agree.ci:6:18`, with the same operation, operand, exact result and limit. Both consumed 11 units of fuel: 1 for the entry and 1 for each of the 10 loop iterations. The line `fault.revision self` stands for the revision of the program under test, which the reference does not compute (6.5).

<a id="how-this-document-is-organized"></a>

### How this document is organized

Clauses 1 to 4 state what the document covers, the documents it relies on, its terms and what conformance means. Clause 5 describes the architecture: one front end, one intermediate representation, three executors and the reference. Clause 6 defines that intermediate representation, SIR, and its text form. Clause 7 defines the C that the compiler emits and the runtime contract it relies on. Clause 8 says what two implementations must agree on and how a case that is not compared is reported. Clause 9 describes the conformance suite and how to run one case. Clause 10 describes the bootstrap of the compiler and what its self-hosting fixpoint shows and does not show. Clause 11 describes receipts and how a reader reruns a published figure. Clause 12 lists the constructs that the tools of cint 0.2.2 refuse. Clause 13 names two future directions. Annex A gives the measured figures of the release, and the Bibliography lists the works cited for information.

Every example in this document was run with the tools of cint 0.2.2. A `cint` block is a complete program, and the `text` block after it is its output exactly as `cint run` prints it. A `console` block is a tool session: a line that begins `$ ` is a command, the lines after it are its output exactly as printed, and a line `...` stands for lines left out. Commands that name a file of the source tree, such as `tools/cint_check.py`, are run from the root of the tree. The others are run from a directory that holds the example's files. In every session the tree's `ref/` directory is on `PYTHONPATH`, so that `python3 -m cint_ref` runs the reference, and the `cint` command is on `PATH`. Digests in the output change when the sources change, so the text says what each digest is, not its value.

<a id="scope"></a>

## 1 Scope

1. This document specifies, for cint 0.2.2:
   - the architecture of the toolchain: its front end, its intermediate representation, its executors and the reference;
   - the semantic intermediate representation (SIR), its text form and the revision identity computed from it;
   - the C that the compiler emits, and the runtime contract that this C relies on;
   - what two implementations must agree on, and how a refusal or a disagreement is reported;
   - the conformance suite: case files, the `.expect` format, operation fixtures, required case lists, and how one case is run;
   - the bootstrap of the compiler and its self-hosting fixpoint;
   - receipts, and how a reader reruns a published figure;
   - the constructs that the tools of cint 0.2.2 refuse.
2. This document does not specify:
   - the language, which SPEC-04P.3 specifies;
   - the byte encodings of fault records and program state, which SPEC-01P.3 specifies;
   - how an executor carries out a kernel dispatch, which SPEC-02P.2 specifies;
   - the C and Python interfaces through which a host program calls cint code, which SPEC-03P.2 specifies;
   - the commands of the cint tools, which SPEC-06P.2 specifies.

<a id="normative-references"></a>

## 2 Normative references

The following documents are referred to in the text in such a way that some or all of their content constitutes requirements of this document. For dated references, only the edition cited applies.

- ISO/IEC 9899:2018, *Information technology: Programming languages: C* (C17). Used for the language of emitted C and for the behaviors that clause 7 avoids.
- D. Crocker, editor and P. Overell. *Augmented BNF for Syntax Specifications: ABNF*. RFC 5234 (STD 68), January 2008. Used for the grammar of 9.3.
- P. Kyzivat. *Case-Sensitive String Support in ABNF*. RFC 7405, December 2014. Used for the `%s"..."` strings of 9.3.
- A. Rundgren, B. Jordan and S. Erdtman. *JSON Canonicalization Scheme (JCS)*. RFC 8785, June 2020. Used for the bytes of a receipt, with the departure in the escaping of strings that 11.2 states.
- T. Bray, editor. *The I-JSON Message Format*. RFC 7493, March 2015. Used for the integer range of a receipt.
- National Institute of Standards and Technology. *Secure Hash Standard (SHS)*. FIPS PUB 180-4, August 2015. Used for SHA-256.
- F. Yergeau. *UTF-8, a transformation format of ISO 10646*. RFC 3629 (STD 63), November 2003.
- The Unicode Consortium. *The Unicode Standard, Version 17.0.0*, 2025. Used for the term Unicode scalar value (8.1).

<a id="terms-definitions-and-symbols"></a>

## 3 Terms, definitions and symbols

The terms of SPEC-04P.3 clause 3 apply, among them diagnostic, refusal, implementation, the reference, executor, entry, fuel, semantic input and revision. The following terms are added. Other terms are defined where they first appear, in italic type.

<a id="term-toolchain"></a>**3.1 toolchain**: the programs of cint 0.2.2 that translate, run and check cint programs: the compiler `cintc`, the `cint` command and its subcommands, the interpreter `cint-interp`, the runtime and the conformance tools under `tools/` and `conformance/tools/`.

<a id="term-front-end"></a>**3.2 front end**: the part of the compiler that reads source text, checks it and produces SIR.

<a id="term-sir"></a>**3.3 SIR**: the semantic intermediate representation, the form of a checked program that every executor of the toolchain runs (clause 6).

<a id="term-node"></a>**3.4 node**: one operation of SIR, such as one checked addition or one call.

<a id="term-site"></a>**3.5 site**: a node that can fault, identified by its module and its ordinal within the module, with the source position and the operation identifier that a fault record reports for it.

<a id="term-emitted-c"></a>**3.6 emitted C**: the C17 source text that the compiler writes for a program.

<a id="term-runtime"></a>**3.7 runtime**: the C code declared in `rt/cint_rt.h` and `rt/cint_rt_host.h` and built as the library `cint_rt`, which emitted C and the interpreter `cint-interp` call for checked arithmetic, fuel, call depth and fault records (SPEC-03P.2, 3.3). Its interface is the runtime contract of 7.10.

<a id="term-helper"></a>**3.8 helper**: a function of the runtime that performs one operation of one type and reports its fault, such as `cint_add_i64`.

<a id="term-capability"></a>**3.9 capability**: a set of SIR node families, forms and types that an executor carries out with their exact meaning. An executor's *capability table* lists, for every family, form and type, whether it is supported.

<a id="term-conformance-case"></a>**3.10 conformance case**: a program, or one operation, together with its expected outcome.

<a id="term-expectation"></a>**3.11 expectation**: the expected outcome of a conformance case, written in the `.expect` format of 9.3. A *frozen* expectation is one that is part of the suite and is not edited in place (9.1).

<a id="term-canonical-outcome"></a>**3.12 canonical outcome**: what implementations compare for one entry: the output bytes, the kind of outcome, the returned value, the fuel consumed and the fault record, or, for a rejected program, the diagnostic code and position.

<a id="term-agreement"></a>**3.13 agreement**: the relation between two implementations whose canonical outcomes for a program and the same semantic inputs are equal in the sense of 8.1.

<a id="term-receipt"></a>**3.14 receipt**: a canonical JSON file (11.2) that records one run of a tool: its inputs, its outcome and what it observed. A bootstrap, a fixpoint run and a conformance run each write one, and so do the tools of SPEC-06P.2 that take `--receipt` (SPEC-06P.2 12).

<a id="term-receipt-identity"></a>**3.15 receipt identity**: the SHA-256 digest of the canonical bytes of a receipt's inputs and outcome (11.2). Two runs with the same receipt identity had the same inputs and the same outcome.

<a id="term-configuration"></a>**3.16 configuration**: one choice of C toolchain, optimization level, helper mode (7.3) and sanitizers under which the toolchain and the programs it compiles are built.

<a id="term-compiler-source-identity"></a>**3.17 compiler source identity**: the SHA-256 digest of the source manifest of the compiler's source files (11.2).

<a id="term-bootstrap"></a>**3.18 bootstrap**: the building of the compiler from its source text and a C toolchain, with no cint compiler built earlier.

<a id="term-fixpoint"></a>**3.19 fixpoint**: the state in which the compiler, built from the C that it emitted for its own source, emits that same C again.

<a id="conformance"></a>

## 4 Conformance

1. In this document, a rule is stated in the present tense, as a fact about a conforming implementation or tool, and ends with its clause identifiers (Foreword). "May" states a permission.
2. Clauses 6 and 7 apply to the toolchain and to any other implementation that produces SIR or emits C for the runtime of 7.10. Clauses 8 to 11 apply to every implementation that claims agreement with the reference.
3. A conformance run is evidence of agreement for the cases it runs, in the configurations it runs. It does not define the language: where an implementation and SPEC-04P.3 differ, SPEC-04P.3 governs, and the difference is a defect of the implementation or an ambiguity in the text.
4. Clause 12, Annex A, the examples and the notes are informative.

<a id="architecture"></a>

## 5 Architecture

The toolchain turns a program's source text into one checked representation and runs that representation in several ways. Each way of running it is held to the same expected results, which a separately written implementation computes from the source text alone.

<a id="overview"></a>

### 5.1 Overview

1. The figure shows the parts and how outcomes are compared.

```text
                     source text (.ci)
                            |
             +--------------+---------------+
             |                              |
     cintc front end                 the reference
             |                       (cint_ref, Python,
            SIR                       reads the source itself)
             |                              |
   +---------+-----------+                  |
   |         |           |                  |
 cpu-c17  cpu-sir-interp gpu-cuda           |
 emitted  cint-interp    cpu-c17 plus       |
 C and    runs the SIR   PTX for the        |
 runtime  text           work-item bodies   |
   |         |           |                  |
   +---------+-----------+--------+---------+
                                  |
                 canonical outcomes, compared (clause 8)
```

<a id="one-front-end"></a>

### 5.2 One front end

The compiler `cintc` is written in cint and is the only front end of the toolchain.

**Semantics**

1. <a id="ARCH-01"></a>One front end produces the SIR of a program, and every path that runs or describes the program takes that SIR: `cint run`, `cint build`, `cint test`, `cint emit-c`, the interpreter and the debugging tools `cint-explain` and `cint-workbench`. Arithmetic is carried out in these places only, each held to the conformance suite: the helpers of the runtime (7.10), the constant evaluation of the front end (held by the constant and run-time category of 9.4), each executor on its capability table, and the reference. The interpreter calls the runtime helper for every arithmetic, conversion and built-in node, the same helper that emitted C calls for that node, so it adds no arithmetic of its own [ARCH-01].

<a id="executors"></a>

### 5.3 Executors

An executor runs the SIR of a program. cint 0.2.2 has three: `cpu-c17`, `cpu-sir-interp` and `gpu-cuda`.

**Semantics**

1. The executor `cpu-c17` runs a program as C17 that the compiler emits (clause 7), compiled by a C toolchain and linked with the runtime. It runs the work-items of a kernel dispatch one at a time, in canonical order, stages the outputs and publishes them only when the dispatch ends without a fault.
2. <a id="BACK-03"></a>`cpu-c17` is the reference implementation of the language on a CPU and the oracle against which other executors are compared. Where it and SPEC-04P.3 differ, SPEC-04P.3 governs (clause 4, paragraph 3). Every other executor is admitted by comparing its outcomes with those of `cpu-c17` and of the reference on the conformance suite [BACK-03].
3. <a id="BACK-01"></a>An executor that writes files is a module of `cintc`, compiled into it. The compiler loads no executor code when it runs, because such code would lie outside the compiler source identity [BACK-01].
4. <a id="BACK-01a"></a>The executor `cpu-sir-interp` is the interpreter `cint-interp`, a C17 program outside `cintc`. It reads the SIR text that `cintc` writes for each module (6.2) and the program file `cint-program.sites` that lists the modules. Before it runs a program it checks the verifier rules it relies on (each temporary defined once and before it is used, the operand types, and a site on every node that can fault) and recomputes the revision identity (6.5); a text that fails these checks is reported with its position and is not run. It needs neither `cintc` nor a C compiler at run time. It calls the runtime helper for each arithmetic, conversion and built-in node and uses the `cint_rt` library for contexts, fault records and output. It executes the structured control flow of SIR as written, keeps frames under the depth limit, counts local arrays against the frame-arena capacity in elements, charges fuel only where SIR charges it (6.1), and runs the work-items of a dispatch one at a time in canonical order, publishing outputs only on success [BACK-01a].
5. <a id="BACK-16"></a>The executor `gpu-cuda` is the `cpu-c17` build of a program together with device code for each kernel: PTX text written by `compiler/back_ptx.ci` with the directives `.version 6.0` and `.target sm_52`, embedded in the module's C file and loaded through the CUDA driver API. Sequential code, the entry checks of a dispatch, publication, the final fold of each reduction and the description of a fault by re-execution run on the host; only the work-item bodies run on the device. Every kernel of a `gpu-cuda` build runs on the device. A kernel that needs a capability the `gpu-cuda` table does not support fails the build with `E_UNSUPPORTED` (5.5) [BACK-16].
6. <a id="BACK-15"></a>The Python package of SPEC-03P.2 names an executor by its backend: `cpu` for `cpu-c17`, `cuda` for `gpu-cuda` and `ref` for `cpu-sir-interp`. `cint.load` builds and loads a library for `cpu`, its default or for `cuda` and raises `NotImplementedError` for `ref`, since `cpu-sir-interp` runs no library; `cint.replay` runs a recording on `ref`, its default or on `cpu`. Any other name is refused with an exception [BACK-15].

**Notes**

7. SPEC-02P.2 specifies how each executor carries out a dispatch so that its outcome is the sequential one.

<a id="the-reference"></a>

### 5.4 The reference

The reference `cint_ref` is a Python program that runs cint source directly. Its results are the expected results of the conformance suite.

**Semantics**

1. <a id="ARCH-02"></a><a id="REF-01"></a>The reference is written in Python with the standard library only. It reads `.ci` source, parses it with its own parser, and executes it on Python integers, which have no fixed width, with an explicit range check after every operation that can overflow. It shares no code, tables, grammar files or generated files with the compiler or the runtime: it does not import, generate from or read any file of `cintc`, of the bootstrap compiler (10.2) or of `rt/cint_rt.h`. It enforces the call-depth limit itself, faulting `E_DEPTH`, so the recursion limit of Python is not reached first [ARCH-02, REF-01].
2. <a id="REF-02"></a>The reference shares no code with the compiler, but it is not independent of it in a stronger sense. Both were written from the same specification text, so an ambiguity in the text can reach both. The check on such shared errors is the set of anchor cases (9.2), each derived by hand with its derivation written beside it [REF-02].
3. <a id="REF-03"></a>The reference runs the work-items of a kernel dispatch one at a time in canonical order, stages the outputs and publishes them only on success. It reports each fault with the full fault record except the revision, which it does not compute [REF-03].
4. <a id="REF-04"></a>Speed is not a requirement of the reference [REF-04].
5. <a id="REF-08"></a>Anyone may run the reference directly on a program in its scope, from its command line or inside another Python runtime. A direct run is not a conformance run and writes no receipt. The reference does not define the meaning of a program: this specification does, and the reference is held to it [REF-08].
6. <a id="REF-07"></a>The reference runs one `test` block of a module as its entry, in a fresh context, with `python3 -m cint_ref run FILE.ci --test NAME`, where `NAME` is the test's name. The outcome of the test body gives the verdict. The reference has no command that runs every test of a module, so a caller names each test. For a test that both run, the outcome agrees with its outcome under `cint test` as 8.1 requires, so the verdicts agree [REF-07].

<a id="capabilities-and-refusal"></a>

### 5.5 Capabilities and refusal

An executor that cannot carry out a node with its exact meaning refuses the program before it runs. It does not run the program with another meaning.

**Semantics**

1. <a id="ARCH-03"></a>The executor is chosen per build. An executor that cannot carry out a SIR node with its exact meaning refuses the program at translation with `E_UNSUPPORTED`, naming the node, its source position and the missing capability. No executor narrows, wraps or reorders an operation to fit what it supports [ARCH-03].
2. <a id="BACK-04"></a>Each executor has a capability table: for every SIR node family, form and type, one of `native`, `lowered:<strategy>` or `unsupported`. The table is part of the compiler source. The table of `gpu-cuda` is the comment at the head of `compiler/back_ptx.ci` [BACK-04].
3. <a id="BACK-05"></a>A program that needs an `unsupported` entry of the selected executor fails to build with `E_UNSUPPORTED`, naming the node, its site and the capability. A device that lacks, when a program is loaded, a capability that translation assumed fails the load with `E_UNSUPPORTED` before any kernel is prepared. Execution does not fall back to a narrower type [BACK-05].
4. <a id="BACK-06"></a>A `lowered:<strategy>` entry names a lowering, such as carrying out a 64-bit operation in two 32-bit words, that has conformance cases of its own. A lowering is admitted only when those cases pass [BACK-06].

**Examples**

5. The kernel below doubles each element with the saturating `+|`. The executor `cpu-c17` runs it.

```cint sat.ci
kernel k[n](in I64[n] a, out I64[n] b) over [i: n] {
    b[i] = a[i] +| a[i];
}

export I64 run() {
    I64[4] a;
    I64[4] b;
    for j in 0..4 { a[j] = (j + 1) as I64; }
    k(a, b);
    return b[3] as I64;
}
"{run()}\n";
```

```text
8
```

The capability table of `gpu-cuda` lists saturating arithmetic as `unsupported`. The emitted C carries the refusal inside the block that only a `gpu-cuda` build compiles:

```console
$ cint emit-c --root . --out out sat.ci
$ grep '#error' out.stage-0/sat.c
#error "gpu-cuda: E_UNSUPPORTED: kernel sat.k needs capability sat, node 35, at 2:17 (SPEC-09 BACK-05)"
```

A `gpu-cuda` build compiles this file with the macro `CINT_GPU_CUDA` defined, so it stops at the `#error`, which names the kernel, the capability, the SIR node and the position `2:17` of the `+|`. A `cpu-c17` build of the same file does not define the macro and runs the program.

<a id="the-compiler"></a>

### 5.6 The compiler

The compiler is a checked cint program. It keeps all its state in tables whose sizes it computes before it starts, and a host program drives it.

**Semantics**

1. The source of the compiler is the import closure of `compiler/main.ci`. In cint 0.2.2 it is 14 modules: `back_c`, `back_ptx`, `check`, `decl`, `emit`, `fold`, `limits`, `lowering`, `main`, `parse`, `resolve`, `scan`, `sha256` and `verify`, each a file `compiler/<name>.ci`.
2. <a id="CINTC-01"></a>All compiler state lives in tables of fixed capacity. The capacity of each table is a formula of the sizes of the inputs, capped by a hard limit. An input that exceeds a capacity is reported with the diagnostic C9001, naming the table, the limit and the source position; it does not crash the compiler and is not cut short silently [CINTC-01].
3. <a id="CINTC-02"></a>Blocks nest at most 256 deep, not counting the body of a function or a test, and the `{` of a block 257 deep is the diagnostic C9004; the arms of an `else if` chain or a `switch` do not nest (paragraph 4). An expression nests as deep as the parser's stack holds, and beyond that is also C9004. The `cint` command reads at most 2,048 modules for one program, and stops a program of more with a tool error and exit status 5; 12 states what it does with a source file beyond the limit below. For a module of `n` source bytes, whose number of tokens, `tokens`, is at most `n + 1`, the tables have these capacities and hard limits; each division rounds down [CINTC-02]:

| Table | Capacity | Hard limit |
|---|---|---|
| Source | `n` bytes | 4,194,304 bytes per module, 67,108,864 bytes per program |
| Modules | the modules of the build | 4,096 per program |
| Syntax nodes | `2 * tokens + 16` | set by the source limit |
| Symbols | `tokens / 2 + 64` | 1,048,576 per module |
| Functions | `tokens / 4 + 1` | 65,536 per module |
| Types | `256 + tokens / 8` | 65,536 per module |
| SIR nodes | `4 * tokens + 16` | set by the source limit |
| Parser stack | `512 + tokens / 32` frames | 131,072 frames |
| Declarations seen across modules | 1, plus `n / 4 + n / 32 + 1` for each module | 1,048,576 per program |
| Output | the bytes that `measure` counts | 268,435,456 bytes per module |
| Retained diagnostics | 100 per module | 100 per module |

4. <a id="CINTC-03"></a>A chain of `else if` arms, and the arms of a `switch`, are one node with an ordered list of arms, and every pass of the compiler walks the list iteratively. A chain of 1,000 arms therefore uses the same stack depth as a chain of 2 [CINTC-03].
5. <a id="CINTC-05"></a>The compiler uses no heap allocator and requests no memory during a call. A host program, the bridge (`rt/cint_bridge.c` and `rt/cint_build.c`), drives it through four exported calls: `plan`, which computes the size of every table from the sizes of the inputs; `compile`, which reads the source of a module into the tables and produces its SIR or its diagnostics; `measure`, which counts the bytes of each output file of a module; and `emit`, which writes them. The bridge allocates the tables after `plan` and the output buffers after `measure` [CINTC-05].
6. <a id="CINTC-04"></a>`measure` and `emit` run over the same SIR in separate calls. The bridge allocates exactly the number of bytes `measure` counted, and `emit` writes exactly that many; a different count is an internal compiler error [CINTC-04].
7. The compiler performs no input or output of its own. 7.1 describes how the bridge reads its inputs and commits its outputs.
8. <a id="CINTC-10"></a>A fault inside the compiler, such as an index outside a table, is reported as an internal compiler error with the full fault record, the compiler source identity and the identity of the compiler's inputs [CINTC-10].

**Notes**

9. Because the compiler is a checked program, a fault inside it stops it instead of corrupting memory. A logic error that produces wrong output without a fault, such as lowering `+` to `+%`, is found only by the conformance suite and the fixpoint (10.4).

<a id="sir"></a>

## 6 SIR

SIR is the program after it has been checked: every value has its type, every intermediate value has a name, control flow is structured, and every point where the program can fault is marked with its source position. Every executor of the toolchain runs SIR, and `cint-explain` prints it.

<a id="design-rules"></a>

### 6.1 Design rules

**Semantics**

1. <a id="SIR-01"></a>SIR is typed. Every temporary, local, parameter and result has exactly one cint type, `Bool` for comparisons and conditions. There is no implicit conversion: every change of width or signedness is an `as` node [SIR-01].
2. <a id="SIR-02"></a>Every node that produces a value defines a new temporary, `%t0`, `%t1` and so on, exactly once. A variable of the source is a *slot*, read by `load.local` and written by `store.local`. Nodes are evaluated in the order they are written; nothing in SIR is unordered [SIR-02].
3. <a id="SIR-03"></a>A checked operation is one node, such as `add.checked.i64`, not an addition followed by a test. An executor lowers the node as a unit and cannot separate the check from the operation [SIR-03].
4. <a id="SIR-04"></a>Every node that can fault, including a fuel charge and a call, is a site: it carries its module, line, column and operation identifier. Sites are numbered from 1 in each module, in node order; the number 0 means no site [SIR-04].
5. <a id="SIR-05"></a>Control flow is structured: blocks, `if`, `loop`, `switch`, `break`, `continue`, `return` and the fault exit. There is no arbitrary jump. `&&` and `||` become an `if` that evaluates the right operand only when it is needed. A chain of `else if` arms is one `if` node with an ordered list of arms. The interpreter runs this structure as it is written and lowers nothing itself [SIR-05].
6. <a id="SIR-06"></a>The front end resolves every default of the language before it writes SIR. A fixed-point multiplication carries its intermediate width and its rounding mode even when the source used the default, and a reduction carries the width of its accumulator. An executor does not supply a default [SIR-06].
7. <a id="SIR-07"></a>Every fuel charge is explicit in SIR, so every executor charges at the same points. An entry's charge and a loop iteration's charge are `fuel.charge` nodes, a call's charge is made by its call node, and a dispatch's charge by its `dispatch.charge` node. The amount of a charge is a constant, or a constant times a product of extents computed with checked `I64` arithmetic, whose overflow is `E_FUEL` at the charge's site when a budget is set. The interpreter charges fuel at these nodes and at no other point [SIR-07].
8. <a id="SIR-15"></a><a id="SIR-16"></a>The opcode of every arithmetic, conversion and reduction node is the operation identifier of SPEC-04P.3 that a fault record reports, in lowercase, with the types written `i64`, `u8`, `i128`: `add.checked.i64`, `div.checked.i64` for the floor quotient `/`, `as.checked.i64.i8`. The `operation` field of a fault record is the opcode of the node that faulted. When an executor carries out a node as a sequence of steps, the record still names the node: a `muldiv` that faults reports `muldiv.checked.i64.i64.floor`, not the division inside it. Type names in type annotations are those of the source, such as `%a: I64` [SIR-15, SIR-16].

<a id="the-text-form"></a>

### 6.2 The text form

SIR has one text form. The compiler writes it for every module, the interpreter reads it, and `cint-explain` prints it.

**Semantics**

1. <a id="SIR-09"></a>The text form is ASCII with LF line endings and one node per line. Temporaries are numbered from `%t0` in each function, in the order they are defined. A node that is a site ends with its ordinal and position, `#<ordinal> @<path>:<line>:<column>`, where `<path>` is the module-relative path. A node is indented two spaces for each level of structure that encloses it, up to 16 levels and 32 spaces at any deeper level, so the size of the text stays linear in the number of nodes; braces carry the structure [SIR-09].
2. <a id="SIR-09a"></a>The text of a module is, in order: a line `module <name>` with the module's dotted name; the `sites` line, `sites <path>` followed by one item `<ordinal>:<line>:<column>:<operation>` for each site in node order; one `struct` line for each struct type of the module, in declaration order; one `global` line for each module-level variable, in declaration order; one `extern` line for each row of the module's host import table, in its order; and the functions, kernels, script body and test blocks of the module. In the `sites` line, the operation of a call's site is `call.enter`, that of an extern call's site is `host.call`, that of a print statement's site is `print`, and that of any other site is the node's own operation identifier. On a node line, a site's annotation begins at the 50th character, or after it when the node text is longer, with at least one space before it. A parameter is written `%<name>`, a slot `$<name>.<k>`, a module-level variable `@<name>` and a structure that `break` or `continue` names `L<k>`; these numbers count from 0, and site ordinals from 1. Values are decimal, and printed text is written in double quotes with `\"`, `\\`, `\n`, `\t` and `\x<hh>` for other bytes outside printable ASCII [SIR-09a].
3. The main forms are these.

| Form | Meaning |
| --- | --- |
| `func @<module>.<name>(%<p>: <T>, ...) -> <T> {` | A function; `-> <T>` is absent when it has no result, and the line begins `export ` for an exported function. A parameter of type `Grants` is written `%<p>: Grants`; an entry receives it as the `I64` 0, which stands for the context's grant set. |
| `kernel @<module>.<name>(%<p>: <T>, ...) {` | A kernel. |
| `script @<module> {` | The top-level statements of a script, its implicit entry. |
| `test @<module> <k> {` | Test block `k` of the module. |
| `local $<x>.<k>: <T>` | A slot comes into scope. |
| `%t<N> = const <T> <value>` | A typed literal. |
| `%t<N> = <operation> %<a>, %<b>` | An arithmetic, bitwise or comparison node, spelled as its operation identifier. A conversion and a unary node take one operand. |
| `%t<N> = load.local $<x>.<k>`, `store.local $<x>.<k>, %<v>` | A slot is read or written. |
| `%t<N> = call @<module>.<name>(<argument>, ...)` | A call, with `%t<N> =` when the function returns a scalar. |
| `extern @<module>.<name>(<P>, ...) -> <R> <class> "<row>" signature <hex>` | A row of the module's host import table, for an extern that the module declares or another module's extern that it calls; the module is the one that declares it. A parameter is `Grants`, a scalar type of at most 64 bits or a rank-1 view of one, written `out <T>[_]` when the extern writes it. ` -> <R>` is absent for `void`; an error union is written `<tag>!<T>` or `<tag>!void` followed by ` errors <n>`, its set's value count. `<class>` is `pure`, `observe` or `unrestricted`, `<row>` the row's name (`fs/1.read` for a service extern, else the extern's name), and `<hex>` its type signature (SPEC-03P.2 7.5), two lowercase hexadecimal digits a byte. |
| `%t<N> = host.call @<module>.<name>(<argument>, ...)` | A call of an extern, with `%t<N> =` when it returns a value. It charges one unit of fuel at its site, as a call does, and the interpreter makes it through the host import table that the `extern` lines give. |
| `dispatch @<module>.<kernel>(<argument>, ...), <bits>` | A kernel dispatch. |
| `fuel.charge <n>` | A charge of `n` units: an entry's, first in its body or a loop iteration's, after the loop's `while`. |
| `if {`, `arm {`, `} then %<c> {`, `else {` | An `if` chain: each `arm` holds its condition's nodes and ends with `then`. |
| `loop L<k> {`, `while %<c>`, `next` | A loop: its condition and `while`, which leaves the loop when `c` is false, the body, then `next`, the target of `continue` and the update. |
| `switch L<k> %<v> {`, `case <items> {`, `default {` | A `switch`; items are values and inclusive ranges, such as `1, 3..=5`. |
| `break L<k>`, `continue L<k>`, `return`, `return %<v>` | A jump. |
| `print.begin`, `print.bytes "<text>"`, `print.value <T> %<v>`, `print.end` | One print statement. |
| `}` | The end of a function or of a structure. |

**Examples**

4. A function and a script that calls it:

```cint add.ci
I64 add3(I64 a, I64 b, I64 c) {
    return a + b + c;
}
"{add3(1, 2, 3)}\n";
```

```text
6
```

`cint emit-c` writes the SIR text of each module into a file named after the module with the extension `.sites`, beside its C file (7.1):

```console
$ cint emit-c --root . --out out add.ci
$ cat out.stage-0/add.sites
module add
sites add.ci 1:2:14:add.checked.i64 2:2:18:add.checked.i64 3:4:1:fuel.charge 4:4:3:call.enter 5:4:1:print
func @add.add3(%a: I64, %b: I64, %c: I64) -> I64 {
  %t0 = add.checked.i64 %a, %b                   #1 @add.ci:2:14
  %t1 = add.checked.i64 %t0, %c                  #2 @add.ci:2:18
  return %t1
}
script @add {
  fuel.charge 1                                  #3 @add.ci:4:1
  %t0 = const I64 1
  %t1 = const I64 2
  %t2 = const I64 3
  %t3 = call @add.add3(%t0, %t1, %t2)            #4 @add.ci:4:3
  print.begin                                    #5 @add.ci:4:1
  print.value I64 %t3
  print.bytes "\n"
  print.end
}
```

The two additions of `a + b + c` are sites 1 and 2, at the two `+` signs. The script charges 1 unit when its entry begins (site 3). The call charges its unit at the call's site (site 4, `call.enter`), where a call that would exceed the depth limit also faults. The function `add3` has no `fuel.charge` of its own, because it is entered only by calls.

A module that declares an extern has an `extern` line before its functions, and each call of the extern is a `host.call` node. The library of SPEC-03P.2 7.6, example 12:

```cint gain.ci
// gain.ci: a library whose export calls a host function.
extern pure I64 gain(I64 x);

export I64 boost(I64 x) {
    return gain(x) + 1;
}
```

```console
$ cint emit-c --root . --out out gain.ci
$ cat out.stage-0/gain.sites
module gain
sites gain.ci 1:4:12:fuel.charge 2:5:12:host.call 3:5:20:add.checked.i64
extern @gain.gain(I64) -> I64 pure "gain" signature 1d00000063696e742d636f72652d312f747970652d7369676e61747572652f7631000000000000000001000000001414
export func @gain.boost(%x: I64) -> I64 {
  fuel.charge 1                                  #1 @gain.ci:4:12
  %t0 = host.call @gain.gain(%x)                 #2 @gain.ci:5:12
  %t1 = const I64 1
  %t2 = add.checked.i64 %t0, %t1                 #3 @gain.ci:5:20
  return %t2
}
```

The signature is the 33 bytes of the domain string, then no definitions, no size symbols, one parameter, `in I64`, and the result `I64`. A program without externs has no `extern` line, so its text, and its revision identity (6.5), is what it would be without them.

5. `cint-explain` prints the fault sites of a function and its SIR:

```console
$ cint-explain add.ci:add3
module add.ci   revision bc1faa5409ed570368f6f2ed815f89623096edcce3fcde135d6a1db89a37a2f2

function add3   add.sites:3   pure
  signature  (in a: I64, in b: I64, in c: I64) -> I64
  fault sites (2): 2 possible, 0 statically excluded
    add3#1  add.ci:2:14  add.checked.i64  E_OVERFLOW possible
    add3#2  add.ci:2:18  add.checked.i64  E_OVERFLOW possible
...
  sir
func @add.add3(%a: I64, %b: I64, %c: I64) -> I64 {
  %t0 = add.checked.i64 %a, %b                   #1 @add.ci:2:14
  %t1 = add.checked.i64 %t0, %c                  #2 @add.ci:2:18
  return %t1
}
```

The first line gives the revision identity of the program (6.5). SPEC-06P.2 describes the other lines.

<a id="verifier"></a>

### 6.3 Verifier

**Constraints**

1. <a id="SIR-10"></a>Before any executor runs, the verifier checks that every temporary is defined exactly once and before it is used; that the types of the operands match the node's signature exactly; that every node that can fault has a site; that every write goes to a view with write permission or carries the run-time `E_ALIAS` check; that kernel bodies meet the restrictions of SPEC-04P.3 6.13; and that the outputs of each dispatch are covered by one alias check over all its writable bindings. A failure is an internal compiler error that names the node, not a warning. The interpreter checks again, when it reads a SIR text, the rules it relies on (5.3) [SIR-10].
2. <a id="SIR-11"></a>The verifier computes the capabilities that each function and kernel needs. The executor's admission of a program compares this set with its capability table (5.5) [SIR-11].

<a id="transformations"></a>

### 6.4 Transformations

**Constraints**

1. <a id="SIR-12"></a>A transformation of SIR is admissible only if, for every input, it keeps the output bytes, whether a fault occurs, the full fault record (code, operation, operands, exact result, position and address, including the step ordinal), every effect that comes before the fault, and the fuel charged up to any point that can be observed [SIR-12].

<a id="revision-identity"></a>

### 6.5 Revision identity

The revision identity is a digest of a program's SIR with the source positions taken out. Fault records, checkpoints and receipts name a program by it.

**Semantics**

1. <a id="SIR-17"></a>The *hashed encoding* of a module is its SIR text (6.2) with the `sites` line removed and, on every node line, the text ` @<path>:<line>:<column>` after the site ordinal removed. The ordinals `#<n>` stay, and so do the spaces before them; no line or column remains. The revision identity is the SHA-256 digest of these bytes, in order [SIR-17]:
   - the domain `cint-core-1/revision/v1`, the profile name `cint-core-1` and the runtime contract version (7.10), each as a 32-bit little-endian length followed by its ASCII bytes;
   - the number of modules, as a 32-bit little-endian integer;
   - for each module, in byte order of its module-relative path: the path, as a 32-bit little-endian length followed by its UTF-8 bytes, then the hashed encoding, as a 64-bit little-endian length followed by its bytes.

   The fuel model is not an input of the revision identity. A separate *source-map digest* is the SHA-256 digest of the domain `cint-core-1/source-map/v1`, as a 32-bit little-endian length followed by its ASCII bytes; the number of modules, as a 32-bit little-endian integer; and for each module, in the same order, its path, as a 32-bit little-endian length followed by its UTF-8 bytes, its number of sites, as a 32-bit little-endian integer, and for each item of its `sites` line, in the line's order, the ordinal, line and column, as 32-bit little-endian integers, and the operation, as a 32-bit little-endian length followed by its ASCII bytes. Both are recorded with a program. Fault positions are read through the source map recorded with the revision. The interpreter recomputes the revision identity from the text it reads, so a checkpoint, a recording or a case runs on it only with the revision it names.
**Examples**

2. For `add.ci` of 6.2, the hashed encoding of the module `add.ci` is the text of `add.sites` without its second line, with each ` @add.ci:<line>:<column>` removed. Its revision identity is the digest that `cint-explain` printed in 6.2, and the same 32 bytes are the array `cg_revision` of the emitted `cint-program.c`. A copy of the module with a blank line added before it keeps the same revision, while its sites move to line 3:

```console
$ mkdir moved
$ { echo; cat add.ci; } > moved/add.ci
$ cint-explain --root moved add.ci:add3
module add.ci   revision bc1faa5409ed570368f6f2ed815f89623096edcce3fcde135d6a1db89a37a2f2

function add3   add.sites:3   pure
  signature  (in a: I64, in b: I64, in c: I64) -> I64
  fault sites (2): 2 possible, 0 statically excluded
    add3#1  add.ci:3:14  add.checked.i64  E_OVERFLOW possible
    add3#2  add.ci:3:18  add.checked.i64  E_OVERFLOW possible
...
```

**Notes**

3. A blank line added to a module, or a function moved down, changes the source map but not the revision identity, so a checkpoint of a context that has not faulted still restores. Reordering two statements, or adding an operation that can fault, changes the site ordinals and so the revision identity.
4. A checkpoint of a context that holds a fault keeps the position of its fault, and a restore maps that position back to a site by its line and column. After an edit that moves the faulting line, such a checkpoint is refused even though the revision identity has not changed.

<a id="emitted-c-contract"></a>

## 7 Emitted-C contract

The compiler emits C17 that is defined for every input that reaches it: each check comes before the operation it guards, and the C compiler is given nothing it may treat as undefined. The emitted C calls the runtime for checked operations, fuel, call depth and fault records. This clause states what the emitted C and the runtime promise each other.

<a id="the-output-set"></a>

### 7.1 The output set

The compiler writes the files of a program as one set, which a reader sees complete or not at all.

**Semantics**

1. <a id="CINTC-06"></a>The compiler performs no input or output. The bridge reads the files named in the build manifest, in manifest order and not by listing a directory, and passes each module's path relative to the source root, with `/` separators. Outputs are committed as one set [CINTC-06]:
   1. All output files are written into a new sibling directory `<out>.stage-<N>`, where `N` is the first unused decimal integer, created exclusively. Each module's files are written as its `emit` call completes, and the program file after the last module. A diagnostic, a fault or a failed write in any module abandons that directory: the committed set stays as it was, byte for byte, and no new file is visible to a reader.
   2. The bridge writes the file `MANIFEST` into that directory. Its first line is `cint-manifest-1`, and each following line is `<sha256> <bytes> <path>` for one output file, in byte order of path. The bridge flushes every file to stable storage.
   3. The commit is one atomic replacement of the single file `<out>/MANIFEST.ref`, which holds the name of the new directory and the SHA-256 digest of its `MANIFEST`: `rename` on POSIX systems, and `MoveFileExW` with `MOVEFILE_REPLACE_EXISTING` and `MOVEFILE_WRITE_THROUGH` on Windows. These are the only platform calls of the bridge.
   4. Readers trust only the files that the `MANIFEST` named by `MANIFEST.ref` lists, after checking each file's digest. Directories left by earlier builds are removed after a commit when possible; their presence does not change what a reader sees.

   A reader that runs while a build commits sees `MANIFEST.ref` name either the complete old set or the complete new one. No claim is made for single files, or for durability after a crash or a power loss, which depends on the file system.
2. <a id="CINTC-12"></a>A module-relative path is one or more segments separated by `/`. Each segment is made of ASCII letters, digits, `_`, `-` and `.`, and is not empty, `.` or `..`. A path has no leading `/`, no drive letter, no `\` and no `:`. No segment names a Windows device (`CON`, `PRN`, `AUX`, `NUL`, `COM1` to `COM9`, `LPT1` to `LPT9`, in any case) before its first `.`, and no segment ends in `.`. All the paths of one build differ under ASCII case folding. With the final `.ci` removed, every segment is an identifier, so a dotted module name and its path correspond one to one. Any other path is the diagnostic C3030, naming the path and the rule it breaks. The bridge opens every file relative to the source root and refuses symbolic links and other reparse points [CINTC-12].

**Examples**

3. The build of `add.ci` from 6.2:

```console
$ cint emit-c --root . --out out add.ci
$ ls out out.stage-0
out:
MANIFEST.ref

out.stage-0:
MANIFEST
add.c
add.sites
cint-program.c
cint-program.sites
$ cat out/MANIFEST.ref
out.stage-0 06d38f20f86a9e377eca47c7cd7eebc5c8fd7154ee06e93498ed881a63395424
$ cat out.stage-0/MANIFEST
cint-manifest-1
1a4f0c8f856d921cbfa7f478970c9926c77ce455ee776711033781b195708d34 2358 add.c
d83a256fcde82d46d7b00eaa2de7c6e491dc1ac747e3f4f10244d35b758887f5 634 add.sites
2a7330a85849fd42f62eb7f4643131da96de92dc9f2cec08ffeb2a36285b141b 1318 cint-program.c
ce55c32dc70f577c08198ce999ba084cbbf8e0e291a2a4828d6933b052ca778d 16 cint-program.sites
$ sha256sum out.stage-0/MANIFEST
06d38f20f86a9e377eca47c7cd7eebc5c8fd7154ee06e93498ed881a63395424  out.stage-0/MANIFEST
```

`add.c` is the module's C, `add.sites` its SIR text, `cint-program.c` the program file that ties the modules together and holds the revision, and `cint-program.sites` the list of modules that the interpreter reads. The digest in `MANIFEST.ref` is the SHA-256 digest of `MANIFEST`, as an external tool computes it.

**Notes**

4. Microsoft does not document the replacement by `MoveFileExW` as atomic, so on Windows the property of paragraph 1, that a reader sees the complete old set or the complete new one, rests on a test that stops the bridge after each file and checks what a reader sees.

<a id="language-level-and-headers"></a>

### 7.2 Language level and headers

**Constraints**

1. <a id="EMIT-01"></a><a id="RT-2"></a>Emitted C is C17: GCC and Clang compile it with `-std=c17`, and MSVC with `/std:c17`. It includes only `<stdint.h>`, `<stddef.h>`, `<stdbool.h>`, `<string.h>`, `<limits.h>` and `cint_rt.h`. It does not include `<math.h>` or `<float.h>`, and it contains no `float`, `double` or `long double` token. The C sources of the runtime, the bridge, the harness, the `cint` command, the interpreter and the bootstrap compiler contain no floating-point type, literal or header either; `python3 tools/cint_lint.py` checks each of these sources for them [EMIT-01, RT-2].
2. <a id="EMIT-02"></a>Emitted C relies only on behavior that C17 defines for every input that reaches it, and on these properties of the C implementation, each checked when `cint_rt.h` is compiled: `CHAR_BIT` is 8 (a static assertion); `int8_t` to `int64_t` and `uint8_t` to `uint64_t` exist, which in C17 makes the signed exact-width types two's complement (an `#error` otherwise); `size_t` has at least 32 bits (a static assertion); and pointers have 64 bits, so the runtime supports 64-bit hosts only (a static assertion). External identifiers must be significant to at least 247 characters, which no source check can test; a generated symbol longer than 247 bytes is the diagnostic C9002 (7.7). No string literal of emitted C, after adjacent literals are joined, is longer than 4,095 characters, the minimum that C17 5.2.4.1 requires an implementation to accept: the literal text of a print statement reaches the runtime in pieces of at most 1,024 bytes, each written as adjacent literals of at most 64 bytes, and the PTX of a module's kernels is an array of literals, one per line [EMIT-02].
3. <a id="EMIT-24"></a>Emitted C is ASCII with LF line endings, a final LF, no trailing space, no tab and four-space indentation. Its first line is the comment `/* cint-core-1, cint-rt-3, emitted by cintc */`, which names the language profile, the runtime contract and the compiler and nothing else [EMIT-24].

<a id="checks-before-operations"></a>

### 7.3 Checks before operations

In C, a signed overflow, a division by zero and a shift by the width of the type are undefined. Emitted C performs the check that cint requires before the operation, so the undefined case is never evaluated, and it computes the defined result with operations that C defines for all inputs.

**Semantics**

1. <a id="EMIT-03"></a><a id="RT-9"></a>No emitted operation is evaluated when its C result would be undefined. Either the check comes first, or the operation is one that C defines for all inputs: unsigned arithmetic, or a checked builtin. Computing a signed result that overflows and testing it afterward is forbidden. Every helper of the runtime computes a defined C result before it reports any fault [EMIT-03, RT-9].
2. <a id="EMIT-04"></a><a id="EMIT-05"></a><a id="RT-8"></a><a id="RT-12"></a>Emitted C performs a checked operation by calling a helper `bool cint_<op>_<t>(cint_ctx *ctx, cint_site site, ..., T *out)`, such as `cint_add_i64(ctx, site, a, b, &out)`. A helper writes `*out` and returns `true` when the exact result is in range. Otherwise it writes the full fault record into the context, if the context holds none yet, and returns `false` without writing `*out`. A wrapping helper that cannot fault takes no context; a wrapping left shift takes the context and the site, because it still faults `E_SHIFT` for a bad count. The checked add, subtract and multiply have two *helper modes*. With `CINT_RT_HELPERS_BUILTIN`, they use `__builtin_add_overflow`, `__builtin_sub_overflow` and `__builtin_mul_overflow` on GCC and Clang, and the intrinsics `_mul128` and `_umul128` for the multiply on MSVC for x64. With `CINT_RT_HELPERS_PORTABLE`, which `CINT_RT_PORTABLE=1` also selects, they use range tests in `int64_t` and `uint64_t` before the operation. Defining both macros is an `#error`. With neither defined, `cint_rt.h` selects the builtin mode on GCC and Clang and the portable mode elsewhere. The two modes give identical outcomes, and the conformance suite runs in both [EMIT-04, EMIT-05, RT-8, RT-12].
3. <a id="EMIT-07"></a>A wrapping operation (`+%`, `-%`, `*%`, `<<%`) is computed in the unsigned type of the same width, or in `uint64_t` for widths below 64, and the result is mapped back to the signed type by a function that C defines. C17 makes the direct conversion of an unsigned value that is out of range to a signed type implementation-defined (6.3.1.3); emitted C does not contain one [EMIT-07].
4. <a id="EMIT-08"></a>An operand of a type narrower than 64 bits is converted explicitly to `int64_t` or `uint64_t` before arithmetic, and the result is checked against the range of its type, or reduced explicitly, before it is narrowed. This keeps C's promotion to `int` out of every operation [EMIT-08].
5. <a id="EMIT-09"></a>A division or a remainder checks the divisor for 0 (`E_DIV_ZERO`) and, for a signed type, the pair (`MIN`, -1), before any C `/` or `%` is evaluated. For that pair a quotient faults `E_OVERFLOW` and a remainder is 0. cint's `/` and `%` are floor division; emitted C derives the floor, truncating and Euclidean results from C's truncating quotient and remainder with an explicit adjustment [EMIT-09].
6. <a id="EMIT-10"></a>A shift checks its count against the width of the operand before shifting, and faults `E_SHIFT` for a negative count or a count at or above the width. A left shift is carried out on the unsigned representation after the widening of paragraph 4. A checked `<<` then shifts the result back by the same count and compares it with the operand: arithmetically, at the operand's width, for a signed type, and logically for an unsigned type. A difference is `E_OVERFLOW`. An arithmetic right shift of a negative value, which C17 leaves implementation-defined (6.5.7), is emitted as `~(~a >> c)` [EMIT-10].

| Case | Shift back | Outcome |
| --- | --- | --- |
| `I64` `1 << 62` | arithmetic: `2^62 >> 62` is 1 | 4611686018427387904 |
| `I64` `1 << 63` | arithmetic: `MIN >> 63` is -1, not 1 | `E_OVERFLOW` |
| `I64` `-1 << 63` | arithmetic: `MIN >> 63` is -1 | -9223372036854775808 |
| `U64` `1 << 63` | logical: `2^63 >> 63` is 1 | 9223372036854775808 |
| `I8` `64 << 1` | arithmetic at width 8: `-128 >> 1` is -64, not 64 | `E_OVERFLOW` |

7. <a id="EMIT-11"></a>A checked conversion `as` tests the source value against the range of the target before any conversion is evaluated, and the conversion is then exact. The test depends on the signedness of the two types [EMIT-11]:

| Source | Target | Test |
| --- | --- | --- |
| signed | unsigned | a value below 0 faults; otherwise `(uint64_t)v` is compared with the target's maximum as `uint64_t` |
| unsigned | signed | `v`, as `uint64_t`, is compared with the target's maximum converted to `uint64_t`; there is no lower test |
| signed | signed | compared in `int64_t` with the target's minimum and maximum |
| unsigned | unsigned | compared in `uint64_t` with the target's maximum |

8. <a id="EMIT-12"></a>A wide integer, `I128` to `I1024`, is emitted as a struct that holds an array of `uint64_t` limbs, least significant limb first. `__int128` and `_BitInt(N)` do not appear in any interface or stored layout. In the builtin helper mode, the runtime may compute a limb product with `unsigned __int128` on GCC and Clang, written with `__extension__` so that `-Wpedantic -Werror` accepts it, and with `_umul128` and `_udiv128` on MSVC for x64 [EMIT-12].

**Examples**

9. The shifts of the table, on both sides of the limit:

```cint shifts.ci
I64 one = 1;
I64 neg = -1;
U64 u = 1;
"{one << 62} {neg << 63} {u << 63}\n";
I8 b = 64;
I8 c = b << 1;
```

```text
4611686018427387904 -9223372036854775808 9223372036854775808
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> shifts.ci:6:10
   |
 6 | I8 c = b << 1;
   |          ^ shl.checked.i8
   |
   = operation: shl.checked.i8
   = operand-count: 2
   = operand: I8 64
   = operand: I64 1
   = exact:   128
   = limit:   I8 127
...
```

10. A wrapping multiplication of `U16` values. In C, `uint16_t` operands are promoted to `int` and 65535 × 65535 overflows a 32-bit `int`, which is undefined. Emitted C multiplies in `uint64_t` and reduces the product modulo 2^16:

```cint wrap16.ci
U16 a = 65535;
U16 p = a *% a;
"{p}\n";
```

```text
1
```

11. Floor division, the remainder of (`MIN`, -1), and the quotient of the same pair:

```cint divmin.ci
I64 m = I64.min;
I64 d = -1;
I64 a = -7;
I64 two = 2;
"{a / two} {a % two} {m % d}\n";
I64 q = m / d;
```

```text
-4 1 0
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> divmin.ci:6:11
   |
 6 | I64 q = m / d;
   |           ^ div.checked.i64
   |
   = operation: div.checked.i64
   = operand-count: 2
   = operand: I64 -9223372036854775808
   = operand: I64 -1
   = exact:   9223372036854775808
   = limit:   I64 9223372036854775807
...
```

12. Checked conversions at the limits of the table of paragraph 7:

```cint narrow.ci
I64 a = -128;
U64 b = 9223372036854775807;
U8 c = 255;
"{a as I8} {b as I64}\n";
I8 d = c as I8;
```

```text
-128 9223372036854775807
fault[E_NARROW]: the value does not fit the destination type
  --> narrow.ci:5:10
   |
 5 | I8 d = c as I8;
   |          ^ as.checked.u8.i8
   |
   = operation: as.checked.u8.i8
   = operand-count: 1
   = operand: U8 255
   = exact:   255
   = limit:   I8 127
...
```

<a id="memory"></a>

### 7.4 Memory

**Semantics**

1. <a id="EMIT-13"></a>Index and slice checks are computed on integer offsets before any pointer is formed. No emitted expression forms a pointer outside an object or more than one element past its end [EMIT-13].
2. <a id="EMIT-14"></a>The bytes of a buffer are reinterpreted only with `memcpy` or through `unsigned char`. No pointer is cast between distinct object types, so the aliasing rules of C17 6.5 are neither relied on nor broken [EMIT-14].
3. <a id="EMIT-15"></a>Every scalar local is initialized where it is declared, and every array is zeroed where the language says it is. A wide temporary may be left uninitialized at its declaration when the node that defines it comes before every use on every path, which the emitter reads from the structured control flow of SIR [EMIT-15].

<a id="the-shape-of-the-code"></a>

### 7.5 The shape of the code

**Semantics**

1. <a id="EMIT-16"></a>Each SIR node becomes one C statement, with two exceptions: an index check that the emitter shows cannot fail becomes none, and a *counted loop* is emitted twice. A counted loop is a loop whose `I64` counter is compared by `<` with a bound that the loop does not change, is stepped once per iteration by a constant of at least 1, and whose body holds only loads, stores, element and field accesses, arithmetic, conversions, comparisons, built-ins and checks. Before it, one test enters a copy of the loop that charges no fuel per iteration, when the context holds no fault and the fuel left covers every iteration the counter allows; otherwise the loop runs as written. Each exit of the copy first sets the fuel consumed to the count the loop as written would have reached, so the outcome, fault record and fuel are those of the loop as written. No emitted C expression contains more than one operation that can fault or has a side effect, so C's unspecified order of evaluation of operands and arguments never matters [EMIT-16].
2. <a id="EMIT-17"></a>Scalar temporaries are C locals, such as `int64_t t3`, not slots in memory managed by the runtime [EMIT-17].
3. <a id="EMIT-18"></a>Faults do not use `setjmp`, `longjmp` or signals. A function emitted for a cint function follows the convention of the runtime's helpers [EMIT-18]:
   - It returns `bool`: `true` on success, and `false` after the runtime has written the fault record. A function that returns a value writes it through a final `T *result` parameter, and only when it returns `true`.
   - A checked operation calls a helper with a `cint_site`, the pair of the module's index in the program and the site ordinal. When the helper returns `false`, the function jumps to its single fault exit.
   - A call of a cint function is preceded by `cint_rt_call_enter(ctx, site)`, which checks the depth limit, charges the call's fuel and records the call site, and is followed on success by `cint_rt_call_leave(ctx)`. On `false` the caller jumps to its fault exit without leaving, so the runtime keeps every active call site for the `stack` field of the fault record.
   - An entry point is a generated wrapper. It calls `cint_rt_entry_begin(ctx, site, fuel, depth)`, then the function, and returns `cint_rt_entry_end(ctx)`, which is `CINT_FAULT` when the context holds a fault.
   - Outside kernels, a check that cannot fail is not written. A checked remainder `a % c` of type `T` by a constant power of two `c` is written `(T)((uint64_t)a & ((uint64_t)c - 1u))`, which is the floor remainder for any `a` in two's complement. An index check of an array of constant extent `E` is not written when the index is a floor remainder by a constant `c` with `0 < c <= E`. A function left with no statement that jumps to its fault exit has no fault exit.

**Examples**

4. The function `add3` of 6.2, as the compiler emits it in `add.c`. The two additions are sites 1 and 2 of module 0:

```c
static bool ci_3_add_4_add3(cint_ctx *ctx, int64_t v_a, int64_t v_b, int64_t v_c, int64_t *result)
{
    int64_t t0 = 0;
    int64_t t1 = 0;
    (void)ctx;
    (void)v_a;
    (void)v_b;
    (void)v_c;
    (void)result;
    static const cint_site cs1 = {0u, 1u}; (void)cs1;
    static const cint_site cs2 = {0u, 2u}; (void)cs2;
    if (!cint_add_i64(ctx, cs1, v_a, v_b, &t0)) goto fault;
    if (!cint_add_i64(ctx, cs2, t0, v_c, &t1)) goto fault;
    *result = t1;
    return true;
fault:
    return false;
}
```

The script calls it between `cint_rt_call_enter(ctx, cs4)` and `cint_rt_call_leave(ctx)`, and the program's entry, `cg_3_add_run`, begins with `cint_rt_entry_begin(ctx, (cint_site){0u, 3u}, fuel, CINT_DEPTH_FROM_CONFIG)`, which makes the entry's charge at site 3.

<a id="independence-from-the-c-compiler-s-options"></a>

### 7.6 Independence from the C compiler's options

**Semantics**

1. <a id="EMIT-19"></a>The observable behavior of emitted C does not depend on the optimization level, on `-fwrapv`, `-ftrapv` or `-fno-strict-aliasing` or on MSVC's `/O1`, `/O2` or `/Od`, for a program that does not exhaust the C stack. The conformance suite runs at `-O0` and `-O2` (`/Od` and `/O2`) and requires identical outcomes [EMIT-19].
2. <a id="EMIT-20"></a>With GCC and Clang, the conformance suite also runs under `-fsanitize=undefined,address`, UndefinedBehaviorSanitizer and AddressSanitizer, and requires that they report nothing. A sanitized build is a conformance build only [EMIT-20].

**Notes**

3. The sanitized runs are evidence for 7.3 paragraph 1 over the programs run, not a proof for all programs (11.4).

<a id="deterministic-output"></a>

### 7.7 Deterministic output

**Semantics**

1. <a id="EMIT-21"></a>Emitted C is a function of these inputs only: the bytes of the declared input files and their module-relative paths, the compiler source identity, the language profile and the executor and its options. It contains no time, date, host name, user name, absolute path, environment value, random value or digest of the compiler's executable. The C toolchain that built the compiler is not an input [EMIT-21].
2. <a id="EMIT-25"></a>Emitting the same program twice, from different working directories, with different environment variables and on different hosts, gives byte-identical C [EMIT-25].
3. <a id="BACK-02"></a>Every file that an executor writes, PTX text included, follows paragraphs 1 and 2: LF line endings, stable numbering and no time or path [BACK-02].
4. <a id="EMIT-22"></a>Symbol names are built with an encoding that is injective, so two different names never give the same symbol, and self-delimiting, so a symbol splits back into its parts in one way only. A component `s` is encoded as `E(s)`: each byte in `[A-Za-z0-9]` is copied, and every other byte, `_` included, becomes `_x` and two uppercase hexadecimal digits. A component is written C(s), which is `_<L>_<E(s)>`, where `<L>` is the decimal length of `E(s)` in bytes. Every symbol encodes the module-relative path `P` without `.ci`, with `/` separators, never the dotted module name. There are four families: `ci` for user functions, `ci` + C(P) + C(name); `cg` for generated internal names, `cg` + C(P) followed by a generated name; `cx` for the public wrapper of an exported function, `cx` + C(P) + C(name); and `cm` for the public descriptor of a module, `cm` + C(P). The program file also exports `cint_program_abi`, `cint_observer_desc` and `cint_library_desc`. A symbol longer than 247 bytes is the diagnostic C9002, naming the symbol. A `cx` or `cm` symbol that contains `__` is the diagnostic C5051, because C++ reserves such identifiers and the public header is included from C++; so are two exports that map to one public symbol [EMIT-22].

| Module | Name | Symbol |
| --- | --- | --- |
| `add` | `add3` | `ci_3_add_4_add3` |
| `ab` | `x_c_d` | `ci_2_ab_11_x_x5Fc_x5Fd` |
| `ab_c` | `x_d` | `ci_7_ab_x5Fc_6_x_x5Fd` |
| `arith/add` | `f` | `ci_12_arith_x2Fadd_1_f` |
| `add` | `add3`, public wrapper | `cx_3_add_4_add3` |
| `a/b_c` | `f`, public wrapper | `cx_11_a_x2Fb_x5Fc_1_f` |
| `a_b/c` | `f`, public wrapper | `cx_11_a_x5Fb_x2Fc_1_f` |

5. <a id="EMIT-23"></a><a id="RT-6"></a><a id="RT-7"></a>Sites are numbered from 1 in each module, in SIR node order, and 0 means no site. A site is the pair `cint_site { uint32_t module; uint32_t index; }`. The C of each module holds one constant site table, whose row `k` gives the line, column and operation identifier of site `k`, and one module descriptor that holds the module-relative path and the table. A helper that faults receives the site and resolves it into the record's position and operation; emitted code does not format a position itself. No hash-table order or address enters the numbering, so two compilations of one program give identical site tables [EMIT-23, RT-6, RT-7].

**Examples**

6. The same program emitted from two directories, with different time zones, locales and `SOURCE_DATE_EPOCH`:

```console
$ mkdir a b
$ cp add.ci a/ && cp add.ci b/
$ (cd a && TZ=UTC LANG=C cint emit-c --root . --out out add.ci)
$ (cd b && TZ=Asia/Tokyo LANG=C.UTF-8 SOURCE_DATE_EPOCH=1 cint emit-c --root . --out out add.ci)
$ diff -r a/out.stage-0 b/out.stage-0 && echo identical
identical
```

<a id="warnings"></a>

### 7.8 Warnings

**Constraints**

1. <a id="EMIT-26"></a>Emitted C, `cint_rt.h`, the `cint_rt` library, the bridge, the harness and the bootstrap compiler compile with no warning under these flags [EMIT-26]:

| Toolchain | Flags |
| --- | --- |
| GCC | `-std=c17 -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow -Wpedantic -Werror` |
| Clang | the GCC flags and `-Wimplicit-int-conversion` |
| MSVC | `/std:c17 /W4 /WX` |

<a id="symbol-audit"></a>

### 7.9 Symbol audit

**Constraints**

1. <a id="EMIT-31"></a>Every generated object is audited at both optimization levels of the conformance suite. The audit fails on an exported symbol other than the `cx_` and `cm_` symbols, `cint_library_desc`, `cint_imports_desc`, `cint_program_abi` and `cint_observer_desc`; on an external definition that is not exported, other than the `ci_` and `cg_` symbols of the program's own modules; and on any undefined symbol outside this list [EMIT-31]:

| Class | Symbols | Reason |
| --- | --- | --- |
| Runtime | `cint_rt_` and the other `cint_` functions of `rt/cint_rt.h` | the runtime contract |
| Same build | `ci_` and `cg_` symbols of another module, or of the program file, of the same build | imports and site tables |
| C support | `memcpy`, `memset`, `memmove`, `memcmp` | emitted by 7.4, and inserted by GCC, Clang and MSVC for large copies and zeroing |
| MSVC x64 and ARM64 | `__chkstk`, `__security_cookie`, `__security_check_cookie`, `__GSHandlerCheck`, `__report_rangecheckfailure`, `__isa_available` | stack probes for frames over one page; the `/GS` checks, the MSVC default; the CPU feature level that code built with `/O2` reads |
| GCC and Clang | `__stack_chk_fail`, `__stack_chk_guard` | `-fstack-protector-strong` |
| GCC and Clang on x86-64, not macOS | `__udivti3`, `__divti3` | the 128-by-64 division of the runtime's `I64` fast path of `div_round` and `muldiv` in the builtin helper mode, the integer library routine of GCC and of compiler-rt, taken from the static `libgcc.a`; GCC 13.3 also leaves an undefined `__divti3` in the same objects that nothing calls |
| Apple Clang, macOS | `__chkstk_darwin`, `___chkstk_darwin`, `bzero` | the stack probe on AArch64 and x86-64; the zeroing of a large local array at `-O0` |

   A row for a target applies to that target only. A library build (`cint build --lib`) also exports the host functions of the runtime, which carry `CINT_RT_EXPORT` in such a build, and the audit of its export table admits exactly those. The list may shrink when a build removes the stack protector with `-fno-stack-protector` or `/GS-`; such a build records the flag in its receipt.

<a id="the-runtime-contract"></a>

### 7.10 The runtime contract

The runtime is the header `rt/cint_rt.h` and the library `cint_rt`. The header is the normative statement of the contract; this subclause explains it.

**Semantics**

1. <a id="RCPT-08"></a>The *runtime contract version* is a string `cint-rt-<N>` that names the interface and meaning of `cint_rt.h` and the `cint_rt` library on which emitted code depends: the names and signatures of the helpers, the layout of the fault record and the layout of the site table. It changes when any of these changes incompatibly, and it is an input of the revision identity (6.5). In cint 0.2.2 it is `cint-rt-3`, and the header defines the interface version `CINT_ABI_VERSION` as `0x00030004`, which adds host functions, grants and program arguments to the context's configuration (SPEC-03P.2 7.2). A checkpoint binds to the revision identity; the compiler source identity is recorded beside it for identification only, so a new compiler that leaves the SIR and the runtime contract unchanged does not invalidate checkpoints [RCPT-08].
2. <a id="RT-3"></a>Every symbol that the runtime exports is in the `cint_` namespace. The helpers are `static inline` and add no undefined symbol to a generated object [RT-3].
3. <a id="RT-4"></a>A context, `cint_ctx`, is an opaque object that holds one instance of a program's state from its creation to its destruction. Emitted code passes the pointer to helpers and does not read its fields. For each entry it holds the fuel budget and the fuel consumed, the depth limit and the current depth, and the stack of call sites. Across entries it holds module state, registered buffers, arenas and the fault record [RT-4].
4. <a id="RT-10"></a>Faults are sticky. The first fault of an entry writes the fault record once. A later helper call on a context that holds a fault returns `false` and changes nothing. Emitted code tests every helper result and leaves through the function's single fault exit. Only the host clears the record, with `cint_ctx_clear_fault`, which changes no other state [RT-10].
5. <a id="RT-11"></a>A call checks the depth limit first, faulting `E_DEPTH` without a fuel charge, then charges one unit of fuel, then records its call site. A loop iteration charges one unit after its condition holds. A charge that would take the fuel consumed above the budget faults `E_FUEL` and is not consumed. The entry itself is a call from the host: it charges one unit and adds no position to the stack. A fuel budget of `CINT_FUEL_UNBOUNDED`, which is -1, counts fuel and never faults `E_FUEL`. The runtime provides frames for a depth limit of up to `CINT_RT_MAX_DEPTH`, 1,024; an entry with a larger limit faults `E_UNSUPPORTED` before it runs. Clause 12 compares this limit with the minimum of SPEC-04P.3 [RT-11].
6. <a id="RT-13"></a><a id="RT-14"></a>`cint_fault_record` is a C structure with every field of the fault record of SPEC-04P.3 8.1: the code, the operation, up to 8 typed operands, the exact result, the limit, the position, the revision, the address and the stack. An exact result or operand of a wide type takes at most 257 bytes in fault record version 2 and 264 bytes in version 3. Static assertions fix the size and field offsets of the structure. The structure is a convenience; the canonical form of a record is its bytes, which `cint_fault_encode` writes: little-endian, with minimal integer encodings, tagged operands and no padding, as SPEC-01P.3 specifies. Conformance compares these bytes, not the structure [RT-13, RT-14].
7. <a id="RT-15"></a>Each context holds one atomic flag. Every library function that touches the context sets it on entry by an atomic exchange and clears it on return. A call that finds the flag set returns `CINT_BUSY` and changes nothing. The flag detects a second caller; it does not make a context usable from two threads at once [RT-15].

**Notes**

8. The status values of `rt/cint_rt.h` are `CINT_OK` (0), `CINT_FAULT` (1), `CINT_REFUSED` (2), `CINT_BUSY` (3), `CINT_FAULTED` (4) and `CINT_RESOURCE` (5). SPEC-03P.2 specifies what each means to a host program.

<a id="agreement"></a>

## 8 Agreement

Two implementations of cint agree on a program when every observable part of its outcome is the same. This clause says what is compared, what is not, and how a case that is not compared is reported, so that a refusal is never counted as agreement.

<a id="what-is-compared"></a>

### 8.1 What is compared

**Semantics**

1. <a id="REF-06"></a>A program is in the scope of both the reference and a compiler when the reference runs it and the compiler builds it without a diagnostic whose code begins with `C9`. For such a program and the same semantic inputs, the two agree as a conformance case does: the same bytes on standard output; the same kind of outcome; the same returned value; the same fuel consumed; and the same fault record in every field except the revision and the source-map digest, which the reference does not compute. For a program that both reject, they agree when they report the same diagnostic code at the same position. A disagreement is a defect of one of them or an ambiguity in the text; it is not settled by choosing one. The program, reduced to a case, joins the suite. Agreement on one program is a measurement of that program and shows nothing about another [REF-06].
2. <a id="DIAG-01"></a>A position, in a diagnostic, a fault record, SIR text or an `.expect` file, is written `<path>:<line>:<column>`. `<path>` is the module-relative path, `.ci` included, with `/` separators. Lines count from 1. Columns count from 1 in Unicode scalar values, not bytes [DIAG-01].
3. <a id="DIAG-02"></a>Codes have two separate sets of names. A fault code is one of the `E_` names of SPEC-04P.3 5.2.3. A diagnostic code is `C` and four digits, listed in Annex B of SPEC-04P.3. A constant expression whose evaluation meets a fault condition is the diagnostic C6001, which carries the fault code, and C6002 when it runs out of compile-time fuel. A diagnostic that reports a limit of an implementation has a code that begins with `C9`: C9001 for a capacity or an output size, C9002 for a generated symbol longer than 247 bytes, C9003 for a local array over the frame limit, and C9004 for nesting beyond the implementation's limit [DIAG-02].
4. These are not compared: the message text of a diagnostic, the reason given for a refused entry, executables and their digests, timings and any other observation of the host.

**Examples**

5. The `+` below is the sixth Unicode scalar value of its line and the seventh byte, because `é` takes two bytes in UTF-8:

```cint column.ci
I8 a = 100;
"é{a + a}\n";
```

```text
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> column.ci:2:6
...
```

<a id="categories-of-a-case"></a>

### 8.2 Categories of a case

**Semantics**

1. <a id="CONF-14"></a>A conformance run reports each case in exactly one category. The category depends only on the case, its frozen expectation and the compiler under test, not on the C toolchain, so it belongs in the outcome part of the receipt [CONF-14]:
   1. *Compared.* The implementation produced an outcome and it was compared with the frozen expectation. An expected diagnostic is compared too: the frozen outcome is a compile error, and the compiler reports the same code at the same position.
   2. *Unsupported by this implementation.* The frozen outcome is a value, a fault or a compile error with another code or position; the compiler reports a `C9` code under a documented limit; and the file `conformance/unsupported.txt` lists that case, compiler, code and limit. For `gpu-cuda`, the refusal `E_UNSUPPORTED` of 5.5 takes the place of the `C9` code. An unsupported case is a named gap, not agreement.
   3. *Held.* The file `conformance/held.txt` lists the case, which has no frozen expectation.
   4. *Outside the subset.* The compiler under test accepts only a subset of the language (10.2), the case is outside it and the compiler reports the diagnostic for that. 
   5. *Not applicable.* An operation fixture whose operation has no operator form in the subset that the compiler accepts, so that no program exists for it.

   Any other result is a disagreement: a `C9` code that `conformance/unsupported.txt` does not list, a refusal by a subset compiler of a case inside its subset, or a compile error whose code or position differs from the frozen expectation. A case that the list names as unsupported and that the compiler accepts is also a disagreement, until the list is corrected.
2. Each line of `conformance/unsupported.txt` is `<case> <compiler> <code> <limit> <reason>`. For `gpu-cuda` the compiler is `cintc/gpu-cuda`, the code is `E_UNSUPPORTED`, and the limit is `BACK-05 capability <name>`, where `<name>` is the capability that the refusal names; a refusal that names another capability is a disagreement. For example, the line `control/nest_blocks_200 cint-seed C9004 SEED-15 the seed compiles at most 199 nested blocks` lists a case that the seed refuses under its nesting limit (10.2, paragraph 8).

<a id="disagreement"></a>

### 8.3 Disagreement

**Semantics**

1. <a id="CONF-04"></a>Any disagreement fails the run. A disagreement between the reference and a hand-derived anchor expectation is a defect of the reference, and it also fails the run until the reference is fixed or the expectation is corrected by an erratum (9.5) [CONF-04].

<a id="the-run-matrix"></a>

### 8.4 The run matrix

**Semantics**

1. <a id="CONF-03"></a>A conformance run executes every case on the reference and on every build in this matrix, and requires identical canonical outcomes across all of them [CONF-03]:

| Axis | Values |
| --- | --- |
| C toolchain | GCC on Linux x86-64; Clang on Linux x86-64 and Apple Clang on macOS AArch64; MSVC on Windows x86-64 |
| Optimization | `-O0` and `-O2` (`/Od` and `/O2`) |
| Helper mode | `CINT_RT_HELPERS_BUILTIN` and `CINT_RT_HELPERS_PORTABLE` |
| Compiler | the `cintc` built by the bootstrap (10.3), and the bootstrap compiler on the cases of its subset (10.2) |
| Executor | `cpu-c17` on every case; `cpu-sir-interp` on every case; `gpu-cuda` on the cases its capability table supports, the others producing `E_UNSUPPORTED` at translation |

<a id="parallel-executors"></a>

### 8.5 Parallel executors

An executor may run the work-items of a dispatch in parallel, but its outcome is the one of running them one at a time in canonical order.

**Semantics**

1. <a id="BACK-09"></a>The canonical outcome of a dispatch, fuel included, is the outcome of running its work-items one at a time in canonical order, as `cpu-c17` does. The dispatch first charges `N`, the number of its work-items. A parallel executor computes the outcome as follows, where `F` is the fuel left when the first work-item starts, `c(i, s)` is the fuel that item `i` has charged through its step `s`, and `C(i)` is the total charge of item `i` when it runs to its end or to its first fault other than `E_FUEL` [BACK-09]:
   - `k_fault` is the least item with a fault other than `E_FUEL`, and `s_fault` the step of that fault.
   - `k_fuel` is the least item `i` with a charge point `s` such that `C(0) + ... + C(i - 1) + c(i, s) > F`, and `s_fuel` is the first such `s`. Without a budget there is no `k_fuel`.
   - The canonical fault is the lesser of (`k_fault`, `s_fault`) and (`k_fuel`, `s_fuel`) in work-item, then step, order; the second is `E_FUEL`.
   - The fuel consumed by the dispatch is `N` plus the charges of items 0 to `k - 1` plus the charges of item `k` before its faulting step, where `k` is the item of the canonical fault; a charge that faults `E_FUEL` is not consumed. Without a fault it is `N` plus the sum of all `C(i)`.

   Which work-item failed first on the hardware cannot be observed.
2. <a id="BACK-10"></a>The outputs of a dispatch that faults are not published [BACK-10].
3. <a id="BACK-12"></a>Running a work-item again to find or describe the canonical fault charges no fuel. The canonical fuel is computed once, by paragraph 1 [BACK-12].

**Examples**

4. A dispatch of 1,000 work-items, each of which charges 10 units in its loop. Item 900 overflows on its second iteration:

```console
$ cat dispatch_fuel.ci
kernel tenfold[n](in I64[n] a, out I64[n] b) over [i: n] {
    I64 s = 0;
    for j in 0..10 {
        s += a[i];
    }
    b[i] = s;
}

export I64 run() {
    I64[1000] a;
    I64[1000] b;
    a[900] = I64.max;
    tenfold(a, b);
    return b[0];
}
$ python3 -m cint_ref run dispatch_fuel.ci --entry run --format 4
...
fuel-consumed 10003
fault.code E_OVERFLOW
fault.operation add.checked.i64
...
fault.kernel dispatch_fuel.tenfold
fault.address 0 900 3
...
$ python3 -m cint_ref run dispatch_fuel.ci --entry run --format 4 --fuel 6001
...
fuel-consumed 6001
fault.code E_FUEL
fault.operation fuel.charge
fault.exact none
fault.limit I64 6001
fault.position dispatch_fuel.ci:3:5
...
fault.kernel dispatch_fuel.tenfold
fault.address 0 500 0
...
```

Without a budget, the entry consumes 1 unit, the dispatch 1,000, items 0 to 899 10 each, and item 900 2 before its overflow: 10,003. With a budget of 6,001, `F` is 5,000 after the entry's unit and the dispatch's 1,000, items 0 to 499 consume it, and the first charge of item 500 faults `E_FUEL`. The overflow at item 900 is not reported, because item 500 comes first. The address is the dispatch, the work-item and the step.

<a id="the-conformance-suite"></a>

## 9 The conformance suite

The conformance suite, in `conformance/`, holds the cases on which every implementation must agree, with their expected outcomes. Most expectations come from the reference, and the boundary cases are derived by hand.

<a id="case-files"></a>

### 9.1 Case files

**Semantics**

1. <a id="CONF-01"></a>There are two kinds of case, with one rule of agreement (8.1). An *operation fixture* is one operation on given operands, in a CIF-1 file under `conformance/integer-machine/`, one JSON object per line. A *program case* is a `.ci` file and its `.expect` file, for whole programs: control flow, views, kernels, diagnostics and fuel. Each operation fixture is also run through generated *table programs*, `conformance/tables/<name>.ci` with the list of calls `<name>.cases`, so both kinds run on every implementation. A program case begins with a header comment of lines `// case: <name>`, `// clause: <rules>` and `// entry: <entries>; args: <arguments>[; fuel <n>][; depth <n>]`; without an `entry` line, the case runs its script, else its function `main`, else its only test. A case whose `main` takes the program arguments may list them in a line `// argv: <word>, <word>, ...` of words that hold no comma, and every executor gives them to the program entry as `cint run FILE.ci -- <word>...` does; without the line, `main` receives none. A case with several entries has one file `<case>.<entry>.expect` for each. The `.expect` file is canonical text in the grammar of 9.3 [CONF-01].
2. <a id="CONF-02"></a>An expectation comes from one of three sources, which its `source` line states: `anchor`, derived by hand, with the derivation written beside it, and used for boundary cases and to check the reference itself; `reference`, written by the reference with `conformance/tools/gen_expect.py` and then frozen; or `compile-error`, the expected diagnostic code and position. A frozen expectation is not edited in place. It changes in one of three ways [CONF-02]:
   - A change of meaning (a value, a fault code or content, a fuel count or a diagnostic code or position) comes with a new language profile, which has its own frozen expectations. A change of encoding with the same content comes with a new version of that encoding: a new domain string, a new runtime contract version (7.10) or a new `.expect` format (9.3). Frozen files of the old version stay valid and are read in their own format.
   - A `reference` expectation found wrong because of a defect of the reference, with the specification text unchanged, is replaced by a recorded erratum, a file `conformance/errata/<id>.txt` that names the case, the old and the new expectation, the clause text that decides it, and who checked it. Errata are part of the digest of the suite. An `anchor` expectation is corrected only by an erratum of the same form, which names two people who checked it.
   - A change to the `clause` line alone, which no comparison reads, regenerates the file with `conformance/tools/gen_expect.py` in a change of its own, in which every other line stays identical and after which `gen_expect.py --check` reports that no file differs.

<a id="operation-fixtures-and-coverage"></a>

### 9.2 Operation fixtures and coverage

**Semantics**

1. A CIF-1 line is one JSON object with the keys `id`, `status`, `op`, `args` and `expect`, in that order. `op` is an operation identifier, `args` a list of typed values `{"t": <type>, "v": <decimal>}`, and `expect` holds either a `value` or a `fault` with its code and, where the record has them, its exact result and limit. Every integer is a JSON string of decimal digits, so no JSON reader loses precision. A hand-derived anchor adds a key `derivation` that shows the arithmetic.
2. The suite holds these fixtures: `anchors.cif1.jsonl`, the anchors; `exh8/`, every operand pair of the 14 binary operator forms (`+ - * / % << >> +% -% *% <<% +| -| *|`) on `I8` and `U8`, 1,835,008 records; `bnd64/`, the `I64` boundary matrix of the 16 values `MIN`, `MIN + 1`, -2^32, -2^31, -2, -1, 0, 1, 2, 2^31 - 1, 2^31, 2^32, 3037000499, 3037000500, `MAX - 1` and `MAX` for each form (3037000499 and 3037000500 bracket the square root of 2^63); and `bnd128/` to `bnd1024/`, the boundary matrices of the wide types.
3. <a id="CONF-08"></a>Every implementation that evaluates constants is also run on the *constant and run-time category*: each boundary-matrix and exhaustive 8-bit expression is evaluated as a constant expression and at run time from variables, and the two must give the same value, or the same fault code, operation and operands, which the constant program reports as the diagnostic C6001. If the front end folded `I8` `100 +| 100` to anything other than 127, the two programs would disagree [CONF-08].

**Examples**

4. The reference checks the anchors against their hand-derived values:

```console
$ python3 -m cint_ref cif1 conformance/integer-machine/anchors.cif1.jsonl
cif1: 300 records, 300 agree, 0 disagree, 0 unsupported, 0 invalid
```

<a id="the-expect-format"></a>

### 9.3 The `.expect` format

The `.expect` format is the canonical text of an outcome: one field per line, in a fixed order, with every integer in exact decimal.

**Syntax**

1. <a id="CONF-11"></a>An `.expect` file is the text that this grammar derives. The notation is ABNF (RFC 5234) with the case-sensitive strings `%s"..."` of RFC 7405; `ALPHA`, `DIGIT`, `SP`, `LF` and `VCHAR` are the core rules of RFC 5234. A file whose third line is `format 2`, `format 3`, `format 4` or `format 5` is a file of that format. A file without that line is a format 1 file, stays valid and is compared in format 1. Keys appear in this order: `case`, `clause`, `format`, `source`, `outcome`, `stdout-bytes`, `stdout-sha256`, `return`, `fuel-consumed`, the `error.` keys, the `fault.` keys, the `diagnostic.` keys, `refused.reason` and `state.global` [CONF-11].

```abnf
expect-file      = case-line clause-line [ format-line ]
                   ( run-case / compile-case / refused-case )
format-line      = %s"format 2" LF                   ; absent: format 1

case-line        = %s"case" SP case-name LF
case-name        = segment *( "/" segment )
segment          = 1*( ALPHA / DIGIT / "_" / "-" / "." )
clause-line      = %s"clause" SP text LF
text             = VCHAR *( *SP VCHAR )            ; printable ASCII, no edge space

run-case         = %s"source" SP ( %s"anchor" / %s"reference" ) LF
                   ( value-body / fault-body ) *state-line   ; state-line: format 2
state-line       = %s"state.global" SP module-name SP name SP typed-value LF
value-body       = %s"outcome value" LF output-lines [ return-line ] fuel-line
fault-body       = %s"outcome fault" LF output-lines fuel-line fault-record
output-lines     = %s"stdout-bytes" SP count LF
                   [ %s"stdout-sha256" SP 64lhex LF ]
return-line      = %s"return" SP typed-value LF
fuel-line        = %s"fuel-consumed" SP count LF
fault-record     = %s"fault.code" SP fault-code LF
                   %s"fault.operation" SP op-id LF
                   *( %s"fault.operand" SP typed-value LF )
                   %s"fault.exact" SP ( int / %s"none" ) LF
                   %s"fault.limit" SP ( typed-value / %s"none" ) LF
                   %s"fault.position" SP position LF
                   %s"fault.revision self" LF
                   [ %s"fault.source-map self" LF ]      ; format 2
                   %s"fault.address" SP ( %s"none" / address ) LF
                   %s"fault.stack-depth" SP count LF
                   *( %s"fault.stack" SP position LF )   ; format 2

compile-case     = %s"source compile-error" LF %s"outcome compile-error" LF
                   %s"diagnostic.code" SP c-code LF
                   %s"diagnostic.position" SP position LF
                   [ diagnostic-fault ]
diagnostic-fault = %s"diagnostic.fault.code" SP fault-code LF
                   %s"diagnostic.fault.operation" SP op-id LF
                   *( %s"diagnostic.fault.operand" SP typed-value LF )
                   %s"diagnostic.fault.exact" SP ( int / %s"none" ) LF
                   %s"diagnostic.fault.limit" SP ( typed-value / %s"none" ) LF

refused-case     = %s"source reference" LF %s"outcome refused" LF
                   %s"refused.reason" SP text LF

typed-value      = int-type SP int / %s"Bool" SP ( %s"true" / %s"false" )
                   / %s"Z" SP int
int-type         = %s"I" ( "8" / "16" / "32" / "64" / "128" / "256" / "512" / "1024" )
                   / %s"U" ( "8" / "16" / "32" / "64" )
fault-code       = %s"E_" 1*( %x41-5A / "_" )
op-id            = 1*( %x61-7A / DIGIT / "_" / "." )
c-code           = %s"C" 4DIGIT
position         = segment *( "/" segment ) ":" pos-int ":" pos-int
address          = count SP count SP count         ; dispatch, work-item, step
int              = [ "-" ] count
count            = "0" / %x31-39 *DIGIT
pos-int          = %x31-39 *DIGIT
lhex             = DIGIT / %x61-66                 ; lowercase hexadecimal digit
module-name      = name *( "." name )
name             = ( ALPHA / "_" ) *( ALPHA / DIGIT / "_" )
```

2. Format 3 adds the outcome `error`, for an entry that returns an error, by incremental alternatives (`=/`), so the rules above are unchanged:

```abnf
format-line      =/ %s"format 3" LF
run-case         =/ %s"source" SP ( %s"anchor" / %s"reference" ) LF
                    error-body *state-line
error-body       = %s"outcome error" LF output-lines fuel-line error-value
error-value      = %s"error.set" SP [ module-name "." ] name LF
                   %s"error.value" SP [ name "." ] name LF
                   %s"error.tag" SP uint-type SP pos-int LF
uint-type        = %s"U" ( "8" / "16" / "32" / "64" )
```

3. Format 4 adds the fault record of a kernel dispatch:

```abnf
format-line      =/ %s"format 4" LF
fault-record     =/ %s"fault.code" SP fault-code LF
                    %s"fault.operation" SP op-id LF
                    *( %s"fault.operand" SP typed-value LF )
                    *( %s"fault.descriptor" SP descriptor LF )
                    %s"fault.exact" SP ( int / %s"none" ) LF
                    %s"fault.limit" SP ( typed-value / %s"none" ) LF
                    %s"fault.position" SP position LF
                    %s"fault.revision self" LF
                    %s"fault.source-map self" LF
                    %s"fault.phase" SP phase LF
                    %s"fault.kernel" SP kernel-name LF
                    %s"fault.address" SP kernel-address LF
                    %s"fault.stack-depth" SP count LF
                    *( %s"fault.stack" SP position LF )
kernel-address   = count SP ( count SP count / %s"none none" )
                                                   ; dispatch, then work-item and step
phase            = %s"entry" / %s"work-item" / %s"epilogue"
kernel-name      = module-name "." name
descriptor       = elem-type SP ( count SP count / %s"none" ) SP count SP int
                   1*( SP count SP int SP int ) SP ( %s"read" / %s"write" )
elem-type        = int-type
```

4. Format 5 adds fixed-point values, the unsigned wide types and fixed-point element types:

```abnf
format-line      =/ %s"format 5" LF
typed-value      =/ q-type SP %s"raw" SP int
int-type         =/ %s"U" ( "128" / "256" / "512" )
elem-type        =/ q-type
q-type           = %s"Q" pos-int "." count          ; i + f is 8, 16, 32, 64 or 128
```

**Constraints**

5. These rules hold in addition to the grammar:
   1. The file is ASCII with an LF after every line, the last included. It contains no CR, no tab and no trailing space.
   2. `stdout-sha256` is present exactly when `stdout-bytes` is not 0, and is the SHA-256 digest of the output bytes.
   3. `return` is present exactly when the entry returns a value. A `void` function, a test block and a script have no `return` line.
   4. There is one `fault.operand` line for each operand of the record, in the record's order. `fault.exact` and `fault.limit` are `none` when the record has no such field.
   5. `fault.revision self` means that the record's revision must equal the revision identity of the program under test, and `fault.source-map self` the same for the source-map digest; both are compared only between builds of `cintc`. `fault.address` gives the dispatch, the work-item as one linear index and the step. `fault.stack-depth` is the number of positions in the record's stack. A format 2 file writes `fault.source-map self` in every fault record and one `fault.stack` line per position, outermost first; a format 1 file writes neither.
   6. `diagnostic-fault` is present exactly when the code is C6001 or C6002. The message text of the diagnostic is not part of the file.
   7. The outcome `refused` means that the host interface refused the entry before it started, for example for a fuel budget below 0. No execution exists, so the file has no output or fuel line. The `refused.reason` text is informative and is not compared. An implementation that cannot decide a case does not produce a frozen expectation for it.
   8. The `clause` value is free text. A case cites the rules it exercises by their clause identifiers, with `;` between documents.
   9. Two `.expect` texts agree when they are equal byte for byte, except the `refused.reason` line and the `fault.revision` and `fault.source-map` comparisons of rule 5. A format 1 file is compared in format 1.
   10. A program with module-level variables writes, in format 2 and later, one `state.global` line for each scalar module-level variable after the outcome lines: modules in the order of the program's module table, the root module first and then each imported module at its first import, breadth first; within a module, in declaration order.
   11. Lines have no length bound in the grammar. A reader accepts a `Z` operand of 1,235 characters and an exact result of 1,253 characters, the largest a record of `cint-core-1` can hold.
   12. In format 3, `error.set` is the error set of the entry's result type, qualified by its module, `error.value` the name of the value and `error.tag` its tag with the set's underlying type. For a test that an error leaves by `try`, `error.set` is the set of that error. The `error` outcome has no `return` line.
   13. In format 4, a fault raised by a kernel dispatch takes the kernel form of `fault-record`, and any other fault the form of format 2. `fault.kernel` is the kernel's qualified name. A fault in the `entry` or `epilogue` phase writes its dispatch number and `none none`; a `work-item` fault writes the dispatch number, the work-item and the step. A record that carries view descriptors writes one `fault.descriptor` line for each, after its operands: the element type, the buffer identifier and generation (`none` for arrays of a `.ci` program), the rank, the origin and for each dimension its extent, lower bound and stride, then the permission.
   14. In format 5, a fixed-point value is its type, the word `raw` and its storage integer: `Q16.16 raw 98304` is 1.5. A fixed-point type's `i + f` is 8, 16, 32, 64 or 128, and its raw value lies in the range of a signed integer of that width. A fixed-point `exact` keeps the `int` form, as the fault record holds it. `U128` to `U512` are the unsigned wide types.

   A file of a later format may use every line of the earlier formats, and files of earlier formats are not rewritten. `conformance/tools/check_expect_abnf.py` checks every `.expect` file of the suite against this grammar and the rules that a single file can show.

**Examples**

6. The expectation of the case `arith/add_i64_overflow` is in format 1:

```console
$ cat conformance/arith/add_i64_overflow.expect
case arith/add_i64_overflow
clause SPEC-01 IM-30, IM-32, IM-106, IM-107
source reference
outcome fault
stdout-bytes 0
fuel-consumed 1
fault.code E_OVERFLOW
fault.operation add.checked.i64
fault.operand I64 9223372036854775807
fault.operand I64 1
fault.exact 9223372036854775808
fault.limit I64 9223372036854775807
fault.position arith/add_i64_overflow.ci:5:15
fault.revision self
fault.address none
fault.stack-depth 0
```

The case is a test block, which is its entry and charges 1 unit. The fault is in the entry's own body, so the stack is empty. The print after the addition does not run, so no byte was written. The program is lines 2 to 7 of `conformance/arith/add_i64_overflow.ci`, after its header comment:

```cint
test "add overflow at I64 max" {
    I64 a = 9223372036854775807;
    I64 b = 1;
    I64 c = a + b;
    "c={c}\n";
}
```

<a id="lists-of-the-suite"></a>

### 9.4 Lists of the suite

**Semantics**

1. `conformance/held.txt` lists, one per line, the program cases that have no frozen expectation, each with its reason. A held case is not compared.
2. `conformance/unsupported.txt` lists the cases that an implementation refuses under a documented limit, in the line format of 8.2.
3. `conformance/errata/` holds the errata of 9.1.

<a id="required-case-lists"></a>

### 9.5 Required case lists

**Semantics**

1. <a id="CONF-15"></a>A *required case list* is a file `conformance/required/<name>.txt` with one line `<case> <compiler> <category>` for each case and compiler, where the category is one of 8.2 and, where the category needs one, the line adds the limit. The compiler column names the executor as well: `cintc` for the compiler's C on `cpu-c17`, `cintc/cpu-sir-interp` for its SIR text on the interpreter, and `cintc/gpu-cuda` for `gpu-cuda`. `python3 tools/cint_check.py --required FILE` fails a run when a listed case is absent, a case of the suite is not listed, a case's category differs from the list, a compile error's code or position differs from the frozen expectation, or a case has no `.expect` file and is not held [CONF-15].

<a id="observing-compiled-programs"></a>

### 9.6 Observing compiled programs

**Semantics**

1. <a id="CONF-10"></a>A compiled conformance program is observed through `cint-harness`, a C17 program. It reads a *case list*, one call per line, `<module> <function> <typed arguments>`, and calls each exported function through its observer entry with a fresh context and the case's fuel budget. It writes the canonical outcome (the returned value, or the full fault record from the context, and the fuel consumed) in `.expect` syntax. The reference evaluates the same calls. The same harness serves every compiler under test, so all are compared through the same path. A program that defines the host import descriptor `cint_imports_desc` (SPEC-03P.2 7.6) has it passed to each fresh context, with no host table and no grant set, so each call of a host import faults `host.call` and each service call that needs a capability returns `denied` [CONF-10].
2. <a id="CONF-10a"></a>The interpreter is observed the same way: `cint-interp observe` takes the command line of `cint-harness`, with a directory of SIR text in place of a program library, and writes the same text. `tools/cint_check.py --interp` runs it on every compared case [CONF-10a].
3. <a id="CONF-12"></a>A case list is the text that this grammar derives, with the core rules of 9.3. Line `n` is call `n`, and each call runs in a fresh context [CONF-12]:

```abnf
case-list        = *call-line
call-line        = module-name SP name *( SP typed-value ) LF
```

4. <a id="CONF-13"></a>A program built for the harness follows the observer interface `cint-observe-1` [CONF-13]:
   - It is one shared library built from the C of all its modules, the program file and the `cint_rt` library of the same version as the harness. Its exported symbols have C linkage and carry `CINT_RT_EXPORT`. The harness resolves the program's path before it loads it.
   - It exports exactly two observer symbols, both defined in the program file: `const uint32_t cint_program_abi = CINT_ABI_VERSION;` and `const cint_observer cint_observer_desc;`. The descriptor names the program (every module's site table, the root module first) and lists one entry for every exported function of every module whose parameters and result are scalars, in module order and then declaration order. The harness does not call a program whose `cint_program_abi` differs from its own. The program file also defines the library descriptor `cint_library_desc` and the host import descriptor `cint_imports_desc` of SPEC-03P.2, which are not part of the observer interface.
   - Each entry carries its module index, its name, a type signature of its result and at most 16 parameters (the tags of `I8` to `I64`, `U8` to `U64` and `Bool`) and its function, of type `cint_status (*)(cint_ctx *ctx, int64_t fuel, int64_t depth, const uint64_t *args, uint64_t *result)`. An argument is a 64-bit pattern: sign-extended for a signed type, zero-extended for an unsigned type, 0 or 1 for `Bool`. The function calls `cint_rt_entry_begin(ctx, <its entry site>, fuel, depth)`, returns that status unless it is `CINT_OK`, then runs the body and returns `cint_rt_entry_end(ctx)`. It writes `*result` only when there is a result and the status is `CINT_OK`.
   - Entry functions are `static`, named `cg` + C(P) + `_o<N>`, with type signatures `cg` + C(P) + `_t<N>`, numbered in each module in declaration order (7.7). Each module's C file defines its observer table `cg` + C(P) + `_obs` and its site table with external linkage, not exported, so that the program file can name them.

   A change to this interface is a new version, `cint-observe-2`. The layouts of `cint_observer` and `cint_observer_entry` are declared in `rt/cint_rt.h`, which governs where it and this paragraph differ.

<a id="further-checks-of-a-run"></a>

### 9.7 Further checks of a run

**Semantics**

1. <a id="CONF-05"></a>For each conformance program, the compiler is run twice: from two different working directories, with the source tree at two different absolute paths, and with different values of `TZ`, `LANG` and `SOURCE_DATE_EPOCH`. The two sets of emitted C are compared byte for byte, and a difference fails the run [CONF-05].
2. <a id="CONF-16"></a>The reference writes the canonical type signature of each export (SPEC-03P.2), and `tools/cint_check.py` compares it, for every export of the suite that has a public wrapper, with the bytes that the compiler under test writes into the program's reflection table. A difference fails the run as an outcome difference does [CONF-16].
3. <a id="CONF-17"></a>A conformance run on `gpu-cuda` uses the same frozen expectations. Each compared case runs at two block sizes, 32 and 256 work-items (`tools/cint_check.py --backend gpu-cuda --block 32` and `--block 256`), on the device. The boundary matrices of `bnd64/` also run as kernels, through the kernel tables `conformance/tables/kernel_<op>_<form>_i64.ci`: one dispatch over the rows that do not fault, and one dispatch for each row that faults, with expectations in format 4. A case that the capability table of `gpu-cuda` does not support is unsupported, with its line in `conformance/unsupported.txt` naming the capability (8.2) [CONF-17].

<a id="running-one-case"></a>

### 9.8 Running one case

**Examples**

1. The reference checks one case against its frozen expectation. It prints nothing and exits with status 0 when they agree, and names the first line that differs otherwise:

```console
$ python3 -m cint_ref check conformance/arith/add_i64_overflow.ci conformance/arith/add_i64_overflow.expect
$ echo $?
0
$ sed 's/^fuel-consumed 1$/fuel-consumed 2/' conformance/arith/add_i64_overflow.expect > wrong.expect
$ python3 -m cint_ref check conformance/arith/add_i64_overflow.ci wrong.expect
wrong.expect: line 6 differs
  expected: fuel-consumed 2
  actual:   fuel-consumed 1
$ echo $?
1
```

2. `tools/cint_check.py` runs cases through a compiler and compares them with their frozen expectations and with the reference. `--only` selects the cases whose name contains its argument, and `--quick` leaves out the table programs. `--compiler b1 --cint "$BOOT/cint"` tests the compiler inside the `cint` command that the bootstrap of 10.3 wrote into the directory `$BOOT`; without `--cint`, the tool first runs that bootstrap itself. `--leg`, `--opt` and `--helpers` choose the configuration and `--no-receipt` writes no receipt (11.3 writes them):

```console
$ python3 tools/cint_check.py --leg gcc --opt 0 --helpers portable --quick --compiler b1 --cint "$BOOT/cint" --only arith/add_i64_overflow --no-receipt
inventory: 1 programs, 1 records, 0 cases classified without compiling; cint_ref against the frozen .expect files: 0 differ (23 ms)
...
runtime symbol audit: 131 defined external symbols, 0 outside cint_
gcc-0-portable-quick: 1 programs compiled, 1 records compared, 0 disagree, 0 sanitizer reports, 0 nondeterministic, 0 symbol audit findings, 0 type signatures compared, 0 type signature disagreements, 8 s; suite 5ccd378e30da7e1917bb03176f68a388d770da998075a10160043635f6e89d79
not compared: held 0, not_applicable 0, outside_subset 0, unsupported 0; disagreements before compiling: 0
...
```

The first line reports that the reference agrees with the frozen expectation. The runtime's own symbols are audited (7.10). The case was compiled twice (9.7) and compared, and no case fell in another category of 8.2. The digest after `suite` is the SHA-256 digest of the suite's files, which identifies the version of the suite.

<a id="bootstrap"></a>

## 10 Bootstrap

The compiler is written in cint, so building it needs a cint compiler. cint 0.2.2 breaks this circle with a C17 program written by hand, which compiles the compiler's source once. Every later compiler is built from that one. This clause describes that program, how the compiler is built from it, the check that the compiler reproduces itself, and what that check does not show.

<a id="names"></a>

### 10.1 Names

1. These names are used in this clause only:
   - *C0*: the C toolchain of a configuration.
   - *the seed*: the bootstrap compiler `cint-seed`, built by C0 from `seed/`.
   - *the seed's C*: the C that the seed emits for the compiler's source.
   - *the first compiler*: the compiler that C0 builds from the seed's C, the runtime and the bridge. The `cint` command contains the same compiler objects as the first compiler.
   - *the first emitted C*: the C that the first compiler emits for the compiler's source.
   - *the second compiler*: the compiler that C0 builds from the first emitted C.
   - *the second emitted C*: the C that the second compiler emits for the compiler's source.

<a id="the-seed"></a>

### 10.2 The seed

**Semantics**

1. <a id="SEED-01"></a>The seed compiles the compiler's source into the seed's C, and, for its own conformance, programs of the boot subset of paragraph 7. It is not a compiler for users and is not installed [SEED-01].
2. <a id="SEED-02"></a>The seed is written by hand in C17. It is not generated from cint, and no generated C file is an input of the bootstrap [SEED-02].
3. <a id="SEED-03"></a>The seed depends only on the C17 standard library, `cint_rt.h`, the `cint_rt` library and the bridge. Its platform calls are confined to the bridge [SEED-03].
4. <a id="SEED-04"></a>The seed shares no code with `cintc` other than the runtime and the bridge. It translates its parse tree directly into C and builds no SIR [SEED-04].
5. <a id="SEED-05"></a>The seed performs every run-time check of the boot subset and produces the full fault record, field for field identical to the one `cintc` produces, except the revision, which it reports as absent because it builds no SIR [SEED-05].
6. <a id="SEED-13"></a>The seed performs no optimization. It does not remove, merge or reorder run-time checks; it folds no expression that is not a constant expression; it does not inline functions, eliminate common subexpressions, hoist loop invariants or unroll loops; it does not choose a representation for a value from the value; and it relies on no extension of the C compiler other than those that `cint_rt.h` selects. The C compiler that compiles the seed's C may optimize [SEED-13].
7. <a id="BOOT-01"></a><a id="BOOT-02"></a>The *boot subset*, `cint-boot-1`, is the part of the language that the seed accepts: the integer types `I8` to `I64` and `U8` to `U64`, `Bool`, the checked, wrapping and bitwise operators, `as` and `as%`, structs, local arrays of constant extent, rank-1 views as parameters, constants, functions, `if`, `while`, the C form and the range form of `for`, `switch`, and `import` and `export`. It excludes recursion, fixed point, the wide types, kernels and reductions, saturating operators, `defer`, `try` and error unions, print statements, `test` blocks, compile-time execution beyond constant expressions and bytes outside ASCII in a source file. A string literal there is a read-only view of its bytes, of type `in U8[_]`, and the type `Str` is not part of it. The seed refuses a program outside the subset with a diagnostic at the first construct outside it, and `cintc` accepts the same program as `cint-core-1` [BOOT-01, BOOT-02].
8. <a id="SEED-15"></a>The seed emits C nested at most 200 levels deep, counting each brace level and each open `for (;;)`. A construct beyond that is C9004, so the seed compiles at most 199 nested blocks, 66 nested `while` loops or 49 nested `for` loops. `else if` arms and `switch` arms do not count. The limit is the seed's, not the subset's: such a program is inside the subset, and a conformance run reports it as unsupported by the seed [SEED-15].
9. <a id="SEED-16"></a>The seed holds the magnitude of an integer literal in 64 bits. Where only a literal's size matters, it decides as the reference does. Where the value would have to be reported, it refuses with C9102: a decimal literal of more than 1,233 significant digits, and a checked conversion `as` of a literal of 2^64 or more. A conformance run reports such a case as unsupported by the seed [SEED-16].

**Notes**

10. The root of trust that the seed adds is a set of files a reader can read. The C toolchain that compiles it stays trusted, and 10.5 says what checks it.
11. Two translators of different structure that agree, such as the seed and `cintc`, are stronger evidence than one translator checked against itself.

<a id="building-the-compiler"></a>

### 10.3 Building the compiler

**Semantics**

1. <a id="BOOT-03"></a>The compiler's own source keeps these design rules: no call cycle; no print statement or other input or output; no module-level variable or statement, since state outside the tables that the bridge allocates would outlive a module; no call of a program function in a constant, a static assertion or an extent; and ASCII bytes only. In cint 0.2.2 the compiler's source is inside the boot subset, so the seed compiles it directly [BOOT-03].
2. <a id="CINTC-07"></a>The compiler's sources, the seed, the runtime, the bridge and the harness use LF line endings, and the bootstrap refuses a CR byte in them [CINTC-07].
3. `python3 tools/cint_bootstrap.py --leg gcc --out "$BOOT"` builds the compiler with GCC into the directory `$BOOT`, which must lie outside the source tree. It builds, in order: the runtime objects; the seed, from `seed/*.c`, `rt/cint_rt.c` and `rt/cint_bridge.c`; the seed's C, by running the seed on `compiler/main.ci` in `compiler/`, one C file for the whole compiler; the first compiler, from the seed's C, `compiler/tests/golden_host.c`, the runtime and the bridge; and the `cint` command, from `cli/` and the same compiler objects. It then writes `cint`, two toolchain files and the receipt `bootstrap.json` into `$BOOT`, and copies the repository's `lib/` to `$BOOT/lib`, the standard library root that `cint` reads by default (SPEC-06P.2 5.3). `cint.toolchain` is the configuration with which `cint run` and `cint build` compile programs, `-O2` with the builtin helpers, and `cint-bootstrap.toolchain` the one with which the first compiler and `cint` were built, `-O0` with the portable helpers.
4. <a id="RCPT-05"></a>Digests are computed twice: by the `digest` export of `compiler/sha256.ci` in the first compiler, and by an external SHA-256 implementation. The bootstrap tool computes in the first compiler the digests of the source manifests, of the seed's C, of `cint` and of the runtime sources, compares each with Python's `hashlib` and fails on any difference. The receipt is written by the tool, not by the compiler, which performs no input or output and reads no clock [RCPT-05].

**Examples**

5. The receipt of a bootstrap:

```console
$ jq -r '.schema, .identity.profile, .identity.runtime_contract_version, .outcome.source_digest_verification' "$BOOT/bootstrap.json"
cint-bootstrap-receipt-1
cint-core-1
cint-rt-3
pass
$ jq -r '.identity.compiler_sources[].path' "$BOOT/bootstrap.json"
compiler/back_c.ci
compiler/back_ptx.ci
compiler/check.ci
compiler/decl.ci
compiler/emit.ci
compiler/fold.ci
compiler/limits.ci
compiler/lowering.ci
compiler/main.ci
compiler/parse.ci
compiler/resolve.ci
compiler/scan.ci
compiler/sha256.ci
compiler/verify.ci
$ jq -cS '{identity, outcome}' "$BOOT/bootstrap.json" | tr -d '\n' | sha256sum
2b4859c123ecdced8c3bb8ec5e6eee55c1de5fb55b5e53eb014dee1daf9d0cfc  -
$ jq -r .identity_sha256 "$BOOT/bootstrap.json"
2b4859c123ecdced8c3bb8ec5e6eee55c1de5fb55b5e53eb014dee1daf9d0cfc
```

The identity names the 14 source files of the compiler with their digests, the runtime sources and the seed's source identity. The last two commands recompute the receipt identity (11.2) with external tools and show the value the receipt records.

<a id="the-fixpoint"></a>

### 10.4 The fixpoint

**Semantics**

1. <a id="FIX-01"></a>The first compiler compiles the compiler's source into the first emitted C, one C file for each module and a `MANIFEST`; C0 builds the second compiler from it; and the second compiler compiles the same source into the second emitted C. The first and the second emitted C are byte-identical, file by file, the `MANIFEST` included. The seed's C is not compared with them, because the seed is a separate translator that does not optimize, and its C differs from that of `cintc` [FIX-01].
2. <a id="FIX-02"></a>The fixpoint is checked by comparing bytes and by comparing the SHA-256 digest of each file, and both results are recorded in the receipt. The report of a difference names the first differing file, its byte offset and its line [FIX-02].
3. <a id="FIX-03"></a>The first and the second compiler emit byte-identical C for every program of the corpus: every `.ci` program under `conformance/`, `examples/` and `compiler/tests/`. This extends the fixpoint from one program, the compiler, to all of them [FIX-03].
4. <a id="FIX-05"></a>The digests of the executables of the seed and of the first and second compilers are recorded as observations, outside every identity. They differ between toolchains and may differ between two builds with one toolchain; for example, an MSVC link without `/Brepro` writes a time stamp into the executable. No claim is made that executables are byte-identical [FIX-05].
5. `python3 tools/cint_fixpoint.py --leg gcc --receipt FILE` builds the seed, the two compilers and the two emitted C in one configuration and checks paragraphs 1 to 3; `--opt` and `--helpers` choose the configuration, `--sanitize` adds the sanitizers of 7.6 and `--no-corpus` checks paragraphs 1 and 2 only. The receipt's identity holds the input digests and the digest of the first emitted C, and no path, time or executable digest.

<a id="what-the-fixpoint-does-not-show"></a>

### 10.5 What the fixpoint does not show

**Semantics**

1. <a id="FIX-04"></a>*Cross-toolchain bootstrap agreement*: the first emitted C is byte-identical whichever toolchain of the run matrix (8.4) is C0, at every optimization level. `python3 tools/cint_fixpoint.py --compare RECEIPT ...` checks that a set of fixpoint receipts has one identity. Different C compilers produce different executables of the seed and of the first compiler, so a miscompilation that changed the compiler's emitted C would have to occur identically in every C compiler of the matrix to go undetected. The check shows that the compiler's emitted C does not depend on which toolchain of the matrix built the compiler. It does not show that any distributed executable corresponds to the compiler's source (10.4 paragraph 4); that the shared inputs, the compiler's source, the seed, the runtime and the bridge, are free of defects, since every toolchain compiles the same ones; or that the toolchains share no code or design [FIX-04].

**Notes**

2. The idea of rebuilding a compiler with a diverse second compiler and comparing the result comes from Wheeler's diverse double-compiling. That technique makes a different claim: under its stated assumptions, the equality of the final executable with the distributed one shows that source and executable correspond. The check of paragraph 1 designates no trusted compiler and compares emitted C, so it makes no claim about executables and is not diverse double-compiling.

<a id="reproducing-the-bootstrap"></a>

### 10.6 Reproducing the bootstrap

**Examples**

1. A reader with a C17 toolchain, Python 3 and the source tree reproduces the bootstrap and the fixpoint with two commands, each on one configuration:

```text
python3 tools/cint_bootstrap.py --leg gcc --out "$BOOT"
python3 tools/cint_fixpoint.py --leg gcc --receipt fixpoint-gcc-0-portable.json
```

`--leg` is `gcc`, `clang` or `apple-clang` on Linux and macOS, and `msvc` on Windows. The fixpoint receipt's identity is then compared with the published one (11.3).

<a id="receipts-and-reproduction"></a>

## 11 Receipts and reproduction

A receipt records what a run read, what it concluded and what it observed of the machine. Its identity covers only the first two, so two machines that ran the same inputs to the same outcome produce the same identity. A published figure is rerun by producing a receipt and comparing its identity.

<a id="identities"></a>

### 11.1 Identities

**Semantics**

1. <a id="RCPT-01"></a>Each kind of output has its own identity [RCPT-01]:

| Identity | Inputs | What it determines |
| --- | --- | --- |
| Build identity of emitted text | the sources and their module-relative paths, the compiler source identity, the language profile, the executor and its options | emitted C and PTX, byte for byte; C0 is not an input (7.7) |
| Executable observation | the above and C0 | executables and objects, recorded and not claimed identical (10.4) |
| Revision identity | 6.5 | the meaning of a program, shared by every build of it against one runtime contract version |
| Execution identity | the revision and the semantic inputs | the output bytes, the fault record and the fuel |

   An x86-64 executable and a GPU kernel do not share a digest; built against one runtime contract version, they share the revision identity and the canonical outcomes.

<a id="receipt-content"></a>

### 11.2 Receipt content

**Semantics**

1. <a id="RCPT-02"></a>A bootstrap, a fixpoint run and a conformance run each write a receipt. A receipt file is the receipt object followed by one LF, written as the JSON Canonicalization Scheme (RFC 8785) writes it, with keys sorted and no white space outside strings, except in the escaping of strings. That departure is this: a receipt holds ASCII characters only, so a character outside ASCII is written `\u` with four lowercase hexadecimal digits, and a character above U+FFFF as its UTF-16 surrogate pair, where RFC 8785 writes the character itself. The tools of this clause write a receipt with Python's `json.dumps` and the arguments `sort_keys=True`, `separators=(",", ":")` and `ensure_ascii=True`. They refuse a string outside ASCII, and they write U+007F as `\u007f`, where RFC 8785 and the tools of SPEC-06P.2 12.1 write it as itself. A receipt holds integers only, each within the I-JSON range of plus or minus 2^53 - 1; larger values and all digests are strings, and digests are 64 lowercase hexadecimal characters. Arrays of files are in byte order of path. The key `schema` names the format of the receipt and its version, such as `cint-bootstrap-receipt-1` or `cint-conformance-receipt-2`. A new version is written to a new file, and a receipt of an earlier version is not edited or regenerated; it stays valid for the sources it names. `identity.encodings` records each encoding version that the receipt relies on, such as the `.expect` formats and the fault record version [RCPT-02].
2. <a id="RCPT-03"></a>A receipt has three parts: `identity`, the inputs and the digests of emitted text; `outcome`, the result of each check; and `observations`, the timings, the host, the C toolchain, the digests of executables and the counts of executions. The receipt identity is the SHA-256 digest of the bytes of the object `{"identity": ..., "outcome": ...}`, written as paragraph 1 states. No field of `identity` or `outcome` depends on the toolchain or the host. The count of cases in the outcome counts distinct cases, not executions [RCPT-03].
3. <a id="RCPT-04"></a>The compiler source identity is the SHA-256 digest of the *source manifest* of the compiler's source files. A source manifest is the bytes `CISRC001`, the number of files as a 32-bit little-endian integer, and then, for each file in byte order of its path relative to the root of the tree, the path's length as a 16-bit little-endian integer, the path in UTF-8, the file's length as a 64-bit little-endian integer, and the 32-byte SHA-256 digest of its contents. In a fixpoint receipt, the digest of the first emitted C (10.1) is the digest of the source manifest of its files, with paths relative to the directory that holds them. The digest of the C that the bootstrap compiler emits is an observation, because that C depends on the version of the bootstrap compiler [RCPT-04].

<a id="rerunning-a-published-figure"></a>

### 11.3 Rerunning a published figure

**Examples**

1. A published figure of agreement is a set of receipts. To rerun one, a reader builds `cint` with the bootstrap of 10.3, runs `tools/cint_check.py` with the same arguments, writes the receipt with `--receipt` and compares its identity with the published one. Two configurations of the same tree and the same suite have the same identity, because nothing in it depends on the configuration:

```console
$ python3 tools/cint_check.py --leg gcc --opt 0 --helpers portable --quick --compiler b1 --cint "$BOOT/cint" --only arith/add_i64_overflow --receipt rc/receipt-gcc-0-portable.json
...
receipt written
$ python3 tools/cint_check.py --leg gcc --opt 2 --helpers builtin --quick --compiler b1 --cint "$BOOT/cint" --only arith/add_i64_overflow --receipt rc/receipt-gcc-2-builtin.json
...
gcc-2-builtin-quick: 1 programs compiled, 1 records compared, 0 disagree, 0 sanitizer reports, 0 nondeterministic, 0 symbol audit findings, 0 type signatures compared, 0 type signature disagreements, 14 s; suite 5ccd378e30da7e1917bb03176f68a388d770da998075a10160043635f6e89d79
...
receipt written
$ python3 tools/cint_receipts.py identity rc
receipt-gcc-0-portable.json 0dd4dfea0066583c pass
receipt-gcc-2-builtin.json 0dd4dfea0066583c pass
1 receipt identity in 2 receipts
$ python3 tools/cint_receipts.py compare --outcome rc/receipt-gcc-0-portable.json rc/receipt-gcc-2-builtin.json
compare: same outcome
$ jq -c '.identity | {schema, profile, runtime_contract_version, encodings}' rc/receipt-gcc-0-portable.json
{"schema":"cint-conformance-receipt-2","profile":"cint-core-1","runtime_contract_version":"cint-rt-3","encodings":{"expect_formats":[1,2,3,4,5],"fault":"cint-core-1/fault/v2","type_signature":"cint-core-1/type-signature/v1"}}
$ jq -cS '{identity, outcome}' rc/receipt-gcc-0-portable.json | tr -d '\n' | sha256sum
0dd4dfea0066583c8e5dcc9ddc73017defc38ee2a4443760b3231a1e2e11d7af  -
$ jq -r .observations.receipt_identity_sha256 rc/receipt-gcc-0-portable.json
0dd4dfea0066583c8e5dcc9ddc73017defc38ee2a4443760b3231a1e2e11d7af
```

`cint_receipts.py identity` prints the first 16 hexadecimal digits of each receipt identity and whether the run passed. The last two commands compute the receipt identity with `jq` and `sha256sum`, which share no code with the tools of the tree; `jq -cS` writes the object with sorted keys and no spaces, which for these receipts are the bytes of 11.2, paragraph 1. A reader whose receipt identity equals the published one ran the same inputs to the same outcome.

2. <a id="DISC-07"></a>A report of a reproduction states the machine, the operating system, the toolchain and its version, the commands run, the resulting receipt identities and any deviation from the published procedure [DISC-07].

<a id="claims"></a>

### 11.4 Claims

**Semantics**

1. <a id="DISC-02"></a>A published claim about cint is backed by material that a reader can obtain, normally a receipt. No claim of compliance with MISRA C, the SEI CERT C Coding Standard or another standard is made without a documented assessment against named editions and rules. Citations are to published material [DISC-02].
2. <a id="DISC-03"></a>The statement that emitted C has no undefined behavior is made only in this form [DISC-03]:

| Part | Content |
| --- | --- |
| Claim | Emitted C, `cint_rt.h` and the `cint_rt` library do not evaluate an operation whose C17 behavior is undefined, for programs that `cintc` accepts. |
| Design rules | 7.3 and 7.4. |
| Evidence | The conformance programs compiled and run under UndefinedBehaviorSanitizer and AddressSanitizer on the toolchains and versions that the receipts record, with no report. These sanitizers do not detect violations of the aliasing rules, unsequenced modifications or every lifetime error, so the evidence covers the programs run, not every accepted program. |
| Assumptions | A correct bootstrap compiler (10.2), `cintc`, runtime and bridge; the properties of 7.2 paragraph 2; a correct C toolchain for the constructs used (none of the toolchains of the matrix claims full C17 conformance); no foreign code; buffers from the host that meet the contract of SPEC-03P.2. |
| Exclusions | Exhaustion of the C stack; foreign functions; buffers from the host that break the contract. |

3. <a id="DISC-04"></a>A performance number is published only with its workload, machine, toolchain, flags and method of measurement [DISC-04].

<a id="refusals-of-the-cint-tools-in-0-2-0"></a>

## 12 Refusals of the cint tools in 0.2.2

This clause is informative. It lists the constructs that the reference runs and that a tool of cint 0.2.2 refuses, by construct, with the tool and the diagnostic, and the limits of the tools that are below the minimums of SPEC-04P.3. A refused program is not run with another meaning (8.2), and on an implementation that runs it, its outcome is the one SPEC-04P.3 specifies. The interpreter runs the SIR text that `cint` writes, so a construct that `cint` refuses does not reach it.

1. The `cint` command (`cint run`, `cint build`, `cint test` and `cint emit-c`) refuses these constructs with the diagnostic C9102, or the one that the row names, at the position shown:

| Construct | Example | Position |
| --- | --- | --- |
| `as?` from a wide type | `I64 r = v as? I64 catch -1;` with `I128 v` | the operand |
| A result form on a wide type | `I128 r = add_result(a, b) catch 0;` | the call |
| `switch` on a wide value | `switch (v)` with `I256 v` | the scrutinee |
| `rotl` and `rotr` by a count of a wide type | `rotl(a, n)` with `U64 a`, `I128 n` | the call |
| `muldiv` of narrow operands into a wide result type | `muldiv(I128, a, b, c, floor)` with `I64` operands | the call |
| An exported function with a parameter or a result of a wide integer type or a fixed-point type | `export I64 low(I128 x)` | the parameter's name, or the result type |
| An extern with a parameter or a result of a wide integer type or a fixed-point type, a view parameter of fixed-point elements, or an `inout` view parameter | `extern pure I64 f(I128 x);`, `extern observe void f(inout I64[] xs);` | the parameter's name, or the result type |
| An array declared at the top level of a script | `I64[3] a = [1, 2, 3];` | the array literal, or the declaration when it has none |
| A struct type declared in a script | `struct P { I64 x; I64 y; }` before top-level statements | the struct's name |
| An array literal of structs | `P[2] ps = [P(1, 2), P(3, 4)];` | the `[` of the literal |
| A view variable over an array of structs | `in P[_] v = ps;` | the variable's name |
| Named arguments in a call of a user function | `diff(b = 1, a = 10)` | the first name |
| A step `by` in a range `for` loop | `for i in 0..10 by 3 { ... }` | the step |
| A selected import | `import geo.vec.{V2, dot2};` | the `import` |
| `catch` on a call of a function of an imported module | `parse.digit(c) catch -1` with `import lib.parse;` | the function's name |
| A `catch` expression as the operand of another operator | `(f(1) catch 0) + 1` | the `catch` |
| The wrapping negation `-%` | `I8 n = -%m;` | the operator |
| `len` of a string literal | `len("abc")` | the string literal |
| The format conversions `!bits` and `!lanes(T)` in a print statement | `"{x!bits}\n";`, `"{w!lanes(U8)}\n";` | the print statement |
| The format kind `c` | `"{c:c}\n";` | the value of the hole |
| An array, or a slice of one, as the value of a hole | `"{a}\n";`, `"{a[1..]}\n";` with `I32[3] a` | the value of the hole, or the `[` of the slice |
| An empty struct type | `struct Empty { }` | the struct's name |
| An array literal as an argument of a struct constructor | `Vec3([1, 2, 3])` | the `[` of the literal |
| An array literal as the right operand of an assignment, to an array or a slice | `a = [3, 4];`, `a[0..2] = [3, 4];` | the `[` of the literal |
| A module-level array whose extent is the name of a constant or a constant expression | `I64[N] table;`, `I64[N * 2] table;` | the name, or the operator of the expression |
| A module-level array of more than 65,536 elements | `I64[70000] big;` | the extent |
| A module-level array of structs | `P[4] ps;` | the element type |
| A module-level array of another module | `tab.table[3]` with `import tab;` | the array's name |
| An element of a module-level array of rank 2 to 4 in the value of a hole, in a function that uses the array nowhere else | `"{g[1, 1]}\n";` with `I64[2, 2] g` | the `[` of the element |
| An array of handles | `Handle(I64)[3] hs;` | the type |
| An exported arena | `export Arena(I64, 4) store;` | the type |
| An array element as the scalar `out` argument of a kernel | `total(a, r[0]);` | the `[` of the element |
| A local `Arena(T)` or `Pool(T)` initialized from a module-level name | `Arena(I64) s = scratch;` | the local's name |
| Arena storage, or a view variable over it, passed to a view parameter | `total(scratch[h])` | the call |
| A slice of arena storage, or of a view variable over it | `scratch[h][2..4]`, `v[2..5]` | the `[` of the slice |
| A dispatch of a kernel of an imported module, called after the module's name or alias | `dbl.twice(a, b);` with `import kmod.dbl;` | the kernel's name, with the diagnostic C3005 |

2. A program whose entry is a function `main`, rather than top-level statements, may declare struct types: the row of a struct declared in a script applies to scripts only. The `cint` command also reports C9102 for `==` between two arrays, which the reference rejects with the diagnostic C2013 at the same position, so both reject such a program. It reports C9102, at the print statement, for the conversion `!ratio` of an integer, as in `"{x!ratio}\n";` with `I64 x`. SPEC-04P.3 7.5.4 makes that conversion C1037, and the reference reports C1037 at the value of the hole. The C3005 of a kernel called after a module's name is not a refusal in the sense of SPEC-04P.3 4, paragraph 4, whose codes begin with `C9`: the `cint` command rejects that conforming program as it rejects an undefined name.
3. In one case the direction is reversed, and the reference refuses a run that the `cint` command carries out. For `clamp(x, lo, hi)` with `lo` greater than `hi` at run time, `cint run` faults `E_DOMAIN` with the operation `clamp.checked.<t>`, such as `clamp.checked.i64`, and the operands `x`, `lo` and `hi`, with no exact result and no limit, as SPEC-04P.3 7.2.1 states. The reference reports the outcome `refused` for that run.
4. A `gpu-cuda` build refuses a kernel whose body needs a capability that the `gpu-cuda` table lists as unsupported, with `E_UNSUPPORTED`, naming the kernel, the capability, the SIR node and the position (5.5). The `cpu-c17` build of the same program runs it.

| Capability | Construct in a kernel body | Example |
| --- | --- | --- |
| `sat` | the saturating operators `+\|`, `-\|` and `*\|`, and the saturating conversion `as\|` | `b[i] = a[i] +\| a[i];` |
| `div` | `/` and `%` | `b[i] = a[i] / 2;` |
| `shift` | `<<` and `>>`, wrapping or checked | `b[i] = a[i] << 1;` |
| `bits` | `&`, `\|`, `^`, `~` and `!` | `b[i] = a[i] & 3;` |
| `neg` | unary `-` | `b[i] = -a[i];` |
| `as.wrap` | the wrapping conversion `as%` | `(a[i] as% I32) as I64` |
| `builtin` | every built-in function except `div_round` | `b[i] = min(a[i], 2);` |
| `switch` | `switch` | `switch (a[i]) { ... }` |
| `store.narrow` | a store into a view of 8-bit, 16-bit or `Bool` elements | `out U8[n] b` |
| `array` | a local array | `I64[2] t;` |
| `wide` | any value of `I128` to `I1024` | `in I128[n] a` |
| `fixed` | any value of a fixed-point type | `in Q16.16[n] a` |

5. Three limits of the tools of cint 0.2.2 are below the minimums that SPEC-04P.3 5.3 states. A program beyond any of them is stopped, and the reference runs it:

| Quantity | Minimum in SPEC-04P.3 5.3 | Limit of the tools | Beyond the limit |
| --- | --- | --- | --- |
| Size of one source file | 16,777,216 bytes | 4,194,304 bytes, in `cint run`, `cint build`, `cint test` and `cint emit-c` | For the file it is given, the command stops with a tool error and exit status 5; an imported module of that size is the diagnostic C3009. The reference runs a file of 5,242,880 bytes. |
| Call-depth limit accepted as a semantic input | 4,096 active calls | 1,024 active calls, the constant `CINT_RT_MAX_DEPTH` of the runtime (7.10) | An entry whose depth limit is larger faults `E_UNSUPPORTED` at `call.enter` before it runs, with the limit 1,024. The reference runs a recursion 4,000 calls deep under a depth limit of 4,096. |
| Size of an imported module, against the size of the root module | 16,777,216 bytes, as for every source file | A bound that grows with the size and the content of the root module, in `cint run`, `cint build`, `cint test` and `cint emit-c`: 745,150 bytes for the root module of 34 bytes below | The command stops with the diagnostic C9001 at line 0 of the root module, position `0:0`, though the imported module is under 4,194,304 bytes; SPEC-04P.3 5.3 places a C9001 that concerns a whole file at line 1. Under the root module `import big;`, `"{big.f()}\n";` and a line of six spaces, 34 bytes, an imported module `big` of 745,151 bytes that defines `f` and is otherwise comment lines is C9001. Under a root module of 341 bytes, an imported module of 4,000,000 bytes runs. The reference runs the program. |

6. Two tools other than the `cint` command refuse programs that the reference runs:
   - The interpreter's `run --record` and `run --case` (SPEC-06P.2 8.2) do not write a recording or a case of a program with a module-level variable of a fixed-point type: the command stops with exit status 5, and `run` without them runs the program.
   - The workbench, `cint repl` and `cint debug` (SPEC-06P.2 9.6) refuse a module that declares an extern or a `requires`, with the diagnostic C4012.

<a id="future-directions"></a>

## 13 Future directions

This clause is informative. It names work that cint 0.2.2 does not include.

1. *Reproducible executables.* The executables of a bootstrap differ between toolchains and may differ between two builds with one toolchain, so receipts record their digests as observations only (10.4). Byte-identical executables, built with options such as `-ffile-prefix-map` for GCC and Clang and `/Brepro` for MSVC, let a receipt identify the executable as well as the C it was built from.
2. *Diverse double-compiling.* In Wheeler's technique, the compiler's source is compiled by a second compiler that is trusted, and the resulting executable is compared with the distributed one; equality shows that the source and the executable correspond. It needs reproducible executables and a designated trusted compiler, and it extends the agreement of 10.5 from emitted C to executables.

<a id="measured-agreement"></a>

## Annex A (informative) Measured agreement

1. The figures of this annex were measured on the sources of cint 0.2.2 and hold for cint 0.2.3. 0.2.3 differs from those sources in one line of the C that `cintc` writes at the start of a function (clause 7), and paragraph 5 gives the checks run on it. The suite digest of every run is `a75d4ad48ae0b34fa7366ef2e9e68788e9195f44712c34eedfb7350acaa36271`, and 11.3 shows how a reader reruns a figure.
2. `cpu-c17`: `cintc`, built fresh from the seed, compiles 732 programs and compares 1,867,975 of their 1,868,166 records with no disagreement, and refuses the other 191 (clause 12). The counts are the same in 8 configurations: GCC 16.2.1 and Clang 23.1.3 on Linux x86-64, MSVC 19.44 on Windows x86-64 and Apple Clang 21 on macOS AArch64, each at `-O0` (`/Od`) in portable mode and at `-O2` (`/O2`) in builtin mode.
3. `cpu-sir-interp`: with GCC 16.2.1 on Linux x86-64, the interpreter compares the 1,868,166 records with no disagreement.
4. The sources of 0.2.2 have a sanitizer finding: under Clang's UndefinedBehaviorSanitizer at `-O2`, the C of every function that uses a field or an element of a struct as a place read through a null pointer when the function started. GCC and Clang without the sanitizer did not report it, and it changed no compared record. cint 0.2.3 fixes it.
5. cint 0.2.3 passes the tool checks on Linux x86-64, with and without the sanitizers, and on Windows x86-64; the 552 checks of the golden test suite of `cintc` under Clang's sanitizers at `-O2` in builtin mode; and a build of the published tree from its bootstrap, with the runtime tests and the examples.
6. This annex gives no figure for `gpu-cuda`, for the fixpoint or for decoder fuzzing.

<a id="bibliography"></a>

## Bibliography

- David A. Wheeler. *Fully Countering Trusting Trust through Diverse Double-Compiling*. PhD dissertation, George Mason University, 2009. arXiv:1004.5534. Diverse double-compiling, in 10.5 and 13.
- Free Software Foundation. "Integer Overflow Builtins." *Using the GNU Compiler Collection (GCC)*. https://gcc.gnu.org/onlinedocs/gcc/Integer-Overflow-Builtins.html. The builtins of the helper mode of 7.3, paragraph 2.
- LLVM Project, the Clang Team. "UndefinedBehaviorSanitizer." *Clang documentation*. https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html. The sanitized conformance runs of 7.6 and 11.4.
- LLVM Project, the Clang Team. "AddressSanitizer." *Clang documentation*. https://clang.llvm.org/docs/AddressSanitizer.html. The sanitized conformance runs of 7.6 and 11.4.

## Annex B (informative) Clause identifiers stated in other editions

A tool message, or an earlier edition, may cite a clause identifier of this document's series whose rule a 0.2.2 edition states elsewhere. Each such identifier is listed here with the edition and section that state it, or, where no 0.2.2 edition states its rule, with the section that describes the topic.

| Identifier | Stated in |
|---|---|
| <a id="BACK-07"></a>BACK-07 | [SPEC-02P.2 9.1](SPEC-02P.2-KERNELS.md#BACK-07) |
| <a id="BACK-08"></a>BACK-08 | [SPEC-02P.2 9.2](SPEC-02P.2-KERNELS.md#BACK-08) |
| <a id="BACK-13"></a>BACK-13 | [SPEC-02P.2 10.6](SPEC-02P.2-KERNELS.md#BACK-13) |
| <a id="CINTC-09"></a>CINTC-09 | [SPEC-06P.2 9.9](SPEC-06P.2-TOOLS.md#CINTC-09) |
