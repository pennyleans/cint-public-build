# SPEC-00P.1: Overview

SPEC-00P.1, edition 1 of the overview of the CINT specification, cint v0.1.0, 2026-10-06.

Status: Draft. This edition describes the language profile `cint-core-1`, which is not yet frozen: a Proposed rule may change, and an Open question may be decided, in a later edition. The text of this edition does not change after its release. Corrections that change no meaning are listed in [ERRATA.md](ERRATA.md); a change of meaning is published as a new edition. Clause identifiers, section numbers, and open-question identifiers are the same in every edition and are never reused. SPEC-02, SPEC-03, and SPEC-05 to SPEC-09 have no public edition yet; their clauses are cited by identifier and are not part of this edition. What a given release of cint implements is stated in that release's notes, not here. Every number below that describes CINT is a computed expectation, not a measurement.

This section states what CINT is, how its profiles are layered, how a new feature earns a place, who the specification is written for, and where each topic is owned. It defines no arithmetic, syntax, or interface of its own. Where it summarizes another section, that section governs.

## 1. Conventions

| Label | Meaning |
|---|---|
| Specified | Normative for `cint-core-1`. A change to the meaning it gives a frozen expectation (a value, a fault code or fault content, a fuel count, or a diagnostic code or position) requires a new language profile; a change of encoding that carries the same content is a new version of that encoding (section 3). |
| Proposed | The intended design. It may change before the profile is frozen. |
| Measured | A result that was measured, with a receipt a reader can obtain. This edition reports no measurement. |
| Open | Undecided. Each edition lists the open questions it cites in its Open questions section. |

These labels give specification status only. Implementation status, which changes from release to release, is given in each release's notes. Every subsection below carries one of these labels. Where a section holds more than one kind of statement, its heading names the label of each group. Examples are `.ci` source for `cint-core-1`. Terms are defined once, in [GLOSSARY-P.1](GLOSSARY-P.1.md), with the section that defines each.

Sources are cited inline as numbered markers, such as [1], and listed under References at the end of this section.

## 2. Identity (Specified)

### 2.1 Statement

CINT is an integer-only, C-family language and workbench. Its design center is one statement:

> **HolyC-style immediacy, a precisely specified integer machine, and array kernels whose execution strategy can change without changing their meaning.**

"HolyC-style immediacy" credits Terry A. Davis's HolyC, the language of TempleOS, which has no `main()` function and runs code outside functions in order [1].

The three parts are separate responsibilities, provided by one toolchain:

| Part | What it promises | Where it is specified |
|---|---|---|
| HolyC-style immediacy | Write a line, run it, and see the exact value. Top-level statements run like a notebook cell. The workbench evaluates entries with the same typed core as compiled code. A fault shows exact operands and a source position. | SPEC-04 section 10; SPEC-06 sections 2 and 5 |
| A precisely specified integer machine | Every value is a mathematical integer in a declared range. Every operation is defined by its exact result and an overflow intent written in the operator. Faults produce canonical fault records with exact operands. | SPEC-01 |
| Kernels whose execution strategy can change without changing meaning | A kernel is written for one work-item (one execution of the kernel body for one index), as in the SPMD (single program, multiple data) model of ISPC [2]. A dispatch is one call of a kernel over its whole iteration space. Kernel outputs are exclusive; this restriction on mutation draws on Futhark's uniqueness-typed in-place updates [3]. Outputs are published only on success (SPEC-02 P-1). Schedules, SIMD, GPU backends, and multiword lowering change speed, never results. Keeping the schedule separate from the computation follows Halide [4]. A backend that cannot preserve meaning refuses with `E_UNSUPPORTED`. | SPEC-02 |

One rule ties them together: **one meaning, several execution strategies**. The workbench, `cint run`, compile-time execution, the C reference backend, and every accelerated backend execute the same semantic intermediate representation, the semantic IR (SIR) (SPEC-09 ARCH-01). A difference between any two of them on the same semantic inputs is a conformance defect (SPEC-06 section 2, principle 1).

### 2.2 What the identity rules out

| Not this | Because | Instead |
|---|---|---|
| "C with no floats" | Removing a type does not specify a machine. | SPEC-01 defines every operator, rounding, conversion, and fault on mathematical integers. |
| "Fortran made deterministic" | Fortran's `do concurrent` expresses restrictions, not proofs ([5], the DO CONCURRENT construct). | SPEC-02 states the restrictions (exclusive outputs, no cross-work-item communication) and checks them. |
| A REPL with its own arithmetic | A second semantics would make workbench values untrustworthy. | The workbench compiles every entry to SIR and runs it on a conforming backend (SPEC-06 5.1). |
| "Integer reductions are order-independent" | Checked and saturating folds are not associative. | Named reductions with defined meaning: `sum`, `fold_checked`, `sum_wrap`, `sum_sat`, `min`, `max`, and `count` (SPEC-01 section 6). |
| Silent narrowing to fit a backend | WGSL has no runtime 64-bit integer; its concrete integer types are `i32` and `u32` ([6], "Scalar Types"). | Multiword lowering with its own fixtures, range-proven lowering, or refusal (SPEC-01 IM-129; SPEC-02 B-1, B-18). |
| Compliance claims | No assessment exists. | No MISRA, CERT, DO-178C, or other claim is made in any section (SPEC-07 section 14). |

