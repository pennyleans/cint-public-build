# GLOSSARY-P.1: Glossary

GLOSSARY-P.1, edition 1 of the glossary of the CINT specification, cint v0.1.0, 2026-10-06.

Status: Draft. This edition describes the terms of the language profile `cint-core-1`, which is not yet frozen, and defines no rule of its own. The text of this edition does not change after its release. Corrections that change no meaning are listed in [ERRATA.md](ERRATA.md); a change of meaning is published as a new edition. Clause identifiers, section numbers, and open-question identifiers are the same in every edition and are never reused. SPEC-02, SPEC-03, and SPEC-05 to SPEC-09 have no public edition yet; their clauses are cited by identifier and are not part of this edition. What a given release of cint implements is stated in that release's notes, not here.

Every defined term and type of the specification that the editions of cint v0.1.0 use ([SPEC-00P.1](SPEC-00P.1-OVERVIEW.md), [SPEC-01P.1](SPEC-01P.1-INTEGER-MACHINE.md), and [SPEC-04P.1](SPEC-04P.1-LANGUAGE-SURFACE.md)), one line each, with the section that defines it. The defining section's text governs; a definition here is a pointer, not a restatement with independent force. A term marked (Proposed) or (Open) has that status in its defining section. Rule identifiers such as `F-1` are section-local: SPEC-02 F-1 (dispatch numbering) and SPEC-03 F-1 (Fortran wrappers) are different rules, so this glossary always names the section.

## 1. Symbols and operators

| Term | Definition | Defined in |
|---|---|---|
| `+` `-` `*` `/` `%` `<<` `>>` | Checked operators: the exact result if in range, otherwise a fault (`E_OVERFLOW`, `E_DIV_ZERO`, or `E_SHIFT`). | SPEC-01 IM-30 |
| `+%` `-%` `*%` `<<%` | Wrapping forms: the exact result reduced modulo `2^w` (balanced residue for ternary types). | SPEC-01 IM-30; SPEC-05 TR-T1-2 |
| `+\|` `-\|` `*\|` | Saturating forms: the exact result clamped to the type's range. | SPEC-01 IM-30 |
| `/` and `%` on integers | Floor division and the remainder with the divisor's sign, as Python `//` and `%`. | SPEC-01 IM-35 |
| `/` on fixed point | Rational division rounded once, half to even by default. | SPEC-01 IM-66 |
| `>>` | Arithmetic (signed) or logical (unsigned) right shift, equal to `floor(a / 2^k)`. | SPEC-01 IM-45 |
| `as` | Checked conversion; `E_NARROW` if the value does not fit the target. | SPEC-01 IM-50 |
| `as%` | Wrapping conversion; a reinterpretation of the bit pattern between equal widths. | SPEC-01 IM-50 |
| `as\|` | Saturating conversion. (Proposed) | SPEC-01 IM-50 |
| `as?` | Result-returning conversion giving `ArithError!T`. | SPEC-01 IM-50, IM-188 |
| `round` clause | A rounding mode written after a conversion target or a fraction literal: `x as I32 round floor`, `0.1 round half_even`. | SPEC-01 IM-69; SPEC-04 LS-20, LS-37 |
| `..` | Half-open range: `0..n` is 0 to n - 1. | SPEC-04 LS-171 |
| `..=` | Inclusive range; the only range form allowed in a `case` label. | SPEC-04 LS-171, LS-177 |
| `by` | Step of a range or array slice; a nonzero compile-time constant. | SPEC-04 LS-161, LS-175 |
| `0t...` | Balanced-ternary literal, most significant digit first, digits `N`, `0`, and `P`. | SPEC-04 LS-30; SPEC-05 TR-LIT-1 |
| `{expr}` hole | Interpolation in a print statement, `format` call, or `assert` message, with optional `=`, `!conv`, and `:spec`. | SPEC-04 LS-198 |
| `@name` | Attribute syntax, for example `@packed`, `@abi(c)`, and `@soa` (Proposed). | SPEC-04 LS-241 |
| `_` | The discard name; also a wildcard extent in a shape (`I64[_]`). | SPEC-04 LS-16, LS-66 |

## 2. Types

