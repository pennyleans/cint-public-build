# SPEC-02P.2: Kernels and executors

SPEC-02P.2, edition 2 of the kernels and executors specification, cint v0.2.3, 2026-10-23.

<a id="foreword"></a>

## Foreword

This document specifies what an executor of cint 0.2.2 does to carry out a kernel dispatch, and how each of the three executors of that release does it. It is the companion of SPEC-04P.3, whose clause 6.13 states what a kernel and a dispatch mean to a program.

Each rule ends with one or more clause identifiers in brackets. An identifier, such as F-5 or B-16a, names one rule across editions and documents: a rule keeps its identifier when its text moves to another section or another document, and an identifier is never reused. Code comments, test files and other documents cite rules by these identifiers. Identifiers that begin with `A`, `P` or `X` name rules of kernels, here and in SPEC-04P.3; in SPEC-03P.2 the same letters name rules of the host interface, which are other rules. A rule that another edition states is cited in brackets with that edition's name, such as [SPEC-04P.3 X-1]: the rule is stated there, and this document says what an executor does to keep it. Section numbers belong to this edition only.

This edition replaces SPEC-02P.1. It differs from that edition in the capability profile of `gpu-cuda` (10.5): a kernel that holds a fixed-point value is refused with the capability `fixed`, and the saturating conversion `as|` with `sat`. A reduction over a fixed-point type of 128-bit storage uses the accumulators of 9.1 as an integer of that width does. No rule moved and no identifier changed.

<a id="introduction"></a>

## Introduction

A kernel is written for one point of an iteration space, and a dispatch runs it for every point. SPEC-04P.3 6.13 defines the meaning of a dispatch by a sequential procedure: check the arguments, run the work-items one at a time in ascending order, complete the reductions and publish the outputs only if nothing faulted. An executor that runs work-items in another order, or many at once on a GPU, has to reach the outcome of that procedure by other means: the same output bytes, the same reduction results, the same fuel consumed and, when something faults, the same fault record, down to the work-item and the operation inside it.

This first example dispatches one kernel. `scale` multiplies each element of `a` by `k`; the program runs it twice:

```cint scale.ci
kernel scale[n](in I64[n] a, in I64 k, out I64[n] b) over [i: n] {
    b[i] = a[i] * k;
}

void run() {
    I64[4] a = [1, -2, 3, 4];
    I64[4] b;
    scale(a, 3, b);
    "{b[0]} {b[1]} {b[2]} {b[3]}\n";
    a[1] = 4611686018427387904;
    a[3] = -4611686018427387904;
    scale(a, 3, b);
    "{b[0]} {b[1]} {b[2]} {b[3]}\n";
}

run();
```

```text
3 -6 9 12
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> scale.ci:2:17
   |
 2 |     b[i] = a[i] * k;
   |                 ^ mul.checked.i64
   |
   = operation: mul.checked.i64
   = operand-count: 2
   = operand: I64 4611686018427387904
   = operand: I64 3
   = exact:   13835058055282163712
   = limit:   I64 9223372036854775807
   ...
   = address: scale.scale, dispatch 1, phase 1, work-item 1, step 2
   ...
```

In the second dispatch, work-items 1 and 3 both overflow. The fault reported is the one at the lower work-item, 1, whichever an executor meets first. Its address names the kernel `scale` of module `scale`, the second dispatch of the entry (numbered from 0), the work-item phase, work-item 1 and step 2: the place `b[i]` is step 0, the read `a[i]` step 1 and the product step 2. Nothing of the faulted dispatch is published, so `b` still holds `3 -6 9 12`. Every executor of cint 0.2.2 is required to report this same record (clause 4).

<a id="how-this-document-is-organized"></a>

### How this document is organized

Clauses 1 to 4 give the scope, the references, the terms and what conformance means. Clause 5 describes a dispatch: its phases, its number, the checks it makes before any work-item runs, the step ordinal, and the choice and content of the fault it reports. Clause 6 gives the test that decides whether the arguments of a dispatch may overlap. Clause 7 states how outputs are held back until a dispatch succeeds. Clause 8 states how a work-item is evaluated and charged fuel, and clause 9 how reductions are computed so that their results do not depend on the order of evaluation. Clause 10 states the obligations of every executor and describes `cpu-c17`, `cpu-sir-interp` and `gpu-cuda`. Clause 11 works one kernel through the rules on all three. Clause 12 lists directions that cint 0.2.2 does not implement.

Subclauses follow the shape of SPEC-04P.3: a summary, then **Semantics**, **Faults**, **Examples** and **Notes** as a subclause needs them. Examples and notes are informative. Every example is a complete program, and its output is shown exactly as cint 0.2.2 prints it on `cpu-c17`; the reference gives the same outcome. Where a tool of that release refuses an example (4, paragraph 4), the output shown is the reference's. A line `...` in shown output stands for lines left out. Sessions of the tools are shown in console blocks, with each command after `$ ` and each entry of a session after its prompt.

<a id="scope"></a>

## 1 Scope

1. This document specifies, for programs in `cint-core-1`, the language profile that SPEC-04P.3 defines:
   - the phases of a dispatch, the numbering of dispatches and the checks a dispatch makes before any work-item runs, in their order;
   - the step ordinal, and which of several faults in one dispatch is reported;
   - the test that decides whether two arguments of a dispatch may overlap;
   - the staging and publication of outputs;
   - the evaluation of a work-item and the fuel it is charged;
   - the evaluation of reductions inside kernels and the attribution of their faults;
   - what every executor shall and shall not do, and what each executor of cint 0.2.2 runs and refuses.
2. This document does not specify:
   - the form and meaning of kernels, arrays and views as a program sees them, which SPEC-04P.3 6.12 and 6.13 specify;
   - the host interface through which a host program registers memory, places it on a device and issues dispatches, which SPEC-03P.2 specifies;
   - the byte encodings of fault records, which SPEC-01P.3 specifies;
   - the intermediate representation, the C that the compiler emits and the conformance suite, which SPEC-09P.2 specifies;
   - speed. No performance figure is given, and none is claimed.

<a id="normative-references"></a>

## 2 Normative references

The following documents are referred to in the text in such a way that some or all of their content constitutes requirements of this document. For dated references, only the edition cited applies.

- ISO/IEC 9899:2018, *Information technology: Programming languages: C* (C17). Used for the C that `cpu-c17` emits and the C17 component `cpu-sir-interp`.
- NVIDIA. *Parallel Thread Execution ISA*, version 9.4. Used for the device code of `gpu-cuda`: sections 9.7.1.3 (`mul`), 9.7.2.1 and 9.7.2.2 (`add.cc`, `addc`) and 9.7.15.5 (`atom`).
- NVIDIA. *CUDA Driver API Reference Manual*, version 13.4. Used for section 6.27, Module Management, through which `gpu-cuda` loads its device code.

The other cint documents of the same release, SPEC-04P.3, SPEC-01P.3, SPEC-03P.2 and SPEC-09P.2, are named in the text where they hold detail.

<a id="terms-definitions-and-symbols"></a>

## 3 Terms, definitions and symbols

The terms of SPEC-04P.3 clause 3 apply, among them *value*, *fault*, *fault record*, *operation identifier*, *refusal*, *the reference*, *executor*, *entry*, *fuel*, *view*, *kernel*, *dispatch*, *work-item* and *revision*. In addition:

<a id="term-iteration-space"></a>**3.1 iteration space**: the set of index tuples given by a kernel's `over` clause. Each tuple is one work-item.

<a id="term-work-item-index"></a>**3.2 work-item index**: the position of a work-item's index tuple in the row-major order of the iteration space, counted from 0. For `over [y: h, x: w]`, the work-item `(y, x)` has index `y * w + x`.

<a id="term-owned-block"></a>**3.3 owned block**: the elements of an `out` or `inout` array argument that one work-item may write: those whose leading indices are the work-item's index tuple.

<a id="term-varying"></a><a id="term-uniform"></a>**3.4 uniform, varying**: a value in a kernel body is *uniform* when it is the same for every work-item of a dispatch, and *varying* otherwise.

<a id="term-contribution"></a>**3.5 contribution**: the value a work-item adds to a reduction when it executes a `reduce` statement.

<a id="term-phase"></a>**3.6 phase**: one of the three parts of a dispatch: *entry* (numbered 0), in which the arguments are checked; *work-item* (1), in which the work-items run; and *epilogue* (2), in which the reductions are completed.

<a id="term-entry-check"></a>**3.7 entry check**: a check a dispatch makes in its entry phase, before any work-item runs.

<a id="term-dispatch-number"></a>**3.8 dispatch number**: the position of a dispatch among the dispatches of its entry, counted from 0.

<a id="term-step-ordinal"></a>**3.9 step ordinal**: the position of an operation among the counted operations a work-item has evaluated, counted from 0 (5.4).

<a id="term-address"></a>**3.10 address**: the part of a fault record that locates a fault in a dispatch: the kernel's qualified name, the dispatch number, the phase, and, in the work-item phase, the work-item index and the step ordinal.

<a id="term-canonical-fault"></a>**3.11 canonical fault**: the fault a dispatch reports: the least of the faults it could raise, in the order of 5.5.

<a id="term-staging"></a>**3.12 staging**: storage that holds the outputs of a dispatch until they are published. A program cannot observe it.

<a id="term-publish"></a>**3.13 publish**: to make every output of a dispatch visible at once.

<a id="term-storage"></a>**3.14 storage**: the elements a view reaches into: an array, an arena, or a region registered through the host interface. Views made from one array share its storage.

<a id="term-addressed-storage"></a>**3.15 addressed storage**: the set of element offsets that a view reaches (6.2).

<a id="term-bounding-interval"></a>**3.16 bounding interval**: the smallest interval of element offsets that contains a view's addressed storage (6.2).

<a id="term-device"></a><a id="term-host"></a>**3.17 host, device**: the *host* is the processor that runs a program's sequential code; a *device* is a GPU on which an executor runs work-items.

<a id="term-placement"></a>**3.18 placement**: the memory, host or device, that holds a view's storage.

<a id="term-capability-profile"></a>**3.19 capability profile**: the record of what an executor admits and what it refuses (10.2).

<a id="term-launch"></a>**3.20 launch**: one start of device code over a range of work-item indices.

<a id="term-fault-word"></a>**3.21 fault word**: the 64-bit location in device memory that holds the least index of a work-item that faulted on the device (10.6).

<a id="symbols"></a>

### 3.22 Symbols

| Symbol | Meaning |
|---|---|
| `N` | The number of work-items of a dispatch: the product of the extents of its iteration space. |
| `k` | The index of the work-item of the canonical fault. |
| `abs(s)` | The absolute value of the integer `s`. |
| `gcd(a, b, ...)` | The greatest common divisor of the integers listed, not all 0. |
| `C(i)` | The total fuel that work-item `i` charges when it runs to its end or to its first fault. |

<a id="conformance"></a>

## 4 Conformance

1. In this document, "shall" states a requirement on an executor or on a program, and "shall not" a prohibition. "May" states a permission.
2. <a id="Q-2"></a>An executor conforms to this document when, for every dispatch it admits, it produces the outcome of the procedure of 5.1: the same output bytes, the same reduction results, the same fuel consumed, the same dispatch numbers and the same fault record as the reference. For a dispatch it does not admit, it refuses with `E_UNSUPPORTED`, during translation or in the entry phase, before any output is written and before any fuel is charged; it does not run the dispatch in a narrower or approximate form (10.1) [Q-2].
3. `cpu-c17` is the executor against which the others are compared, and every executor is compared with the reference on the conformance suite, as SPEC-09P.2 5.3 states; where an executor or the reference differs from this document, this document governs, and the difference is a defect [SPEC-09P.2 BACK-03].
4. As SPEC-04P.3 clause 4 states, an implementation may also refuse a construct that it does not implement, with a diagnostic whose code begins with `C9`. The program stays conforming, and on an implementation that runs it, its outcome is the one this document specifies. SPEC-09P.2 lists the constructs that the cint tools of each release refuse.

<a id="dispatch"></a>

## 5 Dispatch

<a id="the-phases-of-a-dispatch"></a>

### 5.1 The phases of a dispatch

A dispatch has three phases. In the entry phase it checks its arguments and charges its fuel. In the work-item phase it runs the body once for every point of the iteration space. In the epilogue it completes the reductions. Its outputs are published only after all three succeed. This subclause states the procedure whose outcome every executor gives.

**Semantics**