### 2.3 Properties in one table

Each property below is stated with the rule that specifies its mechanism.

| Property | Scope | Specified by |
|---|---|---|
| Every operation has a defined result or a defined fault for every input. | Operations of SPEC-01 sections 4 to 6 | SPEC-01 IM-128 |
| Overflow intent is in the operator: `+` checks, `+%` wraps, and `+\|` saturates. | All integer and fixed-point types | SPEC-01 IM-30 |
| No implicit conversion between runtime numeric types. Literals take their type from context. | All code, including compile-time evaluation | SPEC-01 IM-21, IM-50; SPEC-04 LS-56 |
| A fault is canonical: one fault record, exact operands, a source position, and, inside a kernel, a logical address (dispatch number, work-item index, step ordinal) independent of which lane failed first. | Every backend | SPEC-01 IM-106, IM-112; SPEC-02 F-3 to F-11 |
| Kernel outputs are published only if the whole dispatch succeeds. | Staged dispatches (the default: outputs are computed into staging, SPEC-02 P-1) | SPEC-02 P-1 to P-5 |
| Execution agreement: the same semantic inputs (the execution identity of SPEC-01 IM-159) give the same output state hash (SHA-256 [7]), canonical return bytes, fault record bytes, fuel consumed (fuel is the deterministic budget of a versioned fuel model, counted in `fuel-v1` units, not time; SPEC-01 section 10), and effect-request trace, which fixes stdout byte for byte, on every conforming implementation. | Inputs in scope of SPEC-01 IM-159 | SPEC-01 IM-159, IM-160 |
| Replay from a recording reproduces every canonical state, fault, and published output. | `full` recordings without `unrestricted` effects, hazards, or resource failures | SPEC-03 H-16 |
| Registering a borrowed host input copies none of its elements. A copy happens only when the caller asks for one (SPEC-03 P-15) or a recording requires one (P-16), and each copy is reported. | Python, C, and Fortran bridges | SPEC-03 P-1, P-9, P-15, P-16, M-30a |

Replay follows a model of versioned state plus recorded effects. Record and replay as a debugging method has precedent in rr [8]. The bridges rest on the Python buffer protocol [9], DLPack [10], the C17 language [11], and the C descriptors of Fortran 2018 [5]; SPEC-03 specifies how each is used.

What none of these properties claims: that a computation is numerically adequate for a physical problem. Bit identity, approximation quality, and method adequacy are three separate questions with three kinds of evidence (SPEC-03 F-12; SPEC-01 IM-160).

## 3. Profile layering (Specified)

A language profile names a meaning: values, fault codes and fault content (SPEC-01 IM-106 fields), fuel counts, and diagnostic codes and positions. A change to any of these for a frozen expectation is a new profile (SPEC-09 CONF-02), with its own frozen fixtures. Each canonical encoding is versioned separately: by its domain string `<profile>/<kind>/v<n>` (SPEC-01 IM-152), the runtime contract by `cint-rt-N` (SPEC-09 RCPT-08), and the `.expect` text by its `format` line (SPEC-09 CONF-11). A change of encoding that carries the same content is a new version of that encoding, not a new profile. Every receipt identity records the profile and each encoding version it relies on (SPEC-09 RCPT-02). Frozen expectations are never edited in place (SPEC-01 IM-170, SPEC-09 CONF-02).

Named versions, each with its kind:

| Name (kind) | Status | What it is | Files | Where |
|---|---|---|---|---|
| `cint-core-1` (language profile) | Specified by this draft; not yet frozen | The language, integer machine, kernels, host boundary, ternary representations, workbench contracts, and security model specified in SPEC-01 to SPEC-09 | `.ci` | All sections |
| `fuel-v1` (fuel model) | Specified | The fuel model of `cint-core-1`: one unit per call and per loop iteration, `ceil(n / 64)` per reduction over `n` elements, and for a kernel `N` per dispatch over `N` work items plus the charges inside them | n/a | SPEC-01 section 10 |
| `cint-boot-1` (boot subset of the language) | Proposed until stage T1 (section 8) accepts the seed compiler | The frozen subset in which the self-hosted compiler is written and which the C17 [11] seed compiler accepts | `.ci` | SPEC-09 section 5 |
| `cint-sf-1` (gateway federation profile) | Specified as a gateway profile | The SpaceFOM federation profile of the CINT gateway | n/a | SPEC-08 3.3 |
| `cint-bt27-legacy` (legacy language profile) | Frozen | The previous language of the project (bt27), its bytecode VM, and its native profile. It is not published. It may be mined for ideas, not extended. | `.cint` | SPEC-05 section 11 maps its values, operations, faults, and word encoding |
| Later language profiles and fuel models | Proposed | Candidates named by the sections: workgroup-shared memory with deterministic barriers (SPEC-02 W-3); unsigned wide types and larger fixed point (SPEC-01 questions 3 and 4); additional ternary widths and ternary fixed point (SPEC-05 O-6, O-10); a balanced-ternary hardware backend under the same meaning (SPEC-05 section 10), where balanced ternary is the radix-3 notation with digits -1, 0, and +1 described by Knuth [12]; and a successor fuel model (SPEC-01 IM-137) | n/a | The sections named |