| Term | Definition | Defined in |
|---|---|---|
| `I8` `I16` `I32` `I64` | Signed integers of 8, 16, 32, and 64 bits; values are mathematical integers in range. | SPEC-01 IM-4 |
| `U8` `U16` `U32` `U64` | Unsigned integers of 8, 16, 32, and 64 bits. | SPEC-01 IM-4 |
| `I64` (canonical word) | The type of lengths, extents, strides, indices, counts, dispatch numbers, work-item indices, step ordinals, and fuel. | SPEC-01 IM-7 |
| `I128` `I256` `I512` `I1024` | Fixed-capacity signed wide integers with the full operator set, stored as little-endian 64-bit limbs. | SPEC-01 2.4 |
| `U128` `U256` `U512` `U1024` | Unsigned wide integers. (Proposed; adoption is Open) | SPEC-01 IM-6 |
| `Q<i>.<f>` | Signed binary fixed point with `i + f` storage bits (8, 16, 32, or 64), `i` counting the sign bit, value `raw / 2^f`. | SPEC-01 IM-54 |
| `Q16.16`, `Q32.32` | Fixed point with 32-bit and 64-bit signed storage and 16 and 32 fractional bits. | SPEC-01 IM-54 |
| `UQ<i>.<f>` | Unsigned fixed point. (Proposed) | SPEC-01 IM-59 |
| `Bool` | `false` or `true`; not an integer; required for every condition. | SPEC-01 IM-10 |
| `Str` | A read-only view of UTF-8 bytes; string literals have static lifetime. | SPEC-04 LS-40 |
| `T1` | One balanced trit: -1, 0, or +1; stored as one byte. | SPEC-05 TR-T1-1, TR-T1-6 |
| `T27` | Integers in `[-M, M]` with `M = (3^27 - 1) / 2 = 3812798742493`; carried in an `I64`. | SPEC-05 TR-T27-1, TR-T27-4 |
| `PT5[...]` | Packed trit array, five trits per byte as a signed base-243 digit. | SPEC-05 TR-PT5-1 |
| `PT4[...]` | Packed trit array, two bits per trit (`00` 0, `01` +1, `11` -1, `10` invalid). | SPEC-05 TR-PT4-1, TR-PT4-2 |
| Array type `T[e1, ..., er]` | Owned, row-major, zero-filled storage of a fixed shape. | SPEC-04 4.3 |
| `struct` | Aggregate with fields in declaration order at aligned offsets; constructed as `Body(...)`. | SPEC-04 4.4 |
| Bit field | A struct field with a declared bit width; writing an out-of-range value faults `E_NARROW`. | SPEC-04 4.4 |
| Lane (sub-word) | A packed sub-word of an integer, read and written as `x.lanes(T)[k]`; lane 0 is least significant, and the lane width must divide the integer's width. In the specifications "lane" also means a SIMD lane; it never names the integer-only performance path. | SPEC-04 LS-85 |
| `enum` | A named set of values over a declared integer type; not an integer. | SPEC-04 4.5 |
| Error set | `error Name { ... }`: named error values numbered from 1. Its values are ordinary values: they can be stored, compared, and switched on. | SPEC-04 LS-93, LS-314 |
| Combined error set | `error A = B \| C;`: the values of `B`, then those of `C`, numbered by concatenation; `try` converts a member set's tag. | SPEC-04 LS-97 |
| `E!T` (error union) | Either a `T` or a value of error set `E`; valid only as a function result. | SPEC-04 4.6 |
| `ArithError` | The built-in error set `{ overflow, div_zero, shift, narrow }`, numbered 1 to 4, of the result-returning arithmetic forms. | SPEC-01 IM-188 |
| `ArithError!T` | The result type of the result-returning arithmetic forms. | SPEC-01 IM-30, IM-120 |
| `AllocError` | The built-in error set `{ full }` of `alloc_result` and `insert_result`. | SPEC-03 M-19 |
| Tuple type | `(T1, T2, ...)`, valid only as a return type and consumed by destructuring. | SPEC-04 LS-98 |
| Type alias | `type Name = T;`; `distinct` aliases are Proposed. | SPEC-04 4.8 |
| Type signature | The parameter and result types of a function. An `extern` declaration's canonical type signature is hashed and checked when a context is created. Written "type signature," never "signature," which means an Ed25519 signature. | SPEC-03 A-10; SPEC-04 LS-56 |
| `Round` | Built-in enum of the rounding modes. | SPEC-04 LS-147 |
| `Arena(T)` | A context-owned region of one element type with bump allocation, counted in elements and invalidated as a whole by reset; declared at module level with a constant capacity. | SPEC-03 1.3, M-14, M-19, M-20 |
| `Pool(T)` | A context-owned array of fixed-size slots, each with its own generation; declared at module level with a constant capacity. | SPEC-03 1.3, M-15, M-20 |
| `Handle(T)` | A 32-byte canonical, generation-checked reference into an arena or pool. | SPEC-03 M-12 |
| `Z` | The mathematical integers; also the tag of exact values in fault records. | SPEC-01 IM-1, IM-146 |

## 3. Fault codes and statuses

| Term | Definition | Defined in |
|---|---|---|
| Error (recoverable) | A value of an error set, returned in `E!T` and handled with `try` or `catch`. Not a fault. | SPEC-04 LS-93; SPEC-01 IM-120 |
| Fault | An entry-ending stop with a canonical fault record that program code cannot catch, inspect, or resume; the context stays faulted until the host resets it or restores a checkpoint. Not an exception. | SPEC-01 IM-118 |
| `E_OVERFLOW` (1) | Checked arithmetic result, rounded quotient, or final reduction value out of range. | SPEC-01 IM-104 |
| `E_DIV_ZERO` (2) | Division or remainder by zero, in every form. | SPEC-01 IM-104 |
| `E_BOUNDS` (3) | Index outside an extent, or a view's bounding interval outside its buffer; arena or pool capacity exhausted. | SPEC-01 IM-104; SPEC-02 V-6; SPEC-03 M-19 |
| `E_SHAPE` (4) | Shape mismatch, `min` or `max` of an empty input, or a dispatch above `M64` work-items. | SPEC-01 IM-104; SPEC-02 K-6 |
| `E_SHIFT` (5) | Shift, rotation, rescale, or digit position outside the operation's declared range. | SPEC-01 IM-104; SPEC-05 TR-DIG-6 |
| `E_NARROW` (6) | Checked conversion out of range; invalid packed or carrier bytes; a refused host float. | SPEC-01 IM-104; SPEC-05 TR-VAL-2 |
| `E_ALIAS` (7) | A writable output overlaps another operand, or a write view is not provably injective. | SPEC-01 IM-104; SPEC-02 A-2 to A-8 |
| `E_STALE_HANDLE` (8) | A handle or view whose arena, pool slot, or buffer generation has ended, or a torn buffer. | SPEC-01 IM-104; SPEC-03 M-17; SPEC-02 P-10 |
| `E_FUEL` (9) | A fuel charge would exceed the entry's allowance. | SPEC-01 IM-104, IM-141 |
| `E_UNSUPPORTED` (10) | A backend cannot implement a type, operation, layout, or call depth exactly; never a type error. | SPEC-01 IM-104, IM-129 |
| `E_DOMAIN` (11) | `isqrt`, `isqrt_round`, or fixed-point `sqrt` of a negative value. | SPEC-01 IM-104 |
| `E_DEPTH` (12) | A user-function call would exceed the entry's call-depth limit. | SPEC-01 IM-104, IM-119 |
| `E_ASSERT` (13) | A failed `assert` outside tests. | SPEC-01 IM-104; SPEC-04 8.9 |
| Sticky fault | The fault record persists and the context refuses entries until the host program resets it or restores a checkpoint. | SPEC-01 IM-118 |
| Fault record | The canonical record: code, operation, operands, exact, limit, position, revision, address, and stack. | SPEC-01 IM-106, IM-149 |
| Redacted operand | A `secret` value replaced by its type descriptor (tag `7f`) in a fault record. | SPEC-01 IM-109 |
| `CINT_OK` (0) | Entry completed; every dispatch's outputs published. | SPEC-03 A-6 |
| Boundary fault record | The record a faulted context holds after `CINT_RESOURCE`, `CINT_HOST_ERROR`, or `CINT_HAZARD`. (Proposed encoding) | SPEC-03 A-6 |
| Refusal | The host boundary rejects a call before an entry or registration begins (`CINT_REFUSED`): no fuel, no dispatch number, no fault, state unchanged. It may name an `E_` code as its reason (SPEC-01 IM-71). Write "refuse" only in this sense and for the loader's admission statuses; otherwise name the outcome. | SPEC-03 H-12 |

## 4. Numeric operations and built-ins

