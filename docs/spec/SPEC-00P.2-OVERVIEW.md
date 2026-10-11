# SPEC-00P.2: Overview

SPEC-00P.2, edition 2 of the overview of the cint specifications, cint v0.2.0, 2026-10-11.

This edition replaces SPEC-00P.1.

<a id="foreword"></a>

## Foreword

This document is the overview of the specifications published with cint 0.2.0: what cint is, how the documents fit together, where to start reading, and which claims cint makes, with their limits. It is informative. It states no rule of its own, and where it summarizes a rule, the document that states the rule governs.

<a id="what-cint-is"></a>

## 1 What cint is

cint is a programming language for computations whose results must be exact and the same on every machine. It is a C-family language: declarations name the type first, and functions, blocks, statements and most operators are written as in C. Source files end in `.ci`. It rests on six mechanisms:

- **Integers only.** Every value is an integer of a declared type, a `Bool`, or an array or struct of them. The integer types are `I8` to `I64`, `U8` to `U64` and the wide types `I128` to `I1024` and `U128` to `U512`; fixed-point types such as `Q16.16`, with 16 integer bits, counting the sign bit, and 16 fraction bits, hold multiples of 2^-16 as integers. There is no floating-point type (SPEC-04P.2 6.3).
- **Every plain operator is checked.** `a + b` gives the exact sum when it fits the type and faults when it does not. Wrapping and saturating arithmetic have their own operators, `a +% b` and `a +| b` and `add_result(a, b)` returns an error value instead of faulting. The spelling alone decides; no option changes it (SPEC-04P.2 6.7).
- **No implicit conversions.** A value changes type only through a written conversion: `as` is checked, `as%` wraps and `as?` returns an error value. A literal has no type of its own and takes the one its context requires (SPEC-04P.2 6.5, 6.6).
- **A fault stops the program with a full record.** A *fault* stops a run at an operation that cannot give a correct result. Its *fault record* names the operation, each operand with its type, the exact result, the bound crossed, the source position and the calls that led there. Program code cannot catch a fault. An expected failure is an *error* instead: a value that a function returns and its caller handles with `try` or `catch` (SPEC-04P.2 5.2.3, 6.11).
- **Three executors and a reference.** `cpu-c17` compiles a program to C17 and builds it with the host's C compiler. `cpu-sir-interp`, the interpreter, runs the compiler's intermediate form directly. `gpu-cuda` runs *kernels* on NVIDIA GPUs. A kernel is a function applied once to each point of an index space, and its run for one point is a *work-item*. *The reference*, `cint_ref`, is a separate implementation in Python whose results are the expected results of the conformance suite (SPEC-09P.1 5).
- **One outcome on every implementation.** A run has no hidden inputs, such as the time or memory addresses. Given the same program and inputs, every conforming implementation writes the same output bytes, ends with the same fault record or none, and consumes the same *fuel*, a count of calls, loop iterations, reductions over arrays such as `sum`, and kernel dispatches that can also bound a run (SPEC-04P.2 5.2.4, 8.2).

A source file whose statements stand at its top level is a *script*, and its statements run in order. In the examples, a string on a line of its own is a print statement: it writes the string, with each `{...}` replaced by the value of the expression inside. Here are the four spellings of one sum of two `U8` values, whose range is 0 to 255:

```cint forms.ci
// forms.ci: one sum of two U8 values, in each spelling.
U8 a = 200;
U8 b = 100;
"wrapping:   {a +% b}\n";
"saturating: {a +| b}\n";
U8 c = add_result(a, b) catch 0;
"result:     {c}\n";
"checked:    {a + b}\n";
```

```text
wrapping:   44
saturating: 255
result:     0
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> forms.ci:8:17
   |
 8 | "checked:    {a + b}\n";
   |                 ^ add.checked.u8
   |
   = operation: add.checked.u8
   = operand-count: 2
   = operand: U8 200
   = operand: U8 100
   = exact:   300
   = limit:   U8 255
...
```