Rules between profiles:

- A `cint-core-1` compiler rejects `.cint` files with diagnostic C1001 (SPEC-04 LS-3). Bytes of legacy compiled programs, checkpoints, and traces are not valid in `cint-core-1` (SPEC-05 section 11).
- Proposed: a file may begin with `profile "cint-core-1";`. Without it, `.ci` means `cint-core-1` (SPEC-04 LS-8).
- The canonical encodings carry the profile name in their domain strings (`cint-core-1/state/v1`, `cint-core-1/revision/v1`), so bytes of two profiles never collide (SPEC-01 IM-152).
- `cint-core-1` is defined on mathematical integers with declared widths. A balanced-ternary backend implements the same language profile. Ternary hardware gains no license to change binary-defined behavior (SPEC-01 section 12; SPEC-05 TR-PRIN-2, TR-PRIN-5).

## 4. A one-page tour (Specified: the `.ci` program; Proposed: the Python call)

The first demonstration program of CINT is a double-buffered integer field stencil with explicit rounding. This 32-line script is that demonstration in miniature.

```ci
// tour.ci: a double-buffered integer heat field, profile cint-core-1
kernel diffuse[h, w](in I32[h, w] u, in I64 kn, in I64 kd,
                     out I32[h, w] v, out I64 total) over [y: h, x: w] {
    I64 c = u[y, x] as I64;
    I32 r = u[y, x];
    if (y > 0 && y < h - 1 && x > 0 && x < w - 1) {
        I64 lap = (u[y - 1, x] as I64) + (u[y + 1, x] as I64)
                + (u[y, x - 1] as I64) + (u[y, x + 1] as I64) - 4 * c;
        r = (c + div_round(kn * lap, kd, half_even)) as I32;
    }
    v[y, x] = r;                    // the work-item's own element
    reduce total = sum(r);          // exact accumulation, one final range check
}

I32[64, 64] a;                      // zero-filled
I32[64, 64] b;
a[32, 32] = 65_536;
I64 heat = 0;
for t in 0..240 {
    diffuse(a, 1, 8, b, heat);      // staged: b is published only if every work-item succeeds
    swap(a, b);                     // exchange the two buffers (handles may be swapped; SPEC-04 LS-145)
}
"after 240 steps: total={heat} center={a[32, 32]}\n";

test "one step from a hot spot" {
    I32[8, 8] p;
    I32[8, 8] q;
    p[3, 4] = 65_536;
    I64 s = 0;
    diffuse(p, 1, 8, q, s);
    assert(q[3, 4] == 32_768 && q[2, 4] == 8_192 && s == 65_536);
}
```

`tour.ci` is expected to print the following. These values were computed with an exact model, not measured.

```text
after 240 steps: total=42319 center=175
```

The model uses Python `fractions.Fraction` rational arithmetic [13], half-to-even division, checked 32-bit storage, and boundary cells held fixed (the kernel updates interior cells only). The model script is not yet published; until it is, the values can be checked only by recomputation. The total started at 65,536 and ends at 42,319. Of the 23,217 units lost, about 14 leave through the fixed boundary cells; the rest, about 23,203, are removed by explicit half-to-even rounding of small updates. The exact `sum` reports the loss instead of hiding it. The same rounding loss is visible in SPEC-02 15.3 (RF-2) and SPEC-06 16.2.

What each line exercises:

| Lines | Construct | Rule |
|---|---|---|
| 2 to 3 | A kernel with shape symbols `h` and `w`, modes `in` and `out`, and an iteration space `over [y: h, x: w]` | SPEC-02 K-1, K-2, K-8 |
| 4, 7 to 9 | Widening written as `as I64`; one named rounding, `div_round(..., half_even)`; and a checked narrowing back to `I32` | SPEC-01 IM-44, IM-50, IM-101 |
| 6 | `h - 1` is checked `I64` index arithmetic. Comparisons are `Bool` and do not chain. | SPEC-01 IM-7, IM-10, IM-48 |
| 11 | A write to the work-item's own element. Outputs are exclusive. | SPEC-02 K-10, A-2 |
| 12 | A named reduction with an exact sum (the reference uses an `I128` accumulator for 32-bit contributions; SPEC-01 IM-80 permits any exact method) | SPEC-01 IM-77, IM-80; SPEC-02 R-5 |
| 15 to 18 | Owned arrays are zero-filled, literals take their types from context, and `_` separates digits. | SPEC-04 3.5, 4.2, 4.3 |
| 19 to 22 | A dispatch is a statement, its outputs are published on success, and `swap` exchanges the contents of two owned arrays; an implementation may exchange handles instead of copying (SPEC-04 LS-145). | SPEC-02 K-14, P-1; SPEC-04 LS-145 |
| 23 | A bare string statement prints, with exact integer formatting. | SPEC-04 LS-193; SPEC-01 IM-90 |
| 25 to 32 | An inline test, run by `cint test` and not by `cint run` | SPEC-04 section 12 |

