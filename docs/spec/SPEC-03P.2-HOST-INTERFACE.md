# SPEC-03P.2: The host interface: C and Python

SPEC-03P.2, edition 2 of the cint host interface specification, cint v0.2.3, 2026-10-23.

<a id="foreword"></a>

## Foreword

This document specifies how a program written in C or Python calls cint code, as cint 0.2.2 implements it: the libraries that `cint build --lib` writes, the C interface of those libraries, the host functions and standard services that cint code calls through `extern`, and the Python package `cint` that is built on them. It replaces SPEC-03P.1: it adds interface versions 3.3 and 3.4 and the header that a library build writes (7.2), the program arguments of `main` (7.2, 9.3, 9.4), host functions (7.6), extern calls in recordings and replay (9.3), and the standard services with the grants that cover them (9.4).

Each rule ends with one or more clause identifiers in brackets. An identifier, such as A-12 or P-20a, names one rule across editions and documents, and it is never reused. The letter says where the rule belongs: `M` memory, `H` the boundary between a host and its contexts, `A` the C interface, `P` the Python package, `X` the conversion of floating-point values and `RT` the runtime contract. In SPEC-02P.2 and SPEC-04P.3, identifiers that begin with `A`, `P` or `X` name rules of kernels, which are not the rules of this document that have the same identifiers. A rule that another edition states is cited in brackets with that edition's name, such as [SPEC-09P.2 RT-10]. Section numbers belong to this edition only.

<a id="introduction"></a>

## Introduction

A cint program can be a library as well as a script. A module that declares `export` functions and kernels, and has no `main` and no top-level statements, is built by `cint build --lib` into one shared library. A host program loads that library, creates a *context* for it and calls its exports. The exports run exactly as they would if cint code called them: every plain operator is checked, a fault stops the call with a fault record, and fuel is counted.

The running example of this document is a kernel that advances positions by velocities, and a function that totals an array:

```cint
// advance.ci: one kernel and one function, built as a library.
export kernel advance[n](in I64[n] pos, in I64[n] vel, in I64 dt, out I64[n] next) over [i: n] {
    next[i] = pos[i] + vel[i] * dt;
}

export I64 total[n](in I64[n] x) {
    return sum(x);
}
```

From Python, the exchange reads as follows (8.6 runs it in full):

```python
mod = cint.load("advance.ci")
ctx = mod.context()
ctx.advance(pos, vel, 16, next=nxt)
```

Three things cross the boundary, and this document is about each of them. Memory crosses as *buffers* that the host registers with the context and *views* of them; cint code never sees an address. Control crosses as calls through typed C functions, one for each export, which check every argument before any cint code runs. Outcomes cross as a status, a result, and, when the call faults, the same fault record that a cint caller would get. Calls also cross the other way: cint code calls functions of its host program that it declares with `extern` (7.6), and the runtime's own services for files and the terminal, within the grants that the host gives (9.4).

<a id="how-this-document-is-organized"></a>

### How this document is organized

Clauses 1 to 4 state what the document covers, the documents it relies on, its terms and what conformance means. Clause 5 describes how the state of a context is represented, and what a native pointer may be. Clause 6 describes buffers: the registry, ownership, leases, how outputs are published and views. Clause 7 is the C interface: statuses, the runtime header, the calling sequence with a complete C program, the reflection table, type signatures and host functions. Clause 8 is the Python package: loading and calling, borrowing arrays without a copy, DLPack, outputs and the hazards around them, the values that cross and a complete NumPy program. Clause 9 covers output effects, checkpoints, recordings and replay as a host sees them, and the standard services with the grants that cover them. Clause 10 works through further cases that run. Clause 11 lists future directions.

Subclauses of clauses 5 to 9 open with a short summary and then have some of these parts:

- **Synopsis**: the C declarations or Python signatures of the subclause.
- **Constraints**: rules a host program shall follow.
- **Semantics**: what the interface does.
- **Faults**: the faults the interface itself can raise, and what each record holds.
- **Examples** and **Notes**: informative.

Every example is a complete file. A C or Python file is shown once, with its name in its first line, and the commands that build and run it are shown in a `console` block with their output exactly as the cint 0.2.2 tools and Python 3.13 with NumPy 2.5.3 printed it on Linux x86-64 with gcc 13.3.0. A library file is named `libNAME.so` on Linux; other systems use their own suffix. A line `...` in shown output stands for lines left out.

<a id="scope"></a>

## 1 Scope

1. This document specifies, for profile `cint-core-1` as cint 0.2.2 implements it:
   - the canonical and the runtime representation of a context's state, and what a native pointer may be;
   - the buffer registry, the ownership of memory, leases and the publication of outputs;
   - the C interface of a library that `cint build --lib` writes: the status results, the context, the registry functions, the calling sequence, the reflection table and the canonical type signature;
   - the host functions that cint code calls through extern declarations: the host table, the host import descriptor, the call and its faults;
   - the Python package `cint`: loading, contexts, calls, borrowing through the buffer protocol and DLPack, outputs, the values that cross, the conversion of floating-point values and the exceptions;
   - output effects, checkpoints, recordings and replay, as a host program uses them;
   - the standard services `fs/1` and `console/1` and the grants that cover them, as the runtime carries them out on the host.
2. This document does not specify:
   - the language, including what a view, a kernel, a dispatch, a fault, fuel, an extern, a service and a grant mean, which SPEC-04P.3 specifies;
   - the byte encodings of fault records, states, checkpoints and recordings and the revision identity, which SPEC-01P.3 specifies;
   - how an executor carries out a dispatch, which SPEC-02P.2 specifies;
   - the `cint build` command and its options, which SPEC-06P.2 specifies;
   - the encoding of generated symbol names, which SPEC-09P.2 specifies;
   - what a host program does with memory it shares, and the behavior of host code.

<a id="normative-references"></a>

## 2 Normative references

The following documents are referred to in the text in such a way that some or all of their content constitutes requirements of this document. For dated references, only the edition cited applies.

- ISO/IEC 9899:2018, *Information technology: Programming languages: C* (C17). Used for the C interface: `<stdint.h>`, `static_assert`, and `atomic_exchange`.
- Travis Oliphant and Carl Banks. *PEP 3118: Revising the buffer protocol*. Python Enhancement Proposal, created 2006-08-28. Used for the buffer protocol: `Py_buffer`, its request flags and its format strings.
- Python Software Foundation. "Buffer Protocol." *Python/C API Reference Manual*, Python 3.14. Used for the buffer protocol as Python 3.14 defines it.
- Python Software Foundation. "struct: Interpret bytes as packed binary data," section "Format Characters." *The Python standard library*, Python 3.14. Used for the format characters that a buffer's format string takes from the `struct` module.
- DLPack contributors. *DLPack: Open In Memory Tensor Structure*, release v1.3, 2026-01-26: `dlpack.h` version 1.3 and the DLPack Python specification. Used for `DLManagedTensorVersioned`, its flags and the capsule protocol.
- Consortium for Python Data API Standards. *Python array API standard*, version 2023.12, `array.__dlpack__`. Used for the arguments of a DLPack exchange.
- National Institute of Standards and Technology. *Secure Hash Standard (SHS)*. FIPS PUB 180-4, August 2015. Used for SHA-256.
- IEEE. *IEEE Std 754-2019, IEEE Standard for Floating-Point Arithmetic*, 2019. Used for binary32 and binary64 and for roundTiesToEven.

<a id="terms-definitions-and-symbols"></a>

## 3 Terms, definitions and symbols

The terms of SPEC-04P.3 clause 3 apply, in particular value, type, fault, fault record, operation identifier, error, refusal, executor, entry, fuel, call depth, frame arena, semantic input, module, array, view, kernel, dispatch, arena, pool, handle, revision, effect class, extern, service, capability and grant set. The following terms are added. Other terms are defined where they first appear, in italic type.

<a id="term-host-program"></a>**3.1 host program**: a program written in another language, here C or Python, that calls cint code through the interface of this document.

<a id="term-library"></a>**3.2 library**: the shared library that `cint build --lib` writes for a program whose root module has no `main` and no top-level statements. It holds the program's compiled code and a copy of the runtime.

<a id="term-runtime"></a>**3.3 runtime**: the C code, declared in `rt/cint_rt.h` and `rt/cint_rt_host.h` and built as the library `cint_rt`, that holds contexts, buffers and fault records for compiled cint code, and that emitted C and the executor `cpu-sir-interp` call for checked arithmetic, fuel and call depth. Its interface is the runtime contract `cint-rt-3`.

<a id="term-context"></a>**3.4 context**: one instance of a program's state, held by a host program, in which entries run one at a time: its module variables, arenas and pools, its buffer registry, its counters and its fault record. A context persists across entries until the host destroys it.

<a id="term-export"></a>**3.5 export**: an exported function or kernel of the root module, which a host program calls through its wrapper.

<a id="term-wrapper"></a>**3.6 wrapper**: the C function of a library that checks a host's arguments, runs one export as one entry, and returns a status.

<a id="term-status"></a>**3.7 status**: the `int32_t` value a function of the C interface returns to say how a call ended.

<a id="term-refused-call"></a>**3.8 refused call**: a call whose arguments cannot be formed into cint values. Nothing runs, no fuel is charged, and the context is unchanged. A refused call is not a refusal in the sense of SPEC-04P.3 3.16, which concerns programs that an implementation cannot execute.

<a id="term-entry-fault"></a>**3.9 entry fault**: a fault raised by the checks an entry makes on its arguments after they were formed and before any of its code runs.

<a id="term-buffer"></a>**3.10 buffer**: a run of elements of one type in one placement, registered with a context. A *registration* records a buffer in the registry under a *buffer identifier*, and a *generation* that tells one lifetime of the registration from the next.

<a id="term-ownership"></a>**3.11 ownership**: which party frees a buffer's storage: the context, the runtime for a buffer it created, the Python package for a `Buffer`, or the host program for borrowed memory.

<a id="term-borrow"></a>**3.12 borrow**: the use of host memory by cint code without a copy. The host keeps the memory alive while it is registered.

<a id="term-lease"></a>**3.13 lease**: a period during which the runtime hands the host a read-only address of storage the runtime owns. In Python the same role is played by an *export* of a `Buffer` through the buffer protocol or DLPack.

<a id="term-publication"></a>**3.14 publication**: the moment at which the new contents of a dispatch's outputs become visible, which is when the dispatch completes without a fault.

<a id="term-copy-report"></a>**3.15 copy report**: the list of every copy an entry made: which argument, why, its element type and its element count.

<a id="term-canonical-representation"></a>**3.16 canonical representation**: the machine-independent form of a context's state, built from identifiers, typed values and declared lengths. The *runtime representation* is the in-process form: addresses, allocations and synchronization objects.

<a id="term-state-hash"></a>**3.17 state hash**: the SHA-256 digest of the canonical representation of a context's state (5.1), as defined in SPEC-01P.3, 3.12.

<a id="term-checkpoint"></a>**3.18 checkpoint**: the canonical bytes of a context's state, in an envelope that names the runtime contract, from which a context can be restored (9.2), as defined in SPEC-01P.3, 3.11.

<a id="term-recording"></a>**3.19 recording**: a file that holds a program's identity and code, a checkpoint of a context when recording began and the events that followed, from which the run can be replayed (9.3), as defined in SPEC-01P.3, 3.14.

<a id="term-reflection-table"></a>**3.20 reflection table**: the table a library carries that describes each export: its name, kind, effect class, parameter names and type signature, and its wrapper.

<a id="term-type-signature"></a>**3.21 type signature**: the canonical bytes that describe the parameter modes, parameter types and result type of an export or an extern.

<a id="term-host-function"></a>**3.22 host function**: a function of a host program that cint code calls through an extern declaration. The host program passes its host functions in a *host table* when it creates a context.

<a id="term-host-import-descriptor"></a>**3.23 host import descriptor**: the table a library carries that lists, for each module of the program, the externs that the module declares, with each one's name, effect class, parameter count and type signature.

<a id="conformance"></a>

## 4 Conformance

1. In this document, "shall" states a requirement on a host program or on an implementation, and "shall not" a prohibition. "May" states a permission.
2. A conforming implementation of this interface shall give each entry that a host program makes the outcome that SPEC-04P.3 specifies for it: the same output bytes, the same fault record and the same fuel as every other conforming implementation. A dispatch issued by a host call has the same meaning, checks and fault records as the same dispatch issued from cint code (7.1 paragraph 3; 10.3 shows one).
3. The interface is defined for little-endian hosts with 64-bit pointers: x86-64 and AArch64. A host program that breaks a constraint of this document, or that changes memory it has lent to a context while an entry runs, gets no outcome this document specifies.
4. An implementation may refuse a construct that it does not implement, with a diagnostic whose code begins with `C9`. SPEC-09P.2 lists what the tools of each release refuse.
5. No claim is made about host code itself: what it computes, whether it terminates, what it does with memory it shares with a context, and whether a host function keeps the promise of its effect class (7.6).

<a id="representations"></a>

## 5 Representations

<a id="runtime-and-canonical-representation"></a>

### 5.1 Runtime and canonical representation

A context's state has two forms. The runtime form is what a process holds: addresses, allocations, device handles. The canonical form is what a checkpoint, a recording and a state hash hold: identifiers, typed values and lengths, with no address in them. Two machines that run the same entries reach the same canonical state, whatever their addresses were.

**Semantics**

1. <a id="M-1"></a>Every part of a context's state has a canonical representation. Two contexts are in the same state exactly when their canonical representations are the same bytes. The identifier counters (paragraph 3) are part of the state, so two contexts with the same values but different histories of registration are in different states [M-1].
2. <a id="M-2"></a>Nothing in the runtime representation enters the canonical representation, a state hash, the canonical fields of a fault record, or any value that cint code can read [M-2].
3. <a id="M-3"></a>The canonical representation of a context holds exactly these items, in the order and bytes that SPEC-01P.3 specifies [M-3]:
   - the revision identity, the profile `cint-core-1` and the fuel model `fuel-v1`;
   - the value of every module variable, by qualified name;
   - each arena and each pool, with its identifier, capacity, generations and contents;
   - each registration (6.1), with its identifier, generation, element type, extent, ownership, ceiling and publish mode, and none of the contents of its memory;
   - the counters: the next buffer, arena and pool identifiers and the number of entries made;
   - the fault record, or its absence;
   - the number of effects completed.
4. <a id="M-4"></a>Every multi-byte integer of the canonical representation is little-endian. Hosts are little-endian too, so borrowed memory is used as it is and never byte-swapped [M-4].
5. <a id="M-5"></a>The state hash is SHA-256 over the canonical representation, framed with the domain string `cint-core-1/state/v1` [M-5].

**Examples**

6. The checkpoint that 9.2 takes holds the value of `counter.tally` by its qualified name, its type and its value and the registry entries of two borrowed arrays carry their identifier, generation, element type, extent and modes but none of their contents.

<a id="what-a-native-pointer-may-be"></a>

### 5.2 What a native pointer may be

An address is a fact about one process. It is used to reach memory and to decide whether two arrays overlap, and for nothing else.

**Semantics**

1. <a id="M-6"></a>cint source has no pointer type, no address-of operator and no conversion between an integer and an address. A program cannot observe, compare or hash an address [M-6].
2. <a id="M-7"></a>A native pointer may be [M-7]:
   - the base of a registered buffer, held by the runtime;
   - a field of a buffer descriptor that the host passes when it registers memory (7.2);
   - the read-only address of a lease of a buffer the runtime created, valid until the lease ends (6.3);
   - compared by the runtime with another to decide whether two registrations overlap (6.5).
3. <a id="M-8"></a>A native pointer is never the identity of an allocation, which is its buffer identifier or its handle. It is never part of a canonical value, checkpoint, recording, fault record or state hash, never converted to or from a cint integer, and never used to order objects [M-8].
4. <a id="M-9"></a>The runtime may move storage it owns between entries. Buffer identifiers and handles are unaffected. A host program that keeps the address of such storage outside a lease has no assurance that it stays valid [M-9].

**Examples**

5. In 10.1, the host reads the output of `scale` through the address of a lease and ends the lease; it reads the next output with `cint_buffer_read`, into an array of its own, and keeps no address of the runtime's storage after `cint_lease_end`.

<a id="buffers-and-ownership"></a>

## 6 Buffers and ownership

<a id="the-buffer-registry"></a>

### 6.1 The buffer registry

Each context keeps a table of the memory it may reach. A host program adds an entry, a registration and receives an identifier and a generation; views name the identifier and the generation, never an address.

**Semantics**

1. <a id="M-10"></a>Each context has one buffer registry. An entry of the registry holds [M-10]:
   - `id`, a nonzero `U64` buffer identifier;
   - `generation`, a `U64` that release advances and publication leaves unchanged;
   - the element type and the extent, a count of elements of at least 0;
   - the ownership: context, created by the runtime, or borrowed (3.11);
   - the placement: host memory, or `device(1)` on a context with a device; placement is not canonical;
   - the ceiling, `read` or `write`: the most any view of the buffer may do;
   - the publish mode, `none` or `copy`, which says whether a borrowed buffer may receive a dispatch's output (6.4);
   - the base address and the count of live leases, which are runtime representation only.
2. <a id="M-11"></a>Buffer identifiers count from 1 in the order of registration and are never reused while the context exists. Each registration starts at generation 1. An identifier depends only on the order of registrations, never on an address, a time or a thread. A restore (9.2) sets the next buffer, arena and pool identifiers each to the greater of the checkpoint's value and the context's own, so that no identifier is issued twice in the life of a context [M-11].
3. <a id="M-11a"></a>A restore (9.2) removes every registration of memory the context does not own, and its identifiers are not issued again. A C host registers such memory again; the Python package does it on the next use of the `View` or `Buffer` (8.2) [M-11a].
4. A context holds at most `max_buffers` live registrations, a field of its configuration (7.2) whose default is 256. A registration beyond that returns `CINT_RESOURCE`.

**Examples**

5. In 10.1, the text `"123"` is registered first and gets identifier 1; after it is released, the text `"12x"` gets identifier 2, not 1 again. A view that names identifier 99, which was never issued, is a refused call.

<a id="ownership"></a>

### 6.2 Ownership

Every piece of storage has one party that frees it, and that party never changes behind the host's back.

**Semantics**

1. Storage owned by the context, which is its module variables, arenas, pools and frame arena, lives and dies with the context. Its contents are part of the canonical state.
2. Storage that the runtime creates for the host with `cint_buffer_create` is owned by the runtime. It is zero-filled when created, freed by `cint_buffer_release`, and freed with the context by `cint_ctx_destroy`.
3. <a id="P-17"></a>A `Buffer` of the Python package owns its storage. Each registration of it ends before the storage is freed, by `Buffer.release()` or by garbage collection. Destroying a context ends that context's registrations and leaves the storage with the `Buffer`, and storage never passes between the package and a context [P-17].
4. Borrowed memory belongs to the host program. The runtime never frees it, and the host program shall keep it valid and in place until the registration ends.
5. Ownership never moves implicitly. Only a copy that the host asks for, or one listed in the copy report (6.4), moves contents from the storage of one party to the storage of another.

**Examples**