The exact sum is 300. It wraps to 44 and saturates at 255, and `add_result` gives the error `ArithError.overflow`, which `catch 0` replaces with 0. The checked `+` faults, and its print statement writes nothing. A line `...` in shown output stands for lines left out.

Two types in one sum are rejected before the program runs:

```cint mixed.ci
// mixed.ci: an I32 and an I64 in one sum.
I32 a = 5;
I64 b = 7;
"{b + a}\n";
```

```text
error[C2001]: type mismatch
  --> mixed.ci:4:7
   |
 4 | "{b + a}\n";
   |       ^
```

A conversion of one operand fixes it: `b + (a as I64)`.

<a id="a-first-program"></a>

## 2 A first program

Functions are declared as in C. This script computes two factorials in `I64`:

```cint factorial.ci
// factorial.ci: n! computed in I64.
I64 factorial(I64 n) {
    I64 f = 1;
    for k in 1..=n {
        f = f * k;
    }
    return f;
}

"10! = {factorial(10)}\n";
"20! = {factorial(20)}\n";
```

`cint run factorial.ci` prints:

```text
10! = 3628800
20! = 2432902008176640000
```

`cint run` compiles the script to C, builds it and runs it. `for k in 1..=n` runs its body for each `k` from 1 through `n`.

The factorial of 21, 51090942171709440000, does not fit in `I64`. A third line asks for it:

```cint factorial21.ci
// factorial21.ci: one step past the largest factorial I64 holds.
I64 factorial(I64 n) {
    I64 f = 1;
    for k in 1..=n {
        f = f * k;
    }
    return f;
}

"10! = {factorial(10)}\n";
"20! = {factorial(20)}\n";
"21! = {factorial(21)}\n";
```

```text
10! = 3628800
20! = 2432902008176640000
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> factorial21.ci:5:15
   |
 5 |         f = f * k;
   |               ^ mul.checked.i64
   |
   = operation: mul.checked.i64
   = operand-count: 2
   = operand: I64 2432902008176640000
   = operand: I64 21
   = exact:   51090942171709440000
   = limit:   I64 9223372036854775807
...
   = stack-depth: 1
   = called from: factorial21.ci:12:9
   = fuel-consumed: 55 (not part of the fault record)
   = state: the program stopped; stdout holds what it wrote before the fault
```

The program wrote two lines. The third print statement wrote nothing, because the expression inside its `{...}` faulted. The report, on the standard error, reads:

- `fault[E_OVERFLOW]`: the fault code and its fixed message.
- `-->` and the excerpt: the position `factorial21.ci:5:15`, the 15th character of line 5, which is the `*` of `f = f * k`.
- `operation`: `mul.checked.i64`, the checked product of two `I64` values.
- `operand`: both operands, exact and typed: the factorial of 20, and 21.
- `exact` and `limit`: the true product, and `I64.max`, the bound it crossed.
- `called from`: the call in line 12 that led there.
- `fuel-consumed` and `state` follow the record and are not part of it.

The program does not continue with a wrapped value. SPEC-04P.2 8.1 explains every line of a report.

<a id="the-documents"></a>

## 3 The documents

The specifications of cint 0.2.0 are these editions:

| Edition | Title | What it covers | Kind |
|---|---|---|---|
| SPEC-00P.2 | Overview | This document | Informative |
| SPEC-01P.2 | Canonical encodings and identity | The bytes of values, fault records, program state, checkpoints and recordings; the identities of a program and of a run | Normative |
| SPEC-02P.1 | Kernels and executors | What an executor does to carry out a kernel dispatch, and what each executor of 0.2.0 runs and refuses | Normative |
| SPEC-03P.1 | The host interface: C and Python | How C and Python programs call cint code, lend it memory, and checkpoint and record it | Normative |
| SPEC-04P.2 | The cint language | The language and its built-in operations, faults and fuel, and how to find out why a program did what it did | Normative; Annexes B to F informative |
| SPEC-06P.1 | The cint tools | The commands, outputs and exit statuses of `cint`, the interpreter and the debug tools | Normative |
| SPEC-09P.1 | Toolchain and conformance | The compiler's intermediate form, the C it emits, what implementations agree on, the conformance suite, the bootstrap, receipts and the constructs that the tools of 0.2.0 refuse | Normative; clause 12 informative |
| GLOSSARY-P.2 | Glossary | Every defined term, with the edition and section that define it | Informative |
| REFERENCES-P.2 | References | The published works that the editions cite | Informative |