The next block shows a fault in the layout of SPEC-04 LS-276. Calling `diffuse(p, 1, 1, q, s)` with `p[3, 4] = 2147483647` makes the center value `2147483647 - 4 * 2147483647 = -6442450941`, which does not fit `I32`:

```text
fault[E_NARROW]: checked conversion out of range of I32
  --> tour.ci:9:54 in kernel diffuse
   |
 9 |         r = (c + div_round(kn * lap, kd, half_even)) as I32;
   |                                                      ^^ as.checked.i64.i32
   |
   = value:     -6442450941   I64
   = range:     I32 is -2147483648 ..= 2147483647
   = dispatch:  0
   = work-item: 28 (index tuple [3, 4])
   = step:      25
   = published: nothing (q keeps its previous contents)
```

The address is canonical on every backend. Work-item 28 is the least faulting work-item in row-major order (work-items 20 and 27 compute 2147483647, which fits), and step 25 counts the operations of SPEC-01 IM-113 that work-item 28 evaluated before the conversion. The same case appears as fixture E-2 of SPEC-02 15.4.

From Python, the same kernel, declared `export` in an importable module, is called on NumPy memory without copying the input (SPEC-03 6.2, 6.7). The Python buffer protocol [9] and DLPack [10] are the two ways the bridge reaches that memory; NumPy works with both [14]. SPEC-03 specifies how the bridge uses each.

Proposed: the Python parameter lists (SPEC-03 section 6) and the backend names (SPEC-09 BACK-15) are not yet frozen, so the call below shows the intended shape only.

```python
import numpy as np, cint
mod = cint.load("field.ci", backend="cpu")         # "cpu" is the C reference backend (SPEC-09 BACK-15)
ctx = mod.context()
u = np.zeros((64, 64), dtype=np.int32); u[32, 32] = 65536
v = cint.empty((64, 64), "I32")                    # bridge-owned output, published on success
with cint.borrow(u) as ub:                         # registration copies no elements; refused rather than copied
    ctx.diffuse(ub, 1, 8, v=v)                     # the scalar out total returns as an exact Python int
```

## 5. Admission test for new features (Specified)

Every proposed addition to CINT answers one question first:

> **Does it make exact computation easier to express, inspect, accelerate, or reproduce?**

A proposal states which of the four verbs it serves, with a concrete example. A proposal that serves none is declined, however convenient it is. A proposal that serves one verb but weakens another (for example, a speedup that changes a fault, or a convenience that hides a conversion) is declined or redesigned.

| Verb | Means | Admitted examples | Declined or deferred examples |
|---|---|---|---|
| Express | The exact computation can be written directly, with its rounding and overflow intent visible. | Named reductions (SPEC-01 IM-77); `div_round`, `muldiv`, and `mul_full` with wide intermediates (SPEC-01 IM-51); fixed point with declared storage, scale, intermediate width, and rounding (SPEC-01 IM-54); case ranges `case 1..=5:` (SPEC-04 LS-177) | Implicit widening and integer promotion (SPEC-04 LS-271); chained comparisons (SPEC-04 LS-153); a postfix grammar |
| Inspect | A value, fault, or decision can be seen exactly. | The exact inspector (SPEC-06 section 6); `cint explain` (SPEC-06 section 7); canonical fault records with exact operands (SPEC-01 IM-106) | Scientific notation for integers (SPEC-04 LS-210); approximate displays without a marker (SPEC-06 6.1) |
| Accelerate | It runs faster without changing any value, fault, or fuel count. | Schedules, after Halide [4] (SPEC-02 H-1 to H-5); multiword and range-proven lowering (SPEC-02 B-10, B-18); exact parallel location of `fold_checked` faults by prefix sums [15] (SPEC-01 IM-83) | Silent narrowing; reassociating `sum_sat`; coarrays as a core feature |
| Reproduce | A result can be repeated, compared, and replayed. | Counter-based randomness, after Random123 [16] (SPEC-02 section 13); recordings and replay (SPEC-03 4.3); content-addressed builds (SPEC-06 4.2); the self-hosting fixpoint, in which the compiler's emitted C stops changing when the compiler rebuilds itself (SPEC-09 section 11) | Unrestricted compile-time execution (SPEC-04 section 14); ambient clocks or environment (SPEC-03 H-8) |

Procedure (Specified):