1. An executor shall give every dispatch the outcome of the procedure of SPEC-04P.3 6.13.6, whatever order of execution it uses, sequential or parallel. That procedure makes the entry checks of 5.3, the last of which charges the dispatch's fuel. It then runs each work-item in ascending index, as an independent sequential program charged as 8.4 states, and then completes the reductions (9.3) and publishes the outputs (clause 7). It stops at the first fault [SPEC-04P.3 X-1].
2. <a id="F-12"></a>A dispatch is synchronous: the statement after it starts only when the dispatch has published its outputs. The dispatches of an entry are carried out in program order. An executor may prepare a dispatch before the one ahead of it completes, but if the one ahead faults, the prepared one is discarded: it publishes nothing and takes no dispatch number. An executor shall not begin a dispatch that reads the outputs of an earlier dispatch until the earlier one has published them [F-12].
3. A fault in any phase ends the entry, as every fault does (SPEC-04P.3 5.2.3).

**Notes**

4. The procedure defines an outcome, not a method. `cpu-c17` and the interpreter follow it as written. `gpu-cuda` runs the work-item phase on a device, with many work-items at once, and recovers the outcome of the procedure by the protocol of 10.6.

<a id="dispatch-numbers"></a>

### 5.2 Dispatch numbers

Every dispatch of an entry has a number, its position among the dispatches of that entry. The number is part of the address of a fault in the dispatch.

**Semantics**

1. <a id="F-1"></a>Each entry counts its dispatches. The first dispatch of an entry has number 0, the next 1 and so on, in program order. Every dispatch takes the next number, whether it succeeds or faults, and a dispatch that faults in its entry phase takes one too. A dispatch that the host interface refuses before it begins, because its arguments cannot be formed into values at all, takes none (SPEC-03P.2) [F-1] [SPEC-04P.3 P-3, IM-112].
2. The count restarts at 0 at every entry. It is not part of the program's state, and a checkpoint neither saves nor restores it [F-1] [SPEC-04P.3 IM-112].
3. A fault outside any dispatch has no address at all; no dispatch number stands for "not in a kernel" [F-1].
4. <a id="F-2"></a>A dispatch has one number even when an executor splits it into several launches [F-2].

**Examples**

5. One entry runs three dispatches of one kernel. The first two double both elements; the third overflows at work-item 1:

```cint dispatch_count.ci
kernel double[n](in I64[n] a, out I64[n] b) over [i: n] {
    b[i] = a[i] * 2;
}

void run() {
    I64[2] a = [1, 1152921504606846976];
    I64[2] b;
    double(a, b);
    double(b, a);
    "{a[0]} {a[1]}\n";
    double(a, b);
}

run();
```

```text
4 4611686018427387904
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> dispatch_count.ci:2:17
   |
 2 |     b[i] = a[i] * 2;
   |                 ^ mul.checked.i64
   |
   = operation: mul.checked.i64
   = operand-count: 2
   = operand: I64 4611686018427387904
   = operand: I64 2
   = exact:   9223372036854775808
   = limit:   I64 9223372036854775807
   ...
   = address: dispatch_count.double, dispatch 2, phase 1, work-item 1, step 2
   ...
```

<a id="entry-checks"></a>

### 5.3 Entry checks

Before any work-item runs, a dispatch checks that its arguments are usable and that it can pay for its work-items. The checks run in a fixed order, so that a dispatch with several faulty arguments reports the same fault on every executor.

**Semantics**

1. <a id="F-5"></a>A dispatch makes the checks of Table 1 in the order of the table. Within a check, the arguments are taken in the order in which their parameters are declared, and the dimensions of each in order. The first check that fails gives the fault of the dispatch, in the entry phase; no further check runs and no work-item runs [F-5].

Table 1. Entry checks, in order.

| Check | What is checked | Fault | Operation | Operands | Limit |
|---|---|---|---|---|---|
| 1 | The storage of each array argument has not been released or reset since the view was made | `E_STALE_HANDLE` | `bind.stale` | parameter; the view's generation (`U64`) | the storage's current generation (`U64`) |
| 2 | Each argument of an `out` or `inout` parameter is a view that may be written | `E_ALIAS` | `bind.permission` | parameter | none |
| 3 | Each array argument has the parameter's element type | `E_UNSUPPORTED` | `bind.type` | parameter | none |
| 3 | Each array argument has the parameter's rank | `E_SHAPE` | `bind.type` | parameter; the view's rank | the declared rank |
| 4 | Every array argument is held in memory that the executor runs the dispatch on | `E_UNSUPPORTED` | `bind.placement` | the first parameter that differs | none |
| 5 | The size symbols bind, and every extent agrees (SPEC-04P.3 6.13.2) | `E_SHAPE` | `bind.shape` | parameter; dimension; the argument's extent | the extent computed from the declaration |
| 5 | An extent expression `n + c` or `n - c` stays in `I64` | `E_OVERFLOW` | `add.checked.i64`, `sub.checked.i64` | the two operands | the bound crossed |
| 5 | Each constraint of the `where` clause holds, in written order | `E_SHAPE` | `bind.where` | the constraint's index from 0; the value of its left side; the value of its right side | none |
| 6 | Each argument of an `out` or `inout` parameter passes the test of 6.4 | `E_ALIAS` | `bind.injective` | parameter | none |
| 7 | Each argument of an `out` or `inout` parameter is shown disjoint from every other array argument by the test of 6.3 | `E_ALIAS` | `bind.alias` | the `out` or `inout` parameter; the other parameter | none |
| 8 | The dispatch is within the executor's limits | `E_UNSUPPORTED` | `bind.limit` | parameter | none |
| 9 | The fuel left in the entry covers `N` units (8.4) | `E_FUEL` | `fuel.charge` | none | the entry's fuel budget |

A *generation* is a count that each storage keeps and that its release or reset advances; a view records the generation of its storage when the view is made, and check 1 compares the two. A parameter is given by its position in the declaration, counted from 0, and a dimension likewise. Every operand of a `bind` operation is an `I64` unless the table says otherwise, and no entry check has an exact result.

2. Checks 1 to 3 and 5 to 7 depend only on the program and its arguments, and decide the same way on every executor. Check 4 depends also on the memory an executor runs on, and decides the same way when the arguments are placed where each executor runs. Check 8 is a refusal by one executor: its fault is not compared between executors [F-5].
3. Check 9 comes after check 8, so a dispatch that an executor does not admit is never charged. No entry check changes state or charges fuel except check 9, whose charge is made only when every other check has passed [F-5].
4. <a id="V-12"></a>All array arguments of one dispatch shall have the same placement, and that placement shall be one the executor runs on. No executor moves program data between host and device memory to meet this rule; a program moves data explicitly, through the host interface (SPEC-03P.2). The one exception is the read-back of 10.6, which serves only to describe a fault [V-12].
5. <a id="V-13"></a>Placement is not part of any outcome. Output bytes, reduction results and fault records do not depend on it, and the descriptor of a view in a fault record (6.3) leaves it out [V-13].
6. Checks 2 and 3 fail only for a dispatch issued through the host interface: for a dispatch written in cint source, translation rejects the same mismatches with a diagnostic (SPEC-04P.3 6.13.2).

**Faults**

7. Each row of Table 1 gives the fault code, the operation identifier, the operands and the limit of the fault that its check raises. The position and the stack of every entry-phase fault are those of 5.6.

**Examples**

8. The two passes of the size binding: `n` is bound to 3 by `b`, so `a` should have extent 4. Its extent is 5, and check 5 faults on parameter 0, dimension 0:

```cint shape_rt.ci
kernel k[n](in I64[n + 1] a, out I64[n] b) over [i: n] {
    b[i] = a[i] + a[i + 1];
}

void go(I64 m) {
    I64[8] a;
    I64[8] b;
    k(a[0..5], b[0..m]);
    "done\n";
}
go(3);
```

```text
fault[E_SHAPE]: shapes do not match
  --> shape_rt.ci:8:5
   |
 8 |     k(a[0..5], b[0..m]);
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
   = address: shape_rt.k, dispatch 0, phase 0, work-item none, step none
   ...
```

9. In this example a `where` constraint is false. The constraint `n >= 2` has index 0, and its sides are 1 and 2:

```cint where_rt.ci
kernel smooth[n](in I64[n] u, out I64[n] v) over [i: n] where n >= 2 {
    v[i] = u[i];
}

void go(I64 m) {
    I64[4] a;
    I64[4] b;
    smooth(a[0..m], b[0..m]);
    "done\n";
}
go(1);
```

```text
fault[E_SHAPE]: shapes do not match
  --> where_rt.ci:8:5
   |
 8 |     smooth(a[0..m], b[0..m]);
   |     ^ bind.where
   |
   = operation: bind.where
   = operand-count: 3
   = operand: I64 0
   = operand: I64 1
   = operand: I64 2
   = exact:   none
   = limit:   none
   ...
```

<a id="step-ordinal"></a>

### 5.4 Step ordinal

The step ordinal locates a fault inside a work-item. It counts operations of the source program, not instructions of an executor, so that every executor gives the same number.

**Semantics**

1. <a id="F-6"></a>Within a work-item, each evaluation of a counted operation receives the next step ordinal, from 0, in the evaluation order of SPEC-04P.3, the place of a store before its right side; SPEC-04P.3 8.1 lists the counted operations, which include the shifts and unary minus and leave out the bitwise operators `&`, `|`, `^` and `~` [F-6] [SPEC-04P.3 IM-113].
2. An operation counts whether or not it can fault for the operands at hand, and however an executor carries it out [SPEC-04P.3 IM-114].
3. <a id="F-6a"></a>A `reduce` statement counts once, as one call of its operation, at the point where the work-item executes it [F-6a].
4. A fault at a fuel charge inside the body carries as its step ordinal the number of counted operations that the work-item evaluated before the charge (8.4) [F-6a].

**Examples**

5. In `r[i] = a[i] + b[i] * c[i]`, the place `r[i]` is step 0, the reads `a[i]`, `b[i]` and `c[i]` are steps 1 to 3, the product is step 4 and the sum step 5. Work-item 1 overflows at the sum:

```cint step5.ci
kernel muladd[n](in I64[n] a, in I64[n] b, in I64[n] c, out I64[n] r) over [i: n] {
    r[i] = a[i] + b[i] * c[i];
}

void go() {
    I64[2] a = [1, 9223372036854775807];
    I64[2] b = [2, 1];
    I64[2] c = [3, 1];
    I64[2] r;
    muladd(a, b, c, r);
    "unreachable\n";
}
go();
```

```text
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> step5.ci:2:17
   |
 2 |     r[i] = a[i] + b[i] * c[i];
   |                 ^ add.checked.i64
   |
   = operation: add.checked.i64
   = operand-count: 2
   = operand: I64 9223372036854775807
   = operand: I64 1
   = exact:   9223372036854775808
   = limit:   I64 9223372036854775807
   ...
   = address: step5.muladd, dispatch 0, phase 1, work-item 1, step 5
   ...
```

**Notes**

6. A step ordinal says which operation of a work-item faulted, and the position and the operation identifier of the record say the same in source terms. An executor that cannot count steps on its own hardware may obtain the ordinal by running the faulting work-item again (10.6).

<a id="canonical-fault-selection"></a>

### 5.5 Canonical fault selection

Several work-items of one dispatch can fault, and a work-item can fault while a reduction would also fault in the epilogue. One of these faults is reported, chosen by a rule that does not depend on the order in which an executor met them.

**Semantics**

1. <a id="F-3"></a>The faults of a dispatch are ordered by the key (phase, work-item index, step ordinal). Faults of the entry phase are ordered by the order of the checks of 5.3, and faults of the epilogue by the declaration order of the reduction targets (9.3). The canonical fault of a dispatch is the least fault under this key, and it is the one reported [F-3] [SPEC-04P.3 IM-112].
2. Across dispatches, the dispatch number orders faults. Because a fault ends the entry, the first dispatch that faults is the last dispatch of its entry [F-3].
3. <a id="F-4"></a>The order is logical. It makes no claim about which hardware thread faulted first in time, and an executor that meets faults in another order shall still report the least [F-4].

**Examples**

4. The work-item `(y, x)` of a 3 by 4 iteration space has index `4 * y + x`. Work-items `(2, 1)` and `(2, 3)`, indices 9 and 11, both divide by 0; work-item 9 is reported:

```cint index2d.ci
kernel recip[h, w](in I64[h, w] a, out I64[h, w] b) over [y: h, x: w] {
    b[y, x] = 1000 / a[y, x];
}

void run() {
    I64[3, 4] a = [[1, 2, 3, 4], [5, 6, 7, 8], [9, 0, 10, 0]];
    I64[3, 4] b;
    recip(a, b);
}

run();
```

