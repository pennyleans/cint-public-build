# Tutorial 04: an I8 matrix product from Python

Status: tutorial, box 10 (roadmap column 02), 2026-10-06. Written for a Python and NumPy developer who has read tutorial 01. Every output below is pasted from a run on Linux (Ubuntu 24.04) with the GCC leg (GCC 13.3), CPython 3.11.15, and NumPy 2.4.6. The MSVC leg on Windows and the Apple Clang leg on macOS have not run this script yet.

In this tutorial a Python script multiplies a matrix of `I8` activations by a matrix of ternary weights (-1, 0, or 1) in CINT, checks the product against NumPy, runs a kernel into two kinds of output, and then keeps the same product in `I8`, where one element does not fit. You see which arguments are borrowed, which are copied, and which are neither, and what a fault leaves in each array. This is the first Python demonstration of decision 15 (box 10 design note, BX10-22).

## 1. The program

`examples/matmul_i8.ci`:

```ci
// matmul_i8.ci: I8 activations times ternary weights, called from Python.
// Build and call it with: python3 examples/matmul_i8.py
// Tutorial 04 walks through the script, its copies, and its fault.
// There is no floating point anywhere.

// y = x times w: each element is the exact dot product of a row of x and a
// column of w, formed wider than I8 and checked once as an I32 (SPEC-01
// IM-87). The weights are -1, 0 or 1, stored as I8 elements.
export void matmul[m, k, n](in I8[m, k] x, in I8[k, n] w, inout I32[m, n] y) {
    for i in 0..m {
        for j in 0..n {
            y[i, j] = dot(I32, x[i, ..], w[.., j]);
        }
    }
}

// The same product kept in I8: the first element outside -128 to 127 faults
// E_OVERFLOW, and the elements before it are already written.
export void matmul_narrow[m, k, n](in I8[m, k] x, in I8[k, n] w, inout I8[m, n] y) {
    for i in 0..m {
        for j in 0..n {
            y[i, j] = dot(I8, x[i, ..], w[.., j]);
        }
    }
}

// Negative elements become 0: a kernel, one work-item per element, whose
// output is published only if every work-item succeeds (SPEC-02 P-1).
export kernel relu[m, n](in I32[m, n] y, out I32[m, n] z) over [i: m, j: n] {
    I32 v = y[i, j];
    if (v < 0) {
        v = 0;
    }
    z[i, j] = v;
}
```

- `export` makes a function or kernel callable from a host program; Python sees exactly the exports (BX10-10).
- `[m, k, n]` are shape symbols. They are not arguments: the bridge reads them from the arrays it is given, and when the extents disagree the call faults `E_SHAPE` at entry, before anything runs (SPEC-03 H-12).
- `in` parameters are read-only. A function has no `out` parameter, so `matmul` returns its product through the `inout` array `y` (SPEC-04 LS-119). A kernel may have `out` parameters.
- `x[i, ..]` is row `i` of `x` and `w[.., j]` is column `j` of `w`, both views with no copy. `dot(I32, a, b)` forms the exact sum of the elementwise products, however wide it gets, and checks it once against `I32` (SPEC-01 IM-87). No product or partial sum can overflow on the way.
- `relu` is a kernel: its body runs once for each pair `(i, j)` in `[0, m) x [0, n)`, and the runtime publishes `z` only when every work-item succeeds (SPEC-02 P-1, SPEC-03 M-30).

The weights are ternary values stored one per `I8` element. Packed ternary storage, five trits to a byte (`PT5`), waits for roadmap box 20.

## 2. Run it

The script needs NumPy and a bootstrapped `cint` (tutorial 01), either on `PATH` or named by `CINT_EXE`. Run it from the repository root:

```text
$ PYTHONPATH=python python3 examples/matmul_i8.py
y = x @ w: [[27, -12, -15], [-12, 27, -15], [0, -19, 19], [-75, 300, -225]]
equal to NumPy's int64 product: True
matmul: no copies
relu(y): [[27, 0, 0], [0, 27, 0], [0, 0, 19], [0, 300, 0]]
relu: 1 copy
  staging z: I32 x 12
relu(y) into NumPy: [[27, 0, 0], [0, 27, 0], [0, 0, 19], [0, 300, 0]]
relu: 1 copy
  publish-copy z: I32 x 12
matmul_narrow faulted: E_OVERFLOW dot.checked.i8.i8 at matmul_i8.ci:22:23
E_OVERFLOW at matmul_i8.ci:22:23

operation: dot.checked.i8.i8
operand:   16
result:    300
maximum:   127
y8 after the fault: [[27, -12, -15], [-12, 27, -15], [0, -19, 19], [-75, 0, 0]]
the context holds the fault: True
y after the fault: [[27, -12, -15], [-12, 27, -15], [0, -19, 19], [-75, 300, -225]]
```

`cint.load("examples/matmul_i8.ci")` builds the source into a shared library with `cint build --lib` the first time, in a directory keyed by the source's digest and the toolchain identity, and loads the cached library on later runs (BX10-02). `mod.context()` makes a context: the memory, fuel, and fault record that calls share.

`inputs()` makes the same arrays on every machine, with no randomness. `x` is 4 by 16, with elements from -8 to 8 and the last row multiplied by 15, so that it reaches 120. `w` is 16 by 3, with elements -1, 0, and 1. `x @ w` in `int64` is the reference; the CINT product equals it.

Reported: the run above, on the working tree at `bc9135d`, 2026-10-06; no receipt. The script's stdout is committed as `examples/matmul_i8.stdout`, and `python/tests/test_examples.py` runs the script and compares its stdout with that file byte for byte.

## 3. Copy, no copy, or borrow

Every copy in a call is one the script asks for (SPEC-03 P-9). Storage is one of three kinds (box 10 design note, section 5.1):

- **borrow**: NumPy owns the memory and CINT reads or writes it in place for one call. An array passed bare to an `in` parameter is borrowed read-only (P-20a, BX10-04). An array the entry writes must be wrapped, `cint.borrow(a, writable=True)`, and passed by name (BX10-14).
- **copy**: the data is copied. `cint.copy(a)` copies into a `Buffer`, and a kernel output may be staged and copied once on success.
- **no copy**: a `Buffer` that the bridge owns, made by `cint.empty(shape, elem)` or `cint.copy`, used where it is.

| Line of the script | Argument | Kind | What happens |
| --- | --- | --- | --- |
| `ctx.matmul(x, w, y=cint.borrow(y, writable=True))` | `x`, `w` | borrow | read in place; released when the call returns (P-20a) |
| | `y` | borrow | `matmul` is a function, so it writes `y` in place, with no staging and no copy (M-30b) |
| `z = cint.empty((4, 3), "I32")` | | no copy | a `Buffer` of 12 `I32` elements that the bridge owns (P-17) |
| `ctx.relu(y, z=z)` | `y` | borrow | read in place |
| | `z` | copy, once, on success | a kernel output: `z` is pending during the dispatch and published only if it succeeds (M-30) |
| `ctx.relu(y, z=cint.borrow(z2, writable=True, publish="copy"))` | `z2` | borrow, and one copy on success | the dispatch writes runtime-owned staging, copied into `z2` only on success (P-15, M-30a) |
| `ctx.matmul_narrow(x, w, y=cint.borrow(y8, writable=True))` | `x`, `w`, `y8` | borrow | as for `matmul` |

`ctx.last_entry()` is the copy report of the last call (BX10-17). It names each copy, why it was made, the element type, and the number of elements, never bytes:

- `matmul: no copies`: a function writes its `inout` arrays in place.
- `staging z: I32 x 12`: SPEC-03 publishes a bridge-owned output by renaming, which swaps in new storage and copies nothing (M-31, P-17). The CPU runtime of this box cannot rename storage that the bridge holds, so it uses M-31's fallback, staging and one copy, and the report says so. A runtime that renames would print `relu: no copies` here.
- `publish-copy z: I32 x 12`: the copy the script asked for with `publish="copy"`.

A borrowed array bound to a kernel output without `publish="copy"` is refused at entry. Nothing runs, and the context holds an `E_UNSUPPORTED` fault, operation `bind.limit`, whose operand is the parameter's index (P-15, M-30a). The bridge adds what to do:

```text
z is a borrowed View bound to out I32[n0, n1] z: borrow it with writable=True, publish="copy" to have the output copied in on success, or pass a Buffer from cint.empty (SPEC-03 P-15, M-30a)
```

Staging keeps a half-written kernel output out of your array: when a dispatch faults, nothing it computed is published (SPEC-02 P-1). A function runs in one order, so its stores before a fault are well defined, and it writes in place (M-30b).

## 4. The fault

`matmul_narrow` keeps each element in `I8`. Element `[3, 1]` is the dot product of row 3 of `x` and column 1 of `w`, which is 300, and `I8` holds -128 to 127. The call raises `cint.OverflowFault`, a subclass of `cint.Fault`, which carries the canonical fault record (BX10-13):

- `E_OVERFLOW`: a checked operation had an exact result outside its type's range.
- `dot.checked.i8.i8`: `dot` over `I8` elements, checked against `I8` (SPEC-01 IM-130).
- `operand: 16`: a reduction's one operand is its element count `n` as an `I64`, not the elements (SPEC-01 IM-81). The record stays the same size however long the vectors are.
- `result: 300` and `maximum: 127`: the exact sum, and the bound it crossed, `I8.max`. In the record these are `exact` and `limit`.
- `matmul_i8.ci:22:23`: the `dot` token, the callee name of the reduction (SPEC-01 IM-87).
- The stack is empty: the call came from Python, a host call with no call site (SPEC-01 IM-107). The same call made from a `.ci` function would list that call's position.

NumPy computes the same product in `int8` without a fault: `(x @ w)[3, 1]` is 44 there, 300 minus 256, and `(x @ w)[3, 2]` is 31 (NumPy 2.4.6 on the inputs above). `matmul` has no fault because it keeps the product in `I32`.

### What the fault leaves in each array

| Array | After the fault | Why |
| --- | --- | --- |
| `y8` | rows 0 to 2 and `y8[3, 0]` written, `y8[3, 1]` and `y8[3, 2]` still 0 | a function writes in place, and the stores of completed statements stay (M-30b, SPEC-01 IM-121, IM-122) |
| `x`, `w` | unchanged | `in` parameters are read-only |
| `y`, `z`, `z2` | unchanged | earlier calls are not reverted, and this call did not bind them |

Had a work-item of `relu` faulted, `z` and `z2` would have kept their contents: a dispatch that faults publishes nothing (SPEC-02 P-1, SPEC-03 M-30). `python/tests/test_kernels.py` checks this for a `Buffer` and for an array borrowed with `publish="copy"` (SPEC-03 section 9, case 20).

A fault is sticky (SPEC-03 A-7). Until the script calls `ctx.clear_fault()`, every call on the context raises `cint.Faulted` and runs nothing. The message says what to do:

```text
the context holds E_OVERFLOW, dot.checked.i8.i8 at matmul_i8.ci:22:23 and runs no entry until the fault is cleared (SPEC-03 A-7). It was raised while the entry ran, so the module state is as the fault left it: call ctx.clear_fault() and then an entry that reinitializes that state (A-7b), or make a new context with Module.context().
```

`matmul_i8.ci` has no module state, so `clear_fault()` is enough here. A module with state provides an entry that reinitializes it, and the script calls that entry after `clear_fault()` (A-7b, BX10-05).

### The same fault as the reference interpreter

`python/tests/test_examples.py` writes the same inputs into a `.ci` function `run()` that calls `matmul_narrow`, runs it with `cint_ref`, and compares the two records. The code, operation, operands, exact value, limit, and position are equal. Only the stack differs: under `cint_ref`, the call has a caller, `run()`, so its stack holds one call site.

Reported: `python -m unittest tests.test_examples` from `python/`, on the working tree at `bc9135d`, 2026-10-06, GCC leg; no receipt.

## 5. What this tutorial does not show

- The MSVC and Apple Clang legs. The run above is Linux, GCC.
- Packed ternary storage (`PT5`) and `tdot`, which wait for box 20.
- A GPU. The kernel ran on the CPU backend; the CUDA backend is box 11.
- The in-place form `ctx.<export>.inplace(...)` (P-15), which no runtime provides yet.
- Recording and replay of a call (box 13).
- Nothing here is a performance claim.