1. The proposal names its verb, its owning section, and every section whose text it touches.
2. It states its conformance impact: new fixtures, changed fixtures (which force a new profile), and the backends that must implement or refuse it.
3. It states the evidence it needs before it becomes Specified: a derivation, a fixture set checked against the independent reference (SPEC-09 section 9), or a measurement with a receipt (SPEC-06 section 12).
4. It is recorded as Proposed in the owning section, and listed as an open question, until it is decided.

## 6. Audience ladder (Specified)

The intended order is that engineers first want to experiment with CINT, then find it easy to use, and then develop for it. Python and AI developers are a primary audience. Fortran teams migrating critical systems are the second. Engineers who check claims read the specification as evidence, and the claims discipline of every section is written for them.

| Rung | Reader | What they need first | Start here | Then |
|---|---|---|---|---|
| Experiment | A Python or AI developer who has never seen CINT | Run something in a minute, see exact values, and see a fault explained. | SPEC-04 section 19 (the exact ledger tutorial); SPEC-06 16.1 (session A); section 4 above | SPEC-03 6.7 (a kernel on a NumPy array); SPEC-05 section 8 (a 1.58-bit linear layer, after BitNet b1.58 [17]) |
| Use | An engineer putting an integer computation under a Python, C, or Fortran system | Clear interfaces, no hidden copies, reproducible results, and honest refusals | SPEC-03 sections 5 to 8 (C ABI, Python bridge, Fortran wrappers, float boundary) | SPEC-02 sections 3 to 6 (views, kernels, aliasing, publication); SPEC-06 sections 9 and 10 (differential debugger, replay) |
| Develop for | A contributor writing a backend, a library, or the compiler | Exact meaning, conformance fixtures, and the rules a backend must keep | SPEC-01 (integer machine); SPEC-02 sections 11 and 12 (canonical faults, backends) | SPEC-09 (compiler, SIR, emitted C, fixpoint); SPEC-07 (security model) |
| Check | A reviewer of the evidence behind a claim | What is claimed, what is measured, by what method, and what is not claimed | SPEC-08 section 2 (definitions of equal and better) and section 11 (gates) | SPEC-07 section 14 (claims not made); SPEC-09 section 13 (disclosure package) |

Two audience rules apply everywhere. First, a newcomer's first error message must be exact and must propose an explicit fix that never silences a fault (SPEC-04 17.6). Second, no performance figure appears without its workload, machine, compiler configuration, method, and receipt (SPEC-06 11, SPEC-09 DISC-04).

## 7. The measured bar (Specified)

The results of the predecessor integer engine are the bar the CINT engine must equal or exceed. Its public prototype, its verifier, and the measurements behind its results are published in spaceFOM-integer [18]. The bar also includes accuracy against closed-form, NASA GMAT, and JPL Horizons references. SPEC-08 section 2 states the bar, defines "equal" as passing every E (equal) gate against those results and "better" as passing each declared B (better) gate, and section 6 specifies a Proposed fix for a boundary-tick gap in the predecessor's results. This edition does not repeat the figures.

## 8. Implementation order (Specified: the order; the acceptance tests are owned by SPEC-09 section 14)

SPEC-09 section 14 gives the acceptance test of each stage.