```text
fault[E_DIV_ZERO]: division by zero
  --> index2d.ci:2:20
   |
 2 |     b[y, x] = 1000 / a[y, x];
   |                    ^ div.checked.i64
   |
   = operation: div.checked.i64
   = operand-count: 2
   = operand: I64 1000
   = operand: I64 0
   = exact:   none
   = limit:   none
   ...
   = address: index2d.recip, dispatch 0, phase 1, work-item 9, step 2
   ...
```

5. With `k = 2`, work-items 1 and 3 overflow, and the sum of the four products would also overflow `I32` in the epilogue. The work-item phase comes first, so work-item 1 is reported:

```cint mix.ci
kernel scaled_total[n](in I32[n] x, in I32 k, out I32[n] y, out I32 s) over [i: n] {
    I32 v = x[i] * k;
    y[i] = v;
    reduce s = sum(v);
}

void run(I32 k) {
    I32[4] x = [5, 1073741824, 7, 1073741824];
    I32[4] y;
    I32 s = 0;
    scaled_total(x, k, y, s);
    "{s}\n";
}

run(2);
```

```text
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> mix.ci:2:18
   |
 2 |     I32 v = x[i] * k;
   |                  ^ mul.checked.i32
   |
   = operation: mul.checked.i32
   = operand-count: 2
   = operand: I32 1073741824
   = operand: I32 2
   = exact:   2147483648
   = limit:   I32 2147483647
   ...
   = address: mix.scaled_total, dispatch 0, phase 1, work-item 1, step 1
   ...
```

6. With `k = 1`, no work-item faults and the epilogue reports the sum. Its operand is the number of contributions, and its position is the operation `sum` in the `reduce` statement:

```cint mix1.ci
kernel scaled_total[n](in I32[n] x, in I32 k, out I32[n] y, out I32 s) over [i: n] {
    I32 v = x[i] * k;
    y[i] = v;
    reduce s = sum(v);
}

void run(I32 k) {
    I32[4] x = [5, 1073741824, 7, 1073741824];
    I32[4] y;
    I32 s = 0;
    scaled_total(x, k, y, s);
    "{s}\n";
}

run(1);
```

```text
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> mix1.ci:4:16
   |
 4 |     reduce s = sum(v);
   |                ^ sum.checked.i32.i32
   |
   = operation: sum.checked.i32.i32
   = operand-count: 1
   = operand: I64 4
   = exact:   2147483660
   = limit:   I32 2147483647
   ...
   = address: mix1.scaled_total, dispatch 0, phase 2, work-item none, step none
   ...
```

<a id="the-fault-record-of-a-dispatch"></a>

### 5.6 The fault record of a dispatch

A fault in a dispatch has the record of every fault (SPEC-04P.3 8.1), with an address added. This subclause states what the address and the other fields hold in each phase.

**Semantics**

1. <a id="F-8"></a>The record of a fault in a dispatch holds, beyond the fields of every fault record, the address of the fault: the kernel's name qualified by its module, the dispatch number and the phase. A fault in the work-item phase also holds the work-item index and the step ordinal; a fault in the entry or the epilogue phase holds neither [F-8].
2. The operation, operands, exact result and limit are, in the entry phase, those of Table 1; in the work-item phase, those of the operation that faulted, as SPEC-04P.3 states for it; in the epilogue, those of the reduction (9.3) [F-8].
3. The position is: in the entry phase, the dispatch, at the kernel's name in the dispatch statement; in the work-item phase, the operator or subscript that faulted; in the epilogue, the operation named in the `reduce` statement. The stack of a work-item or epilogue fault has the dispatch as its innermost entry. The stack of an entry-phase fault is that of the code that issued the dispatch, as for a fault at the entry of a call. A dispatch is not a call: it is charged only the fuel of 8.4 [F-8].
4. Details of an executor, a device or the native code are not part of the record. A tool may show them beside it, marked as not part of the record [F-8].
5. A fault of check 7 also carries a descriptor of each of the two views (6.3).

**Notes**

6. `cint run` prints the address as one line, `= address: module.kernel, dispatch D, phase P, work-item W, step S`, with `none` for a field the phase does not have. Examples 4 to 6 of 5.5 show it for the work-item phase and the epilogue, and examples 8 and 9 of 5.3 for the entry phase. SPEC-06P.2 describes the report, and SPEC-01P.3 the encoding of the record.

<a id="aliasing-the-overlap-test"></a>

## 6 Aliasing: the overlap test

<a id="what-the-test-establishes"></a>

### 6.1 What the test establishes

Every output of a dispatch must share no element with any other array argument (SPEC-04P.3 6.13.2). This clause gives the test with which an executor establishes that, and states its limits.

**Semantics**

1. An executor establishes that an `out` or `inout` argument shares no element with another array argument by the test of 6.3, at entry check 7. As SPEC-04P.3 6.13.2 states, the test is sufficient but not exact: a pair it shows disjoint shares no element, and every other pair faults `E_ALIAS`, whether the two views overlap or not [SPEC-04P.3 A-3].
2. The same test serves every way of issuing a dispatch, and no assertion by a program that two arguments are independent is taken as evidence [SPEC-04P.3 A-3].

<a id="addressed-storage-and-bounding-interval"></a>

### 6.2 Addressed storage and bounding interval

A view reaches a set of elements of its storage. The test of 6.3 reasons about that set through its offsets and through the interval that contains it.

**Semantics**

1. A view of rank `r` has an origin, the offset in its storage of the element with all indices 0, and an extent and a stride for each dimension, and its element at an index tuple has the offset that SPEC-04P.3 6.12.4 gives, computed exactly [SPEC-04P.3 V-4].
2. The addressed storage of a view is the set of the offsets of all its index tuples. It is empty when any extent is 0. When every extent is positive, the bounding interval of the view is the closed interval `[lo, hi]`, where `lo` is the origin plus `stride * (extent - 1)` summed over the dimensions with a negative stride, and `hi` is the origin plus the same sum over the dimensions with a positive stride. It is the smallest interval that contains the addressed storage, and `lo` and `hi` are both addressed. A view is formed only when its bounding interval lies within its storage (SPEC-04P.3 6.12.4) [SPEC-04P.3 V-5].

**Examples**

3. This example lists views of `I32[8, 16] m` and of `I32[8] a`, with their origins, strides and bounding intervals:

| View | Origin | Extents | Strides | Bounding interval |
|---|---|---|---|---|
| `m[.., 0..8]` | 0 | (8, 8) | (16, 1) | [0, 119] |
| `m[.., 8..16]` | 8 | (8, 8) | (16, 1) | [8, 127] |
| `m[.., 0..16 by 2]` | 0 | (8, 8) | (16, 2) | [0, 126] |
| `m[.., 1..16 by 2]` | 1 | (8, 8) | (16, 2) | [1, 127] |
| `a` | 0 | (8) | (1) | [0, 7] |
| `reverse(a, 0)` | 7 | (8) | (-1) | [0, 7] |

<a id="the-pairwise-test"></a>

### 6.3 The pairwise test

The pairwise test applies three tests in order, each of which can show two views disjoint. A pair that none of them shows disjoint is refused.

**Semantics**

1. <a id="A-5"></a>For each `out` or `inout` parameter `p`, in declaration order, and each other array parameter `q`, in declaration order, the arguments of `p` and `q` are tested; a pair of two `out` or `inout` parameters is tested once, from the earlier of the two. The tests are applied in order, and the first that shows the pair disjoint accepts it [A-5]:
   1. *Empty test*: the view of `p` or the view of `q` has no elements.
   2. *Interval test*: the bounding intervals of the two views, taken as ranges of bytes in one memory, do not intersect. Views of two arrays that share no memory never intersect.
   3. *Gcd test*: the two views have the same storage and the same element size; `g` is the greatest common divisor of `abs(stride)` over every dimension of either view whose extent is above 1; and `g >= 2` and `origin(p) mod g` differs from `origin(q) mod g`.
2. A pair that no test of paragraph 1 shows disjoint faults `E_ALIAS` [A-5].
3. The gcd test is sound. A dimension of extent 1 adds nothing to any offset, and every other dimension adds a multiple of its stride, which is a multiple of `g`. Every offset of either view is therefore congruent to its origin modulo `g`, and two views whose origins differ modulo `g` have no offset in common [A-5].
4. <a id="A-6"></a>No more general test is made. Whether two strided views share an element is in general an integer feasibility problem, which NumPy's documentation of `numpy.shares_memory` calls NP-complete; the interval test corresponds to `numpy.may_share_memory`. A disjoint layout that these tests do not show disjoint is refused, and the remedy is a layout that they do show, or an explicit copy [A-6].

**Faults**

5. <a id="A-8"></a>`E_ALIAS` is raised at entry check 7 by the first pair, in the order of paragraph 1, that no test shows disjoint. The record holds the operation identifier `bind.alias`; two `I64` operands, the position of `p` and then that of `q`; no exact result and no limit; and a descriptor of the view of `p` and then of `q`: the element type, the rank, the origin, the extent and the stride of each dimension, and whether the view may be written. The descriptors leave out the placement. The record does not say which test failed. Its position is the dispatch [A-8].

**Examples**

6. Two arguments are two planes of one array. The bounding intervals are [0, 63] and [64, 127], and the interval test accepts the pair:

```cint alias_planes.ci
kernel copy2[h, w](in I32[h, w] u, out I32[h, w] v) over [y: h, x: w] {
    v[y, x] = u[y, x] + 1;
}

void go() {
    I32[2, 8, 8] p;
    p[0, 3, 4] = 41;
    copy2(p[0], p[1]);
    "{p[1, 3, 4]}\n";
}
go();
```

```text
42
```

7. Two arguments are the even and the odd elements of each row. The bounding intervals [0, 126] and [1, 127] intersect, but every stride is a multiple of `g = 2` and the origins are 0 and 1, so the gcd test accepts the pair:

```cint alias_even_odd.ci
kernel inc2[h, w](in I32[h, w] u, out I32[h, w] v) over [y: h, x: w] {
    v[y, x] = u[y, x] + 1;
}

void run() {
    I32[8, 16] m;
    m[2, 6] = 41;
    inc2(m[.., 0..16 by 2], m[.., 1..16 by 2]);
    "{m[2, 7]}\n";
}

run();
```

```text
42
```

8. The left and right halves of each row share no element, but no test shows it: the bounding intervals [0, 119] and [8, 127] intersect and the strides 16 and 1 give `g = 1`. The pair faults; the operands name `v` (1) and `u` (0):

```cint alias_cols.ci
kernel copy2[h, w](in I32[h, w] u, out I32[h, w] v) over [y: h, x: w] {
    v[y, x] = u[y, x] + 1;
}

void go() {
    I32[8, 16] m;
    copy2(m[.., 0..8], m[.., 8..16]);
    "done\n";
}
go();
```

```text
fault[E_ALIAS]: views overlap
  --> alias_cols.ci:7:5
   |
 7 |     copy2(m[.., 0..8], m[.., 8..16]);
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

9. An array and its reversal share every element:

```cint rev_alias.ci
kernel copy1[n](in I32[n] u, out I32[n] v) over [i: n] {
    v[i] = u[i] + 1;
}

void go() {
    I32[8] a;
    copy1(a, reverse(a, 0));
}
go();
```

```text
fault[E_ALIAS]: views overlap
  --> rev_alias.ci:7:5
   |
 7 |     copy1(a, reverse(a, 0));
   |     ^ bind.alias
   |
   = operation: bind.alias
   = operand-count: 2
   = operand: I64 1
   = operand: I64 0
   ...