6. In 10.1, the array `xs` is borrowed memory: the host program owns it, and releasing its registration leaves the memory with the host. The output `y` is created by `cint_buffer_create`, so the runtime owns it; it is zero-filled when created and freed by `cint_buffer_release` before the context is destroyed.

<a id="leases-and-exports"></a>

### 6.3 Leases and exports

A lease gives a host program a read-only address for a bounded time, so that it can read an output that the runtime holds without copying it. In Python, an export through the buffer protocol or DLPack plays the same role.

**Synopsis**

```text
cint_status cint_buffer_lease(cint_ctx *ctx, cint_buffer_id id, const void **out_ptr, uint64_t *out_lease);
cint_status cint_lease_end(cint_ctx *ctx, uint64_t lease);
cint_status cint_buffer_read(cint_ctx *ctx, cint_view view, void *dst, size_t dst_bytes);
```

**Constraints**

1. A host program shall not write through a leased address, and shall not use it after `cint_lease_end`. Either is outside every assurance of this document.

**Semantics**

2. <a id="A-9"></a>The C interface returns the address of storage the runtime owns only through a lease. `cint_buffer_lease` gives a read-only address of a created buffer's current storage and a lease number; lease numbers count from 1 and are never reused. The address stays valid until `cint_lease_end` of that number. Leasing borrowed memory, or ending an unknown lease, is refused with reason `CINT_REFUSAL_LEASE`. While a buffer has a live lease, an entry that binds it to an `out` or `inout` parameter returns `CINT_BUSY`, runs nothing and records nothing. A host that wants a copy instead calls `cint_buffer_read`, which copies a view's elements in row-major order of their indices into `dst`, which shall hold exactly their bytes [A-9].
3. <a id="P-18"></a>A `Buffer` of the Python package offers no writable export. Its buffer-protocol exports are read-only, its DLPack exports are versioned capsules marked `DLPACK_FLAG_BITMASK_READ_ONLY`, and the package counts them. Using a `Buffer` as an output while it has a live export raises `cint.Busy` with code `E_ALIAS` [P-18].

**Examples**

4. In 10.1, a call that would write a leased buffer returns status 3, `CINT_BUSY`; after `cint_lease_end` the same call runs. In 8.4, a NumPy array made by `numpy.asarray(buf)` holds an export, and the next call that writes `buf` raises `Busy`.

**Notes**

5. The read-only mark is a request to the consumer, not a protection. A consumer that ignores it, through raw addresses, `__array_interface__`, or a DLPack consumer that has no read-only concept, can still write, and such a write is outside every assurance. NumPy 2.5.3 honors the mark: `numpy.from_dlpack` of a `Buffer` gives an array whose writes raise `ValueError` (8.3). CuPy 14.2.0 and PyTorch 2.14.1 accept a read-only DLPack capsule without honoring the mark, so a write through the array they return reaches the `Buffer`'s memory.

<a id="outputs-and-publication"></a>

### 6.4 Outputs and publication

A kernel's outputs change all at once or not at all. A function's `inout` arrays change as its statements run. This subclause says how each reaches memory the host owns.

**Semantics**

1. <a id="M-30"></a>Publication is per dispatch. An array bound to an `out` or `inout` parameter of a dispatch is pending from the start of the dispatch until it ends. When the dispatch completes, its new contents are published and later dispatches of the same entry read them. When the dispatch faults, nothing is published: the array holds exactly what it held before the dispatch, whoever owns it. Arrays published by earlier dispatches of the same entry keep their published contents (SPEC-04P.3 6.13.6) [M-30].
2. <a id="RT-19"></a>The runtime computes every output of a dispatch into staging storage of its own, zero-filled, with an `inout` argument's elements copied in first, and copies the result into the argument's memory when the dispatch completes [RT-19].
3. <a id="M-30a"></a>A borrowed buffer may be bound to an `out` or `inout` parameter of a dispatch only when its registration declares the publish mode `copy`. A borrowed buffer registered with `none` faults at entry (Faults below). No executor writes a borrowed output directly during a dispatch [M-30a].
4. <a id="M-30b"></a>A function, which has no `out` parameter, writes an array bound to its `inout` parameter in place, as it writes its own memory, with no staging and no copy. After a fault the array holds the stores of the statements that completed, those of callees included; sequential stores are not rolled back. Such a binding needs a writable registration, and its publish mode is not consulted [M-30b].
5. <a id="M-31"></a>Every copy an entry makes is listed in its copy report, with the reason, the element type and the number of elements. The reasons are `staging` for the output of a dispatch, `publish-copy` for an output into borrowed memory, `snapshot` and `fault-readback`. An entry with no copies reports `no copies`. The report holds element counts, not bytes [M-31].
6. <a id="P-15"></a>In Python, a borrowed `View` may be bound to a kernel's `out` or `inout` parameter only if it was created with `writable=True, publish="copy"`, and the copy is listed in `Context.last_entry()`. A `Buffer` is the output that needs no borrowed memory. A function's `inout` parameter takes a `View` created with `writable=True`, or a `Buffer`, and is written in place with nothing in the copy report [P-15].

**Faults**

7. `E_UNSUPPORTED`, operation `bind.limit`, is raised at entry when a borrowed buffer registered with publish mode `none` is bound to an `out` or `inout` parameter of a dispatch. The operand is the parameter's index as an `I64`, counted from 0 in declaration order. Nothing runs and the dispatch publishes nothing.

**Examples**

8. 8.4 shows each case from Python, and 10.2 shows an entry with two dispatches whose second faults: the first dispatch's output stays published, and the second's is not.

**Notes**

9. A caller that needs all or nothing from a function passes a copy and copies it back when the entry succeeds.

<a id="views-and-scope"></a>

### 6.5 Views and scope

A view names a registered buffer and says which of its elements to use and how: its element type, rank, extents, strides and permission. What a view means inside cint code is SPEC-04P.3 6.12; this subclause covers what a host supplies.

**Semantics**

1. A host passes each array argument as a view: a buffer identifier and generation, an element type, a rank from 1 to 4, an origin and for each dimension an extent, a stride in elements and a lower bound, with a permission, `read` or `write`. Its admission, indexing and lower bounds follow SPEC-04P.3 6.12.
2. <a id="A-8a"></a>Strided host memory whose first logical element is not its lowest address, as with negative strides, is registered from its lowest reachable byte, with an extent equal to the reachable span in elements, and the view's origin is the offset of the first logical element from that base. The Python package does this for every array it borrows, and a C host does the same [A-8a].
3. <a id="H-13"></a>Read-only views may overlap. A view bound to an `out` or `inout` parameter shall not overlap any other array argument of the entry. Overlap is decided by comparing the byte ranges of host memory across every registration, not only within one buffer identifier, so two registrations of the same memory, even with different element types, overlap. Memory that is mapped at two virtual addresses cannot be detected and is outside every assurance [H-13].
4. A view of borrowed memory exists for one entry: cint code cannot keep it after the entry returns (SPEC-04P.3 6.12).
5. <a id="M-28"></a>Only device code indexes device memory. On a context with a device, an entry checks the placement of each view it is given (7.1): a kernel that the host calls takes views of device memory only, and a function takes views of host memory, except for a parameter that its body only passes, whole, to dispatches, which may be in either placement. A kernel that cint code dispatches on such a context also reaches host memory: for that dispatch the runtime maps into the device the host memory its views reach and the storage the context owns. A view in the wrong placement faults `E_UNSUPPORTED`, operation `bind.placement`. A `copy` between views of different placements goes through the runtime. `cint_buffer_read` refuses a view of device memory with reason `CINT_REFUSAL_UNSUPPORTED`, and the Python package refuses to read device memory on the host; `cint.copy` copies it to host memory [M-28].

**Faults**

6. `E_ALIAS`, operation `bind.alias`, is raised at entry when a written view overlaps another array argument. Its operands are the written parameter's index and the other parameter's index, as `I64`. 8.4 raises it by passing one NumPy array as both input and output.

**Examples**

7. In 8.2, example 14, a reversed array is registered from its lowest address with a negative stride in each dimension, and the copy report of that call lists no copies.

<a id="the-c-interface"></a>

## 7 The C interface

<a id="status-results"></a>

### 7.1 Status results

Every function of the C interface that can fail returns a status. A status says whether the call ran, and when it ran, whether it completed; the fault record, the error result and the refusal reason give the detail.

**Synopsis**

```text
#define CINT_OK        0   /* completed; an error result also completes */
#define CINT_FAULT     1   /* faulted, at entry or while running */
#define CINT_REFUSED   2   /* the arguments could not be formed; nothing ran */
#define CINT_BUSY      3   /* another call is active, or an output is leased */
#define CINT_FAULTED   4   /* the context holds a fault; nothing ran */
#define CINT_RESOURCE  5   /* the allocator, the registry or a device had no room */
#define CINT_DIVERGED       6   /* replay found a difference (9.3) */
#define CINT_UNREPLAYABLE   7   /* replay cannot go on (9.3) */

cint_status cint_ctx_fault(const cint_ctx *ctx, cint_fault_record *out);
cint_status cint_fault_get(const cint_ctx *ctx, uint8_t *buf, size_t cap, size_t *len);
cint_status cint_ctx_refusal(const cint_ctx *ctx, uint32_t *reason);
cint_status cint_ctx_error(const cint_ctx *ctx, const cint_error_set **set, uint64_t *tag);
cint_status cint_fuel_consumed(const cint_ctx *ctx, int64_t *out);
cint_status cint_ctx_clear_fault(cint_ctx *ctx);
```

**Semantics**

1. <a id="A-4"></a>A function that can fail returns a `cint_status`, and writes its out parameters only when it returns `CINT_OK`. `cint_abi_version` and `cint_ctx_destroy` cannot fail and return no status. Reading the fault of a context that holds none returns `CINT_OK` with a record whose code is 0 [A-4].
2. <a id="A-6"></a>The statuses and the state of the context after each are [A-6]:

| Status | Value | Meaning | Context afterwards |
|---|---|---|---|
| `CINT_OK` | 0 | The entry completed and every dispatch published. An entry that returns an error value completes too (7.3). | Ready |
| `CINT_FAULT` | 1 | cint code faulted, at entry or while running. | Faulted |
| `CINT_REFUSED` | 2 | The arguments could not be formed into cint values. | Unchanged |
| `CINT_BUSY` | 3 | Another call is active on the context, or an output has a live lease. | Unchanged |
| `CINT_FAULTED` | 4 | The context holds a fault and runs no entry until it is cleared or restored. | Unchanged |
| `CINT_RESOURCE` | 5 | The allocator, the registry or a device had no room. It is not a cint fault. | Unchanged before an entry |
| `CINT_DIVERGED` | 6 | Replay found a difference from the recording. | Replay stopped |
| `CINT_UNREPLAYABLE` | 7 | Replay reached an event it cannot reproduce. | Replay stopped |

3. <a id="H-12"></a>A refused call and an entry fault are told apart by when the check fails. A refused call is one whose arguments cannot be formed: an unknown or released buffer identifier, a descriptor or view with an invalid field, a view that reaches outside its registration, a `Bool` byte other than 0 or 1, a fuel budget below -1 (paragraph 9), or, in Python, an argument of a refused kind. It charges no fuel, consumes no dispatch number, makes no registration and leaves the context unchanged. An entry fault is one whose arguments were formed and then failed a check of the entry (Faults below). It charges no fuel, leaves the context Faulted and for a kernel consumes the dispatch number. A host-issued dispatch therefore has the same outcome as the same dispatch issued from cint code [H-12].
4. <a id="A-7"></a>After `CINT_FAULT`, the context holds the state that SPEC-04P.3 5.2.3 describes: stores of completed statements remain, the outputs of the faulting dispatch are not published, and outputs published by earlier dispatches remain. The context then answers every entry with `CINT_FAULTED` until the host calls `cint_ctx_clear_fault` or restores a checkpoint [A-7].
5. The first fault of an entry writes the record once, and nothing after it in that entry changes it. Only the host clears it, as SPEC-09P.2 7.10 states [SPEC-09P.2 RT-10].
6. <a id="A-7a"></a>`cint_ctx_clear_fault` clears the fault record and changes nothing else: no registration, buffer, view, lease or counter. The fault record is part of the canonical state, so clearing it changes the state hash [A-7a].
7. <a id="A-7b"></a>A fault ends the entry without running its deferred statements, so state that a `defer` would have restored stays as the fault left it. The host releases what it acquired around an entry, such as registrations, borrowed memory and leases, in code that runs for every status. To bring a faulted context back to a known state, the host restores a checkpoint or clears the fault and then calls an entry that reinitializes that state, in that order. The context is the only fault boundary: work that must survive its own fault runs in a context of its own, with its inputs and outputs copied explicitly [A-7b].
8. <a id="A-17"></a>`cint_ctx_fault` copies the held fault record into a `cint_fault_record`, a structure whose code, operation, operands, position, kernel address and stack mirror the record. `cint_fault_get` gives the record's canonical bytes, which SPEC-01P.3 encodes: it sets `*len` to their length, 0 when no fault is held and copies `min(cap, *len)` bytes, so a call with `cap` 0 asks for the length. Conformance compares these bytes, never the structure. `cint_fuel_consumed` gives the fuel the last entry consumed; a refused call is not an entry and leaves it as it was [A-17].
9. <a id="H-11"></a>Each wrapper takes the fuel budget of its entry as an `int64_t`. A budget `B` of 0 or more is that entry's budget. The value -1, `CINT_FUEL_UNBOUNDED`, means no budget: no `E_FUEL` occurs, and fuel is still counted. Any other negative value is a refused call. Fuel consumed starts from 0 at each entry, so a budget applies to one entry [H-11].
10. `cint_ctx_refusal` gives the reason for the last refused call that a view or a scalar caused, as one of the `CINT_REFUSAL_*` values below, or `CINT_REFUSAL_NONE` (0) when the last call was not refused. A fuel budget below -1 is refused with reason 0.

| Reason | Value | The argument |
|---|---|---|
| `CINT_REFUSAL_BUFFER` | 1 | names no registration |
| `CINT_REFUSAL_GENERATION` | 2 | names a released registration |
| `CINT_REFUSAL_TYPE` | 3 | has an unknown type, nonzero reserved bytes or a type other than its registration's |
| `CINT_REFUSAL_RANK` | 4 | has a rank outside 1 to 4 |
| `CINT_REFUSAL_SIZE` | 6 | has an element offset beyond the `I64` range |
| `CINT_REFUSAL_EXTENT` | 7 | has a negative origin or extent, or reaches outside its registration |
| `CINT_REFUSAL_ALIGN` | 8 | starts at an address not aligned for its type |
| `CINT_REFUSAL_PERMISSION` | 9 | is a write view of a read-only registration |
| `CINT_REFUSAL_CALL` | 10 | is otherwise invalid, such as a descriptor of the wrong size |
| `CINT_REFUSAL_BOOL` | 11 | holds a `Bool` byte other than 0 or 1 |
| `CINT_REFUSAL_RESULT` | 12 | is a NULL result pointer |
| `CINT_REFUSAL_LEASED` | 13 | is a leased output; the call returns `CINT_BUSY` |
| `CINT_REFUSAL_UNSUPPORTED` | 14 | is device memory, or a synchronization object, the context cannot take |
| `CINT_REFUSAL_LEASE` | 15 | asks for a lease of memory the runtime does not own, or ends an unknown lease |

**Faults**

11. Once its arguments are formed, a wrapper makes the entry checks over every array parameter in turn, in this order, and faults at the first that fails, with the parameter indices counted from 0 in declaration order and no fuel charged:
    1. `E_STALE_HANDLE`, `bind.stale`: the view's generation is not the registration's. Operands: the parameter index (`I64`) and the view's generation (`U64`); limit: the registration's generation.
    2. `E_ALIAS`, `bind.permission`: a `read` view for an `out` or `inout` parameter. Operand: the parameter index.
    3. `E_UNSUPPORTED`, `bind.type`: an element type other than the parameter's; or `E_SHAPE`, `bind.type`: a rank other than the parameter's, with the view's rank as a second operand and the declared rank as the limit.
    4. `E_SHAPE`, `bind.shape`: an extent that disagrees with the extent its shape variable already took. Operands: the parameter index, the dimension and the extent given; limit: the extent expected.
    5. `E_UNSUPPORTED`, `bind.placement`: a view in the wrong placement for its parameter, on a context with a device.
    6. `E_ALIAS`, `bind.injective`: a written view whose elements are not distinct.
    7. `E_ALIAS`, `bind.alias`: a written view that overlaps another array argument (6.5).
    8. `E_UNSUPPORTED`, `bind.limit`: a borrowed output registered with publish mode `none` (6.4).

    Each such fault is positioned at the export's name, and a kernel's record carries the kernel address with phase `entry`. The checks are those of SPEC-04P.3 6.13 for a dispatch issued from cint code.

**Examples**

12. 10.1 runs one call of each kind: refused calls with reasons 1, 2, 7 and 9, a fuel budget of -2 and entry faults `bind.stale`, `bind.permission`, `bind.shape` and `bind.limit`.

<a id="the-runtime-header"></a>

### 7.2 The runtime header

A host program includes `cint_rt.h`, which includes `cint_rt_host.h`, and links the library. Everything it calls is a function of the runtime or a wrapper, and everything it passes is a fixed-layout C structure.

**Synopsis**

```text
uint32_t    cint_abi_version(void);
cint_status cint_ctx_create(const cint_ctx_config *config, cint_ctx **out);
void        cint_ctx_destroy(cint_ctx *ctx);
const uint8_t *cint_ctx_args(const cint_ctx *ctx, size_t *length);
cint_status cint_buffer_register(cint_ctx *ctx, const cint_buffer_desc *desc,
                                 cint_buffer_id *out_id, uint64_t *out_generation);
cint_status cint_buffer_create(cint_ctx *ctx, cint_type type, int64_t extent, uint32_t device,
                               cint_buffer_id *out_id, uint64_t *out_generation);
cint_status cint_buffer_release(cint_ctx *ctx, cint_buffer_id id);

typedef struct cint_ctx_config {        /* 152 bytes */
    uint32_t size;                      /* sizeof(cint_ctx_config) */
    uint32_t max_buffers;               /* live registrations; 0 selects 256 */
    const cint_program *program;        /* required */
    cint_allocator allocator;           /* alloc NULL selects malloc and free */
    int64_t depth_limit;                /* call depth of every entry; 0 selects 256 */
    int64_t frame_arena_bytes;          /* frame arena; 0 selects 16777216 */
    cint_output_fn output;              /* receives each print statement; NULL discards */
    void *output_user;
    const cint_module_info *module;     /* the root module's descriptor */
    uint32_t device_count, reserved;    /* 0, or 1 for a gpu-cuda library */
    const void *const *devices;
    uint32_t host_count, reserved2;     /* the host table's functions (7.6) */
    const cint_host_fn *host;           /* NULL when host_count is 0 */
    const cint_host_desc *imports;      /* the host import descriptor (7.6), or NULL */
    const uint8_t *grants;              /* the grant lines (9.4), or NULL */
    size_t grants_length;
    const uint8_t *args;                /* the program arguments of main, or NULL */
    size_t args_length;
} cint_ctx_config;

typedef struct cint_buffer_desc {       /* 112 bytes */
    uint32_t size;                      /* sizeof(cint_buffer_desc) */
    uint32_t writable;                  /* the ceiling: 0 read, 1 write */
    cint_type type;                     /* element type */
    cint_mem mem;                       /* kind CINT_MEM_HOST and the lowest reachable byte */
    int64_t extent;                     /* elements */
    uint32_t device;                    /* 0 for host memory */
    uint32_t publish;                   /* CINT_PUBLISH_NONE, or CINT_PUBLISH_COPY if writable */
    cint_sync sync;                     /* kind 0 for host memory */
} cint_buffer_desc;

typedef struct cint_view {              /* 144 bytes */
    cint_buffer_id buffer;              /* uint64_t, never 0 */
    uint64_t generation;
    cint_type type;                     /* code: the element's type tag (7.5) */
    uint8_t rank;                       /* 1 to 4 */
    uint8_t perm;                       /* CINT_VIEW_READ 0, CINT_VIEW_WRITE 1 */
    uint8_t reserved[6];
    int64_t origin;                     /* elements from the buffer's base */
    int64_t shape[4], stride[4], lower[4];
} cint_view;
```