| Term | Definition | Defined in |
|---|---|---|
| Form (operator form) | Checked, wrapping, saturating, or result-returning; chosen by spelling, never by a mode or flag. | SPEC-01 IM-28 |
| Result-returning form | `add_result`, `sub_result`, `mul_result`, `div_result`, `rem_result`, `shl_result`, and `as?`, returning `ArithError!T`. | SPEC-01 IM-30, IM-188 |
| `wrap(T, v)` | The unique in-range residue of `v` modulo `2^w(T)`. | SPEC-01 IM-1 |
| `sat(T, v)` | `v` clamped to `[MIN(T), MAX(T)]`. | SPEC-01 IM-1 |
| `abs`, `uabs` | Checked absolute value; `uabs` returns the unsigned type of the same width and never faults. | SPEC-01 IM-34 |
| `min`, `max`, `clamp` | Scalar minimum, maximum, and clamp; never fault for ordered bounds. | SPEC-01 IM-49 |
| `div_trunc`, `rem_trunc` | Truncating division and remainder, equal to C `/` and `%` and Fortran `MOD`. | SPEC-01 IM-36 |
| `div_euclid`, `rem_euclid` | Euclidean division with a remainder in `[0, abs(b))`. | SPEC-01 IM-36 |
| `divmod` | Floor quotient and remainder as a pair. | SPEC-01 IM-36 |
| `div_round(a, b, mode)` | The rational `a / b` rounded once by `mode`, then range-checked. | SPEC-01 IM-44 |
| Rounding mode | One of `floor`, `ceil`, `trunc`, `away`, `half_even`, `half_away`, `half_trunc`, `half_up`, and `half_down`; `half_even` is the default where a mode may be omitted. | SPEC-01 IM-42 |
| `mul_full(a, b)` | The exact product in the type of double width. | SPEC-01 IM-51 |
| `muldiv(T, a, b, c, mode)` | `a * b / c` with an exact double-width product, rounded once, result type `T`. | SPEC-01 IM-51 |
| `isqrt`, `isqrt_round` | Integer square root, floor or rounded by a mode. | SPEC-01 IM-51 |
| `mul`, `div`, `mul_wrap`, `mul_sat` (fixed point) | Fixed-point multiply or divide with a named rounding mode and form. | SPEC-01 IM-62, IM-66 |
| `sqrt(a, mode)` | Fixed-point square root with a `2w` intermediate. | SPEC-01 IM-67 |
| `x.raw`, `Q.raw(n)` | The storage integer of a fixed-point value, and construction from one. | SPEC-01 IM-57 |
| `rotl`, `rotr` | Bit rotation of unsigned values. | SPEC-01 IM-45 |
| `sum(xs)`, `sum(R, xs)` | Exact sum in Z with one final range check against `R`; order-independent. | SPEC-01 IM-77 |
| `fold_checked(op, init, xs)` | Left fold in logical order with every step checked; faults at the first out-of-range prefix. | SPEC-01 IM-77 |
| `sum_wrap(xs)` | Sum modulo `2^w`; order-independent. | SPEC-01 IM-77 |
| `sum_sat(xs)` | Left-to-right saturating fold; never reassociated. | SPEC-01 IM-77 |
| `min(xs)`, `max(xs)`, `count(mask)` | Reductions to the least element, the greatest element, and the number of `true` elements. | SPEC-01 IM-77 |
| `dot(R, a, b)` | Exact dot product with one final range check. (Proposed) | SPEC-01 IM-87 |
| Accumulator-width bound | `w + ceil(log2 n)` bits suffice for a sum of `n` values of `w` bits. | SPEC-01 IM-80 |
| Logical order | Row-major order of logical indices, independent of memory layout. | SPEC-01 IM-74 |
| `len`, `extent`, `size`, `lower` | Length, per-dimension extent, element count, and declared lower bound of an array or view. | SPEC-04 LS-145 |
| `copy`, `fill`, `equal` | Element copy between disjoint or identical views, fill, and element-wise equality. | SPEC-04 LS-145 |
| `swap(a, b)` | Exchange the contents of two owned arrays of identical type, by exchanging storage. | SPEC-04 LS-145 |
| `to_device`, `to_host` | Explicit placement transfers that produce new buffers. | SPEC-02 V-12 |
| `format(buf, "...")`, `utf8(bytes)` | Formatting into a caller buffer, and UTF-8 validation of bytes. | SPEC-04 LS-216, LS-145 |
| `embed("path")` | A compile-time constant holding a declared file's bytes. (Proposed) | SPEC-04 LS-269 |
| `wrap_bits(v, w)` | Wrap a value into a `w`-bit signed range. (Proposed) | SPEC-04 LS-83 |
| `random(seed, tick, entity, purpose, draw)` | Stateless counter-based generator (Threefry-4x64-20). Its `seed` argument is the random seed. | SPEC-02 N-1 to N-4 |
| `trit(x, i)` | Trit `i` of the balanced representation of `x`. | SPEC-05 TR-DIG-2 |
| `shl3(x, k)` | `x * 3^k`, checked. | SPEC-05 TR-DIG-3 |
| `rescale3(x, k)` | Delete the `k` low balanced trits: `x / 3^k` rounded to nearest, never a tie; distinct from integer division. | SPEC-05 TR-DIG-4 |
| `rescale3_rem(x, k)` | The deleted part `x - rescale3(x, k) * 3^k`. | SPEC-05 TR-DIG-5 |
| `sign(x)` | -1, 0, or +1 as `T1`. | SPEC-05 TR-T1-4 |
| `tnot`, `tand`, `tor` | Kleene three-valued logic on `T1`. (Proposed) | SPEC-05 TR-T1-3 |
| `pack5`, `pack4`, `unpack`, `repack4`, `repack5` | Conversions between `T1[n]` and packed trit arrays. | SPEC-05 section 6 |
| `encode`, `decode_pt5`, `decode_pt4` | Canonical byte export of packed arrays, and validated import from `U8` data. | SPEC-05 section 6 |
| `trits(x, n)`, `from_trits(A, t)` | Conversion between an integer and its `n` balanced trits. | SPEC-05 section 6 |
| `tdot(A, w, x)` | Exact ternary dot product with a declared accumulator type `A` and one final range check. | SPEC-05 TR-DOT-1 |
| Trit capacity `K` | The least `n` with `R(n)` at least a type's largest magnitude (41 for `I64`). | SPEC-05 section 2 |
| Theorem T1 (`tdot`) | `S` fits `A` for every input exactly when `n * m <= A_max`. | SPEC-05 7.2 |

## 5. Language surface