SPEC-04P.2 depends on no other document, so a reader learning the language needs only it. Each of the others serves one kind of reader, as section 4 shows.

**Editions.** An edition is named by the document's number, `P` for public, and the edition's number: SPEC-04P.2 is the second edition of SPEC-04, and SPEC-02P.1 the first of SPEC-02. Each document counts its own editions.

- Each minor release, such as 0.2.0, publishes an edition of every document. A patch release, such as 0.2.1, publishes the next edition only of a document whose rules or listed refusals it changes.
- A released edition's text does not change. A correction that changes no meaning is published as an erratum; a change of meaning is a new edition.
- Each rule ends with its *clause identifier*, such as IM-106 or LS-17. The identifier stays with the rule when the rule moves to another section or document, and it is not reused: IM-106, the fields of a fault record, was in SPEC-01P.1 and is in SPEC-04P.2 8.1. Section numbers belong to one edition.
- Every rule has an anchor named by its clause identifier, and every section an anchor named by its title, so a link keeps working in each edition that keeps the rule or the section.
- A citation gives the edition, the section and the identifier: SPEC-04P.2 8.1, IM-106.

<a id="reading-orders"></a>

## 4 Reading orders

Each list is an order to read in, not everything worth reading.

A learner reads in this order:

1. Sections 1, 2 and 6 of this overview.
2. SPEC-04P.2, Introduction; 5.1.2, scripts and programs; 6.3.1, integer types; 6.5, literals; 6.7.6, the additive operators; 6.6, conversions.
3. SPEC-04P.2 6.10, statements; 7.5, formatting; 8.1, reading a fault report; 8.5, a first session with the debug tools.
4. SPEC-04P.2 6.9, declarations; 6.11, errors and faults; 6.12, arrays and views; 6.15.3, tests.

A C programmer reads in this order:

1. SPEC-04P.2 Annex D, cint for C and Python programmers and Annex C, what cint does where C leaves behavior undefined, and what it leaves out.
2. SPEC-04P.2 6.6, conversions, none of them implicit; 6.7.1, precedence, where the bitwise operators bind tighter than in C; 6.7.7, division, which rounds toward negative infinity.
3. SPEC-04P.2 6.12, arrays and views; 6.14, arenas, pools and handles; 6.11, errors.
4. SPEC-09P.1 7, the C that the compiler emits and SPEC-03P.1 7, calling cint code from C.

A Python user reads in this order:

1. SPEC-04P.2 Annex D, then the learner's order.
2. SPEC-03P.1 8, the Python module, in particular 8.2, borrowing arrays without a copy, and 8.6, a complete example with NumPy.
3. SPEC-04P.2 6.13, kernels and SPEC-03P.1 6.4, outputs and publication.
4. SPEC-04P.2 8.4 and SPEC-03P.1 9, checkpoints, recordings and replay.

An implementer reads in this order:

1. SPEC-04P.2 clauses 3 to 7 in order, with Annex A, the grammar and Annex B, the codes.
2. SPEC-04P.2 8.1 and 8.2, the fault record and what implementations agree on.
3. SPEC-02P.1, the executors and SPEC-01P.2, the encodings and identities.
4. SPEC-09P.1 8, agreement; 9, the conformance suite; 12, the refusals of the tools of 0.2.0; 10, the bootstrap.
5. SPEC-06P.1 7, the forms of reports and SPEC-03P.1 7, the C interface.

<a id="what-0-2-0-includes-and-what-it-does-not"></a>

## 5 What 0.2.0 includes and what it does not

The language of cint 0.2.0 is what SPEC-04P.2 states and the reference runs:

- the integer types from `I8` to `I1024` and from `U8` to `U512`, the fixed-point types, `Bool`, structs, error sets and error unions;
- the checked, wrapping and saturating operators, the result forms, the conversions `as`, `as%` and `as?` and the built-in operations of SPEC-04P.2 clause 7, such as `muldiv`, `div_round` and `sum`;
- variables and constants; functions with `in` and `inout` parameters; `if`, `while`, `for` over a range, `switch`, labeled `break` and `continue`, `defer` and `errdefer`, `assert`, and print statements with format specifications;
- arrays and views of rank 1 to 4, with checked indexing and slicing; kernels and their reductions; arenas, pools and handles;
- scripts, programs with `main`, importable modules with imports and exports and tests.

The tools of cint 0.2.0 are the `cint` command with `run`, `build`, `test` and `emit-c`; the interpreter, `cint interp`; `cint workbench`, `cint explain` and `cint debug`; the C interface and the Python module of SPEC-03P.1; and the reference.

The `cint` command of 0.2.0 does not build every construct that the reference runs. For such a construct it reports, when it translates the program, a refusal diagnostic such as C9102, and it does not run the program with another meaning. On an implementation that runs the construct, its meaning is the one SPEC-04P.2 states. SPEC-09P.1 clause 12 lists each refused construct with its code and position, and the operations that `gpu-cuda` refuses in a kernel with the fault `E_UNSUPPORTED`.

Some features are in no implementation of cint 0.2.0, the reference included. Among them are string variables and formatting into a string, enumerations, tuples, type aliases, attributes, bit fields, the `do` loop, `for` over the elements of an array, default arguments, functions that return views, reading input from files or the terminal, schedules for kernels, dispatches that write their outputs in place, the type `U1024` and executors other than the three of section 1. Each document's Future directions clause describes those of its area: SPEC-04P.2 6.16, SPEC-01P.2 10, SPEC-02P.1 12, SPEC-03P.1 11, SPEC-06P.1 13 and SPEC-09P.1 13.

<a id="finding-out-why"></a>

## 6 Finding out why

When a program faults, or prints something unexpected, SPEC-04P.2 clause 8 shows how to find out why. This section is a short tour of it, with the programs of section 2 and this module:

```cint
// fact.ci: n! in I64, as a module with two tests.
export I64 factorial(I64 n) {
    I64 f = 1;
    for k in 1..=n {
        f = f * k;
    }
    return f;
}

test "20! fits in I64" {
    assert(factorial(20) == 2432902008176640000);
}

test "21! does not" expect_fault E_OVERFLOW {
    _ = factorial(21);
}
```

`fact.ci` has only declarations, so it is an *importable module*, which runs nothing by itself. `export` lets code outside the module call `factorial`, and each `test` block states one expectation about it (SPEC-04P.2 5.1.2, 6.15).

<a id="the-fault-record"></a>

### 6.1 The fault record

The report of section 2 prints one fault record. Besides the fields read there, the record holds the revision of the program text, and it holds no time, no memory address and no name of a machine or an executor, so one fault gives one record everywhere. A fault in a kernel dispatch also has an address: the dispatch number, the work-item index and the step. When several work-items fault, the record describes the least of them in a fixed order, whatever order they ran in, so a GPU reports the same fault as the C executor (SPEC-04P.2 8.1, SPEC-02P.1 5.5).

<a id="agreement"></a>

### 6.2 Agreement

The reference, `cint_ref`, runs the same files. It runs as `python3 -m cint_ref` with the `ref` directory of the cint source tree on `PYTHONPATH`, and `--format 5` selects the newest form of its outcome text (SPEC-09P.1 9.3):

```console
$ python3 -m cint_ref run --format 5 --stdout ref.out factorial.ci
case factorial
format 5
source reference
outcome value
stdout-bytes 40
stdout-sha256 30b3b4dbc6ca79e5fd6c441e533d6027db1fda245cabfcd3c986ea96020d5606
fuel-consumed 33
$ cint run factorial.ci | cmp - ref.out && echo same
same
```

