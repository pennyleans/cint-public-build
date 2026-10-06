# SPEC-01P.1: Integer machine

SPEC-01P.1, edition 1 of the integer machine specification, cint v0.1.0, 2026-10-06.

Status: Draft. This edition describes the language profile `cint-core-1`, which is not yet frozen: a Proposed rule may change, and an Open question may be decided, in a later edition. The text of this edition does not change after its release. Corrections that change no meaning are listed in [ERRATA.md](ERRATA.md); a change of meaning is published as a new edition. Clause identifiers, section numbers, and open-question identifiers are the same in every edition and are never reused. SPEC-02, SPEC-03, and SPEC-05 to SPEC-09 have no public edition yet; their clauses are cited by identifier and are not part of this edition. What a given release of cint implements is stated in that release's notes, not here.

This section defines the computational core of `cint-core-1`: the numeric types, the exact meaning of every arithmetic, conversion, and comparison operation, the named reduction operations, evaluation order, the fault model, fuel accounting, and the canonical byte serialization of values, faults, state, and program revision identity. Every operation is defined on mathematical integers (the set Z) together with a declared width and an explicit overflow intent. An implementation conforms when, for every program and input in scope, it produces the same canonical values, the same canonical fault record, and the same fuel count as the reference definition given here. How an implementation achieves that (binary or balanced-ternary hardware, scalar or SIMD code, CPU or GPU) is not part of the meaning. Balanced ternary is the radix-3 notation with digits -1, 0, and +1 described by Knuth [1].

The design rests on these principles: `I64` is the canonical word; overflow intent is in the operator; there are no implicit conversions between runtime numeric types; reductions are named operations with defined meaning rather than assumed consequences of integer arithmetic; fixed point declares storage, sign, scale, intermediate width, rounding, and overflow; faults are canonical, carry exact operands, and are logically addressed; backends never silently narrow. Section 13 gives a boundary-case fixture table and a fixture file format (CIF-1) that an independent Python exact-integer reference can implement directly.

## 1. Conventions

### 1.1 Statement labels

Every rule in this document carries one of three labels, either on the rule itself or on the heading that groups it. A fourth label, Measured, marks a figure from a cited receipt; it appears only in informative text and never states a rule.

| Label | Meaning |
|---|---|
| Specified | Normative for `cint-core-1`. A conforming implementation must behave exactly as stated. A change to the meaning it gives a frozen expectation (a value, a fault code or fault content, a fuel count, or a diagnostic code or position) requires a new language profile. A change of encoding that carries the same content is a new version of that encoding, by its domain string (IM-152), the runtime contract version, or the `.expect` format, not a new profile (SPEC-00 section 1). |
| Proposed | The intended design. Implementations should follow it; it may change before the profile is frozen without a profile rename. |
| Open | Undecided. Listed in section 15. Implementations must not rely on any particular resolution. |
| Measured | A figure from a named measurement run, cited with its receipt and date. Informative only (section 14). |

Implementation status is given in each release's notes. A Specified heading may contain a rule individually labeled Proposed or Open; the individual label governs that rule. Section 14 is informative (sources only). Section 16 lists Specified exclusions.

Clause identifiers. Each Specified or Proposed rule carries a clause identifier `IM-<n>`, written in bold at its start. Its label is the one written on the rule or, when the rule carries none, the one on its heading. Examples, tables of examples, and rationale that follow a rule belong to that rule. The identifiers IM-1 to IM-183 were assigned in document order. An identifier is never reused or renumbered: a rule added later takes the next unused number wherever it appears, and a withdrawn rule keeps its number with the word "Withdrawn." Conformance cases, `.expect` files (SPEC-09 9.2), and other sections cite a single rule by its identifier, for example SPEC-01 IM-30. A citation by section number cites every rule in that section. Open items carry no identifier; they are listed in section 15.

The words "must," "must not," and "may" are normative. "Is a compile error" means the program is rejected before execution with a diagnostic carrying a source position; no execution state exists. Compile errors carry the numeric diagnostic codes of SPEC-04 LS-283. Where this section writes "compile error `E_X`," the diagnostic is C2003 when a literal or constant is out of range of its type (reporting `E_NARROW`), or C6001 carrying the fault code `E_X` when a fault occurs during compile-time evaluation; "compile error (type)" is a C2xxx type diagnostic. Operation-level fixtures (CIF-1, section 13.1) record the fault code, and the program-level `.expect` layer records the C-numbered diagnostic code (for example C2003).

### 1.2 Notation (Specified)

**IM-1.** This section uses the following notation.

| Notation | Meaning |
|---|---|
| Z | The mathematical integers, unbounded. |
| `w(T)` | Bit width of binary integer type `T`. |
| `MIN(T)`, `MAX(T)` | Least and greatest values of `T`. For signed `T`: `-2^(w-1)` and `2^(w-1) - 1`. For unsigned `T`: `0` and `2^w - 1`. |
| `in(T, v)` | True when `MIN(T) <= v <= MAX(T)` for `v` in Z. |
| `wrap(T, v)` | For a binary integer type `T` only: the unique `r` with `in(T, r)` and `r ≡ v (mod 2^w(T))`. Wrapping for ternary types is owned by the ternary section (section 12). |
| `sat(T, v)` | `MIN(T)` if `v < MIN(T)`, `MAX(T)` if `v > MAX(T)`, else `v`. |
| `clamp(v, l, h)` | For `l <= h`: `min(max(v, l), h)`. |
| `floor(x)` | Greatest integer `<= x`, for rational `x`. |
| `M64`, `m64` | `MAX(I64) = 9223372036854775807`, `MIN(I64) = -9223372036854775808`. |

All examples are written in `.ci` syntax. Values in tables are decimal unless prefixed `0x`.

### 1.3 Relation to other sections

**IM-2.** Specified. This section owns arithmetic meaning, numeric types, reductions, evaluation order, the fault model, fuel accounting, operation identifiers, and the canonical serialization of values, fault records, state framing, and program revision identity (section 11). Other sections own: arrays, views, and bounds (`E_BOUNDS`, `E_SHAPE`, `E_ALIAS`); kernels and dispatch; arenas and handles (`E_STALE_HANDLE`); ternary representations (`T1`, `T27`, packed trits, `tdot`, `rescale3`); the host boundary (Python, C ABI, Fortran wrappers, recorded effects); statements and modules; the semantic IR (SIR) and its canonical encoding. Where this section mentions their faults or types, it defines only the parts the integer machine depends on. Where another section restates a rule owned here, this section governs.

### 1.4 Required alignment in other sections (Specified)

**IM-3.** Each section listed below must agree with this section on the item named, and where they differ, this section's rule governs.

| Section | Item | Required change |
|---|---|---|
| Memory and host (SPEC-03) | `cint_fault` struct (5.3) | The canonical fault record is the byte encoding of section 11.2. The ABI must provide a call that returns those bytes. Any struct is a non-canonical, possibly lossy convenience view. "Not in a kernel" is a presence flag, never dispatch value 0. Work-item index is the linear `I64` of section 9.3. |
| SPEC-03 | M-3, M-5, H-15 | State framing, domain strings, and the revision identity are those of sections 11.3 and 11.4 (lowercase, versioned domain strings). |
| SPEC-03 | `cint_elem`, A-13, F-4 | Fixed point is described parametrically (storage element plus `f`), matching tag `31` of section 11.1, not by one enumerator per format. |
| SPEC-03 | M-19, A-8 | Wide integers are aligned to 8 bytes at the ABI (section 2.4), not to their size. |
| SPEC-03 | H-11, A-12 | Fuel allowance is per entry and is an `I64` with `B >= 0`; a host value above `M64` is refused (section 10.1). |
| SPEC-03 | X-4, X-5, X-6 | Rounding-mode names are those of section 4.5 (`trunc`, not `toward_zero`). The `saturate` policy is defined in section 5.7. |
| Bootstrap (SPEC-09) | SEED-08 | Already consistent with section 3.2 (constant expressions use run-time checked semantics). |
| SPEC-09 | SIR-07 | A `fuel.charge` amount may be computed from shapes known at the charge point (section 10.2). |
| SPEC-09 | SIR catalog 6.2, SIR-03, SIR-09 | Opcodes are spelled with the lowercase operation identifiers of section 9.8, or mapped to them by a specified table. Every named operation of sections 4.10, 5, and 6 needs a node family with internal widths up to 2048 bits. |
| SPEC-09 | CONF-01 | Program-level `.ci`/`.expect` cases and operation-level CIF-1 fixtures are two layers with the mapping of section 13.1. |
| SPEC-09 | Q7 | Closed by section 4.4: `MIN % -1` returns 0. |
| Ternary (SPEC-05) | TR-T27-2, TR-T27-3, `rescale3` | Ternary wrapping is owned there (section 12). `E_SHIFT` covers any count outside an operation's declared range (section 9.1). Compile-time type errors are compile errors, not `E_UNSUPPORTED`. The default `/` is floor division (section 4.4). |
| Security model (SPEC-07) | SEC-SD-1, SEC-SD-2 | Redaction uses the redacted-operand form of sections 9.2 and 11.1. |
| SPEC-07 | SEC-ED-12 | The digest reduction note of section 2.4 applies. |
| SPEC-07 | D10, section 14 item 7 | D10 holds only to the extent that fuel bounds computation (section 10.3). Item 7 should cite SPEC-09 DISC-03 (section 9.6). |

## 2. Types

### 2.1 Scalar integer types (Specified)

**IM-4.** The scalar integer types are:

| Type | Signed | Width | MIN | MAX |
|---|---|---:|---:|---:|
| `I8` | yes | 8 | -128 | 127 |
| `I16` | yes | 16 | -32768 | 32767 |
| `I32` | yes | 32 | -2147483648 | 2147483647 |
| `I64` | yes | 64 | -9223372036854775808 | 9223372036854775807 |
| `U8` | no | 8 | 0 | 255 |
| `U16` | no | 16 | 0 | 65535 |
| `U32` | no | 32 | 0 | 4294967295 |
| `U64` | no | 64 | 0 | 18446744073709551615 |
| `I128` | yes | 128 | `-2^127` | `2^127 - 1` |
| `I256` | yes | 256 | `-2^255` | `2^255 - 1` |
| `I512` | yes | 512 | `-2^511` | `2^511 - 1` |
| `I1024` | yes | 1024 | `-2^1023` | `2^1023 - 1` |

**IM-5.** A value of an integer type is a member of Z within the type's range. Its meaning does not depend on representation. Signed types are specified with two's complement bit patterns only where an operation is defined on bits (bitwise operators, `as%`, `<<%`, serialization); everywhere else only the mathematical value is observable.

**IM-6.** Proposed: unsigned wide types `U128`, `U256`, `U512`, `U1024` with the same limb layout and the operations of section 2.4. Open: whether they are needed in `cint-core-1` (section 15, question 3).

### 2.2 The canonical word (Specified)

**IM-7.** `I64` is the canonical machine word. Concretely:

- Array lengths, shape extents, strides, indices produced by `for i in 0..n`, `count` results, dispatch numbers, work-item indices, step ordinals, fuel budgets, fuel counts, and call-depth limits are `I64`.
- A literal with no typing context has type `I64` (section 3.2).
- The bounds of a range `a..b` in `for i in a..b` must have type `I64` (literals are typed `I64` there). A bound of any other type is a compile error; there is no implicit widening:

```c
I32 n32 = 10;
for i in 0..n32 { }            // compile error: range bound has type I32, not I64
for i in 0..(n32 as I64) { }   // valid; i has type I64
```

- A backend that cannot execute `I64` arithmetic natively must either lower it to an exactly equivalent multiword sequence or refuse the program with `E_UNSUPPORTED` (section 9.7). Semantic narrowing is forbidden: no `I64` value may be computed, stored, or compared at fewer bits in a way that can change any value or fault.

**IM-8.** As-if representation rule. An implementation may hold an `I64` value in a narrower machine representation when it has proven, from static facts or from facts checked at dispatch admission (admitted extents, device limits), that every value the variable can take fits that representation, and that no observable value or fault differs. Example: on a WGSL target, buffer bindings are limited by `maxStorageBufferBindingSize` [2] and `arrayLength` returns `u32` [3], so after admission checks the extents of every bound view fit in 32 bits. A kernel's index arithmetic over those extents may then use `u32` registers, provided every index computation that could exceed 32 bits (for example a product of two extents) is either proven in range or computed exactly. The representation is not observable; the fault record still carries `I64` values.

**IM-9.** Being the canonical word grants no implicit conversion: an `I32` is never silently widened to `I64`.

### 2.3 Bool (Specified)

**IM-10.** `Bool` has the two values `false` and `true`. Conditions in `if`, `while`, `for`, `?:`, `&&`, and `||` must have type `Bool`; an integer condition is a compile error. `b as I64` (and to any integer type) yields 0 or 1. There is no conversion from integers to `Bool`; write `x != 0`.

### 2.4 Wide integers (Specified)

**IM-11.** `I128`, `I256`, `I512`, and `I1024` are fixed-capacity signed integers. They are ordinary value types: every operator, form, and fault of section 4 applies to them exactly as to `I64`.

**IM-12.** Limb layout. A value of width `w` is stored as `L = w / 64` limbs, each a 64-bit unsigned bit pattern. Limb 0 holds bits 0 to 63 (least significant), limb `L - 1` holds the most significant bits including the sign bit. The value is the two's complement interpretation of the concatenated `w` bits. There is no separate sign word, no length field, and no unused limb; every bit pattern is a valid value, so there is exactly one representation per value.

| Type | Limbs | Bytes | ABI alignment | Array stride |
|---|---:|---:|---:|---:|
| `I128` | 2 | 16 | 8 | 16 |
| `I256` | 4 | 32 | 8 | 32 |
| `I512` | 8 | 64 | 8 | 64 |
| `I1024` | 16 | 128 | 8 | 128 |

**IM-13.** Canonical bytes. The canonical byte serialization of a wide value is its `w / 8` bytes in little-endian order (limb 0 first, each limb little-endian). Examples:

| Type | Value | Canonical bytes (hex) |
|---|---|---|
| `I128` | `-1` | `ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff` |
| `I128` | `2^64` | `00 00 00 00 00 00 00 00 01 00 00 00 00 00 00 00` |
| `I128` | `1` | `01` followed by 15 bytes `00` |

**IM-14.** ABI. At the C ABI and in Fortran wrappers a wide value is an array of `L` native `uint64_t` limbs, limb 0 first, each limb in the host's native byte order. Its alignment is 8 bytes (the alignment of `uint64_t`), and an array of wide values has stride `w / 8` bytes with no padding. Canonical bytes (serialized state, fault records, recordings) are always little-endian; on a little-endian host the two coincide, and on a big-endian host the boundary converts each limb. Compiler-native wide types (for example C23 `_BitInt(N)` [4] or the GCC extension `__int128` [5]) may be used inside generated code but must not appear in a stable external interface.

**IM-15.** Legacy. The `cint-bt27-legacy` wide library uses a 65-word sign-magnitude layout with radix 65536. That layout is not used by `cint-core-1` and its values are not interchangeable without explicit conversion.

**IM-16.** Operations at each width (S = Specified, P = Proposed, a dash = not provided):

| Operation | `I8`..`I64`, `U8`..`U64` | `I128` | `I256` | `I512` | `I1024` |
|---|---|---|---|---|---|
| `+ - *` checked, `+% -% *%`, `+\| -\| *\|` | S | S | S | S | S |
| unary `-`, `abs` | S | S | S | S | S |
| `uabs` | S (signed only) | P (needs `U128`) | P | P | P |
| `/ %` (floor), `div_trunc`, `rem_trunc`, `div_euclid`, `rem_euclid`, `divmod` | S | S | S | S | S |
| `div_round(a, b, mode)` | S | S | S | S | S |
| `== != < <= > >=` | S | S | S | S | S |
| `& \| ^ ~` | S | S | S | S | S |
| `<< <<% >>` | S | S | S | S | S |
| `rotl`, `rotr` | S (unsigned only) | P (with `U128`..) | P | P | P |
| `as`, `as%` | S | S | S | S | S |
| `as?` | S | S | S | S | S |
| `as\|` | P | P | P | P | P |
| `mul_full(a, b)` result width `2w` | S (`I8` to `I16`, ..., `I64` to `I128`; `U64` to `U128` is P) | S (to `I256`) | S (to `I512`) | S (to `I1024`) | dash |
| `muldiv(T, a, b, c, mode)` intermediate width `2w` | S | S | S | S | S |
| `isqrt(x)`, `isqrt_round(x, mode)` | S | S | S | S | S |
| `min`, `max`, `clamp` | S | S | S | S | S |
| `pow(x, n)` checked | P | P | P | P | P |
| `ilog2(x)`, `clz`, `ctz`, `popcount` | P | P | P | P | P |

`I1024` has no `mul_full` because `mul_full` returns a program-visible type of width `2w`, and `cint-core-1` defines no 2048-bit type. Adding `I2048` would double the largest fixed-capacity value, its ABI size, and its canonical encoding for one operation; that cost is not justified by any workload examined so far. `muldiv` on `I1024` remains Specified because its 2048-bit product is internal and never a program type.

**IM-17.** Implementation note (Specified consequence, informative cost). Section 8.2 forbids partial stores. For `acc = acc + x` on `I1024`, signed overflow is known only after the most significant limb's carry, so an implementation that updates limbs in place must either compute into a temporary and copy back, or run a read-only carry pass first. An implementation may instead update in place and restore the old limbs on overflow, provided no observable state at the fault differs.

**IM-18.** Digest reduction note (Specified meaning; for the security section). A 512-bit unsigned digest read as `I512` is negative when its top bit is set, and floor `%` (section 4.4) then yields the residue of the negative value, not of the unsigned digest. Code that reduces a digest `d` modulo `L` must first form the unsigned value in a wider type:

```c
I512  d  = digest_as_i512(h);                  // little-endian bytes reinterpreted, may be negative
I1024 du = (d as I1024) + (d < 0 ? (1 as I1024) << 512 : 0);   // unsigned value in [0, 2^512)
I1024 r  = du % (L as I1024);
```

With all 64 digest bytes equal to `ff`, `d` is -1; `d % L` gives `L - 1` while the correct residue of `2^512 - 1` is a different value (fixture 117). Open question 3 decides whether `U512` replaces this pattern.

## 3. Literals and context typing

### 3.1 Literal forms (Specified)

**IM-19.** The literal forms are:

| Form | Example | Value |
|---|---|---|
| Decimal | `1_000_000` | 1000000 |
| Hexadecimal | `0xFF_FF` | 65535 |
| Binary | `0b1010` | 10 |
| Octal | `0o17` | 15 |
| Character | `'a'`, `'\n'`, `'\''`, `'\\'`, `'\u{1F600}'` | The Unicode scalar value [6]: 97, 10, 39, 92, 128512 |
| Decimal fraction (fixed-point context only) | `1.5` | 3/2 |
| Hexadecimal fraction (fixed-point context only) | `0x1.8` | 3/2 |
| Balanced ternary | `0tP0N` | 8 (owned by the ternary section) |