| Term | Definition | Defined in |
|---|---|---|
| `.ci` | Source file extension of `cint-core-1`; one file is one module. | SPEC-04 LS-3 |
| Module | One `.ci` source file. Compiled code is a program or library, and compiled GPU code is a kernel binary; a Fortran module is always written "Fortran module." | SPEC-04 LS-3, 10 |
| Literal | An integer, character, fraction, or ternary literal: an exact value with no type until its context gives one. | SPEC-01 IM-20, IM-21; SPEC-04 LS-53 |
| Negative literal | A prefix `-` in operand position followed by a numeric or character literal, whatever whitespace or comments separate them, forming one literal (`-128` and `- 128` in `I8`); `-(128)` is a negation. | SPEC-01 IM-25; SPEC-04 LS-28 |
| Context typing | The rules by which a literal receives its type (declaration, parameter, return, other operand, element, case, built-in unification, otherwise `I64`). | SPEC-01 IM-21; SPEC-04 LS-56 |
| Constant expression | An expression of literals, typed constants, operators, and named operations, typed first and evaluated at compile time with run-time semantics. | SPEC-01 IM-23, IM-103 |
| `const` | A named constant with a declared type; there are no untyped named constants. | SPEC-01 IM-24; SPEC-04 LS-111 |
| `var` | Declaration that takes the initializer's type. (Proposed) | SPEC-04 LS-105 |
| View variable | A local declared with `in` (read-only) or `inout` (writable) that aliases existing storage. | SPEC-04 LS-106 |
| Second-class view | A view may be a local, parameter, or argument, never stored or returned unless derived from a parameter. | SPEC-03 M-21; SPEC-04 LS-106 |
| Array slice | A view of a sub-range of an array, such as `a[lo..hi]`, `a[lo..=hi]`, or `a[lo..hi by 2]`. It has the permission of the expression it is taken from and, in each dimension, the parent's declared lower bound. | SPEC-04 4.3, LS-161 |
| Size parameter | A bracketed `I64` bound from argument shapes: `I64 dot[n](in I64[n] a, ...)`. | SPEC-04 LS-117 |
| Parameter mode | `in` (default, read-only), `inout` (writable), or `out` (kernels and `extern` declarations only). | SPEC-04 LS-119; SPEC-02 K-2 |
| `try`, `catch`, `_ =` | The three ways to consume an error union. | SPEC-04 6.5 |
| `defer` | Run a statement when the block exits normally; deferred statements do not run after a fault. | SPEC-04 8.7 |
| `errdefer` | Run a statement when the function exits by returning an error; admitted only where an error can leave, and never run after a fault. | SPEC-04 LS-189, LS-322 |
| `switch` | Exhaustive, no implicit fallthrough, inclusive case ranges, and `fallthrough;` written explicitly. | SPEC-04 8.5 |
| Print statement | A statement of string literals that writes exact formatted bytes, for example `"x={x}\n";`. | SPEC-04 LS-193 |
| Script | A module with top-level statements, run like a notebook cell; cannot be imported. | SPEC-04 LS-218 |
| Importable module | A module of declarations, imports, tests, and `static_assert`, with no top-level statements; it can be imported. Not a library: a library is a loadable build (section 15). | SPEC-04 LS-220 |
| `import`, `export` | Module import by path, and visibility to importers. | SPEC-04 11 |
| Test block | `test "name" { ... }`, run by `cint test` in a fresh context. | SPEC-04 12 |
| `expect_fault` | Test-header clause requiring the body to end with a given canonical fault code. | SPEC-04 LS-236 |
| `assert`, `static_assert` | Run-time and compile-time assertions; a failing comparison reports both operands exactly. | SPEC-04 LS-191 |
| Compile-time execution | Evaluation in constants, extents, case items, `static_assert`, enum values, and attributes, with the same typed core and no clock, environment, or undeclared file. | SPEC-04 14; SPEC-01 IM-103 |
| Frame arena | Storage for local arrays of sequential code, counted in elements; its capacity is a semantic input, 2,097,152 elements by default. | SPEC-04 LS-110; SPEC-06 1.4 |
| `extern` | Declaration of a host function with an effect class. | SPEC-03 H-1 |
| `secret` | Type qualifier marking key material for constant-time rules and redaction. (Proposed) | SPEC-07 SEC-CT-1 |
| C code | Numeric compile diagnostic code (C1xxx lexical to C9xxx limits). | SPEC-04 LS-283 |
| `CINT-FAULT-1`, `CINT-DIAG-1` | Machine-readable JSON forms of faults and diagnostics. | SPEC-06 8.4, 15; SPEC-04 LS-289 |
| Suggested fix | An explicit source edit with a stated meaning; never a wrapping or saturating replacement of a checked operation. | SPEC-04 LS-292 |

## 6. Views, kernels, and dispatch