The reference prints the outcome, not the output: the run ended normally after writing 40 bytes, whose SHA-256 digest is shown, and consumed 33 units of fuel, 1 for the script, 1 for each of the two calls and 1 for each of the 30 iterations. `--stdout` saved the output bytes, and `cmp` finds them equal to those of `cint run`.

It runs `factorial21.ci` to the same end:

```console
$ python3 -m cint_ref run --format 5 factorial21.ci
case factorial21
format 5
source reference
outcome fault
stdout-bytes 40
stdout-sha256 30b3b4dbc6ca79e5fd6c441e533d6027db1fda245cabfcd3c986ea96020d5606
fuel-consumed 55
fault.code E_OVERFLOW
fault.operation mul.checked.i64
fault.operand I64 2432902008176640000
fault.operand I64 21
fault.exact 51090942171709440000
fault.limit I64 9223372036854775807
fault.position factorial21.ci:5:15
fault.revision self
fault.source-map self
fault.address none
fault.stack-depth 1
fault.stack factorial21.ci:12:9
```

The output is the 40 bytes of `factorial.ci`, since the third print wrote nothing. The fuel is the 55 of the report: 1 for the script, 1 for each of the three calls and 51 for the iterations. The fault matches the report field for field, except that the reference writes `self` for the revision and the source map, since it does not compute them. The reference shares no code with the compiler, so each checks the other, and any difference between them is a defect in one of them (SPEC-04P.2 8.2, SPEC-09P.1 8.1).

<a id="golden-outputs-and-tests"></a>

### 6.3 Golden outputs and tests

A *golden*, or golden output, is an output checked once, saved, and compared byte for byte with the output of each run after it: `cint run factorial.ci > factorial.golden` saves one, and `diff` compares. Every implementation gives the same outcome, so a golden made with one checks a run on any other. A test keeps its expectation inside the module, and `cint test` runs each test from a fresh state:

```console
$ cint test fact.ci
test fact.ci:10 "20! fits in I64" ... ok
test fact.ci:14 "21! does not" ... ok
2 of 2 tests passed
```

The second test passes because `factorial(21)` faults `E_OVERFLOW`, the fault it names; it would fail if the call returned a value (SPEC-04P.2 8.3, 6.15.3).

<a id="checkpoints-and-replay"></a>

### 6.4 Checkpoints and replay

An *entry* is one run of a program from one of its entry points, such as a call of an exported function by a host program, and the end of an entry is a *boundary*. In a `cint workbench` session each line typed is an entry, and every entry is recorded. A *checkpoint* saves the program's state at a boundary, and *replay* runs recorded entries again and compares each outcome with the recording:

```console
$ cint workbench fact.ci
...
ci[1]> :load fact.ci
loaded fact (fact.ci)
ci[2]> fact.factorial(20)
= 2432902008176640000 : I64
ci[3]> I64 p = fact.factorial(19);
p = 121645100408832000 : I64
ci[4]> :checkpoint p19
checkpoint p19   #1   bindings 1   revisions: session@r1, fact@r1
ci[4]> p = p * 20;
ci[5]> p = p * 21;
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> entry 5:1:7
...
   = state: no store completed in this entry; context reset
...
ci[6]> p
= 2432902008176640000 : I64
ci[7]> :replay p19
replaying 3 entries from p19 to the current state (entry 4 to entry 6), comparing prints and outcomes with the recording
entry 5: E_OVERFLOW reproduced, its fault record equal to the recorded one
replay matches the recording at 3 boundaries
ci[7]> :restore p19
saved current state as auto-1
restored p19   #1
ci[7]> p
= 121645100408832000 : I64
ci[8]> :quit --yes
```

- Loading the module is entry 1, and its function is named `fact.factorial`. An expression entry prints its value and its type.
- `I64 p` is a variable of the session, kept from entry to entry. `:checkpoint p19` saves the state while `p` holds the factorial of 19.
- Entry 5 faults. No store of it completed, so `p` still holds the factorial of 20, and the session clears the fault before the next entry.
- `:replay p19` restores the checkpoint, runs entries 4 to 6 again and finds every outcome equal to the recorded one, the fault record included. `:restore p19` makes the checkpoint's state current again.