**IM-20.** Underscores may appear between digits only. A literal denotes a value in Z (or a rational for fractions); it never denotes a bit pattern. `0xFFFFFFFF` is 4294967295 whatever its context. A character literal is an integer literal: it has no type of its own, is typed by context like any other integer literal, and must fit its type (`U8 c = '\u{100}';` is a compile error `E_NARROW`). A character literal containing a surrogate code point or more than one scalar value is a compile error. `case 'a'..='z':` is therefore valid wherever integer case ranges are (case ranges are inclusive, SPEC-04 LS-177).

### 3.2 Context typing and constant expressions (Specified)

**IM-21.** A literal has no runtime type of its own. It takes its type from its immediately enclosing context, by the first applicable rule:

1. The declared type of the variable it initializes or is assigned to.
2. The parameter type of the argument position it fills.
3. The declared return type, in a `return` statement.
4. The type of the other operand of a binary arithmetic, bitwise, or comparison operator whose other operand is typed. This rule does not apply to either operand of `<<`, `<<%`, or `>>`.
5. The element type, in an array initializer of declared element type.
6. Otherwise `I64`.

**IM-22.** Propagation. An operator expression all of whose operands are untyped (literals, or operator expressions that are themselves untyped) is untyped, and its context type passes to its operands. For `<<`, `<<%`, and `>>`: the left operand is typed by rules 1 to 3, 5, and 6 (the context of the whole shift), never by rule 4 from the count; a literal count is typed `I64`, and a typed count keeps its own type (section 4.6).

```c
U8  k = 40;
I64 v = 1 << k;              // 1 is typed I64 from the declaration, not U8 from k: 1099511627776
```

**IM-23.** Constant expressions. A constant expression is an expression built only from literals, names declared with `const`, operators, and named operations of sections 4 and 5. It is typed first, by the rules above exactly as if it were evaluated at run time, and then evaluated at compile time with the run-time semantics of sections 4 and 5: checked, wrapping, and saturating forms, shift-count rules, and fixed-point rounding apply unchanged. There is no evaluation in Z of a whole expression. A fault during this evaluation is a compile error carrying the fault's code and operand capture (section 8.4). A literal whose value is not in the range of its type is a compile error `E_NARROW`.

The unit and the order of compile-time evaluation (Specified):

- Unit. The maximal constant expression is the unit of compile-time evaluation. Within it, `&&`, `||`, and `?:` evaluate only the operands the result requires (section 8.1). A skipped operand is type-checked and its literals are range-checked, but it is not evaluated. So `const Bool B = false && (1 / 0 == 0);` is `false` with no diagnostic, and `true ? 1 : 1 / 0` is 1.
- Operands that run time may skip. A constant operand below the right side of `&&` or `||`, or below an arm of `?:`, in a non-constant expression is not evaluated at compile time; it runs, and faults, only if it is reached (section 8.4: folding never changes the outcome). In `Bool b = x == 2 && (1 / 0 == 0);` the program compiles and faults `E_DIV_ZERO` at run time when `x` is 2. The rule is about operators, not reachability: a constant expression that its statement always evaluates is still a compile error (C6001), even in a statement that a run may never reach.
- Literal conversions. A checked conversion whose source is a literal (IM-26) is decided at compile time wherever it appears, including inside an operand that `&&`, `||`, or `?:` skips, as the range check of a literal is. Out of range is C6001 `E_NARROW` at the `as`. No run-time fault record therefore carries a literal's `Z` (IM-108).
- Competing errors. A literal's range is checked when the literal is evaluated, in evaluation order. After a fault-free evaluation, every literal, including one in a skipped operand, is range-checked (C2003), and the checked conversions of literals in skipped operands are decided with those checks, in source order. So `const I8 X = 100 + 100 - 150;` is C6001 (`E_OVERFLOW`) at the first `+`, as SPEC-09 SEED-08 says, and `150` is never reached.

**IM-24.** Every `const` declaration has a declared type (`const I8 K = 100;`). A `const` name is an ordinary typed operand; it does not take its type from context. There are no untyped named constants.

**IM-25.** Negative literals. A prefix `-` in operand position followed by a numeric or character literal forms a single negative literal, whatever whitespace or comments separate the two tokens: `-128`, `- 128`, and `-/* c */128` are each -128 in context `I8`, never the negation of an `I8` 128. A `-` before any other expression, including a parenthesized literal or a negative literal, is negation: `-(128)` and `- -128` are negations.

**IM-26.** Conversion sources. A literal operand is a literal, a negative literal, or a literal operand in parentheses. A literal operand that is the source of `as`, `as%`, `as?`, or `as|` is a value in Z, and the conversion applies to that value directly: `(18446744073709551615) as U64` is 18446744073709551615. Any other untyped source expression is typed by rule 6 (`I64`) before conversion.

```c
I8   a = -128;                      // valid
I8   b = 128;                       // compile error E_NARROW
U8   c = -1;                        // compile error E_NARROW
U8   c2 = - 1;                      // compile error E_NARROW: the negative literal -1 (IM-25)
U64  u = (18446744073709551615) as U64;  // 18446744073709551615: a literal operand (IM-26)
U64  d = -1 as% U64;                // 18446744073709551615
I32  e = 0xFFFFFFFF;                // compile error E_NARROW: the literal is 4294967295
I32  f = 0xFFFFFFFF as% I32;        // -1
I64  g = 3 * 1_000_000_000_000 / 7; // all literals typed I64: 428571428571
I16  h = 40000 - 30000;             // compile error E_NARROW: 40000 is not in I16
I32  v = 2_000_000_000 * 2 / 4;     // compile error E_OVERFLOW at *, exact 4000000000 (same as run time)
I64  s = 1 << 100 >> 90;            // compile error E_SHIFT: count 100 exceeds 63
I8   p = 100 +% 100;                // -56
const I8 K = 100;
I16  z = K + K;                     // compile error: I8 result initializes I16 (type error, no fault code)
I16  z2 = (K as I16) + (K as I16);  // 200
Q16.16 q = (1.0 / 3.0) * 3.0;       // raw 65535: 1.0 / 3.0 rounds to raw 21845 (section 5.4), then * 3.0
I32  x = 7;
I32  y = x + 2_147_483_647;         // literal typed I32 by rule 4; with x = 7 this faults E_OVERFLOW at run time
```

**IM-27.** Fixed-point literals (Specified). In a context of type `Q<i>.<f>`, a decimal, hexadecimal, or integer literal denotes its exact rational value `v`. If `v * 2^f` is an integer within range, the raw value is that integer. If it is not an integer, the literal is a compile error unless written with an explicit rounding clause:

```c
Q16.16 half  = 0.5;                       // raw 32768, exact
Q16.16 two   = 2;                         // raw 131072, exact
Q16.16 tenth = 0.1;                       // compile error: 0.1 is not representable in Q16.16
Q16.16 t2    = 0.1 round half_even;       // raw 6554
Q16.16 r     = Q16.16.raw(6554);          // raw constructor, exact
```

Open: typed literal suffixes (for example `127_I8`) for use where no context exists (section 15, question 6).

## 4. Integer operations

### 4.1 Operator forms (Specified)

**IM-28.** Each arithmetic operator has up to three forms. The form is chosen by the spelling, never by a mode, pragma, or build flag.

| Operation | Checked (faults) | Wrapping (mod `2^w`) | Saturating (clamps) | Result-returning (no fault) |
|---|---|---|---|---|
| Add | `a + b` | `a +% b` | `a +\| b` | `add_result(a, b)` |
| Subtract | `a - b` | `a -% b` | `a -\| b` | `sub_result(a, b)` |
| Multiply | `a * b` | `a *% b` | `a *\| b` | `mul_result(a, b)` |
| Divide (floor) | `a / b` | dash | dash | `div_result(a, b)` |
| Remainder (floor) | `a % b` | dash | dash | `rem_result(a, b)` |
| Shift left | `a << k` | `a <<% k` | dash | `shl_result(a, k)` |
| Shift right | `a >> k` | dash | dash | dash |
| Negate | `-a` | `0 -% a` | `0 -\| a` | `sub_result(0, a)` |
| Convert | `a as T` | `a as% T` | `a as\| T` (Proposed) | `a as? T` |

**IM-29.** Both operands of a binary arithmetic operator must have the same type (after literal typing). Mixing `I32` and `I64`, or `I64` and `U64`, is a compile error.

**IM-30.** Let `v` be the exact result in Z (or rational for division) of the mathematical operation. Then:

- Checked: if `in(T, v)`, the result is `v`; otherwise the operation faults `E_OVERFLOW`.
- Wrapping: the result is `wrap(T, v)`.
- Saturating: the result is `sat(T, v)`.
- Result-returning: the result is an error union `ArithError!T` (a value that holds either a `T` or an arithmetic error, SPEC-04 LS-94) holding `v` when the checked form would succeed, or the `ArithError` value that mirrors the fault code the checked form would raise: `.overflow` for `E_OVERFLOW`, `.div_zero` for `E_DIV_ZERO`, `.shift` for `E_SHIFT`, and `.narrow` for `E_NARROW` (IM-188). It is consumed with `try`, `catch`, or `_ =`; to branch on the error, bind it with `catch (e)` and switch on the error value (SPEC-04 LS-95).

**IM-188.** (Specified.) `ArithError` is a built-in error set, declared as if by `error ArithError : U16 { overflow, div_zero, shift, narrow }` (SPEC-04 LS-92, LS-93). Its values are numbered 1 to 4, in the order of the fault codes they mirror (IM-104 codes 1, 2, 5, and 6), so a value's tag does not equal its fault code; equal numbers would need explicit values in error sets, which SPEC-04 LS-93 does not provide. `as?` is the result-returning form of `as`: it returns `.narrow` where `as` faults `E_NARROW`. `cint-core-1` defines no result-returning form of fixed-point `*`, `/`, or `sqrt` yet; if such forms are added, `ArithError` can gain a value such as `domain` for them at its end, which keeps every earlier number (SPEC-04 LS-93).

**IM-31.** Wrapping and saturating forms never fault, except that shift-count and division-by-zero preconditions still apply (sections 4.4, 4.6).

### 4.2 Addition, subtraction, multiplication (Specified)

**IM-32.** `v = a + b`, `a - b`, `a * b` in Z, then the form rule of 4.1. Unsigned subtraction with `a < b` is an overflow in the checked form.

| Expression | Type | Result |
|---|---|---|
| `M64 + 1` | `I64` | `E_OVERFLOW`, exact result 9223372036854775808 |
| `M64 +% 1` | `I64` | -9223372036854775808 |
| `M64 +\| 1` | `I64` | 9223372036854775807 |
| `m64 - 1` | `I64` | `E_OVERFLOW`, exact result -9223372036854775809 |
| `m64 -% 1` | `I64` | 9223372036854775807 |
| `3 - 5` | `U8` | `E_OVERFLOW`, exact result -2 |
| `3 -% 5` | `U8` | 254 |
| `3 -\| 5` | `U8` | 0 |
| `m64 * -1` | `I64` | `E_OVERFLOW`, exact result 9223372036854775808 |
| `m64 *% -1` | `I64` | -9223372036854775808 |
| `65536 * 32768` | `I32` | `E_OVERFLOW`, exact result 2147483648 |
| `-65536 * 32768` | `I32` | -2147483648 |
| `100 *% 3` | `I8` | 44 |
| `-128 *\| -1` | `I8` | 127 |

### 4.3 Negation and absolute value (Specified)

**IM-33.** `-a` is `0 - a` in the checked form. Minimum-integer negation faults: `-m64` raises `E_OVERFLOW` with exact result 9223372036854775808. For unsigned types, `-a` faults unless `a == 0`.

**IM-34.** `abs(a)` is checked: `abs(m64)` raises `E_OVERFLOW`. `uabs(a)` returns the absolute value in the unsigned type of the same width (`I8` to `U8`, ..., `I64` to `U64`) and never faults; for wide types it exists only if unsigned wide types are adopted (section 15, question 3): `uabs(m64) == 9223372036854775808` as `U64`.

| Expression | Type | Result |
|---|---|---|
| `-m64` | `I64` | `E_OVERFLOW`, exact 9223372036854775808 |
| `0 -% m64` | `I64` | -9223372036854775808 |
| `0 -\| m64` | `I64` | 9223372036854775807 |
| `-x` with `x = 1` | `U32` | `E_OVERFLOW`, exact -1 |
| `abs(-128)` | `I8` | `E_OVERFLOW`, exact 128 |
| `uabs(-128)` | `I8` to `U8` | 128 |

### 4.4 Division and remainder (Specified)

**IM-35.** Decision: `/` and `%` use floor division. For `b != 0`:

```text
a / b = floor(a / b)            (rational quotient rounded toward negative infinity)
a % b = a - b * (a / b)
```

Consequently `a == (a / b) * b + a % b` always holds, `|a % b| < |b|`, and a nonzero remainder has the sign of the divisor.

Reasons: floor division matches Python's `//` and `%` [7], so the primary audience's reference arithmetic and the independent Python oracle agree without adjustment; it matches the `cint-bt27-legacy` profile, in which division floors and the remainder has the divisor's sign; `a >> k` equals `a / 2^k` for every signed `a`; and periodic indexing (`i % n` with `n > 0`) is never negative. On targets with native truncating division of the operand width, floor division costs a sign-correction step after the native instruction; on targets without native division of the operand width (for example `I64` on a 32-bit GPU), division is a multiword software routine whatever its rounding. This section makes no cost claim beyond that.

**IM-36.** Truncating and Euclidean variants are named functions. The Euclidean pair follows Boute's definition, in which the remainder satisfies `0 <= r < |b|` for every sign of the operands [8]. They are provided because C [9], Fortran `MOD` [10], and WGSL integer `/` and `%` ([3], section 8.8) truncate:

| Function | Quotient | Remainder sign |
|---|---|---|
| `a / b`, `a % b` | `floor(a / b)` | sign of `b` (or zero) |
| `div_trunc(a, b)`, `rem_trunc(a, b)` | toward zero | sign of `a` (or zero); equals C `/ %` and Fortran `MOD` |
| `div_euclid(a, b)`, `rem_euclid(a, b)` | `q` with `0 <= r < \|b\|` | never negative |
| `divmod(a, b)` | returns `(a / b, a % b)` as a pair | as floor |

Fortran `MODULO` equals `%` [10].

**IM-37.** Porting note (Specified). C [9] and Fortran [10] integer division truncate; `cint-core-1` `/` floors. The two differ, without any fault, whenever the operands have opposite signs and the division is inexact: `-7 / 2` is -3 in C and Fortran and -4 in `cint-core-1`. Ported code must map (Fortran intrinsics as defined in [10]):

| Source construct | `cint-core-1` |
|---|---|
| C `a / b`, Fortran `a / b` (integers) | `div_trunc(a, b)` |
| C `a % b`, Fortran `MOD(a, b)` | `rem_trunc(a, b)` |
| Fortran `MODULO(a, b)` | `a % b` |
| Fortran `INT(x)`, where `x` is a `REAL` ported to a `Q` type | `x as I32 round trunc` (or the target width) |
| Fortran `NINT(x)`, where `x` is a `REAL` ported to a `Q` type | `x as I32 round half_away` |

**IM-38.** Proposed: the migration assistant (SPEC-03 7.3) flags every translated `/` and `%` whose operands are not proven non-negative and requires an explicit choice.

**IM-39.** Faults:

- `b == 0`: every division and remainder function faults `E_DIV_ZERO`, including `0 / 0`, `rem_euclid(5, 0)`, and the result-returning forms (which return the error value).
- Signed `a == MIN(T)` and `b == -1`: the quotient `-MIN(T)` is out of range, so `a / b`, `div_trunc`, and `div_euclid` fault `E_OVERFLOW`. The remainders are 0, which is in range, so `a % b`, `rem_trunc`, and `rem_euclid` return 0 and do not fault. `divmod` faults because its quotient does.

**IM-40.** Implementations must not evaluate `MIN % -1` with a native instruction whose behavior is undefined or trapping for that input (C [9], x86 `IDIV` [11]); they must special-case it.

Examples (`I64`):

| `a` | `b` | `a / b` | `a % b` | `div_trunc` | `rem_trunc` | `div_euclid` | `rem_euclid` |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 7 | 2 | 3 | 1 | 3 | 1 | 3 | 1 |
| -7 | 2 | -4 | 1 | -3 | -1 | -4 | 1 |
| 7 | -2 | -4 | -1 | -3 | 1 | -3 | 1 |
| -7 | -2 | 3 | -1 | 3 | -1 | 4 | 1 |
| 6 | -3 | -2 | 0 | -2 | 0 | -2 | 0 |
| `m64` | 2 | -4611686018427387904 | 0 | -4611686018427387904 | 0 | -4611686018427387904 | 0 |
| `m64` | 3 | -3074457345618258603 | 1 | -3074457345618258602 | -2 | -3074457345618258603 | 1 |
| `m64` | -1 | `E_OVERFLOW` | 0 | `E_OVERFLOW` | 0 | `E_OVERFLOW` | 0 |
| 5 | 0 | `E_DIV_ZERO` | `E_DIV_ZERO` | `E_DIV_ZERO` | `E_DIV_ZERO` | `E_DIV_ZERO` | `E_DIV_ZERO` |

Unsigned division has no sign cases; all six functions agree.

**IM-41.** There is no wrapping or saturating division operator. Proposed: `div_wrap(a, b)` returning `MIN(T)` for `MIN(T) / -1` and `div_sat(a, b)` returning `MAX(T)`, both still faulting `E_DIV_ZERO`.

### 4.5 Rounding modes (Specified)

**IM-42.** Every operation that rounds a rational to an integer names its rounding mode. Rounding modes are compile-time constants of the enumeration `Round`; in a `round` clause or a mode argument, the bare name may be written. These names are the only mode names; other sections must use them (there is no `toward_zero`; the mode is `trunc`).

| Mode | Rounds `x` to | `2.5` | `-2.5` | `2.4` | `-2.6` | `3.5` |
|---|---|---:|---:|---:|---:|---:|
| `floor` | toward negative infinity | 2 | -3 | 2 | -3 | 3 |
| `ceil` | toward positive infinity | 3 | -2 | 3 | -2 | 4 |
| `trunc` | toward zero | 2 | -2 | 2 | -2 | 3 |
| `away` | away from zero | 3 | -3 | 3 | -3 | 4 |
| `half_even` | nearest; ties to even | 2 | -2 | 2 | -3 | 4 |
| `half_away` | nearest; ties away from zero | 3 | -3 | 2 | -3 | 4 |
| `half_trunc` | nearest; ties toward zero | 2 | -2 | 2 | -3 | 3 |
| `half_up` | nearest; ties toward positive infinity | 3 | -2 | 2 | -3 | 4 |
| `half_down` | nearest; ties toward negative infinity | 2 | -3 | 2 | -3 | 3 |

**IM-43.** `half_even` is the default wherever a rounding mode may be omitted. Rounding is applied once, to the exact rational value, before any range check or narrowing; there is no double rounding.

**IM-44.** `div_round(a, b, mode)` returns the rational `a / b` rounded by `mode`, then checked against `T`. `div_round(a, b, floor)` equals `a / b`. `div_round(a, b, trunc)` equals `div_trunc(a, b)`.