| Term | Definition | Defined in |
|---|---|---|
| Buffer | A registered region of elements of one type and placement, with an identifier and generation. | SPEC-02 2; SPEC-03 1.3 |
| View | A descriptor of typed, shaped access to a buffer: buffer and generation, element type, rank, shape, lower bounds, view origin, signed strides, permission, and placement. | SPEC-02 V-1 |
| View origin | The element offset field of a view. "Origin" alone is acceptable inside view and kernel sections; elsewhere, "origin" is the plain-language word for build identity plus signer. | SPEC-02 V-1 |
| Bounding interval | The smallest closed interval of element offsets that contains a view's addressed storage; both ends are addressed. The test T1 compares byte bounding intervals. | SPEC-02 V-5, A-5 |
| Lower bound | A declared first index of a dimension (`I64[1..=n]`); changes only the index-to-offset map. | SPEC-02 V-7 to V-9 |
| Permission | `read` or `write`; never stronger than the buffer's ceiling. | SPEC-02 V-1; SPEC-03 M-27 |
| Placement | `host` or `device(k)`; runtime representation only, never canonical. | SPEC-02 V-12, V-13 |
| Maximum rank | 4 in `cint-core-1` (`CINT_MAX_RANK`). (Proposed) | SPEC-02 V-2 |
| `cint_view` | The single C ABI view descriptor, 144 bytes. | SPEC-03 5.3 |
| Kernel | A function written for one work-item and dispatched over an iteration space. "Kernel" alone always means a CINT array kernel; the kernel of the host operating system is the operating system kernel. | SPEC-02 K-1 |
| Iteration space | The shape given by a kernel's `over` clause; one work-item runs at each of its points. | SPEC-02 2, K-8 |
| Shape symbol | A bracketed `I64` bound at dispatch entry from argument extents. | SPEC-02 K-1, K-5, K-6 |
| `over` clause | The iteration space of a kernel, `over [y: h, x: w]`. | SPEC-02 K-8 |
| `where` clause | Requirements on shape symbols, checked at dispatch entry. | SPEC-02 K-1 |
| Element form | A kernel body of whole-array elementwise assignments, defined as the indexed form. | SPEC-02 K-12 |
| Work-item | One point of the iteration space. | SPEC-02 2 |
| Work-item index | The row-major linear `I64` position of a work-item, ignoring lower bounds. | SPEC-02 K-9; SPEC-01 IM-112 |
| Owned block | The elements of an `out` or `inout` array that one work-item may write: a write permission within one dispatch, not ownership. | SPEC-02 K-8, K-10 |
| Coverage pattern | One of the two forms by which the compiler proves every owned element of an `out` is written. | SPEC-02 K-10a |
| Kernel-admissible function | A function with no mutable global state, I/O, printing, allocation, dispatch, or recursion. | SPEC-02 K-11 |
| Uniform, varying | Whether an expression has the same value for every work-item of a dispatch. | SPEC-02 X-3 |
| `reduce` statement | `reduce target = op(expr);`: a work-item's contribution to a named reduction into a scalar `out`. | SPEC-02 R-1 |
| Dispatch | One kernel invocation with bound arguments within an entry; a statement in `.ci`. | SPEC-02 2, K-14 |
| Dispatch number | The 0-based position of a dispatch within its entry; per entry, not state. | SPEC-02 F-1; SPEC-01 IM-112 |
| Staged dispatch | The default: outputs computed into staging and published only on success. | SPEC-02 P-1 |
| Publish | Make all of a dispatch's outputs visible at once, without changing any buffer identity or generation. | SPEC-02 P-1, P-5 |
| Staging | Storage holding a dispatch's outputs before publication; not observable. | SPEC-02 2 |
| Swap, Copy-out, Direct write | The publication strategies: storage replacement, copy from staging, and in-place writes. | SPEC-02 P-5 |
| `in_place` | The explicitly named in-place dispatch of `inout` parameters. | SPEC-02 P-6 |
| Aliasing rule | Read-only inputs may overlap; every writable output's addressed bytes are disjoint from those of every other array argument, established conservatively by T0 to T4. | SPEC-02 A-1, A-2, A-3 |
| Tests T0 to T4 | The ordered pairwise disjointness tests (empty, byte bounding interval, stride gcd, same view, and same element). Each is sufficient: a pair classified disjoint has disjoint addressed bytes. | SPEC-02 A-5 |
| Injectivity test | The sufficient test that a write view maps distinct indices to distinct offsets. | SPEC-02 A-7 |
| Entry check | One of the ordered checks a dispatch performs before any work-item runs (checks 1 to 9, 3a, and 7a). | SPEC-02 F-5 |
| Phase | `entry`, `work-item`, or `epilogue`: the first key of the canonical fault order within a dispatch. | SPEC-02 F-3; SPEC-01 IM-112 |
| Epilogue | The reduction step after all work-items, where `sum` narrowing and empty `min` or `max` fault. | SPEC-02 R-7 |
| Canonical result | Of a dispatch: on success, the output bytes in canonical serialization, the reduction results, the fuel consumed, and the dispatch counter; on a fault, the canonical fault record, the fuel consumed, and the dispatch counter. | SPEC-02 2 |
| Schedule | A separate declaration of how a kernel runs (tile, vectorize, parallel, unroll, workgroup, device); never changes results. | SPEC-02 H-1 to H-5 |
| Requirement, request | Schedule directives that must be met (or fail), and that a backend may decline and report. | SPEC-02 H-3, H-4 |
| Backend | An implementation strategy for SIR. | SPEC-02 2; SPEC-09 8 |
| Capability profile | The record of what a backend admits and refuses; names `ref-c`, `simd-c`, `wgsl-32`, `wgsl-mw64`, `vk-64`, `vk-mw64`, `cuda`, and `metal`. | SPEC-02 B-4, 12.3 |
| Backend identifier | The executable backends `cpu-c17`, `cpu-simd-*`, `cpu-sir-interp`, `gpu-wgsl-32`, `gpu-spirv-vk`, `gpu-cuda`, and `gpu-metal`; mapped to capability profiles in SPEC-06 1.4. | SPEC-09 8.2 |
| Reference | An implementation written to be compared against, not an authority: the specification text governs, and a disagreement is a defect in the reference or an Open ambiguity in the text. The references are `cint_ref`, `cpu-c17` with capability profile `ref-c`, and the hand-derived anchors. | SPEC-09 REF-01 to REF-04, BACK-03, CONF-02; SPEC-02 B-5 |
| Oracle | For one named check, what it compares against: the frozen `.expect` file, `cpu-c17` (`ref-c`) for backend admission, or `cint_ref` when an expectation is generated. Each receipt names its oracle. | SPEC-02 2, B-5 |
| Multiword lowering | Exact implementation of `I64` as two 32-bit words on a backend without 64-bit integers. | SPEC-02 B-10 |
| Range-proven lowering | Evaluating an `I64` expression in a narrower type under an entry-checked bound that proves every value fits. | SPEC-02 B-18; SPEC-01 IM-8 |
| Never silently narrow | No backend may compute in fewer bits, substitute wrapping, or drop a check; it refuses instead. | SPEC-02 B-1; SPEC-01 IM-129 |

## 7. Integer machine contracts

