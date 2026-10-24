# GLOSSARY-P.4: Glossary

GLOSSARY-P.4, edition 4 of the glossary of the cint specification, cint v0.2.4, 2026-10-24.

This edition replaces GLOSSARY-P.3. It is a reference to the rules of the other editions: where an entry and the section it points to differ, the section governs. Its text does not change after its release; a correction that changes no meaning is listed in [ERRATA.md](ERRATA.md), and a change of meaning is published as a new edition.

Section 1 explains, in plain words, the ideas a first reading meets. Section 2 lists the types and operators. Section 3 lists every term that a 0.2.4 edition defines in its clause 3, with the first sentence of its definition and a link to it. Each entry has an anchor named by its word, such as `GLOSSARY-P.4.md#golden` in section 1 and `GLOSSARY-P.4.md#term-fuel` in section 3.

## 1 Words for a first reading

These entries explain words that the specifications use in their everyday programming sense, for a reader who is new to programming or to C. Each points to the section that teaches the idea. Section 3 gives the precise definitions.

| Word | Meaning | Read |
|---|---|---|
| <a id="value"></a>value | A number a program works with. In cint every value has a type: an integer type, `Bool`, a fixed-point type or an array or struct type built from them. A fixed-point value such as `1.5` in `Q16.16` is held as an integer count of units of 2^-16. | [SPEC-04P.4 6.2.1](SPEC-04P.4-LANGUAGE.md#values) |
| <a id="type"></a>type | The kind of a value: its range and the operations it allows. `I32` is a signed integer of 32 bits; `U8` an unsigned integer of 8 bits, from 0 to 255. Every variable has one type, written before its name. | [SPEC-04P.4 6.3](SPEC-04P.4-LANGUAGE.md#types) |
| <a id="variable"></a>variable | A named place that holds a value and can be given a new one: `I64 total = 0;` declares one, and `total = total + 5;` changes it. | [SPEC-04P.4 6.9.1](SPEC-04P.4-LANGUAGE.md#variables) |
| <a id="constant"></a>constant | A named value that never changes, fixed when the program is translated: `const I64 DAYS = 7;`. | [SPEC-04P.4 6.9.2](SPEC-04P.4-LANGUAGE.md#constants) |
| <a id="expression"></a>expression | Text that computes a value, such as `price * count + 1`. | [SPEC-04P.4 6.7](SPEC-04P.4-LANGUAGE.md#expressions) |
| <a id="statement"></a>statement | One step of a program, such as a declaration, an assignment, an `if` or a loop. A declaration, an assignment and a call end with `;`; an `if`, a loop and a block end with `}`. | [SPEC-04P.4 6.10](SPEC-04P.4-LANGUAGE.md#statements) |
| <a id="loop"></a>loop | A statement that runs a block again and again: `for i in 0..10 { }` runs it once for each `i` from 0 to 9, and `while (x > 0) { }` runs it while the condition holds. | [SPEC-04P.4 6.10.3, 6.10.4](SPEC-04P.4-LANGUAGE.md#the-for-statement) |
| <a id="function"></a>function | A named piece of program that receives values through its parameters, computes, and may return a value: `I64 square(I64 x) { return x * x; }`. | [SPEC-04P.4 6.9.4](SPEC-04P.4-LANGUAGE.md#functions) |
| <a id="parameter"></a><a id="argument"></a>parameter, argument | A *parameter* is a name a function declares for a value it receives; an *argument* is the value a call passes. In `square(7)`, 7 is the argument for the parameter `x`. | [SPEC-04P.4 6.9.5](SPEC-04P.4-LANGUAGE.md#parameters-and-arguments) |
| <a id="array"></a>array | A fixed number of values of one type, stored in order and numbered from 0: `I64[3] a = [10, 20, 30];` and `a[0]` is 10. | [SPEC-04P.4 6.12](SPEC-04P.4-LANGUAGE.md#arrays-and-views) |
| <a id="view"></a>view | A way to reach part or all of an array without copying it, which knows its own length and may be read-only. A function that takes `in I64[_] v` can read an array of any length. | [SPEC-04P.4 6.3.5, 6.12.4](SPEC-04P.4-LANGUAGE.md#views-as-parameters) |
| <a id="kernel"></a>kernel | A function that runs once for each point of a range of indices, each point writing only its own part of the result, so the points can run in parallel, on a GPU for example. | [SPEC-04P.4 6.13](SPEC-04P.4-LANGUAGE.md#kernels) |
| <a id="size-parameter"></a>size parameter | A name for an array length that a function or kernel takes from its arguments, written in brackets after its name: `I64 total[n](in I64[n] a)` names the length of `a` as `n`. | [SPEC-04P.4 6.9.5](SPEC-04P.4-LANGUAGE.md#parameters-and-arguments) |
| <a id="struct"></a><a id="record"></a>struct (record) | A value made of named fields, such as a point with `x` and `y`. Other languages call it a record. | [SPEC-04P.4 6.9.6](SPEC-04P.4-LANGUAGE.md#structs) |
| <a id="script"></a><a id="program"></a><a id="module"></a>script, program, module | A *module* is one source file. A *script* is a module whose statements run in order from the top; a *program* may instead start at `main`; an *importable module* only declares things for other modules to use. | [SPEC-04P.4 5.1.2](SPEC-04P.4-LANGUAGE.md#scripts-and-programs) |
| <a id="translation"></a>translation | Everything that happens to a program before it runs: reading it, checking it and compiling it. A mistake found then is reported as a *diagnostic*, and the program does not run. | [SPEC-04P.4 5.1.3](SPEC-04P.4-LANGUAGE.md#diagnostics) |
| <a id="fault"></a>fault | The stop of a run at an operation that cannot give a correct result, such as a sum too large for its type, a division by zero or an index past the end of an array. | [SPEC-04P.4 5.2.3](SPEC-04P.4-LANGUAGE.md#faults) |
| <a id="fault-record"></a><a id="fault-report"></a>fault record, fault report | The *fault record* says exactly what failed: the operation, its operands, the exact result, the bound crossed and the position. The *fault report* is how a tool prints it. A record is the same on every machine. | [SPEC-04P.4 8.1](SPEC-04P.4-LANGUAGE.md#fault-record) |
| <a id="error"></a>error | An expected failure that a function returns as a value, such as "not found", and that its caller handles with `try` or `catch`. Unlike a fault, an error does not stop the program. | [SPEC-04P.4 6.11](SPEC-04P.4-LANGUAGE.md#errors-and-faults) |
| <a id="service"></a><a id="grant"></a>service, grant | A *service* is a set of functions that the runtime supplies, such as `fs` for files and `console` for the terminal. A program calls one only under a *grant* from the person who runs it, such as `--grant fs.read:/data=data`; a call that no grant covers returns the error `denied` and changes nothing. | [SPEC-04P.4 6.9.9](SPEC-04P.4-LANGUAGE.md#services-and-grants) |
| <a id="byte-string"></a>byte string | A string literal written `b"abc"`: the bytes of its text, as a read-only view of `U8` elements, so that a program can compare bytes with `equal` or pass them to a service. | [SPEC-04P.4 6.4.8](SPEC-04P.4-LANGUAGE.md#string-literals) |
| <a id="run-cache"></a>run cache | The directory in which `cint run` keeps the executable it built, so that a later run of the same program starts it without building it again; `--rebuild` ignores it. | [SPEC-06P.3 5.4](SPEC-06P.3-TOOLS.md#cint-run) |
| <a id="test"></a>test | A block in a module that checks one expectation, such as that a function returns 5 or that a call faults; `cint test` runs every test. | [SPEC-04P.4 6.15.3](SPEC-04P.4-LANGUAGE.md#tests) |
| <a id="golden"></a>golden | An output saved from a run that was checked once, kept with the program and compared byte for byte with the output of each later run of the same program on the same inputs. | [SPEC-04P.4 8.3](SPEC-04P.4-LANGUAGE.md#goldens) |
| <a id="fuel"></a>fuel | A count of the steps a run takes: calls, loop iterations, reductions and kernel dispatches. It is the same on every machine, and a budget of fuel can stop a run that goes on too long. | [SPEC-04P.4 5.2.4](SPEC-04P.4-LANGUAGE.md#fuel) |
| <a id="the-reference"></a>the reference | A second implementation of cint, written in Python to follow the specification directly. Its results are the expected results of the conformance suite. | [SPEC-04P.4 8.2](SPEC-04P.4-LANGUAGE.md#agreement) |
| <a id="checkpoint"></a><a id="replay"></a>checkpoint, replay | A *checkpoint* saves a program's state between runs; *replay* runs recorded steps again from a checkpoint and checks that every outcome repeats. | [SPEC-04P.4 8.4](SPEC-04P.4-LANGUAGE.md#replay) |
| <a id="debug-tools"></a>debug tools | `cint explain`, `cint debug` and the `cint workbench` session, which show what can fault, find the first fault of a run, and let a program be tried one line at a time. | [SPEC-04P.4 8.5](SPEC-04P.4-LANGUAGE.md#debug-tools) |

## 2 Types and operators

| Spelling | Meaning | Read |
|---|---|---|
| `I8` `I16` `I32` `I64` | Signed integers of 8, 16, 32 and 64 bits. | [SPEC-04P.4 6.3.1](SPEC-04P.4-LANGUAGE.md#integer-types) |
| `U8` `U16` `U32` `U64` | Unsigned integers of 8, 16, 32 and 64 bits. | [SPEC-04P.4 6.3.1](SPEC-04P.4-LANGUAGE.md#integer-types) |
| `I128` `I256` `I512` `I1024`, `U128` `U256` `U512` | The wide integer types, with the same operators as the others. | [SPEC-04P.4 6.3.1](SPEC-04P.4-LANGUAGE.md#integer-types) |
| `Q<i>.<f>`, such as `Q16.16` | Signed fixed point with `i` integer bits and `f` fraction bits: an integer counted in units of 2^-`f`. | [SPEC-04P.4 6.3.2](SPEC-04P.4-LANGUAGE.md#fixed-point-types) |
| `Bool` | `true` or `false`. | [SPEC-04P.4 6.3.3](SPEC-04P.4-LANGUAGE.md#bool) |
| `T[n]`, `T[n, m]` | An array of `n` elements, or of `n` by `m`, of type `T`. | [SPEC-04P.4 6.3.4](SPEC-04P.4-LANGUAGE.md#array-types) |
| `in T[_]`, `inout T[_]` | A view that may only be read, or may be read and written. | [SPEC-04P.4 6.3.5](SPEC-04P.4-LANGUAGE.md#view-types) |
| `E!T` | An error union: a value of `T`, or an error of the error set `E`. | [SPEC-04P.4 6.3.7](SPEC-04P.4-LANGUAGE.md#error-sets-and-error-unions) |
| `Grants` | The grant set of a run, which `main` may receive and pass to the standard services. | [SPEC-04P.4 6.9.9](SPEC-04P.4-LANGUAGE.md#services-and-grants) |
| `extern` | A function that the program calls and the host program or the runtime supplies, declared with its effect class: `extern observe I64 clock_ns();`. | [SPEC-04P.4 6.9.8](SPEC-04P.4-LANGUAGE.md#extern-declarations) |
| `+` `-` `*` `/` `%` `<<` `>>` | Every plain operator is checked: the exact result when it is in range, and a fault otherwise. | [SPEC-04P.4 6.7](SPEC-04P.4-LANGUAGE.md#expressions) |
| `+%` `-%` `*%` `<<%` | Wrapping operators, which never fault: the exact result reduced into the type's range. | [SPEC-04P.4 6.7.6](SPEC-04P.4-LANGUAGE.md#additive-operators) |
| `+\|` `-\|` `*\|` | Saturating operators, which never fault: the exact result clamped to the type's range. | [SPEC-04P.4 6.7.6](SPEC-04P.4-LANGUAGE.md#additive-operators), [6.7.7](SPEC-04P.4-LANGUAGE.md#multiplicative-operators) |
| `/` and `%` on integers | Division rounded toward negative infinity, and the remainder with the sign of the divisor: `-7 / 2` is -4 and `-7 % 2` is 1. | [SPEC-04P.4 6.7.7](SPEC-04P.4-LANGUAGE.md#multiplicative-operators) |
| `as`, `as%`, `as\|`, `as?` | Conversions: checked, wrapping, saturating and returning an error union. | [SPEC-04P.4 6.6](SPEC-04P.4-LANGUAGE.md#conversions) |
| `round` | A rounding mode after a conversion or a fraction literal: `x as I32 round floor`. | [SPEC-04P.4 6.7.15](SPEC-04P.4-LANGUAGE.md#rounding-modes-and-the-round-clause) |
| `..` and `..=` | A range that excludes, or includes, its end: `0..n` is 0 to `n - 1`. | [SPEC-04P.4 6.10.4](SPEC-04P.4-LANGUAGE.md#the-for-statement) |
| `0t...` | An integer literal written in balanced ternary, with the digits `N`, `0` and `P` for -1, 0 and 1: `0tPN` is 3 - 1 = 2. | [SPEC-04P.4 6.4.5](SPEC-04P.4-LANGUAGE.md#integer-literals) |
| `b"..."` | A byte string: the bytes of the text, as a read-only view of `U8`. | [SPEC-04P.4 6.4.8](SPEC-04P.4-LANGUAGE.md#string-literals) |
| `equal(a, b)` | `true` when two arrays or views have the same extents and equal elements. | [SPEC-04P.4 7.4](SPEC-04P.4-LANGUAGE.md#array-operations) |
| `f() catch (e) { ... };` | The `catch` statement, on a call whose result is `E!void`; its block may complete. | [SPEC-04P.4 6.11.4](SPEC-04P.4-LANGUAGE.md#catch) |
| `{expr}` | A hole in a print statement, replaced by the value of `expr`. | [SPEC-04P.4 7.5.1](SPEC-04P.4-LANGUAGE.md#holes) |
| `_` | The discard name, and an extent taken from the argument in a view type. | [SPEC-04P.4 6.11.5](SPEC-04P.4-LANGUAGE.md#discarding-a-result) |

## 3 Terms of the specifications

| Term | Definition | Defined in |
|---|---|---|
| <a id="term-address"></a>address | The part of a fault record that locates a fault in a dispatch: the kernel's qualified name, the dispatch number, the phase, and, in the work-item phase, the work-item index and the step ordinal. | [SPEC-02P.2 3.10](SPEC-02P.2-KERNELS.md#term-address) |
| <a id="term-addressed-storage"></a>addressed storage | The set of element offsets that a view reaches. | [SPEC-02P.2 3.15](SPEC-02P.2-KERNELS.md#term-addressed-storage) |
| <a id="term-agreement"></a>agreement | The relation between two implementations whose canonical outcomes for a program and the same semantic inputs are equal. | [SPEC-09P.3 3.13](SPEC-09P.3-TOOLCHAIN.md#term-agreement) |
| <a id="term-arena"></a>arena | A region of storage with a fixed capacity from which objects are taken and released together. | [SPEC-04P.4 3.30](SPEC-04P.4-LANGUAGE.md#term-arena) |
| <a id="term-array"></a>array | A fixed number of elements of one type, stored in order. | [SPEC-04P.4 3.26](SPEC-04P.4-LANGUAGE.md#term-array) |
| <a id="term-bootstrap"></a>bootstrap | The building of the compiler from its source text and a C toolchain, with no cint compiler built earlier. | [SPEC-09P.3 3.18](SPEC-09P.3-TOOLCHAIN.md#term-bootstrap) |
| <a id="term-borrow"></a>borrow | The use of host memory by cint code without a copy. | [SPEC-03P.3 3.12](SPEC-03P.3-HOST-INTERFACE.md#term-borrow) |
| <a id="term-boundary"></a>boundary | A point of a run at which the state of its context is defined and its state hash is taken: the end of an entry that ran, or, at dispatch density, the publication of a kernel dispatch. | [SPEC-01P.3 3.16](SPEC-01P.3-ENCODINGS.md#term-boundary) |
| <a id="term-effect-class"></a>effect class | What a call of a function or an extern may do outside the program: `pure`, `observe` or `unrestricted`. | [SPEC-04P.4 3.45](SPEC-04P.4-LANGUAGE.md#term-effect-class) |
| <a id="term-extern"></a>extern | A function that a module declares with `extern` and does not define; a host program or a standard service supplies it. | [SPEC-04P.4 3.46](SPEC-04P.4-LANGUAGE.md#term-extern) |
| <a id="term-grant-set"></a>grant set | The capabilities given to a run, fixed before the program starts; a program holds it as a value of type `Grants`. | [SPEC-04P.4 3.49](SPEC-04P.4-LANGUAGE.md#term-grant-set) |
| <a id="term-host-function"></a>host function | A function of a host program that cint code calls through an extern declaration. | [SPEC-03P.3 3.22](SPEC-03P.3-HOST-INTERFACE.md#term-host-function) |
| <a id="term-host-import-descriptor"></a>host import descriptor | The table a library carries that lists, for each module of the program, the externs that the module declares, with each one's name, effect class, parameter count and type signature. | [SPEC-03P.3 3.23](SPEC-03P.3-HOST-INTERFACE.md#term-host-import-descriptor) |
| <a id="term-service"></a>service | A named, versioned set of externs that the runtime supplies, such as `fs/1`. | [SPEC-04P.4 3.47](SPEC-04P.4-LANGUAGE.md#term-service) |
| boundary | A point of a run at which the state of its context is defined and its state hash is taken, such as the end of an entry that ran (SPEC-01P.3, 3.16). | [SPEC-06P.3 3.7](SPEC-06P.3-TOOLS.md#term-boundary) |
| <a id="term-bounding-interval"></a>bounding interval | The smallest interval of element offsets that contains a view's addressed storage. | [SPEC-02P.2 3.16](SPEC-02P.2-KERNELS.md#term-bounding-interval) |
| <a id="term-buffer"></a>buffer | A run of elements of one type in one placement, registered with a context. | [SPEC-03P.3 3.10](SPEC-03P.3-HOST-INTERFACE.md#term-buffer) |
| <a id="term-call-depth"></a>call depth | The number of user-function calls active at one time in an entry. | [SPEC-04P.4 3.22](SPEC-04P.4-LANGUAGE.md#term-call-depth) |
| <a id="term-canonical"></a>canonical | Designated, by a named and versioned rule, as the one representation of a content or the one selection among several. | [SPEC-01P.3 3.1](SPEC-01P.3-ENCODINGS.md#term-canonical) |
| <a id="term-canonical-fault"></a>canonical fault | The fault a dispatch reports: the least of the faults it could raise, in the order of 5.5. | [SPEC-02P.2 3.11](SPEC-02P.2-KERNELS.md#term-canonical-fault) |
| <a id="term-canonical-outcome"></a>canonical outcome | What implementations compare for one entry: the output bytes, the kind of outcome, the returned value, the fuel consumed and the fault record, or, for a rejected program, the diagnostic code and position. | [SPEC-09P.3 3.12](SPEC-09P.3-TOOLCHAIN.md#term-canonical-outcome) |
| <a id="term-canonical-representation"></a>canonical representation | The machine-independent form of a context's state, built from identifiers, typed values and declared lengths. | [SPEC-03P.3 3.16](SPEC-03P.3-HOST-INTERFACE.md#term-canonical-representation) |
| <a id="term-capability"></a>capability | Of a program: the right to use a group of service externs, possibly limited to a root, written as capability text such as `fs.read:/data`. Of an executor: a set of SIR node families, forms and types that the executor carries out with their exact meaning. | [SPEC-04P.4 3.48](SPEC-04P.4-LANGUAGE.md#term-capability), [SPEC-09P.3 3.9](SPEC-09P.3-TOOLCHAIN.md#term-capability) |
| <a id="term-capability-profile"></a>capability profile | The record of what an executor admits and what it refuses. | [SPEC-02P.2 3.19](SPEC-02P.2-KERNELS.md#term-capability-profile) |
| <a id="term-case"></a>case | A recording that also holds the source text of every module of its program, so that the run can be shown, replayed and compared on another machine with no other file. | [SPEC-01P.3 3.15](SPEC-01P.3-ENCODINGS.md#term-case) |
| case | A recording that also holds the source text of every module of its program (SPEC-01P.3, 3.15). | [SPEC-06P.3 3.9](SPEC-06P.3-TOOLS.md#term-case) |
| <a id="term-checked-operator"></a>checked operator | An operator that gives the exact result when it is in the range of the result type and otherwise raises a fault. | [SPEC-04P.4 3.5](SPEC-04P.4-LANGUAGE.md#term-checked-operator) |
| <a id="term-checkpoint"></a>checkpoint | The canonical bytes of a state, in an envelope that names the runtime contract, from which a context can be restored. | [SPEC-01P.3 3.11](SPEC-01P.3-ENCODINGS.md#term-checkpoint) |
| checkpoint | The canonical bytes of a context's state, in an envelope that names the runtime contract, from which a context can be restored, as defined in SPEC-01P.3, 3.11. | [SPEC-03P.3 3.18](SPEC-03P.3-HOST-INTERFACE.md#term-checkpoint) |
| checkpoint | The canonical bytes of a state, taken between entries, from which a context can be restored (SPEC-01P.3, 3.11). | [SPEC-06P.3 3.10](SPEC-06P.3-TOOLS.md#term-checkpoint) |
| <a id="term-compiler-source-identity"></a>compiler source identity | The SHA-256 digest of the source manifest of the compiler's source files. | [SPEC-09P.3 3.17](SPEC-09P.3-TOOLCHAIN.md#term-compiler-source-identity) |
| <a id="term-configuration"></a>configuration | One choice of C toolchain, optimization level, helper mode and sanitizers under which the toolchain and the programs it compiles are built. | [SPEC-09P.3 3.16](SPEC-09P.3-TOOLCHAIN.md#term-configuration) |
| <a id="term-conformance-case"></a>conformance case | A program, or one operation, together with its expected outcome. | [SPEC-09P.3 3.10](SPEC-09P.3-TOOLCHAIN.md#term-conformance-case) |
| <a id="term-context"></a>context | One instance of a program's state, held by a host program, in which entries run one at a time: its module variables, arenas and pools, its buffer registry, its counters and its fault record (SPEC-03P.3 3.4). | [SPEC-01P.3 3.9](SPEC-01P.3-ENCODINGS.md#term-context) |
| context | One instance of a program's state, held by a host program, in which entries run one at a time: its module variables, arenas and pools, its buffer registry, its counters and its fault record. | [SPEC-03P.3 3.4](SPEC-03P.3-HOST-INTERFACE.md#term-context) |
| <a id="term-contribution"></a>contribution | The value a work-item adds to a reduction when it executes a `reduce` statement. | [SPEC-02P.2 3.5](SPEC-02P.2-KERNELS.md#term-contribution) |
| <a id="term-copy-report"></a>copy report | The list of every copy an entry made: which argument, why, its element type and its element count. | [SPEC-03P.3 3.15](SPEC-03P.3-HOST-INTERFACE.md#term-copy-report) |
| <a id="term-decoder"></a>decoder | The part of an implementation that reads the bytes of an encoding. | [SPEC-01P.3 3.8](SPEC-01P.3-ENCODINGS.md#term-decoder) |
| <a id="term-device"></a>device | A GPU on which an executor runs work-items. | [SPEC-02P.2 3.17](SPEC-02P.2-KERNELS.md#term-device) |
| <a id="term-diagnostic"></a>diagnostic | A message an implementation reports during translation when a program breaks a constraint. | [SPEC-04P.4 3.15](SPEC-04P.4-LANGUAGE.md#term-diagnostic) |
| <a id="term-digest-chain"></a>digest chain | The digest that a recording carries in its last event, computed from the state hash of its first checkpoint and of each boundary in turn, so that equal chains mean equal states, and equal fault records, at every boundary. | [SPEC-01P.3 3.17](SPEC-01P.3-ENCODINGS.md#term-digest-chain) |
| <a id="term-dispatch"></a>dispatch | One call of a kernel over an iteration space. | [SPEC-04P.4 3.29](SPEC-04P.4-LANGUAGE.md#term-dispatch) |
| <a id="term-dispatch-number"></a>dispatch number | The position of a dispatch among the dispatches of its entry, counted from 0. | [SPEC-02P.2 3.8](SPEC-02P.2-KERNELS.md#term-dispatch-number) |
| <a id="term-domain-string"></a>domain string | The string, of the form `<profile>/<kind>/v<n>`, that begins each binary encoding of this document and the input of every digest, and names its encoding and version, such as `cint-core-1/fault/v2`. | [SPEC-01P.3 3.3](SPEC-01P.3-ENCODINGS.md#term-domain-string) |
| <a id="term-effect"></a>effect | A request that an entry makes of its host. | [SPEC-01P.3 3.13](SPEC-01P.3-ENCODINGS.md#term-effect) |
| <a id="term-emitted-c"></a>emitted C | The C17 source text that the compiler writes for a program. | [SPEC-09P.3 3.6](SPEC-09P.3-TOOLCHAIN.md#term-emitted-c) |
| <a id="term-encoding"></a>encoding | A rule that maps a content, such as a value or a fault record, to bytes. | [SPEC-01P.3 3.2](SPEC-01P.3-ENCODINGS.md#term-encoding) |
| <a id="term-entry"></a>entry | One run of a program from its entry point: the top-level statements of a script, the function `main`, a `test` block or an exported function called from a host program. | [SPEC-04P.4 3.20](SPEC-04P.4-LANGUAGE.md#term-entry) |
| <a id="term-entry-check"></a>entry check | A check a dispatch makes in its entry phase, before any work-item runs. | [SPEC-02P.2 3.7](SPEC-02P.2-KERNELS.md#term-entry-check) |
| <a id="term-entry-fault"></a>entry fault | A fault raised by the checks an entry makes on its arguments after they were formed and before any of its code runs. | [SPEC-03P.3 3.9](SPEC-03P.3-HOST-INTERFACE.md#term-entry-fault) |
| <a id="term-error"></a>error | A value of an error set, returned by a function to report an expected failure. | [SPEC-04P.4 3.13](SPEC-04P.4-LANGUAGE.md#term-error) |
| <a id="term-error-set"></a>error set | A type declared with `error`, whose values are named errors. | [SPEC-04P.4 3.13](SPEC-04P.4-LANGUAGE.md#term-error-set) |
| <a id="term-error-union"></a>error union | An *error union* `E!T` is a function result that holds a `T` or a value of the error set `E`. | [SPEC-04P.4 3.13](SPEC-04P.4-LANGUAGE.md#term-error-union) |
| <a id="term-exact-result"></a>exact result | The result of an arithmetic operation computed in Z, before any check against the range of the result type. | [SPEC-04P.4 3.4](SPEC-04P.4-LANGUAGE.md#term-exact-result) |
| <a id="term-execution-identity"></a>execution identity | The canonical form of the semantic inputs of one entry. | [SPEC-01P.3 3.22](SPEC-01P.3-ENCODINGS.md#term-execution-identity) |
| <a id="term-executor"></a>executor | The part of an implementation that runs a translated program. | [SPEC-04P.4 3.19](SPEC-04P.4-LANGUAGE.md#term-executor) |
| <a id="term-exit-status"></a>exit status | The integer a tool returns to the operating system when it ends, from the table of 5.2. | [SPEC-06P.3 3.2](SPEC-06P.3-TOOLS.md#term-exit-status) |
| <a id="term-expectation"></a>expectation | The expected outcome of a conformance case, written in the `.expect` format of 9.3. | [SPEC-09P.3 3.11](SPEC-09P.3-TOOLCHAIN.md#term-expectation) |
| <a id="term-export"></a>export | An exported function or kernel of the root module, which a host program calls through its wrapper. | [SPEC-03P.3 3.5](SPEC-03P.3-HOST-INTERFACE.md#term-export) |
| <a id="term-fault"></a>fault | The stop of a run at an operation that cannot give a correct result, such as an overflow, a division by zero or an index outside an array. | [SPEC-04P.4 3.9](SPEC-04P.4-LANGUAGE.md#term-fault) |
| <a id="term-fault-code"></a>fault code | The name of the kind of a fault, such as `E_OVERFLOW`. | [SPEC-04P.4 3.10](SPEC-04P.4-LANGUAGE.md#term-fault-code) |
| <a id="term-fault-record"></a>fault record | The description of a fault that an implementation produces: its fault code, the operation, the operands, the exact result, the bound that was crossed, the source position and the identity of the program. | [SPEC-04P.4 3.11](SPEC-04P.4-LANGUAGE.md#term-fault-record) |
| <a id="term-fault-word"></a>fault word | The 64-bit location in device memory that holds the least index of a work-item that faulted on the device. | [SPEC-02P.2 3.21](SPEC-02P.2-KERNELS.md#term-fault-word) |
| <a id="term-fixpoint"></a>fixpoint | The state in which the compiler, built from the C that it emitted for its own source, emits that same C again. | [SPEC-09P.3 3.19](SPEC-09P.3-TOOLCHAIN.md#term-fixpoint) |
| <a id="term-frame-arena"></a>frame arena | The storage that holds the local arrays of active function calls, counted in elements. | [SPEC-04P.4 3.23](SPEC-04P.4-LANGUAGE.md#term-frame-arena) |
| <a id="term-front-end"></a>front end | The part of the compiler that reads source text, checks it and produces SIR. | [SPEC-09P.3 3.2](SPEC-09P.3-TOOLCHAIN.md#term-front-end) |
| <a id="term-fuel"></a>fuel | A count of steps (calls, loop iterations, reductions and kernel dispatches) that an entry is charged as it runs. | [SPEC-04P.4 3.21](SPEC-04P.4-LANGUAGE.md#term-fuel) |
| <a id="term-golden"></a>golden | An output, or the text of a whole outcome, saved from a run whose output was checked once, kept with the program and compared byte for byte with the output of each later run of the program on the same inputs. | [SPEC-04P.4 3.32](SPEC-04P.4-LANGUAGE.md#term-golden) |
| <a id="term-handle"></a>handle | A checked reference to an object in an arena or a pool. | [SPEC-04P.4 3.30](SPEC-04P.4-LANGUAGE.md#term-handle) |
| <a id="term-helper"></a>helper | A function of the runtime that performs one operation of one type and reports its fault, such as `cint_add_i64`. | [SPEC-09P.3 3.8](SPEC-09P.3-TOOLCHAIN.md#term-helper) |
| <a id="term-host"></a>host | The processor that runs a program's sequential code. | [SPEC-02P.2 3.17](SPEC-02P.2-KERNELS.md#term-host) |
| <a id="term-host-program"></a>host program | A program written in another language, here C or Python, that calls cint code through the interface of this document. | [SPEC-03P.3 3.1](SPEC-03P.3-HOST-INTERFACE.md#term-host-program) |
| <a id="term-identity"></a>identity | A SHA-256 digest, written as 64 lowercase hexadecimal digits, that names a program, a file or a state. | [SPEC-06P.3 3.14](SPEC-06P.3-TOOLS.md#term-identity) |
| <a id="term-implementation"></a>implementation | A set of programs that translates and runs cint programs. | [SPEC-04P.4 3.17](SPEC-04P.4-LANGUAGE.md#term-implementation) |
| <a id="term-iteration-space"></a>iteration space | The set of index tuples given by a kernel's `over` clause. | [SPEC-02P.2 3.1](SPEC-02P.2-KERNELS.md#term-iteration-space) |
| <a id="term-kernel"></a>kernel | A function that runs once for each point of an iteration space, writing its results to output views that only that point may write. | [SPEC-04P.4 3.28](SPEC-04P.4-LANGUAGE.md#term-kernel) |
| <a id="term-launch"></a>launch | One start of device code over a range of work-item indices. | [SPEC-02P.2 3.20](SPEC-02P.2-KERNELS.md#term-launch) |
| <a id="term-lease"></a>lease | A period during which the runtime hands the host a read-only address of storage the runtime owns. | [SPEC-03P.3 3.13](SPEC-03P.3-HOST-INTERFACE.md#term-lease) |
| <a id="term-library"></a>library | The shared library that `cint build --lib` writes for a program whose root module has no `main` and no top-level statements. | [SPEC-03P.3 3.2](SPEC-03P.3-HOST-INTERFACE.md#term-library) |
| <a id="term-machine-form"></a>machine form | One JSON object, named by its `schema` key, written when the tool is given `--json`. | [SPEC-06P.3 3.4](SPEC-06P.3-TOOLS.md#term-machine-form) |
| <a id="term-module"></a>module | One source file. | [SPEC-04P.4 3.25](SPEC-04P.4-LANGUAGE.md#term-module) |
| <a id="term-node"></a>node | One operation of SIR, such as one checked addition or one call. | [SPEC-09P.3 3.4](SPEC-09P.3-TOOLCHAIN.md#term-node) |
| <a id="term-object"></a>object | Storage that holds a value: a variable, an element of an array or a field of a struct. | [SPEC-04P.4 3.33](SPEC-04P.4-LANGUAGE.md#term-object) |
| <a id="term-operation-identifier"></a>operation identifier | The name of an operation in a fault record, such as `add.checked.i64`. | [SPEC-04P.4 3.12](SPEC-04P.4-LANGUAGE.md#term-operation-identifier) |
| <a id="term-output-set"></a>output set | The files that `cint emit-c` writes for a program: the C of each module and of the program, the intermediate representation of each module as text, and a manifest of their digests. | [SPEC-06P.3 3.5](SPEC-06P.3-TOOLS.md#term-output-set) |
| <a id="term-owned-block"></a>owned block | The elements of an `out` or `inout` array argument that one work-item may write: those whose leading indices are the work-item's index tuple. | [SPEC-02P.2 3.3](SPEC-02P.2-KERNELS.md#term-owned-block) |
| <a id="term-ownership"></a>ownership | Which party frees a buffer's storage: the context, the runtime for a buffer it created, the Python package for a `Buffer` or the host program for borrowed memory. | [SPEC-03P.3 3.11](SPEC-03P.3-HOST-INTERFACE.md#term-ownership) |
| <a id="term-payload"></a>payload | The bytes of a value without its tag. | [SPEC-01P.3 3.5](SPEC-01P.3-ENCODINGS.md#term-payload) |
| <a id="term-phase"></a>phase | One of the three parts of a dispatch: *entry* (numbered 0), in which the arguments are checked; *work-item*, in which the work-items run; and *epilogue*, in which the reductions are completed. | [SPEC-02P.2 3.6](SPEC-02P.2-KERNELS.md#term-phase) |
| <a id="term-placement"></a>placement | The memory, host or device, that holds a view's storage. | [SPEC-02P.2 3.18](SPEC-02P.2-KERNELS.md#term-placement) |
| <a id="term-pool"></a>pool | A fixed set of slots that are taken and released one at a time. | [SPEC-04P.4 3.30](SPEC-04P.4-LANGUAGE.md#term-pool) |
| <a id="term-presence-byte"></a>presence byte | A byte `00`, which marks an optional field as absent, or `01`, which marks it present and is followed by the field. | [SPEC-01P.3 3.7](SPEC-01P.3-ENCODINGS.md#term-presence-byte) |
| <a id="term-project-directory"></a>project directory | A directory that holds a program's modules, either directly or in a subdirectory `src`, with the root module `main.ci`. | [SPEC-06P.3 3.6](SPEC-06P.3-TOOLS.md#term-project-directory) |
| <a id="term-publication"></a>publication | The moment at which the new contents of a dispatch's outputs become visible, which is when the dispatch completes without a fault. | [SPEC-03P.3 3.14](SPEC-03P.3-HOST-INTERFACE.md#term-publication) |
| <a id="term-publish"></a>publish | To make every output of a dispatch visible at once. | [SPEC-02P.2 3.13](SPEC-02P.2-KERNELS.md#term-publish) |
| <a id="term-raw-value"></a>raw value | The integer `r` of the storage type of a fixed-point type that stands for the value `r / 2^f`. | [SPEC-04P.4 3.36](SPEC-04P.4-LANGUAGE.md#term-raw-value) |
| <a id="term-receipt"></a>receipt | A canonical JSON file that records one run of a tool: its inputs, its outcome and what it observed (SPEC-09P.3, 3.14). | [SPEC-06P.3 3.12](SPEC-06P.3-TOOLS.md#term-receipt) |
| receipt | A canonical JSON file that records one run of a tool: its inputs, its outcome and what it observed. | [SPEC-09P.3 3.14](SPEC-09P.3-TOOLCHAIN.md#term-receipt) |
| <a id="term-receipt-identity"></a>receipt identity | The SHA-256 digest of the canonical bytes of a receipt's inputs and outcome. | [SPEC-09P.3 3.15](SPEC-09P.3-TOOLCHAIN.md#term-receipt-identity) |
| <a id="term-record-kind"></a>record kind | Whether a fault record is a *run-time* record, produced by an entry, or a *compile-time* record, produced when a constant expression faults during translation. | [SPEC-01P.3 3.21](SPEC-01P.3-ENCODINGS.md#term-record-kind) |
| <a id="term-recording"></a>recording | A file that holds the identity and code of a program, a checkpoint of a context when recording began, and the events that followed, in order: each entry with its arguments, its effects and its outcome and the checkpoints, restores, cleared faults and changes of revision between entries. | [SPEC-01P.3 3.14](SPEC-01P.3-ENCODINGS.md#term-recording) |
| recording | A file that holds a program's identity and code, a checkpoint of a context when recording began and the events that followed, from which the run can be replayed, as defined in SPEC-01P.3, 3.14. | [SPEC-03P.3 3.19](SPEC-03P.3-HOST-INTERFACE.md#term-recording) |
| recording | A file that holds a program, a checkpoint of a context when recording began and the events that followed, from which the run can be replayed (SPEC-01P.3, 3.14). | [SPEC-06P.3 3.8](SPEC-06P.3-TOOLS.md#term-recording) |
| <a id="term-reflection-table"></a>reflection table | The table a library carries that describes each export: its name, kind, effect class, parameter names and type signature and its wrapper. | [SPEC-03P.3 3.20](SPEC-03P.3-HOST-INTERFACE.md#term-reflection-table) |
| <a id="term-refusal"></a>refusal | The report by an implementation that it will not run a valid program, or a kernel dispatch, because it cannot execute it exactly. | [SPEC-04P.4 3.16](SPEC-04P.4-LANGUAGE.md#term-refusal) |
| <a id="term-refused-call"></a>refused call | A call whose arguments cannot be formed into cint values. | [SPEC-03P.3 3.8](SPEC-03P.3-HOST-INTERFACE.md#term-refused-call) |
| <a id="term-replay"></a>replay | Running the entries of a recording again from its initial state with the recorded inputs, and comparing each boundary with the recording. | [SPEC-06P.3 3.11](SPEC-06P.3-TOOLS.md#term-replay) |
| <a id="term-result-form"></a>result form | A built-in operation, such as `add_result`, that returns an error union instead of raising the fault its checked operator would raise. | [SPEC-04P.4 3.8](SPEC-04P.4-LANGUAGE.md#term-result-form) |
| <a id="term-revision"></a>revision | The identity of a program, a digest computed as SPEC-01P.3 specifies. | [SPEC-04P.4 3.31](SPEC-04P.4-LANGUAGE.md#term-revision) |
| <a id="term-runtime"></a>runtime | The C code, declared in `rt/cint_rt.h` and `rt/cint_rt_host.h` and built as the library `cint_rt`, that holds contexts, buffers and fault records for compiled cint code, and that emitted C and the executor `cpu-sir-interp` call for checked arithmetic, fuel and call depth. | [SPEC-03P.3 3.3](SPEC-03P.3-HOST-INTERFACE.md#term-runtime) |
| runtime | The C code declared in `rt/cint_rt.h` and `rt/cint_rt_host.h` and built as the library `cint_rt`, which emitted C and the interpreter `cint-interp` call for checked arithmetic, fuel, call depth and fault records (SPEC-03P.3, 3.3). | [SPEC-09P.3 3.7](SPEC-09P.3-TOOLCHAIN.md#term-runtime) |
| <a id="term-runtime-contract-version"></a>runtime contract version | The name of the interface between compiled code and the runtime library, `cint-rt-3` in cint 0.2.4. | [SPEC-01P.3 3.18](SPEC-01P.3-ENCODINGS.md#term-runtime-contract-version) |
| <a id="term-saturating-operator"></a>saturating operator | An operator spelled with a `\|` suffix after an arithmetic operator (`+\|`, `-\|`, `*\|`), whose result is `sat(T, v)` for the exact result `v`. | [SPEC-04P.4 3.7](SPEC-04P.4-LANGUAGE.md#term-saturating-operator) |
| <a id="term-semantic-input"></a>semantic input | A value that decides the outcome of an entry: the program, its input values, the effects the host supplied to it, the fuel allowance, the call-depth limit and the frame-arena capacity. | [SPEC-04P.4 3.24](SPEC-04P.4-LANGUAGE.md#term-semantic-input) |
| <a id="term-sequential-code"></a>sequential code | The code of a program outside kernel bodies. | [SPEC-04P.4 3.35](SPEC-04P.4-LANGUAGE.md#term-sequential-code) |
| <a id="term-session"></a>session | One run of the workbench: a sequence of entries and commands typed at its prompt, with the state they produce. | [SPEC-06P.3 3.13](SPEC-06P.3-TOOLS.md#term-session) |
| <a id="term-sir"></a>SIR | The semantic intermediate representation, the form of a checked program that every executor of the toolchain runs. | [SPEC-09P.3 3.3](SPEC-09P.3-TOOLCHAIN.md#term-sir) |
| <a id="term-sir-text"></a>SIR text | The canonical text form of one module's intermediate representation, specified by SPEC-09P.3. | [SPEC-01P.3 3.19](SPEC-01P.3-ENCODINGS.md#term-sir-text) |
| <a id="term-site"></a>site | A node that can fault, identified by its module and its ordinal within the module, with the source position and the operation identifier that a fault record reports for it. | [SPEC-09P.3 3.5](SPEC-09P.3-TOOLCHAIN.md#term-site) |
| <a id="term-size-parameter"></a>size parameter | A name, bound at each call or dispatch, for an extent of an array or view argument; inside the body it is a read-only `I64`. | [SPEC-04P.4 3.34](SPEC-04P.4-LANGUAGE.md#term-size-parameter) |
| <a id="term-source-map"></a>source map | The sites of every module of a program, each with its ordinal, line, column and operation identifier. | [SPEC-01P.3 3.20](SPEC-01P.3-ENCODINGS.md#term-source-map) |
| <a id="term-staging"></a>staging | Storage that holds the outputs of a dispatch until they are published. | [SPEC-02P.2 3.12](SPEC-02P.2-KERNELS.md#term-staging) |
| <a id="term-state"></a>state | The canonical content of a context between entries: the values of its module-level variables, its arenas and pools, its registry of buffers, its identifier counters, the fault record it holds if any, and the count of effects it has completed. | [SPEC-01P.3 3.10](SPEC-01P.3-ENCODINGS.md#term-state) |
| <a id="term-state-hash"></a>state hash | The SHA-256 digest of the canonical bytes of a state. | [SPEC-01P.3 3.12](SPEC-01P.3-ENCODINGS.md#term-state-hash) |
| state hash | The SHA-256 digest of the canonical representation of a context's state, as defined in SPEC-01P.3, 3.12. | [SPEC-03P.3 3.17](SPEC-03P.3-HOST-INTERFACE.md#term-state-hash) |
| <a id="term-status"></a>status | The `int32_t` value a function of the C interface returns to say how a call ended. | [SPEC-03P.3 3.7](SPEC-03P.3-HOST-INTERFACE.md#term-status) |
| <a id="term-step-ordinal"></a>step ordinal | The position of an operation among the counted operations a work-item has evaluated, counted from 0. | [SPEC-02P.2 3.9](SPEC-02P.2-KERNELS.md#term-step-ordinal) |
| <a id="term-storage"></a>storage | The elements a view reaches into: an array, an arena or a region registered through the host interface. | [SPEC-02P.2 3.14](SPEC-02P.2-KERNELS.md#term-storage) |
| <a id="term-storage-integer"></a>storage integer | The integer `r` of the storage type of a fixed-point type that stands for the value `r / 2^f`. | [SPEC-04P.4 3.36](SPEC-04P.4-LANGUAGE.md#term-storage-integer) |
| <a id="term-tag"></a>tag | The bytes, beginning with one byte, that name the type of a value where values of several types can occur. | [SPEC-01P.3 3.4](SPEC-01P.3-ENCODINGS.md#term-tag) |
| <a id="term-tagged-value"></a>tagged value | A tag followed by a payload. | [SPEC-01P.3 3.6](SPEC-01P.3-ENCODINGS.md#term-tagged-value) |
| <a id="term-text-form"></a>text form | Written for a person to read. | [SPEC-06P.3 3.4](SPEC-06P.3-TOOLS.md#term-text-form) |
| <a id="term-the-reference"></a>the reference | The implementation `cint_ref`, written to follow this document directly, whose results are the expected results of the conformance suite. | [SPEC-04P.4 3.18](SPEC-04P.4-LANGUAGE.md#term-the-reference) |
| <a id="term-tool"></a>tool | The `cint` command, or one of the commands that it starts: `cint interp`, `cint workbench`, `cint repl`, `cint explain` and `cint debug`. | [SPEC-06P.3 3.1](SPEC-06P.3-TOOLS.md#term-tool) |
| <a id="term-tool-error"></a>tool error | The refusal by a tool of its command line or its environment, such as an unknown option or an unreadable file. | [SPEC-06P.3 3.3](SPEC-06P.3-TOOLS.md#term-tool-error) |
| <a id="term-toolchain"></a>toolchain | The programs of cint 0.2.4 that translate, run and check cint programs: the compiler `cintc`, the `cint` command and its subcommands, the interpreter `cint-interp`, the runtime and the conformance tools under `tools/` and `conformance/tools/`. | [SPEC-09P.3 3.1](SPEC-09P.3-TOOLCHAIN.md#term-toolchain) |
| <a id="term-translation"></a>translation | Everything an implementation does with a program before it runs: reading the source text, checking the constraints, and compiling or preparing the program. | [SPEC-04P.4 3.14](SPEC-04P.4-LANGUAGE.md#term-translation) |
| <a id="term-type"></a>type | The set of values an object may hold and the operations defined on them. | [SPEC-04P.4 3.3](SPEC-04P.4-LANGUAGE.md#term-type) |
| <a id="term-type-signature"></a>type signature | The canonical bytes that describe the parameter modes, parameter types and result type of an export or an extern. | [SPEC-03P.3 3.21](SPEC-03P.3-HOST-INTERFACE.md#term-type-signature) |
| <a id="term-uniform"></a>uniform | A value in a kernel body is *uniform* when it is the same for every work-item of a dispatch, and *varying* otherwise. | [SPEC-02P.2 3.4](SPEC-02P.2-KERNELS.md#term-uniform) |
| <a id="term-value"></a>value | What an object holds or an expression produces, together with its type. | [SPEC-04P.4 3.2](SPEC-04P.4-LANGUAGE.md#term-value) |
| <a id="term-varying"></a>varying | A value in a kernel body is *uniform* when it is the same for every work-item of a dispatch, and *varying* otherwise. | [SPEC-02P.2 3.4](SPEC-02P.2-KERNELS.md#term-varying) |
| <a id="term-view"></a>view | A typed, shaped window onto array storage, given by its element type, its rank, an extent and a stride for each dimension, and whether it may be written. | [SPEC-04P.4 3.27](SPEC-04P.4-LANGUAGE.md#term-view) |
| <a id="term-work-item-index"></a>work-item index | The position of a work-item's index tuple in the row-major order of the iteration space, counted from 0. | [SPEC-02P.2 3.2](SPEC-02P.2-KERNELS.md#term-work-item-index) |
| <a id="term-wrapper"></a>wrapper | The C function of a library that checks a host's arguments, runs one export as one entry and returns a status. | [SPEC-03P.3 3.6](SPEC-03P.3-HOST-INTERFACE.md#term-wrapper) |
| <a id="term-wrapping-operator"></a>wrapping operator | An operator spelled with a `%` suffix after an arithmetic operator (`+%`, `-%`, `*%`, `<<%` and unary `-%`), whose result is the exact result reduced modulo 2 to the power of the type's width into the type's range. | [SPEC-04P.4 3.6](SPEC-04P.4-LANGUAGE.md#term-wrapping-operator) |
| <a id="term-writer"></a>writer | The part of an implementation that produces the bytes of an encoding. | [SPEC-01P.3 3.8](SPEC-01P.3-ENCODINGS.md#term-writer) |
| <a id="term-z"></a>Z | The set of mathematical integers, of unbounded size. | [SPEC-04P.4 3.1](SPEC-04P.4-LANGUAGE.md#term-z) |