```

**Notes**

10. The gcd is taken over the dimensions of both views together. Taken over one view only it would be unsound: with `p` of extents (2, 2), strides (1, 4) and origin 0 and `q` of extents (1, 2), strides (1, 4) and origin 1, the gcd over `q` alone is 4 and the origins differ modulo 4, yet both views contain the offsets 1 and 5. Over both views the gcd is 1, and the test makes no claim.
11. Some refused pairs do overlap: the same view twice, or two views of one element. Others, such as example 8, do not. The fault is the same, because the rule is about what the test shows, not about what is true.

<a id="the-write-view-test"></a>

### 6.4 The write-view test

An `out` or `inout` argument must reach a distinct element for each index tuple, or two work-items could write one element. Views made in cint source always do; a view that arrives through the host interface is checked at entry check 6.

**Semantics**

1. <a id="A-7"></a>A view with no elements passes. Otherwise, discard the dimensions of extent 1 and sort the others by `abs(stride)`, ascending, ties by ascending dimension number, as `d1, d2, ...`. The view passes when `abs(stride(d1)) >= 1` and, for every `j > 1`, `abs(stride(dj))` is at least 1 plus the sum, over every `i < j`, of `abs(stride(di)) * (extent(di) - 1)` [A-7].
2. A view that passes reaches a distinct offset for each index tuple: each dimension in the sorted order steps past every offset that the dimensions before it can reach. The test is sufficient but not exact [A-7].

**Faults**

3. `E_ALIAS` is raised at entry check 6 by the first `out` or `inout` argument that does not pass. The record holds the operation identifier `bind.injective` and one `I64` operand, the parameter's position, with no exact result and no limit. Its position is the dispatch [A-7] [SPEC-04P.3 A-4].

**Examples**

4. Row-major and column-major views pass, and so do their reversals, transposes and regular slices. The view of extents (2, 2) and strides (1, 1) fails, and it does reach offset 1 twice. The view of extents (3, 2) and strides (2, 3) reaches the distinct offsets 0, 3, 2, 5, 4 and 7, but fails, because 3 is less than `1 + 2 * 2`.

**Notes**

5. In cint source, the views a program can form all pass: slices take a step that is a positive literal, and `reverse` and `transpose` keep distinct offsets distinct.

<a id="when-the-checks-run"></a>

### 6.5 When the checks run

**Semantics**

1. When two arguments of a dispatch in cint source are the same variable, or provably the same view expression, and one of them is bound to an `out` or `inout` parameter, translation rejects the dispatch with the diagnostic C5010. Every other case is checked in the entry phase, before any work-item runs [A-8].

**Examples**

2. The same array for an `in` and an `out` parameter is rejected before the program runs:

```cint same_c5010.ci
kernel copy1[n](in I32[n] u, out I32[n] v) over [i: n] {
    v[i] = u[i] + 1;
}

void go() {
    I32[8] a;
    copy1(a, a);
}
go();
```

```text
error[C5010]: parameter mode, alias, or ABI rule
  --> same_c5010.ci:7:14
   |
 7 |     copy1(a, a);
   |              ^
```

<a id="publication"></a>

## 7 Publication

<a id="publish-on-success"></a>

### 7.1 Publish on success

A dispatch writes its outputs to staging and publishes them only when it has succeeded. A program, or a host, therefore sees either every output of a dispatch or none.

**Semantics**

1. An executor writes every output of a dispatch to staging, and publishes the outputs, or nothing, as SPEC-04P.3 6.13.4 states [SPEC-04P.3 P-1].
2. Neither a statement of the program nor a call of the host interface made after a dispatch can observe some of its outputs published and others not (SPEC-04P.3 6.13.4); code outside the program that reads borrowed host memory while a dispatch runs is outside this rule (SPEC-03P.2) [SPEC-04P.3 P-2].
3. <a id="P-11"></a>After a dispatch faults, the host observes: every `in`, `out` and `inout` argument unchanged; every variable bound to a scalar `out` unchanged; the fault record of 5.6; the fuel consumed of 8.4; the dispatch count advanced by one; and the context faulted until the host clears or restores it [P-11].
4. <a id="P-5"></a>No way of staging is observable. Publication does not change the identity of the storage or invalidate a view of it, and every view of the storage, made before or after the dispatch, reads the published elements afterward [P-5].

**Examples**

5. A `cint-workbench` session runs entries one at a time and keeps their variables, so it can show what a faulted dispatch leaves. The first dispatch publishes `total = 5`; the second faults at work-item 1, and `total` keeps 5:

```console
$ cint-workbench
...
ci[1]> kernel total_checked[n](in I64[n] x, out I64 s) over [i: n] { reduce s = fold_checked(add, 0, x[i]); }
total_checked   revision session@r1
ci[2]> I64 total = 3;
total = 3 : I64
ci[3]> I64[3] data = [5, 1, -1];
data : I64[3]   3 elements   host
  [0] = 5
  [1] = 1
  [2] = -1
ci[4]> total_checked(data, total);
ci[5]> total
= 5 : I64
ci[6]> data[0] = 9223372036854775807;
ci[7]> total_checked(data, total);
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> entry 1:1:74
   |
 1 | kernel total_checked[n](in I64[n] x, out I64 s) over [i: n] { reduce s = fold_checked(add, 0, x[i]); }
   |                                                                          ^ fold_checked.add.i64
   |
   = operation: fold_checked.add.i64
   = operand-count: 3
   = operand: I64 1
   = operand: I64 9223372036854775807
   = operand: I64 1
   = exact:   9223372036854775808
   = limit:   I64 9223372036854775807
...
   = address: session.total_checked, dispatch 0, phase 1, work-item 1, step 1
...
ci[8]> total
= 5 : I64
```

The session runs on `cpu-sir-interp`. The revision line of the report, which this transcript leaves out, names the program the positions refer to.

<a id="a-staged-inout"></a>

### 7.2 A staged `inout`

**Semantics**

1. In a dispatch, an `inout` parameter has the copy-in, copy-out meaning that SPEC-04P.3 6.13.4 states: every read of it gives the element's value from before the dispatch, even after its own work-item has written the element [SPEC-04P.3 P-4].

**Examples**

2. Each work-item writes its element twice. The second statement reads `t[i]` again and gets the value from before the dispatch, not the value the first statement wrote, so the result is ten times the old value:

```cint inout_twice.ci
kernel bump[n](inout I64[n] t) over [i: n] {
    t[i] = t[i] + 1;
    t[i] = t[i] * 10;
}

void run() {
    I64[3] t = [1, 2, 3];
    bump(t);
    "{t[0]} {t[1]} {t[2]}\n";
}

run();
```

```text
10 20 30
```

<a id="copy-out"></a>

### 7.3 Copy-out

**Semantics**

1. <a id="B-16d"></a>Every executor of cint 0.2.2 publishes every output by *copy-out*: it gives the output storage of its own for staging, of the same kind of memory as the output, and on success copies the staged elements into the output. For an `inout`, the staging starts as a copy of the elements before the dispatch. An output in device memory is staged in device memory, never through host memory [P-5, B-16d].
2. On a fault, the staging is discarded and the outputs are not touched [SPEC-09P.2 BACK-10].
3. <a id="P-5a"></a>A kernel that a host program calls directly lists each copy of publication in the copy report of its entry (SPEC-03P.2). A dispatch issued from cint source copies the same way and does not list its copies [P-5a].
4. Storage that an executor uses for its own purposes during a dispatch, such as the fault word and the scratch of 10.5 and 10.6, is not program data and is not listed as a copy [B-16d].

<a id="steps-made-of-several-dispatches"></a>

### 7.4 Steps made of several dispatches

**Semantics**

1. <a id="P-13"></a>A sequence of dispatches is not published as a unit. A step of a program that needs several dispatches gets all-or-nothing behavior by double buffering: every dispatch of the step writes into a second set of arrays, and the program uses that set as its current state only after the last dispatch of the step succeeds. If a dispatch faults, the entry ends before the exchange and the first set still holds the last complete step [P-13].

**Notes**

2. The worked example of clause 11 alternates two arrays in this way: each dispatch reads one and writes the other.

<a id="execution-of-a-work-item"></a>

## 8 Execution of a work-item

<a id="independence-of-work-items"></a>

### 8.1 Independence of work-items

**Semantics**

1. A work-item cannot observe another work-item, so every order of running the work-items, concurrent orders included, gives the same staged outputs and the same canonical fault, as SPEC-04P.3 6.13.6 states; contributions to reductions and charges of fuel are combined only by the rules of clause 9 and 8.4 [SPEC-04P.3 X-2].
2. <a id="W-1"></a>A kernel body has no memory shared between work-items, no barrier, no atomic operation and no other way for work-items to communicate [W-1].
3. <a id="W-2"></a>Because of paragraphs 1 and 2, the canonical fault can be addressed by work-item and step, and a single work-item can be run again, alone, to describe a fault (10.6) [W-2].

<a id="uniform-and-varying-values"></a>

### 8.2 Uniform and varying values

**Semantics**

1. <a id="X-3"></a>Every expression in a kernel body is uniform or varying. Size symbols and scalar `in` parameters are uniform. The over variables are varying. An expression is varying when an operand is varying, when it reads an array element at a varying index, or when it is a local variable assigned under varying control flow; local variables are classified by inference [X-3].
2. <a id="X-5"></a>The distinction never changes meaning: a varying `if`, `while` or `break` means exactly what it means in the sequential procedure of 5.1, and an executor carries it out by masking, by divergent threads, or in any other way that gives that meaning [X-5].
3. <a id="X-6"></a>The body is written for one work-item. It has no lane indices, vector types or masks. For `over [y: h, x: w]`, work-item `i` runs with `y = i / w` and `x = i % w`; for more dimensions the tuple is the row-major decomposition of `i` in the same way [X-6].

**Notes**

4. The distinction serves executors in lowering a body and tools in describing it. The single-work-item body and the distinction between uniform and varying values follow the SPMD model of ISPC (Bibliography).

<a id="evaluation-inside-a-work-item"></a>

### 8.3 Evaluation inside a work-item

**Semantics**

1. Within a work-item, statements and expressions are evaluated and checked as SPEC-04P.3 6.13.3 states, and an executor may reorder, combine or vectorize the work only when the result, including which fault occurs first and its operands, is unchanged [SPEC-04P.3 X-7].

<a id="fuel"></a>

### 8.4 Fuel

A dispatch is charged fuel in two parts: one unit for each work-item, at entry, and the charges its work-items make as they run, as sequential code would.

**Semantics**

1. Fuel is charged against the budget of the current entry, as SPEC-04P.3 6.13.6 states; there is no budget per work-item [SPEC-04P.3 X-8].
2. The dispatch charge of `N` units, one for each work-item, is made once, at entry check 9; if it would take the entry's consumed fuel above its budget, the dispatch faults `E_FUEL` in the entry phase and no work-item runs [SPEC-04P.3 X-8].
3. Inside a work-item, loop iterations and calls charge one unit each, as in sequential code, and the charges are ordered by ascending work-item index, then by evaluation order within a work-item [SPEC-04P.3 X-8].
4. The canonical `E_FUEL` fault inside a dispatch is the first charge, in the order of paragraph 3, at which the entry's consumed fuel (everything consumed before the dispatch, plus `N`, plus the charges of every lower work-item, plus the charges this work-item made before) would exceed the budget. Its step ordinal is the number of counted operations the work-item evaluated before the charge [SPEC-04P.3 X-8].
5. The fuel consumed by a dispatch is that of SPEC-04P.3 6.13.6 and SPEC-09P.2 8.5: on success, `N` plus every charge of every work-item; after a fault at work-item `k`, `N` plus the charges of work-items 0 to `k - 1` plus the charges of `k` made before its fault; after an entry-phase fault, nothing. A charge that faults `E_FUEL` is not counted, and neither is any charge made by a work-item above `k` on an executor that ran it [SPEC-04P.3 X-8] [SPEC-09P.2 BACK-09].
6. An executor that runs work-items in parallel computes the same consumption and the same canonical fault from the exact prefix sums of the total charges `C(i)` of the work-items, as SPEC-09P.2 8.5 states [SPEC-09P.2 BACK-09].
7. Running a work-item or a group of work-items again, to describe a fault, charges no fuel (SPEC-09P.2 8.5) [SPEC-09P.2 BACK-12].

**Faults**

8. `E_FUEL` is raised by a charge that would take the entry's consumed fuel above its budget. The record holds the operation identifier `fuel.charge`, no operands, no exact result and the budget as the limit, an `I64`. In the entry phase its position is the dispatch; inside a work-item, the loop or call that charged.

**Examples**

9. The kernel `count_up` runs a loop of 10 iterations in each of its 1,000 work-items, and work-item 900 overflows. A `cint-workbench` session sets a fuel budget per entry with `:fuel`, and `--check cpu-c17` runs each entry on `cpu-c17` as well as on `cpu-sir-interp` and compares the outcomes. Without a budget the overflow at work-item 900 is reported, after 10,003 units: 2 for the calls of `go` and `run`, 1,000 for the dispatch, 10 for each of work-items 0 to 899, and 1 for the first iteration of work-item 900. With a budget of 6,002, work-item 500 cannot make its first charge, and the fuel fault comes first:

```console
$ cat budget.ci
kernel count_up[n](in I64[n] a, out I64[n] r) over [i: n] {
    I64 acc = a[i];
    for j in 0..10 {
        acc = acc + 1;
    }
    r[i] = acc;
}