| Term | Definition | Defined in |
|---|---|---|
| Entry | One host call into a context; carries its own fuel allowance, depth limit, and dispatch counter. | SPEC-01 IM-117; SPEC-03 1.3 |
| Context | One instance of a loaded program's or library's state, persistent across entries until it is destroyed; the unit of isolation, checkpointing, and replay. Its fault record stays across entries until the host clears it. | SPEC-03 1.3; SPEC-01 IM-118 |
| Evaluation order | Operands left to right, each completely, places before right sides; the first fault in this order is the fault. | SPEC-01 8.1, IM-98 |
| Counted operation | An operation that receives a step ordinal: every arithmetic operator form, conversion, subscript, view-forming operation, named-operation call, and user-function call. | SPEC-01 IM-113 |
| Step ordinal | The 0-based position of the faulting counted operation within its work-item. | SPEC-01 IM-113; SPEC-02 F-6 |
| Logical address | (dispatch number, work-item index, step ordinal), with the phase within a dispatch. | SPEC-01 IM-106, IM-112 |
| Canonical fault | The least fault under (dispatch number, phase, work-item index, step ordinal); a specification choice, not a hardware observation. | SPEC-01 9.3; SPEC-02 F-3 |
| Operation identifier | The lowercase name of a faulting operation, for example `add.checked.i64` and `mul.checked.q16_16.half_even`. | SPEC-01 IM-130 |
| Fuel | An execution budget in `fuel-v1` units: 1 per user-function call, 1 per loop iteration, ceil(n / 64) per reduction over n elements, a Proposed per-dispatch charge, and 0 for scalar operations. Not elapsed time and not a count of physical instructions: an entry's fuel consumed is the same on every conforming implementation and at every optimization level. | SPEC-01 IM-137, IM-139 |
| `fuel-v1` | The fuel model of `cint-core-1`. | SPEC-01 IM-139 |
| Charge point | A call entry, a loop-iteration start, a reduction, or a kernel dispatch. | SPEC-01 IM-139 |
| Fuel allowance (budget) | The per-entry `I64` limit `B >= 0`; `-1` at the ABI means unbounded. | SPEC-01 IM-137; SPEC-03 H-11 |
| Depth limit | The per-entry maximum number of active calls `D`; Proposed default 256. | SPEC-01 IM-119 |
| Semantic input | A value that is part of an execution identity: the profile, fuel model, revision, source-map digest, entry identifier, arguments hash, input state hash, supplied-effects hash, fuel allowance, depth limit, and frame-arena capacity. | SPEC-01 IM-159; SPEC-04 LS-1 |
| State after a fault | Completed stores remain; the faulting statement's store does not occur; kernel outputs stay unpublished. | SPEC-01 IM-121 |
| Canonical | Designated as the one representation, or the one selection, by a named and versioned rule (a domain string such as `cint-core-1/state/v1`, or the order of SPEC-01 IM-112). It says which form is compared, not that the value is correct: two implementations can produce identical canonical bytes and both be wrong. | SPEC-01 11.1, IM-152 |
| Canonical serialization | Little-endian tagged encoding of values, fault records, and state. | SPEC-01 11.1 |
| Canonical bytes | The bytes the canonical serialization produces for a value, fault record, or state. They contain no addresses, padding, placement, or host paths, and they are the only bytes hashed or compared across implementations. | SPEC-01 11.1 to 11.3 |
| Type tag | One byte identifying a type in canonical bytes (`14` for `I64`, `31` fixed point, `61` view identity, `7f` redacted). | SPEC-01 IM-146 |
| View identity | The tag `61` descriptor of a view in a fault record, without placement or contents. | SPEC-01 IM-146 |
| Domain string | Length-prefixed `cint-core-1/<kind>/v<n>` at the start of every serialized object and hash input. | SPEC-01 IM-152 |
| Canonical state | Logical values only: globals, arenas, pools, registry, fault record, and effect-log position; no addresses, padding, placement, or per-entry quantities. | SPEC-01 11.3; SPEC-03 M-3 |
| State hash | SHA-256 of the canonical state bytes. | SPEC-01 IM-154; SPEC-03 M-5 |
| Revision | One version of a module's code, identified by its revision identity; the source bytes are retained alongside it. | SPEC-06 1.3 |
| Revision identity | SHA-256 over `cint-core-1/revision/v1`, the language profile, the runtime contract version, the module count, and each module's path and canonical SIR, sorted by path bytes and length-prefixed; identifies meaning under one runtime contract, not an executable. An x86-64, an AArch64, and a GPU build of one program built against the same runtime contract version share it; rebuilding an unchanged program against a new runtime contract version changes it. Whether SIR includes source positions: no, under SPEC-09 SIR-17 (Proposed; SPEC-09 Q14). | SPEC-01 IM-157; SPEC-09 SIR-17; SPEC-03 H-15 |
| Execution identity | The input tuple of an entry: profile, fuel model, revision, source-map digest, entry identifier, arguments hash, input state hash, supplied-effects hash, fuel allowance, depth limit, and frame-arena capacity. | SPEC-01 IM-159 |
| Execution agreement | Two executions with equal execution identity produce equal output state hashes, canonical return bytes or absence, canonical fault record bytes or absence, fuel consumed, and effect-request traces. | SPEC-01 IM-159, IM-160 |
| Supplied-effects hash | SHA-256 over what the host supplied to an entry, in ordinal order: each effect's status, canonical result, and contents written to `out` views. It excludes the requests the entry sends out and the input contents, which the arguments hash covers. | SPEC-01 IM-160; SPEC-03 H-5, H-14 |
| Effect-request trace | The ordered sequence of effect requests an entry issued, each with its ordinal, call site, function or service name, and canonical arguments, plus the request bytes of an output effect, up to the entry's end or fault. Equal traces give byte-equal stdout. | SPEC-01 IM-159; SPEC-03 H-7 |
| Deterministic | The same specified inputs (an execution identity) produce the same specified observable outcomes (execution agreement). Host time, scheduling, native addresses, and unrecorded host effects are not inputs. A deterministic result can still be numerically inadequate. The documents use the word only in this sense. | SPEC-01 IM-159, IM-160 |
| Exact | Equal to the mathematical value, with no rounding or truncation. Always say what is exact (an integer operand, a rational intermediate, a sum in Z, a byte equality); exact does not mean correct. | SPEC-01 IM-106 |
| `exact` (fault record field) | The value the range check rejected, as Z; for an operation that rounds once (`muldiv`, `div_round`, fixed-point `*`, `/`, and `sqrt`, a `round` clause, a host-float conversion), the rounded integer, not the rational. The name is historical. | SPEC-01 IM-106, IM-110 |
| Build identity | The complete inputs of a build: sources and their module-relative names, compiler source identity, language profile, backend and its options, and declared inputs. The same build identity produces byte-identical generated source; executables built from it by a C toolchain are observations, not identities. The build record carries it as the source, toolchain, and inputs digests. Two builds that differ only in target or schedule have different build identities and the same revision identity. | SPEC-09 RCPT-01; SPEC-07 9.1; SPEC-02 H-5 |
| Source map | The table from each site number to module, line, and column; its hash is the source-map identity. | SPEC-06 1.3; SPEC-09 SIR-04 |
| CIF-1 | JSON Lines fixture format for single operations. (Proposed format) | SPEC-01 IM-168 |
| `.expect` | Canonical text fixture for whole programs; its grammar is an ABNF. | SPEC-09 CONF-01, CONF-11 |
| Clause identifier | The stable name of one Specified or Proposed rule: `IM-<n>` in SPEC-01, `LS-<n>` in SPEC-04, `<AREA>-<NN>` elsewhere; assigned once and never reused. Conformance cases cite rules by it. | SPEC-01 1.1; SPEC-04 LS-2; SPEC-09 1.1 |
| As-if representation | Holding an `I64` value in fewer bits when range is proven and nothing observable differs. | SPEC-01 IM-8 |