**Constraints**

1. A host program shall zero every reserved field and every unused field, and shall set each `size` field to the size of its structure.
2. A host program shall make at most one call on a context at a time, and shall not call into a context from the output callback of an entry of that context. A call that breaks this returns `CINT_BUSY` (paragraph 9).

**Semantics**

3. <a id="A-1"></a>The interface is C17 with the fixed-width integer types of `<stdint.h>`. Its structures hold no field of an `enum` type and no compiler-specific wide integer type; every field is a fixed-width integer, a data pointer, a function pointer or a `size_t` and `static_assert` checks every structure's size and the offsets of its fields. Wide integers cross as structures of `uint64_t` limbs, least significant first. The header refuses to compile for a big-endian target or for pointers other than 64 bits [A-1].
4. <a id="A-2"></a>All state is reached through the opaque context `cint_ctx`. A library holds no mutable static data, so one library serves any number of contexts in one process [A-2].
5. A context is the scope of state, isolation, checkpointing and replay, and SPEC-09P.2 7.10 states what it holds. `cint_ctx_create` checks the configuration, allocates the context and creates its module variables, arenas and pools with their declared capacities; `cint_ctx_destroy` frees everything the context and the runtime own [SPEC-09P.2 RT-4]. A context holds the program arguments of its configuration, which a `main` that takes them receives (SPEC-04P.3 LS-221): `args_length` bytes at `args`, each argument's bytes followed by one NUL byte, in order, or none when `args_length` is 0. `cint_ctx_args` returns the bytes a context holds and writes their length, NULL and 0 when it holds none.
6. <a id="A-5"></a>`cint_abi_version` returns the interface version as `(major << 16) | minor`, which is 3.4 in cint 0.2.2. Each module descriptor records the version it was built for, and `cint_ctx_create` refuses a module of another major version. Version 3.3 appends to `cint_ctx_config` the host table, the host import descriptor and the grants, 136 bytes where version 3.2 has 96, and adds the host functions of 7.6. Version 3.4 appends the program arguments, 152 bytes, and adds `cint_ctx_args`. A runtime of version 3.4 refuses a configuration of an earlier size [A-5].
7. <a id="A-10"></a>`cint_ctx_create` returns `CINT_REFUSED` for an invalid configuration: a NULL argument, a wrong `size`, no program or an invalid one, an allocator with `alloc` but no `release`, a negative depth limit or frame-arena size, a module of another interface version or of another program, a host table or a host import descriptor that 7.6 refuses, a host import that does not resolve (7.6), grants that 9.4 refuses, or program arguments whose last byte is not NUL or whose `args` is NULL with a length other than 0. It returns `CINT_RESOURCE` when the allocator cannot supply the context or the storage of the program's arenas and pools, whose declared capacities and element types fix their size; the host chooses no arena capacity [A-10].
8. <a id="A-8"></a>`cint_buffer_register` checks a descriptor in this order, and refuses the first failure with the reason in brackets: `size`, `writable`, the reserved fields and `publish` (`CALL`); host memory with no synchronization object, or device memory the context can take (`UNSUPPORTED`); a `Bool`, `I8` to `U64` or record element type (`TYPE`); an extent of at least 0 (`EXTENT`); a byte size that fits an `I64` and the address space (`SIZE`); an address that is not NULL unless the size is 0 and that is aligned (`ALIGN`). On success it writes the buffer identifier and the generation. A borrowed registration lasts until `cint_buffer_release`, or until a restore, and the host keeps the memory valid until then [A-8].
9. <a id="A-11"></a>A host program shall not use one context from two threads at once. A call that finds another call active on the context returns `CINT_BUSY` and disturbs nothing; the busy flag of SPEC-09P.2 7.10 detects a second caller, and does not make a context usable from two threads. Different contexts may be used from different threads [A-11] [SPEC-09P.2 RT-15].
10. <a id="H-10"></a>An entry is synchronous, and at most one entry is active on a context at a time. Contexts share no mutable state [H-10].
11. <a id="A-13a"></a>The alignment an element type requires is its size, for types of at most 8 bytes, and 8 for wider integers and for records [A-13a].
12. <a id="A-3"></a>A library exports exactly these symbols: the host functions of the runtime (those of this subclause and of 6.3, 7.1 and 7.6, with `cint_buffer_register_bytes`, `cint_buffer_register_elements`, `cint_fault_encode`, `cint_ctx_fault_descriptors` and `cint_site_resolve`, which gives the module path, line and column of a site, such as a fault record's position); one wrapper `cx_...` per export of the root module (7.3); one module descriptor `cm_...` per module; the library descriptor `cint_library_desc` (7.4); the host import descriptor `cint_imports_desc` (7.6); and the observer symbols `cint_program_abi` and `cint_observer_desc` of SPEC-09P.2 9.6. Each library carries its own copy of the runtime, so a host program creates and uses each context through one library only. The code generated from cint source calls the runtime for every effect and allocation, and reaches no operating-system, input or output, time, environment or thread function of its own [A-3].
13. <a id="A-16"></a>A context has at most one device. A library built for `gpu-cuda` takes `device_count` 1 and in `devices[0]` a record of the device it runs on; every other library takes `device_count` 0 [A-16].
14. <a id="A-16a"></a>A `gpu-cuda` context reaches its device through the NVIDIA driver library, which the runtime loads by name when the context first uses the device, as at its first dispatch: `libcuda.so.1` on Linux, and `nvcuda.dll` on Windows, from the system directory only. No CUDA header or toolkit takes part in a build. The runtime uses the device's primary context with a stream of its own, and makes the primary context current only for each driver call, restoring the caller's, so a host that uses CUDA itself keeps its own context. When the library cannot be loaded or lacks an entry point, when the device cannot map host memory, or when a driver call fails, the call returns `CINT_RESOURCE`, which is not a cint fault; inside an entry, the boundary record is `host.resource`. In Python, `cint.device("cuda:0")` opens the device through the same library and raises `OSError` when the library cannot be loaded [A-16a].

**Notes**

15. `cint_library_desc` and `cint_imports_desc` are defined by every library but not declared by `cint_rt.h`, since their types are the same for every program; a host program declares them as `extern const cint_library cint_library_desc;` and `extern const cint_host_desc cint_imports_desc;`.
16. Beside the library, `cint build --lib` writes a header named for it, such as `gain.h` for `libgain.so` (SPEC-06P.2 5.5). The header includes `cint_rt.h` and declares, for C and for C++ inside `extern "C"`, each module descriptor, each wrapper with its export's qualified name in a comment, `cint_library_desc`, `cint_imports_desc` and the observer symbols. A host program that puts the library's directory on its include path includes it in place of the declarations that 7.3 and 10.1 write out, as 7.6 does.

<a id="the-calling-sequence"></a>

### 7.3 The calling sequence

A host program creates a context, registers the memory it lends, calls wrappers, reads results and faults, and releases what it registered. This subclause gives the shape of a wrapper, the C form of each type and a complete program.

**Synopsis**

```text
cint_status cx_<module>_<name>(cint_ctx *ctx, int64_t fuel, <parameters in declaration order>, <result pointer>);

cint_status cx_7_advance_7_advance(cint_ctx *ctx, int64_t fuel, cint_view pos, cint_view vel,
                                   int64_t dt, cint_view next);
cint_status cx_7_advance_5_total(cint_ctx *ctx, int64_t fuel, cint_view x, int64_t *result);
```

**Semantics**

1. <a id="A-12"></a>Each export of the root module has one wrapper. Its name is `cx` followed by the encoded path of the module and the encoded name of the export, each a length and the text with some characters escaped, as SPEC-09P.2 specifies: `advance` in `advance.ci` is `cx_7_advance_7_advance`, and `parse_u` in `tour.ci` is `cx_4_tour_10_parse_x5Fu`. It takes the context, the fuel budget (7.1), the parameters in declaration order and last a pointer to the result when the export returns a value. Size parameters are not arguments: their values are taken from the views and checked for agreement (7.1, check 4). The depth limit and the frame-arena size come from the configuration [A-12].
2. <a id="A-13"></a>Each type crosses in one C form, and a result is written through the result pointer only when the status is `CINT_OK` [A-13]:

| cint type | C parameter | C result |
|---|---|---|
| `I8`, `I16`, `I32`, `I64` | `int8_t` to `int64_t` | a pointer to the same type |
| `U8`, `U16`, `U32`, `U64` | `uint8_t` to `uint64_t` | a pointer to the same type |
| `I128` to `I1024` | a pointer to a structure of `uint64_t` limbs | a pointer to the same structure |
| `Q<i>.<f>` | the storage integer, raw: 1.0 in `Q32.32` is 4294967296 | a pointer to the storage integer |
| `Bool` | `uint8_t`, 0 or 1; any other byte is refused (`CINT_REFUSAL_BOOL`) | a pointer to `uint8_t` |
| array, any mode | `cint_view` | none |
| struct, `in` or `inout` | `cint_view` of rank 1 and extent 1 whose type code is `0x71` and whose `record_id` names the struct | a pointer to the C struct |
| error union `E!T` | none | a pointer to a struct of the tag, in the set's underlying type and 0 for success, then the value of `T` |

   A `Bool` element of an array, or a `Bool` field of a struct element, is checked as a byte before the body reads it, and any byte other than 0 or 1 is a refused call.
3. <a id="A-18"></a>An export whose function returns an error value completes. Its status is `CINT_OK`, the tag in its result is the error's tag in its set, counted from 1, and the value field is 0. The context is not faulted, and the outputs of its dispatches are published as for any completed entry. `cint_ctx_error` gives the error set, as a `cint_error_set` with its qualified name and value names, and the tag, or NULL and 0 when the last entry returned no error. The error is part of the result, never of the fault record [A-18].
4. A print statement in an export reaches the configuration's `output` callback once per statement, with the statement's bytes whole. A callback that returns a status other than `CINT_OK` faults the entry. With `output` NULL, print statements are formed and their bytes discarded.

**Examples**

5. This is a complete host program for `advance.ci` (Introduction). It registers three arrays of host memory, runs `advance`, totals the output, makes `advance` fault and recovers.

```c
/* host.c: calls advance.ci through the library that `cint build --lib` wrote. */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "cint_rt.h"

/* What the library exports for advance.ci. */
extern const cint_module_info cm_7_advance;
cint_status cx_7_advance_7_advance(cint_ctx *ctx, int64_t fuel, cint_view pos, cint_view vel,
                                   int64_t dt, cint_view next);
cint_status cx_7_advance_5_total(cint_ctx *ctx, int64_t fuel, cint_view x, int64_t *result);

static const char *const fault_names[] = {"", "E_OVERFLOW", "E_DIV_ZERO", "E_BOUNDS", "E_SHAPE",
    "E_SHIFT", "E_NARROW", "E_ALIAS", "E_STALE_HANDLE", "E_FUEL", "E_UNSUPPORTED", "E_DOMAIN",
    "E_DEPTH", "E_ASSERT"};

/* Registers n I64 elements of host memory and writes a view of all of them. */
static cint_status share(cint_ctx *ctx, int64_t *p, int64_t n, int writable, cint_view *v)
{
    cint_buffer_desc d;
    cint_status s;
    memset(&d, 0, sizeof d);
    d.size = sizeof d;
    d.writable = writable ? 1u : 0u;
    d.type.code = CINT_TAG_I64;
    d.mem.kind = CINT_MEM_HOST;
    d.mem.u.host.ptr = p;
    d.extent = n;
    d.publish = writable ? CINT_PUBLISH_COPY : CINT_PUBLISH_NONE;
    memset(v, 0, sizeof *v);
    s = cint_buffer_register(ctx, &d, &v->buffer, &v->generation);
    v->type.code = CINT_TAG_I64;
    v->rank = 1;
    v->perm = writable ? CINT_VIEW_WRITE : CINT_VIEW_READ;
    v->shape[0] = n;
    v->stride[0] = 1;
    return s;
}

/* An I64 operand of a fault record: its tag byte, then 8 bytes, little-endian. */
static int64_t operand_i64(const cint_tvalue *t)
{
    uint64_t u = 0;
    for (int k = 8; k >= 1; k--) u = (u << 8) | t->bytes[k];
    return (int64_t)u;
}

static void report(cint_ctx *ctx)
{
    static cint_fault_record r;
    const cint_module *m;
    const cint_site_info *at;
    if (cint_ctx_fault(ctx, &r) != CINT_OK || r.code == 0) return;
    m = r.program->modules[r.position.module];
    at = &m->sites[r.position.index];
    printf("fault %s %.*s at %.*s:%" PRIu32 ":%" PRIu32 "\n", fault_names[r.code],
           (int)r.operation_len, r.operation, (int)m->path_len, m->path, at->line, at->column);
    printf("  operands:");
    for (unsigned k = 0; k < r.operand_count; k++) printf(" %" PRId64, operand_i64(&r.operands[k]));
    printf("\n");
    if (r.has_address)
        printf("  kernel %.*s, dispatch %" PRId64 ", work item %" PRId64 "\n",
               (int)r.address.name_len, r.address.name, r.address.dispatch, r.address.work_item);
}

static void show(const char *name, const int64_t *a, int n)
{
    printf("%s =", name);
    for (int k = 0; k < n; k++) printf(" %" PRId64, a[k]);
    printf("\n");
}

int main(void)
{
    int64_t pos[4] = {0, 1, 2, 3}, vel[4] = {16, 16, 16, 16}, next[4] = {0, 0, 0, 0}, total = 0;
    cint_view vp, vv, vn;
    cint_ctx_config config;
    cint_ctx *ctx = NULL;
    cint_status s;

    memset(&config, 0, sizeof config);
    config.size = sizeof config;
    config.program = cm_7_advance.program;
    config.module = &cm_7_advance;
    if (cint_ctx_create(&config, &ctx) != CINT_OK) return 1;
    if (share(ctx, pos, 4, 0, &vp) != CINT_OK || share(ctx, vel, 4, 0, &vv) != CINT_OK ||
        share(ctx, next, 4, 1, &vn) != CINT_OK) return 1;

    s = cx_7_advance_7_advance(ctx, CINT_FUEL_UNBOUNDED, vp, vv, 1, vn);
    printf("advance: status %d\n", s);
    show("next", next, 4);
    s = cx_7_advance_5_total(ctx, CINT_FUEL_UNBOUNDED, vn, &total);
    printf("total: status %d, result %" PRId64 "\n", s, total);

    vel[2] = INT64_C(4611686018427387904);   /* 2^62: vel[2] * 2 overflows */
    s = cx_7_advance_7_advance(ctx, CINT_FUEL_UNBOUNDED, vp, vv, 2, vn);
    printf("advance: status %d\n", s);
    report(ctx);
    show("next", next, 4);

    s = cx_7_advance_5_total(ctx, CINT_FUEL_UNBOUNDED, vn, &total);
    printf("total: status %d\n", s);
    printf("clear_fault: status %d\n", cint_ctx_clear_fault(ctx));
    s = cx_7_advance_5_total(ctx, 100, vn, &total);
    printf("total: status %d, result %" PRId64 "\n", s, total);

    cint_buffer_release(ctx, vp.buffer);
    cint_buffer_release(ctx, vv.buffer);
    cint_buffer_release(ctx, vn.buffer);
    cint_ctx_destroy(ctx);
    return 0;
}
```

Built and run, with `CINT_RT` naming the directory that holds `cint_rt.h` (`rt/` in the source tree):

```console
$ cint build --lib --out out advance.ci
built: out/libadvance.so
$ cc -std=c17 -Wall -Wextra -Wpedantic -I "$CINT_RT" host.c -L out -ladvance -Wl,-rpath,'$ORIGIN/out' -o host
$ ./host
advance: status 0
next = 16 17 18 19
total: status 0, result 70
advance: status 1
fault E_OVERFLOW mul.checked.i64 at advance.ci:3:31
  operands: 4611686018427387904 2
  kernel advance.advance, dispatch 0, work item 2
next = 16 17 18 19
total: status 4
clear_fault: status 0
total: status 0, result 70
```

6. The second `advance` faults at work-item 2, where `vel[i] * dt` is 2^63. The output `next` keeps the contents the first call published, because the faulting dispatch publishes nothing. `total` then answers `CINT_FAULTED` (4) without running, and after `cint_ctx_clear_fault` it runs again with a budget of 100 fuel.

**Notes**

7. The wrapper names above are written out for clarity. A host that does not want to depend on the encoding finds each wrapper by its export name in the reflection table (7.4), as `tour.c` does in 10.1.

<a id="the-reflection-table"></a>

### 7.4 The reflection table

A library describes itself. From one symbol whose name is the same for every program, a host program finds every module, every export, its parameters, its type signature and its wrapper.

**Synopsis**

```text
typedef struct cint_name { const char *bytes; uint32_t len; uint32_t reserved; } cint_name;  /* UTF-8, no NUL */

typedef struct cint_export {            /* 72 bytes */
    cint_name name;
    uint32_t kind;                      /* CINT_EXPORT_FUNCTION 1, CINT_EXPORT_KERNEL 2 */
    uint32_t effect;                    /* CINT_PURE 1, CINT_OBSERVE 2, CINT_UNRESTRICTED 3 */
    uint32_t size_count, param_count;
    const cint_name *size_names;        /* declaration order; NULL when size_count is 0 */
    const cint_name *param_names;       /* declaration order; NULL when param_count is 0 */
    const uint8_t *type_signature;      /* 7.5 */
    uint64_t type_signature_len;
    cint_entry_fn call;                 /* the wrapper, cast to void (*)(void) */
} cint_export;

typedef struct cint_module_table {      /* 32 bytes */
    const cint_module_info *info;       /* the module's descriptor */
    uint32_t module;                    /* its index in info->program->modules */
    uint32_t export_count;
    const cint_export *exports;         /* declaration order */
    const cint_name *record_names;      /* the qualified name of each record row of info */
} cint_module_table;

typedef struct cint_library {           /* 16 bytes */
    uint32_t abi;                       /* the interface version of 7.2 */
    uint32_t module_count;
    const cint_module_table *const *modules;
} cint_library;

extern const cint_library cint_library_desc;
```

**Semantics**

1. <a id="A-14"></a>Each module of a program has a descriptor, `cm` followed by its encoded path, which holds the interface version, the program with its site tables and revision identity, and the module's table of record layouts. The descriptor of the root module is the one a host passes to `cint_ctx_create` [A-14].
2. <a id="A-19"></a>The library descriptor `cint_library_desc` leads to one table per module, in the order of the program's module table. A module's table holds its descriptor and one row per export that has a wrapper, in declaration order; only the root module's table has rows. A row gives the export's name, its kind, its effect class, the names of its size parameters and of its parameters, its type signature and its wrapper, which a host casts to the prototype the type signature gives. A struct parameter's view carries as `record_id` the index of the row of the root module's record layout table whose qualified name is the struct's [A-19].
3. <a id="H-2"></a>The effect class of an export is the one SPEC-04P.3 6.9.8 computes for its function: `CINT_PURE` (1) when it neither prints nor calls an extern other than a `pure` one; `CINT_OBSERVE` (2) when it, or a function it calls, prints or calls an `observe` extern, since a print statement is an output effect (9.1); and `CINT_UNRESTRICTED` (3) when it, or a function it calls, calls an `unrestricted` extern. A kernel is always pure [H-2].

**Examples**

4. This program lists the exports of `tour.ci` (10.1) and the bytes of each type signature after its 33-byte domain string:

```c
/* reflect.c: lists the exports of a library from its reflection table. */
#include <inttypes.h>
#include <stdio.h>
#include "cint_rt.h"

extern const cint_library cint_library_desc;

static void names(const cint_name *n, uint32_t count)
{
    for (uint32_t k = 0; k < count; k++) printf("%s%.*s", k ? " " : "", (int)n[k].len, n[k].bytes);
}

int main(void)
{
    printf("abi %" PRIu32 ".%" PRIu32 ", %" PRIu32 " module\n", cint_library_desc.abi >> 16,
           cint_library_desc.abi & 0xFFFFu, cint_library_desc.module_count);
    for (uint32_t m = 0; m < cint_library_desc.module_count; m++) {
        const cint_module_table *t = cint_library_desc.modules[m];
        const cint_module *mod = t->info->program->modules[t->module];
        printf("module %.*s: %" PRIu32 " exports\n", (int)mod->path_len, mod->path, t->export_count);
        for (uint32_t k = 0; k < t->export_count; k++) {
            const cint_export *e = &t->exports[k];
            printf("  %.*s: %s, effect %" PRIu32 ", sizes [", (int)e->name.len, e->name.bytes,
                   e->kind == CINT_EXPORT_KERNEL ? "kernel" : "function", e->effect);
            names(e->size_names, e->size_count);
            printf("], parameters [");
            names(e->param_names, e->param_count);
            printf("], signature %" PRIu64 " bytes\n", e->type_signature_len);
            /* The bytes after the 33 of the domain string, 16 to a line. */
            for (uint64_t j = 33; j < e->type_signature_len; j++)
                printf("%s%02x%s", (j - 33) % 16 == 0 ? "    " : " ", e->type_signature[j],
                       (j - 33) % 16 == 15 || j + 1 == e->type_signature_len ? "\n" : "");
        }
    }
    return 0;
}
```

```console
$ cint build --lib --out out tour.ci
built: out/libtour.so
$ cc -std=c17 -Wall -Wextra -Wpedantic -I "$CINT_RT" reflect.c -L out -ltour -Wl,-rpath,'$ORIGIN/out' -o reflect
$ ./reflect
abi 3.4, 1 module
module tour.ci: 3 exports
  parse_u: function, effect 1, sizes [], parameters [s], signature 117 bytes
    01 00 00 00 73 0f 00 00 00 74 6f 75 72 2e 50 61
    72 73 65 45 72 72 6f 72 22 00 03 00 00 00 05 00
    00 00 65 6d 70 74 79 09 00 00 00 62 61 64 5f 64
    69 67 69 74 09 00 00 00 74 6f 6f 5f 6c 61 72 67
    65 00 00 00 00 01 00 00 00 00 52 21 01 00 74 00
    00 00 00 14
  scale: kernel, effect 1, sizes [n], parameters [x k y], signature 82 bytes
    00 00 00 00 01 00 00 00 03 00 00 00 00 52 14 01
    01 00 00 00 00 00 00 00 00 00 00 00 00 00 14 02
    52 14 01 01 00 00 00 00 00 00 00 00 00 00 00 00
    00
  greet: function, effect 2, sizes [], parameters [times], signature 48 bytes
    00 00 00 00 00 00 00 00 01 00 00 00 00 14 14
```

5. `greet` prints, so its effect class is 2, `CINT_OBSERVE`. 7.5 reads the bytes.

<a id="type-signatures"></a>

### 7.5 Type signatures

A type signature is the exact description of an export's interface, in bytes a host can decode without the source: the mode and type of each parameter and the type of the result, with every struct and error set it names written out in full.

**Syntax**

```text
type_signature = domain       U32 29, then the ASCII bytes "cint-core-1/type-signature/v1"
                 def_count    U32
                 definitions  def_count definitions
                 size_count   U32            the size parameters or shape symbols
                 param_count  U32
                 params       for each parameter: mode U8 (00 in, 01 inout, 02 out), then a type
                 result       a type, or 00 for none
```

**Semantics**

1. <a id="A-20"></a>All integers of a type signature are little-endian. A type is a tag byte followed by its parameters [A-20]:

| Tag | Form | Following bytes |
|---|---|---|
| `01` | `Bool` | none |
| `11` to `18` | `I8`, `I16`, `I32`, `I64`, `I128`, `I256`, `I512`, `I1024` | none |
| `21` to `24` | `U8`, `U16`, `U32`, `U64` | none |
| `31` | `Q<i>.<f>` | the storage tag, `11` to `14`, then `f` as `U16` |
| `52` | array | the element type; the rank as `U8`, 1 to 4; then one shape per dimension |
| `71` | struct | the index of its definition, `U32` |
| `73` | error-set value | the index of its definition, `U32` |
| `74` | error union `E!T` | the index of the set's definition, `U32`; then `00` for `void` or the type `T` |

   A shape is a form byte and its values. Form `00` is any extent (`_`), form `01` an extent `B`, form `02` the range `L ..= B` with lower bound `L` as an `I64` then `B`. An extent or bound `B` is a size-symbol index as `U32` (`FF FF FF FF` for none) followed by an offset as `I64`, and its value is the symbol's value plus the offset: `n` is (0, 0) when `n` is the first size symbol, and `8` is (none, 8). An array occurs only as a parameter, or as a struct field with literal extents. An error union occurs only as a result.
2. <a id="A-21"></a>A struct or an error set is defined once in the signature and named by its index. A struct definition is `71`, the qualified name, a layout byte, the field count as `U32` and for each field its name, a bit width as `U8` (0 when not a bit field) and its type. An error-set definition is `73`, the qualified name, the underlying unsigned tag (`21` to `24`), the form `00`, the value count as `U32` and each value's name in tag order; a set that combines others has the form `01` and lists its member sets by definition index. A name is a `U32` length and UTF-8 bytes; a qualified name is the module's dotted name, a full stop and the declared name, such as `tour.ParseError`. Definitions are listed in the order in which a depth-first walk of the parameters, then the result, completes them, so a definition refers only to earlier ones [A-21].
3. <a id="A-22"></a>A type signature holds the modes, the types and the full definitions of named types, including the names of struct fields and error values. It holds no parameter name, size name, kind or effect class, which are fields of the table row, and no kernel `over` clause. Renaming a parameter therefore leaves the signature unchanged, and renaming a struct field changes it. A host passes every argument, since a default argument is filled in only by a cint caller [A-22].
4. <a id="A-24"></a>A type signature is untrusted input to a host. A decoder rejects, before it allocates for what follows: another domain string; an unknown tag, mode, shape form, layout or set form; a tag where the table does not allow it, an array of arrays or a field array with an open extent; a fixed-point storage tag outside `11` to `14` or an `f` above the storage width less 1; a rank of 0 or above 4, or a size-symbol index at or above `size_count`; a definition index that is not earlier than the reference or that names a definition of another kind; a definition that nothing refers to, or two of one kind with one name; an error set with more values than its underlying type can number; two fields or values of one definition with one name; a bit width wider than its field; a name that is not an identifier; and any count or length larger than the bytes remaining, truncated input, or trailing bytes. A host that meets a tag it does not know rejects that signature and treats the export as not callable [A-24].
5. <a id="A-25"></a>An export whose parameters or result take a form that has no C carrier, which are an arena, a pool or a handle, a view as a result or an array extent outside the forms above, gets no wrapper and no row and stays callable from cint code [A-25].
6. <a id="A-26"></a>The domain string carries version 1. A new form enters as a new tag of version 1, so the bytes of every existing signature stay the same; changing an existing form needs version 2. The reflection table and the result carriers of 7.3 belong to interface version 3 and the runtime contract `cint-rt-3`, which is part of every program's revision identity [A-26].

**Examples**

7. The signature of `scale` (10.1), `kernel scale[n](in I64[n] x, in I64 k, out I64[n] y)`, is shown below after its domain string, as 7.4 printed it:

```text
00 00 00 00                                         no definitions
01 00 00 00  03 00 00 00                            one size symbol, three parameters
00 52 14 01  01 00 00 00 00  00 00 00 00 00 00 00 00   in I64[n] x: array of I64, rank 1, extent (n, 0)
00 14                                               in I64 k
02 52 14 01  01 00 00 00 00  00 00 00 00 00 00 00 00   out I64[n] y
00                                                  no result
```

8. The signature of `parse_u`, `ParseError!I64 parse_u(in U8[_] s)`, defines the error set first:

```text
01 00 00 00                                         one definition
73 0f 00 00 00 "tour.ParseError" 22 00              0: error set, underlying U16, declared
   03 00 00 00  05 00 00 00 "empty"  09 00 00 00 "bad_digit"  09 00 00 00 "too_large"
00 00 00 00  01 00 00 00                            no size symbols, one parameter
00 52 21 01 00                                      in U8[_] s: any extent
74 00 00 00 00 14                                   result: error union of definition 0 and I64
```

<a id="host-functions"></a>

### 7.6 Host functions

A module calls a function of its host program through an extern declaration (SPEC-04P.3 6.9.8). The host program supplies the function in a table when it creates the context. The runtime checks the table against the declarations before any entry, calls the function with typed arguments, and faults the entry when the function fails.

**Synopsis**

```text
typedef struct cint_host_fn {           /* 64 bytes */
    const char *name;                   /* the extern's name, NUL-terminated */
    uint32_t effect;                    /* CINT_PURE 1, CINT_OBSERVE 2, CINT_UNRESTRICTED 3 */
    uint32_t reserved;
    uint8_t signature[32];              /* SHA-256 of the extern's type signature */
    cint_status (*call)(void *user, const cint_value *args, uint32_t nargs, cint_value *result);
    void *user;
} cint_host_fn;

typedef struct cint_value {             /* 200 bytes */
    cint_type type;                     /* a scalar's type, or a view's element type */
    uint32_t kind;                      /* CINT_VAL_SCALAR 1, CINT_VAL_VIEW 3; 0 for a void result */
    uint32_t is_error;                  /* an error union's result: 1 for the error alternative */
    int64_t error;                      /* the error's tag in its set, from 1 */
    union {
        int64_t i64;                    /* signed scalars, sign-extended; Bool as 0 or 1 */
        uint64_t u64;                   /* unsigned scalars, zero-extended */
        uint8_t *wide;                  /* not passed in cint 0.2.2 */
        cint_arg_view view;
    } as;
} cint_value;

typedef struct cint_arg_view {          /* 168 bytes */
    cint_type type;                     /* the element type */
    uint8_t rank;                       /* 1 */
    uint8_t perm;                       /* CINT_VIEW_READ 0 for in, CINT_VIEW_WRITE 1 for out */
    uint8_t reserved[6];
    cint_mem mem;                       /* host memory: the first element */
    int64_t shape[4], byte_stride[4], lower[4];
    int64_t reachable_bytes;            /* from the lowest byte to past the highest; 0 if empty */
} cint_arg_view;

typedef struct cint_host_import {       /* 40 bytes */
    cint_name name;                     /* the extern's name */
    uint32_t effect;
    uint32_t param_count;
    const uint8_t *type_signature;      /* 7.5 */
    uint64_t type_signature_len;
} cint_host_import;

typedef struct cint_host_table {        /* 16 bytes */
    uint32_t count;
    uint32_t reserved;
    const cint_host_import *imports;    /* declaration order; NULL when count is 0 */
} cint_host_table;

typedef struct cint_host_desc {         /* 16 bytes */
    uint32_t module_count;              /* the program's */
    uint32_t reserved;
    const cint_host_table *const *tables;  /* one per module, in program order; NULL for none */
} cint_host_desc;

extern const cint_host_desc cint_imports_desc;
void cint_signature_hash(const uint8_t *signature, size_t length, uint8_t digest[32]);
```

**Constraints**

1. A host function shall return `CINT_OK` only when it has produced one alternative of the declared result: for a scalar result, a value of the declared type in `as.i64` or `as.u64`; for an error union, either `is_error` 0 and the value, or `is_error` 1 and in `error` a tag from 1 to the error set's value count; for `void`, nothing. It shall write an `out` view's elements only through the view's `mem` and `byte_stride`, and shall not keep the address of a view after it returns [H-5].
2. <a id="H-9"></a>A host function shall not call into the context that called it. It receives no context, and a call into that context during the host function returns `CINT_BUSY` and disturbs nothing (7.2, paragraph 9) [H-9].

**Semantics**

3. Every library defines the host import descriptor `cint_imports_desc`. For each module of the program, in the order of the program's module table, it holds a table of the externs that the module declares, in declaration order, each with its name, its effect class, its parameter count and its type signature (7.5); a module that declares none has a NULL table. A row whose name holds `/`, such as `fs/1.read`, is an extern of a standard service, which the runtime supplies (9.4); every other row is a *host import*. A library of a program that declares no extern defines a descriptor too.
4. <a id="A-23"></a>The type signature hash of an extern is SHA-256 over its type signature, domain string included. `cint_signature_hash` computes it, so a host program can fill `cint_host_fn.signature` from the descriptor's row [A-23].
5. A host program passes the descriptor as `config.imports` and its host table as `config.host`, with `host_count` functions. `cint_ctx_create` refuses with `CINT_REFUSED` a host table with a function whose name is NULL or empty or holds `/`, whose effect class is not 1 to 3, whose reserved field is not 0 or whose `call` is NULL, and a descriptor whose module count is not the program's or whose rows are malformed. It then resolves each host import: the first function of the host table with the import's name shall have the import's effect class and, in `signature`, the hash of its type signature. An import with no function of its name, or whose function differs in either, refuses creation with `CINT_REFUSED`. Functions that no import names are ignored. A row of a standard service resolves against the functions of the services that the runtime supplies (9.4) by the same rules, so a row of another service or version refuses creation with `CINT_REFUSED`. With `imports` NULL, creation resolves nothing, and every call of an extern faults (paragraph 9) [A-10].
6. <a id="A-15"></a>A call of a host import charges one unit of fuel at its site, as a call of a function does, and then calls the host function with the arguments in declaration order: a scalar as a `cint_value` of kind `CINT_VAL_SCALAR`, and a view as one of kind `CINT_VAL_VIEW` whose `cint_arg_view` has rank 1 and addresses the elements directly through `mem` and `byte_stride`, valid only for the duration of the call. For an `out` view the runtime passes staging memory, dense, and copies its contents into the argument's elements only when the host function returns `CINT_OK` with the value alternative, so a refused call or an error result leaves them as they were. Scalars are of at most 64 bits, and views have elements of such a scalar type [A-15].
7. <a id="H-5"></a>An extern receives and returns only cint values and views. A host function changes the memory of the program only through an `out` view of its declaration, and what it writes there is part of its result, which a recording holds (9.3) [H-5].
8. <a id="H-6"></a>A `pure` class is a claim of the host program, not a proof: the runtime does not record a `pure` call and calls it again on replay. A host function declared `pure` whose result does not depend on its arguments alone makes replay differ, and the differences are the host program's [H-6].
9. A call that cannot be made or that fails is a fault of the entry, with code `E_UNSUPPORTED` and positioned at the call:
   - `host.call`: the context resolved no function for the extern, as when `imports` was NULL, or an argument is not a scalar of the declared type or a rank-1 view of scalars. No fuel is charged.
   - `host.error.` and the extern's name, such as `host.error.gain`: the host function returned a status other than `CINT_OK`. The status is the fault's one operand, an `I64`.
   - `host.result.` and the extern's name: the host function returned `CINT_OK` without one alternative of the declared result.
10. A parameter of type `Grants` (SPEC-04P.3 6.9.9) has no C form. A service extern's row counts it among its parameters and leaves it out of its type signature, and the runtime passes the context's grant set for it (9.4). An export with a `Grants` parameter gets no wrapper and no row of the reflection table, and stays callable from cint code. In cint 0.2.2 a host program's calls therefore reach no standard service: the services serve the `main` of a program, as 9.4 shows [A-12].
11. The Python package passes no host table and no grants. A module whose exports call a host import loads, and each call of the import faults `host.call`, which the package raises as `cint.UnsupportedFault`.

**Examples**

12. This module declares one host import, `pure`, and exports a function that calls it:

```cint
// gain.ci: a library whose export calls a host function.
extern pure I64 gain(I64 x);

export I64 boost(I64 x) {
    return gain(x) + 1;
}
```

The host program below reads the import's row from the descriptor, supplies a function for it and calls `boost` twice. Without its host table, creating the context is refused. The host function refuses a negative argument, and the call faults:

```c
/* gain_host.c: supplies the extern `gain` of gain.ci and calls `boost`. */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "gain.h"

static const char *const fault_names[] = {"", "E_OVERFLOW", "E_DIV_ZERO", "E_BOUNDS", "E_SHAPE",
    "E_SHIFT", "E_NARROW", "E_ALIAS", "E_STALE_HANDLE", "E_FUEL", "E_UNSUPPORTED", "E_DOMAIN",
    "E_DEPTH", "E_ASSERT"};

/* An I64 operand of a fault record: its tag byte, then 8 bytes, little-endian. */
static int64_t operand_i64(const cint_tvalue *t)
{
    uint64_t u = 0;
    for (int k = 8; k >= 1; k--) u = (u << 8) | t->bytes[k];
    return (int64_t)u;
}

/* The host function: three times its argument, or a refusal outside 0 to INT64_MAX / 3. */
static cint_status gain(void *user, const cint_value *args, uint32_t nargs, cint_value *result)
{
    (void)user;
    if (nargs != 1 || args[0].as.i64 < 0 || args[0].as.i64 > INT64_MAX / 3) return CINT_REFUSED;
    result->as.i64 = args[0].as.i64 * 3;
    return CINT_OK;
}

int main(void)
{
    const cint_host_table *table = cint_imports_desc.tables[0];
    const cint_host_import *row = &table->imports[0];
    cint_host_fn fn;
    cint_ctx_config config;
    cint_ctx *ctx = NULL;
    static cint_fault_record r;
    int64_t out = 0;
    cint_status s;

    printf("import %.*s, %" PRIu32 " parameter\n", (int)row->name.len, row->name.bytes, row->param_count);
    memset(&fn, 0, sizeof fn);
    fn.name = "gain";
    fn.effect = CINT_PURE;
    cint_signature_hash(row->type_signature, (size_t)row->type_signature_len, fn.signature);
    fn.call = gain;

    memset(&config, 0, sizeof config);
    config.size = sizeof config;
    config.program = cm_4_gain.program;
    config.module = &cm_4_gain;
    config.imports = &cint_imports_desc;
    printf("create without the host table: status %d\n", cint_ctx_create(&config, &ctx));
    config.host_count = 1;
    config.host = &fn;
    s = cint_ctx_create(&config, &ctx);
    printf("create with it: status %d\n", s);
    if (s != CINT_OK) return 1;

    s = cx_4_gain_5_boost(ctx, CINT_FUEL_UNBOUNDED, 14, &out);
    printf("boost(14): status %d, result %" PRId64 "\n", s, out);
    s = cx_4_gain_5_boost(ctx, CINT_FUEL_UNBOUNDED, -1, &out);
    printf("boost(-1): status %d\n", s);
    if (cint_ctx_fault(ctx, &r) == CINT_OK && r.code != 0)
        printf("fault %s %.*s, operand %" PRId64 "\n", fault_names[r.code], (int)r.operation_len, r.operation,
               operand_i64(&r.operands[0]));
    cint_ctx_destroy(ctx);
    return 0;
}
```

Built and run, with the library's own header on the include path:

```console
$ cint build --lib --out out gain.ci
built: out/libgain.so
$ cc -std=c17 -Wall -Wextra -Wpedantic -I "$CINT_RT" -I out gain_host.c -L out -lgain -Wl,-rpath,'$ORIGIN/out' -o gain_host
$ ./gain_host
import gain, 1 parameter
create without the host table: status 2
create with it: status 0
boost(14): status 0, result 43
boost(-1): status 1
fault E_UNSUPPORTED host.error.gain, operand 2
```

13. `boost(14)` is `gain(14) + 1`, 43. The refusal is status 2, `CINT_REFUSED`, which becomes the operand of the fault `host.error.gain`. The context is then Faulted, as after any fault (7.1).

<a id="the-python-package"></a>

## 8 The Python package

<a id="the-package-surface"></a>

### 8.1 The package surface

The package `cint` loads a library, or builds one from a `.ci` source, and makes each export a method of a context. It needs CPython 3.11 or later and nothing outside the standard library; a call that returns a NumPy array needs NumPy only when it is made.

**Synopsis**

```text
cint.load(source, *, backend="cpu") -> Module
Module.context(*, device=None, record=None, fuel=10**9, depth=256,
               frame_arena_elements=2_097_152) -> Context
Context.<export>(*args, fuel=..., **written) -> result
Context.entry(name) -> callable
Context.fault() -> Fault | None             Context.clear_fault() -> None
Context.fuel_consumed() -> int               Context.last_entry() -> EntryReport
Context.checkpoint() -> bytes                Context.restore(data) -> None
Context.state_hash() -> bytes                Context.entry_scope() -> context manager
cint.borrow(obj, *, elem=None, writable=False, publish=None, layout="any", context=None) -> View
cint.copy(obj, *, elem=None, device=None, from_float=None) -> Buffer
cint.empty(shape, elem, *, device=None) -> Buffer
cint.from_float(obj, elem, *, rounding="half_even", overflow="refuse") -> Buffer
cint.to_float(view_or_buffer, *, dtype="float64") -> numpy.ndarray
cint.Recorder(path, *, mode="full")          cint.replay(module, path, *, backend="ref", stop=None)
cint.read_checkpoint(data) -> CheckpointView
```

**Semantics**

1. `cint.load(path)` with a path to a `.ci` file builds the program with `cint build --lib` into a build directory keyed by the digests of its modules and the identity of the toolchain, so loading unchanged source again builds nothing; with a path to a library file it loads that file. The library a load builds from source also holds the checkpoint, recording and replay functions of the runtime, which 9.2 and 9.3 need; a library written by `cint build --lib` directly does not, and those calls on its contexts raise `NotImplementedError`. The `cint` command is the one the environment variable `CINT_EXE` names, else `cint` on the search path. `backend` is `"cpu"` for the executor `cpu-c17` or `"cuda"` for `gpu-cuda`; SPEC-09P.2 5.3 names every backend [SPEC-09P.2 BACK-15].
2. `Module.context()` creates a context. `fuel` is the budget of each entry, `None` for no budget; `depth` is the call-depth limit; `frame_arena_elements` is the frame-arena capacity in 8-byte elements. Print statements of a context are discarded unless it records (9.3).
3. Each export with a row in the reflection table is a method of the context, and `ctx.entry(name)` reaches the same call by name, which serves a name that is a Python keyword or a method of `Context`. A call builds its arguments from the row's parameter names, modes, element types and ranks. An argument the entry may write, which is a function's `inout` and a kernel's `out` and `inout`, is passed by name, as in `ctx.advance(p, v, 16, next=out)`, and the caller supplies every such array; other arguments are positional or named. A keyword `fuel=` sets the budget of one call. An export whose signature holds a struct is listed and reachable through `ctx.entry`, and calling it raises `NotImplementedError` naming the parameter.
4. A completed call returns the export's result: a Python `int` for an integer, a `bool` for `Bool`, `None` for `void`. An error value raises `cint.ErrorResult` (8.5).
5. `Context.last_entry()` gives the copy report of the last entry (6.4). `Context.fault()` gives the held fault, or `None`. `Context.entry_scope()` is a context manager that takes a checkpoint when its block starts and restores it when a `cint.Fault` leaves the block, which then goes on raising; an error result, another exception or a normal exit restores nothing.
6. The objects of the package are `View`, which owns no memory and borrows host memory (8.2); `Buffer`, which owns its storage (6.2); and `Fixed`, an exact fixed-point scalar with `raw` and `as_fraction()`.
7. The exceptions, and what raises them:

| Exception | Raised for |
|---|---|
| `cint.Fault` | `CINT_FAULT`. One subclass per code: `OverflowFault`, `DivZeroFault`, `BoundsFault`, `ShapeFault`, `ShiftFault`, `NarrowFault`, `AliasFault`, `StaleHandleFault`, `FuelFault`, `UnsupportedFault`, `DomainFault`, `DepthFault`, `AssertFault`. Its attributes are those of the fault record: `code`, `operation`, `operands`, `exact`, `limit`, `file`, `line`, `column`, `stack`, `revision`, `kernel`, `dispatch`, `phase`, `work_item`, `work_index`, `step`, `reason`, and `record`, the canonical bytes. |
| `cint.Refused` | `CINT_REFUSED`: a refused call. It carries `code` and the operand fields, and no position. |
| `cint.BorrowRefused` | A borrow that `cint.borrow` cannot make (8.2); a subclass of `Refused`. |
| `cint.Busy` | `CINT_BUSY`, including an output with a live export. |
| `cint.Faulted` | An entry on a context that holds a fault. `held` is that fault, and `nothing_ran` says whether it happened before any code of its entry ran, in which case `clear_fault()` alone restores the state. |
| `cint.Unpublished` | Reading a `Buffer` that has never been published. |
| `cint.StaleHandle` | Using a `View` or `Buffer` after its release. |
| `cint.ResourceError` | `CINT_RESOURCE` during an entry. |
| `cint.ReplayDivergence`, `cint.Unreplayable` | `CINT_DIVERGED` and `CINT_UNREPLAYABLE` from `cint.replay`. |
| `cint.ErrorResult` | An entry that returned an error value. It is not a `Fault`. |
| `cint.LoadError`, `cint.BuildError` | A library that cannot be loaded, or a source that cannot be built. |

8. <a id="P-16"></a>Each call releases the GIL while the entry runs. A second call on a context that is running an entry raises `cint.Busy`, and separate contexts may run in separate threads [P-16].

<a id="borrowing-cpu-arrays-without-a-copy"></a>

### 8.2 Borrowing CPU arrays without a copy

`cint.borrow` lends the memory of a NumPy array, or of any object with the buffer protocol or DLPack, to cint code as it is. It copies nothing; what it cannot take as it is, it refuses, and says what to do instead.

**Semantics**

1. <a id="P-1"></a>`cint.borrow` chooses a protocol. An object with the buffer protocol, whose `__dlpack_device__`, if it has one, reports `kDLCPU`, is borrowed through the buffer protocol: the package asks for `PyBUF_STRIDES | PyBUF_FORMAT`, with `PyBUF_WRITABLE` when `writable=True`, and never for indirect buffers. An object without the buffer protocol whose `__dlpack__` reports `kDLCPU` is borrowed through DLPack (8.3). Any other object is refused [P-1].
2. <a id="P-2"></a>The `View` holds the `Py_buffer` or the DLPack capsule, and with it a reference to the exporter, until it is released by `View.release()`, by the end of a `with` block, or by garbage collection. Holding the exporter keeps the memory alive; it does not keep it from changing (8.4) [P-2].
3. <a id="P-3"></a>The element type comes from the signedness of the format character and the reported item size, not from the character's nominal C type, so NumPy's `int64` maps to `I64` whether its format is `l` or `q`. The prefixes `@`, `=` and `<` mean native order, which is little-endian on every host [P-3]:

| Format | Item size | Element |
|---|---|---|
| `b`, `h`, `i`, `l`, `q`, `n` | 1, 2, 4, 8 | `I8`, `I16`, `I32`, `I64` |
| `B`, `H`, `I`, `L`, `Q`, `N` | 1, 2, 4, 8 | `U8`, `U16`, `U32`, `U64` |
| `?` | 1 | `Bool`; each byte shall be 0 or 1, and is checked at entry |
| `c` | 1 | refused; use `B` |
| `e`, `f`, `d`, `g`, `Z...` | any | refused: floating-point data is converted explicitly (8.5) |
| any, with the prefix `>` or `!` | any | refused; `cint.copy` takes it |
| struct formats, `x`, `s`, `P`, `O` | any | refused |

4. <a id="P-3a"></a>`cint.copy` of a big-endian integer array converts each element's byte order exactly as it copies [P-3a].
5. <a id="P-4"></a>When `elem` is given, it shall equal the mapped element, except that a fixed-point `elem` whose storage type is the mapped element, such as `"Q32.32"` over `I64`, registers the buffer with that fixed-point type. Any other difference is refused. The package never converts [P-4].
6. <a id="P-5"></a>Byte strides shall be multiples of the item size, and the data pointer shall be aligned for the element type (7.2). Negative strides are accepted and registered as 6.5 describes [P-5].
7. <a id="P-6"></a>Rank 0 and ranks above 4 are refused. A scalar is passed as a Python `int` [P-6].
8. <a id="P-7"></a>`writable=True` over a read-only export is refused, and `writable=False` over a writable export gives a `read` view [P-7].
9. <a id="P-8"></a>`layout="C"` or `layout="F"` refuses an export that is not contiguous in that order; `layout="any"` accepts any strided layout that passes paragraph 6 [P-8].
10. <a id="P-9"></a>`cint.borrow` never copies an input, under any combination of arguments. Every condition it cannot meet raises `BorrowRefused`, which names the condition and the alternative, usually `cint.copy`. The only copies that involve a borrowed `View` are the output copy that `publish="copy"` asks for (6.4) and the snapshot of an input taken while recording [P-9].
11. <a id="P-20"></a>A `View` or `Buffer` is registered with a context on its first use there, after every argument of the entry has passed the checks of a refused call, and keeps that registration until it is released or the context is restored. After a restore, its next use registers it again under a new identifier [P-20].
12. <a id="P-20a"></a>An argument that is not a `View` or a `Buffer` but that `cint.borrow(obj)` would accept with its defaults, passed to an `in` parameter, is borrowed read-only for that call and released when the call returns. A parameter the entry may write takes only a `View` or a `Buffer`, and any other argument for it is refused. A list, a tuple, a `range`, or any object with neither the buffer protocol nor `__dlpack__` is refused, naming `cint.copy(seq, elem=...)`, which converts nested sequences of `int` exactly, all or nothing [P-20a].

**Examples**

13. Transposed, Fortran-ordered and reversed arrays are borrowed as they are, and what cannot be borrowed is refused:

```cint
// grid.ci: views of rank 2, and two dispatches in one entry.
export I64 corner[h, w](in I64[h, w] a) {
    return a[0, 0] * 1000 + a[h - 1, w - 1];
}

export kernel triple[h, w](in I64[h, w] a, out I64[h, w] b) over [i: h, j: w] {
    b[i, j] = a[i, j] * 3;
}

export kernel double_it[n](in I64[n] x, out I64[n] y) over [i: n] {
    y[i] = x[i] + x[i];
}

export void twice[n](inout I64[n] a, inout I64[n] b) {
    double_it(a, b);
    double_it(b, a);
}
```

```python
# borrow.py: what cint.borrow takes without a copy, and what it refuses.
import numpy as np
import cint

mod = cint.load("grid.ci")
ctx = mod.context()

a = np.arange(6, dtype=np.int64).reshape(2, 3)
print(ctx.corner(a), ctx.corner(a.T), ctx.corner(np.asfortranarray(a)), ctx.corner(a[::-1, ::-1]))
print(cint.borrow(a[::-1, ::-1]))
print(cint.borrow(np.array([True, False])))
print(ctx.last_entry())

refused = {
    "float64": lambda: cint.borrow(np.zeros(4)),
    "big-endian": lambda: cint.borrow(np.zeros(4, dtype=">i8")),
    "misaligned": lambda: cint.borrow(np.frombuffer(bytearray(17), dtype=np.int64, count=2, offset=1)),
    "rank 5": lambda: cint.borrow(np.zeros((1, 1, 1, 1, 1), dtype=np.int64)),
    "writable over read-only": lambda: cint.borrow(np.frombuffer(b"abcd", dtype=np.uint8), writable=True),
}
for label, attempt in refused.items():
    try:
        attempt()
    except cint.BorrowRefused as e:
        print(label + ":", type(e).__name__, e.code)

print(cint.copy(np.array([15625], dtype=">i8")).tolist())
print(cint.copy([[1, 2], [3, 4]], elem="I16").tolist())
try:
    ctx.corner([[1, 2], [3, 4]])
except cint.Refused as e:
    print("list:", type(e).__name__, e.code)
```

```console
$ python3 borrow.py
5 5 5 5000
<cint.View I64[2, 3] read-only, no publish via buffer protocol>
<cint.View Bool[2] read-only, no publish via buffer protocol>
corner: no copies
float64: BorrowRefused E_UNSUPPORTED
big-endian: BorrowRefused E_UNSUPPORTED
misaligned: BorrowRefused E_UNSUPPORTED
rank 5: BorrowRefused E_UNSUPPORTED
writable over read-only: BorrowRefused E_UNSUPPORTED
[15625]
[[1, 2], [3, 4]]
list: Refused E_UNSUPPORTED
```

14. The transposed array `a.T` has shape (3, 2) and strides of 1 and 3 elements, and its corners are still 0 and 5. The reversed array is registered from its lowest address with a negative stride in each dimension, and the copy report of that call lists no copies. The big-endian array holds the 8-byte big-endian encoding of 15625, which `cint.copy` reads correctly.

<a id="borrowing-through-dlpack"></a>

### 8.3 Borrowing through DLPack

DLPack lets an array library hand its memory to another without a copy. The package consumes DLPack for objects that offer no buffer protocol, and produces read-only DLPack capsules for its `Buffer` objects.

**Semantics**

1. <a id="P-10"></a>The package exchanges a DLPack capsule when it borrows, to read the tensor's description, and again at each entry that uses the `View`, calling `__dlpack__(stream=s, max_version=(1, 0), dl_device=None, copy=False)`. The data pointer, shape, strides and data type shall equal those of the first exchange; a change is refused. A producer that cannot honor `copy=False`, or a capsule marked `DLPACK_FLAG_BITMASK_IS_COPIED`, is refused. For CPU memory the stream is `None`; for CUDA memory it is the context's stream, so the producer's earlier writes come first [P-10].
2. <a id="P-10a"></a>If `__dlpack__` with `max_version` raises `TypeError`, the package calls it with `stream` alone, as the array API standard allows for older producers. Such an unversioned capsule cannot express read-only status or `copy=False`, so it is accepted only for a read-only borrow of CPU memory [P-10a].
3. <a id="P-11"></a>A device array shall be on the context's device. A CUDA array used with a context of another device, or with a CPU context, is refused before any exchange at the entry that binds it, or when it is borrowed if `context=` is given. There is no implicit transfer [P-11].
4. <a id="P-12"></a>The data types `kDLInt` and `kDLUInt` with 8, 16, 32 or 64 bits and one lane map to `I8` to `U64`, and `kDLBool` with 8 bits and one lane maps to `Bool`, whose bytes are checked at entry. Every other type code, every other width of `kDLBool`, and every lane count other than 1 is refused [P-12].
5. <a id="P-13"></a>DLPack strides are in elements, and a NULL `strides` means compact row-major order. `byte_offset` shall be a multiple of the element size. A tensor marked read-only gives a view whose ceiling is `read`, and `writable=True` over it is refused [P-13].
6. <a id="P-14"></a>The package calls each capsule's deleter when it no longer needs the tensor: a capsule of one entry when that entry ends, the first capsule when the `View` is released, and a refused capsule at once [P-14].

**Examples**

7. An object that offers only DLPack is borrowed through it, and a `Buffer` exported through DLPack to NumPy is read-only:

```python
# dlpack.py: borrowing through DLPack, and the read-only mark on exports.
import numpy as np
import cint

mod = cint.load("calls.ci")
ctx = mod.context()

class DLPackOnly:
    """An array that offers DLPack and not the buffer protocol."""
    def __init__(self, a):
        self.a = a
    def __dlpack__(self, **kwargs):
        return self.a.__dlpack__(**kwargs)
    def __dlpack_device__(self):
        return self.a.__dlpack_device__()

x = np.arange(4, dtype=np.int64)
v = cint.borrow(DLPackOnly(x))
print(v)
buf = cint.empty((4,), "I64")
ctx.square(v, y=buf)
print(buf.tolist())

m = np.from_dlpack(buf)
print("writeable:", m.flags.writeable)
try:
    m[0] = 99
except ValueError:
    print("write refused; buf =", buf.tolist())
del m
with memoryview(buf) as mv:
    print("memoryview read-only:", mv.readonly)
```

```console
$ python3 dlpack.py
<cint.View I64[4] read-only, no publish via DLPack>
[0, 1, 4, 9]
writeable: False
write refused; buf = [0, 1, 4, 9]
memoryview read-only: True
```

**Notes**

8. Consumers differ in honoring the read-only mark (6.3). For CUDA arrays, which need a machine with an NVIDIA GPU, the rules above are those the package applies; no CUDA run is shown in this document.

<a id="outputs-and-mutation-hazards"></a>

### 8.4 Outputs and mutation hazards

Results reach Python memory in three ways: a kernel's output into a `Buffer`, a kernel's output copied into borrowed memory, and a function's `inout` array written in place. This subclause shows each, and what happens when memory is shared in ways the rules forbid.

**Semantics**

1. The rules of 6.4 apply to every output: a dispatch publishes all of its outputs or none, a borrowed output needs `writable=True, publish="copy"`, and a function's `inout` array is written in place.
2. A `Buffer` bound to a kernel output is pending for that dispatch, published when it completes, and keeps its previous contents when it faults. Reading it raises `Unpublished` only if it has never been published (6.2).
3. While an entry runs with the GIL released, borrowed memory can be changed by another NumPy view, another thread or native code. No claim is made for memory that a foreign writer changes during a borrow: the result of such an entry is outside the replay contract, no error is raised, and memory safety is not claimed, since a writer can change an index value between its check and its use. A caller that cannot exclude such writers passes `cint.copy` of its input.
4. The hazards, and what a caller does about each:

| Situation | What happens | What the caller does |
|---|---|---|
| Input borrowed, nothing else writes it | Exact result | Nothing |
| Input borrowed, another writer changes it during the call | No assurance, including memory safety | Use `cint.copy`, or synchronize |
| Kernel output borrowed with `publish="copy"` | Copied in when the dispatch completes | Accept the copy, or use a `Buffer` |
| Kernel output borrowed without `publish="copy"` | Entry fault `E_UNSUPPORTED`, `bind.limit` | Add `publish="copy"`, or use a `Buffer` |
| Kernel output given as a bare array | Refused | Pass a `View` or a `Buffer` |
| Function `inout` array | Written in place; after a fault it holds the stores that completed | Pass a copy and copy it back, if all or nothing is needed |
| One memory passed as input and output | Entry fault `E_ALIAS`, `bind.alias` | Use a separate output |
| `Buffer` output with a live export | `Busy`, code `E_ALIAS` | End the export first |
| `View` used after its release | `StaleHandle` | Borrow again |

**Examples**

5. The script below shows each case on the library `calls.ci`:

```cint
// calls.ci: the values and arrays that cross from Python.
error ParseError { empty, bad_digit, too_large }

export ParseError!I64 parse_u(in U8[_] s) {
    if (len(s) == 0) {
        return ParseError.empty;
    }
    I64 v = 0;
    for i in 0..len(s) {
        U8 c = s[i];
        if (c < 48 || c > 57) {
            return ParseError.bad_digit;
        }
        v = v * 10 + ((c - 48) as I64);
    }
    return v;
}

export void scale[n](inout I32[n] a, I32 k) {
    for i in 0..n {
        a[i] = a[i] * k;
    }
}

export Bool positive(I64 x) {
    return x > 0;
}

export Bool both(Bool a, Bool b) {
    return a && b;
}

export I8 narrow(I64 x) {
    return x as I8;
}

export kernel square[n](in I64[n] x, out I64[n] y) over [i: n] {
    y[i] = x[i] * x[i];
}
```

```python
# outputs.py: how results reach Python memory, and the hazards around them.
import numpy as np
import cint

mod = cint.load("calls.ci")
ctx = mod.context()
x = np.arange(4, dtype=np.int64)

def show(label, call):
    try:
        call()
        print(label + ": ok;", ctx.last_entry())
    except (cint.Fault, cint.Refused, cint.Busy, cint.Unpublished) as e:
        parts = [type(e).__name__, getattr(e, "code", None), getattr(e, "operation", None)]
        print(label + ":", " ".join(p for p in parts if p))
        if isinstance(e, cint.Fault):
            ctx.clear_fault()

# A kernel output in borrowed memory needs publish="copy".
y = np.zeros(4, dtype=np.int64)
show("borrowed, no publish", lambda: ctx.square(x, y=cint.borrow(y, writable=True)))
show("borrowed, publish copy", lambda: ctx.square(x, y=cint.borrow(y, writable=True, publish="copy")))
print(y.tolist())
show("bare array as output", lambda: ctx.square(x, y=y))
show("same memory in and out", lambda: ctx.square(x, y=cint.borrow(x, writable=True, publish="copy")))

# A Buffer is published by the runtime; a live export blocks it as an output.
buf = cint.empty((4,), "I64")
show("read before publication", lambda: buf.tolist())
show("buffer", lambda: ctx.square(x, y=buf))
held = np.asarray(buf)
show("buffer with a live export", lambda: ctx.square(x, y=buf))
del held
show("after the export ends", lambda: ctx.square(x, y=buf))

# A function writes its inout argument in place, and a fault keeps finished stores.
a = np.array([1, 2, 3], dtype=np.int32)
ctx.scale(a=cint.borrow(a, writable=True), k=3)
print(a.tolist(), ctx.last_entry())
b = np.array([1, 2**30, 3], dtype=np.int32)
show("scale overflow", lambda: ctx.scale(a=cint.borrow(b, writable=True), k=4))
print(b.tolist())
```

```console
$ python3 outputs.py
borrowed, no publish: UnsupportedFault E_UNSUPPORTED bind.limit
borrowed, publish copy: ok; square: 1 copy
  publish-copy y: I64 x 4
[0, 1, 4, 9]
bare array as output: Refused E_UNSUPPORTED
same memory in and out: AliasFault E_ALIAS bind.alias
read before publication: Unpublished
buffer: ok; square: 1 copy
  staging y: I64 x 4
buffer with a live export: Busy E_ALIAS
after the export ends: ok; square: 1 copy
  staging y: I64 x 4
[3, 6, 9] scale: no copies
scale overflow: OverflowFault E_OVERFLOW mul.checked.i32
[4, 1073741824, 3]
```

6. The last call faults at `a[1] * 4`, which is 2^32 and outside `I32`. The store to `a[0]` had completed, so the array holds 4 there; `a[1]` and `a[2]` are unchanged.

<a id="values-that-cross"></a>

### 8.5 Values that cross

Scalars cross as Python values of exactly the parameter's type. Nothing converts implicitly: a value out of range, a value of another numeric type, or a floating-point value is refused before the call runs.

**Semantics**

1. <a id="P-21"></a>A Python `int` for an integer parameter takes the parameter's type, and a value outside its range is refused with `E_NARROW`. A Python `bool` is refused for an integer parameter, although `bool` is a subclass of `int`. A `Bool` parameter takes `True` or `False`, as a Python `bool` or a `numpy.bool_`, and refuses an `int`; a `Bool` result is a Python `bool`. Text crosses as `in U8[n]`: Python passes `bytes`, which is borrowed for the call, and a `str` is refused, naming `s.encode("utf-8")` [P-21].
2. <a id="P-21a"></a>A fixed-point parameter takes only a `cint.Fixed` of exactly its format. A bare `int` is refused, because it could mean the raw storage or the value; `cint.Fixed("Q32.32", value=1)` has raw 4294967296, and `cint.Fixed("Q32.32", raw=1)` is 2^-32. `cint.Fixed(..., value=v)` takes an `int` or a `fractions.Fraction` that is exactly representable and in range, and refuses any other with `E_NARROW` [P-21a].
3. <a id="P-22"></a>A NumPy integer scalar is taken for an integer parameter only if its kind and item size map to the parameter's type under 8.2: `numpy.int64` and `numpy.longlong` both map to `I64`, and `numpy.int32` for an `I64` parameter is refused [P-22].
4. <a id="P-23"></a>A Python `float`, a `numpy.floating` or a `decimal.Decimal` is refused for every parameter [P-23].
5. <a id="P-24"></a>Integer results are Python `int` values, exact at every width, and fixed-point results are `cint.Fixed`, whose decimal form is exact since its scale is a power of two [P-24].
6. An entry that returns an error value raises `cint.ErrorResult`, which is not a `Fault`, and leaves the context Ready. It carries `set`, the error set; `name`, the value; and `tag`, its number from 1. Each error set of the module has its own subclass, reachable as an attribute of the module, so `except mod.ParseError` catches that set alone and `except cint.Fault` catches none.
7. <a id="X-1"></a>Floating-point values enter cint memory only through an explicit conversion, `cint.from_float` or `cint.copy(..., from_float=...)`, which takes a target type, a rounding mode and an overflow policy [X-1].
8. <a id="X-2"></a>The accepted formats are IEEE 754 binary32 and binary64. Others, such as binary16, bfloat16 and x87 extended, are refused [X-2].
9. <a id="X-3"></a>The conversion is exact before it rounds. It decodes each value's bits into an integer significand `m` and exponent `e`, forms the exact rational `m * 2^(e + f)` for a target with `f` fractional bits (0 for an integer type), rounds once and checks the range. It does no floating-point arithmetic, so the processor's rounding mode and its treatment of subnormal values do not affect it [X-3].
10. <a id="X-4"></a>The rounding modes are those of SPEC-04P.3 6.7.15: `floor`, `ceil`, `trunc`, `away`, `half_even`, `half_away`, `half_trunc`, `half_up` and `half_down`, with `half_even` the default. Any other name is refused with `E_UNSUPPORTED` before anything is converted [X-4].
11. <a id="X-5"></a>NaN of any payload and both infinities are refused under every overflow policy, with `E_NARROW`, the reason `non_finite` and the input's bits as the operand [X-5].
12. <a id="X-6"></a>The overflow policy `refuse`, the default, refuses a value whose rounded result is outside the target's range with `E_NARROW`, carrying the input's bits, its exact value as a significand and a power of two, and the target's range. The policy `saturate`, which shall be named, clamps to the range. There is no wrapping policy [X-6].
13. <a id="X-7"></a>Negative zero converts to 0. Subnormal inputs convert exactly under the same rule [X-7].
14. <a id="X-8"></a>A conversion of many values is all or nothing: if any element is refused, no `Buffer` is made and the exception names the first refused element in row-major order and the number refused [X-8].
15. <a id="X-10"></a>`cint.to_float` converts integers or fixed-point values to binary64, rounding to nearest with ties to even, computed from the integer and not through floating-point arithmetic: the `I64` value 2^53 + 1 converts to 9007199254740992.0 [X-10].
16. <a id="X-11"></a>The result of `cint.to_float` is a value for display or export. It is not part of the canonical state, is not hashed and enters cint memory again only through paragraph 7. A round trip through `to_float` and `from_float` need not give back the same value [X-11].

**Examples**

17. Scalars, error results and conversions on `calls.ci` (8.4):

```python
# values.py: scalar values, error results and float conversions at the Python boundary.
import numpy as np
import cint

mod = cint.load("calls.ci")
ctx = mod.context()

def show(label, call):
    try:
        print(label + ":", repr(call()))
    except (cint.Fault, cint.Refused, cint.ErrorResult, TypeError) as e:
        parts = [type(e).__name__, getattr(e, "code", None)]
        print(label + ":", " ".join(p for p in parts if p))
        if isinstance(e, cint.Fault):
            ctx.clear_fault()

show("narrow(100)", lambda: ctx.narrow(100))
show("narrow(300)", lambda: ctx.narrow(300))
show("narrow(2**63)", lambda: ctx.narrow(2**63))
show("narrow(np.int64(5))", lambda: ctx.narrow(np.int64(5)))
show("narrow(np.longlong(5))", lambda: ctx.narrow(np.longlong(5)))
show("narrow(np.int32(5))", lambda: ctx.narrow(np.int32(5)))
show("narrow(True)", lambda: ctx.narrow(True))
show("narrow(1.0)", lambda: ctx.narrow(1.0))
show("positive(7)", lambda: ctx.positive(7))
show("positive(True)", lambda: ctx.positive(True))
show("both(True, np.bool_(True))", lambda: ctx.both(True, np.bool_(True)))
show("both(1, 0)", lambda: ctx.both(1, 0))
show("narrow(5, fuel=0)", lambda: ctx.narrow(5, fuel=0))
show("narrow(5, fuel=-2)", lambda: ctx.narrow(5, fuel=-2))

show("parse_u(b'123')", lambda: ctx.parse_u(b"123"))
show("parse_u('123')", lambda: ctx.parse_u("123"))
try:
    ctx.parse_u(b"12x")
except mod.ParseError as e:
    print("parse_u(b'12x'):", e.set, e.name, e.tag, isinstance(e, cint.Fault))

show("from_float", lambda: cint.from_float(np.array([1.5, 2.5, -2.5, -0.0]), "I64").tolist())
show("from_float floor", lambda: cint.from_float(np.array([1.5, -2.5]), "I64", rounding="floor").tolist())
show("from_float Q16.16", lambda: [q.raw for q in cint.from_float(np.array([1.5, 0.1]), "Q16.16").tolist()])
show("from_float NaN", lambda: cint.from_float(np.array([1.0, float("nan")]), "I64"))
show("from_float 2**63", lambda: cint.from_float(np.array([2.0**63]), "I64"))
show("saturate", lambda: cint.from_float(np.array([2.0**63]), "I64", overflow="saturate").tolist())
show("to_float", lambda: cint.to_float(cint.copy(np.array([2**53 + 1], dtype=np.int64))).tolist())
```

```console
$ python3 values.py
narrow(100): 100
narrow(300): NarrowFault E_NARROW
narrow(2**63): Refused E_NARROW
narrow(np.int64(5)): 5
narrow(np.longlong(5)): 5
narrow(np.int32(5)): Refused E_UNSUPPORTED
narrow(True): Refused E_UNSUPPORTED
narrow(1.0): Refused E_UNSUPPORTED
positive(7): True
positive(True): Refused E_UNSUPPORTED
both(True, np.bool_(True)): True
both(1, 0): Refused E_UNSUPPORTED
narrow(5, fuel=0): FuelFault E_FUEL
narrow(5, fuel=-2): Refused E_NARROW
parse_u(b'123'): 123
parse_u('123'): Refused E_UNSUPPORTED
parse_u(b'12x'): ParseError bad_digit 2 False
from_float: [2, 2, -2, 0]
from_float floor: [1, -3]
from_float Q16.16: [98304, 6554]
from_float NaN: Refused E_NARROW
from_float 2**63: Refused E_NARROW
saturate: [9223372036854775807]
to_float: [9007199254740992.0]
```

18. `narrow(300)` runs and faults inside the function, at the conversion `x as I8`, so it is a `NarrowFault`. `narrow(2**63)` never runs: 2^63 is not an `I64`, so the argument cannot be formed and the call is refused. `narrow(5, fuel=0)` is formed and runs out of fuel at the entry's own charge. `0.1` in `Q16.16` is 6553.6 before rounding, and `half_even` gives raw 6554.

<a id="a-complete-example-with-numpy"></a>

### 8.6 A complete example with NumPy

This program uses `advance.ci` (Introduction) from Python: it borrows two NumPy arrays without a copy, writes into a `Buffer`, reads the result through a read-only export, makes the kernel fault and recovers. It was run with Python 3.13 and NumPy 2.5.3.

```python
# numpy_example.py: advance.ci called from Python with NumPy arrays.
import numpy as np
import cint

mod = cint.load("advance.ci")          # builds the library once, then reuses it
ctx = mod.context()

n = 8
pos = np.arange(n, dtype=np.int64)
vel = np.full(n, 3, dtype=np.int64)
nxt = cint.empty((n,), "I64")          # runtime-owned output, not yet published

with cint.borrow(pos) as p, cint.borrow(vel) as v:
    ctx.advance(p, v, 16, next=nxt)
print(nxt.tolist())
print(ctx.last_entry())

out = np.asarray(nxt)                  # a read-only export, no copy
print(out.flags.writeable, ctx.total(out))
del out                                # end the export before nxt is written again

vel[5] = 2**62
try:
    ctx.advance(pos, vel, 16, next=nxt)
except cint.OverflowFault as f:
    print(f.code, f.operation, f.operands, f.work_item, f.position)
    print(f)
print(nxt.tolist())

try:
    ctx.total(pos)
except cint.Faulted as e:
    print("Faulted, nothing ran:", e.nothing_ran)
ctx.clear_fault()
print(ctx.total(pos))
```

```console
$ python3 numpy_example.py
[48, 49, 50, 51, 52, 53, 54, 55]
advance: 1 copy
  staging next: I64 x 8
False 412
E_OVERFLOW mul.checked.i64 (4611686018427387904, 16) 5 advance.ci:3:31
E_OVERFLOW at advance.ci:3:31

operation: mul.checked.i64
left:      4611686018427387904
right:     16
result:    73786976294838206464
maximum:   9223372036854775807
kernel:    advance.advance, dispatch 0, phase work-item, work item 5, step 3
[48, 49, 50, 51, 52, 53, 54, 55]
Faulted, nothing ran: False
28
```

1. The first call borrows `pos` and `vel` and writes `nxt`; the copy report lists the one copy, of `nxt` from staging storage. `np.asarray(nxt)` is a read-only export, and passing it to `total` borrows it again for the call.
2. The second call passes the arrays bare, so each is borrowed for that call. Work-item 5 faults at `vel[i] * dt`, 2^62 times 16; the fault names the operation, both operands, the exact result and the bound. `nxt` keeps what the first call published.
3. The context is then Faulted, and `total` raises `cint.Faulted` without running. The held fault happened while work-items ran, so `nothing_ran` is false and the host decides what state to rebuild. After `clear_fault`, `total(pos)` gives 0 + 1 + ... + 7 = 28.

<a id="host-effects-checkpoints-and-replay"></a>

## 9 Host effects, checkpoints and replay

<a id="output-effects-and-ambient-access"></a>

### 9.1 Output effects and ambient access

cint code reaches the world outside its context only through what the host supplies. The one effect every library has is output: a print statement.

**Semantics**

1. <a id="H-8"></a>cint source has no clock, environment, file, network, thread or random-source operation. Each of these reaches cint code only through an extern: a host function (7.6) or a standard service (9.4). An entry's outcome therefore depends only on its semantic inputs and on what crossed the boundary (SPEC-04P.3 5.2.7) [H-8].
2. <a id="H-7"></a>A print statement is an output effect: an effect with no result. Its bytes reach the host through the output callback (7.3), or are discarded. While a context records, each print statement is written to the recording as an effect with the bytes and the status the host returned; on replay the bytes are compared with the recording and not printed, and a difference stops replay with `CINT_DIVERGED`. The `write` of `console/1` hands its bytes to the same callback (9.4) [H-7].

**Examples**

3. Each entry of the library `counter.ci` of 9.3, example 9, runs a print statement. While `record.py` records, each print is written to the recording as an effect, and the replays of that example compare its bytes with the recording.

<a id="checkpoints"></a>

### 9.2 Checkpoints

A checkpoint is a context's state as bytes. Restoring it into a context of the same program gives back that state exactly, on either CPU executor and on any machine.

**Semantics**

1. <a id="H-19"></a>A checkpoint is an envelope, which holds the format name, the runtime contract and the digests of the build, and the canonical state of 5.1, which holds the revision identity. The envelope is not hashed. A checkpoint is taken between entries, of a Ready or a Faulted context. Restoring a checkpoint whose revision identity differs from the program's is refused with `CINT_REFUSED`, and so is one whose runtime contract version is not exactly that of the context's runtime. A checkpoint taken on another executor or machine, with the same revision identity, is accepted [H-19].
2. <a id="H-19a"></a>Checkpoint bytes are untrusted input. A restore checks every structural rule before it replaces any state: minimal encodings, known tags and lengths, arena extents within capacity, child arenas within their parents, registry extents consistent with stored contents, pool slots within capacity, distinct and vacant free-list entries, generations within range, identifier counters above every identifier present and no trailing bytes. Malformed input is refused with `CINT_REFUSED`, and the context is unchanged [H-19a].
3. <a id="H-15"></a>The revision identity of a program is a SHA-256 digest over the profile, the runtime contract and the canonical intermediate form of the program with every module it imports, as SPEC-01P.3 specifies. It is the same for every executor and every machine, and it changes when the program or the runtime contract changes. A fault record, a checkpoint and a recording each carry it [H-15].
4. In Python, `Context.checkpoint()` returns the bytes, `Context.restore(data)` restores them, `Context.state_hash()` gives the 32-byte state hash and `cint.read_checkpoint(data)` decodes a checkpoint into a read-only view of its globals, arenas, pools, registry, counters and fault record.
5. <a id="H-19b"></a>Between entries, a context can move to another revision of its program. In cint 0.2.2 the workbench makes this change for its session, whose declarations each make a new revision of the session module (SPEC-06P.2); a library that `cint build --lib` writes, and the Python package, offer no call for it. A module variable whose qualified name and type, an array's extents and a struct's whole type included, are unchanged keeps its value; a new one starts at its initializer, and one that is retyped or removed is dropped. A module-level arena or pool whose module, name, kind, capacity and element type are unchanged keeps its contents, and a kept arena keeps its child arenas; every other container is dropped, and the new program's other containers start empty. The kept containers take the identifiers of the new program's declarations, and a handle names its container's new identifier, or no container when its container was dropped, so that its next use faults `E_STALE_HANDLE`. The registry, the buffer identifier counter, the number of entries and the number of effects are kept, and no identifier is issued twice. A recording holds the change as a revision event with the new revision's code (SPEC-01P.3 8.4). The runtime refuses the change for a context that holds a fault or that was created with a library's module descriptor, and for a program whose tables do not fit the context's state; the workbench then reports that the session's state cannot move to this revision, and starts the session's state again from its declarations, in a new context [H-19b].

<a id="recordings-and-replay"></a>

### 9.3 Recordings and replay

A recording holds the context's initial state, every call the host made on the context and every outcome, so that a run can be checked again without its environment. Replay makes the same calls on a new context and compares each outcome.

**Semantics**

1. <a id="H-14b"></a>A host records a context through a recorder, which makes the host's calls on the context and writes their events. Beginning a recording writes its header, the code of the program, and a checkpoint of the context as it is. Each entry, checkpoint, restore and cleared fault is then made through the recorder, and print statements are written as effects. Ending the recording writes its last event. A context is recorded by one recorder at a time. In cint 0.2.2 a recorded entry takes integer and `Bool` arguments only: an entry with an array argument is refused while recording. The recorder and replay are part of the runtime that `cint.load` builds from source (8.1), which passes no host table (7.6); the tools record extern calls, as SPEC-06P.2 11.2 describes [H-14b].
2. <a id="H-14"></a>A recording consists of a header, which holds the profile, the fuel model, the runtime contract, the revision identity and the recording mode; the code of each revision it runs; the initial checkpoint; and the stream of events [H-14]:
   - `entry`: its sequence number, the export's name, its canonical arguments, its fuel budget, depth limit and frame-arena capacity, and, for the program entry of a context that holds program arguments, those arguments (SPEC-01P.3 8.4);
   - `effect`: the bytes of a print statement and the status the host returned; or the call of an `observe` extern, with its name, a request block of its arguments and a response block of its result and the contents of its `out` views (SPEC-01P.3 8.4);
   - `unrestricted`: the call of an `unrestricted` extern, written before the call, after which the recording ends;
   - `outcome`: the status, the fault record bytes if any, the fuel consumed, the result or the error result and the state hash;
   - `checkpoint`, `restore` and `clear_fault`, with the state hashes and the record they concern;
   - `end`, written last, with the head of the chain of digests over the events and the number of events. A recording without it is incomplete and is never loaded in part.
3. <a id="H-18"></a>A recording is in `full` mode unless `digest` is asked for. A `full` recording reproduces a run on its own; a `digest` recording holds digests in place of input contents and checks a run whose inputs are supplied again [H-18].
4. <a id="H-17"></a><a id="H-17a"></a>Replay checks the recording whole, its checkpoints and its chain of digests, before it reads any of it. It then restores the initial checkpoint into the context it is given, and applies the events in order. Each entry is called with its recorded arguments. A program entry runs over the program arguments its event holds, or over none when it holds none, in `full` mode; in `digest` mode the context's own program arguments shall have the recorded length and digest, else replay diverges at that entry. Each print statement is compared with its effect and gets the recorded status. A `pure` extern is called. An `observe` extern's call is compared with its effect by position, site, name and request block; in `full` mode the recorded response is given to the program as the call's result and `out` contents and the host function is not called, and in `digest` mode the host function is called and its status and response digests are compared with the effect's. A difference is a divergence at that effect. Replay stops before an `unrestricted` event with `CINT_UNREPLAYABLE`. At each `outcome`, the status, the fault record bytes, the fuel consumed, the result, the error result and the state hash are compared in that order. The first difference stops replay with `CINT_DIVERGED`, the index of the event and what differed. An entry whose recorded outcome is `CINT_RESOURCE` depends on the machine and is not called again: replay stops before it with `CINT_UNREPLAYABLE` and the index of its event. A replay given a stop boundary stops after that boundary's comparisons and leaves the context as replay left it [H-17, H-17a].
5. <a id="H-16"></a>The replay contract: given a `full` recording, any conforming implementation that loads the program with the recorded revision identity and applies the events produces, at every entry, the same status, the same fault record bytes, the same fuel consumed, the same output and the same state hash. An executor that wrote a wrong value into any output would differ at that entry [H-16].
6. <a id="H-20"></a>Replay covers cint state only. Python or C code that runs between entries is not replayed; its influence on the context is exactly what crossed the boundary and was recorded [H-20].
7. <a id="H-21"></a>Replay is defined over canonical faults, not over the order in which work-items happened to fail. When several work-items of a dispatch fault, the reported fault is the one least in the canonical order of SPEC-04P.3 6.13.6, which puts the lowest work-item index first, on every executor [H-21].
8. In Python, `cint.Recorder(path, mode="full")` is passed to `Module.context(record=...)`, and closing it ends the recording. `cint.replay(module, path, backend=...)` replays it, with `backend="ref"` running the interpreter `cpu-sir-interp` and `backend="cpu"` running `cpu-c17`, and returns a result whose `status` is `"ok"` when every comparison agreed. With `stop=k`, replay stops after the `k`th boundary, and the result's `checkpoint` holds the state reached there.

**Examples**

9. The library below has module state:

```cint
// counter.ci: a library with module state, for checkpoints and recordings.
I64 tally = 0;

export I64 bump(I64 amount) {
    tally += amount;
    "tally is {tally}\n";
    return tally;
}
```

The program records a session with a checkpoint, a restore and a fault, replays it on both CPU executors, stops a replay after two entries, and reads the checkpoint it took:

```python
# record.py: checkpoints, a recording, and its replay on both CPU executors.
import cint

mod = cint.load("counter.ci")
with cint.Recorder("run.cintrec") as rec:
    ctx = mod.context(record=rec)
    print(ctx.bump(2), ctx.bump(40))
    saved = ctx.checkpoint()
    print(ctx.bump(1))
    ctx.restore(saved)
    print(ctx.bump(10))
    try:
        ctx.bump(2**62)
        ctx.bump(2**62)
    except cint.OverflowFault as f:
        print(f.operation, f.operands)
    ctx.clear_fault()
    final = ctx.state_hash()

for backend in ("ref", "cpu"):
    result = cint.replay(mod, "run.cintrec", backend=backend)
    print(backend, result.status, result.boundaries, result.final_state_hash == final)

part = cint.replay(mod, "run.cintrec", stop=2)
print(part.status, part.stopped, part.boundaries)
fresh = mod.context()
fresh.restore(part.checkpoint)
print(fresh.bump(0))

view = cint.read_checkpoint(saved)
print(view.globals)
```

```console
$ python3 record.py
2 42
43
52
add.checked.i64 (4611686018427387956, 4611686018427387904)
ref ok 6 True
cpu ok 6 True
ok True 2
42
(Global(name='counter.tally', type='I64', value=42),)
```

10. The restore takes `tally` back to 42, so the next `bump(10)` gives 52. The second `bump(2**62)` overflows; the fault and the cleared fault are recorded. Each replay makes the six entries again, compares every outcome and ends with the same state hash. The replay stopped after two entries leaves `tally` at 42, which a new context restores from the replay's checkpoint.

<a id="services-and-grants"></a>

### 9.4 Services and grants

A standard service is a set of externs that the runtime itself supplies: `fs/1` reads and writes files under the host directories that grants name, and `console/1` writes the program's output and reads the terminal. SPEC-04P.3 6.9.9 gives the program's side: `requires`, the `Grants` parameter, the `denied` result and what each extern returns. This subclause gives the host's side: how the grants reach the runtime, and what each service does on the host.

**Syntax**

```text
grants      = line { LF line } [ LF ]                the bytes of config.grants, no NUL
line        = name [ ":" arguments ] [ "=" directory ]
name        = 1 to 63 of the characters A-Z a-z 0-9 _ - .
arguments   = the capability's arguments, separated by ","
directory   = a path of the host, relative to the current directory unless absolute
```

**Constraints**

1. A host program passes the grant set of a context as `config.grants`, `grants_length` bytes of lines, or NULL with length 0 for an empty grant set. Each line is a capability text (SPEC-04P.3 6.9.9, paragraph 7). A line of `fs.read` or `fs.write` has one argument, a logical path, the grant's *root*, followed by `=` and the host directory that the root maps to, as in `fs.read:/data=/srv/data`.

**Semantics**

2. `cint_ctx_create` reads the lines before it creates the context's state, and refuses the configuration with `CINT_REFUSED` when the bytes hold a NUL; when a line has an empty name, a name longer than 63 characters or a name with another character; when a line of `fs.read` or `fs.write` has no logical path after the colon, or no `=` and directory; when a line of any other capability holds `=`; or when a directory cannot be opened as a directory. Every other line is accepted: a grant that the program does not require, or whose capability no service checks, covers no call [A-10].
3. Each directory is opened once, when the context is created, and stays the root of its grant for the life of the context. The program never sees the directory: it names files by logical paths under the root.
4. A call of `fs/1` checks its path before its grant. A path that is not a logical path (SPEC-04P.3 6.9.9, paragraph 12), or that is longer than 4,095 bytes, returns `path`. A path that no grant covers returns `denied`: `read` and `stat` need a grant of `fs.read`, and `put` one of `fs.write`, whose root is the path or an ancestor of it. Then the path below the root is looked up in the root's directory, one segment at a time:
   - A segment longer than 255 bytes returns `path`.
   - A segment matches only an entry of exactly its bytes. On a host whose file system matches names without regard to case, a segment that the file system would match with another case returns `not_found`.
   - A symbolic link is never followed: a link where the path names a directory or a file returns `not_found`.
   - A segment before the last that names a file returns `not_directory`.
   - For `read` and `stat`, the root itself, or a directory where the path names a file, returns `is_directory`, and an entry that is neither a regular file nor a directory returns `not_found`.
5. `read(g, path, offset, buf)` returns `bounds` unless `offset` is from 0 to the file's size. Otherwise it reads, from `offset`, the lesser of the bytes left in the file and the extent of `buf`, writes them to the start of `buf` and returns their count. `stat(g, path)` returns the file's size in bytes.
6. `put(g, path, bytes)` replaces the file at `path` with `bytes`, or leaves the directory as it was. The file may be absent or a regular file. A directory there returns `is_directory`, and any other entry, or one whose name differs from the segment only in the case of ASCII letters, returns `exists`. The runtime writes the bytes to a new entry named `.cint-put-` and a number in the same directory, flushes it to the device, and renames it over the target, so a reader of the target sees the old bytes or the new ones and never a part. When a step fails, the new entry is removed. A directory of the path that does not exist returns `not_found`: `put` creates no directory.
7. An error of the host that is a refusal of permission returns `denied`; no space left or a file too large, `quota`; a name too long for the host, `path`. An error that `fs.FsError` has no value for makes the call return `CINT_RESOURCE`, which faults the entry `host.error.fs/1.read` (or `stat`, or `put`) with the operand 5 (7.6, paragraph 9).
8. `console/1`'s `write` hands its bytes to the configuration's `output` callback, the one that print statements reach (7.3), and needs no grant. With `output` NULL it returns `unavailable`, and when the callback returns a status other than `CINT_OK` it returns `closed`; unlike a print statement, a failed `write` does not fault the entry.
9. `read`, `read_line` and `window` need a grant of `console.input` and use the standard input of the process. `read` waits up to `wait_ms` milliseconds for input, or without limit when it is negative, and returns 0 when none arrives; otherwise it returns the bytes that are ready, at most the extent of `buf`. `read_line` reads up to a line feed, which it drops with a carriage return before it, and leaves the rest of a line longer than `buf` for the next call. At the end of the input, `read` returns `closed`, and `read_line` returns the bytes of a last line that has no line feed, or `closed` when there are none. `window` reads the size of a standard stream that is a terminal, and returns `unavailable` when none is.
10. `raw` needs a grant of `console.raw`, and switches the terminal of the standard input on or off raw mode: no echo, no line editing, no signals from keys, no flow control and no conversion of input or output line ends. It returns `unavailable` when the standard input is not a terminal. The terminal belongs to the whole process, so one context at a time holds raw mode, and `raw(g, true)` from another returns `unavailable`. The runtime restores the terminal when the context that holds raw mode turns it off, faults or is destroyed, when the process exits, and when the process ends on a hangup, interrupt, quit or terminate signal whose default action was in place.
11. Every extern of both services is `observe`. A recording holds each call as an effect named by the service, its version and the extern, such as `fs/1.stat` (SPEC-01P.3 8.4).
12. The `main` of a program receives the grants of its run. `cint run` passes its `--grant` lines (SPEC-06P.2 5.4), and so does an executable that `cint build` writes, which joins its `--grant` lines with line feeds into `config.grants` and passes the program's host import descriptor as `config.imports` (SPEC-06P.2 5.5). Such an executable passes its program arguments in `config.args` in the same way, when its `main` takes them. A grant line that the runtime refuses ends either with exit status 4 before any of the program runs.

**Examples**

13. This program asks `fs.stat` about six paths. The directory `data` holds the 12-byte file `greeting.txt`, a symbolic link `link.txt` to it and a directory `sub`:

```cint
// probe.ci: what fs.stat answers for each path main names.
import fs;
requires fs.read("/data");

void probe(Grants g, in U8[_] path) {
    I64 n = fs.stat(g, path) catch (e) {
        if (e == fs.FsError.path) {
            "path\n";
        } else if (e == fs.FsError.denied) {
            "denied\n";
        } else if (e == fs.FsError.not_found) {
            "not_found\n";
        } else if (e == fs.FsError.is_directory) {
            "is_directory\n";
        } else {
            "another error\n";
        }
        return;
    };
    "{n} bytes\n";
}

void main(Grants g) {
    probe(g, "/data/greeting.txt");
    probe(g, "/data/GREETING.TXT");
    probe(g, "/data/link.txt");
    probe(g, "/data/sub");
    probe(g, "/data/../greeting.txt");
    probe(g, "/other/greeting.txt");
}
```

```console
$ stat -c '%n %F' data/*
data/greeting.txt regular file
data/link.txt symbolic link
data/sub directory
$ cint build --out out probe.ci
built: out/probe
$ ./out/probe --grant fs.read:/data=data
12 bytes
not_found
not_found
is_directory
path
denied
```

14. The name in another case does not match on Linux, and would not on a host that matches names without case either. The link is not followed. `..` is not a segment of a logical path, so that path fails before any grant is consulted, and `/other` is under no grant.

<a id="worked-cases-that-run"></a>

## 10 Worked cases that run

<a id="a-tour-of-the-c-interface"></a>

### 10.1 A tour of the C interface

This program uses the library `tour.ci` and finds each wrapper by name in the reflection table. It shows an error result, the output callback and the busy flag, a buffer the runtime creates, a lease, refused calls and entry faults.

```cint
// tour.ci: a library for a tour of the C interface.
error ParseError { empty, bad_digit, too_large }

export ParseError!I64 parse_u(in U8[_] s) {
    if (len(s) == 0) {
        return ParseError.empty;
    }
    I64 v = 0;
    for i in 0..len(s) {
        U8 c = s[i];
        if (c < 48 || c > 57) {
            return ParseError.bad_digit;
        }
        v = v * 10 + ((c - 48) as I64);
    }
    return v;
}

export kernel scale[n](in I64[n] x, in I64 k, out I64[n] y) over [i: n] {
    y[i] = x[i] * k;
}

export I64 greet(I64 times) {
    for i in 0..times {
        "hello {i}\n";
    }
    return times;
}
```

```c
/* tour.c: error results, print output, leases, refused calls and entry faults. */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "cint_rt.h"

extern const cint_library cint_library_desc;

typedef struct { uint16_t tag; int64_t value; } parse_result;   /* ParseError!I64 */
typedef cint_status (*parse_fn)(cint_ctx *, int64_t, cint_view, parse_result *);
typedef cint_status (*scale_fn)(cint_ctx *, int64_t, cint_view, int64_t, cint_view);
typedef cint_status (*greet_fn)(cint_ctx *, int64_t, int64_t, int64_t *);

static const char *const fault_names[] = {"", "E_OVERFLOW", "E_DIV_ZERO", "E_BOUNDS", "E_SHAPE",
    "E_SHIFT", "E_NARROW", "E_ALIAS", "E_STALE_HANDLE", "E_FUEL", "E_UNSUPPORTED", "E_DOMAIN",
    "E_DEPTH", "E_ASSERT"};

/* The wrapper of an export, found by name in the reflection table. */
static cint_entry_fn find(const char *name)
{
    const cint_module_table *t = cint_library_desc.modules[0];
    for (uint32_t k = 0; k < t->export_count; k++)
        if (t->exports[k].name.len == strlen(name) && memcmp(t->exports[k].name.bytes, name, strlen(name)) == 0)
            return t->exports[k].call;
    return NULL;
}

/* Receives the bytes of each print statement. The first time, it also tries to call back
   into the context, which is busy with the entry that printed. */
static cint_ctx *reentry;
static cint_status out(void *user, const uint8_t *bytes, size_t len)
{
    printf("%s%.*s", (const char *)user, (int)len, (const char *)bytes);
    if (reentry != NULL) {
        printf("  (clear_fault from the callback: status %d)\n", cint_ctx_clear_fault(reentry));
        reentry = NULL;
    }
    return CINT_OK;
}

static cint_view view_of(cint_buffer_id id, uint64_t gen, int64_t n, uint8_t perm)
{
    cint_view v;
    memset(&v, 0, sizeof v);
    v.buffer = id;
    v.generation = gen;
    v.type.code = CINT_TAG_I64;
    v.rank = 1;
    v.perm = perm;
    v.shape[0] = n;
    v.stride[0] = 1;
    return v;
}

/* Registers n elements of host memory, with publish none, and returns a view of them. */
static cint_view borrow(cint_ctx *ctx, void *p, uint16_t code, int64_t n, int writable)
{
    cint_buffer_desc d;
    cint_buffer_id id = 0;
    uint64_t gen = 0;
    memset(&d, 0, sizeof d);
    d.size = sizeof d;
    d.writable = writable ? 1u : 0u;
    d.type.code = code;
    d.mem.kind = CINT_MEM_HOST;
    d.mem.u.host.ptr = p;
    d.extent = n;
    cint_buffer_register(ctx, &d, &id, &gen);
    cint_view v = view_of(id, gen, n, writable ? CINT_VIEW_WRITE : CINT_VIEW_READ);
    v.type.code = code;
    return v;
}

/* A tagged I64 or U64 value: its tag byte, then 8 bytes, little-endian. */
static uint64_t bits_of(const cint_tvalue *t)
{
    uint64_t u = 0;
    for (int k = 8; k >= 1; k--) u = (u << 8) | t->bytes[k];
    return u;
}

/* Prints the held fault, then clears it. */
static void show_fault(cint_ctx *ctx)
{
    static cint_fault_record r;
    size_t len = 0;
    cint_ctx_fault(ctx, &r);
    cint_fault_get(ctx, NULL, 0, &len);
    printf("  %s %.*s, operands", fault_names[r.code], (int)r.operation_len, r.operation);
    for (unsigned k = 0; k < r.operand_count; k++) printf(" %" PRIu64, bits_of(&r.operands[k]));
    if (r.has_limit) printf(", limit %" PRIu64, bits_of(&r.limit));
    printf(", %zu canonical bytes\n", len);
    cint_ctx_clear_fault(ctx);
}

int main(void)
{
    const parse_fn parse_u = (parse_fn)find("parse_u");
    const scale_fn scale = (scale_fn)find("scale");
    const greet_fn greet = (greet_fn)find("greet");
    int64_t xs[4] = {1, 2, 3, 4}, zs[4] = {0}, copy[4] = {0}, result = 0;
    cint_ctx_config config;
    cint_ctx *ctx = NULL;
    cint_status s;
    uint32_t why = 0;

    memset(&config, 0, sizeof config);
    config.size = sizeof config;
    config.program = cint_library_desc.modules[0]->info->program;
    config.module = cint_library_desc.modules[0]->info;
    config.output = out;
    config.output_user = "  | ";
    if (cint_ctx_create(&config, &ctx) != CINT_OK) return 1;

    /* 1. An error result completes the entry. */
    const char *texts[2] = {"123", "12x"};
    for (int k = 0; k < 2; k++) {
        parse_result pr;
        const cint_error_set *set = NULL;
        uint64_t tag = 0;
        cint_view v = borrow(ctx, (void *)texts[k], CINT_TAG_U8, 3, 0);
        s = parse_u(ctx, CINT_FUEL_UNBOUNDED, v, &pr);
        cint_ctx_error(ctx, &set, &tag);
        if (set == NULL)
            printf("parse_u(\"%s\"): status %d, value %" PRId64 "\n", texts[k], s, pr.value);
        else
            printf("parse_u(\"%s\"): status %d, tag %u, error %s.%s\n", texts[k], s, pr.tag, set->name,
                   set->values[tag - 1]);
        cint_buffer_release(ctx, v.buffer);
    }

    /* 2. Print statements reach the output callback. */
    reentry = ctx;
    s = greet(ctx, CINT_FUEL_UNBOUNDED, 2, &result);
    printf("greet: status %d, result %" PRId64 "\n", s, result);

    /* 3. A runtime-owned output, read through a lease and by copy. */
    cint_view x = borrow(ctx, xs, CINT_TAG_I64, 4, 0);
    cint_buffer_id yid = 0;
    uint64_t ygen = 0, lease = 0;
    cint_type i64;
    memset(&i64, 0, sizeof i64);
    i64.code = CINT_TAG_I64;
    cint_buffer_create(ctx, i64, 4, 0, &yid, &ygen);
    cint_view y = view_of(yid, ygen, 4, CINT_VIEW_WRITE);
    s = scale(ctx, CINT_FUEL_UNBOUNDED, x, 10, y);
    const void *addr = NULL;
    cint_buffer_lease(ctx, yid, &addr, &lease);
    const int64_t *ys = addr;
    printf("scale: status %d, y = %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64 " (lease %" PRIu64 ")\n", s,
           ys[0], ys[1], ys[2], ys[3], lease);
    printf("scale while leased: status %d\n", scale(ctx, CINT_FUEL_UNBOUNDED, x, 100, y));
    cint_lease_end(ctx, lease);
    s = scale(ctx, CINT_FUEL_UNBOUNDED, x, 100, y);
    cint_buffer_read(ctx, y, copy, sizeof copy);
    printf("after lease_end: status %d, y = %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64 "\n", s, copy[0],
           copy[1], copy[2], copy[3]);

    /* 4. Arguments that cannot be formed are refused. */
    cint_view unknown = x;
    unknown.buffer = 99;
    s = scale(ctx, CINT_FUEL_UNBOUNDED, unknown, 1, y);
    cint_ctx_refusal(ctx, &why);
    printf("unknown buffer: status %d, reason %u\n", s, why);
    cint_view wide = y;
    wide.shape[0] = 5;
    s = scale(ctx, CINT_FUEL_UNBOUNDED, x, 1, wide);
    cint_ctx_refusal(ctx, &why);
    printf("view past the extent: status %d, reason %u\n", s, why);
    cint_view upgraded = x;
    upgraded.perm = CINT_VIEW_WRITE;
    s = scale(ctx, CINT_FUEL_UNBOUNDED, x, 1, upgraded);
    cint_ctx_refusal(ctx, &why);
    printf("write view of read-only memory: status %d, reason %u\n", s, why);
    printf("fuel -2: status %d\n", scale(ctx, -2, x, 1, y));

    /* 5. Arguments that can be formed are checked at entry, and fail as faults. */
    cint_view old = x;
    old.generation = 7;
    printf("wrong generation: status %d\n", scale(ctx, CINT_FUEL_UNBOUNDED, old, 1, y));
    show_fault(ctx);
    printf("read view as output: status %d\n", scale(ctx, CINT_FUEL_UNBOUNDED, x, 1, x));
    show_fault(ctx);
    cint_view short_y = y;
    short_y.shape[0] = 3;
    printf("short output: status %d\n", scale(ctx, CINT_FUEL_UNBOUNDED, x, 1, short_y));
    show_fault(ctx);
    cint_view z = borrow(ctx, zs, CINT_TAG_I64, 4, 1);
    printf("borrowed output, publish none: status %d\n", scale(ctx, CINT_FUEL_UNBOUNDED, x, 1, z));
    show_fault(ctx);

    cint_buffer_release(ctx, x.buffer);
    s = scale(ctx, CINT_FUEL_UNBOUNDED, x, 1, y);
    cint_ctx_refusal(ctx, &why);
    printf("released input: status %d, reason %u\n", s, why);
    cint_buffer_release(ctx, yid);
    cint_ctx_destroy(ctx);
    return 0;
}
```

Built and run, after the `cint build --lib --out out tour.ci` of 7.4:

```console
$ cc -std=c17 -Wall -Wextra -Wpedantic -I "$CINT_RT" tour.c -L out -ltour -Wl,-rpath,'$ORIGIN/out' -o tour
$ ./tour
parse_u("123"): status 0, value 123
parse_u("12x"): status 0, tag 2, error tour.ParseError.bad_digit
  | hello 0
  (clear_fault from the callback: status 3)
  | hello 1
greet: status 0, result 2
scale: status 0, y = 10 20 30 40 (lease 1)
scale while leased: status 3
after lease_end: status 0, y = 100 200 300 400
unknown buffer: status 2, reason 1
view past the extent: status 2, reason 7
write view of read-only memory: status 2, reason 9
fuel -2: status 2
wrong generation: status 1
  E_STALE_HANDLE bind.stale, operands 0 7, limit 1, 155 canonical bytes
read view as output: status 1
  E_ALIAS bind.permission, operands 2, 142 canonical bytes
short output: status 1
  E_SHAPE bind.shape, operands 2 0 3, limit 4, 164 canonical bytes
borrowed output, publish none: status 1
  E_UNSUPPORTED bind.limit, operands 2, 137 canonical bytes
released input: status 2, reason 2
```

1. Both calls of `parse_u` complete with status 0 (7.3 paragraph 3). The second returns `ParseError.bad_digit`: the tag in the result is 2, the second value of the set and `cint_ctx_error` gives the set's qualified name and the value's name. The context is not faulted, and the next call runs.
2. Each print statement of `greet` reaches `out` whole, with the `output_user` pointer of the configuration. The first time, `out` calls `cint_ctx_clear_fault` on the context that is running `greet`, against 7.2 paragraph 2, to show what the runtime does: it answers `CINT_BUSY` (3) and changes nothing, and `greet` goes on to return 2.
3. `y` is storage the runtime created, zero-filled, and `scale` writes it (6.2). The lease gives its address for reading, and lease numbers count from 1. While the lease is live, a call that binds `y` to an output returns `CINT_BUSY` and runs nothing; after `cint_lease_end` the same call runs, and `cint_buffer_read` copies the new contents out (6.3).
4. Five calls are refused, and none of them runs or changes the context (7.1 paragraphs 3 and 10). Buffer 99 was never issued (reason 1). A view of five elements of a four-element buffer reaches outside its registration (reason 7). A write view of `x`, which was registered read-only, is reason 9. A fuel budget of -2 is refused. At the end, after `x` is released, a view with its old identifier is reason 2.
5. Four calls have arguments that can be formed, so the wrapper checks them at entry and faults with no fuel charged (7.1 paragraph 11). A view of `x` with generation 7, against a registration of generation 1, is `bind.stale`, with the parameter index and the generation as operands and the registration's generation as the limit. The read view `x` passed for the output `y` is `bind.permission` for parameter 2. A three-element view for `y`, after `n` took 4 from `x`, is `bind.shape`: parameter 2, dimension 0, extent 3, limit 4. Host memory registered writable with publish mode `none` and bound to an output is `bind.limit` (6.4). After each fault, `show_fault` reads the record, takes the length of its canonical bytes from `cint_fault_get`, and clears it, so the next call runs.

<a id="dispatches-within-one-entry"></a>

### 10.2 Dispatches within one entry

An entry can issue several dispatches, and each publishes its outputs when it completes. This program calls `twice` of `grid.ci` (8.2), a function that issues two dispatches of the kernel `double_it`, the first writing `b` from `a` and the second writing `a` from `b`. It then calls `double_it` directly with two work-items that overflow.

```python
# dispatches.py: two dispatches in one entry, and the fault of the lowest work-item.
import numpy as np
import cint

mod = cint.load("grid.ci")
ctx = mod.context()

a = np.array([1, 2, 3], dtype=np.int64)
b = np.zeros(3, dtype=np.int64)
ctx.twice(a=cint.borrow(a, writable=True), b=cint.borrow(b, writable=True))
print(a.tolist(), b.tolist())

a = np.array([1, 2**61, 3], dtype=np.int64)
b = np.zeros(3, dtype=np.int64)
try:
    ctx.twice(a=cint.borrow(a, writable=True), b=cint.borrow(b, writable=True))
except cint.OverflowFault as f:
    print(f.operation, "dispatch", f.dispatch, "work item", f.work_item)
print(a.tolist(), b.tolist())
ctx.clear_fault()

x = np.zeros(1000, dtype=np.int64)
x[900] = 2**62
x[7] = 2**62
try:
    ctx.double_it(x, y=cint.empty((1000,), "I64"))
except cint.OverflowFault as f:
    print(f.operation, "dispatch", f.dispatch, "work item", f.work_item)
```

```console
$ python3 dispatches.py
[4, 8, 12] [2, 4, 6]
add.checked.i64 dispatch 1 work item 1
[1, 2305843009213693952, 3] [2, 4611686018427387904, 6]
add.checked.i64 dispatch 0 work item 7
```

1. The first call leaves `b` equal to twice the old `a`, and then `a` equal to twice `b`.
2. In the second call, dispatch 0 completes and publishes `b`, whose second element is 2^62. Dispatch 1 faults at work-item 1, where 2^62 + 2^62 is 2^63. Nothing of dispatch 1 is published: `a` holds what it held before the call, and `b` keeps what dispatch 0 published (6.4 paragraph 1).
3. Dispatch numbers count from 0 in each entry. The direct call of `double_it` is a new entry, so its one dispatch is number 0. Work-items 7 and 900 both overflow, and the fault reported is that of work-item 7, the lowest index, on every executor (9.3 paragraph 7).

<a id="the-same-fault-from-cint-code"></a>

### 10.3 The same fault from cint code

A dispatch issued from cint code is checked and faults as the same dispatch issued by a host (7.1 paragraph 3). This script runs the kernel of `advance.ci` twice in one run, the second time with `dt` equal to 2, as `host.c` does in 7.3.

```cint advance_run.ci
kernel advance[n](in I64[n] pos, in I64[n] vel, in I64 dt, out I64[n] next) over [i: n] {
    next[i] = pos[i] + vel[i] * dt;
}

void run() {
    I64[4] pos = [0, 1, 2, 3];
    I64[4] vel = [16, 16, 4611686018427387904, 16];
    I64[4] next = [0, 0, 0, 0];
    advance(pos, vel, 1, next);
    "next: {next[0]} {next[1]} {next[2]} {next[3]}\n";
    advance(pos, vel, 2, next);
    "not reached\n";
}

run();
```

```text
next: 16 17 4611686018427387906 19
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> advance_run.ci:2:31
...
   = operation: mul.checked.i64
   = operand-count: 2
   = operand: I64 4611686018427387904
   = operand: I64 2
   = exact:   9223372036854775808
   = limit:   I64 9223372036854775807
...
   = address: advance_run.advance, dispatch 1, phase 1, work-item 2, step 3
...
```

1. The operation, the operands and the limit are those that `host.c` reports in 7.3: the fault record does not depend on which side issued the dispatch. The address differs in the dispatch number. Here both dispatches belong to one run of the script, so the one that faults is number 1; in 7.3 each call of a wrapper is an entry of its own, and its one dispatch is number 0. Phase 1 is the work-item phase; the entry checks of 7.1 paragraph 11 fault in phase 0.
2. `cint run` prints the fault in the form that SPEC-06P.2 describes, with the source line, the revision identity and the call stack among the lines this example skips.

<a id="future-directions"></a>

## 11 Future directions

This clause describes interfaces that cint 0.2.2 does not provide. It states no requirement.

1. **Grants for a host program's calls.** A wrapper that passes the context's grant set for an export's `Grants` parameter, so that a host program's calls reach the standard services, and host functions that take wide integers and fixed-point values.
2. **Fortran.** `cint build` writes, for each module, a Fortran module of `bind(C)` interfaces and a C shim that implements them, so that a Fortran program calls exports with its own arrays and never handles a view.
3. **The migration assistant.** A set of commands reads a floating-point routine, measures the ranges its variables take on stated inputs, proposes a fixed-point format for each, writes a cint skeleton, and compares the port with the original in a report.
4. **Input hazard checks.** With `check_inputs`, the Python package hashes each borrowed input at entry and again before each publication of a dispatch that reads it, and faults the entry when the two differ; while recording, it snapshots borrowed inputs instead. The check detects a change by a foreign writer that persists to the second hash and no other, so the limit of 8.4 paragraph 3 remains.
5. **Further services.** Services for a clock, randomness drawn from a recorded seed, and the network, each with its capabilities, and the listing and removal of files under `fs`.
6. **Further loading and calling forms.** Loading a module from Python on the interpreter, a host table for the Python package, an in-place form of a kernel call, and libraries built by `cint build --lib` that hold the checkpoint, recording and replay functions are candidates for a future edition.

## Annex A (informative) Clause identifiers stated in other editions

A tool message, or an earlier edition, may cite a clause identifier of this document's series whose rule a 0.2.2 edition states elsewhere. Each such identifier is listed here with the edition and section that state it, or, where no 0.2.2 edition states its rule, with the section that describes the topic.

| Identifier | Stated in |
|---|---|
| <a id="H-14a"></a>H-14a | [SPEC-01P.3 8.2](SPEC-01P.3-ENCODINGS.md#H-14a) |
| <a id="M-12"></a>M-12 | [SPEC-04P.3 6.14.4](SPEC-04P.3-LANGUAGE.md#M-12) |
| <a id="M-13"></a>M-13 | [SPEC-04P.3 6.14.1](SPEC-04P.3-LANGUAGE.md#M-13) |
| <a id="M-14"></a>M-14 | [SPEC-04P.3 6.14.2](SPEC-04P.3-LANGUAGE.md#M-14) |
| <a id="M-14a"></a>M-14a | [SPEC-04P.3 6.14.2](SPEC-04P.3-LANGUAGE.md#M-14a) |
| <a id="M-15"></a>M-15 | [SPEC-04P.3 6.14.3](SPEC-04P.3-LANGUAGE.md#M-15) |
| <a id="M-16"></a>M-16 | [SPEC-04P.3 6.14.3](SPEC-04P.3-LANGUAGE.md#M-16) |
| <a id="M-17"></a>M-17 | [SPEC-04P.3 6.14.4](SPEC-04P.3-LANGUAGE.md#M-17) |
| <a id="M-17a"></a>M-17a | [SPEC-04P.3 6.14.3](SPEC-04P.3-LANGUAGE.md#M-17a) |
| <a id="M-18"></a>M-18 | [SPEC-04P.3 6.14.3](SPEC-04P.3-LANGUAGE.md#M-18) |
| <a id="M-19"></a>M-19 | [SPEC-04P.3 6.14.2](SPEC-04P.3-LANGUAGE.md#M-19) |
| <a id="M-20"></a>M-20 | [SPEC-04P.3 6.14.1](SPEC-04P.3-LANGUAGE.md#M-20) |
| <a id="M-20a"></a>M-20a | [SPEC-04P.3 6.14.1](SPEC-04P.3-LANGUAGE.md#M-20a) |
| <a id="M-20b"></a>M-20b | [SPEC-04P.3 6.14.1](SPEC-04P.3-LANGUAGE.md#M-20b) |
| <a id="M-21"></a>M-21 | [SPEC-04P.3 6.12.4](SPEC-04P.3-LANGUAGE.md#M-21) |
| <a id="M-22"></a>M-22 | [SPEC-04P.3 6.12.4](SPEC-04P.3-LANGUAGE.md#M-22) |
| <a id="M-23"></a>M-23 | [SPEC-04P.3 6.14.5](SPEC-04P.3-LANGUAGE.md#M-23) |
| <a id="M-24"></a>M-24 | [SPEC-04P.3 6.12.1](SPEC-04P.3-LANGUAGE.md#M-24) |
| <a id="M-24a"></a>M-24a | [SPEC-04P.3 6.12.4](SPEC-04P.3-LANGUAGE.md#M-24a) |
| <a id="M-25"></a>M-25 | [SPEC-04P.3 6.12.4](SPEC-04P.3-LANGUAGE.md#M-25) |
| <a id="M-26"></a>M-26 | [SPEC-04P.3 6.13.2](SPEC-04P.3-LANGUAGE.md#M-26) |
| <a id="M-27"></a>M-27 | [SPEC-04P.3 6.12.2](SPEC-04P.3-LANGUAGE.md#M-27) |
| <a id="X-9"></a>X-9 | [Not a rule of this edition; SPEC-03P.2 8.5](SPEC-03P.2-HOST-INTERFACE.md#values-that-cross) |