The tools go back in time by replay from a checkpoint; they do not run a program backward (SPEC-04P.2 8.4).

<a id="the-debug-tools"></a>

### 6.5 The debug tools

`cint explain` shows what the compiler knows about a function without running it. Each *fault site*, an operation that can fault, has an identifier:

```console
$ cint explain fact.ci:factorial
module fact.ci   revision f0d0bba7a6264e17cd2556c3618e861ca5750392885362dcf3ad62407252ed69

function factorial   fact.sites:3   pure
  signature  (in n: I64) -> I64
  fault sites (3): 3 possible, 0 statically excluded
    factorial#1  fact.ci:2:12  fuel.charge      E_FUEL possible
    factorial#2  fact.ci:4:5   fuel.charge      E_FUEL possible
    factorial#3  fact.ci:5:15  mul.checked.i64  E_OVERFLOW possible
...
```

`factorial#1` is the fuel charge of each call, `factorial#2` that of each iteration and `factorial#3` the `*` that faulted in section 2. `pure` says only that the function prints nothing and calls nothing that prints.

`cint debug locate` runs an exported function as an entry, under a recording, and finds the first fault. It prints the report and the last counted operations before the fault, and `--save-case` keeps the run as a *case*, a recording that runs on another machine with the tools alone. `cint debug diff` runs a case on `cpu-c17` and on `cpu-sir-interp` and compares them at every boundary:

```console
$ cint debug locate fact.ci --entry factorial --args I64 21 --save-case f21.case
program    fact.ci   evaluator cpu-sir-interp, recorded as the case f21.case
target     boundary 1 of 1, factorial(I64 21): the first fault, reproduced by replay
bracket    the initial state; then boundary 1
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> fact.ci:5:15
...
   = site: factorial#3
...
  ord  position           operation              operands                 value
...
   37                     add.wrap.i64           19, 1                    20
   38  fact.ci:5:15       mul.checked.i64        121645100408832000, 20   2432902008176640000
   39                     add.wrap.i64           20, 1                    21
   40  fact.ci:5:15       mul.checked.i64        2432902008176640000, 21  E_OVERFLOW
$ cint debug diff f21.case
case       f21.case   1 boundary   density entry
reference  cpu-c17
candidate  cpu-sir-interp
boundaries 1 of 1 compared
outcome    agree
```

The `site` line names `factorial#3`. Each row of the trace is one counted operation with its exact operands and value: the last two products before the fault and, between them, the step of the loop index `k`, which has no position of its own. `agree` says that both executors reached the same state and the same fault record; here `reference` names the executor that the other is compared with, not the reference implementation. SPEC-04P.2 8.5 continues with a first session and the commands a learner uses first, and SPEC-06P.1 defines every command.

<a id="claims-and-evidence"></a>

## 7 Claims and evidence

Each claim is stated briefly, with the rules that carry it and its limits. Where a sentence here and a rule differ, the rule holds. Figures that support a claim come from the receipts published with cint 0.2.0, and SPEC-09P.1 11.3 shows how to run one again and compare the result.

1. **Exact or a fault.** Every plain arithmetic operator, and the checked conversion `as`, gives the exact result or faults, and no option changes what a spelling does.
   - Rules: SPEC-04P.2 6.7, IM-28 and IM-31; 6.7.6, IM-30; 6.6, IM-50.
   - Limits: the wrapping and saturating forms do not fault on overflow, but they do fault on a shift count out of range and on a divisor of 0.
2. **No implicit conversion.** A value changes type only through a conversion written in the program.
   - Rules: SPEC-04P.2 6.6, IM-9 and IM-50; 6.5, LS-56.
   - Limits: a literal takes its type from its context, which types the literal and converts no value.