## 8. Memory and host boundary

| Term | Definition | Defined in |
|---|---|---|
| Host program | The program that embeds CINT and supplies externs, for example a Python process, a Fortran or C application, or the HLA gateway. | SPEC-03 |
| Host operating system | The operating system on which CINT code and its host program run. | SPEC-07 1.2 |
| Buffer registry | The per-context table from buffer identifier to location, extent, type, placement, ownership, and generation. | SPEC-03 M-10 |
| Non-context-owned buffer | A borrowed buffer or a bridge-owned buffer. Its contents are not canonical state; they enter the replay contract through the recording (H-14). | SPEC-03 1.3 |
| Borrow | Permission for CINT to read, or write, host-owned memory without copying it, checked at registration and at every entry. | SPEC-03 1.3, P-1 |
| Zero-copy | A named operation at a named boundary moves no element bytes into new storage: registration by `cint.borrow` (SPEC-03 P-1, P-9), or an export lease (A-9). Other operations on the same data may copy, and each copy is in the entry's copy report. | SPEC-03 P-9, P-15, P-16 |
| Borrowed buffer | Host memory used without copying under a checked contract. | SPEC-03 1.3, P-1 |
| Bridge-owned buffer | Memory the Python bridge allocated (`cint.copy`, `cint.empty`). | SPEC-03 1.3 |
| Generation | A counter whose change invalidates handles and views; never wraps and never changes on publication. | SPEC-03 M-14 to M-16 |
| Child arena | An arena carved from a parent; resetting the parent retires it. | SPEC-03 M-20 |
| Effect | One call from CINT code to a host function. | SPEC-03 1.3 |
| Effect class | `pure` (re-executed on replay), `observe` (recorded, replayed from the log), or `unrestricted` (replay stops before it). | SPEC-03 H-1 |
| Effect log | The ordered record of effects consumed and produced by an execution: the event stream of a recording. | SPEC-06 1.3; SPEC-03 H-14 |
| Recording | Header, initial state, and event stream from which replay reproduces a session. | SPEC-03 H-14 |
| Hazard | A borrowed input changed by another alias while an entry ran. | SPEC-03 P-16 |
| Checkpoint | A non-hashed envelope plus canonical state bytes; restoring into a different revision needs a declared migration. | SPEC-03 H-19 |
| Runtime contract version | `cint-rt-<N>`, naming the ABI and semantics of the runtime that emitted code depends on. | SPEC-09 RCPT-08 |
| `cint.borrow` | Borrow of a Python array whose registration copies none of its elements; refused rather than copied. | SPEC-03 6.1, P-9 |
| `cint.copy`, `cint.empty` | Explicit copy into bridge-owned memory, and an unpublished output buffer. | SPEC-03 6.1 |
| Float boundary conversion | Explicit conversion of a binary32 or binary64 value with a named rounding mode and overflow policy; non-finite values refused. | SPEC-03 X-1 to X-8; SPEC-01 IM-70 |
| Fortran wrapper | Generated `bind(C)` interface and C shim over `ISO_Fortran_binding.h` descriptors. | SPEC-03 F-1 to F-5 |
| Migration assistant | `cint migrate`: scan, measure observed ranges, propose, port, and compare a floating-point routine. (Proposed command names) | SPEC-03 F-10 to F-13 |

## 9. Workbench and tooling

| Term | Definition | Defined in |
|---|---|---|
| `cint` | The single command: run, build, test, bench, fmt, explain, workbench, debug, and doc. | SPEC-06 3.1 |
| `cint build` | Builds ordinary projects with no separately maintained build script. | SPEC-06 3.3 |
| `cint.project` (project manifest) | Declarative project manifest. (Proposed name and syntax) | SPEC-06 3.3 |
| Workbench | The live session that compiles every entry to SIR and runs it on a conforming backend. | SPEC-06 5 |
| Workbench entry | One line or block submitted in the workbench; each is one host entry. | SPEC-06 5.2 |
| Branch | A named line of work in the checkpoint graph. | SPEC-06 5.5 |
| Exact inspector | `:inspect`, showing every value exactly, with its type, in the formats `/raw`, `/hex`, `/bytes`, `/trits`, `/grid`, and others. | SPEC-06 6 |
| Watch | An expression evaluated at every boundary and reported when it changes. | SPEC-06 6.4 |
| `explain` | Report of shapes, reads and writes, aliasing, fault sites, reductions, fuel, and backend lowering of a target. | SPEC-06 7 |
| Site | A numbered fault-capable node of SIR with its module, line, and column. | SPEC-09 SIR-04 |
| Differential debugger | Runs a saved case on a reference and a candidate backend and reports the first canonical difference. | SPEC-06 9 |
| Case | A self-contained saved checkpoint, entries, and effects for the differential debugger. | SPEC-06 9.2 |
| Time travel | Replay to a boundary from the nearest earlier checkpoint. | SPEC-06 10.1 |
| Receipt | A canonical JSON file (RFC 8785 serialization plus one LF) that records a build, a measurement, or a conformance run, with the parts `identity`, `outcome`, and `observations`. A measured claim cites its receipt. A receipt stays with the project; it is not the build record, which travels with a program or library. | SPEC-09 RCPT-02, RCPT-03; SPEC-06 12 |

## 10. Ternary representations

| Term | Definition | Defined in |
|---|---|---|
| Balanced ternary | Digits -1, 0, and +1 with weights `3^i`. | SPEC-05 TR-DIG-1 |
| 1.58-bit pipeline | Integer absmean ternarization, absmax activation quantization, `tdot`, and one rounded rescale. | SPEC-05 8.2 |
| Legacy word encoding | The `cint-bt27-legacy` 6-byte base-243 word, related byte by byte to `PT5[27]`. | SPEC-05 11.4 |

## 11. Security