export I64 run() {
    I64[1000] a;
    I64[1000] r;
    a[900] = 9223372036854775807;
    count_up(a, r);
    return r[999];
}
$ cint-workbench budget.ci
...
ci[1]> :load budget.ci
loaded budget (budget.ci)
ci[2]> export I64 go() { return budget.run(); }
go : () -> I64   revision session@r1
ci[3]> :run go() --check cpu-c17
...
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> budget.ci:4:19
...
   = address: budget.count_up, dispatch 0, phase 1, work-item 900, step 1
...
   = fuel-consumed: 10003 (not part of the fault record)
...
check: cpu-sir-interp and cpu-c17 agree at 1 boundary
ci[4]> :fuel 6002
fuel: 6002 units per entry
ci[4]> :run go() --check cpu-c17
fault[E_FUEL]: fuel allowance exhausted
  --> budget.ci:3:5
   |
 3 |     for j in 0..10 {
   |     ^ fuel.charge
   |
   = operation: fuel.charge
   = operand-count: 0
   = exact:   none
   = limit:   I64 6002
...
   = address: budget.count_up, dispatch 0, phase 1, work-item 500, step 1
...
   = fuel-consumed: 6002 (not part of the fault record)
...
check: cpu-sir-interp and cpu-c17 agree at 1 boundary
```

The step ordinal 1 of the fuel fault counts the read `a[i]`, the one counted operation before the loop's first charge. The fuel consumed, 6,002, does not include the charge that faulted, and the overflow at work-item 900 is not reached.

<a id="reductions"></a>

## 9 Reductions

<a id="accumulator-widths"></a>

### 9.1 Accumulator widths

A `sum` in a kernel adds its contributions exactly and checks only the final total against the target's type. This subclause states how wide an exact sum must be held.

**Semantics**

1. A sum of `n >= 1` values of `w` bits, signed or unsigned, never leaves the range of an accumulator of `w + ceil(log2(n))` bits of the same signedness, in any order of addition, as SPEC-04P.3 7.3 states [SPEC-04P.3 IM-80].
2. <a id="R-5"></a>Because `n` is at most `MAX(I64)`, below 2^63, the accumulators of Table 2 suffice for every dispatch. An executor shall hold the sum of a `sum` reduction exactly, in these accumulators or by any other method that gives the exact sum, and check it once, at the end, against the target's type [R-5] [SPEC-04P.3 IM-80].

Table 2. Accumulators for `sum`.

| Width of a contribution | Signed contributions | Unsigned contributions |
|---|---|---|
| Up to 64 bits | `I128` | 128-bit unsigned |
| 128 bits | `I256` | 256-bit unsigned |
| 256 bits | `I512` | 512-bit unsigned |
| 512 bits | `I1024` | 1024-bit unsigned |
| 1024 bits | an internal accumulator of 1088 bits, which is not a program type | the same |

3. <a id="BACK-07"></a>The compiler chooses the width of the accumulator before the dispatch, from the largest number of contributions the dispatch can have. With no other bound, that number is `MAX(I64)`, so a `sum` of 64-bit contributions needs 127 bits, and the check of the final total against the target's type is made once [BACK-07].
4. The other reductions (`fold_checked`, `sum_wrap`, `sum_sat`, `min`, `max` and `count`) act at the width of the contribution type, for every integer type, the wide types included [R-5].
5. A reduction over a fixed-point type acts on the stored integers of its contributions and gives its result in their format, and no reduction rounds, as SPEC-04P.3 7.3 states. A `sum` of contributions of 128-bit storage, such as `Q80.48`, is held in the accumulator of the 128-bit row of Table 2 [SPEC-04P.3 R-4].

**Examples**

6. This example computes three exact sums. The first passes through `MAX(I64) + 1` and returns to `MAX(I64)` without a fault. The second sums four `I32` maxima into an `I64` target. The third is the first one in `I128`:

```cint acc.ci
kernel total[n](in I64[n] x, out I64 s) over [i: n] {
    reduce s = sum(x[i]);
}

kernel total32[n](in I32[n] x, out I64 s) over [i: n] {
    reduce s = sum(x[i]);
}

kernel total128[n](in I128[n] x, out I128 s) over [i: n] {
    reduce s = sum(x[i]);
}

void run() {
    I64[3] x = [9223372036854775807, 1, -1];
    I64 s = 0;
    total(x, s);
    "{s}\n";
    I32[4] y = [2147483647, 2147483647, 2147483647, 2147483647];
    total32(y, s);
    "{s}\n";
    I128[3] z = [I128.max, 1, -1];
    I128 w = 0;
    total128(z, w);
    "{w}\n";
}

run();
```

```text
9223372036854775807
8589934588
170141183460469231731687303715884105727
```

7. This example reduces `Q16.16` contributions with a `sum` and a `max`:

```cint fixed_reduce.ci
kernel tot[n](in Q16.16[n] a, out Q16.16 s, out Q16.16 m) over [i: n] {
    reduce s = sum(a[i]);
    reduce m = max(a[i]);
}

void go() {
    Q16.16[3] a = [1.25, -0.5, 2.0];
    Q16.16 s = 0.0;
    Q16.16 m = 0.0;
    tot(a, s, m);
    "{s} {m}\n";
}
go();
```

```text
2.75 2.0
```

<a id="order-of-evaluation"></a>

### 9.2 Order of evaluation

**Semantics**

1. `sum` and `sum_wrap` have no intermediate fault, and the results of `min`, `max` and `count` do not depend on order (SPEC-04P.3 7.3), so an executor may evaluate them in any order or tree [SPEC-04P.3 IM-82, IM-85].
2. An executor shall give the left-to-right value of `sum_sat`, the fold `acc = acc +| x` from 0 over the contributions in ascending work-item order, as SPEC-04P.3 7.3 states; reassociating `+|` does not conform [SPEC-04P.3 IM-84].
3. <a id="R-10"></a><a id="IM-86"></a>An executor may compute `sum_sat` in parallel by composing maps. Write `clamp(v, L, H)` for `min(max(v, L), H)`, and `f(c, L, H)` for the map that sends `x` to `clamp(x + c, L, H)`, with `L <= H` and three free parameters. Applying `f(c1, L1, H1)` and then `f(c2, L2, H2)` gives `f(c1 + c2, P, Q)`, with `P = clamp(L1 + c2, L2, H2)` and `Q = clamp(H1 + c2, L2, H2)`. Each contribution `e` is the map `f(e, MIN(T), MAX(T))`, the composed offset `c` is held exactly in the `sum` accumulator, and the result is the composed map applied to 0. The maps are not closed under composition when `L` and `H` are fixed at the type's bounds, and a scan built that way does not conform [R-10, IM-86].
4. <a id="R-9"></a><a id="BACK-08"></a>An executor may compute `fold_checked(add, init, ...)` in parallel. Before the first fault, the accumulator after contribution `j` equals `init` plus the exact sum of contributions 0 to `j`, so an exact prefix sum in the `sum` accumulator finds the first contribution whose prefix leaves the target's range, and gives the same fault and operands as the sequential fold [R-9, BACK-08].

**Examples**

5. `sum_sat` over `I8` depends on the order of the contributions. In ascending order, `120, 120, -120` gives 120, then 127 by saturation, then 7; `120, -120, 120` gives 120:

```cint sat.ci
kernel total_sat[n](in I8[n] x, out I8 s) over [i: n] {
    reduce s = sum_sat(x[i]);
}

void run() {
    I8[3] a = [120, 120, -120];
    I8[3] b = [120, -120, 120];
    I8 s = 0;
    total_sat(a, s);
    "{s}\n";
    total_sat(b, s);
    "{s}\n";
}

run();
```

```text
7
120
```

**Notes**

6. The composition rule of paragraph 3, checked on `I8`: composing `+120` and then `-120` gives the map with `c = 0`, `L = -128` and `H = 7`, which sends 0 to 0 and 10 to 7, as the sequential fold does.

<a id="fault-attribution"></a>

### 9.3 Fault attribution

**Semantics**

1. A `fold_checked` overflow that occurs when contribution `k` is added is a fault of work-item `k`, at the step ordinal of its `reduce` statement, with the record that SPEC-04P.3 6.13.5 gives [SPEC-04P.3 R-6, IM-83].
2. A fault of the epilogue, the final check of a `sum` or a `min` or `max` with no contributions, occurs only when no work-item faulted, and epilogue faults are ordered after every work-item and among themselves by the declaration order of their targets, as SPEC-04P.3 6.13.5 states [SPEC-04P.3 R-7].
3. If the canonical fault is at work-item `k`, every work-item below `k` completed without a fault (SPEC-04P.3 6.13.6), so the partial result of a fold before `k`, which paragraph 1 uses, is fully defined [SPEC-04P.3 R-8].

**Examples**

4. The reduction comes before the division. At work-item 1 the fold overflows at step 1, before the division by `x[1] - 10 = 0` at step 4:

```cint tie_a.ci
kernel check[n](in I64[n] x, out I64[n] y, out I64 s) over [i: n] {
    reduce s = fold_checked(add, 0, x[i]);
    y[i] = div_round(100, x[i] - 10, floor);
}

void run() {
    I64[3] x = [9223372036854775807, 10, 0];
    I64[3] y;
    I64 s = 0;
    check(x, y, s);
}

run();
```

```text
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> tie_a.ci:2:16
   |
 2 |     reduce s = fold_checked(add, 0, x[i]);
   |                ^ fold_checked.add.i64
   |
   = operation: fold_checked.add.i64
   = operand-count: 3
   = operand: I64 1
   = operand: I64 9223372036854775807
   = operand: I64 10
   = exact:   9223372036854775817
   = limit:   I64 9223372036854775807
   ...
   = address: tie_a.check, dispatch 0, phase 1, work-item 1, step 1
   ...
```

5. This example puts the same statements in the other order. The division at step 3 now comes before the reduction at step 5:

```cint tie_b.ci
kernel check[n](in I64[n] x, out I64[n] y, out I64 s) over [i: n] {
    y[i] = div_round(100, x[i] - 10, floor);
    reduce s = fold_checked(add, 0, x[i]);
}

void run() {
    I64[3] x = [9223372036854775807, 10, 0];
    I64[3] y;
    I64 s = 0;
    check(x, y, s);
}

run();
```

```text
fault[E_DIV_ZERO]: division by zero
  --> tie_b.ci:2:12
   |
 2 |     y[i] = div_round(100, x[i] - 10, floor);
   |            ^ div_round.checked.i64.floor
   |
   = operation: div_round.checked.i64.floor
   = operand-count: 2
   = operand: I64 100
   = operand: I64 0
   = exact:   none
   = limit:   none
   ...
   = address: tie_b.check, dispatch 0, phase 1, work-item 1, step 3
   ...
```

**Notes**

6. When a `fold_checked` overflow and another fault fall on the same work-item, the step ordinal decides between them, as for any two faults of one work-item (5.5). Examples 4 and 5 show both orders.

<a id="executors"></a>

## 10 Executors

<a id="exact-or-refused"></a>

### 10.1 Exact or refused

An executor computes every value at its declared width, with its declared behavior on overflow, or it does not run the program. This subclause states that obligation for every executor.

**Semantics**

1. <a id="B-1"></a>An executor shall implement every operation it admits at the declared width of its type and with its declared behavior on overflow. It shall not compute at fewer bits than the type has, substitute wrapping for checked arithmetic, substitute floating point for integers, round differently, drop a check or copy data into another layout without the program's request. Staging for publication (7.3) is part of publication, not a change of layout [B-1] [SPEC-04P.3 IM-129].
2. An executor that cannot carry out an operation, a type, a width or a layout exactly refuses it with `E_UNSUPPORTED`, naming the operation, the type and the capability: during translation when the need is known then, and otherwise in the entry phase of the dispatch, before any work-item runs [B-1] [SPEC-04P.3 IM-129].
3. <a id="B-2"></a><a id="IM-123"></a><a id="IM-126"></a>A checked operation is carried out with operations whose result is defined for every input, and its check is made before any result that depends on it is stored: by testing the operands first, by instructions that give a carry or the high half of a product, or by the checked built-in operations of a C compiler. Computing a signed result that overflows, in a language where that is undefined, and testing it afterward does not conform [B-2, IM-123, IM-126].
4. <a id="IM-124"></a><a id="IM-125"></a>A division or remainder tests for a divisor of 0, and for `MIN(T) / -1`, before any native division instruction. A shift tests its count before any native shift instruction [IM-124, IM-125].
5. <a id="B-3"></a>Where a target language or machine defines a result for a case that cint treats as a fault, such as a division by 0 that gives a value, the executor tests for the case itself and raises the fault that SPEC-04P.3 defines [B-3].

<a id="capability-profiles"></a>

### 10.2 Capability profiles

**Semantics**

1. <a id="B-4"></a>Each executor has a capability profile, the capability table of SPEC-09P.2 5.5: for every family of operation, every form of it and every type, an entry that says the executor runs it natively, runs it by a named lowering, or refuses it [B-4] [SPEC-09P.2 BACK-04].
2. A program that needs an entry the selected executor refuses fails to build, and a device that lacks a capability the build assumed fails to load, with `E_UNSUPPORTED`, as SPEC-09P.2 5.5 states; an executor never falls back to a narrower type [SPEC-09P.2 BACK-05].
3. An entry that names a lowering is admitted only when the conformance cases of that lowering pass (SPEC-09P.2 5.5) [SPEC-09P.2 BACK-06].

<a id="cpu-c17"></a>

### 10.3 `cpu-c17`

`cpu-c17` translates a program to C17 and compiles it with the system's C compiler. It runs every kernel on the host, one work-item at a time.

**Semantics**

1. <a id="B-5"></a>`cpu-c17` implements every type and operation of `cint-core-1` in kernels as in sequential code: `Bool`, the signed integer types from 8 to 1024 bits, the unsigned ones from 8 to 512 bits, and fixed point, with the accumulators of 9.1 up to the internal 1088-bit one. It runs the work-items of a dispatch in ascending index order, stops at the first fault and produces the complete fault record directly [B-5].
2. Its output is deterministic, as SPEC-09P.2 7.7 states: the same program gives the same C, with LF line endings, stable numbering and no time stamps or paths [SPEC-09P.2 BACK-02].
3. <a id="B-7"></a>The C it emits compiles without warnings under `-std=c17 -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow -Wpedantic -Werror` with GCC and Clang, and under `/std:c17 /W4 /WX` with MSVC [B-7].

**Examples**

4. The kernel below uses operators and statements that `gpu-cuda` does not run (10.5): unary minus, the bitwise operators, `max`, a saturating addition, division, a shift, a wrapping conversion, `switch` and a local array. `cpu-c17` and the interpreter run it:

```cint ops_kernel.ci
kernel ops[n](in I64[n] a, out I64[n] b) over [i: n] {
    I64 x = a[i];
    I64 y = -x;
    I64 z = (x & 6) | (x ^ 3);
    I64 m = max(x, 2);
    I64 s = x +| 1;
    I64 d = x / 2;
    I64 sh = x << 1;
    I32 w = x as% I32;
    I64 sw = 0;
    switch (x) {
        case 1: sw = 10;
        default: sw = 20;
    }
    I64[2] loc = [x, x];
    b[i] = y + z + m + s + d + sh + (w as I64) + sw + loc[1];
}

void go() {
    I64[3] a = [1, 2, 3];
    I64[3] b;
    ops(a, b);
    "{b[0]} {b[1]} {b[2]}\n";
}
go();
```

```text
19 35 39
```

5. The kernel below writes an output of 8-bit elements:

```cint narrow_out.ci
kernel half[n](in U8[n] a, out U8[n] b) over [i: n] {
    b[i] = a[i] / 2;
}

void go() {
    U8[3] a = [10, 20, 255];
    U8[3] b;
    half(a, b);
    "{b[0]} {b[1]} {b[2]}\n";
}
go();
```

```text
5 10 127
```

6. The kernel below computes over `I128`:

```cint wide_kernel.ci
kernel sq[n](in I128[n] a, out I128[n] b) over [i: n] {
    b[i] = a[i] * a[i];
}

void go() {
    I128[2] a = [9223372036854775807, 3];
    I128[2] b;
    sq(a, b);
    "{b[0]} {b[1]}\n";
}
go();
```

```text
85070591730234615847396907784232501249 9
```

7. The kernel below computes over the fixed-point type `Q16.16`:

```cint fixed_kernel.ci
kernel half[n](in Q16.16[n] a, out Q16.16[n] b) over [i: n] {
    b[i] = a[i] * 0.5;
}

void go() {
    Q16.16[2] a = [3.0, -1.5];
    Q16.16[2] b;
    half(a, b);
    "{b[0]} {b[1]}\n";
}
go();
```

```text
1.5 -0.75
```

8. The kernel below computes over `U128`:

```cint u128_kernel.ci
kernel inc[n](in U128[n] a, out U128[n] b) over [i: n] {
    b[i] = a[i] + 1;
}

void go() {
    U128[2] a = [18446744073709551615, 3];
    U128[2] b;
    inc(a, b);
    "{b[0]} {b[1]}\n";
}
go();
```

```text
18446744073709551616 4
```

<a id="cpu-sir-interp"></a>

### 10.4 `cpu-sir-interp`

`cpu-sir-interp` is the interpreter, `cint-interp`. It runs a program from the text of its intermediate representation, with no C compiler.

**Semantics**

1. `cpu-sir-interp` reads the text of the intermediate representation that the compiler writes for each module, checks it and runs it, as SPEC-09P.2 5.3 states [SPEC-09P.2 BACK-01a].
2. For every arithmetic operation, conversion and built-in operation, it calls the same run-time routine that the C of `cpu-c17` calls, so it adds no arithmetic of its own; it runs the work-items of each dispatch one at a time in ascending order, and counts step ordinals as 5.4 states [SPEC-09P.2 BACK-01a].
3. It is not part of the compiler, while the code generators of the other executors are, as SPEC-09P.2 5.3 states [SPEC-09P.2 BACK-01].
4. `cint-workbench`, `cint-repl` and `cint-debug` run programs on `cpu-sir-interp`, and `cint-workbench` can compare each entry with `cpu-c17` (8.4, example 9). SPEC-06P.2 describes these tools.

<a id="gpu-cuda"></a>

### 10.5 `gpu-cuda`

`gpu-cuda` runs the work-item phase of each dispatch on an NVIDIA GPU and everything else on the host. Its capability profile admits the 64-bit and narrower integer types and a subset of the operations; it refuses the rest during translation.

**Semantics**

1. <a id="B-16a"></a>A `gpu-cuda` program is the `cpu-c17` program together with device code for each kernel. The C that cint emits for a module carries that device code: compiled as it is, the C is the `cpu-c17` program and compiled with the macro `CINT_GPU_CUDA` defined and linked with the run-time files `rt/cint_cuda.c` and `rt/cint_cuda_dispatch.c`, it is the `gpu-cuda` program. The host runs the sequential code, the entry checks of 5.3, publication, the final fold of each reduction and the description of a fault by running one work-item again (10.6). The device runs only the bodies of work-items. Every kernel of a `gpu-cuda` program runs on the device [B-16a] [SPEC-09P.2 BACK-16].
2. <a id="B-16"></a>The device code is PTX text, version 6.0 for target `sm_52` with 64-bit addresses: one module for each cint module that defines kernels, held in that module's C as an array of string literals, one for each line and loaded through the CUDA driver API (section 6.27, Module Management) at the first dispatch of one of its kernels. No NVIDIA compiler takes part in a build [B-16].
3. In the device code, every value is held in a 64-bit register. A checked operation of 8, 16 or 32 bits computes its exact result in 64 bits and tests that the result fits its type. A checked operation of 64 bits tests the signs of its operands and its result for addition and subtraction, and the high half of the product, from `mul.hi`, for multiplication. Each test branches to the work-item's fault path before any result that depends on it is stored. No CUDA C++ is generated, so the undefined signed overflow of CUDA C++ does not arise. Element addresses are formed only after the view is admitted, from checked `I64` index arithmetic [B-16].
4. Work-item `i` runs in thread `i`: the block index times the block size plus the thread index, over a one-dimensional grid, plus the first index of the launch, with the index tuple derived from `i` as 8.2 states. The block size is 256 work-items unless the build chooses another. A dispatch larger than one launch can hold is split into launches over ascending ranges of indices, each started after the previous one completes, under the one dispatch number of the dispatch. A launch whose whole range lies above a fault already recorded is not started, since none of its work-items can be canonical [B-16a].
5. <a id="Q-3"></a>The conformance suite runs every case on `gpu-cuda` with two block sizes, 32 and 256 work-items, and the outcomes shall be the same [Q-3].
6. <a id="B-16c"></a>When a body charges fuel, the allowance `F` left after the dispatch charge is passed to every launch, and each work-item writes its total charge `C(i)` to device scratch, one `I64` element per work-item. A work-item stops once its own charges exceed `F`, since its prefix then exceeds `F` too and the canonical fuel fault is at or before it. After the launches, the host finds the work-item of the fuel fault by the exact prefix sum of 8.4 [B-16c].
7. A work-item that executes a `reduce` statement writes its contribution, and a mark that it contributed, to device scratch, one element of each per work-item and per reduction. After the dispatch, the host folds the contributions in ascending order with the `cpu-c17` routine of each reduction: `sum` exactly in the accumulator of 9.1, and `fold_checked` with its first overflow located by the exact prefix of 9.2. The reduction results are therefore those of `cpu-c17` [B-16c].
8. Outputs are published by copy-out, staged in memory of the output's own kind (7.3) [B-16d].
9. <a id="B-16e"></a>A launch that the driver stops, by a time limit, the loss of the device or any other driver error, ends the entry with the host status `CINT_RESOURCE` and a boundary record whose operation is `host.resource` (SPEC-03P.2). It is not a cint fault, and the failure of a device is outside the scope of every rule of this document. A GPU that also drives a display may have a time limit on each launch, so a long launch can end this way where `cpu-c17` completes. The executor changes no setting of the driver or of the operating system [B-16e].
10. <a id="B-16f"></a>The capability profile of `gpu-cuda` is Table 3. Every family, form or type that the table does not list is refused [B-16f] [SPEC-09P.2 BACK-04].

Table 3. The capability profile of `gpu-cuda`.

| Field | Content |
|---|---|
| Native types | `Bool`, `I8` to `I64`, `U8` to `U64`, as values and as element types of `in` arrays |
| Native operations | checked and wrapping `+`, `-` and `*`; comparisons; `&&`, `||` and `!` on `Bool`; the checked conversion `as` between the native types; `div_round` in every rounding mode; `if`, `else`, `for`, `while`, `break` and `continue`; fuel charges; element reads; element stores into outputs of `I32`, `U32`, `I64` and `U64`; `reduce` in every form over contributions of the native types |
| Lowered | none |
| Fault word | `U64` in device memory (10.6) |
| Work-items per dispatch | up to `MAX(I64)`, in launches over ascending ranges |
| Placements | host memory that the context owns and maps for the device, and device memory |

11. The capabilities that `gpu-cuda` refuses, each with the name that its refusal gives:

| Capability | What needs it |
|---|---|
| `wide` | a value, element or operand of an integer type wider than 64 bits |
| `sat` | a saturating operator, or the conversion `as\|` |
| `result` | a result form, or an error value |
| `div` | `/` or `%` |
| `shift` | a shift |
| `bits` | `&`, `|`, `^` or `~` |
| `neg` | unary minus |
| `as.wrap` | the conversions `as%` and `as?` |
| `builtin` | a built-in operation other than `div_round`, such as `min`, `max` or `abs` |
| `switch` | a `switch` statement |
| `store.narrow` | a store into an output whose elements are `Bool` or of 8 or 16 bits |
| `array` | a local array |
| `struct` | a struct value |
| `view.make` | forming a view inside the body |
| `reduce.view` | a reduction over a whole view inside the body |
| `call` | a call of a function |
| `print`, `assert` | printing or asserting inside the body |
| `state` | a module-level variable |
| `outer.write` | an assignment to a variable that the work-item body does not declare |
| `params` | a kernel whose parameters need more than 4,096 bytes, or more than 61 parameters |
| `fixed` | a value, an element or a view of a fixed-point type |
| `node` | any other construct |

12. A program that needs a refused capability does not build for `gpu-cuda`: the build fails with `E_UNSUPPORTED`, naming the first refused construct of the module's kernels, the kernel, the capability and the line and column of the construct (10.2). The same program builds and runs on `cpu-c17` [SPEC-09P.2 BACK-05].

**Examples**

13. The kernel `ops` of 10.3, example 4, does not build for `gpu-cuda`: its first refused construct is the unary minus at 3:13, capability `neg`. The kernel `half` of 10.3, example 5, is refused at its division at 2:17, capability `div`, and a kernel that copies a `U8` input to a `U8` output is refused at its store, capability `store.narrow`. The kernel `sq` of example 6 is refused with `wide`, at its declaration at 1:8, and the kernel `half` of example 7 with `fixed`, also at 1:8.

**Notes**

14. Table 3 is the profile of cint 0.2.2. Clause 12 names further lowerings, which this profile does not include.

<a id="the-fault-protocol-on-a-gpu"></a>

### 10.6 The fault protocol on a GPU

On a GPU, many work-items run at once and several may fault. The protocol of this subclause finds the canonical fault with one word of device memory per dispatch, and describes it by running one work-item again on the host.

**Semantics**

1. <a id="F-9"></a><a id="B-16b"></a>For each dispatch, `gpu-cuda` keeps a fault word, a `U64` in device memory set to all ones, the value that stands for no fault, before the first launch [F-9, B-16b].
2. <a id="F-10"></a><a id="BACK-13"></a>Each work-item `i` follows this protocol [F-10, BACK-13]:
   1. When it starts, it reads the fault word and exits at once, writing nothing, if the word is below `i`: such a work-item cannot be canonical.
   2. It runs the body, testing each checked operation before it computes it.
   3. On a fault, it takes the atomic minimum of the fault word and `i` (`atom.global.min.u64`) and exits without any further write. Nothing else about the fault is kept on the device.
   4. On completion, its writes are already in staging.
3. A work-item below the final value of the fault word never exits early, because the word never falls below its final value. Every work-item below the canonical one therefore runs to its end, as 9.3 requires [F-10].
4. <a id="F-11"></a>After the device completes, the host takes `k_b`, the value of the fault word; `k_f`, the work-item of the first fuel fault (10.5, paragraph 6); and `k_r`, the work-item of the first `fold_checked` overflow, from its fold (10.5, paragraph 7). Let `k` be the least of the three that exists [F-11, B-16b].
5. If `k` exists, the host runs work-item `k` again, in the `cpu-c17` code of the same kernel from the same program, with the inputs from before the dispatch, the uniform arguments, the partial result of each `fold_checked` before `k`, and the fuel consumed before `k`. The first fault of that run is the canonical fault, so its record, position, stack, operands and step ordinal are those that `cpu-c17` reports. Inputs in device memory are read back to the host for this run, and the read-back is listed in the entry's copy report (SPEC-03P.2). If none of `k_b`, `k_r` and `k_f` exists, the host completes the reductions and publishes [F-11, B-16b].
6. When two of `k_b`, `k_r` and `k_f` are equal, the run decides by step ordinal which fault came first [F-11].
7. If work-item `k` completes on the host without a fault, the device and the host disagree, and the executor is defective. The entry ends with the host status `CINT_RESOURCE`, and the context holds a boundary record whose operation is `host.divergence`, with both observations beside it. It is not a cint fault, and a conformance run counts the case as failed [F-11, B-16b].
8. <a id="F-11a"></a><a id="IM-116"></a>An executor may find and describe the canonical fault by another method, such as the least index over per-block slots or running the whole dispatch again on the host, provided that the fault record, the fuel consumed and the outputs are those of this protocol [F-11a, IM-116] [SPEC-04P.3 IM-115].

**Notes**

9. Running one work-item again is sound because work-items are independent (8.1): its inputs are intact, since `in` arguments are never written and staged outputs are not published. The re-run is not charged fuel (8.4, paragraph 7).

<a id="worked-example-heat-diffusion"></a>

## 11 Worked example: heat diffusion

This clause takes one kernel through the rules of this document: an explicit step of the heat equation on a grid of `I32` temperatures, in raw units. The diffusion number is the rational `kn / kd`. Cells on the edge of the grid are held at their values. Each interior update rounds once, to nearest with ties to even, and the kernel also reports the exact total of the new grid. Every example of this clause runs on `cpu-c17` and on the interpreter, and every operation of the kernel is in the profile of `gpu-cuda` (Table 3); the conformance suite of SPEC-09P.2 holds cases of this kernel and compares their outcomes with the reference on `cpu-c17` and on `gpu-cuda`.

<a id="the-kernel-and-one-step"></a>

### 11.1 The kernel and one step

1. Line numbers in the records of this clause refer to the kernel as listed, counted from its first line. The program below takes one step from a hot spot of 65536 at `(3, 4)` on an 8 by 8 grid, with `kn = 1` and `kd = 8`. At the center the Laplacian is `-4 * 65536` and the flux `-32768`, so the center keeps 32768, and each of the four neighbors receives 8192. The total is unchanged:

```cint heat_spot.ci
kernel diffuse[h, w](
    in  I32[h, w] u,
    in  I64       kn,
    in  I64       kd,
    out I32[h, w] v,
    out I64       total
) over [y: h, x: w] {
    I64 c = u[y, x] as I64;
    I32 r = u[y, x];
    if (y > 0 && y < h - 1 && x > 0 && x < w - 1) {
        I64 lap = (u[y - 1, x] as I64) + (u[y + 1, x] as I64)
                + (u[y, x - 1] as I64) + (u[y, x + 1] as I64) - 4 * c;
        I64 flux = div_round(kn * lap, kd, half_even);
        r = (c + flux) as I32;
    }
    v[y, x] = r;
    reduce total = sum(r);
}

void step(I32 spot, I32 level) {
    I32[8, 8] a;
    I32[8, 8] b;
    if (level != 0) {
        for y in 0..8 {
            for x in 0..8 {
                a[y, x] = level;
            }
        }
    }
    a[3, 4] = spot;
    I64 total = 0;
    diffuse(a, 1, 8, b, total);
    "center {b[3, 4]}, neighbors {b[2, 4]} {b[4, 4]} {b[3, 3]} {b[3, 5]}, total {total}\n";
}

step(65536, 0);
step(12, 0);
step(-4, 0);
step(2147483647, 2147483647);
```

```text
center 32768, neighbors 8192 8192 8192 8192, total 65536
center 6, neighbors 2 2 2 2, total 14
center -2, neighbors 0 0 0 0, total -2
center 2147483647, neighbors 2147483647 2147483647 2147483647 2147483647, total 137438953408
```

2. The second and third lines show the rounding. From a spot of 12, each neighbor's exact flux is 12 / 8 = 1.5, which rounds to the even 2, and the total grows to 14. From a spot of -4, each neighbor's exact flux is -0.5, which rounds to 0, and the total is -2. Rounding can add or remove heat; the result is exact and the same on every executor.
3. In the last line every cell holds `MAX(I32)`, so every Laplacian is 0 and every cell keeps its value. The total, 64 times 2147483647, is 137438953408, which is held exactly in the `I128` accumulator of the `sum` (9.1) and fits the `I64` target.

<a id="step-ordinals-of-a-work-item"></a>

### 11.2 Step ordinals of a work-item

1. An interior work-item evaluates 28 counted operations (5.4); the widening conversions `as I64` count:

| Step | Line | Operation | Step | Line | Operation |
|---|---|---|---|---|---|
| 0 | 8 | read `u[y, x]` | 14 | 12 | `as I64` |
| 1 | 8 | `as I64` | 15 | 12 | `+` |
| 2 | 9 | read `u[y, x]` | 16 | 12 | `x + 1` |
| 3 | 10 | `h - 1` | 17 | 12 | read `u[y, x + 1]` |
| 4 | 10 | `w - 1` | 18 | 12 | `as I64` |
| 5 | 11 | `y - 1` | 19 | 12 | `+` |
| 6 | 11 | read `u[y - 1, x]` | 20 | 12 | `4 * c` |
| 7 | 11 | `as I64` | 21 | 12 | `-` |
| 8 | 11 | `y + 1` | 22 | 13 | `kn * lap` |
| 9 | 11 | read `u[y + 1, x]` | 23 | 13 | `div_round` |
| 10 | 11 | `as I64` | 24 | 14 | `c + flux` |
| 11 | 11 | `+` | 25 | 14 | `as I32` |
| 12 | 12 | `x - 1` | 26 | 16 | place `v[y, x]` |
| 13 | 12 | read `u[y, x - 1]` | 27 | 17 | `reduce` (`sum`) |

2. A work-item in row 0 stops the condition at `y > 0` and evaluates steps 0 to 2, then the place `v[y, x]` as step 3 and the `reduce` as step 4. The kernel has no loop and no call, so a dispatch over an `h` by `w` grid consumes `h * w` units of fuel, its dispatch charge.

<a id="faults"></a>

### 11.3 Faults

1. The first case is an overflow. With `u[3, 4] = 1000`, `kn = 2^61` and `kd = MAX(I64)`, the interior work-items with a nonzero Laplacian are 20, 27, 28, 29 and 36: the spot and its four neighbors. Work-item 20, `(2, 4)`, is the least and its product `kn * lap` (step 22) overflows before `div_round` (step 23):

```cint heat_e1.ci
kernel diffuse[h, w](
    in  I32[h, w] u,
    in  I64       kn,
    in  I64       kd,
    out I32[h, w] v,
    out I64       total
) over [y: h, x: w] {
    I64 c = u[y, x] as I64;
    I32 r = u[y, x];
    if (y > 0 && y < h - 1 && x > 0 && x < w - 1) {
        I64 lap = (u[y - 1, x] as I64) + (u[y + 1, x] as I64)
                + (u[y, x - 1] as I64) + (u[y, x + 1] as I64) - 4 * c;
        I64 flux = div_round(kn * lap, kd, half_even);
        r = (c + flux) as I32;
    }
    v[y, x] = r;
    reduce total = sum(r);
}

void step() {
    I32[8, 8] a;
    I32[8, 8] b;
    a[3, 4] = 1000;
    I64 total = 0;
    diffuse(a, 2305843009213693952, 9223372036854775807, b, total);
    "{total}\n";
}

step();
```

```text
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> heat_e1.ci:13:33
   |
13 |         I64 flux = div_round(kn * lap, kd, half_even);
   |                                 ^ mul.checked.i64
   |
   = operation: mul.checked.i64
   = operand-count: 2
   = operand: I64 2305843009213693952
   = operand: I64 1000
   = exact:   2305843009213693952000
   = limit:   I64 9223372036854775807
   ...
   = address: heat_e1.diffuse, dispatch 0, phase 1, work-item 20, step 22
   ...
```

2. The second case is a narrowing. With `u[3, 4] = MAX(I32)` and `kn = kd = 1`, work-items 20 and 27 compute `r = 2147483647`, which fits. At work-item 28, the spot, the Laplacian is `-4 * 2147483647`, the flux equals it, and `c + flux = -6442450941` does not fit `I32`:

```cint heat_e2.ci
kernel diffuse[h, w](
    in  I32[h, w] u,
    in  I64       kn,
    in  I64       kd,
    out I32[h, w] v,
    out I64       total
) over [y: h, x: w] {
    I64 c = u[y, x] as I64;
    I32 r = u[y, x];
    if (y > 0 && y < h - 1 && x > 0 && x < w - 1) {
        I64 lap = (u[y - 1, x] as I64) + (u[y + 1, x] as I64)
                + (u[y, x - 1] as I64) + (u[y, x + 1] as I64) - 4 * c;
        I64 flux = div_round(kn * lap, kd, half_even);
        r = (c + flux) as I32;
    }
    v[y, x] = r;
    reduce total = sum(r);
}

void step() {
    I32[8, 8] a;
    I32[8, 8] b;
    a[3, 4] = 2147483647;
    I64 total = 0;
    diffuse(a, 1, 1, b, total);
    "{total}\n";
}

step();
```

```text
fault[E_NARROW]: the value does not fit the destination type
  --> heat_e2.ci:14:24
   |
14 |         r = (c + flux) as I32;
   |                        ^ as.checked.i64.i32
   |
   = operation: as.checked.i64.i32
   = operand-count: 1
   = operand: I64 -6442450941
   = exact:   -6442450941
   = limit:   I32 -2147483648
   ...
   = address: heat_e2.diffuse, dispatch 0, phase 1, work-item 28, step 25
   ...
```

3. The third case is a division by 0. With `kd = 0`, the first interior work-item, 9, faults in `div_round` although its Laplacian is 0: the divisor is checked whatever the dividend:

```cint heat_e9.ci
kernel diffuse[h, w](
    in  I32[h, w] u,
    in  I64       kn,
    in  I64       kd,
    out I32[h, w] v,
    out I64       total
) over [y: h, x: w] {
    I64 c = u[y, x] as I64;
    I32 r = u[y, x];
    if (y > 0 && y < h - 1 && x > 0 && x < w - 1) {
        I64 lap = (u[y - 1, x] as I64) + (u[y + 1, x] as I64)
                + (u[y, x - 1] as I64) + (u[y, x + 1] as I64) - 4 * c;
        I64 flux = div_round(kn * lap, kd, half_even);
        r = (c + flux) as I32;
    }
    v[y, x] = r;
    reduce total = sum(r);
}

void step() {
    I32[8, 8] a;
    I32[8, 8] b;
    a[3, 4] = 1;
    I64 total = 0;
    diffuse(a, 1, 0, b, total);
    "{total}\n";
}

step();
```

```text
fault[E_DIV_ZERO]: division by zero
  --> heat_e9.ci:13:20
   |
13 |         I64 flux = div_round(kn * lap, kd, half_even);
   |                    ^ div_round.checked.i64.half_even
   |
   = operation: div_round.checked.i64.half_even
   = operand-count: 2
   = operand: I64 0
   = operand: I64 0
   = exact:   none
   = limit:   none
   ...
   = address: heat_e9.diffuse, dispatch 0, phase 1, work-item 9, step 23
   ...
```

<a id="three-reductions-of-the-same-data"></a>

### 11.4 Three reductions of the same data

1. With every cell at `MAX(I32)`, the `I64` total of 11.1 succeeds. The same data with the target `total` declared `I32` gives two other outcomes, because the programs ask for two other operations. With `sum`, no work-item faults, and the epilogue finds that the exact total 137438953408 of 64 contributions does not fit `I32`:

```cint heat_sum32.ci
kernel diffuse[h, w](
    in  I32[h, w] u,
    in  I64       kn,
    in  I64       kd,
    out I32[h, w] v,
    out I32       total
) over [y: h, x: w] {
    I64 c = u[y, x] as I64;
    I32 r = u[y, x];
    if (y > 0 && y < h - 1 && x > 0 && x < w - 1) {
        I64 lap = (u[y - 1, x] as I64) + (u[y + 1, x] as I64)
                + (u[y, x - 1] as I64) + (u[y, x + 1] as I64) - 4 * c;
        I64 flux = div_round(kn * lap, kd, half_even);
        r = (c + flux) as I32;
    }
    v[y, x] = r;
    reduce total = sum(r);
}

void step() {
    I32[8, 8] a;
    I32[8, 8] b;
    for y in 0..8 {
        for x in 0..8 {
            a[y, x] = 2147483647;
        }
    }
    I32 total = 0;
    diffuse(a, 1, 8, b, total);
    "{total}\n";
}

step();
```

```text
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> heat_sum32.ci:17:20
   |
17 |     reduce total = sum(r);
   |                    ^ sum.checked.i32.i32
   |
   = operation: sum.checked.i32.i32
   = operand-count: 1
   = operand: I64 64
   = exact:   137438953408
   = limit:   I32 2147483647
   ...
   = address: heat_sum32.diffuse, dispatch 0, phase 2, work-item none, step none
   ...
```

2. With `fold_checked`, the fold overflows when the contribution of work-item 1, `(0, 1)`, is added. Work-item 1 is in row 0, so its `reduce` is step 4 (11.2, paragraph 2):

```cint heat_fold32.ci
kernel diffuse[h, w](
    in  I32[h, w] u,
    in  I64       kn,
    in  I64       kd,
    out I32[h, w] v,
    out I32       total
) over [y: h, x: w] {
    I64 c = u[y, x] as I64;
    I32 r = u[y, x];
    if (y > 0 && y < h - 1 && x > 0 && x < w - 1) {
        I64 lap = (u[y - 1, x] as I64) + (u[y + 1, x] as I64)
                + (u[y, x - 1] as I64) + (u[y, x + 1] as I64) - 4 * c;
        I64 flux = div_round(kn * lap, kd, half_even);
        r = (c + flux) as I32;
    }
    v[y, x] = r;
    reduce total = fold_checked(add, 0, r);
}

void step() {
    I32[8, 8] a;
    I32[8, 8] b;
    for y in 0..8 {
        for x in 0..8 {
            a[y, x] = 2147483647;
        }
    }
    I32 total = 0;
    diffuse(a, 1, 8, b, total);
    "{total}\n";
}

step();
```

```text
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> heat_fold32.ci:17:20
   |
17 |     reduce total = fold_checked(add, 0, r);
   |                    ^ fold_checked.add.i32
   |
   = operation: fold_checked.add.i32
   = operand-count: 3
   = operand: I64 1
   = operand: I32 2147483647
   = operand: I32 2147483647
   = exact:   4294967294
   = limit:   I32 2147483647
   ...
   = address: heat_fold32.diffuse, dispatch 0, phase 1, work-item 1, step 4
   ...
```

<a id="forty-steps-by-double-buffering"></a>

### 11.5 Forty steps by double buffering

1. A run of 40 steps on a 16 by 16 grid alternates two arrays, as 7.4 describes: each dispatch reads one and writes the other. The total falls from 65536 because the edge cells are held at 0, so heat that reaches the edge leaves the grid, and rounding adds or removes a unit here and there:

```cint heat_tour.ci
kernel diffuse[h, w](
    in  I32[h, w] u,
    in  I64       kn,
    in  I64       kd,
    out I32[h, w] v,
    out I64       total
) over [y: h, x: w] {
    I64 c = u[y, x] as I64;
    I32 r = u[y, x];
    if (y > 0 && y < h - 1 && x > 0 && x < w - 1) {
        I64 lap = (u[y - 1, x] as I64) + (u[y + 1, x] as I64)
                + (u[y, x - 1] as I64) + (u[y, x + 1] as I64) - 4 * c;
        I64 flux = div_round(kn * lap, kd, half_even);
        r = (c + flux) as I32;
    }
    v[y, x] = r;
    reduce total = sum(r);
}

void tour() {
    I32[16, 16] a;
    I32[16, 16] b;
    a[8, 8] = 65536;
    I64 heat = 0;
    for t in 0..20 {
        diffuse(a, 1, 8, b, heat);
        diffuse(b, 1, 8, a, heat);
    }
    "after 40 steps: total={heat} center={a[8, 8]} edge={a[1, 8]}\n";
}

tour();
```

```text
after 40 steps: total=60150 center=1042 edge=72
```

2. The interpreter prints the same line from the same program. Every operation of the kernel is in the profile of `gpu-cuda` (Table 3), so the program also builds for `gpu-cuda`, which runs the 40 dispatches on the device and is required by clause 4 to print the same line.

<a id="future-directions"></a>

## 12 Future directions

This clause is informative. It names work that cint 0.2.2 does not implement, in one or two sentences each, and makes no promise about any of it.

1. In-place dispatch. A dispatch that writes its `inout` arguments directly, without staging, with a stated contract for what a fault leaves in them and no rollback.
2. Schedules. A separate declaration that asks an executor to tile, vectorize, parallelize or place a kernel on a device, or requires it to, and that cannot change any outcome of a dispatch, including fuel.
3. Declared lower bounds. Array parameters indexed from a declared lower bound, such as `1..=n`, which change only the translation from index to offset.
4. Workgroup memory and barriers. Memory shared within a group of work-items, with barriers whose meaning is deterministic, and a fault order extended to the group and the phase between barriers.
5. Calls in kernel bodies. Calls of functions that read no mutable state, perform no I/O and do not recurse, with their counted operations in the caller's step sequence.
6. `fold_checked(mul, ...)` in kernels, which needs exact prefix products to locate its first overflow in parallel.
7. Publication without copying. Publication by exchanging the storage of an output that covers its whole buffer, a requirement that a dispatch publish without copying, and the listing of every copy of a dispatch issued from cint source.
8. Other executors. A C executor with SIMD lanes; executors for other kinds of device; and evaluation of a narrow type in place of `I64` where a bound checked at entry shows that every intermediate value fits.
9. Lowerings for `gpu-cuda`. The capabilities that Table 3 refuses, wide integers by limb sequences, fixed point with 128-bit intermediates, outputs of 8- and 16-bit elements and reductions evaluated on the device.
10. Diagnostic orders. Running the work-items of a dispatch in descending or permuted order, without stopping at the first fault, to test that the canonical fault does not depend on the order.
11. Counter-based random numbers. A pure function of a key and counters, so that no order of execution can change which value belongs to which draw.
12. Recorded overlap outcomes. Recording, as an effect of the entry, the outcome of the interval test on borrowed host memory, so that a replay decides the same way.
13. Narrower accumulators. A declared bound on an extent, with which the accumulator of a `sum` can be narrower than Table 2 gives.
14. Qualifiers `uniform` and `varying` on local variables, checked against inference.
15. Exact tests. An exact test of whether a write view reaches distinct elements, and exact tests of disjointness beyond those of 6.3.

<a id="bibliography"></a>

## Bibliography

- Guy E. Blelloch. *Prefix Sums and Their Applications*. Technical Report CMU-CS-90-190, School of Computer Science, Carnegie Mellon University, November 1990. The prefix sums of 9.2 and 10.5.
- NumPy Developers. "numpy.shares_memory." *NumPy Reference* (NumPy v2.5 Manual). https://numpy.org/doc/stable/reference/generated/numpy.shares_memory.html. The overlap question of 6.3.
- Matt Pharr and William R. Mark. "ispc: A SPMD Compiler for High-Performance CPU Programming." *2012 Innovative Parallel Computing (InPar)*, pages 1-13, May 2012. DOI 10.1109/InPar.2012.6339601. The uniform and varying values of 8.2.

## Annex A (informative) Clause identifiers stated in other editions

A tool message, or an earlier edition, may cite a clause identifier of this document's series whose rule a 0.2.2 edition states elsewhere. Each such identifier is listed here with the edition and section that state it, or, where no 0.2.2 edition states its rule, with the section that describes the topic.

| Identifier | Stated in |
|---|---|
| <a id="K-1"></a>K-1 | [SPEC-04P.3 6.13.2](SPEC-04P.3-LANGUAGE.md#K-1) |
| <a id="K-2"></a>K-2 | [SPEC-04P.3 6.13.2](SPEC-04P.3-LANGUAGE.md#K-2) |
| <a id="K-3"></a>K-3 | [SPEC-04P.3 6.13.1](SPEC-04P.3-LANGUAGE.md#K-3) |
| <a id="K-4"></a>K-4 | [SPEC-04P.3 6.13.2](SPEC-04P.3-LANGUAGE.md#K-4) |
| <a id="K-5"></a>K-5 | [SPEC-04P.3 6.13.1](SPEC-04P.3-LANGUAGE.md#K-5) |
| <a id="K-6"></a>K-6 | [SPEC-04P.3 6.13.2](SPEC-04P.3-LANGUAGE.md#K-6) |
| <a id="K-8"></a>K-8 | [SPEC-04P.3 6.13.1](SPEC-04P.3-LANGUAGE.md#K-8) |
| <a id="K-10"></a>K-10 | [SPEC-04P.3 6.13.3](SPEC-04P.3-LANGUAGE.md#K-10) |
| <a id="K-10a"></a>K-10a | [SPEC-04P.3 6.13.3](SPEC-04P.3-LANGUAGE.md#K-10a) |
| <a id="K-11"></a>K-11 | [SPEC-04P.3 6.13.3](SPEC-04P.3-LANGUAGE.md#K-11) |
| <a id="K-12"></a>K-12 | [SPEC-04P.3 6.13.3](SPEC-04P.3-LANGUAGE.md#K-12) |
| <a id="R-1"></a>R-1 | [SPEC-04P.3 6.13.5](SPEC-04P.3-LANGUAGE.md#R-1) |
| <a id="R-4"></a>R-4 | [SPEC-04P.3 7.3](SPEC-04P.3-LANGUAGE.md#R-4) |
| <a id="V-2"></a>V-2 | [SPEC-04P.3 6.12.1](SPEC-04P.3-LANGUAGE.md#V-2) |
| <a id="V-4"></a>V-4 | [SPEC-04P.3 6.12.4](SPEC-04P.3-LANGUAGE.md#V-4) |
| <a id="V-5"></a>V-5 | [SPEC-04P.3 6.12.4](SPEC-04P.3-LANGUAGE.md#V-5) |
| <a id="V-11"></a>V-11 | [SPEC-04P.3 6.12.4](SPEC-04P.3-LANGUAGE.md#V-11) |
| <a id="X-8"></a>X-8 | [SPEC-04P.3 6.13.6](SPEC-04P.3-LANGUAGE.md#X-8) |
| <a id="P-6"></a>P-6 | [Not a rule of this edition; SPEC-02P.2 12, paragraph 1](SPEC-02P.2-KERNELS.md#future-directions) |
