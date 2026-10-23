# SPEC-04P.3: The cint language

SPEC-04P.3, edition 3 of the cint language specification, cint v0.2.3, 2026-10-23.

<a id="foreword"></a>

## Foreword

This document specifies the cint programming language. cint 0.2.2 implements this edition, except for the constructs that its tools refuse (4, paragraph 4).

Each rule ends with one or more clause identifiers in brackets, such as [LS-17] or [IM-30]. An identifier names one rule across editions: a rule keeps its identifier when its text moves to another section or another document, and an identifier is never reused. Section numbers belong to this edition only.

This edition replaces SPEC-04P.2. It adds extern declarations and the standard services through which a program reads and writes files and the terminal (6.9.8, 6.9.9), the program arguments of `main` (5.1.2), the saturating conversion `as|` (6.6), the result forms on fixed-point operands (6.7.12) and fixed-point types of 128-bit storage (6.3.2). No rule of SPEC-04P.2 moved. Annex F lists every clause identifier and where it is.

<a id="introduction"></a>

## Introduction

cint is a C-family programming language for programs whose results must be exact and the same everywhere. Every value has a declared type: an integer type, a fixed-point type whose values are scaled integers, `Bool`, an error set, or an aggregate of these. Every plain arithmetic operator is checked: it gives the exact mathematical result, and when that result does not fit its type, the program stops with a fault that names the operation, its operands and the exact result. Wrapping and saturating arithmetic exist, but only through operators, conversions and named operations spelled for them.

A cint program has one meaning. Given the same semantic inputs (3.24), every conforming implementation (4) that runs the program writes the same output bytes and consumes the same count of fuel (3.21). When the program faults, every such implementation produces a fault record (3.11) equal in every field except `revision` and `source-map`, which identify the program and not the outcome (8.2). This holds whether the program is compiled to C, run by the interpreter `cint-interp` (3.19), or has its kernels (3.28) dispatched (3.29) to a GPU. An implementation may refuse a program that it cannot run exactly (4, paragraphs 3 and 4).

The first program below sums the squares of 1 to 10. Top-level statements run in order, and a string on its own prints itself, with each `{...}` replaced by the value of the expression inside it:

```cint squares.ci
I64 total = 0;
for i in 1..=10 {
    total += i * i;
}
"sum of squares: {total}\n";
```

prints

```text
sum of squares: 385
```

The same loop in `I8`, whose range is -128 to 127, cannot reach the end:

```cint squares8.ci
I8 total = 0;
for i in 1..=10 {
    I8 k = i as I8;
    total += k * k;
    "after {k}: {total}\n";
}
```

```text
after 1: 1
after 2: 5
after 3: 14
after 4: 30
after 5: 55
after 6: 91
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> squares8.ci:4:11
   |
 4 |     total += k * k;
   |           ^ add.checked.i8
   |
   = operation: add.checked.i8
   = operand-count: 2
   = operand: I8 91
   = operand: I8 49
   = exact:   140
   = limit:   I8 127
   ...
```

The seventh addition would give 140, so the program stops there. The six lines it wrote before the fault stay written, and the report names the operation, both operands, the exact result and the bound it crossed. The program never continues with a wrapped value.

<a id="how-this-document-is-organized"></a>

### How this document is organized

Clauses 1 to 4 state what the document covers, the documents it relies on, its terms and what conformance means. Clause 5 describes the environment a program is translated and run in: modules and scripts, diagnostics, faults, fuel, call depth and the limits of an implementation. Clause 6 defines the language, from the concepts it rests on through types, lexical elements, expressions, declarations and statements, to arrays, views, kernels and memory. Clause 7 defines the built-in operations. Clause 8 explains how to find out why a program did what it did: the fault record, the agreement of every implementation on every outcome, golden outputs and tests, checkpoints and replay and the debug tools. Annex A gives the complete grammar. The other annexes are informative, except B.3, which states the form of operation identifiers: the diagnostic and fault codes, how cint treats each kind of behavior that C leaves undefined, a guide for C and Python programmers, the open questions and an index of clause identifiers.

Each subclause of clauses 6, 7 and 8 opens with a short summary and then has some of these parts:

- The **summary**: informative. It introduces the subclause and states no rule; where it and a rule differ, the rule governs.
- **Syntax**: the grammar of the construct, in the notation of 6.1.
- **Constraints**: rules checked when the program is translated. A program that breaks a constraint is rejected with a diagnostic and does not run.
- **Semantics**: what the construct means when the program runs.
- **Faults**: the faults the construct can raise, and what each fault record holds.
- **Examples** and **Notes**: informative. A note or example may cite the identifiers of the rules it illustrates and adds no rule of its own.

Every example in this document is a complete program or a complete part of one, and its output is shown as cint 0.2.2 prints it, except where a value is replaced by a placeholder in angle brackets, which the text explains. Where a tool of that release refuses an example (4, paragraph 4), the output shown is the reference's. A line `...` in shown output stands for lines left out.

<a id="scope"></a>

## 1 Scope

1. This document specifies the form and meaning of programs written in the cint language, profile `cint-core-1`. It specifies:
   - the representation of source text;
   - types, values and the operators on them;
   - declarations, statements, modules and tests;
   - arrays, views and kernels;
   - arenas, pools and handles;
   - the faults a program can raise and the content of the record each fault produces;
   - the fuel a program consumes and the bounds on call depth and local storage;
   - the diagnostics an implementation reports when it rejects a program.
2. This document does not specify:
   - the byte encodings of fault records, program state, checkpoints, recordings and identities, which SPEC-01P.3 specifies, except the canonical form of a handle (6.14.4) and the storage of a `Bool` (6.3.3), which this document states;
   - how an executor carries out a kernel dispatch so that it agrees with clause 6.13, which SPEC-02P.2 specifies;
   - the C and Python interfaces through which a host program calls cint code, which SPEC-03P.2 specifies;
   - the commands of the cint tools, which SPEC-06P.2 specifies; where this document states what `cint run` does, it does so to fix the semantic inputs and the output of its examples;
   - the intermediate representation, the C that the compiler emits and the conformance suite, which SPEC-09P.2 specifies;
   - the behavior of code outside the program that the program reaches through the host interface.

<a id="normative-references"></a>

## 2 Normative references

The following documents are referred to in the text in such a way that some or all of their content constitutes requirements of this document. For dated references, only the edition cited applies.

- The Unicode Consortium. *The Unicode Standard*, Version 17.0.0, 2025. Used for the terms Unicode scalar value and code point, and for the property `Default_Ignorable_Code_Point`.
- The Unicode Consortium. *Unicode Standard Annex #9: Unicode Bidirectional Algorithm*, revision 51, 2025-08-13, the revision of Unicode 17.0.0. Used for the directional formatting characters of 6.4.1.
- F. Yergeau. *UTF-8, a transformation format of ISO 10646*. RFC 3629 (STD 63), 2003-11. Used for the encoding of source text (6.4.1).
- SPEC-01P.3, *Canonical encodings and identity*, edition 3. Used for the encodings of fault records, state, checkpoints and identities, for the value of each fault code in an encoded record, and for the terms of 3.37 to 3.43 (3.31, 5.2.3, 8.1, 8.2, 8.4).
- SPEC-02P.2, *Kernels and executors*, edition 2. Used for the full procedure of the overlap tests of 6.12.5 and 6.13.2, and for what an executor shall do to give a kernel dispatch its meaning (6.13).
- SPEC-03P.2, *The host interface: C and Python*, edition 2. Used for the host program that creates contexts and runs entries, for the layout of values at the C interface, for the host imports and the grants a host program supplies, and for the term of 3.44 (5.2.1, 6.3, 6.9.8, 6.9.9, 6.15.2, 8.4).
- SPEC-06P.2, *The cint tools*, edition 2. Used for the command language of a workbench session, the exit statuses and options of each tool, the grants of `cint run`, and the allowances each tool gives (5.1.2, 5.2, 6.9.9, 6.15, 8.5).
- SPEC-09P.2, *Toolchain and conformance*, edition 2. Used for the conformance suite, the refusals of the cint tools (4, paragraph 4) and the SIR (8.2, 8.5).

Clauses 5 to 7 depend on SPEC-02P.2 for the overlap tests of 6.12.5 and 6.13.2. Their other citations of these editions point to encodings, interfaces and tool options that do not change the outcome of a program.

<a id="terms-definitions-and-symbols"></a>

## 3 Terms, definitions and symbols

For the purposes of this document, the following terms and definitions apply. Other terms are defined where they first appear, in italic type. Terms explicitly defined in this document are not to be presumed to refer implicitly to similar terms defined elsewhere.

<a id="term-z"></a>**3.1 Z**: the set of mathematical integers, of unbounded size.

<a id="term-value"></a>**3.2 value**: what an object (3.33) holds or an expression produces, together with its type. A value of an integer type is an element of Z in the type's range, and a value of a fixed-point type is a multiple of 2^-f in its range. The other values are those of `Bool`, of error sets and of handle types, and the arrays and structs built from them (6.3).

<a id="term-type"></a>**3.3 type**: the set of values an object (3.33) may hold and the operations defined on them. An integer type has a *range* from its minimum `MIN(T)` to its maximum `MAX(T)`.

<a id="term-exact-result"></a>**3.4 exact result**: the result of an arithmetic operation computed in Z, before any check against the range of the result type. For an operation that rounds, the exact result is the value after its one rounding, before the range check (6.7.15).

<a id="term-checked-operator"></a>**3.5 checked operator**: an operator that gives the exact result when it is in the range of the result type and otherwise raises a fault. Every plain operator is checked: an arithmetic operator spelled without a `%` or `|` suffix, such as `+`, `/`, `%` or `<<`.

<a id="term-wrapping-operator"></a>**3.6 wrapping operator**: an operator spelled with a `%` suffix after an arithmetic operator (`+%`, `-%`, `*%`, `<<%` and unary `-%`), whose result is the exact result reduced modulo 2 to the power of the type's width into the type's range. A wrapping operator does not fault on overflow.

<a id="term-saturating-operator"></a>**3.7 saturating operator**: an operator spelled with a `|` suffix after an arithmetic operator (`+|`, `-|`, `*|`), whose result is `sat(T, v)` (3.50) for the exact result `v`. A saturating operator does not fault on overflow.

<a id="term-result-form"></a>**3.8 result form**: a built-in operation, such as `add_result`, that returns an error union (3.13) instead of raising the fault its checked operator would raise.

<a id="term-fault"></a>**3.9 fault**: the stop of a run at an operation that cannot give a correct result, such as an overflow, a division by zero or an index outside an array (5.2.3).

<a id="term-fault-code"></a>**3.10 fault code**: the name of the kind of a fault, such as `E_OVERFLOW`.

<a id="term-fault-record"></a>**3.11 fault record**: the description of a fault that an implementation produces: its fault code, the operation, the operands, the exact result, the bound that was crossed, the source position, and the identity of the program.

<a id="term-operation-identifier"></a>**3.12 operation identifier**: the name of an operation in a fault record, such as `add.checked.i64`.

<a id="term-error-union"></a><a id="term-error-set"></a><a id="term-error"></a>**3.13 error, error set, error union**: an *error set* is a type declared with `error`, whose values are named errors (6.3.7). An *error* is a value of an error set, returned by a function to report an expected failure; unlike a fault, an error is handled by the program, with `try` or `catch`. An *error union* `E!T` is a function result that holds a `T` or a value of the error set `E` (6.3.7).

<a id="term-translation"></a>**3.14 translation**: everything an implementation does with a program before it runs: reading the source text, checking the constraints, and compiling or preparing the program.

<a id="term-diagnostic"></a>**3.15 diagnostic**: a message an implementation reports during translation when a program breaks a constraint. A diagnostic has a code, the letter `C` followed by four digits, and a source position.

<a id="term-refusal"></a>**3.16 refusal**: the report by an implementation that it will not run a valid program, or a kernel dispatch, because it cannot execute it exactly. A refused program is not rejected: it stays conforming. A refusal is reported during translation when the need is known then, and otherwise when an entry or a kernel dispatch begins, before any of its work runs. A refused program is never run with a different meaning.

<a id="term-implementation"></a>**3.17 implementation**: a set of programs that translates and runs cint programs.

<a id="term-the-reference"></a>**3.18 the reference**: the implementation `cint_ref`, written to follow this document directly, whose results are the expected results of the conformance suite.

<a id="term-executor"></a>**3.19 executor**: the part of an implementation that runs a translated program (8.2, Note 13).

<a id="term-entry"></a>**3.20 entry**: one run of a program from its entry point: the top-level statements of a script, the function `main`, a `test` block or an exported function called from a host program.

<a id="term-fuel"></a>**3.21 fuel**: a count of steps (calls, loop iterations, reductions and kernel dispatches) that an entry is charged as it runs. An entry with a fuel allowance of `B` faults at the charge that would take its consumed fuel above `B`.

<a id="term-call-depth"></a>**3.22 call depth**: the number of user-function calls active at one time in an entry.

<a id="term-frame-arena"></a>**3.23 frame arena**: the storage that holds the local arrays of active function calls, counted in elements.

<a id="term-semantic-input"></a>**3.24 semantic input**: a value that decides the outcome of an entry: the program, its input values, the effects the host supplied to it (8.2, paragraph 2), the fuel allowance, the call-depth limit and the frame-arena capacity.

<a id="term-module"></a>**3.25 module**: one source file. Scripts and importable modules are defined in 5.1.2.

<a id="term-array"></a>**3.26 array**: a fixed number of elements of one type, stored in order.

<a id="term-view"></a>**3.27 view**: a typed, shaped window onto array storage, given by its element type, its rank, an extent and a stride for each dimension, and whether it may be written (6.3.5, 6.12.4).

<a id="term-kernel"></a>**3.28 kernel**: a function that runs once for each point of an iteration space, writing its results to output views that only that point may write.

<a id="term-dispatch"></a>**3.29 dispatch**: one call of a kernel over an iteration space. Each point of the space is a *work-item*.

<a id="term-handle"></a><a id="term-pool"></a><a id="term-arena"></a>**3.30 arena, pool, handle**: an *arena* is a region of storage with a fixed capacity from which objects are taken and released together; a *pool* is a fixed set of slots that are taken and released one at a time; a *handle* is a checked reference to an object in an arena or a pool.

<a id="term-revision"></a>**3.31 revision**: the identity of a program, a digest computed as SPEC-01P.3 specifies. Source positions are not part of it (8.1), and it depends on the runtime contract version (3.43, 8.4). A fault record names the revision its source positions refer to.

<a id="term-golden"></a>**3.32 golden**: an output, or the text of a whole outcome (8.3), saved from a run whose output was checked once, kept with the program, and compared byte for byte with the output of each later run of the program on the same inputs (8.3).

<a id="term-object"></a>**3.33 object**: storage that holds a value: a variable, an element of an array, or a field of a struct (6.2.2).

<a id="term-size-parameter"></a>**3.34 size parameter**: a name, bound at each call or dispatch, for an extent of an array or view argument; inside the body it is a read-only `I64` (6.9.5, 6.13.2).

<a id="term-sequential-code"></a>**3.35 sequential code**: the code of a program outside kernel bodies (6.13).

<a id="term-storage-integer"></a><a id="term-raw-value"></a>**3.36 raw value, storage integer**: the integer `r` of the storage type of a fixed-point type that stands for the value `r / 2^f` (6.3.2).

<a id="term-boundary"></a>**3.37 boundary**: as defined in SPEC-01P.3, 3.16. A boundary is a point of a run at which the state of its context (3.44) is defined and its state hash is taken, such as the end of an entry (3.20) that ran.

<a id="term-case"></a>**3.38 case**: as defined in SPEC-01P.3, 3.15. A case is a recording (3.40) that also holds the source text of every module of its program, so that the run can be replayed on another machine with no other file.

<a id="term-checkpoint"></a>**3.39 checkpoint**: as defined in SPEC-01P.3, 3.11. A checkpoint is the canonical bytes of the state of a context (3.44), taken between entries, from which the context can be restored.

<a id="term-recording"></a>**3.40 recording**: as defined in SPEC-01P.3, 3.14. A recording is a file that holds the identity and code of a program, a checkpoint (3.39) taken when recording began, and the entries and other events that followed, in order.

<a id="term-sir-text"></a>**3.41 SIR text**: as defined in SPEC-01P.3, 3.19. SIR text is the canonical text form of the intermediate representation of one module.

<a id="term-source-map"></a>**3.42 source map**: as defined in SPEC-01P.3, 3.20. A source map lists each operation that can fault in every module of a program, with its ordinal, line, column and operation identifier (3.12).

<a id="term-runtime-contract-version"></a>**3.43 runtime contract version**: as defined in SPEC-01P.3, 3.18. The runtime contract version names the interface between compiled code and the runtime library: `cint-rt-3` in cint 0.2.2.

<a id="term-context"></a>**3.44 context**: as defined in SPEC-03P.2, 3.4. A context is one instance of a program's state, held by a host program, in which entries (3.20) run one at a time.

<a id="term-effect-class"></a>**3.45 effect class**: what a call of a function or an extern may do outside the program: `pure`, `observe` or `unrestricted` (6.9.8).

<a id="term-extern"></a>**3.46 extern**: a function that a module declares with `extern` and does not define; a host program or a standard service supplies it (6.9.8).

<a id="term-service"></a>**3.47 service**: a named, versioned set of externs that the runtime supplies, such as `fs/1` (6.9.9).

<a id="term-capability"></a>**3.48 capability**: the right to use a group of service externs, possibly limited to a root, written as capability text such as `fs.read:/data` (6.9.9).

<a id="term-grant-set"></a>**3.49 grant set**: the capabilities given to a run, fixed before the program starts; a program holds it as a value of type `Grants` (6.9.9).

<a id="symbols"></a>

### 3.50 Symbols

| Symbol | Meaning |
|---|---|
| `MIN(T)`, `MAX(T)` | The least and greatest values of the integer type `T`. |
| `w` | The width of an integer type in bits. |
| `in(T, v)` | True when `MIN(T) <= v <= MAX(T)`. |
| `wrap(T, v)` | The value of `T` congruent to `v` modulo 2^`w`. |
| `sat(T, v)` | `MIN(T)` if `v < MIN(T)`, `MAX(T)` if `v > MAX(T)` and `v` otherwise. |
| `floor(x)` | The greatest integer not greater than the rational `x`. |
| `ceil(x)` | The least integer not less than the rational `x`. |
| `log2(x)` | The base-2 logarithm of the positive rational `x`. |
| `min(a, b)`, `max(a, b)` | The lesser and the greater of `a` and `b`. |
| `clamp(v, l, h)` | For `l <= h`, `min(max(v, l), h)`. |
| `2^k` | 2 raised to the power `k`. |
| `\|a\|` | The absolute value of `a`. |

<a id="conformance"></a>

## 4 Conformance

1. In this document, "shall" states a requirement on a program or on an implementation, and "shall not" a prohibition. "May" states a permission. A rule on programs is a constraint, checked during translation, unless it says that a run that breaks it faults.
2. <a id="LS-272"></a>A *conforming program* is one that satisfies every constraint of this document. A conforming program has exactly one outcome for each set of semantic inputs: it ends normally after writing specified output bytes, or it stops with one fault, described by one fault record, after writing a specified prefix of its output [LS-272].
3. A *conforming implementation* shall:
   - reject every program that breaks a constraint, with a diagnostic as described in 5.1.3, and produce no executable code for it;
   - for every conforming program that it runs, produce the outcome this document specifies: the same output bytes, the same fuel consumed and a fault record equal in every field except `revision` and `source-map` to that of every other conforming implementation (8.2, paragraph 3);
   - <a id="IM-129"></a>refuse a conforming program, or a kernel dispatch, that it cannot execute exactly: during translation when the need is known then, and otherwise with the fault `E_UNSUPPORTED` when the entry or the dispatch begins, before any of its work runs (5.2.5) [IM-129].
4. An implementation may refuse a construct that it does not implement, with a diagnostic whose code begins with `C9`. Such a refusal is not a rejection: the program stays conforming, and on an implementation that runs it, its outcome is the one this document specifies [SPEC-09P.2 CONF-14]. The code C9102 has two uses: it diagnoses a constraint that names no code of its own, and, since it begins with `C9`, it can also report a refusal. The reference tells the two uses apart. A program that the reference rejects breaks a constraint and is not conforming, whatever code a tool gives for it. A construct that the reference runs and a tool refuses with a C9 code is conforming. SPEC-09P.2, clause 12 lists those constructs for the cint tools.
5. An implementation shall not narrow, approximate or substitute: it shall not compute, store or compare a value at fewer bits than its type, or round it differently, in any way that can change a value or a fault [IM-7, IM-129].
6. <a id="LS-273"></a><a id="LS-274"></a>Within the scope of this document there is no undefined or unspecified behavior. The implementation limits of 5.3 are the one implementation-defined part: an implementation may accept more than the minimum limits, and a limit decides only whether a program translates or runs, never its outcome. Where this document gives an operation no result, such as `reshape` outside the condition of 7.4, paragraph 9, an implementation refuses the program (3.16). Annex C lists, for each kind of behavior that the C standard leaves to the implementation or leaves undefined, what cint does instead. The scope does not cover code reached through the host interface, or changes that such code makes to memory it shares with the program. It does not cover defects in an implementation, in the C it emits or in the C compiler that compiles it. It also does not cover hardware faults, or exhaustion of host resources that an implementation cannot detect and report as a fault [LS-273, LS-274].
7. <a id="LS-275"></a>This document makes no claim that cint or any implementation of it complies with a safety or coding standard, such as MISRA C or the SEI CERT C Coding Standard. Such a claim would need a documented assessment against specific editions and rules [LS-275].
8. The language profile defined by this document is `cint-core-1`. A `.ci` source file without a profile declaration is written in it (5.1.1).
9. The conformance suite of SPEC-09P.2 holds programs with their expected outcomes, produced by the reference. Passing the suite is evidence of conformance, not a definition of it: where an implementation, the reference included, and this document differ, this document governs.

<a id="environment"></a>

## 5 Environment

This clause describes the setting in which a program is translated and run. Translation (5.1) turns source files into a program or rejects them with diagnostics. Execution (5.2) runs the program as entries into a context, writes its output, stops it at a fault, and bounds it by fuel, call depth and frame storage. Implementation limits (5.3) say how large a program every implementation accepts.

<a id="translation"></a>

### 5.1 Translation

<a id="source-files-and-modules"></a>

#### 5.1.1 Source files and modules

A cint program is written in source files. The contents of a module decide whether it is a script, which runs, or an importable module, which other modules use.

**Syntax**

```ebnf
module       = [ profile_decl ] { import_decl } { top_item } ;
profile_decl = "profile" STRING ";" ;
```

`import_decl` and `top_item` are defined in 6.15.1.

**Constraints**

1. <a id="LS-3"></a>A source file has a name that ends in `.ci`, and its contents are source text as 6.4.1 describes. A file whose name ends in `.cint` is not a source file of this language and is rejected with C1001, reported at line 1, at the first character. This edition gives no diagnostic for other names, which are not source files by the first sentence [LS-3].
2. <a id="LS-8"></a>A profile declaration, when present, shall be the first item of the file; anywhere else it is a syntax error (C1050). It shall name `cint-core-1`, the profile this document defines; a file whose declaration names any other profile is refused (3.16) [LS-8].

**Semantics**

3. Each source file is one module [LS-3]. A file without a profile declaration is written in `cint-core-1` [LS-8].

**Examples**

4. A profile declaration that names the profile changes nothing:

```cint profiled.ci
profile "cint-core-1";
"profile checked\n";
```

```text
profile checked
```

<a id="scripts-and-programs"></a>

#### 5.1.2 Scripts and programs

A program starts from one module, its root. The root is either a script, whose top-level statements run in source order, or a module that defines a function `main`. A module with only declarations is an importable module: it has no entry of its own and is used through `import`.

**Constraints**

1. <a id="LS-220"></a>A script, a module with at least one top-level statement (paragraph 5), cannot be imported (C3010) [LS-220].
2. <a id="LS-221"></a>A program that is not a script may define its entry as `void main()` or as `E!void main()` for an error set `E`. Either form may take a parameter of type `Grants`, the run's grant set (6.9.9), and a parameter `in U8[_]`, the program arguments, in that order: `main()`, `main(Grants g)`, `main(in U8[_] args)` or `main(Grants g, in U8[_] args)`. Any other parameter list is diagnosed C3044 at the first parameter outside these forms. A module that has both top-level statements and `main` is rejected with C3011, at the name `main` [LS-221].
3. <a id="LS-219"></a>A variable declared at the top level of a script is a local of the script's entry. It is visible from its declaration to the end of the script, and never inside a function, kernel or test: naming it there is C3004 [LS-219].
4. <a id="LS-312"></a>The diagnostics whose cause depends on whether a module is a script (C3004, C3010, C3011, and C6004 for a module-level initializer that is not constant, 6.9.3) carry a note. The note names the first top-level statement, in source order, that makes the module a script, or says that the module has none. The note changes neither the code nor the position of the diagnostic [LS-312].

**Semantics**

5. <a id="LS-218"></a>A *script* is a module that contains at least one top-level statement [LS-218]. A `test` block is not a statement and does not make a module a script [LS-312].
6. An *importable module* contains only imports, declarations, tests and `static_assert` [LS-220].
7. When a script runs, its top-level statements run in source order, as one entry (5.2.1) [LS-218].
8. In a script, the declarations of structs, error sets, arenas, pools, functions, kernels and constants are visible throughout the module, whatever their order. An arena or pool is created with the context, before the first statement runs [LS-219].
9. A program with `main` runs `main` as its entry. When `main` has the result type `E!void` and returns an error, the entry ends with that error as its result (5.2.1) [LS-221].
10. The *program arguments* are the words that the host gives the run, such as those after `--` in `cint run FILE.ci -- ARG...` (SPEC-06P.2 5.1). `main` receives them as one read-only view: each word's bytes followed by one NUL byte, in the order given, so a run given none makes the view empty. They cost no fuel and need no grant, and a recording of the run holds them (SPEC-03P.2 H-14) [LS-221].
11. <a id="LS-222"></a>There is one meaning for every way a program runs. A script run by `cint run`, a program with `main`, and the same statements entered in a `cint workbench` session have the same types, the same arithmetic and the same faults, whichever executor (3.19) runs them, except that a variable declared at a session prompt is a module-level variable (paragraph 13), where in a script it is a local of the entry (paragraph 3) [LS-222].
12. <a id="LS-223"></a>What is entered in a `cint workbench` session, other than a command, is `cint-core-1` source. The session's own commands, such as `:checkpoint` and `:inspect`, form a separate command language that SPEC-06P.2 defines [LS-223].
13. <a id="LS-224"></a>In a `cint workbench` session, an entry that is a single expression prints its value and its type. A variable declared at the prompt is a module-level variable of the session's own module, so a function declared later can read it [LS-224].

**Examples**

14. A script may call a function declared after the call:

```cint ordered.ci
"{twice(21)}\n";
I64 twice(I64 v) {
    return 2 * v;
}
```

```text
42
```

15. A top-level variable of a script is not visible inside a function. The note names the statement that makes the module a script:

```cint balance.ci
void show() {
    "{balance}\n";
}
show();
I64 balance = 5;
```

```text
error[C3004]: script local used in a function or test
  --> balance.ci:2:7
   |
 2 |     "{balance}\n";
   |       ^
   |
   = note: this module is a script because of the top-level statement at balance.ci:4:1
```

16. The same program with `main`: `balance` is now a local of `main` and is passed as an argument:

```cint balance_main.ci
void show(I64 balance) {
    "{balance}\n";
}
void main() {
    I64 balance = 5;
    show(balance);
}
```

```text
5
```

17. A module cannot be a script and define `main`:

```cint both.ci
void main() {
    "in main\n";
}
"top level\n";
```

```text
error[C3011]: script rule
  --> both.ci:1:6
   |
 1 | void main() {
   |      ^
   |
   = note: this module is a script because of the top-level statement at both.ci:4:1
```

18. A module whose every top-level item is a declaration is not a script, so its module-level variables need constant initializers. The note says so:

```cint initial.ci
I64 start() {
    return 4;
}
I64 counter = start();
```

```text
error[C6004]: constant expression required
  --> initial.ci:4:15
   |
 4 | I64 counter = start();
   |               ^
   |
   = note: this file is a module, not a script, because every top-level item is a declaration (LS-218)
```

19. `main` may take the program arguments, which `cint run` gives after `--`:

```cint words.ci
void main(in U8[_] args) {
    I64 n = 0;
    for i in 0..len(args) {
        if (args[i] == 0) {
            n += 1;
        }
    }
    "{n} arguments in {len(args)} bytes\n";
}
```

```console
$ cint run words.ci -- alpha b c
3 arguments in 10 bytes
$ cint run words.ci
0 arguments in 0 bytes
```

20. This example shows a `cint workbench` session. Each entry is source text; an expression prints its value and type, and a fault is reported in the layout of 5.1.3:

```console
$ cint workbench
cint workbench   profile cint-core-1   backend cpu-sir-interp   new session
ci[1]> 7 / 2
= 3 : I64
ci[2]> I64 x = 5;
x = 5 : I64
ci[3]> I64 triple() { return x * 3; }
triple : () -> I64   revision session@r2
ci[4]> triple()
= 15 : I64
ci[5]> I8 small = 100;
small = 100 : I8
ci[6]> small + small
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> entry 6:1:7
   |
 1 | small + small
   |       ^ add.checked.i8
   |
   = operation: add.checked.i8
   = operand-count: 2
   = operand: I8 100
   = operand: I8 100
   = exact:   200
   = limit:   I8 127
...
```

**Notes**

21. `cint run` needs a program: a root module with neither top-level statements nor `main` has no entry, and `cint run` stops with a usage error. `cint test` runs the tests of such a module (6.15.3).
22. SPEC-06P.2 describes the session in full, including how a redefinition in a live session makes a new revision of the session's module. A session names the revisions of its module `session@r1`, `session@r2` and so on and the `backend` field of its first line names the executor (3.19).
23. A function that needs the value of a script variable takes it as a parameter (example 16).

<a id="diagnostics"></a>

#### 5.1.3 Diagnostics

When a program breaks a constraint, the implementation rejects it with a diagnostic: a code, a position and a message. The code and the position belong to the language, so implementations that report a code for a program report the same first one (4, paragraph 4 and B.1 state where cint and the reference differ). A run-time fault (5.2.3) is reported in the same layout.

**Semantics**

1. <a id="LS-283"></a>A diagnostic has a *code*, the letter `C` followed by four digits, and a *position*, the file, line and column (6.4.2) of the offending construct. The first digit of the code gives its category [LS-283]:

| Codes | Category |
|---|---|
| C1xxx | Files, source text, tokens and syntax |
| C2xxx | Types, conversions, constants and views |
| C3xxx | Names, modules, tests and capabilities |
| C4xxx | Statements, control flow and names used as keywords |
| C5xxx | Parameter modes, kernels and externs |
| C6xxx | Evaluation during translation |
| C9xxx | Implementation limits and refusals (5.3; 4, paragraph 4) |

   The subclause that defines each code states its position; Annex B lists every code with that subclause.

2. <a id="LS-278"></a>The position of a diagnostic is the first character of the offending construct. Where a construct can be read two ways, the subclause that defines the code names the token: for example, C2003 is reported at the literal, inside any parentheses and at the `-` of a negative literal; C3001 at the declared name; C6001 at the operator that faulted. A diagnostic that concerns a whole file is reported at line 1, at the first character [LS-278].
3. <a id="LS-282"></a>Translation finds errors in phases: first forming tokens from the source text, then parsing, then checking names, types and constant expressions. The first diagnostic reported is the least, in the order of line, then column, then code, among the errors of the earliest phase that finds any. An implementation may report further diagnostics after the first, in the same order, at most 100 for a module, followed by a count of the rest. The output is deterministic [LS-282].
4. Only the code and the position are compared between implementations; the message and the notes are text for the reader [LS-312].
5. <a id="LS-277"></a>The text form has two severities: `error`, for a diagnostic, after which the program is rejected and nothing runs, and `fault`, for a run-time fault (5.2.3). `cint-core-1` defines no warnings: every construct is either valid or rejected [LS-277].
6. <a id="LS-276"></a>The text form of a diagnostic or a fault has this layout [LS-276]:

```text
<severity>[<code>]: <message>
  --> <file>:<line>:<column>
   |
NN | <the source line, as written>
   | <spaces><caret> <operation or note>
   |
   = <label>: <exact value>
```

   The caret marks the position. A diagnostic may add `= note:` lines and one `= help:` line, and a tool may add ` in <function>` to the position line. For a fault, the `<label>` lines are the fields of the fault record (5.2.3, 8.1), followed by the lines that 8.1 lists as not part of the record.
7. <a id="LS-279"></a>When the source line contains tabs, the caret line reproduces them at the same places, so the caret stands under its character in any tab width (6.4.2, example 5) [LS-279].
8. <a id="LS-289"></a>The *machine form* of a diagnostic or a fault is one JSON object on one line. `cint --json` writes it in place of the text form. Keys are sorted by their UTF-8 bytes and the object holds no insignificant white space. A value of the program is written as an object with its type `t` and its exact decimal value `v`, both strings, such as `{"t":"I64","v":"9223372036854775807"}` [LS-289]. The keys are:
   - a diagnostic has the keys `code`, `column`, `file`, `line`, `message`, `schema` (the string `CINT-DIAG-1`) and `severity`;
   - a fault has the keys `schema` (the string `CINT-FAULT-1`) and `severity`, the fields of the fault record (`code`, `operation`, `operands`, `exact`, `limit`, `file`, `line`, `column`, `revision`, `source_map`, `address` and `stack`), equal to those of the record (8.1), and the key `noncanonical`, an object that holds what is not part of the record, such as `fuel_consumed`. The `state` line of the text form (8.1) has no key [LS-289].
   - <a id="LS-290"></a>For a fault in a kernel, `address` is an object with the keys `dispatch`, `step` and `work_item`, which are part of the record, beside `kernel` and `phase` [LS-290].
9. <a id="LS-291"></a><a id="LS-292"></a><a id="LS-293"></a><a id="LS-294"></a>A diagnostic may suggest a fix, on its `help` line. A suggested fix [LS-291, LS-292, LS-293, LS-294]:
   - is an explicit edit of the source whose meaning is stated, never "add a cast" without saying which conversion and what it does;
   - is at most one per diagnostic;
   - never replaces a checked operator or conversion with a wrapping or saturating one, and never suggests `as%` to silence a range error; it may suggest a checked conversion, a wider type or a named operation with a wide intermediate such as `muldiv`;
   - leaves the meaning of the rest of the program unchanged, gives a program that translates, and says which fault a checked operation it adds can raise;
   - is applied by a tool only on request.

**Examples**

10. This example shows a literal that does not fit its type. The caret marks the literal:

```cint fit_level.ci
I8 level = 200;
"{level}\n";
```

```text
error[C2003]: literal does not fit its type
  --> fit_level.ci:1:12
   |
 1 | I8 level = 200;
   |            ^
```

11. A lexical error is reported before a type error on an earlier line, because forming tokens is the earlier phase. `007` is a decimal literal with a leading zero (6.4.5):

```cint phases.ci
I8 a = 300;
I64 b = 007;
"{a} {b}\n";
```

```text
error[C1022]: invalid integer literal
  --> phases.ci:2:9
   |
 2 | I64 b = 007;
   |         ^
```

12. This example shows the machine form of a diagnostic and of a fault. The program `fit.ci` is `fit_level.ci` above; `add_one.ci` prints `start` and then faults inside the function `add_one`:

```console
$ cint --json run fit.ci
{"code":"C2003","column":12,"file":"fit.ci","line":1,"message":"literal does not fit its type","schema":"CINT-DIAG-1","severity":"error"}
$ cint --json run add_one.ci
start
{"address":null,"code":"E_OVERFLOW","column":14,"exact":{"t":"Z","v":"9223372036854775808"},"file":"add_one.ci","limit":{"t":"I64","v":"9223372036854775807"},"line":2,"noncanonical":{"fuel_consumed":"2"},"operands":[{"t":"I64","v":"9223372036854775807"},{"t":"I64","v":"1"}],"operation":"add.checked.i64","revision":"<hash>","schema":"CINT-FAULT-1","severity":"fault","source_map":null,"stack":[{"column":12,"file":"add_one.ci","line":6}]}
```

   `<hash>` stands for the `revision` value, 64 hexadecimal digits that identify the program (3.31). `stack` holds the position of the call `add_one(big)`.

**Notes**

13. `cint run` writes diagnostics and fault reports to its standard error, and the program's output to its standard output. It prints neither the `help` line nor the function name of paragraph 6.

<a id="execution"></a>

### 5.2 Execution

<a id="program-start-and-termination"></a>

#### 5.2.1 Program start and termination

A program runs inside a context, one entry at a time. The context holds what lasts between entries; an entry is one run from an entry point to its end.

**Semantics**

1. <a id="IM-117"></a>A *context* is the state of one loaded program: its module-level variables, its arenas and pools and its fault record. It lasts across entries until the host program that created it destroys it (SPEC-03P.2) [IM-117].
2. Each entry (3.20) has its own fuel allowance (5.2.4), call-depth limit (5.2.5) and frame arena (5.2.6), and counts its own fuel and its own kernel dispatches [IM-117].
3. `cint run` creates a context and runs one entry in it: the top-level statements of a script, or `main` (5.1.2). Each `test` block runs as an entry in a fresh context of its own (6.15.3).
4. An entry ends in one of three ways [LS-221]:
   - it *completes*: the last top-level statement of the script finishes, or the entry function returns;
   - it *ends with an error*: `main`, of result type `E!void`, returns an error value of `E`;
   - it *faults* (5.2.3).
5. An entry that ends with an error is not faulted: the error value is its result, the context is not faulted and its output stands. `cint run` reports the error on its standard error, as 6.11.7 states, and exits with the status of Note 7 [LS-221].

**Examples**

6. This example shows a program whose `main` returns an error:

```cint config_check.ci
error Config { missing, invalid }
Config!void main() {
    "checking\n";
    return .invalid;
}
```

   The output written before the `return` stands, and `cint run` reports the error and exits with status 6:

```console
$ cint run config_check.ci
checking
error: config_check.Config.invalid (tag 2)
```

**Notes**

7. The exit status of `cint run` is 0 when the entry completes, 1 when it faults, 2 when the program is rejected with a diagnostic, and 6 when `main` returns an error. SPEC-06P.2 lists every status.

<a id="output"></a>

#### 5.2.2 Output

Output is the bytes a program prints.

**Semantics**

1. A program's output is the sequence of bytes that its print statements write, in the order they run (6.10.10, with the formatting of 7.5). The bytes depend only on the program and its semantic inputs (5.2.7), never on the host's locale or platform, so the same entry writes the same bytes on every implementation (8.2). `cint run` writes them to its standard output.

**Examples**

2. Output written before a fault stands. The fault report follows it:

```cint partial.ci
for i in 1..=3 {
    "line {i}\n";
}
U8 level = 255;
level += 1;
"not reached\n";
```

```text
line 1
line 2
line 3
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> partial.ci:5:7
   |
 5 | level += 1;
   |       ^ add.checked.u8
   ...
```

<a id="faults"></a>

#### 5.2.3 Faults

A fault stops a run at an operation that cannot give a correct result. It is reported with everything needed to see why: the operation, its operands, the exact result and the bound it crossed, and where it happened.

**Semantics**

1. <a id="IM-104"></a>Every fault has one of these *fault codes*. The number is the code's value in the encoded fault record (SPEC-01P.3) [IM-104]:

| Code | Number | Raised by | Rules |
|---|---:|---|---|
| `E_OVERFLOW` | 1 | A checked operator, conversion or named operation whose exact or rounded result is outside the range of its type | 6.7, 6.6, 7.2, 7.3 |
| `E_DIV_ZERO` | 2 | A division or remainder whose divisor is 0, `muldiv` included | 6.7.7, 7.2 |
| `E_BOUNDS` | 3 | An index outside an extent, a slice whose bounds are not valid, a full arena or pool or an exhausted frame arena | 6.12, 6.14, 5.2.6 |
| `E_SHAPE` | 4 | Views whose shapes disagree, a negative extent, `min` or `max` of an empty input, or a dispatch whose work-item count exceeds `MAX(I64)` | 6.12, 7.3, 7.4, 6.13 |
| `E_SHIFT` | 5 | A shift or rotation count outside `0` to `w - 1` | 6.7.8 |
| `E_NARROW` | 6 | A checked conversion whose value is outside the target's range, or a `c` hole whose value is not a Unicode scalar value | 6.6, 7.5.3 |
| `E_ALIAS` | 7 | A writable argument that overlaps another argument, or operands of `copy` that overlap | 6.9.5, 6.12.5, 6.13, 7.4 |
| `E_STALE_HANDLE` | 8 | A handle that is not valid for one of the reasons of 6.14.4, among them the null handle and a handle whose generation has ended, or an allocation from or a reset of a retired arena | 6.14 |
| `E_FUEL` | 9 | A charge point that would exceed the entry's fuel allowance | 5.2.4 |
| `E_UNSUPPORTED` | 10 | An entry, a dispatch or a call depth whose type, operation or layout the implementation cannot carry out exactly, found when the entry or the dispatch begins (3.16) | 4, 5.2.5 |
| `E_DOMAIN` | 11 | `isqrt`, `isqrt_round` or a fixed-point square root of a negative value, or `clamp` with its bounds in the wrong order | 7.2 |
| `E_DEPTH` | 12 | A call that would exceed the entry's call-depth limit | 5.2.5 |
| `E_ASSERT` | 13 | A failed `assert` outside a test | 6.10.9 |

2. <a id="IM-118"></a>A fault ends the current entry at once. No later operation of the entry runs, its deferred statements do not run (6.10.7), and the entry has no result [IM-118].
3. Program code cannot catch, inspect or resume a fault. No construct of the language observes a fault raised in its own context: the context is the only fault boundary, and a fault is handled by the program that supervises the context, through the host interface. A failure that a program is meant to handle is an error, returned as a value (6.11) [IM-118].
4. A fault sets the fault record of the context. The record stays: every further entry into the context fails at once with the same record, which the host receives, until the host resets the context. Resetting clears the fault record and changes no other state. A host that wants an earlier state restores a checkpoint (8.4) [IM-118].
5. <a id="IM-121"></a>After a fault, the state of the context is [IM-121]:

| Part of the state | What it holds |
|---|---|
| Module-level variables and arrays written by sequential code | Every store of every statement that completed before the fault, including statements of functions that the faulting statement called; not the store of the faulting statement itself (6.2.4) |
| Outputs of the kernel dispatch that faulted | Not published: their previous contents remain (6.13.6) |
| Fault record | The record of the fault (8.1) |
| Fuel consumed by the entry | The sum of the charges that succeeded; a charge that faults `E_FUEL` is not counted (5.2.4) |
| Local variables of active calls | Not part of the state; the debug tools can show them (8.5) |

6. <a id="IM-122"></a>Sequential code (3.35) does not roll back stores [IM-122].
7. <a id="LS-284"></a>A fault report names the fault code, the operation, every operand, the source position and the call path. The *operation identifier* is lower case and has the form `name.form.t` (B.3), with a second type for a conversion and a rounding mode where one applies: `add.checked.i64`, `shl.wrap.u32`, `as.checked.i64.i8`, `div.checked.i128` [LS-284]. 8.1 describes every field of the record and how to read a report.
8. <a id="LS-280"></a>Every value in a report is printed exactly, in decimal, with its type. When a fault concerns a range, the report prints the bound that was crossed as `limit`. A value is never abbreviated, rounded or written in scientific notation. A fixed-point operand is shown as its type, the word `raw` and its raw value (3.36), from which its exact decimal value follows (8.1) [LS-280].
9. <a id="LS-285"></a>Each fault code records these fields beyond the operation and the position [LS-285]:

| Code | Fields |
|---|---|
| `E_OVERFLOW` | the operands, the exact result and the bound crossed |
| `E_DIV_ZERO` | the dividend and the divisor |
| `E_BOUNDS` | the index or the bounds of a slice, the dimension and the extent |
| `E_SHAPE` | the extents that disagree |
| `E_SHIFT` | the value and the count; the limit is the largest valid count, `w - 1` |
| `E_NARROW` | the value, and the bound of the target type it crossed |
| `E_ALIAS` | the two arguments that overlap |
| `E_STALE_HANDLE` | the handle and the generations that disagree |
| `E_FUEL` | the allowance, as the limit |
| `E_DEPTH` | the depth limit |
| `E_UNSUPPORTED` | the operation that cannot be carried out |
| `E_DOMAIN` | the operand |
| `E_ASSERT` | the two operands of a comparison condition, or none |

10. <a id="LS-286"></a>A fault record is never shortened to fit a fixed-size structure. Its operands can exceed 1,024 bits: the exact product of two `I1024` values needs up to 2,047 bits [LS-286].
11. <a id="IM-105"></a>A host resource that runs out or fails, such as memory for a host allocation, a file or device error or a failing host callback, is not a fault. It is an outcome of the host interface (SPEC-03P.2): it has no fault record, and the entry it ends has no execution identity (8.2) [IM-105].
12. A fault during the evaluation of a constant expression at translation is not a run-time fault; it is the diagnostic C6001, which carries the same fault code and operands (6.8).

**Examples**

13. This example shows a fault inside a function. The report adds one `called from` line for each active call, outermost first:

```cint called.ci
I64 add_one(I64 v) {
    return v + 1;
}
"start\n";
I64 big = I64.max;
I64 next = add_one(big);
"{next}\n";
```

```text
start
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> called.ci:2:14
   |
 2 |     return v + 1;
   |              ^ add.checked.i64
   |
   = operation: add.checked.i64
   = operand-count: 2
   = operand: I64 9223372036854775807
   = operand: I64 1
   = exact:   9223372036854775808
   = limit:   I64 9223372036854775807
   ...
   = stack-depth: 1
   = called from: called.ci:6:12
   ...
```

14. A division by zero has no exact result and no limit:

```cint by_zero.ci
I64 total = 10;
I64 items = 0;
I64 mean = total / items;
"{mean}\n";
```

```text
fault[E_DIV_ZERO]: division by zero
  --> by_zero.ci:3:18
   |
 3 | I64 mean = total / items;
   |                  ^ div.checked.i64
   |
   = operation: div.checked.i64
   = operand-count: 2
   = operand: I64 10
   = operand: I64 0
   = exact:   none
   = limit:   none
   ...
```

15. A failed `assert` outside a test faults `E_ASSERT` at the first character of its condition, and records both operands of the comparison. The message is not evaluated:

```cint asserted.ci
I64 have = 3;
I64 want = 4;
assert(have == want, "counts differ");
"after\n";
```

```text
fault[E_ASSERT]: assertion failed
  --> asserted.ci:3:8
   |
 3 | assert(have == want, "counts differ");
   |        ^ assert.checked.bool
   |
   = operation: assert.checked.bool
   = operand-count: 2
   = operand: I64 3
   = operand: I64 4
   ...
```

**Notes**

16. In C, signed overflow, division by zero and an index outside an array are undefined behavior: the program may continue with any value. In cint each is a fault, reported at the operation. Annex C lists the others.
17. A program that needs a change of state to happen entirely or not at all keeps two copies and switches between them, or writes the new state with a kernel, whose outputs are published only when every work-item succeeds (6.13.6).

<a id="fuel"></a>

#### 5.2.4 Fuel

Fuel counts the steps an entry takes: calls, loop iterations, reductions and kernel dispatches. An entry may be given a fuel allowance, and it faults when a step would take it past the allowance.

**Semantics**

1. <a id="IM-137"></a>Fuel is counted in units of the fuel model `fuel-v1`, at the charge points of paragraph 3. It is not elapsed time and not a count of machine instructions: the fuel an entry consumes is the same on every conforming implementation, every executor and every optimization level [IM-137].
2. Each entry has a *fuel allowance* `B`, an `I64` with `B >= 0` and its consumed fuel starts at 0. A host that passes a value that is not such an `I64` (for example `2^63` as an unsigned 64-bit value) is not accepted when the entry starts (SPEC-03P.2). An entry may have no allowance: it still counts fuel, and only a charge that would carry its count past `MAX(I64)` faults `E_FUEL`, with the limit `I64 9223372036854775807` [IM-137].
3. <a id="IM-139"></a>The charge points of `fuel-v1` are these, and nothing else charges fuel [IM-139]:

| Charge point | Units |
|---|---:|
| A call of a user function, including the entry function and the implicit entry of a script, after the depth check and before the parameters are bound | 1 |
| Each iteration of a `for` or `while` loop, including the first, after the condition is `true` and before the body runs | 1 |
| A reduction over `n` elements (`sum`, `fold_checked`, `sum_wrap`, `sum_sat`, `min`, `max`, `count` and `dot` over an array), once, before any element is read | `ceil(n / 64)`, and 0 for `n = 0` |
| A kernel dispatch of `N` work-items, once, after the entry checks of the dispatch pass (6.13.6, paragraph 7) and before any work-item runs; the calls and loop iterations inside each work-item then charge as above, in work-item order | `N` |
| Operators, conversions and named scalar operations at any width, even when written as calls (`abs`, `muldiv`, `isqrt`, `add_result` and the others); array assignment and initializers; `copy`, `fill` and other operations that iterate over elements | 0 |

4. <a id="IM-141"></a>When a charge would make the entry's consumed fuel exceed `B`, the charge point faults `E_FUEL` with the operation identifier `fuel.charge` and the limit `B`. The step does not happen: the call is not entered, the iteration does not begin, the reduction reads no element, the dispatch does not start. The fault's position is the loop keyword for an iteration, the callee name for a call or a reduction, the function's name in its declaration for an entry function, the `test` keyword for a test block, and the first top-level statement for the implicit entry of a script [IM-141].
5. <a id="LS-141"></a>At a call, the arguments are evaluated first, then the checks on entry to the callee run in their stated order (shapes, then overlap of writable arguments, 6.9.5), then the depth check (5.2.5), then the fuel charge, then the body. A call that fails a check is not charged [LS-141].
6. <a id="IM-142"></a>An implementation may charge in batches, such as a loop's whole trip count at once, only when the result is the same on every input: the same position of an `E_FUEL` fault, the same consumed count and the same state at the fault [IM-142].
7. <a id="IM-143"></a><a id="IM-140"></a>With an allowance, every entry ends: each iteration, call, reduction and dispatch is charged, the work between two charge points is bounded by the program's text and the extents of the arrays it reads and writes, and the iterations and calls inside each work-item of a kernel are charged as they occur [IM-143, IM-140].
8. Fuel is counted per entry. It does not accumulate across entries and is not part of the context's state. The fuel an entry consumed is part of its outcome (8.2) [IM-137].

**Examples**

9. A fault report shows the fuel the entry consumed before the fault. Here it is 22: 1 for the entry `main`, 1 for the call of `total`, 3 for its iterations, 1 for the call of `total2` and 16 for `sum` (7.3) over 1,000 elements. `n` is a size parameter (3.34):

```cint fuel_count.ci
I64 total[n](in I64[n] xs) {
    I64 s = 0;
    for i in 0..n {
        s += xs[i];
    }
    return s;
}
I64 total2[n](in I64[n] xs) {
    return sum(xs);
}
void main() {
    I64[3] small = [1, 2, 3];
    I64[1000] big;
    "{total(small)} {total2(big)}\n";
    I8 stop = 127;
    stop += 1;
}
```

```text
6 0
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> fuel_count.ci:16:10
   ...
   = fuel-consumed: 22 (not part of the fault record)
   ...
```

10. A call that fails its shape check is not charged; the arguments are slices (6.12.3). The fuel consumed is 2, for `main` and `run`; the call of `copy_first` charged nothing:

```cint shape_first.ci
void copy_first[n](inout I64[n] dst, in I64[n] src) {
    dst[0] = src[0];
}
void run(I64 k) {
    I64[6] x;
    I64[6] y;
    copy_first(x[0..k], y[2..5]);
}
void main() {
    run(4);
}
```

```text
fault[E_SHAPE]: shapes do not match
  --> shape_first.ci:7:5
   |
 7 |     copy_first(x[0..k], y[2..5]);
   |     ^ bind.shape
   ...
   = fuel-consumed: 2 (not part of the fault record)
   ...
```

11. `cint debug locate` (8.5) runs a program with a fuel allowance. Its first lines name the executor that ran the program, the boundary (3.37) of the recording (3.40) at which the fault was found, and the *bracket*, the boundaries between which the fault lies (8.5). With an allowance of 3, the script `countdown.ci` charges 1 for its entry and 1 for each of two iterations; the third iteration would take it to 4, so the `while` faults:

```cint countdown.ci
I64 k = 0;
while (k < 5) {
    k += 1;
}
"{k}\n";
```

```text
5
```

```console
$ cint debug locate countdown.ci --fuel 3
program    countdown.ci   evaluator cpu-sir-interp, under a recording
target     boundary 1 of 1, the program entry: the first fault, reproduced by replay
bracket    the initial state; then boundary 1
fault[E_FUEL]: fuel allowance exhausted
  --> countdown.ci:2:1
   |
 2 | while (k < 5) {
   | ^ fuel.charge
   |
   = operation: fuel.charge
   = operand-count: 0
   = exact:   none
   = limit:   I64 3
...
   = fuel-consumed: 3 (not part of the fault record)
...
```

**Notes**

12. Operators and named scalar operations charge nothing because the cost of each is bounded by the width of its operands, and the number of them between two charge points is bounded by the program's text [IM-140].
13. 5.2.7 gives the allowance of `cint run`, and SPEC-06P.2 lists the allowance each tool gives.

<a id="call-depth"></a>

#### 5.2.5 Call depth

Every call of a user function makes the active calls one deeper. Each entry has a limit on that depth, and the call that would pass it faults at the same call on every implementation, instead of overflowing a native stack.

**Semantics**

1. <a id="IM-119"></a>Each entry has a *call-depth limit* `D`, an `I64` with `D >= 1`. The entry function, or the implicit entry of a script, has depth 1 and each active call of a user function adds 1 [IM-119].
2. <a id="LS-144"></a>A call that would make the depth exceed `D` faults `E_DEPTH` before its fuel charge and before its frame is created. The call is not entered and consumes no fuel. The fault record has the operation identifier `call.enter`, no operands and the limit `D`; its position is the callee name of that call, and its stack holds the `D - 1` call sites of the active calls [IM-119, LS-144].
3. Recursion is permitted outside kernels. A kernel body calls no user function (6.13.3), so a kernel never recurses [LS-144].
4. `D` is given by the host program or the tool that starts the entry (5.2.7). An implementation shall accept every `D` up to 4,096 (5.3) [LS-144].
5. An implementation whose native stack cannot hold `D` frames of the program shall keep the frames elsewhere, or refuse the entry with `E_UNSUPPORTED` before it starts. It shall not crash [IM-119, LS-144].

**Examples**

6. Recursion to a depth of 11 completes. The second call stops at depth 256: the script's entry and 255 active calls of `down`:

```cint depth.ci
I64 down(I64 n) {
    if (n == 0) {
        return 0;
    }
    return down(n - 1) + 1;
}
"{down(10)}\n";
"{down(1000000)}\n";
```

```text
10
fault[E_DEPTH]: call depth limit exceeded
  --> depth.ci:5:12
   |
 5 |     return down(n - 1) + 1;
   |            ^ call.enter
   |
   = operation: call.enter
   = operand-count: 0
   = exact:   none
   = limit:   I64 256
   ...
   = stack-depth: 255
   = called from: depth.ci:8:3
   = called from: depth.ci:5:12
   ...
```

<a id="the-frame-arena"></a>

#### 5.2.6 The frame arena

The local arrays of sequential code (3.35) live in a region of fixed capacity, the frame arena and not on a native stack. Its capacity is counted in elements, so a program runs out of it at the same declaration on every implementation, and running out is a fault, not a crash.

**Constraints**

1. <a id="LS-110"></a>A local array whose extent is a size parameter (3.34), such as `I64[n] tmp;`, is permitted only in sequential code. In a kernel body it is diagnosed C9102 at that extent, since the private arrays of a kernel have constant extents (6.13.3) [LS-110].

**Semantics**

2. Each local array of sequential code is storage of the entry's frame arena (3.23). The arena's capacity, and the use of it, are counted in elements: each element counts once, whatever its type [LS-110].
3. The capacity is a semantic input of the entry (5.2.7). Its default, and the least capacity an implementation shall accept, is 2,097,152 elements [LS-110].
4. Each entry starts with an empty frame arena. A local array is charged its number of elements when its declaration runs, after its shape is checked (6.12.1) and before its initializer, and is released when its block exits, however the block exits. The charges therefore follow the nesting of blocks and the depth of recursion, and an array declared in a loop body is charged once in each iteration and released before the next [LS-110].
5. Scalars, structs, view variables and the private arrays of a kernel are not charged. The top-level arrays of a script are locals of its entry and are charged [LS-110].
6. A declaration whose charge would exceed the capacity faults `E_BOUNDS` with the operation identifier `arena.alloc`, at the declared name. The operands are three `I64` values: the elements requested, the elements in use and the capacity. The record has no exact result and no limit [LS-110].
7. An implementation need not charge the local arrays of an entry when it proves that, over every call path, the entry never uses more than 2,097,152 elements: no run within the semantic inputs can then exhaust the arena [LS-110].

**Examples**

8. Each call of `nest` holds 100,000 elements. Twenty nested calls fit; the twenty-first does not. `E_BOUNDS` covers an exhausted frame arena (5.2.3), so the message reads `index out of bounds`:

```cint frames.ci
I64 nest(I64 level) {
    I64[100000] scratch;
    scratch[0] = level;
    if (level == 0) {
        return 0;
    }
    return nest(level - 1) + scratch[0];
}
"{nest(19)}\n";
"{nest(25)}\n";
```

```text
190
fault[E_BOUNDS]: index out of bounds
  --> frames.ci:2:17
   |
 2 |     I64[100000] scratch;
   |                 ^ arena.alloc
   |
   = operation: arena.alloc
   = operand-count: 3
   = operand: I64 100000
   = operand: I64 2000000
   = operand: I64 2097152
   = exact:   none
   = limit:   none
   ...
```

9. An array declared in a loop body is released at the end of each iteration, so 100 iterations of 100,000 elements fit:

```cint loop_frames.ci
void main() {
    I64 total = 0;
    for i in 0..100 {
        I64[100000] row;
        row[0] = i;
        total += row[0];
    }
    "{total}\n";
}
```

```text
4950
```

10. Elements are counted, not bytes. After an array of 2,097,152 `I64` elements, one more `U8` element does not fit:

```cint element_count.ci
void main() {
    I64[2097152] all;
    U8[1] one;
    "{one[0]}\n";
}
```

```text
fault[E_BOUNDS]: index out of bounds
  --> element_count.ci:3:11
   |
 3 |     U8[1] one;
   |           ^ arena.alloc
   |
   = operation: arena.alloc
   = operand-count: 3
   = operand: I64 1
   = operand: I64 2097152
   = operand: I64 2097152
   ...
```

<a id="semantic-inputs"></a>

#### 5.2.7 Semantic inputs

The outcome of an entry is decided by its semantic inputs (3.24) and by nothing else. Three of them are numbers that bound the entry, given by the host program or the tool that starts it.

**Semantics**

1. The fuel allowance (5.2.4), the call-depth limit (5.2.5) and the frame-arena capacity (5.2.6) are given for each entry by the host program, by the configuration of the tool (SPEC-06P.2), or by the tool that starts the entry. A value given explicitly replaces the default [LS-144, IM-137, LS-110].
2. `cint run` starts its entry with these values [LS-144, LS-110]:

| Semantic input | Range | `cint run` |
|---|---|---|
| Fuel allowance | an `I64` from 0, or none | none |
| Call-depth limit `D` | an `I64` from 1 | 256 |
| Frame-arena capacity | at least 2,097,152 elements | 2,097,152 elements |

3. 8.2 states what agreement on an outcome means, and what never enters it: the time, the machine, the locale, the scheduling of threads and the addresses of native storage.

<a id="implementation-limits"></a>

### 5.3 Implementation limits

Every implementation accepts programs at least as large as the limits below.

**Semantics**

1. <a id="LS-311"></a>A conforming implementation shall accept every program within these limits [LS-311]:

| Quantity | Limit |
|---|---|
| Size of a source file | at least 16,777,216 bytes |
| Nesting of blocks and of expressions, not counting `else if` chains | at least 256 |
| `else if` arms in one chain | no limit other than the file's size |
| Cases in one `switch` | at least 65,536 |
| Parameters of one function | at least 64 |
| Fields of one struct | at least 4,096 |
| Length of an identifier | exactly 255 bytes: a longer identifier is C1012 on every implementation (6.4.4) |
| Magnitude of a literal | exactly `2^4096 - 1`: a larger literal is C1023 on every implementation (6.4.5) |
| Rank of an array or view | exactly 4 (6.12) |
| Call-depth limit `D` accepted as a semantic input | at least 4,096 active calls (5.2.5) |
| Frame-arena capacity accepted as a semantic input | at least 2,097,152 elements (5.2.6) |

2. The rows that say "exactly" bind every implementation; the others are minimums. An implementation may accept more [LS-311].
3. A program beyond one of an implementation's own limits is rejected with a diagnostic whose code begins with `C9`: C9001 for a capacity or output size, reported at line 1 when it concerns a whole file, and C9004 for nesting beyond the implementation's limit, at the construct that exceeds it. The implementation shall not miscompile or truncate the program [LS-311].

<a id="language"></a>

## 6 Language

This clause defines the language: the notation of its grammar, the concepts it rests on, its types, lexical elements, literals, conversions, expressions, declarations, statements, errors, arrays and views, kernels, memory and modules.

<a id="notation"></a>

### 6.1 Notation

This subclause gives the notation of the Syntax parts and the symbols used in rules. Clause 3 defines the terms.

1. Syntax is written in a form of EBNF: `=` defines a rule, `;` ends it, `|` separates alternatives, `[ x ]` is optional, `{ x }` is repeated zero or more times, `( )` groups, quoted text is a terminal, and `(* *)` encloses a comment [LS-295].
2. The names of lexical rules, which form tokens, are written in capitals, such as `IDENT` and `DEC_INT`. Tokens are formed by maximal munch (6.4.2), and white space and comments between tokens are skipped. The other rules are written in lower case and are built from tokens [LS-295].
3. A Syntax part states the structure of a construct only. The rules on types, on mixing operators, on exhaustive `switch` and the like are its Constraints. Annex A collects every Syntax part [LS-295].
4. <a id="IM-1"></a>Rules use the symbols of 3.45, with these values. For an integer type `T` of width `w`: when `T` is signed, `MIN(T)` is `-2^(w-1)` and `MAX(T)` is `2^(w-1) - 1`; when `T` is unsigned, `MIN(T)` is 0 and `MAX(T)` is `2^w - 1`. `wrap(T, v)` is the one value `r` with `in(T, r)` and `r` congruent to `v` modulo `2^w`. `clamp(v, l, h)`, for `l <= h`, is `min(max(v, l), h)`. `2^k` is 2 raised to the power `k`. These symbols have the same values as the built-in operations of the same names (7.2.1), without their faults [IM-1].
5. Numbers in rules and tables are decimal unless written with the prefix `0x` [IM-1].

<a id="concepts"></a>

### 6.2 Concepts

<a id="values"></a>

#### 6.2.1 Values

Every value a program computes has a type: an integer in its type's range, a fixed-point value, a `Bool`, an error value, a handle, or an array or struct of these. How a machine stores an integer is not part of its meaning.

**Semantics**

1. <a id="IM-5"></a>A value of an integer type is a member of Z within the type's range. Its meaning does not depend on how it is represented [IM-5].
2. Only the operations defined on bits see a representation: the bitwise operators, the rotations, the wrapping conversion `as%`, the wrapping shift `<<%`, the conversions `!bits` and `!lanes` of a hole (7.5.4), and the encodings of SPEC-01P.3. For these, a signed value has the two's complement bit pattern of its type's width. Everywhere else, only the mathematical value can be observed [IM-5].
3. `Bool` has the values `false` and `true` and is not an integer type (6.3.3). Arrays and structs hold values of the types of 6.3.

**Examples**

4. The value -1 of type `I8` has the bit pattern `11111111`, which `as%` reads as the `U8` value 255. The plain operators see only the value:

```cint values.ci
I8 a = -1;
U8 bits = a as% U8;
I8 b = a * a;
"{bits} {b} {a & 15}\n";
```

```text
255 1 15
```

<a id="objects-and-storage-durations"></a>

#### 6.2.2 Objects and storage durations

An *object* is storage that holds a value: a variable, an element of an array or a field of a struct. Each object exists for a stated span, its *storage duration* and holds a value of its type for the whole span.

**Semantics**

1. A local variable, including a parameter, exists from the moment its declaration runs, or its function is entered, until its block exits, however the block exits. A local array is storage of the frame arena (5.2.6) [LS-112].
2. The variables declared at the top level of a script are locals of the script's entry (5.1.2): they exist until the entry ends.
3. A module-level variable of an importable module, or of a program with `main`, exists as long as the context and keeps its value from one entry to the next (6.9.3).
4. The bytes of a string literal exist for the whole run and cannot be written (6.4.8).
5. Objects in arenas and pools exist until their arena is reset or their slot is released (6.14).
6. Every object holds a value of its type from the moment it exists: a variable of an integer, fixed-point, `Bool` or error-set type has a required initializer, and an array or struct without one is filled with zeros (6.9.1). No object is read before it holds a value.
7. A view refers to the storage of another object. A view cannot be stored, so it never outlives that object (6.12).

<a id="scopes-and-visibility"></a>

#### 6.2.3 Scopes and visibility

A name is visible in a region of the program, its scope. Names declared at module level are visible throughout the module; local names are visible from their declaration to the end of their block. A name visible at a point cannot be declared again there.

**Constraints**

1. <a id="LS-112"></a>A declaration shall not use a name that is visible at that point, whether the visible name is a local, a parameter, a module-level name, an imported name or a built-in name. A repeated name is C3001, at the declared name. There is no shadowing [LS-112].
2. A name that is not visible at the point of use is C3005 [LS-112].

**Semantics**

3. Each block introduces a scope [LS-112].
4. The names of the types, structs, error sets, functions, kernels, constants and module-level variables of a module are visible everywhere in the module, whatever the order of declarations [LS-112].
5. A local name, including a top-level variable of a script (5.1.2), is visible from its declaration to the end of its block [LS-112].

**Examples**

6. A name of an enclosing block cannot be declared again in an inner block:

```cint shadow.ci
I64 x = 1;
if (x == 1) {
    I64 x = 2;
    "{x}\n";
}
```

```text
error[C3001]: duplicate name
  --> shadow.ci:3:9
   |
 3 |     I64 x = 2;
   |         ^
```

7. Blocks that do not enclose each other may declare the same name, and a constant may be declared after the function that uses it:

```cint siblings.ci
I64 scaled(I64 v) {
    return v * K;
}
const I64 K = 10;
I64 total = 0;
for i in 0..3 {
    I64 step = i + 1;
    total += step;
}
for i in 0..3 {
    I64 step = scaled(i);
    total += step;
}
"{total}\n";
```

```text
36
```

8. A local name is not visible before its declaration:

```cint too_early.ci
"{later}\n";
I64 later = 1;
```

```text
error[C3005]: undefined or unusable name
  --> too_early.ci:1:3
   |
 1 | "{later}\n";
   |   ^
```

<a id="evaluation-order"></a>

#### 6.2.4 Evaluation order

Every expression is evaluated in one order, left to right, and every operand is evaluated exactly once. So when an expression could fault in more than one place, the first fault in that order is the fault, on every implementation.

**Constraints**

1. <a id="LS-142"></a>A call always has parentheses. Naming a function without calling it is rejected: as a statement, such as `f;`, with C4012, and where a value is expected, such as `I64 y = twice;`, with C4010 [LS-142].
2. <a id="LS-143"></a>A call statement whose function returns a value other than `void` shall use the value or discard it with `_ =` (6.11); otherwise it is C4011 [LS-143].
3. <a id="IM-97"></a>Assignment, compound assignment, `++` and `--` are statements, not expressions. They have no value and cannot appear inside an expression (6.7.14) [IM-97].

**Semantics**

4. <a id="IM-92"></a><a id="LS-137"></a>The operands of an operator are evaluated left to right, each completely, including any fault it raises, before the next begins. The operator is applied after its operands are evaluated [IM-92, LS-137].
5. <a id="IM-93"></a>A call evaluates the callee, then each argument from left to right, then performs the call. Named arguments are evaluated in the order written. The elements of an array literal and the arguments of a struct constructor are evaluated from left to right [IM-93, LS-137].
6. <a id="IM-94"></a><a id="LS-138"></a>`&&`, `||`, `? :` and `catch` evaluate only the operands that the result requires [IM-94, LS-138].
7. <a id="IM-95"></a>No operand is evaluated twice, and none is skipped because an implementation can predict its value, if its evaluation could fault [IM-95].
8. <a id="IM-96"></a>Consequently, the first fault in evaluation order is the fault of the expression [IM-96].
9. <a id="IM-98"></a><a id="LS-139"></a>An assignment `place = expr` evaluates the subexpressions of the place, such as an array and its indices, from left to right and checks them first, then evaluates `expr`, then stores. `place op= expr` reads the place once, when the place is checked [IM-98, LS-139].
10. <a id="IM-99"></a>The store of the statement that faults does not happen. Every store, and every output, of a statement that completed before the fault remains, including those of statements in functions that the faulting statement called [IM-99].
11. <a id="IM-100"></a>In sequential code, stores become observable in program order. An implementation may reorder or remove a store only when no run, including one that faults later, can observe the difference in the state of the context [IM-100].
12. <a id="IM-101"></a>Every intermediate result has the type of its operation. There is no integer promotion, no evaluation in a wider type and no implicit widening to `I64` [IM-101].
13. <a id="IM-102"></a>A wider intermediate exists only where a rule names one: the fixed-point `*`, `/` and square root (6.7.7, 7.2), the fixed-point conversions (6.3.2, paragraph 7), `mul_full`, `muldiv`, `isqrt` and `qmul` (7.2) and `sum` and `dot` (7.3). An implementation may use internal temporaries, but they are never observable [IM-102].

**Examples**

14. Operands and arguments are evaluated from left to right, and the result follows the precedence of the operators:

```cint left_right.ci
I64 show(I64 v) {
    "[{v}]";
    return v;
}
I64 s = show(1) + show(2) * show(3);
"\n{s}\n";
```

```text
[1][2][3]
7
```

15. The left operand faults first, so the division by zero is never reached:

```cint first_fault.ci
I64 x = I64.max;
I64 y = 0;
I64 r = (x + 1) / y;
"{r}\n";
```

```text
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> first_fault.ci:3:12
   |
 3 | I64 r = (x + 1) / y;
   |            ^ add.checked.i64
   ...
```

16. The place is checked before the value is computed:

```cint place_first.ci
void main() {
    I64[3] a;
    I64 z = 0;
    a[-1] = 1 / z;
}
```

```text
fault[E_BOUNDS]: index out of bounds
  --> place_first.ci:4:6
   |
 4 |     a[-1] = 1 / z;
   |      ^ index.checked.i64
   |
   = operation: index.checked.i64
   = operand-count: 1
   = operand: I64 -1
   = exact:   none
   = limit:   I64 3
   ...
```

17. `g` completes before `1 / z` faults, so its output and its store remain; the assignment to `y` does not happen:

```cint completed.ci
I64 counter = 0;
I64 g() {
    counter = counter + 1;
    "g ran, counter {counter}\n";
    return 5;
}
void step(I64 z) {
    I64 y = 7;
    y = g() + 1 / z;
}
void main() {
    step(0);
}
```

```text
g ran, counter 1
fault[E_DIV_ZERO]: division by zero
  --> completed.ci:9:17
   ...
```

18. `&&`, `||` and `? :` skip the operands they do not need:

```cint short_circuit.ci
I64 loud(I64 v) {
    "[{v}]";
    return v;
}
I64 a = 0;
Bool b = a != 0 && loud(1) == 1;
Bool c = a == 0 || loud(2) == 2;
I64 d = a == 0 ? loud(3) : loud(4);
"\n{b} {c} {d}\n";
```

```text
[3]
false true 3
```

19. The product `a * b` has type `I32`, so it faults even though the final quotient would fit. Converting first, or naming a wide intermediate, gives the quotient:

```cint intermediate.ci
I32 a = 2_000_000_000;
I32 b = 2;
I32 d = ((a as I64) * (b as I64) / 4) as I32;
I32 e = muldiv(I32, a, b, 4, floor);
"{d} {e}\n";
I32 c = a * b / 4;
"{c}\n";
```

```text
1000000000 1000000000
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> intermediate.ci:6:11
   |
 6 | I32 c = a * b / 4;
   |           ^ mul.checked.i32
   ...
```

20. A call statement must use the result of its function:

```cint unused.ci
I64 f() {
    return 1;
}
f();
```

```text
error[C4011]: control-flow rule
  --> unused.ci:4:1
   |
 4 | f();
   | ^
```

**Notes**

21. In C, the order in which the operands of most operators and the arguments of a call are evaluated is unspecified, and `i = i++ + 1` is undefined. In cint the order is fixed, and an assignment cannot appear inside an expression.

<a id="types"></a>

### 6.3 Types

Every object and every expression of a cint program has a type, and the type is known when the program is translated. The type decides which values an object can hold, which operators apply to it, and what happens when a result does not fit. This section describes each kind of type: its values, how its name is written, and when two types are the same. The conversions between types are in 6.6, the operators in 6.7 and the declarations that create objects in 6.9.

cint has no floating-point type and no pointer type, and no value changes type without a conversion written in the program (6.6), except that a value of an error set converts to a larger set (6.3.7).

**Semantics**

1. The types are:

| Kind | Types | Values | Section |
|---|---|---|---|
| Integer | `I8` `I16` `I32` `I64` `I128` `I256` `I512` `I1024`, `U8` `U16` `U32` `U64` `U128` `U256` `U512` | the integers in the type's range | 6.3.1 |
| Fixed point | `Q<i>.<f>`, such as `Q16.16` | the multiples of 2^-f in the type's range | 6.3.2 |
| Boolean | `Bool` | `false` and `true` | 6.3.3 |
| Array | an element type and a shape, such as `I64[8]` or `I32[2, 3]` | a fixed number of elements of the element type | 6.3.4 |
| View | `in` or `inout` before an array type, such as `in I64[_]` | a window onto the elements of an array | 6.3.5 |
| Struct | a name declared with `struct` | one value for each field | 6.3.6 |
| Error set and error union | a name declared with `error`; `E!T` | the values of the set; a `T` or an error | 6.3.7 |
| Arena, pool and handle | `Arena(T)`, `Pool(T)`, `Handle(T[])`, `Handle(T)` | an arena, a pool or a checked reference into one | 6.3.8 |

   [LS-50]

2. The integer types and the fixed-point types are the *numeric types*. The numeric types and `Bool` are the *scalar types*.

3. <a id="IM-56"></a>Two integer types, or two fixed-point types, are the same type only when their names are the same. Two struct types, or two error sets, are the same type only when they come from the same declaration, so two structs with the same fields are different types. An operator, an assignment or an argument that needs one type and is given another is diagnosed C2001 (6.6) [IM-56].

<a id="integer-types"></a>

#### 6.3.1 Integer types

An integer type holds every integer from its minimum to its maximum, and no other value. Its name is `I` for a signed type or `U` for an unsigned type, followed by its width in bits. The types of 8 to 64 bits are the usual machine integers. The *wide types*, `I128` to `I1024` and `U128` to `U512`, are integer types like the others, with larger ranges.

**Syntax**

```ebnf
INT_TYPE = "I8" | "I16" | "I32" | "I64" | "I128" | "I256" | "I512" | "I1024"
         | "U8" | "U16" | "U32" | "U64" | "U128" | "U256" | "U512" ;   (* spelled as IDENT *)
```

**Constraints**

1. <a id="IM-7"></a><a id="IM-9"></a>The bounds of a `for` range shall have type `I64`, as an array index does (6.12.2). A bound of another integer type is diagnosed C2001, even when its value fits; `as I64` (6.6) converts it [IM-7, IM-9].

**Semantics**

2. <a id="IM-4"></a><a id="IM-6"></a>An integer type `T` of width `w` has the range `MIN(T)` to `MAX(T)`. For a signed type these are -2^(w-1) and 2^(w-1) - 1; for an unsigned type, 0 and 2^w - 1 [IM-4, IM-6]:

| Type | `w` | `MIN(T)` | `MAX(T)` |
|---|---:|---:|---:|
| `I8` | 8 | -128 | 127 |
| `I16` | 16 | -32768 | 32767 |
| `I32` | 32 | -2147483648 | 2147483647 |
| `I64` | 64 | -9223372036854775808 | 9223372036854775807 |
| `I128` | 128 | -2^127 | 2^127 - 1 |
| `I256` | 256 | -2^255 | 2^255 - 1 |
| `I512` | 512 | -2^511 | 2^511 - 1 |
| `I1024` | 1024 | -2^1023 | 2^1023 - 1 |
| `U8` | 8 | 0 | 255 |
| `U16` | 16 | 0 | 65535 |
| `U32` | 32 | 0 | 4294967295 |
| `U64` | 64 | 0 | 18446744073709551615 |
| `U128` | 128 | 0 | 2^128 - 1 |
| `U256` | 256 | 0 | 2^256 - 1 |
| `U512` | 512 | 0 | 2^512 - 1 |

3. A value of an integer type is an integer in Z, within the type's range. How a value is stored does not change its meaning (6.2.1).

4. <a id="LS-51"></a>`T.min` and `T.max` are `MIN(T)` and `MAX(T)` as values of type `T`. Both are constant expressions (6.8) [LS-51].

5. <a id="IM-11"></a><a id="IM-16"></a>The wide types are integer types like `I64`: they are copied by assignment and passed in any mode. Every operator of 6.7 and every conversion of 6.6 applies to them with the same meaning, the same forms and the same faults as on `I64`; only the range differs [IM-11, IM-16].

6. The named operations of 7.2 are defined for every integer type, with these exceptions, each diagnosed C2103: `rotl` and `rotr` take unsigned types only; `uabs` takes a signed type and gives the unsigned type of the same width, so it is not defined for `I1024`; `mul_full` gives the type of twice the width with the same signedness, so it is not defined for `I1024` or `U512` [IM-16].

7. `I64` is the *canonical word*. The length and the extents of an array, an index, the bounds and the variable of a `for` range, the result of `count`, a fuel allowance and a call-depth limit are `I64` values. A literal with no context to type it is an `I64` (6.5) [IM-7].

8. <a id="IM-8"></a>An implementation may hold an `I64` value in a narrower machine representation only where it has established, from facts known at translation or checked before a dispatch begins, that every value the object can take fits that representation and that no value and no fault can differ [IM-8].

**Examples**

9. This example shows the range of each type of 8 to 128 bits:

```cint int_ranges.ci
"I8   {I8.min} to {I8.max}\n";
"U8   {U8.min} to {U8.max}\n";
"I16  {I16.min} to {I16.max}\n";
"U16  {U16.min} to {U16.max}\n";
"I32  {I32.min} to {I32.max}\n";
"U32  {U32.min} to {U32.max}\n";
"I64  {I64.min} to {I64.max}\n";
"U64  {U64.min} to {U64.max}\n";
"I128 {I128.min} to {I128.max}\n";
```

```text
I8   -128 to 127
U8   0 to 255
I16  -32768 to 32767
U16  0 to 65535
I32  -2147483648 to 2147483647
U32  0 to 4294967295
I64  -9223372036854775808 to 9223372036854775807
U64  0 to 18446744073709551615
I128 -170141183460469231731687303715884105728 to 170141183460469231731687303715884105727
```

10. A wide type holds the square of the largest `I64`, and `I256` holds its cube. The same multiplication checks its result as it does in `I64`, so four times the square, which needs 129 bits, faults:

```cint wide.ci
I128 big = I64.max as I128;
I128 square = big * big;
"{square}\n";
I256 cube = (square as I256) * (big as I256);
"{cube}\n";
I128 four = square * 4;
"{four}\n";
```

```text
85070591730234615847396907784232501249
784637716923335095224261902710254454442933591094742482943
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> wide.ci:6:20
   |
 6 | I128 four = square * 4;
   |                    ^ mul.checked.i128
   |
   = operation: mul.checked.i128
   = operand-count: 2
   = operand: I128 85070591730234615847396907784232501249
   = operand: I128 4
   = exact:   340282366920938463389587631136930004996
   = limit:   I128 170141183460469231731687303715884105727
   ...
```

11. This example shows the unsigned wide types:

```cint unsigned_wide.ci
"U128 {U128.min} to {U128.max}\n";
U256 one = 1;
U256 top = one << 255;
"{top}\n";
U128 a = U128.max;
"{a +% 1} {a +| 1 == a}\n";
```

```text
U128 0 to 340282366920938463463374607431768211455
57896044618658097711785492504343953926634992332820282019728792003956564819968
0 true
```

12. A range bound of type `I32` is not widened to `I64`. The fix is `0..(n as I64)`:

```cint canonical_word.ci
I32 n = 3;
for i in 0..n {
    "{i}\n";
}
```

```text
error[C2001]: type mismatch
  --> canonical_word.ci:2:13
   |
 2 | for i in 0..n {
   |             ^
```

**Notes**

13. In C, the width of `int` and `long` depends on the platform, and an operand narrower than `int` is promoted before arithmetic. In cint each width is fixed by the type's name, and an `I8` operand stays an `I8`: there is no promotion.

14. The byte layout of integer values in storage and in fault records is given by SPEC-01P.3, and their layout at the C interface by SPEC-03P.2.

<a id="fixed-point-types"></a>

#### 6.3.2 Fixed-point types

A fixed-point type holds numbers with a fixed number of binary fraction digits. `Q16.16` has 16 integer bits, counting the sign bit, and 16 fraction bits: every value is a whole multiple of 2^-16, from -32768 up to 32768 - 2^-16. A value is stored as a signed integer, its raw value, equal to the number times 2^16. The number 1.5 is stored as 98304, and the step between two neighboring values is 2^-16, which is 0.0000152587890625. Fixed point serves where a C program would use floating point for numbers of a known resolution and range, such as positions, rates and gains. Each value is an exact multiple of the resolution, and an operation that rounds does so once, by a named rounding mode: `half_even` unless the program names another.

**Syntax**

```ebnf
FIXED_TYPE = "Q" nonzero { digit } "." ( "0" | nonzero { digit } ) ;
```

1. <a id="LS-36"></a>A fixed-point type name is one token with no space inside: `Q16.16`, `Q1.15`, `Q8.0`. The number before the point is `i` and the number after it is `f` [LS-36].

**Constraints**

2. <a id="IM-55"></a>`i + f` shall be 8, 16, 32, 64 or 128; otherwise the name is diagnosed C2005. The grammar makes `i` at least 1 and `f` at least 0 [IM-55, LS-36].

**Semantics**

3. <a id="IM-54"></a>`Q<i>.<f>` is a signed type. `i` counts the integer bits, the sign bit included and `f` counts the fraction bits. Its *storage type* is the signed integer type of width `w = i + f`: `I8`, `I16`, `I32`, `I64` or `I128` [IM-54, IM-55].

4. A value of `Q<i>.<f>` is the rational number `r / 2^f`, where the integer `r`, the value's *raw value*, is a value of the storage type. The raw value is also called the *storage integer* (3.36). The type therefore has exactly 2^w values, one for each raw value; its *resolution*, the difference between neighboring values, is 2^-f; and its range is -2^(i-1) to 2^(i-1) - 2^-f [IM-54]:

| Type | Storage type | Resolution | Least value | Greatest value |
|---|---|---|---:|---:|
| `Q1.15` | `I16` | 2^-15 | -1 | 1 - 2^-15 |
| `Q8.8` | `I16` | 2^-8 | -128 | 128 - 2^-8 |
| `Q8.0` | `I8` | 1 | -128 | 127 |
| `Q16.16` | `I32` | 2^-16 | -32768 | 32768 - 2^-16 |
| `Q32.32` | `I64` | 2^-32 | -2^31 | 2^31 - 2^-32 |
| `Q4.60` | `I64` | 2^-60 | -8 | 8 - 2^-60 |
| `Q80.48` | `I128` | 2^-48 | -2^79 | 2^79 - 2^-48 |
| `Q1.127` | `I128` | 2^-127 | -1 | 1 - 2^-127 |

5. `Q.min` and `Q.max` are the least and the greatest value of `Q`, and `Q.epsilon` is its resolution, the value whose raw value is 1. All three are constant expressions (6.8) [LS-51].

6. Two fixed-point types are the same type only when both `i` and `f` are equal, as 6.3 states. `Q16.16` and `Q8.24` are different types, and `Q16.16` is a different type from its storage type `I32`. A value moves from one of these types to another only by a conversion (6.6), and the raw value of a fixed-point value is read and written with `.raw` (6.6).

7. Every operation on a fixed-point value is defined on its raw value. Addition, subtraction, negation and comparison act on the raw values exactly as on the storage type, and do not round. Multiplication, division and square root compute their result in an intermediate of 2w bits and round it once to the type's resolution, by `half_even` unless the program names another mode; the rounding modes are defined in 6.7.15. As on integers, the plain operators fault when a result does not fit, the wrapping operators wrap the raw value and the saturating operators clamp it. The rules are in 6.7 and 7.2 [IM-54].

**Examples**

8. This example shows the number, its raw value, the resolution and the range of `Q16.16`:

```cint fixed_basics.ci
Q16.16 a = 1.5;
Q16.16 b = -0.25;
"{a} is stored as {a.raw}\n";
"{b} is stored as {b.raw}\n";
"resolution {Q16.16.epsilon}\n";
"range {Q16.16.min} to {Q16.16.max}\n";
```

```text
1.5 is stored as 98304
-0.25 is stored as -16384
resolution 0.0000152587890625
range -32768.0 to 32767.9999847412109375
```

9. Formats trade range for resolution. `Q8.0` holds the integers of `I8`:

```cint fixed_formats.ci
"Q1.15:  {Q1.15.min} to {Q1.15.max}, step {Q1.15.epsilon}\n";
"Q8.8:   {Q8.8.min} to {Q8.8.max}, step {Q8.8.epsilon}\n";
"Q8.0:   {Q8.0.min} to {Q8.0.max}, step {Q8.0.epsilon}\n";
"Q32.32: step {Q32.32.epsilon}\n";
```

```text
Q1.15:  -1.0 to 0.999969482421875, step 0.000030517578125
Q8.8:   -128.0 to 127.99609375, step 0.00390625
Q8.0:   -128.0 to 127.0, step 1.0
Q32.32: step 0.00000000023283064365386962890625
```

10. A number that is not a multiple of the resolution has no exact value in the type. The decimal 0.1 is one: written with a `round` clause (6.4.6), it takes the nearest value, which is closer in `Q16.16` than in `Q8.8`:

```cint fixed_grid.ci
Q16.16 tenth = 0.1 round half_even;
Q8.8 coarse = 0.1 round half_even;
"{tenth} (raw {tenth.raw})\n";
"{coarse} (raw {coarse.raw})\n";
```

```text
0.100006103515625 (raw 6554)
0.1015625 (raw 26)
```

11. A type of 128-bit storage holds a wide range at a fine resolution. `Q80.48` holds a distance in meters across the solar system to 2^-48 of a meter:

```cint fixed_wide.ci
Q80.48 au = 149597870700.0;
"au {au} (raw {au.raw})\n";
"step {Q80.48.epsilon}\n";
```

```text
au 149597870700.0 (raw 42108057171246227600179200)
step 0.000000000000003552713678800500929355621337890625
```

13. `Q17.16` would need 33 bits of storage:

```cint fixed_name.ci
Q17.16 x = 1.0;
"{x}\n";
```

```text
error[C2005]: fraction, fixed-point, enum literal, or rounding rule
  --> fixed_name.ci:1:1
   |
 1 | Q17.16 x = 1.0;
   | ^
```

**Notes**

13. Some documentation names a fixed-point format by its fraction bits alone, and calls `Q1.15` "Q15". A cint type name always gives both numbers, and `i` includes the sign bit.

14. `cint-core-1` has no unsigned fixed-point type and no fixed-point type wider than 128 bits.

15. ISO C has no fixed-point type, and a C program that needs one scales an integer by hand. A `Q` type is that scaled integer with the scale in its type, so the compiler keeps track of the scale and of every rounding.

<a id="bool"></a>

#### 6.3.3 Bool

`Bool` is the type of conditions and of the results of comparisons. It has two values, `false` and `true`. It is not an integer type: an integer does not become a `Bool`, and a `Bool` takes part in no arithmetic.

**Syntax**

```ebnf
bool_literal = "false" | "true" ;
```

The type is written `Bool`, one of the alternatives of `base_type` (A.5).

**Constraints**

1. <a id="IM-10"></a>The condition of `if`, `while` and `for`, the first operand of `?:` and the operands of `&&`, `||` and `!` shall have type `Bool`. An integer in one of these places is diagnosed C2002; a comparison such as `n != 0` gives a `Bool` [IM-10, LS-135].

2. <a id="LS-50"></a>`Bool` is not a numeric type. An arithmetic or bitwise operator with a `Bool` operand is diagnosed C2103. Of the comparisons, only `==` and `!=` apply to `Bool` (6.7.10) [LS-50].

**Semantics**

3. `Bool` has the two values `false` and `true`, written as the literals `false` and `true`. In storage, a `Bool` occupies one byte, which holds 0 for `false` and 1 for `true` [IM-10, LS-50].

4. A `Bool` converts to an integer type with `as`, and no conversion goes from an integer to `Bool`, as 6.6 states.

**Examples**

5. A comparison gives a `Bool`, and `as` turns a `Bool` into 0 or 1:

```cint bool.ci
I64 n = 3;
Bool odd = n % 2 != 0;
if (odd && n > 2) {
    "odd and above 2\n";
}
"{odd} {odd as I64} {(!odd) as U8}\n";
```

```text
odd and above 2
true 1 0
```

6. An integer is not a condition:

```cint bool_condition.ci
I64 n = 3;
if (n) {
    "nonzero\n";
}
```

```text
error[C2002]: condition is not `Bool`
  --> bool_condition.ci:2:5
   |
 2 | if (n) {
   |     ^
```

**Notes**

7. In C, any scalar value is a condition and a comparison gives an `int`. In cint, `if (n)` is diagnosed and a comparison gives a `Bool`.

<a id="array-types"></a>

#### 6.3.4 Array types

An array type is an element type followed by a shape. Declaring arrays, their extents and initializers, indexing, slicing and copying are specified in 6.12.

**Semantics**

1. An array type is an element type followed by a *shape*: a list of *extents* in brackets, one for each dimension. `I64[8]` holds 8 `I64` values, and `I32[2, 3]` holds 2 rows of 3 `I32` values.
2. The number of extents is the array's *rank*, from 1 to 4 (6.12.1). The element type is a scalar type, a struct type, an error set or a handle type.
3. An array holds its elements in row-major order, so the last index varies fastest.

**Examples**

4. This example shows the elements of `I32[2, 3]` in storage order, read through a rank-1 view of the same elements (`reshape`, 7.4):

```cint array_types.ci
void show() {
    I32[2, 3] m = [[1, 2, 3], [4, 5, 6]];
    in I32[_] flat = reshape(m, 6);
    for k in 0..6 {
        if (k > 0) {
            " ";
        }
        "{flat[k]}";
    }
    "\n";
}
show();
```

```text
1 2 3 4 5 6
```

**Notes**

5. In C, `int m[2][3]` is an array of arrays and `m[i][j]` indexes twice. In cint, `I32[2, 3]` is one array of rank 2, and its element is `m[i, j]` (6.12.2).

<a id="view-types"></a>

#### 6.3.5 View types

A view type is an array type with a permission in front of it. Declaring views is specified in 6.9.1, passing them in 6.9.5 and 6.12.4, and taking them by slicing in 6.12.3.

**Syntax**

```ebnf
view_decl = ( "in" | "inout" ) type IDENT "=" expr ";" ;
param     = [ "in" | "inout" ] type IDENT ;
```

View types appear in a `view_decl` (6.9.1) and in a `param` (6.9.5), each with a `type` that has a `shape`.

**Semantics**

1. A view type is an array type with a permission in front of it: `in I64[_]` is a read-only view of rank 1, and `inout I32[_, _]` a view of rank 2 that may also be written. The extent `_` accepts any extent.
2. A view holds no elements of its own: it refers to the elements of an array or of an arena allocation, and reading or writing through it reads or writes those elements.
3. A view may be a local variable, a parameter or an argument, but it cannot be stored in a struct, an array or a module-level variable.

**Examples**

4. This example takes a read-only view of the first three elements and a writable view of the rest. The write through `tail` changes `a`:

```cint view_types.ci
I64 total[n](in I64[n] xs) {
    I64 s = 0;
    for i in 0..n {
        s += xs[i];
    }
    return s;
}
I64 run() {
    I64[5] a = [1, 2, 3, 4, 5];
    in I64[_] head = a[0..3];
    inout I64[_] tail = a[3..];
    tail[0] = 40;
    return total(head) * 1000 + total(a);
}
"{run()}\n";
```

```text
6051
```

<a id="struct-types"></a>

#### 6.3.6 Struct types

A struct type is a named record: a fixed list of fields, each with a name and a type. A struct value holds one value for each field. A struct is declared with `struct` and its values are made with a constructor (6.9.6).

**Constraints**

1. <a id="LS-79"></a>A field shall not have an `Arena` or `Pool` type (C2051), and the extents of an array field shall be constant (C6004). A view type cannot be written as the type of a field (C1050) [LS-79].

2. <a id="LS-313"></a>A struct shall not contain itself, directly or through the fields or elements of its fields: C2055 at the field's name [LS-313].

3. Struct values are not compared: `==`, `!=` and the ordering comparisons with struct operands are diagnosed C2103 [LS-313].

**Semantics**

4. A field may have a scalar type, an array type with constant extents, another struct type, an error set or a `Handle` type [LS-79, LS-314].

5. <a id="LS-78"></a>A struct value is a value, as an integer is. Assignment, initialization and passing as an `in` argument copy every field, the elements of array fields included, and a change made to the copy afterward does not reach the original [LS-78].

**Examples**

6. This example shows a struct with a struct field that holds an array. `b` is a copy of `a`, so the write to `b` leaves `a` unchanged; a struct declared without an initializer has every field 0:

```cint struct_types.ci
struct Vec3 {
    I32[3] c;
}
struct Body {
    I64 id;
    Vec3 pos;
    Bool active;
}
void main() {
    Body a;
    a.id = 7;
    a.pos.c[0] = 1;
    Body b = a;
    b.pos.c[0] = 100;
    "{a.pos.c[0]} {b.pos.c[0]} {b.id} {b.active}\n";
}
```

```text
1 100 7 false
```

7. An arena is not a field:

```cint struct_arena_field.ci
struct Scratch {
    Arena(I64) a;
}
void main() {
    "unreached\n";
}
```

```text
error[C2051]: type rule
  --> struct_arena_field.ci:2:5
   |
 2 |     Arena(I64) a;
   |     ^
```

**Notes**

8. The layout of a struct at the C interface, which a host program sees, is given by SPEC-03P.2.

<a id="error-sets-and-error-unions"></a>

#### 6.3.7 Error sets and error unions

An error set is a type whose values name the expected ways an operation can fail, such as `ParseError.bad_digit`. An error union `E!T` is the result type of a function that returns either a value of type `T` or a value of the error set `E`. Errors are ordinary values that the program handles; they are not faults (5.2.3). Returning, propagating and handling errors with `return`, `try` and `catch` is specified in 6.11.

**Syntax**

```ebnf
error_decl  = "error" IDENT [ ":" INT_TYPE ]
              ( "{" IDENT { "," IDENT } [ "," ] "}"
              | "=" qualified { "|" qualified } ";" ) ;
union_type  = qualified "!" ( "void" | base_type ) ;
qualified   = IDENT [ "." IDENT ] ;
```

A `union_type` appears only as the result type of a function (6.9.4).

**Constraints**

1. The constraints on the underlying type of an error set and on the operands of a combined set (paragraph 9) are stated in 6.9.7.

2. A call of a function whose result is an error union shall be consumed where it is made, as 6.11.2 states (C2040).

3. <a id="LS-97"></a>The short form `.name`, used where the set is known, and the form `C.name`, for a combined set `C`, name a value only when exactly one member set declares `name`. When several do, both forms are diagnosed C2044, and the value is written with the set that declares it, such as `QueueError.full` [LS-97].

4. A value of an error set converts to an error set that holds all of its values: at `try`, at `return`, in an assignment or an initializer, as an argument, and as an operand of `==` or `!=` whose other operand has the larger set. It does not convert the other way: that is diagnosed C2042 at `try` and at `return`, and C2001 elsewhere [LS-97].

5. <a id="LS-91"></a><a id="LS-314"></a>Error-set values are compared only with `==` and `!=`; an ordering comparison is diagnosed C2103. No error value is 0, so an array or struct variable that holds error-set values, directly or inside a field or an element, needs an initializer: a declaration without one is diagnosed C2054. A variable of an error-set type, like every scalar variable, needs one too (C2050, 6.9.1) [LS-91, LS-314].

**Semantics**

6. <a id="LS-92"></a>`error Name { a, b, c }` declares an error set whose values are `Name.a`, `Name.b` and `Name.c`. Where the set is known from the context, a value may be written `.a` [LS-92].

7. <a id="LS-93"></a>The values of a set are numbered from 1 in the order of declaration; the number of a value is its *tag*, and 0 means success. The numbers are stored in the set's underlying type, `U16` unless the declaration names another. Adding a value at the end of a set keeps the numbers of the earlier values. An error value is its number alone and carries no other data [LS-93].

8. What a value of an error union `E!T` holds is stated in 6.11.2.

9. A *combined set*, `error C = A | B;`, or `error C : U32 = A | B;` with a declared underlying type, holds every value of each of its operand sets. Its values are numbered by concatenation: the values of the first operand keep their numbers and the values of the second follow, in their own order; a set that appears twice is included once, at its first position. Each declared set keeps its own numbering, and a value keeps its identity when it converts from one set to another. With `error IoError { closed, full }`, `error ParseError { empty, bad_digit, too_large }` and `error IoOrParse = IoError | ParseError;`, `IoOrParse` numbers `IoError.closed` 1, `IoError.full` 2, `ParseError.empty` 3, `ParseError.bad_digit` 4 and `ParseError.too_large` 5. `ParseError.bad_digit` is one value, which a `ParseError` holds as 2 and an `IoOrParse` as 4 [LS-97].

10. An error-set value is an ordinary value. A variable, a struct field, an array element, a parameter and a function result may have an error-set type. A `switch` over an error-set value without `default` lists every value of the set (6.10.5). Error unions are function results only, so every error is handled or discarded at its call [LS-314].

11. <a id="LS-96"></a>Two error sets are built in. `ArithError`, the set of the result forms and of the conversion `as?` (6.6, 6.7.12), has the four values `overflow`, `div_zero`, `shift` and `narrow`; `AllocError` is the set of `alloc_result` and `insert_result` (6.14) [LS-96].

**Examples**

12. A function that returns a digit or an error, a function whose combined set holds `ParseError`, and the three ways to consume a call:

```cint error_types.ci
error ParseError { empty, bad_digit, too_large }
error IoError : U8 { closed, full }
error IoOrParse = IoError | ParseError;

ParseError!I64 digit(I64 c) {
    if (c < 48 || c > 57) {
        return .bad_digit;
    }
    return c - 48;
}

IoOrParse!I64 twice(I64 c) {
    I64 d = try digit(c);
    return d * 2;
}

void main() {
    I64 a = digit(55) catch -1;
    I64 b = digit(65) catch -1;
    "{a} {b}\n";
    I64 c = twice(52) catch -1;
    "{c}\n";
    I64 d = twice(120) catch (e) {
        switch (e) {
            case IoError.closed, IoError.full: { "io\n"; }
            case ParseError.empty, ParseError.too_large: { "other\n"; }
            case ParseError.bad_digit: { "bad digit\n"; }
        }
        return;
    };
    "{d}\n";
}
```

```text
7 -1
8
bad digit
```

13. This example shows error-set values in an array and in a struct field, each with an initializer:

```cint error_values.ci
error ParseError { empty, bad_digit }
struct Rec {
    I64 n;
    ParseError why;
}
void main() {
    ParseError[2] es = [ParseError.bad_digit, .empty];
    Rec r = Rec(4, .bad_digit);
    "{es[0] == r.why} {es[1] == ParseError.empty}\n";
}
```

```text
true true
```

14. Both sets of `IoOrQueue` declare `full`, so the short form names neither:

```cint error_ambiguous.ci
error IoError { closed, full }
error QueueError { full, stalled }
error IoOrQueue = IoError | QueueError;
IoOrQueue!void push(I64 n) {
    if (n > 3) {
        return .full;
    }
    return;
}
void main() {
    _ = push(5);
}
```

```text
error[C2044]: type rule
  --> error_ambiguous.ci:6:16
   |
 6 |         return .full;
   |                ^
```

15. An array of an error set has no value to start from:

```cint error_zero_fill.ci
error ParseError { empty, bad_digit }
void main() {
    ParseError[3] es;
    "{es[0] == ParseError.empty}\n";
}
```

```text
error[C2054]: type rule
  --> error_zero_fill.ci:3:5
   |
 3 |     ParseError[3] es;
   |     ^
```

**Notes**

16. A caller that needs more detail than an error value carries passes an argument for it, such as an `inout` struct. An error-set type for array elements and fields lets a program record why each of many inputs failed.

<a id="arena-pool-and-handle-types"></a>

#### 6.3.8 Arena, pool and handle types

The arena, pool and handle types name storage that a program sets aside once and reuses. An *arena* is a region of a fixed number of elements of one type, from which runs of elements are taken and then released together. A *pool* is a fixed number of slots of one type, taken and released one at a time. A *handle* is a checked reference into one of them: using a handle whose storage has been released faults instead of reading other data. Their declarations, operations and faults are specified in 6.14.

**Semantics**

1. The type forms are these [LS-50]:

| Form | Names |
|---|---|
| `Arena(T, capacity)` | in a module-level declaration, an arena of `capacity` elements of type `T` |
| `Arena(T)` | the type of an arena parameter or of a child arena |
| `Pool(T, capacity)` | in a module-level declaration, a pool of `capacity` slots of type `T` |
| `Pool(T)` | the type of a pool parameter |
| `Handle(T[])` | a run of elements taken from an arena of `T` |
| `Handle(T)` | one slot of a pool of `T` |

**Examples**

2. This example shows a run of 4 elements from an arena and a slot of a pool:

```cint memory_types.ci
struct Body {
    I64 id;
    I64 mass;
}
Arena(I64, 64) scratch;
Pool(Body, 8) bodies;
void main() {
    Handle(I64[]) h = scratch.alloc(4);
    scratch[h][2] = 7;
    Handle(Body) b = bodies.insert(Body(1, 50));
    bodies[b].mass += 5;
    "{scratch[h][2]} {len(scratch[h])} {bodies[b].mass}\n";
}
```

```text
7 4 55
```

<a id="lexical-elements"></a>

### 6.4 Lexical elements

The source text of a module is read as a sequence of tokens: identifiers, keywords, literals and punctuators, separated by white space and comments. Each lexical subclause below gives its part of the lexical grammar; Annex A collects them [LS-296].

<a id="source-text"></a>

#### 6.4.1 Source text

Source text is UTF-8. Outside literals and comments it is ASCII. A small set of characters that can make text display differently from how it is read is rejected everywhere.

**Constraints**

1. <a id="LS-4"></a>Source text shall be valid UTF-8 without a byte-order mark. A byte-order mark at the start of the file, an invalid UTF-8 sequence, or a byte 0x00 anywhere in the file is C1002 [LS-4].
2. <a id="LS-5"></a>Outside string literals, character literals and comments, only ASCII characters may appear. Any other character is C1013 [LS-5].
3. <a id="LS-6"></a>The following characters shall not appear unescaped anywhere, comments and string literals included. Each is C1003, reported at the character [LS-4, LS-6]:
   - the bidirectional formatting characters U+202A to U+202E and U+2066 to U+2069;
   - the separators U+2028 and U+2029, which some editors display as line breaks;
   - every code point with the Unicode property `Default_Ignorable_Code_Point`, such as U+200B, U+200D, U+2060 and U+FEFF after the start of the file.
4. <a id="LS-7"></a>A control character other than horizontal tab (U+0009), line feed (U+000A), and a carriage return immediately followed by a line feed shall not appear unescaped (C1004) [LS-7].

**Semantics**

5. A character excluded by paragraph 3 may be written in a string literal as an escape, such as `\u{200D}` (6.4.8) [LS-6].

**Examples**

6. The zero-width space U+200B, written as an escape, is three bytes of UTF-8 in the string:

```cint zero_width.ci
I64 size_of[n](in U8[n] s) {
    return n;
}
I64 k = size_of("a\u{200B}b");
"{k}\n";
```

```text
5
```

**Notes**

7. Rejecting the bidirectional and invisible characters rules out the forms of the attack known as Trojan Source (CVE-2021-42574) that rely on those characters, in which code displays differently from how a compiler reads it. Characters that look alike but differ, inside string literals and comments, are not rejected.

<a id="lines-columns-and-white-space"></a>

#### 6.4.2 Lines, columns and white space

A line ends at a line feed, or at a carriage return followed by one. Positions in diagnostics and fault reports count lines and columns from 1.

**Constraints**

1. <a id="LS-9"></a>A line ends at a line feed (LF) or at a carriage return followed by a line feed (CR LF). A carriage return not followed by a line feed is C1004 [LS-9].

**Semantics**

2. <a id="LS-10"></a>Lines are numbered from 1. Columns are numbered from 1 and count Unicode scalar values from the start of the line; a tab counts as one column [LS-10].
3. <a id="LS-11"></a>White space, which is spaces, tabs and line ends, separates tokens and has no other meaning. Indentation has no meaning [LS-11].
4. <a id="LS-12"></a>Tokens are formed by *maximal munch*: at each point, the longest sequence of characters that forms a valid token is taken. `a+%b` is `a`, `+%`, `b`, and `x<<%2` is `x`, `<<%`, `2` [LS-12].

**Examples**

5. In the second line, a tab comes before `U8` and another after `=`. The `+` is at the eleventh column, counting each tab as one, and the caret line repeats the tabs so that the caret stands under the `+` (5.1.3):

```cint tabs.ci
U8 a = 200;
	U8 b =	a + a;
"{b}\n";
```

```text
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> tabs.ci:2:11
   |
 2 | 	U8 b =	a + a;
   | 	      	  ^ add.checked.u8
   ...
```

6. Maximal munch reads `a--b` as `a`, `--`, `b`, and `--` cannot appear in an expression. With a space, `a - -b` subtracts a negated `b`:

```cint munch.ci
I64 a = 5;
I64 b = 2;
I64 c = a - -b;
I64 d = a--b;
"{c} {d}\n";
```

```text
error[C1042]: `++` or `--` used as an expression
  --> munch.ci:4:10
   |
 4 | I64 d = a--b;
   |          ^
```

<a id="comments"></a>

#### 6.4.3 Comments

A comment is text for the reader, and counts as white space. A line comment runs to the end of its line; a block comment runs to its matching end, and block comments nest.

**Syntax**

```ebnf
LINE_COMMENT  = "//" { line_char } ;
BLOCK_COMMENT = "/*" { BLOCK_COMMENT | any_char } "*/" ;
line_char     = (* any Unicode scalar value except a line end *) ;
any_char      = (* any Unicode scalar value *) ;
```

**Constraints**

1. <a id="LS-14"></a>A block comment without its matching `*/` is C1005, reported at its `/*` [LS-14].

**Semantics**

2. <a id="LS-13"></a>`//` begins a comment that ends at the end of the line. `/*` begins a comment that ends at the matching `*/` [LS-13].
3. Block comments nest: `/* a /* b */ c */` is one comment [LS-14].
4. `///` begins a line comment like `//` [LS-14].

**Examples**

5. This example shows comments of each kind:

```cint comments.ci
/* outer /* inner */ still a comment */
// line comment
I64 x = 7 /* inside an expression */ + 1; // after a statement
/// Doubles a value.
I64 twice(I64 v) {
    return 2 * v;
}
"{twice(x)}\n";
```

```text
16
```

**Notes**

6. A comment that begins with `///` is a documentation comment, which a tool may attach to the declaration that follows it.
7. In C, block comments do not nest, so commenting out code that holds a comment ends the comment early. In cint the outer comment continues to its own `*/`.

<a id="identifiers-and-keywords"></a>

#### 6.4.4 Identifiers and keywords

An identifier names something the program declares. Keywords are reserved and cannot be names. The names of the built-in types and functions are not keywords, but they are declared already and cannot be declared again.

**Syntax**

```ebnf
letter = "A" .. "Z" | "a" .. "z" | "_" ;
digit  = "0" .. "9" ;
IDENT  = letter { letter | digit } ;    (* not a KEYWORD; at most 255 bytes *)
```

**Constraints**

1. <a id="LS-15"></a>An identifier is a letter or `_` followed by letters, digits and `_`. Identifiers are case-sensitive and at most 255 bytes long; a longer identifier is C1012 on every implementation [LS-15].
2. <a id="LS-17"></a>A name that is a capital `I`, `U`, `Q` or `T` followed only by digits is reserved for type names, whether or not a type of that name exists, and is C1010 when declared: `Q32` is not a valid variable name [LS-17].
3. <a id="LS-18"></a><a id="LS-19"></a>These words are keywords and shall not be used as identifiers. Where a name is expected, a keyword is C4012 [LS-18, LS-19]:

```text
as          assert      break       by          case        catch
const       continue    default     defer       distinct    do
else        enum        errdefer    error       expect_fault
export      extern      fallthrough false       for         if
import      in          in_place    inout       kernel      out
over        profile     reduce      requires    return      schedule
secret      static_assert           struct      switch      test
true        try         type        var         void        where
while
```

   No construct of this edition uses eight of these keywords; they are reserved, and A.2, paragraph 7 names them.

4. <a id="LS-22"></a>The names of the built-in functions, such as `len`, `min`, `sum` and `count`, are not keywords, but they are visible in every module without an import, as if declared before its first line, and shall not be declared again (C3001). 7.1 lists them [LS-22].
5. <a id="LS-23"></a><a id="LS-297"></a>A fault name, such as `E_OVERFLOW` in a test header (6.15.3), is an identifier checked against the fault codes of 5.2.3. A name that begins `E_` and is not one of them is C1011 [LS-23, LS-297].

**Semantics**

6. <a id="LS-20"></a>`round` and `at` are identifiers that the grammar matches by spelling in the positions where it names them: `round` right after the target type of a conversion (`x as I32 round floor`) and right after a fraction literal (`0.1 round half_even`), and `at` in a test header (6.15.3). So are the effect classes `pure`, `observe` and `unrestricted` right after `extern` (6.9.8), and the argument kinds `path` and `name` in a `@service` attribute (6.9.9). Elsewhere each is an ordinary identifier [LS-20, LS-297].
7. <a id="LS-21"></a>The built-in type names are `I8`, `I16`, `I32`, `I64`, `U8`, `U16`, `U32`, `U64`, `I128`, `I256`, `I512`, `I1024`, `U128`, `U256`, `U512`, every fixed-point name `Q<i>.<f>`, `Bool`, `Arena`, `Pool` and `Handle`, which 6.3 defines, and `Grants`, which 6.9.9 defines. The built-in error set is `ArithError` (6.3.7). The name `Str` is reserved and names no type in this edition [LS-21].
8. Every built-in type name starts with a capital letter [LS-21].

**Examples**

9. Identifiers are case-sensitive, and `round` is an ordinary name outside a rounding clause:

```cint names.ci
I64 count2 = 2;
I64 Count2 = 3;
I64 _tmp = 4;
I64 round = 5;
"{count2} {Count2} {_tmp} {round}\n";
```

```text
2 3 4 5
```

10. This example shows a name of the reserved form:

```cint reserved_q.ci
I64 Q32 = 1;
"{Q32}\n";
```

```text
error[C1010]: invalid name
  --> reserved_q.ci:1:5
   |
 1 | I64 Q32 = 1;
   |     ^
```

11. This example shows a keyword where a name is expected:

```cint keyword_name.ci
I64 kernel = 1;
"{kernel}\n";
```

```text
error[C4012]: statement form not permitted
  --> keyword_name.ci:1:1
   |
 1 | I64 kernel = 1;
   | ^
```

12. A built-in function name cannot be declared again:

```cint builtin_name.ci
I64 count = 3;
"{count}\n";
```

```text
error[C3001]: duplicate name
  --> builtin_name.ci:1:5
   |
 1 | I64 count = 3;
   |     ^
```

13. This example shows a misspelled fault name:

```cint fault_name.ci
test "bad name" expect_fault E_OVERFLW {
    I8 a = 127;
    a += 1;
}
"x\n";
```

```text
error[C1011]: invalid name
  --> fault_name.ci:1:30
   |
 1 | test "bad name" expect_fault E_OVERFLW {
   |                              ^
```

**Notes**

14. Identifiers are ASCII only. C and C++ admit other characters in identifiers; cint does not, which keeps out the names that look alike because of characters outside ASCII.
15. The examples of this document name declared types with a capital letter too, such as a struct `Body` or an error set `IoError`; the grammar does not require it.

<a id="integer-literals"></a>

#### 6.4.5 Integer literals

An integer literal writes an exact integer in decimal, hexadecimal, binary, octal or balanced ternary. It has no type of its own: it takes one from where it is used (6.5). A minus sign written before a literal is part of the literal, so `-128` is one value, not the negation of 128.

**Syntax**

```ebnf
INT_LIT     = DEC_INT | HEX_INT | OCT_INT | BIN_INT | TERN_INT ;
DEC_INT     = "0" | nonzero { [ sep ] digit } ;
HEX_INT     = "0x" hexdigit { [ sep ] hexdigit } ;
OCT_INT     = "0o" octdigit { [ sep ] octdigit } ;
BIN_INT     = "0b" bindigit { [ sep ] bindigit } ;
TERN_INT    = "0t" terndigit { [ sep ] terndigit } ;
nonzero     = "1" .. "9" ;
hexdigit    = digit | "a" .. "f" | "A" .. "F" ;
octdigit    = "0" .. "7" ;
bindigit    = "0" | "1" ;
terndigit   = "N" | "0" | "P" ;
sep         = "_" ;                                (* only between two digits *)
neg_literal = "-" ( INT_LIT | CHAR_LIT | FRAC_LIT [ round_clause ] ) ;
```

`CHAR_LIT` is defined in 6.4.7, `FRAC_LIT` in 6.4.6 and `round_clause` in 6.7.15.

**Constraints**

1. <a id="LS-25"></a>The prefix letter is lower case: `0X1F` is C1020 [LS-25].
2. <a id="LS-26"></a>A single `_` may stand between two digits. It shall not lead, trail, follow the prefix or repeat: `1__0`, `1_` and `0x_1F` are C1021, and `_1` is an identifier [LS-26].
3. <a id="LS-27"></a>A decimal literal of more than one digit shall not begin with `0`: `007` is C1022. A lone `0` is decimal [LS-27].
4. A prefix with no digits after it, or a digit outside the prefix's radix, such as `0x`, `0b102` or `0o8`, is C1024.
5. <a id="LS-31"></a>The magnitude of a literal shall be at most `2^4096 - 1`. A larger literal is C1023, reported at its first character, on every implementation; it is never truncated [LS-31].
6. <a id="LS-29"></a>There are no type suffixes [LS-29].

**Semantics**

7. <a id="LS-24"></a><a id="IM-19"></a>The forms and their values are these [LS-24, IM-19]:

| Form | Digits | Example | Value |
|---|---|---|---|
| Decimal | `0`, or a nonzero digit and then digits | `1_000_000` | 1000000 |
| Hexadecimal | `0x`, then digits `0` to `9` and `a` to `f` in either case | `0xDEAD_beef` | 3735928559 |
| Binary | `0b`, then `0` and `1` | `0b1010_0101` | 165 |
| Octal | `0o`, then `0` to `7` | `0o755` | 493 |
| Balanced ternary | `0t`, then `N`, `0` and `P` | `0tP0N` | 8 |

8. A literal denotes a value in Z, never a bit pattern: `0xFFFFFFFF` is 4294967295 wherever it appears. It takes its type from its context and shall fit that type (6.5) [IM-20, LS-29].
9. <a id="LS-30"></a>A balanced-ternary literal lists its digits most significant first, with `N` for -1, `0` for 0 and `P` for +1, so `0tP0N` is 9 - 1 = 8 and `0tN` is -1. It is an ordinary integer literal, typed by context like the others [LS-30].
10. <a id="LS-28"></a><a id="IM-25"></a>A `-` in operand position followed by an integer, character or fraction literal forms one *negative literal*, whatever white space or comments stand between the two. `-128`, `- 128` and `-/* c */128` are each the value -128, which fits `I8`, and `-0.5` is the value -1/2. A `round` clause (6.7.15) after a negative fraction literal rounds the negative value (6.5): in the fixed-point type `Q8.8` (6.3.2), `-0.1 round floor` is -26/256 and `-0.1 round ceil` is -25/256. A `-` before anything else, including a parenthesized literal or another negative literal, is the negation operator: `-(128)` and `- -128` are negations (6.7.5) [LS-28, IM-25].

**Examples**

11. This example shows each form, and a negative balanced-ternary literal:

```cint int_forms.ci
I64 d = 1_000_000;
I64 h = 0xDEAD_beef;
I64 b = 0b1010_0101;
I64 o = 0o755;
I64 t = 0tP0N;
I64 n = -0tP;
"{d} {h} {b} {o} {t} {n}\n";
```

```text
1000000 3735928559 165 493 8 -1
```

12. A negative literal, with or without space after the minus sign, holds the least `I8` and `I64` values:

```cint negative.ci
I8 a = -128;
I8 b = - 128;
I8 c = -/* c */128;
I64 m = -9223372036854775808;
"{a} {b} {c} {m}\n";
```

```text
-128 -128 -128 -9223372036854775808
```

13. Parentheses make `128` a literal of its own, typed `I8` before the negation applies, and 128 does not fit `I8`:

```cint negated.ci
I8 a = -(128);
"{a}\n";
```

```text
error[C2003]: literal does not fit its type
  --> negated.ci:1:10
   |
 1 | I8 a = -(128);
   |          ^
```

14. This example shows a decimal literal with a leading zero:

```cint leading_zero.ci
I64 mode = 0755;
"{mode}\n";
```

```text
error[C1022]: invalid integer literal
  --> leading_zero.ci:1:12
   |
 1 | I64 mode = 0755;
   |            ^
```

**Notes**

15. In C, `0755` is octal and means 493. cint rejects it, and the octal value is written `0o755`.
16. In C, `-2147483648` is the negation of `2147483648`, which does not fit `int`, so the least `int` cannot be written as a literal. In cint, `-2147483648` is one literal, of value `MIN(I32)` in context `I32`.

<a id="fraction-literals"></a>

#### 6.4.6 Fraction literals

A fraction literal writes an exact rational number with digits on both sides of a point, such as `1.5`. It is used for the fixed-point types of 6.3.2, never for an integer type.

**Syntax**

```ebnf
FRAC_LIT = DEC_FRAC | HEX_FRAC ;
DEC_FRAC = DEC_INT "." digit { [ sep ] digit } ;
HEX_FRAC = HEX_INT "." hexdigit { [ sep ] hexdigit } ;
```

**Constraints**

1. <a id="LS-33"></a>A fraction literal has at least one digit on each side of the point: `1.` and `.5` are not literals. Digit separators follow 6.4.5 on each side [LS-33].
2. <a id="LS-35"></a>A fraction literal may take only a fixed-point type. In any other context it is C2004: `I64 x = 1.5;` is rejected [LS-35].

**Semantics**

3. A decimal fraction literal denotes the exact rational its digits write: `0.1` is 1/10. A hexadecimal fraction literal is read in radix 16 on both sides: `0x1.8` is 3/2 [LS-33, LS-35].
4. <a id="LS-34"></a>Because a fraction needs a digit after the point, `1..5` is the three tokens `1`, `..` and `5`, and `0x1..5` is `0x1`, `..` and `5` [LS-34].
5. How a fraction literal takes a fixed-point type, and when it must be followed by a `round` clause, is stated in 6.5.

**Examples**

6. This example shows fraction literals in fixed-point contexts. The second line prints each storage integer (6.3.2):

```cint fractions.ci
Q16.16 half = 0.5;
Q16.16 h2 = 0x1.8;
Q8.0 whole = 100.0;
Q16.16 grouped = 1_000.000_5 round half_even;
"{half} {h2} {whole} {grouped}\n";
"{half.raw} {h2.raw} {whole.raw} {grouped.raw}\n";
```

```text
0.5 1.5 100.0 1000.0005035400390625
32768 98304 100 65536033
```

7. A fraction literal cannot initialize an integer:

```cint fraction_int.ci
I64 x = 1.5;
"{x}\n";
```

```text
error[C2004]: a fraction literal cannot have the integer type I64
...
```

8. A range of integers is not a fraction:

```cint range_dots.ci
for i in 1..5 {
    "{i} ";
}
"\n";
```

```text
1 2 3 4 
```

<a id="character-literals"></a>

#### 6.4.7 Character literals

A character literal writes one Unicode character between single quotes and means its code point, as an integer: `'A'` is 65.

**Syntax**

```ebnf
CHAR_LIT  = "'" ( char_char | escape ) "'" ;
char_char = (* any Unicode scalar value except "'", "\" and a line end *) ;
```

`escape` is defined in 6.4.8.

**Constraints**

1. <a id="LS-39"></a>A character literal holds exactly one Unicode scalar value or one escape: `''` and `'ab'` are C1030 [LS-39].
2. <a id="IM-20"></a>The escapes are those of string literals (6.4.8), and `\xHH` is limited to 00 to 7F as in a string: `'\x41'` is 65 and `'\xff'` is C1032. An escape `\u{...}` whose value is a surrogate, U+D800 to U+DFFF, or above U+10FFFF is C1031 [LS-39, IM-20].
3. The value shall fit the type the literal takes, as 6.5 states: `U8 c = '\u{100}';` is C2003.

**Semantics**

4. A character literal is an integer literal equal to the code point of its character: `'A'` is 65, `'é'` is 233 and `'\u{1F600}'` is 128512. It has no type of its own and takes one from its context like any integer literal (6.5) [LS-39, IM-20].

**Examples**

5. This example shows character literals and escapes:

```cint chars.ci
I64 a = 'A';
I64 e = 'é';
I64 s = '\u{1F600}';
I64 n = '\n';
I64 q = '\'';
I64 x = '\x41';
U8 z = '\0';
"{a} {e} {s} {n} {q} {x} {z}\n";
```

```text
65 233 128512 10 39 65 0
```

6. A character literal is valid in a case range:

```cint char_case.ci
U8 c = 'q';
switch (c) {
    case 'a'..='z': { "lower\n"; }
    default: { "other\n"; }
}
```

```text
lower
```

<a id="string-literals"></a>

#### 6.4.8 String literals

A string literal writes a sequence of bytes, the UTF-8 encoding of its text with each escape replaced. It is what a print statement writes, the message of an `assert`, the name of a test and the text passed to a function that reads bytes.

**Syntax**

```ebnf
STRING   = '"' { str_char | escape | "{{" | "}}" | hole } '"' ;
str_char = (* any Unicode scalar value except '"', "\", "{", "}" and a line end *) ;
escape   = "\" ( "\" | '"' | "'" | "n" | "r" | "t" | "0"
               | "x" octdigit hexdigit
               | "u{" hexdigit [ hexdigit [ hexdigit [ hexdigit [ hexdigit [ hexdigit ] ] ] ] ] "}" ) ;
```

`hole` is defined in 7.5. A hole `{expr}` prints the value of `expr`.

**Constraints**

1. <a id="LS-42"></a><a id="LS-43"></a>The escapes are these, and any other backslash sequence is C1032, reported at the backslash [LS-42, LS-43]:

| Escape | Bytes |
|---|---|
| `\\`, `\"`, `\'` | the character after the backslash |
| `\n`, `\r`, `\t`, `\0` | 0x0A, 0x0D, 0x09, 0x00 |
| `\xHH` | the one byte `HH`, from 00 to 7F |
| `\u{H...}`, with 1 to 6 hexadecimal digits | the UTF-8 encoding of the Unicode scalar value; a surrogate, U+D800 to U+DFFF, or a value above U+10FFFF is C1031 |

2. <a id="LS-44"></a>A literal `{` or `}` is written `{{` or `}}` in every string literal. A single `{` begins a hole, an expression whose value is written in its place; a single `}` without its `{` is C1039 [LS-44].
3. Holes are permitted only in print statements and in the message of an `assert`. A hole anywhere else, such as in the name of a test or in a literal passed as an argument, is C1033 [LS-44].
4. <a id="LS-46"></a>A string literal shall not contain an unescaped line end (C1034) [LS-46].

**Semantics**

5. <a id="LS-40"></a>A string literal denotes the bytes of its text in UTF-8, with each escape replaced by its bytes and each `{{` or `}}` by one brace. There is no terminating byte: `"a\0b"` is three bytes, the middle one 0x00 [LS-40, LS-42].
6. <a id="LS-45"></a>Adjacent string literals are joined during translation: `"ab" "cd"` is `"abcd"` [LS-45].
7. The bytes of a string literal are storage that lasts for the whole run and cannot be written [LS-40].
8. A string literal passed as the argument for a parameter `in U8[n]` is a read-only view of its bytes, whose extent `n` is the number of bytes [LS-40].
9. `len` of a string literal is its number of bytes: `len("abc")` is 3 and `len("é")` is 2 [LS-40].

**Examples**

10. This example shows escapes and doubled braces in a print statement:

```cint escapes.ci
"tab[\t] quote[\"] backslash[\\] braces[{{}}] hex[\x41] smile[\u{1F600}]\n";
```

```text
tab[	] quote["] backslash[\] braces[{}] hex[A] smile[😀]
```

11. This example shows string literals as arguments. Adjacent literals are joined, and the extent is the number of bytes:

```cint string_args.ci
I64 count_a[n](in U8[n] s) {
    I64 k = 0;
    for i in 0..n {
        if (s[i] == 'a') {
            k += 1;
        }
    }
    return k;
}
I64 size_of[n](in U8[n] s) {
    return n;
}
I64 a = count_a("banana");
I64 b = count_a("a\0b");
I64 c = size_of("ab" "cd");
I64 d = size_of("é");
"{a} {b} {c} {d}\n";
```

```text
3 1 4 2
```

12. This example shows `len` of a string literal:

```cint string_len.ci
I64 n = len("abc");
I64 m = len("é");
"{n} {m}\n";
```

```text
3 2
```

13. A hole is not permitted in the name of a test:

```cint hole_name.ci
I64 x = 1;
test "value {x}" {
    assert(1 == 1);
}
"x\n";
```

```text
error[C1033]: invalid character or string literal
  --> hole_name.ci:2:13
   |
 2 | test "value {x}" {
   |             ^
```

14. This example shows an unknown escape:

```cint bad_escape.ci
"bad \q escape\n";
```

```text
error[C1032]: invalid character or string literal
  --> bad_escape.ci:1:6
   |
 1 | "bad \q escape\n";
   |      ^
```

**Notes**

15. A string literal in C ends with a hidden zero byte, and a zero byte inside it ends it early for most library functions. A cint string literal has no terminator, and a zero byte is an ordinary byte.

<a id="punctuators"></a>

#### 6.4.9 Punctuators

Punctuators are the operators and separators of the language. Several operators come in three spellings that differ in what happens when the exact result is out of range: the plain spelling faults, the spelling with `%` wraps and the spelling with `|` saturates.

**Semantics**

1. <a id="LS-48"></a>The punctuators are [LS-48]:

```text
+   -   *   /   %   <<  >>          checked arithmetic and shifts
+%  -%  *%  <<%                     wrapping forms
+|  -|  *|                          saturating forms
&   |   ^   ~                       bitwise
==  !=  <   <=  >   >=              comparison
&&  ||  !                           logical
?   :                               conditional expression
=   +=  -=  *=  /=  %=  <<=  >>=  &=  |=  ^=
+%= -%= *%= <<%= +|= -|= *|=        assignment
++  --                              increment and decrement statements
as%  as|  as?                       conversions
..  ..=                             ranges
(   )   [   ]   {   }   ,   ;   .   @
!                                   also in error-union types
```

2. <a id="LS-49"></a>`as%`, `as|` and `as?` are single tokens with no space inside: `a as % U8` is a syntax error (C1050) [LS-49]. The word `as` itself is a keyword (6.4.4). The punctuator `@` begins an attribute, which only an extern declaration takes (6.9.9).

3. Each operator's meaning, and the faults it can raise, are in 6.7; the conversions are in 6.6.

**Examples**

4. Without spaces, maximal munch (6.4.2) finds the wrapping and saturating operators:

```cint punct.ci
I64 a = I64.max;
I64 b = 1;
I64 c = a+%b;
I64 d = b<<%63;
U8 e = 250;
U8 f = e+|10;
"{c} {d} {f}\n";
```

```text
-9223372036854775808 -9223372036854775808 255
```

<a id="literals-and-context-typing"></a>

### 6.5 Literals and context typing

A literal has no type of its own. It takes the type its context gives it, such as the type of the variable it initializes or of the other operand of an operator, and it must fit that type exactly. There is no implicit conversion between types afterward, except from an error set to a larger one (6.3.7, 6.6), so a literal's context decides how the arithmetic around it is done.

**Constraints**

1. <a id="LS-53"></a><a id="LS-58"></a>An integer, character or fraction literal shall be exactly representable in the type its context gives it. An integer or character literal that does not fit is C2003, reported at the literal, inside any parentheses or at the `-` of a negative literal [LS-53, IM-20]. Representability is a property of the literal alone: with `I8 x`, `x + 300` is C2003, while `x + 100` is valid and its addition is checked when it runs [LS-58].
2. <a id="LS-54"></a>An operator expression whose operands are all literals is evaluated during translation with its run-time meaning; a fault there is C6001 (6.8) [LS-54].
3. <a id="LS-60"></a>A context-typed error value, such as `.overdrawn`, needs a context that gives its error set; with none it is C2007 [LS-60].

**Semantics**

4. <a id="LS-56"></a><a id="IM-21"></a>A literal takes its type from the first of these rules that applies [LS-56, IM-21]:
   1. the declared type of the variable, constant, field or element it initializes or is assigned to;
   2. the parameter type of the argument it gives, in a call of a user function;
   3. the result type of the function, in a `return` statement;
   4. the type of the other operand of a binary arithmetic, bitwise or comparison operator, when that operand is typed, except that this rule never types a shift count (paragraph 7);
   5. the element type, in an array literal whose element type is declared;
   6. the type of the value a `switch` tests, for a `case` item;
   7. for an argument of a built-in function whose arguments share one type, such as `min`, `max` or `clamp`, the type of its typed arguments; when none is typed, the declared type that the call's result initializes;
   8. otherwise `I64`.
5. <a id="IM-22"></a>An operator expression whose operands are all literals has no type of its own either: the context's type passes to each operand. The expression is then evaluated with the meaning it would have at run time, checked, wrapping or saturating as written; it is never evaluated in Z as a whole [LS-54, IM-22].
6. <a id="LS-55"></a><a id="IM-24"></a>A named constant is not a literal. Every `const` declaration names its type, and a constant is an ordinary typed operand that never takes its type from its context (6.9.2) [LS-55, IM-24].
7. <a id="LS-57"></a>The count of `<<`, `<<%` and `>>` may have any integer type and is compared as a value in Z (6.7.8). A literal count has type `I64`. The left operand of a shift is typed by the context of the whole shift, by rules 1 to 3 and 5 to 8, never by rule 4 from the operand of an enclosing operator and never by the count [LS-57, IM-22].
8. <a id="LS-59"></a>A literal operand of a comparison takes the type of the other operand, so it must fit that type: with `I8 x`, `x < 1000` is C2003, and `(x as I16) < 1000` is valid [LS-59].
9. A *literal operand* is a literal, a negative literal (6.4.5) or a literal operand in parentheses. As the source of a conversion `as`, `as%`, `as|` or `as?`, a literal operand of an integer or character literal is converted from its value in Z, and any other source made only of literals is typed `I64` first (6.6, paragraph 14) [LS-54].
10. <a id="IM-27"></a><a id="LS-37"></a>In a fixed-point context `Q<i>.<f>`, a decimal, hexadecimal or integer literal denotes its exact rational value `v`. When `v * 2^f` is an integer within the range of the storage type, that integer is the stored value. When it is an integer outside that range the literal is C2003; when it is not an integer, the literal is C2006 unless a `round` clause follows it, which rounds the value once to a multiple of `2^-f` with the named mode before the range is checked (6.3.2, 6.7.15) [IM-27, LS-37, LS-53].
11. An error value written `.name`, such as `.overdrawn`, takes its error set from its context, by the rules of paragraph 4 [LS-60].

**Examples**

12. Literals typed by declarations and by the other operand, with wrapping forms and floor division:

```cint contexts.ci
const I64 LIMIT = 1 << 40;
I16 m = -32768;
I8 p = 100 +% 100;
U8 s = 200 +% 100;
I64 g = 3 * 1_000_000_000_000 / 7;
I64 fl = 7 / 2;
I64 neg = -7 / 2;
"{LIMIT} {m} {p} {s} {g} {fl} {neg}\n";
```

```text
1099511627776 -32768 -56 44 428571428571 3 -4
```

13. A literal that does not fit the type of the other operand is rejected at the literal; one that fits is checked when the operation runs:

```cint operand_fit.ci
I8 x = 0;
I8 y = x + 300;
"{y}\n";
```

```text
error[C2003]: literal does not fit its type
  --> operand_fit.ci:2:12
   |
 2 | I8 y = x + 300;
   |            ^
```

```cint operand_run.ci
I8 x = 100;
I8 y = x + 100;
"{y}\n";
```

```text
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> operand_run.ci:2:10
   |
 2 | I8 y = x + 100;
   |          ^ add.checked.i8
   ...
```

14. A literal-only expression is typed by its context and evaluated in that type. Here `40000` does not fit `I16`, and `2_000_000_000 * 2` overflows `I32` during translation:

```cint literal_only.ci
I16 h = 40000 - 30000;
"{h}\n";
```

```text
error[C2003]: literal does not fit its type
  --> literal_only.ci:1:9
   |
 1 | I16 h = 40000 - 30000;
   |         ^
```

```cint literal_fault.ci
I32 v = 2_000_000_000 * 2 / 4;
"{v}\n";
```

```text
error[C6001]: fault during constant evaluation
  --> literal_fault.ci:1:23
   |
 1 | I32 v = 2_000_000_000 * 2 / 4;
   |                       ^
   ...
   = operation: mul.checked.i32
   = operand-count: 2
   = operand: I32 2000000000
   = operand: I32 2
   = exact:   4000000000
   = limit:   I32 2147483647
   ...
```

15. A constant has its declared type and does not adapt to its context:

```cint typed_const.ci
const I64 LIMIT = 1 << 40;
I32 small = LIMIT;
"{small}\n";
```

```text
error[C2001]: type mismatch
  --> typed_const.ci:2:13
   |
 2 | I32 small = LIMIT;
   |             ^
```

16. The left operand of a shift takes the type of the whole shift's context, not of the count. With an `I8` variable, `x + (1 << 6)` mixes `I8` with an `I64` shift:

```cint shift_type.ci
U8 k = 40;
I64 v = 1 << k;
"{v}\n";
```

```text
1099511627776
```

```cint shift_mix.ci
I8 x = 0;
I8 y = x + (1 << 6);
"{y}\n";
```

```text
error[C2001]: type mismatch
  --> shift_mix.ci:2:12
   |
 2 | I8 y = x + (1 << 6);
   |            ^
```

17. A literal argument of `clamp` with no typed argument takes the type the result initializes, `U8` here, and 300 does not fit it:

```cint builtin_args.ci
I8 x = 100;
I8 a = min(x, 50);
I8 b = max(3, 4);
"{a} {b}\n";
U8 c = clamp(300, 0, 255);
```

```text
error[C2003]: literal does not fit its type
  --> builtin_args.ci:5:14
   |
 5 | U8 c = clamp(300, 0, 255);
   |              ^
```

18. A literal operand is converted from its value in Z; any other literal-only source is typed `I64` first:

```cint literal_convert.ci
U64 all = -1 as% U64;
U64 top = (18446744073709551615) as U64;
I8 low = 300 as% I8;
U64 q = (5 - 6) as% U64;
"{all} {top} {low} {q}\n";
```

```text
18446744073709551615 18446744073709551615 44 18446744073709551615
```

19. This example shows fixed-point contexts. `0.1` is not a multiple of `2^-16`, so it needs a `round` clause; a quotient of literals is evaluated with fixed-point division (6.7.7):

```cint fixed_context.ci
Q16.16 two = 2;
Q16.16 t2 = 0.1 round half_even;
Q16.16 third = 1.0 / 3.0;
Q16.16 mix = 1 / 3.0;
"{two.raw} {t2.raw} {third.raw} {mix.raw}\n";
```

```text
131072 6554 21845 21845
```

```cint fixed_inexact.ci
Q16.16 tenth = 0.1;
"{tenth}\n";
```

```text
error[C2006]: 0.1 is not a multiple of 2^-16, the resolution of Q16.16
...
```

20. This example shows an error value with no context:

```cint error_context.ci
error Ledger { overdrawn, closed }
Bool b = .overdrawn == .closed;
"{b}\n";
```

```text
error[C2007]: fraction, fixed-point, enum literal, or rounding rule
  --> error_context.ci:2:10
   |
 2 | Bool b = .overdrawn == .closed;
   |          ^
```

**Notes**

21. C gives an integer constant a type from its value and suffix and then converts operands by the usual arithmetic conversions. cint does neither: a literal takes the type of its context, and operands of different types are rejected (C2001) unless converted with `as` (6.6).

<a id="conversions"></a>

### 6.6 Conversions

A value changes type only through a conversion that the program writes. `x as T` converts `x` to the type `T` and faults when the value does not fit; `x as% T` wraps the value into `T`; `x as| T` clamps it to the range of `T`; `x as? T` returns an error instead of faulting. There are no implicit conversions: not in arithmetic, not in assignment, and not from a narrower integer type to a wider one. The one exception is a value of an error set, which converts to a larger set (6.3.7). A conversion that changes a fixed-point value's resolution or makes it an integer rounds once, by the mode its `round` clause names, or by `half_even` when it has none.

**Syntax**

```ebnf
conv_expr    = unary_expr { conv_op type [ round_clause ] } ;
conv_op      = "as" | "as%" | "as|" | "as?" ;
round_clause = "round" IDENT ;
```

1. `as%`, `as|` and `as?` are single tokens (6.4.9). The `IDENT` after `round` is the name of a rounding mode (6.7.15).

**Constraints**

2. <a id="IM-50"></a><a id="LS-134"></a>A value of one numeric type shall not be used where another type is required. An operand, an initializer, an argument or a returned value of the wrong type is diagnosed C2001, even when every value of its type fits the required type: with `I32 a` and `I64 b`, `b + a` is diagnosed, and `b + (a as I64)` is valid [IM-9, IM-50, LS-134].

3. <a id="LS-135"></a>A conversion from `Bool` shall be written with `as`, and its target shall be an integer type: `as%`, `as|` and `as?` from a `Bool`, and a conversion in either direction between `Bool` and a fixed-point type, are diagnosed C2008. A conversion whose source is a struct is diagnosed C2008 as well [LS-135].

4. There is no conversion from an integer to `Bool`: `n as Bool` is diagnosed C2056, and a comparison such as `n != 0` gives a `Bool` [IM-10, LS-135].

5. <a id="IM-69"></a><a id="LS-133"></a>A `round` clause is permitted only on a conversion that rounds: from a fixed-point type to an integer type, or to a fixed-point type with fewer fraction bits (paragraphs 11 and 12). On any other conversion it is diagnosed C2009. The name after `round` shall be a rounding mode (C3007) [IM-69, LS-133].

6. <a id="LS-136"></a>A conversion binds tighter than every binary operator, and conversions group from left to right: `a as I64 + b` is `(a as I64) + b`, and `x as I32 as% I16` converts twice. A unary operator directly before a conversion needs parentheses (6.7.2) [LS-136].

**Semantics**

7. Each conversion has three forms. For a source value `x`, a target type `T` and the value `v` that paragraphs 9 to 13 define [IM-50, LS-133]:
   - `x as T`, the *checked conversion*, gives `v` when `in(T, v)` and otherwise faults;
   - `x as% T`, the *wrapping conversion*, gives `wrap(T, v)` for an integer `T`, and for a fixed-point `T` the value whose raw value is `wrap(S, v)`, where `S` is the storage type of `T` and `v` the raw value the conversion computes;
   - `x as| T`, the *saturating conversion*, gives `sat(T, v)` for an integer `T`, and for a fixed-point `T` the value whose raw value is `sat(S, v)`: the value of `T` nearest to `v`, after any rounding;
   - `x as? T`, the *result-returning conversion*, gives a value of type `ArithError!T`: `v` where `x as T` would give it, and `ArithError.narrow` where `x as T` would fault. It is consumed with `try`, `catch` or `_ =` (6.11).

8. A conversion to a type whose range holds every value of the source type, such as `I32` to `I64` or `U8` to `I16`, gives the source value and never faults. Between two integer types of the same width, `as%` keeps the bit pattern: `-1` as `I8` becomes 255 as `U8` [IM-50].

9. From an integer type to an integer type, `v` is the value of `x` [IM-50].

10. <a id="IM-68"></a>From an integer type to `Q<i>.<f>`, the conversion is exact: `v` is the raw value `x * 2^f`, and the checked conversion faults when it is outside the storage type [IM-68].

11. From `Q<i>.<f>` to `Q<j>.<g>`: when `g >= f`, `v` is the raw value of `x` times 2^(g - f), exact; when `g < f`, `v` is the raw value of `x` divided by 2^(f - g), rounded once by the mode of the `round` clause, `half_even` when there is none. The range check or the wrap applies to `v` after the rounding [IM-68, IM-69].

12. From `Q<i>.<f>` to an integer type, `v` is the value of `x` rounded once to an integer by the mode of the `round` clause, `half_even` when there is none. The range check or the wrap applies to `v` after the rounding [IM-68, IM-69].

13. From `Bool` to an integer type, `v` is 0 for `false` and 1 for `true` [IM-10, LS-135].

14. <a id="IM-26"></a>When the source of a conversion is a literal operand (6.5) of an integer or character literal, the conversion applies to the literal's value in Z, which has no type: `(18446744073709551615) as U64` is valid, and `-1 as% U64` is 18446744073709551615. Any other source whose operands are all literals is typed `I64` first (6.5): `(200 + 100) as% U8` converts the `I64` value 300 [IM-26].

15. <a id="IM-57"></a><a id="LS-38"></a>`x.raw`, for `x` of `Q<i>.<f>`, is the raw value of `x` as a value of the storage type. `Q.raw(n)` is the value of the fixed-point type `Q` whose raw value is `n`; `n` shall have exactly the storage type of `Q` (C2001). Both are exact and never fault: `Q16.16.raw(n)` with the `I32` 98304 is 1.5, and its `.raw` is 98304 again [IM-57, LS-38].

**Faults**

16. <a id="IM-132"></a><a id="IM-133"></a>The fault `E_NARROW` is raised by `x as T` when `v` is outside the range of `T`. The fault record holds the operation identifier `as.checked.s.t`, where `s` and `t` are the source and target types in lower case. A fixed-point type is written with `_` for its point (`q16_16`), and the identifier ends with `.mode` when the conversion rounds, whether its mode was written or not. The record holds one operand, the source value. Its exact result is `v`, which is a raw value when the target is a fixed-point type, and its limit is the bound of `T` that `v` crossed, a value of type `T`. Its position is the `as` token [IM-50, IM-68, IM-132, IM-133]. `as%`, `as|` and `as?` raise no fault.

17. <a id="IM-184"></a>A checked conversion whose source is a literal operand is decided at translation (6.8). When the value does not fit, the program is rejected with C6001, and the fault it reports has the operation identifier `unassigned`, which names a checked conversion of a literal decided at translation (B.3), the operand as a value in Z (`Z 300`), the exact result and the bound of `T` as the limit [IM-26, IM-184].

**Examples**

18. This example shows checked conversions that fit, and wrapping conversions that keep the low bits:

```cint convert_int.ci
I64 x = 2147483648;
"{x as% I32} {x as I128}\n";
I64 m = -1;
"{m as% U64} {m as% U8}\n";
U64 u = 9223372036854775808;
"{u as% I64}\n";
I64 n = -129;
"{n as% I8} {n as I16}\n";
```

```text
-2147483648 2147483648
18446744073709551615 255
-9223372036854775808
127 -129
```

19. 300 does not fit `U8`. `as%` gives 300 - 256, and `as` stops the program:

```cint convert_narrow.ci
I64 big = 300;
"{big as% U8}\n";
U8 small = big as U8;
"{small}\n";
```

```text
44
fault[E_NARROW]: the value does not fit the destination type
  --> convert_narrow.ci:3:16
   |
 3 | U8 small = big as U8;
   |                ^ as.checked.i64.u8
   |
   = operation: as.checked.i64.u8
   = operand-count: 1
   = operand: I64 300
   = exact:   300
   = limit:   U8 255
   ...
```

20. `as|` gives the nearest value of the target type. Rounding comes first, then the clamp:

```cint convert_sat.ci
I64 big = 300;
I64 low = -1000;
"{big as| U8} {low as| U8} {big as| I8} {low as| I8}\n";
Q16.16 x = 300.75;
Q16.16 y = -300.75;
"{x as| U8} {x as| U8 round floor} {y as| I8}\n";
I64 n = 40000;
"{n as| Q16.16} {(-1) as| U64}\n";
```

```text
255 0 127 -128
255 255 -128
32767.9999847412109375 0
```

21. `as?` with `catch` gives a fallback value instead of a fault:

```cint convert_result.ci
I64 big = 300;
I64 ok = 200;
U8 a = big as? U8 catch U8.max;
U8 b = ok as? U8 catch U8.max;
"{a} {b}\n";
```

```text
255 200
```

22. A conversion binds tighter than `+`:

```cint convert_precedence.ci
I32 a = 5;
I64 b = 7;
I64 c = a as I64 + b;
"{c}\n";
```

```text
12
```

23. Without the conversion, the addition mixes `I64` and `I32`:

```cint convert_implicit.ci
I32 a = 5;
I64 b = 7;
I64 c = b + a;
"{c}\n";
```

```text
error[C2001]: type mismatch
  --> convert_implicit.ci:3:13
   |
 3 | I64 c = b + a;
   |             ^
```

24. Literal operands convert from their values in Z:

```cint convert_literal.ci
U64 top = (18446744073709551615) as U64;
U64 all = -1 as% U64;
U8 low = (200 + 100) as% U8;
"{top} {all} {low}\n";
```

```text
18446744073709551615 18446744073709551615 44
```

25. A checked conversion of a literal that does not fit is found at translation:

```cint convert_literal_fault.ci
U8 k = 300 as U8;
"{k}\n";
```

```text
error[C6001]: fault during constant evaluation
  --> convert_literal_fault.ci:1:12
   |
 1 | U8 k = 300 as U8;
   |            ^
   |
fault[E_NARROW]: the value does not fit the destination type
  --> convert_literal_fault.ci:1:12
   |
 1 | U8 k = 300 as U8;
   |            ^ unassigned
   |
   = operation: unassigned
   = operand-count: 1
   = operand: Z 300
   = exact:   300
   = limit:   U8 255
   ...
```

26. An integer does not convert to `Bool`:

```cint convert_bool.ci
I64 n = 3;
Bool b = n as Bool;
"{b}\n";
```

```text
error[C2056]: integer used as `Bool`
  --> convert_bool.ci:2:10
   |
 2 | Bool b = n as Bool;
   |          ^
```

27. A conversion between integer types does not round, so it takes no `round` clause:

```cint convert_round_clause.ci
I64 n = 4;
I32 m = n as I32 round floor;
"{m}\n";
```

```text
error[C2009]: fraction, fixed-point, enum literal, or rounding rule
  --> convert_round_clause.ci:2:11
   |
 2 | I32 m = n as I32 round floor;
   |           ^
```

28. From fixed point to an integer, -2.5 rounds to -2 by `half_even` and to -3 by `floor` and `half_away`. From an integer, and to a type with more fraction bits, the conversion is exact. From `Q32.32` to `Q16.16`, the value 2^-32 rounds to 0 by `half_even` and to 2^-16 by `ceil`:

```cint convert_fixed.ci
Q16.16 x = -2.5;
"{x as I32} {x as I32 round floor} {x as I32 round half_away}\n";
I64 n = 3;
Q16.16 q = n as Q16.16;
"{q} raw {q.raw}\n";
Q32.32 fine = q as Q32.32;
"{fine} raw {fine.raw}\n";
Q32.32 tiny = Q32.32.raw(1);
Q16.16 t0 = tiny as Q16.16;
Q16.16 t1 = tiny as Q16.16 round ceil;
"{t0.raw} {t1.raw}\n";
```

```text
-2 -3 -3
3.0 raw 196608
3.0 raw 12884901888
0 1
```

29. 300.75 rounds to 301 by `half_even`, which does not fit `I8`. The operand is shown with its raw value, 300.75 times 2^16 and the operation identifier names the rounding mode, which was not written:

```cint convert_fixed_narrow.ci
Q16.16 x = 300.75;
I8 c = x as I8;
"{c}\n";
```

```text
fault[E_NARROW]: the value does not fit the destination type
  --> convert_fixed_narrow.ci:2:10
   |
 2 | I8 c = x as I8;
   |          ^ as.checked.q16_16.i8.half_even
   |
   = operation: as.checked.q16_16.i8.half_even
   = operand-count: 1
   = operand: Q16.16 raw 19709952
   = exact:   301
   = limit:   I8 127
   ...
```

30. 40000 does not fit `Q16.16`, whose largest value is 32768 - 2^-16. When the target is fixed point, the exact result and the limit are raw values:

```cint convert_to_fixed_narrow.ci
I64 n = 40000;
Q16.16 w = n as Q16.16;
"{w}\n";
```

```text
fault[E_NARROW]: the value does not fit the destination type
  --> convert_to_fixed_narrow.ci:2:14
   |
 2 | Q16.16 w = n as Q16.16;
   |              ^ as.checked.i64.q16_16
   |
   = operation: as.checked.i64.q16_16
   = operand-count: 1
   = operand: I64 40000
   = exact:   2621440000
   = limit:   Q16.16 raw 2147483647
   ...
```

31. `as%` rounds first and then wraps: 300.75 rounds to 301, which wraps to 45 in `U8`, and truncates to 300, which wraps to 44. 40000 times 2^16 does not fit `I32`, so its raw value wraps. `as?` reports the value that does not fit `I8`:

```cint convert_fixed_wrap.ci
Q16.16 x = 300.75;
U8 a = x as% U8;
U8 b = x as% U8 round trunc;
"{a} {b}\n";
I64 n = 40000;
Q16.16 w = n as% Q16.16;
"{w}\n";
I8 c = x as? I8 catch I8.max;
"{c}\n";
```

```text
45 44
-25536.0
127
```

32. `Q16.16.raw` builds a value from its raw value, which is an `I32` (paragraph 15):

```cint convert_raw.ci
I32 n = 98304;
Q16.16 x = Q16.16.raw(n);
Q16.16 y = Q16.16.raw(-1);
"{x} {y}\n";
I32 back = y.raw;
"{back}\n";
```

```text
1.5 -0.0000152587890625
-1
```

**Notes**

33. In C, the usual arithmetic conversions and assignment convert values without a cast, and converting an out-of-range value to a signed integer type gives an implementation-defined result. In cint every conversion is written, and `as` faults where C would give such a result.

34. `as%` is the only conversion that reinterprets a bit pattern. A program that means to wrap writes `as%`; a program that expects every value to fit writes `as` and learns of a value that does not.

<a id="expressions"></a>

### 6.7 Expressions

An expression computes a value from operands. This subclause defines the operators of cint from those that bind tightest to those that bind loosest, then the result forms, which report an arithmetic failure as an error value, then assignment, and last the rounding modes that every rounding operation names. Literals and their typing are in 6.4 and 6.5, conversions in 6.6, the order in which operands are evaluated in 6.2.4, and the operations called by name in clause 7.

The operators apply to every integer type of 6.3.1, from `I8` to `I1024` and from `U8` to `U512`, with the same meaning at every width: a wide type is an ordinary integer type. The arithmetic operators, the comparisons and negation also apply to fixed-point types (6.3.2), as each subclause states.

**Semantics**

1. An arithmetic operator has up to three spellings, one for each form: the plain spelling is a checked operator, the spelling with `%` a wrapping operator, and the spelling with `|` a saturating operator (3.5 to 3.7). The spelling alone chooses the form; no mode, option or build setting changes it [IM-28].

| Operation | Checked | Wrapping | Saturating | Result form (6.7.12) |
|---|---|---|---|---|
| Add | `a + b` | `a +% b` | `a +\| b` | `add_result(a, b)` |
| Subtract | `a - b` | `a -% b` | `a -\| b` | `sub_result(a, b)` |
| Multiply | `a * b` | `a *% b` | `a *\| b` | `mul_result(a, b)` |
| Divide | `a / b` | none | none | `div_result(a, b)` |
| Remainder | `a % b` | none | none | `rem_result(a, b)` |
| Shift left | `a << k` | `a <<% k` | none | `shl_result(a, k)` |
| Shift right | `a >> k` | none | none | none |
| Negate | `-a` | `-%a` | `0 -\| a` | `sub_result(0, a)` |

Conversions have forms of their own (6.6).

2. <a id="IM-31"></a>A wrapping or saturating operator never faults on overflow. Two conditions still fault in every form: a shift count outside its range (6.7.8) and a divisor of 0 (6.7.7) [IM-31].

3. As 6.2.4 states, every intermediate result has the type of the operation that produced it: no operand is promoted, and nothing is computed in a wider type except inside the built-in operations that say so (clause 7).

**Notes**

4. A fault record names the faulting operation by an operation identifier (3.12) of the form `name.form.t` (B.3). `form` is `checked`, `wrap` or `sat`, and `t` is the token of the operand type: the type's name in lower case (`i8`, `u64`, `i128`), or `q<i>_<f>` for the fixed-point type `Q<i>.<f>` (`q16_16`). An operation that rounds adds the rounding mode it used: `mul.checked.q16_16.half_even`. Annex B lists every operation identifier, and 8.1 describes the whole record.

<a id="precedence-and-associativity"></a>

#### 6.7.1 Precedence and associativity

When an expression has no parentheses, the precedence of its operators decides which operands each operator takes, and their associativity decides how operators of one level group. The order is close to C's. The differences are: the bitwise operators bind tighter than the comparisons; comparisons do not chain, and some mixtures need parentheses (6.7.2); a conversion binds tighter than the binary operators; `catch` binds loosest; and assignment is a statement (6.7.14).

**Syntax**

```ebnf
expr         = cond_expr [ "catch" ( "(" IDENT ")" block | cond_expr ) ] ;
cond_expr    = or_expr [ "?" expr ":" cond_expr ] ;
or_expr      = and_expr { "||" and_expr } ;
and_expr     = cmp_expr { "&&" cmp_expr } ;
cmp_expr     = bor_expr [ cmp_op bor_expr ] ;
bor_expr     = bxor_expr { "|" bxor_expr } ;
bxor_expr    = band_expr { "^" band_expr } ;
band_expr    = shift_expr { "&" shift_expr } ;
shift_expr   = add_expr { ( "<<" | ">>" | "<<%" ) add_expr } ;
add_expr     = mul_expr { ( "+" | "-" | "+%" | "-%" | "+|" | "-|" ) mul_expr } ;
mul_expr     = conv_expr { ( "*" | "/" | "%" | "*%" | "*|" ) conv_expr } ;
conv_expr    = unary_expr { conv_op type [ round_clause ] } ;
conv_op      = "as" | "as%" | "as|" | "as?" ;
unary_expr   = neg_literal | ( "-" | "-%" | "~" | "!" | "try" ) unary_expr | postfix_expr ;
postfix_expr = ( call | primary ) { "[" index_list "]" | "." IDENT } ;
```

**Semantics**

1. <a id="LS-149"></a>The operators have these levels, from the tightest to the loosest [LS-149]:

| Level | Operators | Grouping | Defined in |
|---|---|---|---|
| 1 | call `f(...)`, index `a[...]`, member `x.f` | left to right | 6.7.4 |
| 2 | unary `-` `-%` `~` `!` `try` | right to left | 6.7.5, 6.11 |
| 3 | `as` `as%` `as\|` `as?`, each with an optional `round` clause | left to right | 6.6 |
| 4 | `*` `/` `%` `*%` `*\|` | left to right | 6.7.7 |
| 5 | `+` `-` `+%` `-%` `+\|` `-\|` | left to right | 6.7.6 |
| 6 | `<<` `>>` `<<%` | left to right | 6.7.8 |
| 7 | `&` | left to right | 6.7.9 |
| 8 | `^` | left to right | 6.7.9 |
| 9 | `\|` | left to right | 6.7.9 |
| 10 | `==` `!=` `<` `<=` `>` `>=` | none | 6.7.10 |
| 11 | `&&` | left to right | 6.7.11 |
| 12 | `\|\|` | left to right | 6.7.11 |
| 13 | `? :` | right to left | 6.7.13 |
| 14 | `catch` | none | 6.11 |

2. An operator of a tighter level takes its operands before an operator of a looser level: `a + b * c` is `a + (b * c)`, and `b + a as I64` is `b + (a as I64)`. Operators of one level group as the table says: `a - b - c` is `(a - b) - c`, and `p ? x : q ? y : z` is `p ? x : (q ? y : z)` [LS-149].
3. The two levels without grouping do not chain. A comparison whose operand is an unparenthesized comparison is rejected (6.7.2), and a second `catch` after a fallback, as in `f() catch 0 catch 1`, is a syntax error (C1050) [LS-149].
4. Parentheses override the levels: a parenthesized expression is one operand, whatever operators it contains [LS-149].
5. <a id="LS-150"></a>The bitwise operators bind tighter than the comparisons, so `flags & MASK == 0` is `(flags & MASK) == 0` [LS-150].
6. A few combinations that the levels would accept are rejected unless they are parenthesized (6.7.2).

**Examples**

7. This example shows levels and grouping:

```cint prec_basic.ci
I64 a = 2 + 3 * 4;
I64 b = 10 - 4 - 3;
I64 c = 100 / 10 / 5;
I64 d = -2 * 3;
I32 n = 7;
I64 e = a + n as I64;
"{a} {b} {c} {d} {e}\n";
```

```text
14 3 2 -6 21
```

8. A bitwise operation inside a comparison needs no parentheses:

```cint prec_flags.ci
U8 flags = 6;
U8 mask = 1;
Bool clear = flags & mask == 0;
"{clear}\n";
```

```text
true
```

9. `catch` (6.7.12) binds loosest of all, so its fallback here is `0 + 1`, not `0`:

```cint prec_catch.ci
I64 big = I64.max;
I64 v = add_result(big, 1) catch 0 + 1;
"{v}\n";
```

```text
1
```

**Notes**

10. In C, `==` binds tighter than `&`, so `flags & mask == 0` means `flags & (mask == 0)`. The same text in cint tests the masked bits.

<a id="required-parentheses-and-unchained-comparisons"></a>

#### 6.7.2 Required parentheses and unchained comparisons

Some combinations of operators are often misread, or are read one way by a C programmer and another way by a reader of other languages. cint rejects them until parentheses say which grouping is meant.

**Constraints**

1. <a id="LS-151"></a>Each of these combinations shall be parenthesized. Without the parentheses it is diagnosed C2101, at the first character of the expression that needs them [LS-151]:
   - `&&` and `||` in one expression: `a && b || c`;
   - a shift with an operand that is an arithmetic operation (`+`, `-`, `*`, `/` or `%`, in any form): `1 << n + 1`;
   - a bitwise operator (`&`, `|` or `^`) with an operand that is an arithmetic operation: `a & b + 1`;
   - two different bitwise operators: `a & b | c`;
   - a unary operator `-`, `-%`, `~` or `!` directly followed by a conversion: `-x as I32`, whatever the values involved.
2. A negative literal (6.4.5) is one operand, not a unary operator applied to a literal, so `-1 as% U64` and `- 1 as% U64` need no parentheses [LS-151].
3. <a id="LS-153"></a>An operand of a comparison shall not be an unparenthesized comparison: `a < b < c`, `0 <= i < n` and `a == b == c` are diagnosed C2102, at the first character of the expression. A parenthesized comparison is an ordinary `Bool` operand, so `(a < b) == c` is valid when `c` is a `Bool` [LS-153, IM-48].

**Examples**

4. This example shows mixed `&&` and `||`:

```cint paren_logic.ci
Bool a = true;
Bool b = false;
Bool c = true;
Bool d = a && b || c;
"{d}\n";
```

```text
error[C2101]: parentheses required
  --> paren_logic.ci:4:10
   |
 4 | Bool d = a && b || c;
   |          ^
```

5. This example shows the same operations with the parentheses in place. A shift or a comparison as the operand of a bitwise operator needs none, and neither does a negative literal before a conversion:

```cint paren_ok.ci
Bool a = true;
Bool b = false;
Bool c = true;
I64 n = 3;
I64 x = 5;
U8 m = 12;
Bool d = (a && b) || c;
I64 s = 1 << (n + 1);
U8 t = (m & 10) | 3;
Bool z = m << 1 & 3 == 0;
U64 u = -1 as% U64;
I8 v = (-x) as% I8;
"{d} {s} {t} {z} {u} {v}\n";
```

```text
true 16 11 true 18446744073709551615 -5
```

6. This example shows a unary operator directly before a conversion:

```cint paren_unary.ci
I64 x = 5;
I32 y = -x as I32;
"{y}\n";
```

```text
error[C2101]: parentheses required
  --> paren_unary.ci:2:9
   |
 2 | I32 y = -x as I32;
   |         ^
```

7. This example shows a chained comparison:

```cint paren_chain.ci
I64 i = 5;
I64 n = 10;
Bool ok = 0 <= i < n;
"{ok}\n";
```

```text
error[C2102]: chained comparison
  --> paren_chain.ci:3:11
   |
 3 | Bool ok = 0 <= i < n;
   |           ^
```

**Notes**

8. In C, `0 <= i < n` compiles: it compares the result of `0 <= i`, which is 0 or 1, with `n`, and so is true for every `i` when `n` is greater than 1.
9. A program writes `(-x) as I32` or `-(x as I32)`, whichever is meant, in place of `-x as I32`, and `0 <= i && i < n` in place of `0 <= i < n`.

<a id="primary-expressions"></a>

#### 6.7.3 Primary expressions

A primary expression is an operand that no operator builds: a literal, a name, a parenthesized expression, an array literal, an error value typed by its context, or a property of a type. Each is defined where its type is; this subclause collects them.

**Syntax**

```ebnf
primary      = literal
             | STRING { STRING }
             | IDENT
             | ( INT_TYPE | "Bool" | FIXED_TYPE ) [ "." IDENT ]
             | FIXED_TYPE "." "raw" "(" [ args ] ")"
             | "(" expr ")"
             | array_literal
             | "." IDENT ;
literal      = INT_LIT | CHAR_LIT | FRAC_LIT [ round_clause ] | bool_literal ;
```

`bool_literal` is defined in 6.3.3 and `array_literal` in 6.12.1. A `STRING` operand is a string literal passed as an argument (6.4.8).

**Semantics**

1. A literal denotes a value in Z, or a rational number for a fraction literal. It has no type of its own and takes the type of its context (6.4.5 to 6.4.8, 6.5). A fraction literal appears only where the context is a fixed-point type, and one that is not on that type's grid needs a `round` clause (6.4.6, 6.7.15).
2. A name denotes a variable, constant, parameter, function, type or imported module in scope (6.2.3). As an operand, a variable or a parameter gives its current value.
3. A parenthesized expression has the type and the value of the expression it encloses. Parentheses group (6.7.1) and do not convert, and a literal in parentheses is still a literal operand (6.5).
4. An array literal `[a, b, c]` lists the elements of an array in order; nested array literals give arrays of rank 2 and above (6.12.1).
5. `.name` is a value of an error set, typed by its context (6.3.7, 6.11).
6. `T.min` and `T.max` give the bounds of the integer or fixed-point type `T` (6.3.1, 6.3.2), and `Q<i>.<f>.raw(n)` gives the fixed-point value whose raw value is `n` (6.3.2).

**Examples**

7. A type property, a parenthesized operand and a character literal typed by its context:

```cint primary.ci
I8 top = I8.max;
I64 x = (2 + 3) * 4;
U8 c = 'A';
"{top} {x} {c}\n";
```

```text
127 20 65
```

<a id="postfix-expressions"></a>

#### 6.7.4 Postfix expressions

A postfix expression is a call or a primary expression, followed by any number of indexes and member selections. They bind tighter than every other operator and are read from left to right.

**Syntax**

```ebnf
postfix_expr = ( call | primary ) { "[" index_list "]" | "." IDENT } ;
call         = IDENT [ "." IDENT ] "(" [ args ] ")" ;
args         = arg { "," arg } [ "," ] ;
arg          = [ IDENT "=" ] expr ;
index_list   = index { "," index } ;
index        = expr
             | [ expr ] ".." [ expr ] [ "by" expr ]
             | [ expr ] "..=" expr [ "by" expr ] ;
```

**Constraints**

1. The callee of a call shall name a function, a built-in operation, a struct type (a constructor, 6.9.6) or a kernel (a dispatch, 6.13.4), either directly or after the name or alias of an imported module (`alias.f`, 6.15.1); or, after the name of an arena or a pool, one of its members, such as `scratch.alloc(4)` (6.14). A callee name that denotes anything else, such as a variable, is diagnosed C3007.

**Semantics**

2. Postfix operators bind tighter than every other operator and apply from left to right, as 6.7.1 states: `-grid[0, 1]` is `-(grid[0, 1])`, and `p.x + p.y` adds two fields.
3. A call evaluates its arguments from left to right, each completely, and then calls the function (6.2.4). Arguments match parameters by position, or by name, written `name = value` (6.9.5). Calling a struct type with values for its fields builds a struct value (6.9.6).
4. A built-in operation is called with the same syntax (clause 7). Some built-in operations take a type or a rounding-mode name as an argument; that argument is a bare name, not a value (6.7.15, 7.1).
5. A call of a function that returns no result (`void`) has no value. It stands only as a statement (6.10.1); as an operand it is diagnosed C2001.
6. `a[i]` gives an element of an array or view, `m[i, j]` an element of an array of rank 2, and `a[lo..hi]` a slice. Their meaning and their faults are in 6.12.2 and 6.12.3.
7. `s.f` gives the field `f` of the struct value `s` (6.3.6, 6.9.6).
8. `++` and `--` are statements, not postfix operators (6.7.14).

**Examples**

9. This example shows a call, two fields, an element of a rank-2 array and a negated element:

```cint postfix.ci
struct Point {
    I64 x;
    I64 y;
}
I64 area(I64 width, I64 height) {
    return width * height;
}
void main() {
    I64[2, 3] grid = [[1, 2, 3], [4, 5, 6]];
    Point p = Point(3, 4);
    "{area(2, 5)} {p.x + p.y} {grid[1, 2]} {-grid[0, 1]}\n";
}
```

```text
10 7 6 -2
```

10. Precedence decides which operands an operator takes, not when they are evaluated. The operands below are evaluated from left to right, although `*` is applied before `+`:

```cint postfix_order.ci
I64 trace(I64 k) {
    "{k} ";
    return k;
}
I64 v = trace(1) + trace(2) * trace(3);
"= {v}\n";
```

```text
1 2 3 = 7
```

<a id="unary-operators"></a>

#### 6.7.5 Unary operators

`-` negates, `-%` negates with wrapping, `~` complements the bits of an integer, and `!` negates a `Bool`. The absolute value is the built-in operation `abs`, checked like `-`, and `uabs` gives it in the unsigned type of the same width, where it always fits.

**Syntax**

```ebnf
unary_expr   = neg_literal
             | ( "-" | "-%" | "~" | "!" | "try" ) unary_expr
             | postfix_expr ;
```

`abs(a)` and `uabs(a)` are calls (6.7.4). `try` is defined in 6.11.

**Constraints**

1. <a id="IM-60"></a>The operand of `-` shall have an integer or a fixed-point type. The operand of `-%` and of `~` shall have an integer type. Any other operand type is diagnosed C2103 [IM-47, IM-60].
2. The operand of `!` shall have type `Bool` (6.3.3); any other type is diagnosed C2002.
3. <a id="IM-34"></a>The argument of `abs` shall have an integer or a fixed-point type. The argument of `uabs` shall have a signed integer type from `I8` to `I512`; any other type, `I1024` included, is diagnosed C2103 [IM-34].
4. A `-` directly before a numeric or character literal forms a negative literal (6.4.5), which is one operand and not a negation.

**Semantics**

5. <a id="IM-33"></a>`-a` is the checked negation: the result is `-a` when `in(T, -a)`, and otherwise the operation faults. For a signed type only `-MIN(T)` is out of range; for an unsigned type, `-a` faults for every `a` other than 0 [IM-33].
6. `-%a` is `wrap(T, -a)`, the same value as `0 -% a`. It never faults. The saturating negation is written `0 -| a` [IM-28].
7. `~a` inverts every bit of the two's complement pattern of `a`, of width `w`, and has type `T`. So `~a` is `-a - 1` for a signed type and `MAX(T) - a` for an unsigned type. It never faults [IM-47].
8. `!b` is `true` when `b` is `false`, and `false` when `b` is `true`.
9. `abs(a)` is the checked absolute value: `|a|` when it is in range, and a fault for `abs(MIN(T))` [IM-34].
10. `uabs(a)` is `|a|` as a value of the unsigned type of the same width: `I8` gives `U8`, and so on up to `I512`, which gives `U512`. It never faults [IM-34].
11. On a fixed-point operand, `-a` and `abs(a)` act on the raw value with the integer meaning above. Nothing rounds [IM-60].

**Faults**

12. The fault `E_OVERFLOW` is raised by `-a` when `-a` is out of range. The record holds the operation identifier `neg.checked.t`, the one operand, the exact result `-a`, and the bound it crossed as the limit. Its position is the `-` token [IM-33].
13. The fault `E_OVERFLOW` is raised by `abs(a)` when `|a|` is out of range. The record holds `abs.checked.t`, the operand, the exact result `|a|` and the limit `MAX(T)`. Its position is the callee name `abs` [IM-34].

**Examples**

14. This example shows negation, complement and the two absolute values:

```cint unary.ci
I8 s = 5;
U8 a = 0b1100;
I8 m = -128;
"{-s} {~s} {~a} {~m}\n";
"{abs(m + 1)} {uabs(m)}\n";
```

```text
-5 -6 243 127
127 128
```

15. An unsigned value has no negation other than 0:

```cint neg_unsigned.ci
U8 zero = 0;
U8 one = 1;
"{-zero}\n";
U8 b = -one;
"{b}\n";
```

```text
0
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> neg_unsigned.ci:4:8
   |
 4 | U8 b = -one;
   |        ^ neg.checked.u8
   |
   = operation: neg.checked.u8
   = operand-count: 1
   = operand: U8 1
   = exact:   -1
   = limit:   U8 0
   ...
```

16. This example shows `abs` of the minimum of a signed type:

```cint abs_min.ci
I8 m = -128;
I8 c = abs(m);
"{c}\n";
```

```text
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> abs_min.ci:2:8
   |
 2 | I8 c = abs(m);
   |        ^ abs.checked.i8
   |
   = operation: abs.checked.i8
   = operand-count: 1
   = operand: I8 -128
   = exact:   128
   = limit:   I8 127
   ...
```

17. This example shows wrapping and saturating negation of `I64.min`:

```cint neg_forms.ci
I64 m = I64.min;
I64 x = 5;
"{-%x} {-%m} {0 -| m}\n";
```

```text
-5 -9223372036854775808 9223372036854775807
```

18. This example shows `uabs` of a wide type, and negation and `abs` on fixed point:

```cint unary_wide_fixed.ci
I128 w = I128.min;
U128 u = uabs(w);
Q16.16 q = -2.5;
"{u} {-q} {abs(q)}\n";
```

```text
170141183460469231731687303715884105728 2.5 2.5
```

19. The minimum of `Q16.16` has no negation. The record holds raw values: the exact result, raw 2147483648, is 32768.0, one raw unit above the maximum:

```cint neg_fixed_min.ci
Q16.16 m = Q16.16.min;
Q16.16 n = -m;
"{n}\n";
```

```text
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> neg_fixed_min.ci:2:12
   |
 2 | Q16.16 n = -m;
   |            ^ neg.checked.q16_16
   |
   = operation: neg.checked.q16_16
   = operand-count: 1
   = operand: Q16.16 raw -2147483648
   = exact:   2147483648
   = limit:   Q16.16 raw 2147483647
   ...
```

**Notes**

20. In C, negating an unsigned value wraps without notice, and `abs(INT_MIN)` is undefined behavior. In cint the first faults unless the value is 0, and the second faults; `-%` and `uabs` give the wrapped value and the unsigned magnitude when a program wants them.

<a id="additive-operators"></a>

#### 6.7.6 Additive operators

`+` adds and `-` subtracts. Each has three spellings that differ only in what happens when the exact result is outside the range of the type: the plain operator faults, the wrapping operator reduces the result modulo 2^`w`, and the saturating operator clamps it to the nearer bound. On fixed-point operands the six operators act on the raw values and do not round.

**Syntax**

```ebnf
add_expr = mul_expr { ( "+" | "-" | "+%" | "-%" | "+|" | "-|" ) mul_expr } ;
```

**Constraints**

1. <a id="IM-29"></a>Both operands shall have the same integer type, or the same fixed-point type, after a literal operand takes its type from the other operand (6.5). There is no implicit conversion: mixing `I32` with `I64`, `I64` with `U64` or `Q16.16` with `I32` or with another fixed-point type, is diagnosed C2001 [IM-29, LS-156].
2. <a id="LS-152"></a>The six operators have the same precedence and group from left to right, so plain, wrapping and saturating forms may be mixed: `a +% b + c` is `(a +% b) + c` [LS-152].

**Semantics**

3. <a id="IM-32"></a>The result has the type `T` of the operands. Let `v` be `a + b` or `a - b`, computed in Z [IM-32].
4. <a id="IM-30"></a>For `+` and `-`, the result is `v` when `in(T, v)`. Otherwise the operation faults [IM-30].
5. For `+%` and `-%`, the result is `wrap(T, v)` [IM-30].
6. For `+|` and `-|`, the result is `sat(T, v)` [IM-30].
7. For an unsigned type, `a - b` with `a < b` has a negative exact result and therefore faults; `a -% b` wraps and `a -| b` gives 0 [IM-32].
8. When `T` is a fixed-point type, the operators act on the raw values `A` and `B` of the operands (6.3.2): `v` is `A + B` or `A - B`, paragraphs 4 to 6 apply to `v` with the range of the raw values, and the result is the value of `T` whose raw value that gives. Nothing rounds. A literal operand denotes its exact value in `T`, so with `a` of type `Q16.16`, `a + 1` adds 1.0 [IM-60].
9. For an integer type, the result forms `add_result(a, b)` and `sub_result(a, b)` give `v` when it is in range and the error `ArithError.overflow` otherwise (6.7.12).

**Faults**

10. The fault `E_OVERFLOW` is raised by `+` or `-` when `v` is outside the range of `T`. The fault record holds the operation identifier `add.checked.t` or `sub.checked.t`, where `t` is the token of `T` (`i64`, `u8`, `i128`, `q16_16`); both operands; the exact result `v`; and, as the limit, the bound that `v` crossed. For a fixed-point `T`, the exact result and the limit are raw values. Its position is the operator token. The wrapping and saturating forms raise no fault.

**Examples**

11. This example shows results at the edge of `I64`, whose maximum is 9223372036854775807, and of `U8`:

```cint additive.ci
I64 big = I64.max;
"{big +% 1}\n";
"{big +| 1}\n";
U8 a = 3;
U8 b = 5;
"{a -% b} {a -| b}\n";
```

```text
-9223372036854775808
9223372036854775807
254 0
```

12. The plain operator stops the program instead:

```cint additive_fault.ci
U8 a = 3;
U8 b = 5;
U8 d = a - b;
"{d}\n";
```

```text
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> additive_fault.ci:3:10
   |
 3 | U8 d = a - b;
   |          ^ sub.checked.u8
   |
   = operation: sub.checked.u8
   = operand-count: 2
   = operand: U8 3
   = operand: U8 5
   = exact:   -2
   = limit:   U8 0
   ...
```

13. This example shows the same rule at 128 bits:

```cint additive_wide.ci
I128 top = I128.max;
"{top +% 1}\n";
I128 c = top + 1;
"{c}\n";
```

```text
-170141183460469231731687303715884105728
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> additive_wide.ci:3:14
   |
 3 | I128 c = top + 1;
   |              ^ add.checked.i128
   |
   = operation: add.checked.i128
   = operand-count: 2
   = operand: I128 170141183460469231731687303715884105727
   = operand: I128 1
   = exact:   170141183460469231731687303715884105728
   = limit:   I128 170141183460469231731687303715884105727
   ...
```

14. Fixed-point sums, a literal operand and the two other forms at the top of `Q16.16`, whose raw values are those of `I32`:

```cint additive_fixed.ci
Q16.16 a = 0.75;
Q16.16 b = 1.5;
"{a + b} {a - b} {(a - b).raw} {a + 1}\n";
Q16.16 big = Q16.16.max;
Q16.16 step = Q16.16.raw(1);
"{big +| step} {big +% step}\n";
```

```text
2.25 -0.75 -49152 1.75
32767.9999847412109375 -32768.0
```

15. The checked sum one raw unit past the top of `Q16.16`. The operands, the exact result and the limit are raw values:

```cint additive_fixed_fault.ci
Q16.16 big = Q16.16.max;
Q16.16 step = Q16.16.raw(1);
Q16.16 c = big + step;
"{c}\n";
```

```text
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> additive_fixed_fault.ci:3:16
   |
 3 | Q16.16 c = big + step;
   |                ^ add.checked.q16_16
   |
   = operation: add.checked.q16_16
   = operand-count: 2
   = operand: Q16.16 raw 2147483647
   = operand: Q16.16 raw 1
   = exact:   2147483648
   = limit:   Q16.16 raw 2147483647
   ...
```

**Notes**

16. In C, signed overflow of `+` is undefined behavior and unsigned arithmetic wraps without notice. In cint both fault, and a program that wants wrapping says so with `+%`.

<a id="multiplicative-operators"></a>

#### 6.7.7 Multiplicative operators

`*` multiplies, `/` divides and `%` gives the remainder. Integer `/` rounds the quotient toward negative infinity, called floor division, and `%` gives the remainder that goes with it, which has the sign of the divisor. On fixed-point operands, `*` and `/` form the exact product or quotient and round it once, half to even unless another mode is named.

**Syntax**

```ebnf
mul_expr = conv_expr { ( "*" | "/" | "%" | "*%" | "*|" ) conv_expr } ;
```

**Constraints**

1. Both operands shall have the same integer type, or for `*`, `*%`, `*|` and `/` the same fixed-point type, after a literal operand takes its type from the other operand (6.5). Mixing two types is diagnosed C2001, including two fixed-point types of different formats and a fixed-point type with an integer type. The built-in operations `qmul` and `mul_int` multiply such pairs (7.2.4) [IM-29, LS-156].
2. <a id="LS-160"></a><a id="IM-66"></a>`%` is not defined on fixed point; a fixed-point operand of `%` is diagnosed C2103 [LS-160, IM-66].
3. The five operators have the same precedence and group from left to right, as 6.7.1 states: `a / b * c` is `(a / b) * c`.

**Semantics**

4. On integers, let `v` be `a * b` computed in Z. `*` gives `v` when `in(T, v)` and otherwise faults; `*%` gives `wrap(T, v)`; `*|` gives `sat(T, v)` [IM-32, IM-30].
5. <a id="IM-35"></a><a id="LS-158"></a>For `b` other than 0, `a / b` is `floor(a / b)`: the rational quotient rounded toward negative infinity [IM-35, LS-158].
6. For `b` other than 0, `a % b` is `a - b * (a / b)`. So `a == (a / b) * b + a % b` holds, `|a % b| < |b|`, and a remainder other than 0 has the sign of the divisor: `-7 / 2` is -4 and `-7 % 2` is 1 [IM-35, LS-158].
7. <a id="IM-39"></a>When `b` is 0, `/` and `%` fault, `0 / 0` included [IM-39].
8. <a id="IM-40"></a>When `T` is signed, `a` is `MIN(T)` and `b` is -1, the quotient `-MIN(T)` is out of range, so `/` faults. The remainder is 0, which is in range, so `MIN(T) % -1` is 0 and does not fault, on every implementation, including one whose processor traps on that division [IM-39, IM-40].
9. <a id="IM-41"></a>There is no wrapping or saturating form of `/` or `%` [IM-41]. Truncating and Euclidean division are built-in operations (7.2).
10. <a id="LS-159"></a><a id="IM-44"></a>Integer division under another rounding mode is the built-in operation `div_round(a, b, mode)`: the rational quotient `a / b`, rounded once by `mode` (6.7.15) and then checked against `T`. `div_round(a, b, floor)` equals `a / b`, and `div_round(a, b, trunc)` equals `div_trunc(a, b)` [LS-159, IM-44].
11. <a id="IM-61"></a><a id="IM-62"></a>On a fixed-point type `Q<i>.<f>`, let `A` and `B` be the raw values of `a` and `b`. `a * b` forms the exact product `A * B`, then the raw result `R = round(A * B / 2^f, half_even)`; `*` checks `R` against the range of the raw values, `*%` wraps it and `*|` saturates it [IM-61, IM-62, LS-160].
12. <a id="IM-65"></a>On a fixed-point type, `a / b` forms the exact `A * 2^f` and gives the raw result `R = round(A * 2^f / B, half_even)`, checked against the range of the raw values. A `B` of 0 faults before any other check. Fixed-point `/` is a rounded rational division, not floor division of the raw values, and it has only the checked form [IM-65, IM-66].
13. The built-in operations `mul(a, b, mode)` and `div(a, b, mode)` are fixed-point `*` and `/` rounded by `mode` instead of `half_even`; `mul_wrap(a, b, mode)` and `mul_sat(a, b, mode)` are `*%` and `*|` rounded by `mode`. They are defined on fixed point only, and an integer argument is diagnosed C2103 [IM-62, IM-66, LS-160].
14. The products and the scaled dividend of paragraphs 11 and 12 are exact, so the result is the one that rounding the true product or quotient by the mode gives (6.7.15) [IM-61, IM-65].

**Faults**

15. The fault `E_OVERFLOW` is raised by `*` when the result is out of range, and by `/` when `a` is `MIN(T)` and `b` is -1. The record holds `mul.checked.t` or `div.checked.t`, both operands, the exact result, and the bound it crossed as the limit. Its position is the operator token [IM-32, IM-39].
16. The fault `E_DIV_ZERO` is raised by `/` and `%` when `b` is 0. The record holds `div.checked.t` or `rem.checked.t` and both operands; it has no exact result and no limit. Its position is the operator token [IM-39].
17. On fixed point, the operation identifier also names the rounding mode, written or defaulted: `mul.checked.q16_16.half_even` for `*`, `div.checked.q16_16.floor` for `div(a, b, floor)`. The operands are recorded with their raw values, and the exact result is the rounded raw value `R`. The position is the operator token, or the callee name for `mul` and `div`. `*%`, `*|`, `mul_wrap` and `mul_sat` raise no fault [IM-62, IM-66].

**Examples**

18. This example shows floor division and its remainder for each combination of signs:

```cint floor_div.ci
I64 a = 7;
I64 b = 2;
"{a / b} {a % b}\n";
"{-a / b} {-a % b}\n";
"{a / -b} {a % -b}\n";
"{-a / -b} {-a % -b}\n";
```

```text
3 1
-4 1
-4 -1
3 -1
```

19. `MIN(T) % -1` is 0, and `MIN(T) / -1` faults:

```cint div_min.ci
I64 m = I64.min;
I64 neg = -1;
"{m % neg}\n";
I64 q = m / neg;
"{q}\n";
```

```text
0
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> div_min.ci:4:11
   |
 4 | I64 q = m / neg;
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

20. This example shows a divisor of 0:

```cint div_zero.ci
I64 a = 5;
I64 z = 0;
I64 r = a % z;
"{r}\n";
```

```text
fault[E_DIV_ZERO]: division by zero
  --> div_zero.ci:3:11
   |
 3 | I64 r = a % z;
   |           ^ rem.checked.i64
   |
   = operation: rem.checked.i64
   = operand-count: 2
   = operand: I64 5
   = operand: I64 0
   = exact:   none
   = limit:   none
   ...
```

21. This example shows the three forms of `*`:

```cint products.ci
I32 a = 65536;
I32 b = 32768;
I8 c = 100;
I8 m = -128;
"{-a * b} {a *% b} {a *| b}\n";
"{c *% 3} {m *| -1} {m *% -1}\n";
```

```text
-2147483648 -2147483648 2147483647
44 127 -128
```

22. An intermediate result has the type of its operands, so `a * b / 4` faults at `*` although the final value would fit `I32`. A program that needs the wider product converts first (or calls `muldiv`, 7.2):

```cint no_promotion.ci
I32 a = 2_000_000_000;
I32 b = 2;
I32 d = ((a as I64) * (b as I64) / 4) as I32;
"{d}\n";
I32 c = a * b / 4;
"{c}\n";
```

```text
1000000000
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> no_promotion.ci:5:11
   |
 5 | I32 c = a * b / 4;
   |           ^ mul.checked.i32
   |
   = operation: mul.checked.i32
   = operand-count: 2
   = operand: I32 2000000000
   = operand: I32 2
   = exact:   4000000000
   = limit:   I32 2147483647
   ...
```

23. This example shows floor division at 128 bits:

```cint div_wide.ci
I128 m = I128.min;
"{m / 3} {m % 3}\n";
```

```text
-56713727820156410577229101238628035243 1
```

24. This example shows fixed-point products and quotients. The raw value 1 times 1.5 is 1.5 raw units, which `half_even` rounds to 2, `floor` to 1 and `ceil` to 2; 1.0 / 3.0 rounds to raw 21845, and three times that is raw 65535, not 1.0:

```cint fixed_muldiv.ci
Q16.16 a = 1.5;
Q16.16 b = 2.25;
"{a * b} {b / a}\n";
Q16.16 tiny = Q16.16.raw(1);
"{(tiny * a).raw} {mul(tiny, a, floor).raw} {mul(tiny, a, ceil).raw}\n";
Q16.16 one = 1.0;
Q16.16 three = 3.0;
Q16.16 third = one / three;
"{third} {third.raw} {(third * three).raw} {div(one, three, ceil).raw}\n";
```

```text
3.375 1.5
2 1 2
0.3333282470703125 21845 65535 21846
```

25. The wrapping and saturating products at the top of `Q16.16`, where 256.0 * 128.0 is 32768.0, one raw unit past the range:

```cint fixed_sat.ci
Q16.16 a = 256.0;
Q16.16 b = 128.0;
"{a *| b} {a *% b} {mul_sat(a, b, floor)}\n";
```

```text
32767.9999847412109375 -32768.0 32767.9999847412109375
```

26. This example shows the checked product of the same operands. The identifier names `half_even`, the mode that `*` uses without writing it, and the exact result is the rounded raw value:

```cint fixed_mul_fault.ci
Q16.16 a = 256.0;
Q16.16 b = 128.0;
Q16.16 c = a * b;
"{c}\n";
```

```text
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> fixed_mul_fault.ci:3:14
   |
 3 | Q16.16 c = a * b;
   |              ^ mul.checked.q16_16.half_even
   |
   = operation: mul.checked.q16_16.half_even
   = operand-count: 2
   = operand: Q16.16 raw 16777216
   = operand: Q16.16 raw 8388608
   = exact:   2147483648
   = limit:   Q16.16 raw 2147483647
   ...
```

27. This example shows `%` on fixed point:

```cint fixed_rem.ci
Q16.16 a = 2.5;
Q16.16 b = 1.0;
Q16.16 r = a % b;
"{r}\n";
```

```text
error[C2103]: operator not defined for the operand type
  --> fixed_rem.ci:3:12
   |
 3 | Q16.16 r = a % b;
   |            ^
```

**Notes**

28. <a id="IM-37"></a>C's integer `/` truncates toward 0, so `-7 / 2` is -3 in C and -4 in cint; the two differ, without any fault, whenever the operands have opposite signs and the division is inexact. Code ported from C writes `div_trunc(a, b)` for C's `a / b` and `rem_trunc(a, b)` for C's `a % b` (7.2) [IM-37].
29. In C, `INT_MIN / -1` and `INT_MIN % -1` are undefined behavior. In cint the first faults and the second is 0.

<a id="shifts-and-rotations"></a>

#### 6.7.8 Shifts and rotations

`<<` shifts left and `>>` shifts right. A left shift is a multiplication by a power of 2, checked like `*`; `<<%` keeps the low `w` bits instead. A right shift is a floor division by a power of 2, which never overflows. A shift count outside 0 to `w - 1` faults in every form; it is never reduced modulo `w`. The built-in operations `rotl` and `rotr` rotate the bits of an unsigned integer.

**Syntax**

```ebnf
shift_expr = add_expr { ( "<<" | ">>" | "<<%" ) add_expr } ;
```

`rotl(a, k)` and `rotr(a, k)` are calls (6.7.4).

**Constraints**

1. The left operand shall have an integer type; a shift of a `Bool` or of a fixed-point value is diagnosed C2103 [IM-60].
2. <a id="IM-45"></a><a id="LS-156"></a>The count, the right operand, may have any integer type, independent of the type of the left operand. A literal count has type `I64`, and a literal left operand takes its type from the context of the whole shift, not from the count (6.5) [IM-45, LS-156].
3. The first argument of `rotl` and `rotr` shall have an unsigned integer type; a signed argument is diagnosed C2103. Their count may have any integer type [IM-45].
4. A shift with an arithmetic operand needs parentheses (6.7.2).

**Semantics**

5. The result has the type `T` of the left operand, of width `w`. The type of the count never affects it [IM-45].
6. <a id="LS-157"></a>The count `k` is compared as a value in Z. Every shift form, `<<%` and `>>` included and `rotl` and `rotr`, require `0 <= k <= w - 1` and fault otherwise. A count is never reduced modulo `w`: `x << 64` on `I64` faults; it does not give `x` and does not give 0 [IM-45, LS-157].
7. `a << k` is `v = a * 2^k` computed in Z: the result is `v` when `in(T, v)`, and otherwise the operation faults [IM-45, LS-156].
8. `a <<% k` is `wrap(T, a * 2^k)`: the low `w` bits of the two's complement pattern shifted left [IM-45].
9. `a >> k` is `floor(a / 2^k)`. For a signed type it is an arithmetic shift and for an unsigned type a logical one. It never overflows [IM-45].
10. <a id="IM-46"></a>There is no logical right shift of a signed type. `I1024` has no unsigned type of its width [IM-46].
11. `rotl(a, k)` and `rotr(a, k)` rotate the `w`-bit pattern of `a` left or right by `k` bits [IM-45].

**Faults**

12. <a id="IM-185"></a>The fault `E_SHIFT` is raised by a shift or a rotation whose count `k` is outside 0 to `w - 1`. The record holds `shl.checked.t` for `<<`, `shl.wrap.t` for `<<%`, `shr.checked.t` for `>>` or `rotl.checked.t` or `rotr.checked.t`, where `t` names the type of the left operand only; as operands, the left operand and then the count, the count with its own type; no exact result; and as the limit `w - 1` as an `I64`, for every count out of range, negative counts included. Its position is the operator token, or the callee name of a rotation [IM-45, IM-185].
13. The fault `E_OVERFLOW` is raised by `<<` when `a * 2^k` is out of range. The record holds `shl.checked.t`, both operands, the exact result `a * 2^k`, and the bound it crossed as the limit. Its position is the operator token [IM-45].

**Examples**

14. This example shows shifts at the edges of `I64` and of the unsigned types:

```cint shifts.ci
I64 one = 1;
I64 m1 = -1;
I64 m7 = -7;
"{one << 62} {m1 << 63} {one <<% 63}\n";
"{m7 >> 1} {m1 >> 63}\n";
U8 x = 255;
U64 u = U64.max;
"{x <<% 4} {u >> 63}\n";
```

```text
4611686018427387904 -9223372036854775808 -9223372036854775808
-4 -1
240 1
```

15. The count's type does not affect the result, and a literal left operand is typed by its declaration, not by the count:

```cint shift_count.ci
U8 k = 40;
I64 v = 1 << k;
I8 s = 3;
U8 b = 1;
U8 w = b << s;
"{v} {w}\n";
```

```text
1099511627776 8
```

16. This example shows a count equal to the width:

```cint shift_wide_count.ci
I64 x = 1;
"start\n";
I64 v = x << 64;
"{v}\n";
```

```text
start
fault[E_SHIFT]: shift count outside 0..w-1
  --> shift_wide_count.ci:3:11
   |
 3 | I64 v = x << 64;
   |           ^ shl.checked.i64
   |
   = operation: shl.checked.i64
   = operand-count: 2
   = operand: I64 1
   = operand: I64 64
   = exact:   none
   = limit:   I64 63
   ...
```

17. This example shows a negative count, recorded with its own type `I8`:

```cint shift_negative.ci
I64 x = 1;
I8 k = -1;
I64 v = x >> k;
"{v}\n";
```

```text
fault[E_SHIFT]: shift count outside 0..w-1
  --> shift_negative.ci:3:11
   |
 3 | I64 v = x >> k;
   |           ^ shr.checked.i64
   |
   = operation: shr.checked.i64
   = operand-count: 2
   = operand: I64 1
   = operand: I8 -1
   = exact:   none
   = limit:   I64 63
   ...
```

18. A logical right shift of a signed value, written with a conversion and two rotations:

```cint shift_logical.ci
I64 a = -8;
U64 b = (a as% U64) >> 60;
U32 x = 0x80000001;
"{b} {a >> 60}\n";
"{rotl(x, 1)} {rotr(x, 1)}\n";
```

```text
15 -1
3 3221225472
```

19. This example shows the same rules at 128 bits:

```cint shift_i128.ci
I128 one = 1;
I128 v = one << 100;
"{v} {v >> 99} {one <<% 127}\n";
```

```text
1267650600228229401496703205376 2 -170141183460469231731687303715884105728
```

20. This example shows a logical right shift of an `I128`, through `U128`:

```cint shift_u128.ci
I128 a = -8;
U128 b = (a as% U128) >> 124;
"{b} {a >> 124}\n";
```

```text
15 -1
```

21. This example shows a left shift past the top of `U128`. The count keeps its own type, `I64`:

```cint shift_u128_fault.ci
U128 top = U128.max;
U128 v = top << 1;
"{v}\n";
```

```text
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> shift_u128_fault.ci:2:14
   |
 2 | U128 v = top << 1;
   |              ^ shl.checked.u128
   |
   = operation: shl.checked.u128
   = operand-count: 2
   = operand: U128 340282366920938463463374607431768211455
   = operand: I64 1
   = exact:   680564733841876926926749214863536422910
   = limit:   U128 340282366920938463463374607431768211455
   ...
```

**Notes**

22. In C, a shift count equal to or greater than the width, or negative, is undefined behavior and x86 processors mask the count to 5 or 6 bits. In cint such a count faults. A left shift of a negative signed value, undefined in C, is the product `a * 2^k` in cint, checked like `*`.
23. A logical right shift of a signed value converts it to the unsigned type of the same width with `as%` (6.6) first: `(a as% U64) >> k` for `I64`, and `U128` to `U512` for `I128` to `I512`.
24. The message of `E_SHIFT` writes the valid counts as `0..w-1`, meaning 0 to `w - 1` inclusive.

<a id="bitwise-operators"></a>

#### 6.7.9 Bitwise operators

`&`, `|` and `^` combine the bits of two integers of one type: and, or, and exclusive or. They work on the two's complement pattern of the type's width and do not fault.

**Syntax**

```ebnf
bor_expr  = bxor_expr { "|" bxor_expr } ;
bxor_expr = band_expr { "^" band_expr } ;
band_expr = shift_expr { "&" shift_expr } ;
```

**Constraints**

1. <a id="IM-47"></a>Both operands shall have the same integer type, after a literal operand takes its type from the other operand (6.5); mixing types is diagnosed C2001 [IM-47, LS-156].
2. The bitwise operators are not defined on `Bool`, where `&&`, `||` and `!` apply (6.7.11) or on fixed point, whose raw values `.raw` gives (6.3.2). Such an operand is diagnosed C2103 [IM-47].
3. Two different bitwise operators, or a bitwise operator with an arithmetic operand, need parentheses (6.7.2).

**Semantics**

4. `a & b`, `a | b` and `a ^ b` apply and or and exclusive or to each bit of the two's complement patterns of `a` and `b`, of width `w` and give the value of type `T` with the resulting pattern. They never fault [IM-47].
5. `&` binds tighter than `^`, and `^` tighter than `|`; all three bind tighter than the comparisons (6.7.1).

**Examples**

6. This example shows the three operators, and the patterns of a signed wide type:

```cint bitwise.ci
U8 a = 0b1100;
U8 b = 0b1010;
I128 big = I128.max;
"{a & b} {a | b} {a ^ b}\n";
"{~big} {big & 255} {big ^ big}\n";
```

```text
8 14 6
-170141183460469231731687303715884105728 255 0
```

7. This example shows `&` on `Bool`:

```cint bitwise_bool.ci
Bool a = true;
Bool b = false;
Bool c = a & b;
"{c}\n";
```

```text
error[C2103]: operator not defined for the operand type
  --> bitwise_bool.ci:3:10
   |
 3 | Bool c = a & b;
   |          ^
```

**Notes**

8. In C, `&` and `|` accept `bool` operands and do not short-circuit. In cint a program writes `&&` or `||` for conditions.

<a id="comparisons"></a>

#### 6.7.10 Comparisons

`==`, `!=`, `<`, `<=`, `>` and `>=` compare two values of one type and give a `Bool`. They compare mathematical values, not bit patterns, and they do not fault.

**Syntax**

```ebnf
cmp_expr = bor_expr [ cmp_op bor_expr ] ;
cmp_op   = "==" | "!=" | "<" | "<=" | ">" | ">=" ;
```

**Constraints**

1. <a id="IM-48"></a>Both operands shall have the same type, after a literal operand takes the type of the other operand (6.5). Comparing `I64` with `U64` or with `I32` is diagnosed C2001; a literal that does not fit the other operand's type is diagnosed C2003 [IM-48, LS-153].
2. `Bool` operands take only `==` and `!=`; the four ordering operators on `Bool` are diagnosed C2103 [IM-48].
3. Values of an error set take only `==` and `!=` (6.3.7). Arrays, structs, arenas, pools and handles have no comparison operators (6.12, 6.3.6, 6.14).
4. A comparison does not chain (6.7.2).

**Semantics**

5. A comparison compares the mathematical values of its operands and gives `true` or `false`. It never faults [IM-48].
6. Values of one fixed-point type compare by their raw values, which orders them as their values are ordered [IM-48].

**Examples**

7. This example shows comparisons at the edges of the range:

```cint compare.ci
Bool a = true;
Bool b = false;
I64 m = I64.min;
U64 u = U64.max;
"{a == b} {a != b}\n";
"{m < 0} {u > 0} {u == 18446744073709551615}\n";
```

```text
false true
true true true
```

8. This example shows a signed and an unsigned operand:

```cint compare_mixed.ci
I64 a = 5;
U64 b = 5;
Bool c = a < b;
"{c}\n";
```

```text
error[C2001]: type mismatch
  --> compare_mixed.ci:3:14
   |
 3 | Bool c = a < b;
   |              ^
```

9. The literal takes the type `I8` from `x`, and 1000 is not an `I8`. `(x as I16) < 1000` is the comparison the program means:

```cint compare_literal.ci
I8 x = 0;
Bool b = x < 1000;
"{b}\n";
```

```text
error[C2003]: literal does not fit its type
  --> compare_literal.ci:2:14
   |
 2 | Bool b = x < 1000;
   |              ^
```

10. This example shows fixed-point comparisons, with a literal operand:

```cint compare_fixed.ci
Q16.16 a = -0.5;
Q16.16 b = 0.25;
"{a < b} {a == b} {a >= b} {b < 1}\n";
```

```text
true false false true
```

**Notes**

11. In C, comparing a signed with an unsigned operand converts the signed one, so `-1 < 0u` is false. cint rejects the mixed comparison, and the program states the conversion it wants.

<a id="logical-operators"></a>

#### 6.7.11 Logical operators

`&&` and `||` combine two conditions. Each evaluates its right operand only when the left operand does not already decide the result, so the right operand can rely on what the left one tested. `!` is in 6.7.5.

**Syntax**

```ebnf
or_expr  = and_expr { "||" and_expr } ;
and_expr = cmp_expr { "&&" cmp_expr } ;
```

**Constraints**

1. Each operand shall have type `Bool` (6.3.3); an operand of another type is diagnosed C2002. An integer `x` is tested as `x != 0`.
2. `&&` and `||` in one expression need parentheses (6.7.2).

**Semantics**

3. `a && b` is `true` when both operands are `true`. `b` is evaluated only when `a` is `true` (6.2.4).
4. `a || b` is `true` when either operand is `true`. `b` is evaluated only when `a` is `false` (6.2.4).

**Examples**

5. The right operands divide by `b` only when `b` is not 0:

```cint logical.ci
I64 a = 5;
I64 b = 0;
Bool ok = b != 0 && a / b > 1;
Bool ok2 = b == 0 || a / b > 1;
"{ok} {ok2}\n";
```

```text
false true
```

6. An integer is not a condition:

```cint logical_int.ci
I64 a = 5;
Bool ok = a && true;
"{ok}\n";
```

```text
error[C2002]: condition is not `Bool`
  --> logical_int.ci:2:11
   |
 2 | Bool ok = a && true;
   |           ^
```

<a id="result-forms"></a>

#### 6.7.12 Result forms

A result form computes what a checked operator computes, but where the operator would fault it returns an error instead. A program uses one where an overflow, a divisor of 0 or a bad shift count is an expected outcome that it handles: the result is an error union (6.3.7), and the program consumes it with `try` or `catch` (6.11).

**Syntax**

The result forms are calls of built-in operations (6.7.4): `add_result(a, b)`, `sub_result(a, b)`, `mul_result(a, b)`, `div_result(a, b)`, `rem_result(a, b)` and `shl_result(a, k)`, and on fixed-point operands `mul_result(a, b, mode)` and `div_result(a, b, mode)`, where `mode` names a rounding mode (6.7.15). The conversion `x as? T` is the result form of `as` (6.6).

**Constraints**

1. <a id="IM-28"></a>The operands shall have the types the checked operator requires: two integers of one type, a literal operand taking its type from the other operand and two literal operands having type `I64`; for `shl_result`, a left operand of an integer type and a count of any integer type. `add_result`, `sub_result`, `mul_result` and `div_result` also take two operands of one fixed-point type. Two types are diagnosed C2001, and a `Bool` operand C2103. `rem_result` and `shl_result` on fixed-point operands, and a mode argument on integer operands, are diagnosed C2103 [IM-29, IM-28, IM-60].
2. A result form has the type `ArithError!T`, where `T` is the operand type. Like every error union it shall be consumed by `try`, by `catch` or by `_ =` (6.11); a result form used in any other way is diagnosed C2040 [IM-30].

**Semantics**

3. <a id="IM-188"></a>`ArithError` is a built-in error set (6.3.7) with four values, in this order: `overflow`, `div_zero`, `shift` and `narrow`. Its values are numbered 1 to 4 in that order, with an underlying type `U16`. Each value mirrors one fault code: `overflow` mirrors `E_OVERFLOW`, `div_zero` `E_DIV_ZERO`, `shift` `E_SHIFT`, and `narrow` `E_NARROW` [IM-188].
4. A result form holds the value that its checked operation gives when that operation does not fault, and otherwise the `ArithError` value that mirrors the fault the operation would raise. It never faults [IM-30, IM-188]:

| Result form | Checked operation | Errors it can hold |
|---|---|---|
| `add_result(a, b)` | `a + b` | `overflow` |
| `sub_result(a, b)` | `a - b` | `overflow` |
| `mul_result(a, b)` | `a * b` | `overflow` |
| `div_result(a, b)` | `a / b` | `div_zero`, `overflow` |
| `rem_result(a, b)` | `a % b` | `div_zero` |
| `shl_result(a, k)` | `a << k` | `shift`, `overflow` |
| `x as? T` | `x as T` (6.6) | `narrow` |

5. The result form of a negation is `sub_result(0, a)` [IM-28].
6. On fixed-point operands, `add_result` and `sub_result` act on the raw values as `+` and `-` do, and `mul_result` and `div_result` round once as `*` and `/` do, by the mode argument or by `half_even` when there is none (6.7.6, 6.7.7). Each holds `overflow` where its operator would fault `E_OVERFLOW`, and `div_result` holds `div_zero` for a divisor of 0 [IM-60, IM-62].

**Examples**

7. This example shows `catch` with a fallback value:

```cint result_catch.ci
I64 safe_add(I64 a, I64 b) {
    I64 r = add_result(a, b) catch 0;
    return r;
}
"{safe_add(1, 2)} {safe_add(I64.max, 1)}\n";
```

```text
3 0
```

8. This example shows binding the error with `catch (e)` and switching on it. A `switch` without `default` lists all four values (6.10.5):

```cint result_switch.ci
void show(ArithError e) {
    switch (e) {
        case .overflow: "overflow\n";
        case .div_zero: "div_zero\n";
        case .shift: "shift\n";
        case .narrow: "narrow\n";
    }
}
void try_div(I64 a, I64 b) {
    I64 q = div_result(a, b) catch (e) {
        show(e);
        return;
    };
    "{q}\n";
}
try_div(7, 2);
try_div(7, 0);
try_div(I64.min, -1);
```

```text
3
div_zero
overflow
```

9. `try` passes the error to the caller of a function whose result is an error union:

```cint result_try.ci
ArithError!I64 mean(I64 total, I64 n) {
    I64 q = try div_result(total, n);
    return q;
}
I64 a = mean(10, 3) catch -1;
I64 b = mean(10, 0) catch -1;
"{a} {b}\n";
```

```text
3 -1
```

10. Each form holds what its checked operation gives where that operation does not fault: `rem_result(I64.min, -1)` holds 0, as `%` gives 0:

```cint result_values.ci
I64 a = rem_result(I64.min, -1) catch 99;
I64 b = shl_result(1, 64) catch 99;
U8 c = sub_result(3 as U8, 5 as U8) catch 0;
I64 d = sub_result(0, I64.min) catch 7;
"{a} {b} {c} {d}\n";
```

```text
0 99 0 7
```

11. On fixed-point operands, `div_result` takes a rounding mode, and each form holds `overflow` or `div_zero` where its operator would fault:

```cint result_fixed.ci
Q16.16 one = 1.0;
Q16.16 three = 3.0;
Q16.16 rate = 1.5;
Q16.16 big = 30000.0;
Q16.16 a = div_result(one, three, floor) catch 0.0;
Q16.16 b = div_result(one, three, ceil) catch 0.0;
Q16.16 c = mul_result(big, rate) catch Q16.16.max;
Q16.16 d = div_result(rate, 0.0) catch -1.0;
Q16.16 e = add_result(big, big) catch Q16.16.min;
"{a} {b}\n{c} {d} {e}\n";
```

```text
0.3333282470703125 0.333343505859375
32767.9999847412109375 -1.0 -32768.0
```

12. This example shows a result form whose error is not consumed:

```cint result_unused.ci
I64 a = 5;
I64 b = add_result(a, 1);
"{b}\n";
```

```text
error[C2040]: type rule
  --> result_unused.ci:2:9
   |
 2 | I64 b = add_result(a, 1);
   |         ^
```

**Notes**

13. A fault stops the run, and program code cannot catch it (5.2.3). A result form is how a program tests for an arithmetic failure that it expects, without stopping.

<a id="conditional-expression"></a>

#### 6.7.13 Conditional expression

`c ? x : y` gives `x` when the condition `c` is true and `y` otherwise, and evaluates only the operand it gives.

**Syntax**

```ebnf
cond_expr = or_expr [ "?" expr ":" cond_expr ] ;
```

**Constraints**

1. <a id="LS-154"></a>The condition `c` shall have type `Bool`; another type is diagnosed C2002 [LS-154].
2. `x` and `y` shall have the same type; two types are diagnosed C2001. A literal operand takes the type of the other operand, and when both are literals they take the type of the context (6.5) [LS-154].

**Semantics**

3. `c ? x : y` evaluates `c`, then only the operand it selects: `x` when `c` is `true` and `y` when `c` is `false` [LS-154].
4. The conditional expression groups from right to left, as 6.7.1 states: `p ? x : q ? y : z` is `p ? x : (q ? y : z)`.

**Examples**

5. Only the selected operand runs:

```cint cond.ci
I64 pick(I64 k) {
    "pick {k}\n";
    return k;
}
Bool yes = true;
Bool no = false;
I64 v = yes ? pick(1) : pick(2);
I64 w = no ? 1 : no ? 2 : 3;
"{v} {w}\n";
```

```text
pick 1
1 3
```

6. A division by 0 in the operand that is not selected does not run:

```cint cond_guard.ci
Bool c = false;
I64 z = 0;
U8 v = c ? 1 : 200;
I64 w = c ? 10 / z : 3;
"{v} {w}\n";
```

```text
200 3
```

7. This example shows operands of two types:

```cint cond_types.ci
Bool c = true;
I64 a = 1;
I32 b = 2;
I64 v = c ? a : b;
"{v}\n";
```

```text
error[C2001]: type mismatch
  --> cond_types.ci:4:17
   |
 4 | I64 v = c ? a : b;
   |                 ^
```

<a id="assignment"></a>

#### 6.7.14 Assignment, compound assignment, `++` and `--`

An assignment stores a value in a place. A compound assignment such as `x += y` combines the place's value with another value and stores the result, and `x++` and `x--` add or subtract 1. All of them are statements: none has a value, and none can appear inside an expression.

**Syntax**

```ebnf
simple_stmt = place assign_op expr
            | place ( "++" | "--" )
            | [ "try" ] call ;
place       = IDENT { "." IDENT | "[" index_list "]" } ;
assign_op   = "=" | "+=" | "-=" | "*=" | "/=" | "%=" | "<<=" | ">>=" | "&=" | "|=" | "^="
            | "+%=" | "-%=" | "*%=" | "<<%=" | "+|=" | "-|=" | "*|=" ;
```

**Constraints**

1. <a id="LS-164"></a><a id="LS-166"></a>Assignment, compound assignment, `++` and `--` are statements, not expressions. An assignment written as the condition of an `if` or a `while` is diagnosed C1041, `++` or `--` inside an expression C1042, and an assignment inside any other expression is a syntax error (C1050) [LS-164, LS-166].
2. The left side shall be a *place*: a variable, an element of an array or view, or a field of a struct, that the program may write. Assigning to a `const` is diagnosed C2058; the parameters that a function may not write are in 6.9.5.
3. In `p = e`, the value of `e` shall have the type of `p`, a literal taking that type (6.5); another type is diagnosed C2001.
4. <a id="LS-165"></a>A compound form exists for every binary arithmetic and bitwise operator: `+=`, `-=`, `*=`, `/=`, `%=`, `<<=`, `>>=`, `&=`, `|=`, `^=`, `+%=`, `-%=`, `*%=`, `<<%=`, `+|=`, `-|=` and `*|=`. `p op= e` has the constraints of `p op e` (6.7.6 to 6.7.9) [LS-165].
5. The place of `++` and `--` shall have an integer type; a `Bool` place is diagnosed C2103 [LS-166].

**Semantics**

6. `p = e` evaluates and checks the subexpressions of the place, such as its indices, then evaluates `e`, then stores the value (6.2.4).
7. `p op= e` means `p = p op e`, except that the place `p` is evaluated once [LS-165].
8. `p++` means `p += 1`, and `p--` means `p -= 1`. Both are checked: at the type's limit they fault [LS-166].
9. A statement that faults does not store (6.2.4).

**Faults**

10. A compound assignment raises the faults of its operator (6.7.6 to 6.7.9), with that operator's operation identifier, at the position of the compound operator token: `x += 1` with `x` equal to `I8.max` records `add.checked.i8`, with operands `x` and 1 [LS-165].
11. `p++` and `p--` raise `E_OVERFLOW` at the type's limit. The record holds `add.checked.t` or `sub.checked.t`, with the operands `p` and 1, the exact result and the limit, and its position is the `++` or `--` token [LS-166].

**Examples**

12. This example shows every compound form:

```cint compound.ci
I64 x = 10;
x += 5;
"{x} ";
x -= 3;
"{x} ";
x *= 2;
"{x} ";
x /= 5;
"{x} ";
x %= 3;
"{x} ";
x <<= 4;
"{x} ";
x >>= 1;
"{x} ";
x &= 12;
"{x} ";
x |= 3;
"{x} ";
x ^= 5;
"{x}\n";
U8 u = 250;
u +%= 10;
"{u} ";
u -%= 20;
"{u} ";
u *%= 3;
"{u} ";
u <<%= 4;
"{u} ";
u +|= 250;
"{u} ";
u -|= 251;
"{u} ";
u = 20;
u *|= 20;
"{u}\n";
```

```text
15 12 24 4 1 16 8 8 11 14
4 240 208 0 250 0 255
```

13. The place of a compound assignment is evaluated once, so `idx` runs once:

```cint compound_place.ci
I64 idx(I64 k) {
    "idx({k}) ";
    return k;
}
void main() {
    I64[3] a = [10, 20, 30];
    a[idx(1)] += 5;
    "{a[1]}\n";
}
```

```text
idx(1) 25
```

14. This example shows `++` at the top of `I8`:

```cint incr_fault.ci
I8 i = 126;
i++;
"{i}\n";
i++;
"{i}\n";
```

```text
127
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> incr_fault.ci:4:2
   |
 4 | i++;
   |  ^ add.checked.i8
   |
   = operation: add.checked.i8
   = operand-count: 2
   = operand: I8 127
   = operand: I8 1
   = exact:   128
   = limit:   I8 127
   ...
```

15. This example shows an assignment where a condition belongs:

```cint assign_cond.ci
I64 x = 1;
if (x = 5) {
    "five\n";
}
```

```text
error[C1041]: assignment in a condition
  --> assign_cond.ci:2:5
   |
 2 | if (x = 5) {
   |     ^
```

16. This example shows `++` inside an expression:

```cint incr_expr.ci
void main() {
    I64[3] a = [1, 2, 3];
    I64 i = 0;
    a[i++] = 5;
}
```

```text
error[C1042]: `++` or `--` used as an expression
  --> incr_expr.ci:4:8
   |
 4 |     a[i++] = 5;
   |        ^
```

17. Compound assignment on fixed point rounds as the operator does:

```cint compound_fixed.ci
Q16.16 q = 1.5;
q *= q;
"{q}\n";
q /= 2;
"{q}\n";
q += 0.25;
"{q}\n";
```

```text
2.25
1.125
1.375
```

**Notes**

18. In C, an assignment is an expression with a value and `i++` yields the old value, so `if (x = 5)` and `a[i++] = a[i]` compile. Neither can be written in cint.

<a id="rounding-modes-and-the-round-clause"></a>

#### 6.7.15 Rounding modes and the `round` clause

Where an operation must turn an exact rational value into an integer, or into a value on the grid of a fixed-point type, it rounds once, by a rounding mode that the program can name. cint has nine modes. `half_even` is the default where a mode may be left out, and no operation rounds a value that is already rounded.

**Syntax**

```ebnf
round_clause = "round" IDENT ;   (* IDENT is a mode name *)
```

`round` is recognized only after the target type of a conversion and after a fraction literal; elsewhere it is an ordinary name (6.4.4).

**Constraints**

1. <a id="IM-42"></a>The name after `round`, and the name in the mode argument of a built-in operation, shall be one of the nine mode names of the table below. Any other name is diagnosed C3007 [IM-42].
2. A `round` clause may follow the target type of a conversion that rounds (6.6) and a fraction literal (6.4.6). After a conversion that does not round it is diagnosed C2009 (6.6).

**Semantics**

3. The modes round a rational `x` as follows; the last five columns give the result for five values of `x` [IM-42]:

| Mode | Rounds `x` to | 2.5 | -2.5 | 2.4 | -2.6 | 3.5 |
|---|---|---:|---:|---:|---:|---:|
| `floor` | the integer toward negative infinity | 2 | -3 | 2 | -3 | 3 |
| `ceil` | the integer toward positive infinity | 3 | -2 | 3 | -2 | 4 |
| `trunc` | the integer toward 0 | 2 | -2 | 2 | -2 | 3 |
| `away` | the integer away from 0 | 3 | -3 | 3 | -3 | 4 |
| `half_even` | the nearest integer; a tie to the even one | 2 | -2 | 2 | -3 | 4 |
| `half_away` | the nearest integer; a tie away from 0 | 3 | -3 | 2 | -3 | 4 |
| `half_trunc` | the nearest integer; a tie toward 0 | 2 | -2 | 2 | -3 | 3 |
| `half_up` | the nearest integer; a tie toward positive infinity | 3 | -2 | 2 | -3 | 4 |
| `half_down` | the nearest integer; a tie toward negative infinity | 2 | -3 | 2 | -3 | 3 |

4. <a id="IM-43"></a>Every operation that rounds names its mode: in a `round` clause, in a mode argument, or by default. Where the mode may be left out, it is `half_even` [IM-42, IM-43].
5. An operation rounds once, from its exact rational result, before its range check. No operation rounds an already rounded intermediate, so the result is the one the table gives for the exact value [IM-43].
6. When a rounding operation faults, the exact result in its record is the rounded value, and its operation identifier ends with the mode it used, written or defaulted: `mul.checked.q16_16.half_even`, `div_round.checked.i64.floor` (8.1).
7. The operations that round are: fixed-point `*` and `/`, and the built-in operations `mul`, `div`, `mul_wrap` and `mul_sat` (6.7.7); `div_round` (6.7.7); a conversion from fixed point to an integer type, or to a fixed-point type with fewer fraction bits (6.6); a fraction literal that is not on its type's grid (6.4.6); and the built-in operations of clause 7 that take a mode, `muldiv`, `isqrt_round`, the fixed-point `sqrt`, `qmul` and the fixed-point `dot`.

**Examples**

8. `div_round` under each mode, for the five values of the table: 5 / 2 is 2.5, 12 / 5 is 2.4, -13 / 5 is -2.6 and 7 / 2 is 3.5:

```cint round_modes.ci
void row(I64 a, I64 b) {
    "{a:>3} / {b}: {div_round(a, b, floor):>2} {div_round(a, b, ceil):>2} {div_round(a, b, trunc):>2}";
    " {div_round(a, b, away):>2} {div_round(a, b, half_even):>2} {div_round(a, b, half_away):>2}";
    " {div_round(a, b, half_trunc):>2} {div_round(a, b, half_up):>2} {div_round(a, b, half_down):>2}\n";
}
row(5, 2);
row(-5, 2);
row(12, 5);
row(-13, 5);
row(7, 2);
```

```text
  5 / 2:  2  3  2  3  2  3  2  3  2
 -5 / 2: -3 -2 -2 -3 -2 -3 -2 -2 -3
 12 / 5:  2  3  2  3  2  2  2  2  2
-13 / 5: -3 -2 -2 -3 -3 -3 -3 -3 -3
  7 / 2:  3  4  3  4  4  4  3  4  3
```

9. `round` clauses on conversions and on fraction literals. -2.5 converts to -2 by default (`half_even`), and 0.1 is raw 6553.6 in `Q16.16`, which `half_even` rounds to 6554:

```cint round_clause.ci
Q16.16 x = -2.5;
I32 a = x as I32;
I32 b = x as I32 round floor;
I32 c = x as I32 round half_away;
Q16.16 t = 0.1 round half_even;
Q16.16 u = -0.1 round ceil;
"{a} {b} {c} {t.raw} {u.raw}\n";
```

```text
-2 -3 -3 6554 -6553
```

10. This example shows a rounding operation that faults. Its identifier ends with the mode named in the call, and the exact result is the raw value after that one rounding:

```cint round_fault.ci
Q16.16 a = 256.0;
Q16.16 c = mul(a, a, floor);
"{c}\n";
```

```text
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> round_fault.ci:2:12
   |
 2 | Q16.16 c = mul(a, a, floor);
   |            ^ mul.checked.q16_16.floor
   |
   = operation: mul.checked.q16_16.floor
   = operand-count: 2
   = operand: Q16.16 raw 16777216
   = operand: Q16.16 raw 16777216
   = exact:   4294967296
   = limit:   Q16.16 raw 2147483647
   ...
```

11. This example shows a name that is not a mode:

```cint round_name.ci
I64 a = 7;
I64 b = 2;
I64 v = div_round(a, b, nearest);
"{v}\n";
```

```text
error[C3007]: undefined or unusable name
  --> round_name.ci:3:25
   |
 3 | I64 v = div_round(a, b, nearest);
   |                         ^
```

**Notes**

12. `half_even` is also the default rounding of IEEE 754 binary floating point. `trunc` is the rounding of C's integer division and of C's conversion from a floating type to an integer type.

<a id="constant-expressions-and-compile-time-evaluation"></a>

### 6.8 Constant expressions and compile-time evaluation

Some values are computed while the program is translated: the initializer of a `const` declaration, the extent of a module-level array, a `case` item, the condition of `static_assert` and every maximal constant expression (paragraph 4). This *compile-time evaluation* uses the operators and the types of a run. A constant expression is typed first and then evaluated with checked, wrapping or saturating arithmetic exactly as it would be at run time, so a computation gives the same value whether it is done at translation or at run time. A fault at translation is reported as the diagnostic C6001, with the fault's operands.

**Constraints**

1. <a id="LS-264"></a>The following shall be constant expressions: the initializer of a `const` declaration (6.9.2); the initializer of a module-level variable of an importable module (6.9.3); an extent of a module-level array (6.12.1, 6.12.6) and of an array in a kernel body (6.13.3); a `case` item (6.10.5); the condition of `static_assert` (6.10.9); and the capacity of an arena or a pool (6.14.1). Anything else in these places is diagnosed C6004, except in an extent of an array in a kernel body, where it is diagnosed C9102 (6.13.3) [LS-264].

**Semantics**

2. <a id="IM-23"></a>A *constant expression* is an expression built only from literals, names declared with `const`, the type properties `T.min`, `T.max` and `Q.epsilon`, the operators of 6.7, the conversions `as`, `as%` and `as|`, the constructor `Q.raw(n)` and the named operations of 7.2, each with constant operands. A variable, a function call, the member `.raw`, `as?` and `catch` are not constant expressions [IM-23].

3. <a id="IM-103"></a><a id="LS-265"></a>A constant expression is typed by the rules of 6.5, exactly as if it were evaluated at run time, and is then evaluated with the meaning of 6.6, 6.7 and 7.2: the checked, wrapping and saturating forms, the rules for shift counts and the rounding of fixed point apply unchanged. No expression is evaluated in Z as a whole: every intermediate result has the type of its operation, as at run time [IM-23, IM-103, LS-265].

4. The unit of compile-time evaluation is the *maximal constant expression*: a constant expression that is not an operand of a larger constant expression. Every maximal constant expression is evaluated at translation, wherever it appears, including inside an expression that is evaluated at run time [IM-23, LS-264].

5. Inside a maximal constant expression, `&&`, `||` and `?:` evaluate only the operands that their result requires, as at run time (6.2.4). An operand they skip is type-checked and its literals are checked against their types, but it is not evaluated: `false && (1 / 0 == 0)` is `false`, and `true ? 1 : 1 / 0` is 1 [IM-23].

6. A constant operand that `&&`, `||` or `?:` of a non-constant expression may skip is not evaluated at translation. It is evaluated at run time when the run reaches it, and faults then if its evaluation faults. With `I64 x`, `x == 2 && (1 / 0 == 0)` is translated, and faults `E_DIV_ZERO` when it is evaluated with `x` equal to 2 [IM-23, LS-264].

7. A constant expression that its statement always evaluates is evaluated at translation, even when the statement is in a branch that a run does not reach [IM-23, LS-264].

8. A checked conversion whose source is a literal operand (6.6) is decided at translation wherever it appears, in an operand that `&&`, `||` or `?:` skips as well [IM-23].

9. When a program has several errors of this kind, the first in this order is reported. Evaluation runs in the order of 6.2.4, and each literal is checked against its type when it is evaluated: a literal that does not fit is C2003, and a fault is C6001. When evaluation ends without a fault, every literal of a skipped operand is checked against its type (C2003), and then the checked conversions of literal operands in skipped operands are decided, in source order [IM-23].

10. <a id="LS-268"></a>Compile-time evaluation reads no clock, no environment variable and no file. Its result depends only on the program's text [IM-103, LS-268].

11. <a id="LS-270"></a>Compile-time evaluation computes values and nothing else: it generates no code, and it inspects no declaration other than through `T.min`, `T.max` and `Q.epsilon` [LS-270].

**Faults**

12. A fault during compile-time evaluation is not raised at run time. The program is rejected with C6001 at the position the fault would have at run time, and the diagnostic reports the fault as a fault record would: its fault code, the operation identifier, the operands, the exact result and the limit [IM-23, IM-103, LS-265].

**Examples**

13. This example shows constants built from type properties, wrapping and saturating operators, conversions and named operations:

```cint const_expressions.ci
const I64 A = min(3, 4);
const I64 B = div_round(7, 2, half_even);
const I8 C = I8.max;
const I64 D = abs(-5) + isqrt(50);
const U8 E = 200 +| 100;
const I64 F = (300 as% U8) as I64;
"{A} {B} {C} {D} {E} {F}\n";
```

```text
3 4 127 12 255 44
```

14. The literals take the type `I8` from the declaration, so the first addition is done in `I8` and overflows. Evaluation stops there, and the literal 150, which does not fit `I8` either, is not reached:

```cint const_fault.ci
const I8 X = 100 + 100 - 150;
"{X}\n";
```

```text
error[C6001]: fault during constant evaluation
  --> const_fault.ci:1:18
   |
 1 | const I8 X = 100 + 100 - 150;
   |                  ^
   |
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> const_fault.ci:1:18
   |
 1 | const I8 X = 100 + 100 - 150;
   |                  ^ add.checked.i8
   |
   = operation: add.checked.i8
   = operand-count: 2
   = operand: I8 100
   = operand: I8 100
   = exact:   200
   = limit:   I8 127
   ...
```

15. In the other order the literal 150 is evaluated first, and it does not fit:

```cint const_order.ci
const I8 Y = 150 - 100 + 100;
"{Y}\n";
```

```text
error[C2003]: literal does not fit its type
  --> const_order.ci:1:14
   |
 1 | const I8 Y = 150 - 100 + 100;
   |              ^
```

16. The final value 1000000000 fits `I32`, but the product is computed in `I32` first, as it would be at run time:

```cint const_typed.ci
I32 v = 2_000_000_000 * 2 / 4;
"{v}\n";
```

```text
error[C6001]: fault during constant evaluation
  --> const_typed.ci:1:23
   |
 1 | I32 v = 2_000_000_000 * 2 / 4;
   |                       ^
   |
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> const_typed.ci:1:23
   |
 1 | I32 v = 2_000_000_000 * 2 / 4;
   |                       ^ mul.checked.i32
   |
   = operation: mul.checked.i32
   = operand-count: 2
   = operand: I32 2000000000
   = operand: I32 2
   = exact:   4000000000
   = limit:   I32 2147483647
   ...
```

17. This example shows computing in `I64` and converting the result once:

```cint const_widened.ci
I32 v = ((2_000_000_000 as I64) * 2 / 4) as I32;
"{v}\n";
```

```text
1000000000
```

18. Operands that `&&` and `?:` skip are not evaluated:

```cint const_skip.ci
const Bool B = false && (1 / 0 == 0);
const I64 T = true ? 1 : 1 / 0;
"{B} {T}\n";
```

```text
false 1
```

19. Here `x == 2` is not constant, so `1 / 0 == 0` is left to run time. The first call skips it; the second evaluates it and faults:

```cint const_runtime.ci
I64 check(I64 x) {
    Bool b = x == 2 && (1 / 0 == 0);
    if (b) {
        return 1;
    }
    return 0;
}
"{check(1)}\n";
"{check(2)}\n";
```

```text
0
fault[E_DIV_ZERO]: division by zero
  --> const_runtime.ci:2:27
   |
 2 |     Bool b = x == 2 && (1 / 0 == 0);
   |                           ^ div.checked.i64
   |
   = operation: div.checked.i64
   = operand-count: 2
   = operand: I64 1
   = operand: I64 0
   ...
```

20. `1 / 0` in a declaration is always evaluated by its statement, so the program is rejected although the call does not take the branch:

```cint const_unreached.ci
I64 check(I64 x) {
    if (x > 5) {
        I64 y = 1 / 0;
        return y;
    }
    return x;
}
"{check(1)}\n";
```

```text
error[C6001]: fault during constant evaluation
  --> const_unreached.ci:3:19
   |
 3 |         I64 y = 1 / 0;
   |                   ^
   |
fault[E_DIV_ZERO]: division by zero
  --> const_unreached.ci:3:19
   |
 3 |         I64 y = 1 / 0;
   |                   ^ div.checked.i64
   ...
```

21. A function call is not a constant expression:

```cint const_call.ci
I64 square(I64 x) {
    return x * x;
}
const I64 K = square(4);
"{K}\n";
```

```text
error[C6004]: constant expression required
  --> const_call.ci:4:15
   |
 4 | const I64 K = square(4);
   |               ^
```

22. Fixed-point constants round as they would at run time. `1.0 / 3.0` rounds to the nearest multiple of 2^-16 before the product, so three times it is not 1.0:

```cint const_fixed.ci
Q16.16 q = (1.0 / 3.0) * 3.0;
const Q16.16 THIRD = 1.0 / 3.0;
"{q} {q.raw} {THIRD.raw}\n";
```

```text
0.9999847412109375 65535 21845
```

**Notes**

23. A C compiler may fold `2000000000 * 2 / 4` to any value, because signed overflow in C is undefined behavior, and C requires a diagnostic only where a constant expression is required. In cint the folding is done in the declared type and reports the overflow with its operands, as a run would.

<a id="declarations"></a>

### 6.9 Declarations

A declaration introduces a name and says what it stands for: a variable, a constant, a function, a struct type or an error set. This subclause gives the form of each declaration, the rules checked when it is translated, and what it does when it runs. The types that declarations name are defined in 6.3, where a name is visible in 6.2.3, and the order in which expressions are evaluated in 6.2.4.

<a id="variables"></a>

#### 6.9.1 Variables

A variable declaration names an object, gives its type and gives its first value. A variable of an integer, fixed-point, `Bool` or error-set type always has an initializer, so no object is read before it holds a value (6.2.2). An array or struct variable without an initializer starts with every element and field equal to 0. A *view variable* names a window onto array storage that already exists.

**Syntax**

```ebnf
var_decl  = type IDENT [ "=" expr ] ";" ;
view_decl = ( "in" | "inout" ) type IDENT "=" expr ";" ;
```

**Constraints**

1. <a id="LS-104"></a>A variable of an integer type, of a fixed-point type, of `Bool` or of an error-set type shall have an initializer. A declaration without one is diagnosed C2050 at the declared name [LS-104].
2. An array or struct variable that holds error-set values needs an initializer (C2054, 6.3.7).
3. <a id="LS-106"></a>A view variable is declared with `in`, which gives read-only access, or `inout`, which gives read and write access, and shall have an initializer: an array or a view of the same element type and rank. Its type writes `_` for each extent, which the initializer supplies. An `inout` view variable shall be initialized from storage that may be written; a read-only source is diagnosed C2067 [LS-106].

**Semantics**

4. A declaration runs when control reaches it. Its initializer is evaluated and the result becomes the variable's value. An array or struct variable without an initializer starts with every integer element and field equal to 0 and every `Bool` equal to `false`, so every read of every object returns a value the program wrote or this rule gave [LS-104].
5. <a id="LS-109"></a>A local variable may be assigned after its declaration. Parameters are read-only unless their mode is `inout` (6.9.5) [LS-109].
6. A view variable designates the storage of its initializer. Reading an element through the view reads that storage, and writing through an `inout` view writes it, so a write to the array after the declaration is visible through the view [LS-106].
7. <a id="LS-107"></a>Assigning to a view variable copies the elements of the right side into the storage the view designates. It does not make the view designate other storage. The two sides shall have the same shape, as 6.12.5 states for every copy [LS-107].

**Examples**

8. Scalars need an initializer. Arrays and structs without one start at 0:

```cint decl_zero.ci
struct Point { I64 x; I64 y; }

void main() {
    I64 ticks = 3;
    I32[4] window;
    Point p;
    Bool ready = window[0] == 0;
    "{ticks} {window[0]} {window[3]} {p.x} {p.y} {ready}\n";
}
```

```text
3 0 0 0 0 true
```

9. A scalar declared without a value is rejected:

```cint decl_uninit.ci
I64 total;
"{total}\n";
```

```text
error[C2050]: uninitialized variable
  --> decl_uninit.ci:1:5
   |
 1 | I64 total;
   |     ^
```

10. A view variable reads and writes the array it was made from, and assignment to it copies elements:

```cint decl_view.ci
void demo() {
    I64[6] data;
    for i in 0..6 {
        data[i] = i * 10;
    }
    in I64[_] head = data[0..3];
    inout I64[_] tail = data[3..];
    tail[0] = -1;
    "{head[2]} {data[3]}\n";
    I64[3] fresh;
    tail = fresh;
    "{data[3]} {data[5]} {len(tail)}\n";
}

demo();
```

```text
20 -1
0 0 3
```

**Notes**

11. C leaves an automatic variable without an initializer indeterminate. In cint a scalar without one is rejected, and an array or struct without one starts at 0.

<a id="constants"></a>

#### 6.9.2 Constants

A constant names a value that is computed once, during translation, and does not change. Every constant has a declared type, such as `I64`, `U8` or `Bool`. A constant can be used wherever a constant expression is required (6.8), such as an array extent or a `case` label. A constant declared at the top level of a module is visible in the whole module, before and after its declaration (6.2.3).

**Syntax**

```ebnf
const_decl = "const" type IDENT "=" expr ";" ;
```

**Constraints**

1. <a id="LS-111"></a>The initializer shall be a constant expression of the declared type (6.8). It may name other constants, in any order, but a constant shall not depend on itself, directly or through other constants: C6005 [LS-111].
2. The initializer is evaluated during translation with the meaning it would have at run time. When that evaluation faults, the program is diagnosed C6001 at the faulting operator, and the diagnostic carries the fault's code, operation, operands, exact result and limit [LS-111].
3. A constant shall not be assigned: C2058 [LS-111].

**Semantics**

4. A use of a constant gives the value of its initializer [LS-111].

**Examples**

5. Constants may refer to each other and be used before their declaration:

```cint const_order.ci
I64 area() {
    return WIDTH * HEIGHT;
}

const I64 WIDTH = 3 * SCALE;
const I64 HEIGHT = 4 * SCALE;
const I64 SCALE = 1_000;

"{area()}\n";
```

```text
12000000
```

6. A constant can size an array and label a `case`:

```cint const_use.ci
const I64 SLOTS = 4;
const I64 LAST = SLOTS - 1;

void main() {
    I64[SLOTS] ring;
    for i in 0..SLOTS {
        ring[i] = i * i;
        switch (i) {
            case LAST: "last {ring[i]}\n";
            default: "slot {ring[i]}\n";
        }
    }
}
```

```text
slot 0
slot 1
slot 4
last 9
```

**Notes**

7. A constant takes the place of both C's `#define` of a value and C's `const` object: it has a type, it is checked like any other expression, and no program can write it.

<a id="module-level-variables"></a>

#### 6.9.3 Module-level variables

A variable declared at the top level of an importable module, or of a program that defines `main`, is a *module-level variable*: it exists as long as the context, keeps its value from one entry to the next (5.2.1), and every function of its module can use it. Its initial value is fixed during translation, so the order in which modules start cannot be observed. The top-level variables of a script are different: they are locals of the script's entry (5.1.2).

**Syntax**

```ebnf
top_item    = [ "export" ] declaration
            | test_decl
            | static_assert
            | statement ;
declaration = const_decl | var_decl | func_decl | kernel_decl
            | struct_decl | error_decl | memory_decl
            | extern_decl | requires_decl ;
```

`var_decl` is in 6.9.1, `memory_decl` in 6.14.1 and `export` in 6.15.2.

**Constraints**

1. <a id="LS-113"></a>The initializer of a module-level variable shall be a constant expression (6.8). An array literal or a struct constructor whose items are all constant expressions, nested literals and constructors included, is a constant initializer of a variable of its type. Any other initializer is diagnosed C6004 at its first character [LS-113].
2. The constant initializer is checked as the same initializer of a local variable would be, and a fault during its evaluation is diagnosed C6001 [LS-113].
3. An arena or a pool (6.14) is declared at the top level of the module, in a script as well as in an importable module, with a constant capacity; another capacity is diagnosed C6004 [LS-113].

**Semantics**

4. A module-level variable is created with the context, with the value of its initializer, or with every element and field equal to 0 when it has none. Its items are evaluated once, during translation, in source order [LS-113].
5. Module-level variables keep their values from one call and one entry to the next. A test starts with the initial values again (6.15.3) [LS-113].

**Examples**

6. A program with `main` keeps its state at module level:

```cint module_state.ci
I64 calls = 0;
I64[4] history;
I64[3] weights = [3, 2, 1];

void record(I64 v) {
    history[calls % 4] = v * weights[calls % 3];
    calls += 1;
}

void main() {
    record(10);
    record(20);
    record(30);
    "{calls} {history[0]} {history[1]} {history[2]} {history[3]}\n";
}
```

```text
3 30 40 30 0
```

7. An initializer that would run code is rejected. The file has only declarations, so it is an importable module, not a script:

```cint module_init.ci
I64 first() {
    return 7;
}

I64 start = first();

void main() {
    "{start}\n";
}
```

```text
error[C6004]: constant expression required
  --> module_init.ci:5:13
   |
 5 | I64 start = first();
   |             ^
```

<a id="functions"></a>

#### 6.9.4 Functions

A function is a named block of code with typed parameters and a typed result. Its form is C's: the result type, the name, optional size parameters in brackets, the parameters in parentheses and the body. Functions may be declared in any order and call each other freely; there are no prototypes and no header files.

**Syntax**

```ebnf
func_decl   = result_type IDENT [ size_params ] "(" [ params ] ")" block ;
result_type = "void" | base_type | union_type ;
union_type  = qualified "!" ( "void" | base_type ) ;
size_params = "[" IDENT { "," IDENT } "]" ;
params      = param { "," param } [ "," ] ;
param       = [ "in" | "inout" ] type IDENT ;
```

**Constraints**

1. <a id="LS-114"></a>The result type is `void`, a type or an error union `E!T` or `E!void`, where `E` names an error set (6.9.7, 6.11) [LS-114].
2. <a id="LS-116"></a>A function whose result is not `void` or `E!void` shall not be able to reach the closing brace of its body: C4001 at the function's name. A `switch` that covers every value and returns in every case counts as returning. Every loop counts as able to complete, whatever its condition, so a `while (true)` loop that returns still needs a `return` after it [LS-116].
3. <a id="LS-117"></a>Each size parameter shall be bound by the extent of at least one array or view parameter: C2065 at the size parameter [LS-117].
4. A function is called with parentheses, as 6.2.4 states (C4010, C4012).
5. A call whose function returns a result other than `void` uses that result or discards it with `_ =`, as 6.2.4 states (C4011). A call that returns an error union is consumed as 6.11 states.

**Semantics**

6. <a id="LS-115"></a>Functions may be declared before or after their uses, in any order, within the module [LS-115].
7. A *size parameter* is a name, bound at each call, for an extent of an array or view argument. Inside the body it is a read-only `I64`. When it appears in the type of several parameters, every extent it names shall be equal at the call. A mismatch that translation can see is diagnosed C2012; otherwise the call faults `E_SHAPE` before the body runs (6.9.5, paragraph 12) [LS-117].

**Examples**

8. A size parameter takes its value from the argument, so one function serves arrays of every length and slices of them:

```cint fn_size.ci
I64 total[n](in I64[n] a) {
    I64 t = 0;
    for i in 0..n {
        t += a[i];
    }
    return t;
}

void main() {
    I64[5] d;
    for i in 0..5 {
        d[i] = i + 1;
    }
    "{total(d)} {total(d[1..3])}\n";
}
```

```text
15 5
```

9. Translation does not follow conditions to decide whether a function returns:

```cint fn_noreturn.ci
I64 signum(I64 x) {
    if (x < 0) {
        return -1;
    } else if (x > 0) {
        return 1;
    }
}

"{signum(5)}\n";
```

```text
error[C4001]: missing return
  --> fn_noreturn.ci:1:5
   |
 1 | I64 signum(I64 x) {
   |     ^
```

10. A result that is neither used nor discarded is rejected:

```cint fn_unused.ci
I64 twice(I64 x) {
    return 2 * x;
}

twice(3);
_ = twice(4);
```

```text
error[C4011]: control-flow rule
  --> fn_unused.ci:5:1
   |
 5 | twice(3);
   | ^
```

**Notes**

11. A call evaluates its arguments, runs the entry checks of 6.9.5, checks the call depth and charges fuel (5.2.5, 5.2.4), and then runs the body. A function may call itself, as deep as the call-depth limit of the entry allows (5.2.5).
12. A function returns a scalar, a struct, a handle, an error-set value or an error union. To produce an array, it fills an `inout` array parameter (6.9.5, example 15).

<a id="parameters-and-arguments"></a>

#### 6.9.5 Parameters and arguments

Each parameter has a *mode* that says what the function may do with it. `in`, the default, gives read-only access; `inout` lets the function write the caller's array or struct. Arguments are matched to parameters by position or by name. Because an `inout` argument cannot overlap another argument of the same call, a function's reads and writes do not interfere, and an implementation may pass arguments by reference without changing a result.

**Syntax**

```ebnf
param = [ "in" | "inout" ] type IDENT ;
args  = arg { "," arg } [ "," ] ;
arg   = [ IDENT "=" ] expr ;
```

**Constraints**

1. <a id="LS-119"></a>A parameter without a mode has mode `in`. `inout` applies to arrays, views and structs; an `inout` parameter of an integer, `Bool` or error-set type is diagnosed C2064 [LS-119].
2. `out` is a parameter mode of kernels (6.13.2). A function writes an array through an `inout` parameter, and an `out` parameter of a function is diagnosed C5001 at the mode [LS-119].
3. <a id="LS-120"></a>A parameter is assigned, or written through, only when its mode is `inout`: C2060 otherwise [LS-120].
4. An `inout` argument shall be storage the caller may write: a variable, a field or element of one or a writable view. A read-only argument for an `inout` parameter is diagnosed C2067 [LS-119].
5. <a id="LS-126"></a>A call supplies exactly one argument for each parameter. Positional arguments come first, then *named arguments*, written `name = value`. Too few or too many arguments are diagnosed C2022; a parameter that receives two arguments is C2062; a name that is not a parameter is C2063; a positional argument after a named one is C2069 [LS-126].
6. <a id="LS-121"></a>An `inout` argument shall not overlap any other argument of the same call, read-only or writable. An overlap known during translation is diagnosed C5010; other overlaps are checked when the call is made (paragraph 13). Read-only arguments may overlap each other [LS-121].
7. <a id="LS-122"></a>An argument that is a module-level variable, or a view of one, shall not be passed in any mode to a function that writes that variable, directly or through any function it calls. A function writes a module-level variable when it assigns it, or calls a function that does, directly or through any chain of calls in the program. A call that breaks this rule is diagnosed C5012 at the argument [LS-122].

**Semantics**

8. An `in` argument is read-only for the duration of the call. A scalar is passed by value. For an array or struct, whether the function receives a copy or a reference cannot be observed, because constraints 6 and 7 rule out every write that could show the difference [LS-119, LS-122].
9. An `inout` argument is passed by reference: every write the function makes is visible to the caller when the call returns [LS-119].
10. Named arguments are matched to the parameters of the same name and evaluated in the order they are written, like all arguments (6.2.4) [LS-126].
11. <a id="LS-123"></a>When a call is made, after its arguments are evaluated and before fuel is charged, its *entry checks* run in this order, and the first that fails is the fault: the permission of each view, the element type and rank, the binding and agreement of shapes and the overlap of `inout` arguments. A handle argument is not among them: passing a handle does not check it, and a handle that is not valid faults with `E_STALE_HANDLE` at its first dereference in the callee (6.14.4) [LS-123].

**Faults**

12. The fault `E_SHAPE` is raised when two extents that bind the same size parameter differ. The record holds the operation identifier `bind.shape`; three `I64` operands, the index of the parameter whose extent disagrees (from 0), the dimension (from 0) and that extent; no exact result; and, as the limit, the extent that the size parameter was bound to first. Its position is the callee's name at the call, with the caller's stack [LS-117, LS-123].
13. The fault `E_ALIAS` is raised when an `inout` argument overlaps another argument. The record holds the operation identifier `bind.alias`; two `I64` operands, the index of the `inout` parameter (the lower index when both are `inout`) and the index of the parameter it overlaps; and no exact result and no limit. Its position is the callee's name at the call, with the caller's stack [LS-121].

**Examples**

14. A function writes an array through `inout`. The same function with `out` is rejected, because `out` is for kernels:

```cint param_out.ci
void squares[n](out I64[n] dst) {
    for i in 0..n {
        dst[i] = i * i;
    }
}

void main() {
    I64[4] sq;
    squares(sq);
    "{sq[3]}\n";
}
```

```text
error[C5001]: parameter mode, alias, or ABI rule
  --> param_out.ci:1:17
   |
 1 | void squares[n](out I64[n] dst) {
   |                 ^
```

15. With `inout` the function runs; a kernel, by contrast, declares the array it writes `out`:

```cint param_inout.ci
void squares[n](inout I64[n] dst) {
    for i in 0..n {
        dst[i] = i * i;
    }
}

kernel square_all[n](in I64[n] src, out I64[n] dst) {
    dst = src * src;
}

void main() {
    I64[4] sq;
    squares(sq);
    I64[4] again;
    square_all(sq, again);
    "{sq[3]} {again[3]}\n";
}
```

```text
9 81
```

16. An `inout` struct parameter lets a function update the caller's struct:

```cint param_struct.ci
struct Acc { I64 sum; I64 n; }

void add(inout Acc a, I64 v) {
    a.sum += v;
    a.n += 1;
}

void main() {
    Acc a = Acc(0, 0);
    add(a, 5);
    add(a, 7);
    "{a.sum} {a.n}\n";
}
```

```text
12 2
```

17. Named arguments may come in any order after the positional ones:

```cint param_named.ci
I64 sub(I64 a, I64 b) {
    return a - b;
}

I64 r = sub(b = 1, a = 10);
I64 s = sub(10, b = 3);
"{r} {s}\n";
```

```text
9 7
```

18. Two slices of one array that do not overlap may be passed together. Overlapping slices fault when the call is made, before the body runs:

```cint param_alias.ci
void add_into[n](inout I64[n] dst, in I64[n] src) {
    for i in 0..n {
        dst[i] += src[i];
    }
}

void main() {
    I64[6] a;
    for i in 0..6 {
        a[i] = i;
    }
    add_into(a[0..3], a[3..6]);
    "{a[0]} {a[1]} {a[2]}\n";
    add_into(a[0..3], a[2..5]);
}
```

```text
3 5 7
fault[E_ALIAS]: views overlap
  --> param_alias.ci:14:5
   |
14 |     add_into(a[0..3], a[2..5]);
   |     ^ bind.alias
   |
   = operation: bind.alias
   = operand-count: 2
   = operand: I64 0
   = operand: I64 1
   = exact:   none
   = limit:   none
   ...
```

19. The shape check comes before the overlap check. Here the slices overlap and also differ in length, and the fault is `E_SHAPE`: parameter 1, dimension 0, has extent 3 where `n` was bound to 4:

```cint param_shape.ci
void add_into[n](inout I64[n] dst, in I64[n] src) {
    for i in 0..n {
        dst[i] += src[i];
    }
}

void go(I64 k) {
    I64[6] x;
    add_into(x[0..k], x[2..5]);
}

void main() {
    go(4);
}
```

```text
fault[E_SHAPE]: shapes do not match
  --> param_shape.ci:9:5
   |
 9 |     add_into(x[0..k], x[2..5]);
   |     ^ bind.shape
   |
   = operation: bind.shape
   = operand-count: 3
   = operand: I64 1
   = operand: I64 0
   = operand: I64 3
   = exact:   none
   = limit:   I64 4
   ...
```

20. A function that writes a module-level array cannot also receive a view of it:

```cint param_global.ci
I64[8] g;

void bump(in I64[_] a) {
    g[0] = a[1] + 1;
}

void main() {
    bump(g[..]);
}
```

```text
error[C5012]: parameter mode, alias, or ABI rule
  --> param_global.ci:8:10
   |
 8 |     bump(g[..]);
   |          ^
```

**Notes**

21. In C a function writes through a pointer, and two pointer arguments may point into the same array, so a compiler must assume they alias. In cint the overlap is excluded by constraint 6, so a callee may assume that its writable parameters do not overlap, as C's `restrict` lets a C function assume.

<a id="structs"></a>

#### 6.9.6 Structs

A struct groups named fields of fixed types into one value. A struct declaration names the type and its fields; a struct value is made by calling the type's name like a function, with one argument for each field. A field is read and written with `.` (6.7.4): `p.x`, `o.left.v`, `b.cells[1]`. The struct type itself, its values and its copying are described in 6.3.6.

**Syntax**

```ebnf
struct_decl = "struct" IDENT "{" { field } "}" ;
field       = type IDENT ";" ;
call        = IDENT [ "." IDENT ] "(" [ args ] ")" ;
```

A constructor is a `call` (6.7.4) whose callee names a struct type.

**Constraints**

1. The fields of a struct shall have distinct names: C3001 at the second name [LS-112].
2. The types a field may have, and the rule that a struct does not contain itself, are stated in 6.3.6.
3. <a id="LS-77"></a>A constructor supplies every field exactly once, by position in declaration order or by name, under the rules of 6.9.5, constraint 5. A missing field is diagnosed C2020 at the constructor; extra arguments are C2022 [LS-77].

**Semantics**

4. A constructor evaluates all its arguments first, in the order written (6.2.4). It then checks the shape of each argument for an array field against the field, in field declaration order, and only then copies each argument into its field. The struct does not share storage with any argument [LS-77].
5. An argument for an array field shall have the field's shape. A mismatch that translation can see is diagnosed C2012. Otherwise the constructor faults `E_SHAPE` before it copies any field, with the operation identifier `copy.shape`, two `I64` operands, the lowest dimension that differs (from 0) and the argument's extent in it, no exact result and the field's extent as the limit. Its position is the constructor's name [LS-77].
6. A struct is copied by assignment, by initialization and by passing as an `in` argument, as 6.3.6 states.

**Examples**

7. This example shows positional and named constructors, nested structs and an array field:

```cint struct_use.ci
struct Inner { I64 v; }
struct Outer { Inner left; Inner right; I64[3] extra; }

Outer build(I64 k) {
    Outer o;
    o.left.v = k;
    o.right = Inner(v = k * 2);
    o.extra[2] = k * 3;
    return o;
}

void main() {
    Outer o = build(5);
    Outer other = o;
    other.left.v = 99;
    "{o.left.v} {o.right.v} {o.extra[2]} {other.left.v}\n";
}
```

```text
5 10 15 99
```

8. Every field must be supplied:

```cint struct_missing.ci
struct Point { I64 x; I64 y; }

void main() {
    Point a = Point(x = 3);
    "{a.x}\n";
}
```

```text
error[C2020]: wrong fields or arguments
  --> struct_missing.ci:4:15
   |
 4 |     Point a = Point(x = 3);
   |               ^
```

9. A constructor copies an array argument, so a change to the array after the constructor does not reach the struct:

```cint struct_copy.ci
struct Box { I64[3] cells; I32 tag; }

void main() {
    I64[3] init;
    init[1] = 5;
    Box b = Box(init, 7);
    init[1] = 6;
    "{b.cells[1]} {init[1]} {b.tag}\n";
}
```

```text
5 6 7
```

**Notes**

10. `Body{ ... }`, the brace initialization of C++, and C's `{ ... }` initializer are not constructors in cint; the constructor is a call of the type's name.
11. Structs have no `==` and no text form. A comparison of two structs is diagnosed C2103, and a struct in a format hole C1037 (7.5); a program compares or prints the fields.

<a id="error-sets"></a>

#### 6.9.7 Error sets

An *error set* declares the names of the expected failures a group of functions can report, such as `bad_digit` or `overdrawn`. Each name is a value of the set. A function reports one by returning it through an error union (6.11). Error sets can be combined, so that a function calling into several parts of a program can pass on the errors of all of them.

**Syntax**

```ebnf
error_decl = "error" IDENT [ ":" INT_TYPE ]
             ( "{" IDENT { "," IDENT } [ "," ] "}"
             | "=" qualified { "|" qualified } ";" ) ;
```

**Constraints**

1. The values of one set shall have distinct names: C3001 [LS-92].
2. The *underlying type*, written after `:`, is the unsigned integer type that holds the tags of the set's values (6.3.7); without it the type is `U16`. A signed underlying type, and a set with more values than its underlying type can number from 1, are diagnosed C2031 [LS-93].
3. In a *combined set*, `error C = A | B;`, each operand shall name an error set, itself combined or not; any other name is diagnosed C3007 [LS-97].

**Semantics**

4. The values of a set and their tags, the values of a combined set, the short forms of a value, the conversions between sets, and the comparison of error values are stated in 6.3.7.

**Examples**

5. This example shows error values stored in an array and tested with `switch`:

```cint errset_values.ci
error Stage { parse, check, emit }

void main() {
    Stage[3] seen = [Stage.emit, .parse, Stage.check];
    for i in 0..3 {
        switch (seen[i]) {
            case .parse: "parse ";
            case .check: "check ";
            case .emit: "emit ";
        }
    }
    "\n";
}
```

```text
emit parse check 
```

6. A combined set numbers the values of `IoError` first and those of `ParseError` after them. `try` converts the tag of `bad_digit` from 2 to 4, and the error that `main` returns is reported with its tag in the combined set (6.11.7):

```cint ingest.ci
error IoError : U8 { closed, full }
error ParseError { empty, bad_digit, too_large }
error IoOrParse = IoError | ParseError;

ParseError!I64 digit(I64 c) {
    if (c < 48 || c > 57) {
        return .bad_digit;
    }
    return c - 48;
}

IoOrParse!void main() {
    I64 d = try digit(55);
    "digit {d}\n";
    I64 e = try digit(120);
    "digit {e}\n";
}
```

```console
$ cint run ingest.ci
digit 7
error: ingest.IoOrParse.bad_digit (tag 4)
```

7. A name declared by two member sets must be written with its set:

```cint errset_ambiguous.ci
error IoError { closed, full }
error QueueError { full, stalled }
error IoOrQueue = IoError | QueueError;

IoOrQueue!I64 push() {
    return .full;
}

I64 v = push() catch 0;
"{v}\n";
```

```text
error[C2044]: type rule
  --> errset_ambiguous.ci:6:12
   |
 6 |     return .full;
   |            ^
```

**Notes**

8. An error set plays the part that `errno` values or negative return codes play in C, with two differences: the compiler knows which errors a function can return, and the caller cannot forget to handle one (6.11).

<a id="extern-declarations"></a>

#### 6.9.8 Extern declarations

An extern declaration names a function that a module calls but does not define. Code outside the program supplies it: the host program that loads the program, or the runtime, when the extern belongs to one of the standard services of 6.9.9. The declaration gives the function's parameters, its result and its *effect class*, which states what a call may do outside the program. cint source has no other way to read a clock, a file or the terminal: each of these reaches a program only through an extern.

**Syntax**

```ebnf
extern_decl   = [ attribute ] "extern" effect_class result_type IDENT
                "(" [ extern_params ] ")" ";" ;
effect_class  = "pure" | "observe" | "unrestricted" ;
extern_params = extern_param { "," extern_param } [ "," ] ;
extern_param  = [ "in" | "out" | "inout" ] type IDENT ;
```

`attribute` is in 6.9.9. `pure`, `observe` and `unrestricted` are matched by spelling after `extern`; elsewhere each is an ordinary identifier (6.4.4).

**Constraints**

1. <a id="LS-323"></a>An extern declaration is a top item of a module (A.4). It has no body: a block after its parameter list is a syntax error (C1050). It may be exported, and its name is a name of the module like a function's, declared once (C3001) [LS-323].
2. Each parameter of an extern is an integer, fixed-point or `Bool` scalar, a `Grants` value (6.9.9) or a view of rank 1 (6.3.5), and its result is `void`, such a scalar, or an error union of `void` or such a scalar. Any other parameter or result type is diagnosed C5050 at the parameter or the result [LS-323].
3. A parameter's mode is `in` when none is written. `out` and `inout` apply only to a view parameter: `out` gives the extern a view to write, as a kernel's `out` parameter does (6.13.2). A mode other than `in` on a scalar parameter is diagnosed C2064 at the mode [LS-323].
4. An extern is called as a function is, and its arguments are bound as 6.9.5 states. An argument for an `out` or `inout` view parameter shall be storage the caller may write: a read-only argument, such as a string literal, is diagnosed C2067 at the argument. An extern named without a call is diagnosed C4010 [LS-119, LS-323].
5. An extern is never called at translation: a constant expression that calls one is diagnosed C6004, as a call of a function is (6.8) [LS-323].

**Semantics**

6. <a id="LS-324"></a>The effect class of an extern is the one its declaration names [LS-324]:
   - `pure`: the result depends only on the arguments, and the call has no effect outside the program. A call is not recorded (8.4).
   - `observe`: the call reads or changes something outside the program, such as a clock, a file or the terminal. Each call, with its arguments and its result, is an event of a recording, so a replay can give the program the same results without calling the extern again (8.4).
   - `unrestricted`: the call may do anything, and a replay cannot reproduce it. A recording ends at the first call of an `unrestricted` extern (8.4).
7. Every function of a program has an effect class too, which translation computes and the source does not declare. It is the greatest, in the order `pure`, `observe`, `unrestricted`, of `pure` and the classes of the externs and functions the body calls, where a print statement (6.10.10) counts as a call of an `observe` extern. A function that calls only `pure` externs and prints nothing is `pure` [LS-324].
8. An extern declared without the attribute `@service` is a *host import*. The host program that loads the program supplies a function for each host import, and the loading fails when one is missing or its parameters, result or effect class differ from the declaration (SPEC-03P.2). An extern with `@service` belongs to a standard service, which the runtime supplies (6.9.9) [LS-323, LS-325].

**Examples**

9. This module declares two host imports and exports three functions. The reference prints the externs and the effect class of each function with `python3 -m cint_ref caps`:

```cint ext_classes.ci
extern observe I64 clock_ns();
extern pure I64 gain(I64 x);

export I64 boosted(I64 x) {
    return gain(x) * 2;
}

export I64 elapsed(I64 t0) {
    return clock_ns() - t0;
}

export void report(I64 x) {
    "x = {x}\n";
}
```

```console
$ python3 -m cint_ref caps ext_classes.ci
extern ext_classes.clock_ns observe
extern ext_classes.gain pure
effect ext_classes.boosted pure
effect ext_classes.elapsed observe
effect ext_classes.report observe
```

   `boosted` calls only a `pure` extern and stays `pure`. `elapsed` calls an `observe` extern, and `report` prints, so both are `observe`.

10. A scalar parameter is passed by value, so it cannot be written:

```cint ext_mode.ci
extern observe void sample(out I64 x);
"x\n";
```

```text
error[C2064]: parameter or argument rule
  --> ext_mode.ci:1:28
   |
 1 | extern observe void sample(out I64 x);
   |                            ^
```

11. A struct does not cross to the host as a parameter of an extern:

```cint ext_struct.ci
struct S { I64 a; }

extern observe void f(S s);

export I64 run() {
    return 1;
}
```

```text
error[C5050]: parameter mode, alias, or ABI rule
  --> ext_struct.ci:3:23
   |
 3 | extern observe void f(S s);
   |                       ^
```

**Notes**

12. `cint run` has no host program and the reference has no host table, so neither runs a program that calls a host import. A program that reaches the outside world under `cint run` does so through the standard services of 6.9.9, and a library that `cint build --lib` writes takes its host imports from the host program that loads it (SPEC-03P.2).
13. The effect class is what makes recording and replay exact (8.4). A `pure` extern adds nothing to a recording, because calling it again gives the same result. An `observe` extern adds its results, so the replay supplies them. An `unrestricted` extern ends the part of a run that a replay can reproduce.

<a id="services-and-grants"></a>

#### 6.9.9 Services, capabilities and the grant set

A *service* is a set of externs that the runtime supplies, named and versioned like `fs/1` or `console/1`. A program reaches a file or the terminal only by calling one of these externs, and only with a *grant* from the person who runs it. Authority is written down twice: the program declares with `requires` each *capability* it may use, which is part of its text, and the command that runs it grants capabilities, which is part of the run. A service call that no grant covers does not fault: it returns the error value `denied`, which the program handles like any other error.

**Syntax**

```ebnf
attribute     = "@service" "(" STRING [ "," capability ] ")" ;
capability    = qualified [ "(" cap_kind { "," cap_kind } ")" ] ;
cap_kind      = "path" | "name" ;
requires_decl = "requires" qualified [ "(" [ expr { "," expr } ] ")" ] ";" ;
```

The type `Grants` is a built-in type name (6.4.4). `path` and `name` are matched by spelling inside a `capability`; `requires` is a keyword.

**Constraints**

1. <a id="LS-325"></a>`@service` is written only before an `extern`: on any other declaration it is diagnosed C3043 at that declaration. Its `STRING` names the service and its version, such as `"fs/1"`. The `capability` after it names the capability that covers the extern, with the kind of each argument the capability takes: `path`, a logical path (paragraph 12), or `name`, a logical name. An extern of a service that needs no capability, such as `console/1`'s `write`, names none [LS-325].
2. A service extern that names a capability shall have an error union as its result, and the error set shall have a value named `denied`: otherwise the declaration is diagnosed C3043 at the extern's name [LS-325].
3. <a id="LS-326"></a>`Grants` is the type of the run's grant set. A value of type `Grants` is received as a parameter of a function or an extern and passed as an argument. It shall not be the type of a variable, a field, an array element or a result: C3042 at the type. Two `Grants` values are not compared: C2103 [LS-326].
4. <a id="LS-327"></a>`requires` declares, at the top level of a module, a capability that the program may use. Its name shall be a capability that a service extern of the program names (paragraph 1), and it has one argument for each kind of that capability, each a string literal: a logical path for `path`, and a logical name for `name`. An unknown capability, a wrong count of arguments, or an argument that is not a logical path or a logical name of the kind is diagnosed C3041 at the capability's name or at the argument. A `requires` is not exported: `export requires` is a syntax error (C1050). Inside a function or a block, `requires` is diagnosed C4012 [LS-327].
5. <a id="LS-328"></a>A call of a service extern whose capability no `requires` of the program names, in any of its modules, is diagnosed C3040 at the call [LS-328].
6. `main` may take a parameter of type `Grants`, before the program arguments if it takes them too: `void main(Grants g)`, `E!void main(Grants g)` and `void main(Grants g, in U8[_] args)` (5.1.2) [LS-221].

**Semantics**

7. The program's *capability manifest* is the set of the capabilities its `requires` declarations name, collected from every module of the program. Each is written as its *capability text*: its name, a colon and its arguments separated by commas, with no space, as in `fs.read:/data`. The manifest is part of the program's text, so a reader learns from it, before any run, every root the program can ask for [LS-327].
8. The *grant set* of a run is fixed before the program starts and does not change while it runs. Each grant is a capability text, and a grant of a `path` capability maps its logical root to a directory of the host, which the program never sees. `cint run --grant` gives the grants of a run (SPEC-06P.2), and a host program gives them when it creates the context (SPEC-03P.2). A run with no grant has an empty grant set [LS-326].
9. The value of `g` in `main(Grants g)` is the run's grant set. A program passes it on as an argument and cannot create, widen or change one [LS-326].
10. A call of a service extern is covered when a grant of the run has the extern's capability name and, for each `path` argument, a root equal to the call's path or an ancestor of it, separated by `/`. A covered call runs the service. A call that no grant covers does not reach the host: it returns the `denied` value of the extern's error set, and changes nothing [LS-328].
11. A service reports every failure as a value of its error set, never as a fault (5.2.3). A fault in the program ends the run, and service calls that completed before it stay completed [LS-325].
12. A *logical path* is absolute and separated by `/`, such as `/data/field.bin`. Each segment holds ASCII letters, digits, `_`, `-` and `.`, is not `.` or `..`, does not end in `.`, and is not one of the device names of 6.15.1, paragraph 1, with or without an extension. A path has no empty segment and no trailing `/`, and `/` alone is the root, whose descendants are all paths. Names are compared byte for byte [LS-327].
13. Two standard services are defined in this edition. Each is a module of the standard library (6.15.1), which a program imports by its name:

| Module | Service | Extern | Capability | Result | What it does |
|---|---|---|---|---|---|
| `fs` | `fs/1` | `read(Grants g, in U8[] path, I64 offset, out U8[] buf)` | `fs.read(path)` | `FsError!I64` | reads the file at `path` from byte `offset` into `buf`, at most its extent, and returns the count read |
| `fs` | `fs/1` | `stat(Grants g, in U8[] path)` | `fs.read(path)` | `FsError!I64` | returns the size of the file at `path` in bytes |
| `fs` | `fs/1` | `put(Grants g, in U8[] path, in U8[] bytes)` | `fs.write(path)` | `FsError!void` | replaces the file at `path` with `bytes`, or leaves it unchanged |
| `console` | `console/1` | `write(Grants g, in U8[] bytes)` | none | `ConsoleError!void` | writes every byte to the program's output, or fails |
| `console` | `console/1` | `read(Grants g, out U8[] buf, I64 wait_ms)` | `console.input` | `ConsoleError!I64` | returns the bytes of the console input that arrive within `wait_ms` milliseconds, at most the extent of `buf`; 0 on a timeout, and a negative `wait_ms` waits without limit |
| `console` | `console/1` | `read_line(Grants g, out U8[] buf)` | `console.input` | `ConsoleError!I64` | reads one line without its line end and returns its length; a longer line arrives in pieces |
| `console` | `console/1` | `window(Grants g)` | `console.input` | `ConsoleError!I64` | returns the terminal's rows in the high 32 bits and its columns in the low 32 bits |
| `console` | `console/1` | `raw(Grants g, Bool on)` | `console.raw` | `ConsoleError!void` | turns raw mode on or off: no echo, no line editing, no signals and no output processing; the runtime turns it off when the context ends or faults |

   Every extern of both services is `observe`. `fs.FsError` has the values `path`, `denied`, `not_found`, `exists`, `not_directory`, `is_directory`, `not_empty`, `bounds` and `quota`, numbered 1 to 9 in that order, and `console.ConsoleError` has `denied`, `closed` and `unavailable`, numbered 1 to 3. `fs.write` does not imply `fs.read`: a program that reads and writes one root requires both [LS-325].

**Examples**

14. This program reads a file under the root `/data`. Run with a grant that maps `/data` to the directory `data`, it reads the file, and run without one, `fs.read` returns `denied` and `try` passes it on as the result of `main` (6.11.7):

```cint reads.ci
import fs;
requires fs.read("/data");

fs.FsError!void main(Grants g) {
    U8[64] buf;
    I64 n = try fs.read(g, "/data/greeting.txt", 0, buf);
    "read {n} bytes: {buf[0]} {buf[1]}\n";
    return;
}
```

```console
$ printf 'hello, file\n' > data/greeting.txt
$ cint run --grant fs.read:/data=data reads.ci
read 12 bytes: 104 101
$ cint run reads.ci
error: fs.FsError.denied (tag 2)
```

15. A program can treat `denied` as one outcome among others and go on:

```cint denied_fallback.ci
import fs;
requires fs.read("/data");

I64 size_or(Grants g, I64 fallback) {
    I64 n = fs.stat(g, "/data/greeting.txt") catch (e) {
        if (e == fs.FsError.denied) {
            "no grant covers /data\n";
        }
        return fallback;
    };
    return n;
}

void main(Grants g) {
    "{size_or(g, -1)}\n";
}
```

```console
$ cint run denied_fallback.ci
no grant covers /data
-1
$ cint run --grant fs.read:/data=data denied_fallback.ci
12
```

16. Reading and writing take two capabilities, which may name two roots. `fs.put` writes the new file whole, or leaves the old one in place:

```cint copy_file.ci
import fs;
requires fs.read("/in");
requires fs.write("/out");

fs.FsError!void main(Grants g) {
    I64 total = try fs.stat(g, "/in/greeting.txt");
    U8[64] buf;
    I64 n = try fs.read(g, "/in/greeting.txt", 0, buf);
    try fs.put(g, "/out/copy.txt", buf[0..n]);
    "copied {n} of {total} bytes\n";
    return;
}
```

```console
$ cint run --grant fs.read:/in=data --grant fs.write:/out=out copy_file.ci
copied 12 of 12 bytes
```

17. `console/1`'s `write` needs no capability, so a program needs no `requires` to call it:

```cint hello_console.ci
import console;

console.ConsoleError!void main(Grants g) {
    try console.write(g, "hello from console/1\n");
    return;
}
```

```text
hello from console/1
```

18. A service call needs a `requires` of its capability somewhere in the program:

```cint no_requires.ci
import fs;

export I64 run(Grants g) {
    return fs.stat(g, "/data/a") catch -1;
}
```

```text
error[C3040]: name or module rule
  --> no_requires.ci:4:12
   |
 4 |     return fs.stat(g, "/data/a") catch -1;
   |            ^
```

19. A root is a logical path, which has no trailing `/`:

```cint root_syntax.ci
import fs;
requires fs.read("/data/");

export I64 run(Grants g) {
    return fs.stat(g, "/data/a") catch -1;
}
```

```text
error[C3041]: name or module rule
  --> root_syntax.ci:2:18
   |
 2 | requires fs.read("/data/");
   |                  ^
```

20. A grant set is passed, never stored:

```cint grants_local.ci
import fs;
requires fs.read("/data");

export I64 run(Grants g) {
    Grants h = g;
    return fs.stat(h, "/data/a") catch -1;
}
```

```text
error[C3042]: name or module rule
  --> grants_local.ci:5:5
   |
 5 |     Grants h = g;
   |     ^
```

**Notes**

21. The grant set is an argument rather than a global so that the code that can reach a file is visible in the text: a function without a `Grants` parameter calls no service, and neither does anything it calls.
22. Each service call is an `observe` event of a recording (8.4). With the full contents of every read in the recording, a replay runs the program without the files or the terminal it first ran against.
23. A program that requires nothing and receives no grant still runs: its service calls return `denied`, and `console/1`'s `write` works as print statements do.

<a id="statements"></a>

### 6.10 Statements

A statement is one step of a program. The statements of a block run one after another, in the order written. Every statement either ends with `;` or is a braced construct such as `if` or `while`, and the bodies of all compound statements are blocks in braces. A statement has no value: assignment, `++` and `--` are statements (6.7.14), so an assignment cannot appear inside a condition.

<a id="blocks-and-simple-statements"></a>

#### 6.10.1 Blocks and simple statements

A *block* is a sequence of statements in braces. It may stand anywhere a statement may, and it starts a new scope: a name declared in it is visible from its declaration to the closing brace (6.2.3). A *simple statement* is a call, an assignment or an increment, followed by `;`.

**Syntax**

```ebnf
block       = "{" { statement } "}" ;
statement   = block
            | const_decl
            | var_decl
            | view_decl
            | simple_stmt ";"
            | discard
            | print_stmt
            | if_stmt
            | [ IDENT ":" ] loop_stmt
            | switch_stmt
            | jump_stmt
            | defer_stmt
            | assert_stmt
            | static_assert
            | reduce_stmt ;
simple_stmt = place assign_op expr
            | place ( "++" | "--" )
            | [ "try" ] call ;
```

**Constraints**

1. <a id="LS-163"></a>There is no empty statement. A `;` where a statement is expected, as in `if (c);` or after the closing brace of a compound statement, is diagnosed C1040 [LS-163].
2. <a id="LS-167"></a>Only these stand as statements: a declaration, a call (with `try` where its result is an error union, 6.11), an assignment or compound assignment, `++` or `--`, a discard `_ = expr`, a print statement, the compound statements of this clause, and, in a kernel, a `reduce` statement (6.13.5). Any other expression followed by `;`, such as `x + 1;`, is diagnosed C4012 at its first character. A type followed by a keyword where a name is expected, as in `I64 do = 1;`, is also C4012, at the type [LS-167].

**Semantics**

3. The statements of a block run in order. A block ends when its last statement completes, or earlier through `return`, `break`, `continue`, `try` or a fault; the deferred statements of 6.10.7 run when it ends other than by a fault [LS-163].

**Examples**

4. A block limits the scope of the names declared in it:

```cint stmt_block.ci
I64 x = 3;
{
    I64 y = x * 2;
    "{y}\n";
}
"{x}\n";
```

```text
6
3
```

5. An expression whose value would be thrown away is not a statement:

```cint stmt_expr.ci
I64 x = 3;
x + 1;
```

```text
error[C4012]: statement form not permitted
  --> stmt_expr.ci:2:1
   |
 2 | x + 1;
   | ^
```

<a id="the-if-statement"></a>

#### 6.10.2 The if statement

`if` runs a block when a condition is true and, optionally, another block when it is false. An `else if` chain tests several conditions in turn. The condition is always in parentheses and every branch is a block in braces.

**Syntax**

```ebnf
if_stmt = "if" "(" expr ")" block [ "else" ( if_stmt | block ) ] ;
```

**Constraints**

1. <a id="LS-168"></a>The condition shall be in parentheses and each branch shall be a block in braces: C1043, at the first character of the part that is missing its parentheses or braces [LS-168].
2. The condition has type `Bool`; an integer condition is diagnosed C2002, and an assignment written in a condition C1041 (6.3.3, 6.7.14) [LS-168].

**Semantics**

3. The condition is evaluated. When it is `true` the first block runs; otherwise the `else` part, if any, runs. In `else if`, the nested `if` is the `else` part, so an `else if` chain does not deepen the nesting of the program, and a chain may have any number of arms (5.3) [LS-168].

**Examples**

4. This example shows an `else if` chain:

```cint if_chain.ci
void grade(I64 score) {
    if (score >= 90) {
        "A\n";
    } else if (score >= 80) {
        "B\n";
    } else {
        "C\n";
    }
}

grade(95);
grade(85);
grade(10);
```

```text
A
B
C
```

5. A branch without braces is rejected:

```cint if_braces.ci
I64 x = 3;
if (x > 2) "big\n";
```

```text
error[C1043]: missing parentheses or braces
  --> if_braces.ci:2:12
   |
 2 | if (x > 2) "big\n";
   |            ^
```

**Notes**

6. C accepts an integer as a condition and an unbraced body. Both are rejected in cint: the condition is written as a comparison, `if (n != 0)`, and a body is always a block, so a second statement cannot fall outside it.

<a id="the-while-statement"></a>

#### 6.10.3 The while statement

`while` runs a block for as long as a condition stays true. The condition is tested before each iteration, so the block may not run at all.

**Syntax**

```ebnf
while_stmt = "while" "(" expr ")" block ;
```

**Constraints**

1. <a id="LS-169"></a>The condition shall be in parentheses and the body shall be a block: C1043. The condition has type `Bool` (C2002) [LS-169].

**Semantics**

2. The condition is evaluated. When it is `true` the body runs and the statement repeats; when it is `false` the statement ends. Each iteration is charged fuel (5.2.4), so a loop that does not end stops at the entry's fuel allowance when it has one (5.2.4) [LS-169].

**Examples**

3. This example shows a loop that counts:

```cint while_count.ci
I64 n = 1;
while (n < 100) {
    n *= 3;
}
"{n}\n";
```

```text
243
```

<a id="the-for-statement"></a>

#### 6.10.4 The for statement

`for` has two forms. The *range form*, `for i in 0..n`, runs a block once for each integer of a range, which is the common case. The *C form*, `for (init; condition; update)`, is C's loop, for iteration that the range form does not express.

**Syntax**

```ebnf
for_stmt = "for" IDENT "in" range block
         | "for" "(" for_init ";" expr ";" simple_stmt ")" block ;
range    = expr ( ".." | "..=" ) expr [ "by" expr ] ;
for_init = type IDENT "=" expr | simple_stmt ;
```

**Constraints**

1. <a id="LS-170"></a>In both forms the body shall be a block, and in the C form the header shall be in parentheses: C1043 [LS-170].
2. <a id="LS-172"></a>In the range form, the bounds have type `I64`; a literal bound takes that type. A bound of another type is diagnosed C2001; a conversion, as in `for i in 0..(n as I64)`, gives it the type `I64` [LS-172].
3. <a id="LS-173"></a>The loop variable of the range form is read-only in the body: an assignment to it is diagnosed C2052 [LS-173].
4. <a id="LS-175"></a>The step after `by` shall be a constant expression other than 0: C2053 [LS-175].
5. In the C form, `for_init` declares a variable or is a simple statement, the condition has type `Bool`, and the update is a simple statement [LS-170].

**Semantics**

6. In the range form, the bounds and the step are evaluated once, from left to right, before the first iteration; a change to a variable used in a bound does not change the range. The loop variable is an `I64` declared for the body [LS-172].
7. <a id="LS-171"></a>`lo..hi` is *half-open*: the loop variable takes the values `lo`, `lo + 1`, and so on, while it is less than `hi`. `lo..=hi` is *inclusive*: the values continue while they are at most `hi` [LS-171].
8. With `by s`, the values are `lo`, `lo + s`, `lo + 2s`, and so on. For `s > 0` they continue while less than `hi` (`..`) or at most `hi` (`..=`); for `s < 0`, while greater than `hi` or at least `hi` [LS-171].
9. <a id="LS-174"></a>When the first value already fails its test, as in `5..5` or `3..=1`, the body does not run [LS-174].
10. The loop computes no value past the last one it runs, so a range that ends at the maximum of `I64` completes without overflow [LS-175].
11. In the C form, `for_init` runs once; a variable it declares is visible only in the loop and may be assigned. Then, as long as the condition is `true`, the body runs, followed by the update. The update is an ordinary statement: `i++` past the maximum of the variable's type faults `E_OVERFLOW` at the `++` (6.7.14) [LS-170].

**Examples**

12. This example shows half-open and inclusive ranges, and an empty one:

```cint for_range.ci
for i in 0..4 {
    "{i} ";
}
"\n";
for i in 1..=4 {
    "{i} ";
}
"\n";
for i in 5..5 {
    "never ";
}
"done\n";
```

```text
0 1 2 3 
1 2 3 4 
done
```

13. This example shows a step, upward and downward:

```cint for_step.ci
for i in 0..10 by 3 {
    "{i} ";
}
"\n";
for i in 10..0 by -3 {
    "{i} ";
}
"\n";
```

```text
0 3 6 9 
10 7 4 1 
```

14. A range may end at the top of `I64`. The bounds of a narrower range are converted to `I64`:

```cint for_edge.ci
I64 n = 0;
for i in (I64.max - 2)..=I64.max {
    n += 1;
}
for i in (I8.min as I64)..=(I8.max as I64) {
    n += 1;
}
"{n}\n";
```

```text
259
```

15. In the C form the update is checked like any statement. An `I8` counter cannot pass 127:

```cint for_c.ci
for (I8 k = 125; k <= 127; k++) {
    "{k}\n";
}
```

```text
125
126
127
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> for_c.ci:1:29
   |
 1 | for (I8 k = 125; k <= 127; k++) {
   |                             ^ add.checked.i8
   |
   = operation: add.checked.i8
   = operand-count: 2
   = operand: I8 127
   = operand: I8 1
   = exact:   128
   = limit:   I8 127
   ...
```

16. The loop variable of the range form cannot be assigned; a loop that needs to change its own counter uses the C form:

```cint for_readonly.ci
for i in 0..4 {
    i = 2;
}
```

```text
error[C2052]: loop variable assigned
  --> for_readonly.ci:2:5
   |
 2 |     i = 2;
   |     ^
```

**Notes**

17. `for i in 0..n` corresponds to Python's `for i in range(n)`, half-open in the same way. Unlike a C loop over `int`, the range form cannot overflow its counter.

<a id="the-switch-statement"></a>

#### 6.10.5 The switch statement

`switch` selects one of several blocks of statements by the value of an integer, `Bool` or error value. A case lists single values and inclusive ranges. Control does not pass from one case to the next unless the case ends with `fallthrough;`, and every value of the type is handled, by a case or by `default`.

**Syntax**

```ebnf
switch_stmt    = "switch" "(" expr ")" "{" { case_clause } [ default_clause ] "}" ;
case_clause    = "case" case_item { "," case_item } ":" statement { statement } ;
case_item      = or_expr [ "..=" or_expr ] ;
default_clause = "default" ":" statement { statement } ;
```

**Constraints**

1. <a id="LS-177"></a>The value tested, the *scrutinee*, has an integer type, `Bool` or an error-set type. Another type is diagnosed C4026 [LS-177].
2. A case item is a constant expression (6.8) or an inclusive range `lo..=hi` of constant expressions, of the scrutinee's type. An item that is not constant is diagnosed C6004. A half-open range `lo..hi` is diagnosed C4022, and a range with `lo > hi` is C4024 [LS-177].
3. <a id="LS-181"></a>No value shall appear in two items of one `switch`: C4021 at the second of the two items [LS-181].
4. <a id="LS-179"></a>A case clause shall have at least one statement: C4023 at its `case`. Values that share a body are listed in one clause, `case 1, 3:` [LS-179].
5. <a id="LS-182"></a>A `switch` shall have a `default` clause or cover every value of the scrutinee's type: every value of an error set, both values of `Bool` or the full range of an integer type. Otherwise it is diagnosed C4020 at `switch` [LS-182].
6. <a id="LS-183"></a>`default` shall be the last clause and appear at most once: C4025 [LS-183].
7. <a id="LS-178"></a>`fallthrough;` shall be the last statement of a clause that another clause follows: C4014 otherwise [LS-178].

**Semantics**

8. The scrutinee is evaluated once, and the clause with an item that holds its value runs; when no item holds it, `default` runs [LS-177].
9. A clause ends at the next `case`, the `default` or the closing brace and control then leaves the `switch`. `fallthrough;` continues into the body of the next clause instead [LS-178].
10. <a id="LS-180"></a>`break;` in a clause leaves the `switch`. To leave a loop that encloses the `switch`, a labeled `break` names the loop (6.10.6) [LS-180].
11. Each clause is its own scope [LS-183].

**Examples**

12. This example shows single values, lists, ranges and `fallthrough`:

```cint switch_classify.ci
void classify(I64 code) {
    switch (code) {
        case 0:
            "ok\n";
        case 1..=4, 9:
            "warning {code}\n";
        case 5..=8:
            "error {code}\n";
            fallthrough;
        case 10:
            "logged\n";
        default:
            "unknown {code}\n";
    }
}

classify(0);
classify(9);
classify(6);
classify(42);
```

```text
ok
warning 9
error 6
logged
unknown 42
```

13. Two ranges cover every `U8`, so no `default` is needed:

```cint switch_full.ci
U8 b = 200;
switch (b) {
    case 0..=127: "low\n";
    case 128..=255: "high\n";
}
```

```text
high
```

14. A clause with no body is rejected, so C's stacked labels are written as one list:

```cint switch_empty.ci
I64 c = 5;
switch (c) {
    case 1:
    case 2: "small\n";
    default: "other\n";
}
```

```text
error[C4023]: `switch` rule
  --> switch_empty.ci:3:5
   |
 3 |     case 1:
   |     ^
```

**Notes**

15. In C, control falls from one case into the next unless a `break` stops it, and a range of values needs a label for each. cint inverts the default: each case ends by itself, and `fallthrough;` asks for C's behavior where it is meant.

<a id="break-continue-and-labels"></a>

#### 6.10.6 break, continue and labels

`break` leaves a loop or a `switch`, and `continue` starts the next iteration of a loop. A loop may carry a label, so that `break` and `continue` can name an outer loop directly. There is no `goto`.

**Syntax**

```ebnf
loop_stmt = while_stmt | for_stmt ;
jump_stmt = ( "break" | "continue" ) [ IDENT ] ";"
          | return_stmt
          | "fallthrough" ";" ;
```

A label is the `IDENT ":"` that `statement` (6.10.1) allows before a `loop_stmt`. `return_stmt` is in 6.10.8.

**Constraints**

1. <a id="LS-184"></a>`break;` appears inside a loop or a `switch`, and `continue;` inside a loop. `break name;` and `continue name;` appear inside the loop labeled `name`. Any other use is diagnosed C4013 [LS-184].

**Semantics**

2. `break;` ends the innermost enclosing loop or `switch`. `continue;` ends the current iteration of the innermost enclosing loop; in the C form of `for`, the update runs next, then the condition [LS-184].
3. `break name;` ends the loop labeled `name`, and `continue name;` starts its next iteration, leaving every loop and `switch` in between [LS-184].

**Examples**

4. This example shows a labeled `continue` and `break` in a search over pairs:

```cint jump_label.ci
void find() {
    rows: for r in 0..4 {
        for c in 0..4 {
            if (c > r) {
                continue rows;
            }
            if (r * c == 4) {
                "found {r} {c}\n";
                break rows;
            }
        }
    }
    "after\n";
}

find();
```

```text
found 2 2
after
```

5. Inside a `switch`, `break` leaves the `switch` and `continue` continues the loop:

```cint jump_switch.ci
for i in 0..6 {
    switch (i) {
        case 1: continue;
        case 3: break;
        default: "{i} ";
    }
    "| ";
}
"\n";
```

```text
0 | 2 | | 4 | 5 | 
```

<a id="defer-and-errdefer"></a>

#### 6.10.7 defer and errdefer

`defer` schedules a statement to run when the enclosing block ends, so that cleanup is written next to the setup it undoes. `errdefer` schedules a statement that runs only when the function returns an error. Neither runs after a fault: a fault stops the program where it happens (5.2.3).

**Syntax**

```ebnf
defer_stmt = ( "defer" | "errdefer" ) ( block | simple_stmt ";" | discard ) ;
```

**Constraints**

1. <a id="LS-187"></a>A deferred statement shall not contain a `return`, `try`, `break` or `continue` that would leave it: C4030. A `break` or `continue` of a loop inside the deferred statement is permitted [LS-187].
2. <a id="LS-320"></a>A deferred statement may consume an error union with `catch` or `_ =` (6.11); `try` is excluded by constraint 1 [LS-320].
3. <a id="LS-322"></a>`errdefer` appears only where an error can leave its block: in a function whose result is an error union, and in a test (6.15.3). Elsewhere it could not run and is diagnosed C4031: in a function or kernel whose result is not an error union, at the top level of a script, and inside a deferred statement [LS-322].

**Semantics**

4. <a id="LS-185"></a>`defer` schedules its statement to run when the enclosing block ends by reaching its closing brace, by `return`, `break` or `continue` or by `try` passing an error on. The statements deferred in one block run in the reverse of the order in which they were scheduled [LS-185].
5. <a id="LS-186"></a>A deferred statement reads its variables when it runs, not when it is scheduled [LS-186].
6. <a id="LS-316"></a>`return expr;` evaluates `expr`, then runs the pending deferred statements, then returns the value of `expr`. A deferred statement cannot change the value returned [LS-316].
7. <a id="LS-318"></a>A `defer` in a loop body is scheduled in each iteration and runs when that iteration's block ends, including by `continue` or `break` [LS-318].
8. <a id="LS-319"></a>A deferred statement scheduled inside a `catch (e) { ... }` block runs when that block ends [LS-319].
9. <a id="LS-321"></a>At the top level of a script, a deferred statement runs when the script's entry ends normally, by reaching the end of the script or by `return;` [LS-321].
10. <a id="LS-189"></a>`errdefer` runs only when the function ends by returning an error, through `return .name;` or through `try` or when an error leaves a test (6.15.3). Such statements run together with the deferred statements of the same blocks, in reverse order of scheduling. No form of `errdefer` gives access to the error [LS-189].
11. <a id="LS-317"></a>Running a deferred statement charges no fuel of its own; the calls and loop iterations inside it are charged as anywhere else (5.2.4). A fault inside a deferred statement is an ordinary fault, at its own position, with the call stack of the function that scheduled it [LS-317].
12. <a id="LS-188"></a>Deferred statements do not run after a fault. A fault ends the entry at once, and its fault record describes the state at the fault (5.2.3) [LS-188].

**Examples**

13. Deferred statements run in reverse order when their block ends, and read variables as they are then:

```cint defer_order.ci
void steps() {
    I64 n = 1;
    defer {
        "first scheduled, n = {n}\n";
    }
    defer {
        "second scheduled\n";
    }
    n = 5;
    {
        defer {
            "inner block ends\n";
        }
        "in inner block\n";
    }
    "body ends\n";
}

steps();
```

```text
in inner block
inner block ends
body ends
second scheduled
first scheduled, n = 5
```

14. The returned value is fixed before the deferred statement runs:

```cint defer_return.ci
I64 counter() {
    I64 n = 1;
    defer n = 100;
    return n;
}

"{counter()}\n";
```

```text
1
```

15. A `defer` in a loop runs at the end of each iteration, including one that `continue` ends:

```cint defer_loop.ci
for i in 0..3 {
    defer {
        "end of {i}\n";
    }
    if (i == 1) {
        continue;
    }
    "body {i}\n";
}
```

```text
body 0
end of 0
end of 1
body 2
end of 2
```

16. `errdefer` undoes work only on the error path:

```cint defer_err.ci
error StoreError { full, closed }

StoreError!I64 reserve(I64 want, I64 room) {
    if (want > room) {
        return .full;
    }
    return want;
}

StoreError!I64 stage(I64 want) {
    defer {
        "leave stage {want}\n";
    }
    errdefer {
        "undo stage {want}\n";
    }
    I64 got = try reserve(want, 10);
    "staged {got}\n";
    return got;
}

void main() {
    I64 a = stage(4) catch -1;
    I64 b = stage(40) catch -1;
    "{a} {b}\n";
}
```

```text
staged 4
leave stage 4
undo stage 40
leave stage 40
4 -1
```

17. After a fault, no deferred statement runs:

```cint defer_fault.ci
void fragile(I8 x) {
    defer {
        "cleanup\n";
    }
    I8 y = x + 100;
}

"start\n";
fragile(100);
```

```text
start
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> defer_fault.ci:5:14
   |
 5 |     I8 y = x + 100;
   |              ^ add.checked.i8
   |
   = operation: add.checked.i8
   = operand-count: 2
   = operand: I8 100
   = operand: I8 100
   = exact:   200
   = limit:   I8 127
   ...
```

18. An `errdefer` where no error can leave is rejected:

```cint defer_noerr.ci
void tidy() {
    errdefer {
        "undo\n";
    }
}

tidy();
```

```text
error[C4031]: control-flow rule
  --> defer_noerr.ci:2:5
   |
 2 |     errdefer {
   |     ^
```

19. A deferred statement that would leave by `return` is rejected:

```cint defer_leave.ci
I64 pick(I64 x) {
    defer {
        return 0;
    }
    return x;
}

"{pick(3)}\n";
```

```text
error[C4030]: control-flow rule
  --> defer_leave.ci:3:9
   |
 3 |         return 0;
   |         ^
```

**Notes**

20. A print statement is not a simple statement, so a deferred print is written in a block: `defer { "done\n"; }`.
21. Python's `with` statement runs its cleanup when an exception leaves it. A cint fault is not an exception, and cleanup after a fault belongs to the host program that runs the entry (SPEC-03P.2).

<a id="the-return-statement"></a>

#### 6.10.8 The return statement

`return` ends the current function and gives its result. At the top level of a script it ends the script, and in a test it ends the test.

**Syntax**

```ebnf
return_stmt = "return" [ expr ] ";" ;
```

**Constraints**

1. <a id="LS-190"></a>`return;` appears in a function whose result is `void` or `E!void`, in a test, and at the top level of a script. `return expr;` appears in a function with any other result. Any other combination is diagnosed C2066 [LS-190].
2. In a function whose result is `E!T`, `expr` is either a value of type `T` or a value of the error set `E` (6.11.2) [LS-190].

**Semantics**

3. `return` runs the pending deferred statements (6.10.7) and ends the function, giving the value of `expr` to the caller [LS-190].
4. At the top level of a script, `return;` ends the script's entry normally. In a test, `return;` ends the test as if its body had completed (6.15.3) [LS-190].

**Examples**

5. `return;` ends a script early:

```cint return_script.ci
defer {
    "script ends\n";
}
"before\n";
return;
"not reached\n";
```

```text
before
script ends
```

<a id="assert-and-static-assert"></a>

#### 6.10.9 assert and static_assert

`assert` checks a condition when the program runs and stops it with a fault when the condition is false. `static_assert` checks a condition during translation. When the condition of `assert` is a comparison, the fault record holds both compared values, so the report shows what the values were, not only that the check failed.

**Syntax**

```ebnf
assert_stmt   = "assert" "(" expr [ "," STRING { STRING } ] ")" ";" ;
static_assert = "static_assert" "(" expr [ "," STRING ] ")" ";" ;
```

**Constraints**

1. <a id="LS-191"></a>The condition has type `Bool`: C2002 [LS-191].
2. <a id="LS-192"></a>The condition of `static_assert` is a constant expression (6.8). When it is `false`, the program is diagnosed C6010 [LS-192].

**Semantics**

3. `assert` evaluates its condition. When it is `true`, the statement completes. When it is `false`, the program faults `E_ASSERT`. The message, including any hole in it, is not evaluated and is not part of the fault record [LS-191].
4. `assert` is checked in every build and every program; no option removes it [LS-191].

**Faults**

5. The fault `E_ASSERT` is raised by `assert` when its condition is `false`. The record holds the operation identifier `assert.checked.bool`. When the condition is a comparison, its operands are the two compared values, with their types; otherwise it has no operands. It has no exact result and no limit. Its position is the first character of the condition [LS-191].

**Examples**

6. A failed comparison shows both sides:

```cint assert_fail.ci
I64 total = 7 * 6;
assert(total == 42);
"ok\n";
assert(total < 40, "total is too large");
"not reached\n";
```

```text
ok
fault[E_ASSERT]: assertion failed
  --> assert_fail.ci:4:8
   |
 4 | assert(total < 40, "total is too large");
   |        ^ assert.checked.bool
   |
   = operation: assert.checked.bool
   = operand-count: 2
   = operand: I64 42
   = operand: I64 40
   = exact:   none
   = limit:   none
   ...
```

7. A static assertion that fails stops translation:

```cint assert_static.ci
const I64 SLOTS = 4;
static_assert(SLOTS * 2 == 9, "eight slots expected");
"x\n";
```

```text
error[C6010]: compile-time evaluation rule
  --> assert_static.ci:2:1
   |
 2 | static_assert(SLOTS * 2 == 9, "eight slots expected");
   | ^
```

**Notes**

8. C's `assert` is removed when `NDEBUG` is defined, and a failure prints only the text of the condition. A cint `assert` stays in every program, and its record holds the values compared.

<a id="print-statements"></a>

#### 6.10.10 Print statements

A string literal standing as a statement prints itself. Each hole `{...}` in it is replaced by the text of the value inside, as 7.5 describes. Printing is the only output a program has, and its bytes are part of the program's outcome: the same on every implementation, operating system and locale.

**Syntax**

```ebnf
print_stmt = STRING { STRING } ";" ;
```

**Constraints**

1. <a id="LS-196"></a>A kernel body shall not contain a print statement: C5020 (6.13.3) [LS-196].

**Semantics**

2. <a id="LS-193"></a>A print statement writes the formatted bytes of its string literals, in order, to the program's output. Adjacent literals form one statement [LS-193].
3. <a id="LS-194"></a>No newline is added. The bytes written are exactly the UTF-8 bytes of the formatted text, and a line feed is written as one byte, 10, on every operating system [LS-194].
4. The text of each hole is the same on every locale, operating system and executor, as 7.5.1 states.
5. Printing is an effect of the entry: a recording of a run holds the bytes printed, and replay reproduces them (8.4) [LS-196].
6. 7.5.1 gives the order in which the holes of a statement are evaluated, and what is written when one of them faults.

**Examples**

7. Adjacent literals form one statement, and no newline is added between statements:

```cint print_adjacent.ci
I64 n = 7;
"line one, " "n = {n}\n";
"no newline here, ";
"{{braces}}\n";
```

```text
line one, n = 7
no newline here, {braces}
```

8. A fault in a hole stops the statement before it writes anything:

```cint print_fault.ci
I8 x = 100;
"start\n";
"x = {x}, doubled = {x + x}\n";
```

```text
start
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> print_fault.ci:3:24
   |
 3 | "x = {x}, doubled = {x + x}\n";
   |                        ^ add.checked.i8
   |
   = operation: add.checked.i8
   = operand-count: 2
   = operand: I8 100
   = operand: I8 100
   = exact:   200
   = limit:   I8 127
   ...
```

<a id="errors-and-faults"></a>

### 6.11 Errors and faults

A program meets two kinds of failure. An *error* is an expected outcome that the program handles: bad input, a full buffer, an account that would be overdrawn. A function reports one by returning a value of an error set, and its caller must deal with it. A *fault* is the stop of a run at an operation that cannot give a correct result (3.9), such as an overflow or an index outside an array: the operation stops the program, and no program code can catch it (5.2.3). This clause defines how errors are returned and handled, and how an error differs from a fault.

<a id="errors-compared-with-faults"></a>

#### 6.11.1 Errors compared with faults

| | Error | Fault |
|---|---|---|
| What it is for | An expected outcome the caller handles | An operation that cannot give a correct result |
| How it starts | `return .overdrawn;`, or `try` passing one on | Raised by an operation or a failed `assert`; no statement names a fault to raise |
| How it is handled | `try`, `catch` or `_ =`, in the caller | Not by the program: it ends the entry, and the host program or tool that ran the entry reads the fault record |
| What it is | A value returned through an error union | A fault record (8.1) |

**Semantics**

1. <a id="LS-128"></a>An error is a value of an error set (6.9.7), returned by a function whose result type is an error union (6.11.2). Returning it is an ordinary return: the caller receives it, and no other code runs on the way except the deferred statements of the returning function (6.10.7) [LS-128].
2. A fault is raised by an operation; no statement raises one by name. Program code cannot construct, catch, inspect or resume a fault; a fault ends the entry with its fault record (5.2.3), and an `expect_fault` test only observes it (6.15.3) [LS-128].
3. <a id="IM-120"></a>Arithmetic that may legitimately fail, such as a division by a value that may be 0, is written with a *result form*, which returns an error where the plain operator would fault: `div_result(a, b)`, `add_result(a, b)` and the other forms of 6.7.12, and the conversion `x as? T` of 6.6. Their errors are values of the built-in error set `ArithError` and are handled as any other error [IM-120].

<a id="error-unions"></a>

#### 6.11.2 Error unions

An *error union* `E!T` is the result type of a function that either succeeds with a value of type `T` or fails with a value of the error set `E`. `E!void` is the result of a function that succeeds without a value. Every call of such a function is handled where it is made.

**Syntax**

```ebnf
result_type = "void" | base_type | union_type ;
union_type  = qualified "!" ( "void" | base_type ) ;
```

**Constraints**

1. <a id="LS-94"></a>An error union is written only as the result type of a function (6.9.4) [LS-94].
2. <a id="LS-95"></a>A call whose result is an error union shall be consumed where it appears, by `try` (6.11.3), `catch` (6.11.4) or `_ =` (6.11.5). Any other use, including a call statement on its own and `return f(x);`, is diagnosed C2040 at the call [LS-95].
3. In a function whose result is `E!T`, `return expr;` gives either a value of type `T` or a value of `E`, or of a set that `E` holds (6.3.7). In a function whose result is `E!void`, `return;` and reaching the closing brace give success [LS-94].

**Semantics**

4. A value of type `E!T` holds either a success value of type `T` or an error value of `E`. A value of `E!void` holds either success, with no value, or an error value [LS-94].

**Examples**

5. A function that can fail, and a caller that supplies a value in place of the error:

```cint union_digit.ci
error ParseError { empty, bad_digit, too_large }

ParseError!I64 digit(I64 c) {
    if (c < 48 || c > 57) {
        return .bad_digit;
    }
    return c - 48;
}

I64 v = digit(55) catch -1;
I64 w = digit(120) catch -1;
"{v} {w}\n";
```

```text
7 -1
```

6. A call left unhandled is rejected:

```cint union_unused.ci
error E { failed }

E!void check(I64 x) {
    if (x < 0) {
        return .failed;
    }
}

void main() {
    check(1);
}
```

```text
error[C2040]: type rule
  --> union_unused.ci:10:5
   |
10 |     check(1);
   |     ^
```

<a id="try"></a>

#### 6.11.3 try

`try` passes an error on. Applied to a call, it gives the call's success value, or, when the call returns an error, returns that error from the current function at once. A chain of functions that each use `try` passes an error back to the first caller that handles it.

**Syntax**

```ebnf
unary_expr = neg_literal
           | ( "-" | "-%" | "~" | "!" | "try" ) unary_expr
           | postfix_expr ;
```

`try` is one of the prefix operators of `unary_expr` (6.7.5).

**Constraints**

1. <a id="LS-129"></a>The operand of `try` is a call, or a result form, whose result is an error union. Any other operand, including a kernel dispatch, is diagnosed C2043 [LS-129].
2. `try` appears in a function whose result is an error union, or in a test. In a function, the function's error set shall hold every value of the operand's set: the same set, or a combined set that includes it (6.3.7). Otherwise it is diagnosed C2042 at the operand [LS-129].
3. <a id="LS-315"></a>In a test, `try` admits an error of any set [LS-315].

**Semantics**

4. `try` evaluates its operand. On success it gives the success value. On an error, it runs the pending deferred statements of the current function, `errdefer` included (6.10.7) and returns the error from the function, converted to the function's error set [LS-129].
5. In a test, an error that `try` passes on ends the test and fails it (6.15.3) [LS-315].

**Examples**

6. This example shows two calls that may fail, combined into one function that may fail:

```cint try_chain.ci
error ParseError { empty, bad_digit, too_large }

ParseError!I64 digit(I64 c) {
    if (c < 48 || c > 57) {
        return .bad_digit;
    }
    return c - 48;
}

ParseError!I64 two_digits(I64 hi, I64 lo) {
    I64 a = try digit(hi);
    I64 b = try digit(lo);
    return a * 10 + b;
}

I64 n = two_digits(52, 50) catch -1;
I64 m = two_digits(52, 65) catch -1;
"{n} {m}\n";
```

```text
42 -1
```

7. `try` cannot pass on an error that the function's own set does not hold:

```cint try_wider.ci
error E { low }
error F { high }

E!I64 check(I64 x) {
    if (x < 0) {
        return .low;
    }
    return x;
}

F!I64 relay(I64 x) {
    I64 v = try check(x);
    return v;
}

I64 r = relay(1) catch 0;
"{r}\n";
```

```text
error[C2042]: type rule
  --> try_wider.ci:12:13
   |
12 |     I64 v = try check(x);
   |             ^
```

<a id="catch"></a>

#### 6.11.4 catch

`catch` handles an error where the call is made. `f() catch value` gives a value in place of the error. `f() catch (e) { ... }` names the error and runs a block, which must leave: by `return`, `break` or `continue`. Either way, the expression has a value in every case.

**Syntax**

```ebnf
expr = cond_expr [ "catch" ( "(" IDENT ")" block | cond_expr ) ] ;
```

**Constraints**

1. <a id="LS-130"></a>The left operand of `catch` is a call, or a result form, whose result is an error union with a success value [LS-130].
2. In `call catch value`, the value has the success type, which a literal takes from its context (6.5) [LS-130].
3. <a id="LS-131"></a>In `call catch (e) { ... }`, the block shall not complete by reaching its closing brace: it ends with `return`, `break`, `continue` or another statement that leaves it. A block that can complete is diagnosed C4002 at its opening brace [LS-131].
4. `catch` binds more loosely than every operator, so its value operand extends as far as an expression can: `f() catch 0 + 1` gives `0 + 1` on an error [LS-130].

**Semantics**

5. The call is evaluated. On success the expression gives the success value. On an error, `catch value` evaluates `value` and gives it; the value is evaluated only in that case [LS-130].
6. On an error, `catch (e) { ... }` binds `e` to the error value, with the error set of the call as its type, and runs the block. A deferred statement scheduled in the block runs when the block ends (6.10.7) [LS-131].

**Examples**

7. A `catch` block that tells the errors apart with `switch` and leaves with `return`:

```cint catch_block.ci
error ParseError { empty, bad_digit, too_large }

ParseError!I64 digit(I64 c) {
    if (c < 48 || c > 57) {
        return .bad_digit;
    }
    return c - 48;
}

void show(I64 c) {
    I64 d = digit(c) catch (e) {
        switch (e) {
            case .empty: "empty\n";
            case .bad_digit: "bad digit {c}\n";
            case .too_large: "too large\n";
        }
        return;
    };
    "digit {d}\n";
}

show(55);
show(65);
```

```text
digit 7
bad digit 65
```

8. In a loop, a `catch` block can skip to the next iteration:

```cint catch_loop.ci
error E { negative }

E!I64 root_floor(I64 x) {
    if (x < 0) {
        return .negative;
    }
    return isqrt(x);
}

I64 total = 0;
for i in -2..3 {
    I64 r = root_floor(i * 5) catch (e) {
        "skip {i}\n";
        continue;
    };
    total += r;
}
"{total}\n";
```

```text
skip -2
skip -1
5
```

9. The value after `catch` is a whole expression:

```cint catch_value.ci
error E { failed }

E!I64 f(I64 x) {
    if (x < 0) {
        return .failed;
    }
    return x * 2;
}

I64 w = f(-1) catch 0 + 1;
I64 u = f(4) catch 0 + 1;
"{w} {u}\n";
```

```text
1 8
```

10. A `catch` block must leave:

```cint catch_falls.ci
error E { failed }

E!I64 f(I64 x) {
    if (x > 0) {
        return .failed;
    }
    return x;
}

I64 y = f(1) catch (e) {
    "caught\n";
};
"{y}\n";
```

```text
error[C4002]: control-flow rule
  --> catch_falls.ci:10:24
   |
10 | I64 y = f(1) catch (e) {
   |                        ^
```

**Notes**

11. `catch` resembles Python's `try: ... except E:` with two differences: the handler is attached to one call, not to a region of code, and it handles errors only, not faults.

<a id="discarding-a-result"></a>

#### 6.11.5 Discarding a result

`_ = expr;` evaluates an expression and throws away its value. It is the only way to drop an error without handling it, and it is written out where the error is ignored, so a reader sees every place that does so.

**Syntax**

```ebnf
discard = "_" "=" expr ";" ;
```

**Semantics**

1. <a id="LS-132"></a>`_ = expr;` evaluates `expr` and discards its value. When `expr` is a call that returns an error union, the error, if any, is discarded too and the statement completes [LS-132].

**Examples**

2. This example shows discarding the result of a call that may fail:

```cint discard.ci
error E { failed }

E!I64 attempt(I64 x) {
    if (x < 0) {
        return .failed;
    }
    "attempt {x}\n";
    return x;
}

_ = attempt(-1);
_ = attempt(3);
"done\n";
```

```text
attempt 3
done
```

<a id="errors-from-arithmetic"></a>

#### 6.11.6 Errors from arithmetic

The result forms of 6.7.12 and the conversion `x as? T` of 6.6 turn a fault that a program expects into an error that it handles. Their error set is the built-in `ArithError`, whose values name the fault the plain operator would have raised, such as `.div_zero` for `E_DIV_ZERO` and `.narrow` for `E_NARROW`.

**Semantics**

1. The value or error that a result form holds, and the rule that it is consumed with `try`, `catch` or `_ =`, are stated in 6.7.12.

**Examples**

2. A mean that reports an empty input as an error instead of a fault, and a narrowing that falls back to a default:

```cint arith_errors.ci
ArithError!I64 mean(I64 total, I64 n) {
    I64 q = try div_result(total, n);
    return q;
}

I8 narrow_or(I64 v, I8 fallback) {
    return v as? I8 catch fallback;
}

I64 a = mean(10, 2) catch -1;
I64 b = mean(10, 0) catch (e) {
    if (e == .div_zero) {
        "no samples\n";
    }
    return;
};
"{a} {b}\n";
```

```text
no samples
```

3. `as?` binds more tightly than `catch`, so a fallback needs no parentheses:

```cint arith_narrow.ci
I8 narrow_or(I64 v, I8 fallback) {
    return v as? I8 catch fallback;
}

"{narrow_or(99, -1)} {narrow_or(300, -1)}\n";
```

```text
99 -1
```

<a id="an-error-result-of-main"></a>

#### 6.11.7 An error result of main

A program whose entry is `main` may let `main` return an error, so that a whole program can end with an expected failure that is not a fault. The program defines `IoError!void main()`, or the same with another error set, in place of `void main()` (5.1.2).

**Semantics**

1. When `main` returns an error, the entry ends with that error as its result, as 5.2.1 states. It is not a fault, and no fault record is made.
2. `cint run` then writes one line to standard error, `error: <set>.<value> (tag <n>)`, where `<set>` is the error set of `main`'s result qualified by its module (a built-in set such as `ArithError` has no module part), `<value>` is the error's name and `<n>` its tag in that set, and exits with the status of 5.2.1, Note 7 [LS-221].

**Examples**

3. This example shows a ledger that refuses to overdraw. The first withdrawal succeeds; the second returns `.overdrawn` through `try`, and `main` ends with that error:

```cint ledger.ci
error LedgerError { overdrawn, frozen }

LedgerError!I64 withdraw(I64 balance, I64 amount) {
    if (amount > balance) {
        return .overdrawn;
    }
    return balance - amount;
}

LedgerError!void main() {
    I64 b = try withdraw(100, 30);
    "balance {b}\n";
    I64 c = try withdraw(b, 500);
    "balance {c}\n";
}
```

```console
$ cint run ledger.ci
balance 70
error: ledger.LedgerError.overdrawn (tag 1)
$ echo $?
6
```

<a id="arrays-and-views"></a>

### 6.12 Arrays and views

An array holds a fixed number of elements of one type, and a view is a typed, shaped window onto the elements of an array. Arrays are how a program holds data in bulk; views are how it hands that data to functions and kernels without copying it. This section covers declaring arrays (6.12.1), reaching one element (6.12.2), taking part of an array as a view (6.12.3), passing views to functions (6.12.4), the rules for copying between arrays and views (6.12.5), and arrays declared at module level (6.12.6). The built-in operations on arrays and views, such as `len`, `extent`, `copy`, `transpose`, `reverse` and `reshape`, are in 7.4.

The examples of this section declare their arrays inside functions, most often inside `void main()` (5.1.2).

<a id="array-objects-and-initializers"></a>

#### 6.12.1 Array objects and initializers

An array type is an element type followed by a *shape*: a list of *extents* in brackets, one per dimension. `I64[8]` is eight `I64` values; `I32[3, 4]` is three rows of four `I32` values. The number of extents is the array's *rank*. Declaring a variable of an array type creates the array, with every element set to 0 unless an initializer gives the elements.

**Syntax**

```ebnf
type          = base_type [ shape ] ;
base_type     = INT_TYPE | "Bool" | FIXED_TYPE | "Grants" | memory_type | qualified ;
shape         = "[" [ extent { "," extent } ] "]" ;
extent        = "_" | expr ;
array_literal = "[" [ expr { "," expr } [ "," ] ] "]" ;
```

An `array_literal` of rank 2 or more is written as nested literals: each item of the outer literal is the literal of one row.

**Constraints**

1. <a id="V-1"></a><a id="V-2"></a><a id="M-24"></a>The element type of an array is an integer type, a fixed-point type (6.3.2), `Bool`, an error set (6.3.7), a struct type or a handle type (6.14). The rank of an array or view type is 1 to 4: an array or view type with more than four extents is diagnosed C9102 at its `[` [V-1, V-2, M-24].
2. <a id="LS-62"></a>Each extent of a declared array is an `I64` expression whose value is known when the declaration runs: in a function body, an `I64` constant expression, a size parameter of the function (6.9.5), or an expression of constants and size parameters; in a module-level declaration and in a kernel body, a constant expression. Any other extent is diagnosed C6004, and in a kernel body C9102 (6.13.3). A constant extent below 0 is diagnosed C2015 [LS-62].
3. <a id="LS-66"></a>The extent `_` accepts any extent. It is written only in the type of a parameter or of a view variable (6.12.4); in the type of a declared array, such as `I64[_] a;`, it is diagnosed C9102. `I64[]` is shorthand for `I64[_]` [LS-66].
4. <a id="LS-67"></a>An array literal that initializes an array shall have exactly as many items as the extent it fills, at every level of nesting (C2011). A trailing comma is permitted. A brace list such as `{1, 2}` is not an array literal and is a syntax error (C1050) [LS-67].
5. <a id="LS-71"></a>`==`, `!=` and the ordering comparisons are not defined on arrays: comparing two arrays is diagnosed C2013 [LS-71].

**Semantics**

6. <a id="LS-63"></a>An array of shape `[n0, ..., nr-1]` holds `n0 * ... * nr-1` elements. They are stored in *row-major order*: the last index varies fastest, so `m[0, 0]`, `m[0, 1]`, `m[0, 2]` are adjacent [LS-63].
7. An array declared without an initializer has every element 0: `false` for `Bool`, every field 0 for a struct, and the null handle for a handle [LS-67].
8. An array literal gives the elements in row-major order. An initializer that is an array or a view copies its elements into the new array (6.12.5) [LS-67].
9. <a id="LS-68"></a>An extent may be 0. `I64[0] none;` declares an array with no elements, and every index into it faults [LS-68].
10. When a declaration runs, its extents are evaluated from left to right before any storage is taken [LS-62]. A local array takes its storage from the frame arena when its declaration runs and returns it when its block exits (5.2.6).

**Faults**

11. The fault `E_SHAPE` is raised by a declaration whose extent evaluates below 0. The fault record holds the operation identifier `decl.shape`; two operands, the dimension as an `I64` counted from 0 and the requested extent as an `I64`; no exact result; and the limit `I64 0`. Its position is the declared name. The check runs after every extent of the declaration is evaluated, and the first negative extent from the left is reported [LS-62].

**Examples**

12. Declarations without and with initializers, and nested literals. The loop prints the elements in storage order:

```cint array_basics.ci
void main() {
    I64[4] zeros;
    I32[2, 3] m = [[1, 2, 3], [4, 5, 6]];
    Bool[2] flags = [true, false];
    "{zeros[3]} {m[1, 0]} {flags[1]}\n";
    for i in 0..2 {
        for j in 0..3 {
            "{m[i, j]} ";
        }
    }
    "\n";
}
```

```text
0 4 false
1 2 3 4 5 6 
```

13. This example shows an extent computed from a size parameter. `n` is bound to the extent of the argument (6.9.5), and `tmp` has one more element:

```cint array_size_param.ci
I64 prefix_total[n](in I64[n] xs) {
    I64[n + 1] tmp;
    for i in 0..n {
        tmp[i + 1] = tmp[i] + xs[i];
    }
    return tmp[n];
}

void main() {
    I64[4] a = [1, 2, 3, 4];
    "{prefix_total(a)}\n";
}
```

```text
10
```

14. This example shows the same form with an extent that comes out negative. The size parameter `n` is 2, so `n - 3` is -1:

```cint array_negative_extent.ci
I64 g[n](in I64[n] xs) {
    I64[n - 3] tmp;
    return n;
}

void main() {
    I64[2] a;
    "{g(a)}\n";
}
```

```text
fault[E_SHAPE]: shapes do not match
  --> array_negative_extent.ci:2:16
   |
 2 |     I64[n - 3] tmp;
   |                ^ decl.shape
   |
   = operation: decl.shape
   = operand-count: 2
   = operand: I64 0
   = operand: I64 -1
   = exact:   none
   = limit:   I64 0
   ...
```

15. This example shows a literal with too few items:

```cint array_literal_count.ci
void main() {
    I64[3] a = [1, 2];
    "{a[0]}\n";
}
```

```text
error[C2011]: array rule
  --> array_literal_count.ci:2:16
   |
 2 |     I64[3] a = [1, 2];
   |                ^
```

16. This example shows an array with no elements:

```cint array_empty.ci
void main() {
    I64[0] none = [];
    "{len(none)}\n";
    I64 i = 0;
    "{none[i]}\n";
}
```

```text
0
fault[E_BOUNDS]: index out of bounds
  --> array_empty.ci:5:11
   |
 5 |     "{none[i]}\n";
   |           ^ index.checked.i64
   |
   = operation: index.checked.i64
   = operand-count: 1
   = operand: I64 0
   = exact:   none
   = limit:   I64 0
   ...
```

17. Arrays are not compared with `==`:

```cint array_compare.ci
void main() {
    I64[2] a;
    I64[2] b;
    if (a == b) {
        "same\n";
    }
}
```

```text
error[C2013]: array rule
  --> array_compare.ci:4:9
   |
 4 |     if (a == b) {
   |         ^
```

**Notes**

18. A C array decays to a pointer and carries no extent. A cint array keeps its shape: every index is checked against it, and an array passed to a function arrives as a view that still has its extents (6.12.4).

<a id="indexing"></a>

#### 6.12.2 Indexing

An index expression names one element: `a[i]` in an array of rank 1, `m[i, j]` in an array of rank 2, and so on. Indices count from 0. Every index is checked against its extent, and an index outside the array stops the program with a fault instead of reaching other storage.

**Syntax**

```ebnf
postfix_expr = ( call | primary ) { "[" index_list "]" | "." IDENT } ;
index_list   = index { "," index } ;
```

`call` is in 6.7.4 and `primary` in 6.7.3. An `index` is an `expr`, or a range, which makes a slice (6.12.3).

**Constraints**

1. <a id="LS-64"></a>Every index has type `I64`. A literal index is typed `I64`. An index of another integer type is diagnosed C2001; `as I64` (6.6) converts it [LS-64].
2. <a id="LS-65"></a>An index expression gives at most as many indices as the rank of the array or view it indexes. More indices, as in `a[0, 1]` for an array `a` of rank 1, are diagnosed C9102 at the `[` [LS-65].
3. Indexing the result of a partial index directly, `m[i][j]`, is diagnosed C2010. The element is written `m[i, j]` [LS-65].
4. <a id="M-27"></a>An element of a view without write permission is not assignable. Assigning to an element of an `in` parameter is diagnosed C2060, and assigning to an element of an `in` view variable is diagnosed C9102 [M-27].

**Semantics**

5. <a id="V-7"></a>The valid indices of a dimension of extent `n` are 0 to `n - 1` [LS-64, V-7].
6. <a id="IM-186"></a>Every index expression is evaluated, from left to right, before any index is checked. The dimensions are then checked in order, and the first index outside its extent faults. When the index expression is the place of an assignment, the place is evaluated and checked before the value is evaluated (6.2.4) [IM-186].
7. A *partial index* gives fewer indices than the rank: indexing an array or view of rank `r` with `k < r` indices gives a view of rank `r - k` of the remaining dimensions, with the permission of the indexed expression. For `I32[2, 8, 8] p`, `p[0]` is the same view as the slice `p[0, .., ..]` [LS-65].

**Faults**

8. The fault `E_BOUNDS` is raised by an index below 0 or at or above its extent. The fault record holds the operation identifier `index.checked.e`, where `e` is the element type in lower case, or `struct` for a struct element. For rank 1 it holds one operand, the index as an `I64` and the extent as the limit, an `I64`. For rank 2 and more, whether every index is given or only the leading ones, it holds two operands, the dimension counted from 0 and the index, both `I64` and that dimension's extent as the limit. There is no exact result. Its position is the `[` of the index expression [IM-186, LS-64].

**Examples**

9. This example shows a rank-2 element and a row passed as a view:

```cint index_rank2.ci
I64 row_total(in I64[_] row) {
    I64 t = 0;
    for j in 0..len(row) {
        t += row[j];
    }
    return t;
}

void main() {
    I64[3, 4] m;
    m[1, 0] = 1;
    m[1, 3] = 4;
    m[2, 0] = 100;
    "{m[1, 3]} {row_total(m[1])}\n";
}
```

```text
4 5
```

10. Of two indices outside their extents, the dimension checked first is reported. Here `i` is 2 in a dimension of extent 2:

```cint index_bounds.ci
void main() {
    I64[2, 3] m;
    I64 i = 2;
    I64 j = 3;
    m[i, j] = 5;
}
```

```text
fault[E_BOUNDS]: index out of bounds
  --> index_bounds.ci:5:6
   |
 5 |     m[i, j] = 5;
   |      ^ index.checked.i64
   |
   = operation: index.checked.i64
   = operand-count: 2
   = operand: I64 0
   = operand: I64 2
   = exact:   none
   = limit:   I64 2
   ...
```

11. An index of type `I32` is not converted silently:

```cint index_type.ci
void main() {
    I64[3] a = [1, 2, 3];
    I32 k = 1;
    "{a[k]}\n";
}
```

```text
error[C2001]: type mismatch
  --> index_type.ci:4:9
   |
 4 |     "{a[k]}\n";
   |         ^
```

12. `a[k as I64]` is the form that compiles:

```cint index_convert.ci
void main() {
    I64[3] a = [1, 2, 3];
    I32 k = 1;
    "{a[k as I64]}\n";
}
```

```text
2
```

**Notes**

13. In C, an index outside an array is undefined behavior, and a negative index has undefined behavior, which often reads the storage before the array. In cint both fault, and the fault record names the index and the extent it missed.

<a id="slicing-and-index-bases"></a>

#### 6.12.3 Slicing and index bases

A range in an index position takes a *slice*: a view of part of an array, sharing the array's storage. `a[2..5]` is the three elements `a[2]`, `a[3]` and `a[4]`; writing through a slice writes the array. A step, `by k`, takes every `k`-th element and a negative step runs backward.

**Syntax**

```ebnf
index_list = index { "," index } ;
index      = expr
           | [ expr ] ".." [ expr ] [ "by" expr ]
           | [ expr ] "..=" expr [ "by" expr ] ;
```

An index expression with at least one range in it is a slice.

**Constraints**

1. The bounds of a range have type `I64`, as an index does (C2001) [LS-64].
2. <a id="LS-161"></a>The step after `by` is an integer constant expression other than 0. A step that is not a constant, or is 0, is diagnosed C2053 [LS-161].

**Semantics**

3. <a id="LS-162"></a>A slice is a view of the storage of the expression it is taken from. Taking a slice copies no element. The slice has the permission of that expression: a slice of an `in` view is read-only [LS-161, LS-162].
4. In each dimension, an index removes the dimension and a range keeps it. `m[.., j]` is column `j` of a rank-2 array, a rank-1 view whose consecutive elements are a row's length apart; `m[i, 1..3]` is part of row `i` [LS-161].
5. With a positive step `k` (1 when `by` is absent), `a[lo..hi]` holds the elements at `lo`, `lo + k`, `lo + 2k` and so on, below `hi`. `a[lo..=hi]` is `a[lo..hi + 1]`, with the addition exact. An omitted first bound is 0 and an omitted second bound is the extent, so `a[..]` is the whole dimension [LS-161, IM-186].
6. With a negative step `k`, the range runs downward from its first bound: `a[f..g by k]` holds the elements at `f`, `f + k` and so on, above `g` and `a[f..=g by k]` includes `g`. An omitted first bound is the last index, `n - 1` for extent `n` and an omitted second bound runs through index 0. `a[.. by -1]` is the whole dimension reversed [LS-161].
7. For a dimension of extent `n`, a range with a positive step is valid when `0 <= lo <= hi <= n` after omitted bounds are filled in. A range with a negative step is valid when `-1 <= g <= f <= n - 1` for `f..g`, and when `0 <= g <= f + 1 <= n` for `f..=g`. A valid range may be empty: `a[3..3]`, `a[3..=2]`, `a[n..]`, and a reversed range whose bounds meet, are empty slices, not faults [LS-161, LS-162].
8. Every array and view is indexed from 0 in every dimension. The first element of a slice has index 0, whatever its position in the array it was taken from: in `in I64[_] v = a[2..5];`, `v[0]` is `a[2]` [LS-64, V-7].

**Faults**

9. The fault `E_BOUNDS` is raised by a range that is not valid under paragraph 7. The fault record holds the operation identifier `slice.checked.e`, with `e` as for an index (6.12.2); as operands, the dimension at rank 2 and more, then the two bounds as written, after omitted bounds are filled in, all `I64`; no exact result; and that dimension's extent as the limit. Its position is the `[` of the slice [IM-186, LS-161].

**Examples**

10. This example shows the forms of a range on one array of ten elements:

```cint slice_forms.ci
void show(in I64[_] v) {
    for i in 0..len(v) {
        "{v[i]} ";
    }
    "(len {len(v)})\n";
}

void main() {
    I64[10] a;
    for i in 0..10 {
        a[i] = i;
    }
    show(a[2..5]);
    show(a[2..=5]);
    show(a[..3]);
    show(a[7..]);
    show(a[1..8 by 3]);
    show(a[8..2 by -2]);
    show(a[.. by -1]);
    show(a[3..=2]);
    show(a[10..]);
}
```

```text
2 3 4 (len 3)
2 3 4 5 (len 4)
0 1 2 (len 3)
7 8 9 (len 3)
1 4 7 (len 3)
8 6 4 (len 3)
9 8 7 6 5 4 3 2 1 0 (len 10)
(len 0)
(len 0)
```

11. This example shows slices of a rank-2 array, and a write through a slice:

```cint slice_rank2.ci
void show(in I64[_] v) {
    for i in 0..len(v) {
        "{v[i]} ";
    }
    "\n";
}

void main() {
    I64[3, 4] m = [[1, 2, 3, 4], [5, 6, 7, 8], [9, 10, 11, 12]];
    show(m[.., 2]);
    show(m[1, 1..3]);
    inout I64[_, _] corner = m[0..2, 2..4];
    corner[1, 1] = 80;
    "{m[1, 3]} {extent(corner, 0)} {extent(corner, 1)}\n";
}
```

```text
3 7 11 
6 7 
80 2 2
```

12. This example shows a range whose first bound is above its second:

```cint slice_bounds.ci
void main() {
    I64[5] a;
    I64 lo = 4;
    I64 hi = 2;
    in I64[_] v = a[lo..hi];
    "{len(v)}\n";
}
```

```text
fault[E_BOUNDS]: index out of bounds
  --> slice_bounds.ci:5:20
   |
 5 |     in I64[_] v = a[lo..hi];
   |                    ^ slice.checked.i64
   |
   = operation: slice.checked.i64
   = operand-count: 2
   = operand: I64 4
   = operand: I64 2
   = exact:   none
   = limit:   I64 5
   ...
```

13. This example shows a step that is not a constant:

```cint slice_step.ci
void main() {
    I64[5] a;
    I64 k = 2;
    in I64[_] v = a[0..4 by k];
    "{len(v)}\n";
}
```

```text
error[C2053]: type rule
  --> slice_step.ci:4:29
   |
 4 |     in I64[_] v = a[0..4 by k];
   |                             ^
```

**Notes**

14. A range in an index has the meaning of a range in a `for` loop (6.10.4): `..` excludes its second bound and `..=` includes it.

<a id="views-as-parameters"></a>

#### 6.12.4 Views as parameters

An array passed to a function arrives as a view: the function reads, and with `inout` writes, the caller's elements in place, and nothing is copied. A view can also be a local variable, declared with `in` or `inout` (6.9.1). Every view carries its extents, so a function written for `in I64[_]` works on a whole array, a slice, a row or a column, and checks every index against the extent of what it received.

**Semantics**

1. <a id="V-3"></a>A view consists of: the storage it refers to; its element type; its rank `r`, from 1 to 4 (6.12.1); for each dimension `k`, an extent `n[k]` and a *stride* `s[k]`, the signed distance in elements between consecutive indices of that dimension; its *origin* `o`, the position in the storage of the element whose indices are all 0; and its *permission*, read or write, where write also permits reading. Strides and the origin are counted in elements, whatever the element type [V-1, V-3, M-24].
2. <a id="V-4"></a><a id="M-24a"></a>The element of a view at indices `(i[0], ..., i[r-1])` is the element at position `o + i[0] * s[0] + ... + i[r-1] * s[r-1]` of its storage, computed exactly [V-4, M-24a].
3. <a id="V-10"></a><a id="V-16"></a>A declared array is a view of its own storage with origin 0 and row-major strides: for `I32[3, 4] m`, the strides are 4 and 1. A slice, a partial index, `transpose`, `reverse` and `reshape` (7.4) each give a new view of the same storage, with other extents, strides and origin and copy no element. A stride may be negative, as in a reversed view [V-10, V-16].
4. <a id="V-11"></a>`reshape` gives a view only when the new shape addresses the same elements in the same row-major order as the view it is applied to; otherwise an implementation refuses the program (3.16, 7.4) [V-11].
5. <a id="V-5"></a><a id="V-6"></a><a id="M-25"></a>A view is formed only if every element it can address lies within its storage, and every access through a formed view checks each index against its extent (6.12.2, 6.12.3). No view reaches storage outside the array it was taken from [V-5, V-6, M-25].
6. Two views of the same storage see the same elements: a write through one is read through every other at once [V-10].
7. A view has at most the permission of the expression it is taken from. An `in` parameter or view variable has read permission and an `inout` one has write permission. An `in` view's elements are not assignable (6.12.2), and an `in` view is not an argument for an `inout` parameter: passing one is diagnosed C2067 [M-27, LS-162].
8. <a id="M-21"></a><a id="M-22"></a>A view is a local variable, a parameter or an argument and nothing else. A view is not stored in a module-level variable, a struct field, an array, an arena or a pool, and it does not outlive the block in which it was formed. Data that another scope or another entry reaches is held in an arena or a pool and reached through a handle (6.14) [M-21, M-22].
9. Arguments for array parameters, size parameters and the shapes a parameter accepts are in 6.9.5. The extent `_` accepts any extent (6.12.1), and a size parameter names an extent so that other parameters and local arrays can use it.

**Examples**

10. This example shows one function, five kinds of argument. Every call passes a view; none copies:

```cint view_args.ci
I64 total(in I64[_] xs) {
    I64 s = 0;
    for i in 0..len(xs) {
        s += xs[i];
    }
    return s;
}

void main() {
    I64[2, 3] m = [[1, 2, 3], [4, 5, 6]];
    I64[4] a = [10, 20, 30, 40];
    "{total(a)} {total(a[1..3])} {total(m[1])} {total(m[.., 2])} {total(a[.. by -1])}\n";
}
```

```text
100 50 15 9 100
```

11. Writes through an `inout` view reach the caller's array, and a transposed view reads the same storage with its strides exchanged:

```cint view_inout.ci
void bump(inout I64[_] row) {
    for j in 0..len(row) {
        row[j] += 1;
    }
}

void main() {
    I64[2, 3] m;
    bump(m[1]);
    bump(m[.., 0]);
    in I64[_, _] t = transpose(m);
    "{m[0, 0]} {m[1, 0]} {m[1, 2]} {t[2, 1]} {extent(t, 0)}\n";
}
```

```text
1 2 1 1 3
```

12. A slice of an `in` parameter is read-only and is not passed for an `inout` parameter:

```cint view_permission.ci
void clear(inout I64[_] v) {
    v[0] = 0;
}

void first_two(in I64[_] xs) {
    clear(xs[0..2]);
}

void main() {
    I64[3] a = [1, 2, 3];
    first_two(a);
}
```

```text
error[C2067]: parameter or argument rule
  --> view_permission.ci:6:11
   |
 6 |     clear(xs[0..2]);
   |           ^
```

**Notes**

13. A C function that takes a pointer and a length trusts the length it is given. A cint view carries its extents and strides with it, and the callee indexes it with the same checks as the array it came from.

<a id="aliasing-and-copies"></a>

#### 6.12.5 Aliasing and copies

Elements move between arrays only by an explicit copy: an array declared with an initializer, an array assignment or `copy`. A copy requires equal shapes, and its source and destination must not overlap unless they are the same view. When they might overlap, the program copies through storage it declares; no implementation inserts a hidden temporary.

**Constraints**

1. <a id="LS-70"></a>The source and destination of an array assignment, an array initializer and `copy(dst, src)` shall have the same shape. A difference known at translation is diagnosed C2012 [LS-70].
2. When the source and destination of an array assignment or a `copy` provably overlap at translation, and are not the same view, the program is diagnosed C5010 [LS-70].

**Semantics**

3. <a id="LS-69"></a>A declaration with a type and no permission keyword declares a new array. If its initializer is an array or a view, the elements are copied into the new array, and a write to the new array after the declaration does not reach the source: `I32[8, 8] left = m[.., 0..8];` is a copy. A declaration with `in` or `inout` declares a view, which shares the storage of its initializer: `inout I32[_, _] left = m[.., 0..8];` writes `m` [LS-69].
4. Array assignment `b = a`, where `b` is an array, a view with write permission or a slice of one, and `copy(dst, src)` copy every element of the source to the destination in row-major order [LS-70].
5. <a id="IM-187"></a>The run-time checks of a copy follow the evaluation of the destination place and the source in their order (6.2.4). The shape check comes first, then the overlap check, and only then is any element written. A copy that faults writes no element [IM-187, LS-70].
6. The source and destination of a copy shall be shown disjoint, or be exactly the same view. They are shown disjoint when either has no elements, when the lowest and highest positions each addresses do not overlap, or when both are views of one storage whose strides share a common factor `g` of at least 2 and whose origins differ modulo `g`. SPEC-02P.2 gives the procedure in full. A copy of a view onto exactly the same view has no effect. Any other pair faults `E_ALIAS` [LS-70].
7. To move data within one array between places that may overlap, a program copies through an array it declares. No implementation stages a copy through hidden storage [LS-70].
8. Arguments bound to `inout` parameters of a function call follow the overlap rule of 6.9.5.

**Faults**

9. The fault `E_SHAPE` is raised by a copy whose source and destination differ in shape at run time. The fault record holds the operation identifier `copy.shape`; two operands, the lowest dimension in which the extents differ, counted from 0, and the source's extent in that dimension, both `I64`; no exact result; and the destination's extent in that dimension as the limit, an `I64`. Its position is the `=` of the assignment or declaration, or the name `copy` [IM-187].
10. The fault `E_ALIAS` is raised by a copy whose source and destination are not shown disjoint at run time and are not the same view. The fault record holds the operation identifier `copy.alias`, no operands, no exact result and no limit. Its position is the `=` or the name `copy` [IM-187, LS-70].

**Examples**

11. This example shows a copy and a view from the same initializer:

```cint copy_or_view.ci
void main() {
    I64[2, 4] m = [[1, 2, 3, 4], [5, 6, 7, 8]];
    I64[2, 2] left = m[.., 0..2];
    inout I64[_, _] right = m[.., 2..4];
    left[0, 0] = 100;
    right[0, 0] = 300;
    "{m[0, 0]} {m[0, 2]} {left[0, 0]}\n";
}
```

```text
1 300 100
```

12. Copies that are accepted: through a declared array, onto the same view, and between the even and odd elements of one array, which share storage but no element:

```cint copy_accepted.ci
void main() {
    I64[8] x = [0, 1, 2, 3, 4, 5, 6, 7];
    I64[4] tmp;
    tmp = x[2..6];
    x[0..4] = tmp;
    x[0..8] = x[0..8];
    x[0..8 by 2] = x[1..8 by 2];
    for i in 0..8 {
        "{x[i]} ";
    }
    "\n";
}
```

```text
3 3 5 5 5 5 7 7 
```

13. This example shows overlap known at translation. `x[0..8 by 2]` addresses positions 0, 2, 4 and 6 and `x[0..4]` positions 0 to 3:

```cint copy_overlap.ci
void main() {
    I64[16] x;
    x[0..8 by 2] = x[0..4];
}
```

```text
error[C5010]: parameter mode, alias, or ABI rule
  --> copy_overlap.ci:3:20
   |
 3 |     x[0..8 by 2] = x[0..4];
   |                    ^
```

14. This example shows overlap known only at run time. With `j` equal to 2, the source `x[2..6]` and the destination `x[0..4]` share two elements:

```cint copy_alias.ci
void shift(inout I64[_] x, I64 j) {
    copy(x[0..4], x[j..j + 4]);
}

void main() {
    I64[8] x = [0, 1, 2, 3, 4, 5, 6, 7];
    shift(x, 2);
}
```

```text
fault[E_ALIAS]: views overlap
  --> copy_alias.ci:2:5
   |
 2 |     copy(x[0..4], x[j..j + 4]);
   |     ^ copy.alias
   |
   = operation: copy.alias
   = operand-count: 0
   = exact:   none
   = limit:   none
   ...
```

15. This example shows shapes that differ at run time:

```cint copy_shape.ci
void put(inout I64[_] dst, in I64[_] src) {
    dst = src;
}

void main() {
    I64[2] dst;
    I64[3] src = [1, 2, 3];
    put(dst, src);
}
```

```text
fault[E_SHAPE]: shapes do not match
  --> copy_shape.ci:2:9
   |
 2 |     dst = src;
   |         ^ copy.shape
   |
   = operation: copy.shape
   = operand-count: 2
   = operand: I64 0
   = operand: I64 3
   = exact:   none
   = limit:   I64 2
   ...
```

**Notes**

16. C's `memcpy` has undefined behavior on overlapping arguments, and `memmove` behaves as if it copied through a temporary. A cint copy does neither: an overlap that the tests cannot exclude is a diagnostic or a fault, and the program states where the elements go in between.

<a id="module-level-arrays"></a>

#### 6.12.6 Module-level arrays

An array declared at module level lives as long as the program's context, instead of the call that declares it. Every function of the module reads and writes it, and it can be passed as a view argument like any other array. The declaration rules for module-level variables, arrays and handles are in 6.9.3; this subclause states what differs for arrays.

**Constraints**

1. The extents of a module-level array are constant expressions, as 6.12.1 states.

**Semantics**

2. A module-level array has every element 0 unless its declaration has a constant initializer (6.9.3), and it keeps its elements between calls and between entries. It is not storage of the frame arena (5.2.6) [LS-67].
3. In a script, a variable declared among the top-level statements is a local of the script's implicit entry (5.1.2), so an array declared there is a local array, not a module-level array. A module-level array belongs to a module without top-level statements, such as one that defines `void main()`.

**Examples**

4. This example shows a module-level counter array, written by one function and read by another:

```cint module_array.ci
I64[4] counts;
I64[2, 2] grid = [[1, 2], [3, 4]];

void tally(I64 k) {
    counts[k] += 1;
}

I64 total(in I64[_] v) {
    I64 s = 0;
    for i in 0..len(v) {
        s += v[i];
    }
    return s;
}

void main() {
    tally(1);
    tally(1);
    tally(3);
    grid[1, 0] += counts[1];
    "{counts[0]} {counts[1]} {counts[3]} {total(counts)} {grid[1, 0]}\n";
}
```

```text
0 2 1 3 5
```

5. An array declared among the top-level statements of a script is a local of the script's entry:

```cint script_array.ci
I64[3] a = [1, 2, 3];
a[0] += 40;
"{a[0]}\n";
```

```text
41
```

6. This example shows a module-level extent that is not a constant:

```cint module_array_extent.ci
I64 n = 3;
I64[n] a;

void main() {
    "{len(a)}\n";
}
```

```text
error[C6004]: constant expression required
  --> module_array_extent.ci:2:5
   |
 2 | I64[n] a;
   |     ^
```

<a id="kernels"></a>

### 6.13 Kernels

A kernel is a function written for one point of an iteration space. A *dispatch* runs it once for every point, each run a *work-item*, over arrays passed as arguments. Every work-item reads the values its arguments held before the dispatch and writes only its own part of the outputs, so no work-item can see another, and the outputs are the same whatever order the work-items run in. If every work-item succeeds, all outputs appear at once; if any faults, none does.

This section states the meaning of a kernel and of a dispatch: declarations (6.13.1), parameters (6.13.2), the body (6.13.3), the dispatch statement (6.13.4), reductions (6.13.5) and the reference meaning that every executor reproduces (6.13.6). The executors of cint 0.2.2 that run kernels, and what each of them shall do to give that meaning, are in SPEC-02P.2.

<a id="kernel-declarations"></a>

#### 6.13.1 Kernel declarations

A kernel declaration names the kernel, its size parameters (3.34), its parameters with a mode for each, and usually an *over clause* that gives the iteration space. The body is written once, for one work-item, using the over clause's variables as that work-item's indices.

**Syntax**

```ebnf
kernel_decl  = "kernel" IDENT size_params "(" [ kparam { "," kparam } [ "," ] ] ")"
               [ over_clause ] [ where_clause ] block ;
kparam       = ( "in" | "out" | "inout" ) type IDENT ;
over_clause  = "over" "[" IDENT ":" shape_expr { "," IDENT ":" shape_expr } "]" ;
where_clause = "where" constraint { "," constraint } ;
constraint   = shape_expr ( "==" | "<" | "<=" | ">" | ">=" ) shape_expr ;
shape_expr   = IDENT | INT_LIT | IDENT ( "+" | "-" ) INT_LIT ;
```

Each extent in the type of an array parameter is a `shape_expr`, and `INT_LIT` is an integer literal (6.4.5). `size_params` is as for a function (6.9.4), and `export` comes before a kernel as before any `declaration` (6.15.1).

**Constraints**

1. <a id="LS-244"></a>Every parameter has a mode, `in`, `out` or `inout`; a parameter without one is a syntax error (C1050). A kernel has no result type: a type before the kernel's name is diagnosed C1010 [LS-244].
2. <a id="K-5"></a>Each size parameter appears alone as an extent of at least one array parameter. A size parameter that appears in no such extent is diagnosed C2065 [K-5].
3. <a id="K-3"></a>A kernel has at least one parameter of mode `out` or `inout`. A kernel without one is diagnosed C9102 at its name [K-3].
4. <a id="LS-246"></a>An array parameter has an integer or fixed-point element type, and a scalar parameter an integer or fixed-point type. A parameter of any other type, such as `Bool` or a struct, is diagnosed C5028 [LS-246].
5. A scalar parameter has mode `in` or `out`. A scalar `inout` parameter is diagnosed C2064 [K-2].
6. <a id="K-8"></a>With an over clause of `d` variables, every array parameter of mode `out` or `inout` has rank `d` or more, and its first `d` extents are the over clause's extents, written the same way and in the same order. A parameter of lower rank or with other leading extents is diagnosed C9102 at its name [K-8].
7. Each extent of an array parameter, each over extent and each side of a `where` constraint is a shape expression: a size parameter, a decimal literal or a size parameter plus or minus a decimal literal. A `where` constraint compares two of them with `==`, `<`, `<=`, `>` or `>=`. Any other extent or comparison, such as `I64[n * 2]` or `where n != 0`, is diagnosed C9102 [K-1].

**Semantics**

8. <a id="LS-243"></a>A kernel body is written for one work-item [LS-243].
9. Size parameters are `I64` values, bound when a dispatch begins from the extents of the arguments (6.13.2). A `where` clause states requirements on them, checked at the same time [LS-244, K-6].
10. <a id="LS-247"></a>The over clause `over [v1: E1, ..., vd: Ed]` gives the *iteration space*: one work-item for each index tuple `(v1, ..., vd)` with `0 <= vk < Ek`. Each over variable is an `I64` that holds its work-item's index in that dimension. If any `Ek` is 0, the dispatch has no work-items [K-8, LS-247].
11. <a id="K-9"></a>The *work-item index* is the position of a work-item's tuple in row-major order, counted from 0: for `over [y: h, x: w]`, the work-item `(y, x)` has index `y * w + x` [K-9].
12. For an array parameter of mode `out` or `inout` and rank `r`, the dimensions after the first `d` form the *owned block*: work-item `(v1, ..., vd)` owns the elements whose first `d` indices are `v1, ..., vd`. With `over [i: n]` and `out I64[n, k] q`, work-item `i` owns the row `q[i, ..]`; with `out I64[n] g`, it owns the one element `g[i]` [K-8].
13. When a kernel's only outputs are scalar `out` parameters, its over extents may be any shape expressions [K-8].
14. A kernel without an over clause is in element form (6.13.3) [LS-247].

**Examples**

15. This example shows a kernel over a rank-2 iteration space. Each work-item computes one element of `b`:

```cint kernel_scale.ci
kernel scale[h, w](in I32[h, w] a, in I32 k, out I64[h, w] b) over [y: h, x: w] {
    b[y, x] = (a[y, x] as I64) * (k as I64);
}

void main() {
    I32[2, 3] a = [[1, 2, 3], [4, 5, 6]];
    I64[2, 3] b;
    scale(a, 7, b);
    "{b[0, 1]} {b[1, 2]}\n";
}
```

```text
14 42
```

16. This example shows an over extent that is a shape expression, and a work-item for each adjacent pair:

```cint kernel_diffs.ci
kernel diffs[n](in I64[n] x, out I64[n - 1] d) over [i: n - 1] {
    d[i] = x[i + 1] - x[i];
}

void main() {
    I64[4] x = [1, 4, 9, 16];
    I64[3] d;
    diffs(x, d);
    "{d[0]} {d[1]} {d[2]}\n";
}
```

```text
3 5 7
```

17. `Bool` is not a kernel parameter type:

```cint kernel_bool.ci
kernel pick[n](in Bool[n] keep, out I64[n] b) over [i: n] {
    b[i] = 0;
}

void main() {
    "unreached\n";
}
```

```text
error[C5028]: parameter mode, alias, or ABI rule
  --> kernel_bool.ci:1:16
   |
 1 | kernel pick[n](in Bool[n] keep, out I64[n] b) over [i: n] {
   |                ^
```

<a id="parameter-modes-and-shapes"></a>

#### 6.13.2 Parameter modes and shapes

The mode of a kernel parameter says what the body may do with it. An `in` array is read, an `out` array is written, and an `inout` array is both read and written; a scalar `in` is a value shared by every work-item, and a scalar `out` receives the result of a reduction (6.13.5). When a dispatch begins, the size parameters are bound from the arguments, every extent is checked, and the arguments are checked for overlap, before any work-item runs.

| Mode | Kind | The body may | The body may not | Argument |
|---|---|---|---|---|
| `in` | array | read any element | write | a view, with read or write permission |
| `in` | scalar | read the value, which is the same for every work-item | assign it | a value of exactly the parameter's type |
| `out` | array | write the elements of its owned block | read | a view with write permission |
| `out` | scalar | be the target of one `reduce` statement | read or assign it | a variable of exactly the parameter's type |
| `inout` | array | read any element; write the elements of its owned block | write other elements | a view with write permission |

**Constraints**

1. <a id="K-2"></a>An `in` parameter is not assigned: a write to an element of an `in` array or to a scalar `in` is diagnosed C2060 [K-2].
2. The body does not read an array `out` parameter, and it does not read or assign a scalar `out` parameter except through its `reduce` statement. Such a read or assignment is diagnosed C9102 [K-2].
3. <a id="K-4"></a><a id="K-7"></a>An array argument has the element type and the rank of its parameter (C2001). An argument for an `out` or `inout` array has write permission (C2067). When the extents of every argument are known at translation, the checks of paragraph 6 run then, and a mismatch is diagnosed C2012 [K-2, K-4, K-7].
4. An argument for a scalar `in` parameter has exactly the parameter's type (C2001); a literal argument takes the parameter's type. An argument for a scalar `out` parameter is a variable of exactly the parameter's type: another type is diagnosed C2001, and an argument that is not a variable, such as a literal, is diagnosed C2067 [K-2, K-4].
5. <a id="LS-245"></a>An argument for an `out` or `inout` parameter that provably overlaps another array argument at translation, such as the same array passed twice, is diagnosed C5010 [LS-245].

**Semantics**

6. <a id="K-6"></a>Size parameters are bound in two passes over the array parameters, in declaration order and, within each, dimension by dimension. The first pass binds each size parameter at its first occurrence alone as an extent, to the argument's extent there. The second pass evaluates every extent with checked `I64` arithmetic and compares it with the argument's extent; the first difference faults. For `kernel k[n](in I64[n + 1] a, out I64[n] b)` with `a` of extent 5 and `b` of extent 3, the first pass binds `n` to 3 from `b`, and the second reports `a`, whose extent should be 4 [K-6].
7. <a id="K-1"></a>After binding, the constraints of the `where` clause are evaluated in written order, and the first that is false faults [K-1].
8. <a id="A-1"></a>`in` arguments may overlap each other, and one view may be passed to several `in` parameters [A-1].
9. <a id="A-2"></a>The elements of every `out` or `inout` argument shall be disjoint from the elements of every other array argument of the same dispatch, whatever its mode [A-2].
10. <a id="A-3"></a>Overlap is decided by the tests of 6.12.5, paragraph 6, given in full in SPEC-02P.2. A pair those tests show disjoint is accepted; every other pair, whether it overlaps or the tests cannot decide, faults `E_ALIAS`. Some pairs that share no element therefore fault. The same tests apply to every dispatch, whether cint code or a host program issues it, and nothing a program states about its arguments counts as evidence that they are disjoint [A-3].
11. <a id="A-4"></a><a id="M-26"></a>Every view bound to an `out` or `inout` parameter shall address a distinct element for each index tuple. A view that fails the test of SPEC-02P.2 for this property faults `E_ALIAS` [A-4, M-26].
12. These checks run when the dispatch begins, before any work-item. A fault here is in the *entry phase* of the dispatch: its address has a dispatch number and no work-item or step (6.13.6).

**Faults**

13. The fault `E_SHAPE` is raised by an argument extent that differs from the extent the second pass computes. The fault record holds the operation identifier `bind.shape`; three operands, the parameter's position counted from 0, the dimension counted from 0 and the argument's extent, all `I64`; no exact result; and the computed extent as the limit, an `I64`. Its position is the kernel name of the dispatch [K-6].
14. The fault `E_OVERFLOW` is raised when an extent expression `n + c` or `n - c` leaves the range of `I64`, with the operation identifier `add.checked.i64` or `sub.checked.i64`. A computed extent below 0 faults `E_SHAPE` [K-6].
15. The fault `E_SHAPE` is raised by a false `where` constraint. The fault record holds the operation identifier `bind.where`; three operands, the constraint's position in the clause counted from 0 and the values of its left and right sides, all `I64`; no exact result and no limit [K-1].
16. The fault `E_ALIAS` is raised by a pair of arguments not shown disjoint. The fault record holds the operation identifier `bind.alias` and two `I64` operands: the position of the `out` or `inout` parameter and the position of the other parameter. It has no exact result and no limit [A-3].

**Examples**

17. This example shows the results of the overlap tests for `kernel inc[n](in I32[n] u, out I32[n] v)`. Each row shows the outcome of the program written in its first column:

| Dispatch | Result |
|---|---|
| `inc(a, b)` with two arrays | accepted |
| `inc(a, a)` | C5010 |
| `inc(p[0], p[1])` with `I32[2, 64] p` | accepted: the rows do not overlap |
| `inc(m[0..128 by 2], m[1..128 by 2])` with `I32[128] m` | accepted: the even and odd elements share no element |
| `inc(a[0..4], a[4..8])` with `I32[8] a` | accepted |
| `inc(q[0..8], q[1..9])` with `I32[9] q` | `E_ALIAS`: the views share seven elements |
| `inc(a, reverse(a, 0))` | `E_ALIAS`: the views are the same elements in another order |

18. This example shows the same array for an `in` and an `out` parameter:

```cint kernel_same_array.ci
kernel inc[n](in I32[n] u, out I32[n] v) over [i: n] {
    v[i] = u[i] + 1;
}

void main() {
    I32[16] a;
    inc(a, a);
}
```

```text
error[C5010]: parameter mode, alias, or ABI rule
  --> kernel_same_array.ci:7:12
   |
 7 |     inc(a, a);
   |            ^
```

19. The two passes of paragraph 6, with extents that the compiler does not see because they arrive through views:

```cint kernel_shape.ci
kernel pair_sums[n](in I64[n + 1] a, out I64[n] b) over [i: n] {
    b[i] = a[i] + a[i + 1];
}

void sums(in I64[_] a, inout I64[_] b) {
    pair_sums(a, b);
}

void main() {
    I64[5] a;
    I64[3] b;
    sums(a, b);
}
```

```text
fault[E_SHAPE]: shapes do not match
  --> kernel_shape.ci:6:5
   |
 6 |     pair_sums(a, b);
   |     ^ bind.shape
   |
   = operation: bind.shape
   = operand-count: 3
   = operand: I64 0
   = operand: I64 0
   = operand: I64 5
   = exact:   none
   = limit:   I64 4
   ...
   = address: kernel_shape.pair_sums, dispatch 0, phase 0, work-item none, step none
   ...
```

The operands name parameter 0, `a`, and its dimension 0, whose extent is 5 where `n + 1` is 4. The address line names the kernel, the dispatch number, the entry phase (0) and no work-item or step.

20. This example shows a `where` clause, and overlapping slices of one array:

```cint kernel_where.ci
kernel smooth[n](in I64[n] u, out I64[n] v) over [i: n] where n >= 3 {
    v[i] = u[i];
}

void main() {
    I64[2] u;
    I64[2] v;
    smooth(u, v);
}
```

```text
fault[E_SHAPE]: shapes do not match
  --> kernel_where.ci:8:5
   |
 8 |     smooth(u, v);
   |     ^ bind.where
   |
   = operation: bind.where
   = operand-count: 3
   = operand: I64 0
   = operand: I64 2
   = operand: I64 3
   = exact:   none
   = limit:   none
   ...
```

Overlapping slices of one array:

```cint kernel_alias.ci
kernel inc[n](in I32[n] u, out I32[n] v) over [i: n] {
    v[i] = u[i] + 1;
}

void main() {
    I32[9] q;
    inc(q[0..8], q[1..9]);
}
```

```text
fault[E_ALIAS]: views overlap
  --> kernel_alias.ci:7:5
   |
 7 |     inc(q[0..8], q[1..9]);
   |     ^ bind.alias
   |
   = operation: bind.alias
   = operand-count: 2
   = operand: I64 1
   = operand: I64 0
   = exact:   none
   = limit:   none
   ...
```

**Notes**

21. A program whose arguments share no element but fail the overlap tests chooses a layout that the tests accept, or copies one argument first.

<a id="the-body"></a>

#### 6.13.3 The body

A kernel body is ordinary sequential code with three kinds of limit: it writes outputs only at its own work-item's indices, it writes every element it owns, and it does nothing whose effect another work-item or the rest of the program could see while the dispatch runs.

**Constraints**

1. An over variable is not assigned (C2052) [LS-247].
2. <a id="LS-248"></a><a id="K-10"></a>Every write to an array parameter of mode `out` or `inout` has the over variables, in order, as its first indices. Any further indices address the owned block and may be any `I64` expressions. A write of another form is diagnosed C5031 [LS-248, K-10].
3. <a id="K-10a"></a>Every element of the owned block of every array `out` parameter is written on every path through the body that completes. A kernel satisfies this rule when its writes fit one of two patterns; a kernel whose writes fit neither is diagnosed C5031 [LS-248, K-10a]:
   - a write whose indices are exactly the over variables, executed unconditionally, in both branches of an `if` with an `else`, or in every case of a `switch` with a `default`;
   - a nest of `for` loops, one for each dimension of the owned block, each over that dimension's full range `0..E` with a bound that is the same for every work-item, whose innermost body writes the element with the loop variables in dimension order, unconditionally and before any `break` or `continue`; the nest itself runs under the conditions of the first pattern.
4. <a id="LS-250"></a><a id="K-11"></a>A kernel body does not contain a print statement (C5020), a `defer` statement (C5021), `try` or a value of an error union type (C5022), a dispatch (C5023) or a use of a module-level variable (C5025). Named constants and the built-in operations of clause 7 may be used [LS-250, K-11].
5. <a id="K-13"></a>A local array in a kernel body, a *private array*, has constant extents and its number of elements times the width of its element type, counting a `Bool` element as 8 bits, is at most 32,768 bits: at most 512 `I64` elements, or 4,096 `U8` elements. A private array with an extent that is not constant is diagnosed C9102 at that extent, and a private array above this size is diagnosed C9102 at its name [K-13, LS-250].
6. <a id="K-12"></a>A body without an over clause consists of assignments whose left side is a whole array parameter of mode `out` or `inout` and whose right side is an elementwise expression over array parameters of the same shape and scalar `in` parameters. A body without an over clause that holds anything else is diagnosed C9102 at the kernel's name [K-12].

**Semantics**

7. <a id="X-7"></a>Each work-item runs the body as a sequential program, in the evaluation order of 6.2.4, and every checked operator is checked where it occurs. An executor may reorder or combine work only when the outputs, and which fault occurs first with its operands, are unchanged [X-7].
8. <a id="LS-256"></a>Size parameters, over variables and the arithmetic on indices are `I64` [LS-256].
9. A work-item may write an element of its owned block more than once, under `if` and in loops. The value published for the element is the last one written in the work-item's evaluation order. An element of an `inout` owned block that the work-item does not write keeps the value it had before the dispatch [K-10, K-10a].
10. A body in element form means the same as the indexed form with one over variable for each dimension: `next = old + step;` over parameters of shape `[n]` is `next[i] = old[i] + step[i];` with `over [i: n]`. A scalar `in` parameter in the expression is the same value for every element [K-12, LS-247].

11. A kernel body that calls a user function is outside this edition (6.16), and an implementation refuses a program that has one (3.16).

**Examples**

12. This example shows element form, with a scalar parameter:

```cint kernel_element_form.ci
kernel scale_add[n](in I64[n] a, in I64 k, out I64[n] b) {
    b = a * k + 1;
}

void main() {
    I64[3] a = [1, 2, 3];
    I64[3] b;
    scale_add(a, 10, b);
    "{b[0]} {b[1]} {b[2]}\n";
}
```

```text
11 21 31
```

13. Writes that cover the owned block: in both branches of an `if`, and in a loop over the block. Each work-item of `row_sums` owns one element of `s` and one row of `c`:

```cint kernel_coverage.ci
kernel clamp_negative[n](in I64[n] a, out I64[n] b) over [i: n] {
    if (a[i] < 0) {
        b[i] = 0;
    } else {
        b[i] = a[i];
    }
}

kernel row_sums[n, k](in I64[n, k] a, out I64[n] s, out I64[n, k] c) over [i: n] {
    I64 t = 0;
    for j in 0..k {
        t += a[i, j];
        c[i, j] = t;
    }
    s[i] = t;
}

void main() {
    I64[3] a = [-1, 2, -3];
    I64[3] b;
    clamp_negative(a, b);
    I64[2, 3] m = [[1, 2, 3], [4, 5, 6]];
    I64[2] s;
    I64[2, 3] c;
    row_sums(m, s, c);
    "{b[0]} {b[1]} {b[2]}\n";
    "{s[0]} {s[1]} {c[1, 0]} {c[1, 1]} {c[1, 2]}\n";
}
```

```text
0 2 0
6 15 4 9 15
```

14. The last write is the one published:

```cint kernel_last_write.ci
kernel flag_positive[n](in I64[n] a, out I64[n] b) over [i: n] {
    b[i] = 1;
    if (a[i] > 0) {
        b[i] = 2;
    }
}

void main() {
    I64[2] a = [0, 5];
    I64[2] b;
    flag_positive(a, b);
    "{b[0]} {b[1]}\n";
}
```

```text
1 2
```

15. This example shows a write outside the owned block:

```cint kernel_write_rule.ci
kernel spill[n](in I64[n] a, out I64[n] b) over [i: n] {
    b[i + 1] = a[i];
}

void main() {
    "unreached\n";
}
```

```text
error[C5031]: parameter mode, alias, or ABI rule
  --> kernel_write_rule.ci:2:5
   |
 2 |     b[i + 1] = a[i];
   |     ^
```

16. A write that some work-items skip leaves part of the output unwritten:

```cint kernel_coverage_missing.ci
kernel partial[n](in I64[n] a, out I64[n] b) over [i: n] {
    if (i > 0) {
        b[i] = a[i];
    }
}

void main() {
    "unreached\n";
}
```

```text
error[C5031]: parameter mode, alias, or ABI rule
  --> kernel_coverage_missing.ci:3:9
   |
 3 |         b[i] = a[i];
   |         ^
```

17. A kernel body does not print:

```cint kernel_print.ci
kernel noisy[n](in I64[n] a, out I64[n] b) over [i: n] {
    "i={i}\n";
    b[i] = a[i];
}

void main() {
    "unreached\n";
}
```

```text
error[C5020]: parameter mode, alias, or ABI rule
  --> kernel_print.ci:2:5
   |
 2 |     "i={i}\n";
   |     ^
```

18. An index in the owned block is checked against the block's extent. Here work-item 0 reads `a[0, 3]` in a dimension of extent 3:

```cint kernel_block_bounds.ci
kernel shift_rows[n, k](in I64[n, k] a, out I64[n, k] b) over [i: n] {
    for j in 0..k {
        b[i, j] = a[i, j + 1];
    }
}

void main() {
    I64[2, 3] a;
    I64[2, 3] b;
    shift_rows(a, b);
}
```

```text
fault[E_BOUNDS]: index out of bounds
  --> kernel_block_bounds.ci:3:20
   |
 3 |         b[i, j] = a[i, j + 1];
   |                    ^ index.checked.i64
   |
   = operation: index.checked.i64
   = operand-count: 2
   = operand: I64 1
   = operand: I64 3
   = exact:   none
   = limit:   I64 3
   ...
   = address: kernel_block_bounds.shift_rows, dispatch 0, phase 1, work-item 0, step 8
   ...
```

<a id="dispatch"></a>

#### 6.13.4 Dispatch

A dispatch is written like a call statement: the kernel's name and its arguments. It runs every work-item, then publishes the outputs, before the next statement runs.

**Syntax**

```ebnf
call = IDENT [ "." IDENT ] "(" [ args ] ")" ;
```

A dispatch is a `call` whose callee names a kernel, standing as a statement (6.10.1). `args` is as for any call (6.7.4).

**Constraints**

1. <a id="LS-251"></a>A dispatch is a statement and has no value. A dispatch used as an expression is diagnosed C2001, and `try` before a dispatch is diagnosed C2043 [LS-251].
2. A dispatch is written in sequential code. A dispatch inside a kernel body is diagnosed C5023 (6.13.3).

**Semantics**

3. <a id="K-14"></a>A dispatch runs its kernel's work-items as 6.13.6 defines. It is synchronous: when it completes, the statement after it runs and observes the published outputs [K-14, LS-251].
4. <a id="P-1"></a>Every dispatch is *staged*. The work-items write into storage of their own, which the program cannot observe. If the dispatch completes without a fault, every `out` and `inout` array argument and every scalar `out` variable is *published*: given its new elements at once. If the dispatch faults, nothing is published, and every argument and every variable bound to a scalar `out` holds exactly what it held before the dispatch [P-1, LS-251].
5. <a id="P-2"></a>No statement after a dispatch can observe some of its outputs updated and others not [P-2].
6. <a id="P-4"></a>In a dispatch, an `inout` parameter has copy-in, copy-out meaning. Every read of it, at any index and at any time during the dispatch, gives the element's value from before the dispatch, including a work-item's read of an element it has already written. Elements that no work-item writes keep their values [P-4, LS-251].
7. A fault in a dispatch ends the entry, as any fault does (5.2.3): no statement of the entry after it runs [K-14].
8. <a id="P-3"></a><a id="LS-288"></a>Every dispatch takes the next *dispatch number* of the entry, from 0 in program order, whether it completes or faults [P-3, LS-288].
9. <a id="K-15"></a>A dispatch issued through the host interface makes the same checks in the same order and has the same outcome as the same dispatch written in cint source. SPEC-03P.2 describes that interface [K-15].

**Examples**

10. This example shows copy-in and copy-out. Each work-item but the last copies its right neighbor, and every read sees the value from before the dispatch:

```cint dispatch_inout.ci
kernel shift_left[n](inout I64[n] t) over [i: n] {
    if (i + 1 < n) {
        t[i] = t[i + 1];
    }
}

void main() {
    I64[4] t = [1, 2, 3, 4];
    shift_left(t);
    "{t[0]} {t[1]} {t[2]} {t[3]}\n";
}
```

```text
2 3 4 4
```

11. A step that needs the whole previous state alternates between two arrays, one dispatch per step. Each dispatch reads one array and publishes the other:

```cint dispatch_double_buffer.ci
kernel smooth[n](in I64[n] u, out I64[n] v) over [i: n] where n >= 3 {
    I64 r = u[i];
    if (i > 0 && i < n - 1) {
        r = (u[i - 1] + 2 * u[i] + u[i + 1]) / 4;
    }
    v[i] = r;
}

void main() {
    I64[5] a = [0, 0, 64, 0, 0];
    I64[5] b;
    for step in 0..2 {
        smooth(a, b);
        smooth(b, a);
    }
    for i in 0..5 {
        "{a[i]} ";
    }
    "\n";
}
```

```text
0 12 17 12 0 
```

12. A dispatch has no value:

```cint dispatch_try.ci
kernel copy1[n](in I64[n] a, out I64[n] b) over [i: n] {
    b[i] = a[i];
}

void main() {
    I64[3] a;
    I64[3] b;
    try copy1(a, b);
}
```

```text
error[C2043]: type rule
  --> dispatch_try.ci:8:5
   |
 8 |     try copy1(a, b);
   |     ^
```

**Notes**

13. A view of an output formed before a dispatch reads the published elements after it, as every view of the same storage does (6.12.4):

```cint dispatch_view.ci
kernel inc[n](in I64[n] a, out I64[n] b) over [i: n] {
    b[i] = a[i] + 1;
}

void main() {
    I64[3] a = [10, 20, 30];
    I64[3] b;
    in I64[_] head = b[0..2];
    inc(a, b);
    "{head[0]} {head[1]}\n";
}
```

```text
11 21
```

<a id="reductions-in-kernels"></a>

#### 6.13.5 Reductions in kernels

A reduction combines one value from each work-item into one scalar result: a sum, a count, a minimum or a maximum. It is written as a `reduce` statement whose target is a scalar `out` parameter. Contributions are combined in ascending work-item index, so a reduction whose result depends on order, such as a checked fold, has one result whatever order the work-items ran in.

**Syntax**

```ebnf
reduce_stmt = "reduce" IDENT "=" reduction ";" ;
reduction   = ( "sum" | "sum_wrap" | "sum_sat" | "min" | "max" | "count" ) "(" expr ")"
            | "fold_checked" "(" "add" "," expr "," expr ")" ;
```

**Constraints**

1. <a id="R-1"></a>A `reduce` statement appears only in a kernel body, and its target is a scalar `out` parameter. A `reduce` statement outside a kernel body, or with another target, such as a local variable, is diagnosed C9102 [R-1].
2. <a id="LS-249"></a>Each scalar `out` parameter is the target of exactly one `reduce` statement. A `reduce` statement is not inside a loop; it may be inside an `if`. A scalar `out` parameter that is the target of no `reduce` statement, a second `reduce` statement to one target, and a `reduce` statement inside a loop are diagnosed C9102 [R-1, LS-249].
3. The contribution of `sum(e)` has an integer type, and the target has an integer type. In `fold_checked(add, init, e)`, `e` and `init` have exactly the target's type, and `init` is the same for every work-item. The contribution of `sum_wrap`, `sum_sat`, `min` and `max` has exactly the target's type. The contribution of `count(c)` is a `Bool`, and the target is an `I64`. Another type is diagnosed C2001; a conversion of the contribution, as in `fold_checked(add, 0, x[i] as I64)`, gives it the target's type [LS-249].
4. For fixed-point contributions, the target of `sum` has the type of the contributions, as 7.3 states for `sum(R, xs)`. A target of another type is diagnosed C9102.

**Semantics**

5. A work-item that executes the `reduce` statement contributes the value of its expression; a work-item that does not execute it contributes nothing. Each work-item contributes at most once to each target [R-1].
6. <a id="R-2"></a>The *logical order* of contributions is ascending work-item index [R-2].
7. Each form is the operation of 7.3 applied to the contributions in logical order, with the target's type as the result type [LS-249]:
   - `sum(e)`: the exact sum, checked once against the target's range after the last contribution;
   - `fold_checked(add, init, e)`: `init`, then each contribution added in logical order with a checked `+` in the target's type;
   - `sum_wrap(e)`: the exact sum reduced modulo 2^`w` into the target's range, which is the same as adding with `+%` in any order;
   - `sum_sat(e)`: `init` 0, then each contribution added in logical order with `+|`;
   - `min(e)`, `max(e)`: the least or greatest contribution;
   - `count(c)`: the number of contributions that are `true`.
8. <a id="R-3"></a>With no contributions, `sum`, `sum_wrap`, `sum_sat` and `count` give 0 and `fold_checked` gives `init`; `min` and `max` fault [R-3, LS-249].
9. The target receives its result when the dispatch publishes its outputs (6.13.4), and not if the dispatch faults.
10. On fixed-point contributions, each form acts on the storage integers and nothing rounds, as 7.3 states.

**Faults**

11. <a id="R-6"></a>The fault `E_OVERFLOW` is raised by `fold_checked` at the first contribution `k` in logical order whose partial result leaves the target's range. The fault is attributed to work-item `k`, at the step of its `reduce` statement. The fault record holds the operation identifier `fold_checked.add.t`, where `t` is the target type in lower case; three operands, the index `k` as an `I64`, the accumulator before `k` and the contribution; the exact result; and the bound it crossed as the limit [R-6, LS-249].
12. <a id="R-7"></a>The fault `E_OVERFLOW` is raised by `sum` when the exact sum is outside the target's range. The fault is in the *epilogue* of the dispatch, after every work-item. The record holds the operation identifier `sum.checked.e.r`, where `e` is the contribution type and `r` the target type; one operand, the number of contributions as an `I64`; the exact sum; and the bound it crossed as the limit [LS-249, R-7].
13. The fault `E_SHAPE` is raised in the epilogue by `min` or `max` with no contributions. The record holds the operation identifier `reduce_min.checked.t` or `reduce_max.checked.t` and one operand, `I64 0` [LS-249, R-7].
14. Epilogue faults occur only when no work-item faulted. Among themselves they are ordered by the declaration order of their targets [R-7].

**Examples**

15. This example shows six reductions over one input:

```cint reduce_forms.ci
kernel stats[n](in I32[n] x, out I64 total, out I32 lo, out I32 hi, out I64 negatives,
                out I32 wrapped, out I8 clamped) over [i: n] {
    reduce total = sum(x[i]);
    reduce lo = min(x[i]);
    reduce hi = max(x[i]);
    reduce negatives = count(x[i] < 0);
    reduce wrapped = sum_wrap(x[i]);
    reduce clamped = sum_sat((x[i] / 1000000000) as I8);
}

void main() {
    I32[4] x = [2147483647, 5, -7, 2147483647];
    I64 total = 0;
    I32 lo = 0;
    I32 hi = 0;
    I64 negatives = 0;
    I32 wrapped = 0;
    I8 clamped = 0;
    stats(x, total, lo, hi, negatives, wrapped, clamped);
    "{total} {lo} {hi} {negatives} {wrapped} {clamped}\n";
}
```

```text
4294967292 -7 2147483647 1 -4 3
```

`total` holds the exact sum in an `I64`; `wrapped` holds the same sum reduced into `I32`. `clamped` adds the quotients 2, 0, -1 and 2, since integer `/` is floor division (6.7.7).

16. `sum_sat` depends on the order of its contributions, which is the work-item order. The two inputs hold the same values in different orders:

```cint reduce_sat_order.ci
kernel total_sat[n](in I8[n] x, out I8 s) over [i: n] {
    reduce s = sum_sat(x[i]);
}

void main() {
    I8[3] p = [120, 120, -120];
    I8[3] q = [120, -120, 120];
    I8 s1 = 0;
    I8 s2 = 0;
    total_sat(p, s1);
    total_sat(q, s2);
    "{s1} {s2}\n";
}
```

```text
7 120
```

In the first order, 120 + 120 saturates to 127, and 127 - 120 is 7.

17. This example shows a contribution under `if`, and reductions with no contributions:

```cint reduce_empty.ci
kernel positive_sum[n](in I64[n] x, out I64 s) over [i: n] {
    if (x[i] > 0) {
        reduce s = sum(x[i]);
    }
}

kernel summary[n](in I64[n] x, out I64 s, out I64 c, out I64 f) over [i: n] {
    reduce s = sum(x[i]);
    reduce c = count(x[i] > 0);
    reduce f = fold_checked(add, 7, x[i]);
}

void main() {
    I64[4] x = [3, -1, 4, -1];
    I64 p = 0;
    positive_sum(x, p);
    I64[0] none;
    I64 s = 5;
    I64 c = 5;
    I64 f = 5;
    summary(none, s, c, f);
    "{p} {s} {c} {f}\n";
}
```

```text
7 0 0 7
```

18. A fixed-point sum adds the stored integers, with no rounding:

```cint reduce_fixed.ci
kernel total_q[n](in Q16.16[n] a, out Q16.16 t) over [i: n] {
    reduce t = sum(a[i]);
}

void main() {
    Q16.16[3] a = [1.5, 3.0, 0.25];
    Q16.16 t = 0.0;
    total_q(a, t);
    "{t}\n";
}
```

```text
4.75
```

19. This example shows a checked fold. The partial sum after work-item 0 is `I64.max`, and work-item 1 adds 1:

```cint kernel_fold.ci
kernel total_checked[n](in I64[n] x, out I64 s) over [i: n] {
    reduce s = fold_checked(add, 0, x[i]);
}

void main() {
    I64[3] x = [9223372036854775807, 1, -1];
    I64 s = 0;
    total_checked(x, s);
    "{s}\n";
}
```

```text
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> kernel_fold.ci:2:16
   |
 2 |     reduce s = fold_checked(add, 0, x[i]);
   |                ^ fold_checked.add.i64
   |
   = operation: fold_checked.add.i64
   = operand-count: 3
   = operand: I64 1
   = operand: I64 9223372036854775807
   = operand: I64 1
   = exact:   9223372036854775808
   = limit:   I64 9223372036854775807
   ...
   = address: kernel_fold.total_checked, dispatch 0, phase 1, work-item 1, step 1
   ...
```

`sum` over the same input gives `I64.max`: the exact sum is in range, so the overflow of the partial sum does not matter.

20. This example shows a sum checked in the epilogue. The first dispatch's exact sum, 2147483647, fits `I32`; the second's, 2147483648, does not:

```cint kernel_epilogue.ci
kernel total[n](in I32[n] x, out I32 s) over [i: n] {
    reduce s = sum(x[i]);
}

void main() {
    I32[3] x = [2147483647, 1, -1];
    I32[2] y = [2147483647, 1];
    I32 s = 0;
    total(x, s);
    "{s}\n";
    total(y, s);
    "{s}\n";
}
```

```text
2147483647
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> kernel_epilogue.ci:2:16
   |
 2 |     reduce s = sum(x[i]);
   |                ^ sum.checked.i32.i32
   |
   = operation: sum.checked.i32.i32
   = operand-count: 1
   = operand: I64 2
   = exact:   2147483648
   = limit:   I32 2147483647
   ...
   = address: kernel_epilogue.total, dispatch 1, phase 2, work-item none, step none
   ...
```

The address names dispatch 1, the second dispatch of the entry and phase 2, the epilogue.

**Notes**

21. The accumulator that `sum` uses, and the bound that makes its exact sum always representable, are in 7.3.

<a id="meaning-of-a-dispatch"></a>

#### 6.13.6 Meaning of a dispatch

The meaning of a dispatch is defined by a sequential procedure: check the arguments, run the work-items one at a time in ascending index, combine the reductions, publish. Every executor gives the outcome of this procedure, including on a GPU, where many work-items run at once and several may fault: the fault reported is always the one the procedure reaches first.

**Semantics**

1. <a id="X-1"></a>A dispatch of `N` work-items means the following procedure [X-1]:
   1. Run the entry checks of 6.13.2, then charge the dispatch's fuel (paragraph 7). If one fails, that fault is the dispatch's fault; stop.
   2. For each work-item index `i` from 0 to `N - 1`, run the body as an independent sequential program, with the over variables set to the index tuple of `i`, fresh local variables, fuel charged to the entry, every read of an `in` or `inout` argument giving its value from before the dispatch, every write to an `out` or `inout` argument going to work-item `i`'s owned block in staging, and each `reduce` statement contributing to its reduction. If a work-item faults, that fault is the dispatch's fault; stop.
   3. Complete the reductions and check their results (6.13.5). If one faults, that fault is the dispatch's fault; stop.
   4. Publish the outputs (6.13.4).
2. <a id="X-2"></a>A work-item reads only values from before the dispatch and its own local state, and writes only its own owned blocks in staging. Contributions to reductions and charges of fuel are combined only by the rules of 6.13.5 and paragraph 7. Every order of running the work-items, including running them at the same time, therefore gives the same outputs and the same fault [X-2].
3. <a id="LS-287"></a>A dispatch has three *phases*: entry (0), work-items (1) and epilogue (2). When more than one fault could occur, the fault of the dispatch is the least by phase, then by work-item index, then by step ordinal. It is the fault the procedure of paragraph 1 reaches first, whichever fault an executor's hardware met first in time [LS-287].
4. <a id="R-8"></a>If the fault of a dispatch is at work-item `k`, every work-item with a lower index completed without a fault [R-8].
5. The record of a fault in a dispatch holds, beyond the fields of every fault record (8.1), the *address* of the fault: the qualified name of the kernel, the dispatch number, the phase, the work-item index and the *step ordinal*, the position of the faulting operation among the counted operations the work-item evaluated (8.1). The work-item index and the step ordinal are absent for a fault in the entry or the epilogue phase. The work-item index is the linear index of 6.13.1, the same on every executor [LS-287].
6. Dispatch numbers count from 0 within each entry (6.13.4). Step ordinals count from 0 within each work-item, in evaluation order. A `reduce` statement counts as one operation where the work-item executes it [LS-288].
7. <a id="X-8"></a>A dispatch of `N` work-items is charged `N` units of fuel when its entry checks pass, before any work-item runs. If that charge would exceed the fuel allowance, the dispatch faults `E_FUEL` in the entry phase and no work-item runs. Inside the body, calls and loop iterations are charged as in sequential code (5.2.4), in logical order: ascending work-item index, then evaluation order within each work-item. The fuel consumed by a dispatch that faults at work-item `k` is `N`, plus every charge of work-items 0 to `k - 1`, plus the charges of work-item `k` made before its fault. A charge that faults `E_FUEL` is not itself counted [X-8].

**Examples**

8. Two work-items overflow, at indices 2 and 4. The fault reported is at work-item 2, whichever an executor reached first:

```cint kernel_lowest.ci
kernel square[n](in I32[n] x, out I32[n] y) over [i: n] {
    y[i] = x[i] * x[i];
}

void main() {
    I32[6] x = [1, 2, 50000, 3, 46341, 4];
    I32[6] y;
    square(x, y);
    "{y[0]}\n";
}
```

```text
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> kernel_lowest.ci:2:17
   |
 2 |     y[i] = x[i] * x[i];
   |                 ^ mul.checked.i32
   |
   = operation: mul.checked.i32
   = operand-count: 2
   = operand: I32 50000
   = operand: I32 50000
   = exact:   2500000000
   = limit:   I32 2147483647
   ...
   = address: kernel_lowest.square, dispatch 0, phase 1, work-item 2, step 3
   = stack-depth: 1
   = called from: kernel_lowest.ci:8:5
   = fuel-consumed: 7 (not part of the fault record)
   ...
```

The step ordinal counts the place `y[i]` as step 0, the two reads of `x[i]` as steps 1 and 2, and the product as step 3. The fuel consumed is 1 for the call of `main` and 6 for the dispatch of six work-items; the body has no loop or call.

**Notes**

9. No output of the faulted dispatch in this example is published: `y` holds what it held before. A host program that runs the entry, or a checkpoint taken after it, observes `y` unchanged (SPEC-03P.2, 8.4).

<a id="arenas-pools-and-handles"></a>

### 6.14 Arenas, pools and handles

An arena and a pool are storage that a program declares once, at module level, with a fixed capacity, and then reuses. An arena hands out runs of elements one after another and takes them all back at once with `reset`. A pool holds single elements in slots, taken with `insert` and given back one at a time with `remove`. The program refers to what it was given through a *handle*. Every use of a handle is checked: a handle whose storage was given back, or that belongs to another arena or pool, faults with `E_STALE_HANDLE` instead of reading other data. Storage is returned only by `remove` and `reset`, never by a free of an address, and no handle is a raw address.

The type forms are in 6.3.8. This section states the declarations (6.14.1), arenas (6.14.2), pools (6.14.3), handles and the stale-handle fault (6.14.4), views of arena storage (6.14.5) and how the frame arena that holds local arrays relates to them (6.14.6).

<a id="memory-declarations"></a>

#### 6.14.1 Declarations

An arena or a pool is declared at the top level of a module with its element type and its capacity. Functions reach it by its name, or through a parameter of type `Arena(T)` or `Pool(T)`. Handles are ordinary values that can be copied and stored.

**Syntax**

```ebnf
memory_decl = ( "Arena" | "Pool" ) "(" memory_elem "," expr ")" IDENT ";" ;
memory_type = ( "Arena" | "Pool" ) "(" memory_elem ")"
            | "Handle" "(" memory_elem [ "[" [ "_" ] "]" ] ")" ;
memory_elem = INT_TYPE | "Bool" | qualified ;         (* an integer, Bool or struct type *)
```

`memory_decl` is a top-level declaration (6.9.3). `memory_type` is a type (6.3.8).

**Constraints**

1. <a id="M-20"></a>A memory declaration appears at the top level of a module. Inside a function, a test or a block it is diagnosed C4012 at its first token [M-20].
2. The capacity is a constant expression of type `I64` (6.9.3) whose value is at least 0. A negative capacity is diagnosed C6006 at the capacity [M-20].
3. The element type `T` of an arena or a pool is an integer type, `Bool` or a struct type [M-19].
4. <a id="M-20b"></a>A value of type `Arena(T)` or `Pool(T)` is a parameter, or a local variable with an initializer; such a local without an initializer is diagnosed C9102 at its name. It is not stored in a module-level variable, a struct field, an array, an arena or a pool, and it is not returned: each of these is diagnosed C2051 at its type. It is not assigned after its declaration: C2058 at the target of the assignment [M-20b].
5. The operations that change an arena or a pool, `alloc`, `alloc_result`, `child`, `reset`, `insert`, `insert_result` and `remove`, are applied to a module-level name, a local or an `inout` parameter. Applied to an `in` parameter they are diagnosed C2067 at the receiver, and an assignment through a dereference of an `in` parameter, such as `a[h][0] = 0`, is diagnosed C2060 at its target [M-20b].
6. <a id="M-20a"></a>A handle has the type that the operation creating it gives: `alloc` gives `Handle(T[])` and `insert` gives `Handle(T)`, where `T` is the element type. A handle of another type where one is required, such as a `Handle(I64[])` given to a pool's `remove`, is diagnosed C2001 at it [M-20a].
7. Handles, arenas and pools are not compared: `==` and the other comparisons on them are diagnosed C2103 at the left operand [M-20a].

**Semantics**

8. Every arena and pool of a program is created, empty, with the module-level variables (6.9.3), with the element type and the capacity its declaration gives. No arena or pool grows: its storage is fixed by the declarations [M-20, M-19].
9. In a script, a memory declaration among the top-level statements is still a module-level declaration. The arena or pool exists before the first statement runs and is visible throughout the module, above its declaration as well as below. It is not a local of the script's entry [M-20].
10. <a id="M-13"></a>Arenas are numbered from 1, and pools are numbered from 1, by separate counters. Module-level arenas and pools are numbered in the order the modules are loaded, the root module first and then the modules it imports, breadth first in import order and within a module in declaration order. A child arena (6.14.2) takes the next arena number when `child` runs. A number is not reused. The number is the *container* recorded in each handle the arena or pool gives [M-13].
11. A copy of an `Arena(T)` or `Pool(T)` value names the same arena or pool. An `inout Arena(T)` parameter to which a module-level arena is passed allocates from that arena [M-20b].
12. A handle is an ordinary value. It is copied by assignment, passed in any mode, returned and stored in variables, struct fields and arrays. A `Handle` variable declared without an initializer, and a `Handle` module-level variable or struct field that is given no value, is the *null handle* [M-20a].
13. The operations of 6.14.2 and 6.14.3 and the checks of handles charge no fuel (5.2.4) [M-20a].

**Examples**

14. This example shows a script whose arena is declared after the statements that use it:

```cint script_arena.ci
Handle(I64[]) h = scratch.alloc(4);
for i in 0..4 {
    scratch[h][i] = i * i;
}
"{sum(scratch[h])}\n";

Arena(I64, 8) scratch;
```

```text
14
```

15. This example shows an arena declared inside a function:

```cint arena_in_function.ci
void main() {
    Arena(I64, 8) scratch;
}
```

```text
error[C4012]: statement form not permitted
  --> arena_in_function.ci:2:5
   |
 2 |     Arena(I64, 8) scratch;
   |     ^
```

16. This example shows a reset through an `in` parameter:

```cint arena_in_param.ci
Arena(I64, 16) scratch;

void clear(in Arena(I64) a) {
    a.reset();
}

void main() {
    clear(scratch);
}
```

```text
error[C2067]: parameter or argument rule
  --> arena_in_param.ci:4:5
   |
 4 |     a.reset();
   |     ^
```

17. Handles kept in a module-level variable and in struct fields. The struct is declared in a program with `void main()` (6.9.6):

```cint handle_fields.ci
struct Body {
    I64 mass;
    I64 vel;
}

struct Link {
    Handle(Body) from;
    Handle(Body) to;
}

const I64 BODIES = 4;

Pool(Body, BODIES) bodies;
Handle(Body) sun;

void main() {
    sun = bodies.insert(Body(mass = 100, vel = 0));
    Link l;
    l.from = sun;
    l.to = bodies.insert(Body(mass = 3, vel = 2));
    bodies[l.to].vel += 1;
    "{bodies[l.from].mass} {bodies[l.to].vel}\n";
}
```

```text
100 3
```

18. This example shows an array of handles:

```cint handle_array.ci
Pool(I64, 4) counts;

void main() {
    Handle(I64)[3] hs;
    hs[0] = counts.insert(5);
    "{counts[hs[0]]}\n";
}
```

```text
5
```

19. An arena of an unsigned wide type, and a local that names a module-level arena and a pool:

```cint arena_u128.ci
Arena(U128, 2) ids;

void main() {
    Handle(U128[]) k = ids.alloc(1);
    "{ids[k][0]}\n";
}
```

```text
0
```

```cint arena_local_name.ci
Arena(I64, 8) scratch;
Pool(I64, 2) counts;

void main() {
    Arena(I64) s = scratch;
    Pool(I64) p = counts;
    Handle(I64[]) h = s.alloc(2);
    scratch[h][1] = 4;
    Handle(I64) k = p.insert(3);
    "{s[h][1]} {counts[k]}\n";
}
```

```text
4 3
```

**Notes**

20. An arena holds one element type. Data of several types that is allocated together goes in a struct element, as in 6.14.2, or in several arenas.

<a id="arenas"></a>

#### 6.14.2 Arenas

An arena hands out runs of consecutive elements, each run after the last, and takes them all back at once with `reset`. A run is reached through its handle, as a view of rank 1. A *child* arena is carved out of a parent for allocations that end sooner than the parent's.

**Syntax**

```ebnf
call = IDENT [ "." IDENT ] "(" [ args ] ")" ;
```

An arena operation is a `call` `a.alloc(n)`, `a.alloc_result(n)`, `a.child(n)` or `a.reset()`, where the name `a` denotes an arena (6.7.4). The dereference `a[h]` is an index expression (6.12.2) whose index is a handle.

**Constraints**

1. <a id="M-19"></a>The argument of `alloc`, `alloc_result` and `child` has type `I64`; another type is diagnosed C2001 [M-19].

**Semantics**

2. <a id="M-14"></a>An arena of capacity `c` has a *used extent* `u` and a *generation* `g`, both 0 when it is created. Its storage is `c` elements, numbered from 0 [M-14, M-19].
3. `a.alloc(n)`, for `0 <= n` and `u + n <= c`, gives the run of elements `u` to `u + n - 1`, sets each of them to 0 (every field to 0 for a struct element), and increases `u` by `n`. It returns a handle with slot `u` (the used extent before the allocation), length `n`, generation `g`, the arena's number and kind 1 (6.14.4). `alloc(0)` gives a handle of length 0. An allocation does not move an earlier one [M-14, M-19].
4. `a.alloc_result(n)` is the result form (3.8) of `alloc`, of type `AllocError!Handle(T[])`. Where `u + n` exceeds `c` for a count `n` of at least 0, it changes nothing and returns `AllocError.full`, the one value of the built-in error set `AllocError` (6.3.7), whose tag is `U16 1`. A negative count faults as for `alloc` [M-19].
5. <a id="M-14a"></a>`a[h]`, for a handle `h` that is valid in `a` (6.14.4), is the run of `h` as a view of rank 1 whose extent is the handle's length. It has write permission, except where `a` is an `in` parameter, where it has read permission. `a[h][i]` is an element of the run: its index is checked against the run's extent (6.12.2), so no index reaches a neighboring run [M-14a, M-20a].
6. `a.reset()` increases `g` by 1 and sets `u` to 0. Every handle given before the reset is stale from then on (6.14.4), and the next allocation starts at element 0 [M-14].
7. `a.child(c)` charges `c` elements to `a` as `a.alloc(c)` does, and faults as it does. It returns a new arena of type `Arena(T)`: its elements are the charged elements of `a`, its element type is that of `a`, its capacity is `c`, its used extent and its generation are 0, and it takes the next arena number (6.14.1). `child` has no result form [M-20, M-13].
8. Resetting a child does not change its parent. Resetting an arena *retires* each of its descendants: children, their children and so on. A retired arena gives no allocation and cannot be reset (paragraph 10), and every handle into it is stale. Its elements return to the parent's free storage at the parent's reset [M-20].

**Faults**

9. The fault `E_BOUNDS` is raised by `alloc`, `alloc_result` or `child` with a count below 0, and by `alloc` or `child` with a count `n` for which `u + n` exceeds `c`. The record holds the operation identifier `arena.alloc`; three `I64` operands, the count requested, the used extent `u` and the capacity `c`; and no exact result and no limit. Its position is the member name, `alloc` or `child` [M-19, M-17a].
10. The fault `E_STALE_HANDLE` is raised by `alloc`, `alloc_result` or `child` on a retired arena, with the operation identifier `arena.alloc`, and by `reset` on a retired arena, with the operation identifier `arena.reset`. The record is that of 6.14.4, paragraph 7, with the reason `retired`, and its handle operands are the arena's own fields: slot 0, the capacity as the length, the arena's generation, its number and kind 1. Its position is the member name [M-17a, M-20].

**Examples**

11. This example shows runs of 3, 2 and 0 elements. Elements not assigned are 0:

```cint arena_alloc.ci
Arena(I64, 16) scratch;

void main() {
    Handle(I64[]) a = scratch.alloc(3);
    Handle(I64[]) b = scratch.alloc(2);
    Handle(I64[]) e = scratch.alloc(0);
    scratch[a][0] = 4;
    scratch[a][2] = 6;
    scratch[b][1] = 9;
    "{len(scratch[a])} {len(scratch[e])} {sum(scratch[a])} {scratch[a][1]} {scratch[b][0]} {scratch[b][1]}\n";
}
```

```text
3 0 10 0 0 9
```

12. Each call takes the whole arena and resets it before it returns, so the arena does not fill:

```cint arena_reset.ci
Arena(I64, 4) scratch;

I64 step(I64 k) {
    Handle(I64[]) h = scratch.alloc(4);
    for i in 0..4 {
        scratch[h][i] = k + i;
    }
    I64 t = sum(scratch[h]);
    scratch.reset();
    return t;
}

void main() {
    I64 a = step(0);
    I64 b = step(10);
    I64 c = step(100);
    "{a} {b} {c}\n";
}
```

```text
6 46 406
```

13. The result form reports a full arena as an error, which the caller handles with `catch` (6.11). The second call needs 4 elements where 2 are left:

```cint arena_alloc_result.ci
Arena(I64, 6) scratch;

AllocError!I64 ones(I64 n) {
    Handle(I64[]) h = try scratch.alloc_result(n);
    for i in 0..n {
        scratch[h][i] = 1;
    }
    return sum(scratch[h]);
}

void main() {
    I64 a = ones(4) catch -1;
    I64 b = ones(4) catch -1;
    I64 c = ones(2) catch -1;
    "{a} {b} {c}\n";
}
```

```text
4 -1 2
```

14. The faulting form stops the program, and the record gives the numbers:

```cint arena_full.ci
Arena(I64, 8) scratch;

void main() {
    Handle(I64[]) a = scratch.alloc(5);
    Handle(I64[]) b = scratch.alloc(4);
}
```

```text
fault[E_BOUNDS]: index out of bounds
  --> arena_full.ci:5:31
   |
 5 |     Handle(I64[]) b = scratch.alloc(4);
   |                               ^ arena.alloc
   |
   = operation: arena.alloc
   = operand-count: 3
   = operand: I64 4
   = operand: I64 5
   = operand: I64 8
   = exact:   none
   = limit:   none
   ...
```

15. A child arena of 3 elements is charged to its parent between `a` and `b`. Resetting the child gives its elements back to the child, not to the parent, so the parent is full after `c`:

```cint arena_child.ci
Arena(I64, 8) scratch;

void main() {
    Handle(I64[]) a = scratch.alloc(2);
    Arena(I64) part = scratch.child(3);
    Handle(I64[]) b = scratch.alloc(1);
    Handle(I64[]) x = part.alloc(3);
    part[x][2] = 9;
    part.reset();
    Handle(I64[]) y = part.alloc(2);
    "{part[y][0]} {len(part[y])}\n";
    Handle(I64[]) c = scratch.alloc(2);
    "{len(scratch[c])}\n";
    Handle(I64[]) d = scratch.alloc(1);
}
```

```text
0 2
2
fault[E_BOUNDS]: index out of bounds
  --> arena_child.ci:14:31
   |
14 |     Handle(I64[]) d = scratch.alloc(1);
   |                               ^ arena.alloc
   |
   = operation: arena.alloc
   = operand-count: 3
   = operand: I64 1
   = operand: I64 8
   = operand: I64 8
   ...
```

16. This example shows an arena of struct elements. Its capacity and its used extent count elements, not bytes:

```cint arena_struct.ci
struct Body {
    I64 mass;
    I64 vel;
}

Arena(Body, 3) scratch;

void main() {
    Handle(Body[]) two = scratch.alloc(2);
    Handle(Body[]) one = scratch.alloc(1);
    scratch[two][1].mass = 11;
    scratch[one][0] = Body(mass = 2, vel = 4);
    "{scratch[two][1].mass} {scratch[one][0].vel} {scratch[two][0].mass}\n";
}
```

```text
11 4 0
```

17. This example shows an allocation from a retired arena. The handle operands are the child's own fields: slot 0, its capacity 4, generation 0, arena number 2 and kind 1; the reason is 2, `retired`:

```cint arena_retired.ci
Arena(I64, 8) scratch;

void main() {
    Arena(I64) part = scratch.child(4);
    scratch.reset();
    Handle(I64[]) u = part.alloc(1);
}
```

```text
fault[E_STALE_HANDLE]: stale handle
  --> arena_retired.ci:6:28
   |
 6 |     Handle(I64[]) u = part.alloc(1);
   |                            ^ arena.alloc
   |
   = operation: arena.alloc
   = operand-count: 7
   = operand: U64 0
   = operand: I64 4
   = operand: U64 0
   = operand: U32 2
   = operand: U8 1
   = operand: U64 0
   = operand: U8 2
   ...
```

**Notes**

18. An arena has no partial reset. Allocations that end sooner than the rest are taken from a child arena, and the child is reset.
19. At one reset every microsecond, the generation of paragraph 6 reaches 2^64 - 1 after about 584,500 years.

<a id="pools"></a>

#### 6.14.3 Pools

A pool holds up to its capacity of single elements, each in a *slot*. `insert` stores a value in a free slot and returns a handle to it; `remove` frees the slot. A freed slot is used again, and from then on the handle to its old contents is stale.

**Syntax**

```ebnf
call = IDENT [ "." IDENT ] "(" [ args ] ")" ;
```

A pool operation is a `call` `p.insert(v)`, `p.insert_result(v)` or `p.remove(h)`, where the name `p` denotes a pool (6.7.4). The dereference `p[h]` is an index expression (6.12.2) whose index is a handle.

**Constraints**

1. The argument of `insert` and `insert_result` has the element type `T` of the pool, and that of `remove` has type `Handle(T)`. Another type is diagnosed C2001 [M-20a].

**Semantics**

2. <a id="M-15"></a>A pool of capacity `c` has the slots 0 to `c - 1`. Each slot is *vacant*, *occupied* or *retired*, and has a generation, a `U64` that starts at 0. In a new pool every slot is vacant [M-15].
3. <a id="M-18"></a>`p.insert(v)` stores `v` in a vacant slot, marks the slot occupied and returns a handle with the slot's index, length 1, the slot's generation, the pool's number and kind 2 (6.14.4). The slot is the one most recently vacated by `remove` and not taken since; when there is none, it is the vacant slot of lowest index. The handles a program obtains are therefore fixed by its sequence of operations [M-18, M-12].
4. `p.insert_result(v)` is the result form (3.8) of `insert`, of type `AllocError!Handle(T)`. When the pool is full it changes nothing and returns `AllocError.full` (6.14.2) [M-19].
5. A pool is *full* when every slot is occupied or retired [M-19].
6. `p[h]`, for a handle `h` that is valid in `p` (6.14.4), is the slot's element as a place. It can be read, assigned whole, or have a field read or assigned, as in `bodies[h].mass += 5` [M-20a].
7. <a id="M-17a"></a>`p.remove(h)` checks `h` as `p[h]` does. It then marks the slot vacant and increases its generation by 1, so `h` and every copy of it are stale from then on [M-15, M-17a].
8. <a id="M-16"></a>A generation does not wrap. A `remove` that would set a slot's generation to 2^64 - 1 marks the slot retired instead, and no `insert` takes a retired slot [M-16].

**Faults**

9. The fault `E_BOUNDS` is raised by `insert` into a full pool. The record holds the operation identifier `pool.insert`; three `I64` operands, the number of occupied slots, the number of retired slots and the capacity; and no exact result and no limit. Its position is the member name `insert` [M-19, M-17a].
10. The fault `E_STALE_HANDLE` is raised by `p[h]` and by `remove` with a handle that is not valid in `p`, with the operation identifier `pool.deref` (6.14.4) [M-17a].

**Examples**

11. This example shows two bodies in a pool. A slot is assigned whole, a field is updated in place, and a copy of an element outlives the slot it came from:

```cint pool_basics.ci
struct Body {
    I64 mass;
    I64 vel;
}

Pool(Body, 4) bodies;

void main() {
    Handle(Body) a = bodies.insert(Body(mass = 7, vel = 1));
    Handle(Body) b = bodies.insert(Body(mass = 5, vel = 2));
    bodies[a].vel += bodies[b].vel;
    bodies[b] = Body(mass = 6, vel = 0);
    Body saved = bodies[a];
    bodies.remove(a);
    "{saved.mass} {saved.vel} {bodies[b].mass}\n";
}
```

```text
7 3 6
```

12. The most recently freed slot is used first. `x` takes slot 2, freed last, and `y` takes slot 0. After `x` is removed, the record shows its slot 2 at generation 1, where the slot is now at generation 2:

```cint pool_order.ci
Pool(I64, 3) counts;

void main() {
    Handle(I64) a = counts.insert(1);
    Handle(I64) b = counts.insert(2);
    Handle(I64) c = counts.insert(3);
    counts.remove(a);
    counts.remove(c);
    Handle(I64) x = counts.insert(4);
    Handle(I64) y = counts.insert(5);
    "{counts[b]} {counts[x]} {counts[y]}\n";
    counts.remove(x);
    "{counts[x]}\n";
}
```

```text
2 4 5
fault[E_STALE_HANDLE]: stale handle
  --> pool_order.ci:13:13
   |
13 |     "{counts[x]}\n";
   |             ^ pool.deref
   |
   = operation: pool.deref
   = operand-count: 7
   = operand: U64 2
   = operand: I64 1
   = operand: U64 1
   = operand: U32 1
   = operand: U8 2
   = operand: U64 2
   = operand: U8 3
   ...
```

13. This example shows a full pool. The slot freed by `remove` is used again, and the fourth `insert` finds 2 occupied slots, none retired, in a capacity of 2:

```cint pool_full.ci
Pool(I64, 2) counts;

void main() {
    Handle(I64) a = counts.insert(10);
    Handle(I64) b = counts.insert(20);
    counts.remove(a);
    Handle(I64) c = counts.insert(30);
    Handle(I64) d = counts.insert(40);
}
```

```text
fault[E_BOUNDS]: index out of bounds
  --> pool_full.ci:8:28
   |
 8 |     Handle(I64) d = counts.insert(40);
   |                            ^ pool.insert
   |
   = operation: pool.insert
   = operand-count: 3
   = operand: I64 2
   = operand: I64 0
   = operand: I64 2
   ...
```

14. This example shows the result form:

```cint pool_insert_result.ci
Pool(I64, 2) counts;

AllocError!I64 keep(I64 v) {
    Handle(I64) h = try counts.insert_result(v);
    return counts[h];
}

void main() {
    I64 a = keep(10) catch -1;
    I64 b = keep(20) catch -1;
    I64 c = keep(30) catch -1;
    "{a} {b} {c}\n";
}
```

```text
10 20 -1
```

**Notes**

15. A pool of capacity 0 is valid and is always full.

<a id="handles-and-stale-handles"></a>

#### 6.14.4 Handles and stale handles

A handle records where its element or run is, and the generation under which it was given. Each use compares it with the arena or pool it is used in. A handle that no longer matches faults with `E_STALE_HANDLE`, and the record says which check failed.

**Semantics**

1. <a id="M-12"></a>A handle has five fields. Its canonical form is 32 bytes, little-endian, with the fields at these offsets [M-12]:

| Offset | Field | Type | Arena handle | Pool handle |
| --- | --- | --- | --- | --- |
| 0 | slot | `U64` | the first element of the run | the slot index |
| 8 | length | `I64` | the number of elements of the run | 1 |
| 16 | generation | `U64` | the arena's generation at the allocation | the slot's generation at the insertion |
| 24 | container | `U32` | the arena's number | the pool's number |
| 28 | kind | `U8` | 1 | 2 |
| 29 | reserved | 3 bytes | 0 | 0 |

2. The *null handle* has every field 0, and it is the only handle of kind 0. A decoder of the canonical form rejects 32 bytes whose reserved bytes are not 0 or whose kind is not 0, 1 or 2 [M-12].
3. A handle `h` is *valid* in an arena `a` when all of these hold: `a`, and every arena that `a` was carved from, is not retired; `h` has `a`'s number as its container and kind 1; `h`'s generation equals `a`'s generation; `h`'s length is at least 0; and `h`'s slot plus its length, computed without overflow, is at most `a`'s used extent [M-14].
4. <a id="M-17"></a>A handle `h` is valid in a pool `p` when `h` has `p`'s number as its container and kind 2, its slot is below `p`'s capacity, that slot is occupied, and the slot's generation equals `h`'s generation [M-15, M-17].
5. Each dereference, `a[h]` or `p[h]` and each `remove`, checks its handle before anything else, and a handle that is not valid faults. Copying, storing, passing and returning a handle does not check it [M-17].
6. When a handle is not valid for more than one reason, the first reason in this order is reported [M-17]:

| Order | Reason | Code | Condition |
| --- | --- | --- | --- |
| 1 | `null` | 1 | the handle is the null handle |
| 2 | `container` | 6 | the handle's container or kind is not that of the arena or pool it is used in |
| 3 | `retired` | 2 | the arena, an arena it was carved from, or the pool slot is retired |
| 4 | `generation` | 3 | the handle's generation differs from the current generation |
| 5 | `extent` | 4 | in an arena, slot plus length exceeds the used extent; in a pool, the slot is at or above the capacity |
| 6 | `vacant` | 5 | in a pool, the slot is not occupied |

**Faults**

7. The fault `E_STALE_HANDLE` is raised by a dereference or a `remove` whose handle is not valid. The record holds the operation identifier `handle.check` for an arena and `pool.deref` for a pool. It has seven operands: the handle's slot (`U64`), length (`I64`), generation (`U64`), container (`U32`) and kind (`U8`); the current generation as a `U64`, which is the arena's generation, or the generation of the pool's slot when the handle's slot is below the capacity and 0 otherwise; and the reason's code as a `U8`. It has no exact result and no limit. Its position is the `[` of the dereference, or the member name `remove` [M-17a].

**Examples**

8. This example shows a handle used after its slot was freed and filled again. The new element is at slot 0 with generation 1; the old handle has generation 0, so the reason is 3, `generation`:

```cint stale_pool.ci
struct Body {
    I64 mass;
    I64 vel;
}

Pool(Body, 4) bodies;

void main() {
    Handle(Body) h = bodies.insert(Body(mass = 7, vel = 1));
    bodies.remove(h);
    Handle(Body) k = bodies.insert(Body(mass = 9, vel = 0));
    "{bodies[k].mass}\n";
    "{bodies[h].mass}\n";
}
```

```text
9
fault[E_STALE_HANDLE]: stale handle
  --> stale_pool.ci:13:13
   |
13 |     "{bodies[h].mass}\n";
   |             ^ pool.deref
   |
   = operation: pool.deref
   = operand-count: 7
   = operand: U64 0
   = operand: I64 1
   = operand: U64 0
   = operand: U32 1
   = operand: U8 2
   = operand: U64 1
   = operand: U8 3
   ...
```

9. A handle declared without an initializer is the null handle, reason 1:

```cint stale_null.ci
Pool(I64, 2) counts;

void main() {
    Handle(I64) h;
    "{counts[h]}\n";
}
```

```text
fault[E_STALE_HANDLE]: stale handle
  --> stale_null.ci:5:13
   |
 5 |     "{counts[h]}\n";
   |             ^ pool.deref
   |
   = operation: pool.deref
   = operand-count: 7
   = operand: U64 0
   = operand: I64 0
   = operand: U64 0
   = operand: U32 0
   = operand: U8 0
   = operand: U64 0
   = operand: U8 1
   ...
```

10. This example shows a handle from pool 2 used in pool 1. Its slot and generation match a live slot of `red`, and the dereference still reads nothing: the reason is 6, `container`:

```cint stale_container.ci
Pool(I64, 2) red;
Pool(I64, 2) blue;

void main() {
    Handle(I64) r = red.insert(1);
    Handle(I64) b = blue.insert(2);
    "{red[b]}\n";
}
```

```text
fault[E_STALE_HANDLE]: stale handle
  --> stale_container.ci:7:10
   |
 7 |     "{red[b]}\n";
   |          ^ pool.deref
   |
   = operation: pool.deref
   = operand-count: 7
   = operand: U64 0
   = operand: I64 1
   = operand: U64 0
   = operand: U32 2
   = operand: U8 2
   = operand: U64 0
   = operand: U8 6
   ...
```

11. This example shows a handle into an arena after a reset. The new run starts at element 0 again and reads 0; the old handle has generation 0 where the arena has generation 1:

```cint stale_arena.ci
Arena(I64, 8) scratch;

void main() {
    Handle(I64[]) tmp = scratch.alloc(4);
    scratch[tmp][0] = 3;
    scratch.reset();
    Handle(I64[]) fresh = scratch.alloc(4);
    "{scratch[fresh][0]}\n";
    "{scratch[tmp][0]}\n";
}
```

```text
0
fault[E_STALE_HANDLE]: stale handle
  --> stale_arena.ci:9:14
   |
 9 |     "{scratch[tmp][0]}\n";
   |              ^ handle.check
   |
   = operation: handle.check
   = operand-count: 7
   = operand: U64 0
   = operand: I64 4
   = operand: U64 0
   = operand: U32 1
   = operand: U8 1
   = operand: U64 1
   = operand: U8 3
   ...
```

12. This example shows a handle into a child arena after the parent's reset. `first` and `scratch` are arenas 1 and 2, so the child is arena 3; the reason is 2, `retired`:

```cint stale_retired.ci
Arena(I64, 8) first;
Arena(I64, 8) scratch;

void main() {
    Arena(I64) part = scratch.child(4);
    Handle(I64[]) t = part.alloc(2);
    part[t][0] = 5;
    scratch.reset();
    "{part[t][0]}\n";
}
```

```text
fault[E_STALE_HANDLE]: stale handle
  --> stale_retired.ci:9:11
   |
 9 |     "{part[t][0]}\n";
   |           ^ handle.check
   |
   = operation: handle.check
   = operand-count: 7
   = operand: U64 0
   = operand: I64 2
   = operand: U64 0
   = operand: U32 3
   = operand: U8 1
   = operand: U64 0
   = operand: U8 2
   ...
```

**Notes**

13. A child arena belongs to the block that declares the local naming it. A handle into the child that is kept after the block can be used only through another arena's name, and that use faults with the reason `container` [M-20b].

<a id="views-of-arena-storage"></a>

#### 6.14.5 Views of arena storage

`a[h]` is a view (6.12.4) of rank 1 over the run of `h`. Such a view remembers the handle it came from, and every access through it checks that handle again, so a reset between the view's creation and its use is caught at the use.

**Semantics**

1. <a id="M-23"></a>A view obtained from `a[h]` records the handle `h`. Each element access through it checks `h` as a dereference does (6.14.4), then checks the index (6.12.2) [M-23].
2. The check is made whatever runs between the view's creation and its use: a reset in a called function, through a module-level name or through two parameters that name the same arena [M-23].
3. A view obtained from `a[h]` can initialize a view variable (6.9.1), be indexed, be measured with `len`, be reduced with `sum`, be sliced and be passed as a view argument. A slice of it records the same handle [M-23, M-14a].

**Faults**

4. The fault `E_STALE_HANDLE` is raised by an element access through a view whose handle is not valid. The record is that of 6.14.4, paragraph 7, with the operation identifier `handle.check` and the operands of the handle the view was obtained from. Its position is the `[` of the element access [M-23, M-17a].

**Examples**

5. This example shows a view variable over a run:

```cint arena_view.ci
Arena(I64, 16) scratch;

void main() {
    Handle(I64[]) h = scratch.alloc(3);
    scratch[h][2] = 5;
    in I64[_] v = scratch[h];
    "{sum(v)} {len(v)} {v[2]}\n";
}
```

```text
5 3 5
```

6. This example shows a reset in a called function, between the view's creation and its use:

```cint arena_view_stale.ci
Arena(I64, 64) scratch;

void clear() {
    scratch.reset();
}

I64 total_first(Handle(I64[]) h) {
    inout I64[_] v = scratch[h];
    I64 t = sum(v);
    clear();
    I64 x = v[0];
    return t + x;
}

void main() {
    Handle(I64[]) h = scratch.alloc(3);
    scratch[h][0] = 2;
    "{total_first(h)}\n";
}
```

```text
fault[E_STALE_HANDLE]: stale handle
  --> arena_view_stale.ci:11:14
   |
11 |     I64 x = v[0];
   |              ^ handle.check
   |
   = operation: handle.check
   = operand-count: 7
   = operand: U64 0
   = operand: I64 3
   = operand: U64 0
   = operand: U32 1
   = operand: U8 1
   = operand: U64 1
   = operand: U8 3
   ...
```

7. This example shows a run passed to a function's view parameter, and a slice of a run:

```cint arena_view_arg.ci
Arena(I64, 16) scratch;

I64 total(in I64[_] v) {
    return sum(v);
}

void main() {
    Handle(I64[]) h = scratch.alloc(3);
    scratch[h][2] = 5;
    "{total(scratch[h])}\n";
}
```

```text
5
```

```cint arena_view_slice.ci
Arena(I64, 16) scratch;

void main() {
    Handle(I64[]) h = scratch.alloc(4);
    scratch[h][3] = 7;
    in I64[_] tail = scratch[h][2..4];
    "{len(tail)} {tail[1]}\n";
}
```

```text
2 7
```

**Notes**

8. A pool element is reached as the place `p[h]` (6.14.3), not through a view.

<a id="frame-arena-and-arenas"></a>

#### 6.14.6 The frame arena and arenas

Local arrays and arenas are two kinds of storage for runs of elements. A local array lives in the frame arena (5.2.6) and ends with its block. A run taken from an arena lives until the arena is reset, and is reached through a handle that can be returned and stored.

**Semantics**

1. The frame arena holds the local arrays of the running entry (5.2.6). No handle refers to its storage and no arena operation names it: a local array is released when its block exits, and not by `reset`.
2. When the frame arena is exhausted, the fault is `E_BOUNDS` with the operation identifier `arena.alloc` and the three operands of 6.14.2, paragraph 9, at the declared name of the array (5.2.6).
3. Storage that is referenced after the call that fills it returns, or across entries, is taken from an arena or a pool, and the program keeps a handle to it. A view of it is formed where it is used [M-22].

**Examples**

4. A function that fills a run and returns its handle, and a function that reads the run through an `in` parameter:

```cint arena_return.ci
Arena(I64, 16) scratch;

Handle(I64[]) count_up(inout Arena(I64) a, I64 n) {
    Handle(I64[]) h = a.alloc(n);
    for i in 0..n {
        a[h][i] = i + 1;
    }
    return h;
}

I64 last(in Arena(I64) a, Handle(I64[]) h) {
    return a[h][len(a[h]) - 1];
}

void main() {
    Handle(I64[]) h = count_up(scratch, 4);
    "{sum(scratch[h])} {last(scratch, h)}\n";
}
```

```text
10 4
```

<a id="modules-programs-and-tests"></a>

### 6.15 Modules, programs and tests

A program is made of modules. This clause gives how one module uses another, what a module makes visible to the modules that import it, and how a module states tests that `cint test` runs. Scripts, programs with `main` and importable modules are defined in 5.1.2.

<a id="modules-and-imports"></a>

#### 6.15.1 Modules and imports

Each source file is one module (5.1.1), and its path names it. A module uses another by importing it, and then reaches the other module's exported names through a qualifier. A module is translated from its own source and the exported declarations of the modules it imports: there are no header files, prototypes or include guards.

**Syntax**

```ebnf
import_decl = "import" module_path
              [ "as" IDENT | "." "{" IDENT { "," IDENT } [ "," ] "}" ] ";" ;
module_path = IDENT { "." IDENT } ;
top_item    = [ "export" ] declaration
            | test_decl
            | static_assert
            | statement ;
declaration = const_decl | var_decl | func_decl | kernel_decl
            | struct_decl | error_decl | memory_decl
            | extern_decl | requires_decl ;
```

`module` is in 5.1.1, the declarations in 6.9, 6.13.1 and 6.14.1 and `test_decl` in 6.15.3. `export` does not precede a `requires_decl`. A `statement` among the top-level items makes the module a script (5.1.2).

**Constraints**

1. <a id="LS-225"></a>A module's path relative to the source root (paragraph 11) names it: each `/` becomes `.` and the final `.ci` is removed, so the file `geo/vec.ci` is the module `geo.vec`. Each segment of the path, the last without `.ci`, shall be an identifier (6.4.4) and shall not be one of the device names `con`, `prn`, `aux`, `nul`, `com1` to `com9` and `lpt1` to `lpt9`, in any mix of upper and lower case. Two module paths of one program shall not be equal when ASCII letters are compared without regard to case. A path that breaks one of these rules is diagnosed C3030 at the start of that file, position `<path>:1:1` [LS-225].
2. <a id="LS-226"></a>Every `import` declaration shall come before every other item of the module except a profile declaration (5.1.1). An import after another item is diagnosed C3012 at its `import` [LS-226].
3. The file of each imported module shall be readable under the source root, or the module shall be one of the standard library (paragraph 12). An import whose module file cannot be read is diagnosed C3009 at its module path [LS-225].
4. <a id="LS-230"></a>A module shall not import itself, directly or through other modules. The import that closes a cycle is diagnosed C3003 at its module path [LS-230].
5. <a id="LS-228"></a>Each name in a module is declared once, whatever it names: a type, a function, a constant, a variable, an error set, a kernel, an arena, a pool or an import. A second declaration of a name is diagnosed C3001 at the second declared name. There is no overloading: two functions with the same name and different parameters are two declarations of one name [LS-228].
6. An import declares one name in the importing module: the last segment of the module path, or the identifier after `as`. A second import that declares the same name, or another declaration of it, is diagnosed C3001, as paragraph 5 states [LS-228].
7. A selected import, `import a.b.{X, y};`, declares each listed name in the importing module, unqualified. A listed name that is also declared in the importing module, or listed by another selected import, is diagnosed C3002 [LS-228].
8. Imports are not transitive. When `a` imports `b` and `b` imports `c`, the names of `c` are not visible in `a` unless `a` imports `c` too. A qualified name whose qualifier names no import of the module is diagnosed C3005, or C3006 where a type is required, at the qualifier [LS-226].
9. Only the exported names of an imported module are visible to the importer (6.15.2). A qualified name that names a private declaration of the imported module is diagnosed C3008 at the name after the qualifier [LS-227].
10. A script cannot be imported, as 5.1.2 states (C3010).

**Semantics**

11. The *source root* is the directory that module paths are relative to. For `cint run FILE` and `cint test FILE` it is the directory of `FILE`, unless the command names another with `--root` (SPEC-06P.2) [LS-225].
12. The *standard library* is a set of modules that every program can import without a file of its own: in this edition `fs` and `console`, the modules of the standard services (6.9.9). An import of a path of one segment that names no module file under the source root reads that module of the standard library. A module file of the same path under the source root is used instead [LS-225].
13. `import a.b;` makes each exported name `n` of the module `a.b` available as `b.n`. `import a.b as v;` makes it available as `v.n`, and `b` then names nothing. `import a.b.{X, y};` makes the listed names available unqualified, and only those names [LS-228].
14. <a id="LS-229"></a>A module is translated from its own source and the exported declarations of the modules it imports [LS-229].
15. Each module-level variable is one object of the context (6.9.3), however many modules import its module. An importer reads an exported variable as `m.name` and may assign it, and every module that names the variable reaches the same object [LS-227].

**Examples**

16. This example shows a module and a program that imports it. The program's directory is the source root, and the module `geo.vec` is the file `geo/vec.ci` below it. It exports a struct, a constant and two functions and keeps `helper` private:

```cint geo/vec.ci
export struct V2 { I64 x; I64 y; }

export const I64 UNIT = 1000;

export I64 dot2(V2 a, V2 b) {
    return a.x * b.x + a.y * b.y;
}

export V2 scaled(V2 a, I64 k) {
    return V2(a.x * k, a.y * k);
}

I64 helper() {
    return 0;
}
```

The program `main.ci` imports it and reaches each exported name through the qualifier `vec`, the last segment of the module path:

```cint main.ci
import geo.vec;

void main() {
    vec.V2 a = vec.V2(3, 4);
    vec.V2 b = vec.scaled(a, 2);
    "{vec.dot2(a, b)} {b.x} {vec.UNIT}\n";
}
```

```console
$ cint run main.ci
50 6 1000
```

17. `as` gives the qualifier another name, and the struct's fields can be named in its constructor:

```cint short.ci
import geo.vec as v;

void main() {
    v.V2 a = v.V2(x = 1, y = 2);
    "{v.dot2(a, a)}\n";
}
```

```console
$ cint run short.ci
5
```

18. A selected import names the listed declarations without a qualifier:

```cint pick.ci
import geo.vec.{V2, dot2};

void main() {
    V2 a = V2(3, 4);
    "{dot2(a, a)}\n";
}
```

```text
25
```

19. A private name is not visible to an importer:

```cint private.ci
import geo.vec;

void main() {
    "{vec.helper()}\n";
}
```

```console
$ cint run private.ci
error[C3008]: undefined or unusable name
  --> private.ci:4:11
   |
 4 |     "{vec.helper()}\n";
   |           ^
```

20. Imports are not transitive. The module `geo.shape` imports `geo.vec`, and a program that imports only `geo.shape` cannot name `vec`:

```cint geo/shape.ci
import geo.vec;

export I64 square_area(I64 side) {
    return side * side * vec.UNIT;
}
```

```cint reach.ci
import geo.shape;

void main() {
    "{shape.square_area(3)} {vec.UNIT}\n";
}
```

```console
$ cint run reach.ci
error[C3005]: undefined or unusable name
  --> reach.ci:4:30
   |
 4 |     "{shape.square_area(3)} {vec.UNIT}\n";
   |                              ^
```

21. Module state is shared. The module `util.tally` counts calls in an exported variable; the program reads the count, assigns it and calls again:

```cint util/tally.ci
export I64 calls = 0;

export I64 tick() {
    calls += 1;
    return calls;
}
```

```cint counter.ci
import util.tally;

void main() {
    _ = tally.tick();
    _ = tally.tick();
    "{tally.calls}\n";
    tally.calls = 10;
    "{tally.tick()}\n";
}
```

```console
$ cint run counter.ci
2
11
```

22. Each name is declared once, so a function cannot be overloaded:

```cint overload.ci
I64 area(I64 side) {
    return side * side;
}

I64 area(I64 w, I64 h) {
    return w * h;
}

void main() {
    "{area(3)}\n";
}
```

```text
error[C3001]: duplicate name
  --> overload.ci:5:5
   |
 5 | I64 area(I64 w, I64 h) {
   |     ^
```

23. Imports come first:

```cint import_late.ci
I64 one() {
    return 1;
}

import util.text;

void main() {
    "{one()}\n";
}
```

```text
error[C3012]: `import` after other items
  --> import_late.ci:5:1
   |
 5 | import util.text;
   | ^
```

24. A module that imports itself is the shortest cycle:

```cint cycle.ci
import cycle;

export I64 one() {
    return 1;
}

void main() {
    "{one()}\n";
}
```

```text
error[C3003]: name or module rule
  --> cycle.ci:1:8
   |
 1 | import cycle;
   |        ^
```

**Notes**

25. The rules of paragraph 1 make one module path name the same file on every file system, including one that ignores the case of letters or reserves device names.
26. SPEC-06P.2 describes the options of `cint run`, `cint test` and `cint build`, including programs with several source roots.

<a id="exports"></a>

#### 6.15.2 Exports

`export` before a top-level declaration makes it visible to the modules that import its module. Every other declaration is private to its module. The exported declarations of a module are its interface, and an importer is translated against that interface alone (6.15.1).

**Constraints**

1. <a id="LS-227"></a>`export` is written before a declaration at the top level of a module (6.15.1). Before a test, a statement or a `static_assert` or inside a block, it is a syntax error, C1050 [LS-227].

**Semantics**

2. An exported declaration is visible to every module that imports its module, through the qualifier or the selection of the import (6.15.1). A declaration without `export` is private: it is visible throughout its own module, its tests included (6.15.3) and nowhere else [LS-227].
3. Exporting a struct exports all its fields. An importer constructs the struct, reads and assigns its fields and passes it as it does a struct of its own [LS-227].
4. Exporting an error set exports its values. An importer names the set as `m.E`, uses it in a result type such as `m.E!I64`, and writes its values as `.name` where the type is known (6.3.7) [LS-227].

**Examples**

5. The module `util.parse` exports an error set and a function that returns it:

```cint util/parse.ci
export error ParseError { empty, bad_digit }

export ParseError!I64 digit(I64 c) {
    if (c < 48 || c > 57) {
        return .bad_digit;
    }
    return c - 48;
}
```

6. A test is not exported:

```cint export_test.ci
export test "always" {
    assert(true);
}

void main() {
    "ok\n";
}
```

```text
error[C1050]: syntax error
  --> export_test.ci:1:8
   |
 1 | export test "always" {
   |        ^
```

**Notes**

7. `export` gives no access from a program written in another language. A host program calls the exported functions of a cint library through the host interface, which SPEC-03P.2 defines with the signatures it accepts.

<a id="tests"></a>

#### 6.15.3 Tests

A test is a named block at the top level of a module that checks what the module's code does. `cint test` runs the tests of a program, each in a fresh context, and reports which passed. A test either expects its body to complete or expects one fault, which its header names. 8.3 shows how tests serve as golden outputs.

**Syntax**

```ebnf
test_decl = "test" STRING [ "expect_fault" IDENT [ "at" DEC_INT ] ] block ;
```

**Constraints**

1. <a id="LS-232"></a>A test appears at the top level of a module of any kind: a script, a program with `main` or an importable module. A test inside a function or a block is a syntax error, C1050 [LS-232].
2. <a id="LS-233"></a>The name of a test is a string literal without holes. A hole in it is diagnosed C1033 [LS-233].
3. Two tests of one module shall not have the same name. The second is diagnosed C3020 at its `test` [LS-233].
4. <a id="LS-237"></a>`expect_fault` appears only in a test header. Anywhere else it is diagnosed C3021 at `expect_fault` [LS-237].
5. <a id="LS-236"></a>The name after `expect_fault` is a fault code of 5.2.3. A name that begins `E_` and is not one is diagnosed C1011 (6.4.4) [LS-236].
6. A test may name every declaration of its module, private ones included and the exported names of the modules its module imports. It shall not name a top-level variable of a script: C3004 (5.1.2) [LS-232].

**Semantics**

7. <a id="LS-239"></a>Every translation checks the tests of each module, so `cint run` reports a diagnostic in a test as `cint test` does. Only `cint test` runs tests. It runs no entry of the program: the top-level statements of a script and the function `main` do not run during `cint test`, unless a test calls `main` [LS-232, LS-239].
8. <a id="LS-234"></a>Each test runs as one entry (3.20) in a fresh context: every module-level variable starts at its initial value and every arena and pool is empty. No test can observe the effects of another [LS-234].
9. `cint test FILE` runs the tests of every module of the program whose root is `FILE`. It takes the modules in the byte order of their paths relative to the source root, and the tests of each module in source order, so the report is the same on every run [LS-234].
10. <a id="LS-235"></a>A test without `expect_fault` passes when its body completes, by reaching its end or by `return;`. It fails when an `assert` fails, when any other fault occurs, or when an error leaves it [LS-235].
11. In a test body, `try` admits an error of any set, and an error that leaves the test through `try` fails it, as 6.11.3 states. The report names the error as `cint run` names an error result of `main` (6.11.7). A test that expects an error binds it with `catch` and checks it with `assert` [LS-315].
12. A test with `expect_fault E_NAME` passes when its body faults and the fault's code is `E_NAME`. It fails when the body completes, when an error leaves it, or when it faults with another code. The fault ends the test as a fault ends any entry (5.2.3): nothing after it runs, deferred statements do not run, and the test's context is discarded. A fault is not caught or resumed, so a test checks one fault, and several faults take several tests [LS-236].
13. A failed `assert` is the fault `E_ASSERT` (6.10.9), so `expect_fault E_ASSERT` expects an assertion to fail [LS-236].
14. <a id="LS-240"></a>With `at N`, the test passes only when, in addition, the line of the fault's position (5.2.3) is `N`. That position is the faulting operation's, which may be inside a function the test calls [LS-240].
15. <a id="LS-238"></a>`cint test` writes one line for each test, `test <file>:<line> "<name>" ... ok` or `... FAILED`, where `<file>:<line>` is the position of the word `test`. After a failure it writes why: the fault report of 5.2.3, the error line of 6.11.7 or a line that names the fault the header expected. It ends with the line `<p> of <n> tests passed` and exits with status 0 when every test passed and 3 otherwise (SPEC-06P.2) [LS-238].

**Examples**

16. This example shows four tests of one function. The third expects a fault; the fourth states a wrong value, so its `assert` fails and the report shows both compared values:

```cint arith.ci
I64 mean(I64 total, I64 n) {
    return total / n;
}

test "floor division of negatives" {
    assert(-7 / 2 == -4);
    assert(-7 % 2 == 1);
}

test "mean of three" {
    assert(mean(9, 3) == 3);
}

test "a zero count faults" expect_fault E_DIV_ZERO {
    I64 m = mean(10, 0);
}

test "a wrong expectation fails" {
    assert(mean(10, 4) == 3);
}
```

```console
$ cint test arith.ci
test arith.ci:5 "floor division of negatives" ... ok
test arith.ci:10 "mean of three" ... ok
test arith.ci:14 "a zero count faults" ... ok
test arith.ci:18 "a wrong expectation fails" ... FAILED
fault[E_ASSERT]: assertion failed
  --> arith.ci:19:12
   |
19 |     assert(mean(10, 4) == 3);
   |            ^ assert.checked.bool
   |
   = operation: assert.checked.bool
   = operand-count: 2
   = operand: I64 2
   = operand: I64 3
   = exact:   none
   = limit:   none
...
3 of 4 tests passed
$ echo $?
3
```

17. This example shows errors in tests. The first test expects an error and checks which one; the second passes errors on with `try`; in the third an error leaves the test, which fails it:

```cint parse.ci
error ParseError { empty, bad_digit }

ParseError!I64 digit(I64 c) {
    if (c < 48 || c > 57) {
        return .bad_digit;
    }
    return c - 48;
}

test "a letter is a bad digit" {
    I64 v = digit(65) catch (e) {
        assert(e == .bad_digit);
        return;
    };
    assert(false);
}

test "two digits parse" {
    I64 a = try digit(52);
    I64 b = try digit(50);
    assert(a * 10 + b == 42);
}

test "an error fails the test" {
    I64 a = try digit(120);
    assert(a == 0);
}
```

```console
$ cint test parse.ci
test parse.ci:10 "a letter is a bad digit" ... ok
test parse.ci:18 "two digits parse" ... ok
test parse.ci:24 "an error fails the test" ... FAILED
error: parse.ParseError.bad_digit (tag 2)
2 of 3 tests passed
```

18. Each test starts from fresh module state, so both tests pass:

```cint state.ci
I64 counter = 0;

void bump() {
    counter += 1;
}

test "the first test sees fresh state" {
    bump();
    assert(counter == 1);
}

test "the second test sees fresh state too" {
    bump();
    bump();
    assert(counter == 2);
}
```

```console
$ cint test state.ci
test state.ci:7 "the first test sees fresh state" ... ok
test state.ci:12 "the second test sees fresh state too" ... ok
2 of 2 tests passed
```

19. `at` names the line of the faulting operation. Here it is the addition on line 2, inside the function the tests call, so the second test fails:

```cint lines.ci
I8 inc(I8 a) {
    return a + 1;
}

test "the fault is at line 2" expect_fault E_OVERFLOW at 2 {
    I8 top = inc(127);
}

test "the fault is not at line 6" expect_fault E_OVERFLOW at 6 {
    I8 top = inc(127);
}
```

```console
$ cint test lines.ci
test lines.ci:5 "the fault is at line 2" ... ok
test lines.ci:9 "the fault is not at line 6" ... FAILED
  expected fault E_OVERFLOW at line 6
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> lines.ci:2:14
   |
 2 |     return a + 1;
   |              ^ add.checked.i8
...
1 of 2 tests passed
```

20. The top-level statements of a script do not run during `cint test`. This script faults when it runs, and its test passes:

```cint notebook.ci
I8 level = 127;
level += 1;
"not reached\n";

I8 half(I8 v) {
    return v / 2;
}

test "half rounds down" {
    assert(half(-7) == -4);
}
```

```console
$ cint test notebook.ci
test notebook.ci:9 "half rounds down" ... ok
1 of 1 tests passed
$ cint run notebook.ci
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> notebook.ci:2:7
   |
 2 | level += 1;
   |       ^ add.checked.i8
...
```

21. Test names are unique within a module:

```cint test_dup.ci
"{twice(5)}\n";

I64 twice(I64 x) {
    return 2 * x;
}

test "twice" {
    assert(twice(2) == 4);
}

test "twice" {
    assert(twice(-2) == -4);
}
```

```text
error[C3020]: name or module rule
  --> test_dup.ci:11:1
   |
11 | test "twice" {
   | ^
```

22. `expect_fault` is not a statement:

```cint test_header.ci
void main() {
    I8 a = 127;
    expect_fault E_OVERFLOW;
    a += 1;
}
```

```text
error[C3021]: name or module rule
  --> test_header.ci:3:5
   |
 3 |     expect_fault E_OVERFLOW;
   |     ^
```

**Notes**

23. `cint test` reports verdicts; it does not show the output a test writes. A test states what it expects with `assert`, which records both compared values when it fails (6.10.9).
24. 8.3 uses tests as golden outputs, and SPEC-06P.2 gives the options of `cint test`.

<a id="future-directions"></a>

### 6.16 Future directions

This subclause lists constructs that are not part of this edition. Annex A has no production for them, and an implementation rejects or refuses a program that uses one (A.1, paragraph 4). Listing a construct here does not commit any edition to define it, or to use the spelling shown. Each entry says what a program writes instead in this edition.

<a id="future-types-and-values"></a>

#### 6.16.1 Types and values

1. This edition has no string type, whose values a program could store, compare and pass. A string literal is print text or a read-only argument (6.4.8).
2. This edition has no byte-string literals such as `b"..."`, no raw string literals, in which backslashes are not escapes, and no string literals of several lines. A string literal is UTF-8 text on one line, with the escapes of 6.4.8.
3. This edition has no enumerations. `const` declarations (6.9.2) name the values.
4. This edition has no tagged unions and no optional values. An error union (6.3.7) carries either a value or an error.
5. A rounding mode is not a value in this edition, so a program cannot store, pass or compare one. A mode is written as a bare name in a `round` clause or a mode argument (6.7.15).
6. This edition has no unsigned fixed point, and no fixed-point format whose integer and fraction bits total more than 128 (6.3.2).
7. This edition has no `U1024`. The widest unsigned type is `U512` (6.3.1).
8. This edition has no type properties such as `T.bits`, `T.frac_bits`, `T.bytes` and `offset`, no packed structs and no bit fields. The width of each type is fixed by its name (6.3), and the layout of a struct is not observable from a program.
9. This edition has no error sets with a signed underlying type, no error-set variables at module level and no conversion of an error value to its integer tag (6.3.7, 6.9.7).
10. A constant has a scalar type in this edition (6.9.2), so a constant array such as `const I32[4] PRIMES = [2, 3, 5, 7];` cannot be declared.
11. A program cannot declare a function or a type with a type parameter. The operations of clause 7 accept several types, and `Arena(T)`, `Pool(T)` and `Handle(T[])` (6.3.8) are the only types with a parameter.

<a id="future-declarations-and-statements"></a>

#### 6.16.2 Declarations and statements

12. This edition has no `var` declarations, which take a local's type from its initializer. An initializer made only of literals has no type of its own (6.5), so a declaration names its type.
13. A parameter has no default value. A call names every argument, positionally or by name (6.9.5).
14. A function has no `where` clause on its size parameters. A kernel may have one (6.13.1).
15. This edition has no tuples, so a function cannot return several values to be unpacked, as in `(q, r) = divmod(7, 2);`. A function returns one value and writes others through `inout` parameters (6.9.5).
16. This edition has no `do` loops, which test the condition after the body. A `while` loop with a `break` (6.10.3, 6.10.6) does the same.
17. A loop does not run over the elements of an array or a view, as in `for i, x in a { }`. A `for` statement runs over a range of indices (6.10.4).
18. An array or a view has no declared lower bound, such as `I64[1..=m]`. Every array and view is indexed from 0 in every dimension (6.12.3).
19. A function cannot return an array or a view, and a view cannot be stored at module level. A function writes its array results into a view parameter (6.12.4).
20. A constant expression cannot call a user function, and no operation makes the contents of a file a constant array. A constant expression holds literals, constants, operators and the named operations of 7.2 (6.8).
21. The only attribute of this edition is `@service` on an extern (6.9.9). There is no annotation on an export that sets how a function is called from C, Python or Fortran. The host interface of every export is the one SPEC-03P.2 defines (6.15.2).

22. A program cannot narrow its grant set before it passes it on, as in `g.narrow(fs.read("/data/a"))`: a function receives the whole grant set or none (6.9.9).
23. The standard services of this edition are `fs/1` and `console/1`. There is no service for a clock, a source of random bits, a directory listing or the removal of a file (6.9.9).
24. A `test` block receives no grant set, so a test calls no service extern (6.15.3).

<a id="future-operations"></a>

#### 6.16.3 Operations

25. This edition has no `sqrt_result`, the result form of `sqrt` (7.2.4), and `ArithError` has no value for an operand outside an operation's domain. A square root of a negative value faults `E_DOMAIN`.
26. `++` and `--` do not apply to a fixed-point place. `x += 1.0;` adds one to a fixed-point `x` (6.7.14).
27. This edition has no wrapping or saturating division (`div_wrap`, `div_sat`), no wrapping or saturating square root (`sqrt_wrap`, `sqrt_sat`), and no `divmod`, which needs tuples (paragraph 15). 7.2.2 gives the division functions.
28. `clamp` does not take fixed-point operands (7.2.1).
29. An integer literal has no exponent notation, such as `1e9`. Digit separators keep long literals readable: `1_000_000_000` (6.4.5).
30. A fraction literal is not the source of a conversion, as in `2.5 as I32`. A fraction literal takes a fixed-point type from its context (6.5), and that value converts (6.6).
31. A reduction does not run along one axis of an array of rank 2 or more, `fold_checked` takes no user function and does not apply `mul` to fixed-point elements, and `dot` does not take arrays of rank 2 (7.3).
32. This edition has no `equal(a, b)` or `swap(a, b)` on arrays, no operators such as `a + b` on whole arrays, no `reshape` that copies, no `transpose` of a rank other than 2, and no `extent` or `reverse` with a dimension computed at run time (7.4).
33. A kernel body calls no user function (6.13.3).
34. A program cannot format into an array, as in `format(buf, ...)`, and structs, error values, handles and arrays of rank 2 or more have no default rendering (7.5.2).

<a id="future-memory"></a>

#### 6.16.4 Memory

35. An arena or a pool cannot hold fixed-point elements, and a kernel body cannot declare one (6.14).

<a id="library"></a>

## 7 Library

The library is the set of operations a program calls by name without declaring or importing them: integer and fixed-point arithmetic that needs a wider intermediate or a named rounding, reductions over arrays, functions that measure, fill, copy and re-view arrays and the formatting that print statements apply to values. A library operation follows the rules of the operators. A checked operation gives an exact result, or a result rounded once by a mode the program names, and checks that result against its type; when the result does not fit, it faults with a record that names the operation, its operands and the bound it crossed. The wrapping and saturating operations (`sum_wrap`, `sum_sat`, `mul_wrap`, `mul_sat`) never fault on overflow.

The result forms `add_result` to `shl_result` are in 6.7.12. The members of arenas and pools, such as `alloc` and `insert`, are in 6.14.

<a id="reserved-names-and-arguments"></a>

### 7.1 Reserved names and arguments

Library names are reserved in every module, so `sum` or `min` means the same thing in every program. A call of a library operation is written like a function call, but some of its arguments are not values: a type that names the result, a rounding mode or an operation, each written as a bare name.

**Constraints**

1. The names in this table are the built-in functions. They are visible in every module without an import. A declaration of one of them, of any kind and in any scope, is diagnosed C3001 [LS-145].

| Names | Subclause |
|---|---|
| `abs`, `uabs` | 6.7.5 |
| `div_round`; `mul`, `div`, `mul_wrap`, `mul_sat` | 6.7.7 |
| `rotl`, `rotr` | 6.7.8 |
| `min`, `max`, `clamp` | 7.2.1 |
| `div_trunc`, `rem_trunc`, `div_euclid`, `rem_euclid` | 7.2.2 |
| `mul_full`, `muldiv`, `isqrt`, `isqrt_round` | 7.2.3 |
| `sqrt`, `qmul`, `mul_int` | 7.2.4 |
| `add_result`, `sub_result`, `mul_result`, `div_result`, `rem_result`, `shl_result` | 6.7.12 |
| `sum`, `fold_checked`, `sum_wrap`, `sum_sat`, `count`, `dot`; `min` and `max` over an array | 7.3 |
| `len`, `extent`, `size`, `copy`, `fill`, `transpose`, `reverse`, `reshape` | 7.4 |

2. The names `divmod`, `equal`, `swap`, `lower`, `wrap_bits`, `random`, `format`, `utf8`, `embed`, `to_device`, `to_host`, `rescale3`, `rescale3_rem`, `shl3`, `sign` and `tdot` are reserved for built-in operations: a declaration of one of them is diagnosed C3001, as for the names of paragraph 1, and this edition gives them no meaning [LS-145].
3. <a id="LS-146"></a><a id="LS-307"></a>Three kinds of argument are names, not values [LS-146, LS-307]:
   - A *type argument* is a type name written as the first argument of `muldiv`, `sum`, `dot` or `qmul`. It names the result type. Only these built-ins take a type argument; a type name as an argument of a user function is diagnosed C3007.
   - A *mode argument* is the name of a rounding mode (6.7.15), written bare as the last argument of `div_round`, `muldiv`, `isqrt_round`, `mul`, `div`, `mul_wrap`, `mul_sat`, `sqrt`, `qmul` or `dot`. A mode position is resolved by its position, not by looking the name up, so a variable named `floor` does not change `div_round(a, b, floor)`. Any other name in a mode position is diagnosed C3007.
   - An *operation argument* is `add` or `mul`, written bare as the first argument of `fold_checked`. Any other name there is diagnosed C3007.
4. A built-in takes no named arguments (C2063). A call with the wrong number of arguments for the built-in is diagnosed C2022 [LS-145].
5. Where the description of a built-in gives several value arguments one type `T`, they shall have the same type, and mixing two types is diagnosed C2001. A literal argument takes the type of the typed arguments. When every such argument is a literal, `T` is the type that the context of the call requires, or `I64` when the context requires none (6.5) [LS-145].

**Semantics**

6. Each call of a built-in is one operation. It gives its result or raises one fault, positioned at the name of the built-in in the call, whatever the implementation computes internally [IM-51].

**Examples**

7. A mode position takes the mode's name even when a variable has the same name:

```cint mode_position.ci
I64 floor = 10;
"{div_round(7, 2, floor)} {div_round(7, 2, ceil)} {floor}\n";
```

```text
3 4 10
```

8. A library name cannot be declared:

```cint reserved_name.ci
I64 count = 0;
"{count}\n";
```

```text
error[C3001]: duplicate name
  --> reserved_name.ci:1:5
   |
 1 | I64 count = 0;
   |     ^
```

**Notes**

9. In C, a local variable may hide a library function of the same name. A cint program cannot declare these names at all, so a reader of `sum(xs)` or `min(a, b)` never needs to look for a declaration.

<a id="integer-and-fixed-point-operations"></a>

### 7.2 Integer and fixed-point operations

These functions compute what an operator cannot: the lesser or greater of two values, quotients rounded toward zero or by the Euclidean convention, products and quotients that need a wider intermediate, square roots and fixed-point products of two formats or of a fixed-point value and an integer. Each gives an exact or once-rounded result and checks it against its result type, as a checked operator does.

| Call | Arguments | Result | Faults |
|---|---|---|---|
| `min(a, b)`, `max(a, b)` | `T` integer or fixed point | `T` | none |
| `clamp(x, lo, hi)` | `T` integer | `T` | `E_DOMAIN` |
| `div_trunc(a, b)`, `div_euclid(a, b)` | `T` integer | `T` | `E_DIV_ZERO`, `E_OVERFLOW` |
| `rem_trunc(a, b)`, `rem_euclid(a, b)` | `T` integer | `T` | `E_DIV_ZERO` |
| `mul_full(a, b)` | `T` of `I8` to `I512` or `U8` to `U256` | integer type of width 2`w`, same signedness | none |
| `muldiv(R, a, b, c, mode)` | `T` integer; `R` any integer type | `R` | `E_DIV_ZERO`, `E_OVERFLOW` |
| `isqrt(x)`, `isqrt_round(x, mode)` | `T` integer | `T` | `E_DOMAIN`, `E_OVERFLOW` |
| `sqrt(a)`, `sqrt(a, mode)` | `Q<i>.<f>` | the same type | `E_DOMAIN`, `E_OVERFLOW` |
| `qmul(R, a, b)`, `qmul(R, a, b, mode)` | `a`, `b` fixed point of any formats; `R` fixed point | `R` | `E_OVERFLOW` |
| `mul_int(a, n)` | `a` fixed point; `n` any integer type | the type of `a` | `E_OVERFLOW` |

The other built-in operations on single values are stated with the operators they complete: `abs` and `uabs` in 6.7.5; `div_round`, and the fixed-point `mul`, `div`, `mul_wrap` and `mul_sat` with a rounding mode, in 6.7.7; `rotl` and `rotr` in 6.7.8; and the result forms in 6.7.12. The rounding modes are in 6.7.15.

<a id="min-max-and-clamp"></a>

#### 7.2.1 min, max and clamp

`min` and `max` choose one of two values, and `clamp` limits a value to a range. None of them computes a new value, so none can overflow.

**Constraints**

1. <a id="IM-49"></a>The two arguments of `min(a, b)` and of `max(a, b)` shall have one integer or fixed-point type `T`. The three arguments of `clamp(x, lo, hi)` shall have one integer type `T` [LS-145, IM-49].
2. When `lo` and `hi` are both constant expressions and `lo > hi`, the call of `clamp` is diagnosed C6006 [IM-49].
3. A call of `min` or `max` whose last argument is an array or a view is the reduction of 7.3, not this function.

**Semantics**

4. `min(a, b)` gives the lesser and `max(a, b)` the greater of the two values. On fixed point they compare the values, which is the order of the storage integers [IM-49].
5. When `lo <= hi`, `clamp(x, lo, hi)` gives `lo` if `x < lo`, `hi` if `x > hi` and `x` otherwise. `min` and `max` never fault, and `clamp` faults only as paragraph 6 states [IM-49].

**Faults**

6. The fault `E_DOMAIN` is raised by `clamp` when `lo > hi` at run time, whatever `x` is. The record holds the operation identifier `clamp.checked.t` and the operands `x`, `lo` and `hi`. It has no exact result and no limit [IM-49].

**Examples**

7. This example shows `min`, `max` and `clamp`, including a value that `clamp` raises to its lower bound and one that it lowers to its upper bound:

```cint min_max.ci
I64 t = -7;
"{min(t, 2)} {max(t, 2)} {clamp(t, -5, 5)} {clamp(12, -5, 5)}\n";
```

```text
-7 2 -5 5
```

8. Constant bounds in the wrong order are found during translation:

```cint clamp_bounds.ci
I64 x = 9;
"{clamp(x, 5, 1)}\n";
```

```text
error[C6006]: compile-time evaluation rule
  --> clamp_bounds.ci:2:3
...
```

<a id="division-functions"></a>

#### 7.2.2 Division functions

The operators `/` and `%` divide with the quotient rounded toward negative infinity (6.7.7). These functions give the two other conventions in common use.

| Function | Quotient `q` | Remainder `r` |
|---|---|---|
| `a / b`, `a % b` (6.7.7) | `floor(a / b)` | zero or the sign of `b` |
| `div_trunc(a, b)`, `rem_trunc(a, b)` | rounded toward zero | zero or the sign of `a` |
| `div_euclid(a, b)`, `rem_euclid(a, b)` | the `q` for which `0 <= r < \|b\|` | never negative |

**Constraints**

1. <a id="IM-36"></a>The two arguments of each function of this subclause shall have one integer type `T`. A fixed-point argument is diagnosed C2103 [IM-36].

**Semantics**

2. For `b != 0`, `div_trunc(a, b)` is the rational `a / b` rounded toward zero, and `rem_trunc(a, b)` is `a - b * div_trunc(a, b)` [IM-36].
3. For `b != 0`, `div_euclid(a, b)` is the integer `q` for which `r = a - b * q` satisfies `0 <= r < |b|`, and `rem_euclid(a, b)` is that `r` [IM-36].
4. In each pair, `a == q * b + r` holds exactly. The quotients of `div_trunc` and `div_euclid` are checked against `T`: when the exact quotient is outside the range of `T`, the function faults [IM-36, IM-39].
5. For signed `T`, `MIN(T)` divided by -1 has the quotient `-MIN(T)`, which is outside the range, so `div_trunc` and `div_euclid` fault. Its remainder is 0, so `rem_trunc(MIN(T), -1)` and `rem_euclid(MIN(T), -1)` give 0 and do not fault. An implementation shall give 0 for these remainders and shall not trap [IM-39, IM-40].

**Faults**

6. The fault `E_DIV_ZERO` is raised by every function of this subclause when `b == 0`, before any other check. The record holds the operation identifier `<name>.checked.t`, for example `rem_euclid.checked.i64`, and the operands `a` and `b`. It has no exact result and no limit. Its position is the name of the function in the call [IM-39].
7. The fault `E_OVERFLOW` is raised by `div_trunc` and `div_euclid` when the quotient is outside the range of `T`. The record holds the same operation and operands, the exact quotient, and the bound it crossed [IM-39].

**Examples**

8. The three conventions differ when the operands have opposite signs:

```cint division_forms.ci
I64 a = -7;
I64 b = 2;
"{a / b} {a % b}\n";
"{div_trunc(a, b)} {rem_trunc(a, b)}\n";
"{div_euclid(a, b)} {rem_euclid(a, b)}\n";
"{div_euclid(7, -2)} {rem_euclid(7, -2)}\n";
```

```text
-4 1
-3 -1
-4 1
-3 1
```

9. The remainders of `MIN(T)` by -1 are 0; the quotient does not fit:

```cint trunc_overflow.ci
I64 m = I64.min;
I64 d = -1;
"{rem_trunc(m, d)} {rem_euclid(m, d)}\n";
I64 q = div_trunc(m, d);
```

```text
0 0
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> trunc_overflow.ci:4:9
   |
 4 | I64 q = div_trunc(m, d);
   |         ^ div_trunc.checked.i64
   |
   = operation: div_trunc.checked.i64
   = operand-count: 2
   = operand: I64 -9223372036854775808
   = operand: I64 -1
   = exact:   9223372036854775808
   = limit:   I64 9223372036854775807
   ...
```

10. This example shows division by zero:

```cint euclid_zero.ci
I64 a = 5;
I64 z = 0;
"{rem_euclid(a, z)}\n";
```

```text
fault[E_DIV_ZERO]: division by zero
  --> euclid_zero.ci:3:3
   |
 3 | "{rem_euclid(a, z)}\n";
   |   ^ rem_euclid.checked.i64
   |
   = operation: rem_euclid.checked.i64
   = operand-count: 2
   = operand: I64 5
   = operand: I64 0
   = exact:   none
   = limit:   none
   ...
```

**Notes**

11. Fortran's `MOD` truncates, like C's `%` and corresponds to `rem_trunc`; Fortran's `MODULO` corresponds to `%`. A remainder that must lie in `0` to `|b| - 1`, such as an index into a ring buffer, is `rem_euclid` whatever the sign of `b`.

<a id="wide-intermediates"></a>

#### 7.2.3 Wide intermediates

An operator never computes in a wider type than its operands. When a computation needs more bits in the middle than at the end, such as a product that is divided at once, the program asks for it by name. Each of these functions keeps its intermediate exact and rounds at most once.

**Constraints**

1. <a id="IM-51"></a>The arguments of `mul_full(a, b)` shall have one type `T` among `I8` to `I512` and `U8` to `U256`; `I1024` and `U512` are diagnosed C2103 [IM-51].
2. <a id="IM-53"></a>In `muldiv(R, a, b, c, mode)`, `a`, `b` and `c` shall have one integer type `T`. `R` may be `T` or any other integer type [IM-53].
3. The argument of `isqrt(x)` and of `isqrt_round(x, mode)` shall have an integer type `T` [IM-51].

**Semantics**

4. `mul_full(a, b)` gives the exact product `a * b` as a value of the integer type of width 2`w` and the same signedness as `T`: `I16` for `I8`, `I128` for `I64`, `I1024` for `I512`, `U128` for `U64`, `U512` for `U256`. Every such product fits, so `mul_full` never faults [IM-51].
5. `muldiv(R, a, b, c, mode)` forms the exact product `a * b`, divides it by `c` as a rational, rounds the quotient once by `mode` and checks the rounded value against `R`. The product is never narrowed, whatever the widths of `T` and `R` [IM-51, IM-53].
6. `isqrt(x)` gives `floor(sqrt(x))`: the unique `r >= 0` with `r * r <= x < (r + 1) * (r + 1)` [IM-51].
7. `isqrt_round(x, mode)` gives the square root of `x` rounded by `mode`. The square root of an integer is never exactly halfway between two integers, so every `half_` mode gives the nearest integer. The rounded value is checked against `T` [IM-51].

**Faults**

8. The fault `E_DIV_ZERO` is raised by `muldiv` when `c == 0`. The record holds the operation identifier `muldiv.checked.t.r.mode`, which names both `T` and `R`, for example `muldiv.checked.i64.i64.floor`, and the operands `a`, `b` and `c`. It has no exact result and no limit [IM-51].
9. The fault `E_OVERFLOW` is raised by `muldiv` when the rounded quotient is outside the range of `R`. The record holds the same operation and operands, the rounded quotient as the exact result, and the bound of `R` it crossed [IM-51].
10. <a id="IM-52"></a>The fault `E_DOMAIN` is raised by `isqrt` and `isqrt_round` when `x < 0`. The record holds the operation identifier `isqrt.checked.t` or `isqrt_round.checked.t.mode` and the operand `x`. It has no exact result and no limit [IM-52].
11. The fault `E_OVERFLOW` is raised by `isqrt_round` when the rounded root is outside the range of `T`, with the rounded root as the exact result [IM-51].

**Examples**

12. This example shows a product that overflows `I64` and a scaled ratio that does not:

```cint wide_ops.ci
I64 big = I64.max;
I128 p = mul_full(big, big);
"{p}\n";
"{muldiv(I64, big, big, big, floor)}\n";
"{muldiv(I64, 7, 5, 2, half_even)} {muldiv(I64, 9, 5, 2, half_even)}\n";
I128 x = 1 << 65;
"{isqrt(x)} {isqrt_round(x, half_even)}\n";
```

```text
85070591730234615847396907784232501249
9223372036854775807
18 22
6074000999 6074001000
```

13. The exact result in a `muldiv` record is the rounded quotient, before the range check:

```cint muldiv_overflow.ci
I64 big = I64.max;
I64 three = 3;
I64 r = muldiv(I64, big, three, 2, ceil);
"{r}\n";
```

```text
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> muldiv_overflow.ci:3:9
   |
 3 | I64 r = muldiv(I64, big, three, 2, ceil);
   |         ^ muldiv.checked.i64.i64.ceil
   |
   = operation: muldiv.checked.i64.i64.ceil
   = operand-count: 3
   = operand: I64 9223372036854775807
   = operand: I64 3
   = operand: I64 2
   = exact:   13835058055282163711
   = limit:   I64 9223372036854775807
   ...
```

14. `mul_full` of `U64` gives `U128`:

```cint mul_full_u64.ci
U64 a = U64.max;
U128 p = mul_full(a, a);
"{p}\n";
```

```text
340282366920938463426481119284349108225
```

<a id="fixed-point-operations"></a>

#### 7.2.4 Fixed-point operations

A fixed-point value `Q<i>.<f>` is held as a storage integer `A`, its value being `A / 2^f` (6.3.2). The operators combine values of one format. These functions take a square root, multiply values of two different formats and multiply a fixed-point value by an integer. Each rounds at most once, on the exact result, and checks the rounded storage integer against the range of the result's storage type.

**Constraints**

1. <a id="IM-67"></a>The argument of `sqrt(a)` and of `sqrt(a, mode)` shall have a fixed-point type. An integer argument is diagnosed C2103; `isqrt` is the integer square root [IM-67].
2. <a id="IM-63"></a>In `qmul(R, a, b)` and `qmul(R, a, b, mode)`, `R` shall be a fixed-point type and `a` and `b` values of fixed-point types, of any formats. An integer argument is diagnosed C2103 [IM-63].
3. <a id="IM-64"></a>In `mul_int(a, n)`, `a` shall have a fixed-point type and `n` an integer type of any width [IM-64].

**Semantics**

4. For `a` of `Q<i>.<f>` with storage integer `A >= 0`, `sqrt(a, mode)` gives the value whose storage integer is `isqrt_round(A * 2^f, mode)`, computed in an intermediate of at least 2`w` bits. `sqrt(a)` uses `half_even`. The storage integer is then checked against the range of the storage type. For `i >= 2` the root is always in range; for `Q1.<f>`, the modes that round up can reach `2^(w - 1)`, which is not [IM-67].
5. For `a` of `Q<ia>.<fa>` with storage integer `A`, `b` of `Q<ib>.<fb>` with storage integer `B`, and `R` of `Q<ir>.<fr>`, `qmul(R, a, b, mode)` forms the exact product `A * B`, whose scale is `2^(fa + fb)`. When `fa + fb > fr` the storage integer of the result is `A * B / 2^(fa + fb - fr)` rounded once by `mode`, and `qmul(R, a, b)` uses `half_even`. Otherwise it is `A * B * 2^(fr - fa - fb)`, exactly and nothing rounds. The storage integer is then checked against the range of `R` [IM-63].
6. `mul_int(a, n)` gives the value of the type of `a` whose storage integer is `A * n`, exactly, checked against the range of that type. Nothing rounds [IM-64].

**Faults**

7. The fault `E_DOMAIN` is raised by `sqrt` when `a < 0`. The record holds the operation identifier `sqrt.checked.q.mode`, where `q` spells the type with an underscore for the point (`q16_16` for `Q16.16`) and `mode` is the mode used, written or default; the operand is `a`. It has no exact result and no limit [IM-52, IM-67].
8. The fault `E_OVERFLOW` is raised by `sqrt` when the rounded storage integer is out of range, for example `sqrt(Q1.15.raw(32767), ceil)`, whose rounded storage integer is 32768. The record holds the operation, the operand, that storage integer as the exact result, and the type's bound as the limit [IM-67].
9. The fault `E_OVERFLOW` is raised by `qmul` when the storage integer of the result is outside the range of `R`. The operation identifier is `qmul.checked.qa.qb.r`, followed by `.mode` only when the operation rounds; the operands are `a` and `b`, and the exact result is the rounded storage integer [IM-63].
10. The fault `E_OVERFLOW` is raised by `mul_int` when `A * n` is outside the range of the storage type. The operation identifier is `mul_int.checked.q.n`, for example `mul_int.checked.q16_16.i64`; the operands are `a` and `n`, the exact result is `A * n`, and the limit is the bound of the type of `a` [IM-64].

**Examples**

11. A square root rounded two ways, a product of two formats and a product by an integer. `0.5` in `Q1.15` has the storage integer 16384, and `1.5` in `Q16.16` has 98304; their product has the scale `2^31`, and `qmul` rounds it to `2^16` or widens it to `2^32`:

```cint fixed_ops.ci
Q16.16 two = 2.0;
"{sqrt(two)} {sqrt(two, floor)}\n";
Q16.16 a = 1.5;
Q1.15 h = 0.5;
"{qmul(Q16.16, a, h)} {qmul(Q32.32, a, h)}\n";
I32 k = 3;
"{mul_int(a, k)}\n";
```

```text
1.414215087890625 1.4141998291015625
0.75 0.75
4.5
```

12. This example shows a square root of a negative value. In a fault record, a fixed-point value is written as its storage integer, `Q16.16 raw -131072` for -2.0:

```cint sqrt_domain.ci
void show(Q16.16 a) {
    "{sqrt(a)}\n";
}
show(-2.0);
```

```text
fault[E_DOMAIN]: operand outside the operation's domain
  --> sqrt_domain.ci:2:7
   |
 2 |     "{sqrt(a)}\n";
   |       ^ sqrt.checked.q16_16.half_even
   |
   = operation: sqrt.checked.q16_16.half_even
   = operand-count: 1
   = operand: Q16.16 raw -131072
   = exact:   none
   = limit:   none
   ...
```

13. A product by an integer that leaves the range of `Q16.16`. The exact result is the storage integer `A * n`:

```cint mulint_over.ci
void show(Q16.16 a, I64 k) {
    "{mul_int(a, k)}\n";
}
show(20000.5, 2);
```

```text
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> mulint_over.ci:2:7
   |
 2 |     "{mul_int(a, k)}\n";
   |       ^ mul_int.checked.q16_16.i64
   |
   = operation: mul_int.checked.q16_16.i64
   = operand-count: 2
   = operand: Q16.16 raw 1310752768
   = operand: I64 2
   = exact:   2621505536
   = limit:   Q16.16 raw 2147483647
   ...
```

<a id="reductions"></a>

### 7.3 Reductions

A reduction combines the elements of an array or a view into one value. Checked and saturating additions do not give the same result in every order: with `M` the maximum of `I64`, `(M + 1) + (-1)` faults while `M + (1 + (-1))` gives `M` and in `I8` saturating `120 + 120 - 120` gives 7 left to right but 120 right to left. So each reduction is a named operation whose result is defined independently of how an implementation schedules the work [IM-73].

| Call | Result | Order | Faults |
|---|---|---|---|
| `sum(xs)`, `sum(R, xs)` | the exact sum, checked once against `R` (default: the element type) | any | `E_OVERFLOW` at the final check |
| `fold_checked(add, init, xs)`, `fold_checked(mul, init, xs)` | `init` combined with each element in turn, each step checked | logical order | `E_OVERFLOW` at the first step out of range |
| `sum_wrap(xs)` | the exact sum, wrapped to the element type | any | none |
| `sum_sat(xs)` | `0` combined with each element in turn by `+\|` | logical order | none |
| `min(xs)`, `max(xs)` | the least or greatest element | any | `E_SHAPE` when `xs` is empty |
| `min(init, xs)`, `max(init, xs)` | as above, with `init` included | any | none |
| `count(mask)` | the number of `true` elements, as an `I64` | any | none |
| `dot(R, a, b)` | the exact sum of the products `a[k] * b[k]`, checked once against `R` | any | `E_SHAPE`, `E_OVERFLOW` |

**Constraints**

1. <a id="IM-77"></a>The array argument of a reduction, `xs`, `mask`, `a` or `b`, shall be an array or a view; any other argument is diagnosed C2103. Every reduction but `dot` accepts any rank from 1 to 4 [IM-77].
2. The elements shall have an integer or fixed-point type, diagnosed C2103 otherwise, except for `count`, whose elements shall be `Bool` (C2001) [IM-77].
3. <a id="R-4"></a>In `sum(R, xs)`, `R` shall be an integer type when the elements are integers; any integer `R` is allowed, wider or narrower than the elements. When the elements are fixed point, `R` is the element type. Any other `R` is diagnosed C9102 [IM-77, R-4].
4. In `fold_checked(op, init, xs)` and in `min(init, xs)` and `max(init, xs)`, `init` shall have the element type (C2001). The operation `mul` is allowed on integer elements [IM-77].
5. <a id="IM-87"></a>`dot(R, a, b)` shall have a type as its first argument (C3007). `a` and `b` shall be rank-1 arrays or views of one element type (C2001). `R` is an integer type when the elements are integers and a fixed-point type when they are fixed point, and a mode argument may follow only for fixed point [IM-87].
6. A reduction across the work-items of a kernel is written with a `reduce` statement (6.13.5).

**Semantics**

7. <a id="IM-74"></a>The *logical order* of the elements of an array or view is the row-major order of their logical indices, in which the last index varies fastest. It does not depend on the layout of the storage, on strides, or on where the view begins [IM-74].
8. <a id="IM-78"></a><a id="IM-82"></a>`sum(xs)` and `sum(R, xs)` compute the exact sum of the elements in Z and check it once against `R`, which is the element type when omitted. Partial sums are never checked, so every order of addition gives the same result. The final check is a range check on an exact value, so it raises `E_OVERFLOW`, not the `E_NARROW` of a conversion [IM-77, IM-78, IM-82].
9. <a id="IM-80"></a>An implementation may compute a sum of `n` signed or unsigned `w`-bit elements in any accumulator of at least `w + ceil(log2(n))` bits, in any association order, because no partial sum can leave that range; or by any other method that gives the exact sum. For `I64` and narrower elements an `I128` accumulator is enough [IM-80].
10. <a id="IM-83"></a>`fold_checked(add, init, xs)` sets an accumulator of the element type to `init` and then, for each element `x` in logical order, sets it to `acc + x` with a checked addition in the element type; `fold_checked(mul, init, xs)` uses a checked multiplication. The result is the final accumulator. A fault is raised at the least index `j` whose step leaves the range. For `add`, the accumulator after element `j` is `init + x_0 + ... + x_j` exactly, so an implementation may compute exact prefix sums in a wider accumulator and report the least `j` whose prefix is out of range [IM-77, IM-83].
11. `sum_wrap(xs)` gives `wrap(E, s)`, where `E` is the element type and `s` the exact sum. Wrapped addition is associative and commutative, so every order gives this result [IM-77, IM-82].
12. <a id="IM-84"></a>`sum_sat(xs)` sets an accumulator to 0 and, for each element `x` in logical order, sets it to `acc +| x`. The result is the left-to-right value. An implementation that computes it in parallel shall reproduce that value exactly; reassociating `+|` does not conform [IM-77, IM-84].
13. <a id="IM-85"></a>`min(xs)` and `max(xs)` give the least and the greatest element. `min(init, xs)` and `max(init, xs)` include `init` among the elements. `count(mask)` gives the number of `true` elements as an `I64`. These results do not depend on order [IM-77, IM-85].
14. <a id="IM-79"></a>Over an empty array or view, `sum` gives 0 in its result type, `fold_checked` gives `init`, `sum_wrap`, `sum_sat` and `count` give 0 and `min(init, xs)` and `max(init, xs)` give `init` [IM-79].
15. `dot(R, a, b)` with integer elements forms the exact sum of the products `a[k] * b[k]` and checks it once against `R`. An accumulator of `2w + ceil(log2(n))` bits holds every partial sum [IM-87].
16. On fixed-point elements of one type `Q<i>.<f>`, `sum`, `sum_wrap`, `sum_sat`, `fold_checked(add, ...)`, `min` and `max` act on the storage integers as on integer elements of the storage type, and the result has the element type. Nothing rounds [R-4].
17. `dot(R, a, b, mode)` with elements of `Q<i>.<f>` and storage integers `A_k` and `B_k`, and `R` of `Q<j>.<g>`, forms the exact sum `S` of the products `A_k * B_k`, whose scale is `2^(2f)`. When `g < 2f` the storage integer of the result is `S / 2^(2f - g)` rounded once by `mode`, and `dot(R, a, b)` uses `half_even`; otherwise it is `S * 2^(g - 2f)`, exactly. One range check against `R` follows. The rounding is applied to the exact sum, so the result does not depend on the order of the additions [IM-87].
18. A reduction over `n` elements charges fuel as 5.2.4 specifies; each reduction is one operation whatever its length [IM-77].

**Faults**

19. Every fault of a reduction is positioned at the name of the built-in in the call [IM-77].
20. <a id="IM-81"></a>The fault `E_OVERFLOW` is raised by `sum` when the exact sum is outside the range of `R`. The record holds the operation identifier `sum.checked.e.r`, which always names both the element type and `R`, one operand, the count of elements `n` as an `I64`, the exact sum as the exact result, and the bound of `R` it crossed. It does not hold the elements [IM-81].
21. The fault `E_OVERFLOW` is raised by `fold_checked` at the least index `j` whose step leaves the range. The record holds the operation identifier `fold_checked.add.e` or `fold_checked.mul.e`, the operands `j` as an `I64`, the accumulator before step `j` and the element `x_j`, both in the element type, the out-of-range result as the exact result, and the bound it crossed [IM-83].
22. The fault `E_SHAPE` is raised by `min(xs)` and `max(xs)` when `xs` is empty. The record holds the operation identifier `reduce_min.checked.e` or `reduce_max.checked.e` and one operand, `I64 0`, the count. It has no exact result and no limit [IM-77].
23. The fault `E_SHAPE` is raised by `dot` when `a` and `b` have different extents, before any fuel is charged. The record holds the operation identifier `dot.checked.e.r`, the operands `I64 0`, the dimension and the extent of `b` as an `I64`, no exact result and the extent of `a` as the limit [IM-87].
24. The fault `E_OVERFLOW` is raised by `dot` when the result is outside the range of `R`, with the record of `sum`: the count `n`, the exact sum, rounded for fixed point, and the bound of `R`. For fixed point the operation is `dot.checked.q.r` followed by `.mode` only when the operation rounds [IM-87].

**Examples**

25. This example shows four reductions of the same three values:

```cint reductions.ci
void show() {
    I8[3] v = [120, 120, -120];
    I8 a = sum(v);
    I8 c = sum_sat(v);
    I8 d = sum_wrap(v);
    I8[3] u = [127, 127, 127];
    I16 e = sum(I16, u);
    "{a} {c} {d} {e}\n";
    "{min(v)} {max(v)} {min(-128, v)}\n";
    Bool[4] m = [true, false, true, true];
    "{count(m)}\n";
    I32[3] x = [1, 2, 3];
    I32[3] y = [4, 5, 6];
    "{dot(I64, x, y)}\n";
}
show();
```

```text
120 7 120 381
-120 120 -128
3
32
```

26. `fold_checked` checks every step; the exact total of `v` fits `I8`, but the second partial sum does not:

```cint fold_fault.ci
void show() {
    I8[3] v = [120, 120, -120];
    I8 b = fold_checked(add, 0, v);
    "{b}\n";
}
show();
```

```text
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> fold_fault.ci:3:12
   |
 3 |     I8 b = fold_checked(add, 0, v);
   |            ^ fold_checked.add.i8
   |
   = operation: fold_checked.add.i8
   = operand-count: 3
   = operand: I64 1
   = operand: I8 120
   = operand: I8 120
   = exact:   240
   = limit:   I8 127
   ...
```

27. A `sum` checks only its final result:

```cint sum_fault.ci
void show() {
    I8[3] u = [127, 127, 127];
    I8 f = sum(u);
    "{f}\n";
}
show();
```

```text
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> sum_fault.ci:3:12
   |
 3 |     I8 f = sum(u);
   |            ^ sum.checked.i8.i8
   |
   = operation: sum.checked.i8.i8
   = operand-count: 1
   = operand: I64 3
   = exact:   381
   = limit:   I8 127
   ...
```

28. The order-dependent reductions follow logical order. A Fortran array `A(2, 2)` stored column by column is reduced in its own element order by reducing the transposed view (7.4):

```cint reduce_order.ci
void show() {
    I8[2, 2] g = [[100, -100], [100, -100]];
    "{sum_sat(g)} {fold_checked(add, 0, g)} {sum_sat(transpose(g))}\n";
}
show();
```

```text
0 0 -73
```

29. This example shows the least element of an empty view:

```cint min_empty.ci
void show() {
    I64[3] v = [4, 5, 6];
    "{min(9, v[0..0])}\n";
    "{min(v[0..0])}\n";
}
show();
```

```text
9
fault[E_SHAPE]: shapes do not match
  --> min_empty.ci:4:7
   |
 4 |     "{min(v[0..0])}\n";
   |       ^ reduce_min.checked.i64
   |
   = operation: reduce_min.checked.i64
   = operand-count: 1
   = operand: I64 0
   = exact:   none
   = limit:   none
   ...
```

30. Fixed-point reductions keep the format; `dot` rounds the exact sum once. Here each product `2^-16 * 0.5` is below the resolution of `Q16.16`, but their exact sum is not:

```cint fixed_reductions.ci
void show() {
    Q16.16[3] v = [0.5, 0.25, -1.0];
    "{sum(v)} {min(v)} {sum_sat(v)}\n";
    Q16.16[2] x = [Q16.16.raw(1), Q16.16.raw(1)];
    Q16.16[2] y = [0.5, 0.5];
    "{dot(Q16.16, x, y)}\n";
}
show();
```

```text
-0.25 -1.0 -0.25
0.0000152587890625
```

**Notes**

31. <a id="IM-75"></a><a id="IM-76"></a>A Fortran array `A(m, n)` presented with the shape `[m, n]` is reduced in the order `A(1,1)`, `A(1,2)`, and so on, while Fortran's array element order is `A(1,1)`, `A(2,1)`, and so on. The order does not change `sum`, `sum_wrap`, `min`, `max` or `count`, but it changes `fold_checked` and `sum_sat`, as example 28 shows. Reducing `transpose(a)`, a view that copies no element, gives Fortran's order [IM-75, IM-76].

<a id="array-operations"></a>

### 7.4 Array operations

These functions report the shape of an array or view, fill and copy elements and give new views of the same elements in another order or shape. A view function moves no element; it gives a view with other extents and strides over the same storage (6.12).

| Call | Result |
|---|---|
| `len(a)` | the extent of a rank-1 array or view, as an `I64` |
| `extent(a, d)` | the extent of dimension `d`, counted from 0, as an `I64` |
| `size(a)` | the number of elements, the product of the extents, as an `I64` |
| `fill(dst, v)` | sets every element of `dst` to `v` |
| `copy(dst, src)` | copies the elements of `src` into `dst` |
| `transpose(a)` | a view of a rank-2 array or view with its two dimensions exchanged |
| `reverse(a, d)` | a view in which dimension `d` runs backward |
| `reshape(a, e1, ...)` | a view of the elements of `a`, in logical order, with the extents `e1, ...` |

**Constraints**

1. <a id="LS-145"></a>The first argument of each function shall be an array or a view, diagnosed C2103 otherwise [LS-145].
2. The argument of `len` shall have rank 1; `len` of an array of higher rank is diagnosed C2014; `extent` and `size` measure such an array [LS-145].
3. The dimension `d` of `extent(a, d)` and of `reverse(a, d)` is an `I64` constant expression whose value is a dimension of `a`, from 0 to its rank minus 1 [LS-145].
4. The destination of `fill` and of `copy` shall be a writable array or view, a place the function may write (C2067). The value of `fill` shall have the element type. The source of `copy` shall have the element type and rank of the destination (C2001), and where both extents of a dimension are known during translation they shall be equal (C2012) [LS-145].
5. When the storage that the two arguments of `copy` name is known during translation to overlap without being the same view, the call is diagnosed C5010 (6.12.5) [LS-145].
6. The argument of `transpose` shall have rank 2. The extents `e1, ...` of `reshape` are `I64` values (C2001), from one to four of them [LS-145].

**Semantics**

7. `len`, `extent` and `size` read the shape and never fault [LS-145].
8. `fill(dst, v)` stores `v` in every element of `dst`. `copy(dst, src)` stores each element of `src` in the element of `dst` at the same logical index. A `copy` whose two arguments are the same view stores nothing [LS-145].
9. `transpose(a)` gives the view whose element `[i, j]` is the element `[j, i]` of `a`. `reverse(a, d)` gives the view whose index `k` in dimension `d` names the element at index `extent(a, d) - 1 - k` of `a`. `reshape(a, e1, ...)` gives the view with extents `e1, ...` whose elements, in logical order, are the elements of `a` in logical order; this edition defines it when the product of `e1, ...` equals `size(a)` and the elements of `a` are stored contiguously in row-major order. Outside that condition `reshape` has no result in this edition, and an implementation refuses the program (3.16). Each view function gives a view of the same storage, which is writable when `a` is; no element is copied [LS-145].

**Faults**

10. The fault `E_SHAPE` is raised by `copy` when the extents of `src` and `dst` differ in a dimension, before any element is stored. The fault record is that of a copy, as 6.12.5, paragraph 9, states, and its position is the name `copy`.
11. The fault `E_ALIAS` is raised by `copy` when `src` and `dst` are not shown disjoint (6.12.5) and are not the same view, after the shape check and before any element is stored. The fault record is that of a copy, as 6.12.5, paragraph 10, states.

**Examples**

12. This example shows shapes, and three views of one array:

```cint array_ops.ci
void show(in I32[_, _] m) {
    for i in 0..extent(m, 0) {
        for j in 0..extent(m, 1) {
            "{m[i, j]} ";
        }
        "| ";
    }
    "\n";
}
void demo() {
    I32[2, 3] m = [[1, 2, 3], [4, 5, 6]];
    "{extent(m, 0)} {extent(m, 1)} {size(m)}\n";
    show(m);
    show(transpose(m));
    show(reverse(m, 1));
    show(reshape(m, 3, 2));
    I32[6] flat;
    fill(flat, 7);
    I32[3] row;
    copy(row, m[1]);
    "{len(flat)} {flat[0]} {row[0]} {row[2]}\n";
}
demo();
```

```text
2 3 6
1 2 3 | 4 5 6 | 
1 4 | 2 5 | 3 6 | 
3 2 1 | 6 5 4 | 
1 2 | 3 4 | 5 6 | 
6 7 4 6
```

13. A view is writable when its array is, so `fill` through a reversed view writes the array:

```cint fill_view.ci
void demo() {
    I32[2, 3] m = [[1, 2, 3], [4, 5, 6]];
    fill(reverse(m, 0)[0], 9);
    "{m[0, 0]} {m[1, 0]}\n";
}
demo();
```

```text
1 9
```

14. This example shows extents that differ only at run time:

```cint copy_shape.ci
void demo() {
    I32[4] a = [1, 2, 3, 4];
    I64 k = 2;
    I32[3] b;
    copy(b, a[0..k]);
    "{b[0]}\n";
}
demo();
```

```text
fault[E_SHAPE]: shapes do not match
  --> copy_shape.ci:5:5
   |
 5 |     copy(b, a[0..k]);
   |     ^ copy.shape
   |
   = operation: copy.shape
   = operand-count: 2
   = operand: I64 0
   = operand: I64 2
   = exact:   none
   = limit:   I64 3
   ...
```

15. This example shows overlapping storage, known only at run time:

```cint copy_alias.ci
void demo() {
    I32[4] a = [1, 2, 3, 4];
    I64 k = 1;
    copy(a[0..3], a[k..k + 3]);
    "{a[0]}\n";
}
demo();
```

```text
fault[E_ALIAS]: views overlap
  --> copy_alias.ci:4:5
   |
 4 |     copy(a[0..3], a[k..k + 3]);
   |     ^ copy.alias
   |
   = operation: copy.alias
   = operand-count: 0
   = exact:   none
   = limit:   none
   ...
```

<a id="formatting"></a>

### 7.5 Formatting

A print statement (6.10.10) writes its string literals with each hole `{...}` replaced by the text of a value. This subclause defines that text. Every value prints exactly: an integer in full in decimal, whatever its width and a fixed-point value as the exact decimal expansion of its value. No value is ever rounded for display unless the hole asks for a precision, and none is printed in scientific notation [IM-90, IM-91, LS-210].

<a id="holes"></a>

#### 7.5.1 Holes

A hole names an expression and, optionally, how to print it: the expression's own text before its value, a conversion applied first and a format specification.

**Syntax**

```ebnf
hole        = "{" hole_expr [ "=" ] [ "!" conversion ] [ ":" spec ] "}" ;
hole_expr   = or_expr ;
conversion  = "bits" | "raw" | "lanes" "(" lane_type ")" | "ratio" ;   (* "ratio" is reserved, 7.5.4 *)
lane_type   = "I8" | "I16" | "I32" | "I64" | "U8" | "U16" | "U32" | "U64" ;
```

`or_expr` is the expression of 6.7 without an unparenthesized conditional expression or `catch`. `spec` is in 7.5.3, and which `lane_type` applies to which value is in 7.5.4.

**Constraints**

1. <a id="LS-198"></a>Holes may appear in the string literals of a print statement and in the message of an `assert` (6.10.9). A hole in any other string literal is diagnosed C1033 [LS-198].
2. <a id="LS-199"></a>The hole expression is the longest prefix of the hole's text that is an `or_expr`. It shall not be empty and shall not contain a string literal or a brace, and a conditional expression in it shall be in parentheses: `{(ok ? a : b)}`. A hole that breaks these rules, has text left over after its expression, or names a conversion other than those of 7.5.4 and `ratio` is diagnosed C1035 [LS-198, LS-199].
3. The value of a hole shall not be a struct, a handle, an arena or a pool; such a hole is diagnosed C1037 [LS-214]. A hole whose value is an error value, or an array or view of rank 2 or more, has no rendering in this edition (6.16), and an implementation refuses a program that has one (3.16). The other values, of an integer type, a fixed-point type or `Bool` and rank-1 arrays and views of them, are rendered as 7.5.2 states.
4. <a id="LS-215"></a>A literal brace in a string literal is written twice, `{{` or `}}`; a single `}` is diagnosed C1039 [LS-215].

**Semantics**

5. `{expr}` prints the value of `expr` in the default rendering of its type (7.5.2) [LS-198].
6. <a id="LS-200"></a>`{expr=}` prints the text of `expr` exactly as written in the source, spaces included, then `=`, then the value. The `=` comes immediately before `!`, `:` or the closing brace [LS-198, LS-200].
7. `{expr!conv}` applies a conversion (7.5.4) before rendering, and `{expr:spec}` applies a format specification (7.5.3). The three parts may be combined, in the order `{expr=!conv:spec}`; `{expr:}` with an empty specification is `{expr}` [LS-198].
8. <a id="LS-197"></a>The holes of a statement are evaluated from left to right, each once, before anything from the statement is written. When the evaluation or the rendering of a hole faults, nothing from that statement is written; what earlier statements wrote stays written [LS-197].
9. <a id="LS-195"></a>The bytes written depend only on the values and the holes. They do not depend on locale, host operating system or executor: digits are the ASCII digits and the minus sign is `-` (U+002D) [LS-195].

**Examples**

10. This example shows plain holes, holes that print their own text with `=`, a format specification, a conditional expression in parentheses and doubled braces:

```cint holes.ci
I64 n = 5;
I64 x = 7;
Bool ok = true;
"{n} {n=} {n + 1 =} {n=:>4} {(ok ? n : x)} {{literal}}\n";
```

```text
5 n=5 n + 1 =6 n=   5 5 {literal}
```

11. A conditional expression needs parentheses:

```cint hole_cond.ci
Bool ok = true;
"{ok ? 1 : 2}\n";
```

```text
error[C1035]: invalid character or string literal
  --> hole_cond.ci:2:3
...
```

12. A fault in a hole writes nothing from its statement:

```cint hole_fault.ci
I64 x = I64.max;
"before\n";
"a {x} b {x + 1} c\n";
```

```text
before
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> hole_fault.ci:3:13
   |
 3 | "a {x} b {x + 1} c\n";
   |             ^ add.checked.i64
   ...
```

<a id="default-rendering"></a>

#### 7.5.2 Default rendering

Each type has one default text, the one a hole without a specification prints.

**Semantics**

1. <a id="IM-90"></a><a id="LS-214"></a>An integer of any width prints as its exact value in decimal: a `-` before a negative value, no sign before any other, no leading zeros and no grouping. `I1024` and `U512` values print every digit [IM-90, LS-214].
2. <a id="LS-207"></a>A fixed-point value prints as the shortest exact decimal expansion of its value with at least one digit after the point: `3.0`, `-0.25`. The value `A / 2^f` of a `Q<i>.<f>` always has a terminating decimal expansion of at most `f` fractional digits, so the default rendering is always exact [IM-90, LS-207, LS-214].
3. A `Bool` prints as `true` or `false` [IM-90, LS-214].
4. A rank-1 array or view prints as `[`, its elements in order separated by `, `, then `]`, every element, each rendered as the hole specifies [LS-214].

**Examples**

5. This example shows integers of every width:

```cint render_int.ci
I8 a = -128;
U64 b = U64.max;
I128 c = I128.min;
I1024 d = I1024.max;
"{a} {b} {c}\n{d}\n";
```

```text
-128 18446744073709551615 -170141183460469231731687303715884105728
89884656743115795386465259539451236680898848947115328636715040578866337902750481566354238661203768010560056939935696678829394884407208311246423715319737062188883946712432742638151109800623047059726541476042502884419075341171231440736956555270413618581675255342293149119973622969239858152417678164812112068607
```

6. This example shows unsigned wide types:

```cint render_unsigned_wide.ci
U128 a = U128.max;
U256 b = 1 << 200;
"{a}\n{b}\n";
```

```text
340282366920938463463374607431768211455
1606938044258990275541962092341162602522202993782792835301376
```

7. This example shows fixed point. The smallest positive `Q32.32` value, `2^-32`, needs 32 fractional digits:

```cint render_fixed.ci
Q32.32 a = 1.5;
Q32.32 b = 3;
Q32.32 c = Q32.32.raw(1);
Q16.16 d = -0.25;
"{a} {b} {d}\n{c}\n";
```

```text
1.5 3.0 -0.25
0.00000000023283064365386962890625
```

8. This example shows arrays and views:

```cint render_array.ci
void show() {
    I32[3] a = [1, -2, 3];
    Bool[2] f = [true, false];
    "{a} {a[1..]} {f} {a:+}\n";
}
show();
```

```text
[1, -2, 3] [-2, 3] [true, false] [+1, -2, +3]
```

<a id="format-specifications"></a>

#### 7.5.3 Format specifications

A format specification, after the `:` of a hole, controls the width and alignment of the text, the sign, the base, digit grouping, the number of fractional digits of a fixed-point value, and the scale of an integer printed as a decimal fraction. Its syntax follows Python's format specification mini-language; where the two differ, the rules here apply.

**Syntax**

```ebnf
spec      = [ [ fill ] align ] [ sign ] [ "#" ] [ "0" ] [ width ] [ group ]
            [ "." precision ] [ kind ] [ "/" scale ] ;
fill      = (* any Unicode scalar value other than "}" *) ;
align     = "<" | ">" | "^" ;
sign      = "-" | "+" | " " ;
width     = digit { digit } ;
group     = "_" | "," ;
precision = digit { digit } ;
kind      = "d" | "x" | "X" | "o" | "b" | "t" | "c" ;
scale     = "1" "0" { "0" } ;
digit     = "0" .. "9" ;
```

**Constraints**

1. <a id="LS-202"></a>The digits of `width`, `precision` and `scale` are the ASCII digits `0` to `9`. A specification that does not match the syntax, including a digit of another script, is diagnosed C1037 [LS-202].
2. <a id="LS-213"></a>A specification shall apply to the type of the value. Each of these is diagnosed C1037 [LS-213]:
   - a precision on an integer;
   - on `Bool`, anything but fill, alignment and width;
   - with `t`: `#`, a sign flag, `0` or a group;
   - with `c`: `#`, `0`, a group or a sign flag;
   - `,` with `x`, `X`, `o` or `b`;
   - `#` with `d` or with the default decimal kind;
   - a width with neither an alignment nor the `0` flag;
   - the `0` flag together with an alignment or a group;
   - a scale together with a kind other than `d`, with `#`, with a group, or with a conversion;
   - on a fixed-point value, a kind, a scale or `#`.
3. <a id="LS-206"></a>A precision together with a scale is diagnosed C1036 [LS-206, LS-213].

**Semantics**

4. <a id="LS-211"></a><a id="LS-212"></a>*Width, fill and alignment.* `width` is the minimum length of the text in Unicode scalar values. A shorter text is padded with the fill character, a space by default: after the text for `<`, before it for `>`, and on both sides for `^`, with the extra character, when the padding is odd, on the right. A text longer than the width is never truncated. Width and alignment apply to every rendering, `Bool` included and to each element of an array, not to the array as a whole [LS-202, LS-211, LS-212].
5. *Sign.* `-` prints a sign before negative values only, the default. `+` prints `+` before zero and positive values. A space prints a space before them [LS-202].
6. *Zero padding.* The `0` flag with a width pads with `0` digits between the sign, or the radix prefix, and the digits, to the width [LS-202].
7. <a id="LS-203"></a><a id="LS-204"></a>*Kinds.* An integer prints by its mathematical value in every kind, with `-` before a negative value: `{-255:x}` is `-ff`. `d` is decimal, the default; `x` and `X` are hexadecimal with lowercase or uppercase digits; `o` is octal; `b` is binary. With `#`, the prefix `0x`, `0o` or `0b` precedes the digits, always in lowercase, so `{255:#X}` is `0xFF`. The storage bit pattern of a negative value is printed only through the `!bits` conversion of 7.5.4 [LS-202, LS-203, LS-204].
8. *Groups.* `_` separates every three digits in decimal and every four in `x`, `X`, `o` and `b`, counted from the right; `,` separates every three digits in decimal [LS-202].
9. <a id="LS-205"></a>*Balanced ternary.* `t` prints the shortest `0t` literal (6.4.5) of the value: the prefix `0t`, then the digits `N`, `0` and `P` for -1, 0 and 1, most significant first, with no leading `0` digit; the sign is carried by the digits. `{8:t}` is `0tP0N`, `{-8:t}` is `0tN0P` and `{0:t}` is `0t0`. The text is always a valid literal of the same value [LS-205].
10. <a id="LS-209"></a>*Characters.* `c` prints the character whose code point is the value [LS-209].
11. *Scale.* `/scale`, a power of ten of at least 10, prints an integer `v` as the exact decimal `v / scale`, with as many fractional digits as `scale` has zeros: `{1234:/100}` is `12.34` and `{-5:/100}` is `-0.05`. This prints amounts held as scaled integers, such as cents, without a fixed-point type [LS-206].
12. <a id="LS-208"></a>*Precision.* `.precision` on a fixed-point value rounds the exact value to that many fractional digits, ties to even and prints exactly that many; with a precision of 0 it prints no point. When the rounded value is zero, no minus sign is printed [LS-208].
13. <a id="LS-210"></a>The text never uses an exponent or scientific notation [LS-210].

**Faults**

14. The fault `E_NARROW` is raised by a hole of kind `c` whose value is not a Unicode scalar value: a negative value, a surrogate code point from U+D800 to U+DFFF, or a value above U+10FFFF. The record holds the operation identifier `format.char.t`, the value as its one operand and as the exact result, and no limit. Its position is the first character of the hole expression [LS-209].

**Examples**

15. This example shows width, fill, alignment and sign:

```cint spec_layout.ci
I64 p = 42;
I64 n = -42;
I64 z = 0;
Bool ok = true;
"[{n}] [{z:+}] [{p: }] [{n:08}] [{p:>6}] [{p:<6}] [{p:*^7}] [{ok:>7}]\n";
```

```text
[-42] [+0] [ 42] [-0000042] [    42] [42    ] [**42***] [   true]
```

16. This example shows bases, prefixes and groups:

```cint spec_bases.ci
I64 h = 255;
I64 m = -255;
U32 w = 0xDEADBEEF;
I64 big = 1234567;
"[{h:x}] [{h:#X}] [{m:x}] [{h:o}] [{h:#b}] [{w:#_x}] [{big:_}] [{big:,}] [{m:#010x}]\n";
```

```text
[ff] [0xFF] [-ff] [377] [0b11111111] [0xdead_beef] [1_234_567] [1,234,567] [-0x00000ff]
```

17. This example shows balanced ternary and scaled integers:

```cint spec_ternary_scale.ci
I64 e = 8;
I64 m = -8;
I64 money = 123456;
I64 cents = -5;
I64 low = I64.min;
"[{e:t}] [{m:t}] [{e:*^9t}] [{money:/100}] [{cents:/100}] [{money:+010/100}]\n";
"{low:/100}\n";
```

```text
[0tP0N] [0tN0P] [**0tP0N**] [1234.56] [-0.05] [+001234.56]
-92233720368547758.08
```

18. A specification that does not apply is found during translation:

```cint spec_mismatch.ci
I64 n = 42;
"{n:.3}\n";
```

```text
error[C1037]: invalid format specification
  --> spec_mismatch.ci:2:3
...
```

19. This example shows characters. 233 is U+00E9, `é`:

```cint spec_char.ci
I64 c = 233;
"[{c:c}] [{c:>3c}]\n";
```

```text
[é] [  é]
```

20. Precision on fixed point rounds ties to even and never prints `-0`:

```cint spec_precision.ci
Q32.32 a = 0.125;
Q32.32 b = 0.375;
Q32.32 c = -0.03125;
Q32.32 d = 1.5;
"{a:.2} {b:.2} {c:.1} {d:.0} {d:>8} {d:+.3}\n";
```

```text
0.12 0.38 0.0 2      1.5 +1.500
```

<a id="format-conversions"></a>

#### 7.5.4 Conversions

A conversion, after the `!` of a hole, prints a value through another reading of the same bits: the two's complement pattern of an integer, the storage integer of a fixed-point value, or an integer split into lanes.

**Constraints**

1. <a id="LS-201"></a>`!bits` applies to integers and to fixed-point values, `!raw` to fixed-point values only and `!lanes(T)` to integers only, with `T` an unsigned type whose width divides the width of the value. Any other use, any conversion applied to `Bool`, `!ratio` applied to an integer and `!bits` together with the kind `t` or `c`, are diagnosed C1037 [LS-201, LS-213].

**Semantics**

2. `!bits` gives the two's complement bit pattern of the value as an unsigned integer of the same width: for `I8` -1 it is 255. On a fixed-point value it is the pattern of the storage integer. With `x`, `X`, `o` or `b`, the digits are padded with zeros to the full width of the type [LS-201].
3. `!raw` gives the storage integer of a fixed-point value, a signed integer, as `x.raw` does (6.3.2). The format specification then applies with the rules of integers [LS-201].
4. `!lanes(T)` splits the bit pattern of the value into lanes of the width of `T`, lane 0 the least significant and prints them as an array of `T` values, lane 0 first. The format specification applies to each lane [LS-201].

5. The conversion name `ratio` is reserved, and this edition gives `!ratio` no meaning. Applied to an integer or to `Bool` it is diagnosed C1037 (paragraph 1); applied to a fixed-point value, an implementation refuses the program (3.16) [LS-201].

**Examples**

6. This example shows `!bits`, `!lanes` and `!raw` on integers and on a fixed-point value:

```cint conversions.ci
I8 m1 = -1;
I8 low = -128;
I64 n = -42;
U32 w = 0x12345678;
Q8.8 q = -2.5;
"[{m1!bits}] [{m1!bits:x}] [{low!bits:#b}] [{n!bits:x}]\n";
"[{w!lanes(U8):02x}] [{w!lanes(U16)}] [{w=!lanes(U8)}]\n";
"[{q!raw}] [{q!raw:x}] [{q!bits:x}]\n";
```

```text
[255] [ff] [0b10000000] [ffffffffffffffd6]
[[78, 56, 34, 12]] [[22136, 4660]] [w=[120, 86, 52, 18]]
[-640] [-280] [fd80]
```

**Notes**

7. C's `printf("%x", -1)` reinterprets a negative value as unsigned without saying so. In cint, `{-1:x}` prints `-1`, and the pattern `ffffffffffffffff` appears only when the hole asks for it with `!bits`.

<a id="debugging"></a>

## 8 Faults, agreement and debugging

This clause explains how to find out why a program did what it did. 8.1 reads the record that a fault produces, field by field. 8.2 states which outcomes of a run are identical on every implementation, executor and machine and how to compare two runs. 8.3 shows how saved outputs and tests detect a change of behavior. 8.4 describes checkpoints, recordings and replay and 8.5 introduces the debug tools through a first session. What a fault is, and the state a fault leaves, are in 5.2.3; fuel and call depth are in 5.2.4 and 5.2.5.

<a id="fault-record"></a>

### 8.1 The fault record

A fault stops the entry that raised it (5.2.3) and produces one fault record: a fixed set of fields that say what failed, with which values, where, and in which program. The record holds no time, no memory address and no name of an executor, so every conforming implementation produces, for the same fault, a record equal in every field except `revision` and `source-map` (8.2). Every tool prints the record in one layout, the *fault report*.

**Semantics**

1. <a id="IM-106"></a>A fault produces exactly one fault record. Its fields are [IM-106, LS-278]:
   - `code`: the fault code (Annex B).
   - `operation`: the operation identifier of the operation that faulted, such as `add.checked.i64`, at most 64 ASCII characters (Annex B).
   - `operands`: every operand of that operation as a typed value, exact and never rounded or shortened, in source order; at most 8.
   - `exact`: the value that failed the range check. For an operation that does not round, it is the exact result (3.4). For an operation that rounds, such as `muldiv`, `div_round` or a conversion with a `round` clause, it is the value after the operation's one rounding. A record of `E_OVERFLOW` or `E_NARROW` has it; a record of another code has none unless the subclause of its operation gives one.
   - `limit`: the bound that was crossed: `MIN(T)` or `MAX(T)` of the result type, the largest permitted shift count, the extent of the indexed dimension, the fuel allowance or the depth limit. A record with no bound to cross, such as one of `E_DIV_ZERO`, has none.
   - `position`: the module-relative path of the source file, such as `month.ci`, the line and the column, both counted from 1 and the column counted in Unicode scalar values, of the operator token or of the callee's name for a call.
   - `revision`: the revision (3.31) of the program to which the position refers.
   - `source-map`: the digest of the program's *source map*, the table that gives the line and column of each fault site. The revision leaves source positions out, so an edit that only moves code, such as reformatting, keeps the revision; this field is the one that ties the record's positions to the source text they were taken from. SPEC-01P.3 defines the digest and the encoding of the whole record.
   - `address`: present only for a fault inside a kernel dispatch (paragraph 4).
   - `stack`: the call sites of the active calls (paragraph 2).

2. <a id="IM-107"></a>The `stack` field holds one position for each active call of a user function: the position of the callee's name at that call, outermost call first. The entry (3.20) is called by the host, not by program code, so it contributes no position: a fault in the entry's own code has an empty stack, and a fault `k` calls below the entry has `k` positions. A call that faults `E_DEPTH` is not entered; its position is the record's `position` and is not in the stack, so under a depth limit `D` the stack has at most `D - 1` positions [IM-107].

3. <a id="IM-111"></a>The record never holds a native memory address, register contents, the time of day, a thread identifier or the name of the executor that ran the program. A tool may print such things beside the record, marked as not part of it [IM-111].

4. <a id="IM-112"></a>An entry faults at most once. In sequential code the fault is the first in evaluation order (6.2.4). In a kernel dispatch several work-items can reach a fault; the record describes the *canonical fault*, the one that is least in this order [IM-112]:
   - the *dispatch number*, which counts the dispatches of the current entry from 0 in program order and starts again at 0 in each entry;
   - the *phase*: 0 for the checks made when the dispatch begins, 1 for the work-items, 2 for the reduction epilogue that follows them (6.13);
   - in phase 1, the *work-item index*, the row-major position of the work-item in the iteration space: in `kernel K[n, m]`, work-item `(i, j)` has index `i * m + j`;
   - in phase 1, the step ordinal of paragraph 5.

   The `address` field holds the kernel's qualified name and these numbers; in phases 0 and 2 it has no work-item index and no step ordinal. Sequential code has no address.

5. <a id="IM-113"></a>The *step ordinal* numbers the counted operations that a work-item evaluates, from 0, in evaluation order, including those inside the functions it calls. The step ordinal of a fault is the ordinal of the operation that faulted. Each evaluation of these operations is counted: every arithmetic operator in each of its plain, wrapping and saturating forms, the shifts and unary `-` included; every conversion; every subscript and every slice, whether it reads or is the place of a store; every call of a built-in operation of clause 7; and every call of a user function, counted at the call, before the callee's own operations. Literals, reads of variables, comparisons, the bitwise operators `&`, `|`, `^` and `~`, the logical operators, the conditional operator itself, loop control and fuel charges are not counted [IM-113].

6. <a id="IM-114"></a>An operation is counted even when the compiler has proved that it cannot fault, and however it is compiled. In the kernel body `out[i] = a[i] + b[i] * c[i];`, with the `+` overflowing, the place `out[i]` is step 0, because the place is evaluated first, `a[i]` is 1, `b[i]` 2, `c[i]` 3, `*` 4 and `+` 5, so the step ordinal is 5 [IM-114].

7. <a id="IM-115"></a>The canonical order is a definition, not a statement about which processor lane failed first. Every executor reports the canonical fault, in whatever order it ran the work-items; SPEC-02P.2 states how an executor that runs work-items in parallel finds it [IM-115].

8. Every tool prints a fault record as a fault report, in this layout:
   - the line `fault[CODE]:` followed by a fixed message for the code;
   - the line `-->` followed by the position;
   - the source line, with a caret under the operator token and the operation identifier beside it;
   - one line for each field of the record, in this order: `operation`, `operand-count`, one `operand` line for each operand, `exact`, `limit`, `revision`, `source-map`, `address` and `stack-depth`, then one `called from` line for each position of the stack, outermost first. A field that is absent reads `none`;
   - two lines that are not part of the record: `fuel-consumed`, the fuel the entry consumed before the fault, and `state`, which says what the fault left (5.2.3).

   An operand and the limit are written as the name of their type and their value in decimal, such as `I8 127`. A fixed-point value is written with its raw value (6.3.2): the type, the word `raw` and the raw value, so 300.75 of type `Q16.16` reads `Q16.16 raw 19709952`. A value in Z, such as the operand of a conversion decided during translation (6.6), reads `Z 300`. The exact result is written as a decimal integer alone; when the result type of the operation is a fixed-point type, it is a raw value.

   `cint run` writes the program's output to the standard output and the report to the standard error. With `--json` it writes the same fields as one JSON object (8.2, Example 8).

9. The session tools `cint workbench` and `cint debug` (8.5) print further lines after `state`, under the line `= noncanonical: not part of the fault record`: `stack-source`, the source line of each position of the stack; `work-item-tuple`, the index of a faulting work-item as a tuple; `workbench-position`, the entry or boundary of the session (8.4); `site`, the identifier that `cint explain` gives the operation that faulted (8.5); and `backend`, the executor that ran the entry. None of these lines is part of the record.

**Examples**

10. This example shows a deposit that does not fit. The fourth amount added to the total exceeds `I64.max`:

```cint month.ci
// month.ci: four deposits in whole cents; the last one is too large.
I64 add_cents(I64 total, I64 cents) {
    return total + cents;
}

I64 month() {
    I64[4] amounts = [1999, 425, 10, 9223372036854775000];
    I64 total = 0;
    for i in 0..4 {
        total = add_cents(total, amounts[i]);
        "after deposit {i}: {total}\n";
    }
    return total;
}

I64 m = month();
"month: {m}\n";
```

```text
after deposit 0: 1999
after deposit 1: 2424
after deposit 2: 2434
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> month.ci:3:18
   |
 3 |     return total + cents;
   |                  ^ add.checked.i64
   |
   = operation: add.checked.i64
   = operand-count: 2
   = operand: I64 2434
   = operand: I64 9223372036854775000
   = exact:   9223372036854777434
   = limit:   I64 9223372036854775807
...
   = source-map: none
   = address: none
   = stack-depth: 2
   = called from: month.ci:16:9
   = called from: month.ci:10:17
   = fuel-consumed: 10 (not part of the fault record)
   = state: the program stopped; stdout holds what it wrote before the fault
```

The report, read from the top:

| Line | What it says here |
|---|---|
| `after deposit ...` | The program's own output: three lines written before the fault, on the standard output. The report follows on the standard error. |
| `fault[E_OVERFLOW]` | The fault code, then the fixed message for that code. |
| `--> month.ci:3:18` | The position: file `month.ci`, line 3, the 18th character of the line, which is the `+` of `total + cents`. |
| excerpt | Line 3 of the source, a caret under the `+`, and the operation beside it. |
| `operation` | `add.checked.i64`, the checked `+` of two `I64` values. |
| `operand-count`, `operand` | Two operands in source order: `total`, which is 2434 after three deposits, and `cents`, the fourth amount. The record holds values, not names. |
| `exact` | The sum of the operands in Z, which `I64` cannot hold. |
| `limit` | `I64.max`, the bound that the exact result crossed. |
| `revision` | Left out above: 64 hexadecimal digits that identify the program (3.31). |
| `source-map` | The field for the digest of the source map (paragraph 1). cint 0.2.2 records none, so it reads `none`. |
| `address` | `none`: the fault is not inside a kernel dispatch. |
| `stack-depth`, `called from` | Two active calls, outermost first. The script's top-level statements are the entry and add no position; they call `month` at `month.ci:16:9`, and `month` calls `add_cents` at `month.ci:10:17`. |
| `fuel-consumed` | 10: 1 when the entry begins, 1 for the call of `month`, 4 for the iterations of the loop and 4 for the calls of `add_cents` (5.2.4). |
| `state` | What the fault left: the program stopped, and its output holds what it wrote before the fault (5.2.3). |

11. A rounding operation records the rounded value as its exact result. `muldiv` forms `I64.max * 3`, divides it by 2 and rounds the quotient once, half up, to 13835058055282163711 (7.2.3); that value fails the range check:

```cint rounded.ci
// rounded.ci: a price raised by half, rounded half up to a whole cent.
I64 raise(I64 cents) {
    return muldiv(I64, cents, 3, 2, half_up);
}

"{raise(5)}\n";
"{raise(I64.max)}\n";
```

```text
8
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> rounded.ci:3:12
   |
 3 |     return muldiv(I64, cents, 3, 2, half_up);
   |            ^ muldiv.checked.i64.i64.half_up
   |
   = operation: muldiv.checked.i64.i64.half_up
   = operand-count: 3
   = operand: I64 9223372036854775807
   = operand: I64 3
   = operand: I64 2
   = exact:   13835058055282163711
   = limit:   I64 9223372036854775807
...
```

The position of a call of a built-in operation is the callee's name: `rounded.ci:3:12` is the first letter of `muldiv`.

12. An index fault has the index as its operand and the extent of the array as its limit. It has no exact result:

```cint lookup.ci
// lookup.ci: a rate for each quarter, indexed from 0.
I64 rate(I64 quarter) {
    I64[4] rates = [5, 6, 7, 8];
    return rates[quarter];
}

"{rate(3)}\n";
"{rate(4)}\n";
```

```text
8
fault[E_BOUNDS]: index out of bounds
  --> lookup.ci:4:17
   |
 4 |     return rates[quarter];
   |                 ^ index.checked.i64
   |
   = operation: index.checked.i64
   = operand-count: 1
   = operand: I64 4
   = exact:   none
   = limit:   I64 4
...
```

13. A fault in a kernel has an address. Work-items 2 and 4 of this dispatch both overflow; the record describes work-item 2. The reference writes the same record as lines of text (SPEC-09P.2):

```console
$ cat scale.ci
// scale.ci: one work-item per element; work-items 2 and 4 overflow I32.
kernel scale[n](in I32[n] xs, out I32[n] ys) over [i: n] {
    ys[i] = xs[i] * 1000;
}

void run() {
    I32[5] xs = [5, 7, 3000000, 4, 2500000];
    I32[5] ys;
    scale(xs, ys);
    "{ys[0]}\n";
}

"start\n";
run();
$ cint run scale.ci
start
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> scale.ci:3:19
   |
 3 |     ys[i] = xs[i] * 1000;
   |                   ^ mul.checked.i32
   |
   = operation: mul.checked.i32
   = operand-count: 2
   = operand: I32 3000000
   = operand: I32 1000
   = exact:   3000000000
   = limit:   I32 2147483647
   = revision: f9a1803eff0a14c869489fb3338645e1219abaca73514174cccd7463a5a4710c
   = source-map: none
   = address: scale.scale, dispatch 0, phase 1, work-item 2, step 2
   = stack-depth: 2
   = called from: scale.ci:14:1
   = called from: scale.ci:9:5
   = fuel-consumed: 7 (not part of the fault record)
   = state: the program stopped; stdout holds what it wrote before the fault
$ python3 -m cint_ref run --format 4 scale.ci
case scale
format 4
source reference
outcome fault
stdout-bytes 6
stdout-sha256 46210dddc66714c3d8d226711510cf8421774214016c508c72a833a05370f6b5
fuel-consumed 7
fault.code E_OVERFLOW
fault.operation mul.checked.i32
fault.operand I32 3000000
fault.operand I32 1000
fault.exact 3000000000
fault.limit I32 2147483647
fault.position scale.ci:3:19
fault.revision self
fault.source-map self
fault.phase work-item
fault.kernel scale.scale
fault.address 0 2 2
fault.stack-depth 2
fault.stack scale.ci:14:1
fault.stack scale.ci:9:5
```

The address reads: kernel `scale` of module `scale`; dispatch 0, the first of the entry; phase 1, the work-items; work-item 2, whose element 3000000 times 1000 is 3000000000. Work-item 4 would fault as well, but 2 is the lower index. The step ordinal is 2: in `ys[i] = xs[i] * 1000;` the place `ys[i]` is step 0, `xs[i]` step 1 and the `*` step 2. The dispatch `scale(xs, ys)` at line 9 is a position of the stack, as a call is. The reference writes the phase by name and the address as the dispatch number, the work-item index and the step ordinal, and writes `self` for the revision and the source map, which it does not compute (8.2).

**Notes**

14. A report is read in three steps. The position and the excerpt show which operation failed. The operands and the exact result show why: which values were combined, and how far the result lies outside the type. The stack shows how the program reached that operation, from the outermost call inward.

15. After the source is edited, a saved report can point at a line that now holds other code. The revision changes with every change of the compiled program, so a report whose revision differs from the program's belongs to other code. An edit that only moves code, such as reformatting, changes positions but not the revision; that is what the `source-map` field is for (paragraph 1).

16. The `fuel-consumed` line is not part of the record, but the fuel an entry consumes is one of the outcomes on which every implementation agrees (8.2).

17. cint 0.2.2 records no source map in the `source-map` field of a fault record: every report of 0.2.2 prints `none` there. A recording and a case name the digest of the source map in their header (8.4), and `cint debug replay` prints it (8.5).

<a id="agreement"></a>

### 8.2 Determinism and agreement

A cint run has no hidden inputs. Given the same semantic inputs (3.24), every conforming implementation, on every executor and machine, writes the same output bytes, ends with no fault record or with one that is equal in every field except `revision` and `source-map` (paragraph 3), and consumes the same fuel. This subclause states exactly which outcomes are identical, what is not covered and how to compare two runs.

**Semantics**

1. <a id="IM-159"></a>The outcome of an entry is decided by its *execution identity*, the tuple of [IM-159]:
   - the language profile and the fuel model;
   - the revision of the program and the digest of its source map;
   - the entry, and its arguments;
   - the state before the entry;
   - the effects the host supplied to the entry (paragraph 2);
   - the fuel allowance, the call-depth limit and the frame-arena capacity.

   Two runs with equal execution identity *agree* when they produce equal:
   - state after the entry;
   - return value, or the absence of one;
   - fault record, or the absence of one;
   - fuel consumed by the entry;
   - *effect-request trace*: the ordered list of the requests the entry made of the host, each with its ordinal, call site, name and arguments, together with the bytes of every print.

   A program's output is the bytes of its print requests, in order, so two runs that agree write the same output bytes, including the bytes written before a fault. Each item is compared through the encoding and digest that SPEC-01P.3 gives it.

2. <a id="IM-160"></a>The effects the host supplied are its answers to the entry's requests, in order: the status of each, its result, and the contents it wrote into the program's arrays. The requests the entry sent out are not among them. In this document "deterministic" means only this: equal execution identity gives the five equal outcomes of paragraph 1. Host time, thread scheduling, memory addresses and host effects that were not recorded are not inputs of an entry, so none of them can change an outcome. A conforming implementation agrees with the reference on every execution in the scope of both, except in the `revision` and `source-map` fields of a fault record (paragraph 3). An entry ended by a failure of host resources, such as the host running out of memory, has no execution identity, and this paragraph says nothing about it. The *build identity* of a compiled program, which names the executable or kernel binary that a build produced, is separate: an x86-64 executable and a CUDA kernel binary of the same program have different build identities and the same revision. Agreement says nothing about whether a program's integer results are numerically adequate for the problem it models, for example in place of a floating-point computation; that is a separate question with its own evidence [IM-160].

3. A program is in the scope of both the reference and a compiler when the reference runs it and the compiler builds it without a refusal (4, paragraph 4). For such a program and the same semantic inputs, the compiled program and the reference write the same output bytes, consume the same fuel and end with fault records equal in every field except `revision` and `source-map`, which the reference does not compute and writes as `self`. A disagreement is a defect of one of them or an ambiguity in this document, never a choice between them. Agreement on one program is a measurement of that program and shows nothing about another [SPEC-09P.2 REF-06].

4. Every tool runs the same compiled form of a program: `cint run`, `cint test`, `cint workbench`, `cint debug`, `cint interp`, and every executor. No tool has arithmetic, fault or post-fault rules of its own. A difference between any two of them on the same semantic inputs is a defect.

5. Observation does not change meaning. Inspecting, watching, tracing, checkpointing and recording never change the state of a run, its step ordinals, the fuel it consumes or its fault.

**Examples**

6. The reference is run as `python3 -m cint_ref` (SPEC-09P.2), and `--format 4` selects the text form of an outcome shown below. The output of `month.ci` (8.1, Example 10) has the same digest under `cint` and under the reference, and the reference ends with the same fault record:

```console
$ cint run month.ci 2> /dev/null | sha256sum
2f1ada714964d61a382d8867eb0903f28bca1dbc9d918982300c7f445df4f3df  -
$ python3 -m cint_ref run --format 4 month.ci
case month
format 4
source reference
outcome fault
stdout-bytes 66
stdout-sha256 2f1ada714964d61a382d8867eb0903f28bca1dbc9d918982300c7f445df4f3df
fuel-consumed 10
fault.code E_OVERFLOW
fault.operation add.checked.i64
fault.operand I64 2434
fault.operand I64 9223372036854775000
fault.exact 9223372036854777434
fault.limit I64 9223372036854775807
fault.position month.ci:3:18
fault.revision self
fault.source-map self
fault.address none
fault.stack-depth 2
fault.stack month.ci:16:9
fault.stack month.ci:10:17
```

`2> /dev/null` discards the report, so `sha256sum` reads the output alone. The reference's `stdout-sha256` is the same digest, of the same 66 bytes. Every field of its fault matches the report of 8.1, and so does the fuel consumed, 10.

7. The fuel allowance is a semantic input. Given an allowance of 9, the same program has another outcome, and every implementation gives it: the tenth charge, for the fourth call of `add_cents`, faults `E_FUEL`. The record's position is that call, its limit is the allowance, and it has no operands and no exact result. The output is the same 66 bytes, because the fault comes after the third print:

```console
$ python3 -m cint_ref run --fuel 9 --format 4 month.ci
case month
format 4
source reference
outcome fault
stdout-bytes 66
stdout-sha256 2f1ada714964d61a382d8867eb0903f28bca1dbc9d918982300c7f445df4f3df
fuel-consumed 9
fault.code E_FUEL
fault.operation fuel.charge
fault.exact none
fault.limit I64 9
fault.position month.ci:10:17
fault.revision self
fault.source-map self
fault.address none
fault.stack-depth 1
fault.stack month.ci:16:9
```

8. With `--json`, `cint run` writes the record as one JSON object, after the program's output:

```console
$ cint --json run month.ci
after deposit 0: 1999
after deposit 1: 2424
after deposit 2: 2434
{"address":null,"code":"E_OVERFLOW","column":18,"exact":{"t":"Z","v":"9223372036854777434"},"file":"month.ci","limit":{"t":"I64","v":"9223372036854775807"},"line":3,"noncanonical":{"fuel_consumed":"10"},"operands":[{"t":"I64","v":"2434"},{"t":"I64","v":"9223372036854775000"}],"operation":"add.checked.i64","revision":"92b1e2717518c491d86f09d90528fa1930c049c76f2c4aad6f9e0392f84da4ac","schema":"CINT-FAULT-1","severity":"fault","source_map":null,"stack":[{"column":9,"file":"month.ci","line":16},{"column":17,"file":"month.ci","line":10}]}
```

The key `schema` names the format, `CINT-FAULT-1`. Every value is a string of decimal digits beside its type, as in `{"t":"I64","v":"2434"}`, so a JSON reader that converts numbers to binary floating point cannot change it; the exact result has the type `Z`. The key `revision` holds the program's revision, which the report of 8.1 left out. What is not part of the record is under `noncanonical`, here the fuel consumed.

**Notes**

9. To compare a run A with a run B, for two implementations, two executors or two machines:
   - Fix the semantic inputs: the same program text, so the same revision; the same input values; the same fuel allowance, depth limit and frame-arena capacity.
   - Keep the output and the report apart: `cint run p.ci > a.out 2> a.err`.
   - Compare the output bytes with `cmp` or a digest.
   - Compare the reports from the `fault[` line through the last `called from` line, and the `fuel-consumed` line. Under `--json`, compare every key outside `noncanonical`, and `fuel_consumed` inside it.
   - A difference in any of these is a defect of A or of B, not a property of the program. Reduce the program to the smallest one that still shows it. For two executors on one recorded run, `cint debug diff` makes this comparison at every boundary (8.5).

10. Two agreeing runs can still differ in what is not an outcome: the time and memory they take, the files a build writes, the text of the `state` line, which each tool words for its own context, and the lines under `noncanonical`. The reference writes `self` where an implementation writes the revision and the source map.

11. Agreement shows that two implementations give the same outcome, not that either outcome is what the programmer intended. Two implementations can agree and both be wrong. This document decides which outcome is correct, and the conformance suite checks it on many programs.

12. In C, two compilers or one compiler at two optimization levels, can produce different output from one program whose behavior is undefined or unspecified, such as a signed overflow or an unsequenced modification. cint has no such behavior (Annex C), so a difference between two cint implementations is a defect of one of them or an ambiguity in this document (paragraph 3).

13. cint 0.2.2 has three executors (3.19): `cpu-c17`, which compiles the program to C; `cpu-sir-interp`, the interpreter `cint-interp`; and `gpu-cuda`, which runs kernels on NVIDIA GPUs.

<a id="goldens"></a>

### 8.3 Golden outputs and tests

A *golden* is an output saved from a run that has been checked once, kept with the program and compared byte for byte with the output of every subsequent run of the same program on the same inputs. A difference means that the program, or the implementation that runs it, changed behavior. Because every conforming implementation gives the same outcome (8.2), a golden made on one executor or machine checks a run on any other. A golden can be a saved output, compared with `diff`; the expectations written into a module's tests (6.15.3), checked by `cint test`; or the reference's record of a whole outcome, the fault included.

**Semantics**

1. `cint test FILE` runs every test of the module `FILE` (6.15.3), in source order, each from the module's initial state, and prints one line for each test: the word `test`, the file and line of the test, its name in quotes and `ok` or `FAILED`. A failed test is followed by its fault report, whose `state` line says that the test's output is not shown; when an `assert` on a comparison fails, the record's operands are the two sides of the comparison, exactly and with their types (6.10.9). The last line counts the tests that passed. Tests may run in parallel; the order and the results reported are those of running them one after another.

**Examples**

2. This example shows a program whose output is worth keeping. `div_round` rounds the tax on 200 cents, 16.5 cents, to the even cent (7.2):

```cint invoice.ci
// invoice.ci: tax at 8.25 percent on three subtotals, in whole cents.
I64 tax(I64 subtotal) {
    return div_round(subtotal * 825, 10000, half_even);
}

void invoice() {
    I64[3] subtotals = [200, 2434, 600];
    for i in 0..3 {
        I64 s = subtotals[i];
        "tax on {s}: {tax(s)}\n";
    }
}

invoice();
```

```text
tax on 200: 16
tax on 2434: 201
tax on 600: 50
```

3. This example shows making and checking a golden. After the output is checked once, it is saved; each run after that is compared with it, and so is the reference's output. A change of rounding mode in `invoice_up.ci` changes one line of the output, and the comparison shows it:

```console
$ cint run invoice.ci > invoice.golden
$ cat invoice.golden
tax on 200: 16
tax on 2434: 201
tax on 600: 50
$ cint run invoice.ci | diff invoice.golden - && echo same
same
$ python3 -m cint_ref run --stdout ref.out invoice.ci > /dev/null
$ diff invoice.golden ref.out && echo same
same
$ diff invoice.ci invoice_up.ci
1c1
< // invoice.ci: tax at 8.25 percent on three subtotals, in whole cents.
---
> // invoice_up.ci: invoice.ci with half_up in place of half_even.
3c3
<     return div_round(subtotal * 825, 10000, half_even);
---
>     return div_round(subtotal * 825, 10000, half_up);
$ cint run invoice_up.ci | diff invoice.golden -
1c1
< tax on 200: 16
---
> tax on 200: 17
```

4. Tests keep the expectations inside the module. `tax.ci` states three: the tax on 2434 cents, the tie at 200 cents and the overflow of `subtotal * 825` near `I64.max`. As a script it also prints one line, which `cint run` shows; `cint test` runs the tests instead:

```cint tax.ci
// tax.ci: sales tax at 8.25 percent, in whole cents, rounded half to even.
I64 tax(I64 subtotal) {
    return div_round(subtotal * 825, 10000, half_even);
}

test "tax on 2434 cents" {
    assert(tax(2434) == 201);
}

test "a tie rounds to the even cent" {
    assert(tax(200) == 16);
}

test "a subtotal near I64.max overflows" expect_fault E_OVERFLOW {
    _ = tax(I64.max);
}

"tax on 200: {tax(200)}\n";
```

```text
tax on 200: 16
```

5. `tax_up.ci` is `tax.ci` with `half_up` in place of `half_even`. Its tie test fails, and the report shows both sides of the comparison: the tax is 17 where 16 was expected:

```console
$ cint test tax.ci
test tax.ci:6 "tax on 2434 cents" ... ok
test tax.ci:10 "a tie rounds to the even cent" ... ok
test tax.ci:14 "a subtotal near I64.max overflows" ... ok
3 of 3 tests passed
$ cint test tax_up.ci
test tax_up.ci:6 "tax on 2434 cents" ... ok
test tax_up.ci:10 "a tie rounds to the even cent" ... FAILED
fault[E_ASSERT]: assertion failed
  --> tax_up.ci:11:12
   |
11 |     assert(tax(200) == 16);
   |            ^ assert.checked.bool
   |
   = operation: assert.checked.bool
   = operand-count: 2
   = operand: I64 17
   = operand: I64 16
   = exact:   none
   = limit:   none
   = revision: 34ad5e3ae42e19c14239f8b71b3eee34f41bf4ac14dad8b7bd8200ce87ce381a
   = source-map: none
   = address: none
   = stack-depth: 0
   = fuel-consumed: 2 (not part of the fault record)
   = state: the test stopped; its output is not shown
test tax_up.ci:14 "a subtotal near I64.max overflows" ... ok
2 of 3 tests passed
```

6. This example shows a golden of a whole outcome. A saved output does not hold the fault, which goes to the standard error. The reference writes the outcome of a run as lines of text: the length and digest of the output, the fuel consumed and the fields of the fault (SPEC-09P.2). Saved, these lines are a golden of the whole outcome; `python3 -m cint_ref check` runs the program again and exits with status 0 when the outcome is the saved one:

```console
$ python3 -m cint_ref run month.ci > month.expect
$ cat month.expect
case month
source reference
outcome fault
stdout-bytes 66
stdout-sha256 2f1ada714964d61a382d8867eb0903f28bca1dbc9d918982300c7f445df4f3df
fuel-consumed 10
fault.code E_OVERFLOW
fault.operation add.checked.i64
fault.operand I64 2434
fault.operand I64 9223372036854775000
fault.exact 9223372036854777434
fault.limit I64 9223372036854775807
fault.position month.ci:3:18
fault.revision self
fault.address none
fault.stack-depth 2
$ python3 -m cint_ref check month.ci month.expect && echo same
same
```

**Notes**

7. A golden is only as good as its one check. Read the output before saving it, and check it against an independent computation where one exists: here, 8.25 percent of 2434 cents is 200.805 cents, which rounds to 201.

8. When a comparison with a golden fails, read the difference before replacing the golden. A golden replaced to make a comparison pass checks nothing.

9. A test states its expectation inside the module and runs with every `cint test`; a saved output records everything the program prints, including lines no test mentions. A program that prints a report benefits from both.

10. A golden made with any conforming implementation, on any executor or machine, is valid for every other (8.2). A golden that differs between two of them shows a defect in one of them, not a property of the program.

<a id="replay"></a>

### 8.4 Checkpoints, recordings and replay

A host program runs a cint program as a sequence of entries (3.20), and the end of an entry is a *boundary*. At a boundary no function of the program is running, and the program's *state* is its module-level variables, its arenas and pools and its fault record, with its identifier counters, its count of entries begun and its count of effects completed (SPEC-01P.3, 3.10). A *checkpoint* saves the state at a boundary. A *recording* saves everything needed to run a sequence of entries again, and *replay* runs them again and checks each boundary against the recording. The debug tools go back in time by replay: they never run a program backward.

The examples of this subclause drive entries with `cint workbench`, which 8.5 introduces: each line of cint source typed after the prompt `ci[N]>` is one entry, and a line that starts with `:` is a command.

**Semantics**

1. Boundaries are the ends of entries, and the host program decides where entries begin and end. Program code cannot make a boundary inside an entry, and the locals of a running function are not part of the state, so no checkpoint is taken in the middle of a loop. A program that does all its work in one `main` has one boundary, at its end. In a session, each entry is one step (Note 11).

2. A checkpoint holds the state at a boundary, which carries the program's revision, inside an *envelope*, a header that names the format, the runtime contract version (3.43) and the build of the program that wrote it (SPEC-01P.3); the envelope is not part of any identity. Restoring a checkpoint into a program of another revision fails. A checkpoint taken on another executor or by another build of the same revision is accepted. A build of the same source under another runtime contract version has another revision, so a checkpoint does not restore across that change: a restore requires the checkpoint's runtime contract to be exactly the context's [SPEC-03P.2 H-19].

3. Restoring a checkpoint makes its state current exactly, the fault record included, together with the fuel allowance and depth limit that were in force when it was taken. Before a restore, a session saves the current state as an automatic checkpoint `auto-N`, unless a checkpoint of the same state exists, so a restore loses nothing. A restore does not rewind the outside world: output printed after the checkpoint stays printed.

4. A recording holds [SPEC-03P.2 H-14]:
   - a header: the language profile, the fuel model, the runtime contract version, the revision, the digest of the source map, the mode (`full`, which holds every input, or `digest`, which holds digests of the inputs), and the boundary density, which says whether boundaries are the ends of entries only or also the ends of kernel dispatches;
   - the initial state, as a checkpoint;
   - the code of every revision the recording runs: each module's compiled form and its source;
   - the events, in order: each entry, with its arguments, fuel allowance, depth limit and frame-arena capacity; each effect, with its status, its result and, for a print, its bytes, and for a call of an `observe` extern, its arguments and the contents it wrote (6.9.8); each outcome, with its status, fault record, fuel consumed, result and state digest; each clearing of a fault, checkpoint, restore and change of revision; and last an end event, which holds the head of the chain of digests over the events. A recording without its end event is incomplete and is never loaded.

   A *case* is a recording in `full` mode that holds the source of every module, so it runs on another machine with nothing but the tools.

5. To reach a boundary, replay restores the latest checkpoint at or before it and runs the recorded entries again, with their recorded arguments, fuel allowances and limits and with the recorded answers of the host in place of the outside world. At each boundary it compares the outcome with the recording: the status, the fault record, the fuel consumed, the result and the state digest. The first difference stops replay with the status `DIVERGED` and the boundary where it happened. A print is compared with its recorded bytes and is not printed again. Going back one step is replay to the previous boundary.

6. Replay covers only what was recorded. `cint run` records nothing; a `cint workbench` session records every entry, and `cint debug locate` records the run it makes (8.5). Replay runs the recorded revisions, never code edited since. It compares outcomes at boundaries only, so a difference that appears and disappears inside one entry is not detected. Reaching a boundary costs a run from the nearest earlier checkpoint.

7. In a session, a fault leaves the state that 5.2.3 describes, and its record stays until it is cleared. Before the next entry the session clears it, as a host program does. Clearing changes nothing else, is recorded as an event, and is printed as `context reset`.

**Examples**

8. The module `ledger.ci` keeps a balance in cents. It has only declarations, so it is an importable module, not a script (5.1.2):

```cint
// ledger.ci: a balance kept in whole cents.
export I64 balance = 0;

export I64 deposit(I64 cents) {
    balance = balance + cents;
    return balance;
}

export I64 deposit_all[n](in I64[n] amounts) {
    for i in 0..n {
        _ = deposit(amounts[i]);
    }
    return balance;
}

export I64 month() {
    I64[4] amounts = [1999, 425, 10, 9223372036854775000];
    return deposit_all(amounts);
}

test "three deposits" {
    I64[3] amounts = [1999, 425, 10];
    assert(deposit_all(amounts) == 2434);
}

test "a deposit past I64.max faults" expect_fault E_OVERFLOW {
    _ = deposit(I64.max);
    _ = deposit(1);
}
```

9. This example shows a session that checkpoints, faults, replays and restores. In the output, `auto-N` names an automatic checkpoint, and an *observe result* is the answer to an observation request that the program made of its host (8.2):

```console
$ cint workbench ledger.ci
cint workbench   profile cint-core-1   backend cpu-sir-interp   new session
ci[1]> :load ledger.ci
loaded ledger (ledger.ci)
ci[2]> ledger.deposit(1999)
= 1999 : I64
ci[3]> :checkpoint paid
checkpoint paid   #1   revisions: ledger@r1
ci[3]> ledger.deposit(425)
= 2424 : I64
ci[4]> ledger.deposit(10)
= 2434 : I64
ci[5]> ledger.deposit(9223372036854775000)
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> ledger.ci:5:23
   |
 5 |     balance = balance + cents;
   |                       ^ add.checked.i64
   |
   = operation: add.checked.i64
   = operand-count: 2
   = operand: I64 2434
   = operand: I64 9223372036854775000
   = exact:   9223372036854777434
   = limit:   I64 9223372036854775807
...
   = state: no store completed in this entry; context reset
...
ci[6]> ledger.balance
= 2434 : I64
ci[7]> :effects
new context
revisions: ledger@r1
entry 2   ledger.deposit(1999)
  value I64 1999   fuel 2   state 8c69387e
entry 3   ledger.deposit(425)
  value I64 2424   fuel 2   state 4cd4a38f
entry 4   ledger.deposit(10)
  value I64 2434   fuel 2   state 345376d9
entry 5   ledger.deposit(9223372036854775000)
  fault E_OVERFLOW   fuel 2   state 5d62ad7e
context reset
entry 6   ledger.balance
  value I64 2434   fuel 1   state 4da4d5f3
entries 5   prints 0   context resets 1   changes of revision set 0
ci[7]> :save-case ledger.case
note: the case includes every recorded observe result verbatim; this case has none
case ledger.case   checkpoint paid   entries 3..6   effects 0   density entry
ci[7]> :watch ledger.balance
watch 1   ledger.balance = 2434 : I64
ci[7]> :replay paid
replaying 4 entries from paid to the current state (entry 3 to entry 6), comparing prints and outcomes with the recording
watch 1   entry 3   ledger.balance = 2424
watch 1   entry 4   ledger.balance = 2434
entry 5: E_OVERFLOW reproduced, its fault record equal to the recorded one
replay matches the recording at 4 boundaries
ci[7]> :goto 3
saved current state as auto-1
at the end of entry 3, reached by replay
later entries go on branch entry-3, from auto-2
ci[7]> ledger.balance
= 2424 : I64
ci[8]> :restore paid
saved current state as auto-3
restored paid   #1
ci[8]> ledger.balance
= 1999 : I64
ci[9]> :diff auto-1
binding          status      differing   first canonical difference
ledger.balance   differs     1           1999 here, 2434 in auto-1
ci[9]> :quit --yes
```

Step by step:

- `cint workbench ledger.ci` loads the module as entry 1; its exports are named `ledger.deposit` and `ledger.balance`.
- Entry 2 deposits 1999. `:checkpoint paid` saves the state at the boundary after entry 2, with the revision of each loaded module: `ledger@r1` is the first revision of `ledger` in the session.
- Entries 3 and 4 deposit 425 and 10. Entry 5 faults. Its `state` line says that no store of the entry completed, so `balance` keeps 2434, and that the session cleared the fault before the next entry. Entry 6 reads 2434.
- `:effects` lists the recording: each entry with its value or fault, its fuel and the first 8 hexadecimal digits of the state digest at its boundary. The clearing of the fault, `context reset`, is an event of its own.
- `:save-case ledger.case` writes a case that starts from the checkpoint `paid` and holds entries 3 to 6. None of them printed, so the case holds no effects.
- `:watch ledger.balance` asks for each change of the balance at a boundary. `:replay paid` restores `paid` and runs entries 3 to 6 again: the watch reports the changes at entries 3 and 4, entry 5 faults again with a record equal to the recorded one, and all 4 boundaries match the recording.
- `:goto 3` saves the current state as `auto-1`, then makes the boundary after entry 3 current by restore and replay. Entry 7 reads 2424 there. Entries typed after `:goto` start a new branch, named after the boundary.
- `:restore paid` saves the current state as `auto-3` and makes `paid` current, where entry 8 reads 1999. `:diff auto-1` compares the current state with `auto-1`, binding by binding: the balance differs, and both values are shown.

10. The case replays outside the session. A case saved from a session holds each typed entry as a function `wb_entry` of a module `session.ci`. `cint debug replay` prints one row for each boundary, with the event of the recording that ends it, the entry, its outcome and its fuel. `--to 2` stops after boundary 2. `cint debug locate --changed` finds the last boundary at which a binding changed:

```console
$ cint debug replay ledger.case
case       ledger.case   4 boundaries   13 events   mode full   density entry
revision   b355aac957aa2a7c3354721d8f5655d61f73d8eb7b3f3663754b473f33d8dbc4
source-map 51d7e33355e7ac6a2ef888d9be9a20ac4ff75b2816ffa6e52ab43eccc6763f40
boundary  event  entry                          outcome                  fuel
       1      2  wb_entry()                     value I64 2424           2
       2      5  wb_entry()                     value I64 2434           2
       3      8  wb_entry()                     faulted E_OVERFLOW       2
       4     12  wb_entry()                     value I64 2434           1
replay     ok, 4 of 4 boundaries compared with the recording
chain      086cf3fa7c8e63463e760f70b6aa1a3fb478c2337568b1c5c40cc2e5ad80f3dd
state      4da4d5f350a73fd9086179f8c15d41e47bf4343c82dd6c8df690d7a0935da4d9
$ cint debug replay ledger.case --to 2
case       ledger.case   4 boundaries   13 events   mode full   density entry
revision   b355aac957aa2a7c3354721d8f5655d61f73d8eb7b3f3663754b473f33d8dbc4
source-map 51d7e33355e7ac6a2ef888d9be9a20ac4ff75b2816ffa6e52ab43eccc6763f40
boundary  event  entry                          outcome                  fuel
       1      2  wb_entry()                     value I64 2424           2
       2      5  wb_entry()                     value I64 2434           2
replay     ok, 2 of 4 boundaries compared with the recording
chain      848c931491de4e7f7ebf29fabc5e8bec777181e820fcc6f009b3a0eb8a16550b
state      345376d98439eb1b1034db3124deb6ffd1c87f7720ab6a6a0f94e3b2a998e6e7
$ cint debug locate ledger.case --changed ledger.balance
case       ledger.case   4 boundaries
target     boundary 2 of 4, wb_entry(): the last change of ledger.balance, from 2424 to 2434
bracket    boundary 1, wb_entry(): value I64 2424; then boundary 2
the last 2 counted operations of boundary 2, of 3 trace events (:trace shows them all)
  ord  position           operation              operands                 value
    0  session.ci:4:8     call                   10
    1  ledger.ci:5:23     add.checked.i64        2424, 10                 2434
    -  session.ci:4:8     return                                          2434
```

`chain` is the head of the chain of digests over the events replayed, and `state` the state digest at the last boundary replayed. Its first 8 digits are those that `:effects` showed for the same boundary: entry 6 for the whole case, entry 4 for `--to 2`. Boundary 2, entry 4 of the session, is the last change of the balance; the fault at boundary 3 stored nothing.

**Notes**

11. A program meant to be stepped through is written as a module whose exported function performs one step, called once for each step by its host program or by `:run` in a session. Each call then ends at a boundary that can be checkpointed, watched and replayed.

12. A checkpoint costs storage in proportion to the state; replay costs time in proportion to the distance from the nearest checkpoint. Checkpoints taken often make replay short.

<a id="debug-tools"></a>

### 8.5 The debug tools

Five more commands of `cint` run the debug tools and the interpreter. `cint workbench` runs a *session*: cint source entered one entry at a time, with commands to inspect, checkpoint, restore and replay; `cint repl` runs the same session loop. `cint explain` describes what the compiler knows about a function without running it. `cint debug` finds the fault of a program or a recording, replays recordings and cases and compares two executors on a case. `cint interp FILE` runs a program on `cpu-sir-interp`, as `cint run` runs it on `cpu-c17`. The `backend` and `evaluator` fields of their output name the executor (3.19). This subclause teaches a first session and the commands used most; SPEC-06P.2 defines every command, option and output format.

**Semantics**

1. A session compiles each entry with the compiler that `cint run` uses and runs it on `cpu-sir-interp`. The type rules are those of clause 6: a session has no conversions, dynamic types or relaxed overflow checks of its own. The same entry in a session and in a compiled program gives the same values, output, state after a fault and fault record except its `revision`.

2. A session follows these rules:
   - A line that starts with `:` is a command. Any other line is cint source, a declaration, a statement or an expression, and is one entry.
   - Entries are numbered from 1, and the prompt `ci[N]>` shows the number the next entry takes. `:load` and `:run` each make one entry; the other commands make none. A position inside an entry is written `entry N:line:column`.
   - An entry that is not complete at the end of a line continues on the next, after the prompt `..>`.
   - A declaration adds to the *session module*. A variable declared at the prompt is a module-level variable of it, which keeps its value from entry to entry and is part of the state. A declaration makes a new revision of the session module.
   - An expression entry prints its value and its type.
   - Entries typed after a restore or a `:goto` start a new *branch* of the session's recording (8.4, example 9).
   - `cint workbench FILE` loads the module `FILE` as entry 1, which the session shows as the line `ci[1]> :load FILE`. Its exports are named with the module's name, as in `ledger.deposit`.
   - `:run f(args)` calls the exported function `f` of the session module as the entry itself, as `cint run` calls a program's entry, so `f` adds no position to a stack. An expression entry `f(args)` is an entry that calls `f`, and that call is a position of the stack.

3. This clause uses the following commands:

   | Command | What it does |
   |---|---|
   | `:load FILE` | Compiles and loads a module, as a new revision, and brings its exports into scope. |
   | `:run f(args)` | Calls the exported function `f` as the entry. |
   | `:inspect EXPR [/hex]` | Shows the value of `EXPR` exactly, and in hexadecimal with `/hex`. It is not an entry and changes nothing. |
   | `:trace` | Lists the counted operations of the last entry, by replaying it on `cpu-sir-interp`. |
   | `:fuel N`, `:depth N` | Sets the fuel allowance or the depth limit of each entry. By default an entry has no allowance and a depth limit of 256; `:fuel none` removes the allowance. |
   | `:checkpoint NAME` | Saves the state at the current boundary under a name. |
   | `:restore NAME` | Makes the state of a checkpoint current (8.4). |
   | `:diff NAME` | Compares the current state with a checkpoint, binding by binding. |
   | `:watch EXPR` | Evaluates `EXPR` at every boundary and reports each change. |
   | `:replay NAME` | Runs the recorded entries again from a checkpoint and compares each outcome with the recording. |
   | `:goto N`, `:goto fault` | Makes current the boundary after entry `N`, or after the entry that faulted, by restore and replay. |
   | `:effects` | Lists the recording of the current branch. |
   | `:save-case PATH` | Writes a case (8.4) for `cint debug`. |
   | `:quit [--yes]` | Ends the session; `--yes` ends it without asking about unsaved work. |

4. `cint explain FILE:NAME` describes the function or kernel `NAME` of the module `FILE` without running it: its signature, with the mode of each parameter; its effect class, `pure` when it prints nothing and calls nothing that prints, and `observe` otherwise (`pure` here means only that; such a function may write module state); every *fault site*, an operation that can fault, with its identifier `NAME#n`, its position, its operation identifier and its fault code, and whether a fault there is possible or excluded; its fuel charges; and its compiled form. A site's identifier is fixed for a revision, and the `site` line of a fault report names the site that faulted.

5. `cint debug locate PROGRAM --entry NAME` runs the exported function `NAME` of a module as the entry, on `cpu-sir-interp` under a recording, and replays the recording to the first boundary that ends in a fault or an error result. It prints the fault report, the *bracket*, which is the last boundary before that one and the boundary itself, and the last counted operations of the faulting entry. Given a recording or a case in place of a program, it replays that. With `--changed BINDING` it looks instead for the last boundary at which the binding changed, scanning backward: it replays the last interval between checkpoints, then the one before, until it finds the change. It does not bisect, because a value can change and change back between two checkpoints. `--save-case PATH` keeps what it recorded as a case.

6. `cint debug diff CASE` runs a case on two executors from the same checkpoint, with the same effects: `cpu-c17`, the baseline and `cpu-sir-interp`, the candidate. The output labels them `reference` and `candidate`; the label `reference` names the baseline executor, not the reference of 3.18. It compares the state digest at each boundary, and the outcome, and reports the first of these that applies:
   - `invalid-case`: the case failed validation;
   - `refused`: the candidate reported a refusal (3.16) for the case, which shows no agreement;
   - `unreproduced`: a difference did not repeat when the candidate ran again;
   - `fault-difference`: one side faulted and the other did not, or the two fault records differ;
   - `value-difference`: a state differs; the first differing boundary, binding and element are reported with both values;
   - `agree`: every boundary is equal, and so are the results and the fault records.

7. `cint debug CASE` opens a recording or a case in a read-only session. `:effects`, `:goto`, `:inspect`, `:trace` and `:replay` work there; an entry typed there is not accepted, so nothing typed is ever presented as part of the recording.

**Examples**

8. This example shows a first session. It starts with no module loaded:

```console
$ cint workbench
cint workbench   profile cint-core-1   backend cpu-sir-interp   new session
ci[1]> 6 * 7
= 42 : I64
ci[2]> I8 small = 100;
small = 100 : I8
ci[3]> small + 27
= 127 : I8
ci[4]> small + 28
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> entry 4:1:7
   |
 1 | small + 28
   |       ^ add.checked.i8
   |
   = operation: add.checked.i8
   = operand-count: 2
   = operand: I8 100
   = operand: I8 28
   = exact:   128
   = limit:   I8 127
   = revision: 7a2e7048b4517fe4ed50dc022b2835a872cb724a37ca8b173bde6ad95e9f97ce
   = source-map: none
   = address: none
   = stack-depth: 0
   = fuel-consumed: 1 (not part of the fault record)
   = state: no store completed in this entry; context reset
   = noncanonical: not part of the fault record
   = workbench-position: entry 4
   = site: wb_entry#2
   = backend: cpu-sir-interp
ci[5]> small +| 28
= 127 : I8
ci[6]> :inspect small /hex
small : I8   (signed, 8 bits)
  exact   100
  hex     0x64
ci[6]> export I64 cube(I64 x) {
..>     return x * x * x;
..> }
cube : (I64) -> I64   revision session@r2
ci[7]> :run cube(2097151)
= 9223358842721533951 : I64
ci[8]> :run cube(2097152)
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> entry 6:2:18
   |
 2 |     return x * x * x;
   |                  ^ mul.checked.i64
   |
   = operation: mul.checked.i64
   = operand-count: 2
   = operand: I64 4398046511104
   = operand: I64 2097152
   = exact:   9223372036854775808
   = limit:   I64 9223372036854775807
   = revision: fe6b5d6907815d89bd5e60d39033ae0188a65e04a93827b9952bef82dc894535
   = source-map: none
   = address: none
   = stack-depth: 0
   = fuel-consumed: 1 (not part of the fault record)
   = state: no store completed in this entry; context reset
   = noncanonical: not part of the fault record
   = workbench-position: entry 8
   = site: cube#3
   = backend: cpu-sir-interp
ci[9]> :trace
entry 8   :run cube(2097152)   evaluator cpu-sir-interp, by replay
  ord  position           operation              operands                 value
    0  entry 6:2:14       mul.checked.i64        2097152, 2097152         4398046511104
    1  entry 6:2:18       mul.checked.i64        4398046511104, 2097152   E_OVERFLOW
ci[9]> :quit --yes
```

Step by step:

- The first line names the language profile, `cint-core-1`, and the executor that runs each entry, `cpu-sir-interp`.
- `6 * 7` is an expression entry, and prints its value and type.
- `I8 small = 100;` declares a module-level variable of the session module.
- `small + 27` is 127, `I8.max`. `small + 28` faults at `entry 4:1:7`, the `+` in line 1 of entry 4. The stack is empty because the fault is in the entry's own code. Under `noncanonical`, `site` names the fault site `wb_entry#2` of the entry's compiled code.
- `small +| 28` is the saturating form, which gives 127 and does not fault (6.7.6).
- `:inspect` shows the value exactly and in hexadecimal; it makes no entry, so the prompt stays at 6.
- The declaration of `cube` takes three lines, continued after `..>`. It makes the second revision of the session module, `session@r2`.
- `:run cube(2097151)` makes `cube` the entry: 2097151 cubed fits in `I64`. `:run cube(2097152)` faults at the second `*`, because 2^42 times 2^21 is 2^63, one more than `I64.max`. The position `entry 6:2:18` lies in the text of entry 6, where `cube` was declared; `workbench-position` says that entry 8 ran it. The stack is empty because `:run` makes `cube` the entry.
- `:trace` replays the last entry and lists its counted operations: the first `*` gave 4398046511104 and the second faulted.

9. This example shows limits in a session. With a fuel allowance of 3, the call of `deposit` inside `deposit_all` would be the fourth charge: 1 when the entry begins, 1 for the call of `deposit_all` and 1 for the first iteration. With a depth limit of 2, the entry has depth 1 and `deposit_all` depth 2, so the call of `deposit` would exceed the limit and is not entered:

```console
$ cint workbench ledger.ci
cint workbench   profile cint-core-1   backend cpu-sir-interp   new session
ci[1]> :load ledger.ci
loaded ledger (ledger.ci)
ci[2]> :fuel 3
fuel: 3 units per entry
ci[2]> I64[5] small = [1, 2, 3, 4, 5];
small : I64[5]   5 elements   host
  [0] = 1
  [1] = 2
  [2] = 3
  [3] = 4
  [4] = 5
ci[3]> ledger.deposit_all(small)
fault[E_FUEL]: fuel allowance exhausted
  --> ledger.ci:11:13
   |
11 |         _ = deposit(amounts[i]);
   |             ^ fuel.charge
   |
   = operation: fuel.charge
   = operand-count: 0
   = exact:   none
   = limit:   I64 3
   = revision: e5f6a0f59e784400137676ad2d3022e75d83e23ed40f7cbdfbe0db4fc63fdfdf
   = source-map: none
   = address: none
   = stack-depth: 1
   = called from: entry 3:1:8
   = fuel-consumed: 3 (not part of the fault record)
   = state: no store completed in this entry; context reset
   = noncanonical: not part of the fault record
   = stack-source: entry 3:1:8
   |
 1 | ledger.deposit_all(small)
   |        ^
   |
   = workbench-position: entry 3
   = site: deposit_all#6
   = backend: cpu-sir-interp
ci[4]> :fuel none
fuel: no allowance per entry
ci[4]> :depth 2
depth: 2 per entry
ci[4]> ledger.deposit_all(small)
fault[E_DEPTH]: call depth limit exceeded
  --> ledger.ci:11:13
   |
11 |         _ = deposit(amounts[i]);
   |             ^ call.enter
   |
   = operation: call.enter
   = operand-count: 0
   = exact:   none
   = limit:   I64 2
   = revision: e5f6a0f59e784400137676ad2d3022e75d83e23ed40f7cbdfbe0db4fc63fdfdf
   = source-map: none
   = address: none
   = stack-depth: 1
   = called from: entry 4:1:8
   = fuel-consumed: 3 (not part of the fault record)
   = state: no store completed in this entry; context reset
   = noncanonical: not part of the fault record
   = stack-source: entry 4:1:8
   |
 1 | ledger.deposit_all(small)
   |        ^
   |
   = workbench-position: entry 4
   = site: deposit_all#6
   = backend: cpu-sir-interp
ci[5]> :quit --yes
```

Both faults are at the callee's name of the call that failed, have no operands and no exact result, and have the allowance or the limit as their limit. The stack holds one position, the call of `deposit_all` in the entry; the call of `deposit` that failed is the record's position, not a position of the stack (8.1, paragraph 2).

10. `cint explain` lists the fault sites of `deposit`. Lines about analysis, fuel charges and how each executor compiles the function are left out:

```console
$ cint explain ledger.ci:deposit
module ledger.ci   revision fe98a3096002b4f1708db7ae5b9254a718cc55103ac59c209538413ab8ec92da

function deposit   ledger.sites:4   pure
  signature  (in cents: I64) -> I64
  fault sites (2): 2 possible, 0 statically excluded
    deposit#1  ledger.ci:4:12  fuel.charge      E_FUEL possible
    deposit#2  ledger.ci:5:23  add.checked.i64  E_OVERFLOW possible
...
  sir
export func @ledger.deposit(%cents: I64) -> I64 {
  fuel.charge 1                                  #1 @ledger.ci:4:12
  %t0 = load.global @balance
  %t1 = add.checked.i64 %t0, %cents              #2 @ledger.ci:5:23
  store.global @balance, %t1
  %t2 = load.global @balance
  return %t2
}
```

The function line gives `ledger.sites:4`, the line of `deposit` in the module's SIR text (3.41), the textual form of the compiled program, not a source position: the SIR text is a separate file with its own line numbers. `deposit` is `pure`: it prints nothing. It has two fault sites: `deposit#1`, the fuel charge when the function is called, and `deposit#2`, the `+` at line 5. The section `sir` is the function's part of that SIR text, the compiled form, which `cpu-sir-interp` runs; `#1` and `#2` mark the two sites. The module's revision is the one in the reports of the next example.

11. `cint debug locate` runs `month` under a recording, finds its fault and keeps the recording as the case `month.case`:

```console
$ cint debug locate ledger.ci --entry month --save-case month.case
program    ledger.ci   evaluator cpu-sir-interp, recorded as the case month.case
target     boundary 1 of 1, month(): the first fault, reproduced by replay
bracket    the initial state; then boundary 1
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> ledger.ci:5:23
   |
 5 |     balance = balance + cents;
   |                       ^ add.checked.i64
   |
   = operation: add.checked.i64
   = operand-count: 2
   = operand: I64 2434
   = operand: I64 9223372036854775000
   = exact:   9223372036854777434
   = limit:   I64 9223372036854775807
   = revision: fe98a3096002b4f1708db7ae5b9254a718cc55103ac59c209538413ab8ec92da
   = source-map: none
   = address: none
   = stack-depth: 2
   = called from: ledger.ci:18:12
   = called from: ledger.ci:11:13
   = fuel-consumed: 10 (not part of the fault record)
   = state: the post-fault state of SPEC-01 IM-121, reached by replay
   = noncanonical: not part of the fault record
   = stack-source: ledger.ci:18:12
   |
18 |     return deposit_all(amounts);
   |            ^
   |
   = stack-source: ledger.ci:11:13
   |
11 |         _ = deposit(amounts[i]);
   |             ^
   |
   = workbench-position: boundary 1
   = site: deposit#2
   = backend: cpu-sir-interp, by replay
the last 16 counted operations of boundary 1, of 19 trace events (:trace shows them all)
  ord  position           operation              operands                 value
    0  ledger.ci:18:12    call                   4, 0
    1  ledger.ci:11:28    index.checked.i64      0
    2  ledger.ci:11:13    call                   1999
    3  ledger.ci:5:23     add.checked.i64        0, 1999                  1999
    -  ledger.ci:11:13    return                                          1999
    4                     add.wrap.i64           0, 1                     1
    5  ledger.ci:11:28    index.checked.i64      1
    6  ledger.ci:11:13    call                   425
    7  ledger.ci:5:23     add.checked.i64        1999, 425                2424
    -  ledger.ci:11:13    return                                          2424
    8                     add.wrap.i64           1, 1                     2
    9  ledger.ci:11:28    index.checked.i64      2
   10  ledger.ci:11:13    call                   10
   11  ledger.ci:5:23     add.checked.i64        2424, 10                 2434
    -  ledger.ci:11:13    return                                          2434
   12                     add.wrap.i64           2, 1                     3
   13  ledger.ci:11:28    index.checked.i64      3
   14  ledger.ci:11:13    call                   9223372036854775000
   15  ledger.ci:5:23     add.checked.i64        2434, 9223372036854775000 E_OVERFLOW
```

The entry is `month`, so its call of `deposit_all` at line 18 is the outermost position of the stack. The `state` line names IM-121, the state after a fault (5.2.3); `reached by replay` says that `locate` reached that state by replaying the recording. The bracket is the initial state and boundary 1, because the run has one entry. The trace shows the loop of `deposit_all` reading each amount, calling `deposit` and adding, until the fourth addition faults.

12. The case replays, and `cint debug diff` finds that `cpu-c17` and `cpu-sir-interp` agree on it:

```console
$ cint debug replay month.case
case       month.case   1 boundary   2 events   mode full   density entry
revision   fe98a3096002b4f1708db7ae5b9254a718cc55103ac59c209538413ab8ec92da
source-map 5166a2a13a791ed722204de3e5d81ef0913bd6a97ca5a14701a7a5b85836bed7
boundary  event  entry                          outcome                  fuel
       1      1  month()                        faulted E_OVERFLOW       10
replay     ok, 1 of 1 boundary compared with the recording
chain      ffa80772fafde299a3d0c4a7e288de3a8a9955434949a2822d8ef3b5150ea745
state      4ffbd08ab16440492352753af4f115886f504a056574fb17a8caa7de901c2b7c
$ cint debug diff month.case
case       month.case   1 boundary   density entry
reference  cpu-c17
candidate  cpu-sir-interp
boundaries 1 of 1 compared
outcome    agree
```

13. A case opens read-only, as the first line says. The session starts at the end of the case's last entry, and the prompt shows the number an entry would take. The balance after the fault is 2434: the three deposits that completed stay stored (5.2.3):

```console
$ cint debug month.case
cint workbench   profile cint-core-1   backend cpu-sir-interp   debug month.case (read-only)
case: entries 1   boundaries 1
at the end of entry 1, reached by replay
ci[2]> :effects
new context
entry 1   ledger.ci: month()
  fault E_OVERFLOW   fuel 10   state 4ffbd08a
entries 1   prints 0   context resets 0   changes of revision set 0
ci[2]> :goto fault
at the end of entry 1, reached by replay
ci[2]> :inspect ledger.balance
ledger.balance : I64
  exact   2434
ci[2]> :quit
```

**Notes**

14. A routine for an unexpected fault has these steps:
   - Read the report (8.1): the position, the operands and the exact result say what failed, and the stack says how the program got there.
   - Run `cint explain` on the function to see its fault sites; the report's `site` line names the one that faulted.
   - Run `cint debug locate` on the program to see the operations that led to the fault, and keep the run as a case with `--save-case`.
   - Run `cint debug diff` on the case. `agree` means the two executors agree on this compiled form; a defect in the compiler that produced the form is not excluded. Any other outcome points at one of the executors.
   - Fix the program, then add a test or a golden (8.3) that would have caught the fault.

15. In a trace, `ord` numbers the operations of the whole entry from 0, in evaluation order. A row with `-` is a return, listed but not numbered. An `add.wrap.i64` row that the source does not contain, without a position in Example 11, is a loop's step of its index variable from one value to the next. The step ordinal of a kernel fault (8.1, paragraph 5) is a different count: it starts at 0 in each work-item and does not count loop steps. The operands and values of a trace are exact, and the row that faulted shows the fault code as its value.

<a id="grammar"></a>

## Annex A (normative) Grammar

This annex is the complete grammar of the language: every token and every syntactic form that a program of this edition may contain. Clauses 6 and 7 give each form with its meaning, in Syntax parts that use the names and definitions of this annex; a Syntax part that shows only some alternatives of a production says so. This annex collects the forms in one place and adds the rules that decide between two readings of the same text.

<a id="grammar-notation"></a>

### A.1 Notation

1. <a id="LS-295"></a>A production has the form `name = definition ;`. In a definition, `|` separates alternatives, `[ x ]` is optional, `{ x }` is repeated zero or more times, `( )` groups, quoted text is a terminal, `"a" .. "z"` is any character in that range, and `(* *)` encloses a comment. Lexical productions, which define tokens, have names in capitals; the others define the syntax over tokens [LS-295].
2. A source file is read as a sequence of tokens from first character to last. At each point the token taken is the longest that the lexical productions allow. White space and comments may separate two tokens and are otherwise ignored; they are required only between two tokens that would otherwise read as one, such as a keyword and an identifier [LS-295].
3. The grammar gives the form of a program. A text that it produces may still break a constraint of clauses 5 to 7, which a diagnostic reports, such as an operand of the wrong type, a constant out of range, or a `switch` that does not cover its values. Those rules are not repeated here [LS-295].
4. A text that the grammar does not produce is not a program of this edition. A.8 lists the forms outside the grammar that are diagnosed with a code of their own. The forms listed in 6.16, among them `var` declarations, `enum` declarations and tuples, are reserved: this edition gives them no meaning, and an implementation rejects or refuses a program that uses one and does not run it. The reference refuses such a program (3.16). Any other text outside the grammar is a syntax error, C1050 [LS-295].

<a id="grammar-lexical"></a>

### A.2 Lexical grammar

5. <a id="LS-296"></a>The lexical productions are these [LS-296]:

```ebnf
(* characters *)
letter      = "A" .. "Z" | "a" .. "z" | "_" ;
digit       = "0" .. "9" ;
nonzero     = "1" .. "9" ;
hexdigit    = digit | "a" .. "f" | "A" .. "F" ;
octdigit    = "0" .. "7" ;
bindigit    = "0" | "1" ;
terndigit   = "N" | "0" | "P" ;
sep         = "_" ;                                     (* only between two digits *)

(* separators, skipped between tokens *)
WHITE_SPACE   = " " | TAB | LF ;      (* TAB is U+0009 and LF is U+000A; a CR LF pair reads as LF *)
LINE_COMMENT  = "//" { line_char } ;
BLOCK_COMMENT = "/*" { BLOCK_COMMENT | any_char } "*/" ;   (* comments nest *)
line_char     = (* any Unicode scalar value except a line end *) ;
any_char      = (* any Unicode scalar value *) ;

(* names *)
IDENT       = letter { letter | digit } ;               (* not a KEYWORD; at most 255 bytes *)
KEYWORD     = "as" | "assert" | "break" | "by" | "case" | "catch" | "const" | "continue"
            | "default" | "defer" | "distinct" | "do" | "else" | "enum" | "errdefer" | "error"
            | "expect_fault" | "export" | "fallthrough" | "false" | "for" | "if" | "import"
            | "in" | "in_place" | "inout" | "kernel" | "out" | "over" | "profile" | "reduce"
            | "return" | "schedule" | "secret" | "static_assert" | "struct" | "switch" | "test"
            | "true" | "try" | "type" | "var" | "void" | "where" | "while" ;
INT_TYPE    = "I8" | "I16" | "I32" | "I64" | "I128" | "I256" | "I512" | "I1024"
            | "U8" | "U16" | "U32" | "U64" | "U128" | "U256" | "U512" ;   (* spelled as IDENT *)
FIXED_TYPE  = "Q" nonzero { digit } "." ( "0" | nonzero { digit } ) ;

(* literals *)
INT_LIT     = DEC_INT | HEX_INT | OCT_INT | BIN_INT | TERN_INT ;
DEC_INT     = "0" | nonzero { [ sep ] digit } ;
HEX_INT     = "0x" hexdigit { [ sep ] hexdigit } ;
OCT_INT     = "0o" octdigit { [ sep ] octdigit } ;
BIN_INT     = "0b" bindigit { [ sep ] bindigit } ;
TERN_INT    = "0t" terndigit { [ sep ] terndigit } ;
FRAC_LIT    = DEC_FRAC | HEX_FRAC ;
DEC_FRAC    = DEC_INT "." digit { [ sep ] digit } ;
HEX_FRAC    = HEX_INT "." hexdigit { [ sep ] hexdigit } ;
CHAR_LIT    = "'" ( char_char | escape ) "'" ;
STRING      = '"' { str_char | escape | "{{" | "}}" | hole } '"' ;
char_char   = (* any Unicode scalar value except "'", "\" and a line end *) ;
str_char    = (* any Unicode scalar value except '"', "\", "{", "}" and a line end *) ;
escape      = "\" ( "\" | '"' | "'" | "n" | "r" | "t" | "0"
                  | "x" octdigit hexdigit
                  | "u{" hexdigit [ hexdigit [ hexdigit [ hexdigit [ hexdigit [ hexdigit ] ] ] ] ] "}" ) ;

(* operators and punctuators *)
PUNCT       = "<<%=" | "<<=" | ">>=" | "+%=" | "-%=" | "*%=" | "+|=" | "-|=" | "*|=" | "..="
            | "<<%" | "<<" | ">>" | "+%" | "-%" | "*%" | "+|" | "-|" | "*|"
            | "==" | "!=" | "<=" | ">=" | "&&" | "||" | "++" | "--"
            | "+=" | "-=" | "*=" | "/=" | "%=" | "&=" | "|=" | "^=" | ".."
            | "+" | "-" | "*" | "/" | "%" | "<" | ">" | "&" | "|" | "^" | "~" | "!" | "?"
            | ":" | "=" | "(" | ")" | "[" | "]" | "{" | "}" | "," | ";" | "."
            | "@" | "as%" | "as|" | "as?" ;
```

6. A token that begins with a digit extends over every letter, digit and `_` that follows it and over a following `.` only when a digit follows the point (a hexadecimal digit after `0x`), so `1..5` is `1`, `..`, `5`, and `12ab` is one token that matches no production. The whole token shall match `INT_LIT` or `FRAC_LIT`. A literal's magnitude is within the limit of 5.3 (C1023) [LS-296].
7. A word that matches `IDENT` and is a `KEYWORD` is that keyword. Every keyword is reserved, including `distinct`, `do`, `enum`, `in_place`, `schedule`, `secret`, `type` and `var`, which no production of this edition uses. The words `as%`, `as|` and `as?` are single tokens: `as` followed at once by `%`, `|` or `?` [LS-296].
8. The spellings of `INT_TYPE`, `Bool` and the mode and operation names are identifiers, not keywords. A declaration shall not declare a name that matches `I`, `U`, `Q` or `T` followed only by digits (C1010) [LS-296].
9. Outside comments, character literals and string literals, every character of a source file is ASCII (C1013). A `\x` escape gives a value from 00 to 7F, and a backslash sequence that `escape` does not produce, such as `\xff`, is C1032; a `\u{...}` escape gives a Unicode scalar value, not a surrogate code point and not above U+10FFFF (C1031). A character literal holds exactly one character or escape [LS-296].
10. Fault names, mode names, the operation names `add` and `mul` as the first argument of `fold_checked` (7.3), and the words `round` and `at` are identifiers, matched by their spelling in the positions where the productions name them. A fault name shall be one of the codes of 5.2.3 (C1011) [LS-297].

<a id="grammar-holes"></a>

### A.3 Holes and format specifications

11. <a id="LS-298"></a>A hole is the text from a `{` in a string literal that is not doubled to the next `}`. Holes are allowed in the string literals of a print statement and of the message of an `assert`. The text of a hole is read with the lexical productions of A.2 and these productions [LS-298]:

```ebnf
hole        = "{" hole_expr [ "=" ] [ "!" conversion ] [ ":" spec ] "}" ;
hole_expr   = or_expr ;
conversion  = "bits" | "raw" | "lanes" "(" lane_type ")" | "ratio" ;   (* "ratio" is reserved, 7.5.4 *)
lane_type   = "I8" | "I16" | "I32" | "I64" | "U8" | "U16" | "U32" | "U64" ;
spec        = [ [ fill ] align ] [ sign ] [ "#" ] [ "0" ] [ width ] [ group ]
              [ "." precision ] [ kind ] [ "/" scale ] ;
fill        = (* any Unicode scalar value other than "}" *) ;
align       = "<" | ">" | "^" ;
sign        = "-" | "+" | " " ;
width       = digit { digit } ;
group       = "_" | "," ;
precision   = digit { digit } ;
kind        = "d" | "x" | "X" | "o" | "b" | "t" | "c" ;
scale       = "1" "0" { "0" } ;
```

12. The `hole_expr` of a hole contains no string literal, and a conditional expression in it is in parentheses (C1035). Which conversions and specifications apply to which types is in 7.5.3 and 7.5.4, which diagnose the others with C1037, a signed `lane_type` among them [LS-298].

<a id="grammar-modules-and-declarations"></a>

### A.4 Modules and declarations

13. <a id="LS-299"></a>The productions of modules and declarations are these [LS-299]:

```ebnf
module        = [ profile_decl ] { import_decl } { top_item } ;
profile_decl  = "profile" STRING ";" ;                  (* the STRING is "cint-core-1" *)
import_decl   = "import" module_path
                [ "as" IDENT | "." "{" IDENT { "," IDENT } [ "," ] "}" ] ";" ;
module_path   = IDENT { "." IDENT } ;

top_item      = [ "export" ] declaration
              | test_decl
              | static_assert
              | statement ;

declaration   = const_decl | var_decl | func_decl | kernel_decl
              | struct_decl | error_decl | memory_decl
              | extern_decl | requires_decl ;
const_decl    = "const" type IDENT "=" expr ";" ;
var_decl      = type IDENT [ "=" expr ] ";" ;
memory_decl   = ( "Arena" | "Pool" ) "(" memory_elem "," expr ")" IDENT ";" ;

func_decl     = result_type IDENT [ size_params ] "(" [ params ] ")" block ;
result_type   = "void" | base_type | union_type ;
union_type    = qualified "!" ( "void" | base_type ) ;
size_params   = "[" IDENT { "," IDENT } "]" ;
params        = param { "," param } [ "," ] ;
param         = [ "in" | "inout" ] type IDENT ;

kernel_decl   = "kernel" IDENT size_params "(" [ kparam { "," kparam } [ "," ] ] ")"
                [ over_clause ] [ where_clause ] block ;
kparam        = ( "in" | "out" | "inout" ) type IDENT ;
over_clause   = "over" "[" IDENT ":" shape_expr { "," IDENT ":" shape_expr } "]" ;
where_clause  = "where" constraint { "," constraint } ;
constraint    = shape_expr ( "==" | "<" | "<=" | ">" | ">=" ) shape_expr ;
shape_expr    = IDENT | INT_LIT | IDENT ( "+" | "-" ) INT_LIT ;

struct_decl   = "struct" IDENT "{" { field } "}" ;
field         = type IDENT ";" ;
error_decl    = "error" IDENT [ ":" INT_TYPE ]
                ( "{" IDENT { "," IDENT } [ "," ] "}"
                | "=" qualified { "|" qualified } ";" ) ;

extern_decl   = [ attribute ] "extern" effect_class result_type IDENT
                "(" [ extern_params ] ")" ";" ;
effect_class  = "pure" | "observe" | "unrestricted" ;
extern_params = extern_param { "," extern_param } [ "," ] ;
extern_param  = [ "in" | "out" | "inout" ] type IDENT ;
attribute     = "@service" "(" STRING [ "," capability ] ")" ;
capability    = qualified [ "(" cap_kind { "," cap_kind } ")" ] ;
cap_kind      = "path" | "name" ;
requires_decl = "requires" qualified [ "(" [ expr { "," expr } ] ")" ] ";" ;

test_decl     = "test" STRING [ "expect_fault" IDENT [ "at" DEC_INT ] ] block ;
static_assert = "static_assert" "(" expr [ "," STRING ] ")" ";" ;
```

14. A `profile_decl`, when present, is the first item of the file, and every `import_decl` precedes every `top_item` (C3012). `export` does not precede a `test_decl`, a `static_assert` or a `requires_decl`. The `STRING` of a `profile_decl`, a `test_decl` or a `static_assert` contains no hole (C1033) [LS-299].
15. A `func_decl`, a `kernel_decl`, a `struct_decl`, an `error_decl`, a `memory_decl`, an `extern_decl`, a `requires_decl` and a `test_decl` appear only as top items, never inside a block. A capacity is written only in a `memory_decl` [LS-299].

<a id="grammar-types"></a>

### A.5 Types

16. <a id="LS-300"></a>The productions of types are these [LS-300]:

```ebnf
type          = base_type [ shape ] ;
base_type     = INT_TYPE | "Bool" | FIXED_TYPE | "Grants" | memory_type | qualified ;
memory_type   = ( "Arena" | "Pool" ) "(" memory_elem ")"
              | "Handle" "(" memory_elem [ "[" [ "_" ] "]" ] ")" ;
memory_elem   = INT_TYPE | "Bool" | qualified ;         (* an integer, Bool or struct type *)
qualified     = IDENT [ "." IDENT ] ;                  (* a struct or error-set name *)
shape         = "[" [ extent { "," extent } ] "]" ;
extent        = "_" | expr ;
```

17. `[]` is `[_]`. A shape has one to four extents, and an array element is not itself an array. A `qualified` name has at most one module prefix [LS-300].

<a id="grammar-statements"></a>

### A.6 Statements

18. <a id="LS-301"></a>The productions of statements are these [LS-301]:

```ebnf
block         = "{" { statement } "}" ;
statement     = block
              | const_decl
              | var_decl
              | view_decl
              | simple_stmt ";"
              | discard
              | print_stmt
              | if_stmt
              | [ IDENT ":" ] loop_stmt
              | switch_stmt
              | jump_stmt
              | defer_stmt
              | assert_stmt
              | static_assert
              | reduce_stmt ;

view_decl     = ( "in" | "inout" ) type IDENT "=" expr ";" ;
simple_stmt   = place assign_op expr
              | place ( "++" | "--" )
              | [ "try" ] call ;
place         = IDENT { "." IDENT | "[" index_list "]" } ;
assign_op     = "=" | "+=" | "-=" | "*=" | "/=" | "%=" | "<<=" | ">>=" | "&=" | "|=" | "^="
              | "+%=" | "-%=" | "*%=" | "<<%=" | "+|=" | "-|=" | "*|=" ;
discard       = "_" "=" expr ";" ;

print_stmt    = STRING { STRING } ";" ;
if_stmt       = "if" "(" expr ")" block [ "else" ( if_stmt | block ) ] ;
loop_stmt     = while_stmt | for_stmt ;
while_stmt    = "while" "(" expr ")" block ;
for_stmt      = "for" IDENT "in" range block
              | "for" "(" for_init ";" expr ";" simple_stmt ")" block ;
range         = expr ( ".." | "..=" ) expr [ "by" expr ] ;
for_init      = type IDENT "=" expr | simple_stmt ;
switch_stmt   = "switch" "(" expr ")" "{" { case_clause } [ default_clause ] "}" ;
case_clause   = "case" case_item { "," case_item } ":" statement { statement } ;
case_item     = or_expr [ "..=" or_expr ] ;
default_clause = "default" ":" statement { statement } ;
jump_stmt     = ( "break" | "continue" ) [ IDENT ] ";"
              | return_stmt
              | "fallthrough" ";" ;
return_stmt   = "return" [ expr ] ";" ;
defer_stmt    = ( "defer" | "errdefer" ) ( block | simple_stmt ";" | discard ) ;
assert_stmt   = "assert" "(" expr [ "," STRING { STRING } ] ")" ";" ;
reduce_stmt   = "reduce" IDENT "=" reduction ";" ;
reduction     = ( "sum" | "sum_wrap" | "sum_sat" | "min" | "max" | "count" ) "(" expr ")"
              | "fold_checked" "(" "add" "," expr "," expr ")" ;
```

19. A `view_decl` is a statement of a block, not a top item. A `reduce_stmt` appears only in the body of a kernel. A label names only a `while` or `for` statement [LS-301].

<a id="grammar-expressions"></a>

### A.7 Expressions

20. <a id="LS-302"></a>The productions of expressions are these [LS-302]:

```ebnf
expr          = cond_expr [ "catch" ( "(" IDENT ")" block | cond_expr ) ] ;
cond_expr     = or_expr [ "?" expr ":" cond_expr ] ;
or_expr       = and_expr { "||" and_expr } ;
and_expr      = cmp_expr { "&&" cmp_expr } ;
cmp_expr      = bor_expr [ cmp_op bor_expr ] ;
cmp_op        = "==" | "!=" | "<" | "<=" | ">" | ">=" ;
bor_expr      = bxor_expr { "|" bxor_expr } ;
bxor_expr     = band_expr { "^" band_expr } ;
band_expr     = shift_expr { "&" shift_expr } ;
shift_expr    = add_expr { ( "<<" | ">>" | "<<%" ) add_expr } ;
add_expr      = mul_expr { ( "+" | "-" | "+%" | "-%" | "+|" | "-|" ) mul_expr } ;
mul_expr      = conv_expr { ( "*" | "/" | "%" | "*%" | "*|" ) conv_expr } ;
conv_expr     = unary_expr { conv_op type [ round_clause ] } ;
conv_op       = "as" | "as%" | "as?" ;
round_clause  = "round" IDENT ;                          (* IDENT is a mode name *)
unary_expr    = neg_literal
              | ( "-" | "-%" | "~" | "!" | "try" ) unary_expr
              | postfix_expr ;
neg_literal   = "-" ( INT_LIT | CHAR_LIT | FRAC_LIT [ round_clause ] ) ;
postfix_expr  = ( call | primary ) { "[" index_list "]" | "." IDENT } ;
call          = IDENT [ "." IDENT ] "(" [ args ] ")" ;
args          = arg { "," arg } [ "," ] ;
arg           = [ IDENT "=" ] expr ;
index_list    = index { "," index } ;
index         = expr
              | [ expr ] ".." [ expr ] [ "by" expr ]
              | [ expr ] "..=" expr [ "by" expr ] ;

primary       = literal
              | STRING { STRING }                        (* an argument for an `in U8[_]` parameter *)
              | IDENT
              | ( INT_TYPE | "Bool" | FIXED_TYPE ) [ "." IDENT ]
              | FIXED_TYPE "." "raw" "(" [ args ] ")"
              | "(" expr ")"
              | array_literal
              | "." IDENT ;
literal       = INT_LIT | CHAR_LIT | FRAC_LIT [ round_clause ] | bool_literal ;
bool_literal  = "false" | "true" ;
array_literal = "[" [ expr { "," expr } [ "," ] ] "]" ;
```

21. A `neg_literal` is formed whenever a `-` in the position of an operand is followed by an `INT_LIT`, `CHAR_LIT` or `FRAC_LIT` token, whatever white space or comments lie between them: `- 5` is the literal -5. A binary `-` is not affected [LS-302].
22. A `call` names its callee directly: a function, a built-in operation, a struct (a constructor), a kernel (a dispatch), after a module name a function, kernel or struct of that module or after the name of an arena or a pool one of its members (paragraph 28). An expression in parentheses, an indexed expression or the result of a call is not called [LS-302].
23. A `STRING` used as a value is an argument for a parameter of type `in U8[_]`, which receives its bytes. Its adjacent literals are one string, and it contains no hole (C1033) [LS-302].
24. `( INT_TYPE | "Bool" | FIXED_TYPE ) "." IDENT` names a property of the type, such as `I64.max`; the type alone is a type argument (A.8, paragraph 30) [LS-302].

<a id="grammar-disambiguation"></a>

### A.8 Disambiguation and diagnosed forms

25. <a id="LS-303"></a>A statement that begins with a type followed by an identifier, or with `const`, `in` or `inout`, is a declaration. At module level, a result type, an identifier, then `(` or `[` begin a function declaration, and a `const_decl` or a `var_decl` is a `declaration`, not a `statement`, so it does not make the module a script (5.1.2). A `_` followed by `=` begins a `discard`. Any other statement that does not begin with a keyword or a string literal is a simple statement [LS-303].
26. <a id="LS-304"></a>In a `case_item`, a conditional expression is in parentheses, so the `:` that ends the case is never read as part of it; `case_item` is an `or_expr` for this reason [LS-304].
27. <a id="LS-305"></a>In `for x in a..b`, the `range` belongs to the `for` statement: `in` is never an expression operator, and `..` and `..=` appear only in an `index`, a `range` and a `case_item`, never as expression operators [LS-305].
28. <a id="LS-306"></a>`a.f(args)`, where `a` is a name, is a `call` of `f` qualified by `a`. When `a` names an imported module, `f` is a function, kernel or struct of that module. When `a` names an arena, `f` is one of its members `alloc`, `alloc_result`, `child` and `reset`; when `a` names a pool, `f` is one of `insert`, `insert_result` and `remove` (6.14). A program does not declare members [LS-306].
29. `a.f` without an argument list is a field of the struct `a`, the storage integer `a.raw` of a fixed-point value, or a name, constant or struct of the module `a` [LS-306].
30. In an argument list, an argument that is a type name and not a value is a type argument; only the built-in operations take type arguments (7.1). The mode and operation positions of the built-in operations take a bare identifier (7.1) [LS-307].
31. These forms are outside the grammar, and the reference diagnoses each with the code shown; 5.1.3 gives the position of each [LS-295]:

| Form | Code | Subclause |
|---|---|---|
| `;` where a statement or a body is expected | C1040 | 6.10.1 |
| `=` as the top operator of a condition of `if`, `while` or a C-form `for` | C1041 | 6.10.2 |
| `++` or `--` inside an expression | C1042 | 6.7.14 |
| a condition of `if` or `while` without parentheses, or a body of `if`, `while` or `for` without braces | C1043 | 6.10.2 |
| a shape after the capacity of a `memory_decl`, as in `Arena(I64, 4)[2] a;` | C2051 | 6.14.1 |
| an assignment whose left side is not a `place` | C2058 | 6.7.14 |
| `&&` with `\|\|`, a bitwise operator with an arithmetic operand or another bitwise operator, a shift with an arithmetic operand or a unary operator before a conversion, without parentheses | C2101 | 6.7.2 |
| a second comparison operator in one `cmp_expr` | C2102 | 6.7.2 |
| an `import_decl` after a `top_item` | C3012 | 6.15.1 |
| `expect_fault` outside the header of a test | C3021 | 6.15.3 |
| an expression statement that is not a call, or a keyword where a name is expected | C4012 | 6.10.1 |
| a `memory_decl` inside a block | C4012 | 6.14.1 |
| `..` in a `case_item` | C4022 | 6.10.5 |
| a `case_clause` with no statement | C4023 | 6.10.5 |
| `default` before a `case_clause`, or twice | C4025 | 6.10.5 |
| `out` on a function parameter | C5001 | 6.9.5 |
| a hole that does not match `hole`, other than in its `spec` | C1035 | 7.5.1 |
| a hole in any other string literal | C1033 | 7.5.1 |
| a single `}` in a string literal | C1039 | 6.4.8 |
| a `spec` that does not match the production `spec` | C1037 | 7.5.3 |
| blocks or expressions nested deeper than the implementation allows | C9004 | 5.3 |

<a id="codes-and-operation-identifiers"></a>

## Annex B (informative) Codes and operation identifiers

This annex collects the codes that translation and execution report: the diagnostic codes of translation (B.1), the fault codes of execution (B.2) and the operation identifiers that name the operation in a fault record (B.3). Each diagnostic code and fault code is defined in the subclause that the tables name. B.3 is the exception: it states the form and the spelling of operation identifiers, and its paragraphs that carry clause identifiers are normative.

<a id="diagnostic-codes"></a>

### B.1 Diagnostic codes

1. A diagnostic code is `C` followed by four digits, and its first digit gives its category, as the table of 5.1.3, paragraph 1 shows [LS-283].

2. The table lists every diagnostic code, with its meaning and the subclause that defines it; that subclause gives its position. Five codes, C5051, C6002, C9001, C9002 and C9102, are reported by cint and not by the reference. C9102 is both a refusal and the code of some constraints (4, paragraph 4). cint refuses some constructs with C9102 before it reaches their other rules, so it does not report every code of the table [LS-313].

| Code | Meaning | Subclause |
|---|---|---|
| C1001 | a source file named with the extension `.cint` | 5.1.1 |
| C1002 | a byte-order mark, invalid UTF-8 or the byte 0x00 | 6.4.1 |
| C1003 | a forbidden invisible or bidirectional character | 6.4.1 |
| C1004 | a control character, or a CR not followed by LF | 6.4.1, 6.4.2 |
| C1005 | an unterminated block comment | 6.4.3 |
| C1010 | a declared name that is `I`, `U`, `Q` or `T` followed only by digits | 6.4.4 |
| C1011 | an unknown fault name after `expect_fault` | 6.4.4 |
| C1012 | an identifier over 255 bytes | 6.4.4 |
| C1013 | a non-ASCII character outside a literal or a comment | 6.4.1 |
| C1020 | an uppercase radix prefix, such as `0X` | 6.4.5 |
| C1021 | a misplaced digit separator `_` | 6.4.5 |
| C1022 | a decimal literal of two or more digits that begins with `0` | 6.4.5 |
| C1023 | a literal over 4,096 bits of magnitude | 6.4.5 |
| C1024 | a radix prefix or a point with no digit after it, or a digit outside the radix | 6.4.5 |
| C1030 | a character literal that is empty or holds more than one character | 6.4.7 |
| C1031 | an escape that is not a Unicode scalar value | 6.4.7, 6.4.8 |
| C1032 | an unknown escape, or `\x` above 7F | 6.4.7, 6.4.8 |
| C1033 | a hole outside a print statement or an `assert` message | 6.4.8, 7.5.1 |
| C1034 | a line break in a string literal | 6.4.8 |
| C1035 | an invalid hole: empty, unterminated, holding a string literal or an unparenthesized conditional expression, or naming an unknown conversion | 7.5.1 |
| C1036 | a precision together with a scale | 7.5.3 |
| C1037 | a format specification that does not match the syntax or does not apply to the value | 7.5.3, 7.5.4 |
| C1038 | an unterminated string or character literal | 6.4.7, 6.4.8 |
| C1039 | a single `}` in a string literal | 6.4.8 |
| C1040 | an empty statement | 6.10.1 |
| C1041 | an assignment in a condition | 6.10.2 |
| C1042 | `++` or `--` used as an expression | 6.7.14 |
| C1043 | a condition without parentheses, or a body without braces | 6.10.2 to 6.10.4 |
| C1050 | a syntax error: an expected token or construct is missing | Annex A |
| C2001 | a type mismatch | 6.5, 6.7 |
| C2002 | a condition, or an operand of `!`, that is not `Bool` | 6.3.3, 6.10 |
| C2003 | a literal or constant that does not fit its type | 6.4.5, 6.5 |
| C2004 | a fraction literal given an integer type | 6.4.6 |
| C2005 | a fixed-point type spelling that names no format | 6.3.2 |
| C2006 | a fraction literal off its type's grid without a `round` clause | 6.5 |
| C2007 | an error value `.name` without an error-set context | 6.5 |
| C2008 | a conversion between two types that have none | 6.6 |
| C2009 | a `round` clause on a conversion that does not round | 6.6, 6.7.15 |
| C2010 | a partial index indexed again, where `m[i, j]` is meant | 6.12.2 |
| C2011 | an array literal with the wrong number of elements | 6.12.1 |
| C2012 | extents, known during translation, that differ | 6.12.5, 7.4 |
| C2013 | arrays compared with a comparison operator | 6.12.1 |
| C2014 | `len` of an array of rank 2 or more | 7.4 |
| C2015 | a negative constant extent | 6.12.1 |
| C2020 | a struct field not supplied | 6.9.6 |
| C2022 | the wrong number of arguments or fields | 6.9.5, 7.1 |
| C2031 | an error set with a signed underlying type, or with more values than its underlying type holds | 6.9.7 |
| C2040 | an error union not consumed by `try`, `catch` or `_ =` | 6.11 |
| C2042 | an error value that the function's error set does not hold | 6.11 |
| C2043 | `try` applied to a value that is not an error union | 6.11 |
| C2044 | an error value name held by several member sets of a combined set | 6.3.7 |
| C2050 | a scalar variable without an initializer | 6.9.1 |
| C2051 | an arena or pool stored in a struct field or an array element | 6.3.6, 6.14 |
| C2052 | an assignment to a loop variable | 6.10.4 |
| C2053 | a step `by` that is not a nonzero constant | 6.10.4, 6.12.3 |
| C2054 | a declaration without an initializer that would fill an error-set value with zero | 6.3.7 |
| C2055 | a struct that contains itself | 6.9.6 |
| C2056 | an integer used as `Bool` | 6.6 |
| C2058 | an assignment to a constant or to something that is not a place | 6.7.14 |
| C2060 | an assignment to a parameter | 6.9.5 |
| C2062 | a parameter given more than one argument | 6.9.5 |
| C2063 | an unknown named argument, or a named argument to a built-in operation | 6.9.5, 7.1 |
| C2064 | an `inout` parameter of a scalar type, or an `out` or `inout` scalar parameter of an extern | 6.9.5, 6.9.8 |
| C2065 | a size parameter bound by no array or view parameter | 6.9.4, 6.13.1 |
| C2066 | a `return` value missing or not allowed | 6.10.8 |
| C2067 | an argument or destination that is not writable | 6.9.5, 7.4 |
| C2069 | a positional argument after a named one | 6.9.5 |
| C2101 | parentheses required | 6.7.2 |
| C2102 | a chained comparison | 6.7.2 |
| C2103 | an operator or built-in operation not defined for the operand type | 6.7, clause 7 |
| C3001 | a duplicate name, a built-in name included | 6.2.3, 7.1 |
| C3002 | a selected import name that collides with another name | 6.15.1 |
| C3003 | an import cycle | 6.15.1 |
| C3004 | a script local used in a function or test | 5.1.2 |
| C3005 | an undefined name or member | 6.2.3 |
| C3006 | an unknown type | 6.3 |
| C3007 | a name of the wrong kind: a type where a value is needed, or a value where a type, mode or operation is needed | 7.1 |
| C3008 | a name that its module does not export | 6.15.2 |
| C3009 | a module file that cannot be read | 6.15.1 |
| C3010 | an import of a script | 5.1.2 |
| C3011 | a script that defines `main` | 5.1.2 |
| C3012 | `import` after other items | 6.15.1 |
| C3020 | a duplicate test name | 6.15.3 |
| C3021 | `expect_fault` outside the header of a test | 6.15.3 |
| C3030 | a module path that does not form a valid file path | 6.15.1 |
| C3040 | a call of a service extern whose capability no `requires` names | 6.9.9 |
| C3041 | a `requires` of an unknown capability, or with arguments that do not fit it | 6.9.9 |
| C3042 | `Grants` as the type of anything but a parameter | 6.9.9 |
| C3043 | `@service` on a declaration other than an extern, or a service extern without a `denied` result | 6.9.9 |
| C3044 | a parameter list of `main` other than `()`, `(Grants g)`, `(in U8[_] args)` or `(Grants g, in U8[_] args)` | 5.1.2 |
| C4001 | a function that can reach its end without returning a value | 6.9.4 |
| C4002 | a `catch` block that can complete | 6.11 |
| C4010 | a function, extern or kernel named without a call | 6.9.4, 6.9.8 |
| C4011 | a result not used | 6.9.4 |
| C4012 | a statement form not permitted, including a keyword where a name is expected | 6.10.1 |
| C4013 | `break` or `continue` outside a loop, or with an unknown label | 6.10.6 |
| C4014 | a misplaced `fallthrough` | 6.10.5 |
| C4020 | a `switch` that does not cover every value | 6.10.5 |
| C4021 | overlapping case items | 6.10.5 |
| C4022 | a case range written with `..` | 6.10.5 |
| C4023 | a case clause with no statement | 6.10.5 |
| C4024 | an empty case range | 6.10.5 |
| C4025 | `default` not last, or repeated | 6.10.5 |
| C4026 | a scrutinee of a type that `switch` does not take | 6.10.5 |
| C4030 | a jump or `try` that would leave a deferred statement | 6.10.7 |
| C4031 | an `errdefer` where no error can leave | 6.10.7 |
| C5001 | `out` on a function parameter | 6.9.5 |
| C5010 | arguments, or the operands of a copy, known during translation to overlap | 6.9.5, 6.12.5 |
| C5012 | an argument that the function also reaches as a module-level variable | 6.9.5 |
| C5020 | a print statement in a kernel | 6.13.3 |
| C5021 | `defer` in a kernel | 6.13.3 |
| C5022 | `try` or an error union in a kernel | 6.13.3 |
| C5023 | a dispatch in a kernel | 6.13.3 |
| C5025 | a module-level variable in a kernel | 6.13.3 |
| C5028 | a kernel parameter of a type that kernels do not take | 6.13.1 |
| C5031 | a kernel write outside its work-item's block, or writes that do not cover it | 6.13.3 |
| C5050 | a parameter or result type of an extern that does not cross to the host | 6.9.8 |
| C5051 | two exported names that give one symbol in generated code, or a name that gives no valid symbol (cint only) | SPEC-09P.2 |
| C6001 | a fault during constant evaluation | 6.8 |
| C6002 | compile-time fuel exhausted (cint only) | 6.8 |
| C6004 | a constant expression required | 6.8 |
| C6005 | a constant that depends on itself | 6.9.2 |
| C6006 | constant arguments that break a built-in precondition: `clamp` with `lo > hi`, or a negative arena or pool capacity | 6.14, 7.2.1 |
| C6010 | a `static_assert` that fails | 6.10.9 |
| C9001 | a capacity or output size of the implementation exceeded (cint only) | 5.3 |
| C9002 | a generated symbol over 247 bytes (cint only) | SPEC-09P.2 |
| C9004 | blocks or expressions nested beyond the implementation's limit | 5.3 |
| C9102 | a construct that the implementation does not implement (a refusal, 3.16), or a constraint that names no code of its own (4, paragraph 4) (cint only) | 4 |

<a id="fault-codes"></a>

### B.2 Fault codes

3. A fault code names the kind of fault. Its number is the code's value in the canonical encodings of SPEC-01P.3. The table of 5.2.3, paragraph 1 lists every fault code with its number and the operations that raise it.

<a id="operation-identifiers"></a>

### B.3 Operation identifiers

4. <a id="IM-130"></a>The operation of a fault record, and the text after the caret in a fault report, is an operation identifier of this form. Every identifier is lowercase ASCII [IM-130]:

```ebnf
op_id      = name "." form "." op_type { "." op_type } [ "." mode ] | system_id ;
form       = "checked" | "wrap" | "sat" | "result" | "add" | "mul" ;
op_type    = "i8" | "i16" | "i32" | "i64" | "i128" | "i256" | "i512" | "i1024"
           | "u8" | "u16" | "u32" | "u64" | "u128" | "u256" | "u512" | "bool"
           | "q" digit { digit } "_" digit { digit } ;      (* Q16.16 is q16_16 *)
name       = (* the name of the operation, as in the table of paragraph 9 *) ;
mode       = (* a rounding mode name, 6.7.15 *) ;
system_id  = "fuel.charge" | "call.enter"
           | "index.checked." ( op_type | "struct" ) | "slice.checked." ( op_type | "struct" )
           | "copy.shape" | "copy.alias" | "decl.shape"
           | "assert.checked.bool" | "format.char." op_type | "unassigned"
           | "bind." check_name
           | "arena.alloc" | "arena.reset" | "pool.deref" | "pool.insert" | "handle.check" ;
check_name = "alias" | "injective" | "limit" | "permission" | "placement"
           | "shape" | "stale" | "type" | "where" ;
```

5. <a id="IM-131"></a>`form` is the form of the operator: `checked` for a checked operation, `wrap` and `sat` for the wrapping and saturating forms, and `result` for the result forms. `add` and `mul` occur only as the operation folded by `fold_checked` [IM-131].
6. The first `op_type` is the operand type. A second `op_type` is present when the result type differs from the operand type: for a conversion, the source and then the target. `sum`, `dot` and `muldiv` always write the operand or element type and then the result type, even when the two are equal; `qmul` writes the types of its two operands and then of its result, and `mul_int` the fixed-point type and then the integer type [IM-132].
7. `mode` is present exactly when the operation rounds, whether the mode was written or is the default. It names the mode the operation used [IM-133].
8. A record holds the identifier of an operation that faulted. The wrapping, saturating and result forms of arithmetic never fault, so `sat` and `result` appear in no record, and `wrap` only in `shl.wrap.t`, the form of `<<%`, whose count is checked. Operations that cannot fault, such as the two-operand `min` and `max` (7.2.1), `uabs`, `mul_full`, `sum_wrap`, `sum_sat` and `count`, likewise appear in no record [IM-131].
9. <a id="IM-134"></a>The identifiers of the operations that can fault are these [IM-134]. In the table, `t` is the operand type, `s` and `t` the source and target of a conversion, `e` the element type, `r` the result type, `q`, `qa` and `qb` fixed-point types and `n` an integer type, each spelled as `op_type`:

| Source form | Identifier | Faults | Example |
|---|---|---|---|
| `a + b`, `a - b`, `a * b` (integer); `+`, `-` (fixed point) | `add.checked.t`, `sub.checked.t`, `mul.checked.t` | `E_OVERFLOW` | `add.checked.i64` |
| `-a` | `neg.checked.t` | `E_OVERFLOW` | `neg.checked.i64` |
| `a / b`, `a % b` (integer) | `div.checked.t`, `rem.checked.t` | `E_DIV_ZERO`, `E_OVERFLOW` | `div.checked.i64` |
| `div_trunc`, `rem_trunc`, `div_euclid`, `rem_euclid` | `name.checked.t` | `E_DIV_ZERO`, `E_OVERFLOW` | `rem_euclid.checked.i64` |
| `div_round(a, b, mode)` | `div_round.checked.t.mode` | `E_DIV_ZERO`, `E_OVERFLOW` | `div_round.checked.i64.half_even` |
| `a << k`, `a <<% k`, `a >> k` | `shl.checked.t`, `shl.wrap.t`, `shr.checked.t` | `E_OVERFLOW`, `E_SHIFT` | `shl.wrap.u8` |
| `rotl`, `rotr` | `rotl.checked.t`, `rotr.checked.t` | `E_SHIFT` | `rotl.checked.u32` |
| `abs` | `abs.checked.t` | `E_OVERFLOW` | `abs.checked.i8` |
| `clamp(x, lo, hi)` | `clamp.checked.t` | `E_DOMAIN` | `clamp.checked.i64` |
| `x as T` | `as.checked.s.t[.mode]` | `E_NARROW` | `as.checked.q16_16.i8.half_even` |
| `muldiv(R, a, b, c, mode)` | `muldiv.checked.t.r.mode` | `E_DIV_ZERO`, `E_OVERFLOW` | `muldiv.checked.i64.i64.floor` |
| `isqrt`, `isqrt_round` | `isqrt.checked.t`, `isqrt_round.checked.t.mode` | `E_DOMAIN`, `E_OVERFLOW` | `isqrt_round.checked.i128.half_even` |
| fixed-point `*`, `mul` | `mul.checked.q.mode` | `E_OVERFLOW` | `mul.checked.q16_16.half_even` |
| fixed-point `/`, `div` | `div.checked.q.mode` | `E_DIV_ZERO`, `E_OVERFLOW` | `div.checked.q16_16.floor` |
| fixed-point `sqrt` | `sqrt.checked.q.mode` | `E_DOMAIN`, `E_OVERFLOW` | `sqrt.checked.q16_16.half_even` |
| `qmul(R, a, b[, mode])` | `qmul.checked.qa.qb.r[.mode]` | `E_OVERFLOW` | `qmul.checked.q16_16.q1_15.q16_16.half_even` |
| `mul_int(a, n)` | `mul_int.checked.q.n` | `E_OVERFLOW` | `mul_int.checked.q16_16.i64` |
| `sum(R, xs)` | `sum.checked.e.r` | `E_OVERFLOW` | `sum.checked.i8.i16` |
| `dot(R, a, b)` | `dot.checked.e.r`, and for fixed point `dot.checked.q.r[.mode]` | `E_SHAPE`, `E_OVERFLOW` | `dot.checked.i8.i64` |
| `fold_checked(op, init, xs)` | `fold_checked.add.e`, `fold_checked.mul.e` | `E_OVERFLOW` | `fold_checked.add.i8` |
| `min(xs)`, `max(xs)` | `reduce_min.checked.e`, `reduce_max.checked.e` | `E_SHAPE` | `reduce_min.checked.i64` |
| a fuel charge | `fuel.charge` | `E_FUEL` | |
| a call | `call.enter` | `E_DEPTH`, `E_UNSUPPORTED` | |
| an element read or written through an index | `index.checked.e`, with `struct` for a struct element | `E_BOUNDS` | `index.checked.i32` |
| a slice | `slice.checked.e`, with `struct` for a struct element | `E_BOUNDS` | `slice.checked.i64` |
| an array assignment, an array initializer, a constructor's array field or `copy` | `copy.shape`, `copy.alias` | `E_SHAPE`, `E_ALIAS` | `copy.shape` |
| an array declaration whose extent is negative | `decl.shape` | `E_SHAPE` | |
| `assert(cond)` outside a test | `assert.checked.bool` | `E_ASSERT` | |
| a hole of kind `c` | `format.char.t` | `E_NARROW` | `format.char.i64` |
| a checked conversion of a literal during translation | `unassigned` | `E_NARROW`, in C6001 | |
| the checks of a call or dispatch on its arguments | `bind.shape`, `bind.alias`, `bind.where` | `E_SHAPE`, `E_ALIAS` | `bind.alias` |
| arena and pool operations | `arena.alloc`, `arena.reset`, `pool.deref`, `pool.insert`, `handle.check` | `E_BOUNDS`, `E_STALE_HANDLE` | `pool.deref` |

10. <a id="IM-135"></a>The table fixes the spelling of each identifier. The subclause that defines the operation states its fault, its operands, its exact result and its limit [IM-135].
11. The table lists the identifiers that a cint program can produce. The other `bind.` identifiers name checks on the arguments that a host program passes to an entry or a kernel, and SPEC-03P.2 defines them. An executor whose entry checks of a dispatch fail refuses it with one of the `bind.` checks that SPEC-02P.2 lists [IM-130].

<a id="defined-behavior-and-deliberate-absences"></a>

## Annex C (informative) Defined behavior and deliberate absences

This annex is for a reader who knows C. C leaves many behaviors undefined, unspecified or to the implementation, and a C program that meets one of them can do different things on different compilers, or on the same compiler with different options. Clause 4, paragraph 6, states that within the scope of this document cint has no such behavior. C.1 lists, kind by kind, what cint does where C leaves the outcome open, and where C defines a behavior that cint defines differently. C.2 lists the features of C and of other languages that cint leaves out, each with its reason and what a program writes instead.

<a id="behavior-that-c-leaves-open"></a>

### C.1 Behavior that C leaves open

1. <a id="IM-128"></a>Every operation of this document has a defined result or a defined fault for every input, and every program that is accepted has exactly one outcome for each set of semantic inputs (clause 4, paragraph 2). This claim is about the language. The claim that the C an implementation emits evaluates no C operation whose behavior C leaves undefined, and the assumptions that claim rests on, are stated in SPEC-09P.2; this annex adds nothing to that scope [IM-128].
2. The tables below follow the lists of Annex J of the C standard, ISO/IEC 9899:2018 (C17). The first column names the C17 subclause that sets the behavior. The last column names the subclause of this document that defines what cint does.

| C17 | Undefined in C | In cint | See |
|---|---|---|---|
| 6.5 | A signed arithmetic result outside the range of its type | Every plain operator is checked: the operation faults `E_OVERFLOW`. The wrapping operators `+%`, `-%` and `*%` and the saturating operators `+\|`, `-\|` and `*\|`, give a defined result where the program writes them | 6.7.6, 6.7.7 |
| 6.5.5 | Division or remainder by zero | `/` and `%` fault `E_DIV_ZERO` | 6.7.7 |
| 6.5.5 | `INT_MIN / -1` and `INT_MIN % -1` | `MIN(T) / -1` faults `E_OVERFLOW`; `MIN(T) % -1` is 0 | 6.7.7 |
| 6.5.7 | A shift count that is negative or not less than the width | The shift faults `E_SHIFT`. The count is not reduced modulo the width | 6.7.8 |
| 6.5.7 | A left shift of a negative value, or one whose result does not fit | `a << k` is `a * 2^k`, checked like `*`: `-1 << 1` is -2, and a result outside the type faults `E_OVERFLOW` | 6.7.8 |
| 6.5.2.1, 6.5.6 | An array subscript, or pointer arithmetic, outside the array object | There is no pointer arithmetic. Every index and every slice bound is checked against its extent, and one outside it faults `E_BOUNDS` | 6.12.2, 6.12.3 |
| 6.2.4, 6.7.9 | Reading an automatic object that was not initialized | A variable of an integer type, of `Bool` or of an error-set type without an initializer is C2050. An array without one has every element 0 | 6.9.1, 6.12.1 |
| 6.2.4 | Using an object, through a pointer, after its lifetime has ended | There are no pointers. A view cannot be stored in a struct, an array or a module-level variable, so it does not outlive what it refers to. A handle whose storage has been released faults `E_STALE_HANDLE` | 6.3.5, 6.14 |
| 6.5 | Two unsequenced side effects on one object, or a side effect and an unsequenced read of it, as in `i = i++` | Assignment, compound assignment, `++` and `--` are statements, not expressions, and the operands of an expression are evaluated from left to right | 6.7.14, 6.2.4 |
| 6.9.1 | Using the value of a call that reached the closing brace of its function | A function with a result that can reach its closing brace is C4001 | 6.9.4 |
| 6.5, 6.7.3.1 | Reading an object through an lvalue of another type; writing through a `restrict` pointer whose object another pointer also reaches | There are no pointers and no unions. An `inout` argument that overlaps another argument of the same call is C5010 when the overlap is known during translation, and faults `E_ALIAS` otherwise | 6.9.5 |
| 5.1.2.4 | A data race between threads | Source code has no threads that share memory. Each work-item of a kernel writes only its own block of each output, and a dispatch publishes its outputs only when it completes without a fault | 6.13.3, 6.13.4 |
| 7.21.6.1 | A `printf` conversion specification that does not match the type of its argument | A print hole takes the type of its expression, and a format specification that does not apply to that type is C1037 | 6.10.10, 7.5.3 |
| 6.4 | An unmatched `'` or `"` on a source line | An unterminated character or string literal is diagnosed, as is a byte sequence that is not UTF-8 | 6.4.1, 6.4.7, 6.4.8 |

| C17 | Unspecified in C | In cint | See |
|---|---|---|---|
| 6.5, 6.5.2.2 | The order in which the operands of an operator and the arguments of a call are evaluated, and the order of their side effects | From left to right. Each operand is evaluated completely before the next begins, and the first fault in that order is the one reported | 6.2.4 |
| 6.5.16 | Which operand of an assignment is evaluated first | The place is evaluated and checked first, then the value, then the store is made | 6.2.4 |
| 6.7.9 | The order of the side effects of the expressions in an initializer list | The elements of an array literal and the arguments of a struct constructor are evaluated from left to right | 6.2.4 |

| C17 | Implementation-defined in C | In cint | See |
|---|---|---|---|
| 5.2.4.2.1, 6.2.5 | The widths and ranges of `short`, `int`, `long` and `long long` | Every integer type name states its width: `I8` to `I1024` and `U8` to `U512`. The width fixes the range | 6.3.1 |
| 6.2.5 | Whether plain `char` is signed | There is no `char` type. A character literal is an integer that takes its type from its context | 6.4.7, 6.5 |
| 6.2.6.2 | The representation of signed integers, of which C17 permits three | A value is a member of Z, and its meaning does not depend on how it is represented. Arithmetic and conversions are defined on values | 6.2.1, 6.6 |
| 6.3.1.3 | The result of converting a value to a signed type that cannot hold it | `as` faults `E_NARROW`; `as%` wraps the value into the type; `as|` saturates it; `as?` returns an error | 6.6 |
| 6.5.7 | The result of `>>` on a negative value | `a >> k` is `floor(a / 2^k)`: `-7 >> 1` is -4 | 6.7.8 |
| 5.1.1.2, 5.2.1 | How the bytes of a source file map to the source character set | A source file is UTF-8 | 6.4.1 |
| 5.2.4.1 | The translation limits above the minimums | Every implementation accepts the minimums of 5.3, and rejects a program beyond its own limits with a diagnostic | 5.3 |
| 7.11, J.4 | Output that depends on the locale | A program's output bytes depend only on the program and its semantic inputs | 5.2.2, 7.5 |

3. Some behaviors that C defines, cint defines differently. A C programmer meets these first:

| C17 | Defined in C | In cint | See |
|---|---|---|---|
| 6.2.5 | Unsigned arithmetic wraps modulo 2^N | Unsigned arithmetic is checked like signed arithmetic. `+%`, `-%` and `*%` wrap where the program writes them | 6.7.6, 6.7.7 |
| 6.3.1.1, 6.3.1.8 | The integer promotions and the usual arithmetic conversions: `-1 < 1u` is 0 | There are no implicit conversions, except from an error set to a larger one (6.3.7). Operands of two different types are C2001 | 6.6, 6.7.10 |
| 6.5.5 | `/` truncates toward 0: `-7 / 2` is -3 and `-7 % 2` is -1 | `/` rounds toward negative infinity, and a remainder other than 0 has the sign of the divisor: `-7 / 2` is -4 and `-7 % 2` is 1 | 6.7.7 |
| 6.5.10 | `x & 1 == 0` is `x & (1 == 0)` | The bitwise operators bind tighter than the comparisons: `x & 1 == 0` is `(x & 1) == 0` | 6.7.1 |
| 6.8.4.1, 6.8.5 | A condition is any scalar, and a value other than 0 is true | A condition has type `Bool`; an integer there is C2002 | 6.3.3 |
| 6.8.4.2 | Control passes from one `case` into the next unless a `break` stops it | Control leaves the `switch` at the end of a clause, and continues into the next only after `fallthrough;` | 6.10.5 |
| 6.5.16 | An assignment is an expression, so `if (x = 0)` compiles | An assignment is a statement; one written as a condition is C1041 | 6.7.14 |

**Examples**

4. This example shows floor division and the arithmetic right shift. A C compiler prints `-3 -1` for the first two values; the third is implementation-defined in C:

```cint c_division.ci
I64 a = -7;
I64 b = 2;
"{a / b} {a % b} {a >> 1}\n";
```

```text
-4 1 -4
```

5. `I64.min % -1` is 0. `I64.min / -1` has the exact result 2^63, one more than `I64.max`, so it faults:

```cint c_min_div.ci
I64 m = I64.min;
I64 d = -1;
"{m % d}\n";
I64 q = m / d;
"{q}\n";
```

```text
0
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> c_min_div.ci:4:11
   |
 4 | I64 q = m / d;
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

6. A shift count of 64 on `I64` faults. In C the shift is undefined; the x86-64 shift instruction uses the count modulo 64, so a compiler that emits that instruction gives 1, and one that folds the expression may give another value:

```cint c_shift.ci
I64 x = 1;
I64 k = 64;
I64 y = x << k;
"{y}\n";
```

```text
fault[E_SHIFT]: shift count outside 0..w-1
  --> c_shift.ci:3:11
   |
 3 | I64 y = x << k;
   |           ^ shl.checked.i64
   |
   = operation: shl.checked.i64
   = operand-count: 2
   = operand: I64 1
   = operand: I64 64
   = exact:   none
   = limit:   I64 63
   ...
```

7. Unsigned arithmetic is checked. The wrapping operator gives the result that C's `uint8_t` arithmetic gives after conversion back to `uint8_t`:

```cint c_unsigned.ci
U8 a = 200;
U8 b = 100;
U8 w = a +% b;
"{w}\n";
U8 c = a + b;
"{c}\n";
```

```text
44
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> c_unsigned.ci:5:10
   |
 5 | U8 c = a + b;
   |          ^ add.checked.u8
   |
   = operation: add.checked.u8
   = operand-count: 2
   = operand: U8 200
   = operand: U8 100
   = exact:   300
   = limit:   U8 255
   ...
```

8. A conversion to a narrower type that cannot hold the value faults, where C17 leaves the result to the implementation. `as%` keeps the low 8 bits:

```cint c_narrow.ci
I64 big = 300;
I8 low = big as% I8;
"{low}\n";
I8 small = big as I8;
"{small}\n";
```

```text
44
fault[E_NARROW]: the value does not fit the destination type
  --> c_narrow.ci:4:16
   |
 4 | I8 small = big as I8;
   |                ^ as.checked.i64.i8
   |
   = operation: as.checked.i64.i8
   = operand-count: 1
   = operand: I64 300
   = exact:   300
   = limit:   I8 127
   ...
```

9. In C, `-1 < 1u` converts -1 to `UINT_MAX` and is false. cint has no conversion to apply, so the comparison is rejected:

```cint c_promotion.ci
I64 a = -1;
U64 b = 1;
Bool less = a < b;
"{less}\n";
```

```text
error[C2001]: type mismatch
  --> c_promotion.ci:3:17
   |
 3 | Bool less = a < b;
   |                 ^
```

10. A variable without an initializer is rejected, so no read can see an indeterminate value:

```cint c_uninit.ci
void main() {
    I64 total;
    "{total}\n";
}
```

```text
error[C2050]: uninitialized variable
  --> c_uninit.ci:2:9
   |
 2 |     I64 total;
   |         ^
```

11. A function that can reach its closing brace without a `return` is rejected, even when no call reaches it:

```cint c_no_return.ci
I64 ramp(I64 v) {
    if (v > 0) {
        return 1;
    }
}
"{ramp(5)}\n";
```

```text
error[C4001]: missing return
  --> c_no_return.ci:1:5
   |
 1 | I64 ramp(I64 v) {
   |     ^
```

12. A `switch` clause ends at the next `case`. The C reading of this code would print all three lines:

```cint c_switch.ci
I64 x = 4;
switch (x) {
    case 4: { "four\n"; }
    case 5: { "five\n"; }
    default: { "other\n"; }
}
```

```text
four
```

**Notes**

13. C does not address what happens when deep recursion or large local arrays exhaust the stack, and on most hosts the process ends with a signal. In cint each entry has a call-depth limit and a frame arena, and passing either faults at the same operation on every implementation (5.2.5, 5.2.6).
14. The scope of the claim excludes code reached through the host interface, defects in an implementation or in the C compiler that builds its output, hardware faults, and host resources an implementation cannot detect running out (clause 4, paragraph 6).

<a id="deliberate-absences"></a>

### C.2 Deliberate absences

1. <a id="LS-271"></a>The features in the table below are absent from `cint-core-1`, and an implementation shall not add them [LS-271].

| Absent | Reason | Instead |
|---|---|---|
| Floating-point types, literals and operations | Each operation rounds, so results are not exact. In C the result also depends on the evaluation format (`FLT_EVAL_METHOD`), on whether the compiler contracts an expression such as `a * b + c` into one operation with one rounding (C17 6.5), and on the math library | The fixed-point types (6.3.2), the wide integer types (6.3.1) and integers scaled by a unit the program chooses |
| Raw pointers and pointer arithmetic | A pointer can alias anything and carries no bounds | Views, which carry a shape and a permission (6.3.5, 6.12); handles, checked by generation (6.14) |
| Implicit conversions and the integer promotions | They change values without a trace in the text: in C, `-1 < 1u` is false | The conversions `as`, `as%`, `as|` and `as?` (6.6); literals typed by their context (6.5). The one implicit conversion is from an error set to a larger one (6.3.7) |
| The preprocessor and macros | Text substitution hides meaning from tools, and a diagnostic would point into text the programmer did not write | `const` declarations (6.9.2); modules and `import` (6.15.1) |
| Undefined behavior | A program with undefined behavior has no meaning to debug or to compare | Checked operators and faults (5.2.3) |
| Uninitialized variables | A read before the first write has no defined value | Required initializers (6.9.1); arrays with every element 0 (6.12.1) |
| `goto`, `setjmp` and `longjmp` | Control can jump into or out of any block of a function | Labeled `break` and `continue` (6.10.6); `defer` and `errdefer` (6.10.7) |
| Exceptions | Any call could leave the caller, unseen in its text | Error unions with `try` and `catch`, which mark each exit (6.3.7, 6.11); a fault stops the program and is not caught (5.2.3) |
| Implicit `switch` fallthrough | A missing `break` is a known C weakness (CWE-484) | `fallthrough;` (6.10.5) |
| Chained comparisons such as `0 <= i < n` | In C the expression compares the result of `0 <= i` with `n`; in Python it tests both bounds and evaluates `i` once. Each reading is natural to some readers, and neither shows in the text how many times the middle operand runs or which fault comes first | `0 <= i && i < n`, which states each evaluation and its order; the chained form is C2102 (6.7.2) |
| Operator overloading and function overloading | One definition per name keeps the target of every call and every operator visible in the text | Distinct names; a second declaration of a visible name is C3001 (6.2.3) |
| Untagged unions and type punning | Reading the bytes of one type as another has no defined result | `as%`, which keeps the low bits of a value (6.6) |
| Variadic functions | The arguments are not checked against the parameters | Named arguments (6.9.5); views of any extent (6.12.4) |
| Postfix or parenthesis-free call syntax | A parenthesis-free call would read the same as a session command, which programs and sessions keep apart (5.1.2) | Calls with parentheses; the commands of a `cint workbench` session form a separate language (5.1.2) |
| Negative indexing | An index of -1 that reaches the last element hides an off-by-one error | `a[len(a) - 1]` (7.4); an index below 0 faults `E_BOUNDS` (6.12.2) |
| Implicit allocation: string concatenation, growable arrays, hidden temporaries for overlapping copies | Each has a hidden cost and a hidden way to fail | Arrays of fixed extent (6.12.1); arenas and pools of fixed capacity (6.14); a copy whose source and destination overlap faults `E_ALIAS` (6.12.5) |
| Threads that share memory | Two threads can write the same object without an order, and the result depends on scheduling | Kernels, whose work-items write separate blocks of their outputs (6.13) |
| Identifiers outside ASCII | Two names that look the same can differ in their characters, and a reader cannot tell which one a program means | Names of ASCII letters, digits and `_` (6.4.4); text outside ASCII in string literals and comments (6.4.8) |
| `out` parameters in ordinary functions | A parameter that must be written before the caller reads it adds a written or unwritten state to every path through the function | `inout` parameters, whose arguments hold a value before the call (6.9.5); an `out` parameter of a function is C5001, and `out` is for kernels (6.13) |

**Examples**

2. A second function with the name of the first is rejected at the second name, whatever its parameter types:

```cint c_overload.ci
I64 twice(I64 v) {
    return v * 2;
}
I32 twice(I32 v) {
    return v * 2;
}
"{twice(4)}\n";
```

```text
error[C3001]: duplicate name
  --> c_overload.ci:4:5
   |
 4 | I32 twice(I32 v) {
   |     ^
```

3. The last element is reached by its index. An index of -1 is outside the extent:

```cint c_negindex.ci
void main() {
    I64[3] a = [10, 20, 30];
    I64 last = a[len(a) - 1];
    "{last}\n";
    I64 k = -1;
    I64 bad = a[k];
    "{bad}\n";
}
```

```text
30
fault[E_BOUNDS]: index out of bounds
  --> c_negindex.ci:6:16
   |
 6 |     I64 bad = a[k];
   |                ^ index.checked.i64
   |
   = operation: index.checked.i64
   = operand-count: 1
   = operand: I64 -1
   = exact:   none
   = limit:   I64 3
   ...
```

**Notes**

4. Compile-time functions and a string type of variable length are not part of this edition, and they are not listed here (6.16).

<a id="cint-for-c-and-python-programmers"></a>

## Annex D (informative) cint for C and Python programmers

This annex lists the habits of each language that carry over and those that do not, and points each to the clause that holds the rule. Annex C lists the features that cint leaves out and gives the reasons. The Python spellings are those of the Python Language Reference and the Python Standard Library. The C spellings are those of ISO/IEC 9899:2018, except the case range `case 1 ... 5:`, which is a GCC extension.

<a id="for-python-programmers"></a>

### D.1 For Python programmers

1. The habits in this table carry over with the change shown:

| Python habit | cint spelling | Note |
| --- | --- | --- |
| `for i in range(n):` | `for i in 0..n { }` | Half-open, as in Python. `0..=n` includes `n` (6.10.4). |
| `1_000_000` | `1_000_000` | As in PEP 515, except that `_` does not follow a base prefix: `0x_FF` is C1021 (6.4.5). |
| `print(f"x={x}")` | `"x={x}\n";` | A string literal on its own is a print statement. No newline is added (6.10.10). |
| `f"{x=}"` | `"{x=}\n";` | The same debug form (7.5.1). |
| `a // b`, `a % b` | `a / b`, `a % b` | Floor division and its remainder, as in Python (6.7.7). |
| `0 <= i < n` | `0 <= i && i < n` | A chained comparison is C2102 (6.7.2). |
| `a[-1]` | `a[len(a) - 1]` | There are no negative indices: `a[-1]` faults with `E_BOUNDS` (6.12.2). |
| `try: ... except E:` | `f() catch (e) { ... }` | For errors only. A fault is not caught (6.11.4). |
| `with x:`, cleanup | `defer { ... }` | Runs when the block exits, by any path, and not after a fault (6.10.7). |
| `assert x == y` with pytest | `assert(x == y);` inside `test "name" { }` | Both compared values are reported when the assertion fails (6.10.9, 6.15.3). |
| `float` | none | Fixed point, such as `Q32.32` (6.3.2), or scaled integers, printed with a scale such as `{v:/100}` (7.5.3). |

2. A Python `int` has no bound. Each cint integer type has a range, and every plain operator is checked: an operation whose exact result is outside the range faults (6.7). The forms `+%` and `+|` wrap and saturate where that is the intent (6.7.6).

**Examples**

3. This example shows a loop over a half-open range, the debug form of a hole and floor division:

```cint py_loop.ci
I64 total = 0;
for i in 0..5 {
    total += i;
}
"{total=} {-7 / 2} {-7 % 2}\n";
```

```text
total=10 -4 1
```

4. The last element is at `len(a) - 1`. An index of -1 is outside the array, and the fault record shows the index and the array's extent:

```cint py_last.ci
void main() {
    I64[4] a = [10, 20, 30, 40];
    "{a[len(a) - 1]}\n";
    "{a[-1]}\n";
}
```

```text
40
fault[E_BOUNDS]: index out of bounds
  --> py_last.ci:4:8
   |
 4 |     "{a[-1]}\n";
   |        ^ index.checked.i64
   |
   = operation: index.checked.i64
   = operand-count: 1
   = operand: I64 -1
   = exact:   none
   = limit:   I64 4
...
```

5. `defer` does the work of a `with` block or a `finally` clause. The deferred block runs when `step` returns, from the middle or from the end:

```cint py_defer.ci
void step(I64 k) {
    "open {k}\n";
    defer {
        "close {k}\n";
    }
    if (k == 2) {
        return;
    }
    "work {k}\n";
}

for k in 1..=2 {
    step(k);
}
```

```text
open 1
work 1
close 1
open 2
close 2
```

6. An expected failure is an error value, handled at the call:

```cint py_catch.ci
error InputError { empty, too_long }

InputError!I64 check_length(I64 n) {
    if (n == 0) {
        return .empty;
    }
    if (n > 80) {
        return .too_long;
    }
    return n;
}

I64 a = check_length(12) catch 0;
I64 b = check_length(0) catch 0;
"{a} {b}\n";
```

```text
12 0
```

7. This example shows money in cents, held as an integer and printed with a scale:

```cint py_cents.ci
I64 cents = 123456;
I64 fee = cents / 100;
"{cents:/100} {fee:/100}\n";
```

```text
1234.56 12.34
```

8. A fixed-point value is a multiple of a power of two, held exactly. The hole prints that value with every digit:

```cint py_third.ci
Q32.32 third = 1.0 / 3;
"{third}\n";
```

```text
0.33333333325572311878204345703125
```

9. A chained comparison is rejected, because Python and C read it differently:

```cint py_chain.ci
I64 i = 3;
I64 n = 10;
if (0 <= i < n) {
    "inside\n";
}
```

```text
error[C2102]: chained comparison
  --> py_chain.ci:3:5
   |
 3 | if (0 <= i < n) {
   |     ^
```

<a id="for-c-programmers"></a>

### D.2 For C programmers

1. The habits in this table carry over with the change shown:

| C habit | cint spelling | Difference |
| --- | --- | --- |
| `int64_t x = 0;` | `I64 x = 0;` | A variable of an integer, fixed-point, `Bool` or error-set type always has an initializer (6.9.1). |
| `(int32_t)x` | `x as I32` | Checked: `E_NARROW` when the value does not fit. `x as% I32` wraps (6.6). |
| `x + y` with signed overflow | `x + y` | Faults with `E_OVERFLOW`, for signed and unsigned types alike. `x +% y` wraps and `x +| y` saturates (6.7.6). |
| `switch` with fallthrough | `switch` without fallthrough | `fallthrough;` is written where it is meant. An empty case body is C4023 (6.10.5). |
| `case 1 ... 5:` (GCC) | `case 1..=5:` | Case ranges are inclusive. `case 1..5:` is C4022 (6.10.5). |
| `#include`, `#define` | `import name;`, `const` | There is no preprocessor (6.15.1, 6.9.2). |
| `printf("%d\n", x)` | `"{x}\n";` | Holes in a string literal (6.10.10, 7.5). |
| `if (x)` with an integer `x` | `if (x != 0) { ... }` | A condition is `Bool` (C2002), and braces are required (C1043) (6.10.2). |
| `(a & b) == c` | `a & b == c` | Bitwise operators bind more tightly than comparisons, so the parentheses that C needs can be left out (6.7.1). |
| `int *p; p + 1` | views and indices | There are no pointers in source (6.12). |

2. C leaves signed overflow, an index outside an array and a read of an uninitialized variable undefined. cint defines each: the first two fault, and the third cannot be written. Annex C goes through each kind of behavior that C leaves undefined, unspecified or implementation-defined.

**Examples**

3. Signed overflow is a fault, and its record holds the operands, the exact result and the bound it crossed:

```cint c_overflow.ci
I32 x = 2_147_483_647;
I32 y = x + 1;
"{y}\n";
```

```text
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> c_overflow.ci:2:11
   |
 2 | I32 y = x + 1;
   |           ^ add.checked.i32
   |
   = operation: add.checked.i32
   = operand-count: 2
   = operand: I32 2147483647
   = operand: I32 1
   = exact:   2147483648
   = limit:   I32 2147483647
...
```

4. Wrapping and saturation are written where they are meant, for unsigned types as for signed ones:

```cint c_wrap.ci
U8 a = 250;
U8 b = 10;
I64 big = 300;
"{a +% b} {a +| b} {big as% U8}\n";
```

```text
4 255 44
```

5. A cast that loses information is a fault unless it is written to wrap:

```cint c_narrow.ci
I64 big = 300;
"{big as% U8}\n";
U8 small = big as U8;
"{small}\n";
```

```text
44
fault[E_NARROW]: the value does not fit the destination type
  --> c_narrow.ci:3:16
   |
 3 | U8 small = big as U8;
   |                ^ as.checked.i64.u8
   |
   = operation: as.checked.i64.u8
   = operand-count: 1
   = operand: I64 300
   = exact:   300
   = limit:   U8 255
...
```

6. `a & b == 2` tests the mask, as it reads. In C the same text compares `b` with 2 first:

```cint c_prec.ci
I64 a = 6;
I64 b = 3;
if (a & b == 2) {
    "the mask is 2\n";
}
"{a & b == 2} {a | b}\n";
```

```text
the mask is 2
true 7
```

7. A case falls through only where `fallthrough;` says so, and a case range is inclusive:

```cint c_switch.ci
void describe(I64 k) {
    switch (k) {
        case 0:
            "zero, ";
            fallthrough;
        case 1..=5:
            "small\n";
        default:
            "large\n";
    }
}

describe(0);
describe(3);
describe(9);
```

```text
zero, small
small
large
```

8. An integer is not a condition:

```cint c_if.ci
I64 x = 3;
if (x) {
    "nonzero\n";
}
```

```text
error[C2002]: condition is not `Bool`
  --> c_if.ci:2:5
   |
 2 | if (x) {
   |     ^
```

9. A function writes a caller's array through an `inout` view, where C would pass a pointer:

```cint c_view.ci
void bump(inout I64[_] counts, I64 k) {
    counts[k] += 1;
}

void main() {
    I64[3] counts;
    bump(counts, 1);
    bump(counts, 1);
    "{counts[1]}\n";
}
```

```text
2
```

<a id="open-questions"></a>

## Annex E (informative) Open questions

This annex lists a question about a rule of this edition that the edition leaves open. Questions are numbered within each edition. Where this edition settles a question, the rule states the outcome, and the question is not listed here.

1. **Checking a handle at a call.** In this edition a call does not check its handle arguments when it begins. Passing a stale handle does not fault, and the first use of it in the callee faults `E_STALE_HANDLE` (6.9.5, 6.14.4). The open question is whether a call checks each handle argument when it begins; such a check reports the fault at the caller's line. A caller that needs the fault at its own line uses the handle before the call.

<a id="clause-index"></a>

## Annex F (informative) Clause index

Each clause identifier of this edition, with the section that states its rule and the sections that also cite it. Identifiers are the same in every edition and are never reused; section numbers belong to this edition. Each identifier has an anchor of its own name, such as `#IM-106`.

| Identifier | Stated in | Also cited in |
|---|---|---|
| A-1 | 6.13.2 |  |
| A-2 | 6.13.2 |  |
| A-3 | 6.13.2 |  |
| A-4 | 6.13.2 |  |
| IM-1 | 6.1 |  |
| IM-4 | 6.3.1 |  |
| IM-5 | 6.2.1 |  |
| IM-6 | 6.3.1 |  |
| IM-7 | 6.3.1 | 4 |
| IM-8 | 6.3.1 |  |
| IM-9 | 6.3.1 | 6.6 |
| IM-10 | 6.3.3 | 6.6 |
| IM-11 | 6.3.1 |  |
| IM-16 | 6.3.1 |  |
| IM-19 | 6.4.5 |  |
| IM-20 | 6.4.7 | 6.4.5, 6.5 |
| IM-21 | 6.5 |  |
| IM-22 | 6.5 |  |
| IM-23 | 6.8 |  |
| IM-24 | 6.5 |  |
| IM-25 | 6.4.5 |  |
| IM-26 | 6.6 |  |
| IM-27 | 6.5 |  |
| IM-28 | 6.7.12 | 6.7, 6.7.5 |
| IM-29 | 6.7.6 | 6.7.7, 6.7.12 |
| IM-30 | 6.7.6 | 6.7.7, 6.7.12 |
| IM-31 | 6.7 |  |
| IM-32 | 6.7.6 | 6.7.7 |
| IM-33 | 6.7.5 |  |
| IM-34 | 6.7.5 |  |
| IM-35 | 6.7.7 |  |
| IM-36 | 7.2.2 |  |
| IM-37 | 6.7.7 |  |
| IM-39 | 6.7.7 | 7.2.2 |
| IM-40 | 6.7.7 | 7.2.2 |
| IM-41 | 6.7.7 |  |
| IM-42 | 6.7.15 |  |
| IM-43 | 6.7.15 |  |
| IM-44 | 6.7.7 |  |
| IM-45 | 6.7.8 |  |
| IM-46 | 6.7.8 |  |
| IM-47 | 6.7.9 | 6.7.5 |
| IM-48 | 6.7.10 | 6.7.2 |
| IM-49 | 7.2.1 |  |
| IM-50 | 6.6 |  |
| IM-51 | 7.2.3 | 7.1 |
| IM-52 | 7.2.3 | 7.2.4 |
| IM-53 | 7.2.3 |  |
| IM-54 | 6.3.2 |  |
| IM-55 | 6.3.2 |  |
| IM-56 | 6.3 |  |
| IM-57 | 6.6 |  |
| IM-60 | 6.7.5 | 6.7.6, 6.7.8, 6.7.12 |
| IM-61 | 6.7.7 |  |
| IM-62 | 6.7.7 | 6.7.12 |
| IM-63 | 7.2.4 |  |
| IM-64 | 7.2.4 |  |
| IM-65 | 6.7.7 |  |
| IM-66 | 6.7.7 |  |
| IM-67 | 7.2.4 |  |
| IM-68 | 6.6 |  |
| IM-69 | 6.6 |  |
| IM-74 | 7.3 |  |
| IM-75 | 7.3 |  |
| IM-76 | 7.3 |  |
| IM-77 | 7.3 |  |
| IM-78 | 7.3 |  |
| IM-79 | 7.3 |  |
| IM-80 | 7.3 |  |
| IM-81 | 7.3 |  |
| IM-82 | 7.3 |  |
| IM-83 | 7.3 |  |
| IM-84 | 7.3 |  |
| IM-85 | 7.3 |  |
| IM-87 | 7.3 |  |
| IM-90 | 7.5.2 |  |
| IM-92 | 6.2.4 |  |
| IM-93 | 6.2.4 |  |
| IM-94 | 6.2.4 |  |
| IM-95 | 6.2.4 |  |
| IM-96 | 6.2.4 |  |
| IM-97 | 6.2.4 |  |
| IM-98 | 6.2.4 |  |
| IM-99 | 6.2.4 |  |
| IM-100 | 6.2.4 |  |
| IM-101 | 6.2.4 |  |
| IM-102 | 6.2.4 |  |
| IM-103 | 6.8 |  |
| IM-104 | 5.2.3 |  |
| IM-105 | 5.2.3 |  |
| IM-106 | 8.1 |  |
| IM-107 | 8.1 |  |
| IM-111 | 8.1 |  |
| IM-112 | 8.1 |  |
| IM-113 | 8.1 |  |
| IM-114 | 8.1 |  |
| IM-115 | 8.1 |  |
| IM-117 | 5.2.1 |  |
| IM-118 | 5.2.3 |  |
| IM-119 | 5.2.5 |  |
| IM-120 | 6.11.1 |  |
| IM-121 | 5.2.3 |  |
| IM-122 | 5.2.3 |  |
| IM-128 | C.1 |  |
| IM-129 | 4 |  |
| IM-130 | B.3 |  |
| IM-131 | B.3 |  |
| IM-132 | 6.6 | B.3 |
| IM-133 | 6.6 | B.3 |
| IM-134 | B.3 |  |
| IM-135 | B.3 |  |
| IM-137 | 5.2.4 | 5.2.7 |
| IM-139 | 5.2.4 |  |
| IM-140 | 5.2.4 |  |
| IM-141 | 5.2.4 |  |
| IM-142 | 5.2.4 |  |
| IM-143 | 5.2.4 |  |
| IM-159 | 8.2 |  |
| IM-160 | 8.2 |  |
| IM-184 | 6.6 |  |
| IM-185 | 6.7.8 |  |
| IM-186 | 6.12.2 | 6.12.3 |
| IM-187 | 6.12.5 |  |
| IM-188 | 6.7.12 |  |
| K-1 | 6.13.2 | 6.13.1 |
| K-2 | 6.13.2 | 6.13.1 |
| K-3 | 6.13.1 |  |
| K-4 | 6.13.2 |  |
| K-5 | 6.13.1 |  |
| K-6 | 6.13.2 | 6.13.1 |
| K-7 | 6.13.2 |  |
| K-8 | 6.13.1 |  |
| K-9 | 6.13.1 |  |
| K-10 | 6.13.3 |  |
| K-10a | 6.13.3 |  |
| K-11 | 6.13.3 |  |
| K-12 | 6.13.3 |  |
| K-13 | 6.13.3 |  |
| K-14 | 6.13.4 |  |
| K-15 | 6.13.4 |  |
| LS-3 | 5.1.1 |  |
| LS-4 | 6.4.1 |  |
| LS-5 | 6.4.1 |  |
| LS-6 | 6.4.1 |  |
| LS-7 | 6.4.1 |  |
| LS-8 | 5.1.1 |  |
| LS-9 | 6.4.2 |  |
| LS-10 | 6.4.2 |  |
| LS-11 | 6.4.2 |  |
| LS-12 | 6.4.2 |  |
| LS-13 | 6.4.3 |  |
| LS-14 | 6.4.3 |  |
| LS-15 | 6.4.4 |  |
| LS-17 | 6.4.4 |  |
| LS-18 | 6.4.4 |  |
| LS-19 | 6.4.4 |  |
| LS-20 | 6.4.4 |  |
| LS-21 | 6.4.4 |  |
| LS-22 | 6.4.4 |  |
| LS-23 | 6.4.4 |  |
| LS-24 | 6.4.5 |  |
| LS-25 | 6.4.5 |  |
| LS-26 | 6.4.5 |  |
| LS-27 | 6.4.5 |  |
| LS-28 | 6.4.5 |  |
| LS-29 | 6.4.5 |  |
| LS-30 | 6.4.5 |  |
| LS-31 | 6.4.5 |  |
| LS-33 | 6.4.6 |  |
| LS-34 | 6.4.6 |  |
| LS-35 | 6.4.6 |  |
| LS-36 | 6.3.2 |  |
| LS-37 | 6.5 |  |
| LS-38 | 6.6 |  |
| LS-39 | 6.4.7 |  |
| LS-40 | 6.4.8 |  |
| LS-42 | 6.4.8 |  |
| LS-43 | 6.4.8 |  |
| LS-44 | 6.4.8 |  |
| LS-45 | 6.4.8 |  |
| LS-46 | 6.4.8 |  |
| LS-48 | 6.4.9 |  |
| LS-49 | 6.4.9 |  |
| LS-50 | 6.3.3 | 6.3.8 |
| LS-51 | 6.3.1 | 6.3.2 |
| LS-53 | 6.5 |  |
| LS-54 | 6.5 |  |
| LS-55 | 6.5 |  |
| LS-56 | 6.5 |  |
| LS-57 | 6.5 |  |
| LS-58 | 6.5 |  |
| LS-59 | 6.5 |  |
| LS-60 | 6.5 |  |
| LS-62 | 6.12.1 |  |
| LS-63 | 6.12.1 |  |
| LS-64 | 6.12.2 | 6.12.3 |
| LS-65 | 6.12.2 |  |
| LS-66 | 6.12.1 |  |
| LS-67 | 6.12.1 | 6.12.6 |
| LS-68 | 6.12.1 |  |
| LS-69 | 6.12.5 |  |
| LS-70 | 6.12.5 |  |
| LS-71 | 6.12.1 |  |
| LS-77 | 6.9.6 |  |
| LS-78 | 6.3.6 |  |
| LS-79 | 6.3.6 |  |
| LS-91 | 6.3.7 |  |
| LS-92 | 6.3.7 | 6.9.7 |
| LS-93 | 6.3.7 | 6.9.7 |
| LS-94 | 6.11.2 |  |
| LS-95 | 6.11.2 |  |
| LS-96 | 6.3.7 |  |
| LS-97 | 6.3.7 | 6.9.7 |
| LS-104 | 6.9.1 |  |
| LS-106 | 6.9.1 |  |
| LS-107 | 6.9.1 |  |
| LS-109 | 6.9.1 |  |
| LS-110 | 5.2.6 | 5.2.7 |
| LS-111 | 6.9.2 |  |
| LS-112 | 6.2.3 | 6.2.2, 6.9.6 |
| LS-113 | 6.9.3 |  |
| LS-114 | 6.9.4 |  |
| LS-115 | 6.9.4 |  |
| LS-116 | 6.9.4 |  |
| LS-117 | 6.9.4 | 6.9.5 |
| LS-119 | 6.9.5 |  |
| LS-120 | 6.9.5 |  |
| LS-121 | 6.9.5 |  |
| LS-122 | 6.9.5 |  |
| LS-123 | 6.9.5 |  |
| LS-126 | 6.9.5 |  |
| LS-128 | 6.11.1 |  |
| LS-129 | 6.11.3 |  |
| LS-130 | 6.11.4 |  |
| LS-131 | 6.11.4 |  |
| LS-132 | 6.11.5 |  |
| LS-133 | 6.6 |  |
| LS-134 | 6.6 |  |
| LS-135 | 6.6 | 6.3.3 |
| LS-136 | 6.6 |  |
| LS-137 | 6.2.4 |  |
| LS-138 | 6.2.4 |  |
| LS-139 | 6.2.4 |  |
| LS-141 | 5.2.4 |  |
| LS-142 | 6.2.4 |  |
| LS-143 | 6.2.4 |  |
| LS-144 | 5.2.5 | 5.2.7 |
| LS-145 | 7.4 | 7.1, 7.2.1 |
| LS-146 | 7.1 |  |
| LS-149 | 6.7.1 |  |
| LS-150 | 6.7.1 |  |
| LS-151 | 6.7.2 |  |
| LS-152 | 6.7.6 |  |
| LS-153 | 6.7.2 | 6.7.10 |
| LS-154 | 6.7.13 |  |
| LS-156 | 6.7.8 | 6.7.6, 6.7.7, 6.7.9 |
| LS-157 | 6.7.8 |  |
| LS-158 | 6.7.7 |  |
| LS-159 | 6.7.7 |  |
| LS-160 | 6.7.7 |  |
| LS-161 | 6.12.3 |  |
| LS-162 | 6.12.3 | 6.12.4 |
| LS-163 | 6.10.1 |  |
| LS-164 | 6.7.14 |  |
| LS-165 | 6.7.14 |  |
| LS-166 | 6.7.14 |  |
| LS-167 | 6.10.1 |  |
| LS-168 | 6.10.2 |  |
| LS-169 | 6.10.3 |  |
| LS-170 | 6.10.4 |  |
| LS-171 | 6.10.4 |  |
| LS-172 | 6.10.4 |  |
| LS-173 | 6.10.4 |  |
| LS-174 | 6.10.4 |  |
| LS-175 | 6.10.4 |  |
| LS-177 | 6.10.5 |  |
| LS-178 | 6.10.5 |  |
| LS-179 | 6.10.5 |  |
| LS-180 | 6.10.5 |  |
| LS-181 | 6.10.5 |  |
| LS-182 | 6.10.5 |  |
| LS-183 | 6.10.5 |  |
| LS-184 | 6.10.6 |  |
| LS-185 | 6.10.7 |  |
| LS-186 | 6.10.7 |  |
| LS-187 | 6.10.7 |  |
| LS-188 | 6.10.7 |  |
| LS-189 | 6.10.7 |  |
| LS-190 | 6.10.8 |  |
| LS-191 | 6.10.9 |  |
| LS-192 | 6.10.9 |  |
| LS-193 | 6.10.10 |  |
| LS-194 | 6.10.10 |  |
| LS-195 | 7.5.1 |  |
| LS-196 | 6.10.10 |  |
| LS-197 | 7.5.1 |  |
| LS-198 | 7.5.1 |  |
| LS-199 | 7.5.1 |  |
| LS-200 | 7.5.1 |  |
| LS-201 | 7.5.4 |  |
| LS-202 | 7.5.3 |  |
| LS-203 | 7.5.3 |  |
| LS-204 | 7.5.3 |  |
| LS-205 | 7.5.3 |  |
| LS-206 | 7.5.3 |  |
| LS-207 | 7.5.2 |  |
| LS-208 | 7.5.3 |  |
| LS-209 | 7.5.3 |  |
| LS-210 | 7.5.3 |  |
| LS-211 | 7.5.3 |  |
| LS-212 | 7.5.3 |  |
| LS-213 | 7.5.3 | 7.5.4 |
| LS-214 | 7.5.2 | 7.5.1 |
| LS-215 | 7.5.1 |  |
| LS-218 | 5.1.2 |  |
| LS-219 | 5.1.2 |  |
| LS-220 | 5.1.2 |  |
| LS-221 | 5.1.2 | 5.2.1, 6.9.9, 6.11.7 |
| LS-222 | 5.1.2 |  |
| LS-223 | 5.1.2 |  |
| LS-224 | 5.1.2 |  |
| LS-225 | 6.15.1 |  |
| LS-226 | 6.15.1 |  |
| LS-227 | 6.15.2 | 6.15.1 |
| LS-228 | 6.15.1 |  |
| LS-229 | 6.15.1 |  |
| LS-230 | 6.15.1 |  |
| LS-232 | 6.15.3 |  |
| LS-233 | 6.15.3 |  |
| LS-234 | 6.15.3 |  |
| LS-235 | 6.15.3 |  |
| LS-236 | 6.15.3 |  |
| LS-237 | 6.15.3 |  |
| LS-238 | 6.15.3 |  |
| LS-239 | 6.15.3 |  |
| LS-240 | 6.15.3 |  |
| LS-243 | 6.13.1 |  |
| LS-244 | 6.13.1 |  |
| LS-245 | 6.13.2 |  |
| LS-246 | 6.13.1 |  |
| LS-247 | 6.13.1 | 6.13.3 |
| LS-248 | 6.13.3 |  |
| LS-249 | 6.13.5 |  |
| LS-250 | 6.13.3 |  |
| LS-251 | 6.13.4 |  |
| LS-256 | 6.13.3 |  |
| LS-264 | 6.8 |  |
| LS-265 | 6.8 |  |
| LS-268 | 6.8 |  |
| LS-270 | 6.8 |  |
| LS-271 | C.2 |  |
| LS-272 | 4 |  |
| LS-273 | 4 |  |
| LS-274 | 4 |  |
| LS-275 | 4 |  |
| LS-276 | 5.1.3 |  |
| LS-277 | 5.1.3 |  |
| LS-278 | 5.1.3 | 8.1 |
| LS-279 | 5.1.3 |  |
| LS-280 | 5.2.3 |  |
| LS-282 | 5.1.3 |  |
| LS-283 | 5.1.3 | B.1 |
| LS-284 | 5.2.3 |  |
| LS-285 | 5.2.3 |  |
| LS-286 | 5.2.3 |  |
| LS-287 | 6.13.6 |  |
| LS-288 | 6.13.4 | 6.13.6 |
| LS-289 | 5.1.3 |  |
| LS-290 | 5.1.3 |  |
| LS-291 | 5.1.3 |  |
| LS-292 | 5.1.3 |  |
| LS-293 | 5.1.3 |  |
| LS-294 | 5.1.3 |  |
| LS-295 | A.1 | 6.1, A.8 |
| LS-296 | A.2 |  |
| LS-297 | 6.4.4 | A.2 |
| LS-298 | A.3 |  |
| LS-299 | A.4 |  |
| LS-300 | A.5 |  |
| LS-301 | A.6 |  |
| LS-302 | A.7 |  |
| LS-303 | A.8 |  |
| LS-304 | A.8 |  |
| LS-305 | A.8 |  |
| LS-306 | A.8 |  |
| LS-307 | 7.1 | A.8 |
| LS-311 | 5.3 |  |
| LS-312 | 5.1.2 | 5.1.3 |
| LS-313 | 6.3.6 | B.1 |
| LS-314 | 6.3.7 | 6.3.6 |
| LS-315 | 6.11.3 | 6.15.3 |
| LS-316 | 6.10.7 |  |
| LS-317 | 6.10.7 |  |
| LS-318 | 6.10.7 |  |
| LS-319 | 6.10.7 |  |
| LS-320 | 6.10.7 |  |
| LS-321 | 6.10.7 |  |
| LS-322 | 6.10.7 |  |
| LS-323 | 6.9.8 |  |
| LS-324 | 6.9.8 |  |
| LS-325 | 6.9.9 | 6.9.8 |
| LS-326 | 6.9.9 |  |
| LS-327 | 6.9.9 |  |
| LS-328 | 6.9.9 |  |
| M-12 | 6.14.4 | 6.14.3 |
| M-13 | 6.14.1 | 6.14.2 |
| M-14 | 6.14.2 | 6.14.4 |
| M-14a | 6.14.2 | 6.14.5 |
| M-15 | 6.14.3 | 6.14.4 |
| M-16 | 6.14.3 |  |
| M-17 | 6.14.4 |  |
| M-17a | 6.14.3 | 6.14.2, 6.14.4, 6.14.5 |
| M-18 | 6.14.3 |  |
| M-19 | 6.14.2 | 6.14.1, 6.14.3 |
| M-20 | 6.14.1 | 6.14.2 |
| M-20a | 6.14.1 | 6.14.2, 6.14.3 |
| M-20b | 6.14.1 | 6.14.4 |
| M-21 | 6.12.4 |  |
| M-22 | 6.12.4 | 6.14.6 |
| M-23 | 6.14.5 |  |
| M-24 | 6.12.1 | 6.12.4 |
| M-24a | 6.12.4 |  |
| M-25 | 6.12.4 |  |
| M-26 | 6.13.2 |  |
| M-27 | 6.12.2 | 6.12.4 |
| P-1 | 6.13.4 |  |
| P-2 | 6.13.4 |  |
| P-3 | 6.13.4 |  |
| P-4 | 6.13.4 |  |
| R-1 | 6.13.5 |  |
| R-2 | 6.13.5 |  |
| R-3 | 6.13.5 |  |
| R-4 | 7.3 |  |
| R-6 | 6.13.5 |  |
| R-7 | 6.13.5 |  |
| R-8 | 6.13.6 |  |
| V-1 | 6.12.1 | 6.12.4 |
| V-2 | 6.12.1 |  |
| V-3 | 6.12.4 |  |
| V-4 | 6.12.4 |  |
| V-5 | 6.12.4 |  |
| V-6 | 6.12.4 |  |
| V-7 | 6.12.2 | 6.12.3 |
| V-10 | 6.12.4 |  |
| V-11 | 6.12.4 |  |
| V-16 | 6.12.4 |  |
| X-1 | 6.13.6 |  |
| X-2 | 6.13.6 |  |
| X-7 | 6.13.3 |  |
| X-8 | 6.13.6 |  |

### F.1 Identifiers that moved

Identifiers that SPEC-04P.1 or SPEC-01P.1 held, with their place in those editions and in this one. No identifier moved between SPEC-04P.2 and this edition. An identifier of those editions that is not in this table or in the index above is stated in SPEC-01P.3 or is not a rule of this edition.

| Identifier | Earlier place | Place in this edition |
|---|---|---|
| IM-1 | SPEC-01P.1 1.2 | 6.1 |
| IM-4 | SPEC-01P.1 2.1 | 6.3.1 |
| IM-5 | SPEC-01P.1 2.1 | 6.2.1 |
| IM-6 | SPEC-01P.1 2.1 | 6.3.1 |
| IM-7 | SPEC-01P.1 2.2 | 6.3.1 |
| IM-8 | SPEC-01P.1 2.2 | 6.3.1 |
| IM-9 | SPEC-01P.1 2.2 | 6.3.1 |
| IM-10 | SPEC-01P.1 2.3 | 6.3.3 |
| IM-11 | SPEC-01P.1 2.4 | 6.3.1 |
| IM-16 | SPEC-01P.1 2.4 | 6.3.1 |
| IM-19 | SPEC-01P.1 3.1 | 6.4.5 |
| IM-20 | SPEC-01P.1 3.1 | 6.4.7 |
| IM-21 | SPEC-01P.1 3.2 | 6.5 |
| IM-22 | SPEC-01P.1 3.2 | 6.5 |
| IM-23 | SPEC-01P.1 3.2 | 6.8 |
| IM-24 | SPEC-01P.1 3.2 | 6.5 |
| IM-25 | SPEC-01P.1 3.2 | 6.4.5 |
| IM-26 | SPEC-01P.1 3.2 | 6.6 |
| IM-27 | SPEC-01P.1 3.2 | 6.5 |
| IM-28 | SPEC-01P.1 4.1 | 6.7.12 |
| IM-29 | SPEC-01P.1 4.1 | 6.7.6 |
| IM-30 | SPEC-01P.1 4.1 | 6.7.6 |
| IM-31 | SPEC-01P.1 4.1 | 6.7 |
| IM-32 | SPEC-01P.1 4.2 | 6.7.6 |
| IM-33 | SPEC-01P.1 4.3 | 6.7.5 |
| IM-34 | SPEC-01P.1 4.3 | 6.7.5 |
| IM-35 | SPEC-01P.1 4.4 | 6.7.7 |
| IM-36 | SPEC-01P.1 4.4 | 7.2.2 |
| IM-37 | SPEC-01P.1 4.4 | 6.7.7 |
| IM-39 | SPEC-01P.1 4.4 | 6.7.7 |
| IM-40 | SPEC-01P.1 4.4 | 6.7.7 |
| IM-41 | SPEC-01P.1 4.4 | 6.7.7 |
| IM-42 | SPEC-01P.1 4.5 | 6.7.15 |
| IM-43 | SPEC-01P.1 4.5 | 6.7.15 |
| IM-44 | SPEC-01P.1 4.5 | 6.7.7 |
| IM-45 | SPEC-01P.1 4.6 | 6.7.8 |
| IM-46 | SPEC-01P.1 4.6 | 6.7.8 |
| IM-47 | SPEC-01P.1 4.7 | 6.7.9 |
| IM-48 | SPEC-01P.1 4.8 | 6.7.10 |
| IM-49 | SPEC-01P.1 4.8 | 7.2.1 |
| IM-50 | SPEC-01P.1 4.9 | 6.6 |
| IM-51 | SPEC-01P.1 4.10 | 7.2.3 |
| IM-52 | SPEC-01P.1 4.10 | 7.2.3 |
| IM-53 | SPEC-01P.1 4.10 | 7.2.3 |
| IM-54 | SPEC-01P.1 5.1 | 6.3.2 |
| IM-55 | SPEC-01P.1 5.1 | 6.3.2 |
| IM-56 | SPEC-01P.1 5.1 | 6.3 |
| IM-57 | SPEC-01P.1 5.1 | 6.6 |
| IM-60 | SPEC-01P.1 5.2 | 6.7.5 |
| IM-61 | SPEC-01P.1 5.3 | 6.7.7 |
| IM-62 | SPEC-01P.1 5.3 | 6.7.7 |
| IM-63 | SPEC-01P.1 5.3 | 7.2.4 |
| IM-64 | SPEC-01P.1 5.3 | 7.2.4 |
| IM-65 | SPEC-01P.1 5.4 | 6.7.7 |
| IM-66 | SPEC-01P.1 5.4 | 6.7.7 |
| IM-67 | SPEC-01P.1 5.5 | 7.2.4 |
| IM-68 | SPEC-01P.1 5.6 | 6.6 |
| IM-69 | SPEC-01P.1 5.6 | 6.6 |
| IM-74 | SPEC-01P.1 6.1 | 7.3 |
| IM-75 | SPEC-01P.1 6.1 | 7.3 |
| IM-76 | SPEC-01P.1 6.1 | 7.3 |
| IM-77 | SPEC-01P.1 6.2 | 7.3 |
| IM-78 | SPEC-01P.1 6.2 | 7.3 |
| IM-79 | SPEC-01P.1 6.2 | 7.3 |
| IM-80 | SPEC-01P.1 6.3 | 7.3 |
| IM-81 | SPEC-01P.1 6.3 | 7.3 |
| IM-82 | SPEC-01P.1 6.4 | 7.3 |
| IM-83 | SPEC-01P.1 6.4 | 7.3 |
| IM-84 | SPEC-01P.1 6.4 | 7.3 |
| IM-85 | SPEC-01P.1 6.4 | 7.3 |
| IM-87 | SPEC-01P.1 6.5 | 7.3 |
| IM-90 | SPEC-01P.1 7 | 7.5.2 |
| IM-92 | SPEC-01P.1 8.1 | 6.2.4 |
| IM-93 | SPEC-01P.1 8.1 | 6.2.4 |
| IM-94 | SPEC-01P.1 8.1 | 6.2.4 |
| IM-95 | SPEC-01P.1 8.1 | 6.2.4 |
| IM-96 | SPEC-01P.1 8.1 | 6.2.4 |
| IM-97 | SPEC-01P.1 8.2 | 6.2.4 |
| IM-98 | SPEC-01P.1 8.2 | 6.2.4 |
| IM-99 | SPEC-01P.1 8.2 | 6.2.4 |
| IM-100 | SPEC-01P.1 8.2 | 6.2.4 |
| IM-101 | SPEC-01P.1 8.3 | 6.2.4 |
| IM-102 | SPEC-01P.1 8.3 | 6.2.4 |
| IM-103 | SPEC-01P.1 8.4 | 6.8 |
| IM-104 | SPEC-01P.1 9.1 | 5.2.3 |
| IM-105 | SPEC-01P.1 9.1 | 5.2.3 |
| IM-106 | SPEC-01P.1 9.2 | 8.1 |
| IM-107 | SPEC-01P.1 9.2 | 8.1 |
| IM-111 | SPEC-01P.1 9.2 | 8.1 |
| IM-112 | SPEC-01P.1 9.3 | 8.1 |
| IM-113 | SPEC-01P.1 9.3 | 8.1 |
| IM-114 | SPEC-01P.1 9.3 | 8.1 |
| IM-115 | SPEC-01P.1 9.3 | 8.1 |
| IM-117 | SPEC-01P.1 9.4 | 5.2.1 |
| IM-118 | SPEC-01P.1 9.4 | 5.2.3 |
| IM-119 | SPEC-01P.1 9.4 | 5.2.5 |
| IM-120 | SPEC-01P.1 9.4 | 6.11.1 |
| IM-121 | SPEC-01P.1 9.5 | 5.2.3 |
| IM-122 | SPEC-01P.1 9.5 | 5.2.3 |
| IM-128 | SPEC-01P.1 9.6 | C.1 |
| IM-129 | SPEC-01P.1 9.7 | 4 |
| IM-130 | SPEC-01P.1 9.8 | B.3 |
| IM-131 | SPEC-01P.1 9.8 | B.3 |
| IM-132 | SPEC-01P.1 9.8 | 6.6 |
| IM-133 | SPEC-01P.1 9.8 | 6.6 |
| IM-134 | SPEC-01P.1 9.8 | B.3 |
| IM-135 | SPEC-01P.1 9.8 | B.3 |
| IM-137 | SPEC-01P.1 10.1 | 5.2.4 |
| IM-139 | SPEC-01P.1 10.2 | 5.2.4 |
| IM-140 | SPEC-01P.1 10.2 | 5.2.4 |
| IM-141 | SPEC-01P.1 10.2 | 5.2.4 |
| IM-142 | SPEC-01P.1 10.2 | 5.2.4 |
| IM-143 | SPEC-01P.1 10.3 | 5.2.4 |
| IM-159 | SPEC-01P.1 11.4 | 8.2 |
| IM-160 | SPEC-01P.1 11.4 | 8.2 |
| IM-184 | SPEC-01P.1 9.8 | 6.6 |
| IM-185 | SPEC-01P.1 9.8 | 6.7.8 |
| IM-186 | SPEC-01P.1 9.8 | 6.12.2 |
| IM-187 | SPEC-01P.1 9.8 | 6.12.5 |
| IM-188 | SPEC-01P.1 4.1 | 6.7.12 |
| LS-3 | SPEC-04P.1 3.1 | 5.1.1 |
| LS-4 | SPEC-04P.1 3.1 | 6.4.1 |
| LS-5 | SPEC-04P.1 3.1 | 6.4.1 |
| LS-6 | SPEC-04P.1 3.1 | 6.4.1 |
| LS-7 | SPEC-04P.1 3.1 | 6.4.1 |
| LS-8 | SPEC-04P.1 3.1 | 5.1.1 |
| LS-9 | SPEC-04P.1 3.2 | 6.4.2 |
| LS-10 | SPEC-04P.1 3.2 | 6.4.2 |
| LS-11 | SPEC-04P.1 3.2 | 6.4.2 |
| LS-12 | SPEC-04P.1 3.2 | 6.4.2 |
| LS-13 | SPEC-04P.1 3.3 | 6.4.3 |
| LS-14 | SPEC-04P.1 3.3 | 6.4.3 |
| LS-15 | SPEC-04P.1 3.4 | 6.4.4 |
| LS-17 | SPEC-04P.1 3.4 | 6.4.4 |
| LS-18 | SPEC-04P.1 3.4 | 6.4.4 |
| LS-19 | SPEC-04P.1 3.4 | 6.4.4 |
| LS-20 | SPEC-04P.1 3.4 | 6.4.4 |
| LS-21 | SPEC-04P.1 3.4 | 6.4.4 |
| LS-22 | SPEC-04P.1 3.4 | 6.4.4 |
| LS-23 | SPEC-04P.1 3.4 | 6.4.4 |
| LS-24 | SPEC-04P.1 3.5 | 6.4.5 |
| LS-25 | SPEC-04P.1 3.5 | 6.4.5 |
| LS-26 | SPEC-04P.1 3.5 | 6.4.5 |
| LS-27 | SPEC-04P.1 3.5 | 6.4.5 |
| LS-28 | SPEC-04P.1 3.5 | 6.4.5 |
| LS-29 | SPEC-04P.1 3.5 | 6.4.5 |
| LS-30 | SPEC-04P.1 3.5 | 6.4.5 |
| LS-31 | SPEC-04P.1 3.5 | 6.4.5 |
| LS-33 | SPEC-04P.1 3.6 | 6.4.6 |
| LS-34 | SPEC-04P.1 3.6 | 6.4.6 |
| LS-35 | SPEC-04P.1 3.6 | 6.4.6 |
| LS-36 | SPEC-04P.1 3.6 | 6.3.2 |
| LS-37 | SPEC-04P.1 3.6 | 6.5 |
| LS-38 | SPEC-04P.1 3.6 | 6.6 |
| LS-39 | SPEC-04P.1 3.7 | 6.4.7 |
| LS-40 | SPEC-04P.1 3.8 | 6.4.8 |
| LS-42 | SPEC-04P.1 3.8 | 6.4.8 |
| LS-43 | SPEC-04P.1 3.8 | 6.4.8 |
| LS-44 | SPEC-04P.1 3.8 | 6.4.8 |
| LS-45 | SPEC-04P.1 3.8 | 6.4.8 |
| LS-46 | SPEC-04P.1 3.8 | 6.4.8 |
| LS-48 | SPEC-04P.1 3.9 | 6.4.9 |
| LS-49 | SPEC-04P.1 3.9 | 6.4.9 |
| LS-50 | SPEC-04P.1 4.1 | 6.3.3 |
| LS-51 | SPEC-04P.1 4.1 | 6.3.1 |
| LS-53 | SPEC-04P.1 4.2 | 6.5 |
| LS-54 | SPEC-04P.1 4.2 | 6.5 |
| LS-55 | SPEC-04P.1 4.2 | 6.5 |
| LS-56 | SPEC-04P.1 4.2 | 6.5 |
| LS-57 | SPEC-04P.1 4.2 | 6.5 |
| LS-58 | SPEC-04P.1 4.2 | 6.5 |
| LS-59 | SPEC-04P.1 4.2 | 6.5 |
| LS-60 | SPEC-04P.1 4.2 | 6.5 |
| LS-62 | SPEC-04P.1 4.3 | 6.12.1 |
| LS-63 | SPEC-04P.1 4.3 | 6.12.1 |
| LS-64 | SPEC-04P.1 4.3 | 6.12.2 |
| LS-65 | SPEC-04P.1 4.3 | 6.12.2 |
| LS-66 | SPEC-04P.1 4.3 | 6.12.1 |
| LS-67 | SPEC-04P.1 4.3 | 6.12.1 |
| LS-68 | SPEC-04P.1 4.3 | 6.12.1 |
| LS-69 | SPEC-04P.1 4.3 | 6.12.5 |
| LS-70 | SPEC-04P.1 4.3 | 6.12.5 |
| LS-71 | SPEC-04P.1 4.3 | 6.12.1 |
| LS-77 | SPEC-04P.1 4.4 | 6.9.6 |
| LS-78 | SPEC-04P.1 4.4 | 6.3.6 |
| LS-79 | SPEC-04P.1 4.4 | 6.3.6 |
| LS-91 | SPEC-04P.1 4.5 | 6.3.7 |
| LS-92 | SPEC-04P.1 4.6 | 6.3.7 |
| LS-93 | SPEC-04P.1 4.6 | 6.3.7 |
| LS-94 | SPEC-04P.1 4.6 | 6.11.2 |
| LS-95 | SPEC-04P.1 4.6 | 6.11.2 |
| LS-96 | SPEC-04P.1 4.6 | 6.3.7 |
| LS-97 | SPEC-04P.1 4.6 | 6.3.7 |
| LS-104 | SPEC-04P.1 5.1 | 6.9.1 |
| LS-106 | SPEC-04P.1 5.1 | 6.9.1 |
| LS-107 | SPEC-04P.1 5.1 | 6.9.1 |
| LS-109 | SPEC-04P.1 5.1 | 6.9.1 |
| LS-110 | SPEC-04P.1 5.1 | 5.2.6 |
| LS-111 | SPEC-04P.1 5.2 | 6.9.2 |
| LS-112 | SPEC-04P.1 5.3 | 6.2.3 |
| LS-113 | SPEC-04P.1 5.4 | 6.9.3 |
| LS-114 | SPEC-04P.1 6.1 | 6.9.4 |
| LS-115 | SPEC-04P.1 6.1 | 6.9.4 |
| LS-116 | SPEC-04P.1 6.1 | 6.9.4 |
| LS-117 | SPEC-04P.1 6.1 | 6.9.4 |
| LS-119 | SPEC-04P.1 6.2 | 6.9.5 |
| LS-120 | SPEC-04P.1 6.2 | 6.9.5 |
| LS-121 | SPEC-04P.1 6.2 | 6.9.5 |
| LS-122 | SPEC-04P.1 6.2 | 6.9.5 |
| LS-123 | SPEC-04P.1 6.2 | 6.9.5 |
| LS-126 | SPEC-04P.1 6.3 | 6.9.5 |
| LS-128 | SPEC-04P.1 6.5 | 6.11.1 |
| LS-129 | SPEC-04P.1 6.5 | 6.11.3 |
| LS-130 | SPEC-04P.1 6.5 | 6.11.4 |
| LS-131 | SPEC-04P.1 6.5 | 6.11.4 |
| LS-132 | SPEC-04P.1 6.5 | 6.11.5 |
| LS-133 | SPEC-04P.1 6.6 | 6.6 |
| LS-134 | SPEC-04P.1 6.6 | 6.6 |
| LS-135 | SPEC-04P.1 6.6 | 6.6 |
| LS-136 | SPEC-04P.1 6.6 | 6.6 |
| LS-137 | SPEC-04P.1 6.7 | 6.2.4 |
| LS-138 | SPEC-04P.1 6.7 | 6.2.4 |
| LS-139 | SPEC-04P.1 6.7 | 6.2.4 |
| LS-141 | SPEC-04P.1 6.7 | 5.2.4 |
| LS-142 | SPEC-04P.1 6.7 | 6.2.4 |
| LS-143 | SPEC-04P.1 6.7 | 6.2.4 |
| LS-144 | SPEC-04P.1 6.7 | 5.2.5 |
| LS-145 | SPEC-04P.1 6.8 | 7.4 |
| LS-146 | SPEC-04P.1 6.8 | 7.1 |
| LS-149 | SPEC-04P.1 7.1 | 6.7.1 |
| LS-150 | SPEC-04P.1 7.1 | 6.7.1 |
| LS-151 | SPEC-04P.1 7.2 | 6.7.2 |
| LS-152 | SPEC-04P.1 7.2 | 6.7.6 |
| LS-153 | SPEC-04P.1 7.3 | 6.7.2 |
| LS-154 | SPEC-04P.1 7.4 | 6.7.13 |
| LS-156 | SPEC-04P.1 7.5 | 6.7.8 |
| LS-157 | SPEC-04P.1 7.5 | 6.7.8 |
| LS-158 | SPEC-04P.1 7.5 | 6.7.7 |
| LS-159 | SPEC-04P.1 7.5 | 6.7.7 |
| LS-160 | SPEC-04P.1 7.5 | 6.7.7 |
| LS-161 | SPEC-04P.1 7.6 | 6.12.3 |
| LS-162 | SPEC-04P.1 7.6 | 6.12.3 |
| LS-163 | SPEC-04P.1 8.1 | 6.10.1 |
| LS-164 | SPEC-04P.1 8.1 | 6.7.14 |
| LS-165 | SPEC-04P.1 8.1 | 6.7.14 |
| LS-166 | SPEC-04P.1 8.1 | 6.7.14 |
| LS-167 | SPEC-04P.1 8.1 | 6.10.1 |
| LS-168 | SPEC-04P.1 8.2 | 6.10.2 |
| LS-169 | SPEC-04P.1 8.3 | 6.10.3 |
| LS-170 | SPEC-04P.1 8.4 | 6.10.4 |
| LS-171 | SPEC-04P.1 8.4 | 6.10.4 |
| LS-172 | SPEC-04P.1 8.4 | 6.10.4 |
| LS-173 | SPEC-04P.1 8.4 | 6.10.4 |
| LS-174 | SPEC-04P.1 8.4 | 6.10.4 |
| LS-175 | SPEC-04P.1 8.4 | 6.10.4 |
| LS-177 | SPEC-04P.1 8.5 | 6.10.5 |
| LS-178 | SPEC-04P.1 8.5 | 6.10.5 |
| LS-179 | SPEC-04P.1 8.5 | 6.10.5 |
| LS-180 | SPEC-04P.1 8.5 | 6.10.5 |
| LS-181 | SPEC-04P.1 8.5 | 6.10.5 |
| LS-182 | SPEC-04P.1 8.5 | 6.10.5 |
| LS-183 | SPEC-04P.1 8.5 | 6.10.5 |
| LS-184 | SPEC-04P.1 8.6 | 6.10.6 |
| LS-185 | SPEC-04P.1 8.7 | 6.10.7 |
| LS-186 | SPEC-04P.1 8.7 | 6.10.7 |
| LS-187 | SPEC-04P.1 8.7 | 6.10.7 |
| LS-188 | SPEC-04P.1 8.7 | 6.10.7 |
| LS-189 | SPEC-04P.1 8.7 | 6.10.7 |
| LS-190 | SPEC-04P.1 8.8 | 6.10.8 |
| LS-191 | SPEC-04P.1 8.9 | 6.10.9 |
| LS-192 | SPEC-04P.1 8.9 | 6.10.9 |
| LS-193 | SPEC-04P.1 9.1 | 6.10.10 |
| LS-194 | SPEC-04P.1 9.1 | 6.10.10 |
| LS-195 | SPEC-04P.1 9.1 | 7.5.1 |
| LS-196 | SPEC-04P.1 9.1 | 6.10.10 |
| LS-197 | SPEC-04P.1 9.1 | 7.5.1 |
| LS-198 | SPEC-04P.1 9.2 | 7.5.1 |
| LS-199 | SPEC-04P.1 9.2 | 7.5.1 |
| LS-200 | SPEC-04P.1 9.2 | 7.5.1 |
| LS-201 | SPEC-04P.1 9.2 | 7.5.4 |
| LS-202 | SPEC-04P.1 9.3 | 7.5.3 |
| LS-203 | SPEC-04P.1 9.3 | 7.5.3 |
| LS-204 | SPEC-04P.1 9.3 | 7.5.3 |
| LS-205 | SPEC-04P.1 9.3 | 7.5.3 |
| LS-206 | SPEC-04P.1 9.3 | 7.5.3 |
| LS-207 | SPEC-04P.1 9.3 | 7.5.2 |
| LS-208 | SPEC-04P.1 9.3 | 7.5.3 |
| LS-209 | SPEC-04P.1 9.3 | 7.5.3 |
| LS-210 | SPEC-04P.1 9.3 | 7.5.3 |
| LS-211 | SPEC-04P.1 9.3 | 7.5.3 |
| LS-212 | SPEC-04P.1 9.3 | 7.5.3 |
| LS-213 | SPEC-04P.1 9.3 | 7.5.3 |
| LS-214 | SPEC-04P.1 9.4 | 7.5.2 |
| LS-215 | SPEC-04P.1 9.5 | 7.5.1 |
| LS-218 | SPEC-04P.1 10 | 5.1.2 |
| LS-219 | SPEC-04P.1 10 | 5.1.2 |
| LS-220 | SPEC-04P.1 10 | 5.1.2 |
| LS-221 | SPEC-04P.1 10 | 5.1.2 |
| LS-222 | SPEC-04P.1 10 | 5.1.2 |
| LS-223 | SPEC-04P.1 10 | 5.1.2 |
| LS-224 | SPEC-04P.1 10 | 5.1.2 |
| LS-225 | SPEC-04P.1 11 | 6.15.1 |
| LS-226 | SPEC-04P.1 11 | 6.15.1 |
| LS-227 | SPEC-04P.1 11 | 6.15.2 |
| LS-228 | SPEC-04P.1 11 | 6.15.1 |
| LS-229 | SPEC-04P.1 11 | 6.15.1 |
| LS-230 | SPEC-04P.1 11 | 6.15.1 |
| LS-232 | SPEC-04P.1 12 | 6.15.3 |
| LS-233 | SPEC-04P.1 12 | 6.15.3 |
| LS-234 | SPEC-04P.1 12 | 6.15.3 |
| LS-235 | SPEC-04P.1 12 | 6.15.3 |
| LS-236 | SPEC-04P.1 12 | 6.15.3 |
| LS-237 | SPEC-04P.1 12 | 6.15.3 |
| LS-238 | SPEC-04P.1 12 | 6.15.3 |
| LS-239 | SPEC-04P.1 12 | 6.15.3 |
| LS-240 | SPEC-04P.1 12 | 6.15.3 |
| LS-243 | SPEC-04P.1 13.2 | 6.13.1 |
| LS-244 | SPEC-04P.1 13.2 | 6.13.1 |
| LS-245 | SPEC-04P.1 13.2 | 6.13.2 |
| LS-246 | SPEC-04P.1 13.2 | 6.13.1 |
| LS-247 | SPEC-04P.1 13.2 | 6.13.1 |
| LS-248 | SPEC-04P.1 13.2 | 6.13.3 |
| LS-249 | SPEC-04P.1 13.2 | 6.13.5 |
| LS-250 | SPEC-04P.1 13.2 | 6.13.3 |
| LS-251 | SPEC-04P.1 13.2 | 6.13.4 |
| LS-256 | SPEC-04P.1 13.2 | 6.13.3 |
| LS-264 | SPEC-04P.1 14 | 6.8 |
| LS-265 | SPEC-04P.1 14 | 6.8 |
| LS-268 | SPEC-04P.1 14 | 6.8 |
| LS-270 | SPEC-04P.1 14 | 6.8 |
| LS-271 | SPEC-04P.1 15 | C.2 |
| LS-272 | SPEC-04P.1 16 | 4 |
| LS-273 | SPEC-04P.1 16 | 4 |
| LS-274 | SPEC-04P.1 16 | 4 |
| LS-275 | SPEC-04P.1 16 | 4 |
| LS-276 | SPEC-04P.1 17.1 | 5.1.3 |
| LS-277 | SPEC-04P.1 17.1 | 5.1.3 |
| LS-278 | SPEC-04P.1 17.1 | 5.1.3 |
| LS-279 | SPEC-04P.1 17.1 | 5.1.3 |
| LS-280 | SPEC-04P.1 17.1 | 5.2.3 |
| LS-282 | SPEC-04P.1 17.1 | 5.1.3 |
| LS-283 | SPEC-04P.1 17.2 | 5.1.3 |
| LS-284 | SPEC-04P.1 17.3 | 5.2.3 |
| LS-285 | SPEC-04P.1 17.3 | 5.2.3 |
| LS-286 | SPEC-04P.1 17.3 | 5.2.3 |
| LS-287 | SPEC-04P.1 17.4 | 6.13.6 |
| LS-288 | SPEC-04P.1 17.4 | 6.13.4 |
| LS-289 | SPEC-04P.1 17.5 | 5.1.3 |
| LS-290 | SPEC-04P.1 17.5 | 5.1.3 |
| LS-291 | SPEC-04P.1 17.6 | 5.1.3 |
| LS-292 | SPEC-04P.1 17.6 | 5.1.3 |
| LS-293 | SPEC-04P.1 17.6 | 5.1.3 |
| LS-294 | SPEC-04P.1 17.6 | 5.1.3 |
| LS-295 | SPEC-04P.1 18 | A.1 |
| LS-296 | SPEC-04P.1 18.1 | A.2 |
| LS-297 | SPEC-04P.1 18.1 | 6.4.4 |
| LS-298 | SPEC-04P.1 18.2 | A.3 |
| LS-299 | SPEC-04P.1 18.3 | A.4 |
| LS-300 | SPEC-04P.1 18.4 | A.5 |
| LS-301 | SPEC-04P.1 18.5 | A.6 |
| LS-302 | SPEC-04P.1 18.6 | A.7 |
| LS-303 | SPEC-04P.1 18.6 | A.8 |
| LS-304 | SPEC-04P.1 18.6 | A.8 |
| LS-305 | SPEC-04P.1 18.6 | A.8 |
| LS-306 | SPEC-04P.1 18.6 | A.8 |
| LS-307 | SPEC-04P.1 18.6 | 7.1 |
| LS-311 | SPEC-04P.1 20 | 5.3 |
| LS-312 | SPEC-04P.1 10 | 5.1.2 |
| LS-313 | SPEC-04P.1 17.2 | 6.3.6 |
| LS-314 | SPEC-04P.1 4.6 | 6.3.7 |
| LS-315 | SPEC-04P.1 12 | 6.11.3 |
| LS-316 | SPEC-04P.1 8.7 | 6.10.7 |
| LS-317 | SPEC-04P.1 8.7 | 6.10.7 |
| LS-318 | SPEC-04P.1 8.7 | 6.10.7 |
| LS-319 | SPEC-04P.1 8.7 | 6.10.7 |
| LS-320 | SPEC-04P.1 8.7 | 6.10.7 |
| LS-321 | SPEC-04P.1 8.7 | 6.10.7 |
| LS-322 | SPEC-04P.1 8.7 | 6.10.7 |