| Call (`I64`) | Result |
|---|---|
| `div_round(7, 2, half_even)` | 4 |
| `div_round(5, 2, half_even)` | 2 |
| `div_round(-5, 2, half_even)` | -2 |
| `div_round(-5, 2, half_away)` | -3 |
| `div_round(m64, -1, floor)` | `E_OVERFLOW` |
| `div_round(1, 0, half_even)` | `E_DIV_ZERO` |

Open: an `exact` mode that faults when rounding would be needed (section 15, question 2).

### 4.6 Shifts and rotations (Specified)

**IM-45.** The shift count `k` may have any integer type; it is compared as a value in Z. For an operand of type `T` with width `w`, the count must satisfy `0 <= k <= w - 1`; otherwise every shift form, including `<<%` and `>>`, faults `E_SHIFT`. Counts are never reduced modulo `w`, so `x << 64` on `I64` is a fault, not `x` (the result of x86 count masking [11]) and not `0`. The type of the result is the type of the left operand; the count's type never affects it (section 3.2). The `limit` of the `E_SHIFT` record is always `I64(w - 1)`, for every out-of-range count, negative counts included, and the text form of a diagnostic shows the allowed range `0..w-1`.

| Operation | Meaning when `0 <= k < w` |
|---|---|
| `a << k` | `v = a * 2^k` in Z, then checked (`E_OVERFLOW` if out of range). |
| `a <<% k` | `wrap(T, a * 2^k)`: the low `w` bits of the two's complement pattern shifted left. |
| `a >> k` signed | `floor(a / 2^k)` (arithmetic shift). Never overflows. |
| `a >> k` unsigned | `floor(a / 2^k)` (logical shift). Never overflows. |
| `rotl(a, k)`, `rotr(a, k)` | Unsigned `T` only. Bit rotation by `k`. |

**IM-46.** There is no logical right shift for signed types; for `I8` to `I64` write `(a as% U64) >> k` (or the matching unsigned width). Wide signed types have no logical right shift until unsigned wide types are decided (section 15, question 3).

| Expression | Type | Result |
|---|---|---|
| `1 << 62` | `I64` | 4611686018427387904 |
| `1 << 63` | `I64` | `E_OVERFLOW`, exact 9223372036854775808 |
| `-1 << 63` | `I64` | -9223372036854775808 |
| `1 <<% 63` | `I64` | -9223372036854775808 |
| `1 << 64` | `I64` | `E_SHIFT`, count 64, limit 63 |
| `1 << -1` | `I64` | `E_SHIFT`, count -1 |
| `255 <<% 4` | `U8` | 240 |
| `-7 >> 1` | `I64` | -4 |
| `-1 >> 63` | `I64` | -1 |
| `18446744073709551615 >> 63` | `U64` | 1 |
| `x >> 64` | `I64` | `E_SHIFT` |
| `rotl(0x80000001, 1)` | `U32` | 3 |

### 4.7 Bitwise operations (Specified)

**IM-47.** `&`, `|`, `^` (binary) and `~` (unary) operate on the two's complement bit pattern of width `w` and return a value of the same type. Operands must have the same type. They never fault. `~x == -x - 1` for signed `x`; `~x == MAX(T) - x` for unsigned `x`. Bitwise operators are defined on binary integer types only, not on `Bool` (use `&&`, `||`, `!`), not on fixed point (convert with `.raw`), and not on ternary types (ternary section).

### 4.8 Comparison (Specified)

**IM-48.** `== != < <= > >=` compare mathematical values and return `Bool`. Operands must have the same type; comparing `I64` with `U64` or `I32` is a compile error, and a literal operand takes the other operand's type. Fixed-point values of the same `Q` type compare by raw value. `Bool` supports `==` and `!=` only. Comparisons never fault. Chained comparisons such as `a < b < c` are a compile error.

**IM-49.** `min(a, b)`, `max(a, b)`, and `clamp(x, lo, hi)` are defined for every integer and fixed-point type and never fault when `lo <= hi`. `clamp` with constant bounds `lo > hi` is a compile error. At run time, `lo > hi` faults `E_DOMAIN` at the callee name: operation `clamp.checked.<T>`, operands `x`, `lo`, and `hi`, no `exact` and no `limit`.

### 4.9 Conversions and narrowing (Specified)

**IM-50.** There are no implicit conversions between runtime numeric types. Every conversion is written with `as` or one of its forms. This subsection defines conversions between binary integer types; fixed point is in section 5.6, host floating point in section 5.7, and ternary types in section 12.

| Form | Meaning |
|---|---|
| `x as T` | Checked. If `in(T, x)`, the value is preserved; otherwise faults `E_NARROW`. Widening conversions (every source value fits) never fault and need no runtime check. |
| `x as% T` | Wrapping. `wrap(T, x)`. Between types of equal width this is exactly a reinterpretation of the bit pattern. |
| `x as\| T` | Proposed. Saturating: `sat(T, x)`. |
| `x as? T` | Result-returning: `ArithError!T`, holding `.narrow` where `x as T` faults `E_NARROW` (IM-188). |

| Expression | Result |
|---|---|
| `2147483647 as I32` (from `I64`) | 2147483647 |
| `x as I32` with `I64 x = 2147483648` | `E_NARROW`, value 2147483648, target `I32` |
| `x as% I32` with `I64 x = 2147483648` | -2147483648 |
| `x as U64` with `I64 x = -1` | `E_NARROW` |
| `x as% U64` with `I64 x = -1` | 18446744073709551615 |
| `x as I64` with `U64 x = 9223372036854775808` | `E_NARROW` |
| `x as% I64` with `U64 x = 9223372036854775808` | -9223372036854775808 |
| `x as I8` with `I64 x = -129` | `E_NARROW` |
| `x as% I8` with `I64 x = -129` | 127 |
| `x as I128` with `I64 x = m64` | -9223372036854775808 (widening, no check) |

### 4.10 Named operations with specified wide intermediates (Specified)

**IM-51.** Ordinary operators never use a wider intermediate (section 8.3). Where a wider intermediate is needed, it is requested by name. Each named operation is one semantic operation: it counts once for the step ordinal (section 9.3), whatever its lowering.

| Operation | Meaning | Intermediate | Faults |
|---|---|---|---|
| `mul_full(a, b)` | Exact product as the type of width `2w` and the same signedness | `2w` | none |
| `muldiv(T, a, b, c, mode)` | `a * b / c` rounded once by `mode`, result type `T` | at least `2w` bits; the exact product is never narrowed | `E_DIV_ZERO` if `c == 0`; `E_OVERFLOW` if the rounded quotient is not in `T` |
| `isqrt(x)` | `floor(sqrt(x))`; the unique `r >= 0` with `r^2 <= x < (r + 1)^2` | none needed | a fault if `x < 0` (code: see below) |
| `isqrt_round(x, mode)` | `sqrt(x)` rounded by `mode`; ties cannot occur for integer `x`, so all `half_*` modes give the nearest integer. The rounded value is range-checked: `isqrt_round(M64, ceil)` is `3037000500`, in range, but the rule applies to every width. | none needed | as `isqrt`; `E_OVERFLOW` if the rounded value is not in the type |

**IM-52.** Negative input to `isqrt`, `isqrt_round`, and fixed-point `sqrt` (Specified): the operation faults `E_DOMAIN` (code 11) and produces no value. Fixture 75 is Specified.

**IM-53.** `a`, `b`, and `c` in `muldiv` have one common type; `T` may be that type or any other integer type.

| Call | Result |
|---|---|
| `mul_full(m64, m64)` (`I64`) | `I128` 85070591730234615865843651857942052864 |
| `mul_full(M64, M64)` (`I64`) | `I128` 85070591730234615847396907784232501249 |
| `muldiv(I64, M64, M64, M64, floor)` | 9223372036854775807 |
| `muldiv(I64, M64, 3, 2, floor)` | `E_OVERFLOW`, exact (after rounding) 13835058055282163710 |
| `muldiv(I64, M64, 3, 2, ceil)` | `E_OVERFLOW`, exact (after rounding) 13835058055282163711 |
| `muldiv(I64, 7, 5, 2, half_even)` | 18 |
| `muldiv(I64, 9, 5, 2, half_even)` | 22 |
| `isqrt(2^65)` (`I128`) | 6074000999 |
| `isqrt_round(2^65, half_even)` (`I128`) | 6074001000 |
| `isqrt(-1)` (`I64`) | fault `E_DOMAIN` (IM-52) |

## 5. Fixed point

### 5.1 Definition (Specified)

**IM-54.** `Q<i>.<f>` is a signed binary fixed-point type. It is fully determined by its spelling:

| Property | Rule | `Q1.15` | `Q16.16` | `Q32.32` | `Q4.60` |
|---|---|---|---|---|---|
| Storage width `w` | `i + f`; must be 8, 16, 32, or 64 | 16 | 32 | 64 | 64 |
| Storage type | signed integer of width `w` | `I16` | `I32` | `I64` | `I64` |
| Signedness | always signed; `i` counts the sign bit | signed | signed | signed | signed |
| Scale | value = raw / `2^f` | `2^15` | `2^16` | `2^32` | `2^60` |
| Range | `[-2^(i-1), 2^(i-1) - 2^-f]` | `[-1, 1 - 2^-15]` | `[-32768, 32767.9999847412109375]` | `[-2147483648, 2147483647.99999999976716935634613037109375]` | `[-8, 8 - 2^-60]` |
| Resolution | `2^-f` | `2^-15` | 0.0000152587890625 | `2^-32` | `2^-60` |
| Intermediate width for `*`, `/`, `sqrt` | `2w` | 32 | 64 | 128 | 128 |
| Default rounding | `half_even` | | | | |
| Overflow | by operator form: checked faults `E_OVERFLOW`, `%` forms wrap the raw value, `\|` forms saturate | | | | |

**IM-55.** Constraints: `i >= 1`, `f >= 0`. The convention is that `i` includes the sign bit. This is the convention used, for example, in Arm's CMSIS-DSP documentation, which describes `q15_t` data as "1.15 format" [12]. Some vendor documentation names a Q format by its fraction bits alone; for example, Texas Instruments application report SPRA109 calls the 1.15 format "Q15" [13], which is this section's `Q1.15`. `cint-core-1` uses only the sign-included convention: `Q1.15` is bit 15 sign, bits 14 to 0 fraction; `Q16.16` is bit 31 sign, bits 30 to 16 integer, bits 15 to 0 fraction.

**IM-56.** Two `Q` types are the same type only when both `i` and `f` match. `Q16.16` and `I32` are distinct types even though they share storage.

**IM-57.** `x.raw` yields the storage integer (type `I32` for `Q16.16`), and `Q16.16.raw(n)` constructs from a storage integer of exactly the storage type. Both are exact and never fault.

**IM-58.** ABI (Specified requirement on the host section). Every legal `Q<i>.<f>` crosses the C ABI and the Fortran wrappers. A fixed-point type is described there by its storage element and `f`, exactly as by tag `31` of section 11.1, not by a closed list of formats.

**IM-59.** Proposed: storage widths 128 to 1024 (`Q64.64` in `I128`), and unsigned fixed point `UQ<i>.<f>` where `i` counts no sign bit. Adopting widths up to 1024 changes two bounds: `Q1.1023` raises the compile-time `exact` bound of IM-108 from 520 to 640 bytes, and the work buffer of SPEC-09 CINTC-02 from 65 to 80 limbs.

### 5.2 Addition, subtraction, negation, comparison (Specified)

**IM-60.** On operands of the same `Q` type, `+ - +% -% +| -|`, unary `-`, `abs`, `min`, `max`, and comparisons act on the raw storage values with exactly the integer semantics of section 4. No rounding occurs. `Q16.16.raw(2147483647) + Q16.16.raw(1)` faults `E_OVERFLOW`.

### 5.3 Multiplication (Specified)

**IM-61.** For `a`, `b` of type `Q<i>.<f>` with raw values `A`, `B`:

```text
P = A * B                         exact, in an intermediate of width 2w (always fits)
R = round(P / 2^f, mode)          rational rounded once; mode defaults to half_even
result raw = R, then the form rule: checked faults E_OVERFLOW, *% wraps, *| saturates
```

**IM-62.** `a * b` uses `half_even`. `mul(a, b, mode)` names another mode; `mul_wrap(a, b, mode)` and `mul_sat(a, b, mode)` combine a mode with the other forms.

**IM-63.** Multiplying `Q` types with different `i` or `f` is a compile error. Proposed: `qmul(R, a, b, mode)` for mixed operand types with result type `R`, computing `round(A * B / 2^(fa + fb - fr), mode)` exactly.

**IM-64.** Multiplying a `Q` value by a runtime integer is not written with `*` (the types differ). Proposed: `mul_int(a, n)` computes raw `A * n`, checked, exact.

| Operands (`Q16.16`, raw) | Mode | Exact `P / 2^16` | Result raw |
|---|---|---|---:|
| 98304 * 98304 (1.5 * 1.5) | `half_even` | 147456 | 147456 (2.25) |
| 1 * 98304 | `half_even` | 1.5 | 2 |
| 1 * 98304 | `floor` | 1.5 | 1 |
| 1 * 98304 | `trunc` | 1.5 | 1 |
| -1 * 98304 | `half_even` | -1.5 | -2 |
| -1 * 98304 | `trunc` | -1.5 | -1 |
| -1 * 98304 | `half_up` | -1.5 | -1 |
| 1 * 32768 | `half_even` | 0.5 | 0 |
| -1 * 32768 | `half_even` | -0.5 | 0 |
| -1 * 32768 | `half_away` | -0.5 | -1 |
| 16777216 * 8388608 (256.0 * 128.0) | `half_even` | 2147483648 | `E_OVERFLOW` |
| same, `*\|` | `half_even` | 2147483648 | 2147483647 |
| -16777216 * 8388608 (-256.0 * 128.0) | `half_even` | -2147483648 | -2147483648 (-32768.0) |

### 5.4 Division (Specified)

**IM-65.** For `a`, `b` of type `Q<i>.<f>` with raw values `A`, `B`, the quotient `a / b` is:

```text
N = A * 2^f                       exact, intermediate width 2w
R = round(N / B, mode)            mode defaults to half_even
B == 0 faults E_DIV_ZERO before any other check
result raw = R, then the form rule
```

**IM-66.** `a / b` on fixed point is rational division rounded `half_even`. It is not floor division of the raw values, and it differs from integer `/` deliberately: integer `/` is defined as floor (section 4.4), while fixed-point `/` approximates a real quotient on the grid. `div(a, b, mode)` names another mode. `%` is not defined on fixed point.

| `a / b` in `Q16.16` | Mode | Exact raw quotient | Result raw |
|---|---|---|---:|
| 1.0 / 3.0 | `half_even` | 21845.33... | 21845 |
| 2.0 / 3.0 | `half_even` | 43690.66... | 43691 |
| -1.0 / 3.0 | `half_even` | -21845.33... | -21845 |
| -1.0 / 3.0 | `floor` | -21845.33... | -21846 |
| 1.0 / 0.0 | any | | `E_DIV_ZERO` |
| -32768.0 / -1.0 | `half_even` | 2147483648 | `E_OVERFLOW` |

### 5.5 Square root (Specified)

**IM-67.** `sqrt(a, mode)` for `a` of type `Q<i>.<f>` with raw `A >= 0` computes `R = isqrt_round(A * 2^f, mode)` with intermediate width `2w`. `sqrt(a)` uses `half_even`; because `A * 2^f` is an integer, ties cannot occur and every `half_*` mode gives the nearest representable value. `R` is then range-checked against the storage type by the form rule: the checked form faults `E_OVERFLOW` with exact value `R`; `sqrt_wrap` wraps; `sqrt_sat` saturates. For `i >= 2`, `R` is always in range; for `i = 1` (`Q1.f`), the `ceil`, `away`, and `half_*` modes can round up to `2^(w-1)`, which is out of range. `A < 0` faults `E_DOMAIN` (section 4.10, IM-52).

| Input | Mode | Result raw | Value |
|---|---|---:|---|
| `Q16.16` 2.0 (raw 131072) | `floor` | 92681 | 1.4141998291015625 |
| `Q16.16` 2.0 | `half_even` | 92682 | 1.414215087890625 |
| `Q16.16` 0.5 (raw 32768) | `floor` | 46340 | |
| `Q16.16` 0.5 | `half_even` | 46341 | |
| `Q32.32` 2.0 (raw 8589934592) | `floor` | 6074000999 | |
| `Q32.32` 2.0 | `half_even` | 6074001000 | |
| `Q1.15` raw 32767 | `floor` | 32767 | `A * 2^15 = 1073709056`, `isqrt` 32767 inexact |
| `Q1.15` raw 32767 | `ceil` | `E_OVERFLOW`, exact 32768 | |
| `Q1.15` raw 32767 | `away` | `E_OVERFLOW`, exact 32768 | |
| `Q1.63` raw `M64` | `ceil` | `E_OVERFLOW`, exact 9223372036854775808 | |
| `Q16.16` -1.0 | any | fault `E_DOMAIN` (IM-52) | |

### 5.6 Conversions (Specified)

**IM-68.** Conversions to and from fixed-point types:

| Conversion | Meaning | Faults |
|---|---|---|
| `Q<i>.<f>` to `Q<j>.<g>`, `g >= f` | raw `* 2^(g - f)`, exact | `E_NARROW` if out of range (`as`); wraps (`as%`) |
| `Q<i>.<f>` to `Q<j>.<g>`, `g < f` | raw `/ 2^(f - g)` rounded by the `round` clause, default `half_even` | as above, after rounding |
| integer to `Q<i>.<f>` | raw `= n * 2^f`, exact | `E_NARROW` if out of range |
| `Q<i>.<f>` to integer | value rounded by the `round` clause, default `half_even` | `E_NARROW` if the rounded value is out of range |

**IM-69.** The `round` clause is written after the target type: `x as I32 round floor`, `x as% Q16.16 round trunc`.

| Expression | Result |
|---|---|
| `x as Q16.16` with `Q32.32 x = Q32.32.raw(1)` (`2^-32`) | raw 0 (`half_even` of `2^-16`) |
| `x as Q16.16 round ceil` with the same `x` | raw 1 |
| `x as Q32.32` with `Q16.16 x = 1.0` | raw 4294967296 |
| `x as Q16.16` with `Q32.32 x = 32768.0` | `E_NARROW`, exact raw 2147483648 |
| `x as Q16.16` with `Q32.32 x = -32768.0` | raw -2147483648 |
| `x as I32` with `Q16.16 x = -2.5` | -2 |
| `x as I32 round floor` with `Q16.16 x = -2.5` | -3 |
| `x as I32 round half_away` with `Q16.16 x = -2.5` | -3 |
| `n as Q16.16` with `I64 n = 40000` | `E_NARROW` |

### 5.7 Floating-point values at the host boundary (Specified)

**IM-70.** `cint-core-1` has no floating-point type and no floating-point operation. A binary64 or binary32 value enters only through a host-boundary conversion (owned by the host boundary section) with a declared target type, a rounding mode named as in section 4.5, and an overflow policy. Binary64 and binary32 are the IEEE 754 interchange formats [14]. Its meaning is fixed here:

- A finite input denotes its exact rational value. It is rounded once by the declared mode to the target grid, then range-checked by the overflow policy.
- Overflow policy `refuse` (the default): a rounded value out of range is a failure with code `E_NARROW`. Policy `saturate` (named explicitly) clamps to the target range, mirroring `as|`. There is no wrapping policy.
- NaN, positive infinity, and negative infinity are a failure with code `E_NARROW` under both policies, with operand captured as its exact IEEE 754 bit pattern (as a `U64` or `U32`) [14].
- Negative zero converts to 0.

**IM-71.** Failure outcome. A conversion performed by the host boundary before an entry begins (argument marshaling, buffer import) is a refusal: the entry does not start, the context is not faulted, and the refusal carries code `E_NARROW` and the operand. A conversion performed during an entry (for example a recorded effect delivering a float) is a fault (section 9.4).

**IM-72.** The conversion never depends on the host's floating-point rounding mode, flush-to-zero setting, or library.

| Input | Target, mode, policy | Result |
|---|---|---|
| `32768.0` | `Q16.16`, `half_even`, `refuse` | refusal `E_NARROW`, exact raw 2147483648 |
| `32768.0` | `Q16.16`, `half_even`, `saturate` | raw 2147483647 |
| `-0.0` | `Q16.16`, any | raw 0 |
| NaN `0x7ff8000000000000` | any | refusal `E_NARROW`, operand `U64` 9221120237041090560 |

## 6. Reductions

### 6.1 Principle (Specified)

**IM-73.** Integer reductions are not automatically order-independent under checked or saturating arithmetic. With `M = M64`, `(M + 1) + (-1)` faults while `M + (1 + (-1))` returns `M`; for `I8` saturation `sat(sat(120 + 120) - 120) = 7` while `sat(120 + sat(120 - 120)) = 120`. `cint-core-1` therefore defines each reduction as a separate named operation whose result does not depend on how an implementation schedules it.

**IM-74.** The logical order of a reduction over an array or view is row-major order of logical indices (last index varies fastest), independent of memory layout, strides, and view origin.

**IM-75.** Fortran order (Specified consequence). A Fortran array `A(m, n)` presented with shape `[m, n]` (SPEC-03 F-6) is reduced in the order `A(1,1), A(1,2), ...`, while Fortran's array element order is `A(1,1), A(2,1), ...` ([10], section 9.5.3.2, "Array element order"). For `sum`, `sum_wrap`, `min`, `max`, and `count` the order is irrelevant. For `fold_checked` and `sum_sat` it changes the result. For example, with `I8` values `A(1,1) = 100, A(1,2) = -100, A(2,1) = 100, A(2,2) = -100`:

| Order | Sequence | `fold_checked(add, 0, ...)` | `sum_sat(...)` |
|---|---|---|---|
| Row-major (CINT logical order of shape `[2, 2]`) | 100, -100, 100, -100 | 0 | 0 |
| Column-major (Fortran element order) | 100, 100, -100, -100 | `E_OVERFLOW` at index 1, exact 200 | -73 |

**IM-76.** Proposed: ported code obtains Fortran order by reducing a transposed view (`transpose(a)`, a stride swap owned by the arrays section, no copy; a built-in function of SPEC-04 LS-145).

**IM-189.** (Specified; a consequence of IM-30 that adds no behavior.) Checked additions in any order. A program adds values `x_1, ..., x_n` of an integer type `T` into an accumulator of type `T` that starts at `s`, one checked `+` per value, in an order it chooses: a loop, a scrambled loop, or several loops. Each addition returns the exact sum in Z or faults `E_OVERFLOW` (IM-30). So:

1. When the additions complete without a fault, the accumulator holds `s + x_1 + ... + x_n` exactly. Two orders that both complete give the same value.
2. Whether an order faults depends on the order (IM-73). Let `P` be the sum of the positive `x_i` and `N` the sum of the negative `x_i`. Every partial sum in every order lies in `[s + N, s + P]`. When `in(T, s + N)` and `in(T, s + P)`, no order faults, and every order gives the same value.
3. A program can check the bound of item 2 before it adds, with checked operators, so that the check itself faults when the bound fails. With `s = 0` and every `|x_i| <= m`, the bound holds when `n * m <= MAX(T)`, for signed and unsigned `T` alike: one checked `*` before the loop.
4. Each accumulator of a loop that updates several is its own case of this rule.
5. Wrapping additions (`+%`) give `wrap(T, s + x_1 + ... + x_n)` in every order, with no bound (IM-30). Saturating additions (`+|`) depend on the order (IM-73, IM-84). The named reduction `sum` gives the exact sum in every order and checks only its final result (IM-80, IM-82), so it needs no bound on its partial sums.

Example. Over `I8` with `s = 0`, the values `100, 100, -100` fault in the order written (`100 + 100`) and give `100` in the order `100, -100, 100`. Here `P = 200` is not in `I8`, so item 2 does not apply. The values `60, 60, -100` have `P = 120` and `N = -100`, both in `I8`, so every order gives `20`.

### 6.2 Reduction operations (Specified)

**IM-77.** The reduction operations are:

| Operation | Meaning | Order dependence | Faults |
|---|---|---|---|
| `sum(xs)` / `sum(R, xs)` | Exact sum in Z, then one final range check against the result type `R` (default: element type) | none; any order or tree is conforming | `E_OVERFLOW` only at the final range check |
| `fold_checked(add, init, xs)` / `fold_checked(mul, init, xs)` | `acc = init; for each x in logical order: acc = acc + x` (or `*`), each step checked in the element type | specified left-to-right | `E_OVERFLOW` at the first step whose exact result is out of range |
| `sum_wrap(xs)` | Sum modulo `2^w` of the element type | none (modular addition is associative and commutative) | none |
| `sum_sat(xs)` | `acc = 0; for each x in logical order: acc = acc +\| x` | specified left-to-right; not reassociated | none |
| `min(xs)`, `max(xs)` | Least or greatest element | none | `E_SHAPE` if `xs` is empty, with operand `I64 0` (`n`), no `exact`, and no `limit` |
| `min(init, xs)`, `max(init, xs)` | As above, with `init` included | none | none |
| `count(mask)` | Number of `true` elements of a `Bool` array, as `I64` | none | none |

Every fault of a reduction, `E_FUEL` at its charge included (IM-141), is positioned at the callee name, in sequential code and in kernels, where the callee is the one in the `reduce` statement (SPEC-02 F-8); the records are those of this table, IM-81, and IM-83.

**IM-78.** The final step of `sum` is a range check on an exact value, the same as a checked arithmetic result, and therefore raises `E_OVERFLOW`, not `E_NARROW`. `E_NARROW` is reserved for conversions (`as` and host-boundary conversion).

**IM-79.** Empty inputs: `sum` returns 0 in the result type; `fold_checked` returns `init`; `sum_wrap` and `sum_sat` return 0; `count` returns 0.

```c
I8[3]   v = [120, 120, -120];
I8      a = sum(v);                    // 120: exact total 120 fits I8
I8      b = fold_checked(add, 0, v);   // E_OVERFLOW at index 1, exact 240
I8      c = sum_sat(v);                // 7
I8      d = sum_wrap(v);               // 120
I8[3]   u = [127, 127, 127];
I16     e = sum(I16, u);               // 381: one final range check against I16
I8      f = sum(u);                    // E_OVERFLOW, exact 381
```

### 6.3 Accumulator-width bound for `sum` (Specified)

**IM-80.** For `n >= 1` signed `w`-bit inputs, every partial and final sum lies in `[n * MIN(T), n * MAX(T)]`, which is contained in the range of a signed integer of `w + ceil(log2(n))` bits, because `n * 2^(w-1) <= 2^(w - 1 + ceil(log2(n)))`. For unsigned `w`-bit inputs, every partial sum is at most `n * (2^w - 1) < 2^(w + ceil(log2(n)))`, so an unsigned accumulator of `w + ceil(log2(n))` bits suffices. An implementation may use any accumulator at least this wide, in any association order, and by the bound above never overflows internally; or it may use any other method that yields the exact sum.

Since `n <= M64`, `ceil(log2(n)) <= 63`. For `I64` and narrower inputs an `I128` accumulator is always sufficient. For `I1024` inputs, an implementation needs up to 1087 bits internally; this width is internal and never appears as a program type.

**IM-81.** The final range check against `R` is checked. Its fault record captures `n`, the exact sum in Z, and `R`'s bound; it does not capture every element. The record has one operand, `n` as an `I64`, the exact sum as `exact`, and the bound crossed as `limit`. The view identity (section 11.1, tag `61`) is not a canonical field until arrays in `.ci` programs have canonical identifiers (SPEC-03), as SPEC-02 15.5 already shows.

### 6.4 Canonical faults and parallel evaluation (Specified)

- **IM-82.** `sum` and `sum_wrap`: no intermediate fault exists, so every evaluation order yields the same result.
- **IM-83.** `fold_checked`: the canonical fault is at the least index `j` at which the left fold leaves the range. For `add`, the accumulator after element `j` (with no earlier fault) is `init + x_0 + ... + x_j` exactly, so a parallel implementation may compute exact prefix sums [15] in a wide accumulator and report the least `j` whose prefix is out of range. The fault record captures `j`, the accumulator before step `j`, `x_j`, and the exact out-of-range result: operands `j` as an `I64`, then the accumulator and `x_j` in the element type, `exact` the out-of-range result, and `limit` the bound crossed.
- **IM-84.** `sum_sat`: the result is the left-to-right value. Any parallel method must reproduce it exactly; reassociating `+|` is nonconforming.
- **IM-85.** `min`, `max`, `count`: associative and commutative; any order.

**IM-86.** Proposed note for implementers (`sum_sat` as a scan). The method is a scan in the sense of Blelloch [15], with function composition as the associative operator. Represent each step as a function `f(x) = clamp(x + a, l, h)` with three free parameters: an offset `a` in Z and bounds `l <= h` in `T`. Element `x_j` is the function with `a = x_j`, `l = MIN(T)`, `h = MAX(T)`. These functions are closed under composition: applying `f1 = (a1, l1, h1)` and then `f2 = (a2, l2, h2)` gives

```text
a = a1 + a2
l = clamp(l1 + a2, l2, h2)
h = clamp(h1 + a2, l2, h2)
```

The composed `l` and `h` stay in `T`, but `a` grows to at most `n * 2^(w-1)` in magnitude, so it needs `w + ceil(log2(n))` bits. Applying the composition of all steps to 0 gives the `sum_sat` result. Check with `I8`: composing `+120` then `-120` gives `a = 0`, `l = -128`, `h = 7`, so `f(0) = 0`, `f(10) = 7`, `f(-10) = -10`, which matches the sequential definition. With `l` and `h` fixed at the type bounds the functions are not closed, and a scan built that way is nonconforming.

### 6.5 Further reductions (Proposed; IM-87 Specified for integer views)

- **IM-87.** (Specified for rank-1 views of one integer element type; fixed-point `dot` waits for a rounding rule and stays Proposed.) `dot(R, a, b)`: exact sum of elementwise products, followed by one final range check against `R`. Sufficient accumulator: `2w + ceil(log2(n))` bits, signed for signed operands and unsigned for unsigned operands. For signed operands each product lies in `[-(2^(2w-2) - 2^(w-1)), 2^(2w-2)]`, so the sum lies within `+/- n * 2^(2w-2)`, which fits; the bound is attained in width at `n = 1` by `MIN(T) * MIN(T)`. The ternary `tdot` with a declared accumulation width is owned by the ternary section and follows the same pattern. `a` and `b` are rank-1 views of one integer element type `E`. Unequal extents fault `E_SHAPE` before the fuel charge, operation `dot.checked.<E>.<R>`, with operands `I64 0` (the dimension) and the extent of `b` as an `I64`, no `exact`, and the extent of `a` as `limit`. The final range check faults `E_OVERFLOW` with the record of IM-81. Both faults are at the callee name (IM-77), and the reduction charges `ceil(n / 64)` (IM-139).
- **IM-88.** Axis reductions (`sum(R, xs, axis)`) with the same meanings per array slice; owned jointly with the arrays section.
- **IM-89.** `fold_checked` with a user function, evaluated left to right.

## 7. Integer formatting (Specified)

**IM-90.** The bare string statement (`"x={x}\n";`, statements section) formats values exactly:

| Type | Format | Examples |
|---|---|---|
| Integer, any width | Decimal; `-` for negatives; no `+`, no grouping, no leading zeros | `-9223372036854775808`, `0` |
| Fixed point | Exact decimal expansion of raw / `2^f` (always finite); at least one fractional digit; no trailing zeros beyond the first fractional digit | `Q16.16.raw(1)` prints `0.0000152587890625`; `2.0` prints `2.0`; `Q16.16.raw(-1)` prints `-0.0000152587890625` |
| Bool | `true`, `false` | |

**IM-91.** No value is ever printed in scientific notation or rounded for display by the default format. Other format specifiers (hexadecimal, padding, raw) are owned by the statements section.

## 8. Evaluation order

### 8.1 Expressions (Specified)

- **IM-92.** Operands are evaluated left to right, each completely (including its faults) before the next begins. The operator is applied after both operands are evaluated.
- **IM-93.** Function call: the callee designator, then each argument left to right, then the call.
- **IM-94.** `&&`, `||`, and `c ? a : b` evaluate only the operands the result requires.
- **IM-95.** No operand is evaluated twice, and none is skipped by optimization if its evaluation could fault.

**IM-96.** Consequently the first fault in evaluation order is the fault:

| Expression | Values | Fault |
|---|---|---|
| `(x + 1) / y` | `x = M64`, `y = 0` | `E_OVERFLOW` (left operand first) |
| `a[i] + (1 / z)` | `a` has length 5, `i = 10`, `z = 0` | `E_BOUNDS` |
| `f(g(), 1 / z)` | `g` faults `E_SHIFT`, `z = 0` | `E_SHIFT` |

### 8.2 Statements and stores (Specified)

- **IM-97.** Assignment, compound assignment, `++`, and `--` are statements, not expressions. They have no value and cannot appear inside an expression.
- **IM-98.** An assignment `place = expr` evaluates the place's subexpressions (array, indices) left to right and checks them (`E_BOUNDS` and the like) first, then evaluates `expr`, then stores. `place op= expr` reads the place once, at the point where the place is checked.
- **IM-99.** The faulting statement's own store does not occur. Stores made by statements that completed before the fault remain, including stores made by completed statements inside callees that the faulting statement invoked.
- **IM-100.** In sequential code, stores become observable in program order. An implementation may reorder or eliminate stores only when no execution, including one that faults later, can observe the difference in canonical state (section 9.5).

```c
I64 counter = 0;                 // module state
I64 g() { counter = counter + 1; return 5; }
void step(I64 z) {
    I64 y = 7;
    y = g() + 1 / z;             // z = 0: E_DIV_ZERO. counter is 1 (g's statement completed); y is not stored.
}
```

### 8.3 Explicit temporaries (Specified)

**IM-101.** Every intermediate result has the static type of its operation. There is no integer promotion, no evaluation in a wider precision, and no implicit widening to `I64`:

```c
I32 a = 2_000_000_000;
I32 b = 2;
I32 c = a * b / 4;                         // E_OVERFLOW at a * b, even though the final value 1_000_000_000 fits
I32 d = ((a as I64) * (b as I64) / 4) as I32;   // 1000000000
I32 e = muldiv(I32, a, b, 4, floor);       // 1000000000
```

**IM-102.** A wider intermediate exists only where this section names one: fixed-point `*`, `/`, `sqrt` (section 5), `mul_full`, `muldiv`, `isqrt` (section 4.10), and `sum`, `dot` (section 6). The compiler may introduce internal temporaries, but they are never observable.

### 8.4 Compile-time execution (Specified)

**IM-103.** Constant expressions (section 3.2) and compile-time execution use exactly the semantics of run time: expressions are typed first and then evaluated with the checked, wrapping, saturating, shift, and fixed-point rules of sections 4 and 5. Folding an expression at compile time never changes its value or fault. A fault during compile-time execution is a compile error carrying the same fault code and operand capture. Compile-time execution cannot read clocks, the environment, or undeclared files. This is the rule stated by SPEC-09 SEED-08 for the seed compiler.

## 9. Fault model

### 9.1 Fault codes (Specified)

**IM-104.** The fault codes are:

| Code | Number | Raised by | Defined in |
|---|---:|---|---|
| `E_OVERFLOW` | 1 | Checked arithmetic, `abs`, fixed-point arithmetic and `sqrt`, `sum` final range check, `fold_checked`, `muldiv`, `div_round`, `isqrt_round` | this section |
| `E_DIV_ZERO` | 2 | All division and remainder operations with a zero divisor | this section |
| `E_BOUNDS` | 3 | Index outside a view's extent | arrays and views |
| `E_SHAPE` | 4 | Shape mismatch; `min`/`max` of an empty input; dispatch whose work-item count exceeds `M64` (section 9.3) | arrays and views; this section for reductions and dispatch admission |
| `E_SHIFT` | 5 | Shift, rotation, or rescale count outside the operation's declared range (`0..w-1` for the binary shifts of section 4.6; other sections declare their own ranges) | this section; ternary section for `rescale3` and `shl3` |
| `E_NARROW` | 6 | Checked conversion out of range; failed host-boundary float conversion during an entry (section 5.7) | this section |
| `E_ALIAS` | 7 | Writable output overlapping another operand | kernels |
| `E_STALE_HANDLE` | 8 | Handle whose arena generation has ended | arenas |
| `E_FUEL` | 9 | Fuel allowance exhausted at a charge point | this section |
| `E_UNSUPPORTED` | 10 | Backend cannot implement a type, operation, layout, or call depth exactly (section 9.7). It is never used for a type error in the program, which is a compile error. | this section and backends |
| `E_DOMAIN` | 11 | `isqrt`, `isqrt_round`, and fixed-point `sqrt` of a negative value (IM-52); `clamp` with `lo > hi` at run time (IM-49) | this section |
| `E_DEPTH` | 12 | A user-function call that would exceed the entry's call-depth limit (section 9.4) | this section |
| `E_ASSERT` | 13 | A failed `assert` outside a test block (SPEC-04 8.9). Operation `assert.checked.bool`; position the first character of the condition; operands the two operands of the condition when it is a comparison, none otherwise; the message is not evaluated | language surface |

The numbers are the canonical encoding (section 11.2).

**IM-105.** Not faults (Specified). Exhaustion of host resources (memory for a host allocation, file or device errors, host callback failures) is not an integer-machine fault. It is a host-boundary outcome owned by the host section, it has no canonical fault record, and an execution that ends that way has no execution identity (section 11.4). Writes through a read-only view are a compile error where static; the run-time case is Open (section 15, question 12).

### 9.2 Fault record and exact operand capture (Specified)

**IM-106.** Every fault produces one fault record with these canonical fields:

| Field | Content |
|---|---|
| `code` | Fault code. |
| `operation` | Operation identifier (section 9.8), for example `add.checked.i64`, `as.checked.i64.i32`, `mul.checked.q16_16.half_even`. At most 64 ASCII bytes. |
| `operands` | Every operand as a typed value, exactly (never rounded or abbreviated), in source order, at most 8. For reductions: `n` and the view identity (tag `61`), and for `fold_checked` additionally the index, accumulator, and element. |
| `exact` | The value after the operation's single rounding (if it rounds) and before the range check, as Z. For non-rounding operations this is the exact mathematical result. For `muldiv`, `div_round`, fixed-point `*`, `/`, `sqrt`, conversions with a `round` clause, and host-float conversion, it is the rounded integer (raw value for fixed point). For a conversion from an integer to a fixed-point type it is the scaled raw value n * 2^f, and `limit` is the target's bound as a fixed-point value (tag `31`, whose payload is the raw value), so both are in raw units: `n as Q16.16` with `I64 n = 40000` records operand `I64 40000`, exact 2621440000, and limit `Q16.16` raw 2147483647. The name is historical: the field holds the value that failed the range check, which for a rounding operation is already rounded. Present for `E_OVERFLOW` and `E_NARROW` from a finite value; absent for `E_DIV_ZERO`, `E_SHIFT`, `E_DOMAIN`, non-finite floats, and faults of other sections unless they define it. |
| `limit` | The violated bound: `MAX(T)` or `MIN(T)` of the target, the largest permitted shift count (`I64(w - 1)` for every out-of-range count, IM-45), the extent (IM-186), the fuel allowance, the depth limit. |
| `position` | Source file (module-relative path, at most 1024 UTF-8 bytes [16]), line, and column (1-based, column in Unicode scalar values [6]) of the operator token or call. |
| `revision` | Program revision identity (section 11.4) to which the position refers. |
| `address` | Present only for faults of a kernel dispatch: the kernel's qualified name, the dispatch number, and the phase (`entry`, `work-item`, or `epilogue`, SPEC-02 F-3); in the `work-item` phase only, also the work-item index and step ordinal (section 9.3). Absence of the address, and of the work-item fields within it, is encoded by presence bytes, never by a sentinel value. |
| `stack` | Call-site positions of the active user-function calls, outermost first (the stack rule below); at most `D - 1` positions under depth limit `D` (section 9.4). |

**IM-107.** Stack (Specified). The `stack` field holds one position per active user-function call: the position of that call's call site, which is the callee name (SPEC-04 LS-278), outermost first. The entry (an entry function, a test block, or a script's implicit entry; section 9.4) is a host call. It has no call site and contributes no position. A fault in the entry's own body therefore has an empty stack, and a fault `k` user calls below the entry has `k` positions. The `E_DEPTH` call that faults is not entered (section 9.4), so its own position is the record's `position` and is not in `stack`. The `.expect` layer records the number of positions as `fault.stack-depth` (SPEC-09 9.2), and from format 2 on also each position, as a `fault.stack` line (SPEC-09 CONF-11).

**IM-108.** Size bounds (Specified). Three bounds hold, by field and by record kind (IM-148).

1. Run-time records, and fault records embedded in state or a checkpoint (IM-156): every Z is at most 257 bytes. The largest run-time `exact` or Z operand in `cint-core-1` is the 2048-bit product inside an `I1024` `muldiv` or checked `*`. A `sum` over `I1024` inputs needs at most 1087 bits (136 bytes), and a run-time conversion source at most 136 bytes (`I1024` to `Q1.63`, 1,087 bits). No run-time record carries a literal's Z, because a checked conversion of a literal is decided at compile time (IM-23).
2. Compile-time diagnostic records (SPEC-04 C6001 and C6002) of encoding `cint-core-1/fault/v2`: an operand Z is at most 513 bytes, which holds the largest literal (a magnitude of at most 2^4096 - 1, SPEC-04 LS-31, and a sign).
3. In the same records, `exact` is at most 520 bytes. The largest scaled raw value of an integer-to-fixed-point conversion (IM-106) is (2^4096 - 1) * 2^63, for `Q1.63`: the widest fraction in `cint-core-1` is 63 (storage widths 8 to 64, IM-54, at least one integer bit, and `f <= w - 1`, IM-146), and the value has 4,159 magnitude bits. An integer target gives an `exact` of at most 513 bytes.

A single 520-byte bound is not used, because it would accept operand lengths of 514 to 520 bytes that no valid literal produces. Under the encoding `cint-core-1/fault/v1` every Z is at most 257 bytes. These bounds are arithmetic on the encoding, not measurements. In decimal, a 520-byte Z has at most 1,252 digits.

**IM-109.** Redaction (Specified). A value whose static type carries the `secret` qualifier (owned by the security section, SEC-SD-1 and SEC-SD-2) is never captured. In its place the record holds a redacted operand: the type descriptor of the value with no payload (tag `7f`, section 11.1). `exact` is also redacted when any operand of the faulting operation is secret. Redaction depends only on static types, so every conforming implementation produces the same bytes, and redacted fault records remain part of execution identity. The diagnostic presentation prints `<secret>`.

**IM-110.** Diagnostic presentation follows this example:

```text
E_OVERFLOW at flight.ci:42:17

operation: add.checked.i64
left:      9223372036854775800
right:     12
result:    9223372036854775812
maximum:   9223372036854775807
```

For a rounding operation (`muldiv`, `div_round`, fixed-point `*`, `/`, and `sqrt`, a `round` clause, or a host-float conversion), the line of the `exact` field is labeled `rounded:` instead of `result:`, because the value is already rounded (IM-106).

**IM-111.** Native addresses, register contents, wall-clock time, thread identifiers, and backend names are never part of the canonical record. They may be attached as non-canonical diagnostics.

### 9.3 Canonical ordering and logical address (Specified)

**IM-112.** Sequential code has exactly one fault per execution: the first in evaluation order (section 8). Where several faults can occur in one logical step, the canonical fault is the least in this order:

```text
dispatch number  ->  work-item index  ->  step ordinal
```

- Dispatch number: `I64`, counting kernel dispatches within the current entry (section 9.4) from 0, in program order. It restarts at 0 at every entry, so it is not part of canonical state and is unaffected by checkpoint and restore.
- Work-item index: `I64`, the row-major linearization of the work item's index in the kernel's iteration space (for `kernel K[n, m]`, item `(i, j)` has index `i * m + j`). Dispatch admission checks, before any work item runs and before the dispatch charges fuel, that the product of all extents is at most `M64`; otherwise the dispatch faults `E_SHAPE` with operation `dispatch.admit`, the extents as operands, and the exact product. A backend may additionally refuse dispatches above its own limit (for example `2^32` work items) with `E_UNSUPPORTED`.
- Step ordinal: `I64`, defined at the source level (below), independent of any IR or lowering.
- Phase: within one dispatch, SPEC-02 F-3 orders faults first by phase (entry checks, then work-items, then the reduction epilogue). Entry-phase and epilogue-phase faults carry the dispatch number and phase but no work-item index or step ordinal (section 9.2).

**IM-113.** Step ordinal. Within one work item, each evaluation of a counted operation, in the evaluation order of sections 8.1 and 8.2 (including evaluations inside called functions), receives the next ordinal starting at 0. The step ordinal of a fault is the ordinal of the faulting evaluation. Counted operations:

| Counted (1 each evaluation) | Not counted |
|---|---|
| Every arithmetic operator of every form: binary `+ - * / % << <<% >>` with their `%` and `\|` forms, unary `-` | Literals, variable reads, comparisons, `& \| ^ ~`, `&&`, `\|\|`, `!`, `?:` itself |
| Every conversion `as`, `as%`, `as\|`, `as?`, including widening and fixed-point conversions | Loop control, fuel charges |
| Every subscript access `v[i]` or `v[i, j]`, whether it reads or is the place of a store, and every view-forming operation of the arrays section | Statements without a counted operation |
| Every call of a named operation of sections 4, 5, and 6 and of the ternary section (one count per call, whatever its internal work) | |
| Every call of a user function (counted at the call, before the callee's operations) | |

**IM-114.** An operation counts even when the compiler has proven that it cannot fault, and whatever its lowering. For example, in the kernel body `out[i] = a[i] + b[i] * c[i];` with the `+` overflowing: `out[i]` is 0 (place first, section 8.2), `a[i]` 1, `b[i]` 2, `c[i]` 3, `*` 4, `+` 5; the step ordinal is 5. For example, in `I64 y = (x as I64) + a[i] / z;` with `z = 0`: `as` 0, `a[i]` 1, `/` 2; the step ordinal is 2.

**IM-115.** This is a specification choice, not a claim about which lane physically failed first. An accelerated backend must report the canonical fault's code, operation, operands, exact result, position, dispatch number, and work-item index. It may leave the step ordinal to be filled in by re-executing that work item on the reference implementation, which is valid when the work item's behavior is independent of other work items; the kernel section defines when a workgroup or dispatch must be replayed instead.

**IM-116.** Selecting the canonical work item (Proposed). An admissible method on a GPU is: each workgroup records its least faulting work-item index in workgroup memory, writes it to a per-workgroup slot, and a second reduction pass takes the minimum over slots. This needs no 64-bit atomics. A backend whose atomics or index registers are 32 bits wide may restrict dispatches to fewer than `2^32` work items and refuse larger ones with `E_UNSUPPORTED` at admission.

Reductions use the rules of section 6.4.

### 9.4 Sticky faults, entries, and recoverable errors (Specified)

**IM-117.** An entry is one host call into a context: an entry-point call made through the host boundary. Each entry carries its own fuel allowance (section 10.1) and call-depth limit. Dispatch numbers and fuel consumed are counted per entry.

**IM-118.** Faults are not exceptions. Program code cannot catch, inspect, or resume from a fault. A fault ends the current entry and sets the context's fault record. The fault record is sticky: further entry into that context is refused, and the host receives the same fault record, until the host explicitly resets the context through the host boundary. Reset clears the fault record and alters no other state; because the fault record is part of canonical state (IM-156), clearing it changes the state hash. A host that wants an earlier state restores a checkpoint. The context is the only fault boundary: no construct lets program code observe a fault raised in its own context, and a fault is handled by the program that supervises the context, through the host boundary.

**IM-119.** Call depth. Each entry has a depth limit `D` (an `I64`, `D >= 1`), part of its semantic input. The entry function has depth 1. A user-function call that would make the depth exceed `D` faults `E_DEPTH` before the callee's fuel charge, so it consumes no fuel. An implementation must provide native resources for `D` frames of the program, or refuse the entry with `E_UNSUPPORTED` before execution. Proposed default: `D = 256`.

**IM-120.** Recoverable conditions are values, not faults. Arithmetic that may legitimately fail is written with the result-returning forms (`add_result`, `div_result`, `as?`, and the others in section 4.1), which return `ArithError!T`, and is handled with `try` or `catch` (SPEC-04 LS-96):

```c
ArithError!I64 safe_mean(I64 total, I64 n) {
    I64 q = try div_result(total, n);    // a zero n gives ArithError.div_zero, not the fault E_DIV_ZERO
    return q;
}
```

### 9.5 State observable after a fault (Specified)

**IM-121.** After a fault in a context:

| Item | Observable value |
|---|---|
| Module state and buffers written by sequential code | All stores of completed statements, including statements in callees invoked by the faulting statement; not the faulting statement's own store (section 8.2) |
| Kernel outputs of the faulting dispatch | Unpublished: the previous contents remain (kernel section) |
| Explicitly named in-place kernel operations | As defined by that operation's failure contract (kernel section); no rollback is implied |
| Fault record | Canonical, section 9.2 |
| Fuel consumed by the entry | Sum of the charges that succeeded; a charge that faults `E_FUEL` is not counted (section 10) |
| Local variables of active frames | Diagnostic only, through the workbench inspector; not part of canonical state. An accelerated backend may obtain them by reference re-execution. |

**IM-122.** Sequential code does not roll back stores. Programs that need all-or-nothing state transitions use double buffering or kernels, which publish on success. `cint-core-1` provides them only through publish-on-success kernels or explicit double buffering, not for sequential code.

### 9.6 Implementation obligations (Specified)

- **IM-123.** A C backend must not compute an overflowing signed result and test it afterward; it must use operations defined for all inputs (for example `__builtin_add_overflow` in GCC [17] and Clang [18], or range checks before the operation). `-fwrapv` [19] is not by itself a conforming strategy because the fault must still be detected.
- **IM-124.** Division must special-case `b == 0` and `MIN / -1` before any native division instruction.
- **IM-125.** Shift counts must be checked before any native shift instruction.
- **IM-126.** A fault must be detected before any store that depends on the faulting value.
- **IM-127.** Recursion must be bounded by the depth limit of section 9.4; a native stack overflow is nonconforming.

**IM-128.** Scope of the absence of undefined behavior. This section claims only that the operations it defines have a defined result or a defined fault for every input. The claim that emitted code evaluates no C operation with undefined behavior, and the assumptions under which it holds, are stated once, in SPEC-09 DISC-03; this section adds nothing to that scope.

### 9.7 Backend capability (Specified)

**IM-129.** A backend that cannot implement a type, width, operation, or view layout exactly must refuse with `E_UNSUPPORTED`: at build time when the need is static, otherwise at dispatch entry before any work item runs. It must never narrow, approximate, or substitute a different rounding. It must never introduce a copy that is visible at the API: a borrow that is silently a copy, a placement transfer the program did not request, or an output published by copying where the program requested in-place. Internal temporaries, tiling through workgroup or shared memory, gathering a strided view into scratch, and staged outputs are permitted when they change no value, fault, or API-visible behavior (section 8.3).

Example: WGSL's concrete scalar types are `i32`, `u32`, `f32`, `f16`, and `bool`; its 64-bit `AbstractInt` type exists only for expressions evaluated at shader-creation time (sections 6.2.1 and 6.2.5) [3]. WGSL has no 64-bit integer type a shader can use at run time. A WGSL backend either lowers `I64` to an exactly equivalent multiword sequence that passes the conformance suite, using the as-if rule of section 2.2 where proven, or refuses `I64` kernels.

### 9.8 Operation identifiers (Specified)

**IM-130.** The `operation` field of a fault record, the `op` key of CIF-1 fixtures, and the `op` line of `.expect` files use this grammar. All identifiers are lowercase ASCII.

```text
op-id     = name "." form 1*("." type) ["." mode]  /  sys-id
form      = "checked" / "wrap" / "sat" / "result" / "add" / "mul"
type      = "i8" / "i16" / "i32" / "i64" / "i128" / "i256" / "i512" / "i1024"
          / "u8" / "u16" / "u32" / "u64" / "bool" / "t1" / "t27" / "pt5" / "pt4"
          / "q" 1*DIGIT "_" 1*DIGIT                 ; Q16.16 is q16_16
mode      = a rounding mode name of section 4.5
sys-id    = "fuel.charge" / "call.enter" / "dispatch.admit" / "index.checked." ( type / "struct" )
          / "slice.checked." ( type / "struct" )   ; IM-186
          / "copy.shape"                            ; IM-187
          / "copy.alias"                            ; IM-187
          / "decl.shape"                            ; SPEC-04 LS-62
          / "assert.checked.bool" / "format.char." type
          / "unassigned"                            ; IM-184
          / "bind." check-name                      ; kernel entry checks, SPEC-02 F-8
          / "arena.alloc" / "arena.reset" / "pool.deref" / "handle.check"   ; SPEC-03 M-14 to M-19
          / "pool.insert"                          ; SPEC-03 M-19
          / "host." 1*(ALPHA / DIGIT / "_" / ".")   ; boundary records, SPEC-03 A-6 (Proposed)
check-name = "stale" / "permission" / "type" / "scalar" / "placement" / "shape"
          / "where" / "injective" / "alias" / "domain" / "limit"
```

Rules:

- **IM-131.** `form` is the operator form. `add` and `mul` occur only as the folded operator of `fold_checked`.
- **IM-132.** The first `type` is the operand type. A second `type` is present exactly when the result type differs from the operand type (conversions: source then target; `sum`: element then result; `muldiv`: operand then result; `mul_full` and `uabs`: operand then result).
- **IM-133.** `mode` is present exactly when the operation rounds, whether the mode was written or defaulted. The record always names the mode actually used.
- **IM-184.** (Specified.) A compile-time checked conversion whose source is a literal's value in Z (`300 as I8`, IM-26) has the operation `unassigned` in `cint-core-1`, because Z has no type component; a `z` type token (`as.checked.z.i8`) is noted for a later profile (OQ-124). So format 2 of the `.expect` text freezes literal conversions with the operation `unassigned`, a known gap.
- **IM-185.** (Specified.) `call.enter` with `E_UNSUPPORTED` records an entry whose depth limit `D` exceeds the frames an implementation provides (IM-119), before its fuel charge: operand `I64 D`, limit the implementation's frame count as an `I64` (1,024 in `cint-rt-1`; an interim value until IM-119's resources are Specified). `dispatch.admit` with `E_UNSUPPORTED`, no operands, no exact, and no limit is also what a runtime fault writer records when it is called outside its contract, so a failed helper always leaves the context faulted. `0 -% a` never faults and needs no identifier. `shl_wrap` checks its count and records `shl.wrap.<T>` (IM-45). A shift count of any integer type is recorded with its own type, and the identifier names only the left operand's type.

**IM-134.** The identifier of each source form:

| Source form | Identifier pattern | Example |
|---|---|---|
| `a + b`, `a +% b`, `a +\| b`, `add_result(a, b)` | `add.<form>.<T>` | `add.checked.i64` |
| `-`, `*` (integer) | `sub.<form>.<T>`, `mul.<form>.<T>` | `mul.wrap.i8` |
| unary `-a` | `neg.checked.<T>` | `neg.checked.i64` |
| `a / b`, `a % b`, `div_result`, `rem_result` | `div.<form>.<T>`, `rem.<form>.<T>` | `div.checked.i64` |
| `div_trunc`, `rem_trunc`, `div_euclid`, `rem_euclid`, `divmod` | `<name>.checked.<T>` | `rem_euclid.checked.i64` |
| `div_round(a, b, mode)` | `div_round.checked.<T>.<mode>` | `div_round.checked.i64.half_even` |
| `a << k`, `a <<% k`, `shl_result` | `shl.<form>.<T>` | `shl.wrap.u8` |
| `a >> k` | `shr.checked.<T>` | `shr.checked.i64` |
| `rotl`, `rotr` | `rotl.checked.<T>` | `rotl.checked.u32` |
| `abs`, `uabs` | `abs.checked.<T>`, `uabs.checked.<T>.<U>` | `uabs.checked.i8.u8` |
| `min(a, b)`, `max(a, b)`, `clamp` | `min.checked.<T>` | `clamp.checked.i32` |
| `as`, `as%`, `as\|`, `as?` | `as.<form>.<S>.<T>[.<mode>]` | `as.checked.q16_16.i32.floor` |
| `mul_full` | `mul_full.checked.<T>.<T2>` | `mul_full.checked.i64.i128` |
| `muldiv(T, a, b, c, mode)` | `muldiv.checked.<S>.<T>.<mode>` (second type always present) | `muldiv.checked.i64.i64.floor` |
| `isqrt`, `isqrt_round` | `isqrt.checked.<T>`, `isqrt_round.checked.<T>.<mode>` | `isqrt_round.checked.i128.half_even` |
| fixed-point `*`, `mul`, `mul_wrap`, `mul_sat` | `mul.<form>.<Q>.<mode>` | `mul.checked.q16_16.half_even` |
| fixed-point `/`, `div` | `div.<form>.<Q>.<mode>` | `div.checked.q16_16.floor` |
| fixed-point `sqrt`, `sqrt_wrap`, `sqrt_sat` | `sqrt.<form>.<Q>.<mode>` | `sqrt.checked.q1_15.ceil` |
| `sum(R, xs)` | `sum.checked.<E>.<R>` (second type always present) | `sum.checked.i8.i16` |
| `dot(R, a, b)` | `dot.checked.<E>.<R>` (second type always present) | `dot.checked.i8.i64` |
| `fold_checked(add, init, xs)` | `fold_checked.<add\|mul>.<E>` | `fold_checked.add.i8` |
| `sum_wrap`, `sum_sat` | `sum_wrap.wrap.<E>`, `sum_sat.sat.<E>` | `sum_sat.sat.i8` |
| `min(xs)`, `max(xs)`, `count(mask)` | `reduce_min.checked.<E>`, `reduce_max.checked.<E>`, `count.checked.bool` | `reduce_min.checked.i64` |
| host float conversion | `from_f64.<checked\|sat>.<T>.<mode>`, `from_f32...` | `from_f64.checked.q16_16.half_even` |
| `E_FUEL`, `E_DEPTH`, dispatch admission | `fuel.charge`, `call.enter`, `dispatch.admit` | |
| element read or write place check (`E_BOUNDS`), in sequential code and kernels | `index.checked.<E>`, with `struct` for a struct element (IM-186) | `index.checked.i32` |
| a slice whose bounds are not valid (`E_BOUNDS`, SPEC-04 LS-161) | `slice.checked.<E>`, with `struct` for a struct element (IM-186) | `slice.checked.i64` |
| array assignment, array initializer, array-field construction, or `copy(dst, src)` with unequal run-time extents (`E_SHAPE`, IM-187) | `copy.shape` | `copy.shape` |
| array assignment or `copy(dst, src)` whose source and destination overlap at run time (`E_ALIAS`, SPEC-04 LS-70, IM-187) | `copy.alias` | `copy.alias` |
| an array declaration whose extent evaluates below 0 (`E_SHAPE`, SPEC-04 LS-62) | `decl.shape` | `decl.shape` |
| `assert(cond)` outside a test block (`E_ASSERT`) | `assert.checked.bool` | `assert.checked.bool` |
| a `{x:c}` hole whose value is not a Unicode scalar value (`E_NARROW`, SPEC-04 9.3) | `format.char.<T>` | `format.char.i64` |
| checked conversion of a literal source (compile time only, IM-184) | `unassigned` | |
| kernel entry checks (SPEC-02 F-5) | `bind.<check-name>` | `bind.alias` |
| arena and pool operations (SPEC-03) | `arena.alloc`, `arena.reset`, `pool.deref`, `pool.insert`, `handle.check` | `pool.deref` |
| ternary digit operations (SPEC-05 TR-DIG-0a) | `trit.checked.<T>`, `shl3.checked.<T>`, `rescale3.checked.<T>`, `rescale3_rem.checked.<T>` | `rescale3.checked.i64` |
| ternary dot product (SPEC-05 TR-DOT-3) | `tdot.checked.<W>.<X>`; the accumulator type is carried by `limit` | `tdot.checked.pt5.i8` |
| packed and ternary decoding (SPEC-05 TR-VAL-2) | `decode.checked.<pt5\|pt4\|t1\|t27>` | `decode.checked.pt5` |

**IM-186.** (Specified.) An index fault (`E_BOUNDS`, `index.checked.<E>`) records one operand, the index as an `I64`, and the extent as `limit`, an `I64`, for a negative index as for one at or above the extent. `<E>` is the element type, or `struct` for a struct element. For an array or view of rank 2 or more, whether every index is given or only the leading ones (a partial index, SPEC-04 LS-65), the record has two operands, the dimension (`I64`, counted from 0) and then the index (`I64`), and `limit` is that dimension's extent. Every index expression is evaluated before any check (IM-114); the dimensions are then checked in order, and the first that fails is reported. A rank-1 index keeps the one-operand record. A slice whose bounds are not valid (SPEC-04 LS-161) faults `E_BOUNDS` at the slice's `[` with operation `slice.checked.<E>`: the operands are the dimension (`I64`, at rank 2 and above only, as for an index) and then the two bounds as `I64`, as written, after omitted bounds are filled in (LS-161); there is no `exact`, and `limit` is that dimension's extent. `a[lo..=hi]` is checked as `a[lo..hi + 1]` with the addition exact, so `hi` at the `I64` maximum faults `E_BOUNDS`, not `E_OVERFLOW`.

**IM-187.** (Specified.) A run-time shape mismatch in array assignment `b = a`, an array initializer `T[e] b = a`, or `copy(dst, src)` faults with `E_SHAPE`, operation `copy.shape`. The operands are, in order, the lowest differing dimension as an `I64` numbered from 0, then the source extent in that dimension as an `I64`. The `limit` is the destination extent in that dimension as an `I64`; `exact` is absent. Extents are logical element counts, including the registered logical count for zero-byte elements, not byte counts. The position is the assignment or declaration `=` token, or the callee name `copy` (SPEC-04 LS-278). The check runs after destination-place and source evaluation in their specified order (IM-98, SPEC-04 LS-139), including evaluation of an initializer's destination extent before its source. It precedes the alias check of SPEC-04 LS-70, any copy fuel charge of IM-139, and every element write. A shape fault stores no elements; effects of earlier expression evaluation remain (IM-99). Statically known mismatches remain C2012. Negative extents are governed by SPEC-04 LS-62.

This `copy.shape` record also applies to an array or view argument copied into a constructor's fixed-extent array field (SPEC-04 LS-77). Every constructor argument is evaluated first, once and in written order, including named arguments (SPEC-04 LS-137). Then array-field shapes are checked in field declaration order, using the lowest differing dimension within each field. All shape checks precede every constructor field copy, any copy fuel charge, and publication of the constructed value. A mismatch uses the constructor callee name as its position (SPEC-04 LS-278), including the member callee name of an import-qualified constructor. Earlier argument effects remain; the failed constructor does not write the surrounding assignment destination. Statically known mismatches remain C2012.

The alias fault of array assignment and `copy(dst, src)` (SPEC-04 LS-70) is `E_ALIAS`, operation `copy.alias`, with no operands, no `exact`, and no `limit`. The position is the assignment `=` token or the callee name `copy`. The alias check runs after the shape check of this clause and before every element write, so a faulting copy stores no elements. The two bounding intervals belong to the diagnostic message, not to the record. A call keeps the `bind.alias` record of its entry checks (SPEC-04 LS-121), slice arguments included.

**IM-135.** The rows for other sections fix only the spelling; the faults and operands are owned by the section named. The second type of `tdot.checked.<W>.<X>` is the activation type, an exception to the second-type rule above.

**IM-136.** SIR opcodes are owned by SPEC-09. They should be spelled with these identifiers; where they are not, SPEC-09 must give a complete mapping table, and the fault record always carries the identifier defined here.

## 10. Fuel accounting

### 10.1 Decision (Specified)

**IM-137.** Fuel is a budget of counted steps (calls, loop iterations, reductions, and kernel dispatches; section 10.2) that bounds how many charged steps one entry can take. Fuel is an execution budget, counted in `fuel-v1` units at the charge points of section 10.2. It is not elapsed time and not a count of physical CPU or GPU instructions: an entry's fuel consumed is the same on every conforming implementation and at every optimization level, while its running time and instruction count are not. Wasmtime uses fuel in the same sense of a deterministic budget: "Fuel-based interruption is completely deterministic: the same program run with the same amount of fuel will always be interrupted at the same location in the program (unless it has enough fuel to complete its computation, or there is some other form of non-determinism that causes the program to behave differently)" [20], and Wasmtime recommends it over epoch-based interruption for deterministic execution [21]. Wasmtime's charge model has not been compared with `fuel-v1`'s, so it is cited for determinism only. Fuel is a contract, defined by a versioned fuel model, not by statement counts. The `cint-core-1` fuel model is `fuel-v1`. Fuel is accounted per entry (section 9.4): each entry has an allowance `B`, an `I64` with `B >= 0`, and its consumption starts at 0. A host value that is not an `I64` with `B >= 0` (for example `2^63` passed as an unsigned 64-bit value) is refused at entry. An entry with no allowance has no `E_FUEL` from its allowance and still counts fuel; a charge that would carry its count past `INT64_MAX` faults `E_FUEL` with `limit` `I64 9223372036854775807` (no program can reach it in practice, but the rule says what happens). The fuel consumed by an entry is part of its canonical result (section 11.4); fuel is not cumulative across entries and is not part of canonical state.

**IM-138.** Per-statement step counting, as in `cint-bt27-legacy`, is not part of `cint-core-1`. This section makes no performance claim.

### 10.2 `fuel-v1` charge points (Specified)

**IM-139.** This list is exhaustive. Anything not listed charges nothing.

| Charge point | Units | Status |
|---|---:|---|
| Entry to a user-function call, including the entry function (after the depth check, before parameters are bound) | 1 | Specified |
| Start of each loop iteration of `for`, `for .. in`, `while`, and `do`, including the first: after the loop condition evaluates `true` and before the body executes | 1 | Specified |
| Operators, conversions, and named scalar operations of sections 4 and 5 at any width (`abs`, `muldiv`, `isqrt`, `div_result`, and the others), even when written in call syntax | 0 | Specified |
| Reduction over `n` elements (`sum`, `fold_checked`, `sum_wrap`, `sum_sat`, `min(xs)`, `max(xs)`, `count`, and `dot`), charged once before any element is read | `ceil(n / 64)` (0 for `n = 0`) | Specified |
| Array assignment and array initializers, and built-in operations of other sections that iterate over elements (`copy`, `fill`, packing, `tdot`) | 0 in `fuel-v1` (SPEC-05 TR-FUEL-1); a per-element charge is left to a successor fuel model | Specified |
| Kernel dispatch with `N` work items, charged once after admission (section 9.3) and before any work item runs; loop iterations and calls inside a work item then charge as above, in ascending work-item order (SPEC-02 X-8) | `N` | Specified |

**IM-140.** Scalar named operations are free because each has a cost bounded by its operand width, and the number of them evaluated between two charge points is bounded by the program text. A kernel's loops may depend on data: its dispatch charges `N`, and the loop iterations and calls of each work item charge as they occur (SPEC-02 X-8), so no per-work-item bound is computed.

**IM-141.** When a charge would make the entry's consumed fuel exceed `B`, the charge point faults `E_FUEL` (operation `fuel.charge`, `limit` = `B`): the call is not entered, the iteration does not begin, the reduction reads no element, the dispatch does not start. The position of the fault is the loop keyword for an iteration, the callee name for a call and for a reduction, the function's name in its declaration for an entry function, the `test` keyword for a test block, and the first top-level statement for a script's implicit entry. Charge amounts may depend on shapes known at the charge point; SPEC-09 SIR-07 must allow a `fuel.charge` amount computed from extents.