| Stage | Deliverable | Acceptance test (summary) |
|---|---|---|
| T0 | Independent exact reference for scalar arithmetic (`cint_ref`) and the fixture formats | Boundary fixtures agree with the reference (SPEC-01 IM-171). |
| T1 | C17 seed compiler [11], runtime, and C reference for scalars | Seed compiler conformance on `cint-boot-1`; no warnings; the symbol audit of the emitted C (SPEC-09 7.11) |
| T2 | The compiler in CINT, through SIR and the C backend, with `cint run`, `cint build`, and `cint test` | All T1 cases pass on the compiler built by the seed compiler, and the T2 scalar surface agrees with `cint_ref` (SPEC-09 T2 row). The fixpoint is not a T2 gate; it is a T6 gate (SPEC-09 STAGE-02). |
| T3 | One kernel path end to end: shaped inputs, exclusive output, Python entry, C reference, and one GPU backend | The stencil and the `I8` and ternary matrix multiply agree on `cint_ref`, the C reference, and the GPU, including a deliberate overflow. |
| T4 | Workbench on the same core | A saved case restores and reproduces on another machine. |
| T5 | Expansion: SIMD, more GPU backends, wide integers, packed ternary, Fortran wrappers, and the flight engine | Each addition passes the conformance suite without changing earlier receipts. |
| T6 | Self-hosting gate and public disclosure | S2 equals S3 on every release toolchain (S2 is the C emitted by the compiler built from the seed compiler's output, and S3 is the C emitted by the compiler built from S2; SPEC-09 section 11), and a third party reproduces the published digests, in the sense of a reproducible build [19]. |

## 9. Document map (Specified)

Each topic has exactly one owning section. Where two sections disagree, the owning section governs, and the other section is corrected (SPEC-01 IM-2, SPEC-02 C-1 to C-5, SPEC-04 summary, SPEC-09 1.2).

| Document | Title | Owns |
|---|---|---|
| [SPEC-00P.1](SPEC-00P.1-OVERVIEW.md) | Overview | Identity, profile layering, the admission test, the audience ladder, and this map |
| [SPEC-01P.1](SPEC-01P.1-INTEGER-MACHINE.md) | Integer machine | Numeric types, literals and context typing, operators and their forms, division and rounding, fixed point, reductions, evaluation order, compile-time semantics, the fault model and fault codes, operation identifiers, fuel (`fuel-v1`), canonical serialization, state framing, and revision and execution identity |
| SPEC-02 (no public edition yet) | Kernels and views | The view descriptor and address map, kernel declarations, owned blocks, the aliasing decision procedure, publication and the in-place operation, the SPMD model, in-kernel reductions, schedules, canonical fault selection and the GPU fault protocol, backend capability profiles, and counter-based randomness |
| SPEC-03 (no public edition yet) | Memory, host boundary, and interop | The buffer registry; arenas, pools, and generation-checked handles; view scope; effect classes; recordings and replay; the C ABI; the Python bridge; Fortran wrappers; the float boundary; and the migration assistant |
| [SPEC-04P.1](SPEC-04P.1-LANGUAGE-SURFACE.md) | Language surface | Lexical structure, the spelling of every construct and built-in, declarations, statements, the print statement and formatting, modules, tests, attributes, compile-time execution rules, the diagnostics layout and C codes, and the grammar |
| SPEC-05 (no public edition yet) | Ternary representations and kernels | `T1`, `T27`, balanced-ternary digit operations and `rescale3`, packed trit arrays `PT5` and `PT4`, `tdot` and its accumulation-width theorem, the 1.58-bit inference pipeline, and the legacy mapping |
| SPEC-06 (no public edition yet) | Workbench, debugger, and tooling | The `cint` command, projects without build scripts, the workbench session, checkpoints and branches, the exact inspector, `explain`, fault presentation, the differential debugger, time travel by replay, benchmarks, and receipts as used by the tools |
| SPEC-07 (no public edition yet) | Security model | The threat model, integer cryptographic primitives, developer identity, capability manifests, admission and the default sandbox, build records, host-side use, the service boundary of a hosting environment, release signing, and claims not made |
| SPEC-08 (no public edition yet) | Bolt-under integration: HLA, SpaceFOM, Trick, Fortran | The measured bar and the definitions of equal and better, the canonical stream and gateway, execution control, the boundary-tick fix, the flight ABI and Fortran wrappers, the flight engine port, the qualification gates, and receipts |
| SPEC-09 (no public edition yet) | Compiler architecture, bootstrap, and self-hosting | The seed compiler, `cint-boot-1`, SIR and its hashed encoding, the emitted-C contract, backend plug-in points and identifiers, the conformance suite and `cint_ref`, the fixpoint, build receipts, the disclosure package, and the staged roadmap |
| [GLOSSARY-P.1](GLOSSARY-P.1.md) | Glossary | One-line definitions of every defined term and type used by the published editions, with the section that defines it |
| [REFERENCES-P.1](REFERENCES-P.1.md) | References | The shared bibliography: every work the published editions cite, once, with a stable key |
| [README.md](README.md) | Index | The current edition of each specification, the editions of each release, and the errata list |

"Bolt-under" is this specification's plain word for the attached-processor idea: an exact engine attached under a host system, as Floating Point Systems attached its AP-120B array processor to a host computer [20]. SPEC-08 specifies the interfaces it uses: HLA [21], SpaceFOM [22], the Trick Simulation Environment [23] with TrickHLA [24], and the C descriptors of Fortran 2018 [5].

Reading order for a complete review: SPEC-00, SPEC-01, SPEC-02, SPEC-03, SPEC-04, then SPEC-05 to SPEC-09 in any order. In this release, SPEC-00, SPEC-01, and SPEC-04 are published.

## 10. Out of scope of the specification set (Specified)

- Floating-point types and operations inside CINT. Floating-point values exist only at the host boundary, through explicit conversions (SPEC-01 IM-70, SPEC-03 section 8).
- Physical memory, identity-mapped address spaces, and booting hardware. CINT programs run hosted (SPEC-03; SPEC-07 N2).
- General-purpose references with arbitrary lifetimes and a borrow checker. Views are second-class (SPEC-03 M-21).
- Shared-memory threads in source, and workgroup-shared memory, barriers, and atomics in kernels, for `cint-core-1` (SPEC-02 W-1, SPEC-04 LS-271).
- Performance claims of any kind until receipts exist (SPEC-06 11, SPEC-09 DISC-04).
- Safety, security, or coding-standard compliance and certification claims (SPEC-07 section 14).
- Numerical adequacy of a user's method, or of a port relative to a floating-point original (SPEC-01 IM-160, SPEC-03 F-12).
- Endorsement, affiliation, or validation by any organization whose standards or tools are cited, NASA included. Tools are named only as tools (SPEC-08 section 15).
- Extending the legacy profile `cint-bt27-legacy`.

## 11. Open questions (Open)

These items concern this section's sources.

1. Fortran edition. ISO/IEC 1539-1:2018 [5] has been replaced by ISO/IEC 1539-1:2023 (Fortran 2023). Which edition CINT's Fortran wrappers target is undecided.
2. HLA edition. IEEE 1516-2010 [21] has been followed by IEEE 1516-2025 (HLA 4). Which edition the CINT gateway targets is undecided.
3. [10] is not pinned to a DLPack version. The page showed 0.6.0 when retrieved.
4. [1] rests on a community archive, not a publisher-maintained source.
5. [20] was confirmed by title, venue, pages, and DOI, but its abstract and full text were not read when the reference was checked. The statement that the AP-120B was attached to a host computer rests on the paper's subject, not on a quoted passage.
6. Not applicable to this edition: it concerned run-time infrastructure versions cited only with measurements that this edition does not repeat.
7. [5], [11], [22], and [21] were confirmed by number, title, and publisher; their clause-level wording was not compared with the full text.
8. Not applicable to this edition: section 7 cites only public evidence.
9. Closed. The References list uses the wording of the shared bibliography, [REFERENCES-P.1](REFERENCES-P.1.md).

## References

Entries use the wording of the shared bibliography, [REFERENCES-P.1](REFERENCES-P.1.md); the bracketed key names the shared entry. A "Used here" note names the parts this document relies on.

1. [Davis-HolyC] Terry A. Davis. "HolyC." TempleOS documentation, file `Doc/HolyC.DD` (undated). No publisher-maintained locator exists; community archive of the TempleOS source: https://github.com/cia-foundation/TempleOS/blob/archive/Doc/HolyC.DD. Read 2026-10-02.
2. [Pharr-2012] Matt Pharr and William R. Mark. "ispc: A SPMD Compiler for High-Performance CPU Programming." *2012 Innovative Parallel Computing (InPar)*, pages 1-13, 2012. DOI 10.1109/InPar.2012.6339601.
3. [Henriksen-2017] Troels Henriksen, Niels G. W. Serup, Martin Elsman, Fritz Henglein, and Cosmin E. Oancea. "Futhark: Purely Functional GPU-Programming with Nested Parallelism and In-Place Array Updates." *Proceedings of the 38th ACM SIGPLAN Conference on Programming Language Design and Implementation (PLDI 2017)*, pages 556-571, 2017. DOI 10.1145/3062341.3062354.
4. [RaganKelley-2013] Jonathan Ragan-Kelley, Connelly Barnes, Andrew Adams, Sylvain Paris, Frédo Durand, and Saman Amarasinghe. "Halide: A Language and Compiler for Optimizing Parallelism, Locality, and Recomputation in Image Processing Pipelines." *Proceedings of the 34th ACM SIGPLAN Conference on Programming Language Design and Implementation (PLDI 2013)*, pages 519-530, 2013. DOI 10.1145/2491956.2462176.
5. [ISO-1539-1-2018] ISO/IEC JTC 1/SC 22. *ISO/IEC 1539-1:2018, Information technology: Programming languages: Fortran: Part 1: Base language* (Fortran 2018). International Organization for Standardization, 2018. https://www.iso.org/standard/72320.html. ISO has replaced it with ISO/IEC 1539-1:2023 (Fortran 2023). Final committee draft: J3/18-007r1, https://j3-fortran.org/doc/year/18/18-007r1.pdf. Used here: the DO CONCURRENT construct and clause 18, interoperability with C (`ISO_Fortran_binding.h`, `CFI_cdesc_t`). Which edition CINT targets is Open (section 11).
6. [W3C-WGSL] W3C GPU for the Web Working Group. *WebGPU Shading Language*. W3C Candidate Recommendation Draft, 21 September 2026. https://www.w3.org/TR/2026/CRD-WGSL-20260921/ (latest version: https://www.w3.org/TR/WGSL/). Used here: section "Scalar Types."
7. [FIPS-180-4] National Institute of Standards and Technology. *Secure Hash Standard (SHS)*. FIPS PUB 180-4, August 2015. DOI 10.6028/NIST.FIPS.180-4.
8. [OCallahan-2017] Robert O'Callahan, Chris Jones, Nathan Froyd, Kyle Huey, Albert Noll, and Nimrod Partush. "Engineering Record and Replay for Deployability: Extended Technical Report." arXiv:1705.05937, 2017. https://arxiv.org/abs/1705.05937.
9. [PEP-3118] Travis Oliphant and Carl Banks. *PEP 3118: Revising the buffer protocol*. Python Enhancement Proposal (status Final), 2006. https://peps.python.org/pep-3118/.
10. [DLPack] DLPack contributors. *DLPack: Open In Memory Tensor Structure*. Repository https://github.com/dmlc/dlpack (newest release v1.3, 2026-01-26; v1.0 released 2024-09-09). Documentation https://dmlc.github.io/dlpack/latest/, including the C API (`c_api.html`) and the Python specification (`python_spec.html`); the documentation pages are headed "DLPack 0.6.0 documentation." Read 2026-10-02. Used here: the DLPack version to pin is Open (section 11).
11. [ISO-9899-2018] ISO/IEC JTC 1/SC 22. *ISO/IEC 9899:2018, Information technology: Programming languages: C* (C17). International Organization for Standardization, 2018. https://www.iso.org/standard/74528.html. ISO has replaced it with ISO/IEC 9899:2024. Working drafts with the same clause numbering: WG 14 N2176 (ballot text) and N2310, https://www.open-std.org/jtc1/sc22/wg14/www/docs/n2310.pdf. Used here: CINT targets the 2018 edition.
12. [Knuth-1997] Donald E. Knuth. *The Art of Computer Programming, Volume 2: Seminumerical Algorithms*, 3rd edition. Addison-Wesley, 1997. ISBN 978-0-201-89684-8. Used here: section 4.1, "Positional Number Systems" (balanced ternary).
13. [PSF-fractions] Python Software Foundation. "fractions: Rational numbers." *The Python Standard Library*, Python 3.14. https://docs.python.org/3/library/fractions.html.
14. [NumPy-Interop] NumPy Developers. "Interoperability with NumPy." *NumPy User Guide*. https://numpy.org/doc/stable/user/basics.interoperability.html. Read 2026-10-02. Used here: the buffer protocol and `numpy.from_dlpack` (see also https://numpy.org/doc/stable/reference/generated/numpy.from_dlpack.html).
15. [Blelloch-1990] Guy E. Blelloch. *Prefix Sums and Their Applications*. Technical Report CMU-CS-90-190, School of Computer Science, Carnegie Mellon University, November 1990. http://www.cs.cmu.edu/afs/cs.cmu.edu/project/scandal/public/papers/CMU-CS-90-190.html.
16. [Salmon-2011] John K. Salmon, Mark A. Moraes, Ron O. Dror, and David E. Shaw. "Parallel Random Numbers: As Easy as 1, 2, 3." *Proceedings of the 2011 International Conference for High Performance Computing, Networking, Storage and Analysis (SC11)*, article 16, 2011. DOI 10.1145/2063384.2063405.
17. [Ma-2024] Shuming Ma, Hongyu Wang, Lingxiao Ma, Lei Wang, Wenhui Wang, Shaohan Huang, Li Dong, Ruiping Wang, Jilong Xue, and Furu Wei. "The Era of 1-bit LLMs: All Large Language Models are in 1.58 Bits." arXiv:2402.17764, 2024. https://arxiv.org/abs/2402.17764.
18. [spaceFOM-integer] pennyleans. *spaceFOM-integer*. A prototype that publishes the output of an integer spaceflight simulator through the SISO Space Reference FOM (SpaceFOM) over HLA, with a verifier and the measurements behind its results; the simulator source is not included. https://github.com/pennyleans/spaceFOM-integer. Read 2026-10-06. Used here: the predecessor engine whose results are the bar of section 7.
19. [RB-Definitions] Reproducible Builds project. "Definitions." https://reproducible-builds.org/docs/definition/. Read 2026-10-02.
20. [Charlesworth-1981] Alan E. Charlesworth. "An Approach to Scientific Array Processing: The Architectural Design of the AP-120B/FPS-164 Family." *Computer* 14(9):18-27, IEEE, September 1981. DOI 10.1109/C-M.1981.220595.
21. [IEEE-1516-2010] IEEE. *IEEE Std 1516-2010, IEEE Standard for Modeling and Simulation (M&S) High Level Architecture (HLA): Framework and Rules*. IEEE, 2010. DOI 10.1109/IEEESTD.2010.5553440. IEEE lists this edition as Inactive-Reserved since 2021-03-25 and superseded by IEEE 1516-2025 (HLA 4), https://standards.ieee.org/ieee/1516/6687/. Used here: which edition the CINT gateway targets is Open (section 11).
22. [SISO-STD-018-2020] Simulation Interoperability Standards Organization (SISO). *SISO-STD-018-2020, Standard for Space Reference Federation Object Model (SpaceFOM)*, version 1.0 (document dated 25 October 2019; approved by the SISO Standards Activity Committee on 18 December 2019 and by the SISO Executive Committee on 9 January 2020). SISO, 2020. https://cdn.ymaws.com/www.sisostandards.org/resource/resmgr/standards_products/siso-std-018-2020_srfom.pdf.
23. [NASA-Trick] NASA Johnson Space Center. *Trick Simulation Environment*. https://github.com/nasa/trick. Newest release 25.1.1, 2026-09-04.
24. [NASA-TrickHLA] NASA Johnson Space Center, Simulation and Graphics Branch. *TrickHLA: IEEE 1516 High Level Architecture simulation interoperability middleware for the Trick Simulation Environment*. NASA Open Source Agreement 1.3. https://github.com/nasa/TrickHLA. Release v3.2.2, 2026-04-01.