3. **No undefined behavior.** Within the scope of SPEC-04P.2, no behavior is undefined, unspecified or implementation-defined.
   - Rules: SPEC-04P.2 4, LS-273; Annex C, IM-128.
   - Limits: the scope excludes code reached through the host interface and the memory it changes; defects in an implementation, in the C it emits or in the C compiler; hardware faults; and exhaustion of host resources that an implementation cannot detect (SPEC-04P.2 4, LS-274).
4. **A fault ends the entry with a complete record.** A fault ends the entry at once and gives one record of its operation, operands, exact result, bound and position, with no time, address or executor in it, and program code cannot catch it.
   - Rules: SPEC-04P.2 5.2.3, IM-118; 8.1, IM-106 and IM-111.
   - Limits: stores that completed before the fault remain, because sequential code is not rolled back (SPEC-04P.2 5.2.3, IM-121 and IM-122).
5. **Determinism.** Runs with the same semantic inputs write the same output bytes, end with the same fault record or none, and consume the same fuel, on every conforming implementation, executor and machine.
   - Rules: SPEC-04P.2 4, LS-272; 8.2, IM-159 and IM-160.
   - Limits: a run ended by a failure of host resources, such as memory running out, is outside the claim.
6. **Kernels.** A kernel dispatch reports one fault, chosen by an order of phase, work-item index and step that does not depend on the order in which the work-items ran, and it publishes its outputs only if the whole dispatch completes without a fault.
   - Rules: SPEC-02P.1 5.5, F-3 and F-4; 7.1, P-1; SPEC-04P.2 8.1, IM-112 and IM-115.
   - Limits: code outside cint that reads borrowed host memory during a dispatch is outside the rule (SPEC-02P.1 7.1, P-2).
7. **Agreement with a separate reference.** For a program that the reference runs and the compiler builds without a refusal, the two write the same output, consume the same fuel, and end with the same fault record, except its revision and source-map digest; the reference shares no code, tables or grammar with the compiler or the runtime.
   - Rules: SPEC-04P.2 8.2 and SPEC-09P.1 8.1, REF-06; SPEC-09P.1 5.4, REF-01.
   - Limits: agreement on one program is a measurement of that program alone, and a refused case is listed by name, not counted as agreement (SPEC-09P.1 8.2, CONF-14). Both implementations are written from the same text, so an ambiguity in it can reach both; cases derived by hand check for that (SPEC-09P.1 5.4, REF-02).
   - Evidence: on `cpu-c17`, 24 configurations each compare 1,868,066 records from 658 programs with no disagreement, with 11 cases held, 67 not applicable and 47 unsupported; `cpu-sir-interp` gives the same counts on 23 configurations. cint 0.2.0 claims no figure of agreement for `gpu-cuda` (SPEC-09P.1 Annex A).
8. **The run matrix.** Every conformance case runs on the reference and on every build of the run matrix, and any disagreement fails the run.
   - Rules: SPEC-09P.1 8.4, CONF-03; 8.3, CONF-04.
   - Limits: the matrix is GCC and Clang on Linux x86-64, Apple Clang on macOS AArch64 and MSVC on Windows x86-64, each at two optimization levels.
   - Evidence: the 24 configurations of `cpu-c17` share one receipt identity; with `cpu-sir-interp`, 44 of 48 configurations pass with one receipt identity for each executor, and four did not complete (SPEC-09P.1 Annex A).
9. **Emitted C.** The compiler emits C17 with no floating-point type. Its bytes depend only on the program, the compiler's source, the profile and the executor's options, and its behavior does not depend on the C compiler's optimization level. With the runtime, it evaluates no operation whose behavior C17 leaves undefined.
   - Rules: SPEC-09P.1 7.2, EMIT-01; 7.7, EMIT-21; 7.6, EMIT-19 and EMIT-20; 11.4, DISC-03.
   - Limits: a program that exhausts the C stack is outside the claim. The evidence for the last part is the conformance suite run under UndefinedBehaviorSanitizer and AddressSanitizer with no report, which covers the programs run, and those sanitizers do not detect every kind of undefined behavior.
   - Evidence: 23 sanitized configurations of GCC and Clang on Linux x86-64 report no finding (SPEC-09P.1 Annex A).