**IM-142.** An optimizer may batch charges (for example, charging a loop's known trip count once), provided the observable result is identical: the same `E_FUEL` position and the same consumed count on every input, including the state at the fault.

```c
I64 total(I64[n] xs) {             // call: 1 unit
    I64 s = 0;
    for (I64 i = 0; i < n; i++) {  // n units, one per iteration
        s = s + xs[i];
    }
    return s;                      // fuel for total(xs) with n = 3: 4 units
}
I64 total2(I64[n] xs) {            // call: 1 unit
    return sum(xs);                // reduction: ceil(n / 64) units; n = 1000 gives 16; total 17
}
```

### 10.3 What fuel bounds (Specified)

**IM-143.** With an allowance, every sequential entry terminates: each loop iteration and call is charged, scalar work between charge points is bounded by the program text, and each reduction is charged in proportion to its length. For kernels this holds only under the Proposed dispatch rule of section 10.2. Until that rule is Specified, statements elsewhere (SPEC-07 D10) that fuel bounds all computation by sandboxed code hold for sequential code only.

## 11. Canonical serialization

### 11.1 Values (Specified)

Canonical means designated as the one representation, or the one selection, by a named and versioned rule: a domain string such as `cint-core-1/state/v1` (IM-152), or the order of IM-112. It says which form is compared, not that the value is correct: two implementations can produce identical canonical bytes and both be wrong.

**IM-144.** All multi-byte integers are little-endian. Signed values are two's complement at their type's width.

**IM-145.** Untagged payload:

| Type | Payload |
|---|---|
| `I8`..`I64`, `U8`..`U64` | `w / 8` bytes |
| `I128`..`I1024` | `w / 8` bytes, limb 0 first (section 2.4) |
| `Q<i>.<f>` | the raw storage integer's payload |
| `Bool` | 1 byte, `00` or `01`; other values are invalid |
| `Z` (mathematical integer, used in fault records) | `U32` byte count `n` (at most 257, or the larger compile-time bounds of IM-108 for an operand or `exact`), then `n` bytes of minimal little-endian two's complement; zero has `n = 0`; no redundant sign byte |

**IM-146.** Type descriptor (tag):

| Tag | Type | Following descriptor bytes |
|---|---|---|
| `01` | `Bool` | none |
| `0f` | `Z` | none |
| `11` `12` `13` `14` | `I8` `I16` `I32` `I64` | none |
| `15` `16` `17` `18` | `I128` `I256` `I512` `I1024` | none |
| `21` `22` `23` `24` | `U8` `U16` `U32` `U64` | none |
| `31` | `Q<i>.<f>` | storage tag (`11` to `14`), then `f` as `U16`; `f` must be at most `w - 1` |
| `41` `42` | `T1`, `T27` | none; payload owned by the ternary section |
| `43` `44` | `PT5`, `PT4` packed trit arrays (Proposed tags; descriptor and payload owned by SPEC-05 TR-SER-1) | rank `U8`, then each extent `I64` in trits |
| `51` | Array | element descriptor, rank `U8` (1 to 8), each extent `I64`; payload is the elements' untagged payloads in row-major order |
| `61` | View identity (fault records only) | element descriptor, buffer identifier `U64` and generation `U64` (logical, assigned by SPEC-03 M-10), rank `U8` (1 to 8), origin offset `I64`, then for each dimension extent `I64`, declared lower bound `I64`, and stride `I64`, then permission byte (`00` read, `01` write); no placement (SPEC-02 V-13), no payload, no element contents |
| `71` | Struct (Proposed tag) | qualified struct name (`U32` length and UTF-8); payload is each field's untagged payload in declaration order, with no padding bytes, bit-field storage units as their storage type (SPEC-04 LS-82) |
| `7f` | Redacted (fault records only) | the type descriptor of the redacted value; no payload |

**IM-147.** A tagged value is its descriptor followed by its payload.

**IM-148.** Decoder obligations (Specified). Canonical bytes, including checkpoints passed to restore, are untrusted input. A decoder must reject, before allocating memory for the payload:

- unknown tags, and tags `61` and `7f` outside a fault record;
- invalid `Bool` bytes and non-minimal `Z` encodings;
- a `Z` longer than the IM-108 bound for its field and record kind: 257 bytes, except in a fault record of encoding `cint-core-1/fault/v2` decoded with the compile-time record kind, where an operand may have 513 bytes and `exact` 520. The caller passes the record kind to the decoder: compile-time for a record from a compiler's diagnostic carrier (SPEC-09 CINTC-14) or a retained diagnostic; run-time for a record from `cint_fault_get` or embedded in state or a checkpoint (IM-156). The bytes do not show the kind, so a record decoded under the wrong kind is rejected when a field exceeds that kind's bound. Under `cint-core-1/fault/v1` the bound is 257 bytes for every Z;
- a `Q` descriptor whose storage tag is not `11` to `14` or whose `f` exceeds `w - 1` (that is, `i < 1`);
- an array or view rank of 0 or above 8, or a negative extent;
- an array whose payload size, computed exactly as the product of the extents times the element size, exceeds `M64` or differs from the number of bytes remaining for it;
- any length or count field larger than the bytes remaining;
- truncated input and trailing bytes.

| Value | Tagged bytes (hex) |
|---|---|
| `I64` -2 | `14 fe ff ff ff ff ff ff ff` |
| `I64` 1000 | `14 e8 03 00 00 00 00 00 00` |
| `U8` 255 | `21 ff` |
| `Q16.16` 1.5 | `31 13 10 00 00 80 01 00` |
| `I128` `2^64` | `15 00 00 00 00 00 00 00 00 01 00 00 00 00 00 00 00` |
| `Z` 9223372036854775808 | `0f 09 00 00 00 00 00 00 00 00 00 00 80 00` |
| `Z` -1 | `0f 01 00 00 00 ff` |
| `I8[3]` {120, 120, -120} | `51 11 01 03 00 00 00 00 00 00 00 78 78 88` |
| redacted `I64` | `7f 14` |

### 11.2 Fault records (Specified content; Specified layout, version 2)

**IM-149.** The content is section 9.2. The canonical fault record is these bytes; any host-language structure is a non-canonical view of them (section 1.4). Specified layout, version 2, frozen: domain string `cint-core-1/fault/v2` (`U32` length then bytes), code `U16`, operation (`U32` length then ASCII), operand count `U32` then tagged values, presence byte and tagged `Z` for `exact`, presence byte and tagged `limit`, position (path `U32` length and UTF-8, line `U32`, column `U32`), presence byte and revision (32 bytes), presence byte and source-map digest (32 bytes, SPEC-09 SIR-17), presence byte and address (kernel qualified name as `U32` length and UTF-8, dispatch number `I64`, phase `U8` with 0 entry, 1 work-item, 2 epilogue, then a presence byte and, in the work-item phase, work-item index `I64` and step ordinal `I64`), stack count `U32` then positions. An absent revision or source-map digest is a presence byte 0, never 32 zero bytes. Version 1, `cint-core-1/fault/v1`, which `cint-rt-1` writes, has the revision as 32 bytes with no presence byte and no source-map digest; a version 1 record stays readable as version 1. A change to the version 2 layout is a new version with its own domain string, and a version 2 record stays readable as version 2. A compile-time diagnostic record and a run-time record share the domain string, so the record kind is not in the bytes; the reader passes it (IM-148). For targets of at most 64 bits a compile-time record is at most 2,252 bytes, and in `cint-core-1` at most 2,372 (an `I1024` limit with a 513-byte `exact`); this is arithmetic on the layout, not a measurement.

### 11.3 State and hashing (Specified principles and ownership; Proposed layout)

Specified principles:

- **IM-150.** This section owns the state framing, the domain strings, and the revision identity. Other sections that define parts of state (arenas, pools, registry, effect log) own the content of their blocks and reference this framing.
- **IM-151.** Canonical state contains logical values only: typed values, declared lengths, stable logical references. It never contains native pointers, device allocation handles, or padding bytes.
- **IM-152.** Every serialized object begins with a length-delimited domain string of the form `<profile>/<kind>/v<n>`, lowercase ASCII (for example `cint-core-1/state/v1`), so bytes of different kinds or versions can never collide.
- **IM-153.** All lengths and counts are explicit; there are no terminators.
- **IM-154.** The state hash is SHA-256 of the canonical bytes [22]. A CINT implementation must compute SHA-256 itself, and the conformance suite must check it against the standard test vectors (for example, SHA-256 of the empty string is `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` [23]).
- **IM-155.** Per-entry quantities (fuel allowance, fuel consumed, dispatch counter, depth limit) are not state.

**IM-156.** Proposed layout, in this order: domain string `cint-core-1/state/v1`; revision (32 bytes, section 11.4); profile name (`U32` length and ASCII); globals as count `U64` then each field as fully qualified name (`U32` length and UTF-8) and tagged value, sorted by the bytes of the name; arena block; pool block; registry block (each a `U64` length followed by bytes beginning with the owning section's domain string; the arena and pool blocks have length 0 until arenas and pools hold state); counters: the next buffer identifier, the next arena identifier, the next pool identifier (SPEC-03 M-11, M-13), and the entry sequence number, each a `U64`; fuel model name; fault record (presence byte and section 11.2 bytes; a boundary fault record of SPEC-03 A-6 takes the same form, as A-6 proposes); effect-log position (`I64`), which clearing a fault does not advance (SPEC-03 A-7a). The order follows SPEC-03 M-3, which also fixes the set of items. The embedded fault record carries its own domain string, so its encoding version is visible inside the state bytes; it is bounded as a run-time record (IM-108). State hashes are compared only within one runtime contract version (SPEC-09 RCPT-08), because the fault record layout, and the revision in the state bytes, change with it: rebuilding an unchanged program against a new runtime contract version changes its revision identity and so its state hash (SPEC-03 H-15).


### 11.4 Revision identity and execution identity (Specified)

**IM-157.** Revision identity. The revision of a program is SHA-256 over: the domain string `cint-core-1/revision/v1`; the profile name; the runtime contract version (`U32` length and ASCII, defined by the host section); the module count `U32`; then, for each module of the program (the root module and every module it imports, transitively), sorted by module path bytes, the module path (`U32` length and UTF-8) and the canonical SIR encoding of that module (`U64` length and bytes, owned by SPEC-09). It is computed over meaning, not over source formatting and not over any target executable or kernel binary. This value is the `revision` of fault records, the revision of state, and SPEC-03's code revision identity; there is no second program hash. An independent reference that reports `revision` must implement the SIR canonical encoding, which is normative for that purpose; operation-level fixtures (CIF-1) do not involve `revision`.

**IM-158.** Canonical arguments. The arguments of an entry are encoded as: domain string `cint-core-1/args/v1`, the entry identifier (`U32` length and UTF-8 fully qualified name), argument count `U32`, then each argument as a tagged value; a non-context-owned buffer (borrowed or bridge-owned, SPEC-03 1.3) is encoded as its array value (full contents). The arguments hash is SHA-256 of these bytes.

**IM-159.** Execution identity and execution agreement (Specified). The execution identity of an entry is the input tuple

```text
(profile, fuel model, revision, source-map digest, entry identifier,
 arguments hash, input state hash, supplied-effects hash, fuel allowance,
 depth limit, frame-arena capacity)
```

Two executions with equal execution identity agree (execution agreement) when they produce equal

```text
(output state hash, canonical return value bytes or absence,
 canonical fault record bytes or absence, fuel consumed by the entry,
 effect-request trace)
```

The effect-request trace is the ordered sequence of effect requests the entry issued, each with its ordinal, call site, function or service name, and canonical arguments, plus the request bytes of an output effect (SPEC-03 H-7; a print statement, SPEC-04 LS-193), up to the entry's end or fault. A program's stdout is the in-order concatenation of the request bytes of its print effects, so equal traces give byte-equal stdout, including bytes written before a fault (SPEC-04 LS-197). Equal state shows only an equal effect count, because the state holds the effect-log position (IM-156).

**IM-160.** The supplied-effects hash is SHA-256 over what the host supplied to the entry, in ordinal order: each effect's status, its canonical result, and the contents it wrote to `out` views (SPEC-03 H-5; the `effect` row of H-14). It excludes every request the entry sends out. Input contents are not in it: IM-158 encodes an array argument by its full contents in the arguments hash, for a borrowed buffer and for a bridge-owned buffer alike, so the entry event itself (SPEC-03 H-14) stays out of this hash. These documents use "deterministic" only in this sense: the same execution identity gives the same five outcomes. Host time, scheduling, native addresses, and unrecorded host effects are not inputs. A conforming implementation agrees with the reference definition on every execution in scope. An execution ended by a host resource failure (section 9.1) has no execution identity. Build identity is separate: an x86-64 executable and a kernel binary of the same program, built against the same runtime contract version, have different build identities (their backends differ) and the same revision identity. Execution identity says nothing about numerical adequacy relative to a floating-point original: a deterministic result can still be numerically inadequate, which is a separate question with its own evidence.

## 12. Balanced-ternary implementations (Specified)

Because every type here is defined by a range in Z and every operation by its exact result in Z plus a form rule, a balanced-ternary [1] backend or processor can implement `cint-core-1` with the same meaning, provided it follows these rules:

- **IM-161.** Representation: `I64` needs at least 41 balanced trits, since `(3^41 - 1) / 2 = 18236498188585393201 >= 2^63` while `(3^40 - 1) / 2 = 6078832729528464400 < 2^63`. Range checks are always against the binary bounds of section 2.1, never against the trit capacity.
- **IM-162.** For binary integer types, wrapping forms, bitwise operators, `as%`, and `<<%` are defined on two's complement bit patterns modulo `2^w`; a ternary implementation computes them by explicit reduction modulo `2^w`. They are not trit operations.
- **IM-163.** `/` and `%` are floor division on values; `>>` is floor division by `2^k`. Neither is trit deletion.
- **IM-164.** The balanced-ternary rescaling operation `rescale3` (deleting the low `k` trits, which rounds `x / 3^k` to the nearest integer, without ties because `3^k` is odd) is a distinct named operation owned by the ternary section; for example 5 rescaled by one trit is 2, while `5 / 3` is 1. Its count range and its `E_SHIFT` condition are declared by that section (section 9.1).
- **IM-165.** `T1` and `T27` (range `+/-3812798742493`, matching `cint-bt27-legacy`) are distinct types. Checked conversion `as` between them and binary types is range-based and follows section 4.9. Wrapping arithmetic on ternary types and `as%` to a ternary type are owned by the ternary section (balanced residue modulo `3` for `T1` and `3^27` for `T27`); `wrap` of section 1.2 does not apply to them. `as%` from a ternary type to a binary type is `wrap` of the binary target.
- **IM-166.** Canonical serialization is the binary encoding of section 11 regardless of the executing hardware.

## 13. Conformance fixtures

### 13.1 Fixture formats (Proposed)

**IM-167.** Two layers. Operation-level fixtures (CIF-1, this section) test one operation on given operands and are what the integer-machine suite contains. Program-level cases (`.ci` plus `.expect`, SPEC-09 CONF-01) test whole programs, including evaluation order, fuel, and compile errors. Every CIF-1 fixture maps mechanically to a program-level case: a `.ci` file that declares the arguments with their types, applies the operation once in an entry, and an `.expect` file whose `op`, `code`, `exact`, `limit`, and operand lines are the CIF-1 values with the same operation identifier (section 9.8). Fields that differ in name between the layers are mapped as `position` to `site`, operands in order to `left` and `right` for binary operations.

**IM-168.** CIF-1 fixtures are JSON Lines files [24] (a published convention with no RFC or ISO standard), one fixture per line, UTF-8, LF line endings, keys in the order shown. File naming: `conformance/integer-machine/<group>.cif1.jsonl`. All integers are JSON strings of decimal digits with an optional leading `-`, so no JSON parser loses precision. RFC 8259 notes that integers are exactly interoperable only in `[-(2^53)+1, 2^53-1]` [25].

| Key | Content |
|---|---|
| `id` | Unique fixture identifier, for example `div.checked.i64.007` |
| `status` | `S` or `P` (section 1.1) |
| `op` | Operation identifier (section 9.8), including the mode when the operation rounds |
| `args` | Array of typed values: `{"t": "I64", "v": "-7"}`; fixed point uses raw: `{"t": "Q16.16", "raw": "98304"}`; arrays: `{"t": "I8[3]", "v": ["120", "120", "-120"]}` |
| `expect` | Exactly one of `{"value": <typed value>}`, `{"fault": {...}}`, `{"compile_error": "<code or TYPE>"}`, `{"reject": true}` (decoder fixtures) |

**IM-169.** A `fault` object has `code` and, when the record has them, `exact`, `limit`, `index` (for `fold_checked`), and `operands` (array of typed values, when the fixture asserts them).

```json
{"id":"add.checked.i64.001","status":"S","op":"add.checked.i64","args":[{"t":"I64","v":"9223372036854775807"},{"t":"I64","v":"1"}],"expect":{"fault":{"code":"E_OVERFLOW","exact":"9223372036854775808","limit":"9223372036854775807"}}}
{"id":"rem.checked.i64.003","status":"S","op":"rem.checked.i64","args":[{"t":"I64","v":"7"},{"t":"I64","v":"-2"}],"expect":{"value":{"t":"I64","v":"-1"}}}
{"id":"mul.checked.q16_16.002","status":"S","op":"mul.checked.q16_16.half_even","args":[{"t":"Q16.16","raw":"1"},{"t":"Q16.16","raw":"98304"}],"expect":{"value":{"t":"Q16.16","raw":"2"}}}
{"id":"fold_checked.add.i8.001","status":"S","op":"fold_checked.add.i8","args":[{"t":"I8","v":"0"},{"t":"I8[3]","v":["120","120","-120"]}],"expect":{"fault":{"code":"E_OVERFLOW","exact":"240","limit":"127","index":"1"}}}
```

**IM-170.** The independent reference (Proposed location `tools/cint_ref/`) is written in Python using only built-in integers [26] and `fractions.Fraction` [27], without reading the C implementation. Every implementation, the reference included, must produce identical canonical results for every fixture. Fixtures are frozen: a semantic change produces a new fixture group under a new profile, never an in-place edit; an encoding change that carries the same content produces a new encoding version, and fixtures of the old version stay valid in their own format (SPEC-00 section 1).

### 13.2 Boundary-case fixture table

**IM-171.** `M = 9223372036854775807`, `m = -9223372036854775808`. Column S/P gives each row's status: S rows are Specified expectations; P rows depend on a Proposed rule or an Open question and must not be relied on until it is settled. Rows marked "program" are program-level cases.

| # | S/P | Operation | Inputs | Expected |
|---:|---|---|---|---|
| 1 | S | `add.checked.i64` | `M`, 1 | `E_OVERFLOW`, exact 9223372036854775808 |
| 2 | S | `add.checked.i64` | 9223372036854775800, 12 | `E_OVERFLOW`, exact 9223372036854775812 |
| 3 | S | `add.wrap.i64` | `M`, 1 | -9223372036854775808 |
| 4 | S | `add.sat.i64` | `M`, 1 | 9223372036854775807 |
| 5 | S | `sub.checked.i64` | `m`, 1 | `E_OVERFLOW`, exact -9223372036854775809 |
| 6 | S | `sub.wrap.i64` | `m`, 1 | 9223372036854775807 |
| 7 | S | `sub.checked.u8` | 3, 5 | `E_OVERFLOW`, exact -2 |
| 8 | S | `sub.wrap.u8` | 3, 5 | 254 |
| 9 | S | `sub.sat.u8` | 3, 5 | 0 |
| 10 | S | `mul.checked.i64` | `m`, -1 | `E_OVERFLOW`, exact 9223372036854775808 |
| 11 | S | `mul.wrap.i64` | `m`, -1 | -9223372036854775808 |
| 12 | S | `mul.checked.i32` | 65536, 32768 | `E_OVERFLOW`, exact 2147483648 |
| 13 | S | `mul.checked.i32` | -65536, 32768 | -2147483648 |
| 14 | S | `mul.wrap.i8` | 100, 3 | 44 |
| 15 | S | `mul.sat.i8` | -128, -1 | 127 |
| 16 | S | `neg.checked.i64` | `m` | `E_OVERFLOW`, exact 9223372036854775808 |
| 17 | S | `neg.checked.u32` | 1 | `E_OVERFLOW`, exact -1 |
| 18 | S | `abs.checked.i8` | -128 | `E_OVERFLOW`, exact 128 |
| 19 | S | `uabs.checked.i8.u8` | -128 | `U8` 128 |
| 20 | S | `div.checked.i64` | -7, 2 | -4 |
| 21 | S | `rem.checked.i64` | -7, 2 | 1 |
| 22 | S | `div.checked.i64` | 7, -2 | -4 |
| 23 | S | `rem.checked.i64` | 7, -2 | -1 |
| 24 | S | `div.checked.i64` | -7, -2 | 3 |
| 25 | S | `rem.checked.i64` | -7, -2 | -1 |
| 26 | S | `div_trunc.checked.i64` | -7, 2 | -3 |
| 27 | S | `rem_trunc.checked.i64` | -7, 2 | -1 |
| 28 | S | `div_euclid.checked.i64` | -7, -2 | 4 |
| 29 | S | `rem_euclid.checked.i64` | 7, -2 | 1 |
| 30 | S | `div.checked.i64` | `m`, 3 | -3074457345618258603 |
| 31 | S | `rem.checked.i64` | `m`, 3 | 1 |
| 32 | S | `div.checked.i64` | `m`, -1 | `E_OVERFLOW`, exact 9223372036854775808 |
| 33 | S | `rem.checked.i64` | `m`, -1 | 0 |
| 34 | S | `div.checked.i64` | 5, 0 | `E_DIV_ZERO` |
| 35 | S | `rem_euclid.checked.i64` | 0, 0 | `E_DIV_ZERO` |
| 36 | S | `div_round.checked.i64.half_even` | 5, 2 | 2 |
| 37 | S | `div_round.checked.i64.half_away` | -5, 2 | -3 |
| 38 | S | `shl.checked.i64` | 1, 63 | `E_OVERFLOW`, exact 9223372036854775808 |
| 39 | S | `shl.checked.i64` | -1, 63 | -9223372036854775808 |
| 40 | S | `shl.wrap.i64` | 1, 63 | -9223372036854775808 |
| 41 | S | `shl.checked.i64` | 1, 64 | `E_SHIFT`, limit 63 |
| 42 | S | `shl.wrap.i64` | 1, -1 | `E_SHIFT`, limit `I64` 63 (IM-45) |
| 43 | S | `shl.wrap.u8` | 255, 4 | 240 |
| 44 | S | `shr.checked.i64` | -7, 1 | -4 |
| 45 | S | `shr.checked.i64` | -1, 63 | -1 |
| 46 | S | `shr.checked.u64` | 18446744073709551615, 63 | 1 |
| 47 | S | `shr.checked.i64` | 1, 64 | `E_SHIFT` |
| 48 | S | `as.checked.i64.i32` | 2147483648 | `E_NARROW`, exact 2147483648 |
| 49 | S | `as.wrap.i64.i32` | 2147483648 | -2147483648 |
| 50 | S | `as.checked.i64.u64` | -1 | `E_NARROW`, exact -1 |
| 51 | S | `as.wrap.i64.u64` | -1 | 18446744073709551615 |
| 52 | S | `as.wrap.u64.i64` | 9223372036854775808 | -9223372036854775808 |
| 53 | S | `as.wrap.i64.i8` | -129 | 127 |
| 54 | S | program: literal `I8` | `128` | compile error `E_NARROW` |
| 55 | S | program: literal `I8` | `-128` | -128 |
| 56 | S | program: literal `I32` | `0xFFFFFFFF` | compile error `E_NARROW` |
| 57 | S | `mul_full.checked.i64.i128` | `m`, `m` | `I128` 85070591730234615865843651857942052864 |
| 58 | S | `muldiv.checked.i64.i64.floor` | `M`, `M`, `M` | 9223372036854775807 |
| 59 | S | `muldiv.checked.i64.i64.floor` | `M`, 3, 2 | `E_OVERFLOW`, exact 13835058055282163710 |
| 60 | S | `add.checked.i128` | `2^127 - 1`, 1 | `E_OVERFLOW`, exact 170141183460469231731687303715884105728 |
| 61 | S | `isqrt_round.checked.i128.half_even` | `2^65` | 6074001000 |
| 62 | S | `mul.checked.q16_16.half_even` | raw 98304, raw 98304 | raw 147456 |
| 63 | S | `mul.checked.q16_16.half_even` | raw 1, raw 98304 | raw 2 |
| 64 | S | `mul.checked.q16_16.floor` | raw -1, raw 98304 | raw -2 |
| 65 | S | `mul.checked.q16_16.trunc` | raw -1, raw 98304 | raw -1 |
| 66 | S | `mul.checked.q16_16.half_even` | raw -1, raw 32768 | raw 0 |
| 67 | S | `mul.checked.q16_16.half_even` | raw 16777216, raw 8388608 | `E_OVERFLOW`, exact raw 2147483648 |
| 68 | S | `mul.sat.q16_16.half_even` | raw 16777216, raw 8388608 | raw 2147483647 |
| 69 | S | `div.checked.q16_16.half_even` | raw 131072, raw 196608 | raw 43691 |
| 70 | S | `div.checked.q16_16.floor` | raw -65536, raw 196608 | raw -21846 |
| 71 | S | `div.checked.q16_16.half_even` | raw 65536, raw 0 | `E_DIV_ZERO` |
| 72 | S | `sqrt.checked.q16_16.floor` | raw 131072 | raw 92681 |
| 73 | S | `sqrt.checked.q16_16.half_even` | raw 131072 | raw 92682 |
| 74 | S | `sqrt.checked.q32_32.half_even` | raw 8589934592 | raw 6074001000 |
| 75 | S | `sqrt.checked.q16_16.half_even` | raw -65536 | fault `E_DOMAIN` (IM-52) |
| 76 | S | `as.checked.q32_32.q16_16.half_even` | raw 1 | raw 0 |
| 77 | S | `as.checked.q32_32.q16_16.half_even` | raw 140737488355328 (32768.0) | `E_NARROW`, exact raw 2147483648 |
| 78 | S | `as.checked.q16_16.i32.half_even` | raw -163840 (-2.5) | -2 |
| 79 | S | `as.checked.q16_16.i32.floor` | raw -163840 | -3 |
| 80 | S | `sum.checked.i8.i8` | {120, 120, -120} | 120 |
| 81 | S | `fold_checked.add.i8` | init 0, {120, 120, -120} | `E_OVERFLOW`, index 1, exact 240 |
| 82 | S | `sum_sat.sat.i8` | {120, 120, -120} | 7 |
| 83 | S | `sum_sat.sat.i8` | {120, -120, 120} | 120 |
| 84 | S | `sum_wrap.wrap.i8` | {127, 1} | -128 |
| 85 | S | `sum.checked.i8.i8` | {127, 127, 127} | `E_OVERFLOW` (final range check), exact 381, limit 127 |
| 86 | S | `sum.checked.i8.i16` | {127, 127, 127} | 381 |
| 87 | S | `sum.checked.i64.i64` | {`M`, 1, -1} | 9223372036854775807 |
| 88 | S | `fold_checked.add.i64` | init 0, {`M`, 1, -1} | `E_OVERFLOW`, index 1, exact 9223372036854775808 |
| 89 | S | `sum.checked.i64.i64` | {} | 0 |
| 90 | S | `reduce_min.checked.i64` | {} | `E_SHAPE` |
| 91 | S | `reduce_max.checked.i64` | {-1, `m`} | -1 |
| 92 | S | program: order | `(x + 1) / y`, `x = M`, `y = 0` | `E_OVERFLOW` |
| 93 | S | serialize `I128` | `2^64` | `15 00 00 00 00 00 00 00 00 01 00 00 00 00 00 00 00` |
| 94 | S | serialize `Q16.16` | raw 98304 | `31 13 10 00 00 80 01 00` |
| 95 | S | program: `fuel-v1` | `total` of section 10.2, `n = 3`, allowance 3 | `E_FUEL` at the third iteration's charge point; fuel consumed 3 |
| 96 | S | program: constant | `I16 h = 40000 - 30000;` | compile error `E_NARROW` (literal 40000) |
| 97 | S | program: constant | `I32 v = 2_000_000_000 * 2 / 4;` | compile error `E_OVERFLOW`, exact 4000000000 |
| 98 | S | program: constant | `I64 s = 1 << 100 >> 90;` | compile error `E_SHIFT`, count 100 |
| 99 | S | program: constant | `I8 p = 100 +% 100;` | -56 |
| 100 | S | program: constant | `Q16.16 q = (1.0 / 3.0) * 3.0;` | raw 65535 |
| 101 | S | program: constant | `const I8 K = 100; I16 z = K + K;` | compile error (type mismatch, no fault code) |
| 102 | S | `sqrt.checked.q1_15.ceil` | raw 32767 | `E_OVERFLOW`, exact 32768, limit 32767 |
| 103 | S | `sqrt.checked.q1_15.away` | raw 32767 | `E_OVERFLOW`, exact 32768 |
| 104 | S | `sqrt.checked.q1_15.floor` | raw 32767 | raw 32767 |
| 105 | S | `sqrt.checked.q1_63.ceil` | raw `M` | `E_OVERFLOW`, exact 9223372036854775808 |
| 106 | S | `muldiv.checked.i64.i64.half_even` | `M`, 3, 2 | `E_OVERFLOW`, exact 13835058055282163710 |
| 107 | S | `muldiv.checked.i64.i64.ceil` | `M`, 3, 2 | `E_OVERFLOW`, exact 13835058055282163711 |
| 108 | S | program: stores | section 8.2 example, `z = 0` | `E_DIV_ZERO`; `counter` is 1; `y` is not stored |
| 109 | S | program: shift typing | `U8 k = 40; I64 v = 1 << k;` | 1099511627776 |
| 110 | S | program: range bound | `I32 n32 = 10; for i in 0..n32 { }` | compile error (type) |
| 111 | S | decode | `51 14 02` + extents `2^62`, `2^62` + no payload | reject (size exceeds `M64`) |
| 112 | S | decode | `0f` + length 258 + 258 bytes | reject (`Z` too long) |
| 113 | S | decode | `31 13 20 00` + 4 bytes (`Q` with `f = 32` on `I32`) | reject (`i < 1`) |
| 114 | S | decode | `51 11 09` + nine extents | reject (rank 9) |
| 115 | S | decode | `14 fe ff` | reject (truncated) |
| 116 | S | program: depth | `I64 f(I64 n) { return f(n + 1); }` entered as `f(0)`, depth limit 4 | `E_DEPTH` at the fifth call; fuel consumed 4 |
| 117 | S | `rem.checked.i1024` | `(d as I1024)` with `I512 d = -1`, `L` = 2^252 + 27742317777372353535851937790883648493 | 7237005577332262213973186563042994240857116359379907606001950938285454250988 (`L - 1`, the residue of -1); the corrected unsigned value `2^512 - 1` gives 1627715501170711445284395025044413883736156588369414752970002579683115011840 |
| 118 | P | program: dispatch admission | `kernel K[n, m]` with `n = m = 2^32` | `E_SHAPE`, operation `dispatch.admit`, exact `2^64` |
| 119 | P | program: dispatch numbering | host calls an entry that dispatches once, 10 times; checkpoint after call 5; restore; call 7 faults in the kernel | address dispatch number 0 in both the original and the restored run |
| 120 | P | program: Fortran order | section 6.1 example via `transpose` | `E_OVERFLOW` at index 1, exact 200 |
| 121 | S | `reduce` fuel | `sum` over 1000 `I64` elements, entry as `total2` | fuel consumed 17 |

## 14. Sources and precedent

This section is informative and makes no normative statement.

Sources:

| Reference | Source | Scope and caveats |
|---|---|---|
| [3] | W3C, WebGPU Shading Language, sections 6.2.1, "Abstract Numeric Types," 6.2.5, "Scalar Types," and 8.8, "Arithmetic Expressions" | Normative text of a Candidate Recommendation Draft, pinned to the 21 September 2026 snapshot. |

## 15. Open questions

1. Closed: `E_DOMAIN` (code 11) is the fault of `isqrt`, `isqrt_round`, and fixed-point `sqrt` of a negative value (IM-52).
2. Exact rounding mode. Should `Round.exact` exist, faulting when a rounding operation would discard a nonzero remainder? It would need a code such as `E_INEXACT`.
3. Unsigned wide types. Are `U128` to `U1024` needed in `cint-core-1` (for example for `mul_full` of `U64`, SHA-512 [22] or Ed25519 [28] field arithmetic, and a wide logical right shift), or deferred? This must be decided before the security section (SPEC-07 SEC-ED-12) is frozen; until then the digest reduction pattern of section 2.4 applies.
4. Fixed-point storage beyond 64 bits (`Q64.64`) and unsigned fixed point (`UQ`).
5. Closed: `sum(R, xs)` takes the result type as a leading argument and `x as I32 round floor` places the rounding clause after the type; both are confirmed by SPEC-04 6.8 and 6.6.
6. Typed literal suffixes for contexts without a type.
7. Closed: `clamp(x, lo, hi)` with `lo > hi` at run time faults `E_DOMAIN` (IM-49).
8. Closed: kernels with loops that depend on data are admitted. A dispatch charges `N`, then each work item's loop iterations and calls charge as in sequential code (IM-139, SPEC-02 X-8).
9. Final byte layouts of state (section 11.3) are Proposed. Fault record version 2 (section 11.2) is frozen (IM-149); the state blocks, recording events, the checkpoint envelope, and tags `43`, `44`, and `71` stay Open until before the first conformance release.
10. Whether `fold_checked` admits operations beyond `add` and `mul` in `cint-core-1`.
11. Closed: `a..b` is half-open and `a..=b` inclusive everywhere; case labels admit only inclusive ranges (`case 1..=5:`), and `case 1..5:` is compile error C4022 (SPEC-04 8.4, 8.5).
12. `E_READONLY`. Whether a run-time write through a read-only view (SPEC-03 M-27 and its Open question 5) gets its own code or is reported as `E_ALIAS` or `E_BOUNDS`.
13. Not part of this edition. The question concerns public locators for two sources that this edition does not cite.
14. Whether to add a `col` order argument to `fold_checked` and `sum_sat` in addition to `transpose` (section 6.1).
15. Citation gaps. (a) Closed: section 4.4 cites only C, Fortran `MOD`, and WGSL ([3], section 8.8) for truncating division; it makes no general claim about GPU instructions. (b) Closed: section 5.1 says only that TI SPRA109 [13] names the 1.15 format "Q15" by its fraction bits. (c) The Intel manual [11] is pinned by order number. Its update date (21 September 2026) was confirmed on Intel's landing page on 2026-10-02; the IDIV and shift entries were not re-read in that check. (d) Closed: the References list uses the wording of the shared bibliography, [REFERENCES-P.1.md](REFERENCES-P.1.md).

Open questions cited by identifier:

- OQ-124: whether a later profile gives a checked conversion whose source is a literal's value in Z its own operation identifier, with a `z` type token (`as.checked.z.i8`), in place of `unassigned` (IM-184).

Rejected alternatives (one line each):

- Charging wide scalar operations per limb: rejected because each is bounded by its fixed width and their number between charge points is bounded by program text (section 10.2); fuel bounds termination, not cost.
- Excluding redacted fault records from execution identity: rejected because redaction is a function of static types, so redacted bytes are identical across conforming implementations (section 9.2).

## 16. Out of scope

- **IM-172.** Floating-point types and operations of any kind inside CINT. Floating-point values exist only at the host boundary (section 5.7).
- **IM-173.** Array and view layout, indexing, and bounds rules beyond the fault codes and encodings named here (arrays and views section).
- **IM-174.** Kernel syntax, dispatch, aliasing checks, and publication (kernels section).
- **IM-175.** Ternary representations, packed trit encodings, ternary wrapping, `tdot`, and `rescale3` (ternary section).
- **IM-176.** Arenas and handle generations (arenas section).
- **IM-177.** Concurrency between execution contexts, shared-memory races, and atomics.
- **IM-178.** Transcendental functions (trigonometric, exponential, logarithmic). Libraries may provide them on top of this section, with their own accuracy statements.
- **IM-179.** Constant-time execution guarantees for cryptographic code. The cryptography section must state its own requirements. Redaction of secret operands (section 9.2) is in scope; timing is not.
- **IM-180.** Host resource exhaustion and host errors (section 9.1).
- **IM-181.** Performance. No timing, throughput, or cost claim is made by this section.
- **IM-182.** Compliance with MISRA, CERT, or any other standard. No such claim is made without a documented assessment.
- **IM-183.** Numerical adequacy of a port relative to a floating-point original. Execution identity (section 11.4) is a different property.

## References

Entries use the wording of the shared bibliography, REFERENCES-P.1 ([REFERENCES-P.1.md](REFERENCES-P.1.md)); the bracketed key names the shared entry. A "Used here" note names the parts this document relies on.

1. [Knuth-1997] Donald E. Knuth. *The Art of Computer Programming, Volume 2: Seminumerical Algorithms*, 3rd edition. Addison-Wesley, 1997. ISBN 978-0-201-89684-8. Used here: section 4.1, "Positional Number Systems" (balanced ternary).
2. [W3C-WebGPU] W3C GPU for the Web Working Group. *WebGPU*. W3C Candidate Recommendation Draft, 15 September 2026. https://www.w3.org/TR/2026/CRD-webgpu-20260915/ (latest version: https://www.w3.org/TR/webgpu/). Used here: section 3.6.2, "Limits" (`maxStorageBufferBindingSize`).
3. [W3C-WGSL] W3C GPU for the Web Working Group. *WebGPU Shading Language*. W3C Candidate Recommendation Draft, 21 September 2026. https://www.w3.org/TR/2026/CRD-WGSL-20260921/ (latest version: https://www.w3.org/TR/WGSL/). Used here: section 6.2.1, "Abstract Numeric Types"; section 6.2.5, "Scalar Types"; section 8.8, "Arithmetic Expressions" (integer `/` and `%` truncate); and section 17.4.1, `arrayLength`.
4. [ISO-9899-2024] ISO/IEC JTC 1/SC 22. *ISO/IEC 9899:2024, Information technology: Programming languages: C* (C23). International Organization for Standardization, 2024. https://www.iso.org/standard/82075.html. Used here: bit-precise integer types, `_BitInt(N)`.
5. [GCC-int128] Free Software Foundation. "128-bit Integers" (`__int128`). *Using the GNU Compiler Collection (GCC)*. https://gcc.gnu.org/onlinedocs/gcc/_005f_005fint128.html. Read 2026-10-02. Used here: the 128-bit integer extension.
6. [Unicode-18.0] The Unicode Consortium. *The Unicode Standard, Version 18.0.0*. The Unicode Consortium, 2026. ISBN 978-1-936213-36-8. https://www.unicode.org/versions/Unicode18.0.0/. Used here: section 3.9, definition D76 ("Unicode scalar value").
7. [PSF-LangRef] Python Software Foundation. *The Python Language Reference*, Python 3.14. https://docs.python.org/3/reference/. Used here: section 6.7, "Binary arithmetic operations" (floor division `//` and the sign of `%`), https://docs.python.org/3/reference/expressions.html#binary-arithmetic-operations.
8. [Boute-1992] Raymond T. Boute. "The Euclidean definition of the functions div and mod." *ACM Transactions on Programming Languages and Systems* 14(2):127-144, 1992. DOI 10.1145/128861.128862.
9. [ISO-9899-2018] ISO/IEC JTC 1/SC 22. *ISO/IEC 9899:2018, Information technology: Programming languages: C* (C17). International Organization for Standardization, 2018. https://www.iso.org/standard/74528.html. ISO has replaced it with ISO/IEC 9899:2024. Working drafts with the same clause numbering: WG 14 N2176 (ballot text) and N2310, https://www.open-std.org/jtc1/sc22/wg14/www/docs/n2310.pdf. Used here: section 6.5.5, "Multiplicative operators," paragraph 6 (truncation toward zero; `a/b` and `a%b` undefined when the quotient is not representable), checked against N2310. Cited because C17 is the seed compiler's language.
10. [ISO-1539-1-2018] ISO/IEC JTC 1/SC 22. *ISO/IEC 1539-1:2018, Information technology: Programming languages: Fortran: Part 1: Base language* (Fortran 2018). International Organization for Standardization, 2018. https://www.iso.org/standard/72320.html. ISO has replaced it with ISO/IEC 1539-1:2023 (Fortran 2023). Final committee draft: J3/18-007r1, https://j3-fortran.org/doc/year/18/18-007r1.pdf. Used here: section 9.5.3.2 ("Array element order"), section 10.1.5.2.2 (integer division), and sections 16.9.100 (`INT`), 16.9.135 (`MOD`), 16.9.136 (`MODULO`), and 16.9.141 (`NINT`), checked against J3/18-007r1. Cited because Fortran 2018 defines `ISO_Fortran_binding`.
11. [Intel-SDM] Intel Corporation. *Intel 64 and IA-32 Architectures Software Developer's Manual* (Volume 1: Basic Architecture; Combined Volumes 2A to 2D: Instruction Set Reference, order number 325383). Updated 21 September 2026. https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html. Used here: instruction entries IDIV (divide error #DE when the signed quotient is too large) and SAL/SAR/SHL/SHR (count masked to 5 bits, or 6 bits with a 64-bit operand).
12. [Arm-CMSIS-DSP] Arm Limited. "Vector Scale." *CMSIS-DSP* documentation. https://arm-software.github.io/CMSIS-DSP/latest/group__BasicScale.html (legacy CMSIS 5 text: https://arm-software.github.io/CMSIS_5/DSP/html/group__BasicScale.html). Read 2026-10-02; the page states no library version. Used here: `arm_scale_q15`, inputs "in 1.15 format."
13. [TI-SPRA109] J. Stevenson. *Q-Values in the Watch Window*. Application Report SPRA109, Texas Instruments, February 2002. https://www.ti.com/lit/an/spra109/spra109.pdf.
14. [IEEE-754-2019] IEEE. *IEEE Std 754-2019, IEEE Standard for Floating-Point Arithmetic*. IEEE, 2019. DOI 10.1109/IEEESTD.2019.8766229.
15. [Blelloch-1990] Guy E. Blelloch. *Prefix Sums and Their Applications*. Technical Report CMU-CS-90-190, School of Computer Science, Carnegie Mellon University, November 1990. http://www.cs.cmu.edu/afs/cs.cmu.edu/project/scandal/public/papers/CMU-CS-90-190.html.
16. [RFC-3629] F. Yergeau. *UTF-8, a transformation format of ISO 10646*. RFC 3629 (STD 63), November 2003. https://www.rfc-editor.org/rfc/rfc3629.
17. [GCC-Integer-Overflow-Builtins] Free Software Foundation. "Integer Overflow Builtins." *Using the GNU Compiler Collection (GCC)*. https://gcc.gnu.org/onlinedocs/gcc/Integer-Overflow-Builtins.html. Read 2026-10-02.
18. [Clang-LangExt] LLVM Project. "Clang Language Extensions." *Clang documentation*. https://clang.llvm.org/docs/LanguageExtensions.html. Read 2026-10-02. Used here: section "Checked Arithmetic Builtins," https://clang.llvm.org/docs/LanguageExtensions.html#checked-arithmetic-builtins.
19. [GCC-Code-Gen-Options] Free Software Foundation. "Options for Code Generation Conventions." *Using the GNU Compiler Collection (GCC)*. https://gcc.gnu.org/onlinedocs/gcc/Code-Gen-Options.html. Read 2026-10-02. Used here: `-fwrapv`.
20. [Wasmtime-Interrupting] Wasmtime project. "Interrupting Execution." *Wasmtime documentation*. https://docs.wasmtime.dev/examples-interrupting-wasm.html. Read 2026-10-03. The page does not name its publisher in its text; it links to the repository `github.com/bytecodealliance/wasmtime`. Used here: the quoted sentence on fuel (IM-137).
21. [Wasmtime-Deterministic] Wasmtime project. "Deterministic Execution." *Wasmtime documentation*. https://docs.wasmtime.dev/examples-deterministic-wasm-execution.html. Read 2026-10-03. The page does not name its publisher in its text. Used here: fuel recommended over epoch-based interruption for deterministic execution (IM-137).
22. [FIPS-180-4] National Institute of Standards and Technology. *Secure Hash Standard (SHS)*. FIPS PUB 180-4, August 2015. DOI 10.6028/NIST.FIPS.180-4.
23. [NIST-CAVP-SHA-Vectors] National Institute of Standards and Technology, Cryptographic Algorithm Validation Program. SHA test vectors for byte-oriented messages (`shabytetestvectors.zip`). https://csrc.nist.gov/CSRC/media/Projects/Cryptographic-Algorithm-Validation-Program/documents/shs/shabytetestvectors.zip. Used here: file `SHA256ShortMsg.rsp`, entry `Len = 0`.
24. [JSON-Lines] *JSON Lines*. https://jsonlines.org/. A community format description, not a formal standard; the site states that the media type `application/jsonl` is not yet standardized. Read 2026-10-02.
25. [RFC-8259] T. Bray, editor. *The JavaScript Object Notation (JSON) Data Interchange Format*. RFC 8259 (STD 90), December 2017. https://www.rfc-editor.org/rfc/rfc8259. Used here: section 6.
26. [PSF-stdtypes] Python Software Foundation. "Built-in Types." *The Python Standard Library*, Python 3.14. https://docs.python.org/3/library/stdtypes.html. Used here: section "Numeric Types: int, float, complex" ("Integers have unlimited precision").
27. [PSF-fractions] Python Software Foundation. "fractions: Rational numbers." *The Python Standard Library*, Python 3.14. https://docs.python.org/3/library/fractions.html.
28. [RFC-8032] S. Josefsson and I. Liusvaara. *Edwards-Curve Digital Signature Algorithm (EdDSA)*. RFC 8032, January 2017. https://www.rfc-editor.org/rfc/rfc8032.