| Term | Definition | Defined in |
|---|---|---|
| Key agent | The process that holds secret keys and signs. | SPEC-07 6.3 |
| Capability | A named authority beyond compute and the program's own arena, with class 0, 1, or 2. | SPEC-07 7.2 |
| Capability manifest | `CICAP001`: the sorted canonical capability texts a program declares. | SPEC-07 SEC-CAP-6, SEC-CAP-7 |
| Admission | The loader's decision to run a program and with which grants. Every other sense is qualified: backend admission, feature admission, dispatch admission. The check that a compiler meets a roadmap stage is stage acceptance. | SPEC-07 8.1 |
| Default sandbox | Compute, own arena, and console output only; for SIR programs that are unsigned or signed by an untrusted key. Native programs are refused, never sandboxed. | SPEC-07 8.2, SEC-ADM-13 |

## 12. Bolt-under integration

| Term | Definition | Defined in |
|---|---|---|
| Bolt-under | The plain word for the attached-processor idea: an exact integer engine attached under a host system, as Floating Point Systems attached its AP-120B array processor to a host computer. The host keeps its tools. | SPEC-00 9; SPEC-08 Summary |
| Equal, better | Passing every E gate against the measured bar; passing a declared B gate. | SPEC-08 2.2, 2.3 |
| Gate classes E, B, C, R | Equal, better, conformance, and reference-validation gates. | SPEC-08 11.1 |
| Gateway federate | Host code that publishes the engine's wire bytes through an RTI without numeric conversion. | SPEC-08 3.1 |
| Canonical stream | `CINTSTR1` records between engine and gateway: `META`, `FRAME`, `COMMIT`, `ACK`, `MODE`, `SAVE`, `RESTORE`, `FAULT`, `EFFECT`, and `REFUSE`. | SPEC-08 4.1 |
| `cint-sf-1` | The SpaceFOM federation profile of the CINT gateway. | SPEC-08 3.3 |
| Boundary tick | The tick whose logical time equals a scheduled mode transition. | SPEC-08 1.3 |

## 13. Compiler and bootstrap

| Term | Definition | Defined in |
|---|---|---|
| SIR | The typed semantic IR with explicit temporaries, checked nodes, and fuel charges. | SPEC-09 6 |
| `cint-seed` (seed compiler) | The hand-written C17 seed compiler that accepts only `cint-boot-1`. Written "seed compiler" wherever "seed" could also mean a secret seed or a random seed. | SPEC-09 4 |
| `cint-boot-1` | The frozen subset the compiler is written in, stated as the SPEC-04 section 18 productions it admits, with restrictions; it has no `Str` type. | SPEC-09 5, BOOT-01, BOOT-02 |
| `cintc` | The product compiler, written in CINT. | SPEC-09 1.3, 10 |
| `cint-rt-1` | The runtime contract between generated C and the runtime; the header `rt/cint_rt.h` is normative. | SPEC-09 RCPT-08 |
| `cint_ref` | The separately written Python exact reference. | SPEC-09 REF-01 to REF-04 |
| S1, S2, S3 and B0, B1, B2 | Emitted C of the compiler at stages 1 to 3, and the executables built from them. | SPEC-09 1.3, 11.1 |
| Executable | A native program file, including the compiler executables B0, B1, and B2. "Binary" is used only for base 2. | SPEC-09 1.3 |
| Build manifest | The ordered list of source files that a build reads; the bridge reads files in its order, never by directory enumeration. | SPEC-09 CINTC-06, CINTC-12 |
| Fixpoint | S2 and S3 byte-identical. | SPEC-09 FIX-01 |
| Stages T0 to T6 (roadmap stages) | The staged roadmap from reference machine to public release. A bootstrap stage (S1 with B0 to S3 with B2) is a different thing; a WGSL shader stage is written "shader stage". | SPEC-09 14, 11.1 |
| Stage acceptance | The check that a compiler meets a roadmap stage: its acceptance test, its required case list, and its receipts (`tools/cint_accept.py`). Not "admission," which is the loader's decision. | SPEC-09 STAGE-01, CONF-15 |
| Disclosure package | Full source, fixtures, receipts, and a reproduction procedure for third parties. | SPEC-09 13 |

## 14. Profiles and sources

| Term | Definition | Defined in |
|---|---|---|
| Language profile | A named meaning: values, fault codes and content, fuel counts, and diagnostic codes and positions; a change to any of these for a frozen expectation requires a new language profile, while an encoding change that carries the same content is a new encoding version. "Profile" alone means the language profile; other kinds are qualified (capability, device, federation, verification profile). | SPEC-00 3; SPEC-01 1.1 |
| `cint-core-1` | The language profile this specification defines. | SPEC-00 3 |
| `cint-bt27-legacy` | The frozen previous language; `.cint` files. | SPEC-00 3; SPEC-05 11 |
| Admission test | Does it make exact computation easier to express, inspect, accelerate, or reproduce? | SPEC-00 5 |
| Specified, Proposed, Open | Specification status, what the text decides; every rule carries exactly one. | SPEC-00 1 |
| Measured | Evidence status: a result that was measured, with a receipt a reader can obtain. | SPEC-00 1 |
| REFERENCES-P.1 | The shared bibliography; each document's References list is a subset of it, with the same wording. | [REFERENCES-P.1.md](REFERENCES-P.1.md) |

## 15. Programs, libraries, and build records

| Term | Definition | Defined in |
|---|---|---|
| Program | An executable that `cint build` produces, run by `cint run` or by the host operating system. | SPEC-06 3.3; SPEC-07 8 |
| Library | A loadable build of CINT exports, called from Python, C, Fortran, or the HLA gateway through the C ABI. | SPEC-03 5 to 7; SPEC-08 3 |
| Generated source | The C17 text the compiler emits for a program or library (S1, S2, and S3 for the compiler itself). The same build identity produces byte-identical generated source. WGSL, SPIR-V, and other backend text is emitted text under the same rule. | SPEC-09 EMIT-01, RCPT-01 |
| Kernel binary | A CINT kernel compiled for a GPU backend, for example SPIR-V for `gpu-spirv-vk` or PTX for `gpu-cuda`. Its bytes are part of build identity; the GPU driver's own compilation of them is not. | SPEC-09 8.2, 16 |
| Build record | The canonical record that travels with a build output and binds its payload digest to its revision identity, source digest, toolchain digest, inputs digest, capability manifest digest, program identity, targets, dependencies, and signer (format `CIBREC01`). It contains no timestamps, host paths, or user names. A SIR container carries it as a trailing section; a native program or library carries it in a sidecar file. | SPEC-07 9.1 to 9.4 |
| Build receipt | The receipt that `cint build` or a bootstrap writes. | SPEC-09 RCPT-02 |