10. **The compiler compiles itself.** The compiler, built from the C it emitted for its own source, emits that C again byte for byte; the two generations emit the same C for every program of the corpus; and that C does not depend on which toolchain of the run matrix built the compiler.
    - Rules: SPEC-09P.1 10.4, FIX-01 and FIX-03; 10.5, FIX-04.
    - Limits: emitted C is compared, not executables.
    - Evidence: the fixpoint passes on MSVC, GCC, Clang and Apple Clang with one receipt identity (SPEC-09P.1 Annex A).
11. **Replay reproduces a run.** Replaying a `full` recording reproduces, at every entry, the status, the fault record, the fuel consumed, the output and the state hash.
    - Rules: SPEC-03P.1 9.3, H-16.
    - Limits: replay covers the state of the cint program, not host code that runs between entries (SPEC-03P.1 9.3, H-20), and it compares outcomes at boundaries, not inside an entry (SPEC-04P.2 8.4).

These are not claimed:

- **Compliance with a safety or coding standard,** such as MISRA C or the SEI CERT C Coding Standard. Such a claim would need a documented assessment against named editions and rules (SPEC-04P.2 4, LS-275; SPEC-09P.1 11.4, DISC-02).
- **Anything about memory that code outside cint changes** while cint code uses it, or about what host code computes (SPEC-04P.2 4, LS-274; SPEC-03P.1 4 and 8.4).
- **Executables reproducible byte for byte.** The emitted C is reproducible; the executables built from it differ between toolchains and may differ between two builds with one toolchain (SPEC-09P.1 10.4, FIX-05).
- **That the fixpoint ties an executable to its source.** The fixpoint compares emitted C. It is not diverse double-compiling, and it does not show that an executable corresponds to the compiler's source (SPEC-09P.1 10.5, FIX-04).
- **Numerical adequacy.** An exact and repeatable result can still be a poor model of the problem it computes, for example where integers replace a floating-point computation (SPEC-04P.2 8.2, IM-160).
- **Outcomes outside the run matrix,** on other C compilers or machines (SPEC-09P.1 8.4, CONF-03).
- **Speed.** These documents make no claim about speed. A speed figure published with a release names its workload, machine, toolchain, flags and method of measurement (SPEC-09P.1 11.4, DISC-04).
- **Correctness from agreement.** Two implementations can agree and both be wrong. Agreement shows that outcomes are equal; the specifications decide which outcome is correct (SPEC-04P.2 8.2).

<a id="the-name"></a>

## 8 The name

The language is spelled cint, in lowercase, also at the start of a sentence. It is not related to `CINT`, the C and C++ interpreter of the ROOT data analysis framework from CERN.

<a id="prior-work"></a>

## 9 Prior work

cint takes most of its syntax from C, as ISO/IEC 9899:2018 defines it. Its arithmetic follows languages that put the behavior on overflow in the program text. Zig writes wrapping and saturating addition as `+%` and `+|`, the spellings cint uses and cint's error sets and error unions, with `try`, `catch`, `defer` and `errdefer`, are adapted from Zig as well. Rust gives its integer types checked, wrapping and saturating arithmetic as named methods, such as `checked_add`.

A kernel is written for one work-item, as in the single-program, multiple-data model of ISPC and the rule that each work-item writes only its own part of the outputs draws on Futhark's in-place updates. Finding a fault by recording a run and replaying it has precedent in record-and-replay debuggers such as rr. Top-level statements that run in order, with no `main` function needed, come from Terry A. Davis's HolyC. REFERENCES-P.2 lists each of these works.

<a id="license"></a>

## 10 License

The cint source code, these specifications and the receipts published with a release are licensed under the Apache License, Version 2.0, with the LLVM exception: SPDX identifier `Apache-2.0 WITH LLVM-exception`. The file `LICENSE` at the root of the source tree holds the terms.
